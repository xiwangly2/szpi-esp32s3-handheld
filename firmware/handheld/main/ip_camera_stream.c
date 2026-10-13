#include "ip_camera_stream.h"
#include "http_parser.h"
#include "multipart_parser.h"
#include <stdlib.h>
#include <string.h>

static bool equal_ci(const char *a, size_t length, const char *b)
{
    if (strlen(b) != length) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)a[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)b[i]) return false;
    }
    return true;
}

bool ipcam_url_valid(const char *url)
{
    if (!url) return false;
    size_t size = strlen(url);
    if (!size || size >= IPCAM_URL_SIZE) return false;
    for (size_t i = 0; i < size; i++) {
        if ((unsigned char)url[i] <= 32 || (unsigned char)url[i] >= 127) return false;
    }
    struct http_parser_url parsed = {0};
    if (http_parser_parse_url(url, size, 0, &parsed) != 0 ||
        !(parsed.field_set & (1U << UF_HOST)) || !(parsed.field_set & (1U << UF_SCHEMA)) ||
        (parsed.field_set & (1U << UF_FRAGMENT))) return false;
    if ((parsed.field_set & (1U << UF_PORT)) && parsed.port == 0) return false;
    const char *scheme = url + parsed.field_data[UF_SCHEMA].off;
    size_t length = parsed.field_data[UF_SCHEMA].len;
    return equal_ci(scheme, length, "http") || equal_ci(scheme, length, "https");
}

