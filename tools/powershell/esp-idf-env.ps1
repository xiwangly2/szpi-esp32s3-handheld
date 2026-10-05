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

$idfToolsPy = Join-Path $env:IDF_PATH "tools\idf_tools.py"
if (-not (Test-Path -LiteralPath $idfToolsPy)) {
    throw "Cannot find idf_tools.py under IDF_PATH: $env:IDF_PATH"
}

# Let the selected IDF resolve supported tool versions, not the newest directory.
$idfExports = & $EspIdfPython $idfToolsPy export --format key-value
if ($LASTEXITCODE -ne 0) {
    throw "ESP-IDF tool export failed. Install the tools required by $env:IDF_PATH."
}
foreach ($line in $idfExports) {
    if ($line -match '^([A-Z][A-Z0-9_]*)=(.*)$') {
        $name = $Matches[1]
        $value = $Matches[2]
        if ($name -eq "PATH") {
            $value = $value.Replace('%PATH%', $env:PATH)
        }
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}

if (-not $env:IDF_PYTHON_CHECK_CONSTRAINTS) {
    $env:IDF_PYTHON_CHECK_CONSTRAINTS = "0"
}
