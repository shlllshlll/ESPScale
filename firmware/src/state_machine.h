#pragma once
#include <cstdint>

enum class DeviceState : uint8_t {
    PROVISIONING,
    CONNECTING_WIFI,
    CONNECTING_MQTT,
    RUNNING,
    ERROR_WIFI,
    ERROR_MQTT,
    ERROR_HX711,
    FACTORY_RESET
};

void stateMachineBegin();
void stateMachineTick(unsigned long nowMs);
DeviceState stateMachineGetState();
const char* stateMachineGetStateName();
void stateMachineSetState(DeviceState newState);
