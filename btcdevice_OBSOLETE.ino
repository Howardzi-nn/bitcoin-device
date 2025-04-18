#include <WiFi.h>
#include <WiFiClientSecure.h> // For secure HTTPS connection
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h> // Use ESP32Servo instead of Servo.h
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "secrets.h" // WiFi Configuration (WiFi name and Password)

// OLED display settings
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int ledPin = 14;    // GPIO14
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

String fngValue = "";
String fngClassification = "";

double BTCUSDPrice = 0;
String dayChangeString = "";

void setup() {
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

  // Initialize LED, not working now
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // Turn LED on initially

  fetchFNGData();
  fetchBitcoinData();
}

void loop() {
  unsigned long currentMillis = millis();

  // Fetch FNG data at defined intervals
  if (currentMillis - previousFNGMillis >= fngInterval) {
    previousFNGMillis = currentMillis;
    fetchFNGData();
  }

  // Fetch Bitcoin data at defined intervals
  if (currentMillis - previousBitcoinMillis >= bitcoinInterval) {
    previousBitcoinMillis = currentMillis;
    fetchBitcoinData();
  }

  // Add any additional non-blocking code here
}

// Function to fetch and process FNG data
void fetchFNGData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Not recommended for production

    String url = "https://" + String(fngHost) + fngPath;
    Serial.print("Connecting to FNG API: ");
    Serial.println(url);

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

      fngValue = doc["data"][0]["value"].as<String>();
      fngClassification = doc["data"][0]["value_classification"].as<String>();
      String nextUpdate = doc["data"][0]["time_until_update"];
      Serial.print("Fear and Greed Index: ");
      Serial.println(fngValue);
      Serial.print("Classification: ");
      Serial.println(fngClassification);

      servoPos = abs((fngValue.toInt() * 1.8) - 180); // Map 0-100 to 0-180
      servo.write(servoPos);
      Serial.print("Servo Position: ");
      Serial.println(servoPos);

      if (fngValue.toInt() > 60) { // Greed
        digitalWrite(ledPin, HIGH); // Turn LED on
      } else if (fngValue.toInt() < 40) { // Fear
        digitalWrite(ledPin, LOW); // Turn LED off
      } else {
        // Medium, toggle LED
        digitalWrite(ledPin, !digitalRead(ledPin));
      }

    } else {
      Serial.print("FNG GET failed, error: ");
      Serial.println(httpCode);
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

// Function to fetch and process Bitcoin data
void fetchBitcoinData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Not recommended for production

    String url = "https://" + String(bitcoinHost) + bitcoinPath;
    Serial.print("Connecting to Bitcoin API: ");
    Serial.println(url);

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

      BTCUSDPrice = doc["bpi"]["USD"]["rate_float"].as<double>();
      String lastUpdated = doc["time"]["updated"].as<String>();
      Serial.print("BTC/USD Price: ");
      Serial.println(BTCUSDPrice);

      Serial.print("Getting history...");
      String historyUrl = "https://" + String(bitcoinHost) + historyPath;
      http.end(); // Close previous connection
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

        if (isUp) {
          percentChange = ((BTCUSDPrice - yesterdayPrice) / yesterdayPrice) * 100;
        } else {
          percentChange = ((BTCUSDPrice - yesterdayPrice) / yesterdayPrice) * 100;
        }

        Serial.print("Percent Change: ");
        Serial.println(percentChange);

        dayChangeString = "24hr Change: " + String(percentChange, 2) + "%";

        updateDisplay();

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

// Function to update the OLED display with all the data
void updateDisplay() {
  display.clearDisplay();

  // FNG Data
  display.setTextSize(1);
  String fngDisplayString = "FNG: " + fngValue + " (" + fngClassification + ")";
  printCenter(fngDisplayString, 0, 0);

  // BTC Price
  display.setTextSize(2);
  printCenter("$" + String(BTCUSDPrice, 2), 0, 25);

  // 24hr Change
  display.setTextSize(1);
  printCenter(dayChangeString, 0, 55);

  display.display();
}

// Helper function to center text on the OLED display
void printCenter(const String buf, int x, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buf, x, y, &x1, &y1, &w, &h); // Calculate width of the string
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(buf);
}