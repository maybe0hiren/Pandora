PANDORA WINDOWS BUILD, TESTING & PACKAGING GUIDE (v1.0.0)

KNOWN DEVELOPMENT MACHINE CONFIGURATION
  Qt:       C:\Qt\6.12.0\mingw_64
  MinGW:    C:\Qt\Tools\mingw1310_64
  CMake:    C:\Qt\Tools\CMake_64

BUILD + PORTABLE PACKAGE
  1. Install the matching Qt 6 MinGW kit, MinGW compiler, and CMake.
  2. Open the Pandora project directory.
  3. Double-click build_and_package_windows.bat, or run:
       powershell -NoProfile -ExecutionPolicy Bypass -File .\build_windows_mingw.ps1
       powershell -NoProfile -ExecutionPolicy Bypass -File .\package_windows_mingw.ps1
  4. The portable ZIP is created at:
       dist\Pandora-Portable-Windows-x64.zip
  5. Run automated unit and 3-node integration tests:
       .\build-mingw\PandoraTests.exe

OPTIONAL INSTALLER
  Install Inno Setup 6, open Pandora_Setup.iss and click Compile.
  The installer output is placed in dist\installer\Pandora-Setup-Windows-x64.exe.

FIREWALL & NETWORK REQUIREMENTS
  - UDP Port 43171: LAN session discovery broadcast.
  - TCP Port 43170 (default, configurable): Encrypted peer mesh & chunked replica transfer.
  Allow Pandora.exe on Private Networks when prompted by Windows Defender Firewall.
