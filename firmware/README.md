# Pendant ESP32-S3 firmware

This is an ESP-IDF 5.5+ project for the Waveshare ESP32-S3 Touch AMOLED 1.75C.
It uses the Waveshare BSP for the CO5300 display, touch, ES7210/ES8311 audio
path, and board power setup; it does not hard-code GPIO numbers.

## Build and flash

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbserial-XXXX flash monitor
```

The first build downloads the two dependencies in `main/idf_component.yml`:
the Waveshare BSP and micro-opus. Confirm the actual flash size reported by
`idf.py flash-id` before shipping: the defaults assume the 32 MB board revision.

## What runs on the pendant

- `face_ui.c` owns the 466×466 black/cyan procedural face and touch-and-hold PTT.
- `audio_engine.c` captures 20 ms, 16 kHz mono microphone frames and plays 24 kHz
  speaker frames through Opus.
- `ble_transport.c` exposes the encrypted Voice Link service via NimBLE.
- `voice_runtime.c` synchronises display states with the audio/BLE lifecycle.

The full packet contract is [../protocol/voice-link-v1.md](../protocol/voice-link-v1.md).
Use the desktop LVGL simulator before tuning geometry or expressions; actual
frame-rate, touch, mic directionality, and speaker level still require hardware
bring-up on the exact board revision.
