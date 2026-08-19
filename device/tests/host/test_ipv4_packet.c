// SPDX-License-Identifier: MIT
#include "ipv4_packet.h"

#include <assert.h>
#include <string.h>

static const uint8_t valid_udp[] = {
    0x45, 0x00, 0x00, 0x1c, 0, 1, 0x40, 0, 64, 17, 0, 0,
    10, 77, 0, 2, 1, 1, 1, 1,
    0xc0, 0x00, 0x00, 0x35, 0x00, 0x08, 0, 0,
};

int main(void) {
    assert(ipv4_packet_validate(valid_udp, sizeof(valid_udp), 0x0a4d0002U, true) == IPV4_PACKET_OK);

    uint8_t packet[sizeof(valid_udp)];
    memcpy(packet, valid_udp, sizeof(packet));
    packet[0] = 0x65;
    assert(ipv4_packet_validate(packet, sizeof(packet), 0x0a4d0002U, true) == IPV4_PACKET_BAD_VERSION);

    memcpy(packet, valid_udp, sizeof(packet));
    packet[0] = 0x44;
    assert(ipv4_packet_validate(packet, sizeof(packet), 0x0a4d0002U, true) == IPV4_PACKET_BAD_HEADER);

    memcpy(packet, valid_udp, sizeof(packet));
    packet[3] = 0x1b;
    assert(ipv4_packet_validate(packet, sizeof(packet), 0x0a4d0002U, true) == IPV4_PACKET_BAD_SIZE);

    memcpy(packet, valid_udp, sizeof(packet));
    packet[6] = 0x20;
    assert(ipv4_packet_validate(packet, sizeof(packet), 0x0a4d0002U, true) == IPV4_PACKET_FRAGMENTED);

    memcpy(packet, valid_udp, sizeof(packet));
    packet[15] = 3;
    assert(ipv4_packet_validate(packet, sizeof(packet), 0x0a4d0002U, true) == IPV4_PACKET_WRONG_ADDRESS);

    memcpy(packet, valid_udp, sizeof(packet));
    packet[16] = 10;
    packet[17] = 77;
    packet[18] = 0;
    packet[19] = 2;
    assert(ipv4_packet_validate(packet, sizeof(packet), 0x0a4d0002U, false) == IPV4_PACKET_OK);

    assert(ipv4_packet_validate(valid_udp, 19, 0x0a4d0002U, true) == IPV4_PACKET_BAD_SIZE);
    return 0;
}
