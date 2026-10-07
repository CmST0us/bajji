// SPDX-License-Identifier: MIT
#include "passport_ui.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include "lvgl.h"
#include "src/libs/tjpgd/tjpgd.h"

LV_FONT_DECLARE(bajji_font_16);
LV_FONT_DECLARE(bajji_font_20);
namespace bajji {
namespace {
constexpr std::size_t kJpegWorkSize = 4096;
constexpr std::size_t kJpegDecodeBudget = 40 * 1024;
constexpr std::uint32_t kBase = 0x05070c, kSurface = 0x101a27, kAccent = 0x54d7ff;
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
struct JpegSession {
    lv_fs_file_t file{};
    lv_draw_buf_t* target{};
};

std::size_t jpeg_read(JDEC* decoder, std::uint8_t* buffer, std::size_t length) {
    auto* session = static_cast<JpegSession*>(decoder->device);
    if (buffer) {
        std::uint32_t read = 0;
        if (lv_fs_read(&session->file, buffer, length, &read) != LV_FS_RES_OK) return 0;
        return read;
    }
    std::uint32_t position = 0;
    if (lv_fs_tell(&session->file, &position) != LV_FS_RES_OK) return 0;
    if (lv_fs_seek(&session->file, position + length, LV_FS_SEEK_SET) != LV_FS_RES_OK) return 0;
    return length;
}

int jpeg_write(JDEC* decoder, void* bitmap, JRECT* rect) {
    auto* session = static_cast<JpegSession*>(decoder->device);
    lv_draw_buf_t* target = session->target;
    const std::int32_t width = static_cast<std::int32_t>(target->header.w);
    const std::int32_t height = static_cast<std::int32_t>(target->header.h);
    const std::int32_t stride = static_cast<std::int32_t>(target->header.stride);
    const std::int32_t span = static_cast<std::int32_t>(rect->right) - rect->left + 1;
    // TJpgDec descales the rect itself, so these are already destination coordinates. They
    // cannot leave the buffer for a well formed image; clamp anyway rather than trust the file.
    if (rect->right >= width || rect->bottom >= height || span <= 0) return 0;
    const auto* source = static_cast<const std::uint8_t*>(bitmap);
    for (std::int32_t y = rect->top; y <= static_cast<std::int32_t>(rect->bottom); ++y) {
        auto* out = reinterpret_cast<std::uint16_t*>(target->data + y * stride) + rect->left;
        for (std::int32_t x = 0; x < span; ++x) {
            // tjpgd.c:885 writes blue first, which is also LVGL's RGB888 byte order.
            const std::uint8_t blue = source[0];
            const std::uint8_t green = source[1];
            const std::uint8_t red = source[2];
            source += 3;
            out[x] = static_cast<std::uint16_t>(((red & 0xf8) << 8) | ((green & 0xfc) << 3) |
                                                (blue >> 3));
        }
    }
    return 1;
}

static lv_draw_buf_t* decode_jpeg_scaled(const char* path) {
    auto* session = new (std::nothrow) JpegSession;
    if (!session) return nullptr;
    if (lv_fs_open(&session->file, path, LV_FS_MODE_RD) != LV_FS_RES_OK) {
        delete session;
        return nullptr;
    }
    auto* work = lv_malloc(kJpegWorkSize);
    auto* decoder = static_cast<JDEC*>(lv_malloc(sizeof(JDEC)));
    lv_draw_buf_t* buffer = nullptr;
    if (work && decoder && jd_prepare(decoder, jpeg_read, work, kJpegWorkSize, session) == JDR_OK) {
        // 64-bit: width and height are 16 bit each, so the product times two overflows a
        // 32-bit size_t near the top of the range and would pick scale 0 for a huge image.
        std::uint8_t scale = 0;
        while (scale < 3 && static_cast<std::uint64_t>(decoder->width >> scale) *
                                    (decoder->height >> scale) * 2U > kJpegDecodeBudget) {
            ++scale;
        }
        const std::uint32_t width = decoder->width >> scale;
        const std::uint32_t height = decoder->height >> scale;
        if (width && height && static_cast<std::uint64_t>(width) * height * 2 <= kJpegDecodeBudget) {
            buffer = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        }
        if (buffer) {
            session->target = buffer;
            const JRESULT result = jd_decomp(decoder, jpeg_write, scale);
            if (result != JDR_OK) {
                LV_LOG_WARN("jd_decomp failed: %d", result);
                lv_draw_buf_destroy(buffer);
                buffer = nullptr;
            } else {
                lv_draw_buf_flush_cache(buffer, nullptr);
                LV_LOG_USER("jpeg %ux%u decoded at 1/%u", decoder->width, decoder->height,
                            1U << scale);
            }
        }
    }
    lv_free(decoder);
    lv_free(work);
    lv_fs_close(&session->file);
    delete session;
    return buffer;
}


lv_obj_t* text(lv_obj_t* parent, const char* value, int x, int y, int width,
               const lv_font_t* font = &bajji_font_16, std::uint32_t tint = 0xf4f8ff) {
    auto* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_text(label, value);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(tint), 0);
    return label;
}
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, std::uint32_t fill) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(fill), 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
void key_hint(lv_obj_t* parent, const char* title, int y, std::uint32_t tint) {
    auto* hint = panel(parent, 192, y, 48, 36, kSurface);
    lv_obj_set_style_border_width(hint, 2, 0);
    lv_obj_set_style_border_color(hint, lv_color_hex(tint), 0);
    text(hint, title, 6, 9, 38, &bajji_font_16, tint);
}
const Choice* types(const char* category, unsigned& count) {
    if (!std::strcmp(category, "bq")) { count = std::size(kBqTypes); return kBqTypes; }
    if (!std::strcmp(category, "acg")) { count = std::size(kAcgTypes); return kAcgTypes; }
    if (!std::strcmp(category, "furry")) { count = std::size(kFurryTypes); return kFurryTypes; }
    count = 0; return nullptr;
}
}

