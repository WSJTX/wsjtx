package com.wsjtx.android.models

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

// ─────────────────────────────────────────────────────────────────
// Q65 Sub-mode
// ─────────────────────────────────────────────────────────────────

enum class Q65SubMode(val label: String, val nspsMultiplier: Int) {
    A("A", 1), B("B", 2), C("C", 4), D("D", 8), E("E", 16);

    fun toneSeparation(period: Q65Period): Double? {
        val nsps = period.nspsAt12k * nspsMultiplier
        val totalSec = nsps.toDouble() * 85 / 12_000.0
        return if (totalSec <= period.seconds) 12_000.0 / nsps else null
    }
}

// ─────────────────────────────────────────────────────────────────
// Q65 Period
// ─────────────────────────────────────────────────────────────────

enum class Q65Period(val seconds: Int, val nspsAt12k: Int, val hint: String) {
    P15 (15,   1_800,  "2m tropo/MS"),
    P30 (30,   3_600,  "70cm"),
    P60 (60,   6_912,  "23cm / 70cm EME"),
    P120(120, 13_824,  "23cm EME (deep)"),
    P300(300, 27_648,  "6cm+ EME");

    val label: String get() = "${seconds}s"
    val baseToneSeparation: Double get() = 12_000.0 / nspsAt12k
}

// ─────────────────────────────────────────────────────────────────
// TX Slot
// ─────────────────────────────────────────────────────────────────

enum class TxSlot(val label: String) {
    FIRST("1st"), SECOND("2nd");

    fun isMyTurn(cycleSeconds: Int): Boolean {
        val t    = System.currentTimeMillis() / 1_000L
        val slot = (t / cycleSeconds) % 2
        return if (this == FIRST) slot == 0L else slot == 1L
    }
}

// ─────────────────────────────────────────────────────────────────
// Operating Config (all modes)
// ─────────────────────────────────────────────────────────────────

class OperatingConfig {
    val rxFreq     = MutableStateFlow(1_000)
    val txFreq     = MutableStateFlow(1_000)
    val freqLocked = MutableStateFlow(true)
    val txSlot     = MutableStateFlow(TxSlot.FIRST)

    fun setRxFreq(hz: Int) {
        rxFreq.value = hz
        if (freqLocked.value) txFreq.value = hz
    }
    fun setTxFreq(hz: Int) {
        txFreq.value = hz
        if (freqLocked.value) rxFreq.value = hz
    }
}

// ─────────────────────────────────────────────────────────────────
// Q65 Config
// ─────────────────────────────────────────────────────────────────

class Q65Config {
    val subMode = MutableStateFlow(Q65SubMode.A)
    val period  = MutableStateFlow(Q65Period.P60)

    val modeLabel: String get() = "Q65-${period.value.seconds}${subMode.value.label}"

    val effectiveToneSeparation: Double
        get() = subMode.value.toneSeparation(period.value) ?: period.value.baseToneSeparation

    val subModeWarning: String?
        get() = if (subMode.value.toneSeparation(period.value) == null)
            "Sub-mode ${subMode.value.label} too narrow for ${period.value.seconds}s; using A"
        else null
}
