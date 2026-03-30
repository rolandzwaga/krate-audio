; Gradus VST3 Plugin Installer
; Inno Setup Script
;
; This script is compiled by the release workflow using Inno Setup
; to create a Windows installer for the Gradus VST3 plugin.

#define AppName "Gradus"
#define AppPublisher "Krate Audio"
#define AppURL "https://github.com/rolandzwaga/krate-audio"
#define AppCopyright "Copyright (c) 2026 Krate Audio"
; Version is passed from the release workflow via /DVersion=x.x.x
#ifndef Version
  #define Version "0.0.0"
#endif

[Setup]
; Unique application ID (GUID) - generated for Gradus
AppId={{D4F7A2B6-8C3E-4F1D-A5E9-7B0D3C6F8A2E}
AppName={#AppName}
AppVersion={#Version}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
AppCopyright={#AppCopyright}

; Installation directory - VST3 standard location with vendor folder
DefaultDirName={commonpf}\Common Files\VST3\Krate Audio\Gradus
; No user-selectable program group needed for a plugin
DisableProgramGroupPage=yes

; Output file naming
OutputDir=.
OutputBaseFilename=Gradus-{#Version}-Windows-x64

; Compression settings
Compression=lzma2
SolidCompression=yes

; Require admin privileges for system-wide installation
PrivilegesRequired=admin

; Allow upgrade installations without uninstall
UsePreviousAppDir=yes

; Installer appearance
WizardStyle=modern
SetupIconFile=compiler:SetupClassicIcon.ico

; Show changelog before installation
InfoBeforeFile=CHANGELOG.txt

; Architecture
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Copy the entire Gradus.vst3 bundle recursively
; Source path is relative to where the workflow runs (artifact extraction location)
Source: "Gradus.vst3\*"; DestDir: "{app}\Gradus.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; Install changelog with the plugin
Source: "CHANGELOG.txt"; DestDir: "{app}\Gradus.vst3"; Flags: ignoreversion
; Factory presets - install to system-wide presets location
; {commonappdata} = C:\ProgramData on modern Windows
Source: "presets\*"; DestDir: "{commonappdata}\Krate Audio\Gradus"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; No desktop icons needed for a VST plugin

[Registry]
; No registry entries needed - VST3 hosts scan the standard directory

[UninstallDelete]
; Clean up the entire plugin directory on uninstall
Type: filesandordirs; Name: "{app}\Gradus.vst3"

[Code]
// Display a message after installation
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Installation complete - no action needed
  end;
end;
