// SPDX-License-Identifier: MIT
import AVFoundation
import ImageIO
import SwiftUI
import UIKit

struct VideoTrimEditorView: View {
    let draft: VideoDraft
    let wallpaper: WallpaperStore
    @Binding var showsTransferStatus: Bool

    @State private var player: AVPlayer
    @State private var isPlaying = true
    @State private var exportProgress = 0.0
    @State private var exportTask: Task<Void, Never>?
    @State private var errorMessage: String?
    private let playbackClock = Timer.publish(every: 0.08, on: .main, in: .common).autoconnect()

    init(draft: VideoDraft, wallpaper: WallpaperStore,
         showsTransferStatus: Binding<Bool>) {
        self.draft = draft
        self.wallpaper = wallpaper
        _showsTransferStatus = showsTransferStatus
        _player = State(initialValue: AVPlayer(url: draft.fileURL))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("选择\(draft.sourceLabel)的入点与出点。截选片段会循环预览，并导出为方形 GIF。")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 16) {
                ZStack {
                    SquareVideoPlayer(player: player)
                        .frame(maxWidth: .infinity)
                        .aspectRatio(1, contentMode: .fit)
                        .clipShape(.rect(cornerRadius: 16))

                    Button {
                        isPlaying.toggle()
                        isPlaying ? player.play() : player.pause()
                    } label: {
                        Image(systemName: isPlaying ? "pause.fill" : "play.fill")
                            .font(.title2)
                            .foregroundStyle(.white)
                            .frame(width: 56, height: 56)
                            .background(.black.opacity(0.58), in: .circle)
                    }
                    .accessibilityLabel(isPlaying ? "暂停循环预览" : "播放循环预览")
                }
                .overlay {
                    RoundedRectangle(cornerRadius: 16)
                        .stroke(Color.bajjiAccent, lineWidth: 3)
                }

                selectionTrack

                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text("入点")
                        Spacer()
                        Text("\(draft.inPoint, format: .number.precision(.fractionLength(1))) 秒")
                    }
                    .font(.subheadline)
                    Slider(value: Binding(
                        get: { draft.inPoint },
                        set: { value in
                            draft.setInPoint(value)
                            seekToInPoint()
                        }
                    ), in: 0...max(0.1, draft.duration - draft.minimumDuration), step: 0.1)
                    .accessibilityLabel("GIF 入点")
                }

                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text("出点")
                        Spacer()
                        Text("\(draft.outPoint, format: .number.precision(.fractionLength(1))) 秒")
                    }
                    .font(.subheadline)
                    Slider(value: Binding(
                        get: { draft.outPoint },
                        set: { value in
                            draft.setOutPoint(value)
                            seekToInPoint()
                        }
                    ), in: draft.inPoint + draft.minimumDuration...draft.maximumOutPoint, step: 0.1)
                    .accessibilityLabel("GIF 出点")
                }

                Divider()
                HStack {
                    Label("片段 \(durationLabel)", systemImage: "timeline.selection")
                    Spacer()
                    Text("最长 6 秒")
                        .foregroundStyle(.secondary)
                }
                .font(.subheadline)
            }
            .padding(18)
            .bajjiCard()

            VStack(alignment: .leading, spacing: 6) {
                Text("导出规格")
                    .font(.headline)
                Text("468×468 · 优先 12 fps · 循环播放 · 最大 3 MB")
                    .foregroundStyle(.secondary)
                Text("复杂视频超限时会自动降低帧率；原视频不会被修改。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            .padding(16)
            .background(Color(uiColor: .tertiarySystemGroupedBackground))
            .clipShape(.rect(cornerRadius: 16))

            if exportTask != nil {
                ProgressView(value: exportProgress) {
                    Text("正在导出 GIF")
                } currentValueLabel: {
                    Text(exportProgress, format: .percent.precision(.fractionLength(0)))
                }
                .tint(.bajjiAccent)
            }

            Button(exportTask == nil ? "导出 GIF 并发送" : "正在导出…") {
                startExport()
            }
            .buttonStyle(BajjiPrimaryButtonStyle())
            .disabled(exportTask != nil)

            Button("重新选择") { wallpaper.discardDraft() }
                .buttonStyle(BajjiOutlineButtonStyle())
                .disabled(exportTask != nil)
        }
        .padding(24)
        .onAppear {
            seekToInPoint()
            player.play()
        }
        .onReceive(playbackClock) { _ in
            let seconds = player.currentTime().seconds
            if seconds.isFinite && (seconds >= draft.outPoint || seconds < draft.inPoint) {
                seekToInPoint()
            }
        }
        .onDisappear {
            player.pause()
            exportTask?.cancel()
        }
        .alert("无法导出 GIF", isPresented: Binding(
            get: { errorMessage != nil },
            set: { if !$0 { errorMessage = nil } }
        )) {
            Button("好", role: .cancel) {}
        } message: {
            Text(errorMessage ?? "未知错误")
        }
    }

    private var selectionTrack: some View {
        GeometryReader { proxy in
            let width = proxy.size.width
            let start = width * draft.inPoint / draft.duration
            let end = width * draft.outPoint / draft.duration
            ZStack(alignment: .leading) {
                Capsule().fill(Color.secondary.opacity(0.2))
                Capsule()
                    .fill(Color.bajjiAccent)
                    .frame(width: max(8, end - start))
                    .offset(x: start)
                Circle().fill(.white).stroke(Color.bajjiAccent, lineWidth: 3)
                    .frame(width: 24, height: 24)
                    .offset(x: max(0, start - 12))
                Circle().fill(.white).stroke(Color.bajjiAccent, lineWidth: 3)
                    .frame(width: 24, height: 24)
                    .offset(x: min(width - 24, end - 12))
            }
        }
        .frame(height: 24)
        .accessibilityHidden(true)
    }

    private var durationLabel: String {
        String(format: "%.1f 秒", draft.selectedDuration)
    }

    private func seekToInPoint() {
        player.seek(
            to: CMTime(seconds: draft.inPoint, preferredTimescale: 600),
            toleranceBefore: .zero, toleranceAfter: .zero
        )
        if isPlaying { player.play() }
    }

    private func startExport() {
        guard exportTask == nil else { return }
        exportProgress = 0
        errorMessage = nil
        exportTask = Task {
            do {
                let result = try await VideoGIFExporter.shared.export(
                    fileURL: draft.fileURL, inPoint: draft.inPoint, outPoint: draft.outPoint
                ) { exportProgress = $0 }
                try wallpaper.saveGIF(result, title: draft.gifTitle)
                exportTask = nil
                showsTransferStatus = true
            } catch is CancellationError {
                exportTask = nil
            } catch {
                errorMessage = error.localizedDescription
                exportTask = nil
            }
        }
    }
}

