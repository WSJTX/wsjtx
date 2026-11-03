package com.wsjtx.android.ui.screens

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.wsjtx.android.audio.AppViewModel
import com.wsjtx.android.utils.GridLocator
import com.google.android.gms.location.LocationServices

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(vm: AppViewModel, activity: Activity) {
    val prefs   = LocalContext.current.getSharedPreferences("wsjtx", 0)
    var myCall  by remember { mutableStateOf(prefs.getString("myCall", "") ?: "") }
    var myGrid  by remember { mutableStateOf(prefs.getString("myGrid", "") ?: "") }
    var rigHost by remember { mutableStateOf(prefs.getString("rigHost", "192.168.1.1") ?: "") }
    var rigPort by remember { mutableStateOf(prefs.getInt("rigPort", 4532).toString()) }
    var pskEnabled by remember { mutableStateOf(prefs.getBoolean("pskReporter", false)) }

    val isConnected by vm.rigControl.isConnected.collectAsState()
    val rigError    by vm.rigControl.lastError.collectAsState()

    val locationLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            LocationServices.getFusedLocationProviderClient(activity)
                .lastLocation.addOnSuccessListener { loc ->
                    if (loc != null) myGrid = GridLocator.grid(loc)
                }
        }
    }

    fun save() {
        prefs.edit()
            .putString("myCall", myCall)
            .putString("myGrid", myGrid)
            .putString("rigHost", rigHost)
            .putInt("rigPort", rigPort.toIntOrNull() ?: 4532)
            .putBoolean("pskReporter", pskEnabled)
            .apply()
        vm.rigControl.host    = rigHost
        vm.rigControl.rigPort = rigPort.toIntOrNull() ?: 4532
        vm.decoder.setMyCall(myCall)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                actions = {
                    TextButton(onClick = { save() }) { Text("Save") }
                }
            )
        }
    ) { pad ->
        Column(
            modifier = Modifier
                .padding(pad)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // ── Station ──────────────────────────────────────────
            Text("Station", style = MaterialTheme.typography.titleSmall)
            OutlinedTextField(myCall, { myCall = it.uppercase() },
                label = { Text("My Callsign") }, singleLine = true,
                modifier = Modifier.fillMaxWidth())
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(myGrid, { myGrid = it.uppercase() },
                    label = { Text("My Grid") }, singleLine = true,
                    modifier = Modifier.weight(1f))
                Spacer(Modifier.width(8.dp))
                Button(onClick = {
                    if (ContextCompat.checkSelfPermission(activity,
                            Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
                        LocationServices.getFusedLocationProviderClient(activity)
                            .lastLocation.addOnSuccessListener { loc ->
                                if (loc != null) myGrid = GridLocator.grid(loc)
                            }
                    } else locationLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
                }) { Text("GPS") }
            }

            HorizontalDivider()

            // ── Rig Control ──────────────────────────────────────
            Text("Rig Control (Hamlib)", style = MaterialTheme.typography.titleSmall)
            OutlinedTextField(rigHost, { rigHost = it },
                label = { Text("Host") }, singleLine = true,
                modifier = Modifier.fillMaxWidth())
            OutlinedTextField(rigPort, { rigPort = it },
                label = { Text("Port") }, singleLine = true,
                modifier = Modifier.fillMaxWidth())
            Row(verticalAlignment = Alignment.CenterVertically) {
                val dot = if (isConnected) "🟢" else "🔴"
                Text("$dot ${if (isConnected) "Connected" else "Disconnected"}",
                    style = MaterialTheme.typography.labelMedium)
                Spacer(Modifier.weight(1f))
                Button(onClick = {
                    save()
                    if (isConnected) vm.rigControl.disconnect() else vm.rigControl.connect()
                }) { Text(if (isConnected) "Disconnect" else "Connect") }
            }
            rigError?.let { Text(it, color = MaterialTheme.colorScheme.error,
                style = MaterialTheme.typography.labelSmall) }

            HorizontalDivider()

            // ── PSK Reporter ─────────────────────────────────────
            Text("PSK Reporter", style = MaterialTheme.typography.titleSmall)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Upload spots to pskreporter.info", modifier = Modifier.weight(1f))
                Switch(pskEnabled, { pskEnabled = it })
            }
        }
    }
}
