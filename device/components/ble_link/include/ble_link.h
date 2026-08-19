// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bridge_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BAJJI_BRIDGE_PSM 0x0081U
#define BAJJI_BRIDGE_MTU 1536U

typedef struct {
    bool advertising;
    bool connected;
    bool encrypted;
    bool bonded;
    bool coc_connected;
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
    uint32_t dropped_frames;
    uint32_t protocol_errors;
    int32_t last_disconnect_reason;
} ble_link_status_t;

esp_err_t ble_link_start(void);
ble_link_status_t ble_link_snapshot(void);
esp_err_t ble_link_send(const bridge_frame_t* frame);
esp_err_t ble_link_clear_bond(void);

#ifdef __cplusplus
}
#endif
