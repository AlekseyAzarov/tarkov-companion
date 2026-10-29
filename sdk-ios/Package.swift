// swift-tools-version: 5.10
import PackageDescription

// Before building: scripts/build-ios-core.sh (the core) and
// scripts/generate-swift-contract.mjs (constants from contracts/).
let package = Package(
    name: "TarkovCompanion",
    platforms: [.iOS(.v15), .macOS(.v12)],
    products: [
        .library(name: "TarkovCompanion", targets: ["TarkovCompanion"]),
    ],
    targets: [
        .binaryTarget(name: "TarkovCompanionCore", path: "Frameworks/TarkovCompanionCore.xcframework"),
        .target(
            name: "TarkovCompanion",
            dependencies: ["TarkovCompanionCore"],
            linkerSettings: [.linkedLibrary("c++")]
        ),
        .testTarget(name: "TarkovCompanionTests", dependencies: ["TarkovCompanion", "TarkovCompanionCore"]),
    ]
)
