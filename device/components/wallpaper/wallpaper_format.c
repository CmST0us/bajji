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

static uint32_t read_le24(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U);
}

static uint32_t read_le32(const uint8_t* data) {
    return read_le24(data) | ((uint32_t)data[3] << 24U);
}

static uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];
}

static wallpaper_format_result_t validate_jpeg(const uint8_t* data, size_t size,
                                                wallpaper_media_info_t* info) {
    if (!data || !info || size < 4 || data[0] != 0xff || data[1] != 0xd8 ||
        data[size - 2] != 0xff || data[size - 1] != 0xd9) {
        return WALLPAPER_FORMAT_INVALID;
    }

    wallpaper_media_info_t found = {.format = WALLPAPER_MEDIA_JPEG};
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
            // TJpgDec, which is the only JPEG decoder on the device, handles baseline SOF0
            // and nothing else: every other SOF returns JDR_FMT3 (tjpgd.c:1073-1085). A
            // progressive JPEG passes every other check here, so without this it would be
            // downloaded, cached and then drawn as an empty rectangle.
            if (marker != 0xc0) return WALLPAPER_FORMAT_UNSUPPORTED;
            if (segment_size < 8) return WALLPAPER_FORMAT_INVALID;
            found.height = (uint16_t)(((uint16_t)data[offset + 3] << 8U) | data[offset + 4]);
            found.width = (uint16_t)(((uint16_t)data[offset + 5] << 8U) | data[offset + 6]);
            if (found.width == 0 || found.height == 0) return WALLPAPER_FORMAT_INVALID;

            // tjpgd.c:991-1007: grayscale or Y/Cb/Cr only, and of the chroma layouts only
            // 4:4:4, 4:2:2 and 4:2:0, with Cb/Cr sampled 1x1.
            const uint8_t components = data[offset + 7];
            if (components != 1 && components != 3) return WALLPAPER_FORMAT_UNSUPPORTED;
            if (segment_size < (size_t)(8 + 3 * components)) return WALLPAPER_FORMAT_INVALID;
            for (uint8_t index = 0; index < components; ++index) {
                const uint8_t sampling = data[offset + 9 + 3 * index];
                if (index == 0) {
                    if (sampling != 0x11 && sampling != 0x21 && sampling != 0x22) {
                        return WALLPAPER_FORMAT_UNSUPPORTED;
                    }
                } else if (sampling != 0x11) {
                    return WALLPAPER_FORMAT_UNSUPPORTED;
                }
                if (data[offset + 10 + 3 * index] > 3) return WALLPAPER_FORMAT_INVALID;
            }
        }
        if (marker == 0xda) break;  // Entropy data ends at the already-checked EOI marker.
        offset += segment_size;
    }
    if (found.width == 0 || found.height == 0) return WALLPAPER_FORMAT_INVALID;
    *info = found;
    return WALLPAPER_FORMAT_OK;
}

static wallpaper_format_result_t validate_png(const uint8_t* data, size_t size,
                                               wallpaper_media_info_t* info) {
    static const uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (size < 45 || memcmp(data, signature, sizeof(signature)) != 0 ||
        read_be32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4) != 0 ||
        memcmp(data + size - 8, "IEND", 4) != 0) {
        return WALLPAPER_FORMAT_INVALID;
    }
    const uint32_t width = read_be32(data + 16);
    const uint32_t height = read_be32(data + 20);
    if (!width || !height || width > UINT16_MAX || height > UINT16_MAX) {
        return WALLPAPER_FORMAT_INVALID;
    }
    *info = (wallpaper_media_info_t){
        .format = WALLPAPER_MEDIA_PNG,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
    };
    return WALLPAPER_FORMAT_OK;
}

static wallpaper_format_result_t validate_gif(const uint8_t* data, size_t size,
                                               wallpaper_media_info_t* info) {
    if (size < 14 || (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0) ||
        data[size - 1] != 0x3b) {
        return WALLPAPER_FORMAT_INVALID;
    }
    const uint16_t width = (uint16_t)data[6] | ((uint16_t)data[7] << 8U);
    const uint16_t height = (uint16_t)data[8] | ((uint16_t)data[9] << 8U);
    if (!width || !height) return WALLPAPER_FORMAT_INVALID;
    *info = (wallpaper_media_info_t){
        .format = WALLPAPER_MEDIA_GIF,
        .width = width,
        .height = height,
        .animated = true,
    };
    return WALLPAPER_FORMAT_OK;
}

