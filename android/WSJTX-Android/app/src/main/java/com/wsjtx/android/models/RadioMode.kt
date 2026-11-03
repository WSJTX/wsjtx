package com.wsjtx.android.models

// ─────────────────────────────────────────────────────────────────
// Radio Mode
// ─────────────────────────────────────────────────────────────────

enum class RadioMode(
    val label: String,
    val cycleSeconds: Double,
    val toneSeparation: Double,
    val toneCount: Int,
    val symbolCount: Int
) {
    FT8  ("FT8",  15.0,   6.25,           8,  79),
    FT4  ("FT4",   7.5,  20.833,          8, 105),
    JT65 ("JT65", 60.0,   2.692,         65, 126),
    JT9  ("JT9",  60.0,   1.736,          9,  85),
    WSPR ("WSPR",120.0,   1.465,          4, 162),
    Q65  ("Q65",  60.0,  12_000.0/6_912, 64,  85);
}

// ─────────────────────────────────────────────────────────────────
// Band
// ─────────────────────────────────────────────────────────────────

enum class Band(val label: String, val centreHz: Double) {
    M160   ("160m",    1_900_000.0),
    M80    ("80m",     3_750_000.0),
    M60    ("60m",     5_357_000.0),
    M40    ("40m",     7_150_000.0),
    M30    ("30m",    10_125_000.0),
    M20    ("20m",    14_175_000.0),
    M17    ("17m",    18_118_000.0),
    M15    ("15m",    21_225_000.0),
    M12    ("12m",    24_940_000.0),
    M10    ("10m",    28_850_000.0),
    M6     ("6m",     52_000_000.0),
    M4     ("4m",     70_200_000.0),
    M2     ("2m",    145_000_000.0),
    M70CM  ("70cm",  433_500_000.0),
    M23CM  ("23cm",1_297_000_000.0),
    M13CM  ("13cm",2_320_000_000.0),
    M9CM   ("9cm",  3_400_000_000.0),
    M6CM   ("6cm",  5_760_000_000.0),
    M3CM   ("3cm", 10_368_000_000.0),
    M1_25CM("1.25cm",24_048_000_000.0);

    fun defaultFrequency(mode: RadioMode): Double = when (Pair(this, mode)) {
        // FT8
        Pair(M160, RadioMode.FT8)  -> 1_840_000.0
        Pair(M80,  RadioMode.FT8)  -> 3_573_000.0
        Pair(M60,  RadioMode.FT8)  -> 5_357_000.0
        Pair(M40,  RadioMode.FT8)  -> 7_074_000.0
        Pair(M30,  RadioMode.FT8)  -> 10_136_000.0
        Pair(M20,  RadioMode.FT8)  -> 14_074_000.0
        Pair(M17,  RadioMode.FT8)  -> 18_100_000.0
        Pair(M15,  RadioMode.FT8)  -> 21_074_000.0
        Pair(M12,  RadioMode.FT8)  -> 24_915_000.0
        Pair(M10,  RadioMode.FT8)  -> 28_074_000.0
        Pair(M6,   RadioMode.FT8)  -> 50_313_000.0
        Pair(M4,   RadioMode.FT8)  -> 70_100_000.0
        Pair(M2,   RadioMode.FT8)  -> 144_174_000.0
        Pair(M70CM,RadioMode.FT8)  -> 432_174_000.0
        Pair(M23CM,RadioMode.FT8)  -> 1_296_174_000.0
        Pair(M13CM,RadioMode.FT8)  -> 2_320_143_000.0
        Pair(M9CM, RadioMode.FT8)  -> 3_400_100_000.0
        Pair(M6CM, RadioMode.FT8)  -> 5_760_100_000.0
        Pair(M3CM, RadioMode.FT8)  -> 10_368_100_000.0
        Pair(M1_25CM,RadioMode.FT8)-> 24_048_100_000.0
        // FT4
        Pair(M80,  RadioMode.FT4)  -> 3_575_000.0
        Pair(M40,  RadioMode.FT4)  -> 7_047_500.0
        Pair(M20,  RadioMode.FT4)  -> 14_080_000.0
        // WSPR
        Pair(M40,  RadioMode.WSPR) -> 7_038_600.0
        Pair(M30,  RadioMode.WSPR) -> 10_138_700.0
        Pair(M20,  RadioMode.WSPR) -> 14_095_600.0
        // Q65 EME
        Pair(M2,   RadioMode.Q65)  -> 144_174_000.0
        Pair(M70CM,RadioMode.Q65)  -> 432_174_000.0
        Pair(M23CM,RadioMode.Q65)  -> 1_296_174_000.0
        Pair(M13CM,RadioMode.Q65)  -> 2_320_143_000.0
        Pair(M9CM, RadioMode.Q65)  -> 3_400_100_000.0
        Pair(M6CM, RadioMode.Q65)  -> 5_760_100_000.0
        Pair(M3CM, RadioMode.Q65)  -> 10_368_100_000.0
        Pair(M1_25CM,RadioMode.Q65)-> 24_048_100_000.0
        else                        -> defaultFrequency(RadioMode.FT8)
    }

    fun frequencyString(mode: RadioMode): String {
        val hz = defaultFrequency(mode)
        return if (hz >= 1_000_000_000) "%.3f GHz".format(hz / 1_000_000_000)
               else "%.3f MHz".format(hz / 1_000_000)
    }
}
