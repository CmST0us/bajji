// SPDX-License-Identifier: MIT
import CoreBluetooth
import Foundation

final class L2CAPStream: NSObject, StreamDelegate, @unchecked Sendable {
    private let input: InputStream
    private let output: OutputStream
    private var parser = BridgeStreamParser()
    private var pending: [Data] = []
    private var pendingOffset = 0
    private var closed = false

    var onFrame: ((BridgeFrame) -> Void)?
    var onBytes: ((_ received: Int, _ sent: Int) -> Void)?
    var onDrop: (() -> Void)?
    var onClose: ((String) -> Void)?

    init(channel: CBL2CAPChannel) {
        input = channel.inputStream
        output = channel.outputStream
    }

    func start() {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            input.delegate = self
            output.delegate = self
            input.schedule(in: .main, forMode: .default)
            output.schedule(in: .main, forMode: .default)
            input.open()
            output.open()
        }
    }

    func send(_ frame: BridgeFrame) {
        guard let encoded = try? frame.encode() else { return }
        DispatchQueue.main.async { [weak self] in
            guard let self, !closed else { return }
            guard pending.count < 32 else {
                onDrop?()
                return
            }
            pending.append(encoded)
            drainWrites()
        }
    }

    func stop() {
        DispatchQueue.main.async { [weak self] in self?.close("stopped") }
    }

    func stream(_ stream: Stream, handle eventCode: Stream.Event) {
        switch eventCode {
        case .hasBytesAvailable:
            readAvailableBytes()
        case .hasSpaceAvailable:
            drainWrites()
        case .errorOccurred:
            close(stream.streamError?.localizedDescription ?? "stream error")
        case .endEncountered:
            close("stream ended")
        default:
            break
        }
    }

    private func readAvailableBytes() {
        var bytes = [UInt8](repeating: 0, count: 4096)
        while input.hasBytesAvailable {
            let count = input.read(&bytes, maxLength: bytes.count)
            guard count > 0 else {
                if count < 0 { close(input.streamError?.localizedDescription ?? "read failed") }
                return
            }
            onBytes?(count, 0)
            do {
                for frame in try parser.append(bytes.prefix(count)) {
                    onFrame?(frame)
                }
            } catch {
                close("protocol error: \(error)")
                return
            }
        }
    }

    private func drainWrites() {
        while output.hasSpaceAvailable, let data = pending.first {
            let count = data.withUnsafeBytes { rawBuffer in
                let bytes = rawBuffer.bindMemory(to: UInt8.self)
                return output.write(bytes.baseAddress!.advanced(by: pendingOffset),
                                    maxLength: data.count - pendingOffset)
            }
            guard count > 0 else {
                if count < 0 { close(output.streamError?.localizedDescription ?? "write failed") }
                return
            }
            pendingOffset += count
            onBytes?(0, count)
            if pendingOffset == data.count {
                pending.removeFirst()
                pendingOffset = 0
            }
        }
    }

    private func close(_ reason: String) {
        guard !closed else { return }
        closed = true
        input.remove(from: .main, forMode: .default)
        output.remove(from: .main, forMode: .default)
        input.close()
        output.close()
        pending.removeAll()
        onClose?(reason)
    }
}
