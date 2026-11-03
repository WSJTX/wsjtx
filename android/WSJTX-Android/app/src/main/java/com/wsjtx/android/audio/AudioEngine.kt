package com.wsjtx.android.audio

import android.media.*
import com.wsjtx.android.models.RadioMode
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlin.math.*

class AudioEngine {

    // ── State ─────────────────────────────────────────────────────
    val isRunning    = MutableStateFlow(false)
    val inputLevel   = MutableStateFlow(0f)

    var onSampleBuffer: ((FloatArray, Double) -> Unit)? = null

    // ── Private ───────────────────────────────────────────────────
    private val sampleRate    = 48_000
    private val targetRate    = 12_000
    private val channelConfig = AudioFormat.CHANNEL_IN_MONO
    private val audioFormat   = AudioFormat.ENCODING_PCM_FLOAT

    private var recorder:  AudioRecord? = null
    private var trackNode: AudioTrack?  = null
    private var captureJob: Job?        = null
    @Volatile private var cancelTX      = false

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    // ── Start / Stop RX ───────────────────────────────────────────

    fun start() {
        if (isRunning.value) return
        val minBuf = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat)
        recorder = AudioRecord(
            MediaRecorder.AudioSource.MIC,
            sampleRate, channelConfig, audioFormat,
            maxOf(minBuf, sampleRate / 10 * 4)
        )
        recorder?.startRecording()
        isRunning.value = true

        captureJob = scope.launch {
            val buf = FloatArray(4096)
            while (isActive && isRunning.value) {
                val read = recorder?.read(buf, 0, buf.size, AudioRecord.READ_BLOCKING) ?: break
                if (read > 0) {
                    val slice = buf.copyOf(read)
                    val rms = rms(slice)
                    inputLevel.value = (rms * 4f).coerceIn(0f, 1f).takeIf { it.isFinite() } ?: 0f
                    val down = downsample(slice, sampleRate, targetRate)
                    onSampleBuffer?.invoke(down, targetRate.toDouble())
                }
            }
        }
    }

    fun stop() {
        isRunning.value = false
        captureJob?.cancel()
        recorder?.stop()
        recorder?.release()
        recorder = null
    }

    fun stopTransmit() {
        cancelTX = true
        scope.launch { trackNode?.stop() }
    }

    // ── TX ────────────────────────────────────────────────────────

    fun transmit(
        symbols: IntArray,
        mode: RadioMode,
        baseFreqHz: Double = 1_000.0,
        toneSepOverride: Double? = null,
        onComplete: () -> Unit = {}
    ) {
        if (symbols.isEmpty()) { onComplete(); return }
        cancelTX = false

        scope.launch {
            val txRate   = sampleRate
            val toneSep  = toneSepOverride ?: mode.toneSeparation
            val symLen   = (txRate / toneSep).toInt()
            val total    = symbols.size * symLen
            val wave     = FloatArray(total)
            var phase    = 0.0

            for ((i, sym) in symbols.withIndex()) {
                if (cancelTX) { onComplete(); return@launch }
                val freq  = baseFreqHz + sym * toneSep
                val start = i * symLen
                for (j in 0 until symLen) {
                    wave[start + j] = sin(2.0 * PI * freq * j / txRate + phase).toFloat()
                }
                phase += 2.0 * PI * freq * symLen / txRate
            }

            // Raised-cosine ramp
            val rampLen = maxOf(1, (txRate * 0.008).toInt())
            for (i in 0 until minOf(rampLen, wave.size)) {
                val env = (0.5 - 0.5 * cos(PI * i / rampLen)).toFloat()
                wave[i] *= env
                wave[wave.size - 1 - i] *= env
            }

            val track = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setSampleRate(txRate)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .build()
                )
                .setBufferSizeInBytes(total * 4)
                .setTransferMode(AudioTrack.MODE_STATIC)
                .build()
            trackNode = track

            track.write(wave, 0, wave.size, AudioTrack.WRITE_BLOCKING)
            if (!cancelTX) {
                track.play()
                // Wait for playback to finish
                while (track.playState == AudioTrack.PLAYSTATE_PLAYING) {
                    if (cancelTX) break
                    delay(50)
                }
            }
            track.stop()
            track.release()
            trackNode = null
            onComplete()
        }
    }

    // ── DSP helpers ───────────────────────────────────────────────

    private fun downsample(samples: FloatArray, inRate: Int, outRate: Int): FloatArray {
        val ratio = inRate / outRate
        if (ratio <= 1) return samples
        val filtered = firLowPass(samples, outRate.toFloat() / inRate * 0.45f, 32)
        return FloatArray(filtered.size / ratio) { filtered[it * ratio] }
    }

    private fun firLowPass(input: FloatArray, cutoff: Float, order: Int): FloatArray {
        val n      = order + 1
        val kernel = FloatArray(n)
        val M      = order.toFloat()
        for (i in 0 until n) {
            val x = i - M / 2
            kernel[i] = if (x == 0f) 2 * cutoff
                        else sin(2.0 * PI * cutoff * x).toFloat() / (PI.toFloat() * x)
            kernel[i] *= 0.54f - 0.46f * cos(2.0 * PI * i / M).toFloat()
        }
        // Pad to avoid over-read (length >= input.size + n - 1)
        val padded = input + FloatArray(n - 1)
        val out    = FloatArray(input.size)
        for (i in out.indices) {
            var acc = 0.0
            for (k in 0 until n) acc += padded[i + k].toDouble() * kernel[k]
            out[i] = acc.toFloat()
        }
        return out
    }

    private fun rms(s: FloatArray): Float {
        if (s.isEmpty()) return 0f
        var sum = 0.0
        for (v in s) sum += v * v
        val r = sqrt(sum / s.size).toFloat()
        return if (r.isFinite()) r else 0f
    }
}
