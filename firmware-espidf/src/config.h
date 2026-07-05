#pragma once

#include <stdint.h>

// ============================================================================
// Pin Definitions
// ============================================================================
#define LED_PIN             8
#define HX711_SCK_PIN       3
#define HX711_DOUT_PIN      4

// ============================================================================
// BLE UUIDs
// ============================================================================
// All characteristics share the same group octets: DD6C-4ED8-9555-3BE20F962A74
#define SERVICE_UUID             "F3860834-DD6C-4ED8-9555-3BE20F962A74"
#define CHAR_DEVICE_INFO_UUID    "2A29F239-DD6C-4ED8-9555-3BE20F962A74"
#define CHAR_WIFI_CREDS_UUID     "C1B2A3D4-DD6C-4ED8-9555-3BE20F962A74"
#define CHAR_NETWORK_STATUS_UUID "D5E6F7A8-DD6C-4ED8-9555-3BE20F962A74"
#define CHAR_SCALE_SETTINGS_UUID "B9C8D7E6-DD6C-4ED8-9555-3BE20F962A74"
#define CHAR_WEIGHT_STREAM_UUID  "F5E4D3C2-DD6C-4ED8-9555-3BE20F962A74"
#define CHAR_COMMAND_UUID        "A1B2C3D4-DD6C-4ED8-9555-3BE20F962A74"
#define CHAR_EVENT_UUID          "E8F7A6B5-DD6C-4ED8-9555-3BE20F962A74"

// ============================================================================
// Timing Constants (ms)
// ============================================================================
#define WIFI_CONNECT_TIMEOUT_MS     15000
#define SCALE_READ_INTERVAL_MS      400
#define STATE_MACHINE_TICK_MS       100
#define HEARTBEAT_LOG_INTERVAL_MS   2000
#define HX711_READY_TIMEOUT_MS      80
#define HTTP_POST_INTERVAL_MS       5000
#define HTTP_TIMEOUT_MS             10000
#define MQTT_CONNECT_TIMEOUT_MS     10000
#define MQTT_RECONNECT_BASE_MS      5000

// ============================================================================
// WiFi Retry
// ============================================================================
#define WIFI_MAX_RETRY              3
#define WIFI_RECONNECT_BASE_MS      5000

// ============================================================================
// Device Info
// ============================================================================
#define FIRMWARE_VERSION            "0.3.0"
#define BLE_ADV_NAME_PREFIX         "ESPScale-"
#define DEFAULT_SERVER_URL          "http://espscale.shlll.top"

// ============================================================================
// Upload Modes
// ============================================================================
#define MODE_HTTP_DIRECT            0
#define MODE_MQTT                   1
#define MODE_BLE_ONLY               2

// ============================================================================
// MQTT Topics
// ============================================================================
#define MQTT_TOPIC_WEIGHT           "espscale/%s/weight"
#define MQTT_TOPIC_STATUS           "espscale/%s/status"
#define MQTT_TOPIC_CMD              "espscale/%s/cmd"

// ============================================================================
// NVS
// ============================================================================
#define NVS_NAMESPACE               "espscale"

// ============================================================================
// Task Stack Sizes
// ============================================================================
#define SCALE_TASK_STACK_SIZE       3072
#define BLE_TASK_STACK_SIZE         8192
#define NETWORK_TASK_STACK_SIZE     4096
#define STATE_TASK_STACK_SIZE       2048

// ============================================================================
// Task Priorities
// ============================================================================
#define SCALE_TASK_PRIORITY         5
#define BLE_TASK_PRIORITY           4
#define NETWORK_TASK_PRIORITY       3
#define STATE_TASK_PRIORITY         2

// ============================================================================
// Queue Sizes
// ============================================================================
#define WEIGHT_QUEUE_SIZE           8
#define CMD_QUEUE_SIZE              4

// ============================================================================
// Default Values
// ============================================================================
#define DEFAULT_CAL_FACTOR          397.6f
#define DEFAULT_UNIT                "g"
#define DEFAULT_UPLOAD_INTERVAL_MS  5000
#define DEFAULT_MQTT_PORT           1883

// ============================================================================
// JSON Buffer Size
// ============================================================================
#define JSON_BUFFER_SIZE            512
