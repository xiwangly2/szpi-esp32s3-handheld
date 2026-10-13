#include "ip_camera_app.h"
#include "ip_camera_stream.h"
#include "esp32_s3_szp.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_jpeg_dec.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define IPCAM_CONFIG_PATH "/sdcard/szpi/config/ip-camera.ini"
#define IPCAM_PIXELS (320 * 240)
LV_FONT_DECLARE(font_alipuhui20);
static const char *TAG = "ip_camera";

typedef struct {
    char url[IPCAM_URL_SIZE];
    unsigned max_fps, snapshot_ms;
} ip_camera_config_t;

typedef struct {
    lv_obj_t *page, *image, *top, *bottom, *status, *play_label, *gear;
    lv_obj_t *editor, *address, *edit_status;
    lv_img_dsc_t descriptor;
    uint16_t *pixels, *display;
    uint8_t *frame, *read_buffer;
    jpeg_dec_handle_t decoder, probe;
    uint16_t output_width, output_height;
    bool scaled, fullscreen, loaded;
    unsigned edit_max_fps, edit_snapshot_ms;
    ip_camera_config_t config;
    atomic_bool closing, run, import_tf;
    atomic_uint revision;
    unsigned request_revision, frames, received, max_fps;
    int64_t last_frame_us, last_render_us, report_us;
    const char *frame_error;
    void (*on_close)(void);
} camera_page_t;

typedef struct { char type[256]; bool invalid; } response_headers_t;
static camera_page_t *s_camera;

static bool active(camera_page_t *s)
{
    return !atomic_load(&s->closing) && atomic_load(&s->run) &&
           s->request_revision == atomic_load(&s->revision);
}

