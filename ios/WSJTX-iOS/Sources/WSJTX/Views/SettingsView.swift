import SwiftUI
import CoreLocation

// MARK: - Settings View

public struct SettingsView: View {

    @EnvironmentObject private var app: AppState
    @StateObject private var locationManager = LocationManager()
    @State private var showAbout = false

    public var body: some View {
        NavigationStack {
            Form {
                stationSection
                audioSection
                decodeSection
                rigControlSection
                pskReporterSection
                displaySection
                aboutSection
            }
            .navigationTitle("Settings")
            .sheet(isPresented: $showAbout) { AboutView() }
            .onReceive(locationManager.$grid) { grid in
                if !grid.isEmpty && app.settings.myGrid.isEmpty {
                    app.settings.myGrid = grid
                }
            }
        }
    }

    // MARK: Station

    private var stationSection: some View {
        Section("Station") {
            HStack {
                Label("Callsign", systemImage: "antenna.radiowaves.left.and.right")
                Spacer()
                TextField("W1AW", text: $app.settings.myCall)
                    .multilineTextAlignment(.trailing)
                    .textInputAutocapitalization(.characters)
                    .autocorrectionDisabled()
                    .onChange(of: app.settings.myCall) { _, new in
                        app.decoder.setMyCall(new)
                    }
            }

            HStack {
                Label("Grid", systemImage: "map")
                Spacer()
                TextField("FN31", text: $app.settings.myGrid)
                    .multilineTextAlignment(.trailing)
                    .textInputAutocapitalization(.characters)
                    .autocorrectionDisabled()
            }

            Button("Use GPS location") {
                locationManager.requestLocation()
            }

            Stepper("TX Power: \(app.settings.myPower) dBm",
                    value: $app.settings.myPower,
                    in: 0 ... 60,
                    step: 1)
        }
    }

    // MARK: Audio

    private var audioSection: some View {
        Section("Audio") {
            VStack(alignment: .leading) {
                Text("RX Gain: \(Int(app.settings.rxGain * 100))%")
                Slider(value: $app.settings.rxGain, in: 0 ... 2, step: 0.05)
            }
            VStack(alignment: .leading) {
                Text("TX Gain: \(Int(app.settings.txGain * 100))%")
                Slider(value: $app.settings.txGain, in: 0 ... 1, step: 0.05)
            }
        }
    }

    // MARK: Decode

    private var decodeSection: some View {
        Section("Decoder") {
            Stepper("Max decodes: \(app.settings.maxDecodes)",
                    value: $app.settings.maxDecodes,
                    in: 5 ... 200,
                    step: 5)
            Stepper("Shallow passes: \(app.settings.nShallow)",
                    value: $app.settings.nShallow,
                    in: 0 ... 5)
            Stepper("Deep passes: \(app.settings.nDeep)",
                    value: $app.settings.nDeep,
                    in: 0 ... 3)
            VStack(alignment: .leading) {
                Text("Freq tolerance: \(app.settings.tolerance, format: .number.precision(.fractionLength(1))) Hz")
                Slider(value: $app.settings.tolerance, in: 0.5 ... 10, step: 0.5)
            }
        }
    }

    // MARK: Rig Control

    private var rigControlSection: some View {
        Section("Rig Control (Hamlib / flrig)") {
            HStack {
                Text("Host")
                Spacer()
                TextField("192.168.1.1", text: $app.settings.rigHost)
                    .multilineTextAlignment(.trailing)
                    .keyboardType(.numbersAndPunctuation)
                    .autocorrectionDisabled()
            }
            HStack {
                Text("Port")
                Spacer()
                TextField("4532", value: $app.settings.rigPort, format: .number)
                    .multilineTextAlignment(.trailing)
                    .keyboardType(.numberPad)
            }
            HStack {
                Text("WSJT-X UDP port")
                Spacer()
                TextField("2237", value: $app.settings.wsjtxPort, format: .number)
                    .multilineTextAlignment(.trailing)
                    .keyboardType(.numberPad)
            }

            HStack {
                let connected = app.rigControl.isConnected
                Circle()
                    .fill(connected ? .green : .red)
                    .frame(width: 10, height: 10)
                Text(connected ? "Connected" : "Disconnected")
                    .foregroundStyle(.secondary)
                Spacer()
                Button(connected ? "Disconnect" : "Connect") {
                    connected ? app.rigControl.disconnect() : app.rigControl.connect()
                }
                .buttonStyle(.bordered)
                .font(.subheadline)
            }
        }
    }

    // MARK: PSK Reporter

    private var pskReporterSection: some View {
        Section("PSK Reporter") {
            Toggle("Upload spots", isOn: $app.settings.pskReporterEnabled)
        }
    }

    // MARK: Display

    private var displaySection: some View {
        Section("Display") {
            Picker("Colour scheme", selection: Binding(
                get: { app.settings.colorSchemeRaw },
                set: { app.settings.colorSchemeRaw = $0 }
            )) {
                Text("System").tag(0)
                Text("Light").tag(1)
                Text("Dark").tag(2)
            }
            .pickerStyle(.segmented)

            VStack(alignment: .leading) {
                Text("Waterfall low: \(app.settings.waterfallLow, format: .number.precision(.fractionLength(0))) dB")
                Slider(value: $app.settings.waterfallLow, in: -30 ... 10)
            }
            VStack(alignment: .leading) {
                Text("Waterfall high: \(app.settings.waterfallHigh, format: .number.precision(.fractionLength(0))) dB")
                Slider(value: $app.settings.waterfallHigh, in: 10 ... 60)
            }
        }
    }

    // MARK: About

    private var aboutSection: some View {
        Section {
            Button("About WSJT-X iOS") { showAbout = true }
        }
    }
}

// MARK: - Location Manager

final class LocationManager: NSObject, ObservableObject, CLLocationManagerDelegate {
    @Published var grid = ""
    private let mgr = CLLocationManager()

    override init() {
        super.init()
        mgr.delegate = self
        mgr.desiredAccuracy = kCLLocationAccuracyKilometer
    }

    func requestLocation() {
        mgr.requestWhenInUseAuthorization()
        mgr.requestLocation()
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let loc = locations.first else { return }
        grid = GridLocator.grid(from: loc.coordinate)
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        print("[Location] \(error.localizedDescription)")
    }
}

// MARK: - About View

struct AboutView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    Image(systemName: "antenna.radiowaves.left.and.right")
                        .font(.system(size: 60))
                        .foregroundStyle(.tint)

                    Text("WSJT-X iOS")
                        .font(.largeTitle.bold())

                    Text("A native iPhone port of WSJT-X, the weak-signal amateur radio software.")
                        .multilineTextAlignment(.center)
                        .foregroundStyle(.secondary)

                    Divider()

                    Group {
                        InfoRow(label: "Modes", value: "FT8 · FT4 · JT65 · JT9 · WSPR · Q65")
                        InfoRow(label: "Upstream", value: "wsjt.sourceforge.io")
                        InfoRow(label: "License", value: "GNU GPL v3")
                        InfoRow(label: "Original authors", value: "Joe Taylor K1JT et al.")
                    }
                }
                .padding()
            }
            .navigationTitle("About")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}

struct InfoRow: View {
    let label: String
    let value: String
    var body: some View {
        HStack {
            Text(label).foregroundStyle(.secondary)
            Spacer()
            Text(value).multilineTextAlignment(.trailing)
        }
        .font(.subheadline)
    }
}

// Expose colorSchemeRaw for settings view binding — property is public on AppSettings.
