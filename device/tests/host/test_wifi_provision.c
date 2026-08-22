// SPDX-License-Identifier: MIT
#include "wifi_provision.h"

#include <assert.h>
#include <string.h>

int main(void) {
    const uint8_t valid[] = {
        1, WIFI_PROVISION_WPA2, 5, 8,
        'B', 'a', 'j', 'j', 'i',
        'p', 'a', 's', 's', 'w', 'o', 'r', 'd',
    };
    wifi_provision_credentials_t credentials;
    assert(wifi_provision_decode(valid, sizeof(valid), &credentials));
    assert(credentials.security == WIFI_PROVISION_WPA2);
    assert(credentials.ssid_length == 5);
    assert(credentials.password_length == 8);
    assert(memcmp(credentials.ssid, "Bajji", 5) == 0);

    uint8_t wrong_length[sizeof(valid)];
    memcpy(wrong_length, valid, sizeof(valid));
    wrong_length[2] = 6;
    assert(!wifi_provision_decode(wrong_length, sizeof(wrong_length), &credentials));

    uint8_t embedded_zero[sizeof(valid)];
    memcpy(embedded_zero, valid, sizeof(valid));
    embedded_zero[5] = 0;
    assert(!wifi_provision_decode(embedded_zero, sizeof(embedded_zero), &credentials));

    const uint8_t invalid_open[] = {1, WIFI_PROVISION_OPEN, 1, 1, 'x', 'p'};
    assert(!wifi_provision_decode(invalid_open, sizeof(invalid_open), &credentials));

    const uint8_t manual[] = {
        1, WIFI_NETWORK_MANUAL, WIFI_PROVISION_WPA2, 5, 8,
        'B', 'a', 'j', 'j', 'i',
        'p', 'a', 's', 's', 'w', 'o', 'r', 'd',
    };
    wifi_network_mode_t mode = WIFI_NETWORK_UNSET;
    assert(wifi_network_set_decode(manual, sizeof(manual), &mode, &credentials));
    assert(mode == WIFI_NETWORK_MANUAL);
    assert(credentials.ssid_length == 5 && credentials.password_length == 8);
    const uint8_t shared[] = {1, WIFI_NETWORK_SHARED};
    assert(wifi_network_set_decode(shared, sizeof(shared), &mode, &credentials));
    assert(mode == WIFI_NETWORK_SHARED);
    const uint8_t vpn[] = {1, WIFI_NETWORK_VPN};
    assert(wifi_network_set_decode(vpn, sizeof(vpn), &mode, &credentials));
    assert(mode == WIFI_NETWORK_VPN);
    const uint8_t short_password[] = {
        1, WIFI_NETWORK_MANUAL, WIFI_PROVISION_WPA2, 1, 7,
        'x', '1', '2', '3', '4', '5', '6', '7',
    };
    assert(!wifi_network_set_decode(short_password, sizeof(short_password), &mode, &credentials));
    const uint8_t wep[] = {1, WIFI_NETWORK_MANUAL, WIFI_PROVISION_WEP, 1, 5,
                           'x', '1', '2', '3', '4', '5'};
    assert(!wifi_network_set_decode(wep, sizeof(wep), &mode, &credentials));

    assert(wifi_network_mode_uses_wifi(WIFI_NETWORK_MANUAL));
    assert(wifi_network_mode_uses_wifi(WIFI_NETWORK_SHARED));
    assert(!wifi_network_mode_uses_wifi(WIFI_NETWORK_VPN));
    assert(wifi_network_mode_uses_phone(WIFI_NETWORK_VPN));
    assert(!wifi_network_mode_uses_phone(WIFI_NETWORK_MANUAL));
    assert(wifi_network_shared_write_allowed(WIFI_NETWORK_SHARED, 1));
    assert(!wifi_network_shared_write_allowed(WIFI_NETWORK_SHARED, 0));
    assert(!wifi_network_shared_write_allowed(WIFI_NETWORK_VPN, 1));
    assert(wifi_network_shared_write_allowed(WIFI_NETWORK_UNSET, 0));
    assert(wifi_network_mode_after_credentials(WIFI_CREDENTIAL_SOURCE_PORTAL) ==
           WIFI_NETWORK_MANUAL);
    assert(wifi_network_mode_after_credentials(WIFI_CREDENTIAL_SOURCE_SHARED) ==
           WIFI_NETWORK_SHARED);
    return 0;
}
