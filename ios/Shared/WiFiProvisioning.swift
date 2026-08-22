// SPDX-License-Identifier: MIT
import Foundation

enum BajjiBluetooth {
    static let serviceUUID = "6F8F8DB0-9C86-4AC5-A854-3A9E2F20B321"
    static let infoUUID = "6F8F8DB0-9C86-4AC5-A854-3A9E2F20B322"
    static let wifiProvisioningUUID = "6F8F8DB0-9C86-4AC5-A854-3A9E2F20B323"
}

enum WiFiProvisioningSecurity: UInt8 {
    case open
    case wep
    case wpa
    case owe
    case wpa2
    case wpa3
}

enum WiFiProvisioningPayload {
    static func encode(ssid: Data, security: WiFiProvisioningSecurity,
                       password: String?) throws -> Data {
        let passwordData = Data((password ?? "").utf8)
        guard (1...32).contains(ssid.count), !ssid.contains(0),
              passwordData.count <= 64, !passwordData.contains(0),
              validPasswordLength(passwordData.count, for: security) else {
            throw WiFiProvisioningError.invalidCredentials
        }
        return Data([1, security.rawValue, UInt8(ssid.count), UInt8(passwordData.count)])
            + ssid + passwordData
    }

    private static func validPasswordLength(_ length: Int,
                                            for security: WiFiProvisioningSecurity) -> Bool {
        switch security {
        case .open, .owe: length == 0
        case .wep: [5, 10, 13, 26].contains(length)
        case .wpa, .wpa2, .wpa3: (8...64).contains(length)
        }
    }
}

enum WiFiProvisioningError: LocalizedError {
    case invalidCredentials
    case unsupportedNetwork

    var errorDescription: String? {
        switch self {
        case .invalidCredentials: "The shared Wi-Fi credentials cannot be used by Bajji."
        case .unsupportedNetwork: "Bajji supports personal Wi-Fi networks, not enterprise login."
        }
    }
}
