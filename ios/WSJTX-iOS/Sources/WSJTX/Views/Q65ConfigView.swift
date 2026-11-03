import SwiftUI

// MARK: - Freq + Slot Bar (ALL modes)

/// Compact RX/TX frequency strip and 1st/2nd slot toggle shown for every mode.
public struct FreqSlotBar: View {

    @ObservedObject var op: OperatingConfig
    let cycleSeconds: Int
    @State private var showDetail = false

    public init(op: OperatingConfig, cycleSeconds: Int) {
        self.op           = op
        self.cycleSeconds = cycleSeconds
    }

    public var body: some View {
        VStack(spacing: 0) {
            summaryRow
            if showDetail { detailPanel }
        }
        .background(Color(.tertiarySystemBackground))
    }

    private var summaryRow: some View {
        HStack(spacing: 10) {
            // RX freq badge
            Label("\(op.rxFreq) Hz", systemImage: "dot.radiowaves.left.and.right")
                .labelStyle(.titleAndIcon)
                .font(.system(.caption2, design: .monospaced))
                .foregroundStyle(.green)

            if op.rxFreq != op.txFreq {
                Image(systemName: "arrow.right").font(.caption2).foregroundStyle(.secondary)
                Text("TX \(op.txFreq) Hz")
                    .font(.system(.caption2, design: .monospaced)).foregroundStyle(.red)
            }

            Spacer()

            // Slot toggle button
            Button {
                op.txSlot = op.txSlot == .first ? .second : .first
            } label: {
                Text("TX \(op.txSlot.rawValue)")
                    .font(.caption.bold())
                    .padding(.horizontal, 6).padding(.vertical, 2)
                    .background(Color.orange.opacity(0.2))
                    .clipShape(RoundedRectangle(cornerRadius: 4))
            }
            .buttonStyle(.plain)

            SlotStatusBadge(op: op, cycleSeconds: cycleSeconds)

            Button { withAnimation { showDetail.toggle() } } label: {
                Image(systemName: showDetail ? "chevron.up" : "chevron.down")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .padding(.horizontal, 12).padding(.vertical, 6)
    }

    private var detailPanel: some View {
        VStack(spacing: 8) {
            Divider()

            // RX frequency
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("RX freq").font(.caption2).foregroundStyle(.secondary)
                    Spacer()
                    Text("\(op.rxFreq) Hz")
                        .font(.system(.caption, design: .monospaced)).foregroundStyle(.green)
                    Stepper("", value: Binding(
                        get: { op.rxFreq }, set: { op.setRxFreq($0) }
                    ), in: 200...3000, step: 50).labelsHidden().frame(width: 80)
                }
                Slider(value: Binding(
                    get: { Double(op.rxFreq) }, set: { op.setRxFreq(Int($0)) }
                ), in: 200...3000, step: 50).tint(.green)
            }

            // TX frequency + lock
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("TX freq").font(.caption2).foregroundStyle(.secondary)
                    Spacer()
                    Text("\(op.txFreq) Hz")
                        .font(.system(.caption, design: .monospaced)).foregroundStyle(.red)
                    Toggle("", isOn: $op.freqLocked).labelsHidden()
                        .onChange(of: op.freqLocked) { _, locked in if locked { op.txFreq = op.rxFreq } }
                    Image(systemName: op.freqLocked ? "lock" : "lock.open")
                        .font(.caption).foregroundStyle(.secondary)
                    Stepper("", value: $op.txFreq, in: 200...3000, step: 50)
                        .labelsHidden().frame(width: 80).disabled(op.freqLocked)
                }
                Slider(value: Binding(
                    get: { Double(op.txFreq) },
                    set: { op.txFreq = op.freqLocked ? op.rxFreq : Int($0) }
                ), in: 200...3000, step: 50).tint(.red).disabled(op.freqLocked)
            }

            // TX slot
            HStack {
                Text("TX slot").font(.caption2).foregroundStyle(.secondary)
                Picker("TX slot", selection: $op.txSlot) {
                    ForEach(TxSlot.allCases) { s in Text(s.rawValue).tag(s) }
                }
                .pickerStyle(.segmented).frame(width: 140)
                Spacer()
            }
        }
        .padding(.horizontal, 12).padding(.bottom, 8)
    }
}

// MARK: - Slot Status Badge

/// "TX NOW" / "RX NOW" badge — updates every second from UTC.
public struct SlotStatusBadge: View {
    @ObservedObject var op: OperatingConfig
    let cycleSeconds: Int
    @State private var myTurn = false
    private let timer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    public var body: some View {
        Text(myTurn ? "TX NOW" : "RX NOW")
            .font(.caption2.bold())
            .padding(.horizontal, 5).padding(.vertical, 2)
            .background(myTurn ? Color.red.opacity(0.2) : Color.green.opacity(0.2))
            .foregroundStyle(myTurn ? .red : .green)
            .clipShape(RoundedRectangle(cornerRadius: 4))
            .onReceive(timer) { _ in myTurn = op.txSlot.isMyTurn(cycleSeconds: cycleSeconds) }
            .onAppear     { myTurn = op.txSlot.isMyTurn(cycleSeconds: cycleSeconds) }
    }
}

// MARK: - Q65 Config Panel (Q65-specific extras only)

/// Shows Q65-specific controls (sub-mode, period, hint) below FreqSlotBar.
public struct Q65ConfigPanel: View {

    @ObservedObject var config: Q65Config
    @ObservedObject var op: OperatingConfig

    public init(config: Q65Config, op: OperatingConfig) {
        self.config = config
        self.op     = op
    }

    public var body: some View {
        VStack(spacing: 0) {
            FreqSlotBar(op: op, cycleSeconds: config.period.rawValue)
            Divider()
            q65Extras
        }
        .background(Color(.tertiarySystemBackground))
    }

    private var q65Extras: some View {
        VStack(spacing: 8) {
            HStack(spacing: 16) {
                // Mode badge
                Text(config.modeLabel)
                    .font(.system(.caption, design: .monospaced).bold())
                    .padding(.horizontal, 6).padding(.vertical, 2)
                    .background(Color.purple.opacity(0.2))
                    .clipShape(RoundedRectangle(cornerRadius: 4))

                Spacer()

                // Sub-mode picker
                VStack(alignment: .trailing, spacing: 2) {
                    Text("Sub-mode").font(.caption2).foregroundStyle(.secondary)
                    Picker("Sub-mode", selection: $config.subMode) {
                        ForEach(Q65SubMode.allCases) { s in Text(s.rawValue).tag(s) }
                    }
                    .pickerStyle(.segmented).frame(width: 185)
                }
            }

            HStack {
                // Period picker
                VStack(alignment: .leading, spacing: 2) {
                    Text("Period").font(.caption2).foregroundStyle(.secondary)
                    Picker("Period", selection: $config.period) {
                        ForEach(Q65Period.allCases) { p in Text(p.label).tag(p) }
                    }.pickerStyle(.segmented)
                }

                Spacer(minLength: 8)

                Text(config.period.hint)
                    .font(.caption2).foregroundStyle(.secondary)
                    .multilineTextAlignment(.trailing)
            }
        }
        .padding(.horizontal, 12).padding(.vertical, 8)
    }
}
