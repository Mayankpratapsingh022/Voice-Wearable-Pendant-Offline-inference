package ai.pendant.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * Foreground service that owns the BLE link. It stays alive while the Flutter UI is
 * closed, but does not capture or archive audio outside an explicit face hold.
 */
class PendantService : Service(), BlePendantClient.Listener {
    private enum class CaptureMode { IDLE, ASSISTANT, MEETING }

    private lateinit var client: BlePendantClient
    private lateinit var wifiClient: WifiPendantClient
    private lateinit var notes: EncryptedNoteStore
    private lateinit var pipeline: LocalVoicePipeline
    private val worker: ExecutorService = Executors.newSingleThreadExecutor()
    private val capturedFrames = mutableListOf<LocalVoicePipeline.OpusFrame>()
    private var capturing = false
    private var captureMode = CaptureMode.IDLE

    override fun onCreate() {
        super.onCreate()
        notes = EncryptedNoteStore(applicationContext)
        pipeline = NativeRuntimePlaceholder(ModelPack(applicationContext))
        client = BlePendantClient(applicationContext, this)
        wifiClient = WifiPendantClient(object : WifiPendantClient.Listener {
            override fun onWifiPacket(packet: VoiceLink.Packet) = onPacket(packet)
            override fun onWifiChanged(connected: Boolean, message: String) {
                updateNotification(message)
                PendantEventBus.emit(mapOf("type" to "transport", "message" to message))
            }
            override fun onWifiError(message: String) = onError(message)
        })
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, notification("Ready to pair"))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_PAIR -> client.startPairing()
            ACTION_WIFI_PROVISION -> {
                val ssid = intent.getStringExtra(EXTRA_WIFI_SSID).orEmpty()
                val password = intent.getStringExtra(EXTRA_WIFI_PASSWORD).orEmpty()
                if (ssid.isBlank()) onError("Enter your phone hotspot name")
                else runCatching { client.sendWifiProvision(ssid, password) }
                    .onSuccess { trace("Sent phone-hotspot setup to pendant") }
                    .onFailure { onError(it.message ?: "Could not send Wi‑Fi setup to pendant") }
            }
            ACTION_START, null -> client.resumeKnownDevice()
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        client.close()
        wifiClient.close()
        worker.shutdownNow()
        super.onDestroy()
    }

    override fun onConnectionChanged(connection: BlePendantClient.Connection) {
        val message = when (connection) {
            BlePendantClient.Connection.PAIRING -> "Looking for Pendant…"
            BlePendantClient.Connection.CONNECTING -> "Connecting to Pendant…"
            BlePendantClient.Connection.CONNECTED -> "Pendant connected"
            BlePendantClient.Connection.DISCONNECTED -> "Pendant disconnected"
        }
        updateNotification(message)
        PendantEventBus.emit(mapOf("type" to "connection", "connection" to connection.name.lowercase(), "message" to message))
    }

    override fun onPacket(packet: VoiceLink.Packet) {
        when (packet.kind) {
            VoiceLink.CONTROL -> onControl(packet.payload)
            VoiceLink.MIC_OPUS -> if (capturing && captureMode != CaptureMode.IDLE) {
                synchronized(capturedFrames) {
                    capturedFrames += LocalVoicePipeline.OpusFrame(packet.payload.copyOf(), packet.timestampMs)
                }
            }
        }
    }

    override fun onError(message: String) {
        updateNotification(message)
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "error", "message" to message))
    }

    private fun onControl(payload: ByteArray) {
        Log.i(LOG_TAG, "control opcode=0x${payload.firstOrNull()?.toUByte()?.toString(16) ?: "--"}")
        when (payload.firstOrNull()) {
            VoiceLink.WIFI_READY -> {
                val endpoint = payload.drop(1).toByteArray().decodeToString()
                if (endpoint.matches(Regex("\\d{1,3}(?:\\.\\d{1,3}){3}"))) {
                    trace("Pendant joined hotspot at $endpoint; opening Wi‑Fi voice link")
                    wifiClient.connect(endpoint)
                } else {
                    trace("Wi‑Fi setup: $endpoint")
                }
            }
            VoiceLink.WIFI_FAILED -> onError(payload.drop(1).toByteArray().decodeToString().ifBlank { "Pendant Wi‑Fi setup failed" })
            VoiceLink.ASSISTANT_REQUEST -> {
                trace("Pendant PWR request received")
                beginAssistantTurn()
            }
            VoiceLink.ASSISTANT_CANCEL -> {
                trace("Pendant cancelled the assistant request")
                cancelAssistantTurn()
            }
            VoiceLink.MEETING_TOGGLE -> {
                trace("Pendant meeting button request received")
                toggleMeeting()
            }
            VoiceLink.CAPTURE_STARTED -> {
                synchronized(capturedFrames) {
                    // Face touch remains compatible with the original protocol.
                    if (captureMode == CaptureMode.IDLE) captureMode = CaptureMode.ASSISTANT
                    capturedFrames.clear()
                    capturing = true
                }
                val message = if (captureMode == CaptureMode.MEETING) "Meeting capture in progress" else "Listening on your pendant"
                updateNotification(message)
                PendantEventBus.emit(mapOf("type" to "status", "phase" to "listening", "message" to message))
                trace("Audio capture started; streaming Opus frames from pendant")
            }
            VoiceLink.CAPTURE_STOPPED -> {
                val (mode, frames) = synchronized(capturedFrames) {
                    capturing = false
                    captureMode to capturedFrames.toList().also {
                        capturedFrames.clear()
                        captureMode = CaptureMode.IDLE
                    }
                }
                trace("Audio capture stopped; received ${frames.size} audio frames")
                if (mode == CaptureMode.MEETING) processMeeting(frames) else if (mode == CaptureMode.ASSISTANT) processCapture(frames)
            }
        }
    }

    private fun beginAssistantTurn() {
        if (captureMode != CaptureMode.IDLE || capturing) {
            sendControl(VoiceLink.UNAVAILABLE, "Voice is busy")
            return
        }
        synchronized(capturedFrames) {
            captureMode = CaptureMode.ASSISTANT
            capturedFrames.clear()
        }
        sendControl(VoiceLink.ASSISTANT_ACCEPTED)
        updateNotification("Starting local assistant…")
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "starting", "message" to "Starting assistant"))
    }

    private fun cancelAssistantTurn() {
        synchronized(capturedFrames) {
            if (captureMode == CaptureMode.ASSISTANT) {
                captureMode = CaptureMode.IDLE
                capturing = false
                capturedFrames.clear()
            }
        }
        sendControl(VoiceLink.IDLE)
        updateNotification("Pendant connected")
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "idle", "message" to "Assistant cancelled"))
    }

    private fun toggleMeeting() {
        if (captureMode == CaptureMode.MEETING) {
            sendControl(VoiceLink.MEETING_STOPPED)
            updateNotification("Finishing meeting…")
            PendantEventBus.emit(mapOf("type" to "status", "phase" to "meeting_stopping", "message" to "Finishing meeting"))
            return
        }
        if (captureMode != CaptureMode.IDLE || capturing) {
            sendControl(VoiceLink.UNAVAILABLE, "Voice is busy")
            return
        }
        synchronized(capturedFrames) {
            captureMode = CaptureMode.MEETING
            capturedFrames.clear()
        }
        sendControl(VoiceLink.MEETING_STARTED)
        updateNotification("Meeting capture in progress")
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "meeting", "message" to "Meeting capture started"))
    }

    private fun processCapture(frames: List<LocalVoicePipeline.OpusFrame>) {
        if (frames.isEmpty()) {
            sendControl(VoiceLink.ERROR, "No audio received")
            sendControl(VoiceLink.IDLE)
            onError("No audio was received from Pendant")
            return
        }
        sendControl(VoiceLink.PROCESSING)
        updateNotification("Processing locally…")
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "transcribing", "message" to "Moonshine STT: transcribing audio…"))
        trace("STT stage started with ${frames.size} audio frames")
        worker.execute {
            runCatching {
                PendantEventBus.emit(mapOf("type" to "status", "phase" to "thinking", "message" to "Qwen LLM: generating reply…"))
                trace("LLM stage started")
                pipeline.process(frames, notes.list())
            }.onSuccess(::deliverResult).onFailure { error ->
                sendControl(VoiceLink.ERROR, error.message ?: "Local processing failed")
                sendControl(VoiceLink.IDLE)
                onError(error.message ?: "Local processing failed")
            }
        }
    }

    /**
     * Audio is held in memory only until the offline pipeline returns a
     * transcript. The store receives transcript/summary, never Opus frames.
     */
    private fun processMeeting(frames: List<LocalVoicePipeline.OpusFrame>) {
        if (frames.isEmpty()) {
            sendControl(VoiceLink.ERROR, "No meeting audio received")
            sendControl(VoiceLink.IDLE)
            onError("No meeting audio was received from Pendant")
            return
        }
        sendControl(VoiceLink.PROCESSING)
        updateNotification("Summarising meeting locally…")
        worker.execute {
            runCatching { pipeline.process(frames, notes.list()) }
                .onSuccess { result ->
                    val record = notes.save(
                        title = "Meeting — ${result.title}",
                        summary = result.summary,
                        transcript = result.transcript,
                        tags = (result.tags + "meeting").distinct(),
                    )
                    sendControl(VoiceLink.SAVED, "Meeting saved")
                    sendControl(VoiceLink.IDLE)
                    updateNotification("Pendant connected")
                    PendantEventBus.emit(
                        mapOf("type" to "meeting_saved", "phase" to "saved", "message" to "Meeting saved locally", "note" to record.toMap()),
                    )
                }
                .onFailure { error ->
                    sendControl(VoiceLink.ERROR, error.message ?: "Meeting processing failed")
                    sendControl(VoiceLink.IDLE)
                    onError(error.message ?: "Meeting processing failed")
                }
        }
    }

    private fun deliverResult(result: LocalVoicePipeline.Result) {
        trace("STT transcript complete (${result.transcript.length} characters)")
        val shouldSave = result.saveRequested && explicitlyRequestsSave(result.transcript)
        val record = if (shouldSave) {
            notes.save(result.title, result.summary, result.transcript, result.tags)
        } else {
            null
        }
        if (record != null) {
            sendControl(VoiceLink.SAVED, "Note saved")
            PendantEventBus.emit(
                mapOf(
                    "type" to "note_saved",
                    "phase" to "saved",
                    "message" to "Note saved locally",
                    "note" to record.toMap(),
                ),
            )
        }
        sendControl(VoiceLink.SPEAKING)
        updateNotification("Speaking…")
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "speaking", "message" to result.reply))
        trace("Kokoro TTS: streaming ${result.speech.size} audio frames")
        result.speech.forEach { frame ->
            sendSpeakerFrame(frame.opus24Khz, frame.timestampMs, frame.mouthLevel)
        }
        sendControl(VoiceLink.IDLE)
        updateNotification("Pendant connected")
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "idle", "message" to "Hold the pendant face to talk"))
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(CHANNEL_ID, "Pendant connection", NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    private fun notification(content: String): Notification = Notification.Builder(this, CHANNEL_ID)
        .setSmallIcon(ai.pendant.companion.R.drawable.notification_icon)
        .setContentTitle("Pendant")
        .setContentText(content)
        .setOngoing(true)
        .build()

    private fun updateNotification(content: String) {
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification(content))
    }

    private fun trace(message: String) {
        Log.i(LOG_TAG, message)
        PendantEventBus.emit(mapOf("type" to "trace", "message" to message))
    }

    private fun sendControl(opcode: Byte, message: String? = null) {
        if (wifiClient.isConnected()) wifiClient.sendControl(opcode, message) else client.sendControl(opcode, message)
    }

    private fun sendSpeakerFrame(opus: ByteArray, timestampMs: Long, mouthLevel: Int) {
        if (wifiClient.isConnected()) wifiClient.sendSpeakerFrame(opus, timestampMs, mouthLevel)
        else client.sendSpeakerFrame(opus, timestampMs, mouthLevel)
    }

    companion object {
        private const val ACTION_START = "ai.pendant.companion.START"
        private const val ACTION_PAIR = "ai.pendant.companion.PAIR"
        private const val ACTION_WIFI_PROVISION = "ai.pendant.companion.WIFI_PROVISION"
        private const val EXTRA_WIFI_SSID = "wifi_ssid"
        private const val EXTRA_WIFI_PASSWORD = "wifi_password"
        private const val CHANNEL_ID = "pendant_connection"
        private const val NOTIFICATION_ID = 17
        private const val LOG_TAG = "PendantVoice"

        fun start(context: Context) = launch(context, ACTION_START)
        fun pair(context: Context) = launch(context, ACTION_PAIR)
        fun provisionWifi(context: Context, ssid: String, password: String) =
            launch(context, ACTION_WIFI_PROVISION, Intent(context, PendantService::class.java)
                .putExtra(EXTRA_WIFI_SSID, ssid).putExtra(EXTRA_WIFI_PASSWORD, password))

        private fun launch(context: Context, action: String, intent: Intent = Intent(context, PendantService::class.java)) {
            context.startForegroundService(intent.setAction(action))
        }
    }
}
