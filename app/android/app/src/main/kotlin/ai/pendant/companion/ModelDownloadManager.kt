package ai.pendant.companion

import android.content.Context
import android.os.StatFs
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.concurrent.Executors

/**
 * Installs fixed, immutable Hugging Face revisions. Files are first written to
 * a private .part file and become visible to the runtime only after download
 * and (where supplied) SHA-256 validation succeeds.
 */
object ModelDownloadManager {
    private data class Artifact(
        val relativePath: String,
        val url: String,
        val expectedSha256: String? = null,
    )

    private val artifacts = listOf(
        Artifact(
            "llm/qwen3-1.7b-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/resolve/61cf2fa/Qwen3-1.7B-Q8_0.gguf?download=true",
            "061b54daade076b5d3362dac252678d17da8c68f07560be70818cace6590cb1a",
        ),
        Artifact("stt/moonshine-base-en-int8/preprocess.onnx", "https://huggingface.co/csukuangfj/sherpa-onnx-moonshine-base-en-int8/resolve/052b0798ad1bf046a140fdd4efcd9426530fa3f5/preprocess.onnx?download=true"),
        Artifact("stt/moonshine-base-en-int8/encode.int8.onnx", "https://huggingface.co/csukuangfj/sherpa-onnx-moonshine-base-en-int8/resolve/052b0798ad1bf046a140fdd4efcd9426530fa3f5/encode.int8.onnx?download=true"),
        Artifact("stt/moonshine-base-en-int8/cached_decode.int8.onnx", "https://huggingface.co/csukuangfj/sherpa-onnx-moonshine-base-en-int8/resolve/052b0798ad1bf046a140fdd4efcd9426530fa3f5/cached_decode.int8.onnx?download=true"),
        Artifact("stt/moonshine-base-en-int8/uncached_decode.int8.onnx", "https://huggingface.co/csukuangfj/sherpa-onnx-moonshine-base-en-int8/resolve/052b0798ad1bf046a140fdd4efcd9426530fa3f5/uncached_decode.int8.onnx?download=true"),
        Artifact("stt/moonshine-base-en-int8/tokens.txt", "https://huggingface.co/csukuangfj/sherpa-onnx-moonshine-base-en-int8/resolve/052b0798ad1bf046a140fdd4efcd9426530fa3f5/tokens.txt?download=true"),
        Artifact("tts/kokoro-v1.0/model.onnx", "https://huggingface.co/huangjune/Kokoro-82M-v1.0-ONNX/resolve/d3f5dac/model.onnx?download=true"),
        Artifact("tts/kokoro-v1.0/voices.bin", "https://huggingface.co/huangjune/Kokoro-82M-v1.0-ONNX/resolve/d3f5dac/voices.bin?download=true"),
    )

    private const val ESTIMATED_BYTES = 2_650_000_000L
    private val executor = Executors.newSingleThreadExecutor()
    private val lock = Any()
    private var downloading = false
    private var message = "Models have not been downloaded"
    private var downloadedBytes = 0L
    private var totalBytes = ESTIMATED_BYTES
    private var lastProgressEmitMs = 0L

    fun snapshot(): Map<String, Any> = synchronized(lock) {
        mapOf(
            "downloading" to downloading,
            "downloadMessage" to message,
            "downloadedBytes" to downloadedBytes,
            "totalBytes" to totalBytes,
        )
    }

    fun start(context: Context) {
        synchronized(lock) {
            if (downloading) return
            val stat = StatFs(context.filesDir.absolutePath)
            if (stat.availableBytes < ESTIMATED_BYTES) {
                message = "Need about 2.7 GB free app storage before downloading models"
                emit()
                return
            }
            downloading = true
            downloadedBytes = 0L
            totalBytes = ESTIMATED_BYTES
            message = "Preparing verified Hugging Face model download…"
        }
        emit()
        executor.execute {
            try {
                val root = ModelPack(context).root()
                artifacts.forEachIndexed { index, artifact ->
                    val destination = File(root, artifact.relativePath)
                    if (destination.isFile && verifyIfRequired(destination, artifact.expectedSha256)) {
                        addCompleted(destination.length())
                        return@forEachIndexed
                    }
                    update("Downloading ${index + 1} of ${artifacts.size}: ${destination.name}")
                    download(artifact, destination)
                }
                synchronized(lock) {
                    downloading = false
                    message = "Model files downloaded. Native voice engine setup is still required."
                }
            } catch (error: Exception) {
                synchronized(lock) {
                    downloading = false
                    message = "Model download failed: ${error.message ?: "network error"}"
                }
            }
            emit()
        }
    }

    private fun download(artifact: Artifact, destination: File) {
        destination.parentFile?.mkdirs()
        val temporary = File(destination.parentFile, "${destination.name}.part")
        val connection = (URL(artifact.url).openConnection() as HttpURLConnection).apply {
            instanceFollowRedirects = true
            connectTimeout = 20_000
            readTimeout = 30_000
            setRequestProperty("Accept", "application/octet-stream")
        }
        connection.connect()
        check(connection.responseCode in 200..299) { "Hugging Face returned HTTP ${connection.responseCode}" }
        val digest = MessageDigest.getInstance("SHA-256")
        connection.inputStream.use { input ->
            BufferedInputStream(input).use { source ->
                BufferedOutputStream(FileOutputStream(temporary, false)).use { output ->
                    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                    while (true) {
                        val count = source.read(buffer)
                        if (count < 0) break
                        output.write(buffer, 0, count)
                        digest.update(buffer, 0, count)
                        addCompleted(count.toLong())
                    }
                }
            }
        }
        connection.disconnect()
        val actual = digest.digest().joinToString("") { "%02x".format(it) }
        check(artifact.expectedSha256 == null || actual.equals(artifact.expectedSha256, ignoreCase = true)) {
            "Integrity check failed for ${destination.name}"
        }
        check(temporary.renameTo(destination)) { "Could not install ${destination.name}" }
    }

    private fun verifyIfRequired(file: File, expected: String?): Boolean {
        if (expected == null) return true
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }.equals(expected, ignoreCase = true)
    }

    private fun addCompleted(bytes: Long) {
        val shouldEmit = synchronized(lock) {
            downloadedBytes += bytes
            val now = System.currentTimeMillis()
            if (now - lastProgressEmitMs >= 350L) {
                lastProgressEmitMs = now
                true
            } else {
                false
            }
        }
        if (shouldEmit) emit()
    }

    private fun update(value: String) = synchronized(lock) { message = value }.also { emit() }

    private fun emit() = PendantEventBus.emit(mapOf("type" to "model", "message" to snapshot()["downloadMessage"].toString()))
}
