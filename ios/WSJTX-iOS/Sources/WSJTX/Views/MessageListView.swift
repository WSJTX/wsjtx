import SwiftUI

// MARK: - Message List View

/// Displays all decoded messages for the current period.  Selecting a message
/// pre-fills the TX fields and advances the QSO state.
public struct MessageListView: View {

    @EnvironmentObject private var app: AppState
    @State private var filter  = MessageFilter.all
    @State private var search  = ""

    private var filtered: [DecodedMessage] {
        app.messages.filter { msg in
            (filter == .all ||
             (filter == .cq        && msg.isCQ) ||
             (filter == .mine      && msg.isDirectedToMe) ||
             (filter == .new       && msg.isNew))
            &&
            (search.isEmpty ||
             msg.text.localizedCaseInsensitiveContains(search) ||
             (msg.callsignA?.localizedCaseInsensitiveContains(search) ?? false))
        }
    }

    public var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                filterBar
                messageTable
            }
            .navigationTitle("Decodes")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(role: .destructive) { app.messages.removeAll() } label: {
                        Image(systemName: "trash")
                    }
                }
            }
            .searchable(text: $search, placement: .navigationBarDrawer)
        }
    }

    // MARK: Filter bar

    private var filterBar: some View {
        Picker("Filter", selection: $filter) {
            ForEach(MessageFilter.allCases) { f in
                Text(f.label).tag(f)
            }
        }
        .pickerStyle(.segmented)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color(.secondarySystemBackground))
    }

    // MARK: Table

    private var messageTable: some View {
        List(filtered) { msg in
            MessageRow(msg: msg)
                .listRowBackground(rowBackground(for: msg))
                .onTapGesture { selectMessage(msg) }
                .swipeActions(edge: .leading) {
                    Button {
                        app.dxCall = msg.callsignA ?? ""
                        app.dxGrid = msg.grid ?? ""
                        app.logQSO()
                    } label: { Label("Log", systemImage: "pencil") }
                        .tint(.blue)
                }
        }
        .listStyle(.plain)
        .font(.system(.caption, design: .monospaced))
    }

    private func rowBackground(for msg: DecodedMessage) -> Color {
        if msg.isDirectedToMe { return Color.green.opacity(0.2) }
        if msg.isCQ           { return Color.yellow.opacity(0.12) }
        return Color.clear
    }

    private func selectMessage(_ msg: DecodedMessage) {
        app.dxCall = msg.callsignA ?? ""
        app.dxGrid = msg.grid ?? ""
    }
}

// MARK: - Message Row

struct MessageRow: View {
    let msg: DecodedMessage

    var body: some View {
        HStack(alignment: .top, spacing: 0) {
            // Time
            Text(msg.utcTime)
                .foregroundStyle(.secondary)
                .frame(width: 38, alignment: .leading)

            // SNR
            Text(snrText)
                .foregroundStyle(snrColor)
                .frame(width: 38, alignment: .trailing)

            // DT
            Text(String(format: "%+.1f", msg.dt))
                .foregroundStyle(.secondary)
                .frame(width: 36, alignment: .trailing)

            // Freq
            Text("\(msg.frequency)")
                .foregroundStyle(.secondary)
                .frame(width: 44, alignment: .trailing)

            Spacer(minLength: 8)

            // Message text
            Text(msg.text)
                .foregroundStyle(textColor)
                .lineLimit(1)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(.vertical, 2)
    }

    private var snrText: String {
        msg.snr >= 0 ? "+\(msg.snr)" : "\(msg.snr)"
    }

    private var snrColor: Color {
        msg.snr >= 10 ? .green : msg.snr >= 0 ? .primary : .orange
    }

    private var textColor: Color {
        if msg.isDirectedToMe { return .green }
        if msg.isCQ           { return .yellow }
        return .primary
    }
}

// MARK: - Filter enum

enum MessageFilter: String, CaseIterable, Identifiable {
    case all, cq, mine, new
    var id: String { rawValue }
    var label: String {
        switch self {
        case .all:  return "All"
        case .cq:   return "CQ"
        case .mine: return "Mine"
        case .new:  return "New"
        }
    }
}
