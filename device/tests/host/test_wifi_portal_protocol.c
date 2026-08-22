// SPDX-License-Identifier: MIT
#include "wifi_portal_protocol.h"

#include <assert.h>
#include <string.h>

int main(void) {
    const char* token = "0123456789abcdef0123456789abcdef";
    const char form_body[] =
        "password=correct%20horse%2B&network=12&token=0123456789abcdef0123456789abcdef";
    wifi_portal_form_t form;
    assert(wifi_portal_decode_form(form_body, strlen(form_body), token, &form));
    assert(form.network_index == 12);
    assert(form.password_length == 14);
    assert(memcmp(form.password, "correct horse+", 14) == 0);

    const char open_body[] =
        "token=0123456789abcdef0123456789abcdef&network=0&password=";
    assert(wifi_portal_decode_form(open_body, strlen(open_body), token, &form));
    assert(form.password_length == 0);
    assert(!wifi_portal_decode_form("network=0&password=", 19, token, &form));
    assert(!wifi_portal_decode_form(
        "token=0123456789abcdef0123456789abcdee&network=0&password=", 58,
        token, &form));
    assert(!wifi_portal_decode_form(
        "token=0123456789abcdef0123456789abcdef&network=0&password=%00", 61,
        token, &form));

    const uint8_t query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x05, 'b', 'a', 'j', 'j', 'i',
        0x05, 's', 'e', 't', 'u', 'p', 0x00, 0x00, 0x01, 0x00, 0x01,
    };
    const uint8_t ip[] = {192, 168, 4, 1};
    uint8_t reply[64];
    const size_t reply_length = wifi_portal_build_dns_reply(
        query, sizeof(query), ip, reply, sizeof(reply));
    assert(reply_length == sizeof(query) + 16);
    assert(reply[2] == 0x85 && reply[3] == 0x00);
    assert(reply[6] == 0x00 && reply[7] == 0x01);
    assert(memcmp(reply + reply_length - 4, ip, sizeof(ip)) == 0);
    assert(!wifi_portal_build_dns_reply(query, sizeof(query) - 2, ip,
                                        reply, sizeof(reply)));
    uint8_t compressed[sizeof(query)];
    memcpy(compressed, query, sizeof(query));
    compressed[12] = 0xc0;
    assert(!wifi_portal_build_dns_reply(compressed, sizeof(compressed), ip,
                                        reply, sizeof(reply)));
    return 0;
}
