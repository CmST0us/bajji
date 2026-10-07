// SPDX-License-Identifier: MIT
#include "passport_ui.hpp"
#include "lvgl.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>
#include "src/misc/lv_text_private.h"

static bajji::WallpaperStatus wallpaper;
static wifi_link_status_t wifi;
static ble_link_status_t link;
static unsigned refresh_count, unpair_count;
namespace bajji {
BoardHal& BoardHal::instance() { static BoardHal board; return board; }
BoardStatus BoardHal::snapshot() { BoardStatus b; b.pmic = Health::ok; b.battery_percent = 78; return b; }
esp_err_t BoardHal::set_brightness(std::uint8_t) { return ESP_OK; }
esp_err_t wallpaper_set_display_mode(DisplayMode mode) { wallpaper.settings.display_mode = mode; return ESP_OK; }
void wallpaper_request_refresh() { ++refresh_count; }
esp_err_t wallpaper_cancel_request() { wallpaper.busy = false; return ESP_OK; }
esp_err_t wallpaper_save_settings(const char* category, const char* type) {
    wallpaper.settings.configured = true;
    std::snprintf(wallpaper.settings.category, sizeof(wallpaper.settings.category), "%s", category);
    std::snprintf(wallpaper.settings.type, sizeof(wallpaper.settings.type), "%s", type);
    return ESP_OK;
}
esp_err_t wallpaper_set_auto_refresh(std::uint16_t value) { wallpaper.settings.auto_refresh_minutes = value; return ESP_OK; }
}
extern "C" {
esp_err_t wifi_link_start_portal() {
    wifi.portal_state = WIFI_PORTAL_READY;
    std::snprintf(wifi.portal_ssid, sizeof(wifi.portal_ssid), "Bajji-123456");
    return ESP_OK;
}
esp_err_t wifi_link_stop_portal() { wifi.portal_state = WIFI_PORTAL_OFF; return ESP_OK; }
wifi_link_status_t wifi_link_snapshot() { return wifi; }
esp_err_t ble_link_clear_bond() { ++unpair_count; link.has_bond = false; return ESP_OK; }
}
static std::array<std::uint16_t, 240 * 320> frame;
static void flush(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels) {
    const auto* source = reinterpret_cast<std::uint16_t*>(pixels);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) frame[y * 240 + x] = *source++;
    lv_display_flush_ready(display);
}
static void bounds(lv_obj_t* object) {
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 240 && area.y2 < 320);
    if (lv_obj_check_type(object, &lv_label_class)) {
        const auto* font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
        const char* value = lv_label_get_text(object);
        uint32_t index = 0;
        while (value[index]) {
            const auto cp = lv_text_encoded_next(value, &index);
            if (cp < 32) continue;
            lv_font_glyph_dsc_t glyph{};
            assert(lv_font_get_glyph_dsc(font, &glyph, cp, 0) && !glyph.is_placeholder);
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) bounds(lv_obj_get_child(object, i));
}
static void capture(const std::string& name) {
    lv_obj_update_layout(lv_screen_active());
    bounds(lv_screen_active());
    lv_refr_now(nullptr);
    auto* file = std::fopen(name.c_str(), "wb"); assert(file);
    std::fprintf(file, "P6\n240 320\n255\n");
    for (auto pixel : frame) {
        const unsigned char rgb[] = {static_cast<unsigned char>(((pixel >> 11) & 31) * 255 / 31),
            static_cast<unsigned char>(((pixel >> 5) & 63) * 255 / 63),
            static_cast<unsigned char>((pixel & 31) * 255 / 31)};
        std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    std::filesystem::create_directories(argv[1]);
    std::filesystem::copy_file(PASSPORT_FIXTURE, std::string(argv[1]) + "/fixture.jpg",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::current_path(argv[1]);
    lv_init();
    auto* display = lv_display_create(240, 320);
    static std::uint8_t buffer[240 * 20 * 2];
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    bajji::ProductUI ui;
    link.has_bond = true;
    ui.create(link, wifi, wallpaper);
    const std::string output = argv[1];
    auto send = [&](bajji::ButtonEvents e) {
        lv_tick_inc(100);
        ui.refresh(bajji::BoardHal::instance().snapshot(), link, wifi, wallpaper, e);
        lv_obj_update_layout(lv_screen_active()); bounds(lv_screen_active());
    };
    capture(output + "/home.ppm");
    send({.a_pressed = true}); assert(wallpaper.settings.display_mode == bajji::DisplayMode::fit_blur);
    send({.b_pressed = true}); assert(refresh_count == 1);
    send({.ok_pressed = true}); capture(output + "/settings.ppm");
    // Every settings row and nested chooser is reachable without touch.
    send({.ok_pressed = true}); capture(output + "/categories.ppm");
    for (unsigned i = 0; i < 10; ++i) send({.b_pressed = true});
    send({.b_pressed = true}); send({.ok_pressed = true}); capture(output + "/types.ppm");
    send({.ok_pressed = true}); assert(std::strcmp(wallpaper.settings.category, "acg") == 0);
    send({.ok_pressed = true}); send({.b_pressed = true}); send({.ok_pressed = true});
    capture(output + "/brightness.ppm"); send({.back_pressed = true});
    send({.b_pressed = true}); send({.b_pressed = true}); send({.ok_pressed = true});
    capture(output + "/interval.ppm"); send({.back_pressed = true});
    for (unsigned i = 0; i < 3; ++i) send({.b_pressed = true});
    send({.ok_pressed = true}); send({.ok_pressed = true}); capture(output + "/wifi.ppm");
    assert(wifi.portal_state == WIFI_PORTAL_READY);
    send({.ok_pressed = true}); assert(wifi.portal_state == WIFI_PORTAL_OFF);
    send({.back_pressed = true});
    for (unsigned i = 0; i < 4; ++i) send({.b_pressed = true});
    send({.ok_pressed = true}); send({.ok_pressed = true}); capture(output + "/unpair.ppm");
    send({.ok_pressed = true}); assert(unpair_count == 0);
    send({.ok_pressed = true}); send({.b_pressed = true}); send({.ok_pressed = true}); assert(unpair_count == 1);
    link.connected = true; link.passkey = 123456; send({}); capture(output + "/pairing.ppm");
    // Exercise actual JPEG decode and both display modes using a supplied bounded fixture.
    link.encrypted = true;
    wallpaper.has_cache = true; wallpaper.revision = 1;
    std::snprintf(wallpaper.lvgl_path, sizeof(wallpaper.lvgl_path), "S:fixture.jpg");
    send({}); capture(output + "/image-fit.ppm");
    assert(lv_obj_check_type(lv_obj_get_child(lv_obj_get_child(lv_screen_active(), 0), 0), &lv_image_class));
    const auto fit_top_pixel = frame[10 * 240 + 100];
    send({.a_pressed = true}); send({}); capture(output + "/image-cover.ppm");
    assert(frame[10 * 240 + 100] != fit_top_pixel);
    wallpaper.busy = true; send({}); capture(output + "/loading.ppm");
    send({.back_pressed = true}); assert(!wallpaper.busy);
    std::puts("Passport UI: bounds, navigation, actions and captures passed");
}
