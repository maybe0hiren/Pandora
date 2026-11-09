$ErrorActionPreference = 'Stop'

if (-not $env:QT_ROOT)    { $env:QT_ROOT = 'C:\Qt\6.12.0\mingw_64' }
if (-not $env:MINGW_ROOT) { $env:MINGW_ROOT = 'C:\Qt\Tools\mingw1310_64' }

$exe = Join-Path $PSScriptRoot 'build-mingw\Pandora.exe'
$windeployqt = Join-Path $env:QT_ROOT 'bin\windeployqt.exe'
$mingwBin = Join-Path $env:MINGW_ROOT 'bin'
if (-not (Test-Path $exe)) { throw "Build Pandora first. Missing: $exe" }
if (-not (Test-Path $windeployqt)) { throw "Qt deployment tool not found: $windeployqt" }

$out = Join-Path $PSScriptRoot 'dist\Pandora-Portable'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item $exe $out

$env:Path = "$mingwBin;$env:Path"
& $windeployqt --release --compiler-runtime --no-translations --dir $out (Join-Path $out 'Pandora.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }

# Ensure common MinGW runtime DLLs are present alongside the executable.
$runtimeDlls = @('libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll')
foreach ($dll in $runtimeDlls) {
    $source = Join-Path $mingwBin $dll
    if (Test-Path $source) { Copy-Item $source $out -Force }
}

@'
Pandora - Portable Windows x64 package

Run Pandora.exe. Qt and MinGW do not need to be installed on this PC.
Allow Pandora through Windows Defender Firewall on private networks when prompted.

IMPORTANT: This package currently contains the Pandora proof-of-concept application.
It is NOT yet a complete distributed storage product. Cross-node replication,
secure authenticated joining, remote file retrieval, Guardian consensus and
automatic replica repair are not fully implemented. Do not store important or
sensitive files in this build.

Supported target: Windows 11 x64. This package has to be tested on a clean
Windows 11 computer before it can be called a verified portable release.
'@ | Set-Content -Encoding UTF8 (Join-Path $out 'README.txt')

$zip = Join-Path $PSScriptRoot 'dist\Pandora-Portable-Windows-x64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -Force
Write-Host "Portable package assembled: $zip" -ForegroundColor Green
Write-Host 'Run it on a clean Windows 11 x64 machine to verify missing runtime dependencies.' -ForegroundColor Yellow
