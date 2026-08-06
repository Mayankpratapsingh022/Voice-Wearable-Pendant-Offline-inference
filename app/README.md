# Pendant Android companion

Flutter renders the companion interface. Kotlin owns the work that must survive
when the UI is closed: BLE, the connected-device foreground service, encrypted
note storage, and the local-model boundary.

## Run checks

```sh
flutter pub get
flutter analyze
flutter test
cd android && ./gradlew :app:compileDebugKotlin
```

Android 12+ requires the Nearby Devices prompt before pairing. The service starts
only from the app and shows a persistent notification while it owns the pendant
connection.

## Model runtime boundary

`LocalVoicePipeline` is the one deliberately incomplete production boundary. It
defines 16 kHz input Opus, text/note output, and 24 kHz output Opus so the rest
of the system is real and testable without model files. `NativeRuntimePlaceholder`
refuses capture processing rather than quietly sending voice data to a cloud.

Replace it with a native Android binding that performs, in order: sherpa-onnx
Opus decode + Moonshine STT, Qwen3 GGUF inference through llama.cpp, Kokoro ONNX
TTS, PCM envelope extraction, then 24 kHz Opus encoding. The required on-device
model locations are intentionally versioned in `ModelPack`; import code must
verify a signed SHA-256 manifest before copying a pack there.
