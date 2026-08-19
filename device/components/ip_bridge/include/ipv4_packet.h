// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IPV4_PACKET_OK = 0,
    IPV4_PACKET_BAD_SIZE,
    IPV4_PACKET_BAD_VERSION,
    IPV4_PACKET_BAD_HEADER,
    IPV4_PACKET_FRAGMENTED,
    IPV4_PACKET_WRONG_ADDRESS,
} ipv4_packet_result_t;

ipv4_packet_result_t ipv4_packet_validate(const uint8_t* packet, size_t length,
                                          uint32_t expected_address,
                                          bool from_expected_address);

#ifdef __cplusplus
}
#endif
