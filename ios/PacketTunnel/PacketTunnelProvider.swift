// SPDX-License-Identifier: MIT
import Foundation
@preconcurrency import NetworkExtension

final class PacketTunnelProvider: NEPacketTunnelProvider, @unchecked Sendable {
    private var bridge: BluetoothBridge?
    private let phaseZero = PhaseZeroRunner()
    private let phaseZeroLock = NSLock()
    private var phaseZeroRequested = false

    override func startTunnel(options: [String: NSObject]?,
                              completionHandler: @escaping (Error?) -> Void) {
        let completion = TunnelStartCompletion(completionHandler)
        setPhaseZeroRequested((options?["phaseZero"] as? NSNumber)?.boolValue ?? false)

        let settings = NEPacketTunnelNetworkSettings(tunnelRemoteAddress: "10.77.0.1")
        let ipv4 = NEIPv4Settings(addresses: ["10.77.0.1"], subnetMasks: ["255.255.255.252"])
        ipv4.includedRoutes = [NEIPv4Route(destinationAddress: "10.77.0.0", subnetMask: "255.255.255.252")]
        ipv4.excludedRoutes = [.default()]
        settings.ipv4Settings = ipv4
        settings.mtu = 1280

        setTunnelNetworkSettings(settings) { [weak self] error in
            guard let self, error == nil else {
                completion.call(error)
                return
            }
            let bridge = BluetoothBridge()
            bridge.onFrame = { [weak self] frame in self?.phaseZero.receive(frame) }
            bridge.onReady = { [weak self, weak bridge] in
                guard let self, phaseZeroIsRequested(), let bridge else { return }
                phaseZero.start { [weak bridge] frame in bridge?.send(frame) }
            }
            bridge.onUnavailable = { [weak self] in self?.phaseZero.stop() }
            self.bridge = bridge
            bridge.start()
            completion.call(nil)
        }
    }

    override func stopTunnel(with reason: NEProviderStopReason,
                             completionHandler: @escaping () -> Void) {
        setPhaseZeroRequested(false)
        phaseZero.stop()
        bridge?.stop()
        bridge = nil
        completionHandler()
    }

    override func handleAppMessage(_ messageData: Data,
                                   completionHandler: ((Data?) -> Void)? = nil) {
        switch String(data: messageData, encoding: .utf8) {
        case "phase0:start":
            setPhaseZeroRequested(true)
            startPhaseZeroIfReady()
            completionHandler?(Data("ok".utf8))
        case "phase0:stop":
            setPhaseZeroRequested(false)
            phaseZero.stop()
            completionHandler?(Data("ok".utf8))
        case "binding:clear":
            bridge?.clearBinding()
            completionHandler?(Data("ok".utf8))
        default:
            let snapshot = TunnelSnapshot(
                bluetooth: bridge?.snapshot() ?? BluetoothSnapshot(),
                phaseZero: phaseZero.snapshot()
            )
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            completionHandler?(try? encoder.encode(snapshot))
        }
    }

    private func startPhaseZeroIfReady() {
        guard let bridge else { return }
        bridge.whenReady { [weak self, weak bridge] in
            guard let self, phaseZeroIsRequested(), let bridge else { return }
            phaseZero.start { [weak bridge] frame in bridge?.send(frame) }
        }
    }

    private func setPhaseZeroRequested(_ requested: Bool) {
        phaseZeroLock.withLock { phaseZeroRequested = requested }
    }

    private func phaseZeroIsRequested() -> Bool {
        phaseZeroLock.withLock { phaseZeroRequested }
    }
}

private final class TunnelStartCompletion: @unchecked Sendable {
    let call: (Error?) -> Void

    init(_ call: @escaping (Error?) -> Void) {
        self.call = call
    }
}

private struct TunnelSnapshot: Codable {
    let bluetooth: BluetoothSnapshot
    let phaseZero: PhaseZeroSnapshot
}
