# WLAN QR Scan

Open `WLAN`, then tap `扫码` in the top-right corner after the network list
appears. Show a Wi-Fi sharing QR code to the camera. The live view has a scan
frame and a back button. Recognition runs locally; no image is uploaded or
saved to the TF card.

A recognized Wi-Fi code closes the camera and starts the normal connection
flow automatically. The connection page displays the SSID, not the password.
After a successful DHCP connection, the existing WLAN history is saved in NVS
and, if mounted, `/sdcard/szpi/config/wlan_history.ini`. A TF card is not needed
to scan or remember a network. Network credentials in that existing INI file
are plain text; avoid committing a populated card backup to the public repo.

## Supported Codes

The scanner uses the [ZXing Wi-Fi QR convention](https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11):

```text
WIFI:T:WPA;S:ExampleNetwork;P:ExamplePassword;;
WIFI:T:nopass;S:Guest;;
```

- `WPA` and `WPA2`: WPA/WPA2 personal networks, 8-63-byte passphrases or 64 hex digits.
- `SAE` and `WPA3`: WPA3 personal networks with 8-63-byte passphrases.
- `nopass`, empty `T`, or omitted `T`: open network; the password is ignored.
- `H:true`: hidden SSID; connection targets the decoded SSID directly.
- UTF-8 text, up to 32 bytes for the SSID; backslash-escaped `;`, `:`, `,`, `"`, and `\`.
- Fields may appear in any order. Duplicate credential fields, control bytes,
  malformed UTF-8, and oversized input are rejected rather than truncated.

The ESP32-S3 uses 2.4 GHz WLAN. A code cannot add support for a 5 GHz-only AP.
WEP, enterprise/EAP, DPP provisioning, and captive-portal browser login are not
implemented by this feature. URLs and other QR contents do not trigger actions.

## Lifecycle And Resources

The [Espressif quirc component](https://components.espressif.com/components/espressif/quirc/versions/1.2.0/readme)
(ISC license) decodes 320x240 camera frames in a single worker task. Preview,
code, and payload buffers use PSRAM; quirc's larger allocations also use the
project's PSRAM-enabled allocator. The worker has a 16 KiB stack because quirc's
decode function contains an 8896-byte local work buffer.

The GC0308 RGB565 capture path uses one 153600-byte frame buffer in PSRAM, with
`CONFIG_CAMERA_PSRAM_DMA` disabled and `CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX=8192`.
At 320x240 the pinned driver uses a 7680-byte internal DMA bounce buffer. Its
default 30720-byte request failed after WLAN and the QR worker allocated their
internal stacks, even with several MiB of free PSRAM. This smaller bounce
buffer also applies to the photo app's shared camera initialization. It costs
more DMA interrupts; check preview stability while WLAN is connected.

Direct PSRAM DMA was tested in `e130869` but is not qualified for this board
configuration: it produced abnormal color/ghosting and heap assertions at
`cam_deinit()` when leaving both scanner and photo views. Keep it disabled;
do not distribute that experimental binary as a stable camera release.

When reusing an old `sdkconfig`, set Camera configuration > DMA buffer size to
8192 and turn off PSRAM DMA. Changing `sdkconfig.defaults` alone does not
replace existing saved values. The camera-start log reports the effective
mode and largest DMA-capable block; `cam_hal` reports the actual buffer size.

Scanning stops after 60 seconds or on Back. The camera and decoder are released
before handing credentials to WLAN. Deleting the parent page cancels the scan;
generation checks prevent an old result from connecting through a newer WLAN
page. A camera-close guard prevents concurrent use by the photo application.
Passwords and decoded QR payloads are not written to the serial log.

For a reliable optical test, use a bright, sharp QR image with a white margin.
Keep the entire symbol in view and vary the distance slowly. The board camera's
fixed focus and lighting can affect recognition even when host decoder tests pass.

See [the regression checks](../tests/wifi-qr/README.md) for host and device tests.

## Device Verification

On 2026-10-14 (Asia/Shanghai), firmware `ccca57f` was built and flashed to the
connected SZPI ESP32-S3. The user confirmed normal preview colors and no reboot
after repeated scanner/photo-page entry and return. This is the accepted
replacement for the failed direct-PSRAM-DMA experiment `e130869`.

- Host checks: Wi-Fi payload and decoder 2/2, video clock 1/1, IP camera
  protocol 1/1 passed. Flash verification and automatic WLAN reconnection passed.
- The interactive result is user-observed, not a complete captured serial
  trace. A subsequent no-reset serial snapshot showed about 55 KiB free
  internal heap, a 31 KiB largest internal block, and about 7.9 MiB free PSRAM.
- This round does not establish long-duration soak reliability, saved-photo
  file integrity, or fresh optical scan-to-connect/persistence coverage.

Local merged artifact: `release/szpi-esp32s3-handheld-ccca57f-camera-bounce.bin`.
SHA-256: `A81B195DAB78567CD3103D612E964F186B458ED881DADBB6DFC260BDA06C7684`.
