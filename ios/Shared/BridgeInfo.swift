// SPDX-License-Identifier: MIT
import Foundation

struct BridgeInfo: Equatable, Sendable {
    static let encodedSize = 22
    static let baseCapabilities: UInt8 = 0x07
    static let settingsCapability: UInt8 = 0x08
    static let wallpaperCapability: UInt8 = 0x10
    static let networkControlCapability: UInt8 = 0x20
    static let controlOnlyCapability: UInt8 = 0x40
    static let currentCapabilities: UInt8 = 0x7F

    let capabilities: UInt8
    let psm: UInt16
    let maximumPayload: UInt16
    let deviceID: Data

    init(data: Data) throws {
        guard data.count == Self.encodedSize,
              data[0] == BridgeFrame.version else {
            throw BridgeProtocolError.invalidBridgeInfo
        }
        let parsedPSM = UInt16(data[2]) << 8 | UInt16(data[3])
        let parsedMaximumPayload = UInt16(data[4]) << 8 | UInt16(data[5])
        guard parsedPSM != 0,
              (20...BridgeFrame.maximumPayload).contains(Int(parsedMaximumPayload)) else {
            throw BridgeProtocolError.invalidBridgeInfo
        }
        capabilities = data[1]
        psm = parsedPSM
        maximumPayload = parsedMaximumPayload
        deviceID = Data(data[6..<Self.encodedSize])
    }
}
