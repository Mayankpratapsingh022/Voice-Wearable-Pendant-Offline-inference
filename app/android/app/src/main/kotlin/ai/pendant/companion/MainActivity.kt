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
            "requestBluetoothPermissions" -> requestBluetoothPermissions(result)
            "start" -> {
                PendantService.start(this)
                result.success(null)
            }
            "startPairing" -> {
                PendantService.pair(this)
                result.success(null)
            }
            "listNotes" -> {
                val query = call.argument<String>("query").orEmpty()
                result.success(EncryptedNoteStore(applicationContext).list(query).map(NoteRecord::toMap))
            }
            else -> result.notImplemented()
        }
    }

    private fun requestBluetoothPermissions(result: MethodChannel.Result) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            result.success(null)
            return
        }
        val permissions = buildList {
            add(Manifest.permission.BLUETOOTH_SCAN)
            add(Manifest.permission.BLUETOOTH_CONNECT)
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
                if (granted) result.success(null) else result.error("permission_denied", "Bluetooth permission is required", null)
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
