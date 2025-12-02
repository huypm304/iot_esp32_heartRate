#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "secrets.h" 
#include <Wire.h>
#include "MAX30105.h" 
#include "spo2_algorithm.h" 
// ==========================================
// CẤU HÌNH CẢM BIẾN & THUẬT TOÁN
// ==========================================
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

// ==========================================
// CẤU HÌNH MẠNG (AWS & MQTT)
// ==========================================
WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(512); // Buffer lớn chút cho JSON

// Hàm làm mượt dữ liệu (Lấy từ code của bạn)
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

// ==========================================
// CÁC HÀM KẾT NỐI MẠNG
// ==========================================
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
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // Đồng bộ giờ quốc tế
  Serial.print("Dang cap nhat gio");
  while (time(nullptr) < 1000000000l) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nDa cap nhat gio xong!");
  // ----------------------------------

  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.begin(MQTT_HOST, 8883, net);

  Serial.print("Connecting AWS");
  while (!client.connect("ESP32_Health_Device")) {
    Serial.print("."); delay(100);
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

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  // 1. Khởi động cảm biến
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Sensor not found!");
    while (1);
  }

  // Cấu hình tối ưu (Lấy từ code của bạn)
  byte ledBrightness = 60;
  byte sampleAverage = 4;
  byte ledMode = 2;
  byte sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  // 2. Kết nối mạng
  connectAWS();
  
  Serial.println("System Ready! Place finger on sensor.");
}

void loop() {
  // Giữ kết nối mạng
  client.loop();
  if (!client.connected()) connectAWS();

  // --- BƯỚC 1: Thu thập 100 mẫu đầu tiên (Mất 1 giây) ---
  for (byte i = 0; i < bufferLength; i++) {
    while (particleSensor.available() == false) 
      particleSensor.check();

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }

  // --- BƯỚC 2: Tính toán & Cập nhật liên tục ---
  while (1) {
    // Dịch chuyển 25 mẫu cũ ra ngoài (Cửa sổ trượt)
    for (byte i = 25; i < 100; i++) {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    // Đọc thêm 25 mẫu mới
    for (byte i = 75; i < 100; i++) {
      while (particleSensor.available() == false) 
        particleSensor.check();

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    // Tính toán lại HR & SpO2
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

    // Kiểm tra có tay không?
    long avgIR = 0;
    for(int i=75; i<100; i++) avgIR += irBuffer[i];
    avgIR /= 25;

    if (avgIR < 50000) {
      Serial.println("No finger!");
      // Reset lịch sử làm mượt
      for(int i=0; i<SMOOTHING_WINDOW; i++) { hrHistory[i]=0; spo2History[i]=0; }
    } else {
      // Có tay -> Thêm vào lịch sử & Làm mượt
      addReading(heartRate, validHeartRate, spo2, validSPO2);
      
      int finalHR = getSmoothedAverage(hrHistory);
      int finalSpO2 = getSmoothedAverage(spo2History);

      if (finalHR > 0 && finalSpO2 > 0) {
        Serial.print("HR: "); Serial.print(finalHR);
        Serial.print(" | SpO2: "); Serial.println(finalSpO2);
        
        // Gửi lên AWS (Chỉ gửi khi số liệu ổn định)
        publishMessage(finalHR, finalSpO2);
      }
    }
    
    // Xử lý mạng trong vòng lặp con này luôn
    client.loop();
    if (!client.connected()) connectAWS();
  }
}