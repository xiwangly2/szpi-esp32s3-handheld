#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// UI entry points are called with the LVGL lock held. NULL path selects the default video.
esp_err_t handheld_video_open(const char *path, uint8_t *volume, void (*on_close)(void));
bool handheld_video_is_open(void);
