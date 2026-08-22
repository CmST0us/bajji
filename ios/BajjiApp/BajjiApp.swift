// SPDX-License-Identifier: MIT
import SwiftUI

@main
struct BajjiApp: App {
    @State private var tunnel: TunnelManager
    @State private var device: DeviceConnectionManager
    @State private var accessory = AccessoryManager()
    @State private var wallpaper = WallpaperStore()

    init() {
        let tunnel = TunnelManager()
        _tunnel = State(initialValue: tunnel)
        _device = State(initialValue: DeviceConnectionManager(tunnel: tunnel))
    }

    var body: some Scene {
        WindowGroup {
            ContentView(tunnel: tunnel, device: device, accessory: accessory, wallpaper: wallpaper)
        }
    }
}
