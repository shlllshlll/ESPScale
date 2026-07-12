#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CMD_NONE = 0,
    CMD_TARE,
    CMD_CALIBRATE,
    CMD_SET_MODE,
    CMD_SET_CONFIG,
    CMD_SET_WIFI,
    CMD_SET_MQTT,
    CMD_GET_STATUS,
    CMD_REBOOT,
    CMD_FACTORY_RESET,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    char request_id[37];
    char raw_json[512];
} cmd_request_t;

typedef struct {
    float weight;
    char unit[4];
    bool stable;
    uint32_t timestamp_ms;
} weight_data_t;

cmd_request_t protocol_parse(const char *json);
char *protocol_build_ack(const char *cmd_name, bool success, const char *request_id, const char *data);
char *protocol_build_weight(const weight_data_t *data);
char *protocol_build_device_info(const char *device_id, const char *device_name,
                                  const char *firmware_version, uint8_t mode,
                                  const char *state);
char *protocol_build_network_status(bool wifi_connected, const char *ip,
                                     int8_t rssi, bool mqtt_connected);
const char *protocol_cmd_name(cmd_type_t cmd);
bool protocol_json_get_string(const char *json, const char *key, char *value, size_t value_len);
bool protocol_json_get_number(const char *json, const char *key, double *value);

#ifdef __cplusplus
}
#endif