static wallpaper_format_result_t validate_webp(const uint8_t* data, size_t size,
                                                wallpaper_media_info_t* info) {
    if (size < 30 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WEBP", 4) != 0 ||
        read_le32(data + 4) + 8U > size) {
        return WALLPAPER_FORMAT_INVALID;
    }
    uint32_t width = 0;
    uint32_t height = 0;
    bool animated = false;
    size_t offset = 12;
    while (offset + 8 <= size) {
        const uint8_t* chunk = data + offset;
        const uint32_t chunk_size = read_le32(chunk + 4);
        if (chunk_size > size - offset - 8U) return WALLPAPER_FORMAT_TRUNCATED;
        const uint8_t* body = chunk + 8;
        if (memcmp(chunk, "VP8X", 4) == 0 && chunk_size >= 10) {
            animated = (body[0] & 0x02U) != 0;
            width = read_le24(body + 4) + 1U;
            height = read_le24(body + 7) + 1U;
        } else if (!width && memcmp(chunk, "VP8 ", 4) == 0 && chunk_size >= 10 &&
                   body[3] == 0x9d && body[4] == 0x01 && body[5] == 0x2a) {
            width = ((uint32_t)body[6] | ((uint32_t)body[7] << 8U)) & 0x3fffU;
            height = ((uint32_t)body[8] | ((uint32_t)body[9] << 8U)) & 0x3fffU;
        } else if (!width && memcmp(chunk, "VP8L", 4) == 0 && chunk_size >= 5 &&
                   body[0] == 0x2f) {
            const uint32_t bits = read_le32(body + 1);
            width = (bits & 0x3fffU) + 1U;
            height = ((bits >> 14U) & 0x3fffU) + 1U;
        }
        offset += 8U + chunk_size + (chunk_size & 1U);
    }
    if (!width || !height || width > UINT16_MAX || height > UINT16_MAX) {
        return WALLPAPER_FORMAT_INVALID;
    }
    *info = (wallpaper_media_info_t){
        .format = WALLPAPER_MEDIA_WEBP,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .animated = animated,
    };
    return WALLPAPER_FORMAT_OK;
}

wallpaper_format_result_t wallpaper_media_validate(const uint8_t* data, size_t size,
                                                    size_t maximum_size,
                                                    wallpaper_media_info_t* info) {
    if (!data || !info) return WALLPAPER_FORMAT_INVALID;
    if (size > maximum_size) return WALLPAPER_FORMAT_TOO_LARGE;
    if (size >= 2 && data[0] == 0xff && data[1] == 0xd8) return validate_jpeg(data, size, info);
    if (size >= 8 && data[0] == 0x89 && memcmp(data + 1, "PNG", 3) == 0) {
        return validate_png(data, size, info);
    }
    if (size >= 6 && memcmp(data, "GIF8", 4) == 0) return validate_gif(data, size, info);
    if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
        return validate_webp(data, size, info);
    }
    return WALLPAPER_FORMAT_INVALID;
}

static int matches(const char* value, const char* const* allowed, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(value, allowed[index]) == 0) return 1;
    }
    return 0;
}

int wallpaper_settings_valid(const char* category, const char* type) {
    static const char* const categories[] = {
        "acg", "landscape", "anime", "pc_wallpaper", "mobile_wallpaper",
        "general_anime", "ai_drawing", "bq", "furry",
    };
    static const char* const bq[] = {"xiongmao", "waiguoren", "maomao", "ikun", "eciyuan"};
    static const char* const acg[] = {"pc", "mb"};
    static const char* const furry[] = {"z4k", "szs8k", "s4k", "4k"};
    if (!category || !type || (!category[0] && type[0]) ||
        (category[0] && !matches(category, categories, sizeof(categories) / sizeof(categories[0])))) {
        return 0;
    }
    if (!type[0]) return 1;
    if (strcmp(category, "bq") == 0) return matches(type, bq, sizeof(bq) / sizeof(bq[0]));
    if (strcmp(category, "acg") == 0) return matches(type, acg, sizeof(acg) / sizeof(acg[0]));
    if (strcmp(category, "furry") == 0) {
        return matches(type, furry, sizeof(furry) / sizeof(furry[0]));
    }
    return 0;
}

int wallpaper_build_random_url(const char* category, const char* type, uint32_t nonce,
                               char* output, size_t output_size) {
    static const char base[] = "https://uapis.cn/api/v1/random/image";
    if (!output || !output_size || !wallpaper_settings_valid(category, type)) return -1;
    const int length =
        !category[0]
            ? snprintf(output, output_size, "%s?_=%lu", base, (unsigned long)nonce)
            : !type[0]
                  ? snprintf(output, output_size, "%s?category=%s&_=%lu", base, category,
                             (unsigned long)nonce)
                  : snprintf(output, output_size, "%s?category=%s&type=%s&_=%lu", base, category,
                             type, (unsigned long)nonce);
    return length > 0 && (size_t)length < output_size ? 0 : -1;
}

// The upstream endpoint frequently serves progressive JPEGs, which TJpgDec - the only JPEG
// decoder on this device - cannot read at all, and full resolution files of a megabyte or
// more, which take minutes to pull over the BLE tunnel. The Worker in
// cloudflare/image-proxy/worker.mjs converts every still or animation to WebP and fixes the
// dimensions before transfer: cover is 466x466 at quality 90, while fit is contained in
// 328x328 at quality 85. The nonce remains in the query so each refresh rolls a new image.
static const char kOriginPrefix[] = "https://uapis.cn/api/v1/random/image?";
static const char kProxyBase[] =
    "https://bajji-image-proxy.eric3u.cc";

int wallpaper_build_proxy_url(const char* origin, bool fit, char* output, size_t output_size) {
    if (!origin || !output || !output_size) return -1;
    const size_t prefix_length = sizeof(kOriginPrefix) - 1;
    if (strncmp(origin, kOriginPrefix, prefix_length) != 0 || !origin[prefix_length]) return -1;
    const int length = snprintf(output, output_size, "%s/%s?%s", kProxyBase,
                                fit ? "fit" : "cover", origin + prefix_length);
    return length > 0 && (size_t)length < output_size ? 0 : -1;
}
