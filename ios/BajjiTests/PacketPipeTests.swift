// SPDX-License-Identifier: MIT
import Foundation
import Testing
#if SWIFT_PACKAGE
@testable import BridgeCore
#endif

struct PacketPipeTests {
    private func packet(id: UInt8) -> Data {
        Data([
            0x45, 0, 0, 20, 0, id, 0x40, 0, 64, 6, 0, 0,
            10, 77, 0, 2, 1, 1, 1, id,
        ])
    }

    @Test func preservesDatagramBoundaries() throws {
        let pipe = try PacketPipe()
        let first = packet(id: 1)
        let second = packet(id: 2)
        try pipe.writePacket(first)
        try pipe.writePacket(second)

        var bytes = [UInt8](repeating: 0, count: PacketPipe.maximumDatagramSize)
        let firstCount = recv(pipe.forwarderFD, &bytes, bytes.count, 0)
        #expect(try IPv4Packet.packet(fromTunDatagram: Data(bytes.prefix(firstCount))) == first)
        let secondCount = recv(pipe.forwarderFD, &bytes, bytes.count, 0)
        #expect(try IPv4Packet.packet(fromTunDatagram: Data(bytes.prefix(secondCount))) == second)
    }

    @Test func readsOneForwarderPacketAndCloseIsIdempotent() throws {
        let pipe = try PacketPipe()
        let expected = packet(id: 3)
        let datagram = IPv4Packet.tunDatagram(expected)
        let count = datagram.withUnsafeBytes {
            send(pipe.forwarderFD, $0.baseAddress, $0.count, 0)
        }
        #expect(count == datagram.count)
        #expect(try pipe.readPacket() == expected)
        #expect(try pipe.readPacket() == nil)
        pipe.close()
        pipe.close()
        #expect(throws: PacketPipeError.closed) { try pipe.writePacket(expected) }
    }
}
