// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace bajji {

constexpr std::uint64_t auto_refresh_deadline_ms(std::uint64_t now_ms,
                                                 std::uint16_t minutes) {
    return minutes ? now_ms + static_cast<std::uint64_t>(minutes) * 60U * 1000U : 0;
}

constexpr std::uint32_t auto_refresh_wait_ms(std::uint64_t now_ms,
                                             std::uint64_t deadline_ms) {
    if (!deadline_ms) return std::numeric_limits<std::uint32_t>::max();
    if (deadline_ms <= now_ms) return 0;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        deadline_ms - now_ms, std::numeric_limits<std::uint32_t>::max() - 1U));
}

}  // namespace bajji
