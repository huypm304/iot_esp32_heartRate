#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
//#include "secrets.h" 
#include <Wire.h>
#include "MAX30105.h" 
#include "heartRate.h"

#define AWS_IOT_PUBLISH_TOPIC   "health/monitor"

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(256);


void setup(){

}

void readSensorData() {
  //  Function to read data from MAX30102 sensor
}

void loop() {
  // put your main code here, to run repeatedly:
  client.loop();//MQTT connect loop
}

// put function definitions here:
void publishMessage(){

}

void connectWiFi(){

}