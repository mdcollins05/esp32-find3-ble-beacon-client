/*
  This file is part of esp-find3-client by Sylwester aka DatanoiseTV.
  The original source can be found at https://github.com/DatanoiseTV/esp-find3-client.

  26/04/2020: Adjustements by Wizardry and Steamworks.

  esp-find3-client is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  esp-find3-client is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with esp-find3-client.  If not, see <http://www.gnu.org/licenses/>.
*/

///////////////////////////////////////////////////////////////////////////
//                             CONFIGURATION                             //
///////////////////////////////////////////////////////////////////////////

// Set to the WiFi AP name.
#define WIFI_SSID ""
// Set to the WiFi AP password.
#define WIFI_PSK ""

// Family name.
#define FAMILY_NAME ""

#define DEVICE_NAME ""

// BLE requires large app partition or the sketch will not fit.
// Please pick either of:
// * Tools -> Partition scheme -> Minimal SPIFFS (1.9MB APP / 190KB SPIFFS)
// * Tools -> Partition scheme -> Huge App (3MB No OTA / 1MB SPIFFS)
#define BLE_SCANTIME 10

// Official server: cloud.internalpositioning.com
#define FIND_HOST "cloud.internalpositioning.com"
// Official port: 443 and SSL set to 1
#define FIND_PORT 443
// Whether to use SSL for the HTTP connection.
// Set to 1 for official cloud server.
#define USE_HTTP_SSL 1
// Timeout connecting to find3 server expressed in milliseconds.
#define HTTP_TIMEOUT 5000

// Set to 1 to enable. Used for verbose debugging.
#define DEBUG 0

// Restart every 7 days?
#define AUTO_RESTART 0

// Set to 1 to enable LED blinking
#define BLINK_LED 0
// Change the LED pin if needed for your board
#define LED_PIN 2

///////////////////////////////////////////////////////////////////////////
//                              INTERNALS                                //
///////////////////////////////////////////////////////////////////////////

#include <WiFiClientSecure.h>
#include <WiFi.h>

#define GET_CHIP_ID() String(((uint16_t)(ESP.getEfuseMac() >> 32)), HEX)

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEBeacon.h>

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
  }
};

void blinkLED(int duration_on, int duration_off, int count) {
  if (BLINK_LED == 1) {
    for (int i = 0; i < count; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(duration_on);
      digitalWrite(LED_PIN, LOW);
      delay(duration_off);
    }
  } else {
    delay(duration_off);
  }
}

void scan(void) {
  blinkLED(800, 200, 1);

  String request;

  StaticJsonDocument<256> jsonBuffer;
  JsonObject root = jsonBuffer.to<JsonObject>();

  if (DEVICE_NAME == "") {
    root["d"] = "esp-" + GET_CHIP_ID();
  } else {
    root["d"] = DEVICE_NAME;
  }
  
  root["f"] = FAMILY_NAME;
  JsonObject data = root.createNestedObject("s");

  Serial.println("[ INFO ]\tBLE scan starting..");
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();  // create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  // active scan uses more power, but get results faster
  BLEScanResults foundDevices = pBLEScan->start(BLE_SCANTIME);

  Serial.print("[ INFO ]\t");
  Serial.print(foundDevices.getCount());
  Serial.println(" BLE devices found.");

  JsonObject bt_network = data.createNestedObject("ble_beacon");
  for (int i = 0; i < foundDevices.getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    std::string mac = device.getAddress().toString();
    if (device.haveManufacturerData() == true) {
      std::string strManufacturerData = device.getManufacturerData();
      uint8_t cManufacturerData[100];
      strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length(), 0);
      if (strManufacturerData.length() == 25 && cManufacturerData[0] == 0x4C && cManufacturerData[1] == 0x00) {
        BLEBeacon oBeacon = BLEBeacon();
        oBeacon.setData(strManufacturerData);
        bt_network[(String)oBeacon.getProximityUUID().toString().c_str()] = (int)foundDevices.getDevice(i).getRSSI();
      }
    }
  }

  serializeJson(root, request);

#if (DEBUG == 1)
  Serial.println("[ DEBUG ]\t" + request);
#endif

#if (USE_HTTP_SSL == 1)
  WiFiClientSecure client;
#else
  WiFiClient client;
#endif
  if (!client.connect(FIND_HOST, FIND_PORT)) {
    Serial.println("[ WARN ]\tConnection to server failed...");
    return;
  }

  // We now create a URI for the request
  String url = "/passive";

  Serial.print("[ INFO ]\tRequesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("POST ") + url + " HTTP/1.1\r\n" + "Host: " + FIND_HOST + "\r\n" + "Content-Type: application/json\r\n" + "Content-Length: " + request.length() + "\r\n\r\n" + request + "\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > HTTP_TIMEOUT) {
      Serial.println("[ ERROR ]\tHTTP Client Timeout !");
      blinkLED(800, 200, 2);
      client.stop();
      return;
    }
  }

  // Check HTTP status
  char status[60] = { 0 };
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Serial.print(F("[ ERROR ]\tUnexpected Response: "));
    Serial.println(status);
    blinkLED(800, 200, 2);
    return;
  } else {
    Serial.println(F("[ INFO ]\tGot a 200 OK."));
  }

  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("[ ERROR ]\t Invalid Response"));
    blinkLED(800, 200, 2);
    return;
  } else {
    Serial.println("[ INFO ]\tLooks like a valid response.");
  }

  Serial.println("[ INFO ]\tClosing connection.");
  Serial.println("=============================================================");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);

  Serial.println("Find3 ESP32 client by DatanoiseTV (Modified by mdcollins05) (BLE support)");

  Serial.print("[ INFO ]\tChipID is: ");
  Serial.println("esp-" + GET_CHIP_ID());

  WiFi.setHostname(DEVICE_NAME);

  int attemptsCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (attemptsCount == 0) {
      Serial.println("[ INFO ]\tConnecting to WiFi..");
      blinkLED(200, 200, 1);
    } else {
      Serial.println("[ WARN ]\tWiFi not connected, retrying...");
      blinkLED(200, 200, 3);
      WiFi.disconnect();
      delay(5000);
    }
    
    WiFi.begin(WIFI_SSID, WIFI_PSK);
    delay(5000);
    attemptsCount++;
    if (attemptsCount >= 10) {
      ESP.restart();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[ INFO ]\tWiFi connection established.");
    Serial.print("[ INFO ]\tIP address: ");
    Serial.println(WiFi.localIP());
    blinkLED(200, 200, 2);
    delay(1000);
  }
}

void loop() {
  int attemptsCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ WARN ]\tWiFi not connected, retrying...");
    blinkLED(200, 200, 3);
    WiFi.disconnect();
    delay(5000);
    WiFi.begin(WIFI_SSID, WIFI_PSK);
    delay(5000);
    attemptsCount++;
    if (attemptsCount >= 10) {
      ESP.restart();
    }
  }

  scan();

  if (AUTO_RESTART == 1) {
    if ( millis()  >= 604800000UL ) {
      ESP.restart();
    }
  }
}
