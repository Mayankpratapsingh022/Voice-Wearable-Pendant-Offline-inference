#include "audio_engine.h"

#include <stdbool.h>
#include <math.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "opus.h"
#include "voice_runtime.h"

#define MIC_SAMPLE_RATE 16000
#define MIC_FRAME_SAMPLES 320
#define SPEAKER_SAMPLE_RATE 24000
#define SPEAKER_FRAME_SAMPLES 480
#define PLAYBACK_QUEUE_DEPTH 12
#define FEEDBACK_QUEUE_DEPTH 1
#define AUDIO_TASK_STACK_SIZE (16 * 1024)
#define FEEDBACK_BUFFER_SAMPLES 3600
#define TWO_PI_F 6.28318530718f
#define PI_F 3.14159265359f

typedef struct {
    uint16_t size;
    uint32_t timestamp_ms;
    uint8_t mouth_level;
    uint8_t data[VOICE_LINK_MAX_PAYLOAD];
} speaker_packet_t;

static const char *TAG = "audio_engine";
static esp_codec_dev_handle_t microphone;
static esp_codec_dev_handle_t speaker;
static OpusEncoder *mic_encoder;
static OpusDecoder *speaker_decoder;
static QueueHandle_t playback_queue;
static QueueHandle_t feedback_queue;
static volatile bool capture_active;
static bool microphone_open;
static bool speaker_open;
// The audio task is the sole owner of these buffers.  Keeping them static
// avoids spending its FreeRTOS stack on PCM and packet workspaces.
static int16_t microphone_pcm[MIC_FRAME_SAMPLES];
static int16_t speaker_pcm[SPEAKER_FRAME_SAMPLES];
static int16_t feedback_pcm[FEEDBACK_BUFFER_SAMPLES];
static uint8_t opus_buffer[VOICE_LINK_MAX_PAYLOAD];

