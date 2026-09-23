#include "app_ui.h"
#include "audio_player.h"
#include "esp32_s3_szp.h"
#include "random_image_app.h"
#include "file_iterator.h"
#include "string.h"
#include <dirent.h>
#include "bt/ble_hidd_demo.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "jpeg_decoder.h"
#include "extra/libs/gif/lv_gif.h"
#include "driver/gpio.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <strings.h>
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "app_ui";

LV_FONT_DECLARE(font_alipuhui20);

lv_obj_t * main_obj; // 主界面
lv_obj_t * main_text_label; // 主界面 欢迎语
lv_obj_t * icon_in_obj; // 应用界面
int icon_flag; // 标记现在进入哪个应用 在主界面时为0

/******************************** 第1个图标 姿态传感器 应用程序*************************************************************************************/
lv_obj_t * label_x; // x角度值
lv_obj_t * label_y; // y角度值
lv_obj_t * label_z; // z角度值
lv_obj_t * x_bar;   // x角度bar
lv_obj_t * y_bar;   // y角度bar
lv_obj_t * z_bar;   // z角度bar

lv_obj_t * att_label; // 标题栏文字

lv_timer_t * my_lv_timer;

lv_obj_t *btn_att_back; // att姿态应用 后退按钮

static void att_clear_refs(void)
{
    label_x = NULL;
    label_y = NULL;
    label_z = NULL;
    x_bar = NULL;
    y_bar = NULL;
    z_bar = NULL;
    att_label = NULL;
    btn_att_back = NULL;
}

static int att_clamp_angle(float value)
{
    int angle = (int)roundf(value);
    if (angle < -100) {
        return -100;
    }
    if (angle > 100) {
        return 100;
    }
    return angle;
}

// 返回主界面按钮事件处理函数
static void btn_att_back_cb(lv_event_t * e)
{
    if (my_lv_timer != NULL) {
        lv_timer_del(my_lv_timer);
        my_lv_timer = NULL;
    }
    qmi8658_close(); // 关闭芯片运行
    if (icon_in_obj != NULL) {
        lv_obj_del(icon_in_obj); // 删除画布
        icon_in_obj = NULL;
    }
    att_clear_refs();
    icon_flag = 0;
}

// 定时更新姿态角度值
void att_update_cb(lv_timer_t * timer)
{
    if (icon_flag != 1 || icon_in_obj == NULL || label_x == NULL || x_bar == NULL) {
        return;
    }

    t_sQMI8658 QMI8658 = {0};
    int att_x, att_y, att_z;

    // 获取XYZ角度
    esp_err_t ret = qmi8658_fetch_angleFromAcc(&QMI8658);
    if (ret != ESP_OK) {
        if (att_label != NULL) {
            lv_label_set_text(att_label, "传感器等待");
        }
        return;
    }

    att_x = att_clamp_angle(QMI8658.AngleX);  // 四舍五入
    att_y = att_clamp_angle(QMI8658.AngleY);  // 四舍五入
    att_z = att_clamp_angle(QMI8658.AngleZ);  // 四舍五入

    // 更新角度值
    lv_label_set_text_fmt(label_x, "X: %d", att_x);
    lv_label_set_text_fmt(label_y, "Y: %d", att_y);
    lv_label_set_text_fmt(label_z, "Z: %d", att_z);
    // 更新角度bar
    lv_bar_set_start_value(x_bar, att_x-10, LV_ANIM_OFF);
    lv_bar_set_value(x_bar, att_x+10, LV_ANIM_OFF);
    lv_bar_set_start_value(y_bar, att_y-10, LV_ANIM_OFF);
    lv_bar_set_value(y_bar, att_y+10, LV_ANIM_OFF);
    lv_bar_set_start_value(z_bar, att_z-10, LV_ANIM_OFF);
    lv_bar_set_value(z_bar, att_z+10, LV_ANIM_OFF);

    // 判断运动状态
    uint8_t status = 0;
    ret = qmi8658_fetch_motion(&status);
    if (ret == ESP_OK && att_label != NULL) {
        if (status & 0x20) // 判断是否发生Any-Motion
        {
            lv_label_set_text(att_label, "运动或震动");
        }
        else if (status & 0x40) // 判断是否发生No-Motion
        {
            lv_label_set_text(att_label, "静止");
        }
        else if (status & 0x80) // 判断是否发生Significant-Motion
        {
            lv_label_set_text(att_label, "剧烈运动");
        }
    }
}

// 姿态监测处理任务
static void task_process_att(void *arg)
{
    esp_err_t ret = qmi8658_init();
    if (ret != ESP_OK){ // 如果传感器初始化不成功
        // 液晶屏提醒用户 传感器错误
        lvgl_port_lock(0);
        if (icon_flag == 1 && icon_in_obj != NULL) {
            if (att_label != NULL) {
                lv_label_set_text(att_label, "初始化失败");
            }
            lv_obj_t * label = lv_label_create(icon_in_obj);
            lv_label_set_text_fmt(label, "QMI8658传感器错误\n%s", esp_err_to_name(ret));
            lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        }
        lvgl_port_unlock();
    }
    else{ // 传感器初始化成功
        lvgl_port_lock(0);
        if (icon_flag != 1 || icon_in_obj == NULL) {
            lvgl_port_unlock();
            qmi8658_close();
            vTaskDelete(NULL);
            return;
        }

        // 显示x角度值
        label_x = lv_label_create(icon_in_obj);
        lv_label_set_text(label_x, "X:");
        lv_obj_set_style_text_color(label_x, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(label_x, &lv_font_montserrat_20, 0);
        lv_obj_align(label_x, LV_ALIGN_TOP_LEFT, 20, 60);
        // 显示x角度bar
        x_bar = lv_bar_create(icon_in_obj);
        lv_obj_set_size(x_bar, 200, 25);
        lv_obj_align(x_bar, LV_ALIGN_TOP_LEFT, 80, 60);
        lv_bar_set_mode(x_bar, LV_BAR_MODE_RANGE);
        lv_bar_set_range(x_bar, -101, 101);
        lv_bar_set_start_value(x_bar, -10, LV_ANIM_OFF);
        lv_bar_set_value(x_bar, 10, LV_ANIM_OFF);

        // 显示y角度值
        label_y = lv_label_create(icon_in_obj);
        lv_label_set_text(label_y, "Y:");
        lv_obj_set_style_text_color(label_y, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(label_y, &lv_font_montserrat_20, 0);
        lv_obj_align(label_y, LV_ALIGN_TOP_LEFT, 20, 120);
        // 显示y角度bar
        y_bar = lv_bar_create(icon_in_obj);
        lv_obj_set_size(y_bar, 200, 25);
        lv_obj_align(y_bar, LV_ALIGN_TOP_LEFT, 80, 120);
        lv_bar_set_mode(y_bar, LV_BAR_MODE_RANGE);
        lv_bar_set_range(y_bar, -101, 101);
        lv_bar_set_start_value(y_bar, -10, LV_ANIM_OFF);
        lv_bar_set_value(y_bar, 10, LV_ANIM_OFF);

        // 显示z角度值
        label_z = lv_label_create(icon_in_obj);
        lv_label_set_text(label_z, "Z:");
        lv_obj_set_style_text_color(label_z, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(label_z, &lv_font_montserrat_20, 0);
        lv_obj_align(label_z, LV_ALIGN_TOP_LEFT, 20, 180);
        // 显示z角度bar
        z_bar = lv_bar_create(icon_in_obj);
        lv_obj_set_size(z_bar, 200, 25);
        lv_obj_align(z_bar, LV_ALIGN_TOP_LEFT, 80, 180);
        lv_bar_set_mode(z_bar, LV_BAR_MODE_RANGE);
        lv_bar_set_range(z_bar, -101, 101);
        lv_bar_set_start_value(z_bar, -10, LV_ANIM_OFF);
        lv_bar_set_value(z_bar, 10, LV_ANIM_OFF);

        // 创建一个lv_timer 用于更新角度
        my_lv_timer = lv_timer_create(att_update_cb, 200, NULL);
        lvgl_port_unlock();
    }

    vTaskDelete(NULL);
}

static void att_event_handler(lv_event_t * e)
{
    if (icon_flag != 0) {
        return;
    }

    // 创建一个界面
    static lv_style_t style;
    static bool style_ready;
    if (!style_ready) {
        lv_style_init(&style);
        lv_style_set_radius(&style, 10);
        lv_style_set_bg_opa( &style, LV_OPA_COVER );
        lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
        lv_style_set_border_width(&style, 0);
        lv_style_set_pad_all(&style, 0);
        lv_style_set_width(&style, 320);
        lv_style_set_height(&style, 240);
        style_ready = true;
    }

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    lv_obj_t *att_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(att_title, 320, 40);
    lv_obj_set_style_pad_all(att_title, 0, 0);  // 设置间隙
    lv_obj_align(att_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(att_title, lv_color_hex(0x30a830), 0);
    // 显示标题
    att_label = lv_label_create(att_title);
    lv_label_set_text(att_label, "运动监测");
    lv_obj_set_style_text_color(att_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(att_label, &font_alipuhui20, 0);
    lv_obj_align(att_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_att_back = lv_btn_create(att_title);
    lv_obj_align(btn_att_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_att_back, 60, 30);
    lv_obj_set_style_border_width(btn_att_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_att_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_att_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_att_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_att_back, btn_att_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_att_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    icon_flag = 1; // 标记已经进入第一个应用
    BaseType_t ok = xTaskCreatePinnedToCore(task_process_att, "task_process_att", 4 * 1024, NULL, 5, NULL, 1);
    if (ok != pdPASS) {
        lv_label_set_text(att_label, "任务创建失败");
    }
}


/*********************  第2个图标   音乐播放器 *********************************************************************************************/
static audio_player_config_t player_config = {0};
static uint8_t g_sys_volume = VOLUME_DEFAULT;
static file_iterator_instance_t *file_iterator = NULL;
static bool g_audio_player_ready;
static bool g_audio_list_mode = true;

lv_obj_t *music_list;
lv_obj_t *label_play_pause;
lv_obj_t *btn_play_pause;
lv_obj_t *volume_slider;

lv_obj_t *music_title_label;
lv_obj_t *btn_music_back;

#define MUSIC_DIR_PRIMARY SD_MOUNT_POINT "/szpi/music"
#define MUSIC_DIR_ALT     SD_MOUNT_POINT "/music"

static bool music_name_is_audio(const char *name)
{
    if (name == NULL || name[0] == '.') {
        return false;
    }

    const char *ext = strrchr(name, '.');
    return ext != NULL && (strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".wav") == 0);
}

static void music_iterator_free(file_iterator_instance_t *iter)
{
    if (iter == NULL) {
        return;
    }

    if (iter->list != NULL) {
        for (size_t i = 0; i < iter->count; i++) {
            free(iter->list[i]);
        }
        free(iter->list);
    }
    free((void *)iter->directory_path);
    free(iter);
}

static file_iterator_instance_t *music_iterator_new_from_dir(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return NULL;
    }

    size_t count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (music_name_is_audio(entry->d_name)) {
            count++;
        }
    }

    if (count == 0) {
        closedir(dir);
        return NULL;
    }

    file_iterator_instance_t *iter = calloc(1, sizeof(*iter));
    if (iter == NULL) {
        closedir(dir);
        return NULL;
    }

    iter->list = calloc(count, sizeof(char *));
    iter->directory_path = strdup(dir_path);
    if (iter->list == NULL || iter->directory_path == NULL) {
        closedir(dir);
        music_iterator_free(iter);
        return NULL;
    }

    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL && iter->count < count) {
        if (!music_name_is_audio(entry->d_name)) {
            continue;
        }

        iter->list[iter->count] = strdup(entry->d_name);
        if (iter->list[iter->count] == NULL) {
            closedir(dir);
            music_iterator_free(iter);
            return NULL;
        }
        iter->count++;
    }
    closedir(dir);

    return iter;
}

static file_iterator_instance_t *music_iterator_new_from_sd(void)
{
    const char *dirs[] = {
        MUSIC_DIR_PRIMARY,
        MUSIC_DIR_ALT,
        SD_MOUNT_POINT,
    };

    if (bsp_sdcard_mount() != ESP_OK) {
        ESP_LOGW(TAG, "SD card not ready for music");
        return NULL;
    }

    mkdir(SD_MOUNT_POINT "/szpi", 0775);
    mkdir(MUSIC_DIR_PRIMARY, 0775);

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        file_iterator_instance_t *iter = music_iterator_new_from_dir(dirs[i]);
        if (iter != NULL) {
            ESP_LOGI(TAG, "Music dir: %s, count=%u", dirs[i], (unsigned)iter->count);
            return iter;
        }
    }

    return NULL;
}

static bool music_has_tracks(void)
{
    return file_iterator != NULL && file_iterator->count > 0;
}

// 播放指定序号的音乐
static void play_index(int index)
{
    if (!music_has_tracks()) {
        ESP_LOGW(TAG, "No music tracks available");
        return;
    }

    ESP_LOGI(TAG, "play_index(%d)", index);
    g_audio_list_mode = true;

    char filename[128];
    int retval = file_iterator_get_full_path_from_index(file_iterator, index, filename, sizeof(filename));
    if (retval == 0) {
        ESP_LOGE(TAG, "unable to retrieve filename");
        return;
    }

    FILE *fp = fopen(filename, "rb");
    if (fp) {
        ESP_LOGI(TAG, "Playing '%s'", filename);
        audio_player_play(fp);
    } else {
        ESP_LOGE(TAG, "unable to open index %d, filename '%s'", index, filename);
    }
}

// 设置声音处理函数
static esp_err_t _audio_player_mute_fn(AUDIO_PLAYER_MUTE_SETTING setting)
{
    esp_err_t ret = ESP_OK;
    // 判断是否需要静音
    bsp_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);
    // 如果不是静音 设置音量
    if (setting == AUDIO_PLAYER_UNMUTE) {
        bsp_codec_volume_set(g_sys_volume, NULL);
    }
    ret = ESP_OK;

    return ret;
}

// 播放音乐函数 播放音乐的时候 会不断进入
static esp_err_t _audio_player_write_fn(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;

    ret = bsp_i2s_write(audio_buffer, len, bytes_written, timeout_ms);

    return ret;
}

// 设置采样率 播放的时候进入一次
static esp_err_t _audio_player_std_clock(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    // ret = bsp_codec_set_fs(rate, bits_cfg, ch); // 如果播放的音乐固定是16000采样率 这里可以不用打开 如果采样率未知 把这里打开
    return ret;
}

// 回调函数 播放器每次动作都会进入
static void _audio_player_callback(audio_player_cb_ctx_t *ctx)
{
    ESP_LOGI(TAG, "ctx->audio_event = %d", ctx->audio_event);
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE: {  // 播放完一首歌 进入这个case
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_IDLE");
        if (!g_audio_list_mode || file_iterator == NULL) {
            pa_en(0);
            break;
        }
        // 指向下一首歌
        file_iterator_next(file_iterator);
        int index = file_iterator_get_index(file_iterator);
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
        // 修改当前播放的音乐名称
        lvgl_port_lock(0);
        lv_dropdown_set_selected(music_list, index);
        lvgl_port_unlock();
        break;
    }
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING: // 正在播放音乐
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_PLAY");
        pa_en(1); // 打开音频功放
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE: // 正在暂停音乐
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_PAUSE");
        pa_en(0); // 关闭音频功放
        break;
    default:
        break;
    }
}

