// SPDX-License-Identifier: MIT
import AccessorySetupKit
@preconcurrency import CoreBluetooth
import Foundation
import OSLog
import WiFiInfrastructure

@available(iOS 26.2, *)
final class WiFiSharingProvider: @unchecked Sendable {
    private static let logger = Logger(subsystem: "com.eric3u.bajji", category: "WiFiSharing")
    private let accessory: ASAccessory
    private let writer = ProvisioningWriter()
    private var task: Task<Void, Never>?

    init(accessory: ASAccessory) {
        self.accessory = accessory
    }

    func start() {
        guard task == nil else {
            Self.logger.info("Wi-Fi sharing provider already running")
            return
        }
        Self.logger.info("starting Wi-Fi sharing provider task")
        task = Task.detached { [accessory, writer] in
            let logger = Self.logger
            do {
                logger.info("creating Wi-Fi Infrastructure network sharing provider")
                let provider = try await WINetworkSharingProvider(for: accessory)
                logger.info("listening for Wi-Fi Infrastructure network events")
                for try await event in provider.networkEvents() {
                    guard BajjiSharedSettings.defaults.integer(
                        forKey: BajjiSharedSettings.networkModeKey
                    ) == Int(DeviceNetworkMode.shared.rawValue) else {
                        logger.info("ignored Wi-Fi event because shared mode is not selected")
                        continue
                    }
                    logger.info("received Wi-Fi network event: app_requested=\(event.appRequestedSharing) new_network=\(event.newShareableNetworkAvailable) networks=\(event.networks.count)")
                    guard !Task.isCancelled else {
                        logger.info("Wi-Fi sharing provider task cancelled while handling event")
                        return
                    }
                    if event.appRequestedSharing || event.newShareableNetworkAvailable {
                        logger.info("presenting Wi-Fi Infrastructure ask-to-share UI")
                        let result = try await provider.presentAskToShareUI()
                        let resultDescription = String(describing: result)
                        logger.info("extension ask-to-share UI completed: \(resultDescription, privacy: .public)")
                    }
                    // ponytail: the newest shared network is the provisioning target; add an
                    // accessory scan/current-AP match if multi-network selection becomes necessary.
                    guard let network = event.networks.max(by: {
                        $0.lastModified < $1.lastModified
                    }) else {
                        logger.info("Wi-Fi network event has no shareable networks")
                        continue
                    }
                    let policies = network.securityPolicy.map { $0.description }.sorted()
                        .joined(separator: ",")
                    logger.info("selected newest shared network: ssid_bytes=\(network.ssid.data.count) security_policy=\(policies, privacy: .public)")
                    let payload = try Self.payload(for: network)
                    logger.info("sending encoded Wi-Fi provisioning payload: bytes=\(payload.count)")
                    try await writer.send(payload, to: accessory)
                    logger.info("sent Wi-Fi provisioning payload to Bajji")
                }
            } catch is CancellationError {
                logger.info("Wi-Fi sharing provider task cancelled")
                return
            } catch {
                logger.error("Wi-Fi sharing failed: \(error.localizedDescription, privacy: .public)")
            }
        }
    }

    func stop() {
        Self.logger.info("stopping Wi-Fi sharing provider task: active=\(self.task != nil)")
        task?.cancel()
        task = nil
    }

