// SPDX-License-Identifier: MIT
#include "board_hal.hpp"
#include "ble_link.h"
#include "diagnostics_ui.hpp"
#include "ip_bridge.h"
#include "wallpaper_service.hpp"
#include "wifi_link.h"

#include <inttypes.h>

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
    const esp_err_t event_result = esp_event_loop_create_default();
    if (event_result != ESP_OK) {
        ESP_LOGE("main", "default event loop init failed: %s", esp_err_to_name(event_result));
    }
    auto& board = bajji::BoardHal::instance();
    const esp_err_t result = board.init();
    if (result != ESP_OK) ESP_LOGE("main", "board init incomplete: %s", esp_err_to_name(result));
    const esp_err_t ip_result = ip_bridge_start();
    if (ip_result != ESP_OK) ESP_LOGE("main", "IP bridge init failed: %s", esp_err_to_name(ip_result));
    const esp_err_t wifi_result = wifi_link_start();
    if (wifi_result != ESP_OK) ESP_LOGE("main", "Wi-Fi init failed: %s", esp_err_to_name(wifi_result));
    ble_link_set_handlers(
        [](const bridge_frame_t* frame, void*) { ip_bridge_receive(frame); },
        [](bool ready, void*) { ip_bridge_set_link(ready); }, NULL);
    ble_link_set_provision_handler(
        [](const uint8_t* payload, size_t length, void*) {
            return wifi_link_provision(payload, length);
        }, NULL);
    const esp_err_t ble_result = ble_link_start();
    if (ble_result != ESP_OK) ESP_LOGE("main", "BLE init failed: %s", esp_err_to_name(ble_result));
    const esp_err_t wallpaper_result = bajji::wallpaper_start();
    if (wallpaper_result != ESP_OK) {
        ESP_LOGE("main", "wallpaper service incomplete: %s", esp_err_to_name(wallpaper_result));
    }

    bajji::ProductUI ui;
    bool sample_imu = false;
    if (board.snapshot().display == bajji::Health::ok && board.lvgl_lock()) {
        ui.create(ble_link_snapshot(), bajji::wallpaper_snapshot());
        sample_imu = ui.image_visible() && board.auto_rotation_enabled();
        board.lvgl_unlock();
    }

    TickType_t last_stats_tick = xTaskGetTickCount();
    TickType_t last_ui_tick = last_stats_tick;
    bajji::ButtonEvents pending_buttons{};
    std::uint64_t last_rx_bytes = 0;
    std::uint64_t last_tx_bytes = 0;
    while (true) {
        // GPIO sampling runs in the 10 ms timer callback. Drain its state independently
        // of the heavier UI/status refresh so edges do not wait for its cadence.
        board.poll(sample_imu);
        const bajji::ButtonEvents buttons = board.take_button_events();
        // Keep edges until the UI lock succeeds, but use the latest hold progress.
        pending_buttons.a_pressed |= buttons.a_pressed;
        pending_buttons.b_pressed |= buttons.b_pressed;
        pending_buttons.chord_started |= buttons.chord_started;
        pending_buttons.chord_completed |= buttons.chord_completed;
        pending_buttons.chord_cancelled |= buttons.chord_cancelled;
        pending_buttons.chord_progress_ms = buttons.chord_progress_ms;
        const TickType_t now = xTaskGetTickCount();
        const bool input_pending = pending_buttons.a_pressed || pending_buttons.b_pressed ||
                                   pending_buttons.chord_started ||
                                   pending_buttons.chord_completed ||
                                   pending_buttons.chord_cancelled ||
                                   pending_buttons.chord_progress_ms;
        const TickType_t ui_period = pdMS_TO_TICKS(sample_imu ? 33 : 100);
        if (!input_pending && now - last_ui_tick < ui_period) {
            vTaskDelay(pdMS_TO_TICKS(sample_imu ? 3 : 10));
            continue;
        }
        const ble_link_status_t link = ble_link_snapshot();
        const ip_bridge_status_t ip = ip_bridge_snapshot();
        const wifi_link_status_t wifi = wifi_link_snapshot();
        const bool online = (wifi.connected || ip.link_up) && ip.time_valid;
        bajji::WallpaperStatus wallpaper = bajji::wallpaper_snapshot();
        if (online != wallpaper.online) {
            bajji::wallpaper_set_online(online);
            wallpaper = bajji::wallpaper_snapshot();
        }
        // The timer task keeps sampling GPIO while this task waits. Once an edge is queued,
        // wait for the current render to release LVGL instead of missing every short gap.
        if (board.lvgl_lock(input_pending ? 1000 : 0)) {
            ui.refresh(board.snapshot(), link, wallpaper, pending_buttons);
            sample_imu = ui.image_visible() && board.auto_rotation_enabled();
            pending_buttons = {};
            last_ui_tick = now;
            board.lvgl_unlock();
        }
        const TickType_t elapsed = now - last_stats_tick;
        if (elapsed >= pdMS_TO_TICKS(5000)) {
            if (link.coc_connected) {
                const double seconds = static_cast<double>(elapsed) / configTICK_RATE_HZ;
                ESP_LOGI("bridge_stats",
                         "RX %.1f KB/s (%llu B, %" PRIu32 " frames) | "
                         "TX %.1f KB/s (%llu B, %" PRIu32 " frames) | "
                         "queue=%" PRIu32 " tx_err=%" PRIu32 " proto=%" PRIu32,
                         static_cast<double>(link.rx_bytes - last_rx_bytes) / seconds / 1000.0,
                         static_cast<unsigned long long>(link.rx_bytes), link.rx_frames,
                         static_cast<double>(link.tx_bytes - last_tx_bytes) / seconds / 1000.0,
                         static_cast<unsigned long long>(link.tx_bytes), link.tx_frames,
                         link.queue_overflows, link.tx_errors, link.protocol_errors);
            }
            last_stats_tick = now;
            last_rx_bytes = link.rx_bytes;
            last_tx_bytes = link.tx_bytes;
        }
        vTaskDelay(pdMS_TO_TICKS(sample_imu ? 3 : 10));
    }
}
