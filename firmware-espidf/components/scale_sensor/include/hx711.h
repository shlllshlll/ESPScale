#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare gpio_num_t
typedef int gpio_num_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HX711_GAIN_128 = 1,   // Channel A, gain 128
    HX711_GAIN_64  = 3,   // Channel A, gain 64
    HX711_GAIN_32  = 2,   // Channel B, gain 32
} hx711_gain_t;

typedef struct {
    gpio_num_t dout_pin;
    gpio_num_t sck_pin;
    hx711_gain_t gain;
    float scale;
    int32_t offset;
} hx711_t;

esp_err_t hx711_init(hx711_t *dev);
bool hx711_is_ready(hx711_t *dev);
int32_t hx711_read(hx711_t *dev);
int32_t hx711_read_average(hx711_t *dev, uint8_t times);
float hx711_get_units(hx711_t *dev, uint8_t times);
void hx711_tare(hx711_t *dev, uint8_t times);
void hx711_set_scale(hx711_t *dev, float scale);
float hx711_get_scale(hx711_t *dev);
void hx711_set_offset(hx711_t *dev, int32_t offset);
int32_t hx711_get_offset(hx711_t *dev);
void hx711_power_down(hx711_t *dev);
void hx711_power_up(hx711_t *dev);

#ifdef __cplusplus
}
#endif
