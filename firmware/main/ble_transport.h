#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "voice_link.h"

esp_err_t ble_transport_init(void);
bool ble_transport_is_connected(void);
esp_err_t ble_transport_send(const voice_link_packet_t *packet);
