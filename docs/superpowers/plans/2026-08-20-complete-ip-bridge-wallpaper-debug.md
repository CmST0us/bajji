# Complete IP Bridge, Bing Wallpaper, and Debug Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make StopWatch use the iPhone as an IPv4 TCP/UDP/DNS uplink over BLE L2CAP CoC, fetch and display the Bing daily wallpaper itself, and expose diagnostics through a KEY B long-press tools view.

**Architecture:** ESP-IDF registers a point-to-point lwIP default netif whose complete IPv4 packets enter a NimBLE-owned bounded queue. The iOS Packet Tunnel feeds those packets through a datagram socketpair into pinned, symbol-isolated HEV tun2socks and loopback SOCKS5 libraries. A Device background service uses normal lwIP sockets through `esp_http_client`, persists one validated JPEG, and lets LVGL render it without blocking the UI thread.

**Tech Stack:** ESP-IDF 6.0, lwIP, NimBLE, FreeRTOS, LVGL 9.5, SPIFFS, `esp_http_client`, cJSON, Swift 6, CoreBluetooth, NetworkExtension, POSIX sockets, HEV tun2socks/SOCKS5.

**Spec:** `docs/superpowers/specs/2026-08-20-complete-ip-bridge-wallpaper-debug-design.md`

## Global Constraints

- Do not flash the StopWatch; the user performs flashing and physical tests.
- Use `/Users/eki/esp/esp-idf` at ESP-IDF v6.0.
- Device is `10.77.0.2/30`, gateway `10.77.0.1`, DNS `1.1.1.1`, MTU 1280.
- Support IPv4 TCP/UDP/DNS only; reject IPv6 and fragmented IPv4.
- Every queue is bounded and drops only complete newest packets.
- Pin HEV to `0428c4ebb0df933ebac8e507832f252ef7da47f1` and `b6e41fe7c1a30aa5b8ac425233d3c95cd618a214`.
- Keep generated `ios/ThirdParty`, ESP-IDF build, DerivedData, and local signing files ignored.
- Preserve `com.eric3u.bajji` identifiers and current signing settings.

## File Map

- `device/components/ip_bridge/`: pure IPv4 validation, lwIP netif, time sync, and counters.
- `device/components/ble_link/ble_link.c`: encrypted CoC and NimBLE-host-owned send queue.
- `ios/Shared/IPv4Packet.swift`, `PacketPipe.swift`: testable packet boundary code.
- `ios/PacketTunnel/HEVForwarder.swift`, `IPForwarder.swift`: HEV and BLE packet lifecycle.
- `ios/tools/fetch_forwarder_deps.py`: exact checkout, build, symbol isolation, verification.
- `device/components/wallpaper/`: HTTPS, metadata/JPEG validation, persistent cache.
- `device/components/ui/diagnostics_ui.cpp`: round home and internal tools.
- `device/components/board_hal/button_state.hpp`: testable short/long press state.

---

### Task 1: Extend Bridge v1 and validate IPv4 identically

**Files:**
- Modify: `protocol/bridge-v1.md`
- Modify: `device/components/bridge_protocol/include/bridge_protocol.h`
- Modify: `device/components/bridge_protocol/bridge_protocol.c`
- Create: `device/components/ip_bridge/include/ipv4_packet.h`
- Create: `device/components/ip_bridge/ipv4_packet.c`
- Modify: `device/tests/host/test_bridge_protocol.c`
- Create: `device/tests/host/test_ipv4_packet.c`
- Modify: `device/tests/host/run.sh`
- Create: `ios/Shared/IPv4Packet.swift`
- Modify: `ios/Shared/BridgeFrame.swift`
- Create: `ios/BajjiTests/IPv4PacketTests.swift`

**Interfaces:** C `ipv4_packet_validate(bytes, length, expected_address, from_device)` and Swift `IPv4Packet.validate(_:expectedAddress:direction:)`; new 8-byte `TIME_SYNC` frame kind `0x22`.

- [x] Write failing C checks for version, IHL, total length, MF/offset, source/destination address, and TIME_SYNC encode/parse.

