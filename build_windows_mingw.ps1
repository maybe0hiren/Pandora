$ErrorActionPreference = 'Stop'

# Pandora Windows x64 build helper for the Qt MinGW kit used by this project.
# Override these environment variables if Qt is installed elsewhere:
#   $env:QT_ROOT = 'C:\Qt\6.12.0\mingw_64'
#   $env:MINGW_ROOT = 'C:\Qt\Tools\mingw1310_64'
#   $env:CMAKE_ROOT = 'C:\Qt\Tools\CMake_64'

if (-not $env:QT_ROOT)   { $env:QT_ROOT   = 'C:\Qt\6.12.0\mingw_64' }
if (-not $env:MINGW_ROOT){ $env:MINGW_ROOT= 'C:\Qt\Tools\mingw1310_64' }
if (-not $env:CMAKE_ROOT){ $env:CMAKE_ROOT= 'C:\Qt\Tools\CMake_64' }

$cmake = Join-Path $env:CMAKE_ROOT 'bin\cmake.exe'
$gpp = Join-Path $env:MINGW_ROOT 'bin\g++.exe'
$gcc = Join-Path $env:MINGW_ROOT 'bin\gcc.exe'
$make = Join-Path $env:MINGW_ROOT 'bin\mingw32-make.exe'
$qtConfig = Join-Path $env:QT_ROOT 'lib\cmake\Qt6\Qt6Config.cmake'

foreach ($path in @($cmake, $gpp, $gcc, $make, $qtConfig)) {
    if (-not (Test-Path $path)) { throw "Required build component not found: $path`nCheck QT_ROOT, MINGW_ROOT and CMAKE_ROOT." }
}

$env:Path = "$(Join-Path $env:QT_ROOT 'bin');$(Join-Path $env:MINGW_ROOT 'bin');$(Join-Path $env:CMAKE_ROOT 'bin');$env:Path"

& $cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build-mingw') -G 'MinGW Makefiles' `
    "-DCMAKE_MAKE_PROGRAM=$make" `
    "-DCMAKE_C_COMPILER=$gcc" `
    "-DCMAKE_CXX_COMPILER=$gpp" `
    "-DQt6_DIR=$(Join-Path $env:QT_ROOT 'lib\cmake\Qt6')" `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& $cmake --build (Join-Path $PSScriptRoot 'build-mingw') --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Pandora compilation failed.' }

$exe = Join-Path $PSScriptRoot 'build-mingw\Pandora.exe'
if (-not (Test-Path $exe)) { throw "Build completed but executable was not found: $exe" }
Write-Host "Build succeeded: $exe" -ForegroundColor Green
