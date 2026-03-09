; Innexus VST3 Plugin Installer
; Inno Setup Script
;
; This script is compiled by the release workflow using Inno Setup
; to create a Windows installer for the Innexus VST3 plugin.

#define AppName "Innexus"
#define AppPublisher "Krate Audio"
#define AppURL "https://github.com/rolandzwaga/krate-audio"
#define AppCopyright "Copyright (c) 2026 Krate Audio"
; Version is passed from the release workflow via /DVersion=x.x.x
#ifndef Version
  #define Version "0.0.0"
#endif

[Setup]
; Unique application ID (GUID) - generated for Innexus
AppId={{A3C8F5E1-7B2D-4E6A-91D3-5F0C2A8E4D7B}
AppName={#AppName}
AppVersion={#Version}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
AppCopyright={#AppCopyright}

; Installation directory - VST3 standard location with vendor folder
DefaultDirName={commonpf}\Common Files\VST3\Krate Audio\Innexus
; No user-selectable program group needed for a plugin
DisableProgramGroupPage=yes

; Output file naming
OutputDir=.
OutputBaseFilename=Innexus-{#Version}-Windows-x64

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
; Copy the entire Innexus.vst3 bundle recursively
; Source path is relative to where the workflow runs (artifact extraction location)
Source: "Innexus.vst3\*"; DestDir: "{app}\Innexus.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; Install changelog with the plugin
Source: "CHANGELOG.txt"; DestDir: "{app}\Innexus.vst3"; Flags: ignoreversion
; Factory presets - install to system-wide presets location
; {commonappdata} = C:\ProgramData on modern Windows
Source: "presets\*"; DestDir: "{commonappdata}\Krate Audio\Innexus"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; No desktop icons needed for a VST plugin

[Registry]
; No registry entries needed - VST3 hosts scan the standard directory

[UninstallDelete]
; Clean up the entire plugin directory on uninstall
Type: filesandordirs; Name: "{app}\Innexus.vst3"

[Code]
// Display a message after installation
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Installation complete - no action needed
  end;
end;
