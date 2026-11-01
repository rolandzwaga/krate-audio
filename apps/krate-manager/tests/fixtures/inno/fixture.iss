; Krate Manager CI fixture installer (spec FR-060, FR-100, FR-101, FR-102)
; Inno Setup Script
;
; Compiled only by the manager-ci jobs helper-elevated and inno-locked-file with
; Minionguyjpro/Inno-Setup-Action and installed only on their ephemeral runners; never run elsewhere.
; It copies the plugin scripts' behaviour-deciding lines verbatim (PrivilegesRequired,
; UsePreviousAppDir, the two Architectures lines, every [Files] Flags value, the [UninstallDelete]
; entry; checked by scripts/inno-fixture.test.js). Only the AppId, the names and the roots differ:
; every path lies under {#Root}, so no run writes the runner's real Program Files or ProgramData.
;
; Defines, passed with /D:
;   Version  the fixture version (1.0.0, 1.0.1)
;   Root     the root standing in for the system folders, e.g. $RUNNER_TEMP\real\root
;   Payload  the staged inputs:
;              {#Payload}\Fixture.vst3\Contents\x86_64-win\Fixture.vst3  the locked-dll cdylib, renamed
;              {#Payload}\version.txt                                    the unlocked file, a text file holding the version
;              {#Payload}\presets\                                       at least one preset file

#define AppName "Fixture"
#define AppPublisher "Krate Audio"
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Root
  #error Root is required: /DRoot=<a directory under the runner temp dir>
#endif
#ifndef Payload
  #error Payload is required: /DPayload=<the staged input directory>
#endif

[Setup]
; Fixed fixture application ID (GUID); its uninstall key is {2258598C-E668-45BD-A814-B9C5D1D2C5CD}_is1
AppId={{2258598C-E668-45BD-A814-B9C5D1D2C5CD}
AppName={#AppName}
AppVersion={#Version}
AppPublisher={#AppPublisher}

; Installation directory - the plugin layout under {#Root}
DefaultDirName={#Root}\VST3\Krate Audio\Fixture
DisableProgramGroupPage=yes

; Output file naming
OutputDir=.
OutputBaseFilename=Fixture-{#Version}-Windows-x64

; Compression settings
Compression=lzma2
SolidCompression=yes

; Copied from the plugin scripts
PrivilegesRequired=admin

; Copied from the plugin scripts
UsePreviousAppDir=yes

; Installer appearance
WizardStyle=modern
SetupIconFile=compiler:SetupClassicIcon.ico

; Architecture - copied from the plugin scripts
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; The bundle, with the DLL at Fixture.vst3\Contents\x86_64-win\Fixture.vst3 (a real plug-in's layout)
Source: "{#Payload}\Fixture.vst3\*"; DestDir: "{app}\Fixture.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; The unlocked file, a text file holding the version
Source: "{#Payload}\version.txt"; DestDir: "{app}\Fixture.vst3"; Flags: ignoreversion
; Presets, under {#Root}\ProgramData in place of the system-wide presets location
Source: "{#Payload}\presets\*"; DestDir: "{#Root}\ProgramData\Krate Audio\Fixture"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; Clean up the entire plugin directory on uninstall
Type: filesandordirs; Name: "{app}\Fixture.vst3"
