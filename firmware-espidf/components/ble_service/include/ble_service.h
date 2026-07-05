#pragma once

#include "esp_err.h"
#include "protocol.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_service_init(void);
esp_err_t ble_notify_weight(const weight_data_t *data);
esp_err_t ble_notify_network_status(void);
bool ble_is_connected(void);
uint16_t ble_get_conn_handle(void);

#ifdef __cplusplus
}
#endif
