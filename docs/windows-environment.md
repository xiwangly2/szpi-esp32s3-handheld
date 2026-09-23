# Windows Environment Notes

This project is intended to build from a normal PowerShell session through the
checked-in scripts under `tools/powershell`.

## Recommended Setup

- Keep ESP-IDF installs versioned, for example `C:\esp\v5.4.4\esp-idf`.
- Keep ESP-IDF tools under `C:\Espressif`.
- Avoid relying on global ESP-IDF entries in User or Machine `Path`.
- Let `tools/powershell/idf.ps1` select the project toolchain.

Run:

```powershell
.\tools\powershell\doctor.ps1
```

The doctor script checks the firmware directory, ESP-IDF path, Python venv,
serial ports, and common Windows upgrade leftovers.

## Why Not Put ESP-IDF Tools Globally In PATH

ESP-IDF projects are sensitive to toolchain versions. If global `Path` contains
tools from another IDF installation, for example a v6.x Python venv or gdb, a
v5.4.x project may pick up mixed tools. This can lead to confusing build and
flash errors.

The helper scripts set the required paths only for the current command.

## PROCESSOR_ARCHITECTURE

Some upgraded or hosted Windows shells may miss the standard
`PROCESSOR_ARCHITECTURE` environment variable. ESP-IDF uses Python platform
detection and can then report a platform like `Windows-`.

The project scripts patch this variable for the current process when it is
missing. If your own terminal repeatedly shows this problem outside the project
scripts, create a PowerShell profile entry:

```powershell
if (-not $env:PROCESSOR_ARCHITECTURE) {
    $env:PROCESSOR_ARCHITECTURE = "AMD64"
}
```

Do not add this unless you actually see the problem in your normal terminal.
