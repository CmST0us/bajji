// SPDX-License-Identifier: MIT
#include "wifi_provision.h"

#include <string.h>

static int contains_zero(const uint8_t* bytes, size_t length) {
    return memchr(bytes, 0, length) != NULL;
}

int wifi_provision_credentials_valid(wifi_provision_security_t security,
                                     size_t password_length) {
    switch (security) {
        case WIFI_PROVISION_OPEN:
        case WIFI_PROVISION_OWE:
            return password_length == 0;
        case WIFI_PROVISION_WEP:
            return password_length == 5 || password_length == 10 ||
                   password_length == 13 || password_length == 26;
        case WIFI_PROVISION_WPA:
        case WIFI_PROVISION_WPA2:
        case WIFI_PROVISION_WPA3:
            return password_length >= 8 && password_length <= WIFI_PROVISION_MAX_PASSWORD;
        default:
            return 0;
    }
}

int wifi_provision_decode(const uint8_t* payload, size_t length,
                          wifi_provision_credentials_t* credentials) {
    if (!payload || !credentials || length < 4 || payload[0] != WIFI_PROVISION_VERSION) return 0;
    const size_t ssid_length = payload[2];
    const size_t password_length = payload[3];
    const wifi_provision_security_t security = (wifi_provision_security_t)payload[1];
    if (ssid_length == 0 || ssid_length > WIFI_PROVISION_MAX_SSID ||
        password_length > WIFI_PROVISION_MAX_PASSWORD ||
        length != 4 + ssid_length + password_length ||
        contains_zero(payload + 4, ssid_length) ||
        contains_zero(payload + 4 + ssid_length, password_length) ||
        !wifi_provision_credentials_valid(security, password_length)) {
        return 0;
    }
    memset(credentials, 0, sizeof(*credentials));
    credentials->security = security;
    credentials->ssid_length = (uint8_t)ssid_length;
    credentials->password_length = (uint8_t)password_length;
    memcpy(credentials->ssid, payload + 4, ssid_length);
    memcpy(credentials->password, payload + 4 + ssid_length, password_length);
    return 1;
}

int wifi_network_set_decode(const uint8_t* payload, size_t length,
                            wifi_network_mode_t* mode,
                            wifi_provision_credentials_t* credentials) {
    if (!payload || !mode || !credentials || length < 2 ||
        payload[0] != WIFI_PROVISION_VERSION || payload[1] > WIFI_NETWORK_VPN) {
        return 0;
    }
    *mode = (wifi_network_mode_t)payload[1];
    memset(credentials, 0, sizeof(*credentials));
    if (*mode != WIFI_NETWORK_MANUAL) return length == 2;
    if (length < 5) return 0;

    const wifi_provision_security_t security = (wifi_provision_security_t)payload[2];
    const size_t ssid_length = payload[3];
    const size_t password_length = payload[4];
    if ((security != WIFI_PROVISION_OPEN && security != WIFI_PROVISION_WPA2 &&
         security != WIFI_PROVISION_WPA3) ||
        ssid_length == 0 || ssid_length > WIFI_PROVISION_MAX_SSID ||
        password_length > WIFI_PROVISION_MAX_PASSWORD ||
        length != 5 + ssid_length + password_length ||
        contains_zero(payload + 5, ssid_length) ||
        contains_zero(payload + 5 + ssid_length, password_length) ||
        !wifi_provision_credentials_valid(security, password_length)) {
        return 0;
    }
    credentials->security = security;
    credentials->ssid_length = (uint8_t)ssid_length;
    credentials->password_length = (uint8_t)password_length;
    memcpy(credentials->ssid, payload + 5, ssid_length);
    memcpy(credentials->password, payload + 5 + ssid_length, password_length);
    return 1;
}

int wifi_network_mode_uses_wifi(wifi_network_mode_t mode) {
    return mode == WIFI_NETWORK_MANUAL || mode == WIFI_NETWORK_SHARED;
}

int wifi_network_mode_uses_phone(wifi_network_mode_t mode) {
    return mode == WIFI_NETWORK_VPN;
}

int wifi_network_shared_write_allowed(wifi_network_mode_t mode, int awaiting_credentials) {
    return (mode == WIFI_NETWORK_SHARED && awaiting_credentials) ||
           mode == WIFI_NETWORK_UNSET;
}

wifi_network_mode_t wifi_network_mode_after_credentials(wifi_credential_source_t source) {
    return source == WIFI_CREDENTIAL_SOURCE_PORTAL ? WIFI_NETWORK_MANUAL
                                                   : WIFI_NETWORK_SHARED;
}
