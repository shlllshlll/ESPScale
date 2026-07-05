#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_host[64];
    uint16_t mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[64];
    char server_url[128];
    char device_id[24];
    char device_name[32];
    float cal_factor;
    char unit[4];
    uint32_t upload_interval_ms;
    uint8_t mode;
    char api_key[64];
    uint8_t cfg_version;
} stored_config_t;

typedef struct {
    char host[64];
    uint16_t port;
    char user[32];
    char pass[64];
} mqtt_config_t;

esp_err_t storage_init(void);
esp_err_t storage_load_config(stored_config_t *cfg);
esp_err_t storage_save_wifi(const char *ssid, const char *pass);
esp_err_t storage_save_cal_factor(float factor);
esp_err_t storage_save_unit(const char *unit);
esp_err_t storage_save_upload_interval(uint32_t interval_ms);
esp_err_t storage_save_mode(uint8_t mode);
esp_err_t storage_save_server_url(const char *url);
esp_err_t storage_save_mqtt_config(const mqtt_config_t *cfg);
esp_err_t storage_save_api_key(const char *key);
esp_err_t storage_save_device_name(const char *name);
esp_err_t storage_factory_reset(void);
const char *storage_get_device_id(void);

#ifdef __cplusplus
}
#endif
