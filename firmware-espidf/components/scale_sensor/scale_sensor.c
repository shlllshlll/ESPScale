#include "scale_sensor.h"
#include "hx711.h"
#include "storage.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "scale";

static hx711_t s_hx711;
static bool s_ready = false;

esp_err_t scale_sensor_init(void) {
    stored_config_t cfg;
    storage_load_config(&cfg);

    s_hx711 = (hx711_t){
        .dout_pin = HX711_DOUT_PIN,
        .sck_pin = HX711_SCK_PIN,
        .gain = HX711_GAIN_128,
        .scale = cfg.cal_factor > 0 ? cfg.cal_factor : DEFAULT_CAL_FACTOR,
        .offset = 0,
    };

    esp_err_t ret = hx711_init(&s_hx711);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HX711 init failed");
        return ret;
    }

    // Wait for HX711 to be ready
    int retries = 30;
    while (!hx711_is_ready(&s_hx711) && retries > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retries--;
    }

    if (!hx711_is_ready(&s_hx711)) {
        ESP_LOGE(TAG, "HX711 not found");
        s_ready = false;
        return ESP_ERR_TIMEOUT;
    }

    // Tare
    hx711_tare(&s_hx711, 20);
    s_ready = true;

    ESP_LOGI(TAG, "Scale ready, cal_factor=%.2f", s_hx711.scale);
    return ESP_OK;
}

float scale_sensor_read_weight(uint8_t samples) {
    if (!s_ready) return 0.0f;
    return hx711_get_units(&s_hx711, samples);
}

void scale_sensor_tare(uint8_t samples) {
    if (!s_ready) return;
    hx711_tare(&s_hx711, samples);
}

float scale_sensor_calibrate(float expected_weight) {
    if (!s_ready) return 0.0f;

    // Tare first
    hx711_tare(&s_hx711, 20);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Read average raw value
    int32_t raw = hx711_read_average(&s_hx711, 20);
    float new_factor = (float)raw / expected_weight;

    // Update calibration factor
    s_hx711.scale = new_factor;
    storage_save_cal_factor(new_factor);

    ESP_LOGI(TAG, "Calibrated: raw=%ld expected=%.1f factor=%.2f",
             raw, expected_weight, new_factor);
    return new_factor;
}

bool scale_sensor_is_ready(void) {
    return s_ready;
}

float scale_sensor_get_cal_factor(void) {
    return s_hx711.scale;
}
