#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "secrets.h"

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Servo and LED settings
const int ledPin = 14;
const int servoPin = 18;
Servo servo;
int servoPos = 0;

// API settings for FNG
const char* fngHost = "api.alternative.me";
const String fngPath = "/fng/";

// API settings for Bitcoin
const char* bitcoinHost = "api.coindesk.com";
const String bitcoinPath = "/v1/bpi/currentprice/BTC.json";
const String historyPath = "/v1/bpi/historical/close.json";

// Timing intervals (in milliseconds)
unsigned long previousFNGMillis = 0;
unsigned long fngInterval = 60000; // 1 minute

unsigned long previousBitcoinMillis = 0;
unsigned long bitcoinInterval = 60000; // 1 minute

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("Connecting to WiFi...");
  display.display();

  // Connect to WiFi
  WiFi.begin(SECRET_SSID, SECRET_WIFI_PASSWORD);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }
  Serial.println();
  Serial.println("WiFi connected");

  display.println("\nConnected to: ");
  display.print(SECRET_SSID);
  display.display();
  delay(1500);
  display.clearDisplay();
  display.display();

  // Initialize Servo
  servo.attach(servoPin, 500, 2400); // Attach servo with min and max pulse widths
  servo.write(servoPos);

  // Initialize LED
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // Turn LED on initially

  // Initial fetches
  fetchFNGData();
  fetchBitcoinData();
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousFNGMillis >= fngInterval) {
    previousFNGMillis = currentMillis;
    fetchFNGData();
  }

  if (currentMillis - previousBitcoinMillis >= bitcoinInterval) {
    previousBitcoinMillis = currentMillis;
    fetchBitcoinData();
  }

  // Add any additional non-blocking code here
}

void fetchFNGData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();

    String url = "https://" + String(fngHost) + fngPath;
    Serial.print("Connecting to FNG API: ");
    Serial.println(url);
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("Fetching FNG data...");
    display.display();

    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      StaticJsonDocument<300> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        http.end();
        return;
      }

      String value = doc["data"][0]["value"];
      String nextUpdate = doc["data"][0]["time_until_update"];
      Serial.print("Fear and Greed Index: ");
      Serial.println(value);

      // Control Servo based on FNG value
      servoPos = abs((value.toInt() * 1.8) - 180); // Map 0-100 to 0-180
      servo.write(servoPos);
      Serial.print("Servo Position: ");
      Serial.println(servoPos);

      // Control LED based on FNG value (example logic)
      if (value.toInt() > 60) { // Greed
        digitalWrite(ledPin, HIGH); // Turn LED on
      } else if (value.toInt() < 40) { // Fear
        digitalWrite(ledPin, LOW); // Turn LED off
      } else {
        // Medium, toggle LED
        digitalWrite(ledPin, !digitalRead(ledPin));
      }

      display.setCursor(0, 10);
      display.setTextSize(1);
      display.print("FNG Index: ");
      display.println(value);
      display.display();

    } else {
      Serial.print("FNG GET failed, error: ");
      Serial.println(httpCode);
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

void fetchBitcoinData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();

    String url = "https://" + String(bitcoinHost) + bitcoinPath;
    Serial.print("Connecting to Bitcoin API: ");
    Serial.println(url);
    display.setCursor(0, 20);
    display.setTextSize(1);
    display.println("Fetching BTC data...");
    display.display();

    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      StaticJsonDocument<2000> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print(F("deserializeJson Failed: "));
        Serial.println(error.f_str());
        http.end();
        return;
      }

      Serial.print("HTTP Status Code: ");
      Serial.println(httpCode);

      double BTCUSDPrice = doc["bpi"]["USD"]["rate_float"].as<double>();
      String lastUpdated = doc["time"]["updated"].as<String>();
      Serial.print("BTC/USD Price: ");
      Serial.println(BTCUSDPrice);

      Serial.print("Getting history...");
      String historyUrl = "https://" + String(bitcoinHost) + historyPath;
      http.end();
      http.begin(client, historyUrl);
      int historyHttpCode = http.GET();

      if (historyHttpCode == HTTP_CODE_OK) {
        String historyPayload = http.getString();
        StaticJsonDocument<2000> historyDoc;
        DeserializationError historyError = deserializeJson(historyDoc, historyPayload);

        if (historyError) {
          Serial.print(F("deserializeJson(History) failed: "));
          Serial.println(historyError.f_str());
          http.end();
          return;
        }

        Serial.print("History HTTP Status Code: ");
        Serial.println(historyHttpCode);
        JsonObject bpi = historyDoc["bpi"].as<JsonObject>();
        double yesterdayPrice = 0;
        for (JsonPair kv : bpi) {
          yesterdayPrice = kv.value().as<double>();
        }

        Serial.print("Yesterday's Price: ");
        Serial.println(yesterdayPrice);

        bool isUp = BTCUSDPrice > yesterdayPrice;
        double percentChange;
        String dayChangeString = "24hr. Change: ";

        if (isUp) {
          percentChange = ((BTCUSDPrice - yesterdayPrice) / yesterdayPrice) * 100;
        } else {
          percentChange = ((yesterdayPrice - BTCUSDPrice) / yesterdayPrice) * 100;
          dayChangeString += "-";
        }

        Serial.print("Percent Change: ");
        Serial.println(percentChange);

        dayChangeString += String(percentChange, 2) + "%";

        display.clearDisplay();
        // Header
        display.setTextSize(1);
        printCenter("BTC/USD", 0, 0);
        // BTC Price
        display.setTextSize(2);
        printCenter("$" + String(BTCUSDPrice, 2), 0, 25);
        // 24hr Change
        display.setTextSize(1);
        printCenter(dayChangeString, 0, 55);
        display.display();

        http.end();
      } else {
        Serial.print("History GET failed, error: ");
        Serial.println(historyHttpCode);
      }
    } else {
      Serial.print("Bitcoin GET failed, error: ");
      Serial.println(httpCode);
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

void printCenter(const String buf, int x, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buf, x, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(buf);
}