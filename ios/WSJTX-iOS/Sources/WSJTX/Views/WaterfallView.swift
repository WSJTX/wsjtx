import SwiftUI
import UIKit

// MARK: - Waterfall View

/// Scrolling waterfall spectrogram with RX/TX frequency markers and period boundary lines.
public struct WaterfallView: UIViewRepresentable {

    @ObservedObject var data: WaterfallData

    public var fLow:  Float = 200
    public var fHigh: Float = 3000
    public var dbLow:  Float = -55
    public var dbHigh: Float =  10

    public var rxFreq: Int = 1_000
    public var txFreq: Int = 1_000
    public var transmitting: Bool = false

    /// T/R period in seconds — drives the horizontal period boundary lines.
    public var periodSeconds: Int = 15

    public func makeUIView(context: Context) -> WaterfallUIView {
        let v = WaterfallUIView()
        v.fLow          = fLow
        v.fHigh         = fHigh
        v.dbLow         = dbLow
        v.dbHigh        = dbHigh
        v.periodSeconds = periodSeconds
        data.onRow = { [weak v] bins, freqAxis in
            DispatchQueue.main.async { v?.pushRow(bins, freqAxis: freqAxis) }
        }
        return v
    }

    public func updateUIView(_ uiView: WaterfallUIView, context: Context) {
        uiView.dbLow         = dbLow
        uiView.dbHigh        = dbHigh
        uiView.rxFreq        = Float(rxFreq)
        uiView.txFreq        = Float(txFreq)
        uiView.transmitting  = transmitting
        uiView.periodSeconds = periodSeconds
    }
}

// MARK: - WaterfallUIView

public final class WaterfallUIView: UIView {

    public var fLow:  Float = 200
    public var fHigh: Float = 3000
    public var dbLow:  Float = -55
    public var dbHigh: Float =  10

    // RX/TX frequency markers
    public var rxFreq:       Float = 1_000
    public var txFreq:       Float = 1_000
    public var transmitting: Bool  = false

    /// T/R period length in seconds for the horizontal period lines.
    public var periodSeconds: Int = 15 {
        didSet { periodOverlay.periodSeconds = periodSeconds }
    }

    private let pixelRowHeight: Int = 2

    private var imageBuffer:  UIImage?
    private let imageView      = UIImageView()
    private let freqOverlay    = FrequencyAxisOverlay()
    private let markerOverlay  = FreqMarkerOverlay()
    private let periodOverlay  = PeriodLineOverlay()

    public var onFrequencySelected: ((Float) -> Void)?

    public override init(frame: CGRect) { super.init(frame: frame); setup() }
    required init?(coder: NSCoder) { super.init(coder: coder); setup() }

    private func setup() {
        backgroundColor = .black
        imageView.contentMode = .scaleToFill
        imageView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(imageView)

        freqOverlay.translatesAutoresizingMaskIntoConstraints = false
        addSubview(freqOverlay)

        for overlay in [markerOverlay, periodOverlay] as [UIView] {
            overlay.translatesAutoresizingMaskIntoConstraints = false
            overlay.backgroundColor = .clear
            overlay.isUserInteractionEnabled = false
            addSubview(overlay)
        }

        NSLayoutConstraint.activate([
            imageView.topAnchor.constraint(equalTo: topAnchor),
            imageView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -20),
            imageView.leadingAnchor.constraint(equalTo: leadingAnchor),
            imageView.trailingAnchor.constraint(equalTo: trailingAnchor),

            freqOverlay.topAnchor.constraint(equalTo: imageView.bottomAnchor),
            freqOverlay.bottomAnchor.constraint(equalTo: bottomAnchor),
            freqOverlay.leadingAnchor.constraint(equalTo: leadingAnchor),
            freqOverlay.trailingAnchor.constraint(equalTo: trailingAnchor),

            markerOverlay.topAnchor.constraint(equalTo: topAnchor),
            markerOverlay.bottomAnchor.constraint(equalTo: imageView.bottomAnchor),
            markerOverlay.leadingAnchor.constraint(equalTo: leadingAnchor),
            markerOverlay.trailingAnchor.constraint(equalTo: trailingAnchor),

            periodOverlay.topAnchor.constraint(equalTo: topAnchor),
            periodOverlay.bottomAnchor.constraint(equalTo: imageView.bottomAnchor),
            periodOverlay.leadingAnchor.constraint(equalTo: leadingAnchor),
            periodOverlay.trailingAnchor.constraint(equalTo: trailingAnchor),
        ])

