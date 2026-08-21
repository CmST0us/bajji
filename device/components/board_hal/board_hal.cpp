/*
 * Portions SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "board_hal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "M5GFX.h"
#include "M5IOE1.h"
#include "M5PM1.h"
#include "audio_codec_data_if.h"
#include "audio_codec_if.h"
#include "audio_codec_ctrl_if.h"
#include "board_math.hpp"
#include "button_state.hpp"
#include "bmi270_bmm150.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "lgfx/v1/panel/Panel_CO5300.hpp"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace bajji {
namespace {

constexpr char kTag[] = "board";
constexpr gpio_num_t kSda = GPIO_NUM_47;
constexpr gpio_num_t kScl = GPIO_NUM_48;
constexpr gpio_num_t kButtonA = GPIO_NUM_2;
constexpr gpio_num_t kButtonB = GPIO_NUM_1;
constexpr gpio_num_t kSpeakerPa = GPIO_NUM_14;
constexpr gpio_num_t kDisplayCs = GPIO_NUM_39;
constexpr gpio_num_t kDisplayTe = GPIO_NUM_38;
constexpr int kSampleRate = 44100;
constexpr int kDisplayPowerAttempts = 25;
constexpr int kDisplayPowerReadyMs = 80;
constexpr int kDisplayPowerSettleMs = 50;
constexpr int kIoeWriteAttempts = 5;
constexpr int kIoeWriteRetryMs = 2;
constexpr char kBoardNvsNamespace[] = "board";
constexpr char kBrightnessKey[] = "brightness";
// One buffer tall enough for the whole screen: LVGL renders each invalidated area in a
// single pass, so there are no chunk seams, while the panel still pushes only the dirty
// rect. The flush is synchronous, so a second buffer would buy nothing.
constexpr int kLvglBufferLines = 468;

i2c_bus_handle_t i2c_bus = nullptr;
i2c_master_bus_handle_t native_i2c_bus = nullptr;
std::unique_ptr<M5PM1> pmic;
std::unique_ptr<M5IOE1> ioe;
i2c_master_dev_handle_t touch_device = nullptr;
i2c_master_dev_handle_t rtc_device = nullptr;
bmi270_bmm150_handle_t imu = nullptr;
SemaphoreHandle_t lvgl_mutex = nullptr;
esp_codec_dev_handle_t codec = nullptr;
ButtonState buttons;
std::int64_t motor_stop_us = 0;
std::int64_t battery_poll_us = 0;
std::int64_t sensor_poll_us = 0;

// Mirrors m5gfx::Panel_StopWatch (M5GFX.cpp:820-846), which M5GFX.cpp defines at file
// scope and does not export in a header. Panel_CO5300 supplies the depths and
// dummy_read_pixel; only the panel geometry and the init list differ.
class PanelStopWatch final : public lgfx::Panel_CO5300 {
public:
    PanelStopWatch() {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
    }

    const std::uint8_t* getInitCommands(std::uint8_t list_number) const override {
        static constexpr std::uint8_t commands[] = {
            0x11, 0 + CMD_INIT_DELAY, 150,
            0xC4, 1, 0x80,
            0x35, 1, 0x80,
            0x44, 2, 0x01, 0xD2,
            0x53, 1, 0x20,
            0x20, 0,
            0x36, 1, 0,
            0x51, 1, 0xA0,
            0x29, 0,
            0xff, 0xff,
        };
        return list_number == 0 ? commands : nullptr;
    }
};

class StopWatchDisplay final : public M5GFX {
public:
    bool init_impl(bool use_reset, bool use_clear) override {
        auto bus_config = bus_.config();
        bus_config.freq_write = 80000000;
        bus_config.freq_read = 10000000;
        bus_config.pin_sclk = GPIO_NUM_40;
        bus_config.pin_io0 = GPIO_NUM_41;
        bus_config.pin_io1 = GPIO_NUM_42;
        bus_config.pin_io2 = GPIO_NUM_46;
        bus_config.pin_io3 = GPIO_NUM_45;
        bus_config.spi_host = SPI2_HOST;
        bus_config.spi_mode = 0;
        bus_config.spi_3wire = true;
        bus_config.dma_channel = SPI_DMA_CH_AUTO;
        bus_.config(bus_config);
        panel_.setBus(&bus_);

        auto panel_config = panel_.config();
        panel_config.pin_rst = GPIO_NUM_NC;
        panel_config.pin_cs = kDisplayCs;
        panel_config.panel_width = 468;
        panel_config.panel_height = 468;
        panel_config.offset_x = 6;
        panel_config.offset_y = 0;
        panel_config.readable = false;
        panel_.config(panel_config);
        setPanel(&panel_);
        lgfx::pinMode(kDisplayTe, lgfx::pin_mode_t::input_pullup);

        if (!LGFX_Device::init_impl(use_reset, use_clear)) return false;

        // M5GFX.cpp:1131-1139. Drawing lands in a PSRAM framebuffer and the panel pushes
        // only the dirty rect, so an update fits in the vertical blanking interval instead
        // of streaming a whole frame past the scan line. The framebuffer also rounds the
        // dirty rect out to even bounds (Panel_AMOLED.cpp:677-683), which is what makes
        // odd-origin drawing render correctly.
        if (panel_.initPanelFb()) {
            auto* framebuffer = panel_.getPanelFb();
            if (framebuffer) {
                framebuffer->setBus(&bus_);
                framebuffer->setAutoDisplay(true);
                setPanel(framebuffer);
            }
        } else {
            ESP_LOGW(kTag, "panel framebuffer unavailable; falling back to direct draw");
        }

        panel_.setBrightness(display_duty(60));
        return true;
    }

    void set_brightness(std::uint8_t percent) { panel_.setBrightness(display_duty(percent)); }

private:
    lgfx::Bus_SPI bus_;
    PanelStopWatch panel_;
};

std::unique_ptr<StopWatchDisplay> display;

bool read_register(i2c_master_dev_handle_t device, std::uint8_t reg, void* data, std::size_t size) {
    return device && i2c_master_transmit_receive(device, &reg, 1, static_cast<std::uint8_t*>(data), size, 50) == ESP_OK;
}

bool add_i2c_device(std::uint8_t address, std::uint32_t speed, i2c_master_dev_handle_t* device) {
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = speed;
    return i2c_master_bus_add_device(native_i2c_bus, &config, device) == ESP_OK;
}

bool init_i2c() {
    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = kSda,
        .scl_io_num = kScl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = 100000},
        .clk_flags = 0,
    };
    i2c_bus = i2c_bus_create(I2C_NUM_0, &config);
    if (!i2c_bus) return false;
    native_i2c_bus = i2c_bus_get_internal_bus_handle(i2c_bus);
    return native_i2c_bus != nullptr;
}

bool init_pmic() {
    pmic = std::make_unique<M5PM1>();
    if (pmic->begin(native_i2c_bus) != M5PM1_OK) {
        pmic.reset();
        return false;
    }
    pmic->setI2cSleepTime(0);
    pmic->setI2cSleepTime(0);
    pmic->btnSetConfig(M5PM1_BTN_TYPE_CLICK, M5PM1_BTN_CLICK_DELAY_1000MS);
    pmic->wdtSet(0);
    pmic->ldoSetPowerHold(true);
    pmic->setChargeEnable(true);
    pmic->gpioSet(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT, 0, M5PM1_GPIO_PULL_NONE,
                  M5PM1_GPIO_DRIVE_PUSHPULL);
    pmic->gpioSetFunc(M5PM1_GPIO_NUM_2, M5PM1_GPIO_FUNC_GPIO);
    pmic->gpioSetMode(M5PM1_GPIO_NUM_2, M5PM1_GPIO_MODE_INPUT);
    pmic->gpioSetPull(M5PM1_GPIO_NUM_2, M5PM1_GPIO_PULL_NONE);
    pmic->setSingleResetDisable(true);
    return true;
}

void load_brightness(BoardStatus& status) {
    nvs_handle_t handle = 0;
    if (nvs_open(kBoardNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
    std::uint8_t value = status.brightness;
    if (nvs_get_u8(handle, kBrightnessKey, &value) == ESP_OK && value <= 100) {
        status.brightness = value;
    }
    nvs_close(handle);
}

void persist_brightness(std::uint8_t value) {
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(kBoardNvsNamespace, NVS_READWRITE, &handle);
    if (result == ESP_OK) result = nvs_set_u8(handle, kBrightnessKey, value);
    if (result == ESP_OK) result = nvs_commit(handle);
    if (handle) nvs_close(handle);
    if (result != ESP_OK) ESP_LOGW(kTag, "brightness persistence failed: %s", esp_err_to_name(result));
}

// Pulse the OLED (IO5) and touch (IO4) reset lines together, as M5GFX.cpp:1916-1920 does.
// Without this the CO5300 keeps its previous session's state across a soft reset.
bool write_ioe_verified(std::uint8_t pin, std::uint8_t value) {
    for (int attempt = 1; attempt <= kIoeWriteAttempts; ++attempt) {
        m5ioe1_err_t result = M5IOE1_FAIL;
        ioe->digitalWriteWithRes(pin, value, &result);
        if (result == M5IOE1_OK) return true;
        ESP_LOGW(kTag, "IO expander pin %u write retry %d/%d", pin, attempt,
                 kIoeWriteAttempts);
        vTaskDelay(pdMS_TO_TICKS(kIoeWriteRetryMs));
    }
    return false;
}

bool reset_display_and_touch() {
    if (!write_ioe_verified(M5IOE1_PIN_4, 0) ||
        !write_ioe_verified(M5IOE1_PIN_5, 0)) return false;
    vTaskDelay(pdMS_TO_TICKS(8));
    if (!write_ioe_verified(M5IOE1_PIN_4, 1) ||
        !write_ioe_verified(M5IOE1_PIN_5, 1)) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
    return true;
}

bool init_ioe() {
    ioe = std::make_unique<M5IOE1>();
    if (ioe->begin(native_i2c_bus, 0x4f, M5IOE1_I2C_FREQ_400K) != M5IOE1_OK &&
        ioe->begin(native_i2c_bus, 0x6f, M5IOE1_I2C_FREQ_400K) != M5IOE1_OK) {
        ioe.reset();
        return false;
    }
    ioe->setI2cSleepTime(0);
    ioe->setI2cSleepTime(0);
    // Hold the OLED chip select high before the bus exists, so bus glitches during
    // the reset pulse are not latched as commands (M5GFX.cpp:1900-1902).
    gpio_set_direction(kDisplayCs, GPIO_MODE_OUTPUT);
    gpio_set_level(kDisplayCs, 1);
    for (auto pin : {M5IOE1_PIN_9, M5IOE1_PIN_8, M5IOE1_PIN_10, M5IOE1_PIN_4,
                     M5IOE1_PIN_5, M5IOE1_PIN_1, M5IOE1_PIN_3}) {
        ioe->pinMode(pin, OUTPUT);
    }
    ioe->digitalWrite(M5IOE1_PIN_10, 0);
    ioe->digitalWrite(M5IOE1_PIN_4, 1);
    ioe->digitalWrite(M5IOE1_PIN_5, 1);
    ioe->digitalWrite(M5IOE1_PIN_1, 1);  // MUX_CTR, high in M5GFX.cpp:1913
    ioe->digitalWrite(M5IOE1_PIN_3, 1);
    ioe->setPwmFrequency(5000);
    ioe->setPwmDuty(0, 0, false, true);
    gpio_set_direction(kSpeakerPa, GPIO_MODE_OUTPUT);
    gpio_set_level(kSpeakerPa, 0);

    for (int attempt = 1; attempt <= kDisplayPowerAttempts; ++attempt) {
        ioe->digitalWrite(M5IOE1_PIN_8, 1);
        vTaskDelay(pdMS_TO_TICKS(kDisplayPowerReadyMs));
        if (ioe->digitalRead(M5IOE1_PIN_8) == 1) {
            vTaskDelay(pdMS_TO_TICKS(kDisplayPowerSettleMs));
            if (reset_display_and_touch()) return true;
            ESP_LOGE(kTag, "display reset lines did not deassert");
            return false;
        }
        ESP_LOGW(kTag, "display power enable retry %d/%d", attempt, kDisplayPowerAttempts);
    }
    ESP_LOGE(kTag, "display power rail did not enable");
    return false;
}

bool init_touch() {
    if (!ioe) return false;
    ioe->digitalWrite(M5IOE1_PIN_4, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    ioe->digitalWrite(M5IOE1_PIN_4, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (!add_i2c_device(0x15, 100000, &touch_device)) return false;
    std::uint8_t chip = 0;
    std::uint8_t version = 0;
    return read_register(touch_device, 0xa7, &chip, 1) &&
           read_register(touch_device, 0xa9, &version, 1) && chip != 0 && version != 0;
}

TouchPoint read_touch() {
    std::array<std::uint8_t, 7> data{};
    if (!read_register(touch_device, 0x00, data.data(), data.size())) return {};
    const auto event = static_cast<std::uint8_t>((data[3] & 0xc0U) >> 6U);
    return {
        .pressed = data[2] > 0 && (event == 0 || event == 2),
        .x = static_cast<std::int16_t>(((data[3] & 0x0fU) << 8U) | data[4]),
        .y = static_cast<std::int16_t>(((data[5] & 0x0fU) << 8U) | data[6]),
    };
}

void lvgl_flush(lv_display_t* lv_display, const lv_area_t* area, std::uint8_t* pixels) {
    auto* gfx = static_cast<StopWatchDisplay*>(lv_display_get_driver_data(lv_display));
    const std::uint32_t width = area->x2 - area->x1 + 1;
    const std::uint32_t height = area->y2 - area->y1 + 1;
    gfx->startWrite();
    gfx->setAddrWindow(area->x1, area->y1, width, height);
    // rgb565_t is the little-endian source; LGFX converts it to the panel's
    // big-endian swap565_t on the way into the framebuffer.
    gfx->writePixels(reinterpret_cast<lgfx::rgb565_t*>(pixels), width * height);
    gfx->endWrite();  // auto-display pushes the accumulated dirty rect
    lv_display_flush_ready(lv_display);
}

void lvgl_touch(lv_indev_t*, lv_indev_data_t* data) {
    const auto point = read_touch();
    data->state = point.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (point.pressed) {
        data->point.x = point.x;
        data->point.y = point.y;
    }
}

void lvgl_tick(void*) { lv_tick_inc(10); }

void lvgl_task(void*) {
    while (true) {
        if (xSemaphoreTake(lvgl_mutex, portMAX_DELAY) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool init_display_and_lvgl() {
    display = std::make_unique<StopWatchDisplay>();
    if (!display->init()) {
        display.reset();
        return false;
    }
    lv_init();
    auto* lv_display = lv_display_create(display->width(), display->height());
    if (!lv_display) return false;
    lv_display_set_color_format(lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_driver_data(lv_display, display.get());
    lv_display_set_flush_cb(lv_display, lvgl_flush);
    const std::size_t buffer_size =
        display->width() * kLvglBufferLines * sizeof(lv_color16_t);
    constexpr auto buffer_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    auto* buffer = heap_caps_malloc(buffer_size, buffer_caps);
    if (!buffer) return false;
    lv_display_set_buffers(lv_display, buffer, nullptr, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    auto* input = lv_indev_create();
    if (!input) return false;
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, lvgl_touch);
    lv_indev_set_display(input, lv_display);

    lvgl_mutex = xSemaphoreCreateMutex();
    if (!lvgl_mutex) return false;
    esp_timer_create_args_t timer_config{};
    timer_config.callback = lvgl_tick;
    timer_config.name = "lvgl_tick";
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&timer_config, &timer) != ESP_OK ||
        esp_timer_start_periodic(timer, 10000) != ESP_OK) return false;
    return xTaskCreate(lvgl_task, "lvgl", 16384, nullptr, 3, nullptr) == pdPASS;
}

bool init_audio() {
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_handle_t rx = nullptr;
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_18,
            .bclk = GPIO_NUM_17,
            .ws = GPIO_NUM_15,
            .dout = GPIO_NUM_21,
            .din = GPIO_NUM_16,
            .invert_flags = {},
        },
    };
    if (i2s_new_channel(&channel_config, &tx, &rx) != ESP_OK ||
        i2s_channel_init_std_mode(tx, &standard_config) != ESP_OK ||
        i2s_channel_init_std_mode(rx, &standard_config) != ESP_OK ||
        i2s_channel_enable(tx) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) return false;

    audio_codec_i2s_cfg_t i2s_config{};
    i2s_config.rx_handle = rx;
    i2s_config.tx_handle = tx;
    const audio_codec_data_if_t* data = audio_codec_new_i2s_data(&i2s_config);
    audio_codec_i2c_cfg_t i2c_config{};
    i2c_config.addr = ES8311_CODEC_DEFAULT_ADDR;
    i2c_config.bus_handle = native_i2c_bus;
    const audio_codec_ctrl_if_t* control = audio_codec_new_i2c_ctrl(&i2c_config);
    const audio_codec_gpio_if_t* gpio = audio_codec_new_gpio();
    es8311_codec_cfg_t es8311_config{};
    es8311_config.ctrl_if = control;
    es8311_config.gpio_if = gpio;
    es8311_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_config.pa_pin = GPIO_NUM_NC;
    es8311_config.pa_reverted = false;
    es8311_config.use_mclk = true;
    const audio_codec_if_t* codec_if = es8311_codec_new(&es8311_config);
    esp_codec_dev_cfg_t codec_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data,
    };
    codec = esp_codec_dev_new(&codec_config);
    esp_codec_dev_sample_info_t format{};
    format.bits_per_sample = 16;
    format.channel = 1;
    format.sample_rate = kSampleRate;
    if (!data || !control || !gpio || !codec_if || !codec || esp_codec_dev_open(codec, &format) != 0) {
        codec = nullptr;
        return false;
    }
    esp_codec_dev_set_in_gain(codec, 30.0f);
    esp_codec_dev_set_out_vol(codec, 70);
    if (ioe) ioe->digitalWrite(M5IOE1_PIN_10, 1);
    gpio_set_level(kSpeakerPa, 1);
    return true;
}

bool init_imu() {
    const bmi270_bmm150_config_t config = {
        .i2c_addr = 0x68,
        .config_file_ptr = nullptr,
        .mode = BOSCH_ACCELEROMETER_ONLY,
    };
    return bmi270_bmm150_sensor_create(i2c_bus, &imu, &config) == ESP_OK;
}

bool init_rtc() {
    if (!add_i2c_device(0x32, 100000, &rtc_device)) return false;
    std::array<std::uint8_t, 7> now{};
    return read_register(rtc_device, 0x10, now.data(), now.size());
}

void init_buttons() {
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << kButtonA) | (1ULL << kButtonB),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
}

std::uint8_t bcd(std::uint8_t value) { return (value >> 4U) * 10U + (value & 0x0fU); }

void update_rtc(BoardStatus& status) {
    std::array<std::uint8_t, 7> data{};
    if (!read_register(rtc_device, 0x10, data.data(), data.size())) {
        status.rtc = Health::error;
        return;
    }
    const unsigned second = bcd(data[0] & 0x7fU);
    const unsigned minute = bcd(data[1] & 0x7fU);
    const unsigned hour = bcd(data[2] & 0x3fU);
    const unsigned day = bcd(data[4] & 0x3fU);
    const unsigned month = bcd(data[5] & 0x1fU);
    const unsigned year = 2000U + bcd(data[6]);
    if (month == 0 || month > 12 || day == 0 || day > 31 || hour > 23 || minute > 59 || second > 59) {
        status.rtc = Health::error;
        return;
    }
    std::snprintf(status.rtc_text.data(), status.rtc_text.size(), "%04u-%02u-%02u %02u:%02u:%02u",
                  year, month, day, hour, minute, second);
    status.rtc = Health::ok;
}

}  // namespace

BoardHal& BoardHal::instance() {
    static BoardHal board;
    return board;
}

esp_err_t BoardHal::init() {
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_result = nvs_flash_erase();
        if (nvs_result == ESP_OK) nvs_result = nvs_flash_init();
    }
    if (nvs_result != ESP_OK || !init_i2c()) return nvs_result == ESP_OK ? ESP_FAIL : nvs_result;
    load_brightness(status_);

    status_.pmic = init_pmic() ? Health::ok : Health::error;
    status_.io_expander = init_ioe() ? Health::ok : Health::error;
    status_.motor = ioe ? Health::ok : Health::error;
    status_.display = status_.io_expander == Health::ok && init_display_and_lvgl()
                          ? Health::ok
                          : Health::error;
    if (status_.display == Health::ok) display->set_brightness(status_.brightness);
    status_.touch = status_.io_expander == Health::ok && init_touch()
                        ? Health::ok
                        : Health::error;
    status_.audio = init_audio() ? Health::ok : Health::error;
    status_.imu = init_imu() ? Health::ok : Health::error;
    status_.rtc = init_rtc() ? Health::ok : Health::error;
    init_buttons();
    status_.buttons = Health::ok;
    poll();
    ESP_LOGI(kTag, "hardware bring-up complete");
    return status_.display == Health::ok ? ESP_OK : ESP_FAIL;
}

BoardStatus BoardHal::snapshot() { return status_; }

void BoardHal::poll() {
    const auto now = esp_timer_get_time();
    const bool a = gpio_get_level(kButtonA) == 0;
    const bool b = gpio_get_level(kButtonB) == 0;
    buttons.update(a, b, static_cast<std::uint64_t>(now / 1000));

    if (motor_stop_us && now >= motor_stop_us) stop_vibration();
    if (status_.touch == Health::ok) status_.touch_point = read_touch();

    if (now - battery_poll_us >= 1000000) {
        battery_poll_us = now;
        std::uint16_t millivolts = 0;
        std::uint16_t vin = 0;
        std::uint8_t charge = 1;
        if (pmic && pmic->readVbat(&millivolts) == M5PM1_OK) {
            status_.battery_mv = millivolts;
            status_.battery_percent = battery_percent(millivolts);
            if (pmic->readVin(&vin) == M5PM1_OK &&
                pmic->gpioGetInput(M5PM1_GPIO_NUM_2, &charge) == M5PM1_OK) {
                status_.charging = vin > 4000 && charge == 0;
            }
        } else if (pmic) {
            status_.pmic = Health::error;
        }
        if (rtc_device) update_rtc(status_);
    }

    if (now - sensor_poll_us >= 250000) {
        sensor_poll_us = now;
        int available = 0;
        if (imu && bmi270_bmm150_sensor_acceleration_available(imu, &available) == ESP_OK && available > 0) {
            bmi270_bmm150_sensor_read_acceleration(imu, &status_.imu_sample.accel_y,
                                                   &status_.imu_sample.accel_x,
                                                   &status_.imu_sample.accel_z);
            if (bmi270_bmm150_sensor_gyroscope_available(imu, &available) == ESP_OK && available > 0) {
                bmi270_bmm150_sensor_read_gyroscope(imu, &status_.imu_sample.gyro_y,
                                                    &status_.imu_sample.gyro_x,
                                                    &status_.imu_sample.gyro_z);
            }
        }
    }

}

void BoardHal::set_brightness(std::uint8_t percent) {
    status_.brightness = std::min<std::uint8_t>(percent, 100);
    if (display) display->set_brightness(status_.brightness);
    persist_brightness(status_.brightness);
}

std::uint8_t BoardHal::brightness() const { return status_.brightness; }

void BoardHal::vibrate(std::uint16_t duration_ms, std::uint8_t strength) {
    if (!ioe) return;
    ioe->setPwmDuty(0, motor_duty(strength), false, true);
    motor_stop_us = esp_timer_get_time() + static_cast<std::int64_t>(duration_ms) * 1000;
}

void BoardHal::stop_vibration() {
    if (ioe) ioe->setPwmDuty(0, 0, false, true);
    motor_stop_us = 0;
}

void BoardHal::play_tone(std::uint16_t frequency_hz, std::uint16_t duration_ms) {
    if (!codec || frequency_hz < 20 || duration_ms == 0) return;
    frequency_hz = std::min<std::uint16_t>(frequency_hz, 10000);
    duration_ms = std::min<std::uint16_t>(duration_ms, 1000);
    const std::size_t count = static_cast<std::size_t>(kSampleRate) * duration_ms / 1000;
    std::vector<std::int16_t> samples(count);
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t index = 0; index < count; ++index) {
        samples[index] = static_cast<std::int16_t>(std::sin(2.0 * pi * frequency_hz * index / kSampleRate) * 7000);
    }
    esp_codec_dev_write(codec, samples.data(), samples.size() * sizeof(samples[0]));
    std::array<std::int16_t, kSampleRate / 100> silence{};
    esp_codec_dev_write(codec, silence.data(), sizeof(silence));
}

void BoardHal::shutdown() {
    stop_vibration();
    if (ioe) {
        ioe->digitalWrite(M5IOE1_PIN_10, 0);
        ioe->digitalWrite(M5IOE1_PIN_3, 0);
    }
    gpio_set_level(kSpeakerPa, 0);
    if (pmic) pmic->shutdown();
}

ButtonEvents BoardHal::take_button_events() { return buttons.take_events(); }

bool BoardHal::lvgl_lock(std::uint32_t timeout_ms) {
    return lvgl_mutex && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void BoardHal::lvgl_unlock() {
    if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
}

const char* health_text(Health health) {
    switch (health) {
        case Health::ok: return "OK";
        case Health::error: return "ERR";
        case Health::unavailable: return "N/A";
    }
    return "?";
}

}  // namespace bajji
