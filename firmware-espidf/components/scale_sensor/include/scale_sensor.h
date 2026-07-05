#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t scale_sensor_init(void);
float scale_sensor_read_weight(uint8_t samples);
void scale_sensor_tare(uint8_t samples);
float scale_sensor_calibrate(float expected_weight);
bool scale_sensor_is_ready(void);
float scale_sensor_get_cal_factor(void);

#ifdef __cplusplus
}
#endif