ipcam_content_t ipcam_content_type(const char *value, char boundary[IPCAM_BOUNDARY_SIZE])
{
    boundary[0] = 0;
    if (!value || strlen(value) > 255) return IPCAM_CONTENT_INVALID;
    const char *p = value;
    while (*p == ' ' || *p == '\t') p++;
    const char *end = strchr(p, ';');
    if (!end) end = p + strlen(p);
    const char *trimmed = end;
    while (trimmed > p && (trimmed[-1] == ' ' || trimmed[-1] == '\t')) trimmed--;
    if (equal_ci(p, (size_t)(trimmed - p), "image/jpeg")) return IPCAM_CONTENT_JPEG;
    if (!equal_ci(p, (size_t)(trimmed - p), "multipart/x-mixed-replace")) return IPCAM_CONTENT_INVALID;
    p = end;
    bool found = false;
    while (*p) {
        if (*p++ != ';') return IPCAM_CONTENT_INVALID;
        while (*p == ' ' || *p == '\t') p++;
        const char *name = p;
        while (*p && *p != '=' && *p != ';') p++;
        if (*p != '=') return IPCAM_CONTENT_INVALID;
        const char *name_end = p++;
        while (name_end > name && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
        while (*p == ' ' || *p == '\t') p++;
        bool quoted = *p == '"';
        if (quoted) p++;
        const char *start = p;
        while (*p && (quoted ? *p != '"' : *p != ';')) p++;
        const char *stop = p;
        if (quoted) { if (*p != '"') return IPCAM_CONTENT_INVALID; p++; }
        else while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;
        if (equal_ci(name, (size_t)(name_end - name), "boundary")) {
            size_t size = (size_t)(stop - start);
            if (found || !size || size >= IPCAM_BOUNDARY_SIZE) return IPCAM_CONTENT_INVALID;
            for (size_t i = 0; i < size; i++) {
                if ((unsigned char)start[i] < 33 || (unsigned char)start[i] > 126 ||
                    start[i] == '"' || start[i] == '\\') return IPCAM_CONTENT_INVALID;
            }
            memcpy(boundary, start, size); boundary[size] = 0; found = true;
        }
        while (*p == ' ' || *p == '\t') p++;
    }
    return found ? IPCAM_CONTENT_MJPEG : IPCAM_CONTENT_INVALID;
}

bool ipcam_fit(uint16_t width, uint16_t height, uint16_t *out_width, uint16_t *out_height)
{
    if (!width || !height || width > 2560 || height > 1920) return false;
    uint32_t w = width, h = height;
    if (w > 320 || h > 240) {
        if (w * 240 > h * 320) { h = h * 320 / w; w = 320; }
        else { w = w * 240 / h; h = 240; }
        w &= ~7U; h &= ~7U;
        if (!w || !h || w * 8 < width || h * 8 < height) return false;
    }
    *out_width = (uint16_t)w; *out_height = (uint16_t)h;
    return true;
}

struct ipcam_stream {
    multipart_parser *parser;
    uint8_t *buffer;
    size_t capacity, length, headers_size, expected_size;
    char field[64], value[256];
    size_t field_size, value_size, preamble_size;
    bool value_started, have_length, have_type, started, finished, failed;
    ipcam_frame_cb callback;
    void *context;
};

static int finish_header(ipcam_stream_t *s)
{
    if (!s->field_size) return 0;
    while (s->value_size && (s->value[s->value_size - 1] == ' ' || s->value[s->value_size - 1] == '\t'))
        s->value[--s->value_size] = 0;
    if (equal_ci(s->field, s->field_size, "content-type")) {
        char unused[IPCAM_BOUNDARY_SIZE];
        if (s->have_type || ipcam_content_type(s->value, unused) != IPCAM_CONTENT_JPEG) return 1;
        s->have_type = true;
    } else if (equal_ci(s->field, s->field_size, "content-length")) {
        if (s->have_length || !s->value_size) return 1;
        size_t length = 0;
        for (size_t i = 0; i < s->value_size; i++) {
            if (s->value[i] < '0' || s->value[i] > '9') return 1;
            unsigned digit = (unsigned)(s->value[i] - '0');
            if (digit > s->capacity || length > (s->capacity - digit) / 10) return 1;
            length = length * 10 + digit;
        }
        if (!length) return 1;
        s->expected_size = length; s->have_length = true;
    }
    s->field_size = s->value_size = 0;
    s->field[0] = s->value[0] = 0;
    s->value_started = false;
    return 0;
}

static int part_begin(multipart_parser *p)
{
    ipcam_stream_t *s = multipart_parser_get_data(p);
    s->length = s->headers_size = s->field_size = s->value_size = s->expected_size = 0;
    s->have_length = s->have_type = s->value_started = false;
    s->field[0] = s->value[0] = 0;
    return 0;
}

static int header_field(multipart_parser *p, const char *data, size_t size)
{
    ipcam_stream_t *s = multipart_parser_get_data(p);
    if (s->value_started && finish_header(s)) { s->failed = true; return 1; }
    if (size >= sizeof(s->field) - s->field_size || size > 2048 - s->headers_size) {
        s->failed = true; return 1;
    }
    memcpy(s->field + s->field_size, data, size);
    s->field_size += size; s->field[s->field_size] = 0; s->headers_size += size;
    return 0;
}

static int header_value(multipart_parser *p, const char *data, size_t size)
{
    ipcam_stream_t *s = multipart_parser_get_data(p);
    s->value_started = true;
    if (size >= sizeof(s->value) - s->value_size || size > 2048 - s->headers_size) {
        s->failed = true; return 1;
    }
    memcpy(s->value + s->value_size, data, size);
    s->value_size += size; s->value[s->value_size] = 0; s->headers_size += size;
    return 0;
}

static int headers_done(multipart_parser *p)
{
    ipcam_stream_t *s = multipart_parser_get_data(p);
    if (finish_header(s)) { s->failed = true; return 1; }
    return 0;
}

static int part_data(multipart_parser *p, const char *data, size_t size)
{
    ipcam_stream_t *s = multipart_parser_get_data(p);
    if (size > s->capacity - s->length) { s->failed = true; return 1; }
    memcpy(s->buffer + s->length, data, size); s->length += size;
    return 0;
}

static int part_end(multipart_parser *p)
{
    ipcam_stream_t *s = multipart_parser_get_data(p);
    if ((s->have_length && s->expected_size != s->length) || s->length < 4 ||
        s->buffer[0] != 0xff || s->buffer[1] != 0xd8 ||
        !s->callback(s->context, s->buffer, s->length)) { s->failed = true; return 1; }
    return 0;
}

static int body_end(multipart_parser *p)
{
    ((ipcam_stream_t *)multipart_parser_get_data(p))->finished = true;
    return 0;
}

static const multipart_parser_settings callbacks = {
    .on_header_field = header_field, .on_header_value = header_value,
    .on_part_data = part_data, .on_part_data_begin = part_begin,
    .on_headers_complete = headers_done, .on_part_data_end = part_end, .on_body_end = body_end,
};

ipcam_stream_t *ipcam_stream_create(const char *boundary, uint8_t *buffer, size_t capacity,
                                    ipcam_frame_cb callback, void *context)
{
    if (!boundary || !*boundary || strlen(boundary) >= IPCAM_BOUNDARY_SIZE || !buffer ||
        capacity < 4 || capacity > IPCAM_FRAME_LIMIT || !callback) return NULL;
    ipcam_stream_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    char delimiter[IPCAM_BOUNDARY_SIZE + 2] = "--";
    memcpy(delimiter + 2, boundary, strlen(boundary) + 1);
    s->parser = multipart_parser_init(delimiter, &callbacks);
    if (!s->parser) { free(s); return NULL; }
    s->buffer = buffer; s->capacity = capacity; s->callback = callback; s->context = context;
    multipart_parser_set_data(s->parser, s);
    return s;
}

bool ipcam_stream_feed(ipcam_stream_t *s, const void *data, size_t size)
{
    if (!s || s->failed || (!data && size)) return false;
    const char *p = data;
    while (!s->started && size && (*p == '\r' || *p == '\n')) {
        if (++s->preamble_size > 4) { s->failed = true; return false; }
        p++; size--;
    }
    if (!size) return true;
    s->started = true;
    if (multipart_parser_execute(s->parser, p, size) != size) s->failed = true;
    return !s->failed;
}

bool ipcam_stream_finished(const ipcam_stream_t *s) { return s && s->finished && !s->failed; }
void ipcam_stream_destroy(ipcam_stream_t *s)
{
    if (s) { multipart_parser_free(s->parser); free(s); }
}
