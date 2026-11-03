import SwiftUI

// MARK: - Logbook View

public struct LogbookView: View {

    @EnvironmentObject private var app: AppState
    @State private var searchText = ""
    @State private var showExporter = false
    @State private var adifText = ""

    private var filtered: [QSORecord] {
        guard !searchText.isEmpty else { return app.logbook }
        return app.logbook.filter {
            $0.dxCall.localizedCaseInsensitiveContains(searchText) ||
            $0.grid.localizedCaseInsensitiveContains(searchText)
        }
    }

    public var body: some View {
        NavigationStack {
            List(filtered.reversed()) { record in
                QSORow(record: record)
            }
            .listStyle(.plain)
            .searchable(text: $searchText)
            .navigationTitle("Logbook (\(app.logbook.count))")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Menu {
                        Button("Export ADIF…") {
                            adifText  = ADIFExporter.export(records: app.logbook)
                            showExporter = true
                        }
                        Button("Clear log…", role: .destructive) {
                            app.logbook.removeAll()
                        }
                    } label: {
                        Image(systemName: "ellipsis.circle")
                    }
                }
            }
            .sheet(isPresented: $showExporter) {
                ADIFExportSheet(adif: adifText)
            }
            .overlay {
                if app.logbook.isEmpty {
                    ContentUnavailableView(
                        "No QSOs logged",
                        systemImage: "book.closed",
                        description: Text("Swipe a decoded message left to log a QSO.")
                    )
                }
            }
        }
    }
}

// MARK: - QSO Row

struct QSORow: View {
    let record: QSORecord

    var body: some View {
        HStack(alignment: .top) {
            VStack(alignment: .leading, spacing: 2) {
                Text(record.dxCall)
                    .font(.headline)
                Text(record.grid.isEmpty ? "—" : record.grid)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 2) {
                Text(record.mode.rawValue)
                    .font(.caption.bold())
                    .foregroundStyle(Color.accentColor)
                Text(record.frequencyMHz + " MHz")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                Text("\(record.dateString) \(record.timeString)Z")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 4)
    }
}

// MARK: - ADIF Export Sheet

struct ADIFExportSheet: View {
    let adif: String
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ScrollView {
                Text(adif)
                    .font(.system(.caption2, design: .monospaced))
                    .padding()
                    .textSelection(.enabled)
            }
            .navigationTitle("ADIF Export")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    ShareLink(item: adif, subject: Text("WSJTX-iOS Log"),
                              message: Text("Amateur radio QSO log"))
                }
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}
