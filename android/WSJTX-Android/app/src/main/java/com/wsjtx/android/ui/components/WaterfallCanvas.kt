package com.wsjtx.android.ui.components

import android.graphics.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.*
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import com.wsjtx.android.dsp.WaterfallData
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import kotlin.math.roundToInt

// ─────────────────────────────────────────────────────────────────
// Waterfall Canvas — Compose Canvas backed by a Bitmap
// ─────────────────────────────────────────────────────────────────

@Composable
fun WaterfallCanvas(
    waterfall:    WaterfallData,
    rxFreq:       Float = 1_000f,
    txFreq:       Float = 1_000f,
    transmitting: Boolean = false,
    periodSec:    Int = 15,
    fLow:         Float = 200f,
    fHigh:        Float = 3_000f,
    dbLow:        Float = -55f,
    dbHigh:       Float = 10f,
    modifier:     Modifier = Modifier,
    onFreqSelected: ((Float) -> Unit)? = null
) {
    var bitmap       by remember { mutableStateOf<ImageBitmap?>(null) }
    var bmpWidth     by remember { mutableIntStateOf(0) }
    var bmpHeight    by remember { mutableIntStateOf(0) }
    val rowHeightPx  = 2

    // Period line tracking
    val periodLines  = remember { mutableStateListOf<Pair<Float, String>>() }  // (y, label)
    var lastPeriodIdx by remember { mutableLongStateOf(-1L) }
    val utcFmt       = remember { DateTimeFormatter.ofPattern("HH:mm:ss").withZone(ZoneOffset.UTC) }

    // Wire waterfall frame callback
    LaunchedEffect(waterfall) {
        waterfall.onRow = { bins, freqAxis ->
            withContext(Dispatchers.Main) {
                val w = bmpWidth.takeIf { it > 0 } ?: return@withContext
                val h = bmpHeight.takeIf { it > 0 } ?: return@withContext

                // Scroll existing bitmap down
                val newBmp = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888)
                val canvas = android.graphics.Canvas(newBmp)
                bitmap?.let { canvas.drawBitmap(it.asAndroidBitmap(), 0f, rowHeightPx.toFloat(), null) }

                // Paint new row at top
                val rowPixels = IntArray(w)
                for (px in 0 until w) {
                    val hz    = fLow + (fHigh - fLow) * px / w
                    val db    = interpolateDB(hz, bins, freqAxis)
                    rowPixels[px] = waterfallArgb(db, dbLow, dbHigh)
                }
                newBmp.setPixels(rowPixels, 0, w, 0, 0, w, rowHeightPx)
                bitmap = newBmp.asImageBitmap()

                // Period line tracking
                val nowSec   = System.currentTimeMillis() / 1_000L
                val pidx     = if (periodSec > 0) nowSec / periodSec else 0
                val updated  = periodLines.map { (y, l) -> Pair(y + rowHeightPx, l) }
                periodLines.clear()
                periodLines.addAll(updated.filter { it.first < h + 20 })
                if (pidx != lastPeriodIdx && lastPeriodIdx >= 0) {
                    val ts = utcFmt.format(Instant.ofEpochSecond(pidx * periodSec))
                    periodLines.add(Pair(0f, ts))
                }
                lastPeriodIdx = pidx
            }
        }
    }

    Canvas(
        modifier = modifier
            .pointerInput(fLow, fHigh) {
                detectTapGestures { offset ->
                    val hz = fLow + (fHigh - fLow) * (offset.x / size.width)
                    onFreqSelected?.invoke(hz)
                }
            }
    ) {
        bmpWidth  = size.width.roundToInt()
        bmpHeight = size.height.roundToInt()

        // Draw waterfall bitmap
        bitmap?.let { bmp ->
            drawImage(bmp, dstOffset = IntOffset.Zero,
                      dstSize = IntSize(bmpWidth, bmpHeight))
        }

        // Period lines (yellow horizontal)
        periodLines.forEach { (y, label) ->
            drawLine(
                color  = Color.Yellow.copy(alpha = 0.85f),
                start  = androidx.compose.ui.geometry.Offset(0f, y),
                end    = androidx.compose.ui.geometry.Offset(bmpWidth.toFloat(), y),
                strokeWidth = 2f
            )
        }

        // RX marker (green dashed vertical)
        val rxX = (rxFreq - fLow) / (fHigh - fLow) * bmpWidth
        drawLine(Color.Green.copy(alpha = 0.85f),
            start = androidx.compose.ui.geometry.Offset(rxX, 0f),
            end   = androidx.compose.ui.geometry.Offset(rxX, bmpHeight.toFloat()),
            strokeWidth = 1.5f, pathEffect = PathEffect.dashPathEffect(floatArrayOf(4f, 2f)))

        // TX marker (solid red)
        if (txFreq != rxFreq || transmitting) {
            val txX = (txFreq - fLow) / (fHigh - fLow) * bmpWidth
            drawLine(
                color = if (transmitting) Color.Red else Color.Red.copy(alpha = 0.5f),
                start = androidx.compose.ui.geometry.Offset(txX, 0f),
                end   = androidx.compose.ui.geometry.Offset(txX, bmpHeight.toFloat()),
                strokeWidth = if (transmitting) 2f else 1f
            )
        }
    }
}

// ── Colour map ────────────────────────────────────────────────────

private fun waterfallArgb(db: Float, dbLow: Float, dbHigh: Float): Int {
    val norm = ((db - dbLow) / (dbHigh - dbLow)).coerceIn(0f, 1f)
    val (h, b) = when {
        norm < 0.25f -> 0.67f to norm * 4
        norm < 0.5f  -> 0.50f + (norm - 0.25f) * 2 * 0.17f to 1f
        norm < 0.75f -> 0.33f - (norm - 0.5f)  * 2 * 0.16f to 1f
        else          -> 0f to 1f
    }
    return android.graphics.Color.HSVToColor(floatArrayOf(h * 360, 1f, b.coerceAtLeast(0.05f)))
}

private fun interpolateDB(hz: Float, bins: FloatArray, freqAxis: FloatArray): Float {
    if (freqAxis.isEmpty()) return -120f
    val lo = freqAxis.indexOfLast { it <= hz }.takeIf { it >= 0 } ?: return bins.lastOrNull() ?: -120f
    if (lo + 1 >= bins.size) return bins[lo]
    val t = if (freqAxis[lo + 1] > freqAxis[lo]) (hz - freqAxis[lo]) / (freqAxis[lo + 1] - freqAxis[lo]) else 0f
    return bins[lo] * (1 - t) + bins[lo + 1] * t
}
