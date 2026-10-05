#include "wifi_qr_scanner.h"

#include "esp32_s3_szp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"
#include "quirc.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define SCAN_WIDTH 320
#define SCAN_HEIGHT 240
#define SCAN_FRAME_BYTES (SCAN_WIDTH * SCAN_HEIGHT * 2)
#define SCAN_TIMEOUT_US (60LL * 1000 * 1000)

LV_FONT_DECLARE(font_alipuhui20);
static const char *TAG = "wifi_qr";

typedef struct {
    lv_obj_t *page;
    lv_obj_t *image;
    lv_obj_t *status;
    lv_img_dsc_t descriptor;
    uint8_t *display;
    atomic_bool cancelled;
    uint32_t generation;
    wifi_qr_scan_cb_t on_finish;
} wifi_qr_scan_t;

static wifi_qr_scan_t *s_scan;

bool wifi_qr_scanner_is_running(void)
{
    lvgl_port_lock(0);
    bool running = s_scan != NULL;
    lvgl_port_unlock();
    return running;
}

static void scan_status(wifi_qr_scan_t *scan, const char *message)
{
    lvgl_port_lock(0);
    if (!atomic_load(&scan->cancelled) && scan->status != NULL) {
        lv_label_set_text(scan->status, message);
    }
    lvgl_port_unlock();
}

static void scan_back_cb(lv_event_t *event)
{
    wifi_qr_scan_t *scan = lv_event_get_user_data(event);
    atomic_store(&scan->cancelled, true);
    if (scan->status != NULL) {
        lv_label_set_text(scan->status, "关闭中...");
    }
}

static void scan_delete_cb(lv_event_t *event)
{
    wifi_qr_scan_t *scan = lv_event_get_user_data(event);
    atomic_store(&scan->cancelled, true);
    scan->page = NULL;
    scan->image = NULL;
    scan->status = NULL;
}

static const char *parse_status(wifi_qr_parse_result_t result)
{
    switch (result) {
    case WIFI_QR_NOT_WIFI: return "不是WLAN二维码";
    case WIFI_QR_UNSUPPORTED_AUTH: return "不支持此认证方式";
    case WIFI_QR_TOO_LONG: return "网络名或密码过长";
    default: return "WLAN二维码内容无效";
    }
}

