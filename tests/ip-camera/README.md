# IP Camera Tests

From the repository root on Windows:

```powershell
cmake -S tests/ip-camera -B tests/ip-camera/build -G "Visual Studio 17 2022" -A x64 -DIDF_PATH=C:/esp/v5.4.4/esp-idf
cmake --build tests/ip-camera/build --config Debug
ctest --test-dir tests/ip-camera/build -C Debug --output-on-failure
```

Tests use the actual bundled multipart parser and pinned IDF HTTP URL parser.
They exercise every two-chunk split point, 1-33-byte reads, binary JPEG-like
payloads, MIME parameters, malformed lengths, size limits, truncation, scaling
bounds and consumer cancellation. Synthetic payloads test framing, not JPEG
pixel decoding. No server is opened and no camera is contacted by these tests.

Device acceptance with a configured test endpoint: JPEG snapshots, MJPEG with
and without Content-Length, correct letterboxing/colors, pause/reconnect,
full-screen controls, Save/reopen persistence, TF import/cancel, Back during
connection and repeated reopen. Test 401/403/404, invalid MIME, oversized JPEG,
truncated stream, stalled input, Wi-Fi loss and TLS failure. Recheck local video
playback afterward. Do not put private camera URLs or captures in test output.
