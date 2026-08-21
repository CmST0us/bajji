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
    wallpaper_media_info_t info = {0};
    assert(wallpaper_media_validate(jpeg, sizeof(jpeg), 1024, &info) == WALLPAPER_FORMAT_OK);
    assert(info.format == WALLPAPER_MEDIA_JPEG && !info.animated);
    assert(info.width == 480 && info.height == 800);
    assert(wallpaper_media_validate(jpeg, sizeof(jpeg), 8, &info) ==
           WALLPAPER_FORMAT_TOO_LARGE);

    uint8_t malformed[sizeof(jpeg)];
    memcpy(malformed, jpeg, sizeof(jpeg));
    malformed[0] = 0;
    assert(wallpaper_media_validate(malformed, sizeof(malformed), 1024, &info) ==
           WALLPAPER_FORMAT_INVALID);
    memcpy(malformed, jpeg, sizeof(jpeg));
    malformed[13] = 0;
    malformed[14] = 0;
    assert(wallpaper_media_validate(malformed, sizeof(malformed), 1024, &info) ==
           WALLPAPER_FORMAT_INVALID);
    assert(wallpaper_media_validate(jpeg, sizeof(jpeg) - 1, 1024, &info) ==
           WALLPAPER_FORMAT_INVALID);

    const uint8_t png[] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
        0x00, 0x00, 0x01, 0xd2, 0x00, 0x00, 0x01, 0xd2,
        0x08, 0x06, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 'I', 'E', 'N', 'D',
        0x00, 0x00, 0x00, 0x00,
    };
    assert(wallpaper_media_validate(png, sizeof(png), 1024, &info) == WALLPAPER_FORMAT_OK);
    assert(info.format == WALLPAPER_MEDIA_PNG && info.width == 466 && info.height == 466);

    const uint8_t gif[] = {
        'G', 'I', 'F', '8', '9', 'a', 0xd2, 0x01, 0xd2, 0x01, 0x00, 0x00, 0x00, 0x3b,
    };
    assert(wallpaper_media_validate(gif, sizeof(gif), 1024, &info) == WALLPAPER_FORMAT_OK);
    assert(info.format == WALLPAPER_MEDIA_GIF && info.animated && info.width == 466);

    const uint8_t webp[] = {
        'R', 'I', 'F', 'F', 22, 0, 0, 0, 'W', 'E', 'B', 'P',
        'V', 'P', '8', 'X', 10, 0, 0, 0, 0x02, 0, 0, 0,
        0xd1, 0x01, 0x00, 0xd1, 0x01, 0x00,
    };
    assert(wallpaper_media_validate(webp, sizeof(webp), 1024, &info) == WALLPAPER_FORMAT_OK);
    assert(info.format == WALLPAPER_MEDIA_WEBP && info.animated && info.width == 466);

    char url[256];
    assert(wallpaper_build_random_url("bq", "eciyuan", url, sizeof(url)) == 0);
    assert(strcmp(url, "https://uapis.cn/api/v1/random/image?category=bq&type=eciyuan") == 0);
    assert(wallpaper_build_random_url("landscape", "", url, sizeof(url)) == 0);
    assert(strcmp(url, "https://uapis.cn/api/v1/random/image?category=landscape") == 0);
    assert(wallpaper_build_random_url("", "", url, sizeof(url)) == 0);
    assert(wallpaper_build_random_url("landscape", "eciyuan", url, sizeof(url)) != 0);
    assert(wallpaper_build_random_url("../bad", "", url, sizeof(url)) != 0);
    return 0;
}