// mp3播放器初始化
void mp3_player_init(void)
{
    // 获取文件信息
    if (file_iterator == NULL) {
        file_iterator = music_iterator_new_from_sd();
    }

    // 初始化音频播放
    if (g_audio_player_ready) {
        g_audio_list_mode = true;
        return;
    }

    player_config.mute_fn = _audio_player_mute_fn;
    player_config.write_fn = _audio_player_write_fn;
    player_config.clk_set_fn = _audio_player_std_clock;
    player_config.priority = 6;
    player_config.coreID = 1;

    ESP_ERROR_CHECK(audio_player_new(player_config));
    ESP_ERROR_CHECK(audio_player_callback_register(_audio_player_callback, NULL));
    g_audio_player_ready = true;
    g_audio_list_mode = true;
}


// 按钮样式相关定义
typedef struct {
    lv_style_t style_bg;
    lv_style_t style_focus_no_outline;
} button_style_t;

static button_style_t g_btn_styles;

button_style_t *ui_button_styles(void)
{
    return &g_btn_styles;
}

// 按钮样式初始化
static void ui_button_style_init(void)
{
    /*Init the style for the default state*/
    lv_style_init(&g_btn_styles.style_focus_no_outline);
    lv_style_set_outline_width(&g_btn_styles.style_focus_no_outline, 0);

    lv_style_init(&g_btn_styles.style_bg);
    lv_style_set_bg_opa(&g_btn_styles.style_bg, LV_OPA_100);
    lv_style_set_bg_color(&g_btn_styles.style_bg, lv_color_make(255, 255, 255));
    lv_style_set_shadow_width(&g_btn_styles.style_bg, 0);
}

// 播放暂停按钮 事件处理函数
static void btn_play_pause_cb(lv_event_t *event)
{
    if (!g_audio_player_ready || !music_has_tracks()) {
        return;
    }

    lv_obj_t *btn = lv_event_get_target(event);
    lv_obj_t *lab = (lv_obj_t *) btn->user_data;

    audio_player_state_t state = audio_player_get_state();
    printf("state=%d\n", state);
    if(state == AUDIO_PLAYER_STATE_IDLE){
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PAUSE);
        lvgl_port_unlock();
        int index = file_iterator_get_index(file_iterator);
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
    }else if (state == AUDIO_PLAYER_STATE_PAUSE) {
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PAUSE);
        lvgl_port_unlock();
        audio_player_resume();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) {
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PLAY);
        lvgl_port_unlock();
        audio_player_pause();
    }
}

// 上一首 下一首 按键事件处理函数
static void btn_prev_next_cb(lv_event_t *event)
{
    if (!g_audio_player_ready || !music_has_tracks()) {
        return;
    }

    bool is_next = (bool) event->user_data;

    if (is_next) {
        ESP_LOGI(TAG, "btn next");
        file_iterator_next(file_iterator);
    } else {
        ESP_LOGI(TAG, "btn prev");
        file_iterator_prev(file_iterator);
    }
    // 修改当前的音乐名称
    int index = file_iterator_get_index(file_iterator);
    lvgl_port_lock(0);
    lv_dropdown_set_selected(music_list, index);
    // lv_obj_t *label_title = (lv_obj_t *) music_list->user_data;
    // lv_label_set_text_static(label_title, file_iterator_get_name_from_index(file_iterator, index));
    lvgl_port_unlock();
    // 执行音乐事件
    audio_player_state_t state = audio_player_get_state();
    printf("prev_next_state=%d\n", state);
    if (state == AUDIO_PLAYER_STATE_IDLE) { 
        // Nothing to do
    }else if (state == AUDIO_PLAYER_STATE_PAUSE){ // 如果当前正在暂停歌曲
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
        audio_player_pause();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) { // 如果当前正在播放歌曲
        // 播放歌曲
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
    }
}

// 音量调节滑动条 事件处理函数
static void volume_slider_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    int volume = lv_slider_get_value(slider); // 获取slider的值
    bsp_codec_volume_set(volume, NULL); // 设置声音大小
    g_sys_volume = volume; // 把声音赋值给g_sys_volume保存
    ESP_LOGI(TAG, "volume '%d'", volume);
}

// 音乐列表 点击事件处理函数
static void music_list_cb(lv_event_t *event)
{
    if (!g_audio_player_ready || !music_has_tracks()) {
        return;
    }

    uint16_t index = lv_dropdown_get_selected(music_list);
    ESP_LOGI(TAG, "switching index to '%d'", index);
    file_iterator_set_index(file_iterator, index);
    
    audio_player_state_t state = audio_player_get_state();
    if (state == AUDIO_PLAYER_STATE_PAUSE){ // 如果当前正在暂停歌曲
        play_index(index);
        audio_player_pause();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) { // 如果当前正在播放歌曲
        play_index(index);
    }
}

// 音乐名称加入列表
static void build_file_list(lv_obj_t *music_list)
{
    lvgl_port_lock(0);
    lv_dropdown_clear_options(music_list);
    lvgl_port_unlock();

    if (!music_has_tracks()) {
        lvgl_port_lock(0);
        lv_dropdown_set_options_static(music_list, "No music");
        lvgl_port_unlock();
        return;
    }

    for(size_t i = 0; i < file_iterator->count; i++)
    {
        const char *file_name = file_iterator_get_name_from_index(file_iterator, i);
        if (NULL != file_name) {
            lvgl_port_lock(0);
            lv_dropdown_add_option(music_list, file_name, i); // 添加音乐名称到列表中
            lvgl_port_unlock();
        }
    }
    lvgl_port_lock(0);
    lv_dropdown_set_selected(music_list, 0); // 选择列表中的第一个
    lvgl_port_unlock();
}

