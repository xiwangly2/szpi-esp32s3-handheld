# TF Card Layout

The firmware mounts the TF card at `/sdcard`.

Recommended layout:

```text
/szpi/config/wifi.ini
/szpi/config/wlan_history.ini
/szpi/cache/random/latest.jpg
/szpi/photos/
/szpi/music/
/music/
```

## WiFi Config

Copy `examples/tf-card/szpi/config/wifi.ini.example` to:

```text
/szpi/config/wifi.ini
```

Example:

```ini
ssid=YOUR_2G_SSID
password=YOUR_WIFI_PASSWORD
api_url=https://example.com/image.php?key=change-me&return=print&esp=1
```

Leave `password` empty for an open WLAN.

On Windows, the helper script can create the recommended directories and
`wifi.ini` in one step:

```powershell
.\tools\powershell\prepare-tf-card.ps1 -Drive E: -Ssid "YOUR_2G_SSID" -Password "YOUR_WIFI_PASSWORD" -ApiUrl "https://example.com/image.php?key=change-me&return=print&esp=1"
```

## WLAN History

Successful connections from the WLAN UI are saved to:

```text
/szpi/config/wlan_history.ini
```

The firmware keeps recent networks so the next connection can skip password
entry when the SSID is seen again.

## File Systems

The project enables FatFs long file names, UTF-8 API encoding, codepage 936, and
optional exFAT support. exFAT on a GPT TF card is the intended convenient setup
for larger media cards. If a card cannot mount on a specific firmware build,
try a single-partition MBR card as a compatibility fallback.

For lots of Unicode text, store files as UTF-8 on TF. The expensive part is not
the text file itself, but the display font. A product-quality build should use a
smaller CJK subset or load fonts from TF instead of compiling a huge full font
into the firmware.
