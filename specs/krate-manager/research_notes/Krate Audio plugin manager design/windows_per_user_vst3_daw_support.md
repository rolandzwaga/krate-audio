# Windows per-user VST3 folder: which DAWs scan `%LOCALAPPDATA%\Programs\Common\VST3` by default

Research date: 2026-10-29. Question: if the Krate Audio plugin manager installs VST3 bundles per-user into
`C:\Users\<user>\AppData\Local\Programs\Common\VST3` (no admin rights), which Windows DAWs pick them up
without the user adding a custom path?

Steinberg's spec lists the folder as the first ("User") Windows location, `FOLDERID_UserProgramFilesCommon`,
"Mainly used for development use case" (VST 3 Developer Portal, Plugin Locations). It was introduced with
VST SDK 3.7.4 (2021-12-16).

## Summary table

| DAW | Version | Scans per-user folder by default | Custom VST3 paths allowed | Evidence quality | Primary URL |
|---|---|---|---|---|---|
| Ableton Live | 12 (article edited 2026-01-20) | **No** (only `C:\Program Files\Common Files\VST3` is the "VST3 Plug-In System Folder"; no per-user path documented) | Yes, exactly one "VST3 Plug-In Custom Folder" | Medium (vendor KB; negative by omission, no test) | https://help.ableton.com/hc/en-us/articles/209071729 |
| FL Studio | 2024 / 2025 (current online manual) | **No** (manual enumerates the exact VST3 folders: `Program Files\Common Files\VST3`, `Program Files\VST3` + x86 twins; "must be installed to the exact locations shown below") | **No** for VST3 ("You can only add custom search paths for VST 1 and VST 2") | High (official manual, explicit list) | https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm |
| Cubase | 13 / 14 (and 11, 12) | **Yes** per Steinberg staff: "Already supported by Cubase 11 and higher" (Yvan, 2021-12-17) | **No** (fixed spec locations only) | Medium-High (staff forum answer; Steinberg KB and manual still list only `Common Files\VST3`) | https://forums.steinberg.net/t/vst-3-7-4-sdk-released/754853 |
| Nuendo | 13 / 14 | **Yes (inferred)**: same host engine as Cubase; not named in the staff answer | No | Low-Medium (inference from Cubase) | same thread |
| PreSonus Studio One | 6 / 7 (any version >= 5.5.1) | **Yes**: "With version 5.5.1 and above, S1 will now also load VST3 plug-ins from ... `C:\Users\<username>\AppData\Local\Programs\Common\VST3`" | Yes (Options > Locations > VST Plug-ins > Add) | High (vendor KB, edited 2022-07-21, updated 2024-08-22) | https://support.presonus.com/hc/en-us/articles/360045544271 |
| Bitwig Studio | 5.x (any version >= 4.1.6) | **Yes**: 4.1.6 (2022-02-02) "Windows: Now looks for plug-ins inside `%LOCALAPPDATA%/Programs/Common/VST3/` (added in VST3 3.7.4)"; 4.3.10 (2022-10-07) added it to the default plug-in folder list | Yes (Settings > Locations; also `VST3_PATH` env var since 3.3.8) | High (official release notes) | https://downloads.bitwig.com/stable/4.1.6/Release-Notes-4.1.6.html , https://downloads.bitwig.com/stable/4.3.10/Release-Notes-4.3.10.html |
| REAPER | 7 (any version >= 6.44) | **Yes**: v6.44 (2022-01-07) "VST: scan `%LOCALAPPDATA%\Programs\Common\VST3` on Windows" | Yes (Preferences > Plug-ins > VST > path list) | High (official changelog) | https://www.reaper.fm/whatsnew.txt |
| Cakewalk by BandLab | 2022.02 and later | **Yes**: "The following new path has been added to the default VST scan locations: `%LOCALAPPDATA%/Programs/Common/VST3/`" | Yes (Preferences > File > VST Settings > VST Scan Paths) | High (official release notes) | https://docs.google.com/document/d/1aFOe_zJrd3x2EnaZ_Jc3iSbZPG2WANiCD4_RP83OjlA/edit |
| Cakewalk Sonar (2024+) | current | **Yes (inferred)**: same code base as CbB 2022.02+; no Sonar-specific statement found | Yes (VST Scan Paths) | Low-Medium (inference) | none |
| Tracktion Waveform | 12 / 13 | **Unknown**. JUCE-based host; JUCE's own default list has included the folder since JUCE 6.1.6, but Waveform's default scan list is not documented and Tracktion Engine does not set the paths | Yes (Settings > Plugins > scanning paths) | Low | none (see details) |
| Avid Pro Tools | 2024 / 2025 | **N/A**: Pro Tools hosts AAX only; no VST3 support announced in the 2025.6 / 2025.10 / 2025.12 releases | N/A | Medium (vendor KBs + release coverage; no first-party Avid statement captured) | https://bluecataudio.com/HelpDesk/knowledgebase.php?article=23 (secondary) |
| Renoise | 3.4 / 3.5 | **No**: manual says "the actual VST3 files must be located in the default directory or they will not be detected"; users confirm only `Common Files\VST3` is scanned | **No** for VST3 (extra folders are VST2-only) | High for "no custom paths"; Medium for the per-user question (manual says "default directory", singular, without enumerating) | https://tutorials.renoise.com/wiki/Preferences |
| Acoustica Mixcraft | 10 | **Unknown**: manual documents "Edit VST/VSTi Folders" (add/edit/delete) but never lists the default folders | Yes (Preferences > Plug-Ins > Edit VST/VSTi Folders; staff say to add `Common Files\VST3` there if missing) | Low | https://acoustica.com/mixcraft-10-manual/preferences |

