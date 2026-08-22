// SPDX-License-Identifier: MIT
#include "device_control.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ble_link.h"
#include "board_hal.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "wallpaper_service.hpp"
#include "wifi_link.h"

namespace bajji {
namespace {

constexpr char kTag[] = "device_control";
constexpr std::uint8_t kSettingsVersion = 1;
constexpr std::uint16_t kMaximumAutoRefreshMinutes = 1440;

enum class Status : std::uint8_t {
    success = 0,
    invalid_state = 1,
    invalid_argument = 2,
    storage = 3,
    checksum = 4,
    unsupported = 5,
    busy = 6,
};

enum class MessageKind : std::uint8_t { frame, disconnect };

struct Message {
    MessageKind kind{MessageKind::frame};
    bridge_frame_t frame{};
};

QueueHandle_t queue;
std::atomic_bool ready{};
bool started;

std::uint32_t read_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) << 24U |
           static_cast<std::uint32_t>(bytes[1]) << 16U |
           static_cast<std::uint32_t>(bytes[2]) << 8U |
           bytes[3];
}

std::uint16_t read_u16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) << 8U | bytes[1]);
}

void write_u32(std::uint8_t* bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value >> 24U);
    bytes[1] = static_cast<std::uint8_t>(value >> 16U);
    bytes[2] = static_cast<std::uint8_t>(value >> 8U);
    bytes[3] = static_cast<std::uint8_t>(value);
}

Status status_for(esp_err_t error) {
    switch (error) {
        case ESP_OK: return Status::success;
        case ESP_ERR_INVALID_STATE: return Status::invalid_state;
        case ESP_ERR_INVALID_ARG:
        case ESP_ERR_INVALID_SIZE: return Status::invalid_argument;
        case ESP_ERR_INVALID_CRC: return Status::checksum;
        case ESP_ERR_NOT_SUPPORTED:
        case ESP_ERR_INVALID_RESPONSE: return Status::unsupported;
        case ESP_ERR_TIMEOUT: return Status::busy;
        default: return Status::storage;
    }
}

void send_settings(std::uint16_t sequence, Status result) {
    const WallpaperStatus wallpaper = wallpaper_snapshot();
    const std::uint16_t minutes = wallpaper.settings.auto_refresh_minutes;
    bridge_frame_t response{};
    response.type = BRIDGE_TYPE_SETTINGS_STATE;
    response.sequence = sequence;
    response.payload_len = 6;
    response.payload[0] = static_cast<std::uint8_t>(result);
    response.payload[1] = kSettingsVersion;
    response.payload[2] = BoardHal::instance().brightness();
    response.payload[3] = static_cast<std::uint8_t>(wallpaper.settings.display_mode);
    response.payload[4] = static_cast<std::uint8_t>(minutes >> 8U);
    response.payload[5] = static_cast<std::uint8_t>(minutes);
    ble_link_send(&response);
}

void send_wallpaper(std::uint16_t sequence, std::uint8_t request_type, Status result,
                    std::uint32_t next_offset) {
    bridge_frame_t response{};
    response.type = BRIDGE_TYPE_WALLPAPER_RESULT;
    response.sequence = sequence;
    response.payload_len = 6;
    response.payload[0] = request_type;
    response.payload[1] = static_cast<std::uint8_t>(result);
    write_u32(response.payload + 2, next_offset);
    ble_link_send(&response);
}

void send_network(std::uint16_t sequence, Status result) {
    const wifi_link_status_t wifi = wifi_link_snapshot();
    wifi_network_link_state_t link_state = wifi.network_state;
    if (wifi.mode == WIFI_NETWORK_VPN) {
        const ble_link_status_t link = ble_link_snapshot();
        link_state = link.bridge_ready && link.data_role ? WIFI_NETWORK_LINK_CONNECTED
                                                        : WIFI_NETWORK_LINK_CONNECTING;
    }
    const std::size_t ssid_length = strnlen(wifi.network_ssid, 32);
    bridge_frame_t response{};
    response.type = BRIDGE_TYPE_NETWORK_STATE;
    response.sequence = sequence;
    response.payload_len = static_cast<std::uint16_t>(10U + ssid_length);
    response.payload[0] = static_cast<std::uint8_t>(result);
    response.payload[1] = WIFI_PROVISION_VERSION;
    response.payload[2] = static_cast<std::uint8_t>(wifi.mode);
    response.payload[3] = static_cast<std::uint8_t>(link_state);
    response.payload[4] = static_cast<std::uint8_t>(wifi.rssi);
    write_u32(response.payload + 5, static_cast<std::uint32_t>(wifi.last_error));
    response.payload[9] = static_cast<std::uint8_t>(ssid_length);
    memcpy(response.payload + 10, wifi.network_ssid, ssid_length);
    ble_link_send(&response);
}

void handle_settings(const bridge_frame_t& frame) {
    if (frame.type == BRIDGE_TYPE_SETTINGS_GET) {
        send_settings(frame.sequence, Status::success);
        return;
    }
    const std::uint8_t brightness = frame.payload[1];
    const std::uint8_t mode = frame.payload[2];
    const std::uint16_t minutes = read_u16(frame.payload + 3);
    if (frame.payload[0] != kSettingsVersion || brightness < 10 || brightness > 100 ||
        mode > static_cast<std::uint8_t>(DisplayMode::fit_blur) ||
        minutes > kMaximumAutoRefreshMinutes) {
        send_settings(frame.sequence, Status::invalid_argument);
        return;
    }
    esp_err_t result = wallpaper_apply_parameters(static_cast<DisplayMode>(mode), minutes);
    if (result == ESP_OK) result = BoardHal::instance().set_brightness(brightness);
    send_settings(frame.sequence, status_for(result));
}

