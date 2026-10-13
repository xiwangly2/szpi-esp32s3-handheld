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

## Next

- On-demand JPEG thumbnails for the media library.
- Incremental media index updates instead of full-card rescans.
- Real-camera qualification of IP camera preview over WLAN.
- Better lifecycle guards when quickly switching camera, audio, WLAN, and BLE
  pages.

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
