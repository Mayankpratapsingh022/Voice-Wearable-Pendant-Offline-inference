#include "face_ui.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"
#include "bsp/esp32_s3_touch_amoled_1_75c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mmap_assets.h"
#include "lvgl.h"
#include "voice_runtime.h"

static const char *TAG = "moon_ui";

#define DISPLAY_SIZE 466
#define GLYPH_SIZE 8
#define GRID_COLS 58
#define GRID_ROWS 58
#define FRAME_INTERVAL_MS 33
#define MOON_FRAME_INTERVAL_MS 200
#define ORBIT_FRAME_INTERVAL_MS 66
#define ORBIT_FRAME_COUNT 38
#define CONNECTED_TICK_DURATION_MS 1100
#define LOOP_DURATION_MS 3500
#define PI_F 3.14159265f
#define CRESCENT_ENDPOINT_RAD .95f

#define INK_COLOR 0x050507
#define GLYPH_COLOR 0xeee7d9
#define RADAR_GREEN 0x48f59b
#define RADAR_GREEN_DIM 0x15583b

typedef struct {
    float x;
    float y;
    float rx;
    float ry;
    float darkness;
} mare_t;

typedef struct {
    float x;
    float y;
    float radius;
    float depth;
    float rim;
} crater_t;

/* These are the same surface landmarks used by the ASCII loop preview. */
static const mare_t maria[] = {
    {-.43f, -.13f, .23f, .14f, .42f}, {-.16f, -.10f, .29f, .18f, .34f},
    { .18f, -.21f, .17f, .10f, .25f}, { .38f,  .15f, .27f, .16f, .39f},
    { .05f,  .26f, .24f, .13f, .35f}, {-.30f,  .31f, .16f, .105f,.26f},
    {-.06f,  .47f, .24f, .09f, .30f}, { .55f, -.39f, .16f, .08f, .22f},
    {-.66f,  .28f, .13f, .09f, .23f},
};

static const crater_t craters[] = {
    {-.66f,-.29f,.115f,.34f,.40f}, {-.45f,-.35f,.150f,.38f,.44f},
    {-.24f,-.46f,.065f,.24f,.27f}, {-.06f,-.47f,.085f,.28f,.31f},
    { .16f,-.40f,.105f,.32f,.38f}, { .38f,-.30f,.072f,.25f,.29f},
    { .58f,-.17f,.140f,.38f,.45f}, {-.68f, .04f,.078f,.25f,.29f},
    {-.36f,-.02f,.175f,.40f,.46f}, {-.04f, .04f,.075f,.24f,.28f},
    { .25f,-.02f,.115f,.34f,.39f}, { .48f, .11f,.090f,.27f,.31f},
    {-.55f, .29f,.105f,.31f,.36f}, {-.25f, .27f,.062f,.22f,.25f},
    { .03f, .27f,.135f,.37f,.43f}, { .32f, .33f,.078f,.25f,.29f},
    { .60f, .39f,.120f,.34f,.40f}, {-.40f, .50f,.070f,.23f,.26f},
    {-.08f, .50f,.090f,.27f,.31f}, { .19f, .49f,.065f,.22f,.25f},
};

#define MARE_COUNT (sizeof(maria) / sizeof(maria[0]))
#define CRATER_COUNT (sizeof(craters) / sizeof(craters[0]))
#define GLYPH_BUFFER_SIZE ((GRID_COLS + 1) * GRID_ROWS + 1)

static lv_obj_t *glyph_field;
static lv_obj_t *orbit_image;
static lv_obj_t *radar_rings[3];
static lv_obj_t *radar_sweep;
static lv_obj_t *radar_blip;
static lv_obj_t *radar_center;
static lv_obj_t *connected_tick;
static mmap_assets_handle_t orbit_assets;
static lv_image_dsc_t orbit_frame_dsc[ORBIT_FRAME_COUNT];
static char glyph_buffer[GLYPH_BUFFER_SIZE];
static bool capture_requested;
static bool processing_visible;
static bool processing_dismissed;
static bool processing_loop_requested;
static bool pairing_visible;
static bool tick_visible;
static bool connection_was_active;
static uint32_t tick_ms;
static uint32_t last_moon_render_ms;
static uint32_t last_orbit_frame_ms;
static uint32_t orbit_frame_index;
static uint32_t connected_tick_until_ms;
static const lv_point_precise_t connected_tick_points[] = {{142, 244}, {205, 306}, {332, 156}};

