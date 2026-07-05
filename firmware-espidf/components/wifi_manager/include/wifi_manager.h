#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *pass);
esp_err_t wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);
int8_t wifi_manager_get_rssi(void);
const char *wifi_manager_get_ip(void);

#ifdef __cplusplus
}
#endif
