// SPDX-License-Identifier: MIT
import AVFoundation
import CoreTransferable
import Foundation
import ImageIO
import Observation
import Photos
import PhotosUI
import SwiftUI
import UniformTypeIdentifiers
import UIKit

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

enum WallpaperMediaFormat: String, Codable, Sendable {
    case png
    case gif

    var label: String { rawValue.uppercased() }
    var fileExtension: String { rawValue }

    static func detect(_ data: Data) -> Self? {
        if data.starts(with: [0x89, 0x50, 0x4E, 0x47]) { return .png }
        if data.starts(with: Array("GIF87a".utf8)) ||
            data.starts(with: Array("GIF89a".utf8)) { return .gif }
        return nil
    }
}

enum WallpaperImportKind: Equatable {
    case photo
    case video
    case livePhoto

    static func detect(_ contentTypes: [UTType]) -> Self {
        if contentTypes.contains(where: { $0.conforms(to: .livePhoto) }) { return .livePhoto }
        if contentTypes.contains(where: { $0.conforms(to: .movie) }) { return .video }
        return .photo
    }
}

struct WallpaperHistoryItem: Identifiable, Codable, Hashable, Sendable {
    let id: UUID
    var title: String
    var format: WallpaperMediaFormat
    var sentAt: Date
    var byteCount: Int
    var duration: Double?
    var frameRate: Int?
}

enum WallpaperTransferSource: Identifiable, Equatable {
    case current
    case history(UUID)

    var id: String {
        switch self {
        case .current: "current"
        case let .history(id): "history-\(id.uuidString)"
        }
    }
}

struct WallpaperTransferPayload {
    let data: Data
    let preview: UIImage
    let title: String
    let format: WallpaperMediaFormat
    let duration: Double?
    let frameRate: Int?
}

struct GIFExportResult {
    let data: Data
    let preview: UIImage
    let duration: Double
    let frameRate: Int
}

@MainActor
@Observable
final class VideoDraft {
    static let maximumDuration = 6.0

    let fileURL: URL
    let duration: Double
    let previewImage: UIImage
    let isLivePhoto: Bool
    var inPoint = 0.0
    var outPoint: Double

    init(fileURL: URL, duration: Double, previewImage: UIImage, isLivePhoto: Bool = false) {
        self.fileURL = fileURL
        self.duration = duration
        self.previewImage = previewImage
        self.isLivePhoto = isLivePhoto
        outPoint = min(duration, Self.maximumDuration)
    }

    var sourceLabel: String { isLivePhoto ? "实况照片" : "视频" }
    var gifTitle: String { "\(sourceLabel) GIF" }
    var selectedDuration: Double { outPoint - inPoint }
    var minimumDuration: Double { min(0.5, duration) }
    var maximumOutPoint: Double { min(duration, inPoint + Self.maximumDuration) }

    func setInPoint(_ value: Double) {
        inPoint = min(max(0, value), max(0, duration - minimumDuration))
        outPoint = min(maximumOutPoint, max(outPoint, inPoint + minimumDuration))
    }

    func setOutPoint(_ value: Double) {
        outPoint = min(maximumOutPoint, max(inPoint + minimumDuration, value))
    }
}

private struct PickedVideo: Transferable {
    let fileURL: URL

    static var transferRepresentation: some TransferRepresentation {
        FileRepresentation(importedContentType: .movie) { received in
            let destination = FileManager.default.temporaryDirectory
                .appending(path: "bajji-video-\(UUID().uuidString).mov")
            try FileManager.default.copyItem(at: received.file, to: destination)
            return Self(fileURL: destination)
        }
    }
}

