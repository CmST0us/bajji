// SPDX-License-Identifier: MIT
import Observation
import PhotosUI
import SwiftUI
import UIKit

struct ContentView: View {
    let tunnel: TunnelManager
    let accessory: AccessoryManager
    let wallpaper: WallpaperStore

    var body: some View {
        TabView {
            Tab("设备", systemImage: "applewatch") {
                NavigationStack {
                    DeviceHomeView(tunnel: tunnel, accessory: accessory)
                }
            }

            Tab("图片", systemImage: "photo.on.rectangle.angled") {
                NavigationStack {
                    ImagesHomeView(accessory: accessory, wallpaper: wallpaper)
                }
            }

            Tab("设置", systemImage: "gearshape") {
                NavigationStack {
                    SettingsHomeView(tunnel: tunnel, accessory: accessory, wallpaper: wallpaper)
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
    let accessory: AccessoryManager
    let wallpaper: WallpaperStore
    @State private var showsEditor = false
    @State private var showsTransferStatus = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("选择、预览并发送适合圆形表盘的 Bajji 图片。")
                    .font(.body)
                    .foregroundStyle(.secondary)

                VStack(alignment: .leading, spacing: 18) {
                    StatusBadge(
                        wallpaper.currentImage == nil ? "当前" : "待发送",
                        color: wallpaper.currentImage == nil ? .bajjiSuccess : .bajjiWarning
                    )

                    Group {
                        if let image = wallpaper.currentImage {
                            Image(uiImage: image)
                                .resizable()
                                .scaledToFill()
                                .clipShape(.circle)
                                .overlay { Circle().stroke(Color.bajjiAccent, lineWidth: 3) }
                        } else {
                            BajjiArtwork()
                        }
                    }
                    .frame(width: 208, height: 208)
                    .frame(maxWidth: .infinity)

                    Text(wallpaper.currentImage == nil ? "当前随机壁纸" : "我的图片")
                        .font(.title2.weight(.semibold))
                    Text(wallpaper.currentImage == nil ?
                         "由 StopWatch 管理" : "468×468 · 已保存在 Bajji App")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    Divider()
                    Text(wallpaper.updatedAt.map {
                        "最近更新  \($0.formatted(date: .abbreviated, time: .shortened))"
                    } ?? "最近更新  尚未同步")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(18)
                .bajjiCard()

                Button(wallpaper.currentImage == nil ? "自定义图片" : "更换图片") {
                    showsEditor = true
                }
                .buttonStyle(BajjiPrimaryButtonStyle())

                if wallpaper.currentImage != nil {
                    Button("查看发送状态") { showsTransferStatus = true }
                        .buttonStyle(BajjiOutlineButtonStyle())
                }
            }
            .padding(.horizontal, 24)
            .padding(.bottom, 24)
        }
        .background(Color(uiColor: .systemGroupedBackground))
        .navigationTitle("图片")
        .sheet(isPresented: $showsEditor) {
            WallpaperEditorView(wallpaper: wallpaper)
        }
        .sheet(isPresented: $showsTransferStatus) {
            WallpaperTransferStatusView(accessory: accessory, wallpaper: wallpaper)
        }
    }
}

private struct WallpaperEditorView: View {
    @Environment(\.dismiss) private var dismiss
    let wallpaper: WallpaperStore
    @State private var selectedItem: PhotosPickerItem?
    @State private var importError: String?
    @State private var dragOrigin: CGSize = .zero
    @State private var zoomOrigin: CGFloat = 1
    @State private var showsTransferStatus = false

    var body: some View {
        NavigationStack {
            ScrollView {
                if let image = wallpaper.draftImage {
                    editor(image)
                } else {
                    pickerIntro
                }
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .navigationTitle(wallpaper.draftImage == nil ? "自定义图片" : "调整预览")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") {
                        wallpaper.discardDraft()
                        dismiss()
                    }
                }
            }
        }
        .onChange(of: selectedItem) { _, item in
            guard let item else { return }
            Task {
                do {
                    try await wallpaper.importPhoto(item)
                    dragOrigin = .zero
                    zoomOrigin = 1
                } catch {
                    importError = error.localizedDescription
                }
            }
        }
        .alert("无法导入图片", isPresented: Binding(
            get: { importError != nil },
            set: { if !$0 { importError = nil } }
        )) {
            Button("好", role: .cancel) {}
        } message: {
            Text(importError ?? "未知错误")
        }
        .sheet(isPresented: $showsTransferStatus) {
            WallpaperTransferStatusView(accessory: nil, wallpaper: wallpaper)
        }
        .onChange(of: showsTransferStatus) { wasPresented, isPresented in
            if wasPresented && !isPresented { dismiss() }
        }
    }

