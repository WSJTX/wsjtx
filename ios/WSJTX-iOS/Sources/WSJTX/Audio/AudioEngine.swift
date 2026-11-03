import AVFoundation
import Accelerate

// MARK: - Audio Engine

/// Manages real-time audio capture (microphone / line-in) and playback
/// using AVAudioEngine.  All heavy work happens on a dedicated serial queue
/// so the main thread is never blocked.
public final class AudioEngine: ObservableObject {

    // MARK: Public state
    @Published public private(set) var isRunning = false
    @Published public private(set) var inputLevel: Float = 0   // 0…1

    /// Called for every captured buffer.  Runs on `processingQueue`.
    public var onSampleBuffer: (([Float], Double) -> Void)?

    // MARK: Private
    private let engine      = AVAudioEngine()
    /// Serial queue used exclusively for all AVAudioPlayerNode calls.
    /// Serialising ALL playerNode access (play/scheduleBuffer/stop) onto one
    /// queue is the only way to avoid AVAudioPlayerNode's internal semaphore
    /// deadlock that occurs when stop() is called concurrently from two threads.
    private let playerQ     = DispatchQueue(label: "wsjtx.player", qos: .userInitiated)
    private let processingQ = DispatchQueue(label: "wsjtx.audio",  qos: .userInitiated)
    private var playerNode  = AVAudioPlayerNode()
    private let targetRate: Double = 12_000

    /// Set to true to abort the current synthesis + playback at the next opportunity.
    private var cancelTX = false

    // MARK: - Setup

    public init() {
        setupSession()
        setupGraph()
    }

    private func setupSession() {
        let session = AVAudioSession.sharedInstance()
        do {
            // Use .default mode (not .measurement) so the output path is fully
            // active and routes audio to the speaker / headphones at full quality.
            // .measurement is input-only optimisation and can suppress TX output.
            try session.setCategory(.playAndRecord,
                                    mode: .default,
                                    options: [.defaultToSpeaker, .allowBluetooth])
            try session.setPreferredSampleRate(48_000)
            try session.setPreferredIOBufferDuration(0.02)
            try session.setActive(true)
        } catch {
            print("[AudioEngine] Session setup error: \(error)")
        }
    }

    private func setupGraph() {
        let input = engine.inputNode
        let rxFmt = AVAudioFormat(standardFormatWithSampleRate: 48_000, channels: 1)!

        engine.attach(playerNode)
        // Use format:nil so the engine auto-selects the hardware output format.
        // The TX path queries playerNode.outputFormat(forBus:0) at transmit time
        // to guarantee the buffer format always matches the actual connection format.
        engine.connect(playerNode, to: engine.mainMixerNode, format: nil)

        // Down-sample tap: capture at 48 kHz, present 12 kHz to decoder
        input.installTap(onBus: 0, bufferSize: 4096, format: rxFmt) { [weak self] buf, _ in
            guard let self, let data = buf.floatChannelData?[0] else { return }
            let count = Int(buf.frameLength)
            let samples = Array(UnsafeBufferPointer(start: data, count: count))
            let down = self.downsample(samples, from: 48_000, to: Int(self.targetRate))
            let rms = self.rms(down)
            DispatchQueue.main.async { self.inputLevel = min(rms * 4, 1) }
            self.onSampleBuffer?(down, self.targetRate)
        }
    }

    // MARK: - Control

    public func start() {
        guard !isRunning else { return }
        do {
            try engine.start()
            isRunning = true
        } catch {
            print("[AudioEngine] Start error: \(error)")
        }
    }

    public func stop() {
        guard isRunning else { return }
        engine.stop()
        isRunning = false
    }

    public func stopTransmit() {
        // Set the cancellation flag so any in-progress synthesis bails early.
        processingQ.async { [weak self] in self?.cancelTX = true }
        // Stop the player node — serialised through playerQ to avoid the
        // semaphore deadlock: completion callback and stopTransmit() both
        // calling playerNode.stop() concurrently was the hang.
        playerQ.async { [weak self] in self?.playerNode.stop() }
    }

    // MARK: - Transmit

