import Accelerate
import Foundation

// MARK: - FFT Processor

/// Computes short-time FFT magnitude spectra for the waterfall display
/// and feeds spectral frames into the decoder.
public final class FFTProcessor {

    // MARK: Types
    public struct Frame {
        public let bins: [Float]        // dB magnitude, length == fftSize/2
        public let centerFreqs: [Float] // Hz for each bin
        public let sampleRate: Double
    }

    // MARK: Configuration
    public let fftSize: Int
    public let overlap: Int
    public let sampleRate: Double

    public var onFrame: ((Frame) -> Void)?

    // MARK: Private
    private var fftSetup: FFTSetup
    private let log2n: vDSP_Length
    private var window: [Float]
    private var accumulator: [Float] = []
    private let freqs: [Float]
    // Normalisation: 2/(fftSize) gives 0 dB for full-scale input sine
    private let normScale: Float

    public init(fftSize: Int = 2048, overlap: Int = 1024, sampleRate: Double = 12_000) {
        self.fftSize    = fftSize
        self.overlap    = overlap
        self.sampleRate = sampleRate
        self.log2n      = vDSP_Length(log2(Double(fftSize)))
        self.fftSetup   = vDSP_create_fftsetup(log2n, FFTRadix(kFFTRadix2))!
        self.normScale  = 2.0 / Float(fftSize)

        // Hann window
        var w = [Float](repeating: 0, count: fftSize)
        vDSP_hann_window(&w, vDSP_Length(fftSize), Int32(vDSP_HANN_NORM))
        self.window = w

        // Pre-compute frequency axis (Hz per bin)
        let binHz = Float(sampleRate) / Float(fftSize)
        self.freqs = (0 ..< fftSize / 2).map { Float($0) * binHz }
    }

    deinit { vDSP_destroy_fftsetup(fftSetup) }

    // MARK: - Public API

    public func process(samples: [Float]) {
        accumulator.append(contentsOf: samples)
        let hop = fftSize - overlap
        while accumulator.count >= fftSize {
            let slice = Array(accumulator.prefix(fftSize))
            accumulator.removeFirst(hop)
            processFrame(slice)
        }
    }

    // MARK: - Private

    private func processFrame(_ samples: [Float]) {
        // Apply Hann window
        var windowed = [Float](repeating: 0, count: fftSize)
        vDSP_vmul(samples, 1, window, 1, &windowed, 1, vDSP_Length(fftSize))

        // Pack real data into split complex for vDSP_fft_zrip.
        // Stride 2 is the canonical Apple pattern: treats the float array as
        // interleaved complex by taking pairs (even→real, odd→imaginary).
        realPart.withUnsafeMutableBufferPointer { rp in
            imagPart.withUnsafeMutableBufferPointer { ip in
                var sc = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                windowed.withUnsafeBytes { raw in
                    vDSP_ctoz(raw.bindMemory(to: DSPComplex.self).baseAddress!, 2,
                              &sc, 1, vDSP_Length(fftSize / 2))
                }
                vDSP_fft_zrip(fftSetup, &sc, 1, log2n, FFTDirection(FFT_FORWARD))
                vDSP_zvabs(&sc, 1, &magnitudes, 1, vDSP_Length(fftSize / 2))
            }
        }

        // Normalise to 0 dB for a full-scale sine, then convert to dB
        vDSP_vsmul(magnitudes, 1, [normScale], &magnitudes, 1, vDSP_Length(fftSize / 2))
        var dbBins = [Float](repeating: 0, count: fftSize / 2)
        var eps: Float = 1e-10
        vDSP_vsadd(magnitudes, 1, &eps, &dbBins, 1, vDSP_Length(fftSize / 2))
        // vDSP_vdbcon flag 0: out[i] = B * 20 * log10(in[i]), B=1 → 20*log10
        vDSP_vdbcon(dbBins, 1, [Float(1)], &dbBins, 1, vDSP_Length(fftSize / 2), 0)

        let frame = Frame(bins: dbBins, centerFreqs: freqs, sampleRate: sampleRate)
        onFrame?(frame)
    }

    // Pre-allocated work buffers (avoids per-frame heap allocation)
    private lazy var realPart   = [Float](repeating: 0, count: fftSize / 2)
    private lazy var imagPart   = [Float](repeating: 0, count: fftSize / 2)
    private lazy var magnitudes = [Float](repeating: 0, count: fftSize / 2)
}

// MARK: - Waterfall Data

/// Accumulates FFT frames and delivers them to the waterfall UI via a direct
/// callback (not @Published) so no frames are dropped when SwiftUI coalesces
/// updates.
public final class WaterfallData: ObservableObject {
    public let maxRows: Int
    public let binCount: Int

    // Snapshot used only for initial layout; the waterfall UIView is driven
    // by onRow directly so it never misses a frame.
    @Published public private(set) var freqAxis: [Float] = []

    // Direct per-frame callback → WaterfallUIView.pushRow
    public var onRow: (([Float], [Float]) -> Void)?

    private let fft: FFTProcessor

    public init(maxRows: Int = 300, fftSize: Int = 2048, sampleRate: Double = 12_000) {
        self.maxRows  = maxRows
        self.binCount = fftSize / 2
        self.fft      = FFTProcessor(fftSize: fftSize, sampleRate: sampleRate)

        fft.onFrame = { [weak self] frame in
            guard let self else { return }
            if self.freqAxis.isEmpty {
                DispatchQueue.main.async { self.freqAxis = frame.centerFreqs }
            }
            // Deliver directly on the calling (audio) thread; WaterfallUIView
            // marshals to the main thread internally.
            self.onRow?(frame.bins, frame.centerFreqs)
        }
    }

    public func ingest(samples: [Float]) {
        fft.process(samples: samples)
    }
}