Working conclusion for the manager: per-user install alone reaches Cubase/Nuendo (11+), Studio One (5.5.1+),
Bitwig (4.1.6+), REAPER (6.44+) and Cakewalk/Sonar (2022.02+). It does **not** reach Ableton Live, FL Studio
or Renoise without either admin rights (system folder) or, for Live only, the user pointing the single custom
folder at it; FL Studio and Renoise offer no custom VST3 path at all (the only workaround users report is a
directory junction inside `C:\Program Files\Common Files\VST3`, which again needs admin rights).

## Spec and host-SDK references

### Steinberg VST 3 Developer Portal, "Plugin Locations" (primary)

URL: https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Locations.html

Windows list, verbatim (priority order as printed):

1. User: `%LOCALAPPDATA%/Programs/Common/VST3/` — "FOLDERID_UserProgramFilesCommon native bitdepth: 32bit
   Plug-in on 32bit OS, 64bit on 64bit OS" — "Mainly used for development use case." — example
   `C:\Users\MyUserName\AppData\Local\Programs\Common\VST3`
2. Global: `/Program Files/Common Files/VST3/` — `FOLDERID_ProgramFilesCommon`
2. Global: `/Program Files (x86)/Common Files/VST3/` — "32bit Plug-ins on 64bit Windows"
3. Application: `$APPFOLDER/VST3/`

"The host recursively scans these folders at startup in this order (Global/Application)." (Note the sentence
names Global/Application only; the User entry is listed but not mentioned in the scan-order sentence.)

### VST SDK 3.7.4 release announcement (primary, Steinberg staff)

URL: https://forums.steinberg.net/t/vst-3-7-4-sdk-released/754853 — posted 2021-12-16 by Yvan (Steinberg);
changelog dated 2021/12/14.

- Changelog: "New VST3 plug-in location user local on Windows: `%LOCALAPPDATA%/Programs/Common/VST3/`";
  cmake: "Add support of new VST3 Location on Windows: new option `SMTG_PLUGIN_TARGET_USER_PROGRAM_FILES_COMMON`";
  "VST3PluginTestHost v3.2.20: Adapted to scan the new VST3 plug-in location on Windows".
- Yvan, 2021-12-17: Steinberg will ask the major hosts to implement it; the location is "Already supported by
  Cubase 11 and higher"; it is mainly for development without admin rights but may help multi-user machines.
- Arne_Scheffler (Steinberg), 2022-01-03: "If there's a conflict then the plug-in with the higher version is
  used"; "normally the new location should only be used for development" and manufacturers should install for
  all users.
