// SPDX-License-Identifier: MIT
import SwiftUI

struct ContentView: View {
    let tunnel: TunnelManager
    let accessory: AccessoryManager

    var body: some View {
        TabView {
            Tab("设备", systemImage: "applewatch") {
                NavigationStack {
                    DeviceHomeView(tunnel: tunnel, accessory: accessory)
                }
            }

            Tab("图片", systemImage: "photo.on.rectangle.angled") {
                NavigationStack {
                    ImagesHomeView()
                }
            }

            Tab("设置", systemImage: "gearshape") {
                NavigationStack {
                    SettingsHomeView(tunnel: tunnel, accessory: accessory)
                }
            }
        }
        .tint(.bajjiAccent)
    }
}

private struct DeviceHomeView: View {
    let tunnel: TunnelManager
    let accessory: AccessoryManager
    @State private var showsPairing = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text(accessory.hasAccessory ? "StopWatch 已连接" : "连接你的 StopWatch")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                if accessory.hasAccessory {
                    connectedCard
                    connectionHealth
                    NavigationLink("管理网络") {
                        NetworkView(tunnel: tunnel, accessory: accessory)
                    }
                    .buttonStyle(BajjiPrimaryButtonStyle())
                } else {
                    emptyCard
                    Button("添加 StopWatch") {
                        showsPairing = true
                    }
                    .buttonStyle(BajjiPrimaryButtonStyle())
                    .disabled(accessory.isBusy)
                }
            }
            .padding(.horizontal, 16)
            .padding(.bottom, 24)
        }
        .background(Color(uiColor: .systemGroupedBackground))
        .navigationTitle("Bajji")
        .sheet(isPresented: $showsPairing) {
            PairingSetupView(accessory: accessory)
        }
        .task {
            await tunnel.refresh()
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(1))
                await tunnel.readSnapshot()
            }
        }
    }

    private var emptyCard: some View {
        VStack(spacing: 20) {
            BajjiArtwork()
                .frame(width: 168, height: 168)

            VStack(spacing: 8) {
                Text("你的 Bajji，随时在线")
                    .font(.title2.bold())
                Text("Wi‑Fi 优先联网；不可用时自动切换到 iPhone。")
                    .font(.body)
                    .foregroundStyle(.white.opacity(0.78))
                    .multilineTextAlignment(.center)
            }

            StatusBadge("尚未添加设备", color: .bajjiWarning)
        }
        .foregroundStyle(.white)
        .frame(maxWidth: .infinity)
        .padding(24)
        .background(Color.bajjiDarkSurface)
        .clipShape(.rect(cornerRadius: 32))
        .accessibilityElement(children: .combine)
    }

    private var connectedCard: some View {
        VStack(alignment: .leading, spacing: 20) {
            HStack(alignment: .firstTextBaseline) {
                Text(accessory.status)
                    .font(.title2.bold())
                Spacer()
                StatusBadge("已添加", color: .bajjiSuccess)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("VPN 兜底")
                    .font(.subheadline)
                    .foregroundStyle(.white.opacity(0.72))
                Text(tunnel.status)
                    .font(.title3.weight(.semibold))
                Text("Wi‑Fi 优先；VPN 仅在需要时提供备用链路。")
                    .font(.subheadline)
                    .foregroundStyle(.white.opacity(0.72))
            }
        }
        .foregroundStyle(.white)
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(20)
        .background(Color.bajjiDarkSurface)
        .clipShape(.rect(cornerRadius: 32))
        .accessibilityElement(children: .combine)
    }

    private var connectionHealth: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("连接状态")
                .font(.title3.weight(.semibold))
                .padding(.bottom, 8)

            StatusRow(label: "StopWatch", value: "已绑定", valueColor: .bajjiSuccess)
            Divider()
            StatusRow(label: "VPN 兜底", value: tunnel.status)
            Divider()
            StatusRow(
                label: "蓝牙链路",
                value: tunnel.diagnostics?.bluetooth.state ?? "按需连接"
            )
        }
        .padding(20)
        .bajjiCard()
    }
}