    private var pickerIntro: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("从 iOS 照片选择器挑选一张图片。")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 12) {
                StatusBadge("系统界面", color: .bajjiAccent)
                Text("使用 iOS 照片选择器")
                    .font(.title2.weight(.semibold))
                Text("只有你明确确认的项目会交给 Bajji；App 不需要浏览整个照片图库。")
                    .foregroundStyle(.secondary)
                Divider()
                Text("选择器、权限与取消行为由 iOS 管理。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            .padding(18)
            .bajjiCard()

            VStack(alignment: .leading, spacing: 8) {
                Text("导入后处理")
                    .font(.headline)
                Text("支持常见照片格式；确认后生成 468×468 的圆形显示预览。")
                    .foregroundStyle(.secondary)
            }
            .padding(16)
            .background(Color(uiColor: .tertiarySystemGroupedBackground))
            .clipShape(.rect(cornerRadius: 16))

            Text("原图保留在系统照片库中；Bajji 仅保存预览所需的结果。")
                .font(.footnote)
                .foregroundStyle(.secondary)

            PhotosPicker(selection: $selectedItem, matching: .images) {
                Text("选择照片")
            }
            .buttonStyle(BajjiPrimaryButtonStyle())
        }
        .padding(24)
    }

    private func editor(_ image: UIImage) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("拖动和缩放图片，确认圆形表盘中的可见区域。")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 16) {
                Image(uiImage: image)
                    .resizable()
                    .aspectRatio(contentMode: wallpaper.displayMode.contentMode)
                    .scaleEffect(wallpaper.zoom)
                    .offset(wallpaper.offset)
                    .frame(width: 208, height: 208)
                    .background(.black)
                    .clipShape(.circle)
                    .overlay { Circle().stroke(Color.bajjiAccent, lineWidth: 3) }
                    .frame(maxWidth: .infinity)
                    .contentShape(Circle())
                    .gesture(
                        DragGesture()
                            .onChanged { value in
                                wallpaper.offset = CGSize(
                                    width: dragOrigin.width + value.translation.width,
                                    height: dragOrigin.height + value.translation.height
                                )
                            }
                            .onEnded { _ in dragOrigin = wallpaper.offset }
                    )
                    .simultaneousGesture(
                        MagnifyGesture()
                            .onChanged { value in
                                wallpaper.zoom = min(4, max(1, zoomOrigin * value.magnification))
                            }
                            .onEnded { _ in zoomOrigin = wallpaper.zoom }
                    )
                    .onTapGesture(count: 2) {
                        wallpaper.resetTransform()
                        dragOrigin = .zero
                        zoomOrigin = 1
                    }
                    .accessibilityLabel("圆形壁纸预览")

                Picker("显示方式", selection: Bindable(wallpaper).displayMode) {
                    ForEach(WallpaperDisplayMode.allCases, id: \.self) { mode in
                        Text(mode.label).tag(mode)
                    }
                }
                .pickerStyle(.segmented)

                VStack(alignment: .leading, spacing: 6) {
                    Text(String(format: "缩放  %.1f×", wallpaper.zoom))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Slider(value: Bindable(wallpaper).zoom, in: 1...4, step: 0.1)
                }

                Divider()
                Text("双指缩放 · 单指拖动 · 双击复位")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            .padding(18)
            .bajjiCard()

            Text("圆形以外的区域不会显示；保存前仍可返回重新选图。")
                .font(.footnote)
                .foregroundStyle(.secondary)

            Button("使用此预览") {
                do {
                    try wallpaper.saveDraft()
                    showsTransferStatus = true
                } catch {
                    importError = error.localizedDescription
                }
            }
            .buttonStyle(BajjiPrimaryButtonStyle())
        }
        .padding(24)
    }
}

