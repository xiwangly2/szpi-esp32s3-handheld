#include "information_payload.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

bool info_url_valid(const char *url)
{
    if (!url) return false;
    if (!*url) return true;
    size_t n = strlen(url);
    if (n >= INFO_URL_SIZE || strncmp(url, "https://", 8) || n <= 8) return false;
    const char *path = strchr(url + 8, '/');
    if (!path || path == url + 8) return false;
    for (const char *p = url; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= 32 || c >= 127 || c == '@' || c == '#' || c == '\\' || c == '?') return false;
    }
    return true;
}

bool info_token_valid(const char *token)
{
    if (!token || strlen(token) >= INFO_TOKEN_SIZE) return false;
    for (const char *p = token; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return false;
    }
    return true;
}

static bool copy_text(char *dst, size_t capacity, const cJSON *value)
{
    if (!cJSON_IsString(value) || !value->valuestring) return false;
    size_t size = strlen(value->valuestring);
    if (size >= capacity) return false;
    const unsigned char *p = (const unsigned char *)value->valuestring;
    for (size_t i = 0; i < size;) {
        uint32_t cp = p[i++]; unsigned follow = 0; uint32_t min = 0;
        if (cp < 0x80) { if ((cp < 32 && cp != '\n') || cp == 127) return false; continue; }
        if (cp >= 0xc2 && cp <= 0xdf) { follow = 1; cp &= 31; min = 0x80; }
        else if (cp >= 0xe0 && cp <= 0xef) { follow = 2; cp &= 15; min = 0x800; }
        else if (cp >= 0xf0 && cp <= 0xf4) { follow = 3; cp &= 7; min = 0x10000; }
        else return false;
        while (follow--) { if (i >= size || (p[i] & 0xc0) != 0x80) return false; cp = (cp << 6) | (p[i++] & 63); }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    memcpy(dst, value->valuestring, size + 1); return true;
}

bool info_document_parse(const char *json, size_t length, info_document_t *out)
{
    if (!json || !out || !length || length > INFO_BODY_LIMIT || json[length] || memchr(json, 0, length)) return false;
    // Bound recursive parser stack use before handing syntax validation to cJSON.
    unsigned depth = 0; bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; i++) {
        char c = json[i];
        if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; depth--; }
    }
    if (depth || quoted) return false;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length + 1, NULL, true);
    if (!root) return false;
    bool ok = false; unsigned seen = 0;
    memset(out, 0, sizeof(*out));
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *sections = cJSON_GetObjectItemCaseSensitive(root, "sections");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !cJSON_IsArray(sections) || cJSON_GetArraySize(sections) != 3) goto done;
    const cJSON *section;
    cJSON_ArrayForEach(section, sections) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(section, "id");
        if (!cJSON_IsString(id)) goto done;
        int index = !strcmp(id->valuestring, "weather") ? 0 : !strcmp(id->valuestring, "rss") ? 1 : !strcmp(id->valuestring, "services") ? 2 : -1;
        if (index < 0 || (seen & (1U << index))) goto done;
        seen |= 1U << index;
        info_section_t *s = &out->sections[index];
        if (!copy_text(s->state, sizeof(s->state), cJSON_GetObjectItemCaseSensitive(section, "state")) ||
            !copy_text(s->source, sizeof(s->source), cJSON_GetObjectItemCaseSensitive(section, "source"))) goto done;
        if (strcmp(s->state, "ok") && strcmp(s->state, "stale") && strcmp(s->state, "error") && strcmp(s->state, "unconfigured")) goto done;
        const cJSON *time = cJSON_GetObjectItemCaseSensitive(section, "updatedAt");
        if (!cJSON_IsNumber(time) || !isfinite(time->valuedouble) || time->valuedouble < 0 || time->valuedouble > 4102444800.0) goto done;
        s->updated_at = (int64_t)time->valuedouble;
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(section, "items");
        if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) > INFO_ITEM_COUNT) goto done;
        const cJSON *item;
        cJSON_ArrayForEach(item, items) {
            info_item_t *row = &s->items[s->count++];
            if (!copy_text(row->title, sizeof(row->title), cJSON_GetObjectItemCaseSensitive(item, "title")) ||
                !copy_text(row->detail, sizeof(row->detail), cJSON_GetObjectItemCaseSensitive(item, "detail"))) goto done;
        }
    }
    ok = seen == 7;
done:
    cJSON_Delete(root);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}
