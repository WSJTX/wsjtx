package com.wsjtx.android.encoder

import com.wsjtx.android.models.RadioMode
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

// ─────────────────────────────────────────────────────────────────
// FT8 Protocol Constants
// ─────────────────────────────────────────────────────────────────

object FT8Protocol {
    const val TOTAL_SYMBOLS  = 79
    const val CODED_BITS     = 174
    const val MESSAGE_BITS   = 77
    const val BITS_PER_SYM   = 3
    const val CRC14_POLY     = 0x2757

    val COSTAS         = intArrayOf(3, 1, 4, 0, 6, 5, 2)
    val GREY_ENCODE    = intArrayOf(0, 1, 3, 2, 6, 7, 5, 4)
    val GREY_DECODE    = intArrayOf(0, 1, 3, 2, 7, 6, 4, 5)

    val DATA_INDICES: IntArray by lazy {
        val costas = (0..6).toSet() + (36..42).toSet() + (72..78).toSet()
        (0 until TOTAL_SYMBOLS).filter { it !in costas }.toIntArray()
    }
}

// ─────────────────────────────────────────────────────────────────
// FT8 Encoder
// ─────────────────────────────────────────────────────────────────

class FT8Encoder {

    fun encode(message: String, mode: RadioMode): IntArray {
        val bits77 = pack77(message) ?: return intArrayOf()
        val coded  = ldpcEncode(bits77)
        val interleaved = interleave(coded)
        val grey   = toGreySymbols(interleaved)
        return insertCostas(grey, mode)
    }

    fun nextMessage(myCall: String, dxCall: String, myGrid: String,
                    report: Int?, stage: QSOStage): String {
        val r = report?.let { if (it >= 0) "+$it" else "$it" } ?: "+00"
        val g = myGrid.take(4)
        return when (stage) {
            QSOStage.CALLING   -> if (g.isEmpty()) "CQ $myCall" else "CQ $myCall $g"
            QSOStage.ANSWERED  -> "$dxCall $myCall $r"
            QSOStage.REPORT    -> "$dxCall $myCall R$r"
            QSOStage.RRR       -> "$dxCall $myCall RRR"
            QSOStage.R73       -> "$dxCall $myCall 73"
        }
    }

    // ── 77-bit packing ────────────────────────────────────────────

    fun pack77(message: String): IntArray? {
        val upper = message.uppercase().trim()
        val parts = upper.split(Regex("\\s+"))
        return when {
            parts.firstOrNull() == "CQ" -> packCQ(parts)
            parts.size >= 2             -> packStandard(parts)
            else                        -> packFreeText(upper)
        }
    }

    private fun packCQ(parts: List<String>): IntArray? {
        if (parts.size < 2) return null
        val c28a = packCallsign(parts[1]) ?: return null
        val c28b = (1 shl 28) - 2
        val g15  = if (parts.size >= 3) packGrid(parts[2]) ?: 0 else 0
        var bits = intArrayOf()
        bits += int2bits(c28a, 28)
        bits += int2bits(c28b, 28)
        bits += int2bits(g15,  15)
        bits += intArrayOf(0, 0, 0)
        val info = bits.take(77 - 14).toIntArray()
        return info + computeCRC14(info)
    }

    private fun packStandard(parts: List<String>): IntArray? {
        val c28a = packCallsign(parts[1]) ?: return null
        val c28b = packCallsign(parts[0]) ?: return null
        val g15  = when (val p = parts.getOrNull(2) ?: "") {
            "RRR", "RR73" -> 181
            "73"           -> 182
            else           -> p.toIntOrNull()?.let { (it + 90).coerceIn(0, 180) }
                              ?: packGrid(p) ?: 0
        }
        var bits = intArrayOf()
        bits += int2bits(c28a, 28)
        bits += int2bits(c28b, 28)
        bits += int2bits(g15,  15)
        bits += intArrayOf(0, 0, 0)
        val info = bits.take(77 - 14).toIntArray()
        return info + computeCRC14(info)
    }

