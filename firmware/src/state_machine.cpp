#include "state_machine.h"
#include "config.h"
#include "storage.h"
#include "scale_sensor.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "utils.h"

static DeviceState sState = DeviceState::PROVISIONING;
static int sHx711FailCount = 0;
static unsigned long sLastUploadMs = 0;
static bool sMqttStarted = false;

static const char* stateName(DeviceState s) {
    switch (s) {
    case DeviceState::PROVISIONING:   return "PROVISIONING";
    case DeviceState::CONNECTING_WIFI: return "CONNECTING_WIFI";
    case DeviceState::CONNECTING_MQTT: return "CONNECTING_MQTT";
    case DeviceState::RUNNING:        return "RUNNING";
    case DeviceState::ERROR_WIFI:     return "ERROR_WIFI";
    case DeviceState::ERROR_MQTT:     return "ERROR_MQTT";
    case DeviceState::ERROR_HX711:    return "ERROR_HX711";
    case DeviceState::FACTORY_RESET:  return "FACTORY_RESET";
    default:                          return "UNKNOWN";
    }
}

void stateMachineBegin() {
    if (storageHasWifi()) {
        stateMachineSetState(DeviceState::CONNECTING_WIFI);
    } else {
        stateMachineSetState(DeviceState::PROVISIONING);
    }
}

void stateMachineSetState(DeviceState newState) {
    if (sState == newState) return;
    LOG_INFO("State: %s -> %s", stateName(sState), stateName(newState));
    sState = newState;
}

void stateMachineTick(unsigned long nowMs) {
    const auto& cfg = storageGet();

    switch (sState) {
    case DeviceState::PROVISIONING:
        if (storageHasWifi()) {
            stateMachineSetState(DeviceState::CONNECTING_WIFI);
        }
        break;

    case DeviceState::CONNECTING_WIFI: {
        WifiState ws = wifiManagerGetState();
        if (ws == WifiState::CONNECTED) {
            sMqttStarted = false;
            if (cfg.mode == MODE_MQTT) {
                mqttClientReset();
                mqttClientConnect();
                sMqttStarted = true;
                stateMachineSetState(DeviceState::CONNECTING_MQTT);
            } else {
                stateMachineSetState(DeviceState::RUNNING);
            }
        } else if (ws == WifiState::ERROR) {
            stateMachineSetState(DeviceState::ERROR_WIFI);
        }
        break;
    }

    case DeviceState::CONNECTING_MQTT: {
        if (!sMqttStarted) {
            mqttClientConnect();
            sMqttStarted = true;
        }
        MqttClientState ms = mqttClientGetState();
        if (ms == MqttClientState::CONNECTED) {
            stateMachineSetState(DeviceState::RUNNING);
        } else if (ms == MqttClientState::ERROR) {
            stateMachineSetState(DeviceState::ERROR_MQTT);
        }
        break;
    }

    case DeviceState::RUNNING: {
        if (wifiManagerGetState() != WifiState::CONNECTED) {
            sMqttStarted = false;
            stateMachineSetState(DeviceState::ERROR_WIFI);
            break;
        }
        if (!scaleIsReady()) {
            sHx711FailCount++;
            if (sHx711FailCount > 10) {
                stateMachineSetState(DeviceState::ERROR_HX711);
            }
        } else {
            sHx711FailCount = 0;
        }

        // Route upload based on mode
        if (cfg.mode == MODE_HTTP_DIRECT) {
            // HTTP upload handled in main loop via interval timer
        } else if (cfg.mode == MODE_MQTT) {
            // MQTT tick handled in main loop
        }
        // MODE_BLE_ONLY: no upload, just BLE streaming
        break;
    }

    case DeviceState::ERROR_WIFI:
        if (wifiManagerGetState() == WifiState::CONNECTED) {
            sMqttStarted = false;
            if (cfg.mode == MODE_MQTT) {
                mqttClientReset();
                mqttClientConnect();
                sMqttStarted = true;
                stateMachineSetState(DeviceState::CONNECTING_MQTT);
            } else {
                stateMachineSetState(DeviceState::RUNNING);
            }
        }
        break;

    case DeviceState::ERROR_MQTT:
        if (wifiManagerGetState() != WifiState::CONNECTED) {
            sMqttStarted = false;
            stateMachineSetState(DeviceState::ERROR_WIFI);
            break;
        }
        if (mqttClientGetState() == MqttClientState::CONNECTED) {
            stateMachineSetState(DeviceState::RUNNING);
        } else if (mqttClientGetState() == MqttClientState::DISCONNECTED) {
            mqttClientConnect();
            sMqttStarted = true;
            stateMachineSetState(DeviceState::CONNECTING_MQTT);
        }
        break;

    case DeviceState::ERROR_HX711:
        if (scaleIsReady()) {
            sHx711FailCount = 0;
            stateMachineSetState(DeviceState::RUNNING);
        }
        break;

    default:
        break;
    }

    // --- LED control (active LOW) ---
    bool ledOn = false;

    switch (sState) {
    case DeviceState::PROVISIONING:
        ledOn = (nowMs % 2200) < 200;
        break;
    case DeviceState::CONNECTING_WIFI:
    case DeviceState::CONNECTING_MQTT:
        ledOn = (nowMs % 600) < 100;
        break;
    case DeviceState::RUNNING:
        ledOn = true;
        break;
    case DeviceState::ERROR_WIFI: {
        unsigned long cycle = nowMs % 1600;
        ledOn = (cycle < 200) || (cycle >= 400 && cycle < 600) || (cycle >= 800 && cycle < 1000);
        break;
    }
    case DeviceState::ERROR_HX711: {
        unsigned long cycle = nowMs % 3500;
        ledOn = (cycle < 500) || (cycle >= 1000 && cycle < 1500) || (cycle >= 2000 && cycle < 2500);
        break;
    }
    default:
        ledOn = false;
        break;
    }

    digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
}

DeviceState stateMachineGetState() { return sState; }

const char* stateMachineGetStateName() { return stateName(sState); }
