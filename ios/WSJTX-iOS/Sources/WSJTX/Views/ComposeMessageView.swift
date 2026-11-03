import SwiftUI

// MARK: - Compose Message View

/// Modal sheet for composing a TX message and controlling the transmitter.
public struct ComposeMessageView: View {

    @EnvironmentObject private var app: AppState
    @Environment(\.dismiss) private var dismiss

    @State private var stage: QSOStage = .calling
    @State private var customMessage = ""
    @State private var useCustom = false
    @State private var reportOffset = 0   // −30 … +30 dB adjust

    private var suggestedMessage: String {
        app.encoder.nextMessage(
            myCall: app.settings.myCall,
            dxCall: app.dxCall,
            myGrid: app.settings.myGrid,
            report: reportOffset,
            qsoStage: stage
        )
    }

    public var body: some View {
        NavigationStack {
            Form {
                // ── DX info ──────────────────────────────────────────
                Section("Contact") {
                    HStack {
                        Text("Their call")
                        Spacer()
                        TextField("DX callsign", text: $app.dxCall)
                            .multilineTextAlignment(.trailing)
                            .textInputAutocapitalization(.characters)
                            .autocorrectionDisabled()
                    }
                    HStack {
                        Text("Their grid")
                        Spacer()
                        TextField("e.g. FN31", text: $app.dxGrid)
                            .multilineTextAlignment(.trailing)
                            .textInputAutocapitalization(.characters)
                            .autocorrectionDisabled()
                    }
                }

                // ── QSO stage ────────────────────────────────────────
                Section("Stage") {
                    Picker("Stage", selection: $stage) {
                        Text("CQ").tag(QSOStage.calling)
                        Text("Answer").tag(QSOStage.answered)
                        Text("Report").tag(QSOStage.report)
                        Text("RRR").tag(QSOStage.rrr)
                        Text("73").tag(QSOStage.r73)
                    }
                    .pickerStyle(.segmented)

                    if stage == .answered || stage == .report {
                        Stepper("Report: \(reportOffset >= 0 ? "+" : "")\(reportOffset) dB",
                                value: $reportOffset,
                                in: -30 ... 30,
                                step: 1)
                    }
                }

                // ── Message preview ──────────────────────────────────
                Section("Message") {
                    Toggle("Custom", isOn: $useCustom)
                    if useCustom {
                        TextField("Free text (max 13 chars)", text: $customMessage)
                            .textInputAutocapitalization(.characters)
                            .autocorrectionDisabled()
                            .onChange(of: customMessage) { _, new in
                                if new.count > 13 { customMessage = String(new.prefix(13)) }
                            }
                    } else {
                        Text(suggestedMessage)
                            .font(.system(.body, design: .monospaced))
                            .foregroundStyle(.primary)
                    }
                }

                // ── TX controls ──────────────────────────────────────
                Section {
                    Button(action: transmit) {
                        Label(
                            app.transmitting ? "Stop TX" : "Transmit",
                            systemImage: app.transmitting
                                ? "stop.circle.fill"
                                : "antenna.radiowaves.left.and.right"
                        )
                        .frame(maxWidth: .infinity)
                        .font(.headline)
                    }
                    .listRowBackground(app.transmitting ? Color.red : Color.accentColor)
                    .foregroundStyle(.white)
                    .disabled(app.settings.myCall.isEmpty)

                    if let err = app.txError {
                        Text(err)
                            .font(.caption)
                            .foregroundStyle(.red)
                    }
                }
            }
            .navigationTitle("Compose TX")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }

    private func transmit() {
        if app.transmitting {
            app.stopTransmitting()
        } else {
            let msg = useCustom ? customMessage : suggestedMessage
            app.txMessage = msg
            app.transmit(message: msg)
        }
    }
}
