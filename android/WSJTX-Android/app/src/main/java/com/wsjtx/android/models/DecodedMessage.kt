package com.wsjtx.android.models

import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

// ─────────────────────────────────────────────────────────────────
// Decoded Message
// ─────────────────────────────────────────────────────────────────

data class DecodedMessage(
    val id:             Long   = System.nanoTime(),
    val utcTime:        String,          // "HHmm"
    val snr:            Int,             // dB
    val dt:             Double,          // time offset (s)
    val frequency:      Int,             // audio Hz
    val mode:           RadioMode,
    val text:           String,
    val callsignA:      String? = null,
    val callsignB:      String? = null,
    val grid:           String? = null,
    val report:         Int?    = null,
    val isDirectedToMe: Boolean = false,
    val isCQ:           Boolean = false
)

// ─────────────────────────────────────────────────────────────────
// Message Parser
// ─────────────────────────────────────────────────────────────────

object MessageParser {
    private val UTC_FMT = DateTimeFormatter.ofPattern("HHmm").withZone(ZoneOffset.UTC)

    fun parse(raw: String, snr: Int, dt: Double, freq: Int,
              mode: RadioMode, myCall: String): DecodedMessage {
        val text  = raw.trim()
        val parts = text.split(Regex("\\s+"))

        var callA: String? = null; var callB: String? = null
        var grid: String? = null;  var report: Int? = null
        var isCQ = false

        if (parts.firstOrNull()?.uppercase() == "CQ") {
            isCQ  = true
            callA = parts.getOrNull(1)
            val last = parts.lastOrNull() ?: ""
            if (isGrid(last)) grid = last else callB = last
        } else if (parts.size >= 2) {
            callB = parts[0]; callA = parts[1]
            parts.getOrNull(2)?.let { p ->
                when {
                    isGrid(p)    -> grid   = p
                    p.toIntOrNull() != null -> report = p.toInt()
                }
            }
        }

        val utc = UTC_FMT.format(Instant.now())
        return DecodedMessage(
            utcTime        = utc,
            snr            = snr,
            dt             = dt,
            frequency      = freq,
            mode           = mode,
            text           = text,
            callsignA      = callA,
            callsignB      = callB,
            grid           = grid,
            report         = report,
            isDirectedToMe = callB?.uppercase() == myCall.uppercase(),
            isCQ           = isCQ
        )
    }

    private fun isGrid(s: String): Boolean {
        val u = s.uppercase()
        return u.length >= 4 && u[0].isLetter() && u[1].isLetter() &&
               u[2].isDigit()  && u[3].isDigit()
    }
}
