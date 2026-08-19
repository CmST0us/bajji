# iOS Packet Tunnel Phase 0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove on iOS 26 hardware that a Packet Tunnel extension can own CoreBluetooth, open the encrypted StopWatch CoC, sustain more than 50 KB/s while locked for 30 minutes, and reconnect.

**Architecture:** A small SwiftUI containing app installs the VPN configuration. The Packet Tunnel owns a restoration-enabled `CBCentralManager`, Bridge v1 stream codec, L2CAP channel and Phase 0 traffic generator; shared state is snapshots only.

**Tech Stack:** Swift 6.3, SwiftUI, CoreBluetooth, NetworkExtension, XCTest, Xcode 26.5.

**Spec:** `docs/superpowers/specs/2026-08-19-bajji-stopwatch-ble-ip-bridge-design.md`

## Global Constraints

- Deployment target is iOS 26.0 and validation requires a development-signed physical iPhone.
- Packet Tunnel is the sole CoreBluetooth owner.
- VPN installs only route `10.77.0.0/30`; it never installs a default route.
- Phase 0 is failed by any missing locked-screen, throughput, reconnect or memory criterion.
- No full TCP/UDP forwarder is added before the gate passes.

---

## File Map

- `ios/Bajji.xcodeproj/project.pbxproj`: app, PacketTunnel and test targets.
- `ios/Package.swift`: host-runnable package for platform-independent Shared tests.
- `ios/Config/*.xcconfig`: checked-in identifiers plus ignored local signing overrides.
- `ios/Shared/BridgeFrame.swift`, `BridgeStreamParser.swift`, `BridgeInfo.swift`: protocol values.
- `ios/BajjiApp/`: VPN install/start/status UI.
- `ios/PacketTunnel/PacketTunnelProvider.swift`: extension lifecycle.
- `ios/PacketTunnel/BluetoothBridge.swift`: CoreBluetooth central and CoC stream owner.
- `ios/PacketTunnel/PhaseZeroRunner.swift`: throughput/reconnect counters.
- `ios/BajjiTests/`: parser and settings tests.

### Task 1: Swift protocol library and tests

**Files:**
- Create: `ios/Shared/BridgeFrame.swift`
- Create: `ios/Shared/BridgeStreamParser.swift`
- Create: `ios/Shared/BridgeInfo.swift`
- Create: `ios/Package.swift`
- Create: `ios/BajjiTests/BridgeStreamParserTests.swift`

**Interfaces:**
- Produces: `BridgeFrame.encode()`, `BridgeStreamParser.append(_:)`, `BridgeInfo.init(data:)`.

- [ ] **Step 1: Write failing XCTest cases**

```swift
func testSplitFrame() throws {
    var parser = BridgeStreamParser()
    XCTAssertEqual(try parser.append(Data([0xBA, 0x77, 1])), [])
    XCTAssertEqual(try parser.append(validPing.dropFirst(3)).first?.type, .ping)
}
func testRejectsOversizePayload() {
    var parser = BridgeStreamParser()
    XCTAssertThrowsError(try parser.append(oversizeHeader))
}
```

- [ ] **Step 2: Implement the bounded value types**

```swift
struct BridgeFrame: Equatable {
    enum Kind: UInt8 { case hello = 0x01, helloAck = 0x02, ipv4 = 0x10, ping = 0x20, pong = 0x21, error = 0x7F }
    let type: Kind
    let sequence: UInt16
    let payload: Data
}
```

Parser buffer capacity is 1288 bytes plus one incoming chunk; invalid versions, types or lengths throw `BridgeProtocolError` and clear state.

- [ ] **Step 3: Run the platform-independent unit tests**

Run: `swift test --package-path ios`

Expected: parser and BridgeInfo tests pass.

### Task 2: Xcode targets and containing app

**Files:**
- Create: `ios/Bajji.xcodeproj/project.pbxproj`
- Create: `ios/Config/Base.xcconfig`
- Create: `ios/Config/Local.xcconfig.example`
- Create: `ios/BajjiApp/BajjiApp.swift`
- Create: `ios/BajjiApp/ContentView.swift`
- Create: `ios/BajjiApp/TunnelManager.swift`
- Create: `ios/BajjiApp/Info.plist`
- Create: `ios/BajjiApp/Bajji.entitlements`
- Create: `ios/PacketTunnel/Info.plist`
- Create: `ios/PacketTunnel/PacketTunnel.entitlements`

**Interfaces:**
- Produces: `TunnelManager.install()`, `start()`, `stop()`, `sendMessage(_:)`.

- [ ] **Step 1: Configure exact targets and entitlements**

App embeds `com.apple.networkextension.packet-tunnel`; both targets share `group.com.cmstopus.bajji`. `Local.xcconfig.example` contains only `DEVELOPMENT_TEAM = YOUR_TEAM_ID`, while `Local.xcconfig` remains ignored.

```xml
<key>com.apple.developer.networking.networkextension</key>
<array><string>packet-tunnel-provider</string></array>
```

