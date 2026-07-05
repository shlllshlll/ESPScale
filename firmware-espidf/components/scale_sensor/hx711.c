#include "hx711.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include <string.h>

// GPIO functions
extern esp_err_t gpio_config(const void *pGPIOConfig);
extern int gpio_get_level(gpio_num_t gpio_num);
extern esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);

typedef struct {
    uint64_t pin_bit_mask;
    unsigned int mode: 2;
    unsigned int pull_up_en: 1;
    unsigned int pull_down_en: 1;
    unsigned int intr_type: 3;
} gpio_config_t;

#define GPIO_MODE_OUTPUT 2
#define GPIO_MODE_INPUT  1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0

static const char *TAG = "hx711";

// HX711 timing requirements:
// SCK high time: min 0.2us, max 50us
// SCK low time: min 0.2us
// DOUT falling edge to SCK rising edge: min 0.1us

#define HX711_SCK_HIGH_TIME_US  1
#define HX711_SCK_LOW_TIME_US   1

esp_err_t hx711_init(hx711_t *dev) {
    if (!dev) return ESP_ERR_INVALID_ARG;

    // Configure SCK as output
    gpio_config_t sck_cfg = {
        .pin_bit_mask = (1ULL << dev->sck_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&sck_cfg);
    if (ret != ESP_OK) return ret;

    // Configure DOUT as input
    gpio_config_t dout_cfg = {
        .pin_bit_mask = (1ULL << dev->dout_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&dout_cfg);
    if (ret != ESP_OK) return ret;

    // SCK initial low
    gpio_set_level(dev->sck_pin, 0);

    // Default values
    if (dev->scale == 0.0f) dev->scale = 1.0f;
    dev->offset = 0;

    ESP_LOGI(TAG, "HX711 initialized: DOUT=%d SCK=%d gain=%d",
             dev->dout_pin, dev->sck_pin, dev->gain);
    return ESP_OK;
}

bool hx711_is_ready(hx711_t *dev) {
    return gpio_get_level(dev->dout_pin) == 0;
}

int32_t hx711_read(hx711_t *dev) {
    // Wait for DOUT to go low (data ready)
    int timeout = 100;
    while (!hx711_is_ready(dev) && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    if (!hx711_is_ready(dev)) {
        ESP_LOGW(TAG, "HX711 timeout waiting for data");
        return 0;
    }

    // Read 24 bits (MSB first)
    int32_t data = 0;
    for (int i = 0; i < 24; i++) {
        gpio_set_level(dev->sck_pin, 1);
        ets_delay_us(HX711_SCK_HIGH_TIME_US);

        data = (data << 1) | gpio_get_level(dev->dout_pin);

        gpio_set_level(dev->sck_pin, 0);
        ets_delay_us(HX711_SCK_LOW_TIME_US);
    }

    // Send extra clock pulses to set gain
    for (int i = 0; i < dev->gain; i++) {
        gpio_set_level(dev->sck_pin, 1);
        ets_delay_us(HX711_SCK_HIGH_TIME_US);
        gpio_set_level(dev->sck_pin, 0);
        ets_delay_us(HX711_SCK_LOW_TIME_US);
    }

    // Convert to signed 24-bit integer
    if (data & 0x800000) {
        data |= (int32_t)0xFF000000;
    }

    return data;
}

int32_t hx711_read_average(hx711_t *dev, uint8_t times) {
    int64_t sum = 0;
    int valid = 0;

    for (int i = 0; i < times; i++) {
        int32_t val = hx711_read(dev);
        if (val != 0) {
            sum += val;
            valid++;
        }
    }

    return valid > 0 ? (int32_t)(sum / valid) : 0;
}

float hx711_get_units(hx711_t *dev, uint8_t times) {
    int32_t raw = hx711_read_average(dev, times);
    return (float)(raw - dev->offset) / dev->scale;
}

void hx711_tare(hx711_t *dev, uint8_t times) {
    int64_t sum = 0;
    for (int i = 0; i < times; i++) {
        sum += hx711_read(dev);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    dev->offset = (int32_t)(sum / times);
    ESP_LOGI(TAG, "Tare offset: %ld", dev->offset);
}

void hx711_set_scale(hx711_t *dev, float scale) {
    dev->scale = scale;
}

float hx711_get_scale(hx711_t *dev) {
    return dev->scale;
}

void hx711_set_offset(hx711_t *dev, int32_t offset) {
    dev->offset = offset;
}

int32_t hx711_get_offset(hx711_t *dev) {
    return dev->offset;
}

void hx711_power_down(hx711_t *dev) {
    gpio_set_level(dev->sck_pin, 0);
    gpio_set_level(dev->sck_pin, 1);
    ets_delay_us(60);
}

void hx711_power_up(hx711_t *dev) {
    gpio_set_level(dev->sck_pin, 0);
    ets_delay_us(1);
}
