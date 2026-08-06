#include "audio_engine.h"

#include <stdbool.h>
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
static volatile bool capture_active;
static bool microphone_open;
static bool speaker_open;

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

static void audio_task(void *arg) {
    (void)arg;
    int16_t microphone_pcm[MIC_FRAME_SAMPLES];
    int16_t speaker_pcm[SPEAKER_FRAME_SAMPLES];
    uint8_t opus_buffer[VOICE_LINK_MAX_PAYLOAD];
    speaker_packet_t speaker_packet;

    for (;;) {
        if (capture_active) {
            const int read = esp_codec_dev_read(microphone, microphone_pcm, sizeof(microphone_pcm));
            if (read == sizeof(microphone_pcm)) {
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
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(speaker, &speaker_format), TAG, "speaker open failed");
    speaker_open = true;
    esp_codec_dev_set_out_vol(speaker, 65);
    xTaskCreatePinnedToCore(audio_task, "audio_engine", 8192, NULL, 6, NULL, 0);
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
