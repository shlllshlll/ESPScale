#include "mqtt_handler.h"
#include "storage.h"
#include "protocol.h"
#include "config.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "mqtt";

extern EventGroupHandle_t g_sys_events;
extern QueueHandle_t g_cmd_queue;

#define MQTT_CONNECTED_BIT   BIT1

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

static esp_err_t handle_mqtt_command(const char *topic, const char *data, int data_len) {
    char buf[512];
    if (data_len >= (int)sizeof(buf)) data_len = sizeof(buf) - 1;
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';

    cmd_request_t cmd = protocol_parse(buf);
    if (cmd.type == CMD_NONE) {
        ESP_LOGW(TAG, "Unknown MQTT command: %s", buf);
        return ESP_ERR_INVALID_ARG;
    }

    char *ack = NULL;

    switch (cmd.type) {
    case CMD_TARE: {
        // Send to command queue for scale_task to process
        xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(100));
        ack = protocol_build_ack("tare", true, cmd.request_id, NULL);
        break;
    }
    case CMD_SET_MODE: {
        // Parse mode from raw_json
        double mode;
        if (protocol_json_get_number(cmd.raw_json, "mode", &mode)) {
            storage_save_mode((uint8_t)mode);
            ESP_LOGI(TAG, "Mode set to %d", (uint8_t)mode);
        }
        ack = protocol_build_ack("set_mode", true, cmd.request_id, NULL);
        break;
    }
    case CMD_SET_CONFIG: {
        double mode, interval, mqtt_port;
        char server_url[128] = {0};
        char mqtt_host[64] = {0};
        char mqtt_user[32] = {0};
        char mqtt_pass[64] = {0};
        char unit[4] = {0};
        char api_key[64] = {0};

        bool changed = false;

        if (protocol_json_get_number(cmd.raw_json, "mode", &mode)) {
            storage_save_mode((uint8_t)mode);
            changed = true;
        }
        if (protocol_json_get_string(cmd.raw_json, "server_url", server_url, sizeof(server_url))) {
            storage_save_server_url(server_url);
            changed = true;
        }
        if (protocol_json_get_string(cmd.raw_json, "mqtt_host", mqtt_host, sizeof(mqtt_host))) {
            mqtt_config_t mqtt_cfg = {0};
            strncpy(mqtt_cfg.host, mqtt_host, sizeof(mqtt_cfg.host) - 1);
            if (protocol_json_get_number(cmd.raw_json, "mqtt_port", &mqtt_port)) {
                mqtt_cfg.port = (uint16_t)mqtt_port;
            } else {
                mqtt_cfg.port = DEFAULT_MQTT_PORT;
            }
            protocol_json_get_string(cmd.raw_json, "mqtt_user", mqtt_user, sizeof(mqtt_user));
            protocol_json_get_string(cmd.raw_json, "mqtt_pass", mqtt_pass, sizeof(mqtt_pass));
            strncpy(mqtt_cfg.user, mqtt_user, sizeof(mqtt_cfg.user) - 1);
            strncpy(mqtt_cfg.pass, mqtt_pass, sizeof(mqtt_cfg.pass) - 1);
            storage_save_mqtt_config(&mqtt_cfg);
            changed = true;
        }
        if (protocol_json_get_string(cmd.raw_json, "unit", unit, sizeof(unit))) {
            storage_save_unit(unit);
            changed = true;
        }
        if (protocol_json_get_number(cmd.raw_json, "upload_interval_ms", &interval)) {
            storage_save_upload_interval((uint32_t)interval);
            changed = true;
        }
        if (protocol_json_get_string(cmd.raw_json, "api_key", api_key, sizeof(api_key))) {
            storage_save_api_key(api_key);
            changed = true;
        }
        ESP_LOGI(TAG, "Config updated via MQTT%s", changed ? "" : " (no changes)");
        ack = protocol_build_ack("set_config", true, cmd.request_id, NULL);
        break;
    }
    case CMD_GET_STATUS: {
        stored_config_t cfg;
        storage_load_config(&cfg);
        char data_str[256];
        snprintf(data_str, sizeof(data_str),
                 "{\"device_id\":\"%s\",\"mode\":%d,\"server_url\":\"%s\",\"unit\":\"%s\","
                 "\"upload_interval_ms\":%lu,\"cal_factor\":%.2f}",
                 cfg.device_id, cfg.mode, cfg.server_url, cfg.unit,
                 cfg.upload_interval_ms, cfg.cal_factor);
        ack = protocol_build_ack("get_status", true, cmd.request_id, data_str);
        break;
    }
    case CMD_REBOOT: {
        ack = protocol_build_ack("reboot", true, cmd.request_id, NULL);
        // Publish ack before reboot
        if (s_client && s_connected) {
            char event_topic[64];
            stored_config_t cfg;
            storage_load_config(&cfg);
            snprintf(event_topic, sizeof(event_topic), "espscale/%s/event", cfg.device_id);
            esp_mqtt_client_publish(s_client, event_topic, ack, 0, 1, 0);
        }
        free(ack);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
        return ESP_OK;
    }
    case CMD_FACTORY_RESET: {
        ack = protocol_build_ack("factory_reset", true, cmd.request_id, NULL);
        if (s_client && s_connected) {
            char event_topic[64];
            stored_config_t cfg;
            storage_load_config(&cfg);
            snprintf(event_topic, sizeof(event_topic), "espscale/%s/event", cfg.device_id);
            esp_mqtt_client_publish(s_client, event_topic, ack, 0, 1, 0);
        }
        free(ack);
        vTaskDelay(pdMS_TO_TICKS(300));
        storage_factory_reset();
        esp_restart();
        return ESP_OK;
    }
    default: {
        ack = protocol_build_ack(protocol_cmd_name(cmd.type), false, cmd.request_id, NULL);
        break;
    }
    }

    // Publish ack to event topic
    if (ack && s_client && s_connected) {
        char event_topic[64];
        stored_config_t cfg;
        storage_load_config(&cfg);
        snprintf(event_topic, sizeof(event_topic), "espscale/%s/event", cfg.device_id);
        esp_mqtt_client_publish(s_client, event_topic, ack, 0, 1, 0);
    }
    free(ack);
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "MQTT connected");
        s_connected = true;
        xEventGroupSetBits(g_sys_events, MQTT_CONNECTED_BIT);

        stored_config_t cfg;
        storage_load_config(&cfg);

        // Subscribe to command topic
        char cmd_topic[64];
        snprintf(cmd_topic, sizeof(cmd_topic), "espscale/%s/cmd", cfg.device_id);
        esp_mqtt_client_subscribe(s_client, cmd_topic, 1);
        ESP_LOGI(TAG, "Subscribed to %s", cmd_topic);

        // Publish online status
        char status_topic[64];
        char status_payload[128];
        snprintf(status_topic, sizeof(status_topic), "espscale/%s/status", cfg.device_id);
        snprintf(status_payload, sizeof(status_payload),
                 "{\"device_id\":\"%s\",\"status\":\"online\"}", cfg.device_id);
        esp_mqtt_client_publish(s_client, status_topic, status_payload, 0, 1, 1);
        break;
    }
    case MQTT_EVENT_DISCONNECTED: {
        ESP_LOGW(TAG, "MQTT disconnected");
        s_connected = false;
        xEventGroupClearBits(g_sys_events, MQTT_CONNECTED_BIT);
        break;
    }
    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "MQTT rx topic=%.*s data=%.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        handle_mqtt_command(event->topic, event->data, event->data_len);
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

