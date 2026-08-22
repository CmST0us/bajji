// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_PROVISION_VERSION 1U
#define WIFI_PROVISION_MAX_SSID 32U
#define WIFI_PROVISION_MAX_PASSWORD 64U

typedef enum {
    WIFI_PROVISION_OPEN = 0,
    WIFI_PROVISION_WEP = 1,
    WIFI_PROVISION_WPA = 2,
    WIFI_PROVISION_OWE = 3,
    WIFI_PROVISION_WPA2 = 4,
    WIFI_PROVISION_WPA3 = 5,
} wifi_provision_security_t;

typedef struct {
    wifi_provision_security_t security;
    uint8_t ssid[WIFI_PROVISION_MAX_SSID];
    uint8_t password[WIFI_PROVISION_MAX_PASSWORD];
    uint8_t ssid_length;
    uint8_t password_length;
} wifi_provision_credentials_t;

int wifi_provision_decode(const uint8_t* payload, size_t length,
                          wifi_provision_credentials_t* credentials);

#ifdef __cplusplus
}
#endif