static void status(camera_page_t *s, const char *message)
{
    lvgl_port_lock(0);
    if (s->page && !atomic_load(&s->closing) &&
        s->request_revision == atomic_load(&s->revision) && strcmp(lv_label_get_text(s->status), message)) {
        lv_label_set_text(s->status, message);
        if (s->fullscreen) {
            s->fullscreen = false;
            lv_obj_clear_flag(s->top, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s->bottom, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lvgl_port_unlock();
}

static void update_play(camera_page_t *s)
{
    if (s->play_label) lv_label_set_text(s->play_label, atomic_load(&s->run) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void stop_current_request(camera_page_t *s)
{
    lvgl_port_lock(0);
    if (s->request_revision == atomic_load(&s->revision)) {
        atomic_store(&s->run, false);
        update_play(s);
    }
    lvgl_port_unlock();
}

static void sleep_active(camera_page_t *s, unsigned milliseconds)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)milliseconds * 1000;
    while (active(s) && esp_timer_get_time() < deadline) vTaskDelay(pdMS_TO_TICKS(20));
}

static void load_config(ip_camera_config_t *config, bool use_nvs)
{
    *config = (ip_camera_config_t){ .max_fps = 10, .snapshot_ms = 500 };
    strlcpy(config->url, CONFIG_HANDHELD_IP_CAMERA_URL, sizeof(config->url));
    if (bsp_sdcard_mount() == ESP_OK) {
        FILE *f = fopen(IPCAM_CONFIG_PATH, "r");
        if (f) {
            char line[600];
            size_t total = 0;
            while (fgets(line, sizeof(line), f) && (total += strlen(line)) <= 8192) {
                bool complete = strchr(line, '\n') != NULL || feof(f);
                if (!complete) break;
                line[strcspn(line, "\r\n")] = 0;
                if (!strncmp(line, "url=", 4)) {
                    if (strlen(line + 4) < sizeof(config->url)) strlcpy(config->url, line + 4, sizeof(config->url));
                    else config->url[0] = 0;
                }
                if (!strncmp(line, "max_fps=", 8)) {
                    unsigned value = (unsigned)strtoul(line + 8, NULL, 10);
                    if (value >= 1 && value <= 15) config->max_fps = value;
                }
                if (!strncmp(line, "snapshot_ms=", 12)) {
                    unsigned value = (unsigned)strtoul(line + 12, NULL, 10);
                    if (value >= 200 && value <= 10000) config->snapshot_ms = value;
                }
            }
            fclose(f);
        }
    }
    if (use_nvs) {
        nvs_handle_t nvs;
        if (nvs_open("ip_camera", NVS_READONLY, &nvs) == ESP_OK) {
            char url[IPCAM_URL_SIZE]; size_t size = sizeof(url);
            if (nvs_get_str(nvs, "url", url, &size) == ESP_OK && ipcam_url_valid(url))
                strlcpy(config->url, url, sizeof(config->url));
            nvs_close(nvs);
        }
    }
}

static esp_err_t save_url(const char *url)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open("ip_camera", NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;
    ret = *url ? nvs_set_str(nvs, "url", url) : nvs_erase_key(nvs, "url");
    if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
    if (ret == ESP_OK) ret = nvs_commit(nvs);
    nvs_close(nvs);
    return ret;
}

static bool receive_frame(void *context, const uint8_t *data, size_t size)
{
    camera_page_t *s = context;
    if (!active(s)) return false;
    s->received++;
    s->last_frame_us = esp_timer_get_time();
    if (s->last_render_us && s->last_frame_us - s->last_render_us < 1000000 / s->max_fps) return true;
    jpeg_dec_header_info_t original = {0};
    uint16_t width = 0, height = 0;
    s->frame_error = "JPEG格式或尺寸不支持";
    if (size < 4 || size > IPCAM_FRAME_LIMIT) return false;
    if (!s->probe) {
        jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
        config.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
        if (jpeg_dec_open(&config, &s->probe) != JPEG_ERR_OK) { s->frame_error = "解码内存不足"; return false; }
    }
    // Probe without scaling so source limits cannot be hidden by output scaling.
    jpeg_dec_io_t probe_io = { .inbuf = (uint8_t *)data, .inbuf_len = (int)size };
    if (jpeg_dec_parse_header(s->probe, &probe_io, &original) != JPEG_ERR_OK ||
        !ipcam_fit(original.width, original.height, &width, &height)) return false;
    bool scaled = original.width != width || original.height != height;
    if (scaled && ((original.width & 7) || (original.height & 7))) return false;
    if (!s->decoder || width != s->output_width || height != s->output_height || scaled != s->scaled) {
        if (s->decoder) jpeg_dec_close(s->decoder);
        s->decoder = NULL;
        jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
        config.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
        if (scaled) { config.scale.width = width; config.scale.height = height; }
        if (jpeg_dec_open(&config, &s->decoder) != JPEG_ERR_OK) { s->frame_error = "解码内存不足"; return false; }
        s->output_width = width; s->output_height = height; s->scaled = scaled;
    }
    jpeg_dec_io_t io = { .inbuf = (uint8_t *)data, .inbuf_len = (int)size, .outbuf = (uint8_t *)s->pixels };
    jpeg_dec_header_info_t info = {0}; int length = 0;
    if (jpeg_dec_parse_header(s->decoder, &io, &info) != JPEG_ERR_OK ||
        jpeg_dec_get_outbuf_len(s->decoder, &length) != JPEG_ERR_OK ||
        length != width * height * 2 || length > IPCAM_PIXELS * 2 ||
        jpeg_dec_process(s->decoder, &io) != JPEG_ERR_OK) return false;
    s->frame_error = NULL;
    lvgl_port_lock(0);
    if (s->image && active(s)) {
        uint16_t *old = s->display; s->display = s->pixels; s->pixels = old;
        lv_img_cache_invalidate_src(&s->descriptor);
        s->descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
        s->descriptor.header.w = width; s->descriptor.header.h = height;
        s->descriptor.data = (uint8_t *)s->display; s->descriptor.data_size = length;
        lv_img_set_src(s->image, &s->descriptor); lv_obj_center(s->image);
        lv_obj_invalidate(s->image);
        s->frames++;
        if (s->frames == 1 || esp_timer_get_time() - s->report_us >= 1000000) {
            lv_label_set_text_fmt(s->status, "直播  %u帧  %ux%u", s->frames, width, height);
            s->report_us = esp_timer_get_time();
        }
    }
    lvgl_port_unlock();
    s->last_render_us = esp_timer_get_time();
    if (s->frames == 1 || s->frames % 100 == 0)
        ESP_LOGI(TAG, "rendered=%u received=%u source=%ux%u output=%ux%u", s->frames, s->received,
                 original.width, original.height, width, height);
    return active(s);
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_headers_t *h = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "Content-Type")) {
        if (h->type[0] || strlen(event->header_value) >= sizeof(h->type)) h->invalid = true;
        else strlcpy(h->type, event->header_value, sizeof(h->type));
    }
    return ESP_OK;
}

static const char *request_stream(camera_page_t *s, const ip_camera_config_t *profile, bool *snapshot)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return "WLAN未连接";
    response_headers_t headers = {0};
    esp_http_client_config_t config = {
        .url = profile->url, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 1500, .buffer_size = 4096, .buffer_size_tx = 1024,
        .disable_auto_redirect = true, .event_handler = http_event, .user_data = &headers,
        .auth_type = HTTP_AUTH_TYPE_BASIC,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ipcam_stream_t *stream = NULL;
    const char *error = "连接失败";
    if (!client) return "网络内存不足";
    esp_http_client_set_header(client, "Accept", "image/jpeg, multipart/x-mixed-replace");
    if (!active(s) || esp_http_client_open(client, 0) != ESP_OK) goto done;
    int64_t content_length = esp_http_client_fetch_headers(client);
    int code = esp_http_client_get_status_code(client);
    if (code == 401 || code == 403) { error = "鉴权失败"; goto done; }
    if (code >= 300 && code < 400) { error = "请配置最终视频流地址"; goto done; }
    if (code != 200 || content_length < 0) { error = "HTTP响应失败"; goto done; }
    char boundary[IPCAM_BOUNDARY_SIZE];
    ipcam_content_t type = headers.invalid ? IPCAM_CONTENT_INVALID : ipcam_content_type(headers.type, boundary);
    *snapshot = type == IPCAM_CONTENT_JPEG;
    if (type == IPCAM_CONTENT_INVALID) { error = "仅支持JPEG/MJPEG"; goto done; }
    if (*snapshot && content_length > IPCAM_FRAME_LIMIT) { error = "图片超过512KB"; goto done; }
    if (type == IPCAM_CONTENT_MJPEG) {
        stream = ipcam_stream_create(boundary, s->frame, IPCAM_FRAME_LIMIT, receive_frame, s);
        if (!stream) { error = "解析内存不足"; goto done; }
    }
    size_t length = 0;
    s->last_frame_us = esp_timer_get_time();
    s->frame_error = NULL;
    while (active(s)) {
        if (esp_timer_get_time() - s->last_frame_us > 8000000) { error = "接收画面超时"; break; }
        int got = esp_http_client_read(client, (char *)s->read_buffer, 4096);
        if (got < 0) { error = "视频流连接中断"; break; }
        if (!got) {
            if (*snapshot && esp_http_client_is_complete_data_received(client) && length &&
                (content_length == 0 || content_length == (int64_t)length)) {
                error = receive_frame(s, s->frame, length) ? NULL : s->frame_error;
                if (!error && !active(s)) error = "已暂停";
            } else error = "视频流已结束";
            break;
        }
        if (*snapshot) {
            if ((size_t)got > IPCAM_FRAME_LIMIT - length) { error = "图片超过512KB"; break; }
            memcpy(s->frame + length, s->read_buffer, (size_t)got); length += (size_t)got;
        } else if (!ipcam_stream_feed(stream, s->read_buffer, (size_t)got)) {
            error = s->frame_error ? s->frame_error : "MJPEG数据无效或过大";
            break;
        } else if (ipcam_stream_finished(stream)) { error = "视频流已结束"; break; }
        vTaskDelay(1);
    }
done:
    ipcam_stream_destroy(stream);
    esp_http_client_cleanup(client);
    return error;
}

static void worker(void *argument)
{
    camera_page_t *s = argument;
    ip_camera_config_t profile;
    load_config(&profile, true);
    lvgl_port_lock(0);
    s->config = profile; s->loaded = true;
    if (s->gear) lv_obj_clear_state(s->gear, LV_STATE_DISABLED);
    atomic_store(&s->run, ipcam_url_valid(profile.url));
    update_play(s);
    lvgl_port_unlock();
    unsigned failures = 0, previous_revision = 0;
    while (!atomic_load(&s->closing)) {
        if (atomic_exchange(&s->import_tf, false)) {
            unsigned import_revision = atomic_load(&s->revision);
            load_config(&profile, false);
            lvgl_port_lock(0);
            if (!atomic_load(&s->closing) && s->editor && import_revision == atomic_load(&s->revision)) {
                s->edit_max_fps = profile.max_fps;
                s->edit_snapshot_ms = profile.snapshot_ms;
                if (s->address) lv_textarea_set_text(s->address, profile.url);
                if (s->edit_status) lv_label_set_text(s->edit_status, "TF");
            }
            lvgl_port_unlock();
        }
        s->request_revision = atomic_load(&s->revision);
        if (s->request_revision != previous_revision) {
            failures = 0; previous_revision = s->request_revision;
            s->frames = s->received = 0; s->last_render_us = 0;
        }
        lvgl_port_lock(0); profile = s->config; lvgl_port_unlock();
        if (!atomic_load(&s->run)) {
            status(s, !*profile.url ? "未配置地址" : ipcam_url_valid(profile.url) ? "已暂停" : "地址无效");
            vTaskDelay(pdMS_TO_TICKS(50)); continue;
        }
        if (!ipcam_url_valid(profile.url)) {
            status(s, "地址无效"); stop_current_request(s); continue;
        }
        if (!s->frame) s->frame = heap_caps_malloc(IPCAM_FRAME_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s->read_buffer) s->read_buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s->pixels) s->pixels = heap_caps_aligned_alloc(16, IPCAM_PIXELS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s->display) s->display = heap_caps_aligned_alloc(16, IPCAM_PIXELS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s->frame || !s->read_buffer || !s->pixels || !s->display) {
            status(s, "预览内存不足"); stop_current_request(s);
            while (!atomic_load(&s->closing) && s->request_revision == atomic_load(&s->revision))
                vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            s->max_fps = profile.max_fps;
            if (!s->frames) status(s, "连接中...");
            bool snapshot = false;
            unsigned before = s->received;
            const char *error = request_stream(s, &profile, &snapshot);
            if (!active(s)) continue;
            if (!error) { failures = 0; sleep_active(s, profile.snapshot_ms); continue; }
            if (s->received != before && !s->frame_error) failures = 0;
            failures++;
            status(s, error);
            ESP_LOGW(TAG, "request stopped, retry=%u rendered=%u", failures, s->frames);
            if (failures < 3) { sleep_active(s, 1000U << (failures - 1)); continue; }
            stop_current_request(s);
            // Preserve the error until the user explicitly retries or changes settings.
            lvgl_port_lock(0); update_play(s); lvgl_port_unlock();
            while (!atomic_load(&s->closing) && s->request_revision == atomic_load(&s->revision))
                vTaskDelay(pdMS_TO_TICKS(50));
        }
        lvgl_port_lock(0); update_play(s); lvgl_port_unlock();
    }
    if (s->decoder) jpeg_dec_close(s->decoder);
    if (s->probe) jpeg_dec_close(s->probe);
    lvgl_port_lock(0);
    if (s->page) lv_obj_del(s->page);
    lv_img_cache_invalidate_src(&s->descriptor);
    free(s->pixels); free(s->display); free(s->frame); free(s->read_buffer);
    s_camera = NULL;
    if (s->on_close) s->on_close();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "closed, stack_free=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    free(s); vTaskDelete(NULL);
}

