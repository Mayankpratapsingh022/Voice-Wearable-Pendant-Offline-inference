#include "voice_runtime.h"

#include <string.h>

#include "app_state.h"
#include "audio_engine.h"
#include "ble_transport.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wifi_transport.h"

static const char *TAG = "voice_runtime";
static uint16_t outbound_sequence;
static esp_timer_handle_t capture_start_timer;
static volatile bool assistant_pending;
static volatile bool meeting_active;

#define CAPTURE_SOUND_GATE_US (260 * 1000)

static bool phone_is_connected(void) {
    return wifi_transport_is_connected() || ble_transport_is_connected();
}

static esp_err_t send_phone_packet(const voice_link_packet_t *packet) {
    return wifi_transport_is_connected() ? wifi_transport_send(packet) : ble_transport_send(packet);
}

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
    if (send_phone_packet(&packet) != ESP_OK) {
        ESP_LOGW(TAG, "unable to send control packet");
    }
}

static void start_capture_now(void) {
    if (!phone_is_connected()) {
        app_state_set_face(APP_FACE_DISCONNECTED, "Phone not connected");
        return;
    }
    if (audio_engine_start_capture() != ESP_OK) {
        app_state_set_face(APP_FACE_ERROR, "Microphone unavailable");
        return;
    }
    app_state_set_capture(true);
    app_state_set_face(APP_FACE_LISTENING, meeting_active ? "Meeting capture" : "Listening");
    send_control(VOICE_CONTROL_CAPTURE_STARTED, NULL);
}

static void on_capture_start_timer(void *arg) {
    (void)arg;
    if (assistant_pending || meeting_active) {
        assistant_pending = false;
        start_capture_now();
    }
}

static void schedule_capture_after_feedback(void) {
    if (capture_start_timer == NULL) {
        return;
    }
    esp_timer_stop(capture_start_timer);
    esp_timer_start_once(capture_start_timer, CAPTURE_SOUND_GATE_US);
}

static void stop_capture_and_notify(void) {
    const app_state_snapshot_t state = app_state_snapshot();
    audio_engine_stop_capture();
    app_state_set_capture(false);
    if (state.capturing) {
        send_control(VOICE_CONTROL_CAPTURE_STOPPED, NULL);
    }
}

esp_err_t voice_runtime_init(void) {
    ESP_RETURN_ON_ERROR(audio_engine_init(), TAG, "audio init failed");
    const esp_timer_create_args_t timer_args = {
        .callback = on_capture_start_timer,
        .name = "capture_gate",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &capture_start_timer), TAG, "capture timer init failed");
    ESP_RETURN_ON_ERROR(pendant_ble_init(), TAG, "BLE init failed");
    ESP_RETURN_ON_ERROR(wifi_transport_init(), TAG, "Wi-Fi init failed");
    return ESP_OK;
}

void voice_runtime_request_capture(bool enabled) {
    if (!phone_is_connected()) {
        app_state_set_face(APP_FACE_DISCONNECTED, "Phone not connected");
        return;
    }

    if (enabled) {
        audio_engine_stop_playback();
        start_capture_now();
        return;
    }

    stop_capture_and_notify();
    app_state_set_face(APP_FACE_TRANSCRIBING, "Transcribing…");
}

void voice_runtime_request_assistant(void) {
    const app_state_snapshot_t state = app_state_snapshot();
    if (!phone_is_connected()) {
        audio_engine_play_feedback(AUDIO_FEEDBACK_UNAVAILABLE);
        app_state_set_face(APP_FACE_DISCONNECTED, "Phone not connected");
        return;
    }
    if (meeting_active) {
        audio_engine_play_feedback(AUDIO_FEEDBACK_UNAVAILABLE);
        app_state_set_face(APP_FACE_LISTENING, "Meeting capture active");
        return;
    }
    if (state.capturing || assistant_pending || state.face == APP_FACE_TRANSCRIBING || state.face == APP_FACE_THINKING ||
        state.face == APP_FACE_SPEAKING) {
        voice_runtime_cancel_assistant();
        return;
    }
    meeting_active = false;
    assistant_pending = true;
    audio_engine_stop_playback();
    audio_engine_play_feedback(AUDIO_FEEDBACK_BUTTON);
    app_state_set_face(APP_FACE_THINKING, "Starting assistant");
    send_control(VOICE_CONTROL_ASSISTANT_REQUEST, NULL);
}

