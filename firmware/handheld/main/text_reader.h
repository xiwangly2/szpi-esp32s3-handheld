#pragma once
#include "esp_err.h"
#include "lvgl.h"

// Caller owns these preview objects and holds the LVGL lock. Deleting page
// cancels pending work; the single reader worker owns all file I/O and buffers.
esp_err_t text_reader_open(lv_obj_t *page, lv_obj_t *bar, lv_obj_t *body,
                           lv_obj_t *title, const char *path);
