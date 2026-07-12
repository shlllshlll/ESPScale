#include "protocol.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "protocol";

static const char *CMD_NAMES[] = {
    "none",
    "tare",
    "calibrate",
    "set_mode",
    "set_config",
    "set_wifi",
    "set_mqtt",
    "get_status",
    "reboot",
    "factory_reset",
};

// Simple JSON string extraction helper
bool protocol_json_get_string(const char *json, const char *key, char *value, size_t value_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);

    const char *start = strstr(json, search);
    if (!start) {
        // Try without quotes for numbers
        snprintf(search, sizeof(search), "\"%s\":", key);
        start = strstr(json, search);
        if (!start) return false;
        start += strlen(search);
        // Skip whitespace
        while (*start == ' ') start++;
        // Copy until comma or end
        const char *end = strchr(start, ',');
        if (!end) end = strchr(start, '}');
        if (!end) return false;
        size_t len = end - start;
        if (len >= value_len) len = value_len - 1;
        strncpy(value, start, len);
        value[len] = '\0';
        return true;
    }

    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end) return false;

    size_t len = end - start;
    if (len >= value_len) len = value_len - 1;
    strncpy(value, start, len);
    value[len] = '\0';
    return true;
}

// Simple JSON number extraction helper
bool protocol_json_get_number(const char *json, const char *key, double *value) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *start = strstr(json, search);
    if (!start) return false;

    start += strlen(search);
    while (*start == ' ') start++;

    *value = atof(start);
    return true;
}

cmd_request_t protocol_parse(const char *json) {
    cmd_request_t req;
    memset(&req, 0, sizeof(req));

    if (!json) return req;

    strncpy(req.raw_json, json, sizeof(req.raw_json) - 1);

    char cmd_str[32] = {0};
    if (protocol_json_get_string(json, "cmd", cmd_str, sizeof(cmd_str))) {
        for (int i = 1; i < sizeof(CMD_NAMES) / sizeof(CMD_NAMES[0]); i++) {
            if (strcmp(cmd_str, CMD_NAMES[i]) == 0) {
                req.type = (cmd_type_t)i;
                break;
            }
        }
        if (req.type == CMD_NONE) {
            ESP_LOGW(TAG, "Unknown command: %s", cmd_str);
        }
    }

    char request_id[37] = {0};
    if (protocol_json_get_string(json, "request_id", request_id, sizeof(request_id))) {
        strncpy(req.request_id, request_id, sizeof(req.request_id) - 1);
    }

    return req;
}

char *protocol_build_ack(const char *cmd_name, bool success, const char *request_id, const char *data) {
    char *json = malloc(256);
    if (!json) return NULL;

    int len = snprintf(json, 256,
                       "{\"event\":\"cmd_ack\",\"cmd\":\"%s\",\"success\":%s",
                       cmd_name, success ? "true" : "false");

    if (request_id && strlen(request_id) > 0) {
        len += snprintf(json + len, 256 - len, ",\"request_id\":\"%s\"", request_id);
    }

    if (data && strlen(data) > 0) {
        len += snprintf(json + len, 256 - len, ",\"data\":%s", data);
    }

    snprintf(json + len, 256 - len, "}");
    return json;
}

char *protocol_build_weight(const weight_data_t *data) {
    if (!data) return NULL;

    char *json = malloc(128);
    if (!json) return NULL;

    snprintf(json, 128,
             "{\"weight\":%.1f,\"unit\":\"%s\",\"stable\":%s,\"timestamp\":%lu}",
             data->weight, data->unit, data->stable ? "true" : "false",
             data->timestamp_ms);
    return json;
}

char *protocol_build_device_info(const char *device_id, const char *device_name,
                                  const char *firmware_version, uint8_t mode,
                                  const char *state) {
    char *json = malloc(256);
    if (!json) return NULL;

    snprintf(json, 256,
             "{\"device_id\":\"%s\",\"device_name\":\"%s\",\"firmware_version\":\"%s\",\"mode\":%d,\"state\":\"%s\"}",
             device_id, device_name, firmware_version, mode, state);
    return json;
}

char *protocol_build_network_status(bool wifi_connected, const char *ip,
                                     int8_t rssi, bool mqtt_connected) {
    char *json = malloc(128);
    if (!json) return NULL;

    snprintf(json, 128,
             "{\"wifi_connected\":%s,\"ip\":\"%s\",\"rssi\":%d,\"mqtt_connected\":%s}",
             wifi_connected ? "true" : "false", ip ? ip : "",
             rssi, mqtt_connected ? "true" : "false");
    return json;
}

const char *protocol_cmd_name(cmd_type_t cmd) {
    if (cmd >= 0 && cmd < sizeof(CMD_NAMES) / sizeof(CMD_NAMES[0])) {
        return CMD_NAMES[cmd];
    }
    return "unknown";
}
