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

void storageFactoryReset() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.clear();
    p.end();
}
