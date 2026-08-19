// SPDX-License-Identifier: MIT
#include "button_state.hpp"

#include <cassert>

int main() {
    bajji::ButtonState button;

    button.update(true, 100);
    button.update(false, 900);
    assert(button.take_short_press());
    assert(!button.take_long_press());

    button.update(true, 2000);
    button.update(true, 3199);
    assert(!button.take_long_press());
    button.update(true, 3200);
    assert(button.take_long_press());
    button.update(true, 4000);
    assert(!button.take_long_press());
    button.update(false, 4100);
    assert(!button.take_short_press());
    return 0;
}
