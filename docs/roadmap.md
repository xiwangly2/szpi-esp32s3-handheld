# Roadmap

This project targets a playful, practical ESP32-S3 handheld. Some ideas are
firmware work; some need external hardware because of ESP32-S3 limits.

## Now

- Media library scan and `/szpi/cache/media_index.tsv`.
- TF card file browsing and safe previews for common media types.
- Camera photo capture, WAV recorder, MP3/WAV playback, WLAN history, and random
  image caching.

## Next

- Incremental media index updates instead of full-card rescans.
- On-demand JPEG thumbnails for the media library.
- Richer media library filters: photos, recordings, music, videos, documents.
- Better lifecycle guards when quickly switching camera, audio, WLAN, and BLE
  pages.

## Hardware Notes

- ESP32-S3 has BLE, not Bluetooth Classic. Board-only A2DP sink/source is not a
  realistic target. For Bluetooth speaker/microphone features, use BLE Audio
  experiments where supported or add an external Classic Bluetooth audio module
  connected by I2S/UART.
- USB cameras are possible only within USB full-speed and driver limits. Tiny
  MJPEG or low-resolution streams are more realistic than high-frame-rate UVC.
- Video playback should start with very small MJPEG-like clips or image
  sequences. MP4/H.264 decode is outside a comfortable ESP32-S3 software-only
  budget.
- Video recording can be explored as periodic JPEG frames plus WAV audio, then
  post-processing on a computer, rather than real-time MP4 encoding on-device.
