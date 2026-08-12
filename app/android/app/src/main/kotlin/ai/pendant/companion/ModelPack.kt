package ai.pendant.companion

import android.content.Context
import java.io.File

/** The app never silently falls back to cloud inference. All required model files live here. */
class ModelPack(context: Context) {
    private val root = File(context.filesDir, "models")

    data class Status(
        val ready: Boolean,
        val missing: List<String>,
        val installedBytes: Long,
    ) {
        fun toMap(): Map<String, Any> = mapOf(
            "ready" to ready,
            "missing" to missing,
            "installedBytes" to installedBytes,
        )
    }

    fun status(): Status {
        val missing = REQUIRED_RELATIVE_PATHS.filterNot { File(root, it).isFile }
        return Status(missing.isEmpty(), missing, rootSize(root))
    }

    fun root(): File = root.also { it.mkdirs() }

    private fun rootSize(file: File): Long = when {
        !file.exists() -> 0L
        file.isFile -> file.length()
        else -> file.listFiles()?.sumOf(::rootSize) ?: 0L
    }

    companion object {
        /**
         * Paths are deliberately versioned. The installer must verify SHA-256 values
         * from its signed manifest before moving anything into these locations.
         */
        val REQUIRED_RELATIVE_PATHS = listOf(
            "llm/qwen3-1.7b-q8_0.gguf",
            "stt/moonshine-base-en-int8/preprocess.onnx",
            "stt/moonshine-base-en-int8/encode.int8.onnx",
            "stt/moonshine-base-en-int8/cached_decode.int8.onnx",
            "stt/moonshine-base-en-int8/uncached_decode.int8.onnx",
            "stt/moonshine-base-en-int8/tokens.txt",
            "tts/kokoro-v1.0/model.onnx",
            "tts/kokoro-v1.0/voices.bin",
        )
    }
}
