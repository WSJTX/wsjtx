import Foundation

// MARK: - Radio Mode

/// All digital modes supported by WSJT-X iOS.
public enum RadioMode: String, CaseIterable, Codable, Identifiable {
    case ft8   = "FT8"
    case ft4   = "FT4"
    case jt65  = "JT65"
    case jt9   = "JT9"
    case wspr  = "WSPR"
    case q65   = "Q65"

    public var id: String { rawValue }

    /// Length of one transmit/receive cycle in seconds.
    public var cycleLength: Double {
        switch self {
        case .ft8:  return 15
        case .ft4:  return 7.5
        case .jt65: return 60
        case .jt9:  return 60
        case .wspr: return 120
        case .q65:  return 60   // Q65-60 (standard for 1.2 GHz / 432 MHz EME)
        }
    }

    /// Tone separation in Hz between adjacent FSK symbols.
    public var toneSeparation: Double {
        switch self {
        case .ft8:  return 6.25
        case .ft4:  return 20.833
        case .jt65: return 2.692
        case .jt9:  return 1.736
        case .wspr: return 1.465
        case .q65:  return 12_000.0 / 6_912.0   // Q65-60A: ≈ 1.7361 Hz
        }
    }

    /// Number of tones in the modulation alphabet.
    public var toneCount: Int {
        switch self {
        case .ft8, .ft4: return 8
        case .jt65:      return 65
        case .jt9:       return 9
        case .wspr:      return 4
        case .q65:       return 64  // 64-tone FSK
        }
    }

    /// Total symbols per transmission frame (including sync).
    public var symbolCount: Int {
        switch self {
        case .ft8:  return 79
        case .ft4:  return 105
        case .jt65: return 126
        case .jt9:  return 85
        case .wspr: return 162
        case .q65:  return 85   // 7 Costas sync + 78 data
        }
    }

    /// Synchronisation-frame first-symbol offset in samples at 12000 Hz.
    public var syncOffset: Int {
        switch self {
        case .ft8:  return 0
        case .ft4:  return 0
        default:    return 0
        }
    }

    /// Whether the mode requires strict UTC synchronisation.
    public var requiresUTCSync: Bool { true }
}

// MARK: - Band

/// Amateur radio bands from 160 m through 24 GHz, with WSJT-X standard dial frequencies.
public enum Band: String, CaseIterable, Codable, Identifiable {
    // HF
    case m160    = "160m"
    case m80     = "80m"
    case m60     = "60m"
    case m40     = "40m"
    case m30     = "30m"
    case m20     = "20m"
    case m17     = "17m"
    case m15     = "15m"
    case m12     = "12m"
    case m10     = "10m"
    // VHF
    case m6      = "6m"
    case m4      = "4m"
    case m2      = "2m"
    // UHF
    case m70cm   = "70cm"
    // Microwave
    case m23cm   = "23cm"   // 1.2 GHz
    case m13cm   = "13cm"   // 2.3/2.4 GHz
    case m9cm    = "9cm"    // 3.4 GHz
    case m6cm    = "6cm"    // 5.7 GHz
    case m3cm    = "3cm"    // 10 GHz
    case m1_25cm = "1.25cm" // 24 GHz

    public var id: String { rawValue }

    /// Nominal centre frequency in Hz (for display / rig control).
    public var centreFrequency: Double {
        switch self {
        case .m160:    return 1_900_000
        case .m80:     return 3_750_000
        case .m60:     return 5_357_000
        case .m40:     return 7_150_000
        case .m30:     return 10_125_000
        case .m20:     return 14_175_000
        case .m17:     return 18_118_000
        case .m15:     return 21_225_000
        case .m12:     return 24_940_000
        case .m10:     return 28_850_000
        case .m6:      return 52_000_000
        case .m4:      return 70_200_000
        case .m2:      return 145_000_000
        case .m70cm:   return 433_500_000
        case .m23cm:   return 1_297_000_000
        case .m13cm:   return 2_320_000_000
        case .m9cm:    return 3_400_000_000
        case .m6cm:    return 5_760_000_000
        case .m3cm:    return 10_368_000_000
        case .m1_25cm: return 24_048_000_000
        }
    }

