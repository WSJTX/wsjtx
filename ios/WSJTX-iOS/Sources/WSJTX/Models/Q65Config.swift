import Foundation
import Combine

// MARK: - TX Slot

/// Which half of the period pair this station transmits in.
/// Applies to ALL WSJT-X modes; mirrors the WSJT-X "1st / 2nd" toggle.
public enum TxSlot: String, CaseIterable, Identifiable, Codable {
    case first  = "1st"
    case second = "2nd"

    public var id: String { rawValue }

    /// Returns true when the current UTC time falls in this station's TX slot.
    public func isMyTurn(cycleSeconds: Int) -> Bool {
        let t    = Int(Date().timeIntervalSince1970)
        let slot = (t / cycleSeconds) % 2  // 0 = even, 1 = odd
        return self == .first ? slot == 0 : slot == 1
    }
}

// MARK: - Operating Config (all modes)

/// Mode-independent operating parameters shown for every mode:
/// audio RX/TX frequency and 1st/2nd TX slot.
public final class OperatingConfig: ObservableObject {

    /// Audio receive frequency (Hz) — green marker on waterfall.
    @Published public var rxFreq: Int = 1_000

    /// Audio transmit frequency (Hz) — red marker.
    @Published public var txFreq: Int = 1_000

    /// When true, TX frequency tracks RX frequency.
    @Published public var freqLocked: Bool = true

    /// Whether this station transmits in the first or second slot.
    @Published public var txSlot: TxSlot = .first

    public init() {}

    public func setRxFreq(_ hz: Int) {
        rxFreq = hz
        if freqLocked { txFreq = hz }
    }

    public func setTxFreq(_ hz: Int) {
        txFreq = hz
        if freqLocked { rxFreq = hz }
    }
}

// MARK: - Q65 Period

/// T/R sequence length in seconds.
public enum Q65Period: Int, CaseIterable, Identifiable, Codable {
    case p15  = 15
    case p30  = 30
    case p60  = 60
    case p120 = 120
    case p300 = 300

    public var id: Int { rawValue }
    public var label: String { "\(rawValue)s" }

    /// Standard WSJT-X samples-per-symbol at 12 kHz for sub-mode A.
    /// All 85 symbols fit within the T/R period with ~15% guard margin.
    public var nspsAt12k: Int {
        switch self {
        case .p15:  return 1_800   // 85 × 150 ms = 12.75 s
        case .p30:  return 3_600   // 85 × 300 ms = 25.5 s
        case .p60:  return 6_912   // 85 × 576 ms = 48.96 s
        case .p120: return 13_824  // 85 × 1152 ms = 97.9 s
        case .p300: return 27_648  // 85 × 2304 ms = 195.8 s
        }
    }

    /// Tone separation at 12 kHz for sub-mode A (base).
    public var baseToneSeparation: Double { 12_000.0 / Double(nspsAt12k) }

    /// Recommended band/use-case hint.
    public var hint: String {
        switch self {
        case .p15:  return "2 m tropo/MS"
        case .p30:  return "70 cm"
        case .p60:  return "23 cm / 70 cm EME"
        case .p120: return "23 cm EME (deep)"
        case .p300: return "6 cm+ EME"
        }
    }
}

// Typealias kept for source compatibility in Q65ConfigView.
public typealias Q65TxSlot = TxSlot

// MARK: - Q65 Config (Q65-specific extras)

/// Q65-only parameters: sub-mode and period.
/// RX/TX freq and slot are now in OperatingConfig (shared by all modes).
public final class Q65Config: ObservableObject {

    @Published public var subMode: Q65SubMode = .a
    @Published public var period:  Q65Period  = .p60

    public var modeLabel: String { "Q65-\(period.rawValue)\(subMode.rawValue)" }

    /// Effective tone separation (Hz) for the current period + sub-mode.
    /// Falls back to period's base tone separation if the sub-mode is too
    /// narrow to fit 85 symbols in the T/R period.
    public var effectiveToneSeparation: Double {
        subMode.toneSeparation(for: period) ?? period.baseToneSeparation
    }

    /// Human-readable warning when the sub-mode doesn't fit the period.
    public var subModeWarning: String? {
        guard subMode.toneSeparation(for: period) == nil else { return nil }
        return "Sub-mode \(subMode.rawValue) too narrow for \(period.rawValue)s period; using A"
    }

    public var cycleLength: Double { Double(period.rawValue) }

    public init() {}
}
