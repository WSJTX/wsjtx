import Foundation
import Accelerate

// MARK: - FT8 Encoder

/// Encodes standard FT8 / FT4 messages into 8-FSK symbol arrays.
///
/// Usage:
/// ```swift
/// let enc = FT8Encoder(settings: settings)
/// let symbols = enc.encode(message: "CQ W1AW FN31", mode: .ft8)
/// audioEngine.transmit(symbols: symbols, mode: .ft8)
/// ```
public final class FT8Encoder {

    private let settings: AppSettings

    public init(settings: AppSettings) {
        self.settings = settings
    }

    // MARK: - Public API

    /// Encodes a text message into FSK tone indices ready for audio synthesis.
    /// Returns an empty array if the message cannot be encoded.
    public func encode(message: String, mode: RadioMode) -> [Int] {
        guard let bits = pack77(message: message) else { return [] }
        let coded     = ldpcEncode(bits: bits)
        let interleaved = interleave(coded)
        let greySymbols = toGreySymbols(interleaved)
        return insertCostas(greySymbols, mode: mode)
    }

    // MARK: - Standard message candidates

    /// Generates the appropriate next-in-sequence TX message given QSO state.
    public func nextMessage(
        myCall: String,
        dxCall: String,
        myGrid: String,
        report: Int?,
        qsoStage: QSOStage
    ) -> String {
        let r = report.map { $0 >= 0 ? "+\($0)" : "\($0)" } ?? "+00"
        let grid4 = String(myGrid.prefix(4))
        switch qsoStage {
        case .calling:
            // Omit grid if not set; CQ with grid is preferred but not required
            return grid4.isEmpty ? "CQ \(myCall)" : "CQ \(myCall) \(grid4)"
        case .answered:  return "\(dxCall) \(myCall) \(r)"
        case .report:    return "\(dxCall) \(myCall) R\(r)"
        case .rrr:       return "\(dxCall) \(myCall) RRR"
        case .r73:       return "\(dxCall) \(myCall) 73"
        }
    }

    // MARK: - 77-bit message packing (internal helpers used by Q65Encoder)

    /// Public entry point for Q65Encoder to reuse FT8's message packing.
    public func pack77Public(message: String) -> [Int]? { pack77(message: message) }

    /// Public entry point for Q65Encoder to reuse FT8's LDPC encoder.
    public func ldpcEncodePublic(bits: [Int]) -> [Int] { ldpcEncode(bits: bits) }

    private func pack77(message: String) -> [Int]? {
        let upper = message.uppercased().trimmingCharacters(in: .whitespaces)
        let parts = upper.components(separatedBy: .whitespaces)

        if parts.first == "CQ" {
            return packCQ(parts: parts)
        } else if parts.count >= 2 {
            return packStandard(parts: parts)
        }
        return packFreeText(text: upper)
    }

    // CQ <mycall> [<grid4>] — grid is optional
    private func packCQ(parts: [String]) -> [Int]? {
        guard parts.count >= 2 else { return nil }
        let callA = parts[1]
        guard let c28a = packCallsign(callA) else { return nil }
        let c28b = (1 << 28) - 2  // special "CQ" code

        // g15 = 0 means no grid; use grid if provided and valid
        let g15: Int
        if parts.count >= 3, let packed = packGrid(parts[2]) {
            g15 = packed
        } else {
            g15 = 0
        }

        var bits = [Int]()
        bits += int2bits(c28a, count: 28)
        bits += int2bits(c28b, count: 28)
        bits += int2bits(g15,  count: 15)
        bits += [0, 0, 0]   // i3 = 0, n3 = 0
        bits += computeCRC14(bits: Array(bits.prefix(77 - 14)))
        return Array(bits.prefix(77))
    }

    // <dxcall> <mycall> <grid4|report|RRR|RR73|73>
    private func packStandard(parts: [String]) -> [Int]? {
        guard parts.count >= 3,
              let c28a = packCallsign(parts[1]),
              let c28b = packCallsign(parts[0]) else { return nil }

        let payload = parts[2]
        let g15: Int
        if let grid = packGrid(payload) {
            g15 = grid
        } else if let snr = Int(payload) {
            g15 = max(0, min(180, snr + 90))
        } else if payload == "RRR" || payload == "RR73" {
            g15 = 181
        } else if payload == "73" {
            g15 = 182
        } else {
            g15 = 0
        }

        var bits = [Int]()
        bits += int2bits(c28a, count: 28)
        bits += int2bits(c28b, count: 28)
        bits += int2bits(g15,  count: 15)
        bits += [0, 0, 0]
        let info = Array(bits.prefix(77 - 14))
        bits = info + computeCRC14(bits: info)
        return Array(bits.prefix(77))
    }

    // Free text — up to 13 characters
    private func packFreeText(text: String) -> [Int]? {
        let chars = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?_"
        let padded = String((text + String(repeating: " ", count: 13)).prefix(13))
        var n: UInt64 = 0
        for ch in padded {
            guard let idx = chars.firstIndex(of: ch) else { return nil }
            n = n * UInt64(chars.count) + UInt64(chars.distance(from: chars.startIndex, to: idx))
        }
        var bits = (0 ..< 77).reversed().map { Int((n >> $0) & 1) }
        bits[74] = 0; bits[75] = 1; bits[76] = 1  // i3 = 3 (free text)
        return bits
    }

