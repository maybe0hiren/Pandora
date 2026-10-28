param(
 [string]$QtKit = 'C:\Qt\6.11.2\mingw_64',
 [string]$CompilerBin = 'C:\Qt\Tools\mingw1310_64\bin',
 [string]$Ninja = 'C:\Qt\Tools\Ninja\ninja.exe'
)
$ErrorActionPreference = 'Stop'
$env:PATH = "$CompilerBin;$QtKit\bin;" + $env:PATH
$sourceRoot = Split-Path $PSScriptRoot -Parent
cmake -S $sourceRoot -B "$sourceRoot\build" -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$CompilerBin\g++.exe" "-DCMAKE_PREFIX_PATH=$QtKit"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build "$sourceRoot\build"
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir "$sourceRoot\build" -L unit --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed' }
Write-Host 'Build and protocol tests passed. Run ctest --test-dir build -L integration to test sockets.'

