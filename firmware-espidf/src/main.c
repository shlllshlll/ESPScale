#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "config.h"
#include "app_event_defs.h"
#include "storage.h"
#include "protocol.h"
#include "scale_sensor.h"
#include "ble_service.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "http_client.h"

static const char *TAG = "main";

// Event base definition
ESP_EVENT_DEFINE_BASE(APP_EVENT);

// Event group bits
#define WIFI_CONNECTED_BIT   BIT0
#define MQTT_CONNECTED_BIT   BIT1
#define BLE_CONNECTED_BIT    BIT2
#define SCALE_READY_BIT      BIT3

// Device state
typedef enum {
    STATE_PROVISIONING,
    STATE_CONNECTING_WIFI,
    STATE_CONNECTING_MQTT,
    STATE_RUNNING,
    STATE_ERROR_WIFI,
    STATE_ERROR_MQTT,
    STATE_ERROR_HX711,
    STATE_FACTORY_RESET,
} device_state_t;

// Task handles
static TaskHandle_t s_scale_task_handle = NULL;
static TaskHandle_t s_ble_task_handle = NULL;
static TaskHandle_t s_network_task_handle = NULL;
static TaskHandle_t s_state_task_handle = NULL;

// Communication primitives
QueueHandle_t g_weight_queue = NULL;
QueueHandle_t g_cmd_queue = NULL;
EventGroupHandle_t g_sys_events = NULL;
SemaphoreHandle_t g_config_mutex = NULL;

// Device state
static volatile device_state_t s_device_state = STATE_PROVISIONING;

// Forward declarations
static void scale_task(void *pvParameters);
static void ble_task(void *pvParameters);
static void network_task(void *pvParameters);
static void state_task(void *pvParameters);
static void led_init(void);
static void led_set(bool on);
static void led_blink_pattern(int on_ms, int off_ms, int count);

// ============================================================================
// LED Control
// ============================================================================
static void led_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_PIN, 0); // LED on (active low)
}

static void led_set(bool on) {
    gpio_set_level(LED_PIN, on ? 0 : 1);
}

// ============================================================================
// Scale Task
// ============================================================================
static void scale_task(void *pvParameters) {
    ESP_LOGI(TAG, "Scale task started");

    esp_err_t ret = scale_sensor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scale sensor init failed");
        s_device_state = STATE_ERROR_HX711;
        vTaskDelete(NULL);
        return;
    }

    xEventGroupSetBits(g_sys_events, SCALE_READY_BIT);

    stored_config_t cfg;
    xSemaphoreTake(g_config_mutex, portMAX_DELAY);
    storage_load_config(&cfg);
    xSemaphoreGive(g_config_mutex);

    weight_data_t data;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        // Process commands
        cmd_request_t cmd;
        while (xQueueReceive(g_cmd_queue, &cmd, 0) == pdTRUE) {
            if (cmd.type == CMD_TARE) {
                scale_sensor_tare(20);
                ESP_LOGI(TAG, "Tare done");
            } else if (cmd.type == CMD_CALIBRATE) {
                // TODO: Extract expected_weight from params
                float new_factor = scale_sensor_calibrate(500.0f);
                ESP_LOGI(TAG, "Calibrated: factor=%.2f", new_factor);
            }
        }

        // Read weight
        if (scale_sensor_is_ready()) {
            float weight = scale_sensor_read_weight(5);

            data.weight = weight;
            strncpy(data.unit, cfg.unit, sizeof(data.unit) - 1);
            data.stable = true; // TODO: Implement stability detection
            data.timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

            // Send to queue (non-blocking)
            xQueueOverwrite(g_weight_queue, &data);

            static int log_count = 0;
            if (++log_count >= 5) { // Log every 2 seconds
                ESP_LOGI(TAG, "weight=%.1f%s heap=%lu",
                         weight, cfg.unit, esp_get_free_heap_size());
                log_count = 0;
            }
        } else {
            ESP_LOGW(TAG, "HX711 not ready");
        }

        // Precise 400ms period
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SCALE_READ_INTERVAL_MS));
    }
}

