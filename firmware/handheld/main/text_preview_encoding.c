#include "text_preview_encoding.h"
#include "ff.h"
#include <string.h>

#if FF_USE_LFN == 0 || (FF_CODE_PAGE != 936 && FF_CODE_PAGE != 0)
#error "GBK text preview requires the existing FatFs CP936 Unicode table"
#endif

typedef enum { DECODE_OK, DECODE_INVALID, DECODE_INCOMPLETE } decode_status_t;

static decode_status_t utf8_next(const uint8_t *p, size_t size, uint32_t *cp, size_t *used)
{
    *used = 1;
    if (p[0] < 0x80) { *cp = p[0]; return DECODE_OK; }
    unsigned count;
    if (p[0] >= 0xc2 && p[0] <= 0xdf) count = 2;
    else if (p[0] >= 0xe0 && p[0] <= 0xef) count = 3;
    else if (p[0] >= 0xf0 && p[0] <= 0xf4) count = 4;
    else return DECODE_INVALID;
    for (unsigned i = 1; i < count && i < size; i++) {
        if ((p[i] & 0xc0) != 0x80) return DECODE_INVALID;
        if (i == 1 && ((p[0] == 0xe0 && p[i] < 0xa0) || (p[0] == 0xed && p[i] >= 0xa0) ||
                      (p[0] == 0xf0 && p[i] < 0x90) || (p[0] == 0xf4 && p[i] > 0x8f))) return DECODE_INVALID;
    }
    if (size < count) return DECODE_INCOMPLETE;
    *cp = p[0] & (0x7f >> count);
    for (unsigned i = 1; i < count; i++) *cp = (*cp << 6) | (p[i] & 0x3f);
    *used = count; return DECODE_OK;
}

static bool utf8_prefix_valid(const uint8_t *input, size_t length, bool has_more)
{
    for (size_t i = 0; i < length;) {
        uint32_t cp; size_t used;
        decode_status_t state = utf8_next(input + i, length - i, &cp, &used);
        if (state != DECODE_OK) return state == DECODE_INCOMPLETE && has_more;
        i += used;
    }
    return true;
}

