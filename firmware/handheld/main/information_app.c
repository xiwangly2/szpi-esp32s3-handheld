#include "information_app.h"
#include "information_payload.h"
#include "information_setup.h"
#include "esp32_s3_szp.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

LV_FONT_DECLARE(font_alipuhui20);
#define INFO_CONFIG_PATH "/sdcard/szpi/config/info.ini"
typedef struct { char url[INFO_URL_SIZE], token[INFO_TOKEN_SIZE]; } info_config_t;
typedef struct {
    lv_obj_t *page, *list, *status, *tabs, *gear, *editor, *address, *token, *keyboard, *edit_status;
    lv_obj_t *setup_page, *setup_content, *setup_toggle, *setup_status;
    info_setup_t *setup;
    info_setup_details_t setup_details;
    info_config_t config, pending_config;
    info_document_t *document;
    atomic_bool closing, refresh, import_tf, save;
    atomic_bool setup_stop, setup_busy;
    atomic_int setup_request;
    atomic_uint revision;
    bool loaded;
    bool setup_wifi_qr, setup_saved;
    unsigned tab;
    void (*on_close)(void);
} info_page_t;
static info_page_t *s_info;
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int width, bool symbol, lv_event_cb_t cb, info_page_t *s);
static void setup_show_details(info_page_t *s);
static void setup_close_ui(info_page_t *s);

static lv_obj_t *label(lv_obj_t *parent, const char *text, int width)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, &font_alipuhui20, 0);
    lv_obj_set_width(obj, width); lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_label_set_text(obj, text); return obj;
}

static lv_obj_t *band(lv_obj_t *parent, int y, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, 0, y); lv_obj_set_size(obj, 320, height);
    lv_obj_set_style_pad_all(obj, 0, 0); lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0); lv_obj_set_style_bg_color(obj, lv_color_hex(0x171c1b), 0);
    lv_obj_set_style_text_color(obj, lv_color_white(), 0); lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void render(info_page_t *s)
{
    if (!s->list || atomic_load(&s->closing)) return;
    lv_obj_clean(s->list);
    if (!s->document) { label(s->list, "暂无内容", 288); return; }
    const info_section_t *section = &s->document->sections[s->tab];
    const char *state = !strcmp(section->state, "stale") ? "缓存内容 / 来源暂不可用" :
        !strcmp(section->state, "error") ? "来源暂不可用" : !strcmp(section->state, "unconfigured") ? "后端未配置此来源" : NULL;
    if (state) { lv_obj_t *notice = label(s->list, state, 288); lv_obj_set_style_text_color(notice, lv_color_hex(0xf0c660), 0); }
    for (unsigned i = 0; i < section->count; i++) {
        lv_obj_t *title = label(s->list, section->items[i].title, 288);
        lv_obj_set_style_text_color(title, lv_color_hex(0x82d9b5), 0);
        label(s->list, section->items[i].detail, 288);
    }
    if (!section->count && !state) label(s->list, "暂无条目", 288);
    char stamp[48] = "";
    if (section->updated_at) {
        time_t when = (time_t)section->updated_at; struct tm tm;
        gmtime_r(&when, &tm); strftime(stamp, sizeof(stamp), "%m-%d %H:%M UTC", &tm);
    }
    lv_obj_t *source = label(s->list, section->source, 288);
    lv_obj_set_style_text_font(source, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(source, lv_color_hex(0xa8afad), 0);
    lv_obj_t *time_label = label(s->list, stamp, 288);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
    lv_obj_scroll_to_y(s->list, 0, LV_ANIM_OFF);
}

static void set_status(info_page_t *s, const char *message)
{
    lvgl_port_lock(0);
    if (s->status && !atomic_load(&s->closing)) lv_label_set_text(s->status, message);
    lvgl_port_unlock();
}

static void load_config(info_config_t *out, bool use_nvs)
{
    memset(out, 0, sizeof(*out));
    if (bsp_sdcard_mount() == ESP_OK) {
        FILE *file = fopen(INFO_CONFIG_PATH, "r");
        if (file) {
            char line[512]; size_t total = 0;
            while (fgets(line, sizeof(line), file) && (total += strlen(line)) <= 4096) {
                if (!strchr(line, '\n') && !feof(file)) break;
                line[strcspn(line, "\r\n")] = 0;
                if (!strncmp(line, "url=", 4) && info_url_valid(line + 4)) strlcpy(out->url, line + 4, sizeof(out->url));
                if (!strncmp(line, "token=", 6) && info_token_valid(line + 6)) strlcpy(out->token, line + 6, sizeof(out->token));
            }
            fclose(file);
        }
    }
    nvs_handle_t nvs;
    if (use_nvs && nvs_open("info_desk", NVS_READONLY, &nvs) == ESP_OK) {
        info_config_t saved = {0}; size_t size = sizeof(saved), u = sizeof(saved.url), t = sizeof(saved.token);
        bool found = nvs_get_blob(nvs, "profile", &saved, &size) == ESP_OK && size == sizeof(saved);
        if (!found) found = nvs_get_str(nvs, "url", saved.url, &u) == ESP_OK && nvs_get_str(nvs, "token", saved.token, &t) == ESP_OK;
        if (found && memchr(saved.url, 0, sizeof(saved.url)) && memchr(saved.token, 0, sizeof(saved.token)) &&
            info_url_valid(saved.url) && info_token_valid(saved.token)) *out = saved;
        nvs_close(nvs);
    }
}

static esp_err_t save_config(const info_config_t *config)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open("info_desk", NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;
    // A single NVS value prevents a partial URL/token update after power loss.
    ret = nvs_set_blob(nvs, "profile", config, sizeof(*config));
    if (ret == ESP_OK) ret = nvs_commit(nvs);
    nvs_close(nvs); return ret;
}

typedef struct { info_page_t *page; unsigned revision; char mime[64]; } request_context_t;
static esp_err_t http_event(esp_http_client_event_t *event)
{
    request_context_t *context = event->user_data;
    if (atomic_load(&context->page->closing) || context->revision != atomic_load(&context->page->revision)) return ESP_FAIL;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value && !strcasecmp(event->header_key, "Content-Type"))
        strlcpy(context->mime, event->header_value, sizeof(context->mime));
    return ESP_OK;
}

