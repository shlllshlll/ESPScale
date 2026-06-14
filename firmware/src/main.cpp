#include <Arduino.h>
#include "config.h"
#include "utils.h"
#include "storage.h"
#include "scale_sensor.h"
#include "wifi_manager.h"
#include "state_machine.h"
#include "ble_server.h"

static unsigned long lastScaleRead = 0;
static unsigned long lastStateTick = 0;
static unsigned long lastSerialPrint = 0;

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
    stateMachineBegin();

    LOG_INFO("Setup done, free heap: %u", ESP.getFreeHeap());
}

void loop() {
    unsigned long now = millis();

    // 1. HX711 读取 (400ms)
    if (now - lastScaleRead >= SCALE_READ_INTERVAL_MS) {
        lastScaleRead = now;
        scaleReadWeight(5);
    }

    // 2. 状态机 (100ms)
    if (now - lastStateTick >= STATE_TICK_INTERVAL_MS) {
        lastStateTick = now;
        stateMachineTick(now);
    }

    // 3. WiFi 管理
    wifiManagerTick();

    // 4. BLE 维护
    bleServerTick();

    // 5. 心跳日志 (2000ms)
    if (now - lastSerialPrint >= SERIAL_HEARTBEAT_MS) {
        lastSerialPrint = now;
        const auto& cfg = storageGet();
        LOG_INFO("state=%s wifi=%d ble=%d heap=%u",
            stateMachineGetStateName(),
            wifiManagerGetState() == WifiState::CONNECTED,
            bleServerIsConnected(),
            ESP.getFreeHeap());
    }
}
