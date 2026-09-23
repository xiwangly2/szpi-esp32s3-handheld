# VS Code ESP-IDF

Recommended extension:

- Espressif IDF extension for VS Code

Open this folder as the workspace root, then use the firmware project folder:

```text
firmware/handheld
```

The repository intentionally does not commit `.vscode/settings.json`, because
that file usually contains local paths and serial ports. A typical local setup
uses:

```json
{
  "idf.currentSetup": "C:\\esp\\v5.4.4\\esp-idf",
  "idf.flashType": "UART",
  "idf.portWin": "COM7"
}
```

Change the IDF path and COM port for your machine. After the extension is
configured, the usual Build, Flash, and Monitor buttons should map to:

```powershell
idf.py -B build_lvgl build
idf.py -B build_lvgl -p COM7 flash
idf.py -B build_lvgl -p COM7 monitor
```

The repository also provides VS Code tasks that call the checked-in helper
scripts without storing machine-specific settings:

- `Project: Doctor`
- `ESP-IDF: Build handheld`
- `ESP-IDF: Flash handheld`
- `ESP-IDF: Monitor handheld`
- `ESP-IDF: Flash and monitor handheld`
- `ESP-IDF: Merge release binary`
