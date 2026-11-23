
; Pandora Windows Installer
; Requires Inno Setup 6 or newer.
; Build the portable package before compiling this installer.

#define MyAppName "Pandora"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Pandora Project"
#define MyAppExeName "Pandora.exe"
#define DistDir "dist\Pandora-Portable"

[Setup]
AppId={{D4D6E0D2-9A37-4D4A-9A15-5C4F3B1A9E70}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}

DefaultDirName={localappdata}\Programs\Pandora
DefaultGroupName=Pandora
DisableProgramGroupPage=yes

PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

OutputDir=dist\installer
OutputBaseFilename=Pandora-Setup-Windows-x64

SetupIconFile=assets\pandora.ico
UninstallDisplayIcon={app}\Pandora.exe

WizardStyle=modern
Compression=lzma2
SolidCompression=yes

Uninstallable=yes
CloseApplications=yes
RestartApplications=no

VersionInfoVersion=1.0.0.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Pandora Peer-to-Peer Cloud Storage
VersionInfoProductName={#MyAppName}

; Preserve existing installation data during upgrades.
UsePreviousAppDir=yes
UsePreviousGroup=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Pandora"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Pandora"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Pandora"; Flags: postinstall nowait skipifsilent
