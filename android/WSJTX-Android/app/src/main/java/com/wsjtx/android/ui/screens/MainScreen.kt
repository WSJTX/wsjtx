package com.wsjtx.android.ui.screens

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.wsjtx.android.audio.AppViewModel

// ─────────────────────────────────────────────────────────────────
// Navigation destinations
// ─────────────────────────────────────────────────────────────────

sealed class Screen(val route: String, val icon: @Composable () -> Unit, val label: String) {
    object Waterfall : Screen("waterfall", { Icon(Icons.Default.Waves, null) },     "Waterfall")
    object Decodes   : Screen("decodes",   { Icon(Icons.Default.Message, null) },   "Decodes")
    object Logbook   : Screen("logbook",   { Icon(Icons.Default.Book, null) },      "Logbook")
    object Settings  : Screen("settings",  { Icon(Icons.Default.Settings, null) },  "Settings")
}

// ─────────────────────────────────────────────────────────────────
// Main Screen
// ─────────────────────────────────────────────────────────────────

@Composable
fun MainScreen(vm: AppViewModel, activity: Activity) {
    val navController = rememberNavController()

    // Request mic permission on start
    val launcher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> if (granted) vm.startListening() }

    LaunchedEffect(Unit) {
        if (ContextCompat.checkSelfPermission(activity, Manifest.permission.RECORD_AUDIO)
            == PackageManager.PERMISSION_GRANTED) {
            vm.startListening()
        } else {
            launcher.launch(Manifest.permission.RECORD_AUDIO)
        }
    }

    val screens = listOf(Screen.Waterfall, Screen.Decodes, Screen.Logbook, Screen.Settings)
    var selected by remember { mutableStateOf(0) }

    Scaffold(
        bottomBar = {
            NavigationBar {
                screens.forEachIndexed { i, s ->
                    NavigationBarItem(
                        selected  = selected == i,
                        onClick   = { selected = i; navController.navigate(s.route) { launchSingleTop = true } },
                        icon      = s.icon,
                        label     = { Text(s.label) }
                    )
                }
            }
        }
    ) { padding ->
        NavHost(
            navController = navController,
            startDestination = Screen.Waterfall.route,
            modifier = Modifier.padding(padding)
        ) {
            composable(Screen.Waterfall.route) { WaterfallScreen(vm) }
            composable(Screen.Decodes.route)   { DecodesScreen(vm) }
            composable(Screen.Logbook.route)   { LogbookScreen(vm) }
            composable(Screen.Settings.route)  { SettingsScreen(vm, activity) }
        }
    }
}