static uint16_t read16(const uint8_t *p, bool little)
{
    return little ? (uint16_t)(p[0] | ((uint16_t)p[1] << 8)) : (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static decode_status_t utf16_next(const uint8_t *p, size_t size, bool little, uint32_t *cp, size_t *used)
{
    *used = 1;
    if (size < 2) return DECODE_INCOMPLETE;
    uint16_t first = read16(p, little); *used = 2;
    if (first >= 0xdc00 && first <= 0xdfff) return DECODE_INVALID;
    if (first < 0xd800 || first > 0xdbff) { *cp = first; return DECODE_OK; }
    if (size < 4) return DECODE_INCOMPLETE;
    uint16_t second = read16(p + 2, little);
    if (second < 0xdc00 || second > 0xdfff) return DECODE_INVALID;
    *cp = 0x10000 + (((uint32_t)first - 0xd800) << 10) + second - 0xdc00;
    *used = 4; return DECODE_OK;
}

static decode_status_t gbk_next(const uint8_t *p, size_t size, uint32_t *cp, size_t *used)
{
    *used = 1;
    if (p[0] < 0x80) { *cp = p[0]; return DECODE_OK; }
    if (p[0] == 0x80) { *cp = 0x20ac; return DECODE_OK; }
    if (p[0] < 0x81 || p[0] > 0xfe) return DECODE_INVALID;
    if (size < 2) return DECODE_INCOMPLETE;
    if (p[1] < 0x40 || p[1] > 0xfe || p[1] == 0x7f) return DECODE_INVALID;
    *used = 2;
    *cp = ff_oem2uni((WCHAR)(((uint16_t)p[0] << 8) | p[1]), 936);
    return *cp ? DECODE_OK : DECODE_INVALID;
}

static size_t encode_utf8(uint32_t cp, char encoded[4])
{
    if (cp < 0x80) { encoded[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        encoded[0] = (char)(0xc0 | (cp >> 6)); encoded[1] = (char)(0x80 | (cp & 63)); return 2;
    }
    if (cp < 0x10000) {
        encoded[0] = (char)(0xe0 | (cp >> 12)); encoded[1] = (char)(0x80 | ((cp >> 6) & 63));
        encoded[2] = (char)(0x80 | (cp & 63)); return 3;
    }
    encoded[0] = (char)(0xf0 | (cp >> 18)); encoded[1] = (char)(0x80 | ((cp >> 12) & 63));
    encoded[2] = (char)(0x80 | ((cp >> 6) & 63)); encoded[3] = (char)(0x80 | (cp & 63)); return 4;
}

const char *text_encoding_name(text_encoding_t encoding)
{
    switch (encoding) {
    case TEXT_ENCODING_UTF8: return "UTF-8";
    case TEXT_ENCODING_GBK: return "GBK";
    case TEXT_ENCODING_UTF16LE: return "UTF-16LE";
    case TEXT_ENCODING_UTF16BE: return "UTF-16BE";
    default: return "Auto";
    }
}

bool text_preview_decode_window(const uint8_t *input, size_t length, bool has_more, bool at_start,
                                text_encoding_t requested, char *output, size_t capacity,
                                uint16_t *source_end, text_decode_result_t *result)
{
    if (result) memset(result, 0, sizeof(*result));
    if (output && capacity) output[0] = 0;
    if ((!input && length) || !output || !capacity || !result || (source_end && length > UINT16_MAX) ||
        requested < TEXT_ENCODING_AUTO || requested > TEXT_ENCODING_UTF16BE) return false;
    text_encoding_t bom = TEXT_ENCODING_AUTO; size_t bom_bytes = 0;
    if (at_start && length >= 3 && !memcmp(input, "\xef\xbb\xbf", 3)) { bom = TEXT_ENCODING_UTF8; bom_bytes = 3; }
    else if (at_start && length >= 2 && input[0] == 0xff && input[1] == 0xfe) { bom = TEXT_ENCODING_UTF16LE; bom_bytes = 2; }
    else if (at_start && length >= 2 && input[0] == 0xfe && input[1] == 0xff) { bom = TEXT_ENCODING_UTF16BE; bom_bytes = 2; }
    text_encoding_t encoding = requested;
    if (encoding == TEXT_ENCODING_AUTO) encoding = bom != TEXT_ENCODING_AUTO ? bom :
        utf8_prefix_valid(input, length, has_more) ? TEXT_ENCODING_UTF8 : TEXT_ENCODING_GBK;
    result->encoding = encoding;
    size_t i = bom == encoding ? bom_bytes : 0, written = 0;
    if (source_end) source_end[0] = (uint16_t)i;
    bool previous_cr = false;
    while (i < length) {
        uint32_t cp = 0; size_t used = 1;
        decode_status_t state = encoding == TEXT_ENCODING_UTF8 ? utf8_next(input + i, length - i, &cp, &used) :
            encoding == TEXT_ENCODING_GBK ? gbk_next(input + i, length - i, &cp, &used) :
            utf16_next(input + i, length - i, encoding == TEXT_ENCODING_UTF16LE, &cp, &used);
        if (state == DECODE_INCOMPLETE && has_more) { result->incomplete_tail = true; break; }
        if (state == DECODE_OK && cp == '\r' && has_more && i + used == length) {
            result->incomplete_tail = true; break;
        }
        bool replaced = state != DECODE_OK;
        if (replaced) cp = '?';
        if (cp == '\n' && previous_cr) {
            previous_cr = false; i += used;
            if (source_end) source_end[written] = (uint16_t)i;
            continue;
        }
        previous_cr = cp == '\r';
        if (cp == '\r') cp = '\n';
        if (cp == '\t') cp = ' ';
        if ((cp < 0x20 && cp != '\n') || cp == 0x7f) { cp = '?'; replaced = true; }
        char encoded[4]; size_t bytes = encode_utf8(cp, encoded);
        if (bytes > capacity - 1 - written) { result->output_truncated = true; break; }
        memcpy(output + written, encoded, bytes);
        if (source_end) for (size_t j = 0; j < bytes; j++) source_end[written + j] = (uint16_t)i;
        written += bytes; i += used;
        if (source_end) source_end[written] = (uint16_t)i;
        if (replaced) result->replacements++;
    }
    output[written] = 0; result->input_consumed = i; result->output_size = written;
    return true;
}

bool text_preview_decode(const uint8_t *input, size_t length, bool has_more,
                         text_encoding_t requested, char *output, size_t capacity,
                         text_decode_result_t *result)
{
    return text_preview_decode_window(input, length, has_more, true, requested, output, capacity, NULL, result);
}
