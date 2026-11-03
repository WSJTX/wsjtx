package com.wsjtx.android.dsp

import kotlin.math.*

// ─────────────────────────────────────────────────────────────────
// Pure-Kotlin Cooley-Tukey FFT with Hann window
// ─────────────────────────────────────────────────────────────────

class FFTProcessor(
    val fftSize: Int    = 2048,
    val overlap: Int    = 1024,
    val sampleRate: Double = 12_000.0,
    val onFrame: ((FloatArray, FloatArray) -> Unit)? = null   // (dBBins, freqAxis)
) {
    private val log2n   = log2(fftSize.toDouble()).toInt()
    private val window  = FloatArray(fftSize) { i -> hann(i, fftSize) }
    private val freqAxis = FloatArray(fftSize / 2) { i -> (i * sampleRate / fftSize).toFloat() }
    private val normScale = 2f / fftSize

    // Work buffers reused per frame
    private val re = FloatArray(fftSize)
    private val im = FloatArray(fftSize)
    private val mag = FloatArray(fftSize / 2)
    private val accumulator = ArrayDeque<Float>()

    fun process(samples: FloatArray) {
        samples.forEach { accumulator.addLast(it) }
        val hop = fftSize - overlap
        while (accumulator.size >= fftSize) {
            val slice = FloatArray(fftSize) { accumulator[it] }
            repeat(hop) { accumulator.removeFirst() }
            processFrame(slice)
        }
    }

    private fun processFrame(samples: FloatArray) {
        // Apply Hann window
        for (i in 0 until fftSize) { re[i] = samples[i] * window[i]; im[i] = 0f }
        fft(re, im, fftSize)

        // Magnitude → dB
        val half = fftSize / 2
        for (i in 0 until half) {
            val m = sqrt(re[i] * re[i] + im[i] * im[i]) * normScale
            mag[i] = if (m > 1e-10f) (20f * log10(m.toDouble())).toFloat() else -120f
        }
        onFrame?.invoke(mag.copyOf(), freqAxis)
    }

    // In-place Cooley-Tukey FFT (radix-2, DIT)
    private fun fft(re: FloatArray, im: FloatArray, n: Int) {
        // Bit-reverse permutation
        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
            j = j xor bit
            if (i < j) { re[i] = re[j].also { re[j] = re[i] }; im[i] = im[j].also { im[j] = im[i] } }
        }
        // Butterfly passes
        var len = 2
        while (len <= n) {
            val ang = -2.0 * PI / len
            val wRe = cos(ang).toFloat(); val wIm = sin(ang).toFloat()
            var i = 0
            while (i < n) {
                var curRe = 1f; var curIm = 0f
                for (jj in 0 until len / 2) {
                    val uRe = re[i + jj];  val uIm = im[i + jj]
                    val vRe = re[i + jj + len / 2] * curRe - im[i + jj + len / 2] * curIm
                    val vIm = re[i + jj + len / 2] * curIm + im[i + jj + len / 2] * curRe
                    re[i + jj] = uRe + vRe; im[i + jj] = uIm + vIm
                    re[i + jj + len / 2] = uRe - vRe; im[i + jj + len / 2] = uIm - vIm
                    val newRe = curRe * wRe - curIm * wIm
                    curIm = curRe * wIm + curIm * wRe; curRe = newRe
                }
                i += len
            }
            len = len shl 1
        }
    }

    private fun hann(i: Int, n: Int) = (0.5 - 0.5 * cos(2.0 * PI * i / n)).toFloat()
}

// ─────────────────────────────────────────────────────────────────
// Waterfall Data — ring buffer + UTC period line tracking
// ─────────────────────────────────────────────────────────────────

class WaterfallData(
    val maxRows: Int = 300,
    fftSize: Int = 2048,
    sampleRate: Double = 12_000.0
) {
    private val processor = FFTProcessor(fftSize = fftSize, sampleRate = sampleRate)

    // Newest row first
    val rows     = ArrayDeque<FloatArray>()
    var freqAxis = floatArrayOf(); private set

    // Direct per-frame callback — set by the UI layer
    var onRow: ((FloatArray, FloatArray) -> Unit)? = null

    init {
        processor.onFrame = { bins, axis ->
            if (freqAxis.isEmpty()) freqAxis = axis
            onRow?.invoke(bins, axis)
            synchronized(rows) {
                rows.addFirst(bins)
                if (rows.size > maxRows) rows.removeLast()
            }
        }
    }

    fun ingest(samples: FloatArray) = processor.process(samples)
}
