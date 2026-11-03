package com.wsjtx.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import com.wsjtx.android.ui.screens.MainScreen
import com.wsjtx.android.ui.theme.WSJTXTheme
import com.wsjtx.android.audio.AppViewModel

class MainActivity : ComponentActivity() {

    private val vm: AppViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            WSJTXTheme {
                MainScreen(vm = vm, activity = this)
            }
        }
    }

    override fun onStop() {
        super.onStop()
        vm.stopListening()
    }
}
