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
                    SettingsHomeView(accessory: accessory)
                }
            }
        }
        .tint(.bajjiAccent)
    }
}

private struct DeviceHomeView: View {
    let tunnel: TunnelManager
    let accessory: AccessoryManager

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
                        Text("网络与 VPN")
                            .navigationTitle("网络")
                    }
                    .buttonStyle(BajjiPrimaryButtonStyle())
                } else {
                    emptyCard
                    Button("添加 StopWatch") {
                        Task { await accessory.presentPicker() }
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
    let accessory: AccessoryManager

    var body: some View {
        List {
            Section("STOPWATCH") {
                NavigationLink("StopWatch 参数") {
                    Text("亮度、换图与触感")
                        .navigationTitle("StopWatch 参数")
                }
                NavigationLink("网络与 VPN") {
                    Text("Wi‑Fi 优先")
                        .navigationTitle("网络与 VPN")
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
