// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "wallpaper_format.h"

namespace bajji {

constexpr std::uint32_t kWallpaperTransferMaximumBytes = 3U * 1024U * 1024U;

enum class DisplayMode : std::uint8_t { cover = 0, fit_blur = 1 };

struct WallpaperSettings {
    bool configured{};
    char category[24]{};
    char type[16]{};
    DisplayMode display_mode{DisplayMode::cover};
    std::uint16_t auto_refresh_minutes{};
};

struct WallpaperStatus {
    bool mounted{};
    bool online{};
    bool internet_verified{};
    bool busy{};
    bool has_cache{};
    std::uint32_t revision{};
    std::uint32_t request_revision{};
    std::int32_t last_error{};
    WallpaperSettings settings{};
    wallpaper_media_info_t media{};
    char lvgl_path[32]{};
    char state[80]{};
};

esp_err_t wallpaper_start();
void wallpaper_set_online(bool online_and_time_valid);
void wallpaper_request_refresh();
esp_err_t wallpaper_cancel_request();
esp_err_t wallpaper_save_settings(const char* category, const char* type);
esp_err_t wallpaper_set_display_mode(DisplayMode mode);
esp_err_t wallpaper_set_auto_refresh(std::uint16_t minutes);
esp_err_t wallpaper_apply_parameters(DisplayMode mode, std::uint16_t auto_refresh_minutes);
esp_err_t wallpaper_transfer_begin(wallpaper_media_format_t format, std::uint32_t size,
                                   std::uint32_t crc32);
esp_err_t wallpaper_transfer_write(std::uint32_t offset, const std::uint8_t* bytes,
                                   std::size_t length);
std::uint32_t wallpaper_transfer_received_bytes();
esp_err_t wallpaper_transfer_commit();
esp_err_t wallpaper_transfer_cancel();
WallpaperStatus wallpaper_snapshot();

}  // namespace bajji
