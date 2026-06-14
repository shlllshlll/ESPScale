#pragma once
#include <Arduino.h>
#include <WiFi.h>

#define LOG_INFO(fmt, ...)  Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

inline String generateDeviceId() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return "esp32c3-" + mac.substring(6);
}
