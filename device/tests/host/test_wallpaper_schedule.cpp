// SPDX-License-Identifier: MIT
#include "wallpaper_schedule.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using bajji::auto_refresh_deadline_ms;
    using bajji::auto_refresh_wait_ms;

    assert(auto_refresh_deadline_ms(1234, 0) == 0);
    assert(auto_refresh_deadline_ms(1234, 5) == 301234);
    assert(auto_refresh_deadline_ms(10, 1440) == 86400010);

    assert(auto_refresh_wait_ms(10, 0) == std::numeric_limits<std::uint32_t>::max());
    assert(auto_refresh_wait_ms(10, 10) == 0);
    assert(auto_refresh_wait_ms(11, 10) == 0);
    assert(auto_refresh_wait_ms(10, 610) == 600);
    return 0;
}
