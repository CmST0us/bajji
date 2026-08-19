// SPDX-License-Identifier: MIT
import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

enum PacketPipeError: Error, Equatable {
    case createFailed(Int32)
    case configureFailed(Int32)
    case closed
    case wouldBlock
    case invalidDatagram
    case ioFailed(Int32)
}

final class PacketPipe: @unchecked Sendable {
    static let maximumDatagramSize = BridgeFrame.maximumPayload + 4

    private let lock = NSLock()
    private var deviceFD: Int32
    private var tunnelFD: Int32

    init() throws {
        var descriptors = [Int32](repeating: -1, count: 2)
        #if canImport(Darwin)
        let kind = SOCK_DGRAM
        #else
        let kind = Int32(SOCK_DGRAM.rawValue)
        #endif
        guard socketpair(AF_UNIX, kind, 0, &descriptors) == 0 else {
            throw PacketPipeError.createFailed(errno)
        }
        do {
            for descriptor in descriptors {
                guard fcntl(descriptor, F_SETFL, fcntl(descriptor, F_GETFL) | O_NONBLOCK) == 0 else {
                    throw PacketPipeError.configureFailed(errno)
                }
            }
        } catch {
            Darwin.close(descriptors[0])
            Darwin.close(descriptors[1])
            throw error
        }
        deviceFD = descriptors[0]
        tunnelFD = descriptors[1]
    }

    var forwarderFD: Int32 { lock.withLock { tunnelFD } }
    var readerFD: Int32 { lock.withLock { deviceFD } }

    func writePacket(_ packet: Data) throws {
        guard packet.count <= BridgeFrame.maximumPayload else {
            throw PacketPipeError.invalidDatagram
        }
        let datagram = IPv4Packet.tunDatagram(packet)
        let descriptor = lock.withLock { deviceFD }
        guard descriptor >= 0 else { throw PacketPipeError.closed }
        let count = datagram.withUnsafeBytes {
            send(descriptor, $0.baseAddress, $0.count, MSG_DONTWAIT)
        }
        guard count == datagram.count else {
            if count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) {
                throw PacketPipeError.wouldBlock
            }
            throw PacketPipeError.ioFailed(errno)
        }
    }

    func readPacket() throws -> Data? {
        let descriptor = lock.withLock { deviceFD }
        guard descriptor >= 0 else { throw PacketPipeError.closed }
        var bytes = [UInt8](repeating: 0, count: Self.maximumDatagramSize + 1)
        let count = recv(descriptor, &bytes, bytes.count, MSG_DONTWAIT)
        if count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) { return nil }
        guard count > 0, count <= Self.maximumDatagramSize else {
            if count < 0 { throw PacketPipeError.ioFailed(errno) }
            throw PacketPipeError.invalidDatagram
        }
        return try IPv4Packet.packet(fromTunDatagram: Data(bytes.prefix(count)))
    }

    func close() {
        let descriptors = lock.withLock { () -> (Int32, Int32) in
            let result = (deviceFD, tunnelFD)
            deviceFD = -1
            tunnelFD = -1
            return result
        }
        if descriptors.0 >= 0 { Darwin.close(descriptors.0) }
        if descriptors.1 >= 0 { Darwin.close(descriptors.1) }
    }

    deinit { close() }
}
