#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    AUDIO_FEEDBACK_BUTTON,
    AUDIO_FEEDBACK_ASK_ACCEPTED,
    AUDIO_FEEDBACK_CANCELLED,
    AUDIO_FEEDBACK_MEETING_PENDING,
    AUDIO_FEEDBACK_MEETING_STARTED,
    AUDIO_FEEDBACK_MEETING_STOPPED,
    AUDIO_FEEDBACK_UNAVAILABLE,
} audio_feedback_t;

esp_err_t audio_engine_init(void);
esp_err_t audio_engine_start_capture(void);
void audio_engine_stop_capture(void);
void audio_engine_stop_playback(void);
/** Queue a short local cue. Feedback is rendered by the audio task, never BLE. */
void audio_engine_play_feedback(audio_feedback_t feedback);
esp_err_t audio_engine_queue_speaker_opus(const uint8_t *data, size_t size, uint32_t timestamp_ms, uint8_t mouth_level);