void handle_network(const bridge_frame_t& frame) {
    if (frame.type == BRIDGE_TYPE_NETWORK_GET) {
        send_network(frame.sequence, Status::success);
        return;
    }
    wifi_network_mode_t mode;
    wifi_provision_credentials_t credentials{};
    if (!wifi_network_set_decode(frame.payload, frame.payload_len, &mode, &credentials)) {
        send_network(frame.sequence, Status::invalid_argument);
        return;
    }
    const esp_err_t result = wifi_link_set_network(
        mode, mode == WIFI_NETWORK_MANUAL ? &credentials : nullptr);
    memset(&credentials, 0, sizeof(credentials));
    send_network(frame.sequence, status_for(result));
}

void handle_wallpaper(const bridge_frame_t& frame) {
    esp_err_t result = ESP_ERR_INVALID_ARG;
    std::uint32_t next_offset = wallpaper_transfer_received_bytes();
    switch (frame.type) {
        case BRIDGE_TYPE_WALLPAPER_BEGIN:
            if (frame.payload[0] == 1) {
                result = wallpaper_transfer_begin(
                    static_cast<wallpaper_media_format_t>(frame.payload[1]),
                    read_u32(frame.payload + 2), read_u32(frame.payload + 6));
            }
            next_offset = wallpaper_transfer_received_bytes();
            break;
        case BRIDGE_TYPE_WALLPAPER_CHUNK:
            result = wallpaper_transfer_write(read_u32(frame.payload), frame.payload + 4,
                                              frame.payload_len - 4U);
            next_offset = wallpaper_transfer_received_bytes();
            break;
        case BRIDGE_TYPE_WALLPAPER_COMMIT:
            next_offset = wallpaper_transfer_received_bytes();
            result = wallpaper_transfer_commit();
            break;
        case BRIDGE_TYPE_WALLPAPER_CANCEL:
            result = wallpaper_transfer_cancel();
            next_offset = 0;
            break;
        default:
            break;
    }
    send_wallpaper(frame.sequence, frame.type, status_for(result), next_offset);
}

void worker(void*) {
    while (true) {
        Message message;
        if (xQueueReceive(queue, &message, portMAX_DELAY) != pdTRUE) continue;
        if (message.kind == MessageKind::disconnect) {
            wallpaper_transfer_cancel();
        } else if (message.frame.type == BRIDGE_TYPE_SETTINGS_GET ||
                   message.frame.type == BRIDGE_TYPE_SETTINGS_SET) {
            handle_settings(message.frame);
        } else if (message.frame.type == BRIDGE_TYPE_NETWORK_GET ||
                   message.frame.type == BRIDGE_TYPE_NETWORK_SET) {
            handle_network(message.frame);
        } else {
            handle_wallpaper(message.frame);
        }
    }
}

void send_busy(const bridge_frame_t& request) {
    static bridge_frame_t response;
    response.sequence = request.sequence;
    if (request.type == BRIDGE_TYPE_SETTINGS_GET || request.type == BRIDGE_TYPE_SETTINGS_SET) {
        response.type = BRIDGE_TYPE_SETTINGS_STATE;
        response.payload_len = 6;
        response.payload[0] = static_cast<std::uint8_t>(Status::busy);
        response.payload[1] = kSettingsVersion;
        response.payload[2] = response.payload[3] = response.payload[4] = response.payload[5] = 0;
    } else if (request.type == BRIDGE_TYPE_NETWORK_GET ||
               request.type == BRIDGE_TYPE_NETWORK_SET) {
        send_network(request.sequence, Status::busy);
        return;
    } else {
        response.type = BRIDGE_TYPE_WALLPAPER_RESULT;
        response.payload_len = 6;
        response.payload[0] = request.type;
        response.payload[1] = static_cast<std::uint8_t>(Status::busy);
        const std::uint32_t retry_offset = request.type == BRIDGE_TYPE_WALLPAPER_CHUNK
                                               ? read_u32(request.payload)
                                               : 0;
        write_u32(response.payload + 2, retry_offset);
    }
    ble_link_send(&response);
}

}  // namespace

esp_err_t device_control_start() {
    if (started) return ESP_OK;
    queue = xQueueCreate(3, sizeof(Message));
    if (!queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(worker, "device_control", 8192, nullptr, 3, nullptr) != pdPASS) {
        vQueueDelete(queue);
        queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    started = true;
    return ESP_OK;
}

void device_control_receive(const bridge_frame_t* frame) {
    if (!frame || !queue || !ready.load()) return;
    const Message message{.kind = MessageKind::frame, .frame = *frame};
    if (xQueueSend(queue, &message, 0) != pdTRUE) send_busy(*frame);
}

void device_control_set_ready(bool is_ready) {
    ready.store(is_ready);
    if (is_ready || !queue) return;
    xQueueReset(queue);
    const Message message{.kind = MessageKind::disconnect};
    xQueueSendToFront(queue, &message, 0);
}

}  // namespace bajji
