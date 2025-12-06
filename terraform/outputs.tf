
output "final_cert" {
  value = module.iot_core.device_cert
  sensitive = true
}

output "final_key" {
  value     = module.iot_core.private_key
  sensitive = true
}

output "final_endpoint" {
  value = module.iot_core.iot_endpoint
}

//terraform output -raw final_cert
//terraform output -raw final_key
//terraform output -raw final_endpoint