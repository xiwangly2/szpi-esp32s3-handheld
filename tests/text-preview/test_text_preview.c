#include "text_preview_encoding.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static void check(const uint8_t *data, size_t size, bool more, text_encoding_t encoding,
                  const char *expected, text_encoding_t detected, size_t errors, bool incomplete)
{
    char out[512]; text_decode_result_t result;
    assert(text_preview_decode(data, size, more, encoding, out, sizeof(out), &result));
    assert(!strcmp(out, expected)); assert(result.encoding == detected); assert(result.replacements == errors);
    assert(result.incomplete_tail == incomplete); assert(result.output_size == strlen(out));
}

static void samples(void)
{
    const uint8_t gbk[] = {0xd6,0xd0,0xce,0xc4,0xa3,0xac,0xc4,0xe3,0xba,0xc3,0xa3,0xa1};
    const char *chinese = "\xe4\xb8\xad\xe6\x96\x87\xef\xbc\x8c\xe4\xbd\xa0\xe5\xa5\xbd\xef\xbc\x81";
    check(gbk, sizeof(gbk), false, TEXT_ENCODING_AUTO, chinese, TEXT_ENCODING_GBK, 0, false);
    check((const uint8_t *)chinese, strlen(chinese), false, TEXT_ENCODING_AUTO, chinese, TEXT_ENCODING_UTF8, 0, false);
    const uint8_t le[] = {0xff,0xfe,0x2d,0x4e,0x87,0x65,0x3d,0xd8,0x00,0xde};
    const uint8_t be[] = {0xfe,0xff,0x4e,0x2d,0x65,0x87,0xd8,0x3d,0xde,0x00};
    const char *utf16_output = "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80";
    check(le, sizeof(le), false, TEXT_ENCODING_AUTO, utf16_output, TEXT_ENCODING_UTF16LE, 0, false);
    check(be, sizeof(be), false, TEXT_ENCODING_AUTO, utf16_output, TEXT_ENCODING_UTF16BE, 0, false);
    check(le + 2, sizeof(le) - 2, false, TEXT_ENCODING_UTF16LE, utf16_output, TEXT_ENCODING_UTF16LE, 0, false);
    const uint8_t bom[] = {0xef,0xbb,0xbf,0xe4,0xb8,0xad};
    check(bom, sizeof(bom), false, TEXT_ENCODING_AUTO, "\xe4\xb8\xad", TEXT_ENCODING_UTF8, 0, false);
    const uint8_t controls[] = {'A','\r','\n','B','\r','C','\t','D',0,'E'};
    check(controls, sizeof(controls), false, TEXT_ENCODING_AUTO, "A\nB\nC D?E", TEXT_ENCODING_UTF8, 1, false);
    check(NULL, 0, false, TEXT_ENCODING_AUTO, "", TEXT_ENCODING_UTF8, 0, false);
    const uint8_t euro[] = {0x80};
    check(euro, sizeof(euro), false, TEXT_ENCODING_AUTO, "\xe2\x82\xac", TEXT_ENCODING_GBK, 0, false);
    // Both interpretations are possible without a BOM; manual GBK must be available.
    const uint8_t ambiguous[] = {0xc2,0xa9};
    check(ambiguous, 2, false, TEXT_ENCODING_AUTO, "\xc2\xa9", TEXT_ENCODING_UTF8, 0, false);
    char forced[16]; text_decode_result_t result;
    assert(text_preview_decode(ambiguous, 2, false, TEXT_ENCODING_GBK, forced, sizeof(forced), &result));
    assert(result.replacements == 0 && strcmp(forced, "\xc2\xa9"));
}

