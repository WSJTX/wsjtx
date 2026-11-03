package com.wsjtx.android.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.wsjtx.android.audio.AppViewModel
import com.wsjtx.android.models.DecodedMessage

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DecodesScreen(vm: AppViewModel) {
    val messages by vm.messages.collectAsState()
    var filter   by remember { mutableStateOf(MessageFilter.ALL) }
    var search   by remember { mutableStateOf("") }

    val filtered = messages.filter { msg ->
        (filter == MessageFilter.ALL ||
         (filter == MessageFilter.CQ   && msg.isCQ) ||
         (filter == MessageFilter.MINE && msg.isDirectedToMe)) &&
        (search.isEmpty() || msg.text.contains(search, ignoreCase = true))
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Decodes (${messages.size})") },
                actions = {
                    IconButton(onClick = { vm.messages.value = emptyList() }) {
                        Icon(Icons.Default.Delete, "Clear")
                    }
                }
            )
        }
    ) { pad ->
        Column(modifier = Modifier.padding(pad)) {
            // Filter chips
            Row(modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)) {
                MessageFilter.values().forEach { f ->
                    FilterChip(selected = filter == f, onClick = { filter = f },
                        label = { Text(f.label) }, modifier = Modifier.padding(end = 4.dp))
                }
            }
            // Search
            OutlinedTextField(value = search, onValueChange = { search = it },
                label = { Text("Search") }, modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp),
                singleLine = true)
            HorizontalDivider()
            // Message list
            LazyColumn {
                items(filtered, key = { it.id }) { msg ->
                    MessageRow(msg) {
                        vm.dxCall.value = msg.callsignA ?: ""
                        vm.dxGrid.value = msg.grid ?: ""
                    }
                }
            }
        }
    }
}

@Composable
private fun MessageRow(msg: DecodedMessage, onClick: () -> Unit) {
    val bgColor = when {
        msg.isDirectedToMe -> MaterialTheme.colorScheme.secondary.copy(alpha = 0.15f)
        msg.isCQ           -> MaterialTheme.colorScheme.tertiary.copy(alpha = 0.1f)
        else               -> Color.Transparent
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(bgColor)
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        val mono = MaterialTheme.typography.labelSmall.copy(fontFamily = FontFamily.Monospace)
        Text(msg.utcTime, style = mono, modifier = Modifier.width(36.dp),
             color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
        val snrColor = when {
            msg.snr >= 10  -> MaterialTheme.colorScheme.secondary
            msg.snr >= 0   -> MaterialTheme.colorScheme.onSurface
            else           -> MaterialTheme.colorScheme.tertiary
        }
        Text("%+3d".format(msg.snr), style = mono, color = snrColor,
             modifier = Modifier.width(32.dp))
        Text("%+.1f".format(msg.dt), style = mono, modifier = Modifier.width(36.dp),
             color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
        Text("${msg.frequency}", style = mono, modifier = Modifier.width(44.dp),
             color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
        Spacer(Modifier.width(8.dp))
        val textColor = when {
            msg.isDirectedToMe -> MaterialTheme.colorScheme.secondary
            msg.isCQ           -> MaterialTheme.colorScheme.tertiary
            else               -> MaterialTheme.colorScheme.onSurface
        }
        Text(msg.text, style = mono, color = textColor, modifier = Modifier.weight(1f),
             maxLines = 1)
    }
    HorizontalDivider(thickness = 0.5.dp, color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
}

private enum class MessageFilter(val label: String) { ALL("All"), CQ("CQ"), MINE("Mine") }
