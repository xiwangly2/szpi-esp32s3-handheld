#include "wifi_qr_payload.h"

#include <string.h>

static bool text_is(const char *value, const char *expected)
{
    while (*value && *expected) {
        unsigned char c = (unsigned char)*value++;
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c + ('a' - 'A'));
        }
        if (c != (unsigned char)*expected++) {
            return false;
        }
    }
    return *value == *expected;
}

static bool valid_utf8(const unsigned char *text)
{
    while (*text) {
        uint32_t c = *text++;
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f) {
                return false;
            }
            continue;
        }
        unsigned count;
        uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            count = 1; minimum = 0x80; c &= 0x1f;
        } else if (c >= 0xe0 && c <= 0xef) {
            count = 2; minimum = 0x800; c &= 0x0f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            count = 3; minimum = 0x10000; c &= 0x07;
        } else {
            return false;
        }
        while (count--) {
            unsigned char next = *text++;
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            c = (c << 6) | (next & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

static bool hex_psk(const char *password)
{
    for (size_t i = 0; i < 64; i++) {
        char c = password[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

wifi_qr_parse_result_t wifi_qr_parse(const uint8_t *payload, size_t length,
                                     wifi_qr_credentials_t *out)
{
    if (out == NULL) {
        return WIFI_QR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if (payload == NULL || length < 5 || memcmp(payload, "WIFI:", 5) != 0) {
        return WIFI_QR_NOT_WIFI;
    }
    if (length > 1024) {
        return WIFI_QR_TOO_LONG;
    }
    wifi_qr_credentials_t parsed = {0};
    bool seen_ssid = false, seen_password = false, seen_auth = false, seen_hidden = false;
    size_t pos = 5;
    while (pos < length) {
        if (payload[pos] == ';') {
            while (pos < length && payload[pos] == ';') {
                pos++;
            }
            if (pos != length) {
                return WIFI_QR_INVALID;
            }
            break;
        }
        size_t key_start = pos;
        while (pos < length && payload[pos] != ':') {
            if (payload[pos] < 'A' || payload[pos] > 'Z') {
                if (payload[pos] < '0' || payload[pos] > '9') {
                    return WIFI_QR_INVALID;
                }
            }
            pos++;
        }
        size_t key_len = pos - key_start;
        if (key_len == 0 || pos == length) {
            return WIFI_QR_INVALID;
        }
        pos++;
        bool quoted = pos < length && payload[pos] == '"';
        if (quoted) {
            pos++;
        }
        bool closed_quote = !quoted;
        char value[130];
        size_t used = 0;
        while (pos < length && payload[pos] != ';') {
            unsigned char c = payload[pos++];
            if (quoted && c == '"') {
                closed_quote = true;
                break;
            }
            if (c == '\\') {
                if (pos == length) {
                    return WIFI_QR_INVALID;
                }
                c = payload[pos++];
                if (c != '\\' && c != ';' && c != ':' && c != ',' && c != '"') {
                    return WIFI_QR_INVALID;
                }
            }
            if (c < 0x20 || c == 0x7f) {
                return WIFI_QR_INVALID;
            }
            if (used + 1 >= sizeof(value)) {
                return WIFI_QR_TOO_LONG;
            }
            value[used++] = (char)c;
        }
        if (!closed_quote || pos == length || payload[pos++] != ';') {
            return WIFI_QR_INVALID;
        }
        value[used] = '\0';
        if (!valid_utf8((const unsigned char *)value)) {
            return WIFI_QR_INVALID;
        }

        char key = key_len == 1 ? (char)payload[key_start] : '\0';
        switch (key) {
        case 'S':
            if (seen_ssid) { return WIFI_QR_INVALID; }
            if (used > 32) { return WIFI_QR_TOO_LONG; }
            memcpy(parsed.ssid, value, used + 1);
            seen_ssid = true;
            break;
        case 'P':
            if (seen_password) { return WIFI_QR_INVALID; }
            if (used > 64) { return WIFI_QR_TOO_LONG; }
            memcpy(parsed.password, value, used + 1);
            seen_password = true;
            break;
        case 'T':
            if (seen_auth) { return WIFI_QR_INVALID; }
            seen_auth = true;
            if (text_is(value, "nopass") || value[0] == '\0') {
                parsed.auth = WIFI_QR_AUTH_OPEN;
            } else if (text_is(value, "wpa") || text_is(value, "wpa2")) {
                parsed.auth = WIFI_QR_AUTH_WPA;
            } else if (text_is(value, "sae") || text_is(value, "wpa3")) {
                parsed.auth = WIFI_QR_AUTH_WPA3;
            } else {
                return WIFI_QR_UNSUPPORTED_AUTH;
            }
            break;
        case 'H':
            if (seen_hidden) { return WIFI_QR_INVALID; }
            seen_hidden = true;
            if (!text_is(value, "true") && !text_is(value, "false") && used != 0) {
                return WIFI_QR_INVALID;
            }
            parsed.hidden = text_is(value, "true");
            break;
        default:
            if (key == 'E' || key == 'I' || key == 'A' ||
                (key_len == 3 && memcmp(payload + key_start, "PH2", 3) == 0)) {
                return WIFI_QR_UNSUPPORTED_AUTH;
            }
            break;
        }
    }
    if (!seen_ssid || parsed.ssid[0] == '\0') {
        return WIFI_QR_INVALID;
    }
    if (parsed.auth == WIFI_QR_AUTH_OPEN) {
        memset(parsed.password, 0, sizeof(parsed.password));
    } else {
        size_t password_len = strlen(parsed.password);
        if (password_len < 8 || (password_len == 64 &&
            (parsed.auth != WIFI_QR_AUTH_WPA || !hex_psk(parsed.password)))) {
            return WIFI_QR_INVALID;
        }
    }
    *out = parsed;
    return WIFI_QR_OK;
}
