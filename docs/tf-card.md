# TF Card Layout

The firmware mounts the TF card at `/sdcard`.

Recommended layout:

```text
/szpi/config/wifi.ini
/szpi/config/wlan_history.ini
/szpi/cache/random/latest.jpg
/szpi/photos/
/szpi/recordings/
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
entry when the SSID is seen again. The same history is also mirrored into device
NVS as a no-card fallback.

## File Systems

The default firmware configuration enables:

- FAT12/FAT16/FAT32 through FatFs
- exFAT through `CONFIG_FATFS_EXFAT=y`
- long file names allocated on heap
- UTF-8 API paths with codepage 936
- 4096-byte FatFs block reporting
- FatFs buffers preferred in PSRAM where possible

Recommended card format:

- exFAT for larger media cards
- one Microsoft Basic Data partition
- GPT is supported by the bundled FatFs scanner and is the intended setup for
  modern large TF cards

The launcher has two TF-card entries:

- `TF卡`: file browsing and media preview
- `TF管理`: card information, product-directory initialization, and formatting

The TF manager can identify the card capacity, mounted FAT variant, partition
table style, and the first few partition entries:

- GPT
- MBR
- raw FAT/superfloppy with no partition table

It can format the mounted TF card. Use `FS` to choose:

- `Auto`: FatFs chooses a suitable FAT/exFAT variant
- `FAT32`: maximum compatibility
- `exFAT`: larger media cards and large files

Formatting requires tapping `FMT` and then `OK?`. It erases the TF card and
recreates the `/szpi` product directories.

`DIR` recreates the `/szpi` product directories without formatting.

Multi-partition cards are currently detected and displayed, but the device UI
does not yet switch between multiple mounted volumes. The firmware still mounts
one FatFs volume at `/sdcard`.

USB-ZIP, USB-FDD, and HDD modes are legacy PC BIOS boot-disk layout concepts.
They are not SDMMC electrical modes and are not required for this handheld to
use a TF card. Treat them as future boot-disk authoring features, not normal
card filesystem choices.

Compatibility fallback:

- FAT32 or exFAT
- single partition
- MBR partition table

Not supported as TF-card filesystems: NTFS, APFS, ext4, HFS+, or multi-volume
switching from the device UI.

ESP-IDF leaves exFAT disabled by default because it is optional in FatFs. This
project enables it by default for the handheld media use case. If you ship a
commercial product, review the licensing and IP notes that apply to your
distribution.

For lots of Unicode text, store files as UTF-8 on TF. The expensive part is not
the text file itself, but the display font. A product-quality build should use a
smaller CJK subset or load fonts from TF instead of compiling a huge full font
into the firmware.
