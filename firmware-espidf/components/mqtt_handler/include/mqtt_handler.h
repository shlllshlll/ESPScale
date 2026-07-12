#pragma once

#include "esp_err.h"
#include "protocol.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_handler_init(void);
esp_err_t mqtt_handler_start(void);
esp_err_t mqtt_handler_stop(void);
esp_err_t mqtt_handler_publish_weight(const weight_data_t *data);
esp_err_t mqtt_handler_publish_status(const char *status);
esp_err_t mqtt_handler_publish_event(const char *payload);
bool mqtt_handler_is_connected(void);
void mqtt_handler_reset(void);

#ifdef __cplusplus
}
#endif