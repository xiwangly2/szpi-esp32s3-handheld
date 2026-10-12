terraform {
  required_version = ">= 1.6.0"
  required_providers {
    cloudflare = { source = "cloudflare/cloudflare", version = "~> 5.0" }
  }
}
variable "account_id" { type = string }
variable "name" {
  type    = string
  default = "szpi-handheld-media"
}
variable "read_token" {
  type      = string
  sensitive = true
}
variable "admin_token" {
  type      = string
  sensitive = true
}
variable "r2_access_key_id" {
  type      = string
  sensitive = true
}
variable "r2_secret_access_key" {
  type      = string
  sensitive = true
}
provider "cloudflare" {} # CLOUDFLARE_API_TOKEN is used by the provider, not put in the Worker.

resource "cloudflare_r2_bucket" "media" {
  account_id = var.account_id
  name       = var.name
  location   = "APAC"
  lifecycle { prevent_destroy = true }
}
resource "cloudflare_workers_script" "media" {
  account_id         = var.account_id
  script_name        = var.name
  main_module        = "worker.mjs"
  content            = file("${path.module}/../../dist/worker.mjs")
  compatibility_date = "2026-10-01"
  bindings = [
    { name = "MEDIA", type = "r2_bucket", bucket_name = cloudflare_r2_bucket.media.name },
    { name = "VIDEO_BUCKET", type = "plain_text", text = cloudflare_r2_bucket.media.name },
    { name = "R2_ACCOUNT_ID", type = "plain_text", text = var.account_id },
    { name = "READ_TOKEN", type = "secret_text", text = var.read_token },
    { name = "ADMIN_TOKEN", type = "secret_text", text = var.admin_token },
    { name = "R2_ACCESS_KEY_ID", type = "secret_text", text = var.r2_access_key_id },
    { name = "R2_SECRET_ACCESS_KEY", type = "secret_text", text = var.r2_secret_access_key }
  ]
  observability = { enabled = false }
}
resource "cloudflare_workers_script_subdomain" "media" {
  account_id       = var.account_id
  script_name      = cloudflare_workers_script.media.script_name
  enabled          = true
  previews_enabled = false
}
output "worker_name" { value = cloudflare_workers_script.media.script_name }
output "bucket" { value = cloudflare_r2_bucket.media.name }
