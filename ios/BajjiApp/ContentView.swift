// SPDX-License-Identifier: MIT
import Foundation
import SwiftUI

struct ContentView: View {
    let tunnel: TunnelManager

    var body: some View {
        NavigationStack {
            Form {
                Section("Tunnel") {
                    LabeledContent("VPN", value: tunnel.status)
                    Button("Install VPN Profile") { Task { await tunnel.install() } }
                    Button("Start Bridge") { Task { await tunnel.start() } }
                        .disabled(tunnel.isBusy)
                    Button("Stop", role: .destructive) { tunnel.stop() }
                }

                if let diagnostics = tunnel.diagnostics {
                    connectionSection(diagnostics.bluetooth, internet: diagnostics.internet)
                    forwarderSection(diagnostics.forwarder)
                    transportSection(diagnostics.bluetooth)
                }

                Section("Diagnostics") {
                    Text(tunnel.detail)
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

    @ViewBuilder
    private func connectionSection(_ bluetooth: BluetoothDiagnostics, internet: String) -> some View {
        Section("Connection") {
            LabeledContent("Bluetooth", value: bluetooth.state)
            LabeledContent("Internet", value: internet)
            LabeledContent("Signal", value: bluetooth.rssi == 0 ? "—" : "\(bluetooth.rssi) dBm")
            LabeledContent("L2CAP PSM", value: String(format: "0x%04X", bluetooth.psm))
            LabeledContent("Max payload", value: "\(bluetooth.maximumPayload) B")
            LabeledContent("Reconnects", value: "\(bluetooth.reconnects)")
        }
    }

    @ViewBuilder
    private func forwarderSection(_ forwarder: IPForwarderDiagnostics) -> some View {
        Section("IP Forwarder") {
            LabeledContent("State", value: forwarder.state)
            LabeledContent("Device → Internet",
                           value: "\(forwarder.fromDevicePackets) · \(formattedBytes(forwarder.fromDeviceBytes))")
            LabeledContent("Internet → Device",
                           value: "\(forwarder.toDevicePackets) · \(formattedBytes(forwarder.toDeviceBytes))")
            LabeledContent("Dropped") {
                Text("\(forwarder.droppedPackets)")
                    .foregroundStyle(forwarder.droppedPackets == 0 ? Color.secondary : Color.red)
            }
            LabeledContent("Invalid", value: "\(forwarder.invalidPackets)")
            LabeledContent("HEV", value: forwarder.hev.state)
            if !forwarder.lastError.isEmpty || !forwarder.hev.lastError.isEmpty {
                LabeledContent("Last error") {
                    Text(forwarder.lastError.isEmpty ? forwarder.hev.lastError : forwarder.lastError)
                        .foregroundStyle(.red)
                }
            }
        }
    }

    @ViewBuilder
    private func transportSection(_ bluetooth: BluetoothDiagnostics) -> some View {
        Section("Transport") {
            LabeledContent("Stream TX", value: formattedBytes(bluetooth.sentBytes))
            LabeledContent("Stream RX", value: formattedBytes(bluetooth.receivedBytes))
            LabeledContent("Queue overflows") {
                Text("\(bluetooth.queueOverflows)")
                    .foregroundStyle(bluetooth.queueOverflows == 0 ? Color.secondary : Color.red)
            }
            if !bluetooth.lastError.isEmpty {
                LabeledContent("Last error") {
                    Text(bluetooth.lastError).foregroundStyle(.red)
                }
            }
        }
    }

    private func formattedBytes(_ value: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: value), countStyle: .binary)
    }
}
