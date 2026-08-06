package ai.pendant.companion

/**
 * Contract for the all-local inference layer. Its implementation is intentionally
 * independent from Flutter and BLE so audio can stream while the app UI is closed.
 */
interface LocalVoicePipeline {
    data class OpusFrame(val payload: ByteArray, val timestampMs: Long)

    data class SynthesisFrame(
        val opus24Khz: ByteArray,
        val timestampMs: Long,
        val mouthLevel: Int,
    )

    data class Result(
        val transcript: String,
        val reply: String,
        val title: String,
        val summary: String,
        val tags: List<String>,
        val saveRequested: Boolean,
        val speech: List<SynthesisFrame>,
    )

    /**
     * Decode 16 kHz Opus -> streaming STT -> Qwen -> Kokoro -> 24 kHz Opus.
     * The actual implementation must remain offline; no credentials or network URL
     * appear in this contract.
     */
    fun process(frames: List<OpusFrame>, relevantNotes: List<NoteRecord>): Result
}

class ModelPackNotReadyException(missing: List<String>) : IllegalStateException(
    "Local model pack is incomplete: ${missing.joinToString()}",
)

/**
 * Safe initial runtime: it refuses to pretend that speech has been processed until
 * the native sherpa-onnx / llama.cpp / Kokoro bindings are included in the APK.
 */
class NativeRuntimePlaceholder(private val modelPack: ModelPack) : LocalVoicePipeline {
    override fun process(
        frames: List<LocalVoicePipeline.OpusFrame>,
        relevantNotes: List<NoteRecord>,
    ): LocalVoicePipeline.Result {
        val status = modelPack.status()
        if (!status.ready) throw ModelPackNotReadyException(status.missing)
        throw UnsupportedOperationException(
            "The model files are installed but the native runtime binding is not packaged. " +
                "Build the sherpa-onnx, llama.cpp and ONNX Runtime bindings before enabling capture.",
        )
    }
}

/** Saving is allowed only after a clear spoken save intent, never by automatic summarisation. */
fun explicitlyRequestsSave(transcript: String): Boolean {
    val normalized = transcript.lowercase()
    return Regex("\\b(remember|note|save)\\b").containsMatchIn(normalized)
}
