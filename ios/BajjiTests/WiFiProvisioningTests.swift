// SPDX-License-Identifier: MIT
import Foundation
import Testing
@testable import BridgeCore

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
