# Bajji iOS bridge

The `BajjiBridge` scheme contains the SwiftUI app, Packet Tunnel extension and
tests. The extension alone owns CoreBluetooth and routes only `10.77.0.0/30`.

1. Copy `Config/Local.xcconfig.example` to `Config/Local.xcconfig` and set the
   Apple Developer team ID.
2. Open `Bajji.xcodeproj`, enable the App Group and Packet Tunnel capabilities
   for the selected team, then run `Bajji` on an iOS 26 physical iPhone.
3. Build the pinned HEV forwarders: `python3 ios/tools/fetch_forwarder_deps.py`.
4. Tap **Install VPN Profile**, then **Start Bridge**. Enter the six-digit
   passkey shown on StopWatch when iOS asks.

Local checks:

```sh
swift test --package-path ios
xcodebuild build -project ios/Bajji.xcodeproj -target Bajji \
  -sdk iphoneos26.5 -arch arm64 ONLY_ACTIVE_ARCH=YES CODE_SIGNING_ALLOWED=NO
```

`ios/ThirdParty` is generated and ignored. The bootstrap checks out exact
commits, builds Apple XCFrameworks, and isolates duplicate static-library
symbols before Xcode links the Packet Tunnel.