static esp_codec_dev_sample_info_t microphone_format = {
    .sample_rate = MIC_SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static esp_codec_dev_sample_info_t speaker_format = {
    .sample_rate = SPEAKER_SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static size_t ms_to_samples(uint16_t milliseconds) {
    return ((size_t)SPEAKER_SAMPLE_RATE * milliseconds) / 1000U;
}

static void mix_feedback_note(size_t start, size_t length, float start_hz, float end_hz, int16_t amplitude) {
    const size_t end = start + length > FEEDBACK_BUFFER_SAMPLES ? FEEDBACK_BUFFER_SAMPLES : start + length;
    for (size_t index = start; index < end; ++index) {
        const float progress = (float)(index - start) / (float)(length > 1 ? length - 1 : 1);
        const float envelope = sinf(PI_F * progress);
        const float frequency = start_hz + (end_hz - start_hz) * progress;
        const float sample = sinf(TWO_PI_F * frequency * (float)(index - start) / SPEAKER_SAMPLE_RATE);
        const int32_t mixed = (int32_t)feedback_pcm[index] + (int32_t)(sample * envelope * amplitude);
        feedback_pcm[index] = mixed > INT16_MAX ? INT16_MAX : (mixed < INT16_MIN ? INT16_MIN : (int16_t)mixed);
    }
}

static void render_feedback(audio_feedback_t feedback) {
    memset(feedback_pcm, 0, sizeof(feedback_pcm));
    size_t samples = 0;
    switch (feedback) {
        case AUDIO_FEEDBACK_BUTTON:
            samples = ms_to_samples(44);
            mix_feedback_note(0, samples, 205.0f, 150.0f, 1700);
            break;
        case AUDIO_FEEDBACK_ASK_ACCEPTED:
            samples = ms_to_samples(68);
            mix_feedback_note(0, samples, 390.0f, 700.0f, 2300);
            break;
        case AUDIO_FEEDBACK_CANCELLED:
            samples = ms_to_samples(82);
            mix_feedback_note(0, samples, 670.0f, 250.0f, 2100);
            break;
        case AUDIO_FEEDBACK_MEETING_PENDING:
            samples = ms_to_samples(38);
            mix_feedback_note(0, samples, 300.0f, 275.0f, 1500);
            break;
        case AUDIO_FEEDBACK_MEETING_STARTED:
            samples = ms_to_samples(148);
            mix_feedback_note(0, ms_to_samples(48), 410.0f, 520.0f, 1900);
            mix_feedback_note(ms_to_samples(76), ms_to_samples(48), 610.0f, 740.0f, 2100);
            break;
        case AUDIO_FEEDBACK_MEETING_STOPPED:
            samples = ms_to_samples(148);
            mix_feedback_note(0, ms_to_samples(48), 630.0f, 520.0f, 1900);
            mix_feedback_note(ms_to_samples(76), ms_to_samples(48), 420.0f, 300.0f, 2000);
            break;
        case AUDIO_FEEDBACK_UNAVAILABLE:
            samples = ms_to_samples(170);
            mix_feedback_note(0, ms_to_samples(54), 280.0f, 230.0f, 1700);
            mix_feedback_note(ms_to_samples(92), ms_to_samples(54), 250.0f, 190.0f, 1500);
            break;
        default:
            return;
    }
    if (samples > 0 && speaker_open) {
        esp_codec_dev_write(speaker, feedback_pcm, samples * sizeof(int16_t));
    }
}

static void audio_task(void *arg) {
    (void)arg;
    speaker_packet_t speaker_packet;
    audio_feedback_t feedback;

    for (;;) {
        // A cue takes precedence over both the Opus queue and microphone
        // frames. The runtime opens the microphone only after the cue has
        // finished, so it cannot transcribe its own confirmation sound.
        if (xQueueReceive(feedback_queue, &feedback, 0) == pdTRUE) {
            render_feedback(feedback);
            continue;
        }
        if (capture_active) {
            // esp_codec_dev_read returns an ESP_CODEC_DEV_* status code, not
            // the number of bytes read.  Treating it as a byte count dropped
            // every microphone frame before it reached the BLE transport.
            const int read_result = esp_codec_dev_read(microphone, microphone_pcm, sizeof(microphone_pcm));
            if (read_result == ESP_CODEC_DEV_OK) {
                const int encoded = opus_encode(mic_encoder, microphone_pcm, MIC_FRAME_SAMPLES, opus_buffer, sizeof(opus_buffer));
                if (encoded > 0) {
                    voice_runtime_on_mic_opus(opus_buffer, (size_t)encoded, (uint32_t)(esp_timer_get_time() / 1000));
                }
            }
            continue;
        }

        if (xQueueReceive(playback_queue, &speaker_packet, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }
        const int decoded = opus_decode(speaker_decoder, speaker_packet.data, speaker_packet.size, speaker_pcm,
                                        SPEAKER_FRAME_SAMPLES, 0);
        if (decoded > 0) {
            esp_codec_dev_write(speaker, speaker_pcm, decoded * sizeof(int16_t));
        }
    }
}

esp_err_t audio_engine_init(void) {
    ESP_RETURN_ON_ERROR(bsp_audio_init(NULL), TAG, "BSP audio init failed");
    microphone = bsp_audio_codec_microphone_init();
    speaker = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(microphone != NULL && speaker != NULL, ESP_ERR_NOT_FOUND, TAG, "audio codec unavailable");

    int opus_error = OPUS_OK;
    mic_encoder = opus_encoder_create(MIC_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &opus_error);
    ESP_RETURN_ON_FALSE(mic_encoder != NULL && opus_error == OPUS_OK, ESP_FAIL, TAG, "Opus encoder init failed");
    speaker_decoder = opus_decoder_create(SPEAKER_SAMPLE_RATE, 1, &opus_error);
    ESP_RETURN_ON_FALSE(speaker_decoder != NULL && opus_error == OPUS_OK, ESP_FAIL, TAG, "Opus decoder init failed");
    opus_encoder_ctl(mic_encoder, OPUS_SET_BITRATE(16000));
    opus_encoder_ctl(mic_encoder, OPUS_SET_COMPLEXITY(3));
    opus_encoder_ctl(mic_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    playback_queue = xQueueCreate(PLAYBACK_QUEUE_DEPTH, sizeof(speaker_packet_t));
    ESP_RETURN_ON_FALSE(playback_queue != NULL, ESP_ERR_NO_MEM, TAG, "playback queue allocation failed");
    feedback_queue = xQueueCreate(FEEDBACK_QUEUE_DEPTH, sizeof(audio_feedback_t));
    ESP_RETURN_ON_FALSE(feedback_queue != NULL, ESP_ERR_NO_MEM, TAG, "feedback queue allocation failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(speaker, &speaker_format), TAG, "speaker open failed");
    speaker_open = true;
    esp_codec_dev_set_out_vol(speaker, 65);
    xTaskCreatePinnedToCore(audio_task, "audio_engine", AUDIO_TASK_STACK_SIZE, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "audio engine ready");
    return ESP_OK;
}

esp_err_t audio_engine_start_capture(void) {
    if (capture_active) {
        return ESP_OK;
    }
    if (!microphone_open) {
        ESP_RETURN_ON_ERROR(esp_codec_dev_open(microphone, &microphone_format), TAG, "microphone open failed");
        microphone_open = true;
    }
    capture_active = true;
    return ESP_OK;
}

void audio_engine_stop_capture(void) {
    capture_active = false;
    if (microphone_open) {
        esp_codec_dev_close(microphone);
        microphone_open = false;
    }
}

void audio_engine_stop_playback(void) {
    xQueueReset(playback_queue);
}

void audio_engine_play_feedback(audio_feedback_t feedback) {
    if (feedback_queue == NULL || !speaker_open) {
        return;
    }
    // Feedback is an acknowledgement, not a sequence. A newer user action
    // deliberately replaces a stale cue rather than sounding late.
    xQueueOverwrite(feedback_queue, &feedback);
}

esp_err_t audio_engine_queue_speaker_opus(const uint8_t *data, size_t size, uint32_t timestamp_ms, uint8_t mouth_level) {
    if (data == NULL || size == 0 || size > VOICE_LINK_MAX_PAYLOAD || !speaker_open) {
        return ESP_ERR_INVALID_ARG;
    }
    speaker_packet_t packet = {
        .size = (uint16_t)size,
        .timestamp_ms = timestamp_ms,
        .mouth_level = mouth_level,
    };
    memcpy(packet.data, data, size);
    return xQueueSend(playback_queue, &packet, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
