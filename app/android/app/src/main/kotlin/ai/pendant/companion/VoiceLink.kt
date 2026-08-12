package ai.pendant.companion

import java.nio.ByteBuffer
import java.nio.ByteOrder

object VoiceLink {
    const val VERSION: Byte = 1
    const val HEADER_SIZE = 12
    const val MAX_PAYLOAD = 192

    const val CONTROL: Byte = 0x01
    const val MIC_OPUS: Byte = 0x02
    const val SPEAKER_OPUS: Byte = 0x03
    const val TELEMETRY: Byte = 0x04

    const val CAPTURE_STARTED: Byte = 0x01
    const val CAPTURE_STOPPED: Byte = 0x02
    const val PROCESSING: Byte = 0x03
    const val SPEAKING: Byte = 0x04
    const val SAVED: Byte = 0x05
    const val IDLE: Byte = 0x06
    const val ERROR: Byte = 0x07
    const val ASSISTANT_REQUEST: Byte = 0x08
    const val ASSISTANT_CANCEL: Byte = 0x09
    const val MEETING_TOGGLE: Byte = 0x0a
    const val ASSISTANT_ACCEPTED: Byte = 0x0b
    const val MEETING_STARTED: Byte = 0x0c
    const val MEETING_STOPPED: Byte = 0x0d
    const val UNAVAILABLE: Byte = 0x0e
    const val WIFI_PROVISION: Byte = 0x0f
    const val WIFI_READY: Byte = 0x10
    const val WIFI_FAILED: Byte = 0x11

    data class Packet(
        val kind: Byte,
        val sequence: Int,
        val timestampMs: Long,
        val mouthLevel: Int,
        val payload: ByteArray,
    )

    fun decode(data: ByteArray): Packet? {
        if (data.size < HEADER_SIZE) return null
        val buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        if (buffer.get() != VERSION) return null
        val kind = buffer.get()
        val sequence = buffer.short.toInt() and 0xffff
        val timestamp = buffer.int.toLong() and 0xffffffffL
        val payloadSize = buffer.short.toInt() and 0xffff
        val mouth = buffer.get().toInt() and 0xff
        val flags = buffer.get()
        if (!isKnownKind(kind) || flags.toInt() != 0 || payloadSize > MAX_PAYLOAD || data.size != HEADER_SIZE + payloadSize) return null
        return Packet(kind, sequence, timestamp, mouth, ByteArray(payloadSize).also(buffer::get))
    }

    fun encode(packet: Packet): ByteArray {
        require(packet.payload.size <= MAX_PAYLOAD)
        return ByteBuffer.allocate(HEADER_SIZE + packet.payload.size)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(VERSION)
            .put(packet.kind)
            .putShort(packet.sequence.toShort())
            .putInt(packet.timestampMs.toInt())
            .putShort(packet.payload.size.toShort())
            .put(packet.mouthLevel.toByte())
            .put(0)
            .put(packet.payload)
            .array()
    }

    private fun isKnownKind(kind: Byte): Boolean = kind == CONTROL || kind == MIC_OPUS ||
        kind == SPEAKER_OPUS || kind == TELEMETRY
}
