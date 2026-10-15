#include "text_reader.h"
#include "text_preview_encoding.h"
#include "esp32_s3_szp.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define READER_FILE_LIMIT (8 * 1024 * 1024)
#define READER_MAX_PAGES 65536
#define READER_INPUT 2048
#define READER_OUTPUT (READER_INPUT * 3 + 1)
#define READER_DISPLAY_LIMIT 1024
#define READER_TEXT_WIDTH 304
#define READER_TEXT_HEIGHT 162
#define READER_LINE_SPACE 2
#define READER_LAYOUT 1
#define BOOKMARK_MAGIC 0x31524454U
LV_FONT_DECLARE(font_alipuhui20);
static const char *TAG = "text_reader";

typedef struct {
    uint32_t magic, layout, sequence, file_size, page, count, requested, encoding;
    int64_t modified;
    uint64_t prefix_hash, checksum;
    char path[512];
} bookmark_t;

typedef struct reader {
    lv_obj_t *page_obj, *content, *progress, *encoding_control, *previous, *next;
    char path[512], cache_base[96];
    FILE *file;
    uint8_t *raw;
    char *utf8;
    uint16_t *source_end;
    uint32_t *offsets;
    uint32_t file_size, current, count, next_offset, sequence;
    int64_t modified, save_after;
    uint64_t prefix_hash;
    text_encoding_t requested, encoding;
    bool ready, busy, dirty, can_save, saved_ok, tracking, swiped;
    lv_point_t press;
    atomic_bool closing;
    atomic_int command, selected_encoding;
    atomic_uint revision;
} reader_t;

