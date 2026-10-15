variable "information_config" {
  description = "Optional info desk sources as JSON; empty disables upstream fetches."
  type        = string
  default     = ""
  validation {
    condition     = length(var.information_config) <= 4096 && (var.information_config == "" || can(jsondecode(var.information_config)))
    error_message = "Use empty text or a JSON object of at most 4096 characters."
  }
}
