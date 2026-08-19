// SPDX-License-Identifier: MIT
import Foundation
import Testing
#if SWIFT_PACKAGE
@testable import BridgeCore
#endif

private final class FrameRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var frames: [BridgeFrame] = []

    func append(_ frame: BridgeFrame) { lock.withLock { frames.append(frame) } }
    var count: Int { lock.withLock { frames.count } }
    var first: BridgeFrame? { lock.withLock { frames.first } }
}

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

    #if !SWIFT_PACKAGE
    @Test func phaseZeroKeepsFourFramesInFlight() async throws {
        let runner = PhaseZeroRunner()
        let recorder = FrameRecorder()
        runner.start { recorder.append($0) }
        for _ in 0..<20 where recorder.count < 4 {
            try await Task.sleep(for: .milliseconds(5))
        }

        #expect(recorder.count == 4)
        #expect(runner.snapshot().inFlightFrames == 4)

        runner.receive(try #require(recorder.first))
        for _ in 0..<20 where recorder.count < 5 {
            try await Task.sleep(for: .milliseconds(5))
        }

        let running = runner.snapshot()
        #expect(recorder.count == 5)
        #expect(running.inFlightFrames == 4)
        #expect(running.sentPayloadBytes == UInt64(BridgeFrame.maximumPayload * 5))
        #expect(running.echoedPayloadBytes == UInt64(BridgeFrame.maximumPayload))

        runner.stop()
    }
    #endif
}
