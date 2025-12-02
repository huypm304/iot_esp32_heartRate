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
EOF
}