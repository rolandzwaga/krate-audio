# Feature Specification: Krate Manager Phase 10 — The Elevated Helper and the Windows / macOS Install Drivers

**Spec slug:** `krate-manager-phase10`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 10 (lines 709-810); Decisions STRATEGY (line 23: "the elevated helper
runs each plugin's Inno `.exe` with `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LOG` … The helper's
fixed op set … The exact Inno behaviour with a locked file under `/NOCLOSEAPPLICATIONS` is unmeasured and is measured
in Phase 10 on a hosted Windows runner"), Q2 (line 25), Q4 (line 27), Q7 (line 30), Q8 (line 31), ROSTER (line 33);
Core Philosophy "Privilege is a narrow, one-shot door" (lines 71-74); Architecture Overview (diagram lines 81-102,
"Processes" lines 104-110, per-OS table lines 114-118); Phase 4 line 477-479 (the launcher is injected "in Phase 10");
Phase 9 (the `InstallDriver` trait, lines 673-674, 707); Phase 12 (the user-performed launcher checks, lines 902-905);
Dependency Graph edges 4→10, 6→10, 7→10, 9→10, 10→12 (lines 959-960); Not in v1 (lines 982-984); Cross-Cutting
Constraints (lines 987-1028, in particular the named hosted-runner exception, lines 994-996).
**Depends on:** Phase 4 (`Roots`, `AppContext`, `test_support::fresh_temp_roots`, `manager-ci.yml`, the clippy
`disallowed-methods` guard), Phase 5 (`PluginEntry`, `InstallFacts.windows_uninstall_key` / `macos_pkg_id`,
`PlatformKey`), Phase 6 (`detect::detect_plugin`, `InstallerRecord::{Inno, Pkg}`, `HostFacts`, `HostIo`,
`OsHostIo`), Phase 7 (`download::obtain_and`, `VerifiedArtifact`, the release key), Phase 9 (`InstallDriver`,
`Action`, `ActionResult`, `InstallEnv`, `CommandEnv`, `DriverResolver`, `InstallIo`, `action_gate`, `allowed_actions`,
`for_command`, `remove_for_command`, the install and remove commands and their UI).
**Consumed by:** Phase 11 (the release pipeline bundles the helper sidecar into the universal macOS build: roadmap
Phase 11 does not name the helper today, so FR-123 (d) adds it there), Phase 12
("Update all" runs a multi-item batch through the same drivers and one elevation per batch; the user-performed UAC /
osascript checks; the first-launch documentation).
**Status:** DRAFT, revision 4 (plan SI-1 to SI-31 and tasks TA-2, TA-6, TA-12 encoded); revision 3 resolved the review issues (see Review notes) and encoded the clarifications of 2026-11-01 (see Clarifications): specification only, no implementation.
**Date:** 2026-11-01 · verified against `feat/krate-manager` at `2be37176` ("feat(krate-manager): Phase 9 Linux install,
update and uninstall", `git rev-parse --short HEAD`, clean tree); `specs/krate-manager-phase10/` did not exist before
this file.

---

## Overview

Phase 9 gave the manager an `InstallDriver` trait and a Linux driver that places files itself
(`apps/krate-manager/crates/core/src/install.rs:334-338`, `:355-406`); Windows and macOS have no driver yet
(`install.rs:410-413`: `matches!(os, Os::Linux).then_some(&LINUX)`), so their cards offer no action. Phase 10 adds
both, following the STRATEGY ruling: the manager never places files on Windows or macOS; it downloads and verifies each
plugin's **existing** installer (the Inno `.exe`, the unsigned `.pkg`) and has a small elevated helper run it.

The helper, `krate-manager-helper` (today the Phase 4 stub, `crates/helper/src/main.rs:1-4`), becomes a separate binary
shipped as a Tauri sidecar. It reads one JSON job file whose SHA-256 it receives on its command line, performs a
**fixed operation set** sequentially (`run_inno_installer`, `run_inno_uninstaller`, `run_pkg_installer`,
`uninstall_pkg`), and writes a result file with one entry per operation. It copies every installer into a directory
only administrators can write, hashing it as it copies, and runs the copy; it builds every allowed root from
compiled-in constants and takes no root, path argument or command string from the job. The validation and operation
logic is a library tested **unelevated** against temp roots through fake process, registry and filesystem seams; the
real binary runs elevated only in two named CI jobs on ephemeral GitHub-hosted runners.

The core gains the Windows and macOS `InstallDriver` implementations: they turn a batch of verified downloads and
uninstall requests into one job, raise **one** elevation prompt for it (Windows `ShellExecuteExW` verb `runas`, macOS
`osascript … with administrator privileges`, behind one launcher trait whose only real implementation lives in
`src-tauri`), and map each result to a named outcome: installed, locked ("close your DAW and retry"), failed, or refused
by the user. After a macOS install the core runs `killall -9 AudioComponentRegistrar` and the notice adds Apple's AU
rescan steps. What the Inno installer does when a plug-in file is mapped by a running DAW is not known today; the
`inno-locked-file` CI job measures it, and the driver's "locked" mapping follows the recorded measurement (M-1).

---

## Scope

**In scope (roadmap Phase 10 components, lines 713-766):**

1. `krate-manager-helper`: the job file, the fixed operation set, the result file (lines 714-738; D1 to D6).
2. TOCTOU-safe installer handling: copy into an admin-only directory, hash while copying, run the copy (lines 739-741;
   D7).
3. Allowed roots as a library parameter and compiled-in binary constants; `StagingRoot` from the job's canonical
   location (lines 742-751; D3).
4. The elevation launchers behind one trait, one prompt per batch (lines 752-754; D8).
5. The Windows and macOS `InstallDriver` implementations, the macOS registrar kill and notices (lines 755-761; D9 to
   D14).
6. The locked-file measurement on a hosted Windows runner and the mapping that follows it (lines 762-766; D10, M-1).
7. The two CI jobs `helper-elevated (macos-latest | windows-latest)` and `inno-locked-file (windows-latest)` (lines
   791-807), the bundling of the helper as a sidecar (roadmap line 109), and the release-build check that the test-only
   feature is absent (line 795).

**Non-goals (owned elsewhere):**

| Not in Phase 10 | Owner |
|---|---|
| "Update all", the download queue, a multi-item confirmation, parallel downloads | Phase 12 (roadmap lines 884-888). Phase 10's commands stay one-item batches with Phase 9's one-item confirmation (Phase 9 D15); the drivers already take a batch (FR-040) |
| The user-performed checks of the real UAC and osascript prompts, the real `.exe` / `.pkg` through the manager, the DAW-loaded locked case on a real machine | Phase 12 (lines 902-905) |
| The universal (`lipo`) macOS helper, the manager's release pipeline and first-install packaging | Phase 11 (lines 827-845; the helper half is added to Phase 11's text by FR-123 (d), since lines 812-877 never name the helper, its sidecar or `externalBin`). Phase 10's `bundle.js` builds the host-triple helper only (FR-092) |
| Self-update | Phase 11 |
| Any Linux elevation; a `pkexec` launcher | Never (STRATEGY, line 23; roadmap line 738 "Linux has no elevated operation") |
| A generic copy / rename / remove helper op; removing shadowing copies elsewhere on disk; writing the registry from the manager; a persistent service | Never (lines 738, 809-810, 982-984) |
| Closing or detecting a DAW | Never (Q7; line 1003) |
| Reinstall previous version, a beta channel | Not in v1 (lines 974, 981) |
| Any change to `plugins/`, `dsp/`, `cmake/`, `extern/`, `release.yml`, `feed.yml`, `ci.yml`, the plugin `setup.iss` scripts, the plugin pkgs, or `tools/feed-contract/` contracts and schemas | Never in this phase (FR-122) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 10 does with it |
|---|---|---|
| `specs/Krate-manager-roadmap.md:709-810` | Phase 10 components, outcomes, "Does NOT" | Source of every FR / SC (Traceability) |
| `specs/Krate-manager-roadmap.md:23` (STRATEGY), `:71-74`, `:116-117` | Inno flags, one UAC / osascript prompt per batch, the four helper ops, uninstall polls "until the bundle folder is gone", `pkgutil --files` removal then `--forget` | ✅ FR-001 to FR-034 |
| `specs/Krate-manager-roadmap.md:994-996` | The only exceptions to "never touch the real system" are named CI jobs on ephemeral hosted runners: "Phase 10 `helper-elevated` and `inno-locked-file`" | ✅ FR-110, FR-111 |
| `specs/Krate-manager-roadmap.md:477-479` | "the elevation launcher in Phase 10, each with its production value built in the same setup (Phase 4 spec D17)" | 🔶 D8: the launcher is injected through `CommandEnv` beside `drivers`, not as an `AppContext` field; the roadmap line is amended (FR-123) |
| `apps/krate-manager/crates/helper/Cargo.toml:1-15`; `crates/helper/src/lib.rs:1`; `crates/helper/src/main.rs:1-4`; `crates/helper/tests/stub.rs:1-23` | The Phase 4 D20 stub: no dependency, prints `krate-manager-helper: no operations in this build`, exits 2; already a dev-dependency on `krate-manager-core` with `test-support` | 🔶 Replaced by the helper (FR-001 to FR-060); the stub test is replaced by `helper_usage_exits_2` (FR-012) |
| `specs/krate-manager-phase4/spec.md:334-336` (D20) | "Phase 10 gives it its operation set" | ✅ |
| `apps/krate-manager/crates/core/src/install.rs:100-162`, `:164-196` | `InstallError` and `code()` | 🔶 New codes (FR-079) |
| `install.rs:57-73` | `Step` (FR-029 step names) | 🔶 `JobWritten`, `JobRemoved` (FR-079) |
| `install.rs:254-266`, `:288-303` | `Action::{Install, Update, Uninstall}`, `Outcome`, `ActionResult` | ✅ The Windows / macOS drivers consume and produce exactly these |
| `install.rs:305-320` | `Notice::RescanDaw`, `ActionSummary` | 🔶 `Notice::RescanDawMacos` (FR-076) |
| `install.rs:322-332` | `InstallProgress` | 🔶 `Elevating` (FR-075) |
| `install.rs:334-352`, `:408-413` | `InstallDriver`, `DriverResolver`, `NativeDrivers`; `driver_for` offers only Linux | 🔶 `driver_for` returns the Windows and macOS drivers too (FR-070) |
| `install.rs:415-437`, `:440-457` | `InstallEnv`, `CommandEnv` (`Copy`), `native_env` | 🔶 Both gain `elevation` and `host_io` (FR-071) |
| `install.rs:472-494` | `InstallIo` (read and mutating methods) | ✅ The job and result files go through it (FR-073) |
| `install.rs:2694-2738` | `ActionGate::{Blocked, Open { manifest }}`; `gate` returns `Ok(None)` off Linux after the journal check (`:2729-2731`) | 🔶 `Open` gains `remove: RemoveBasis`; `action_gate` takes the plugin's `Detection` (FR-080) |
| `install.rs:2742-2766` | `allowed_actions`: `Remove` iff a manifest | 🔶 `Remove` iff `RemoveBasis` is removable (FR-081) |
| `install.rs:2820-2845` | `BUSY` per `(state_dir, stem)` | ✅ Unchanged; a second, global guard serialises elevated batches (FR-078) |
| `install.rs:2850-2965` | `for_command` / `install_command`: gate at `:2932`, detection at `:2938`, `allowed_actions` at `:2942` | 🔶 Detection before the gate (FR-082) |
| `install.rs:3040-3128` | `remove_for_command(env, name, now_unix, progress)`: no detection, no `host` | 🔶 Gains `host: &dyn HostFacts` and a detection (FR-082) |
| `crates/core/src/detect.rs:131-166` | `InnoValues { display_version, install_location }`, `PkgOutputs`, `InstallerRecord::{Inno, Pkg, Manifest}` | ✅ The record the gate reads |
| `detect.rs:180-204` | `HostFacts` (`os_version`, `inno_values`, `pkg_outputs`); `PlatformFacts` | ✅ Read-only, unelevated; unchanged |
| `detect.rs:369-399` | Windows candidates: the copy at `InstallLocation\<vst3_bundle>` is managed, else `Vst3Global\Krate Audio\<stem>\<vst3_bundle>` | ✅ FR-080's Windows rule uses the same `InstallLocation` |
| `detect.rs:479-506` | `same_path` (ASCII case folding), `usable_install_location` | ✅ FR-080 compares the install location with `same_path` |
| `detect.rs:826-881` | `inno_record`, `parse_pkg_info`, `count_pkg_files`, `pkg_record` | ✅ Unchanged |
| `detect.rs:981-1072` | `detect_plugin(os, ctx, entry, host, listing) -> Detection` | ✅ Its `record` feeds FR-080 |
| `crates/core/src/platform/mod.rs:12-15`, `:63-84` | `APP_IDENTIFIER = "com.krateaudio.krate-manager"`, `VENDOR_DIR = "Krate Audio"`; `RunOutcome`; `HostIo { run, registry_values, rtl_version }` | ✅ `HostIo::run` runs `killall` (FR-077); `APP_IDENTIFIER` is the state-dir name the helper requires (FR-021) |
| `platform/windows.rs:9-14`, `:36-60`, `:67-73` | `UNINSTALL_KEY`, `DISPLAY_VERSION`, `INSTALL_LOCATION`; Windows roots: `Vst3Global` = `%CommonProgramW6432%\VST3`; the HKLM subkey `…\Uninstall\<key>` | ✅ The core reads; the helper re-reads elevated (FR-030) |
| `platform/macos.rs:93-101`, `:157-163` | `PKGUTIL = "/usr/sbin/pkgutil"`, `PKG_INFO_FLAG`, `PKG_FILES_FLAG`, `PKGUTIL_WAIT` 5 s; argv `[pkgutil, --pkg-info, id]` | ✅ The helper uses the same absolute path (FR-033) |
| `apps/krate-manager/src-tauri/src/os_facts.rs:46-107` | Windows registry read: `RegOpenKeyExW(HKEY_LOCAL_MACHINE, …, KEY_READ \| KEY_WOW64_64KEY)`, `RegGetValueW(… RRF_RT_REG_SZ …)` | ✅ The pattern the helper's registry reader follows (FR-030) |
| `crates/core/src/feed.rs:65-87`, `:121-128` | `PlatformAsset.format: AssetFormat::{InnoExe, Pkg, TarGz}`; `InstallFacts { …, windows_uninstall_key, macos_pkg_id }` | ✅ Format check (FR-072) and the uninstall facts (FR-031, FR-033) |
| `crates/core/src/contract.rs:1049-1061` | `windows_uninstall_key` `^\{[0-9A-F]{8}-…-[0-9A-F]{12}\}_is1$`; `macos_pkg_id` `^com\.krateaudio\.[a-z0-9]+$` | ✅ `app_id` is the key up to and including its first `}` (D4) (plan SI-7) |
| `contract.rs:137` | `PLUGIN_NAME_PATTERN = "^[A-Z][A-Za-z0-9]{0,63}$"` | ✅ The `<Name> AUv3.app` rule (FR-034) |
| `tools/feed-contract/CONTRACT.md:74-75` | `windows_uninstall_key` "equals the `setup.iss` AppId plus `_is1`", read by "manager (Phase 10, Windows uninstall)"; `macos_pkg_id` read by "manager (Phase 10, macOS receipt removal)" | ✅ |
| `crates/core/src/verify.rs:113-157` | `VerifiedArtifact` (no public constructor): `path()`, `sha256()`, `format()`, `platform()` | ✅ The only source of an installer op (FR-072) |
| `crates/core/src/download.rs:29`, `:1370-1388` | Verified artifacts sit under `<staging>/downloads/<sha256 hex>/` | ✅ The helper's source root is `<state>/staging` (FR-021) |
| `crates/core/src/context.rs:50-61`, `:84-129` | `AppContext { roots, settings, feed_url, http, signing_key }` (`Clone, Debug, PartialEq`); `assemble_with_os` runs `install::recover` | 🔶 `recover` also removes stale job files (FR-074); `AppContext` unchanged (D8) |
| `crates/core/src/status.rs:202-257`, `:364-439` | `build_with_os`: detection per entry, then a gate only when a driver exists (`:213`, `:231-233`); `plugin_view` fills `actions` from the gate | 🔶 Gate over the detection (FR-080); `remove_hint` (FR-083) |
| `apps/krate-manager/src-tauri/src/commands.rs:104-199` | `install_plugin`, `remove_plugin` over `install::native_env` | 🔶 Build the native launcher and pass `PlatformFacts` to Remove (FR-090) |
| `src-tauri/src/lib.rs:27-99` | Startup; eight commands in `generate_handler!` | ✅ No new command (FR-090) |
| `src-tauri/tauri.conf.json:6-11`, `:19-24`; `tauri.windows.conf.json`, `tauri.macos.conf.json`, `tauri.linux.conf.json` (one line each) | `beforeBuildCommand: npm run build`; `bundle.macOS.signingIdentity "-"`; per-OS `targets` | 🔶 A `tauri.helper.conf.json` merged only by the bundle step (D12, FR-091) |
| `src-tauri/capabilities/main.json:1-12` | Eight `allow-*` permissions, nothing else | ✅ Unchanged: no `shell:*` permission (FR-092) |
| `apps/krate-manager/Cargo.toml:13-51` | Workspace dependencies; `windows-sys` 0.61.2 with five features (`:49`); `sha2` 0.11.1 (`:36`); `serde` / `serde_json` (`:33-34`) | 🔶 `windows-sys` features (FR-093) |
| `apps/krate-manager/clippy.toml` | `disallowed-methods`: `Roots::from_os`, `Roots::new`, `std::env::{var, var_os, vars, vars_os, home_dir, temp_dir}` | ✅ The helper reads no environment (FR-022) |
| `crates/core/tests/injection.rs:562-591` (`READER_PINS`) | `windows-sys` pinned to five features, home `src-tauri/Cargo.toml` | 🔶 Updated pins (FR-093) |
| `crates/core/tests/config_guard.rs:100-102` | `MEMBERS`, `LIBRARY_MEMBERS` include `crates/helper` | ✅ |
| `.github/workflows/manager-ci.yml:28-34`, `:35-37`, `:86`, `:117-120`, `:153-178`, `:180-183` | Job `check` on `windows-latest`, `macos-latest`, `ubuntu-24.04`, `defaults.run.shell: bash`; `cargo clippy --workspace --all-targets --locked -- -D warnings` (no non-default feature); `cargo test --workspace --locked`; `required tests ran` (reads `req.cargo` and `req.vitest` only); `tauri build`. No `node --test` step | 🔶 Two new jobs, a bundle step, a `ci-temp-roots absent` step, a feature-lint step, a `node --test` step (FR-100 to FR-109) |
| `.github/workflows/feed-tools.yml:83`, `:86-122` | `node --test "tools/feed-contract/tests/*.test.js"` with a fixed `PLAN` and `NAMES` list | ✅ Unchanged: it never runs `apps/krate-manager/scripts/*.test.js` (hence FR-109) |
| `apps/krate-manager/src-tauri/tauri.conf.json:23` | `"windows": { "nsis": { "installMode": "currentUser" } }` (roadmap Q5, lines 28, 495, 839) | ✅ The manager, and so the bundled helper, lands in a per-user, user-writable folder on Windows (D19) |
| `crates/core/src/platform/windows.rs:37-41`; `platform/macos.rs:35` | The state dir is `%LOCALAPPDATA%\com.krateaudio.krate-manager` (Windows) and `~/Library/Application Support/com.krateaudio.krate-manager` (macOS) | ✅ The shape FR-025 binds the job's state dir to |
| `crates/core/src/detect.rs:1047-1050`; `crates/core/src/roots.rs:173-184` | `detect_plugin` calls `ctx.roots.check_sandboxed(loc)` on every record location; on a sandboxed (test) `Roots` it panics unless the path is a strict descendant of the sandbox base | ✅ Every outside-root fixture names `<sandbox>/elsewhere/…` (SC-012, SC-013) |
| `crates/core/src/install.rs:578-580` | `OsInstallIo::canonicalize` is plain `std::fs::canonicalize` | ✅ On Windows it yields a verbatim `\\?\` path (External facts), the form FR-027 compares in |
| `tools/feed-contract/tests/manager-ci-shape.test.js:337`, `:808` | `EXPECTED_REQUIRED`; `jobs` must equal `['check']` | 🔶 Three jobs, their shape (FR-107) |
| `apps/krate-manager/tests/required-tests.json:37`, `:269`, `:279`, `:293`, `:339`, `:415` | `helper_stub_exits_2_with_one_line`; Phase 9's `no_driver_off_linux`, `actions_per_state`, `native_wiring_offers_nothing_windows` / `_macos`, vitest `renders no install buttons on windows or macos` | 🔶 Replaced by their Phase 10 successors (FR-108) |
| `plugins/iterum/installers/windows/setup.iss:27`, `:37`, `:50`, `:53`, `:63-64`, `:72-77`, `:102-104` | `AppId={{8F7C…}`; `DefaultDirName={commonpf}\Common Files\VST3\Krate Audio\Iterum`; `PrivilegesRequired=admin`; `UsePreviousAppDir=yes`; `ArchitecturesAllowed=x64compatible`, `ArchitecturesInstallIn64BitMode=x64compatible` (without the latter the `_is1` key lands in the 32-bit view); bundle and presets in `[Files]` with `Flags: ignoreversion recursesubdirs createallsubdirs`, presets to `{commonappdata}\Krate Audio\Iterum` (`:77`); `[UninstallDelete]` `Type: filesandordirs; Name: "{app}\Iterum.vst3"`. No `CloseApplications` directive (default `yes`). All nine scripts carry an `AppId` (`setup.iss:18` in the other eight) | ✅ What the helper runs, unchanged |
| `.github/workflows/release.yml:244-247` | `Minionguyjpro/Inno-Setup-Action@v1.2.7` compiles `setup.iss` with `/DVersion=` | ✅ The CI fixture setups compile with the same action (FR-101) |
| `release.yml:294-312`, `:323-341` | pkg staging: `Library/Audio/Plug-Ins/{VST3,Components}`, optional `Applications/<Name> AUv3.app`, presets under `Library/Application Support/Krate Audio/<Name>[/<subdir>]`; `pkgbuild --root staging … --identifier "$IDENTIFIER" --version … --install-location /`; identifier `com.krateaudio.<name lower-cased>` unless `version.json` overrides (`:325`) | ✅ The receipt layout `uninstall_pkg` removes (FR-033, FR-034) |
| `plugins/iterum/installers/macos/preinstall` | Runs as root inside the pkg | ✅ Unchanged |
| `specs/krate-manager/probes/results.md:9`, `:14`, `:18-19` | No Mark-of-the-Web on manager downloads; `sudo installer -pkg` installs an unquarantined payload; Open Anyway and the self-replace untested | ✅ Why the helper never touches xattrs (FR-024) |

---

## External facts used (verified this session)

The roadmap names no new crate, npm package or Tauri plugin for Phase 10. Phase 10 adds no crate: the helper uses the
workspace's `serde` 1.0.229, `serde_json` 1.0.154, `sha2` 0.11.1 and `windows-sys` 0.61.2 (`Cargo.toml:33-36`, `:49`).

| Fact | Source |
|---|---|
| Inno Setup command line: `/VERYSILENT` "the installation progress window is not displayed"; `/SUPPRESSMSGBOXES` "Only has an effect when combined with /SILENT or /VERYSILENT", default answers "Abort/Retry prompts: Abort", "Retry/Cancel: Cancel", "Restart prompts: Yes"; `/NORESTART` "Prevents Setup from restarting the system following a successful installation, or after a Preparing to Install failure that requests a restart"; `/NOCLOSEAPPLICATIONS` "Prevents Setup from closing applications using files that need to be updated by Setup"; `/LOG="filename"` "If a file with the specified name already exists it will be overwritten. If the file cannot be created, Setup will abort"; `/RESTARTEXITCODE` "(By default, 0 is returned in this case.)" | https://jrsoftware.org/ishelp/topic_setupcmdline.htm (fetched 2026-11-01; the page shows no version) |
| `CloseApplications`: "Default value: yes"; "If set to yes or force and Setup is running silently, Setup will always close and restart such applications unless told not to via the command line"; "Setup uses Windows Restart Manager" | https://jrsoftware.org/ishelp/topic_setup_closeapplications.htm. Consequence: the nine scripts set no `CloseApplications`, so without `/NOCLOSEAPPLICATIONS` a silent run would close a running DAW |
| Setup exit codes 0 to 8 (0 success; 1 init failure; 2 cancel before install; 3 fatal preparing; 4 fatal during install; 5 cancel during install "or chose Abort at an Abort-Retry-Ignore box"; 6 debugger; 7 Preparing to Install cannot proceed; 8 the same and a restart is needed); "applications should handle unexpected codes" | https://jrsoftware.org/ishelp/topic_setupexitcodes.htm |
| Uninstaller: "any non-zero exit code indicates that the uninstaller was not run to completion"; it copies itself to TEMP, the original exits first, so the exit code can arrive before the removal is done; `/VERYSILENT`, `/SUPPRESSMSGBOXES`, `/NORESTART` "not to reboot even if it's necessary"; the uninstaller is `unins???.exe` | https://jrsoftware.org/ishelp/topic_uninstexitcodes.htm, https://jrsoftware.org/ishelp/topic_uninstcmdline.htm; per_os_install_mechanics.md lines 68, 71 |
| The uninstall key is `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{AppId}_is1` in the 64-bit view for Krate's admin 64-bit installers; `UninstallString`, `InstallLocation` are among its values; Inno's `QuietUninstallString` uses `/SILENT`, not `/VERYSILENT` (third-party report) | per_os_install_mechanics.md lines 74, 78-79, 101, 107; https://learn.microsoft.com/en-us/windows/win32/msi/uninstall-registry-key; the WAPT forum thread found 2026-11-01 (https://forum.tranquil.it/en/viewtopic.php?p=4724). Hence the helper reads `UninstallString` and supplies its own fixed flags (FR-031) |
| `ShellExecuteExW(*mut SHELLEXECUTEINFOW) -> BOOL`; `ERROR_CANCELLED` "The function prompted the user for additional information, but the user canceled the request"; "COM should be initialized before ShellExecuteEx is called" with `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED \| COINIT_DISABLE_OLE1DDE)`; `lpVerb` `runas` "Launches an application as Administrator. User Account Control (UAC) will prompt"; `SEE_MASK_NOCLOSEPROCESS` "the hProcess member receives the process handle"; `SEE_MASK_NOASYNC` "must be specified if the thread … does not have a message loop"; `SEE_MASK_FLAG_NO_UI` "Security prompts are exempted" | https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw (updated 2025-07-01); https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shellexecuteinfow (updated 2026-02-03) |
| windows-sys 0.61.2: `ShellExecuteExW` and `SHELLEXECUTEINFOW` are behind `Win32_UI_Shell` **and** `Win32_System_Registry` (`src/Windows/Win32/UI/Shell/mod.rs:755-756`, `:5672-5674`); `SHGetKnownFolderPath` (`:573`), `FOLDERID_ProgramData` (`:3072`), `FOLDERID_ProgramFilesCommon` (`:3074`) under `Win32_UI_Shell`; `CoInitializeEx`, `CoTaskMemFree` under `Win32_System_Com`; `WaitForSingleObject`, `GetExitCodeProcess` under `Win32_System_Threading`; `CreateDirectoryW` under `Win32_Storage_FileSystem` + `Win32_Security`; `ConvertStringSecurityDescriptorToSecurityDescriptorW`, `SDDL_REVISION_1` under `Win32_Security_Authorization`; `ERROR_CANCELLED = 1223`; `SEE_MASK_NOCLOSEPROCESS = 64` | The crate source in the local registry, `windows-sys-0.61.2/src/…` and `Cargo.toml:137,141,163,184,234,250,272` (read 2026-11-01); https://docs.rs/windows-sys/0.61.2/windows_sys/Win32/UI/Shell/fn.ShellExecuteExW.html |
| `FOLDERID_ProgramFilesCommon` {F7F1ED05-9F6D-47A2-AAAE-29D317C6F066}, default `%ProgramFiles%\Common Files`; `FOLDERID_ProgramData` {62AB5D82-FDC1-4DC3-A9DD-070D1D495D97}, default `%ProgramData%` | https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid (updated 2023-08-30) |
| Command-line quoting: whitespace delimits; a quoted string is one argument; "Backslashes are interpreted literally, unless they immediately precede a double quote mark"; 2n backslashes + `"` → n backslashes and a delimiter | https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments (updated 2021-12-10) |
| GitHub-hosted runners: "The Linux and macOS virtual machines both run using passwordless `sudo`"; "Windows virtual machines are configured to run as administrators with User Account Control (UAC) disabled" | https://docs.github.com/en/actions/reference/runners/github-hosted-runners (fetched 2026-11-01). This is the roadmap's "elevation state the spec confirms against the hosted image's documentation" (line 793-794) |
| `osascript`: "Any arguments following the script will be passed as a list of strings to the direct parameter of the "run" handler"; `-e` "Enter one line of a script" | https://keith.github.io/xcode-man-pages/osascript.1.html |
| `do shell script "command" with administrator privileges` "runs the command as the root user"; "you should quote any variables using `quoted form of`"; a non-zero exit "throws an AppleScript error with the status as the error number" | Apple TN2065, https://developer.apple.com/library/archive/technotes/tn2065/_index.html |
| AppleScript error -128 "User canceled" | https://developer.apple.com/library/content/documentation/AppleScript/Conceptual/AppleScriptLangGuide/reference/ASLR_error_codes.html |
| The Tauri updater's macOS elevation idiom is `do shell script … with administrator privileges` | tauri_feasibility.md line 45 (source: plugins-workspace `updater.rs`) |
| `installer`: "The installer command requires root privileges to run"; path `/usr/sbin/installer`; `-pkg <pathToPackage> -target device` | https://keith.github.io/xcode-man-pages/installer.8.html |
| `pkgutil`: `--files` "List all of the files installed under the package-id"; `--only-files` "List only files (not directories) in `--files` listing"; `--only-dirs` "List only directories (not files)"; `--forget` "Discard all receipt data about package-id, but do not touch the installed files"; `--volume` defaults to `/` | https://keith.github.io/xcode-man-pages/pkgutil.1.html. The existence of `--only-dirs` shows `--files` lists directories; whether those include the structural ancestors (`Library`, `Library/Audio`, …) of a `--install-location /` pkg is not documented and is asserted on the `helper-elevated` macOS leg (SC-018) |
| Tauri sidecars: `bundle.externalBin` paths "relative to the `tauri.conf.json` file"; "a binary with the same name and a `-$TARGET_TRIPLE` suffix must exist"; host triple from `rustc --print host-tuple` | https://v2.tauri.app/develop/sidecar/ (Tauri 2) |
| `tauri-build` 2.7.4 `try_build` merges the `TAURI_CONFIG` env JSON into the config (`src/lib.rs:681-684`) and, when `bundle.externalBin` is set, copies each sidecar beside the build output with the triple stripped (`:735-737`, `copy_binaries` `:139`), failing when one is missing | Crate source in the local registry (workspace `tauri-build = "2.7.4"`), read 2026-11-01. Consequence: `externalBin` in the base config would break every plain `cargo build` / `clippy` / `test` of `src-tauri` on a fresh checkout (D12) |
| `tauri build -c/--config`: "JSON strings or paths to JSON, JSON5 or TOML files to merge with the default configuration file" | https://v2.tauri.app/reference/cli/ (`@tauri-apps/cli` 2.12.4, `package.json`) |
| The shell plugin resolves a sidecar as `current_exe().parent().join(name)` (`.exe` appended on Windows) | `relative_command_path`, https://github.com/tauri-apps/plugins-workspace/blob/v2/plugins/shell/src/process/mod.rs. Phase 10 does not use the shell plugin; it resolves the same path itself (FR-090) |
| AU pickup after an install: `killall -9 AudioComponentRegistrar` (exit code ignored), then Apple's ladder: restart, Logic "Reset & Rescan Selection", "Full Audio Unit Reset", move `~/Library/Caches/AudioUnitCache` out (https://support.apple.com/en-us/122179) | report line 34; per_os_install_mechanics.md line 180 |
| "some AudioUnits hold open the osascript port and prevent elevating permissions" | per_os_install_mechanics.md line 126 (JUCE forum) |
| `std::fs::OpenOptions::create_new` (fails on an existing file or dangling symlink), `std::fs::canonicalize`, `std::process::Command` (argv, no shell) | std 1.99.1 docs, as cited by Phases 8 and 9 (`specs/krate-manager-phase9/spec.md:152-157`) |
| `std::fs::canonicalize` on Windows: "this converts the path to use extended length path syntax", i.e. a verbatim `\\?\C:\…` path | https://doc.rust-lang.org/std/fs/fn.canonicalize.html (std 1.99.1). Consequence: FR-027 |
| `std::os::windows::process::CommandExt::raw_arg` appends text to the command line "without any quoting or escaping", so the child's own runtime parses it as `ShellExecuteExW`'s `lpParameters` would be | https://doc.rust-lang.org/std/os/windows/process/trait.CommandExt.html#tymethod.raw_arg (std 1.99.1). Used by SC-015's `windows_parameters_parse_back_windows` |
| Cargo sets `CARGO_BIN_EXE_<name>` ("the absolute path to a binary target's executable") only when building an integration test or benchmark | https://doc.rust-lang.org/cargo/reference/environment-variables.html#environment-variables-cargo-sets-for-crates |
| A target with `required-features` is skipped by `cargo build` / `test` / `clippy` unless those features are enabled | https://doc.rust-lang.org/cargo/reference/cargo-targets.html#the-required-features-field. Consequence: FR-105's compile-only step |
| `Start-Process -Wait` "waits for the specified process and its descendants to complete"; `-PassThru` returns the process object (its `ExitCode`) | https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/start-process. Used for the GUI-subsystem helper in the Windows CI steps (FR-100) |
| windows-sys 0.61.2: `SW_HIDE: SHOW_WINDOW_CMD = 0i32` is defined only in `Win32/UI/WindowsAndMessaging/mod.rs:3354`; `SHELLEXECUTEINFOW.nShow` is a plain `i32` (`Win32/UI/Shell/mod.rs:1807`, `:1825`, `:1849`); `FOLDERID_UserProfiles` (`:3134`), `FOLDERID_LocalAppData` (`:3049`), `FOLDERID_Windows` (`:3141`) under `Win32_UI_Shell` | Crate source in the local registry, read 2026-11-01. Consequence: FR-090 passes the literal `0` and FR-093 adds no `Win32_UI_WindowsAndMessaging` |
| 7-Zip lists and extracts NSIS installers (`7z l`, `7z e`); `7z` is on the `windows-latest` image | https://www.7-zip.org/ (supported formats, "NSIS" among the unpack-only formats); the runner-images Windows readme (tools list). Not re-fetched this session: FR-104's step fails `::error` when `7z` is absent or lists no entry, so a wrong premise reads red, never vacuously green |

---

## Decisions taken in this spec

The roadmap defers two things to this spec by name: the hosted runner's elevation state (confirmed above) and the
recorded locked-file result (M-1, measured by this phase's CI job). D1 to D15, D17 and D18 settle what its Phase 10
text names but does not pin down; D16 and D19 depart from or go beyond roadmap text and are flagged in Open questions.

- **D1, the helper is its own crate; the core never depends on it.** `crates/helper` has a library
  (`krate_manager_helper`) holding the job and result schema, the validation, the operation runner and every OS seam as a
  trait, and a binary whose `main.rs` builds the production seams. Its normal dependencies are `serde`, `serde_json`,
  `sha2`, and `windows-sys` on Windows only: the elevated binary links no HTTP stack, no Tauri, no PAC engine. The core
  writes jobs with its own serializer (`install::job`, FR-073); agreement is proven by a helper integration test that
  parses jobs the core wrote and results the helper wrote back to the core (SC-011), through the dev-dependency the stub
  already has (`crates/helper/Cargo.toml:10-11`).
- **D2, the job travels by path and its hash by argv.** The core writes `<state>/jobs/job-<32 lower-hex>.json` and
  launches `krate-manager-helper --job <canonical path> --job-sha256 <64 lower-hex>`. The job file sits in a
  user-writable directory, so the helper reads it once into memory (at most 64 KiB), hashes those bytes and refuses a
  mismatch: a job swapped after the core wrote it, and after the user consented at the prompt, is never run. This
  extends the roadmap's TOCTOU rule (lines 739-741) from the installer to the job; the argv is fixed at elevation time.
- **D3, the roots are constants of the binary.** `AllowedRoots` and `StagingRoot` are parameters of every library
  function (roadmap line 742). `main` builds `AllowedRoots::production()` from constants: Windows `<FOLDERID_ProgramFilesCommon>\VST3\Krate Audio` and
  `<FOLDERID_ProgramData>\Krate Audio`; macOS `/Library/Audio/Plug-Ins/VST3`, `/Library/Audio/Plug-Ins/Components`,
  `/Library/Application Support/Krate Audio`, and `/Applications` restricted to one `<Name> AUv3.app` entry; plus the
  state-dir rule of D17. Only a build with the cargo feature `ci-temp-roots` accepts `--ci-roots <file>` naming temp
  roots and the one temp state dir; without the feature that argument is a usage error. The shape (plan SI-16): `AllowedRoots
  { os: OsRoots, state_dir: StateDirRule }` with `OsRoots::{Windows(WindowsRoots), Macos(MacosRoots), None}`;
  `WindowsRoots { vst3_vendor, preset_vendor, run_base }`; `MacosRoots { vst3, components, presets, applications, volume,
  run_base, run_ancestors: Vec<PathBuf> }`. `run_base` (D7's parent of the run directory: Windows `%SystemRoot%\Temp`,
  macOS the helper folder), `run_ancestors` (D7's root-owned chain above the helper folder; production `["/Library",
  "/Library/Application Support"]`) and `volume` (the base `pkgutil`'s `location` is relative to, production `/`) are
  roots too, so every op runs against temp dirs on every leg: D7's ancestor chain is `run_ancestors` + `run_base` + the
  new run directory, never a literal `/Library` path. `--ci-roots` replaces the plug-in, preset and application roots and
  the state dir only, never `run_base`, `run_ancestors` or `volume` (the CI jobs prove the real run directory, SC-017,
  SC-018). `production()` on Linux is `OsRoots::None`.
- **D4, `app_id` is the feed key up to and including its first `}`.** The feed's `windows_uninstall_key` is `{GUID}_is1`
  (`contract.rs:1049-1055`, fixed to `^\{GUID\}_is1$` at feed parse); the job's `run_inno_uninstaller.app_id` is the
  key cut after its first `}`, i.e. the `{GUID}` part, so no `_is1` literal is needed in `install.rs`, where
  `roster.rs:201-207` bans it (plan SI-7); the helper re-validates it (FR-031), matched against
  `^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$`, and the helper appends `_is1` itself
  (roadmap line 723).
- **D5, the helper reads every receipt and registry fact itself.** No listing, location, uninstaller path or install
  directory travels in the job. `uninstall_pkg` runs `pkgutil --pkg-info`, `--files --only-files` and `--files
  --only-dirs` as root; `run_inno_uninstaller` reads `UninstallString` and `InstallLocation` from HKLM in the 64-bit view.
- **D6, Windows uninstall completion.** The uninstaller returns before its work is done (External facts). The op
  succeeds when, within 120 s (polled every 500 ms), all three hold: the `unins*.exe` it started is gone, the install
  directory holds no entry whose name ends in `.vst3` (case-insensitive), and the `<app_id>_is1` key is gone. This is
  "polls until the bundle folder is gone" (roadmap line 726) without the job naming the bundle (D5).
- **D7, the admin-only run directory: one per job.** A job with at least one installer op gets exactly **one** fresh
  `run-<32 lower-hex>` directory (the name from `HelperIo::random_hex`: a `RandomState`-derived, collision-checked name;
  not a secret) (plan SI-9), created before its first installer op runs and removed after its last op
  (FR-043); every installer op of the job copies into it as `installer-<index>` and logs to `inno-<index>.log`, so the
  names never collide. A job with only uninstall ops creates none. Windows: under `<FOLDERID_Windows>\Temp`, created by
  `CreateDirectoryW` with the explicit, protected DACL `D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)` (SYSTEM and
  Administrators only), so the parent's inherited ACL never applies; macOS: under
  `/Library/Application Support/com.krateaudio.krate-manager-helper/`, mode `0700`, after checking (`lstat`, never
  following) that `/Library`, `/Library/Application Support`, that folder and the new directory are each a directory
  (not a symlink), owned by uid 0 and have `mode & 0o022 == 0`. That check is a pure function,
  `validate::run_dir_ancestors_ok(&[LstatFacts { uid, mode, kind }]) -> Result<(), HelperError>`, fed by `HelperIo`
  (SC-027). The macOS helper folder `/Library/Application Support/com.krateaudio.krate-manager-helper/` is created by the
  helper when absent (`HelperIo::create_helper_folder`, `DirBuilder` mode `0o755`), then checked with the run directory
  by `run_dir_ancestors_ok`; the run directory is `0o700` (plan SI-23). Creation fails on an existing name (no reuse). The run directory's path is built from the known folder (or
  the literal), never canonicalized, and contains no space, so the Inno `/LOG=` argument needs no quoting (FR-030).
- **D8, the launcher is injected through `CommandEnv`, beside `drivers`.** `AppContext` is `Clone + Debug + PartialEq`
  data shared with status (`context.rs:50-61`); a launcher is behaviour. Phase 9 already injects its driver resolver
  through `CommandEnv` (`install.rs:426-437`). So `CommandEnv` and `InstallEnv` gain `elevation: &dyn Elevation`, and
  the roadmap's line 478 ("the elevation launcher in Phase 10" joins `AppContext`) is amended (FR-123). Every decision
  (the argv, the quoting, the osascript script, the outcome mapping) is core code; `src-tauri/src/os_elevation.rs`
  only executes, as `os_facts.rs` does for registry reads (`os_facts.rs:1`).
- **D9, the gate reads the installer record.** Off Linux the gate gains a `RemoveBasis`: Windows `Inno { app_id }` when
  the detection holds an `InstallerRecord::Inno` whose `install_location` is a direct child of
  `<Vst3Global root>\Krate Audio` (compared with `same_path`), `OutsideRoot` when it is elsewhere (shown as managed, Remove
  through Windows Settings, roadmap lines 728-730), macOS `Pkg { pkg_id }` when the detection holds an
  `InstallerRecord::Pkg`. A plugin installed by the old `.exe` / `.pkg` has the same record and is removable like any
  other (Q4).
- **D10, the Inno mapping lives in the helper, and "locked" follows the measurement.** `classify_inno(exit_code,
  log) -> InnoVerdict { Installed, Locked, Failed }` is a pure function of the helper library. Until M-1 is recorded it
  maps exit 0 to `Installed` and every other code to `Failed { exit_code }`; it never returns `Locked`. The build stage
  replaces the rule with the measured one in the same change that commits the M-1 fixture (FR-062), and SC-016 then
  replays the recorded exit code and log through it, the runner and the core's driver. The op runner reaches the rule
  only through the `classify` field of `Seams` (FR-053), so the batch behaviour around a `Locked` verdict (SC-003) is
  testable before M-1 with a test classifier; only `main.rs` passes `classify_inno` (SC-021).
- **D11, one elevated batch at a time, installers in order.** The helper runs its ops strictly in job order. The core
  holds one process-wide elevation guard around write-job → launch → read-result; a second batch while one runs is
  refused with `E_INSTALL_ELEVATION_BUSY` before any job is written (installers never run concurrently, Q7 and
  Cross-Cutting line 1003-1007). The guard lives in the `Elevation` object, not in a `static` (plan SI-3):
  `Elevation::guard() -> &ElevationGuard`, `ElevationGuard` a core type holding all the guard logic; "process-wide"
  means one `NativeElevation` per process (in Tauri state as `Arc<NativeElevation>`), and each test fake owns its own
  guard, so a fresh `CommandEnv` with a fresh fake is a manager restart and parallel tests never share a guard. The guard is also held after `LaunchOutcome::TimedOut` until the stale job's result appears (FR-078a); there is no queue (Phase 12).
- **D12, the sidecar is merged only into bundle builds.** `bundle.externalBin` lives in a new
  `src-tauri/tauri.helper.conf.json`, passed with `--config` by a Node bundle script on Windows and macOS. The base
  config stays without it, so `cargo build`, `clippy` and `test` need no staged helper (External facts, tauri-build).
  Linux bundles no helper (no elevated op).
- **D13, the macOS AU notice is its own variant.** `Notice::RescanDawMacos` carries the "restart or rescan your DAW"
  text plus Apple's AU steps; Windows keeps `Notice::RescanDaw` (Q7; roadmap line 760-761).
- **D14, every timeout is a named constant.** `INNO_INSTALL_TIMEOUT` 600 s, `PKG_INSTALL_TIMEOUT` 600 s,
  `UNINSTALL_POLL_TIMEOUT` 120 s at `UNINSTALL_POLL_INTERVAL` 500 ms, `PKGUTIL_TIMEOUT` 30 s per run, `KILLALL_WAIT` 5 s;
  the core waits for the launched helper at most the sum over the job's ops plus 120 s (`ELEVATION_MARGIN`). A timed-out
  installer is not killed (the batch stops, FR-042); the timed-out helper process is left to finish, and the elevation guard stays held while it does (FR-078a).
- **D15, helper exit codes.** 0: a result file was written (whatever the op outcomes); 2: usage error (wrong or extra
  arguments), nothing written; 3: the job was refused before any op, a result file with `job_error` was written; 4: the
  job location or the result file could not be used, nothing written. The core reads the result file, never stdout
  (the Windows helper has no console, FR-050).
- **D16, structural ancestors in a pkg receipt are tolerated, never removed (departs from roadmap line 735).** The
  roadmap says `uninstall_pkg` validates **every** path against `AllowedRoots` and "one path outside refuses the whole
  op" (lines 734-736). A plugin pkg is built with `--install-location /` (`release.yml:323-341`), and `pkgutil --files`
  lists directories (External facts, `--only-dirs`); whether that listing includes the ancestors `Library`,
  `Library/Audio`, `Library/Audio/Plug-Ins`, `Library/Application Support` and `Applications` is not documented. If it
  does, the literal rule refuses every real uninstall. So a **directory** entry that equals an allowed root, is an
  ancestor of one, or is `/Applications` is classified *structural*: accepted by validation and never removed (FR-034).
  Nothing else relaxes: every **file** entry, and every directory that is neither structural nor under an allowed root,
  still refuses the whole op (a file directly under an ancestor, e.g. `Library/foo`, is refused, SC-001). The
  `helper-elevated` macOS leg records the raw listing (SC-018), which tells whether the exception is ever exercised.
  FR-123 (e) records the exception in the roadmap's Phase 10 text.
- **D17, the job must sit in the invoking user's state dir (roadmap lines 747-748).** Beyond FR-021's path shape, the
  state dir `<S>` must be a user's own state dir, decided without reading any environment (FR-022):
  - Windows: `<S>` equals `<FOLDERID_UserProfiles>\<one plain component>\AppData\Local\com.krateaudio.krate-manager`
    (compared on the canonical, verbatim form, FR-027), the shape `platform/windows.rs:37-41` produces for a default
    `%LOCALAPPDATA%`. The elevated process cannot read the invoking user's own known folders (over-the-shoulder UAC runs
    it as another account), so the rule binds to "a profile's LocalAppData", not to one SID; a redirected
    `%LOCALAPPDATA%` outside the profiles folder is refused (`E_HELPER_JOB_LOCATION`, Edge cases).
  - macOS: `<S>`'s parent (`Application Support`), `<S>`, `<S>/jobs`, `<S>/staging` and the job file are each owned by
    one and the same uid, and that uid is not 0 (`symlink_metadata` + `MetadataExt::uid`). A same-named directory under
    `/tmp` or any root-owned parent is refused.
  - A `ci-temp-roots` build replaces both rules with exact equality to the `state_dir` its `--ci-roots` file names
    (FR-052).
  The rule is a field of `AllowedRoots` (`state_dir: StateDirRule::{UnderProfiles(PathBuf), SameOwnerNonRoot,
  Exact(PathBuf)}`), so the library tests it with temp dirs (SC-001).
- **D18, the helper library is OS-neutral; the target OS is a parameter.** Every op kind, its validation and its
  execution compile on every OS. `run(args, os: HelperOs, roots, &seams)` takes the target OS (`HelperOs::{Windows,
  Macos, Linux}`); "an op of the other OS" and "Linux rejects every op" (FR-001, FR-036) are properties of the `os`
  argument, not of `cfg`. Only `main_with` (FR-050) passes the cfg-derived `HelperOs::current()`. Hence every library
  SC row runs on every CI leg, each OS value passed explicitly; only the `Os*` seam implementations are cfg-gated.
- **D19, the elevated binary's integrity: residual risk recorded, not engineered away (goes beyond the roadmap; ruled
  by the user on 2026-11-01, Clarifications Q1).** On Windows the manager is installed per-user (`tauri.conf.json:23`, Q5), so the bundled
  `krate-manager-helper.exe` lies in a folder the user can write, and any process running as that user can replace it
  before the UAC prompt; D2, D3, D7 and FR-020 to FR-035 then never apply. On macOS the same holds for an `.app` the
  user copied in (the Phase 11 pkg installs as root; whether the installed helper is root-owned and not user-writable is
  Phase 11's to assert). The spec records this as residual risk because: (i) a process that can write that folder can
  already raise its own elevation prompt for its own binary (`ShellExecuteExW` `runas` / `osascript … with
  administrator privileges` need no privilege to call), and since nothing is signed (the project's no-certificate rule)
  the prompt carries no publisher identity a swap could forge, so the swap adds no capability; (ii) hashing the helper
  in the manager before launch leaves the check-to-`ShellExecuteExW` window open, because the check runs unelevated;
  (iii) copying the helper into an admin-only folder first needs an elevated copier, which is either the unverified
  helper or a second system-binary elevation (two prompts on first use, against "one prompt per batch", line 753-754,
  and a shell-string elevation); (iv) installing the helper under Program Files contradicts Q5's `currentUser` ruling.
  What the spec does enforce: the core elevates only the path `NativeElevation` resolved once at startup beside the
  manager's own executable (FR-090), never a path from the job, feed, settings or UI (SC-028). The user ruled on 2026-11-01 (Clarifications Q1) to accept
  this as residual risk: an unsigned UAC prompt has no publisher identity to forge, so a swap adds no capability. No digest
  check of the helper, no admin-folder copy of it; Q5's `currentUser` install stands; FR-123 (f) records the ruling in the roadmap.

---

## Functional Requirements

### A. The job and result files (roadmap lines 714-717; D1, D2, D15)

- **FR-001** The job is a JSON object with exactly the keys `job_version` (integer 1) and `ops` (an array of 1 to 32
  objects). Each op has exactly the keys of its kind; any unknown key, missing key, duplicate key, wrong type, unknown
  `op` value or `job_version` other than 1 refuses the whole job with `E_HELPER_JOB_PARSE` (exit 3, no op runs). The
  four kinds:
  - `{ "op": "run_inno_installer", "file": <string>, "sha256": <64 lower-hex> }` (target OS Windows)
  - `{ "op": "run_inno_uninstaller", "app_id": <string> }` (target OS Windows)
  - `{ "op": "run_pkg_installer", "file": <string>, "sha256": <64 lower-hex> }` (target OS macOS)
  - `{ "op": "uninstall_pkg", "pkg_id": <string> }` (target OS macOS)
  The target OS is `run`'s `os` argument (D18), not a `cfg`: the schema, the parser and every op compile and are tested
  on every OS. An op whose target OS is not `os` is `E_HELPER_JOB_PARSE` for the whole job. No field carries arguments,
  a root, a destination, a command string, a listing or a log path; `file` is the only path and is checked by FR-020.
- **FR-002** Duplicate keys are detected (a `serde_json::Value` map would keep the last one): the job is parsed with a
  `Deserialize` implementation that rejects a repeated key (`#[serde(deny_unknown_fields)]` structs plus a
  duplicate-key check over the raw object), so `{"op":"run_pkg_installer","op":"uninstall_pkg",…}` is refused.
