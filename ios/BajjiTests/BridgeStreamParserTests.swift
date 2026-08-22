// SPDX-License-Identifier: MIT
import Foundation
import Testing
#if SWIFT_PACKAGE
@testable import BridgeCore
#endif

struct BridgeStreamParserTests {
    private let ping = Data([
        0xBA, 0x77, 0x01, 0x20, 0x00, 0x08, 0x00, 0x2A,
        1, 2, 3, 4, 5, 6, 7, 8
    ])

    @Test func splitAndCoalescedFrames() throws {
        var parser = BridgeStreamParser()
        #expect(try parser.append(ping.prefix(3)).isEmpty)
        #expect(try parser.append(ping.dropFirst(3)) == [
            BridgeFrame(type: .ping, sequence: 42, payload: Data(1...8))
        ])

        let frames = try parser.append(ping + ping)
        #expect(frames.count == 2)
        #expect(frames.allSatisfy { $0.type == .ping })
    }

    @Test func reassemblesMaximumFrameAcrossPeerMTU() throws {
        let frame = BridgeFrame(
            type: .ipv4,
            sequence: 7,
            payload: Data(repeating: 0xA5, count: BridgeFrame.maximumPayload)
        )
        let encoded = try frame.encode()
        var parser = BridgeStreamParser()

        #expect(encoded.count == 1288)
        #expect(try parser.append(encoded.prefix(1251)).isEmpty)
        #expect(try parser.append(encoded.dropFirst(1251)) == [frame])
    }

    @Test func rejectsMalformedHeaderAndOversizePayload() throws {
        var parser = BridgeStreamParser()
        var invalidVersion = ping
        invalidVersion[2] = 2
        #expect(throws: BridgeProtocolError.invalidHeader) {
            try parser.append(invalidVersion)
        }

        let oversizeHeader = Data([0xBA, 0x77, 1, 0x10, 0x05, 0x01, 0, 0])
        #expect(throws: BridgeProtocolError.invalidPayloadLength) {
            try parser.append(oversizeHeader)
        }
    }

    @Test func encodesFrameAndParsesBridgeInfo() throws {
        let encoded = try BridgeFrame(type: .pong, sequence: 0x1234, payload: Data(1...8)).encode()
        #expect(Array(encoded.prefix(8)) == [0xBA, 0x77, 1, 0x21, 0, 8, 0x12, 0x34])

        let id = Data(0..<16)
        let info = try BridgeInfo(data: Data([1, 7, 0, 0x81, 5, 0]) + id)
        #expect(info.psm == 0x81)
        #expect(info.maximumPayload == 1280)
        #expect(info.deviceID == id)
    }

    @Test func encodesAndParsesClearBond() throws {
        let frame = BridgeFrame(type: .clearBond, sequence: 7, payload: Data())
        let encoded = try frame.encode()
        #expect(Array(encoded) == [0xBA, 0x77, 1, 0x30, 0, 0, 0, 7])

        var parser = BridgeStreamParser()
        #expect(try parser.append(encoded) == [frame])
    }

    @Test func encodesAndParsesTimeSync() throws {
        let frame = BridgeFrame(type: .timeSync, sequence: 9, payload: Data([0, 0, 0, 0, 0x68, 0xA6, 0x70, 0x80]))
        let encoded = try frame.encode()
        #expect(Array(encoded.prefix(8)) == [0xBA, 0x77, 1, 0x22, 0, 8, 0, 9])

        var parser = BridgeStreamParser()
        #expect(try parser.append(encoded) == [frame])
    }

    @Test func validatesControlFramesAndCRC() throws {
        let frames = [
            BridgeFrame(type: .settingsGet, sequence: 1, payload: Data()),
            BridgeFrame(type: .settingsSet, sequence: 2, payload: Data(repeating: 0, count: 5)),
            BridgeFrame(type: .settingsState, sequence: 3, payload: Data(repeating: 0, count: 6)),
            BridgeFrame(type: .wallpaperBegin, sequence: 4, payload: Data(repeating: 0, count: 10)),
            BridgeFrame(type: .wallpaperChunk, sequence: 5, payload: Data(repeating: 0, count: 5)),
            BridgeFrame(type: .wallpaperCommit, sequence: 6, payload: Data()),
            BridgeFrame(type: .wallpaperCancel, sequence: 7, payload: Data()),
            BridgeFrame(type: .wallpaperResult, sequence: 8, payload: Data(repeating: 0, count: 6))
        ]
        var parser = BridgeStreamParser()
        let encoded = try frames.reduce(into: Data()) { $0.append(try $1.encode()) }
        #expect(try parser.append(encoded) == frames)
        #expect(BridgeFrame.crc32(Data("123456789".utf8)) == 0xCBF43926)

        #expect(throws: BridgeProtocolError.invalidPayloadLength) {
            try BridgeFrame(
                type: .wallpaperChunk,
                sequence: 9,
                payload: Data(repeating: 0, count: 4)
            ).encode()
        }

        let settings = frames[1]
        #expect(try BridgeFrame.decode(settings.encode()) == settings)
        #expect(throws: BridgeProtocolError.incompleteFrame) {
            try BridgeFrame.decode(Data([0xBA, 0x77]))
        }
    }

}
