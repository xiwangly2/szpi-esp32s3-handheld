# Video Checks

```powershell
cmake -S tests/video -B tests/video/build
cmake --build tests/video/build --config Debug
ctest --test-dir tests/video/build -C Debug --output-on-failure
python tests/video/test_video_api.py --php C:\path\to\php.exe
```

The native suite checks long-uptime clocks, pause/resume, repeated pause calls,
and restarting the timeline. The Python suite runs the real PHP endpoint on a
temporary localhost server and validates upload/authentication, hash integrity,
duplicate protection, GET/HEAD/ranges, invalid media and traversal rejection.
It stops its server and only cleans files in its own test-build directory.

On the board, test a converted full-length video from TF, pause/resume, restart,
mute/volume, full screen, and Back while loading/playing. Open a video from a
nested file-browser directory and verify the same path is restored. Enter the
music player/recorder afterward to check codec release. Corrupt/truncated AVI,
H.264 MP4, and oversized dimensions must show errors without rebooting.

With an authorized backend, test successful download, a chunked response,
HTTP 401/404, disconnection, full card, cancellation, and cached offline playback.
The old cache must survive a failed download. Compare serial shown/dropped
counts and duration with FFprobe. Record visible/audible device results
separately from compilation and host tests.
