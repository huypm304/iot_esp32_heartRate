
# 1. Create Role for IoT Core
resource "aws_iam_role" "iot_role" {
  name = "${var.project_name}_IoT_Role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action = "sts:AssumeRole"
      Effect = "Allow"
      Principal = { Service = "iot.amazonaws.com" }
    }]
  })
}

# 2. Attach Permissions to that Role
resource "aws_iam_role_policy" "iot_policy" {
  name = "${var.project_name}_Access_Policy"
  role = aws_iam_role.iot_role.id

  policy = jsonencode({
    Version = "2012-10-17"
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
  
  # IMPORTANT: SQL query to filter data
  sql         = "SELECT * FROM 'health/monitor'"
  sql_version = "2016-03-23"

  # Action 1: Save to DynamoDB
  dynamodbv2 {
    role_arn = aws_iam_role.iot_role.arn
    put_item {
      table_name = aws_dynamodb_table.health_data.name
    }
  }

  # Action 2: Send Metrics to CloudWatch
  cloudwatch_metric {
    role_arn = aws_iam_role.iot_role.arn
    metric_namespace = var.project_name
    metric_name      = "SpO2"
    metric_value     = "$${spo2}" 
    metric_unit      = "Percent"
  }
  
  cloudwatch_metric {
    role_arn = aws_iam_role.iot_role.arn
    metric_namespace = var.project_name
    metric_name      = "HeartRate"
    metric_value     = "$${heart_rate}" 
    metric_unit      = "Count"
  }
}