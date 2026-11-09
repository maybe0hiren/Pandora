@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0Pandora.exe" (
  echo Pandora.exe was not found beside this launcher.
  pause
  exit /b 1
)
start "" "%~dp0Pandora.exe"
endlocal
