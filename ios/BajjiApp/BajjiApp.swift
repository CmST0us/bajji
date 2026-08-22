// SPDX-License-Identifier: MIT
import SwiftUI

@main
struct BajjiApp: App {
    @State private var tunnel = TunnelManager()
    @State private var accessory = AccessoryManager()
    @State private var wallpaper = WallpaperStore()

    var body: some Scene {
        WindowGroup {
            ContentView(tunnel: tunnel, accessory: accessory, wallpaper: wallpaper)
        }
    }
}
