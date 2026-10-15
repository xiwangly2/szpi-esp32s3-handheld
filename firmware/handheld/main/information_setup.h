#pragma once
#include "esp_err.h"
#include "information_setup_protocol.h"

typedef struct info_setup info_setup_t;
typedef struct {
    char url[80], ssid[33], password[17], pin[INFO_SETUP_PIN_SIZE];
    bool hotspot;
} info_setup_details_t;

// Called from the information worker, never with the LVGL lock held.
esp_err_t info_setup_start(bool hotspot, const char *current_url, bool token_set,
                           info_setup_t **out, info_setup_details_t *details);
bool info_setup_take_update(info_setup_t *setup, info_setup_update_t *out);
void info_setup_finish_update(info_setup_t *setup, bool saved, const char *url, bool token_set);
bool info_setup_expired(info_setup_t *setup);
void info_setup_stop(info_setup_t *setup);
