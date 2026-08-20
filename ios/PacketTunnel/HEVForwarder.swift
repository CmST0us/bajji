// SPDX-License-Identifier: MIT
import Darwin
import Foundation
import HevSocks5Server
import HevSocks5Tunnel
import OSLog

struct HEVSnapshot: Codable {
    var state = "idle"
    var txPackets: UInt64 = 0
    var txBytes: UInt64 = 0
    var rxPackets: UInt64 = 0
    var rxBytes: UInt64 = 0
    var lastError = ""
}

enum HEVForwarderError: Error, LocalizedError {
    case socket(Int32)
    case serverNotReady
    case invalidDescriptor

    var errorDescription: String? {
        switch self {
        case .socket(let code): "loopback socket error \(code)"
        case .serverNotReady: "local SOCKS5 server did not become ready"
        case .invalidDescriptor: "packet pipe is closed"
        }
    }
}

final class HEVForwarder: @unchecked Sendable {
    private let serverQueue = DispatchQueue(label: "com.eric3u.bajji.forwarder.server")
    private let tunnelQueue = DispatchQueue(label: "com.eric3u.bajji.forwarder.tunnel")
    private let lock = NSLock()
    private let logger = Logger(subsystem: "com.eric3u.bajji", category: "HEV")
    private var current = HEVSnapshot()
    private var running = false

    func start(fd: Int32) throws {
        guard fd >= 0 else { throw HEVForwarderError.invalidDescriptor }
        let port = try reserveLoopbackPort()
        let serverConfiguration = Data("""
        main:
          workers: 1
          port: \(port)
          listen-address: '127.0.0.1'
          udp-port: 0
          udp-listen-address: '127.0.0.1'
          domain-address-type: ipv4
        misc:
          log-file: null
          log-level: warn
        """.utf8)
        let tunnelConfiguration = Data("""
        tunnel:
          mtu: 1280
          ipv4: 10.77.0.1
        socks5:
          port: \(port)
          address: 127.0.0.1
          udp: 'tcp'
        misc:
          task-stack-size: 24576
          tcp-buffer-size: 4096
          max-session-count: 128
          log-file: null
          log-level: warn
        """.utf8)

        lock.withLock {
            running = true
            current = HEVSnapshot(state: "starting SOCKS5")
        }
        serverQueue.async { [weak self] in
            let result = serverConfiguration.withUnsafeBytes { bytes in
                hev_socks5_server_main_from_str(bytes.bindMemory(to: UInt8.self).baseAddress,
                                                UInt32(bytes.count))
            }
            self?.recordExit(component: "SOCKS5", result: result)
        }
        guard waitForServer(port: port) else {
            stop()
            throw HEVForwarderError.serverNotReady
        }

        lock.withLock { current.state = "running" }
        tunnelQueue.async { [weak self] in
            let result = tunnelConfiguration.withUnsafeBytes { bytes in
                hev_socks5_tunnel_main_from_str(bytes.bindMemory(to: UInt8.self).baseAddress,
                                                UInt32(bytes.count), fd)
            }
            self?.recordExit(component: "tun2socks", result: result)
        }
        logger.info("forwarder running: socks=127.0.0.1:\(port) mtu=1280")
    }

    func stop() {
        let wasRunning = lock.withLock { () -> Bool in
            let value = running
            running = false
            current.state = "stopped"
            return value
        }
        guard wasRunning else { return }
        hev_socks5_tunnel_quit()
        hev_socks5_server_quit()
        tunnelQueue.sync {}
        serverQueue.sync {}
        logger.info("forwarder stopped")
    }

    func snapshot() -> HEVSnapshot {
        var txPackets = 0
        var txBytes = 0
        var rxPackets = 0
        var rxBytes = 0
        if lock.withLock({ running }) {
            hev_socks5_tunnel_stats(&txPackets, &txBytes, &rxPackets, &rxBytes)
        }
        return lock.withLock {
            current.txPackets = UInt64(txPackets)
            current.txBytes = UInt64(txBytes)
            current.rxPackets = UInt64(rxPackets)
            current.rxBytes = UInt64(rxBytes)
            return current
        }
    }

    private func recordExit(component: String, result: Int32) {
        lock.withLock {
            guard running else { return }
            current.state = "error"
            current.lastError = "\(component) exited with \(result)"
        }
        logger.error("\(component, privacy: .public) exited: \(result)")
    }

    private func reserveLoopbackPort() throws -> UInt16 {
        let descriptor = socket(AF_INET, SOCK_STREAM, 0)
        guard descriptor >= 0 else { throw HEVForwarderError.socket(errno) }
        defer { Darwin.close(descriptor) }
        var address = sockaddr_in()
        address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        address.sin_family = sa_family_t(AF_INET)
        address.sin_addr.s_addr = inet_addr("127.0.0.1")
        let bindResult = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.bind(descriptor, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bindResult == 0 else { throw HEVForwarderError.socket(errno) }
        var length = socklen_t(MemoryLayout<sockaddr_in>.size)
        let nameResult = withUnsafeMutablePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                getsockname(descriptor, $0, &length)
            }
        }
        guard nameResult == 0 else { throw HEVForwarderError.socket(errno) }
        return UInt16(bigEndian: address.sin_port)
    }

    private func waitForServer(port: UInt16) -> Bool {
        let deadline = Date().addingTimeInterval(2)
        while Date() < deadline {
            let descriptor = socket(AF_INET, SOCK_STREAM, 0)
            if descriptor >= 0 {
                var address = sockaddr_in()
                address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
                address.sin_family = sa_family_t(AF_INET)
                address.sin_port = port.bigEndian
                address.sin_addr.s_addr = inet_addr("127.0.0.1")
                let result = withUnsafePointer(to: &address) {
                    $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                        Darwin.connect(descriptor, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
                    }
                }
                Darwin.close(descriptor)
                if result == 0 { return true }
            }
            usleep(20_000)
        }
        return false
    }
}
