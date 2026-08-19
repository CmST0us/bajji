# Bajji iOS bridge

The `BajjiBridge` scheme contains the SwiftUI app, Packet Tunnel extension and
tests. The extension alone owns CoreBluetooth and routes only `10.77.0.0/30`.

1. Build the pinned HEV forwarders: `python3 ios/tools/fetch_forwarder_deps.py`.
2. Copy `Config/Local.xcconfig.example` to `Config/Local.xcconfig` and set the
   Apple Developer team ID.
3. Open `Bajji.xcodeproj`, select the `BajjiBridge` scheme, and run it on an
   iOS 26 physical iPhone using the App Group and Packet Tunnel entitlements.
4. Tap **Install VPN Profile**, then **Start Bridge**. Enter the six-digit
   passkey shown on StopWatch when iOS asks.

Once the bridge is ready, Bajji owns `10.77.0.2/30`; the extension translates
its IPv4 TCP, UDP, and DNS traffic through the iPhone. Phase Zero echo is an
explicit debug-only mode and must remain off for normal forwarding.

For lifecycle testing, detach the Xcode debugger before swiping away the host
App. Xcode stopping a debug session also terminates the extension. Use
[`complete-ip-bridge-template.md`](../docs/validation/complete-ip-bridge-template.md)
to record the physical-device checks; firmware flashing is intentionally left
to the user.

Local checks:

```sh
swift test --package-path ios
xcodebuild -project ios/Bajji.xcodeproj -scheme BajjiBridge \
  -configuration Debug -sdk iphoneos -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO build
```

`ios/ThirdParty` is generated and ignored. The bootstrap checks out exact
commits, builds Apple XCFrameworks, and isolates duplicate static-library
symbols before Xcode links the Packet Tunnel.
