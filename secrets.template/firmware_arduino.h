// Firmware secrets (Arduino) — compile-time injection
// Copy to ../secrets/firmware_arduino.h and fill in real values.
// This file is included via platformio.ini build_flags: -include secrets/firmware_arduino.h
// If the file does not exist, firmware uses empty strings (no MQTT auth).

#pragma once

// MQTT broker credentials for the device.
// Must match MQTT_USER/MQTT_PASS in server.env and mosquitto.passwd.
#define DEFAULT_MQTT_USER "espscale"
#define DEFAULT_MQTT_PASS "change-me-to-a-secure-mqtt-password"