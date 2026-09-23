$ErrorActionPreference = "Stop"

$repo = Resolve-Path "$PSScriptRoot\..\.."
$project = Join-Path $repo "firmware\handheld"

Write-Host "Repository: $repo"
Write-Host "Firmware:   $project"

if (-not (Test-Path -LiteralPath $project)) {
    throw "Firmware project not found."
}

. "$PSScriptRoot\esp-idf-env.ps1"

$idfPy = Join-Path $env:IDF_PATH "tools\idf.py"
if (-not (Test-Path -LiteralPath $idfPy)) {
    throw "idf.py not found under IDF_PATH: $env:IDF_PATH"
}

Write-Host "ESP-IDF:    $env:IDF_PATH"
Write-Host "IDF tools:  $env:IDF_TOOLS_PATH"
Write-Host "Python:     $EspIdfPython"
& $EspIdfPython $idfPy --version
if ($LASTEXITCODE -ne 0) {
    throw "idf.py --version failed."
}

Write-Host ""
Write-Host "Detected serial ports:"
$ports = @()
try {
    $ports = @(Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Description)
    $ports | Format-Table -AutoSize
}
catch {
    Write-Host "Could not list serial ports through WMI: $($_.Exception.Message)"
}

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$userEspEntries = @($userPath -split ';' | Where-Object { $_ -match '\\Espressif\\tools\\' })
$idfRoots = @()
if (Test-Path -LiteralPath "C:\esp") {
    $idfRoots = @(Get-ChildItem -LiteralPath "C:\esp" -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
}

Write-Host ""
Write-Host "Environment notes:"
if (-not $EspIdfEnvHadProcessorArchitecture) {
    Write-Host "  - PROCESSOR_ARCHITECTURE was missing in this shell; patched to $env:PROCESSOR_ARCHITECTURE for ESP-IDF."
}
if ($idfRoots.Count -gt 1) {
    Write-Host "  - Multiple ESP-IDF installs found: $($idfRoots -join ', ')"
}
if ($userEspEntries.Count -gt 0) {
    Write-Host "  - User PATH contains ESP-IDF tool entries:"
    foreach ($entry in $userEspEntries) {
        Write-Host "    $entry"
    }
    Write-Host "    Project scripts set their own tool paths, so global ESP-IDF PATH entries are optional and can cause version mixing."
}
if ($userEspEntries.Count -eq 0 -and $idfRoots.Count -le 1 -and $EspIdfEnvHadProcessorArchitecture) {
    Write-Host "  - No obvious ESP-IDF PATH/version issue detected."
}

$examplePort = if ($ports.Count -gt 0) { $ports[0].DeviceID } else { "COM7" }

Write-Host ""
Write-Host "Useful next commands:"
Write-Host "  .\tools\powershell\idf.ps1 build"
Write-Host "  .\tools\powershell\idf.ps1 -Port $examplePort flash monitor"
Write-Host "  .\tools\powershell\prepare-tf-card.ps1 -Drive E:"
