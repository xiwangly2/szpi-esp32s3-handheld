/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble_hidd_demo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_hidd_prf_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "hid_dev.h"

/**
 * Brief:
 * This example Implemented BLE HID device profile related functions, in which the HID device
 * has 4 Reports (1 is mouse, 2 is keyboard and LED, 3 is Consumer Devices, 4 is Vendor devices).
 * Users can choose different reports according to their own application scenarios.
 * BLE HID profile inheritance and USB HID class.
 */

/**
 * Note:
 * 1. Win10 does not support vendor report , So SUPPORT_REPORT_VENDOR is always set to FALSE, it defines in hidd_le_prf_int.h
 * 2. Update connection parameters are not allowed during iPhone HID encryption, slave turns
 * off the ability to automatically update connection parameters during encryption.
 * 3. After our HID device is connected, the iPhones write 1 to the Report Characteristic Configuration Descriptor,
 * even if the HID encryption is not completed. This should actually be written 1 after the HID encryption is completed.
 * we modify the permissions of the Report Characteristic Configuration Descriptor to `ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED`.
 * if you got `GATT_INSUF_ENCRYPTION` error, please ignore.
 */

#define HID_DEMO_TAG "HID_DEMO"


static uint16_t hid_conn_id = 0;
static bool sec_conn = false;
static bool s_bt_hid_started;
static bool s_bt_classic_released;
static bool s_bt_shutting_down;
static bool s_hid_connected;
static bool s_hid_advertising;
static bool s_hid_have_remote;
static esp_bd_addr_t s_hid_remote_bda;

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);

#define HIDD_DEVICE_NAME            "SZPI-HID"
static uint8_t hidd_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x03c0,       //HID Generic,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x30,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};


static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch(event) {
        case ESP_HIDD_EVENT_REG_FINISH: {
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                //esp_bd_addr_t rand_addr = {0x04,0x11,0x11,0x11,0x11,0x05};
                esp_ble_gap_set_device_name(HIDD_DEVICE_NAME);
                esp_ble_gap_config_adv_data(&hidd_adv_data);

            }
            break;
        }
        case ESP_BAT_EVENT_REG: {
            break;
        }
        case ESP_HIDD_EVENT_DEINIT_FINISH:
	     break;
		case ESP_HIDD_EVENT_BLE_CONNECT: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_CONNECT");
            hid_conn_id = param->connect.conn_id;
            s_hid_connected = true;
            s_hid_advertising = false;
            s_hid_have_remote = true;
            memcpy(s_hid_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            break;
        }
        case ESP_HIDD_EVENT_BLE_DISCONNECT: {
            sec_conn = false;
            s_hid_connected = false;
            s_hid_have_remote = false;
            hid_conn_id = 0;
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT");
            if (s_bt_hid_started && !s_bt_shutting_down) {
                esp_ble_gap_start_advertising(&hidd_adv_params);
            }
            break;
        }
        case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT: {
            ESP_LOGI(HID_DEMO_TAG, "%s, ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT", __func__);
            ESP_LOG_BUFFER_HEX(HID_DEMO_TAG, param->vendor_write.data, param->vendor_write.length);
            break;
        }
        case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT");
            ESP_LOG_BUFFER_HEX(HID_DEMO_TAG, param->led_write.data, param->led_write.length);
            break;
        }
        default:
            break;
    }
    return;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&hidd_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        s_hid_advertising = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
        ESP_LOGI(HID_DEMO_TAG, "advertising %s", s_hid_advertising ? "started" : "failed");
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_hid_advertising = false;
        break;
     case ESP_GAP_BLE_SEC_REQ_EVT:
        for(int i = 0; i < ESP_BD_ADDR_LEN; i++) {
             ESP_LOGD(HID_DEMO_TAG, "%x:",param->ble_security.ble_req.bd_addr[i]);
        }
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
	 break;
     case ESP_GAP_BLE_AUTH_CMPL_EVT:
        sec_conn = param->ble_security.auth_cmpl.success;
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_LOGI(HID_DEMO_TAG, "remote BD_ADDR: %08x%04x",\
                (bd_addr[0] << 24) + (bd_addr[1] << 16) + (bd_addr[2] << 8) + bd_addr[3],
                (bd_addr[4] << 8) + bd_addr[5]);
        ESP_LOGI(HID_DEMO_TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
        ESP_LOGI(HID_DEMO_TAG, "pair status = %s",param->ble_security.auth_cmpl.success ? "success" : "fail");
        if(!param->ble_security.auth_cmpl.success) {
            ESP_LOGE(HID_DEMO_TAG, "fail reason = 0x%x",param->ble_security.auth_cmpl.fail_reason);
        }
        break;
    default:
        break;
    }
}

