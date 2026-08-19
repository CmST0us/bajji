// SPDX-License-Identifier: MIT
@preconcurrency import CoreBluetooth
import Foundation

struct BluetoothSnapshot: Codable {
    var state = "idle"
    var deviceID = ""
    var psm: UInt16 = 0
    var maximumPayload: UInt16 = 0
    var rssi = 0
    var receivedBytes: UInt64 = 0
    var sentBytes: UInt64 = 0
    var reconnects = 0
    var droppedFrames = 0
    var lastError = ""
}

final class BluetoothBridge: NSObject, @unchecked Sendable {
    static var serviceUUID: CBUUID { CBUUID(string: "6F8F8DB0-9C86-4AC5-A854-3A9E2F20B321") }
    static var infoUUID: CBUUID { CBUUID(string: "6F8F8DB0-9C86-4AC5-A854-3A9E2F20B322") }

    private let queue = DispatchQueue(label: "com.cmstopus.bajji.packet-tunnel.ble")
    private let snapshotLock = NSLock()
    private let defaults = UserDefaults(suiteName: "group.com.cmstopus.bajji")!
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var stream: L2CAPStream?
    private var currentSnapshot = BluetoothSnapshot()
    private var connectionCount = 0
    private var shouldReconnect = true
    private var handshakeNonce: Data?

    var onFrame: ((BridgeFrame) -> Void)?
    var onReady: (() -> Void)?

    func start() {
        queue.async { [weak self] in
            guard let self else { return }
            central = CBCentralManager(
                delegate: self,
                queue: queue,
                options: [CBCentralManagerOptionRestoreIdentifierKey: "com.cmstopus.bajji.packet-tunnel.ble"]
            )
            update { $0.state = "starting Bluetooth" }
        }
    }

    func stop() {
        queue.async { [weak self] in
            guard let self else { return }
            shouldReconnect = false
            stream?.stop()
            if let peripheral { central?.cancelPeripheralConnection(peripheral) }
            central?.stopScan()
            update { $0.state = "stopped" }
        }
    }

    func send(_ frame: BridgeFrame) {
        queue.async { [weak self] in
            guard let self, handshakeNonce == nil else { return }
            stream?.send(frame)
        }
    }

    func snapshot() -> BluetoothSnapshot {
        queue.async { [weak self] in self?.peripheral?.readRSSI() }
        return snapshotLock.withLock { currentSnapshot }
    }

    func clearBinding() {
        defaults.removeObject(forKey: "boundDeviceID")
        update { $0.deviceID = "" }
    }

    private func scan() {
        guard central.state == .poweredOn else { return }
        update { $0.state = "scanning" }
        central.scanForPeripherals(withServices: [Self.serviceUUID], options: [
            CBCentralManagerScanOptionAllowDuplicatesKey: false
        ])
    }

    private func connect(_ peripheral: CBPeripheral) {
        guard self.peripheral == nil else { return }
        self.peripheral = peripheral
        peripheral.delegate = self
        central.stopScan()
        update { $0.state = "connecting" }
        central.connect(peripheral)
    }

    private func fail(_ message: String, reconnect: Bool = true) {
        update {
            $0.state = "error"
            $0.lastError = message
        }
        shouldReconnect = reconnect
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
    }

    private func update(_ change: (inout BluetoothSnapshot) -> Void) {
        snapshotLock.withLock { change(&currentSnapshot) }
    }

    private func useBridgeInfo(_ data: Data) {
        do {
            let info = try BridgeInfo(data: data)
            guard info.capabilities & 0x07 == 0x07,
                  info.maximumPayload == BridgeFrame.maximumPayload else {
                fail("StopWatch does not support the required IPv4/TCP/UDP profile", reconnect: false)
                return
            }
            if let expected = defaults.data(forKey: "boundDeviceID"), expected != info.deviceID {
                fail("Device ID does not match the saved StopWatch", reconnect: false)
                return
            }
            if defaults.data(forKey: "boundDeviceID") == nil {
                defaults.set(info.deviceID, forKey: "boundDeviceID")
            }
            update {
                $0.deviceID = info.deviceID.map { String(format: "%02x", $0) }.joined()
                $0.psm = info.psm
                $0.maximumPayload = info.maximumPayload
                $0.state = "opening L2CAP CoC"
            }
            peripheral?.openL2CAPChannel(CBL2CAPPSM(info.psm))
        } catch {
            fail("Invalid BridgeInfo: \(error)", reconnect: false)
        }
    }

