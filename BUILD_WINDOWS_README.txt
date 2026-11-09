PANDORA WINDOWS BUILD / PACKAGING GUIDE

This repository currently contains a proof-of-concept implementation, not the
finished distributed-storage system specified by the project requirements.
The scripts below automate compilation and runtime bundling; they do not add
missing distributed-system functionality.

KNOWN DEVELOPMENT MACHINE CONFIGURATION
  Qt:       C:\Qt\6.12.0\mingw_64
  MinGW:    C:\Qt\Tools\mingw1310_64
  CMake:    C:\Qt\Tools\CMake_64

BUILD + PORTABLE PACKAGE
  1. Install the matching Qt 6 MinGW kit, MinGW compiler and CMake.
  2. Open the Pandora project directory.
  3. Double-click build_and_package_windows.bat.
     Or run the two PowerShell scripts separately.
  4. The ZIP should be created at:
       dist\Pandora-Portable-Windows-x64.zip
  5. Extract it on a clean Windows 11 x64 PC and run Pandora.exe.
  6. Test create/join, file transfer, firewall behaviour and shutdown on at
     least two physical computers before distributing it.

OPTIONAL INSTALLER
  Install Inno Setup 6, open Pandora_Setup.iss and click Compile.
  The installer output is placed in dist\installer.

CUSTOM QT LOCATIONS
  Set QT_ROOT, MINGW_ROOT and CMAKE_ROOT environment variables before running
  the scripts if your installation differs from the defaults.

PORTABILITY WARNING
  windeployqt bundles Qt runtime dependencies and the script copies the usual
  MinGW runtime DLLs. This is not proof of portability. Test on a clean Windows
  11 x64 computer with no Qt or MinGW installed. The installer is unsigned, so
  Windows SmartScreen may show an unknown-publisher warning.

PRODUCT STATUS
  The supplied application still has major missing features: authenticated
  peer join, encrypted peer transport, bidirectional peer mesh, actual remote
  replication and retrieval, replica repair, robust Guardian election,
  distributed conflict preservation, quota enforcement and coordinated
  Guardian-only shutdown. Do not use it for important or sensitive files.
