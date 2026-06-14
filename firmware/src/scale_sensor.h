#pragma once
#include <cstdint>

void scaleBegin(float calFactor);
bool scaleIsReady();
float scaleReadWeight(uint8_t samples = 5);
float scaleGetLastWeight();
long scaleGetLastRaw();
void scaleTare(uint8_t samples = 20);
float scaleCalibrate(float expectedWeight);
