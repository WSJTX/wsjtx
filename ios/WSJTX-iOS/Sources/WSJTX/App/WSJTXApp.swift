import SwiftUI
import Combine

@main
struct WSJTXApp: App {
    @StateObject private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            MainView()
                .environmentObject(appState)
                .preferredColorScheme(appState.settings.colorScheme)
        }
    }
}

/// Top-level application state shared across all views.
@MainActor
final class AppState: ObservableObject {
    @Published var settings       = AppSettings()
    @Published var audioEngine:     AudioEngine
    @Published var decoder:         FT8Decoder
    @Published var encoder:         FT8Encoder
    @Published var q65Encoder:      Q65Encoder
    @Published var opConfig        = OperatingConfig()
    @Published var q65Config       = Q65Config()
    @Published var messages:       [DecodedMessage] = []
    @Published var logbook:        [QSORecord]      = []
    @Published var currentBand:     Band            = .m20
    @Published var currentMode:     RadioMode       = .ft8 {
        didSet { currentPeriodSeconds = currentMode == .q65
            ? q65Config.period.rawValue
            : Int(currentMode.cycleLength) }
    }
    /// The effective T/R period in seconds — reflects mode and Q65 period setting.
    /// Publishing this here (rather than reading q65Config.period from a nested
    /// ObservableObject) ensures WaterfallTabView re-renders when period changes.
    @Published var currentPeriodSeconds: Int        = 15
    @Published var transmitting     = false
    @Published var txError:        String?          = nil
    @Published var dxCall           = ""
    @Published var dxGrid           = ""
    @Published var txMessage        = ""
    @Published var rigControl:      RigControl

    private var q65Cancellable: AnyCancellable?

    init() {
        let settings = AppSettings()
        let engine   = AudioEngine()
        let dec      = FT8Decoder()
        let enc      = FT8Encoder(settings: settings)
        let q65      = Q65Encoder(settings: settings)
        let rig      = RigControl(settings: settings)

        self.settings    = settings
        self.audioEngine = engine
        self.decoder     = dec
        self.encoder     = enc
        self.q65Encoder  = q65
        self.rigControl  = rig

        wireDecoder(engine: engine, decoder: dec)

        // Propagate q65Config.period changes into currentPeriodSeconds so that
        // WaterfallTabView (which observes AppState, not Q65Config) re-renders.
        q65Cancellable = q65Config.objectWillChange.sink { [weak self] _ in
            guard let self else { return }
            Task { @MainActor in
                if self.currentMode == .q65 {
                    self.currentPeriodSeconds = self.q65Config.period.rawValue
                }
            }
        }
    }

    private func wireDecoder(engine: AudioEngine, decoder: FT8Decoder) {
        engine.onSampleBuffer = { [weak self] samples, sampleRate in
            guard let self else { return }
            decoder.ingest(samples: samples, sampleRate: sampleRate)
        }
        decoder.onMessage = { [weak self] msg in
            guard let self else { return }
            Task { @MainActor in
                self.messages.insert(msg, at: 0)
                if self.messages.count > 500 { self.messages.removeLast() }
            }
        }
    }

    func startListening() { audioEngine.start() }
    func stopListening()  { audioEngine.stop()  }

    func stopTransmitting() {
        audioEngine.stopTransmit()
        transmitting = false
    }

    func transmit(message: String) {
        guard !settings.myCall.isEmpty else {
            txError = "Set your callsign in Settings before transmitting."
            return
        }

        let symbols: [Int]
        let toneSepOverride: Double?
        switch currentMode {
        case .ft8, .ft4:
            let encoded = encoder.encode(message: message, mode: currentMode)
            guard !encoded.isEmpty else { txError = "Could not encode: \(message)"; return }
            symbols       = encoded
            toneSepOverride = nil

        case .q65:
            let encoded = q65Encoder.encode(message: message, subMode: q65Config.subMode)
            guard !encoded.isEmpty else { txError = "Could not encode Q65: \(message)"; return }
            symbols       = encoded
            // Use the period-correct tone separation — this is what fixes the wrong TX duration.
            toneSepOverride = q65Config.effectiveToneSeparation

        default:
            txError = "\(currentMode.rawValue) TX not yet supported. Use FT8, FT4, or Q65."
            return
        }

        txError = nil
        let txHz = Double(opConfig.txFreq)
        print("[TX] \(currentMode.rawValue) \(symbols.count) symbols @ \(Int(txHz))Hz (sep=\(String(format:"%.3f", toneSepOverride ?? currentMode.toneSeparation))Hz): \(message)")
        audioEngine.transmit(symbols: symbols, mode: currentMode,
                             baseFreq: txHz, toneSeparationOverride: toneSepOverride) { [weak self] in
            DispatchQueue.main.async { self?.transmitting = false }
        }
        transmitting = true
    }

    func logQSO() {
        guard !dxCall.isEmpty else { return }
        let record = QSORecord(
            myCall: settings.myCall,
            dxCall: dxCall,
            frequency: currentBand.defaultFrequency(for: currentMode),
            mode: currentMode,
            grid: dxGrid
        )
        logbook.append(record)
        ADIFExporter.append(record: record, to: settings.logPath)
    }
}
