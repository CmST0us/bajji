// SPDX-License-Identifier: MIT
import Foundation

struct BridgeInfo: Equatable, Sendable {
    static let encodedSize = 22

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