private struct WallpaperTransferStatusView: View {
    @Environment(\.dismiss) private var dismiss
    let accessory: AccessoryManager?
    let wallpaper: WallpaperStore

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    Text("预览资源已安全保存在 Bajji App；当前 Bridge v1 尚未提供手机向设备传图的消息。")
                        .foregroundStyle(.secondary)

                    VStack(alignment: .leading, spacing: 18) {
                        if let image = wallpaper.currentImage {
                            Image(uiImage: image)
                                .resizable()
                                .scaledToFill()
                                .frame(width: 72, height: 72)
                                .clipShape(.circle)
                        }
                        StatusBadge("1 / 4 · 已准备", color: .bajjiWarning)
                        TransferStep(number: "01", title: "准备圆形资源", detail: "468×468 JPEG 已保存在 App", state: .complete)
                        TransferStep(number: "02", title: "发送到 StopWatch", detail: "等待固件与 Bridge 协议支持", state: .blocked)
                        TransferStep(number: "03", title: "写入壁纸缓存", detail: "设备校验后才可替换旧图", state: .pending)
                        TransferStep(number: "04", title: "应用并确认", detail: "收到设备显示回执后才算完成", state: .pending)
                    }
                    .padding(18)
                    .bajjiCard()

                    if accessory?.hasAccessory == false {
                        Text("先添加 StopWatch；图片仍会保留在 App 中。")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }

                    Text("未发送或传输失败不会覆盖设备当前壁纸。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)

                    Button("完成") { dismiss() }
                        .buttonStyle(BajjiPrimaryButtonStyle())
                }
                .padding(24)
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .navigationTitle("待发送")
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}

private struct TransferStep: View {
    enum State: Equatable { case complete, blocked, pending }
    let number: String
    let title: String
    let detail: String
    let state: State

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Text(number)
                .font(.caption.weight(.semibold))
                .foregroundStyle(color)
                .frame(width: 30, alignment: .leading)
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.body.weight(.semibold))
                    .foregroundStyle(color)
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(state == .pending ? Color.secondary : color)
            }
        }
    }

    private var color: Color {
        switch state {
        case .complete: .bajjiSuccess
        case .blocked: .bajjiWarning
        case .pending: .secondary
        }
    }
}

private struct SettingsHomeView: View {
    let tunnel: TunnelManager
    let accessory: AccessoryManager
    let wallpaper: WallpaperStore

    var body: some View {
        List {
            Section {
                Text("管理 StopWatch、连接与 Bajji 行为。")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }

            Section("STOPWATCH") {
                NavigationLink {
                    StopWatchParametersView(wallpaper: wallpaper)
                } label: {
                    SettingsRowLabel(title: "StopWatch 参数", detail: "亮度、换图与触感")
                }
                NavigationLink {
                    NetworkView(tunnel: tunnel, accessory: accessory)
                } label: {
                    SettingsRowLabel(title: "网络与 VPN", detail: "Wi‑Fi 优先")
                }
                NavigationLink {
                    ImagesHomeView(accessory: accessory, wallpaper: wallpaper)
                } label: {
                    SettingsRowLabel(
                        title: "图片",
                        detail: wallpaper.currentImage == nil ? "随机壁纸" : "待发送"
                    )
                }
            }

            Section("支持") {
                NavigationLink {
                    DiagnosticsView(tunnel: tunnel, accessory: accessory)
                } label: {
                    SettingsRowLabel(
                        title: "诊断与绑定",
                        detail: accessory.hasAccessory ? "已绑定" : "尚未绑定"
                    )
                }
                LabeledContent("Bajji", value: "版本 \(appVersion)")
            }

            Section {
                Text("系统 VPN 与照片授权仍由 iOS 管理。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("设置")
    }

    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.0"
    }
}

private struct SettingsRowLabel: View {
    let title: String
    let detail: String

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            Text(detail)
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
    }
}

private struct StopWatchParametersView: View {
    let wallpaper: WallpaperStore
    @AppStorage("bajji.brightness") private var brightness = 0.72
    @AppStorage("bajji.autoRefresh") private var autoRefresh = true
    @AppStorage("bajji.refreshMinutes") private var refreshMinutes = 30
    @AppStorage("bajji.haptics") private var haptics = true

