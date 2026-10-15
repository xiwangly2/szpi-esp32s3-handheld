#include "information_setup.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETUP_PORT 8080
#define SETUP_DURATION_US (300LL * 1000000)
extern const unsigned char setup_html_start[] asm("_binary_info_setup_html_start");
extern const unsigned char setup_html_end[] asm("_binary_info_setup_html_end");
extern const unsigned char setup_js_start[] asm("_binary_info_setup_js_start");
extern const unsigned char setup_js_end[] asm("_binary_info_setup_js_end");
extern const unsigned char setup_css_start[] asm("_binary_info_setup_css_start");
extern const unsigned char setup_css_end[] asm("_binary_info_setup_css_end");

struct info_setup {
    httpd_handle_t server;
    esp_netif_t *ap;
    SemaphoreHandle_t lock;
    wifi_mode_t original_mode;
    bool mode_changed, stopping, token_set, pending, busy, saved, failed;
    unsigned attempts;
    int64_t deadline, saved_at;
    uint32_t bind_ip;
    char authority[64], pin[INFO_SETUP_PIN_SIZE], session[INFO_SETUP_SESSION_SIZE], current_url[INFO_URL_SIZE];
    info_setup_update_t update;
};

static bool active(info_setup_t *s)
{
    return !s->stopping && s->attempts < INFO_SETUP_MAX_ATTEMPTS && esp_timer_get_time() < s->deadline;
}

static void headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}

static esp_err_t json_response(httpd_req_t *req, const char *status, const char *json)
{
    headers(req); httpd_resp_set_status(req, status); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static bool valid_origin(httpd_req_t *req, info_setup_t *s)
{
    char host[64], origin[80] = "";
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    if (httpd_req_get_hdr_value_len(req, "Origin") && httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) return false;
    return info_setup_origin_valid(host, origin, s->authority);
}

static bool authorized(httpd_req_t *req, info_setup_t *s)
{
    char session[INFO_SETUP_SESSION_SIZE];
    if (!valid_origin(req, s) || httpd_req_get_hdr_value_str(req, "X-Setup-Token", session, sizeof(session)) != ESP_OK) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ok = active(s) && info_setup_secret_equal(s->session, session);
    xSemaphoreGive(s->lock); memset(session, 0, sizeof(session)); return ok;
}

static bool read_body(httpd_req_t *req, char *body)
{
    char type[48];
    if (!req->content_len || req->content_len > INFO_SETUP_BODY_LIMIT ||
        httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8"))) return false;
    size_t used = 0; int64_t deadline = esp_timer_get_time() + 5000000;
    while (used < req->content_len) {
        if (esp_timer_get_time() >= deadline) return false;
        int got = httpd_req_recv(req, body + used, req->content_len - used);
        if (got <= 0) return false;
        used += got;
    }
    body[used] = 0; return true;
}

static esp_err_t asset_handler(httpd_req_t *req)
{
    info_setup_t *s = req->user_ctx;
    if (!valid_origin(req, s)) return json_response(req, "403 Forbidden", "{\"error\":\"Invalid origin\"}");
    headers(req);
    if (!strcmp(req->uri, "/setup.js")) {
        httpd_resp_set_type(req, "text/javascript; charset=utf-8");
        return httpd_resp_send(req, (const char *)setup_js_start, setup_js_end - setup_js_start - 1);
    }
    if (!strcmp(req->uri, "/setup.css")) {
        httpd_resp_set_type(req, "text/css; charset=utf-8");
        return httpd_resp_send(req, (const char *)setup_css_start, setup_css_end - setup_css_start - 1);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)setup_html_start, setup_html_end - setup_html_start - 1);
}

static esp_err_t pair_handler(httpd_req_t *req)
{
    info_setup_t *s = req->user_ctx;
    if (!valid_origin(req, s)) return json_response(req, "403 Forbidden", "{\"error\":\"Invalid origin\"}");
    char body[INFO_SETUP_BODY_LIMIT + 1], pin[INFO_SETUP_PIN_SIZE];
    if (!read_body(req, body)) return json_response(req, "400 Bad Request", "{\"error\":\"Invalid request\"}");
    bool valid = info_setup_parse_pin(body, req->content_len, pin);
    memset(body, 0, sizeof(body));
    char result[80];
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool allowed = active(s);
    bool ok = allowed && valid && info_setup_secret_equal(s->pin, pin);
    if (allowed && !ok) s->attempts++;
    if (ok) snprintf(result, sizeof(result), "{\"session\":\"%s\"}", s->session);
    xSemaphoreGive(s->lock); memset(pin, 0, sizeof(pin));
    if (!ok) return json_response(req, "403 Forbidden", "{\"error\":\"Pairing failed or expired\"}");
    esp_err_t ret = json_response(req, "200 OK", result); memset(result, 0, sizeof(result)); return ret;
}

static esp_err_t state_handler(httpd_req_t *req)
{
    info_setup_t *s = req->user_ctx;
    if (!authorized(req, s)) return json_response(req, "403 Forbidden", "{\"error\":\"Session expired\"}");
    cJSON *json = cJSON_CreateObject();
    if (!json) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    cJSON_AddStringToObject(json, "url", s->current_url);
    cJSON_AddBoolToObject(json, "tokenSet", s->token_set);
    cJSON_AddStringToObject(json, "state", s->saved ? "saved" : s->failed ? "failed" : s->busy ? "saving" : "ready");
    xSemaphoreGive(s->lock);
    char *body = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if (!body) return ESP_ERR_NO_MEM;
    esp_err_t ret = json_response(req, "200 OK", body); free(body); return ret;
}

static esp_err_t save_handler(httpd_req_t *req)
{
    info_setup_t *s = req->user_ctx;
    if (!authorized(req, s)) return json_response(req, "403 Forbidden", "{\"error\":\"Session expired\"}");
    char body[INFO_SETUP_BODY_LIMIT + 1]; info_setup_update_t update;
    if (!read_body(req, body) || !info_setup_parse_update(body, req->content_len, &update)) {
        memset(body, 0, sizeof(body)); return json_response(req, "400 Bad Request", "{\"error\":\"Invalid configuration\"}");
    }
    memset(body, 0, sizeof(body));
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ok = active(s) && !s->busy && !s->saved;
    if (ok) { s->update = update; s->pending = s->busy = true; s->failed = false; }
    xSemaphoreGive(s->lock); memset(&update, 0, sizeof(update));
    return json_response(req, ok ? "202 Accepted" : "409 Conflict", ok ? "{\"state\":\"saving\"}" : "{\"error\":\"Busy or expired\"}");
}

static esp_err_t open_socket(httpd_handle_t server, int fd)
{
    info_setup_t *s = httpd_get_global_user_ctx(server);
    struct sockaddr_in local; socklen_t size = sizeof(local);
    if (getsockname(fd, (struct sockaddr *)&local, &size) || local.sin_family != AF_INET || local.sin_addr.s_addr != s->bind_ip) return ESP_FAIL;
    return ESP_OK;
}

static void retain_context(void *context) { (void)context; }

static void random_hex(char *out, unsigned bytes)
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < bytes; i++) { uint8_t value; esp_fill_random(&value, 1); out[2 * i] = digits[value >> 4]; out[2 * i + 1] = digits[value & 15]; }
    out[bytes * 2] = 0;
}

