terraform {
  required_version = ">= 1.6.0"
  required_providers {
    tencentcloud = { source = "tencentcloudstack/tencentcloud", version = "~> 1.83" }
  }
}
variable "region" {
  type    = string
  default = "ap-guangzhou"
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
provider "tencentcloud" { region = var.region }
resource "tencentcloud_cos_bucket" "media" {
  bucket      = var.bucket_name
  acl         = "private"
  force_clean = false
  lifecycle { prevent_destroy = true }
}
resource "tencentcloud_scf_function" "media" {
  name              = var.name
  func_type         = "HTTP"
  runtime           = "CustomImage"
  handler           = "index.main_handler"
  mem_size          = 512
  timeout           = 15
  enable_public_net = true
  image_config {
    image_type = "personal"
    image_uri  = var.image
    image_port = 9000
  }
  environment = {
    PORT               = "9000"
    STORAGE_PROVIDER   = "cos"
    STORAGE_REGION     = var.region
    VIDEO_BUCKET       = tencentcloud_cos_bucket.media.bucket
    STORAGE_ACCESS_KEY = var.storage_access_key
    STORAGE_SECRET_KEY = var.storage_secret_key
    READ_TOKEN         = var.read_token
    ADMIN_TOKEN        = var.admin_token
  }
}
output "function_host" { value = tencentcloud_scf_function.media.host }
output "bucket" { value = tencentcloud_cos_bucket.media.bucket }
