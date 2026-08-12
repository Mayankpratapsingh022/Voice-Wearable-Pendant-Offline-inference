#include "app_state.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

static app_state_snapshot_t state;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;

void app_state_init(void) {
    portENTER_CRITICAL(&state_lock);
    memset(&state, 0, sizeof(state));
    // Waiting for a phone is still a normal idle screen.  The pairing radar is
    // used only for an explicit pairing transaction requested by the app.
    state.face = APP_FACE_DISCONNECTED;
    state.battery_percent = 100;
    strlcpy(state.status, "Phone not connected", sizeof(state.status));
    portEXIT_CRITICAL(&state_lock);
}

void app_state_set_face(app_face_state_t face, const char *status) {
    portENTER_CRITICAL(&state_lock);
    state.face = face;
    if (status != NULL) {
        strlcpy(state.status, status, sizeof(state.status));
    }
    portEXIT_CRITICAL(&state_lock);
}

void app_state_set_connection(bool connected) {
    portENTER_CRITICAL(&state_lock);
    state.connected = connected;
    if (!connected) {
        state.capturing = false;
        state.mouth_level = 0;
        state.face = APP_FACE_DISCONNECTED;
        strlcpy(state.status, "Phone not connected", sizeof(state.status));
    } else if (state.face == APP_FACE_PAIRING || state.face == APP_FACE_DISCONNECTED) {
        state.face = APP_FACE_IDLE;
        strlcpy(state.status, "Hold face to talk", sizeof(state.status));
    }
    portEXIT_CRITICAL(&state_lock);
}

void app_state_set_capture(bool capturing) {
    portENTER_CRITICAL(&state_lock);
    state.capturing = capturing;
    portEXIT_CRITICAL(&state_lock);
}

void app_state_set_mouth_level(uint8_t mouth_level) {
    portENTER_CRITICAL(&state_lock);
    state.mouth_level = mouth_level;
    portEXIT_CRITICAL(&state_lock);
}

void app_state_set_battery_percent(uint8_t battery_percent) {
    portENTER_CRITICAL(&state_lock);
    state.battery_percent = battery_percent > 100 ? 100 : battery_percent;
    portEXIT_CRITICAL(&state_lock);
}

app_state_snapshot_t app_state_snapshot(void) {
    app_state_snapshot_t copy;
    portENTER_CRITICAL(&state_lock);
    copy = state;
    portEXIT_CRITICAL(&state_lock);
    return copy;
}
