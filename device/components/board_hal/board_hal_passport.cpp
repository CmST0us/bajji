// SPDX-License-Identifier: MIT
#include "board_hal.hpp"
#include "passport_buttons.hpp"
#include <algorithm>
#include <array>
#include <cmath>
extern "C" {
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
}
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace bajji {
namespace {
SemaphoreHandle_t ui_mutex, state_mutex;
adc_oneshot_unit_handle_t adc;
adc_cali_handle_t calibration;
PassportButtons keys;
portMUX_TYPE key_mux = portMUX_INITIALIZER_UNLOCKED;
std::int64_t battery_time;
constexpr unsigned kBufferLines = 20;

bool flush_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* context) {
    lv_display_flush_ready(static_cast<lv_display_t*>(context));
    return false;
}
void flush(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels) {
    lv_draw_sw_rgb565_swap(pixels, lv_area_get_width(area) * lv_area_get_height(area));
    if (esp_lcd_panel_draw_bitmap(bsp_display_panel(), area->x1, area->y1,
                                 area->x2 + 1, area->y2 + 1, pixels) != ESP_OK) {
        lv_display_flush_ready(display);
    }
}
void ui_task(void*) {
    for (;;) {
        xSemaphoreTake(ui_mutex, portMAX_DELAY);
        const auto delay = lv_timer_handler();
        xSemaphoreGive(ui_mutex);
        vTaskDelay(pdMS_TO_TICKS(std::clamp<std::uint32_t>(delay, 2, 30)));
    }
}
void button_task(void*) {
    constexpr std::uint16_t windows[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;
    for (;;) {
        int raw = 0, mv = -1;
        PassportButtons::Key key = PassportButtons::Key::none;
        if (adc_oneshot_read(adc, BSP_BTN_ADC_CHANNEL, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(calibration, raw, &mv) == ESP_OK) {
            for (unsigned i = 0; i < BSP_BTN_COUNT; ++i) {
                if (mv >= windows[i][0] && mv < windows[i][1])
                    key = static_cast<PassportButtons::Key>(i + 1);
            }
        }
        portENTER_CRITICAL(&key_mux);
        keys.update(key, esp_timer_get_time() / 1000);
        portEXIT_CRITICAL(&key_mux);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
bool init_buttons() {
    adc_oneshot_unit_init_cfg_t config{};
    config.unit_id = BSP_BTN_ADC_UNIT;
    if (adc_oneshot_new_unit(&config, &adc) != ESP_OK) return false;
    adc_oneshot_chan_cfg_t channel{};
    channel.atten = ADC_ATTEN_DB_12;
    channel.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_cali_curve_fitting_config_t cal{};
    cal.unit_id = BSP_BTN_ADC_UNIT;
    cal.chan = BSP_BTN_ADC_CHANNEL;
    cal.atten = channel.atten;
    cal.bitwidth = channel.bitwidth;
    if (adc_oneshot_config_channel(adc, BSP_BTN_ADC_CHANNEL, &channel) != ESP_OK ||
        adc_cali_create_scheme_curve_fitting(&cal, &calibration) != ESP_OK) return false;
    return xTaskCreate(button_task, "passport_keys", 2048, nullptr, 2, nullptr) == pdPASS;
}
}  // namespace

BoardHal& BoardHal::instance() { static BoardHal board; return board; }
esp_err_t BoardHal::init() {
    // Never erase pairing, network credentials or unrelated NVS on an initialization error.
    ESP_RETURN_ON_ERROR(nvs_flash_init(), "passport", "NVS init failed");
    ui_mutex = xSemaphoreCreateMutex();
    state_mutex = xSemaphoreCreateMutex();
    if (!ui_mutex || !state_mutex) return ESP_ERR_NO_MEM;
    status_.auto_rotation_enabled = false;
    nvs_handle_t settings;
    if (nvs_open("board", NVS_READONLY, &settings) == ESP_OK) {
        std::uint8_t brightness;
        if (nvs_get_u8(settings, "brightness", &brightness) == ESP_OK && brightness <= 100)
            status_.brightness = brightness;
        nvs_close(settings);
    }
    ESP_RETURN_ON_ERROR(bsp_display_init(), "passport", "display init failed");
    lv_init();
    lv_tick_set_cb([]() -> std::uint32_t { return esp_timer_get_time() / 1000; });
    auto* display = lv_display_create(BSP_LCD_W, BSP_LCD_H);
    auto* buffer = heap_caps_malloc(BSP_LCD_W * kBufferLines * 2, MALLOC_CAP_DMA);
    if (!display || !buffer) return ESP_ERR_NO_MEM;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, nullptr, BSP_LCD_W * kBufferLines * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    const esp_lcd_panel_io_callbacks_t callbacks{.on_color_trans_done = flush_done};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(bsp_display_io(), &callbacks,
                        display), "passport", "display callback failed");
    lv_display_set_flush_cb(display, flush);
    status_.display = Health::ok;
    bsp_display_backlight(status_.brightness);
    status_.buttons = init_buttons() ? Health::ok : Health::error;
    status_.pmic = bsp_battery_init() == ESP_OK ? Health::ok : Health::unavailable;
    // Initialize audio only when requested: its DMA buffers compete with Wi-Fi/BLE on C3.
    if (xTaskCreate(ui_task, "lvgl", 6144, nullptr, 1, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
BoardStatus BoardHal::snapshot() {
    if (!state_mutex) return status_;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const auto result = status_;
    xSemaphoreGive(state_mutex);
    return result;
}
void BoardHal::poll(bool) {
    if (!state_mutex) return;
    const auto now = esp_timer_get_time();
    if (now - battery_time < 5000000) return;
    battery_time = now;
    const int soc = bsp_battery_soc(), mv = bsp_battery_mv();
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if (soc >= 0 && mv >= 0) {
        status_.pmic = Health::ok;
        status_.battery_percent = soc;
        status_.battery_mv = mv;
    } else if (status_.pmic == Health::ok) status_.pmic = Health::error;
    xSemaphoreGive(state_mutex);
}
esp_err_t BoardHal::set_brightness(std::uint8_t percent) {
    if (!state_mutex) return ESP_ERR_INVALID_STATE;
    percent = std::min<std::uint8_t>(percent, 100);
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    nvs_handle_t settings = 0;
    auto result = nvs_open("board", NVS_READWRITE, &settings);
    if (result == ESP_OK) result = nvs_set_u8(settings, "brightness", percent);
    if (result == ESP_OK) result = nvs_commit(settings);
    if (settings) nvs_close(settings);
    if (result == ESP_OK) { status_.brightness = percent; bsp_display_backlight(percent); }
    xSemaphoreGive(state_mutex);
    return result;
}
std::uint8_t BoardHal::brightness() const {
    return const_cast<BoardHal*>(this)->snapshot().brightness;
}
void BoardHal::set_auto_rotation_enabled(bool) {}
bool BoardHal::auto_rotation_enabled() const { return false; }
void BoardHal::vibrate(std::uint16_t, std::uint8_t) {}
void BoardHal::stop_vibration() {}
void BoardHal::play_tone(std::uint16_t hz, std::uint16_t duration) {
    if (hz < 20 || !duration || bsp_audio_init() != ESP_OK) return;
    if (bsp_audio_set_format(16000, 16, 1) != ESP_OK) return;
    std::array<std::int16_t, 160> samples{};
    const unsigned count = 16U * std::min<unsigned>(duration, 1000);
    for (unsigned start = 0; start < count; start += samples.size()) {
        const auto length = std::min<unsigned>(samples.size(), count - start);
        for (unsigned i = 0; i < length; ++i)
            samples[i] = std::sin(6.2831853 * std::min<unsigned>(hz, 7000) * (start + i) / 16000) * 4000;
        if (bsp_audio_write(samples.data(), length * sizeof(samples[0])) != ESP_OK) break;
    }
    bsp_audio_sleep();
}
void BoardHal::shutdown() {
    // Power is controlled by a dedicated hardware key; there is no software power latch.
    bsp_display_backlight(0);
    bsp_audio_sleep();
}
ButtonEvents BoardHal::take_button_events() {
    portENTER_CRITICAL(&key_mux);
    const auto events = keys.take_events();
    portEXIT_CRITICAL(&key_mux);
    return events;
}
bool BoardHal::lvgl_lock(std::uint32_t ms) {
    return ui_mutex && xSemaphoreTake(ui_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
void BoardHal::lvgl_unlock() { if (ui_mutex) xSemaphoreGive(ui_mutex); }
const char* health_text(Health health) {
    return health == Health::ok ? "OK" : health == Health::error ? "ERR" : "N/A";
}
}  // namespace bajji
