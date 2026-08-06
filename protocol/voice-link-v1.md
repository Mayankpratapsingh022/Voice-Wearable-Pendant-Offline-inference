# Voice Link v1

Voice Link is the private BLE GATT protocol between one pendant and one Android phone. It is binary, versioned, and intentionally independent of Flutter or ESP-IDF types.

## Transport

The ESP32-S3 is a BLE peripheral/GATT server. Android is the central/client. Pair using LE Secure Connections and retain one bonded phone. All characteristics belong to the private `9d2d0001-596f-4b6d-a090-6f1d2d9a0000` service.

| Characteristic suffix | Direction | Property | Purpose |
| --- | --- | --- | --- |
| `...0001` | both | write + notify | control and lifecycle events |
| `...0002` | pendant → phone | notify | 16 kHz microphone Opus frames |
| `...0003` | phone → pendant | write without response | 24 kHz TTS Opus frames |
| `...0004` | pendant → phone | notify | battery and diagnostics |

The concrete characteristic UUIDs use the same base UUID with the final byte replaced by the suffix. The Android client requests the largest supported MTU, 2M PHY where available, then subscribes to control, microphone, and telemetry notifications.

## Packet frame

Every control and audio characteristic payload starts with this 12-byte little-endian header.

| Byte | Field | Type | Meaning |
| ---: | --- | --- | --- |
| 0 | version | `u8` | Always `1` for this protocol |
| 1 | kind | `u8` | Packet kind below |
| 2–3 | sequence | `u16` | Per-direction sequence, wraps naturally |
| 4–7 | timestamp_ms | `u32` | Source/playback timeline in milliseconds |
| 8–9 | payload_size | `u16` | Bytes after the header |
| 10 | mouth_level | `u8` | `0..255` mouth openness; zero for microphone frames |
| 11 | flags | `u8` | Reserved; must be zero in v1 |

Reject frames when the version is unsupported, the length is not exactly `12 + payload_size`, or the payload exceeds 192 bytes. A 20 ms speech Opus frame at v1 bitrates fits comfortably in that limit.

## Packet kinds

| Value | Name | Characteristic | Payload |
| ---: | --- | --- | --- |
| `0x01` | `CONTROL` | control | one control opcode plus optional binary fields |
| `0x02` | `MIC_OPUS` | microphone | 16 kHz, mono, 20 ms Opus speech packet |
| `0x03` | `SPEAKER_OPUS` | speaker | 24 kHz, mono, 20 ms Opus speech packet |
| `0x04` | `TELEMETRY` | telemetry | v1 telemetry payload |

Control opcodes are one byte. `0x01` capture started, `0x02` capture stopped, `0x03` phone processing, `0x04` phone speaking, `0x05` note saved, `0x06` idle, and `0x07` error. `0x07` is followed by a UTF-8 error message of no more than 80 bytes.

## Timing rules

- The pendant sends a capture-start control frame before its first microphone frame and a capture-stop control frame after its last frame.
- The phone timestamps speaker frames against a single session playback timeline. The pendant starts playback only after it has at least five frames buffered.
- `mouth_level` is measured from the uncompressed TTS PCM that produced the frame. The pendant applies it when the timestamp reaches its playback clock, not when a BLE notification arrives.
- Sequence gaps are allowed; the receiver counts them as packet loss and continues. Never replay an audio frame.

## Compatibility

Add fields only by defining a new protocol version. The control opcodes and packet layout above are the contract used by the firmware and Kotlin service tests.