static esp_err_t init_orbit_frames(void) {
    const mmap_assets_config_t config = {
        .partition_label = "orbit_frames",
        .flags = {
            .mmap_enable = true,
        },
    };
    esp_err_t result = mmap_assets_new(&config, &orbit_assets);
    if (result != ESP_OK) {
        return result;
    }

    if (mmap_assets_get_stored_files(orbit_assets) != ORBIT_FRAME_COUNT) {
        ESP_LOGE(TAG, "orbital frame count mismatch");
        return ESP_ERR_INVALID_SIZE;
    }

    for (uint32_t index = 0; index < ORBIT_FRAME_COUNT; ++index) {
        const uint8_t *data = mmap_assets_get_mem(orbit_assets, index);
        const int data_size = mmap_assets_get_size(orbit_assets, index);
        const lv_image_header_t *header = (const lv_image_header_t *)data;
        if (data == NULL || data_size != (int)(sizeof(lv_image_header_t) + DISPLAY_SIZE * DISPLAY_SIZE) ||
            header->magic != LV_IMAGE_HEADER_MAGIC || header->cf != LV_COLOR_FORMAT_A8 ||
            header->w != DISPLAY_SIZE || header->h != DISPLAY_SIZE) {
            ESP_LOGE(TAG, "invalid orbital frame %" PRIu32, index);
            return ESP_ERR_INVALID_SIZE;
        }
        orbit_frame_dsc[index].header = *header;
        orbit_frame_dsc[index].data_size = data_size - sizeof(lv_image_header_t);
        orbit_frame_dsc[index].data = data + sizeof(lv_image_header_t);
    }
    return ESP_OK;
}

static float clampf(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    return value > maximum ? maximum : value;
}

static float wrapf(float value) {
    while (value > 1.0f) {
        value -= 2.0f;
    }
    while (value < -1.0f) {
        value += 2.0f;
    }
    return value;
}

static uint32_t glyph_hash(int32_t x, int32_t y) {
    uint32_t value = (uint32_t)(x * 374761393) + (uint32_t)(y * 668265263);
    value = (value ^ (value >> 13)) * 1274126177U;
    return value ^ (value >> 16);
}

