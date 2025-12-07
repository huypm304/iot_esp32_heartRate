// ============================================
// CUSTOM HR ALGORITHM - Bỏ Maxim, tự tính!
// ============================================

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include <Wire.h>
#include "MAX30105.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

MAX30105 particleSensor;

// OLED Setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define OLED_SDA 4
#define OLED_SCL 5

TwoWire I2C_OLED = TwoWire(1);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_OLED, OLED_RESET);

// ============================================
// CUSTOM PEAK DETECTION PARAMETERS
// ============================================
#define BUFFER_SIZE 400          // 2 seconds at 100Hz
#define PEAK_MIN_HEIGHT 0.6      // Peak phải cao hơn 60% baseline
#define PEAK_MIN_DISTANCE 40     // Min 40 samples giữa 2 peaks (600ms) → max 100 bpm
#define PEAK_MAX_DISTANCE 150    // Max 150 samples (1.5s) → min 40 bpm

#define HR_MIN 40
#define HR_MAX 140
#define SPO2_MIN 70
#define SPO2_MAX 100

// Buffers
uint32_t irBuffer[BUFFER_SIZE];
int bufferIndex = 0;
bool bufferFilled = false;

// Peak detection state
uint32_t lastPeakTime = 0;
int peakCount = 0;
uint32_t rrIntervals[10]; // Store last 10 RR intervals
int rrIndex = 0;

// Results
float currentHR = 0;
float currentSpO2 = 98; // Default SpO2 (không tính chính xác, chỉ ước lượng)

// Smoothing
float emaHR = 0;
bool emaInitialized = false;
#define EMA_ALPHA 0.3

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(512);

// ============================================
// SIGNAL PREPROCESSING
// ============================================
float getBaseline(uint32_t *buffer, int size) {
  uint64_t sum = 0;
  for (int i = 0; i < size; i++) {
    sum += buffer[i];
  }
  return (float)sum / size;
}

float getStdDev(uint32_t *buffer, int size, float mean) {
  float variance = 0;
  for (int i = 0; i < size; i++) {
    float diff = buffer[i] - mean;
    variance += diff * diff;
  }
  return sqrt(variance / size);
}

