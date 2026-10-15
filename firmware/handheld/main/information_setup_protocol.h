#pragma once
#include "information_payload.h"

#define INFO_SETUP_BODY_LIMIT 768
#define INFO_SETUP_PIN_SIZE 7
#define INFO_SETUP_SESSION_SIZE 33
#define INFO_SETUP_MAX_ATTEMPTS 8
typedef enum { INFO_TOKEN_KEEP, INFO_TOKEN_REPLACE, INFO_TOKEN_CLEAR } info_token_action_t;
typedef struct {
    char url[INFO_URL_SIZE], token[INFO_TOKEN_SIZE];
    info_token_action_t action;
} info_setup_update_t;

bool info_setup_parse_pin(const char *json, size_t length, char pin[INFO_SETUP_PIN_SIZE]);
bool info_setup_parse_update(const char *json, size_t length, info_setup_update_t *out);
bool info_setup_secret_equal(const char *expected, const char *provided);
bool info_setup_origin_valid(const char *host, const char *origin, const char *authority);
bool info_setup_update_allowed(const info_setup_update_t *update, const char *current_url, bool token_set);
bool info_setup_ipv4_mapped_matches(const uint8_t address[16], uint32_t expected_network_order);
