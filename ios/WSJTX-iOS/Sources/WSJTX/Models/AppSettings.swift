import Foundation
import SwiftUI

// MARK: - App Settings

/// Persisted user preferences, stored in UserDefaults.
public final class AppSettings: ObservableObject {

    // MARK: Station
    @AppStorage("myCall")     public var myCall     = ""
    @AppStorage("myGrid")     public var myGrid     = ""
    @AppStorage("myPower")    public var myPower    = 10   // dBm, default 10 mW

    // MARK: Audio
    @AppStorage("txGain")     public var txGain     = 0.5  // 0…1
    @AppStorage("rxGain")     public var rxGain     = 1.0  // 0…1
    @AppStorage("sampleRate") public var sampleRate = 12_000  // Hz

    // MARK: Network / Rig control
    @AppStorage("rigHost")    public var rigHost    = "192.168.1.1"
    @AppStorage("rigPort")    public var rigPort    = 4532  // Hamlib / flrig default
    @AppStorage("wsjtxPort")  public var wsjtxPort  = 2237  // WSJT-X UDP protocol

    // MARK: Decode
    @AppStorage("maxDecodes") public var maxDecodes = 30
    @AppStorage("nSHallow")   public var nShallow   = 2     // shallow decode passes
    @AppStorage("nDeep")      public var nDeep      = 0     // deep decode passes (CPU heavy)
    @AppStorage("tolerance")  public var tolerance   = 4.0  // Hz, frequency pull-in window

    // MARK: Logging
    @AppStorage("logPath")    public var logPath    = ""

    /// Resolved log URL; falls back to Documents/wsjtx.adi when path is empty.
    public var logURL: URL {
        guard !logPath.isEmpty else {
            let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first!
            return docs.appendingPathComponent("wsjtx.adi")
        }
        return URL(fileURLWithPath: logPath)
    }

    // MARK: UI
    @AppStorage("colorSchemeRaw") public var colorSchemeRaw = 0
    public var colorScheme: ColorScheme? {
        switch colorSchemeRaw {
        case 1:  return .light
        case 2:  return .dark
        default: return nil     // follow system
        }
    }
    public func setColorScheme(_ cs: ColorScheme?) {
        switch cs {
        case .light:  colorSchemeRaw = 1
        case .dark:   colorSchemeRaw = 2
        default:      colorSchemeRaw = 0
        }
    }

    @AppStorage("waterfallLow")  public var waterfallLow  = -55.0  // dB (noise floor for phone mic)
    @AppStorage("waterfallHigh") public var waterfallHigh =  10.0  // dB (just above full-scale)

    // MARK: PSK Reporter
    @AppStorage("pskReporter")   public var pskReporterEnabled = false

}
