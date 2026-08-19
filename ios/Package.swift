// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "BajjiBridge",
    platforms: [.macOS(.v13)],
    products: [.library(name: "BridgeCore", targets: ["BridgeCore"])],
    targets: [
        .target(name: "BridgeCore", path: "Shared"),
        .testTarget(name: "BridgeCoreTests", dependencies: ["BridgeCore"], path: "BajjiTests")
    ]
)
