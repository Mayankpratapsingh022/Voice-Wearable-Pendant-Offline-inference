package ai.pendant.companion

import android.content.Context
import java.io.File

/** The app never silently falls back to cloud inference. All required model files live here. */
class ModelPack(context: Context) {
    private val root = File(context.filesDir, "models")

    data class Status(
        val ready: Boolean,
        val missing: List<String>,
    ) {
        fun toMap(): Map<String, Any> = mapOf("ready" to ready, "missing" to missing)
    }

    fun status(): Status {
        val missing = REQUIRED_RELATIVE_PATHS.filterNot { File(root, it).isFile }
        return Status(missing.isEmpty(), missing)
    }

    fun root(): File = root.also { it.mkdirs() }

    companion object {
        /**
         * Paths are deliberately versioned. The installer must verify SHA-256 values
         * from its signed manifest before moving anything into these locations.
         */
        val REQUIRED_RELATIVE_PATHS = listOf(
            "llm/qwen3-1.7b-q8_0.gguf",
            "stt/moonshine-base-en/encoder.onnx",
            "stt/moonshine-base-en/decoder.onnx",
            "tts/kokoro-v0.19/model.onnx",
            "tts/kokoro-v0.19/voices.bin",
            "embeddings/bge-small-en-v1.5/model.onnx",
        )
    }
}
