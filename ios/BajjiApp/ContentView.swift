// SPDX-License-Identifier: MIT
import Observation
import PhotosUI
import SwiftUI
import UIKit

struct ContentView: View {
    let tunnel: TunnelManager
    let device: DeviceConnectionManager
    let accessory: AccessoryManager
    let wallpaper: WallpaperStore

    var body: some View {
        TabView {
            Tab("设备", systemImage: "applewatch") {
                NavigationStack {
                    DeviceHomeView(tunnel: tunnel, device: device, accessory: accessory)
                }
            }

            Tab("图片", systemImage: "photo.on.rectangle.angled") {
                NavigationStack {
                    ImagesHomeView(device: device, accessory: accessory, wallpaper: wallpaper)
                }
            }

            Tab("设置", systemImage: "gearshape") {
                NavigationStack {
                    SettingsHomeView(tunnel: tunnel, device: device, accessory: accessory,
                                     wallpaper: wallpaper)
                }
            }
        }
        .tint(.bajjiAccent)
        .task(id: accessory.selectedBluetoothIdentifier) {
            await device.start(identifier: accessory.selectedBluetoothIdentifier)
        }
    }
}

private struct DeviceHomeView: View {
    let tunnel: TunnelManager
    let device: DeviceConnectionManager
    let accessory: AccessoryManager
    @State private var showsPairing = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text(accessory.hasAccessory ? device.status : "连接你的 StopWatch")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                if accessory.hasAccessory {
                    connectedCard
                    connectionHealth
                    NavigationLink("管理网络") {
                        NetworkView(tunnel: tunnel, device: device, accessory: accessory)
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
    }

    private var emptyCard: some View {
        VStack(spacing: 20) {
            BajjiArtwork()
                .frame(width: 168, height: 168)

            VStack(spacing: 8) {
                Text("你的 Bajji，随时在线")
                    .font(.title2.bold())
                Text("添加后自动连接设备，再由你选择 Wi‑Fi 或 VPN。")
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
                StatusBadge(device.status, color: device.isReady ? .bajjiSuccess : .bajjiWarning)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("当前网络")
                    .font(.subheadline)
                    .foregroundStyle(.white.opacity(0.72))
                Text(networkModeLabel)
                    .font(.title3.weight(.semibold))
                Text("网络方式由你选择；设备不会自动切换到其他链路。")
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
            StatusRow(label: "控制连接", value: device.status,
                      valueColor: device.isReady ? .bajjiSuccess : .secondary)
            Divider()
            StatusRow(label: "网络方式", value: networkModeLabel)
        }
        .padding(20)
        .bajjiCard()
    }

    private var networkModeLabel: String {
        switch device.network?.mode {
        case .manual: "指定 Wi‑Fi"
        case .shared: "共享 iPhone Wi‑Fi"
        case .vpn: "VPN 通道"
        case .unset, nil: "尚未选择"
        }
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
    let device: DeviceConnectionManager
    let accessory: AccessoryManager
    @State private var showsWiFiSharing = false
    @State private var errorMessage: String?

    var body: some View {
        List {
            Section {
                Text("选择一种互斥的上网方式。连接失败时，Bajji 只重试当前选择。")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }

            Section("连接方式") {
                NavigationLink {
                    ManualNetworkView(device: device)
                } label: {
                    networkRow(.manual, title: "手动设置目标网络",
                               detail: "输入 SSID 与密码，通过蓝牙保存到设备")
                }

                Button {
                    selectShared()
                } label: {
                    networkRow(.shared, title: "共享 iPhone Wi‑Fi",
                               detail: "由 Wi‑Fi Infrastructure 安全共享当前网络")
                }
                .buttonStyle(.plain)
                .disabled(!device.isReady || device.isBusy || accessory.isBusy)

                Button {
                    Task { await selectVPN() }
                } label: {
                    networkRow(.vpn, title: "使用 VPN 通道",
                               detail: vpnDetail)
                }
                .buttonStyle(.plain)
                .disabled(!device.isReady || device.isBusy)
            }

            Section {
                LabeledContent("设备控制", value: device.status)
                LabeledContent("网络", value: networkStatus)
                if device.network?.mode == .vpn {
                    LabeledContent("VPN", value: tunnel.status)
                }
                if let ssid = device.network?.ssid, !ssid.isEmpty {
                    LabeledContent("SSID", value: ssid)
                }
            } header: {
                Text("当前状态")
            } footer: {
                Text("也可在 StopWatch 设置中打开独立 Wi‑Fi 配网；Portal 成功后会显示为“手动设置目标网络”。")
            }

            if let errorMessage {
                Section {
                    Label(errorMessage, systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(.red)
                }
            }
        }
        .navigationTitle("网络")
        .sheet(isPresented: $showsWiFiSharing) {
            WiFiSharingView(device: device, accessory: accessory)
        }
        .task {
            if device.isReady { _ = try? await device.readNetworkState() }
        }
    }

    private func networkRow(_ mode: DeviceNetworkMode, title: String,
                            detail: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: device.network?.mode == mode ? "checkmark.circle.fill" : "circle")
                .foregroundStyle(device.network?.mode == mode ? Color.bajjiAccent : .secondary)
                .font(.title3)
            VStack(alignment: .leading, spacing: 3) {
                Text(title)
                    .foregroundStyle(.primary)
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            if device.network?.mode == mode {
                StatusBadge("已选择", color: .bajjiAccent)
            }
        }
        .frame(minHeight: 52)
        .contentShape(Rectangle())
    }

    private func selectShared() {
        Task {
            do {
                _ = try await device.selectNetwork(.shared)
                showsWiFiSharing = true
                accessory.shareWiFi()
            } catch {
                errorMessage = error.localizedDescription
            }
        }
    }

    private func selectVPN() async {
        do {
            _ = try await device.selectNetwork(.vpn)
            errorMessage = nil
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private var networkStatus: String {
        switch device.network?.linkState {
        case .disabled: "已停用"
        case .unconfigured: "等待选择"
        case .awaitingCredentials: "等待凭据"
        case .connecting: "连接中"
        case .connected: "已连接"
        case .retrying: "重试中"
        case nil: "正在读取"
        }
    }

    private var vpnDetail: String {
        switch tunnel.state {
        case .notInstalled, .invalid: "选择后安装 VPN 配置并立即启动"
        case .connected: "VPN 活动，设备通过 iPhone 上网"
        case .connecting, .reconnecting: "VPN 正在连接"
        default: "选择后保持按需 VPN 活动"
        }
    }
}

private struct ManualNetworkView: View {
    @Environment(\.dismiss) private var dismiss
    let device: DeviceConnectionManager
    @State private var ssid = ""
    @State private var password = ""
    @State private var errorMessage: String?

    var body: some View {
        Form {
            Section {
                TextField("SSID", text: $ssid)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                SecureField("密码（开放网络留空）", text: $password)
                    .textContentType(.password)
            } header: {
                Text("目标网络")
            } footer: {
                Text("支持开放网络及 WPA2/WPA3 个人网络；密码不会保存在 App 中。")
            }
            Section {
                Button(device.isBusy ? "正在连接…" : "保存并连接") {
                    Task { await save() }
                }
                .disabled(device.isBusy || ssid.isEmpty)
                if let errorMessage {
                    Text(errorMessage).foregroundStyle(.red)
                }
            }
        }
        .navigationTitle("手动设置 Wi‑Fi")
        .navigationBarTitleDisplayMode(.inline)
    }

    private func save() async {
        do {
            _ = try await device.selectNetwork(.manual, ssid: ssid, password: password)
            password = ""
            dismiss()
        } catch {
            password = ""
            errorMessage = error.localizedDescription
        }
    }
}

private struct WiFiSharingView: View {
    @Environment(\.dismiss) private var dismiss
    let device: DeviceConnectionManager
    let accessory: AccessoryManager

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
            Text("StopWatch 将只使用刚刚由 iOS 共享的个人网络。")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 12) {
                StatusBadge("已共享", color: .bajjiSuccess)
                Text("个人 Wi‑Fi")
                    .font(.title2.bold())
                Text("凭据由系统加密下发；Bajji App 不保存密码。")
                    .foregroundStyle(.secondary)
            }
            .padding(18)
            .bajjiCard()

            Text("实际联网状态由 StopWatch 确认；失败时只会重试这一个网络。")
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

            Text("关闭此页后可在网络列表中改选手动 Wi‑Fi 或 VPN 通道。")
                .font(.footnote)
                .foregroundStyle(.secondary)
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
    let device: DeviceConnectionManager
    let accessory: AccessoryManager
    let wallpaper: WallpaperStore
    @State private var showsEditor = false
    @State private var showsTransferStatus = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("选择并发送方形原图；圆屏遮罩由 StopWatch 最终处理。")
                    .font(.body)
                    .foregroundStyle(.secondary)

                VStack(alignment: .leading, spacing: 18) {
                    StatusBadge(
                        wallpaper.currentImage == nil ? "当前" :
                            (wallpaper.needsTransfer ? "待发送" : "已同步"),
                        color: wallpaper.currentImage == nil || !wallpaper.needsTransfer
                            ? .bajjiSuccess : .bajjiWarning
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
                         "由 StopWatch 管理" :
                            (wallpaper.needsTransfer ? "468×468 · 已保存在 Bajji App" :
                                "468×468 · StopWatch 已确认"))
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
                    Button(wallpaper.needsTransfer ? "发送到 StopWatch" : "查看发送状态") {
                        showsTransferStatus = true
                    }
                        .buttonStyle(BajjiOutlineButtonStyle())
                }
            }
            .padding(.horizontal, 24)
            .padding(.bottom, 24)
        }
        .background(Color(uiColor: .systemGroupedBackground))
        .navigationTitle("图片")
        .sheet(isPresented: $showsEditor) {
            WallpaperEditorView(device: device, accessory: accessory, wallpaper: wallpaper)
        }
        .sheet(isPresented: $showsTransferStatus) {
            WallpaperTransferStatusView(device: device, accessory: accessory, wallpaper: wallpaper)
        }
    }
}

private struct WallpaperEditorView: View {
    @Environment(\.dismiss) private var dismiss
    let device: DeviceConnectionManager
    let accessory: AccessoryManager
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
            WallpaperTransferStatusView(device: device, accessory: accessory, wallpaper: wallpaper)
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
                Text("支持常见照片格式；确认后生成 468×468 无损方形 PNG。")
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
            Text("拖动和缩放图片，选择要发送给 StopWatch 的方形区域。")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 16) {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFill()
                    .scaleEffect(wallpaper.zoom)
                    .offset(wallpaper.offset)
                    .frame(width: 208, height: 208)
                    .background(.black)
                    .clipped()
                    .overlay { Rectangle().stroke(Color.bajjiAccent, lineWidth: 3) }
                    .frame(maxWidth: .infinity)
                    .contentShape(Rectangle())
                    .gesture(
                        DragGesture()
                            .onChanged { value in
                                wallpaper.setOffset(CGSize(
                                    width: dragOrigin.width + value.translation.width,
                                    height: dragOrigin.height + value.translation.height
                                ), for: image)
                            }
                            .onEnded { _ in dragOrigin = wallpaper.offset }
                    )
                    .simultaneousGesture(
                        MagnifyGesture()
                            .onChanged { value in
                                wallpaper.setZoom(zoomOrigin * value.magnification, for: image)
                            }
                            .onEnded { _ in zoomOrigin = wallpaper.zoom }
                    )
                    .onTapGesture(count: 2) {
                        wallpaper.resetTransform()
                        dragOrigin = .zero
                        zoomOrigin = 1
                    }
                    .accessibilityLabel("方形壁纸裁切预览")

                VStack(alignment: .leading, spacing: 6) {
                    Text(String(format: "缩放  %.1f×", wallpaper.zoom))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Slider(value: Binding(
                        get: { wallpaper.zoom },
                        set: { wallpaper.setZoom($0, for: image) }
                    ), in: 1...4, step: 0.1)
                }

                Divider()
                Text("双指缩放 · 单指拖动 · 双击复位")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            .padding(18)
            .bajjiCard()

            Text("实际保存完整方形文件；首页圆形效果仅用于模拟设备显示。")
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
    let device: DeviceConnectionManager
    let accessory: AccessoryManager
    let wallpaper: WallpaperStore
    @State private var phase = Phase.ready
    @State private var progress = 0.0
    @State private var errorMessage: String?
    @State private var transferTask: Task<Void, Never>?

    private enum Phase: Equatable { case ready, sending, validating, success, failure, cancelled }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    Text("图片会通过已配对的加密蓝牙链路发送；设备校验完成前不会替换当前壁纸。")
                        .foregroundStyle(.secondary)

                    VStack(alignment: .leading, spacing: 18) {
                        if let image = wallpaper.currentImage {
                            Image(uiImage: image)
                                .resizable()
                                .scaledToFill()
                                .frame(width: 72, height: 72)
                                .clipShape(.circle)
                        }
                        StatusBadge(statusLabel, color: statusColor)
                        if phase == .sending {
                            ProgressView(value: progress) {
                                Text("正在发送")
                            } currentValueLabel: {
                                Text(progress, format: .percent.precision(.fractionLength(0)))
                            }
                            .tint(.bajjiAccent)
                            .accessibilityValue(progress.formatted(.percent.precision(.fractionLength(0))))
                        }
                        TransferStep(number: "01", title: "准备方形资源", detail: "468×468 无损 PNG 已保存在 App", state: .complete)
                        TransferStep(number: "02", title: "发送到 StopWatch", detail: transferDetail, state: transferStepState)
                        TransferStep(number: "03", title: "校验壁纸文件", detail: "核对大小、CRC、格式与解码预算", state: validationStepState)
                        TransferStep(number: "04", title: "原子替换并确认", detail: "设备回执成功后才更新当前壁纸", state: applyStepState)
                    }
                    .padding(18)
                    .bajjiCard()

                    if !accessory.hasAccessory {
                        Text("先添加 StopWatch；图片仍会保留在 App 中。")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    } else if device.isReady && !supportsWallpaper {
                        Text("当前 StopWatch 固件不支持手机传图，请先升级设备固件。")
                            .font(.footnote)
                            .foregroundStyle(.red)
                    } else if let errorMessage {
                        Label(errorMessage, systemImage: "exclamationmark.triangle.fill")
                            .font(.footnote)
                            .foregroundStyle(.red)
                            .padding(14)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(Color.red.opacity(0.08))
                            .clipShape(.rect(cornerRadius: 14))
                    }

                    Text("未发送或传输失败不会覆盖设备当前壁纸。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)

                    actionButton
                }
                .padding(24)
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .navigationTitle(phase == .success ? "已发送" : "发送壁纸")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("关闭") { dismiss() }
                }
            }
        }
        .task {
            if !wallpaper.needsTransfer, wallpaper.currentImage != nil {
                phase = .success
                progress = 1
            }
        }
        .onDisappear { transferTask?.cancel() }
    }

    @ViewBuilder private var actionButton: some View {
        if phase == .sending || phase == .validating {
            Button("取消传输", role: .cancel) { transferTask?.cancel() }
                .buttonStyle(BajjiOutlineButtonStyle())
        } else if phase == .success {
            Button("完成") { dismiss() }
                .buttonStyle(BajjiPrimaryButtonStyle())
        } else if !device.isReady {
            Button(device.status) {}
            .buttonStyle(BajjiPrimaryButtonStyle())
            .disabled(true)
        } else {
            Button(bridgeReady ? "发送到 StopWatch" : "正在连接 StopWatch…") {
                startTransfer()
            }
            .buttonStyle(BajjiPrimaryButtonStyle())
            .disabled(!bridgeReady || !supportsWallpaper || wallpaper.currentImage == nil)
        }
    }

    private var bridgeReady: Bool {
        device.isReady
    }

    private var supportsWallpaper: Bool {
        device.capabilities & BridgeInfo.wallpaperCapability != 0
    }

    private var statusLabel: String {
        switch phase {
        case .ready: "已准备"
        case .sending: "正在发送"
        case .validating: "设备正在校验"
        case .success: "设备已确认"
        case .failure: "发送失败"
        case .cancelled: "已取消"
        }
    }

    private var statusColor: Color {
        switch phase {
        case .ready, .cancelled: .bajjiWarning
        case .sending, .validating: .bajjiAccent
        case .success: .bajjiSuccess
        case .failure: .red
        }
    }

    private var transferDetail: String {
        phase == .sending ? progress.formatted(.percent.precision(.fractionLength(0))) :
            (phase == .success || phase == .validating ? "全部分块已确认" : "等待开始")
    }

    private var transferStepState: TransferStep.State {
        switch phase {
        case .sending: .active
        case .validating, .success: .complete
        case .failure where progress < 1: .error
        default: .pending
        }
    }

    private var validationStepState: TransferStep.State {
        switch phase {
        case .validating: .active
        case .success: .complete
        case .failure where progress >= 1: .error
        default: .pending
        }
    }

    private var applyStepState: TransferStep.State {
        phase == .success ? .complete : .pending
    }

    private func startTransfer() {
        guard transferTask == nil else { return }
        phase = .sending
        progress = 0
        errorMessage = nil
        transferTask = Task {
            do {
                let data = try wallpaper.transferData()
                try await device.sendWallpaper(data) { stage, value in
                    progress = value
                    switch stage {
                    case .sending: phase = .sending
                    case .validating: phase = .validating
                    case .complete: phase = .success
                    }
                }
                wallpaper.markSent()
            } catch is CancellationError {
                phase = .cancelled
            } catch {
                errorMessage = error.localizedDescription
                phase = .failure
            }
            transferTask = nil
        }
    }
}