    // MARK: - Callsign packing

    private let callChars = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/"

    private func packCallsign(_ call: String) -> Int? {
        var s = call.uppercased()
        // Pad / normalise to 6 characters
        while s.count < 6 { s = " " + s }
        let chars = Array(s.prefix(6))
        var n = 0
        for c in chars {
            guard let idx = callChars.firstIndex(of: c) else { return nil }
            n = n * 38 + callChars.distance(from: callChars.startIndex, to: idx)
        }
        return n
    }

    // MARK: - Grid packing

    private func packGrid(_ grid: String) -> Int? {
        let g = grid.uppercased()
        guard g.count >= 4 else { return nil }
        let chars = Array(g)
        guard chars[0].isLetter && chars[1].isLetter &&
              chars[2].isNumber && chars[3].isNumber else { return nil }
        let lon = Int(chars[0].asciiValue! - UInt8(UnicodeScalar("A").value))
        let lat = Int(chars[1].asciiValue! - UInt8(UnicodeScalar("A").value))
        let c   = Int(chars[2].asciiValue! - UInt8(UnicodeScalar("0").value))
        let d   = Int(chars[3].asciiValue! - UInt8(UnicodeScalar("0").value))
        let g15 = 181 + (lon * 18 + c) + (lat * 18 + d) * 180
        return g15 < (1 << 15) ? g15 : nil
    }

    // MARK: - LDPC (174, 87) encoding

    /// Systematic LDPC encoder: appends 87 parity bits to the 87 information bits.
    /// The FT8 message is 77 bits; bits 77-86 are implicit zeros (systematic padding).
    private func ldpcEncode(bits: [Int]) -> [Int] {
        let k = 87
        let n = FT8Protocol.codedBits  // 174
        // Pad the 77-bit message to k=87 bits with zeros (systematic LDPC input).
        var coded = bits + [Int](repeating: 0, count: n - bits.count)

        // Generate parity bits using the approximate circulant G_P matrix.
        // Use coded[j] (not bits[j]) so indices 77-86 safely read the zero padding.
        for i in k ..< n {
            var parity = 0
            for j in 0 ..< k {
                parity ^= coded[j] & generatorBit(row: i - k, col: j)
            }
            coded[i] = parity
        }
        return coded
    }

    /// Returns bit (row, col) of the LDPC parity sub-matrix.
    /// Replace with table lookup for the full (87×87) G_P matrix.
    private func generatorBit(row: Int, col: Int) -> Int {
        // Circulant structure: row i involves columns in a specific pattern.
        // This is a simplified approximation; the exact matrix is in the Fortran source.
        return (row + col) % 3 == 0 ? 1 : 0
    }

    // MARK: - Interleaving

    private func interleave(_ bits: [Int]) -> [Int] {
        let n = bits.count
        var out = [Int](repeating: 0, count: n)
        for i in 0 ..< n {
            let j = Int(reverseBits(UInt8(i), bits: 7))
            if j < n { out[j] = bits[i] }
        }
        return out
    }

    private func reverseBits(_ b: UInt8, bits: Int) -> UInt8 {
        var v = b, r = UInt8(0)
        for _ in 0 ..< bits { r = (r << 1) | (v & 1); v >>= 1 }
        return r
    }

    // MARK: - Grey-code to 8-FSK symbols

    private func toGreySymbols(_ bits: [Int]) -> [Int] {
        var symbols = [Int]()
        stride(from: 0, to: bits.count - 2, by: 3).forEach { i in
            let b = bits[i] * 4 + bits[i + 1] * 2 + bits[i + 2]
            symbols.append(FT8Protocol.greyEncode[b])
        }
        return symbols
    }

    // MARK: - Costas array insertion

    private func insertCostas(_ data: [Int], mode: RadioMode) -> [Int] {
        let costas = FT8Protocol.ft8CostasArray
        var frame  = [Int](repeating: 0, count: FT8Protocol.ft8TotalSymbols)

        // Place Costas arrays
        for (j, t) in costas.enumerated() {
            frame[j]      = t   // positions 0–6
            frame[36 + j] = t   // positions 36–42
            frame[72 + j] = t   // positions 72–78
        }

        // Place data symbols
        let dataIndices = FT8Protocol.ft8DataIndices
        for (k, idx) in dataIndices.enumerated() {
            if k < data.count { frame[idx] = data[k] }
        }
        return frame
    }

    // MARK: - CRC-14

    private func computeCRC14(bits: [Int]) -> [Int] {
        var reg: UInt16 = 0
        for bit in bits {
            let msb = (reg >> 13) & 1
            reg = (reg << 1) | UInt16(bit)
            if msb == 1 { reg ^= UInt16(FT8Protocol.crc14Poly) }
        }
        for _ in 0 ..< 14 {
            let msb = (reg >> 13) & 1
            reg <<= 1
            if msb == 1 { reg ^= UInt16(FT8Protocol.crc14Poly) }
        }
        return (0 ..< 14).reversed().map { Int((reg >> $0) & 1) }
    }

    // MARK: - Bit helpers

    private func int2bits(_ val: Int, count: Int) -> [Int] {
        (0 ..< count).reversed().map { (val >> $0) & 1 }
    }
}

// MARK: - QSO Stage

public enum QSOStage: CaseIterable {
    case calling, answered, report, rrr, r73
}
