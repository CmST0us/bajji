// SPDX-License-Identifier: MIT
import Foundation
import Testing
#if SWIFT_PACKAGE
@testable import BridgeCore
#endif

@Test func wifiProvisioningPayload() throws {
    let payload = try WiFiProvisioningPayload.encode(
        ssid: Data("Bajji".utf8), security: .wpa2, password: "password"
    )
    #expect(payload == Data([1, 4, 5, 8]) + Data("Bajjipassword".utf8))
    #expect(throws: WiFiProvisioningError.self) {
        try WiFiProvisioningPayload.encode(
            ssid: Data("Bajji".utf8), security: .open, password: "password"
        )
    }
}

@Test func networkControlPayloads() throws {
    #expect(try NetworkControlPayload.set(mode: .shared) == Data([1, 2]))
    #expect(try NetworkControlPayload.set(mode: .vpn) == Data([1, 3]))
    #expect(try NetworkControlPayload.set(
        mode: .manual, ssid: "Bajji", password: "password"
    ) == Data([1, 1, 4, 5, 8]) + Data("Bajjipassword".utf8))
    #expect(try NetworkControlPayload.set(
        mode: .manual, ssid: "Open", password: ""
    ) == Data([1, 1, 0, 4, 0]) + Data("Open".utf8))
    #expect(throws: WiFiProvisioningError.self) {
        try NetworkControlPayload.set(mode: .manual, ssid: "Bajji", password: "short")
    }

    let payload = Data([0, 1, 1, 4, 0xD8, 0xFF, 0xFF, 0xFF, 0x2A, 5])
        + Data("Bajji".utf8)
    #expect(try NetworkControlPayload.state(from: payload) == DeviceNetworkState(
        mode: .manual, linkState: .connected, rssi: -40, error: -214, ssid: "Bajji"
    ))
}
