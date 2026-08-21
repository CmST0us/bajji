// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

#include "ble_link.h"
#include "board_hal.hpp"
#include "wallpaper_service.hpp"

struct _lv_obj_t;
struct _lv_event_t;

namespace bajji {

class ProductUI {
public:
    void create(const ble_link_status_t& link, const WallpaperStatus& wallpaper);
    void refresh(const BoardStatus& board, const ble_link_status_t& link,
                 const WallpaperStatus& wallpaper, const ButtonEvents& buttons);

private:
    enum class Page : std::uint8_t {
        startup,
        unpaired,
        pairing_code,
        pairing_success,
        settings,
        category,
        type,
        loading,
        image,
        error,
    };

    void show(Page page, const WallpaperStatus& wallpaper, std::uint32_t passkey = 0);
    void show_image(const WallpaperStatus& wallpaper);
    void show_controls();
    void hide_hold();
    void update_settings_labels();
    void select_category(std::uint32_t index);
    void select_type(std::uint32_t index);
    void save_settings();
    void cancel_loading();
    void toggle_mode(const WallpaperStatus& wallpaper);
    void refresh_image(const WallpaperStatus& wallpaper);

    static void root_clicked(_lv_event_t* event);
    static void category_row_clicked(_lv_event_t* event);
    static void type_row_clicked(_lv_event_t* event);
    static void save_clicked(_lv_event_t* event);
    static void cancel_clicked(_lv_event_t* event);
    static void category_choice_clicked(_lv_event_t* event);
    static void type_choice_clicked(_lv_event_t* event);

    Page page_{Page::startup};
    _lv_obj_t* root_{};
    _lv_obj_t* controls_{};
    _lv_obj_t* refresh_overlay_{};
    _lv_obj_t* cache_error_{};
    _lv_obj_t* cache_error_text_{};
    _lv_obj_t* hold_overlay_{};
    _lv_obj_t* hold_arc_{};
    _lv_obj_t* hold_ms_{};
    _lv_obj_t* pairing_code_{};
    _lv_obj_t* loading_state_{};
    _lv_obj_t* category_value_{};
    _lv_obj_t* type_value_{};
    _lv_obj_t* type_row_{};
    void* webp_player_{};
    void* still_image_{};  // lv_draw_buf_t* holding the pre-decoded still wallpaper
    WallpaperSettings draft_{};
    DisplayMode display_mode_{DisplayMode::cover};
    WallpaperStatus latest_wallpaper_{};
    std::uint32_t page_since_ms_{};
    std::uint32_t controls_deadline_ms_{};
    std::uint32_t controls_hide_started_ms_{};
    std::uint32_t cache_error_deadline_ms_{};
    std::uint32_t wallpaper_revision_{};
    std::uint32_t request_revision_{};
    bool controls_visible_{};
    bool controls_hiding_{};
};

}  // namespace bajji
