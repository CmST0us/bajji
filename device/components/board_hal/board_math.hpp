// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
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

}  // namespace bajji
