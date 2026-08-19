// SPDX-License-Identifier: MIT
#include "board_hal.hpp"
#include "ble_link.h"
#include "diagnostics_ui.hpp"
#include "ip_bridge.h"
#include "wallpaper_service.hpp"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
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

    bajji::DiagnosticsUI ui;
    if (board.snapshot().display == bajji::Health::ok && board.lvgl_lock()) {
        ui.create();
        board.lvgl_unlock();
    }

    TickType_t last_stats_tick = xTaskGetTickCount();
    std::uint64_t last_rx_bytes = 0;
    std::uint64_t last_tx_bytes = 0;
    while (true) {
        board.poll();
        if (board.button_a_pressed()) {
            board.vibrate(80, 60);
            board.play_tone(880, 80);
        }
        if (board.button_b_pressed()) {
            board.set_brightness(static_cast<std::uint8_t>((board.brightness() % 100) + 20));
        }
        const ble_link_status_t link = ble_link_snapshot();
        const ip_bridge_status_t ip = ip_bridge_snapshot();
        bajji::wallpaper_set_online(ip.link_up && ip.time_valid);
        if (board.lvgl_lock(50)) {
            ui.refresh(board.snapshot(), link);
            board.lvgl_unlock();
        }
        const TickType_t now = xTaskGetTickCount();
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
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
