#pragma once
#include <cstdint>

// --- 引脚定义 ---
constexpr uint8_t LED_PIN = 8;
constexpr uint8_t HX711_SCK_PIN = 3;
constexpr uint8_t HX711_DOUT_PIN = 4;

// --- BLE UUID ---
constexpr const char* SERVICE_UUID = "F3860834-DD6C-4ED8-9555-3BE20F962A74";
constexpr const char* CHAR_DEVICE_INFO_UUID = "2A29F239-DD6C-4ED8-9555-3BE20F962A74";
constexpr const char* CHAR_WIFI_CREDS_UUID = "C1B2A3D4-DD6C-4ED8-9555-3BE20F962A74";
constexpr const char* CHAR_NETWORK_STATUS_UUID = "D5E6F7A8-DD6C-4ED8-9555-3BE20F962A74";
constexpr const char* CHAR_SCALE_SETTINGS_UUID = "B9C8D7E6-DD6C-4ED8-9555-3BE20F962A74";
constexpr const char* CHAR_WEIGHT_STREAM_UUID = "F5E4D3C2-DD6C-4ED8-9555-3BE20F962A74";
constexpr const char* CHAR_COMMAND_UUID = "A1B2C3D4-DD6C-4ED8-9555-3BE20F962A74";
constexpr const char* CHAR_EVENT_UUID = "E8F7A6B5-DD6C-4ED8-9555-3BE20F962A74";

// --- 时间常量 (ms) ---
constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
constexpr unsigned long SCALE_READ_INTERVAL_MS = 400;
constexpr unsigned long STATE_TICK_INTERVAL_MS = 100;
constexpr unsigned long SERIAL_HEARTBEAT_MS = 2000;
constexpr unsigned long HX711_READY_TIMEOUT_MS = 80;

// --- WiFi 重连 ---
constexpr uint8_t WIFI_MAX_RETRY = 3;
constexpr unsigned long WIFI_RECONNECT_BASE_MS = 5000;

// --- 设备信息 ---
constexpr const char* FIRMWARE_VERSION = "0.2.0";
constexpr const char* DEVICE_NAME_PREFIX = "ESPScale-";

// --- 服务器地址 ---
constexpr const char* DEFAULT_SERVER_URL = "http://espscale.shlll.top";

// --- 上报模式 ---
constexpr uint8_t MODE_HTTP_DIRECT = 0;
constexpr uint8_t MODE_MQTT = 1;
constexpr uint8_t MODE_BLE_ONLY = 2;
constexpr uint8_t DEFAULT_MODE = MODE_BLE_ONLY;

// --- HTTP ---
constexpr unsigned long HTTP_TIMEOUT_MS = 10000;
constexpr unsigned long HTTP_POST_INTERVAL_MS = 5000;

// --- MQTT ---
constexpr unsigned long MQTT_CONNECT_TIMEOUT_MS = 10000;
constexpr unsigned long MQTT_RECONNECT_BASE_MS = 5000;
constexpr uint8_t MQTT_MAX_RETRY = 3;

#ifndef DEFAULT_MQTT_USER
#define DEFAULT_MQTT_USER ""
#endif
#ifndef DEFAULT_MQTT_PASS
#define DEFAULT_MQTT_PASS ""
#endif

// --- NVS ---
constexpr const char* NVS_NAMESPACE = "espscale";