static void delete_cb(lv_event_t *event)
{
    camera_page_t *s = lv_event_get_user_data(event);
    atomic_store(&s->closing, true);
    s->page = s->image = s->status = s->play_label = s->gear = NULL;
    s->editor = s->address = s->edit_status = NULL;
}

static void close_cb(lv_event_t *event)
{
    camera_page_t *s = lv_event_get_user_data(event);
    atomic_store(&s->closing, true);
    lv_label_set_text(s->status, "关闭中...");
}

static void toggle_cb(lv_event_t *event)
{
    camera_page_t *s = lv_event_get_user_data(event);
    if (!s->loaded || atomic_load(&s->closing)) return;
    atomic_fetch_add(&s->revision, 1);
    atomic_store(&s->run, !atomic_load(&s->run)); update_play(s);
}

static void retry_cb(lv_event_t *event)
{
    camera_page_t *s = lv_event_get_user_data(event);
    if (!s->loaded || atomic_load(&s->closing)) return;
    atomic_fetch_add(&s->revision, 1); atomic_store(&s->run, true); update_play(s);
}

static void fullscreen_cb(lv_event_t *event)
{
    camera_page_t *s = lv_event_get_user_data(event);
    s->fullscreen = !s->fullscreen;
    if (s->fullscreen) { lv_obj_add_flag(s->top, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s->bottom, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_clear_flag(s->top, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s->bottom, LV_OBJ_FLAG_HIDDEN); }
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width,
                        bool symbol, lv_event_cb_t cb, camera_page_t *s)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y); lv_obj_set_size(btn, width, 32);
    lv_obj_set_style_radius(btn, 4, 0); lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x176b78), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, s);
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, symbol ? &lv_font_montserrat_20 : &font_alipuhui20, 0);
    lv_label_set_text(label, text); lv_obj_center(label);
    return btn;
}

