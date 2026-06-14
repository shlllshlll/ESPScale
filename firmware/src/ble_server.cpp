#include "ble_server.h"
#include "ble_protocol.h"
#include "config.h"
#include "storage.h"
#include "utils.h"

static NimBLEServer* pServer = nullptr;
static NimBLEAdvertising* pAdvertising = nullptr;

static NimBLECharacteristic* pCharDeviceInfo = nullptr;
static NimBLECharacteristic* pCharWifiCreds = nullptr;
static NimBLECharacteristic* pCharNetworkStatus = nullptr;
static NimBLECharacteristic* pCharScaleSettings = nullptr;
static NimBLECharacteristic* pCharWeightStream = nullptr;
static NimBLECharacteristic* pCharCommand = nullptr;
static NimBLECharacteristic* pCharEvent = nullptr;

static bool sConnected = false;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        (void)connInfo;
        sConnected = true;
        bleOnConnect(pServer);
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        (void)connInfo;
        sConnected = false;
        bleOnDisconnect(pServer, reason);
        pAdvertising->start();
    }
};

class DeviceInfoCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        (void)connInfo;
        bleOnDeviceInfoRead(pChar);
    }
};

class WifiCredsCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        (void)connInfo;
        bleOnWifiCredsWrite(pChar);
    }
};

class NetworkStatusCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        (void)connInfo;
        bleOnNetworkStatusRead(pChar);
    }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        (void)connInfo;
        bleOnCommandWrite(pChar);
    }
};

void bleServerBegin() {
    const auto& cfg = storageGet();
    String suffix = cfg.deviceId.substring(max(0, (int)cfg.deviceId.length() - 4));
    String devName = String(DEVICE_NAME_PREFIX) + suffix;
    NimBLEDevice::init(devName.c_str());

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    pCharDeviceInfo = pService->createCharacteristic(
        CHAR_DEVICE_INFO_UUID, NIMBLE_PROPERTY::READ);
    pCharDeviceInfo->setCallbacks(new DeviceInfoCallbacks());

    pCharWifiCreds = pService->createCharacteristic(
        CHAR_WIFI_CREDS_UUID, NIMBLE_PROPERTY::WRITE);
    pCharWifiCreds->setCallbacks(new WifiCredsCallbacks());

    pCharNetworkStatus = pService->createCharacteristic(
        CHAR_NETWORK_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    pCharNetworkStatus->setCallbacks(new NetworkStatusCallbacks());

    pCharScaleSettings = pService->createCharacteristic(
        CHAR_SCALE_SETTINGS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);

    pCharWeightStream = pService->createCharacteristic(
        CHAR_WEIGHT_STREAM_UUID, NIMBLE_PROPERTY::NOTIFY);

    pCharCommand = pService->createCharacteristic(
        CHAR_COMMAND_UUID, NIMBLE_PROPERTY::WRITE);
    pCharCommand->setCallbacks(new CommandCallbacks());

    pCharEvent = pService->createCharacteristic(
        CHAR_EVENT_UUID, NIMBLE_PROPERTY::NOTIFY);

    pService->start();

    pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();

    LOG_INFO("BLE server started: %s", devName.c_str());
}

void bleServerTick() {
    if (!sConnected && pAdvertising && !pAdvertising->isAdvertising()) {
        pAdvertising->start();
    }
}

bool bleServerIsConnected() { return sConnected; }

NimBLECharacteristic* bleServerGetCharDeviceInfo()    { return pCharDeviceInfo; }
NimBLECharacteristic* bleServerGetCharWifiCreds()     { return pCharWifiCreds; }
NimBLECharacteristic* bleServerGetCharNetworkStatus()  { return pCharNetworkStatus; }
NimBLECharacteristic* bleServerGetCharScaleSettings()  { return pCharScaleSettings; }
NimBLECharacteristic* bleServerGetCharWeightStream()   { return pCharWeightStream; }
NimBLECharacteristic* bleServerGetCharCommand()        { return pCharCommand; }
NimBLECharacteristic* bleServerGetCharEvent()          { return pCharEvent; }