private struct PairingSetupView: View {
    @Environment(\.dismiss) private var dismiss
    let accessory: AccessoryManager

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 24) {
                    BajjiArtwork()
                        .frame(width: 168, height: 168)

                    VStack(spacing: 8) {
                        Text("让 iPhone 找到 Bajji")
                            .font(.title2.weight(.semibold))
                        Text("保持 StopWatch 靠近并亮屏。下一步会打开系统配件选择器。")
                            .foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                    }

                    HStack(spacing: 16) {
                        StatusBadge("1 · 发现设备", color: .bajjiAccent)
                        StatusBadge("2 · 安全配对", color: .bajjiSuccess)
                    }

                    Button {
                        Task {
                            await accessory.presentPicker()
                            if accessory.hasAccessory { dismiss() }
                        }
                    } label: {
                        if accessory.isPairing {
                            ProgressView()
                                .tint(.white)
                                .accessibilityLabel("正在打开配件选择器")
                        } else {
                            Text("打开配件选择器")
                        }
                    }
                    .buttonStyle(BajjiPrimaryButtonStyle())
                    .disabled(accessory.isBusy)

                    Text("设备信息与权限提示以 iOS 系统界面为准。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                }
                .padding(24)
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .navigationTitle("添加 StopWatch")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
            }
        }
        .interactiveDismissDisabled(accessory.isPairing)
    }
}

private struct NetworkView: View {
    let tunnel: TunnelManager
    let accessory: AccessoryManager
    @State private var showsWiFiSharing = false
    @State private var showsVPNSetup = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("StopWatch 优先使用 Wi‑Fi；不可用时自动降级到 iPhone。")
                    .font(.body)
                    .foregroundStyle(.secondary)

                networkCard(
                    eyebrow: "首选链路",
                    title: "Wi‑Fi",
                    badge: wifiBadge,
                    detail: "通过 iOS 系统安全共享当前个人网络；App 不保存或显示密码。"
                ) {
                    Button(wifiActionTitle) { showsWiFiSharing = true }
                        .buttonStyle(BajjiPrimaryButtonStyle())
                        .disabled(!accessory.hasAccessory || accessory.isBusy)
                }

                networkCard(
                    eyebrow: "备用链路",
                    title: "VPN 兜底",
                    badge: vpnBadge,
                    detail: "仅在 Wi‑Fi 不可用时，经 BLE L2CAP 为 StopWatch 转发 IPv4。"
                ) {
                    vpnAction
                }

                VStack(alignment: .leading, spacing: 8) {
                    Text("链路优先级")
                        .font(.subheadline.weight(.semibold))
                    Text("Wi‑Fi  →  iPhone VPN  →  离线")
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(16)
                .background(Color(uiColor: .tertiarySystemGroupedBackground))
                .clipShape(.rect(cornerRadius: 16))

                Text("系统扩展、权限或地区不支持时，会提供明确的恢复路径。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            .padding(24)
        }
        .background(Color(uiColor: .systemGroupedBackground))
        .navigationTitle("网络")
        .sheet(isPresented: $showsWiFiSharing) {
            WiFiSharingView(tunnel: tunnel, accessory: accessory)
        }
        .sheet(isPresented: $showsVPNSetup) {
            VPNSetupView(tunnel: tunnel)
        }
        .task { await tunnel.refresh() }
    }

    private func networkCard<Actions: View>(
        eyebrow: String,
        title: String,
        badge: BadgeValue,
        detail: String,
        @ViewBuilder actions: () -> Actions
    ) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(eyebrow.uppercased())
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
            HStack(alignment: .firstTextBaseline) {
                Text(title)
                    .font(.title2.weight(.semibold))
                Spacer()
                StatusBadge(badge.label, color: badge.color)
            }
            Text(detail)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            actions()
                .padding(.top, 12)
        }
        .padding(18)
        .bajjiCard()
    }

    @ViewBuilder
    private var vpnAction: some View {
        switch tunnel.state {
        case .notInstalled, .invalid:
            Button("设置 VPN 兜底") { showsVPNSetup = true }
                .buttonStyle(BajjiOutlineButtonStyle())
        case .installed:
            Button("启用 VPN 兜底") { Task { await tunnel.start() } }
                .buttonStyle(BajjiOutlineButtonStyle())
                .disabled(tunnel.isBusy)
        case .connected, .reconnecting:
            Button("停止 VPN 兜底", role: .destructive) { tunnel.stop() }
                .buttonStyle(BajjiOutlineButtonStyle(color: .red))
        case .connecting, .disconnecting, .unknown:
            ProgressView(tunnel.status)
                .frame(maxWidth: .infinity, minHeight: 56)
        }
    }

    private var wifiActionTitle: String {
        switch accessory.wifiSharingState {
        case .shared: "重新共享 Wi‑Fi"
        case .failed, .restricted: "检查并重试"
        case .notShared, .authorizing: "共享 iPhone Wi‑Fi"
        }
    }

    private var wifiBadge: BadgeValue {
        switch accessory.wifiSharingState {
        case .notShared: BadgeValue("尚未共享", .bajjiWarning)
        case .authorizing: BadgeValue("授权中", .bajjiAccent)
        case .shared: BadgeValue("已共享", .bajjiSuccess)
        case .restricted: BadgeValue("受限", .bajjiWarning)
        case .failed: BadgeValue("需重试", .bajjiWarning)
        }
    }

    private var vpnBadge: BadgeValue {
        switch tunnel.state {
        case .notInstalled, .invalid: BadgeValue("未设置", .secondary)
        case .installed: BadgeValue("待命", .secondary)
        case .connecting: BadgeValue("连接中", .bajjiAccent)
        case .connected: BadgeValue("活动", .bajjiSuccess)
        case .reconnecting: BadgeValue("重连中", .bajjiWarning)
        case .disconnecting: BadgeValue("停止中", .secondary)
        case .unknown: BadgeValue("未知", .secondary)
        }
    }
}

