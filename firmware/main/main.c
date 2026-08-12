#include "app_state.h"
#include "button_input.h"
#include "face_ui.h"
#include "nvs_flash.h"
#include "voice_runtime.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "pendant";

void app_main(void) {
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    app_state_init();
    ESP_ERROR_CHECK(face_ui_init());
    error = voice_runtime_init();
    if (error != ESP_OK) {
        // Keep the display usable if BLE setup fails.  A reboot loop looks
        // like a blinking pairing animation and makes recovery needlessly hard.
        ESP_LOGE(TAG, "voice runtime unavailable: %s", esp_err_to_name(error));
        app_state_set_face(APP_FACE_ERROR, "Bluetooth unavailable");
        return;
    }
    error = button_input_init();
    if (error != ESP_OK) {
        // Physical controls are an enhancement, not a reason to take the
        // renderer down. The moon/orbital animation must remain available.
        ESP_LOGW(TAG, "physical controls unavailable: %s", esp_err_to_name(error));
    }
    ESP_LOGI(TAG, "Pendant ready");
}