    var body: some View {
        Form {
            Section {
                HStack {
                    Text("屏幕亮度")
                    Spacer()
                    Text(brightness, format: .percent.precision(.fractionLength(0)))
                        .foregroundStyle(.secondary)
                }
                Slider(value: $brightness, in: 0.1...1, step: 0.01) {
                    Text("屏幕亮度")
                } minimumValueLabel: {
                    Image(systemName: "sun.min")
                } maximumValueLabel: {
                    Image(systemName: "sun.max.fill")
                }
            }

            Section {
                Toggle("自动换图", isOn: $autoRefresh)
                NavigationLink {
                    RefreshIntervalView()
                } label: {
                    LabeledContent("刷新间隔", value: autoRefresh ? intervalLabel : "手动")
                }
                .disabled(!autoRefresh)

                Picker("图片显示", selection: Bindable(wallpaper).displayMode) {
                    ForEach(WallpaperDisplayMode.allCases, id: \.self) { mode in
                        Text(mode.label).tag(mode)
                    }
                }

                Toggle("触感反馈", isOn: $haptics)
            }

            Section {
                Text("这些值会保存在 App 中。当前 Bridge v1 尚未提供设备参数消息，设备确认与失败回滚会在协议支持后启用。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("StopWatch 参数")
        .sensoryFeedback(.selection, trigger: haptics)
    }

    private var intervalLabel: String {
        guard autoRefresh else { return "手动" }
        return RefreshInterval(rawValue: refreshMinutes)?.label ?? "30 分钟"
    }
}

private struct RefreshIntervalView: View {
    @AppStorage("bajji.autoRefresh") private var autoRefresh = true
    @AppStorage("bajji.refreshMinutes") private var refreshMinutes = 30

    var body: some View {
        List {
            Section {
                ForEach(RefreshInterval.allCases, id: \.self) { option in
                    Button {
                        refreshMinutes = option.rawValue
                        autoRefresh = option != .manual
                    } label: {
                        HStack {
                            Text(option.label)
                                .foregroundStyle(.primary)
                            Spacer()
                            if refreshMinutes == option.rawValue {
                                Image(systemName: "checkmark")
                                    .fontWeight(.semibold)
                                    .foregroundStyle(Color.bajjiAccent)
                            }
                        }
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            } header: {
                Text("间隔")
            } footer: {
                Text("自动换图关闭时不会计时；重新开启后从当前时刻开始。")
            }
        }
        .navigationTitle("刷新间隔")
    }
}

private enum RefreshInterval: Int, CaseIterable {
    case fifteen = 15
    case thirty = 30
    case sixty = 60
    case manual = 0

    var label: String {
        switch self {
        case .fifteen: "15 分钟"
        case .thirty: "30 分钟"
        case .sixty: "1 小时"
        case .manual: "手动"
        }
    }
}

private struct DiagnosticsView: View {
    let tunnel: TunnelManager
    let accessory: AccessoryManager
    @State private var showsUnpairConfirmation = false
    @State private var isUnpairing = false
    @State private var unpairError: String?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("查看当前链路状态，或安全解除 StopWatch 绑定。")
                    .foregroundStyle(.secondary)

                VStack(alignment: .leading, spacing: 8) {
                    StatusBadge(statusBadge.label, color: statusBadge.color)
                    Text(accessory.hasAccessory ? accessory.status : "Bajji StopWatch")
                        .font(.title2.weight(.semibold))
                    StatusRow(
                        label: "BLE 绑定",
                        value: accessory.hasAccessory ? "已添加" : "未添加",
                        valueColor: accessory.hasAccessory ? .bajjiSuccess : .secondary
                    )
                    Divider()
                    StatusRow(label: "Wi‑Fi", value: wifiStatus)
                    Divider()
                    StatusRow(label: "VPN", value: tunnel.status)
                }
                .padding(18)
                .bajjiCard()

                if let diagnostics = tunnel.diagnostics {
                    VStack(alignment: .leading, spacing: 0) {
                        Text("实时诊断")
                            .font(.headline)
                            .padding(.bottom, 8)
                        StatusRow(label: "互联网", value: internetStatus(diagnostics.internet))
                        Divider()
                        StatusRow(label: "BLE RSSI", value: rssiText(diagnostics.bluetooth.rssi))
                        Divider()
                        StatusRow(
                            label: "设备 → Internet",
                            value: formattedBytes(diagnostics.forwarder.fromDeviceBytes)
                        )
                        Divider()
                        StatusRow(
                            label: "Internet → 设备",
                            value: formattedBytes(diagnostics.forwarder.toDeviceBytes)
                        )
                        Divider()
                        StatusRow(
                            label: "丢弃 / 无效",
                            value: "\(diagnostics.forwarder.droppedPackets) / \(diagnostics.forwarder.invalidPackets)",
                            valueColor: diagnostics.forwarder.droppedPackets == 0 &&
                                diagnostics.forwarder.invalidPackets == 0 ? .secondary : .red
                        )
                    }
                    .padding(18)
                    .bajjiCard()
                }

                Text(tunnel.detail)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)

                Button("刷新状态") {
                    Task {
                        await tunnel.refresh()
                        await tunnel.readSnapshot()
                    }
                }
                .buttonStyle(BajjiOutlineButtonStyle())

                Button("解除绑定", role: .destructive) {
                    showsUnpairConfirmation = true
                }
                .buttonStyle(BajjiDestructiveButtonStyle())
                .disabled(!accessory.hasAccessory || isUnpairing)

                Text("解除绑定会清除配件关系；不会删除 iPhone 照片图库中的原图。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            .padding(24)
        }
        .background(Color(uiColor: .systemGroupedBackground))
        .navigationTitle("诊断与绑定")
        .task {
            await tunnel.refresh()
            await tunnel.readSnapshot()
        }
        .confirmationDialog(
            "解除 StopWatch 绑定？",
            isPresented: $showsUnpairConfirmation,
            titleVisibility: .visible
        ) {
            Button("解除 StopWatch 绑定", role: .destructive) { unpair() }
            Button("保留绑定", role: .cancel) {}
        } message: {
            Text("解除后需重新添加并授权 StopWatch；iPhone 照片中的原图不会删除。")
        }
        .alert("无法解除绑定", isPresented: Binding(
            get: { unpairError != nil },
            set: { if !$0 { unpairError = nil } }
        )) {
            Button("好", role: .cancel) {}
        } message: {
            Text(unpairError ?? "未知错误")
        }
    }

    private func unpair() {
        isUnpairing = true
        Task {
            if tunnel.state == .connected {
                await tunnel.clearBinding()
            }
            await accessory.removeAccessory()
            if accessory.hasAccessory {
                unpairError = accessory.detail
            } else if tunnel.state == .connected {
                tunnel.stop()
            }
            isUnpairing = false
        }
    }

    private var statusBadge: BadgeValue {
        if accessory.hasAccessory && tunnel.state != .invalid {
            BadgeValue("状态正常", .bajjiSuccess)
        } else if accessory.hasAccessory {
            BadgeValue("需检查", .bajjiWarning)
        } else {
            BadgeValue("未绑定", .secondary)
        }
    }

    private var wifiStatus: String {
        switch accessory.wifiSharingState {
        case .notShared: "尚未共享"
        case .authorizing: "授权中"
        case .shared: "已共享"
        case .restricted: "受限"
        case .failed: "需重试"
        }
    }

    private func rssiText(_ value: Int) -> String {
        value == 0 ? "—" : "\(value) dBm"
    }

    private func internetStatus(_ value: String) -> String {
        switch value.lowercased() {
        case "online", "connected": "已联网"
        case "offline", "disconnected": "离线"
        default: value
        }
    }

    private func formattedBytes(_ value: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: value), countStyle: .binary)
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
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.body.weight(.semibold))
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, minHeight: 56)
            .background(Color.bajjiAccent.opacity(configuration.isPressed ? 0.78 : 1))
            .clipShape(.rect(cornerRadius: 16))
            .shadow(color: .bajjiAccent.opacity(0.14), radius: 7, y: 6)
            .opacity(isEnabled ? 1 : 0.42)
    }
}

