param(
    [Parameter(Mandatory = $true)]
    [string]$Drive,
    [string]$Ssid = "YOUR_2G_SSID",
    [string]$Password = "YOUR_WIFI_PASSWORD",
    [string]$ApiUrl = "https://example.com/image.php?key=change-me&return=print&esp=1"
)

$ErrorActionPreference = "Stop"

$root = $Drive.TrimEnd("\")
if ($root.Length -eq 2 -and $root[1] -eq ":") {
    $root += "\"
}

if (-not (Test-Path -LiteralPath $root)) {
    throw "Drive path does not exist: $root"
}

$configDir = Join-Path $root "szpi\config"
$cacheDir = Join-Path $root "szpi\cache\random"
$photoDir = Join-Path $root "szpi\photos"
$musicDir = Join-Path $root "szpi\music"

New-Item -ItemType Directory -Force -Path $configDir, $cacheDir, $photoDir, $musicDir | Out-Null

$wifiIni = Join-Path $configDir "wifi.ini"
@"
ssid=$Ssid
password=$Password
api_url=$ApiUrl
"@ | Set-Content -LiteralPath $wifiIni -Encoding UTF8

Write-Host "TF card layout prepared under $root"
Write-Host "WiFi config: $wifiIni"
Write-Host "Edit wifi.ini if you do not want to pass secrets on the command line."
