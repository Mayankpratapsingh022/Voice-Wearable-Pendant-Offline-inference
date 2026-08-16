#pragma once

#include "esp_err.h"

typedef enum {
    FACE_UI_VISUAL_MOON_LOOP = 0,
    FACE_UI_VISUAL_MOON_STILL = 1,
    FACE_UI_VISUAL_ORBITAL_LOOP = 2,
    FACE_UI_VISUAL_ORBITAL_STILL = 3,
} face_ui_visual_profile_t;

esp_err_t face_ui_init(void);
face_ui_visual_profile_t face_ui_visual_profile(void);
esp_err_t face_ui_set_visual_profile(face_ui_visual_profile_t profile);
uint8_t face_ui_moon_phase(void);
esp_err_t face_ui_set_moon_phase(uint8_t phase);
