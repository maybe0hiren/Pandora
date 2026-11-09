$ErrorActionPreference = "Stop"
$exe = Join-Path $PSScriptRoot "build\Release\Pandora.exe"
if (-not (Test-Path $exe)) {
    throw "Pandora.exe not found. Run .\build_windows.ps1 first."
}
if ($env:QT_DIR) {
    $qtBin = Join-Path $env:QT_DIR "bin"
    if (Test-Path $qtBin) { $env:PATH = "$qtBin;$env:PATH" }
}
& $exe
