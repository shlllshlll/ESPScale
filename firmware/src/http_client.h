#pragma once
#include <Arduino.h>

enum class HttpClientState { IDLE, SENDING, WAITING, DONE, ERROR };

void httpClientBegin();
void httpClientTick();
bool httpClientIsIdle();
bool httpClientPostWeight(float weight, const String& unit, bool stable);
float httpClientGetLastWeight();  // For state machine to check if data was sent
