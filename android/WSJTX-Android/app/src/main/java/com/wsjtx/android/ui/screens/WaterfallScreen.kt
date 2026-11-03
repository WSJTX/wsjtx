package com.wsjtx.android.ui.screens

import android.graphics.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.RadioButtonChecked
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.*
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.*
import com.wsjtx.android.audio.AppViewModel
import com.wsjtx.android.models.*
import com.wsjtx.android.ui.components.*
import kotlinx.coroutines.flow.collectLatest
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import kotlin.math.abs

// ─────────────────────────────────────────────────────────────────
// Waterfall Screen
// ─────────────────────────────────────────────────────────────────

@Composable
fun WaterfallScreen(vm: AppViewModel) {
    val mode         by vm.currentMode.collectAsState()
    val band         by vm.currentBand.collectAsState()
    val isRunning    by vm.audioEngine.isRunning.collectAsState()
    val inputLevel   by vm.audioEngine.inputLevel.collectAsState()
    val transmitting by vm.transmitting.collectAsState()
    val periodSec    by vm.currentPeriodSeconds.collectAsState()
    val rxFreq       by vm.opConfig.rxFreq.collectAsState()
    val txFreq       by vm.opConfig.txFreq.collectAsState()

    Column(modifier = Modifier.fillMaxSize()) {

        // ── Band / Mode bar ──────────────────────────────────────
        BandModeBar(vm)

        // ── Freq + Slot strip ────────────────────────────────────
        if (mode == RadioMode.Q65) {
            Q65ConfigPanel(vm.q65Config, vm.opConfig)
        } else {
            FreqSlotBar(vm.opConfig, periodSec)
        }

        // ── Waterfall ────────────────────────────────────────────
        WaterfallCanvas(
            waterfall    = vm.waterfall,
            rxFreq       = rxFreq.toFloat(),
            txFreq       = txFreq.toFloat(),
            transmitting = transmitting,
            periodSec    = periodSec,
            modifier     = Modifier
                .fillMaxWidth()
                .weight(1f)
                .clickable { /* tap to tune handled inside canvas */ }
        )

        HorizontalDivider()

        // ── Cycle timer + level meter ────────────────────────────
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            CycleTimerBar(periodSec)
            Spacer(Modifier.weight(1f))
            LevelMeterBar(inputLevel)
        }

        HorizontalDivider()

        // ── Quick TX panel ───────────────────────────────────────
        QuickTXPanel(vm)
    }
}

// ─────────────────────────────────────────────────────────────────
// Band / Mode Bar
// ─────────────────────────────────────────────────────────────────

