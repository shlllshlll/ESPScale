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
        cmd_request_t cmd;
        while (xQueueReceive(g_cmd_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
            case CMD_TARE:
                scale_sensor_tare(20);
                ESP_LOGI(TAG, "Tare done");
                break;
            case CMD_CALIBRATE: {
                double expected = 500.0;
                protocol_json_get_number(cmd.raw_json, "expected_weight", &expected);
                float new_factor = scale_sensor_calibrate((float)expected);
                storage_save_cal_factor(new_factor);
                ESP_LOGI(TAG, "Calibrated: factor=%.2f", new_factor);
                break;
            }
            case CMD_SET_MODE: {
                double mode;
                if (protocol_json_get_number(cmd.raw_json, "mode", &mode)) {
                    storage_save_mode((uint8_t)mode);
                    ESP_LOGI(TAG, "Mode set to %d", (uint8_t)mode);
                }
                break;
            }
            case CMD_SET_CONFIG: {
                double mode, interval, mqtt_port;
                char server_url[128] = {0};
                char mqtt_host[64] = {0};
                char mqtt_user[32] = {0};
                char mqtt_pass[64] = {0};
                char unit[4] = {0};
                char api_key[64] = {0};

                if (protocol_json_get_number(cmd.raw_json, "mode", &mode)) {
                    storage_save_mode((uint8_t)mode);
                }
                if (protocol_json_get_string(cmd.raw_json, "server_url", server_url, sizeof(server_url))) {
                    storage_save_server_url(server_url);
                }
                if (protocol_json_get_string(cmd.raw_json, "mqtt_host", mqtt_host, sizeof(mqtt_host))) {
                    mqtt_config_t mqtt_cfg = {0};
                    strncpy(mqtt_cfg.host, mqtt_host, sizeof(mqtt_cfg.host) - 1);
                    mqtt_cfg.port = protocol_json_get_number(cmd.raw_json, "mqtt_port", &mqtt_port) ? (uint16_t)mqtt_port : DEFAULT_MQTT_PORT;
                    protocol_json_get_string(cmd.raw_json, "mqtt_user", mqtt_user, sizeof(mqtt_user));
                    protocol_json_get_string(cmd.raw_json, "mqtt_pass", mqtt_pass, sizeof(mqtt_pass));
                    strncpy(mqtt_cfg.user, mqtt_user, sizeof(mqtt_cfg.user) - 1);
                    strncpy(mqtt_cfg.pass, mqtt_pass, sizeof(mqtt_cfg.pass) - 1);
                    storage_save_mqtt_config(&mqtt_cfg);
                }
                if (protocol_json_get_string(cmd.raw_json, "unit", unit, sizeof(unit))) {
                    storage_save_unit(unit);
                }
                if (protocol_json_get_number(cmd.raw_json, "upload_interval_ms", &interval)) {
                    storage_save_upload_interval((uint32_t)interval);
                }
                if (protocol_json_get_string(cmd.raw_json, "api_key", api_key, sizeof(api_key))) {
                    storage_save_api_key(api_key);
                }
                ESP_LOGI(TAG, "Config updated via BLE command");
                break;
            }
            case CMD_SET_WIFI: {
                char ssid[33] = {0};
                char pass[65] = {0};
                if (protocol_json_get_string(cmd.raw_json, "ssid", ssid, sizeof(ssid)) && strlen(ssid) > 0) {
                    protocol_json_get_string(cmd.raw_json, "password", pass, sizeof(pass));
                    storage_save_wifi(ssid, pass);
                    wifi_manager_connect(ssid, pass);
                    ESP_LOGI(TAG, "WiFi updated: %s", ssid);
                }
                break;
            }
            case CMD_SET_MQTT: {
                char mqtt_host[64] = {0};
                char mqtt_user[32] = {0};
                char mqtt_pass[64] = {0};
                double mqtt_port = DEFAULT_MQTT_PORT;
                protocol_json_get_string(cmd.raw_json, "mqtt_host", mqtt_host, sizeof(mqtt_host));
                protocol_json_get_number(cmd.raw_json, "mqtt_port", &mqtt_port);
                protocol_json_get_string(cmd.raw_json, "mqtt_user", mqtt_user, sizeof(mqtt_user));
                protocol_json_get_string(cmd.raw_json, "mqtt_pass", mqtt_pass, sizeof(mqtt_pass));
                mqtt_config_t mqtt_cfg = {0};
                strncpy(mqtt_cfg.host, strlen(mqtt_host) > 0 ? mqtt_host : "localhost", sizeof(mqtt_cfg.host) - 1);
                mqtt_cfg.port = (uint16_t)mqtt_port;
                strncpy(mqtt_cfg.user, mqtt_user, sizeof(mqtt_cfg.user) - 1);
                strncpy(mqtt_cfg.pass, mqtt_pass, sizeof(mqtt_cfg.pass) - 1);
                storage_save_mqtt_config(&mqtt_cfg);
                ESP_LOGI(TAG, "MQTT config saved: %s:%d", mqtt_cfg.host, mqtt_cfg.port);
                break;
            }
            case CMD_REBOOT:
                ESP_LOGI(TAG, "Rebooting...");
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
                break;
            case CMD_FACTORY_RESET:
                ESP_LOGI(TAG, "Factory reset...");
                vTaskDelay(pdMS_TO_TICKS(200));
                storage_factory_reset();
                esp_restart();
                break;
            default:
                break;
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

    wifi_manager_init();

    stored_config_t cfg = {0};
    bool wifi_attempted = false;
    bool mqtt_started = false;
    weight_data_t data;
    TickType_t last_post = 0;
    TickType_t last_cfg_load = 0;
    TickType_t last_mode_log = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        // Reload NVS config periodically (not every loop — avoids spam + NVS wear)
        if ((now - last_cfg_load) >= pdMS_TO_TICKS(1000)) {
            xSemaphoreTake(g_config_mutex, portMAX_DELAY);
            storage_load_config(&cfg);
            xSemaphoreGive(g_config_mutex);
            last_cfg_load = now;
        }

        if ((now - last_mode_log) >= pdMS_TO_TICKS(10000)) {
            ESP_LOGI(TAG, "upload mode=%d wifi=%d mqtt=%d ssid=%s interval=%lums",
                     cfg.mode,
                     wifi_manager_is_connected(),
                     mqtt_handler_is_connected(),
                     cfg.wifi_ssid,
                     (unsigned long)(cfg.upload_interval_ms ? cfg.upload_interval_ms
                                                            : DEFAULT_UPLOAD_INTERVAL_MS));
            last_mode_log = now;
        }

        if (cfg.mode == MODE_BLE_ONLY) {
            s_device_state = STATE_RUNNING;
            if (mqtt_started) {
                mqtt_handler_stop();
                mqtt_handler_reset();
                mqtt_started = false;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // Connect WiFi once when credentials appear (BLE provision or NVS).
        if (!wifi_manager_is_connected() && strlen(cfg.wifi_ssid) > 0 && !wifi_attempted) {
            s_device_state = STATE_CONNECTING_WIFI;
            wifi_manager_connect(cfg.wifi_ssid, cfg.wifi_pass);
            wifi_attempted = true;

            EventBits_t bits = xEventGroupWaitBits(g_sys_events,
                                                    WIFI_CONNECTED_BIT,
                                                    pdFALSE, pdTRUE,
                                                    pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "WiFi connected");
                s_device_state = STATE_RUNNING;
            } else {
                ESP_LOGW(TAG, "WiFi connection timeout");
                s_device_state = STATE_ERROR_WIFI;
                wifi_attempted = false;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        // WiFi may also be started by BLE wifi_creds callback — detect that.
        if (wifi_manager_is_connected()) {
            wifi_attempted = true;
            if (cfg.mode == MODE_MQTT && !mqtt_started) {
                s_device_state = STATE_CONNECTING_MQTT;
                ESP_LOGI(TAG, "Starting MQTT client...");
                if (mqtt_handler_start() != ESP_OK) {
                    ESP_LOGE(TAG, "MQTT start failed");
                    mqtt_started = false;
                } else {
                    mqtt_started = true;
                }
            }
            if (cfg.mode == MODE_HTTP_DIRECT) {
                s_device_state = STATE_RUNNING;
            }
        }

        // Timer-based upload: peek latest sample (do NOT consume — BLE also peeks).
        // Old path used xQueueReceive which raced with BLE and could starve upload.
        uint32_t interval_ms = cfg.upload_interval_ms > 0
                                   ? cfg.upload_interval_ms
                                   : DEFAULT_UPLOAD_INTERVAL_MS;
        if ((now - last_post) >= pdMS_TO_TICKS(interval_ms)) {
            if (xQueuePeek(g_weight_queue, &data, 0) == pdTRUE) {
                if (!wifi_manager_is_connected()) {
                    ESP_LOGW(TAG, "Skip upload: WiFi not connected (mode=%d)", cfg.mode);
                } else if (cfg.mode == MODE_HTTP_DIRECT) {
                    ESP_LOGI(TAG, "Upload via HTTP weight=%.1f", data.weight);
                    http_client_post_weight(&data);
                    last_post = now;
                } else if (cfg.mode == MODE_MQTT) {
                    if (mqtt_handler_is_connected()) {
                        ESP_LOGI(TAG, "Upload via MQTT weight=%.1f", data.weight);
                        mqtt_handler_publish_weight(&data);
                        last_post = now;
                    } else {
                        ESP_LOGW(TAG, "Skip upload: MQTT not connected");
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
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
