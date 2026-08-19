// SPDX-License-Identifier: MIT
import Foundation
import Network
@preconcurrency import NetworkExtension
import OSLog

final class PacketTunnelProvider: NEPacketTunnelProvider, @unchecked Sendable {
    private let logger = Logger(subsystem: "com.eric3u.bajji", category: "PacketTunnel")
    private let phaseZero = PhaseZeroRunner()
    private let stateLock = NSLock()
    private let forwarderQueue = DispatchQueue(label: "com.eric3u.bajji.forwarder.lifecycle")
    private let pathQueue = DispatchQueue(label: "com.eric3u.bajji.path")
    private let pathMonitor = NWPathMonitor()
    private var bridge: BluetoothBridge?
    private var forwarder: IPForwarder?
    private var forwarderFallback = IPForwarderSnapshot()
    private var phaseZeroRequested = false
    private var stopping = false
    private var internetState = "checking"
    private var pathSignature: String?

    override func startTunnel(options: [String: NSObject]?,
                              completionHandler: @escaping (Error?) -> Void) {
        let completion = TunnelStartCompletion(completionHandler)
        stateLock.withLock {
            phaseZeroRequested = (options?["phaseZero"] as? NSNumber)?.boolValue ?? false
            stopping = false
        }
        logger.info("starting tunnel: debug_echo=\(self.debugEchoRequested())")

        let settings = NEPacketTunnelNetworkSettings(tunnelRemoteAddress: "10.77.0.1")
        let ipv4 = NEIPv4Settings(addresses: ["10.77.0.1"], subnetMasks: ["255.255.255.252"])
        ipv4.includedRoutes = [NEIPv4Route(destinationAddress: "10.77.0.0",
                                            subnetMask: "255.255.255.252")]
        ipv4.excludedRoutes = [.default()]
        settings.ipv4Settings = ipv4
        settings.mtu = 1280

        setTunnelNetworkSettings(settings) { [weak self] error in
            guard let self, error == nil else {
                if let error {
                    self?.logger.error("network settings failed: \(error.localizedDescription, privacy: .public)")
                }
                completion.call(error)
                return
            }
            let bridge = BluetoothBridge()
            self.bridge = bridge
            self.configure(bridge)
            do {
                if !self.debugEchoRequested() { try self.replaceForwarder(for: bridge) }
            } catch {
                self.logger.error("forwarder start failed: \(error.localizedDescription, privacy: .public)")
                completion.call(error)
                return
            }
            self.startPathMonitor(for: bridge)
            bridge.start()
            self.logger.info("network settings applied; Bluetooth bridge starting")
            completion.call(nil)
        }
    }

    override func stopTunnel(with reason: NEProviderStopReason,
                             completionHandler: @escaping () -> Void) {
        logger.info("stopping tunnel: reason=\(reason.rawValue)")
        stateLock.withLock {
            stopping = true
            phaseZeroRequested = false
        }
        pathMonitor.cancel()
        phaseZero.stop()
        bridge?.stop()
        bridge = nil
        forwarderQueue.sync { self.stopForwarder() }
        completionHandler()
    }

    override func handleAppMessage(_ messageData: Data,
                                   completionHandler: ((Data?) -> Void)? = nil) {
        switch String(data: messageData, encoding: .utf8) {
        case "phase0:start", "debug:echo:start":
            setDebugEcho(true)
            completionHandler?(Data("ok".utf8))
        case "phase0:stop", "debug:echo:stop":
            setDebugEcho(false)
            completionHandler?(Data("ok".utf8))
        case "binding:clear":
            bridge?.clearBinding()
            completionHandler?(Data("ok".utf8))
        default:
            let snapshot = TunnelSnapshot(
                internet: stateLock.withLock { internetState },
                bluetooth: bridge?.snapshot() ?? BluetoothSnapshot(),
                forwarder: forwarderSnapshot(),
                phaseZero: phaseZero.snapshot()
            )
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            completionHandler?(try? encoder.encode(snapshot))
        }
    }

