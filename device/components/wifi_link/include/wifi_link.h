// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_PORTAL_OFF = 0,
    WIFI_PORTAL_STARTING,
    WIFI_PORTAL_READY,
    WIFI_PORTAL_CONNECTING,
    WIFI_PORTAL_SUCCESS,
    WIFI_PORTAL_FAILED,
} wifi_portal_state_t;

typedef struct {
    bool initialized;
    bool configured;
    bool connected;
    int8_t rssi;
    uint32_t reconnects;
    int32_t last_error;
    wifi_portal_state_t portal_state;
    char portal_ssid[33];
    char portal_selected_ssid[33];
    uint16_t portal_seconds_remaining;
    int32_t portal_last_error;
} wifi_link_status_t;

esp_err_t wifi_link_start(void);
esp_err_t wifi_link_provision(const uint8_t* payload, size_t length);
esp_err_t wifi_link_start_portal(void);
esp_err_t wifi_link_stop_portal(void);
wifi_link_status_t wifi_link_snapshot(void);

#ifdef __cplusplus
}
#endif
