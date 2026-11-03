# Krate Manager — Plugin Install / Update Manager Roadmap

*(Status: **DRAFT 2026-10-29**, revised after the challenge review (see Review notes) and the roadmap grill (see Decisions).
Research: `specs/krate-manager/reports/Krate Audio plugin
manager design.md` (the "report"), nine notes in `specs/krate-manager/research_notes/Krate Audio plugin manager design/`
(cited by file name), and the measured answers in `specs/krate-manager/probes/results.md` (the "probes"), which override
any research claim they contradict.)*

A phased plan for a small cross-platform desktop app that shows which Krate Audio plugins are installed, whether
updates exist, and downloads, installs, updates and uninstalls them, and updates itself. Part A changes the release
pipeline and the feed (Node, YAML, one C++ guard test). Part B builds the app (Rust core + TypeScript UI on Tauri 2).
Each phase is sized to become one spec (`specs/krate-manager-phase<N>/`) with its own tests and evaluation criteria, and
each is buildable and testable on Windows; macOS and Linux evidence comes from the manager's CI workflow on hosted
runners, which only the user can trigger.

## Decisions

The user's answers to the roadmap grill, 2026-10-29. The roadmap body below already carries every decision (the Q-ids
used in the body point at these lines); this log only records what was asked, what was decided and where it landed.
The STRATEGY ruling removed the former Phase 11 (Legacy Installs), so the former Phases 12 and 13 are now Phases 11
and 12 and the phase count is twelve.

