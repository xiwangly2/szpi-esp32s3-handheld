# Release Build

Build:

```powershell
cd firmware\handheld
idf.py -B build_lvgl build
```

Create a merged binary:

```powershell
idf.py -B build_lvgl merge-bin
```

The merged binary is generated at:

```text
firmware/handheld/build_lvgl/merged-binary.bin
```

Flash a merged binary manually:

```powershell
python -m esptool --chip esp32s3 -p COM7 -b 460800 --before default_reset --after hard_reset write_flash 0x0 build_lvgl/merged-binary.bin
```

Best practice is to upload merged binaries to GitHub/Gitea Releases rather than
committing them to the source repository.

From the repository root you can generate a release artifact and SHA256 file:

```powershell
.\tools\powershell\package-release.ps1
```

Output:

```text
release/handheld_lvgl_esp32s3.bin
release/handheld_lvgl_esp32s3.sha256.txt
```
