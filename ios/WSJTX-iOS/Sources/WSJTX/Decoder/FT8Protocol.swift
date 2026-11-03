import Foundation

// MARK: - FT8 Protocol Constants

/// Complete FT8 (and FT4) protocol specification derived from the WSJT-X source.
public enum FT8Protocol {

    // MARK: Timing

    /// Nominal sample rate used throughout WSJT-X (Hz).
    public static let sampleRate: Double = 12_000

    /// FT8: symbol duration = 1 / 6.25 Hz ≈ 160 ms.
    public static let ft8SymbolDuration: Double = 1.0 / 6.25

    /// FT4: symbol duration = 1 / 20.833 Hz ≈ 48 ms.
    public static let ft4SymbolDuration: Double = 1.0 / 20.833

    /// FT8: samples per symbol at 12 kHz.
    public static let ft8SamplesPerSymbol: Int = Int(sampleRate * ft8SymbolDuration)   // 1920

    /// FT4: samples per symbol at 12 kHz.
    public static let ft4SamplesPerSymbol: Int = Int(sampleRate * ft4SymbolDuration)   // 576

    /// FT8: total symbols per transmission = 79.
    public static let ft8TotalSymbols: Int = 79

    /// FT4: total symbols per transmission = 105.
    public static let ft4TotalSymbols: Int = 105

    /// FT8: 4-symbol Costas synchronisation arrays (3× used).
    public static let ft8CostasArray: [Int] = [3, 1, 4, 0, 6, 5, 2]

    /// FT4: 4-symbol Costas synchronisation arrays.
    public static let ft4CostasArray: [Int] = [0, 3, 1, 2]

    // MARK: Message structure

    /// Number of information bits in a standard 77-bit FT8 message.
    public static let messageBits: Int = 77

    /// Total coded bits after LDPC(174, 87) encoding.
    public static let codedBits: Int = 174

    /// 3-bit symbol size.
    public static let bitsPerSymbol: Int = 3

    // MARK: Costas placement in the 79-symbol frame

    /// Symbol indices where the first Costas array is placed.
    public static let ft8Costas1: ClosedRange<Int> = 0 ... 6

    /// Symbol indices where the second Costas array is placed.
    public static let ft8Costas2: ClosedRange<Int> = 36 ... 42

    /// Symbol indices where the third Costas array is placed.
    public static let ft8Costas3: ClosedRange<Int> = 72 ... 78

    // MARK: LDPC (174, 87) parity-check matrix helpers

    /// Generator polynomial degree for the CRC-14 used in FT8.
    public static let crc14Poly: UInt32 = 0x2757

    // MARK: Grey code tables (3-bit)

    /// Maps symbol index to Grey-coded value (encoding).
    public static let greyEncode: [Int] = [0, 1, 3, 2, 6, 7, 5, 4]

    /// Maps Grey-coded value to symbol index (decoding).
    public static let greyDecode: [Int] = [0, 1, 3, 2, 7, 6, 4, 5]

    /// Symbol indices carrying data (complement of all Costas positions).
    public static var ft8DataIndices: [Int] {
        let costas = Set(ft8Costas1).union(Set(ft8Costas2)).union(Set(ft8Costas3))
        return (0 ..< ft8TotalSymbols).filter { !costas.contains($0) }
    }
}