actor VideoGIFExporter {
    static let shared = VideoGIFExporter()
    private let candidateFrameRates = [12, 8, 6, 4]

    func export(fileURL: URL, inPoint: Double, outPoint: Double,
                progress: @MainActor @escaping @Sendable (Double) -> Void) async throws
        -> GIFExportResult {
        let duration = outPoint - inPoint
        guard duration > 0 else { throw WallpaperError.videoTooShort }

        for (attempt, frameRate) in candidateFrameRates.enumerated() {
            try Task.checkCancellation()
            let result = try await render(
                fileURL: fileURL, inPoint: inPoint, duration: duration,
                frameRate: frameRate
            ) { value in
                let base = Double(attempt) / Double(self.candidateFrameRates.count)
                let span = 1 / Double(self.candidateFrameRates.count)
                await progress(base + value * span)
            }
            if result.data.count <= WallpaperStore.maximumTransferBytes {
                await progress(1)
                return GIFExportResult(
                    data: result.data, preview: result.preview,
                    duration: duration, frameRate: frameRate
                )
            }
        }
        throw WallpaperError.gifTooLarge
    }

    private func render(fileURL: URL, inPoint: Double, duration: Double, frameRate: Int,
                        progress: @escaping @Sendable (Double) async -> Void) async throws
        -> (data: Data, preview: UIImage) {
        let frameCount = max(1, Int(ceil(duration * Double(frameRate))))
        let output = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(
            output as CFMutableData, UTType.gif.identifier as CFString, frameCount, nil
        ) else {
            throw WallpaperError.couldNotEncode
        }
        CGImageDestinationSetProperties(destination, [
            kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFLoopCount: 0]
        ] as CFDictionary)

        let asset = AVURLAsset(url: fileURL)
        let generator = AVAssetImageGenerator(asset: asset)
        generator.appliesPreferredTrackTransform = true
        generator.maximumSize = CGSize(width: 936, height: 936)
        generator.requestedTimeToleranceBefore = .zero
        generator.requestedTimeToleranceAfter = .zero
        let delay = 1 / Double(frameRate)
        let frameProperties = [
            kCGImagePropertyGIFDictionary: [
                kCGImagePropertyGIFDelayTime: delay,
                kCGImagePropertyGIFUnclampedDelayTime: delay
            ]
        ] as CFDictionary
        var preview: UIImage?

        for index in 0..<frameCount {
            try Task.checkCancellation()
            let seconds = inPoint + min(duration - 0.001, Double(index) / Double(frameRate))
            let (image, _) = try await generator.image(
                at: CMTime(seconds: max(inPoint, seconds), preferredTimescale: 600)
            )
            let rendered = WallpaperRenderer.render(
                UIImage(cgImage: image), zoom: 1, offset: .zero
            )
            guard let cgImage = rendered.cgImage else { throw WallpaperError.couldNotEncode }
            if preview == nil { preview = rendered }
            CGImageDestinationAddImage(destination, cgImage, frameProperties)
            await progress(Double(index + 1) / Double(frameCount))
        }

        guard CGImageDestinationFinalize(destination),
              let preview,
              WallpaperMediaFormat.detect(output as Data) == .gif else {
            throw WallpaperError.couldNotEncode
        }
        return (output as Data, preview)
    }
}

@MainActor
@Observable
final class WallpaperStore {
    nonisolated static let maximumTransferBytes = 3 * 1024 * 1024
    private static let maximumImportBytes = 40 * 1024 * 1024
    private static let maximumPixels: CGFloat = 50_000_000

    @ObservationIgnored private let directory: URL
    @ObservationIgnored private let defaults: UserDefaults
    @ObservationIgnored private var historyPreviews: [UUID: UIImage] = [:]

    private(set) var histories: [WallpaperHistoryItem] = []
    var currentImage: UIImage?
    var draftImage: UIImage?
    var draftVideo: VideoDraft?
    var currentFormat: WallpaperMediaFormat = .png
    var currentTitle = "我的图片"
    var currentDuration: Double?
    var currentFrameRate: Int?
    private(set) var currentHistoryID: UUID?
    var displayMode: WallpaperDisplayMode = .fill {
        didSet { defaults.set(displayMode.rawValue, forKey: "bajji.wallpaperDisplayMode") }
    }
    var zoom: CGFloat = 1
    var offset: CGSize = .zero
    var updatedAt: Date?
    var lastSentAt: Date?

    var needsTransfer: Bool {
        guard currentImage != nil, let updatedAt else { return false }
        return lastSentAt.map { $0 < updatedAt } ?? true
    }

    init(directory: URL? = nil, defaults: UserDefaults = .standard) {
        self.directory = directory ?? FileManager.default.urls(
            for: .applicationSupportDirectory, in: .userDomainMask
        )[0].appending(path: "Bajji", directoryHint: .isDirectory)
        self.defaults = defaults
        if let rawValue = defaults.string(forKey: "bajji.wallpaperDisplayMode"),
           let savedMode = WallpaperDisplayMode(rawValue: rawValue) {
            displayMode = savedMode
        }
        lastSentAt = defaults.object(forKey: "bajji.wallpaperLastSentAt") as? Date
        loadHistory()
        loadCurrent()
    }

    func importMedia(_ item: PhotosPickerItem) async throws {
        let kind = WallpaperImportKind.detect(item.supportedContentTypes)
        // Live Photos can prefer an image UTI, so request PHLivePhoto before routing by UTI.
        // Source: developer.apple.com/documentation/photokit/selecting-photos-and-videos-in-ios
        if let livePhoto = try await item.loadTransferable(type: PHLivePhoto.self) {
            try await importLivePhoto(livePhoto)
            return
        }
        switch kind {
        case .photo: try await importPhoto(item)
        case .video: try await importVideo(item)
        case .livePhoto: throw WallpaperError.unreadableLivePhoto
        }
    }

