#include "board_math.hpp"

#include <cassert>

int main() {
    using bajji::battery_percent;
    using bajji::display_duty;
    using bajji::motor_duty;

    assert(battery_percent(3200) == 0);
    assert(battery_percent(3300) == 0);
    assert(battery_percent(3750) == 50);
    assert(battery_percent(4200) == 100);
    assert(battery_percent(4300) == 100);

    assert(motor_duty(0) == 0);
    assert(motor_duty(1) == 25);
    assert(motor_duty(100) == 100);

    assert(display_duty(0) == 0);
    assert(display_duty(100) == 255);
}
