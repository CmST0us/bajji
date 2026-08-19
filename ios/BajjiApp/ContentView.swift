// SPDX-License-Identifier: MIT
import Foundation
import SwiftUI

struct ContentView: View {
    let tunnel: TunnelManager

    var body: some View {
        NavigationStack {
            Form {
                Section("Tunnel") {
                    LabeledContent("Status", value: tunnel.status)
                    Toggle("Run 60-second Phase 0 echo", isOn: Bindable(tunnel).phaseZeroOnStart)
                        .onChange(of: tunnel.phaseZeroOnStart) {
                            Task { await tunnel.applyPhaseZeroSetting() }
                        }
                    Button("Install VPN Profile") { Task { await tunnel.install() } }
                    Button("Start Bridge") { Task { await tunnel.start() } }
                        .disabled(tunnel.isBusy)
                    Button("Stop", role: .destructive) { tunnel.stop() }
                }

                if let diagnostics = tunnel.diagnostics {
                    connectionSection(diagnostics.bluetooth)
                    transportSection(diagnostics.bluetooth)
                    phaseZeroSection(diagnostics.phaseZero)
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
                await tunnel.applyPhaseZeroSetting()
                while !Task.isCancelled {
                    try? await Task.sleep(for: .seconds(1))
                    await tunnel.readSnapshot()
                }
            }
        }
    }

    @ViewBuilder
    private func connectionSection(_ bluetooth: BluetoothDiagnostics) -> some View {
        Section("Connection") {
            LabeledContent("State", value: bluetooth.state)
            LabeledContent("Signal", value: bluetooth.rssi == 0 ? "—" : "\(bluetooth.rssi) dBm")
            LabeledContent("L2CAP PSM", value: String(format: "0x%04X", bluetooth.psm))
            LabeledContent("Max payload", value: "\(bluetooth.maximumPayload) B")
            LabeledContent("Reconnects", value: "\(bluetooth.reconnects)")
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

    @ViewBuilder
    private func phaseZeroSection(_ phaseZero: PhaseZeroDiagnostics) -> some View {
        Section("Phase Zero Echo") {
            LabeledContent("State", value: phaseZero.running ? "Running · window 4" : "Stopped")
            LabeledContent("Payload TX", value: formattedBytes(phaseZero.sentPayloadBytes))
            LabeledContent("Echo RX", value: formattedBytes(phaseZero.echoedPayloadBytes))
            LabeledContent("Echo rate", value: String(format: "%.1f KB/s", phaseZero.echoedKilobytesPerSecond))
            LabeledContent("50 KB/s target") {
                Text(phaseZero.echoedKilobytesPerSecond >= 50 ? "PASS" : "Pending")
                    .foregroundStyle(phaseZero.echoedKilobytesPerSecond >= 50 ? Color.green : Color.secondary)
            }
            LabeledContent("In flight", value: "\(phaseZero.inFlightFrames) frames")
            LabeledContent("Elapsed", value: String(format: "%.1f s", phaseZero.elapsedSeconds))
        }
    }

    private func formattedBytes(_ value: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: value), countStyle: .binary)
    }
}
