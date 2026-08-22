// SPDX-License-Identifier: MIT
import Foundation
import Observation

enum DeviceConnectionState {
    case unbound
    case connecting
    case connected
    case unreachable
    case upgradeRequired
}

@MainActor
@Observable
final class DeviceConnectionManager {
    private let tunnel: TunnelManager
    private var bridge: BluetoothBridge?
    private var identifier: UUID?
    private var monitorTask: Task<Void, Never>?

    var state: DeviceConnectionState = .unbound
    var status = "尚未绑定"
    var detail = "添加 StopWatch 后会自动建立蓝牙控制连接。"
    var bluetooth: BluetoothSnapshot?
    var network: DeviceNetworkState?
    var isBusy = false

    var isReady: Bool { state == .connected }
    var capabilities: UInt8 { bluetooth?.capabilities ?? 0 }

    init(tunnel: TunnelManager) {
        self.tunnel = tunnel
        tunnel.onStateChange = { [weak self] _ in self?.routeForCurrentMode() }
    }

    func start(identifier: UUID?) async {
        if self.identifier != identifier { updateAccessory(identifier: identifier) }
        await tunnel.refresh()
        if tunnel.state == .connected || tunnel.state == .connecting ||
            tunnel.state == .reconnecting {
            await refresh()
        } else {
            await disableVPN()
            connectControl()
        }
        if monitorTask == nil {
            monitorTask = Task { [weak self] in
                while !Task.isCancelled {
                    await self?.refresh()
                    try? await Task.sleep(for: .seconds(1))
                }
            }
        }
    }

    func updateAccessory(identifier: UUID?) {
        guard self.identifier != identifier else { return }
        self.identifier = identifier
        bridge?.stop()
        bridge = nil
        bluetooth = nil
        network = nil
        if identifier == nil {
            state = .unbound
            status = "尚未绑定"
            detail = "请重新添加 StopWatch。"
        } else {
            routeForCurrentMode()
        }
    }

    func refresh() async {
        guard identifier != nil else { return }
        if tunnel.state == .connected || tunnel.state == .reconnecting {
            await tunnel.readSnapshot()
            bluetooth = tunnel.diagnostics?.bluetooth.snapshot
            let ready = tunnel.diagnostics?.bluetooth.state == "L2CAP ready"
            state = ready ? .connected : .connecting
            status = ready ? "已连接" : "自动连接中"
            if ready && network == nil {
                do {
                    let current = try await readNetworkState()
                    if current.mode != .vpn { await disableVPN() }
                } catch {
                    detail = error.localizedDescription
                }
            }
        } else if let bridge {
            update(from: bridge.snapshot())
        }
    }

    func readNetworkState() async throws -> DeviceNetworkState {
        let response = try await request(type: .networkGet, payload: Data())
        let value = try parseNetwork(response)
        network = value
        save(mode: value.mode)
        return value
    }

    @discardableResult
    func selectNetwork(_ mode: DeviceNetworkMode, ssid: String = "",
                       password: String = "") async throws -> DeviceNetworkState {
        guard mode != .unset else { throw DeviceControlError.invalidResponse }
        isBusy = true
        defer { isBusy = false }
        let payload = try NetworkControlPayload.set(mode: mode, ssid: ssid, password: password)
        let value = try parseNetwork(try await request(type: .networkSet, payload: payload))
        network = value
        save(mode: mode)
        if mode == .vpn {
            bridge?.stop()
            bridge = nil
            await activateVPN()
        } else {
            await disableVPN()
            connectControl()
        }
        return value
    }

    func readDeviceSettings() async throws -> DeviceSettings {
        let bytes = [UInt8](try await request(type: .settingsGet, payload: Data()).payload)
        guard bytes.count == 6, bytes[1] == 1,
              let mode = DeviceDisplayMode(rawValue: bytes[3]) else {
            throw DeviceControlError.invalidResponse
        }
        try validateStatus(bytes[0])
        return DeviceSettings(
            brightnessPercent: bytes[2], displayMode: mode,
            autoRefreshMinutes: UInt16(bytes[4]) << 8 | UInt16(bytes[5])
        )
    }

