package ai.pendant.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * Foreground service that owns the BLE link. It stays alive while the Flutter UI is
 * closed, but does not capture or archive audio outside an explicit face hold.
 */
class PendantService : Service(), BlePendantClient.Listener {
    private lateinit var client: BlePendantClient
    private lateinit var notes: EncryptedNoteStore
    private lateinit var pipeline: LocalVoicePipeline
    private val worker: ExecutorService = Executors.newSingleThreadExecutor()
    private val capturedFrames = mutableListOf<LocalVoicePipeline.OpusFrame>()
    private var capturing = false

    override fun onCreate() {
        super.onCreate()
        notes = EncryptedNoteStore(applicationContext)
        pipeline = NativeRuntimePlaceholder(ModelPack(applicationContext))
        client = BlePendantClient(applicationContext, this)
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, notification("Ready to pair"))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_PAIR -> client.startPairing()
            ACTION_START, null -> client.resumeKnownDevice()
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        client.close()
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
            VoiceLink.MIC_OPUS -> if (capturing) {
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
        when (payload.firstOrNull()) {
            VoiceLink.CAPTURE_STARTED -> {
                synchronized(capturedFrames) {
                    capturedFrames.clear()
                    capturing = true
                }
                updateNotification("Listening…")
                PendantEventBus.emit(mapOf("type" to "status", "phase" to "listening", "message" to "Listening on your pendant"))
            }
            VoiceLink.CAPTURE_STOPPED -> {
                val frames = synchronized(capturedFrames) {
                    capturing = false
                    capturedFrames.toList().also { capturedFrames.clear() }
                }
                processCapture(frames)
            }
        }
    }

    private fun processCapture(frames: List<LocalVoicePipeline.OpusFrame>) {
        if (frames.isEmpty()) {
            client.sendControl(VoiceLink.ERROR, "No audio received")
            client.sendControl(VoiceLink.IDLE)
            onError("No audio was received from Pendant")
            return
        }
        client.sendControl(VoiceLink.PROCESSING)
        updateNotification("Processing locally…")
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "thinking", "message" to "Transcribing and thinking locally…"))
        worker.execute {
            runCatching {
                pipeline.process(frames, notes.list())
            }.onSuccess(::deliverResult).onFailure { error ->
                client.sendControl(VoiceLink.ERROR, error.message ?: "Local processing failed")
                client.sendControl(VoiceLink.IDLE)
                onError(error.message ?: "Local processing failed")
            }
        }
    }

    private fun deliverResult(result: LocalVoicePipeline.Result) {
        val shouldSave = result.saveRequested && explicitlyRequestsSave(result.transcript)
        val record = if (shouldSave) {
            notes.save(result.title, result.summary, result.transcript, result.tags)
        } else {
            null
        }
        if (record != null) {
            client.sendControl(VoiceLink.SAVED, "Note saved")
            PendantEventBus.emit(
                mapOf(
                    "type" to "note_saved",
                    "phase" to "saved",
                    "message" to "Note saved locally",
                    "note" to record.toMap(),
                ),
            )
        }
        client.sendControl(VoiceLink.SPEAKING)
        updateNotification("Speaking…")
        PendantEventBus.emit(mapOf("type" to "status", "phase" to "speaking", "message" to result.reply))
        result.speech.forEach { frame ->
            client.sendSpeakerFrame(frame.opus24Khz, frame.timestampMs, frame.mouthLevel)
        }
        client.sendControl(VoiceLink.IDLE)
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

    companion object {
        private const val ACTION_START = "ai.pendant.companion.START"
        private const val ACTION_PAIR = "ai.pendant.companion.PAIR"
        private const val CHANNEL_ID = "pendant_connection"
        private const val NOTIFICATION_ID = 17

        fun start(context: Context) = launch(context, ACTION_START)
        fun pair(context: Context) = launch(context, ACTION_PAIR)

        private fun launch(context: Context, action: String) {
            context.startForegroundService(Intent(context, PendantService::class.java).setAction(action))
        }
    }
}
