# Roadmap

This project targets a playful, practical ESP32-S3 handheld. Some ideas are
firmware work; some need external hardware because of ESP32-S3 limits.

## Now

- Media library scan and `/szpi/cache/media_index.tsv`.
- TF card file browsing and safe previews for common media types.
- On-board `/local` wear-level FAT for small config, logs, and cache fallback.
- Camera photo capture, WAV recorder, MP3/WAV playback, WLAN history, BLE HID
  combo control, and random image caching.
- MJPEG/PCM AVI playback with a full-length 320x240/15-fps device check.
- Configurable HTTP(S) JPEG/MJPEG IP camera viewer; protocol tests are separate
  from real camera compatibility validation.
- [Information desk](information-desk.md): compact weather/RSS/provider-status
  API and LVGL reader; AWS backend verified, device interaction still needs testing.

## Next

- On-demand JPEG thumbnails for the media library.
- Incremental media index updates instead of full-card rescans.
- Real-camera qualification of IP camera preview over WLAN.
- Better lifecycle guards when quickly switching camera, audio, WLAN, and BLE
  pages.
- Evaluate an on-demand voice assistant with exclusive microphone/speaker
  ownership before attempting always-on wake word or full duplex audio.

## Voice Assistant And Firmware Launcher

XiaoZhi-style voice conversation is a candidate application, not yet implemented.
The [current upstream XiaoZhi project](https://github.com/78/xiaozhi-esp32)
requires ESP-IDF 6.0.1+ as checked on 2026-10-15, while this project deliberately
pins 5.4.4. Do not merge the entire firmware or upgrade the stable SDK blindly;
evaluate a pinned compatible version or adapt the transport/audio modules.
Measure internal RAM, codec/AFE/wake-model needs and Flash before promising fit.

The board has 16 MiB Flash and 8 MiB PSRAM. Before the information desk, the app
used 7.58 MiB of a 12 MiB factory partition, plus a separate 3 MiB local-storage
partition. There is currently no second app slot or OTA data partition.

[ESP32 Launcher](https://github.com/bmorcelli/Launcher/wiki) is a relevant example
of selecting firmware from SD/network and installing it into Flash before boot.
It is not arbitrary firmware execution directly from TF. This board still needs
verified display/touch/SD/button mappings. Any launcher design needs a protected
recovery app, image identity/size/hash validation, power-loss recovery, explicit
NVS/config compatibility and a backup/migration plan for local storage. Never
write a merged whole-Flash binary into an application slot. No partition changes
or launcher installation are part of the information-desk implementation.

## Hardware Notes

- ESP32-S3 has BLE, not Bluetooth Classic. Board-only A2DP/HFP speaker or
  microphone modes are not realistic targets. For Bluetooth speaker/microphone
  features, add an external Classic
  Bluetooth audio module connected by I2S/UART.
- ESP32-S3 does not provide native LE Audio either.
- USB camera means a physical UVC camera attached to the ESP32-S3 USB Host/OTG
  port. It is possible only within USB full-speed and driver limits. Tiny MJPEG
  or low-resolution streams are more realistic than high-frame-rate UVC.
- IP camera means a camera already on the WLAN, usually exposing HTTP JPEG,
  MJPEG, or RTSP. HTTP JPEG/MJPEG is the best first target for this handheld:
  the existing WiFi, HTTPS, JPEG decode, and LVGL display path are already close
  to what it needs.
- Local MJPEG/PCM AVI is implemented. Additional codecs such as restricted
  H.264 software decoding are separate experiments, not universal MP4 support.
- Video recording can be explored as periodic JPEG frames plus WAV audio, then
  post-processing on a computer, rather than real-time MP4 encoding on-device.