    func applyDeviceSettings(_ settings: DeviceSettings) async throws -> DeviceSettings {
        let minutes = settings.autoRefreshMinutes
        let payload = Data([
            1, settings.brightnessPercent, settings.displayMode.rawValue,
            UInt8(truncatingIfNeeded: minutes >> 8), UInt8(truncatingIfNeeded: minutes)
        ])
        let bytes = [UInt8](try await request(type: .settingsSet, payload: payload).payload)
        guard bytes.count == 6, bytes[1] == 1,
              let mode = DeviceDisplayMode(rawValue: bytes[3]) else {
            throw DeviceControlError.invalidResponse
        }
        try validateStatus(bytes[0])
        return DeviceSettings(
            brightnessPercent: bytes[2], displayMode: mode,
            autoRefreshMinutes: UInt16(bytes[4]) << 8 | UInt16(bytes[5])
        )
    }

    func sendWallpaper(_ data: Data,
                       progress: (WallpaperTransferStage, Double) -> Void) async throws {
        guard !data.isEmpty, data.count <= 3 * 1024 * 1024 else {
            throw DeviceControlError.invalidWallpaper
        }
        let format: UInt8
        if data.starts(with: [0x89, 0x50, 0x4E, 0x47]) {
            format = 2
        } else if data.starts(with: [0xFF, 0xD8]) {
            format = 1
        } else {
            throw DeviceControlError.invalidWallpaper
        }
        let size = UInt32(data.count)
        let checksum = BridgeFrame.crc32(data)
        let begin = Data([
            1, format,
            UInt8(truncatingIfNeeded: size >> 24), UInt8(truncatingIfNeeded: size >> 16),
            UInt8(truncatingIfNeeded: size >> 8), UInt8(truncatingIfNeeded: size),
            UInt8(truncatingIfNeeded: checksum >> 24), UInt8(truncatingIfNeeded: checksum >> 16),
            UInt8(truncatingIfNeeded: checksum >> 8), UInt8(truncatingIfNeeded: checksum)
        ])
        var began = false
        do {
            try? await sendWallpaperFrame(type: .wallpaperCancel, payload: Data(), offset: 0)
            try await sendWallpaperFrame(type: .wallpaperBegin, payload: begin, offset: 0)
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
                try await sendWallpaperFrame(type: .wallpaperChunk, payload: payload,
                                             offset: UInt32(end))
                offset = end
                progress(.sending, Double(offset) / Double(data.count))
            }
            progress(.validating, 1)
            try await sendWallpaperFrame(type: .wallpaperCommit, payload: Data(), offset: size)
            progress(.complete, 1)
        } catch {
            if began {
                try? await sendWallpaperFrame(type: .wallpaperCancel, payload: Data(), offset: 0)
            }
            throw error
        }
    }

    func clearBinding() async {
        if tunnel.state == .connected {
            await tunnel.clearBinding()
        } else {
            bridge?.clearBinding()
        }
    }

    private func connectControl() {
        guard let identifier,
              tunnel.state != .connected, tunnel.state != .connecting,
              tunnel.state != .reconnecting, tunnel.state != .disconnecting,
              bridge == nil else { return }
        state = .connecting
        status = "自动连接中"
        let bridge = BluetoothBridge(role: .controlOnly, targetIdentifier: identifier)
        bridge.onSnapshot = { [weak self] snapshot in
            Task { @MainActor [weak self] in self?.update(from: snapshot) }
        }
        bridge.onReady = { [weak self] in
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.state = .connected
                self.status = "已连接"
                do {
                    let current = try await self.readNetworkState()
                    if current.mode == .vpn {
                        self.bridge?.stop()
                        self.bridge = nil
                        await self.activateVPN()
                    }
                } catch {
                    self.detail = error.localizedDescription
                }
            }
        }
        self.bridge = bridge
        bridge.start()
    }

    private func update(from snapshot: BluetoothSnapshot) {
        bluetooth = snapshot
        switch snapshot.state {
        case "L2CAP ready":
            state = .connected
            status = "已连接"
            detail = "蓝牙控制链路已就绪。"
        case "error" where snapshot.lastError.contains("firmware"):
            state = .upgradeRequired
            status = "需要升级"
            detail = snapshot.lastError
        case "error", "bound device unavailable", "disconnected":
            state = .unreachable
            status = "不可达"
            detail = snapshot.lastError
        default:
            state = .connecting
            status = "自动连接中"
        }
    }

    private func request(type: BridgeFrame.Kind, payload: Data) async throws -> BridgeFrame {
        if tunnel.state == .connected {
            return try await tunnel.sendFrame(type: type, payload: payload)
        }
        guard let bridge else { throw DeviceControlError.notConnected }
        return try await withCheckedThrowingContinuation { continuation in
            bridge.request(type: type, payload: payload) { continuation.resume(with: $0) }
        }
    }

    private func parseNetwork(_ response: BridgeFrame) throws -> DeviceNetworkState {
        guard response.type == .networkState, let status = response.payload.first else {
            throw DeviceControlError.invalidResponse
        }
        try validateStatus(status)
        return try NetworkControlPayload.state(from: response.payload)
    }

    private func sendWallpaperFrame(type: BridgeFrame.Kind, payload: Data,
                                    offset: UInt32) async throws {
        let bytes = [UInt8](try await request(type: type, payload: payload).payload)
        guard bytes.count == 6, bytes[0] == type.rawValue else {
            throw DeviceControlError.invalidResponse
        }
        try validateStatus(bytes[1])
        let accepted = UInt32(bytes[2]) << 24 | UInt32(bytes[3]) << 16 |
            UInt32(bytes[4]) << 8 | UInt32(bytes[5])
        guard accepted == offset else { throw DeviceControlError.invalidResponse }
    }

    private func validateStatus(_ status: UInt8) throws {
        guard status == 0 else { throw DeviceControlError.deviceStatus(status) }
    }

    private func save(mode: DeviceNetworkMode) {
        BajjiSharedSettings.defaults.set(mode.rawValue, forKey: BajjiSharedSettings.networkModeKey)
    }

    private func activateVPN() async {
        guard identifier != nil else { return }
        if tunnel.state == .notInstalled || tunnel.state == .invalid {
            await tunnel.install()
        }
        do {
            try await tunnel.setOnDemand(true)
            if tunnel.state == .installed { await tunnel.start() }
        } catch {
            state = .unreachable
            status = "VPN 不可用"
            detail = error.localizedDescription
        }
    }

    private func disableVPN() async {
        if tunnel.state != .notInstalled {
            try? await tunnel.setOnDemand(false)
        }
        if tunnel.state == .connected || tunnel.state == .connecting ||
            tunnel.state == .reconnecting {
            tunnel.stop()
        }
    }

    private func routeForCurrentMode() {
        let mode = DeviceNetworkMode(rawValue: UInt8(
            BajjiSharedSettings.defaults.integer(forKey: BajjiSharedSettings.networkModeKey)
        )) ?? .unset
        if mode == .vpn {
            bridge?.stop()
            bridge = nil
        } else if tunnel.state == .installed || tunnel.state == .notInstalled ||
                    tunnel.state == .invalid {
            connectControl()
        }
    }
}

private extension BluetoothDiagnostics {
    var snapshot: BluetoothSnapshot {
        BluetoothSnapshot(
            state: state, deviceID: deviceID, psm: psm, maximumPayload: maximumPayload,
            capabilities: capabilities, rssi: rssi, receivedBytes: receivedBytes,
            sentBytes: sentBytes, reconnects: reconnects, queueOverflows: queueOverflows,
            lastError: lastError
        )
    }
}

private enum DeviceControlError: LocalizedError {
    case notConnected
    case invalidResponse
    case invalidWallpaper
    case deviceStatus(UInt8)

    var errorDescription: String? {
        switch self {
        case .notConnected: "StopWatch 蓝牙控制链路尚未就绪。"
        case .invalidResponse: "StopWatch 返回了无法识别的响应。"
        case .invalidWallpaper: "壁纸文件为空或超过 3 MB。"
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
