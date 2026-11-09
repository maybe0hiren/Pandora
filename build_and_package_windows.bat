@echo off
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows_mingw.ps1"
if errorlevel 1 goto :failed

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_windows_mingw.ps1"
if errorlevel 1 goto :failed

echo.
echo Pandora build and portable package steps completed.
echo See dist\Pandora-Portable-Windows-x64.zip
pause
exit /b 0

:failed
echo.
echo Build or packaging failed. Read the error above.
pause
exit /b 1
