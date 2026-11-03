import XCTest
@testable import WSJTX

final class GridLocatorTests: XCTestCase {

    func testWashingtonDCGrid() {
        let grid = GridLocator.grid(lat: 38.897, lon: -77.036)
        XCTAssertEqual(grid, "FM18", "Washington DC should be FM18")
    }

    func testNewYorkGrid() {
        let grid = GridLocator.grid(lat: 40.712, lon: -74.006)
        XCTAssertEqual(String(grid.prefix(4)), "FN20")
    }

    func testLondonGrid() {
        let grid = GridLocator.grid(lat: 51.507, lon: -0.128)
        XCTAssertEqual(String(grid.prefix(4)), "IO91")
    }

    func testRoundTrip() {
        let original = "FN31"
        guard let coord = GridLocator.coordinate(from: original) else {
            XCTFail("coordinate() returned nil"); return
        }
        let recovered = GridLocator.grid(lat: coord.latitude, lon: coord.longitude)
        XCTAssertEqual(String(recovered.prefix(4)), original)
    }

    func testValidGrid() {
        XCTAssertTrue(GridLocator.isValid("FN31"))
        XCTAssertTrue(GridLocator.isValid("IO91AA"))
        XCTAssertFalse(GridLocator.isValid("ZZ99"))  // fields out of range
        XCTAssertFalse(GridLocator.isValid("FN"))    // too short
        XCTAssertFalse(GridLocator.isValid("FN31X")) // odd length
    }

    func testDistanceCalculation() {
        // Washington DC (FM18) to New York (FN20): ~340 km
        guard let dist = GridLocator.distance(from: "FM18", to: "FN20") else {
            XCTFail("distance() returned nil"); return
        }
        XCTAssertLessThan(abs(dist - 340), 80, "Distance should be ~340 km ± 80")
    }

    func testBearingNorth() {
        // Two grids at same longitude, second further north → bearing ≈ 0°
        guard let bearing = GridLocator.bearing(from: "FN20", to: "FN30") else {
            XCTFail(); return
        }
        XCTAssertLessThan(abs(bearing), 30, "Bearing should be roughly northward")
    }
}

// MARK: -

final class RadioModeTests: XCTestCase {

    func testFT8CycleLength() {
        XCTAssertEqual(RadioMode.ft8.cycleLength, 15)
    }

    func testFT4CycleLength() {
        XCTAssertEqual(RadioMode.ft4.cycleLength, 7.5)
    }

    func testFT8ToneSeparation() {
        XCTAssertEqual(RadioMode.ft8.toneSeparation, 6.25)
    }

    func testDefaultFrequency20mFT8() {
        XCTAssertEqual(Band.m20.defaultFrequency(for: .ft8), 14_074_000)
    }

    func testDefaultFrequency40mFT8() {
        XCTAssertEqual(Band.m40.defaultFrequency(for: .ft8), 7_074_000)
    }

    func testAllBandsCovered() {
        for band in Band.allCases {
            let f = band.defaultFrequency(for: .ft8)
            XCTAssertGreaterThan(f, 0, "\(band.rawValue) has no default frequency")
        }
    }
}

// MARK: -

final class FT8ProtocolTests: XCTestCase {

    func testFT8SamplesPerSymbol() {
        // 12000 Hz / 6.25 Hz = 1920 samples
        XCTAssertEqual(FT8Protocol.ft8SamplesPerSymbol, 1920)
    }

    func testCostasLength() {
        XCTAssertEqual(FT8Protocol.ft8CostasArray.count, 7)
    }

    func testDataIndicesCount() {
        // 79 total − 3 × 7 Costas = 58 data symbols
        XCTAssertEqual(FT8Protocol.ft8DataIndices.count, 58)
    }

    func testDataIndicesNoOverlapWithCostas() {
        let costas = Set(FT8Protocol.ft8Costas1)
            .union(Set(FT8Protocol.ft8Costas2))
            .union(Set(FT8Protocol.ft8Costas3))
        for idx in FT8Protocol.ft8DataIndices {
            XCTAssertFalse(costas.contains(idx), "Data index \(idx) overlaps Costas")
        }
    }

    func testGreyCodeRoundTrip() {
        for i in 0 ..< 8 {
            let encoded = FT8Protocol.greyEncode[i]
            let decoded = FT8Protocol.greyDecode[encoded]
            XCTAssertEqual(decoded, i, "Grey code round-trip failed for \(i)")
        }
    }
}

// MARK: -

final class MessageParserTests: XCTestCase {

    func testParseCQ() {
        let msg = MessageParser.parse(
            raw: "CQ W1AW FN31",
            snr: 5, dt: 0.2, frequency: 1250,
            mode: .ft8, myCall: "VK2ABC"
        )
        XCTAssertTrue(msg.isCQ)
        XCTAssertEqual(msg.callsignA, "W1AW")
        XCTAssertEqual(msg.grid, "FN31")
        XCTAssertFalse(msg.isDirectedToMe)
    }

    func testParseDirectedMessage() {
        let msg = MessageParser.parse(
            raw: "VK2ABC W1AW +05",
            snr: 12, dt: 0.1, frequency: 1500,
            mode: .ft8, myCall: "VK2ABC"
        )
        XCTAssertTrue(msg.isDirectedToMe)
        XCTAssertEqual(msg.callsignA, "W1AW")
        XCTAssertEqual(msg.callsignB, "VK2ABC")
        XCTAssertEqual(msg.report, 5)
    }

    func testParseRRR() {
        let msg = MessageParser.parse(
            raw: "VK2ABC W1AW RRR",
            snr: 8, dt: 0.0, frequency: 1600,
            mode: .ft8, myCall: "VK2ABC"
        )
        XCTAssertTrue(msg.isDirectedToMe)
        XCTAssertNil(msg.report)
    }
}

// MARK: -

final class ADIFExporterTests: XCTestCase {

    func testExportContainsCallsign() {
        let record = QSORecord(
            myCall: "W1AW",
            dxCall: "VK2ABC",
            frequency: 14_074_000,
            mode: .ft8,
            grid: "QF56"
        )
        let adif = ADIFExporter.export(records: [record])
        XCTAssertTrue(adif.contains("VK2ABC"))
        XCTAssertTrue(adif.contains("FT8"))
        XCTAssertTrue(adif.contains("20m"))
        XCTAssertTrue(adif.contains("<EOR>"))
    }

    func testExportHeader() {
        let adif = ADIFExporter.export(records: [])
        XCTAssertTrue(adif.contains("<EOH>"))
        XCTAssertTrue(adif.contains("WSJTX-i"))
    }
}
