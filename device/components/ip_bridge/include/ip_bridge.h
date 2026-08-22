// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bridge_protocol.h"
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool link_up;
    bool time_valid;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t dropped_packets;
    uint32_t invalid_packets;
    int32_t last_error;
} ip_bridge_status_t;

esp_err_t ip_bridge_start(void);
void ip_bridge_set_link(bool up);
void ip_bridge_set_wifi_netif(esp_netif_t* netif, bool up);
void ip_bridge_receive(const bridge_frame_t* frame);
ip_bridge_status_t ip_bridge_snapshot(void);

#ifdef __cplusplus
}
#endif