- 2026-10-29 STRATEGY, install strategy: REVERSED by the user. The manager runs the existing installers on Windows and macOS instead of placing files from new zip archives (Strategy A replaces Strategy B everywhere). Windows: the elevated helper runs each plugin's Inno `.exe` with `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LOG`, sequentially, behind one UAC prompt per batch; uninstall runs the Inno uninstaller silently and polls until the bundle folder is gone. macOS: the helper runs each `.pkg` through `installer -pkg FILE -target /`, sequentially, behind one admin prompt per batch; uninstall removes the files `pkgutil --files` lists, then `pkgutil --forget`. Linux: the manager extracts the existing `tar.gz` itself and tracks it in a per-plugin manifest. No new per-OS archives; the manager downloads the release's `.exe` / `.pkg` / `.tar.gz` verbatim; Phase 2 instead adds `SHA256SUMS`, one minisign signature per existing asset (trusted comment binds plugin, version, platform, asset name) and `release.json`; the Windows rename-aside path and zip extraction for Windows/macOS are dropped (path checks stay for the Linux tar.gz); every installer is size-, SHA-256- and signature-verified before the helper runs it, and the helper re-hashes it as it copies it into an admin-only directory. The helper's fixed op set: run a verified Inno installer, run a verified Inno uninstaller, `installer -pkg` on a verified pkg, remove `pkgutil`-listed files plus forget (nothing elevated on Linux). The exact Inno behaviour with a locked file under `/NOCLOSEAPPLICATIONS` is unmeasured and is measured in Phase 10 on a hosted Windows runner. Consequences recorded: the macOS `renamex_np` swap, the generic copy/rename/remove helper ops, the `pkexec` launcher, the AppImage-FUSE helper risk and the consented removal of shadowing copies (Phase 6 still reports duplicates) are dropped. [Positioning; Architecture Overview (diagram, per-OS table, feed and artifact model, state); Key design decisions 2-3; Reuse Inventory; Repository Layout; Phases 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12; Dependency Graph; Not in v1; Cross-Cutting Constraints; Review notes; former Phase 11 removed and later phases renumbered]
- 2026-10-29 Q1, app location and versioning: `apps/krate-manager/` with the `core` / `helper` / `src-tauri` workspace; `apps/krate-manager/version.json` is the single version source and a lint (`tools/lint-manager-version.js`) checks `tauri.conf.json` and `Cargo.toml` equal to it; tag `manager/vX.Y.Z`; `CHANGELOG.md`; first public version 0.1.0; released by manual dispatch of `release-manager.yml`. [Repository Layout; Reuse Inventory; Phase 4; Phase 11; Phase 12]
- 2026-10-29 Q2, macOS plug-in install route: superseded by STRATEGY; macOS runs the existing `.pkg` through `installer -pkg` via the helper, keeping the `pkgutil` receipt. The pkg has no choices, so the AUv3 app is installed whenever the release's pkg contains it and the manager does not handle it separately. [Architecture Overview (per-OS table); Phase 10]
- 2026-10-29 Q3, Linux root and factory presets: Linux default plug-in root is `~/.vst3` with no prompt; the manager extracts the tar.gz and puts the factory presets in `~/Documents/Krate Audio/<P>` (plus `preset_subdir` from `version.json` when set), tracked by the manifest so uninstall is exact, appearing as user presets; Windows and macOS keep the installers' system locations and have no per-user opt-in. [Architecture Overview (per-OS table, repo fact); Reuse Inventory; Phase 4 (settings model); Phase 9; Not in v1]
- 2026-10-29 Q4, plugins installed by the old `.exe` / `.pkg`: resolved by STRATEGY, no migration; the newer installer upgrades them in place (same Inno AppId, same pkg identifier), the manager treats them as installed and manages them like any other. The migration ops, the pre-install uninstall/forget and the separate legacy-install phase are dropped; uninstall uses the same Inno uninstaller and `pkgutil` route as everything else. [Key design decisions 2; Phase 6; Phase 10; former Phase 11 removed; Dependency Graph]
- 2026-10-29 Q5, first install of the manager: Windows Tauri NSIS `currentUser` (self-update never prompts); macOS an unsigned pkg installing to `/Applications` with a `curl` Terminal one-liner documented beside it (provisional until the Open Anyway GUI check); Linux AppImage plus `.deb` built in an `ubuntu:22.04` container; the landing page gains a Krate Manager card. [Reuse Inventory (landing page); Phase 11; Phase 12]
- 2026-10-29 Q6, supported OS floor: declare only what is measured: macOS 15 and later (`minimumSystemVersion` 15.0), Windows 10 22H2 and Windows 11 x64, Linux at the glibc of the `ubuntu:22.04` build; the macOS manager is a universal build so Intel Macs on 15 are covered. [Phase 4; Phase 11]
- 2026-10-29 Q7, Update all and a running DAW: one confirmation listing every update, then one elevation prompt per batch; bounded parallel downloads, sequential installers; the manager never closes a DAW (Windows installers get `/NOCLOSEAPPLICATIONS`, a locked plug-in fails cleanly with a "close your DAW and retry" message while the batch continues); no DAW process detection; every install ends with a "restart or rescan your DAW" notice (macOS adds Apple's AU rescan steps). [Phase 10; Phase 12; Not in v1; Cross-Cutting Constraints]
- 2026-10-29 Q8, uninstall and rollback: one Remove button per plugin running the platform uninstall (Windows Inno uninstaller, macOS `pkgutil` removal plus forget, Linux manifest); it removes the plug-in and its factory presets, never user presets; no "Reinstall previous version" in v1. [Phase 9; Phase 10; Not in v1]
- 2026-10-29 Q9, prereleases and the v1 `version` key: the v1 `version` key the nine in-editor banners read is the newest non-prerelease; prereleases appear only in a v2 `prerelease` sibling with `channel: "beta"`, which the manager ignores in v1. [Phase 1; Phase 5; Not in v1]
- 2026-10-29 ROSTER, where the plugin list comes from: data-driven. The manager lists exactly the plugins under the feed's `plugins` key and ships no plugin names, bundle names, AppIds or pkg identifiers of its own; detection, install and uninstall take every per-plugin fact from that plugin's feed entry (`install` block, `platforms`). A new plugin appears in the manager when its release reaches the feed, with no manager release. A v1-only entry (no `install` block) is listed with its available version and shown as "not available through the manager". [Phase 5; Phase 6; Cross-Cutting Constraints]
- 2026-10-29 Q10, plugins whose newest release predates Part A: backfill-sign. A one-time CI job (`backfill-sign.yml`) downloads each plugin's current `.exe`, `.pkg` and `.tar.gz`, writes `SHA256SUMS`, one minisign signature per asset and `release.json`, and attaches them to the existing releases, so all nine plugins are manageable at the first manager release with no plugin re-release; detection still falls back to the class `Version` where the top-level `moduleinfo` `Version` says 0.8.0. [Reuse Inventory; Repository Layout; Phase 2; Phase 3; Phase 6; Phase 12; Dependency Graph]
- 2026-11-03 first public version: 1.0.0, not Q1's 0.1.0 (the user: the manager is feature complete). Phase 12 spec revision R-5. [Phase 12]

## Positioning

| Is | Is not |
|---|---|
| One window listing every Krate plugin in the feed (nine today): installed version, available version, one action (Install / Update / Remove), release notes (report §"Copy Audio Damage's single screen", line 69) | A store, an account system, a licence or trial manager: the plugins are free and open source |
| The installer of record for users who choose it: it downloads each plugin's existing installer verbatim, verifies it, and runs it (the Windows Inno `.exe` and the macOS `.pkg`, both through the elevated helper); on Linux, where there is no installer, it extracts the existing `.tar.gz` itself and tracks what it placed | A second install format: no new per-OS zip archives, no files placed by the manager on Windows or macOS, no per-user install on those two (the installers decide the paths) |
| Self-updating with Tauri's minisign updater, no platform certificate (report line 9; probes line 10) | Signed, notarized or listed in a store. **No code-signing certificate will ever be bought.** A design that needs one is a defect |
| A resident-free app: it runs when the user opens it | A background service or daemon (prior_art.md KQ1: Native Access's NTK Daemon is the support-ticket generator) |

**v1 scope:** detect installed Krate plugins and their versions, install, update, uninstall, self-update. Nothing else.

**What stays exactly as it is:**
- The per-plugin installers: Inno Setup `.exe` (`plugins/<p>/installers/windows/setup.iss`), unsigned `.pkg` (`release.yml:331-337`),
  Linux `.tar.gz` (`release.yml:403`). They keep shipping with every release unchanged, and they are the assets the
  manager downloads verbatim and runs (Windows, macOS) or extracts (Linux). Browser downloads of a manual installer stay
  the `.pkg` on macOS.
- The in-editor update checker in all nine plugins (`plugins/shared/src/update/update_checker.cpp:222-262`), reading
  `https://rolandzwaga.github.io/krate-audio/versions.json` (e.g. `plugins/iterum/src/update/iterum_update_config.h:12`),
  which 301-redirects to `https://krateaudio.com/versions.json` (Pages custom domain, `docs.yml:240` `cname: krateaudio.com`).
  The manager uses the `krateaudio.com` address directly.
  Every feed change in this roadmap is additive so those parsers keep working.

Nothing is retired by this roadmap. Retiring anything later needs its own phase and the user's ruling.

## Core Philosophy

- **The manager is a better install path than a browser, because only browsers mark files.** Non-browser downloads
  carry no `com.apple.quarantine` on Sequoia 15.7.9 and Tahoe 26.6.2 (probes line 13) and no Mark-of-the-Web on Windows
  (probes line 9). Everything the manager writes, it writes unmarked; it never adds quarantine.
- **One verify path on three OSes, the OS's own install step after it.** Download, size, SHA-256 and signature checks
  are identical everywhere. The install step then runs the plugin's existing installer: the Inno `.exe` on Windows and
  the `.pkg` on macOS, both through the helper; on Linux, which has no installer, the manager extracts the `.tar.gz`
  itself. Only the roots and the elevation idiom differ, and both sit behind one platform abstraction.
- **Verify before anything touches the asset.** Size, SHA-256 and a minisign signature bound to plugin, version and
  platform are checked before a single byte is extracted or executed, and before the helper runs an installer as admin.
- **Privilege is a narrow, one-shot door.** A small helper binary does only the elevated work, from a fixed and
  validated operation set (run a verified Inno installer, run a verified Inno uninstaller, run `installer -pkg` on a
  verified pkg, remove `pkgutil`-listed files plus forget), once per batch. Downloading, hashing and signature checks
  run as the user. Linux needs no elevation.
- **Never touch the real system in a test.** Every root, the feed URL and the elevation launcher are injected.
- **Additive, never breaking.** The feed grows beside the three keys the shipped plugins read; the plugin-side
  installers and update banner keep working throughout.

## Architecture Overview

```
 ┌──────────────────────────── Krate Manager (per-user install) ───────────────────────────┐
 │  Webview UI (TypeScript, Vite)          no fs / shell / http capability, no remote content │
 │        │  #[tauri::command] calls + Channel<Progress>                                      │
 │        ▼                                                                                   │
 │  Rust core  (krate-manager-core: pure library, Tauri-free)                                 │
 │   feed client ─ version model ─ detection ─ download+verify ─ extract ─ install drivers    │
 │        │            ▲                 │                                      │             │
 │        │            │  Platform (Windows | macOS | Linux impl) + injected Roots │             │
 │        ▼            │                 ▼                                      ▼             │
 │   state dir: settings · install manifests · staging · logs        user-writable roots      │
 │                                                                   (~/.vst3, Linux only)   │
 └───────┬──────────────────────────────────────────────────────────────────┬────────────────┘
         │ HTTPS (feed URL injected)                                         │ job file (verified
         ▼                                                                   ▼ staged paths + hashes)
 GitHub Pages: versions.json (schema 2)        krate-manager-helper  ◄── UAC (Windows) / osascript admin
               manager/latest.json (Tauri)     fixed op set only          (macOS); none on Linux
 GitHub Releases (per-tag URLs from the feed):      │
   each plugin's existing .exe / .pkg / .tar.gz,    ▼
   SHA256SUMS, one .minisig per asset,           system roots, written by the installers the helper runs:
   release.json                                  C:\Program Files\Common Files\VST3\Krate Audio\<P>, C:\ProgramData\Krate Audio\<P>, /Library/Audio/Plug-Ins/…, …
```

**Processes.** (1) The webview renders state and sends intents; it gets the narrowest Tauri capability set (only the
app's own commands) and no remote content, because sensitive operations must not be driveable from JS with arbitrary
paths (tauri_feasibility.md Q7 inference, CVE-2026-95624 lesson in self_update.md KQ1). (2) The Rust core does
every network, filesystem and process operation in `#[tauri::command]` functions (tauri_feasibility.md Q1 inference:
granting the webview `fs` scopes over `/Library/Audio/Plug-Ins/**` widens the attack surface for nothing). (3) The
elevated helper is a separate binary shipped as a Tauri sidecar (`bundle.externalBin`, tauri_feasibility.md Q1, source
https://v2.tauri.app/develop/sidecar/), launched by the core per batch, never by the webview.

**Per-OS install strategy (what the probes support; the manager runs the existing installers on Windows and macOS).**

| OS | Plug-in root | Factory presets (where the plugins read them) | Install mechanism and elevation | Evidence |
|---|---|---|---|---|
| Windows | `C:\Program Files\Common Files\VST3\Krate Audio\<P>\<P>.vst3` (the path the Inno script uses, `setup.iss:37,72`; the installer decides it, there is no per-user opt-in) | `C:\ProgramData\Krate Audio\<P>` (`preset_paths.cpp:36-41`; Membrum adds `\Kits`, `plugins/membrum/installers/windows/setup.iss:69`), placed by the installer | The helper runs the plugin's existing Inno `.exe` with `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LOG`, sequentially, behind one UAC prompt per batch. Uninstall runs the Inno uninstaller silently and polls until the bundle folder is gone (the uninstaller returns before its files are removed) | A per-user folder cannot be the default: Ableton Live 12, FL Studio, Renoise ignore it (probes line 8; windows_per_user_vst3_daw_support.md summary table); the `.exe` installers already ship |
| macOS | `/Library/Audio/Plug-Ins/{VST3,Components}`, written by the existing `.pkg`; the AUv3 app is installed whenever the release's pkg contains it (the pkg has no choices, the manager does not handle it separately) | `/Library/Application Support/Krate Audio/<P>[/<preset_subdir>]` (`preset_paths.cpp:42-43`; `release.yml:306-312`), placed by the pkg | The helper runs each existing `.pkg` through `installer -pkg FILE -target /`, sequentially, behind one osascript `with administrator privileges` prompt per batch (the idiom the Tauri updater uses, tauri_feasibility.md Q2). Uninstall removes the files `pkgutil --files` lists for the receipt, then runs `pkgutil --forget` | `installer -pkg` installs unquarantined and leaves a receipt (probes line 14); the shipped bundles are already ad-hoc signed (probes lines 13-15, 22-27) |
| Linux | `~/.vst3/<P>.vst3` (settings root, default `~/.vst3`, no prompt; Steinberg priority 1, Ardour default, the only place Flatpak Bitwig sees; per_os_install_mechanics.md KQ6) | `~/Documents/Krate Audio/<P>` plus `preset_subdir` from `version.json` when set (the Linux README's own instruction), placed by the manager and listed in its manifest; they appear as user presets in the browser | None. The manager extracts the existing Linux-x64 `.tar.gz` itself (there is no installer) and tracks every path it placed in a per-plugin manifest, so uninstall is exact | No signing cost on Linux (unsigned_distribution.md KQ3) |

**Repo fact that shapes the strategy:** the plugins read factory presets only from a system folder on every OS
(`plugins/shared/src/platform/preset_paths.cpp:33-51`; scanned at `plugins/shared/src/preset/preset_manager.cpp:118-121`)
and user presets from `~/Documents/Krate Audio/<P>` (`preset_paths.cpp:8-31`). On Windows and macOS the existing
installers put the factory presets in that system folder. On Linux there is no installer and a prompt-free per-user
install cannot write `/usr/share/krate-audio`, so the manager follows what the Linux README already tells users
(`plugins/membrum/installers/linux/README.txt`, "FACTORY PRESETS") and places them in the user preset folder. A
plugin-side per-user factory folder is not planned.

**Feed and artifact model.**
- `versions.json` on Pages moves to `schema_version: 2`. The three keys the shipped parser reads stay at their path and
  type: `plugins.<Name>.version` (required string), `download_url`, `release_notes` (optional strings)
  (`update_checker.cpp:228-256`); the parser never reads `schema_version` and turns any exception into "no update"
  (`update_checker.cpp:258-261`). New keys go beside them (feed_and_ci.md "Feed schema design", the v2 example):
  `tag`, `pub_date`, `channel` (`"stable"`; prereleases only in the `prerelease` sibling), `release_notes_url`, `notes_markdown`, a `platforms` map keyed `{os}-{arch}`
  with `{url, size, sha256, signature, format, min_os}` (`format` = `inno-exe`, `pkg` or `tar.gz`; the entry is the plugin's existing installer asset, there is no nested `installer` object; the Linux `tar.gz` entry also carries `payload` = file count + digest of the archive's
  canonical file listing, Phase 1), and an
  `install` block (`vst3_bundle`, `au_component`, `preset_subdir`, `windows_uninstall_key`, `macos_pkg_id`); a
  top-level `manager` object points at `manager/latest.json`. `release_notes` stays `""` because nine editors render it
  inline (report line 50).
- `manager/latest.json` is a separate Tauri-shaped file: Tauri's static format needs `version` and `platforms` at the
  top level and validates the whole file, so one file cannot serve both parsers (report line 52; https://v2.tauri.app/plugin/updater/).
- Every binary stays on GitHub Releases at its **per-tag** `browser_download_url`. The feed never contains
  `/releases/latest/…` (in a monorepo "latest" is whichever plugin shipped last) and the manager never calls
  `api.github.com` (60 requests/hour per IP unauthenticated) (report line 54; feed_and_ci.md "Hosting").
- Per release, CI adds **no new archive**. It adds `SHA256SUMS` over the existing assets (Windows Inno `.exe`, macOS
  `.pkg`, Linux `.tar.gz`), one `.minisig` per asset and a small `release.json` manifest (report table at line 56-61).
  The manager downloads the existing assets verbatim. A one-time backfill job (Phase 2) attaches the same files to each
  plugin's current release. One minisign key signs both plugin assets and manager
  updates: `tauri signer` keys are standard minisign keys, verified both ways (probes line 10).

**Self-update path.** `tauri-plugin-updater` ≥ 2.14.1 with `allowDowngrades` left `false` and
`requireSignedVersion: true` (two September-2026 CVEs; report line 46; self_update.md KQ1). Windows: NSIS
`installMode: currentUser` so self-update never raises UAC, updater `passive` mode (self_update.md KQ7). macOS: the
updater un-tars in memory and swaps the bundle with `renamex_np(RENAME_SWAP)`, writing no quarantine
(tauri_plist_quarantine_and_runners.md Q2; probes line 12), falling back to an AppleScript admin prompt only on
`PermissionDenied`. Linux: AppImage overwrite in place, or `pkexec dpkg -i` / `rpm -U` for packages (self_update.md KQ1).
An end-to-end self-replace has **not** been tested (probes line 19); Phase 11 makes it a CI gate.

**Where state lives.** One per-user state directory (the OS app-data location, resolved through the platform
abstraction and injected in tests) holds settings (the Linux plug-in root and the Linux graphics toggle), one install
manifest per Linux plugin (every path the manager placed, with its hash, the Scoop/Homebrew model, report line 34; on
Windows and macOS the installer's own record, the Inno uninstall key and the `pkgutil` receipt, plays that role), the
swap journal (Phase 9, Linux), the download/staging area, and logs. The core writes it atomically; the webview never
touches it directly.

### Key design decisions (from the research and probes; not open)

1. **Tauri 2.** The only stack whose first-party updater ships without an Apple or Microsoft certificate on all three
   OSes (report lines 5-20; framework_comparison.md "Ranked recommendation"). Current versions at research time:
   `tauri` 2.12.5, `tauri-plugin-updater` 2.14.1 (tauri_feasibility.md header and Q1, crates.io).
2. **Strategy A: run the existing installers on Windows and macOS (user ruling 2026-10-29, reversing the earlier
   "Strategy B, extract and place").** The manager downloads the existing `.exe` / `.pkg` verbatim and the helper runs
   them; Linux, which has no installer, is extracted by the manager. Plugins installed earlier by the same `.exe` or
   `.pkg` are upgraded in place (same Inno AppId, same pkg identifier) and need no migration.
3. **Windows default is the system VST3 folder behind one UAC prompt**, written by the Inno installer (probes line 24).
4. **One minisign key for both feeds**, generated with `tauri signer generate`, stored as a CI secret the user creates;
   never generated into or committed to the repo (probes lines 10, 26).
5. **No platform signing step is needed for the plugins on macOS:** the shipped plug-ins are already ad-hoc signed
   (probes line 15), so the report's "mandatory CI codesign step" (report line 38) is not a Part A deliverable. The CI
   macOS configure passes `-DSMTG_CODE_SIGN_IDENTITY_MAC="-"` and relies on the SDK's post-build ad-hoc sign
   (`.github/workflows/ci.yml:599-604`). The **manager** app is ad-hoc signed by the Tauri bundler
   (`"signingIdentity": "-"`), and `codesign --verify --deep --strict` passes on it (probes line 17).
6. **Detection reads files, never loads code:** `Contents/Resources/moduleinfo.json` (JSON5 trailing commas) on all
   OSes, AU `Info.plist` on macOS (report line 26). The installed version is the **class** `Version` reduced to three
   parts: it is the one that tracks `version.json` (report line 26) on old and new bundles alike, and
   `tools/check-bundle.js:50-55` requires every class `Version` to equal `version.json` or start with `"<ver>."`. Bundles
   built since commit `294a26e7` also carry it at top level; bundles released before each plugin's next release still
   say `0.8.0` there (probes line 20). The top-level `Version` is therefore only a consistency check, logged when it
   disagrees, never the source (Phase 6).
7. **Smart App Control (Windows 11, enforcement mode) is unsupported**, documented as such: it blocks unsigned code
   outright and only the user can turn it off (report line 42; unsigned_distribution.md KQ2a).

## Reuse Inventory (existing repo → Krate Manager)

Legend: ✅ reused unchanged · 🔶 changed (additively) · 🆕 new.

| Piece | File (verified) | What the manager uses | Change |
|---|---|---|---|
| Release workflow | `.github/workflows/release.yml` — manual `workflow_dispatch` with `plugin` choice and `prerelease` flag (`:26-48`); tag `<plugin>/v<ver>` (`:76`); Windows job stages `<Name>.vst3` + `presets/` + `CHANGELOG.txt` (`:221-237`); macOS job stages VST3 + component + optional AUv3 app + presets and reads `preset_subdir` and `identifier` from `version.json` (`:285-337`); Linux job tars `<Name>.vst3/` + `presets/` + `README.txt` + `CHANGELOG.txt` (`:379-403`); `release` job uploads three assets with `softprops/action-gh-release@v2` (`:514-526`) | The existing assets the manager downloads verbatim (`.exe`, `.pkg`, `.tar.gz`) | 🔶 Phase 2: add `SHA256SUMS`, `.minisig`s, `release.json`, provenance attestation (no new archives), `make_latest: false`, a dry-run input. Phase 3: replace the inline feed step with a call to `feed.yml` |
| Feed generator | `release.yml:645-685` (inline Node: per `plugins/*/version.json`, latest local git tag, emits `{schema_version: 1, plugins: {Name: {version, download_url: data.url, release_notes: ""}}}`; needs `fetch-depth: 0`, `:542`) | The v1 key semantics to preserve | 🆕 `tools/gen-feed.js` (Phase 1) + 🆕 `feed.yml` (Phase 3) replace it. Note: today's step takes the highest git tag including tags of GitHub prereleases (`:665`); Phase 1 states the v1 `version` rule explicitly (Q9) |
| Landing page | the 'Generate Root Index' step of `release.yml` and the root-index step of `docs.yml` (`tools/gen-root-index.js`), also run by `release-manager.yml` job `landing` | — | 🔶 Phase 12: ported to `tools/gen-root-index.js` (Node; Clarifications Q2), which renders the plugin cards byte-for-byte as before plus a Krate Manager card |
| In-plugin parser | `plugins/shared/src/update/update_checker.cpp:222-262`; tests `plugins/shared/tests/test_update_checker.cpp:79` ("UpdateChecker - JSON parsing", `[update]`) | The compatibility contract | 🔶 Phase 1: one added test case feeding a v2 document (test-only change, no plugin release) |
| Plugin feed URL | `plugins/*/src/update/*_update_config.h` (e.g. `iterum_update_config.h:12`) | The Pages site stays the feed home; the shipped plugins reach it through the `github.io` 301, the manager uses `https://krateaudio.com/versions.json` | ✅ |
| `version.json` | `plugins/<p>/version.json` (e.g. Membrum: `"version": "0.12.2"`, `"preset_subdir": "Kits"`); read by `cmake/KratePlugin.cmake:35-56` (`krate_plugin_read_version`, sets `PROJECT_VERSION` so moduleinfo carries it) | Name, version, `preset_subdir`, optional `identifier` (`release.yml:17,321`) feed the `install` block | ✅ read only. `preset_subdir` is consumed only by the macOS job (`release.yml:306`); the Windows script hard-codes `Kits` (`membrum/installers/windows/setup.iss:69`); the feed takes it from `version.json` |
| Bundle gate | `tools/check-bundle.js` (moduleinfo `Version` and class `Version` must match `version.json`, JSON5 commas stripped, `:27-55`); run in CI `ci.yml:480,774,1269` | The detection contract (class versions are four-part, `startsWith("<ver>.")`, `:52`) | ✅ |
| Inno scripts | `plugins/<p>/installers/windows/setup.iss` (nine): `AppId={{GUID}` (literal `{GUID}`), `DefaultDirName={commonpf}\Common Files\VST3\Krate Audio\<P>`, `PrivilegesRequired=admin`, presets to `{commonappdata}\Krate Audio\<P>`; Iterum's `[InstallDelete]` removes obsolete factory presets (`iterum/installers/windows/setup.iss:80-94`) | What the manager runs: the existing `.exe` silently (same AppId, so a newer installer upgrades an older install in place), and the uninstaller found through the `<GUID>_is1` uninstall key (64-bit HKLM view, per_os_install_mechanics.md KQ2) | ✅ unchanged; the AppId is read into the feed's `windows_uninstall_key` |
| macOS preinstall | `plugins/iterum/installers/macos/preinstall` (removes the same obsolete presets as root) | Runs as part of the `.pkg` the manager installs; nothing for the manager to honour | ✅ unchanged |
| Preset paths | `plugins/shared/src/platform/preset_paths.cpp:8-51` | Where factory and user presets must land | ✅ unchanged (a plugin-side per-user factory folder is not planned; on Linux the manager places the factory presets in the user folder) |
| Probe script | `specs/krate-manager/probes/macos-gatekeeper-probe.js` | The quarantine / signature checks to turn into regression gates | 🔶 Phase 4 reuses its Q6 checks inside the manager CI workflow |
| Gate tooling | `tools/run-gate.js`, `tools/header-consumers.js` | The one C++ test change (Phase 1) runs `shared_tests` sharded | ✅ |
| CI build artifacts | `ci.yml:960-969` (macOS: raw `.vst3` / `.component` / AUv3 `.app` via `upload-artifact@v7`, zipped, so modes are lost); Linux uploads `ci.yml:1287-1351` | Nothing: the manager runs the released installers, not these bundles | ✅ unchanged (the tar upload that would have preserved modes for new zips is dropped with the zips) |
| 🆕 | `apps/krate-manager/` (proposal), `.github/workflows/{manager-ci,feed,feed-tools,release-manager,backfill-sign}.yml`, `tools/gen-feed.js`, `tools/gen-release-manifest.js`, `tools/minisign.js`, `tools/feed-contract/`, `tools/lint-manager-version.js`, `tools/backfill-sign.js`, `tools/gen-manager-latest.js`, `tools/manager-release-preflight.js` | — | New |

## Repository Layout (decided, Q1)

```
apps/krate-manager/
  Cargo.toml                 workspace: core, helper, src-tauri
  package.json, tsconfig.json (strict), vite + vitest + linter config (chosen in Phase 4)
  version.json               single version source (like plugins/<p>/version.json); first public version 1.0.0;
                             a lint checks tauri.conf.json and Cargo.toml equal it
  CHANGELOG.md               manager changelog (one entry per released version)
  src/                       TypeScript UI (view + state only)
  src-tauri/                 thin Tauri shell: tauri.conf.json, capabilities/ (minimal), commands → core
  crates/core/               krate-manager-core: feed, version model, Platform + Roots, detection, download,
                             verify, Linux extract, per-OS install drivers, manifests. No Tauri dependency, so `cargo test`
                             needs no webview stack
  crates/helper/             krate-manager-helper: elevated binary + the validated-op library it wraps
  tests/fixtures/            feeds, bundles, archives (signing keys are generated per test run, never committed)
.github/workflows/manager-ci.yml       fmt, clippy, cargo test, tsc, lint, vitest, tauri build on 3 OSes
.github/workflows/feed-tools.yml       node --test for tools/gen-feed.js, gen-release-manifest.js, minisign.js on 3 OSes
.github/workflows/feed.yml             builds versions.json v2 from Releases + release.json, deploys Pages
.github/workflows/release-manager.yml  bundle.js --release matrix, manual dispatch, tag manager/vX.Y.Z, tools/gen-manager-latest.js writes latest.json → feed.yml (Phase 11 D4)
.github/workflows/backfill-sign.yml    one-time: signs each plugin's current release assets, attaches SHA256SUMS, .minisigs, release.json
tools/gen-feed.js                      pure feed builder (fixtures in, versions.json out), `node --test`
tools/gen-release-manifest.js          release.json from staged assets (+ sizes, sha256, signatures)
tools/minisign.js                      minisign keygen / sign / verify over node:crypto (tests and tools; CI signs with rsign2)
tools/feed-contract/                   feed-v2 and release.json JSON Schemas, trusted-comment grammar, Linux archive layout,
                                       payload-digest rule, shared fixtures (Phase 1; read by the tools and the manager tests)
tools/feed-contract/keys/krate-signing.pub  the release signing public key (minisign, two lines; committed by the user, Phase 2
                                       FR-061); CI checks every signature against it, Phase 7 embeds it, Phase 11 is its updater pubkey
tools/release-facts.js                 Mach-O arch set + minimum macOS, glibc floor, Linux listing check (Phase 2)
tools/backfill-sign.js                 backfill logic over an injected `gh` runner (fixtures in tests), `node --test`
tools/lint-manager-version.js          version.json vs tauri.conf.json vs Cargo.toml equality lint, gated in manager-ci
tools/gen-manager-latest.js            `latest.json` with per-tag URLs, every signature verified (Phase 11)
tools/manager-release-preflight.js     version, CHANGELOG, tag, updater-config and key checks; the release body (Phase 11)
```

`apps/` is outside `plugins/`, so the plugin roster lints (`tools/lint-plugin-roster.js`,
`tools/lint-plugin-editor-baseline.js`) and `ci.yml`'s path filter (`ci.yml:4-23`, which lists `plugins/**`, `dsp/**`,
… but not `apps/**`) do not fire on manager-only changes; the manager has its own workflow. The same filter lists no
`tools/**`, and no workflow in `.github/workflows/` (`ci.yml`, `docs.yml`, `long-tests-nightly.yml`, `release.yml`,
`valgrind-nightly.yml`) runs `node --test` today, so the Part A tools get their own lane, `feed-tools.yml` (Phase 1).

---

## Part A — Release Pipeline and Feed (plugin side; Node / YAML / one C++ test; testable on Windows)

### Phase 1: Feed v2 Schema and Generator

**Status: ✅ COMPLETE (PENDING-CI: 13 item(s)) (2026-10-30)** — see specs/krate-manager-phase1/compliance.md

**Spec:** `krate-manager-phase1`

Phase 1 owns **every contract** that later phases build or test against, so Phases 2-3 and Phases 4-10 depend on
Phase 1 and not on each other. All of it lives in `tools/feed-contract/` (schemas, prose, fixtures).

Components:
- 🆕 **C1, versions.json v2 schema**: a JSON Schema file plus a prose contract (field, type, required, who reads it).
  Base: feed_and_ci.md v2 example (lines 50-80), with the additions this roadmap rules: platform keys follow Tauri's
  `{os}-{arch}` (`windows-x86_64`, `linux-x86_64`, `darwin-aarch64` / `darwin-x86_64`, or `darwin-universal` if Phase 2
  finds the macOS plug-in binaries universal); `install` fields are plain names validated by pattern (no path
  separators), because the manager never turns a feed string into a filesystem path unchecked. Each `platforms` entry describes the plugin's existing installer asset (`format`: `inno-exe`, `pkg` or `tar.gz`); only the Linux `tar.gz` entry
  carries `payload: {files, sha256}`: the file count and the SHA-256 of the archive's **canonical listing** (C4); the `.exe` and `.pkg` entries carry none (the manager verifies only their size, SHA-256 and signature). The
  full per-file list stays in `release.json` (C2) because factory-preset trees run to hundreds of files per plugin and
  nine plugins × three platforms of them would bloat the file nine editors poll; the digest binds the feed to the exact
  list, and the manager rebuilds the list from the verified, extracted Linux tree (Phase 8) and compares digests. The
  signed archive is the source of the list, so the manager never needs `release.json` itself.
- 🆕 **`channel`** (Q9 ruled): each plugin entry carries `channel: "stable"`, and a prerelease newer than the
  newest stable release goes into a sibling `prerelease` object with `channel: "beta"` and the same shape as the
  plugin entry minus the v1 keys (feed_and_ci.md lines 62 and 97; report line 52: prereleases "under a `channel:
  "beta"` sibling, not into `version`, because the v1 parser upgrades on any higher SemVer"). The manager ignores the
  sibling in v1.
- 🆕 **C2, release.json schema**: `tag, name, version, pub_date, channel, assets{platform-key: {name, url, size, sha256,
  signature, format, min_os, payload?}}, install{…}`, where each asset is the plugin's existing installer asset and
  `payload{files, sha256, list}` (`list` = the canonical listing (C4)) exists only for the Linux `tar.gz`. `gen-feed.js` (below) and `gen-release-manifest.js` (Phase 2) both validate against it.
- 🆕 **C3, trusted-comment grammar** for every plugin-asset signature: exactly one line,
  `krate:1:<Name>:<version>:<platform-key>:<asset-file-name>` (`<Name>` = the feed key, e.g. `Iterum`; `<version>`
  SemVer; `<platform-key>` one of C1's keys; `<asset-file-name>` the release asset's name, so one asset's signature
  cannot stand in for another asset's). minisign signs the trusted comment with the global signature
  (`ed25519(<signature> || <trusted_comment>)`, https://jedisct1.github.io/minisign/ "Signature format", minisign
  0.12), so a valid archive cannot be replayed under another entry. The manager's own updates keep Tauri's format
  (Phase 11).
- 🆕 **C3 known-answer fixture**: one small file, its signature made with `rsign2` 0.6.7 (`rsign sign <file> -t
  "<C3 comment>"`, https://github.com/jedisct1/rsign2; the tool the probes proved against a `tauri signer` key, probes
  line 10) and the matching public key, committed under `tools/feed-contract/fixtures/`. The throwaway private key
  that made it is deleted after generation and never committed (the cross-cutting key rule). Phases 2 and 7 verify it
  with the Node and the Rust implementation.
- 🆕 **C4, Linux archive layout and canonical listing.** The Linux tarball has no wrapping top folder (`release.yml:402-403`
  `cd staging; tar -czvf … *`) and its layout stays as users extract it today: `<Name>.vst3/`, `presets/`,
  `README.txt`, `CHANGELOG.txt` (`release.yml:384-395`). Windows and macOS have no archive layout: the manager downloads the
  existing `.exe` / `.pkg` verbatim and the installers place the files. The canonical listing is one line per entry,
  `<type> <mode> <relative POSIX path>[ -> <link target>]` (`type` f/d/l, `mode` octal), sorted bytewise, LF
  terminated; its SHA-256 is `payload.sha256`.
- 🆕 `tools/gen-feed.js`: a pure function from (plugin `version.json` set, per-release `release.json` set, release list
  with prerelease flags) to `versions.json`. No network; inputs are files. `node --test` fixtures. **The Q9 rule for
  the v1 `version` key is applied here and only here**: the v1 `version` is the newest non-prerelease release (a
  prerelease never changes it; today's generator takes the newest tag, `release.yml:665`).
- 🆕 A legacy-compat mode that reproduces today's v1 output from tags only, so the v1 keys are proven byte-identical
  before anything else changes.
- 🔶 One Catch2 case in `plugins/shared/tests/test_update_checker.cpp` feeding a v2 document (with `platforms`,
  `install`, `manager`, `channel`, a `prerelease` sibling, `schema_version: 2`) to `parseResponse` and asserting the v1
  result is unchanged; plus a case with `schema_version: 99` (the parser must not care).
- 🆕 `.github/workflows/feed-tools.yml`: `node --test` for `tools/gen-feed.js` (and, from Phase 2,
  `tools/gen-release-manifest.js` and `tools/minisign.js`) on `ubuntu-latest`, `macos-latest` and `windows-latest`,
  path-filtered to those files, `tools/feed-contract/**` and itself. Needed because `ci.yml`'s filter
  (`ci.yml:5-13`, `:16-23`) lists no `tools/**` and no workflow runs `node --test` today.

Measurable outcomes (for SCs):
- For every fixture **without a prerelease newer than its latest release**, the v1 projection (`schema_version`,
  `plugins.*.{version, download_url, release_notes}`) of the v2 output equals today's generator output key for key.
  For prerelease fixtures a prerelease never changes the v1 `version` key (it stays the newest non-prerelease) and
  appears only in the `prerelease` sibling.
- Every C1-C4 contract has a fixture set checked against it: valid and invalid `versions.json` and `release.json`
  documents against their schemas (accept / named rejection); trusted comments against the C3 grammar (wrong field
  count, a platform key outside C1, a non-SemVer version, a path separator in the file name: each rejected); and
  canonical listings computed from fixture trees on disk equal their committed expected listing and digest.
- The C++ v2 case passes in `shared_tests` (run through `node tools/run-gate.js --filter '[update]' shared_tests`);
  `node tools/header-consumers.js` confirms no shared header changed.
- The generator rejects (non-zero exit, named error) any input that would put a non-string in a v1 key, a
  `/releases/latest/` URL anywhere, a non-`https` URL, or an `install` value containing a path separator or `..`.
- `release_notes` is `""` for every plugin in every output.
- The known-answer fixture's trusted comment parses under C3, and `rsign verify` accepts the fixture with its
  committed public key (run once when it is made, and in the `feed-tools.yml` legs below).
- PENDING-CI: `feed-tools.yml` jobs `node-test (ubuntu-latest | macos-latest | windows-latest)` green, each running the
  same `node --test` set as the local Windows run.

Does NOT: touch `release.yml`, publish anything, sign anything with a real key (the fixture uses a throwaway one),
or change any plugin source.

### Phase 2: Release Assets, Hashes, Signatures and the Backfill

**Status: ✅ COMPLETE (PENDING-CI: 15 item(s)) (2026-10-30)** — see specs/krate-manager-phase2/compliance.md

**Spec:** `krate-manager-phase2`

Components:
- 🔶 **No new archives.** `release.yml` keeps uploading the three assets it uploads today, and those are what the
  manager downloads verbatim: the Windows Inno `.exe`, the macOS `.pkg` (`release.yml:331-337`) and the Linux
  `.tar.gz` (`release.yml:403`). No Windows zip, no macOS `ditto` zip, and no `ci.yml` tar-upload or build-tree listing
  for them. The Linux tarball's canonical listing (Phase 1 C4) is computed from the tarball itself.
- 🔶 The release jobs record the facts the feed needs and nobody has checked yet: the macOS job `lipo -archs` of each
  Mach-O (no `CMAKE_OSX_ARCHITECTURES` is set anywhere in the repo's CMake or workflows, so universal vs arm64-only is
  unverified) and the minimum macOS version from the load commands (no `CMAKE_OSX_DEPLOYMENT_TARGET` either). The Linux
  job records the minimum glibc (`objdump -T`); the Linux plugins build on `ubuntu-latest` (`ci.yml:1060-1062`), not the
  22.04 the report assumed (report line 65).
- 🆕 `tools/gen-release-manifest.js`: writes `release.json` in the Phase 1 C2 schema (per asset: size, SHA-256,
  signature; for the Linux `tar.gz` also the `payload` count, digest and C4 listing computed from the tarball itself).
- 🆕 **Signing and verification tooling (ruled here):**
  - CI signs with **`rsign2` 0.6.7** (https://docs.rs/crate/rsign2/latest, released 2026-09-25; README
    https://github.com/jedisct1/rsign2: `rsign sign <file> -t "<trusted comment>"`, signatures "can be verified with
    `minisign`" and the reverse). It is the tool the probes already proved against a `tauri signer` key in both
    directions (probes line 10). `tauri signer sign` is not used for plugin assets: whether it signs arbitrary files
    with a custom trusted comment was never confirmed (feed_and_ci.md line 190), and the tauri CLI is not in the repo
    until Phase 4. CI installs it with `cargo install rsign2 --version 0.6.7 --locked`.
  - Node tests and tools use **`tools/minisign.js`**, a small in-repo implementation over `node:crypto`, because no
    minisign npm verifier exists (feed_and_ci.md line 174): keygen (`generateKeyPairSync('ed25519')` + random 8-byte
    key id), sign and verify of the prehashed `ED` variant (`ed25519(Blake2b-512(<file>))`, the format new
    implementations must use, https://jedisct1.github.io/minisign/ "Signature format", minisign 0.12), the global
    signature over `<signature> || <trusted comment>`, and the key id check. Ed25519 is `crypto.sign(null, data, key)`
    / `crypto.verify(null, …)` (https://nodejs.org/docs/latest-v26.x/api/crypto.html, Ed25519 example); Node v24.7.0 on
    this box reports `blake2b512` in `crypto.getHashes()` and a sign/verify round-trip (checked 2026-10-29). Unencrypted
    keys only: it signs nothing but throwaway test keys.
  - `tools/minisign.js` is checked against the Phase 1 rsign2 **known-answer fixture**; Phase 7 checks the Rust path
    against the same fixture, so the Node and Rust implementations cannot drift apart unnoticed.
- 🔶 `release` job: `SHA256SUMS` over every asset, one minisign signature per asset made with the CI secret, each
  signature's trusted comment in the **Phase 1 C3 grammar** (binding plugin, version, platform key and asset name;
  the same mechanism Tauri's `requireSignedVersion` uses for its `version:` field, self_update.md KQ1), so a valid
  asset cannot be replayed under another entry. Upload `release.json`, `SHA256SUMS`, `.minisig`s; add
  `actions/attest-build-provenance` (human-verifiable provenance, never consulted by the manager; report line 63);
  set `make_latest: false` (report line 54).
- 🔶 A `dry_run` input on `release.yml` that builds every asset, the manifest and the signatures (with a throwaway
  key generated in the job) and uploads them as workflow artifacts, without tagging, releasing or deploying Pages.
  It is how the user proves the pipeline without shipping.
- 🆕 **Backfill-sign job (Q10 ruled)**, `.github/workflows/backfill-sign.yml` over `tools/backfill-sign.js` (manual
  dispatch, `dry_run` input defaulting to `true`): for each of the nine plugins it finds the current release (the newest
  non-prerelease, the Phase 1 rule), downloads that release's existing `.exe`, `.pkg` and `.tar.gz`, and with the same
  `gen-release-manifest.js` and rsign2 signing as the `release` job writes `SHA256SUMS`, one `.minisig` per asset (C3
  trusted comment binding plugin, version, platform and asset name) and `release.json`, then attaches them to that
  existing release with `gh release upload` and no `--clobber`. It changes no existing asset and publishes no new
  release, so all nine plugins are manageable at the manager's first release with no plugin re-release. It is
  idempotent: a release that already carries `release.json` is skipped. The facts a release-time job would have recorded
  (macOS `lipo -archs` and minimum OS, Linux glibc) are derived from the downloaded assets; the spec chooses how (for
  example `pkgutil --expand-full` on a macOS runner, `objdump -T` on the tarball). Detection of these older releases
  still falls back to the class `Version` where a bundle's top-level `moduleinfo` `Version` says `0.8.0` (Phase 6).
- The private key is a repository secret the user creates with `tauri signer generate` (probes line 10). No key is
  generated into the repo; tests generate a throwaway key in a temp dir.

Measurable outcomes:
- `gen-release-manifest.js` on a fixture asset set (stub `.exe`, stub `.pkg`, a fixture `.tar.gz`) yields sizes and
  SHA-256 equal to independent computation, and every `signature` (made by `tools/minisign.js` with a throwaway key
  generated in a temp dir) verifies against the throwaway public key; a one-byte tamper fails (`node --test`, Windows).
- `tools/minisign.js` verifies the Phase 1 rsign2 known-answer fixture, and rejects it after a one-byte change to the
  file, the signature line or the trusted comment (`node --test`, Windows).
- A trusted comment for asset A presented with asset B's metadata fails the binding check.
- PENDING-CI: `feed-tools.yml` (all three legs) runs the same `node --test` set and, with `rsign2` 0.6.7 installed,
  checks interop both ways: `rsign verify` accepts a `tools/minisign.js` signature with a C3 trusted comment, and
  `tools/minisign.js` accepts a fresh `rsign sign -t` signature.
- The Linux tarball's `payload` digest equals the C4 listing digest of the extracted tarball (`node --test` on a
  fixture tarball, Windows; PENDING-CI on the `dry_run` Linux job for the real tarball).
- Backfill, with an injected fake `gh` runner and a fixture set of nine releases: every release gets `SHA256SUMS`, three
  verifying `.minisig`s and a `release.json` that validates against the Phase 1 C2 schema; no existing asset is
  re-uploaded or overwritten (the fake records every upload argv: no `--clobber`, no existing name); a second run
  uploads nothing; a release missing one of the three assets is reported by name and gets nothing attached (no
  half-signed release).
- PENDING-CI (user-triggered): `backfill-sign.yml` with `dry_run: true` on the real repository lists nine plugins ×
  three assets, produces `release.json` files that validate against C2, and uploads nothing. A live run is a user step
  after that green dry run, not this phase's exit criterion.
- The `dry_run` run of `release.yml` (PENDING-CI, user-triggered) produces `SHA256SUMS`, the `.minisig`s and
  `release.json` for one plugin; `release.json` validates against the Phase 1 C2 schema and each asset's SHA-256
  equals that of the built asset.

Does NOT: change the installers' content, regenerate the feed, publish a release, add any codesign step (Key
decision 5), or change `ci.yml`.

### Phase 3: Feed Workflow and Pages Deploy

**Status: ✅ COMPLETE (PENDING-CI: 6 item(s)) (2026-10-30)** — see specs/krate-manager-phase3/compliance.md

**Spec:** `krate-manager-phase3`

Components:
- 🆕 `.github/workflows/feed.yml` (`workflow_call` + `workflow_dispatch`): lists releases with `gh release list`
  (CI only; ~20 of the 1,000 hourly `GITHUB_TOKEN` requests, feed_and_ci.md "CI pipeline"), downloads each plugin's
  newest `release.json`, runs `tools/gen-feed.js`, copies `manager/latest.json` when a manager release exists, deploys
  with `peaceiris/actions-gh-pages@v4` and `keep_files: true` (as `release.yml:688-692` does today), plus `cname: krateaudio.com` (as `docs.yml:240` does), so a feed
  deploy never drops the custom domain both the manager and the plugins' 301 depend on. Idempotent; needs
  no `fetch-depth: 0`.
- 🆕 A `dry_run` input on `feed.yml` (default `true` for a manual dispatch): it builds `_site/versions.json` (and
  `manager/latest.json` when a manager release exists), validates it against the Phase 1 C1 schema, runs the v1
  projection comparison against the currently live `versions.json` (every v1 key equal, except where a release
  newer than the live file explains the difference, which the step prints), and uploads the result as a workflow
  artifact **without** calling `peaceiris/actions-gh-pages`. Nine shipped editors poll the live file, so a broken
  generator must be seen in an artifact first, never by users.
- 🔶 `release.yml` `deploy-pages` calls `feed.yml` instead of the inline generator (`release.yml:645-685`), passing
  `dry_run: false` for a real release and `dry_run: true` when `release.yml` itself runs with its Phase 2 `dry_run`.
- Plugins whose newest release has no `release.json` (until the Phase 2 backfill has run, Q10 ruled) keep their v1
  entry and get no `platforms`; the manager shows "not available through the manager yet" for them. After the backfill
  all nine plugins have a `release.json`.

Measurable outcomes:
- Running `gen-feed.js` twice on the same inputs yields byte-identical output.
- On a fixture of today's real tag set, v1 keys are identical to the live `versions.json` shape the plugins parse.
- A feed with one v2 plugin and eight v1-only plugins validates against the Phase 1 C1 schema and parses in the C++
  guard test (`node tools/run-gate.js --filter '[update]' shared_tests`). The same fixture is in Phase 5's list for
  the Rust client.
- PENDING-CI (user-triggered): a `workflow_dispatch` of `feed.yml` with `dry_run: true` is green: the artifact's
  `versions.json` validates against C1 and its v1 projection matches the live file under the rule above.
- User step after that green dry run, not this phase's exit criterion: a live dispatch (`dry_run: false`), then one
  plugin's update banner against the live feed, checked by the user.

Does NOT: move the feed to another domain, change the landing page, or touch the per-plugin installers.

---

## Part B — The App (`apps/krate-manager/`)

### Phase 4: Scaffold, Platform Abstraction and the Manager's CI

**Status: ✅ COMPLETE (PENDING-CI: 14 item(s)) (2026-10-30)** — see specs/krate-manager-phase4/compliance.md

**Spec:** `krate-manager-phase4`

Components:
- 🆕 Tauri 2 app from `create-tauri-app` (Vite + TypeScript; template list at https://v2.tauri.app/start/create-project/,
  tauri_feasibility.md Q7). The spec picks the UI framework and the linter.
- 🆕 Cargo workspace (`crates/core`, `crates/helper`, `src-tauri`) with an empty but wired command surface.
- 🆕 `Platform` abstraction in `core` with Windows, macOS and Linux implementations behind `cfg`, and a `Roots` value
  (plug-in roots, preset roots, state dir, staging dir) that is **always passed in**: production builds it from the
  OS, tests build it from temp dirs. The feed URL is injected the same way (`AppContext`, Phase 4); the HTTP client
  base joins `AppContext` in Phase 5 and the elevation launcher in Phase 10, which joins `CommandEnv` beside the driver
  resolver rather than `AppContext` (Phase 10 D8), each with its production value built in the same setup (Phase 4 spec D17).
  The guard is mechanical, not a convention: (1) `core` has no OS-root constructor at all; `Roots::new` takes explicit
  paths, and `Platform::default_roots()` only *computes* the default paths (pure, testable on every OS, touches
  nothing). (2) The one production constructor, `Roots::from_os()`, combines them in `src-tauri` and is listed in the
  workspace `clippy.toml` under `disallowed-methods` (clippy lint `disallowed_methods`,
  https://rust-lang.github.io/rust-clippy/master/index.html#disallowed_methods), so every call to it fails
  `cargo clippy --all-targets -- -D warnings`, tests included, except the single call site in the app's setup, which
  carries `#[allow(clippy::disallowed_methods)]` with its one-line reason. (3) Every test builds roots through one
  test-support builder, `temp_roots()`, which panics on any path outside `std::env::temp_dir()`.
- 🆕 **Settings model** (moved here from Phase 12 because Phase 9 consumes it): a versioned schema (the Linux plug-in
  root, default `~/.vst3`; the Linux graphics toggle's stored value; there is no install-scope key and no per-user
  opt-in, because the Windows and macOS installers decide the paths), atomic persistence in the injected state dir
  (write temp file, fsync, rename), defaults per OS matching the per-OS table above, and an injected settings value in
  tests. No UI; Phase 12 adds the settings screen.
- 🆕 Capability file granting the webview only the app's commands; CSP allowing no remote origin.
- 🆕 `tauri.conf.json`: macOS `signingIdentity: "-"` and `minimumSystemVersion: "15.0"` (Q6: macOS 15 and later,
  Windows 10 22H2 / 11 x64, Linux at the glibc of the `ubuntu:22.04` build), Windows NSIS `installMode: "currentUser"`, no
  `LSFileQuarantineEnabled` (the bundler writes none, tauri_plist_quarantine_and_runners.md Q1).
- 🆕 **Version source (Q1):** `apps/krate-manager/version.json` is the single version source (first public version
  1.0.0, tag `manager/vX.Y.Z`, `apps/krate-manager/CHANGELOG.md`), and `tools/lint-manager-version.js` fails when
  `tauri.conf.json` or any `Cargo.toml` version in the workspace differs from it. The lint runs in `manager-ci.yml` and
  its tests in `feed-tools.yml`'s `node --test` set.
- 🆕 `.github/workflows/manager-ci.yml`, path-filtered to `apps/krate-manager/**` and itself: matrix `windows-latest`,
  `macos-latest` (arm64, macOS 26 since 2026-07-22, tauri_plist_quarantine_and_runners.md Q3), an Ubuntu leg with
  WebKitGTK 4.1 (on `ubuntu-24.04`, the current Tauri prerequisites apt line, https://v2.tauri.app/start/prerequisites/; the report line 65 `libappindicator3-dev … patchelf` set belongs to Phase 11's `ubuntu:22.04` container; Phase 4 spec D16). Each leg runs
  `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `cargo test`, `tsc --noEmit`, the linter,
  `vitest`, and an unsigned `tauri build`. The macOS leg also asserts the built `Info.plist` has no
  `LSFileQuarantineEnabled` and `codesign --verify --deep --strict` passes (probes line 17 turned into a regression gate).
  It builds natively for arm64; the universal macOS build is Phase 11's (release-manager.yml; macOS 15 and later, Intel included).

Measurable outcomes:
- All checks green on Windows locally with zero warnings; macOS and Linux legs PENDING-CI (job and step named).
- Each `Platform` implementation compiles and its `default_roots()` returns the specified paths for injected
  environment values (unit tests per OS; macOS and Linux PENDING-CI on their `manager-ci` legs).
- A deliberate call to `Roots::from_os()` inside a test target makes `cargo clippy --all-targets -- -D warnings` fail
  with `disallowed_methods`: a fixture crate `tests/fixtures/clippy-guard/` (excluded from the workspace members,
  same `clippy.toml`) calls it from a `#[test]`, and a `manager-ci` step on every leg asserts clippy exits non-zero
  naming that lint; and
  `temp_roots()` given a path outside `std::env::temp_dir()` panics (a `#[should_panic]` test).
- Settings: round-trip, an unknown future schema version rejected with a named error and the defaults kept, and a
  crash between temp write and rename (injected) leaves the previous settings file intact.
- Tests run in parallel (cargo's default threads, vitest parallel files); no `--test-threads=1`.
- The version lint (`node --test`): accepts `tauri.conf.json` and every workspace `Cargo.toml` equal to `version.json`,
  and rejects each of them differing, a missing `version.json` and a non-SemVer value, with a named error. The built
  macOS `Info.plist` has `LSMinimumSystemVersion` 15.0 (PENDING-CI, `manager-ci` macOS leg).

Does NOT: fetch a feed, read a plug-in folder, or build a release pipeline for the manager.

### Phase 5: Feed Client and Version Model

**Status: ✅ COMPLETE (PENDING-CI: 21 item(s)) (2026-10-31)** — see specs/krate-manager-phase5/compliance.md

**Spec:** `krate-manager-phase5`

Components:
- 🆕 `core::feed`: fetch `versions.json` from the injected URL (production: `https://krateaudio.com/versions.json`) over HTTPS (HTTP only for the local fixture server in
  tests), parse schema 1 and 2, ignore unknown keys, validate every v2 entry against the Phase 1 contract, and keep
  the last good copy in the state dir for offline use. A manual "check now" always fetches from the network with
  `Cache-Control: no-cache` / `Pragma: no-cache`; the Pages edge ignores them and may serve a copy up to 600 s old
  (measured 2026-10-30).
- 🆕 `core::version`: SemVer compare, platform-key resolution for the running OS/arch (native key, then
  `darwin-universal`), four-part class versions reduced to three parts for comparison (`check-bundle.js:52`).
- 🆕 A local fixture HTTP server used by tests; an opt-in live-network test, ignored by default and never run in a gate.

Measurable outcomes:
- Fixture feeds (the Phase 1 `tools/feed-contract/` set, read in place): v1-only, v2, mixed (including Phase 3's
  one-v2 / eight-v1 feed), a v2 feed with a `prerelease` sibling (ignored in v1 of the manager), malformed JSON, wrong
  types, an `install` value with `..`, a `/releases/latest/` URL, an `http://` asset URL, a `tar.gz` `platforms` entry without
  `payload`: each yields the specified result (accept, accept-without-platforms, or a named rejection), and a
  rejected feed never replaces the last good copy.
- Offline: the client returns the cached feed flagged stale.
- Roster (ROSTER ruled): the client's plugin list is exactly the feed's `plugins` keys. A fixture feed with a tenth,
  invented plugin yields ten entries, and one with a plugin removed yields eight, with no code change. A test greps the
  built core and UI for the nine plugin names and fails on any hit outside test fixtures.
- No code path names `api.github.com` (a test greps the built core for the host string).

Does NOT: read the disk for installed plugins, download artifacts, or render UI beyond what tests need.

### Phase 6: Detection and the Status Screen

**Status: ✅ COMPLETE (PENDING-CI: 18 item(s)) (2026-10-31)** — see specs/krate-manager-phase6/compliance.md

**Spec:** `krate-manager-phase6`

Components:
- 🆕 `core::detect`: scan the injected plug-in roots (per OS: the Steinberg user and global VST3 folders, the Windows
  `Krate Audio\<P>` subfolder, macOS `Components`), read `Contents/Resources/moduleinfo.json` with trailing commas
  tolerated, and take the installed version from the **class** `Version` reduced to three parts (Key decision 6: it
  tracks `version.json` on old and new bundles alike, report line 26, `check-bundle.js:50-55`, probes line 20). All
  class versions must reduce to the same value, or the copy reports "version unknown". The top-level `Version` is
  only a consistency check: a disagreement is logged and never changes the result; a moduleinfo with no class
  `Version` falls back to the top-level value, flagged. Read the AU `Info.plist` version on macOS, and report every
  copy found so a stale global copy shadowing a user copy is visible (Steinberg "first found per Processor UID",
  report line 34).
- 🆕 Installer-record facts, read only: the Inno uninstall key per plugin (64-bit HKLM view) on Windows, the `pkgutil`
  receipt (identifier and `--files` listing) on macOS, the install manifest on Linux, all behind the platform abstraction with fakes in tests. A plugin installed earlier by the old `.exe` or `.pkg` is simply installed (the current installers carry the same Inno AppId and pkg identifier, so a newer installer upgrades it in place); there is no "legacy" state (Q4 ruled).
- 🆕 The first visible product: one screen listing every plugin in the feed, nine today (installed version or "not installed", available
  version, release notes from `notes_markdown` rendered with no remote content), read-only.
- 🆕 Phase 6 owns the "check now" control, whose text states that a new release can take up to ten minutes to appear
  (`feed::EDGE_MAX_AGE_SECS`). Phase 6 reads the running OS version through `Platform` and passes it to Phase 5's
  `version::resolve`, which compares it with the asset's `min_os` (`Availability::NeedsNewerOs`).

Measurable outcomes:
- Fixture trees in temp dirs (current bundles, legacy `0.8.0` bundles, a bundle whose top-level `Version` and class
  `Version` disagree on a value other than `0.8.0` (e.g. top `1.0.3`, class `1.0.4.0`: reported `1.0.4`, mismatch
  logged), a real `0.8.0` release whose class `Version` is `0.8.0.x`, classes that disagree with each other,
  duplicates in two roots, a corrupt moduleinfo, a single-file legacy `.vst3`) produce exactly the specified
  installed-state table.
- The fake registry / fake `pkgutil` drive the installer-record facts; no test reads the real registry or real folders.
- UI test (vitest) renders each state from fixtures.
- Detection is feed-driven (ROSTER ruled): bundle names, the AU component name, the Windows uninstall key and the
  macOS pkg identifier come from each entry's `install` block. A fixture feed with an invented plugin and a matching
  fixture bundle reports it installed; a v1-only entry is listed as "not available through the manager".
- PENDING-CI, `manager-ci` macOS leg, `cargo test -p krate-manager-core detect::`: the AU `Info.plist` reader and the
  `pkgutil` receipt parser (fixture plist and fixture `pkgutil --pkg-info` / `--files` output, run through the macOS
  `Platform` implementation) produce the specified table; the Linux leg runs the same detection cases over
  `~/.vst3`-shaped temp roots.

Does NOT: install, write anything outside the state dir, or call the helper.

### Phase 7: Download and Verify

**Status: ✅ COMPLETE (PENDING-CI: 11 item(s)) (2026-10-31)** — see specs/krate-manager-phase7/compliance.md

**Spec:** `krate-manager-phase7`

Components:
- 🆕 `core::download`: streamed HTTPS download into the staging dir, progress reported through a plain callback /
  `std::sync::mpsc::Sender<Progress>` that `core` tests observe directly (core stays Tauri-free, Repository Layout);
  `src-tauri` adapts it to a Tauri `Channel` for the webview (https://v2.tauri.app/develop/calling-rust/). Resume with
  a `Range` request (neither `plugin-http` nor `plugin-upload` documents resume, tauri_feasibility.md Q1), bounded
  retries.
- 🆕 `core::verify`: `Content-Length` vs feed `size`, streamed SHA-256 vs feed `sha256`, minisign signature vs the
  embedded public key, `tools/feed-contract/keys/krate-signing.pub` compiled in (Phase 2 FR-061; the `minisign-verify` crate the Tauri updater uses; https://docs.rs/minisign-verify; the spec
  confirms the current version; docs.rs shows 0.3.0 on 2026-10-29, whose `Signature::trusted_comment(&self) -> &str`
  exposes the trusted comment; research saw 0.2.5 and the updater's move to 0.3, self_update.md KQ1), and the
  trusted-comment binding: the comment must parse under the Phase 1 C3 grammar and its plugin, version, platform key
  and asset name must equal the feed entry. A verified artifact is marked so; nothing else is ever extracted or executed, and the helper only ever runs a verified installer asset.
- Test keys and signatures: throwaway keys made per test run in a temp dir with the `minisign` crate as a
  dev-dependency (0.10.0, `minisign::sign(pk, sk, data_reader, trusted_comment, untrusted_comment)`,
  https://docs.rs/minisign/latest/minisign/fn.sign.html), never committed.

Measurable outcomes:
- Against the fixture server with a throwaway key: a good artifact verifies; a one-byte tamper, a wrong size, a wrong
  hash, a signature by another key, and a valid signature bound to another plugin or version are each rejected with
  a named error, and neither the extraction entry point nor the install-driver entry point is called for them (call count 0).
- The Phase 1 rsign2 known-answer fixture (`tools/feed-contract/fixtures/`) verifies through `core::verify` and fails
  after a one-byte change to the file, the signature or the trusted comment; with Phase 2's rsign2 ↔ `tools/minisign.js`
  interop, the rsign2, Node and Rust paths agree.
- Progress: the callback receives monotonically increasing byte counts ending at the feed `size` (core test, no Tauri).
- An interrupted download resumes and the result verifies; a server that ignores `Range` falls back to a full download.
- Disk-full and permission errors in the staging dir surface as named errors and leave no half file marked verified.

Does NOT: extract, place files, or elevate.

### Phase 8: Safe Extraction and Staging (Linux tar.gz)

**Status: ✅ COMPLETE (PENDING-CI: 19 item(s)) (2026-11-01)** — see specs/krate-manager-phase8/compliance.md

**Spec:** `krate-manager-phase8`

Components:
- 🆕 `core::extract`: tar.gz extraction of a **verified** artifact (the Linux release asset; the manager extracts no zip and no Windows or macOS asset) into a fresh staging directory. Rejects
  absolute paths, `..` components, drive prefixes, entries that resolve outside the target, symlinks whose target
  escapes the bundle, hard links and device files; preserves in-bundle relative symlinks and exec bits. Applies no
  extended attributes (the manager never adds quarantine). The path checks (zip-slip class) apply to tar entries unchanged.
- 🆕 A staged-layout check: the C4 canonical listing (Phase 1) of the extracted tree has the feed entry's
  `payload.files` count and `payload.sha256` digest, and its root matches the C4 Linux layout (no extra, no
  missing file, no wrong mode or link). The manager needs no `release.json` for this.

Measurable outcomes:
- A corpus of crafted archives (zip-slip with `../`, absolute, Windows drive and UNC paths, escaping symlink, symlink
  chain, duplicate entries, oversized entry count) is rejected 100 %, and a scan of the parent temp dir after each
  attempt finds zero bytes written outside the staging dir.
- A real-shaped fixture bundle round-trips with symlinks and exec bits intact, and its listing digest equals the
  Phase 1 fixture's (unit test on Windows for the logic).
- PENDING-CI, `manager-ci` macOS and Linux legs, `cargo test -p krate-manager-core extract::`: the same fixtures
  extracted on a real POSIX filesystem keep exec bits (`mode & 0o111` per the listing) and in-bundle relative
  symlinks, the escaping-symlink and symlink-chain cases are rejected, and the zero-bytes-outside scan passes. On
  macOS the staging dir is under `std::env::temp_dir()` (`/var/folders/…`, where `/var` is a symlink to
  `/private/var`), so these cases also prove the under-target check canonicalizes both sides.

Does NOT: write into any plug-in or preset root.

### Phase 9: Linux Install, Update and Uninstall (unprivileged)

**Status: ✅ COMPLETE (PENDING-CI: 18 item(s)) (2026-11-01)** — see specs/krate-manager-phase9/compliance.md

**Spec:** `krate-manager-phase9`

Windows and macOS run the plugins' existing installers (Phase 10). Linux has no installer, so this phase is the manager
extracting the verified tar.gz itself and tracking what it placed.

Components:
- 🆕 `core::install`: the per-OS `InstallDriver` trait (install, update, uninstall, each returning a named result;
  the Windows and macOS drivers arrive in Phase 10) and its Linux implementation: an install plan from (staged tree
  from Phase 8, the Phase 4 settings model, `Roots`) to destinations: the plug-in bundle to `<settings root>/<P>.vst3`
  (default `~/.vst3`, no prompt) and the factory presets to `~/Documents/Krate Audio/<P>` plus `preset_subdir` from
  `version.json` when set (the layout the Linux README instructs, `plugins/membrum/installers/linux/README.txt`,
  "FACTORY PRESETS"). They appear as user presets in the browser.
- 🆕 **Journaled bundle swap.** `<P>.vst3` is a directory, and `rename` cannot replace a non-empty directory. Replacing it
  takes two renames, and a crash between them would leave neither. So per bundle: stage the new tree beside the target
  on the same volume (`<P>.vst3.krate-new`); write the intent (target, old-aside name, new name, step) to a journal in
  the state dir and flush it; rename old → `<P>.vst3.krate-old`; rename new → `<P>.vst3`; mark the journal done; delete
  old. A **recovery pass at every start** reads unfinished journal entries and either completes the swap (new absent,
  target present → finish the presets, manifest and cleanup) or rolls it back (new missing or incomplete → rename old back), then cleans up. The commit point is the rename of new into the target: a crash before it rolls back (new present, target missing → rename old back, or a first install ends absent); from it on the recovery pass completes the swap (Phase 9 spec D7, Q1). The
  Windows rename-aside path for mapped DLLs and the macOS `renamex_np` swap of the earlier draft are dropped: those
  platforms no longer place files themselves.
- 🆕 Install manifests in the state dir: every placed path with its hash (the only record of a Linux install). Update
  removes files the previous manifest had and the new payload lacks.
- 🆕 **Uninstall (Q8 ruled): one Remove action per plugin**, driven only by manifest paths that lie under configured
  roots. It removes the plug-in and its factory presets (everything the manifest placed) and nothing else; user presets
  in `~/Documents/Krate Audio` are never touched. A placed file whose current hash differs from the manifest (a factory
  preset the user edited) is left in place and reported. There is no "Reinstall previous version" in v1.
- 🆕 UI actions Install / Update / Remove per card, with progress, over the `InstallDriver` trait.

Measurable outcomes:
- In temp-dir roots: install → update → uninstall leaves exactly the specified tree at each step, factory presets
  under the user preset folder (with and without `preset_subdir`) and user presets beside them untouched.
- Crash safety: a crash injected after **each** journaled step (stage written, journal written, old renamed aside,
  new renamed in, journal marked done), followed by the recovery pass, leaves the target holding exactly the old or
  exactly the new bundle (compared by C4 listing digest), and no `.krate-old` / `.krate-new` leftovers after cleanup.
- A manifest entry pointing outside the configured roots is refused, and the test proves no file outside was touched.
- A user-edited placed preset survives Remove and is reported; an unplaced file in the preset folder survives Remove.
- The journal and manifest logic is OS-neutral and runs on Windows against temp dirs. PENDING-CI, `manager-ci` Linux
  leg, `cargo test -p krate-manager-core install::`: the same install / update / uninstall and crash cases under
  `~/.vst3`-shaped temp roots on a real POSIX filesystem.

Does NOT: run an installer or elevate (Phase 10), or self-update.

### Phase 10: The Elevated Helper and the Windows / macOS Install Drivers

**Spec:** `krate-manager-phase10`

**Status: ✅ COMPLETE (PENDING-CI: 1 item(s)) (2026-11-01)** — see specs/krate-manager-phase10/compliance.md

Components:
- 🆕 `krate-manager-helper`: reads one JSON job file, performs a **fixed operation set** sequentially, and writes a
  result file with one entry per operation (so one failed plugin never hides the others). No shell strings, no
  arbitrary commands, no arguments taken from the job; unknown ops and unknown fields rejected. The set (STRATEGY
  ruled); no later phase widens it:
  - `run_inno_installer { file, sha256 }` (Windows): runs the plugin's existing Inno `.exe` with the fixed arguments
    `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LOG=<helper-chosen path>`. `/NOCLOSEAPPLICATIONS`
    means the installer never closes a DAW; the exit code and log are mapped to a named result (installed, or locked:
    "close your DAW and retry").
  - `run_inno_uninstaller { app_id }` (Windows): `app_id` must match the Inno GUID pattern; the helper itself reads
    `UninstallString` from `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\<app_id>_is1` in the 64-bit view
    (per_os_install_mechanics.md lines 74, 101), requires the executable to be an `unins*.exe` in the plugin's install
    dir under the compiled-in Windows plug-in root, and runs it with the fixed arguments `/VERYSILENT
    /SUPPRESSMSGBOXES /NORESTART`, never a path or argument from the job; it then polls until the bundle folder is gone
    with a bounded timeout (the uninstaller returns before its files are removed, per_os_install_mechanics.md KQ2).
    A managed copy whose recorded `InstallLocation` lies outside the compiled-in Windows plug-in root is
    never uninstalled by the helper: the manager shows it as managed and tells the user to uninstall it from
    Windows Settings.
  - `run_pkg_installer { file, sha256 }` (macOS): runs `/usr/sbin/installer -pkg <copy> -target /`, nothing else.
  - `uninstall_pkg { pkg_id }` (macOS): `pkg_id` must match `^com\.krateaudio\.[a-z0-9]+$` (the default identifier
    `release.yml:321` builds; no plugin's `version.json` overrides it today). The helper reads the receipt's install
    location (`pkgutil --pkg-info`) and file list (`pkgutil --files`), validates **every** path (joined to the install
    location, canonicalized) against `AllowedRoots` before removing anything (one path outside refuses the whole op
    (a directory entry that is an allowed root, an ancestor of one, or `/Applications` is structural: accepted and
    never removed; a file entry never is, Phase 10 D16)),
    removes the files (symlinks as links, directories only when empty and never a root itself), then runs `/usr/sbin/pkgutil
    --forget <pkg_id>`.
  Linux has no elevated operation. There is no generic copy / rename / remove op.
- 🆕 **TOCTOU-safe installer handling.** The installer file sits in the user-writable staging dir, so the helper copies
  it into a directory only administrators can write (created by the helper), hashing **as it copies**, compares to the
  job's SHA-256 before anything runs, and runs the copy, never the staged original. There is no check-then-run window.
- 🆕 **Allowed roots: library parameter, binary constants.** The validation library takes `AllowedRoots` and
  `StagingRoot` as parameters and is tested with temp roots. The helper binary's `main` builds `AllowedRoots` only from
  compiled-in per-OS constants (Windows `C:\Program Files\Common Files\VST3\Krate Audio` and `C:\ProgramData\Krate Audio`,
  resolved through the known-folder API; macOS `/Library/Audio/Plug-Ins/VST3`, `/Library/Audio/Plug-Ins/Components`,
  `/Library/Application Support/Krate Audio`, and in `/Applications` only a `<Name> AUv3.app` entry the pkg installed)
  and derives `StagingRoot` from the job file's own canonical location (the job must sit in the invoking user's state
  dir, whose `staging/` is the only source root). Its root construction takes no input from the job file or the
  settings file, both of which the unelevated user can write.
- Every operation validates against those roots after canonicalizing both sides (installer source under
  `StagingRoot`, no symlink in the source path, removals under an allowed root).
- 🆕 Launchers behind one trait: Windows `ShellExecuteExW` verb `runas` (`ERROR_CANCELLED` = user refused,
  per_os_install_mechanics.md KQ3) and macOS `osascript … with administrator privileges`. The core batches every
  pending install, update and uninstall into one job: one prompt per batch. Linux has no launcher.
- 🆕 **Windows and macOS `InstallDriver` implementations** (the trait from Phase 9): build the batch job from verified
  downloads, run the installers sequentially, and map each result to a named outcome (installed, locked, failed, refused
  by the user). A plugin installed earlier by the old `.exe` / `.pkg` is upgraded in place by the newer installer (same
  Inno AppId, same pkg identifier), so the drivers need no migration step; uninstall uses the Inno uninstaller and the
  `pkgutil` route like any other plugin. After a macOS install the core runs `killall -9 AudioComponentRegistrar` (exit
  code ignored) through the injected command runner; every install ends with a "restart or rescan your DAW" notice, and
  the macOS notice adds Apple's AU rescan steps (report line 34).
- 🆕 **Measure the locked-file behaviour (measured 2026-11-01, Phase 10 spec M-1).** On a hosted Windows runner (job
  `inno-locked-file`, Manager CI run 41342540379, Inno Setup 6.7.1) a `/NOCLOSEAPPLICATIONS` update whose plug-in DLL
  is held open retries `DeleteFile` four times, defaults the suppressed Abort/Retry/Ignore box to Abort and exits 5
  after about 4 s: nothing is replaced (both files stay the old version), no restart is scheduled, it does not hang.
  The driver maps exactly exit 5 plus the line `DeleteFile: The existing file appears to be in use (5).` to the "close
  your DAW and retry" outcome; any other failure is "installer failed (exit N)".
- Residual risk, accepted by the user on 2026-11-01: the per-user helper can be replaced by a process running as the
  user before the prompt; no digest check and no admin-folder copy; Q5's `currentUser` install stands (Phase 10 D19).

Measurable outcomes:
- The helper's validation library, run **unelevated** against temp roots, rejects every crafted job (installer outside
  `StagingRoot`, `..`, symlinked source, hash mismatch, a file changed between hash and run, unknown op, extra fields,
  a field carrying arguments, paths or roots, a `roots` or `settings_roots` field naming `C:\Windows` or `/etc`, an
  `app_id` that is not a GUID, an uninstaller path outside the plug-in root in the fake registry, a `pkg_id` outside the
  pattern, a fake `pkgutil --files` listing with one `..` or `/etc` entry among valid ones: refused as a whole, nothing
  removed) and performs every valid one, through a fake process runner and fake registry that record the exact argv.
- Exact argv: the recorded Inno installer command is `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS
  /LOG=<path>`, the uninstaller `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`, the pkg command
  `installer -pkg <copy> -target /`; the run copy's hash equals the job's SHA-256.
- A batch of three plugins where the middle one reports the locked result still runs the third, and the result file
  names each outcome.
- Uninstall polling: with a fake uninstaller that returns at once while the bundle folder disappears later, the op waits
  and succeeds; if the folder never disappears it fails with a named timeout.
- The binary's root construction: a unit test proves `AllowedRoots` built by the binary equals the compiled-in
  constants whatever the job file and the settings file contain (both fixtures name extra roots), and that the job
  parser has no field that can add a root.
- Canonicalization under a symlinked temp root: a temp root reached through a symlinked parent (on macOS the native
  `/var` → `/private/var`; on Linux and Windows a test-made link) accepts a destination inside it and rejects one that
  escapes it through the link.
- The launcher is exercised only through a fake in tests; an opt-in manual test exists and never runs in a gate. No
  test, gate or agent runs the helper elevated or an installer **on the dev box**.
- User refusal at the prompt leaves the system untouched and the UI in a defined state.
- PENDING-CI, `manager-ci` job `helper-elevated (macos-latest | windows-latest)`, on ephemeral hosted runners only: the
  real helper binary runs elevated (`sudo` on macOS; on Windows from the runner's administrator session, whose
  elevation state the spec confirms against the hosted image's documentation) on jobs whose paths are temp roots passed
  in through a test-only build of the binary (`--features ci-temp-roots`, a feature that `release-manager.yml` builds
  never enable and a `manager-ci` step checks is absent from the release build's `cargo tree -e features`), and asserts
  the result file and the placed tree. The macOS leg builds a fixture pkg with `pkgbuild --identifier
  com.krateaudio.fixture` installing into a temp dir, runs `run_pkg_installer` through the helper, asserts the files
  are placed, runs `uninstall_pkg com.krateaudio.fixture` and asserts the files are gone and `pkgutil --pkgs` no longer
  lists it; it also runs the symlinked-root canonicalization case elevated. The Windows leg compiles a fixture Inno
  script, runs `run_inno_installer` on it and `run_inno_uninstaller` against its uninstall key under a temp root, and
  asserts the argv a stub recorded. The osascript and UAC launchers themselves have no automated evidence; Phase 12's
  user-performed checks cover them.
- PENDING-CI, `manager-ci` job `inno-locked-file (windows-latest)`: install the fixture Inno setup once, map one of its
  DLLs into a process (`LoadLibraryW` on a fixture `cdylib`), run the same installer again with `/VERYSILENT
  /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LOG`, and record exit code, log, file state and run time; the
  result is written into the phase spec and asserted by the driver's mapping test (the fake runner replays the recorded
  exit code and log).

Does NOT: write the registry itself, install a persistent service, run anything outside the fixed op set, or touch
Linux (no elevation).

### Phase 11: Self-Update and the Manager's Release Pipeline

**Spec:** `krate-manager-phase11`

**Status: ✅ COMPLETE (PENDING-CI: 17 item(s)) (2026-11-02)** — see specs/krate-manager-phase11/compliance.md

Components:
- 🆕 `tauri-plugin-updater` ≥ 2.14.1 with `requireSignedVersion: true`, `allowDowngrades` false, `createUpdaterArtifacts:
  true`, Windows `passive` (self_update.md KQ7); `relaunch()` after install on macOS/Linux.
- 🆕 `.github/workflows/release-manager.yml`, **manual dispatch only** (Q1 ruled): reads `apps/krate-manager/version.json`
  (first public version 1.0.0), re-runs `tools/lint-manager-version.js` and refuses to run if `tauri.conf.json` /
  `Cargo.toml` disagree, if `apps/krate-manager/CHANGELOG.md` has no entry for that version, or if the tag exists;
  tag `manager/v<ver>`; `bundle.js --release` builds each leg (no `tauri-action`: 1.0.0 writes `api.github.com`
  asset URLs and dropped the `tagName` rewrite, Phase 11 D4); `gh release upload`; `tools/gen-manager-latest.js`
  writes `latest.json` with per-tag github.com URLs and the three darwin keys; the signing secret
  `TAURI_SIGNING_PRIVATE_KEY` with its password `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` (both environment secrets of the
  main-only `signing` environment, Phase 2 FR-061), the updater `pubkey` taken from
  `tools/feed-contract/keys/krate-signing.pub`, then `feed.yml` to publish `manager/latest.json` and the `manager` block.
- **One universal macOS manager build, Intel Macs included (Q6 ruled: macOS 15 and later, `minimumSystemVersion`
  15.0).** The macOS leg runs `macos-latest` with
  `bundle.js --release --target universal-apple-darwin` (both rustup targets installed), and
  `tools/gen-manager-latest.js` writes the three darwin keys `darwin-aarch64`, `darwin-x86_64` and
  `darwin-universal` for the one universal archive (Phase 11 D4, D6: no `tauri-action`, see the bullet above).
  tauri_feasibility.md line 178 notes that tauri-action's
  documented matrix instead builds aarch64 and x86_64 as two jobs; one universal job is chosen because it costs one
  macOS runner slot instead of two and yields a single `.app` for the pkg below. The manager runs on Intel Macs
  whatever Phase 2's `lipo -archs` finds for the plugins: if the plug-in binaries turn out arm64-only, the feed has no
  `darwin-x86_64` / `darwin-universal` plug-in entries, and the manager on an Intel Mac shows those plugins as "not
  available for this Mac" (Phase 5's platform-key resolution) while still updating itself. Whether the plugins
  should build universal is a plugin-build question outside this roadmap.
  The release build stages a universal `krate-manager-helper` (`lipo -create` of the aarch64 and x86_64 helper
  builds) under the sidecar name the universal target needs, merges `src-tauri/tauri.helper.conf.json` (Phase 10
  D12), and the PENDING-CI `release-manager.yml` macOS row asserts `lipo -archs` on
  `Contents/MacOS/krate-manager-helper` lists `x86_64 arm64`.
- 🆕 **The manager's first-install packaging (Q5 ruled), built here:** Windows Tauri NSIS `installMode: currentUser`
  (self-update never prompts); macOS an unsigned `.pkg` wrapping the ad-hoc-signed universal `.app`, made with `pkgbuild`
  like the plugins' pkgs (`release.yml:331-337`; Tauri's `BundleType` has no pkg, only `deb, rpm, appimage, msi, nsis,
  app, dmg`, https://v2.tauri.app/reference/config/), installing to `/Applications`, plus a documented `curl` Terminal
  one-liner beside it (provisional until Phase 12's Open Anyway GUI check); Linux AppImage + `.deb` built inside an
  `ubuntu:22.04` container on the Ubuntu leg (report line 65 on the 22.04 runner's retirement). The Linux floor is that
  image's glibc (Q6), recorded with `objdump -T` on the built binary.
- Updater endpoint: one endpoint, `https://krateaudio.com/manager/latest.json`; no stable second host (D5).
- 🆕 End-to-end self-replace gate (the one probe item never tested, probes line 19): CI builds two versions with a
  throwaway key, serves `latest.json` from a local server, runs the old build's updater headless, and asserts the new
  version launches, `codesign --verify --deep --strict` passes and no quarantine xattr exists (macOS); the Windows and
  Linux legs assert the version change.

Measurable outcomes:
- A tampered update, an update signed by another key, and a downgrade (older validly signed version announced as new)
  are each refused (local test with the fixture server). The local test calls only `check()` and `Update::download` /
  `Update::verify` (tauri-plugin-updater 2.14.1: `download` "downloads the updater package and verifies it",
  `verify` "makes no request and installs nothing", https://docs.rs/tauri-plugin-updater/latest/tauri_plugin_updater/struct.Update.html).
  The manager reaches `install` / `download_and_install` only through its own `UpdateInstaller` trait, whose test
  implementation panics if called, so no local run can launch the NSIS installer (`/UPDATE /P`,
  tauri_feasibility.md line 83) or replace a bundle on the dev box. The successful download → install → relaunch
  path runs only in the e2e gate on ephemeral runners.
- The e2e gate is green on all three legs (PENDING-CI, `release-manager.yml` job `updater-e2e`, named per leg).
- PENDING-CI, `release-manager.yml` macOS leg: `lipo -archs` on the built binary lists `x86_64 arm64`, and `lipo -archs`
  on `Contents/MacOS/krate-manager-helper` lists `x86_64 arm64`, and the
  generated `latest.json` carries `darwin-aarch64`, `darwin-x86_64` and `darwin-universal` entries, each with a
  per-tag URL (no `/releases/latest/`).
- PENDING-CI, first-install artifacts on fresh hosted runners (never the dev box): the macOS leg installs the manager
  pkg with `sudo installer -pkg` from a copy given `com.apple.quarantine` (as a browser would), then asserts
  `xattr -l` on the installed `.app` shows no `com.apple.quarantine` and the app launches headless (the probes'
  `sudo installer` result, probes line 14, as a regression gate for the manager's own pkg); the Ubuntu leg installs
  the `.deb` with `apt-get install ./…deb` and launches the app and the AppImage under `xvfb-run`; the Windows leg
  runs the NSIS setup silently (`/S`) and asserts the per-user install path and a launch.
- PENDING-CI, `release-manager.yml` dispatched in `dry_run` with a `version.json` that disagrees with
  `tauri.conf.json`, with an existing tag, and with no CHANGELOG entry: each fails before any build; the Linux binary's
  highest required `GLIBC_` symbol version (`objdump -T`) is at most what the `ubuntu:22.04` image provides.

Does NOT: publish a public manager release (that is the user's dispatch after Phase 12).

### Phase 12: Product Completion and First Release Readiness

**Spec:** `krate-manager-phase12`

Components:
- 🆕 "Update all" and the download queue (Q7 ruled): one confirmation listing every update, then one elevation prompt
  per batch (Windows, macOS); bounded parallel downloads, installers run sequentially. The manager never closes a DAW:
  the Windows installers run with `/NOCLOSEAPPLICATIONS`, and a plug-in whose files are locked fails cleanly with a
  "close your DAW and retry" message while the rest of the batch continues. No DAW process detection. Every install ends
  with a "restart or rescan your DAW" notice (macOS adds Apple's AU rescan steps).
- 🆕 The settings **screen** over the Phase 4 settings model (the Linux plug-in root), and the Linux
  NVIDIA/WebKitGTK workaround as a user toggle, never shipped unconditionally (report line 22;
  https://v2.tauri.app/develop/debug/linux-graphics/). The screen refuses to change the Linux plug-in root while any install manifest exists; the user removes those plugins first (Phase 9 FR-064, Q3).
- 🆕 Remove for a plugin that has an install manifest but is no longer in the feed (an orphan manifest), driven by the manifest alone (Phase 9 Q8).
- 🆕 **User-performed checks** (the user runs them on real machines; no agent can, and none runs in a gate), each
  recorded in `specs/krate-manager-phase12/` with date, OS build and screenshots or terminal output:
  - macOS GUI install, on a Mac running Sequoia 15.x and one running Tahoe 26.x, after 1.0.0 is public (D17 R-POST-4,
    Phase 12 spec revision 2026-11-03: no Mac before release; so is the macOS launcher check below, R-POST-5): download
    the manager pkg with a browser, double-click it, record whether Installer.app refuses it and whether Privacy & Security then offers
    "Open Anyway"; complete the install that way if offered, then run `xattr -l` on the installed `.app` and record
    whether it carries `com.apple.quarantine` and whether it launches. Neither fact is measured yet: the probes
    tested only `sudo installer -pkg` (probes line 14), the Open Anyway UI "could not be checked headless" (probes
    line 18), no Apple source documents Installer.app's payload quarantine (unsigned_distribution.md line 77), and
    whether Tahoe offers Open Anyway for an unsigned pkg is unverified (unsigned_distribution.md line 78).
  - The real elevation launchers once each (the UAC prompt on Windows, the osascript admin prompt on macOS), each
    installing one plugin through the manager (the real Inno `.exe` / the real `.pkg`) and recording the prompt text and
    the result; on Windows additionally with the plug-in loaded in a DAW, recording the "close your DAW and retry"
    path; and once uninstalling the plugin again (Inno uninstaller / `pkgutil` removal). Linux has no elevation.
  - Linux: install and Remove one plugin through the real manager, recording that no prompt appears and the placed
    paths (`~/.vst3/<P>.vst3`, `~/Documents/Krate Audio/<P>`) (Phase 12 spec FR-074 (f)).
  - the self-update of a pkg-installed manager on macOS: record the admin prompt and that the swapped bundle is then
    user-owned (Phase 11 D11), recorded after the first self-update between two public releases (D17 R-POST-2).
- 🆕 First-launch documentation as a deliverable: Windows SmartScreen "More info → Run anyway" and Smart App Control
  unsupported; macOS first launch per Q5, **written from the recorded macOS GUI check above**; Linux format notes and
  "add `~/.vst3` to REAPER's path list" (REAPER Linux default unconfirmed, per_os_install_mechanics.md KQ6). Published
  on the landing page / README. The landing step, the 'Generate Root Index' step of `release.yml` and the root-index step of `docs.yml` (`tools/gen-root-index.js`), also run by `release-manager.yml` job `landing`, gains a Krate Manager card (Q5 ruled)
  linking the latest manager release per OS and carrying this text; the landing page is rendered by `tools/gen-root-index.js` for `release.yml`, `docs.yml` and `release-manager.yml` (Phase 12 Q2 ruled), the plugin cards unchanged byte-for-byte, and the per-plugin
  installers stay listed beside it. It states the supported Windows floor, Windows 10 22H2 and Windows 11 x64 (Q6),
  which no `tauri.conf.json` key can carry (Phase 4 spec Non-goals). A redirected `%LOCALAPPDATA%` is unsupported on
  Windows: every elevated batch is refused with `E_HELPER_JOB_LOCATION` (Phase 10 D17).
- 🆕 Release readiness for manager 1.0.0 (from `apps/krate-manager/version.json`): every Part B gate green on Windows
  and every PENDING-CI row closed by a named green `manager-ci` / `release-manager` run, except the post-release rows listed in specs/krate-manager-phase12/spec.md D17 (Phase 11 SC-014's `publish` half and SC-020, Phase 12 R-POST-1 to R-POST-5), which the first real dispatch and the release after it close, and the Phase 2 backfill run
  live, so the live feed offers all nine plugins with `platforms` (no plugin re-release).
- 🆕 Before every manager release, check the RustSec advisory database (https://rustsec.org/) for `rquickjs`, `rquickjs-core` and `rquickjs-sys`, the vendored QuickJS-ng PAC engine that Phase 5 pins for the macOS and Linux builds (R-15); an advisory is answered with a pin bump before the release.

Measurable outcomes:
- "Update all" over N outdated plugins needing system roots (Windows, macOS) shows one confirmation listing every update
  and raises exactly one elevation prompt (fake launcher counts); downloads run in parallel up to a bound while installs
  run one at a time (the fake driver records zero overlap); a plugin whose fake install returns the locked result shows
  the "close your DAW and retry" message while the remaining plugins still install; no code path enumerates running
  processes; every install ends with the "restart or rescan your DAW" notice, and the macOS notice includes Apple's AU
  rescan steps.
- Every user-visible string about first launch exists in the docs and matches the probe-measured behaviour; every
  macOS first-launch string matches the recorded result of the user-performed GUI check on both Sequoia and Tahoe,
  and this row stays open until that record exists. If the check shows the pkg route fails (no Open Anyway, or a
  quarantined payload), Q5 is re-ruled before the docs are written.
- The user-performed launcher checks are recorded for all three OSes.

Does NOT: add features beyond v1 scope.

---

## Dependency Graph

```
Phase 1  ✅ contracts C1-C4 (Linux tar layout), gen-feed.js, KAT fixture, feed-tools.yml
  │
  ├──► Phase 2✅SHA256SUMS, signatures, release.json, dry_run, backfill-sign ──► Phase 3  feed.yml (+ dry_run) + Pages ─┐
  │                                                                                                                      │
  │    Phase 4  scaffold, Platform/Roots, settings model, version.json + lint, manager-ci (precedes every Part B phase)  │
  │       │                                                                                                              │
  ├──────►├──► Phase 5  feed client + version model                                                                      │
  │       │       ├────────────► Phase 6  detection + status screen ─────────┐                                           │
  │       │       ▼                                                          │                                           │
  └──────►└──► Phase 7  download + verify ──► Phase 8  Linux extraction ──► Phase 9  Linux install/update/uninstall      │
                  │                           (C4 from Phase 1)              │  (InstallDriver trait, manifests)          │
                  │                                                          ▼                                           │
                  ├─────────────────────────────────────────► Phase 10  elevated helper + Windows/macOS drivers ◄─ Phase 6
                  │                                                          │                                           │
                  └──────────────► Phase 11  self-update, release-manager.yml, packaging ◄───────────────────────────────┘
                                                                             │
                                                                             ▼
                                   Phase 12  Update all, settings UI, user-performed checks, docs, readiness
                                             (needs Phases 10 and 11)
```

Edges, as a list: 1→2, 1→3, 2→3, 1→5, 1→7, 1→8; 4→5, 4→6, 4→7, 4→8, 4→9, 4→10, 4→11; 5→6, 5→7; 6→9, 6→10; 7→8,
7→10, 7→11; 8→9; 9→10; 3→11; 10→12, 11→12.

**Parallelism.** Phase 1 owns every contract Part B builds or tests against (the versions.json v2 schema with the
Linux `payload` digest, the release.json schema, the trusted-comment grammar and its known-answer fixture, the Linux
archive layout and canonical listing), and its fixtures are what Part B tests read. So once Phase 1 is done,
Phases 2-3 and Phases 4-10 run in parallel: no Part B phase needs Phase 2 or 3 output except the opt-in live test.
Phase 11 needs Phase 3 (the feed carries `manager/latest.json`), Phase 4 (the app) and Phase 7 (the shared
verification code and key). The first public manager release needs Phase 12 and the Phase 2 backfill run (Q10): no
plugin re-release is needed.

## Not in v1 (research suggestions deferred, with reason)

- **Node.js CLI over the same feed** (report line 22; framework_comparison.md ranking): useful, but outside the five v1
  jobs; Part A's `gen-feed.js` keeps the schema in Node so a CLI can follow.
- **"Install another version" / rollback** (report line 69): a sixth job; ruled out of v1 (Q8).
- **winget / Homebrew / Flatpak distribution**: winget is possible for unsigned exes (unsigned_distribution.md KQ2c) but
  is extra channel work; Homebrew disables Gatekeeper-failing casks from September 2026 (report line 40); a Flatpak
  manager cannot write `/usr` and is invisible to Flatpak DAWs (report line 34).
- **In-app Sigstore verification**: adds a trusted-root refresh for a check minisign already gives (report line 63).
- **Pointing the in-editor update banner at the manager**: a plugin-side change; the banner stays as it is.
- **Per-user installs on Windows and macOS**: the installers decide the paths (Q3).
- **A prerelease (`beta`) channel in the manager**: the feed carries the sibling, the manager ignores it in v1 (Q9).
- **DAW process detection**: the manager never closes or detects a DAW (Q7).
- **Removing shadowing or stale copies elsewhere on disk**: Phase 6 reports every copy found, but an elevated removal of
  an arbitrary path is outside the helper's fixed op set.
- **Windows on Arm, AAX, CLAP**: no such builds exist (`release.yml` ships `-Windows-x64` only).

## Cross-Cutting Constraints (apply to every spec)

- **Never touch the real system.** This dev box has the user's real plugins installed (including per-user dev copies
  under `%LOCALAPPDATA%\Programs\Common\VST3`). Every filesystem root (plug-in folders, preset folders, the state
  dir), the feed URL and the elevation mechanism is injected; tests use temp dirs and a local fixture HTTP server. No
  test, gate or agent writes to a real VST3 / Components / preset folder, ProgramData or the registry, or runs the
  elevated helper or an installer for real. No test calls github.com or api.github.com; an opt-in live-network test is
  ignored by default and never runs in a gate. The only exceptions are named CI jobs on **ephemeral GitHub-hosted
  runners** that the user triggers (Phase 10 `helper-elevated` and `inno-locked-file`, Phase 11 `updater-e2e` and the first-install
  artifact checks): each runs on a throwaway VM, never on this box, and the helper job writes only to temp roots.
- **Security is designed in.** Every downloaded artifact is verified (size, SHA-256, minisign signature with the
  plugin/version/platform binding) before it is extracted or executed. Extraction rejects absolute paths, `..`, and
  links escaping the target. The helper accepts only its fixed, validated operation set: no shell strings from the UI,
  no paths it has not checked against allowed roots, re-hashed sources. The webview gets the narrowest capability set
  and no remote content. Feed strings never become filesystem paths unvalidated. No private key is generated into or
  committed to the repo: tests make a throwaway key in a temp dir; the real key is a CI secret the user creates.
- **Never close a user's DAW (Q7).** Any batch of installs, updates or removals asks for one confirmation listing every
  item and raises one elevation prompt per batch, never one per plugin. No code path closes or kills another process:
  Windows installers always get `/NOCLOSEAPPLICATIONS`, and there is no DAW process detection. A plug-in whose files
  are locked fails on its own with a "close your DAW and retry" message while the rest of the batch continues. Every
  install ends with a "restart or rescan your DAW" notice; on macOS it adds Apple's AU rescan steps.
- **Unsigned reality.** No step may require a certificate. macOS builds are ad-hoc signed (`"signingIdentity": "-"`)
  in CI. The manager never adds quarantine to what it writes, and its `Info.plist` never gains
  `LSFileQuarantineEnabled`. First-launch instructions are a deliverable (Phase 12), not an afterthought.
- **Cross-platform.** Every OS-specific path goes through the one `Platform` abstraction with a Windows, macOS and
  Linux implementation, each with tests. Windows-green proves nothing for the other two: macOS and Linux are proven by
  `manager-ci.yml` (and the release workflows) on hosted runners, which only the user triggers. A row that needs macOS
  or Linux evidence closes as **PENDING-CI** with the exact job and assertion, never as a local pass.
- **Zero warnings.** `cargo clippy --all-targets -- -D warnings`, `cargo fmt --check`, `tsc --noEmit` under `strict`,
  and the linter Phase 4 chooses. No `#[allow]` or `// @ts-ignore` without a one-line reason the reviewer accepts.
- **Parallel tests.** `cargo test` with its default threads per test binary, vitest with parallel files, `node --test`
  for tools; never `--test-threads=1` or `--no-file-parallelism` except a documented timing case. Any C++ change goes
  through `node tools/run-gate.js` and `node tools/header-consumers.js` like every phase in this repo.
- **The feed is the roster (ROSTER).** The manager ships no list of plugins. Every per-plugin fact it uses (name,
  versions, assets, bundle names, uninstall key, pkg identifier, preset subfolder) comes from that plugin's entry in
  `versions.json`, so adding a plugin to the repo never needs a manager release.
- **Additive until ruled otherwise.** The in-editor update checker and the per-plugin installers keep working at every
  phase boundary; the v1 feed keys keep their path, type and meaning.
- **Naming.** Rust per rustfmt/clippy conventions; TypeScript per the Phase 4 lint config; Node tools follow
  `tools/*.js` (flat files, `#!/usr/bin/env node`, a usage line, `--self-test` or `node --test`).
- **Citations.** Every claim about existing code cites a file:line read in that session; every claim about a crate,
  npm package or Tauri API cites its current docs page or source with the version. No invented APIs.

## Open Questions

None. All ten questions, and the install-strategy reversal, were answered by the user on 2026-10-29; the answers are in
the Decisions section and in the phases above.

## Review notes

Challenge review, 2026-10-29: all 21 issues applied (1 blocker, 12 major, 8 minor); none rejected. Where the
resolution differs from the suggested one, the reason:

- **Blocker (contracts in Phase 1), `payload`:** the feed carries `payload: {files, sha256}` (count + digest of the
  C4 canonical listing), not the full file list or a `manifest_url`. Factory-preset trees run to hundreds of files per
  plugin, and the full list for nine plugins × three platforms would bloat the file nine shipped editors poll. The
  digest binds the feed to the exact list as tightly as the list itself; the manager rebuilds the list from the
  verified archive, so it needs no extra download and no second signed file. The full list stays in `release.json`.
- **Blocker, known-answer fixture:** the rsign2 KAT fixture moved to Phase 1 (not Phase 2) so Phase 7 depends on
  Phase 1 only and the parallelism sentence holds. Phase 2 keeps the CI signing ruling and `tools/minisign.js`.
- **Archive layout (C4)** (superseded 2026-10-29: the Windows and macOS archives are dropped, only the Linux tar layout remains): macOS uses `ditto -c -k` without `--keepParent`, departing from the report's command line,
  because the source is the C4 root directory rather than a bundle; `--keepParent` would add a wrapping folder that
  the Windows and Linux archives (and today's Linux tarball, `release.yml:402-403`) do not have.
- **Helper allowed roots:** stricter than suggested. The shipped binary trusts compiled-in roots only and no
  settings-named root at all, even one under a system folder; a custom VST3 root is never an elevated target. The
  elevated CI job reaches temp roots through a test-only cargo feature that release builds are checked not to
  enable.
- **Elevated evidence (macOS/Linux rows):** the hosted-runner job runs the real helper binary under `sudo` /
  an admin session, but the osascript and UAC *launchers* (the earlier draft also had `pkexec`) cannot be driven headless; they move to Phase 12's
  user-performed checks rather than claim automated evidence.
- **"Never touch the real system" vs hosted-runner jobs:** the constraint is about this dev box. The Cross-Cutting
  section now names the three CI jobs that install or elevate on ephemeral GitHub-hosted VMs (the e2e updater gate
  was already in the draft); none runs on this box or in a local gate.
- **Intel Macs:** ruled supported through one universal build (report line 65), independent of whether the plugins
  turn out universal in Phase 2; the alternative (declare Intel unsupported) was not taken because the universal
  build costs one runner slot and the manager still self-updates there. Question scan: the OS floor and Intel support
  are the user's call, so this ruling is now Q6's recommendation; the former versioning question merged into Q1.
- **Settings model:** moved to Phase 4 (not Phase 9), next to `Roots`, because Phase 9 consumes it and the Phase 12 settings screen edits it.
