$ErrorActionPreference = "Stop"

Write-Host "Pandora Windows build helper" -ForegroundColor Cyan
Write-Host "Requires Visual Studio 2022 C++ workload, CMake, and Qt 6 (MSVC 2022 64-bit)." 
Write-Host "If Qt is not in the default location, set QT_DIR to the Qt kit path, e.g."
Write-Host '  $env:QT_DIR = "C:\Qt\6.8.3\msvc2022_64"'
Write-Host ""

if (-not $env:QT_DIR) {
    $candidates = @(
        "C:\Qt\6.10.0\msvc2022_64",
        "C:\Qt\6.9.3\msvc2022_64",
        "C:\Qt\6.8.3\msvc2022_64",
        "C:\Qt\6.8.2\msvc2022_64",
        "C:\Qt\6.7.3\msvc2022_64",
        "C:\Qt\6.6.3\msvc2022_64",
        "C:\Qt\6.5.3\msvc2022_64",
        "C:\Qt\6.4.3\msvc2022_64"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate "lib\cmake\Qt6\Qt6Config.cmake")) {
            $env:QT_DIR = $candidate
            break
        }
    }
}

if (-not $env:QT_DIR -or -not (Test-Path (Join-Path $env:QT_DIR "lib\cmake\Qt6\Qt6Config.cmake"))) {
    throw "Qt 6 MSVC kit not found. Install Qt 6 with the MSVC 2022 64-bit kit, then set QT_DIR to that kit directory."
}

Write-Host "Using Qt: $env:QT_DIR"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$env:QT_DIR"
cmake --build build --config Release --parallel

$exe = Join-Path (Get-Location) "build\Release\Pandora.exe"
if (Test-Path $exe) {
    Write-Host ""
    Write-Host "Build succeeded: $exe" -ForegroundColor Green
    Write-Host "To run with Qt DLLs, use the Qt deployment tool or launch from a configured Qt environment."
} else {
    Write-Warning "Build command finished but the expected executable was not found at $exe. Check the CMake output."
}
