// SPDX-License-Identifier: MIT
#include "board_hal.hpp"
#include "diagnostics_ui.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
    auto& board = bajji::BoardHal::instance();
    const esp_err_t result = board.init();
    if (result != ESP_OK) ESP_LOGE("main", "board init incomplete: %s", esp_err_to_name(result));

    bajji::DiagnosticsUI ui;
    if (board.snapshot().display == bajji::Health::ok && board.lvgl_lock()) {
        ui.create();
        board.lvgl_unlock();
    }

    while (true) {
        board.poll();
        if (board.button_a_pressed()) {
            board.vibrate(80, 60);
            board.play_tone(880, 80);
        }
        if (board.button_b_pressed()) {
            board.set_brightness(static_cast<std::uint8_t>((board.brightness() % 100) + 20));
        }
        if (board.lvgl_lock(50)) {
            ui.refresh(board.snapshot());
            board.lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
