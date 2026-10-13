#include "video_player.h"
#include "video_clock.h"
#include "esp32_s3_szp.h"
#include "esp_avi_extractor.h"
#include "esp_extractor_ctrl.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "jpeg_decoder.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define VIDEO_DEFAULT "/sdcard/szpi/videos/BadApple.avi"
#define VIDEO_CACHE "/sdcard/szpi/cache/video/online.avi"
#define VIDEO_PART VIDEO_CACHE ".part"
#define VIDEO_OLD VIDEO_CACHE ".old"
#define VIDEO_MAX_BYTES (256U * 1024 * 1024)
#define VIDEO_MAX_FRAME (128U * 1024)
#define VIDEO_PIXELS (320 * 240)

LV_FONT_DECLARE(font_alipuhui20);
static const char *TAG = "handheld_video";
enum { COMMAND_NONE, COMMAND_PLAY, COMMAND_DOWNLOAD };
typedef struct video_session video_session_t;
typedef struct {
    FILE *file;
    uint32_t size;
    esp_extractor_handle_t extractor;
    video_session_t *session;
    int64_t parse_deadline;
} video_source_t;

struct video_session {
    lv_obj_t *page, *image, *top, *bottom, *status, *time, *progress, *play_label, *mute_label;
    lv_img_dsc_t image_desc;
    lv_timer_t *timer;
    uint16_t *pixels, *display;
    char path[512];
    char url[512];
    uint8_t *volume;
    void (*on_close)(void);
    atomic_bool closing, audio_done, audio_error, stop_media;
    atomic_int command;
    video_clock_t clock;
    bool playing, fullscreen, muted, has_audio;
    bool select_default;
    uint32_t duration;
    uint32_t shown, dropped;
    video_source_t audio;
    esp_extractor_audio_stream_info_t audio_info;
};

static video_session_t *s_video;

static bool interrupted(video_session_t *s)
{
    return atomic_load(&s->closing) || atomic_load(&s->stop_media) ||
           atomic_load(&s->command) != COMMAND_NONE;
}

// Called with the LVGL lock held, like the playback controls it reflects.
static void update_audio_output(video_session_t *s)
{
    bool enabled = s->playing && s->has_audio && !s->muted && !s->clock.paused && !interrupted(s);
    if (!enabled) { pa_en(0); }
    bsp_codec_mute_set(!enabled);
    if (enabled) { pa_en(1); }
}

static void set_status(video_session_t *s, const char *message)
{
    lvgl_port_lock(0);
    if (s->status != NULL && !atomic_load(&s->closing)) {
        lv_label_set_text(s->status, message);
    }
    lvgl_port_unlock();
}

static int64_t elapsed_us(video_session_t *s, bool *paused)
{
    lvgl_port_lock(0);
    int64_t value = video_clock_elapsed(&s->clock, esp_timer_get_time());
    *paused = s->clock.paused;
    lvgl_port_unlock();
    return value;
}

static bool wait_for_pts(video_session_t *s, int64_t target_us)
{
    while (!interrupted(s)) {
        bool paused;
        int64_t elapsed = elapsed_us(s, &paused);
        if (!paused && elapsed >= target_us) { return true; }
        // At 100 Hz, pdMS_TO_TICKS(5) is zero and would starve the idle task.
        vTaskDelay(1);
    }
    return false;
}

static int source_read(void *buffer, uint32_t size, void *arg)
{
    video_source_t *source = arg;
    if (interrupted(source->session) ||
        (source->parse_deadline && esp_timer_get_time() > source->parse_deadline)) {
        return -1;
    }
    size_t count = fread(buffer, 1, size, source->file);
    return ferror(source->file) ? -1 : (int)count;
}

static int source_seek(uint32_t offset, void *arg)
{
    video_source_t *source = arg;
    if (offset > source->size || interrupted(source->session)) { return -1; }
    return fseek(source->file, (long)offset, SEEK_SET);
}

static uint32_t source_size(void *arg) { return ((video_source_t *)arg)->size; }

static void source_close(video_source_t *source)
{
    if (source->extractor != NULL) { esp_extractor_close(source->extractor); }
    if (source->file != NULL) { fclose(source->file); }
    memset(source, 0, sizeof(*source));
}

