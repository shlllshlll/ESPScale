#pragma once
#include <Arduino.h>

struct StoredConfig {
    String wifiSsid;
    String wifiPass;
    String mqttHost;
    uint16_t mqttPort = 1883;
    String mqttUser;
    String mqttPass;
    String mqttTopic = "espscale";
    String deviceId;
    String deviceName = "ESPScale";
    float calFactor = 397.6f;
    String unit = "g";
    uint32_t uploadIntervalMs = 5000;
    uint8_t mode = 0;
    String apiKey;
    uint8_t cfgVersion = 1;
};

void storageBegin();
const StoredConfig& storageGet();
bool storageHasWifi();
void storageSaveWifi(const String& ssid, const String& pass);
void storageSaveCalFactor(float factor);
void storageFactoryReset();
