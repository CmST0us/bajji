// SPDX-License-Identifier: MIT
#if !SWIFT_PACKAGE && canImport(UIKit)
import Testing
import UIKit

@Suite("Wallpaper media")
@MainActor
struct WallpaperMediaTests {
    @Test func detectsMediaAndKeepsTrimInsideSixSeconds() {
        #expect(WallpaperImportKind.detect([.jpeg]) == .photo)
        #expect(WallpaperImportKind.detect([.quickTimeMovie]) == .video)
        #expect(WallpaperImportKind.detect([.heic, .livePhoto]) == .livePhoto)
        #expect(WallpaperMediaFormat.detect(Data([0x89, 0x50, 0x4E, 0x47])) == .png)
        #expect(WallpaperMediaFormat.detect(Data("GIF89a".utf8)) == .gif)
        #expect(WallpaperMediaFormat.detect(Data([0xFF, 0xD8])) == nil)

        let draft = VideoDraft(
            fileURL: URL(fileURLWithPath: "/tmp/video.mov"), duration: 10,
            previewImage: UIImage()
        )
        #expect(draft.inPoint == 0)
        #expect(draft.outPoint == 6)
        draft.setInPoint(8)
        #expect(draft.inPoint == 8)
        #expect(draft.outPoint == 8.5)
        draft.setOutPoint(20)
        #expect(draft.outPoint == 10)
        draft.setInPoint(1)
        #expect(draft.outPoint == 7)
        #expect(draft.selectedDuration == 6)

        let livePhoto = VideoDraft(
            fileURL: URL(fileURLWithPath: "/tmp/live.mov"), duration: 3,
            previewImage: UIImage(), isLivePhoto: true
        )
        #expect(livePhoto.gifTitle == "实况照片 GIF")
    }

    @Test func recordsOnlySuccessfulSendsAndReusesHistoryOnResend() throws {
        let root = FileManager.default.temporaryDirectory
            .appending(path: "bajji-history-test-\(UUID().uuidString)", directoryHint: .isDirectory)
        let suiteName = "bajji-history-test-\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer {
            try? FileManager.default.removeItem(at: root)
            defaults.removePersistentDomain(forName: suiteName)
        }

        let store = WallpaperStore(directory: root, defaults: defaults)
        store.draftImage = UIGraphicsImageRenderer(size: CGSize(width: 64, height: 64)).image {
            UIColor.systemTeal.setFill()
            $0.fill(CGRect(x: 0, y: 0, width: 64, height: 64))
        }
        try store.saveDraft()
        #expect(store.histories.isEmpty)

        let current = try store.transferPayload(for: .current)
        try store.recordSuccessfulSend(source: .current, payload: current)
        let historyID = try #require(store.histories.first?.id)
        #expect(store.histories.count == 1)
        #expect(store.currentHistoryID == historyID)
        #expect(store.needsTransfer == false)

        try store.recordSuccessfulSend(source: .current, payload: current)
        #expect(store.histories.count == 1)

        let history = try store.transferPayload(for: .history(historyID))
        try store.recordSuccessfulSend(source: .history(historyID), payload: history)
        #expect(store.histories.count == 1)
        #expect(store.currentHistoryID == historyID)
        #expect(try store.transferPayload(for: .current).data == history.data)

        let reloaded = WallpaperStore(directory: root, defaults: defaults)
        #expect(reloaded.histories.count == 1)
        #expect(reloaded.currentHistoryID == historyID)
        #expect(try reloaded.transferPayload(for: .history(historyID)).data == history.data)
    }
}
#endif
