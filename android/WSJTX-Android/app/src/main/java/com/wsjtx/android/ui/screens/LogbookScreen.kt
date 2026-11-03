package com.wsjtx.android.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.wsjtx.android.audio.AppViewModel
import com.wsjtx.android.models.ADIFExporter
import com.wsjtx.android.models.QSORecord

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LogbookScreen(vm: AppViewModel) {
    val logbook by vm.logbook.collectAsState()
    var search  by remember { mutableStateOf("") }
    val context = LocalContext.current

    val filtered = logbook.filter { r ->
        search.isEmpty() || r.dxCall.contains(search, ignoreCase = true) ||
        r.grid.contains(search, ignoreCase = true)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Logbook (${logbook.size})") },
                actions = {
                    IconButton(onClick = {
                        val adif = ADIFExporter.export(logbook)
                        val dir  = context.getExternalFilesDir(null) ?: return@IconButton
                        java.io.File(dir, "wsjtx_export.adi").writeText(adif)
                    }) { Icon(Icons.Default.Share, "Export ADIF") }
                    IconButton(onClick = { vm.logbook.value = emptyList() }) {
                        Icon(Icons.Default.Delete, "Clear")
                    }
                }
            )
        }
    ) { pad ->
        Column(modifier = Modifier.padding(pad)) {
            OutlinedTextField(value = search, onValueChange = { search = it },
                label = { Text("Search callsign / grid") },
                modifier = Modifier.fillMaxWidth().padding(8.dp), singleLine = true)
            HorizontalDivider()
            if (filtered.isEmpty()) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text("No QSOs logged yet. Swipe a decode left to log.")
                }
            } else {
                LazyColumn {
                    items(filtered.reversed(), key = { it.id }) { r -> QSORow(r) }
                }
            }
        }
    }
}

@Composable
private fun QSORow(r: QSORecord) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(r.dxCall, style = MaterialTheme.typography.titleSmall)
            Text(r.grid.ifEmpty { "—" }, style = MaterialTheme.typography.labelSmall,
                 color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
        }
        Column(horizontalAlignment = Alignment.End) {
            Text(r.mode.label, color = MaterialTheme.colorScheme.primary,
                 style = MaterialTheme.typography.labelSmall)
            Text("${r.frequencyMHz} MHz",
                 style = MaterialTheme.typography.labelSmall.copy(fontFamily = FontFamily.Monospace),
                 color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
            Text("${r.dateString} ${r.timeString}Z",
                 style = MaterialTheme.typography.labelSmall,
                 color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
        }
    }
    HorizontalDivider(thickness = 0.5.dp, color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
}
