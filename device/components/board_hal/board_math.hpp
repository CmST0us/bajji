// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bajji {

// ponytail: two-point LiPo estimate; replace with a measured discharge curve if UI accuracy matters.
constexpr std::uint8_t battery_percent(std::uint16_t millivolts) {
    constexpr std::uint16_t empty_mv = 3300;
    constexpr std::uint16_t full_mv = 4200;
    if (millivolts <= empty_mv) return 0;
    if (millivolts >= full_mv) return 100;
    return static_cast<std::uint8_t>((millivolts - empty_mv) * 100U / (full_mv - empty_mv));
}

constexpr std::uint8_t motor_duty(std::uint8_t strength) {
    if (strength == 0) return 0;
    return static_cast<std::uint8_t>(25U + std::min<unsigned>(strength, 100U) * 75U / 100U);
}

constexpr std::uint8_t display_duty(std::uint8_t brightness) {
    return static_cast<std::uint8_t>(std::min<unsigned>(brightness, 100U) * 255U / 100U);
}

struct ImageRotationState {
    float degrees = 0.0f;
    bool initialized = false;
};

inline ImageRotationState update_image_rotation(ImageRotationState previous, float accel_x,
                                                float accel_y, float gyro_z,
                                                float elapsed_seconds) {
    constexpr float radians_to_degrees = 57.2957795f;
    constexpr float minimum_vertical_g = 0.35f;
    constexpr float filter_time_constant_seconds = 0.20f;
    constexpr float mount_offset_degrees = 0.0f;
    constexpr float gyro_direction = -1.0f;

    if (std::hypot(accel_x, accel_y) < minimum_vertical_g) return {};

    const float accel_degrees = std::remainder(
        std::atan2(accel_x, -accel_y) * radians_to_degrees + mount_offset_degrees, 360.0f);
    if (!previous.initialized) return {accel_degrees, true};

    const float dt = std::clamp(elapsed_seconds, 0.0f, 0.10f);
    const float predicted = previous.degrees + gyro_direction * gyro_z * dt;
    const float correction = std::remainder(accel_degrees - predicted, 360.0f);
    const float weight = dt / (filter_time_constant_seconds + dt);
    return {std::remainder(predicted + correction * weight, 360.0f), true};
}

}  // namespace bajji
