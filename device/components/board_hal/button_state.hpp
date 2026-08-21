// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace bajji {

struct ButtonEvents {
    bool a_pressed{};
    bool b_pressed{};
    bool chord_started{};
    bool chord_completed{};
    bool chord_cancelled{};
    std::uint16_t chord_progress_ms{};
};

class ButtonState {
public:
    explicit ButtonState(std::uint32_t chord_window_ms = 120,
                         std::uint32_t chord_hold_ms = 1000)
        : chord_window_ms_(chord_window_ms), chord_hold_ms_(chord_hold_ms) {}

    void update(bool a_down, bool b_down, std::uint64_t now_ms) {
        const bool a_rising = a_down && !a_down_;
        const bool b_rising = b_down && !b_down_;
        const bool a_falling = !a_down && a_down_;
        const bool b_falling = !b_down && b_down_;

        if (!a_down_ && !b_down_ && (a_rising || b_rising)) {
            first_pressed_at_ms_ = now_ms;
        }
        if (a_rising) a_pending_ = true;
        if (b_rising) b_pending_ = true;

        const bool pair_formed = a_down && b_down && !chord_active_ && !suppress_until_release_ &&
                                 now_ms - first_pressed_at_ms_ <= chord_window_ms_;
        if (pair_formed) {
            chord_active_ = true;
            chord_started_at_ms_ = now_ms;
            a_pending_ = false;
            b_pending_ = false;
            events_.chord_started = true;
        }

        if (chord_active_) {
            if (a_down && b_down) {
                const std::uint64_t held = now_ms - chord_started_at_ms_;
                events_.chord_progress_ms = static_cast<std::uint16_t>(
                    held < chord_hold_ms_ ? held : chord_hold_ms_);
                if (!chord_completed_ && held >= chord_hold_ms_) {
                    chord_completed_ = true;
                    suppress_until_release_ = true;
                    events_.chord_completed = true;
                }
            } else if (!chord_completed_) {
                chord_active_ = false;
                suppress_until_release_ = true;
                events_.chord_progress_ms = 0;
                events_.chord_cancelled = true;
            }
        } else if (!suppress_until_release_) {
            const bool chord_window_closed = now_ms - first_pressed_at_ms_ > chord_window_ms_;
            if (chord_window_closed && a_down && !b_down && a_pending_ && !b_pending_) {
                a_pending_ = false;
                events_.a_pressed = true;
            }
            if (chord_window_closed && b_down && !a_down && b_pending_ && !a_pending_) {
                b_pending_ = false;
                events_.b_pressed = true;
            }
            if (a_falling && a_pending_) {
                a_pending_ = false;
                events_.a_pressed = true;
            }
            if (b_falling && b_pending_) {
                b_pending_ = false;
                events_.b_pressed = true;
            }
        }

        a_down_ = a_down;
        b_down_ = b_down;
        if (!a_down && !b_down) {
            chord_active_ = false;
            chord_completed_ = false;
            suppress_until_release_ = false;
            a_pending_ = false;
            b_pending_ = false;
            first_pressed_at_ms_ = 0;
            chord_started_at_ms_ = 0;
            if (!events_.chord_completed) events_.chord_progress_ms = 0;
        }
    }

    ButtonEvents take_events() {
        const ButtonEvents result = events_;
        events_.a_pressed = false;
        events_.b_pressed = false;
        events_.chord_started = false;
        events_.chord_completed = false;
        events_.chord_cancelled = false;
        if (!chord_active_) events_.chord_progress_ms = 0;
        return result;
    }

private:
    std::uint32_t chord_window_ms_;
    std::uint32_t chord_hold_ms_;
    std::uint64_t first_pressed_at_ms_{};
    std::uint64_t chord_started_at_ms_{};
    bool a_down_{};
    bool b_down_{};
    bool a_pending_{};
    bool b_pending_{};
    bool chord_active_{};
    bool chord_completed_{};
    bool suppress_until_release_{};
    ButtonEvents events_{};
};

}  // namespace bajji
