#pragma once
#include <NimBLEDevice.h>

void bleOnDeviceInfoRead(NimBLECharacteristic* pChar);
void bleOnWifiCredsWrite(NimBLECharacteristic* pChar);
void bleOnNetworkStatusRead(NimBLECharacteristic* pChar);
void bleOnCommandWrite(NimBLECharacteristic* pChar);
void bleOnConnect(NimBLEServer* pServer);
void bleOnDisconnect(NimBLEServer* pServer, int reason);
void bleNetworkStatusNotify();
void bleNotifyWeight(float weight, const String& unit, bool stable);