    func saveDraft() throws {
        guard let image = draftImage else { throw WallpaperError.unreadableImage }
        let rendered = WallpaperRenderer.render(image, zoom: zoom, offset: offset)
        guard let data = rendered.pngData() else { throw WallpaperError.couldNotEncode }
        try saveCurrent(
            data: data, preview: rendered, title: "我的图片", format: .png,
            duration: nil, frameRate: nil
        )
        draftImage = nil
        resetTransform()
    }

    func saveGIF(_ result: GIFExportResult, title: String = "视频 GIF") throws {
        guard result.data.count <= Self.maximumTransferBytes else {
            throw WallpaperError.gifTooLarge
        }
        try saveCurrent(
            data: result.data, preview: result.preview, title: title, format: .gif,
            duration: result.duration, frameRate: result.frameRate
        )
        discardDraft()
    }

    func discardDraft() {
        draftImage = nil
        if let fileURL = draftVideo?.fileURL { try? FileManager.default.removeItem(at: fileURL) }
        draftVideo = nil
        resetTransform()
    }

    func transferPayload(for source: WallpaperTransferSource) throws -> WallpaperTransferPayload {
        switch source {
        case .current:
            guard let url = currentMediaURL,
                  let currentImage else { throw WallpaperError.unreadableImage }
            let data = try Data(contentsOf: url)
            guard !data.isEmpty else { throw WallpaperError.unreadableImage }
            return WallpaperTransferPayload(
                data: data, preview: currentImage, title: currentTitle,
                format: currentFormat, duration: currentDuration, frameRate: currentFrameRate
            )
        case let .history(id):
            guard let item = historyItem(id: id) else { throw WallpaperError.historyMissing }
            let data = try Data(contentsOf: historyMediaURL(for: item))
            guard let preview = historyPreview(for: item), !data.isEmpty else {
                throw WallpaperError.historyMissing
            }
            return WallpaperTransferPayload(
                data: data, preview: preview, title: item.title, format: item.format,
                duration: item.duration, frameRate: item.frameRate
            )
        }
    }

    func recordSuccessfulSend(source: WallpaperTransferSource,
                              payload: WallpaperTransferPayload) throws {
        let sentAt = Date()
        let id: UUID
        switch source {
        case .current:
            id = currentHistoryID ?? UUID()
        case let .history(historyID):
            guard historyItem(id: historyID) != nil else { throw WallpaperError.historyMissing }
            id = historyID
            try saveCurrent(
                data: payload.data, preview: payload.preview, title: payload.title,
                format: payload.format, duration: payload.duration,
                frameRate: payload.frameRate, updatedAt: sentAt, historyID: id
            )
        }

        try FileManager.default.createDirectory(
            at: historyDirectory, withIntermediateDirectories: true
        )
        try payload.data.write(to: historyMediaURL(id: id, format: payload.format), options: .atomic)
        guard let previewData = payload.preview.pngData() else {
            throw WallpaperError.couldNotEncode
        }
        try previewData.write(to: historyPreviewURL(id: id), options: .atomic)
        histories.removeAll { $0.id == id }
        histories.insert(WallpaperHistoryItem(
            id: id, title: payload.title, format: payload.format, sentAt: sentAt,
            byteCount: payload.data.count, duration: payload.duration,
            frameRate: payload.frameRate
        ), at: 0)
        historyPreviews[id] = payload.preview
        currentHistoryID = id
        lastSentAt = sentAt
        defaults.set(sentAt, forKey: "bajji.wallpaperLastSentAt")
        try persistHistory()
        try persistCurrentMetadata()
    }

    func historyItem(id: UUID) -> WallpaperHistoryItem? {
        histories.first { $0.id == id }
    }

    func historyPreview(for item: WallpaperHistoryItem) -> UIImage? {
        if let cached = historyPreviews[item.id] { return cached }
        guard let data = try? Data(contentsOf: historyPreviewURL(id: item.id)),
              let image = UIImage(data: data) else { return nil }
        historyPreviews[item.id] = image
        return image
    }

    func historyData(for item: WallpaperHistoryItem) -> Data? {
        try? Data(contentsOf: historyMediaURL(for: item))
    }