    private static func payload(for network: WINetworkSharingProvider.Network) throws -> Data {
        let password: String?
        let credentialKind: String
        if case .none = network.credentials {
            password = nil
            credentialKind = "none"
        } else if case let .password(value) = network.credentials {
            password = value
            credentialKind = "password"
        } else {
            logger.error("unsupported Wi-Fi credential kind")
            throw WiFiProvisioningError.unsupportedNetwork
        }
        // Apple reports every allowed mode, not one selected mode; see
        // WiFiInfrastructure/WINetworkSharingProvider/Network/securityPolicy.
        // Use the lowest password mode so WPA2/WPA3 transition networks remain usable.
        let security: WiFiProvisioningSecurity
        if network.securityPolicy.contains(.owe) { security = .owe }
        else if network.securityPolicy.contains(.open) { security = .open }
        else if network.securityPolicy.contains(.wep) { security = .wep }
        else if network.securityPolicy.contains(.wpa) { security = .wpa }
        else if network.securityPolicy.contains(.wpa2) { security = .wpa2 }
        else if network.securityPolicy.contains(.wpa3) { security = .wpa3 }
        else {
            let policy = String(describing: network.securityPolicy)
            logger.error("unsupported Wi-Fi security policy: \(policy, privacy: .public)")
            throw WiFiProvisioningError.unsupportedNetwork
        }
        assert(!network.securityPolicy.contains(.wpa2) || security != .wpa3)
#if DEBUG
        let ssid = String(data: network.ssid.data, encoding: .utf8)
            ?? network.ssid.data.base64EncodedString()
        logger.warning("SENSITIVE debug credentials: ssid=\(ssid, privacy: .public) password=\(password ?? "", privacy: .public)")
#endif
        logger.info("encoding Wi-Fi provisioning payload: ssid_bytes=\(network.ssid.data.count) security=\(security.rawValue) credentials=\(credentialKind, privacy: .public) password_bytes=\(password?.utf8.count ?? 0)")
        let payload = try WiFiProvisioningPayload.encode(
            ssid: network.ssid.data, security: security, password: password
        )
        logger.info("encoded Wi-Fi provisioning payload: bytes=\(payload.count)")
        return payload
    }
}

@available(iOS 26.2, *)
private final class ProvisioningWriter: NSObject, @unchecked Sendable {
    private struct Pending {
        let payload: Data
        let identifier: UUID
        let continuation: CheckedContinuation<Void, Error>
    }

    private let queue = DispatchQueue(label: "com.eric3u.bajji.wifi-sharing.ble")
    private let logger = Logger(subsystem: "com.eric3u.bajji", category: "ProvisioningWriter")
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var characteristic: CBCharacteristic?
    private var pending: Pending?

    func send(_ payload: Data, to accessory: ASAccessory) async throws {
        logger.info("BLE provisioning send requested: payload_bytes=\(payload.count)")
        guard let identifier = accessory.bluetoothIdentifier else {
            logger.error("BLE provisioning send rejected: missing Bluetooth identifier")
            throw ProvisioningWriterError.missingBluetoothIdentifier
        }
        try await withCheckedThrowingContinuation {
            (continuation: CheckedContinuation<Void, any Error>) in
            queue.async { [self] in
                guard pending == nil else {
                    logger.error("BLE provisioning send rejected: another write is pending")
                    continuation.resume(throwing: ProvisioningWriterError.busy)
                    return
                }
                pending = Pending(payload: payload, identifier: identifier,
                                  continuation: continuation)
                logger.info("BLE provisioning request queued: payload_bytes=\(payload.count)")
                if central == nil {
                    logger.info("creating CoreBluetooth central for provisioning write")
                    central = CBCentralManager(delegate: self, queue: queue)
                }
                connectIfReady()
            }
        }
    }

    private func connectIfReady() {
        guard let pending else {
            logger.debug("BLE connect check ignored: no pending provisioning write")
            return
        }
        guard central?.state == .poweredOn else {
            logger.info("BLE provisioning waiting for central: state=\(self.central?.state.rawValue ?? -1)")
            return
        }
        if peripheral?.identifier != pending.identifier {
            let peripherals = central.retrievePeripherals(withIdentifiers: [pending.identifier])
            logger.info("retrieved paired peripherals for provisioning: count=\(peripherals.count)")
            peripheral = peripherals.first
            characteristic = nil
        }
        guard let peripheral else {
            logger.error("BLE provisioning peripheral unavailable")
            fail(.accessoryUnavailable)
            return
        }
        peripheral.delegate = self
        logger.info("BLE provisioning peripheral state: state=\(peripheral.state.rawValue) characteristic_ready=\(self.characteristic != nil)")
        switch peripheral.state {
        case .connected:
            if characteristic != nil {
                write()
            } else {
                logger.info("discovering Bajji BLE service")
                peripheral.discoverServices([CBUUID(string: BajjiBluetooth.serviceUUID)])
            }
        case .connecting:
            logger.info("waiting for Bajji BLE peripheral connection")
        default:
            logger.info("connecting Bajji BLE peripheral")
            central.connect(peripheral)
        }
    }

