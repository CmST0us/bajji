// SPDX-License-Identifier: MIT
import Foundation
import NetworkExtension
import Observation

struct TunnelDiagnostics: Decodable {
    let internet: String
    let bluetooth: BluetoothDiagnostics
    let forwarder: IPForwarderDiagnostics
    let phaseZero: PhaseZeroDiagnostics
}

struct IPForwarderDiagnostics: Decodable {
    let state: String
    let fromDevicePackets: UInt64
    let fromDeviceBytes: UInt64
    let toDevicePackets: UInt64
    let toDeviceBytes: UInt64
    let droppedPackets: UInt64
    let invalidPackets: UInt64
    let lastError: String
    let hev: HEVDiagnostics
}

struct HEVDiagnostics: Decodable {
    let state: String
    let txPackets: UInt64
    let txBytes: UInt64
    let rxPackets: UInt64
    let rxBytes: UInt64
    let lastError: String
}

struct BluetoothDiagnostics: Decodable {
    let state: String
    let deviceID: String
    let psm: UInt16
    let maximumPayload: UInt16
    let rssi: Int
    let receivedBytes: UInt64
    let sentBytes: UInt64
    let reconnects: Int
    let queueOverflows: Int
    let lastError: String
}

struct PhaseZeroDiagnostics: Decodable {
    let running: Bool
    let sentPayloadBytes: UInt64
    let echoedPayloadBytes: UInt64
    let elapsedSeconds: Double
    let echoedKilobytesPerSecond: Double
    let inFlightFrames: Int
}

@MainActor
@Observable
final class TunnelManager {
    private static let providerBundleIdentifier = Bundle.main.object(
        forInfoDictionaryKey: "PacketTunnelProviderBundleIdentifier"
    ) as! String
    private var manager: NETunnelProviderManager?

    var status = "Not installed"
    var detail = "Install the VPN profile, then start the bridge."
    var diagnostics: TunnelDiagnostics?
    var phaseZeroOnStart = false
    var isBusy = false

    func refresh() async {
        do {
            manager = try await loadManager()
            status = manager.map { statusText($0.connection.status) } ?? "Not installed"
        } catch {
            detail = error.localizedDescription
        }
    }

    func install() async {
        await perform {
            let manager = try await self.loadManager() ?? NETunnelProviderManager()
            let proto = NETunnelProviderProtocol()
            proto.providerBundleIdentifier = Self.providerBundleIdentifier
            proto.serverAddress = "StopWatch BLE"
            manager.protocolConfiguration = proto
            manager.localizedDescription = "Bajji StopWatch Bridge"
            manager.isEnabled = true
            try await manager.saveToPreferences()
            try await manager.loadFromPreferences()
            self.manager = manager
            self.status = "Installed"
            self.detail = "VPN profile is ready."
        }
    }

    func start() async {
        await perform {
            guard let manager = try await self.loadManager() else {
                throw TunnelError.notInstalled
            }
            self.manager = manager
            try manager.connection.startVPNTunnel(options: [
                "phaseZero": NSNumber(value: self.phaseZeroOnStart)
            ])
            self.status = "Connecting"
            self.detail = self.phaseZeroOnStart
                ? "Pair when prompted; Debug Echo starts after L2CAP is ready."
                : "Pair when prompted; the StopWatch will use the iPhone uplink."
        }
    }

    func stop() {
        manager?.connection.stopVPNTunnel()
        status = "Disconnecting"
    }

    func readSnapshot() async {
        guard let session = manager?.connection as? NETunnelProviderSession,
              session.status == .connected else {
            status = manager.map { statusText($0.connection.status) } ?? "Not installed"
            diagnostics = nil
            return
        }
        do {
            let data = try await send("snapshot", through: session)
            diagnostics = try JSONDecoder().decode(TunnelDiagnostics.self, from: data)
            status = statusText(session.status)
        } catch {
            detail = error.localizedDescription
        }
    }

    func applyPhaseZeroSetting() async {
        guard let session = manager?.connection as? NETunnelProviderSession,
              session.status == .connected else { return }
        do {
            _ = try await send(phaseZeroOnStart ? "debug:echo:start" : "debug:echo:stop",
                               through: session)
            detail = phaseZeroOnStart ? "Debug echo requested." : "Production forwarding enabled."
        } catch {
            detail = error.localizedDescription
        }
    }

    func clearBinding() async {
        guard let session = manager?.connection as? NETunnelProviderSession,
              session.status == .connected else {
            detail = "Start the tunnel before clearing its saved Device ID."
            return
        }
        do {
            _ = try await send("binding:clear", through: session)
            detail = "Pairing reset requested. Re-pair when prompted; iOS Settings is not required."
        } catch {
            detail = error.localizedDescription
        }
    }

    private func perform(_ operation: () async throws -> Void) async {
        guard !isBusy else { return }
        isBusy = true
        defer { isBusy = false }
        do {
            try await operation()
        } catch {
            detail = error.localizedDescription
        }
    }

    private func loadManager() async throws -> NETunnelProviderManager? {
        let managers = try await NETunnelProviderManager.loadAllFromPreferences()
        return managers.first {
            ($0.protocolConfiguration as? NETunnelProviderProtocol)?.providerBundleIdentifier == Self.providerBundleIdentifier
        }
    }

    private func send(_ command: String, through session: NETunnelProviderSession) async throws -> Data {
        try await withCheckedThrowingContinuation { continuation in
            do {
                try session.sendProviderMessage(Data(command.utf8)) { data in
                    continuation.resume(returning: data ?? Data())
                }
            } catch {
                continuation.resume(throwing: error)
            }
        }
    }

    private func statusText(_ value: NEVPNStatus) -> String {
        switch value {
        case .invalid: "Invalid"
        case .disconnected: "Disconnected"
        case .connecting: "Connecting"
        case .connected: "Connected"
        case .reasserting: "Reconnecting"
        case .disconnecting: "Disconnecting"
        @unknown default: "Unknown"
        }
    }
}

private enum TunnelError: LocalizedError {
    case notInstalled

    var errorDescription: String? { "Install the Bajji VPN profile first." }
}
