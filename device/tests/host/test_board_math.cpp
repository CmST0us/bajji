#include "board_math.hpp"

#include <cassert>
#include <cmath>

namespace {

bool near(float actual, float expected, float tolerance = 0.01f) {
    return std::fabs(actual - expected) <= tolerance;
}

}  // namespace

int main() {
    using bajji::battery_percent;
    using bajji::display_duty;
    using bajji::ImageRotationState;
    using bajji::motor_duty;
    using bajji::update_image_rotation;

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

    constexpr float frame_seconds = 1.0f / 30.0f;
    assert(near(update_image_rotation({}, 0.0f, -1.0f, 0.0f, frame_seconds).degrees, 0.0f));
    assert(near(update_image_rotation({}, -1.0f, 0.0f, 0.0f, frame_seconds).degrees, -90.0f));
    assert(near(update_image_rotation({}, 1.0f, 0.0f, 0.0f, frame_seconds).degrees, 90.0f));

    const auto gyro = update_image_rotation({0.0f, true}, 0.0f, -1.0f, 90.0f,
                                            frame_seconds);
    assert(gyro.degrees < -2.0f && gyro.degrees > -3.0f);

    const auto wrapped = update_image_rotation({179.0f, true}, -0.01745f, 0.99985f,
                                               0.0f, frame_seconds);
    assert(wrapped.degrees > 179.0f);

    const auto flat = update_image_rotation(wrapped, 0.1f, 0.1f, 90.0f, frame_seconds);
    assert(!flat.initialized && near(flat.degrees, 0.0f));
    const auto upright_again = update_image_rotation(flat, 1.0f, 0.0f, 0.0f, frame_seconds);
    assert(upright_again.initialized && near(upright_again.degrees, 90.0f));
}
