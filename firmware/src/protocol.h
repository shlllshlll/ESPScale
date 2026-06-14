#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class Command : uint8_t {
    NONE,
    TARE,
    CALIBRATE,
    SET_MODE,
    SET_CONFIG,
    SET_WIFI,
    SET_MQTT,
    GET_STATUS,
    REBOOT,
    FACTORY_RESET
};

struct CmdRequest {
    Command cmd = Command::NONE;
    String requestId;
    String rawJson;
};

const char* commandName(Command cmd);
CmdRequest protocolParse(const String& json);
String protocolBuildAck(const String& cmdName, bool success, const String& requestId);
String protocolBuildWeight(float weight, const String& unit, bool stable, uint32_t seq);
