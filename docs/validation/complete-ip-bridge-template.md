# Complete IP Bridge Physical Validation

The user flashes the ESP32 firmware and records the physical-device results below.

## Build under test

- Date/time:
- Device commit:
- iOS commit:
- iPhone/iOS:
- Transport: PHY ___ / interval ___ ms / CoC MTU ___ / MPS ___

## Connectivity

| Check | Expected | Result / evidence |
| --- | --- | --- |
| DNS | Device resolves a public hostname | |
| TCP | Device completes a public TCP request | |
| UDP | Device receives a UDP response | |
| HTTPS | Bing metadata and JPEG pass TLS validation | |
| Bing wallpaper | Current image, date, and caption appear | |
| Throughput | Sustained one-way rate >50 KB/s | |
| Offline cache | Last valid wallpaper remains after bridge loss/reboot | |

## Lifecycle and recovery

| Check | Expected | Result / evidence |
| --- | --- | --- |
| Lock screen | VPN and bridge remain available | |
| Swipe-kill host App | VPN extension remains active when not under Xcode | |
| BLE reconnect | Bridge recovers without reinstalling VPN | |
| Clear pairing (App) | Re-pair prompt appears without opening Settings | |
| Clear pairing (Device) | Re-pair prompt appears without opening Settings | |
| Wi-Fi to cellular | Sessions recover and new traffic succeeds | |
| Cellular to Wi-Fi | Sessions recover and new traffic succeeds | |

## Device interaction and stability

| Check | Expected | Result / evidence |
| --- | --- | --- |
| Round home | No text or touch target is clipped | |
| KEY B short | Brightness cycles once | |
| KEY B hold 1.2 s | Internal tools opens/closes once; no brightness change | |
| DNS/HTTPS/PING tools | Result and counters update | |
| Wallpaper refresh | Busy/result/error state is readable | |
| Clear/power confirmations | Destructive action requires confirmation | |
| 30-minute soak | No crash, watchdog, or unexpected disconnect | |
| Heap | Start ___ / minimum ___ / end ___ bytes | |
| Drops | BLE queue ___ / IP drops ___ / invalid ___ | |

## Failure log

- Device serial excerpt:
- Packet Tunnel excerpt:
- Reproduction steps:
