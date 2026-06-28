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

// Helper: parse JSON params from raw command JSON
static JsonObject parseParams(const String& rawJson) {
    static StaticJsonDocument<512> doc;
    doc.clear();
    deserializeJson(doc, rawJson);
    return doc["params"].as<JsonObject>();
}

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
    mqtt["connected"] = false;

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
    mqtt["connected"] = false;

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
        doc["mode"] = cfg.mode;
        doc["server_url"] = cfg.serverUrl;
        doc["mqtt_host"] = cfg.mqttHost;
        doc["mqtt_port"] = cfg.mqttPort;
        doc["heap_free"] = ESP.getFreeHeap();
        doc["uptime_s"] = millis() / 1000;
        doc["firmware_version"] = FIRMWARE_VERSION;
        String out;
        serializeJson(doc, out);
        pEvent->setValue(out.c_str());
        pEvent->notify();
        break;
    }
    case Command::SET_CONFIG: {
        JsonObject params = parseParams(req.rawJson);
        if (params.isNull()) {
            String ack = protocolBuildAck("set_config", false, req.requestId);
            pEvent->setValue(ack.c_str());
            pEvent->notify();
            break;
        }

        if (params.containsKey("mode")) storageSetMode(params["mode"].as<uint8_t>());
        if (params.containsKey("server_url")) storageSetServerUrl(params["server_url"].as<const char*>());
        if (params.containsKey("mqtt_host")) {
            storageSetMqttConfig(
                params["mqtt_host"] | "",
                params["mqtt_port"] | 1883,
                params["mqtt_user"] | "",
                params["mqtt_pass"] | ""
            );
        }
        if (params.containsKey("unit")) {
            storageSaveUnit(params["unit"].as<const char*>());
        }
        if (params.containsKey("upload_interval_ms")) {
            storageSaveUploadInterval(params["upload_interval_ms"].as<uint32_t>());
        }

        LOG_INFO("Config updated via BLE");
        String ack = protocolBuildAck("set_config", true, req.requestId);
        pEvent->setValue(ack.c_str());
        pEvent->notify();
        break;
    }
    case Command::SET_MQTT: {
        JsonObject params = parseParams(req.rawJson);
        if (params.isNull()) {
            String ack = protocolBuildAck("set_mqtt", false, req.requestId);
            pEvent->setValue(ack.c_str());
            pEvent->notify();
            break;
        }
        storageSetMqttConfig(
            params["mqtt_host"] | "localhost",
            params["mqtt_port"] | 1883,
            params["mqtt_user"] | "",
            params["mqtt_pass"] | ""
        );
        LOG_INFO("MQTT config saved via BLE");
        String ack = protocolBuildAck("set_mqtt", true, req.requestId);
        pEvent->setValue(ack.c_str());
        pEvent->notify();
        break;
    }
    case Command::SET_MODE: {
        JsonObject params = parseParams(req.rawJson);
        if (!params.isNull() && params.containsKey("mode")) {
            storageSetMode(params["mode"].as<uint8_t>());
            LOG_INFO("Mode set to %u", params["mode"].as<uint8_t>());
        }
        String ack = protocolBuildAck("set_mode", true, req.requestId);
        pEvent->setValue(ack.c_str());
        pEvent->notify();
        break;
    }
    case Command::TARE: {
        scaleTare(20);
        LOG_INFO("Tare done, reading=%ld", scaleGetLastRaw());
        String ack = protocolBuildAck("tare", true, req.requestId);
        pEvent->setValue(ack.c_str());
        pEvent->notify();
        break;
    }
    case Command::CALIBRATE: {
        JsonObject params = parseParams(req.rawJson);
        float expected = params["expected_weight"] | 100.0f;
        float newFactor = scaleCalibrate(expected);
        storageSaveCalFactor(newFactor);
        LOG_INFO("Calibrated: new factor=%.2f", newFactor);
        String ack = protocolBuildAck("calibrate", true, req.requestId);
        pEvent->setValue(ack.c_str());
        pEvent->notify();
        break;
    }
    case Command::SET_WIFI: {
        JsonObject params = parseParams(req.rawJson);
        if (!params.isNull()) {
            const char* ssid = params["ssid"] | "";
            const char* pass = params["password"] | "";
            if (strlen(ssid) > 0) {
                storageSaveWifi(ssid, pass);
                wifiManagerConnect(ssid, pass);
                LOG_INFO("WiFi updated via command: %s", ssid);
            }
        }
        String ack = protocolBuildAck("set_wifi", true, req.requestId);
        pEvent->setValue(ack.c_str());
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

void bleNotifyWeight(float weight, const String& unit, bool stable) {
    NimBLECharacteristic* pChar = bleServerGetCharWeightStream();
    if (!pChar) return;

    static uint32_t seq = 0;
    String payload = protocolBuildWeight(weight, unit, stable, seq++);

    pChar->setValue(payload.c_str());
    pChar->notify();
}
