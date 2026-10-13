#pragma once
#include "esp_err.h"

// The caller holds the LVGL lock. The page owns its network worker until exit.
esp_err_t ip_camera_open(void (*on_close)(void));
