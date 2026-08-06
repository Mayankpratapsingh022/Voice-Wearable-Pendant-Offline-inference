package ai.pendant.companion

import android.os.Handler
import android.os.Looper
import io.flutter.plugin.common.EventChannel

object PendantEventBus : EventChannel.StreamHandler {
    private val mainHandler = Handler(Looper.getMainLooper())
    private var sink: EventChannel.EventSink? = null

    override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
        sink = events
    }

    override fun onCancel(arguments: Any?) {
        sink = null
    }

    fun emit(
        type: String,
        message: String,
        connection: String? = null,
        phase: String? = null,
        note: NoteRecord? = null,
    ) {
        val payload = linkedMapOf<String, Any?>(
            "type" to type,
            "message" to message,
            "connection" to connection,
            "phase" to phase,
            "note" to note?.toMap(),
        )
        mainHandler.post { sink?.success(payload) }
    }

    fun emit(payload: Map<String, Any?>) {
        mainHandler.post { sink?.success(payload) }
    }
}
