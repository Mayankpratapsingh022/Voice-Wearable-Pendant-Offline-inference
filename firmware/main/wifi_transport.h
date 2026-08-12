#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "voice_link.h"

esp_err_t wifi_transport_init(void);
esp_err_t wifi_transport_provision(const uint8_t *ssid, size_t ssid_size, const uint8_t *password, size_t password_size);
bool wifi_transport_is_connected(void);
esp_err_t wifi_transport_send(const voice_link_packet_t *packet);