esp_err_t info_setup_start(bool hotspot, const char *current_url, bool token_set,
                           info_setup_t **out, info_setup_details_t *details)
{
    *out = NULL; memset(details, 0, sizeof(*details));
    info_setup_t *s = calloc(1, sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;
    s->lock = xSemaphoreCreateMutex();
    if (!s->lock) { free(s); return ESP_ERR_NO_MEM; }
    esp_err_t ret = esp_wifi_get_mode(&s->original_mode);
    if (ret != ESP_OK || s->original_mode != WIFI_MODE_STA) { ret = ESP_ERR_INVALID_STATE; goto fail; }
    esp_netif_ip_info_t ip = {0}, sta_ip = {0};
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) esp_netif_get_ip_info(sta, &sta_ip);
    details->hotspot = hotspot;
    if (hotspot) {
        if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")) { ret = ESP_ERR_INVALID_STATE; goto fail; }
        s->ap = esp_netif_create_default_wifi_ap();
        if (!s->ap) { ret = ESP_ERR_NO_MEM; goto fail; }
        IP4_ADDR(&ip.ip, 172, 30, 42, 1); IP4_ADDR(&ip.netmask, 255, 255, 255, 0); ip.gw = ip.ip;
        if (sta_ip.ip.addr && (sta_ip.ip.addr & sta_ip.netmask.addr) == (ip.ip.addr & sta_ip.netmask.addr)) {
            IP4_ADDR(&ip.ip, 192, 168, 73, 1); ip.gw = ip.ip;
            if ((sta_ip.ip.addr & sta_ip.netmask.addr) == (ip.ip.addr & sta_ip.netmask.addr)) { ret = ESP_ERR_INVALID_STATE; goto fail; }
        }
        esp_netif_dhcps_stop(s->ap);
        ret = esp_netif_set_ip_info(s->ap, &ip); if (ret != ESP_OK) goto fail;
        esp_netif_dhcps_start(s->ap);
        uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(details->ssid, sizeof(details->ssid), "SZPI-Setup-%02X%02X", mac[4], mac[5]);
        random_hex(details->password, 8);
        wifi_config_t ap = {0};
        strcpy((char *)ap.ap.ssid, details->ssid); strcpy((char *)ap.ap.password, details->password);
        ap.ap.ssid_len = strlen(details->ssid); ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        ap.ap.channel = 1; ap.ap.max_connection = 2; ap.ap.pmf_cfg.capable = true;
        ret = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (ret != ESP_OK) goto fail;
        ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (ret == ESP_OK) { s->mode_changed = true; ret = esp_wifi_set_config(WIFI_IF_AP, &ap); }
        memset(&ap, 0, sizeof(ap));
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        if (ret != ESP_OK) goto fail;
    } else {
        if (!sta_ip.ip.addr) { ret = ESP_ERR_INVALID_STATE; goto fail; }
        ip = sta_ip;
    }
    s->bind_ip = ip.ip.addr;
    snprintf(s->authority, sizeof(s->authority), IPSTR ":%d", IP2STR(&ip.ip), SETUP_PORT);
    snprintf(details->url, sizeof(details->url), "http://%s", s->authority);
    snprintf(s->pin, sizeof(s->pin), "%06u", (unsigned)(esp_random() % 1000000));
    strcpy(details->pin, s->pin); random_hex(s->session, 16);
    strlcpy(s->current_url, current_url, sizeof(s->current_url)); s->token_set = token_set;
    s->deadline = esp_timer_get_time() + SETUP_DURATION_US;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = SETUP_PORT; config.stack_size = 7168;
    config.max_open_sockets = 3; config.max_uri_handlers = 6; config.lru_purge_enable = true;
    config.recv_wait_timeout = 2; config.send_wait_timeout = 2;
    config.global_user_ctx = s; config.global_user_ctx_free_fn = retain_context; config.open_fn = open_socket;
    ret = httpd_start(&s->server, &config); if (ret != ESP_OK) goto fail;
    const httpd_uri_t handlers[] = {
        { .uri = "/", .method = HTTP_GET, .handler = asset_handler, .user_ctx = s },
        { .uri = "/setup.js", .method = HTTP_GET, .handler = asset_handler, .user_ctx = s },
        { .uri = "/setup.css", .method = HTTP_GET, .handler = asset_handler, .user_ctx = s },
        { .uri = "/api/pair", .method = HTTP_POST, .handler = pair_handler, .user_ctx = s },
        { .uri = "/api/config", .method = HTTP_GET, .handler = state_handler, .user_ctx = s },
        { .uri = "/api/config", .method = HTTP_POST, .handler = save_handler, .user_ctx = s },
    };
    for (unsigned i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        ret = httpd_register_uri_handler(s->server, &handlers[i]); if (ret != ESP_OK) goto fail;
    }
    *out = s;
    printf("information_setup: %s at %s (5 minute window)\n", hotspot ? "AP+STA" : "WLAN", details->url);
    return ESP_OK;
fail:
    info_setup_stop(s); memset(details, 0, sizeof(*details)); return ret;
}

