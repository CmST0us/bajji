// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "ble_link.h"
#include "board_hal.hpp"
#include "wallpaper_service.hpp"
#include "wifi_link.h"
struct _lv_obj_t;
namespace bajji {
class ProductUI {
public:
    void create(const ble_link_status_t&, const wifi_link_status_t&, const WallpaperStatus&);
    void refresh(const BoardStatus&, const ble_link_status_t&, const wifi_link_status_t&,
                 const WallpaperStatus&, const ButtonEvents&);
    bool image_visible() const;
private:
    enum class Page { home, settings, category, type, brightness, interval, wifi, pairing, unpair };
    void draw();
    void select();
    void back();
    unsigned item_count() const;
    void enter(Page);
    Page page_{Page::home};
    unsigned selected_{};
    _lv_obj_t* root_{};
    _lv_obj_t* status_label_{};
    void* image_{};
    std::uint32_t image_revision_{};
    std::uint32_t last_draw_{};
    ble_link_status_t link_{};
    wifi_link_status_t wifi_{};
    WallpaperStatus wallpaper_{};
    BoardStatus board_{};
    char category_[24]{};
    std::uint16_t interval_{5};
    esp_err_t action_error_{ESP_OK};
};
}
