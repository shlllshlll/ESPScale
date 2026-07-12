#include "ble_protocol.h"
#include "ble_server.h"
#include "config.h"
#include "storage.h"
#include "protocol.h"
#include "command_dispatch.h"
#include "wifi_manager.h"
#include "scale_sensor.h"
#include "state_machine.h"
#include "mqtt_client.h"
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
    doc["mode"] = cfg.mode;
    doc["mode_label"] = cfg.mode == 0 ? "http_direct" : (cfg.mode == 1 ? "mqtt" : "ble_only");
    doc["state"] = stateMachineGetStateName();
    String out;
    serializeJson(doc, out);
    pChar->setValue(out.c_str());
}

void bleOnWifiCredsWrite(NimBLECharacteristic* pChar) {
    std::string value = pChar->getValue();
    LOG_INFO("WiFi creds received: %s", value.c_str());

    StaticJsonDocument<512> doc;
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

    // Extended fields: mode, server_url, mqtt config
    if (doc.containsKey("mode")) {
        storageSetMode(doc["mode"].as<uint8_t>());
        LOG_INFO("Mode set to %u", doc["mode"].as<uint8_t>());
    }
    if (doc.containsKey("server_url")) {
        const char* url = doc["server_url"] | "";
        storageSetServerUrl(url);
        LOG_INFO("Server URL set to %s", url);
    }
    if (doc.containsKey("mqtt_host")) {
        const char* host = doc["mqtt_host"] | "";
        uint16_t port = doc["mqtt_port"] | 1883;
        const char* user = doc["mqtt_user"] | "";
        const char* mqttPass = doc["mqtt_pass"] | "";
        storageSetMqttConfig(host, port, user, mqttPass);
        LOG_INFO("MQTT config set: %s:%d", host, port);
    }

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
    mqtt["connected"] = (mqttClientGetState() == MqttClientState::CONNECTED);

    String out;
    serializeJson(doc, out);
    pChar->setValue(out.c_str());
}

void bleNetworkStatusNotify() {
    NimBLECharacteristic* pChar = bleServerGetCharNetworkStatus();
    if (!pChar) return;

    StaticJsonDocument<256> doc;

    JsonObject wifi = doc.createNestedObject("wifi");
    bool wifiOk = (wifiManagerGetState() == WifiState::CONNECTED);
    wifi["connected"] = wifiOk;
    wifi["rssi"] = wifiOk ? wifiManagerGetRssi() : 0;
    if (wifiOk) {
        wifi["ip"] = WiFi.localIP().toString();
    }

    JsonObject mqtt = doc.createNestedObject("mqtt");
    mqtt["connected"] = (mqttClientGetState() == MqttClientState::CONNECTED);

    String out;
    serializeJson(doc, out);
    pChar->setValue(out.c_str());
    pChar->notify();
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

    // For GET_STATUS, the ack payload contains device status data
    // We need to send it as the event notification
    String ack = commandDispatch(req);

    // Check if it's a get_status response (contains data)
    if (req.cmd == Command::GET_STATUS) {
        // Parse ack to extract data field
        StaticJsonDocument<512> ackDoc;
        DeserializationError err = deserializeJson(ackDoc, ack);
        if (!err && ackDoc.containsKey("data")) {
            String dataStr;
            serializeJson(ackDoc["data"], dataStr);
            pEvent->setValue(dataStr.c_str());
            pEvent->notify();
            return;
        }
    }

    pEvent->setValue(ack.c_str());
    pEvent->notify();
}

void bleNotifyWeight(float weight, const String& unit, bool stable) {
    NimBLECharacteristic* pChar = bleServerGetCharWeightStream();
    if (!pChar) return;

    static uint32_t seq = 0;
    String payload = protocolBuildWeight(weight, unit, stable, seq++);

    pChar->setValue(payload.c_str());
    pChar->notify();
}
