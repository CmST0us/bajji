// SPDX-License-Identifier: MIT
import Foundation
import OSLog

struct IPForwarderSnapshot: Codable {
    var state = "idle"
    var fromDevicePackets: UInt64 = 0
    var fromDeviceBytes: UInt64 = 0
    var toDevicePackets: UInt64 = 0
    var toDeviceBytes: UInt64 = 0
    var droppedPackets: UInt64 = 0
    var invalidPackets: UInt64 = 0
    var lastError = ""
    var hev = HEVSnapshot()
}

final class IPForwarder: @unchecked Sendable {
    private let queue = DispatchQueue(label: "com.eric3u.bajji.forwarder.ip")
    private let lock = NSLock()
    private let ingressSlots = DispatchSemaphore(value: 32)
    private let logger = Logger(subsystem: "com.eric3u.bajji", category: "IPForwarder")
    private let hev = HEVForwarder()
    private var pipe: PacketPipe?
    private var source: DispatchSourceRead?
    private var sendToDevice: ((BridgeFrame) -> Void)?
    private var current = IPForwarderSnapshot()
    private var sequence: UInt16 = 1

    func start(sendToDevice: @escaping @Sendable (BridgeFrame) -> Void) throws {
        stop()
        let pipe = try PacketPipe()
        do {
            try hev.start(fd: pipe.forwarderFD)
        } catch {
            pipe.close()
            throw error
        }
        let source = DispatchSource.makeReadSource(fileDescriptor: pipe.readerFD, queue: queue)
        source.setEventHandler { [weak self] in self?.drainResponses() }
        self.pipe = pipe
        self.source = source
        self.sendToDevice = sendToDevice
        lock.withLock { current = IPForwarderSnapshot(state: "running") }
        source.resume()
        logger.info("IPv4 forwarding started")
    }

    func receiveFromDevice(_ frame: BridgeFrame) {
        guard ingressSlots.wait(timeout: .now()) == .success else {
            update { $0.droppedPackets += 1 }
            return
        }
        queue.async { [weak self] in
            guard let self else { return }
            defer { ingressSlots.signal() }
            guard frame.type == .ipv4 else { return }
            do {
                try IPv4Packet.validate(frame.payload, expectedAddress: [10, 77, 0, 2],
                                        direction: .fromDevice)
                guard let pipe else { throw PacketPipeError.closed }
                try pipe.writePacket(frame.payload)
                update {
                    $0.fromDevicePackets += 1
                    $0.fromDeviceBytes += UInt64(frame.payload.count)
                }
            } catch let error as IPv4PacketError {
                update {
                    $0.invalidPackets += 1
                    $0.lastError = String(describing: error)
                }
            } catch {
                update {
                    $0.droppedPackets += 1
                    $0.lastError = error.localizedDescription
                }
            }
        }
    }

    func stop() {
        let cleanup = {
            self.source?.cancel()
            self.source = nil
            self.hev.stop()
            self.pipe?.close()
            self.pipe = nil
            self.sendToDevice = nil
            self.lock.withLock { self.current.state = "stopped" }
        }
        if DispatchQueue.getSpecific(key: queueKey) == queueValue {
            cleanup()
        } else {
            queue.sync(execute: cleanup)
        }
    }

    func snapshot() -> IPForwarderSnapshot {
        lock.withLock {
            current.hev = hev.snapshot()
            return current
        }
    }

    private let queueKey: DispatchSpecificKey<UInt8> = {
        let key = DispatchSpecificKey<UInt8>()
        return key
    }()
    private let queueValue: UInt8 = 1

    private func drainResponses() {
        guard let pipe else { return }
        while true {
            do {
                guard let packet = try pipe.readPacket() else { return }
                try IPv4Packet.validate(packet, expectedAddress: [10, 77, 0, 2], direction: .toDevice)
                let frame = BridgeFrame(type: .ipv4, sequence: sequence, payload: packet)
                sequence &+= 1
                sendToDevice?(frame)
                update {
                    $0.toDevicePackets += 1
                    $0.toDeviceBytes += UInt64(packet.count)
                }
            } catch let error as IPv4PacketError {
                update {
                    $0.invalidPackets += 1
                    $0.lastError = String(describing: error)
                }
            } catch {
                update {
                    $0.droppedPackets += 1
                    $0.lastError = error.localizedDescription
                }
                return
            }
        }
    }

    private func update(_ change: (inout IPForwarderSnapshot) -> Void) {
        lock.withLock { change(&current) }
    }

    init() {
        queue.setSpecific(key: queueKey, value: queueValue)
    }
}