static bool source_open(video_source_t *source, video_session_t *s, const char *path, uint8_t mask)
{
    struct stat st;
    memset(source, 0, sizeof(*source));
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 12 ||
        st.st_size > VIDEO_MAX_BYTES) { return false; }
    source->session = s;
    source->size = (uint32_t)st.st_size;
    source->file = fopen(path, "rb");
    if (source->file == NULL) { return false; }
    setvbuf(source->file, NULL, _IOFBF, 8192);
    uint8_t header[12];
    if (fread(header, 1, sizeof(header), source->file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) || memcmp(header + 8, "AVI ", 4)) { goto fail; }
    rewind(source->file);
    source->parse_deadline = esp_timer_get_time() + 10000000;
    esp_extractor_config_t cfg = {
        .type = ESP_EXTRACTOR_TYPE_AVI, .extract_mask = mask,
        .in_read_cb = source_read, .in_seek_cb = source_seek, .in_size_cb = source_size,
        .in_ctx = source, .out_pool_size = 2 * VIDEO_MAX_FRAME, .out_align = 4,
    };
    if (esp_extractor_open(&cfg, &source->extractor) != ESP_EXTRACTOR_ERR_OK) { goto fail; }
    bool no_index = true;
    esp_extractor_ctrl(source->extractor, ESP_EXTRACTOR_CTRL_TYPE_SET_NO_INDEXING, &no_index, sizeof(no_index));
    if (esp_extractor_parse_stream(source->extractor) != ESP_EXTRACTOR_ERR_OK) { goto fail; }
    source->parse_deadline = 0;
    return true;
fail:
    source_close(source);
    return false;
}

static bool video_info_valid(video_source_t *source, esp_extractor_stream_info_t *info)
{
    if (esp_extractor_get_stream_info(source->extractor, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, 0, info) != ESP_EXTRACTOR_ERR_OK) {
        return false;
    }
    esp_extractor_video_stream_info_t *v = &info->video_info;
    return v->format == ESP_EXTRACTOR_VIDEO_FORMAT_MJPEG && v->width > 0 && v->width <= 320 &&
           v->height > 0 && v->height <= 240 && v->fps > 0 && v->fps <= 30 &&
           info->duration > 0 && info->duration <= 30 * 60 * 1000;
}

static bool audio_info_valid(const esp_extractor_audio_stream_info_t *a)
{
    return a->format == ESP_EXTRACTOR_AUDIO_FORMAT_PCM && a->bits_per_sample == 16 &&
           (a->channel == 1 || a->channel == 2) && a->sample_rate >= 8000 && a->sample_rate <= 44100;
}

