// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"

namespace bajji {

enum class Health : std::uint8_t { unavailable, ok, error };

struct TouchPoint {
    bool pressed = false;
    std::int16_t x = -1;
    std::int16_t y = -1;
};

struct ImuSample {
    float accel_x = 0;
    float accel_y = 0;
    float accel_z = 0;
    float gyro_x = 0;
    float gyro_y = 0;
    float gyro_z = 0;
};

struct BoardStatus {
    Health pmic = Health::unavailable;
    Health io_expander = Health::unavailable;
    Health display = Health::unavailable;
    Health touch = Health::unavailable;
    Health audio = Health::unavailable;
    Health motor = Health::unavailable;
    Health imu = Health::unavailable;
    Health rtc = Health::unavailable;
    Health buttons = Health::unavailable;
    std::uint16_t battery_mv = 0;
    std::uint8_t battery_percent = 0;
    bool charging = false;
    std::uint8_t brightness = 60;
    std::uint16_t microphone_level = 0;
    TouchPoint touch_point{};
    ImuSample imu_sample{};
    std::array<char, 24> rtc_text{};
};

class BoardHal {
public:
    static BoardHal& instance();

    esp_err_t init();
    BoardStatus snapshot();
    void poll();

    void set_brightness(std::uint8_t percent);
    std::uint8_t brightness() const;
    void vibrate(std::uint16_t duration_ms, std::uint8_t strength = 60);
    void stop_vibration();
    void play_tone(std::uint16_t frequency_hz, std::uint16_t duration_ms);
    void shutdown();

    bool button_a_pressed();
    bool button_b_pressed();
    bool lvgl_lock(std::uint32_t timeout_ms = 1000);
    void lvgl_unlock();

    BoardHal(const BoardHal&) = delete;
    BoardHal& operator=(const BoardHal&) = delete;

private:
    BoardHal() = default;
    BoardStatus status_{};
};

const char* health_text(Health health);

}  // namespace bajji
