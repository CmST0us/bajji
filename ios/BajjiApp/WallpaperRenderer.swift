// SPDX-License-Identifier: MIT
import UIKit

enum WallpaperRenderer {
    // Preserve the edited crop, with its orientation already normalized by the existing
    // renderer. GIF/video uses its preview frame, explicitly described before sending.
    static func passportPayload(_ source: WallpaperTransferPayload) throws -> WallpaperTransferPayload {
        let image = source.preview
        guard image.size.width > 0, image.size.height > 0 else { throw WallpaperError.couldNotEncode }
        let scale = min(120 / image.size.width, 160 / image.size.height)
        let size = CGSize(width: max(1, floor(image.size.width * scale)),
                          height: max(1, floor(image.size.height * scale)))
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        format.opaque = true
        format.preferredRange = .standard
        let preview = UIGraphicsImageRenderer(size: size, format: format).image { context in
            UIColor.black.setFill()
            context.fill(CGRect(origin: .zero, size: size))
            image.draw(in: CGRect(origin: .zero, size: size))
        }
        guard let bytes = preview.jpegData(compressionQuality: 0.8), bytes.count <= 64 * 1024 else {
            throw WallpaperError.couldNotEncode
        }
        return WallpaperTransferPayload(data: bytes, preview: preview, title: source.title,
                                        format: .jpeg, duration: nil, frameRate: nil)
    }

    static let deviceSize = CGSize(width: 468, height: 468)
    static let previewSize: CGFloat = 208

    static func clampedOffset(_ offset: CGSize, imageSize: CGSize,
                              zoom: CGFloat) -> CGSize {
        let scale = max(previewSize / imageSize.width, previewSize / imageSize.height)
        let width = imageSize.width * scale * zoom
        let height = imageSize.height * scale * zoom
        let limitX = max(0, (width - previewSize) / 2)
        let limitY = max(0, (height - previewSize) / 2)
        return CGSize(
            width: min(limitX, max(-limitX, offset.width)),
            height: min(limitY, max(-limitY, offset.height))
        )
    }

    static func render(_ image: UIImage, zoom: CGFloat, offset: CGSize) -> UIImage {
        let zoom = min(4, max(1, zoom))
        let offset = clampedOffset(offset, imageSize: image.size, zoom: zoom)
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        format.opaque = true
        format.preferredRange = .standard
        return UIGraphicsImageRenderer(size: deviceSize, format: format).image { context in
            let canvas = CGRect(origin: .zero, size: deviceSize)
            UIColor.black.setFill()
            context.fill(canvas)
            context.cgContext.interpolationQuality = .high
            let scale = max(deviceSize.width / image.size.width,
                            deviceSize.height / image.size.height)
            let drawSize = CGSize(width: image.size.width * scale * zoom,
                                  height: image.size.height * scale * zoom)
            let previewToDevice = deviceSize.width / previewSize
            image.draw(in: CGRect(
                x: (deviceSize.width - drawSize.width) / 2 + offset.width * previewToDevice,
                y: (deviceSize.height - drawSize.height) / 2 + offset.height * previewToDevice,
                width: drawSize.width,
                height: drawSize.height
            ))
        }
    }
}
