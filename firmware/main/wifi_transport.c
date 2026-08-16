#include "wifi_transport.h"

#include <errno.h>
#include <string.h>

#include "ble_transport.h"
#include "app_state.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "voice_runtime.h"

#define WIFI_VOICE_PORT 32323
#define WIFI_STATUS_MAX_TEXT 44

static const char *TAG = "wifi_transport";
static int client_socket = -1;
static SemaphoreHandle_t socket_lock;
static TaskHandle_t server_task_handle;
static bool server_running;
static uint16_t sequence;

#define PENDANT_AP_PASSWORD "pnd2026!"

static void wifi_server_task(void *arg);

static void start_voice_server(void) {
    if (!server_running) {
        server_running = true;
        xTaskCreate(wifi_server_task, "wifi_voice", 6144, NULL, 5, &server_task_handle);
    }
}

static bool recv_all(int socket_fd, uint8_t *buffer, size_t size) {
    size_t received = 0;
    while (received < size) {
        const int result = recv(socket_fd, buffer + received, size - received, 0);
        if (result <= 0) return false;
        received += (size_t)result;
    }
    return true;
}

static void send_ble_status(voice_control_opcode_t opcode, const char *text) {
    uint8_t payload[VOICE_LINK_MAX_PAYLOAD] = {opcode};
    size_t payload_size = 1;
    if (text != NULL) {
        const size_t text_size = strnlen(text, sizeof(payload) - 1);
        memcpy(&payload[1], text, text_size);
        payload_size += text_size;
    }
    const voice_link_packet_t packet = {
        .header = {
            .version = VOICE_LINK_VERSION,
            .kind = VOICE_LINK_CONTROL,
            .sequence = sequence++,
            .payload_size = payload_size,
        },
        .payload = payload,
    };
    ble_transport_send(&packet);
}

static void close_client(void) {
    xSemaphoreTake(socket_lock, portMAX_DELAY);
    if (client_socket >= 0) {
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
        client_socket = -1;
    }
    xSemaphoreGive(socket_lock);
    voice_runtime_on_wifi_connection(false);
}

static void wifi_server_task(void *arg) {
    (void)arg;
    const int listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_socket < 0) {
        ESP_LOGE(TAG, "cannot create voice socket: errno=%d", errno);
        server_running = false;
        vTaskDelete(NULL);
        return;
    }
    const int enabled = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(WIFI_VOICE_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listen_socket, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listen_socket, 1) != 0) {
        ESP_LOGE(TAG, "cannot listen for phone: errno=%d", errno);
        close(listen_socket);
        server_running = false;
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        const int accepted = accept(listen_socket, NULL, NULL);
        if (accepted < 0) continue;
        close_client();
        xSemaphoreTake(socket_lock, portMAX_DELAY);
        client_socket = accepted;
        xSemaphoreGive(socket_lock);
        ESP_LOGI(TAG, "phone connected over Wi-Fi");
        voice_runtime_on_wifi_connection(true);

        uint8_t encoded[VOICE_LINK_HEADER_SIZE + VOICE_LINK_MAX_PAYLOAD];
        while (recv_all(accepted, encoded, VOICE_LINK_HEADER_SIZE)) {
            const size_t payload_size = (size_t)encoded[8] | ((size_t)encoded[9] << 8);
            if (payload_size > VOICE_LINK_MAX_PAYLOAD || !recv_all(accepted, encoded + VOICE_LINK_HEADER_SIZE, payload_size)) break;
            voice_link_packet_t packet;
            if (voice_link_decode(encoded, VOICE_LINK_HEADER_SIZE + payload_size, &packet)) {
                voice_runtime_on_phone_packet(&packet);
            }
        }
        close_client();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        close_client();
        esp_wifi_connect();
    }
    if (base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) return;
    const ip_event_got_ip_t *event = data;
    char ip[WIFI_STATUS_MAX_TEXT];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
    ESP_LOGI(TAG, "joined hotspot at %s", ip);
    send_ble_status(VOICE_CONTROL_WIFI_READY, ip);
    start_voice_server();
}

esp_err_t wifi_transport_init(void) {
    socket_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(socket_lock != NULL, ESP_ERR_NO_MEM, TAG, "socket lock allocation failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    const esp_err_t loop_result = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(loop_result == ESP_OK || loop_result == ESP_ERR_INVALID_STATE, loop_result, TAG, "event loop init failed");
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL), TAG, "wifi event registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL), TAG, "ip event registration failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), TAG, "wifi storage init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "wifi mode init failed");
    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "Wi-Fi MAC unavailable");
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "PENDANT-%02X%02X%02X", mac[3], mac[4], mac[5]);
    strlcpy((char *)ap.ap.password, PENDANT_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "Wi-Fi AP config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");
    char qr[48];
    snprintf(qr, sizeof(qr), "PENDANT:W:%02X%02X%02X:%s", mac[3], mac[4], mac[5], PENDANT_AP_PASSWORD);
    app_state_set_pairing_uri(qr);
    start_voice_server();
    ESP_LOGI(TAG, "Wi-Fi pairing AP ready: %s", ap.ap.ssid);
    return ESP_OK;
}

esp_err_t wifi_transport_provision(const uint8_t *ssid, size_t ssid_size, const uint8_t *password, size_t password_size) {
    if (ssid == NULL || password == NULL || ssid_size == 0 || ssid_size > 32 || password_size > 63) return ESP_ERR_INVALID_ARG;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, ssid_size);
    memcpy(config.sta.password, password, password_size);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.failure_retry_cnt = 8;
    const esp_err_t disconnect_result = esp_wifi_disconnect();
    if (disconnect_result != ESP_OK && disconnect_result != ESP_ERR_WIFI_NOT_CONNECT) {
        return disconnect_result;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "wifi config failed");
    send_ble_status(VOICE_CONTROL_WIFI_READY, "Joining phone hotspot…");
    return esp_wifi_connect();
}

bool wifi_transport_is_connected(void) {
    bool connected;
    xSemaphoreTake(socket_lock, portMAX_DELAY);
    connected = client_socket >= 0;
    xSemaphoreGive(socket_lock);
    return connected;
}

esp_err_t wifi_transport_send(const voice_link_packet_t *packet) {
    if (packet == NULL || !wifi_transport_is_connected()) return ESP_ERR_INVALID_STATE;
    uint8_t encoded[VOICE_LINK_HEADER_SIZE + VOICE_LINK_MAX_PAYLOAD];
    size_t encoded_size = 0;
    ESP_RETURN_ON_FALSE(voice_link_encode(encoded, sizeof(encoded), packet, &encoded_size), ESP_ERR_INVALID_ARG, TAG, "packet encode failed");
    xSemaphoreTake(socket_lock, portMAX_DELAY);
    const int socket_fd = client_socket;
    size_t sent_size = 0;
    while (socket_fd >= 0 && sent_size < encoded_size) {
        const int sent = send(socket_fd, encoded + sent_size, encoded_size - sent_size, 0);
        if (sent <= 0) break;
        sent_size += (size_t)sent;
    }
    xSemaphoreGive(socket_lock);
    return sent_size == encoded_size ? ESP_OK : ESP_FAIL;
}
