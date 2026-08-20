// SPDX-License-Identifier: MIT
import Foundation
import Testing
#if SWIFT_PACKAGE
@testable import BridgeCore
#endif

struct IPv4PacketTests {
    private let valid = Data([
        0x45, 0, 0, 28, 0, 1, 0x40, 0, 64, 17, 0, 0,
        10, 77, 0, 2, 1, 1, 1, 1,
        0xC0, 0, 0, 53, 0, 8, 0, 0
    ])

    @Test func validatesDirectionAndHeader() throws {
        try IPv4Packet.validate(valid, expectedAddress: [10, 77, 0, 2], direction: .fromDevice)

        var fragment = valid
        fragment[6] = 0x20
        #expect(throws: IPv4PacketError.fragmented) {
            try IPv4Packet.validate(fragment, expectedAddress: [10, 77, 0, 2], direction: .fromDevice)
        }

        var response = valid
        response.replaceSubrange(16..<20, with: [10, 77, 0, 2])
        try IPv4Packet.validate(response, expectedAddress: [10, 77, 0, 2], direction: .toDevice)
    }

    @Test func addsAndStripsDarwinTunPrefix() throws {
        let datagram = IPv4Packet.tunDatagram(valid)
        #expect(Array(datagram.prefix(4)) == [0, 0, 0, UInt8(AF_INET)])
        #expect(try IPv4Packet.packet(fromTunDatagram: datagram) == valid)

        var wrong = datagram
        wrong[3] = UInt8(AF_INET6)
        #expect(throws: IPv4PacketError.unsupportedAddressFamily(UInt32(AF_INET6))) {
            try IPv4Packet.packet(fromTunDatagram: wrong)
        }
    }
}
