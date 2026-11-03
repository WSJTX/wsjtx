package com.wsjtx.android.decoder

import com.wsjtx.android.models.DecodedMessage
import com.wsjtx.android.models.MessageParser
import com.wsjtx.android.models.RadioMode
import kotlin.math.*

/**
 * Real-time FT8 decoder.
 *
 * Pipeline:
 *   ingest(samples) → accumulate → correlate Costas arrays in spectrogram
 *   → extract soft LLRs → hard LDPC → CRC-14 → unpack 77-bit message
 */
class FT8Decoder {

    var myCall   = ""
    var onMessage: ((DecodedMessage) -> Unit)? = null

    private val sampleRate  = 12_000.0
    private val mode        = RadioMode.FT8
    private val accumulator = ArrayDeque<Float>()
    private val periodSamples get() = (mode.cycleSeconds * sampleRate).toInt()

    // ── Ingestion ─────────────────────────────────────────────────

    fun setMyCall(call: String) { myCall = call }

    fun ingest(samples: FloatArray, rate: Double) {
        samples.forEach { accumulator.addLast(it) }
        if (accumulator.size >= periodSamples + 19_200) {   // + guard
            val chunk = FloatArray(periodSamples) { accumulator[it] }
            repeat(periodSamples) { accumulator.removeFirst() }
            decodePeriod(chunk)
        }
    }

    // ── Period decode ──────────────────────────────────────────────

    private fun decodePeriod(samples: FloatArray) {
        val symLen = (sampleRate / mode.toneSeparation).toInt()
        val spec   = buildSpectrogram(samples, symLen)

        // Costas scan over coarse dt / freq grid
        val maxDtSamples = sampleRate.toInt()
        val step         = symLen / 4
        val candidates   = mutableListOf<Triple<Int, Int, Float>>()

        for (dtOff in -maxDtSamples..maxDtSamples step step) {
            for (fb in 0..(spec.firstOrNull()?.size ?: 0) - 8) {
                val score = costasScore(spec, dtOff / step, fb)
                if (score > 10f) candidates += Triple(dtOff, fb, score)
            }
        }

        deduplicateCandidates(candidates).forEach { (dt, fb, snr) ->
            tryDecode(samples, spec, dt, fb, snr)?.let { msg ->
                onMessage?.invoke(msg)
            }
        }
    }

    // ── Spectrogram ───────────────────────────────────────────────

    private fun buildSpectrogram(samples: FloatArray, symLen: Int): List<FloatArray> {
        val rows = mutableListOf<FloatArray>()
        var s = 0
        while (s + symLen <= samples.size) {
            rows += fftMag(samples.copyOfRange(s, s + symLen), symLen, 8 * 4)
            s += symLen
        }
        return rows
    }

    private fun fftMag(samples: FloatArray, size: Int, nBins: Int): FloatArray {
        val re  = FloatArray(size) { samples.getOrElse(it) { 0f } }
        val im  = FloatArray(size)
        cooleyTukey(re, im, size)
        val mag = FloatArray(nBins)
        for (i in 0 until nBins) mag[i] = sqrt(re[i] * re[i] + im[i] * im[i]) / size
        return mag
    }

