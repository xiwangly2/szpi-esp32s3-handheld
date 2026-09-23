param(
    [string]$Name = "handheld_lvgl_esp32s3"
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path "$PSScriptRoot\..\.."
$project = Join-Path $repo "firmware\handheld"
$build = Join-Path $project "build_lvgl"
$merged = Join-Path $build "merged-binary.bin"
$release = Join-Path $repo "release"

Push-Location $project
try {
    & "$PSScriptRoot\idf.ps1" merge-bin
    if ($LASTEXITCODE -ne 0) {
        throw "merge-bin failed."
    }
}
finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $merged)) {
    throw "Merged binary was not created: $merged"
}

New-Item -ItemType Directory -Force -Path $release | Out-Null
$out = Join-Path $release "$Name.bin"
Copy-Item -LiteralPath $merged -Destination $out -Force

$hash = (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash
$size = (Get-Item -LiteralPath $out).Length
$manifest = Join-Path $release "$Name.sha256.txt"
"$hash  $Name.bin" | Set-Content -LiteralPath $manifest -Encoding ASCII

Write-Host "Release binary: $out"
Write-Host "Size: $size bytes"
Write-Host "SHA256: $hash"
