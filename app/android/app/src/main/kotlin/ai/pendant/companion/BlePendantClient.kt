package ai.pendant.companion

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import java.util.ArrayDeque
import java.util.UUID

/** Android-central implementation of the private Voice Link BLE service. */
class BlePendantClient(
    context: Context,
    private val listener: Listener,
) {
    interface Listener {
        fun onConnectionChanged(connection: Connection)
        fun onPacket(packet: VoiceLink.Packet)
        fun onError(message: String)
    }

    enum class Connection { PAIRING, DISCONNECTED, CONNECTING, CONNECTED }

    private val context = context.applicationContext
    private val manager = context.getSystemService(BluetoothManager::class.java)
    private val preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
    private var scanner: BluetoothLeScanner? = null
    private var gatt: BluetoothGatt? = null
    private var control: BluetoothGattCharacteristic? = null
    private var microphone: BluetoothGattCharacteristic? = null
    private var speaker: BluetoothGattCharacteristic? = null
    private var telemetry: BluetoothGattCharacteristic? = null
    private val notificationQueue = ArrayDeque<BluetoothGattCharacteristic>()
    private val reconnectHandler = Handler(Looper.getMainLooper())
    private var reconnectAttempts = 0
    private var reconnectEnabled = false
    private val reconnectRunnable = Runnable {
        if (reconnectEnabled) resumeKnownDevice()
    }

    @SuppressLint("MissingPermission")
    fun resumeKnownDevice() {
        reconnectEnabled = true
        val address = preferences.getString(KEY_DEVICE_ADDRESS, null) ?: run {
            listener.onConnectionChanged(Connection.DISCONNECTED)
            return
        }
        if (!hasConnectPermission()) {
            listener.onError("Bluetooth permission is not granted")
            return
        }
        val adapter = manager?.adapter ?: run {
            listener.onError("Bluetooth is not available")
            return
        }
        listener.onConnectionChanged(Connection.CONNECTING)
        gatt = adapter.getRemoteDevice(address).connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    @SuppressLint("MissingPermission")
    fun startPairing() {
        if (!hasScanPermission()) {
            listener.onError("Bluetooth scan permission is not granted")
            return
        }
        val adapter = manager?.adapter ?: run {
            listener.onError("Bluetooth is not available")
            return
        }
        if (!adapter.isEnabled) {
            listener.onError("Turn Bluetooth on to pair the pendant")
            return
        }
        stopScan()
        reconnectEnabled = true
        cancelReconnect()
        listener.onConnectionChanged(Connection.PAIRING)
        scanner = adapter.bluetoothLeScanner
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE_UUID)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner?.startScan(listOf(filter), settings, scanCallback)
    }

    @SuppressLint("MissingPermission")
    fun sendControl(opcode: Byte, message: String? = null) {
        val messageBytes = message.orEmpty().encodeToByteArray().take(VoiceLink.MAX_PAYLOAD - 1).toByteArray()
        val payload = byteArrayOf(opcode) + messageBytes
        send(control, VoiceLink.Packet(VoiceLink.CONTROL, nextSequence(), nowMs(), 0, payload), false)
    }

    @SuppressLint("MissingPermission")
    fun sendWifiProvision(ssid: String, password: String) {
        val ssidBytes = ssid.encodeToByteArray()
        val passwordBytes = password.encodeToByteArray()
        require(ssidBytes.isNotEmpty() && ssidBytes.size <= 32)
        require(passwordBytes.size <= 63)
        val payload = byteArrayOf(VoiceLink.WIFI_PROVISION, ssidBytes.size.toByte(), passwordBytes.size.toByte()) + ssidBytes + passwordBytes
        send(control, VoiceLink.Packet(VoiceLink.CONTROL, nextSequence(), nowMs(), 0, payload), false)
    }

    /** Sends a 20 ms 24 kHz Opus frame. The device uses [mouthLevel] on the same playback timeline. */
    @SuppressLint("MissingPermission")
    fun sendSpeakerFrame(opus: ByteArray, timestampMs: Long, mouthLevel: Int) {
        send(speaker, VoiceLink.Packet(VoiceLink.SPEAKER_OPUS, nextSequence(), timestampMs, mouthLevel, opus), true)
    }

    @SuppressLint("MissingPermission")
    fun close(forgetDevice: Boolean = false) {
        reconnectEnabled = false
        cancelReconnect()
        stopScan()
        gatt?.close()
        gatt = null
        clearCharacteristics()
        if (forgetDevice) preferences.edit().remove(KEY_DEVICE_ADDRESS).apply()
        listener.onConnectionChanged(Connection.DISCONNECTED)
    }

    @SuppressLint("MissingPermission")
    private fun connect(device: BluetoothDevice) {
        stopScan()
        gatt?.close()
        listener.onConnectionChanged(Connection.CONNECTING)
        reconnectEnabled = true
        gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        scanner?.stopScan(scanCallback)
        scanner = null
    }

    @SuppressLint("MissingPermission")
    private fun send(
        characteristic: BluetoothGattCharacteristic?,
        packet: VoiceLink.Packet,
        noResponse: Boolean,
    ) {
        val activeGatt = gatt
        if (activeGatt == null || characteristic == null) {
            listener.onError("Pendant is not connected")
            return
        }
        val value = VoiceLink.encode(packet)
        val status = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            activeGatt.writeCharacteristic(
                characteristic,
                value,
                if (noResponse) BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE else BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
            )
        } else {
            characteristic.writeType = if (noResponse) {
                BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            } else {
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            }
            characteristic.value = value
            @Suppress("DEPRECATION") if (activeGatt.writeCharacteristic(characteristic)) {
                BluetoothGatt.GATT_SUCCESS
            } else {
                BluetoothGatt.GATT_FAILURE
            }
        }
        if (status != BluetoothGatt.GATT_SUCCESS) listener.onError("BLE write rejected: $status")
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (result.scanRecord?.serviceUuids?.contains(ParcelUuid(SERVICE_UUID)) == true) {
                connect(result.device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            listener.onError("BLE scan failed: $errorCode")
            listener.onConnectionChanged(Connection.DISCONNECTED)
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                preferences.edit().putString(KEY_DEVICE_ADDRESS, gatt.device.address).apply()
                reconnectAttempts = 0
                cancelReconnect()
                listener.onConnectionChanged(Connection.CONNECTED)
                gatt.requestMtu(247)
                return
            }
            clearCharacteristics()
            listener.onConnectionChanged(Connection.DISCONNECTED)
            gatt.close()
            scheduleReconnect()
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) listener.onError("BLE MTU negotiation failed: $status")
            gatt.discoverServices()
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                listener.onError("Pendant services were not discovered: $status")
                return
            }
            val service = gatt.getService(SERVICE_UUID) ?: run {
                listener.onError("The connected device is not a compatible Pendant")
                return
            }
            val controlCharacteristic = service.getCharacteristic(CONTROL_UUID)
            val microphoneCharacteristic = service.getCharacteristic(MICROPHONE_UUID)
            val speakerCharacteristic = service.getCharacteristic(SPEAKER_UUID)
            val telemetryCharacteristic = service.getCharacteristic(TELEMETRY_UUID)
            if (controlCharacteristic == null || microphoneCharacteristic == null ||
                speakerCharacteristic == null || telemetryCharacteristic == null) {
                listener.onError("Pendant Voice Link service is incomplete")
                return
            }
            control = controlCharacteristic
            microphone = microphoneCharacteristic
            speaker = speakerCharacteristic
            telemetry = telemetryCharacteristic
            notificationQueue.clear()
            notificationQueue.add(controlCharacteristic)
            notificationQueue.add(microphoneCharacteristic)
            notificationQueue.add(telemetryCharacteristic)
            enableNextNotification(gatt)
        }

        @SuppressLint("MissingPermission")
        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) listener.onError("BLE notification setup failed: $status")
            enableNextNotification(gatt)
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            handleNotification(value)
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            handleNotification(characteristic.value ?: return)
        }
    }

    @SuppressLint("MissingPermission")
    private fun enableNextNotification(activeGatt: BluetoothGatt) {
        if (notificationQueue.isEmpty()) return
        val characteristic = notificationQueue.removeFirst()
        if (!activeGatt.setCharacteristicNotification(characteristic, true)) {
            listener.onError("Could not subscribe to pendant updates")
            enableNextNotification(activeGatt)
            return
        }
        val descriptor = characteristic.getDescriptor(CLIENT_CHARACTERISTIC_CONFIG_UUID)
        if (descriptor == null) {
            listener.onError("Pendant characteristic has no notification descriptor")
            enableNextNotification(activeGatt)
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val status = activeGatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            if (status != BluetoothGatt.GATT_SUCCESS) {
                listener.onError("BLE descriptor write rejected: $status")
                enableNextNotification(activeGatt)
            }
        } else {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION") if (!activeGatt.writeDescriptor(descriptor)) {
                listener.onError("BLE descriptor write rejected")
                enableNextNotification(activeGatt)
            }
        }
    }

    private fun handleNotification(value: ByteArray) {
        VoiceLink.decode(value)?.let(listener::onPacket) ?: listener.onError("Ignored malformed pendant packet")
    }

    private fun clearCharacteristics() {
        control = null
        microphone = null
        speaker = null
        telemetry = null
        notificationQueue.clear()
    }

    private fun scheduleReconnect() {
        if (!reconnectEnabled || preferences.getString(KEY_DEVICE_ADDRESS, null) == null) return
        reconnectAttempts = (reconnectAttempts + 1).coerceAtMost(5)
        val delayMs = (1_000L shl reconnectAttempts).coerceAtMost(16_000L)
        cancelReconnect()
        listener.onError("Pendant connection lost — retrying in ${delayMs / 1000}s")
        reconnectHandler.postDelayed(reconnectRunnable, delayMs)
    }

    private fun cancelReconnect() = reconnectHandler.removeCallbacks(reconnectRunnable)

    private fun hasScanPermission(): Boolean = Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
        context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED

    private fun hasConnectPermission(): Boolean = Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
        context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    private fun nowMs(): Long = android.os.SystemClock.elapsedRealtime()

    private var sequence = 0
    private fun nextSequence(): Int = (sequence++ and 0xffff)

    private companion object {
        const val PREFERENCES = "pendant_ble"
        const val KEY_DEVICE_ADDRESS = "device_address"
        val SERVICE_UUID: UUID = UUID.fromString("9d2d0001-596f-4b6d-a090-6f1d2d9a0000")
        val CONTROL_UUID: UUID = UUID.fromString("9d2d0001-596f-4b6d-a090-6f1d2d9a0001")
        val MICROPHONE_UUID: UUID = UUID.fromString("9d2d0001-596f-4b6d-a090-6f1d2d9a0002")
        val SPEAKER_UUID: UUID = UUID.fromString("9d2d0001-596f-4b6d-a090-6f1d2d9a0003")
        val TELEMETRY_UUID: UUID = UUID.fromString("9d2d0001-596f-4b6d-a090-6f1d2d9a0004")
        val CLIENT_CHARACTERISTIC_CONFIG_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
