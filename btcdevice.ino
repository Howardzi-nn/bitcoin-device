#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "secrets.h" // SECRET_SSID, SECRET_WIFI_PASSWORD, SECRET_CMC_API_KEY

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Servo and LED settings
const int ledPin   = 14;
const int servoPin = 18;
Servo servo;
int servoPos = 90;

// API settings
// https://alternative.me/crypto/api/
const char* fngHost     = "api.alternative.me";
const String fngPath    = "/fng/";
const char* cmcHost     = "pro-api.coinmarketcap.com";
const String cmcLatestPath    = "/v1/cryptocurrency/quotes/latest?symbol=BTC&convert=USD";
// const String cmcHistoryPath   = "/v1/cryptocurrency/ohlcv/historical?symbol=BTC&time_period=daily";

// Update intervals
unsigned long previousFNGMillis     = 0;
// const unsigned long fngInterval     = 60000;
const unsigned long fngInterval     = 120000;
unsigned long previousBitcoinMillis = 0;
// const unsigned long bitcoinInterval = 60000;
const unsigned long bitcoinInterval = 120000;

// Fetched data
String fngValue, fngClassification;
double BTCUSDPrice = 0;
String dayChangeString;
bool fngSuccess = false, btcSuccess = false;

void printCenter(const String &buf, int x, int y) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(buf, x, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(buf);
}

void setup() {
  Serial.begin(115200);
  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed"); while(true) delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("Connecting to WiFi...");
  display.display();

  // WiFi
  WiFi.begin(SECRET_SSID, SECRET_WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); display.print("."); display.display();
  }
  display.clearDisplay();
  display.println("WiFi connected");
  display.display();
  delay(1000);

  // NTP for TLS
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) { delay(500); now = time(nullptr); }

  // Servo, LED
  servo.attach(servoPin, 500, 2400);
  servo.write(servoPos);
  delay(500);
  servo.detach();
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  // First fetch
  fetchFNGData();
  fetchBitcoinData();
}

void loop() {
  unsigned long m = millis();
  if (m - previousFNGMillis >= fngInterval) {
    previousFNGMillis = m;
    fetchFNGData();
  }
  if (m - previousBitcoinMillis >= bitcoinInterval) {
    previousBitcoinMillis = m;
    fetchBitcoinData();
  }
}

//------------------------------------------------------------------------
void fetchFNGData() {
  if (WiFi.status() != WL_CONNECTED) { fngSuccess = false; updateDisplay(); return; }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  String url = "https://" + String(fngHost) + fngPath;
  http.begin(client, url);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String p = http.getString();
    StaticJsonDocument<300> doc;
    if (!deserializeJson(doc, p)) {
      fngValue = doc["data"][0]["value"].as<String>();
      fngClassification = doc["data"][0]["value_classification"].as<String>();
      int fngInt = fngValue.toInt();
      
      servo.attach(servoPin, 500, 2400);
      // servoPos = abs((fngInt * 1.8) - 180);
      // servoPos = map(fngInt, 0, 100, 180, 0);
      servoPos = map(fngInt, 0, 100, 180, 0);
      // servoPos = map( (fngValue.toInt()), 0, 100, 180, 0 );
      servo.write(servoPos);
      // delay(200); 
      // servo.detach();

      if (fngInt > 60)       digitalWrite(ledPin, HIGH);
      else if (fngInt < 40)  digitalWrite(ledPin, LOW);
      else                   digitalWrite(ledPin, !digitalRead(ledPin));
      fngSuccess = true;
    } else fngSuccess = false;
  } else fngSuccess = false;
  http.end();
  updateDisplay();
}

//------------------------------------------------------------------------
void fetchBitcoinData() {
  if (WiFi.status() != WL_CONNECTED) { btcSuccess = false; updateDisplay(); return; }

  // --- Latest price ---
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  String url = "https://" + String(cmcHost) + cmcLatestPath;
  http.begin(client, url);
  http.addHeader("Accepts", "application/json");
  http.addHeader("X-CMC_PRO_API_KEY", SECRET_CMC_API_KEY);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String p = http.getString();
    StaticJsonDocument<1024> doc;
    if (!deserializeJson(doc, p)) {
      BTCUSDPrice = doc["data"]["BTC"]["quote"]["USD"]["price"].as<double>();
      btcSuccess = true;
    } else btcSuccess = false;
  } else {
    Serial.printf("CMC latest GET failed: %d\n", code);
    btcSuccess = false;
  }
  http.end();

  // --- Historical (last 24h) ---
  HTTPClient httpH;
  WiFiClientSecure clientH;
  clientH.setInsecure();
  // vypočítat čas ve formátu YYYY-MM-DD
  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  char endBuf[11], startBuf[11];
  strftime(endBuf, 11, "%Y-%m-%d", &t);
  t.tm_mday -= 1;
  mktime(&t);
  strftime(startBuf, 11, "%Y-%m-%d", &t);

// String histUrl = String("https://") + cmcHost
//                + cmcHistoryPath
//                + "&time_start=" + startBuf
//                + "&time_end="   + endBuf
//                + "&convert=USD";
//   httpH.begin(clientH, histUrl);
//   httpH.addHeader("Accepts", "application/json");
//   httpH.addHeader("X-CMC_PRO_API_KEY", SECRET_CMC_API_KEY);
//   int codeH = httpH.GET();
//   if (codeH == HTTP_CODE_OK) {
//     String pH = httpH.getString();  
//     StaticJsonDocument<2000> docH;
//     if (!deserializeJson(docH, pH)) {
//       JsonArray arr = docH["data"]["quotes"].as<JsonArray>();
//       double firstClose = arr[0]["quote"]["USD"]["close"].as<double>();
//      double lastClose  = arr[arr.size()-1]["quote"]["USD"]["close"].as<double>();
//      double pct = (BTCUSDPrice - firstClose) / firstClose * 100.0;
//      dayChangeString = "24h: " + String(pct, 2) + "%";
//    }
//  }
//  httpH.end();

  updateDisplay();
}

//------------------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();
  if (!fngSuccess || !btcSuccess) {
    display.setTextSize(1);
    display.setCursor(0, 10);
    if (!fngSuccess) display.println("FNG API error");
    if (!btcSuccess) display.println("CMC API error");
    display.display();
    return;
  }
  // FNG
  display.setTextSize(1);
  printCenter("FNG: " + fngValue + " (" + fngClassification + ")", 0, 0);
  // BTC price
  display.setTextSize(2);
  printCenter("$" + String(BTCUSDPrice, 2), 0, 25);
  // 24h change
  display.setTextSize(1);
  printCenter(dayChangeString, 0, 55);
  display.display();
}
