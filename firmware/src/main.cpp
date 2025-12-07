#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// ==========================================
// CẤU HÌNH CẢM BIẾN & THUẬT TOÁN
// ==========================================
MAX30105 particleSensor;

#define MAX_BRIGHTNESS 255

// ==========================================
// CẤU HÌNH OLED DISPLAY
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define OLED_SDA 4
#define OLED_SCL 5

TwoWire I2C_OLED = TwoWire(1);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_OLED, OLED_RESET);

// Buffer lưu dữ liệu thô
uint32_t irBuffer[100];
uint32_t redBuffer[100];
int32_t bufferLength = 100;

// Biến lưu kết quả đo
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

// Biến làm mượt (EMA - Exponential Moving Average)
#define EMA_ALPHA 0.25 // Smoothing factor (0.0 - 1.0): lower = smoother, higher = more responsive
float emaHR = 0.0;
float emaSPO2 = 0.0;
bool emaInitialized = false;

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(512); // Buffer lớn chút cho JSON

// Hàm cập nhật EMA (Exponential Moving Average)
// Formula: EMA_new = alpha × current_value + (1 - alpha) × EMA_previous
void updateEMA(float &ema, int32_t newValue, bool isValid, int32_t minValue, int32_t maxValue)
{
  // Validate the new value
  bool reallyValid = isValid && newValue >= minValue && newValue <= maxValue && newValue != -999;

  if (reallyValid)
  {
    if (!emaInitialized)
    {
      // First valid reading: initialize EMA with this value
      ema = (float)newValue;
      emaInitialized = true;
    }
    else
    {
      // Apply EMA formula
      ema = EMA_ALPHA * newValue + (1.0 - EMA_ALPHA) * ema;
    }
  }
}

int32_t getEMA(float ema)
{
  return emaInitialized ? (int32_t)(ema + 0.5) : 0; // Round to nearest integer
}

// ==========================================
// CÁC HÀM KẾT NỐI MẠNG
// ==========================================
void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
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

void publishMessage(int hr, int sp)
{
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
// HÀM CẬP NHẬT OLED DISPLAY
// ==========================================
void updateDisplay(int hr, int spo2, bool fingerDetected)
{
  display.clearDisplay();

  if (!fingerDetected)
  {
    // Hiển thị thông báo không có ngón tay
    display.setTextSize(1);
    display.setCursor(20, 0);
    display.println("Health Monitor");
    display.setTextSize(2);
    display.setCursor(10, 12);
    display.println("No Finger");
  }
  else if (hr == 0 || spo2 == 0)
  {
    // Đang đo...
    display.setTextSize(1);
    display.setCursor(20, 0);
    display.println("Health Monitor");
    display.setTextSize(2);
    display.setCursor(5, 12);
    display.println("Reading...");
  }
  else
  {
    // Hiển thị kết quả - Bố cục ngang (HR | SpO2)
    // Line 1: Labels
    display.setTextSize(1);
    display.setCursor(10, 0);
    display.print("HR");
    display.setCursor(80, 0);
    display.print("SpO2");

    // Line 2-3: Values
    display.setTextSize(2);
    display.setCursor(0, 12);
    display.print(hr);

    display.setTextSize(1);
    display.setCursor(35, 18);
    display.print("bpm");

    display.setTextSize(2);
    display.setCursor(68, 12);
    display.print(spo2);

    display.setTextSize(1);
    display.setCursor(110, 18);
    display.print("%");
  }

  display.display();
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup()
{
  Serial.begin(115200);

  // 1. Khởi động OLED
  I2C_OLED.begin(OLED_SDA, OLED_SCL, 100000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println("OLED not found!");
    while (1)
      ;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // 2. Khởi động cảm biến MAX30105
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST))
  {
    Serial.println("Sensor not found!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Sensor Error!");
    display.display();
    while (1)
      ;
  }

  byte ledBrightness = 20;
  byte sampleAverage = 4;
  byte ledMode = 2;
  byte sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  // 3. Kết nối mạng
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();
  connectAWS();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Ready!");
  display.display();
  delay(1000);

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
    for (int i = 75; i < 100; i++)
      avgIR += irBuffer[i];
    avgIR /= 25;

    if (avgIR < 50000)
    {
      Serial.println("No finger!");
      // Reset EMA
      emaHR = 0.0;
      emaSPO2 = 0.0;
      emaInitialized = false;
      // Cập nhật màn hình: Không có ngón tay
      updateDisplay(0, 0, false);
    }
    else
    {
      // Có tay -> Cập nhật EMA
      updateEMA(emaHR, heartRate, validHeartRate, 40, 180); // HR: 40-180 bpm
      updateEMA(emaSPO2, spo2, validSPO2, 70, 100);         // SpO2: 70-100%

      int finalHR = getEMA(emaHR);
      int finalSpO2 = getEMA(emaSPO2);

      if (finalHR > 0 && finalSpO2 > 0)
      {
          Serial.print("IR: "); Serial.print(avgIR);
  Serial.print(" | Red: "); Serial.println(redBuffer[99]);
  Serial.print("Raw HR: "); Serial.print(heartRate);
  Serial.print(" (Valid: "); Serial.print(validHeartRate);
  Serial.print(") | Raw SpO2: "); Serial.print(spo2);
  Serial.print(" (Valid: "); Serial.print(validSPO2); Serial.println(")");
        Serial.print("HR: ");
        Serial.print(finalHR);
        Serial.print(" | SpO2: ");
        Serial.println(finalSpO2);
        // Cập nhật màn hình với dữ liệu hợp lệ
        updateDisplay(finalHR, finalSpO2, true);

        // Gửi lên AWS (Chỉ gửi khi số liệu ổn định)
        publishMessage(finalHR, finalSpO2);
 
      }
      else
      {
        // Có tay nhưng chưa có dữ liệu ổn định
        updateDisplay(0, 0, true);
      }
    }

    // Xử lý mạng trong vòng lặp con này luôn
    client.loop();
    if (!client.connected()) connectAWS();
  }
}