```c
assert(ipv4_packet_validate(valid, sizeof(valid), expected, true) == IPV4_PACKET_OK);
assert(ipv4_packet_validate(fragment, sizeof(fragment), expected, true) == IPV4_PACKET_FRAGMENTED);
```

- [x] Run `device/tests/host/run.sh`; expect missing validator and frame kind.
- [x] Implement the pure C enum/function. Validate size, version/IHL, exact total length, fragmentation, then address.
- [x] Write the same Swift cases plus TUN prefix tests. Prefix is `UInt32(AF_INET).bigEndian`; reject wrong AF or truncated datagrams.
- [x] Run `device/tests/host/run.sh && swift test --package-path ios`; expect PASS.
- [x] Commit with `git commit -m "feat(protocol): validate production IPv4 frames"`.

### Task 2: Make BLE TX thread-safe and register the lwIP default netif

**Files:**
- Create: `device/components/ip_bridge/CMakeLists.txt`
- Create: `device/components/ip_bridge/include/ip_bridge.h`
- Create: `device/components/ip_bridge/ip_bridge.c`
- Modify: `device/components/ble_link/include/ble_link.h`
- Modify: `device/components/ble_link/ble_link.c`
- Modify: `device/components/ble_link/CMakeLists.txt`
- Modify: `device/main/CMakeLists.txt`
- Modify: `device/main/app_main.cpp`

**Interfaces:** `ip_bridge_start`, `ip_bridge_set_link`, `ip_bridge_receive`, `ip_bridge_snapshot`; `ble_link_set_handlers(frame_handler, ready_handler, context)`.

- [x] Change `ble_link` so it retains HELLO, PING/PONG, and CLEAR_BOND handling and forwards only IPV4/TIME_SYNC. Valid HELLO raises ready; disconnect lowers it.
- [x] Initialize an NPL TX event. `ble_link_send` only encodes/appends under the lock and schedules that event; only its host callback and TX_UNSTALLED touch `ble_l2cap_send`, channel, offset, or stall state.
- [x] In `ip_bridge_start`, call `esp_netif_init`, then `netifapi_netif_add`, set default/up, use a raw non-ARP netif (ESP-IDF 6.0 lwIP has no `NETIF_FLAG_POINTTOPOINT`), set MTU, and configure DNS `1.1.1.1`.

```c
IP4_ADDR(&ip, 10, 77, 0, 2);
IP4_ADDR(&mask, 255, 255, 255, 252);
IP4_ADDR(&gateway, 10, 77, 0, 1);
```

- [x] Copy one pbuf chain to one Bridge frame. Return `ERR_MEM` when the transport is unavailable/full.
- [x] Validate incoming IPv4, allocate one `PBUF_RAW`, and call `tcpip_input`. Decode plausible 2024–2100 TIME_SYNC values and call `settimeofday`.
- [x] Wire `ip_bridge` before BLE in `app_main`, remove production IPv4 echo, and add packet/drop logs.
- [x] Run `source /Users/eki/esp/esp-idf/export.sh && idf.py -C device build`; never flash.
- [x] Commit with `git commit -m "feat(device): add Bluetooth lwIP netif"`.

### Task 3: Bootstrap pinned, symbol-isolated HEV XCFrameworks

**Files:**
- Create: `ios/tools/fetch_forwarder_deps.py`
- Create: `ios/Forwarder/THIRD_PARTY_NOTICES.md`
- Modify: `ios/README.md`
- Modify: `.gitignore`

**Interfaces:** produces ignored `HevSocks5Tunnel.xcframework` and `HevSocks5Server.xcframework`; keeps public server APIs unchanged; leaves no defined global symbol collisions.

- [x] Implement recursive exact checkout. Clone if absent, fetch exact commit, detached checkout, recursive submodule update, and assert `HEAD` equals the pin.
- [x] Run each upstream `build-apple.sh` and place only generated XCFrameworks under `ios/ThirdParty`.
- [x] For each matching archive slice, obtain defined globals using `xcrun nm -gjU`, intersect names, exclude `_hev_socks5_server_*`, map each remaining server name to `_bajji_server_<name>`, and run `llvm-objcopy --redefine-syms`.
- [x] Re-run `nm`; fail unless the non-public intersection is empty. Locate objcopy through `xcrun --find` then `PATH`, otherwise print an actionable error.
- [x] Run `python3 ios/tools/fetch_forwarder_deps.py`; expect iOS arm64/simulator slices and zero collisions.
- [x] Commit scripts/notices only with `git commit -m "build(ios): bootstrap isolated HEV forwarder"`.