// 播放器界面初始化
void music_ui(void)
{
    lvgl_port_lock(0);

    ui_button_style_init();// 初始化按键风格

    /* 创建播放暂停控制按键 */
    btn_play_pause = lv_btn_create(icon_in_obj);
    lv_obj_align(btn_play_pause, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_size(btn_play_pause, 50, 50);
    lv_obj_set_style_radius(btn_play_pause, 25, LV_STATE_DEFAULT);
    lv_obj_add_flag(btn_play_pause, LV_OBJ_FLAG_CHECKABLE);

    lv_obj_add_style(btn_play_pause, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_pause, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);

    label_play_pause = lv_label_create(btn_play_pause);

    lv_label_set_text_static(label_play_pause, LV_SYMBOL_PLAY);
    lv_obj_center(label_play_pause);

    lv_obj_set_user_data(btn_play_pause, (void *) label_play_pause);
    lv_obj_add_event_cb(btn_play_pause, btn_play_pause_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 创建上一首控制按键 */
    lv_obj_t *btn_play_prev = lv_btn_create(icon_in_obj);
    lv_obj_set_size(btn_play_prev, 50, 50);
    lv_obj_set_style_radius(btn_play_prev, 25, LV_STATE_DEFAULT);
    lv_obj_clear_flag(btn_play_prev, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_align_to(btn_play_prev, btn_play_pause, LV_ALIGN_OUT_LEFT_MID, -40, 0); 

    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_DEFAULT);

    lv_obj_t *label_prev = lv_label_create(btn_play_prev);
    lv_label_set_text_static(label_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(label_prev, &lv_font_montserrat_24, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_prev, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_center(label_prev);
    lv_obj_set_user_data(btn_play_prev, (void *) label_prev);
    lv_obj_add_event_cb(btn_play_prev, btn_prev_next_cb, LV_EVENT_CLICKED, (void *) false);

    /* 创建下一首控制按键 */
    lv_obj_t *btn_play_next = lv_btn_create(icon_in_obj);
    lv_obj_set_size(btn_play_next, 50, 50);
    lv_obj_set_style_radius(btn_play_next, 25, LV_STATE_DEFAULT);
    lv_obj_clear_flag(btn_play_next, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_align_to(btn_play_next, btn_play_pause, LV_ALIGN_OUT_RIGHT_MID, 40, 0);

    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_DEFAULT);

    lv_obj_t *label_next = lv_label_create(btn_play_next);
    lv_label_set_text_static(label_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(label_next, &lv_font_montserrat_24, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_next, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_center(label_next);
    lv_obj_set_user_data(btn_play_next, (void *) label_next);
    lv_obj_add_event_cb(btn_play_next, btn_prev_next_cb, LV_EVENT_CLICKED, (void *) true);

    /* 创建声音调节滑动条 */
    volume_slider = lv_slider_create(icon_in_obj);
    lv_obj_set_size(volume_slider, 200, 10);
    lv_obj_set_ext_click_area(volume_slider, 15);
    lv_obj_align(volume_slider, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, g_sys_volume, LV_ANIM_ON);
    lv_obj_add_event_cb(volume_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lab_vol_min = lv_label_create(icon_in_obj);
    lv_label_set_text_static(lab_vol_min, LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_font(lab_vol_min, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(lab_vol_min, volume_slider, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    lv_obj_t *lab_vol_max = lv_label_create(icon_in_obj);
    lv_label_set_text_static(lab_vol_max, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(lab_vol_max, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(lab_vol_max, volume_slider, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    /* 创建音乐列表 */ 
    music_list = lv_dropdown_create(icon_in_obj);
    lv_dropdown_clear_options(music_list);
    lv_dropdown_set_options_static(music_list, "Scanning...");
    lv_obj_set_style_text_font(music_list, &font_alipuhui20, LV_STATE_ANY);
    lv_obj_set_width(music_list, 200);
    lv_obj_align(music_list, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_event_cb(music_list, music_list_cb, LV_EVENT_VALUE_CHANGED, NULL);

    build_file_list(music_list);

    lvgl_port_unlock();
}

// 返回主界面按钮事件处理函数
static void btn_music_back_cb(lv_event_t * e)
{
    lv_obj_del(icon_in_obj); 
    if (g_audio_player_ready) {
        audio_player_delete();
        g_audio_player_ready = false;
    }
    music_iterator_free(file_iterator);
    file_iterator = NULL;
    icon_flag = 0;
}

// 进入音乐播放应用
static void music_event_handler(lv_event_t * e)
{
    // 初始化mp3播放器
    mp3_player_init();
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    lv_obj_t *music_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(music_title, 320, 40);
    lv_obj_set_style_pad_all(music_title, 0, 0);  // 设置间隙
    lv_obj_align(music_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(music_title, lv_color_hex(0xf87c30), 0);
    // 显示标题
    music_title_label = lv_label_create(music_title);
    lv_label_set_text(music_title_label, "音乐播放器");
    lv_obj_set_style_text_color(music_title_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(music_title_label, &font_alipuhui20, 0);
    lv_obj_align(music_title_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_music_back = lv_btn_create(music_title);
    lv_obj_align(btn_music_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_music_back, 60, 30);
    lv_obj_set_style_border_width(btn_music_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_music_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_music_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_music_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_music_back, btn_music_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_music_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    icon_flag = 2; // 标记已经进入第二个应用

    music_ui(); // 音乐播放器界面
}


/******************************** 第3个图标 SD卡 应用程序***************************************************************************/
lv_obj_t * sdcard_title; // SD卡页面标题背景
lv_obj_t * sdcard_label; // SD卡页面标题
lv_obj_t * sdcard_file_list; // SD卡文件列表
lv_obj_t * sdcard_preview_page; // 文件预览页面

#define SDCARD_PREVIEW_TOP 40
#define SDCARD_PREVIEW_W BSP_LCD_H_RES
#define SDCARD_PREVIEW_H (BSP_LCD_V_RES - SDCARD_PREVIEW_TOP)
#define SDCARD_TEXT_PREVIEW_LIMIT 4096
#define SDCARD_IMAGE_PREVIEW_LIMIT (2 * 1024 * 1024)
#define SDCARD_PNG_PREVIEW_LIMIT (768 * 1024)
#define SDCARD_GIF_PREVIEW_LIMIT (512 * 1024)
#define SDCARD_TEXT_PREVIEW_EXTRA 96

extern sdmmc_card_t *sdmmc_card;

struct file_path_info
{
    uint8_t path_index;  // 在第几级目录
    char path_now[512]; // 当前文件路径
    char path_back[512]; // 上级文件路径
};
struct file_path_info file_path_info;

typedef enum {
    SD_FILE_OTHER = 0,
    SD_FILE_AUDIO,
    SD_FILE_VIDEO,
    SD_FILE_IMAGE_JPEG,
    SD_FILE_IMAGE_PNG,
    SD_FILE_IMAGE_GIF,
    SD_FILE_TEXT,
} sd_file_type_t;

static uint8_t *s_preview_img_buf;
static lv_img_dsc_t s_preview_img_dsc;
static bool s_lv_fs_ready;
static volatile uint32_t s_sd_preview_generation;

typedef struct {
    char path[512];
    uint32_t generation;
} sd_jpeg_preview_req_t;

// 函数声明
esp_err_t list_sdcard_files(char * path);
static void file_list_btn_cb(lv_event_t * e); 
static void sd_preview_message(const char *title, const char *message);

static const char *sd_file_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    return (dot != NULL && dot[1] != '\0') ? dot + 1 : "";
}

static bool sd_ext_is(const char *ext, const char *want)
{
    return strcasecmp(ext, want) == 0;
}

static sd_file_type_t sd_classify_file(const char *path)
{
    const char *ext = sd_file_ext(path);
    if (sd_ext_is(ext, "mp3") || sd_ext_is(ext, "wav")) {
        return SD_FILE_AUDIO;
    }
    if (sd_ext_is(ext, "mp4") || sd_ext_is(ext, "avi")) {
        return SD_FILE_VIDEO;
    }
    if (sd_ext_is(ext, "jpg") || sd_ext_is(ext, "jpeg")) {
        return SD_FILE_IMAGE_JPEG;
    }
    if (sd_ext_is(ext, "png")) {
        return SD_FILE_IMAGE_PNG;
    }
    if (sd_ext_is(ext, "gif")) {
        return SD_FILE_IMAGE_GIF;
    }
    if (sd_ext_is(ext, "txt") || sd_ext_is(ext, "log") || sd_ext_is(ext, "ini") ||
        sd_ext_is(ext, "json") || sd_ext_is(ext, "md") || sd_ext_is(ext, "csv")) {
        return SD_FILE_TEXT;
    }
    return SD_FILE_OTHER;
}

static void sd_format_file_size(char *out, size_t out_len, off_t size)
{
    if (size < 0) {
        snprintf(out, out_len, "未知");
        return;
    }
    if (size < 1024) {
        snprintf(out, out_len, "%ld B", (long)size);
        return;
    }
    if (size < 1024 * 1024) {
        snprintf(out, out_len, "%.1f KB", (double)size / 1024.0);
        return;
    }
    snprintf(out, out_len, "%.1f MB", (double)size / (1024.0 * 1024.0));
}

static void sd_preview_large_file(const char *title, off_t file_size, size_t limit)
{
    char file_size_text[24];
    char limit_text[24];
    char message[192];

    sd_format_file_size(file_size_text, sizeof(file_size_text), file_size);
    sd_format_file_size(limit_text, sizeof(limit_text), (off_t)limit);
    snprintf(message, sizeof(message),
             "文件较大，已跳过自动预览\n大小: %s\n安全上限: %s\n\n请压缩或裁切后再预览",
             file_size_text, limit_text);
    sd_preview_message(title, message);
}

static void *sd_lv_fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    const char *flags = NULL;
    if (mode == LV_FS_MODE_WR) {
        flags = "wb";
    } else if (mode == LV_FS_MODE_RD) {
        flags = "rb";
    } else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) {
        flags = "rb+";
    }
    return flags == NULL ? NULL : fopen(path, flags);
}

static lv_fs_res_t sd_lv_fs_close(lv_fs_drv_t *drv, void *file_p)
{
    LV_UNUSED(drv);
    fclose((FILE *)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_lv_fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    LV_UNUSED(drv);
    *br = fread(buf, 1, btr, (FILE *)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_lv_fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    return fseek((FILE *)file_p, pos, whence) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t sd_lv_fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    LV_UNUSED(drv);
    long pos = ftell((FILE *)file_p);
    if (pos < 0) {
        return LV_FS_RES_UNKNOWN;
    }
    *pos_p = (uint32_t)pos;
    return LV_FS_RES_OK;
}

static void sd_lv_fs_init_once(void)
{
    if (s_lv_fs_ready) {
        return;
    }

    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);
    fs_drv.letter = 'S';
    fs_drv.open_cb = sd_lv_fs_open;
    fs_drv.close_cb = sd_lv_fs_close;
    fs_drv.read_cb = sd_lv_fs_read;
    fs_drv.seek_cb = sd_lv_fs_seek;
    fs_drv.tell_cb = sd_lv_fs_tell;
    lv_fs_drv_register(&fs_drv);
    s_lv_fs_ready = true;
}

static bool sd_audio_player_ready(void)
{
    if (g_audio_player_ready) {
        return true;
    }

    player_config.mute_fn = _audio_player_mute_fn;
    player_config.write_fn = _audio_player_write_fn;
    player_config.clk_set_fn = _audio_player_std_clock;
    player_config.priority = 6;
    player_config.coreID = 1;

    if (audio_player_new(player_config) != ESP_OK) {
        return false;
    }
    if (audio_player_callback_register(_audio_player_callback, NULL) != ESP_OK) {
        audio_player_delete();
        return false;
    }
    g_audio_player_ready = true;
    return true;
}

static void sd_preview_clear_current(void)
{
    if (g_audio_player_ready && !g_audio_list_mode) {
        audio_player_stop();
    }
    if (sdcard_preview_page != NULL) {
        lv_obj_del(sdcard_preview_page);
        sdcard_preview_page = NULL;
    }
    if (s_preview_img_buf != NULL) {
        heap_caps_free(s_preview_img_buf);
        s_preview_img_buf = NULL;
    }
}

static void sd_preview_cleanup(void)
{
    s_sd_preview_generation++;
    sd_preview_clear_current();
}

static void sd_preview_back_cb(lv_event_t *e)
{
    sd_preview_cleanup();
}

static lv_obj_t *sd_preview_create_page(const char *title)
{
    sd_preview_clear_current();

    sdcard_preview_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sdcard_preview_page, 320, 240);
    lv_obj_set_style_pad_all(sdcard_preview_page, 0, 0);
    lv_obj_set_style_border_width(sdcard_preview_page, 0, 0);
    lv_obj_set_style_radius(sdcard_preview_page, 0, 0);
    lv_obj_set_style_bg_color(sdcard_preview_page, lv_color_hex(0x101820), 0);

    lv_obj_t *bar = lv_obj_create(sdcard_preview_page);
    lv_obj_set_size(bar, 320, SDCARD_PREVIEW_TOP);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x008b8b), 0);

    lv_obj_t *back = lv_btn_create(bar);
    lv_obj_set_size(back, 56, 34);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back, sd_preview_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(back_label);

    lv_obj_t *title_label = lv_label_create(bar);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(title_label, 210);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 8, 0);

    lv_obj_t *body = lv_obj_create(sdcard_preview_page);
    lv_obj_set_size(body, 320, SDCARD_PREVIEW_H);
    lv_obj_align(body, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x101820), 0);
    return body;
}

static void sd_preview_message(const char *title, const char *message)
{
    lvgl_port_lock(0);
    lv_obj_t *body = sd_preview_create_page(title);
    lv_obj_t *label = lv_label_create(body);
    lv_label_set_text(label, message);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(label, 280);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();
}

static void sd_preview_text(const char *path, off_t file_size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        sd_preview_message("文本预览", "文件打开失败");
        return;
    }

    char *buf = heap_caps_malloc(SDCARD_TEXT_PREVIEW_LIMIT + SDCARD_TEXT_PREVIEW_EXTRA,
                                 MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        fclose(f);
        sd_preview_message("文本预览", "内存不足");
        return;
    }

    size_t len = fread(buf, 1, SDCARD_TEXT_PREVIEW_LIMIT, f);
    bool truncated = !feof(f);
    fclose(f);
    buf[len] = '\0';
    if (truncated || file_size > (off_t)SDCARD_TEXT_PREVIEW_LIMIT) {
        snprintf(buf + len, SDCARD_TEXT_PREVIEW_EXTRA, "\n\n...仅显示前 %u KB",
                 (unsigned)(SDCARD_TEXT_PREVIEW_LIMIT / 1024));
    }

    lvgl_port_lock(0);
    lv_obj_t *body = sd_preview_create_page("文本预览");
    lv_obj_t *ta = lv_textarea_create(body);
    lv_obj_set_size(ta, 312, 192);
    lv_obj_align(ta, LV_ALIGN_CENTER, 0, 0);
    lv_textarea_set_text(ta, buf);
    lv_textarea_set_cursor_click_pos(ta, false);
    lv_obj_set_style_text_font(ta, &font_alipuhui20, 0);
    lvgl_port_unlock();

    heap_caps_free(buf);
}

static void sd_build_lv_path(const char *path, char *out, size_t out_len)
{
    snprintf(out, out_len, "S:%s", path);
}

static void sd_preview_png(const char *path)
{
    sd_lv_fs_init_once();
    char lv_path[540];
    sd_build_lv_path(path, lv_path, sizeof(lv_path));

    lvgl_port_lock(0);
    lv_obj_t *body = sd_preview_create_page("PNG预览");
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *img = lv_img_create(body);
    lv_img_set_src(img, lv_path);
    lv_obj_center(img);
    lvgl_port_unlock();
}

static void sd_preview_gif(const char *path)
{
    sd_lv_fs_init_once();
    char lv_path[540];
    sd_build_lv_path(path, lv_path, sizeof(lv_path));

    lvgl_port_lock(0);
    lv_obj_t *body = sd_preview_create_page("GIF动图");
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *gif = lv_gif_create(body);
    lv_gif_set_src(gif, lv_path);
    lv_obj_center(gif);
    lvgl_port_unlock();
}

static esp_jpeg_image_scale_t sd_choose_jpeg_scale(int width, int height)
{
    if (width / 8 >= SDCARD_PREVIEW_W && height / 8 >= SDCARD_PREVIEW_H) {
        return JPEG_IMAGE_SCALE_1_8;
    }
    if (width / 4 >= SDCARD_PREVIEW_W && height / 4 >= SDCARD_PREVIEW_H) {
        return JPEG_IMAGE_SCALE_1_4;
    }
    if (width / 2 >= SDCARD_PREVIEW_W && height / 2 >= SDCARD_PREVIEW_H) {
        return JPEG_IMAGE_SCALE_1_2;
    }
    return JPEG_IMAGE_SCALE_0;
}

static uint16_t *sd_center_crop_canvas(const uint16_t *pixels, int width, int height)
{
    uint16_t *canvas = heap_caps_malloc(SDCARD_PREVIEW_W * SDCARD_PREVIEW_H * sizeof(uint16_t),
                                        MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (canvas == NULL) {
        return NULL;
    }

    memset(canvas, 0, SDCARD_PREVIEW_W * SDCARD_PREVIEW_H * sizeof(uint16_t));
    int src_x = width > SDCARD_PREVIEW_W ? (width - SDCARD_PREVIEW_W) / 2 : 0;
    int src_y = height > SDCARD_PREVIEW_H ? (height - SDCARD_PREVIEW_H) / 2 : 0;
    int copy_w = width > SDCARD_PREVIEW_W ? SDCARD_PREVIEW_W : width;
    int copy_h = height > SDCARD_PREVIEW_H ? SDCARD_PREVIEW_H : height;
    int dst_x = width < SDCARD_PREVIEW_W ? (SDCARD_PREVIEW_W - width) / 2 : 0;
    int dst_y = height < SDCARD_PREVIEW_H ? (SDCARD_PREVIEW_H - height) / 2 : 0;

    for (int y = 0; y < copy_h; y++) {
        const uint16_t *src = pixels + (src_y + y) * width + src_x;
        uint16_t *dst = canvas + (dst_y + y) * SDCARD_PREVIEW_W + dst_x;
        memcpy(dst, src, copy_w * sizeof(uint16_t));
    }
    return canvas;
}

static void sd_preview_jpeg_task(void *arg)
{
    sd_jpeg_preview_req_t *req = (sd_jpeg_preview_req_t *)arg;
    char path[512];
    uint32_t generation = req->generation;
    snprintf(path, sizeof(path), "%s", req->path);
    free(req);

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        if (generation == s_sd_preview_generation) {
            sd_preview_message("JPG预览", "文件打开失败");
        }
        vTaskDelete(NULL);
        return;
    }
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    rewind(f);
    if (file_len <= 0 || file_len > SDCARD_IMAGE_PREVIEW_LIMIT) {
        fclose(f);
        if (generation == s_sd_preview_generation) {
            sd_preview_message("JPG预览", "图片过大或为空");
        }
        vTaskDelete(NULL);
        return;
    }

    uint8_t *jpeg = heap_caps_malloc(file_len, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (jpeg == NULL) {
        fclose(f);
        if (generation == s_sd_preview_generation) {
            sd_preview_message("JPG预览", "内存不足");
        }
        vTaskDelete(NULL);
        return;
    }
    size_t got = fread(jpeg, 1, file_len, f);
    fclose(f);
    if (got != (size_t)file_len) {
        heap_caps_free(jpeg);
        if (generation == s_sd_preview_generation) {
            sd_preview_message("JPG预览", "读取失败");
        }
        vTaskDelete(NULL);
        return;
    }

    esp_jpeg_image_cfg_t info_cfg = {
        .indata = jpeg,
        .indata_size = got,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {
            .swap_color_bytes = 1,
        },
    };
    esp_jpeg_image_output_t original = { 0 };
    esp_err_t ret = esp_jpeg_get_image_info(&info_cfg, &original);
    if (ret == ESP_OK) {
        info_cfg.out_scale = sd_choose_jpeg_scale(original.width, original.height);
        esp_jpeg_image_output_t outimg = { 0 };
        ret = esp_jpeg_get_image_info(&info_cfg, &outimg);
        if (ret == ESP_OK) {
            uint16_t *decoded = heap_caps_malloc(outimg.output_len, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
            if (decoded == NULL) {
                ret = ESP_ERR_NO_MEM;
            } else {
                esp_jpeg_image_cfg_t jpeg_cfg = info_cfg;
                jpeg_cfg.outbuf = (uint8_t *)decoded;
                jpeg_cfg.outbuf_size = outimg.output_len;
                ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
                if (ret == ESP_OK) {
                    uint16_t *canvas = sd_center_crop_canvas(decoded, outimg.width, outimg.height);
                    if (canvas == NULL) {
                        ret = ESP_ERR_NO_MEM;
                    } else {
                        if (generation == s_sd_preview_generation) {
                            lvgl_port_lock(0);
                            lv_obj_t *body = sd_preview_create_page("JPG预览");
                            s_preview_img_buf = (uint8_t *)canvas;
                            s_preview_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
                            s_preview_img_dsc.header.always_zero = 0;
                            s_preview_img_dsc.header.reserved = 0;
                            s_preview_img_dsc.header.w = SDCARD_PREVIEW_W;
                            s_preview_img_dsc.header.h = SDCARD_PREVIEW_H;
                            s_preview_img_dsc.data_size = SDCARD_PREVIEW_W * SDCARD_PREVIEW_H * sizeof(uint16_t);
                            s_preview_img_dsc.data = s_preview_img_buf;
                            lv_obj_t *img = lv_img_create(body);
                            lv_img_set_src(img, &s_preview_img_dsc);
                            lv_obj_center(img);
                            lvgl_port_unlock();
                        } else {
                            heap_caps_free(canvas);
                        }
                    }
                }
                heap_caps_free(decoded);
            }
        }
    }
    heap_caps_free(jpeg);
    if (ret != ESP_OK && generation == s_sd_preview_generation) {
        ESP_LOGW(TAG, "jpeg preview failed: %s", esp_err_to_name(ret));
        sd_preview_message("JPG预览", "解码失败");
    }
    vTaskDelete(NULL);
}

static void sd_preview_jpeg(const char *path, uint32_t generation)
{
    sd_preview_message("JPG预览", "载入中...");
    sd_jpeg_preview_req_t *req = malloc(sizeof(*req));
    if (req != NULL) {
        snprintf(req->path, sizeof(req->path), "%s", path);
        req->generation = generation;
    }
    if (req == NULL ||
        xTaskCreatePinnedToCore(sd_preview_jpeg_task, "jpg_preview", 8192, req, 4, NULL, 1) != pdPASS) {
        free(req);
        sd_preview_message("JPG预览", "任务创建失败");
    }
}

static void sd_preview_audio(const char *path)
{
    if (!sd_audio_player_ready()) {
        sd_preview_message("音频播放", "播放器初始化失败");
        return;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        sd_preview_message("音频播放", "文件打开失败");
        return;
    }

    g_audio_list_mode = false;
    esp_err_t ret = audio_player_play(fp);
    if (ret != ESP_OK) {
        fclose(fp);
        sd_preview_message("音频播放", "播放失败");
        return;
    }

    lvgl_port_lock(0);
    lv_obj_t *body = sd_preview_create_page("音频播放");
    lv_obj_t *symbol = lv_label_create(body);
    lv_label_set_text(symbol, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(symbol, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(symbol, lv_color_hex(0xffffff), 0);
    lv_obj_align(symbol, LV_ALIGN_CENTER, 0, -36);

    lv_obj_t *label = lv_label_create(body);
    lv_label_set_text(label, "正在播放\n返回会停止");
    lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 18);
    lvgl_port_unlock();
}

static void sd_preview_file(const char *path, sd_file_type_t type, off_t file_size)
{
    switch (type) {
    case SD_FILE_AUDIO:
        sd_preview_audio(path);
        break;
    case SD_FILE_IMAGE_JPEG:
        if (file_size <= 0 || file_size > (off_t)SDCARD_IMAGE_PREVIEW_LIMIT) {
            sd_preview_large_file("JPG预览", file_size, SDCARD_IMAGE_PREVIEW_LIMIT);
            break;
        }
        s_sd_preview_generation++;
        sd_preview_jpeg(path, s_sd_preview_generation);
        break;
    case SD_FILE_IMAGE_PNG:
        if (file_size <= 0 || file_size > (off_t)SDCARD_PNG_PREVIEW_LIMIT) {
            sd_preview_large_file("PNG预览", file_size, SDCARD_PNG_PREVIEW_LIMIT);
            break;
        }
        sd_preview_png(path);
        break;
    case SD_FILE_IMAGE_GIF:
        if (file_size <= 0 || file_size > (off_t)SDCARD_GIF_PREVIEW_LIMIT) {
            sd_preview_large_file("GIF动图", file_size, SDCARD_GIF_PREVIEW_LIMIT);
            break;
        }
        sd_preview_gif(path);
        break;
    case SD_FILE_TEXT:
        sd_preview_text(path, file_size);
        break;
    case SD_FILE_VIDEO:
        {
            char size_text[24];
            char message[96];
            sd_format_file_size(size_text, sizeof(size_text), file_size);
            snprintf(message, sizeof(message), "暂未支持视频播放\n文件大小: %s", size_text);
            sd_preview_message("视频文件", message);
        }
        break;
    default:
        sd_preview_message("文件", "暂不支持预览");
        break;
    }
}

// 返回主界面按钮事件处理函数
static void btn_sdback_cb(lv_event_t * e)
{
    if (file_path_info.path_index == 0){ // 如果当前是根目录
        sd_preview_cleanup();
        bsp_sdcard_unmount(); // 卸载SD卡
        lv_obj_del(icon_in_obj); // 回到主界面
        icon_flag = 0;
    }else{
        lv_obj_clean(sdcard_file_list); // 清除当前wifi列表
        esp_err_t ret = list_sdcard_files(file_path_info.path_back); // 列出上一级目录文件
        if (ret == ESP_OK){ // 如果成功列出目录
            strcpy(file_path_info.path_now, file_path_info.path_back); // 刚刚进入的这个目录路径 变成当前路径
            file_path_info.path_index--; // 目录级数索引退一级
            // 计算再向下退一级的目录路径
            char *slash = strrchr(file_path_info.path_back, '/'); // 从后往前查找字符'/'
            if (slash!= NULL) { // 如果查找到
                *slash = '\0'; // 替换为NULL 表示字符串结束
            }
            ESP_LOGI(TAG, "path_index: %d", file_path_info.path_index);
            ESP_LOGI(TAG, "path_now: %s", file_path_info.path_now);
            ESP_LOGI(TAG, "path_back: %s", file_path_info.path_back);
        }
    }
}

// 列出SD卡中的文件
esp_err_t list_sdcard_files(char * path) 
{
    esp_err_t ret;
    DIR *dir;
    struct dirent *ent;
    lv_obj_t * btn;
    if ((dir = opendir(path))!= NULL) { // 打开目录
        while ((ent = readdir(dir))!= NULL) { // 读取目录中的文件
            /* 常规文件处理 */
            if (ent->d_type == DT_REG){ // 如果是常规文件
                sd_file_type_t file_type = sd_classify_file(ent->d_name);
                lvgl_port_lock(0);
                switch (file_type)
                {
                case SD_FILE_AUDIO:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_AUDIO, (const char *)ent->d_name);  // 显示音乐文件图标
                    break;
                case SD_FILE_VIDEO:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_VIDEO, (const char *)ent->d_name);  // 显示视频文件图标
                    break;
                case SD_FILE_IMAGE_JPEG:
                case SD_FILE_IMAGE_PNG:
                case SD_FILE_IMAGE_GIF:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_IMAGE, (const char *)ent->d_name);  // 显示图片文件图标
                    break;
                case SD_FILE_TEXT:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_FILE, (const char *)ent->d_name);  // 显示文本文件图标
                    break;
                default:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_FILE, (const char *)ent->d_name);  // 显示普通文件图标
                    break;
                }
                lv_obj_t *icon = lv_obj_get_child(btn, 0); // 获取图标指针
                lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0); // 修改图标的字体    
                lv_obj_add_event_cb(btn, file_list_btn_cb, LV_EVENT_CLICKED, NULL);  // 添加点击回调函数
                lvgl_port_unlock();
            }
            /* 文件夹处理 */
            else if (ent->d_type == DT_DIR) { // 如果是文件夹
                lvgl_port_lock(0);
                btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_DIRECTORY, (const char *)ent->d_name); 
                lv_obj_t *icon = lv_obj_get_child(btn, 0); // 获取图标指针
                lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0); // 修改图标的字体
                lv_obj_add_event_cb(btn, file_list_btn_cb, LV_EVENT_CLICKED, NULL); // 添加点击回调函数
                lvgl_port_unlock();
            }
        }
        closedir(dir);
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to open directory %s.", path);
        ret = ESP_FAIL;
    }
    return ret;
}

// 文件点击 事件处理函数
static void file_list_btn_cb(lv_event_t * e)
{
    const char *file_name = NULL; // 当前文件名称
    // 获取点击的按钮名称 即文件名称
    file_name = lv_list_get_btn_text(lv_obj_get_parent(e->target), e->target);
    ESP_LOGI(TAG, "file name: %s", file_name);

    char selected_path[512];
    int written = snprintf(selected_path, sizeof(selected_path), "%s/%s", file_path_info.path_now, file_name);
    if (written <= 0 || written >= sizeof(selected_path)) {
        sd_preview_message("文件", "路径过长");
        return;
    }

    struct stat st; // 获取文件状态信息结构体
    if (stat(selected_path, &st) == 0){ // 如果成功获取到状态信息
        if (S_ISDIR(st.st_mode)){ // 如果是目录
            strcpy(file_path_info.path_back, file_path_info.path_now); // 保存上一级目录
            strcpy(file_path_info.path_now, selected_path);
            lv_obj_clean(sdcard_file_list); // 清除当前wifi列表
            esp_err_t ret = list_sdcard_files(file_path_info.path_now);
            if (ret == ESP_OK){ // 如果成功列出了目录
                file_path_info.path_index++; // 表示进入到下一集目录
                ESP_LOGI(TAG, "path_index: %d", file_path_info.path_index);
                ESP_LOGI(TAG, "path_now: %s", file_path_info.path_now);
                ESP_LOGI(TAG, "path_back: %s", file_path_info.path_back);
            } else {
                strcpy(file_path_info.path_now, file_path_info.path_back);
            }
            return;
        }

        if (S_ISREG(st.st_mode)) {
            sd_preview_file(selected_path, sd_classify_file(selected_path), st.st_size);
            return;
        }
    }

    sd_preview_message("文件", "无法打开");
}

// SD卡处理任务
static void task_process_sdcard(void *arg)
{
    esp_err_t ret;

    ret = bsp_sdcard_mount(); // 挂载SD卡
    if(ret != ESP_OK){ // 如果没有挂载成功
        ESP_LOGE(TAG, "Failed to mount filesystem.");
        lvgl_port_lock(0);
        lv_label_set_text(sdcard_label, "SD卡挂载不成功");
        lvgl_port_unlock();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面一点显示的时间
        lvgl_port_lock(0);
        lv_obj_del(icon_in_obj);
        lvgl_port_unlock();
    }else{ // 如果挂载成功
        // 终端显示SD卡信息
        sdmmc_card_print_info(stdout, sdmmc_card);
        // 液晶屏标题栏显示SD卡容量
        lvgl_port_lock(0);
        lv_label_set_text_fmt(sdcard_label, "SD: %lluGB",
            (((uint64_t)sdmmc_card->csd.capacity) * sdmmc_card->csd.sector_size) >> 30);
        lvgl_port_unlock();

        // 创建返回按钮
        lvgl_port_lock(0);
        lv_obj_t *btn_back = lv_btn_create(sdcard_title);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_size(btn_back, 60, 30);
        lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
        lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
        lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
        lv_obj_add_event_cb(btn_back, btn_sdback_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

        lv_obj_t *label_back = lv_label_create(btn_back); 
        lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
        lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
        lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

        // 创建文件列表
        sdcard_file_list = lv_list_create(icon_in_obj);
        lv_obj_set_size(sdcard_file_list, 320, 200);
        lv_obj_align(sdcard_file_list, LV_ALIGN_TOP_LEFT, 0, 40);
        lv_obj_set_style_border_width(sdcard_file_list, 0, 0);
        lv_obj_set_style_text_font(sdcard_file_list, &font_alipuhui20, 0);
        lv_obj_set_scrollbar_mode(sdcard_file_list, LV_SCROLLBAR_MODE_OFF); // 隐藏wifi_list滚动条
        lvgl_port_unlock();
        // 列出 SD 卡中的文件
        file_path_info.path_index = 0; // 表示当前在根目录
        strcpy(file_path_info.path_now, SD_MOUNT_POINT); // 装入当前路径
        list_sdcard_files(file_path_info.path_now); // 列出当前目录文件
    }
    
    vTaskDelete(NULL);
}


// 进入SD卡应用程序
static void sdcard_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    sdcard_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(sdcard_title, 320, 40);
    lv_obj_set_style_pad_all(sdcard_title, 0, 0);  // 设置间隙
    lv_obj_align(sdcard_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(sdcard_title, lv_color_hex(0x008b8b), 0);
    // 显示标题
    sdcard_label = lv_label_create(sdcard_title);
    lv_label_set_text(sdcard_label, "TF卡扫描中...");
    lv_obj_set_style_text_color(sdcard_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(sdcard_label, &font_alipuhui20, 0);
    lv_obj_align(sdcard_label, LV_ALIGN_CENTER, 0, 0);

    icon_flag = 3; // 标记已经进入第三个应用

    xTaskCreatePinnedToCore(task_process_sdcard, "task_process_sdcard", 3 * 1024, NULL, 5, NULL, 1);
}



/******************************** 第4个图标 摄像头 应用程序 *****************************************************************************/
lv_obj_t * img_camera;
lv_obj_t * camera_status_label;
static uint8_t *s_camera_display_buf;
static volatile bool s_camera_save_requested;

// 摄像头图像
lv_img_dsc_t img_camera_dsc = {
  .header.cf = LV_IMG_CF_TRUE_COLOR,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 320,
  .header.h = 240,
  .data_size = 240*320*2,
};

static void camera_set_status(const char *text)
{
    if (camera_status_label == NULL) {
        return;
    }
    lvgl_port_lock(0);
    lv_label_set_text(camera_status_label, text);
    lvgl_port_unlock();
}

static void camera_save_frame(camera_fb_t *frame)
{
    camera_set_status("保存中...");

    if (bsp_sdcard_mount() != ESP_OK) {
        camera_set_status("TF卡不可用");
        return;
    }

    mkdir(SD_MOUNT_POINT "/szpi", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/photos", 0775);

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    bool ok = frame2jpg(frame, 80, &jpg_buf, &jpg_len);
    if (!ok || jpg_buf == NULL || jpg_len == 0) {
        camera_set_status("编码失败");
        return;
    }

    char path[128];
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/szpi/photos/photo_%lld.jpg", esp_timer_get_time() / 1000);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(jpg_buf);
        camera_set_status("保存失败");
        return;
    }

    size_t written = fwrite(jpg_buf, 1, jpg_len, f);
    fclose(f);
    free(jpg_buf);
    camera_set_status(written == jpg_len ? "已保存到TF卡" : "写入失败");
}

// 摄像头处理任务
static void task_process_camera(void *arg)
{
    bool camera_started = false;
    const size_t frame_bytes = 240 * 320 * 2;
    if (s_camera_display_buf == NULL) {
        s_camera_display_buf = heap_caps_malloc(frame_bytes, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    }
    if (s_camera_display_buf == NULL) {
        camera_set_status("显示缓冲不足");
        icon_flag = 0;
        goto cleanup;
    }

    memset(s_camera_display_buf, 0xff, frame_bytes);
    img_camera_dsc.data = s_camera_display_buf;
    lvgl_port_lock(0);
    if (img_camera != NULL) {
        lv_img_set_src(img_camera, &img_camera_dsc);
    }
    lvgl_port_unlock();

    camera_set_status("摄像头启动中...");
    esp_err_t ret = bsp_camera_init();
    if (ret != ESP_OK) {
        char msg[48];
        snprintf(msg, sizeof(msg), "摄像头失败:%s", esp_err_to_name(ret));
        camera_set_status(msg);
        vTaskDelay(pdMS_TO_TICKS(1200));
        icon_flag = 0;
        goto cleanup;
    }

    camera_started = true;
    camera_set_status("Ready");
    int null_frame_count = 0;

    while (icon_flag == 4)
    {
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame == NULL) {
            if (s_camera_save_requested && null_frame_count == 0) {
                camera_set_status("等待画面...");
            }
            null_frame_count++;
            if (null_frame_count > 80) {
                camera_set_status("摄像头无画面");
                null_frame_count = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        null_frame_count = 0;

        if (s_camera_save_requested) {
            s_camera_save_requested = false;
            camera_save_frame(frame);
        }

        size_t copy_len = frame->len < frame_bytes ? frame->len : frame_bytes;
        memcpy(s_camera_display_buf, frame->buf, copy_len);
        if (copy_len < frame_bytes) {
            memset(s_camera_display_buf + copy_len, 0, frame_bytes - copy_len);
        }
        img_camera_dsc.data = s_camera_display_buf;
        lvgl_port_lock(0);
        if (img_camera != NULL) {
            lv_img_set_src(img_camera, &img_camera_dsc);
        }
        lvgl_port_unlock();
        esp_camera_fb_return(frame);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
cleanup:
    if (camera_started) {
        esp_camera_deinit(); // 取消初始化摄像头
    }
    lvgl_port_lock(0);
    if (icon_in_obj != NULL) {
        lv_obj_del(icon_in_obj); // 删除摄像头画布
        icon_in_obj = NULL;
    }
    lvgl_port_unlock();
    if (s_camera_display_buf != NULL) {
        heap_caps_free(s_camera_display_buf);
        s_camera_display_buf = NULL;
    }
    img_camera = NULL;
    camera_status_label = NULL;
    s_camera_save_requested = false;
    dvp_pwdn(1); // 摄像头进入掉电模式
    icon_flag = 0;
    vTaskDelete(NULL);
}

// 返回主界面按钮事件处理函数
static void btn_camback_cb(lv_event_t * e)
{
    icon_flag = 0;
}

static void btn_camera_save_cb(lv_event_t * e)
{
    s_camera_save_requested = true;
    camera_set_status("准备拍照...");
}

// 进入摄像头应用
static void camera_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xcccccc));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    img_camera = lv_img_create(icon_in_obj);
    lv_obj_set_pos(img_camera, 0, 0);
    lv_obj_set_size(img_camera, 320, 240);

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(icon_in_obj);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(btn_back, 60, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_camback_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    lv_obj_t *btn_save = lv_btn_create(icon_in_obj);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(btn_save, 60, 30);
    lv_obj_set_style_border_width(btn_save, 0, 0);
    lv_obj_set_style_pad_all(btn_save, 0, 0);
    lv_obj_set_style_bg_opa(btn_save, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn_save, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_save, btn_camera_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_save = lv_label_create(btn_save);
    lv_label_set_text(label_save, LV_SYMBOL_SAVE);
    lv_obj_set_style_text_font(label_save, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_save, lv_color_hex(0xffffff), 0);
    lv_obj_center(label_save);

    camera_status_label = lv_label_create(icon_in_obj);
    lv_label_set_text(camera_status_label, "Ready");
    lv_obj_set_style_text_font(camera_status_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(camera_status_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(camera_status_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(camera_status_label, LV_OPA_50, 0);
    lv_obj_set_style_pad_left(camera_status_label, 8, 0);
    lv_obj_set_style_pad_right(camera_status_label, 8, 0);
    lv_obj_set_style_pad_top(camera_status_label, 3, 0);
    lv_obj_set_style_pad_bottom(camera_status_label, 3, 0);
    lv_obj_align(camera_status_label, LV_ALIGN_BOTTOM_MID, 0, -6);

    icon_flag = 4; // 标记已经进入第四个应用

    xTaskCreatePinnedToCore(task_process_camera, "task_process_camera", 4 * 1024, NULL, 5, NULL, 1);
}


/******************************** 第5个图标 WiFi设置 应用程序*****************************************************************************/

lv_obj_t *wifi_scan_page;     // wifi扫描页面 obj
lv_obj_t *wifi_password_page; // wifi密码页面 obj
lv_obj_t *wifi_connect_page;  // wifi连接页面 obj
lv_obj_t *obj_scan_title;     // wifi扫描页面标题
lv_obj_t *label_wifi_scan;    // wif扫描页面 label
lv_obj_t *wifi_list;          // wifi列表  list
lv_obj_t *label_wifi_connect; // wifi连接页面label 
lv_obj_t *ta_pass_text;       // 密码输入文本框 textarea
lv_obj_t *roller_num;         // 数字roller
lv_obj_t *roller_letter_low;  // 小写字母roller
lv_obj_t *roller_letter_up;   // 大写字母roller
lv_obj_t *label_wifi_name;    // wifi名称label


#define DEFAULT_SCAN_LIST_SIZE   5                // 最大扫描wifi个数

// wifi事件组
static EventGroupHandle_t s_wifi_event_group = NULL;
// wifi事件
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1
#define WIFI_START_BIT        BIT2
#define WIFI_GET_SNTP_BIT     BIT3
// wifi最大重连次数
#define EXAMPLE_ESP_MAXIMUM_RETRY  3

// wifi账号队列
static QueueHandle_t xQueueWifiAccount = NULL;
// 队列要传输的内容
typedef struct {
    char wifi_ssid[32];  // 获取wifi名称
    char wifi_password[64]; // 获取wifi密码  
    wifi_auth_mode_t authmode;
    char back_flag; // 是否退出       
} wifi_account_t;

#define WIFI_HISTORY_MAX 10
#define WIFI_HISTORY_PATH SD_MOUNT_POINT "/szpi/config/wlan_history.ini"
#define RANDOM_WIFI_CONFIG_PATH SD_MOUNT_POINT "/szpi/config/wifi.ini"

typedef struct {
    char ssid[33];
    char password[65];
    wifi_auth_mode_t authmode;
} wifi_saved_network_t;

static wifi_saved_network_t s_wifi_history[WIFI_HISTORY_MAX];
static size_t s_wifi_history_count;
static bool s_wifi_history_loaded;
static wifi_ap_record_t s_wifi_ap_records[DEFAULT_SCAN_LIST_SIZE];
static uint16_t s_wifi_ap_count;
static bool s_wifi_handlers_registered;
static bool s_wifi_driver_started;
static int s_wifi_retry_num;

static char *wifi_trim(char *text)
{
    if (text == NULL) {
        return NULL;
    }

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

static void wifi_history_upsert(const char *ssid, const char *password, wifi_auth_mode_t authmode)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    for (size_t i = 0; i < s_wifi_history_count; i++) {
        if (strcmp(s_wifi_history[i].ssid, ssid) == 0) {
            snprintf(s_wifi_history[i].password, sizeof(s_wifi_history[i].password), "%s", password ? password : "");
            s_wifi_history[i].authmode = authmode;
            return;
        }
    }

    if (s_wifi_history_count >= WIFI_HISTORY_MAX) {
        memmove(&s_wifi_history[0], &s_wifi_history[1], sizeof(s_wifi_history[0]) * (WIFI_HISTORY_MAX - 1));
        s_wifi_history_count = WIFI_HISTORY_MAX - 1;
    }

    snprintf(s_wifi_history[s_wifi_history_count].ssid, sizeof(s_wifi_history[s_wifi_history_count].ssid), "%s", ssid);
    snprintf(s_wifi_history[s_wifi_history_count].password, sizeof(s_wifi_history[s_wifi_history_count].password), "%s", password ? password : "");
    s_wifi_history[s_wifi_history_count].authmode = authmode;
    s_wifi_history_count++;
}

static const wifi_saved_network_t *wifi_history_find(const char *ssid)
{
    if (ssid == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < s_wifi_history_count; i++) {
        if (strcmp(s_wifi_history[i].ssid, ssid) == 0) {
            return &s_wifi_history[i];
        }
    }
    return NULL;
}

static void wifi_history_commit_entry(char *ssid, char *password, wifi_auth_mode_t authmode)
{
    if (ssid[0] != '\0') {
        wifi_history_upsert(ssid, password, authmode);
    }
    ssid[0] = '\0';
    password[0] = '\0';
}

static void wifi_history_load(void)
{
    if (s_wifi_history_loaded) {
        return;
    }

    s_wifi_history_loaded = true;
    s_wifi_history_count = 0;

    if (bsp_sdcard_mount() != ESP_OK) {
        return;
    }

    FILE *f = fopen(WIFI_HISTORY_PATH, "r");
    if (f == NULL) {
        return;
    }

    char line[160];
    char ssid[33] = {0};
    char password[65] = {0};
    wifi_auth_mode_t authmode = WIFI_AUTH_WPA2_PSK;

    while (fgets(line, sizeof(line), f) != NULL) {
        char *text = wifi_trim(line);
        if (text[0] == '\0' || text[0] == '#') {
            continue;
        }
        if (strcmp(text, "[network]") == 0) {
            wifi_history_commit_entry(ssid, password, authmode);
            authmode = WIFI_AUTH_WPA2_PSK;
            continue;
        }

        char *eq = strchr(text, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *key = wifi_trim(text);
        char *value = wifi_trim(eq + 1);
        if (strcmp(key, "ssid") == 0) {
            snprintf(ssid, sizeof(ssid), "%s", value);
        } else if (strcmp(key, "password") == 0) {
            snprintf(password, sizeof(password), "%s", value);
        } else if (strcmp(key, "auth") == 0) {
            authmode = (wifi_auth_mode_t)atoi(value);
        }
    }

    wifi_history_commit_entry(ssid, password, authmode);
    fclose(f);
}

static void wifi_history_save_all(void)
{
    if (bsp_sdcard_mount() != ESP_OK) {
        return;
    }

    mkdir(SD_MOUNT_POINT "/szpi", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/config", 0775);

    FILE *f = fopen(WIFI_HISTORY_PATH, "w");
    if (f == NULL) {
        return;
    }

    fprintf(f, "# SZPI WLAN history\n");
    for (size_t i = 0; i < s_wifi_history_count; i++) {
        fprintf(f, "[network]\nssid=%s\npassword=%s\nauth=%d\n",
                s_wifi_history[i].ssid,
                s_wifi_history[i].password,
                (int)s_wifi_history[i].authmode);
    }
    fclose(f);
}

static void wifi_save_random_image_config(const char *ssid, const char *password)
{
    if (bsp_sdcard_mount() != ESP_OK) {
        return;
    }

    mkdir(SD_MOUNT_POINT "/szpi", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/config", 0775);

    FILE *f = fopen(RANDOM_WIFI_CONFIG_PATH, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f,
            "# Used by random image app. Edit on your computer if needed.\n"
            "ssid=%s\n"
            "password=%s\n"
            "api=%s\n",
            ssid ? ssid : "",
            password ? password : "",
            CONFIG_RANDOM_IMAGE_API_URL);
    fclose(f);
}

static wifi_auth_mode_t wifi_find_authmode(const char *ssid)
{
    if (ssid == NULL) {
        return WIFI_AUTH_WPA2_PSK;
    }

    for (uint16_t i = 0; i < s_wifi_ap_count; i++) {
        if (strcmp((const char *)s_wifi_ap_records[i].ssid, ssid) == 0) {
            return s_wifi_ap_records[i].authmode;
        }
    }
    return WIFI_AUTH_WPA2_PSK;
}

static bool wifi_auth_is_open(wifi_auth_mode_t authmode)
{
    return authmode == WIFI_AUTH_OPEN;
}

// 密码roller的遮罩显示效果
static void mask_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    static int16_t mask_top_id = -1;
    static int16_t mask_bottom_id = -1;

    if(code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_MASKED);
    }
    else if(code == LV_EVENT_DRAW_MAIN_BEGIN) {
        /* add mask */
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
        lv_coord_t font_h = lv_font_get_line_height(font);

        lv_area_t roller_coords;
        lv_obj_get_coords(obj, &roller_coords);

        lv_area_t rect_area;
        rect_area.x1 = roller_coords.x1;
        rect_area.x2 = roller_coords.x2;
        rect_area.y1 = roller_coords.y1;
        rect_area.y2 = roller_coords.y1 + (lv_obj_get_height(obj) - font_h - line_space) / 2;

        lv_draw_mask_fade_param_t * fade_mask_top = lv_mem_buf_get(sizeof(lv_draw_mask_fade_param_t));
        lv_draw_mask_fade_init(fade_mask_top, &rect_area, LV_OPA_TRANSP, rect_area.y1, LV_OPA_COVER, rect_area.y2);
        mask_top_id = lv_draw_mask_add(fade_mask_top, NULL);

        rect_area.y1 = rect_area.y2 + font_h + line_space - 1;
        rect_area.y2 = roller_coords.y2;

        lv_draw_mask_fade_param_t * fade_mask_bottom = lv_mem_buf_get(sizeof(lv_draw_mask_fade_param_t));
        lv_draw_mask_fade_init(fade_mask_bottom, &rect_area, LV_OPA_COVER, rect_area.y1, LV_OPA_TRANSP, rect_area.y2);
        mask_bottom_id = lv_draw_mask_add(fade_mask_bottom, NULL);

    }
    else if(code == LV_EVENT_DRAW_POST_END) {
        lv_draw_mask_fade_param_t * fade_mask_top = lv_draw_mask_remove_id(mask_top_id);
        lv_draw_mask_fade_param_t * fade_mask_bottom = lv_draw_mask_remove_id(mask_bottom_id);
        lv_draw_mask_free_param(fade_mask_top);
        lv_draw_mask_free_param(fade_mask_bottom);
        lv_mem_buf_release(fade_mask_top);
        lv_mem_buf_release(fade_mask_bottom);
        mask_top_id = -1;
        mask_bottom_id = -1;
    }
}

// 数字键 处理函数
static void btn_num_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Num Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_num, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

// 小写字母确认键 处理函数
static void btn_letter_low_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Letter-Low Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_letter_low, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

// 大写字母确认键 处理函数
static void btn_letter_up_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Letter-Up Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_letter_up, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

static void lv_wifi_connect(void)
{
    if (wifi_password_page != NULL) {
        lv_obj_del(wifi_password_page); // 删除密码输入界面
        wifi_password_page = NULL;
    }

    // 创建一个面板对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_radius(&style, 0);  
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    wifi_connect_page = lv_obj_create(lv_scr_act());
    lv_obj_add_style(wifi_connect_page, &style, 0);

    // 绘制label提示
    label_wifi_connect = lv_label_create(wifi_connect_page);
    lv_label_set_text(label_wifi_connect, "WLAN连接中...");
    lv_obj_set_style_text_font(label_wifi_connect, &font_alipuhui20, 0);
    lv_obj_align(label_wifi_connect, LV_ALIGN_CENTER, 0, -50);
}

static void wifi_begin_connect(const char *ssid, const char *password, wifi_auth_mode_t authmode)
{
    if (ssid == NULL || ssid[0] == '\0' || xQueueWifiAccount == NULL) {
        return;
    }

    wifi_account_t wifi_account = {0};
    snprintf(wifi_account.wifi_ssid, sizeof(wifi_account.wifi_ssid), "%s", ssid);
    snprintf(wifi_account.wifi_password, sizeof(wifi_account.wifi_password), "%s", password ? password : "");
    wifi_account.authmode = authmode;
    wifi_account.back_flag = 0;

    ESP_LOGI(TAG, "connect request SSID:%s auth:%d saved:%s",
             wifi_account.wifi_ssid,
             (int)wifi_account.authmode,
             wifi_account.wifi_password[0] ? "yes" : "empty");
    lv_wifi_connect(); // 显示wifi连接界面
    xQueueSend(xQueueWifiAccount, &wifi_account, portMAX_DELAY);
}

// WiFi连接按钮 处理函数
static void btn_connect_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "OK Clicked");
        const char *wifi_ssid = lv_label_get_text(label_wifi_name);
        const char *wifi_password = lv_textarea_get_text(ta_pass_text);
        wifi_auth_mode_t authmode = wifi_find_authmode(wifi_ssid);
        if(*wifi_password != '\0' || wifi_auth_is_open(authmode)) {
            wifi_begin_connect(wifi_ssid, wifi_password, authmode);
        }
    }
}

// 删除密码按钮
static void btn_del_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Clicked");
        lv_textarea_del_char(ta_pass_text);
    }
}

// 返回按钮
static void btn_back_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "btn_back Clicked");
        lv_obj_del(wifi_password_page); // 删除密码输入界面
        wifi_password_page = NULL;
    }
}

// 进入输入密码界面
static void list_btn_cb(lv_event_t * e)
{
    // 获取点击到的WiFi名称
    const char *wifi_name=NULL;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED) {
        wifi_name = lv_list_get_btn_text(wifi_list, obj);
        ESP_LOGI(TAG, "WLAN Name: %s", wifi_name);
    } else {
        return;
    }

    wifi_auth_mode_t authmode = wifi_find_authmode(wifi_name);
    const wifi_saved_network_t *saved = wifi_history_find(wifi_name);
    if (wifi_auth_is_open(authmode)) {
        wifi_begin_connect(wifi_name, "", authmode);
        return;
    }
    if (saved != NULL && saved->password[0] != '\0') {
        wifi_begin_connect(wifi_name, saved->password, authmode);
        return;
    }

    // 创建密码输入页面
    wifi_password_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(wifi_password_page, 320, 240);
    lv_obj_set_style_border_width(wifi_password_page, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(wifi_password_page, 0, 0);  // 设置间隙
    lv_obj_set_style_radius(wifi_password_page, 0, 0); // 设置圆角

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_ALL, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0x000000), 0); 
    lv_obj_align(label_back, LV_ALIGN_TOP_LEFT, 10, 10);

    // 显示选中的wifi名称
    label_wifi_name = lv_label_create(wifi_password_page);
    lv_obj_set_style_text_font(label_wifi_name, &lv_font_montserrat_20, 0);
    lv_label_set_text(label_wifi_name, wifi_name);
    lv_obj_align(label_wifi_name, LV_ALIGN_TOP_MID, 0, 10);

    // 创建密码输入框
    ta_pass_text = lv_textarea_create(wifi_password_page);
    lv_obj_set_style_text_font(ta_pass_text, &lv_font_montserrat_20, 0);
    lv_textarea_set_one_line(ta_pass_text, true);  // 一行显示
    lv_textarea_set_password_mode(ta_pass_text, false); // 是否使用密码输入显示模式
    lv_textarea_set_placeholder_text(ta_pass_text, "password"); // 设置提醒词
    lv_obj_set_width(ta_pass_text, 150); // 宽度
    lv_obj_align(ta_pass_text, LV_ALIGN_TOP_LEFT, 10, 40); // 位置
    lv_obj_add_state(ta_pass_text, LV_STATE_FOCUSED); // 显示光标

    // 创建“连接按钮”
    lv_obj_t *btn_connect = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_connect, LV_ALIGN_TOP_LEFT, 170, 40);
    lv_obj_set_width(btn_connect, 65); // 宽度
    lv_obj_add_event_cb(btn_connect, btn_connect_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_ok = lv_label_create(btn_connect);
    lv_label_set_text(label_ok, "OK");
    lv_obj_set_style_text_font(label_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_ok);

    // 创建“删除按钮”
    lv_obj_t *btn_del = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_del, LV_ALIGN_TOP_LEFT, 245, 40);
    lv_obj_set_width(btn_del, 65); // 宽度
    lv_obj_add_event_cb(btn_del, btn_del_cb, LV_EVENT_ALL, NULL);  // 事件处理函数

    lv_obj_t *label_del = lv_label_create(btn_del);
    lv_label_set_text(label_del, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_text_font(label_del, &lv_font_montserrat_20, 0);
    lv_obj_center(label_del);

    // 创建roller样式
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_radius(&style, 0);

    // 创建"数字"roller
    const char * opts_num = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

    roller_num = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_num, &style, 0);
    lv_obj_set_style_bg_opa(roller_num, LV_OPA_50, LV_PART_SELECTED);

    lv_roller_set_options(roller_num, opts_num, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_num, 3); // 显示3行
    lv_roller_set_selected(roller_num, 5, LV_ANIM_OFF); // 默认选择
    lv_obj_set_width(roller_num, 90);
    lv_obj_set_style_text_font(roller_num, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_num, LV_ALIGN_BOTTOM_LEFT, 15, -53);
    lv_obj_add_event_cb(roller_num, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"数字"roller 的确认键
    lv_obj_t *btn_num_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_num_ok, LV_ALIGN_BOTTOM_LEFT, 15, -10); // 位置
    lv_obj_set_width(btn_num_ok, 90); // 宽度
    lv_obj_add_event_cb(btn_num_ok, btn_num_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_num_ok = lv_label_create(btn_num_ok);
    lv_label_set_text(label_num_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_num_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_num_ok);

    // 创建"小写字母"roller
    const char * opts_letter_low = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz";

    roller_letter_low = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_letter_low, &style, 0);
    lv_obj_set_style_bg_opa(roller_letter_low, LV_OPA_50, LV_PART_SELECTED); // 设置选中项的透明度
    lv_roller_set_options(roller_letter_low, opts_letter_low, LV_ROLLER_MODE_INFINITE); // 循环滚动模式
    lv_roller_set_visible_row_count(roller_letter_low, 3);
    lv_roller_set_selected(roller_letter_low, 15, LV_ANIM_OFF); // 
    lv_obj_set_width(roller_letter_low, 90);
    lv_obj_set_style_text_font(roller_letter_low, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_letter_low, LV_ALIGN_BOTTOM_LEFT, 115, -53);
    lv_obj_add_event_cb(roller_letter_low, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"小写字母"roller的确认键
    lv_obj_t *btn_letter_low_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_letter_low_ok, LV_ALIGN_BOTTOM_LEFT, 115, -10);
    lv_obj_set_width(btn_letter_low_ok, 90); // 宽度
    lv_obj_add_event_cb(btn_letter_low_ok, btn_letter_low_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_letter_low_ok = lv_label_create(btn_letter_low_ok);
    lv_label_set_text(label_letter_low_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_letter_low_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_letter_low_ok);

    // 创建"大写字母"roller
    const char * opts_letter_up = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX\nY\nZ";

    roller_letter_up = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_letter_up, &style, 0);
    lv_obj_set_style_bg_opa(roller_letter_up, LV_OPA_50, LV_PART_SELECTED); // 设置选中项的透明度
    lv_roller_set_options(roller_letter_up, opts_letter_up, LV_ROLLER_MODE_INFINITE); // 循环滚动模式
    lv_roller_set_visible_row_count(roller_letter_up, 3);
    lv_roller_set_selected(roller_letter_up, 15, LV_ANIM_OFF);
    lv_obj_set_width(roller_letter_up, 90);
    lv_obj_set_style_text_font(roller_letter_up, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_letter_up, LV_ALIGN_BOTTOM_LEFT, 215, -53);
    lv_obj_add_event_cb(roller_letter_up, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"大写字母"roller的确认键
    lv_obj_t *btn_letter_up_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_letter_up_ok, LV_ALIGN_BOTTOM_LEFT, 215, -10);
    lv_obj_set_width(btn_letter_up_ok, 90); 
    lv_obj_add_event_cb(btn_letter_up_ok, btn_letter_up_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_letter_up_ok = lv_label_create(btn_letter_up_ok);
    lv_label_set_text(label_letter_up_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_letter_up_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_letter_up_ok);

}

lv_obj_t * date_label;
lv_obj_t * time_label;

time_t now;
struct tm timeinfo;

// 更新时间函数
void value_update_cb(lv_timer_t * timer)
{
    // 更新日期 星期 时分秒
    time(&now);
    localtime_r(&now, &timeinfo);
    lv_label_set_text_fmt(time_label, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    lv_label_set_text_fmt(date_label, "%d年%02d月%02d日", timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday);
}

// 获得日期时间 任务函数
static void get_time_task(void *pvParameters)
{
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("cn.pool.ntp.org");
    esp_netif_sntp_init(&config);
    // wait for time to be set
    int retry = 0;
    // const int retry_count = 6;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d)", retry++);
    }

    esp_netif_sntp_deinit();
    // 设置时区
    setenv("TZ", "CST-8", 1); 
    tzset();
    // 获取系统时间
    time(&now);
    localtime_r(&now, &timeinfo);

    lvgl_port_lock(0);
    lv_obj_del(main_text_label); // 删除主页的欢迎语 
    // 显示年月日
    date_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(date_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xffffff), 0); 
    lv_label_set_text_fmt(date_label, "%d年%02d月%02d日", timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday);
    lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 10, 5);

    // 显示时间  小时:分钟:秒钟
    time_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(time_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0); 
    lv_label_set_text_fmt(time_label, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    lv_obj_align_to(time_label, date_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lvgl_port_unlock();

    xEventGroupSetBits(s_wifi_event_group, WIFI_GET_SNTP_BIT);

    lv_timer_create(value_update_cb, 1000, NULL);  // 创建一个lv_timer 每秒更新一次时间
    
    vTaskDelete(NULL);
}

// 网络连接 事件处理函数
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_START_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_event_handler_instance_t instance_any_id;
esp_event_handler_instance_t instance_got_ip;
esp_netif_t *sta_netif = NULL;

static esp_err_t wifi_ensure_sta_started(void)
{
    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    wifi_mode_t mode;
    ret = esp_wifi_get_mode(&mode);
    bool wifi_inited = ret != ESP_ERR_WIFI_NOT_INIT;
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
        return ret;
    }

    if (!wifi_inited) {
        if (sta_netif == NULL) {
            sta_netif = esp_netif_create_default_wifi_sta();
            if (sta_netif == NULL) {
                return ESP_FAIL;
            }
        }

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
    }

    if (!s_wifi_handlers_registered) {
        ret = esp_event_handler_instance_register(WIFI_EVENT,
                                                  ESP_EVENT_ANY_ID,
                                                  &event_handler,
                                                  NULL,
                                                  &instance_any_id);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }

        ret = esp_event_handler_instance_register(IP_EVENT,
                                                  IP_EVENT_STA_GOT_IP,
                                                  &event_handler,
                                                  NULL,
                                                  &instance_got_ip);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        s_wifi_handlers_registered = true;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        return ret;
    }
    s_wifi_driver_started = true;
    return ESP_OK;
}

// 扫描附近wifi
static esp_err_t wifi_scan(wifi_ap_record_t ap_info[], uint16_t *ap_number)
{
    uint16_t ap_count = 0;
    esp_err_t ret = wifi_ensure_sta_started();
    if (ret != ESP_OK) {
        return ret;
    }

    memset(ap_info, 0, *ap_number * sizeof(wifi_ap_record_t));

    ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi scan failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", *ap_number);
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), TAG, "get AP num failed");  // 获取扫描到的wifi数量
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(ap_number, ap_info), TAG, "get AP records failed"); // 获取真实的获取到wifi数量和信息
    ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, *ap_number);
    return ESP_OK;
}

