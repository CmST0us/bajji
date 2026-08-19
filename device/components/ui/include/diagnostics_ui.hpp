// SPDX-License-Identifier: MIT
#pragma once

#include "ble_link.h"
#include "board_hal.hpp"

struct _lv_obj_t;

namespace bajji {

class DiagnosticsUI {
public:
    void create();
    void refresh(const BoardStatus& status, const ble_link_status_t& link);

private:
    _lv_obj_t* power_ = nullptr;
    _lv_obj_t* display_touch_ = nullptr;
    _lv_obj_t* audio_motor_ = nullptr;
    _lv_obj_t* imu_rtc_ = nullptr;
    _lv_obj_t* ble_ = nullptr;
    _lv_obj_t* bridge_ = nullptr;
};

}  // namespace bajji
