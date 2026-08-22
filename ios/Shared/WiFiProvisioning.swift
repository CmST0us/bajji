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
    case invalidStatePayload
    case unsupportedNetwork

    var errorDescription: String? {
        switch self {
        case .invalidCredentials: "The shared Wi-Fi credentials cannot be used by Bajji."
        case .invalidStatePayload: "Bajji returned an invalid network state."
        case .unsupportedNetwork: "Bajji supports personal Wi-Fi networks, not enterprise login."
        }
    }
}

enum DeviceNetworkMode: UInt8, CaseIterable, Sendable {
    case unset
    case manual
    case shared
    case vpn
}

enum DeviceNetworkLinkState: UInt8, Sendable {
    case disabled
    case unconfigured
    case awaitingCredentials
    case connecting
    case connected
    case retrying
}

struct DeviceNetworkState: Equatable, Sendable {
    let mode: DeviceNetworkMode
    let linkState: DeviceNetworkLinkState
    let rssi: Int8
    let error: Int32
    let ssid: String
}

enum NetworkControlPayload {
    static func set(mode: DeviceNetworkMode, ssid: String = "",
                    password: String = "") throws -> Data {
        guard mode == .manual else { return Data([1, mode.rawValue]) }
        let ssidData = Data(ssid.utf8)
        let passwordData = Data(password.utf8)
        let security: WiFiProvisioningSecurity = passwordData.isEmpty ? .open : .wpa2
        guard (1...32).contains(ssidData.count), !ssidData.contains(0),
              passwordData.count <= 64, !passwordData.contains(0),
              passwordData.isEmpty || (8...64).contains(passwordData.count) else {
            throw WiFiProvisioningError.invalidCredentials
        }
        return Data([1, mode.rawValue, security.rawValue,
                     UInt8(ssidData.count), UInt8(passwordData.count)])
            + ssidData + passwordData
    }

    static func state(from payload: Data) throws -> DeviceNetworkState {
        let bytes = [UInt8](payload)
        guard bytes.count >= 10, bytes[0] == 0, bytes[1] == 1,
              let mode = DeviceNetworkMode(rawValue: bytes[2]),
              let linkState = DeviceNetworkLinkState(rawValue: bytes[3]),
              bytes.count == 10 + Int(bytes[9]),
              let ssid = String(data: payload[10...], encoding: .utf8) else {
            throw WiFiProvisioningError.invalidStatePayload
        }
        let errorBits = UInt32(bytes[5]) << 24 | UInt32(bytes[6]) << 16 |
            UInt32(bytes[7]) << 8 | UInt32(bytes[8])
        return DeviceNetworkState(
            mode: mode,
            linkState: linkState,
            rssi: Int8(bitPattern: bytes[4]),
            error: Int32(bitPattern: errorBits),
            ssid: ssid
        )
    }
}

enum BajjiSharedSettings {
    static let networkModeKey = "bajji.networkMode"
    static let recentBluetoothIdentifierKey = "bajji.recentBluetoothIdentifier"

    static var defaults: UserDefaults {
        let fallback = "group.com.eric3u.bajji"
        let identifier = Bundle.main.object(forInfoDictionaryKey: "BajjiAppGroupIdentifier")
            as? String ?? fallback
        return UserDefaults(suiteName: identifier) ?? .standard
    }
}
