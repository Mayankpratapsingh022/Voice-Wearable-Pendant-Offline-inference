#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_FACE_PAIRING,
    APP_FACE_DISCONNECTED,
    APP_FACE_IDLE,
    APP_FACE_LISTENING,
    APP_FACE_TRANSCRIBING,
    APP_FACE_THINKING,
    APP_FACE_SPEAKING,
    APP_FACE_SAVED,
    APP_FACE_ERROR,
} app_face_state_t;

typedef struct {
    app_face_state_t face;
    bool connected;
    bool capturing;
    uint8_t mouth_level;
    uint8_t battery_percent;
    char status[48];
} app_state_snapshot_t;

void app_state_init(void);
void app_state_set_face(app_face_state_t face, const char *status);
void app_state_set_connection(bool connected);
void app_state_set_capture(bool capturing);
void app_state_set_mouth_level(uint8_t mouth_level);
void app_state_set_battery_percent(uint8_t battery_percent);
app_state_snapshot_t app_state_snapshot(void);
