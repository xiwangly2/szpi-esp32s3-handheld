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

Scanning stops after 60 seconds or on Back. The camera and decoder are released
before handing credentials to WLAN. Deleting the parent page cancels the scan;
generation checks prevent an old result from connecting through a newer WLAN
page. A camera-close guard prevents concurrent use by the photo application.
Passwords and decoded QR payloads are not written to the serial log.

For a reliable optical test, use a bright, sharp QR image with a white margin.
Keep the entire symbol in view and vary the distance slowly. The board camera's
fixed focus and lighting can affect recognition even when host decoder tests pass.

See [the regression checks](../tests/wifi-qr/README.md) for host and device tests.
