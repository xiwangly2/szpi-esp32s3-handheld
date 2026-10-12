terraform {
  required_version = ">= 1.6.0"
  required_providers {
    aws     = { source = "hashicorp/aws", version = "~> 6.0" }
    archive = { source = "hashicorp/archive", version = "~> 2.0" }
  }
}

variable "region" {
  type    = string
  default = "ap-east-1"
}
variable "name" {
  type    = string
  default = "szpi-handheld-media"
}
variable "read_token" {
  type      = string
  sensitive = true
  validation {
    condition     = length(var.read_token) >= 24
    error_message = "Use a random token of at least 24 characters."
  }
}
variable "admin_token" {
  type      = string
  sensitive = true
  validation {
    condition     = length(var.admin_token) >= 24
    error_message = "Use a separate random admin token of at least 24 characters."
  }
}

provider "aws" {
  region = var.region
  default_tags { tags = { Project = "szpi-esp32s3-handheld", ManagedBy = "Terraform" } }
}
data "aws_caller_identity" "current" {}
data "archive_file" "function" {
  type        = "zip"
  source_file = "${path.module}/../../dist/index.js"
  output_path = "${path.module}/../../dist/lambda.zip"
}
resource "aws_s3_bucket" "media" {
  bucket        = "${var.name}-${data.aws_caller_identity.current.account_id}-${var.region}"
  force_destroy = false
  lifecycle { prevent_destroy = true }
}
resource "aws_s3_bucket_public_access_block" "media" {
  bucket                  = aws_s3_bucket.media.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}
resource "aws_s3_bucket_server_side_encryption_configuration" "media" {
  bucket = aws_s3_bucket.media.id
  rule {
    apply_server_side_encryption_by_default {
      sse_algorithm = "AES256"
    }
  }
}
resource "aws_cloudwatch_log_group" "media" {
  name              = "/aws/lambda/${var.name}"
  retention_in_days = 7
}
resource "aws_iam_role" "lambda" {
  name = "${var.name}-lambda"
  assume_role_policy = jsonencode({ Version = "2012-10-17", Statement = [{
    Effect = "Allow", Action = "sts:AssumeRole", Principal = { Service = "lambda.amazonaws.com" }
  }] })
}
resource "aws_iam_role_policy" "media" {
  name = "media-only"
  role = aws_iam_role.lambda.id
  policy = jsonencode({ Version = "2012-10-17", Statement = [
    { Effect = "Allow", Action = ["s3:GetObject", "s3:PutObject"], Resource = "${aws_s3_bucket.media.arn}/media/*" },
    { Effect = "Allow", Action = ["s3:ListBucket"], Resource = aws_s3_bucket.media.arn },
    { Effect = "Allow", Action = ["logs:CreateLogStream", "logs:PutLogEvents"], Resource = "${aws_cloudwatch_log_group.media.arn}:*" }
  ] })
}
resource "aws_lambda_function" "media" {
  function_name    = var.name
  description      = "SZPI media upload tickets and private download redirects"
  role             = aws_iam_role.lambda.arn
  runtime          = "nodejs22.x"
  handler          = "index.handler"
  filename         = data.archive_file.function.output_path
  source_code_hash = data.archive_file.function.output_base64sha256
  memory_size      = 256
  timeout          = 10
  environment {
    variables = {
      VIDEO_BUCKET     = aws_s3_bucket.media.id
      STORAGE_PROVIDER = "s3"
      STORAGE_REGION   = var.region
      READ_TOKEN       = var.read_token
      ADMIN_TOKEN      = var.admin_token
    }
  }
  depends_on = [aws_iam_role_policy.media]
}
resource "aws_apigatewayv2_api" "media" {
  name          = var.name
  protocol_type = "HTTP"
}
resource "aws_apigatewayv2_integration" "media" {
  api_id                 = aws_apigatewayv2_api.media.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.media.invoke_arn
  payload_format_version = "2.0"
  timeout_milliseconds   = 10000
}
resource "aws_apigatewayv2_route" "media" {
  api_id    = aws_apigatewayv2_api.media.id
  route_key = "$default"
  target    = "integrations/${aws_apigatewayv2_integration.media.id}"
}
resource "aws_apigatewayv2_stage" "media" {
  api_id      = aws_apigatewayv2_api.media.id
  name        = "$default"
  auto_deploy = true
  default_route_settings {
    throttling_rate_limit  = 5
    throttling_burst_limit = 10
  }
}
resource "aws_lambda_permission" "gateway" {
  statement_id  = "AllowProjectHttpApi"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.media.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.media.execution_arn}/*/*"
}
output "api_url" { value = aws_apigatewayv2_api.media.api_endpoint }
output "bucket" { value = aws_s3_bucket.media.id }
output "function_name" { value = aws_lambda_function.media.function_name }