    func currentData() -> Data? {
        guard let currentMediaURL else { return nil }
        return try? Data(contentsOf: currentMediaURL)
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

    private func importPhoto(_ item: PhotosPickerItem) async throws {
        guard let data = try await item.loadTransferable(type: Data.self) else {
            throw WallpaperError.unreadableImage
        }
        guard data.count <= Self.maximumImportBytes,
              let image = UIImage(data: data),
              image.size.width * image.scale * image.size.height * image.scale <=
                Self.maximumPixels else {
            throw WallpaperError.imageTooLarge
        }
        if let fileURL = draftVideo?.fileURL { try? FileManager.default.removeItem(at: fileURL) }
        draftVideo = nil
        draftImage = image
        resetTransform()
    }

    private func importVideo(_ item: PhotosPickerItem) async throws {
        guard let picked = try await item.loadTransferable(type: PickedVideo.self) else {
            throw WallpaperError.unreadableVideo
        }
        do {
            try await prepareVideoDraft(fileURL: picked.fileURL, isLivePhoto: false)
        } catch {
            try? FileManager.default.removeItem(at: picked.fileURL)
            throw error
        }
    }

    private func importLivePhoto(_ livePhoto: PHLivePhoto) async throws {
        let resources = PHAssetResource.assetResources(for: livePhoto)
        guard let video = resources.first(where: { $0.type == .fullSizePairedVideo }) ??
                resources.first(where: { $0.type == .pairedVideo }) else {
            throw WallpaperError.unreadableLivePhoto
        }
        let destination = FileManager.default.temporaryDirectory
            .appending(path: "bajji-live-photo-\(UUID().uuidString).mov")
        let options = PHAssetResourceRequestOptions()
        options.isNetworkAccessAllowed = true
        do {
            try await PHAssetResourceManager.default().writeData(
                for: video, toFile: destination, options: options
            )
            try await prepareVideoDraft(fileURL: destination, isLivePhoto: true)
        } catch {
            try? FileManager.default.removeItem(at: destination)
            throw error
        }
    }

    private func prepareVideoDraft(fileURL: URL, isLivePhoto: Bool) async throws {
        let asset = AVURLAsset(url: fileURL)
        async let durationValue = asset.load(.duration)
        async let tracksValue = asset.loadTracks(withMediaType: .video)
        let (duration, tracks) = try await (durationValue, tracksValue)
        guard !tracks.isEmpty, duration.seconds.isFinite, duration.seconds >= 0.1 else {
            throw WallpaperError.videoTooShort
        }
        let generator = AVAssetImageGenerator(asset: asset)
        generator.appliesPreferredTrackTransform = true
        generator.maximumSize = CGSize(width: 936, height: 936)
        let (image, _) = try await generator.image(at: .zero)
        draftImage = nil
        if let oldURL = draftVideo?.fileURL { try? FileManager.default.removeItem(at: oldURL) }
        draftVideo = VideoDraft(
            fileURL: fileURL, duration: duration.seconds,
            previewImage: UIImage(cgImage: image), isLivePhoto: isLivePhoto
        )
    }

    private func saveCurrent(data: Data, preview: UIImage, title: String,
                             format: WallpaperMediaFormat, duration: Double?, frameRate: Int?,
                             updatedAt: Date = Date(), historyID: UUID? = nil) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try data.write(to: currentMediaURL(format: format), options: .atomic)
        guard let previewData = preview.pngData() else { throw WallpaperError.couldNotEncode }
        try previewData.write(to: currentPreviewURL, options: .atomic)
        for otherFormat in [WallpaperMediaFormat.png, .gif] where otherFormat != format {
            try? FileManager.default.removeItem(at: currentMediaURL(format: otherFormat))
        }
        try? FileManager.default.removeItem(at: legacyImageURL)
        currentImage = preview
        currentFormat = format
        currentTitle = title
        currentDuration = duration
        currentFrameRate = frameRate
        currentHistoryID = historyID
        self.updatedAt = updatedAt
        try persistCurrentMetadata()
    }

    private func loadCurrent() {
        let metadata = (try? Data(contentsOf: currentMetadataURL)).flatMap {
            try? JSONDecoder().decode(CurrentMetadata.self, from: $0)
        }
        let format: WallpaperMediaFormat
        let url: URL
        if let metadata,
           FileManager.default.fileExists(atPath: currentMediaURL(format: metadata.format).path) {
            format = metadata.format
            url = currentMediaURL(format: metadata.format)
        } else if FileManager.default.fileExists(atPath: currentMediaURL(format: .gif).path) {
            format = .gif
            url = currentMediaURL(format: .gif)
        } else if FileManager.default.fileExists(atPath: currentMediaURL(format: .png).path) {
            format = .png
            url = currentMediaURL(format: .png)
        } else if FileManager.default.fileExists(atPath: legacyImageURL.path) {
            format = .png
            url = legacyImageURL
        } else {
            return
        }
        guard let data = try? Data(contentsOf: url),
              let image = loadCurrentPreview(fallback: data) else { return }
        currentImage = image
        currentFormat = format
        currentTitle = metadata?.title ?? (format == .gif ? "视频 GIF" : "我的图片")
        currentDuration = metadata?.duration
        currentFrameRate = metadata?.frameRate
        currentHistoryID = metadata?.historyID
        updatedAt = metadata?.updatedAt ?? (try? url.resourceValues(
            forKeys: [.contentModificationDateKey]
        ))?.contentModificationDate
    }

