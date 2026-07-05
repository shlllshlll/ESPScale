#include "mqtt_handler.h"
#include "storage.h"
#include "protocol.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mqtt";

extern EventGroupHandle_t g_sys_events;
extern QueueHandle_t g_cmd_queue;

// Event group bits
#define WIFI_CONNECTED_BIT   BIT0
#define MQTT_CONNECTED_BIT   BIT1

static bool s_connected = false;

esp_err_t mqtt_handler_init(void) {
    ESP_LOGW(TAG, "MQTT support is disabled (lwip MQTT not available)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mqtt_handler_publish_weight(const weight_data_t *data) {
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mqtt_handler_publish_status(const char *status) {
    return ESP_ERR_NOT_SUPPORTED;
}

bool mqtt_handler_is_connected(void) {
    return s_connected;
}
