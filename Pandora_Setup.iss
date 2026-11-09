; Optional installer definition for Inno Setup 6.
; First run build_windows_mingw.ps1 and package_windows_mingw.ps1.
; Then compile this file with Inno Setup 6.

#define MyAppName "Pandora"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Pandora Project"
#define DistDir "dist\Pandora-Portable"

[Setup]
AppId={{D4D6E0D2-9A37-4D4A-9A15-5C4F3B1A9E70}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Pandora
DefaultGroupName=Pandora
OutputDir=dist\installer
OutputBaseFilename=Pandora-Setup-Windows-x64
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Pandora.exe
PrivilegesRequired=admin

[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Pandora"; Filename: "{app}\Pandora.exe"
Name: "{autodesktop}\Pandora"; Filename: "{app}\Pandora.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\Pandora.exe"; Description: "Launch Pandora"; Flags: postinstall nowait skipifsilent