private struct BajjiOutlineButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled
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
            .opacity(isEnabled ? 1 : 0.42)
    }
}

private struct BajjiDestructiveButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.body.weight(.semibold))
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, minHeight: 54)
            .background(Color.red.opacity(configuration.isPressed ? 0.78 : 1))
            .clipShape(.rect(cornerRadius: 16))
            .opacity(isEnabled ? 1 : 0.36)
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

enum WallpaperDisplayMode: String, CaseIterable {
    case fit
    case fill

    var label: String {
        switch self {
        case .fit: "适应"
        case .fill: "填充"
        }
    }

    var contentMode: ContentMode {
        switch self {
        case .fit: .fit
        case .fill: .fill
        }
    }
}

@MainActor
@Observable
final class WallpaperStore {
    private static let maximumImportBytes = 40 * 1024 * 1024
    private static let maximumPixels: CGFloat = 50_000_000
    private static let deviceSize = CGSize(width: 468, height: 468)

    var currentImage: UIImage?
    var draftImage: UIImage?
    var displayMode: WallpaperDisplayMode = .fill {
        didSet {
            UserDefaults.standard.set(displayMode.rawValue, forKey: "bajji.wallpaperDisplayMode")
        }
    }
    var zoom: CGFloat = 1
    var offset: CGSize = .zero
    var updatedAt: Date?

