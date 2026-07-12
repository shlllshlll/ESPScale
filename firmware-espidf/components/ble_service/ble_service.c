#include "ble_service.h"
#include "config.h"
#include "storage.h"
#include "protocol.h"
#include "wifi_manager.h"
#include "scale_sensor.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "ble";

extern QueueHandle_t g_cmd_queue;

static bool s_connected = false;
static uint16_t s_conn_handle = 0;
static uint16_t s_weight_val_handle = 0;
static uint16_t s_event_val_handle = 0;

// UUID definitions (128-bit) - using different variable names to avoid macro conflicts
static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0x74, 0x2A, 0x96, 0x0F, 0xE2, 0x3B, 0x55, 0x95,
                     0xD8, 0x4E, 0x6C, 0xDD, 0x34, 0x08, 0x86, 0xF3);

static const ble_uuid128_t char_device_info_uuid =
    BLE_UUID128_INIT(0x74, 0x2A, 0x96, 0x0F, 0xE2, 0x3B, 0x55, 0x95,
                     0xD8, 0x4E, 0x6C, 0xDD, 0x39, 0xF2, 0x29, 0x2A);

static const ble_uuid128_t char_wifi_creds_uuid =
    BLE_UUID128_INIT(0x74, 0x2A, 0x96, 0x0F, 0xE2, 0x3B, 0x55, 0x95,
                     0xD8, 0x4E, 0x6C, 0xDD, 0xD4, 0xA3, 0xB2, 0xC1);

static const ble_uuid128_t char_network_status_uuid =
    BLE_UUID128_INIT(0x74, 0x2A, 0x96, 0x0F, 0xE2, 0x3B, 0x55, 0x95,
                     0xD8, 0x4E, 0x6C, 0xDD, 0xA8, 0xF7, 0xE6, 0xD5);

static const ble_uuid128_t char_scale_settings_uuid =
    BLE_UUID128_INIT(0x74, 0x2A, 0x96, 0x0F, 0xE2, 0x3B, 0x55, 0x95,
                     0xD8, 0x4E, 0x6C, 0xDD, 0xE6, 0xD7, 0xC8, 0xB9);

static const ble_uuid128_t char_weight_stream_uuid =
    BLE_UUID128_INIT(0x74, 0x2A, 0x96, 0x0F, 0xE2, 0x3B, 0x55, 0x95,
                     0xD8, 0x4E, 0x6C, 0xDD, 0xC2, 0xD3, 0xE4, 0xF5);

static const ble_uuid128_t char_command_uuid =
    BLE_UUID128_INIT(0x74, 0x2A, 0x96, 0x0F, 0xE2, 0x3B, 0x55, 0x95,
                     0xD8, 0x4E, 0x6C, 0xDD, 0xD4, 0xC3, 0xB2, 0xA1);

static const ble_uuid128_t char_event_uuid =
    BLE_UUID128_INIT(0x74, 0x2A, 0x96, 0x0F, 0xE2, 0x3B, 0x55, 0x95,
                     0xD8, 0x4E, 0x6C, 0xDD, 0xB5, 0xA6, 0xF7, 0xE8);

// Forward declarations
static void ble_advertise(void);
static int device_info_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int wifi_creds_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);
static int network_status_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int scale_settings_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int command_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);
static int weight_stream_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int event_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);

// Simple JSON helper
static bool json_get_string(const char *json, const char *key, char *value, size_t value_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);

    const char *start = strstr(json, search);
    if (!start) return false;

    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end) return false;

    size_t len = end - start;
    if (len >= value_len) len = value_len - 1;
    strncpy(value, start, len);
    value[len] = '\0';
    return true;
}

static bool json_get_number(const char *json, const char *key, double *value) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *start = strstr(json, search);
    if (!start) return false;

    start += strlen(search);
    while (*start == ' ') start++;

    *value = atof(start);
    return true;
}

// GATT service definition
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &char_device_info_uuid.u,
                .access_cb = device_info_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &char_wifi_creds_uuid.u,
                .access_cb = wifi_creds_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &char_network_status_uuid.u,
                .access_cb = network_status_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &char_scale_settings_uuid.u,
                .access_cb = scale_settings_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &char_weight_stream_uuid.u,
                .access_cb = weight_stream_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_weight_val_handle,
            },
            {
                .uuid = &char_command_uuid.u,
                .access_cb = command_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &char_event_uuid.u,
                .access_cb = event_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_event_val_handle,
            },
            {0} // Terminator
        },
    },
    {0} // Terminator
};

