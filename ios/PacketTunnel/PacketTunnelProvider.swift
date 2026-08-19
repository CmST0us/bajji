// SPDX-License-Identifier: MIT
import Foundation
@preconcurrency import NetworkExtension

final class PacketTunnelProvider: NEPacketTunnelProvider, @unchecked Sendable {
    private var bridge: BluetoothBridge?
    private let phaseZero = PhaseZeroRunner()
    private var phaseZeroRequested = false

    override func startTunnel(options: [String: NSObject]?,
                              completionHandler: @escaping (Error?) -> Void) {
        let completion = TunnelStartCompletion(completionHandler)
        phaseZeroRequested = (options?["phaseZero"] as? NSNumber)?.boolValue ?? false

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
                guard let self, phaseZeroRequested, let bridge else { return }
                phaseZero.start { [weak bridge] frame in bridge?.send(frame) }
            }
            self.bridge = bridge
            bridge.start()
            completion.call(nil)
        }
    }

    override func stopTunnel(with reason: NEProviderStopReason,
                             completionHandler: @escaping () -> Void) {
        phaseZero.stop()
        bridge?.stop()
        bridge = nil
        completionHandler()
    }

    override func handleAppMessage(_ messageData: Data,
                                   completionHandler: ((Data?) -> Void)? = nil) {
        switch String(data: messageData, encoding: .utf8) {
        case "phase0:start":
            if let bridge { phaseZero.start { [weak bridge] frame in bridge?.send(frame) } }
            completionHandler?(Data("ok".utf8))
        case "phase0:stop":
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