    init() {
        if let rawValue = UserDefaults.standard.string(forKey: "bajji.wallpaperDisplayMode"),
           let savedMode = WallpaperDisplayMode(rawValue: rawValue) {
            displayMode = savedMode
        }
        guard let data = try? Data(contentsOf: Self.savedImageURL),
              let image = UIImage(data: data) else { return }
        currentImage = image
        updatedAt = (try? Self.savedImageURL.resourceValues(forKeys: [.contentModificationDateKey]))?
            .contentModificationDate
    }

    func importPhoto(_ item: PhotosPickerItem) async throws {
        guard let data = try await item.loadTransferable(type: Data.self) else {
            throw WallpaperError.unreadableImage
        }
        guard data.count <= Self.maximumImportBytes,
              let image = UIImage(data: data),
              image.size.width * image.scale * image.size.height * image.scale <= Self.maximumPixels else {
            throw WallpaperError.imageTooLarge
        }
        draftImage = image
        resetTransform()
    }

    func saveDraft() throws {
        guard let image = draftImage else { throw WallpaperError.unreadableImage }
        let rendered = render(image)
        guard let data = rendered.jpegData(compressionQuality: 0.9) else {
            throw WallpaperError.couldNotEncode
        }
        try FileManager.default.createDirectory(
            at: Self.savedImageURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: Self.savedImageURL, options: .atomic)
        currentImage = rendered
        updatedAt = Date()
        draftImage = nil
        resetTransform()
    }

    func discardDraft() {
        draftImage = nil
        resetTransform()
    }

    func resetTransform() {
        zoom = 1
        offset = .zero
    }

    private func render(_ image: UIImage) -> UIImage {
        let renderer = UIGraphicsImageRenderer(size: Self.deviceSize)
        return renderer.image { context in
            let canvas = CGRect(origin: .zero, size: Self.deviceSize)
            UIColor.black.setFill()
            context.fill(canvas)

            context.cgContext.saveGState()
            UIBezierPath(ovalIn: canvas).addClip()
            let baseScale: CGFloat
            switch displayMode {
            case .fit:
                baseScale = min(Self.deviceSize.width / image.size.width,
                                Self.deviceSize.height / image.size.height)
            case .fill:
                baseScale = max(Self.deviceSize.width / image.size.width,
                                Self.deviceSize.height / image.size.height)
            }
            let drawSize = CGSize(
                width: image.size.width * baseScale * zoom,
                height: image.size.height * baseScale * zoom
            )
            let previewToDevice = Self.deviceSize.width / 208
            let origin = CGPoint(
                x: (Self.deviceSize.width - drawSize.width) / 2 + offset.width * previewToDevice,
                y: (Self.deviceSize.height - drawSize.height) / 2 + offset.height * previewToDevice
            )
            image.draw(in: CGRect(origin: origin, size: drawSize))
            context.cgContext.restoreGState()
        }
    }

    private static var savedImageURL: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appending(path: "Bajji", directoryHint: .isDirectory)
            .appending(path: "wallpaper-preview.jpg")
    }
}

private enum WallpaperError: LocalizedError {
    case unreadableImage
    case imageTooLarge
    case couldNotEncode

    var errorDescription: String? {
        switch self {
        case .unreadableImage: "无法读取所选照片。"
        case .imageTooLarge: "图片过大，请选择小于 40 MB、5000 万像素的照片。"
        case .couldNotEncode: "无法生成 StopWatch 预览资源。"
        }
    }
}
