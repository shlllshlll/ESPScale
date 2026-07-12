#include "protocol.h"
#include "utils.h"

const char* commandName(Command cmd) {
    switch (cmd) {
    case Command::TARE:          return "tare";
    case Command::CALIBRATE:     return "calibrate";
    case Command::SET_MODE:      return "set_mode";
    case Command::SET_CONFIG:    return "set_config";
    case Command::SET_WIFI:      return "set_wifi";
    case Command::SET_MQTT:      return "set_mqtt";
    case Command::GET_STATUS:    return "get_status";
    case Command::REBOOT:        return "reboot";
    case Command::FACTORY_RESET: return "factory_reset";
    default:                     return "unknown";
    }
}

CmdRequest protocolParse(const String& json) {
    CmdRequest req;
    req.rawJson = json;

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        LOG_WARN("protocolParse: JSON error: %s", err.c_str());
        return req;
    }

    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "tare") == 0) req.cmd = Command::TARE;
    else if (strcmp(cmd, "calibrate") == 0) req.cmd = Command::CALIBRATE;
    else if (strcmp(cmd, "set_mode") == 0) req.cmd = Command::SET_MODE;
    else if (strcmp(cmd, "set_config") == 0) req.cmd = Command::SET_CONFIG;
    else if (strcmp(cmd, "set_wifi") == 0) req.cmd = Command::SET_WIFI;
    else if (strcmp(cmd, "set_mqtt") == 0) req.cmd = Command::SET_MQTT;
    else if (strcmp(cmd, "get_status") == 0) req.cmd = Command::GET_STATUS;
    else if (strcmp(cmd, "reboot") == 0) req.cmd = Command::REBOOT;
    else if (strcmp(cmd, "factory_reset") == 0) req.cmd = Command::FACTORY_RESET;

    req.requestId = doc["request_id"] | "";
    return req;
}

String protocolBuildAck(const String& cmdName, bool success, const String& requestId, const char* data) {
    StaticJsonDocument<512> doc;
    doc["event"] = "cmd_ack";
    doc["cmd"] = cmdName;
    doc["success"] = success;
    doc["request_id"] = requestId;
    if (data && strlen(data) > 0) {
        StaticJsonDocument<384> dataDoc;
        deserializeJson(dataDoc, data);
        doc["data"] = dataDoc.as<JsonObject>();
    }
    String out;
    serializeJson(doc, out);
    return out;
}

String protocolBuildWeight(float weight, const String& unit, bool stable, uint32_t seq) {
    StaticJsonDocument<256> doc;
    doc["weight"] = weight;
    doc["unit"] = unit;
    doc["stable"] = stable;
    doc["timestamp"] = millis();
    doc["seq"] = seq;
    String out;
    serializeJson(doc, out);
    return out;
}