@Composable
fun BandModeBar(vm: AppViewModel) {
    val band by vm.currentBand.collectAsState()
    val mode by vm.currentMode.collectAsState()
    var expandBand by remember { mutableStateOf(false) }
    var expandMode by remember { mutableStateOf(false) }

    Surface(color = MaterialTheme.colorScheme.surfaceVariant) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Band dropdown
            Box {
                TextButton(onClick = { expandBand = true }) {
                    Text(band.label, style = MaterialTheme.typography.labelLarge)
                }
                DropdownMenu(expanded = expandBand, onDismissRequest = { expandBand = false }) {
                    Band.values().forEach { b ->
                        DropdownMenuItem(
                            text = { Text(b.label) },
                            onClick = { vm.currentBand.value = b; expandBand = false }
                        )
                    }
                }
            }

            // Mode dropdown
            Box {
                TextButton(onClick = { expandMode = true }) {
                    Text(mode.label, style = MaterialTheme.typography.labelLarge)
                }
                DropdownMenu(expanded = expandMode, onDismissRequest = { expandMode = false }) {
                    RadioMode.values().forEach { m ->
                        DropdownMenuItem(
                            text = { Text(m.label) },
                            onClick = { vm.currentMode.value = m; expandMode = false }
                        )
                    }
                }
            }

            Spacer(Modifier.weight(1f))

            // Dial frequency
            Text(
                band.frequencyString(mode),
                style = MaterialTheme.typography.labelSmall.copy(fontFamily = FontFamily.Monospace),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// Cycle Timer
// ─────────────────────────────────────────────────────────────────

@Composable
fun CycleTimerBar(periodSec: Int) {
    var remaining by remember { mutableIntStateOf(periodSec) }

    LaunchedEffect(periodSec) {
        while (true) {
            val now   = System.currentTimeMillis() / 1_000L
            remaining = (periodSec - (now % periodSec)).toInt()
            kotlinx.coroutines.delay(200)
        }
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(Icons.Default.RadioButtonChecked, null, modifier = Modifier.size(14.dp),
             tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
        Spacer(Modifier.width(4.dp))
        LinearProgressIndicator(
            progress = { (periodSec - remaining).toFloat() / periodSec },
            modifier = Modifier.width(80.dp).height(8.dp),
            color    = if (remaining <= 3) MaterialTheme.colorScheme.error
                       else MaterialTheme.colorScheme.secondary
        )
        Spacer(Modifier.width(4.dp))
        Text("${remaining}s", style = MaterialTheme.typography.labelSmall.copy(
            fontFamily = FontFamily.Monospace))
    }
}

// ─────────────────────────────────────────────────────────────────
// Level Meter
// ─────────────────────────────────────────────────────────────────

@Composable
fun LevelMeterBar(level: Float) {
    val color = when {
        level > 0.85f -> MaterialTheme.colorScheme.error
        level > 0.6f  -> MaterialTheme.colorScheme.tertiary
        else          -> MaterialTheme.colorScheme.secondary
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(Icons.Default.Mic, null, modifier = Modifier.size(14.dp),
             tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
        Spacer(Modifier.width(4.dp))
        LinearProgressIndicator(
            progress = { level },
            modifier = Modifier.width(80.dp).height(8.dp),
            color    = color
        )
    }
}

// ─────────────────────────────────────────────────────────────────
// Quick TX Panel
// ─────────────────────────────────────────────────────────────────

@Composable
fun QuickTXPanel(vm: AppViewModel) {
    val transmitting by vm.transmitting.collectAsState()
    val dxCall       by vm.dxCall.collectAsState()
    val dxGrid       by vm.dxGrid.collectAsState()
    val txMessage    by vm.txMessage.collectAsState()
    val txError      by vm.txError.collectAsState()
    var showCompose  by remember { mutableStateOf(false) }

    Column(modifier = Modifier.padding(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                Text("DX: ${dxCall.ifEmpty { "—" }}",
                     style = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace))
                Text("Grid: ${dxGrid.ifEmpty { "—" }}",
                     style = MaterialTheme.typography.labelSmall,
                     color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
            }
            Button(
                onClick  = { showCompose = true },
                colors   = ButtonDefaults.buttonColors(
                    containerColor = if (transmitting) MaterialTheme.colorScheme.error
                                     else MaterialTheme.colorScheme.primary
                )
            ) {
                Text(if (transmitting) "Stop TX" else "TX")
            }
        }
        if (txMessage.isNotEmpty()) {
            Text(txMessage, style = MaterialTheme.typography.labelSmall.copy(
                fontFamily = FontFamily.Monospace),
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
        }
        txError?.let {
            Text(it, color = MaterialTheme.colorScheme.error,
                 style = MaterialTheme.typography.labelSmall)
        }
    }

    if (showCompose) {
        ComposeMessageDialog(vm) { showCompose = false }
    }
}

// ─────────────────────────────────────────────────────────────────
// Compose Message Dialog
// ─────────────────────────────────────────────────────────────────

@Composable
fun ComposeMessageDialog(vm: AppViewModel, onDismiss: () -> Unit) {
    val mode       by vm.currentMode.collectAsState()
    val transmitting by vm.transmitting.collectAsState()
    var dxCall     by remember { mutableStateOf(vm.dxCall.value) }
    var dxGrid     by remember { mutableStateOf(vm.dxGrid.value) }
    val myCall     = remember {
        vm.getApplication<android.app.Application>()
            .getSharedPreferences("wsjtx", 0).getString("myCall", "") ?: ""
    }
    var stage      by remember { mutableStateOf(com.wsjtx.android.encoder.QSOStage.CALLING) }

    val suggested = vm.encoder.nextMessage(myCall, dxCall, dxGrid, null, stage)

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Compose TX") },
        text = {
            Column {
                OutlinedTextField(dxCall, { dxCall = it }, label = { Text("DX Callsign") },
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(dxGrid, { dxGrid = it }, label = { Text("Their Grid") },
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                Text("Stage:", style = MaterialTheme.typography.labelMedium)
                Row {
                    com.wsjtx.android.encoder.QSOStage.values().forEach { s ->
                        FilterChip(selected = stage == s, onClick = { stage = s },
                            label = { Text(s.name.take(4)) },
                            modifier = Modifier.padding(end = 4.dp))
                    }
                }
                Spacer(Modifier.height(8.dp))
                Text(suggested, style = MaterialTheme.typography.bodyMedium.copy(
                    fontFamily = FontFamily.Monospace),
                    color = MaterialTheme.colorScheme.primary)
            }
        },
        confirmButton = {
            Button(onClick = {
                vm.dxCall.value = dxCall
                vm.dxGrid.value = dxGrid
                if (transmitting) vm.stopTransmitting() else vm.transmit(suggested)
            }) { Text(if (transmitting) "Stop" else "Transmit") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