    private fun cooleyTukey(re: FloatArray, im: FloatArray, n: Int) {
        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
            j = j xor bit
            if (i < j) { re[i] = re[j].also { re[j] = re[i] }; im[i] = im[j].also { im[j] = im[i] } }
        }
        var len = 2
        while (len <= n) {
            val ang = -2.0 * PI / len
            val wRe = cos(ang).toFloat(); val wIm = sin(ang).toFloat()
            var i = 0
            while (i < n) {
                var cRe = 1f; var cIm = 0f
                for (jj in 0 until len / 2) {
                    val uRe = re[i + jj]; val uIm = im[i + jj]
                    val vRe = re[i + jj + len / 2] * cRe - im[i + jj + len / 2] * cIm
                    val vIm = re[i + jj + len / 2] * cIm + im[i + jj + len / 2] * cRe
                    re[i + jj] = uRe + vRe; im[i + jj] = uIm + vIm
                    re[i + jj + len / 2] = uRe - vRe; im[i + jj + len / 2] = uIm - vIm
                    val nr = cRe * wRe - cIm * wIm; cIm = cRe * wIm + cIm * wRe; cRe = nr
                }
                i += len
            }
            len = len shl 1
        }
    }

    // ── Costas correlation ────────────────────────────────────────

    private val COSTAS = intArrayOf(3, 1, 4, 0, 6, 5, 2)

    private fun costasScore(spec: List<FloatArray>, dtOff: Int, fb: Int): Float {
        var score = 0f
        for (pos in intArrayOf(0, 36, 72)) {
            for ((j, t) in COSTAS.withIndex()) {
                val row = pos + j + dtOff
                if (row < 0 || row >= spec.size) continue
                val bins = spec[row]
                val f = fb + t
                if (f >= bins.size) continue
                score += bins[f]
                if (f > 0) score -= bins[f - 1] * 0.5f
                if (f + 1 < bins.size) score -= bins[f + 1] * 0.5f
            }
        }
        return score
    }

    private fun deduplicateCandidates(
        cands: List<Triple<Int, Int, Float>>
    ): List<Triple<Int, Int, Float>> {
        val kept = mutableListOf<Triple<Int, Int, Float>>()
        for (c in cands.sortedByDescending { it.third }) {
            if (kept.none { abs(it.first - c.first) < 2 && abs(it.second - c.second) < 2 })
                kept += c
        }
        return kept
    }

    // ── Symbol extraction + hard LDPC ────────────────────────────

    private fun tryDecode(
        samples: FloatArray, spec: List<FloatArray>,
        dtOff: Int, fb: Int, snr: Float
    ): DecodedMessage? {
        val symLen = (sampleRate / mode.toneSeparation).toInt()
        val DATA_INDICES = buildList {
            val costas = (0..6).toSet() + (36..42).toSet() + (72..78).toSet()
            (0 until 79).forEach { if (it !in costas) add(it) }
        }
        val llrs = FloatArray(174)
        for ((bi, si) in DATA_INDICES.withIndex()) {
            val row = si + dtOff / symLen
            if (row < 0 || row >= spec.size) continue
            val bins = spec[row]
            for (bit in 0 until 3) {
                var s0 = 0f; var s1 = 0f
                val GREY = intArrayOf(0, 1, 3, 2, 6, 7, 5, 4)
                for (tone in 0 until 8) {
                    val mag = bins.getOrElse(fb + tone) { 0f }
                    val mask = 1 shl (2 - bit)
                    if (GREY[tone] and mask != 0) s1 += mag else s0 += mag
                }
                val idx = bi * 3 + bit
                if (idx < llrs.size) llrs[idx] = ln((s1 + 1e-10f) / (s0 + 1e-10f))
            }
        }

        val bits = IntArray(174) { if (llrs[it] >= 0) 1 else 0 }
        val payload = bits.take(91).toIntArray()
        if (!checkCRC14(payload)) return null

        val text = FT8MessagePacker.unpack77(bits.take(77).toIntArray()) ?: return null
        val dtSec = dtOff / sampleRate
        val freqHz = (fb * sampleRate / (symLen * 4)).toInt()
        return MessageParser.parse(text, snr.toInt(), dtSec, freqHz, mode, myCall)
    }

    private fun checkCRC14(bits: IntArray): Boolean {
        if (bits.size < 14) return false
        val data = bits.dropLast(14).toIntArray()
        val crc  = bits.takeLast(14).toIntArray()
        return computeCRC14(data).contentEquals(crc)
    }

    private fun computeCRC14(bits: IntArray): IntArray {
        var reg = 0
        for (bit in bits) {
            val msb = (reg shr 13) and 1; reg = (reg shl 1) or bit
            if (msb == 1) reg = reg xor 0x2757
        }
        repeat(14) {
            val msb = (reg shr 13) and 1; reg = reg shl 1
            if (msb == 1) reg = reg xor 0x2757
        }
        return (0..13).reversed().map { (reg shr it) and 1 }.toIntArray()
    }
}

// ─────────────────────────────────────────────────────────────────
// FT8 Message Packer / Unpacker
// ─────────────────────────────────────────────────────────────────

object FT8MessagePacker {

    fun unpack77(bits: IntArray): String? {
        if (bits.size < 77) return null
        val i3 = bits2int(bits, 74, 3)
        val text = when (i3) {
            0    -> if (bits2int(bits, 71, 3) == 0) unpackType0(bits) else unpackFreeText(bits)
            1, 2, 3, 4 -> unpackType0(bits).takeIf { it.isNotEmpty() } ?: unpackFreeText(bits)
            else -> ""
        }
        return text.trim().takeIf { it.isNotEmpty() }
    }

    private fun unpackType0(bits: IntArray): String {
        val c28a = bits2int(bits, 0,  28)
        val c28b = bits2int(bits, 28, 28)
        val g15  = bits2int(bits, 56, 15)
        val callA = unpackCallsign(c28a)
        val callB = unpackCallsign(c28b)
        val extra = unpackReport(g15)
        return listOf(callA, callB, extra).filter { it.isNotEmpty() }.joinToString(" ")
    }

    private val CALL_CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/"

    private fun unpackCallsign(n: Int): String {
        if (n == 0) return "DE"
        if (n >= (1 shl 28) - 5) return "CQ"
        if (n == (1 shl 28) - 1) return "QRZ"
        var v = n
        val chars = CharArray(6)
        for (i in 5 downTo 0) { chars[i] = CALL_CHARS[v % 38]; v /= 38 }
        return String(chars).trim()
    }

    private fun unpackReport(g15: Int): String {
        if (g15 == 0) return ""
        if (g15 <= 180) return "${g15 - 90}"
        val row = (g15 - 181) / 180; val col = (g15 - 181) % 180
        val lonChar = 'A' + col / 18; val latChar = 'A' + row / 18
        val lonDig  = '0' + (col % 18) / 2; val latDig  = '0' + (row % 18) / 2
        return "$lonChar$latChar$lonDig$latDig"
    }

    private val FREE_CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?_"

    private fun unpackFreeText(bits: IntArray): String {
        var n = 0uL
        for (b in bits.take(77)) n = n * 2u + b.toULong()
        val base = FREE_CHARS.length.toULong()
        val chars = CharArray(13)
        for (i in 12 downTo 0) { chars[i] = FREE_CHARS[(n % base).toInt()]; n /= base }
        return String(chars).trim()
    }

    private fun bits2int(bits: IntArray, from: Int, count: Int): Int {
        var v = 0
        for (i in 0 until count) v = v * 2 + (bits.getOrElse(from + i) { 0 })
        return v
    }
}
