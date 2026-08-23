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
        .library(name: "MicroStickUsageCodex", targets: ["MicroStickUsageCodex"]),
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
        .target(
            name: "MicroStickUsageCodex",
            dependencies: ["MicroStickUsageCore"],
            path: "Sources/MicroStickUsageCodex"
        ),
        .executableTarget(
            name: "MicroStickUsageSync",
            dependencies: [
                "MicroStickUsageCore",
                "MicroStickUsageBluetooth",
                "MicroStickUsageCodex",
            ],
            path: "Sources/MicroStickUsageSync",
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("ServiceManagement"),
            ]
        ),
        .testTarget(
            name: "MicroStickUsageCoreTests",
            dependencies: ["MicroStickUsageCore"],
            path: "Tests/MicroStickUsageCoreTests"
        ),
        .testTarget(
            name: "MicroStickUsageBluetoothTests",
            dependencies: ["MicroStickUsageCore", "MicroStickUsageBluetooth"],
            path: "Tests/MicroStickUsageBluetoothTests"
        ),
        .testTarget(
            name: "MicroStickUsageCodexTests",
            dependencies: ["MicroStickUsageCore", "MicroStickUsageCodex"],
            path: "Tests/MicroStickUsageCodexTests"
        ),
    ]
)
