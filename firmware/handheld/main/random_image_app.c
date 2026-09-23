#include "random_image_app.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp32_s3_szp.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"

#define LCD_W BSP_LCD_H_RES
#define LCD_H BSP_LCD_V_RES
#define IMAGE_TOP 40
#define IMAGE_H (LCD_H - IMAGE_TOP)

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define RANDOM_HTTP_READ_BUF_SIZE 4096

#define SD_APP_DIR SD_MOUNT_POINT "/szpi"
#define SD_CONFIG_DIR SD_APP_DIR "/config"
#define SD_CACHE_DIR SD_APP_DIR "/cache"
#define SD_RANDOM_CACHE_DIR SD_CACHE_DIR "/random"
#define SD_WIFI_CONFIG_PATH SD_CONFIG_DIR "/wifi.ini"
#define SD_RANDOM_LATEST_PATH SD_RANDOM_CACHE_DIR "/latest.jpg"

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    size_t max_len;
    int total_timeout_ms;
    int64_t start_us;
    int64_t last_progress_us;
    size_t last_progress_len;
    const char *progress_prefix;
} http_buffer_t;

static const char *TAG = "random_lvgl";

LV_FONT_DECLARE(font_alipuhui20);
extern int icon_flag;

static lv_obj_t *s_page;
static lv_obj_t *s_image;
static lv_obj_t *s_title_bar;
static lv_obj_t *s_status_label;
static lv_obj_t *s_spinner;
static lv_img_dsc_t s_image_dsc;
static uint8_t *s_image_buf;
static bool s_chrome_visible = true;
static bool s_status_loading;

static EventGroupHandle_t s_wifi_event_group;
static bool s_wifi_started;
static int s_retry_num;
static volatile bool s_fetching;
static bool s_button_task_started;

static char s_wifi_ssid[33] = CONFIG_RANDOM_IMAGE_WIFI_SSID;
static char s_wifi_password[65] = CONFIG_RANDOM_IMAGE_WIFI_PASSWORD;
static char s_api_url[256] = CONFIG_RANDOM_IMAGE_API_URL;

static void random_image_start_refresh(void);

static void log_random_heap(const char *stage)
{
    ESP_LOGI(TAG, "%s heap: dram_free=%u dram_largest=%u psram_free=%u psram_largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

static void trim_ascii(char *text)
{
    char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    size_t len = strlen(text);
    while (len > 0) {
        char c = text[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        text[--len] = '\0';
    }
}

static void mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir %s failed: errno=%d", path, errno);
    }
}

static bool sdcard_prepare(void)
{
    esp_err_t ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TF mount failed: %s", esp_err_to_name(ret));
        return false;
    }

    mkdir_if_missing(SD_APP_DIR);
    mkdir_if_missing(SD_CONFIG_DIR);
    mkdir_if_missing(SD_CACHE_DIR);
    mkdir_if_missing(SD_RANDOM_CACHE_DIR);
    return true;
}

static void write_wifi_template(void)
{
    FILE *existing = fopen(SD_WIFI_CONFIG_PATH, "r");
    if (existing != NULL) {
        fclose(existing);
        return;
    }

    FILE *f = fopen(SD_WIFI_CONFIG_PATH, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "cannot create %s", SD_WIFI_CONFIG_PATH);
        return;
    }

    fprintf(f,
            "# SZPI-S3 LVGL config\n"
            "# Edit on your computer, then reboot the board.\n"
            "ssid=%s\n"
            "password=%s\n"
            "api_url=%s\n",
            s_wifi_ssid, s_wifi_password, s_api_url);
    fclose(f);
}

