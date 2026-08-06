#include "face_ui.h"

#include <stdio.h>

#include "app_state.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "lvgl.h"
#include "voice_runtime.h"

static const char *TAG = "face_ui";
static lv_obj_t *left_eye;
static lv_obj_t *right_eye;
static lv_obj_t *mouth;
static lv_obj_t *ring;
static lv_obj_t *status_label;
static lv_obj_t *battery_label;
static lv_obj_t *touch_target;
static uint32_t tick;
static bool capture_requested;

static lv_color_t eye_color_for(const app_state_snapshot_t *state) {
    if (state->face == APP_FACE_ERROR) {
        return lv_color_hex(0xff5c5c);
    }
    if (state->face == APP_FACE_SAVED) {
        return lv_color_hex(0x7df5bf);
    }
    if (!state->connected) {
        return lv_color_hex(0x3d6671);
    }
    return lv_color_hex(0x56e5ff);
}

static void set_eye(lv_obj_t *eye, int32_t x, int32_t y, int32_t width, int32_t height, lv_color_t color) {
    lv_obj_set_size(eye, width, height);
    lv_obj_set_pos(eye, x, y);
    lv_obj_set_style_radius(eye, height / 2, 0);
    lv_obj_set_style_bg_color(eye, color, 0);
    lv_obj_set_style_shadow_color(eye, color, 0);
    lv_obj_set_style_shadow_width(eye, 22, 0);
    lv_obj_set_style_shadow_opa(eye, LV_OPA_70, 0);
}

static void update_face(lv_timer_t *timer) {
    (void)timer;
    tick += 33;
    const app_state_snapshot_t state = app_state_snapshot();
    const lv_color_t color = eye_color_for(&state);
    const bool blink = state.face == APP_FACE_IDLE && (tick % 4200) < 120;
    const int32_t eye_height = blink ? 5 : (state.face == APP_FACE_LISTENING ? 58 : 48);
    const int32_t gaze = state.face == APP_FACE_THINKING ? (int32_t)((tick / 120) % 20) - 10 : 0;

    set_eye(left_eye, 92 + gaze, 184 - eye_height / 2, 104, eye_height, color);
    set_eye(right_eye, 270 + gaze, 184 - eye_height / 2, 104, eye_height, color);

    const int32_t mouth_height = state.face == APP_FACE_SPEAKING ? 8 + (state.mouth_level * 54 / 255) : 8;
    lv_obj_set_size(mouth, 96, mouth_height);
    lv_obj_set_pos(mouth, 185, 282 - mouth_height / 2);
    lv_obj_set_style_radius(mouth, mouth_height / 2, 0);
    lv_obj_set_style_bg_color(mouth, color, 0);
    lv_obj_set_style_shadow_color(mouth, color, 0);
    lv_obj_set_style_shadow_width(mouth, 16, 0);
    lv_obj_set_style_shadow_opa(mouth, state.face == APP_FACE_SPEAKING ? LV_OPA_60 : LV_OPA_20, 0);

    const bool show_ring = state.face == APP_FACE_LISTENING || state.face == APP_FACE_THINKING;
    lv_obj_set_style_border_color(ring, color, 0);
    lv_obj_set_style_border_width(ring, show_ring ? 3 : 0, 0);
    lv_obj_set_style_border_opa(ring, show_ring ? LV_OPA_80 : LV_OPA_TRANSP, 0);
    if (show_ring) {
        const int32_t phase = (int32_t)((tick / 80) % 18);
        lv_obj_set_size(ring, 352 + phase, 352 + phase);
        lv_obj_center(ring);
    }

    lv_label_set_text(status_label, state.status);
    char battery_text[16];
    snprintf(battery_text, sizeof(battery_text), "%u%%", state.battery_percent);
    lv_label_set_text(battery_label, battery_text);
}

static void on_touch(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    const app_state_snapshot_t state = app_state_snapshot();

    if (code == LV_EVENT_PRESSED) {
        if (state.connected && !capture_requested) {
            capture_requested = true;
            voice_runtime_request_capture(true);
        }
        return;
    }

    if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && capture_requested) {
        capture_requested = false;
        voice_runtime_request_capture(false);
    }
}

esp_err_t face_ui_init(void) {
    bsp_display_start();
    bsp_display_lock(-1);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    ring = lv_obj_create(screen);
    lv_obj_set_size(ring, 352, 352);
    lv_obj_center(ring);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    left_eye = lv_obj_create(screen);
    right_eye = lv_obj_create(screen);
    mouth = lv_obj_create(screen);
    for (lv_obj_t *part = left_eye; part != NULL; part = part == left_eye ? right_eye : part == right_eye ? mouth : NULL) {
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_style_pad_all(part, 0, 0);
        lv_obj_clear_flag(part, LV_OBJ_FLAG_CLICKABLE);
    }

    status_label = lv_label_create(screen);
    lv_obj_set_width(status_label, 340);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xb6d9e1), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);

    battery_label = lv_label_create(screen);
    lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -44, 30);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0x76a3ad), 0);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_16, 0);

    touch_target = lv_obj_create(screen);
    lv_obj_set_size(touch_target, 380, 380);
    lv_obj_center(touch_target);
    lv_obj_set_style_bg_opa(touch_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_target, 0, 0);
    lv_obj_set_style_radius(touch_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(touch_target, on_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch_target, on_touch, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(touch_target, on_touch, LV_EVENT_PRESS_LOST, NULL);

    lv_timer_create(update_face, 33, NULL);
    update_face(NULL);
    bsp_display_unlock();
    ESP_LOGI(TAG, "procedural face ready");
    return ESP_OK;
}
