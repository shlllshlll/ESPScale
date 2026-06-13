#include <string>
#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <HX711.h>

// Bluetooth
NimBLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;

// UUID（可自定义，但必须唯一）
#define SERVICE_UUID "F3860834-DD6C-4ED8-9555-3BE20F962A74"
#define CHARACTERISTIC_UUID "1B796AC2-C6C4-4AC1-B586-7FB2318549D8"

// wifi
const char *WIFI_SSID = "shlll";
const char *WIFI_PASSWORD = "shihaolei";

// define LED according to pin diagram
const int LED = 8;

// HX711 wiring: GPIO4 -> SCK, GPIO3 -> DOUT (data)
const int HX711_SCK_PIN = 3;
const int HX711_DOUT_PIN = 4;
HX711 scale;

// Update this value after calibration with known weights.
// set_scale uses division: output = raw / factor, so larger factor = smaller reading.
// Correct formula: new = current * (current_reading / actual)
// 445.77 * (546.2 / 612.4) = 397.6
float hx711CalFactor = 397.6f;
const char *weightUnit = "g";

class ServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
  {
    (void)pServer;
    (void)connInfo;
    deviceConnected = true;
    Serial.println("Bluetooth device connected");
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
  {
    (void)connInfo;
    deviceConnected = false;
    Serial.print("Bluetooth device disconnected, reason=");
    Serial.println(reason);
    NimBLEDevice::startAdvertising();
  }
};

class CharCallbacks : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
  {
    (void)connInfo;
    std::string value = pCharacteristic->getValue();
    Serial.print("Bluetooth Recv: ");
    Serial.println(value.c_str());

    // 示例：收到 ON/OFF 控制LED
    if (value == "ON")
      digitalWrite(LED, LOW);
    if (value == "OFF")
      digitalWrite(LED, HIGH);
  }
};

void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting WiFi");

  const unsigned long wifiStart = millis();
  const unsigned long wifiTimeoutMs = 15000;

  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - wifiStart > wifiTimeoutMs)
    {
      Serial.println();
      Serial.println("WiFi connect timeout, continue without WiFi");
      return;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

void setupBLE()
{
  NimBLEDevice::init("ESP32C3-BLE");

  NimBLEServer *pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);

  pCharacteristic->setCallbacks(new CharCallbacks());
  pCharacteristic->setValue("Hello BLE");

  pServer->start();

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE Server已启动");
}

void setupScale()
{
  pinMode(HX711_DOUT_PIN, INPUT);
  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  scale.set_scale(hx711CalFactor);

  if (!scale.wait_ready_timeout(3000))
  {
    Serial.println("HX711 not found or not ready");
    Serial.print("HX711 DOUT level=");
    Serial.println(digitalRead(HX711_DOUT_PIN));
    return;
  }

  Serial.println("HX711 ready, tare...");
  scale.tare(20);
  Serial.println("HX711 tare done");
}

void setup()
{
  Serial.begin(115200);
  const unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 4000)
  {
    delay(10);
  }

  Serial.println("Booting ESP32-C3...");

  // initialize digital pin LED as an output
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  connectWiFi();
  setupBLE();
  setupScale();

  Serial.println("Setup done");
}

// unsigned long prevLedMillis = 0; // will store last time LED was updated
// const long ledInterval = 2000;   // interval at which to blink (milliseconds)
// uint8_t led_status = LOW;

unsigned long prevSerialMillis = 0; // will store last time Serial was updated
const long serialInterval = 2000;   // interval at which to print to Serial (milliseconds)

unsigned long prevNotifyMillis = 0; // will store last time notification was sent
const long notifyInterval = 1000;   // interval at which to send notifications (milliseconds)

unsigned long prevWeightMillis = 0;
const long weightInterval = 400;
static int hx711NotReadyCount = 0;
const int hx711NotReadyWarnThreshold = 5;

void loop()
{
  unsigned long currentMillis = millis();

  // if (currentMillis - prevLedMillis >= ledInterval) {
  //   // save the last time you blinked the LED
  //   prevLedMillis = currentMillis;
  //   digitalWrite(LED, led_status);   // turn the LED on or off
  //   led_status = (led_status == LOW) ? HIGH : LOW; // toggle the LED
  // }

  // if (currentMillis - prevSerialMillis >= serialInterval) {
  //   // save the last time you printed to Serial
  //   prevSerialMillis = currentMillis;
  //   Serial.println("Hello, ESP32!\n");
  // }

  if (currentMillis - prevSerialMillis >= serialInterval)
  {
    prevSerialMillis = currentMillis;
    Serial.print("heartbeat, connected=");
    Serial.println(deviceConnected ? "true" : "false");
  }

  if (deviceConnected && currentMillis - prevNotifyMillis >= notifyInterval)
  {
    static int counter = 0;
    prevNotifyMillis = currentMillis;
    String msg = "CNT: " + String(counter++);

    pCharacteristic->setValue(msg.c_str());
    pCharacteristic->notify(); // 推送数据给手机
    Serial.print("notify: ");
    Serial.println(msg);
  }

  if (currentMillis - prevWeightMillis >= weightInterval)
  {
    prevWeightMillis = currentMillis;

    if (scale.wait_ready_timeout(80))
    {
      hx711NotReadyCount = 0;
      float weight = scale.get_units(5);
      Serial.print("weight(");
      Serial.print(weightUnit);
      Serial.print("): ");
      Serial.println(weight, 2);
    }
    else
    {
      hx711NotReadyCount++;
      if (hx711NotReadyCount >= hx711NotReadyWarnThreshold)
      {
        hx711NotReadyCount = 0;
        Serial.print("HX711 not ready x5, DOUT level=");
        Serial.println(digitalRead(HX711_DOUT_PIN));
      }
    }
  }
}