// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "mac-window-tools",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "mac-window-tools", targets: ["mac-window-tools"]),
        .library(name: "WindowTools", targets: ["WindowTools"]),
    ],
    targets: [
        // The rules: which windows match, which one wins, how a window is printed, how the command line reads.
        // No AppKit, no CoreGraphics, so the tests do not need a display.
        .target(name: "WindowTools"),
        // The command: asks macOS for windows and takes the picture.
        .executableTarget(name: "mac-window-tools", dependencies: ["WindowTools"]),
        .testTarget(name: "WindowToolsTests", dependencies: ["WindowTools"]),
    ]
)
