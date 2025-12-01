# DYNAMODB TABLE
resource "aws_dynamodb_table" "health_data" {
  name           = "${var.project_name}_Logs"
  billing_mode   = "PAY_PER_REQUEST"
  hash_key       = "device_id"
  range_key      = "timestamp"

  attribute {
    name = "device_id"
    type = "S"
  }

  attribute {
    name = "timestamp"
    type = "N"
  }

  tags = {
    Environment = "Production"
    Project     = var.project_name
  }
}