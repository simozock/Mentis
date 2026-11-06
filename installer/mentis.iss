; Windows installer of Mentis (Inno Setup 6, https://jrsoftware.org/ishelp/).
; Built by .github/workflows/release.yml from the folder of "cmake --install":
;   ISCC /DAppVersion=X.Y.Z /DStageDir=<folder> installer\mentis.iss
; Per user, no administrator (FEAT-090): %LOCALAPPDATA%\Programs\Mentis, so an update installs
; without a UAC prompt. The app updates itself by running this installer with
; /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RELAUNCH=1 (src/updates.cpp).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\stage"
#endif

[Setup]
; The AppId identifies the installation for updates and uninstall: never change it.
AppId={{7945E710-5449-4F64-9516-A04F44E196E9}
AppName=Mentis
AppVersion={#AppVersion}
AppVerName=Mentis {#AppVersion}
AppPublisher=Mentis
AppPublisherURL=https://github.com/simozock/Mentis
AppSupportURL=https://github.com/simozock/Mentis/issues
AppUpdatesURL=https://github.com/simozock/Mentis/releases
; The setup's version information (SignPath checks product name and version).
VersionInfoVersion={#AppVersion}
VersionInfoProductName=Mentis
VersionInfoProductVersion={#AppVersion}
VersionInfoCompany=Mentis
VersionInfoDescription=Mentis setup
; With PrivilegesRequired=lowest {autopf} is {userpf}: %LOCALAPPDATA%\Programs.
PrivilegesRequired=lowest
DefaultDirName={autopf}\Mentis
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
SetupIconFile=..\assets\appicon\mentis.ico
UninstallDisplayIcon={app}\bin\Mentis.exe
; Explorer refreshes its icons at the end: the shortcuts point to the same Mentis.exe path, so after
; an update whose icon changed they kept the old one from Windows' icon cache.
ChangesAssociations=yes
OutputDir=..\dist
OutputBaseFilename=Mentis-{#AppVersion}-setup-x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; A running Mentis is closed before its files are replaced. force: also "Mentis --mcp" started by an
; agent, which has no window to answer a close request; otherwise a silent update would abort.
CloseApplications=force

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Libraries and plugins of the previous version that the new one no longer has. Only over an
; existing installation: a first install into a folder the user chose never deletes anything there.
Type: filesandordirs; Name: "{app}\bin"; Check: IsUpgrade
Type: filesandordirs; Name: "{app}\plugins"; Check: IsUpgrade
Type: filesandordirs; Name: "{app}\translations"; Check: IsUpgrade

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Mentis"; Filename: "{app}\bin\Mentis.exe"
Name: "{autodesktop}\Mentis"; Filename: "{app}\bin\Mentis.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\Mentis.exe"; Description: "{cm:LaunchProgram,Mentis}"; Flags: nowait postinstall skipifsilent
; The update started from the app: open Mentis again after the silent install.
Filename: "{app}\bin\Mentis.exe"; Flags: nowait; Check: ShouldRelaunch

[Code]
function IsUpgrade: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\unins000.exe'));
end;

function ShouldRelaunch: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;
