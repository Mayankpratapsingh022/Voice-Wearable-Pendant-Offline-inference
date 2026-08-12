#pragma once

#include "esp_err.h"

/**
 * Installs the physical-control task.
 *
 * PWR is read through the AXP2101 short-press status bit and BOOT is GPIO0
 * after the ESP32 boot window has completed.
 */
esp_err_t button_input_init(void);
