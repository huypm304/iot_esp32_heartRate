provider "aws" {
  region = var.aws_region
}

# 1. Gọi Module DynamoDB
module "db" {
  source       = "./modules/dynamodb"
  project_name = var.project_name
}

# 2. Gọi Module IoT
module "iot_core" {
  source = "./modules/iot"

  # Truyền biến vào
  project_name  = var.project_name
  wifi_ssid     = var.wifi_ssid
  wifi_password = var.wifi_password
  
  dynamodb_table_name = module.db.table_name
}

