// SPDX-License-Identifier: MIT
#include "diagnostics_ui.hpp"

#include "lvgl.h"

namespace bajji {
namespace {

lv_obj_t* add_label(lv_obj_t* parent, const char* text) {
    auto* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t* add_button(lv_obj_t* parent, const char* text, lv_event_cb_t callback) {
    auto* button = lv_button_create(parent);
    lv_obj_set_height(button, 48);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void test_feedback(lv_event_t*) {
    auto& board = BoardHal::instance();
    board.vibrate(100, 60);
    board.play_tone(880, 80);
}

void cycle_brightness(lv_event_t*) {
    auto& board = BoardHal::instance();
    board.set_brightness(static_cast<std::uint8_t>((board.brightness() % 100) + 20));
}

void confirm_shutdown(lv_event_t*) {
    auto* message = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(message, "Power off?");
    lv_msgbox_add_text(message, "The power key turns the device back on.");
    auto* power_off = lv_msgbox_add_footer_button(message, "Power off");
    lv_obj_add_event_cb(power_off, [](lv_event_t*) { BoardHal::instance().shutdown(); }, LV_EVENT_CLICKED, nullptr);
    auto* cancel = lv_msgbox_add_footer_button(message, "Cancel");
    lv_obj_add_event_cb(cancel, [](lv_event_t* event) {
        lv_msgbox_close(static_cast<lv_obj_t*>(lv_event_get_user_data(event)));
    }, LV_EVENT_CLICKED, message);
}

}  // namespace

void DiagnosticsUI::create() {
    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080b12), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xeaf2ff), 0);

    auto* root = lv_obj_create(screen);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x080b12), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 20, 0);
    lv_obj_set_style_pad_row(root, 12, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

    auto* title = lv_label_create(root);
    lv_label_set_text(title, "BAJJI / STOPWATCH");
    lv_obj_set_style_text_color(title, lv_color_hex(0x54d7ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    power_ = add_label(root, "Power: starting");
    display_touch_ = add_label(root, "Display / Touch: starting");
    audio_motor_ = add_label(root, "Audio / Motor: starting");
    imu_rtc_ = add_label(root, "IMU / RTC: starting");
    ble_ = add_label(root, "BLE: waiting for bridge service");
    bridge_ = add_label(root, "Bridge: offline");

    auto* actions = lv_obj_create(root);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
    add_button(actions, "Tone + Motor", test_feedback);
    add_button(actions, "Brightness", cycle_brightness);
    add_button(actions, "Power off", confirm_shutdown);

    auto* hint = add_label(root, "Key A: feedback test    Key B: brightness");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8290a8), 0);
}

void DiagnosticsUI::refresh(const BoardStatus& status) {
    if (!power_) return;
    lv_label_set_text_fmt(power_, "Power: PMIC %s / IOE %s    Battery %u%% (%u mV)%s",
                          health_text(status.pmic), health_text(status.io_expander),
                          status.battery_percent, status.battery_mv, status.charging ? " charging" : "");
    lv_label_set_text_fmt(display_touch_, "Display %s / Touch %s    (%d, %d)%s    Brightness %u%%",
                          health_text(status.display), health_text(status.touch), status.touch_point.x,
                          status.touch_point.y, status.touch_point.pressed ? " pressed" : "", status.brightness);
    lv_label_set_text_fmt(audio_motor_, "Audio %s / Motor %s    Microphone %u",
                          health_text(status.audio), health_text(status.motor), status.microphone_level);
    lv_label_set_text_fmt(imu_rtc_, "IMU %s  A %.2f %.2f %.2f\nRTC %s  %s",
                          health_text(status.imu), static_cast<double>(status.imu_sample.accel_x),
                          static_cast<double>(status.imu_sample.accel_y),
                          static_cast<double>(status.imu_sample.accel_z), health_text(status.rtc),
                          status.rtc_text.data());
}

}  // namespace bajji