private struct TransferStep: View {
    enum State: Equatable { case complete, active, error, pending }
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
        case .active: .bajjiAccent
        case .error: .red
        case .pending: .secondary
        }
    }
}

private struct SettingsHomeView: View {
    let tunnel: TunnelManager
    let device: DeviceConnectionManager
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
                    StopWatchParametersView(device: device, wallpaper: wallpaper)
                } label: {
                    SettingsRowLabel(title: "StopWatch 参数", detail: "亮度、显示与换图")
                }
                NavigationLink {
                    NetworkView(tunnel: tunnel, device: device, accessory: accessory)
                } label: {
                    SettingsRowLabel(title: "网络与 VPN", detail: "用户选择")
                }
                NavigationLink {
                    ImagesHomeView(device: device, accessory: accessory, wallpaper: wallpaper)
                } label: {
                    SettingsRowLabel(
                        title: "图片",
                        detail: wallpaper.currentImage == nil ? "随机壁纸" :
                            (wallpaper.needsTransfer ? "待发送" : "已同步")
                    )
                }
            }

            Section("支持") {
                NavigationLink {
                    DiagnosticsView(tunnel: tunnel, device: device, accessory: accessory)
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
    let device: DeviceConnectionManager
    let wallpaper: WallpaperStore
    @AppStorage("bajji.brightness") private var brightness = 0.72
    @AppStorage("bajji.autoRefresh") private var autoRefresh = true
    @AppStorage("bajji.refreshMinutes") private var refreshMinutes = 30
    @AppStorage("bajji.haptics") private var haptics = true
    @State private var isSyncing = false
    @State private var didLoadDeviceValues = false
    @State private var syncMessage: String?
    @State private var syncFailed = false
    @State private var syncSuccessCount = 0

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

                Toggle("iPhone 操作触感", isOn: $haptics)
            }

            Section {
                parameterAction
                if let syncMessage {
                    Label(syncMessage, systemImage: syncFailed ? "exclamationmark.triangle.fill" : "checkmark.circle.fill")
                        .foregroundStyle(syncFailed ? Color.red : Color.bajjiSuccess)
                }
            } footer: {
                Text("亮度、显示方式与换图间隔会在设备确认后生效；iPhone 操作触感仅保存在本机。")
                    .font(.footnote)
            }
        }
        .navigationTitle("StopWatch 参数")
        .task {
            while !Task.isCancelled {
                if canSync && !didLoadDeviceValues { await loadDeviceValues() }
                try? await Task.sleep(for: .seconds(1))
            }
        }
        .onChange(of: autoRefresh) { _, enabled in
            if enabled && refreshMinutes == 0 { refreshMinutes = 30 }
        }
        .sensoryFeedback(.success, trigger: syncSuccessCount) { _, _ in haptics }
    }

    @ViewBuilder private var parameterAction: some View {
        if !device.isReady {
            Button(device.status) {}
                .disabled(true)
        } else if bridgeReady && !supportsSettings {
            Button("需要升级 StopWatch 固件") {}
                .disabled(true)
        } else {
            Button(bridgeReady ? "应用到 StopWatch" : "正在连接 StopWatch…") {
                Task { await applyDeviceValues() }
            }
            .disabled(!canSync || isSyncing)
        }
    }

    private var bridgeReady: Bool {
        device.isReady
    }

    private var supportsSettings: Bool {
        device.capabilities & BridgeInfo.settingsCapability != 0
    }

    private var canSync: Bool { bridgeReady && supportsSettings }

    private func loadDeviceValues() async {
        guard !isSyncing else { return }
        isSyncing = true
        defer { isSyncing = false }
        do {
            updateForm(try await device.readDeviceSettings())
            didLoadDeviceValues = true
            syncMessage = "已读取 StopWatch 当前参数"
            syncFailed = false
        } catch {
            didLoadDeviceValues = true
            syncMessage = error.localizedDescription
            syncFailed = true
        }
    }

    private func applyDeviceValues() async {
        guard !isSyncing else { return }
        isSyncing = true
        defer { isSyncing = false }
        let settings = DeviceSettings(
            brightnessPercent: UInt8(clamping: Int((brightness * 100).rounded())),
            displayMode: wallpaper.displayMode == .fit ? .fitBlur : .cover,
            autoRefreshMinutes: autoRefresh ? UInt16(clamping: refreshMinutes) : 0
        )
        do {
            updateForm(try await device.applyDeviceSettings(settings))
            syncMessage = "StopWatch 已应用并保存参数"
            syncFailed = false
            syncSuccessCount += 1
        } catch {
            if let current = try? await device.readDeviceSettings() { updateForm(current) }
            syncMessage = error.localizedDescription + " 已重新读取设备当前值。"
            syncFailed = true
        }
    }

    private func updateForm(_ settings: DeviceSettings) {
        brightness = Double(settings.brightnessPercent) / 100
        wallpaper.displayMode = settings.displayMode == .fitBlur ? .fit : .fill
        autoRefresh = settings.autoRefreshMinutes > 0
        refreshMinutes = Int(settings.autoRefreshMinutes)
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
    let device: DeviceConnectionManager
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
                    StatusRow(label: "控制连接", value: device.status)
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
            await device.clearBinding()
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
    var lastSentAt: Date?

    var needsTransfer: Bool {
        guard currentImage != nil, let updatedAt else { return false }
        return lastSentAt.map { $0 < updatedAt } ?? true
    }

    init() {
        if let rawValue = UserDefaults.standard.string(forKey: "bajji.wallpaperDisplayMode"),
           let savedMode = WallpaperDisplayMode(rawValue: rawValue) {
            displayMode = savedMode
        }
        lastSentAt = UserDefaults.standard.object(forKey: "bajji.wallpaperLastSentAt") as? Date
        guard let url = Self.currentImageURL,
              let data = try? Data(contentsOf: url),
              let image = UIImage(data: data) else { return }
        currentImage = image
        updatedAt = (try? url.resourceValues(forKeys: [.contentModificationDateKey]))?
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
        let rendered = WallpaperRenderer.render(image, zoom: zoom, offset: offset)
        guard let data = rendered.pngData() else {
            throw WallpaperError.couldNotEncode
        }
        try FileManager.default.createDirectory(
            at: Self.savedImageURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: Self.savedImageURL, options: .atomic)
        try? FileManager.default.removeItem(at: Self.legacyImageURL)
        currentImage = rendered
        updatedAt = Date()
        draftImage = nil
        resetTransform()
    }

    func discardDraft() {
        draftImage = nil
        resetTransform()
    }

    func transferData() throws -> Data {
        guard let url = Self.currentImageURL else { throw WallpaperError.unreadableImage }
        let data = try Data(contentsOf: url)
        guard !data.isEmpty else { throw WallpaperError.unreadableImage }
        return data
    }

    func markSent() {
        lastSentAt = Date()
        UserDefaults.standard.set(lastSentAt, forKey: "bajji.wallpaperLastSentAt")
    }

    func resetTransform() {
        zoom = 1
        offset = .zero
    }

    func setZoom(_ value: CGFloat, for image: UIImage) {
        zoom = min(4, max(1, value))
        offset = WallpaperRenderer.clampedOffset(offset, imageSize: image.size, zoom: zoom)
    }

    func setOffset(_ value: CGSize, for image: UIImage) {
        offset = WallpaperRenderer.clampedOffset(value, imageSize: image.size, zoom: zoom)
    }

    private static var savedImageURL: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appending(path: "Bajji", directoryHint: .isDirectory)
            .appending(path: "wallpaper-preview.png")
    }

    private static var legacyImageURL: URL {
        savedImageURL.deletingLastPathComponent().appending(path: "wallpaper-preview.jpg")
    }

    private static var currentImageURL: URL? {
        if FileManager.default.fileExists(atPath: savedImageURL.path) { return savedImageURL }
        if FileManager.default.fileExists(atPath: legacyImageURL.path) { return legacyImageURL }
        return nil
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
