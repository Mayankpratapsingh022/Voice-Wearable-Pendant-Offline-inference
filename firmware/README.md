# Pendant ESP32-S3 firmware

This is an ESP-IDF 5.5+ project for the ESP32-S3 wearable. It uses a board
support package for display, touch, audio, and power setup, so application code
does not hard-code GPIO numbers.

## Build and flash

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbserial-XXXX flash monitor
```

The first build downloads the dependencies in `main/idf_component.yml`. Confirm
the actual flash size with `esptool.py --port PORT flash_id` before shipping;
the defaults target 32 MB flash.

`micro-opus` 0.4.1 has a path-handling defect on macOS and Linux when the full
firmware directory path contains spaces. Build this project from a path without
spaces until that dependency is updated.

## Rive-derived EAF expression asset

The ESP32 does not execute `.riv` files directly. The supplied Rive source is
rendered with Rive Canvas on the development machine as individual `Face`
expressions, converted to EAF, and embedded one expression at a time. Generate
the `Hello` expression used by the firmware before building:

```sh
cd tools/rive-eaf-export
npm install
npx playwright install chromium
npm run render -- ../../24941-46557-expression-grid.riv \
  --artboard Face --animation "Hello" --fps 15 --output hello
npm run convert -- out/24941-46557-expression-grid/hello/preview.gif
```

On boot, the pendant plays one exact 466 × 466 Rive-rendered `Hello` expression
full-screen. It never renders the 3 × 3 source grid. This visual-test build
keeps the expression visible until reset, avoiding a boot-time false touch from
the touch controller. Generated GIF, PNG, and EAF files are intentionally local
build outputs; the `.riv` source and conversion scripts remain the design source
of truth.

## What runs on the pendant

- `face_ui.c` owns the EAF expression playback, procedural fallback face, expression
  test controls, and touch-and-hold PTT. It uses real audio amplitude for the
  talking mouth, so speech interrupts any idle expression immediately.
- `audio_engine.c` captures microphone frames and plays speaker frames through
  Opus.
- `ble_transport.c` exposes the encrypted Voice Link service via NimBLE.
- `voice_runtime.c` synchronises display states with the audio/BLE lifecycle.

The full packet contract is [../protocol/voice-link-v1.md](../protocol/voice-link-v1.md).
Use the desktop LVGL simulator before tuning geometry or expressions; actual
frame-rate, touch, mic directionality, and speaker level still require hardware
bring-up on the exact board revision.

## Expression test controls

The bottom `FACE` slider switches the on-device expression clips: Neutral,
Curious, Happy, Surprised, Skeptical, and Sleepy. The `<` and `>` buttons step
through the same list. These controls sit above the push-to-talk area, so they
will not start a recording. The small `x` in the upper-left turns the AMOLED
off; tap the face once to wake it, then press and hold the face to talk.
