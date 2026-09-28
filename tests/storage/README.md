# Storage Layout Checks

These host tests compile the same MBR parser used by the firmware. They cover
FAT32 MBR entries, SFD boot code that resembles an MBR, invalid signatures,
out-of-card partitions, protective GPT entries, sparse partition slots, and
64-bit LBA arithmetic. No disk or serial port is opened.

Run with a host C compiler and CMake (on Windows, Visual Studio C++ tools):

```powershell
cmake -S tests/storage -B tests/storage/build
cmake --build tests/storage/build --config Debug
ctest --test-dir tests/storage/build -C Debug --output-on-failure
```

These tests do not prove real-card mounting, writes, or touch interactions.
