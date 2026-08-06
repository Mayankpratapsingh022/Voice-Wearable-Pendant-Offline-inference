#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "voice_link.h"

esp_err_t voice_runtime_init(void);
void voice_runtime_request_capture(bool enabled);
void voice_runtime_on_phone_packet(const voice_link_packet_t *packet);
void voice_runtime_on_ble_connection(bool connected);
void voice_runtime_on_mic_opus(const uint8_t *data, size_t size, uint32_t timestamp_ms);
