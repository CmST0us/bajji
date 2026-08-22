// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bridge_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BAJJI_BRIDGE_PSM 0x0081U
#define BAJJI_BRIDGE_MTU 1536U

typedef struct {
    bool initialized;
    bool advertising;
    bool connected;
    bool encrypted;
    bool bonded;
    bool has_bond;
    bool coc_connected;
    bool bridge_ready;
    bool data_role;
    uint32_t passkey;
    uint16_t connection_interval_units;
    uint8_t tx_phy;
    uint8_t rx_phy;
    uint16_t peer_coc_mtu;
    uint16_t peer_mps;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t queue_overflows;
    uint32_t tx_errors;
    uint32_t protocol_errors;
    uint32_t pings_sent;
    uint32_t pongs_received;
    uint32_t last_ping_rtt_ms;
    int32_t last_disconnect_reason;
} ble_link_status_t;

typedef void (*ble_link_frame_handler_t)(const bridge_frame_t* frame, void* context);
typedef void (*ble_link_ready_handler_t)(bool ready, bool data_role, void* context);
typedef esp_err_t (*ble_link_provision_handler_t)(const uint8_t* payload, size_t length,
                                                  void* context);

esp_err_t ble_link_start(void);
ble_link_status_t ble_link_snapshot(void);
esp_err_t ble_link_send(const bridge_frame_t* frame);
esp_err_t ble_link_ping(void);
esp_err_t ble_link_clear_bond(void);
esp_err_t ble_link_set_handlers(ble_link_frame_handler_t frame_handler,
                                ble_link_ready_handler_t ready_handler,
                                void* context);
esp_err_t ble_link_set_provision_handler(ble_link_provision_handler_t handler, void* context);

#ifdef __cplusplus
}
#endif
