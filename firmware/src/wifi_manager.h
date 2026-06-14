#pragma once
#include <Arduino.h>

enum class WifiState { DISCONNECTED, CONNECTING, CONNECTED, ERROR };

void wifiManagerBegin();
void wifiManagerTick();
WifiState wifiManagerGetState();
int wifiManagerGetRssi();
void wifiManagerConnect(const String& ssid, const String& pass);