// 清除wifi初始化内容
static void wifiset_deinit(void)
{
    // WiFi may be shared with random image and time sync. Keep the driver alive.
}

// WIFI连接任务
static void wifi_connect(void *arg)
{
    wifi_account_t wifi_account;

    while (true)
    {
        // 如果收到wifi账号队列消息
        if(xQueueReceive(xQueueWifiAccount, &wifi_account, portMAX_DELAY))
        {
            if (wifi_account.back_flag == 1){  // 退出任务标志
                if (xQueueWifiAccount != NULL) {
                    vQueueDelete(xQueueWifiAccount);
                    xQueueWifiAccount = NULL;
                }
                break; // 跳出while循环
            }
            
            wifi_config_t wifi_config = {
                .sta = {
                    .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
                    .sae_h2e_identifier = "",
                    },
            };
            snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", wifi_account.wifi_ssid);
            snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", wifi_account.wifi_password);
            wifi_config.sta.threshold.authmode = wifi_account.wifi_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;

            esp_err_t ret = wifi_ensure_sta_started();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(ret));
                if (label_wifi_connect != NULL) {
                    lvgl_port_lock(0);
                    lv_label_set_text(label_wifi_connect, "WLAN 启动失败");
                    lvgl_port_unlock();
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (wifi_connect_page != NULL) {
                    lvgl_port_lock(0);
                    lv_obj_del(wifi_connect_page);
                    wifi_connect_page = NULL;
                    lvgl_port_unlock();
                }
                continue;
            }

            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
            s_wifi_retry_num = 0;
            ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "wifi config failed: %s", esp_err_to_name(ret));
                if (label_wifi_connect != NULL) {
                    lvgl_port_lock(0);
                    lv_label_set_text(label_wifi_connect, "WLAN 配置失败");
                    lvgl_port_unlock();
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (wifi_connect_page != NULL) {
                    lvgl_port_lock(0);
                    lv_obj_del(wifi_connect_page);
                    wifi_connect_page = NULL;
                    lvgl_port_unlock();
                }
                continue;
            }
            esp_wifi_disconnect();
            esp_wifi_connect();
            /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
            * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(20000));

            /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
            * happened. */
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
                wifi_history_upsert(wifi_account.wifi_ssid, wifi_account.wifi_password, wifi_account.authmode);
                wifi_history_save_all();
                wifi_save_random_image_config(wifi_account.wifi_ssid, wifi_account.wifi_password);
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接成功");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                wifi_connect_page = NULL;
                lv_obj_del(wifi_scan_page); // 删除此页面
                wifi_scan_page = NULL;
                lvgl_port_unlock();
                vQueueDelete(xQueueWifiAccount); // 删除队列
                xQueueWifiAccount = NULL;
                icon_flag = 0; // 标记回到主界面
                xTaskCreatePinnedToCore(get_time_task, "get_time_task", 2 * 1024, NULL, 5, NULL, 0);  // 创建获取时间任务
                break; // 跳出while循环删除任务
            } else if (bits & WIFI_FAIL_BIT) {
                ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接失败");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                wifi_connect_page = NULL;
                lvgl_port_unlock();
                xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT); // 清除此事件标志位

            } else {
                ESP_LOGE(TAG, "UNEXPECTED EVENT");
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接异常");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                wifi_connect_page = NULL;
                lv_obj_del(wifi_scan_page); // 删除此页面
                wifi_scan_page = NULL;
                lvgl_port_unlock();
            }
        }
    }
    vTaskDelete(NULL);
}

