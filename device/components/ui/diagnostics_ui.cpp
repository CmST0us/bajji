// SPDX-License-Identifier: MIT
#include "diagnostics_ui.hpp"

#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "esp_err.h"
#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"

namespace bajji {
namespace {

constexpr int kDisplayDiameter = 466;
constexpr int kRoundSafeSide = 328;
static_assert(kRoundSafeSide * kRoundSafeSide * 2 <= kDisplayDiameter * kDisplayDiameter);

const char* yes_no(bool value) { return value ? "YES" : "NO"; }

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

void add_section(lv_obj_t* parent, const char* text) {
    auto* label = add_label(parent, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0x54d7ff), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
}

lv_obj_t* add_button(lv_obj_t* parent, const char* text, lv_event_cb_t callback,
                     void* user_data = nullptr) {
    auto* button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(48));
    lv_obj_set_height(button, 48);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

lv_obj_t* add_actions(lv_obj_t* parent) {
    auto* actions = lv_obj_create(parent);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 8, 0);
    lv_obj_set_style_pad_row(actions, 8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
    return actions;
}

void request_refresh(lv_event_t*) { wallpaper_request_refresh(); }
void request_dns(lv_event_t*) { wallpaper_request_dns_test(); }
void request_https(lv_event_t*) { wallpaper_request_https_test(); }

void request_ping(lv_event_t* event) {
    auto* result = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
    const esp_err_t error = ble_link_ping();
    lv_label_set_text_fmt(result, "PING: %s", esp_err_to_name(error));
}

void close_message(lv_event_t* event) {
    lv_msgbox_close(static_cast<lv_obj_t*>(lv_event_get_user_data(event)));
}

void clear_bond(lv_event_t* event) {
    ble_link_clear_bond();
    close_message(event);
}

void power_off(lv_event_t*) { BoardHal::instance().shutdown(); }

void confirm_clear_bond(lv_event_t*) {
    auto* message = lv_msgbox_create(nullptr);
    lv_obj_set_width(message, 300);
    lv_msgbox_add_title(message, "Clear pairing?");
    lv_msgbox_add_text(message, "The phone must pair again.");
    auto* clear = lv_msgbox_add_footer_button(message, "Clear");
    lv_obj_add_event_cb(clear, clear_bond, LV_EVENT_CLICKED, message);
    auto* cancel = lv_msgbox_add_footer_button(message, "Cancel");
    lv_obj_add_event_cb(cancel, close_message, LV_EVENT_CLICKED, message);
}

void confirm_shutdown(lv_event_t*) {
    auto* message = lv_msgbox_create(nullptr);
    lv_obj_set_width(message, 300);
    lv_msgbox_add_title(message, "Power off?");
    lv_msgbox_add_text(message, "The power key turns Bajji back on.");
    auto* off = lv_msgbox_add_footer_button(message, "Power off");
    lv_obj_add_event_cb(off, power_off, LV_EVENT_CLICKED, nullptr);
    auto* cancel = lv_msgbox_add_footer_button(message, "Cancel");
    lv_obj_add_event_cb(cancel, close_message, LV_EVENT_CLICKED, message);
}

void toggle_tools_event(lv_event_t* event) {
    static_cast<DiagnosticsUI*>(lv_event_get_user_data(event))->toggle_tools();
}

}  // namespace

void DiagnosticsUI::create() {
    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070c), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xf4f8ff), 0);

    home_ = lv_obj_create(screen);
    lv_obj_set_size(home_, kDisplayDiameter, kDisplayDiameter);
    lv_obj_center(home_);
    lv_obj_set_style_radius(home_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(home_, lv_color_hex(0x0b1522), 0);
    lv_obj_set_style_border_width(home_, 0, 0);
    lv_obj_set_style_pad_all(home_, 0, 0);
    lv_obj_remove_flag(home_, LV_OBJ_FLAG_SCROLLABLE);

    wallpaper_image_ = lv_image_create(home_);
    lv_obj_set_size(wallpaper_image_, kDisplayDiameter, kDisplayDiameter);
    lv_obj_center(wallpaper_image_);
    lv_image_set_inner_align(wallpaper_image_, LV_IMAGE_ALIGN_COVER);
    lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    auto* safe = lv_obj_create(home_);
    lv_obj_set_size(safe, kRoundSafeSide, kRoundSafeSide);
    lv_obj_center(safe);
    lv_obj_set_style_bg_color(safe, lv_color_hex(0x05070c), 0);
    lv_obj_set_style_bg_opa(safe, LV_OPA_40, 0);
    lv_obj_set_style_border_width(safe, 0, 0);
    lv_obj_set_style_radius(safe, 36, 0);
    lv_obj_set_style_pad_all(safe, 0, 0);
    lv_obj_remove_flag(safe, LV_OBJ_FLAG_SCROLLABLE);

    home_top_ = add_label(safe, "BLE -- | NET -- | --%");
    lv_obj_set_pos(home_top_, 0, 18);
    lv_obj_set_style_text_align(home_top_, LV_TEXT_ALIGN_CENTER, 0);

    home_time_ = add_label(safe, "--:--:--");
    lv_obj_set_pos(home_time_, 0, 70);
    lv_obj_set_style_text_align(home_time_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(home_time_, &lv_font_montserrat_48, 0);

    home_date_ = add_label(safe, "---- -- --");
    lv_obj_set_pos(home_date_, 0, 130);
    lv_obj_set_style_text_align(home_date_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(home_date_, lv_color_hex(0xc8d4e6), 0);

    home_caption_ = add_label(safe, "Bing Daily Wallpaper");
    lv_obj_set_height(home_caption_, 72);
    lv_obj_set_pos(home_caption_, 0, 196);
    lv_obj_set_style_text_align(home_caption_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(home_caption_, &lv_font_source_han_sans_sc_16_cjk, 0);

    home_state_ = add_label(safe, "Waiting for phone bridge");
    lv_obj_set_height(home_state_, 36);
    lv_obj_set_pos(home_state_, 0, 270);
    lv_obj_set_style_text_align(home_state_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(home_state_, lv_color_hex(0x7ee6ff), 0);
    lv_obj_add_flag(home_state_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_state_, request_refresh, LV_EVENT_CLICKED, nullptr);

    auto* hint = add_label(safe, "Tap status to refresh | Hold B for tools");
    lv_obj_set_pos(hint, 0, 307);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9aa8bc), 0);

    tools_ = lv_obj_create(screen);
    lv_obj_set_size(tools_, kRoundSafeSide, kRoundSafeSide);
    lv_obj_center(tools_);
    lv_obj_set_style_bg_color(tools_, lv_color_hex(0x080b12), 0);
    lv_obj_set_style_border_width(tools_, 0, 0);
    lv_obj_set_style_pad_all(tools_, 10, 0);
    lv_obj_set_style_pad_row(tools_, 8, 0);
    lv_obj_set_flex_flow(tools_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(tools_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tools_, LV_SCROLLBAR_MODE_AUTO);

    auto* title = add_label(tools_, "INTERNAL TOOLS");
    lv_obj_set_style_text_color(title, lv_color_hex(0x54d7ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    auto* close = lv_button_create(tools_);
    lv_obj_set_width(close, LV_PCT(100));
    lv_obj_set_height(close, 44);
    lv_obj_add_event_cb(close, toggle_tools_event, LV_EVENT_CLICKED, this);
    auto* close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);

    add_section(tools_, "NETWORK");
    network_ = add_label(tools_, "Starting...");
    add_section(tools_, "BLUETOOTH");
    bluetooth_ = add_label(tools_, "Starting...");
    add_section(tools_, "TESTS");
    test_result_ = add_label(tools_, "No test run");
    auto* tests = add_actions(tools_);
    add_button(tests, "DNS", request_dns);
    add_button(tests, "HTTPS", request_https);
    add_button(tests, "Wallpaper", request_refresh);
    add_button(tests, "PING", request_ping, test_result_);

    add_section(tools_, "WALLPAPER");
    wallpaper_ = add_label(tools_, "No cache");

    add_section(tools_, "SECURITY");
    auto* security = add_actions(tools_);
    add_button(security, "Clear pairing", confirm_clear_bond);
    add_section(tools_, "POWER");
    auto* power = add_actions(tools_);
    add_button(power, "Power off", confirm_shutdown);

    auto* tools_hint = add_label(tools_, "Hold KEY B to close tools");
    lv_obj_set_style_text_color(tools_hint, lv_color_hex(0x8290a8), 0);
    lv_obj_set_style_text_align(tools_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(tools_, LV_OBJ_FLAG_HIDDEN);
}

void DiagnosticsUI::toggle_tools() {
    if (!home_ || !tools_) return;
    tools_visible_ = !tools_visible_;
    if (tools_visible_) {
        lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(tools_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_y(tools_, 0, LV_ANIM_OFF);
    } else {
        lv_obj_add_flag(tools_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(home_, LV_OBJ_FLAG_HIDDEN);
    }
}

void DiagnosticsUI::refresh(const BoardStatus& board, const ble_link_status_t& link,
                            const ip_bridge_status_t& ip, const WallpaperStatus& wallpaper) {
    if (!home_) return;
    lv_label_set_text_fmt(home_top_, "BLE %s | NET %s | %u%%", link.bridge_ready ? "UP" : "--",
                          wallpaper.internet_verified ? "UP" : "--", board.battery_percent);

    char date[11] = "---- -- --";
    char time[9] = "--:--:--";
    if (std::strlen(board.rtc_text.data()) >= 19) {
        std::memcpy(date, board.rtc_text.data(), 10);
        date[4] = ' ';
        date[7] = ' ';
        std::memcpy(time, board.rtc_text.data() + 11, 8);
    }
    lv_label_set_text(home_time_, time);
    lv_label_set_text(home_date_, date);
    lv_label_set_text(home_caption_, wallpaper.copyright[0] ? wallpaper.copyright
                                                            : "Bing Daily Wallpaper");
    if (link.passkey) {
        lv_label_set_text_fmt(home_state_, "Pair on iPhone: %06" PRIu32, link.passkey);
    } else {
        lv_label_set_text(home_state_, wallpaper.state);
    }

    if (wallpaper.has_cache && wallpaper.revision != wallpaper_revision_) {
        wallpaper_revision_ = wallpaper.revision;
        lv_image_cache_drop(kWallpaperLvglPath);
        lv_image_set_src(wallpaper_image_, kWallpaperLvglPath);
        lv_obj_remove_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(wallpaper_image_);
    } else if (!wallpaper.has_cache) {
        lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);
    }

    char ip_rx[24];
    char ip_tx[24];
    format_bytes(ip.rx_bytes, ip_rx, sizeof(ip_rx));
    format_bytes(ip.tx_bytes, ip_tx, sizeof(ip_tx));
    lv_label_set_text_fmt(network_,
                          "IPv4 10.77.0.2/30  GW 10.77.0.1\nDNS 1.1.1.1 | link %s | clock %s | internet %s\n"
                          "RX %s / %" PRIu32 " pkt | TX %s / %" PRIu32
                          " pkt\nDrop %" PRIu32 " | invalid %" PRIu32 " | error %ld",
                          yes_no(ip.link_up), yes_no(ip.time_valid),
                          yes_no(wallpaper.internet_verified), ip_rx, ip.rx_packets, ip_tx,
                          ip.tx_packets, ip.dropped_packets, ip.invalid_packets,
                          static_cast<long>(ip.last_error));

    char ble_rx[24];
    char ble_tx[24];
    format_bytes(link.rx_bytes, ble_rx, sizeof(ble_rx));
    format_bytes(link.tx_bytes, ble_tx, sizeof(ble_tx));
    lv_label_set_text_fmt(bluetooth_,
                          "%s | encrypted %s | bonded %s\nPHY %s/%s | interval %.2f ms\n"
                          "CoC MTU %u | MPS %u\nRX %s / %" PRIu32 " | TX %s / %" PRIu32
                          "\nQueue %" PRIu32 " | TX err %" PRIu32 " | protocol %" PRIu32,
                          link.bridge_ready ? "BRIDGE READY" : (link.connected ? "CONNECTED" : "OFFLINE"),
                          yes_no(link.encrypted), yes_no(link.bonded), phy_text(link.tx_phy),
                          phy_text(link.rx_phy),
                          static_cast<double>(link.connection_interval_units) * 1.25,
                          link.peer_coc_mtu, link.peer_mps, ble_rx, link.rx_frames, ble_tx,
                          link.tx_frames, link.queue_overflows, link.tx_errors, link.protocol_errors);

    if (link.pongs_received) {
        lv_label_set_text_fmt(test_result_, "PING: %" PRIu32 " replies, last %" PRIu32 " ms",
                              link.pongs_received, link.last_ping_rtt_ms);
    } else if (wallpaper.test_result[0]) {
        lv_label_set_text(test_result_, wallpaper.test_result);
    }
    lv_label_set_text_fmt(wallpaper_, "Cache %s | busy %s | date %s\n%s\nError %ld",
                          yes_no(wallpaper.has_cache), yes_no(wallpaper.busy),
                          wallpaper.start_date[0] ? wallpaper.start_date : "--------",
                          wallpaper.state, static_cast<long>(wallpaper.last_error));
}

}  // namespace bajji
