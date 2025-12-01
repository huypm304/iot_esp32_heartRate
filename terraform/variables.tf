variable "aws_region" {
  default     = "ap-southeast-1" 
}

variable "project_name" {
  default     = "esp32_health_monitor"
}

variable "wifi_ssid" {
  description = "HCMUS-Phonghoc"
  type        = string
}

variable "wifi_password" {
  description = "khtn@phonghoc"
  type        = string
  sensitive   = true
}