// GAP event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE connect: status=%d handle=%d",
                 event->connect.status, event->connect.conn_handle);
        if (event->connect.status == 0) {
            s_connected = true;
            s_conn_handle = event->connect.conn_handle;
        } else {
            ble_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnect: reason=%d", event->disconnect.reason);
        s_connected = false;
        s_conn_handle = 0;
        ble_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "BLE advertise complete");
        ble_advertise();
        break;
    }
    return 0;
}

static void ble_advertise(void) {
    int rc;
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 0x20,
        .itvl_max = 0x30,
    };

    // Build device name
    stored_config_t cfg;
    storage_load_config(&cfg);
    char adv_name[40];
    snprintf(adv_name, sizeof(adv_name), "%s%s", BLE_ADV_NAME_PREFIX,
             strlen(cfg.device_id) > 4 ? cfg.device_id + strlen(cfg.device_id) - 4 : cfg.device_id);

    // Set GAP device name (will be returned on name read requests)
    rc = ble_svc_gap_device_name_set(adv_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set device name: %d", rc);
    }

    // Advertising data: flags + 128-bit service UUID (fits in 31-byte ADV payload)
    struct ble_hs_adv_fields adv_fields = {};
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.uuids128 = &svc_uuid;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
    }

    // Scan response data: device name
    struct ble_hs_adv_fields rsp_fields = {};
    rsp_fields.name = (uint8_t *)adv_name;
    rsp_fields.name_len = strlen(adv_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan rsp fields: %d", rc);
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising as: %s (with service UUID)", adv_name);
    }
}

