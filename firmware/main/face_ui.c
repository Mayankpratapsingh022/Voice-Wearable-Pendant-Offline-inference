#include "face_ui.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"
#include "bsp/esp32_s3_touch_amoled_1_75c.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mmap_assets.h"
#include "nvs.h"
#include "lvgl.h"
#include "src/libs/qrcode/lv_qrcode.h"
#include "voice_runtime.h"

static const char *TAG = "moon_ui";

#define DISPLAY_SIZE 466
#define GLYPH_SIZE 8
#define GRID_COLS 58
#define GRID_ROWS 58
#define FRAME_INTERVAL_MS 33
#define MOON_FRAME_INTERVAL_MS 200
#define ORBIT_FRAME_INTERVAL_MS 67
#define ORBIT_FRAME_COUNT 38
#define ORBIT_PALETTE_SIZE (256 * 4)
#define ORBIT_STRAND_COUNT 22
#define ORBIT_POINTS_PER_STRAND 72
#define CONNECTED_TICK_DURATION_MS 1100
#define LOOP_DURATION_MS 3500
#define RADAR_LOOP_DURATION_MS 4200
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
static lv_obj_t *orbital_lines[ORBIT_STRAND_COUNT];
static lv_point_precise_t orbital_points[ORBIT_STRAND_COUNT][ORBIT_POINTS_PER_STRAND];
static lv_obj_t *radar_rings[3];
static lv_obj_t *radar_beam;
static lv_obj_t *radar_center;
static lv_obj_t *pair_qr;
static lv_obj_t *pair_caption;
static lv_obj_t *pair_touch_target;
static lv_obj_t *connected_tick;
static mmap_assets_handle_t orbit_assets;
static lv_image_dsc_t orbit_frame_dsc[ORBIT_FRAME_COUNT];
static lv_image_dsc_t orbit_render_dsc;
static uint16_t *orbit_render_pixels;
static char glyph_buffer[GLYPH_BUFFER_SIZE];
static bool processing_visible;
static bool pairing_visible;
static bool tick_visible;
static bool connection_was_active;
static face_ui_visual_profile_t visual_profile = FACE_UI_VISUAL_ORBITAL_LOOP;
static uint8_t moon_phase = 100;
static float visual_energy;
static bool orbital_lines_visible;
static uint32_t tick_ms;
static uint32_t last_moon_render_ms;
static uint32_t last_orbit_frame_ms;
static uint32_t orbit_frame_index;
static uint32_t connected_tick_until_ms;
static char rendered_pairing_uri[48];
static const lv_point_precise_t connected_tick_points[] = {{142, 244}, {205, 306}, {332, 156}};
static const lv_point_precise_t radar_beam_points[] = {{233, 233}, {233, 55}};