static void boundaries(void)
{
    const uint8_t utf8[] = {'A',0xe4,0xb8};
    check(utf8, sizeof(utf8), true, TEXT_ENCODING_AUTO, "A", TEXT_ENCODING_UTF8, 0, true);
    check(utf8, sizeof(utf8), false, TEXT_ENCODING_UTF8, "A??", TEXT_ENCODING_UTF8, 2, false);
    const uint8_t gbk[] = {'A',0xd6};
    check(gbk, sizeof(gbk), true, TEXT_ENCODING_GBK, "A", TEXT_ENCODING_GBK, 0, true);
    check(gbk, sizeof(gbk), false, TEXT_ENCODING_GBK, "A?", TEXT_ENCODING_GBK, 1, false);
    const uint8_t utf16[] = {0xff,0xfe,'A',0,0x3d,0xd8,0x00};
    check(utf16, sizeof(utf16), true, TEXT_ENCODING_AUTO, "A", TEXT_ENCODING_UTF16LE, 0, true);
    const uint8_t invalid_utf8[] = {0xc0,0xaf,0xed,0xa0,0x80,0xf4,0x90,0x80,0x80};
    check(invalid_utf8, sizeof(invalid_utf8), false, TEXT_ENCODING_UTF8, "?????????", TEXT_ENCODING_UTF8, 9, false);
    const uint8_t invalid_gbk[] = {0x81,0x7f,'A',0xff};
    check(invalid_gbk, sizeof(invalid_gbk), false, TEXT_ENCODING_GBK, "??A?", TEXT_ENCODING_GBK, 3, false);
    const uint8_t bad_surrogate[] = {0x00,0xd8,'A',0,0x00,0xdc};
    check(bad_surrogate, sizeof(bad_surrogate), false, TEXT_ENCODING_UTF16LE, "?A?", TEXT_ENCODING_UTF16LE, 2, false);
    uint8_t long_text[4096]; memset(long_text, 'a', sizeof(long_text)); long_text[4095] = 0xe4;
    char out[12289]; text_decode_result_t result;
    assert(text_preview_decode(long_text, sizeof(long_text), true, TEXT_ENCODING_AUTO, out, sizeof(out), &result));
    assert(result.encoding == TEXT_ENCODING_UTF8 && result.incomplete_tail && result.output_size == 4095);
    const uint8_t character[] = {0xd6,0xd0};
    for (size_t capacity = 1; capacity <= 4; capacity++) {
        unsigned char guarded[6]; memset(guarded, 0xa5, sizeof(guarded));
        assert(text_preview_decode(character, 2, false, TEXT_ENCODING_GBK, (char *)guarded + 1, capacity, &result));
        assert(guarded[0] == 0xa5 && guarded[capacity + 1] == 0xa5);
        assert(result.output_size == (capacity == 4 ? 3U : 0U)); assert(result.output_truncated == (capacity < 4));
    }
}

static void malformed_inputs(void)
{
    uint32_t random = 0x32535049;
    for (unsigned sample = 0; sample < 12000; sample++) {
        uint8_t input[32], output[100];
        for (size_t i = 0; i < sizeof(input); i++) { random = random * 1664525u + 1013904223u; input[i] = (uint8_t)(random >> 24); }
        size_t length = sample % (sizeof(input) + 1), capacity = sample % 97 + 1;
        memset(output, 0xa5, sizeof(output)); text_decode_result_t result;
        assert(text_preview_decode(input, length, sample % 2 != 0, (text_encoding_t)(sample % 5),
                                   (char *)output + 1, capacity, &result));
        assert(result.input_consumed <= length && result.output_size < capacity);
        assert(output[0] == 0xa5 && output[capacity + 1] == 0xa5 && output[1 + result.output_size] == 0);
#ifdef _WIN32
        if (result.output_size) {
            wchar_t decoded[100];
            assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)output + 1,
                                       (int)result.output_size, decoded, 100) > 0);
        }
#endif
    }
}

