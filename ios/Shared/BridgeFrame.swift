// SPDX-License-Identifier: MIT
import Foundation

enum BridgeProtocolError: Error, Equatable {
    case invalidHeader
    case invalidPayloadLength
    case oversizedBuffer
    case invalidBridgeInfo
    case incompleteFrame
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
        case timeSync = 0x22
        case clearBond = 0x30
        case settingsGet = 0x31
        case settingsSet = 0x32
        case settingsState = 0x33
        case wallpaperBegin = 0x40
        case wallpaperChunk = 0x41
        case wallpaperCommit = 0x42
        case wallpaperCancel = 0x43
        case wallpaperResult = 0x44
        case error = 0x7F

        func accepts(length: Int) -> Bool {
            switch self {
            case .hello: length == 22
            case .helloAck: length == 7
            case .ipv4: (20...BridgeFrame.maximumPayload).contains(length)
            case .ping, .pong, .timeSync: length == 8
            case .clearBond, .settingsGet, .wallpaperCommit, .wallpaperCancel: length == 0
            case .settingsSet: length == 5
            case .settingsState: length == 6
            case .wallpaperBegin: length == 10
            case .wallpaperChunk: (5...BridgeFrame.maximumPayload).contains(length)
            case .wallpaperResult: length == 6
            case .error: length == 2
            }
        }

        var controlResponse: Kind? {
            switch self {
            case .settingsGet, .settingsSet: .settingsState
            case .wallpaperBegin, .wallpaperChunk, .wallpaperCommit, .wallpaperCancel:
                .wallpaperResult
            default: nil
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

    static func decode(_ data: Data) throws -> BridgeFrame {
        var parser = BridgeStreamParser()
        let frames = try parser.append(data)
        guard frames.count == 1, try frames[0].encode() == data else {
            throw BridgeProtocolError.incompleteFrame
        }
        return frames[0]
    }

    static func crc32<S: DataProtocol>(_ bytes: S) -> UInt32 {
        var checksum = UInt32.max
        for byte in bytes {
            checksum ^= UInt32(byte)
            for _ in 0..<8 {
                checksum = (checksum >> 1) ^ (0xEDB88320 & (0 &- (checksum & 1)))
            }
        }
        return checksum ^ UInt32.max
    }
}
