#include "app_state.h"
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
    ESP_ERROR_CHECK(voice_runtime_init());
    ESP_LOGI(TAG, "Pendant ready");
}
