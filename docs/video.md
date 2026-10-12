# Video Playback

The handheld plays **AVI containing baseline MJPEG video and optional 16-bit
little-endian PCM audio**. The original H.264/AAC MP4 must first be converted on
the PC. The default conversion is 320x240, 15 fps, 22050 Hz mono audio, with the
original aspect ratio preserved using black padding.

## Convert

Install FFmpeg and FFprobe in PATH, then run from the repository root:

```powershell
.\tools\powershell\convert-video.ps1 -InputFile 'D:\Media\BadApple.mp4' `
  -OutputFile '.\release\video\BadApple.avi'
```

The script checks codecs, dimensions, file size, and maximum JPEG packet size
before publishing the output. It writes a JSON manifest with duration, size,
and SHA256. Use `-Fps 10`, `15`, `20`, or `30`; 15 is the initial recommended
profile, not a guarantee for every card or source. `-Quality 6` is the default
FFmpeg JPEG quantizer; larger values reduce file size. Existing output is only
replaced with `-Force`. Source videos and converted assets are not committed.

## TF Card

Copy the converted file to:

```text
/szpi/videos/BadApple.avi
```

Open the launcher's third-page `视频` entry. It selects that file, then the last
network cache as a fallback. Other AVI files can be opened directly from the
TF/local file browser or the media library. Closing the player reveals the
original list; opening it from the launcher returns home.

The player includes play/pause, restart, mute, volume, elapsed/total time, and a
progress indicator. Tap the picture to hide/show the top and bottom controls.
The progress indicator is read-only; seeking is not currently implemented.
Playback stops at the end. The restart button starts again from the beginning.

## Backend Source

The [portable Serverless backend](../backend/serverless-media/README.md) provides
Terraform modules for AWS, Cloudflare, Tencent, and Alibaba. AWS deployment and
full-file transfer have been verified; the other modules are statically
validated templates. A generated `.deploy/video.ini` contains the device's
read-only URL. The existing PHP endpoint below remains an alternative.

Upload the **converted AVI**, not the original MP4, using the separate
[PHP endpoint](../backend/video-api/README.md), or serve the AVI as a static
HTTP(S) file. Create `/szpi/config/video.ini` on the card:

```ini
url=https://example.com/video/video.php?key=YOUR_KEY&file=BadApple.avi
```

The optional menuconfig default `HANDHELD_VIDEO_URL` is blank in public builds.
The TF configuration overrides it. Connect WLAN, then tap `网络` in the player
to download and play. If no default file/cache exists but a URL is configured,
the launcher entry downloads it automatically.

Downloads use a temporary `.part` file, enforce a 256 MiB cap and a 10-minute
total deadline, validate the AVI header/stream metadata, and then replace
`/szpi/cache/video/online.avi`. Failed or cancelled transfers keep the previous
cache. A successful cached video plays offline. HTTPS uses the certificate
bundle. Device-side downloading still requires a writable TF card.

## Limits And Verification

- Video dimensions must be at most 320x240; frame rate at most 30 fps.
- Each compressed JPEG/PCM packet is capped at 128 KiB; total file size at
  256 MiB and duration at 30 minutes. Oversized/unsupported files show an error.
- Audio accepts PCM16, mono/stereo, 8-44.1 kHz. MP3/AAC tracks and H.264 video
  are not decoded by this player. Use the converter for predictable playback.
- Separate bounded extractor instances feed JPEG rendering and PCM output.
  Their clocks share pause accounting; late video frames are dropped rather
  than accumulating unlimited memory. Extractor indexing is disabled.
- Closing/restarting waits for the old audio task before reusing the codec or
  freeing the preview. Large pixel/frame pools use PSRAM.

`tests/video` checks clock behavior and the PHP API. FFprobe validates converted
media. Serial progress reports shown/dropped frames; only an actual full-length
device run can establish smoothness, audible sound, sync, and gesture behavior.

The demuxer is Espressif's `esp_extractor` binary component. Its license is
separate from this repository's application code; see `NOTICE.md`. On IDF 5.4,
`extractor_log_compat.c` forwards its preformatted V1 log calls to the legacy
logging entry point without changing the SDK or the other components.