    /// Returns the WSJT-X standard dial frequency (Hz) for a given mode.
    public func defaultFrequency(for mode: RadioMode) -> Double {
        switch (self, mode) {
        // ── FT8 ─────────────────────────────────────────────────────────
        case (.m160,    .ft8): return 1_840_000
        case (.m80,     .ft8): return 3_573_000
        case (.m60,     .ft8): return 5_357_000
        case (.m40,     .ft8): return 7_074_000
        case (.m30,     .ft8): return 10_136_000
        case (.m20,     .ft8): return 14_074_000
        case (.m17,     .ft8): return 18_100_000
        case (.m15,     .ft8): return 21_074_000
        case (.m12,     .ft8): return 24_915_000
        case (.m10,     .ft8): return 28_074_000
        case (.m6,      .ft8): return 50_313_000
        case (.m4,      .ft8): return 70_100_000
        case (.m2,      .ft8): return 144_174_000
        case (.m70cm,   .ft8): return 432_174_000
        case (.m23cm,   .ft8): return 1_296_174_000
        case (.m13cm,   .ft8): return 2_320_143_000
        case (.m9cm,    .ft8): return 3_400_100_000
        case (.m6cm,    .ft8): return 5_760_100_000
        case (.m3cm,    .ft8): return 10_368_100_000
        case (.m1_25cm, .ft8): return 24_048_100_000

        // ── FT4 ─────────────────────────────────────────────────────────
        case (.m80,  .ft4): return 3_575_000
        case (.m40,  .ft4): return 7_047_500
        case (.m30,  .ft4): return 10_140_000
        case (.m20,  .ft4): return 14_080_000
        case (.m17,  .ft4): return 18_104_000
        case (.m15,  .ft4): return 21_140_000
        case (.m12,  .ft4): return 24_919_000
        case (.m10,  .ft4): return 28_180_000
        case (.m6,   .ft4): return 50_318_000
        case (.m2,   .ft4): return 144_170_000

        // ── WSPR ────────────────────────────────────────────────────────
        case (.m160,  .wspr): return 1_836_600
        case (.m80,   .wspr): return 3_568_600
        case (.m40,   .wspr): return 7_038_600
        case (.m30,   .wspr): return 10_138_700
        case (.m20,   .wspr): return 14_095_600
        case (.m17,   .wspr): return 18_104_600
        case (.m15,   .wspr): return 21_094_600
        case (.m12,   .wspr): return 24_924_600
        case (.m10,   .wspr): return 28_124_600
        case (.m6,    .wspr): return 50_293_000
        case (.m2,    .wspr): return 144_489_000
        case (.m70cm, .wspr): return 432_300_000

        // ── Q65 (EME + weak signal VHF/UHF/µwave) ───────────────────────
        case (.m6,      .q65): return 50_310_000
        case (.m2,      .q65): return 144_174_000
        case (.m70cm,   .q65): return 432_174_000
        case (.m23cm,   .q65): return 1_296_174_000
        case (.m13cm,   .q65): return 2_320_143_000
        case (.m9cm,    .q65): return 3_400_100_000
        case (.m6cm,    .q65): return 5_760_100_000
        case (.m3cm,    .q65): return 10_368_100_000
        case (.m1_25cm, .q65): return 24_048_100_000

        // ── JT65 ────────────────────────────────────────────────────────
        case (.m2,    .jt65): return 144_120_000
        case (.m70cm, .jt65): return 432_100_000
        case (.m23cm, .jt65): return 1_296_100_000

        default:
            // For unspecified combos fall back to the FT8 frequency
            return defaultFrequency(for: .ft8)
        }
    }

    /// Human-readable dial frequency string.
    public func frequencyString(for mode: RadioMode) -> String {
        let hz = defaultFrequency(for: mode)
        if hz >= 1_000_000_000 {
            return String(format: "%.3f GHz", hz / 1_000_000_000)
        } else {
            return String(format: "%.3f MHz", hz / 1_000_000)
        }
    }
}
