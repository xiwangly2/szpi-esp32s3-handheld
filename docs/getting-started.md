# Getting Started

## Hardware

- Board: SZPI ESP32-S3 handheld board
- Target: ESP32-S3
- Flash: 16 MB
- PSRAM: 8 MB octal PSRAM
- Display: board LCD through the project BSP
- Touch: FT5x06
- Storage: TF card, FAT32 or exFAT

## Toolchain

This project has been verified locally with ESP-IDF v5.4.4 on Windows.

Run the project doctor first:

```powershell
.\tools\powershell\doctor.ps1
```

```powershell
cd firmware\handheld
idf.py -B build_lvgl set-target esp32s3
idf.py -B build_lvgl build
```

Flash and monitor:

```powershell
idf.py -B build_lvgl -p COM7 flash monitor
```

Use the real serial port for your board. On Windows, check Device Manager if
COM7 is not present.

From the repository root, the helper script keeps the build directory stable:

```powershell
.\tools\powershell\idf.ps1 build
.\tools\powershell\idf.ps1 -Port COM7 flash monitor
```

## Configuration

The source tree commits `sdkconfig.defaults`, not the generated `sdkconfig`.
Local `sdkconfig` may contain WiFi credentials or machine-specific choices and
is intentionally ignored.

Common project options are under `Random Image LVGL App` in menuconfig:

```powershell
idf.py -B build_lvgl menuconfig
```

## Build Output

Normal build artifacts stay under `firmware/handheld/build_lvgl` and should not
be committed. Create a single merged firmware binary with:

```powershell
idf.py -B build_lvgl merge-bin
```

Or from the repository root:

```powershell
.\tools\powershell\package-release.ps1
```
