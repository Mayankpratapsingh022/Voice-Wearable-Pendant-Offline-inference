# ESP32-S3 Voice AI Wearable Inference Pendant

An offline Voice AI wearable pendant built from a Waveshare ESP32-S3 Touch AMOLED 1.75C and an Android companion app.

The ESP32 owns the touch-first animated face, microphone capture, speaker playback, and BLE transport. The Android phone owns local speech recognition, note storage/search, local LLM inference, and speech synthesis. After model packs are downloaded, no speech or note content leaves the phone.

## Layout

- `firmware/` — ESP-IDF application for the pendant.
- `app/` — Flutter Android companion; Kotlin owns the persistent Bluetooth and AI service.
- `protocol/` — versioned BLE packet contract shared by both implementations.

## Quick start

```sh
# Android companion
cd app
flutter pub get
flutter run
```

```sh
# ESP32-S3 firmware (ESP-IDF 5.5+)
cd firmware
idf.py set-target esp32s3
idf.py build flash monitor
```

The model pack is intentionally excluded from Git. Local model assets and their
signed SHA-256 manifest must be installed on the phone before inference is enabled.

## Implemented foundation

- Procedural LVGL eyes, mouth, glow ring, status text, and touch-and-hold PTT.
- ESP-IDF audio task: 16 kHz microphone Opus uplink and 24 kHz speaker Opus downlink.
- Versioned private BLE GATT protocol, pairing configuration, MTU 247, packet validation, and state cues.
- Flutter black/cyan companion UI and a Kotlin foreground connected-device service.
- Encrypted SQLCipher transcript-only note store with an Android Keystore-wrapped key.
- A strict offline model-pack and inference interface. It currently fails closed until
  the native sherpa-onnx, llama.cpp, and Kokoro bindings are packaged.

See [protocol/voice-link-v1.md](protocol/voice-link-v1.md),
[firmware/README.md](firmware/README.md), and [app/README.md](app/README.md)
before changing either endpoint.
