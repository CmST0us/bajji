// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

#include "ble_link.h"
#include "board_hal.hpp"
#include "ip_bridge.h"
#include "wallpaper_service.hpp"

struct _lv_obj_t;

namespace bajji {

class DiagnosticsUI {
public:
    void create();
    void toggle_tools();
    void refresh(const BoardStatus& board, const ble_link_status_t& link,
                 const ip_bridge_status_t& ip, const WallpaperStatus& wallpaper);

private:
    _lv_obj_t* home_{};
    _lv_obj_t* tools_{};
    _lv_obj_t* wallpaper_image_{};
    _lv_obj_t* home_top_{};
    _lv_obj_t* home_time_{};
    _lv_obj_t* home_date_{};
    _lv_obj_t* home_caption_{};
    _lv_obj_t* home_state_{};
    _lv_obj_t* network_{};
    _lv_obj_t* bluetooth_{};
    _lv_obj_t* wallpaper_{};
    _lv_obj_t* test_result_{};
    std::uint32_t wallpaper_revision_{};
    bool tools_visible_{};
};

}  // namespace bajji