static lv_obj_t *band(lv_obj_t *parent, int y, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, 0, y); lv_obj_set_size(obj, 320, height);
    lv_obj_set_style_pad_all(obj, 0, 0); lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0); lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x192322), 0);
    lv_obj_set_style_text_color(obj, lv_color_white(), 0);
    return obj;
}

static void editor_close(camera_page_t *s)
{
    lv_obj_t *editor = s->editor;
    s->editor = s->address = s->edit_status = NULL;
    if (editor) lv_obj_del(editor);
}

static void editor_back(lv_event_t *event) { editor_close(lv_event_get_user_data(event)); }

static void editor_save(lv_event_t *event)
{
    camera_page_t *s = lv_event_get_user_data(event);
    if (!s->address || atomic_load(&s->closing)) return;
    const char *url = lv_textarea_get_text(s->address);
    if (*url && !ipcam_url_valid(url)) { lv_label_set_text(s->edit_status, "地址无效"); return; }
    if (save_url(url) != ESP_OK) { lv_label_set_text(s->edit_status, "保存失败"); return; }
    strlcpy(s->config.url, url, sizeof(s->config.url));
    s->config.max_fps = s->edit_max_fps;
    s->config.snapshot_ms = s->edit_snapshot_ms;
    atomic_fetch_add(&s->revision, 1); atomic_store(&s->run, *url != 0);
    update_play(s); editor_close(s);
}