// Simple Moving Average Filter
void smoothBuffer(uint32_t *buffer, int size) {
  uint32_t temp[BUFFER_SIZE];
  int window = 5; // 5-point moving average
  
  for (int i = 0; i < size; i++) {
    uint64_t sum = 0;
    int count = 0;
    
    for (int j = -window/2; j <= window/2; j++) {
      int idx = i + j;
      if (idx >= 0 && idx < size) {
        sum += buffer[idx];
        count++;
      }
    }
    temp[i] = sum / count;
  }
  
  // Copy back
  for (int i = 0; i < size; i++) {
    buffer[i] = temp[i];
  }
}
// ============================================
// CUSTOM PEAK DETECTION
// ============================================
void detectPeaks() {
  if (!bufferFilled) return;
  
  smoothBuffer(irBuffer, BUFFER_SIZE);
  // Calculate baseline and threshold
  float baseline = getBaseline(irBuffer, BUFFER_SIZE);
  float stdDev = getStdDev(irBuffer, BUFFER_SIZE, baseline);
  float threshold = baseline + (stdDev * PEAK_MIN_HEIGHT);
  
  // Find peaks
  int validPeaks = 0;
  uint32_t peakTimes[20]; // Max 20 peaks in 2 seconds
  
  for (int i = 5; i < BUFFER_SIZE - 5; i++) {
    // Check if this is a local maximum
    if (irBuffer[i] > irBuffer[i-1] && 
        irBuffer[i] > irBuffer[i+1] &&
        irBuffer[i] > irBuffer[i-2] &&
        irBuffer[i] > irBuffer[i+2] &&
        irBuffer[i] > threshold) {
      
      // Check distance from last peak
      if (validPeaks == 0 || 
          (i - peakTimes[validPeaks-1]) >= PEAK_MIN_DISTANCE) {
        
        if (validPeaks == 0 || 
            (i - peakTimes[validPeaks-1]) <= PEAK_MAX_DISTANCE) {
          
          peakTimes[validPeaks] = i;
          validPeaks++;
          
          if (validPeaks >= 20) break;
        }
      }
    }
  }
  
  // Calculate HR from peaks
  if (validPeaks >= 3) {
    // Calculate average RR interval
    float totalRR = 0;
    int rrCount = 0;
    
    for (int i = 1; i < validPeaks; i++) {
      uint32_t rr = peakTimes[i] - peakTimes[i-1];
      
      // Validate RR interval
      if (rr >= PEAK_MIN_DISTANCE && rr <= PEAK_MAX_DISTANCE) {
        totalRR += rr;
        rrCount++;
      }
    }
    
    if (rrCount > 0) {
      float avgRR = totalRR / rrCount;
      
      // Convert to BPM
      // avgRR is in samples, sample rate = 100Hz
      // So avgRR samples = avgRR/100 seconds
      // HR = 60 / (avgRR/100) = 6000 / avgRR
      float calculatedHR = 6000.0 / avgRR;
      
      // Validate HR
      if (calculatedHR >= HR_MIN && calculatedHR <= HR_MAX) {
        currentHR = calculatedHR;
        
        // Update EMA
        if (!emaInitialized) {
          emaHR = currentHR;
          emaInitialized = true;
        } else {
          emaHR = EMA_ALPHA * currentHR + (1.0 - EMA_ALPHA) * emaHR;
        }
        
        Serial.print("✓ Peaks found: "); Serial.print(validPeaks);
        Serial.print(" | Avg RR: "); Serial.print(avgRR);
        Serial.print(" samples | Raw HR: "); Serial.print(calculatedHR, 1);
        Serial.print(" | Smoothed: "); Serial.println(emaHR, 1);
      } else {
        Serial.print("✗ Invalid HR: "); Serial.println(calculatedHR);
      }
    }
  } else {
    Serial.print("✗ Too few peaks: "); Serial.println(validPeaks);
  }
}

// ============================================
// SIMPLE SPO2 ESTIMATION
// ============================================
float estimateSpO2() {
  // Simplified SpO2 estimation based on IR/RED ratio
  // This is NOT accurate, just for display purposes
  
  uint64_t sumIR = 0, sumRed = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    sumIR += irBuffer[i];
  }
  
  // Read some RED samples for ratio
  uint32_t redSamples[10];
  for (int i = 0; i < 10; i++) {
    while (particleSensor.available() == false) 
      particleSensor.check();
    redSamples[i] = particleSensor.getRed();
    particleSensor.nextSample();
  }
  
  uint64_t sumRedLocal = 0;
  for (int i = 0; i < 10; i++) {
    sumRedLocal += redSamples[i];
  }
  
  float avgIR = sumIR / BUFFER_SIZE;
  float avgRed = sumRedLocal / 10;
  
  // Simple ratio-based estimation
  float ratio = avgRed / avgIR;
  
  // Empirical formula (very rough approximation)
  float spo2 = 120 - 25 * ratio;
  
  // Clamp to valid range
  if (spo2 < SPO2_MIN) spo2 = SPO2_MIN;
  if (spo2 > SPO2_MAX) spo2 = SPO2_MAX;
  
  return spo2;
}

// ============================================
// NETWORK FUNCTIONS
// ============================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

void connectAWS() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing time");
  while (time(nullptr) < 1000000000l) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nTime synced!");

  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
  client.begin(MQTT_HOST, 8883, net);

  Serial.print("Connecting AWS");
  String clientId = "ESP32_" + String(random(0xffff), HEX);
  
  while (!client.connect(clientId.c_str())) {
    Serial.print(".");
    delay(500);
  }
  
  if (!client.connected()) {
    Serial.println("Timeout!");
    return;
  }
  Serial.println("\nAWS Connected!");
}

