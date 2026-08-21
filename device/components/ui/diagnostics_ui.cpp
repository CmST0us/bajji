// SPDX-License-Identifier: MIT
#include "diagnostics_ui.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <new>

#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "webp/demux.h"

extern "C" const lv_font_t bajji_font_16;

namespace bajji {
namespace {

constexpr int kDisplay = 466;
constexpr int kSafeX = 69;
constexpr int kSafeWidth = 328;
constexpr std::uint32_t kControlsDurationMs = 3000;
constexpr std::uint32_t kFadeMs = 180;
constexpr std::uint32_t kPairSuccessMs = 600;

constexpr std::uint32_t kBase = 0x05070c;
constexpr std::uint32_t kSurface = 0x0b1522;
constexpr std::uint32_t kOverlay = 0x101a27;
constexpr std::uint32_t kAccent = 0x54d7ff;
constexpr std::uint32_t kPrimary = 0xf4f8ff;
constexpr std::uint32_t kSecondary = 0xa8b4c7;
constexpr std::uint32_t kSuccess = 0x74e6a5;
constexpr std::uint32_t kWarning = 0xffc857;
constexpr std::uint32_t kError = 0xff6b76;
constexpr std::uint32_t kButtonA = 0xffc52f;
constexpr std::uint32_t kButtonB = 0x2f8fff;
const lv_font_t* const kBodyFont = &bajji_font_16;

struct Choice {
    const char* value;
    const char* label;
};

constexpr Choice kCategories[] = {
    {"", "全部"},
    {"acg", "ACG"},
    {"landscape", "风景"},
    {"bq", "表情包（bq）"},
    {"furry", "Furry"},
    {"anime", "动漫"},
    {"pc_wallpaper", "电脑壁纸"},
    {"mobile_wallpaper", "手机壁纸"},
    {"general_anime", "综合动漫"},
    {"ai_drawing", "AI 绘画"},
};

constexpr Choice kBqTypes[] = {
    {"xiongmao", "熊猫"}, {"waiguoren", "外国人"}, {"maomao", "猫猫"},
    {"ikun", "IKUN"}, {"eciyuan", "二次元（eciyuan）"},
};
constexpr Choice kAcgTypes[] = {{"pc", "电脑（pc）"}, {"mb", "手机（mb）"}};
constexpr Choice kFurryTypes[] = {
    {"z4k", "竖屏 4K"}, {"szs8k", "竖屏 8K"}, {"s4k", "横屏 4K"}, {"4k", "通用 4K"},
};

lv_color_t color(std::uint32_t value) { return lv_color_hex(value); }
std::uint32_t now_ms() { return lv_tick_get(); }
bool deadline_passed(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

lv_obj_t* object(lv_obj_t* parent, int x, int y, int width, int height,
                 std::uint32_t background, int radius = 0) {
    auto* value = lv_obj_create(parent);
    lv_obj_set_pos(value, x, y);
    lv_obj_set_size(value, width, height);
    lv_obj_set_style_bg_color(value, color(background), 0);
    lv_obj_set_style_bg_opa(value, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(value, 0, 0);
    lv_obj_set_style_radius(value, radius, 0);
    lv_obj_set_style_pad_all(value, 0, 0);
    lv_obj_remove_flag(value, LV_OBJ_FLAG_SCROLLABLE);
    return value;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int width,
                const lv_font_t* font = kBodyFont,
                std::uint32_t text_color = kPrimary, lv_text_align_t align = LV_TEXT_ALIGN_CENTER) {
    auto* value = lv_label_create(parent);
    lv_obj_set_pos(value, x, y);
    lv_obj_set_width(value, width);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_label_set_text(value, text);
    lv_obj_set_style_text_font(value, font, 0);
    lv_obj_set_style_text_color(value, color(text_color), 0);
    lv_obj_set_style_text_align(value, align, 0);
    return value;
}

lv_obj_t* status_pill(lv_obj_t* parent, const char* text, std::uint32_t tint, int y) {
    auto* pill = object(parent, 153, y, 160, 48, kSurface, 24);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, color(tint), 0);
    auto* dot = object(pill, 25, 19, 10, 10, tint, 5);
    (void)dot;
    label(pill, text, 44, 14, 92, kBodyFont, tint,
          LV_TEXT_ALIGN_LEFT);
    return pill;
}

lv_obj_t* title(lv_obj_t* parent, const char* text, int y) {
    auto* value = label(parent, text, kSafeX, y, kSafeWidth,
                        kBodyFont, kPrimary);
    lv_obj_set_style_text_letter_space(value, 1, 0);
    return value;
}

lv_obj_t* spinner(lv_obj_t* parent, int x, int y, int size) {
    auto* value = lv_spinner_create(parent);
    lv_obj_set_pos(value, x, y);
    lv_obj_set_size(value, size, size);
    lv_spinner_set_anim_params(value, 900, 92);
    lv_obj_set_style_arc_width(value, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(value, color(kOverlay), LV_PART_MAIN);
    lv_obj_set_style_arc_width(value, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(value, color(kAccent), LV_PART_INDICATOR);
    lv_obj_remove_style(value, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(value, LV_OBJ_FLAG_CLICKABLE);
    return value;
}

lv_obj_t* setting_row(lv_obj_t* parent, int y, const char* name, lv_obj_t** value_out,
                      lv_event_cb_t callback, void* context) {
    auto* row = object(parent, kSafeX, y, kSafeWidth, 72, kSurface, 16);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, color(kOverlay), 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, context);
    label(row, name, 20, 10, 240, kBodyFont, kSecondary,
          LV_TEXT_ALIGN_LEFT);
    *value_out = label(row, "", 20, 38, 260, kBodyFont, kPrimary,
                       LV_TEXT_ALIGN_LEFT);
    label(row, "›", 282, 17, 28, &lv_font_montserrat_28, kAccent);
    return row;
}

const Choice* choices_for(const char* category, std::size_t* count) {
    if (std::strcmp(category, "bq") == 0) {
        *count = sizeof(kBqTypes) / sizeof(kBqTypes[0]);
        return kBqTypes;
    }
    if (std::strcmp(category, "acg") == 0) {
        *count = sizeof(kAcgTypes) / sizeof(kAcgTypes[0]);
        return kAcgTypes;
    }
    if (std::strcmp(category, "furry") == 0) {
        *count = sizeof(kFurryTypes) / sizeof(kFurryTypes[0]);
        return kFurryTypes;
    }
    *count = 0;
    return nullptr;
}

const char* choice_label(const Choice* choices, std::size_t count, const char* value,
                         const char* fallback) {
    for (std::size_t i = 0; i < count; ++i) {
        if (std::strcmp(choices[i].value, value) == 0) return choices[i].label;
    }
    return fallback;
}

struct WebPPlayer {
    std::uint8_t* file{};
    WebPAnimDecoder* decoder{};
    lv_draw_buf_t* frame{};
    lv_draw_buf_t* background{};
    lv_timer_t* timer{};
    lv_obj_t* images[2]{};
    std::uint32_t image_count{};
    int previous_timestamp{};
};

void webp_advance(WebPPlayer* player) {
    if (!player || !player->decoder || !player->frame) return;
    if (!WebPAnimDecoderHasMoreFrames(player->decoder)) {
        WebPAnimDecoderReset(player->decoder);
        player->previous_timestamp = 0;
    }
    std::uint8_t* pixels = nullptr;
    int timestamp = 0;
    if (!WebPAnimDecoderGetNext(player->decoder, &pixels, &timestamp) || !pixels) return;
    const std::uint32_t width = player->frame->header.w;
    const std::uint32_t height = player->frame->header.h;
    const std::uint32_t source_stride = width * 4U;
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(static_cast<std::uint8_t*>(player->frame->data) + y * player->frame->header.stride,
                    pixels + y * source_stride, source_stride);
    }
    lv_draw_buf_flush_cache(player->frame, nullptr);
    for (std::uint32_t i = 0; i < player->image_count; ++i) lv_obj_invalidate(player->images[i]);
    if (player->timer) {
        lv_timer_set_period(player->timer,
                            std::max(16, timestamp - player->previous_timestamp));
    }
    player->previous_timestamp = timestamp;
}

void webp_timer(lv_timer_t* timer) {
    webp_advance(static_cast<WebPPlayer*>(lv_timer_get_user_data(timer)));
}

void destroy_webp_player(WebPPlayer* player) {
    if (!player) return;
    if (player->timer) lv_timer_delete(player->timer);
    if (player->decoder) WebPAnimDecoderDelete(player->decoder);
    if (player->background) lv_draw_buf_destroy(player->background);
    if (player->frame) lv_draw_buf_destroy(player->frame);
    lv_free(player->file);
    delete player;
}

WebPPlayer* create_webp_player(lv_obj_t* parent, const char* path, bool fit, bool animated) {
    auto* player = new (std::nothrow) WebPPlayer;
    if (!player) return nullptr;
    std::uint32_t size = 0;
    player->file = static_cast<std::uint8_t*>(lv_fs_load_with_alloc(path, &size));
    WebPAnimDecoderOptions options{};
    if (!player->file || !WebPAnimDecoderOptionsInit(&options)) {
        destroy_webp_player(player);
        return nullptr;
    }
    options.color_mode = MODE_BGRA;
    options.use_threads = 0;
    WebPData data{player->file, size};
    if (!(player->decoder = WebPAnimDecoderNew(&data, &options))) {
        destroy_webp_player(player);
        return nullptr;
    }
    WebPAnimInfo info{};
    if (!WebPAnimDecoderGetInfo(player->decoder, &info) || !info.canvas_width ||
        !info.canvas_height ||
        !(player->frame = lv_draw_buf_create(info.canvas_width, info.canvas_height,
                                             LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO))) {
        destroy_webp_player(player);
        return nullptr;
    }
    webp_advance(player);
    if (fit) {
        player->background = lv_draw_buf_create(info.canvas_width, info.canvas_height,
                                                LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
        if (!player->background) {
            destroy_webp_player(player);
            return static_cast<WebPPlayer*>(nullptr);
        }
        for (std::uint32_t y = 0; y < info.canvas_height; ++y) {
            std::memcpy(static_cast<std::uint8_t*>(player->background->data) +
                            y * player->background->header.stride,
                        static_cast<std::uint8_t*>(player->frame->data) +
                            y * player->frame->header.stride,
                        info.canvas_width * 4U);
        }
        lv_draw_buf_flush_cache(player->background, nullptr);
    }
    auto add_image = [&](bool background) {
        auto* image = lv_image_create(parent);
        lv_image_set_src(image, background ? player->background : player->frame);
        if (background) {
            lv_obj_set_pos(image, -27, -27);
            lv_obj_set_size(image, 520, 520);
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
            lv_obj_set_style_blur_radius(image, 24, 0);
            lv_obj_set_style_blur_quality(image, LV_BLUR_QUALITY_PRECISION, 0);
            lv_obj_set_style_opa(image, LV_OPA_80, 0);
        } else if (fit) {
            lv_obj_set_pos(image, kSafeX, kSafeX);
            lv_obj_set_size(image, kSafeWidth, kSafeWidth);
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
        } else {
            lv_obj_set_pos(image, 0, 0);
            lv_obj_set_size(image, kDisplay, kDisplay);
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
        }
        lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
        player->images[player->image_count++] = image;
    };
    if (fit) {
        add_image(true);
        auto* veil = object(parent, 0, 0, kDisplay, kDisplay, kBase, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(veil, LV_OPA_20, 0);
    }
    add_image(false);
    if (animated) player->timer = lv_timer_create(webp_timer, 16, player);
    return player;
}

// lv_gif_set_src() with a path opens the file and re-reads it for every frame
// (lv_gif.c:155 GIF_openFile), and each SPIFFS read disables the flash cache on both
// cores, which stalls touch and the main loop for as long as the animation runs. Handing
// it an lv_image_dsc_t takes the GIF_openRAM path instead (lv_gif.c:152).
struct GifSource {
    lv_image_dsc_t dsc{};
    void* data{};
};

GifSource* load_gif(const char* path) {
    std::uint32_t size = 0;
    void* data = lv_fs_load_with_alloc(path, &size);
    if (!data) return nullptr;
    if (!size) {
        lv_free(data);
        return nullptr;
    }
    auto* source = new (std::nothrow) GifSource;
    if (!source) {
        lv_free(data);
        return nullptr;
    }
    source->data = data;
    source->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    source->dsc.header.cf = LV_COLOR_FORMAT_RAW;
    source->dsc.data = static_cast<const std::uint8_t*>(data);
    source->dsc.data_size = size;
    return source;
}

void destroy_gif_source(GifSource* source) {
    if (!source) return;
    lv_free(source->data);
    delete source;
}

}  // namespace

void ProductUI::create(const ble_link_status_t& link, const WallpaperStatus& wallpaper) {
    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, color(kBase), 0);
    lv_obj_set_style_text_color(screen, color(kPrimary), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    latest_wallpaper_ = wallpaper;
    draft_ = wallpaper.settings;
    display_mode_ = wallpaper.settings.display_mode;
    wallpaper_revision_ = wallpaper.revision;
    request_revision_ = wallpaper.request_revision;
    show(Page::startup, wallpaper);
    if (link.initialized) {
        if (!link.has_bond) show(Page::unpaired, wallpaper);
        else if (!wallpaper.settings.configured) show(Page::settings, wallpaper);
        else if (wallpaper.has_cache) show(Page::image, wallpaper);
        else show(Page::loading, wallpaper);
    }
}

void ProductUI::show(Page next, const WallpaperStatus& wallpaper, std::uint32_t passkey) {
    page_ = next;
    page_since_ms_ = now_ms();
    controls_ = nullptr;
    refresh_overlay_ = nullptr;
    cache_error_ = nullptr;
    cache_error_text_ = nullptr;
    hold_overlay_ = nullptr;
    hold_arc_ = nullptr;
    hold_ms_ = nullptr;
    pairing_code_ = nullptr;
    loading_state_ = nullptr;
    category_value_ = nullptr;
    type_value_ = nullptr;
    type_row_ = nullptr;
    controls_visible_ = false;
    controls_hiding_ = false;

    destroy_webp_player(static_cast<WebPPlayer*>(webp_player_));
    webp_player_ = nullptr;
    if (root_) lv_obj_delete(root_);
    if (still_image_) {
        auto* stale = static_cast<lv_draw_buf_t*>(still_image_);
        still_image_ = nullptr;
        lv_image_cache_drop(stale);  // no widget may hold a decoded view of it past this point
        lv_draw_buf_destroy(stale);
    }
    if (gif_source_) {
        destroy_gif_source(static_cast<GifSource*>(gif_source_));
        gif_source_ = nullptr;
    }
    root_ = object(lv_screen_active(), 0, 0, kDisplay, kDisplay, kBase, LV_RADIUS_CIRCLE);
    lv_obj_center(root_);
    lv_obj_set_style_border_width(root_, next == Page::image ? 0 : 2, 0);
    lv_obj_set_style_border_color(root_, color(kOverlay), 0);
    lv_obj_set_style_clip_corner(root_, true, 0);

    switch (next) {
        case Page::startup:
            label(root_, "BAJJI", 0, 68, kDisplay, &lv_font_montserrat_14, kAccent);
            spinner(root_, 177, 124, 112);
            title(root_, "正在启动", 266);
            status_pill(root_, "检查中", kAccent, 322);
            break;
        case Page::unpaired:
            status_pill(root_, "未配对", kWarning, 52);
            object(root_, 191, 128, 84, 84, kSurface, 42);
            label(root_, "!", 191, 134, 84, &lv_font_montserrat_48, kWarning);
            title(root_, "先与手机配对", 234);
            label(root_, "请打开手机端 Bajji\n发起设备配对请求", 93, 280, 280,
                  kBodyFont, kSecondary);
            label(root_, "手机需联网 · 等待配对请求", 100, 364, 266,
                  kBodyFont, kWarning);
            break;
        case Page::pairing_code: {
            auto* modal = object(root_, 69, 93, kSafeWidth, 280, kOverlay, 24);
            lv_obj_set_style_border_width(modal, 1, 0);
            lv_obj_set_style_border_color(modal, color(kAccent), 0);
            lv_obj_set_style_shadow_width(modal, 32, 0);
            lv_obj_set_style_shadow_offset_y(modal, 12, 0);
            lv_obj_set_style_shadow_opa(modal, LV_OPA_30, 0);
            label(modal, "在手机上输入配对码", 0, 38, kSafeWidth,
                  kBodyFont, kPrimary);
            label(modal, "配对码将在 5 分钟后失效", 0, 76, kSafeWidth,
                  kBodyFont, kSecondary);
            pairing_code_ = label(modal, "--- ---", 0, 112, kSafeWidth,
                                  &lv_font_montserrat_48, kAccent);
            lv_obj_set_style_text_letter_space(pairing_code_, 4, 0);
            label(modal, "等待手机确认…", 0, 214, kSafeWidth,
                  kBodyFont, kAccent);
            if (passkey) lv_label_set_text_fmt(pairing_code_, "%03" PRIu32 " %03" PRIu32,
                                                passkey / 1000, passkey % 1000);
            break;
        }
        case Page::pairing_success: {
            auto* modal = object(root_, 69, 93, kSafeWidth, 280, kOverlay, 24);
            lv_obj_set_style_border_width(modal, 1, 0);
            lv_obj_set_style_border_color(modal, color(kSuccess), 0);
            object(modal, 122, 34, 84, 84, kSurface, 42);
            label(modal, LV_SYMBOL_OK, 122, 55, 84, &lv_font_montserrat_32, kSuccess);
            label(modal, "配对成功", 0, 144, kSafeWidth,
                  kBodyFont, kPrimary);
            label(modal, "正在进入图片设置", 0, 190, kSafeWidth,
                  kBodyFont, kSuccess);
            break;
        }
        case Page::settings: {
            draft_ = wallpaper.settings;
            if (!draft_.category[0] && !draft_.configured) {
                std::snprintf(draft_.category, sizeof(draft_.category), "bq");
                std::snprintf(draft_.type, sizeof(draft_.type), "eciyuan");
            }
            status_pill(root_, "已配对", kSuccess, 32);
            title(root_, "图片设置", 100);
            setting_row(root_, 144, "分类", &category_value_,
                        category_row_clicked, this);
            type_row_ = setting_row(root_, 228, "类型", &type_value_,
                                    type_row_clicked, this);
            auto* save = object(root_, kSafeX, 328, kSafeWidth, 56, kAccent, 28);
            lv_obj_add_flag(save, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(save, save_clicked, LV_EVENT_CLICKED, this);
            label(save, "保存并加载", 0, 16, kSafeWidth,
                  kBodyFont, kBase);
            label(root_, "参数保存在设备本地", kSafeX, 399, kSafeWidth,
                  kBodyFont, kSuccess);
            update_settings_labels();
            break;
        }
        case Page::category:
        case Page::type: {
            title(root_, next == Page::category ? "选择分类" : "选择类型", 56);
            auto* list = object(root_, kSafeX, 102, kSafeWidth, 280, kBase, 0);
            lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scroll_dir(list, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(list, 8, 0);
            const Choice* choices = kCategories;
            std::size_t count = sizeof(kCategories) / sizeof(kCategories[0]);
            if (next == Page::type) choices = choices_for(draft_.category, &count);
            for (std::size_t i = 0; i < count; ++i) {
                auto* row = object(list, 0, 0, kSafeWidth, 48, kSurface, 16);
                lv_obj_set_style_min_height(row, 48, 0);
                lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
                const bool selected = std::strcmp(next == Page::category ? draft_.category : draft_.type,
                                                  choices[i].value) == 0;
                if (selected) {
                    lv_obj_set_style_border_width(row, 2, 0);
                    lv_obj_set_style_border_color(row, color(kAccent), 0);
                }
                label(row, choices[i].label, 16, 14, 260,
                      kBodyFont, kPrimary, LV_TEXT_ALIGN_LEFT);
                if (selected) label(row, LV_SYMBOL_OK, 280, 14, 32, &lv_font_montserrat_18, kAccent);
                lv_obj_add_event_cb(row, next == Page::category ? category_choice_clicked
                                                                : type_choice_clicked,
                                    LV_EVENT_CLICKED, this);
            }
            label(root_, next == Page::category ? "向上滑动查看更多" : "选择后自动返回",
                  kSafeX, 397, kSafeWidth, kBodyFont,
                  next == Page::category ? kSecondary : kAccent);
            break;
        }
        case Page::loading: {
            label(root_, "BAJJI IMAGE", 0, 72, kDisplay, &lv_font_montserrat_14, kAccent);
            spinner(root_, 167, 124, 132);
            title(root_, wallpaper.online ? "正在获取图片" : "等待手机连接", 284);
            auto* parameters = object(root_, 139, 334, 188, 36, kOverlay, 18);
            char text[48];
            std::snprintf(text, sizeof(text), "%s%s%s", wallpaper.settings.category,
                          wallpaper.settings.type[0] ? " · " : "", wallpaper.settings.type);
            label(parameters, text, 0, 10, 188, &lv_font_montserrat_14, kAccent);
            loading_state_ = label(root_, wallpaper.online ? "支持 PNG · JPG · GIF · WebP"
                                                            : "通过 BLE 使用手机网络",
                                   kSafeX, 376, kSafeWidth,
                                   kBodyFont, kSecondary);
            auto* cancel = object(root_, 153, 402, 160, 48, kSurface, 24);
            lv_obj_set_style_border_width(cancel, 1, 0);
            lv_obj_set_style_border_color(cancel, color(kSecondary), 0);
            lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(cancel, cancel_clicked, LV_EVENT_CLICKED, this);
            label(cancel, "取消", 0, 14, 160, kBodyFont, kPrimary);
            break;
        }
        case Page::image:
            show_image(wallpaper);
            break;
        case Page::error:
            object(root_, 185, 76, 96, 96, kSurface, 48);
            label(root_, "!", 185, 82, 96, &lv_font_montserrat_48, kError);
            title(root_, "无法获取图片", 198);
            label(root_, wallpaper.online ? "手机网络请求失败\n设备中也没有缓存图片"
                                          : "手机网络不可用\n设备中也没有缓存图片",
                  83, 246, 300, kBodyFont, kSecondary);
            {
                auto* retry = object(root_, 133, 330, 200, 48, kSurface, 24);
                auto* key = object(retry, 8, 8, 40, 32, kButtonB, 16);
                label(key, "B", 0, 8, 40, &lv_font_montserrat_14, kPrimary);
                label(retry, "刷新图片", 60, 14, 124,
                      kBodyFont, kPrimary, LV_TEXT_ALIGN_LEFT);
            }
            label(root_, "请检查手机网络与蓝牙连接", 100, 397, 266,
                  kBodyFont, kError);
            break;
    }
}

// LVGL's JPEG decoder only ever streams MCU blocks (lv_tjpgd.c:213 decoder_get_area); it
// never produces a whole decoded image and never populates the image cache. Drawing straight
// from a file therefore re-reads and re-decodes the entire wallpaper on every redraw, and
// each SPIFFS read stalls both cores while the flash cache is disabled. Decode once into an
// RGB565 buffer here so redraws are a plain blit from PSRAM.
static lv_draw_buf_t* decode_still(const char* path) {
    lv_image_header_t header{};
    if (lv_image_decoder_get_info(path, &header) != LV_RESULT_OK) return nullptr;
    if (!header.w || !header.h) return nullptr;
    auto* buffer = lv_draw_buf_create(header.w, header.h, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!buffer) return nullptr;

    // A canvas is the supported way to render into a draw buffer; it is never shown.
    auto* canvas = lv_canvas_create(lv_screen_active());
    if (!canvas) {
        lv_draw_buf_destroy(buffer);
        return nullptr;
    }
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_draw_buf(canvas, buffer);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_image_dsc_t image_dsc;
    lv_draw_image_dsc_init(&image_dsc);
    image_dsc.src = path;
    const lv_area_t area{0, 0, static_cast<std::int32_t>(header.w) - 1,
                         static_cast<std::int32_t>(header.h) - 1};
    lv_draw_image(&layer, &image_dsc, &area);
    lv_canvas_finish_layer(canvas, &layer);

    lv_obj_delete(canvas);  // the destructor drops the cache entry but leaves the buffer to us
    return buffer;
}

void ProductUI::show_image(const WallpaperStatus& wallpaper) {
    if (!root_ || !wallpaper.has_cache || !wallpaper.lvgl_path[0]) return;
    lv_image_cache_drop(wallpaper.lvgl_path);
    const bool gif = wallpaper.media.format == WALLPAPER_MEDIA_GIF;
    const bool fit = display_mode_ == DisplayMode::fit_blur;
    const bool webp = wallpaper.media.format == WALLPAPER_MEDIA_WEBP;

    lv_draw_buf_t* still = nullptr;
    GifSource* gif_source = nullptr;
    auto add_media = [&](bool background) -> lv_obj_t* {
        lv_obj_t* image = nullptr;
#if LV_USE_GIF
        if (gif) {
            image = lv_gif_create(root_);
            lv_gif_set_color_format(image, LV_COLOR_FORMAT_RGB565);
            if (gif_source) lv_gif_set_src(image, &gif_source->dsc);
            else lv_gif_set_src(image, wallpaper.lvgl_path);
            if (background) lv_gif_pause(image);
        } else
#endif
        {
            image = lv_image_create(root_);
            if (still) lv_image_set_src(image, still);
            else lv_image_set_src(image, wallpaper.lvgl_path);
        }
        if (background) {
            lv_obj_set_pos(image, -27, -27);
            lv_obj_set_size(image, 520, 520);
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
            lv_obj_set_style_blur_radius(image, 24, 0);
            lv_obj_set_style_blur_quality(image, LV_BLUR_QUALITY_PRECISION, 0);
            lv_obj_set_style_opa(image, LV_OPA_80, 0);
        } else if (fit) {
            lv_obj_set_pos(image, kSafeX, kSafeX);
            lv_obj_set_size(image, kSafeWidth, kSafeWidth);
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
        } else {
            lv_obj_set_pos(image, 0, 0);
            lv_obj_set_size(image, kDisplay, kDisplay);
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
        }
        lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
        return image;
    };

    if (webp) {
        webp_player_ = create_webp_player(root_, wallpaper.lvgl_path, fit,
                                          wallpaper.media.animated);
    }
    if (!webp_player_) {
        if (gif) {
            gif_source = load_gif(wallpaper.lvgl_path);
            gif_source_ = gif_source;
            if (!gif_source) LV_LOG_WARN("gif load failed; playing from file");
        } else {
            still = decode_still(wallpaper.lvgl_path);
            still_image_ = still;
            if (!still) LV_LOG_WARN("still decode failed; drawing from file");
        }
        if (fit) {
            add_media(true);
            auto* veil = object(root_, 0, 0, kDisplay, kDisplay, kBase, LV_RADIUS_CIRCLE);
            lv_obj_set_style_bg_opa(veil, LV_OPA_20, 0);
        }
        add_media(false);
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root_, root_clicked, LV_EVENT_CLICKED, this);

    controls_ = object(root_, 0, 0, kDisplay, kDisplay, kBase, 0);
    lv_obj_set_style_bg_opa(controls_, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(controls_, LV_OBJ_FLAG_CLICKABLE);

    auto* a = object(controls_, 35, 30, 132, 56, kButtonA, 28);
    lv_obj_set_style_border_width(a, 1, 0);
    lv_obj_set_style_border_color(a, color(0xffffff), 0);
    lv_obj_set_style_border_opa(a, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(a, color(kButtonA), 0);
    lv_obj_set_style_shadow_width(a, 16, 0);
    lv_obj_set_style_shadow_spread(a, 2, 0);
    lv_obj_set_style_shadow_opa(a, LV_OPA_50, 0);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    label(a, LV_SYMBOL_IMAGE, 76, 14, 36, &lv_font_montserrat_28, kBase);

    auto* b = object(controls_, 298, 30, 132, 56, kButtonB, 28);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, color(0xffffff), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(b, color(kButtonB), 0);
    lv_obj_set_style_shadow_width(b, 16, 0);
    lv_obj_set_style_shadow_spread(b, 2, 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_50, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    label(b, LV_SYMBOL_REFRESH, 20, 14, 36, &lv_font_montserrat_28, kPrimary);

    refresh_overlay_ = object(root_, 126, 352, 214, 48, kOverlay, 24);
    spinner(refresh_overlay_, 10, 8, 32);
    label(refresh_overlay_, "正在换一张…", 52, 14, 146,
          kBodyFont, kPrimary, LV_TEXT_ALIGN_LEFT);
    if (!wallpaper.busy) lv_obj_add_flag(refresh_overlay_, LV_OBJ_FLAG_HIDDEN);

    cache_error_ = object(root_, 83, 304, 300, 48, kOverlay, 24);
    lv_obj_set_style_border_width(cache_error_, 1, 0);
    lv_obj_set_style_border_color(cache_error_, color(kError), 0);
    cache_error_text_ = label(cache_error_, "网络失败 · 显示缓存", 0, 14, 300,
                              kBodyFont, kPrimary);
    lv_obj_add_flag(cache_error_, LV_OBJ_FLAG_HIDDEN);

    hold_overlay_ = object(root_, 0, 0, kDisplay, kDisplay, kBase, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(hold_overlay_, LV_OPA_70, 0);
    hold_arc_ = lv_arc_create(hold_overlay_);
    lv_obj_set_pos(hold_arc_, 163, 112);
    lv_obj_set_size(hold_arc_, 140, 140);
    lv_arc_set_range(hold_arc_, 0, 1000);
    lv_arc_set_value(hold_arc_, 0);
    lv_obj_set_style_arc_width(hold_arc_, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(hold_arc_, color(kOverlay), LV_PART_MAIN);
    lv_obj_set_style_arc_width(hold_arc_, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(hold_arc_, color(kAccent), LV_PART_INDICATOR);
    lv_obj_remove_style(hold_arc_, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(hold_arc_, LV_OBJ_FLAG_CLICKABLE);
    label(hold_overlay_, "A+B", 163, 159, 140, &lv_font_montserrat_20, kPrimary);
    label(hold_overlay_, "长按 1 秒", 163, 191, 140,
          kBodyFont, kSecondary);
    hold_ms_ = label(hold_overlay_, "0 / 1000 ms", 0, 278, kDisplay,
                     &lv_font_montserrat_14, kAccent);
    label(hold_overlay_, "继续按住返回设置", 69, 316, 328,
          kBodyFont, kPrimary);
    label(hold_overlay_, "松开任意按键即可取消", 69, 360, 328,
          kBodyFont, kSecondary);
    lv_obj_add_flag(hold_overlay_, LV_OBJ_FLAG_HIDDEN);
    show_controls();
}

void ProductUI::show_controls() {
    if (!controls_) return;
    lv_obj_remove_flag(controls_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(controls_, LV_OPA_TRANSP, 0);
    lv_obj_fade_in(controls_, kFadeMs, 0);
    controls_visible_ = true;
    controls_hiding_ = false;
    controls_deadline_ms_ = now_ms() + kControlsDurationMs;
}

void ProductUI::hide_hold() {
    if (hold_overlay_) lv_obj_add_flag(hold_overlay_, LV_OBJ_FLAG_HIDDEN);
}

void ProductUI::update_settings_labels() {
    if (!category_value_ || !type_value_) return;
    lv_label_set_text(category_value_, choice_label(kCategories,
        sizeof(kCategories) / sizeof(kCategories[0]), draft_.category, "全部"));
    std::size_t count = 0;
    const Choice* choices = choices_for(draft_.category, &count);
    const bool has_types = count > 0;
    lv_label_set_text(type_value_, has_types ? choice_label(choices, count, draft_.type, "请选择")
                                             : "无额外参数");
    if (has_types) {
        lv_obj_add_flag(type_row_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(type_row_, LV_OPA_COVER, 0);
    } else {
        draft_.type[0] = '\0';
        lv_obj_remove_flag(type_row_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(type_row_, LV_OPA_50, 0);
    }
}

void ProductUI::select_category(std::uint32_t index) {
    if (index >= sizeof(kCategories) / sizeof(kCategories[0])) return;
    char previous_type[sizeof(draft_.type)];
    std::snprintf(previous_type, sizeof(previous_type), "%s", draft_.type);
    std::snprintf(draft_.category, sizeof(draft_.category), "%s", kCategories[index].value);
    std::size_t count = 0;
    const Choice* choices = choices_for(draft_.category, &count);
    if (!count) draft_.type[0] = '\0';
    else if (!choice_label(choices, count, previous_type, nullptr)) {
        std::snprintf(draft_.type, sizeof(draft_.type), "%s", choices[0].value);
    } else {
        std::snprintf(draft_.type, sizeof(draft_.type), "%s", previous_type);
    }
    char selected_type[sizeof(draft_.type)];
    std::snprintf(selected_type, sizeof(selected_type), "%s", draft_.type);
    show(Page::settings, latest_wallpaper_);
    draft_.configured = latest_wallpaper_.settings.configured;
    std::snprintf(draft_.category, sizeof(draft_.category), "%s", kCategories[index].value);
    std::snprintf(draft_.type, sizeof(draft_.type), "%s", selected_type);
    update_settings_labels();
}

void ProductUI::select_type(std::uint32_t index) {
    std::size_t count = 0;
    const Choice* choices = choices_for(draft_.category, &count);
    if (!choices || index >= count) return;
    char category[sizeof(draft_.category)];
    std::snprintf(category, sizeof(category), "%s", draft_.category);
    show(Page::settings, latest_wallpaper_);
    std::snprintf(draft_.category, sizeof(draft_.category), "%s", category);
    std::snprintf(draft_.type, sizeof(draft_.type), "%s", choices[index].value);
    update_settings_labels();
}

void ProductUI::save_settings() {
    if (wallpaper_save_settings(draft_.category, draft_.type) != ESP_OK) return;
    latest_wallpaper_.settings = draft_;
    latest_wallpaper_.settings.configured = true;
    request_revision_ = latest_wallpaper_.request_revision;
    show(Page::loading, latest_wallpaper_);
}

void ProductUI::cancel_loading() {
    wallpaper_cancel_request();
    latest_wallpaper_.busy = false;
    show(Page::settings, latest_wallpaper_);
}

void ProductUI::toggle_mode(const WallpaperStatus& wallpaper) {
    display_mode_ = display_mode_ == DisplayMode::cover ? DisplayMode::fit_blur : DisplayMode::cover;
    wallpaper_set_display_mode(display_mode_);
    show(Page::image, wallpaper);
}

void ProductUI::refresh_image(const WallpaperStatus& wallpaper) {
    wallpaper_request_refresh();
    request_revision_ = wallpaper.request_revision;
    show_controls();
    if (refresh_overlay_) lv_obj_remove_flag(refresh_overlay_, LV_OBJ_FLAG_HIDDEN);
    if (!wallpaper.online && cache_error_) {
        lv_label_set_text(cache_error_text_, "等待手机连接 · 显示缓存");
        lv_obj_remove_flag(cache_error_, LV_OBJ_FLAG_HIDDEN);
        cache_error_deadline_ms_ = now_ms() + kControlsDurationMs;
    }
}

void ProductUI::refresh(const BoardStatus&, const ble_link_status_t& link,
                        const WallpaperStatus& wallpaper, const ButtonEvents& buttons) {
    latest_wallpaper_ = wallpaper;
    const std::uint32_t now = now_ms();

    if (link.passkey && page_ != Page::pairing_code) show(Page::pairing_code, wallpaper, link.passkey);
    if (page_ == Page::pairing_code) {
        if (pairing_code_ && link.passkey) {
            lv_label_set_text_fmt(pairing_code_, "%03" PRIu32 " %03" PRIu32,
                                  link.passkey / 1000, link.passkey % 1000);
        }
        if (link.has_bond && link.bonded) show(Page::pairing_success, wallpaper);
        else if (!link.passkey && !link.has_bond) show(Page::unpaired, wallpaper);
    } else if (page_ == Page::startup && link.initialized) {
        if (!link.has_bond) show(Page::unpaired, wallpaper);
        else if (!wallpaper.settings.configured) show(Page::settings, wallpaper);
        else if (wallpaper.has_cache) show(Page::image, wallpaper);
        else show(Page::loading, wallpaper);
    } else if (page_ == Page::unpaired && link.has_bond) {
        show(Page::pairing_success, wallpaper);
    } else if (page_ == Page::pairing_success &&
               deadline_passed(now, page_since_ms_ + kPairSuccessMs)) {
        if (!wallpaper.settings.configured) show(Page::settings, wallpaper);
        else if (wallpaper.has_cache) show(Page::image, wallpaper);
        else show(Page::loading, wallpaper);
    }

    if (page_ == Page::loading) {
        if (wallpaper.has_cache) {
            wallpaper_revision_ = wallpaper.revision;
            display_mode_ = wallpaper.settings.display_mode;
            show(Page::image, wallpaper);
        } else if (wallpaper.request_revision != request_revision_ &&
                   wallpaper.last_error != ESP_OK) {
            request_revision_ = wallpaper.request_revision;
            show(Page::error, wallpaper);
        } else if (loading_state_) {
            lv_label_set_text(loading_state_, wallpaper.online ? "支持 PNG · JPG · GIF · WebP"
                                                                : "通过 BLE 使用手机网络");
        }
    }

    if (page_ == Page::image) {
        if (wallpaper.has_cache && wallpaper.revision != wallpaper_revision_) {
            wallpaper_revision_ = wallpaper.revision;
            show(Page::image, wallpaper);
        }
        if (refresh_overlay_) {
            if (wallpaper.busy) lv_obj_remove_flag(refresh_overlay_, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(refresh_overlay_, LV_OBJ_FLAG_HIDDEN);
        }
        if (wallpaper.request_revision != request_revision_) {
            request_revision_ = wallpaper.request_revision;
            if (wallpaper.last_error != ESP_OK && cache_error_) {
                lv_label_set_text(cache_error_text_, "网络失败 · 显示缓存");
                lv_obj_remove_flag(cache_error_, LV_OBJ_FLAG_HIDDEN);
                cache_error_deadline_ms_ = now + kControlsDurationMs;
            } else if (cache_error_) {
                lv_obj_add_flag(cache_error_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (cache_error_ && !lv_obj_has_flag(cache_error_, LV_OBJ_FLAG_HIDDEN) &&
            deadline_passed(now, cache_error_deadline_ms_)) {
            lv_obj_add_flag(cache_error_, LV_OBJ_FLAG_HIDDEN);
        }
        if (buttons.a_pressed) toggle_mode(wallpaper);
        if (buttons.b_pressed) refresh_image(wallpaper);
        if (buttons.chord_started && hold_overlay_) lv_obj_remove_flag(hold_overlay_, LV_OBJ_FLAG_HIDDEN);
        if (hold_overlay_ && !lv_obj_has_flag(hold_overlay_, LV_OBJ_FLAG_HIDDEN)) {
            const int progress = std::min<std::uint32_t>(buttons.chord_progress_ms, 1000);
            lv_arc_set_value(hold_arc_, progress);
            lv_label_set_text_fmt(hold_ms_, "%d / 1000 ms", progress);
        }
        if (buttons.chord_cancelled) hide_hold();
        if (buttons.chord_completed) {
            hide_hold();
            show(Page::settings, wallpaper);
        }
        if (controls_visible_ && !controls_hiding_ && deadline_passed(now, controls_deadline_ms_)) {
            controls_hiding_ = true;
            controls_hide_started_ms_ = now;
            lv_obj_fade_out(controls_, kFadeMs, 0);
        } else if (controls_hiding_ &&
                   deadline_passed(now, controls_hide_started_ms_ + kFadeMs)) {
            lv_obj_add_flag(controls_, LV_OBJ_FLAG_HIDDEN);
            controls_hiding_ = false;
            controls_visible_ = false;
        }
    } else if (page_ == Page::error && buttons.b_pressed) {
        wallpaper_request_refresh();
        request_revision_ = wallpaper.request_revision;
        show(Page::loading, wallpaper);
    }
}

void ProductUI::root_clicked(lv_event_t* event) {
    static_cast<ProductUI*>(lv_event_get_user_data(event))->show_controls();
}
void ProductUI::category_row_clicked(lv_event_t* event) {
    auto* self = static_cast<ProductUI*>(lv_event_get_user_data(event));
    self->show(Page::category, self->latest_wallpaper_);
}
void ProductUI::type_row_clicked(lv_event_t* event) {
    auto* self = static_cast<ProductUI*>(lv_event_get_user_data(event));
    self->show(Page::type, self->latest_wallpaper_);
}
void ProductUI::save_clicked(lv_event_t* event) {
    static_cast<ProductUI*>(lv_event_get_user_data(event))->save_settings();
}
void ProductUI::cancel_clicked(lv_event_t* event) {
    static_cast<ProductUI*>(lv_event_get_user_data(event))->cancel_loading();
}
void ProductUI::category_choice_clicked(lv_event_t* event) {
    const auto index = static_cast<std::uint32_t>(lv_obj_get_index(lv_event_get_target_obj(event)));
    static_cast<ProductUI*>(lv_event_get_user_data(event))->select_category(index);
}
void ProductUI::type_choice_clicked(lv_event_t* event) {
    const auto index = static_cast<std::uint32_t>(lv_obj_get_index(lv_event_get_target_obj(event)));
    static_cast<ProductUI*>(lv_event_get_user_data(event))->select_type(index);
}
}  // namespace bajji
