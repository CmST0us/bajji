// SPDX-License-Identifier: MIT
import Foundation
import OSLog

struct PhaseZeroSnapshot: Codable {
    var running = false
    var sentPayloadBytes: UInt64 = 0
    var echoedPayloadBytes: UInt64 = 0
    var elapsedSeconds = 0.0
    var echoedKilobytesPerSecond = 0.0
    var inFlightFrames = 0
}

final class PhaseZeroRunner: @unchecked Sendable {
    private static let windowSize = 24
    private static let payload: Data = {
        var payload = Data(repeating: 0xA5, count: BridgeFrame.maximumPayload)
        payload.replaceSubrange(0..<20, with: [
            0x45, 0, 0x05, 0, 0, 0, 0x40, 0, 64, 17, 0, 0,
            10, 77, 0, 1, 10, 77, 0, 2
        ])
        return payload
    }()

    private let queue = DispatchQueue(label: "com.eric3u.bajji.phase-zero")
    private let lock = NSLock()
    private let logger = Logger(subsystem: "com.eric3u.bajji", category: "PhaseZero")
    private var timer: DispatchSourceTimer?
    private var state = PhaseZeroSnapshot()
    private var startedAt = Date()
    private var sequence: UInt16 = 0
    private var inFlightFrames = 0
    private var sendFrame: (@Sendable (BridgeFrame) -> Void)?

    func start(send: @escaping @Sendable (BridgeFrame) -> Void) {
        queue.async { [weak self] in
            guard let self, timer == nil else { return }
            sequence = 0
            inFlightFrames = 0
            sendFrame = send
            lock.withLock {
                startedAt = Date()
                state = PhaseZeroSnapshot(running: true)
            }
            let timer = DispatchSource.makeTimerSource(queue: queue)
            timer.schedule(deadline: .now() + .seconds(5), repeating: .seconds(5))
            timer.setEventHandler { [weak self] in
                guard let self else { return }
                let elapsed = lock.withLock { Date().timeIntervalSince(startedAt) }
                if elapsed >= 60 {
                    stopOnQueue()
                    return
                }
                let snapshot = lock.withLock {
                    updateRate(elapsed: elapsed)
                    return state
                }
                log(snapshot)
            }
            self.timer = timer
            timer.resume()
            logger.info("started: window=\(Self.windowSize) payload=\(Self.payload.count) B duration=60 s")
            fillWindow()
        }
    }

    func receive(_ frame: BridgeFrame) {
        guard frame.type == .ipv4 else { return }
        queue.async { [weak self] in
            guard let self, timer != nil, inFlightFrames > 0 else { return }
            inFlightFrames -= 1
            lock.withLock {
                state.echoedPayloadBytes += UInt64(frame.payload.count)
                state.inFlightFrames = inFlightFrames
                updateRate(elapsed: Date().timeIntervalSince(startedAt))
            }
            fillWindow()
        }
    }

    func stop() {
        queue.async { [weak self] in self?.stopOnQueue() }
    }

    func snapshot() -> PhaseZeroSnapshot {
        lock.withLock { state }
    }

    private func stopOnQueue() {
        guard timer != nil else { return }
        timer?.cancel()
        timer = nil
        sendFrame = nil
        inFlightFrames = 0
        let snapshot = lock.withLock {
            state.running = false
            state.inFlightFrames = 0
            updateRate(elapsed: Date().timeIntervalSince(startedAt))
            return state
        }
        log(snapshot, prefix: "stopped")
    }

    private func fillWindow() {
        guard timer != nil, let sendFrame else { return }
        while inFlightFrames < Self.windowSize {
            let frame = BridgeFrame(type: .ipv4, sequence: sequence, payload: Self.payload)
            sequence &+= 1
            inFlightFrames += 1
            lock.withLock {
                state.sentPayloadBytes += UInt64(Self.payload.count)
                state.inFlightFrames = inFlightFrames
            }
            sendFrame(frame)
        }
    }

    private func updateRate(elapsed: TimeInterval) {
        state.elapsedSeconds = elapsed
        state.echoedKilobytesPerSecond = elapsed > 0
            ? Double(state.echoedPayloadBytes) / elapsed / 1000
            : 0
    }

    private func log(_ snapshot: PhaseZeroSnapshot, prefix: String = "progress") {
        let elapsed = String(format: "%.1f", snapshot.elapsedSeconds)
        let rate = String(format: "%.1f", snapshot.echoedKilobytesPerSecond)
        logger.info("\(prefix, privacy: .public): elapsed=\(elapsed, privacy: .public) s sent=\(snapshot.sentPayloadBytes) B echoed=\(snapshot.echoedPayloadBytes) B rate=\(rate, privacy: .public) KB/s in_flight=\(snapshot.inFlightFrames)")
    }
}
