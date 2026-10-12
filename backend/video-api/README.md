# Handheld Video Endpoint

Convert a source video on the PC with `tools/powershell/convert-video.ps1`.
This endpoint stores and serves the resulting MJPEG/PCM AVI; it does not run
FFmpeg or accept arbitrary remote URLs. The firmware downloads before playback.

Deploy `video.php` and a configured `config.php` based on `config.sample.php`,
or set `VIDEO_API_KEY` and `VIDEO_MEDIA_DIR`. Set `media_dir` to a writable directory **outside the
public web root** so clients cannot bypass authentication with a static URL.
For Nginx, deny direct access to `config.php` as well. PHP 8.0+ is required.
For uploads, raise both `upload_max_filesize` and `post_max_size` to cover the
converted file (for example 128M and 130M). The application limit is 256 MiB.

Upload a converted file (PowerShell 7):

```powershell
Invoke-RestMethod -Method Post -Uri 'https://example.com/video/video.php' `
  -Headers @{ Authorization = "Bearer $env:VIDEO_API_KEY" } `
  -Form @{ file = Get-Item '.\release\video\BadApple.avi' }
```

Alternatively, place the converted AVI in `media_dir` through your server's
file manager/SFTP. Repeated API uploads to the same name return HTTP 409;
use a versioned filename. A successful upload returns the file size and SHA256.

Set `/szpi/config/video.ini` on the TF card to the download URL:

```ini
url=https://example.com/video/video.php?key=YOUR_KEY&file=BadApple.avi
```

GET, HEAD, and single byte ranges are supported. Wrong authentication is 401,
missing media is 404, and invalid ranges are 416. Keep credentials and video
assets out of Git. This endpoint is independent of the random-image PHP API.