static void set_orbital_lines_visible(bool visible) {
    if (orbital_lines_visible == visible) {
        return;
    }
    orbital_lines_visible = visible;
    for (uint32_t strand = 0; strand < ORBIT_STRAND_COUNT; ++strand) {
        if (visible) {
            lv_obj_clear_flag(orbital_lines[strand], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(orbital_lines[strand], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static esp_err_t render_orbit_pixels(uint32_t index) {
    ESP_RETURN_ON_FALSE(orbit_render_pixels != NULL && index < ORBIT_FRAME_COUNT,
                        ESP_ERR_INVALID_STATE, TAG, "orbital render buffer unavailable");
    // I8 assets start with a 256-entry BGRA palette. Their pixel indices are
    // source luminance; copying that into a native RGB565 canvas avoids the
    // alpha/indexed decoder paths that produce a solid green/white fill on
    // this AMOLED driver.
    const uint8_t *source = orbit_frame_dsc[index].data + ORBIT_PALETTE_SIZE;
    const size_t pixel_count = DISPLAY_SIZE * DISPLAY_SIZE;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const uint8_t level = source[pixel];
        orbit_render_pixels[pixel] = (uint16_t)(((uint16_t)(level >> 3) << 11) |
                                                ((uint16_t)(level >> 2) << 5) |
                                                (level >> 3));
    }
    lv_obj_invalidate(orbit_image);
    return ESP_OK;
}

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
        if (data == NULL || data_size != (int)(sizeof(lv_image_header_t) + ORBIT_PALETTE_SIZE + DISPLAY_SIZE * DISPLAY_SIZE) ||
            header->magic != LV_IMAGE_HEADER_MAGIC || header->cf != LV_COLOR_FORMAT_I8 ||
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
    return state->face == APP_FACE_LISTENING || state->face == APP_FACE_TRANSCRIBING ||
           state->face == APP_FACE_THINKING || state->face == APP_FACE_SPEAKING;
}

static void set_processing_visible(bool visible) {
    if (processing_visible == visible) {
        return;
    }
    processing_visible = visible;
    if (visible) {
        last_orbit_frame_ms = tick_ms - ORBIT_FRAME_INTERVAL_MS;
    }
    // Use the approved orbital-frame sequence rather than the fallback line
    // geometry. RGB565 is safe on this AMOLED driver; indexed source frames
    // were the cause of the earlier green/white corruption.
    set_orbital_lines_visible(false);
    if (visible) {
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
        lv_obj_clear_flag(radar_beam, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(radar_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pair_qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pair_caption, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(radar_beam, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(radar_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pair_qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pair_caption, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_pairing_qr(const app_state_snapshot_t *state) {
    const char *uri = state->pairing_uri[0] == '\0' ? "PENDANT:V1:DISCOVER" : state->pairing_uri;
    if (strcmp(uri, rendered_pairing_uri) == 0) return;
    const lv_result_t result = lv_qrcode_update(pair_qr, uri, strlen(uri));
    if (result == LV_RESULT_OK) {
        strlcpy(rendered_pairing_uri, uri, sizeof(rendered_pairing_uri));
        ESP_LOGI(TAG, "pair QR rendered: %s", uri);
    } else {
        ESP_LOGE(TAG, "pair QR render failed: %d", result);
    }
}

static void on_pairing_touch(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    const app_state_snapshot_t state = app_state_snapshot();
    if (code == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG, "display touch detected (connected=%d face=%d)", state.connected, state.face);
        return;
    }
    // The display itself is the reliable QR entry point. It deliberately does
    // not begin a capture, and remains available even if an old BLE link has
    // left the generic connection state true.
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "display tap: toggling pairing QR");
        voice_runtime_toggle_pairing_qr();
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
    // One unbroken, slow revolution reads as a radar sweep. At 30fps this is
    // only 2.8° per frame, so the line no longer looks like a travelling arc.
    const float progress = (float)(tick_ms % RADAR_LOOP_DURATION_MS) / RADAR_LOOP_DURATION_MS;
    lv_obj_set_style_transform_rotation(radar_beam, (int32_t)(progress * 3600.0f), 0);
}

static void render_orbit_frame(void) {
    const app_state_snapshot_t state = app_state_snapshot();
    const bool active_voice = state.face == APP_FACE_LISTENING || state.face == APP_FACE_SPEAKING;
    const uint8_t source_level = state.face == APP_FACE_LISTENING ? state.input_level : state.mouth_level;
    const float target = active_voice ? (float)source_level / 255.0f : 0.0f;
    // Fast attack lets a spoken syllable feel connected to the strands while
    // the slower release prevents harsh flicker in the original artwork.
    visual_energy += (target - visual_energy) * (target > visual_energy ? .42f : .10f);
    const uint32_t interval = active_voice
                                  ? (uint32_t)(ORBIT_FRAME_INTERVAL_MS - visual_energy * 18.0f)
                                  : ORBIT_FRAME_INTERVAL_MS;
    if ((uint32_t)(tick_ms - last_orbit_frame_ms) < interval) {
        return;
    }
    last_orbit_frame_ms = tick_ms;
    ESP_ERROR_CHECK_WITHOUT_ABORT(render_orbit_pixels(orbit_frame_index));
    orbit_frame_index = (orbit_frame_index + 1) % ORBIT_FRAME_COUNT;
}

static void update_moon(lv_timer_t *timer) {
    (void)timer;
    tick_ms += FRAME_INTERVAL_MS;
    const app_state_snapshot_t state = app_state_snapshot();
    if (state.connected && !connection_was_active) {
        connected_tick_until_ms = tick_ms + CONNECTED_TICK_DURATION_MS;
    }
    connection_was_active = state.connected;

    const bool show_tick = state.connected && tick_ms < connected_tick_until_ms;
    const bool show_pairing = state.face == APP_FACE_PAIRING;
    // The orbital waveform is now the only normal face. Moon remains out of
    // the render path until it is intentionally brought back in a later UI.
    const bool show_orbit = !show_tick && !show_pairing;
    set_processing_visible(show_orbit);
    set_pairing_visible(show_pairing);
    set_connected_tick_visible(show_tick);
    if (show_tick || show_pairing || show_orbit) {
        lv_obj_add_flag(glyph_field, LV_OBJ_FLAG_HIDDEN);
        if (show_pairing) {
            update_pairing_qr(&state);
            update_radar();
        } else if (show_orbit) {
            render_orbit_frame();
        }
        return;
    }
}

face_ui_visual_profile_t face_ui_visual_profile(void) {
    return visual_profile;
}

esp_err_t face_ui_set_visual_profile(face_ui_visual_profile_t profile) {
    if (profile > FACE_UI_VISUAL_ORBITAL_STILL) return ESP_ERR_INVALID_ARG;
    visual_profile = profile;
    nvs_handle_t handle;
    const esp_err_t open_result = nvs_open("visual", NVS_READWRITE, &handle);
    if (open_result != ESP_OK) return open_result;
    const esp_err_t set_result = nvs_set_u8(handle, "profile", (uint8_t)profile);
    const esp_err_t commit_result = set_result == ESP_OK ? nvs_commit(handle) : set_result;
    nvs_close(handle);
    return commit_result;
}

uint8_t face_ui_moon_phase(void) {
    return moon_phase;
}

esp_err_t face_ui_set_moon_phase(uint8_t phase) {
    if (phase > 100) return ESP_ERR_INVALID_ARG;
    moon_phase = phase;
    nvs_handle_t handle;
    const esp_err_t open_result = nvs_open("visual", NVS_READWRITE, &handle);
    if (open_result != ESP_OK) return open_result;
    const esp_err_t set_result = nvs_set_u8(handle, "moon_phase", phase);
    const esp_err_t commit_result = set_result == ESP_OK ? nvs_commit(handle) : set_result;
    nvs_close(handle);
    return commit_result;
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

    orbit_image = lv_image_create(screen);
    lv_obj_set_size(orbit_image, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_set_pos(orbit_image, 0, 0);
    lv_obj_set_style_image_recolor_opa(orbit_image, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(orbit_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(orbit_image, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(orbit_image, LV_OBJ_FLAG_HIDDEN);
    ESP_RETURN_ON_ERROR(init_orbit_frames(), TAG, "orbital frames unavailable");
    orbit_render_pixels = heap_caps_malloc(DISPLAY_SIZE * DISPLAY_SIZE * sizeof(uint16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(orbit_render_pixels != NULL, ESP_ERR_NO_MEM, TAG, "orbital render buffer unavailable");
    memset(&orbit_render_dsc, 0, sizeof(orbit_render_dsc));
    orbit_render_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    orbit_render_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    orbit_render_dsc.header.w = DISPLAY_SIZE;
    orbit_render_dsc.header.h = DISPLAY_SIZE;
    orbit_render_dsc.data_size = DISPLAY_SIZE * DISPLAY_SIZE * sizeof(uint16_t);
    orbit_render_dsc.data = (const uint8_t *)orbit_render_pixels;
    lv_image_set_src(orbit_image, &orbit_render_dsc);

    for (uint32_t strand = 0; strand < ORBIT_STRAND_COUNT; ++strand) {
        orbital_lines[strand] = lv_line_create(screen);
        lv_obj_set_size(orbital_lines[strand], DISPLAY_SIZE, DISPLAY_SIZE);
        lv_obj_set_pos(orbital_lines[strand], 0, 0);
        lv_obj_set_style_line_color(orbital_lines[strand], lv_color_hex(0xe9efff), 0);
        lv_obj_set_style_line_width(orbital_lines[strand], strand % 7 == 0 ? 2 : 1, 0);
        lv_obj_set_style_line_rounded(orbital_lines[strand], true, 0);
        lv_obj_clear_flag(orbital_lines[strand], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(orbital_lines[strand], LV_OBJ_FLAG_HIDDEN);
    }

    const int32_t radar_sizes[] = {330, 230, 130};
    for (uint32_t index = 0; index < 3; ++index) {
        radar_rings[index] = lv_obj_create(screen);
        lv_obj_set_size(radar_rings[index], radar_sizes[index], radar_sizes[index]);
        lv_obj_center(radar_rings[index]);
        lv_obj_set_style_bg_opa(radar_rings[index], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(radar_rings[index], lv_color_hex(RADAR_GREEN_DIM), 0);
        lv_obj_set_style_border_width(radar_rings[index], index == 0 ? 4 : 3, 0);
        lv_obj_set_style_radius(radar_rings[index], LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(radar_rings[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(radar_rings[index], LV_OBJ_FLAG_HIDDEN);
    }

    radar_beam = lv_line_create(screen);
    lv_line_set_points(radar_beam, radar_beam_points, 2);
    lv_obj_set_size(radar_beam, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_set_pos(radar_beam, 0, 0);
    lv_obj_set_style_line_color(radar_beam, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_line_width(radar_beam, 3, 0);
    lv_obj_set_style_line_rounded(radar_beam, true, 0);
    lv_obj_set_style_line_opa(radar_beam, LV_OPA_80, 0);
    lv_obj_set_style_shadow_color(radar_beam, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_shadow_width(radar_beam, 18, 0);
    lv_obj_set_style_shadow_opa(radar_beam, LV_OPA_60, 0);
    lv_obj_set_style_transform_pivot_x(radar_beam, DISPLAY_SIZE / 2, 0);
    lv_obj_set_style_transform_pivot_y(radar_beam, DISPLAY_SIZE / 2, 0);
    lv_obj_clear_flag(radar_beam, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(radar_beam, LV_OBJ_FLAG_HIDDEN);

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

    pair_qr = lv_qrcode_create(screen);
    // Large modules plus a quiet zone are essential for a phone camera at
    // normal pendant distance; 128px was too small to focus reliably.
    lv_qrcode_set_size(pair_qr, 190);
    lv_qrcode_set_dark_color(pair_qr, lv_color_hex(0x050507));
    lv_qrcode_set_light_color(pair_qr, lv_color_hex(0xf2fff7));
    lv_qrcode_set_quiet_zone(pair_qr, true);
    lv_obj_center(pair_qr);
    lv_obj_clear_flag(pair_qr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(pair_qr, LV_OBJ_FLAG_HIDDEN);

    pair_caption = lv_label_create(screen);
    lv_label_set_text(pair_caption, "SCAN TO LINK");
    lv_obj_set_style_text_color(pair_caption, lv_color_hex(RADAR_GREEN), 0);
    lv_obj_set_style_text_font(pair_caption, &lv_font_montserrat_12, 0);
    lv_obj_align(pair_caption, LV_ALIGN_CENTER, 0, 126);
    lv_obj_clear_flag(pair_caption, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(pair_caption, LV_OBJ_FLAG_HIDDEN);

    // Full-screen transparent fallback for board revisions whose PWR key is
    // unavailable. A user tap exposes the QR radar; the next tap restores the
    // moon. It intercepts no connected-state interaction.
    pair_touch_target = lv_obj_create(screen);
    lv_obj_set_size(pair_touch_target, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_set_pos(pair_touch_target, 0, 0);
    lv_obj_set_style_bg_opa(pair_touch_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(pair_touch_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(pair_touch_target, 0, 0);
    // Do not rely on LVGL's object-default flags: this is the physical
    // interaction surface for pairing and must always receive touch events.
    lv_obj_add_flag(pair_touch_target, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pair_touch_target, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pair_touch_target, on_pairing_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(pair_touch_target, on_pairing_touch, LV_EVENT_CLICKED, NULL);

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

    nvs_handle_t visual_handle;
    if (nvs_open("visual", NVS_READONLY, &visual_handle) == ESP_OK) {
        uint8_t stored_profile = FACE_UI_VISUAL_ORBITAL_LOOP;
        if (nvs_get_u8(visual_handle, "profile", &stored_profile) == ESP_OK &&
            stored_profile <= FACE_UI_VISUAL_ORBITAL_STILL) {
            visual_profile = (face_ui_visual_profile_t)stored_profile;
        }
        uint8_t stored_phase = moon_phase;
        if (nvs_get_u8(visual_handle, "moon_phase", &stored_phase) == ESP_OK && stored_phase <= 100) {
            moon_phase = stored_phase;
        }
        nvs_close(visual_handle);
    }

    // The default screensaver is the approved orbital loop. Moon loop/still
    // remain selectable from the companion and keep their settings after the
    // migration.
    nvs_handle_t migration_handle;
    if (nvs_open("visual", NVS_READWRITE, &migration_handle) == ESP_OK) {
        uint8_t migrated = 0;
        nvs_get_u8(migration_handle, "orbital_v3", &migrated);
        if (migrated == 0 && (visual_profile == FACE_UI_VISUAL_MOON_LOOP ||
                              visual_profile == FACE_UI_VISUAL_ORBITAL_STILL)) {
            visual_profile = FACE_UI_VISUAL_ORBITAL_LOOP;
            nvs_set_u8(migration_handle, "profile", (uint8_t)visual_profile);
            ESP_LOGI(TAG, "migrated default screensaver to orbital loop");
        }
        nvs_set_u8(migration_handle, "orbital_v3", 1);
        nvs_commit(migration_handle);
        nvs_close(migration_handle);
    }

    last_orbit_frame_ms = tick_ms - ORBIT_FRAME_INTERVAL_MS;
    render_orbit_frame();
    lv_timer_create(update_moon, FRAME_INTERVAL_MS, NULL);
    bsp_display_unlock();
    ESP_LOGI(TAG, "ASCII lunar phase loop + rendered orbital strands ready");
    return ESP_OK;
}
