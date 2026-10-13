# IP Camera Preview

The fourth launcher page contains `IP相机`. This is a WLAN network viewer, not
the board's built-in camera or a USB/UVC host. Open WLAN and connect first.

## Controls And Configuration

- The settings icon opens a URL field and an on-screen keyboard. Save persists
  the address in NVS and starts viewing. The keyboard's confirm key also saves.
- Play/pause stops and resumes network reception; it retains the last image.
- Refresh reconnects. Tap the image to hide/show the controls. Back requests
  cancellation and frees the network/decoder resources before returning home.
- Settings pauses reception. Back from the editor discards the draft address.
- `TF` imports the card's address into the editor; Save applies and persists it.

Copy `examples/tf-card/szpi/config/ip-camera.ini.example` to:

```text
/szpi/config/ip-camera.ini
```

```ini
url=http://192.168.1.50:8080/video
max_fps=10
snapshot_ms=500
```

The example address is a placeholder, not a discoverable camera. Use the actual
JPEG/MJPEG endpoint supplied by the camera's software, not its HTML dashboard.
Configuration precedence for the URL is **NVS > TF > menuconfig**. The public
`HANDHELD_IP_CAMERA_URL` default is empty. Saving an empty URL removes the NVS
override and stops playback; reopening the app reads the TF/default fallback.
TF `max_fps` accepts 1-15 (default 10), and `snapshot_ms` accepts 200-10000
(default 500). Snapshot requests wait this interval after the previous request.
Keys are case-sensitive and use the shown `key=value` syntax, with no sections.
No card is required when the URL is stored in NVS.

## Protocols And Limits

- HTTP/HTTPS `200` responses with `image/jpeg` are polled as snapshots.
- `multipart/x-mixed-replace` MJPEG uses the advertised boundary, including
  quoted boundaries and bodies split across arbitrary network reads. Per-part
  Content-Length is optional; when present it must match the frame exactly.
- Baseline JPEG only. Each compressed frame is limited to 512 KiB. Source
  dimensions are limited to 2560x1920 and must fit within the decoder's 1/8
  scaling limit. Larger-than-screen images need 8-pixel-aligned source sizes.
  Output is fitted to at most 320x240 with dimensions rounded down to multiples
  of eight when scaling. Black margins preserve the approximate aspect ratio.
- Decoding uses the S3-optimized JPEG component and aligned PSRAM buffers.
  Excess MJPEG frames are discarded before decode to respect `max_fps`.
- Socket timeout is 1.5 seconds, with an 8-second incomplete-frame budget.
  Name resolution and connection shutdown can take longer than one socket
  timeout. The UI remains scheduled while the worker exits.
- Three consecutive failures stop automatic retries. A valid stream followed
  by disconnection can reconnect. Errors restore the controls even when the
  viewer was full screen. Refresh or Save explicitly retries.
- HTTPS uses the certificate bundle and hostname verification. Self-signed
  certificates are not silently accepted. Redirects are not followed; enter
  the final endpoint to avoid forwarding credentials to another destination.
- Basic authentication can use `http[s]://user:password@host/path`. Percent-
  encode reserved characters in credentials. Digest, bearer-header setup,
  RTSP, H.264/H.265, ONVIF discovery, audio, and recording are not implemented.

URLs may contain credentials or access tokens. NVS and TF configuration are
not encrypted by this feature. The settings editor displays the saved URL;
avoid screenshots of credentials and never publish a personal configuration.
HTTP does not encrypt Basic credentials; use trusted HTTPS where available.
Frames remain in RAM and are not automatically saved or uploaded anywhere.

## Verification

`tests/ip-camera` tests the production URL/MIME/scaling and multipart wrapper
against the bundled parser, including byte-by-byte fragmentation, binary data,
wrong lengths/types, oversized input and cancellation. Firmware build and
device menu checks are separate from actual camera compatibility. No real IP
camera endpoint was supplied for this implementation, so sustained streaming,
TLS/auth interoperability, scaled-image rendering and reconnect behavior still
need validation with an actual endpoint. Do not interpret host tests as those
hardware results.

### Device Checkpoint: 2026-10-13

Firmware `b1f0378` built, flashed on COM6, booted, and was packaged. The user
confirmed that the keyboard is fully on screen and that input, Save and Back
work. Serial logs show connection-failure retries reaching the three-attempt
limit without a restart. Native protocol tests and the existing video-clock
test passed. A previous keyboard positioning issue was fixed by explicitly
using top-left alignment instead of inheriting the keyboard's bottom alignment.
Actual JPEG/MJPEG camera rendering and interoperability remain unverified.
