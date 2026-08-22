// SPDX-License-Identifier: MIT
#include "wifi_portal_protocol.h"

#include <stdbool.h>
#include <string.h>

enum {
    kDnsHeaderLength = 12,
    kDnsAnswerLength = 16,
    kTokenLength = 32,
};

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int decode_component(const char* encoded, size_t length, uint8_t* decoded,
                            size_t capacity, size_t* decoded_length) {
    size_t output = 0;
    for (size_t index = 0; index < length; ++index) {
        unsigned value = (unsigned char)encoded[index];
        if (value == '+') {
            value = ' ';
        } else if (value == '%') {
            if (index + 2 >= length) return 0;
            const int high = hex_value(encoded[index + 1]);
            const int low = hex_value(encoded[index + 2]);
            if (high < 0 || low < 0) return 0;
            value = (unsigned)((high << 4) | low);
            index += 2;
        }
        if (value == 0 || output >= capacity) return 0;
        decoded[output++] = (uint8_t)value;
    }
    *decoded_length = output;
    return 1;
}

static int token_matches(const uint8_t* token, size_t length, const char* expected) {
    if (!expected || length != kTokenLength || strlen(expected) != kTokenLength) return 0;
    unsigned difference = 0;
    for (size_t index = 0; index < kTokenLength; ++index) {
        difference |= token[index] ^ (uint8_t)expected[index];
    }
    return difference == 0;
}

int wifi_portal_decode_form(const char* body, size_t length, const char* expected_token,
                            wifi_portal_form_t* form) {
    if (!body || !length || !form) return 0;
    wifi_portal_form_t decoded = {0};
    bool has_token = false;
    bool has_network = false;
    bool has_password = false;

    size_t offset = 0;
    while (offset < length) {
        const size_t field_start = offset;
        while (offset < length && body[offset] != '&') ++offset;
        const size_t field_end = offset;
        if (offset < length) ++offset;

        size_t equals = field_start;
        while (equals < field_end && body[equals] != '=') ++equals;
        if (equals == field_end) return 0;
        const char* value = body + equals + 1;
        const size_t value_length = field_end - equals - 1;
        const size_t name_length = equals - field_start;

        if (name_length == 5 && memcmp(body + field_start, "token", 5) == 0) {
            uint8_t token[kTokenLength];
            size_t token_length = 0;
            if (has_token || !decode_component(value, value_length, token, sizeof(token),
                                                &token_length) ||
                !token_matches(token, token_length, expected_token)) return 0;
            has_token = true;
        } else if (name_length == 7 && memcmp(body + field_start, "network", 7) == 0) {
            if (has_network || value_length == 0 || value_length > 5) return 0;
            unsigned network = 0;
            for (size_t index = 0; index < value_length; ++index) {
                if (value[index] < '0' || value[index] > '9') return 0;
                network = network * 10U + (unsigned)(value[index] - '0');
                if (network > UINT16_MAX) return 0;
            }
            decoded.network_index = (uint16_t)network;
            has_network = true;
        } else if (name_length == 8 && memcmp(body + field_start, "password", 8) == 0) {
            size_t password_length = 0;
            if (has_password || !decode_component(value, value_length, decoded.password,
                                                   sizeof(decoded.password),
                                                   &password_length)) return 0;
            decoded.password_length = (uint8_t)password_length;
            has_password = true;
        } else {
            return 0;
        }
    }
    if (!has_token || !has_network || !has_password) return 0;
    *form = decoded;
    return 1;
}

static uint16_t read_be16(const uint8_t* bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_be16(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

size_t wifi_portal_build_dns_reply(const uint8_t* request, size_t request_length,
                                   const uint8_t ipv4[4], uint8_t* reply,
                                   size_t reply_capacity) {
    if (!request || !ipv4 || !reply || request_length < kDnsHeaderLength) return 0;
    const uint16_t flags = read_be16(request + 2);
    if ((flags & 0x8000U) || (flags & 0x7800U) || read_be16(request + 4) != 1) return 0;

    size_t cursor = kDnsHeaderLength;
    while (cursor < request_length && request[cursor] != 0) {
        const uint8_t label_length = request[cursor++];
        if (label_length > 63 || cursor + label_length > request_length) return 0;
        cursor += label_length;
    }
    if (cursor >= request_length || request[cursor] != 0) return 0;
    ++cursor;
    if (cursor + 4 > request_length || read_be16(request + cursor) != 1 ||
        read_be16(request + cursor + 2) != 1) return 0;
    cursor += 4;
    if (cursor + kDnsAnswerLength > reply_capacity) return 0;

    memcpy(reply, request, cursor);
    write_be16(reply + 2, (uint16_t)(0x8400U | (flags & 0x0100U)));
    write_be16(reply + 4, 1);
    write_be16(reply + 6, 1);
    write_be16(reply + 8, 0);
    write_be16(reply + 10, 0);

    uint8_t* answer = reply + cursor;
    write_be16(answer, 0xc00c);
    write_be16(answer + 2, 1);
    write_be16(answer + 4, 1);
    write_be32(answer + 6, 60);
    write_be16(answer + 10, 4);
    memcpy(answer + 12, ipv4, 4);
    return cursor + kDnsAnswerLength;
}
