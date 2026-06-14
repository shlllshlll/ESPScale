#include "wifi_manager.h"
#include "config.h"
#include "storage.h"
#include "utils.h"
#include <WiFi.h>

static WifiState sWifiState = WifiState::DISCONNECTED;
static String sPendingSsid;
static String sPendingPass;
static unsigned long sConnectStartMs = 0;
static unsigned long sRetryAtMs = 0;
static uint8_t sRetryCount = 0;

void wifiManagerBegin() {
    WiFi.mode(WIFI_STA);
    const auto& cfg = storageGet();
    if (!cfg.wifiSsid.isEmpty()) {
        wifiManagerConnect(cfg.wifiSsid, cfg.wifiPass);
    }
}

void wifiManagerConnect(const String& ssid, const String& pass) {
    sPendingSsid = ssid;
    sPendingPass = pass;
    sRetryCount = 0;
    sWifiState = WifiState::CONNECTING;
    sConnectStartMs = millis();
    WiFi.disconnect();
    delay(100);
    WiFi.begin(ssid.c_str(), pass.c_str());
    LOG_INFO("WiFi connecting to %s...", ssid.c_str());
}

void wifiManagerTick() {
    unsigned long now = millis();

    if (sWifiState == WifiState::CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            sWifiState = WifiState::CONNECTED;
            sRetryCount = 0;
            LOG_INFO("WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
            storageSaveWifi(sPendingSsid, sPendingPass);
        } else if (now - sConnectStartMs > WIFI_TIMEOUT_MS) {
            sWifiState = WifiState::ERROR;
            sRetryAtMs = now + (WIFI_RECONNECT_BASE_MS * (sRetryCount + 1));
            LOG_WARN("WiFi connect timeout (%lus)", WIFI_TIMEOUT_MS / 1000);
        }
        return;
    }

    if (sWifiState == WifiState::ERROR) {
        if (now >= sRetryAtMs && sRetryCount < WIFI_MAX_RETRY) {
            sRetryCount++;
            sWifiState = WifiState::CONNECTING;
            sConnectStartMs = now;
            WiFi.disconnect();
            delay(100);
            WiFi.begin(sPendingSsid.c_str(), sPendingPass.c_str());
            LOG_INFO("WiFi retry %d/%d", sRetryCount, WIFI_MAX_RETRY);
        }
        return;
    }

    if (sWifiState == WifiState::CONNECTED && WiFi.status() != WL_CONNECTED) {
        sWifiState = WifiState::DISCONNECTED;
        LOG_WARN("WiFi disconnected");
    }
}

WifiState wifiManagerGetState() { return sWifiState; }

int wifiManagerGetRssi() { return WiFi.RSSI(); }