struct WallpaperHistoryView: View {
    @Environment(\.dismiss) private var dismiss
    let device: DeviceConnectionManager
    let accessory: AccessoryManager
    let wallpaper: WallpaperStore

    var body: some View {
        NavigationStack {
            Group {
                if wallpaper.histories.isEmpty {
                    ContentUnavailableView(
                        "还没有发送记录",
                        systemImage: "clock.arrow.circlepath",
                        description: Text("图片或 GIF 只有在 StopWatch 确认接收成功后才会保存在这里。")
                    )
                } else {
                    List(wallpaper.histories) { item in
                        NavigationLink {
                            WallpaperHistoryDetailView(
                                itemID: item.id, device: device,
                                accessory: accessory, wallpaper: wallpaper
                            )
                        } label: {
                            WallpaperHistoryRow(item: item, wallpaper: wallpaper)
                        }
                    }
                    .listStyle(.insetGrouped)
                }
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .navigationTitle("发送历史")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("关闭") { dismiss() }
                }
            }
        }
    }
}

private struct WallpaperHistoryRow: View {
    let item: WallpaperHistoryItem
    let wallpaper: WallpaperStore

    var body: some View {
        HStack(spacing: 14) {
            Group {
                if let image = wallpaper.historyPreview(for: item) {
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFill()
                } else {
                    Image(systemName: "photo")
                        .foregroundStyle(.secondary)
                }
            }
            .frame(width: 72, height: 72)
            .background(Color(uiColor: .tertiarySystemGroupedBackground))
            .clipShape(.rect(cornerRadius: 14))

            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text(item.title)
                        .font(.body.weight(.semibold))
                    Spacer()
                    Text(item.format.label)
                        .font(.caption2.weight(.semibold))
                        .padding(.horizontal, 9)
                        .padding(.vertical, 4)
                        .background(Color.bajjiAccent.opacity(0.12), in: .capsule)
                        .foregroundStyle(Color.bajjiAccent)
                }
                Text(item.sentAt.formatted(date: .abbreviated, time: .shortened))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text(item.detail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(minHeight: 80)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(item.title)，\(item.format.label)，\(item.detail)")
    }
}

private struct WallpaperHistoryDetailView: View {
    let itemID: UUID
    let device: DeviceConnectionManager
    let accessory: AccessoryManager
    let wallpaper: WallpaperStore
    @State private var transferSource: WallpaperTransferSource?