        periodOverlay.periodSeconds = periodSeconds
        periodOverlay.rowHeight     = pixelRowHeight

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        addGestureRecognizer(tap)
    }

    // MARK: - Row push

    public func pushRow(_ row: [Float], freqAxis: [Float]) {
        let width = Int(bounds.width) > 0 ? Int(bounds.width) : 375
        let height = pixelRowHeight

        let pixels = mapRow(row, freqAxis: freqAxis, width: width)
        let newStrip = renderStrip(pixels: pixels, width: width, height: height)

        let totalHeight = Int(imageView.bounds.height > 0 ? imageView.bounds.height : 280)
        UIGraphicsBeginImageContextWithOptions(CGSize(width: width, height: totalHeight), false, 1)
        newStrip.draw(in: CGRect(x: 0, y: 0, width: width, height: height))
        if let old = imageBuffer {
            old.draw(in: CGRect(x: 0, y: height, width: width, height: totalHeight - height))
        }
        let composed = UIGraphicsGetImageFromCurrentImageContext()
        UIGraphicsEndImageContext()

        imageBuffer     = composed
        imageView.image = composed
        freqOverlay.fLow  = fLow
        freqOverlay.fHigh = fHigh
        freqOverlay.setNeedsDisplay()

        // Refresh frequency marker lines
        markerOverlay.fLow         = fLow
        markerOverlay.fHigh        = fHigh
        markerOverlay.rxFreq       = rxFreq
        markerOverlay.txFreq       = txFreq
        markerOverlay.transmitting = transmitting
        markerOverlay.setNeedsDisplay()

        // Advance period line overlay — draws horizontal UTC timestamp lines
        periodOverlay.addRow()
    }

    // MARK: - Colour mapping

    private func mapRow(_ row: [Float], freqAxis: [Float], width: Int) -> [UIColor] {
        guard !freqAxis.isEmpty else { return [UIColor](repeating: .black, count: width) }
        return (0 ..< width).map { px in
            let hz = fLow + (fHigh - fLow) * Float(px) / Float(width)
            let db = interpolateDB(hz: hz, row: row, freqAxis: freqAxis)
            return waterfallColor(db: db)
        }
    }

    private func interpolateDB(hz: Float, row: [Float], freqAxis: [Float]) -> Float {
        guard let lo = freqAxis.lastIndex(where: { $0 <= hz }),
              lo + 1 < row.count else {
            return row.last ?? dbLow
        }
        let f0 = freqAxis[lo], f1 = freqAxis[lo + 1]
        let t  = f1 > f0 ? (hz - f0) / (f1 - f0) : 0
        return row[lo] * (1 - t) + row[lo + 1] * t
    }

    /// WSJT-X style blue→cyan→green→yellow→red colour map.
    private func waterfallColor(db: Float) -> UIColor {
        let norm = max(0, min(1, (db - dbLow) / (dbHigh - dbLow)))
        let h, s, b: CGFloat
        switch norm {
        case ..<0.25:
            h = 0.67; s = 1; b = CGFloat(norm * 4)
        case ..<0.5:
            h = 0.5 + CGFloat((norm - 0.25) * 2 * 0.17); s = 1; b = 1
        case ..<0.75:
            h = 0.33 - CGFloat((norm - 0.5) * 2 * 0.16); s = 1; b = 1
        default:
            h = 0; s = 1; b = 1
        }
        return UIColor(hue: h, saturation: s, brightness: max(0.05, b), alpha: 1)
    }

    private func renderStrip(pixels: [UIColor], width: Int, height: Int) -> UIImage {
        UIGraphicsBeginImageContextWithOptions(CGSize(width: width, height: height), false, 1)
        for (x, c) in pixels.enumerated() {
            c.setFill()
            UIRectFill(CGRect(x: x, y: 0, width: 1, height: height))
        }
        let img = UIGraphicsGetImageFromCurrentImageContext() ?? UIImage()
        UIGraphicsEndImageContext()
        return img
    }

    // MARK: - Touch → frequency

    @objc private func handleTap(_ gr: UITapGestureRecognizer) {
        let pt = gr.location(in: imageView)
        let w  = imageView.bounds.width
        guard w > 0 else { return }
        let hz = fLow + (fHigh - fLow) * Float(pt.x / w)
        rxFreq = hz
        onFrequencySelected?(hz)
        markerOverlay.rxFreq = hz
        markerOverlay.setNeedsDisplay()
    }
}

// MARK: - Frequency Marker Overlay

/// Draws thin vertical lines for the RX (green) and TX (red) audio frequencies.
final class FreqMarkerOverlay: UIView {
    var fLow:  Float = 200
    var fHigh: Float = 3000
    var rxFreq: Float = 1_000
    var txFreq: Float = 1_000
    var transmitting: Bool = false

    private func xPos(for freq: Float, in rect: CGRect) -> CGFloat {
        CGFloat((freq - fLow) / (fHigh - fLow)) * rect.width
    }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }

        // RX marker — always shown
        let rx = xPos(for: rxFreq, in: rect)
        ctx.setStrokeColor(UIColor.green.withAlphaComponent(0.85).cgColor)
        ctx.setLineWidth(1.5)
        ctx.setLineDash(phase: 0, lengths: [4, 2])
        ctx.move(to: CGPoint(x: rx, y: 0))
        ctx.addLine(to: CGPoint(x: rx, y: rect.height))
        ctx.strokePath()

        // TX marker — solid red only when different from RX or transmitting
        if txFreq != rxFreq || transmitting {
            let tx = xPos(for: txFreq, in: rect)
            ctx.setStrokeColor((transmitting ? UIColor.red : UIColor.red.withAlphaComponent(0.5)).cgColor)
            ctx.setLineWidth(transmitting ? 2 : 1)
            ctx.setLineDash(phase: 0, lengths: [])
            ctx.move(to: CGPoint(x: tx, y: 0))
            ctx.addLine(to: CGPoint(x: tx, y: rect.height))
            ctx.strokePath()
        }

        // Label RX freq at top
        let attr: [NSAttributedString.Key: Any] = [
            .font: UIFont.monospacedSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: UIColor.green
        ]
        ("\(Int(rxFreq))Hz" as NSString).draw(at: CGPoint(x: rx + 2, y: 2), withAttributes: attr)
    }
}

