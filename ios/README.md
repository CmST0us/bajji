# Bajji iOS Phase 0

The `BajjiBridge` scheme contains the SwiftUI app, Packet Tunnel extension and
tests. The extension alone owns CoreBluetooth and routes only `10.77.0.0/30`.

1. Copy `Config/Local.xcconfig.example` to `Config/Local.xcconfig` and set the
   Apple Developer team ID.
2. Open `Bajji.xcodeproj`, enable the App Group and Packet Tunnel capabilities
   for the selected team, then run `Bajji` on an iOS 26 physical iPhone.
3. Tap **Install VPN Profile**, enable the Phase 0 toggle, then tap **Start
   Bridge**. Enter the six-digit passkey shown on StopWatch when iOS asks.
4. Keep the VPN active while executing `docs/validation/phase0-template.md`.

Local checks:

```sh
swift test --package-path ios
xcodebuild build -project ios/Bajji.xcodeproj -target Bajji \
  -sdk iphoneos26.5 -arch arm64 ONLY_ACTIVE_ARCH=YES CODE_SIGNING_ALLOWED=NO
```

The full IPv4 forwarder stays out of this target until the physical Phase 0
gate passes.
