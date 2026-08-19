// SPDX-License-Identifier: MIT
#include "wallpaper_format.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    const uint8_t jpeg[] = {
        0xff, 0xd8,
        0xff, 0xe0, 0x00, 0x04, 0x00, 0x00,
        0xff, 0xc0, 0x00, 0x08, 0x08, 0x03, 0x20, 0x01, 0xe0, 0x00,
        0xff, 0xda, 0x00, 0x02,
        0xff, 0xd9,
    };
    wallpaper_jpeg_info_t info = {0};
    assert(wallpaper_jpeg_validate(jpeg, sizeof(jpeg), 1024, &info) == WALLPAPER_FORMAT_OK);
    assert(info.width == 480 && info.height == 800);
    assert(wallpaper_jpeg_validate(jpeg, sizeof(jpeg), 8, &info) ==
           WALLPAPER_FORMAT_TOO_LARGE);

    uint8_t malformed[sizeof(jpeg)];
    memcpy(malformed, jpeg, sizeof(jpeg));
    malformed[0] = 0;
    assert(wallpaper_jpeg_validate(malformed, sizeof(malformed), 1024, &info) ==
           WALLPAPER_FORMAT_INVALID);
    memcpy(malformed, jpeg, sizeof(jpeg));
    malformed[13] = 0;
    malformed[14] = 0;
    assert(wallpaper_jpeg_validate(malformed, sizeof(malformed), 1024, &info) ==
           WALLPAPER_FORMAT_INVALID);
    assert(wallpaper_jpeg_validate(jpeg, sizeof(jpeg) - 1, 1024, &info) ==
           WALLPAPER_FORMAT_INVALID);

    char url[256];
    assert(wallpaper_build_bing_url("/th?id=OHR.Example_ZH-CN123", url, sizeof(url)) == 0);
    assert(strcmp(url, "https://www.bing.com/th?id=OHR.Example_ZH-CN123_480x800.jpg") == 0);
    assert(wallpaper_build_bing_url("https://evil.invalid/a", url, sizeof(url)) != 0);
    assert(wallpaper_build_bing_url("/th?id=OHR../secret", url, sizeof(url)) != 0);
    assert(wallpaper_build_bing_url("/th?id=OHR.bad%2Fpath", url, sizeof(url)) != 0);
    return 0;
}
