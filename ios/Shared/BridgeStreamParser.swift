// SPDX-License-Identifier: MIT
import Foundation

struct BridgeStreamParser {
    private var buffer = Data()

    mutating func append<S: DataProtocol>(_ bytes: S) throws -> [BridgeFrame] {
        buffer.append(contentsOf: bytes)
        var frames: [BridgeFrame] = []

        while true {
            resynchronizeMagic()
            guard buffer.count >= BridgeFrame.headerSize else { break }
            guard byte(at: 2) == BridgeFrame.version,
                  let kind = BridgeFrame.Kind(rawValue: byte(at: 3)) else {
                buffer.removeAll(keepingCapacity: true)
                throw BridgeProtocolError.invalidHeader
            }
            let payloadLength = Int(byte(at: 4)) << 8 | Int(byte(at: 5))
            guard kind.accepts(length: payloadLength) else {
                buffer.removeAll(keepingCapacity: true)
                throw BridgeProtocolError.invalidPayloadLength
            }
            let frameLength = BridgeFrame.headerSize + payloadLength
            guard buffer.count >= frameLength else { break }
            let sequence = UInt16(byte(at: 6)) << 8 | UInt16(byte(at: 7))
            let payloadStart = buffer.index(buffer.startIndex, offsetBy: BridgeFrame.headerSize)
            let payloadEnd = buffer.index(payloadStart, offsetBy: payloadLength)
            frames.append(BridgeFrame(
                type: kind,
                sequence: sequence,
                payload: Data(buffer[payloadStart..<payloadEnd])
            ))
            buffer.removeFirst(frameLength)
        }

        guard buffer.count <= BridgeFrame.headerSize + BridgeFrame.maximumPayload else {
            buffer.removeAll(keepingCapacity: true)
            throw BridgeProtocolError.oversizedBuffer
        }
        return frames
    }

    private mutating func resynchronizeMagic() {
        while buffer.count >= 2 && (byte(at: 0) != BridgeFrame.magic[0] || byte(at: 1) != BridgeFrame.magic[1]) {
            buffer.removeFirst()
        }
    }

    private func byte(at offset: Int) -> UInt8 {
        buffer[buffer.index(buffer.startIndex, offsetBy: offset)]
    }
}