// GATT callbacks
static int device_info_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        stored_config_t cfg;
        storage_load_config(&cfg);

        char *json = protocol_build_device_info(
            cfg.device_id, cfg.device_name, FIRMWARE_VERSION,
            cfg.mode, "running");

        if (json) {
            os_mbuf_append(ctxt->om, json, strlen(json));
            free(json);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int wifi_creds_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        // Provision payload includes mqtt_user/pass + server_url — 256 is too small.
        char buf[512];
        if (om_len >= sizeof(buf)) om_len = sizeof(buf) - 1;
        os_mbuf_copydata(ctxt->om, 0, om_len, buf);
        buf[om_len] = '\0';

        ESP_LOGI(TAG, "WiFi creds received: %s", buf);

        char ssid[33] = {0};
        char pass[65] = {0};

        if (json_get_string(buf, "ssid", ssid, sizeof(ssid))) {
            json_get_string(buf, "password", pass, sizeof(pass));
            storage_save_wifi(ssid, pass);
            wifi_manager_connect(ssid, pass);
        }

        double mode;
        if (json_get_number(buf, "mode", &mode)) {
            storage_save_mode((uint8_t)mode);
        }

        char server_url[128] = {0};
        if (json_get_string(buf, "server_url", server_url, sizeof(server_url))) {
            storage_save_server_url(server_url);
        }

        // Extended MQTT config (from APP provision payload)
        char mqtt_host[64] = {0};
        if (json_get_string(buf, "mqtt_host", mqtt_host, sizeof(mqtt_host))) {
            char mqtt_user[32] = {0};
            char mqtt_pass[64] = {0};
            double mqtt_port;
            json_get_string(buf, "mqtt_user", mqtt_user, sizeof(mqtt_user));
            json_get_string(buf, "mqtt_pass", mqtt_pass, sizeof(mqtt_pass));
            mqtt_config_t mqtt_cfg = {0};
            strncpy(mqtt_cfg.host, mqtt_host, sizeof(mqtt_cfg.host) - 1);
            mqtt_cfg.port = json_get_number(buf, "mqtt_port", &mqtt_port)
                                ? (uint16_t)mqtt_port
                                : DEFAULT_MQTT_PORT;
            strncpy(mqtt_cfg.user, mqtt_user, sizeof(mqtt_cfg.user) - 1);
            strncpy(mqtt_cfg.pass, mqtt_pass, sizeof(mqtt_cfg.pass) - 1);
            storage_save_mqtt_config(&mqtt_cfg);
            ESP_LOGI(TAG, "MQTT config set: %s:%d", mqtt_host, mqtt_cfg.port);
        }

        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int network_status_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char *json = protocol_build_network_status(
            wifi_manager_is_connected(),
            wifi_manager_get_ip(),
            wifi_manager_get_rssi(),
            false);

        if (json) {
            os_mbuf_append(ctxt->om, json, strlen(json));
            free(json);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int scale_settings_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        stored_config_t cfg;
        storage_load_config(&cfg);

        char json[128];
        snprintf(json, sizeof(json),
                 "{\"cal_factor\":%.2f,\"unit\":\"%s\",\"upload_interval_ms\":%lu}",
                 cfg.cal_factor, cfg.unit, cfg.upload_interval_ms);
        os_mbuf_append(ctxt->om, json, strlen(json));
        return 0;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        char buf[256];
        os_mbuf_copydata(ctxt->om, 0, om_len, buf);
        buf[om_len] = '\0';

        double cal_factor;
        if (json_get_number(buf, "cal_factor", &cal_factor)) {
            storage_save_cal_factor((float)cal_factor);
        }

        char unit[4] = {0};
        if (json_get_string(buf, "unit", unit, sizeof(unit))) {
            storage_save_unit(unit);
        }

        double interval;
        if (json_get_number(buf, "upload_interval_ms", &interval)) {
            storage_save_upload_interval((uint32_t)interval);
        }

        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int command_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        char buf[512];
        os_mbuf_copydata(ctxt->om, 0, om_len, buf);
        buf[om_len] = '\0';

        ESP_LOGI(TAG, "Command received: %s", buf);

        cmd_request_t cmd = protocol_parse(buf);
        if (cmd.type != CMD_NONE) {
            xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(100));
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int weight_stream_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Return current weight
        weight_data_t data = {
            .weight = scale_sensor_read_weight(5),
            .stable = true,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        strncpy(data.unit, "g", sizeof(data.unit) - 1);

        char *json = protocol_build_weight(&data);
        if (json) {
            os_mbuf_append(ctxt->om, json, strlen(json));
            free(json);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int event_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// GATT register callback - informational only. Services must already be
// added via ble_gatts_count_cfg()/ble_gatts_add_svcs() *before* the NimBLE
// host starts, since ble_gatts_start() iterates the service-def array while
// calling this callback; appending to that array from inside the callback
// corrupts the heap.
static void ble_gatts_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "Registered GATT service");
        break;
    default:
        break;
    }
}

// NimBLE host task
static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// BLE sync callback
static void ble_on_sync(void) {
    ESP_LOGI(TAG, "BLE host synced");
    ble_advertise();
}

// BLE reset callback
static void ble_on_reset(int reason) {
    ESP_LOGW(TAG, "BLE reset: reason=%d", reason);
}

esp_err_t ble_service_init(void) {
    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.gatts_register_cb = ble_gatts_register_cb;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register GATT services before the host task starts. ble_hs_start()
    // (invoked from the host task via nimble_port_run) both registers
    // queued services and starts advertising-readiness, so the service
    // definitions must be queued up first.
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    // Start BLE host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE service initialized");
    return ESP_OK;
}

esp_err_t ble_notify_weight(const weight_data_t *data) {
    if (!s_connected || s_weight_val_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    char *json = protocol_build_weight(data);
    if (!json) return ESP_ERR_NO_MEM;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(json, strlen(json));
    free(json);

    if (!om) return ESP_ERR_NO_MEM;

    int rc = ble_gattc_notify_custom(s_conn_handle, s_weight_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE notify failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ble_notify_network_status(void) {
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    char *json = protocol_build_network_status(
        wifi_manager_is_connected(),
        wifi_manager_get_ip(),
        wifi_manager_get_rssi(),
        false);

    if (!json) return ESP_ERR_NO_MEM;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(json, strlen(json));
    free(json);

    if (!om) return ESP_ERR_NO_MEM;

    // Notify on network status characteristic
    int rc = ble_gattc_notify_custom(s_conn_handle,
                                      ble_gatts_find_chr(&svc_uuid.u,
                                                          &char_network_status_uuid.u,
                                                          NULL, NULL),
                                      om);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE network status notify failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool ble_is_connected(void) {
    return s_connected;
}

uint16_t ble_get_conn_handle(void) {
    return s_conn_handle;
}