static const char *fetch_info(info_page_t *s, info_document_t *result, unsigned revision)
{
    if (!*s->config.url) return "未配置服务地址";
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return "WLAN未连接 / 保留上次内容";
    request_context_t context = { .page = s, .revision = revision };
    esp_http_client_config_t cfg = { .url = s->config.url, .timeout_ms = 2500, .buffer_size = 1024,
        .buffer_size_tx = 512, .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true,
        .event_handler = http_event, .user_data = &context };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char *body = heap_caps_malloc(INFO_BODY_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const char *error = "连接失败 / 保留上次内容";
    if (!client || !body) { error = "内存不足"; goto done; }
    char auth[INFO_TOKEN_SIZE + 8];
    if (*s->config.token) {
        snprintf(auth, sizeof(auth), "Bearer %s", s->config.token);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (esp_http_client_open(client, 0) != ESP_OK) goto done;
    // A cold serverless request can exceed one socket poll; keep the same request
    // alive while checking cancellation between documented EAGAIN returns.
    int64_t header_deadline = esp_timer_get_time() + 12000000;
    int64_t length;
    do {
        if (atomic_load(&s->closing) || revision != atomic_load(&s->revision)) goto done;
        int64_t remaining = header_deadline - esp_timer_get_time();
        if (remaining <= 0) { error = "等待服务响应超时"; goto done; }
        esp_http_client_set_timeout_ms(client, remaining < 2500000 ? (int)((remaining + 999) / 1000) : 2500);
        length = esp_http_client_fetch_headers(client);
    } while (length == -ESP_ERR_HTTP_EAGAIN);
    esp_http_client_set_timeout_ms(client, 2500);
    int code = esp_http_client_get_status_code(client);
    if (code != 200) { error = code == 401 || code == 403 ? "只读令牌无效" : "服务响应异常"; goto done; }
    if (length < 0 || length > INFO_BODY_LIMIT || strncasecmp(context.mime, "application/json", 16)) {
        error = "响应格式或大小不支持"; goto done;
    }
    size_t used = 0; int64_t deadline = esp_timer_get_time() + 12000000;
    while (!esp_http_client_is_complete_data_received(client)) {
        if (atomic_load(&s->closing) || revision != atomic_load(&s->revision)) goto done;
        if (esp_timer_get_time() > deadline) { error = "响应过大或超时"; goto done; }
        int read = esp_http_client_read(client, body + used, INFO_BODY_LIMIT + 1 - used);
        if (read == -ESP_ERR_HTTP_EAGAIN) continue;
        if (read < 0 || (read == 0 && !esp_http_client_is_complete_data_received(client))) goto done;
        used += read;
        if (used > INFO_BODY_LIMIT) { error = "响应过大"; goto done; }
    }
    body[used] = 0;
    error = info_document_parse(body, used, result) ? NULL : "信息格式不支持";
    ESP_LOGI("information", "response status=%d bytes=%u parsed=%d", code, (unsigned)used, error == NULL);
done:
    if (client) esp_http_client_cleanup(client);
    free(body); return error;
}

static void editor_close(info_page_t *s)
{
    lv_obj_t *editor = s->editor;
    s->editor = s->address = s->token = s->keyboard = s->edit_status = NULL;
    if (editor) lv_obj_del(editor);
}

static void worker(void *arg)
{
    info_page_t *s = arg;
    load_config(&s->config, true);
    lvgl_port_lock(0);
    s->loaded = true;
    if (s->gear) lv_obj_clear_state(s->gear, LV_STATE_DISABLED);
    lvgl_port_unlock();
    int64_t next = 0;
    while (!atomic_load(&s->closing)) {
        int setup_request = atomic_exchange(&s->setup_request, 0);
        if (setup_request && !s->setup) {
            s->setup_saved = false;
            esp_err_t ret = info_setup_start(setup_request == 2, s->config.url, *s->config.token != 0, &s->setup, &s->setup_details);
            lvgl_port_lock(0);
            if (s->setup_page && !atomic_load(&s->closing)) {
                if (ret == ESP_OK) { s->setup_wifi_qr = s->setup_details.hotspot; setup_show_details(s); }
                else if (s->setup_status) lv_label_set_text(s->setup_status, "启动失败，请先打开WLAN");
            } else atomic_store(&s->setup_stop, true);
            lvgl_port_unlock();
            if (ret != ESP_OK) atomic_store(&s->setup_busy, false);
        }
        if (!s->setup && atomic_load(&s->setup_stop)) atomic_store(&s->setup_busy, false);
        if (s->setup) {
            info_setup_update_t update;
            if (info_setup_take_update(s->setup, &update)) {
                info_config_t pending = s->config;
                strlcpy(pending.url, update.url, sizeof(pending.url));
                if (update.action == INFO_TOKEN_REPLACE) strlcpy(pending.token, update.token, sizeof(pending.token));
                if (update.action == INFO_TOKEN_CLEAR) memset(pending.token, 0, sizeof(pending.token));
                esp_err_t ret = info_setup_update_allowed(&update, s->config.url, *s->config.token != 0) ?
                    save_config(&pending) : ESP_ERR_INVALID_ARG;
                info_setup_finish_update(s->setup, ret == ESP_OK, pending.url, *pending.token != 0);
                lvgl_port_lock(0);
                if (!atomic_load(&s->closing)) {
                    if (ret == ESP_OK) {
                        s->config = pending; s->setup_saved = true; free(s->document); s->document = NULL;
                        editor_close(s); render(s); atomic_store(&s->refresh, true);
                    }
                    if (s->setup_status) lv_label_set_text(s->setup_status, ret == ESP_OK ? "已保存" : "保存失败");
                }
                lvgl_port_unlock(); memset(&update, 0, sizeof(update)); memset(&pending, 0, sizeof(pending));
            }
            if (atomic_load(&s->setup_stop) || info_setup_expired(s->setup)) {
                info_setup_stop(s->setup); s->setup = NULL;
                lvgl_port_lock(0); memset(&s->setup_details, 0, sizeof(s->setup_details)); setup_close_ui(s); lvgl_port_unlock();
                atomic_store(&s->setup_busy, false);
                set_status(s, s->setup_saved ? "配置已保存" : "网页设置已关闭");
                atomic_store(&s->refresh, true);
            }
        }
        if (atomic_exchange(&s->import_tf, false)) {
            info_config_t imported; load_config(&imported, false);
            lvgl_port_lock(0);
            if (s->editor && !atomic_load(&s->closing)) {
                lv_textarea_set_text(s->address, imported.url); lv_textarea_set_text(s->token, imported.token);
                lv_label_set_text(s->edit_status, "已读取");
            }
            lvgl_port_unlock();
        }
        if (atomic_exchange(&s->save, false)) {
            info_config_t pending;
            lvgl_port_lock(0); pending = s->pending_config; lvgl_port_unlock();
            esp_err_t ret = save_config(&pending);
            lvgl_port_lock(0);
            if (!atomic_load(&s->closing)) {
                if (ret == ESP_OK) {
                    s->config = pending; free(s->document); s->document = NULL;
                    editor_close(s); render(s); atomic_store(&s->refresh, true);
                } else if (s->edit_status) lv_label_set_text(s->edit_status, "保存失败");
            }
            lvgl_port_unlock();
        }
        if (!s->setup && (atomic_exchange(&s->refresh, false) || esp_timer_get_time() >= next)) {
            unsigned revision = atomic_load(&s->revision);
            set_status(s, "正在更新...");
            info_document_t *doc = heap_caps_calloc(1, sizeof(*doc), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            const char *error = doc ? fetch_info(s, doc, revision) : "内存不足";
            lvgl_port_lock(0);
            if (!atomic_load(&s->closing) && revision == atomic_load(&s->revision) && s->status) {
                if (!error) { free(s->document); s->document = doc; doc = NULL; render(s); }
                lv_label_set_text(s->status, error ? error : "已更新");
            }
            lvgl_port_unlock(); free(doc);
            next = esp_timer_get_time() + 300000000;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    if (s->setup) info_setup_stop(s->setup);
    lvgl_port_lock(0);
    memset(&s->setup_details, 0, sizeof(s->setup_details));
    if (s->page) lv_obj_del(s->page);
    s_info = NULL;
    if (s->on_close) s->on_close();
    lvgl_port_unlock();
    free(s->document); memset(&s->config, 0, sizeof(s->config)); memset(&s->pending_config, 0, sizeof(s->pending_config));
    ESP_LOGI("information", "closed, stack_free=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    free(s); vTaskDelete(NULL);
}

static void delete_cb(lv_event_t *e)
{
    info_page_t *s = lv_event_get_user_data(e); atomic_store(&s->closing, true);
    s->page = s->list = s->status = s->gear = s->editor = s->address = s->token = s->keyboard = s->edit_status = NULL;
    s->setup_page = s->setup_content = s->setup_toggle = s->setup_status = NULL;
}
static void close_cb(lv_event_t *e) { info_page_t *s = lv_event_get_user_data(e); atomic_store(&s->closing, true); lv_label_set_text(s->status, "关闭中..."); }
static void refresh_cb(lv_event_t *e) { info_page_t *s = lv_event_get_user_data(e); if (s->loaded) atomic_store(&s->refresh, true); }
static void tab_cb(lv_event_t *e) { info_page_t *s = lv_event_get_user_data(e); s->tab = lv_btnmatrix_get_selected_btn(s->tabs); if (s->tab < 3) render(s); }
static void edit_back(lv_event_t *e) { editor_close(lv_event_get_user_data(e)); }
static void edit_import(lv_event_t *e) { info_page_t *s = lv_event_get_user_data(e); atomic_store(&s->import_tf, true); }
static void focus_cb(lv_event_t *e) { info_page_t *s = lv_event_get_user_data(e); if (s->keyboard) lv_keyboard_set_textarea(s->keyboard, lv_event_get_target(e)); }
static void edit_save(lv_event_t *e)
{
    info_page_t *s = lv_event_get_user_data(e);
    if (!s->address || atomic_load(&s->closing)) return;
    const char *url = lv_textarea_get_text(s->address), *token = lv_textarea_get_text(s->token);
    if (!info_url_valid(url) || !info_token_valid(token)) { lv_label_set_text(s->edit_status, "配置无效"); return; }
    strlcpy(s->pending_config.url, url, sizeof(s->pending_config.url)); strlcpy(s->pending_config.token, token, sizeof(s->pending_config.token));
    atomic_fetch_add(&s->revision, 1); atomic_store(&s->save, true); lv_label_set_text(s->edit_status, "保存中");
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int width, bool symbol, lv_event_cb_t cb, info_page_t *s)
{
    lv_obj_t *btn = lv_btn_create(parent); lv_obj_set_pos(btn, x, 2); lv_obj_set_size(btn, width, 32);
    lv_obj_set_style_radius(btn, 4, 0); lv_obj_set_style_shadow_opa(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x256d58), 0); lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, s);
    lv_obj_t *t = lv_label_create(btn); lv_obj_set_style_text_font(t, symbol ? &lv_font_montserrat_20 : &font_alipuhui20, 0);
    lv_label_set_text(t, text); lv_obj_center(t); return btn;
}

static void setup_close_ui(info_page_t *s)
{
    lv_obj_t *page = s->setup_page;
    s->setup_page = s->setup_content = s->setup_toggle = s->setup_status = NULL;
    if (page) lv_obj_del(page);
}

static void setup_back(lv_event_t *e)
{
    info_page_t *s = lv_event_get_user_data(e);
    atomic_store(&s->setup_stop, true); atomic_store(&s->setup_request, 0); setup_close_ui(s);
}

static void setup_show_details(info_page_t *s)
{
    if (!s->setup_content) return;
    lv_obj_clean(s->setup_content); s->setup_status = NULL;
    info_setup_details_t *d = &s->setup_details;
    lv_obj_t *frame = lv_obj_create(s->setup_content);
    lv_obj_set_pos(frame, 0, 0); lv_obj_set_size(frame, 160, 160);
    lv_obj_set_style_pad_all(frame, 0, 0); lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_radius(frame, 0, 0); lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *qr = lv_qrcode_create(frame, 128, lv_color_black(), lv_color_white());
    lv_obj_center(qr);
    char payload[160];
    if (s->setup_wifi_qr) snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", d->ssid, d->password);
    else strlcpy(payload, d->url, sizeof(payload));
    lv_qrcode_update(qr, payload, strlen(payload)); memset(payload, 0, sizeof(payload));
    lv_obj_t *heading = label(s->setup_content, "配对码", 144); lv_obj_set_pos(heading, 170, 0);
    lv_obj_t *pin = label(s->setup_content, d->pin, 144); lv_obj_set_pos(pin, 170, 25);
    lv_obj_set_style_text_font(pin, &lv_font_montserrat_24, 0);
    char credentials[96];
    snprintf(credentials, sizeof(credentials), d->hotspot ? "%s\n%s" : "WLAN\n5 min", d->ssid, d->password);
    lv_obj_t *access = label(s->setup_content, credentials, 144); lv_obj_set_pos(access, 170, 63);
    lv_obj_set_style_text_font(access, &lv_font_montserrat_14, 0);
    memset(credentials, 0, sizeof(credentials));
    s->setup_status = label(s->setup_content, s->setup_saved ? "已保存" : "等待配对", 144); lv_obj_set_pos(s->setup_status, 170, 116);
    lv_obj_t *url = label(s->setup_content, d->url, 312); lv_obj_set_pos(url, 2, 170);
    lv_obj_set_style_text_font(url, &lv_font_montserrat_14, 0);
    if (d->hotspot) {
        lv_obj_clear_flag(s->setup_toggle, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(s->setup_toggle, 0), s->setup_wifi_qr ? "网页" : "热点");
    }
}

static void setup_toggle(lv_event_t *e)
{
    info_page_t *s = lv_event_get_user_data(e); s->setup_wifi_qr = !s->setup_wifi_qr; setup_show_details(s);
}

static void setup_choose(lv_event_t *e)
{
    info_page_t *s = lv_event_get_user_data(e);
    if (atomic_exchange(&s->setup_busy, true)) return;
    atomic_fetch_add(&s->revision, 1);
    lv_obj_t *target = lv_event_get_target(e);
    bool hotspot = (uintptr_t)lv_obj_get_user_data(target) == 2;
    atomic_store(&s->setup_stop, false); atomic_store(&s->setup_request, hotspot ? 2 : 1);
    lv_obj_add_state(target, LV_STATE_DISABLED);
    lv_label_set_text(s->setup_status, "启动中...");
}

static void setup_open(lv_event_t *e)
{
    info_page_t *s = lv_event_get_user_data(e);
    if (s->setup_page || atomic_load(&s->closing)) return;
    if (atomic_load(&s->save)) return;
    if (atomic_load(&s->setup_busy)) { if (s->edit_status) lv_label_set_text(s->edit_status, "关闭中"); return; }
    s->setup_page = band(s->page, 0, 240);
    button(s->setup_page, LV_SYMBOL_LEFT, 2, 38, true, setup_back, s);
    lv_obj_t *title = label(s->setup_page, "网页配置", 150); lv_obj_set_pos(title, 48, 8);
    s->setup_toggle = button(s->setup_page, "网页", 240, 76, false, setup_toggle, s);
    lv_obj_add_flag(s->setup_toggle, LV_OBJ_FLAG_HIDDEN);
    s->setup_content = band(s->setup_page, 42, 198);
    lv_obj_t *wlan = button(s->setup_content, "同一WLAN", 8, 144, false, setup_choose, s);
    lv_obj_t *ap = button(s->setup_content, "临时热点", 168, 144, false, setup_choose, s);
    lv_obj_set_user_data(wlan, (void *)(uintptr_t)1); lv_obj_set_user_data(ap, (void *)(uintptr_t)2);
    s->setup_status = label(s->setup_content, "选择连接方式", 304); lv_obj_set_pos(s->setup_status, 8, 60);
}

static void settings_cb(lv_event_t *e)
{
    info_page_t *s = lv_event_get_user_data(e);
    if (!s->loaded || s->editor || atomic_load(&s->closing)) return;
    s->editor = band(s->page, 0, 240);
    button(s->editor, LV_SYMBOL_LEFT, 2, 38, true, edit_back, s);
    s->edit_status = label(s->editor, "配置", 66); lv_obj_set_pos(s->edit_status, 46, 8);
    button(s->editor, LV_SYMBOL_WIFI, 116, 44, true, setup_open, s);
    button(s->editor, "TF", 168, 50, true, edit_import, s); button(s->editor, "保存", 228, 88, false, edit_save, s);
    s->address = lv_textarea_create(s->editor); s->token = lv_textarea_create(s->editor);
    lv_obj_t *fields[] = { s->address, s->token };
    for (int i = 0; i < 2; i++) {
        lv_obj_set_pos(fields[i], 4, 38 + 38 * i); lv_obj_set_size(fields[i], 312, 34);
        lv_obj_set_style_text_font(fields[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_all(fields[i], 6, 0); lv_textarea_set_one_line(fields[i], true);
        lv_textarea_set_max_length(fields[i], i ? INFO_TOKEN_SIZE - 1 : INFO_URL_SIZE - 1);
        lv_obj_add_event_cb(fields[i], focus_cb, LV_EVENT_FOCUSED, s);
        lv_obj_add_event_cb(fields[i], focus_cb, LV_EVENT_CLICKED, s);
    }
    lv_textarea_set_placeholder_text(s->address, "https://.../info"); lv_textarea_set_text(s->address, s->config.url);
    lv_textarea_set_placeholder_text(s->token, "Read token"); lv_textarea_set_password_mode(s->token, true); lv_textarea_set_text(s->token, s->config.token);
    s->keyboard = lv_keyboard_create(s->editor); lv_obj_set_align(s->keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s->keyboard, 0, 114); lv_obj_set_size(s->keyboard, 320, 126);
    lv_obj_set_style_text_font(s->keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_keyboard_set_textarea(s->keyboard, s->address);
    lv_obj_add_event_cb(s->keyboard, edit_save, LV_EVENT_READY, s); lv_obj_add_event_cb(s->keyboard, edit_back, LV_EVENT_CANCEL, s);
}

esp_err_t information_open(void (*on_close)(void))
{
    if (s_info) return ESP_ERR_INVALID_STATE;
    info_page_t *s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s) return ESP_ERR_NO_MEM;
    atomic_init(&s->closing, false); atomic_init(&s->refresh, true); atomic_init(&s->import_tf, false);
    atomic_init(&s->save, false); atomic_init(&s->revision, 1); s->on_close = on_close;
    atomic_init(&s->setup_stop, false); atomic_init(&s->setup_busy, false); atomic_init(&s->setup_request, 0);
    s->page = band(lv_scr_act(), 0, 240); lv_obj_add_event_cb(s->page, delete_cb, LV_EVENT_DELETE, s);
    button(s->page, LV_SYMBOL_LEFT, 2, 38, true, close_cb, s);
    lv_obj_t *title = label(s->page, "信息台", 150); lv_obj_set_pos(title, 48, 8);
    button(s->page, LV_SYMBOL_REFRESH, 232, 40, true, refresh_cb, s);
    s->gear = button(s->page, LV_SYMBOL_SETTINGS, 278, 40, true, settings_cb, s); lv_obj_add_state(s->gear, LV_STATE_DISABLED);
    static const char *tabs[] = { "天气", "RSS", "状态", "" };
    s->tabs = lv_btnmatrix_create(s->page); lv_obj_set_pos(s->tabs, 2, 38); lv_obj_set_size(s->tabs, 316, 34);
    lv_obj_set_style_text_font(s->tabs, &font_alipuhui20, LV_PART_ITEMS); lv_obj_set_style_pad_all(s->tabs, 0, 0);
    lv_btnmatrix_set_map(s->tabs, tabs); lv_btnmatrix_set_btn_ctrl_all(s->tabs, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(s->tabs, true); lv_btnmatrix_set_btn_ctrl(s->tabs, 0, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_add_event_cb(s->tabs, tab_cb, LV_EVENT_VALUE_CHANGED, s);
    s->list = band(s->page, 76, 136); lv_obj_add_flag(s->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s->list, LV_DIR_VER); lv_obj_set_style_pad_all(s->list, 8, 0);
    lv_obj_set_style_pad_row(s->list, 10, 0); lv_obj_set_flex_flow(s->list, LV_FLEX_FLOW_COLUMN);
    s->status = label(s->page, "读取配置...", 308); lv_obj_set_pos(s->status, 6, 216);
    lv_label_set_long_mode(s->status, LV_LABEL_LONG_DOT);
    s_info = s;
    if (xTaskCreatePinnedToCore(worker, "information", 8192, s, 3, NULL, 1) != pdPASS) {
        lv_obj_del(s->page); free(s); s_info = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
