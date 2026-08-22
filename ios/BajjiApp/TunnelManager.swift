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

    var status = "尚未安装"
    var detail = "安装 VPN 配置后即可启动备用链路。"
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
            self.state = .connecting
            self.status = self.statusText(self.state)
            self.detail = "系统提示时完成配对；StopWatch 随后会使用 iPhone 上行链路。"
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

    func readDeviceSettings() async throws -> DeviceSettings {
        let response = try await sendFrame(type: .settingsGet, payload: Data())
        let bytes = [UInt8](response.payload)
        guard bytes.count == 6, bytes[1] == 1,
              let mode = DeviceDisplayMode(rawValue: bytes[3]) else {
            throw TunnelError.invalidResponse
        }
        try validateStatus(bytes[0])
        return DeviceSettings(
            brightnessPercent: bytes[2],
            displayMode: mode,
            autoRefreshMinutes: UInt16(bytes[4]) << 8 | UInt16(bytes[5])
        )
    }

    func applyDeviceSettings(_ settings: DeviceSettings) async throws -> DeviceSettings {
        let minutes = settings.autoRefreshMinutes
        let payload = Data([
            1, settings.brightnessPercent, settings.displayMode.rawValue,
            UInt8(truncatingIfNeeded: minutes >> 8), UInt8(truncatingIfNeeded: minutes)
        ])
        let response = try await sendFrame(type: .settingsSet, payload: payload)
        let bytes = [UInt8](response.payload)
        guard bytes.count == 6, bytes[1] == 1,
              let mode = DeviceDisplayMode(rawValue: bytes[3]) else {
            throw TunnelError.invalidResponse
        }
        try validateStatus(bytes[0])
        return DeviceSettings(
            brightnessPercent: bytes[2],
            displayMode: mode,
            autoRefreshMinutes: UInt16(bytes[4]) << 8 | UInt16(bytes[5])
        )
    }

    func sendWallpaper(_ data: Data,
                       progress: (WallpaperTransferStage, Double) -> Void) async throws {
        guard !data.isEmpty, data.count <= 3 * 1024 * 1024 else {
            throw TunnelError.invalidWallpaper
        }
        let size = UInt32(data.count)
        let checksum = BridgeFrame.crc32(data)
        let begin = Data([
            1, 1,
            UInt8(truncatingIfNeeded: size >> 24), UInt8(truncatingIfNeeded: size >> 16),
            UInt8(truncatingIfNeeded: size >> 8), UInt8(truncatingIfNeeded: size),
            UInt8(truncatingIfNeeded: checksum >> 24), UInt8(truncatingIfNeeded: checksum >> 16),
            UInt8(truncatingIfNeeded: checksum >> 8), UInt8(truncatingIfNeeded: checksum)
        ])
        var began = false
        do {
            try? await sendWallpaperFrame(
                type: .wallpaperCancel,
                payload: Data(),
                expectedOffset: 0
            )
            try await sendWallpaperFrame(type: .wallpaperBegin, payload: begin, expectedOffset: 0)
            began = true
            progress(.sending, 0)
            var offset = 0
            while offset < data.count {
                try Task.checkCancellation()
                let end = min(offset + 1024, data.count)
                let position = UInt32(offset)
                var payload = Data([
                    UInt8(truncatingIfNeeded: position >> 24),
                    UInt8(truncatingIfNeeded: position >> 16),
                    UInt8(truncatingIfNeeded: position >> 8), UInt8(truncatingIfNeeded: position)
                ])
                payload.append(data[offset..<end])
                try await sendWallpaperFrame(
                    type: .wallpaperChunk,
                    payload: payload,
                    expectedOffset: UInt32(end)
                )
                offset = end
                progress(.sending, Double(offset) / Double(data.count))
            }
            progress(.validating, 1)
            try await sendWallpaperFrame(
                type: .wallpaperCommit,
                payload: Data(),
                expectedOffset: size
            )
            progress(.complete, 1)
        } catch {
            if began {
                try? await sendWallpaperFrame(
                    type: .wallpaperCancel,
                    payload: Data(),
                    expectedOffset: 0
                )
            }
            throw error
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

    private func sendFrame(type: BridgeFrame.Kind, payload: Data) async throws -> BridgeFrame {
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

    private func sendWallpaperFrame(type: BridgeFrame.Kind, payload: Data,
                                    expectedOffset: UInt32) async throws {
        let response = try await sendFrame(type: type, payload: payload)
        let bytes = [UInt8](response.payload)
        guard bytes.count == 6, bytes[0] == type.rawValue else {
            throw TunnelError.invalidResponse
        }
        try validateStatus(bytes[1])
        let accepted = UInt32(bytes[2]) << 24 | UInt32(bytes[3]) << 16 |
            UInt32(bytes[4]) << 8 | UInt32(bytes[5])
        guard accepted == expectedOffset else { throw TunnelError.invalidResponse }
    }

    private func validateStatus(_ status: UInt8) throws {
        guard status == 0 else { throw TunnelError.deviceStatus(status) }
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
}

private enum TunnelError: LocalizedError {
    case notInstalled
    case notConnected
    case invalidResponse
    case invalidWallpaper
    case bridgeError(UInt16)
    case deviceStatus(UInt8)

    var errorDescription: String? {
        switch self {
        case .notInstalled: "请先安装 Bajji VPN 配置。"
        case .notConnected: "请先启动 VPN 控制连接，并等待蓝牙链路就绪。"
        case .invalidResponse: "StopWatch 返回了无法识别的响应。"
        case .invalidWallpaper: "壁纸文件为空或超过 3 MB。"
        case let .bridgeError(code):
            code == 1 ? "StopWatch 控制链路尚未就绪或固件不支持此功能。" : "控制扩展拒绝了请求（\(code)）。"
        case let .deviceStatus(status):
            switch status {
            case 1: "StopWatch 当前状态不允许此操作。"
            case 2: "发送给 StopWatch 的参数无效。"
            case 3: "StopWatch 无法写入本地存储。"
            case 4: "壁纸校验失败，请重新发送。"
            case 5: "StopWatch 不支持这张图片的格式或尺寸。"
            case 6: "StopWatch 正忙，请稍后重试。"
            default: "StopWatch 返回错误状态（\(status)）。"
            }
        }
    }
}
