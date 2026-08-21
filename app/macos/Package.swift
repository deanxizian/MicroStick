// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "MicroStickUsageSync",
    platforms: [
        .macOS(.v14),
    ],
    products: [
        .executable(name: "MicroStickUsageSync", targets: ["MicroStickUsageSync"]),
        .library(name: "MicroStickUsageCore", targets: ["MicroStickUsageCore"]),
        .library(name: "MicroStickUsageBluetooth", targets: ["MicroStickUsageBluetooth"]),
    ],
    targets: [
        .target(
            name: "MicroStickUsageCore",
            path: "Sources/MicroStickUsageCore"
        ),
        .target(
            name: "MicroStickUsageBluetooth",
            dependencies: ["MicroStickUsageCore"],
            path: "Sources/MicroStickUsageBluetooth",
            linkerSettings: [
                .linkedFramework("CoreBluetooth"),
            ]
        ),
        .executableTarget(
            name: "MicroStickUsageSync",
            dependencies: ["MicroStickUsageCore", "MicroStickUsageBluetooth"],
            path: "Sources/MicroStickUsageSync",
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("CoreServices"),
                .linkedFramework("ServiceManagement"),
            ]
        ),
        .testTarget(
            name: "MicroStickUsageCoreTests",
            dependencies: ["MicroStickUsageCore"],
            path: "Tests/MicroStickUsageCoreTests",
            resources: [.copy("Fixtures")]
        ),
        .testTarget(
            name: "MicroStickUsageBluetoothTests",
            dependencies: ["MicroStickUsageCore", "MicroStickUsageBluetooth"],
            path: "Tests/MicroStickUsageBluetoothTests"
        ),
    ]
)
