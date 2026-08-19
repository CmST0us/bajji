// SPDX-License-Identifier: MIT
#include "bridge_protocol.h"

#include <string.h>

static int payload_length_valid(uint8_t type, uint16_t length) {
    switch (type) {
        case BRIDGE_TYPE_HELLO: return length == 22;
        case BRIDGE_TYPE_HELLO_ACK: return length == 7;
        case BRIDGE_TYPE_IPV4: return length >= 20 && length <= BRIDGE_MAX_PAYLOAD;
        case BRIDGE_TYPE_PING:
        case BRIDGE_TYPE_PONG: return length == 8;
        case BRIDGE_TYPE_ERROR: return length == 2;
        default: return 0;
    }
}

void bridge_parser_reset(bridge_parser_t* parser) {
    if (parser) memset(parser, 0, sizeof(*parser));
}

static bridge_parse_result_t accept_header(bridge_parser_t* parser) {
    const uint16_t length = (uint16_t)((uint16_t)parser->header[4] << 8U) | parser->header[5];
    if (parser->header[2] != 1 || !payload_length_valid(parser->header[3], length)) {
        bridge_parser_reset(parser);
        return BRIDGE_PROTOCOL_ERROR;
    }
    parser->frame.type = parser->header[3];
    parser->frame.payload_len = length;
    parser->frame.sequence = (uint16_t)((uint16_t)parser->header[6] << 8U) | parser->header[7];
    return BRIDGE_NEED_MORE;
}

bridge_parse_result_t bridge_parser_feed(bridge_parser_t* parser, const uint8_t* bytes,
                                         size_t length, size_t* consumed, bridge_frame_t* out) {
    if (consumed) *consumed = 0;
    if (!parser || !consumed || !out || (length && !bytes)) return BRIDGE_PROTOCOL_ERROR;

    for (size_t index = 0; index < length; ++index) {
        const uint8_t byte = bytes[index];
        *consumed = index + 1;
        if (parser->header_used < BRIDGE_HEADER_SIZE) {
            if (parser->header_used == 0) {
                if (byte == 0xba) parser->header[parser->header_used++] = byte;
                continue;
            }
            if (parser->header_used == 1 && byte != 0x77) {
                parser->header_used = byte == 0xba ? 1 : 0;
                parser->header[0] = 0xba;
                continue;
            }
            parser->header[parser->header_used++] = byte;
            if (parser->header_used == BRIDGE_HEADER_SIZE && accept_header(parser) == BRIDGE_PROTOCOL_ERROR) {
                return BRIDGE_PROTOCOL_ERROR;
            }
        } else {
            parser->frame.payload[parser->payload_used++] = byte;
        }

        if (parser->header_used == BRIDGE_HEADER_SIZE &&
            parser->payload_used == parser->frame.payload_len) {
            *out = parser->frame;
            bridge_parser_reset(parser);
            return BRIDGE_FRAME_READY;
        }
    }
    return BRIDGE_NEED_MORE;
}

size_t bridge_encode(const bridge_frame_t* frame, uint8_t* out, size_t capacity) {
    if (!frame || !out || !payload_length_valid(frame->type, frame->payload_len)) return 0;
    const size_t size = BRIDGE_HEADER_SIZE + frame->payload_len;
    if (capacity < size) return 0;
    out[0] = 0xba;
    out[1] = 0x77;
    out[2] = 1;
    out[3] = frame->type;
    out[4] = (uint8_t)(frame->payload_len >> 8U);
    out[5] = (uint8_t)frame->payload_len;
    out[6] = (uint8_t)(frame->sequence >> 8U);
    out[7] = (uint8_t)frame->sequence;
    memcpy(out + BRIDGE_HEADER_SIZE, frame->payload, frame->payload_len);
    return size;
}