esp_err_t mqtt_handler_init(void) {
    if (s_client) {
        return ESP_OK;
    }

    stored_config_t cfg;
    storage_load_config(&cfg);

    char uri[128] = {0};
    const char *host = strlen(cfg.mqtt_host) > 0 ? cfg.mqtt_host : "localhost";
    uint16_t port = cfg.mqtt_port > 0 ? cfg.mqtt_port : DEFAULT_MQTT_PORT;
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", host, port);

    // LWT config
    char will_topic[64];
    char will_payload[128];
    snprintf(will_topic, sizeof(will_topic), "espscale/%s/status", cfg.device_id);
    snprintf(will_payload, sizeof(will_payload),
             "{\"device_id\":\"%s\",\"status\":\"offline\"}", cfg.device_id);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = cfg.device_id,
        .credentials.username = (strlen(cfg.mqtt_user) > 0) ? cfg.mqtt_user : NULL,
        .credentials.authentication.password = (strlen(cfg.mqtt_pass) > 0) ? cfg.mqtt_pass : NULL,
        .session.last_will.topic = will_topic,
        .session.last_will.msg = will_payload,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
        .buffer.size = 512,
        .buffer.out_size = 512,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "MQTT handler initialized for %s", uri);
    return ESP_OK;
}

esp_err_t mqtt_handler_start(void) {
    if (!s_client) {
        esp_err_t ret = mqtt_handler_init();
        if (ret != ESP_OK) return ret;
    }
    return esp_mqtt_client_start(s_client);
}

esp_err_t mqtt_handler_stop(void) {
    if (!s_client) return ESP_ERR_INVALID_STATE;
    esp_mqtt_client_stop(s_client);
    s_connected = false;
    return ESP_OK;
}

esp_err_t mqtt_handler_publish_weight(const weight_data_t *data) {
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;

    stored_config_t cfg;
    storage_load_config(&cfg);

    char topic[64];
    snprintf(topic, sizeof(topic), "espscale/%s/weight", cfg.device_id);

    char *json = protocol_build_weight(data);
    if (!json) return ESP_ERR_NO_MEM;

    esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);
    ESP_LOGI(TAG, "Published weight: %s", json);
    free(json);
    return ESP_OK;
}

esp_err_t mqtt_handler_publish_status(const char *status) {
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;

    stored_config_t cfg;
    storage_load_config(&cfg);

    char topic[64];
    char payload[128];
    snprintf(topic, sizeof(topic), "espscale/%s/status", cfg.device_id);
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"status\":\"%s\"}", cfg.device_id, status);

    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);
    return ESP_OK;
}

esp_err_t mqtt_handler_publish_event(const char *payload) {
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;

    stored_config_t cfg;
    storage_load_config(&cfg);

    char topic[64];
    snprintf(topic, sizeof(topic), "espscale/%s/event", cfg.device_id);

    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
    return ESP_OK;
}

bool mqtt_handler_is_connected(void) {
    return s_connected;
}

void mqtt_handler_reset(void) {
    s_connected = false;
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}