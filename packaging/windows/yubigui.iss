#ifndef MyAppVersion
#define MyAppVersion "0.1.4"
#endif

#define MyAppName "YubiGUI"
#define MyAppExeName "yubigui.exe"
#define MyStage "..\..\build\windows-dist"

[Setup]
AppId={{E9A73139-762B-49CA-AFD5-9DD9188A2CD6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=Stephan Winter
AppPublisherURL=https://github.com/Target42/YubiGUI
AppSupportURL=https://github.com/Target42/YubiGUI
AppUpdatesURL=https://github.com/Target42/YubiGUI/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=YubiGUI-{#MyAppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\icons\yubigui.ico
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany=Stephan Winter
VersionInfoDescription=YubiKey-Verwaltung
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}.0
MinVersion=10.0
InfoBeforeFile=hinweis.txt
LicenseFile=..\..\LICENSE

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktopicon"; Description: "Verknüpfung auf dem Desktop anlegen"; GroupDescription: "Zusätzliche Aufgaben:"; Flags: unchecked

[Files]
Source: "{#MyStage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{#MyAppName} starten"; Flags: nowait postinstall skipifsilent
