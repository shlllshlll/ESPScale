#include "storage.h"
#include "config.h"
#include "utils.h"
#include <Preferences.h>

static StoredConfig sConfig;

void storageBegin() {
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    sConfig.wifiSsid = p.getString("wifi_ssid", "");
    sConfig.wifiPass = p.getString("wifi_pass", "");
    sConfig.mqttHost = p.getString("mqtt_host", "");
    sConfig.mqttPort = p.getUShort("mqtt_port", 1883);
    sConfig.mqttUser = p.getString("mqtt_user", "");
    sConfig.mqttPass = p.getString("mqtt_pass", "");
    sConfig.mqttTopic = p.getString("mqtt_topic", "espscale");
    sConfig.serverUrl = p.getString("server_url", "");
    sConfig.deviceId = p.getString("device_id", "");
    sConfig.deviceName = p.getString("device_name", "ESPScale");
    sConfig.calFactor = p.getFloat("cal_factor", 397.6f);
    sConfig.unit = p.getString("unit", "g");
    sConfig.uploadIntervalMs = p.getUInt("upload_ms", 5000);
    sConfig.mode = p.getUChar("mode", 0);
    sConfig.apiKey = p.getString("api_key", "");
    sConfig.cfgVersion = p.getUChar("cfg_version", 1);
    p.end();

    if (sConfig.deviceId.isEmpty()) {
        sConfig.deviceId = generateDeviceId();
        p.begin(NVS_NAMESPACE, false);
        p.putString("device_id", sConfig.deviceId);
        p.end();
    }

    LOG_INFO("Storage loaded, device_id=%s", sConfig.deviceId.c_str());
}

const StoredConfig& storageGet() { return sConfig; }

bool storageHasWifi() { return !sConfig.wifiSsid.isEmpty(); }

void storageSaveWifi(const String& ssid, const String& pass) {
    sConfig.wifiSsid = ssid;
    sConfig.wifiPass = pass;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putString("wifi_ssid", ssid);
    p.putString("wifi_pass", pass);
    p.end();
}

void storageSaveCalFactor(float factor) {
    sConfig.calFactor = factor;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putFloat("cal_factor", factor);
    p.end();
}

void storageSaveUnit(const String& unit) {
    sConfig.unit = unit;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putString("unit", unit);
    p.end();
}

void storageSaveUploadInterval(uint32_t ms) {
    sConfig.uploadIntervalMs = ms;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUInt("upload_ms", ms);
    p.end();
}

void storageSaveConfig(const StoredConfig& cfg) {
    sConfig = cfg;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putString("wifi_ssid", cfg.wifiSsid);
    p.putString("wifi_pass", cfg.wifiPass);
    p.putString("mqtt_host", cfg.mqttHost);
    p.putUShort("mqtt_port", cfg.mqttPort);
    p.putString("mqtt_user", cfg.mqttUser);
    p.putString("mqtt_pass", cfg.mqttPass);
    p.putString("mqtt_topic", cfg.mqttTopic);
    p.putString("server_url", cfg.serverUrl);
    p.putUChar("mode", cfg.mode);
    p.putUInt("upload_ms", cfg.uploadIntervalMs);
    p.putString("api_key", cfg.apiKey);
    p.end();
}

void storageSetMode(uint8_t mode) {
    sConfig.mode = mode;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar("mode", mode);
    p.end();
}

void storageSetServerUrl(const String& url) {
    sConfig.serverUrl = url;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putString("server_url", url);
    p.end();
}

void storageSetMqttConfig(const String& host, uint16_t port, const String& user, const String& pass) {
    sConfig.mqttHost = host;
    sConfig.mqttPort = port;
    sConfig.mqttUser = user;
    sConfig.mqttPass = pass;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putString("mqtt_host", host);
    p.putUShort("mqtt_port", port);
    p.putString("mqtt_user", user);
    p.putString("mqtt_pass", pass);
    p.end();
}

void storageFactoryReset() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.clear();
    p.end();
}
