# Feature Notes

## Launcher

The home screen is a two-page LVGL launcher. Touch swipes change pages. GPIO0,
the board user key, changes page on the home screen and refreshes the random
image when the random image app is active.

## Random Image

The random image app downloads a JPEG body, decodes it with `esp_jpeg`, renders
through a PSRAM canvas, and caches the last image under TF card storage.

Touch behavior:

- tap image: toggle full-screen chrome
- swipe right: back
- swipe left/up/down: refresh
- GPIO0: refresh while the app is active

## Media

The file manager previews JPG, PNG, GIF, text, MP3, and WAV. Video files are
currently detected as media but not played.

Music scanning checks:

- `/sdcard/szpi/music`
- `/sdcard/music`
- `/sdcard`

## Camera

Camera initialization happens inside the camera task. Failures are shown on the
LVGL page instead of aborting the whole firmware. Photos are saved to:

```text
/szpi/photos/photo_<time>.jpg
```

## WLAN

The WLAN UI reuses the shared WiFi driver instead of repeatedly tearing it down.
Open networks connect without a password prompt. Successful network credentials
are persisted to TF card history and synced to the random image app config.

## Bluetooth

The current page is a BLE HID demo. It has been made reentrant so repeatedly
opening the page is less likely to crash. A2DP speaker, Bluetooth microphone,
and richer pairing UX are future work.
