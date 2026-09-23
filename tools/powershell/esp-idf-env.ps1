$ErrorActionPreference = "Stop"

$EspIdfEnvHadProcessorArchitecture = [bool]$env:PROCESSOR_ARCHITECTURE
if ($IsWindows -and -not $env:PROCESSOR_ARCHITECTURE) {
    $arch = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    $env:PROCESSOR_ARCHITECTURE = switch ($arch) {
        "X64" { "AMD64" }
        "Arm64" { "ARM64" }
        default { $arch }
    }
}

function Add-PathIfExists {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) {
        return
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $parts = $env:PATH -split ';' | Where-Object { $_ }
    if ($parts -notcontains $resolved) {
        $env:PATH = "$resolved;$env:PATH"
    }
}

function Add-NewestToolPath {
    param(
        [string]$Root,
        [string]$Tool,
        [string]$SubPath = ""
    )
    $toolRoot = Join-Path $Root "tools\$Tool"
    if (-not (Test-Path -LiteralPath $toolRoot)) {
        return
    }

    $candidate = Get-ChildItem -LiteralPath $toolRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1

    if ($candidate) {
        Add-PathIfExists (Join-Path $candidate.FullName $SubPath)
        return
    }

    Add-PathIfExists (Join-Path $toolRoot $SubPath)
}

if (-not $env:IDF_PATH) {
    $defaultIdf = "C:\esp\v5.4.4\esp-idf"
    if (Test-Path -LiteralPath $defaultIdf) {
        $env:IDF_PATH = $defaultIdf
    }
}

if (-not $env:IDF_PATH) {
    throw "IDF_PATH is not set. Open an ESP-IDF shell or set IDF_PATH first."
}

if (-not $env:IDF_TOOLS_PATH) {
    if (Test-Path -LiteralPath "C:\Espressif") {
        $env:IDF_TOOLS_PATH = "C:\Espressif"
    }
    else {
        $env:IDF_TOOLS_PATH = Join-Path $HOME ".espressif"
    }
}

$idfVersion = Split-Path (Split-Path $env:IDF_PATH -Parent) -Leaf
$venvCandidates = @()
if ($env:IDF_PYTHON_ENV_PATH) {
    $venvCandidates += $env:IDF_PYTHON_ENV_PATH
}
if ($idfVersion) {
    $venvCandidates += (Join-Path $env:IDF_TOOLS_PATH "tools\python\$idfVersion\venv")
}
$pythonRoot = Join-Path $env:IDF_TOOLS_PATH "tools\python"
if (Test-Path -LiteralPath $pythonRoot) {
    $venvCandidates += Get-ChildItem -LiteralPath $pythonRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "venv\Scripts\python.exe") } |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "venv" }
}

$EspIdfPython = "python"
foreach ($venv in $venvCandidates) {
    $candidatePython = Join-Path $venv "Scripts\python.exe"
    if (Test-Path -LiteralPath $candidatePython) {
        $env:IDF_PYTHON_ENV_PATH = $venv
        $EspIdfPython = $candidatePython
        Add-PathIfExists (Join-Path $venv "Scripts")
        break
    }
}

$toolsRoot = $env:IDF_TOOLS_PATH
Add-NewestToolPath $toolsRoot "cmake" "bin"
Add-NewestToolPath $toolsRoot "ninja"
Add-NewestToolPath $toolsRoot "xtensa-esp-elf" "xtensa-esp-elf\bin"
Add-NewestToolPath $toolsRoot "xtensa-esp-elf-gdb" "xtensa-esp-elf-gdb\bin"
Add-NewestToolPath $toolsRoot "esp-clang" "bin"
Add-NewestToolPath $toolsRoot "idf-exe"
Add-NewestToolPath $toolsRoot "openocd-esp32" "openocd-esp32\bin"
Add-NewestToolPath $toolsRoot "dfu-util" "dfu-util-0.11-win64"
Add-NewestToolPath $toolsRoot "ccache" "ccache-4.12.1-windows-x86_64"

if (-not $env:ESP_ROM_ELF_DIR) {
    $romRoot = Join-Path $toolsRoot "tools\esp-rom-elfs"
    if (Test-Path -LiteralPath $romRoot) {
        $rom = Get-ChildItem -LiteralPath $romRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if ($rom) {
            $env:ESP_ROM_ELF_DIR = $rom.FullName
        }
    }
}

if (-not $env:IDF_PYTHON_CHECK_CONSTRAINTS) {
    $env:IDF_PYTHON_CHECK_CONSTRAINTS = "0"
}
