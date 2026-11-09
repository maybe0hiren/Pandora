$ErrorActionPreference = 'Stop'

if (-not $env:QT_DIR) {
    throw 'Set QT_DIR to your Qt MSVC kit folder first, e.g. $env:QT_DIR = "C:\Qt\6.8.3\msvc2022_64"'
}
$exe = Join-Path $PSScriptRoot 'build\Release\Pandora.exe'
$windeployqt = Join-Path $env:QT_DIR 'bin\windeployqt.exe'
if (-not (Test-Path $exe)) { throw "Cannot find $exe. Build Pandora first." }
if (-not (Test-Path $windeployqt)) { throw "Cannot find $windeployqt. Check QT_DIR: $env:QT_DIR" }
$out = Join-Path $PSScriptRoot 'Pandora-Portable'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item $exe $out -Force
& $windeployqt --release --compiler-runtime --dir $out (Join-Path $out 'Pandora.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }
Copy-Item (Join-Path $PSScriptRoot 'README.md') (Join-Path $out 'README.txt') -Force
$zip = Join-Path $PSScriptRoot 'Pandora-Portable-Windows-x64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -Force
Write-Host "Portable package created: $zip" -ForegroundColor Green