static void render_ascii_moon(float theta) {
    static const char glyphs[] = " .,:;irsXA253hMHGS#9B&@";
    char *output = glyph_buffer;
    const float rotation = theta / PI_F;
    const float sun_tilt = .46f + sinf(theta * 2.0f) * .075f;
    const float sine_theta = sinf(theta);
    const float cosine_theta = cosf(theta);

    for (int32_t y = 0; y < GRID_ROWS; ++y) {
        for (int32_t x = 0; x < GRID_COLS; ++x) {
            const float px = (x - (GRID_COLS - 1) * .5f) / (GRID_COLS * .505f);
            const float py = (y - (GRID_ROWS - 1) * .5f) / (GRID_ROWS * .505f);
            const float radius = sqrtf(px * px + py * py);
            const float edge_angle = atan2f(py, px);
            const float limb = 1.0f + sinf(edge_angle * 7.0f + .5f) * .011f + cosf(edge_angle * 13.0f) * .006f;

            if (radius <= limb) {
                const float surface_x = wrapf(px + rotation);
                const float z = sqrtf(fmaxf(0.0f, 1.0f - radius * radius));
                const float light_axis = px * cosf(sun_tilt) + py * sinf(sun_tilt);
                const float light = fmaxf(0.0f, light_axis * sine_theta - z * cosine_theta);
                const float grain = (glyph_hash((int32_t)(surface_x * 1000.0f), y) % 1000) / 1000.0f;
                float terrain = .69f + sinf(surface_x * 13.3f + py * 4.7f) * .065f +
                                cosf(py * 17.5f - surface_x * 5.2f) * .055f +
                                sinf((surface_x + py * .28f) * 34.0f) * .032f +
                                cosf((py - surface_x * .44f) * 41.0f) * .028f + (grain - .5f) * .15f;

                for (uint32_t index = 0; index < MARE_COUNT; ++index) {
                    const float dx = (surface_x - maria[index].x) / maria[index].rx;
                    const float dy = (py - maria[index].y) / maria[index].ry;
                    const float distance = dx * dx + dy * dy;
                    if (distance < 1.0f) {
                        terrain -= maria[index].darkness * powf(1.0f - distance, .52f);
                    }
                }

                for (uint32_t index = 0; index < CRATER_COUNT; ++index) {
                    const float dx = surface_x - craters[index].x;
                    const float dy = py - craters[index].y;
                    const float distance = sqrtf(dx * dx + dy * dy);
                    const float normalized = distance / craters[index].radius;
                    if (normalized < 1.0f) {
                        terrain -= craters[index].depth * 1.16f * powf(1.0f - normalized, .56f);
                        if (normalized < .22f) {
                            terrain += .11f * (1.0f - normalized / .22f);
                        }
                    }
                    terrain += expf(-powf((normalized - 1.0f) / .105f, 2.0f)) * craters[index].rim;
                }

                const float falloff = 1.0f - powf(clampf((limb - radius) / .12f, 0.0f, 1.0f), .35f) * .18f;
                const float density = clampf(light * terrain * falloff, 0.0f, 1.0f);
                *output++ = glyphs[(uint32_t)(density * (sizeof(glyphs) - 2))];
            } else if (radius < limb + .035f && glyph_hash(x + (int32_t)(theta * 11.0f), y) % 11U == 0U) {
                *output++ = '.';
            } else {
                *output++ = ' ';
            }
        }
        *output++ = '\n';
    }
    *output = '\0';
    // The label points at a static buffer.  Re-register it after its contents
    // change so LVGL clears its text-layout cache before the next refresh.
    lv_label_set_text_static(glyph_field, glyph_buffer);
    lv_obj_invalidate(glyph_field);
}

static bool should_show_processing(const app_state_snapshot_t *state) {
    if (processing_dismissed) {
        return false;
    }
    return processing_loop_requested || state->face == APP_FACE_LISTENING || state->face == APP_FACE_TRANSCRIBING ||
           state->face == APP_FACE_THINKING || state->face == APP_FACE_SPEAKING;
}

