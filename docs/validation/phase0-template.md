# Packet Tunnel Bluetooth Phase 0

Date: ___
Tester: ___

## Versions

- StopWatch hardware revision: ___
- Device firmware commit: ___
- ESP-IDF: 6.0
- iPhone model: ___
- iOS version: ___
- Xcode version: ___
- iOS app commit: ___

## Link evidence

- Secure pairing and reconnecting bond: pass/fail
- Device UI TX/RX PHY: ___ / ___ (must be 2M / 2M)
- Device UI connection interval: ___ ms (target 15 ms)
- CoC peer MTU / MPS: ___ / ___
- RSSI range: ___ dBm

## Gate

- Foreground echo: pass/fail
- Locked duration: ___ minutes (must be at least 30)
- Minimum sustained one-way payload: ___ KB/s (must be greater than 50)
- Forced disconnect recovery: pass/fail
- Extension terminated or jetsammed: yes/no
- Extension memory start/end/peak: ___ / ___ / ___ MB
- Dropped frames / protocol errors: ___ / ___
- Gate: PASS/FAIL

Do not start the full IP forwarder when any required item fails. Record the
failure unchanged and evaluate the foreground/background CoreBluetooth
fallback described in the approved design.
