; Produces InSequence-FX-Windows-Setup.exe: shows the EULA (must be accepted), then installs
; InSequence FX (with its FFmpeg DLLs) into Program Files with Start-menu and optional desktop shortcuts.
#define MyAppName "InSequence FX"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "HumHouse"
#define MyAppURL "https://github.com/elijahjfrierson-prog/insequence-fx"
#define MyAppExeName MyAppName + ".exe"

[Setup]
AppId={{7D2A9C41-3B6E-4F0A-8C55-INSEQUENCEFX}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\HumHouse\{#MyAppName}
DefaultGroupName=HumHouse
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE.txt
OutputDir=..\..\
OutputBaseFilename=InSequence-FX-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\..\assets\insequence.ico
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
CloseApplications=force
RestartApplications=no
UsePreviousAppDir=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
ChangesAssociations=yes

[InstallDelete]
Type: files; Name: "{app}\{#MyAppExeName}"

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "..\..\build\InSequenceFX_artefacts\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\ffmpeg\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
Root: HKA; Subkey: "Software\Classes\.isfx"; ValueType: string; ValueName: ""; ValueData: "InSequenceFX.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\InSequenceFX.Project"; ValueType: string; ValueName: ""; ValueData: "InSequence FX Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\InSequenceFX.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
