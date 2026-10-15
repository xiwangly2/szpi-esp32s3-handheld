#include "information_setup_protocol.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static cJSON *parse_flat(const char *json, size_t length)
{
    if (!json || !length || length > INFO_SETUP_BODY_LIMIT || json[length] || memchr(json, 0, length)) return NULL;
    bool quoted = false, escaped = false; unsigned depth = 0;
    for (size_t i = 0; i < length; i++) {
        char c = json[i];
        if (quoted) {
            if (escaped) {
                if (c == 'u' && i + 4 < length && !memcmp(json + i + 1, "0000", 4)) return NULL;
                escaped = false;
            } else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{') { if (++depth > 1) return NULL; }
        else if (c == '}') { if (!depth) return NULL; depth--; }
        else if (c == '[' || c == ']') return NULL;
    }
    if (depth || quoted) return NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length + 1, NULL, true);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return NULL; }
    return root;
}

bool info_setup_parse_pin(const char *json, size_t length, char pin[INFO_SETUP_PIN_SIZE])
{
    memset(pin, 0, INFO_SETUP_PIN_SIZE);
    cJSON *root = parse_flat(json, length);
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "pin");
    bool ok = cJSON_GetArraySize(root) == 1 && cJSON_IsString(value) && strlen(value->valuestring) == 6;
    if (ok) for (int i = 0; i < 6; i++) if (value->valuestring[i] < '0' || value->valuestring[i] > '9') ok = false;
    if (ok) memcpy(pin, value->valuestring, 7);
    cJSON_Delete(root); return ok;
}

bool info_setup_parse_update(const char *json, size_t length, info_setup_update_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_flat(json, length);
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    bool ok = cJSON_GetArraySize(root) == 3 && cJSON_IsString(url) && cJSON_IsString(token) && cJSON_IsString(action);
    if (ok) {
        ok = info_url_valid(url->valuestring) && info_token_valid(token->valuestring);
        if (!strcmp(action->valuestring, "keep")) out->action = INFO_TOKEN_KEEP;
        else if (!strcmp(action->valuestring, "replace")) { out->action = INFO_TOKEN_REPLACE; ok = ok && *token->valuestring; }
        else if (!strcmp(action->valuestring, "clear")) out->action = INFO_TOKEN_CLEAR;
        else ok = false;
        if (out->action != INFO_TOKEN_REPLACE && *token->valuestring) ok = false;
        if (!*url->valuestring && out->action != INFO_TOKEN_CLEAR) ok = false;
    }
    if (ok) { strcpy(out->url, url->valuestring); strcpy(out->token, token->valuestring); }
    else memset(out, 0, sizeof(*out));
    cJSON_Delete(root); return ok;
}

bool info_setup_secret_equal(const char *expected, const char *provided)
{
    if (!expected || !provided || !*expected || strlen(expected) != strlen(provided)) return false;
    unsigned diff = 0;
    for (size_t i = 0; expected[i]; i++) diff |= (unsigned char)expected[i] ^ (unsigned char)provided[i];
    return diff == 0;
}

bool info_setup_origin_valid(const char *host, const char *origin, const char *authority)
{
    if (!host || !authority || strcmp(host, authority)) return false;
    if (!origin || !*origin) return true; // Paired CLI requests may omit Origin.
    char expected[80]; snprintf(expected, sizeof(expected), "http://%s", authority);
    return !strcmp(origin, expected);
}

bool info_setup_update_allowed(const info_setup_update_t *update, const char *current_url, bool token_set)
{
    return update && current_url && (update->action != INFO_TOKEN_KEEP || !token_set || !strcmp(update->url, current_url));
}

bool info_setup_ipv4_mapped_matches(const uint8_t address[16], uint32_t expected_network_order)
{
    static const uint8_t prefix[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
    return address && !memcmp(address, prefix, sizeof(prefix)) && !memcmp(address + 12, &expected_network_order, 4);
}
