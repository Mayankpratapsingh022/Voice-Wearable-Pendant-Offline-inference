#include "button_input.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp/esp32_s3_touch_amoled_1_75c.h"
#include "app_state.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "voice_runtime.h"

/* The schematic routes BOOT directly to GPIO0 and PWR to AXP2101 PWRON. */
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define AXP2101_I2C_ADDRESS 0x34
#define AXP2101_IRQ_ENABLE_1 0x41
#define AXP2101_IRQ_STATUS_1 0x49
#define AXP2101_PKEY_SHORT_PRESS (1U << 3)
#define BOOT_DEBOUNCE_MS 35U
#define BOOT_DOUBLE_PRESS_MS 420U
#define BUTTON_POLL_MS 10U
#define PWR_POLL_DIVIDER 8U
#define BOOT_GUARD_MS 1200U
#define PWR_PRESS_COOLDOWN_MS 900U

static const char *TAG = "button_input";
static i2c_master_dev_handle_t pmic;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t pmic_read(uint8_t reg, uint8_t *value) {
    return i2c_master_transmit_receive(pmic, &reg, 1, value, 1, 50);
}

static esp_err_t pmic_write(uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(pmic, data, sizeof(data), 50);
}

static bool poll_power_button(void) {
    if (pmic == NULL) {
        return false;
    }
    uint8_t status = 0;
    if (pmic_read(AXP2101_IRQ_STATUS_1, &status) != ESP_OK || (status & AXP2101_PKEY_SHORT_PRESS) == 0) {
        return false;
    }
    // IRQ status is write-one-to-clear. Do not touch long-press status: that
    // remains reserved for the PMIC's physical power-off behaviour.
    if (pmic_write(AXP2101_IRQ_STATUS_1, AXP2101_PKEY_SHORT_PRESS) != ESP_OK) {
        ESP_LOGW(TAG, "unable to clear PWR key status");
    }
    return true;
}

static void button_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BOOT_GUARD_MS));

    int boot_candidate = gpio_get_level(BOOT_BUTTON_GPIO);
    int boot_stable = boot_candidate;
    uint32_t boot_candidate_since = now_ms();
    uint32_t last_boot_release = 0;
    uint32_t last_boot_pairing_toggle = 0;
    uint32_t last_power_press = 0;
    uint32_t poll_counter = 0;

    for (;;) {
        const uint32_t now = now_ms();
        const int raw_boot = gpio_get_level(BOOT_BUTTON_GPIO);
        if (raw_boot != boot_candidate) {
            boot_candidate = raw_boot;
            boot_candidate_since = now;
        } else if (boot_candidate != boot_stable && now - boot_candidate_since >= BOOT_DEBOUNCE_MS) {
            boot_stable = boot_candidate;
            ESP_LOGI(TAG, "BOOT %s", boot_stable == 0 ? "pressed" : "released");
            // BOOT is active-low. Count a release as one deliberate click.
            if (boot_stable != 0) {
                if (last_boot_release != 0 && now - last_boot_release <= BOOT_DOUBLE_PRESS_MS) {
                    last_boot_release = 0;
                    ESP_LOGI(TAG, "BOOT double press: meeting toggle");
                    voice_runtime_request_meeting_toggle();
                } else {
                    last_boot_release = now;
                }
            }
        }
        if (last_boot_release != 0 && now - last_boot_release > BOOT_DOUBLE_PRESS_MS) {
            last_boot_release = 0;
            // A single BOOT press is a reliable alternate QR trigger when
            // the PMIC PWR key is unavailable on a board revision.
            ESP_LOGI(TAG, "BOOT single press: toggling pairing QR");
            last_boot_pairing_toggle = now;
            voice_runtime_toggle_pairing_qr();
        }

        if (++poll_counter >= PWR_POLL_DIVIDER) {
            poll_counter = 0;
            // AXP2101 key status can remain asserted across several I2C polls
            // on this board. Treat it as one edge until it has had time to
            // settle, otherwise one press alternates start/cancel repeatedly.
            if (poll_power_button() && (last_power_press == 0 || now - last_power_press >= PWR_PRESS_COOLDOWN_MS)) {
                last_power_press = now;
                // Some board revisions emit a false PMIC PWR event after a
                // BOOT click. Do not undo the requested QR toggle; a genuine
                // later PWR click can still return from QR to orbital.
                if (last_boot_pairing_toggle != 0 && now - last_boot_pairing_toggle < 900) {
                    ESP_LOGI(TAG, "ignoring stale PWR event after BOOT QR toggle");
                } else {
                    ESP_LOGI(TAG, "PWR short press: assistant request");
                    voice_runtime_request_assistant();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

esp_err_t button_input_init(void) {
    const gpio_config_t boot_config = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&boot_config), TAG, "BOOT GPIO init failed");

    const i2c_device_config_t pmic_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    const esp_err_t pmic_result = i2c_master_bus_add_device(bsp_i2c_get_handle(), &pmic_config, &pmic);
    if (pmic_result == ESP_OK) {
        uint8_t enable = 0;
        if (pmic_read(AXP2101_IRQ_ENABLE_1, &enable) == ESP_OK) {
            // The display must never depend on the optional PWR-key path.
            // Some board revisions expose the PMIC but reject IRQ writes until
            // their power domain has settled.  Leave BOOT working in that case.
            if (pmic_write(AXP2101_IRQ_ENABLE_1, enable | AXP2101_PKEY_SHORT_PRESS) == ESP_OK) {
                // Clear a potentially stale key press from before firmware boot.
                pmic_write(AXP2101_IRQ_STATUS_1, AXP2101_PKEY_SHORT_PRESS);
            } else {
                ESP_LOGW(TAG, "PWR button unavailable; BOOT double press remains active");
                pmic = NULL;
            }
        } else {
            ESP_LOGW(TAG, "AXP2101 found but PWR button is unavailable");
            pmic = NULL;
        }
    } else {
        ESP_LOGW(TAG, "AXP2101 unavailable; only BOOT double press is active (%s)", esp_err_to_name(pmic_result));
        pmic = NULL;
    }

    BaseType_t created = xTaskCreate(button_task, "button_input", 4096, NULL, 5, NULL);
    if (created != pdPASS) {
        ESP_LOGW(TAG, "button task unavailable; display and Bluetooth continue normally");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "physical controls ready");
    return ESP_OK;
}
