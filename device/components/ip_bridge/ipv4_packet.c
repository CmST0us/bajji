// SPDX-License-Identifier: MIT
#include "ipv4_packet.h"

#include "bridge_protocol.h"

static bool address_matches(const uint8_t* address, uint32_t expected) {
    return address[0] == (uint8_t)(expected >> 24U) &&
           address[1] == (uint8_t)(expected >> 16U) &&
           address[2] == (uint8_t)(expected >> 8U) &&
           address[3] == (uint8_t)expected;
}

ipv4_packet_result_t ipv4_packet_validate(const uint8_t* packet, size_t length,
                                          uint32_t expected_address,
                                          bool from_expected_address) {
    if (!packet || length < 20 || length > BRIDGE_MAX_PAYLOAD) return IPV4_PACKET_BAD_SIZE;
    if ((packet[0] >> 4U) != 4U) return IPV4_PACKET_BAD_VERSION;
    const size_t header_length = (size_t)(packet[0] & 0x0fU) * 4U;
    if (header_length < 20 || header_length > length) return IPV4_PACKET_BAD_HEADER;
    const size_t total_length = ((size_t)packet[2] << 8U) | packet[3];
    if (total_length != length) return IPV4_PACKET_BAD_SIZE;
    const uint16_t fragment = (uint16_t)((uint16_t)packet[6] << 8U) | packet[7];
    if ((fragment & 0x3fffU) != 0U) return IPV4_PACKET_FRAGMENTED;
    const uint8_t* address = packet + (from_expected_address ? 12U : 16U);
    if (!address_matches(address, expected_address)) return IPV4_PACKET_WRONG_ADDRESS;
    return IPV4_PACKET_OK;
}
