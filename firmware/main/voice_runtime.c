#include "voice_runtime.h"

#include <string.h>

#include "app_state.h"
#include "audio_engine.h"
#include "ble_transport.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "voice_runtime";
static uint16_t outbound_sequence;

static void send_control(voice_control_opcode_t opcode, const char *message) {
    uint8_t payload[81] = {opcode};
    size_t size = 1;
    if (message != NULL) {
        const size_t message_size = strnlen(message, sizeof(payload) - 1);
        memcpy(&payload[1], message, message_size);
        size += message_size;
    }
    const voice_link_packet_t packet = {
        .header = {
            .version = VOICE_LINK_VERSION,
            .kind = VOICE_LINK_CONTROL,
            .sequence = outbound_sequence++,
            .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
            .payload_size = (uint16_t)size,
            .mouth_level = 0,
            .flags = 0,
        },
        .payload = payload,
    };
    if (ble_transport_send(&packet) != ESP_OK) {
        ESP_LOGW(TAG, "unable to send control packet");
    }
}

esp_err_t voice_runtime_init(void) {
    ESP_RETURN_ON_ERROR(audio_engine_init(), TAG, "audio init failed");
    ESP_RETURN_ON_ERROR(ble_transport_init(), TAG, "BLE init failed");
    return ESP_OK;
}

void voice_runtime_request_capture(bool enabled) {
    if (!ble_transport_is_connected()) {
        app_state_set_face(APP_FACE_DISCONNECTED, "Phone not connected");
        return;
    }

    if (enabled) {
        audio_engine_stop_playback();
        if (audio_engine_start_capture() == ESP_OK) {
            app_state_set_capture(true);
            app_state_set_face(APP_FACE_LISTENING, "Listening… release to send");
            send_control(VOICE_CONTROL_CAPTURE_STARTED, NULL);
        } else {
            app_state_set_face(APP_FACE_ERROR, "Microphone unavailable");
        }
        return;
    }

    audio_engine_stop_capture();
    app_state_set_capture(false);
    app_state_set_face(APP_FACE_TRANSCRIBING, "Transcribing…");
    send_control(VOICE_CONTROL_CAPTURE_STOPPED, NULL);
}

void voice_runtime_on_ble_connection(bool connected) {
    app_state_set_connection(connected);
    if (!connected) {
        audio_engine_stop_capture();
        audio_engine_stop_playback();
    }
}

void voice_runtime_on_mic_opus(const uint8_t *data, size_t size, uint32_t timestamp_ms) {
    const voice_link_packet_t packet = {
        .header = {
            .version = VOICE_LINK_VERSION,
            .kind = VOICE_LINK_MIC_OPUS,
            .sequence = outbound_sequence++,
            .timestamp_ms = timestamp_ms,
            .payload_size = (uint16_t)size,
            .mouth_level = 0,
            .flags = 0,
        },
        .payload = data,
    };
    if (ble_transport_send(&packet) != ESP_OK) {
        ESP_LOGW(TAG, "dropped microphone packet");
    }
}

void voice_runtime_on_phone_packet(const voice_link_packet_t *packet) {
    if (packet == NULL) {
        return;
    }
    if (packet->header.kind == VOICE_LINK_SPEAKER_OPUS) {
        app_state_set_mouth_level(packet->header.mouth_level);
        app_state_set_face(APP_FACE_SPEAKING, "Speaking");
        audio_engine_queue_speaker_opus(packet->payload, packet->header.payload_size, packet->header.timestamp_ms,
                                        packet->header.mouth_level);
        return;
    }
    if (packet->header.kind != VOICE_LINK_CONTROL || packet->header.payload_size == 0) {
        return;
    }

    switch ((voice_control_opcode_t)packet->payload[0]) {
        case VOICE_CONTROL_PROCESSING:
            app_state_set_face(APP_FACE_THINKING, "Thinking…");
            break;
        case VOICE_CONTROL_SPEAKING:
            app_state_set_face(APP_FACE_SPEAKING, "Speaking");
            break;
        case VOICE_CONTROL_SAVED:
            app_state_set_face(APP_FACE_SAVED, "Note saved");
            break;
        case VOICE_CONTROL_IDLE:
            app_state_set_mouth_level(0);
            app_state_set_face(APP_FACE_IDLE, "Hold face to talk");
            break;
        case VOICE_CONTROL_ERROR:
            if (packet->header.payload_size > 1) {
                char error_message[48];
                const size_t message_length = packet->header.payload_size - 1;
                const size_t copy_length = message_length < sizeof(error_message) - 1 ? message_length : sizeof(error_message) - 1;
                memcpy(error_message, &packet->payload[1], copy_length);
                error_message[copy_length] = '\0';
                app_state_set_face(APP_FACE_ERROR, error_message);
            } else {
                app_state_set_face(APP_FACE_ERROR, "Phone error");
            }
            break;
        default:
            break;
    }
}
