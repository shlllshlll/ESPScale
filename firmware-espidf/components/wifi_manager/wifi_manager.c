#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "config.h"
#include "storage.h"
#include <string.h>

static const char *TAG = "wifi";

extern EventGroupHandle_t g_sys_events;

// Event group bits
#define WIFI_CONNECTED_BIT   BIT0

static int s_retry_count = 0;
static char s_ip[16] = {0};
static bool s_connected = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected");
            s_connected = false;
            xEventGroupClearBits(g_sys_events, WIFI_CONNECTED_BIT);

            if (s_retry_count < WIFI_MAX_RETRY) {
                s_retry_count++;
                ESP_LOGI(TAG, "Retry %d/%d in %d ms",
                         s_retry_count, WIFI_MAX_RETRY,
                         WIFI_RECONNECT_BASE_MS * s_retry_count);
                vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_BASE_MS * s_retry_count));
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "WiFi connection failed after %d retries", WIFI_MAX_RETRY);
            }
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi associated with AP");
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_ip);
        s_connected = true;
        s_retry_count = 0;
        xEventGroupSetBits(g_sys_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *pass) {
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (pass) {
        strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    }

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Idempotent: already-started WiFi returns ESP_ERR_INVALID_STATE.
    // The new network_task may reconnect after timeout; aborting here
    // reboot-loops the device and kills BLE advertising.
    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_retry_count = 0;
    ret = esp_wifi_connect();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    } else if (ret != ESP_ERR_WIFI_CONN) {
        // ESP_ERR_WIFI_CONN = already connecting/connected — not fatal
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect(void) {
    s_connected = false;
    xEventGroupClearBits(g_sys_events, WIFI_CONNECTED_BIT);
    return esp_wifi_disconnect();
}

bool wifi_manager_is_connected(void) {
    return s_connected;
}

int8_t wifi_manager_get_rssi(void) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -127;
}

const char *wifi_manager_get_ip(void) {
    return s_ip;
}
