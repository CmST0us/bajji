// SPDX-License-Identifier: MIT
import AccessorySetupKit
@preconcurrency import CoreBluetooth
import Observation
import OSLog
import UIKit
import WiFiInfrastructure

enum WiFiSharingState: Equatable {
    case notShared
    case authorizing
    case shared
    case restricted(String)
    case failed(String)
}

@MainActor
@Observable
final class AccessoryManager: NSObject {
    var status = "Not added"
    var detail = "Add the StopWatch, then share the iPhone Wi-Fi network."
    var hasAccessory = false
    var isBusy = false
    var isPairing = false
    var wifiSharingState: WiFiSharingState = .notShared

    private let logger = Logger(subsystem: "com.eric3u.bajji", category: "AccessoryManager")
    private let session = ASAccessorySession()
    private var accessory: ASAccessory?
    private var central: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var authorizationPending = false

    private static let pickerItem: ASPickerDisplayItem = {
        let descriptor = ASDiscoveryDescriptor()
        descriptor.bluetoothServiceUUID = CBUUID(string: BajjiBluetooth.serviceUUID)
        descriptor.supportedOptions.insert(.bluetoothPairingLE)
        return ASPickerDisplayItem(
            name: "Bajji StopWatch",
            productImage: UIImage(systemName: "applewatch")!,
            descriptor: descriptor
        )
    }()

    override init() {
        super.init()
        logger.info("activating AccessorySetupKit session")
        session.activate(on: .main) { [weak self] event in
            self?.handle(event)
        }
    }

    func presentPicker() async {
        guard !isBusy else {
            logger.info("AccessorySetupKit picker deferred: another accessory operation is active")
            detail = "Wait for Wi-Fi sharing to finish."
            return
        }
        releaseBluetoothConnection()
        isBusy = true
        isPairing = true
        defer {
            isBusy = false
            isPairing = false
        }
        logger.info("presenting AccessorySetupKit picker")
        do {
            try await session.showPicker(for: [Self.pickerItem])
            logger.info("AccessorySetupKit picker completed")
        } catch {
            logger.error("AccessorySetupKit picker failed: \(error.localizedDescription, privacy: .public)")
            detail = error.localizedDescription
        }
    }