static void audio_task(void *arg)
{
    video_session_t *s = arg;
    uint32_t bytes_per_second = s->audio_info.sample_rate * s->audio_info.channel * 2;
    uint64_t sent_bytes = 0;
    uint32_t packets = 0;
    uint32_t peak = 0;
    uint32_t next_report = bytes_per_second * 5;
    int16_t *stereo = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stereo == NULL) { atomic_store(&s->audio_error, true); }
    while (!interrupted(s) && !atomic_load(&s->audio_error)) {
        esp_extractor_frame_info_t frame = {0};
        esp_extractor_err_t ret = esp_extractor_read_frame(s->audio.extractor, &frame);
        if (ret == ESP_EXTRACTOR_ERR_EOS) { break; }
        if (ret != ESP_EXTRACTOR_ERR_OK) { atomic_store(&s->audio_error, true); break; }
        bool eos = EXTRACTOR_IS_EOS(frame.frame_flag);
        if (frame.stream_type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO && frame.frame_size > 0) {
            if (packets < 3) {
                ESP_LOGI(TAG, "PCM packet: pts=%lu bytes=%lu", (unsigned long)frame.pts, (unsigned long)frame.frame_size);
            }
            packets++;
            if (frame.frame_size > VIDEO_MAX_FRAME || frame.frame_buffer == NULL ||
                frame.frame_size % (2 * s->audio_info.channel) != 0) {
                atomic_store(&s->audio_error, true);
            } else {
                for (size_t pos = 0; pos < frame.frame_size && !interrupted(s);) {
                    int64_t pts = (int64_t)(sent_bytes * 1000000 / bytes_per_second);
                    if (!wait_for_pts(s, pts > 30000 ? pts - 30000 : 0)) { break; }
                    size_t bytes = frame.frame_size - pos;
                    if (bytes > 1024) { bytes = 1024; }
                    void *output = frame.frame_buffer + pos;
                    size_t output_bytes = bytes;
                    for (size_t i = 0; i < bytes; i += 2) {
                        const uint8_t *pcm = frame.frame_buffer + pos + i;
                        int32_t sample = (int16_t)((uint16_t)pcm[0] | ((uint16_t)pcm[1] << 8));
                        uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
                        if (magnitude > peak) { peak = magnitude; }
                    }
                    if (s->audio_info.channel == 1) {
                        const int16_t *mono = (const int16_t *)(frame.frame_buffer + pos);
                        for (size_t i = 0; i < bytes / 2; i++) {
                            stereo[i * 2] = mono[i];
                            stereo[i * 2 + 1] = mono[i];
                        }
                        output = stereo;
                        output_bytes = bytes * 2;
                    }
                    size_t written = 0;
                    if (bsp_i2s_write(output, output_bytes, &written, 100) != ESP_OK || written != output_bytes) {
                        atomic_store(&s->audio_error, true); break;
                    }
                    pos += bytes;
                    sent_bytes += bytes;
                    if (sent_bytes >= next_report) {
                        ESP_LOGI(TAG, "PCM output: sent=%llu peak=%lu", (unsigned long long)sent_bytes,
                                 (unsigned long)peak);
                        if (next_report == bytes_per_second * 5) { bsp_codec_log_playback_state(); }
                        next_report += bytes_per_second * 5;
                        peak = 0;
                    }
                }
            }
        }
        esp_extractor_release_frame(s->audio.extractor, &frame);
        if (eos || atomic_load(&s->audio_error)) { break; }
    }
    if (!interrupted(s)) { vTaskDelay(pdMS_TO_TICKS(80)); }
    free(stereo);
    source_close(&s->audio);
    ESP_LOGI(TAG, "audio stopped: error=%d packets=%lu sent=%llu stack_free=%u", atomic_load(&s->audio_error),
             (unsigned long)packets, (unsigned long long)sent_bytes,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    atomic_store(&s->audio_done, true);
    vTaskDelete(NULL);
}

