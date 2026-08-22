// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
    bool configured;
    bool connected;
    int8_t rssi;
    uint32_t reconnects;
    int32_t last_error;
} wifi_link_status_t;

esp_err_t wifi_link_start(void);
esp_err_t wifi_link_provision(const uint8_t* payload, size_t length);
wifi_link_status_t wifi_link_snapshot(void);

#ifdef __cplusplus
}
#endif