enum { COMMAND_NONE, COMMAND_NEXT, COMMAND_PREVIOUS, COMMAND_ENCODING };
static reader_t *s_pending;
static bool s_worker_running;

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; i++) { hash ^= bytes[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}

static uint64_t hash_start(void) { return UINT64_C(14695981039346656037); }

static void set_message(reader_t *s, const char *message)
{
    lvgl_port_lock(0);
    if (!atomic_load(&s->closing) && s->progress) lv_label_set_text(s->progress, message);
    lvgl_port_unlock();
}

static void free_reader(reader_t *s)
{
    if (s->file) fclose(s->file);
    free(s->raw); free(s->utf8); free(s->source_end); free(s->offsets); free(s);
}

static bool bookmark_header_valid(reader_t *s, const bookmark_t *header)
{
    return header->magic == BOOKMARK_MAGIC && header->layout == READER_LAYOUT &&
        header->file_size == s->file_size && header->modified == s->modified &&
        header->prefix_hash == s->prefix_hash && memchr(header->path, 0, sizeof(header->path)) &&
        !strcmp(header->path, s->path) && header->count >= 1 && header->count <= READER_MAX_PAGES &&
        header->page < header->count && header->requested <= TEXT_ENCODING_UTF16BE &&
        header->encoding >= TEXT_ENCODING_UTF8 && header->encoding <= TEXT_ENCODING_UTF16BE &&
        (header->requested == TEXT_ENCODING_AUTO || header->requested == header->encoding);
}

static bool read_bookmark(reader_t *s, unsigned slot, bookmark_t *header, bool load)
{
    char path[112]; snprintf(path, sizeof(path), "%s.%u", s->cache_base, slot);
    FILE *file = fopen(path, "rb"); if (!file) return false;
    bool ok = fread(header, 1, sizeof(*header), file) == sizeof(*header) && bookmark_header_valid(s, header);
    if (ok) {
        uint64_t expected = header->checksum; header->checksum = 0;
        uint64_t hash = hash_bytes(hash_start(), header, sizeof(*header));
        uint32_t previous = 0, block[64];
        for (uint32_t i = 0; ok && i < header->count;) {
            uint32_t count = header->count - i; if (count > 64) count = 64;
            ok = fread(block, sizeof(*block), count, file) == count;
            if (!ok) break;
            hash = hash_bytes(hash, block, count * sizeof(*block));
            for (uint32_t j = 0; ok && j < count; j++) {
                ok = i + j == 0 ? block[j] == 0 : block[j] > previous && block[j] < s->file_size;
                previous = block[j];
            }
            if (load && ok) memcpy(s->offsets + i, block, count * sizeof(*block));
            i += count;
        }
        ok = ok && hash == expected && fgetc(file) == EOF && !ferror(file);
        header->checksum = expected;
    }
    fclose(file); return ok;
}

static void load_bookmark(reader_t *s)
{
    bookmark_t a, b, chosen;
    bool first = read_bookmark(s, 0, &a, false), second = read_bookmark(s, 1, &b, false);
    if (!first && !second) return;
    unsigned slot = second && (!first || b.sequence > a.sequence) ? 1 : 0;
    if (!read_bookmark(s, slot, &chosen, true)) return;
    s->current = chosen.page; s->count = chosen.count; s->sequence = chosen.sequence;
    s->requested = (text_encoding_t)chosen.requested; s->encoding = (text_encoding_t)chosen.encoding;
    atomic_store(&s->selected_encoding, s->requested);
    ESP_LOGI(TAG, "restored page=%u offset=%u", (unsigned)s->current + 1, (unsigned)s->offsets[s->current]);
}

static bool save_bookmark(reader_t *s)
{
    if (!s->can_save || !s->dirty || !s->count || s->current >= s->count) return false;
    bookmark_t header = { .magic = BOOKMARK_MAGIC, .layout = READER_LAYOUT, .sequence = s->sequence + 1,
        .file_size = s->file_size, .page = s->current, .count = s->count, .requested = s->requested,
        .encoding = s->encoding, .modified = s->modified, .prefix_hash = s->prefix_hash };
    strlcpy(header.path, s->path, sizeof(header.path));
    header.checksum = hash_bytes(hash_bytes(hash_start(), &header, sizeof(header)), s->offsets, s->count * sizeof(*s->offsets));
    char path[112]; snprintf(path, sizeof(path), "%s.%u", s->cache_base, (unsigned)(header.sequence & 1U));
    FILE *file = fopen(path, "wb"); if (!file) return false;
    bool ok = fwrite(&header, 1, sizeof(header), file) == sizeof(header) &&
        fwrite(s->offsets, sizeof(*s->offsets), s->count, file) == s->count;
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (fclose(file)) ok = false;
    if (ok) {
        s->sequence = header.sequence; s->dirty = false;
        ESP_LOGI(TAG, "saved page=%u offsets=%u", (unsigned)s->current + 1, (unsigned)s->count);
    }
    return ok;
}

static bool prepare(reader_t *s)
{
    struct stat st;
    if (stat(s->path, &st) || st.st_size < 0 || st.st_size > READER_FILE_LIMIT) { set_message(s, "文件过大或不可用"); return false; }
    s->file_size = (uint32_t)st.st_size; s->modified = st.st_mtime;
    s->file = fopen(s->path, "rb"); if (!s->file) { set_message(s, "打开失败"); return false; }
    s->raw = heap_caps_malloc(READER_INPUT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s->utf8 = heap_caps_malloc(READER_OUTPUT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s->source_end = heap_caps_malloc(READER_OUTPUT * sizeof(*s->source_end), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s->offsets = heap_caps_malloc(READER_MAX_PAGES * sizeof(*s->offsets), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s->raw || !s->utf8 || !s->source_end || !s->offsets) { set_message(s, "阅读内存不足"); return false; }
    size_t got = fread(s->raw, 1, READER_INPUT + 1, s->file);
    if (ferror(s->file)) { set_message(s, "读取失败"); return false; }
    s->prefix_hash = hash_bytes(hash_start(), s->raw, got < 128 ? got : 128);
    text_decode_result_t result;
    text_preview_decode(s->raw, got > READER_INPUT ? READER_INPUT : got, got > READER_INPUT,
                        TEXT_ENCODING_AUTO, s->utf8, READER_OUTPUT, &result);
    s->encoding = result.encoding; s->requested = TEXT_ENCODING_AUTO;
    s->offsets[0] = 0; s->count = 1;
    const char *root = !strncmp(s->path, "/sdcard/", 8) ? "/sdcard" : !strncmp(s->path, "/local/", 7) ? "/local" : NULL;
    if (root) {
        char directory[64]; snprintf(directory, sizeof(directory), "%s/szpi", root); mkdir(directory, 0775);
        snprintf(directory, sizeof(directory), "%s/szpi/cache", root); mkdir(directory, 0775);
        snprintf(directory, sizeof(directory), "%s/szpi/cache/reader", root); mkdir(directory, 0775);
        struct stat dir;
        s->can_save = !stat(directory, &dir) && S_ISDIR(dir.st_mode);
        snprintf(s->cache_base, sizeof(s->cache_base), "%s/%016llx", directory,
                 (unsigned long long)hash_bytes(hash_start(), s->path, strlen(s->path)));
        if (s->can_save) load_bookmark(s);
    }
    s->saved_ok = s->can_save;
    return true;
}

static bool render_page(reader_t *s, uint32_t target)
{
    unsigned revision = atomic_load(&s->revision);
    int64_t started = esp_timer_get_time();
    clearerr(s->file);
    uint32_t start = s->offsets[target];
    if (fseek(s->file, (long)start, SEEK_SET)) { set_message(s, "定位失败"); return false; }
    size_t got = fread(s->raw, 1, READER_INPUT + 1, s->file);
    if (ferror(s->file) || (got == 0 && start < s->file_size)) { set_message(s, "读取失败"); return false; }
    size_t length = got > READER_INPUT ? READER_INPUT : got;
    text_decode_result_t result;
    if (!text_preview_decode_window(s->raw, length, got > length, start == 0, s->encoding,
                                    s->utf8, READER_OUTPUT, s->source_end, &result)) return false;
    if (result.output_size > READER_DISPLAY_LIMIT) {
        size_t end = READER_DISPLAY_LIMIT;
        while (((uint8_t)s->utf8[end] & 0xc0) == 0x80) end--;
        s->utf8[end] = 0; result.output_size = end;
    }
    lvgl_port_lock(0);
    if (atomic_load(&s->closing) || !s->content || revision != atomic_load(&s->revision)) { lvgl_port_unlock(); return false; }
    size_t end = 0;
    unsigned rows = (READER_TEXT_HEIGHT + READER_LINE_SPACE) / (font_alipuhui20.line_height + READER_LINE_SPACE);
    for (unsigned row = 0; row < rows && s->utf8[end]; row++) {
        uint32_t bytes = _lv_txt_get_next_line(s->utf8 + end, &font_alipuhui20, 0, READER_TEXT_WIDTH, NULL, LV_TEXT_FLAG_NONE);
        if (!bytes) break;
        if (bytes > result.output_size - end) { lv_label_set_text(s->progress, "排版失败"); lvgl_port_unlock(); return false; }
        end += bytes;
    }
    uint32_t next = start + s->source_end[end];
    if (next <= start && start < s->file_size) {
        lv_label_set_text(s->progress, "无法分页"); lvgl_port_unlock(); return false;
    }
    if (next < s->file_size) {
        if (target + 1 >= READER_MAX_PAGES) { lv_label_set_text(s->progress, "达到页数上限"); lvgl_port_unlock(); return false; }
        if (s->count <= target + 1 || s->offsets[target + 1] != next) {
            s->offsets[target + 1] = next; s->count = target + 2;
        }
    }
    s->utf8[end] = 0;
    if (end && s->utf8[end - 1] == '\n') s->utf8[end - 1] = 0;
    lv_label_set_text(s->content, end ? s->utf8 : "空文件");
    s->current = target; s->next_offset = next;
    unsigned percent = next >= s->file_size ? 1000 : (unsigned)((uint64_t)start * 1000 / s->file_size);
    lv_label_set_text_fmt(s->progress, "第%u页 %u.%u%%%s", (unsigned)target + 1, percent / 10, percent % 10, s->saved_ok ? "" : " !");
    if (target) lv_obj_clear_state(s->previous, LV_STATE_DISABLED); else lv_obj_add_state(s->previous, LV_STATE_DISABLED);
    if (next < s->file_size) lv_obj_clear_state(s->next, LV_STATE_DISABLED); else lv_obj_add_state(s->next, LV_STATE_DISABLED);
    char options[96]; snprintf(options, sizeof(options), "Auto: %s\nUTF-8\nGBK / ANSI\nUTF-16LE\nUTF-16BE", text_encoding_name(s->encoding));
    s->ready = false;
    lv_dropdown_set_options(s->encoding_control, options); lv_dropdown_set_selected(s->encoding_control, s->requested);
    s->ready = true; s->dirty = true; s->save_after = esp_timer_get_time() + 1500000;
    lvgl_port_unlock();
    ESP_LOGI(TAG, "page=%u offset=%u next=%u encoding=%s time_ms=%u replacements=%u",
             (unsigned)target + 1, (unsigned)start, (unsigned)next, text_encoding_name(s->encoding),
             (unsigned)((esp_timer_get_time() - started) / 1000), (unsigned)result.replacements);
    return true;
}

static void run_reader(reader_t *s)
{
    bool prepared = prepare(s);
    if (prepared && !atomic_load(&s->closing)) render_page(s, s->current);
    lvgl_port_lock(0); s->busy = false; lvgl_port_unlock();
    while (!atomic_load(&s->closing)) {
        int command = atomic_exchange(&s->command, COMMAND_NONE);
        if (prepared && command) {
            uint32_t target = s->current;
            if (command == COMMAND_NEXT && s->next_offset < s->file_size) target++;
            if (command == COMMAND_PREVIOUS && target) target--;
            if (command == COMMAND_ENCODING) {
                s->requested = (text_encoding_t)atomic_load(&s->selected_encoding);
                if (s->requested != TEXT_ENCODING_AUTO) s->encoding = s->requested;
                else {
                    clearerr(s->file); fseek(s->file, 0, SEEK_SET);
                    size_t got = fread(s->raw, 1, READER_INPUT + 1, s->file); text_decode_result_t detected;
                    text_preview_decode(s->raw, got > READER_INPUT ? READER_INPUT : got, got > READER_INPUT,
                                        TEXT_ENCODING_AUTO, s->utf8, READER_OUTPUT, &detected); s->encoding = detected.encoding;
                }
                target = 0; s->count = 1; s->offsets[0] = 0;
            }
            if (target < s->count) render_page(s, target);
            lvgl_port_lock(0); s->busy = false; lvgl_port_unlock();
        }
        if (prepared && s->dirty && esp_timer_get_time() >= s->save_after) {
            s->saved_ok = save_bookmark(s); s->save_after = esp_timer_get_time() + 10000000;
            if (!s->saved_ok) set_message(s, "进度未保存");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (prepared && s->dirty) save_bookmark(s);
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        lvgl_port_lock(0);
        reader_t *s = s_pending; s_pending = NULL;
        if (!s) { s_worker_running = false; lvgl_port_unlock(); vTaskDelete(NULL); return; }
        lvgl_port_unlock();
        if (!atomic_load(&s->closing)) run_reader(s);
        ESP_LOGI(TAG, "closed stack_free=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (s->file) { fclose(s->file); s->file = NULL; }
        // Wait for LVGL's full deletion cascade before freeing callback data.
        lvgl_port_lock(0);
        free_reader(s);
        lvgl_port_unlock();
    }
}

static void delete_cb(lv_event_t *event)
{
    reader_t *s = lv_event_get_user_data(event); atomic_store(&s->closing, true);
    s->page_obj = s->content = s->progress = s->encoding_control = s->previous = s->next = NULL;
}

static void request_turn(reader_t *s, int command)
{
    if (!s->ready || s->busy || atomic_load(&s->closing)) return;
    if ((command == COMMAND_PREVIOUS && !s->current) || (command == COMMAND_NEXT && s->next_offset >= s->file_size)) return;
    s->busy = true; atomic_store(&s->command, command);
}

static void previous_cb(lv_event_t *e) { request_turn(lv_event_get_user_data(e), COMMAND_PREVIOUS); }
static void next_cb(lv_event_t *e) { request_turn(lv_event_get_user_data(e), COMMAND_NEXT); }
static void encoding_cb(lv_event_t *e)
{
    reader_t *s = lv_event_get_user_data(e);
    if (!s->ready || atomic_load(&s->closing)) return;
    s->busy = true; atomic_fetch_add(&s->revision, 1);
    atomic_store(&s->selected_encoding, lv_dropdown_get_selected(s->encoding_control));
    atomic_store(&s->command, COMMAND_ENCODING);
}

static void pointer_cb(lv_event_t *e)
{
    reader_t *s = lv_event_get_user_data(e); lv_indev_t *input = lv_indev_get_act(); if (!input) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { lv_indev_get_point(input, &s->press); s->tracking = true; s->swiped = false; }
    else if (code == LV_EVENT_PRESSING && s->tracking && !s->swiped) {
        lv_point_t point; lv_indev_get_point(input, &point);
        int dx = point.x - s->press.x, dy = point.y - s->press.y;
        if (abs(dx) >= 40 && abs(dx) > abs(dy) + 12) { s->swiped = true; request_turn(s, dx < 0 ? COMMAND_NEXT : COMMAND_PREVIOUS); }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) s->tracking = false;
}

static lv_obj_t *arrow(lv_obj_t *body, const char *symbol, int x, lv_event_cb_t callback, reader_t *s)
{
    lv_obj_t *button = lv_btn_create(body); lv_obj_set_pos(button, x, 166); lv_obj_set_size(button, 42, 32);
    lv_obj_set_style_shadow_opa(button, 0, 0); lv_obj_set_style_radius(button, 4, 0);
    lv_obj_t *label = lv_label_create(button); lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, symbol); lv_obj_center(label); lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, s);
    lv_obj_add_state(button, LV_STATE_DISABLED); return button;
}

esp_err_t text_reader_open(lv_obj_t *page, lv_obj_t *bar, lv_obj_t *body, lv_obj_t *title, const char *path)
{
    if (s_pending && !atomic_load(&s_pending->closing)) return ESP_ERR_INVALID_STATE;
    if (!path || strlen(path) >= sizeof(((reader_t *)0)->path)) return ESP_ERR_INVALID_ARG;
    reader_t *s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s) return ESP_ERR_NO_MEM;
    strlcpy(s->path, path, sizeof(s->path)); s->busy = true;
    atomic_init(&s->closing, false); atomic_init(&s->command, COMMAND_NONE);
    atomic_init(&s->selected_encoding, TEXT_ENCODING_AUTO); atomic_init(&s->revision, 1);
    if (!s_worker_running) {
        if (xTaskCreatePinnedToCore(worker, "text_reader", 8192, NULL, 3, NULL, 1) != pdPASS) { free(s); return ESP_ERR_NO_MEM; }
        s_worker_running = true;
    }
    if (s_pending) { free_reader(s_pending); s_pending = NULL; }
    s_pending = s; s->page_obj = page;
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, s);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    const char *name = strrchr(path, '/'); lv_label_set_text(title, name ? name + 1 : path);
    lv_obj_set_width(title, 118); lv_label_set_long_mode(title, LV_LABEL_LONG_DOT); lv_obj_align(title, LV_ALIGN_LEFT_MID, 62, 0);
    s->encoding_control = lv_dropdown_create(bar); lv_obj_set_size(s->encoding_control, 134, 34);
    lv_obj_align(s->encoding_control, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_text_font(s->encoding_control, &lv_font_montserrat_14, 0);
    lv_dropdown_set_options(s->encoding_control, "Auto\nUTF-8\nGBK / ANSI\nUTF-16LE\nUTF-16BE");
    lv_obj_add_event_cb(s->encoding_control, encoding_cb, LV_EVENT_VALUE_CHANGED, s);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(body, pointer_cb, LV_EVENT_ALL, s);
    s->content = lv_label_create(body); lv_obj_set_pos(s->content, 8, 2); lv_obj_set_size(s->content, READER_TEXT_WIDTH, READER_TEXT_HEIGHT);
    lv_obj_set_style_text_font(s->content, &font_alipuhui20, 0); lv_obj_set_style_text_line_space(s->content, READER_LINE_SPACE, 0);
    lv_obj_set_style_text_color(s->content, lv_color_white(), 0); lv_label_set_text(s->content, "读取中...");
    lv_obj_clear_flag(s->content, LV_OBJ_FLAG_CLICKABLE);
    s->previous = arrow(body, LV_SYMBOL_LEFT, 2, previous_cb, s); s->next = arrow(body, LV_SYMBOL_RIGHT, 276, next_cb, s);
    s->progress = lv_label_create(body); lv_obj_set_pos(s->progress, 50, 171); lv_obj_set_width(s->progress, 220);
    lv_obj_set_style_text_font(s->progress, &font_alipuhui20, 0); lv_obj_set_style_text_align(s->progress, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s->progress, lv_color_white(), 0); lv_label_set_text(s->progress, "加载进度...");
    return ESP_OK;
}