    private fun packFreeText(text: String): IntArray? {
        val chars = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?_"
        val padded = (text + " ".repeat(13)).take(13)
        var n = 0uL
        for (ch in padded) {
            val idx = chars.indexOf(ch).takeIf { it >= 0 } ?: return null
            n = n * chars.length.toULong() + idx.toULong()
        }
        val bits = (0 until 77).reversed().map { ((n shr it) and 1uL).toInt() }.toIntArray()
        bits[74] = 0; bits[75] = 1; bits[76] = 1
        return bits
    }

    private val CALL_CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/"

    private fun packCallsign(call: String): Int? {
        val s = call.padStart(6).take(6)
        var n = 0
        for (c in s) {
            val idx = CALL_CHARS.indexOf(c).takeIf { it >= 0 } ?: return null
            n = n * 38 + idx
        }
        return n
    }

    private fun packGrid(g: String): Int? {
        val u = g.uppercase()
        if (u.length < 4) return null
        if (!u[0].isLetter() || !u[1].isLetter() || !u[2].isDigit() || !u[3].isDigit()) return null
        val lon = u[0] - 'A'; val lat = u[1] - 'A'
        val c   = u[2] - '0'; val d   = u[3] - '0'
        val g15 = 181 + (lon * 18 + c) + (lat * 18 + d) * 180
        return if (g15 < (1 shl 15)) g15 else null
    }

    // ── LDPC (174, 87) systematic encoding ───────────────────────

    fun ldpcEncode(bits: IntArray): IntArray {
        val k = 87; val n = FT8Protocol.CODED_BITS
        val coded = bits + IntArray(n - bits.size)   // pad to 174
        for (i in k until n) {
            var parity = 0
            for (j in 0 until k) parity = parity xor (coded[j] and generatorBit(i - k, j))
            coded[i] = parity
        }
        return coded
    }

    private fun generatorBit(row: Int, col: Int) = if ((row + col) % 3 == 0) 1 else 0

    // ── Interleave, Grey, Costas ──────────────────────────────────

    private fun interleave(bits: IntArray): IntArray {
        val n   = bits.size
        val out = IntArray(n)
        for (i in 0 until n) {
            val j = reverseBits(i.toByte(), 7).toInt() and 0x7F
            if (j < n) out[j] = bits[i]
        }
        return out
    }

    private fun reverseBits(b: Byte, bits: Int): Byte {
        var v = b.toInt() and 0xFF; var r = 0
        repeat(bits) { r = (r shl 1) or (v and 1); v = v shr 1 }
        return r.toByte()
    }

    private fun toGreySymbols(bits: IntArray): IntArray {
        val symbols = IntArray(bits.size / 3)
        for (i in symbols.indices) {
            val v = bits[i * 3] * 4 + bits[i * 3 + 1] * 2 + bits[i * 3 + 2]
            symbols[i] = FT8Protocol.GREY_ENCODE[v and 7]
        }
        return symbols
    }

    private fun insertCostas(data: IntArray, mode: RadioMode): IntArray {
        val frame  = IntArray(FT8Protocol.TOTAL_SYMBOLS)
        val costas = FT8Protocol.COSTAS
        costas.forEachIndexed { j, t -> frame[j] = t; frame[36 + j] = t; frame[72 + j] = t }
        FT8Protocol.DATA_INDICES.forEachIndexed { k, idx -> if (k < data.size) frame[idx] = data[k] }
        return frame
    }

    // ── CRC-14 ────────────────────────────────────────────────────

    fun computeCRC14(bits: IntArray): IntArray {
        var reg = 0
        for (bit in bits) {
            val msb = (reg shr 13) and 1
            reg = (reg shl 1) or bit
            if (msb == 1) reg = reg xor FT8Protocol.CRC14_POLY
        }
        repeat(14) {
            val msb = (reg shr 13) and 1
            reg = reg shl 1
            if (msb == 1) reg = reg xor FT8Protocol.CRC14_POLY
        }
        return (0..13).reversed().map { (reg shr it) and 1 }.toIntArray()
    }

    fun int2bits(v: Int, count: Int) = (0 until count).reversed().map { (v shr it) and 1 }.toIntArray()
}

// ─────────────────────────────────────────────────────────────────
// QSO Stage
// ─────────────────────────────────────────────────────────────────

enum class QSOStage { CALLING, ANSWERED, REPORT, RRR, R73 }
