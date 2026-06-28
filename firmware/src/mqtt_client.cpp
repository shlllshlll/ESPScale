#include "mqtt_client.h"
#include "config.h"
#include "storage.h"
#include "utils.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient sWifiClient;
static PubSubClient sMqtt(sWifiClient);
static MqttClientState sState = MqttClientState::DISCONNECTED;
static unsigned long sConnectStartMs = 0;
static unsigned long sRetryAtMs = 0;
static uint8_t sRetryCount = 0;

void mqttClientBegin() {
    sState = MqttClientState::DISCONNECTED;
    // PubSubClient buffer: default 256 bytes is fine for our JSON payloads
    sMqtt.setBufferSize(256);
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // We only publish, don't subscribe to anything for now
    String msg((const char*)payload, length);
    LOG_INFO("MQTT rx [%s]: %s", topic, msg.c_str());
}

void mqttClientTick() {
    unsigned long now = millis();

    switch (sState) {
    case MqttClientState::DISCONNECTED:
        break;

    case MqttClientState::CONNECTING: {
        sMqtt.loop();

        if (sMqtt.connected()) {
            sState = MqttClientState::CONNECTED;
            sRetryCount = 0;
            LOG_INFO("MQTT connected");
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
            sState = MqttClientState::DISCONNECTED;
            sRetryAtMs = now + MQTT_RECONNECT_BASE_MS;
            LOG_WARN("MQTT disconnected");
        }
        break;
    }

    case MqttClientState::ERROR: {
        if (now >= sRetryAtMs && sRetryCount < MQTT_MAX_RETRY) {
            sRetryCount++;
            const auto& cfg = storageGet();
            String clientId = cfg.deviceId.isEmpty() ? "espscale-" + String(random(0xffff), HEX) : cfg.deviceId;
            String host = cfg.mqttHost.isEmpty() ? "localhost" : cfg.mqttHost;
            uint16_t port = cfg.mqttPort;

            sMqtt.setServer(host.c_str(), port);
            sMqtt.setCallback(mqttCallback);
            sConnectStartMs = now;
            sState = MqttClientState::CONNECTING;

            const char* user = cfg.mqttUser.isEmpty() ? nullptr : cfg.mqttUser.c_str();
            const char* pass = cfg.mqttPass.isEmpty() ? nullptr : cfg.mqttPass.c_str();
            sMqtt.connect(clientId.c_str(), user, pass);
            LOG_INFO("MQTT connecting to %s:%d (retry %d/%d)", host.c_str(), port, sRetryCount, MQTT_MAX_RETRY);
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