- [ ] **Step 2: Install a provider configuration**

```swift
let proto = NETunnelProviderProtocol()
proto.providerBundleIdentifier = "com.cmstopus.bajji.PacketTunnel"
proto.serverAddress = "StopWatch BLE"
manager.protocolConfiguration = proto
manager.localizedDescription = "Bajji StopWatch Bridge"
manager.isEnabled = true
try await manager.saveToPreferences()
```

- [ ] **Step 3: Build unsigned**

Run: `xcodebuild build -project ios/Bajji.xcodeproj -scheme Bajji -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO`

Expected: App and PacketTunnel compile for arm64 iOS.

- [ ] **Step 4: Commit**

```bash
git add ios
git commit -m "feat(ios): add Bajji app and packet tunnel"
```

### Task 3: Packet Tunnel BLE owner

**Files:**
- Create: `ios/PacketTunnel/PacketTunnelProvider.swift`
- Create: `ios/PacketTunnel/BluetoothBridge.swift`
- Create: `ios/PacketTunnel/L2CAPStream.swift`
- Create: `ios/PacketTunnel/PhaseZeroRunner.swift`

**Interfaces:**
- Consumes: saved 16-byte Device ID and Bridge service UUID.
- Produces: `BluetoothBridge.start()`, `stop()`, `snapshot()` and an `AsyncStream<BridgeFrame>`.

- [ ] **Step 1: Apply the narrow tunnel route**

```swift
let settings = NEPacketTunnelNetworkSettings(tunnelRemoteAddress: "10.77.0.1")
let ipv4 = NEIPv4Settings(addresses: ["10.77.0.1"], subnetMasks: ["255.255.255.252"])
ipv4.includedRoutes = [NEIPv4Route(destinationAddress: "10.77.0.0", subnetMask: "255.255.255.252")]
ipv4.excludedRoutes = [NEIPv4Route.default()]
settings.ipv4Settings = ipv4
settings.mtu = 1280
try await setTunnelNetworkSettings(settings)
```

- [ ] **Step 2: Restore, scan and open CoC**

```swift
central = CBCentralManager(delegate: self, queue: queue,
    options: [CBCentralManagerOptionRestoreIdentifierKey: "com.cmstopus.bajji.packet-tunnel.ble"])
central.scanForPeripherals(withServices: [BridgeUUID.service])
peripheral.openL2CAPChannel(info.psm)
```

Discover BridgeInfo, require the expected Device ID, and reject malformed 22-byte values before opening the channel.

- [ ] **Step 3: Implement stream scheduling and bounded writes**

Use the channel's input/output streams on the extension run loop. Reads feed `BridgeStreamParser`; writes retain at most 32 encoded frames and advance only on `.hasSpaceAvailable`.

```swift
while output.hasSpaceAvailable, let chunk = pending.first {
    let count = chunk.withUnsafeBytes { output.write($0.bindMemory(to: UInt8.self).baseAddress!, maxLength: $0.count) }
    guard count > 0 else { break }
    pending.removeFirst()
}
```

- [ ] **Step 4: Add Phase 0 measurements**

Generate 1280-byte IPV4 test frames for 60 seconds, compute payload bytes per elapsed second, record RSSI/connection/reconnect counters, and store a rolling snapshot. The StopWatch echo mode must be explicitly enabled from the App.

- [ ] **Step 5: Build and test**

Run: `swift test --package-path ios && xcodebuild build -project ios/Bajji.xcodeproj -scheme Bajji -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO`

Expected: build succeeds and all non-Bluetooth unit tests pass.

- [ ] **Step 6: Commit**

```bash
git add ios
git commit -m "spike(ios): validate Bluetooth inside packet tunnel"
```

### Task 4: Physical Phase 0 gate

**Files:**
- Create: `docs/validation/phase0-template.md`

**Interfaces:**
- Produces: one dated evidence record with device/OS versions, negotiated link values, throughput, reconnect and memory observations.

- [ ] **Step 1: Sign and install on a physical iPhone**

Create `ios/Config/Local.xcconfig` with the developer team, build the Debug scheme, flash the Device firmware, pair with the displayed six-digit passkey, and start the VPN.

- [ ] **Step 2: Execute the exact gate**

Run foreground bidirectional echo, lock the phone, run 30 minutes, require one-way payload above 50 KB/s, force one disconnect and observe recovery, then inspect extension memory for growth or jetsam.

- [ ] **Step 3: Record pass or fail without weakening criteria**

```markdown
- Locked duration: 30 minutes
- Minimum sustained one-way payload: ___ KB/s (must be > 50)
- Reconnect: pass/fail
- Extension terminated: yes/no
- Memory start/end/peak: ___ / ___ / ___ MB
- Gate: PASS/FAIL
```

- [ ] **Step 4: Commit evidence**

```bash
git add docs/validation
git commit -m "test: record packet tunnel Phase 0 result"
```
