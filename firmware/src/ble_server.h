#pragma once
#include <NimBLEDevice.h>

void bleServerBegin();
void bleServerTick();
bool bleServerIsConnected();

NimBLECharacteristic* bleServerGetCharDeviceInfo();
NimBLECharacteristic* bleServerGetCharWifiCreds();
NimBLECharacteristic* bleServerGetCharNetworkStatus();
NimBLECharacteristic* bleServerGetCharScaleSettings();
NimBLECharacteristic* bleServerGetCharWeightStream();
NimBLECharacteristic* bleServerGetCharCommand();
NimBLECharacteristic* bleServerGetCharEvent();
