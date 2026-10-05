#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WIFI_QR_AUTH_OPEN,
    WIFI_QR_AUTH_WPA,
    WIFI_QR_AUTH_WPA3,
} wifi_qr_auth_t;

typedef struct {
    char ssid[33];
    char password[65];
    wifi_qr_auth_t auth;
    bool hidden;
} wifi_qr_credentials_t;

typedef enum {
    WIFI_QR_OK,
    WIFI_QR_NOT_WIFI,
    WIFI_QR_INVALID,
    WIFI_QR_TOO_LONG,
    WIFI_QR_UNSUPPORTED_AUTH,
} wifi_qr_parse_result_t;

// Parses the WIFI: QR convention. Output is cleared on every failure.
wifi_qr_parse_result_t wifi_qr_parse(const uint8_t *payload, size_t length,
                                     wifi_qr_credentials_t *out);
