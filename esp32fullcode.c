/*
  ESP32 Multi-Sensor Monitoring with Immediate OLED Alerts + Bluetooth + SD logging
  - OLED alert overlay (big inverted banner) on any alert
  - Sends alert JSON to BluetoothSerial (SPP) immediately
  - Logs alerts to /alerts.log (JSON lines) and periodic sensor snapshots to /senslog.csv
  - Sensors included: DHT22, MQ-2, Soil (Analog), Tilt (digital), BME280, MPU6050, GPS(Neo-6M), DS3231 RTC
  - Use Arduino IDE with ESP32 core

  Adjust PINs, thresholds, and ENABLE_SIM800L flag for your wiring.
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "BluetoothSerial.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <DHT.h>
#include <RTClib.h>
#include <TinyGPSPlus.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------- CONFIG ----------
#define ENABLE_SIM800L false
#define OLED_RESET -1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Pins (tune as per your wiring)
const int PIN_DHT = 15;
const int PIN_MQ2 = 34;
const int PIN_SOIL = 35;
const int PIN_TILT = 14;
const int PIN_BUZZER = 27;
const int SD_CS = 5;

// UART pins
#define SIM800_TX_PIN 25
#define SIM800_RX_PIN 26
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17

// Sensor types and timing
#define DHTTYPE DHT22
#define DHT_READ_INTERVAL_MS 5000
#define SENSOR_REPORT_INTERVAL_MS 5000

// Alert behavior
#define ALERT_DISPLAY_MS 6000        // how long overlay stays (ms)
#define ALERT_MIN_INTERVAL_MS 5000   // minimum gap between same alerts

// Thresholds (tune/calibrate)
const int MQ2_SMOKE_THRESHOLD = 300;     // raw ADC threshold (0-4095) - tune
const int SOIL_DRY_THRESHOLD = 2000;     // raw ADC (0-4095)
const float DHT_TEMP_HIGH = 45.0;
const float DHT_HUM_HIGH = 85.0;

// ---------- LIBS / OBJECTS ----------
BluetoothSerial SerialBT;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BME280 bme;
DHT dht(PIN_DHT, DHTTYPE);
RTC_DS3231 rtc;
TinyGPSPlus gps;
Adafruit_MPU6050 mpu;
HardwareSerial SerialSIM(1);
HardwareSerial SerialGPS(2);

// ---------- STATE ----------
unsigned long lastDHTread = 0;
unsigned long lastSensorReport = 0;
unsigned long lastMQ2Alert = 0;
unsigned long lastSoilAlert = 0;
unsigned long lastTiltAlert = 0;
unsigned long lastDhtAlert = 0;

bool sdAvailable = false;

// Alert overlay info
bool alertActive = false;
unsigned long alertSince = 0;
char alertType[32] = "";
char alertMsg[128] = "";
char alertExtra[128] = "";

// buffers
char jsonBuf[512];
char csvBuf[512];

// ---------- HELPERS ----------
String isoNow() {
  if (!rtc.begin()) {
    // fallback: millis-based approximate (not preferred)
    unsigned long s = millis() / 1000;
    unsigned long h = (s/3600)%24;
    unsigned long m = (s/60)%60;
    unsigned long sec = s%60;
    char t[32];
    snprintf(t, sizeof(t), "1970-01-01T%02lu:%02lu:%02lu", h, m, sec);
    return String(t);
  }
  DateTime now = rtc.now();
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(buf);
}

void sdLogAlert(const char* type, const char* msg, const char* extra, const char* payloadJSON) {
  if (!sdAvailable) return;
  File f = SD.open("/alerts.log", FILE_APPEND);
  if (!f) return;
  // write payloadJSON if provided, else compose small JSON
  if (payloadJSON && strlen(payloadJSON) > 0) {
    f.println(payloadJSON);
  } else {
    char line[512];
    snprintf(line, sizeof(line), "{\"time\":\"%s\",\"type\":\"%s\",\"msg\":\"%s\",\"extra\":\"%s\"}",
             isoNow().c_str(), type, msg, extra ? extra : "");
    f.println(line);
  }
  f.close();
}

void sdLogSensorSnapshot(const char* snapshotCsvLine) {
  if (!sdAvailable) return;
  File f = SD.open("/senslog.csv", FILE_APPEND);
  if (!f) return;
  f.println(snapshotCsvLine);
  f.close();
}

void sendBluetoothAlert(const char* type, const char* msg, const char* extra,
                        float dhtT, float dhtH, float bmeT, float bmeH, float bmeP,
                        int mqRaw, int soilRaw, double gpsLat, double gpsLng) {
  // Compose JSON line (simple, line-based)
  // Example: {"type":"TILT","msg":"Tilt detected","time":"...","extra":"x","dhtT":..,"dhtH":..,"bmeT":..,"bmeH":..,"bmeP":..,"mq":..,"soil":..,"gps":"lat,lng"}
  char gpsField[64] = "";
  if (gpsLat != 0.0 || gpsLng != 0.0) {
    snprintf(gpsField, sizeof(gpsField), ",\"gps\":\"%.6f,%.6f\"", gpsLat, gpsLng);
  }
  snprintf(jsonBuf, sizeof(jsonBuf),
           "{\"type\":\"%s\",\"msg\":\"%s\",\"time\":\"%s\",\"extra\":\"%s\",\"dhtT\":%.2f,\"dhtH\":%.2f,\"bmeT\":%.2f,\"bmeH\":%.2f,\"bmeP\":%.2f,\"mq\":%d,\"soil\":%d%s}",
           type, msg, isoNow().c_str(), extra ? extra : "",
           isnan(dhtT) ? 0.0 : dhtT, isnan(dhtH) ? 0.0 : dhtH,
           isnan(bmeT) ? 0.0 : bmeT, isnan(bmeH) ? 0.0 : bmeH, isnan(bmeP) ? 0.0 : bmeP,
           mqRaw, soilRaw, gpsField);
  // Send
  SerialBT.println(jsonBuf);
  Serial.println("[ALERT_SENT] " + String(jsonBuf));
  // SD log
  sdLogAlert(type, msg, extra, jsonBuf);
}

void triggerAlert(const char* type, const char* msg, const char* extra,
                  float dhtT=NAN, float dhtH=NAN, float bmeT=NAN, float bmeH=NAN, float bmeP=NAN,
                  int mqRaw=0, int soilRaw=0, double gpsLat=0.0, double gpsLng=0.0) {
  // set overlay
  alertActive = true;
  alertSince = millis();
  strncpy(alertType, type, sizeof(alertType)-1);
  strncpy(alertMsg, msg, sizeof(alertMsg)-1);
  if (extra) strncpy(alertExtra, extra, sizeof(alertExtra)-1); else alertExtra[0]=0;
  // beep
  digitalWrite(PIN_BUZZER, HIGH);
  delay(120);
  digitalWrite(PIN_BUZZER, LOW);
  // send to app and log on SD
  sendBluetoothAlert(type, msg, extra, dhtT, dhtH, bmeT, bmeH, bmeP, mqRaw, soilRaw, gpsLat, gpsLng);
}

void drawAlertOverlay() {
  // Draw a prominent inverted banner with ALERT and type/msg
  display.fillRect(0, 0, SCREEN_WIDTH, 20, SSD1306_WHITE); // white bar top
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("!!! ALERT ");
  display.print(alertType);
  display.setCursor(2, 12);
  display.print(alertMsg);
  display.setTextColor(SSD1306_WHITE); // restore
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("ESP32 Monitor (alerts+BT+SD) starting...");

  pinMode(PIN_TILT, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  Wire.begin(21, 22); // SDA, SCL

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init fail");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("OLED OK");
    display.display();
  }

  if (!bme.begin(0x76)) {
    Serial.println("BME280 not found at 0x76; trying 0x77...");
    bme.begin(0x77);
  }

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found");
  }

  dht.begin();

  if (!rtc.begin()) {
    Serial.println("RTC not found");
  } else {
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  if (SD.begin(SD_CS)) {
    sdAvailable = true;
    Serial.println("SD mounted");
    // ensure senslog header exists
    File fh = SD.open("/senslog.csv", FILE_READ);
    if (!fh) {
      File f = SD.open("/senslog.csv", FILE_WRITE);
      if (f) {
        f.println("time,dhtT,dhtH,bmeT,bmeH,bmeP,mqRaw,soilRaw,gpsLat,gpsLng");
        f.close();
      }
    } else fh.close();
  } else {
    sdAvailable = false;
    Serial.println("SD mount failed");
  }

  // Serial ports
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  if (ENABLE_SIM800L) SerialSIM.begin(9600, SERIAL_8N1, SIM800_RX_PIN, SIM800_TX_PIN);

  // Bluetooth
  SerialBT.begin("ESP32_MONITOR");
  Serial.println("BT started: ESP32_MONITOR");

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("ESP32 Monitor Ready");
  display.display();
  delay(800);
}

// ---------- SENSOR READ / CHECK ----------
float readMQ2Raw() { return (float)analogRead(PIN_MQ2); }
int readSoilRaw() { return analogRead(PIN_SOIL); }

void readGPS() {
  while (SerialGPS.available()) gps.encode(SerialGPS.read());
}

void checkSensorsAndAlerts() {
  unsigned long now = millis();

  // DHT check (periodic)
  if (now - lastDHTread > DHT_READ_INTERVAL_MS) {
    lastDHTread = now;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(t) && !isnan(h)) {
      if ((t > DHT_TEMP_HIGH || h > DHT_HUM_HIGH) && (now - lastDhtAlert > ALERT_MIN_INTERVAL_MS)) {
        lastDhtAlert = now;
        char extra[80];
        snprintf(extra, sizeof(extra), "t:%.1f,h:%.1f", t, h);
        // read other sensor values for context
        float bmeT = bme.readTemperature();
        float bmeH = bme.readHumidity();
        float bmeP = bme.readPressure()/100.0F;
        int mq = (int)readMQ2Raw();
        int soil = readSoilRaw();
        double glat = gps.location.isValid() ? gps.location.lat() : 0.0;
        double glng = gps.location.isValid() ? gps.location.lng() : 0.0;
        triggerAlert("DHT", "Temperature/Humidity high", extra, t, h, bmeT, bmeH, bmeP, mq, soil, glat, glng);
      }
    }
  }

  // MQ-2
  int mq = (int)readMQ2Raw();
  if (mq > MQ2_SMOKE_THRESHOLD && (now - lastMQ2Alert > ALERT_MIN_INTERVAL_MS)) {
    lastMQ2Alert = now;
    char extra[64];
    snprintf(extra, sizeof(extra), "mq:%d", mq);
    float dhtT = dht.readTemperature();
    float dhtH = dht.readHumidity();
    float bmeT = bme.readTemperature();
    float bmeH = bme.readHumidity();
    float bmeP = bme.readPressure()/100.0F;
    double glat = gps.location.isValid() ? gps.location.lat() : 0.0;
    double glng = gps.location.isValid() ? gps.location.lng() : 0.0;
    triggerAlert("MQ2", "Smoke/Gas detected", extra, dhtT, dhtH, bmeT, bmeH, bmeP, mq, readSoilRaw(), glat, glng);
  }

  // Soil
  int soil = readSoilRaw();
  if (soil > SOIL_DRY_THRESHOLD && (now - lastSoilAlert > ALERT_MIN_INTERVAL_MS)) {
    lastSoilAlert = now;
    char extra[64];
    snprintf(extra, sizeof(extra), "soil:%d", soil);
    float dhtT = dht.readTemperature();
    float dhtH = dht.readHumidity();
    float bmeT = bme.readTemperature();
    float bmeH = bme.readHumidity();
    float bmeP = bme.readPressure()/100.0F;
    double glat = gps.location.isValid() ? gps.location.lat() : 0.0;
    double glng = gps.location.isValid() ? gps.location.lng() : 0.0;
    triggerAlert("SOIL", "Soil dry", extra, dhtT, dhtH, bmeT, bmeH, bmeP, mq, soil, glat, glng);
  }

  // Tilt / Vibration
  int tilt = digitalRead(PIN_TILT);
  if (tilt == LOW && (now - lastTiltAlert > ALERT_MIN_INTERVAL_MS)) {
    lastTiltAlert = now;
    double glat = gps.location.isValid() ? gps.location.lat() : 0.0;
    double glng = gps.location.isValid() ? gps.location.lng() : 0.0;
    triggerAlert("TILT", "Tilt/Vibration detected", NULL, dht.readTemperature(), dht.readHumidity(),
                 bme.readTemperature(), bme.readHumidity(), bme.readPressure()/100.0F,
                 mq, soil, glat, glng);
  }
}

// Compose and log periodic sensor CSV snapshot (for analytics)
void logSensorSnapshot() {
  float dhtT = dht.readTemperature();
  float dhtH = dht.readHumidity();
  float bmeT = bme.readTemperature();
  float bmeH = bme.readHumidity();
  float bmeP = bme.readPressure()/100.0F;
  int mq = (int)readMQ2Raw();
  int soil = readSoilRaw();
  double glat = gps.location.isValid() ? gps.location.lat() : 0.0;
  double glng = gps.location.isValid() ? gps.location.lng() : 0.0;

  // CSV: time,dhtT,dhtH,bmeT,bmeH,bmeP,mqRaw,soilRaw,gpsLat,gpsLng
  snprintf(csvBuf, sizeof(csvBuf), "\"%s\",%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%.6f,%.6f",
           isoNow().c_str(),
           isnan(dhtT) ? 0.0 : dhtT, isnan(dhtH) ? 0.0 : dhtH,
           isnan(bmeT) ? 0.0 : bmeT, isnan(bmeH) ? 0.0 : bmeH, isnan(bmeP) ? 0.0 : bmeP,
           mq, soil, glat, glng);
  Serial.println(csvBuf);
  sdLogSensorSnapshot(csvBuf);
}

// ---------- DISPLAY ----------
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  // Line 1: Time
  display.println(isoNow());
  // Line 2: DHT
  float dhtT = dht.readTemperature();
  float dhtH = dht.readHumidity();
  if (!isnan(dhtT) && !isnan(dhtH)) {
    display.printf("DHT T:%.1fC H:%.1f%%\n", dhtT, dhtH);
  } else display.println("DHT: --");
  // Line 3: BME
  if (bme.sensorID()) {
    display.printf("BME T:%.1f P:%.0f\n", bme.readTemperature(), bme.readPressure()/100.0F);
  } else display.println("BME: --");
  // Line 4: MQ2 / Soil
  display.printf("MQ:%d Soil:%d\n", (int)readMQ2Raw(), readSoilRaw());
  // Line 5: GPS short
  if (gps.location.isValid()) {
    display.printf("GPS:%.4f,%.4f\n", gps.location.lat(), gps.location.lng());
  } else display.println("GPS: --");

  // If an alert is active and within ALERT_DISPLAY_MS show overlay
  if (alertActive && (millis() - alertSince) < ALERT_DISPLAY_MS) {
    drawAlertOverlay();
  } else {
    alertActive = false;
  }
  display.display();
}

// ---------- LOOP ----------
void loop() {
  // quick GPS read
  readGPS();

  // check sensors and generate alerts if needed
  checkSensorsAndAlerts();

  static unsigned long lastDisplay = 0;
  if (millis() - lastDisplay > 1500) {
    lastDisplay = millis();
    updateDisplay();
  }

  if (millis() - lastSensorReport > SENSOR_REPORT_INTERVAL_MS) {
    lastSensorReport = millis();
    logSensorSnapshot();
  }

  // handle incoming BT commands (optional)
  if (SerialBT.available()) {
    String cmd = SerialBT.readStringUntil('\n');
    Serial.println("BT_CMD: " + cmd);
    if (cmd.indexOf("STATUS") >= 0) {
      // reply with summary
      float dhtT = dht.readTemperature();
      float dhtH = dht.readHumidity();
      int mq = (int)readMQ2Raw();
      int soil = readSoilRaw();
      char out[256];
      snprintf(out, sizeof(out), "{\"status\":\"ok\",\"dhtT\":%.2f,\"dhtH\":%.2f,\"mq\":%d,\"soil\":%d}",
               isnan(dhtT) ? 0.0 : dhtT, isnan(dhtH) ? 0.0 : dhtH, mq, soil);
      SerialBT.println(out);
    } else if (cmd.indexOf("PING") >= 0) {
      SerialBT.println("{\"pong\":1}");
    }
  }

  delay(10);
}