    private func handle(_ frame: BridgeFrame) {
        guard let nonce = handshakeNonce else {
            onFrame?(frame)
            return
        }
        let payload = [UInt8](frame.payload)
        guard frame.type == .helloAck,
              payload.count == 7,
              payload[0] == 0x05, payload[1] == 0x00,
              Data(payload[2..<6]) == nonce,
              payload[6] == 0 else {
            fail("Bridge HELLO handshake failed", reconnect: false)
            return
        }
        handshakeNonce = nil
        update { $0.state = "L2CAP ready" }
        onReady?()
    }
}

extension BluetoothBridge: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            scan()
        } else {
            update { $0.state = "Bluetooth \(central.state.rawValue)" }
        }
    }

    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        let restored = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral]
        if let peripheral = restored?.first {
            connect(peripheral)
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        update { $0.rssi = RSSI.intValue }
        connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connectionCount += 1
        update {
            $0.state = "discovering encrypted BridgeInfo"
            $0.reconnects = max(0, connectionCount - 1)
            $0.lastError = ""
        }
        peripheral.discoverServices([Self.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        self.peripheral = nil
        update { $0.lastError = error?.localizedDescription ?? "connection failed" }
        if shouldReconnect { scan() }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        timestamp: CFAbsoluteTime, isReconnecting: Bool, error: Error?) {
        stream?.stop()
        stream = nil
        self.peripheral = nil
        update {
            $0.state = "disconnected"
            if let error { $0.lastError = error.localizedDescription }
        }
        if shouldReconnect {
            queue.asyncAfter(deadline: .now() + 1) { [weak self] in self?.scan() }
        }
    }
}

extension BluetoothBridge: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil,
              let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            fail(error?.localizedDescription ?? "Bridge service not found")
            return
        }
        peripheral.discoverCharacteristics([Self.infoUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        guard error == nil,
              let characteristic = service.characteristics?.first(where: { $0.uuid == Self.infoUUID }) else {
            fail(error?.localizedDescription ?? "BridgeInfo characteristic not found")
            return
        }
        peripheral.readValue(for: characteristic)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard error == nil, let value = characteristic.value else {
            fail(error?.localizedDescription ?? "Encrypted BridgeInfo read failed")
            return
        }
        useBridgeInfo(value)
    }

    func peripheral(_ peripheral: CBPeripheral, didOpen channel: CBL2CAPChannel?, error: Error?) {
        guard error == nil, let channel else {
            fail(error?.localizedDescription ?? "L2CAP CoC open failed")
            return
        }
        let stream = L2CAPStream(channel: channel)
        stream.onFrame = { [weak self] frame in
            self?.queue.async { [weak self] in self?.handle(frame) }
        }
        stream.onBytes = { [weak self] received, sent in
            self?.update {
                $0.receivedBytes += UInt64(received)
                $0.sentBytes += UInt64(sent)
            }
        }
        stream.onDrop = { [weak self] in self?.update { $0.droppedFrames += 1 } }
        stream.onClose = { [weak self] reason in
            self?.queue.async { [weak self] in
                guard let self else { return }
                update {
                    $0.state = "stream closed"
                    $0.lastError = reason
                }
                if shouldReconnect {
                    central.cancelPeripheralConnection(peripheral)
                }
            }
        }
        self.stream = stream
        let nonceValue = UInt32.random(in: UInt32.min...UInt32.max)
        let nonce = Data([
            UInt8(nonceValue >> 24), UInt8((nonceValue >> 16) & 0xFF),
            UInt8((nonceValue >> 8) & 0xFF), UInt8(nonceValue & 0xFF)
        ])
        handshakeNonce = nonce
        let deviceID = defaults.data(forKey: "boundDeviceID") ?? Data()
        update { $0.state = "Bridge HELLO" }
        stream.start()
        stream.send(BridgeFrame(
            type: .hello,
            sequence: 0,
            payload: deviceID + Data([0x05, 0x00]) + nonce
        ))
    }

    func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: Error?) {
        if error == nil { update { $0.rssi = RSSI.intValue } }
    }
}