    func shareWiFi() {
        let bluetoothAuthorization = CBManager.authorization
        logger.info("Wi-Fi sharing requested: accessory_present=\(self.accessory != nil) bluetooth_authorization=\(bluetoothAuthorization.rawValue)")
        guard !isBusy else {
            logger.info("Wi-Fi sharing ignored: another accessory operation is active")
            return
        }
        guard let accessory else {
            logger.error("Wi-Fi sharing rejected: no accessory")
            detail = "Add the StopWatch first."
            wifiSharingState = .failed(detail)
            return
        }
        guard #available(iOS 26.2, *) else {
            logger.error("Wi-Fi sharing rejected: unsupported iOS version")
            detail = "Wi-Fi sharing requires iOS 26.2 or later."
            wifiSharingState = .restricted(detail)
            return
        }
        if bluetoothAuthorization == .denied || bluetoothAuthorization == .restricted {
            logger.error("Wi-Fi sharing rejected: Bluetooth permission=\(bluetoothAuthorization.rawValue)")
            detail = "Allow Bluetooth for Bajji in Settings."
            wifiSharingState = .restricted(detail)
            return
        }
        if bluetoothAuthorization == .notDetermined {
            logger.info("creating CoreBluetooth central to request Bluetooth permission")
            detail = "Allow Bluetooth access to continue."
        }
        isBusy = true
        wifiSharingState = .authorizing
        authorizationPending = true
        self.accessory = accessory
        if central == nil {
            logger.info("creating CoreBluetooth central for Wi-Fi sharing authorization")
            central = CBCentralManager(
                delegate: self, queue: nil,
                options: [CBCentralManagerOptionShowPowerAlertKey: true]
            )
        }
        connectIfReady()
    }

    private func handle(_ event: ASAccessoryEvent) {
        let eventType = String(describing: event.eventType)
        logger.info("AccessorySetupKit event: type=\(eventType, privacy: .public) session_accessories=\(self.session.accessories.count) event_accessory=\(event.accessory != nil)")
        switch event.eventType {
        case .activated:
            save(session.accessories.first)
        case .accessoryAdded, .accessoryChanged:
            save(event.accessory)
        case .accessoryRemoved:
            save(nil)
        default:
            break
        }
    }

    private func save(_ accessory: ASAccessory?) {
        self.accessory = accessory
        hasAccessory = accessory != nil
        status = accessory?.displayName ?? "Not added"
        if accessory == nil {
            wifiSharingState = .notShared
        }
        logger.info("saved accessory state: present=\(accessory != nil) bluetooth_identifier=\(accessory?.bluetoothIdentifier != nil)")
    }

    private func connectIfReady() {
        guard authorizationPending else { return }
        guard let central else {
            logger.info("authorization waiting for CoreBluetooth central creation")
            return
        }
        guard central.state == .poweredOn else {
            logger.info("authorization waiting for Bluetooth: state=\(central.state.rawValue)")
            return
        }
        guard let identifier = accessory?.bluetoothIdentifier else {
            logger.error("authorization failed: accessory has no Bluetooth identifier")
            finish(.failed("The paired StopWatch has no Bluetooth identifier."))
            return
        }
        let peripherals = central.retrievePeripherals(withIdentifiers: [identifier])
        logger.info("retrieved paired peripherals: count=\(peripherals.count)")
        guard let peripheral = peripherals.first else {
            logger.error("authorization failed: paired peripheral unavailable")
            finish(.failed("The paired StopWatch is not available over Bluetooth."))
            return
        }
        self.peripheral = peripheral
        logger.info("paired peripheral state: state=\(peripheral.state.rawValue)")
        if peripheral.state == .connected {
            authorize()
        } else {
            logger.info("connecting paired peripheral for Wi-Fi sharing authorization")
            central.connect(peripheral)
        }
    }

    private func authorize() {
        guard #available(iOS 26.2, *), let accessory else {
            logger.error("authorization aborted: accessory or platform unavailable")
            finish(.restricted("Wi-Fi sharing authorization is unavailable."))
            return
        }
        authorizationPending = false
        logger.info("creating Wi-Fi network sharing controller")
        Task {
            do {
                let controller = try await WINetworkSharingController(for: accessory)
                logger.info("requesting Wi-Fi sharing authorization")
                let state = try await controller.requestAuthorization()
                let stateDescription = String(describing: state)
                logger.info("Wi-Fi sharing authorization state: \(stateDescription, privacy: .public)")
                if state == .askToShare {
                    logger.info("presenting Wi-Fi ask-to-share UI from host app")
                    let result = try await controller.askToShare()
                    let resultDescription = String(describing: result)
                    logger.info("host ask-to-share UI completed: \(resultDescription, privacy: .public)")
                }
                finish(.shared)
            } catch {
                logger.error("Wi-Fi sharing authorization failed: \(error.localizedDescription, privacy: .public)")
                let message = error.localizedDescription
                let normalized = message.lowercased()
                if normalized.contains("region") || normalized.contains("eligible") ||
                    normalized.contains("restricted") || normalized.contains("entitlement") {
                    finish(.restricted(message))
                } else {
                    finish(.failed(message))
                }
            }
        }
    }

    private func finish(_ state: WiFiSharingState) {
        let message: String
        switch state {
        case .notShared:
            message = "Wi-Fi has not been shared."
        case .authorizing:
            message = "Waiting for iOS authorization."
        case .shared:
            message = "Wi-Fi credentials were securely shared with the StopWatch."
        case .restricted(let reason), .failed(let reason):
            message = reason
        }
        logger.info("Wi-Fi sharing request finished: \(message, privacy: .public)")
        authorizationPending = false
        isBusy = false
        wifiSharingState = state
        detail = message
        releaseBluetoothConnection()
    }

    private func releaseBluetoothConnection() {
        if let central, let peripheral, peripheral.state != .disconnected {
            logger.info("releasing Wi-Fi sharing Bluetooth connection: state=\(peripheral.state.rawValue)")
            central.cancelPeripheralConnection(peripheral)
        }
        peripheral = nil
        central = nil
    }
}

extension AccessoryManager: @MainActor CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        logger.info("CoreBluetooth central state changed: state=\(central.state.rawValue) pending=\(self.authorizationPending)")
        if central.state == .poweredOn {
            connectIfReady()
        } else if authorizationPending &&
                    (central.state == .unauthorized || central.state == .unsupported) {
            logger.error("authorization failed: Bluetooth unavailable state=\(central.state.rawValue)")
            finish(.restricted("Bluetooth is unavailable."))
        } else if authorizationPending && central.state == .poweredOff {
            logger.info("authorization waiting for Bluetooth to power on")
            detail = "Turn on Bluetooth to continue."
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        logger.info("paired peripheral connected for Wi-Fi sharing authorization")
        authorize()
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        logger.error("paired peripheral connection failed: \(error?.localizedDescription ?? "no CoreBluetooth error", privacy: .public)")
        finish(.failed(error?.localizedDescription ?? "Could not connect to the StopWatch."))
    }
}