- **FR-003** The result file `<job stem>.result.json` beside the job is created with `create_new` (fails on any existing
  name, a dangling symlink included; the helper never writes through a link) and holds `{ "result_version": 1,
  "job_sha256": <hex>, "job_error": <code> | null, "ops": [ … ] }` with one entry per job op, in order: `{ "index",
  "op", "outcome": "done" | "locked" | "failed" | "refused" | "not_run", "code": <E_HELPER_*> | null, "exit_code":
  <int> | null, "seconds": <number>, "log_tail": [<string>, …] }`. `log_tail` holds at most the last 40 lines (empty
  lines counted (plan SI-28); each at most 512 bytes, lossy UTF-8) of the op's installer log. With `job_error` set
  (exit 3, the job refused before any op) `ops` is `[]`: a refused job has no parsed ops (plan SI-11). The file is written, `sync_all`ed and closed before exit 0
  or 3. An existing result name is `E_HELPER_RESULT_EXISTS`, exit 4, no op runs; a result file that cannot be created
  for any other reason is `E_HELPER_RESULT_WRITE`, exit 4, nothing written (plan SI-12).
- **FR-004** One failed or refused op never stops the next (roadmap line 715): every op gets its own entry. The one
  exception is FR-042's timeout.
- **FR-005** Arguments: exactly `--job <path> --job-sha256 <hex>` (in either order), plus `--ci-roots <path>` only in a
  `ci-temp-roots` build (D3). Anything else, a repeated flag, or a missing one is exit 2 with one usage line on
  stderr and nothing written. A `--job-sha256` that is not 64 lower-case hex is a usage error too, exit 2: a malformed
  hash can never match, and exit 3 would write a result whose `job_sha256` is not a hash (plan SI-31).

