#pragma once
#include <Arduino.h>

enum class MqttClientState { DISCONNECTED, CONNECTING, CONNECTED, ERROR };

void mqttClientBegin();
void mqttClientTick();
MqttClientState mqttClientGetState();
bool mqttClientPublishWeight(float weight, const String& unit, bool stable);
