output "table_name" {
  value = aws_dynamodb_table.health_data.name
}

output "table_arn" {
  value = aws_dynamodb_table.health_data.arn
}