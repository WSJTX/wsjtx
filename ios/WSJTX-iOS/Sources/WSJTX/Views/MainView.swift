import SwiftUI

// MARK: - Main View

/// Root view — tab bar with Waterfall, Messages, Logbook, and Settings.
public struct MainView: View {

    @EnvironmentObject private var app: AppState
    @State private var selectedTab = 0

    public init() {}

    public var body: some View {
        TabView(selection: $selectedTab) {
            WaterfallTabView()
                .tabItem { Label("Waterfall", systemImage: "waveform") }
                .tag(0)

            MessageListView()
                .tabItem { Label("Decodes", systemImage: "text.bubble") }
                .tag(1)

            LogbookView()
                .tabItem { Label("Logbook", systemImage: "book.closed") }
                .tag(2)

            SettingsView()
                .tabItem { Label("Settings", systemImage: "gear") }
                .tag(3)
        }
        .onAppear {
            app.startListening()   // always start RX; TX still requires a callsign
        }
    }
}

// MARK: - Waterfall Tab

/// Top-level wrapper that stacks the waterfall, frequency strip,
/// message compressor, and TX controls.
struct WaterfallTabView: View {
    @EnvironmentObject private var app: AppState
    @StateObject private var waterfall = WaterfallData()
    @State private var showCompose = false
    @State private var cycleTimer: Timer?
    @State private var secondsRemaining = 15

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // ── Band / Mode bar ──────────────────────────────────────
                BandModeBar()

                // ── Config strip — visible for ALL modes ─────────────────
                if app.currentMode == .q65 {
                    // Q65: FreqSlotBar is embedded inside Q65ConfigPanel
                    Q65ConfigPanel(config: app.q65Config, op: app.opConfig)
                } else {
                    // Every other mode gets the compact freq + slot strip
                    FreqSlotBar(op: app.opConfig,
                                cycleSeconds: Int(app.currentMode.cycleLength))
                }

                Divider()

                // ── Waterfall ─────────────────────────────────────────────
                WaterfallView(data: waterfall,
                              dbLow:         Float(app.settings.waterfallLow),
                              dbHigh:        Float(app.settings.waterfallHigh),
                              rxFreq:        app.opConfig.rxFreq,
                              txFreq:        app.opConfig.txFreq,
                              transmitting:  app.transmitting,
                              periodSeconds: app.currentPeriodSeconds)
                    .frame(maxWidth: .infinity)
                    .frame(height: app.currentMode == .q65 ? 200 : 260)

                Divider()

                // ── Cycle timer ───────────────────────────────────────────
                HStack {
                    CycleTimerView(
                        secondsRemaining: $secondsRemaining,
                        cycleLength: app.currentPeriodSeconds
                    )
                    Spacer()
                    LevelMeter(engine: app.audioEngine)
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 4)

                Divider()

                // ── Quick TX panel ────────────────────────────────────────
                QuickTXPanel()
            }
            .navigationTitle("WSJT-X")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    ListenButton(engine: app.audioEngine) {
                        app.audioEngine.isRunning ? app.stopListening() : app.startListening()
                    }
                }
            }
        }
        .onAppear {
            app.audioEngine.onSampleBuffer = { [weak app] samples, rate in
                waterfall.ingest(samples: samples)
                app?.decoder.ingest(samples: samples, sampleRate: rate)
            }
            startCycleTimer()
        }
        .onDisappear { cycleTimer?.invalidate() }
    }

    private func startCycleTimer() {
        cycleTimer?.invalidate()
        cycleTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak app] _ in
            guard let app else { return }
            let now   = Int(Date().timeIntervalSince1970)
            let cycle = app.currentPeriodSeconds
            secondsRemaining = cycle - (now % cycle)
        }
    }
}

// MARK: - Band / Mode Bar

struct BandModeBar: View {
    @EnvironmentObject private var app: AppState

    var body: some View {
        HStack(spacing: 8) {
            Picker("Band", selection: $app.currentBand) {
                ForEach(Band.allCases) { b in Text(b.rawValue).tag(b) }
            }
            .pickerStyle(.menu)
            .fixedSize()   // expand to fit the widest current label, no wrapping

            Picker("Mode", selection: $app.currentMode) {
                ForEach(RadioMode.allCases) { m in Text(m.rawValue).tag(m) }
            }
            .pickerStyle(.menu)
            .fixedSize()

            Spacer()

            Text(app.currentBand.frequencyString(for: app.currentMode))
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.secondary)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color(.secondarySystemBackground))
    }
}

// MARK: - Cycle Timer

struct CycleTimerView: View {
    @Binding var secondsRemaining: Int
    let cycleLength: Int

    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: "timer")
                .foregroundStyle(.secondary)
                .font(.caption)
            ProgressView(value: Double(cycleLength - secondsRemaining),
                         total: Double(cycleLength))
                .frame(width: 80)
                .tint(secondsRemaining <= 3 ? .red : .green)
            Text("\(secondsRemaining)s")
                .font(.system(.caption2, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 28, alignment: .trailing)
        }
    }
}

// MARK: - Level Meter

/// Observes AudioEngine directly so @Published inputLevel re-renders this view
/// independently of the parent view's update cycle.
struct LevelMeter: View {
    @ObservedObject var engine: AudioEngine

    var body: some View {
        HStack(spacing: 2) {
            Image(systemName: "mic")
                .font(.caption)
                .foregroundStyle(.secondary)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 3).fill(Color(.systemFill))
                    RoundedRectangle(cornerRadius: 3)
                        .fill(levelColor)
                        .frame(width: geo.size.width * CGFloat(engine.inputLevel))
                }
            }
            .frame(width: 80, height: 10)
        }
    }

    private var levelColor: Color {
        engine.inputLevel > 0.85 ? .red : engine.inputLevel > 0.6 ? .yellow : .green
    }
}

// MARK: - Listen Button

/// Observes AudioEngine directly so the label flips between Stop/Listen
/// without depending on the parent view's re-render cycle.
struct ListenButton: View {
    @ObservedObject var engine: AudioEngine
    let action: () -> Void

    var body: some View {
        Button(engine.isRunning ? "Stop" : "Listen", action: action)
    }
}

// MARK: - Quick TX Panel

struct QuickTXPanel: View {
    @EnvironmentObject private var app: AppState
    @State private var showCompose = false

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("DX: \(app.dxCall.isEmpty ? "—" : app.dxCall)")
                        .font(.system(.subheadline, design: .monospaced))
                        .bold()
                    Text("Grid: \(app.dxGrid.isEmpty ? "—" : app.dxGrid)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button {
                    showCompose = true
                } label: {
                    Label("TX", systemImage: "antenna.radiowaves.left.and.right")
                        .font(.subheadline.bold())
                }
                .buttonStyle(.borderedProminent)
                .tint(app.transmitting ? .red : .accentColor)
                .disabled(app.settings.myCall.isEmpty)
            }

            if !app.txMessage.isEmpty {
                Text(app.txMessage)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
        .padding(12)
        .sheet(isPresented: $showCompose) {
            ComposeMessageView()
        }
    }
}