    private func configure(_ bridge: BluetoothBridge) {
        bridge.onFrame = { [weak self] frame in
            guard let self else { return }
            if debugEchoRequested() {
                phaseZero.receive(frame)
            } else if frame.type == .ipv4 {
                activeForwarder()?.receiveFromDevice(frame)
            }
        }
        bridge.onReady = { [weak self, weak bridge] in
            guard let self, let bridge else { return }
            if debugEchoRequested() {
                phaseZero.start { [weak bridge] frame in bridge?.send(frame) }
            } else if activeForwarder() == nil {
                rebuildForwarder(for: bridge)
            }
        }
        bridge.onUnavailable = { [weak self, weak bridge] in
            guard let self else { return }
            phaseZero.stop()
            guard let bridge, !debugEchoRequested() else { return }
            rebuildForwarder(for: bridge)
        }
    }

    private func setDebugEcho(_ enabled: Bool) {
        stateLock.withLock { phaseZeroRequested = enabled }
        guard let bridge else { return }
        if enabled {
            forwarderQueue.async { [weak self] in self?.stopForwarder() }
            bridge.whenReady { [weak self, weak bridge] in
                guard let self, let bridge, debugEchoRequested() else { return }
                phaseZero.start { [weak bridge] frame in bridge?.send(frame) }
            }
        } else {
            phaseZero.stop()
            rebuildForwarder(for: bridge)
        }
    }

    private func startPathMonitor(for bridge: BluetoothBridge) {
        pathMonitor.pathUpdateHandler = { [weak self, weak bridge] path in
            guard let self else { return }
            let signature = self.signature(for: path)
            let previous = self.stateLock.withLock { () -> String? in
                let previous = self.pathSignature
                self.pathSignature = signature
                self.internetState = path.status == .satisfied ? "online" : "offline"
                return previous
            }
            if previous != nil, previous != signature, path.status == .satisfied,
               let bridge, !self.debugEchoRequested() {
                self.logger.info("upstream path changed; restarting forwarding sessions")
                self.rebuildForwarder(for: bridge)
            }
        }
        pathMonitor.start(queue: pathQueue)
    }

    private func signature(for path: NWPath) -> String {
        let status: String = switch path.status {
        case .satisfied: "up"
        case .unsatisfied: "down"
        case .requiresConnection: "waiting"
        @unknown default: "unknown"
        }
        return "\(status)-wifi:\(path.usesInterfaceType(.wifi))-cell:\(path.usesInterfaceType(.cellular))-wired:\(path.usesInterfaceType(.wiredEthernet))"
    }

    private func rebuildForwarder(for bridge: BluetoothBridge) {
        forwarderQueue.async { [weak self, weak bridge] in
            guard let self, let bridge, !debugEchoRequested(), !isStopping() else { return }
            do {
                try replaceForwarder(for: bridge)
            } catch {
                recordForwarderFailure(error)
            }
        }
    }

    private func replaceForwarder(for bridge: BluetoothBridge) throws {
        stopForwarder()
        let next = IPForwarder()
        try next.start { [weak bridge] frame in bridge?.send(frame) }
        if isStopping() || debugEchoRequested() {
            next.stop()
            return
        }
        stateLock.withLock {
            forwarder = next
            forwarderFallback = IPForwarderSnapshot(state: "running")
        }
    }

    private func stopForwarder() {
        let previous = stateLock.withLock { () -> IPForwarder? in
            defer { forwarder = nil }
            return forwarder
        }
        previous?.stop()
    }

    private func recordForwarderFailure(_ error: Error) {
        stateLock.withLock {
            forwarderFallback.state = "error"
            forwarderFallback.lastError = error.localizedDescription
        }
        logger.error("forwarder restart failed: \(error.localizedDescription, privacy: .public)")
    }

    private func activeForwarder() -> IPForwarder? { stateLock.withLock { forwarder } }

    private func forwarderSnapshot() -> IPForwarderSnapshot {
        let value = stateLock.withLock { (forwarder, forwarderFallback) }
        return value.0?.snapshot() ?? value.1
    }

    private func debugEchoRequested() -> Bool { stateLock.withLock { phaseZeroRequested } }
    private func isStopping() -> Bool { stateLock.withLock { stopping } }
}

private final class TunnelStartCompletion: @unchecked Sendable {
    let call: (Error?) -> Void

    init(_ call: @escaping (Error?) -> Void) {
        self.call = call
    }
}

private struct TunnelSnapshot: Codable {
    let internet: String
    let bluetooth: BluetoothSnapshot
    let forwarder: IPForwarderSnapshot
    let phaseZero: PhaseZeroSnapshot
}