private struct WiFiSharingView: View {
    @Environment(\.dismiss) private var dismiss
    let tunnel: TunnelManager
    let accessory: AccessoryManager
    @State private var showsVPNSetup = false

    var body: some View {
        NavigationStack {
            ScrollView {
                Group {
                    switch accessory.wifiSharingState {
                    case .notShared:
                        intro
                    case .authorizing:
                        progress
                    case .shared:
                        success
                    case .restricted(let reason):
                        restricted(reason)
                    case .failed(let reason):
                        failure(reason)
                    }
                }
                .padding(24)
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .navigationTitle(navigationTitle)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(accessory.wifiSharingState == .shared ? "完成" : "关闭") {
                        dismiss()
                    }
                }
            }
        }
        .interactiveDismissDisabled(accessory.wifiSharingState == .authorizing)
        .sheet(isPresented: $showsVPNSetup) {
            VPNSetupView(tunnel: tunnel)
        }
    }

    private var intro: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("由 iOS 授权并加密下发当前个人网络，不需要手动输入密码。")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 12) {
                Text("当前网络")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                Text("由 iOS 选择")
                    .font(.title2.weight(.semibold))
                Text("可共享的个人 Wi‑Fi")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            .padding(18)
            .bajjiCard()

            VStack(alignment: .leading, spacing: 16) {
                Text("将发生什么")
                    .font(.headline)
                ProcessRow(number: "01", title: "iOS 请求授权", detail: "系统界面确认可共享的配件与网络")
                ProcessRow(number: "02", title: "通过加密 BLE 下发", detail: "App 不读取、不展示凭据明文")
            }
            .padding(18)
            .bajjiCard()

            Text("企业身份、证书或不符合条件的网络不会降级为手动密码表单。")
                .font(.footnote)
                .foregroundStyle(.secondary)

            Button("通过 iOS 共享") { accessory.shareWiFi() }
                .buttonStyle(BajjiPrimaryButtonStyle())
                .disabled(accessory.isBusy)
        }
    }

    private var progress: some View {
        VStack(spacing: 24) {
            ProgressView()
                .controlSize(.large)
                .tint(.bajjiAccent)
            Text("正在安全共享")
                .font(.title2.bold())
            Text("请完成 iOS 系统授权，并保持 StopWatch 在附近。")
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            ProcessRow(number: "01", title: "系统授权", detail: "确认当前配件与个人网络")
            ProcessRow(number: "02", title: "加密传输", detail: "通过已配对的 BLE 链路发送")
        }
        .frame(maxWidth: .infinity)
        .padding(.top, 80)
    }

    private var success: some View {
        VStack(alignment: .leading, spacing: 16) {
            Label("Wi‑Fi 已共享", systemImage: "checkmark.circle.fill")
                .font(.title.bold())
                .foregroundStyle(Color.bajjiSuccess)
            Text("StopWatch 会优先使用刚刚由 iOS 共享的个人网络。")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 12) {
                StatusBadge("已共享", color: .bajjiSuccess)
                Text("个人 Wi‑Fi")
                    .font(.title2.bold())
                Text("凭据由系统加密下发；Bajji App 不保存密码。")
                    .foregroundStyle(.secondary)
                Divider()
                StatusRow(label: "VPN 兜底", value: "待命")
            }
            .padding(18)
            .bajjiCard()

            Text("实际联网状态由 StopWatch 确认；若网络不可达，将自动尝试 VPN 兜底。")
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
    }

    private func restricted(_ reason: String) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            Label("无法共享 Wi‑Fi", systemImage: "exclamationmark.triangle.fill")
                .font(.title.bold())
                .foregroundStyle(Color.bajjiWarning)
            Text("当前系统、地区、权限或网络类型不支持这次凭据共享。")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 12) {
                StatusBadge("受限", color: .bajjiWarning)
                Text("保留凭据安全边界")
                    .font(.title2.weight(.semibold))
                Text("Bajji 不会改用 SSID/密码输入框，也不会尝试导出企业身份或证书。")
                    .foregroundStyle(.secondary)
                Divider()
                Text("检查 iOS 26.2 或更高版本、Wi‑Fi Infrastructure 可用性与当前网络类型。")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            .padding(18)
            .bajjiCard()

            Text(reason)
                .font(.footnote)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            Button("使用 VPN 兜底") { showsVPNSetup = true }
                .buttonStyle(BajjiPrimaryButtonStyle())
        }
    }

    private func failure(_ reason: String) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            Label("共享未完成", systemImage: "wifi.exclamationmark")
                .font(.title.bold())
                .foregroundStyle(Color.bajjiWarning)
            Text(reason)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)
            Button("重试") { accessory.shareWiFi() }
                .buttonStyle(BajjiPrimaryButtonStyle())
        }
    }

    private var navigationTitle: String {
        switch accessory.wifiSharingState {
        case .shared: "Wi‑Fi 已共享"
        case .restricted: "共享受限"
        case .failed: "共享未完成"
        case .notShared, .authorizing: "共享 Wi‑Fi"
        }
    }
}

