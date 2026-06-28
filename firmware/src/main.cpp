#include <Arduino.h>
#include "config.h"
#include "utils.h"
#include "storage.h"
#include "scale_sensor.h"
#include "wifi_manager.h"
#include "state_machine.h"
#include "ble_server.h"
#include "ble_protocol.h"
#include "http_client.h"
#include "mqtt_client.h"

static unsigned long lastScaleRead = 0;
static unsigned long lastStateTick = 0;
static unsigned long lastSerialPrint = 0;
static unsigned long lastUploadMs = 0;
static float prevWeight = 0.0f;
static bool weightStable = true;

void setup() {
    Serial.begin(115200);
    unsigned long waitStart = millis();
    while (!Serial && millis() - waitStart < 4000) { delay(10); }

    LOG_INFO("Booting ESPScale %s...", FIRMWARE_VERSION);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    storageBegin();
    scaleBegin(0.0f);
    wifiManagerBegin();
    bleServerBegin();
    httpClientBegin();
    mqttClientBegin();
    stateMachineBegin();

    LOG_INFO("Setup done, free heap: %u", ESP.getFreeHeap());
}

void loop() {
    unsigned long now = millis();

    // 1. HX711 读取 + BLE 推送 + Upload (400ms)
    if (now - lastScaleRead >= SCALE_READ_INTERVAL_MS) {
        lastScaleRead = now;
        float w = scaleReadWeight(5);

        // Detect weight stability (within 0.5g over one reading)
        weightStable = abs(w - prevWeight) < 0.5f;
        prevWeight = w;

        const auto& cfg = storageGet();

        // Push weight via BLE notify if a client is connected
        if (bleServerIsConnected()) {
            bleNotifyWeight(w, cfg.unit, weightStable);
        }

        // Upload based on mode (rate-limited by uploadIntervalMs)
        if (now - lastUploadMs >= cfg.uploadIntervalMs) {
            lastUploadMs = now;
            if (cfg.mode == MODE_HTTP_DIRECT) {
                httpClientPostWeight(w, cfg.unit, weightStable);
            } else if (cfg.mode == MODE_MQTT) {
                mqttClientPublishWeight(w, cfg.unit, weightStable);
            }
        }

        LOG_INFO("scale ready=%d raw=%ld weight=%.1f%s stable=%d send=%d",
            scaleIsReady(),
            scaleGetLastRaw(),
            w,
            cfg.unit.c_str(),
            weightStable,
            bleServerIsConnected());
    }

    // 2. 状态机 (100ms)
    if (now - lastStateTick >= STATE_TICK_INTERVAL_MS) {
        lastStateTick = now;
        stateMachineTick(now);
    }

    // 3. WiFi 管理
    wifiManagerTick();

    // 4. HTTP client (non-blocking)
    httpClientTick();

    // 5. MQTT client (non-blocking)
    mqttClientTick();

    // 6. BLE 维护
    bleServerTick();

    // 7. 心跳日志 (2000ms)
    if (now - lastSerialPrint >= SERIAL_HEARTBEAT_MS) {
        lastSerialPrint = now;
        const auto& cfg = storageGet();
        LOG_INFO("state=%s wifi=%d ble=%d mode=%u heap=%u",
            stateMachineGetStateName(),
            wifiManagerGetState() == WifiState::CONNECTED,
            bleServerIsConnected(),
            cfg.mode,
            ESP.getFreeHeap());
    }
}
