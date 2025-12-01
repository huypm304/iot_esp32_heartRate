provider "aws" {
  region = var.aws_region
}

terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

# 1. Create the IoT Thing
resource "aws_iot_thing" "esp32" {
  name = "${var.project_name}"
}

# 2. Create the Certificate
resource "aws_iot_certificate" "cert" {
  active = true
}

# 3. Attach Certificate to Thing
resource "aws_iot_thing_principal_attachment" "att_thing" {
  principal = aws_iot_certificate.cert.arn
  thing     = aws_iot_thing.esp32.name
}

# 4. Create IoT Policy (Permissions for the device)
resource "aws_iot_policy" "pubsub" {
  name = "${var.project_name}_Policy"

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Action = [
          "iot:Connect",
          "iot:Publish",
          "iot:Subscribe",
          "iot:Receive"
        ]
        Effect   = "Allow"
        Resource = "*"
      }
    ]
  })
}

# 5. Attach Policy to Certificate
resource "aws_iot_policy_attachment" "att_policy" {
  policy = aws_iot_policy.pubsub.name
  target = aws_iot_certificate.cert.arn
}



data "aws_iot_endpoint" "endpoint" {
  endpoint_type = "iot:Data-ATS"
}

resource "local_file" "secrets" {
  filename = "../firmware/src/secrets.h"
  content  = <<EOF
#ifndef SECRETS_H
#define SECRETS_H

// WiFi Config
const char WIFI_SSID[] = "${var.wifi_ssid}";
const char WIFI_PASSWORD[] = "${var.wifi_password}";

// MQTT Config
const char MQTT_HOST[] = "${data.aws_iot_endpoint.endpoint.endpoint_address}";
const char MQTT_TOPIC[] = "health/monitor";

// Certificates (Auto-generated)
const char AWS_CERT_CA[] = R"KEY(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkqUuMSsx7v068+sqy
N/A8QL6Y2yQnikloffl3hA1/yQUuH+I0vpIV6UfjgdwoPa5Lz8TrqY=
-----END CERTIFICATE-----
)KEY";

const char AWS_CERT_CRT[] = R"KEY(
${aws_iot_certificate.cert.certificate_pem}
)KEY";

const char AWS_CERT_PRIVATE[] = R"KEY(
${aws_iot_certificate.cert.private_key}
)KEY";

#endif
EOF
}