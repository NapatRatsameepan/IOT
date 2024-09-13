#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <BH1750.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // ไลบรารี JSON

#define PIN 17
#define NUMPIXELS 12

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
BH1750 lightMeter;

const int relayPin = 14;
int soilMoisturePin = 36;
int soilMoistureValue = 0;
int soilMoisturePercent = 0;
const int upperthreshold = 70;
const int lowerthreshold = 30;
const int trigPin = 13;
const int echoPin = 12;

long duration;
int distance;
int waterLevel;
const float luxThreshold = 20;

const char* ssid = "Redmi Note 11 Pro";
const char* pass = "Napat123";
const String writeAPIKey = "GOOH18P0AJ94WUTR";
const String readAPIKey = "0OZ3Q0LV7PDDHRX7";
const String channelID = "2656084";

bool pumpState = false;
bool neoPixelState = false;
bool manualPumpControl = false; // ปุ่มสำหรับควบคุมปั๊มน้ำ
bool manualLightControl = false; // ปุ่มสำหรับควบคุมไฟ LED

void setup() {
  Serial.begin(115200);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // ปิดปั๊มน้ำเริ่มต้น
  pinMode(soilMoisturePin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");

  Wire.begin();
  lightMeter.begin();

  pixels.begin();
  pixels.show();
}

void loop() {
  // ดึงข้อมูลจาก ThingSpeak เพื่อควบคุมอุปกรณ์
  receiveControlDataFromThingSpeak();

  // ส่งค่าจากเซ็นเซอร์ไปยัง ThingSpeak
  sendSensorDataToThingSpeak();

  // หากไม่มีการกดปุ่ม จะให้ทำงานแบบอัตโนมัติ
  if (!manualPumpControl && !manualLightControl) {
    autoControlDevices(); // ใช้เซ็นเซอร์ควบคุมอุปกรณ์ในโหมด Auto
  } else {
    manualControlDevices(); // ใช้ปุ่มควบคุมอุปกรณ์
  }

  delay(20000); // ThingSpeak update interval (15 seconds)
}

void sendSensorDataToThingSpeak() {
  // อ่านค่าจากเซนเซอร์
  soilMoistureValue = analogRead(soilMoisturePin);
  soilMoisturePercent = map(soilMoistureValue, 0, 4095, 100, 0);

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2;
  waterLevel = map(distance, 0, 11, 10, 0);

  float lux = lightMeter.readLightLevel();
  int luxPercentage = map(lux, 0, 80, 0, 100);
  luxPercentage = constrain(luxPercentage, 0, 100);

  // ส่งข้อมูลไปยัง Field 1, 2, 3 ของ ThingSpeak
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.thingspeak.com/update?api_key=" + writeAPIKey +
                 "&field1=" + String(soilMoisturePercent) +
                 "&field2=" + String(luxPercentage) +
                 "&field3=" + String(waterLevel);
    
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println(httpCode);
      Serial.println(payload);
    } else {
      Serial.println("Error on HTTP request");
    }
    http.end();
  }

  // แสดงข้อมูลที่ถูกส่งไปยัง Serial Monitor
  Serial.print("Soil Moisture Value: ");
  Serial.println(soilMoisturePercent);
  Serial.print("Water Level: ");
  Serial.println(waterLevel);
  Serial.print("Light Level (Lux): ");
  Serial.println(luxPercentage);
}

void receiveControlDataFromThingSpeak() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.thingspeak.com/channels/" + channelID + "/feeds.json?api_key=" + readAPIKey + "&results=1";
    
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("Received control data:");
      Serial.println(payload);

      // ใช้ ArduinoJson เพื่อแปลง JSON
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      }

      // ดึงข้อมูลจาก feeds field
      JsonObject feed = doc["feeds"][0];

      const char* pumpControl = feed["field4"];
      const char* ledControl = feed["field5"];

      // ตรวจสอบการควบคุมปั๊ม
      if (pumpControl != nullptr) {
        manualPumpControl = true; // ระบุว่ามีการกดปุ่ม
        pumpState = (String(pumpControl) == "1");
        Serial.print("Pump Control Value: ");
        Serial.println(pumpControl);
      } else {
        manualPumpControl = false; // ไม่ได้มีการกดปุ่ม
        Serial.println("Pump Control is null.");
      }

      // ตรวจสอบการควบคุมไฟ
      if (ledControl != nullptr) {
        manualLightControl = true; // ระบุว่ามีการกดปุ่ม
        neoPixelState = (String(ledControl) == "1");
        Serial.print("LED Control Value: ");
        Serial.println(ledControl);
      } else {
        manualLightControl = false; // ไม่ได้มีการกดปุ่ม
        Serial.println("LED Control is null.");
      }
    } else {
      Serial.println("Error on HTTP request");
    }
    http.end();
  }
}

// ฟังก์ชันควบคุมอุปกรณ์ในโหมด Auto
void autoControlDevices() {
  Serial.println("Auto Mode Active");

  soilMoistureValue = analogRead(soilMoisturePin);
  soilMoisturePercent = map(soilMoistureValue, 0, 4095, 100, 0);

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2;
  waterLevel = map(distance, 0, 11, 10, 0);

  float lux = lightMeter.readLightLevel();
  int luxPercentage = map(lux, 0, 80, 0, 100);
  luxPercentage = constrain(luxPercentage, 0, 100);

  // Auto-control for NeoPixel based on light level
  if (luxPercentage < luxThreshold) {
    pixels.fill(pixels.Color(0, 0, 255));  // เปิดไฟ LED สีน้ำเงิน
    Serial.println("LED ON (Auto)");
  } else {
    pixels.clear();  // ปิดไฟ LED
    Serial.println("LED OFF (Auto)");
  }
  pixels.show();

  // Auto-control for pump based on soil moisture
  if (soilMoisturePercent <= lowerthreshold) {
    pumpState = true;
  } else if (soilMoisturePercent >= upperthreshold) {
    pumpState = false;
  }

  if (pumpState) {
    digitalWrite(relayPin, LOW);  // ปั๊มเปิด
    Serial.println("Pump ON (Auto)");
  } else {
    digitalWrite(relayPin, HIGH);  // ปั๊มปิด
    Serial.println("Pump OFF (Auto)");
  }
}

// ฟังก์ชันควบคุมอุปกรณ์ด้วยปุ่ม
void manualControlDevices() {
  Serial.println("Manual Control Active");

  // ควบคุมปั๊ม
  if (manualPumpControl) {
    if (pumpState) {
      digitalWrite(relayPin, LOW);  // ปั๊มเปิด
      Serial.println("Pump ON (Manual)");
    } else {
      digitalWrite(relayPin, HIGH);  // ปั๊มปิด
      Serial.println("Pump OFF (Manual)");
    }
  }

  // ควบคุมไฟ LED
  if (manualLightControl) {
    if (neoPixelState) {
      pixels.fill(pixels.Color(0, 0, 255));  // เปิดไฟ LED สีน้ำเงิน
      Serial.println("LED ON (Manual)");
    } else {
      pixels.clear();  // ปิดไฟ LED
      Serial.println("LED OFF (Manual)");
    }
    pixels.show();
  }
}
