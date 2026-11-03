// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "WSJTX-iOS",
    platforms: [.iOS(.v17)],
    products: [
        .library(name: "WSJTX", targets: ["WSJTX"]),
    ],
    dependencies: [],
    targets: [
        .target(
            name: "WSJTX",
            path: "Sources/WSJTX",
            resources: [
                .process("Resources"),
            ]
        ),
        .testTarget(
            name: "WSJTXTests",
            dependencies: ["WSJTX"],
            path: "Tests/WSJTXTests"
        ),
    ]
)
