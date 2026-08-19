# Complete IPv4 Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After Phase 0 passes, carry Device IPv4 TCP/UDP/DNS traffic over BLE and forward it through the iPhone's current network.

**Architecture:** Device installs a point-to-point lwIP netif whose output is Bridge IPV4 frames. iOS connects the L2CAP frame stream to a UNIX datagram PacketPipe, pinned HEV tun2socks, and a loopback-only pinned HEV SOCKS5 server.

**Tech Stack:** ESP-IDF 6.0 lwIP/NimBLE, Swift 6, POSIX socketpair, hev-socks5-tunnel, hev-socks5-server, XCTest.

**Spec:** `docs/superpowers/specs/2026-08-19-bajji-stopwatch-ble-ip-bridge-design.md`

## Global Constraints

- This plan cannot start until `docs/validation/` contains a Phase 0 PASS record.
- Device is `10.77.0.2/30`, gateway is `10.77.0.1`, MTU is 1280 and DNS is `1.1.1.1`.
- First release supports IPv4 TCP/UDP/DNS only; IPv6, ICMP and fragments are rejected.
- HEV commits are exactly `0428c4ebb0df933ebac8e507832f252ef7da47f1` and `b6e41fe7c1a30aa5b8ac425233d3c95cd618a214`.
- Session count is capped at 128 and all queues are bounded.

---

## File Map

- `device/components/ip_bridge/`: lwIP netif, link state and BLE frame queues.
- `device/tests/host/test_ipv4_filter.c`: packet validation checks.
- `ios/Forwarder/PacketPipe.swift`: datagram socketpair and four-byte AF prefix.
- `ios/Forwarder/HEVForwarder.{h,c,swift}`: narrow C bridge and lifecycle.
- `ios/ThirdParty/`: pinned HEV checkouts produced by bootstrap and not committed.
- `ios/tools/fetch_forwarder_deps.py`: exact, idempotent HEV checkout.
- `ios/BajjiTests/PacketPipeTests.swift`: packet-boundary checks.
- `docs/validation/end-to-end-template.md`: final evidence.

### Task 1: Device IPv4 filter and lwIP netif

**Files:**
- Create: `device/components/ip_bridge/CMakeLists.txt`
- Create: `device/components/ip_bridge/include/ip_bridge.h`
- Create: `device/components/ip_bridge/ip_bridge.c`
- Create: `device/components/ip_bridge/ipv4_filter.c`
- Create: `device/tests/host/test_ipv4_filter.c`
- Modify: `device/main/app_main.cpp`

**Interfaces:**
- Consumes: `ble_link_send(const bridge_frame_t *)` and received IPV4 callback.
- Produces: `ip_bridge_start`, `ip_bridge_set_link`, `ip_bridge_receive`, `ip_bridge_snapshot`.

- [ ] **Step 1: Write failing validation checks**

```c
assert(ipv4_validate(valid_udp, sizeof(valid_udp)) == IPV4_ACCEPT);
assert(ipv4_validate(ipv6, sizeof(ipv6)) == IPV4_REJECT_VERSION);
assert(ipv4_validate(fragment, sizeof(fragment)) == IPV4_REJECT_FRAGMENT);
assert(ipv4_validate(oversize, 1281) == IPV4_REJECT_SIZE);
```

- [ ] **Step 2: Implement strict boundary validation**

```c
if ((packet[0] >> 4) != 4) return IPV4_REJECT_VERSION;
if (length > 1280 || length < 20) return IPV4_REJECT_SIZE;
uint16_t frag = ((uint16_t)packet[6] << 8) | packet[7];
if ((frag & 0x3FFFu) != 0) return IPV4_REJECT_FRAGMENT;
```

- [ ] **Step 3: Register the point-to-point netif**

```c
IP4_ADDR(&ip, 10, 77, 0, 2);
IP4_ADDR(&mask, 255, 255, 255, 252);
IP4_ADDR(&gateway, 10, 77, 0, 1);
netif_add(&bridge_netif, &ip, &mask, &gateway, NULL, bridge_netif_init, tcpip_input);
bridge_netif.mtu = 1280;
```

The output callback copies one complete pbuf chain into one bounded Bridge IPV4 frame. HELLO_ACK sets link up; disconnect sets link down and flushes queues.

- [ ] **Step 4: Verify host checks and IDF build**