// ============================================================================
// BLE Task
// ============================================================================
static void ble_task(void *pvParameters) {
    ESP_LOGI(TAG, "BLE task started");

    esp_err_t ret = ble_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE service init failed");
        vTaskDelete(NULL);
        return;
    }

    weight_data_t data;

    while (1) {
        // Wait for weight data
        if (xQueuePeek(g_weight_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (ble_is_connected()) {
                ble_notify_weight(&data);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// Network Task
// ============================================================================
static void network_task(void *pvParameters) {
    ESP_LOGI(TAG, "Network task started");

    // Wait for WiFi
    wifi_manager_init();

    stored_config_t cfg;
    xSemaphoreTake(g_config_mutex, portMAX_DELAY);
    storage_load_config(&cfg);
    xSemaphoreGive(g_config_mutex);

    // Connect WiFi if credentials exist
    if (strlen(cfg.wifi_ssid) > 0) {
        s_device_state = STATE_CONNECTING_WIFI;
        wifi_manager_connect(cfg.wifi_ssid, cfg.wifi_pass);
    }

    // Wait for WiFi connection
    EventBits_t bits = xEventGroupWaitBits(g_sys_events,
                                            WIFI_CONNECTED_BIT,
                                            pdFALSE, pdTRUE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
        s_device_state = STATE_RUNNING;

        // Init MQTT if mode is MQTT
        if (cfg.mode == MODE_MQTT) {
            s_device_state = STATE_CONNECTING_MQTT;
            mqtt_handler_init();
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout");
        s_device_state = STATE_ERROR_WIFI;
    }

    weight_data_t data;
    TickType_t last_post = 0;

    while (1) {
        // Wait for weight data
        if (xQueueReceive(g_weight_queue, &data, pdMS_TO_TICKS(1000)) == pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            TickType_t interval = pdMS_TO_TICKS(cfg.upload_interval_ms);

            if ((now - last_post) >= interval) {
                if (wifi_manager_is_connected()) {
                    if (cfg.mode == MODE_HTTP_DIRECT) {
                        http_client_post_weight(&data);
                    } else if (cfg.mode == MODE_MQTT && mqtt_handler_is_connected()) {
                        mqtt_handler_publish_weight(&data);
                    }
                    last_post = now;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================================
// State Task
// ============================================================================
static void state_task(void *pvParameters) {
    ESP_LOGI(TAG, "State task started");

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        switch (s_device_state) {
            case STATE_PROVISIONING:
                // Slow blink: 200ms on, 2000ms off
                led_blink_pattern(200, 2000, 1);
                break;

            case STATE_CONNECTING_WIFI:
            case STATE_CONNECTING_MQTT:
                // Fast blink: 100ms on, 500ms off
                led_blink_pattern(100, 500, 1);
                break;

            case STATE_RUNNING:
                // Solid on
                led_set(true);
                break;

            case STATE_ERROR_WIFI:
                // Triple short blink
                led_blink_pattern(100, 100, 3);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;

            case STATE_ERROR_MQTT:
                // Off
                led_set(false);
                break;

            case STATE_ERROR_HX711:
                // Triple medium blink
                led_blink_pattern(300, 300, 3);
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case STATE_FACTORY_RESET:
                led_set(false);
                break;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STATE_MACHINE_TICK_MS));
    }
}

static void led_blink_pattern(int on_ms, int off_ms, int count) {
    for (int i = 0; i < count; i++) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_set(false);
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

// ============================================================================
// App Main
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "ESPScale starting... (v%s)", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Init NVS
    ESP_ERROR_CHECK(storage_init());

    // Create communication primitives
    g_weight_queue = xQueueCreate(WEIGHT_QUEUE_SIZE, sizeof(weight_data_t));
    g_cmd_queue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(cmd_request_t));
    g_sys_events = xEventGroupCreate();
    g_config_mutex = xSemaphoreCreateMutex();

    if (!g_weight_queue || !g_cmd_queue || !g_sys_events || !g_config_mutex) {
        ESP_LOGE(TAG, "Failed to create communication primitives");
        return;
    }

    // Init LED
    led_init();

    // Create tasks
    xTaskCreate(scale_task, "scale", SCALE_TASK_STACK_SIZE, NULL,
                SCALE_TASK_PRIORITY, &s_scale_task_handle);

    xTaskCreate(ble_task, "ble", BLE_TASK_STACK_SIZE, NULL,
                BLE_TASK_PRIORITY, &s_ble_task_handle);

    xTaskCreate(network_task, "network", NETWORK_TASK_STACK_SIZE, NULL,
                NETWORK_TASK_PRIORITY, &s_network_task_handle);

    xTaskCreate(state_task, "state", STATE_TASK_STACK_SIZE, NULL,
                STATE_TASK_PRIORITY, &s_state_task_handle);

    ESP_LOGI(TAG, "All tasks created, free heap: %lu bytes", esp_get_free_heap_size());
}