static esp_err_t bt_hid_start(void)
{
    esp_err_t ret;

    if (s_bt_hid_started) {
        return ESP_OK;
    }

    s_bt_shutting_down = false;
    s_hid_connected = false;
    s_hid_advertising = false;
    s_hid_have_remote = false;
    sec_conn = false;
    hid_conn_id = 0;

    if (!s_bt_classic_released) {
        ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            s_bt_classic_released = true;
        } else {
            ESP_LOGW(HID_DEMO_TAG, "release classic bt memory failed: %s", esp_err_to_name(ret));
        }
    }

    esp_bt_controller_status_t ctrl_status = esp_bt_controller_get_status();
    if (ctrl_status == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ret = esp_bt_controller_init(&bt_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(HID_DEMO_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
            return ret;
        }
        ctrl_status = esp_bt_controller_get_status();
    }

    if (ctrl_status != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (ret != ESP_OK) {
            ESP_LOGE(HID_DEMO_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
            return ret;
        }
    }

    esp_bluedroid_status_t bd_status = esp_bluedroid_get_status();
    if (bd_status == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        ret = esp_bluedroid_init();
        if (ret != ESP_OK) {
            ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed: %s", __func__, esp_err_to_name(ret));
            return ret;
        }
        bd_status = esp_bluedroid_get_status();
    }

    if (bd_status != ESP_BLUEDROID_STATUS_ENABLED) {
        ret = esp_bluedroid_enable();
        if (ret != ESP_OK) {
            ESP_LOGE(HID_DEMO_TAG, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(ret));
            return ret;
        }
    }

    if((ret = esp_hidd_profile_init()) != ESP_OK) {
        ESP_LOGE(HID_DEMO_TAG, "%s init hidd failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ///register the callback function to the gap module
    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    /* set the security iocap & auth_req & key size & init key response key parameters to the stack*/
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;     //bonding with peer device after authentication
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;           //set the IO capability to No output No input
    uint8_t key_size = 16;      //the key size should be 7~16 bytes
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    /* If your BLE device act as a Slave, the init_key means you hope which types of key of the master should distribute to you,
    and the response key means which key you can distribute to the Master;
    If your BLE device act as a master, the response key means you hope which types of key of the slave should distribute to you,
    and the init key means which key you can distribute to the slave. */
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    s_bt_hid_started = true;
    return ESP_OK;
}

// 运行蓝牙HID控制程序
esp_err_t app_hid_ctrl(void)
{
    return bt_hid_start();
}

static uint8_t bt_hid_action_usage(bt_hid_action_t action)
{
    switch (action) {
    case BT_HID_ACTION_PLAY_PAUSE:
        return HID_CONSUMER_PLAY_PAUSE;
    case BT_HID_ACTION_PREV:
        return HID_CONSUMER_SCAN_PREV_TRK;
    case BT_HID_ACTION_NEXT:
        return HID_CONSUMER_SCAN_NEXT_TRK;
    case BT_HID_ACTION_STOP:
        return HID_CONSUMER_STOP;
    case BT_HID_ACTION_MUTE:
        return HID_CONSUMER_MUTE;
    case BT_HID_ACTION_VOLUME_DOWN:
        return HID_CONSUMER_VOLUME_DOWN;
    case BT_HID_ACTION_VOLUME_UP:
        return HID_CONSUMER_VOLUME_UP;
    default:
        return 0;
    }
}

const char *bt_hid_action_name(bt_hid_action_t action)
{
    switch (action) {
    case BT_HID_ACTION_PLAY_PAUSE:
        return "播放/暂停";
    case BT_HID_ACTION_PREV:
        return "上一首";
    case BT_HID_ACTION_NEXT:
        return "下一首";
    case BT_HID_ACTION_STOP:
        return "停止";
    case BT_HID_ACTION_MUTE:
        return "静音";
    case BT_HID_ACTION_VOLUME_DOWN:
        return "音量-";
    case BT_HID_ACTION_VOLUME_UP:
        return "音量+";
    default:
        return "未知";
    }
}

esp_err_t bt_hid_send_action(bt_hid_action_t action)
{
    uint8_t usage = bt_hid_action_usage(action);
    if (usage == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_bt_hid_started || !s_hid_connected || !sec_conn) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_hidd_send_consumer_value(hid_conn_id, usage, true);
    vTaskDelay(15 / portTICK_PERIOD_MS);
    esp_hidd_send_consumer_value(hid_conn_id, usage, false);
    ESP_LOGI(HID_DEMO_TAG, "sent action: %s", bt_hid_action_name(action));
    return ESP_OK;
}

static uint8_t bt_hid_key_usage(bt_hid_key_action_t action)
{
    switch (action) {
    case BT_HID_KEY_UP:
        return HID_KEY_UP_ARROW;
    case BT_HID_KEY_DOWN:
        return HID_KEY_DOWN_ARROW;
    case BT_HID_KEY_LEFT:
        return HID_KEY_LEFT_ARROW;
    case BT_HID_KEY_RIGHT:
        return HID_KEY_RIGHT_ARROW;
    case BT_HID_KEY_ENTER:
        return HID_KEY_RETURN;
    case BT_HID_KEY_ESCAPE:
        return HID_KEY_ESCAPE;
    case BT_HID_KEY_SPACE:
        return HID_KEY_SPACEBAR;
    case BT_HID_KEY_BACKSPACE:
        return HID_KEY_DELETE;
    default:
        return 0;
    }
}

const char *bt_hid_key_action_name(bt_hid_key_action_t action)
{
    switch (action) {
    case BT_HID_KEY_UP:
        return "上";
    case BT_HID_KEY_DOWN:
        return "下";
    case BT_HID_KEY_LEFT:
        return "左";
    case BT_HID_KEY_RIGHT:
        return "右";
    case BT_HID_KEY_ENTER:
        return "回车";
    case BT_HID_KEY_ESCAPE:
        return "Esc";
    case BT_HID_KEY_SPACE:
        return "空格";
    case BT_HID_KEY_BACKSPACE:
        return "退格";
    default:
        return "未知";
    }
}

esp_err_t bt_hid_send_key_action(bt_hid_key_action_t action)
{
    uint8_t key = bt_hid_key_usage(action);
    if (key == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_bt_hid_started || !s_hid_connected || !sec_conn) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_hidd_send_keyboard_value(hid_conn_id, 0, &key, 1);
    vTaskDelay(15 / portTICK_PERIOD_MS);
    esp_hidd_send_keyboard_value(hid_conn_id, 0, NULL, 0);
    ESP_LOGI(HID_DEMO_TAG, "sent key: %s", bt_hid_key_action_name(action));
    return ESP_OK;
}

const char *bt_hid_mouse_action_name(bt_hid_mouse_action_t action)
{
    switch (action) {
    case BT_HID_MOUSE_UP:
        return "鼠标上移";
    case BT_HID_MOUSE_DOWN:
        return "鼠标下移";
    case BT_HID_MOUSE_LEFT:
        return "鼠标左移";
    case BT_HID_MOUSE_RIGHT:
        return "鼠标右移";
    case BT_HID_MOUSE_LEFT_CLICK:
        return "左键";
    case BT_HID_MOUSE_RIGHT_CLICK:
        return "右键";
    default:
        return "未知";
    }
}

esp_err_t bt_hid_send_mouse_action(bt_hid_mouse_action_t action)
{
    if (!s_bt_hid_started || !s_hid_connected || !sec_conn) {
        return ESP_ERR_INVALID_STATE;
    }

    int8_t dx = 0;
    int8_t dy = 0;
    uint8_t button = 0;
    switch (action) {
    case BT_HID_MOUSE_UP:
        dy = -12;
        break;
    case BT_HID_MOUSE_DOWN:
        dy = 12;
        break;
    case BT_HID_MOUSE_LEFT:
        dx = -12;
        break;
    case BT_HID_MOUSE_RIGHT:
        dx = 12;
        break;
    case BT_HID_MOUSE_LEFT_CLICK:
        button = 0x01;
        break;
    case BT_HID_MOUSE_RIGHT_CLICK:
        button = 0x02;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    esp_hidd_send_mouse_value(hid_conn_id, button, dx, dy);
    if (button != 0) {
        vTaskDelay(15 / portTICK_PERIOD_MS);
        esp_hidd_send_mouse_value(hid_conn_id, 0, 0, 0);
    }
    ESP_LOGI(HID_DEMO_TAG, "sent mouse: %s", bt_hid_mouse_action_name(action));
    return ESP_OK;
}

bool bt_hid_is_started(void)
{
    return s_bt_hid_started;
}

bool bt_hid_is_connected(void)
{
    return s_hid_connected;
}

bool bt_hid_is_ready(void)
{
    return s_bt_hid_started && s_hid_connected && sec_conn;
}

const char *bt_hid_status_text(void)
{
    if (!s_bt_hid_started) {
        return "已关闭";
    }
    if (s_hid_connected && sec_conn) {
        return "已配对，可控制";
    }
    if (s_hid_connected) {
        return "已连接，等待配对";
    }
    if (s_hid_advertising) {
        return "广播中，等待连接";
    }
    return "启动中";
}

esp_err_t bt_hid_clear_bonds(void)
{
    if (!s_bt_hid_started) {
        return ESP_ERR_INVALID_STATE;
    }

    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num <= 0) {
        return ESP_OK;
    }

    esp_ble_bond_dev_t *dev_list = calloc((size_t)dev_num, sizeof(*dev_list));
    if (dev_list == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_ble_get_bond_device_list(&dev_num, dev_list);
    if (ret == ESP_OK) {
        for (int i = 0; i < dev_num; i++) {
            esp_err_t remove_ret = esp_ble_remove_bond_device(dev_list[i].bd_addr);
            if (remove_ret != ESP_OK) {
                ret = remove_ret;
            }
        }
    }
    free(dev_list);
    return ret;
}

// 关闭蓝牙 
esp_err_t bt_hid_end(void)
{
    esp_err_t ret = ESP_OK;
    if (!s_bt_hid_started) {
        return ESP_OK;
    }

    s_bt_shutting_down = true;
    if (s_hid_connected && s_hid_have_remote) {
        ret |= esp_ble_gap_disconnect(s_hid_remote_bda);
        vTaskDelay(60 / portTICK_PERIOD_MS);
    }
    if (s_hid_advertising) {
        ret |= esp_ble_gap_stop_advertising();
        vTaskDelay(30 / portTICK_PERIOD_MS);
    }

    ret |= esp_hidd_profile_deinit();
    vTaskDelay(10 / portTICK_PERIOD_MS);

    esp_bluedroid_status_t bd_status = esp_bluedroid_get_status();
    if (bd_status == ESP_BLUEDROID_STATUS_ENABLED) {
        ret |= esp_bluedroid_disable();
        bd_status = esp_bluedroid_get_status();
    }
    if (bd_status == ESP_BLUEDROID_STATUS_INITIALIZED) {
        ret |= esp_bluedroid_deinit();
    }

    esp_bt_controller_status_t ctrl_status = esp_bt_controller_get_status();
    if (ctrl_status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        ret |= esp_bt_controller_disable();
        ctrl_status = esp_bt_controller_get_status();
    }
    if (ctrl_status == ESP_BT_CONTROLLER_STATUS_INITED) {
        ret |= esp_bt_controller_deinit();
    }

    sec_conn = false;
    hid_conn_id = 0;
    s_hid_connected = false;
    s_hid_advertising = false;
    s_hid_have_remote = false;
    s_bt_shutting_down = false;
    s_bt_hid_started = false;
    return ret;
}
