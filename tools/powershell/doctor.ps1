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

$examplePort = if ($ports.Count -gt 0) { $ports[0].DeviceID } else { "COM7" }

Write-Host ""
Write-Host "Useful next commands:"
Write-Host "  .\tools\powershell\idf.ps1 build"
Write-Host "  .\tools\powershell\idf.ps1 -Port $examplePort flash monitor"
Write-Host "  .\tools\powershell\prepare-tf-card.ps1 -Drive E:"
