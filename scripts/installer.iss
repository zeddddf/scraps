; Inno Setup script for Scraps
; Build: ISCC.exe scripts\installer.iss  (from repo root context)

#define MyAppName "Scraps"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "zeddddf"
#define MyAppExeName "Scraps.exe"

[Setup]
AppId={{7E2A6C93-5B14-4F8D-9A70-SCRAPS01DOWNLOAD}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/zeddddf/scraps
AppSupportURL=https://github.com/zeddddf/scraps/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
SetupIconFile=..\assets\brand\app.ico
OutputDir=..\dist
OutputBaseFilename=Scraps-{#MyAppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
; 剪贴板管理器按当前用户安装（{autopf} 解析到 %LOCALAPPDATA%\Programs），
; 无需 UAC，静默安装不弹任何 UI（winget 校验要求无交互）。
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
UninstallDisplayIcon={app}\{#MyAppExeName}

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional tasks:"

[Files]
Source: "..\build\Release\Scraps.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\scraps-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\scraps_screenshot.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{#MyAppName} CLI"; Filename: "{app}\scraps-cli.exe"; Parameters: "--help"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Nothing: user data in %LOCALAPPDATA%\Scraps is intentionally preserved on uninstall.
