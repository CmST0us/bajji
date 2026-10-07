// SPDX-License-Identifier: MIT
#pragma once
#include "button_state.hpp"

namespace bajji {
// The ADC ladder cannot identify chords. A key change while held cancels the gesture;
// require release before another action, rather than interpreting a chord as a second key.
class PassportButtons {
public:
    enum class Key { none, up, down, ok };
    void update(Key sample, std::uint64_t now) {
        if (sample != sample_) { sample_ = sample; changed_ = now; }
        if (now - changed_ >= 30 && sample != stable_) {
            const Key previous = stable_;
            stable_ = sample;
            if (sample == Key::none) {
                if (!suppressed_ && previous != Key::none) {
                    events_.a_pressed |= previous == Key::up;
                    events_.b_pressed |= previous == Key::down;
                    events_.ok_pressed |= previous == Key::ok;
                }
                suppressed_ = false;
            } else if (previous != Key::none) {
                suppressed_ = true;
            } else {
                pressed_ = now;
            }
        }
        if (stable_ == Key::ok && !suppressed_ && now - pressed_ >= 700) {
            events_.back_pressed = true;
            suppressed_ = true;
        }
    }
    ButtonEvents take_events() { const auto result = events_; events_ = {}; return result; }
private:
    Key sample_{Key::none}, stable_{Key::none};
    std::uint64_t changed_{}, pressed_{};
    bool suppressed_{};
    ButtonEvents events_{};
};
}  // namespace bajji
