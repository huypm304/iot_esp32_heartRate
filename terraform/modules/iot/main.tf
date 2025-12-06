resource "aws_iot_thing" "esp32" {
  name = "${var.project_name}_ESP32"
}

resource "aws_iot_certificate" "cert" {
  active = true
}

resource "aws_iot_thing_principal_attachment" "att_thing" {
  principal = aws_iot_certificate.cert.arn
  thing     = aws_iot_thing.esp32.name
}

resource "aws_iot_policy" "pubsub" {
  name = "${var.project_name}_Policy"

  policy = jsonencode({
    Version = "2012-10-17",
    Statement = [{
      Action = [
        "iot:Connect",
        "iot:Publish",
        "iot:Subscribe",
        "iot:Receive"
      ]
      Effect   = "Allow"
      Resource = "*"
    }]
  })
}

resource "aws_iot_policy_attachment" "att_policy" {
  policy = aws_iot_policy.pubsub.name
  target = aws_iot_certificate.cert.arn
}

data "aws_iot_endpoint" "endpoint" {
  endpoint_type = "iot:Data-ATS"
}

resource "aws_iam_role" "iot_role" {
  name = "${var.project_name}_IoT_Role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17",
    Statement = [{
      Action = "sts:AssumeRole"
      Effect = "Allow"
      Principal = { Service = "iot.amazonaws.com" }
    }]
  })
}

resource "aws_iam_role_policy" "iot_access" {
  name = "${var.project_name}_Access_Policy"
  role = aws_iam_role.iot_role.id

  policy = jsonencode({
    Version = "2012-10-17",
    Statement = [
      {
        Effect = "Allow",
        Action = [
          "dynamodb:PutItem",
          "cloudwatch:PutMetricData"
        ],
        Resource = "*"
      }
    ]
  })
}

resource "aws_iot_topic_rule" "rule" {
  name        = "${var.project_name}_Rule"
  description = "Route data to DB and Dashboard"
  enabled     = true
  
  sql         = "SELECT * FROM 'health/monitor'"
  sql_version = "2016-03-23"

  # Action 1: Lưu vào DynamoDB
  dynamodbv2 {
    role_arn = aws_iam_role.iot_role.arn # Giờ nó sẽ tìm thấy Role ở trên
    put_item {
      table_name = var.dynamodb_table_name 
    }
  }

  # Action 2: Đẩy SpO2 lên CloudWatch
  cloudwatch_metric {
    role_arn = aws_iam_role.iot_role.arn
    metric_namespace = var.project_name
    metric_name      = "SpO2"
    metric_value     = "$${spo2}" 
    metric_unit      = "Percent"
  }
  
  # Action 3: Đẩy HeartRate lên CloudWatch
  cloudwatch_metric {
    role_arn = aws_iam_role.iot_role.arn
    metric_namespace = var.project_name
    metric_name      = "HeartRate"
    metric_value     = "$${heart_rate}" 
    metric_unit      = "Count"
  }
}