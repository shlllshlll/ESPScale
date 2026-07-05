#pragma once

#include "esp_err.h"
#include "protocol.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_handler_init(void);
esp_err_t mqtt_handler_publish_weight(const weight_data_t *data);
esp_err_t mqtt_handler_publish_status(const char *status);
bool mqtt_handler_is_connected(void);

#ifdef __cplusplus
}
#endif
