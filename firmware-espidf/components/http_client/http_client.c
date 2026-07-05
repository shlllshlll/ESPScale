#include "http_client.h"
#include "esp_http_client.h"
#include "storage.h"
#include "protocol.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "http";

esp_err_t http_client_post_weight(const weight_data_t *data) {
    stored_config_t cfg;
    storage_load_config(&cfg);

    // Build URL
    char url[256];
    const char *server = strlen(cfg.server_url) > 0 ? cfg.server_url : DEFAULT_SERVER_URL;
    snprintf(url, sizeof(url), "%s/api/v1/data", server);

    // Build JSON payload
    char *json = protocol_build_weight(data);
    if (!json) return ESP_ERR_NO_MEM;

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(json);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Device-ID", cfg.device_id);
    if (strlen(cfg.api_key) > 0) {
        esp_http_client_set_header(client, "X-API-Key", cfg.api_key);
    }
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST status=%d weight=%.1f", status, data->weight);
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json);
    return err;
}
