// SPDX-License-Identifier: MIT
import Foundation

struct PhaseZeroSnapshot: Codable {
    var running = false
    var sentPayloadBytes: UInt64 = 0
    var echoedPayloadBytes: UInt64 = 0
    var elapsedSeconds = 0.0
    var echoedKilobytesPerSecond = 0.0
}

final class PhaseZeroRunner: @unchecked Sendable {
    private let queue = DispatchQueue(label: "com.eric3u.bajji.phase-zero")
    private let lock = NSLock()
    private var timer: DispatchSourceTimer?
    private var state = PhaseZeroSnapshot()
    private var startedAt = Date()
    private var sequence: UInt16 = 0

    func start(send: @escaping @Sendable (BridgeFrame) -> Void) {
        queue.async { [weak self] in
            guard let self, timer == nil else { return }
            sequence = 0
            lock.withLock {
                startedAt = Date()
                state = PhaseZeroSnapshot(running: true)
            }
            let timer = DispatchSource.makeTimerSource(queue: queue)
            timer.schedule(deadline: .now(), repeating: .milliseconds(10))
            timer.setEventHandler { [weak self] in
                guard let self else { return }
                let elapsed = lock.withLock { Date().timeIntervalSince(startedAt) }
                if elapsed >= 60 {
                    stopOnQueue()
                    return
                }
                var payload = Data(repeating: 0xA5, count: BridgeFrame.maximumPayload)
                payload.replaceSubrange(0..<20, with: [
                    0x45, 0, 0x05, 0, 0, 0, 0x40, 0, 64, 17, 0, 0,
                    10, 77, 0, 1, 10, 77, 0, 2
                ])
                send(BridgeFrame(type: .ipv4, sequence: sequence, payload: payload))
                sequence &+= 1
                lock.withLock {
                    state.sentPayloadBytes += UInt64(payload.count)
                    updateRate(elapsed: elapsed)
                }
            }
            self.timer = timer
            timer.resume()
        }
    }

    func receive(_ frame: BridgeFrame) {
        guard frame.type == .ipv4 else { return }
        lock.withLock {
            state.echoedPayloadBytes += UInt64(frame.payload.count)
            updateRate(elapsed: Date().timeIntervalSince(startedAt))
        }
    }

    func stop() {
        queue.async { [weak self] in self?.stopOnQueue() }
    }

    func snapshot() -> PhaseZeroSnapshot {
        lock.withLock { state }
    }

    private func stopOnQueue() {
        timer?.cancel()
        timer = nil
        lock.withLock {
            state.running = false
            updateRate(elapsed: Date().timeIntervalSince(startedAt))
        }
    }

    private func updateRate(elapsed: TimeInterval) {
        state.elapsedSeconds = elapsed
        state.echoedKilobytesPerSecond = elapsed > 0
            ? Double(state.echoedPayloadBytes) / elapsed / 1000
            : 0
    }
}