void publishMessage(int hr, int sp) {
  StaticJsonDocument<200> doc;
  doc["device_id"] = "ESP32_01";
  doc["heart_rate"] = hr;
  doc["spo2"] = sp;
  doc["timestamp"] = millis();
  doc["algorithm"] = "custom_peak";
  
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  
  client.publish(MQTT_TOPIC, jsonBuffer);
  Serial.print("📤 AWS: ");
  Serial.println(jsonBuffer);
}

// ============================================
// DISPLAY UPDATE
// ============================================
void updateDisplay(int hr, int spo2, bool fingerDetected) {
  display.clearDisplay();
  
  if (!fingerDetected) {
    display.setTextSize(1);
    display.setCursor(15, 0);
    display.println("Health Monitor");
    display.setTextSize(2);
    display.setCursor(10, 12);
    display.println("No Finger");
  } else if (hr == 0) {
    display.setTextSize(1);
    display.setCursor(15, 0);
    display.println("Health Monitor");
    display.setTextSize(2);
    display.setCursor(5, 12);
    display.println("Reading...");
  } else {
    // Display results
    display.setTextSize(1);
    display.setCursor(10, 0);
    display.print("HR");
    display.setCursor(80, 0);
    display.print("SpO2");
    
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

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Custom HR Algorithm ===");
  
  // OLED
  I2C_OLED.begin(OLED_SDA, OLED_SCL, 100000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED not found!");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();
  
  // Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Sensor not found!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Sensor Error!");
    display.display();
    while (1);
  }
  
  // Optimal settings
  byte ledBrightness = 50;
  byte sampleAverage = 4;
  byte ledMode = 2;
  byte sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  
  // Network
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
  
  Serial.println("System Ready!");
  Serial.println("Using custom peak detection");
  Serial.println("Place finger on sensor...\n");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  client.loop();
  if (!client.connected()) connectAWS();
  
  // Read new sample
  while (particleSensor.available() == false)
    particleSensor.check();
  
  irBuffer[bufferIndex] = particleSensor.getIR();
  particleSensor.nextSample();
  
  bufferIndex++;
  
  // Check if buffer is full
  if (bufferIndex >= BUFFER_SIZE) {
    bufferIndex = 0;
    bufferFilled = true;
  }
  
  // Process every 2 seconds (when buffer is full)
  if (bufferFilled && bufferIndex == 0) {
    // Check for finger presence
    float avgIR = getBaseline(irBuffer, BUFFER_SIZE);
    
    Serial.println("\n━━━━━━━━━━━━━━━━━━━━");
    Serial.print("Avg IR: "); Serial.println(avgIR);
    
    if (avgIR < 50000) {
      Serial.println("No finger detected");
      emaHR = 0;
      emaInitialized = false;
      updateDisplay(0, 0, false);
    } else {
      Serial.println("Finger detected");
      
      static float lastAvgIR = 0;
  float irChange = abs(avgIR - lastAvgIR) / lastAvgIR;
  
  if (lastAvgIR == 0) {
    lastAvgIR = avgIR;
  }
  
  if (irChange > 0.10) { // IR changed > 10%
    Serial.print("Signal unstable (IR changed ");
    Serial.print(irChange * 100, 1);
    Serial.println("%), warming up...");
    updateDisplay(0, 0, true); // Show "Reading..."
    lastAvgIR = avgIR;
    emaHR = 0; // Reset EMA
    emaInitialized = false;
    return; // Skip this cycle
  }
      // Detect peaks and calculate HR
      detectPeaks();
      
      // Estimate SpO2
      currentSpO2 = estimateSpO2();
      
      // Get final values
      int finalHR = (int)(emaHR + 0.5);
      int finalSpO2 = (int)(currentSpO2 + 0.5);
      
      if (finalHR >= HR_MIN && finalHR <= HR_MAX) {
        Serial.print("Final HR: "); Serial.print(finalHR);
        Serial.print(" bpm | SpO2: "); Serial.print(finalSpO2);
        Serial.println("%");
        
        updateDisplay(finalHR, finalSpO2, true);
        publishMessage(finalHR, finalSpO2);
        
        delay(3000); // Send every 3 seconds
      } else {
        Serial.println("Waiting for stable reading...");
        updateDisplay(0, 0, true);
      }
    }
  }
}