    var body: some View {
        Group {
            if let item = wallpaper.historyItem(id: itemID) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        Text("已保存在此 iPhone，可再次发送。")
                            .foregroundStyle(.secondary)

                        VStack(alignment: .leading, spacing: 16) {
                            StatusBadge(item.format.label, color: .bajjiAccent)
                            WallpaperMediaPreview(
                                preview: wallpaper.historyPreview(for: item),
                                gifData: item.format == .gif ? wallpaper.historyData(for: item) : nil
                            )
                            .frame(maxWidth: .infinity)
                            .aspectRatio(1, contentMode: .fit)
                            .clipShape(.rect(cornerRadius: 16))

                            Text(item.title)
                                .font(.title2.weight(.semibold))
                            Text(item.detail)
                                .foregroundStyle(.secondary)
                            Divider()
                            Text("发送于 \(item.sentAt.formatted(date: .abbreviated, time: .shortened)) · StopWatch 已确认")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        .padding(18)
                        .bajjiCard()

                        Text("再次发送失败不会覆盖设备当前壁纸；历史文件仍保留。")
                            .font(.footnote)
                            .foregroundStyle(.secondary)

                        Button("再次发送到 StopWatch") {
                            transferSource = .history(item.id)
                        }
                        .buttonStyle(BajjiPrimaryButtonStyle())
                    }
                    .padding(24)
                }
                .background(Color(uiColor: .systemGroupedBackground))
            } else {
                ContentUnavailableView("记录不可用", systemImage: "exclamationmark.triangle")
            }
        }
        .navigationTitle("发送记录")
        .navigationBarTitleDisplayMode(.inline)
        .sheet(item: $transferSource) { source in
            WallpaperTransferStatusView(
                device: device, accessory: accessory,
                wallpaper: wallpaper, source: source
            )
        }
    }
}

struct WallpaperMediaPreview: View {
    let preview: UIImage?
    let gifData: Data?

    var body: some View {
        Group {
            if let gifData {
                AnimatedGIFView(data: gifData)
            } else if let preview {
                Image(uiImage: preview)
                    .resizable()
                    .scaledToFill()
            } else {
                Image(systemName: "photo")
                    .resizable()
                    .scaledToFit()
                    .padding(64)
                    .foregroundStyle(.secondary)
                    .background(Color(uiColor: .tertiarySystemGroupedBackground))
            }
        }
        .clipped()
    }
}

private struct SquareVideoPlayer: UIViewRepresentable {
    let player: AVPlayer

    func makeUIView(context: Context) -> PlayerView {
        let view = PlayerView()
        view.playerLayer.player = player
        view.playerLayer.videoGravity = .resizeAspectFill
        return view
    }

    func updateUIView(_ view: PlayerView, context: Context) {
        view.playerLayer.player = player
    }
}

private final class PlayerView: UIView {
    override class var layerClass: AnyClass { AVPlayerLayer.self }
    var playerLayer: AVPlayerLayer { layer as! AVPlayerLayer }
}

private struct AnimatedGIFView: UIViewRepresentable {
    let data: Data

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeUIView(context: Context) -> UIImageView {
        let view = UIImageView()
        view.contentMode = .scaleAspectFill
        view.clipsToBounds = true
        return view
    }

    func updateUIView(_ view: UIImageView, context: Context) {
        let fingerprint = data.hashValue
        guard context.coordinator.fingerprint != fingerprint else { return }
        context.coordinator.fingerprint = fingerprint
        view.image = Self.animatedImage(data: data)
        view.startAnimating()
    }

    final class Coordinator {
        var fingerprint: Int?
    }

    private static func animatedImage(data: Data) -> UIImage? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil) else { return nil }
        let count = CGImageSourceGetCount(source)
        guard count > 1 else { return UIImage(data: data) }
        var images: [UIImage] = []
        var duration = 0.0
        images.reserveCapacity(count)
        for index in 0..<count {
            guard let image = CGImageSourceCreateImageAtIndex(source, index, nil) else { continue }
            images.append(UIImage(cgImage: image))
            let properties = CGImageSourceCopyPropertiesAtIndex(source, index, nil)
                as? [CFString: Any]
            let gif = properties?[kCGImagePropertyGIFDictionary] as? [CFString: Any]
            duration += (gif?[kCGImagePropertyGIFUnclampedDelayTime] as? Double) ??
                (gif?[kCGImagePropertyGIFDelayTime] as? Double) ?? 0.1
        }
        guard !images.isEmpty else { return nil }
        return UIImage.animatedImage(with: images, duration: max(0.1, duration))
    }
}

private extension WallpaperHistoryItem {
    var detail: String {
        var parts = ["468×468 \(format.label)"]
        if let duration { parts.append(String(format: "%.1f 秒", duration)) }
        if let frameRate { parts.append("\(frameRate) fps") }
        parts.append(ByteCountFormatter.string(
            fromByteCount: Int64(byteCount), countStyle: .file
        ))
        return parts.joined(separator: " · ")
    }
}
