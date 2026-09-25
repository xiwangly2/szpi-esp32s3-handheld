#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    BT_HID_ACTION_PLAY_PAUSE = 0,
    BT_HID_ACTION_PREV,
    BT_HID_ACTION_NEXT,
    BT_HID_ACTION_STOP,
    BT_HID_ACTION_MUTE,
    BT_HID_ACTION_VOLUME_DOWN,
    BT_HID_ACTION_VOLUME_UP,
} bt_hid_action_t;

typedef enum {
    BT_HID_KEY_UP = 0,
    BT_HID_KEY_DOWN,
    BT_HID_KEY_LEFT,
    BT_HID_KEY_RIGHT,
    BT_HID_KEY_ENTER,
    BT_HID_KEY_ESCAPE,
    BT_HID_KEY_SPACE,
    BT_HID_KEY_BACKSPACE,
} bt_hid_key_action_t;

typedef enum {
    BT_HID_MOUSE_UP = 0,
    BT_HID_MOUSE_DOWN,
    BT_HID_MOUSE_LEFT,
    BT_HID_MOUSE_RIGHT,
    BT_HID_MOUSE_LEFT_CLICK,
    BT_HID_MOUSE_RIGHT_CLICK,
} bt_hid_mouse_action_t;

esp_err_t app_hid_ctrl(void);
esp_err_t bt_hid_end(void);
esp_err_t bt_hid_send_action(bt_hid_action_t action);
esp_err_t bt_hid_send_key_action(bt_hid_key_action_t action);
esp_err_t bt_hid_send_mouse_action(bt_hid_mouse_action_t action);
esp_err_t bt_hid_clear_bonds(void);
bool bt_hid_is_started(void);
bool bt_hid_is_connected(void);
bool bt_hid_is_ready(void);
const char *bt_hid_status_text(void);
const char *bt_hid_action_name(bt_hid_action_t action);
const char *bt_hid_key_action_name(bt_hid_key_action_t action);
const char *bt_hid_mouse_action_name(bt_hid_mouse_action_t action);