static const char *play_file(video_session_t *s)
{
    video_source_t video = {0};
    esp_extractor_stream_info_t info = {0}, audio_info = {0};
    const char *result = "播放结束";
    bool audio_started = false;
    struct stat st;
    if (stat(s->path, &st) != 0) { result = "未找到视频文件"; goto done; }
    if (st.st_size > VIDEO_MAX_BYTES) { result = "视频超过256MB"; goto done; }
    if (!source_open(&video, s, s->path, ESP_EXTRACT_MASK_AV)) {
        result = "AVI文件读取失败"; goto done;
    }
    if (!video_info_valid(&video, &info)) {
        result = "需要320x240 MJPEG AVI"; goto done;
    }
    uint16_t audio_count = 0;
    esp_extractor_get_stream_num(video.extractor, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, &audio_count);
    if (audio_count > 0) {
        if (esp_extractor_get_stream_info(video.extractor, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, 0, &audio_info) != ESP_EXTRACTOR_ERR_OK ||
            !audio_info_valid(&audio_info.audio_info)) {
            result = "音轨需要16位PCM"; goto done;
        }
        esp_extractor_enable_stream(video.extractor, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, 0, false);
        if (!source_open(&s->audio, s, s->path, ESP_EXTRACT_MASK_AUDIO)) {
            result = "音轨读取失败"; goto done;
        }
        s->audio_info = audio_info.audio_info;
        if (bsp_codec_set_fs(s->audio_info.sample_rate, 16, I2S_SLOT_MODE_STEREO) != ESP_OK) {
            result = "音频初始化失败"; source_close(&s->audio); goto done;
        }
    }
    lvgl_port_lock(0);
    s->duration = info.duration;
    if (s->progress != NULL) { lv_bar_set_range(s->progress, 0, (int32_t)s->duration); }
    s->shown = 0; s->dropped = 0;
    s->clock = (video_clock_t){ .start_us = esp_timer_get_time() };
    s->has_audio = audio_count > 0;
    s->playing = true;
    if (s->play_label != NULL) { lv_label_set_text(s->play_label, LV_SYMBOL_PAUSE); }
    bsp_codec_volume_set(*s->volume, NULL);
    update_audio_output(s);
    ESP_LOGI(TAG, "audio setup: rate=%lu source_channels=%u volume=%u muted=%d",
             (unsigned long)s->audio_info.sample_rate, s->audio_info.channel, *s->volume, s->muted);
    lvgl_port_unlock();
    if (audio_count > 0) {
        atomic_store(&s->audio_done, false);
        if (xTaskCreatePinnedToCore(audio_task, "video_audio", 5 * 1024, s, 5, NULL, 0) != pdPASS) {
            atomic_store(&s->audio_done, true);
            source_close(&s->audio);
            result = "音频任务内存不足"; goto done;
        }
        audio_started = true;
    }
    set_status(s, "");
    ESP_LOGI(TAG, "play: %ux%u %ufps duration=%lu audio=%u", info.video_info.width, info.video_info.height,
             info.video_info.fps, (unsigned long)info.duration, audio_count);
    int64_t next_report = esp_timer_get_time() + 5000000;
    int64_t read_us = 0, decode_us = 0, submit_us = 0;
    uint32_t read_count = 0, decode_count = 0;
    while (!interrupted(s)) {
        esp_extractor_frame_info_t frame = {0};
        int64_t phase_start = esp_timer_get_time();
        esp_extractor_err_t ret = esp_extractor_read_frame(video.extractor, &frame);
        read_us += esp_timer_get_time() - phase_start;
        read_count++;
        if (ret == ESP_EXTRACTOR_ERR_EOS) { break; }
        if (ret != ESP_EXTRACTOR_ERR_OK) { result = "视频读取失败"; break; }
        bool eos = EXTRACTOR_IS_EOS(frame.frame_flag);
        bool decode_ok = true;
        if (frame.stream_type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO && frame.frame_size > 0) {
            bool paused;
            int64_t lateness = elapsed_us(s, &paused) - (int64_t)frame.pts * 1000;
            if (!paused && lateness > 1000000 / info.video_info.fps && s->shown > 0) {
                s->dropped++;
                esp_extractor_release_frame(video.extractor, &frame);
                vTaskDelay(1);
                if (eos) { break; }
                continue;
            }
            phase_start = esp_timer_get_time();
            esp_jpeg_image_cfg_t cfg = {
                .indata = frame.frame_buffer, .indata_size = frame.frame_size,
                .out_format = JPEG_IMAGE_FORMAT_RGB565, .out_scale = JPEG_IMAGE_SCALE_0,
                .outbuf = (uint8_t *)s->pixels, .outbuf_size = VIDEO_PIXELS * 2,
                .flags.swap_color_bytes = 1,
            };
            esp_jpeg_image_output_t image_info = {0};
            decode_ok = frame.frame_buffer != NULL && frame.frame_size <= VIDEO_MAX_FRAME &&
                        esp_jpeg_get_image_info(&cfg, &image_info) == ESP_OK &&
                        image_info.width > 0 && image_info.width <= 320 && image_info.height > 0 &&
                        image_info.height <= 240 && image_info.output_len <= VIDEO_PIXELS * 2;
            if (decode_ok) { decode_ok = esp_jpeg_decode(&cfg, &image_info) == ESP_OK; }
            decode_us += esp_timer_get_time() - phase_start;
            decode_count++;
            if (decode_ok && wait_for_pts(s, (int64_t)frame.pts * 1000)) {
                phase_start = esp_timer_get_time();
                lvgl_port_lock(0);
                if (s->image != NULL && !interrupted(s)) {
                        memcpy(s->display, s->pixels, image_info.output_len);
                        s->image_desc.header.cf = LV_IMG_CF_TRUE_COLOR;
                        s->image_desc.header.w = image_info.width;
                        s->image_desc.header.h = image_info.height;
                        s->image_desc.data = (uint8_t *)s->display;
                        s->image_desc.data_size = image_info.output_len;
                        lv_img_set_src(s->image, &s->image_desc);
                        lv_obj_center(s->image);
                        lv_obj_invalidate(s->image);
                        s->shown++;
                }
                lvgl_port_unlock();
                submit_us += esp_timer_get_time() - phase_start;
            }
        }
        esp_extractor_release_frame(video.extractor, &frame);
        if (!decode_ok) { result = "JPEG视频帧解码失败"; break; }
        if (atomic_load(&s->audio_error)) { result = "音频播放失败"; break; }
        if (eos) { break; }
        if (esp_timer_get_time() >= next_report) {
            ESP_LOGI(TAG, "progress: pts=%lu shown=%lu dropped=%lu", (unsigned long)frame.pts,
                     (unsigned long)s->shown, (unsigned long)s->dropped);
            ESP_LOGI(TAG, "timing avg us: read=%lld decode=%lld submit=%lld", read_us / (read_count ? read_count : 1),
                     decode_us / (decode_count ? decode_count : 1), submit_us / (decode_count ? decode_count : 1));
            read_us = decode_us = submit_us = 0;
            read_count = decode_count = 0;
            next_report = esp_timer_get_time() + 5000000;
        }
        vTaskDelay(1);
    }
done:
    if (strcmp(result, "播放结束") != 0 || interrupted(s)) { atomic_store(&s->stop_media, true); }
    while (audio_started && !atomic_load(&s->audio_done)) { vTaskDelay(pdMS_TO_TICKS(10)); }
    source_close(&video);
    lvgl_port_lock(0);
    s->playing = false;
    update_audio_output(s);
    if (s->play_label != NULL) { lv_label_set_text(s->play_label, LV_SYMBOL_PLAY); }
    ESP_LOGI(TAG, "stopped: shown=%lu dropped=%lu elapsed_ms=%lld stack_free=%u", (unsigned long)s->shown,
             (unsigned long)s->dropped, video_clock_elapsed(&s->clock, esp_timer_get_time()) / 1000,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    lvgl_port_unlock();
    return result;
}

static bool load_url(video_session_t *s)
{
    snprintf(s->url, sizeof(s->url), "%s", CONFIG_HANDHELD_VIDEO_URL);
    FILE *f = fopen("/sdcard/szpi/config/video.ini", "r");
    if (f != NULL) {
        char line[544];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "url=", 4) == 0) {
                line[strcspn(line, "\r\n")] = 0;
                strlcpy(s->url, line + 4, sizeof(s->url));
            }
        }
        fclose(f);
    }
    return strncmp(s->url, "http://", 7) == 0 || strncmp(s->url, "https://", 8) == 0;
}

