#include "ble_protocol.h"
#include "ble_server.h"
#include "config.h"
#include "storage.h"
#include "protocol.h"
#include "wifi_manager.h"
#include "scale_sensor.h"
#include "state_machine.h"
#include "utils.h"
#include <ArduinoJson.h>
#include <WiFi.h>

void bleOnConnect(NimBLEServer* pServer) {
    (void)pServer;
    LOG_INFO("BLE client connected");
}

void bleOnDisconnect(NimBLEServer* pServer, int reason) {
    (void)pServer;
    LOG_INFO("BLE client disconnected, reason=%d", reason);
}

void bleOnDeviceInfoRead(NimBLECharacteristic* pChar) {
    const auto& cfg = storageGet();
    StaticJsonDocument<256> doc;
    doc["device_id"] = cfg.deviceId;
    doc["name"] = cfg.deviceName;
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["mode"] = cfg.mode == 0 ? "remote" : "local";
    doc["state"] = stateMachineGetStateName();
    String out;
    serializeJson(doc, out);
    pChar->setValue(out.c_str());
}

void bleOnWifiCredsWrite(NimBLECharacteristic* pChar) {
    std::string value = pChar->getValue();
    LOG_INFO("WiFi creds received: %s", value.c_str());

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err) {
        LOG_WARN("WiFi creds JSON parse error: %s", err.c_str());
        return;
    }

    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["password"] | "";

    if (strlen(ssid) == 0) {
        LOG_WARN("WiFi creds: ssid is empty");
        return;
    }

    LOG_INFO("Saving WiFi creds and connecting...");
    storageSaveWifi(ssid, pass);
    wifiManagerConnect(ssid, pass);
}

void bleOnNetworkStatusRead(NimBLECharacteristic* pChar) {
    StaticJsonDocument<256> doc;

    JsonObject wifi = doc.createNestedObject("wifi");
    bool wifiOk = (wifiManagerGetState() == WifiState::CONNECTED);
    wifi["connected"] = wifiOk;
    wifi["rssi"] = wifiOk ? wifiManagerGetRssi() : 0;
    if (wifiOk) {
        wifi["ip"] = WiFi.localIP().toString();
    }

    JsonObject mqtt = doc.createNestedObject("mqtt");
    mqtt["connected"] = false;

    String out;
    serializeJson(doc, out);
    pChar->setValue(out.c_str());
}

void bleOnCommandWrite(NimBLECharacteristic* pChar) {
    std::string value = pChar->getValue();
    LOG_INFO("Command received: %s", value.c_str());

    CmdRequest req = protocolParse(String(value.c_str()));

    if (req.cmd == Command::NONE) {
        LOG_WARN("Unknown command");
        return;
    }

    NimBLECharacteristic* pEvent = bleServerGetCharEvent();

    switch (req.cmd) {
    case Command::GET_STATUS: {
        const auto& cfg = storageGet();
        StaticJsonDocument<512> doc;
        doc["device_id"] = cfg.deviceId;
        doc["state"] = stateMachineGetStateName();
        doc["wifi_connected"] = (wifiManagerGetState() == WifiState::CONNECTED);
        doc["hx711_ready"] = scaleIsReady();
        doc["cal_factor"] = cfg.calFactor;
        doc["unit"] = cfg.unit;
        doc["mode"] = cfg.mode == 0 ? "remote" : "local";
        doc["heap_free"] = ESP.getFreeHeap();
        doc["uptime_s"] = millis() / 1000;
        doc["firmware_version"] = FIRMWARE_VERSION;
        String out;
        serializeJson(doc, out);
        pEvent->setValue(out.c_str());
        pEvent->notify();
        break;
    }
    case Command::REBOOT: {
        String ack = protocolBuildAck("reboot", true, req.requestId);
        pEvent->setValue(ack.c_str());
        pEvent->notify();
        delay(200);
        ESP.restart();
        break;
    }
    case Command::FACTORY_RESET: {
        String ack = protocolBuildAck("factory_reset", true, req.requestId);
        pEvent->setValue(ack.c_str());
        pEvent->notify();
        delay(200);
        storageFactoryReset();
        ESP.restart();
        break;
    }
    default: {
        String ack = protocolBuildAck(commandName(req.cmd), false, req.requestId);
        pEvent->setValue(ack.c_str());
        pEvent->notify();
        break;
    }
    }
}
