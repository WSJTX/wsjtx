package com.wsjtx.android.encoder

import com.wsjtx.android.models.Q65SubMode

// ─────────────────────────────────────────────────────────────────
// Q65 Protocol Constants
// ─────────────────────────────────────────────────────────────────

object Q65Protocol {
    const val TOTAL_SYMBOLS = 85   // 7 Costas + 78 data
    const val DATA_SYMBOLS  = 78
    const val SYNC_SYMBOLS  = 7
    const val CODED_BITS    = DATA_SYMBOLS * 6   // 468

    /** 7-element Costas array scaled to 64-tone range */
    val COSTAS = intArrayOf(4, 2, 5, 0, 6, 1, 3).map { it * 9 }.toIntArray()

    /** 6-bit Grey encoding table */
    val GREY_ENCODE_6 = IntArray(64) { i -> i xor (i shr 1) }
    val GREY_DECODE_6: IntArray = IntArray(64).also { t -> repeat(64) { t[it xor (it shr 1)] = it } }
}

// ─────────────────────────────────────────────────────────────────
// Q65 Encoder
// ─────────────────────────────────────────────────────────────────

class Q65Encoder {

    private val ft8 = FT8Encoder()

    fun encode(message: String, subMode: Q65SubMode): IntArray {
        val bits77  = ft8.pack77(message) ?: return intArrayOf()
        val coded174 = ft8.ldpcEncode(bits77)
        val coded468 = repeat174to468(coded174)
        val interleaved = interleave468(coded468)
        val dataSymbols = toGreySymbols6(interleaved)
        return insertQ65Sync(dataSymbols)
    }

    fun nextMessage(myCall: String, dxCall: String, myGrid: String,
                    report: Int?, stage: QSOStage) =
        ft8.nextMessage(myCall, dxCall, myGrid, report, stage)

    // ── Encoding steps ────────────────────────────────────────────

    /** Rate-1/3 repetition: 174 → 468 by cycling (i % 174). */
    private fun repeat174to468(bits: IntArray): IntArray =
        IntArray(Q65Protocol.CODED_BITS) { bits[it % bits.size] }

    /** Column-major interleave over 78 × 6 bit matrix. */
    private fun interleave468(bits: IntArray): IntArray {
        val rows = Q65Protocol.DATA_SYMBOLS; val cols = 6
        val out  = IntArray(bits.size)
        for (r in 0 until rows) for (c in 0 until cols) {
            val src = r * cols + c; val dst = c * rows + r
            if (src < bits.size && dst < out.size) out[dst] = bits[src]
        }
        return out
    }

    /** Pack every 6 bits into a 6-bit Grey-coded 64-FSK tone symbol. */
    private fun toGreySymbols6(bits: IntArray): IntArray {
        val symbols = IntArray(Q65Protocol.DATA_SYMBOLS)
        for (i in symbols.indices) {
            var v = 0
            for (k in 0 until 6) v = v * 2 + bits[i * 6 + k]
            symbols[i] = Q65Protocol.GREY_ENCODE_6[v and 63]
        }
        return symbols
    }

    /** Prepend 7-symbol Costas sync block to the 78 data symbols. */
    private fun insertQ65Sync(data: IntArray): IntArray =
        Q65Protocol.COSTAS + data.take(Q65Protocol.DATA_SYMBOLS).toIntArray()
}