static const char *download_video(video_session_t *s)
{
    if (!load_url(s)) { return "未配置网络视频地址"; }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) { return "WLAN未连接"; }
    mkdir("/sdcard/szpi/cache/video", 0775);
    FILE *file = NULL;
    uint8_t *buffer = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_http_client_config_t cfg = {
        .url = s->url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 3000,
        .buffer_size = 4096, .buffer_size_tx = 1024, .addr_type = HTTP_ADDR_TYPE_INET,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    const char *error = "下载失败";
    if (buffer == NULL || client == NULL) { error = "下载内存不足"; goto done; }
    int64_t content_len = 0;
    for (int attempt = 0; attempt < 6; attempt++) {
        if (interrupted(s)) { goto done; }
        if (esp_http_client_open(client, 0) != ESP_OK) { goto done; }
        content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && content_len >= 0) { break; }
        if ((status != 301 && status != 302 && status != 303 && status != 307 && status != 308) ||
            attempt == 5 || esp_http_client_set_redirection(client) != ESP_OK) { goto done; }
        esp_http_client_close(client);
    }
    if (content_len > VIDEO_MAX_BYTES) { error = "视频超过256MB"; goto done; }
    file = fopen(VIDEO_PART, "wb");
    if (file == NULL) { error = "TF卡无法写入"; goto done; }
    uint32_t total = 0;
    int64_t deadline = esp_timer_get_time() + 600LL * 1000000;
    int64_t update_at = 0;
    while (!interrupted(s) && esp_timer_get_time() < deadline) {
        int got = esp_http_client_read(client, (char *)buffer, 8192);
        if (got < 0) { goto done; }
        if (got == 0) {
            if (!esp_http_client_is_complete_data_received(client)) { goto done; }
            break;
        }
        if ((uint32_t)got > VIDEO_MAX_BYTES - total) { error = "视频超过256MB"; goto done; }
        if (fwrite(buffer, 1, (size_t)got, file) != (size_t)got) { error = "TF卡写入失败"; goto done; }
        total += (uint32_t)got;
        if (esp_timer_get_time() >= update_at) {
            char text[80];
            snprintf(text, sizeof(text), "下载 %lu / %lld KB", (unsigned long)(total / 1024), content_len / 1024);
            set_status(s, text);
            update_at = esp_timer_get_time() + 500000;
        }
        vTaskDelay(1);
    }
    if (interrupted(s) || esp_timer_get_time() >= deadline || total < 12 ||
        !esp_http_client_is_complete_data_received(client) || (content_len > 0 && total != content_len)) { goto done; }
    if (fclose(file) != 0) { file = NULL; error = "TF卡写入失败"; goto done; }
    file = NULL;
    esp_http_client_cleanup(client); client = NULL;
    free(buffer); buffer = NULL;
    video_source_t probe = {0};
    esp_extractor_stream_info_t info;
    bool valid = source_open(&probe, s, VIDEO_PART, ESP_EXTRACT_MASK_VIDEO) && video_info_valid(&probe, &info);
    source_close(&probe);
    if (!valid) { error = "下载内容不是兼容视频"; goto done; }
    unlink(VIDEO_OLD);
    bool had_old = access(VIDEO_CACHE, F_OK) == 0;
    if (had_old && rename(VIDEO_CACHE, VIDEO_OLD) != 0) { error = "缓存替换失败"; goto done; }
    if (rename(VIDEO_PART, VIDEO_CACHE) != 0) {
        if (had_old) { rename(VIDEO_OLD, VIDEO_CACHE); }
        error = "缓存替换失败"; goto done;
    }
    unlink(VIDEO_OLD);
    strlcpy(s->path, VIDEO_CACHE, sizeof(s->path));
    error = NULL;
    ESP_LOGI(TAG, "cached video: %lu bytes", (unsigned long)total);
