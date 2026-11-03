package com.wsjtx.android.network

import com.wsjtx.android.models.RadioMode
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import java.net.*
import java.nio.ByteBuffer
import java.nio.ByteOrder

// ─────────────────────────────────────────────────────────────────
// Rig Control (Hamlib TCP + WSJT-X UDP)
// ─────────────────────────────────────────────────────────────────

class RigControl {
    val isConnected  = MutableStateFlow(false)
    val lastError    = MutableStateFlow<String?>(null)
    val currentFreq  = MutableStateFlow(14_074_000.0)

    private var tcpSocket: Socket?        = null
    private var udpSocket: DatagramSocket? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    var host:      String = "192.168.1.1"
    var rigPort:   Int    = 4532
    var wsjtxPort: Int    = 2237

    fun connect() {
        scope.launch {
            try {
                tcpSocket = Socket(host, rigPort).also { it.soTimeout = 3_000 }
                isConnected.value = true
                lastError.value   = null
                startUDPListener()
            } catch (e: Exception) {
                isConnected.value = false
                lastError.value   = e.message
            }
        }
    }

    fun disconnect() {
        tcpSocket?.close(); tcpSocket  = null
        udpSocket?.close(); udpSocket  = null
        isConnected.value = false
    }

    fun setFrequency(hz: Double, mode: RadioMode) {
        scope.launch {
            send("F ${hz.toLong()}\n")
            send("M USB 0\n")
            currentFreq.value = hz
            broadcastWSJTX(hz, mode)
        }
    }

    private fun send(cmd: String) {
        try { tcpSocket?.getOutputStream()?.write(cmd.toByteArray()) }
        catch (e: Exception) { isConnected.value = false; lastError.value = e.message }
    }

    private fun broadcastWSJTX(hz: Double, mode: RadioMode) {
        try {
            val payload = buildWSJTXStatus(hz, mode)
            val packet  = DatagramPacket(payload, payload.size,
                          InetAddress.getByName("255.255.255.255"), wsjtxPort)
            DatagramSocket().use { it.broadcast = true; it.send(packet) }
        } catch (_: Exception) {}
    }

    private fun buildWSJTXStatus(hz: Double, mode: RadioMode): ByteArray {
        val bb = ByteBuffer.allocate(256).order(ByteOrder.BIG_ENDIAN)
        bb.putInt(0xADBCCBDA.toInt())   // magic
        bb.putInt(2)                     // schema
        bb.putInt(1)                     // type: Status
        bb.putInt(0)                     // ID placeholder
        putWSJTXString(bb, "WSJTX-And")
        bb.putLong(hz.toLong())
        putWSJTXString(bb, mode.label)
        repeat(4) { putWSJTXString(bb, "") }
        bb.put(0); bb.put(0); bb.put(0)
        bb.putInt(0); bb.putInt(0)
        repeat(4) { putWSJTXString(bb, "") }
        repeat(3) { bb.put(0) }
        val arr = ByteArray(bb.position())
        bb.rewind(); bb.get(arr)
        return arr
    }

    private fun putWSJTXString(bb: ByteBuffer, s: String) {
        if (s.isEmpty()) { bb.putInt(-1); return }
        bb.putInt(s.length)
        bb.put(s.toByteArray(Charsets.UTF_8))
    }

    private fun startUDPListener() {
        udpSocket = DatagramSocket(wsjtxPort)
        scope.launch {
            val buf = ByteArray(65536)
            while (isActive) {
                try {
                    val pkt = DatagramPacket(buf, buf.size)
                    udpSocket?.receive(pkt) ?: break
                    val bb = ByteBuffer.wrap(pkt.data, 0, pkt.length).order(ByteOrder.BIG_ENDIAN)
                    if (bb.int != 0xADBCCBDA.toInt()) continue
                    bb.int; bb.int   // schema, type
                    bb.int           // ID length
                    readWSJTXString(bb)   // ID
                    currentFreq.value = bb.long.toDouble()
                } catch (_: Exception) { break }
            }
        }
    }

    private fun readWSJTXString(bb: ByteBuffer): String {
        val len = bb.int
        if (len < 0 || len > 1024) return ""
        val b = ByteArray(len); bb.get(b)
        return String(b, Charsets.UTF_8)
    }
}

// ─────────────────────────────────────────────────────────────────
// PSK Reporter
// ─────────────────────────────────────────────────────────────────

class PSKReporter(private val myCall: String, private val myGrid: String) {
    private val pending = mutableListOf<Spot>()
    data class Spot(val call: String, val hz: Double, val mode: RadioMode,
                    val snr: Int, val time: Long)

    fun addSpot(call: String, hz: Double, mode: RadioMode, snr: Int) {
        pending += Spot(call, hz, mode, snr, System.currentTimeMillis() / 1_000)
    }

    fun upload() {
        if (pending.isEmpty() || myCall.isEmpty()) return
        val xml = buildXML()
        pending.clear()
        CoroutineScope(Dispatchers.IO).launch {
            try {
                val conn = URL("https://www.pskreporter.info/report").openConnection()
                    as java.net.HttpURLConnection
                conn.requestMethod = "POST"
                conn.setRequestProperty("Content-Type", "application/xml")
                conn.doOutput = true
                conn.outputStream.writer().use { it.write(xml) }
                conn.responseCode  // trigger request
                conn.disconnect()
            } catch (_: Exception) {}
        }
    }

    private fun buildXML(): String {
        val sb = StringBuilder("""<?xml version="1.0" encoding="UTF-8"?>
<pskreport>
  <receiverInfo callsign="$myCall" locator="$myGrid" programid="WSJTX-And"/>
  <activeReceiver callsign="$myCall" locator="$myGrid"/>
""")
        for (s in pending) sb.appendLine(
            """  <receptionReport callsign="${s.call}" receiverCallsign="$myCall" """ +
            """frequency="${s.hz.toLong()}" mode="${s.mode.label}" sNR="${s.snr}" """ +
            """flowStartSeconds="${s.time}"/>"""
        )
        sb.append("</pskreport>")
        return sb.toString()
    }
}
