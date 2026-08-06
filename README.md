# ESP32 Voice AI Wearable Inference Pendant

An open-source, touch-to-talk Voice AI wearable built around an ESP32 pendant and an Android companion app. It captures a spoken thought, shows a responsive animated face, and turns that thought into a private note or spoken answer.

The pendant handles interaction, audio, animation, and Bluetooth Low Energy. The Android app handles on-device speech recognition, local LLM inference, speech synthesis, encrypted notes, and memory retrieval.

## How it works

1. Hold the pendant face to speak; release it when finished.
2. The ESP32 streams the captured audio to the paired Android phone over Bluetooth Low Energy.
3. The phone processes the request locally and returns status, speech, and mouth-animation cues.
4. Say **remember**, **note**, or **save** to explicitly store the resulting transcript as an encrypted note.

## Features

- Touch-to-talk voice interaction with a reactive animated face.
- Private Bluetooth Low Energy audio and control link between pendant and phone.
- Flutter Android companion with a persistent connected-device service.
- Local-first note search and encrypted transcript storage.
- Designed for fully on-device STT, LLM inference, TTS, and note retrieval.
- No automatic recording and no audio archive: capture happens only during an intentional hold.

## Privacy model

Voice and notes are intended to remain on the paired phone after the model pack is installed. The project does not use cloud inference, analytics, or account-based syncing. Notes are stored as transcripts only and encrypted at rest with a key protected by Android Keystore.

## Project structure

| Directory | Purpose |
| --- | --- |
| `firmware/` | ESP-IDF firmware for interaction, audio, display UI, and BLE. |
| `app/` | Flutter UI plus Kotlin Android service, Bluetooth client, and encrypted notes. |
| `protocol/` | Shared versioned BLE packet contract. |

## Quick start

```sh
# Android companion
cd app
flutter pub get
flutter run
```

```sh
# ESP32 firmware
cd firmware
idf.py set-target esp32s3
idf.py build flash monitor
```

## Current status

The pendant UI, touch-to-talk state flow, BLE protocol, Android foreground service, and encrypted note store are implemented. The native adapters for local STT, LLM inference, and TTS are the next integration step; until they are bundled, the app fails closed rather than sending voice data to a cloud service.

Model files are deliberately excluded from Git. Installers must verify a signed SHA-256 manifest before placing a local model pack on the phone.

## Documentation

- [Voice Link protocol](protocol/voice-link-v1.md)
- [Firmware guide](firmware/README.md)
- [Android companion guide](app/README.md)