### Task 4: Implement PacketPipe and HEV lifecycle

**Files:**
- Create: `ios/Shared/PacketPipe.swift`
- Create: `ios/PacketTunnel/HEVForwarder.swift`
- Create: `ios/PacketTunnel/IPForwarder.swift`
- Create: `ios/BajjiTests/PacketPipeTests.swift`
- Modify: `ios/Package.swift`
- Modify: `ios/Bajji.xcodeproj/project.pbxproj`

**Interfaces:** `PacketPipe.writePacket/readPacket/forwarderFD/close`; `HEVForwarder.start(fd:)/stop/snapshot`; `IPForwarder.start(sendToDevice:)/receiveFromDevice/stop/snapshot`.

- [x] Write failing tests proving one datagram per IP packet, two writes remain two reads, AF prefix correctness, and invalid/truncated rejection.
- [x] Implement `socketpair(AF_UNIX, SOCK_DGRAM, 0)` and set both fds nonblocking. Cap datagrams at 1284 bytes and make close idempotent.
- [x] Reserve a loopback TCP port, start server on one queue, wait up to two seconds for readiness, then start tunnel on another queue.

```yaml
main:
  workers: 1
  port: PORT
  listen-address: '127.0.0.1'
  udp-port: 0
  udp-listen-address: '127.0.0.1'
  domain-address-type: ipv4
```

Tunnel YAML uses MTU 1280, IPv4 `10.77.0.1`, SOCKS `127.0.0.1:PORT`, UDP mode `udp`, 24576-byte task stack, 4096-byte TCP buffer, and 128 max sessions.
- [x] Implement `IPForwarder`: BLE packets enter PacketPipe; a `DispatchSourceRead` drains validated destination-`10.77.0.2` responses and emits one IPV4 frame. Lock counters and last error.
- [x] Add Swift files and both XCFrameworks to PacketTunnel; static frameworks are linked with Do Not Embed.
- [x] Run `swift test --package-path ios` and unsigned `xcodebuild` for scheme `BajjiBridge` on generic iOS.
- [x] Commit with `git commit -m "feat(ios): add bounded HEV IP forwarder"`.

### Task 5: Replace Phase Zero with production tunnel lifecycle

**Files:**
- Modify: `ios/PacketTunnel/PacketTunnelProvider.swift`
- Modify: `ios/PacketTunnel/BluetoothBridge.swift`
- Modify: `ios/PacketTunnel/L2CAPStream.swift`
- Modify: `ios/BajjiApp/TunnelManager.swift`
- Modify: `ios/BajjiApp/ContentView.swift`

**Interfaces:** snapshot includes Bluetooth and Forwarder; provider messages are snapshot and binding clear.

- [x] After HELLO, send network-order `UInt64(Date().timeIntervalSince1970)` TIME_SYNC before ready.
- [x] Start IPForwarder before Bluetooth. Route BLE IPV4 to it and its output to `BluetoothBridge.send`.
- [x] On BLE loss, clear sessions and recreate Forwarder before next ready while VPN remains Waiting. On satisfied `NWPathMonitor` change, restart sessions but preserve BLE.
- [x] Remove Phase Zero after production forwarding is verified.
- [x] Show separate VPN, Bluetooth, and Internet statuses plus IP bytes/packets/drops/invalid/last error.
- [x] Run unsigned iOS build and commit `feat(ios): run production bridge data plane`.

### Task 6: Fetch, validate, and persist the Bing wallpaper

**Files:**
- Create: `device/components/wallpaper/CMakeLists.txt`
- Create: `device/components/wallpaper/include/wallpaper_service.hpp`
- Create: `device/components/wallpaper/wallpaper_service.cpp`
- Create: `device/components/wallpaper/include/wallpaper_format.h`
- Create: `device/components/wallpaper/wallpaper_format.c`
- Create: `device/tests/host/test_wallpaper_format.c`
- Modify: `device/tests/host/run.sh`
- Modify: `device/partitions.csv`
- Modify: `device/sdkconfig.defaults`
- Modify: `device/main/CMakeLists.txt`
- Modify: `device/main/app_main.cpp`

