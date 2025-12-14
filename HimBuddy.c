/****************************************************************
 * PROJECT: HIMBUDDY (Smart Disaster Management System)
 * DEVELOPER: Akshit Negi
 * CLASS: 10th | ROLL NO: 06
 * BOARD: ESP32 DOIT DEVKIT V1
 *
 * UPDATED FEATURES: 
 * - Full Red Blinking Website on Alert
 * - Web Text Messaging to OLED
 * - Magic "alert" command
 * - Google Maps Intent Link
 * - Buzzer Test Button
 ****************************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <TinyGPS++.h>

// ==============================================================
//                    WIFI CONFIGURATION
// ==============================================================
const char* ssid = "Himbuddy by akshitvip"; // Hotspot Name
const char* pass = "akshitvip";             // Password
WebServer server(80);                       // Server Port

// ==============================================================
//                    OLED DISPLAY SETTINGS
// ==============================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==============================================================
//                    PIN DEFINITIONS (STABLE PINS)
// ==============================================================
// Soil Sensor connected to Pin 32 (ADC1)
#define SOIL_PIN 32          

// Gas Sensor connected to Pin 33 (ADC1)
#define MQ2_PIN 33           

// Buzzer connected to Pin 25
#define BUZZER_PIN 25        

// DHT Sensor connected to Pin 4
#define DHTPIN 4
#define DHTTYPE DHT22

// GPS Module connected to Serial2 (RX=16, TX=17)
#define GPS_RX 16
#define GPS_TX 17

// ==============================================================
//                    SAFETY THRESHOLDS (LIMITS)
// ==============================================================
// Isse kam value aayi to Flood hai
#define FLOOD_LIMIT 1500     

// Isse zyada value aayi to Aag hai
#define GAS_LIMIT 2500       

// Isse tez hila to Bhukamp hai
#define QUAKE_LIMIT 3.5      

// ==============================================================
//                    SENSOR OBJECTS
// ==============================================================
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

// ==============================================================
//                    GLOBAL VARIABLES
// ==============================================================
int menuIndex = 0; 
bool inMenu = true; 

// Menu Items Array
String menuItems[] = {
  "SOIL SENSOR", 
  "MQ2 GAS", 
  "DHT TEMP", 
  "MPU QUAKE", 
  "GPS LOC", 
  "DEV INFO"
};

// Variables for Logic
String currentAlert = ""; // Stores current danger message

// --- Variables for New Features (Text Box & Web) ---
String lastWebMessage = "";
bool showMessageMode = false;
unsigned long messageTimer = 0;

// --- MPU Logic Variables ---
unsigned long moveStartTime = 0;
bool earthquake = false;
float lastX = 0;
float lastY = 0;

// ==============================================================
//                 BUZZER FUNCTION (MANUAL TONE)
// ==============================================================
void playTone() {
  for(int i = 0; i < 80; i++) { 
    digitalWrite(BUZZER_PIN, HIGH); 
    delayMicroseconds(250);
    digitalWrite(BUZZER_PIN, LOW); 
    delayMicroseconds(250);
  }
}

// ==============================================================
//             SAFETY CHECK LOGIC (PRIORITY 1)
// ==============================================================
bool checkSafetyPriority() {
  currentAlert = ""; // Reset alert string
  
  // ---------------- CHECK 1: FLOOD ----------------
  int soil = analogRead(SOIL_PIN);
  if (soil < FLOOD_LIMIT && soil > 10) { 
    currentAlert = "FLOOD DETECTED!"; 
    
    // OLED Alert
    display.clearDisplay(); 
    display.setTextColor(WHITE);
    display.setTextSize(3); 
    display.setCursor(10, 10); 
    display.println("FLOOD!");
    display.display();
    
    // Siren Logic
    for(int k=0; k<5; k++) { 
      playTone(); 
      delay(50); 
    }
    return true; // Khatra hai!
  }

  // ---------------- CHECK 2: FIRE ----------------
  int gas = analogRead(MQ2_PIN);
  if (gas > GAS_LIMIT) { 
    currentAlert = "FIRE ALERT!"; 
    
    // OLED Alert
    display.clearDisplay(); 
    display.setTextColor(WHITE);
    display.setTextSize(3); 
    display.setCursor(10, 10); 
    display.println("FIRE!");
    display.display();
    
    // Siren Logic
    for(int k=0; k<5; k++) { 
      playTone(); 
      delay(50); 
    }
    return true; // Khatra hai!
  }

  // ---------------- CHECK 3: EARTHQUAKE ----------------
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  
  float dx = abs(a.acceleration.x - lastX);
  float dy = abs(a.acceleration.y - lastY);
  lastX = a.acceleration.x; 
  lastY = a.acceleration.y;

  if (dx > QUAKE_LIMIT || dy > QUAKE_LIMIT) { 
    currentAlert = "EARTHQUAKE!"; 
    
    // OLED Alert with WARNING SIGN
    display.clearDisplay(); 
    display.setTextColor(WHITE);
    
    // Draw Exclamation Mark (Alert Sign)
    display.setTextSize(4);
    display.setCursor(55, 0);
    display.println("!"); 
    
    display.setTextSize(2); 
    display.setCursor(5, 40); 
    display.println("EARTHQUAKE");
    display.display();
    
    // Siren Logic
    for(int k=0; k<5; k++) { 
      playTone(); 
      delay(50); 
    }
    return true; // Khatra hai!
  }

  return false; // Sab Safe hai
}

// ==============================================================
//                    WEB SERVER HANDLER
// ==============================================================
void handleRoot() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  
  // Auto Refresh for Realtime Alerts
  // HTML refresh se page reload hoga taaki alert dikhe
  html += "<meta http-equiv='refresh' content='2'>"; 
  
  // --- JAVASCRIPT FOR NOTIFICATIONS & VIBRATION ---
  html += "<script>";
  html += "function reqPerm() { Notification.requestPermission(); }";
  
  if (currentAlert != "") {
     // Browser Notification
     html += "if(Notification.permission === 'granted') {";
     html += "  new Notification('HIMBUDDY DANGER!', { body: '" + currentAlert + "' });";
     html += "}";
     // Phone Vibration (200ms vibrate, 100ms stop, 200ms vibrate)
     html += "if (navigator.vibrate) { navigator.vibrate([500, 200, 500]); }";
  }
  html += "</script>";

  // --- CSS STYLING ---
  html += "<style>";
  html += "body { font-family: sans-serif; text-align: center; background: #222; color: white; margin: 0; padding: 10px; }";
  
  // --- DANGER BLINKING LOGIC ---
  if (currentAlert != "") {
    // Agar Alert hai to RED BLINK karega
    html += "body { animation: blinkRed 0.5s infinite; }";
    html += "@keyframes blinkRed { 0% {background-color: red;} 50% {background-color: black;} 100% {background-color: red;} }";
    html += ".alert-box { border: 5px solid yellow; background: darkred; padding: 20px; border-radius: 10px; }";
    html += "h1 { font-size: 40px; }";
  }

  html += "button { width: 90%; padding: 15px; margin: 8px; font-size: 18px; border-radius: 10px; border: none; cursor: pointer; }";
  html += ".nav { background: #007bff; color: white; }"; 
  html += ".act { background: #28a745; color: white; }"; 
  html += ".ext { background: #dc3545; color: white; }"; 
  html += ".info { background: #ffc107; color: black; }";
  html += ".purple { background: #8e44ad; color: white; }"; 
  html += ".orange { background: #e67e22; color: white; }"; 
  
  // Input Box Styling
  html += "input[type=text] { width: 65%; padding: 12px; border-radius: 5px; border: none; margin-bottom: 10px; }";
  html += "input[type=submit] { width: 25%; padding: 12px; background: #27ae60; color: white; border: none; border-radius: 5px; font-weight: bold; }";
  
  html += "</style></head><body>";

  // --- HTML BODY ---
  if (currentAlert != "") {
    // DANGER MODE
    html += "<div class='alert-box'>";
    html += "<h1>⚠️ DANGER ⚠️</h1>";
    html += "<h2>" + currentAlert + "</h2>";
    html += "<h3>GET TO SAFETY!</h3>";
    html += "</div><br>";
  }

  html += "<h1>HIMBUDDY CONTROL</h1>";
  html += "<h3>Mode: " + (inMenu ? "MENU" : menuItems[menuIndex]) + "</h3>";
  
  // --- TEXT BOX FEATURE ---
  html += "<div style='background:#333; padding:15px; border-radius:10px;'>";
  html += "<form action='/msg' method='GET'>";
  html += "<label><b>SEND TO OLED:</b></label><br>";
  html += "<input type='text' name='t' placeholder='Type Msg (or alert)'> ";
  html += "<input type='submit' value='SEND'>";
  html += "</form></div>";

  // Notification Button
  html += "<br><button onclick='reqPerm()' style='background:#6610f2;color:white;'>🔔 ALLOW ALERTS</button>";
  
  html += "<hr>";
  
  // Control Buttons
  html += "<a href='/up'><button class='nav'>UP</button></a>";
  html += "<a href='/down'><button class='nav'>DOWN</button></a>";
  html += "<a href='/select'><button class='act'>SELECT</button></a>";
  html += "<a href='/exit'><button class='ext'>EXIT</button></a>";
  
  html += "<hr>";
  
  // --- MAPS & BUZZER ---
  
  // Logic to create Google Maps Link
  String mapLink = "https://www.google.com/maps/search/?api=1&query=";
  if (gps.location.isValid()) {
     // Real Location Link
     mapLink += String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6);
  } else {
     // Default Jeori Location Link
     mapLink += "31.4982,77.8054";
  }
  
  html += "<a href='" + mapLink + "' target='_blank'><button class='purple'>📍 OPEN MAPS (GPS)</button></a>";
  html += "<a href='/test_buzz'><button class='orange'>🔊 TEST ALARM</button></a>";

  html += "<hr>";
  
  // Extra Features
  html += "<a href='/dev'><button class='info'>SHOW DEV INFO</button></a>";
  html += "<a href='/view_temp'><button class='info'>CHECK TEMP</button></a>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ==============================================================
//           NEW ROUTE HANDLERS (MESSAGE & TEST)
// ==============================================================

// Handler for Text Message from Website
void handleMessage() {
  if (server.hasArg("t")) {
    String msg = server.arg("t");
    
    // Check for Magic Word "alert"
    if (msg.equalsIgnoreCase("alert")) {
       // Trigger Alarm Logic
       display.clearDisplay();
       display.setTextSize(3); 
       display.setTextColor(WHITE);
       display.setCursor(10,20); 
       display.println("ALERT!");
       display.display();
       
       // Beep for 3 seconds approx
       for(int k=0; k<30; k++) { 
         playTone(); 
         delay(50);
       }
       lastWebMessage = "USER SENT ALERT!";
    } else {
       // Normal Message
       lastWebMessage = msg;
    }
    
    // Switch Mode to show message
    showMessageMode = true;
    messageTimer = millis();
    inMenu = false; 
  }
  server.sendHeader("Location", "/"); 
  server.send(303);
}

// Handler for Buzzer Test
void handleBuzzerTest() {
  // Beep 3 times (Total ~3 seconds)
  for(int i=0; i<3; i++) {
    digitalWrite(BUZZER_PIN, HIGH); 
    delay(500);
    digitalWrite(BUZZER_PIN, LOW); 
    delay(500);
  }
  server.sendHeader("Location", "/"); 
  server.send(303);
}

// Function for Temperature Page
void handleWebTemp() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='5'>";
  html += "<style>body{font-family:sans-serif;text-align:center;background:#eee;padding:20px;}.box{background:white;padding:20px;border-radius:10px;}</style></head><body>";
  html += "<div class='box'><h1>LIVE WEATHER</h1>";
  if (isnan(h)) html += "<h2>Sensor Error!</h2>";
  else { html += "<h2>Temp: " + String(t, 1) + " C</h2><h2>Hum: " + String(h, 0) + " %</h2>"; }
  html += "<br><a href='/'><button style='padding:10px;background:#333;color:white;'>BACK</button></a></div></body></html>";
  server.send(200, "text/html", html);
}

// ==============================================================
//                    SETUP WIFI & ROUTES
// ==============================================================
void setupWifi() {
  WiFi.softAP(ssid, pass);
  
  server.on("/", handleRoot);
  
  // Register New Pages
  server.on("/msg", handleMessage);        
  server.on("/test_buzz", handleBuzzerTest); 
  
  server.on("/up", [](){ 
    if(inMenu) { menuIndex--; if(menuIndex < 0) menuIndex = 5; } 
    server.sendHeader("Location", "/"); server.send(303); 
  });
  
  server.on("/down", [](){ 
    if(inMenu) { menuIndex++; if(menuIndex > 5) menuIndex = 0; } 
    server.sendHeader("Location", "/"); server.send(303); 
  });
  
  server.on("/select", [](){ 
    inMenu = false; 
    server.sendHeader("Location", "/"); server.send(303); 
  });
  
  server.on("/exit", [](){ 
    inMenu = true; 
    showMessageMode = false; // Turn off message mode
    server.sendHeader("Location", "/"); server.send(303); 
  });
  
  server.on("/dev", [](){ 
    menuIndex = 5; inMenu = false; 
    server.sendHeader("Location", "/"); server.send(303); 
  });
  
  server.on("/view_temp", handleWebTemp);
  
  server.begin();
}

// ==============================================================
//                    DEV INFO DISPLAY
// ==============================================================
void runDevInfo() {
  display.clearDisplay(); 
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); 
  display.setCursor(0, 0); 
  display.println(F(">> DEVELOPER INFO <<"));
  display.drawLine(0, 10, 128, 10, WHITE);
  
  display.setCursor(0, 15); display.println(F("Dev: Akshit Negi"));
  display.setCursor(0, 27); display.println(F("Class: 10th"));
  display.setCursor(0, 39); display.println(F("Roll No: 06"));
  display.setCursor(0, 51); display.println(F("ID: akshitvip"));
  
  display.display(); 
  delay(1000); 
}

// ==============================================================
//               NEW FUNCTION: SHOW WEB MESSAGE
// ==============================================================
void runWebMessage() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); 
  display.setCursor(0,0); 
  display.println("MESSAGE FROM WEB:");
  display.drawLine(0, 10, 128, 10, WHITE);
  
  display.setTextSize(2); 
  display.setCursor(0, 25);
  display.println(lastWebMessage);
  
  display.display();
  delay(100);
}

// ==============================================================
//                    SENSOR LOGIC FUNCTIONS
// ==============================================================

// --- SOIL SENSOR ---
void runSoil() {
  int soil = analogRead(SOIL_PIN); 
  Serial.print("RAW VALUE: "); Serial.println(soil);
  
  display.clearDisplay(); 
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); 
  display.setCursor(80, 0); 
  display.print("V:"); 
  display.print(soil); 

  if (soil < 10) { 
    display.setTextSize(2); 
    display.setCursor(0, 20); 
    display.println("CHECK"); 
    display.println("WIRING");
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    if (soil < FLOOD_LIMIT) {  
      display.setTextSize(2); 
      display.setCursor(0, 0); 
      display.println("CONNECTED");
      display.setCursor(0, 30); 
      display.println("WET - FLOOD");
      
      // Tone Loop
      for(int i=0; i<50; i++) { 
        digitalWrite(BUZZER_PIN, HIGH); 
        delayMicroseconds(200); 
        digitalWrite(BUZZER_PIN, LOW); 
        delayMicroseconds(200); 
      }
    } else {
      display.setTextSize(2); 
      display.setCursor(0, 0); 
      display.println("CONNECTED");
      display.setCursor(0, 30); 
      display.println("DRY - SAFE");
      digitalWrite(BUZZER_PIN, LOW); 
    }
  }
  display.display(); 
  delay(100);
}

// --- GAS SENSOR ---
void runMQ2() {
  int mq = analogRead(MQ2_PIN);
  
  display.clearDisplay(); 
  display.setTextColor(SSD1306_WHITE);
  
  if (mq < 100) { 
    display.setTextSize(2); 
    display.setCursor(0, 20); 
    display.println("NOT"); 
    display.println("CONNECTED");
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    display.setTextSize(2); 
    display.setCursor(0, 0); 
    display.println("CONNECTED");
    if (mq > GAS_LIMIT) { 
      display.setCursor(0, 30); 
      display.println("GAS !");
      
      // Tone Loop
      for(int i=0; i<50; i++) { 
        digitalWrite(BUZZER_PIN, HIGH); 
        delayMicroseconds(200); 
        digitalWrite(BUZZER_PIN, LOW); 
        delayMicroseconds(200); 
      }
    } else {
      display.setCursor(0, 30); 
      display.println("SAFE");
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
  display.display(); 
  delay(100);
}

// --- DHT SENSOR ---
void runDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  display.clearDisplay(); 
  display.setTextColor(SSD1306_WHITE);
  
  if (isnan(h) || isnan(t)) {
    display.setTextSize(2); 
    display.setCursor(0, 20); 
    display.println("NOT"); 
    display.println("CONNECTED");
  } else {
    display.setTextSize(1); 
    display.setCursor(0, 0); 
    display.println("DHT CONNECTED");
    
    display.setTextSize(2); 
    display.setCursor(0, 20);
    display.print(t, 1); 
    display.println(" C");
    
    display.setCursor(0, 45);
    display.print(h, 0); 
    display.println(" %");
  }
  display.display(); 
  delay(2000);
}

// --- EARTHQUAKE SENSOR ---
void runMPU() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  float x = a.acceleration.x; 
  float y = a.acceleration.y;
  float dx = abs(x - lastX); 
  float dy = abs(y - lastY);
  
  // CHANGE 1: Sensitivity 1.5 se badha kar 2.5 kar di.
  // Ab chote-mote touch se trigger nahi hoga, zor se hilane par hi "moving" mana jayega.
  bool moving = (dx > 2.5 || dy > 2.5); 
  
  unsigned long now = millis();

  if (moving) { 
    if (moveStartTime == 0) moveStartTime = now; 
    
    // CHANGE 2: Time 2000 (2 sec) se kam karke 1200 (1.2 sec) kar diya
    if ((now - moveStartTime) >= 1200) earthquake = true; 
  } else { 
    moveStartTime = 0; 
    earthquake = false; 
  }
  lastX = x; 
  lastY = y; 

  if (earthquake) { 
    for(int i=0; i<50; i++) { 
      digitalWrite(BUZZER_PIN, HIGH); 
      delayMicroseconds(200); 
      digitalWrite(BUZZER_PIN, LOW); 
      delayMicroseconds(200); 
    } 
  } else { 
    digitalWrite(BUZZER_PIN, LOW); 
  }

  display.clearDisplay(); 
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); 
  display.setCursor(0, 0); 
  display.print("X: "); 
  display.println(x, 2);
  display.setCursor(0, 10); 
  display.print("Y: "); 
  display.println(y, 2);
  
  display.setTextSize(2); 
  display.setCursor(0, 40);
  if (earthquake) display.println("EARTHQUAKE");
  else display.println("STABLE");
  
  display.display(); 
  delay(200);
}
// --- GPS SENSOR ---
void runGPS() {
  // GPS data background mein read karta rahega
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  
  display.clearDisplay(); 
  display.setTextColor(SSD1306_WHITE);
  
  // Agar GPS signal VALID nahi hai (abhi search kar raha hai)
  if (!gps.location.isValid()) {
    // Yahan hum "WAIT" ki jagah Default Location dikhayenge
    display.setTextSize(1); 
    display.setCursor(0, 0); 
    display.println("DEFAULT LOCATION:"); // Header
    
    display.setTextSize(1); // Size adjust kar sakte ho
    display.setCursor(0, 15); 
    display.println("GSSS JEORI"); // Location Name
    
    // Jeori ke aas-paas ke coordinates (Hardcoded fake values jab tak real na mile)
    display.setCursor(0, 30); 
    display.print("LAT: 31.4982"); 
    
    display.setCursor(0, 45); 
    display.print("LON: 77.8054");
  } 
  // Agar GPS signal CONNECT ho gaya
  else {
    display.setTextSize(1); 
    display.setCursor(0, 0); 
    display.println("GPS CONNECTED"); // Real Signal
    
    display.setCursor(0, 20); 
    display.print("LAT: "); 
    display.println(gps.location.lat(), 6); // Real Latitude
    
    display.setCursor(0, 35); 
    display.print("LON: "); 
    display.println(gps.location.lng(), 6); // Real Longitude
  }
  
  display.display(); 
  delay(800);
}
// ==============================================================
//                    MAIN SETUP FUNCTION
// ==============================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  
  setupWifi();
  pinMode(BUZZER_PIN, OUTPUT);
  
  dht.begin();
  mpu.begin();
  
  // MPU6050 Sensitivity Settings
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  // Intro Screen
  display.clearDisplay(); 
  display.setTextSize(2); 
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20); 
  display.println("SYSTEM OK");
  display.display(); 
  delay(1000);
}

// ==============================================================
//                    MAIN LOOP FUNCTION
// ==============================================================
void loop() {
  server.handleClient(); 

  // --- SAFETY CHECK (Must run first for notifications) ---
  if (checkSafetyPriority()) {
    return; // Stop here if Alert
  }
  
  // --- NEW: CHECK WEB MESSAGE ---
  // Agar web se message aaya hai, to yahan se show karega
  if (showMessageMode) {
    runWebMessage();
    
    // 5 second baad wapis main menu
    if (millis() - messageTimer > 5000) {
      showMessageMode = false;
      inMenu = true;
    }
    return; // Stop here, don't show menu
  }

  // --- MENU LOGIC ---
  if (inMenu) {
    display.clearDisplay();
    display.setTextSize(2); 
    display.setCursor(0, 0); 
    display.println("MAIN MENU");
    display.drawLine(0, 16, 128, 16, WHITE);
    
    display.setTextSize(1); 
    display.setCursor(0, 25); 
    display.println("Select Sensor:");
    
    display.setTextSize(2); 
    display.setCursor(10, 40); 
    display.print("> "); 
    display.println(menuItems[menuIndex]);
    
    display.display();
    digitalWrite(BUZZER_PIN, LOW); 
    delay(100); 
  } 
  else {
    // Run the selected function
    if (menuIndex == 0) runSoil();
    else if (menuIndex == 1) runMQ2();
    else if (menuIndex == 2) runDHT();
    else if (menuIndex == 3) runMPU();
    else if (menuIndex == 4) runGPS();
    else if (menuIndex == 5) runDevInfo();
  }
}