// 返回主界面按钮事件处理函数
static void btn_backmain_cb(lv_event_t * e)
{
    ESP_LOGI(TAG, "btn_backmain Clicked");

    // // 删除wifi扫描界面
    lvgl_port_lock(0);
    if (wifi_scan_page != NULL) {
        lv_obj_del(wifi_scan_page);
        wifi_scan_page = NULL;
    }
    lvgl_port_unlock();

    // 通知wifi_connect任务退出
    if (xQueueWifiAccount != NULL) {
        wifi_account_t wifi_account = {0};
        wifi_account.back_flag = 1;
        xQueueSend(xQueueWifiAccount, &wifi_account, pdMS_TO_TICKS(100));
    }
    
    wifiset_deinit();// 保留WiFi连接，仅清理WLAN页面状态
    icon_flag = 0;
}

// wifi连接
void app_wifi_connect(void *arg)
{
    vTaskDelay(200 / portTICK_PERIOD_MS);
    // 扫描WLAN信息
    uint16_t ap_number = DEFAULT_SCAN_LIST_SIZE;
    wifi_history_load();
    esp_err_t scan_ret = wifi_scan(s_wifi_ap_records, &ap_number); // 扫描附近wifi
    if (scan_ret != ESP_OK) {
        lvgl_port_lock(0);
        lv_label_set_text_fmt(label_wifi_scan, "WLAN扫描失败\n%s", esp_err_to_name(scan_ret));
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(1200));
        lvgl_port_lock(0);
        if (wifi_scan_page != NULL) {
            lv_obj_del(wifi_scan_page);
            wifi_scan_page = NULL;
        }
        lvgl_port_unlock();
        icon_flag = 0;
        vTaskDelete(NULL);
        return;
    }
    s_wifi_ap_count = ap_number;

    lvgl_port_lock(0);
    // 修改标题
    lv_label_set_text_fmt(label_wifi_scan, "%d WLAN", ap_number);

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(obj_scan_title);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_back, 60, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_backmain_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    // 创建wifi信息列表
    wifi_list = lv_list_create(wifi_scan_page);
    lv_obj_set_size(wifi_list, 320, 200);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_style_text_font(wifi_list, &font_alipuhui20, 0);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF); // 隐藏wifi_list滚动条
    // 显示wifi信息
    lv_obj_t * btn;
    for (int i = 0; i < ap_number; i++) {
        ESP_LOGI(TAG, "SSID \t\t%s", s_wifi_ap_records[i].ssid);  // 终端输出wifi名称
        ESP_LOGI(TAG, "RSSI \t\t%d", s_wifi_ap_records[i].rssi);  // 终端输出wifi信号质量
        // 添加wifi列表
        btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, (const char *)s_wifi_ap_records[i].ssid);
        lv_obj_add_event_cb(btn, list_btn_cb, LV_EVENT_CLICKED, NULL); // 添加点击回调函数
    }
    lvgl_port_unlock();
    
    // 创建wifi连接任务
    xQueueWifiAccount = xQueueCreate(2, sizeof(wifi_account_t));
    xTaskCreatePinnedToCore(wifi_connect, "wifi_connect", 4 * 1024, NULL, 5, NULL, 1);  // 创建wifi连接任务
    vTaskDelete(NULL);
}