**Interfaces:** start/set-online/refresh/DNS-test/HTTPS-test/snapshot functions; fixed-size snapshot strings; cache `/spiffs/wallpaper.jpg`, LVGL path `S:/wallpaper.jpg`.

- [x] Write pure JPEG tests for SOI/EOI, marker bounds, SOF dimensions, wrong format, truncation, and maximum size.
- [x] Add 2 MiB SPIFFS after coredump. Enable certificate bundle, TJPGD, and LVGL stdio drive `S` rooted at `/spiffs`.
- [x] Mount without ordinary destructive auto-format; format once only for an unformatted partition, then remount.
- [x] GET the approved metadata URL with CA validation, 10-second timeout, and 8192-byte cap. Require one cJSON image and string urlbase/startdate/copyright; build only an HTTPS Bing `_480x800.jpg` URL.
- [x] Stream at most 1 MiB to `wallpaper.jpg.tmp`, require 2xx/JPEG/SOI/EOI/SOF, then rename over the cache and save metadata. Any failure removes only temp and preserves cache.
- [x] Worker waits for Online and valid time, skips unchanged startdate, retries 60 s/5 min/15 min/60 min, and serializes manual refresh/DNS/HTTPS tests.
- [x] Run host tests and IDF build; commit `feat(device): fetch and cache Bing wallpaper`.

### Task 7: Add KEY B semantics, round home, and internal tools

**Files:**
- Create: `device/components/board_hal/button_state.hpp`
- Create: `device/tests/host/test_button_state.cpp`
- Modify: `device/tests/host/run.sh`
- Modify: `device/components/board_hal/include/board_hal.hpp`
- Modify: `device/components/board_hal/board_hal.cpp`
- Modify: `device/components/ui/include/diagnostics_ui.hpp`
- Modify: `device/components/ui/diagnostics_ui.cpp`
- Modify: `device/components/ui/CMakeLists.txt`
- Modify: `device/main/app_main.cpp`

**Interfaces:** one-shot `button_b_long_pressed`; UI refresh accepts board/BLE/IP/wallpaper snapshots; `toggle_tools` switches screens.

- [x] Write a host test: release before 1.2 s does nothing; holding 1.2 s emits one long event.
- [x] Replace KEY B edge latch with the tested long-press state machine. KEY A has no validation side effect.
- [x] Create a 466×466 circular clipped home, cover-align `S:/wallpaper.jpg`, and place status/time/caption/touch targets inside the 328×328 safe square.
- [x] On wallpaper revision, call `lv_image_cache_drop("S:/wallpaper.jpg")`, reset source, and invalidate. Hide image when no cache.
- [x] Reuse the scrollable diagnostics layout as tools sections: Network, Bluetooth, Tests, Security, Power. Preserve confirmations for clear bond and shutdown.
- [x] Short B does nothing; long B toggles tools under LVGL mutex; refresh all snapshots every 100 ms.
- [x] Run host tests and IDF build; commit `feat(device): add round home and internal tools`.

### Task 8: Verify and prepare physical handoff

**Files:**
- Create: `docs/validation/complete-ip-bridge-template.md`
- Modify: `ios/README.md`
- Modify: this plan to mark completed checkboxes.

- [x] Run host tests, ESP-IDF build, Swift tests, unsigned generic-iOS build, `git diff --check`, and `git status --short`.
- [x] Audit source evidence for default netif, removed production echo, HEV TCP/UDP, DNS/HTTPS, TIME_SYNC, safe cache replacement, round layout, KEY B long press, bounded queues, diagnostics, and ignored artifacts.
- [x] Add physical fields for DNS/TCP/UDP/HTTPS, Bing date/caption, >50 KB/s, lock screen, killed host App, BLE reconnect, Wi-Fi/cellular switch, offline cache, KEY B, heap, drops, crash/watchdog. State that the user flashes and fills results.
- [x] Commit `test: prepare complete bridge validation`.
