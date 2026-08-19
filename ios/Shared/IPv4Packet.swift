// SPDX-License-Identifier: MIT
import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

enum PacketDirection: Sendable {
    case fromDevice
    case toDevice
}

enum IPv4PacketError: Error, Equatable {
    case invalidSize
    case invalidVersion
    case invalidHeader
    case fragmented
    case wrongAddress
    case invalidAddressFamily
}

enum IPv4Packet {
    static let maximumSize = BridgeFrame.maximumPayload

    static func validate(_ packet: Data, expectedAddress: [UInt8],
                         direction: PacketDirection) throws {
        guard packet.count >= 20, packet.count <= maximumSize else {
            throw IPv4PacketError.invalidSize
        }
        guard packet[0] >> 4 == 4 else { throw IPv4PacketError.invalidVersion }
        let headerLength = Int(packet[0] & 0x0F) * 4
        guard headerLength >= 20, headerLength <= packet.count else {
            throw IPv4PacketError.invalidHeader
        }
        let totalLength = Int(packet[2]) << 8 | Int(packet[3])
        guard totalLength == packet.count else { throw IPv4PacketError.invalidSize }
        let fragment = UInt16(packet[6]) << 8 | UInt16(packet[7])
        guard fragment & 0x3FFF == 0 else { throw IPv4PacketError.fragmented }
        guard expectedAddress.count == 4 else { throw IPv4PacketError.wrongAddress }
        let offset = direction == .fromDevice ? 12 : 16
        guard Array(packet[offset..<(offset + 4)]) == expectedAddress else {
            throw IPv4PacketError.wrongAddress
        }
    }

    static func tunDatagram(_ packet: Data) -> Data {
        var family = UInt32(AF_INET).bigEndian
        var result = withUnsafeBytes(of: &family) { Data($0) }
        result.append(packet)
        return result
    }

    static func packet(fromTunDatagram datagram: Data) throws -> Data {
        guard datagram.count >= 24 else { throw IPv4PacketError.invalidSize }
        let family = datagram.prefix(4).reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
        guard family == UInt32(AF_INET) else { throw IPv4PacketError.invalidAddressFamily }
        return Data(datagram.dropFirst(4))
    }
}
