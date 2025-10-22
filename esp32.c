#include <Arduino.h>
#include "BluetoothSerial.h"
#include <SoftwareSerial.h>
#include <ESP8266SAM.h>

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include "RTClib.h"
#include "SD.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define DHT_PIN               4
#define SOIL_MOISTURE_PIN     34
#define TILT_SENSOR_PIN       35
#define MQ2_PIN               32
#define BUZZER_PIN            14
#define LED_PIN               26
#define AUDIO_OUT_PIN         25 

#define SD_CS_PIN             5

#define GPS_RX_PIN            12
#define GPS_TX_PIN            13

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);

BluetoothSerial SerialBT;
TinyGPSPlus gps;
RTC_DS3231 rtc;
Adafruit_MPU6050 mpu;
ESP8266SAM sam;

#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

unsigned long lastSensorReadMillis = 0;
String currentGpsLocation = "Location not available";
String emergencyNumber = "YOUR_EMERGENCY_NUMBER"; 

void readAndProcessSensors();
void sendDataToBluetooth(float temp, float hum, int soil, String fire, String landslide, String vib);
void triggerHardAlert(String alertType);
void handleBluetoothCommand();
void logToSDCard(String event);
void updateOLED(float temp, float hum, String fire, String landslide);
void makeEmergencyCall(String alertType);
void updateGpsLocation();

void setup() {
    Serial.begin(115200);
    Wire.begin();
    SerialBT.begin("himbuddy_esp32");
    
    pinMode(TILT_SENSOR_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 allocation failed");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("HimBuddy Starting...");
        display.display();
    }

    dht.begin();
    if (!mpu.begin()) {
        Serial.println("MPU6050 not found");
        while (1);
    }
    
    gpsSerial.begin(9600);
    Serial2.begin(9600, SERIAL_8N1, 16, 17); // SIM800L on RX2/TX2

    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC");
    }
    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    if(!SD.begin(SD_CS_PIN)){
        Serial.println("SD Card Mount Failed");
    }
}

void loop() {
    if (millis() - lastSensorReadMillis > 2000) {
        lastSensorReadMillis = millis();
        readAndProcessSensors();
    }
    updateGpsLocation();
    if (SerialBT.available()) {
        handleBluetoothCommand();
    }
}

void readAndProcessSensors() {
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();
    int soilValue = analogRead(SOIL_MOISTURE_PIN);
    int soilPercent = map(soilValue, 0, 4095, 100, 0);
    int gasValue = analogRead(MQ2_PIN);
    String fireStatus = (gasValue > 1500) ? "Detected" : "Normal";
    String landslideStatus = (digitalRead(TILT_SENSOR_PIN) == HIGH) ? "Detected" : "Safe";
    
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    float totalVibration = sqrt(pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2));
    String vibrationStatus = (totalVibration > 20) ? "High" : "Low";

    updateOLED(temp, humidity, fireStatus, landslideStatus);
    sendDataToBluetooth(temp, humidity, soilPercent, fireStatus, landslideStatus, vibrationStatus);

    if (fireStatus == "Detected") {
        triggerHardAlert("Fire");
    }
    if (vibrationStatus == "High") {
        triggerHardAlert("Earthquake");
    }
}

void sendDataToBluetooth(float temp, float hum, int soil, String fire, String landslide, String vib) {
    if (SerialBT.connected()) {
        SerialBT.printf("{\"temp\":%.1f}\n", temp);
        SerialBT.printf("{\"humidity\":%.1f}\n", hum);
        SerialBT.printf("{\"soil\":%d}\n", soil);
        SerialBT.printf("{\"fire\":\"%s\"}\n", fire.c_str());
        SerialBT.printf("{\"landslide\":\"%s\"}\n", landslide.c_str());
        SerialBT.printf("{\"vibration\":\"%s\"}\n", vib.c_str());
        SerialBT.printf("{\"lat\":%.4f, \"lon\":%.4f}\n", gps.location.lat(), gps.location.lng());
    }
}

void triggerHardAlert(String alertType) {
    String alertMessage = alertType + " Detected!";
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    
    if (SerialBT.connected()) {
        SerialBT.printf("{\"alert\":\"%s\"}\n", alertMessage.c_str());
    }
    logToSDCard(alertMessage);
    makeEmergencyCall(alertType);
    delay(2000);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
}

void makeEmergencyCall(String alertType) {
    Serial.println("Making emergency call...");
    Serial2.println("ATD" + emergencyNumber + ";");
    delay(15000); 

    String messageToSpeak = "Attention. " + alertType + " detected at my location. " + currentGpsLocation;
    sam.say(AUDIO_OUT_PIN, messageToSpeak.c_str());
    delay(10000); 

    Serial2.println("ATH"); 
    Serial.println("Call ended.");
}

void handleBluetoothCommand() {
    String command = SerialBT.readStringUntil('\n');
    command.trim();
    if (command.equals("GET_ANALYTICS")) {
        File dataFile = SD.open("/analytics.log");
        if (dataFile) {
            SerialBT.println("\n--- Analytics from SD Card ---");
            while (dataFile.available()) {
                SerialBT.write(dataFile.read());
            }
            dataFile.close();
            SerialBT.println("\n--- End of Analytics ---\n");
        } else {
            SerialBT.println("Failed to open analytics.log file.");
        }
    }
}

void logToSDCard(String event) {
    File dataFile = SD.open("/analytics.log", FILE_APPEND);
    if (dataFile) {
        DateTime now = rtc.now();
        String logEntry = String(now.year()) + "/" + String(now.month()) + "/" + String(now.day()) + " ";
        logEntry += String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()) + " - ";
        logEntry += event;
        dataFile.println(logEntry);
        dataFile.close();
    }
}

void updateOLED(float temp, float hum, String fire, String landslide) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.print("T:"); display.print(temp); display.print(" H:"); display.print(hum);
    display.println("");
    display.print("Fire:"); display.println(fire);
    display.print("L'slide:"); display.println(landslide);
    DateTime now = rtc.now();
    display.print(now.hour()); display.print(":"); display.print(now.minute());
    display.display();
}

void updateGpsLocation(){
    while (gpsSerial.available() > 0) {
        if (gps.encode(gpsSerial.read())) {
            if (gps.location.isUpdated()) {
                currentGpsLocation = "Latitude is " + String(gps.location.lat(), 6) + " and Longitude is " + String(gps.location.lng(), 6);
            }
        }
    }
}