Run: `device/tests/host/run.sh && cd device && source /Users/eki/esp/esp-idf/export.sh && idf.py build`

Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add device
git commit -m "feat(device): add lwIP Bluetooth netif"
```

### Task 2: PacketPipe and pinned HEV build

**Files:**
- Create: `ios/tools/fetch_forwarder_deps.py`
- Create: `ios/Forwarder/PacketPipe.swift`
- Create: `ios/Forwarder/HEVForwarder.h`
- Create: `ios/Forwarder/HEVForwarder.c`
- Create: `ios/Forwarder/HEVForwarder.swift`
- Create: `ios/BajjiTests/PacketPipeTests.swift`
- Modify: `.gitignore`
- Modify: `ios/Bajji.xcodeproj/project.pbxproj`

**Interfaces:**
- Produces: `PacketPipe.writeIPv4(_:)`, `readIPv4()`, `HEVForwarder.start(fd:)`, `stop()`.

- [ ] **Step 1: Write failing datagram-boundary tests**

```swift
func testPacketPipePreservesOneDatagram() throws {
    let pipe = try PacketPipe()
    try pipe.writeIPv4(Data([0x45, 0, 0, 20]))
    XCTAssertEqual(try pipe.readIPv4(), Data([0x45, 0, 0, 20]))
}
```

- [ ] **Step 2: Implement AF-prefixed socketpair**

```swift
var fds = [Int32](repeating: -1, count: 2)
guard socketpair(AF_UNIX, SOCK_DGRAM, 0, &fds) == 0 else { throw POSIXError(.ENOTSOCK) }
var datagram = Data([0, 0, 0, UInt8(AF_INET)])
datagram.append(packet)
```

- [ ] **Step 3: Fetch exact HEV commits**

The Python script clones each repository into `ios/ThirdParty`, checks detached `HEAD` equals the required commit, and exits nonzero on mismatch. It generates no project files and makes no network listener other than `127.0.0.1`.

- [ ] **Step 4: Add the smallest C lifecycle bridge**

```c
int bajji_hev_start(int tun_fd, const char *socks_address, uint16_t port);
void bajji_hev_stop(void);
```

Configuration uses one worker, loopback SOCKS5, TCP/UDP enabled and 128 maximum sessions.

- [ ] **Step 5: Run tests and unsigned build**

Run: `swift test --package-path ios && xcodebuild build -project ios/Bajji.xcodeproj -scheme Bajji -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO`

Expected: PacketPipe tests and existing parser tests pass.

- [ ] **Step 6: Commit**

```bash
git add .gitignore ios
git commit -m "feat(ios): add bounded IP packet forwarder"
```

### Task 3: Connect BLE frames to the forwarder

**Files:**
- Modify: `ios/PacketTunnel/PacketTunnelProvider.swift`
- Modify: `ios/PacketTunnel/BluetoothBridge.swift`
- Modify: `ios/BajjiApp/ContentView.swift`

**Interfaces:**
- Consumes: Bridge IPV4 frame stream and `PacketPipe`.
- Produces: Online bridge state, counters, reconnect and path-change recovery.

- [ ] **Step 1: Start and stop components in ownership order**

```swift
try forwarder.start(fd: pipe.forwarderFD)
try await bluetooth.start()
// stop: Bluetooth -> Forwarder -> PacketPipe
```

- [ ] **Step 2: Bridge only complete IPv4 packets**

```swift
case .ipv4:
    guard frame.payload.count <= 1280, frame.payload.first.map({ $0 >> 4 == 4 }) == true else {
        stats.invalidPackets += 1
        return
    }
    try packetPipe.writeIPv4(frame.payload)
```

Pipe responses become one IPV4 frame each. Queue overflow drops the newest complete packet and increments `droppedPackets`.

- [ ] **Step 3: Recover on link and path changes**

BLE disconnect stops HEV and clears sessions; reconnect creates a new PacketPipe/HEV instance. `NWPathMonitor` changes restart only forwarder sessions while preserving BLE.

- [ ] **Step 4: Build and commit**

Run: `xcodebuild build -project ios/Bajji.xcodeproj -scheme Bajji -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO`

Expected: exit 0.

```bash
git add ios
git commit -m "feat(ios): bridge L2CAP IPv4 to iPhone network"
```

### Task 4: End-to-end validation

**Files:**
- Create: `device/main/network_smoke_test.cpp`
- Create: `docs/validation/end-to-end-template.md`

**Interfaces:**
- Produces: opt-in Device commands for DNS, TCP HTTPS and UDP echo plus a dated validation record.

- [ ] **Step 1: Add opt-in network checks**

```cpp
resolve("example.com");
https_get("https://example.com/");
udp_echo(configured_test_host, configured_test_port, 32);
```

The UDP echo host and port are compile-time test settings, not a production dependency.

- [ ] **Step 2: Execute all physical checks**

Verify DNS, HTTP/HTTPS, UDP echo, >50 KB/s one-way payload, BLE off/on, CoC disconnect/reconnect and Wi-Fi/cellular switching. Confirm IPv6, ICMP and fragmented packets are rejected and counted.

- [ ] **Step 3: Commit evidence**

```bash
git add device/main/network_smoke_test.cpp docs/validation
git commit -m "test: verify end-to-end Bluetooth network bridge"
```
