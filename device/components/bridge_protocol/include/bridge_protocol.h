// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRIDGE_HEADER_SIZE 8U
#define BRIDGE_MAX_PAYLOAD 1280U
#define BRIDGE_MAX_FRAME_SIZE (BRIDGE_HEADER_SIZE + BRIDGE_MAX_PAYLOAD)

typedef enum {
    BRIDGE_TYPE_HELLO = 0x01,
    BRIDGE_TYPE_HELLO_ACK = 0x02,
    BRIDGE_TYPE_IPV4 = 0x10,
    BRIDGE_TYPE_PING = 0x20,
    BRIDGE_TYPE_PONG = 0x21,
    BRIDGE_TYPE_TIME_SYNC = 0x22,
    BRIDGE_TYPE_CLEAR_BOND = 0x30,
    BRIDGE_TYPE_ERROR = 0x7f,
} bridge_frame_type_t;

typedef enum {
    BRIDGE_NEED_MORE = 0,
    BRIDGE_FRAME_READY = 1,
    BRIDGE_PROTOCOL_ERROR = -1,
} bridge_parse_result_t;

typedef struct {
    uint8_t type;
    uint16_t sequence;
    uint16_t payload_len;
    uint8_t payload[BRIDGE_MAX_PAYLOAD];
} bridge_frame_t;

typedef struct {
    uint8_t header[BRIDGE_HEADER_SIZE];
    size_t header_used;
    bridge_frame_t frame;
    size_t payload_used;
} bridge_parser_t;

void bridge_parser_reset(bridge_parser_t* parser);

// consumed is always set. On FRAME_READY, feed any unconsumed suffix again.
bridge_parse_result_t bridge_parser_feed(bridge_parser_t* parser, const uint8_t* bytes,
                                         size_t length, size_t* consumed, bridge_frame_t* out);

size_t bridge_encode(const bridge_frame_t* frame, uint8_t* out, size_t capacity);

#ifdef __cplusplus
}
#endif
