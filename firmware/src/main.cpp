#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "secrets.h" 
#include <Wire.h>
#include "MAX30105.h" 
#include "spo2_algorithm.h" 

MAX30105 particleSensor;

#define MAX_BRIGHTNESS 255

// Buffer lưu dữ liệu thô
uint32_t irBuffer[100]; 
uint32_t redBuffer[100];
int32_t bufferLength = 100;

// Biến lưu kết quả đo
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

// Biến làm mượt (Smoothing)
#define SMOOTHING_WINDOW 10
int32_t hrHistory[SMOOTHING_WINDOW] = {0};
int32_t spo2History[SMOOTHING_WINDOW] = {0};
int historyIndex = 0;

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(512); // Buffer lớn chút cho JSON

void addReading(int32_t newHR, bool hrValid, int32_t newSPO2, bool spo2Valid) {
  bool hrReallyValid = hrValid && newHR > 40 && newHR < 180 && newHR != -999;
  bool spo2ReallyValid = spo2Valid && newSPO2 > 70 && newSPO2 <= 100 && newSPO2 != -999;

  hrHistory[historyIndex] = hrReallyValid ? newHR : 0;
  spo2History[historyIndex] = spo2ReallyValid ? newSPO2 : 0;
  historyIndex = (historyIndex + 1) % SMOOTHING_WINDOW;
}

int32_t getSmoothedAverage(int32_t history[]) {
  int32_t sum = 0;
  int count = 0;
  for (int i = 0; i < SMOOTHING_WINDOW; i++) {
    if (history[i] > 0) {
      sum += history[i];
      count++;
    }
  }
  return count > 0 ? sum / count : 0;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

void connectAWS() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  // --- BẮT ĐẦU SỬA: THÊM ĐỒNG BỘ GIỜ ---
  // Phải có dòng này thì ESP32 mới biết giờ hiện tại
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); 

  Serial.print("Dang cap nhat gio");
  // Chờ đến khi cập nhật giờ thành công (năm > 2001)
  while (time(nullptr) < 1000000000l) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nDa cap nhat gio xong!");
  // --- KẾT THÚC SỬA ---

  // Nạp chứng chỉ (Đảm bảo dùng đúng biến trong secrets.h)
  net.setCACert(AWS_CERT_CA); 
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.begin(MQTT_HOST, 8883, net);

  Serial.print("Connecting AWS");
  
  // Đổi tên Client ID ngẫu nhiên để tránh bị trùng lặp (nếu đang mở web test)
  String clientId = "ESP32_Huy_" + String(random(0xffff), HEX);
  
  while (!client.connect(clientId.c_str())) { 
    Serial.print("."); delay(500);
  }
  
  if(!client.connected()){
    Serial.println("Timeout!"); return;
  }
  Serial.println("\nAWS Connected!");
}

void publishMessage(int hr, int sp) {
  StaticJsonDocument<200> doc;
  doc["device_id"] = "ESP32_01";
  doc["heart_rate"] = hr;
  doc["spo2"] = sp;
  doc["timestamp"] = millis();
  
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  
  // Gửi lên Topic (Lấy từ secrets.h)
  client.publish(MQTT_TOPIC, jsonBuffer);
  Serial.print(">> Sent to AWS: ");
  Serial.println(jsonBuffer);
}

void setup() {
  Serial.begin(115200);
  
  // 1. Khởi động cảm biến
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Sensor not found!");
    while (1);
  }

  byte ledBrightness = 20;
  byte sampleAverage = 16;
  byte ledMode = 2;
  byte sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 16384;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  connectWiFi();
  // 2. Kết nối mạng
  connectAWS();
  
  Serial.println("System Ready! Place finger on sensor.");
}

void loop() {
  // 1. Maintain Network Connection
  client.loop();
  if (!client.connected()) connectAWS();

  // 2. Initial Buffer Filling (Only runs once at startup or reset)
  // We use a static flag to track if buffer is full
  static bool bufferFilled = false;

  if (!bufferFilled) {
    // Fill the first 100 samples
    for (byte i = 0; i < bufferLength; i++) {
      while (particleSensor.available() == false) 
        particleSensor.check();

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }
    bufferFilled = true; // Mark as filled so we don't do this again
  } 
  else {
    // 3. Continuous Processing (Sliding Window)
    
    // Shift the last 75 samples to the beginning
    for (byte i = 25; i < 100; i++) {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    // Read 25 new samples to fill the end
    for (byte i = 75; i < 100; i++) {
      while (particleSensor.available() == false) 
        particleSensor.check();

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    // Run Algorithm
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

    // Check for finger
    long avgIR = 0;
    for(int i=75; i<100; i++) avgIR += irBuffer[i];
    avgIR /= 25;

    if (avgIR < 50000) {
      Serial.println("No finger!");
      // Reset smoothing history
      for(int i=0; i<SMOOTHING_WINDOW; i++) { hrHistory[i]=0; spo2History[i]=0; }
    } else {
      // Add reading and smooth
      addReading(heartRate, validHeartRate, spo2, validSPO2);
      
      int finalHR = getSmoothedAverage(hrHistory);
      int finalSpO2 = getSmoothedAverage(spo2History);

      if (finalHR > 0 && finalSpO2 > 0) {
        Serial.print("HR: "); Serial.print(finalHR);
        Serial.print(" | SpO2: "); Serial.println(finalSpO2);
        
        // Send to AWS
        publishMessage(finalHR, finalSpO2);
  //     if (finalHR > 0 && finalSpO2 > 0) {
    
  // Serial.println("=== DEBUG ===");
  // Serial.print("IR: "); Serial.print(avgIR);
  // Serial.print(" | Red: "); Serial.println(redBuffer[99]);
  // Serial.print("Raw HR: "); Serial.print(heartRate);
  // Serial.print(" (Valid: "); Serial.print(validHeartRate);
  // Serial.print(") | Raw SpO2: "); Serial.print(spo2);
  // Serial.print(" (Valid: "); Serial.print(validSPO2); Serial.println(")");
  // Serial.print("=> Final HR: "); Serial.print(finalHR);
  // Serial.print(" | Final SpO2: "); Serial.println(finalSpO2);
  // Serial.println("=============");

  // publishMessage(finalHR, finalSpO2);
      }
    }
  }
}