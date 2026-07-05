#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "storage";

static stored_config_t s_config;
static char s_device_id[24] = {0};

static void generate_device_id(void) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_device_id, sizeof(s_device_id), "esp32c3-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
}

esp_err_t storage_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition full, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    generate_device_id();
    ESP_LOGI(TAG, "Storage initialized, device_id=%s", s_device_id);
    return ESP_OK;
}

esp_err_t storage_load_config(stored_config_t *cfg) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed, using defaults: %s", esp_err_to_name(ret));
        memset(cfg, 0, sizeof(*cfg));
        strncpy(cfg->device_id, s_device_id, sizeof(cfg->device_id) - 1);
        strncpy(cfg->device_name, "ESPScale", sizeof(cfg->device_name) - 1);
        cfg->cal_factor = DEFAULT_CAL_FACTOR;
        strncpy(cfg->unit, DEFAULT_UNIT, sizeof(cfg->unit) - 1);
        cfg->upload_interval_ms = DEFAULT_UPLOAD_INTERVAL_MS;
        cfg->mqtt_port = DEFAULT_MQTT_PORT;
        return ESP_OK;
    }

    memset(cfg, 0, sizeof(*cfg));

    size_t len;

    len = sizeof(cfg->wifi_ssid);
    nvs_get_str(handle, "wifi_ssid", cfg->wifi_ssid, &len);

    len = sizeof(cfg->wifi_pass);
    nvs_get_str(handle, "wifi_pass", cfg->wifi_pass, &len);

    len = sizeof(cfg->mqtt_host);
    nvs_get_str(handle, "mqtt_host", cfg->mqtt_host, &len);

    uint16_t mqtt_port = DEFAULT_MQTT_PORT;
    if (nvs_get_u16(handle, "mqtt_port", &mqtt_port) == ESP_OK) {
        cfg->mqtt_port = mqtt_port;
    } else {
        cfg->mqtt_port = DEFAULT_MQTT_PORT;
    }

    len = sizeof(cfg->mqtt_user);
    nvs_get_str(handle, "mqtt_user", cfg->mqtt_user, &len);

    len = sizeof(cfg->mqtt_pass);
    nvs_get_str(handle, "mqtt_pass", cfg->mqtt_pass, &len);

    len = sizeof(cfg->server_url);
    nvs_get_str(handle, "server_url", cfg->server_url, &len);

    len = sizeof(cfg->device_name);
    nvs_get_str(handle, "device_name", cfg->device_name, &len);
    if (strlen(cfg->device_name) == 0) {
        strncpy(cfg->device_name, "ESPScale", sizeof(cfg->device_name) - 1);
    }

    float cal_factor = DEFAULT_CAL_FACTOR;
    if (nvs_get_blob(handle, "cal_factor", &cal_factor, &(size_t){sizeof(float)}) == ESP_OK) {
        cfg->cal_factor = cal_factor;
    } else {
        cfg->cal_factor = DEFAULT_CAL_FACTOR;
    }

    len = sizeof(cfg->unit);
    nvs_get_str(handle, "unit", cfg->unit, &len);
    if (strlen(cfg->unit) == 0) {
        strncpy(cfg->unit, DEFAULT_UNIT, sizeof(cfg->unit) - 1);
    }

    uint32_t upload_interval = DEFAULT_UPLOAD_INTERVAL_MS;
    if (nvs_get_u32(handle, "upload_ms", &upload_interval) == ESP_OK) {
        cfg->upload_interval_ms = upload_interval;
    } else {
        cfg->upload_interval_ms = DEFAULT_UPLOAD_INTERVAL_MS;
    }

    uint8_t mode = MODE_HTTP_DIRECT;
    if (nvs_get_u8(handle, "mode", &mode) == ESP_OK) {
        cfg->mode = mode;
    } else {
        cfg->mode = MODE_HTTP_DIRECT;
    }

    len = sizeof(cfg->api_key);
    nvs_get_str(handle, "api_key", cfg->api_key, &len);

    uint8_t cfg_version = 1;
    if (nvs_get_u8(handle, "cfg_ver", &cfg_version) == ESP_OK) {
        cfg->cfg_version = cfg_version;
    } else {
        cfg->cfg_version = 1;
    }

    strncpy(cfg->device_id, s_device_id, sizeof(cfg->device_id) - 1);

    nvs_close(handle);
    ESP_LOGI(TAG, "Config loaded: mode=%d, unit=%s, cal=%.2f",
             cfg->mode, cfg->unit, cfg->cal_factor);
    return ESP_OK;
}

