<div align="center">

# Bajji

**An IPv4 uplink and wallpaper experience for the BLE-only M5Stack StopWatch.**

[**English**](README.md) · [简体中文](README_zh.md)

![ESP-IDF 6.0](https://img.shields.io/badge/ESP--IDF-6.0-E7352C?logo=espressif&logoColor=white)
![Swift 6.0](https://img.shields.io/badge/Swift-6.0-F05138?logo=swift&logoColor=white)
![iOS 26](https://img.shields.io/badge/iOS-26-111827?logo=apple&logoColor=white)
![Hardware: M5Stack StopWatch](https://img.shields.io/badge/Hardware-M5Stack%20StopWatch-ED1C24)

</div>

![Bajji architecture](docs/images/bajji-architecture.svg)

Bajji lets an [M5Stack StopWatch](https://docs.m5stack.com/en/core/StopWatch) reach the Internet through an iPhone. The watch has no Wi-Fi radio: its ESP32-S3 firmware carries IPv4 packets over an encrypted Bluetooth LE credit-based L2CAP channel, while an iOS Packet Tunnel forwards TCP, UDP, and DNS through the phone's current Wi-Fi or cellular connection.

The end-to-end demo runs on the watch itself: it securely pairs with one iPhone, downloads random wallpapers over HTTPS, caches them locally, and renders still or animated media on the round AMOLED display.

> [!IMPORTANT]
> Bajji is an experimental hardware project. The iOS bridge requires a physical iPhone running iOS 26 and developer signing with Packet Tunnel and App Group entitlements. It is not an App Store or TestFlight distribution.

## Highlights

- **No Wi-Fi provisioning** — the StopWatch uses its existing BLE radio as a point-to-point IPv4 link.
- **Secure one-to-one pairing** — LE Secure Connections, a six-digit passkey shown on the watch, persistent bonding, and explicit bond reset.
- **Useful network semantics** — IPv4 with TCP, UDP, and DNS; a 1280-byte MTU avoids fragmentation on the bridge.
- **Round-screen product UI** — on-device pairing, wallpaper category settings, Cover and Fit+Blur display modes, touch controls, and physical A/B button shortcuts.
- **Resilient media pipeline** — JPEG, PNG, GIF, static WebP, and animated WebP with validation, size limits, atomic cache replacement, and offline fallback.
- **Shared wire contract** — C and Swift implementations are checked against the same [Bridge v1 test vectors](protocol/vectors/bridge-v1.json).

## Wallpaper experience

<p align="center">
  <img src="docs/assets/bajji-stopwatch-sample-wallpaper.png" width="480" alt="Sample wallpaper bundled with Bajji">
</p>
<p align="center"><em>Sample wallpaper bundled for display work; the device fetches new images through the BLE bridge.</em></p>

On the image screen, **KEY A** switches between Cover and Fit+Blur, **KEY B** requests another wallpaper, and holding **A+B** for one second returns to settings. The last valid image stays available after a reboot or bridge loss.

## How it works

1. The StopWatch creates a point-to-point lwIP interface at `10.77.0.2/30`.
2. [Bridge v1](protocol/bridge-v1.md) frames complete IPv4 packets over an encrypted LE L2CAP CoC.
3. The Packet Tunnel extension owns CoreBluetooth and exposes the peer gateway at `10.77.0.1/30`.
4. A UNIX datagram pipe connects those packets to pinned HEV tunnel and loopback SOCKS forwarders.
5. The forwarder opens TCP and UDP traffic through the iPhone's active uplink and returns reconstructed IPv4 packets to the watch.

Only `10.77.0.0/30` is installed in the Packet Tunnel. Bajji does not replace the iPhone's default route or proxy the phone's own traffic.

## Hardware and toolchain

| Part | Project target |
|---|---|
| Watch | M5Stack StopWatch |
| SoC | ESP32-S3R8, 240 MHz |
| Memory | 16 MB flash, 8 MB OPI PSRAM |
| Display | Round CO5300 AMOLED, 468×468 panel |
| Device SDK | ESP-IDF 6.0, LVGL 9.5 |
| Phone | Physical iPhone with iOS 26 |
| iOS toolchain | Xcode 26, Swift 6 |

The display is the only component outside the shared system I²C bus. Hardware notes, the schematic, and the complete pin map live in [`docs/hardware`](docs/hardware/description.md).

## Quick start

### 1. Build and flash the firmware

Install and export ESP-IDF 6.0 so that `idf.py` is on your `PATH`, then run:

```sh
cd device
python3 tools/fetch_deps.py
idf.py build
idf.py -p <PORT> flash monitor
```

`fetch_deps.py` checks out the pinned sources under the generated `device/vendor/` directory and applies the repository patches. The flash command writes the bootloader, partition table, and application.

### 2. Build the iOS bridge

On macOS with Xcode 26:

```sh
python3 ios/tools/fetch_forwarder_deps.py
open ios/Bajji.xcodeproj
```

Configure your developer team, unique bundle identifiers, and matching App Group under `ios/Config/`. In Xcode, select the **BajjiBridge** scheme and run it on a physical iPhone. The Simulator cannot provide the required BLE and Packet Tunnel path.

### 3. Pair and connect

1. Start the flashed StopWatch.
2. In the iOS app, tap **Install VPN Profile**, then **Start Bridge**.
3. Enter the six-digit passkey shown on the watch when iOS prompts.
4. Choose a wallpaper category on the watch and save it. The first HTTPS request begins when the bridge and time sync are ready.

For lifecycle testing, detach the Xcode debugger before swiping away the host app; stopping a debug session also terminates the extension.

## Tests

The fast checks do not require StopWatch hardware:

```sh
bash device/tests/host/run.sh
swift test --package-path ios
xcodebuild -project ios/Bajji.xcodeproj -scheme BajjiBridge \
  -configuration Debug -sdk iphoneos -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO build
```

Use the [physical bridge validation template](docs/validation/complete-ip-bridge-template.md) for pairing, throughput, lock-screen, reconnect, network-change, cache, and soak checks.

## Repository layout

| Path | Purpose |
|---|---|
| [`device/`](device/) | ESP-IDF firmware, board HAL, LVGL UI, BLE link, lwIP bridge, and wallpaper pipeline |
| [`ios/`](ios/) | SwiftUI host app, Packet Tunnel extension, shared Bridge codec, tests, and pinned forwarder build |
| [`protocol/`](protocol/) | Bridge v1 specification and cross-language test vectors |
| [`docs/`](docs/) | Hardware references, validation templates, design specs, and images |
| [`wiki/`](wiki/README.md) | Verified engineering notes and platform-specific failure modes |

## Current scope

- IPv4 only; IPv6, ICMP, and IP fragmentation/reassembly are not supported.
- One bonded iPhone and one StopWatch; no concurrent or multi-device bridge.
- The Packet Tunnel as a BLE bridge host is experimental and needs physical-device lifecycle validation.
- The watch deliberately has no Wi-Fi path, account system, favorites, history, or scheduled wallpaper rotation.

Protocol details are normative in [`protocol/bridge-v1.md`](protocol/bridge-v1.md). iOS-specific setup and lifecycle notes are in [`ios/README.md`](ios/README.md).