static void load_sd_config(void)
{
    if (!sdcard_prepare()) {
        return;
    }

    FILE *f = fopen(SD_WIFI_CONFIG_PATH, "r");
    if (f == NULL) {
        write_wifi_template();
        return;
    }

    char line[384];
    while (fgets(line, sizeof(line), f) != NULL) {
        trim_ascii(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        trim_ascii(key);
        trim_ascii(value);

        if (strcasecmp(key, "ssid") == 0) {
            snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", value);
        } else if (strcasecmp(key, "password") == 0 || strcasecmp(key, "psk") == 0) {
            snprintf(s_wifi_password, sizeof(s_wifi_password), "%s", value);
        } else if (strcasecmp(key, "api_url") == 0 || strcasecmp(key, "random_image_url") == 0) {
            snprintf(s_api_url, sizeof(s_api_url), "%s", value);
        }
    }
    fclose(f);

    ESP_LOGI(TAG, "config ssid=%s api=%s", s_wifi_ssid, s_api_url);
}

static esp_err_t write_file_bytes(const char *path, const uint8_t *data, size_t len)
{
    if (!sdcard_prepare()) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }
    size_t written = fwrite(data, 1, len, f);
    int close_ret = fclose(f);
    return (written == len && close_ret == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t read_file_to_buffer(const char *path, size_t max_len, http_buffer_t *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }

    long file_len = ftell(f);
    rewind(f);
    if (file_len <= 0 || (size_t)file_len > max_len) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    out->data = heap_caps_malloc((size_t)file_len + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (out->data == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t got = fread(out->data, 1, (size_t)file_len, f);
    fclose(f);
    if (got != (size_t)file_len) {
        heap_caps_free(out->data);
        memset(out, 0, sizeof(*out));
        return ESP_FAIL;
    }

    out->len = got;
    out->cap = got;
    out->max_len = max_len;
    out->data[out->len] = '\0';
    return ESP_OK;
}

static void ui_apply_chrome_state(void)
{
    if (s_title_bar != NULL) {
        if (s_chrome_visible) {
            lv_obj_clear_flag(s_title_bar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_title_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_status_label != NULL) {
        if (s_chrome_visible) {
            lv_obj_clear_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_spinner != NULL) {
        if (s_status_loading) {
            lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_set_status_locked(const char *text, bool loading)
{
    lvgl_port_lock(0);
    s_status_loading = loading;
    if (s_page != NULL && s_status_label != NULL) {
        lv_label_set_text(s_status_label, text);
    }
    ui_apply_chrome_state();
    lvgl_port_unlock();
}

static void ui_set_status_unlocked(const char *text, bool loading)
{
    s_status_loading = loading;
    if (s_status_label != NULL) {
        lv_label_set_text(s_status_label, text);
    }
    ui_apply_chrome_state();
}

static void ui_show_image_locked(uint16_t *canvas)
{
    lvgl_port_lock(0);
    if (s_page != NULL && s_image != NULL) {
        uint8_t *old_buf = s_image_buf;
        lv_img_set_src(s_image, NULL);

        s_image_buf = (uint8_t *)canvas;
        s_image_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_image_dsc.header.always_zero = 0;
        s_image_dsc.header.reserved = 0;
        s_image_dsc.header.w = LCD_W;
        s_image_dsc.header.h = LCD_H;
        s_image_dsc.data_size = LCD_W * LCD_H * sizeof(uint16_t);
        s_image_dsc.data = s_image_buf;

        lv_img_set_src(s_image, &s_image_dsc);
        lv_obj_move_background(s_image);
        if (s_status_label != NULL) {
            lv_label_set_text(s_status_label, "Ready");
        }
        s_status_loading = false;
        ui_apply_chrome_state();

        if (old_buf != NULL) {
            heap_caps_free(old_buf);
        }
    } else {
        heap_caps_free(canvas);
    }
    lvgl_port_unlock();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
        } else if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t wifi_connect_sta(void)
{
    if (strlen(s_wifi_ssid) == 0) {
        ESP_LOGW(TAG, "WiFi SSID is empty");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_started) {
        EventBits_t current = xEventGroupGetBits(s_wifi_event_group);
        if (current & WIFI_CONNECTED_BIT) {
            return ESP_OK;
        }

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        s_retry_num = 0;
        esp_wifi_connect();
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(20000));
        return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL),
                        TAG, "register wifi event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL),
                        TAG, "register ip event failed");

    wifi_config_t wifi_config = { 0 };
    size_t ssid_len = strnlen(s_wifi_ssid, sizeof(wifi_config.sta.ssid));
    size_t password_len = strnlen(s_wifi_password, sizeof(wifi_config.sta.password));
    memcpy(wifi_config.sta.ssid, s_wifi_ssid, ssid_len);
    memcpy(wifi_config.sta.password, s_wifi_password, password_len);
    wifi_config.sta.threshold.authmode = strlen(s_wifi_password) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set wifi config failed");

    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        return ret;
    }
    s_wifi_started = true;

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

static esp_err_t append_http_data(http_buffer_t *buf, const uint8_t *data, size_t data_len)
{
    size_t new_len = buf->len + data_len;
    if (new_len > buf->max_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (new_len > buf->cap) {
        size_t new_cap = buf->cap == 0 ? 4096 : buf->cap;
        while (new_cap < new_len) {
            new_cap *= 2;
        }
        if (new_cap > buf->max_len) {
            new_cap = buf->max_len;
        }

        uint8_t *new_data = heap_caps_realloc(buf->data, new_cap + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (new_data == NULL) {
            return ESP_ERR_NO_MEM;
        }
        buf->data = new_data;
        buf->cap = new_cap;
    }

    memcpy(buf->data + buf->len, data, data_len);
    buf->len = new_len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}

static void update_download_progress(http_buffer_t *buf)
{
    int64_t now_us = esp_timer_get_time();
    if (buf->progress_prefix == NULL ||
        (now_us - buf->last_progress_us <= 1000000 && buf->len - buf->last_progress_len < 64 * 1024)) {
        return;
    }

    char text[32];
    snprintf(text, sizeof(text), "%s %u KB", buf->progress_prefix, (unsigned)(buf->len / 1024));
    ui_set_status_locked(text, true);
    buf->last_progress_us = now_us;
    buf->last_progress_len = buf->len;
}

static esp_err_t http_get_to_buffer(const char *url, size_t max_len, const char *progress_prefix,
                                    int total_timeout_ms, http_buffer_t *out)
{
    memset(out, 0, sizeof(*out));
    out->max_len = max_len;
    out->total_timeout_ms = total_timeout_ms;
    out->start_us = esp_timer_get_time();
    out->progress_prefix = progress_prefix;
    uint8_t *read_buf = NULL;

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = CONFIG_RANDOM_IMAGE_HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
        .max_redirection_count = 5,
        .addr_type = HTTP_ADDR_TYPE_INET,
    };

    log_random_heap("before http init");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Accept", "image/jpeg,*/*");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        return ret;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "GET status=%d content_length=%d", status, content_len);

    if (status < 200 || status >= 300) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (content_len > 0 && (size_t)content_len > max_len) {
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    read_buf = heap_caps_malloc(RANDOM_HTTP_READ_BUF_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (read_buf == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    while (true) {
        int64_t now_us = esp_timer_get_time();
        if (total_timeout_ms > 0 && now_us - out->start_us > (int64_t)total_timeout_ms * 1000) {
            ret = ESP_ERR_TIMEOUT;
            goto cleanup;
        }

        int read_len = esp_http_client_read(client, (char *)read_buf, RANDOM_HTTP_READ_BUF_SIZE);
        if (read_len < 0) {
            if (read_len == -ESP_ERR_HTTP_EAGAIN) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            ret = ESP_FAIL;
            goto cleanup;
        }
        if (read_len == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                ret = ESP_OK;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        ret = append_http_data(out, read_buf, (size_t)read_len);
        if (ret != ESP_OK) {
            goto cleanup;
        }
        update_download_progress(out);

        if (content_len > 0 && out->len >= (size_t)content_len) {
            ret = ESP_OK;
            break;
        }
    }

    if (out->len == 0) {
        ret = ESP_FAIL;
    }

cleanup:
    heap_caps_free(read_buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    log_random_heap("after http cleanup");
    if (ret != ESP_OK) {
        heap_caps_free(out->data);
        memset(out, 0, sizeof(*out));
    }
    return ret;
}

static esp_jpeg_image_scale_t choose_jpeg_scale(int width, int height)
{
    if (width / 8 >= LCD_W && height / 8 >= LCD_H) {
        return JPEG_IMAGE_SCALE_1_8;
    }
    if (width / 4 >= LCD_W && height / 4 >= LCD_H) {
        return JPEG_IMAGE_SCALE_1_4;
    }
    if (width / 2 >= LCD_W && height / 2 >= LCD_H) {
        return JPEG_IMAGE_SCALE_1_2;
    }
    return JPEG_IMAGE_SCALE_0;
}

static uint16_t *center_crop_canvas(const uint16_t *pixels, int width, int height)
{
    uint16_t *canvas = heap_caps_malloc(LCD_W * LCD_H * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (canvas == NULL) {
        return NULL;
    }

    memset(canvas, 0, LCD_W * LCD_H * sizeof(uint16_t));

    int src_x = width > LCD_W ? (width - LCD_W) / 2 : 0;
    int src_y = height > LCD_H ? (height - LCD_H) / 2 : 0;
    int copy_w = width > LCD_W ? LCD_W : width;
    int copy_h = height > LCD_H ? LCD_H : height;
    int dst_x = width < LCD_W ? (LCD_W - width) / 2 : 0;
    int dst_y = height < LCD_H ? (LCD_H - height) / 2 : 0;

    for (int y = 0; y < copy_h; y++) {
        const uint16_t *src = pixels + (src_y + y) * width + src_x;
        uint16_t *dst = canvas + (dst_y + y) * LCD_W + dst_x;
        memcpy(dst, src, copy_w * sizeof(uint16_t));
    }

    return canvas;
}

static esp_err_t decode_jpeg_to_lvgl(const http_buffer_t *jpeg)
{
    log_random_heap("before jpeg decode");
    esp_jpeg_image_cfg_t info_cfg = {
        .indata = jpeg->data,
        .indata_size = jpeg->len,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {
            .swap_color_bytes = 1,
        },
    };
    esp_jpeg_image_output_t original = { 0 };
    ESP_RETURN_ON_ERROR(esp_jpeg_get_image_info(&info_cfg, &original), TAG, "read jpeg info failed");

    info_cfg.out_scale = choose_jpeg_scale(original.width, original.height);

    esp_jpeg_image_output_t outimg = { 0 };
    ESP_RETURN_ON_ERROR(esp_jpeg_get_image_info(&info_cfg, &outimg), TAG, "read scaled jpeg info failed");

    uint16_t *decoded = heap_caps_malloc(outimg.output_len, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(decoded != NULL, ESP_ERR_NO_MEM, TAG, "no memory for decoded image");

    esp_jpeg_image_cfg_t jpeg_cfg = info_cfg;
    jpeg_cfg.outbuf = (uint8_t *)decoded;
    jpeg_cfg.outbuf_size = outimg.output_len;

    esp_err_t ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "decoded image %dx%d -> %dx%d, %u bytes",
                 original.width, original.height, outimg.width, outimg.height, (unsigned)outimg.output_len);
        uint16_t *canvas = center_crop_canvas(decoded, outimg.width, outimg.height);
        if (canvas == NULL) {
            ret = ESP_ERR_NO_MEM;
        } else {
            ui_show_image_locked(canvas);
        }
    }

    heap_caps_free(decoded);
    log_random_heap("after jpeg decode");
    return ret;
}

static esp_err_t show_cached_image(void)
{
    http_buffer_t cached = { 0 };
    esp_err_t ret = read_file_to_buffer(SD_RANDOM_LATEST_PATH, CONFIG_RANDOM_IMAGE_MAX_JPEG_KB * 1024, &cached);
    if (ret != ESP_OK) {
        return ret;
    }

    ui_set_status_locked("Decode cache", true);
    ret = decode_jpeg_to_lvgl(&cached);
    heap_caps_free(cached.data);
    return ret;
}

static esp_err_t fetch_and_show_random_image(void)
{
    esp_err_t ret = ESP_FAIL;

    for (int attempt = 1; attempt <= CONFIG_RANDOM_IMAGE_FETCH_RETRIES; attempt++) {
        http_buffer_t jpeg = { 0 };
        ui_set_status_locked(attempt == 1 ? "Download image" : "Retry image", true);

        ret = http_get_to_buffer(s_api_url, CONFIG_RANDOM_IMAGE_MAX_JPEG_KB * 1024, "Download",
                                 CONFIG_RANDOM_IMAGE_IMAGE_TIMEOUT_MS, &jpeg);
        if (ret == ESP_OK) {
            esp_err_t cache_ret = write_file_bytes(SD_RANDOM_LATEST_PATH, jpeg.data, jpeg.len);
            if (cache_ret != ESP_OK) {
                ESP_LOGW(TAG, "cache failed: %s", esp_err_to_name(cache_ret));
            }

            ui_set_status_locked("Decode image", true);
            ret = decode_jpeg_to_lvgl(&jpeg);
        }

        heap_caps_free(jpeg.data);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "attempt %d/%d failed: %s", attempt, CONFIG_RANDOM_IMAGE_FETCH_RETRIES, esp_err_to_name(ret));
    }

    ui_set_status_locked("Try cache", true);
    esp_err_t cached_ret = show_cached_image();
    return cached_ret == ESP_OK ? ESP_OK : ret;
}

static void random_fetch_task(void *arg)
{
    load_sd_config();

    ui_set_status_locked("Connect WiFi", true);
    esp_err_t ret = wifi_connect_sta();
    if (ret == ESP_OK) {
        ret = fetch_and_show_random_image();
    } else {
        ESP_LOGW(TAG, "wifi connect failed: %s", esp_err_to_name(ret));
        ui_set_status_locked("WiFi failed, try cache", true);
        if (show_cached_image() == ESP_OK) {
            ret = ESP_OK;
        }
    }

    if (ret != ESP_OK) {
        ui_set_status_locked("Refresh failed", false);
    }

    s_fetching = false;
    vTaskDelete(NULL);
}

static void random_image_start_refresh(void)
{
    if (s_fetching) {
        return;
    }

    s_fetching = true;
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(random_fetch_task, "random_image", 12288, NULL, 4,
                                                    NULL, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ok = xTaskCreatePinnedToCore(random_fetch_task, "random_image", 12288, NULL, 4, NULL, 1);
    }
    if (ok != pdPASS) {
        s_fetching = false;
        ESP_LOGE(TAG, "create random_image task failed");
    }
}

static void random_button_task(void *arg)
{
    gpio_num_t key_gpio = CONFIG_RANDOM_IMAGE_REFRESH_GPIO;
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << key_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    int last_level = 1;
    int64_t press_start_us = 0;

    while (true) {
        int level = gpio_get_level(key_gpio);
        int64_t now_us = esp_timer_get_time();
        if (last_level == 1 && level == 0) {
            press_start_us = now_us;
        } else if (last_level == 0 && level == 1 && press_start_us > 0) {
            if (now_us - press_start_us > 30000 && s_page != NULL) {
                random_image_start_refresh();
            }
            press_start_us = 0;
        }
        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void ensure_button_task(void)
{
    if (s_button_task_started) {
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(random_button_task, "random_key", 4096, NULL, 5,
                                                    NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ok = xTaskCreatePinnedToCore(random_button_task, "random_key", 4096, NULL, 5, NULL, 0);
    }
    if (ok == pdPASS) {
        s_button_task_started = true;
    }
}

static void random_back_cb(lv_event_t *e)
{
    if (s_page != NULL) {
        lv_obj_del(s_page);
        s_page = NULL;
        s_image = NULL;
        s_title_bar = NULL;
        s_status_label = NULL;
        s_spinner = NULL;
    }
    icon_flag = 0;
}

static void random_refresh_cb(lv_event_t *e)
{
    ui_set_status_unlocked("Refreshing", true);
    random_image_start_refresh();
}

static void random_toggle_chrome_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    s_chrome_visible = !s_chrome_visible;
    ui_apply_chrome_state();
}

static void random_page_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT) {
        random_back_cb(e);
    } else if (dir == LV_DIR_LEFT || dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM) {
        ui_set_status_unlocked("Refreshing", true);
        random_image_start_refresh();
    }
}

void random_image_app_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_page != NULL) {
        lv_obj_del(s_page);
    }
    icon_flag = 7;
    s_chrome_visible = true;
    s_status_loading = false;

    s_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_page, LCD_W, LCD_H);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_radius(s_page, 0, 0);
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x101820), 0);
    lv_obj_add_event_cb(s_page, random_page_gesture_cb, LV_EVENT_GESTURE, NULL);

    s_image = lv_img_create(s_page);
    lv_obj_set_size(s_image, LCD_W, LCD_H);
    lv_obj_align(s_image, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_image, random_toggle_chrome_cb, LV_EVENT_CLICKED, NULL);
    if (s_image_buf != NULL) {
        lv_img_set_src(s_image, &s_image_dsc);
        lv_obj_move_background(s_image);
    }

    s_title_bar = lv_obj_create(s_page);
    lv_obj_set_size(s_title_bar, LCD_W, IMAGE_TOP);
    lv_obj_align(s_title_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_pad_all(s_title_bar, 0, 0);
    lv_obj_set_style_radius(s_title_bar, 0, 0);
    lv_obj_set_style_border_width(s_title_bar, 0, 0);
    lv_obj_set_style_bg_color(s_title_bar, lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_opa(s_title_bar, LV_OPA_80, 0);

    lv_obj_t *back = lv_btn_create(s_title_bar);
    lv_obj_set_size(back, 52, 34);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back, random_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(back_label);

    lv_obj_t *title_label = lv_label_create(s_title_bar);
    lv_label_set_text(title_label, "Random Image");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *refresh = lv_btn_create(s_title_bar);
    lv_obj_set_size(refresh, 52, 34);
    lv_obj_align(refresh, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_opa(refresh, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(refresh, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(refresh, random_refresh_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(refresh_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(refresh_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(refresh_label);

    s_status_label = lv_label_create(s_page);
    lv_label_set_text(s_status_label, "Ready");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(s_status_label, lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_opa(s_status_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_left(s_status_label, 8, 0);
    lv_obj_set_style_pad_right(s_status_label, 8, 0);
    lv_obj_set_style_pad_top(s_status_label, 3, 0);
    lv_obj_set_style_pad_bottom(s_status_label, 3, 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    s_spinner = lv_spinner_create(s_page, 1000, 60);
    lv_obj_set_size(s_spinner, 44, 44);
    lv_obj_center(s_spinner);
    ui_apply_chrome_state();

    ensure_button_task();
    ui_set_status_unlocked("Refreshing", true);
    random_image_start_refresh();
}
