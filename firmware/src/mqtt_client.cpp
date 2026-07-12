#include "mqtt_client.h"
#include "config.h"
#include "storage.h"
#include "utils.h"
#include "protocol.h"
#include "command_dispatch.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient sWifiClient;
static PubSubClient sMqtt(sWifiClient);
static MqttClientState sState = MqttClientState::DISCONNECTED;
static unsigned long sConnectStartMs = 0;
static unsigned long sRetryAtMs = 0;
static uint8_t sRetryCount = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length);

void mqttClientBegin() {
    sState = MqttClientState::DISCONNECTED;
    sMqtt.setBufferSize(512);
}

void mqttClientReset() {
    sState = MqttClientState::DISCONNECTED;
    sRetryCount = 0;
}

static void initiateConnect() {
    const auto& cfg = storageGet();
    String clientId = cfg.deviceId.isEmpty() ? "espscale-" + String(random(0xffff), HEX) : cfg.deviceId;
    String host = cfg.mqttHost.isEmpty() ? "localhost" : cfg.mqttHost;
    uint16_t port = cfg.mqttPort;

    sMqtt.setServer(host.c_str(), port);
    sMqtt.setCallback(mqttCallback);
    sConnectStartMs = millis();
    sState = MqttClientState::CONNECTING;

    // LWT: publish offline status on unexpected disconnect
    String willTopic = "espscale/" + cfg.deviceId + "/status";
    String willPayload = "{\"device_id\":\"" + cfg.deviceId + "\",\"status\":\"offline\"}";

    const char* user = cfg.mqttUser.isEmpty() ? nullptr : cfg.mqttUser.c_str();
    const char* pass = cfg.mqttPass.isEmpty() ? nullptr : cfg.mqttPass.c_str();

    sMqtt.connect(clientId.c_str(), user, pass,
                  willTopic.c_str(), 1, true,
                  willPayload.c_str());
    LOG_INFO("MQTT connecting to %s:%d (retry %d/%d)", host.c_str(), port, sRetryCount, MQTT_MAX_RETRY);
}

void mqttClientConnect() {
    if (sState == MqttClientState::DISCONNECTED) {
        initiateConnect();
    }
}

static void onConnected() {
    sRetryCount = 0;
    const auto& cfg = storageGet();

    // Subscribe to command topic
    String cmdTopic = "espscale/" + cfg.deviceId + "/cmd";
    sMqtt.subscribe(cmdTopic.c_str(), 1);
    LOG_INFO("MQTT subscribed to %s", cmdTopic.c_str());

    // Publish online status
    String statusTopic = "espscale/" + cfg.deviceId + "/status";
    String statusPayload = "{\"device_id\":\"" + cfg.deviceId + "\",\"status\":\"online\"}";
    sMqtt.publish(statusTopic.c_str(), statusPayload.c_str(), true);
    LOG_INFO("MQTT published online status");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg((const char*)payload, length);
    LOG_INFO("MQTT rx [%s]: %s", topic, msg.c_str());

    // Parse as command
    CmdRequest req = protocolParse(msg);
    if (req.cmd == Command::NONE) {
        LOG_WARN("MQTT: unknown command in %s", topic);
        return;
    }

    String ack = commandDispatch(req);

    // Publish ack to event topic
    const auto& cfg = storageGet();
    String eventTopic = "espscale/" + cfg.deviceId + "/event";
    sMqtt.publish(eventTopic.c_str(), ack.c_str());
    LOG_INFO("MQTT published ack: %s", ack.c_str());
}

void mqttClientTick() {
    unsigned long now = millis();

    switch (sState) {
    case MqttClientState::DISCONNECTED:
        // Idle until mqttClientConnect() is called by the state machine
        break;

    case MqttClientState::CONNECTING: {
        sMqtt.loop();

        if (sMqtt.connected()) {
            sState = MqttClientState::CONNECTED;
            LOG_INFO("MQTT connected");
            onConnected();
        } else if (now - sConnectStartMs > MQTT_CONNECT_TIMEOUT_MS) {
            sState = MqttClientState::ERROR;
            sRetryAtMs = now + (MQTT_RECONNECT_BASE_MS * (sRetryCount + 1));
            LOG_WARN("MQTT connect timeout");
        }
        break;
    }

    case MqttClientState::CONNECTED: {
        sMqtt.loop();
        if (!sMqtt.connected()) {
            sState = MqttClientState::ERROR;
            sRetryAtMs = now + MQTT_RECONNECT_BASE_MS;
            LOG_WARN("MQTT disconnected, will retry");
        }
        break;
    }

    case MqttClientState::ERROR: {
        if (now >= sRetryAtMs && sRetryCount < MQTT_MAX_RETRY) {
            sRetryCount++;
            initiateConnect();
        }
        break;
    }
    }
}

MqttClientState mqttClientGetState() { return sState; }

bool mqttClientPublishWeight(float weight, const String& unit, bool stable) {
    if (sState != MqttClientState::CONNECTED) return false;

    const auto& cfg = storageGet();
    String topic = "espscale/" + cfg.deviceId + "/weight";

    StaticJsonDocument<256> doc;
    doc["weight"] = weight;
    doc["unit"] = unit;
    doc["stable"] = stable;
    doc["timestamp"] = (unsigned long)(millis() / 1000);

    String payload;
    serializeJson(doc, payload);

    bool ok = sMqtt.publish(topic.c_str(), payload.c_str());
    if (ok) {
        LOG_INFO("MQTT published: %s", payload.c_str());
    } else {
        LOG_WARN("MQTT publish failed");
    }
    return ok;
}