static void set_processing_visible(bool visible) {
    if (processing_visible == visible) {
        return;
    }
    processing_visible = visible;
    if (visible) {
        orbit_frame_index = 0;
        last_orbit_frame_ms = tick_ms - ORBIT_FRAME_INTERVAL_MS;
        lv_obj_clear_flag(orbit_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(orbit_image, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_pairing_visible(bool visible) {
    if (pairing_visible == visible) {
        return;
    }
    pairing_visible = visible;
    for (uint32_t index = 0; index < 3; ++index) {
        if (visible) {
            lv_obj_clear_flag(radar_rings[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(radar_rings[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (visible) {
        lv_obj_clear_flag(radar_sweep, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(radar_blip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(radar_center, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(radar_sweep, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(radar_blip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(radar_center, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_connected_tick_visible(bool visible) {
    if (tick_visible == visible) {
        return;
    }
    tick_visible = visible;
    if (visible) {
        lv_obj_clear_flag(connected_tick, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(connected_tick, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_radar(void) {
    const float progress = (float)(tick_ms % 2600U) / 2600.0f;
    const float angle = progress * 2.0f * PI_F;
    const int32_t center = DISPLAY_SIZE / 2;
    const int32_t radius = 148 + (int32_t)(sinf(angle * 3.0f) * 7.0f);
    lv_arc_set_rotation(radar_sweep, (int32_t)(progress * 360.0f));
    lv_obj_set_pos(radar_blip, center + (int32_t)(cosf(angle) * radius) - 7,
                   center + (int32_t)(sinf(angle) * radius) - 7);
}

static void render_orbit_frame(void) {
    if ((uint32_t)(tick_ms - last_orbit_frame_ms) < ORBIT_FRAME_INTERVAL_MS) {
        return;
    }
    last_orbit_frame_ms = tick_ms;
    lv_image_set_src(orbit_image, &orbit_frame_dsc[orbit_frame_index]);
    orbit_frame_index = (orbit_frame_index + 1) % ORBIT_FRAME_COUNT;
}

static void update_moon(lv_timer_t *timer) {
    (void)timer;
    tick_ms += FRAME_INTERVAL_MS;
    const app_state_snapshot_t state = app_state_snapshot();
    if (state.face == APP_FACE_IDLE || state.face == APP_FACE_DISCONNECTED || state.face == APP_FACE_PAIRING) {
        processing_dismissed = false;
    }
    if (state.connected && !connection_was_active) {
        connected_tick_until_ms = tick_ms + CONNECTED_TICK_DURATION_MS;
    }
    connection_was_active = state.connected;

    const bool show_tick = state.connected && tick_ms < connected_tick_until_ms;
    // Radar is reserved for the explicit pairing flow.  A merely disconnected
    // device still keeps its moon/orbital tap experience.
    const bool show_pairing = state.face == APP_FACE_PAIRING;
    const bool show_processing = !show_tick && !show_pairing && should_show_processing(&state);
    set_processing_visible(show_processing);
    set_pairing_visible(show_pairing);
    set_connected_tick_visible(show_tick);
    if (show_tick || show_pairing || show_processing) {
        lv_obj_add_flag(glyph_field, LV_OBJ_FLAG_HIDDEN);
        if (show_pairing) {
            update_radar();
        } else if (show_processing) {
            render_orbit_frame();
        }
        return;
    }
    lv_obj_clear_flag(glyph_field, LV_OBJ_FLAG_HIDDEN);
    if ((uint32_t)(tick_ms - last_moon_render_ms) < MOON_FRAME_INTERVAL_MS) {
        return;
    }
    last_moon_render_ms = tick_ms;
    const float progress = (float)(tick_ms % LOOP_DURATION_MS) / LOOP_DURATION_MS;
    // Keep the loop in the readable crescent-to-full range. A sine orbit is
    // seamless at the reset point and never holds a fully black new moon.
    const float theta = PI_F + (PI_F - CRESCENT_ENDPOINT_RAD) * sinf(progress * PI_F * 2.0f);
    render_ascii_moon(theta);
}

static void on_moon_touch(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    const app_state_snapshot_t state = app_state_snapshot();
    if (code == LV_EVENT_PRESSED) {
        if (should_show_processing(&state)) {
            // A second tap is an immediate visual escape back to the moon.
            processing_loop_requested = false;
            processing_dismissed = true;
            if (capture_requested) {
                capture_requested = false;
                voice_runtime_request_capture(false);
            }
            ESP_LOGI(TAG, "touch: return to moon");
            return;
        }
        // One tap enters the rendered orbital loop.  It intentionally stays
        // there after a voice request ends; only the next tap returns to moon.
        processing_dismissed = false;
        processing_loop_requested = true;
        ESP_LOGI(TAG, "touch: orbital loop");
        if (state.connected && !capture_requested) {
            capture_requested = true;
            voice_runtime_request_capture(true);
        }
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && capture_requested) {
        capture_requested = false;
        voice_runtime_request_capture(false);
    }
}

esp_err_t face_ui_init(void) {
    bsp_display_start();
    bsp_display_lock(-1);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(INK_COLOR), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    glyph_field = lv_label_create(screen);
    lv_obj_set_size(glyph_field, GRID_COLS * GLYPH_SIZE, GRID_ROWS * GLYPH_SIZE);
    lv_obj_set_pos(glyph_field, 1, 1);
    lv_obj_set_style_text_font(glyph_field, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(glyph_field, lv_color_hex(GLYPH_COLOR), 0);
    lv_obj_set_style_text_letter_space(glyph_field, 0, 0);
    lv_obj_set_style_text_line_space(glyph_field, 0, 0);
    lv_obj_set_style_bg_opa(glyph_field, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(glyph_field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(glyph_field, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_long_mode(glyph_field, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(glyph_field, glyph_buffer);

    ESP_RETURN_ON_ERROR(init_orbit_frames(), TAG, "orbital frames unavailable");
    orbit_image = lv_image_create(screen);
    lv_image_set_src(orbit_image, &orbit_frame_dsc[0]);
    lv_obj_set_size(orbit_image, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_set_pos(orbit_image, 0, 0);
    lv_obj_set_style_image_recolor(orbit_image, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(orbit_image, LV_OPA_COVER, 0);
    lv_obj_clear_flag(orbit_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(orbit_image, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(orbit_image, LV_OBJ_FLAG_HIDDEN);

    const int32_t radar_sizes[] = {330, 230, 130};
    for (uint32_t index = 0; index < 3; ++index) {
        radar_rings[index] = lv_obj_create(screen);
        lv_obj_set_size(radar_rings[index], radar_sizes[index], radar_sizes[index]);
        lv_obj_center(radar_rings[index]);
        lv_obj_set_style_bg_opa(radar_rings[index], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(radar_rings[index], lv_color_hex(RADAR_GREEN_DIM), 0);
        lv_obj_set_style_border_width(radar_rings[index], index == 0 ? 2 : 1, 0);
        lv_obj_set_style_radius(radar_rings[index], LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(radar_rings[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(radar_rings[index], LV_OBJ_FLAG_HIDDEN);
    }

    radar_sweep = lv_arc_create(screen);
    lv_obj_set_size(radar_sweep, 330, 330);
    lv_obj_center(radar_sweep);
    lv_arc_set_range(radar_sweep, 0, 100);
    lv_arc_set_value(radar_sweep, 19);
    lv_arc_set_bg_angles(radar_sweep, 0, 360);
    lv_obj_set_style_arc_opa(radar_sweep, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(radar_sweep, lv_color_hex(RADAR_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(radar_sweep, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(radar_sweep, true, LV_PART_INDICATOR);
    lv_obj_clear_flag(radar_sweep, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(radar_sweep, LV_OBJ_FLAG_HIDDEN);

    radar_blip = lv_obj_create(screen);
    lv_obj_set_size(radar_blip, 14, 14);
    lv_obj_set_style_radius(radar_blip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(radar_blip, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_shadow_color(radar_blip, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_shadow_width(radar_blip, 18, 0);
    lv_obj_set_style_shadow_opa(radar_blip, LV_OPA_70, 0);
    lv_obj_clear_flag(radar_blip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(radar_blip, LV_OBJ_FLAG_HIDDEN);

    radar_center = lv_obj_create(screen);
    lv_obj_set_size(radar_center, 16, 16);
    lv_obj_center(radar_center);
    lv_obj_set_style_radius(radar_center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(radar_center, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_shadow_color(radar_center, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_shadow_width(radar_center, 20, 0);
    lv_obj_set_style_shadow_opa(radar_center, LV_OPA_60, 0);
    lv_obj_clear_flag(radar_center, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(radar_center, LV_OBJ_FLAG_HIDDEN);

    connected_tick = lv_line_create(screen);
    lv_line_set_points(connected_tick, connected_tick_points, 3);
    lv_obj_set_size(connected_tick, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_set_style_line_color(connected_tick, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_line_width(connected_tick, 12, 0);
    lv_obj_set_style_line_rounded(connected_tick, true, 0);
    lv_obj_set_style_shadow_color(connected_tick, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_shadow_width(connected_tick, 18, 0);
    lv_obj_set_style_shadow_opa(connected_tick, LV_OPA_70, 0);
    lv_obj_clear_flag(connected_tick, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(connected_tick, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *touch_target = lv_obj_create(screen);
    lv_obj_set_size(touch_target, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_set_pos(touch_target, 0, 0);
    lv_obj_set_style_bg_opa(touch_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_target, 0, 0);
    lv_obj_set_style_pad_all(touch_target, 0, 0);
    lv_obj_add_flag(touch_target, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(touch_target, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touch_target, on_moon_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch_target, on_moon_touch, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(touch_target, on_moon_touch, LV_EVENT_PRESS_LOST, NULL);

    render_ascii_moon(PI_F);
    lv_timer_create(update_moon, FRAME_INTERVAL_MS, NULL);
    bsp_display_unlock();
    ESP_LOGI(TAG, "ASCII lunar phase loop + rendered orbital strands ready");
    return ESP_OK;
}