bool info_setup_take_update(info_setup_t *s, info_setup_update_t *out)
{
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool ready = active(s) && s->pending;
    if (ready) { *out = s->update; memset(&s->update, 0, sizeof(s->update)); s->pending = false; }
    xSemaphoreGive(s->lock); return ready;
}

void info_setup_finish_update(info_setup_t *s, bool saved, const char *url, bool token_set)
{
    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->busy = false; s->saved = saved; s->failed = !saved; s->saved_at = esp_timer_get_time();
    if (saved) { strlcpy(s->current_url, url, sizeof(s->current_url)); s->token_set = token_set; }
    xSemaphoreGive(s->lock);
}

bool info_setup_expired(info_setup_t *s)
{
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool done = !active(s) || (s->saved && esp_timer_get_time() - s->saved_at > 5000000);
    xSemaphoreGive(s->lock); return done;
}

void info_setup_stop(info_setup_t *s)
{
    if (!s) return;
    xSemaphoreTake(s->lock, portMAX_DELAY); s->stopping = true; xSemaphoreGive(s->lock);
    if (s->server) {
        // global_user_ctx is ours; httpd must not free it before worker cleanup.
        httpd_stop(s->server);
    }
    if (s->mode_changed) {
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_err_t ret = esp_wifi_set_mode(s->original_mode);
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        if (ret != ESP_OK) printf("information_setup: restore mode failed: %s\n", esp_err_to_name(ret));
    }
    if (s->ap) esp_netif_destroy_default_wifi(s->ap);
    vSemaphoreDelete(s->lock); memset(s, 0, sizeof(*s)); free(s);
}
