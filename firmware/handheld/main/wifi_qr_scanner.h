#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "wifi_qr_payload.h"

// Invoked with the LVGL lock held, after the camera and decoder are released.
typedef void (*wifi_qr_scan_cb_t)(const wifi_qr_credentials_t *credentials,
                                 uint32_t generation, const char *error);

esp_err_t wifi_qr_scanner_start(lv_obj_t *parent, uint32_t generation,
                                 wifi_qr_scan_cb_t on_finish);
bool wifi_qr_scanner_is_running(void);
