import Foundation

// MARK: - Decoded Message

/// A single decoded WSJT-X message with metadata.
public struct DecodedMessage: Identifiable, Equatable {
    public let id = UUID()
    public let timestamp: Date
    public let utcTime: String          // "HHMM" format displayed in UI
    public let snr: Int                 // dB, relative to noise floor
    public let dt: Double               // time offset from start of period (s)
    public let frequency: Int           // audio offset in Hz
    public let mode: RadioMode
    public let text: String             // full decoded text
    public let callsignA: String?       // first callsign in message
    public let callsignB: String?       // second callsign / "CQ" / "DE"
    public let grid: String?            // Maidenhead grid, if present
    public let report: Int?             // signal report, if present
    public let isDirectedToMe: Bool     // true when callsignB matches myCall
    public let isCQ: Bool               // true when this is a CQ call
    public let isNew: Bool              // first time this call heard this session

    public static func == (lhs: DecodedMessage, rhs: DecodedMessage) -> Bool {
        lhs.id == rhs.id
    }
}

// MARK: - Message Parser

/// Converts a raw WSJT-X text string into a `DecodedMessage`.
public struct MessageParser {

    public static func parse(
        raw: String,
        snr: Int,
        dt: Double,
        frequency: Int,
        mode: RadioMode,
        myCall: String
    ) -> DecodedMessage {
        let text = raw.trimmingCharacters(in: .whitespaces)
        let parts = text.components(separatedBy: .whitespaces)

        var callA: String? = nil
        var callB: String? = nil
        var grid: String? = nil
        var report: Int? = nil
        var isCQ = false

        if parts.first?.uppercased() == "CQ" {
            isCQ = true
            if parts.count >= 3 {
                callA = parts[1]
                let last = parts.last ?? ""
                if isGrid(last) { grid = last } else { callB = last }
            } else if parts.count == 2 {
                callA = parts[1]
            }
        } else if parts.count >= 2 {
            callB = parts[0]   // destination
            callA = parts[1]   // source
            if parts.count >= 3 {
                let payload = parts[2]
                if isGrid(payload) {
                    grid = payload
                } else if let r = Int(payload) {
                    report = r
                }
            }
        }

        let myCallUpper = myCall.uppercased()
        let isDirected = callB?.uppercased() == myCallUpper

        let fmt = DateFormatter()
        fmt.dateFormat = "HHmm"
        fmt.timeZone = TimeZone(identifier: "UTC")
        let utc = fmt.string(from: Date())

        return DecodedMessage(
            timestamp: Date(),
            utcTime: utc,
            snr: snr,
            dt: dt,
            frequency: frequency,
            mode: mode,
            text: text,
            callsignA: callA,
            callsignB: callB,
            grid: grid,
            report: report,
            isDirectedToMe: isDirected,
            isCQ: isCQ,
            isNew: false        // caller updates from session set
        )
    }

    private static func isGrid(_ s: String) -> Bool {
        let upper = s.uppercased()
        guard upper.count >= 4 && upper.count <= 6 else { return false }
        let chars = Array(upper)
        return chars[0].isLetter && chars[1].isLetter &&
               chars[2].isNumber && chars[3].isNumber
    }
}