// 进入WIFI设置应用
static void wifiset_event_handler(lv_event_t * e)
{  
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xFFFFFF));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    wifi_scan_page = lv_obj_create(lv_scr_act());
    lv_obj_add_style(wifi_scan_page, &style, 0);

    // 创建标题背景
    obj_scan_title = lv_obj_create(wifi_scan_page);
    lv_obj_set_size(obj_scan_title, 320, 40);
    lv_obj_set_style_pad_all(obj_scan_title, 0, 0);  // 设置间隙
    lv_obj_align(obj_scan_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(obj_scan_title, lv_color_hex(0x008b8b), 0);
    // 显示扫描情况
    label_wifi_scan = lv_label_create(obj_scan_title);
    lv_label_set_text(label_wifi_scan, "WLAN扫描中...");
    lv_obj_set_style_text_color(label_wifi_scan, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label_wifi_scan, &font_alipuhui20, 0);
    lv_obj_align(label_wifi_scan, LV_ALIGN_CENTER, 0, 0);

    icon_flag = 5; // 标记已经进入第5个应用

    xTaskCreatePinnedToCore(app_wifi_connect, "app_wifi_connect", 4*1024, NULL, 5, NULL, 0);
}


/******************************** 第6个图标 蓝牙设置 应用程序***********************************************************************************/
lv_obj_t * ble_label;
lv_obj_t * btn_ble_back;

