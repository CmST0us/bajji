// SPDX-License-Identifier: MIT
#include "bridge_protocol.h"

#include <assert.h>
#include <string.h>

static const uint8_t ping[] = {
    0xba, 0x77, 0x01, 0x20, 0x00, 0x08, 0x00, 0x2a,
    1, 2, 3, 4, 5, 6, 7, 8,
};

static void split_frame(void) {
    bridge_parser_t parser = {0};
    bridge_frame_t frame = {0};
    size_t consumed = 0;
    assert(bridge_parser_feed(&parser, ping, 3, &consumed, &frame) == BRIDGE_NEED_MORE);
    assert(consumed == 3);
    assert(bridge_parser_feed(&parser, ping + 3, sizeof(ping) - 3, &consumed, &frame) == BRIDGE_FRAME_READY);
    assert(frame.type == BRIDGE_TYPE_PING);
    assert(frame.sequence == 42);
    assert(frame.payload_len == 8);
    assert(memcmp(frame.payload, ping + BRIDGE_HEADER_SIZE, 8) == 0);
}

static void coalesced_frames(void) {
    uint8_t stream[sizeof(ping) * 2];
    memcpy(stream, ping, sizeof(ping));
    memcpy(stream + sizeof(ping), ping, sizeof(ping));
    bridge_parser_t parser = {0};
    bridge_frame_t frame = {0};
    size_t first = 0;
    size_t second = 0;
    assert(bridge_parser_feed(&parser, stream, sizeof(stream), &first, &frame) == BRIDGE_FRAME_READY);
    assert(first == sizeof(ping));
    assert(bridge_parser_feed(&parser, stream + first, sizeof(stream) - first, &second, &frame) == BRIDGE_FRAME_READY);
    assert(second == sizeof(ping));
}

static void resynchronizes_magic(void) {
    const uint8_t garbage[] = {0, 0xba, 0xba};
    bridge_parser_t parser = {0};
    bridge_frame_t frame = {0};
    size_t consumed = 0;
    assert(bridge_parser_feed(&parser, garbage, sizeof(garbage), &consumed, &frame) == BRIDGE_NEED_MORE);
    assert(bridge_parser_feed(&parser, ping + 1, sizeof(ping) - 1, &consumed, &frame) == BRIDGE_FRAME_READY);
}

static void rejects_invalid_headers(void) {
    uint8_t invalid[BRIDGE_HEADER_SIZE] = {0xba, 0x77, 2, BRIDGE_TYPE_PING, 0, 8, 0, 0};
    bridge_parser_t parser = {0};
    bridge_frame_t frame = {0};
    size_t consumed = 0;
    assert(bridge_parser_feed(&parser, invalid, sizeof(invalid), &consumed, &frame) == BRIDGE_PROTOCOL_ERROR);
    invalid[2] = 1;
    invalid[3] = 0x99;
    assert(bridge_parser_feed(&parser, invalid, sizeof(invalid), &consumed, &frame) == BRIDGE_PROTOCOL_ERROR);
    invalid[3] = BRIDGE_TYPE_IPV4;
    invalid[4] = 0x05;
    invalid[5] = 0x01;
    assert(bridge_parser_feed(&parser, invalid, sizeof(invalid), &consumed, &frame) == BRIDGE_PROTOCOL_ERROR);
}

static void encodes_frame(void) {
    bridge_frame_t frame = {.type = BRIDGE_TYPE_PONG, .sequence = 0x1234, .payload_len = 8};
    memcpy(frame.payload, ping + BRIDGE_HEADER_SIZE, 8);
    uint8_t encoded[BRIDGE_MAX_FRAME_SIZE];
    const size_t size = bridge_encode(&frame, encoded, sizeof(encoded));
    assert(size == 16);
    assert(encoded[6] == 0x12 && encoded[7] == 0x34);
    assert(bridge_encode(&frame, encoded, 15) == 0);
}

static void clear_bond_frame(void) {
    bridge_frame_t frame = {.type = BRIDGE_TYPE_CLEAR_BOND, .sequence = 7, .payload_len = 0};
    uint8_t encoded[BRIDGE_HEADER_SIZE];
    assert(bridge_encode(&frame, encoded, sizeof(encoded)) == sizeof(encoded));

    bridge_parser_t parser = {0};
    bridge_frame_t parsed = {0};
    size_t consumed = 0;
    assert(bridge_parser_feed(&parser, encoded, sizeof(encoded), &consumed, &parsed) == BRIDGE_FRAME_READY);
    assert(parsed.type == BRIDGE_TYPE_CLEAR_BOND);
    assert(parsed.sequence == 7);
    assert(parsed.payload_len == 0);
}

int main(void) {
    split_frame();
    coalesced_frames();
    resynchronizes_magic();
    rejects_invalid_headers();
    encodes_frame();
    clear_bond_frame();
    return 0;
}
