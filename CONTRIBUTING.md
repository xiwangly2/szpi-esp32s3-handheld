# Contributing

This is an embedded device project, so a good contribution includes both the
code change and how it was tested.

## Local Build

```powershell
.\tools\powershell\doctor.ps1
.\tools\powershell\idf.ps1 build
```

## Device Test Notes

When changing firmware behavior, include:

- board model
- ESP-IDF version
- serial port used
- TF card file system
- feature path tested on device
- any crash log or reset reason if applicable

## Secrets

Do not commit:

- real WLAN SSID/password
- deployed `backend/image-api/config.php`
- generated `sdkconfig`
- build outputs
- personal IDE settings

Use `examples/tf-card` and `config.sample.php` for shareable templates.
