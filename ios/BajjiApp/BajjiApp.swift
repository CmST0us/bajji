// SPDX-License-Identifier: MIT
import SwiftUI

@main
struct BajjiApp: App {
    @State private var tunnel = TunnelManager()

    var body: some Scene {
        WindowGroup {
            ContentView(tunnel: tunnel)
        }
    }
}
