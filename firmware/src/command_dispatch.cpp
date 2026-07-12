#include "command_dispatch.h"
#include "config.h"
#include "storage.h"
#include "scale_sensor.h"
#include "state_machine.h"
#include "wifi_manager.h"
#include "utils.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static JsonObject parseParams(const String& rawJson) {
    static StaticJsonDocument<512> doc;
    doc.clear();
    deserializeJson(doc, rawJson);
    return doc["params"].as<JsonObject>();
}

String commandDispatch(const CmdRequest& req) {
    const char* cmdName = commandName(req.cmd);

    switch (req.cmd) {
    case Command::TARE: {
        scaleTare(20);
        LOG_INFO("Tare done, reading=%ld", scaleGetLastRaw());
        return protocolBuildAck("tare", true, req.requestId);
    }
    case Command::CALIBRATE: {
        JsonObject params = parseParams(req.rawJson);
        float expected = params["expected_weight"] | 100.0f;
        float newFactor = scaleCalibrate(expected);
        storageSaveCalFactor(newFactor);
        LOG_INFO("Calibrated: new factor=%.2f", newFactor);
        return protocolBuildAck("calibrate", true, req.requestId);
    }
    case Command::SET_MODE: {
        JsonObject params = parseParams(req.rawJson);
        if (!params.isNull() && params.containsKey("mode")) {
            storageSetMode(params["mode"].as<uint8_t>());
            LOG_INFO("Mode set to %u", params["mode"].as<uint8_t>());
        }
        return protocolBuildAck("set_mode", true, req.requestId);
    }
    case Command::SET_CONFIG: {
        JsonObject params = parseParams(req.rawJson);
        if (params.isNull()) {
            return protocolBuildAck("set_config", false, req.requestId);
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
        if (params.containsKey("api_key")) {
            storageSaveApiKey(params["api_key"].as<const char*>());
        }
        LOG_INFO("Config updated");
        return protocolBuildAck("set_config", true, req.requestId);
    }
    case Command::SET_WIFI: {
        JsonObject params = parseParams(req.rawJson);
        if (!params.isNull()) {
            const char* ssid = params["ssid"] | "";
            const char* pass = params["password"] | "";
            if (strlen(ssid) > 0) {
                storageSaveWifi(ssid, pass);
                wifiManagerConnect(ssid, pass);
                LOG_INFO("WiFi updated: %s", ssid);
            }
        }
        return protocolBuildAck("set_wifi", true, req.requestId);
    }
    case Command::SET_MQTT: {
        JsonObject params = parseParams(req.rawJson);
        if (params.isNull()) {
            return protocolBuildAck("set_mqtt", false, req.requestId);
        }
        storageSetMqttConfig(
            params["mqtt_host"] | "localhost",
            params["mqtt_port"] | 1883,
            params["mqtt_user"] | "",
            params["mqtt_pass"] | ""
        );
        LOG_INFO("MQTT config saved");
        return protocolBuildAck("set_mqtt", true, req.requestId);
    }
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
        String ack = protocolBuildAck("get_status", true, req.requestId, out.c_str());
        return ack;
    }
    case Command::REBOOT: {
        String ack = protocolBuildAck("reboot", true, req.requestId);
        delay(200);
        ESP.restart();
        return ack;
    }
    case Command::FACTORY_RESET: {
        String ack = protocolBuildAck("factory_reset", true, req.requestId);
        delay(200);
        storageFactoryReset();
        ESP.restart();
        return ack;
    }
    default:
        return protocolBuildAck(cmdName, false, req.requestId);
    }
}