// 返回主界面按钮事件处理函数
static void btn_ble_back_cb(lv_event_t * e)
{
    bt_hid_end();
    if (icon_in_obj != NULL) {
        lv_obj_del(icon_in_obj);
        icon_in_obj = NULL;
    }
    icon_flag = 0;
}

// 进入蓝牙设置应用
static void btset_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);
    
    // 创建标题背景
    lv_obj_t *ble_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(ble_title, 320, 40);
    lv_obj_set_style_pad_all(ble_title, 0, 0);  // 设置间隙
    lv_obj_align(ble_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(ble_title, lv_color_hex(0xb87fa8), 0);
    // 显示标题
    ble_label = lv_label_create(ble_title);
    lv_label_set_text(ble_label, "蓝牙控制器");
    lv_obj_set_style_text_color(ble_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(ble_label, &font_alipuhui20, 0);
    lv_obj_align(ble_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_ble_back = lv_btn_create(ble_title);
    lv_obj_align(btn_ble_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_ble_back, 60, 30);
    lv_obj_set_style_border_width(btn_ble_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_ble_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_ble_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_ble_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_ble_back, btn_ble_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_ble_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    esp_err_t ret = app_hid_ctrl();
    if (ret == ESP_OK) {
        lv_label_set_text(ble_label, "BLE HID Ready");
    } else {
        lv_label_set_text_fmt(ble_label, "BLE失败:%s", esp_err_to_name(ret));
    }

    icon_flag = 6; // 标记已经进入第6个应用
}


/******************************** 录音机 应用程序 *****************************************************************************/
#define RECORDER_SAMPLE_RATE       16000
#define RECORDER_CHANNELS          1
#define RECORDER_BITS_PER_SAMPLE   16
#define RECORDER_SRC_CHANNELS      ADC_I2S_CHANNEL
#define RECORDER_FRAMES_PER_CHUNK  512
#define RECORDER_ICON_FLAG         8

static lv_obj_t *s_recorder_status_label;
static lv_obj_t *s_recorder_time_label;
static lv_obj_t *s_recorder_file_label;
static lv_obj_t *s_recorder_level_bar;
static lv_obj_t *s_recorder_button_label;
static TaskHandle_t s_recorder_task_handle;
static volatile bool s_recorder_active;
static volatile bool s_recorder_exit_requested;

static void recorder_write_le16(FILE *f, uint16_t value)
{
    uint8_t b[2] = {
        (uint8_t)(value & 0xff),
        (uint8_t)((value >> 8) & 0xff),
    };
    fwrite(b, 1, sizeof(b), f);
}

static void recorder_write_le32(FILE *f, uint32_t value)
{
    uint8_t b[4] = {
        (uint8_t)(value & 0xff),
        (uint8_t)((value >> 8) & 0xff),
        (uint8_t)((value >> 16) & 0xff),
        (uint8_t)((value >> 24) & 0xff),
    };
    fwrite(b, 1, sizeof(b), f);
}

static void recorder_write_wav_header(FILE *f, uint32_t data_bytes)
{
    uint32_t byte_rate = RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * RECORDER_BITS_PER_SAMPLE / 8;
    uint16_t block_align = RECORDER_CHANNELS * RECORDER_BITS_PER_SAMPLE / 8;

    fwrite("RIFF", 1, 4, f);
    recorder_write_le32(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    recorder_write_le32(f, 16);
    recorder_write_le16(f, 1);
    recorder_write_le16(f, RECORDER_CHANNELS);
    recorder_write_le32(f, RECORDER_SAMPLE_RATE);
    recorder_write_le32(f, byte_rate);
    recorder_write_le16(f, block_align);
    recorder_write_le16(f, RECORDER_BITS_PER_SAMPLE);
    fwrite("data", 1, 4, f);
    recorder_write_le32(f, data_bytes);
}

static void recorder_set_label_text(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }
    lvgl_port_lock(0);
    if (label != NULL) {
        lv_label_set_text(label, text);
    }
    lvgl_port_unlock();
}

static void recorder_update_ui(uint32_t samples_written, int peak)
{
    uint32_t seconds = samples_written / RECORDER_SAMPLE_RATE;
    char time_text[24];
    snprintf(time_text, sizeof(time_text), "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));

    lvgl_port_lock(0);
    if (s_recorder_time_label != NULL) {
        lv_label_set_text(s_recorder_time_label, time_text);
    }
    if (s_recorder_level_bar != NULL) {
        int value = peak * 100 / 32767;
        if (value > 100) {
            value = 100;
        }
        lv_bar_set_value(s_recorder_level_bar, value, LV_ANIM_OFF);
    }
    lvgl_port_unlock();
}

static void recorder_set_button_text(const char *text)
{
    recorder_set_label_text(s_recorder_button_label, text);
}

static void recorder_stop_and_exit_ui(void)
{
    lvgl_port_lock(0);
    if (icon_in_obj != NULL) {
        lv_obj_del(icon_in_obj);
        icon_in_obj = NULL;
    }
    lvgl_port_unlock();
    s_recorder_status_label = NULL;
    s_recorder_time_label = NULL;
    s_recorder_file_label = NULL;
    s_recorder_level_bar = NULL;
    s_recorder_button_label = NULL;
    icon_flag = 0;
}

static void recorder_task(void *arg)
{
    int16_t *raw = heap_caps_malloc(RECORDER_FRAMES_PER_CHUNK * RECORDER_SRC_CHANNELS * sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    int16_t *mono = heap_caps_malloc(RECORDER_FRAMES_PER_CHUNK * sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    FILE *f = NULL;
    uint32_t data_bytes = 0;
    uint32_t samples_written = 0;
    char path[128];
    esp_err_t ret = ESP_OK;

    if (raw == NULL || mono == NULL) {
        recorder_set_label_text(s_recorder_status_label, "录音缓冲不足");
        goto done;
    }

    if (g_audio_player_ready) {
        audio_player_stop();
    }

    ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        recorder_set_label_text(s_recorder_status_label, "TF卡不可用");
        goto done;
    }

    mkdir(SD_MOUNT_POINT "/szpi", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/recordings", 0775);
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/szpi/recordings/rec_%lld.wav", esp_timer_get_time() / 1000);

    f = fopen(path, "wb+");
    if (f == NULL) {
        recorder_set_label_text(s_recorder_status_label, "创建文件失败");
        goto done;
    }

    recorder_write_wav_header(f, 0);
    recorder_set_label_text(s_recorder_status_label, "录音中...");
    recorder_set_label_text(s_recorder_file_label, path);
    recorder_set_button_text("停止");

    while (s_recorder_active && icon_flag == RECORDER_ICON_FLAG) {
        ret = bsp_get_feed_data(true, raw, RECORDER_FRAMES_PER_CHUNK * RECORDER_SRC_CHANNELS * sizeof(int16_t));
        if (ret != ESP_OK) {
            recorder_set_label_text(s_recorder_status_label, "麦克风读取失败");
            break;
        }

        int peak = 0;
        for (int i = 0; i < RECORDER_FRAMES_PER_CHUNK; i++) {
            int16_t sample = raw[i * RECORDER_SRC_CHANNELS + 1];
            mono[i] = sample;
            int abs_sample = sample == INT16_MIN ? 32767 : abs(sample);
            if (abs_sample > peak) {
                peak = abs_sample;
            }
        }

        size_t written = fwrite(mono, sizeof(int16_t), RECORDER_FRAMES_PER_CHUNK, f);
        if (written != RECORDER_FRAMES_PER_CHUNK) {
            recorder_set_label_text(s_recorder_status_label, "写入失败");
            break;
        }

        data_bytes += RECORDER_FRAMES_PER_CHUNK * sizeof(int16_t);
        samples_written += RECORDER_FRAMES_PER_CHUNK;
        recorder_update_ui(samples_written, peak);
    }

    if (f != NULL) {
        fflush(f);
        fseek(f, 0, SEEK_SET);
        recorder_write_wav_header(f, data_bytes);
        fclose(f);
        f = NULL;
    }

    if (data_bytes > 0) {
        recorder_set_label_text(s_recorder_status_label, "已保存");
        recorder_set_label_text(s_recorder_file_label, path);
    }

done:
    if (f != NULL) {
        fclose(f);
    }
    if (raw != NULL) {
        heap_caps_free(raw);
    }
    if (mono != NULL) {
        heap_caps_free(mono);
    }
    s_recorder_active = false;
    s_recorder_task_handle = NULL;
    recorder_set_button_text("开始");
    if (s_recorder_exit_requested) {
        s_recorder_exit_requested = false;
        recorder_stop_and_exit_ui();
    }
    vTaskDelete(NULL);
}

static void recorder_toggle_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_recorder_active) {
        s_recorder_active = false;
        recorder_set_label_text(s_recorder_status_label, "停止中...");
        return;
    }

    if (s_recorder_task_handle != NULL) {
        return;
    }

    s_recorder_exit_requested = false;
    s_recorder_active = true;
    recorder_update_ui(0, 0);
    recorder_set_label_text(s_recorder_file_label, "");
    BaseType_t ok = xTaskCreatePinnedToCore(recorder_task, "recorder", 5 * 1024, NULL, 5, &s_recorder_task_handle, 1);
    if (ok != pdPASS) {
        s_recorder_active = false;
        s_recorder_task_handle = NULL;
        recorder_set_label_text(s_recorder_status_label, "启动失败");
    }
}

static void recorder_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_recorder_active) {
        s_recorder_exit_requested = true;
        s_recorder_active = false;
        recorder_set_label_text(s_recorder_status_label, "停止中...");
        return;
    }

    recorder_stop_and_exit_ui();
}

static void recorder_event_handler(lv_event_t *e)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);
    lv_style_set_height(&style, 240);

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    lv_obj_t *title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(title, 320, 40);
    lv_obj_set_style_pad_all(title, 0, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(title, lv_color_hex(0x2c8dbf), 0);

    lv_obj_t *title_label = lv_label_create(title);
    lv_label_set_text(title_label, "录音机");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title_label, &font_alipuhui20, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_back = lv_btn_create(title);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_back, 60, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0);
    lv_obj_set_style_pad_all(btn_back, 0, 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, recorder_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0);
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    s_recorder_time_label = lv_label_create(icon_in_obj);
    lv_label_set_text(s_recorder_time_label, "00:00");
    lv_obj_set_style_text_font(s_recorder_time_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_recorder_time_label, lv_color_hex(0x1f2933), 0);
    lv_obj_align(s_recorder_time_label, LV_ALIGN_TOP_MID, 0, 62);

    s_recorder_status_label = lv_label_create(icon_in_obj);
    lv_label_set_text(s_recorder_status_label, "Ready");
    lv_obj_set_style_text_font(s_recorder_status_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_recorder_status_label, lv_color_hex(0x3b4856), 0);
    lv_obj_align(s_recorder_status_label, LV_ALIGN_TOP_MID, 0, 98);

    s_recorder_level_bar = lv_bar_create(icon_in_obj);
    lv_obj_set_size(s_recorder_level_bar, 220, 12);
    lv_bar_set_range(s_recorder_level_bar, 0, 100);
    lv_bar_set_value(s_recorder_level_bar, 0, LV_ANIM_OFF);
    lv_obj_align(s_recorder_level_bar, LV_ALIGN_TOP_MID, 0, 128);

    lv_obj_t *btn_record = lv_btn_create(icon_in_obj);
    lv_obj_set_size(btn_record, 118, 46);
    lv_obj_align(btn_record, LV_ALIGN_TOP_MID, 0, 154);
    lv_obj_set_style_bg_color(btn_record, lv_color_hex(0xd64b4b), 0);
    lv_obj_set_style_radius(btn_record, 8, 0);
    lv_obj_add_event_cb(btn_record, recorder_toggle_cb, LV_EVENT_CLICKED, NULL);

    s_recorder_button_label = lv_label_create(btn_record);
    lv_label_set_text(s_recorder_button_label, "开始");
    lv_obj_set_style_text_font(s_recorder_button_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(s_recorder_button_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(s_recorder_button_label);

    s_recorder_file_label = lv_label_create(icon_in_obj);
    lv_label_set_text(s_recorder_file_label, "");
    lv_label_set_long_mode(s_recorder_file_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_recorder_file_label, 290);
    lv_obj_set_style_text_font(s_recorder_file_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_recorder_file_label, lv_color_hex(0x627181), 0);
    lv_obj_align(s_recorder_file_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    s_recorder_active = false;
    s_recorder_exit_requested = false;
    icon_flag = RECORDER_ICON_FLAG;
}


/******************************** 主界面  ******************************/

LV_IMG_DECLARE(img_att_icon);
LV_IMG_DECLARE(img_music_icon);
LV_IMG_DECLARE(img_sd_icon);
LV_IMG_DECLARE(img_camera_icon);
LV_IMG_DECLARE(img_wifiset_icon);
LV_IMG_DECLARE(img_btset_icon);

#define HOME_ITEMS_PER_PAGE 4

typedef enum {
    HOME_ICON_SYMBOL,
    HOME_ICON_IMAGE,
} home_icon_type_t;

typedef struct {
    const char *title;
    const char *subtitle;
    uint32_t color_hex;
    home_icon_type_t icon_type;
    const void *icon_src;
    void (*event_cb)(lv_event_t *e);
} home_app_item_t;

static lv_obj_t *s_home_icon_layer;
static lv_obj_t *s_home_page_label;
static uint8_t s_home_page_index;
static bool s_home_styles_ready;
static bool s_home_launch_locked;
static bool s_home_button_task_started;
static lv_style_t s_home_bg_style;
static lv_style_t s_home_button_style;
static lv_style_t s_home_button_pressed_style;

static const home_app_item_t s_home_apps[] = {
    { "随机图", "API",    0x2fa66a, HOME_ICON_SYMBOL, LV_SYMBOL_IMAGE,   random_image_app_event_handler },
    { "姿态",   "QMI8658", 0xf06d2f, HOME_ICON_IMAGE,  &img_att_icon,     att_event_handler },
    { "音乐",   "Player", 0x4c7bd9, HOME_ICON_IMAGE,  &img_music_icon,   music_event_handler },
    { "TF卡",   "Files",  0x009688, HOME_ICON_IMAGE,  &img_sd_icon,      sdcard_event_handler },
    { "录音",   "WAV",    0x2c8dbf, HOME_ICON_SYMBOL, LV_SYMBOL_AUDIO,   recorder_event_handler },
    { "摄像头", "Camera", 0xd8a318, HOME_ICON_IMAGE,  &img_camera_icon,  camera_event_handler },
    { "WLAN",   "Scan",   0xc85a5a, HOME_ICON_IMAGE,  &img_wifiset_icon, wifiset_event_handler },
    { "蓝牙",   "HID",    0xa36bb8, HOME_ICON_IMAGE,  &img_btset_icon,   btset_event_handler },
};

static uint8_t home_page_count(void)
{
    return (sizeof(s_home_apps) / sizeof(s_home_apps[0]) + HOME_ITEMS_PER_PAGE - 1) / HOME_ITEMS_PER_PAGE;
}

static void home_styles_init(void)
{
    if (s_home_styles_ready) {
        return;
    }

    lv_style_init(&s_home_bg_style);
    lv_style_set_radius(&s_home_bg_style, 0);
    lv_style_set_bg_opa(&s_home_bg_style, LV_OPA_COVER);
    lv_style_set_bg_color(&s_home_bg_style, lv_color_hex(0x111820));
    lv_style_set_bg_grad_color(&s_home_bg_style, lv_color_hex(0x24313a));
    lv_style_set_bg_grad_dir(&s_home_bg_style, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&s_home_bg_style, 0);
    lv_style_set_pad_all(&s_home_bg_style, 0);
    lv_style_set_width(&s_home_bg_style, 320);
    lv_style_set_height(&s_home_bg_style, 240);

    lv_style_init(&s_home_button_style);
    lv_style_set_radius(&s_home_button_style, 10);
    lv_style_set_bg_opa(&s_home_button_style, LV_OPA_COVER);
    lv_style_set_border_width(&s_home_button_style, 0);
    lv_style_set_shadow_width(&s_home_button_style, 0);
    lv_style_set_pad_all(&s_home_button_style, 0);
    lv_style_set_text_color(&s_home_button_style, lv_color_hex(0xffffff));

    lv_style_init(&s_home_button_pressed_style);
    lv_style_set_bg_opa(&s_home_button_pressed_style, LV_OPA_80);

    s_home_styles_ready = true;
}

static void home_render_page(void);

static void home_unlock_launch_cb(lv_timer_t *timer)
{
    s_home_launch_locked = false;
    lv_timer_del(timer);
}

static void home_app_clicked_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_home_launch_locked || icon_flag != 0) {
        return;
    }

    const home_app_item_t *app = (const home_app_item_t *)lv_event_get_user_data(e);
    if (app == NULL || app->event_cb == NULL) {
        return;
    }

    s_home_launch_locked = true;
    lv_timer_t *unlock_timer = lv_timer_create(home_unlock_launch_cb, 1200, NULL);
    lv_timer_set_repeat_count(unlock_timer, 1);
    app->event_cb(e);
}

static void home_switch_page(int delta)
{
    int page_count = (int)home_page_count();
    if (page_count <= 1) {
        return;
    }

    int next = ((int)s_home_page_index + delta) % page_count;
    if (next < 0) {
        next += page_count;
    }

    if (next == s_home_page_index) {
        return;
    }

    s_home_page_index = (uint8_t)next;
    home_render_page();
}

static void home_button_next_page_cb(void *arg)
{
    if (icon_flag == 0 && s_home_icon_layer != NULL) {
        home_switch_page(1);
    }
}

static void home_button_task(void *arg)
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
            int64_t held_us = now_us - press_start_us;
            if (held_us > 30000 && held_us < 2000000 && icon_flag == 0) {
                lv_async_call(home_button_next_page_cb, NULL);
            }
            press_start_us = 0;
        }
        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void home_ensure_button_task(void)
{
    if (s_home_button_task_started) {
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(home_button_task, "home_button", 4 * 1024, NULL, 4, NULL, 1);
    if (ok == pdPASS) {
        s_home_button_task_started = true;
    } else {
        ESP_LOGE(TAG, "create home_button task failed");
    }
}

static void home_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        home_switch_page(1);
    } else if (dir == LV_DIR_RIGHT) {
        home_switch_page(-1);
    }
}

