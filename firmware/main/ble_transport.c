#include "ble_transport.h"

#include <string.h>

#include "app_state.h"
#include "esp_check.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "voice_runtime.h"

static const char *TAG = "ble_transport";

/* UUID byte order is the NimBLE little-endian representation. */
static ble_uuid128_t service_uuid = BLE_UUID128_INIT(0x00, 0x00, 0x9a, 0x2d, 0x1d, 0x6f, 0x90, 0xa0,
                                                      0x6d, 0x4b, 0x6f, 0x59, 0x01, 0x00, 0x2d, 0x9d);
static ble_uuid128_t control_uuid = BLE_UUID128_INIT(0x01, 0x00, 0x9a, 0x2d, 0x1d, 0x6f, 0x90, 0xa0,
                                                      0x6d, 0x4B, 0x6f, 0x59, 0x01, 0x00, 0x2d, 0x9d);
static ble_uuid128_t microphone_uuid = BLE_UUID128_INIT(0x02, 0x00, 0x9a, 0x2d, 0x1d, 0x6f, 0x90, 0xa0,
                                                         0x6d, 0x4B, 0x6f, 0x59, 0x01, 0x00, 0x2d, 0x9d);
static ble_uuid128_t speaker_uuid = BLE_UUID128_INIT(0x03, 0x00, 0x9a, 0x2d, 0x1d, 0x6f, 0x90, 0xa0,
                                                      0x6d, 0x4B, 0x6f, 0x59, 0x01, 0x00, 0x2d, 0x9d);
static ble_uuid128_t telemetry_uuid = BLE_UUID128_INIT(0x04, 0x00, 0x9a, 0x2d, 0x1d, 0x6f, 0x90, 0xa0,
                                                        0x6d, 0x4B, 0x6f, 0x59, 0x01, 0x00, 0x2d, 0x9d);

static uint16_t control_handle;
static uint16_t microphone_handle;
static uint16_t speaker_handle;
static uint16_t telemetry_handle;
static uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t own_address_type;
static bool control_subscribed;
static bool microphone_subscribed;
static bool telemetry_subscribed;

static int gap_event(struct ble_gap_event *event, void *arg);

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        (attr_handle != control_handle && attr_handle != speaker_handle)) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t raw[VOICE_LINK_HEADER_SIZE + VOICE_LINK_MAX_PAYLOAD];
    const int length = OS_MBUF_PKTLEN(ctxt->om);
    if (length <= 0 || length > (int)sizeof(raw) || ble_hs_mbuf_to_flat(ctxt->om, raw, sizeof(raw), NULL) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    voice_link_packet_t packet;
    if (!voice_link_decode(raw, (size_t)length, &packet)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if ((attr_handle == control_handle && packet.header.kind != VOICE_LINK_CONTROL) ||
        (attr_handle == speaker_handle && packet.header.kind != VOICE_LINK_SPEAKER_OPUS)) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    voice_runtime_on_phone_packet(&packet);
    return 0;
}

static struct ble_gatt_chr_def voice_characteristics[] = {
    {
        .uuid = &control_uuid.u,
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &control_handle,
    },
    {
        .uuid = &microphone_uuid.u,
        // NimBLE requires an access callback even for a notify-only value.
        // This callback rejects any client read/write attempt.
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &microphone_handle,
    },
    {
        .uuid = &speaker_uuid.u,
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
        .val_handle = &speaker_handle,
    },
    {
        .uuid = &telemetry_uuid.u,
        // NimBLE requires an access callback even for a notify-only value.
        // This callback rejects any client read/write attempt.
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &telemetry_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def voice_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = voice_characteristics,
    },
    {0},
};

static void start_advertising(void) {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.uuids128 = &service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER, &parameters, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising start failed: %d", rc);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                connection_handle = event->connect.conn_handle;
                app_state_set_face(APP_FACE_IDLE, "Hold face to talk");
                voice_runtime_on_ble_connection(true);
                ESP_LOGI(TAG, "phone connected");
            } else {
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            connection_handle = BLE_HS_CONN_HANDLE_NONE;
            control_subscribed = false;
            microphone_subscribed = false;
            telemetry_subscribed = false;
            voice_runtime_on_ble_connection(false);
            ESP_LOGI(TAG, "phone disconnected; reason=%d", event->disconnect.reason);
            start_advertising();
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == control_handle) {
                control_subscribed = event->subscribe.cur_notify;
            } else if (event->subscribe.attr_handle == microphone_handle) {
                microphone_subscribed = event->subscribe.cur_notify;
            } else if (event->subscribe.attr_handle == telemetry_handle) {
                telemetry_subscribed = event->subscribe.cur_notify;
            }
            return 0;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU negotiated: %d", event->mtu.value);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_advertising();
            return 0;
        default:
            return 0;
    }
}

static void on_sync(void) {
    const int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "address inference failed: %d", rc);
        return;
    }
    start_advertising();
}

static void nimble_host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t pendant_ble_init(void) {
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "NimBLE init failed");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Pendant");
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;

    int rc = ble_gatts_count_cfg(voice_services);
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, TAG, "GATT count failed: %d", rc);
    rc = ble_gatts_add_svcs(voice_services);
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, TAG, "GATT add failed: %d", rc);
    nimble_port_freertos_init(nimble_host_task);
    app_state_set_face(APP_FACE_DISCONNECTED, "Phone not connected");
    return ESP_OK;
}

bool ble_transport_is_connected(void) {
    return connection_handle != BLE_HS_CONN_HANDLE_NONE;
}

esp_err_t ble_transport_send(const voice_link_packet_t *packet) {
    if (packet == NULL || !ble_transport_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((packet->header.kind == VOICE_LINK_MIC_OPUS && !microphone_subscribed) ||
        (packet->header.kind == VOICE_LINK_CONTROL && !control_subscribed) ||
        (packet->header.kind == VOICE_LINK_TELEMETRY && !telemetry_subscribed)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t characteristic_handle = 0;
    switch (packet->header.kind) {
        case VOICE_LINK_CONTROL:
            characteristic_handle = control_handle;
            break;
        case VOICE_LINK_MIC_OPUS:
            characteristic_handle = microphone_handle;
            break;
        case VOICE_LINK_TELEMETRY:
            characteristic_handle = telemetry_handle;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    uint8_t encoded[VOICE_LINK_HEADER_SIZE + VOICE_LINK_MAX_PAYLOAD];
    size_t encoded_size = 0;
    ESP_RETURN_ON_FALSE(voice_link_encode(encoded, sizeof(encoded), packet, &encoded_size), ESP_ERR_INVALID_ARG, TAG,
                        "packet encode failed");
    struct os_mbuf *buffer = ble_hs_mbuf_from_flat(encoded, encoded_size);
    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_NO_MEM, TAG, "notification buffer allocation failed");
    const int rc = ble_gatts_notify_custom(connection_handle, characteristic_handle, buffer);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
