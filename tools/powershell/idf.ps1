[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Project = "$PSScriptRoot\..\..\firmware\handheld",
    [string]$BuildDir = "build_lvgl",
    [string]$Port = "",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

. "$PSScriptRoot\esp-idf-env.ps1"

$idfPy = Join-Path $env:IDF_PATH "tools\idf.py"
if (-not (Test-Path -LiteralPath $idfPy)) {
    throw "Cannot find idf.py under IDF_PATH: $env:IDF_PATH"
}

if ($IdfArgs.Count -eq 0) {
    $IdfArgs = @("build")
}

Push-Location $Project
try {
    $args = @("-B", $BuildDir)
    if ($Port) {
        $args += @("-p", $Port)
    }
    $args += $IdfArgs
    & $EspIdfPython $idfPy @args
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