    private func write() {
        guard let pending, let peripheral, let characteristic else {
            logger.error("BLE provisioning write skipped: missing pending request, peripheral, or characteristic")
            return
        }
        let maximum = peripheral.maximumWriteValueLength(for: .withResponse)
        logger.info("writing Wi-Fi provisioning characteristic: payload_bytes=\(pending.payload.count) max_bytes=\(maximum) properties=\(characteristic.properties.rawValue)")
        guard pending.payload.count <= maximum else {
            logger.error("BLE provisioning payload too large: payload_bytes=\(pending.payload.count) max_bytes=\(maximum)")
            fail(.payloadTooLarge)
            return
        }
        peripheral.writeValue(pending.payload, for: characteristic, type: .withResponse)
    }

    private func fail(_ error: ProvisioningWriterError) {
        logger.error("BLE provisioning failed: \(error.localizedDescription, privacy: .public)")
        pending?.continuation.resume(throwing: error)
        pending = nil
    }

    private func succeed() {
        logger.info("BLE provisioning write completed successfully")
        pending?.continuation.resume()
        pending = nil
    }
}

@available(iOS 26.2, *)
extension ProvisioningWriter: CBCentralManagerDelegate, CBPeripheralDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        logger.info("provisioning CoreBluetooth state changed: state=\(central.state.rawValue)")
        if central.state == .poweredOn {
            connectIfReady()
        } else if central.state == .unauthorized || central.state == .unsupported {
            fail(.bluetoothUnavailable)
        } else if central.state == .poweredOff {
            logger.info("BLE provisioning waiting for Bluetooth to power on")
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        logger.info("provisioning BLE peripheral connected; discovering service")
        peripheral.discoverServices([CBUUID(string: BajjiBluetooth.serviceUUID)])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        logger.error("provisioning BLE connection failed: \(error?.localizedDescription ?? "no CoreBluetooth error", privacy: .public)")
        fail(.accessoryUnavailable)
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        logger.error("provisioning BLE peripheral disconnected: pending=\(self.pending != nil) error=\(error?.localizedDescription ?? "none", privacy: .public)")
        if pending != nil { fail(.accessoryUnavailable) }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        logger.info("BLE service discovery completed: services=\(peripheral.services?.count ?? 0) error=\(error?.localizedDescription ?? "none", privacy: .public)")
        guard error == nil,
              let service = peripheral.services?.first(where: {
                  $0.uuid == CBUUID(string: BajjiBluetooth.serviceUUID)
              }) else {
            fail(.serviceUnavailable)
            return
        }
        logger.info("Bajji BLE service found; discovering Wi-Fi provisioning characteristic")
        peripheral.discoverCharacteristics(
            [CBUUID(string: BajjiBluetooth.wifiProvisioningUUID)], for: service
        )
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        logger.info("BLE characteristic discovery completed: characteristics=\(service.characteristics?.count ?? 0) error=\(error?.localizedDescription ?? "none", privacy: .public)")
        guard error == nil,
              let characteristic = service.characteristics?.first(where: {
                  $0.uuid == CBUUID(string: BajjiBluetooth.wifiProvisioningUUID)
              }) else {
            fail(.characteristicUnavailable)
            return
        }
        self.characteristic = characteristic
        logger.info("Wi-Fi provisioning characteristic found")
        write()
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        logger.info("BLE provisioning write response: error=\(error?.localizedDescription ?? "none", privacy: .public)")
        if error == nil {
            succeed()
        } else {
            fail(.writeFailed)
        }
    }
}

private enum ProvisioningWriterError: LocalizedError {
    case missingBluetoothIdentifier
    case busy
    case bluetoothUnavailable
    case accessoryUnavailable
    case serviceUnavailable
    case characteristicUnavailable
    case payloadTooLarge
    case writeFailed

    var errorDescription: String? {
        switch self {
        case .missingBluetoothIdentifier: "The paired accessory has no Bluetooth identifier."
        case .busy: "Another Wi-Fi configuration is already being sent."
        case .bluetoothUnavailable: "Bluetooth is unavailable."
        case .accessoryUnavailable: "The StopWatch is not connected."
        case .serviceUnavailable: "The Bajji Bluetooth service is unavailable."
        case .characteristicUnavailable: "The Wi-Fi provisioning characteristic is unavailable."
        case .payloadTooLarge: "The Wi-Fi configuration exceeds the Bluetooth write size."
        case .writeFailed: "The StopWatch rejected the Wi-Fi configuration."
        }
    }
}
