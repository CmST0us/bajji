// SPDX-License-Identifier: MIT
#include "diagnostics_ui.hpp"

#include <cstdio>
#include <inttypes.h>

#include "lvgl.h"

namespace bajji {
namespace {

constexpr int kDisplayDiameter = 466;
constexpr int kRoundSafeSide = 328;
static_assert(kRoundSafeSide * kRoundSafeSide * 2 <= kDisplayDiameter * kDisplayDiameter);

const char* phy_text(std::uint8_t phy) {
    switch (phy) {
        case 1: return "1M";
        case 2: return "2M";
        case 3: return "CODED";
        default: return "--";
    }
}

void format_bytes(std::uint64_t bytes, char* output, std::size_t size) {
    if (bytes >= 1024ULL * 1024ULL) {
        std::snprintf(output, size, "%.2f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        std::snprintf(output, size, "%.1f KiB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(output, size, "%llu B", static_cast<unsigned long long>(bytes));
    }
}

lv_obj_t* add_label(lv_obj_t* parent, const char* text) {
    auto* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t* add_button(lv_obj_t* parent, const char* text, lv_event_cb_t callback) {
    auto* button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(48));
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

void clear_bond(lv_event_t*) { ble_link_clear_bond(); }

}  // namespace

void DiagnosticsUI::create() {
    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080b12), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xeaf2ff), 0);

    auto* root = lv_obj_create(screen);
    lv_obj_set_size(root, kRoundSafeSide, kRoundSafeSide);
    lv_obj_center(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x080b12), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 10, 0);
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);

    auto* title = lv_label_create(root);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_text(title, "BAJJI / STOPWATCH");
    lv_obj_set_style_text_color(title, lv_color_hex(0x54d7ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

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
    lv_obj_set_style_pad_row(actions, 8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
    add_button(actions, "Tone + Motor", test_feedback);
    add_button(actions, "Brightness", cycle_brightness);
    add_button(actions, "Clear bond", clear_bond);
    add_button(actions, "Power off", confirm_shutdown);

    auto* hint = add_label(root, "Key A: feedback test    Key B: brightness");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8290a8), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

void DiagnosticsUI::refresh(const BoardStatus& status, const ble_link_status_t& link) {
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
    if (link.passkey) {
        lv_label_set_text_fmt(ble_, "BLE PAIRING\nEnter %06" PRIu32 " on iPhone", link.passkey);
    } else if (link.connected) {
        lv_label_set_text_fmt(ble_, "BLE  %s | %s\nPHY %s/%s | %.2f ms",
                              link.encrypted ? "encrypted" : "connected",
                              link.bonded ? "bonded" : "not bonded", phy_text(link.tx_phy),
                              phy_text(link.rx_phy),
                              static_cast<double>(link.connection_interval_units) * 1.25);
    } else {
        lv_label_set_text(ble_, link.advertising ? "BLE  advertising" : "BLE  starting");
    }
    char rx[24];
    char tx[24];
    format_bytes(link.rx_bytes, rx, sizeof(rx));
    format_bytes(link.tx_bytes, tx, sizeof(tx));
    lv_label_set_text_fmt(
        bridge_,
        "BRIDGE  %s\nCoC MTU %u | MPS %u\nRX %s | %" PRIu32 " frames\nTX %s | %" PRIu32
        " frames\nQueue %" PRIu32 " | TX err %" PRIu32 " | Proto %" PRIu32,
        link.coc_connected ? "PHASE 0 ECHO" : "OFFLINE", link.peer_coc_mtu, link.peer_mps,
        rx, link.rx_frames, tx, link.tx_frames, link.queue_overflows, link.tx_errors,
        link.protocol_errors);
}

}  // namespace bajji
