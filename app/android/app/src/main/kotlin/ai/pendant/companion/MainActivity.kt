package ai.pendant.companion

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private var pendingPermissionResult: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, SERVICE_CHANNEL)
            .setMethodCallHandler(::onMethodCall)
        EventChannel(flutterEngine.dartExecutor.binaryMessenger, EVENT_CHANNEL)
            .setStreamHandler(PendantEventBus)
    }

    private fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "requestBluetoothPermissions" -> requestNetworkPermissions(result)
            "start" -> {
                PendantService.start(this)
                result.success(null)
            }
            "startPairing" -> {
                PendantService.pair(this)
                result.success(null)
            }
            "pairFromQr" -> {
                val code = call.argument<String>("code").orEmpty().trim()
                // The MAC suffix is case-insensitive; the QR password is not.
                val match = Regex("^PENDANT:W:([0-9A-Fa-f]{6}):(.{8,63})$").matchEntire(code)
                if (match == null) {
                    result.error("invalid_qr", "This is not a Pendant Wi-Fi QR code", null)
                } else {
                    PendantService.connectWifiAp(this, "PENDANT-${match.groupValues[1].uppercase()}", match.groupValues[2])
                    result.success(null)
                }
            }
            "provisionWifi" -> {
                val ssid = call.argument<String>("ssid").orEmpty()
                val password = call.argument<String>("password").orEmpty()
                PendantService.provisionWifi(this, ssid, password)
                result.success(null)
            }
            "listNotes" -> {
                val query = call.argument<String>("query").orEmpty()
                result.success(EncryptedNoteStore(applicationContext).list(query).map(NoteRecord::toMap))
            }
            "modelStatus" -> result.success(ModelPack(applicationContext).status().toMap() + ModelDownloadManager.snapshot())
            "downloadModels" -> {
                ModelDownloadManager.start(applicationContext)
                result.success(null)
            }
            "cloudSettings" -> result.success(CloudSettings.snapshot(applicationContext).toMap())
            "saveCloudSettings" -> {
                val settings = CloudSettings.update(
                    applicationContext,
                    mode = call.argument<String>("mode").orEmpty().ifBlank { "cloud" },
                    voice = call.argument<String>("voice").orEmpty(),
                    apiKey = call.argument<String>("apiKey"),
                )
                result.success(settings.toMap())
            }
            "setVisualProfile" -> {
                val profile = call.argument<Int>("profile") ?: -1
                val moonPhase = call.argument<Int>("moonPhase") ?: 100
                if (profile !in 0..3 || moonPhase !in 0..100) {
                    result.error("invalid_profile", "Unknown screensaver profile", null)
                } else {
                    CloudSettings.setVisualProfile(applicationContext, profile, moonPhase)
                    PendantService.setVisualProfile(this, profile, moonPhase)
                    result.success(null)
                }
            }
            else -> result.notImplemented()
        }
    }

    private fun requestNetworkPermissions(result: MethodChannel.Result) {
        val permissions = buildList {
            add(Manifest.permission.ACCESS_FINE_LOCATION)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) add(Manifest.permission.NEARBY_WIFI_DEVICES)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                add(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
        if (permissions.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }) {
            result.success(null)
            return
        }
        pendingPermissionResult = result
        requestPermissions(permissions.toTypedArray(), BLUETOOTH_PERMISSION_REQUEST)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == BLUETOOTH_PERMISSION_REQUEST) {
            val granted = grantResults.all { it == PackageManager.PERMISSION_GRANTED }
            pendingPermissionResult?.let { result ->
                if (granted) result.success(null) else result.error("permission_denied", "Wi-Fi permission is required", null)
            }
            pendingPermissionResult = null
        }
    }

    private companion object {
        const val SERVICE_CHANNEL = "ai.pendant/service"
        const val EVENT_CHANNEL = "ai.pendant/events"
        const val BLUETOOTH_PERMISSION_REQUEST = 420
    }
}
