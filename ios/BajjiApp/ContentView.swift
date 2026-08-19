// SPDX-License-Identifier: MIT
import SwiftUI

struct ContentView: View {
    let tunnel: TunnelManager

    var body: some View {
        NavigationStack {
            Form {
                Section("Tunnel") {
                    LabeledContent("Status", value: tunnel.status)
                    Toggle("Run 60-second Phase 0 echo", isOn: Bindable(tunnel).phaseZeroOnStart)
                    Button("Install VPN Profile") { Task { await tunnel.install() } }
                    Button("Start Bridge") { Task { await tunnel.start() } }
                        .disabled(tunnel.isBusy)
                    Button("Stop", role: .destructive) { tunnel.stop() }
                }

                Section("Diagnostics") {
                    Text(tunnel.detail)
                        .font(.system(.footnote, design: .monospaced))
                        .textSelection(.enabled)
                    Button("Refresh") { Task { await tunnel.readSnapshot() } }
                    Button("Clear Device Binding", role: .destructive) {
                        Task { await tunnel.clearBinding() }
                    }
                }
            }
            .navigationTitle("Bajji Bridge")
            .task {
                await tunnel.refresh()
                while !Task.isCancelled {
                    try? await Task.sleep(for: .seconds(1))
                    await tunnel.readSnapshot()
                }
            }
        }
    }
}