static void source_offsets(void)
{
    const uint8_t data[] = {0xff,0xfe,0x2d,0x4e,'\r',0,'\n',0,0x87,0x65};
    char output[64]; uint16_t map[64]; text_decode_result_t result;
    assert(text_preview_decode_window(data, sizeof(data), false, true, TEXT_ENCODING_AUTO, output, sizeof(output), map, &result));
    assert(!strcmp(output, "\xe4\xb8\xad\n\xe6\x96\x87"));
    assert(map[0] == 2 && map[3] == 4 && map[4] == 8 && map[7] == 10);
    const uint8_t newline_boundary[] = {'A','\r'};
    assert(text_preview_decode_window(newline_boundary, 2, true, false, TEXT_ENCODING_UTF8, output, sizeof(output), map, &result));
    assert(!strcmp(output, "A") && map[1] == 1 && result.incomplete_tail);
    const uint8_t middle_bom[] = {0xef,0xbb,0xbf,'A'};
    assert(text_preview_decode_window(middle_bom, sizeof(middle_bom), false, false, TEXT_ENCODING_UTF8, output, sizeof(output), map, &result));
    assert(result.output_size == 4 && map[3] == 3 && map[4] == 4);
}

static void paging_roundtrip(void)
{
    const char *lines[] = { "\xd6\xd0\xce\xc4\r\nABC\t\n", "\xe4\xb8\xad\xe6\x96\x87\r\nABC\t\n" };
    const char *expected_line = "\xe4\xb8\xad\xe6\x96\x87\nABC \n";
    for (unsigned kind = 0; kind < 2; kind++) {
        size_t line_size = strlen(lines[kind]), expected_size = strlen(expected_line);
        size_t size = line_size * 4000, expected_total = expected_size * 4000;
        uint8_t *input = malloc(size); char *actual = calloc(1, size * 3 + 1); assert(input && actual);
        for (unsigned i = 0; i < 4000; i++) memcpy(input + i * line_size, lines[kind], line_size);
        size_t offset = 0, written = 0; text_encoding_t encoding = TEXT_ENCODING_AUTO;
        while (offset < size) {
            char window[128]; uint16_t map[128]; text_decode_result_t result;
            size_t remaining = size - offset, length = remaining > 37 ? 37 : remaining;
            assert(text_preview_decode_window(input + offset, length, remaining > length, offset == 0, encoding,
                                              window, sizeof(window), map, &result));
            encoding = result.encoding; assert(result.replacements == 0);
            size_t end = 0; unsigned characters = 0;
            while (window[end]) {
                if (((uint8_t)window[end] & 0xc0) != 0x80 && characters++ == 7) break;
                end++;
            }
            assert(map[end] && offset + map[end] <= size);
            memcpy(actual + written, window, end); written += end; offset += map[end];
        }
        assert(written == expected_total);
        for (unsigned i = 0; i < 4000; i++) assert(!memcmp(actual + i * expected_size, expected_line, expected_size));
        free(actual); free(input);
    }
}

