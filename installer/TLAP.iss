; TLAP installer - Inno Setup 6 (per-user, no admin)
; Build:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\TLAP.iss
; Needs build\TLAP.exe (run build.bat first, or just release.bat). Output: dist\TLAPSetup-<version>.exe
; The version is read from the built exe (set in src\version.h).

#define MyAppName "TLAP"
#define MyAppVersion GetStringFileInfo("..\build\TLAP.exe", PRODUCT_VERSION)
#define MyAppPublisher "Micromochi_Teno"
#define MyAppId "MicromochiTeno.TLAP"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
ShowLanguageDialog=yes
InfoAfterFile=Readme.txt
OutputDir=..\dist
OutputBaseFilename=TLAPSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
; Tells the shell that file associations changed when install/uninstall finishes.
ChangesAssociations=yes
VersionInfoVersion={#MyAppVersion}
UninstallDisplayIcon={app}\TLAP.exe
; Close a running TLAP before replacing files.
CloseApplications=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
; Chinese files come from the official Inno Setup repository (jrsoftware/issrc, Files/Languages), bundled here
Name: "zhcn"; MessagesFile: "ChineseSimplified.isl"
Name: "zhtw"; MessagesFile: "ChineseTraditional.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\TLAP.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "Readme.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
; Bundled shaders and README. Only these files are removed on uninstall; .hlsl files the user adds stay.
Source: "..\visualizers\*"; DestDir: "{app}\visualizers"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\TLAP.exe"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\TLAP.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\TLAP.exe"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Settings written next to the exe by the app.
Type: files; Name: "{app}\TLAP.ini"
; Removed only when empty (keeps shaders the user added).
Type: dirifempty; Name: "{app}\visualizers"
Type: dirifempty; Name: "{app}"

[Registry]
; "Open with" candidate only. The default app of any extension is not touched.
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "TLAP"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\TLAP.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".mp3"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".m4a"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".aac"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".flac"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".wav"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".wma"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".ogg"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".oga"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\TLAP.exe\SupportedTypes"; ValueType: string; ValueName: ".opus"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\TLAP.Audio"; ValueType: string; ValueName: ""; ValueData: "TLAP Audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\TLAP.Audio\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\TLAP.exe,0"
Root: HKCU; Subkey: "Software\Classes\TLAP.Audio\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\TLAP.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\.mp3"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.mp3\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.mp3\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.m4a"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.m4a\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.m4a\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.aac"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.aac\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.aac\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.flac"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.flac\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.flac\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.wav"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.wav\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.wav\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.wma"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.wma\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.wma\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.ogg"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.ogg\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.ogg\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.oga"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.oga\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.oga\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.opus"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.opus\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.opus\OpenWithProgids"; ValueType: string; ValueName: "TLAP.Audio"; ValueData: ""; Flags: uninsdeletevalue
