// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

#include "esp_err.h"

namespace bajji {

inline constexpr const char* kWallpaperLvglPath = "S:/wallpaper.jpg";

struct WallpaperStatus {
    bool mounted{};
    bool online{};
    bool busy{};
    bool has_cache{};
    std::uint32_t revision{};
    std::int32_t last_error{};
    char state[48]{};
    char start_date[9]{};
    char copyright[192]{};
    char test_result[96]{};
};

esp_err_t wallpaper_start();
void wallpaper_set_online(bool online_and_time_valid);
void wallpaper_request_refresh();
void wallpaper_request_dns_test();
void wallpaper_request_https_test();
WallpaperStatus wallpaper_snapshot();

}  // namespace bajji