    /// Converts FSK symbol indices to an audio waveform and plays it.
    /// Synthesis runs on a background queue so the MainActor is never blocked.
    /// `baseFreq` sets the lowest tone's audio frequency (Hz); defaults to 1000 Hz.
    /// `completion` is called on the main thread when playback finishes.
    public func transmit(symbols: [Int], mode: RadioMode,
                          baseFreq: Double = 1_000,
                          toneSeparationOverride: Double? = nil,
                          completion: (() -> Void)? = nil) {
        guard engine.isRunning else {
            print("[AudioEngine] Engine not running — start RX first")
            return
        }
        guard !symbols.isEmpty else {
            print("[AudioEngine] Empty symbol array — encode failed")
            return
        }

        // Snapshot format on the calling thread (safe to query from any thread).
        let nodeFmt    = playerNode.outputFormat(forBus: 0)
        let sampleRate = nodeFmt.sampleRate
        guard sampleRate > 0 else {
            print("[AudioEngine] Invalid sample rate: \(sampleRate)")
            return
        }

        // Run synthesis on a background queue — never block the MainActor.
        processingQ.async { [weak self] in
            guard let self else { return }

            // Reset the cancellation flag for this new transmission.
            self.cancelTX = false

            // Use caller-supplied tone separation (e.g. Q65 period-aware) or fall back to mode default.
            let toneSep = toneSeparationOverride ?? mode.toneSeparation
            let symLen       = Int(sampleRate / toneSep)
            let totalSamples = symbols.count * symLen
            var wave  = [Float](repeating: 0, count: totalSamples)
            var phase: Double = 0

            for (i, sym) in symbols.enumerated() {
                // Check cancellation at each symbol boundary (cheap; no lock needed
                // since only processingQ writes cancelTX before we read it here).
                if self.cancelTX {
                    print("[AudioEngine] TX cancelled during synthesis")
                    completion?()
                    return
                }
                let freq  = baseFreq + Double(sym) * toneSep
                let start = i * symLen
                for j in 0 ..< symLen {
                    wave[start + j] = Float(sin(2 * .pi * freq * Double(j) / sampleRate + phase))
                }
                phase += 2 * .pi * freq * Double(symLen) / sampleRate
            }

            if self.cancelTX { completion?(); return }

            // Raised-cosine ramp (8 ms) — avoids key clicks
            let rampLen = max(1, Int(sampleRate * 0.008))
            for i in 0 ..< min(rampLen, wave.count) {
                let env = Float(0.5 * (1 - cos(.pi * Double(i) / Double(rampLen))))
                wave[i] *= env
                wave[wave.count - 1 - i] *= env
            }

            guard let buf = AVAudioPCMBuffer(pcmFormat: nodeFmt,
                                             frameCapacity: AVAudioFrameCount(totalSamples))
            else {
                print("[AudioEngine] Failed to create PCM buffer (\(totalSamples) frames)")
                return
            }
            buf.frameLength = AVAudioFrameCount(totalSamples)
            for ch in 0 ..< Int(nodeFmt.channelCount) {
                buf.floatChannelData?[ch].update(from: wave, count: totalSamples)
            }

            // All playerNode calls go through playerQ — the ONLY way to prevent
            // concurrent stop() calls from deadlocking via AVFoundation's internal
            // semaphore (CancelTimer ↔ StopImpl).
            self.playerQ.async { [weak self] in
                guard let self, !self.cancelTX else { completion?(); return }
                print("[AudioEngine] Scheduling \(totalSamples) frames @ \(Int(sampleRate)) Hz")
                if !self.playerNode.isPlaying { self.playerNode.play() }
                self.playerNode.scheduleBuffer(buf, completionCallbackType: .dataPlayedBack) { [weak self] _ in
                    // Do NOT call playerNode.stop() here — that causes the deadlock.
                    // Simply notify the caller; the player naturally becomes idle.
                    self?.cancelTX = false
                    completion?()
                }
            }
        }
    }

    // MARK: - DSP helpers

    private func downsample(_ samples: [Float], from inRate: Int, to outRate: Int) -> [Float] {
        let ratio = inRate / outRate
        guard ratio > 1 else { return samples }

        // Simple polyphase FIR decimation: first low-pass filter then pick every Nth sample.
        // For production use replace with vDSP_decimf / Apple's Accelerate polyphase filters.
        var filtered = samples
        let cutoff: Float = Float(outRate) / Float(inRate) * 0.45
        filtered = firLowPass(filtered, cutoff: cutoff, order: 32)

        var out = [Float]()
        out.reserveCapacity(samples.count / ratio)
        stride(from: 0, to: filtered.count, by: ratio).forEach { out.append(filtered[$0]) }
        return out
    }

    private func firLowPass(_ input: [Float], cutoff: Float, order: Int) -> [Float] {
        // Hamming-windowed sinc FIR low-pass filter.
        let n = order + 1   // filter length (taps)
        var kernel = [Float](repeating: 0, count: n)
        let M = Float(order)
        for i in 0 ..< n {
            let x = Float(i) - M / 2
            kernel[i] = x == 0 ? 2 * cutoff : sin(2 * .pi * cutoff * x) / (.pi * x)
            kernel[i] *= 0.54 - 0.46 * cos(2 * .pi * Float(i) / M)
        }
        // vDSP_conv requires the signal array to have at least N + P - 1 elements
        // (N = output length, P = filter length). Without this padding the function
        // reads heap memory beyond the array, which — depending on heap layout —
        // can produce NaN or infinity floats that corrupt the RMS and max-out the
        // level meter even in silence.
        let paddedInput = input + [Float](repeating: 0, count: n - 1)
        var out = [Float](repeating: 0, count: input.count)
        vDSP_conv(paddedInput, 1, kernel, 1, &out, 1, vDSP_Length(input.count), vDSP_Length(n))
        return out
    }

    private func rms(_ samples: [Float]) -> Float {
        guard !samples.isEmpty else { return 0 }
        var val: Float = 0
        vDSP_measqv(samples, 1, &val, vDSP_Length(samples.count))
        let result = sqrt(val)
        // Guard against NaN / inf that could arise from heap garbage in vDSP_conv.
        return result.isFinite ? result : 0
    }
}