void voice_runtime_cancel_assistant(void) {
    assistant_pending = false;
    if (capture_start_timer != NULL) {
        esp_timer_stop(capture_start_timer);
    }
    audio_engine_stop_playback();
    // Tell Android to discard the ephemeral frames before CAPTURE_STOPPED.
    // Notifications preserve this ordering on the control characteristic.
    if (phone_is_connected()) {
        send_control(VOICE_CONTROL_ASSISTANT_CANCEL, NULL);
    }
    stop_capture_and_notify();
    audio_engine_play_feedback(AUDIO_FEEDBACK_CANCELLED);
    if (phone_is_connected()) {
        app_state_set_face(APP_FACE_IDLE, "Ready");
    } else {
        app_state_set_face(APP_FACE_DISCONNECTED, "Phone not connected");
    }
}

void voice_runtime_request_meeting_toggle(void) {
    if (!phone_is_connected()) {
        audio_engine_play_feedback(AUDIO_FEEDBACK_UNAVAILABLE);
        app_state_set_face(APP_FACE_DISCONNECTED, "Phone not connected");
        return;
    }
    assistant_pending = false;
    if (capture_start_timer != NULL) {
        esp_timer_stop(capture_start_timer);
    }
    audio_engine_stop_playback();
    audio_engine_play_feedback(AUDIO_FEEDBACK_MEETING_PENDING);
    app_state_set_face(APP_FACE_THINKING, meeting_active ? "Stopping meeting" : "Starting meeting");
    send_control(VOICE_CONTROL_MEETING_TOGGLE, NULL);
}

void voice_runtime_on_ble_connection(bool connected) {
    const bool any_connected = connected || wifi_transport_is_connected();
    app_state_set_connection(any_connected);
    if (!any_connected) {
        assistant_pending = false;
        meeting_active = false;
        if (capture_start_timer != NULL) {
            esp_timer_stop(capture_start_timer);
        }
        audio_engine_stop_capture();
        audio_engine_stop_playback();
    }
}

void voice_runtime_on_wifi_connection(bool connected) {
    const bool any_connected = connected || ble_transport_is_connected();
    app_state_set_connection(any_connected);
    if (!any_connected) {
        assistant_pending = false;
        meeting_active = false;
        if (capture_start_timer != NULL) esp_timer_stop(capture_start_timer);
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
    if (send_phone_packet(&packet) != ESP_OK) {
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
        case VOICE_CONTROL_WIFI_PROVISION: {
            if (packet->header.payload_size < 3) break;
            const uint8_t ssid_size = packet->payload[1];
            const uint8_t password_size = packet->payload[2];
            if ((size_t)ssid_size + password_size + 3U != packet->header.payload_size) break;
            const esp_err_t result = wifi_transport_provision(&packet->payload[3], ssid_size,
                                                               &packet->payload[3 + ssid_size], password_size);
            app_state_set_face(result == ESP_OK ? APP_FACE_THINKING : APP_FACE_ERROR,
                               result == ESP_OK ? "Joining phone hotspot" : "Wi-Fi setup failed");
            break;
        }
        case VOICE_CONTROL_ASSISTANT_ACCEPTED:
            if (assistant_pending) {
                audio_engine_play_feedback(AUDIO_FEEDBACK_ASK_ACCEPTED);
                schedule_capture_after_feedback();
            }
            break;
        case VOICE_CONTROL_MEETING_STARTED:
            meeting_active = true;
            assistant_pending = false;
            audio_engine_play_feedback(AUDIO_FEEDBACK_MEETING_STARTED);
            schedule_capture_after_feedback();
            break;
        case VOICE_CONTROL_MEETING_STOPPED:
            meeting_active = false;
            assistant_pending = false;
            if (capture_start_timer != NULL) {
                esp_timer_stop(capture_start_timer);
            }
            stop_capture_and_notify();
            audio_engine_play_feedback(AUDIO_FEEDBACK_MEETING_STOPPED);
            app_state_set_face(APP_FACE_THINKING, "Saving meeting");
            break;
        case VOICE_CONTROL_UNAVAILABLE:
            assistant_pending = false;
            if (capture_start_timer != NULL) {
                esp_timer_stop(capture_start_timer);
            }
            audio_engine_play_feedback(AUDIO_FEEDBACK_UNAVAILABLE);
            app_state_set_face(APP_FACE_IDLE, "Phone unavailable");
            break;
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