### B. Job location, staging root and source checks (roadmap lines 742-751; D2, D3)

- **FR-020** For every installer op: `file` must be absolute, valid UTF-8, contain no `..` component, satisfy
  `canonicalize(file) == file` (so no component is a link or junction; compared component-wise, ASCII case-insensitive
  on Windows), lie strictly under `StagingRoot`, be a regular file (`symlink_metadata`), and match the extension of its
  op (`.exe` for Inno, `.pkg` for pkg, case-insensitive). Any failure is the op's `refused` with `E_HELPER_SOURCE`.
- **FR-021** `StagingRoot` is derived from the job's location only: the canonical job path must be
  `<S>/jobs/job-<32 lower-hex>.json`, where `canonicalize(job) == job` and the last component of `<S>` is
  `APP_IDENTIFIER` (`platform/mod.rs:13`), and `<S>` passes FR-025; `StagingRoot` = `<S>/staging`. A job elsewhere is
  exit 4, `E_HELPER_JOB_LOCATION`, nothing written.
- **FR-022** The helper reads no environment variable, no settings file and no file other than the job, the staged
  installers it copies, the installer logs it chose, and the OS facts of FR-030 / FR-033. Its root construction takes no
  input from the job (SC-007). The workspace `clippy.toml` `disallowed-methods` apply to the helper unchanged.
- **FR-023** The job bytes are read once (at most 64 KiB; larger is `E_HELPER_JOB_READ`, exit 3) and their SHA-256
  must equal `--job-sha256` before parsing (`E_HELPER_JOB_HASH`, exit 3).
- **FR-024** The helper writes no extended attribute, never adds `com.apple.quarantine`, sets no owner other than its
  own, and changes no ACL except the run directory's creation DACL (D7).
- **FR-025** The state dir `<S>` of FR-021 must satisfy `AllowedRoots.state_dir` (D17): `UnderProfiles(p)` (Windows
  production, `p` = `FOLDERID_UserProfiles` canonicalized): `<S>` == `p\<c>\AppData\Local\com.krateaudio.krate-manager`
  for one plain component `<c>`, compared per FR-027; `SameOwnerNonRoot` (macOS production): `<S>`'s parent, `<S>`,
  `<S>/jobs`, `<S>/staging` and the job file are owned by one uid ≠ 0, each read with `symlink_metadata`;
  `Exact(d)` (`ci-temp-roots` only): `<S>` == `d`. Failure is exit 4, `E_HELPER_JOB_LOCATION`, nothing written.
- **FR-026** At entry, before any op, the library canonicalizes every `AllowedRoots` entry, the `UnderProfiles` /
  `Exact` path and `StagingRoot` once and compares only against those canonical forms ("after canonicalizing both
  sides", roadmap line 750). A root that does not exist yet (a fresh machine has no `C:\ProgramData\Krate Audio`) is
  canonicalized as its longest existing ancestor with the missing plain components re-joined; no existing ancestor, or
  a missing component that is not plain, is exit 4, `E_HELPER_ROOTS`. The job's `file` is **not** made canonical by the
  helper: it must already be canonical (FR-020; the core writes it canonical, FR-073), so a `file` spelled through a
  link is `E_HELPER_SOURCE` even when its target lies inside staging, while roots handed over through a linked parent
  (macOS `/var` → `/private/var`) work.
- **FR-027** Windows path form: every path the core writes into the job (`file`) and onto the argv (`--job`) is the
  verbatim `\\?\…` form `std::fs::canonicalize` returns (External facts); the helper's canonical roots, `StagingRoot`
  and `<S>` are verbatim too (FR-026), so every comparison of FR-020, FR-021, FR-025, FR-031 and FR-034 is made between
  verbatim forms, component-wise, ASCII case-insensitive, the `Prefix` component included (`\\?\C:` equals `\\?\c:`).
  A plain `C:\…` `file` is not canonical and is `E_HELPER_SOURCE`. Paths the helper hands to a child (`<run dir>\…`,
  `/LOG=`) are the plain known-folder forms of D7; the uninstaller is spawned by the registry's own (unquoted) path
  string after its canonical form has passed FR-031.

### C. The fixed operation set (roadmap lines 718-738; D4 to D7)

- **FR-030** `run_inno_installer { file, sha256 }`: (1) use the job's run directory (D7), which the batch runner creates
  just before the job's first installer op; when its creation or D7's ancestor check fails, that op and every later
  installer op of the job are `failed` with `E_HELPER_RUN_DIR` and nothing is spawned for them (uninstall ops still
  run, FR-004); (2) copy `file` to `<run dir>\installer-<index>.exe`, opened read-only without following a final link,
  the destination opened `create_new`, hashing every chunk with SHA-256 as it is written (FR-035); a read or write
  error during the copy (a full disk included) is `failed` with `E_HELPER_COPY` and the partial copy is deleted; the
  digest must equal `sha256`, else `E_HELPER_HASH` and the copy is deleted; (3) spawn the **copy** through `ProcessRunner` with exactly
  the argv `[<copy>, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOCLOSEAPPLICATIONS",
  "/LOG=<run dir>\inno-<index>.log"]` and no shell; a spawn that fails (the copy cannot be started) is `failed` with
  `E_HELPER_SPAWN` (plan SI-12); (4) wait at most `INNO_INSTALL_TIMEOUT`; (5) read the **last** 1 MiB of the log
  (`HelperIo::read_tail`) and map `(exit_code, every line of that tail)` through `seams.classify` (D10; production
  `classify_inno`), so a file-in-use line followed by many rollback or preset lines is still seen; the 40-line `log_tail`
  of FR-003 is for the result only (plan SI-28): `Installed` → `done`, `Locked` → `locked` with
  `E_HELPER_LOCKED`, `Failed` → `failed` with `E_HELPER_EXIT`. The staged original is never opened after step 2 and
  never executed.
- **FR-031** `run_inno_uninstaller { app_id }`: `app_id` must match D4's pattern (`E_HELPER_APP_ID`). The helper reads
  `UninstallString` and `InstallLocation` under `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\<app_id>_is1`
  with `KEY_READ | KEY_WOW64_64KEY` through `Registry` (no key: `E_HELPER_NO_KEY`, outcome `failed`), and spawns
  nothing else first: no run directory is needed (D7). `InstallLocation`
  (trailing `\` trimmed) must canonicalize to a **direct child** of the allowed Windows VST3 vendor root; `UninstallString`,
  with one pair of surrounding double quotes removed and nothing after the closing quote, must canonicalize to a file
  whose parent equals that install directory and whose name matches `^unins[0-9]{3}\.exe$` case-insensitive. Any
  failure is `refused` with `E_HELPER_UNINSTALLER`, nothing run. The helper then spawns that file with exactly
  `[<unins path>, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"]` and polls per D6; on success `done`, on a non-zero
  exit `failed` with `E_HELPER_EXIT`, on a poll timeout `failed` with `E_HELPER_UNINSTALL_TIMEOUT`; a spawn that fails
  is `failed` with `E_HELPER_SPAWN` (plan SI-12). The wait for the spawned uninstaller itself is bounded by
  `UNINSTALL_POLL_TIMEOUT`: an uninstaller that has not exited by then is `failed` with `E_HELPER_UNINSTALL_TIMEOUT`
  (not `E_HELPER_TIMEOUT`: the batch continues, since an uninstaller does not block the next installer the way a
  running setup does) and no poll follows (plan SI-31).
- **FR-032** `run_pkg_installer { file, sha256 }`: steps (1) and (2) of FR-030 with the copy
  `<run dir>/installer-<index>.pkg`, then spawn exactly `["/usr/sbin/installer", "-pkg", <copy>, "-target", "/"]`,
  wait at most `PKG_INSTALL_TIMEOUT`; exit 0 → `done`, otherwise `failed` with `E_HELPER_EXIT`; a spawn that fails is
  `failed` with `E_HELPER_SPAWN` (plan SI-12).
- **FR-033** `uninstall_pkg { pkg_id }`: `pkg_id` must match `^com\.krateaudio\.[a-z0-9]+$` (`E_HELPER_PKG_ID`). The
  helper runs `["/usr/sbin/pkgutil", "--pkg-info", pkg_id]` (no receipt: `failed`, `E_HELPER_PKG_RECEIPT`), takes its
  `package-id` (must equal `pkg_id`) and `location` lines (absent or empty = `/`), then
  `["/usr/sbin/pkgutil", "--files", pkg_id, "--only-files"]` and `["/usr/sbin/pkgutil", "--files", pkg_id,
  "--only-dirs"]`, each bounded by `PKGUTIL_TIMEOUT`; a `pkgutil` run that cannot be started is `failed` with
  `E_HELPER_SPAWN` (plan SI-12). Every listed line (non-empty, a trailing `\r` stripped) is joined
  to `<volume>/<location>` by the same plain-segment join as Phase 9's `dest_path` (a `..`, `.`, empty segment, NUL or absolute
  line refuses the op), where `volume` is `MacosRoots.volume` (D3): production `/`, never settable by `--ci-roots`, a
  temp base in tests, so the join runs against temp dirs on every leg (plan SI-16). Validation runs over **every** line before anything is removed (FR-034); one refusal refuses the
  whole op, nothing removed (`refused`, `E_HELPER_PKG_PATH`). Then: files (and symlinks, removed as the link, never
  followed) are removed; directories under an allowed root are removed deepest first when empty, a non-empty one is
  left; structural directories are never removed. A path already absent counts as removed. When every file entry is
  absent afterwards the helper runs `["/usr/sbin/pkgutil", "--forget", pkg_id]` (failure: `failed`,
  `E_HELPER_FORGET`); when any file entry could not be removed it does not forget (the receipt stays, so a retry
  works) and the op is `failed` with `E_HELPER_REMOVE`.
- **FR-034** Path classification for `uninstall_pkg`, after canonicalizing the entry's **parent** (the entry itself is
  never followed) and re-joining its last component:
  - a file entry is allowed iff it lies strictly under one of the allowed macOS roots, or under
    `/Applications/<N> AUv3.app` where `<N>` matches `contract.rs:137`'s plugin-name pattern;
  - a directory entry is **removable** iff it lies strictly under an allowed root or is (or lies under)
    `/Applications/<N> AUv3.app`; it is **structural** iff it equals an allowed root, is an ancestor of one, or is
    `/Applications` (D16, the one departure from roadmap line 735: tolerated, never removed); every other directory is
    a refusal;
  - the structural class exists for directory entries only: a **file** entry whose parent is structural but not under
    an allowed root (`Library/foo`, `Applications/x`, `foo` at `/`) is a refusal like any file outside the roots;
  - an allowed root itself is never removed.
- **FR-035** Hash-while-copying is one function, `copy_hashed(io, src, dest) -> Result<[u8; 32], HelperError>`, reading
  and writing 64 KiB chunks, the digest over exactly the bytes written, `sync_all` on the destination before it returns.
  The run copy is the only file a `ProcessRunner` spawn ever names as an installer (SC-005).
- **FR-036** There is no other op. No op writes the registry, starts a service, copies to an arbitrary destination,
  renames, or removes anything outside FR-033 / FR-034's validated set; with `os = HelperOs::Linux` every op is
  rejected (exit 3, `E_HELPER_JOB_PARSE`), which is what the Linux binary passes (D18), and the binary is not bundled
  on Linux (D12).

### D. Batch execution and timeouts (roadmap lines 714-717, 778-781; D11, D14)

- **FR-040** Ops run strictly sequentially in job order; no two child processes of the helper run at once.
- **FR-041** Each op's `seconds` is its time from start to outcome as read from `HelperIo::now()` (FR-053; the
  production seam reads a monotonic `Instant` captured at entry), recorded for the M-1 measurement and the logs. Every
  deadline of D6 and D14 inside the helper (the uninstall poll bound, and the installer timeout the runner's `wait`
  is given) is measured on the same clock; no non-test helper code reads `Instant` or `SystemTime` outside `os.rs`
  (SC-021); test sources (`crates/helper/src/*_tests.rs`) may, e.g. SC-004's wall-time bound (plan SI-25).
- **FR-042** An installer that has not exited by its timeout is recorded `failed` with `E_HELPER_TIMEOUT`, is not
  killed, and every later op is `not_run` with `E_HELPER_AFTER_TIMEOUT` (a second installer must not run beside a
  running one). The result file is still written (exit 0).
- **FR-043** After the job's last op (and after a timeout under FR-042) the helper removes the job's one run directory
  (D7; every file it created in it, then the directory), if one was created; a failure there is logged into the
  result's `job_error` only when no op ran, otherwise ignored.

### E. The binary (roadmap lines 742-749; D3, D15)

- **FR-050** The library's module `cli` (`crates/helper/src/cli.rs`; a library module cannot be `main.rs`, the binary
  root) (plan SI-1) exports `main_with(args: Vec<OsString>, seams: &Seams, run: RunFn) -> Exit` with `RunFn
  = fn(&Args, HelperOs, &AllowedRoots, &Seams) -> Exit`, where `Exit { status: u8, code: Option<&'static str> }` with
  `impl From<Exit> for ExitCode` is what `run` returns, because `std::process::ExitCode` exposes no code to compare and
  SC-001's exit-4 rows must name their code (plan SI-17): it parses arguments (FR-005), builds
  `AllowedRoots::production()` (or, in a `ci-temp-roots` build given `--ci-roots`, the roots that file names), takes
  `HelperOs::current()` (the only cfg-derived OS value, D18) and calls `run`. `main.rs` is only `main_with(args_os,
  &production seams, krate_manager_helper::run)` with the OS seams (`OsHelperIo`, `OsRunner`, `OsRegistry` on Windows,
  `classify_inno`); a test passes a recording `run` to observe the roots `main_with` built (SC-007). The Windows binary is a GUI-subsystem executable
  (`#![cfg_attr(windows, windows_subsystem = "windows")]`), so no console window appears behind the UAC prompt.
- **FR-051** `AllowedRoots::production()` on Windows resolves `FOLDERID_ProgramFilesCommon` and `FOLDERID_ProgramData`
  with `SHGetKnownFolderPath` (freeing the string with `CoTaskMemFree`) and joins `VST3\Krate Audio` and `Krate Audio`;
  a lookup failure is exit 4 (`E_HELPER_ROOTS`). On macOS it is four literal paths. The functions that build the roots
  take no argument. The lookup is `os::known_folder(KnownFolder::…)`: `pub enum KnownFolder { ProgramFilesCommon,
  ProgramData, UserProfiles, Windows }` (cfg-free, in `os.rs`) and `#[cfg(windows)] pub fn known_folder(id: KnownFolder)
  -> Option<PathBuf>`, which maps it to `FOLDERID_*` inside `os.rs`, so `roots.rs` names no `windows_sys` item
  (tasks TA-6).
