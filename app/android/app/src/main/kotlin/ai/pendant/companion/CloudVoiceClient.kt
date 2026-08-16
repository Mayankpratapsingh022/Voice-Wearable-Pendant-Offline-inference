package ai.pendant.companion

import android.util.Base64
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/** Live app-to-relay channel. It never logs audio or the Sarvam API key. */
class CloudVoiceClient(private val listener: Listener) {
    interface Listener {
        fun onCloudStatus(phase: String, message: String)
        fun onCloudTranscript(text: String, final: Boolean)
        fun onCloudSpeaker(opus: ByteArray, timestampMs: Long, mouthLevel: Int)
        fun onCloudError(message: String)
    }

    private val http = OkHttpClient.Builder().pingInterval(20, TimeUnit.SECONDS).build()
    private var socket: WebSocket? = null
    @Volatile private var active = false

    fun start(settings: CloudSettings.Snapshot, sarvamKey: String) {
        cancel()
        val url = settings.relayUrl.removeSuffix("/") + "/v1/voice"
        socket = http.newWebSocket(Request.Builder().url(url).build(), object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                active = true
                webSocket.send(JSONObject().apply {
                    put("type", "start")
                    put("sarvamApiKey", sarvamKey)
                    put("voice", settings.voice)
                    put("language", "en-IN")
                }.toString())
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                handle(text)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (active) listener.onCloudError("Cloud voice link failed: ${t.message ?: "network error"}")
                active = false
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) { active = false }
        })
    }

    fun sendMicFrame(frame: LocalVoicePipeline.OpusFrame) {
        if (!active) return
        socket?.send(JSONObject().apply {
            put("type", "mic_opus")
            put("timestampMs", frame.timestampMs)
            put("audio", Base64.encodeToString(frame.payload, Base64.NO_WRAP))
        }.toString())
    }

    fun finish() { if (active) socket?.send("{\"type\":\"finish\"}") }

    fun cancel() {
        active = false
        socket?.close(1000, "cancelled")
        socket = null
    }

    private fun handle(raw: String) = runCatching {
        val event = JSONObject(raw)
        when (event.getString("type")) {
            "status" -> listener.onCloudStatus(event.optString("phase", "thinking"), event.optString("message"))
            "transcript" -> listener.onCloudTranscript(event.optString("text"), event.optBoolean("final", false))
            "speaker_opus" -> listener.onCloudSpeaker(
                Base64.decode(event.getString("audio"), Base64.NO_WRAP),
                event.optLong("timestampMs", android.os.SystemClock.elapsedRealtime()),
                event.optInt("mouthLevel", 0),
            )
            "error" -> listener.onCloudError(event.optString("message", "Cloud voice failed"))
        }
    }.onFailure { listener.onCloudError("Invalid cloud voice response") }
}
