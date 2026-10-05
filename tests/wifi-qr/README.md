# Wi-Fi QR Checks

Run from the repository root with a host C compiler and CMake:

```powershell
cmake -S tests/wifi-qr -B tests/wifi-qr/build
cmake --build tests/wifi-qr/build --config Debug
ctest --test-dir tests/wifi-qr/build -C Debug --output-on-failure
```

The payload suite builds the same parser as the firmware. It checks UTF-8,
escaping and quoting, field order, open/WPA/WPA3 networks, hidden SSIDs,
32-byte SSIDs, 64-digit PSKs, overlong values, duplicated fields, malformed
input, binary/control bytes, and unsupported enterprise/WEP content. Invalid
results must clear the output credentials.

After `tools/powershell/idf.ps1 reconfigure` has downloaded the managed
components, a second test builds the same quirc decoder and LVGL's Nayuki QR
generator. It decodes 24 generated 320x240 grayscale images across four
rotations and both mirror states, passes payloads through the Wi-Fi parser,
and rejects an empty image. Without these components, CMake explicitly reports
that only the parser suite is available.

Host results do not prove optical focus, display behavior, radio authentication,
or persistence. On the actual board:

1. Enter WLAN and open the scanner. Check the camera preview and Back. Repeat
   rapidly, then switch to the photo app and back; neither app may restart or
   lose the camera.
2. Scan an authorized WPA/WPA2 Wi-Fi sharing code. Check the decoded SSID on
   the connection page and DHCP success in serial logs. No password or raw QR
   payload may appear in those logs.
3. Restart the board and check automatic reconnection. Repeat without a TF
   card to exercise the NVS history, then with a card to check its INI copy.
4. Test an open-network code, hidden SSID, Chinese SSID, special-character
   password, and WPA3 when corresponding test APs are available.
5. Test a wrong password and an unreachable AP. The connection must fail or
   time out and allow another scan; no successful-history entry should be saved.
6. Show a URL QR, a WEP/EAP QR, and a malformed Wi-Fi code. Check the status
   messages. Leave while a code is in view; an old scan must not connect later.
7. Leave the camera without a code for 60 seconds. It should close with a
   timeout message, releasing its buffers and the camera.

`wifi_qr: scanner stopped` reports recognition, cancellation, and minimum free
worker stack. Record which cases were actually tested on hardware separately
from host-test results.
