# Feature Notes

## Launcher

The home screen is a paged LVGL launcher. Touch swipes change pages. GPIO0,
the board user key, changes page on the home screen and refreshes the random
image when the random image app is active.

## Random Image

The random image app downloads a JPEG body, decodes it with `esp_jpeg`, renders
through a PSRAM canvas, and caches the last image under TF card storage.
HTTPS uses certificate validation with mbedTLS transient allocations moved to
PSRAM. The refresh task and HTTP read buffer are also PSRAM-friendly so repeated
refreshes do not exhaust internal DRAM after the first successful image.

Touch behavior:

- tap image: toggle full-screen chrome
- swipe right: back
- swipe left/up/down: refresh
- GPIO0: refresh while the app is active

## Media

The file manager previews JPG, PNG, GIF, text, MP3, and WAV. Video files are
currently detected as media but not played.

Large previews are guarded before LVGL receives the file:

- JPG: up to 2 MB, decoded in a worker task
- PNG: up to 768 KB
- GIF: up to 512 KB
- text: only the first 4 KB is loaded into the text preview

Files above those limits show a size message instead of being decoded
automatically. This keeps large media from blocking the UI thread or causing
repeated refresh glitches on a small embedded display.

The TF card file manager is kept focused on browsing and media previews. Disk
operations live in the separate TF manager page.

The media library builds `/szpi/cache/media_index.tsv` in the background and
can browse that index by all media, images, audio, or text. Opening an indexed
entry reuses the same preview and music-player paths as the TF card file
manager.

The TF manager page shows capacity, mounted filesystem, partition table type
(`GPT`, `MBR`, or raw FAT), and the first few partition entries. It also has a
guarded quick formatter for the currently mounted volume:

- `FS` cycles `Auto`, `FAT32`, and `exFAT`
- `DIR` recreates the `/sdcard/szpi` product directories
- `FMT` requires a second confirmation tap before formatting
- after formatting, product directories under `/sdcard/szpi` are recreated

Formatting preserves the current partition-table layout and formats the mounted
data partition. Whole-card repartitioning is intentionally left out of the
touch UI until it can be made harder to trigger by accident.

The advanced USB-ZIP/USB-FDD/HDD boot modes are PC BIOS boot-disk layout
concepts. The firmware currently reports that distinction in the TF manager
instead of offering destructive boot-disk partition editing from the device UI.

Music scanning checks:

- `/sdcard/szpi/music`
- `/sdcard/music`
- `/sdcard`

Music playback modes:

- `停止`: play the current track and stop when it ends
- `顺序`: automatically advance until the end of the list, then stop
- `列表`: loop through the whole list
- `单曲`: repeat the current track

Opening music from the home launcher defaults to `顺序`. Opening an audio file
from the TF card file manager defaults to `停止` and returns to the same folder
when leaving the player.

The recorder stores 16 kHz mono WAV files at:

```text
/sdcard/szpi/recordings/rec_<time>.wav
```

## Camera

Camera initialization happens inside the camera task. Failures are shown on the
LVGL page instead of aborting the whole firmware. Photos are saved to:

```text
/szpi/photos/photo_<time>.jpg
```

## WLAN

The WLAN UI reuses the shared WiFi driver instead of repeatedly tearing it down.
Open networks connect without a password prompt. Successful network credentials
are persisted to device NVS and, when a TF card is present, to TF card history.
The currently connected network is also synced to the random image app config.

## Bluetooth

The current page is a BLE HID demo. It has been made reentrant so repeatedly
opening the page is less likely to crash. A2DP speaker, Bluetooth microphone,
and richer pairing UX are future work.

## Roadmap

Exploratory directions are intentionally tracked as future work until they are
stable on real hardware: A2DP speaker, Bluetooth microphone, USB plug-and-play
camera, short video playback, camera recording, a fuller media library,
thumbnail generation, and background indexing.