bool ProductUI::image_visible() const { return page_ == Page::home && wallpaper_.has_cache; }
void ProductUI::create(const ble_link_status_t& link, const wifi_link_status_t& wifi,
                       const WallpaperStatus& wallpaper) {
    link_ = link; wifi_ = wifi; wallpaper_ = wallpaper;
    board_ = BoardHal::instance().snapshot();
    draw();
}
void ProductUI::enter(Page page) { page_ = page; selected_ = 0; action_error_ = ESP_OK; draw(); }
unsigned ProductUI::item_count() const {
    switch (page_) {
        case Page::settings: return 7;
        case Page::category: return std::size(kCategories);
        case Page::type: { unsigned count; types(category_, count); return count; }
        case Page::brightness: return 3;
        case Page::interval: return 5;
        case Page::unpair: return 2;
        default: return 0;
    }
}
void ProductUI::draw() {
    if (root_) lv_obj_delete(root_);
    root_ = panel(lv_screen_active(), 0, 0, 240, 320, kBase);
    lv_obj_set_style_radius(root_, 0, 0);
    status_label_ = nullptr;
    if (image_revision_ != wallpaper_.revision || page_ != Page::home || wallpaper_.busy) {
        if (image_) lv_draw_buf_destroy(static_cast<lv_draw_buf_t*>(image_));
        image_ = nullptr;
        image_revision_ = 0;
    }
    if (page_ == Page::home && wallpaper_.has_cache && !wallpaper_.busy) {
        if (!image_) {
            image_ = decode_jpeg_scaled(wallpaper_.lvgl_path);
            image_revision_ = wallpaper_.revision;
        }
        if (image_) {
            auto* image = lv_image_create(root_);
            lv_image_set_src(image, image_);
            lv_image_set_antialias(image, false);
            lv_obj_set_size(image, 240, 320);
            lv_image_set_inner_align(image, wallpaper_.settings.display_mode == DisplayMode::cover
                                               ? LV_IMAGE_ALIGN_COVER : LV_IMAGE_ALIGN_CONTAIN);
        }
    }
    // The product front view puts UP, DOWN and OK down the right edge. Preserve the
    // yellow/blue action identities, but never reuse StopWatch's top-edge/chord hints.
    key_hint(root_, page_ == Page::home ? "显示" : LV_SYMBOL_UP, 24, 0xffc52f);
    key_hint(root_, page_ == Page::home ? "换图" : LV_SYMBOL_DOWN, 140, 0x2f8fff);
    key_hint(root_, page_ == Page::home ? "设置" : "OK", 256, kAccent);
    if (page_ == Page::home) {
        if (!image_) {
            text(root_, "Bajji", 12, 34, 164, &bajji_font_20, kAccent);
            text(root_, "AI Passport", 12, 65, 164);
            const char* message = wallpaper_.busy ? "正在加载图片" : wallpaper_.has_cache ?
                "图片加载失败" : link_.has_bond ? "选择图片参数" : "请在 iPhone 配对";
            text(root_, message, 12, 106, 168, &bajji_font_20);
            text(root_, "OK: 设置\nUP: 显示\nDOWN: 换图", 12, 210, 170);
        }
        if (link_.connected && !link_.encrypted) {
            auto* box = panel(root_, 8, 88, 174, 110, kSurface);
            text(box, "配对码", 10, 12, 150);
            char code[12]; std::snprintf(code, sizeof(code), "%06lu", (unsigned long)link_.passkey);
            text(box, code, 10, 46, 152, &lv_font_montserrat_28, kAccent);
        }
        auto* footer = panel(root_, 0, 292, 184, 28, kSurface);
        status_label_ = text(footer, wallpaper_.online ? "在线" : "离线", 10, 6, 174, &bajji_font_16, kAccent);
    } else {
        const char* heading = "设备设置";
        switch (page_) {
            case Page::category: heading = "图片分类"; break;
            case Page::type: heading = "图片类型"; break;
            case Page::brightness: heading = "屏幕亮度"; break;
            case Page::interval: heading = "定时刷新"; break;
            case Page::wifi: heading = "Wi-Fi"; break;
            case Page::pairing: heading = "设备配对"; break;
            case Page::unpair: heading = "解除配对"; break;
            default: break;
        }
        text(root_, heading, 12, 12, 172, &bajji_font_20);
        char battery[24];
        if (board_.pmic == Health::ok) std::snprintf(battery, sizeof(battery), "%u%%", board_.battery_percent);
        else std::snprintf(battery, sizeof(battery), "--%%");
        text(root_, battery, 12, 42, 160, &bajji_font_16, kAccent);
        if (page_ == Page::wifi) {
            if (wifi_.portal_state != WIFI_PORTAL_OFF) {
                auto* qr = lv_qrcode_create(root_);
                lv_qrcode_set_size(qr, 144);
                lv_obj_set_pos(qr, 16, 74);
                lv_qrcode_set_dark_color(qr, lv_color_black());
                lv_qrcode_set_light_color(qr, lv_color_white());
                char code[80]; std::snprintf(code, sizeof(code), "WIFI:T:nopass;S:%s;;", wifi_.portal_ssid);
                lv_qrcode_update(qr, code, std::strlen(code));
                text(root_, wifi_.portal_ssid, 12, 226, 168);
                text(root_, "bajji.setup\nOK: 停止配网", 12, 249, 172);
            } else {
                text(root_, wifi_.connected ? wifi_.network_ssid : "网络不可用", 12, 92, 166);
                text(root_, "OK: 启动配网", 12, 200, 166);
            }
        } else if (page_ == Page::pairing) {
            text(root_, link_.has_bond ? "已配对" : "等待配对", 12, 90, 166);
            text(root_, "Bajji Passport", 12, 124, 170);
            text(root_, "OK: 解除配对", 12, 200, 166);
        } else {
            const auto count = item_count();
            const auto first = (selected_ / 3) * 3;
            for (unsigned index = first; index < std::min(first + 3, count); ++index) {
                char value[80]{};
                switch (page_) {
                    case Page::settings: {
                        constexpr const char* labels[] = {"图片分类", "屏幕亮度", "定时刷新", "Wi-Fi", "设备配对", "保存并加载", "返回"};
                        std::snprintf(value, sizeof(value), "%s", labels[index]); break;
                    }
                    case Page::category: std::snprintf(value, sizeof(value), "%s", kCategories[index].label); break;
                    case Page::type: { unsigned n; std::snprintf(value, sizeof(value), "%s", types(category_, n)[index].label); break; }
                    case Page::brightness: std::snprintf(value, sizeof(value), "%u%%", (index + 1) * 30 + (index == 2 ? 10 : 0)); break;
                    case Page::interval:
                        if (index == 0) std::snprintf(value, sizeof(value), "关闭");
                        else if (index == 1) std::snprintf(value, sizeof(value), "- 1 min");
                        else if (index == 2) std::snprintf(value, sizeof(value), "+ 1 min");
                        else if (index == 3) std::snprintf(value, sizeof(value), "+ 10 min");
                        else std::snprintf(value, sizeof(value), "保存 %u min", interval_);
                        break;
                    case Page::unpair: std::snprintf(value, sizeof(value), "%s", index ? "确认解除" : "取消"); break;
                    default: break;
                }
                const bool focus = index == selected_;
                auto* row = panel(root_, 10, 76 + (index - first) * 56, 172, 48, focus ? kAccent : kSurface);
                text(row, value, 10, 13, 150, &bajji_font_16, focus ? kBase : 0xf4f8ff);
            }
            char position[24]; std::snprintf(position, sizeof(position), "%u / %u", selected_ + 1, count);
            text(root_, position, 12, 251, 168, &bajji_font_16, kAccent);
        }
        status_label_ = text(root_, "长按 OK: 返回", 12, 298, 178, &bajji_font_16, kAccent);
    }
    if (action_error_ != ESP_OK && status_label_) lv_label_set_text(status_label_, "操作失败，请重试");
    last_draw_ = lv_tick_get();
}
void ProductUI::back() {
    if (page_ == Page::home) { enter(Page::settings); return; }
    if (page_ == Page::settings) { enter(Page::home); return; }
    if (page_ == Page::type) { enter(Page::category); return; }
    if (page_ == Page::unpair) { enter(Page::pairing); return; }
    enter(Page::settings);
}
void ProductUI::select() {
    action_error_ = ESP_OK;
    switch (page_) {
        case Page::home: enter(Page::settings); return;
        case Page::settings:
            switch (selected_) {
                case 0: enter(Page::category); return;
                case 1: enter(Page::brightness); return;
                case 2: interval_ = wallpaper_.settings.auto_refresh_minutes; enter(Page::interval); return;
                case 3: enter(Page::wifi); return;
                case 4: enter(Page::pairing); return;
                case 5:
                    if (!wallpaper_.settings.configured) action_error_ = wallpaper_save_settings("", "");
                    else wallpaper_request_refresh();
                    if (action_error_ == ESP_OK) { enter(Page::home); return; }
                    break;
                case 6: enter(Page::home); return;
            }
            break;
        case Page::category: {
            std::snprintf(category_, sizeof(category_), "%s", kCategories[selected_].value);
            unsigned count; types(category_, count);
            if (count) { enter(Page::type); return; }
            action_error_ = wallpaper_save_settings(category_, "");
            if (action_error_ == ESP_OK) { enter(Page::home); return; }
            break;
        }
        case Page::type: {
            unsigned count;
            action_error_ = wallpaper_save_settings(category_, types(category_, count)[selected_].value);
            if (action_error_ == ESP_OK) { enter(Page::home); return; }
            break;
        }
        case Page::brightness:
            action_error_ = BoardHal::instance().set_brightness((selected_ + 1) * 30 + (selected_ == 2 ? 10 : 0));
            if (action_error_ == ESP_OK) { enter(Page::settings); return; }
            break;
        case Page::interval:
            if (selected_ == 1) interval_ = std::max<int>(1, interval_ - 1);
            else if (selected_ == 2 || selected_ == 3) interval_ = std::min<int>(1440, interval_ + (selected_ == 2 ? 1 : 10));
            else {
                action_error_ = wallpaper_set_auto_refresh(selected_ == 0 ? 0 : interval_);
                if (action_error_ == ESP_OK) { enter(Page::settings); return; }
            }
            break;
        case Page::wifi:
            action_error_ = wifi_.portal_state == WIFI_PORTAL_OFF ? wifi_link_start_portal() : wifi_link_stop_portal();
            wifi_ = wifi_link_snapshot(); break;
        case Page::pairing: enter(Page::unpair); return;
        case Page::unpair:
            if (!selected_) { enter(Page::pairing); return; }
            action_error_ = ble_link_clear_bond();
            if (action_error_ == ESP_OK) { enter(Page::home); return; }
            break;
    }
    draw();
}
void ProductUI::refresh(const BoardStatus& board, const ble_link_status_t& link,
                        const wifi_link_status_t& wifi, const WallpaperStatus& wallpaper,
                        const ButtonEvents& buttons) {
    const bool changed = wallpaper.revision != wallpaper_.revision || wallpaper.busy != wallpaper_.busy ||
        wallpaper.settings.display_mode != wallpaper_.settings.display_mode ||
        link.passkey != link_.passkey || link.encrypted != link_.encrypted ||
        link.connected != link_.connected || link.has_bond != link_.has_bond ||
        wifi.portal_state != wifi_.portal_state;
    if (link.connected && !link.encrypted &&
        (!link_.connected || link.passkey != link_.passkey)) page_ = Page::home;
    board_ = board; link_ = link; wifi_ = wifi; wallpaper_ = wallpaper;
    if (buttons.back_pressed) {
        if (page_ == Page::home && wallpaper.busy) { action_error_ = wallpaper_cancel_request(); draw(); }
        else back();
    } else if (buttons.ok_pressed) select();
    else if (page_ == Page::home) {
        if (buttons.a_pressed) {
            action_error_ = wallpaper_set_display_mode(wallpaper.settings.display_mode == DisplayMode::cover ?
                                                       DisplayMode::fit_blur : DisplayMode::cover);
        }
        if (buttons.b_pressed && !wallpaper.busy) wallpaper_request_refresh();
        if (changed || buttons.a_pressed || buttons.b_pressed) draw();
    } else if (buttons.a_pressed || buttons.b_pressed) {
        const auto count = item_count();
        if (count) selected_ = (selected_ + (buttons.a_pressed ? count - 1 : 1)) % count;
        draw();
    } else if (changed || (page_ != Page::home && lv_tick_get() - last_draw_ >= 5000)) draw();
    if (page_ == Page::home && status_label_) {
        const char* status = action_error_ != ESP_OK ? "操作失败，请重试" : wallpaper.busy ?
            "加载中 / 长按 OK 取消" : wallpaper.last_error ? "加载失败 / DOWN 重试" :
            wallpaper.online ? "在线" : "离线";
        lv_label_set_text(status_label_, status);
    }
}
}  // namespace bajji
