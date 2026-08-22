// SPDX-License-Identifier: MIT
import Foundation
import NetworkExtension
import Observation

struct TunnelDiagnostics: Decodable {
    let internet: String
    let bluetooth: BluetoothDiagnostics
    let forwarder: IPForwarderDiagnostics
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

enum TunnelState {
    case notInstalled
    case installed
    case connecting
    case connected
    case reconnecting
    case disconnecting
    case invalid
    case unknown
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
    var isBusy = false
    var state: TunnelState = .notInstalled

    func refresh() async {
        do {
            manager = try await loadManager()
            state = manager.map { tunnelState($0.connection.status) } ?? .notInstalled
            status = statusText(state)
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
            self.state = .installed
            self.status = self.statusText(self.state)
            self.detail = "VPN profile is ready."
        }
    }

    func start() async {
        await perform {
            guard let manager = try await self.loadManager() else {
                throw TunnelError.notInstalled
            }
            self.manager = manager
            try manager.connection.startVPNTunnel()
            self.state = .connecting
            self.status = self.statusText(self.state)
            self.detail = "Pair when prompted; the StopWatch will use the iPhone uplink."
        }
    }

    func stop() {
        manager?.connection.stopVPNTunnel()
        state = .disconnecting
        status = statusText(state)
    }

    func readSnapshot() async {
        guard let session = manager?.connection as? NETunnelProviderSession,
              session.status == .connected else {
            state = manager.map { tunnelState($0.connection.status) } ?? .notInstalled
            status = statusText(state)
            diagnostics = nil
            return
        }
        do {
            let data = try await send("snapshot", through: session)
            diagnostics = try JSONDecoder().decode(TunnelDiagnostics.self, from: data)
            state = tunnelState(session.status)
            status = statusText(state)
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

    private func tunnelState(_ value: NEVPNStatus) -> TunnelState {
        switch value {
        case .invalid: .invalid
        case .disconnected: .installed
        case .connecting: .connecting
        case .connected: .connected
        case .reasserting: .reconnecting
        case .disconnecting: .disconnecting
        @unknown default: .unknown
        }
    }

    private func statusText(_ value: TunnelState) -> String {
        switch value {
        case .notInstalled: "Not installed"
        case .installed: "Ready"
        case .connecting: "Connecting"
        case .connected: "Connected"
        case .reconnecting: "Reconnecting"
        case .disconnecting: "Disconnecting"
        case .invalid: "Invalid"
        case .unknown: "Unknown"
        }
    }
}

private enum TunnelError: LocalizedError {
    case notInstalled

    var errorDescription: String? { "Install the Bajji VPN profile first." }
}
