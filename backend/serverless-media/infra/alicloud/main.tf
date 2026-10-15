terraform {
  required_version = ">= 1.6.0"
  required_providers {
    alicloud = { source = "aliyun/alicloud", version = "~> 1.234" }
  }
}
variable "region" {
  type    = string
  default = "cn-hangzhou"
}
variable "name" {
  type    = string
  default = "szpi-handheld-media"
}
variable "bucket_name" { type = string }
variable "image" { type = string }
variable "read_token" {
  type      = string
  sensitive = true
}
variable "admin_token" {
  type      = string
  sensitive = true
}
variable "storage_access_key" {
  type      = string
  sensitive = true
}
variable "storage_secret_key" {
  type      = string
  sensitive = true
}
provider "alicloud" { region = var.region }
resource "alicloud_oss_bucket" "media" {
  bucket        = var.bucket_name
  force_destroy = false
  lifecycle { prevent_destroy = true }
}
resource "alicloud_oss_bucket_acl" "media" {
  bucket = alicloud_oss_bucket.media.bucket
  acl    = "private"
}
resource "alicloud_fcv3_function" "media" {
  function_name        = var.name
  runtime              = "custom-container"
  handler              = "index.handler"
  memory_size          = 512
  cpu                  = 0.5
  disk_size            = 512
  timeout              = 15
  internet_access      = true
  instance_concurrency = 2
  custom_container_config {
    image = var.image
    port  = 9000
  }
  environment_variables = {
    PORT               = "9000"
    STORAGE_PROVIDER   = "oss"
    STORAGE_REGION     = "oss-${var.region}"
    VIDEO_BUCKET       = alicloud_oss_bucket.media.bucket
    STORAGE_ACCESS_KEY = var.storage_access_key
    STORAGE_SECRET_KEY = var.storage_secret_key
    READ_TOKEN         = var.read_token
    ADMIN_TOKEN        = var.admin_token
    INFO_CONFIG_JSON   = var.information_config
  }
}
resource "alicloud_fcv3_trigger" "http" {
  function_name  = alicloud_fcv3_function.media.function_name
  trigger_name   = "media-http"
  trigger_type   = "http"
  qualifier      = "LATEST"
  trigger_config = jsonencode({ authType = "anonymous", methods = ["GET", "HEAD", "POST"] })
}
output "api_url" { value = alicloud_fcv3_trigger.http.http_trigger[0].url_internet }