esp_err_t storage_save_wifi(const char *ssid, const char *pass) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_str(handle, "wifi_ssid", ssid);
    nvs_set_str(handle, "wifi_pass", pass ? pass : "");
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        strncpy(s_config.wifi_ssid, ssid, sizeof(s_config.wifi_ssid) - 1);
        strncpy(s_config.wifi_pass, pass ? pass : "", sizeof(s_config.wifi_pass) - 1);
        ESP_LOGI(TAG, "WiFi saved: ssid=%s", ssid);
    }
    return ret;
}

esp_err_t storage_save_cal_factor(float factor) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_blob(handle, "cal_factor", &factor, sizeof(float));
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        s_config.cal_factor = factor;
        ESP_LOGI(TAG, "Cal factor saved: %.2f", factor);
    }
    return ret;
}

esp_err_t storage_save_unit(const char *unit) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_str(handle, "unit", unit);
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        strncpy(s_config.unit, unit, sizeof(s_config.unit) - 1);
        ESP_LOGI(TAG, "Unit saved: %s", unit);
    }
    return ret;
}

esp_err_t storage_save_upload_interval(uint32_t interval_ms) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_u32(handle, "upload_ms", interval_ms);
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        s_config.upload_interval_ms = interval_ms;
        ESP_LOGI(TAG, "Upload interval saved: %lu ms", interval_ms);
    }
    return ret;
}

esp_err_t storage_save_mode(uint8_t mode) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_u8(handle, "mode", mode);
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        s_config.mode = mode;
        ESP_LOGI(TAG, "Mode saved: %d", mode);
    }
    return ret;
}

esp_err_t storage_save_server_url(const char *url) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_str(handle, "server_url", url);
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        strncpy(s_config.server_url, url, sizeof(s_config.server_url) - 1);
        ESP_LOGI(TAG, "Server URL saved: %s", url);
    }
    return ret;
}

esp_err_t storage_save_mqtt_config(const mqtt_config_t *cfg) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_str(handle, "mqtt_host", cfg->host);
    nvs_set_u16(handle, "mqtt_port", cfg->port);
    nvs_set_str(handle, "mqtt_user", cfg->user);
    nvs_set_str(handle, "mqtt_pass", cfg->pass);
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        strncpy(s_config.mqtt_host, cfg->host, sizeof(s_config.mqtt_host) - 1);
        s_config.mqtt_port = cfg->port;
        strncpy(s_config.mqtt_user, cfg->user, sizeof(s_config.mqtt_user) - 1);
        strncpy(s_config.mqtt_pass, cfg->pass, sizeof(s_config.mqtt_pass) - 1);
        ESP_LOGI(TAG, "MQTT config saved: host=%s port=%d", cfg->host, cfg->port);
    }
    return ret;
}

esp_err_t storage_save_api_key(const char *key) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_str(handle, "api_key", key);
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        strncpy(s_config.api_key, key, sizeof(s_config.api_key) - 1);
        ESP_LOGI(TAG, "API key saved");
    }
    return ret;
}

esp_err_t storage_save_device_name(const char *name) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_str(handle, "device_name", name);
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        strncpy(s_config.device_name, name, sizeof(s_config.device_name) - 1);
        ESP_LOGI(TAG, "Device name saved: %s", name);
    }
    return ret;
}

esp_err_t storage_factory_reset(void) {
    esp_err_t ret = nvs_flash_erase();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS erased, factory reset complete");
        memset(&s_config, 0, sizeof(s_config));
    }
    return ret;
}

const char *storage_get_device_id(void) {
    return s_device_id;
}