done:
    if (file != NULL) { fclose(file); }
    if (client != NULL) { esp_http_client_cleanup(client); }
    free(buffer);
    if (error != NULL) { unlink(VIDEO_PART); }
    return error;
}

static void video_task(void *arg)
{
    video_session_t *s = arg;
    bool registered = esp_avi_extractor_register() == ESP_EXTRACTOR_ERR_OK;
    s->pixels = heap_caps_malloc(VIDEO_PIXELS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s->display = heap_caps_calloc(VIDEO_PIXELS, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ready = registered && s->pixels != NULL && s->display != NULL;
    if (!ready) { set_status(s, "视频内存不足"); }
    while (!atomic_load(&s->closing)) {
        int command = atomic_exchange(&s->command, COMMAND_NONE);
        if (!ready || command == COMMAND_NONE) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }
        atomic_store(&s->stop_media, false);
        atomic_store(&s->audio_error, false);
        const char *error = NULL;
        if (bsp_sdcard_mount() != ESP_OK && strncmp(s->path, "/local/", 7) != 0) {
            set_status(s, "TF卡不可用"); continue;
        }
        if (s->select_default) {
            strlcpy(s->path, access(VIDEO_DEFAULT, F_OK) == 0 ? VIDEO_DEFAULT : VIDEO_CACHE, sizeof(s->path));
            if (access(s->path, F_OK) != 0 && load_url(s)) { command = COMMAND_DOWNLOAD; }
            s->select_default = access(s->path, F_OK) != 0;
        }
        if (command == COMMAND_DOWNLOAD) {
            set_status(s, "连接视频来源...");
            error = download_video(s);
        }
        if (error == NULL && !interrupted(s)) {
            set_status(s, "加载视频...");
            error = play_file(s);
        }
        if (!atomic_load(&s->closing) && atomic_load(&s->command) == COMMAND_NONE && error != NULL) {
            set_status(s, error);
        }
    }
    if (registered) { esp_avi_extractor_unregister(); }
    pa_en(0);
    bsp_codec_mute_set(true);
    bsp_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, 16, CODEC_DEFAULT_CHANNEL);
    lvgl_port_lock(0);
    if (s->timer != NULL) { lv_timer_del(s->timer); }
    if (s->page != NULL) { lv_obj_del(s->page); }
    lv_img_cache_invalidate_src(&s->image_desc);
    free(s->display); free(s->pixels);
    s_video = NULL;
    if (s->on_close != NULL) { s->on_close(); }
    lvgl_port_unlock();
    free(s);
    vTaskDelete(NULL);
}

