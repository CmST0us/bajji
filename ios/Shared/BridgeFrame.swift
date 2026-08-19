// SPDX-License-Identifier: MIT
import Foundation

enum BridgeProtocolError: Error, Equatable {
    case invalidHeader
    case invalidPayloadLength
    case oversizedBuffer
    case invalidBridgeInfo
}

struct BridgeFrame: Equatable, Sendable {
    static let magic: [UInt8] = [0xBA, 0x77]
    static let version: UInt8 = 1
    static let maximumPayload = 1280
    static let headerSize = 8

    enum Kind: UInt8, Sendable {
        case hello = 0x01
        case helloAck = 0x02
        case ipv4 = 0x10
        case ping = 0x20
        case pong = 0x21
        case clearBond = 0x30
        case error = 0x7F

        func accepts(length: Int) -> Bool {
            switch self {
            case .hello: length == 22
            case .helloAck: length == 7
            case .ipv4: (20...BridgeFrame.maximumPayload).contains(length)
            case .ping, .pong: length == 8
            case .clearBond: length == 0
            case .error: length == 2
            }
        }
    }

    let type: Kind
    let sequence: UInt16
    let payload: Data

    func encode() throws -> Data {
        guard type.accepts(length: payload.count) else {
            throw BridgeProtocolError.invalidPayloadLength
        }
        let length = UInt16(payload.count)
        var encoded = Data([
            Self.magic[0], Self.magic[1], Self.version, type.rawValue,
            UInt8(length >> 8), UInt8(length & 0xFF),
            UInt8(sequence >> 8), UInt8(sequence & 0xFF)
        ])
        encoded.append(payload)
        return encoded
    }
}