#ifdef _WIN32
static void real_book(const wchar_t *path)
{
    FILE *file = _wfopen(path, L"rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long size = ftell(file); assert(size > 0 && size <= 8 * 1024 * 1024);
    rewind(file); uint8_t *raw = malloc((size_t)size); assert(raw);
    assert(fread(raw, 1, (size_t)size, file) == (size_t)size); fclose(file);
    char *actual = calloc(1, (size_t)size * 3 + 1); assert(actual);
    size_t offset = 0, written = 0; unsigned pages = 0; text_encoding_t encoding = TEXT_ENCODING_AUTO;
    while (offset < (size_t)size) {
        char window[6145]; uint16_t map[6145]; text_decode_result_t result;
        size_t remaining = (size_t)size - offset, length = remaining > 2048 ? 2048 : remaining;
        assert(text_preview_decode_window(raw + offset, length, remaining > length, offset == 0, encoding,
                                          window, sizeof(window), map, &result));
        encoding = result.encoding; assert(result.replacements == 0);
        size_t end = 0; unsigned characters = 0;
        while (window[end]) {
            if (((uint8_t)window[end] & 0xc0) != 0x80 && characters++ == 80) break;
            end++;
        }
        assert(map[end] > 0 && offset + map[end] <= (size_t)size);
        memcpy(actual + written, window, end); written += end; offset += map[end]; pages++;
    }
    actual[written] = 0;
    wchar_t *decoded = malloc(((size_t)size + 1) * sizeof(*decoded)); assert(decoded);
    size_t start = size >= 3 && !memcmp(raw, "\xef\xbb\xbf", 3) ? 3 : 0;
    int count = MultiByteToWideChar(encoding == TEXT_ENCODING_GBK ? 936 : CP_UTF8, MB_ERR_INVALID_CHARS,
                                  (const char *)raw + start, (int)((size_t)size - start), decoded, (int)size + 1);
    assert(count > 0); int n = 0; bool cr = false;
    for (int i = 0; i < count; i++) {
        wchar_t c = decoded[i]; if (c == '\n' && cr) { cr = false; continue; }
        cr = c == '\r'; if (c == '\r') c = '\n'; if (c == '\t') c = ' ';
        if ((c < 32 && c != '\n') || c == 127) c = '?'; decoded[n++] = c;
    }
    char *expected = calloc(1, (size_t)size * 3 + 1); assert(expected);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, decoded, n, expected, (int)size * 3, NULL, NULL);
    assert(bytes >= 0 && (size_t)bytes == written && !memcmp(actual, expected, written));
    printf("Full-file mapped paging matched Windows: pages=%u bytes=%zu\n", pages, written);
    free(expected); free(decoded); free(actual); free(raw);
}

static void real_file(const wchar_t *path)
{
    FILE *file = _wfopen(path, L"rb"); assert(file);
    uint8_t data[4097]; size_t got = fread(data, 1, sizeof(data), file); assert(!ferror(file)); fclose(file);
    size_t length = got > 4096 ? 4096 : got;
    char actual[12289]; text_decode_result_t result;
    assert(text_preview_decode(data, length, got > length, TEXT_ENCODING_AUTO, actual, sizeof(actual), &result));
    assert(result.replacements == 0);
    // Compare against Windows' own decoder, including the same prefix boundary.
    int cp = result.encoding == TEXT_ENCODING_GBK ? 936 : CP_UTF8;
    assert(result.encoding == TEXT_ENCODING_GBK || result.encoding == TEXT_ENCODING_UTF8);
    size_t start = length >= 3 && !memcmp(data, "\xef\xbb\xbf", 3) ? 3 : 0;
    wchar_t decoded[8192], normalized[8192];
    int count = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, (const char *)data + start, (int)(result.input_consumed - start), decoded, 8192);
    assert(count > 0); int n = 0; bool previous_cr = false;
    for (int i = 0; i < count; i++) {
        wchar_t c = decoded[i]; if (c == '\n' && previous_cr) { previous_cr = false; continue; }
        previous_cr = c == '\r'; if (c == '\r') c = '\n'; if (c == '\t') c = ' ';
        if ((c < 32 && c != '\n') || c == 127) c = '?'; normalized[n++] = c;
    }
    char expected[24576];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, normalized, n, expected, sizeof(expected) - 1, NULL, NULL); assert(bytes > 0);
    expected[bytes] = 0; assert(!strcmp(actual, expected));
    printf("Sample matched Windows decoder: encoding=%s input=%zu output=%zu dropped_tail=%d\n",
           text_encoding_name(result.encoding), length, result.output_size, result.incomplete_tail);
}
#endif

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    samples(); boundaries(); malformed_inputs(); source_offsets(); paging_roundtrip();
#ifdef _WIN32
    for (int i = 1; i < argc; i++) { real_file(argv[i]); real_book(argv[i]); }
#else
    (void)argc; (void)argv;
#endif
    puts("Text preview encoding checks passed"); return 0;
}