private struct VPNSetupView: View {
    @Environment(\.dismiss) private var dismiss
    let tunnel: TunnelManager

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    Text("当 Wi‑Fi 不可用时，让 StopWatch 临时借用 iPhone 的网络。")
                        .foregroundStyle(.secondary)

                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Text("Bajji Packet Tunnel")
                                .font(.title2.weight(.semibold))
                            Spacer()
                            StatusBadge("按需启用", color: .bajjiAccent)
                        }
                        Text("使用已绑定的 BLE L2CAP 通道承载 IPv4；不会替代 iPhone 的普通 VPN。")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        Divider()
                        StatusRow(label: "路由范围", value: "10.77.0.0/30")
                        StatusRow(label: "触发条件", value: "Wi‑Fi 不可用")
                        StatusRow(label: "传输", value: "绑定的 BLE")
                    }
                    .padding(18)
                    .bajjiCard()

                    VStack(alignment: .leading, spacing: 8) {
                        Text("需要系统确认")
                            .font(.headline)
                        Text("iOS 会显示 VPN 配置授权；Bajji 不会跳过或伪造这个系统界面。")
                            .foregroundStyle(.secondary)
                    }
                    .padding(16)
                    .background(Color(uiColor: .tertiarySystemGroupedBackground))
                    .clipShape(.rect(cornerRadius: 16))

                    Text("安装后保持待命；只有备用链路真正启用时，状态栏才显示活动连接。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)

                    Button {
                        Task {
                            await tunnel.install()
                            if tunnel.state == .installed { dismiss() }
                        }
                    } label: {
                        if tunnel.isBusy {
                            ProgressView().tint(.white)
                        } else {
                            Text(tunnel.state == .installed ? "VPN 配置已安装" : "安装 VPN 配置")
                        }
                    }
                    .buttonStyle(BajjiPrimaryButtonStyle())
                    .disabled(tunnel.isBusy || tunnel.state == .installed)

                    if tunnel.state == .invalid {
                        Text(tunnel.detail)
                            .font(.footnote)
                            .foregroundStyle(.red)
                    }
                }
                .padding(24)
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .navigationTitle("设置 VPN 兜底")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
            }
        }
        .interactiveDismissDisabled(tunnel.isBusy)
    }
}

private struct ProcessRow: View {
    let number: String
    let title: String
    let detail: String

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Text(number)
                .font(.caption.weight(.semibold))
                .foregroundStyle(Color.bajjiAccent)
                .frame(width: 30, alignment: .leading)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.body.weight(.semibold))
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

private struct BadgeValue {
    let label: String
    let color: Color

    init(_ label: String, _ color: Color) {
        self.label = label
        self.color = color
    }
}

