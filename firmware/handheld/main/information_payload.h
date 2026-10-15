#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INFO_BODY_LIMIT 16384
#define INFO_SECTION_COUNT 3
#define INFO_ITEM_COUNT 6
#define INFO_URL_SIZE 384
#define INFO_TOKEN_SIZE 97

typedef struct { char title[321], detail[961]; } info_item_t;
typedef struct {
    char state[16], source[129];
    int64_t updated_at;
    unsigned count;
    info_item_t items[INFO_ITEM_COUNT];
} info_section_t;
typedef struct { info_section_t sections[INFO_SECTION_COUNT]; } info_document_t;

bool info_url_valid(const char *url);
bool info_token_valid(const char *token);
// Input must have a NUL terminator at json[length]. Output must not be on a task stack.
bool info_document_parse(const char *json, size_t length, info_document_t *out);
