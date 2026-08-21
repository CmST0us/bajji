// SPDX-License-Identifier: MIT
#include "button_state.hpp"

#include <cassert>

int main() {
    // Keep the gesture cases focused on chord timing; debounce has its own check below.
    bajji::ButtonState button(120, 1000, 0);

    button.update(true, false, 100);
    button.update(false, false, 180);
    assert(button.take_events().a_pressed);

    button.update(false, true, 300);
    button.update(false, false, 360);
    assert(button.take_events().b_pressed);

    button.update(true, false, 500);
    button.update(true, false, 621);
    assert(button.take_events().a_pressed);
    button.update(false, false, 700);
    assert(!button.take_events().a_pressed);

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

    bajji::ButtonState debounced;
    debounced.update(true, false, 5000);
    debounced.update(false, false, 5005);
    debounced.update(true, false, 5010);
    debounced.update(true, false, 5029);
    assert(!debounced.take_events().a_pressed);
    debounced.update(true, false, 5030);  // stable press edge
    debounced.update(false, false, 5040);
    debounced.update(true, false, 5045);
    debounced.update(false, false, 5050);
    debounced.update(false, false, 5069);
    assert(!debounced.take_events().a_pressed);
    debounced.update(false, false, 5070);  // stable release edge
    assert(debounced.take_events().a_pressed);
    return 0;
}