- SDK 3.7.5 (2022-05-19) later flipped `SMTG_PLUGIN_TARGET_USER_PROGRAM_FILES_COMMON` to default ON on Windows
  (https://forums.steinberg.net/t/vst-3-7-5-sdk-released/786928).

### Steinberg SDK host code: `public.sdk/source/vst/hosting/module_win32.cpp`

Repository: https://github.com/steinbergmedia/vst3_public_sdk/blob/master/source/vst/hosting/module_win32.cpp
(master == commit a3911a4615dabbfdfd9d181ee26b05c70c289a95 "VST SDK 3.8.0", 2025-10-20, the latest commit touching
the file). Line numbers from the raw master file on 2026-10-29:

- L325 `Optional<std::string> getKnownFolder (REFKNOWNFOLDERID folderID)` — wraps `SHGetKnownFolderPath`.
- L557 `Module::PathList Module::getModulePaths ()`
- L561 `// find plug-ins located in common/VST3`
- L563 `if (auto knownFolder = getKnownFolder (FOLDERID_UserProgramFilesCommon))` ... L571 `path.append ("VST3");`
  L575 `findModules (path, list);`  (per-user folder, scanned FIRST)
- L578 `if (auto knownFolder = getKnownFolder (FOLDERID_ProgramFilesCommon))` ... L586 `path.append ("VST3");`
- L593 `// find plug-ins located in VST3 (application folder)` ... L604-605 `path = path.parent_path (); path = path.append ("VST3");`

History (GitHub commits API, path filter on the file):

- 3.7.3, commit 9589800ed94573354bc29de45eec5744523fbfcb (2021-08-10): only `FOLDERID_ProgramFilesCommon` (L431).
- 3.7.4, commit bc459feee68803346737901471441fd4829ec3f9 (2021-12-16): `FOLDERID_UserProgramFilesCommon` added
  (L431), `FOLDERID_ProgramFilesCommon` moved to L438.
  Permalink: https://github.com/steinbergmedia/vst3_public_sdk/blob/bc459feee68803346737901471441fd4829ec3f9/source/vst/hosting/module_win32.cpp#L431

The SDK host code is the reference for hosts built on it (VST3PluginTestHost, Audacity's `lib-vst3`, etc.), not
proof of what a given DAW does; Cubase has its own scanner (`vstscanner.exe`).

### JUCE `VST3PluginFormat::getDefaultLocationsToSearch()` (Windows)

- Feature request: https://forum.juce.com/t/getdefaultlocationstosearch-should-include-localappdata-for-vst3-plugin-scan-path/49735
  — nborthwick, 2022-01-18 ("Steinberg recently revised the folder paths for VST3 plugins ... The path is
  %LOCALAPPDATA%/Programs/Common/VST3/"); reuk (JUCE), 2022-01-28: "The default paths are now updated on develop."
- Commit: https://github.com/juce-framework/JUCE/commit/903873dad2ad2d875608f4cae55a28f3d5b98de0 — authored
  2022-01-19, committed 2022-01-28. Message, verbatim:
  "VST3:  Add local search path on Windows
  We haven't updated the default post-build copy paths yet, as the new local directory is not yet
  well-supported by DAWs."
  The DAW remark lives only in the commit message; the source carries no comment about DAW support.
  Diff: `return FileSearchPath (localAppData + "\\Programs\\Common\\VST3;" + programFiles + "\\Common Files\\VST3");`
  plus the new `File::windowsLocalAppData` special location (`CSIDL_LOCAL_APPDATA`).
- First tagged release: JUCE 6.1.6 (`modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp` L3785);
  6.1.5 still returned only `programFiles + "\\Common Files\\VST3"` (L3750). 7.0.0: L3934. 8.0.0: L4067.
  8.0.4: L4267-4278, https://github.com/juce-framework/JUCE/blob/8.0.4/modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp#L4267
- Current master (2026-10-29): the implementation moved to
  `modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatHeadless.cpp`,
  `VST3PluginFormatHeadless::getDefaultLocationsToSearch()` L148-171:
  https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatHeadless.cpp#L148
  Windows branch now reads
  `return { localAppData + "\\Programs\\Common\\VST3;" + programFiles + "\\VST3" };` with
  `programFiles = File::getSpecialLocation (File::windowsProgramFilesCommon)`, and the function prepends the
  `VST3_PATH` environment variable (colon or semicolon separated) to the defaults. Per-user folder is listed first.
- Consequence: any JUCE host that uses the defaults (Waveform, Element, Plugin Host demo, many small hosts)
  scans the per-user folder since JUCE 6.1.6 (Feb 2022). Hosts that persist their own path list (most of the
  big DAWs) are unaffected by this.

## Per-DAW details

### Ableton Live 12 (Windows)

- (a) Scans per-user folder by default: **No** (as far as the vendor documents).
- (b) Custom VST3 folders: **Yes, one.** "Use VST3 Plug-In Custom Folder"; the Live manual's VST section says
  additional locations are reached by placing shortcuts/aliases inside the custom folder.
- (c) Primary: Ableton KB "Using VST plug-ins on Windows", https://help.ableton.com/hc/en-us/articles/209071729
  (Zendesk API: created 2016-07-12, edited 2026-01-20, updated 2026-10-23). Verbatim: "VST3 devices are installed
  by default to the dedicated VST3 Plug-In System Folder. All your VST3 plug-ins should be installed here:
  C:\Program Files\Common Files\VST3"; "To use VST3 plug-ins, set Use VST3 Plug-in System Folder to On. In most
  cases, you can leave Use VST3 Plug-In Custom Folder turned off. In rare cases, the installer may define a path
  other than the default VST3 System Folder. If needed, you can then select and enable a VST3 Plug-In Custom
  Folder. In this case, choose a dedicated folder. Do not select the same location as either the VST3 System
  folder (C:\Program Files\Common Files\VST3), or your VST2 Plug-In Custom Folder."
  Live 12 manual, "Plug-In Sources" (24.3.1): https://www.ableton.com/en/live-manual/12/using-plug-ins/ — only
  the generic single-custom-folder flow; no AppData path anywhere.
- (d) Secondary: no user or developer report found of Live picking up `%LOCALAPPDATA%\Programs\Common\VST3`;
  all Ableton-forum / NI / Eventide threads about missing VST3s resolve to the system folder or the custom-folder
  toggle (e.g. https://forum.ableton.com/viewtopic.php?p=1822079 , https://community.native-instruments.com/discussion/20391).
  The JUCE commit message ("not yet well-supported by DAWs", Jan 2022) is consistent. Not tested on a machine.

### FL Studio 2024 / 2025 (Image-Line)

- (a) **No.**
- (b) **No** for VST3.
- (c) Primary: FL Studio online manual, "External plugins" page,
  https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm (undated;
  mentions FL Studio 2024.1 and CLAP, so current). Verbatim: "VST3 plugins must be installed to the exact
  locations shown below, otherwise they will not be found during a scan." Windows list: "The
  '..\Program Files\Common Files\VST3' & '..\Program Files\VST3' folders" (native bitness) and "The
  '..\Program Files (x86)\Common Files\VST3' & '..\Program Files (x86)\VST3' folders" (32-bit on 64-bit). No AppData
  entry (its only per-user path is the macOS `~/Library/Audio/Plug-Ins/VST3`, "rarely used").
  System settings page, https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/envsettings_files.htm :
  "You can only add custom search paths for VST 1 and VST 2 format plugins." "VST 3 and AU plugins must be
  installed to default system locations." "It's not possible to remove the default paths, only those you have
  added yourself."
- (d) Secondary: Image-Line forum "Changing vst3 folder default location",
  https://forum.image-line.com/viewtopic.php?p=1991059 (2021-11-24 to 2025-02-10): OP "the vst3s are not detected
  beyond the default location on the c: drive"; an Image-Line Technical Support reply (Hevy (IL), 2021-11-25)
  exists but its body is behind the login wall; the 2025 answer is "You can use a symlink." JUCE forum
  (https://forum.juce.com/t/running-vst3-in-fl-studio-beginner/31364): "apparently you can't add custom VST3 paths".

### Cubase 13 / 14 and Nuendo (Steinberg)

- (a) **Yes** for Cubase 11 and later, on Steinberg staff's word. Nuendo: inferred (same host code; Nuendo 11+
  is contemporaneous), not stated.
- (b) **No.** VST3 locations are fixed; the VST Plug-in Manager path editor applies to VST2 only.
- (c) Primary: VST SDK 3.7.4 announcement thread, https://forums.steinberg.net/t/vst-3-7-4-sdk-released/754853 —
  Yvan (Steinberg), 2021-12-17: the new location is "Already supported by Cubase 11 and higher". Steinberg KB
  "VST plug-in locations on Windows", https://helpcenter.steinberg.de/hc/en-us/articles/115000177084 (edited
  2025-07-10, updated 2025-12-22): "The VST3 format has a dedicated installation path all VST3 plug-ins must comply
  with. All your VST3 plug-ins should be installed here: C:\Program Files\Common Files\VST3 / 32-bit VST3 plug-ins
  on 64-bit Windows: C:\Program Files (x86)\Common Files\VST3" — the KB does not mention the per-user folder.
- (d) Secondary: Steinberg forum "Alternative VST3 folder location",
  https://forums.steinberg.net/t/alternative-vst3-folder-location/666730 (2016-2021): "No, that folder is part
  of the VST3 spec"; junction workaround. Cubase 14 thread https://forums.steinberg.net/t/vsts-missing-on-cubase-14/987945/9 :
  "vst3 plugins must reside in the common files\vst3 folder, it is a strict requirement" (user, not staff). No
  end-user report found that specifically confirms a plug-in loaded from `%LOCALAPPDATA%` in Cubase 13/14; the
  SDK's own `module_win32.cpp` scans the user folder first (see above). Not tested on a machine.

### PreSonus Studio One 6 / 7

- (a) **Yes** (5.5.1 and later).
- (b) **Yes.** Options > Locations > VST Plug-ins > Add. (PreSonus warns never to add all of Program Files or C:.)
- (c) Primary: PreSonus KB "How can I get my 3rd-party plug-ins to show up in Studio One?",
  https://support.presonus.com/hc/en-us/articles/360045544271 (Zendesk API: created 2020-06-27, edited 2022-07-21,
  updated 2024-08-22). Verbatim: "VST3 plugins will load from c:\Program Files\Common Files\VST3. With version
  5.5.1 and above, S1 will now also load VST3 plug-ins from the following location.
  C:\Users\<username>\AppData\Local\Programs\Common\VST3". Also: "From within Studio One, navigate to the Options
  > Locations, and select the VST Plugins tab. ... You can also use the Add button to add custom locations."
- (d) Secondary: HISE forum thread (https://forum.hise.audio/post/58348) has one developer whose plug-in only
  loaded once moved to `Common Files\VST3`; the plug-in was in assorted "custom VST folders", not necessarily the
  per-user VST3 folder, so it does not contradict the KB. No Studio One 7 release note re-states the behaviour;
  the KB is version-agnostic (">= 5.5.1").

### Bitwig Studio 5.x

- (a) **Yes** since 4.1.6.
- (b) **Yes.** Settings > Locations (plug-in folders list, per-format rescan); `VST_PATH` / `VST3_PATH` /
  `LXVST_PATH` env vars honoured since 3.3.8.
- (c) Primary: official release notes.
  - 4.1.6 (released 2 February 2022), https://downloads.bitwig.com/stable/4.1.6/Release-Notes-4.1.6.html :
    "Windows: Now looks for plug-ins inside %LOCALAPPDATA%/Programs/Common/VST3/ (added in VST3 3.7.4)".
  - 4.3.10 (released 07 October 2022), https://downloads.bitwig.com/stable/4.3.10/Release-Notes-4.3.10.html :
    "Add %LOCALAPPDATA%/Programs/Common/CLAP/ and %LOCALAPPDATA%/Programs/Common/VST3/ to default plug-in folders".
  - 3.3.8 / 3.3.11 notes: "VST plug-ins: Added support for VST_PATH, VST3_PATH, and LXVST_PATH environment variables".
  (Bitwig 5.3.x release-note URLs at the same pattern returned 404; no later note retracts the entry.)
- (d) Secondary: Output / Waves / Slate guides describe File > Settings > Locations with `C:\Program Files\Common
  Files\VST3` as the entry to check (https://support.output.com/en/articles/10297628 ,
  https://www.waves.com/support/how-to-find-your-plugins-in-bitwig-studio). Not tested on a machine.

### REAPER 7

- (a) **Yes** since v6.44.
- (b) **Yes.** Options > Preferences > Plug-ins > VST > "Edit path list".
- (c) Primary: official changelog https://www.reaper.fm/whatsnew.txt (file current to v7.84, 2026-10-28), entry
  under "v6.44 - January 7 2022": "+ VST: scan %LOCALAPPDATA%\Programs\Common\VST3 on Windows". No later entry
  reverts it. REAPER keeps a persisted path list per install (`reaper.ini`), so a user who edited the list before
  6.44 may not have the entry; this was not verified.
- (d) Secondary: NI "Plug-in Administration in Reaper"
  (https://support.native-instruments.com/hc/en-us/articles/360015830357) says the VST3 default location is
  "already added by default" (names only `Common Files\VST3`).

### Cakewalk by BandLab (2022.02+) and Cakewalk Sonar

- (a) **Yes** (CbB 2022.02, build 54166, released 2022-02-17). Sonar (BandLab's 2024+ paid successor): inferred,
  no Sonar-specific source found.
- (b) **Yes.** Edit > Preferences > File > VST Settings > VST Scan Paths.
- (c) Primary: Cakewalk by BandLab Release Notes (official Google Doc, text export fetched 2026-10-29),
  https://docs.google.com/document/d/1aFOe_zJrd3x2EnaZ_Jc3iSbZPG2WANiCD4_RP83OjlA/edit , section "Version 2022.02 /
  Misc. Enhancements": "The following new path has been added to the default VST scan locations:
  %LOCALAPPDATA%/Programs/Common/VST3/. This path is recommended as part of the VST3 specification in order to
  facilitate using plug-ins on systems without Admin rights as well as to support multiple users on the same PC."
  Archived copy: https://web.archive.org/web/20230402024819/https://docs.google.com/document/d/1aFOe_zJrd3x2EnaZ_Jc3iSbZPG2WANiCD4_RP83OjlA/edit
- (d) Secondary: KVR news repost of the same notes, 2022-02-17,
  https://kvraudio.com/news/cakewalk-by-bandlab-updated-to-version-2022-02-54166 . NI "Plug-in Administration in
  Cakewalk" (https://support.native-instruments.com/support/solutions/articles/69000879852) lists the default
  scan paths generically.

### Tracktion Waveform 12 / 13

- (a) **Unknown.** Waveform is JUCE-based, and JUCE's `getDefaultLocationsToSearch()` has included the per-user
  folder since 6.1.6 (Feb 2022); whether Waveform seeds its scan list from that function, and whether an existing
  user's persisted list is upgraded, is not documented. Tracktion Engine's open-source `PluginManager`
  (https://github.com/Tracktion/tracktion_engine/blob/develop/modules/tracktion_engine/plugins/tracktion_PluginManager.cpp)
  does not set search paths; the Waveform application does, and it is closed source.
- (b) **Yes.** Settings > Plugins > "Scanning for plugins..." lets the user pick folders (Toontrack guide,
  https://www.toontrack.com/?p=3229597 ; KVR Waveform forum).
- (c) Primary: none found. Waveform 12.x/13.x change lists seen (KVR, https://www.kvraudio.com/forum/viewtopic.php?p=8686706)
  contain no plug-in-path entry.
- (d) Secondary: none specific to the per-user folder.

### Avid Pro Tools 2024 / 2025

- (a)/(b) **Not applicable**: Pro Tools loads AAX only; it does not host VST3. The Pro Tools 2025.6
  (June 2025), 2025.10 (October 2025) and 2025.12 (December 2025) release coverage lists Splice/SoundFlow/360RA/
  Audio Vivid/ARA additions and no VST3 hosting (e.g. https://www.avid.com/press-room/2025/12/avid-marks-year-of-audio-advances-and-partner-expansion-with-new-pro-tools-release ,
  https://www.production-expert.com/production-expert-1/avid-pro-tools-202512-released).
- (c) Primary Avid statement: not captured (no avid.com KB page surfaced in search).
- (d) Secondary: Blue Cat Audio KB "Pro Tools cannot load VST, VST3 plug-ins or Audio Units, only AAX plug-ins"
  (https://bluecataudio.com/HelpDesk/knowledgebase.php?article=23); Image-Line KB "No VST support in Pro Tools"
  (https://support.image-line.com/knowledgebase/base.php?ans=509); Antares 2026 format overview
  (https://www.antarestech.com/blog/vst3-au-aax-clap-plugin-formats-2026). VST-in-Pro-Tools only via AAX wrapper
  plug-ins (Blue Cat PatchWork etc.), which have their own scan settings.

### Renoise 3.4 / 3.5

- (a) **No** (as documented): VST3 must be in "the default directory". Which directories count as default is not
  enumerated; user reports (3.3, 3.4.2) say only `C:\Program Files\Common Files\VST3` was found.
- (b) **No** for VST3; additional folders are VST2-only.
- (c) Primary: Renoise manual, Preferences > Plug/Misc, https://tutorials.renoise.com/wiki/Preferences (3.5-era
  screenshots): VST2: "choose the initial folder where Renoise will scan for plugin files" and "multiple folders
  (Win & macOS)"; VST3: "Enable the use of VST3 plugins in Renoise." ... "the actual VST3 files must be located in
  the default directory or they will not be detected."
- (d) Secondary: Renoise forum "VST3 plugins not recognized if not in Common Files\VST3",
  https://forum.renoise.com/t/vst3-plugins-not-recognized-if-not-in-common-files-vst3/62893 — taktik (Renoise
  developer), 2021-01-13: "The VST3 specs encourage to use the specified default paths only. ... So we definitely
  should use those paths by default. I agree though that it wouldn't hurt to allow specifying an additional custom
  folder, like Ableton does." Still unimplemented per a 2022-06-08 post (3.4.2); 3.5 (July 2025) release notes
  list only VST3 bug fixes. Junction workaround reported to fail for some plug-ins (Arturia).

### Acoustica Mixcraft 10

- (a) **Unknown.** The manual never lists the default scan folders.
- (b) **Yes.** Preferences > Plug-Ins > "EDIT VST/VSTI FOLDERS: Click this button to add, edit, or delete folders
  Mixcraft scans to locate VST Plug-Ins." plus "RE-SCAN ALL VST PLUG-INS".
- (c) Primary: Mixcraft 10 manual, Preferences, https://acoustica.com/mixcraft-10-manual/preferences ; Plug-in
  Manager page https://acoustica.com/mixcraft-10-manual/plug-in-manager (no folder list).
- (d) Secondary: Acoustica staff on forums.acoustica.com ("Finding vst3 plugins", viewtopic.php?p=135904;
  viewtopic.php?p=154093) tell users to open Edit VST/VSTi Folders and add `C:\Program Files\Common Files\VST3` if
  it is not listed, which suggests the folder list is user-visible and editable, including for VST3; the forum
  returned 403 to direct fetches, so this is from search snippets only.

## Gaps (not confirmed)

1. **Ableton Live 12**: no positive or negative test; the "No" rests on Ableton's KB listing only the system
   folder plus a single custom folder. A 5-minute test (drop a `.vst3` bundle into the per-user folder, rescan)
   would settle it.
2. **Cubase 13/14**: the only source is a Steinberg staff forum reply from December 2021 ("Cubase 11 and
   higher"); no Cubase manual, KB or release note mentions the per-user folder, and no end-user report of a
   plug-in loading from it was found. **Nuendo** is inferred from Cubase, not stated anywhere.
3. **Cakewalk Sonar (2024+)**: inferred from the CbB 2022.02 note; no Sonar-specific documentation found.
4. **Tracktion Waveform 12/13**: nothing found either way; only the JUCE-default inference.
5. **Mixcraft 10**: default folder list not documented; forum pages were 403 to direct fetch, staff quotes come
   from search snippets.
6. **Pro Tools**: no first-party Avid statement that VST3 is unsupported was captured (vendor KBs and release
   coverage only). Status in 2026 releases not checked.
7. **Renoise**: "default directory" is singular and unenumerated; whether Renoise's own default list (if it
   uses the SDK host code) includes the per-user folder is not confirmed; user reports predate 3.5.
8. **FL Studio forum staff reply** (Hevy (IL), 2021-11-25) is behind a login wall; the manual's explicit folder
   list is the authority used instead.
9. **REAPER / Bitwig / Studio One upgrade behaviour**: whether users who customised the path list before the
   respective versions get the new default appended was not verified (REAPER persists paths in `reaper.ini`;
   Bitwig 4.3.10 explicitly says "to default plug-in folders").
10. **Article dates** for the Ableton, PreSonus and Steinberg KBs come from the Zendesk Help Center JSON API
    (`created_at` / `edited_at` / `updated_at`), not from a visible byline.
11. Nothing here was verified on a Windows machine; every "Yes"/"No" is documentary.
