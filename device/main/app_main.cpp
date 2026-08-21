// SPDX-License-Identifier: MIT
#include "board_hal.hpp"
#include "ble_link.h"
#include "diagnostics_ui.hpp"
#include "ip_bridge.h"
#include "wallpaper_service.hpp"

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
    ble_link_set_handlers(
        [](const bridge_frame_t* frame, void*) { ip_bridge_receive(frame); },
        [](bool ready, void*) { ip_bridge_set_link(ready); }, NULL);
    const esp_err_t ble_result = ble_link_start();
    if (ble_result != ESP_OK) ESP_LOGE("main", "BLE init failed: %s", esp_err_to_name(ble_result));
    const esp_err_t wallpaper_result = bajji::wallpaper_start();
    if (wallpaper_result != ESP_OK) {
        ESP_LOGE("main", "wallpaper service incomplete: %s", esp_err_to_name(wallpaper_result));
    }

    bajji::ProductUI ui;
    if (board.snapshot().display == bajji::Health::ok && board.lvgl_lock()) {
        ui.create(ble_link_snapshot(), bajji::wallpaper_snapshot());
        board.lvgl_unlock();
    }

    TickType_t last_stats_tick = xTaskGetTickCount();
    TickType_t last_ui_tick = last_stats_tick;
    std::uint64_t last_rx_bytes = 0;
    std::uint64_t last_tx_bytes = 0;
    while (true) {
        // ButtonState's chord window is 120 ms. Keep GPIO sampling independent of the
        // heavier UI/status refresh so a normal short press and the second chord edge
        // cannot both fall between 100 ms refreshes.
        board.poll();
        const TickType_t now = xTaskGetTickCount();
        if (now - last_ui_tick < pdMS_TO_TICKS(100)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        last_ui_tick = now;
        const ble_link_status_t link = ble_link_snapshot();
        const ip_bridge_status_t ip = ip_bridge_snapshot();
        const bool online = ip.link_up && ip.time_valid;
        bajji::WallpaperStatus wallpaper = bajji::wallpaper_snapshot();
        if (online != wallpaper.online) {
            bajji::wallpaper_set_online(online);
            wallpaper = bajji::wallpaper_snapshot();
        }
        // Claim the button events only once the UI can act on them. Taking them first
        // dropped every press made while the LVGL task held the lock; they now queue up
        // in ButtonState instead.
        if (board.lvgl_lock(0)) {
            const bajji::ButtonEvents buttons = board.take_button_events();
            ui.refresh(board.snapshot(), link, wallpaper, buttons);
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
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