- **FR-052** `ci-temp-roots` is declared in `crates/helper/Cargo.toml` `[features]`, is not a default feature, and no
  crate, `tauri.conf.json`, npm script or release workflow enables it; only the two FR-100 jobs pass `--features
  ci-temp-roots` to build and run the binary, and the `check` job's unelevated `clippy ci-temp-roots` / `test
  ci-temp-roots` steps compile and test it (FR-103a). The helper's `[features]` are exactly `ci-temp-roots = []` and
  `test-support = []`; the fakes of FR-053 live in `seams::fake` behind `test-support`, which `crates/core` enables
  only through its dev-dependency `krate-manager-helper = { workspace = true, features = ["test-support"] }` (the
  helper already dev-depends on core; neither normal graph contains the other crate) (plan SI-19). In such a build the
  `--ci-roots` file is a JSON object naming each plug-in, preset and application root of `AllowedRoots` and the one
  `state_dir` (D17 `Exact`), never `run_base`, `run_ancestors` or `volume` (D3) (plan SI-16); every path must be absolute and, after FR-026's
  canonicalization, lie under the canonical directory that holds the `--ci-roots` file (the binary refuses any other).
  The paths may be spelled through a link (the symlinked-root case, FR-100); their canonical forms may not escape.
- **FR-053** The seams are one struct `Seams { io: &dyn HelperIo, runner: &dyn ProcessRunner, registry: &dyn Registry,
  classify: fn(i32, &[String]) -> InnoVerdict }` of library traits: `HelperIo` (read job, canonicalize,
  `symlink_metadata` with owner uid and mode on Unix, open source, create run dir, `copy_hashed`'s writes, read log,
  remove file / dir, list dir, `sleep(d)`, `now() -> Duration` (monotonic since an arbitrary origin), `random_hex()`
  (the run-directory name, D7), `create_helper_folder()` (D7) and `read_tail(path, bound)` (the last bytes of a log,
  FR-030 step 5), these three new (plan SI-23)), `ProcessRunner`
  (`spawn(argv) -> handle`, `wait(handle, timeout) -> Option<exit code>` (`None` = still running at the timeout),
  `output(argv, timeout) -> Option<(status, stdout)>`) and `Registry` (`uninstall_values(subkey) ->
  Option<(Option<String>, Option<String>)>`, `key_exists(subkey) -> bool`). The test fakes share one fake clock:
  `sleep(d)` advances it by `d`, and the fake runner's `wait` advances it by the scripted run time, or by `timeout` when
  it scripts "never exits". The real implementations (`OsHelperIo`, `OsRunner`, `OsRegistry`) live in
  `crates/helper/src/os.rs` and are constructed only in `main.rs`; outside test code `Seams` is built only there
  (SC-021). Both rules apply to non-test sources only: `crates/helper/src/os_tests.rs` builds the real seams over a
  `TempDir` too, so the no-follow, non-blocking, regular-file-only open of the job and the installer source,
  `read_bounded`'s bound, `create_run_dir`'s mode and `OsRunner::wait`'s no-kill timeout
  reach a gate (plan SI-25). Tests pass fakes and either a test classifier or `classify_inno` itself (the replay tests, SC-016).

### F. The locked-file measurement (roadmap lines 762-766, 803-807; D10)

- **FR-060** `manager-ci` job `inno-locked-file (windows-latest)` (FR-102) compiles the fixture Inno script twice,
  `/DVersion=1.0.0` and `/DVersion=1.0.1`, from fixture inputs whose DLL and unlocked file differ per version (the
  `locked-dll` crate embeds the version string at build time, and the unlocked file is a text file holding it), so an
  update ships different bytes as a real one does. It installs 1.0.0, records the SHA-256 of the 1.0.0 and 1.0.1 DLL and
  unlocked file (the "old" and "new" digests), maps the installed 1.0.0 DLL into a separate process, runs the 1.0.1
  setup with `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LOG=<file>`, and records, each as a
  `::notice` annotation (Phase 4 D21: the token here cannot read logs or artifacts): the exit code; the run time in
  seconds; whether the setup exited within 600 s; for the mapped DLL and for the unlocked file, the installed file's
  SHA-256 afterwards and its class `old` / `new` / `other` / `absent` against the two known digests; whether
  `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager` gained a `PendingFileRenameOperations` value and whether that
  value names the DLL; the uninstall key's `DisplayVersion` afterwards; and the log lines that mention the DLL's name or
  the words `DeleteFile`, `in use`, `Retry`, `Abort` or `restart` (at most 40, one annotation each). The combination
  answers the roadmap's "whether files are replaced" (line 763): replaced (`new`), skipped (`old`), or scheduled
  (`old` plus a pending rename naming it).
- **FR-061** The measurement is written into this spec's section **M-1** (date, run id, every recorded value, the
  verbatim log lines) and into `apps/krate-manager/tests/fixtures/helper/inno-locked/` (`exit_code.txt`, `log.txt`,
  `seconds.txt`, `classes.json` (`{"dll": "new" | "old" | "scheduled", "unlocked": …}`) and `locked_line.txt` (the
  recorded file-in-use line's fixed part, one line, no newline), so the CI job has a machine input to compare against)
  (plan SI-26)
  by the build stage, from the annotations, once the user has run the job.
- **FR-062** In the same change, `classify_inno` gains the measured rule: `Locked` iff the exit code equals the
  recorded one **and** the log contains the recorded file-in-use line's fixed part (a substring, not a regex over
  user data; the constant `classify::LOCKED_LINE` equals `locked_line.txt`, asserted by
  `classify::tests::recorded_locked_case`) (plan SI-26), every other non-zero code staying `Failed`. If the measurement shows the setup returns 0 with the DLL
  class `old` (a silent skip, with or without a scheduled rename) or hangs, the rule and the user-facing text are
  re-ruled with the user before the build closes; the row stays open until then. The rule is exactly the recorded exit code
  plus the one log-line substring measured on the current Inno (user ruling 2026-11-01, Q4): there is no second Inno version
  measured and no exit-code-only rule. A setup whose Inno version logs differently from the recorded line is `Failed`, so the
  core reports `E_INSTALL_INSTALLER_FAILED { exit_code }` for it, never `E_INSTALL_LOCKED`.
- **FR-063** The Phase 10 build stops at the M-1 fence (user ruling 2026-11-01, Q2). Once every requirement that does not
  depend on the `inno-locked-file` run is built and green, the build stage writes no M-1 section, no `inno-locked/` fixture
  and no measured `classify_inno` rule, reports those open items, and does not close Phase 10 (the roadmap status line says
  so, FR-123 (h)). The user pushes; on resume the build writes M-1, the fixtures and the measured rule inside Phase 10
  (roadmap line 765), then closes it. Phase 10 is never closed with the provisional rule, and M-1 is not moved to Phase 12.

### G. The core: drivers, job writing, launching (roadmap lines 752-761; D8, D9, D11, D13)

- **FR-070** `install::driver_for(os)` returns `&WINDOWS` for `Os::Windows`, `&MACOS` for `Os::Macos` and `&LINUX` for
  `Os::Linux`. `WindowsDriver` and `MacosDriver` implement `InstallDriver` (`install.rs:335-338`); their logic is
  OS-neutral code compiled and tested on every OS; only `driver_for` decides where each is offered.
- **FR-071** `InstallEnv` and `CommandEnv` gain `elevation: &'a dyn Elevation` and `host_io: &'a dyn HostIo`;
  `native_env(ctx, io, extract, download, elevation, host_io)` passes them through. `InstallEnv` also gains `host: &'a
  dyn HostFacts`: the commands already take `host` (`install.rs:2850-2858`) and pass it in, and FR-076a re-runs
  `detect_plugin` with the facts of the action's target OS, which a `PlatformFacts` built from `host_io` would not give
  in a macOS-target test (plan SI-4). `pub trait Elevation: Sync { fn
  helper(&self) -> Option<&Path>; fn launch(&self, command: &ElevatedCommand, timeout: Duration) -> LaunchOutcome; fn
  guard(&self) -> &ElevationGuard; }` (the third method holds D11's guard) (plan SI-3)
  with `ElevatedCommand { program: PathBuf, args: Vec<String> }` and `LaunchOutcome::{Exited(i32), Refused,
  Failed(&'static str), TimedOut}`. The Linux driver never calls either field.
- **FR-072** Per action, before the job: `Install` / `Update` need `artifact.format()` `InnoExe` (Windows) or `Pkg`
  (macOS), else `E_INSTALL_FORMAT`; the artifact's path is canonicalized through `InstallIo::canonicalize` and must lie
  under the canonical `staging_dir()`, else `E_INSTALL_PATH`. `Uninstall` needs the gate's `RemoveBasis` (FR-080):
  `Inno { app_id }` / `Pkg { pkg_id }`; `OutsideRoot` is `E_INSTALL_REMOVE_ELSEWHERE`; `NotManaged` is
  `E_INSTALL_NOT_MANAGED`. A refused action gets its `ActionResult` and no op; when no op remains, nothing is written
  and nothing is launched.
- **FR-073** `install::job`: the job (FR-001's schema, keys in that order, compact JSON, `file` the canonical path) is
  written under `<state>/jobs/` through `InstallIo` (`create_dir` when absent, `write_new` of
  `job-<32 lower-hex>.json`, the name a `RandomState`-derived, collision-checked name, not a secret: two
  `std::collections::hash_map::RandomState::new()` hashers over a process counter, a collision caught by `write_new`
  and retried up to 4 times; the workspace has no RNG crate and none is added (plan SI-9); `sync_file`); its SHA-256 comes from
  `verify::Sha256Stream` over the written bytes. A write error maps by `ErrorKind` as Phase 9 FR-029 does, with step
  `job_written`. The job and the result file are removed (`remove_file`, step `job_removed`) after the result is read,
  on every path including refusal; a removal error is one `warn!`.
- **FR-074** `install::recover` (Phase 9 FR-070) also removes, under `<state>/jobs/`, every entry whose name matches
  `job-<32 lower-hex>(.result)?.json`, regular files only, never following a link; nothing else there is touched.
  (A helper still running from a crashed manager writes its result with `create_new` beside a removed job; its
  outcome is lost, and detection at the next status shows the true state.)
- **FR-075** The driver sends `InstallProgress::Elevating` once, just before `launch`, after any `Download` events.
- **FR-076** `launch` is called once per batch with `ElevatedCommand { program: helper, args: ["--job", <canonical job
  path>, "--job-sha256", <hex>] }` and D14's batch ceiling (the sum of the job's op timeouts plus `ELEVATION_MARGIN`). `helper()` returning `None` (the sidecar is not beside the
  executable, e.g. a dev run) is `E_INSTALL_HELPER_MISSING` for every action of the job, before anything is written.
  Mapping: `Refused` → every action `E_INSTALL_REFUSED` ("administrator permission was not given; nothing was
  changed"); `Failed(code)` → every action `E_INSTALL_ELEVATION { code }`; `TimedOut` → every action
  `E_INSTALL_NO_RESULT` and the guard stays held (FR-078a); `Exited(0 | 3)` → the result file is read (bounded 1 MiB)
  and checked in this order (plan SI-11): its `job_sha256` must equal the job's, else every action
  `E_INSTALL_NO_RESULT`; then `job_error` set → every action `E_INSTALL_HELPER { code }` (its `ops` is `[]`, FR-003);
  then its `ops` length must equal the job's, else every action `E_INSTALL_NO_RESULT`; otherwise per op: `done` → `Ok(Outcome { version: <entry version for install / update,
  None for remove>, placed: 0, removed: 0, kept: [] })`, `locked` → `E_INSTALL_LOCKED`, `failed` / `refused` /
  `not_run` → `E_INSTALL_HELPER { code }`, except `E_HELPER_EXIT` → `E_INSTALL_INSTALLER_FAILED { exit_code }`; any
  other exit → every action `E_INSTALL_NO_RESULT`. Results come back in batch order, refused-before-job actions in their
  place. A `done` action is `Ok` only after FR-076a.
- **FR-076a** Confirmation by detection (user ruling 2026-11-01, Q5). After the batch result is mapped (FR-076), the driver
  re-runs `detect_plugin` (through the same `HostFacts` and `InstallIo` the status uses; no mutating call) once for each
  action whose op was `done`. An `Install` / `Update` whose detection has no class, or whose class `Version` is not the
  entry's version, and an `Uninstall` whose detection still holds a managed copy or an installer record, becomes
  `E_INSTALL_NOT_APPLIED` (text FR-133) instead of `Ok`. Detection reads `moduleinfo.json`, not the DLL (roadmap Key
  decision 6), so this is a named-error floor, not a proof that every file was replaced. A `locked`, `failed`, `refused`
  or `not_run` action is not re-detected.
- **FR-077** After a macOS batch in which at least one install or update is `done`, the driver calls
  `host_io.run(&platform::KILLALL_ARGV, KILLALL_WAIT)` once and ignores the outcome, where `platform/mod.rs` gains the
  cfg-free `pub const KILLALL_ARGV: [&str; 3] = ["/usr/bin/killall", "-9", "AudioComponentRegistrar"];` (so the macOS
  driver is tested on every leg; `install.rs` may not hold a `"/usr/` literal) (plan SI-5). No
  other process is signalled anywhere; no code path enumerates processes (Q7).
- **FR-078** The process-wide elevation guard (D11: the `ElevationGuard` of `Elevation::guard()`, one per
  `NativeElevation`, one `NativeElevation` per process) (plan SI-3) is taken before the job is written and released when the result
  has been read (on unwind too); a second batch meanwhile returns `E_INSTALL_ELEVATION_BUSY` for each of its actions with
  zero mutating `InstallIo` calls and no launch.
- **FR-078a** Stale hold (user ruling 2026-11-01, Q3). After `LaunchOutcome::TimedOut` the elevation guard is not released and
  the job and result files are not removed; the guard records the stale job's stem. While it holds that stem, every new elevated
  batch is refused `E_INSTALL_ELEVATION_BUSY { stale: true }` before any job is written, with zero mutating `InstallIo` calls
  and no launch, and is never queued (a queue is Phase 12). The hold ends when `<state>/jobs/<stem>.result.json` appears
  (polled: an `InstallIo` existence check at the start of every new elevated batch), at which point the job and result files
  are removed (FR-073) and the guard is released, or when the manager restarts (the guard is in memory; FR-074 removes the
  stale files). The refused card shows the named state of FR-133, "an installer is still running". The timed-out batch's own
  actions stay `E_INSTALL_NO_RESULT`.
- **FR-079** New `InstallError` variants, each `Display` starting with its code: `E_INSTALL_REFUSED`,
  `E_INSTALL_ELEVATION { code }`, `E_INSTALL_ELEVATION_BUSY { stale: bool }`, `E_INSTALL_NOT_APPLIED`, `E_INSTALL_HELPER_MISSING`, `E_INSTALL_NO_RESULT`,
  `E_INSTALL_HELPER { code }`, `E_INSTALL_LOCKED`,
  `E_INSTALL_INSTALLER_FAILED { exit_code }`, `E_INSTALL_REMOVE_ELSEWHERE` (text: "installed outside the standard
  folder; uninstall it from Windows Settings"). One text source (plan SI-10): where FR-133 gives a sentence, the
  `Display` after the code is exactly that sentence (`E_INSTALL_REFUSED`, `E_INSTALL_LOCKED` "A plug-in file is in use.
  Close your DAW and retry.", both `E_INSTALL_ELEVATION_BUSY` forms, `E_INSTALL_NOT_APPLIED`), so the UI shows a
  rejection verbatim and TypeScript holds no code-to-text table; `E_INSTALL_FORMAT`'s text becomes "the artifact is not
  this system's installer format". `Step` gains `JobWritten` (`job_written`) and `JobRemoved`
  (`job_removed`). No variant holds an absolute path.

### H. Gate, actions and commands (roadmap lines 728-730, 753-757; D9)

- **FR-080** `install::action_gate(os, ctx, io, entry, detection: &Detection)` keeps Phase 9's checks and order and
  returns `Open { manifest, remove }` with `remove: RemoveBasis::{NotManaged, Manifest, Inno { app_id }, Pkg { pkg_id },
  OutsideRoot}`: Linux `Manifest` iff a manifest (Phase 9 unchanged); Windows `Inno { app_id }` iff the detection's
  record is `InstallerRecord::Inno` with `install_location: Some(loc)`, the entry has a `windows_uninstall_key`, and
  `loc`'s parent is `same_path`-equal to `<Vst3Global root>\Krate Audio` (`detect.rs:481-488`), `OutsideRoot` when the
  record has a location that is not, else `NotManaged`; macOS `Pkg { pkg_id }` iff the record is `InstallerRecord::Pkg`
  and the entry has a `macos_pkg_id`, else `NotManaged`.
- **FR-081** `allowed_actions` offers `Remove` iff `remove` is `Manifest`, `Inno` or `Pkg`; `Install` and `Update` keep
  Phase 9's rule (`install.rs:2754-2761`), so a downgrade or an equal version is never offered and is refused by FR-093's
  table (Phase 9) before any download: the Inno installer itself would accept a downgrade, so the core gate is the guard.
- **FR-082** `for_command` computes the detection before the gate (`install.rs:2932-2938` swap); `remove_for_command`
  gains `host: &dyn HostFacts` and computes the plugin's detection (one listing scan for its `vst3_bundle`, as
  `install_command` does) before the gate; `Open { remove: NotManaged | OutsideRoot }` returns `E_INSTALL_NOT_MANAGED`
  / `E_INSTALL_REMOVE_ELSEWHERE`.
- **FR-083** `status::build_with_os` computes gates for every OS with a driver, over the detection it already holds;
  `PluginView` gains `remove_hint: Option<RemoveHint>` (`"windows_settings"` iff the gate is `OutsideRoot`), last field
  after `actions`; `view.ts` gains the field with its parser check; the 13 `tests/fixtures/status/*.json` gain
  `"remove_hint": null` as each plugin object's last key.
- **FR-084** The commands keep their shapes: `install_plugin(name, expected_version, action, on_progress)` and
  `remove_plugin(name, on_progress)`; one action per batch, so one elevation prompt per command. `ActionSummary.notice`
  is `Some(RescanDaw)` after a Windows or Linux install / update, `Some(RescanDawMacos)` after a macOS one, `None` after
  a Remove.

### I. src-tauri: launcher, bundling, capability (roadmap lines 104-110, 752-754; D8, D12)

- **FR-090** `src-tauri/src/os_elevation.rs`: `NativeElevation { helper: Option<PathBuf> }`, built once in `lib.rs`
  setup from `current_exe()`'s parent joined with `krate-manager-helper` (`.exe` on Windows), `None` when that file is
  absent or on Linux. It is built only by `krate_manager_lib::resolve_elevation(exe: Option<&Path>) -> NativeElevation`
  in `src-tauri/src/lib.rs`: `run()` passes `std::env::current_exe()`, FR-105's example a synthetic exe path inside the
  cargo profile directory. The path rule is the pure core function `krate_manager_core::install::helper_beside(os: Os,
  exe: Option<&Path>, io: &dyn InstallIo) -> Option<PathBuf>` (in core because `src-tauri`'s library has `test = false`):
  `exe.parent()` must be non-empty; it is joined with `krate-manager-helper` plus `.exe` iff `Os::Windows`; `None` when
  `io.kind_of` of that path is not a file, when `exe` is `None` or has no non-empty parent (a bare name never resolves
  against the CWD), or for `Os::Linux`; `resolve_elevation` calls it with the native `Os` and `FsInstallIo` (plan
  SI-14). The path is resolved once and never changes; the core's `ElevatedCommand.program` is always
  exactly `Elevation::helper()` (D19, SC-028); the core neither hashes nor copies the helper file (user ruling 2026-11-01, Q1). `launch` executes exactly the core's command:
  - Windows: on its own thread, `CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`, then
    `ShellExecuteExW` with `lpVerb "runas"`, `lpFile` the helper, `lpParameters` = `install::windows_parameters(args)`,
    `fMask SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI`, `nShow: 0` (the value of `SW_HIDE`,
    written as the literal with that comment because the constant needs the `Win32_UI_WindowsAndMessaging` feature,
    External facts); `FALSE` with
    `GetLastError() == ERROR_CANCELLED` → `Refused`, other failures → `Failed("E_ELEVATION_SHELLEXECUTE")`; then
    `WaitForSingleObject(hProcess, timeout)`, `GetExitCodeProcess`, `CloseHandle`.
  - macOS: `std::process::Command::new("/usr/bin/osascript")` (the path literal lives in `os_elevation.rs`, never in
    `install.rs`) (plan SI-5) with the args `-e`, `install::OSASCRIPT_LINES[0]`, `-e`,
    `[1]`, `-e`, `[2]`, then the helper path and the command's four args, bounded by the timeout; the outcome is
    `install::osascript_outcome(status, stderr)`.
  `OSASCRIPT_LINES` is the fixed script `on run argv` / `do shell script (quoted form of item 1 of argv) & " " & (quoted
  form of item 2 of argv) & " " & (quoted form of item 3 of argv) & " " & (quoted form of item 4 of argv) & " " &
  (quoted form of item 5 of argv) with administrator privileges` / `end run`; no string from the job, the feed or the
  UI is ever concatenated into script text. `osascript_outcome`: exit 0 → `Exited(0)`; stderr containing `(-128)` →
  `Refused`; stderr ending in `(<n>)` → `Exited(n)`; otherwise `Failed("E_ELEVATION_OSASCRIPT")`.
- **FR-091** `install::windows_parameters(args)` refuses any argument containing `"` or NUL (`E_INSTALL_ELEVATION {
  code: "E_ELEVATION_ARGS" }`), wraps an argument holding a space or tab in double quotes and doubles its trailing
  backslashes before the closing quote (the MS C rules, External facts). The verbatim `\\?\` prefix (FR-027) is ordinary
  text to these rules (`?` is neither a delimiter nor a quote) and is passed through unchanged. Linux `launch` is
  `Failed("E_ELEVATION_NONE")` (never called, FR-071).
- **FR-092** Bundling (D12): `src-tauri/tauri.helper.conf.json` is `{ "bundle": { "externalBin":
  ["binaries/krate-manager-helper"] } }`; `apps/krate-manager/scripts/bundle.js` (Node, `#!/usr/bin/env node`, a usage
  line, `node --test` coverage of its path logic run by FR-109) builds `cargo build -p krate-manager-helper --release --locked`,
  copies the binary to `src-tauri/binaries/krate-manager-helper-<rustc --print host-tuple>[.exe]` and runs `tauri build
  --config src-tauri/tauri.helper.conf.json` on Windows and macOS, plain `tauri build` on Linux; `package.json` gains
  `"bundle": "node scripts/bundle.js"`; `src-tauri/binaries/` is git-ignored. `bundle.js` builds the host-triple
  helper only: a host-tuple name cannot carry a universal binary, so the universal macOS helper (a `lipo` of the
  aarch64 and x86_64 builds under the sidecar name the universal target needs) is Phase 11's, added to its roadmap text
  by FR-123 (d). The capability file is unchanged: the
  webview gets no `shell:*`, `fs:*` or `http:*` permission, and `tauri-plugin-shell` is not a dependency.
  `config_guard.rs`'s pins widen exactly so far (plan SI-8): `tauri.helper.conf.json` is allowed beside the three
  platform files with exactly `{"bundle":{"externalBin":["binaries/krate-manager-helper"]}}`; `src-tauri` `[features]`
  is exactly `["dev-feed = []", "manual-elevation = []"]`; the `package.json` scripts are today's six plus `"bundle":
  "node scripts/bundle.js"`; the helper's `[features]` is exactly `["ci-temp-roots = []", "test-support = []"]`.
- **FR-093** `windows-sys` 0.61.2: the workspace feature list becomes exactly the five of today plus `Win32_UI_Shell`,
  `Win32_System_Com`, `Win32_System_Threading`, `Win32_Security`, `Win32_Security_Authorization`,
  `Win32_Storage_FileSystem`; it is a `[target.'cfg(windows)'.dependencies]` entry of `src-tauri` and `crates/helper`
  only, never of `crates/core`. `injection.rs` `READER_PINS` records the new feature list and both homes;
  `crates/helper/Cargo.toml` `[dependencies]` is exactly `serde`, `serde_json`, `sha2` plus that Windows entry.

### J. CI jobs and guards (roadmap lines 788-807; Cross-Cutting lines 994-996)

- **FR-100** `manager-ci.yml` job `helper-elevated` (runs only when FR-102b's `changes` job says so; matrix `macos-latest`, `windows-latest`; name `helper-elevated
  (${{ matrix.os }})`; `permissions: contents: read`; no secret, no environment) builds `cargo build -p
  krate-manager-helper --release --locked --features ci-temp-roots` and the fixtures, creates temp roots under
  `$RUNNER_TEMP/real` (a state dir `$RUNNER_TEMP/real/com.krateaudio.krate-manager` with `jobs/` and `staging/`, the
  plug-in roots, and the `--ci-roots` file at `$RUNNER_TEMP/real/ci-roots.json` naming them and that `state_dir`),
  writes jobs with `node scripts/write-test-job.js` (FR-001's schema and key order; the helper's own parse in the job is
  the check that it agrees, SC-011 being the Rust-side proof), runs the helper elevated (macOS `sudo`; Windows the
  runner's own session, elevated because UAC is disabled (External facts), proven by a step that asserts the process
  token is elevated before any helper run) and asserts each result and the placed tree with `::notice` / `::error`
  annotations. Every path the jobs write to lies under `$RUNNER_TEMP` (FR-111). The helper is a GUI-subsystem program
  on Windows (FR-050), so every Windows step that runs it uses `shell: pwsh` with `$p = Start-Process -FilePath <helper>
  -ArgumentList … -Wait -PassThru` and asserts `$p.ExitCode` and the result file's presence (External facts,
  `Start-Process`); the macOS steps run it under the job's `shell: bash` default with `sudo` and `$?`. The
  symlinked-root case, on both legs, uses a test-made link `$RUNNER_TEMP/link` → `$RUNNER_TEMP/real` (macOS `ln -s`,
  Windows `New-Item -ItemType SymbolicLink`, admin on the runner): a second `ci-roots-link.json`, in
  `$RUNNER_TEMP/real`, names every root and the `state_dir` through `$RUNNER_TEMP/link/…`; a job whose `file` is the
  canonical path inside staging is accepted, and one whose `file` is spelled through the link is refused
  (`E_HELPER_SOURCE`), so the case stays inside `$RUNNER_TEMP` and FR-052's parent rule (the roadmap's elevated case,
  line 799, names no particular link; the native `/var` → `/private/var` case is the unelevated SC-008).
  - macOS: `pkgbuild --root <stage> --identifier com.krateaudio.fixture --version 1.0.0 --install-location
    $RUNNER_TEMP/real/root fixture.pkg` over a stage holding `Library/Audio/Plug-Ins/VST3/Fixture.vst3/…`,
    `Library/Audio/Plug-Ins/Components/Fixture.component/…`, `Library/Application Support/Krate Audio/Fixture/a.vstpreset`
    and `Applications/Fixture AUv3.app/…`; `run_pkg_installer` places the four trees; the raw `pkgutil --files
    com.krateaudio.fixture --only-dirs` output is annotated; the fixture pkg's `preinstall` (added with `pkgbuild
    --scripts`, running as root) writes `$1` and `stat -f '%u %Sp %Lp'` of `dirname "$1"` to a path under
    `$RUNNER_TEMP` baked in at build time, so the job asserts the run directory's owner uid 0, mode `0700` and type
    while the pkg op runs (`$1` is annotated too, so a wrong assumption about the script's arguments reads red, not
    vacuously green); `uninstall_pkg com.krateaudio.fixture` removes them, keeps the structural directories, and
    `pkgutil --pkgs` no longer lists the id; then the symlinked-root case above runs elevated (FR-020, FR-026).
  - Windows: a stub installer (`tests/fixtures/argv-recorder`, a tiny binary that writes its own argv,
    `current_exe()` and the SDDL of its own directory, read with `pwsh -NoProfile -Command "(Get-Acl -LiteralPath
    '<dir>').Sddl"`, to a file under `$RUNNER_TEMP` named by a constant compiled into the fixture) run through
    `run_inno_installer` proves the exact argv, that the program ran from the run directory, and the run directory's
    DACL (SC-017); a fixture Inno script (FR-101) compiled with `Minionguyjpro/Inno-Setup-Action@v1.2.7`
    (`release.yml:244`) and `/DRoot=$RUNNER_TEMP\real\root` (its `DefaultDirName` `{#Root}\VST3\Krate Audio\Fixture`, a
    fixed fixture AppId) is installed through `run_inno_installer` and removed through `run_inno_uninstaller` with its
    real HKLM key; the job asserts the tree after each, the key's absence after uninstall, and the helper's `seconds`
    for the uninstall poll.
- **FR-101** The fixture Inno script and the fixture DLL crate live under `apps/krate-manager/tests/fixtures/inno/` and
  `tests/fixtures/locked-dll/` (a `cdylib` excluded from the workspace members, as `clippy-guard` is,
  `Cargo.toml:3`); the Inno fixture places the DLL at `Fixture.vst3\Contents\x86_64-win\Fixture.vst3` (a real
  plug-in's layout). No fixture setup is ever run outside these jobs. The fixture script `tests/fixtures/inno/fixture.iss`
  copies the plugin scripts' behaviour-deciding lines verbatim: `PrivilegesRequired=admin`, `UsePreviousAppDir=yes`,
  `ArchitecturesAllowed=x64compatible`, `ArchitecturesInstallIn64BitMode=x64compatible` (`setup.iss:50`, `:53`,
  `:63-64`), every `[Files]` entry's `Flags:` value (`ignoreversion recursesubdirs createallsubdirs`, `:72-77`) and the
  `[UninstallDelete]` entry (`Type: filesandordirs; Name: "{app}\<Name>.vst3"`, `:102-104`); only the AppId, the names
  and every root differ: `{commonpf}\Common Files` becomes `{#Root}` (so `DefaultDirName={#Root}\VST3\Krate
  Audio\Fixture`) and `{commonappdata}` becomes `{#Root}\ProgramData` (presets at `{#Root}\ProgramData\Krate
  Audio\Fixture`), so no fixture run writes the runner's real `C:\ProgramData` (FR-111). A `node --test` case
  `fixture inno script keeps the plugin directives` (`apps/krate-manager/scripts/inno-fixture.test.js`, run by FR-109)
  normalises both `fixture.iss` and `plugins/iterum/installers/windows/setup.iss` by exactly those substitutions (plugin
  name → `Fixture`, AppId → a placeholder, the two roots) and requires the listed lines to be equal, and no line of
  `fixture.iss` to name `{commonpf}`, `{commonappdata}`, `{pf}`, `{sys}` or `{win}`; a planted drift in each listed line
  is reported. Reading `setup.iss` is not a change to it (FR-122).
- **FR-102** `manager-ci.yml` job `inno-locked-file` (`windows-latest`, gated by FR-102b) performs FR-060: install the 1.0.0 fixture
  setup, start `pwsh -NoProfile -Command "[System.Runtime.InteropServices.NativeLibrary]::Load('<dll>'); Start-Sleep
  900"` in the background, wait until that process reports the load, run the 1.0.1 setup with the FR-030 flags and a
  600 s ceiling (`Start-Process -Wait -PassThru` with a 600 s `Wait-Process -Timeout`, so the exit code is the setup's
  own), annotate the FR-060 values, then stop the holder process. The fixture presets go to `{#Root}\ProgramData`
  under `$RUNNER_TEMP` (FR-101). Until M-1 is recorded the job asserts only that every value was recorded (a
  measurement, not a verdict on Inno); after M-1, FR-102a.
- **FR-102a** After M-1 is recorded (FR-061) `inno-locked-file` also asserts, from the values it measures: the setup's exit
  code equals `tests/fixtures/helper/inno-locked/exit_code.txt`; the log contains the recorded file-in-use line's fixed part
  (FR-062); the DLL and unlocked-file classes equal the ones recorded in M-1. The expected values are read from the
  fixture files, never from this spec's markdown: the classes from `inno-locked/classes.json`, the line's fixed part from
  `inno-locked/locked_line.txt` (plan SI-26). A mismatch is an `::error` and the job reads red,
  so an Inno or runner-image change that alters them is seen (user ruling 2026-11-01, Q8).
- **FR-102b** `manager-ci.yml` has (or gains) a `workflow_dispatch` trigger and a `changes` job (`ubuntu-24.04`, because
  the shape test refuses the floating `latest` Ubuntu label anywhere in the file, `manager-ci-shape.test.js:797` (plan SI-2);
  `permissions: contents: read`, no secret, no environment; `actions/checkout@v6` with `fetch-depth: 0`) with one bash
  changed-paths step that outputs `elevated=true` when the
  event is `workflow_dispatch`, or when the changed files include a path under `apps/krate-manager/crates/helper/` or under the fixtures these two jobs consume
  (`apps/krate-manager/tests/fixtures/{helper,inno,locked-dll,argv-recorder}/`), `apps/krate-manager/scripts/write-test-job.js`,
  `apps/krate-manager/Cargo.toml`, `apps/krate-manager/Cargo.lock` (they fix the helper's `serde`, `serde_json`, `sha2`
  and `windows-sys` versions and features, so a dependency bump rebuilds the elevated binary) (plan SI-27)
  or `.github/workflows/manager-ci.yml`; otherwise `false`. The changed files are, for `push`, `git diff --name-only
  "$BEFORE" "$SHA"` (`github.event.before`..`github.sha`) and, for `pull_request` (the workflow triggers on it too), the
  base sha to the head sha; an all-zero `before` or a failing `git diff` (history rewritten) is `true` (fail open: the
  elevated jobs run). Event values reach the script through `env:`, never `${{ }}` inside `run:` (plan SI-18). `helper-elevated` and `inno-locked-file` carry `needs: changes`
  and `if: needs.changes.outputs.elevated == 'true'`; `check` is not gated and runs on every trigger; a skipped job is not a
  failure (user ruling 2026-11-01, Q8).
- **FR-103** A `check`-job step `ci-temp-roots absent` (every leg, after the bundle step): `cargo tree -p
  krate-manager-helper -e features --locked` lists no `ci-temp-roots`; the marker string `--ci-roots` occurs at least
  once in `crates/helper/src/cli.rs` (where `main_with` parses it, FR-050) (plan SI-1) and zero times in the helper the bundle actually carries (Windows: the
  `krate-manager-helper.exe` extracted from the NSIS setup by FR-104; macOS: `Krate Manager.app/Contents/MacOS/
  krate-manager-helper`) and in the staged `src-tauri/binaries/krate-manager-helper-<triple>[.exe]`.
- **FR-103a** Two `check`-job steps, every leg, run the feature-gated code unelevated so it is inside the zero-warnings
  gate: `clippy ci-temp-roots` (`cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked
  -- -D warnings`) and `test ci-temp-roots` (`cargo test -p krate-manager-helper --features ci-temp-roots --locked`,
  its log separate from `cargo-test.txt`, so the `required tests ran` counts of FR-108 are unaffected). Both build into
  the debug profile, never the release binary FR-103 inspects; neither runs the binary elevated or with `--ci-roots`
  against a real root (the `--ci-roots` cases of that run use temp files and the fake seams). Test
  `cli::tests::ci_roots_file_is_read` (feature build only; module `cli`) (plan SI-1) asserts the roots a `--ci-roots` file yields and that a root
  outside its parent is exit 4, `E_HELPER_ROOTS`.
- **FR-104** The `check` job's `tauri build` step runs `npm run bundle` instead (FR-092). On Windows and macOS a step
  `helper bundled` asserts the helper is in what the bundler produced, never in cargo's output: Windows: `7z l` of the
  NSIS setup under `target/release/bundle/nsis/` lists an entry `krate-manager-helper.exe` in the same directory as
  the entry `krate-manager.exe`, and `7z e` of that entry has the SHA-256 of the staged `src-tauri/binaries/…` file
  (NSIS does not modify it; `7z` absent or no such entry is `::error`); macOS:
  `Krate Manager.app/Contents/MacOS/krate-manager-helper` exists and `codesign --verify --deep --strict` still passes,
  the existing step, `manager-ci.yml:278-287` (no hash: the bundler's ad-hoc signing rewrites the file); on Linux, that
  no helper is in the `.deb`.
- **FR-105** The opt-in manual check is `src-tauri/examples/manual_elevation.rs` with `required-features =
  ["manual-elevation"]` (a feature of `src-tauri`, never default), run only by a person with `cargo run -p krate-manager
  --example manual_elevation --features manual-elevation -- <empty dir>`. It refuses to start unless its argument is an
  existing, empty directory. It writes its job under
  `<empty dir>/com.krateaudio.krate-manager/jobs/`, which it creates, and afterwards removes only the files and
  directories it created (never the user's real state dir, FR-110); it builds its launcher through `resolve_elevation`
  (FR-090), raises the real prompt and prints the `LaunchOutcome`. The job's single op is one the helper's own matcher
  refuses before any spawn, registry read or filesystem effect, never "an id that has no receipt" (on macOS the user's
  own `<empty dir>` passes `SameOwnerNonRoot`, so the op runs as root) (plan SI-14): macOS `uninstall_pkg { pkg_id:
  "com.krateaudio.NOT-VALID" }` (fails the `pkg_id` pattern: upper case and `-`) → `refused` `E_HELPER_PKG_ID`;
  Windows `run_inno_uninstaller { app_id: "not-a-guid" }` → `refused` `E_HELPER_APP_ID` should the location ever pass.
  Expected outcome: Windows `Exited(4)` (`E_HELPER_JOB_LOCATION`, FR-025); macOS `Exited(0)` with exactly one `refused`
  `E_HELPER_PKG_ID` entry, zero spawns and no run directory (an uninstall-only job creates none, D7). The check proves
  the prompt and the launch, and no op can have an effect. Since `required-features` hides the example from every plain cargo command (External facts), one `check` step,
  `clippy manual-elevation example` (`cargo clippy -p krate-manager --example manual_elevation --features
  manual-elevation --locked -- -D warnings`), compiles and lints it on every leg and never runs it. No other workflow
  line, script, npm script or gate names `manual-elevation`.
- **FR-106** No job other than the two of FR-100 / FR-102 builds the helper **binary** with `ci-temp-roots`, runs
  `sudo`, compiles or runs an Inno script, runs `pkgbuild` or `installer`, or runs the helper binary; `check` runs the
  helper only as `helper_usage_exits_2` does (no arguments, exit 2), and compiles the feature only in FR-103a's two
  unelevated steps.
- **FR-107** `tools/feed-contract/tests/manager-ci-shape.test.js`: `jobs` equals `['check', 'changes', 'helper-elevated',
  'inno-locked-file']`; each new job has `contents: read`, no `environment:`, no secret; the `--features ci-temp-roots`
  token occurs only in those two jobs and in `check`'s two FR-103a steps (exactly those step names, `clippy` / `test`
  commands with `-p krate-manager-helper`); `manual-elevation` occurs only in `check`'s `clippy manual-elevation
  example` step; `sudo` only in `helper-elevated`; every Windows step of the two new jobs that names the helper binary
  has `shell: pwsh` and `Start-Process` with `-Wait -PassThru` (FR-100); FR-103's, FR-103a's, FR-104's and FR-109's
  steps exist on `check`, and both elevated jobs are gated as FR-102b says; FR-106's bans are tested (plan SI-29):
  `pkgbuild`, `/usr/sbin/installer`, `installer -pkg`, `Minionguyjpro/Inno-Setup-Action`, `ISCC` and a run of the helper
  binary with arguments occur only in `helper-elevated` / `inno-locked-file`, with plants in `check` and `changes`; each with planted mutations reported (a removed
  gate on either job included).
- **FR-108** `tests/required-tests.json` and `EXPECTED_REQUIRED` gain every Rust, vitest and Node name of SC-001 to
  SC-032 (the Node names under a new `node` key that FR-109's step reads), in disjoint lanes (names without suffix
  `cargo.all`; `_windows`, `_macos`, `_unix` as Phase 9 FR-116); they lose
  `helper_stub_exits_2_with_one_line`, `install::tests::no_driver_off_linux`, `install::tests::
  native_wiring_offers_nothing_windows`, `install::tests::native_wiring_offers_nothing_macos` and the vitest title
  `renders no install buttons on windows or macos`, each replaced by its Phase 10 successor named in SC-023, SC-012,
  SC-024 and SC-025; `status::tests::actions_per_state` keeps its name with its table extended (SC-013).
- **FR-109** A `check`-job step `node --test` (every leg, between `lint` and `vitest`, i.e. before `vitest`, so that
  `required tests ran` still directly follows `vitest` as `manager-ci-shape.test.js:878-880` requires) (plan SI-15) runs `node --test
  --test-reporter=tap "scripts/*.test.js"` from `apps/krate-manager/` into `$RUNNER_TEMP/node-tap.txt`;
  `required tests ran` reads that file too and requires every name of `required-tests.json`'s `node` list to pass
  exactly once, as it does for cargo and vitest (`manager-ci.yml:156-177`). `feed-tools.yml` is unchanged
  (`:83` runs only `tools/feed-contract/tests/*.test.js`).

### K. Safety guards and scope fences (Cross-Cutting lines 987-1028)

- **FR-110** No test, gate or agent runs the helper elevated, raises a UAC or osascript prompt, runs an Inno setup,
  `installer`, `pkgutil --forget`, `killall`, or reads or writes the real HKLM uninstall keys **on the dev box**. Every
  helper library test runs unelevated against `fresh_temp_roots()`-style temp roots through the fake seams; the core
  tests use a fake `Elevation` and a fake `HostIo`.
- **FR-111** The only exceptions are FR-100 and FR-102 on ephemeral GitHub-hosted runners, user-triggered by a push or `workflow_dispatch`,
  writing only under `$RUNNER_TEMP` (the fixture setups' presets included, FR-101), the admin-only run directory, the
  pkg receipt database entry of `com.krateaudio.fixture` (macOS, forgotten again by the job), and (Windows) the
  fixture's own HKLM uninstall key.
- **FR-112** `injection.rs` gains: `ShellExecuteExW`, `runas`, `osascript`, `administrator privileges` only in
  `src-tauri/src/os_elevation.rs` and `crates/core/src/install.rs` (the constants); `/usr/sbin/installer`,
  `--forget`, `CreateDirectoryW`, `SHGetKnownFolderPath` only in `crates/helper/src/`; `OsRunner`, `OsRegistry`,
  `OsHelperIo` constructed, and `Seams` built outside `#[cfg(test)]` code, only in `crates/helper/src/main.rs`;
  `Instant::now` and `SystemTime` only in `crates/helper/src/os.rs` within the helper (FR-041); these two rules apply to
  non-test sources only (`is_test_source`, `injection.rs:1559-1567`): the real seams may also be built in
  `crates/helper/src/os_tests.rs`, `Instant` / `SystemTime` may occur in `crates/helper/src/*_tests.rs`, and their plants
  go into `ops.rs` (plan SI-25); `NativeElevation`
  constructed only in
  `src-tauri/src/lib.rs` (by `resolve_elevation`, FR-090); the `/usr/bin/osascript` path only in `os_elevation.rs`, and
  the `killall` argv literal only in `crates/core/src/platform/mod.rs` (`KILLALL_ARGV`, FR-077), pinned there by
  `PLATFORM_LITERALS`: `install.rs` uses the constant (`"/usr/` is banned in core files outside `platform/`,
  `injection.rs:23-31`, `:1316-1320`) (plan SI-5) and `install_tests.rs` compares the registrar argv with
  `platform::KILLALL_ARGV` only, never the literal (`install_tests.rs` is an `FR088_FILES` entry, which may not contain
  `/usr/`) (tasks TA-12); Phase 9's ban on `Command`, `TcpStream` and
  `xattr` in `install.rs` stays (the core never spawns: `killall` goes through `HostIo::run`). Existing workspace-wide
  rules widen their homes exactly so far (plan SI-6): `HOST_FFI` (`RegOpenKeyExW`, `RegGetValueW`, `RegCloseKey`) +
  `crates/helper/src/os.rs`; `HOST_LITERALS` (`/usr/sbin/pkgutil`, the Uninstall key) + `crates/helper/src/ops.rs` (the
  constants) and `crates/helper/src/ops_tests.rs` (the exact-argv assertions, the one test source exempt from the
  test-source "no host literal" rule); `CONFINED_TOKENS` `windows_sys` + `src-tauri/src/os_elevation.rs` and
  `crates/helper/src/os.rs` (`roots.rs` names none, FR-051); `CONFINED_TOML_KEYS` `windows-sys` and `sha2` +
  `crates/helper/Cargo.toml`; the `sha2` tokens + `crates/helper/src/digest.rs` (the helper's only `sha2` user). FR-024
  gets its guard (plan SI-29): `xattr`, `setxattr`, `removexattr`, `chown`, `fchown`, `lchown`, `SetNamedSecurityInfo`,
  `SetFileSecurity`, `SetSecurityInfo` and `com.apple.quarantine` never occur in the helper's non-test sources under
  `crates/helper/src/`. Each rule with a plant.
- **FR-113** The roster guard (Phase 5) covers `crates/helper/src/` and `os_elevation.rs`: no plugin name; the fixture
  names `Fixture` and `com.krateaudio.fixture` occur only under `tests/fixtures/` and the two CI jobs.
- **FR-114** Logs: one `info!` per action result (Phase 9 FR-117, unchanged format) plus, per batch, one `info!` with
  the launch outcome and, per op, the helper's `code`, `exit_code`, `seconds`; at most the 40 `log_tail` lines as
  `debug!`; no absolute path in an `info!` line.
- **FR-120** Phase 9's Linux behaviour is unchanged: every Phase 9 SC name keeps its verdict except the five FR-108
  replaces.
- **FR-121** No change to `extract.rs`, `download.rs`, `verify.rs`, `feed.rs`, `detect.rs` behaviour; `status.rs` gains
  only FR-083; `context.rs` gains nothing (the recovery addition is inside `install::recover`).
- **FR-122** No change to `plugins/`, `dsp/`, `cmake/`, `extern/`, `release.yml`, `feed.yml`, `ci.yml`, any plugin
  `setup.iss` or pkg script, or `tools/feed-contract/` except `tests/manager-ci-shape.test.js` (FR-107).
- **FR-123** Roadmap amendments (`specs/Krate-manager-roadmap.md`), made by the build stage: (a) Phase 4 line 477-479:
  the elevation launcher is injected through `CommandEnv` beside the driver resolver (D8), not `AppContext`; (b) Phase 10
  records the M-1 result and the measured "locked" rule (FR-061, FR-062); (c) the Phase 10 status line; (d) Phase 11
  Components (the universal macOS bullet, lines 827-838): "The release build stages a universal `krate-manager-helper`
  (`lipo -create` of the aarch64 and x86_64 helper builds) under the sidecar name the universal target needs, merges
  `src-tauri/tauri.helper.conf.json` (Phase 10 D12), and the PENDING-CI `release-manager.yml` macOS row asserts `lipo
  -archs` on `Contents/MacOS/krate-manager-helper` lists `x86_64 arm64`", and the matching measurable outcome at line
  864; (e) Phase 10 lines 734-736: after "one path outside refuses the whole op", "(a directory entry that is an allowed
  root, an ancestor of one, or `/Applications` is structural: accepted and never removed; a file entry never is,
  Phase 10 D16)"; (f) Phase 10 Components gain one line recording the per-user helper's residual risk, accepted by the user on 2026-11-01
  (no digest check, no admin-folder copy; Q5's `currentUser` install stands); (g) Phase 12's first-launch documentation gains:
  a redirected `%LOCALAPPDATA%` is unsupported on Windows (every elevated batch is refused `E_HELPER_JOB_LOCATION`, D17);
  (h) Phase 10's status line records that the build stops at the M-1 fence until the user has pushed and `inno-locked-file`
  has run (FR-063). No other line changes.

### L. UI (roadmap lines 756-761; Q7, Q8)

- **FR-130** Windows and macOS cards render the `actions` the core lists, exactly as Linux cards do (Phase 9 FR-097).
- **FR-131** The confirmation adds one line on Windows and macOS: "Windows will ask for administrator permission once." /
  "macOS will ask for your administrator password once."; the Remove confirmation reads "Remove {name}? Its installer's
  uninstaller removes the plug-in and its factory presets; your own presets are kept." on Windows and macOS.
- **FR-132** Progress text for `Elevating`: "Waiting for administrator permission and the installer…".
- **FR-133** Outcome texts: `E_INSTALL_REFUSED` → "Administrator permission was not given; nothing was changed.";
  `E_INSTALL_LOCKED` → "A plug-in file is in use. Close your DAW and retry."; `RescanDawMacos` → the Windows / Linux
  rescan line plus Apple's four AU steps in Apple's order (report line 34; Apple Support 122179; per_os_install_mechanics.md
  lines 163, 180): "If a Logic or GarageBand AU does not appear: 1. Restart the Mac. 2. In Logic's Plug-in Manager, use
  'Reset & Rescan Selection'. 3. Still missing: use 'Full Audio Unit Reset', then reopen Logic. 4. Last resort: move the
  files in ~/Library/Caches/AudioUnitCache out, restart, and reopen Logic (the rescan can take a while)." followed by
  "Apple's guide: support.apple.com/en-us/122179" as plain text (the webview loads and opens no remote content, and
  this phase adds no opener permission). `E_INSTALL_ELEVATION_BUSY { stale: true }` → "An installer is still running. Wait for it to finish, then retry." (the named
  "installer still running" state, FR-078a); `{ stale: false }` → "Another install is running. Retry when it finishes.";
  `E_INSTALL_NOT_APPLIED` → "The installer finished but the change was not applied. Check the installed version and retry."
  Every failure line starts with its code (Phase 9 FR-098).
- **FR-134** A card whose `remove_hint` is `windows_settings` shows "Installed outside the standard folder: uninstall it
  from Windows Settings → Apps." and no Remove button.
- **FR-135** After any outcome the card's buttons are re-enabled and the status refreshes through `plugin_status`
  (Phase 9 FR-098); after `E_INSTALL_REFUSED` the card's actions are exactly what they were before.

---

## Success Criteria

Rust SCs run locally on Windows with `cargo test --workspace --locked` from `apps/krate-manager/` (default threads; a
run counts only with every binary `0 failed`); vitest with `npm test`; Node with `node --test "scripts/*.test.js"` from
`apps/krate-manager/`, in CI by FR-109's `check` step (not `feed-tools.yml`, whose set is
`tools/feed-contract/tests/*.test.js` only, `feed-tools.yml:83`). Because the helper library takes the target OS as a
parameter (D18), every helper-library row runs on every leg with each OS value passed explicitly; only the rows marked
`_windows` / `_macos` / `_unix` touch an OS facility. macOS and Linux rows are **PENDING-CI** on `manager-ci.yml` `check
(macos-latest)` / `check (ubuntu-24.04)`, step `cargo test`, closed by that leg's `required tests ran` notice
(`manager-ci.yml:153-178`); the local WSL lane compiles and tests the `cfg(unix)` code of `-p krate-manager-core` and
`-p krate-manager-helper` (with the helper's `ci-temp-roots` clippy) before the push, but closes no PENDING-CI row
(plan SI-22). Elevated rows are **PENDING-CI** on
`helper-elevated (macos-latest | windows-latest)` and `inno-locked-file (windows-latest)`, closed by the job's
`::notice` annotations, never by a local run. Helper tests run against temp roots with the fake seams of FR-053; core
tests use a fake `Elevation` that records each `ElevatedCommand` and returns a scripted outcome after optionally running
the **helper library** in-process against temp roots (a "loopback" fake) — never the binary, never elevated.

- **SC-001 (crafted jobs refused, roadmap lines 769-774).** `validate::tests::rejects_crafted_jobs` (helper crate):
  every row runs `krate_manager_helper::run` with the `os` the op kind targets (`HelperOs::Windows` for the Inno rows,
  `HelperOs::Macos` for the pkg rows, both for the OS-independent rows) over temp roots with a recording
  `ProcessRunner`, `Registry` and `HelperIo`; each yields its code, zero spawns, zero removals, and the temp tree
  (including a planted `outside/` sibling) byte-identical before and after. The temp state dir passes FR-025 through an
  injected `StateDirRule` (`UnderProfiles(<tmp>/profiles)` with `<S>` = `<tmp>/profiles/u/AppData/Local/com.krateaudio.
  krate-manager` for the Windows rows; `SameOwnerNonRoot` for the macOS rows, the recording `HelperIo` reporting owner
  uid 501 for the state-dir paths on every leg):

  | Job defect | Exit / outcome, code |
  |---|---|
  | installer `file` outside `StagingRoot` | op `refused`, `E_HELPER_SOURCE` |
  | `file` with a `..` component | op `refused`, `E_HELPER_SOURCE` |
  | `file` through a symlinked directory inside staging (`_unix` row; Windows row with `symlink_dir`) | op `refused`, `E_HELPER_SOURCE` |
  | `file` a symlink to a staged file | op `refused`, `E_HELPER_SOURCE` |
  | `sha256` not the file's | op `failed`, `E_HELPER_HASH`, the copy deleted |
  | unknown `op` value; an extra field on an op (`"args"`, `"roots"`, `"settings_roots": ["C:\\Windows"]`, `"dest": "/etc"`, `"log"`); a duplicate key; `job_version` 2; 33 ops; 0 ops | exit 3, `E_HELPER_JOB_PARSE`, no op entry runs |
  | an Inno op with `os = Macos`; a pkg op with `os = Windows`; each of the four valid ops with `os = Linux` (three rows, one per `HelperOs` value, each on every leg, D18) | exit 3, `E_HELPER_JOB_PARSE` |
  | job bytes ≠ `--job-sha256` | exit 3, `E_HELPER_JOB_HASH` |
  | job of 64 KiB + 1 byte (the hash matching) | exit 3, `E_HELPER_JOB_READ`, the bytes read stop at the bound |
  | job outside `<…>/com.krateaudio.krate-manager/jobs/`; job name not `job-<32 hex>.json` | exit 4, `E_HELPER_JOB_LOCATION`, nothing written |
  | a correctly shaped `<tmp>/elsewhere/com.krateaudio.krate-manager/jobs/job-<hex>.json` under `UnderProfiles(<tmp>/profiles)`; `<S>` two levels below a profile (`profiles/u/x/AppData/Local/…`); under `SameOwnerNonRoot`, `<S>`'s parent owned by uid 0 (the `/tmp` case), the job file owned by another uid, `jobs/` owned by uid 0 (FR-025, D17) | exit 4, `E_HELPER_JOB_LOCATION`, nothing written |
  | result file already present (a planted symlink at its name) | exit 4, `E_HELPER_RESULT_EXISTS`, the link and its target untouched |
  | the result file's `create_new` fails with `PermissionDenied` (plan SI-31) | exit 4, `E_HELPER_RESULT_WRITE`, nothing written |
  | `app_id` not a GUID (`{8F7C…}_is1`, lower-case hex, `..\x`) | op `refused`, `E_HELPER_APP_ID` |
  | fake registry: `UninstallString` outside the plug-in root; in the root but not `unins###.exe`; with an argument after the quoted path; `InstallLocation` nested two levels deep | op `refused`, `E_HELPER_UNINSTALLER` |
  | `pkg_id` `com.krateaudio.Iterum`, `com.example.x`, `com.krateaudio.a/b` | op `refused`, `E_HELPER_PKG_ID` |
  | fake `pkgutil --only-files` listing with one `../etc/passwd` or one `etc/hosts` among valid entries; `--only-dirs` with `usr/local` | op `refused`, `E_HELPER_PKG_PATH`, nothing removed, no `--forget` spawn |
  | `--only-files` listing a file directly under a structural ancestor (`Library/foo`, `Library/Audio/foo`, `foo` at `/`) among valid entries (D16: the exception covers directory entries only) | op `refused`, `E_HELPER_PKG_PATH`, nothing removed, no `--forget` spawn |
  | Windows `file` given in plain `C:\…` form while staging is verbatim (FR-027) | op `refused`, `E_HELPER_SOURCE` |

  `validate::tests::performs_valid_jobs` runs each of the four ops valid, each with its own `os`, and asserts outcome
  `done` and the recorded calls (SC-002); its Windows row gives `file`, the job path and `StagingRoot` in verbatim
  `\\?\` form (on a non-Windows leg as literal strings through the fake `HelperIo`) and is accepted, proving FR-027's
  comparison. `validate::tests::verbatim_paths_windows` (lane `cargo.windows`) does the same with real
  `std::fs::canonicalize` output of a temp dir.
- **SC-002 (exact argv, roadmap lines 775-777).** `ops::tests::exact_argv` (helper): the recorded spawns are exactly
  `[<run dir>\installer-0.exe, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOCLOSEAPPLICATIONS",
  "/LOG=<run dir>\inno-0.log"]`, `[<install dir>\unins000.exe, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"]`,
  `["/usr/sbin/installer", "-pkg", <run dir>/installer-0.pkg, "-target", "/"]`, the three `pkgutil` reads and
  `["/usr/sbin/pkgutil", "--forget", "com.krateaudio.alpha"]`; for both installer ops the SHA-256 of the file at the
  spawned path equals the job's `sha256`, and no spawn names a path under `StagingRoot`.
- **SC-003 (batch continues past a locked plugin, roadmap line 778-779).** `ops::tests::batch_of_three_middle_locked`:
  three `run_inno_installer` ops with a fake runner whose second spawn exits 99, and `Seams.classify` set to a test
  classifier that maps exit 99 to `Locked` and otherwise delegates to `classify_inno` (FR-053, D10); the result has
  entries `done`, `locked` (`E_HELPER_LOCKED`), `done`, the third spawn happened after the second exited, exactly one
  run directory was created (before the first spawn) and removed (after the third, D7), and exit is 0. The core twin
  `install::tests::batch_locked_middle_maps_each` (loopback fake, the same test classifier) returns `Ok`,
  `E_INSTALL_LOCKED`, `Ok` in batch order. `ops::tests::batch_timeout_stops_the_rest`: three installer ops whose second
  `wait` scripts "never exits" → entries `done`, `failed` `E_HELPER_TIMEOUT`, `not_run` `E_HELPER_AFTER_TIMEOUT`;
  exactly two spawns (none after the timed-out one), no kill call, the fake clock advanced by exactly
  `INNO_INSTALL_TIMEOUT` during the second op, exit 0, the result file written. `ops::tests::uninstall_only_job_has_no_run_dir`:
  a job of two `run_inno_uninstaller` ops creates no run directory. `ops::tests::run_dir_failure_fails_installers`: the
  fake `HelperIo` fails "create run dir" → every installer op `failed` `E_HELPER_RUN_DIR`, zero spawns for them, an
  uninstall op in the same job still runs. `ops::tests::copy_error_is_named`: a write fault (`StorageFull`) during
  `copy_hashed` → `failed` `E_HELPER_COPY`, the partial copy removed, the next op still runs.
  `ops::tests::spawn_error_is_named` (plan SI-31): a fake runner whose `spawn` fails for op 1 of 2 → op 1 `failed`
  `E_HELPER_SPAWN`, op 2 runs and is `done`. `ops::tests::classify_sees_whole_bounded_log` (plan SI-31): a recording
  classifier receives every line of a log whose matching line is followed by 500 more lines (the result's `log_tail`
  holds only the last 40), and a 3 MiB log hands over only its last MiB (FR-030 step 5).
- **SC-004 (uninstall polling, roadmap lines 780-781).** `ops::tests::uninstall_polls_until_gone` with the fake clock of
  FR-053: the fake uninstaller exits 0 at once while the fake filesystem removes the `.vst3` entry, the `unins000.exe`
  and the key once the fake clock reaches 3 s → `done`, `seconds` == 3 (read from `HelperIo::now()`, FR-041), 6
  recorded `sleep(500 ms)` calls; `ops::tests::uninstall_poll_times_out`: they never disappear → `failed`,
  `E_HELPER_UNINSTALL_TIMEOUT`, 241 checks with 240 recorded `sleep(500 ms)` calls between them, `seconds` == 120, no
  real time spent (the test's own wall time is asserted below 5 s); a non-zero exit → `failed`, `E_HELPER_EXIT` with no
  poll. `ops::tests::uninstaller_never_exits` (FR-031) (plan SI-31): the spawned uninstaller never exits → its `wait`
  bounded by `UNINSTALL_POLL_TIMEOUT` returns none → `failed` `E_HELPER_UNINSTALL_TIMEOUT`, zero poll checks, the next op
  runs.
- **SC-005 (TOCTOU, roadmap lines 739-741, 770).** `ops::tests::runs_the_copy_never_the_original`: a fake `HelperIo`
  rewrites the staged original right after the copy completes → the spawn names the run copy, the copy's hash equals the
  job's, outcome `done`; a fake that rewrites the original **during** the copy (bytes differ from the hash) →
  `E_HELPER_HASH`, zero spawns; a staged file replaced by a symlink before the open → `E_HELPER_SOURCE`, zero spawns.
  The fake opens through the production no-follow open (FR-053), and on Unix a FIFO swapped in before the open is
  `E_HELPER_SOURCE` with zero spawns and the op returns in under 1 s of wall time. The real seams over a `TempDir`
  (plan SI-25): `os::tests::open_source_refuses_links_and_fifos_unix` (lanes `cargo.macos`, `cargo.linux`) and
  `os::tests::open_source_refuses_link_windows` (lane `cargo.windows`): a final link (and on Unix a FIFO) is refused
  without blocking, a regular file opens; `os::tests::read_bounded_stops_at_bound` (every leg): a `MAX + 1`-byte read
  of a 2 × `MAX` file returns `MAX + 1` bytes, and `read_tail` of the same file returns its last `MAX`.
- **SC-006 (macOS path classification, FR-034).** `ops::tests::pkg_paths_classified`: a fake receipt for
  `--install-location /` whose `--only-dirs` lists `Library`, `Library/Audio`, `Library/Audio/Plug-Ins`,
  `Library/Audio/Plug-Ins/VST3`, `Library/Audio/Plug-Ins/VST3/Alpha.vst3`, `…/Contents`, `Library/Application Support`,
  `Library/Application Support/Krate Audio`, `…/Krate Audio/Alpha`, `Applications`, `Applications/Alpha AUv3.app` and
  whose `--only-files` lists the bundle, component, preset and AUv3 files: every file removed, the bundle / preset /
  AUv3 directories removed deepest first, every structural directory and every allowed root untouched, a non-empty
  `…/Krate Audio/Alpha` (a planted user file) left; `--forget` spawned once, last. A listing with `Applications/Other.app`
  or `Applications/alpha AUv3.app` (lower-case first letter) is `E_HELPER_PKG_PATH`, nothing removed.
  `ops::tests::pkg_failure_paths` (FR-033), each row with `os = Macos` on every leg: `--pkg-info` reports no receipt →
  `failed` `E_HELPER_PKG_RECEIPT`, no `--files` read; its `package-id` line differs from `pkg_id` →
  `E_HELPER_PKG_RECEIPT`, nothing removed; one listed file the fake `HelperIo` cannot remove → `failed`
  `E_HELPER_REMOVE`, the other files removed, **no** `--forget` spawn (the receipt stays, so a retry works); a retry of
  the same op with the file now removable → `done` and `--forget` spawned once; `--forget` exiting non-zero → `failed`
  `E_HELPER_FORGET`; a `pkgutil` that cannot be started (`--pkg-info`, either `--files`, `--forget`) → `failed`
  `E_HELPER_SPAWN` (plan SI-12), nothing removed before `--forget`. `result::tests::log_tail_bounds` (FR-003): a log of 100 lines keeps the last 40; a log of 45 lines
  interleaved with empty lines keeps the last 40 lines counting the empty ones (plan SI-31); a 2000-byte line
  is cut to 512 bytes at a UTF-8 boundary; invalid UTF-8 becomes U+FFFD; an empty or missing log gives `[]`.
  `ops::tests::run_dir_removed_after_job` (FR-043): after a done job, a failed job and a timed-out job the fake
  `HelperIo` records the removal of every file created in the run directory and then of the directory itself.
- **SC-007 (roots are constants, roadmap lines 782-784).** `roots::tests::binary_roots_ignore_job_and_settings`: the
  library's `main_with(args, seams, run)` (FR-050) is driven with a recording `run` and a job fixture at
  `<tmp>/com.krateaudio.krate-manager/jobs/job-<hex>.json` plus a settings fixture at the manager's real relative
  location beside it, `<tmp>/com.krateaudio.krate-manager/settings.json`, both naming extra roots (`C:\Windows`, `/etc`,
  a temp dir) in every key the manager's settings and FR-001 know, so a regression that read either file would see
  them; the recording `run` captures the `AllowedRoots` it was handed: they
  equal `AllowedRoots::production()` (Windows: `C:\Program Files\Common Files\VST3\Krate Audio` and
  `C:\ProgramData\Krate Audio` on the runner and the dev box, read-only known-folder lookups; macOS: the four literals);
  `job::tests::no_field_adds_a_root` asserts the job types' field sets are exactly FR-001's (a `deny_unknown_fields`
  plant with a `roots` key is refused). `roots::tests::production_roots_windows` (lane `cargo.windows`) and
  `roots::tests::production_roots_macos` (lane `cargo.macos`, PENDING-CI) assert the literal values.
- **SC-008 (canonicalization under a symlinked temp root, roadmap lines 785-787).** `validate::tests::symlinked_parent_root_unix`
  (macOS: the native `/var` → `/private/var`; Linux: a test-made link) and `validate::tests::symlinked_parent_root_windows`
  (`symlink_dir`, which works unelevated on the dev box, verified 2026-11-01), each over `real/` and a link `link` →
  `real` (macOS: the temp dir itself, reached as `/var/…` → `/private/var/…`), with these inputs and verdicts (FR-020,
  FR-026, FR-034):

  | `AllowedRoots` / `StagingRoot` given as | Input | Verdict |
  |---|---|---|
  | the linked spelling (`link/…`, `/var/…`) | an installer `file` given as its canonical path inside canonical staging | accepted, `done` |
  | the linked spelling | the same `file` spelled through the link (`link/…/x.exe`, `/var/…/x.pkg`) | `refused`, `E_HELPER_SOURCE` (not canonical) |
  | the canonical spelling | a `file` inside staging whose parent is a link (inside staging) to a directory outside it | `refused`, `E_HELPER_SOURCE` |
  | the linked spelling | a pkg `--only-files` entry under an allowed root, reached normally | accepted and removed |
  | the linked spelling | a pkg entry whose parent is a link inside the root pointing outside every root | `refused`, `E_HELPER_PKG_PATH`, nothing removed |
  | a root whose tail does not exist yet under the linked spelling | a `run_pkg_installer` with a canonical `file` | accepted (FR-026's longest-existing-ancestor rule) |

  PENDING-CI for the `_unix` rows on `check (macos-latest)` / `check (ubuntu-24.04)`; the elevated case is SC-018 /
  SC-017 (the `$RUNNER_TEMP/link` case of FR-100).
- **SC-009 (refusal at the prompt, roadmap line 790).** `install::tests::refused_prompt_changes_nothing`: the fake
  `Elevation` returns `Refused` for an install, an update and a remove on Windows and on macOS: each action is
  `E_INSTALL_REFUSED`, the verified artifact is still cached (a retry makes 0 asset requests), `<state>/jobs/` is empty,
  zero helper-library runs, and the fail-closed recorder shows only the job write and its removal. vitest `refused
  permission shows nothing changed and keeps the actions` (FR-133, FR-135).
- **SC-010 (one prompt per batch, roadmap lines 753-754).** `install::tests::one_launch_per_batch`: `WindowsDriver::run`
  over `[Install A, Update B, Uninstall C]` (artifacts verified at `<staging>/downloads/<sha hex>/<asset>`, the Phase 7
  layout `download.rs:29`, `:115-131`, because FR-072 refuses an artifact outside canonical staging (plan SI-20); C with an
  Inno record) calls `launch` exactly once with a
  job of exactly three ops in order (the job bytes the fake read at launch, parsed back) (plan SI-31), and `MacosDriver::run` the same with pkg ops; a batch whose every action is refused
  before the job calls `launch` zero times and writes nothing.
- **SC-011 (core and helper agree on the files, D1).** `crates/helper/tests/core_jobs.rs`
  `core_written_jobs_parse`: jobs written by the core's `install::job` for all four op kinds parse under the helper's
  schema when the helper's `run` is given each op's own `os` (D18; both OS values exercised in the one test, on every
  leg), their hash matches the core's, and a result written by the helper's `result::write` maps through the core's
  FR-076 reader to the expected outcomes; one byte changed in either direction fails.
- **SC-012 (drivers and gate per OS, FR-070, FR-080, FR-081).** `install::tests::drivers_per_os` replaces
  `no_driver_off_linux`: `driver_for` returns a driver for all three OSes with the matching `os()`. `install::tests::
  gate_remove_basis`: Windows record with `InstallLocation` `<Vst3Global>\Krate Audio\Alpha\` → `Inno { app_id }`
  (`windows_uninstall_key` up to and including its first `}`, D4) (plan SI-7); `<sandbox>\elsewhere\Alpha` (inside the `fresh_temp_roots()` sandbox, outside
  `<Vst3Global>\Krate Audio`, so `detect_plugin`'s `check_sandboxed` guard, `detect.rs:1047-1050`, lets it through) →
  `OutsideRoot`; no record → `NotManaged`; macOS pkg record → `Pkg { pkg_id }`; Linux unchanged.
  `install::tests::remove_elsewhere_refused`: `remove_for_command` over a fake Inno record at
  `<sandbox>\elsewhere\Alpha` → `E_INSTALL_REMOVE_ELSEWHERE`, no launch. `install::tests::record_outside_sandbox_panics`
  (`#[should_panic(expected = "outside the test sandbox")]`): a fake record naming a path outside the sandbox (an
  absolute path under a second, unrelated temp dir, never a real system path) panics in the existing guard
  (`roots.rs:173-184`), so no fixture can name the real system.
- **SC-013 (actions per state, FR-083).** `status::tests::actions_per_state` (extended): for `(Os::Windows,
  Arch::X86_64)` and `(Os::Macos, Arch::Aarch64)` over Phase 6's fixture states the actions equal the expected table
  (not installed → `["install"]`; update available with a record → `["update", "remove"]`; up to date with a record →
  `["remove"]`; installed newer with a record → `["remove"]`; a managed copy with no record → its Install / Update per
  update state and no `remove`; Windows record outside the root (`InstallLocation` `<sandbox>\elsewhere\Alpha`, as in
  SC-012, never a real path), update available → `["update"]` with `remove_hint`
  `"windows_settings"`, up to date → `[]` with the same hint; journal pending → `[]`; not available / needs newer OS →
  `remove` only when a record exists); and for every omitted action `for_command` / `remove_for_command` refuse with
  their code before any asset request and with no launch.
- **SC-014 (end to end through the commands, roadmap lines 755-758).** `install::tests::windows_install_update_remove`
  and `install::tests::macos_install_update_remove`: over a fixture feed with a `windows-x86_64` `inno-exe` (resp.
  `darwin-*` `pkg`) asset signed with a throwaway key, `for_command` install → update and `remove_for_command`, with
  `os` passed explicitly, artifacts verified at `<staging>/downloads/<sha hex>/<asset>` (plan SI-20), roots from the new
  `test_support::fresh_elevated_roots()` whose state dir is `<base>/com.krateaudio.krate-manager` (spelled with
  `platform::APP_IDENTIFIER`, because FR-021 requires that last component and `fresh_temp_roots()` names it
  `<base>/state`) (plan SI-21), and the loopback `Elevation` (`#[cfg(test)]` code in
  `crates/core/src/install_loopback_tests.rs`) (plan SI-19) whose fake runner "installs" by writing **both** the state a
  real installer leaves: the fake record (Windows: an Inno record with `InstallLocation` = `<temp Vst3Global>\Krate
  Audio\Alpha` and `DisplayVersion` = the installed feed version; macOS: a pkg receipt for `com.krateaudio.alpha` with
  that version and its file list) **and** the bundle under the temp roots (`<temp Vst3Global>\Krate
  Audio\Alpha\Alpha.vst3`, resp. `<temp VST3>/Alpha.vst3` and `<temp Components>/Alpha.component`) with a
  `moduleinfo.json` of that version; it "uninstalls" by removing both. Each command makes exactly one `launch` and
  returns the FR-084 notice (`RescanDaw` / `RescanDawMacos` / `None`); `status::build` afterwards yields exactly: after
  install of feed version `1.0.0`, Alpha installed at `1.0.0`, `update_state` `up_to_date`, `actions` `["remove"]`;
  after the feed moves to `1.1.0`, `actions` `["update", "remove"]`, and after the update, installed at `1.1.0`,
  `up_to_date`, `["remove"]`; after remove, not installed, `actions` `["install"]`, no copy and no record left under
  the temp roots. The macOS run calls `host_io.run` with exactly `platform::KILLALL_ARGV` (`["/usr/bin/killall", "-9",
  "AudioComponentRegistrar"]`, compared through the constant) (tasks TA-12) once after
  install and once after update and never after remove; the Windows run never calls `host_io.run`.
  `install::tests::job_hash_is_the_verified_digest` (loopback) (plan SI-31): after `verify_file` and before the batch the
  test rewrites the staged artifact's bytes; the launched job's `sha256` equals the verified digest
  (`hex_lower(artifact.sha256())`), the helper reports `failed` `E_HELPER_HASH` with zero spawns, and the action is
  `E_INSTALL_HELPER { code: "E_HELPER_HASH" }`.
- **SC-015 (launcher outcomes, FR-076).** `install::tests::launch_outcomes_map`: `Failed("E_ELEVATION_SHELLEXECUTE")`
  → `E_INSTALL_ELEVATION`; `TimedOut` → `E_INSTALL_NO_RESULT`; `Exited(0)` with no result file, with a result whose
  `job_sha256` differs, with one op fewer → `E_INSTALL_NO_RESULT`; `Exited(3)` with `job_error` → `E_INSTALL_HELPER`;
  `Exited(4)` → `E_INSTALL_NO_RESULT`; `Exited(0)` with an op entry `failed` `E_HELPER_EXIT` and `exit_code` 5 →
  `E_INSTALL_INSTALLER_FAILED { exit_code: 5 }` (the only user-visible Inno failure before M-1); `failed`
  `E_HELPER_TIMEOUT` → `E_INSTALL_HELPER { code: "E_HELPER_TIMEOUT" }`; `not_run` → `E_INSTALL_HELPER { code:
  "E_HELPER_AFTER_TIMEOUT" }`; `helper()` `None` → `E_INSTALL_HELPER_MISSING` with nothing written; a second
  concurrent batch (held on the first's `Elevating` event, Phase 9 SC-014's channel pattern) → `E_INSTALL_ELEVATION_BUSY`,
  zero writes. `install::tests::pre_job_refusals` (FR-072): an `Install` on Windows whose artifact format is `Pkg`, and
  on macOS one whose format is `InnoExe` → `E_INSTALL_FORMAT`; an artifact whose canonical path is outside the canonical
  `staging_dir()` (a `VerifiedArtifact` verified outside `staging_dir()`, Phase 9's `Fx::verified`; minting one is
  confined to `verify.rs` / `download.rs`) (plan SI-13) → `E_INSTALL_PATH`; each with no job written
  and no launch, and in a mixed batch the refused action's result in its own place. `install::tests::elevating_event_order`
  (FR-075) in two parts, because a driver receives already-verified actions and Phase 10 downloads one action per
  command (plan SI-24): (1) `for_command` for one `Install` over the fixture server, with a progress sink that also
  records the fake `Elevation`'s launch as an event: every `Download` event, then exactly one `Elevating`, then the
  launch; (2) `WindowsDriver::run` and `MacosDriver::run` direct with two pre-verified installs: exactly one
  `Elevating`, before the single launch; a batch with only an `Uninstall` emits `Elevating` once and no `Download`.
  `install::tests::osascript_outcome_parses`: exit 0; `execution error: User canceled. (-128)`; `… (3)`; garbage.
  `install::tests::windows_parameters_quote`: a path with spaces, one ending in `\`, one in verbatim `\\?\C:\…` form,
  one with `"` (refused). `windows_parameters_parse_back_windows` (`crates/core/tests/windows_quoting.rs`, lane
  `cargo.windows`) checks the real parser instead of a hand-written one: it spawns a test-only binary target of
  `crates/core`, `argv-echo` (`[[bin]]` with `test = false`, `path = "tests/bin/argv_echo.rs"`, which prints its
  `std::env::args_os()` as JSON; reached through `env!("CARGO_BIN_EXE_argv-echo")`, External facts), with
  `Command::raw_arg(windows_parameters(args))` and asserts the echoed argv equals `args` for `--job
  "C:\Users\John Smith\AppData\Local\…\job-<hex>.json"`, a verbatim `\\?\C:\Users\John Smith\…` path, a path ending
  in `\` and in `\\`, and a path containing a tab. It spawns only that echo binary, unelevated, touching nothing.
- **SC-016 (the locked mapping follows M-1, FR-060 to FR-062).** `classify::tests::provisional_rule` (until M-1):
  exit 0 → `Installed`, every exit 1-8 and 99 → `Failed`, never `Locked`. After M-1 is recorded:
  `classify::tests::recorded_locked_case` replays `tests/fixtures/helper/inno-locked/` (exit code and log) → `Locked`,
  and the same exit code with a log lacking the recorded line → `Failed`; it also asserts `classify::LOCKED_LINE`
  equals `locked_line.txt` (plan SI-26). The roadmap's "asserted by the driver's
  mapping test (the fake runner replays the recorded exit code and log)" (lines 806-807) is two more tests, added with
  M-1: `ops::tests::recorded_locked_case_through_runner` (helper): a `run_inno_installer` op whose fake `ProcessRunner`
  returns `exit_code.txt`'s code after the fake clock advances by `seconds.txt` and whose fake `HelperIo` serves
  `log.txt` as the op's `/LOG=` file, with the **production** `classify_inno` in `Seams` → outcome `locked`, code
  `E_HELPER_LOCKED`, `log_tail` the last 40 recorded lines; and its core twin
  `install::tests::recorded_locked_case_maps_to_locked` (the loopback `Elevation` running that same helper path) →
  `E_INSTALL_LOCKED` with the FR-133 text. PENDING-CI `inno-locked-file (windows-latest)`: every FR-060 value annotated.
  `install::tests::unrecognised_log_falls_back` (Q4): the recorded exit code with a log lacking the recorded line, through the
  helper and the core's loopback → `E_INSTALL_INSTALLER_FAILED { exit_code }`, never `E_INSTALL_LOCKED`. The row closes only
  when M-1 is written into this spec (FR-061) and the three replay tests are in `required-tests.json`; until then it reads
  "stopped at the M-1 fence" (FR-063, SC-032).
- **SC-017 (helper-elevated, Windows, roadmap lines 800-801).** PENDING-CI `helper-elevated (windows-latest)`: a
  `::notice` for each of: the token is elevated; the stub's recorded argv equals SC-002's Inno argv and its
  `current_exe` lies in the run directory; the fixture setup installed `Fixture.vst3` under the temp root and wrote
  `{fixture AppId}_is1` under HKLM (64-bit view); `run_inno_uninstaller` removed the tree and the key, outcome `done`;
  the stub's recorded SDDL of the run directory has a DACL that is protected (`P` among the `D:` flags; `AI` tolerated),
  holds exactly two ACEs, `(A;OICI;FA;;;SY)` and `(A;OICI;FA;;;BA)`, and no ACE carries the `ID` (inherited) flag (D7);
  every `Start-Process` reported the helper's own exit code and a result file existed when it returned (FR-100); the
  `$RUNNER_TEMP/link` job accepted the canonical source and refused the one spelled through the link; the run
  directory is gone after the job.
- **SC-018 (helper-elevated, macOS, roadmap lines 796-799).** PENDING-CI `helper-elevated (macos-latest)`: a
  `::notice` for each of: `run_pkg_installer` placed the four fixture trees under `$RUNNER_TEMP/real/root`; the raw `--only-dirs`
  listing (annotated) contains the structural ancestors and `uninstall_pkg` left them in place; every fixture file is
  gone; `pkgutil --pkgs` no longer lists `com.krateaudio.fixture`; no placed path carries `com.apple.quarantine`
  (`xattr -l`); the preinstall's record shows the run directory (the parent of `$1`, `$1` annotated) with owner uid 0,
  mode `0700`, a directory, under `/Library/Application Support/com.krateaudio.krate-manager-helper/` (D7); the
  `$RUNNER_TEMP/link` job (FR-100) accepted the canonical source and refused the one spelled through the link; the run
  directory is gone after the job.
- **SC-019 (release build has no test roots, roadmap lines 794-795).** PENDING-CI `check (windows-latest | macos-latest
  | ubuntu-24.04)` step `ci-temp-roots absent` (FR-103): `::notice` with `cargo tree` hits 0, marker in source ≥ 1, in
  the bundled helpers 0. Local: `cli::tests::ci_roots_flag_is_usage_error` (module `cli` (plan SI-1); `#[cfg(not(feature = "ci-temp-roots"))]`,
  default features) → exit 2. PENDING-CI `check (…)` steps `clippy ci-temp-roots` and `test ci-temp-roots` (FR-103a)
  green on every leg, the latter's log containing `cli::tests::ci_roots_file_is_read ... ok` exactly once (a
  `::notice`); step `clippy manual-elevation example` (FR-105) green on every leg.
- **SC-020 (bundling, FR-092, FR-104, FR-109).** `node --test` `apps/krate-manager/scripts/bundle.test.js` (test
  `bundle sidecar name per host tuple`): the sidecar file name for each host tuple (`x86_64-pc-windows-msvc` →
  `krate-manager-helper-x86_64-pc-windows-msvc.exe`, `aarch64-apple-darwin` → no extension), the `--config` argument
  only off Linux. `apps/krate-manager/scripts/inno-fixture.test.js` (test `fixture inno script keeps the plugin
  directives`, FR-101). `apps/krate-manager/scripts/write-test-job.test.js` (test `write test job prints verbatim
  windows paths`) (plan SI-31): the drive, UNC, already-verbatim and non-win32 rows of its pure `verbatim(p, platform)`,
  and a job written with `platform = 'win32'` forced holds `\\?\` `file` values in FR-001's key order (FR-027, FR-100).
  All three names are in `required-tests.json`'s `node` list and PENDING-CI on every `check` leg's
  `node --test` step, closed by `required tests ran`. PENDING-CI `check (windows-latest)` / `check (macos-latest)` step
  `helper bundled` (Windows: the `7z l` entry `krate-manager-helper.exe` beside `krate-manager.exe` and the extracted
  file's SHA-256 equal to the staged one, both annotated) and the existing `codesign verify` step green;
  `check (ubuntu-24.04)` asserts no helper in the `.deb`.
- **SC-021 (guards, FR-093, FR-112, FR-113).** `injection_scanner_reports_planted_violations` (existing name) reports each
  FR-112 plant (a `ShellExecuteExW` in `commands.rs`, an `OsRunner` built in `lib.rs` of the helper, a `Seams {` built
  in non-test code of `ops.rs`, an `Instant::now()` in `ops.rs`, a `killall` in `status.rs`, `/usr/sbin/installer` in
  core) and is clean on the tree; `READER_PINS` accepts the new `windows-sys`
  features and homes and reports a `windows-sys` line planted in `crates/core/Cargo.toml`; the roster test covers the
  helper sources.
- **SC-022 (job files, FR-073, FR-074).** `install::tests::job_files_written_and_removed`: after every batch outcome
  (done, refused, helper-missing, no-result) `<state>/jobs/` holds nothing; a `write_new` fault with `StorageFull` →
  every action `E_INSTALL_FULL { step: job_written }`, no launch; `install::tests::recover_removes_stale_jobs`: planted
  `job-<hex>.json`, `job-<hex>.result.json`, a symlink with a job name and an unrelated `notes.txt` → the two regular job
  files removed, the symlink and `notes.txt` untouched.
- **SC-023 (helper usage, FR-005, D15).** `crates/helper/tests/usage.rs` `helper_usage_exits_2` (replaces
  `helper_stub_exits_2_with_one_line`): the built binary with no argument, with `--anything`, with `--job` only, with a
  repeated `--job` → exit 2, empty stdout, one usage line on stderr, no file created in its temp working directory.
  `cli::tests::malformed_hash_is_usage_error` (FR-005) (plan SI-31): `main_with` in-process with a recording `RunFn`,
  `--job-sha256 xyz`, 63 hex digits and upper-case 64 hex → exit 2, the `RunFn` never called, nothing written (never by
  spawning the binary with arguments, FR-106).
- **SC-024 (native wiring per OS).** `install::tests::native_wiring_windows` (lane `cargo.windows`) and
  `install::tests::native_wiring_macos` (lane `cargo.macos`, PENDING-CI) replace Phase 9's `native_wiring_offers_nothing_*`:
  `status::build` over `fresh_temp_roots()` and a fixture feed (fake `HostFacts` with no record) offers `["install"]`;
  `for_command` with `native_env(…, &FakeElevation, &FakeHostIo)` reaches exactly one `launch` with a job whose op kind
  is the OS's installer op (the job bytes the fake read at launch, parsed back: one op, `run_inno_installer` /
  `run_pkg_installer`) (plan SI-31); the fake returns `Refused`, so nothing is installed.
- **SC-025 (UI, FR-130 to FR-135).** vitest in `src/status.test.ts`: `renders install on windows and macos` (replaces
  `renders no install buttons on windows or macos`), `windows confirmation mentions the administrator prompt`, `macos
  confirmation mentions the administrator password`, `shows waiting for administrator permission`, `refused permission
  shows nothing changed and keeps the actions`, `locked shows close your daw and retry`, `macos install shows the au
  rescan steps` (asserts, in order, the phrases "Restart the Mac", "Reset & Rescan Selection", "Full Audio Unit Reset",
  "~/Library/Caches/AudioUnitCache" and "support.apple.com/en-us/122179", FR-133), `outside-root install shows the
  windows settings hint and no remove`, `windows remove confirmation names the uninstaller`, `stale busy shows installer still running`, `not applied shows the change
  was not applied`, and a twelfth, `macos remove confirmation names the uninstaller` (FR-131 holds on macOS too, so a
  macOS card that kept Phase 9's manifest text is red) (plan SI-30). Each passes exactly once
  (`required tests ran`). `install::tests::new_error_texts` (FR-079) (plan SI-31): `to_string()` of every new
  `InstallError` variant equals its code, `": "` and FR-133's sentence (or FR-079's text), each sentence a literal in
  the test, so the rejection strings the vitest rows echo are the core's own `Display`.
- **SC-026 (zero warnings, parallel tests).** `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings` (the
  helper included; `#[expect]` only with a reason, as `lib.rs:28-31`), `npm run typecheck`, `npm run lint` clean locally;
  no `--test-threads`; macOS / Linux PENDING-CI on the same legs' steps; the two new jobs run no test serially. The
  feature-gated code is inside the gate too: locally `cargo clippy -p krate-manager-helper --all-targets --features
  ci-temp-roots -- -D warnings` and `cargo clippy -p krate-manager --example manual_elevation --features
  manual-elevation -- -D warnings` clean; in CI FR-103a's and FR-105's steps (SC-019).
- **SC-027 (the admin-only run directory checks, D7, FR-030).** `validate::tests::run_dir_ancestors` (pure function,
  every leg): `run_dir_ancestors_ok` over four `LstatFacts` (`/Library`, `/Library/Application Support`, the helper
  folder, the new directory) with uid 0, mode `0o755` / `0o700`, kind directory → `Ok`; each of: one ancestor owned by
  uid 501, one with mode `0o775` (group-writable), one with `0o757` (world-writable), one whose kind is symlink →
  `E_HELPER_RUN_DIR`. `ops::tests::run_dir_refusals_spawn_nothing`: through a fake `HelperIo` reporting each of those
  defects, and one whose "create run dir" reports an existing name, every installer op of the job is `failed`
  `E_HELPER_RUN_DIR` with zero spawns and zero copies. `validate::tests::run_dir_sddl` asserts the SDDL constant is
  exactly `D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)`; the applied DACL is proven on the runner (SC-017), the macOS owner and
  mode on the runner too (SC-018). The real seams over a `TempDir` (lanes `cargo.macos`, `cargo.linux`) (plan SI-25):
  `os::tests::run_dir_mode_unix` (`create_run_dir` gives mode `0o700`; a second call on the name fails) and
  `os::tests::wait_timeout_leaves_child_unix` (`OsRunner::spawn(["/bin/sleep", "30"])`, `wait(200 ms)` → still
  running, a second `wait` → still running: the child was not killed, FR-042; the test then kills and reaps it).
  `ops::tests::temp_io_refuses_outside_base` (`#[should_panic(expected = "outside the test sandbox")]`) (plan SI-31): the
  fake `HelperIo` over temp dir A asked to create a run directory under a second, unrelated temp dir B panics, and each
  other mutating method and `open_source` / `read_bounded` / `read_tail` outside its base panics too, so no fake can
  write outside the test's temp area.
- **SC-028 (the core elevates only the resolved helper, D19, FR-090).** `install::tests::launch_program_is_the_resolved_helper`:
  over install, update and remove batches on Windows and macOS, with a job, feed entry and settings that each name
  another executable path, every recorded `ElevatedCommand.program` equals the fake `Elevation::helper()` path and the
  args are exactly `["--job", <canonical job path>, "--job-sha256", <hex>]`; no other string reaches `launch`. D19's
  residual risk is accepted (user ruling 2026-11-01, Q1); this row enforces the part that is enforced. `install::tests::
  helper_file_never_opened`: over the same batches a recording `InstallIo` / `HostIo` sees no read, hash or copy of the helper
  path (no digest check, no admin-folder copy). `install::tests::helper_resolved_beside_exe` (FR-090's
  resolution rule `install::helper_beside`, every `Os` on every leg, over a `TempDir`) (plan SI-31): `<d>/krate-manager[.exe]` with
  the helper beside it → `<d>/krate-manager-helper` (`.exe` iff `Os::Windows`); helper absent → `None`; a directory of
  that name → `None`; `Os::Linux` with the file present → `None`; `exe = None` → `None`; a bare `krate-manager` (empty
  parent) with a helper in the test's temp dir → `None` (never the CWD or `PATH`).

- **SC-029 (timeout holds the guard, FR-078a).** `install::tests::timed_out_holds_guard`: a fake `Elevation` returns `TimedOut`
  for batch 1 (every action `E_INSTALL_NO_RESULT`, job file still present); batch 2 is `E_INSTALL_ELEVATION_BUSY { stale: true }`
  with zero mutating `InstallIo` calls and no `launch`; batch 3 after the fake writes `<stem>.result.json` into `<state>/jobs/`
  proceeds (one launch), and the stale job and result are gone; a fresh `CommandEnv` with a fresh fake `Elevation`, whose
  own `ElevationGuard` stands for the restarted manager's (plan SI-3), is unblocked without the
  result; a concurrent batch while the first runs is `{ stale: false }` and is never queued. Roots from
  `test_support::fresh_elevated_roots()` (plan SI-21).
- **SC-030 (confirmation by detection, FR-076a).** `install::tests::not_applied_detected`: a helper `done` whose fake host state
  shows (Windows and macOS, each) an install with an older class `Version`, an install with no class, an update that left the old
  `Version`, a remove whose copy remains and a remove whose installer record remains → each `E_INSTALL_NOT_APPLIED`; the same
  actions with the matching state → `Ok`; a `locked` action is not re-detected; no mutating call during the detection.
  The macOS install row that becomes `E_INSTALL_NOT_APPLIED` still records exactly one `host_io.run` of
  `platform::KILLALL_ARGV`, since FR-077 keys on the helper's `done` (plan SI-31).
- **SC-031 (elevated jobs are path-gated, FR-102a, FR-102b).** `manager-ci-shape.test.js` test `elevated jobs are path-gated`:
  `workflow_dispatch` is a trigger; the `changes` job has `contents: read`, no `environment:`, no secret; both elevated jobs
  have `needs: changes` and the `if`; `check` has neither; the changed-paths list equals FR-102b's; planted mutations reported.
  PENDING-CI: a push touching none of the paths shows both jobs skipped and one touching `crates/helper/` shows both run. After
  M-1, `inno-locked-file` goes red when the recorded exit code, log line or classes differ (FR-102a).
- **SC-032 (the M-1 fence, FR-063).** Before the push, the build's final report lists the open items (FR-061, FR-062's measured
  rule, the three replay tests, SC-016's post-M-1 half), `classify::tests::provisional_rule` is green, no `inno-locked/` fixture
  or M-1 values exist, and the roadmap's Phase 10 status line reads "stopped at the M-1 fence" (FR-123 (h)). After the resume
  the section M-1 and the fixtures exist, SC-016 is closed and the status line is rewritten.

---

## Safety

- **Injected roots.** The helper library takes `AllowedRoots` and `StagingRoot` as parameters (tests: temp roots);
  the binary builds `AllowedRoots` from compiled-in constants only (D3, FR-051) and `StagingRoot` from the job's own
  canonical location (FR-021); the test-only `ci-temp-roots` feature is checked absent from release builds (FR-103).
  The core's roots, feed URL, launcher (`Elevation`) and command runner (`HostIo`) are injected (FR-071); tests use
  `fresh_temp_roots()`, the local fixture server and fakes.
- **Verified before anything runs.** An installer op is built only from a `VerifiedArtifact` (size, SHA-256, minisign
  signature with the C3 binding, Phase 7), refused unless its path lies in staging (FR-072); the job's own hash travels
  on the elevated argv (D2); the helper copies the installer into an admin-only directory, hashing as it copies, and
  runs the copy (FR-030, FR-032, FR-035).
- **What the elevated helper may do.** Exactly the four ops of FR-030 to FR-033, each with fixed argv and absolute tool
  paths (`/usr/sbin/installer`, `/usr/sbin/pkgutil`, the validated `unins###.exe`), no shell; removals only of
  receipt-listed paths validated, all of them first, under the allowed macOS roots (FR-033, FR-034). It reads no
  environment or settings, writes no registry, adds no quarantine, starts no service, and has no op on Linux
  (FR-022, FR-024, FR-036).
- **Never close a DAW.** Every Inno run carries `/NOCLOSEAPPLICATIONS`, which the scripts' default `CloseApplications=yes`
  would otherwise override in silent mode (External facts); the only process signalled is `AudioComponentRegistrar`
  after a macOS install (FR-077); no process enumeration.
- **What tests never touch.** The dev box's real `C:\Program Files\Common Files\VST3`, `C:\ProgramData\Krate Audio`,
  HKLM uninstall keys, `/Library/Audio/Plug-Ins`, `/Library/Application Support`, `/Applications`, the real state dir;
  no UAC or osascript prompt, no Inno setup, no `installer`, no `pkgutil --forget`, no `killall`, no network beyond the
  fixture server (FR-110). The two elevated CI jobs run on ephemeral hosted runners only (FR-111).
- **What is not protected.** The elevated program itself lies in a user-writable folder on Windows (per-user NSIS
  install, `tauri.conf.json:23`) and possibly on macOS; a process running as the user can replace it before the
  prompt. D19 records this as residual risk, accepted by the user on 2026-11-01; the core elevates only the path resolved at
  startup (SC-028).

---

## Edge cases

| Case | Behaviour | Where |
|---|---|---|
| Interrupted download | Phase 7 resumes it; no job is written until the artifact verifies | Phase 7; FR-072 |
| Disk full writing the job | `E_INSTALL_FULL { step: job_written }`, no launch, artifact kept | FR-073, SC-022 |
| Disk full in the helper's run directory | The copy fails: op `failed`, `E_HELPER_COPY` (FR-030 step 2), the partial copy deleted; the next op still runs; the run dir is removed | FR-030, FR-004, SC-003 |
| `%LOCALAPPDATA%` redirected outside `C:\Users\<name>\AppData\Local` | The state dir fails FR-025: every elevated batch is `E_INSTALL_HELPER { code: "E_HELPER_JOB_LOCATION" }`; nothing runs. Ruled unsupported by the user on 2026-11-01 (Clarifications Q7); documented in Phase 12 | FR-025, FR-123 (g) |
| `/Library/Application Support/com.krateaudio.krate-manager-helper/` tampered (not root-owned, writable, a symlink) | Every installer op `E_HELPER_RUN_DIR`, nothing spawned | D7, SC-027 |
| A pkg receipt that lists `Library`, `Library/Audio`, … (`--install-location /`) | Those directory entries are structural: kept, never removed, the op proceeds; a file entry beside them is still a refusal | D16, FR-034, SC-006 |
| File locked by a running DAW (Windows) | The installer never closes it (`/NOCLOSEAPPLICATIONS`); the outcome follows M-1: `E_INSTALL_LOCKED` "close your DAW and retry" when the measured signature matches, else `E_INSTALL_INSTALLER_FAILED { exit_code }`; the batch continues | FR-030, FR-062, SC-003, SC-016 |
| Partial install (Inno aborted mid-copy) | Whether Inno rolls back is part of M-1 (DLL and unlocked-file hashes before / after); the manager trusts detection afterwards (class `Version`, the record) and offers what the gate allows | FR-060; Phase 6 |
| Installer hangs (an unsuppressible box) | `E_HELPER_TIMEOUT` after 600 s; later ops `not_run`; the process is left running and the elevation guard stays held (new batches are `E_INSTALL_ELEVATION_BUSY { stale: true }`); the core reports each code | FR-042, D14, FR-078a |
| Downgrade attempt (feed older than installed) | Never offered; a forged call is `E_INSTALL_NOT_NEWER` before any download (the Inno installer would accept it, so the gate is the guard) | FR-081 |
| Corrupt feed | Phase 5 keeps the last good copy; actions use it or are refused (`E_DOWNLOAD_NO_FEED`, `E_INSTALL_NO_FEED`) | Phase 5; Phase 9 FR-090, FR-094 |
| Offline | Install / Update need the artifact (a cached verified one is accepted); Remove works from the cached feed and the record (the helper needs no network) | FR-082 |
| Feed `schema_version` 1 entry (no `install`) | "not available through the manager"; no action; a forged Remove `E_INSTALL_NOT_MANAGEABLE` | Phase 9 FR-003 |
| Feed v2 entry without `windows_uninstall_key` / `macos_pkg_id` | Install and Update offered; Remove `NotManaged` (contract requires them when the platform is present, `CONTRACT.md:74-75`) | FR-080 |
| Bundles that still say `0.8.0` at top level | Detection takes the class `Version` (Phase 6); the update decision is Phase 6's state | Phase 6 FR-012 |
| Plugin installed earlier by the old `.exe` / `.pkg` | Same AppId / identifier, so the record exists; Update upgrades in place, Remove uses the same uninstaller / receipt | D9, Q4 |
| Inno record whose `InstallLocation` is outside the standard folder | Shown managed; no Remove; "uninstall it from Windows Settings"; Update still runs the installer (Inno's `UsePreviousAppDir=yes`, `setup.iss:53`, keeps that folder) | FR-080, FR-134 |
| User refuses UAC / the admin password | `E_INSTALL_REFUSED` for every action; nothing written outside the state dir; the job removed; artifacts kept; the card unchanged | FR-076, SC-009 |
| A DAW holds the osascript port (JUCE report) | osascript fails: `E_INSTALL_ELEVATION { code: "E_ELEVATION_OSASCRIPT" }` | FR-090 |
| Smart App Control (enforcement) blocks the unsigned helper | `ShellExecuteExW` fails: `E_INSTALL_ELEVATION`; documented unsupported (Key decision 7; Phase 12 docs) | FR-090 |
| Helper not bundled (dev run) | `E_INSTALL_HELPER_MISSING`, nothing written, no prompt | FR-076 |
| Two installs started at once | The second is `E_INSTALL_ELEVATION_BUSY { stale: false }`, never queued (installers never overlap); same plugin twice stays Phase 9's `E_INSTALL_BUSY` | FR-078 |
| An install after a helper timeout, the stale installer still running | `E_INSTALL_ELEVATION_BUSY { stale: true }` "installer still running" until the stale job's result file appears or the manager restarts | FR-078a, SC-029 |
| The installer exits 0 but detection shows the change was not made | `E_INSTALL_NOT_APPLIED` | FR-076a, SC-030 |
| A pkg receipt whose listing reaches outside the allowed roots | `E_HELPER_PKG_PATH`, nothing removed, receipt kept | FR-033, SC-001 |
| A user preset saved inside a factory preset folder on macOS | The folder is non-empty, so it is left; the user's file is not in the receipt, so it is never removed | FR-033, SC-006 |
| The manager crashes while the helper runs | The helper finishes and writes its result; the next start removes stale job files; status shows the true state | FR-074 |

---

## M-1: the locked-file measurement (recorded 2026-11-01)

Recorded by the main loop at the resume R1 (T020) from the check-run annotations of `inno-locked-file (windows-latest)`
in Manager CI run **41342540379** (commit `dba1dff9`, 2026-11-01T19:35:54Z). The first run (41341359391, `f6022bd5`)
measured the same exit code, classes and pending-rename result (4.4 s) but lost the log lines to GitHub's ten notices
per step; `dba1dff9` moved them to their own step.

| Value | Recorded |
|---|---|
| Inno Setup | 6.7.1 (the setup log's `Setup version` line; `ISCC.exe` `ProductVersion` reads `0.0.0.0`) |
| Exit code | **5** |
| Run time | 4.3 s; exited within 600 s: True |
| DLL digests | 1.0.0 `c8242b40c6432dca608ea3846634bdf1572108499c5d375c72208214b8cfd46f`, 1.0.1 `cdda15d4d08e7390a995ea8161c371985d712876a10b84493871eda4e6a5b784` |
| Unlocked file digests | 1.0.0 `92521fc3cbd964bdc9f584a991b89fddaa5754ed1cc96d6d42445338669c1305`, 1.0.1 `d51e6ec94058554a84558a5b402ee6ef5fdf5455e35c902feda61cda8752943a` |
| DLL afterwards | `c8242b40…`, class **old** |
| Unlocked file afterwards | `92521fc3…`, class **old** (Inno stopped before reaching it) |
| `PendingFileRenameOperations` | gained 0 values; names the DLL: False |
| `DisplayVersion` afterwards | 1.0.0 |
| Holder | pid 872, `NativeLibrary.Load` of the installed DLL |

The ten matching log lines, verbatim (`tests/fixtures/helper/inno-locked/log.txt`):

```
2026-11-01 19:37:20.634   Setup version: Inno Setup version 6.7.1
2026-11-01 19:37:20.634   Setup command line: /SL5="$3021A,846346,842752,D:\a\_temp\setups\Fixture-1.0.1-Windows-x64.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LOG=D:\a\_temp\inno-logs\setup-1.0.1.log
2026-11-01 19:37:20.721   Dest filename: D:\a\_temp\real\root\VST3\Krate Audio\Fixture\Fixture.vst3\Contents\x86_64-win\Fixture.vst3
2026-11-01 19:37:20.723   DeleteFile: The existing file appears to be in use (5). Retrying.
2026-11-01 19:37:21.725   DeleteFile: The existing file appears to be in use (5). Retrying.
2026-11-01 19:37:22.727   DeleteFile: The existing file appears to be in use (5). Retrying.
2026-11-01 19:37:23.727   DeleteFile: The existing file appears to be in use (5). Retrying.
2026-11-01 19:37:24.729   Defaulting to Abort for suppressed message box (Abort/Retry/Ignore):
                          D:\a\_temp\real\root\VST3\Krate Audio\Fixture\Fixture.vst3\Contents\x86_64-win\Fixture.vst3
                          DeleteFile failed; code 5.
```

**Reading.** The update was skipped as a whole, not scheduled and not partial: Inno retried the in-use DLL four times,
answered its suppressed Abort/Retry/Ignore box with Abort and exited 5, leaving every file and the registered version
at 1.0.0. None of FR-062's re-ruling cases (exit 0 with the DLL `old`, a scheduled rename, a hang) occurred. The
measured rule (FR-062): `classify::LOCKED_EXIT = 5`, `classify::LOCKED_LINE = "DeleteFile: The existing file appears to
be in use (5)."` (`locked_line.txt`); `classes.json` is `{"dll":"old","unlocked":"old"}`; `exit_code.txt` 5;
`seconds.txt` 4.3. `log.txt` holds the matching lines only (the job annotates the lines matching FR-060's filter plus
`Setup version`, not the whole log).

---

## New components

| Module / file | Purpose |
|---|---|
| `apps/krate-manager/crates/helper/src/lib.rs` (🔶) | `run(args, os, roots, seams) -> Exit` (plan SI-17), `Exit`, `HelperOs`; module wiring (D18) |
| `crates/helper/src/cli.rs` | `Args`, `parse_args`, `RunFn`, `main_with` and the `--ci-roots` flag (FR-005, FR-050, FR-052; a library module, since `main.rs` is the binary root) (plan SI-1) |
| `crates/helper/src/digest.rs` | SHA-256 over `sha2`, the helper's only `sha2` token home (FR-112) (plan SI-6) |
| `crates/helper/src/job.rs`, `result.rs` | FR-001 to FR-003 schemas, duplicate-key parse, result writer |
| `crates/helper/src/validate.rs` | FR-020, FR-021, FR-025 to FR-027, FR-031 path rules, FR-034 classification, `run_dir_ancestors_ok` (D7); its items public (tasks TA-2) |
| `crates/helper/src/ops.rs` | The four ops, `copy_hashed`, batch execution, timeouts, polling; its items public (tasks TA-2) |
| `crates/helper/src/classify.rs` | `classify_inno` (D10, FR-062) |
| `crates/helper/src/roots.rs` | `AllowedRoots`, `StateDirRule`, `production()`, the `ci-temp-roots` reader; its items public (tasks TA-2) |
| `crates/helper/src/seams.rs` | `Seams`, the `HelperIo`, `ProcessRunner`, `Registry` traits and the test fakes with their shared fake clock (`pub mod fake` behind the helper feature `test-support`) (plan SI-19) |
| `crates/helper/src/os.rs` | `OsHelperIo`, `OsRunner`, `OsRegistry`, `KnownFolder` / `known_folder` (FR-051) and the no-follow open (Windows over `windows-sys`; macOS over `std` and `std::os::unix::fs::{MetadataExt, PermissionsExt, DirBuilderExt}`, no new crate); its items public (tasks TA-2): a helper item the plan marks `pub(crate)` is `pub`, because a `pub(crate)` placeholder no code reads yet warns `dead_code` at a zero-warning gate, and the helper library is a leaf consumed only by its binary and the tests |
| `crates/helper/src/main.rs` (🔶) | Production seams and the call of `cli::main_with` only (plan SI-1), GUI subsystem on Windows |
| `crates/helper/tests/usage.rs`, `tests/core_jobs.rs` | SC-023, SC-011 (replace `tests/stub.rs`) |
| `crates/helper/Cargo.toml` (🔶) | `serde`, `serde_json`, `sha2`, Windows `windows-sys`; feature `ci-temp-roots` |
| `crates/core/src/install.rs` (🔶) | `WindowsDriver`, `MacosDriver`, `Elevation`, `ElevatedCommand`, `LaunchOutcome`, `RemoveBasis`, `install::job`, `windows_parameters`, `OSASCRIPT_LINES`, `osascript_outcome`, the elevation guard, new codes, `Notice::RescanDawMacos`, `InstallProgress::Elevating` |
| `crates/core/src/install_tests.rs`, `test_support.rs` (🔶) | SC-003, SC-009 to SC-016, SC-022, SC-024, SC-028; `FakeElevation` (no helper) and `fresh_elevated_roots()` in `test_support.rs` (plan SI-19) (plan SI-21) |
| `crates/core/src/install_loopback_tests.rs` | The loopback elevation over the helper library, `#[cfg(test)]` code of `install`, never in `test_support.rs` (which the helper's dev build compiles) (plan SI-19) |
| `crates/core/tests/windows_quoting.rs`, `crates/core/tests/bin/argv_echo.rs`, `crates/core/Cargo.toml` (🔶, the `argv-echo` test-only `[[bin]]`) | SC-015's real-parser round trip (lane `cargo.windows`); `config_guard.rs` / `injection.rs` updated where they pin core's targets |
| `crates/core/src/status.rs`, `src/view.ts` (🔶) | Gates on every OS; `remove_hint` |
| `src-tauri/src/os_elevation.rs` | `NativeElevation` (ShellExecuteExW / osascript execution only) |
| `src-tauri/src/lib.rs`, `commands.rs`, `Cargo.toml` (🔶) | Build `NativeElevation`; pass it and `OsHostIo`; Remove gets `PlatformFacts`; `windows-sys` features; feature `manual-elevation` |
| `src-tauri/examples/manual_elevation.rs` | FR-105's opt-in manual check |
| `src-tauri/tauri.helper.conf.json`, `apps/krate-manager/scripts/bundle.js`, `scripts/bundle.test.js`, `scripts/write-test-job.js`, `scripts/inno-fixture.test.js` | D12 bundling; CI job writer; FR-101's directive check (run by FR-109) |
| `src/status.ts`, `src/status.test.ts` (🔶) | FR-130 to FR-135 |
| `tests/fixtures/inno/`, `tests/fixtures/locked-dll/`, `tests/fixtures/argv-recorder/`, `tests/fixtures/helper/` | CI fixtures (FR-100 to FR-102), helper fixtures, the M-1 replay data |
| `tests/fixtures/status/*.json` (🔶, 13 files) | `"remove_hint": null` (FR-083) |
| `.github/workflows/manager-ci.yml` (🔶) | `changes`, `helper-elevated`, `inno-locked-file` (the last two gated by `changes`, FR-102b); on `check`: the bundle, `helper bundled`, `ci-temp-roots absent`, `clippy ci-temp-roots`, `test ci-temp-roots`, `clippy manual-elevation example` and `node --test` steps; `required tests ran` reads the `node` list |
| `crates/core/tests/injection.rs`, `tests/roster.rs` (🔶); `tools/feed-contract/tests/manager-ci-shape.test.js` (🔶); `tests/required-tests.json` (🔶) | FR-093, FR-107, FR-108, FR-112, FR-113 |

---

## Traceability

| Roadmap statement (line) | Requirements | Success criteria |
|---|---|---|
| Helper reads one JSON job, fixed op set, sequential, result per op, no shell / arbitrary commands / job arguments, unknown ops and fields rejected (714-717) | D1, D2, D15, FR-001 to FR-005, FR-036, FR-040 | SC-001, SC-011, SC-023 |
| `run_inno_installer` with the fixed flags; exit code and log mapped; never closes a DAW (718-721) | D10, FR-030, FR-060 to FR-062 | SC-002, SC-003, SC-016 |
| `run_inno_uninstaller`: GUID `app_id`, helper reads `UninstallString` in the 64-bit view, `unins*.exe` in the plugin dir under the root, fixed args, polls with timeout; outside-root copy → Windows Settings (722-730) | D4, D5, D6, D9, FR-031, FR-080, FR-134 | SC-001, SC-002, SC-004, SC-012, SC-017 |
| `run_pkg_installer` (731) | FR-032 | SC-002, SC-018 |
| `uninstall_pkg`: pattern, receipt location and files, every path validated first, symlinks as links, dirs only when empty, never a root, then `--forget` (732-737) | D5, D16 (the structural-directory departure), FR-033, FR-034, FR-123 (e) | SC-001, SC-006, SC-018 |
| No Linux op, no generic op (738) | D18, FR-036 | SC-001 |
| TOCTOU: copy into an admin-only dir, hash as it copies, run the copy (739-741) | D2, D7, FR-030, FR-035, FR-043 | SC-002, SC-005, SC-017, SC-018, SC-027 |
| Allowed roots: library parameter, binary constants, known-folder API, `StagingRoot` from the job's location in the invoking user's state dir, nothing from job or settings (742-751) | D3, D17, FR-020 to FR-027, FR-050 to FR-052 | SC-001, SC-007, SC-008 |
| (beyond the roadmap) the integrity of the elevated binary itself | D19, FR-090 | SC-028 |
| Launchers behind one trait; one prompt per batch; no Linux launcher (752-754) | D8, D11, FR-071, FR-076, FR-078, FR-090, FR-091 | SC-009, SC-010, SC-015 |
| Windows / macOS drivers: batch job from verified downloads, sequential, named outcomes; no migration; registrar kill; DAW notice with AU steps (755-761) | D9, D13, FR-070 to FR-084, FR-130 to FR-135 | SC-003, SC-012 to SC-015, SC-024, SC-025 |
| Measure the locked-file behaviour (exit code, log, whether files are replaced, hang, restart); mapping follows the measurement, asserted by the driver's mapping test replaying the recorded exit code and log (762-766, 803-807) | D10, FR-053, FR-060 to FR-062, FR-102, M-1 | SC-016 |
| Unelevated validation tests; exact argv; batch of three; polling; root construction; canonicalization (769-787) | D18, FR-041, FR-053, FR-110 | SC-001 to SC-008, SC-027 |
| Launcher only through a fake; opt-in manual test; nothing elevated on the dev box (788-789) | FR-105, FR-110 | SC-009, SC-019, SC-021 |
| `helper-elevated` on hosted runners, `ci-temp-roots` absent from release builds, macOS fixture pkg, Windows fixture Inno (791-802) | FR-100, FR-101, FR-103, FR-103a, FR-106, FR-109, FR-111 | SC-017 to SC-020 |
| Sidecar via `bundle.externalBin`, launched by the core, never the webview (109-110); the universal macOS helper handed to Phase 11 | D12, FR-092, FR-104, FR-123 (d) | SC-020 |
| Narrowest capability set; no remote content (104-106) | FR-092 | SC-021 |
| Does NOT write the registry itself, install a service, run outside the op set, or touch Linux (809-810) | FR-036, FR-122 | SC-001, SC-021 |
| Never touch the real system; hosted-runner exceptions (989-996) | FR-110, FR-111 | SC-021 |
| Never close a DAW; one prompt per batch; locked fails on its own; DAW notice (1003-1007) | FR-030, FR-077, FR-078, FR-084 | SC-003, SC-014 |
| ROSTER: every per-plugin fact from the feed (1020-1022) | FR-080, FR-113 | SC-021 |
| Zero warnings; parallel tests (1015-1019) | FR-093, FR-103a, FR-105 | SC-026 |
| (user rulings 2026-11-01) stale hold after a timeout; confirmation by detection; M-1 fence; path-gated elevated jobs | FR-063, FR-076a, FR-078a, FR-102a, FR-102b, FR-123 (g), (h) | SC-029 to SC-032 |

---

## Open questions

None. Every decision the specify stage flagged (D2, D8, D11, D16, D17, D19) and every clarification question is ruled in
Clarifications (2026-11-01). Pending, not open: M-1 (the measurement the roadmap assigns to this phase's CI job; the build
stops at the fence until it is recorded, FR-063).

---

## Review notes

Revision 2 (2026-11-01) answers the review of revision 1. Every blocker and major was applied; none was rejected.
Choices worth naming:

- Helper-integrity issue (D19): of the three suggested resolutions, (c) "record the residual risk with the user's
  ruling" was taken, because (a) and (b) each break an existing ruling (one prompt per batch; Q5 `currentUser`) and (a)
  keeps a check-to-elevation window. That needs the user, not a spec author. The enforceable part (only the
  startup-resolved path is ever elevated) is SC-028.
- `StagingRoot` / invoking-user clause: implemented (FR-025, D17) rather than recorded as a relaxation. The Windows
  form binds to the profiles folder, not the invoking SID, for the over-the-shoulder reason in D17. It is listed in
  Open questions because it refuses a redirected `%LOCALAPPDATA%`.
- `/var` elevated case: resolved by option (a), a test-made link inside `$RUNNER_TEMP` (FR-100). The native `/var` →
  `/private/var` case stays in the unelevated SC-008, where roadmap line 786 places it. FR-052 now checks the
  *canonical* forms against the `--ci-roots` file's parent, so linked spellings of in-bounds roots are allowed.
- Universal helper: handed to Phase 11 by FR-123 (d), not built by Phase 10's `bundle.js`. Phase 10 does not produce
  the universal manager build, so it cannot test a universal sidecar against it.
- `SW_HIDE`: FR-090 passes the literal `0` with a comment, so no `Win32_UI_WindowsAndMessaging` feature is added.
- GUI-subsystem helper in CI: `Start-Process -Wait -PassThru` under `shell: pwsh` for every Windows helper step, with
  an FR-107 shape assertion. The macOS steps keep the job's bash default.
- FR-104 / FR-103 bundle evidence: the Windows check now hashes the helper extracted from the NSIS setup. macOS keeps
  the existence and `codesign --verify` checks without a hash, because the bundler's ad-hoc signing rewrites the file.

---

## Clarifications

### Session 2026-11-01

- Q: The elevated helper sits in the user-writable per-user install folder (D19); which ruling applies? → A: Accept the user-writable helper location as residual risk (an unsigned UAC prompt has no publisher identity to forge, so a swap adds no capability); FR-123 (f) records the ruling in the roadmap; no digest check, no admin-folder copy; Q5's `currentUser` install stands. [FR-090, FR-123, SC-028]
- Q: How does the build handle the dependency of `classify_inno`'s "locked" rule on the `inno-locked-file` run? → A: The build stops at the M-1 fence once everything else is green, the user pushes, the build resumes and writes M-1, the fixtures and the measured rule inside Phase 10 (roadmap line 765); no provisional-rule close, no move to Phase 12. [FR-063, FR-061, FR-062, FR-123, SC-016, SC-032]
- Q: What after `LaunchOutcome::TimedOut` (guard release, overlap with a still-running installer)? → A: The guard stays held and new batches are refused `E_INSTALL_ELEVATION_BUSY` until the stale job's result file appears in `<state>/jobs/` (polled) or the manager restarts; the card shows a named "installer still running" state; no queue (Phase 12). [FR-078a, FR-079, FR-133, SC-029, SC-025]
- Q: Which "locked" rule across Inno versions? → A: The recorded exit code plus the one log-line substring measured on the current Inno; installers whose Inno logs differently fall back to `E_INSTALL_INSTALLER_FAILED { exit_code }`. [FR-062, SC-016]
- Q: Should the driver confirm each outcome by detection? → A: Yes: after the batch, `detect_plugin` is re-run for each done action; an install / update whose class `Version` is not the entry version, or a remove whose copy or record is still detected, becomes `E_INSTALL_NOT_APPLIED` (detection reads `moduleinfo.json`, not the DLL). [FR-076a, FR-079, FR-133, SC-030]
- Q: Structural ancestors in a pkg receipt (D16)? → A: Accept D16: in `uninstall_pkg` a directory entry that is an allowed root, an ancestor of one, or `/Applications` is structural (kept, never removed); every file entry and every other directory still follows the whole-op refusal rule; FR-123 (e) amends the roadmap. [FR-034, FR-123, SC-006, SC-018]
- Q: The Windows state-dir rule with a redirected `%LOCALAPPDATA%` (D17)? → A: As written, profile shape only (`<FOLDERID_UserProfiles>\<name>\AppData\Local\com.krateaudio.krate-manager`); a redirected `%LOCALAPPDATA%` is unsupported and documented in Phase 12. [FR-025, FR-123, SC-001]
- Q: CI cost and lifecycle of `helper-elevated` and `inno-locked-file`? → A: Both run only when `crates/helper/**`, the helper fixtures, `scripts/write-test-job.js` or `manager-ci.yml` changed (a changed-paths step), or on `workflow_dispatch`; after M-1, `inno-locked-file` asserts the recorded exit code, log line and file classes. [FR-102a, FR-102b, FR-107, FR-111, SC-031]
- D2: The job file's SHA-256 is passed on the elevated helper's command line (`--job-sha256`), so a job swapped after consent is refused. [FR-005, FR-076, SC-028]
- D8: The elevation launcher is injected through `CommandEnv` next to the Phase 9 driver resolver, not `AppContext`; roadmap line 478 is amended. [FR-071, FR-123, SC-024]
- D11: A second elevated batch while one runs is refused `E_INSTALL_ELEVATION_BUSY`, not queued. [FR-078, FR-078a, SC-015, SC-029]
- D7: Run directories are `%SystemRoot%\Temp\run-<random>` (explicit protected SYSTEM + Administrators DACL) on Windows and `/Library/Application Support/com.krateaudio.krate-manager-helper/run-<random>` (mode 0700, root-owned parent chain checked) on macOS. [FR-030, SC-017, SC-018, SC-027]

### Session 2026-11-01 (plan preconditions)

- Q: Who writes the plan's spec issues SI-1 to SI-31 into this spec? → A: The plan rows are binding; task T002 writes the spec text in G2 alongside the other tasks (Phases 8/9 precedent), not the main loop before the build. [tasks T002]
- Q: macOS: the launch timeout includes the password dialog (PD-18 / R-7); a dialog left open past the batch ceiling and then cancelled leaves the stale hold until restart. → A: Accepted as planned: no macOS prompt allowance, no osascript-exit release; the card's "an installer is still running" state and a manager restart clear it. [FR-078a, SC-029]
- Q: Task-list deviations TA-2 (helper `pub(crate)` items are `pub`), TA-6 (`os::known_folder` takes a `KnownFolder` enum; `roots.rs` names no `windows_sys` item) and TA-12 (FR088-safe test text; `killall` literal only in `platform/mod.rs`)? → A: All three accepted; T002 writes the spec rows they need (New components "public", FR-051 `KnownFolder`, FR-112 homes). [FR-051, FR-112, tasks TA-2, TA-6, TA-12]
- SI-22 (main loop): the phase workflow's WSL gate step now also covers `-p krate-manager-helper` and its feature clippy once `crates/helper` exists; the roadmap mark step replaces an existing status line (TA-15) instead of adding a second. [tasks TA-11, TA-15]
