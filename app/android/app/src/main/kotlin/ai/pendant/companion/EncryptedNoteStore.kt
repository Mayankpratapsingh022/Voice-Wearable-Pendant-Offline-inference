package ai.pendant.companion

import android.content.ContentValues
import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import net.zetetic.database.sqlcipher.SQLiteDatabase
import java.security.KeyStore
import java.security.SecureRandom
import java.time.Instant
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

data class NoteRecord(
    val id: String,
    val title: String,
    val summary: String,
    val transcript: String,
    val createdAtMs: Long,
    val tags: List<String>,
) {
    fun toMap(): Map<String, Any> = mapOf(
        "id" to id,
        "title" to title,
        "summary" to summary,
        "createdAt" to Instant.ofEpochMilli(createdAtMs).toString(),
        "tags" to tags,
    )
}

/**
 * The transcript database is encrypted at rest with SQLCipher. Its random database
 * passphrase is itself protected by a non-exportable Android Keystore AES-GCM key.
 */
class EncryptedNoteStore(context: Context) {
    private val context = context.applicationContext
    private val database: SQLiteDatabase by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        System.loadLibrary("sqlcipher")
        SQLiteDatabase.openOrCreateDatabase(context.getDatabasePath(DATABASE_NAME), databasePassphrase(), null, null, null)
            .also(::createSchema)
    }

    fun save(
        title: String,
        summary: String,
        transcript: String,
        tags: List<String> = emptyList(),
        createdAtMs: Long = System.currentTimeMillis(),
    ): NoteRecord {
        val record = NoteRecord(
            id = java.util.UUID.randomUUID().toString(),
            title = title.trim().ifBlank { "Untitled note" },
            summary = summary.trim(),
            transcript = transcript.trim(),
            createdAtMs = createdAtMs,
            tags = tags.map(String::trim).filter(String::isNotBlank).distinct(),
        )
        val values = ContentValues().apply {
            put("note_id", record.id)
            put("title", record.title)
            put("summary", record.summary)
            put("transcript", record.transcript)
            put("tags", record.tags.joinToString(","))
            put("created_at_ms", record.createdAtMs)
        }
        database.insertOrThrow("notes", null, values)
        return record
    }

    fun list(query: String = ""): List<NoteRecord> {
        val normalizedQuery = query.trim()
        val selection = if (normalizedQuery.isEmpty()) null else {
            "LOWER(title) LIKE ? OR LOWER(summary) LIKE ? OR LOWER(transcript) LIKE ?"
        }
        val selectionArgs = if (normalizedQuery.isEmpty()) null else {
            val contains = "%${normalizedQuery.lowercase()}%"
            arrayOf(contains, contains, contains)
        }
        val cursor = database.query(
            "notes",
            arrayOf("note_id", "title", "summary", "transcript", "tags", "created_at_ms"),
            selection,
            selectionArgs,
            null,
            null,
            "created_at_ms DESC",
        )
        return cursor.use {
            buildList {
                while (it.moveToNext()) {
                    add(
                        NoteRecord(
                            id = it.getString(0),
                            title = it.getString(1),
                            summary = it.getString(2),
                            transcript = it.getString(3),
                            tags = it.getString(4).orEmpty().split(',').filter(String::isNotBlank),
                            createdAtMs = it.getLong(5),
                        ),
                    )
                }
            }
        }
    }

    private fun createSchema(db: SQLiteDatabase) {
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS notes (
                row_id INTEGER PRIMARY KEY AUTOINCREMENT,
                note_id TEXT NOT NULL UNIQUE,
                title TEXT NOT NULL,
                summary TEXT NOT NULL,
                transcript TEXT NOT NULL,
                tags TEXT NOT NULL,
                created_at_ms INTEGER NOT NULL
            )
            """.trimIndent(),
        )
        db.execSQL("CREATE INDEX IF NOT EXISTS notes_created_at ON notes(created_at_ms DESC)")
    }

    private fun databasePassphrase(): String {
        val preferences = context.getSharedPreferences(KEY_PREFERENCES, Context.MODE_PRIVATE)
        val existing = preferences.getString(KEY_WRAPPED_PASSPHRASE, null)?.let(::unwrap)
        if (existing != null) return existing
        val raw = ByteArray(32).also(SecureRandom()::nextBytes)
        val passphrase = Base64.encodeToString(raw, Base64.NO_WRAP)
        preferences.edit().putString(KEY_WRAPPED_PASSPHRASE, wrap(passphrase)).commit()
        return passphrase
    }

    private fun wrap(value: String): String {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKeystoreKey())
        val ciphertext = cipher.doFinal(value.encodeToByteArray())
        return Base64.encodeToString(cipher.iv, Base64.NO_WRAP) + ":" +
            Base64.encodeToString(ciphertext, Base64.NO_WRAP)
    }

    private fun unwrap(wrapped: String): String? = runCatching {
        val parts = wrapped.split(':', limit = 2)
        require(parts.size == 2)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(
            Cipher.DECRYPT_MODE,
            getOrCreateKeystoreKey(),
            GCMParameterSpec(128, Base64.decode(parts[0], Base64.NO_WRAP)),
        )
        cipher.doFinal(Base64.decode(parts[1], Base64.NO_WRAP)).decodeToString()
    }.getOrNull()

    private fun getOrCreateKeystoreKey(): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
            init(
                KeyGenParameterSpec.Builder(
                    KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                )
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    .build(),
            )
        }.generateKey()
    }

    private companion object {
        const val DATABASE_NAME = "pendant_notes.db"
        const val KEY_PREFERENCES = "pendant_database_key"
        const val KEY_WRAPPED_PASSPHRASE = "wrapped_passphrase"
        const val KEY_ALIAS = "ai.pendant.companion.note_database"
    }
}
