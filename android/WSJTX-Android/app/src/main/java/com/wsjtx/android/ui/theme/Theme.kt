package com.wsjtx.android.ui.theme

import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// WSJT-X inspired dark colour scheme
private val Primary   = Color(0xFF4FC3F7)   // sky blue
private val Secondary = Color(0xFF81C784)   // green
private val Tertiary  = Color(0xFFFFB74D)   // amber
private val Background = Color(0xFF0A0E14)  // near-black
private val Surface   = Color(0xFF111820)
private val OnSurface = Color(0xFFCFD8DC)

private val DarkColors = darkColorScheme(
    primary      = Primary,
    secondary    = Secondary,
    tertiary     = Tertiary,
    background   = Background,
    surface      = Surface,
    onBackground = OnSurface,
    onSurface    = OnSurface,
    onPrimary    = Color.Black,
    onSecondary  = Color.Black
)

@Composable
fun WSJTXTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = DarkColors, content = content)
}
