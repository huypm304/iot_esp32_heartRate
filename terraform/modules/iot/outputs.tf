# 1. Xuất Certificate (File .crt)
output "device_cert" {
  value = aws_iot_certificate.cert.certificate_pem
}

# 2. Xuất Private Key (File .key)
output "private_key" {
  value     = aws_iot_certificate.cert.private_key
  sensitive = true 
}

# 3. Xuất Endpoint 
output "iot_endpoint" {
  value = data.aws_iot_endpoint.endpoint.endpoint_address
}