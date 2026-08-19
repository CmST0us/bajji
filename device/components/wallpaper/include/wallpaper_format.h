// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
} wallpaper_jpeg_info_t;

typedef enum {
    WALLPAPER_FORMAT_OK = 0,
    WALLPAPER_FORMAT_INVALID,
    WALLPAPER_FORMAT_TOO_LARGE,
    WALLPAPER_FORMAT_TRUNCATED,
} wallpaper_format_result_t;

wallpaper_format_result_t wallpaper_jpeg_validate(const uint8_t* data, size_t size,
                                                   size_t maximum_size,
                                                   wallpaper_jpeg_info_t* info);

int wallpaper_build_bing_url(const char* urlbase, char* output, size_t output_size);

#ifdef __cplusplus
}
#endif
