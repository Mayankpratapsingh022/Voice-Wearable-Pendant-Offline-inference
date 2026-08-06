#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t audio_engine_init(void);
esp_err_t audio_engine_start_capture(void);
void audio_engine_stop_capture(void);
void audio_engine_stop_playback(void);
esp_err_t audio_engine_queue_speaker_opus(const uint8_t *data, size_t size, uint32_t timestamp_ms, uint8_t mouth_level);
