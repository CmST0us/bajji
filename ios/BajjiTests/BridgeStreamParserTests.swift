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

    #if !SWIFT_PACKAGE
    @Test func phaseZeroGeneratesAndCountsEcho() async throws {
        let runner = PhaseZeroRunner()
        await withCheckedContinuation { continuation in
            runner.start { frame in
                runner.receive(frame)
                runner.stop()
                continuation.resume()
            }
        }
        try await Task.sleep(for: .milliseconds(20))

        let snapshot = runner.snapshot()
        #expect(snapshot.sentPayloadBytes == UInt64(BridgeFrame.maximumPayload))
        #expect(snapshot.echoedPayloadBytes == UInt64(BridgeFrame.maximumPayload))
    }
    #endif
}
