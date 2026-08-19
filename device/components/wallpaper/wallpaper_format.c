// SPDX-License-Identifier: MIT
#include "wallpaper_format.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int is_sof(uint8_t marker) {
    return (marker >= 0xc0 && marker <= 0xc3) ||
           (marker >= 0xc5 && marker <= 0xc7) ||
           (marker >= 0xc9 && marker <= 0xcb) ||
           (marker >= 0xcd && marker <= 0xcf);
}

wallpaper_format_result_t wallpaper_jpeg_validate(const uint8_t* data, size_t size,
                                                   size_t maximum_size,
                                                   wallpaper_jpeg_info_t* info) {
    if (!data || !info || size < 4 || data[0] != 0xff || data[1] != 0xd8 ||
        data[size - 2] != 0xff || data[size - 1] != 0xd9) {
        return WALLPAPER_FORMAT_INVALID;
    }
    if (size > maximum_size) return WALLPAPER_FORMAT_TOO_LARGE;

    wallpaper_jpeg_info_t found = {0};
    size_t offset = 2;
    while (offset + 1 < size - 2) {
        if (data[offset++] != 0xff) return WALLPAPER_FORMAT_INVALID;
        while (offset < size && data[offset] == 0xff) ++offset;
        if (offset >= size) return WALLPAPER_FORMAT_TRUNCATED;
        const uint8_t marker = data[offset++];
        if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (marker == 0xd9) break;
        if (offset + 2 > size) return WALLPAPER_FORMAT_TRUNCATED;
        const size_t segment_size = ((size_t)data[offset] << 8U) | data[offset + 1];
        if (segment_size < 2 || segment_size > size - offset) {
            return WALLPAPER_FORMAT_TRUNCATED;
        }
        if (is_sof(marker)) {
            if (segment_size < 8) return WALLPAPER_FORMAT_INVALID;
            found.height = (uint16_t)(((uint16_t)data[offset + 3] << 8U) | data[offset + 4]);
            found.width = (uint16_t)(((uint16_t)data[offset + 5] << 8U) | data[offset + 6]);
            if (found.width == 0 || found.height == 0) return WALLPAPER_FORMAT_INVALID;
        }
        if (marker == 0xda) break;  // Entropy data ends at the already-checked EOI marker.
        offset += segment_size;
    }
    if (found.width == 0 || found.height == 0) return WALLPAPER_FORMAT_INVALID;
    *info = found;
    return WALLPAPER_FORMAT_OK;
}

int wallpaper_build_bing_url(const char* urlbase, char* output, size_t output_size) {
    static const char prefix[] = "https://www.bing.com";
    static const char suffix[] = "_480x800.jpg";
    if (!urlbase || !output || strncmp(urlbase, "/th?id=OHR.", 11) != 0 ||
        strstr(urlbase, "..") || strstr(urlbase, "://")) {
        return -1;
    }
    for (const unsigned char* cursor = (const unsigned char*)urlbase; *cursor; ++cursor) {
        if (!(isalnum(*cursor) || strchr("/?=&._-", *cursor))) return -1;
    }
    const int length = snprintf(output, output_size, "%s%s%s", prefix, urlbase, suffix);
    return length > 0 && (size_t)length < output_size ? 0 : -1;
}
