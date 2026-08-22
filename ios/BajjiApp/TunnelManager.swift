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
    let capabilities: UInt8
    let rssi: Int
    let receivedBytes: UInt64
    let sentBytes: UInt64
    let reconnects: Int
    let queueOverflows: Int
    let lastError: String
}

enum DeviceDisplayMode: UInt8, Sendable {
    case cover = 0
    case fitBlur = 1
}

struct DeviceSettings: Equatable, Sendable {
    let brightnessPercent: UInt8
    let displayMode: DeviceDisplayMode
    let autoRefreshMinutes: UInt16
}

enum WallpaperTransferStage: Equatable {
    case sending
    case validating
    case complete
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
    @ObservationIgnored nonisolated(unsafe) private var statusObserver: NSObjectProtocol?

    var status = "尚未安装"
    var detail = "安装 VPN 配置后即可启动备用链路。"
    var diagnostics: TunnelDiagnostics?
    var isBusy = false
    var state: TunnelState = .notInstalled
    var onStateChange: ((TunnelState) -> Void)?

    init() {
        statusObserver = NotificationCenter.default.addObserver(
            forName: .NEVPNStatusDidChange, object: nil, queue: .main
        ) { [weak self] _ in
            Task { @MainActor [weak self] in await self?.refresh() }
        }
    }

    deinit {
        if let statusObserver { NotificationCenter.default.removeObserver(statusObserver) }
    }

    func refresh() async {
        do {
            manager = try await loadManager()
            updateState(manager.map { tunnelState($0.connection.status) } ?? .notInstalled)
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
            self.updateState(.installed)
            self.detail = "VPN 配置已就绪。"
        }
    }

    func start() async {
        await perform {
            guard let manager = try await self.loadManager() else {
                throw TunnelError.notInstalled
            }
            self.manager = manager
            try manager.connection.startVPNTunnel()
            self.updateState(.connecting)
            self.detail = "系统提示时完成配对；StopWatch 随后会使用 iPhone 上行链路。"
        }
    }

    func stop() {
        manager?.connection.stopVPNTunnel()
        updateState(.disconnecting)
    }

    func setOnDemand(_ enabled: Bool) async throws {
        guard let manager = try await loadManager() else { throw TunnelError.notInstalled }
        manager.onDemandRules = enabled ? [NEOnDemandRuleConnect()] : []
        manager.isOnDemandEnabled = enabled
        try await manager.saveToPreferences()
        try await manager.loadFromPreferences()
        self.manager = manager
        updateState(tunnelState(manager.connection.status))
    }

    func readSnapshot() async {
        guard let session = manager?.connection as? NETunnelProviderSession,
              session.status == .connected else {
            updateState(manager.map { tunnelState($0.connection.status) } ?? .notInstalled)
            diagnostics = nil
            return
        }
        do {
            let data = try await send("snapshot", through: session)
            diagnostics = try JSONDecoder().decode(TunnelDiagnostics.self, from: data)
            updateState(tunnelState(session.status))
        } catch {
            detail = error.localizedDescription
        }
    }

    func clearBinding() async {
        guard let session = manager?.connection as? NETunnelProviderSession,
              session.status == .connected else {
            detail = "请先启动 VPN，再清除已保存的设备标识。"
            return
        }
        do {
            _ = try await send("binding:clear", through: session)
            detail = "已请求重置配对；按系统提示重新配对即可，无需前往 iOS 设置。"
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

    func sendFrame(type: BridgeFrame.Kind, payload: Data) async throws -> BridgeFrame {
        guard let expected = type.controlResponse else { throw TunnelError.invalidResponse }
        let session = try await connectedSession()
        let request = BridgeFrame(type: type, sequence: 0, payload: payload)
        let response = try BridgeFrame.decode(try await send(try request.encode(), through: session))
        if response.type == .error {
            let bytes = [UInt8](response.payload)
            let code = bytes.count == 2 ? UInt16(bytes[0]) << 8 | UInt16(bytes[1]) : 0
            throw TunnelError.bridgeError(code)
        }
        guard response.type == expected else { throw TunnelError.invalidResponse }
        return response
    }

    private func connectedSession() async throws -> NETunnelProviderSession {
        if manager == nil { manager = try await loadManager() }
        guard let session = manager?.connection as? NETunnelProviderSession,
              session.status == .connected else {
            throw TunnelError.notConnected
        }
        return session
    }

    private func send(_ command: String, through session: NETunnelProviderSession) async throws -> Data {
        try await send(Data(command.utf8), through: session)
    }

    private func send(_ message: Data, through session: NETunnelProviderSession) async throws -> Data {
        try await withCheckedThrowingContinuation { continuation in
            do {
                try session.sendProviderMessage(message) { data in
                    guard let data, !data.isEmpty else {
                        continuation.resume(throwing: TunnelError.invalidResponse)
                        return
                    }
                    continuation.resume(returning: data)
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
        case .notInstalled: "尚未安装"
        case .installed: "已就绪"
        case .connecting: "正在连接"
        case .connected: "已连接"
        case .reconnecting: "正在重连"
        case .disconnecting: "正在断开"
        case .invalid: "配置无效"
        case .unknown: "未知状态"
        }
    }

    private func updateState(_ value: TunnelState) {
        state = value
        status = statusText(value)
        onStateChange?(value)
    }
}

private enum TunnelError: LocalizedError {
    case notInstalled
    case notConnected
    case invalidResponse
    case bridgeError(UInt16)

    var errorDescription: String? {
        switch self {
        case .notInstalled: "请先安装 Bajji VPN 配置。"
        case .notConnected: "请先启动 VPN 控制连接，并等待蓝牙链路就绪。"
        case .invalidResponse: "StopWatch 返回了无法识别的响应。"
        case let .bridgeError(code):
            code == 1 ? "StopWatch 控制链路尚未就绪或固件不支持此功能。" : "控制扩展拒绝了请求（\(code)）。"
        }
    }
}
