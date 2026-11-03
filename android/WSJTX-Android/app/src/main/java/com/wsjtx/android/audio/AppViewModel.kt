package com.wsjtx.android.audio

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.wsjtx.android.decoder.FT8Decoder
import com.wsjtx.android.dsp.WaterfallData
import com.wsjtx.android.encoder.FT8Encoder
import com.wsjtx.android.encoder.Q65Encoder
import com.wsjtx.android.models.*
import com.wsjtx.android.network.RigControl
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

class AppViewModel(app: Application) : AndroidViewModel(app) {

    val audioEngine  = AudioEngine()
    val waterfall    = WaterfallData()
    val decoder      = FT8Decoder()
    val encoder      = FT8Encoder()
    val q65Encoder   = Q65Encoder()
    val opConfig     = OperatingConfig()
    val q65Config    = Q65Config()
    val rigControl   = RigControl()

    val messages     = MutableStateFlow<List<DecodedMessage>>(emptyList())
    val logbook      = MutableStateFlow<List<QSORecord>>(emptyList())
    val currentBand  = MutableStateFlow(Band.M20)
    val currentMode  = MutableStateFlow(RadioMode.FT8)
    val transmitting = MutableStateFlow(false)
    val txError      = MutableStateFlow<String?>(null)
    val dxCall       = MutableStateFlow("")
    val dxGrid       = MutableStateFlow("")
    val txMessage    = MutableStateFlow("")

    // Current T/R period in seconds — updated when mode or Q65 period changes
    val currentPeriodSeconds: StateFlow<Int> = combine(currentMode, q65Config.period) { mode, period ->
        if (mode == RadioMode.Q65) period.seconds else mode.cycleSeconds.toInt()
    }.stateIn(viewModelScope, SharingStarted.Eagerly, 15)

    init {
        wireAudio()
        wireDecoder()
    }

    private fun wireAudio() {
        audioEngine.onSampleBuffer = { samples, rate ->
            waterfall.ingest(samples)
            decoder.ingest(samples, rate)
        }
    }

    private fun wireDecoder() {
        decoder.onMessage = { msg ->
            viewModelScope.launch {
                messages.update { (listOf(msg) + it).take(500) }
            }
        }
    }

    fun startListening() = audioEngine.start()
    fun stopListening()  = audioEngine.stop()

    fun stopTransmitting() {
        audioEngine.stopTransmit()
        transmitting.value = false
    }

    fun transmit(message: String) {
        val myCall = getMyCall()
        if (myCall.isEmpty()) { txError.value = "Set your callsign in Settings first."; return }

        val symbols: IntArray
        val toneSepOverride: Double?

        when (currentMode.value) {
            RadioMode.FT8, RadioMode.FT4 -> {
                val enc = encoder.encode(message, currentMode.value)
                if (enc.isEmpty()) { txError.value = "Could not encode: $message"; return }
                symbols         = enc
                toneSepOverride = null
            }
            RadioMode.Q65 -> {
                val enc = q65Encoder.encode(message, q65Config.subMode.value)
                if (enc.isEmpty()) { txError.value = "Could not encode Q65: $message"; return }
                symbols         = enc
                toneSepOverride = q65Config.effectiveToneSeparation
            }
            else -> {
                txError.value = "${currentMode.value.label} TX not yet supported."
                return
            }
        }

        txError.value      = null
        txMessage.value    = message
        transmitting.value = true
        audioEngine.transmit(symbols, currentMode.value,
                             opConfig.txFreq.value.toDouble(), toneSepOverride) {
            viewModelScope.launch { transmitting.value = false }
        }
    }

    fun logQSO() {
        if (dxCall.value.isEmpty()) return
        val record = QSORecord(
            myCall      = getMyCall(),
            dxCall      = dxCall.value,
            frequencyHz = currentBand.value.defaultFrequency(currentMode.value),
            mode        = currentMode.value,
            grid        = dxGrid.value
        )
        logbook.update { it + record }
        appendAdifLog(record)
    }

    private fun getMyCall(): String = getApplication<Application>()
        .getSharedPreferences("wsjtx", 0).getString("myCall", "") ?: ""

    private fun appendAdifLog(record: QSORecord) {
        val dir = getApplication<Application>().getExternalFilesDir(null) ?: return
        ADIFExporter.append(record, java.io.File(dir, "wsjtx.adi"))
    }
}
