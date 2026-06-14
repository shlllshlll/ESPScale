#include "scale_sensor.h"
#include "config.h"
#include "storage.h"
#include "utils.h"
#include <HX711.h>

static HX711 scale;
static bool ready = false;
static float lastWeight = 0.0f;

void scaleBegin(float calFactor) {
    pinMode(HX711_DOUT_PIN, INPUT);
    scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);

    float factor = calFactor > 0.0f ? calFactor : storageGet().calFactor;
    scale.set_scale(factor);

    if (scale.wait_ready_timeout(3000)) {
        ready = true;
        scale.tare(20);
        LOG_INFO("HX711 ready, calFactor=%.2f", scale.get_scale());
    } else {
        ready = false;
        LOG_ERROR("HX711 not found, DOUT=%d", digitalRead(HX711_DOUT_PIN));
    }
}

bool scaleIsReady() { return ready; }

float scaleReadWeight(uint8_t samples) {
    if (!ready) return lastWeight;
    if (scale.wait_ready_timeout(HX711_READY_TIMEOUT_MS)) {
        lastWeight = scale.get_units(samples);
    }
    return lastWeight;
}

float scaleGetLastWeight() { return lastWeight; }

long scaleGetLastRaw() { return scale.read(); }

void scaleTare(uint8_t samples) {
    if (!ready) return;
    scale.tare(samples);
    LOG_INFO("HX711 tared");
}

float scaleCalibrate(float expectedWeight) {
    if (!ready) return storageGet().calFactor;
    scale.tare(20);
    delay(500);
    long raw = scale.read_average(20);
    float newFactor = raw / expectedWeight;
    storageSaveCalFactor(newFactor);
    scale.set_scale(newFactor);
    LOG_INFO("Calibrated: raw=%ld expected=%.1f newFactor=%.2f", raw, expectedWeight, newFactor);
    return newFactor;
}
