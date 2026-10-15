#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TEXT_ENCODING_AUTO = 0,
    TEXT_ENCODING_UTF8,
    TEXT_ENCODING_GBK,
    TEXT_ENCODING_UTF16LE,
    TEXT_ENCODING_UTF16BE,
} text_encoding_t;

typedef struct {
    text_encoding_t encoding;
    size_t input_consumed, output_size, replacements;
    bool incomplete_tail, output_truncated;
} text_decode_result_t;

const char *text_encoding_name(text_encoding_t encoding);
// Input need not be NUL-terminated. has_more means a bounded prefix, not EOF.
// Output always ends at a complete UTF-8 character and is NUL-terminated.
// Input and output must not overlap.
bool text_preview_decode(const uint8_t *input, size_t length, bool has_more,
                         text_encoding_t requested, char *output, size_t capacity,
                         text_decode_result_t *result);

// Optional source_end has capacity entries. Valid UTF-8 boundaries map back to
// byte offsets in this input window. Mapped windows must fit in UINT16_MAX bytes.
bool text_preview_decode_window(const uint8_t *input, size_t length, bool has_more, bool at_start,
                                text_encoding_t requested, char *output, size_t capacity,
                                uint16_t *source_end, text_decode_result_t *result);