private struct ImagesHomeView: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("选择、预览并发送适合圆形表盘的 Bajji 图片。")
                    .font(.body)
                    .foregroundStyle(.secondary)

                VStack(alignment: .leading, spacing: 18) {
                    StatusBadge("当前", color: .bajjiSuccess)

                    BajjiArtwork()
                        .frame(width: 208, height: 208)
                        .frame(maxWidth: .infinity)

                    Text("当前壁纸")
                        .font(.title2.weight(.semibold))
                    Text("圆形裁切 · 已应用到 StopWatch")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    Divider()
                    Text("最近更新  尚未同步")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(18)
                .bajjiCard()

                NavigationLink("自定义图片") {
                    Text("选择 Bajji 图片")
                        .navigationTitle("自定义图片")
                }
                .buttonStyle(BajjiPrimaryButtonStyle())
            }
            .padding(.horizontal, 24)
            .padding(.bottom, 24)
        }
        .background(Color(uiColor: .systemGroupedBackground))
        .navigationTitle("图片")
    }
}

private struct SettingsHomeView: View {
    let tunnel: TunnelManager
    let accessory: AccessoryManager

    var body: some View {
        List {
            Section("STOPWATCH") {
                NavigationLink("StopWatch 参数") {
                    Text("亮度、换图与触感")
                        .navigationTitle("StopWatch 参数")
                }
                NavigationLink("网络与 VPN") {
                    NetworkView(tunnel: tunnel, accessory: accessory)
                }
                NavigationLink("图片") {
                    Text("当前壁纸")
                        .navigationTitle("图片")
                }
            }

            Section("支持") {
                NavigationLink("诊断与绑定") {
                    Text(accessory.hasAccessory ? "已绑定" : "尚未绑定")
                        .navigationTitle("诊断与绑定")
                }
                LabeledContent("Bajji", value: "版本 1.0")
            }

            Section {
                Text("系统 VPN 与照片授权仍由 iOS 管理。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("设置")
    }
}

private struct StatusRow: View {
    let label: String
    let value: String
    var valueColor: Color = .secondary

    var body: some View {
        HStack {
            Text(label)
            Spacer()
            Text(value)
                .foregroundStyle(valueColor)
        }
        .font(.body)
        .frame(minHeight: 43)
    }
}

private struct StatusBadge: View {
    let label: String
    let color: Color

    init(_ label: String, color: Color) {
        self.label = label
        self.color = color
    }

    var body: some View {
        Text(label)
            .font(.caption.weight(.semibold))
            .foregroundStyle(.white)
            .padding(.horizontal, 12)
            .frame(minHeight: 28)
            .background(color, in: .capsule)
    }
}

private struct BajjiArtwork: View {
    var body: some View {
        ZStack {
            Circle()
                .fill(
                    LinearGradient(
                        colors: [.indigo, .cyan, .orange],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    )
                )
            Circle()
                .stroke(Color.bajjiAccent, lineWidth: 4)
            Image(systemName: "applewatch")
                .font(.system(size: 64, weight: .semibold))
                .foregroundStyle(.white)
                .shadow(radius: 8)
        }
        .accessibilityLabel("Bajji 当前壁纸")
    }
}

private struct BajjiPrimaryButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.body.weight(.semibold))
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, minHeight: 56)
            .background(Color.bajjiAccent.opacity(configuration.isPressed ? 0.78 : 1))
            .clipShape(.rect(cornerRadius: 16))
            .shadow(color: .bajjiAccent.opacity(0.14), radius: 7, y: 6)
    }
}

private struct BajjiOutlineButtonStyle: ButtonStyle {
    var color: Color = .bajjiAccent

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.body.weight(.semibold))
            .foregroundStyle(color)
            .frame(maxWidth: .infinity, minHeight: 54)
            .background(color.opacity(configuration.isPressed ? 0.12 : 0))
            .overlay {
                RoundedRectangle(cornerRadius: 16)
                    .stroke(color, lineWidth: 1)
            }
            .clipShape(.rect(cornerRadius: 16))
    }
}

private extension View {
    func bajjiCard() -> some View {
        background(Color(uiColor: .secondarySystemGroupedBackground))
            .clipShape(.rect(cornerRadius: 20))
            .overlay {
                RoundedRectangle(cornerRadius: 20)
                    .stroke(Color(uiColor: .separator), lineWidth: 0.5)
            }
            .shadow(color: .black.opacity(0.06), radius: 12, y: 8)
    }
}

private extension Color {
    static let bajjiAccent = Color(red: 0, green: 122 / 255, blue: 140 / 255)
    static let bajjiDarkSurface = Color(red: 28 / 255, green: 28 / 255, blue: 30 / 255)
    static let bajjiSuccess = Color(red: 27 / 255, green: 127 / 255, blue: 74 / 255)
    static let bajjiWarning = Color(red: 154 / 255, green: 91 / 255, blue: 0)
}
