#include <cassert>
#include "passport_buttons.hpp"
using bajji::PassportButtons;
using Key = PassportButtons::Key;
int main() {
    PassportButtons buttons;
    buttons.update(Key::up, 0); buttons.update(Key::none, 10);
    buttons.update(Key::none, 50); assert(!buttons.take_events().a_pressed);
    buttons.update(Key::up, 100); buttons.update(Key::up, 140);
    buttons.update(Key::none, 180); buttons.update(Key::none, 220);
    assert(buttons.take_events().a_pressed); assert(!buttons.take_events().a_pressed);
    buttons.update(Key::ok, 300); buttons.update(Key::ok, 340);
    buttons.update(Key::ok, 1040); assert(buttons.take_events().back_pressed);
    buttons.update(Key::ok, 1200); assert(!buttons.take_events().back_pressed);
    buttons.update(Key::none, 1300); buttons.update(Key::none, 1340);
    assert(!buttons.take_events().ok_pressed);
    buttons.update(Key::ok, 1400); buttons.update(Key::ok, 1440);
    buttons.update(Key::none, 1500); buttons.update(Key::none, 1540);
    assert(buttons.take_events().ok_pressed);
    // Changing ladder voltage while held must not activate either action.
    buttons.update(Key::down, 1600); buttons.update(Key::down, 1640);
    buttons.update(Key::up, 1700); buttons.update(Key::up, 1740);
    buttons.update(Key::none, 1800); buttons.update(Key::none, 1840);
    const auto events = buttons.take_events();
    assert(!events.a_pressed && !events.b_pressed && !events.chord_started);
}
