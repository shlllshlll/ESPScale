#pragma once
#include <Arduino.h>
#include "config.h"

struct StoredConfig {
    String wifiSsid;
    String wifiPass;
    String mqttHost;
    uint16_t mqttPort = 1883;
    String mqttUser;
    String mqttPass;
    String mqttTopic = "espscale";
    String serverUrl;
    String deviceId;
    String deviceName = "ESPScale";
    float calFactor = 397.6f;
    String unit = "g";
    uint32_t uploadIntervalMs = 5000;
    uint8_t mode = DEFAULT_MODE;  // 0=HTTP_DIRECT, 1=MQTT, 2=BLE_ONLY
    String apiKey;
    uint8_t cfgVersion = 1;
};

void storageBegin();
const StoredConfig& storageGet();
bool storageHasWifi();
void storageSaveWifi(const String& ssid, const String& pass);
void storageSaveCalFactor(float factor);
void storageSaveUnit(const String& unit);
void storageSaveUploadInterval(uint32_t ms);
void storageSaveConfig(const StoredConfig& cfg);
void storageSetMode(uint8_t mode);
void storageSetServerUrl(const String& url);
void storageSaveApiKey(const String& key);
void storageSetMqttConfig(const String& host, uint16_t port, const String& user, const String& pass);
void storageFactoryReset();
