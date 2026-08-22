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
    return 0;
}
