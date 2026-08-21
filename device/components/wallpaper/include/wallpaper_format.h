// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WALLPAPER_MEDIA_UNKNOWN = 0,
    WALLPAPER_MEDIA_JPEG,
    WALLPAPER_MEDIA_PNG,
    WALLPAPER_MEDIA_GIF,
    WALLPAPER_MEDIA_WEBP,
} wallpaper_media_format_t;

typedef struct {
    wallpaper_media_format_t format;
    uint16_t width;
    uint16_t height;
    bool animated;
} wallpaper_media_info_t;

typedef enum {
    WALLPAPER_FORMAT_OK = 0,
    WALLPAPER_FORMAT_INVALID,
    WALLPAPER_FORMAT_TOO_LARGE,
    WALLPAPER_FORMAT_TRUNCATED,
    WALLPAPER_FORMAT_UNSUPPORTED,  // well formed, but beyond what the on-device decoder handles
} wallpaper_format_result_t;

wallpaper_format_result_t wallpaper_media_validate(const uint8_t* data, size_t size,
                                                    size_t maximum_size,
                                                    wallpaper_media_info_t* info);

int wallpaper_settings_valid(const char* category, const char* type);
int wallpaper_build_random_url(const char* category, const char* type,
                               char* output, size_t output_size);

#ifdef __cplusplus
}
#endif
