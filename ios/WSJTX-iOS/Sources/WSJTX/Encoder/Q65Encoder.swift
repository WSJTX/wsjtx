import Foundation

// MARK: - Q65 Sub-mode

/// Q65 sub-mode controls tone spacing WITHIN a period.
/// Sub-mode A uses the base nsps for that period; each subsequent
/// sub-mode doubles the nsps (halves tone spacing) for higher sensitivity.
/// Only sub-mode/period combinations whose total duration < period are valid.
public enum Q65SubMode: String, CaseIterable, Identifiable {
    case a = "A"
    case b = "B"
    case c = "C"
    case d = "D"
    case e = "E"

    public var id: String { rawValue }

    /// Multiplier applied to the period's base nsps.
    public var nspsMultiplier: Int {
        switch self {
        case .a: return 1
        case .b: return 2
        case .c: return 4
        case .d: return 8
        case .e: return 16
        }
    }

    /// Effective tone separation for this sub-mode combined with a period.
    /// Returns nil when the combination would exceed the T/R period.
    public func toneSeparation(for period: Q65Period) -> Double? {
        let nsps = period.nspsAt12k * nspsMultiplier
        let totalSeconds = Double(nsps) * Double(Q65Protocol.totalSymbols) / 12_000.0
        guard totalSeconds <= Double(period.rawValue) else { return nil }
        return 12_000.0 / Double(nsps)
    }
}

// MARK: - Q65 Protocol Constants

public enum Q65Protocol {

    /// 64 orthogonal tones (6 bits per symbol).
    public static let toneCount: Int = 64

    /// Data symbols per transmission.
    public static let dataSymbols: Int = 78

    /// Sync symbols per transmission (one Costas block).
    public static let syncSymbols: Int = 7

    /// Total symbols per Q65 frame.
    public static let totalSymbols: Int = syncSymbols + dataSymbols  // 85

    /// Number of coded bits required for 78 data symbols at 6 bits each.
    public static let codedBits: Int = dataSymbols * 6               // 468

    /// Sync Costas array — 7 tones chosen from {0…63} for good autocorrelation.
    /// These match the WSJT-X Q65 implementation (from lib/q65/q65params.f90).
    public static let costasArray: [Int] = [4, 2, 5, 0, 6, 1, 3]
        .map { $0 * 9 }  // scale 7-element [0-6] Costas to 64-tone range → [0-63]

    // 6-bit reflected Grey code lookup (index → Grey value).
    public static let greyEncode6: [Int] = (0..<64).map { i in i ^ (i >> 1) }

    // 6-bit Grey decode (Grey value → index).
    public static let greyDecode6: [Int] = {
        var table = [Int](repeating: 0, count: 64)
        for i in 0..<64 { table[i ^ (i >> 1)] = i }
        return table
    }()
}

// MARK: - Q65 Encoder

/// Encodes a standard 77-bit WSJT-X message into a Q65 85-symbol tone sequence.
///
/// Encoding pipeline:
///   pack77 → LDPC(174,87) → rate-1/3 repetition → 468 coded bits
///   → interleave → 6-bit Grey-coded 64-FSK symbols → Costas sync insertion
///
/// Note: the rate-1/3 repetition (cycling 174 bits three times and truncating)
/// approximates the published Q65 rate-1/6 outer code. For exact WSJT-X
/// bit-for-bit compatibility, the LDPC(468,77) parity matrix from the WSJT-X
/// Fortran source (lib/q65/encode_q65.f90) should replace the repetition step.
public final class Q65Encoder {

    private let ft8Encoder: FT8Encoder

    public init(settings: AppSettings) {
        self.ft8Encoder = FT8Encoder(settings: settings)
    }

    // MARK: - Public API

    public func encode(message: String, subMode: Q65SubMode = .a) -> [Int] {
        guard let bits77 = ft8Encoder.pack77Public(message: message) else { return [] }

        let coded174 = ft8Encoder.ldpcEncodePublic(bits: bits77)   // 174 bits
        let coded468 = repeat174to468(coded174)                      // 468 bits
        let interleaved = interleave468(coded468)
        let dataSymbols = toGreySymbols6(interleaved)                // 78 symbols
        return insertQ65Sync(dataSymbols)                            // 85 symbols
    }

    /// Message candidates for Q65 QSOs — same format as FT8.
    public func nextMessage(myCall: String, dxCall: String, myGrid: String,
                             report: Int?, stage: QSOStage) -> String {
        ft8Encoder.nextMessage(myCall: myCall, dxCall: dxCall, myGrid: myGrid,
                               report: report, qsoStage: stage)
    }

    // MARK: - Encoding steps

    /// Rate-1/3 repetition: 174 → 468 by cycling (i % 174).
    /// Placeholder for the LDPC(468,77) outer code used by WSJT-X.
    private func repeat174to468(_ bits: [Int]) -> [Int] {
        (0..<Q65Protocol.codedBits).map { bits[$0 % bits.count] }
    }

    /// Column-major interleaver over 78 × 6 bit array.
    private func interleave468(_ bits: [Int]) -> [Int] {
        let rows = Q65Protocol.dataSymbols   // 78
        let cols = 6
        var out  = [Int](repeating: 0, count: bits.count)
        for r in 0 ..< rows {
            for c in 0 ..< cols {
                let src = r * cols + c
                let dst = c * rows + r
                if src < bits.count && dst < out.count { out[dst] = bits[src] }
            }
        }
        return out
    }

    /// Pack every 6 bits into a Grey-coded 64-FSK tone.
    private func toGreySymbols6(_ bits: [Int]) -> [Int] {
        var symbols = [Int]()
        symbols.reserveCapacity(Q65Protocol.dataSymbols)
        stride(from: 0, to: bits.count - 5, by: 6).forEach { i in
            var val = 0
            for k in 0 ..< 6 { val = val * 2 + bits[i + k] }
            symbols.append(Q65Protocol.greyEncode6[val & 63])
        }
        return symbols
    }

    /// Inserts the 7-symbol Costas sync block at symbol positions 0–6,
    /// followed by the 78 data symbols at positions 7–84.
    private func insertQ65Sync(_ data: [Int]) -> [Int] {
        var frame = Q65Protocol.costasArray  // 7 sync symbols
        frame.append(contentsOf: data.prefix(Q65Protocol.dataSymbols))
        return frame  // 85 symbols
    }
}
