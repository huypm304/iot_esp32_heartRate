variable "project_name" {
  type = string
}

variable "aws_region" { 
  default = "ap-southeast-1" 
}

variable "dynamodb_table_name" {
  type        = string
}

variable "wifi_ssid" {
  type = string
}
variable "wifi_password" {
  type = string
}