static void close_cb(lv_event_t *e)
{
    video_session_t *s = lv_event_get_user_data(e);
    atomic_store(&s->closing, true);
    update_audio_output(s);
    if (s->status != NULL) { lv_label_set_text(s->status, "关闭中..."); }
}

static void delete_cb(lv_event_t *e)
{
    video_session_t *s = lv_event_get_user_data(e);
    atomic_store(&s->closing, true);
    s->page = s->image = s->status = s->play_label = s->time = s->progress = NULL;
}

static void fullscreen_cb(lv_event_t *e)
{
    video_session_t *s = lv_event_get_user_data(e);
    s->fullscreen = !s->fullscreen;
    if (s->fullscreen) {
        lv_obj_add_flag(s->top, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s->bottom, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s->top, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s->bottom, LV_OBJ_FLAG_HIDDEN);
    }
}

static void pause_cb(lv_event_t *e)
{
    video_session_t *s = lv_event_get_user_data(e);
    if (!s->playing) { atomic_store(&s->command, COMMAND_PLAY); return; }
    video_clock_pause(&s->clock, !s->clock.paused, esp_timer_get_time());
    update_audio_output(s);
    lv_label_set_text(s->play_label, s->clock.paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

static void replay_cb(lv_event_t *e) { atomic_store(&((video_session_t *)lv_event_get_user_data(e))->command, COMMAND_PLAY); }
static void network_cb(lv_event_t *e) { atomic_store(&((video_session_t *)lv_event_get_user_data(e))->command, COMMAND_DOWNLOAD); }

static void mute_cb(lv_event_t *e)
{
    video_session_t *s = lv_event_get_user_data(e);
    s->muted = !s->muted;
    update_audio_output(s);
    lv_label_set_text(s->mute_label, s->muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
}

static void volume_cb(lv_event_t *e)
{
    video_session_t *s = lv_event_get_user_data(e);
    *s->volume = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    bsp_codec_volume_set(*s->volume, NULL);
}

static void update_time(lv_timer_t *timer)
{
    video_session_t *s = timer->user_data;
    if (s->time == NULL || !s->playing) { return; }
    uint32_t ms = (uint32_t)(video_clock_elapsed(&s->clock, esp_timer_get_time()) / 1000);
    if (ms > s->duration) { ms = s->duration; }
    lv_bar_set_value(s->progress, (int32_t)ms, LV_ANIM_OFF);
    lv_label_set_text_fmt(s->time, "%lu:%02lu / %lu:%02lu", (unsigned long)(ms / 60000),
                         (unsigned long)(ms / 1000 % 60), (unsigned long)(s->duration / 60000),
                         (unsigned long)(s->duration / 1000 % 60));
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width,
                        bool symbol, lv_event_cb_t cb, video_session_t *s)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y); lv_obj_set_size(btn, width, 30);
    lv_obj_set_style_radius(btn, 4, 0); lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x176b78), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, s);
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, symbol ? &lv_font_montserrat_20 : &font_alipuhui20, 0);
    lv_label_set_text(label, text); lv_obj_center(label);
    return label;
}