static void editor_import(lv_event_t *event)
{
    camera_page_t *s = lv_event_get_user_data(event);
    if (!s->editor || atomic_load(&s->closing)) return;
    atomic_fetch_add(&s->revision, 1); atomic_store(&s->import_tf, true);
    lv_label_set_text(s->edit_status, "读取TF...");
}

static void settings_cb(lv_event_t *event)
{
    camera_page_t *s = lv_event_get_user_data(event);
    if (!s->loaded || s->editor || atomic_load(&s->closing)) return;
    atomic_store(&s->run, false); atomic_fetch_add(&s->revision, 1); update_play(s);
    s->edit_max_fps = s->config.max_fps;
    s->edit_snapshot_ms = s->config.snapshot_ms;
    s->editor = band(s->page, 0, 240);
    button(s->editor, LV_SYMBOL_LEFT, 2, 2, 38, true, editor_back, s);
    s->edit_status = lv_label_create(s->editor);
    lv_obj_set_style_text_font(s->edit_status, &font_alipuhui20, 0);
    lv_label_set_text(s->edit_status, "地址"); lv_obj_set_pos(s->edit_status, 48, 8);
    lv_obj_set_width(s->edit_status, 126); lv_label_set_long_mode(s->edit_status, LV_LABEL_LONG_DOT);
    button(s->editor, "TF", 182, 2, 52, true, editor_import, s);
    button(s->editor, "保存", 242, 2, 74, false, editor_save, s);
    s->address = lv_textarea_create(s->editor);
    lv_obj_set_pos(s->address, 4, 42); lv_obj_set_size(s->address, 312, 44);
    lv_obj_set_style_text_font(s->address, &lv_font_montserrat_14, 0);
    lv_textarea_set_one_line(s->address, true); lv_textarea_set_max_length(s->address, IPCAM_URL_SIZE - 1);
    lv_textarea_set_placeholder_text(s->address, "http://"); lv_textarea_set_text(s->address, s->config.url);
    lv_obj_t *keyboard = lv_keyboard_create(s->editor);
    lv_obj_set_pos(keyboard, 0, 96); lv_obj_set_size(keyboard, 320, 144);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard, s->address);
    lv_obj_add_event_cb(keyboard, editor_save, LV_EVENT_READY, s);
    lv_obj_add_event_cb(keyboard, editor_back, LV_EVENT_CANCEL, s);
}

