# Media Preview Device Regression

Use the connected SZPI ESP32-S3 with a TF card and the serial monitor. These
checks exercise LVGL, FatFs, and the JPEG worker together; a successful build or
normal boot does not prove touch navigation or cancellation.

Prepare two different baseline JPEGs below 768 KiB, a PNG/GIF below 256 KiB,
a text file, and an invalid JPEG (plain text with a `.jpg` extension). Keep
existing card data. Run from the repository root:

```powershell
.\tools\powershell\idf.ps1 -Port COM6 monitor
```

1. Open the media library, tap a JPEG, toggle full screen, and return. Repeat
   from the file manager. The original list and path must remain available.
2. Tap a JPEG and press Back during loading. Wait for the worker to finish.
   Neither an image nor an error page may reopen over the list or home screen.
3. Rapidly open JPEG A, return, and open JPEG B. Repeat at least 20 times. Only
   the last selected image may appear; there must be no restart or heap panic.
4. Leave a loading JPEG and open a PNG, GIF, or text file. The old JPEG must
   never replace the new preview.
5. Open the invalid JPEG, then repeat while leaving during loading. An active
   request should show a decode error; a cancelled request must stay closed.
6. Return home, reopen the media library, and play an audio file. Leaving the
   music player must return to the media library; file-manager playback must
   return to its source directory.

The serial log records `JPEG preview shown` for committed images and
`JPEG preview finished` with the request generation, whether it is still
current, and the worker's minimum free stack in bytes. A cancelled request may
finish, but must not produce a later `shown` event. After leaving all previews
and allowing workers to exit, RAM use should settle instead of growing with
each cycle. `LVGL task` stack overflow, watchdog resets, or allocator errors
fail the check. Record which cases were actually performed, not just boot logs.