static lv_obj_t *bar(lv_obj_t *parent, int y, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, 0, y); lv_obj_set_size(obj, 320, height);
    lv_obj_set_style_pad_all(obj, 0, 0); lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0); lv_obj_set_style_bg_color(obj, lv_color_hex(0x192322), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

bool handheld_video_is_open(void) { return s_video != NULL; }

esp_err_t handheld_video_open(const char *path, uint8_t *volume, void (*on_close)(void))
{
    if (s_video != NULL || volume == NULL) { return ESP_ERR_INVALID_STATE; }
    if (path != NULL && strlen(path) >= sizeof(((video_session_t *)0)->path)) { return ESP_ERR_INVALID_ARG; }
    video_session_t *s = calloc(1, sizeof(*s));
    if (s == NULL) { return ESP_ERR_NO_MEM; }
    s->volume = volume; s->on_close = on_close; s->select_default = path == NULL;
    if (path != NULL) { strlcpy(s->path, path, sizeof(s->path)); }
    atomic_init(&s->closing, false); atomic_init(&s->audio_done, true);
    atomic_init(&s->audio_error, false); atomic_init(&s->stop_media, false);
    atomic_init(&s->command, COMMAND_PLAY);
    s->page = bar(lv_scr_act(), 0, 240);
    lv_obj_set_style_bg_color(s->page, lv_color_black(), 0);
    lv_obj_add_event_cb(s->page, delete_cb, LV_EVENT_DELETE, s);
    s->image = lv_img_create(s->page); lv_obj_center(s->image);
    lv_obj_add_flag(s->image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s->image, fullscreen_cb, LV_EVENT_CLICKED, s);
    s->top = bar(s->page, 0, 36);
    button(s->top, LV_SYMBOL_LEFT, 2, 3, 44, true, close_cb, s);
    lv_obj_t *title = lv_label_create(s->top);
    lv_obj_set_style_text_font(title, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_width(title, 252); lv_obj_set_pos(title, 58, 7);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    const char *name = path ? strrchr(path, '/') : NULL;
    lv_label_set_text(title, name ? name + 1 : "视频播放器");
    s->status = lv_label_create(s->page);
    lv_obj_set_width(s->status, 304); lv_obj_center(s->status);
    lv_obj_set_style_text_font(s->status, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s->status, lv_color_white(), 0);
    lv_obj_set_style_text_align(s->status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s->status, LV_LABEL_LONG_WRAP); lv_label_set_text(s->status, "加载视频...");
    s->bottom = bar(s->page, 184, 56);
    s->progress = lv_bar_create(s->bottom);
    lv_obj_set_pos(s->progress, 8, 1); lv_obj_set_size(s->progress, 304, 4);
    lv_bar_set_range(s->progress, 0, 30 * 60 * 1000);
    s->time = lv_label_create(s->bottom);
    lv_obj_set_style_text_font(s->time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s->time, lv_color_white(), 0);
    lv_obj_set_pos(s->time, 8, 6); lv_label_set_text(s->time, "0:00 / 0:00");
    button(s->bottom, LV_SYMBOL_REFRESH, 4, 23, 32, true, replay_cb, s);
    s->play_label = button(s->bottom, LV_SYMBOL_PLAY, 42, 23, 32, true, pause_cb, s);
    s->mute_label = button(s->bottom, LV_SYMBOL_VOLUME_MAX, 80, 23, 32, true, mute_cb, s);
    lv_obj_t *slider = lv_slider_create(s->bottom);
    lv_obj_set_pos(slider, 130, 36); lv_obj_set_size(slider, 84, 6);
    lv_slider_set_range(slider, 0, 100); lv_slider_set_value(slider, *volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, volume_cb, LV_EVENT_VALUE_CHANGED, s);
    button(s->bottom, "网络", 238, 23, 76, false, network_cb, s);
    s->timer = lv_timer_create(update_time, 250, s);
    s_video = s;
    if (xTaskCreatePinnedToCore(video_task, "video", 10 * 1024, s, 4, NULL, 1) != pdPASS) {
        lv_timer_del(s->timer); lv_obj_del(s->page); s_video = NULL; free(s);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
