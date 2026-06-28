#include "http_client.h"
#include "config.h"
#include "storage.h"
#include "utils.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>

static HttpClientState sState = HttpClientState::IDLE;
static WiFiClient sClient;
static String sPayload;
static String sPath = "/api/v1/data";
static unsigned long sOpStartMs = 0;
static String sResponseBuf;
static int sHttpPort = 80;
static String sHost;
static String sLastWeight;
static float sLastWeightVal = 0;
static bool sLastStable = true;

void httpClientBegin() {
    sState = HttpClientState::IDLE;
}

static bool resolveUrl() {
    const auto& cfg = storageGet();
    String url = cfg.serverUrl.isEmpty() ? DEFAULT_SERVER_URL : cfg.serverUrl;
    // Parse: http://host:port/path
    int schemeEnd = url.indexOf("://");
    if (schemeEnd < 0) {
        LOG_ERROR("HTTP: bad URL %s", url.c_str());
        return false;
    }
    String scheme = url.substring(0, schemeEnd);
    sHttpPort = (scheme == "https") ? 443 : 80;
    String hostPart = url.substring(schemeEnd + 3);
    int slashIdx = hostPart.indexOf('/');
    if (slashIdx >= 0) {
        sHost = hostPart.substring(0, slashIdx);
        sPath = hostPart.substring(slashIdx) + sPath;
    } else {
        sHost = hostPart;
    }
    // Strip port from host if present
    int colonIdx = sHost.indexOf(':');
    if (colonIdx >= 0) {
        sHttpPort = sHost.substring(colonIdx + 1).toInt();
        sHost = sHost.substring(0, colonIdx);
    }
    return true;
}

bool httpClientPostWeight(float weight, const String& unit, bool stable) {
    if (sState != HttpClientState::IDLE) return false;

    if (sHost.isEmpty() && !resolveUrl()) return false;

    const auto& cfg = storageGet();

    StaticJsonDocument<256> doc;
    doc["weight"] = weight;
    doc["unit"] = unit;
    doc["stable"] = stable;
    doc["timestamp"] = (unsigned long)(millis() / 1000);

    sPayload.clear();
    serializeJson(doc, sPayload);

    sLastWeightVal = weight;
    sLastStable = stable;

    sState = HttpClientState::SENDING;
    sOpStartMs = millis();
    return true;
}

float httpClientGetLastWeight() { return sLastWeightVal; }

bool httpClientIsIdle() { return sState == HttpClientState::IDLE; }

void httpClientTick() {
    unsigned long now = millis();

    switch (sState) {
    case HttpClientState::IDLE:
        break;

    case HttpClientState::SENDING: {
        if (sHost.isEmpty()) {
            sState = HttpClientState::ERROR;
            break;
        }
        LOG_INFO("HTTP connecting to %s:%d", sHost.c_str(), sHttpPort);
        if (!sClient.connect(sHost.c_str(), sHttpPort)) {
            LOG_WARN("HTTP connect failed");
            sState = HttpClientState::ERROR;
            sOpStartMs = now;
            break;
        }
        const auto& cfg = storageGet();

        sClient.print("POST " + sPath + " HTTP/1.1\r\n");
        sClient.print("Host: " + sHost + "\r\n");
        sClient.print("Content-Type: application/json\r\n");
        sClient.print("X-Device-ID: " + cfg.deviceId + "\r\n");
        sClient.print("X-API-Key: " + cfg.apiKey + "\r\n");
        sClient.print("Content-Length: " + String(sPayload.length()) + "\r\n");
        sClient.print("\r\n");
        sClient.print(sPayload);

        sState = HttpClientState::WAITING;
        sOpStartMs = now;
        sResponseBuf.clear();
        break;
    }

    case HttpClientState::WAITING: {
        while (sClient.available()) {
            char c = sClient.read();
            sResponseBuf += c;
        }

        if (!sClient.connected()) {
            sClient.stop();
            sState = HttpClientState::DONE;
            LOG_INFO("HTTP POST OK (%d bytes resp)", sResponseBuf.length());
        } else if (now - sOpStartMs > HTTP_TIMEOUT_MS) {
            sClient.stop();
            sState = HttpClientState::ERROR;
            LOG_WARN("HTTP response timeout");
        }
        break;
    }

    case HttpClientState::DONE:
        sState = HttpClientState::IDLE;
        break;

    case HttpClientState::ERROR:
        if (now - sOpStartMs > 2000) {  // cooldown after error
            sState = HttpClientState::IDLE;
        }
        break;
    }
}
