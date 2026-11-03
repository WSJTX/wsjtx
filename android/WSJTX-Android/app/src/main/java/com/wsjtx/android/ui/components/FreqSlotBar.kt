package com.wsjtx.android.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.wsjtx.android.models.*

// ─────────────────────────────────────────────────────────────────
// Freq + Slot Bar (all modes)
// ─────────────────────────────────────────────────────────────────

@Composable
fun FreqSlotBar(op: OperatingConfig, cycleSeconds: Int) {
    val rxFreq     by op.rxFreq.collectAsState()
    val txFreq     by op.txFreq.collectAsState()
    val freqLocked by op.freqLocked.collectAsState()
    val txSlot     by op.txSlot.collectAsState()
    var expanded   by remember { mutableStateOf(false) }

    Surface(color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.7f)) {
        Column {
            // Summary row
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 12.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("RX $rxFreq Hz", color = MaterialTheme.colorScheme.secondary,
                    style = MaterialTheme.typography.labelSmall.copy(fontFamily = FontFamily.Monospace))
                if (rxFreq != txFreq) {
                    Text(" → TX $txFreq Hz", color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.labelSmall.copy(fontFamily = FontFamily.Monospace))
                }
                Spacer(Modifier.weight(1f))
                // Slot toggle
                TextButton(
                    onClick  = { op.txSlot.value = if (txSlot == TxSlot.FIRST) TxSlot.SECOND else TxSlot.FIRST },
                    modifier = Modifier.height(28.dp)
                ) {
                    Text("TX ${txSlot.label}", style = MaterialTheme.typography.labelSmall)
                }
                SlotStatusChip(txSlot, cycleSeconds)
                IconButton(onClick = { expanded = !expanded }, modifier = Modifier.size(28.dp)) {
                    Text(if (expanded) "▲" else "▼", style = MaterialTheme.typography.labelSmall)
                }
            }

            // Detail panel
            if (expanded) {
                Column(modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)) {
                    HorizontalDivider()
                    Spacer(Modifier.height(6.dp))
                    // RX freq slider
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("RX", color = MaterialTheme.colorScheme.secondary,
                            style = MaterialTheme.typography.labelSmall, modifier = Modifier.width(28.dp))
                        Slider(value = rxFreq.toFloat(), onValueChange = { op.setRxFreq(it.toInt()) },
                            valueRange = 200f..3000f, steps = 55,
                            modifier = Modifier.weight(1f))
                        Text("$rxFreq Hz", style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = FontFamily.Monospace), modifier = Modifier.width(64.dp))
                    }
                    // TX freq slider
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("TX", color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.labelSmall, modifier = Modifier.width(28.dp))
                        Slider(value = txFreq.toFloat(),
                            onValueChange = { if (!freqLocked) op.setTxFreq(it.toInt()) },
                            valueRange = 200f..3000f, steps = 55,
                            enabled = !freqLocked, modifier = Modifier.weight(1f))
                        Text("$txFreq Hz", style = MaterialTheme.typography.labelSmall.copy(
                            fontFamily = FontFamily.Monospace), modifier = Modifier.width(64.dp))
                        IconButton(onClick = { op.freqLocked.value = !freqLocked }, modifier = Modifier.size(24.dp)) {
                            Text(if (freqLocked) "🔒" else "🔓", style = MaterialTheme.typography.labelSmall)
                        }
                    }
                    // TX slot picker
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("TX slot:", style = MaterialTheme.typography.labelSmall,
                            modifier = Modifier.width(56.dp))
                        TxSlot.values().forEach { s ->
                            FilterChip(selected = txSlot == s, onClick = { op.txSlot.value = s },
                                label = { Text(s.label) }, modifier = Modifier.padding(end = 4.dp))
                        }
                    }
                    Spacer(Modifier.height(4.dp))
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// Slot Status Chip
// ─────────────────────────────────────────────────────────────────

@Composable
fun SlotStatusChip(slot: TxSlot, cycleSeconds: Int) {
    var myTurn by remember { mutableStateOf(false) }
    LaunchedEffect(slot, cycleSeconds) {
        while (true) {
            myTurn = slot.isMyTurn(cycleSeconds)
            kotlinx.coroutines.delay(500)
        }
    }
    Surface(
        color  = if (myTurn) MaterialTheme.colorScheme.error.copy(alpha = 0.2f)
                 else MaterialTheme.colorScheme.secondary.copy(alpha = 0.2f),
        shape  = MaterialTheme.shapes.small
    ) {
        Text(if (myTurn) "TX NOW" else "RX NOW",
            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
            color    = if (myTurn) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.secondary,
            style    = MaterialTheme.typography.labelSmall)
    }
}

// ─────────────────────────────────────────────────────────────────
// Q65 Config Panel
// ─────────────────────────────────────────────────────────────────

@Composable
fun Q65ConfigPanel(q65: Q65Config, op: OperatingConfig) {
    val subMode by q65.subMode.collectAsState()
    val period  by q65.period.collectAsState()

    Column {
        FreqSlotBar(op, period.seconds)

        Surface(color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)) {
            Column(modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    // Mode badge
                    Surface(color = MaterialTheme.colorScheme.tertiary.copy(alpha = 0.2f),
                            shape = MaterialTheme.shapes.small) {
                        Text(q65.modeLabel,
                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                            color = MaterialTheme.colorScheme.tertiary,
                            style = MaterialTheme.typography.labelMedium.copy(fontFamily = FontFamily.Monospace))
                    }
                    Spacer(Modifier.weight(1f))
                    q65.subModeWarning?.let { warn ->
                        Text(warn, color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.labelSmall)
                    }
                }
                Spacer(Modifier.height(4.dp))
                // Sub-mode chips
                Row {
                    Text("Sub: ", style = MaterialTheme.typography.labelSmall,
                        modifier = Modifier.align(Alignment.CenterVertically))
                    Q65SubMode.values().forEach { s ->
                        val valid = s.toneSeparation(period) != null
                        FilterChip(selected = subMode == s, onClick = { q65.subMode.value = s },
                            label = { Text(s.label) }, enabled = valid,
                            modifier = Modifier.padding(end = 4.dp))
                    }
                }
                Spacer(Modifier.height(4.dp))
                // Period chips
                Row {
                    Text("Period: ", style = MaterialTheme.typography.labelSmall,
                        modifier = Modifier.align(Alignment.CenterVertically))
                    Q65Period.values().forEach { p ->
                        FilterChip(selected = period == p, onClick = { q65.period.value = p },
                            label = { Text(p.label) }, modifier = Modifier.padding(end = 4.dp))
                    }
                }
                Text(period.hint, style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
            }
        }
    }
}
