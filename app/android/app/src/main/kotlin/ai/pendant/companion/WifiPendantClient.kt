package ai.pendant.companion

import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.Executors

/** Local TCP Voice Link used after BLE has provisioned the phone hotspot. */
class WifiPendantClient(private val listener: Listener) {
    interface Listener {
        fun onWifiPacket(packet: VoiceLink.Packet)
        fun onWifiChanged(connected: Boolean, message: String)
        fun onWifiError(message: String)
    }

    private val executor = Executors.newSingleThreadExecutor()
    private val lock = Any()
    private var socket: Socket? = null
    private var output: BufferedOutputStream? = null
    @Volatile private var connected = false
    private var sequence = 0

    fun connect(host: String) {
        close()
        executor.execute {
            try {
                val opened = Socket()
                opened.connect(InetSocketAddress(host, PORT), 6_000)
                val stream = BufferedOutputStream(opened.getOutputStream())
                synchronized(lock) {
                    socket = opened
                    output = stream
                    connected = true
                }
                listener.onWifiChanged(true, "Wi‑Fi voice link connected")
                readLoop(opened)
            } catch (error: Exception) {
                listener.onWifiError("Wi‑Fi voice link failed: ${error.message ?: "network error"}")
            } finally {
                val wasConnected = connected
                close()
                if (wasConnected) listener.onWifiChanged(false, "Wi‑Fi voice link disconnected")
            }
        }
    }

    fun isConnected(): Boolean = connected

    fun sendControl(opcode: Byte, message: String? = null) {
        val text = message.orEmpty().encodeToByteArray().take(VoiceLink.MAX_PAYLOAD - 1).toByteArray()
        send(VoiceLink.Packet(VoiceLink.CONTROL, nextSequence(), nowMs(), 0, byteArrayOf(opcode) + text))
    }

    fun sendSpeakerFrame(opus: ByteArray, timestampMs: Long, mouthLevel: Int) {
        send(VoiceLink.Packet(VoiceLink.SPEAKER_OPUS, nextSequence(), timestampMs, mouthLevel, opus))
    }

    fun close() {
        synchronized(lock) {
            connected = false
            runCatching { socket?.close() }
            socket = null
            output = null
        }
    }

    private fun readLoop(opened: Socket) {
        val input = BufferedInputStream(opened.getInputStream())
        val header = ByteArray(VoiceLink.HEADER_SIZE)
        while (!opened.isClosed) {
            input.readFully(header)
            val payloadSize = (header[8].toInt() and 0xff) or ((header[9].toInt() and 0xff) shl 8)
            if (payloadSize !in 0..VoiceLink.MAX_PAYLOAD) throw IllegalStateException("Invalid Wi‑Fi Voice Link payload")
            val encoded = header + ByteArray(payloadSize)
            input.readFully(encoded, VoiceLink.HEADER_SIZE, payloadSize)
            VoiceLink.decode(encoded)?.let(listener::onWifiPacket)
        }
    }

    private fun send(packet: VoiceLink.Packet) {
        val bytes = VoiceLink.encode(packet)
        synchronized(lock) {
            check(connected) { "Wi‑Fi pendant link is not connected" }
            output?.apply { write(bytes); flush() } ?: error("Wi‑Fi pendant link is not connected")
        }
    }

    private fun BufferedInputStream.readFully(bytes: ByteArray, offset: Int = 0, count: Int = bytes.size) {
        var position = offset
        val end = offset + count
        while (position < end) {
            val read = read(bytes, position, end - position)
            if (read < 0) throw IllegalStateException("Wi‑Fi pendant link closed")
            position += read
        }
    }

    private fun nextSequence(): Int = (sequence++ and 0xffff)
    private fun nowMs(): Long = android.os.SystemClock.elapsedRealtime()

    private companion object { const val PORT = 32323 }
}