esp_err_t ip_camera_open(void (*on_close)(void))
{
    if (s_camera) return ESP_ERR_INVALID_STATE;
    camera_page_t *s = calloc(1, sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;
    s->on_close = on_close;
    atomic_init(&s->closing, false); atomic_init(&s->run, false);
    atomic_init(&s->import_tf, false); atomic_init(&s->revision, 1);
    s->page = band(lv_scr_act(), 0, 240);
    lv_obj_set_style_bg_color(s->page, lv_color_black(), 0);
    lv_obj_add_event_cb(s->page, delete_cb, LV_EVENT_DELETE, s);
    s->image = lv_img_create(s->page); lv_obj_center(s->image);
    lv_obj_add_flag(s->image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s->image, fullscreen_cb, LV_EVENT_CLICKED, s);
    s->top = band(s->page, 0, 38);
    button(s->top, LV_SYMBOL_LEFT, 2, 3, 40, true, close_cb, s);
    lv_obj_t *title = lv_label_create(s->top);
    lv_obj_set_style_text_font(title, &font_alipuhui20, 0);
    lv_label_set_text(title, "IP相机"); lv_obj_set_pos(title, 54, 8);
    s->gear = button(s->top, LV_SYMBOL_SETTINGS, 276, 3, 42, true, settings_cb, s);
    lv_obj_add_state(s->gear, LV_STATE_DISABLED);
    s->bottom = band(s->page, 202, 38);
    lv_obj_t *play = button(s->bottom, LV_SYMBOL_PLAY, 2, 3, 42, true, toggle_cb, s);
    s->play_label = lv_obj_get_child(play, 0);
    button(s->bottom, LV_SYMBOL_REFRESH, 276, 3, 42, true, retry_cb, s);
    s->status = lv_label_create(s->bottom);
    lv_obj_set_style_text_font(s->status, &font_alipuhui20, 0);
    lv_obj_set_pos(s->status, 52, 8); lv_obj_set_width(s->status, 216);
    lv_label_set_long_mode(s->status, LV_LABEL_LONG_DOT); lv_label_set_text(s->status, "读取配置...");
    s_camera = s;
    if (xTaskCreatePinnedToCore(worker, "ip_camera", 10 * 1024, s, 4, NULL, 1) != pdPASS) {
        lv_obj_del(s->page); free(s); s_camera = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
