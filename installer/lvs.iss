; ---------------------------------------------------------------------------
; SOLO WORSHIPER - Inno Setup 6 script
; Build with:  iscc installer\lvs.iss     (or tools\installer.bat)
; Requires build\SoloWorshiper.exe and build\WebView2Loader.dll
; ---------------------------------------------------------------------------
#define AppName        "Solo Worshiper"
#define AppShortName   "SoloWorshiper"
#define AppVersion     "1.0.0"
#define AppPublisher   "Solo Worshiper"
#define AppExe         "SoloWorshiper.exe"

[Setup]
AppId={{7C4B1E52-3E8A-4F4D-9F31-2B5D7E7A91C4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=SoloWorshiperSetup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
DisableProgramGroupPage=yes
CloseApplications=yes
RestartApplications=no
; Per-monitor DPI aware application: do not let the installer scale the exe.
SetupIconFile=..\assets\app.ico
UninstallDisplayName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\{#AppExe}";              DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\WebView2Loader.dll";    DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";                   DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\docs\*";                      DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}";            Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}";  Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";      Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The WebView2 profile and user presets live in the user profile and are kept on
; purpose: uninstalling must never delete recordings or a singer's presets.
Type: filesandordirs; Name: "{app}\docs"

[Code]
const
  WebView2Key = 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  WebView2Bootstrapper = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703';

function WebView2Installed(): Boolean;
var
  Version: String;
begin
  Result :=
    RegQueryStringValue(HKLM, WebView2Key, 'pv', Version) or
    RegQueryStringValue(HKCU, WebView2Key, 'pv', Version) or
    RegQueryStringValue(HKLM32, WebView2Key, 'pv', Version) or
    RegQueryStringValue(HKCU32, WebView2Key, 'pv', Version);
  if Result then
    Result := (Version <> '') and (Version <> '0.0.0.0');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  Bootstrapper: String;
begin
  Result := '';
  if WebView2Installed() then exit;

  // WebView2 is required for the karaoke player only, but a silent failure later
  // is worse than asking for one download now.
  if MsgBox('The Microsoft Edge WebView2 Runtime was not found.' + #13#10 +
            'It is needed for the karaoke player. Download and install it now?',
            mbConfirmation, MB_YESNO) = IDNO then
    exit;

  Bootstrapper := ExpandConstant('{tmp}\MicrosoftEdgeWebview2Setup.exe');
  try
    DownloadTemporaryFile(WebView2Bootstrapper, 'MicrosoftEdgeWebview2Setup.exe',
                          '', nil);
  except
    Result := 'The WebView2 bootstrapper could not be downloaded. Install it manually from ' +
              WebView2Bootstrapper + ' and run this setup again.';
    exit;
  end;

  if not Exec(Bootstrapper, '/silent /install', '', SW_SHOW, ewWaitUntilTerminated,
              ResultCode) or (ResultCode <> 0) then
    Result := 'The WebView2 Runtime installation failed (exit code ' +
              IntToStr(ResultCode) + '). Install it manually from ' +
              WebView2Bootstrapper + ' and run this setup again.';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    if MsgBox('Remove your saved presets and device settings as well?' + #13#10 +
              'Recordings are never removed.', mbConfirmation, MB_YESNO) = IDYES then
    begin
      DelTree(ExpandConstant('{userappdata}\SoloWorshiper'), True, True, True);
      DelTree(ExpandConstant('{localappdata}\SoloWorshiper'), True, True, True);
    end;
end;