    private func loadHistory() {
        guard let data = try? Data(contentsOf: historyIndexURL),
              let decoded = try? JSONDecoder().decode([WallpaperHistoryItem].self, from: data)
        else { return }
        histories = decoded.filter {
            FileManager.default.fileExists(atPath: historyMediaURL(for: $0).path)
        }.sorted { $0.sentAt > $1.sentAt }
        for item in histories {
            guard let data = try? Data(contentsOf: historyPreviewURL(id: item.id)),
                  let image = UIImage(data: data) else { continue }
            historyPreviews[item.id] = image
        }
    }

    private func loadCurrentPreview(fallback data: Data) -> UIImage? {
        if let previewData = try? Data(contentsOf: currentPreviewURL),
           let preview = UIImage(data: previewData) { return preview }
        return UIImage(data: data)
    }

    private func persistHistory() throws {
        let data = try JSONEncoder().encode(histories)
        try data.write(to: historyIndexURL, options: .atomic)
    }

    private func persistCurrentMetadata() throws {
        guard currentImage != nil, let updatedAt else { return }
        let metadata = CurrentMetadata(
            format: currentFormat, title: currentTitle, duration: currentDuration,
            frameRate: currentFrameRate, updatedAt: updatedAt, historyID: currentHistoryID
        )
        let data = try JSONEncoder().encode(metadata)
        try data.write(to: currentMetadataURL, options: .atomic)
    }

    private var currentMediaURL: URL? {
        let url = currentMediaURL(format: currentFormat)
        if FileManager.default.fileExists(atPath: url.path) { return url }
        if FileManager.default.fileExists(atPath: legacyImageURL.path) { return legacyImageURL }
        return nil
    }

    private func currentMediaURL(format: WallpaperMediaFormat) -> URL {
        directory.appending(path: "wallpaper-preview.\(format.fileExtension)")
    }

    private var currentPreviewURL: URL {
        directory.appending(path: "wallpaper-preview-thumbnail.png")
    }

    private var currentMetadataURL: URL {
        directory.appending(path: "wallpaper-preview.json")
    }

    private var legacyImageURL: URL {
        directory.appending(path: "wallpaper-preview.jpg")
    }

    private var historyDirectory: URL {
        directory.appending(path: "SentHistory", directoryHint: .isDirectory)
    }

    private var historyIndexURL: URL {
        historyDirectory.appending(path: "index.json")
    }

    private func historyMediaURL(for item: WallpaperHistoryItem) -> URL {
        historyMediaURL(id: item.id, format: item.format)
    }

    private func historyMediaURL(id: UUID, format: WallpaperMediaFormat) -> URL {
        historyDirectory.appending(path: "\(id.uuidString).\(format.fileExtension)")
    }

    private func historyPreviewURL(id: UUID) -> URL {
        historyDirectory.appending(path: "\(id.uuidString)-preview.png")
    }
}

private struct CurrentMetadata: Codable {
    let format: WallpaperMediaFormat
    let title: String
    let duration: Double?
    let frameRate: Int?
    let updatedAt: Date
    let historyID: UUID?
}

enum WallpaperError: LocalizedError {
    case unreadableImage
    case imageTooLarge
    case unreadableVideo
    case unreadableLivePhoto
    case videoTooShort
    case couldNotEncode
    case gifTooLarge
    case historyMissing

    var errorDescription: String? {
        switch self {
        case .unreadableImage: "无法读取所选照片。"
        case .imageTooLarge: "图片过大，请选择小于 40 MB、5000 万像素的照片。"
        case .unreadableVideo: "无法读取所选视频。"
        case .unreadableLivePhoto: "无法读取所选实况照片的动态片段。"
        case .videoTooShort: "视频太短，无法生成 GIF。"
        case .couldNotEncode: "无法生成 StopWatch 预览资源。"
        case .gifTooLarge: "GIF 超过 3 MB，请缩短入点与出点之间的时长。"
        case .historyMissing: "这条发送记录的本机文件已不存在。"
        }
    }
}
