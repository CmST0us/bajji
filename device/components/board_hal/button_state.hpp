// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace bajji {

class ButtonState {
public:
    explicit ButtonState(std::uint32_t long_press_ms = 1200) : long_press_ms_(long_press_ms) {}

    void update(bool pressed, std::uint64_t now_ms) {
        if (pressed && !pressed_) {
            pressed_ = true;
            long_emitted_ = false;
            pressed_at_ms_ = now_ms;
        } else if (pressed && !long_emitted_ && now_ms - pressed_at_ms_ >= long_press_ms_) {
            long_emitted_ = true;
            long_latched_ = true;
        } else if (!pressed && pressed_) {
            if (!long_emitted_) short_latched_ = true;
            pressed_ = false;
        }
    }

    bool take_short_press() {
        const bool result = short_latched_;
        short_latched_ = false;
        return result;
    }

    bool take_long_press() {
        const bool result = long_latched_;
        long_latched_ = false;
        return result;
    }

private:
    std::uint32_t long_press_ms_;
    std::uint64_t pressed_at_ms_{};
    bool pressed_{};
    bool long_emitted_{};
    bool short_latched_{};
    bool long_latched_{};
};

}  // namespace bajji
