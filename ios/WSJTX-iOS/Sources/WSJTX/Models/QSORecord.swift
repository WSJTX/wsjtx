import Foundation

// MARK: - QSO Record

/// A logged contact, persisted as an ADIF record.
public struct QSORecord: Identifiable, Codable {
    public let id: UUID
    public let myCall: String
    public let dxCall: String
    public let frequency: Double    // Hz
    public let mode: RadioMode
    public let grid: String
    public let rst_sent: String
    public let rst_recv: String
    public let date: Date
    public var notes: String

    public init(
        myCall: String,
        dxCall: String,
        frequency: Double,
        mode: RadioMode,
        grid: String = "",
        rst_sent: String = "+00",
        rst_recv: String = "+00",
        notes: String = ""
    ) {
        self.id        = UUID()
        self.myCall    = myCall
        self.dxCall    = dxCall
        self.frequency = frequency
        self.mode      = mode
        self.grid      = grid
        self.rst_sent  = rst_sent
        self.rst_recv  = rst_recv
        self.date      = Date()
        self.notes     = notes
    }

    /// Frequency in MHz, formatted for display.
    public var frequencyMHz: String {
        String(format: "%.4f", frequency / 1_000_000)
    }

    /// UTC date string "YYYY-MM-DD".
    public var dateString: String {
        let fmt = DateFormatter()
        fmt.dateFormat = "yyyy-MM-dd"
        fmt.timeZone = TimeZone(identifier: "UTC")
        return fmt.string(from: date)
    }

    /// UTC time string "HHmm".
    public var timeString: String {
        let fmt = DateFormatter()
        fmt.dateFormat = "HHmm"
        fmt.timeZone = TimeZone(identifier: "UTC")
        return fmt.string(from: date)
    }
}

// MARK: - ADIF Exporter

/// Writes QSO records to an ADIF log file (append mode).
public enum ADIFExporter {

    private static let header = """
    WSJT-X iOS Log
    <ADIF_VER:5>3.1.4
    <PROGRAMID:7>WSJTX-i
    <EOH>

    """

    public static func append(record: QSORecord, to path: String) {
        let url = URL(fileURLWithPath: path)
        let fm  = FileManager.default

        if !fm.fileExists(atPath: path) {
            try? header.write(to: url, atomically: true, encoding: .utf8)
        }

        guard let fh = try? FileHandle(forWritingTo: url) else { return }
        fh.seekToEndOfFile()
        let adif = adifString(for: record) + "\n"
        fh.write(Data(adif.utf8))
        try? fh.close()
    }

    public static func export(records: [QSORecord]) -> String {
        var out = header
        for r in records { out += adifString(for: r) + "\n" }
        return out
    }

    private static func field(_ tag: String, _ value: String) -> String {
        "<\(tag):\(value.count)>\(value)"
    }

    private static func adifString(for r: QSORecord) -> String {
        var parts: [String] = []
        parts.append(field("CALL",      r.dxCall))
        parts.append(field("STATION_CALLSIGN", r.myCall))
        parts.append(field("QSO_DATE",  r.dateString.replacingOccurrences(of: "-", with: "")))
        parts.append(field("TIME_ON",   r.timeString))
        parts.append(field("BAND",      bandTag(hz: r.frequency)))
        parts.append(field("FREQ",      r.frequencyMHz))
        parts.append(field("MODE",      r.mode.rawValue))
        parts.append(field("GRIDSQUARE", r.grid))
        parts.append(field("RST_SENT",  r.rst_sent))
        parts.append(field("RST_RCVD",  r.rst_recv))
        if !r.notes.isEmpty { parts.append(field("COMMENT", r.notes)) }
        parts.append("<EOR>")
        return parts.joined(separator: " ")
    }

    private static func bandTag(hz: Double) -> String {
        switch hz {
        case  1_800_000 ..< 2_000_000:  return "160m"
        case  3_500_000 ..< 4_000_000:  return "80m"
        case  5_300_000 ..< 5_410_000:  return "60m"
        case  7_000_000 ..< 7_300_000:  return "40m"
        case 10_100_000 ..< 10_150_000: return "30m"
        case 14_000_000 ..< 14_350_000: return "20m"
        case 18_068_000 ..< 18_168_000: return "17m"
        case 21_000_000 ..< 21_450_000: return "15m"
        case 24_890_000 ..< 24_990_000: return "12m"
        case 28_000_000 ..< 29_700_000: return "10m"
        case 50_000_000 ..< 54_000_000: return "6m"
        default:                         return "?"
        }
    }
}
