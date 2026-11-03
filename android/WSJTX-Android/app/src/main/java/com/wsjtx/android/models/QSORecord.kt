package com.wsjtx.android.models

import java.io.File
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

// ─────────────────────────────────────────────────────────────────
// QSO Record
// ─────────────────────────────────────────────────────────────────

data class QSORecord(
    val id:         Long   = System.nanoTime(),
    val myCall:     String,
    val dxCall:     String,
    val frequencyHz: Double,
    val mode:       RadioMode,
    val grid:       String = "",
    val rstSent:    String = "+00",
    val rstRecv:    String = "+00",
    val timestamp:  Instant = Instant.now(),
    val notes:      String = ""
) {
    private val DATE_FMT = DateTimeFormatter.ofPattern("yyyyMMdd").withZone(ZoneOffset.UTC)
    private val TIME_FMT = DateTimeFormatter.ofPattern("HHmm").withZone(ZoneOffset.UTC)

    val frequencyMHz: String get() = "%.4f".format(frequencyHz / 1_000_000)
    val dateString:   String get() = DATE_FMT.format(timestamp)
    val timeString:   String get() = TIME_FMT.format(timestamp)
    val bandTag: String get() = when {
        frequencyHz in 1_800_000.0..2_000_000.0  -> "160m"
        frequencyHz in 3_500_000.0..4_000_000.0  -> "80m"
        frequencyHz in 7_000_000.0..7_300_000.0  -> "40m"
        frequencyHz in 10_100_000.0..10_150_000.0 -> "30m"
        frequencyHz in 14_000_000.0..14_350_000.0 -> "20m"
        frequencyHz in 21_000_000.0..21_450_000.0 -> "15m"
        frequencyHz in 28_000_000.0..29_700_000.0 -> "10m"
        frequencyHz in 50_000_000.0..54_000_000.0 -> "6m"
        frequencyHz in 144_000_000.0..148_000_000.0 -> "2m"
        frequencyHz in 420_000_000.0..450_000_000.0 -> "70cm"
        frequencyHz in 1_240_000_000.0..1_300_000_000.0 -> "23cm"
        else -> "?"
    }
}

// ─────────────────────────────────────────────────────────────────
// ADIF Exporter
// ─────────────────────────────────────────────────────────────────

object ADIFExporter {
    private val HEADER = """
WSJT-X Android Log
<ADIF_VER:5>3.1.4
<PROGRAMID:8>WSJTX-And
<EOH>

""".trimIndent() + "\n"

    fun append(record: QSORecord, file: File) {
        if (!file.exists()) file.writeText(HEADER)
        file.appendText(adifLine(record) + "\n")
    }

    fun export(records: List<QSORecord>): String =
        HEADER + records.joinToString("\n") { adifLine(it) }

    private fun field(tag: String, value: String) = "<$tag:${value.length}>$value"

    private fun adifLine(r: QSORecord): String = listOf(
        field("CALL",              r.dxCall),
        field("STATION_CALLSIGN",  r.myCall),
        field("QSO_DATE",          r.dateString),
        field("TIME_ON",           r.timeString),
        field("BAND",              r.bandTag),
        field("FREQ",              r.frequencyMHz),
        field("MODE",              r.mode.label),
        field("GRIDSQUARE",        r.grid),
        field("RST_SENT",          r.rstSent),
        field("RST_RCVD",          r.rstRecv),
        if (r.notes.isNotEmpty()) field("COMMENT", r.notes) else "",
        "<EOR>"
    ).filter { it.isNotEmpty() }.joinToString(" ")
}
