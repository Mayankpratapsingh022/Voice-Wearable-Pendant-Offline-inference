package ai.pendant.companion

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Cloud configuration. The Sarvam key is encrypted by a non-exportable Android
 * Keystore key and is intentionally never returned to Flutter or logs. */
object CloudSettings {
    data class Snapshot(
        val mode: String,
        val relayUrl: String,
        val voice: String,
        val hasApiKey: Boolean,
        val visualProfile: Int,
        val moonPhase: Int,
    ) {
        fun toMap(): Map<String, Any> = mapOf(
            "mode" to mode,
            "relayUrl" to relayUrl,
            "voice" to voice,
            "hasApiKey" to hasApiKey,
            "visualProfile" to visualProfile,
            "moonPhase" to moonPhase,
        )
    }

    fun snapshot(context: Context): Snapshot {
        val prefs = prefs(context)
        return Snapshot(
            mode = prefs.getString(MODE, "cloud").orEmpty().ifBlank { "cloud" },
            relayUrl = prefs.getString(RELAY_URL, DEFAULT_RELAY_URL).orEmpty(),
            voice = prefs.getString(VOICE, "").orEmpty(),
            hasApiKey = decrypt(prefs.getString(WRAPPED_KEY, null))?.isNotBlank() == true,
            visualProfile = prefs.getInt(VISUAL_PROFILE, 0).coerceIn(0, 3),
            moonPhase = prefs.getInt(MOON_PHASE, 100).coerceIn(0, 100),
        )
    }

    fun update(
        context: Context,
        mode: String,
        voice: String,
        apiKey: String?,
    ): Snapshot {
        require(mode == "cloud" || mode == "local")
        val editor = prefs(context).edit()
            .putString(MODE, mode)
            .putString(VOICE, voice.trim())
        if (!apiKey.isNullOrBlank()) editor.putString(WRAPPED_KEY, encrypt(apiKey.trim()))
        editor.commit()
        return snapshot(context)
    }

    fun setVisualProfile(context: Context, profile: Int, moonPhase: Int): Snapshot {
        require(profile in 0..3)
        require(moonPhase in 0..100)
        prefs(context).edit().putInt(VISUAL_PROFILE, profile).putInt(MOON_PHASE, moonPhase).commit()
        return snapshot(context)
    }

    fun apiKey(context: Context): String? = decrypt(prefs(context).getString(WRAPPED_KEY, null))

    private fun prefs(context: Context) = context.applicationContext.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)

    private fun encrypt(value: String): String {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey())
        return Base64.encodeToString(cipher.iv, Base64.NO_WRAP) + ":" +
            Base64.encodeToString(cipher.doFinal(value.encodeToByteArray()), Base64.NO_WRAP)
    }

    private fun decrypt(wrapped: String?): String? = runCatching {
        val parts = wrapped.orEmpty().split(':', limit = 2)
        require(parts.size == 2)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), GCMParameterSpec(128, Base64.decode(parts[0], Base64.NO_WRAP)))
        cipher.doFinal(Base64.decode(parts[1], Base64.NO_WRAP)).decodeToString()
    }.getOrNull()

    private fun getOrCreateKey(): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
            init(
                KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    .build(),
            )
        }.generateKey()
    }

    private const val PREFERENCES = "cloud_voice_settings"
    private const val KEY_ALIAS = "ai.pendant.companion.sarvam_key"
    private const val MODE = "mode"
    private const val RELAY_URL = "relay_url"
    // Provisioned with the companion build; it is not a user credential.
    // Kept empty in source until the one-time relay deployment is complete.
    private const val DEFAULT_RELAY_URL = ""
    private const val VOICE = "voice"
    private const val WRAPPED_KEY = "wrapped_sarvam_key"
    private const val VISUAL_PROFILE = "visual_profile"
    private const val MOON_PHASE = "moon_phase"
}