static void home_create_icon(const home_app_item_t *app, uint8_t slot)
{
    static const lv_coord_t x_pos[2] = { 8, 164 };
    static const lv_coord_t y_pos[2] = { 50, 136 };
    uint8_t col = slot % 2;
    uint8_t row = slot / 2;

    lv_obj_t *btn = lv_btn_create(s_home_icon_layer);
    lv_obj_add_style(btn, &s_home_button_style, 0);
    lv_obj_add_style(btn, &s_home_button_pressed_style, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(app->color_hex), 0);
    lv_obj_set_size(btn, 148, 76);
    lv_obj_set_pos(btn, x_pos[col], y_pos[row] - 45);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(btn, home_app_clicked_cb, LV_EVENT_CLICKED, (void *)app);
    lv_obj_add_event_cb(btn, home_gesture_cb, LV_EVENT_GESTURE, NULL);

    if (app->icon_type == HOME_ICON_IMAGE) {
        lv_obj_t *img = lv_img_create(btn);
        lv_img_set_src(img, app->icon_src);
        lv_img_set_zoom(img, 192);
        lv_obj_align(img, LV_ALIGN_LEFT_MID, 7, 0);
    } else {
        lv_obj_t *symbol = lv_label_create(btn);
        lv_label_set_text(symbol, (const char *)app->icon_src);
        lv_obj_set_style_text_font(symbol, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(symbol, lv_color_hex(0xffffff), 0);
        lv_obj_align(symbol, LV_ALIGN_LEFT_MID, 24, 0);
    }

    lv_obj_t *title = lv_label_create(btn);
    lv_label_set_text(title, app->title);
    lv_obj_set_style_text_font(title, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(title, 68);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 76, -12);

    lv_obj_t *subtitle = lv_label_create(btn);
    lv_label_set_text(subtitle, app->subtitle);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xf2f5f8), 0);
    lv_obj_set_style_text_opa(subtitle, LV_OPA_80, 0);
    lv_obj_set_width(subtitle, 68);
    lv_obj_align(subtitle, LV_ALIGN_LEFT_MID, 76, 15);
}

static void home_render_page(void)
{
    if (s_home_icon_layer == NULL) {
        return;
    }

    lv_obj_clean(s_home_icon_layer);

    uint8_t first = s_home_page_index * HOME_ITEMS_PER_PAGE;
    uint8_t app_count = sizeof(s_home_apps) / sizeof(s_home_apps[0]);
    for (uint8_t slot = 0; slot < HOME_ITEMS_PER_PAGE; slot++) {
        uint8_t app_index = first + slot;
        if (app_index >= app_count) {
            break;
        }
        home_create_icon(&s_home_apps[app_index], slot);
    }

    if (s_home_page_label != NULL) {
        lv_label_set_text_fmt(s_home_page_label, "%d/%d", s_home_page_index + 1, home_page_count());
    }
}

void lv_main_page(void)
{
    lvgl_port_lock(0);

    home_styles_init();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);

    main_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(main_obj, &s_home_bg_style, 0);
    lv_obj_clear_flag(main_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(main_obj, home_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *top_line = lv_obj_create(main_obj);
    lv_obj_set_size(top_line, 320, 1);
    lv_obj_set_pos(top_line, 0, 39);
    lv_obj_set_style_pad_all(top_line, 0, 0);
    lv_obj_set_style_border_width(top_line, 0, 0);
    lv_obj_set_style_bg_color(top_line, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(top_line, LV_OPA_20, 0);

    // 显示右上角符号
    lv_obj_t * sylbom_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(sylbom_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sylbom_label, lv_color_hex(0xffffff), 0);
    lv_label_set_text(sylbom_label, LV_SYMBOL_BLUETOOTH" "LV_SYMBOL_WIFI);
    lv_obj_align_to(sylbom_label, main_obj, LV_ALIGN_TOP_RIGHT, -10, 10);

    // 显示左上角欢迎语
    main_text_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(main_text_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(main_text_label, lv_color_hex(0xffffff), 0);
    lv_label_set_long_mode(main_text_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(main_text_label, 215);
    lv_label_set_text(main_text_label, "SZPI-S3 掌机");
    lv_obj_align_to(main_text_label, main_obj, LV_ALIGN_TOP_LEFT, 10, 6);

    s_home_icon_layer = lv_obj_create(main_obj);
    lv_obj_set_size(s_home_icon_layer, 320, 176);
    lv_obj_set_pos(s_home_icon_layer, 0, 45);
    lv_obj_set_style_pad_all(s_home_icon_layer, 0, 0);
    lv_obj_set_style_border_width(s_home_icon_layer, 0, 0);
    lv_obj_set_style_radius(s_home_icon_layer, 0, 0);
    lv_obj_set_style_bg_opa(s_home_icon_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_home_icon_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_home_icon_layer, home_gesture_cb, LV_EVENT_GESTURE, NULL);

    s_home_page_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(s_home_page_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_home_page_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_opa(s_home_page_label, LV_OPA_70, 0);
    lv_obj_align(s_home_page_label, LV_ALIGN_BOTTOM_MID, 0, -7);

    s_home_page_index = 0;
    home_render_page();
    home_ensure_button_task();

    icon_flag = 0;
    lvgl_port_unlock();
}

