// SPDX-License-Identifier: MIT
#include "button_state.hpp"

#include <cassert>

int main() {
    bajji::ButtonState button;

    button.update(true, false, 100);
    button.update(false, false, 180);
    assert(button.take_events().a_pressed);

    button.update(false, true, 300);
    button.update(false, false, 360);
    assert(button.take_events().b_pressed);

    button.update(true, false, 1000);
    button.update(true, true, 1120);
    auto events = button.take_events();
    assert(events.chord_started && !events.a_pressed && !events.b_pressed);
    button.update(true, true, 2119);
    events = button.take_events();
    assert(!events.chord_completed && events.chord_progress_ms == 999);
    button.update(true, true, 2120);
    events = button.take_events();
    assert(events.chord_completed && events.chord_progress_ms == 1000);
    button.update(false, false, 2200);
    events = button.take_events();
    assert(!events.a_pressed && !events.b_pressed);

    button.update(true, false, 3000);
    button.update(true, true, 3050);
    assert(button.take_events().chord_started);
    button.update(true, false, 3500);
    events = button.take_events();
    assert(events.chord_cancelled && !events.a_pressed && !events.b_pressed);
    button.update(false, false, 3600);
    button.take_events();

    button.update(true, false, 4000);
    button.update(true, true, 4121);
    button.update(false, true, 4200);
    events = button.take_events();
    assert(events.a_pressed && !events.chord_started);
    button.update(false, false, 4300);
    assert(button.take_events().b_pressed);
    return 0;
}
