// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t network_index;
    uint8_t password[64];
    uint8_t password_length;
} wifi_portal_form_t;

int wifi_portal_decode_form(const char* body, size_t length, const char* expected_token,
                            wifi_portal_form_t* form);

size_t wifi_portal_build_dns_reply(const uint8_t* request, size_t request_length,
                                   const uint8_t ipv4[4], uint8_t* reply,
                                   size_t reply_capacity);

#ifdef __cplusplus
}
#endif
