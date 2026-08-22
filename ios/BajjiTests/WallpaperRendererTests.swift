// SPDX-License-Identifier: MIT
#if !SWIFT_PACKAGE && canImport(UIKit)
import Testing
import UIKit

@Suite("Wallpaper renderer")
struct WallpaperRendererTests {
    @Test @MainActor func rendersSquareOpaquePNGAtDeviceResolution() throws {
        let input = UIGraphicsImageRenderer(size: CGSize(width: 200, height: 200)).image { context in
            UIColor.red.setFill()
            context.fill(CGRect(x: 0, y: 0, width: 100, height: 100))
            UIColor.green.setFill()
            context.fill(CGRect(x: 100, y: 0, width: 100, height: 100))
            UIColor.blue.setFill()
            context.fill(CGRect(x: 0, y: 100, width: 100, height: 100))
            UIColor.yellow.setFill()
            context.fill(CGRect(x: 100, y: 100, width: 100, height: 100))
        }

        let output = WallpaperRenderer.render(input, zoom: 1, offset: .zero)
        let png = try #require(output.pngData())
        let cgImage = try #require(output.cgImage)

        #expect(output.size == CGSize(width: 468, height: 468))
        #expect(output.scale == 1)
        #expect(cgImage.width == 468 && cgImage.height == 468)
        #expect(Array(png.prefix(8)) == [137, 80, 78, 71, 13, 10, 26, 10])
        #expect(cgImage.alphaInfo == .noneSkipLast || cgImage.alphaInfo == .noneSkipFirst)

        var pixels = [UInt8](repeating: 0, count: 468 * 468 * 4)
        let colorSpace = try #require(CGColorSpace(name: CGColorSpace.sRGB))
        let context = try #require(CGContext(
            data: &pixels, width: 468, height: 468, bitsPerComponent: 8,
            bytesPerRow: 468 * 4, space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ))
        context.draw(cgImage, in: CGRect(x: 0, y: 0, width: 468, height: 468))
        func corner(_ x: Int, _ y: Int) -> String {
            let index = (y * 468 + x) * 4
            let red = pixels[index]
            let green = pixels[index + 1]
            let blue = pixels[index + 2]
            if red > 200 && green > 200 { return "yellow" }
            if red > green && red > blue { return "red" }
            if green > red && green > blue { return "green" }
            return "blue"
        }
        #expect(Set([corner(0, 0), corner(467, 0), corner(0, 467), corner(467, 467)]) ==
                Set(["red", "green", "blue", "yellow"]))

        let wide = UIGraphicsImageRenderer(size: CGSize(width: 400, height: 200)).image { context in
            UIColor.red.setFill()
            context.fill(CGRect(x: 0, y: 0, width: 200, height: 200))
            UIColor.blue.setFill()
            context.fill(CGRect(x: 200, y: 0, width: 200, height: 200))
        }
        let maximumOffset = WallpaperRenderer.clampedOffset(
            CGSize(width: 1_000, height: 0), imageSize: wide.size, zoom: 1
        )
        #expect(maximumOffset == CGSize(width: 104, height: 0))
        let shifted = WallpaperRenderer.render(wide, zoom: 1, offset: maximumOffset)
        let shiftedImage = try #require(shifted.cgImage)
        var center = [UInt8](repeating: 0, count: 4)
        let centerContext = try #require(CGContext(
            data: &center, width: 1, height: 1, bitsPerComponent: 8,
            bytesPerRow: 4, space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ))
        centerContext.draw(shiftedImage, in: CGRect(x: -234, y: -234, width: 468, height: 468))
        #expect(center[0] > 200 && center[2] < 40)
    }
}
#endif