static void scan_task(void *arg)
{
    wifi_qr_scan_t *scan = arg;
    struct quirc *decoder = NULL;
    struct quirc_code *code = NULL;
    struct quirc_data *data = NULL;
    wifi_qr_credentials_t credentials = {0};
    bool camera_started = false;
    bool found = false;
    const char *error = NULL;

    scan->display = heap_caps_malloc(SCAN_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    code = heap_caps_calloc(1, sizeof(*code), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    data = heap_caps_calloc(1, sizeof(*data), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    decoder = quirc_new();
    if (scan->display == NULL || code == NULL || data == NULL || decoder == NULL ||
        quirc_resize(decoder, SCAN_WIDTH, SCAN_HEIGHT) != 0) {
        error = "扫码内存不足";
        goto cleanup;
    }
    if (atomic_load(&scan->cancelled)) {
        goto cleanup;
    }
    esp_err_t ret = bsp_camera_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "camera init failed: %s", esp_err_to_name(ret));
        error = "摄像头启动失败";
        goto cleanup;
    }
    camera_started = true;
    scan_status(scan, "正在识别...");
    int64_t deadline = esp_timer_get_time() + SCAN_TIMEOUT_US;
    int64_t status_until = 0;
    ESP_LOGI(TAG, "scanner started");

    while (!atomic_load(&scan->cancelled) && esp_timer_get_time() < deadline) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame == NULL) {
            scan_status(scan, "等待摄像头画面...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (frame->format != PIXFORMAT_RGB565 || frame->width != SCAN_WIDTH ||
            frame->height != SCAN_HEIGHT || frame->len < SCAN_FRAME_BYTES) {
            esp_camera_fb_return(frame);
            error = "摄像头画面格式异常";
            break;
        }
        uint8_t *gray = quirc_begin(decoder, NULL, NULL);
        for (size_t i = 0; i < SCAN_WIDTH * SCAN_HEIGHT; i++) {
            uint16_t pixel = ((uint16_t)frame->buf[i * 2] << 8) | frame->buf[i * 2 + 1];
            unsigned r = ((pixel >> 11) & 31) * 255 / 31;
            unsigned g = ((pixel >> 5) & 63) * 255 / 63;
            unsigned b = (pixel & 31) * 255 / 31;
            gray[i] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
        }
        lvgl_port_lock(0);
        if (!atomic_load(&scan->cancelled) && scan->image != NULL) {
            memcpy(scan->display, frame->buf, SCAN_FRAME_BYTES);
            scan->descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
            scan->descriptor.header.w = SCAN_WIDTH;
            scan->descriptor.header.h = SCAN_HEIGHT;
            scan->descriptor.data_size = SCAN_FRAME_BYTES;
            scan->descriptor.data = scan->display;
            lv_img_set_src(scan->image, &scan->descriptor);
            lv_obj_invalidate(scan->image);
        }
        lvgl_port_unlock();
        esp_camera_fb_return(frame);
        if (atomic_load(&scan->cancelled)) {
            break;
        }

        quirc_end(decoder);
        int count = quirc_count(decoder);
        // Decode a bounded number per frame to keep cancellation responsive.
        for (int i = 0; i < count && i < 4 && !atomic_load(&scan->cancelled); i++) {
            quirc_extract(decoder, i, code);
            quirc_decode_error_t decode_result = quirc_decode(code, data);
            if (decode_result != QUIRC_SUCCESS) {
                quirc_flip(code);
                decode_result = quirc_decode(code, data);
            }
            if (decode_result != QUIRC_SUCCESS) {
                continue;
            }
            wifi_qr_parse_result_t parsed = wifi_qr_parse(data->payload, (size_t)data->payload_len,
                                                        &credentials);
            mbedtls_platform_zeroize(data, sizeof(*data));
            if (parsed == WIFI_QR_OK) {
                found = true;
                break;
            }
            scan_status(scan, parse_status(parsed));
            status_until = esp_timer_get_time() + 2000000;
        }
        if (found) {
            scan_status(scan, "已识别，正在连接...");
            break;
        }
        if (esp_timer_get_time() > status_until) {
            scan_status(scan, "正在识别...");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!found && error == NULL) {
        error = "扫码超时，请重试";
    }

cleanup:
    if (camera_started) {
        esp_camera_deinit();
        dvp_pwdn(1);
    }
    if (decoder != NULL) {
        quirc_destroy(decoder);
    }
    if (data != NULL) {
        mbedtls_platform_zeroize(data, sizeof(*data));
    }
    free(code);
    free(data);
    lvgl_port_lock(0);
    bool cancelled = atomic_load(&scan->cancelled);
    if (scan->page != NULL) {
        lv_obj_del(scan->page);
    }
    lv_img_cache_invalidate_src(&scan->descriptor);
    free(scan->display);
    s_scan = NULL;
    if (!cancelled && scan->on_finish != NULL) {
        scan->on_finish(found ? &credentials : NULL, scan->generation, error);
    }
    lvgl_port_unlock();
    mbedtls_platform_zeroize(&credentials, sizeof(credentials));
    ESP_LOGI(TAG, "scanner stopped: found=%d cancelled=%d stack_free=%u", found, cancelled,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    free(scan);
    vTaskDelete(NULL);
}

esp_err_t wifi_qr_scanner_start(lv_obj_t *parent, uint32_t generation,
                                 wifi_qr_scan_cb_t on_finish)
{
    lvgl_port_lock(0);
    if (s_scan != NULL || parent == NULL || on_finish == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    wifi_qr_scan_t *scan = calloc(1, sizeof(*scan));
    if (scan == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    atomic_init(&scan->cancelled, false);
    scan->generation = generation;
    scan->on_finish = on_finish;
    scan->page = lv_obj_create(parent);
    lv_obj_set_size(scan->page, SCAN_WIDTH, SCAN_HEIGHT);
    lv_obj_set_pos(scan->page, 0, 0);
    lv_obj_set_style_pad_all(scan->page, 0, 0);
    lv_obj_set_style_border_width(scan->page, 0, 0);
    lv_obj_set_style_radius(scan->page, 0, 0);
    lv_obj_set_style_bg_color(scan->page, lv_color_black(), 0);
    lv_obj_clear_flag(scan->page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scan->page, scan_delete_cb, LV_EVENT_DELETE, scan);
    scan->image = lv_img_create(scan->page);
    lv_obj_set_pos(scan->image, 0, 0);
    lv_obj_t *guide = lv_obj_create(scan->page);
    lv_obj_set_size(guide, 160, 160);
    lv_obj_set_pos(guide, 80, 44);
    lv_obj_set_style_bg_opa(guide, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(guide, lv_color_white(), 0);
    lv_obj_set_style_border_width(guide, 2, 0);
    lv_obj_set_style_radius(guide, 0, 0);
    lv_obj_clear_flag(guide, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bar = lv_obj_create(scan->page);
    lv_obj_set_size(bar, 320, 38);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x176b78), 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(bar);
    lv_obj_set_style_text_font(title, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "扫码连接");
    lv_obj_center(title);
    lv_obj_t *back = lv_btn_create(bar);
    lv_obj_set_size(back, 52, 34);
    lv_obj_set_pos(back, 2, 2);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back, scan_back_cb, LV_EVENT_CLICKED, scan);
    lv_obj_t *back_label = lv_label_create(back);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_20, 0);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    scan->status = lv_label_create(scan->page);
    lv_obj_set_width(scan->status, 304);
    lv_obj_set_pos(scan->status, 8, 210);
    lv_obj_set_style_text_font(scan->status, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(scan->status, lv_color_white(), 0);
    lv_obj_set_style_text_align(scan->status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(scan->status, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scan->status, LV_OPA_70, 0);
    lv_label_set_long_mode(scan->status, LV_LABEL_LONG_DOT);
    lv_label_set_text(scan->status, "摄像头启动中...");
    s_scan = scan;
    // quirc_decode has an internal 8896-byte datastream; keep it off the LVGL task.
    if (xTaskCreatePinnedToCore(scan_task, "wifi_qr", 16 * 1024, scan, 4, NULL, 1) != pdPASS) {
        lv_obj_del(scan->page);
        s_scan = NULL;
        free(scan);
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    lvgl_port_unlock();
    return ESP_OK;
}
