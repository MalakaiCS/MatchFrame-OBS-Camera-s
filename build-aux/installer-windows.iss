; MatchFrame Multiview - Windows installer (Inno Setup 6).
;
; Built by .github/scripts/Package-Windows.ps1, which passes:
;   /DPluginName=matchframe-multiview  /DPluginVersion=1.0.0
;   /DSourceDir=<release\Config\matchframe-multiview>  /DOutputDir=<release>  /DOutputName=<file name>
;
; Installs into %ProgramData%\obs-studio\plugins\<PluginName>, the machine-wide plugin
; folder OBS Studio 30+ loads automatically (bin\64bit\<name>.dll + data\).

#ifndef PluginName
  #define PluginName "matchframe-multiview"
#endif
#ifndef PluginVersion
  #define PluginVersion "0.0.0"
#endif

[Setup]
AppId={{6F1C2A7E-4B3D-4E59-9C21-5A8D0E7B3F14}
AppName=MatchFrame Multiview for OBS Studio
AppVersion={#PluginVersion}
AppVerName=MatchFrame Multiview {#PluginVersion}
AppPublisher=MatchFrame
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=MatchFrame Multiview for OBS Studio
CloseApplications=yes
CloseApplicationsFilter=obs64.exe
RestartApplications=no

[Messages]
WelcomeLabel2=This installs the MatchFrame Multiview plugin (version {#PluginVersion}) for OBS Studio 30 or newer.%n%nPlease close OBS Studio before continuing. After installation, open OBS and choose Tools > MatchFrame Multiview.

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: dirifempty; Name: "{app}"