// MARK: - Period Line Overlay

/// Draws full-width bright yellow horizontal lines at each UTC T/R period boundary,
/// labelled HH:MM:SS — matching the WSJT-X desktop waterfall.
///
/// Lines are synced to actual UTC clock boundaries (not row count) so they are
/// always correct regardless of when audio started.
final class PeriodLineOverlay: UIView {

    /// T/R period length in seconds (set via WaterfallUIView.periodSeconds).
    var periodSeconds: Int = 15
    var rowHeight:     Int = 2

    // Each entry: (y offset from top in pixels, UTC label)
    private var lines: [(y: CGFloat, label: String)] = []
    // The UTC period index when the last period boundary was crossed.
    private var lastPeriodIndex: Int = -1

    private let utcFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        f.timeZone   = TimeZone(identifier: "UTC")
        return f
    }()

    override init(frame: CGRect) { super.init(frame: frame); backgroundColor = .clear }
    required init?(coder: NSCoder) { super.init(coder: coder) }

    /// Call once per FFT row pushed to the waterfall.
    func addRow() {
        // Use a fallback height so lines aren't purged before the view is laid out.
        let viewH = bounds.height > 0 ? bounds.height : 300

        // Scroll existing lines down by one row
        lines = lines.compactMap { entry in
            let newY = entry.y + CGFloat(rowHeight)
            // Keep line until it has fully scrolled past the bottom (+20 px grace)
            return newY < viewH + 20 ? (y: newY, label: entry.label) : nil
        }

        // UTC period boundary detection — independent of row count / startup timing
        let now = Int(Date().timeIntervalSince1970)
        let periodIdx = periodSeconds > 0 ? now / periodSeconds : 0
        if periodIdx != lastPeriodIndex {
            if lastPeriodIndex != -1 {
                // A new period has started — insert a line at the top
                let ts = utcFormatter.string(
                    from: Date(timeIntervalSince1970: Double(periodIdx * periodSeconds)))
                lines.append((y: 0, label: ts))
            }
            lastPeriodIndex = periodIdx
        }

        setNeedsDisplay()
    }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        let labelAttr: [NSAttributedString.Key: Any] = [
            .font:            UIFont.monospacedSystemFont(ofSize: 9, weight: .semibold),
            .foregroundColor: UIColor.yellow
        ]

        for line in lines {
            let y = line.y

            // Solid bright yellow line (2 px) for maximum visibility
            ctx.setStrokeColor(UIColor.yellow.withAlphaComponent(0.85).cgColor)
            ctx.setLineWidth(2)
            ctx.setLineDash(phase: 0, lengths: [])
            ctx.move(to: CGPoint(x: 0, y: y))
            ctx.addLine(to: CGPoint(x: rect.width, y: y))
            ctx.strokePath()

            // UTC label at right edge
            let labelSize = (line.label as NSString).size(withAttributes: labelAttr)
            (line.label as NSString).draw(
                at: CGPoint(x: rect.width - labelSize.width - 4, y: y + 2),
                withAttributes: labelAttr
            )
        }
    }
}

// MARK: - Frequency Axis Overlay

final class FrequencyAxisOverlay: UIView {
    var fLow:  Float = 200
    var fHigh: Float = 3000

    override init(frame: CGRect) { super.init(frame: frame); backgroundColor = .clear }
    required init?(coder: NSCoder) { super.init(coder: coder) }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        let tickFreqs: [Float] = stride(from: 500, through: 3000, by: 500).map { $0 }

        let attr: [NSAttributedString.Key: Any] = [
            .font: UIFont.monospacedSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: UIColor.lightGray
        ]

        for f in tickFreqs where f >= fLow && f <= fHigh {
            let x = CGFloat((f - fLow) / (fHigh - fLow)) * rect.width
            ctx.setStrokeColor(UIColor.gray.cgColor)
            ctx.setLineWidth(0.5)
            ctx.move(to: CGPoint(x: x, y: 0))
            ctx.addLine(to: CGPoint(x: x, y: 4))
            ctx.strokePath()

            let label = f >= 1000 ? String(format: "%.1fk", f / 1000) : "\(Int(f))"
            (label as NSString).draw(
                at: CGPoint(x: x - 10, y: 5),
                withAttributes: attr
            )
        }
    }
}
