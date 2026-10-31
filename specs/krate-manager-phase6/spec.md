# Feature Specification: Krate Manager Phase 6 — Detection and the Status Screen

**Spec slug:** `krate-manager-phase6`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 6 (lines 557-595); Decisions log STRATEGY (line 23: "Phase 6 still
reports duplicates"), Q4 (line 27: no legacy state), ROSTER (line 33); Key decision 6 (lines 182-188: detection reads
files, never loads code; the class `Version` is the source); Positioning (line 40: one window listing every plugin in the
feed); Repository Layout (line 228: `tests/fixtures/`); Not in v1 (lines 971-972: removing shadowing copies is out);
Cross-Cutting Constraints (lines 975-1016)
**Depends on:** Phase 4 (`Roots`, `RootKind`, `Settings`, `AppContext`, `SharedState`, `test_support`, the capability
file, the ESLint markup-sink ban, `manager-ci.yml`; edge 4→6, roadmap line 947) and Phase 5 (`feed::check`, the typed
model with `InstallFacts` / `PlainName`, `version::reduce_class_version`, `compare`, `resolve_native`, `OsVersion`,
`EDGE_MAX_AGE_SECS`, the fixture server, the capture logger; edge 5→6).
**Consumed by:** Phase 9 (the Linux install manifest location and the three keys Phase 6 reads, FR-028; the managed
location rule, FR-001), Phase 10 (the Windows and macOS installer-record facts, the `Platform` host methods, the
`HostIo` and `HostFacts` seams, FR-025 to FR-034; edge 6→10), Phase 12 ("Update all" over the status table).
**Status:** DRAFT, revision 3 (review round and the 2026-10-31 clarification session applied, Review notes and
Clarifications): specification only, no implementation
**Date:** 2026-10-31 · verified against `feat/krate-manager` at `3efe70de` ("feat(krate-manager): Phase 5 feed client,
HTTP transport and proxy support", `git log --oneline -1`, clean tree); `specs/krate-manager-phase6/` did not exist
before this file.

---

## Overview

Phase 6 is the first visible product. `core::detect` looks for every plugin the feed names at a fixed, per-OS list of
candidate paths built from that plugin's own `install` block (no plugin name is shipped, ROSTER) and by a bounded listing
of the injected VST3 roots that matches only that plugin's bundle name (nested copies, D2), reads each copy's
`Contents/Resources/moduleinfo.json` (JSON with the trailing commas the VST3 SDK writer emits) and takes its version from
the **class** `Version` entries reduced to three parts, falling back to the top-level `Version` only when no class
carries one; the top-level value is otherwise a logged consistency check (`warn!`, or `debug!` for the known `0.8.0`
stamp). On macOS it also reads each AU `.component`'s `Info.plist`. Beside the files it reads, read-only, the
installer's own record: the Inno uninstall key (64-bit HKLM view) on Windows, the `pkgutil` receipt on macOS, the
manager's install manifest on Linux. The OS-touching readers (registry, `pkgutil`, the running OS version) sit behind
`Platform`: each OS implementation (`platform/windows.rs`, `macos.rs`, `linux.rs`) names its registry path or its
`pkgutil` / `sw_vers` / `getconf` argv and maps the raw answer through pure core functions, and the raw syscalls and
process spawns are one small `HostIo` implementation in `src-tauri`. So every mapping is tested with a fake `HostIo` on
its own OS leg, and detection itself takes a `HostFacts` seam, so the cross-OS tables run on every host with a fake.
`core::status` combines the Phase 5 feed check, the
detection, the running OS version and Phase 5's platform resolution into one serialisable view, and `core::notes` turns
each release's `notes_markdown` into a small typed block tree. Three new Tauri commands: `plugin_status` and `check_now`,
return that view, and `open_release_page` opens a core-validated release page (D15); the vanilla-TypeScript window renders one card per feed plugin (installed version or "Not installed",
latest version, state, every copy found, collapsed release notes, an "Open release page" button) and a "Check now" button whose text says a new release can take
up to ten minutes to appear. Nothing is installed, written outside the state dir, or elevated. The only way out of the
window is the release-page button, which opens one allow-listed https page in the system browser.

---

## Scope

**In scope (roadmap Phase 6 components, lines 561-577):**

1. `core::detect`: scan the injected plug-in roots (the Steinberg user and global VST3 folders per OS, the Windows
   `Krate Audio\<P>` subfolder, macOS `Components`), read `moduleinfo.json` with trailing commas tolerated, take the
   installed version from the class `Version`, log top-level disagreements, fall back to the top-level value when no
   class carries one, read the AU `Info.plist` on macOS, and report every copy found at the candidate paths and in the
   bounded listing of the injected VST3 roots (lines 562-570; D1 to D6; the listing is D2, ruled Q3).
2. Installer-record facts, read only: the Inno uninstall key on Windows, the `pkgutil` receipt on macOS, the install
   manifest on Linux, behind the `Platform` abstraction (per-OS host methods over an injected `HostIo`) with fakes in
   tests; no "legacy" state (line 571-572, Q4; D7).
3. The status screen: one card per feed plugin with installed version or "not installed", available version and the
   release notes rendered from `notes_markdown` (collapsed per card, no remote content) and one "Open release page"
   button; read-only otherwise (lines 573-574; D9, D10, D15).
4. The "check now" control and its ten-minute text (`feed::EDGE_MAX_AGE_SECS`); the running OS version read through the
   platform abstraction and passed to Phase 5's `version::resolve` (lines 575-577; D8).

**Non-goals (owned elsewhere):**

| Not in Phase 6 | Owner |
|---|---|
| Install / Update / Remove buttons and any action that changes a plug-in, preset or receipt | Phase 9 (Linux, roadmap lines 685), Phase 10 (Windows, macOS) |
| Writing the Linux install manifest; any field of it beyond the three Phase 6 reads (FR-028) | Phase 9 (roadmap lines 679-680) |
| Removing shadowing or stale copies | Not in v1 (roadmap lines 971-972); Phase 6 only reports them |
| Downloading, verifying or extracting anything | Phases 7, 8 |
| Running the helper, an installer, `pkgutil --forget`, or anything elevated | Phase 10 |
| "Update all", the download queue, the settings screen, the post-install notice | Phase 12 |
| Any link in the window, or opening anything other than a card's release page | Phase 6 renders no link (D9, FR-075); the one navigation path is the release-page button (D15, FR-064) |
| A beta channel; showing the `prerelease` sibling | Not in v1 (Q9, roadmap line 969); Phase 5 leaves it out of the model |
| A background poll or timer | Not named; the manager is resident-free and checks when the window opens and on "Check now" (D13) |
| Plugin-side changes (an AU `Info.plist` version, a bundle fix) | Ruled out by the user (Q6): no follow-up is recorded; Phase 6 reads what the bundle carries (D6) |
| A cache-first read command, or parallel per-plugin detection | Ruled out by the user (Q4): one `plugin_status` call, detection sequential (FR-040, FR-070, FR-084) |
| Any change to `plugins/`, `dsp/`, `cmake/`, `extern/`, `tools/feed-contract/`, `release.yml`, `feed.yml`, `ci.yml` | Never in this phase (FR-090) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 6 does with it |
|---|---|---|
| `specs/Krate-manager-roadmap.md:557-595` | Phase 6 components, measurable outcomes, "Does NOT" | The source of every FR / SC (Traceability) |
| `apps/krate-manager/crates/core/src/platform/mod.rs:9`, `:12-16`, `:55-58`, `:60-71` | `APP_IDENTIFIER`; `enum Os`; `trait Platform { os(); default_roots(env) }`; `NativePlatform` per `cfg` (each per-OS module is compiled on its own OS only, `:60-71`) | ✅ `Os` keys the pure candidate table (FR-001). 🔶 Gains `pub const VENDOR_DIR: &str = "Krate Audio"` (the literal is already on the platform allow-list, `crates/core/tests/injection.rs:43-71`), the `HostIo` seam and its result types (FR-031), and three `trait Platform` methods, `os_version`, `inno_values`, `pkg_outputs`, each taking `&dyn HostIo` (FR-030; D7) |
| `crates/core/src/platform/windows.rs:21`, `:33-42` | `impl Platform for WindowsPlatform`; Windows plug-in roots: `Vst3User` `%LOCALAPPDATA%\Programs\Common\VST3` (`:36`), `Vst3Global` `%CommonProgramW6432%\VST3` (`:40`) | ✅ The two Windows VST3 roots detection scans. 🔶 Implements the three host methods (FR-030) |
| `crates/core/src/platform/macos.rs:14`, `:23-40` | `impl Platform for MacosPlatform`; macOS roots `Vst3User`, `Vst3Global` `/Library/Audio/Plug-Ins/VST3`, `AuUser`, `AuGlobal` `/Library/Audio/Plug-Ins/Components` | ✅ The four macOS roots. 🔶 Implements the three host methods (FR-030) |
| `crates/core/src/platform/linux.rs:15`, `:33-42` | `impl Platform for LinuxPlatform`; Linux `Vst3User` `~/.vst3` and four `Vst3Global` roots (`/usr/lib64/vst3`, `/usr/lib/vst3`, `/usr/local/lib64/vst3`, `/usr/local/lib/vst3`) | ✅ The Linux roots. 🔶 Implements the three host methods (FR-030) |
| `crates/core/src/roots.rs:10-16`, `:118-162`, `:136-144`, `:145`, `:173-175`, `:196-202` | `RootKind::{Configured, Vst3User, Vst3Global, AuUser, AuGlobal}`; `with_configured_plugin_root` puts a `Configured` root first, panics on a sandboxed `Roots` when the root is not a strict descendant of the sandbox base (`:136-144`) and skips the insert when the path equals an existing root (`:145`); `plugin_roots()`; `vst3_user()` | ✅ Read only; detection takes `ctx.roots.plugin_roots()`. 🔶 Gains `pub(crate) fn check_sandboxed(&self, path: &Path)` (FR-005) |
| `crates/core/src/settings.rs:21-33` | `Settings { linux_plugin_root, linux_graphics_workaround }`, `linux_plugin_root_or_default(&Roots)` (`:28-32`: the stored path, else `vst3_user()`) | ✅ The Linux managed root (FR-001) |
| `crates/core/src/context.rs:50-56`, `:66`, `:124-132`, `:142` | `AppContext { roots, settings, feed_url, http }` (`Clone`); `SharedState = RwLock<AppState>`; `fold` adds the configured root on Linux only; `pub(crate) fn reassemble_with_os(os, prev, settings)` | ✅ The commands clone `AppContext` out of `SharedState` (FR-060). No new field (D10). The Linux configured-root tests build their context through `reassemble_with_os(Os::Linux, …)` (SC-003, SC-015) |
| `crates/core/src/feed.rs:22`, `:45-52`, `:55-62`, `:122-128`, `:141-152`, `:589-594`, `:603-614`, `:644-675`, `:741-743` | `EDGE_MAX_AGE_SECS = 600`; `PluginEntry { name, version_text, version, download_url, release }`; `StableRelease { …, notes_markdown, platforms, install }`; `InstallFacts { vst3_bundle, au_component, preset_subdir, windows_uninstall_key, macos_pkg_id }`; `PlainName` (no public constructor, `as_str()`); `FetchMode::{Normal, CheckNow}`; `FeedSnapshot`, `FeedCheck { snapshot, error, cache_error }`; `FeedError` + `code()`; `check(ctx, mode, now_unix)` | ✅ Consumed unchanged: every per-plugin fact detection uses comes from `release.install` (ROSTER) |
| `crates/core/src/feed.rs:459-469` | An `install` block the model refuses clears that entry's `platforms` and sets `install: None` | ✅ Such an entry is "not checked" (FR-042) |
| `crates/core/src/version.rs:56-65`, `:68-72`, `:75-81`, `:84-94`, `:118-150`, `:187-197`, `:211-230`, `:233-240` | `reduce_class_version` (`X.Y.Z` or `X.Y.Z.N`, `N` 1-9 digits); `Installed::{No, VersionUnknown, Version}`; `UpdateState`; `compare`; `OsVersion` (2-4 parts); `Availability::{Available, NotThroughManager, NotForThisMachine, NeedsNewerOs}`; `resolve`; `resolve_native(entry, running)` | ✅ The version rule (FR-013) and the state (FR-043, FR-045); unchanged |
| `crates/core/src/sysproxy.rs:181-229` | `BoundedRun { outputs, abandoned }`, `run_bounded(commands, bound)`: stdout piped, polled, abandoned (never killed) children reaped on a thread. A spawn failure (`:204-206`), a non-zero exit and a stdout read error (`:216-222`) all return `outputs: None, abandoned: []`, indistinguishable | ✅ Unchanged; reused by `OsHostIo::run` (FR-032), one command per call, as `src-tauri/src/os_proxy.rs:152`, `:221` already do. The three-way fold is why `RunOutcome` has one `NoOutput` case (FR-031) |
| `crates/core/src/test_support.rs:23-30`, `:125-129`, `:171-176`, `:179-191` | `PLUGIN_DIRS` (`vst3-user`, `vst3-global`, and `au-user`, `au-global` on macOS); `fresh_temp_roots()`; `install_capture()` (max level `Trace`) / `take_mine()` → `(target, level, message)` | ✅ Every detection test builds its trees under `fresh_temp_roots()`; `debug!` lines are captured too (SC-005). 🔶 Gains `FakeHostFacts` and `FakeHostIo` (FR-083) |
| `apps/krate-manager/src-tauri/src/lib.rs:72-90` | Builder: `.manage(SharedState::new(state))`, `invoke_handler(generate_handler![commands::app_version, commands::startup_error])` | 🔶 Three handlers added (FR-061, FR-064), plus the opener plugin registration (FR-064) |
| `src-tauri/src/commands.rs:1-14` | `app_version`, `startup_error` (sync, read-only) | 🔶 Gains `plugin_status`, `check_now` (FR-060) and `open_release_page` (FR-064) |
| `src-tauri/build.rs:4` | `AppManifest::new().commands(&["app_version", "startup_error"])` | 🔶 Five commands (FR-061) |
| `src-tauri/capabilities/main.json:1-6` | `"permissions": ["allow-app-version", "allow-startup-error"]` | 🔶 Five permissions (FR-061) |
| `src-tauri/tauri.conf.json:14`, `:16` | Window `main` 800×600; CSP `default-src 'self'; … img-src 'self'; … connect-src ipc: http://ipc.localhost; …` | ✅ Unchanged (FR-062) |
| `src-tauri/src/os_proxy.rs:64`, `:76-77`, `:144-152` | `CHILD_BOUND = 2 s`; `trait SystemProxyFromOs` implemented for `NativePlatform` per `cfg`; `Command::new("gsettings")` through `run_bounded` | ✅ The raw-call pattern the new `os_facts.rs` follows (D7); `os_facts.rs` holds no decision logic (FR-032) |
| `src-tauri/Cargo.toml:10-14`, `:16-19`, `:34-36` | `[lib] test = false`, `[[bin]] test = false`; `[target.'cfg(windows)'.dependencies] windows-sys = { workspace = true }` | ✅ Nothing in `src-tauri` is tested, which is why every mapping lives in core (D7). The `windows-sys` table is reused; the workspace pin gains features (FR-081); `tauri-plugin-opener` joins `[dependencies]` (FR-065) |
| `apps/krate-manager/Cargo.toml:36` | `windows-sys = { version = "0.61.2", features = ["Win32_Foundation", "Win32_Networking_WinHttp"] }` | 🔶 Three features added (FR-081) |
| `crates/core/Cargo.toml:15-26`, `:31-33`, `:35-37` | `[dependencies]` = the FR-007 set (10 keys); `cfg(not(windows))` table = `rquickjs`; macOS target table = `core-foundation` only | 🔶 `pulldown-cmark` joins `[dependencies]`; `plist` joins the macOS target table (FR-080) |
| `crates/core/tests/injection.rs:15-23`, `:43-71`, `:87-98`, `:275`, `:968-1009`, `:1981-2003` | `FORBIDDEN_OUTSIDE_PLATFORM`; `PLATFORM_LITERALS` (27 entries, the only literals a non-test `platform/` file may hold); `FR007_DEPENDENCIES` (10 names); `READER_PINS` (windows-sys features `Win32_Foundation`, `Win32_Networking_WinHttp`); `scan_core_file` (the forbidden substrings outside `platform/`, the allow-list inside it); `core_sources_hold_no_production_literal` scans `crates/core/src` only (`:1983-1984`) | 🔶 `PLATFORM_LITERALS` gains the FR-030 constants (FR-033); FR-080 to FR-084 |
| `crates/core/tests/config_guard.rs:509-560`, `:626`, `:870` | Capabilities exactly `main.json` with two permissions; `build.rs` registers exactly two commands; the CSP check; the `src/` URL / markup-sink scan | 🔶 Five commands and five permissions (FR-063); the opener guard (FR-065); the CSP and sink checks unchanged |
| `crates/core/tests/roster.rs:128`, `:175-189` | `roster_and_hosts_absent_from_sources` scans `crates/*/src`, `src-tauri/src`, `src/`, `index.html` for the nine plugin names, case-sensitively: its self-test asserts "the lower-cased name is not a hit" (`:187`) | ✅ Covers every new file unchanged (FR-087). It cannot see a hard-coded `com.krateaudio.<name>` or Inno `{GUID}_is1`, so FR-089 adds an identifier guard beside it |
| `apps/krate-manager/src/main.ts:1-26`, `src/main.test.ts` | `renderShell`: `startup_error`, then `app_version`; four vitest cases asserting the call list | 🔶 The shell gains the status section; the three required cases keep their titles and gain `plugin_status` in their call lists (FR-070) |
| `apps/krate-manager/eslint.config.mjs:5-23` | `strictTypeChecked`; `innerHTML`, `outerHTML`, `insertAdjacentHTML`, `document.write(ln)`, `eval`, `new Function` banned | ✅ Unchanged: the notes renderer builds DOM with `createElement` / `textContent` only (FR-072) |
| `apps/krate-manager/tsconfig.json` | `strict`, `noUncheckedIndexedAccess`, `exactOptionalPropertyTypes`, `resolveJsonModule`, `include: ["src"]` | ✅ The view types and the fixture imports type-check under it |
| `apps/krate-manager/tests/required-tests.json` | `cargo.all / windows / macos / linux`, `vitest` | 🔶 FR-085 |
| `.github/workflows/manager-ci.yml:29-34`, `:117-120`, `:137-162`, `:196` | Matrix `windows-latest, macos-latest, ubuntu-24.04` (job `check (<os>)`); `cargo test --workspace --locked` teed to `cargo-test.txt`; "required tests ran": exactly one `test <name> ... ok` line per name of `cargo.all` + `cargo.<os>` and one passed vitest per `vitest` title (`:147-157`), one `::error title=required tests ran (<os>)::…` line per miss (`:159`), and on success one `::notice title=required tests ran (<os>)::<n> cargo + <m> vitest` carrying **counts only, no names** (`:161`); "roster absent" | 🔶 One step added, `core dependency tree` (FR-080, FR-090); otherwise unchanged; PENDING-CI rows close on these steps (Success Criteria preamble) |
| `tools/feed-contract/tests/manager-ci-shape.test.js:163-190`, `:200-211`, `:246`, `:274` | The pinned step order, the `::notice` / `::error` assertion-step list and the per-step command lines of the `check` job | 🔶 Gains the `core dependency tree` step in all three (FR-090) |
| `tools/check-bundle.js:6-9`, `:27-30`, `:47-55` | The `0.8.0` history ("every bundle shipped `0.8.0` while its classes said e.g. 0.12.2.1"); `parseModuleInfo` strips trailing commas with a regex; class `Version` must equal `version.json` or start with `"<ver>."` | ✅ The detection contract (FR-013). Its regex also edits commas inside strings; Phase 6's own strip is string-aware (FR-012) |
| `build/windows-x64-release/VST3/Release/Iterum.vst3/Contents/Resources/moduleinfo.json:3`, `:12-13`, `:20`, `:35` (local build, read this session) | Top-level `"Version": "1.0.0"`; trailing commas (`},` before `}`); class versions `"1.0.0.1"` | ✅ The shape of the "current bundle" fixture |
| `C:\Users\scab\AppData\Local\Programs\Common\VST3\Membrum.vst3\Contents\Resources\moduleinfo.json:3`, `:20` (read-only, this session) | A per-user dev copy: top-level `"0.12.2"`, class `"0.12.2.1"` | ✅ Evidence that the dev box has per-user copies (roadmap line 978); never touched by a test |
| `extern/vst3sdk/public.sdk/source/vst/moduleinfo/moduleinfocreator.cpp:48-53`, `:123`, `:157-159`, `:277-281`; `jsoncxx.h:213-218`; `moduleinfoparser.cpp:402-430` | The writer puts a comma after every element; the SDK reader parses with JSON5 flags; keys `Name`, `Version`, `Classes`, class `Version` | ✅ The grammar and keys of FR-012, FR-013 |
| `plugins/*/resources/au-info.plist:19-20`, `:38-39` (all nine) | AUv2 template: `CFBundleVersion` `1.0`, `AudioComponents[0].version` `<integer>1</integer>`, no `CFBundleShortVersionString` | ✅ The AU fact behind D6 |
| `cmake/KratePlugin.cmake:174` and `extern/vst3sdk/cmake/modules/SMTG_Bundle.cmake:80-84`, `:97-124`, `SMTG_AddVST3AuV2.cmake:112` | The AU target passes `INFO_PLIST_TEMPLATE` → `XCODE_ATTRIBUTE_INFOPLIST_FILE`; the `MACOSX_BUNDLE_*_VERSION` properties are set only in the `elseif(XCODE)` branch without a template | ✅ Why the shipped AU plist carries no plugin version (D6) |
| `plugins/iterum/installers/windows/setup.iss:27`, `:29`, `:37`, `:50`, `:53`, `:64`, `:72`; all nine scripts have `DefaultDirName={commonpf}\Common Files\VST3\Krate Audio\<P>` and no `DisableDirPage` (grep, 0 hits each) | `AppId`, `AppVersion`, `DefaultDirName`, `PrivilegesRequired=admin`, `UsePreviousAppDir=yes`, `ArchitecturesInstallIn64BitMode=x64compatible`, the bundle to `{app}\<P>.vst3` | ✅ The Windows managed location and record (FR-001, FR-026) |
| `.github/workflows/release.yml:298-299`, `:325`, `:339-340` | macOS payload `Library/Audio/Plug-Ins/{VST3,Components}`; pkg identifier `com.krateaudio.<name>`; `pkgbuild --version "$VERSION" --install-location /` | ✅ The macOS managed location and the receipt's `version` (FR-001, FR-027) |
| `tools/feed-contract/CONTRACT.md:50`, `:71-75`, `:114-142` | `platforms` and `install` present together; `vst3_bundle` `^[A-Za-z0-9][A-Za-z0-9 _.-]*\.vst3$` equal to `<Name>.vst3`; `au_component` / `macos_pkg_id` required with a `darwin-*` platform, `windows_uninstall_key` (`{GUID}_is1`) with `windows-x86_64`; reader rules 1-8 (rule 6: "not available through the manager"; rule 8: never a path unchecked) | ✅ FR-001, FR-029, FR-042 |
| `tools/feed-contract/fixtures/feeds/valid/{full-v2,mixed,tenth-plugin,v1-only,notes-14000,unknown-platform-key}.json` | `full-v2`: Alpha (`windows-x86_64, darwin-aarch64, linux-x86_64`, notes `"### Fixed\n\n- A fixture note."`), Bravo (`preset_subdir` `Kits`), Charlie; `tenth-plugin`: ten v2 plugins Alpha…Juliet; `v1-only`: three v1 entries; `notes-14000`: Alpha notes of 14,148 chars | ✅ Read in place by the status and detection tests (FR-086) |
| `specs/krate-manager/probes/results.md:8`, `:20` | Ableton Live 12, FL Studio, Renoise ignore the per-user VST3 folder; REAPER, Bitwig, Studio One, Cakewalk, Cubase scan it; bundles released before each plugin's next release say `0.8.0` at top level | ✅ D1, D5 |
| `specs/krate-manager/research_notes/…/per_os_install_mechanics.md:8`, `:13-16`, `:20-23`, `:26-28`, `:74`, `:78-79`, `:86-87`, `:139` | Steinberg folder priority (user first, "First found plug-in (for a given Processor UID)"), bundle format, single-file `.vst3` deprecated since 3.6.10 (Windows SDK host still accepts it, Linux does not), moduleinfo keys, Inno `<AppId>_is1` key in the 64-bit HKLM view for admin-mode 64-bit installers, the Microsoft Uninstall-key values, `pkgutil` options and sample `--pkg-info` output | ✅ D1, D2, D7 |
| `specs/krate-manager/reports/Krate Audio plugin manager design.md:26`, `:69` | Detection without loading; AU `Info.plist` "holds `CFBundleShortVersionString` and the `AudioComponents[].version` integer"; one card per plugin with installed version, available version, action, release notes | ✅ D6 (corrected by the repo fact), FR-071 |

---

## External facts used (verified this session)

| Fact | Source |
|---|---|
| `pulldown-cmark` 0.13.6 (2026-10-30, `rust-version` 1.71.1, MIT): `default = ["getopts", "html"]`, `html = ["pulldown-cmark-escape"]`; non-optional dependencies `bitflags ^2`, `memchr ^2.5`, `unicase ^2.6`. `Parser::new` / `Parser::new_ext(text, Options)`; `Event::{Start(Tag), End(TagEnd), Text, Code, InlineMath, DisplayMath, Html, InlineHtml, FootnoteReference, SoftBreak, HardBreak, Rule, TaskListMarker}`; `Tag::{Paragraph, Heading{level,..}, BlockQuote(..), CodeBlock(..), HtmlBlock, List(Option<u64>), Item, …, Emphasis, Strong, Strikethrough, …, Link{..}, Image{..}, …}`; tables, strikethrough, footnotes, math and heading attributes are produced only when their `Options` flag is set | https://crates.io/api/v1/crates/pulldown-cmark/0.13.6 (+ `/dependencies`); https://docs.rs/pulldown-cmark/0.13.6/pulldown_cmark/enum.Event.html , `enum.Tag.html`, `struct.Parser.html` |
| `plist` 1.10.3 (2026-10-21, `rust-version` 1.88.0, MIT): `default = ["serde"]`; `Value::from_reader<R: Read + Seek>` reads "a plist file of any encoding" (XML or binary); `Value::as_dictionary`, `Dictionary::get`, `Value::as_string`. Already in `Cargo.lock` (`:2321-2329`) through `tauri` 2.12.5, whose `plist` dependency is macOS-only (`tauri-2.12.5/Cargo.toml:404`) | https://crates.io/api/v1/crates/plist/1.10.3 ; local registry copy `plist-1.10.3/src/value.rs:34-55`, `:211`, `:317`, `src/dictionary.rs:38` |
| `windows-sys` 0.61.2 (latest): `Win32_System_Registry` gives `RegOpenKeyExW` (`advapi32`, `samdesired: REG_SAM_FLAGS`), `RegGetValueW`, `RegCloseKey`, `HKEY_LOCAL_MACHINE`, `KEY_READ` (131097), `KEY_WOW64_64KEY` (256), `RRF_RT_REG_SZ` (2); `Wdk_System_SystemServices` gives `RtlGetVersion` (`ntdll`) taking `*mut OSVERSIONINFOW` from `Win32_System_SystemInformation` | local registry copy `windows-sys-0.61.2/src/Windows/Win32/System/Registry/mod.rs:2`, `:45`, `:56`, `:153`, `:169`, `:172`, `:1087`; `Wdk/System/SystemServices/mod.rs:1473`; `Win32/System/SystemInformation/mod.rs:438`; `Cargo.toml:54`, `:234`, `:248` |
| `RegOpenKeyExW`: "does not create the specified key if the key does not exist in the registry"; "If the function succeeds, the return value is ERROR_SUCCESS", else "a nonzero error code"; the page's own example treats `ERROR_FILE_NOT_FOUND` as "Key not found"; "call the RegCloseKey function after you have finished using the handle" | https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw |
| `RegGetValueW`: `hkey` "A handle to an open registry key … returned by the … RegOpenKeyEx … function"; `lpSubKey` "If this parameter is NULL or an empty string, "", the value will be read from the key specified by hkey itself"; `RRF_RT_REG_SZ` "Restrict type to REG_SZ"; "If the lpValue registry value does not exist, the function returns ERROR_FILE_NOT_FOUND"; `pvData` NULL with `pcbData` non-NULL "stores the size of the data, in bytes" | https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew |
| `cargo tree` (cargo 1.99.1, `cargo tree --help` run this session): `-e, --edges <KINDS>` (`normal`), `--prefix <PREFIX>` (`none`), `-f, --format <FORMAT>` (default `{p}`), `--locked`; host target by default | https://doc.rust-lang.org/cargo/commands/cargo-tree.html |
| POSIX `stat`: `[ENOTDIR]` "A component of the path prefix names an existing file that is neither a directory nor a symbolic link to a directory"; Rust `io::ErrorKind::NotADirectory` (stable 1.83.0) "a filesystem path was specified where one of the intermediate directory components was, in fact, a plain file", distinct from `NotFound` | https://pubs.opengroup.org/onlinepubs/9799919799/functions/stat.html ; https://doc.rust-lang.org/std/io/enum.ErrorKind.html |
| `RtlGetVersion` "returns version information about the currently running operating system" in `RTL_OSVERSIONINFOW` (`dwMajorVersion`, `dwMinorVersion`, `dwBuildNumber`) and returns `STATUS_SUCCESS` | https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlgetversion |
| Inno `AppVersion` "is displayed in the Version field of the application's Add/Remove Programs entry"; `UsePreviousAppDir=yes` re-uses the directory of a previous installation; `DisableDirPage` controls the destination page | https://jrsoftware.org/ishelp/topic_setup_appversion.htm , `topic_setup_usepreviousappdir.htm` , `topic_setup_disabledirpage.htm` |
| The Uninstall key's documented values include `DisplayVersion` and `InstallLocation`. Whether Inno writes `InstallLocation` is **not** confirmed from the Inno docs (per_os_install_mechanics.md:107 leaves it unverified); FR-026 treats it as optional | https://learn.microsoft.com/en-us/windows/win32/msi/uninstall-registry-key (per_os_install_mechanics.md:79) |
| `pkgutil --pkg-info <id>` "Print extended information about the specified package-id"; `--files <id>` "List all of the files installed under the package-id"; `--volume` defaults to `/`; "Always use pkgutil to query or modify them" | https://keith.github.io/xcode-man-pages/pkgutil.1.html |
| `sw_vers --productVersion` / `-productVersion` prints the running macOS product version (e.g. `13.0`) | https://keith.github.io/xcode-man-pages/sw_vers.1.html |
| `confstr(_CS_GNU_LIBC_VERSION)`: "A string which identifies the GNU C library version on this system (e.g., "glibc 2.3.4")"; POSIX `getconf` accepts "the names of the symbolic constants used as the name argument of the confstr() function … without the _CS_ prefix" | https://man7.org/linux/man-pages/man3/confstr.3.html ; https://man7.org/linux/man-pages/man1/getconf.1p.html |
| Tauri 2: "Async commands are executed on a separate async task using `async_runtime::spawn`"; borrowed arguments such as `State<'_, Data>` in an async command need the return type wrapped in a `Result`; `tauri::async_runtime::spawn_blocking(func)` "Runs the provided function on an executor dedicated to blocking operations" | https://v2.tauri.app/develop/calling-rust/ ; `tauri-2.12.5/src/async_runtime.rs:309-318` (local registry copy) |
| `tauri-plugin-opener` 2.7.2 (crates.io, published 2026-10-14, `rust-version` 1.90, Apache-2.0 OR MIT; `3.0.0-alpha.3` also exists and is not used). Normal dependencies read this session: `tauri ^2.12`, `open ^5`, `dunce ^1`, `glob ^0.3`, `serde ^1`, `serde_json ^1`, `thiserror ^2`, `url ^2` and `zbus ^5.9` (Linux and BSD), `windows ^0.62` (Windows), `objc2-app-kit ^0.3` and `objc2-foundation ^0.3` (macOS). Webview permissions are generated per command; `allow-open-url` enables the command `open_url` and is written `opener:allow-open-url` in a capability (`permissions/autogenerated/commands/open_url.toml`, read this session). The Rust entry points `tauri_plugin_opener::init()` and `OpenerExt::open_url(url, None::<&str>)` were NOT read this session: the plan stage confirms their signatures against the 2.7.2 source before FR-064 is implemented | https://crates.io/api/v1/crates/tauri-plugin-opener (+ `/2.7.2/dependencies`); https://docs.rs/crate/tauri-plugin-opener/2.7.2/source/permissions/autogenerated/commands/open_url.toml |
| `@tauri-apps/plugin-opener` 2.7.2 (npm, published 2026-10-14, depends on `@tauri-apps/api ^2.12.0`) is the JavaScript counterpart (`openUrl`). It is NOT installed: the webview never calls the opener (D15, FR-065) | https://registry.npmjs.org/@tauri-apps/plugin-opener |
| Release page URL shape: the feed's `release_notes_url` is `https://github.com/<repo>/releases/tag/<tag>` (the fixtures carry the same shape) | `tools/feed-contract/CONTRACT.md:57`; `tools/feed-contract/fixtures/feeds/valid/full-v2.json:11` |

---

## Decisions taken in this spec (open to challenge in the clarify scan)

The roadmap defers no decision to this spec by name. D1-D14 settle points it names but does not pin down.

- **D1, the managed copy decides "installed"; every other copy is listed (rulings Q1, Q2).** Each plugin has one
  *managed location*: the path its own installer (Windows, macOS) or the manager (Linux, Phase 9) writes, because that is
  the copy Install, Update and Remove act on (Phases 9, 10). Windows: `<InstallLocation>\<bundle>` when the Inno key
  carries a usable `InstallLocation`, wherever it lies (ruled Q2: every `setup.iss` has `UsePreviousAppDir=yes` and no
  `DisableDirPage`, so a re-run installs into the folder the user chose before, and that folder is the one Update and
  Remove must act on; only an administrator can write that HKLM value; Phase 10's helper still never runs the uninstaller
  of a copy outside the compiled-in Windows plug-in root, FR-092 (plan SI-14)), else `<Vst3Global>\Krate Audio\<stem>\<bundle>` (`setup.iss:37`,
  `:72`); macOS `<Vst3Global>/<bundle>` (`release.yml:298`); Linux `<settings root>/<bundle>` (`settings.rs:28`). The
  installed state is the managed copy's: known version → that version, unknown → "Installed, version unknown". With no
  managed copy and at least one other VST3 copy it is `elsewhere` (ruled Q1): "Installed outside the manager's location:
  <version>" (the first other copy with a known version, else the word `unknown`), the update state stays
  `not_installed` ("Available to install"), and every copy is listed; with no copy at all it is "Not installed". Every
  other copy found is listed with its own version, and flagged when a host that follows Steinberg's scan order loads it
  before the managed location (D11). Reason: an update installs to the managed location only, so taking the version
  from a shadowing copy of a managed install would announce an update that installing never clears (the phantom-update
  failure, report line 26), and taking it from "any copy" would hide a stale managed copy; `elsewhere` shows the outside
  copy without letting it decide the update state. Consequence, accepted: on this dev box, whose plugins sit in the
  per-user folder, the cards read "Installed outside the manager's location: <version>" with "Available to install" and
  the per-user copies listed under them.
- **D2, a fixed candidate list plus a bounded listing (ruled Q3).** Detection stats a fixed list of paths per plugin
  (FR-001) and, to report nested copies, lists each injected VST3 root once per build under the limits of FR-006: at most
  4 levels below the root and 20,000 entries per root, matching only the `facts.vst3_bundle` names of the feed's checked
  entries (ASCII case-insensitive on Windows). It reads at most one file per candidate or match, and `read_dir` is
  allowed in `detect.rs` only (FR-084). Reason: the roadmap asks to scan the injected roots and to report every copy
  found (lines 562-570; STRATEGY line 23; Not in v1 lines 971-972), and a host that scans recursively loads a copy in
  `<root>\Old\<bundle>` or `<root>\Krate Audio\<bundle>`, either of which can shadow the managed copy; the depth and
  entry caps keep the work bounded in roots that can hold thousands of third-party bundles. Not covered: a copy outside
  every injected VST3 root (on Windows, a custom folder an older Inno install chose when the key records no
  `InstallLocation`, Edge cases), a copy deeper than 4 levels or beyond a root's first 20,000 entries, and an AU copy
  outside the fixed AU candidates.
- **D3, no install facts, no detection.** An entry without `install` (a v1 document, a v2 entry without `platforms`, or
  one whose `install` the model refused, `feed.rs:459-469`) is "not checked": no bundle name is derived from the feed key,
  because ROSTER takes every per-plugin fact from the entry's `install` block (roadmap line 33). Consequence, accepted:
  against today's live v1 feed every card reads "Not checked" until Phase 3's live deploy and the Phase 2 backfill
  (roadmap lines 444-446) give each plugin an `install` block.
- **D4, the moduleinfo grammar is JSON plus trailing commas.** The SDK writer emits a comma after every element
  (`moduleinfocreator.cpp:48-53`, `:123`); nothing else of JSON5 occurs in a Krate bundle (the Iterum file read this
  session). A string-aware pass removes a `,` that is followed only by whitespace and `}` or `]` outside string literals,
  then `serde_json` parses. Comments, single quotes and unquoted keys are not accepted (the copy reads "version unknown",
  logged). Not the `json5` crate: its current release is 1.3.7, published 2026-10-31 (crates.io), the day of this spec.
- **D5, the version rule.** Class versions decide (Key decision 6); all must reduce to one value; the top-level `Version`
  is used only when no class has a `Version`, and is flagged so. Every disagreement with the top-level value is logged
  once per copy (roadmap line 566: "a disagreement is logged and never changes the result"; Key decision 6, lines
  187-188): at `warn!` in general, and at `debug!` when the top-level text is exactly `0.8.0`, the stamp every bundle
  carried before commit `294a26e7` (`check-bundle.js:6-9`, probes line 20). That stamp is expected for released bundles,
  and a `warn!` for it would put one warning per plugin into every run; the roadmap's own measurable outcome singles out
  disagreements "on a value other than `0.8.0`" (line 581-582) as the logged mismatch. The level is the only difference:
  the line is written either way, so no departure from line 566 remains.
- **D6, an AU copy is reported, never decides.** The AUv2 `Info.plist` template of all nine plugins carries
  `CFBundleVersion` `1.0`, `AudioComponents[0].version` `1` and no `CFBundleShortVersionString` (`au-info.plist:19-20`,
  `:38-39`), and the SDK's template path sets no version property (`SMTG_Bundle.cmake:80-84` against `:97-124`). So the
  plist the roadmap asks Phase 6 to read (line 568) carries no plugin version today. Phase 6 reads it anyway: an AU copy's
  version is `CFBundleShortVersionString` when that is a three-part core version, else "version not recorded in the AU
  bundle" (ruled Q6). AU copies are listed beside the VST3 copies and never decide the installed state. No released `.component`
  was read this session (no macOS release bundle is on this box); the fixtures cover both the template shape and a
  plist with a short version string, so the rule holds whichever a shipped bundle turns out to carry.
- **D7, the OS readers sit behind `Platform`; only raw I/O is outside core.** The roadmap puts the installer-record facts
  "behind the platform abstraction with fakes in tests" (line 572), reads the running OS version "through `Platform`"
  (line 576), runs the fixture `pkgutil` output "through the macOS `Platform` implementation" (lines 591-592), and wants
  every OS-specific path in "the one `Platform` abstraction with a Windows, macOS and Linux implementation, each with
  tests" (lines 999-1001). So there are three layers, each with one job:
  (1) **`HostIo`** (core, `platform/mod.rs`): three raw primitives with no decisions: run one argv with a bound, read two
  registry values of one key, call `RtlGetVersion`. Its only real implementation, `OsHostIo` in
  `src-tauri/src/os_facts.rs`, is FFI and `Command` glue (FR-032).
  (2) **`trait Platform`** gains `os_version`, `inno_values` and `pkg_outputs`, each taking `&dyn HostIo`, implemented in
  `platform/windows.rs`, `macos.rs` and `linux.rs` (FR-030). Each one names its registry path or argv from its own
  constants and maps the raw answer through pure mappers in `detect.rs` (FR-034). These methods are tested on their own
  OS leg against a recording `FakeHostIo` fed the committed fixture outputs (SC-025, SC-014). The mappers are tested on
  every host (SC-026).
  (3) **`HostFacts`** (core, `detect.rs`): the facts detection and status consume. In production it is
  `PlatformFacts { io }`, which delegates to `NativePlatform`. In the cross-OS tables (SC-008, SC-009) it is
  `FakeHostFacts`, because a Windows row has to run on a Linux host where `WindowsPlatform` is not compiled.
  The Linux install manifest is a file in the injected state dir, so core reads it directly. What remains untested is
  only the body of `OsHostIo`, roughly the FFI calls `RegOpenKeyExW` / `RegGetValueW` / `RegCloseKey` /
  `RtlGetVersion` and `Command::new(argv[0]).args(..)` handed to `run_bounded`, because no test may read the real
  system (roadmap line 585). Phase 12's user-performed checks exercise it on real machines. This is the Phase 5
  `os_proxy.rs` precedent with every decision moved out of `src-tauri`, which has `test = false`
  (`src-tauri/Cargo.toml:13`, `:19`).
- **D8, the running OS version.** macOS: `/usr/bin/sw_vers -productVersion`; Linux: `getconf GNU_LIBC_VERSION` (glibc is
  what `min_os` means on Linux, Phase 5 FR-037); Windows: `RtlGetVersion` → `major.minor.build` (documented to report
  the running OS; `GetVersionEx` reports what the manifest declares). Each is one `Platform::os_version` method over
  `HostIo` (FR-030). Both processes go through `run_bounded` with a 2 s bound and are never killed. Read once per status
  build; a failure gives `None`, which never gates (Phase 5 FR-037).
- **D9, release notes become a typed block tree in core.** `core::notes` parses `notes_markdown` with `pulldown-cmark`
  0.13.6 (`default-features = false`, so no HTML writer is compiled), `Options::empty()`, into blocks and inlines; the UI
  builds DOM from them with `createElement` / `textContent`. A link becomes its text, an image its alt text, raw HTML
  literal text: there is no anchor, no image and no navigation, so "no remote content" holds by construction and the
  Phase 4 CSP and markup-sink ban stay untouched. Not a TypeScript Markdown library: the Phase 4 frontend has one runtime
  dependency (`@tauri-apps/api`, Phase 4 D1), and a parser in core is tested by `cargo test` on every leg. Not a
  hand-written Markdown subset: CommonMark has too many corner cases.
- **D10, one view, built in core, fixed by golden files.** `core::status::assemble` is a pure function from (OS, feed
  check, running OS version, detections) to a `serde`-serialisable `StatusView`; `status::build` does the I/O around it.
  Golden JSON files under `apps/krate-manager/tests/fixtures/status/` are compared byte for byte by `cargo test` and
  rendered by `vitest`, so the Rust and TypeScript sides cannot drift. `AppContext` gains no field: the host readers are
  a parameter of `build`.
- **D11, scan order is Steinberg's and the shadow runs user over global.** Steinberg's hosts scan the user folder first
  and use the "First found plug-in (for a given Processor UID)" (per_os_install_mechanics.md:13-16). So a per-user copy
  shadows the global one, not the reverse; the roadmap's wording "a stale global copy shadowing a user copy" (line
  569-570, from report line 34) has the direction inverted. A copy is flagged `loads_before_managed` when it sits in a
  Steinberg folder that ranks before the managed location's (user before global; on Linux the globals in the order
  `linux.rs:38-41` lists them). Copies outside Steinberg folders (a configured Linux root, an Inno `InstallLocation`
  elsewhere, AU copies) have no rank and are never flagged. Hosts that ignore the per-user folder (Ableton Live 12, FL
  Studio, Renoise, probes line 8) load the global copy regardless; the flag's text says "hosts that follow Steinberg's
  scan order".
- **D12, dependencies.** Core: `pulldown-cmark` 0.13.6 without default features (all OSes) and `plist` 1.10.3 without
  default features (macOS target table only, already linked into the macOS app through `tauri`). `src-tauri`:
  `windows-sys` gains `Win32_System_Registry`, `Win32_System_SystemInformation` and `Wdk_System_SystemServices`, and it gains `tauri-plugin-opener` 2.7.2 (D15, FR-065). No other
  crate, no npm package.
- **D13, when the screen refreshes.** On window load (`plugin_status`, `FetchMode::Normal`) and on "Check now"
  (`check_now`, `FetchMode::CheckNow`); detection reruns with every check. No timer, no poll, no file watcher.
- **D14, names and codes.** New modules `core::detect`, `core::notes`, `core::status` (flat files with `<stem>_tests.rs`,
  Phase 4 convention); `src-tauri/src/os_facts.rs`. Every error or log code begins its line: `E_DETECT_MODULEINFO`,
  `E_DETECT_CLASSES`, `E_DETECT_TOP_LEVEL`, `E_DETECT_PLIST`, `E_RECORD_READ`, `E_RECORD_PARSE`, `E_RECORD_FUTURE`,
  `E_RECORD_MISMATCH`, `E_RECORD_LOCATION`, `E_RECORD_TIMEOUT`, `E_STATUS_TASK`, `E_DETECT_LISTING_CAP`, `E_OPEN_REJECTED`, `E_OPEN_FAILED`. Log target `krate_manager_core::detect`
  for detection and records, `krate_manager_core::status` for the summary line.
- **D15, the release-page button (ruled Q8; the user chose C over the recommended A).** Each card with a validated
  release page shows one `Open release page` button, and it is the only navigation path out of the window. Core validates
  the feed's `release_notes_url` (`feed.rs:58`) with `status::release_page_url` (FR-049): `https`, host exactly
  `github.com` (the host CONTRACT.md:57 fixes for the field), no userinfo, no explicit port, at most 2,048 bytes. The view
  carries `release_page_url` only when that holds, so the button exists only then. A click calls the command
  `open_release_page(url)` (FR-064), which validates again in core and only then opens the URL in the system browser
  through the Tauri opener crate `tauri-plugin-opener` 2.7.2 (the command is `pub fn open_release_page(url: String) ->
  Result<(), String>`, with no `AppHandle`, and calls the crate-root free function `tauri_plugin_opener::open_url(valid, None::<&str>)`; the plugin is never registered: no
  `tauri_plugin_opener::init()`, no `OpenerExt`, so no opener script is injected into the page and no opener IPC command
  exists (plan SI-15)), the npm counterpart `@tauri-apps/plugin-opener` 2.7.2 (`openUrl`) deliberately not installed.
  The capability permission added is `allow-open-release-page`, the app's own command. The opener plugin's own
  permissions (`opener:allow-open-url` and the rest) are deliberately NOT granted to the webview: a webview that could
  call `open_url` itself would bypass core's host check (FR-065). Release-note link destinations stay dropped and no `<a>`
  exists (D9 for notes is unchanged, FR-075). Residual, accepted: the webview can ask to open any URL that passes the
  host check, not only the feed's; the host allow-list is the boundary.

---

## Functional Requirements

### A. Where detection looks (roadmap lines 562-563; D1, D2, D11)

- **FR-001** `detect::candidates(os: Os, inputs: &CandidateInputs) -> Vec<Candidate>` is pure (no filesystem, no
  environment). `CandidateInputs { plugin_roots: &[PluginRoot], facts: &InstallFacts, windows_install_location:
  Option<&Path>, linux_managed_root: &Path, nested: &[ListedCopy] }` (`ListedCopy { root: RootKind, root_index: usize,
  path: PathBuf }`, FR-006; `root_index` indexes `plugin_roots`, so a nested copy under the n-th `Vst3Global` root gets
  rank n (plan SI-5)). `Candidate { format: CopyFormat::{Vst3, Au}, place: Place, path: PathBuf,
  managed: bool, rank: Option<u8> }`, `Place::{Root(RootKind), WindowsVendor(RootKind), InstallerLocation, Nested(RootKind)}` (`Nested` comes only from the
  listing, FR-006, never from the fixed table below). With
  `<bundle>` = `facts.vst3_bundle`, `<stem>` = `<bundle>` without its `.vst3` suffix (the C1 pattern guarantees the
  suffix, CONTRACT.md:71) and `<component>` = `facts.au_component`, in this order:
  - **Windows:** for each root of kind `Vst3User` or `Vst3Global`, in `plugin_roots` order: `<root>\<bundle>`
    (`Root(kind)`), then `<root>\<VENDOR_DIR>\<stem>\<bundle>` (`WindowsVendor(kind)`); then, when
    `windows_install_location` is `Some(loc)` and `<loc>\<bundle>` is `same_path` as no earlier candidate,
    `<loc>\<bundle>` (`InstallerLocation`). Managed (D1, ruled Q2): when `windows_install_location` is `Some(loc)`, the
    candidate at `<loc>\<bundle>` (the `InstallerLocation` candidate, or the earlier candidate it was `same_path` as),
    wherever `loc` lies: there is no vendor-folder test; otherwise the `WindowsVendor(Vst3Global)` candidate of `<V>`,
    where `<V>` is the first `Vst3Global` root. An `InstallerLocation` candidate lies outside every Steinberg folder
    and has `rank: None`.
  - **macOS:** for each `Vst3User` / `Vst3Global` root: `<root>/<bundle>`; then, when `au_component` is `Some`, for each
    `AuUser` / `AuGlobal` root: `<root>/<component>`. Managed: the first `Vst3Global` candidate (VST3) and the first
    `AuGlobal` candidate (AU).
  - **Linux:** for each `Configured`, `Vst3User` and `Vst3Global` root: `<root>/<bundle>`. Managed: the candidate whose
    root path is `==` `linux_managed_root` (exact `Path` equality: Linux paths are case-sensitive). A context built by
    `context::fold` always holds such a root: `linux_plugin_root_or_default` is either the configured path, which `fold`
    inserted or found already present (`roots.rs:145`), or `vst3_user()` (`settings.rs:28-32`). When no root matches, a
    context `fold` never produces, `candidates` fires `debug_assert!` (so a test that builds one fails), and in a release
    build no candidate is managed, so the plugin reads `installed: not_installed` with every copy listed.
  `rank` (D11): `Vst3User` → 0; the n-th `Vst3Global` root in `plugin_roots` order → n (1-based); `Configured`,
  `InstallerLocation` (unless it is `same_path` as an earlier candidate, which it then is) and every AU candidate →
  `None`. `WindowsVendor(kind)` and `Nested(kind)` take the rank of their root. A managed `InstallerLocation` candidate
  therefore has no rank and no copy is flagged against it (FR-044).
  Path comparison on the Windows table (Windows semantics, whichever host runs it): `detect::same_path(a, b)` is true
  when `a.components()` and `b.components()` have the same length and each pair of components is equal under
  `eq_ignore_ascii_case` of their `as_os_str().as_encoded_bytes()`. (The vendor-folder `within` test is gone, ruled Q2.)
  Every path is built with `Path::join`, so the host's separator is the only one in it.
- **FR-002** `platform::VENDOR_DIR: &str = "Krate Audio"` is the only spelling of the vendor folder outside
  `platform/`'s default roots; `detect.rs` holds no `"Krate Audio"` literal.
- **FR-003** `windows_install_location` comes only from a record's `InstallLocation` (FR-026) that passes
  `detect::usable_install_location(&str) -> Option<PathBuf>`: trailing `\` and `/` trimmed; absolute; at least one
  normal component; no `..` component; no NUL; at most 32,767 UTF-16 units. Anything else is `None` with one `warn!`
  `E_RECORD_LOCATION` naming the plugin (the value is not logged when it fails the length or NUL check).
- **FR-004** `detect::probe(candidate) -> Option<DetectedCopy>` stats `candidate.path` with `std::fs::metadata`
  (symlinks followed, Steinberg allows them, per_os_install_mechanics.md:16): `NotFound` → `None` (no copy); any other
  error → a copy with `CopyVersion::Unknown(Unreadable)` and one `warn!` `E_DETECT_MODULEINFO`; a file or directory → a
  copy read by FR-010 (VST3) or FR-020 (AU). Outside FR-006's bounded listing, detection never lists a directory; it
  reads nothing outside the candidate paths, the listing's matches and the files FR-011 and FR-020 name.
- **FR-005** The sandbox covers every injected path detection reads. `Roots` gains `pub(crate) fn check_sandboxed(&self,
  path: &Path)`, always compiled: when the roots carry a sandbox base (`fresh_temp_roots()` only, `roots.rs:136-144`) it
  panics unless `path` is a strict descendant of that base, using the same test as `with_configured_plugin_root`; with
  no sandbox (every production `Roots`) it does nothing. Its panic reads `install location <path> is outside the test
  sandbox <base>` (plan SI-12). `detect_plugin` calls it on the `InstallLocation` that passed
  FR-003 before building any candidate from it, so a fake record that names a real folder panics before anything is
  stat'ed. The other two injected paths are already covered: the configured Linux root goes through
  `with_configured_plugin_root` (`roots.rs:136-144`), and the Linux managed root is that root or `vst3_user()`.
- **FR-006** (ruled Q3) Nested copies, from a bounded listing. `detect::Listing::scan(roots: &[PluginRoot], names:
  &[&str], limits: ListingLimits) -> Listing` lists each injected root of kind `Configured`, `Vst3User` or `Vst3Global`
  (never an AU root), breadth first, and keeps every entry whose file name equals one of `names`, the `vst3_bundle`
  values of the feed's checked entries (ASCII case-insensitive on Windows, exact elsewhere). `ListingLimits { max_depth,
  max_entries }` have the production values `LISTING_MAX_DEPTH` = 4 and `LISTING_MAX_ENTRIES` = 20,000: depth 1 is a
  direct child of the root, nothing deeper than depth 4 is read, and at most 20,000 directory entries (files and
  directories, matches included) are examined per root. The one `std::fs::read_dir` call of the phase lives in the
  private `list_root`. The children of a directory are visited in file-name byte order, so the result and the point at
  which a cap cuts are deterministic. A directory is entered only when `DirEntry::file_type()` reports a real directory
  (a symlinked directory is not entered, so a link loop cannot exhaust the cap); a matched entry is not entered; a
  directory that cannot be read is skipped without a log. When a root reaches `max_entries`, listing of that root stops
  and one `warn!` `E_DETECT_LISTING_CAP` naming the root kind, the cap and the depth reached is written (once per root
  per build); reaching `max_depth` is not logged. Each root also writes one `debug!` line `listing: <root kind>: <n>
  entries examined`. `status::build` scans the roots once per build for the names of all checked entries and hands the
  `Listing` to every `detect_plugin` call (FR-040, FR-042): one traversal per root, not one per plugin. A match that is
  not `same_path` as a fixed candidate of the same plugin becomes a `ListedCopy { root, root_index, path }`; `candidates`
  turns it into a `Candidate { place: Nested(root kind), managed: false, rank: <the rank of the root at `root_index` in
  `plugin_roots`> }` (plan SI-5), appended after the fixed
  candidates in `plugin_roots` then listing order, and FR-004 probes it like any other. A copy at a fixed candidate is
  reported once, with the fixed candidate's flags. Not seen: a copy outside every injected VST3 root.

### B. Reading a VST3 copy (roadmap lines 563-567; D4, D5)

- **FR-010** A VST3 candidate that is a regular file is the deprecated single-file format
  (per_os_install_mechanics.md:22-23): `CopyVersion::Unknown(SingleFile)`, no log.
- **FR-011** A VST3 directory: read `Contents/Resources/moduleinfo.json` through a reader bounded to
  `MAX_MODULEINFO_BYTES + 1` = 1 MiB + 1 bytes. Missing → `Unknown(NoModuleInfo)`; more than 1 MiB → `Unknown(TooLarge)`;
  any read error (a sharing violation included) → `Unknown(Unreadable)`; not UTF-8 after skipping one leading U+FEFF →
  `Unknown(Corrupt)`. Each of `TooLarge`, `Unreadable`, `Corrupt` logs one `warn!` `E_DETECT_MODULEINFO` with the path
  and the reason; `NoModuleInfo` logs nothing (an incomplete bundle is a state, not an error).
- **FR-012** `detect::strip_trailing_commas(&str) -> Cow<str>` removes each `,` that lies outside a JSON string literal
  (escapes honoured), is followed by zero or more JSON whitespace characters and then `}` or `]`, and is preceded,
  ignoring JSON whitespace, by a character other than `[`, `{` or `,` (plan SI-16); it changes nothing else. The result is parsed with `serde_json::from_str::<Value>`; a parse failure or a root that is not an object is
  `Unknown(Corrupt)` with one `warn!` `E_DETECT_MODULEINFO`.
- **FR-013** The version of a parsed moduleinfo (D5):
  (a) *class versions*: when `Classes` is an array, every element that is an object and has a `Version` key contributes
  that value; a non-string value counts as unparseable. Elements without `Version` contribute nothing.
  (b) one or more class versions: each through `version::reduce_class_version`; all `Some` and equal →
  `Known { version, source: Classes }`; otherwise `Unknown(ClassesDisagree)` and one `warn!` `E_DETECT_CLASSES` naming
  the path and the class version texts.
  (c) no class version: the top-level `Version`, when it is a string, through `reduce_class_version` →
  `Known { version, source: TopLevel }` (the flag the roadmap asks for, line 567); otherwise `Unknown(NoVersion)`.
  (d) consistency (only after (b) gave `Known`): when the top-level `Version` is absent, or reduces to the class
  version, nothing; otherwise, when it is the string `0.8.0`, one `debug!` `E_DETECT_TOP_LEVEL` naming the path, `0.8.0`
  and the class version (D5); otherwise (not a string, or another value), one `warn!` `E_DETECT_TOP_LEVEL` naming the
  path, the top-level text and the class version. It never changes the result.
- **FR-014** `DetectedCopy { format, place, path: PathBuf, managed: bool, rank: Option<u8>, version: CopyVersion }`,
  `CopyVersion::{Known { version: CoreVersion, source: VersionSource::{Classes, TopLevel, AuPlist} }, Unknown(UnknownReason)}`,
  `UnknownReason::{SingleFile, NoModuleInfo, TooLarge, Unreadable, Corrupt, ClassesDisagree, NoVersion, NotABundle,
  AuNoVersion}`.

### C. AU copies, macOS only (roadmap line 568; D6)

- **FR-020** On macOS an AU candidate that is a regular file is `Unknown(NotABundle)`; a directory: read
  `Contents/Info.plist` through the same 1 MiB bound and `plist::Value::from_reader` (XML or binary). Missing or
  unreadable → `Unknown(Unreadable)`; more than 1 MiB → `Unknown(TooLarge)`; unparseable, or a root that is not a
  dictionary → `Unknown(Corrupt)`; each with one `warn!` `E_DETECT_PLIST` naming the path and the reason (a missing
  `Info.plist` is logged, unlike a missing moduleinfo: every AU build writes one from `au-info.plist` through the
  template path, `SMTG_Bundle.cmake:80-84`, so its absence means a damaged bundle); a root dictionary whose `CFBundleShortVersionString` is a string that is a three-part version (exactly `X.Y.Z`, decimal parts, accepted by `CoreVersion::parse`; `1.2` and
  `1.2.3.4` are not; ruled Q6) →
  `Known { version, source: AuPlist }`; otherwise `Unknown(AuNoVersion)`, no log. `CFBundleVersion` and
  `AudioComponents[].version` are not read (both are the constants `1.0` and `1` in every template, D6). Ruled Q6: no plugin-side change to
  `au-info.plist` is made and no follow-up is recorded.
- **FR-021** AU copies never set the installed state (FR-043); they are listed (FR-046). The AU reader and the `plist`
  dependency are compiled on macOS only (`#[cfg(target_os = "macos")]`); on Windows and Linux no AU candidate exists in
  production roots, and `candidates` for `Os::Macos` evaluated on another host only builds paths.

### D. Installer-record facts, read only (roadmap lines 571-572; D7)

- **FR-025** Core declares, in `detect.rs`, the facts seam detection and status consume (D7 layer 3):
  `pub trait HostFacts { fn os_version(&self) -> Option<OsVersion>; fn inno_values(&self, key: &PlainName) ->
  RecordRead<InnoValues>; fn pkg_outputs(&self, pkg_id: &PlainName) -> RecordRead<PkgOutputs>; }`,
  `pub enum RecordRead<T> { Absent, Found(T), Failed(&'static str) }`, `InnoValues { display_version: Option<String>,
  install_location: Option<String> }`, `PkgOutputs { pkg_info: String, files: String }`. `Failed` carries a code
  (`E_RECORD_READ` or `E_RECORD_TIMEOUT`). Its one production implementation is `pub struct PlatformFacts<'a> { pub io:
  &'a dyn HostIo }`, whose three methods call the same-named `NativePlatform` methods of FR-030 with `self.io`;
  `PlatformFacts` holds no other logic.
- **FR-026** Windows record (pure): `detect::inno_record(&InnoValues) -> InstallerRecord::Inno { version_text:
  Option<String>, version: Option<CoreVersion>, install_location: Option<PathBuf> }`, `version` =
  `CoreVersion::parse(display_version)`, `install_location` per FR-003. `Found` with both values absent is still a record:
  the key exists, which the real reader can tell apart from a missing key because it opens the key before reading
  values (FR-032, FR-034).
- **FR-027** macOS record (pure): `detect::parse_pkg_info(text, expected: &PlainName) -> Result<PkgInfo, &'static str>`
  splits lines at the first `": "`; requires `package-id` equal to `expected` and a `version`; keeps `location`;
  ignores every other key (`volume`, `install-time`). A missing required key or another `package-id` is
  `Err(E_RECORD_PARSE)`. `detect::count_pkg_files(text) -> u64` counts non-empty lines. Result: `InstallerRecord::Pkg {
  version_text, version: Option<CoreVersion>, location: Option<String>, files: u64 }` (the receipt version is the pkg's
  `--version`, `release.yml:339`). The listed paths are not interpreted (Phase 10 validates them before any removal).
- **FR-028** Linux record: the file `<state_dir>/manifests/<stem>.json` (`detect::MANIFESTS_DIR = "manifests"`), read
  bounded to 4 MiB. Missing → absent; a read error, a path that is not a regular file, or more than 4 MiB →
  `E_RECORD_READ` (plan SI-18). A JSON object whose `manifest_version` is the integer 1, whose `plugin` equals the
  entry's name and whose `version` is a string → `InstallerRecord::Manifest { version_text, version }`;
  `manifest_version` an integer above 1 → `E_RECORD_FUTURE`; `plugin` another name → `E_RECORD_MISMATCH`; anything else
  → `E_RECORD_PARSE`. Unknown keys are ignored. **Phase 9 writes this file** and may add keys; it never renames, retypes
  or removes these three (the Phase 1 reader rule 1 applied to the manifest).
- **FR-029** Lookup: on Windows `host.inno_values(key)` only when `facts.windows_uninstall_key` is `Some` (the value is
  the subkey name, `{GUID}_is1`, CONTRACT.md:74); on macOS `host.pkg_outputs(id)` only when `facts.macos_pkg_id` is
  `Some`; on Linux the manifest. `Absent` → no record; `Found` → parsed per FR-026 to FR-028; `Failed(code)` or a parse
  error → no record, `record_error: Some(code)` and one `warn!` naming the code and the plugin. A record never changes a
  copy's version. A usable `InstallLocation` passes `ctx.roots.check_sandboxed` (FR-005) before it feeds FR-001.
- **FR-030** `trait Platform` (`platform/mod.rs:55-58`) gains three methods, each implemented in that OS's
  `platform/<os>.rs` (D7 layer 2):
  `fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion>; fn inno_values(&self, io: &dyn HostIo, key: &PlainName)
  -> RecordRead<InnoValues>; fn pkg_outputs(&self, io: &dyn HostIo, pkg_id: &PlainName) -> RecordRead<PkgOutputs>;`
  - **Windows** (`WindowsPlatform`): `inno_values` → `detect::inno_read_from(io.registry_values(&subkey, [DISPLAY_VERSION,
    INSTALL_LOCATION]))` with `subkey` = `UNINSTALL_KEY` + `\` + `key.as_str()` (the 64-bit view of an admin-mode 64-bit
    Inno install, `setup.iss:50`, `:64`; per_os_install_mechanics.md:78, :139); `os_version` →
    `io.rtl_version().and_then(|(a, b, c)| detect::windows_version(a, b, c))`; `pkg_outputs` → `Absent` with no `io`
    call.
  - **macOS** (`MacosPlatform`): `pkg_outputs` → `detect::pkg_read_from(io.run(&[PKGUTIL, PKG_INFO_FLAG, id],
    PKGUTIL_WAIT), || io.run(&[PKGUTIL, PKG_FILES_FLAG, id], PKGUTIL_WAIT))` with `id = pkg_id.as_str()` as its own argv
    element (no shell) and `PKGUTIL_WAIT` = 5 s per run; `os_version` → `detect::os_version_from_run(io.run(&[SW_VERS,
    SW_VERS_FLAG], OS_VERSION_WAIT), detect::parse_product_version)` with `OS_VERSION_WAIT` = 2 s; `inno_values` →
    `Absent` with no `io` call.
  - **Linux** (`LinuxPlatform`): `os_version` → `detect::os_version_from_run(io.run(&[GETCONF, GLIBC_NAME],
    OS_VERSION_WAIT), detect::parse_glibc_version)`; both record methods → `Absent` with no `io` call (the manifest is
    read by core, FR-028).
- **FR-031** `platform/mod.rs` declares the raw seam (D7 layer 1), with no decision in it:
  `pub trait HostIo { fn run(&self, argv: &[&str], bound: Duration) -> RunOutcome; fn registry_values(&self, subkey:
  &str, names: [&str; 2]) -> RegistryRead; fn rtl_version(&self) -> Option<(u32, u32, u32)>; }`;
  `pub enum RunOutcome { Output(String), NoOutput, Abandoned }`: `Output` when the child exited 0 within the bound and
  its stdout was read; `Abandoned` when the bound expired (the child is left to the reaper, never killed);
  `NoOutput` otherwise. `NoOutput` folds a spawn failure, a non-zero exit and a stdout read error together because
  `run_bounded` returns the same `outputs: None, abandoned: []` for all three (`sysproxy.rs:204-206`, `:216-222`), and
  Phase 6 does not change that Phase 5 primitive. Consequence, stated: on macOS a missing or unrunnable
  `/usr/sbin/pkgutil` reads as "no receipt" (`Absent`), the same as a real "No receipt" exit. That costs nothing here,
  because a record never changes the installed state (FR-029), and `pkgutil` is the base-system receipt tool
  (`/usr/sbin`, pkgutil(1): "Always use pkgutil to query or modify them"). Phase 10, which acts on the receipt, has the
  helper re-read it itself (`uninstall_pkg`, roadmap lines 721-726).
  `pub enum RegistryRead { NoKey, Values([Option<String>; 2]), Failed }`: `NoKey` when the key does not exist; `Values`
  in `names` order, `None` for a value that does not exist; `Failed` for any other error.
- **FR-032** `src-tauri/src/os_facts.rs` holds `pub struct OsHostIo;` and its `impl HostIo`, raw glue only (no `match`
  on a `RunOutcome`, no version parsing, no path building):
  - `run`: `Command::new(argv[0]).args(&argv[1..])` handed to `sysproxy::run_bounded(vec![command], bound)`;
    `outputs: Some([text])` → `Output(text)`; a non-empty `abandoned` → `Abandoned`; else `NoOutput`.
  - `registry_values` (Windows; elsewhere `Failed`, never called because no non-Windows `Platform` calls it):
    `RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &mut hkey)`; `ERROR_FILE_NOT_FOUND` →
    `NoKey`; any other non-`ERROR_SUCCESS` → `Failed`. On success, for each name, `RegGetValueW(hkey, null, name,
    RRF_RT_REG_SZ, null, null, &mut size)` (size query), then the read into a buffer of that size;
    `ERROR_FILE_NOT_FOUND` → `None`; any other error → `Failed`; `RegCloseKey(hkey)` on every path after a successful
    open. No registry write API is named in the file.
  - `rtl_version` (Windows; elsewhere `None`): `RtlGetVersion` with `dwOSVersionInfoSize` set; `STATUS_SUCCESS` →
    `Some((dwMajorVersion, dwMinorVersion, dwBuildNumber))`, else `None`.
- **FR-033** The argv and registry names are constants of their `platform/<os>.rs` file, the only place they are spelled:
  `windows.rs` `UNINSTALL_KEY = "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"`, `DISPLAY_VERSION =
  "DisplayVersion"`, `INSTALL_LOCATION = "InstallLocation"`; `macos.rs` `PKGUTIL = "/usr/sbin/pkgutil"`,
  `PKG_INFO_FLAG = "--pkg-info"`, `PKG_FILES_FLAG = "--files"`, `SW_VERS = "/usr/bin/sw_vers"`, `SW_VERS_FLAG =
  "-productVersion"`; `linux.rs` `GETCONF = "/usr/bin/getconf"` (plan SI-19: run by absolute path like the macOS tools;
  glibc's `getconf` is installed there by Debian / Ubuntu `libc-bin` and Fedora `glibc-common`; where it is absent the
  run is `NoOutput`, so `None`, which never gates, Phase 5 FR-037), `GLIBC_NAME = "GNU_LIBC_VERSION"`. Each is `pub` and
  re-exported by `platform/mod.rs` in that OS's existing `pub use` line beside `ENV_KEYS` (`:63`, `:67`, `:71`), so a
  test on that OS names it as `platform::PKGUTIL` and so on (the per-OS modules are private). `injection.rs`
  `PLATFORM_LITERALS` gains exactly these ten literals (37 entries), so `core_sources_hold_no_production_literal`
  stays green with them and still reports any other literal in `platform/`. The scanner compares a literal's source
  text, so `UNINSTALL_KEY`'s `PLATFORM_LITERALS` entry is its doubled-backslash source form
  (`"SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall"` in `injection.rs` source), and `windows.rs`
  builds the subkey `UNINSTALL_KEY` + `\` + key with `String::push_str` / `push('\\')`, adding no string literal (plan
  SI-8, PD-4). They sit inside `platform/`, where the
  `FORBIDDEN_OUTSIDE_PLATFORM` substrings (`"/usr/`) do not apply (`injection.rs:996-1002`).
- **FR-034** Pure mappers in `detect.rs` (D7; compiled and tested on every host):
  `inno_read_from(RegistryRead) -> RecordRead<InnoValues>`: `NoKey` → `Absent`; `Values([d, l])` → `Found(InnoValues {
  display_version: d, install_location: l })` (both `None` included); `Failed` → `Failed(E_RECORD_READ)`.
  `pkg_read_from(info: RunOutcome, files: impl FnOnce() -> RunOutcome) -> RecordRead<PkgOutputs>`: `info` `NoOutput` →
  `Absent` (`files` not called); `info` `Abandoned` → `Failed(E_RECORD_TIMEOUT)` (`files` not called); `info`
  `Output(i)`, then `files()`: `Output(f)` → `Found(PkgOutputs { pkg_info: i, files: f })`, `Abandoned` →
  `Failed(E_RECORD_TIMEOUT)`, `NoOutput` → `Failed(E_RECORD_READ)` (the receipt exists but its listing failed).
  `os_version_from_run(RunOutcome, parse: fn(&str) -> Option<OsVersion>) -> Option<OsVersion>`: `Output(t)` →
  `parse(&t)`; otherwise `None`.

### E. The running OS version (roadmap lines 576-577; D8)

- **FR-035** Pure parsers in `detect.rs`: `parse_product_version(&str) -> Option<OsVersion>` (the trimmed single line
  through `OsVersion::parse`, e.g. `15.7.9`, `26.0`); `parse_glibc_version(&str) -> Option<OsVersion>` (the trimmed text
  must start with `glibc `; the rest through `OsVersion::parse`, e.g. `glibc 2.39` → `2.39`);
  `windows_version(major: u32, minor: u32, build: u32) -> Option<OsVersion>` (`major.minor.build`).
- **FR-036** The running OS version is read through `Platform::os_version(io)` (FR-030): macOS `sw_vers
  -productVersion` and Linux `getconf GNU_LIBC_VERSION` through `HostIo::run` with a 2 s bound, Windows through
  `HostIo::rtl_version`, each mapped by FR-034 / FR-035. `NoOutput`, `Abandoned`, an unparseable text or a failed
  `RtlGetVersion` → `None`. In production `host.os_version()` is `PlatformFacts { io: &OsHostIo }`, so the value comes
  from `NativePlatform` (roadmap line 576).
- **FR-037** `status::build` calls `host.os_version()` once and passes it, with `NativePlatform.os()` and
  `Arch::native()`, to `assemble`, which calls `version::resolve(entry, os, arch, running.as_ref())` for every entry (the
  same values `resolve_native` would use, Phase 5 FR-036; taking them as parameters lets the pure tests cover every
  `(Os, Arch)` pair); `None` never gates (Phase 5 FR-037).

### F. The status view, `core::status` (roadmap lines 573-577; D1, D10, D11)

- **FR-040** `status::build(ctx: &AppContext, mode: FetchMode, now_unix: u64, host: &dyn HostFacts) -> StatusView`:
  (1) `feed::check(ctx, mode, now_unix)` (Phase 5); (2) `running = host.os_version()`; (3) for each plugin of the
  snapshot's feed (none when there is no snapshot), `detect::detect_plugin(NativePlatform.os(), ctx, entry, host, &listing)`, one after the other on the calling thread in
feed order (no parallel detection, ruled Q4), where `listing` is built once before the loop by `Listing::scan` over
`ctx.roots.plugin_roots()` and the `vst3_bundle` names of the entries that have install facts (FR-006);
  (4) `assemble`. It never panics on production `Roots` (the only panic is FR-005's, on a sandboxed test `Roots`) and
  writes nothing (the feed cache write is Phase 5's).
- **FR-041** `status::assemble(os: Os, arch: Arch, check: &FeedCheck, running: Option<OsVersion>, detections:
  &BTreeMap<String, Detection>) -> StatusView` is pure, with the availability taken through `version::resolve(entry, os,
  arch, running.as_ref())`.
- **FR-042** `detect::detect_plugin(os, ctx, entry, host, listing)` returns `Detection::NotChecked` when `entry.release` is `None` or its `install` is
  `None` (D3); otherwise `Detection::Checked { copies: Vec<DetectedCopy>, record: Option<InstallerRecord>, record_error:
  Option<&'static str>, managed_rank: Option<u8> }` (`managed_rank`: the managed VST3 candidate's `rank`, copied from
  the candidate whether or not a copy sits there, plan SI-13), with the record read first (its `InstallLocation` feeds FR-001) and the copies in candidate
  order, the nested copies of FR-006 last. `linux_managed_root` is `ctx.settings.linux_plugin_root_or_default(&ctx.roots)`.
- **FR-043** Installed state (D1, ruled Q1): `NotChecked` → none; otherwise the managed VST3 copy decides:
  `Known { version, .. }` → `Installed::Version(version)`; `Unknown` → `Installed::VersionUnknown`; absent → the view
  value `elsewhere` when at least one other VST3 copy exists (every `format: vst3`, `managed: false` copy, fixed or
  nested; AU copies never count), else `Installed::No`. For `elsewhere` the displayed version is that of the first such
  copy, in candidate order, whose version is `Known` (null when none is), and `version::compare` is given
  `Installed::No`, so the update state is `not_installed` ("Available to install"): an install adds the managed copy,
  and Phases 9, 10 and 12 act on the managed location only. Otherwise the update state is
  `version::compare(installed, entry.version)` (`InstalledNewer` when the feed offers an older version: the downgrade
  case). AU copies are never the managed copy for this rule.
- **FR-044** Per copy `loads_before_managed`: both the copy and the managed location have a `rank` and the copy's is
  lower (D11); the managed location's rank is `Detection::Checked.managed_rank` (FR-042), so the flag holds when no copy
  sits at the managed location (plan SI-13). Per plugin: `record_without_files` (a record exists and the managed VST3 copy is absent);
  `record_version_differs` (the record's `version` and the managed copy's known version are both present and differ).
- **FR-045** Availability (Phase 5 `Availability`) maps to `available`, `not_through_manager`, `not_for_this_machine`,
  `needs_newer_os` (+ `min_os` text). Availability and the update state are independent (Phase 5 FR-038); the UI rule is
  FR-071.
- **FR-046** `StatusView` serialises (`serde`, snake_case keys, `null` for absent optionals, arrays never omitted) to:
  `{ "os": "windows"|"macos"|"linux", "edge_max_age_secs": 600, "running_os": "<ver>"|null,
  "feed": { "source": "fresh"|"cached"|"none", "fetched_at_unix": <u64>|null, "schema_version": <u8>|null,
  "error": "<FeedError Display>"|null }, "plugins": [ { "name", "latest": "<version_text>",
  "availability": "available"|"not_through_manager"|"not_for_this_machine"|"needs_newer_os", "min_os": "<ver>"|null,
  "installed": "not_checked"|"not_installed"|"elsewhere"|"version_unknown"|"version", "installed_version":
  "<ver>"|null (the version of FR-043 for `version` and `elsewhere`),
  "update": "not_installed"|"up_to_date"|"update_available"|"installed_newer"|"unknown"|null,
  "copies": [ { "format": "vst3"|"au", "place": "configured"|"vst3_user"|"vst3_global"|"au_user"|"au_global"|"installer_location",
  "vendor_folder": <bool>, "nested": <bool>, "path": "<display path>", "managed": <bool>, "loads_before_managed": <bool>,
  "version": "<ver>"|null, "version_source": "classes"|"top_level"|"au_plist"|null,
  "unknown_reason": "single_file"|"no_moduleinfo"|"too_large"|"unreadable"|"corrupt"|"classes_disagree"|"no_version"|"not_a_bundle"|"au_no_version"|null } ],
  "record": { "kind": "inno"|"pkg"|"manifest", "version": "<text>"|null, "files": <u64>|null }|null,
  "record_error": "<code>"|null, "record_without_files": <bool>, "record_version_differs": <bool>,
  "release_page_url": "<https url>"|null, "notes": [<Block>…] } ] }`. Plugins appear in the feed model's key order (Phase 5 FR-027). `path` is
  `Path::display()` of the candidate (lossy only for non-UTF-8 names). A nested copy (FR-006) carries its root's kind as
  `place` and `nested: true`; `release_page_url` is FR-049's value.
- **FR-047** Logging: one `info!` per `build` on target `krate_manager_core::status`: `status: <n> plugins, <m> copies,
  feed <fresh|cached|none>`, plus the `warn!` lines of FR-003, FR-004, FR-006, FR-011 to FR-013, FR-020 and FR-029 and the one
  `debug!` of FR-013(d) (one per event). No log line carries the content of a moduleinfo, a plist or a receipt beyond
  the version texts named (SC-005, SC-007, SC-013 plant a marker string to prove it).
- **FR-048** `build` holds no lock; two concurrent builds each return a complete view (detection reads only;
  `status::tests::concurrent_builds`, SC-018).
- **FR-049** (ruled Q8) `status::release_page_url(text: &str) -> Option<String>` is pure: `url::Url::parse(text)`
  succeeds; the scheme is `https`; `host_str()` equals `RELEASE_PAGE_HOST` (`"github.com"`, a constant of `status.rs`,
  compared after the `url` crate's lower-casing); `username()` is empty and `password()` is `None`; `port()` is `None`
  (an explicit default port is normalised away by the parser, any other port is refused); `text.len() <= 2048`; the
  result is `url.as_str()`. `assemble` sets each plugin's `release_page_url` to it applied to
  `entry.release.release_notes_url`, and to `null` when the entry has no release. No other field of the view carries a
  URL.

### G. Release notes, `core::notes` (roadmap line 574; D9)

- **FR-050** `notes::blocks(markdown: &str) -> Vec<Block>` parses with `pulldown_cmark::Parser::new_ext(markdown,
  Options::empty())`.
- **FR-051** The model (`serde`, internally tagged `"type"`): `Block::{Heading { level: u8, inlines }, Paragraph {
  inlines }, List { ordered: bool, start: Option<u64>, items: Vec<Vec<Block>> }, Quote { blocks }, Code { text },
  Rule}`; `Inline::{Text { text }, Strong { inlines }, Emphasis { inlines }, Code { text }, Break}`.
- **FR-052** Mapping: `Heading` keeps its level (1-6); `Paragraph`; `List(None)` → unordered, `List(Some(n))` → ordered
  with `start` = `Some(n)` when `n != 1`, else `None`; an `Item`'s inline content outside a paragraph (a tight list) is
  wrapped in one `Paragraph`; `BlockQuote` → `Quote`; `CodeBlock` → `Code` with the text verbatim; `HtmlBlock`'s text →
  a `Paragraph` of literal `Text`; `Rule`; `Emphasis`, `Strong`; `Event::Code` → `Inline::Code`; `Text` → `Text`;
  `Html` / `InlineHtml` → literal `Text`; `SoftBreak` → `Text { " " }`; `HardBreak` → `Break`; `Link` → its child
  inlines only (the destination is dropped); `Image` → its alt-text inlines only. Any other event (none occurs under
  `Options::empty()`) contributes its text, if any, as `Text`. Adjacent `Text` inlines are merged.
- **FR-053** Bounds: a `notes_markdown` longer than `MAX_NOTES_BYTES` = 65,536 bytes gives `[Paragraph [Text "Release
  notes are too long to show here."]]` without parsing; block containers (lists, quotes) nested deeper than
  `MAX_DEPTH` = 8 and inline containers deeper than 8 are flattened: their content joins the innermost allowed container
  as plain text.
- **FR-054** An absent `notes_markdown`, or one that is empty after trimming, gives `notes: []`.

### H. Tauri commands and capability (roadmap line 573; D10, D13)

- **FR-060** `src-tauri/src/commands.rs` gains `#[tauri::command] pub async fn plugin_status(state: State<'_,
  SharedState>) -> Result<StatusView, String>` and `check_now` (same signature): read the `SharedState` guard, clone the
  `AppContext` (or return `Err` with the `StartupError` `Display` when the context is `Err`), drop the guard, then
  `tauri::async_runtime::spawn_blocking(move || status::build(&ctx, mode, now_unix, &PlatformFacts { io: &OsHostIo }))` with
  `FetchMode::Normal` / `FetchMode::CheckNow` and `now_unix` from `SystemTime::now()`; a join error is
  `Err("E_STATUS_TASK: …")`. The commands hold no logic beyond this glue (`src-tauri` has `test = false`).
- **FR-061** `build.rs` registers `commands(&["app_version", "startup_error", "plugin_status", "check_now",
  "open_release_page"])`; `capabilities/main.json` `permissions` is exactly `["allow-app-version",
  "allow-startup-error", "allow-plugin-status", "allow-check-now", "allow-open-release-page"]` and its `description` is
  exactly `The main window may call only the app's own commands.` (plan SI-21; `real_src_tauri_config_passes_the_guard`
  compares it exactly); `generate_handler!` lists exactly those five commands.
- **FR-062** `tauri.conf.json` (CSP, window, bundle), the platform conf files, `index.html` and the navigation guard
  are unchanged.
- **FR-063** `crates/core/tests/config_guard.rs` expects FR-061's five commands and five permissions; every other
  Phase 4 / Phase 5 assertion stands except one narrowing ruled by Q8 (plan SI-2): the `src-tauri/Cargo.toml`
  `[dependencies]` key set gains `tauri-plugin-opener` (six keys), and the only accepted line containing `tauri-plugin`
  is exactly `tauri-plugin-opener = { workspace = true }` in `[dependencies]`; any other `tauri-plugin*` line, or that
  crate in another table, still fails; the existing `[dependencies]` plant changes to `tauri-plugin-fs = "2"` (reported
  twice, as now) and the `[build-dependencies]` plant stays. Its existing self-test `planted_config_changes_fail_the_guard` gains a planted
  sixth permission, a planted `"allow-fs-read"` and a planted `"opener:allow-open-url"` and reports all three, and
  `real_src_tauri_config_passes_the_guard` passes on the real tree (both names are already in `cargo.all`).
- **FR-064** (ruled Q8) `src-tauri/src/commands.rs` gains `#[tauri::command] pub fn open_release_page(url: String) ->
  Result<(), String>` (no `AppHandle`, which would be an unused parameter, plan SI-15): `status::release_page_url(&url)`
  is `None` → `Err("E_OPEN_REJECTED")` and nothing is opened; `Some(valid)` → `tauri_plugin_opener::open_url(valid,
  None::<&str>)`, the crate-root free function (plan SI-15), whose error becomes `Err("E_OPEN_FAILED: …")`. It opens
  only the validated string, never `url` as received, and holds no other logic. `lib.rs` lists it in
  `generate_handler!` and registers no opener plugin (plan SI-15). The decision, the host check, is in core and tested there (SC-032); the glue
  has no test, like the other commands (`src-tauri` has `test = false`).
- **FR-065** (ruled Q8) Dependency and confinement of the opener. The workspace `Cargo.toml` pins `tauri-plugin-opener =
  "2.7.2"`; `src-tauri/Cargo.toml` `[dependencies]` uses it; `Cargo.lock` gains its dependencies; neither
  `crates/core` nor `crates/helper` depends on it, and `package.json` gains no `@tauri-apps/plugin-opener` (the npm 2.7.2
  counterpart is not used: the webview never calls the opener). The `src-tauri/Cargo.toml` entry is the one accepted
  `tauri-plugin` line of FR-063 (plan SI-2). The identifier `tauri_plugin_opener` occurs only in
  `src-tauri/src/commands.rs`; no source holds the token sequences `tauri_plugin_opener :: init` or
  `tauri_plugin_opener :: Builder`, or the identifier `OpenerExt` (plan SI-15); `capabilities/main.json` holds no permission that starts with
  `opener:`; `src/` holds no import of `@tauri-apps/plugin-opener`; `tauri.conf.json` is unchanged. Core's dependency
  tree stays free of the crate (FR-080's step covers `krate-manager-core`). Guard
  `opener_is_confined_and_not_granted_to_the_webview` (in `config_guard.rs`) with its self-test
  `opener_guard_reports_plants` (a planted `"opener:allow-open-url"` permission, a planted `tauri_plugin_opener` in
  `status.rs`, a planted npm import and a planted `.plugin(tauri_plugin_opener::init())` in `commands.rs` (plan
  SI-15), each reported). `LIB_RS_STRINGS` is unchanged (14 entries, plan SI-15).

### I. The status screen (roadmap lines 573-577, 588; D9, D13)

- **FR-070** `src/main.ts` keeps Phase 4's flow (title; `startup_error` → the notice alone when it returns text;
  otherwise the version line) and then appends the status section and calls `invoke<StatusView>("plugin_status")`. Until
  that call settles the section shows the fixed text `Checking for plugins…` (an `ACTION_TEXTS` entry, FR-071; ruled
  Q4: the first render may wait on the network, up to the 30 s `TOTAL_TIMEOUT` offline, and on `pkgutil` on macOS) and
  the Check now button (FR-073) is disabled; a result, or the FR-074 rejection text, replaces the pending text. There is
  no cache-first command and no second status call at load: the call list of an unlocked start is exactly
  `startup_error`, `app_version`, `plugin_status`. The locked-instance path calls neither status command. Phase 4's
  three required vitest cases keep their titles; their expected call lists gain `"plugin_status"` after `"app_version"`
  (the locked case stays `["startup_error"]`).
- **FR-071** One card per `plugins` entry, in order, each an `<article class="plugin">` holding, as text: an `h2` with
  the name; `Installed: ` + the installed text; `Latest: ` + `latest`; one state line; the copies list; the record
  notices; the `Open release page` button when `release_page_url` is not null; the notes in a collapsed `details` (FR-072).
The exact strings (the vitest assertions):

  | Field | Value → text |
  |---|---|
  | installed | `not_checked` → `Not checked`; `not_installed` → `Not installed`; `elsewhere` → `Installed outside the manager's location: <installed_version>`, with the
word `unknown` in place of the version when `installed_version` is null; `version_unknown` → `Installed, version unknown`; `version` → `<installed_version>` |
  | state line, availability not `available` | `not_through_manager` → `Not available through the manager`; `not_for_this_machine` → `Not available for this Mac` when `os` is `macos`, else `Not available for this computer`; `needs_newer_os` → `Needs macOS <min_os> or newer` / `Needs glibc <min_os> or newer` / `Needs Windows <min_os> or newer` by `os` |
  | state line, availability `available` | `update`: `not_installed` → `Available to install`; `up_to_date` → `Up to date`; `update_available` → `Update available`; `installed_newer` → `Installed version is newer than the latest release`; `unknown` → `Versions cannot be compared` |
  | copy | `Found: <path>` + ` (<version>)`, ` (version unknown: <reason text>)`; `managed` adds ` — installer location`; `loads_before_managed` adds ` — hosts that follow Steinberg's scan order load this copy first`; `format` `au` prefixes `AU: ` |
  | reason text | `single_file` single-file plug-in; `no_moduleinfo` no moduleinfo.json; `too_large` moduleinfo.json too large; `unreadable` cannot be read; `corrupt` unreadable moduleinfo; `classes_disagree` class versions disagree; `no_version` no version recorded; `not_a_bundle` not a bundle; `au_no_version` version not recorded in the AU bundle |
  | record notices | `record_without_files` → `The installer's record lists version <record.version>, but its files are missing.`; `record_version_differs` → `The installer's record says <record.version>; the installed files say <installed_version>.`; `record_error` → `The installer's record could not be read (<code>).` |
  | release page button | `Open release page` (shown only when `release_page_url` is not null) |

  No card shows an Install, Update or Remove control (roadmap line 574: read-only). `src/status.ts` exports this table
  as `STATUS_TEXTS`: one entry per cell (each value of each row, each OS variant of `not_for_this_machine` and
  `needs_newer_os`, each copy suffix, each reason, each notice), holding the template with `{…}` placeholders. The
  renderer builds every one of these strings from `STATUS_TEXTS` only, so the table and the code cannot drift. The texts no golden can reach live in a separate `ACTION_TEXTS` table outside
  the coverage case: the pending text `Checking for plugins…` (FR-070), `Checking…` (FR-073) and the open-failure text
  `Could not open the release page: <rejection text>` (FR-078); SC-034 and SC-035 assert them.
- **FR-072** Notes (ruled Q7): one `details` element per card, closed by default (the renderer never sets `open`), whose
  first child is a `summary` with the text `Release notes` (a `STATUS_TEXTS` entry) and whose other children are the
  blocks rendered with `createElement` / `textContent` only: `Heading` level L → `h{min(L + 2, 6)}`; `Paragraph` →
  `p`; `List` → `ul` / `ol` (`start` set as a property when present) with `li` per item; `Quote` → `blockquote`; `Code`
  → `pre > code`; `Rule` → `hr`; `Strong` → `strong`; `Emphasis` → `em`; inline `Code` → `code`; `Break` → `br`.
  `notes: []` → a `p` reading `No release notes.` inside the `details`.
- **FR-073** The check control: a `button` `Check now` and beside it the text `A new release can take up to <M> minutes
  to appear.` with `M = Math.ceil(edge_max_age_secs / 60)` (10 for 600). A click disables the button, sets its text to
  `Checking…`, calls `invoke<StatusView>("check_now")`, re-renders the section from the result and re-enables the button
  (also after a rejection). A second click while one is pending does nothing. The button starts disabled and is enabled when the first
  `plugin_status` call settles, by a result or a rejection (FR-070, ruled Q4). A rejected `check_now` keeps the previous
  cards and replaces only the feed line with `Could not load the plugin list: <rejection text>` (plan SI-11, PD-13; Q-A).
  The ten-minute note is rendered with the first view (its value comes from `edge_max_age_secs`) and is absent before it
  (plan SI-11, PD-13).
- **FR-074** Feed line: `fresh` → `Checked <time>`; `cached` → `Could not refresh the list: <error>. Showing the copy
  from <time>.`; `none` → `Could not load the plugin list: <error>`; `<time>` = `new Date(fetched_at_unix * 1000)
  .toLocaleString()`. A rejected command shows `Could not load the plugin list: <rejection text>`; a rejected first
  `plugin_status` shows that line and no cards, and a rejected `check_now` keeps the previous cards (FR-073) (plan
  SI-11, PD-13).
- **FR-075** No element the status section renders is an `a`, `img`, `iframe`, `object`, `embed`, `link`, `script` or
  `style` (`details` and `summary` are allowed, ruled Q7), and no element carries an attribute other than `class`,
  `start` (on `ol` only), `disabled` (on a `button` only) and `open` (on `details` only; the renderer never sets it, only
  the user's toggle does); the only attributes set from view data are `ol.start` and `button.disabled`; every view string
  reaches the DOM through `textContent`; the release-page URL is held in a closure, never in an attribute. Phase 4's
  lint ban and `config_guard` source scan (`config_guard.rs:870`) stay green.
- **FR-076** `src/view.ts` declares the `StatusView` types of FR-046 and FR-051; `vitest` imports the golden fixtures of
  FR-086 with `resolveJsonModule` and passes each through `toStatusView` (`src/view.ts`), which rejects a missing key, a
  wrong type or an unknown enum value with a `TypeError` naming the path, so a drifted fixture fails `npm test` (plan
  SI-4).
- **FR-077** `src/styles.css` gains rules for the section only; no font, image or URL.
- **FR-078** (ruled Q8) The release-page button: a `button` with the text `Open release page` (a `STATUS_TEXTS` entry),
  rendered in the card only when `release_page_url` is a string. A click disables that button, calls
  `invoke("open_release_page", { url: release_page_url })` and re-enables it when the call settles; a second click while
  pending does nothing; a rejection shows `Could not open the release page: <rejection text>` (an `ACTION_TEXTS` entry)
  in one `p` of the card, replaced by the next click's outcome. The button is the only way the window navigates: the
  non-test `src/*.ts` files hold no `window.open`, no `location` assignment, no `href` and no import of
  `@tauri-apps/plugin-opener` (a source scan inside SC-034).

### J. Dependencies, guards and lanes (D12; roadmap lines 585, 1003-1007)

- **FR-080** `crates/core/Cargo.toml`: `[dependencies]` gains `pulldown-cmark` (workspace pin `{ version = "0.13.6",
  default-features = false }`); the macOS target table becomes `core-foundation` and `plist` (pin `{ version = "1.10.3",
  default-features = false }`). `injection.rs` `FR007_DEPENDENCIES` gains `pulldown-cmark` (11 names) and the macOS-table
  assertion becomes exactly those two names. Core's normal dependency tree still holds no `tauri*`, `tokio`, `reqwest`,
  `hyper`, `dirs*`, `directories*` or `home` crate, checked on every leg by a new `manager-ci.yml` step `core dependency
  tree`, placed right after `cargo test`. It runs `cargo tree -p krate-manager-core -e normal --locked --prefix none
  --format "{p}"` (host target, so the macOS leg sees the `plist` table) into `$RUNNER_TEMP/core-tree.txt`. It takes
  each line's first word as the crate name and matches it against `^(tauri(-.*)?|tokio|reqwest|hyper|dirs(-.*)?|
  directories(-.*)?|home)$`. Each hit prints `::error title=core dependency tree (<os>)::<crate>`, then `exit 1`;
  otherwise it prints `::notice title=core dependency tree (<os>)::<n> crates, none excluded`, which requires `n` ≥ 1.
  The step is added to `manager-ci-shape.test.js`'s step order, assertion-step list and command lines (FR-090). The
  Windows local run of the same command is recorded in compliance.
- **FR-081** The workspace `windows-sys` pin's features become exactly `Win32_Foundation`, `Win32_Networking_WinHttp`,
  `Win32_System_Registry`, `Win32_System_SystemInformation`, `Wdk_System_SystemServices`; `READER_PINS` follows.
- **FR-082** Confinement (in `injection.rs`; tests `notes_and_plist_crates_are_confined` and
  `host_io_symbols_are_confined`, both covered by the self-test `confinement_scanner_reports_plants`, which plants one
  violation of each rule and requires each to be reported): the path `pulldown_cmark` occurs in code only in
  `crates/core/src/notes.rs` (and `notes_tests.rs`); the path `plist::` only in `detect.rs` inside its
  `#[cfg(target_os = "macos")]` AU reader (and `detect_tests.rs`); the identifiers `RegOpenKeyExW`, `RegGetValueW`,
  `RegCloseKey` and `RtlGetVersion` only in `src-tauri/src/os_facts.rs`; the FR-033 literals `/usr/sbin/pkgutil`,
  `/usr/bin/sw_vers`, `GNU_LIBC_VERSION` and the `UNINSTALL_KEY` text only in their `platform/<os>.rs` file; the
  identifier `OsHostIo` only in `os_facts.rs` and `commands.rs`. `CONFINED_TOKENS`' `windows_sys` row
  (`injection.rs:147`, checked by `ureq_is_confined_to_http_rs`) becomes `[OS_PROXY_RS, OS_FACTS_RS]`, and its existing
  `TAURI_LIB_RS` plant stays reported (plan SI-1).
- **FR-083** Test sources (the `is_test_source` files, `*_tests.rs` under `src` and `crates/*/tests/*.rs`, minus
  `injection.rs`, plus `crates/core/src/test_support.rs` explicitly, because `is_test_source` does not match it and it
  holds the fakes (tasks TA-2)) name `OsHostIo` nowhere and hold
  none of the FR-082 platform literals: a test that checks an argv compares it to
  the FR-033 constants as `platform` re-exports them, never to a re-spelled literal. Test `test_sources_never_name_os_host_io`; self-test
  `test_source_scanner_reports_planted_os_host_io` (a planted `OsHostIo` in a test body and a planted
  `"/usr/sbin/pkgutil"` literal, each reported). `test_support` gains two fakes, each recording every call:
  `FakeHostFacts { os_version, inno: BTreeMap<String, RecordRead<InnoValues>>, pkg: BTreeMap<String,
  RecordRead<PkgOutputs>>, calls: RefCell<Vec<String>> }` (an unlisted key answers `Absent`), which every cross-OS
  detection and status test passes; and `FakeHostIo { runs: BTreeMap<Vec<String>, RunOutcome>, registry:
  BTreeMap<String, RegistryRead>, rtl: Option<(u32, u32, u32)>, calls: RefCell<Vec<String>> }` (an unlisted argv
  answers `NoOutput`, an unlisted subkey `NoKey`), which the per-OS `Platform` tests and `PlatformFacts` tests pass.
- **FR-084** `detect.rs`, `notes.rs` and `status.rs` hold, in code, none of the identifiers `create_dir`,
  `create_dir_all`, `OpenOptions`, `rename`, `remove_file`, `remove_dir`, `remove_dir_all`, `set_permissions`,
  `hard_link`, `symlink`, `Command`, `TcpStream`, `UdpSocket`, `thread::spawn`, `thread::scope`, `rayon` (detection is
  sequential, ruled Q4), nor the paths `fs::write`, `fs::copy` and `File::create` (detection reads; the only network is
  Phase 5's `feed::check`). `read_dir` is held by `detect.rs` alone, exactly once, in FR-006's `list_root` (ruled Q3);
  `notes.rs` and `status.rs` hold none. The same list, `read_dir` included, applies to the non-test `platform/` files
  (none holds any of these identifiers today, grep this session), so the FR-030 host methods reach a process or the
  registry only through `HostIo`; `platform/` keeps its FR-021 filesystem-token ban (`injection.rs:24-39`). Test
  `detect_status_notes_hold_no_write_or_listing_token`; self-test `token_scanner_reports_planted_read_dir` (a planted
  `std::fs::read_dir` in an in-memory copy of `status.rs` is reported, a second `read_dir` in a copy of `detect.rs` is
  reported, a planted `thread::spawn` in `status.rs` is reported, and the real `detect.rs`'s one `read_dir` is not).
- **FR-085** `tests/required-tests.json`: every new Rust test named in SC-002 to SC-027, SC-029 to SC-033 and FR-065, FR-082, FR-083, FR-084,
  FR-089 joins `cargo.all`, except the OS-gated tests, which join their OS list: `windows`:
  `detect::tests::native_layout_windows`, `detect::tests::locked_moduleinfo_windows`,
  `detect::tests::host_facts_through_fake_io_windows`, `detect::tests::listing_case_windows`,
  `detect::tests::listing_skips_junction_windows`; `macos`: `detect::tests::native_layout_macos`,
  `detect::tests::au_plist_cases_macos`, `detect::tests::pkg_receipt_macos`,
  `detect::tests::host_facts_through_fake_io_macos`, `detect::tests::probe_metadata_error_unix`,
  `detect::tests::listing_skips_symlinked_dirs_unix`, `detect::tests::listing_case_exact_unix`; `linux`:
  `detect::tests::native_layout_linux`, `detect::tests::linux_detection_linux`,
  `detect::tests::host_facts_through_fake_io_linux`, `detect::tests::probe_metadata_error_unix`,
  `detect::tests::listing_skips_symlinked_dirs_unix`, `detect::tests::listing_case_exact_unix` (plan SI-3, SI-20,
  §4.14). Every vitest title of
  SC-019, SC-020, SC-034 and SC-035 joins `vitest`.
- **FR-086** Fixtures: the feed fixtures are read in place from `tools/feed-contract/fixtures/feeds/valid/` (Phase 5
  FR-071); bundle trees are built at test time under `fresh_temp_roots()` from small committed `moduleinfo.json` /
  `Info.plist` / `pkgutil` output texts under `apps/krate-manager/tests/fixtures/detect/` (invented plugin names only:
  Alpha … Juliet); golden views under `apps/krate-manager/tests/fixtures/status/<scenario>.json`. No test writes into the
  repo, reads a real plug-in folder, the registry or a receipt, or starts a process other than the test binary, except
  Phase 5's `sleep` / `printf` and, each in exactly one test body acting inside its `TempDir`, `mkfifo` (SC-027) and
  `cmd /C mklink /J` (SC-029) (tasks TA-1).
- **FR-087** The Phase 5 roster guard (`roster.rs:128`) and the "roster absent" CI step keep passing with the new files:
  no plugin name appears in any new non-test source, in the built binary or in `dist/`.
- **FR-088** No new test or step uses `--test-threads=1`, a global mutex, or a fixed port. Enforced by
  `test_sources_hold_no_real_location` (in `injection.rs`) with its self-test `real_location_scanner_reports_plants`.
  They scan exactly `crates/core/src/detect_tests.rs`, `status_tests.rs`, `notes_tests.rs`, `test_support.rs`,
  `src/status.test.ts` and `src/main.test.ts` for the regexes `--test-threads`, `127\.0\.0\.1:[0-9]+`,
  `localhost:[0-9]+`, `OsHostIo`, `HKEY_`, `/Library`, `/usr/`, `%[A-Z_]+%`, `AppData`, `ProgramData` and
  `Program Files`, and require 0 hits; a file it cannot read is a finding (`<file>: missing`), so the guard cannot pass
  vacuously on a renamed file (tasks TA-9). The self-test plants one hit of each regex in an in-memory copy and requires
  each to be reported. (The Phase 4 platform tests, which compute but never touch the real default paths, are not in
  the list: `platform/linux_tests.rs:23-26` and `macos_tests.rs:20-33` spell them on purpose.)
- **FR-089** ROSTER covers AppIds and pkg identifiers too (roadmap line 33: "ships no plugin names, bundle names, AppIds
  or pkg identifiers of its own"). `install_identifiers_absent_from_sources` (in `roster.rs`) scans the non-test sources
  `crates/core/src/detect.rs`, `status.rs`, `notes.rs`, `platform/*.rs` (the non-`_tests` files),
  `src-tauri/src/os_facts.rs`, `commands.rs` and `src/*.ts` (excluding `*.test.ts`). It requires 0 hits of
  `com\.krateaudio\.` outside the one `APP_IDENTIFIER` line of `platform/mod.rs`, 0 hits of the Inno GUID shape
  `\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-`, and 0 hits of `_is1`. Its self-test `install_identifier_scanner_reports_plants`
  plants `"com.krateaudio.alpha"`, `"{0A1F0000-0000-4000-8000-000000000001}_is1"` and a bare `"_is1"` suffix in an
  in-memory copy of `detect.rs` and requires all three to be reported, and requires the real `APP_IDENTIFIER` line not
  to be.

### K. Scope fences

- **FR-090** Files outside `apps/krate-manager/**` this phase may change: `specs/krate-manager-phase6/**`;
  `specs/Krate-manager-roadmap.md`, only the Phase 6 status line and the Phase 10 `run_inno_uninstaller` sentence
(FR-092); `specs/INDEX.md` (FR-091). `manager-ci.yml` gains
  exactly one step, `core dependency tree` (FR-080), and otherwise changes only if a required-tests name list it embeds
  changes. `tools/feed-contract/tests/manager-ci-shape.test.js` gains that step in its step order (`:163-190`), its
  `::notice` / `::error` assertion-step list (`:200-211`) and its command lines (`:246-274`); its `EXPECTED_REQUIRED`
  constant (`:337-…`) gains exactly FR-085's names (the same `cargo.all` / `cargo.windows` / `cargo.macos` /
  `cargo.linux` / `vitest` additions as `tests/required-tests.json`, nothing removed), because `checkRequired`
  (`:739-748`) deep-compares that file with the constant and FR-085 changes it; the file changes in nothing else. Nothing under `plugins/`, `dsp/`, `cmake/`, `extern/`, `tools/feed-contract/` (contracts, schemas, fixtures), and no
  other workflow.
- **FR-091** `specs/INDEX.md` is regenerated with `node tools/gen-specs-index.js`, so `ci.yml`'s `--check`
  (`ci.yml:172`) stays green.
- **FR-092** (ruled Q2, re-ruled Q-C (a)) The roadmap's Phase 10 `run_inno_uninstaller` rule is not widened: the clause
  "requires the executable to be an `unins*.exe` in the plugin's install dir under the compiled-in Windows plug-in root"
  (roadmap lines 716-717) stays unchanged, and this phase adds one sentence to Phase 10 after it: "A managed copy whose
  recorded `InstallLocation` lies outside the compiled-in Windows plug-in root is never uninstalled by the helper: the
  manager shows it as managed and tells the user to uninstall it from Windows Settings." Reason: the HKLM value is
  admin-only, but the folder it names, and the `unins*.exe` / `unins*.dat` in it, can be user-chosen and writable without
  elevation. Nothing else in Phase 10 changes. Verified by SC-028 (plan SI-14).

---

## Success Criteria

Rust SCs run locally on Windows with `cargo test --workspace --locked` from `apps/krate-manager/` (default test
threads); a run counts only with every binary reporting `0 failed`. macOS and Linux rows are **PENDING-CI** on
`manager-ci.yml` jobs `check (macos-latest)` and `check (ubuntu-24.04)` (Phase 4 FR-077). A row closes on a leg only
when all three of these hold. (1) The committed `tests/required-tests.json` of the run's commit lists the row's test
names in `cargo.all` or `cargo.<os>` (Rust) or in `vitest` (vitest). (2) The leg's `required tests ran` step passed and
printed no `::error title=required tests ran (<os>)::…` line; each such line names one missing test
(`manager-ci.yml:159`). (3) The leg's `::notice title=required tests ran (<os>)::<n> cargo + <m> vitest` reads `n` =
`len(cargo.all) + len(cargo.<os>)` and `m` = `len(vitest)` of that same file (`:161`). The notice carries counts only,
never names. Its pass condition is exactly one `test <name> ... ok` line per listed name (`:147-157`), so (1) and (2)
together are what prove each named test ran. Rows backed by a different step name that step. Vitest SCs run with
`npm test`.

- **SC-001 (Windows local gate, zero warnings).** From `apps/krate-manager/`: `cargo fmt --all --check`; `cargo clippy
  --workspace --all-targets --locked -- -D warnings`; `cargo clippy -p krate-manager --all-targets --features dev-feed
  --locked -- -D warnings`; `cargo test --workspace --locked`; `npm run typecheck`, `npm run lint`, `npm test`; `node
  ../../tools/lint-manager-version.js`; `npm run tauri build`. Threshold: every command exit 0, 0 `warning:` lines, no
  `#[allow` / `@ts-ignore` / `eslint-disable` added without a reason; `git status --porcelain` after the lane, minus
  `specs/krate-manager-phase6/evidence/`, equals the snapshot before it. PENDING-CI: the same steps on the macOS and
  Linux `check` legs (`clippy`, `cargo test`, `tauri build` steps green).
- **SC-002 (candidate table, all three OSes on every host).** `detect::tests::candidate_table`: for `full-v2.json`'s
  Alpha and Bravo `install` blocks and `tenth-plugin.json`'s Delta (install `{"vst3_bundle": "Delta.vst3"}` only, its
  one platform `linux-x86_64`), with synthetic root lists for each `Os` (Windows `[Vst3User, Vst3Global]`; macOS
  `[Vst3User, Vst3Global, AuUser, AuGlobal]`; Linux `[Configured, Vst3User, Vst3Global ×4]` and `[Vst3User, Vst3Global
  ×4]`). The Windows rows run without an install location and with three install locations: (a) `<vst3-global>/krate
  audio/ALPHA` (the default vendor folder in other letter case): no extra candidate, the default vendor candidate
  managed; (b) `<vst3-global>/Krate Audio/Custom`: an extra `InstallerLocation` candidate, managed, and the default
  vendor candidate not managed; (c) `<base>/elsewhere`: an extra `InstallerLocation` candidate, managed, `rank: None`, and the default vendor
  candidate not managed (ruled Q2: any usable recorded location is the managed one). The returned candidates (path, place, managed, rank, order)
  equal the FR-001 table exactly, and Delta on macOS (no `au_component`) yields no AU candidate. Paths, on every host:
  every synthetic root and install location is a host-native absolute path built by `Path::join` from one temp base
  (`<base>` above), and every expected path is built with the same `Path::join` from that base, never written as a
  literal (the test holds no `C:\` or `/usr/` literal; FR-088's guard checks the latter). Case (a)'s letter-case
  match goes through `same_path` (FR-001). Threshold: every cell. Local; PENDING-CI (`all`).
- **SC-003 (native layout per OS).** `detect::tests::native_layout_windows` / `_macos` / `_linux` (each gated on its
  `target_os`): over `fresh_temp_roots()` with `ctx.roots.plugin_roots()` as the root list, the managed candidate is
  `vst3-global/Krate Audio/Alpha/Alpha.vst3` (Windows), `vst3-global/Alpha.vst3` + `au-global/Alpha.component`
  (macOS), `vst3-user/Alpha.vst3` and, with the configured root `<base>/custom`, `custom/Alpha.vst3` (Linux). The Linux
  configured-root context is built through `context::reassemble_with_os(Os::Linux, &prev, Settings {
  linux_plugin_root: Some(<base>/custom), .. })` (`context.rs:142`), so `plugin_roots` holds the `Configured` root as
  `fold` puts it (`context.rs:124-132`); no test sets `ctx.settings` by hand. Local (Windows); PENDING-CI `check
  (macos-latest)` (`macos` list), `check (ubuntu-24.04)` (`linux` list).
- **SC-004 (the moduleinfo grammar).** `detect::tests::trailing_comma_cases`: `{"a":[1,2,],}` → parses; a `,]` inside a
  string value is unchanged; `\"` escapes inside strings honoured; `[,]`, `{,}`, `[1,,]` (plan SI-16, SI-17), a `//`
  comment, a single-quoted string and an unquoted key → `Corrupt`; the committed copy of the Iterum-shaped fixture (top `1.0.0`, classes `1.0.0.1`,
  trailing commas as `moduleinfo.json:12-13`) parses. Threshold: every case. Local; PENDING-CI (`all`).
- **SC-005 (the installed-state table, roadmap lines 580-584).** `detect::tests::installed_state_table` builds each row
  under its own `fresh_temp_roots()` (every row is Alpha, so each plugin keeps its one managed location, FR-001, and
  FR-086's names suffice). For each row it checks the managed copy's `CopyVersion` and the log lines of the run
  (capture logger, `take_mine()`, which records every level, `test_support.rs:171-176`):

  | Fixture | Result | `warn!` | `debug!` `E_DETECT_TOP_LEVEL` |
  |---|---|---|---|
  | current bundle: top `1.2.0`, classes `1.2.0.1` ×2 | `Known 1.2.0, Classes` | none | none |
  | legacy: top `0.8.0`, classes `1.0.4.0` | `Known 1.0.4, Classes` | none | one, naming `0.8.0` and `1.0.4` |
  | top `1.0.3`, class `1.0.4.0` | `Known 1.0.4, Classes` | one `E_DETECT_TOP_LEVEL` naming `1.0.3` and `1.0.4` | none |
  | a real 0.8.0 release: top `0.8.0`, class `0.8.0.3` | `Known 0.8.0, Classes` | none | none |
  | classes `1.0.4.0` and `1.0.5.0` | `Unknown(ClassesDisagree)` | one `E_DETECT_CLASSES` | none |
  | class `1.0.4.x` | `Unknown(ClassesDisagree)` | one `E_DETECT_CLASSES` | none |
  | no class `Version`, top `1.2.3` | `Known 1.2.3, TopLevel` | none | none |
  | no class `Version`, no top-level | `Unknown(NoVersion)` | none | none |
  | corrupt moduleinfo (truncated) | `Unknown(Corrupt)` | one `E_DETECT_MODULEINFO` | none |
  | moduleinfo of 1 MiB + 1 byte | `Unknown(TooLarge)` | one `E_DETECT_MODULEINFO` | none |
  | bundle directory without `Contents/Resources/moduleinfo.json` | `Unknown(NoModuleInfo)` | none | none |
  | single-file legacy `Alpha.vst3` | `Unknown(SingleFile)` | none | none |
  | UTF-8 BOM + `current.json` (FR-011; plan SI-17) | `Known 1.2.0, Classes` | none | none |
  | bytes `0xFF 0xFE` + `{}` (FR-011; plan SI-17) | `Unknown(Corrupt)` | one `E_DETECT_MODULEINFO` `not UTF-8` | none |
  | `[]` (FR-012; plan SI-17) | `Unknown(Corrupt)` | one `E_DETECT_MODULEINFO` `root not an object` | none |
  | class `Version: 1` (FR-013 a; plan SI-17) | `Unknown(ClassesDisagree)` | one `E_DETECT_CLASSES` naming `<non-string>` | none |
  | no class version, top-level `Version: 1` (FR-013 c; plan SI-17) | `Unknown(NoVersion)` | none | none |
  | classes `1.2.0.1`, no top-level (FR-013 d; plan SI-17) | `Known 1.2.0, Classes` | none | none |
  | classes `1.2.0.1`, top-level `Version: 1` (FR-013 d; plan SI-17) | `Known 1.2.0, Classes` | one `E_DETECT_TOP_LEVEL` | none |
  | a directory named `Contents/Resources/moduleinfo.json` (PD-22, on every host; plan SI-17) | `Unknown(Unreadable)` | one `E_DETECT_MODULEINFO` | none |

  Every moduleinfo fixture also holds the marker string `KM-CONTENT-MARKER` in an ignored key (in the truncated one,
  before the cut). No captured line of any row contains it (FR-047). Threshold: every row exactly; marker 0 hits.
  Local; PENDING-CI (`all`).
- **SC-006 (duplicates and shadowing).** `detect::tests::duplicates_in_two_roots`, through `assemble` for all three
  OSes, each row with its own fixture and expectation:
  - Windows and macOS rows: Alpha `1.2.0` in the managed location (`vst3-global/Krate Audio/Alpha/Alpha.vst3`,
    `vst3-global/Alpha.vst3`) and `1.1.0` in `vst3-user`, plus Bravo only in `vst3-user`: Alpha `installed: version
    1.2.0`, two copies, the user copy `loads_before_managed: true` (rank 0 before the managed copy's global rank);
    Bravo `installed: elsewhere` (its copy's version) with `update: not_installed` and one listed copy,
    `loads_before_managed: true` (FR-044 ranks against the
    managed *location*, which has rank 1 whether or not a copy sits there).
  - Linux row (the managed root is `vst3-user` there, `settings.rs:28-32`): Alpha `1.2.0` in `vst3-user` (managed) and
    `1.1.0` in `vst3-global`: Alpha `installed: version 1.2.0`, two copies, the `vst3-global` copy
    `loads_before_managed: false` (rank 1 after the managed rank 0); Bravo only in `vst3-global`: `installed:
    elsewhere` (its copy's version), `update: not_installed`, one listed copy, `loads_before_managed: false`.
  Threshold: exact view equality per OS row. Local; PENDING-CI (`all`).
- **SC-007 (installer records, pure).** `detect::tests::record_parsers`: `inno_record` with both values, one, none, a
  relative / `..` / NUL / over-long `InstallLocation` (refused with `E_RECORD_LOCATION`, no value logged for NUL and
  length); `parse_pkg_info` on the committed fixture text (`package-id: com.krateaudio.alpha` / `version: 1.2.0` /
  `volume: /` / `location: /` / `install-time: 1720421876`), another `package-id`, a missing `version`, extra keys;
  `count_pkg_files` on a 3-line and an empty listing; the Linux manifest: valid, `manifest_version` 2
  (`E_RECORD_FUTURE`), another `plugin` (`E_RECORD_MISMATCH`), wrong types (`E_RECORD_PARSE`), unknown keys ignored,
  missing file (absent), a 4 MiB + 1 byte `manifests/Alpha.json` generated at test time and a directory named
  `manifests/Alpha.json` (each `Failed(E_RECORD_READ)`, plan SI-17, SI-18). The accepted `InstallLocation` values are host-native absolute paths built from a temp base,
  and the relative one is `Path::new("relative").join("dir")`. The `--pkg-info` fixture and the valid manifest each also
  carry the line or key `KM-CONTENT-MARKER`, and no captured log line contains it (FR-047). Threshold: every case;
  marker 0 hits. Local; PENDING-CI (`all`).
- **SC-008 (records through the fake host, roadmap line 585).** `detect::tests::records_through_fake_host`, with
  `FakeHostFacts` per OS row. Each case and its expected result:
  - The record is looked up only when the entry has the key: `tenth-plugin.json`'s Delta (install `{"vst3_bundle":
    "Delta.vst3"}` only, platforms `linux-x86_64` only, so it is `not_for_this_machine` on Windows and macOS) gives zero
    `inno_values` calls on the Windows row and zero `pkg_outputs` calls on the macOS row, read from `calls`.
  - A record with the managed copy absent sets `record_without_files`.
  - Record `1.0.3` against files `1.0.4` sets `record_version_differs`.
  - `Failed(E_RECORD_TIMEOUT)` gives `record_error` and one `warn!`.
  - An Inno `InstallLocation` of `<base>/vst3-global/Krate Audio/Custom` makes `…/Custom/Alpha.vst3` the managed copy and
    the default vendor copy an ordinary listed copy.
  - An Inno `InstallLocation` of `<base>/elsewhere` makes `<base>/elsewhere/Alpha.vst3` the managed copy (`managed:
    true`, `rank: None`, no copy flagged `loads_before_managed`) and the default vendor copy an ordinary listed copy
    (ruled Q2, D1).
  Every location is a host-native path built from the sandbox base. Separately,
  `detect::tests::outside_install_location_panics` (`#[should_panic]`, FR-005) feeds an `InstallLocation` that is
  absolute but outside the sandbox base (the base's parent joined with `outside-sandbox`, never created). It panics in
  `check_sandboxed` before any candidate is stat'ed. Threshold: every case; the fake's call list equals the expected
  list; the panic message names the sandbox. Local; PENDING-CI (`all`).
- **SC-009 (feed-driven, roadmap lines 587-589).** `status::tests::feed_driven_detection`: over `tenth-plugin.json`
  with a fixture `Juliet.vst3` (top `0.7.0`, class `0.7.0.1`) in the managed location, the view has ten plugins and
  Juliet reads `installed: version 0.7.0`, `update: up_to_date`; over `v1-only.json` every plugin is `installed:
  not_checked`, `availability: not_through_manager`, `update: null`, no copies, and `FakeHostFacts.calls` holds only
  `os_version`; over `mixed.json`, Delta likewise. Juliet's only platform is `linux-x86_64`, and the update state is
  computed independently of availability (Phase 5 FR-038). So `update: up_to_date` holds on every host, while
  `availability` is `available` on the Linux leg and `not_for_this_machine` on Windows and macOS; the test asserts
  the availability for `NativePlatform.os()`. Threshold: exact. Local; PENDING-CI (`all`).
- **SC-010 (the OS version).** `detect::tests::os_version_parsers`: `15.7.9`, `26.0`, `26.6.2\n` → versions; ``, `abc`,
  `15` → `None`; `glibc 2.39\n` → `2.39`; `glibc 2.35` → `2.35`; `musl 1.2`, `2.39` → `None`; `windows_version(10, 0,
  26200)` → `10.0.26200`. `status::tests::running_os_gates`: over `full-v2.json` (Alpha `darwin-aarch64` `min_os`
  `11.0`) with `assemble(Os::Macos, Arch::Aarch64, …, Some(10.15), …)` Alpha is `needs_newer_os` with `min_os: "11.0"`, with
  `Some(15.7.9)` `available`, with `None` `available`. Threshold: every case. Local; PENDING-CI (`all`).
- **SC-011 (notes).** `notes::tests::blocks_cases`: Alpha's `### Fixed\n\n- A fixture note.` → `[Heading 3 [Text
  "Fixed"], List unordered [[Paragraph [Text "A fixture note."]]]]`; `**bold** — text` with inline code; a fenced code
  block (text verbatim, no highlighting); an ordered list starting at 3; nested lists; `[x](https://e.example)` →
  `Text "x"` with no URL anywhere in the serialised model; `![alt](https://e.example/i.png)` → `Text "alt"`; `<b>x</b>` →
  literal text; a hard break; a 9-deep nested list flattened at depth 8 and a 9-deep nested block quote (`>` ×9)
  flattened the same way: in both, the ninth level's text becomes one `Paragraph [Text …]` appended to the innermost
  allowed container (eight `List` / `Quote` levels) (plan SI-9, PD-8); 9-deep nested emphasis (alternating `*` and `_`
  delimiters around one word) flattened at inline depth 8 (eight `Emphasis` levels around one `Text`); 65,537 bytes → the
  one "too long" paragraph; added (plan SI-17, PD-9): `<div>\nx\n</div>` (a CommonMark type-6 HTML block, unlike
  `<b>x</b>`, which is `InlineHtml` inside a paragraph) → `[Paragraph [Text "<div>\nx\n</div>"]]` (final line break
  removed); `a\n\n---\n\nb` → `[Paragraph [Text "a"], Rule, Paragraph [Text "b"]]`; `a\nb` → `[Paragraph [Text "a b"]]`
  (`SoftBreak` → `Text " "`, merged); `1. x` → `List { ordered: true, start: None, … }`;
  `notes-14000.json`'s Alpha notes parse to a non-empty model; empty and whitespace-only → `[]`. Threshold: exact model
  equality; the serialised model of every case contains no `http`. Local; PENDING-CI (`all`).
- **SC-012 (golden views, D10).** `status::tests::golden_views`. For each scenario, `assemble` runs on synthetic inputs
  with a fixed `now`, and `serde_json::to_string_pretty(&view) + "\n"` must equal
  `tests/fixtures/status/<scenario>.json` byte for byte. The scenarios, each with its `os`:
  - `fresh-all-states` (`windows`): every installed value, every update value, the VST3 unknown reasons
    `single_file`, `no_moduleinfo`, `too_large`, `unreadable`, `corrupt`, `classes_disagree` and `no_version`, and a
    `loads_before_managed` copy.
  - `cached-stale` (`linux`); `no-feed` (`windows`); `future-schema` (`macos`).
  - `macos-intel-not-for-this-mac` (`macos`); `not-for-this-computer-linux` (`linux`).
  - `needs-newer-os-macos`, `needs-newer-os-linux` and `needs-newer-os-windows` (each its own `os`).
  - `macos-au-copies` (`macos`): AU copies with `not_a_bundle`, `au_no_version` and a known `au_plist` version.
  - `record-notices` (`windows`, inno records: `record_without_files`, `record_version_differs`, `record_error`).
  - `notes-rendering` (`linux`).
  - `installed-elsewhere` (`windows`): an `elsewhere` card with a known version, one with `installed_version` null, a
    nested copy (`nested: true`) beside a fixed copy, one card whose `release_page_url` is a github.com URL and one
    whose is null. `nested: true` appears only on `format: vst3` copies.
  Every golden is OS-consistent, a state the product can produce. No AU copy, `au_user` / `au_global` place or `pkg`
  record appears outside an `os: macos` view; no `manifest` record outside `linux`; no `inno` record, `installer_location`
  place or `vendor_folder: true` outside `windows`. The test asserts this rule over every file, as well as the
  equality. Paths: every `DetectedCopy.path` in these inputs is `PathBuf::from` of a fixed forward-slash literal such as
  `/fixture/vst3-global/Alpha.vst3`, passed straight into the `DetectedCopy` and never joined, so `Path::display()`
  writes the same bytes on every host. The test also asserts the directory holds exactly the listed scenarios.
  Threshold: every file equal, every file OS-consistent, no extra file. Local; PENDING-CI (`all`).
- **SC-013 (AU copies, macOS).** `detect::tests::au_plist_cases_macos` (gated on macOS): the template-shaped plist
  (`CFBundleVersion` `1.0`, no short version) → `Unknown(AuNoVersion)`, no log; the same plist plus
  `CFBundleShortVersionString` `1.2.0`, in XML and in binary form → `Known 1.2.0, AuPlist`; `1.2` and `1.2.3.4` → `AuNoVersion` (only a three-part version is reported, ruled Q6); a
  corrupt plist → `Unknown(Corrupt)` with one `E_DETECT_PLIST`; a bundle directory without `Contents/Info.plist` →
  `Unknown(Unreadable)` with one `E_DETECT_PLIST` (FR-020); an `Info.plist` of 1 MiB + 1 byte written at test time →
  `Unknown(TooLarge)` with one `E_DETECT_PLIST` (FR-020; plan SI-17); a `.component` file → `NotABundle`; and an AU copy never
  sets `installed` (VST3 absent, AU present → `not_installed`, one AU copy listed). The corrupt and the template plist
  each carry a `KMContentMarker` key whose string value is `KM-CONTENT-MARKER`; no captured log line contains it
  (FR-047). PENDING-CI `check (macos-latest)` (`macos` list).
- **SC-014 (pkgutil receipt through the macOS `Platform` implementation, roadmap lines 590-592).**
  `detect::tests::pkg_receipt_macos` (gated on macOS) runs `detect_plugin(Os::Macos, …)` with `PlatformFacts { io:
  &fake }`, so `MacosPlatform::pkg_outputs` and `detect::pkg_read_from` run for real (FR-030, FR-034). `fake` is a
  `FakeHostIo` whose `runs` map the argvs `[platform::PKGUTIL, platform::PKG_INFO_FLAG, "com.krateaudio.alpha"]` and
  `[platform::PKGUTIL, platform::PKG_FILES_FLAG, "com.krateaudio.alpha"]` (built from the constants, FR-083) to
  `Output(<committed fixture text>)`. The temp tree holds `vst3-global/Alpha.vst3` and `au-global/Alpha.component`.
  Expected: `record: pkg, version 1.2.0, files 3`; `record_version_differs` false against a `1.2.0` bundle and true
  against `1.1.0`; the fake's `calls` equal those two argvs in that order. The same test with `--pkg-info` mapped to
  `NoOutput` gives no record and one call. PENDING-CI `check (macos-latest)` (`macos` list).
- **SC-015 (Linux layout and manifest, roadmap lines 592-593).** `detect::tests::linux_detection_linux` (gated on
  Linux): `~/.vst3`-shaped temp roots, the managed copy in `vst3-user`, a stale copy in `vst3-global`
  (`loads_before_managed: false`), the configured-root case of SC-003 (built through `reassemble_with_os(Os::Linux, …)`
  as there), and a manifest in `state/manifests/Alpha.json`
  → `record: manifest 1.2.0`; a manifest with no bundle → `record_without_files`. PENDING-CI `check (ubuntu-24.04)`
  (`linux` list).
- **SC-016 (a file locked by a running DAW, Windows).** `detect::tests::locked_moduleinfo_windows` (gated on Windows):
  the test holds `moduleinfo.json` open with `std::os::windows::fs::OpenOptionsExt::share_mode(0)` while detection runs:
  that copy is `Unknown(Unreadable)` with one `E_DETECT_MODULEINFO`, and every other plugin of the same run is detected
  normally. Local (`windows` list).
- **SC-017 (detection writes nothing and lists only through FR-006).** `detect::tests::detection_leaves_the_tree_unchanged`: a
  recursive snapshot (relative path, kind, size, modification time) of the temp base before and after a full
  `status::build` with a `FakeHostFacts` and a fixture-server feed differs only by `state/feed-cache.json` (Phase 5's
  cache) and its parent; and the FR-084 token guard `detect_status_notes_hold_no_write_or_listing_token` passes on the
  real files while `token_scanner_reports_planted_read_dir` reports its planted `read_dir`. Threshold: no other
  difference; guard 0 findings on the real files, the plant reported. Local; PENDING-CI (`all`).
- **SC-018 (status build end to end).** `status::tests::build_cases`: against the Phase 5 fixture server with
  `UrlPolicy::LoopbackOnly`: a served `full-v2.json` → `feed.source: fresh`; `FetchMode::CheckNow` sends `Cache-Control:
  no-cache` (the server's recorded headers); a `Reply::Close` after one accepted fetch → `cached` with the error text
  and detection still run over the cached feed; no cache and `Reply::Close` → `none`, `plugins: []`; the one `info!`
  summary line is logged per build. `status::tests::concurrent_builds` (FR-048): two threads, each with its own
  `FakeHostFacts`, call `status::build` on clones of one `AppContext` over one fixture tree and a fixture-server feed at
  the same time (released together by a `std::sync::Barrier`, local to the test, not global). Each returns a view equal
  to the view of a single build, and each fake's `calls` holds its own full call list. Threshold: every case. Local;
  PENDING-CI (`all`).
- **SC-019 (the screen renders every state, roadmap line 586).** `src/status.test.ts`, one vitest case per golden
  fixture of SC-012 (titles `renders <scenario>`), each rendering the fixture into a detached element and asserting the
  FR-071 / FR-072 / FR-074 strings, the card count, the `h2` names in order, and that `querySelectorAll("a, img, iframe,
  object, embed, link, script, style")` is empty. Each case also walks every element of the rendered section and
  asserts that its attribute names are a subset of `class`, `start` (only on `ol`), `disabled` (only on `button`) and `open` (only on `details`, absent on a fresh render:
  every card's `details` is closed and its `summary` reads `Release notes`, FR-072)
  (FR-075). Plus `does not call status commands when another instance is running`. Plus `every status text is rendered
  by a scenario`, which renders all SC-012 goldens, splits each `STATUS_TEXTS` template (FR-071) at its `{…}`
  placeholders, and asserts that every literal part of every entry occurs in the `textContent` of at least one
  rendered status section (plan SI-10). `renders notes-rendering` also pins the FR-072 mapping (plan SI-17): in Alpha's
  `details`, after the `summary`, the element children's tag sequence is exactly `h3` (Heading 1), `h6` (Heading 5),
  `ol` with `.start === 3`, `ul` holding a nested `ul` inside an `li`, `blockquote`, `pre` whose only child is `code`
  with the code text verbatim, `hr`, `p` holding `strong`, `em`, `code` and a `br`; Bravo's `details` (`notes: []`)
  holds `summary` then exactly one `p` reading `No release notes.` A table cell that no scenario reaches fails this case, so the coverage is measured, not listed by
  hand. Threshold: every case passes. Local; PENDING-CI on every leg's `vitest` step.
- **SC-020 (check now).** `src/main.test.ts` cases `check now disables the button while pending`, `check now calls
  check_now and re-renders`, `check now recovers from a rejection`, `shows the ten-minute note`: with `mockIPC`, a
  pending `check_now` keeps the button disabled with text `Checking…`, a second click adds no call, the resolved view
  replaces the section, a rejection re-enables the button and shows the FR-074 text, and the note reads `A new release
  can take up to 10 minutes to appear.` for `edge_max_age_secs: 600`. Local; PENDING-CI (`vitest`).
- **SC-021 (glue and capability).** `real_src_tauri_config_passes_the_guard` (FR-063: five commands, five permissions)
  passes on the real tree; `planted_config_changes_fail_the_guard` reports the planted sixth permission, the planted
  `"allow-fs-read"` and the planted `"opener:allow-open-url"`; the CSP string is byte-identical to Phase 4's. Threshold: 0 findings on the real tree, every plant
  reported. Local; PENDING-CI (`all`).
- **SC-022 (dependency and confinement guards).** These tests pass on the real tree and their self-tests report every
  plant: `core_dependencies_are_the_fr007_set` (11 names, macOS table `core-foundation` + `plist`);
  `reader_dependencies_are_the_ruled_set` (`injection.rs:2702`, `READER_PINS` with FR-081's five features);
  `core_sources_hold_no_production_literal` (the 37-entry `PLATFORM_LITERALS`, FR-033);
  `notes_and_plist_crates_are_confined` and `host_io_symbols_are_confined` with `confinement_scanner_reports_plants`
  (FR-082); `test_sources_never_name_os_host_io` with `test_source_scanner_reports_planted_os_host_io` (FR-083);
  `install_identifiers_absent_from_sources` with `install_identifier_scanner_reports_plants` (FR-089). The core
  dependency tree: the `core dependency tree` step (FR-080) passes on each leg with its `::notice title=core dependency
  tree (<os>)::<n> crates, none excluded` and no `::error title=core dependency tree (<os>)::…` line; locally, the same
  `cargo tree` command's output is recorded in compliance with 0 matches of the FR-080 regex. Local; PENDING-CI
  (`all`; `core dependency tree` on `check (macos-latest)` and `check (ubuntu-24.04)`).
- **SC-023 (required tests ran, per leg).** Every name of FR-085 runs exactly once per applicable leg. Local: the Windows
  list (`cargo.all` + `cargo.windows`, and `vitest`) is checked against the local `cargo test` output and the vitest
  JSON report by the same rule as `manager-ci.yml:147-157`. PENDING-CI: the `required tests ran` step of `check
  (macos-latest)` and `check (ubuntu-24.04)` passes with no `::error` line, and its notice counts equal the committed
  file's list lengths (Success Criteria preamble).
- **SC-024 (nothing real touched, parallel).** `test_sources_hold_no_real_location` (FR-088) finds 0 hits of its
  eleven regexes in its six files, and `real_location_scanner_reports_plants` reports each planted hit; the roster guard
  and the "roster absent" step stay green (FR-087). Threshold: 0 hits; every plant reported; guards green. Local;
  PENDING-CI (`all`; "roster absent" on each leg).
- **SC-025 (the per-OS `Platform` host methods, roadmap lines 572, 576, 591-592, 999-1001).** One test per OS, each
  gated on its `target_os` and each driving `NativePlatform`'s three FR-030 methods with a recording `FakeHostIo`
  (FR-083). Every expected argv and subkey is built from the FR-033 constants as `platform` re-exports them:
  - `detect::tests::host_facts_through_fake_io_windows`: `inno_values` for `{0A1F0000-0000-4000-8000-000000000001}_is1`
    calls `registry_values(UNINSTALL_KEY + "\" + key, [DISPLAY_VERSION, INSTALL_LOCATION])` once and maps `NoKey` →
    `Absent`, `Values([Some("1.2.0"), None])` → `Found` with that pair, `Values([None, None])` → `Found` with both
    `None`, `Failed` → `Failed(E_RECORD_READ)`; `os_version` maps `rtl = Some((10, 0, 26200))` → `10.0.26200` and
    `None` → `None`; `pkg_outputs` → `Absent` with zero `io` calls.
  - `detect::tests::host_facts_through_fake_io_macos`: `pkg_outputs` maps the two committed fixture outputs to
    `Found`, `--pkg-info` `NoOutput` → `Absent` (one call), `Abandoned` → `Failed(E_RECORD_TIMEOUT)`, and `--pkg-info`
    `Output` with `--files` `NoOutput` → `Failed(E_RECORD_READ)` (two calls); `os_version` maps `[SW_VERS,
    SW_VERS_FLAG]` → `Output("15.7.9\n")` to `15.7.9` and `NoOutput` to `None`; `inno_values` → `Absent` with zero
    calls.
  - `detect::tests::host_facts_through_fake_io_linux`: `os_version` maps `[GETCONF, GLIBC_NAME]` →
    `Output("glibc 2.39\n")` to `2.39`, and `Abandoned` and `NoOutput` to `None`; both record methods → `Absent` with
    zero calls.
  Threshold: every case; each fake's `calls` equals the expected list exactly. Local (Windows); PENDING-CI `check
  (macos-latest)` (`macos` list), `check (ubuntu-24.04)` (`linux` list).
- **SC-026 (the pure host mappers, every host).** `detect::tests::host_read_mappers`: every arm of FR-034's
  `inno_read_from`, `pkg_read_from` and `os_version_from_run`. `pkg_read_from`'s `files` closure counts its calls:
  0 for `info` `NoOutput` or `Abandoned`, 1 otherwise. Threshold: every arm. Local; PENDING-CI (`all`).
- **SC-027 (a metadata error that is not NotFound, FR-004).** `detect::tests::probe_metadata_error_unix` (gated on
  `cfg(unix)`): a candidate whose parent component is a regular file (`<base>/vst3-global` replaced by a file, then
  `<base>/vst3-global/Alpha.vst3` probed). POSIX `stat` fails with `ENOTDIR` there, which Rust reports as
  `ErrorKind::NotADirectory`, not `NotFound` (External facts). Expected: one copy `Unknown(Unreadable)` and one `warn!`
  `E_DETECT_MODULEINFO`. Added (plan SI-17, PD-22): a FIFO at `Alpha.vst3/Contents/Resources/moduleinfo.json`, made in
  the temp tree with `mkfifo` (a bare program name resolved through `PATH`; std has no `mkfifo`; the one process this
  test body may start, acting inside its `TempDir`, tasks TA-1), then `probe` run on a helper thread whose result comes
  back over an `mpsc` channel with `recv_timeout(5 s)`: expected `Unknown(Unreadable)` and one `E_DETECT_MODULEINFO`
  within the timeout, so a regression fails the case instead of hanging the binary. Windows has no temp-tree case that
  reaches this path without a lock or an ACL change, so the Windows leg has no test of it, which is stated here rather
  than faked. PENDING-CI `check (macos-latest)` (`macos`
  list), `check (ubuntu-24.04)` (`linux` list).
- **SC-028 (the roadmap's Phase 10 rule keeps its root clause and gains one sentence, FR-092).** A node check over
  `specs/Krate-manager-roadmap.md` with every whitespace run collapsed to one space: exactly 1 occurrence of `in the
  plugin's install dir under the compiled-in Windows plug-in root`, exactly 1 of `is never uninstalled by the helper`;
  and `git diff` of the roadmap shows only the Phase 6 status line and that added sentence (plan SI-14). Threshold: 1 /
  1 / those two hunks. Local; recorded in compliance (a spec-file check, no CI leg).
- **SC-029 (nested copies are reported, FR-006).** `detect::tests::nested_copies_listed`, on every host, under
  `fresh_temp_roots()`: Alpha bundle directories at `vst3-user/Old/Alpha.vst3` (depth 2),
  `vst3-global/Krate Audio/Alpha.vst3` (depth 2), `vst3-global/a/b/c/Alpha.vst3` (depth 4) and, as non-matches,
  `vst3-global/a/b/c/d/Alpha.vst3` (depth 5), `vst3-user/Old/Other.vst3` (another name) and
  `vst3-user/Old/Alpha.vst3/Contents/Alpha.vst3` (inside a matched bundle). Expected: exactly the first three are
  reported, each `nested: true`, `managed: false`, with its root's `rank` (the user copy `loads_before_managed: true`
  against a global managed location on Windows and macOS, `false` on Linux where the managed root is `vst3-user`);
  two nested copies `a/Alpha.vst3` and `b/Alpha.vst3` of one root are reported in that order; a copy at a fixed
  candidate is reported once with the fixed candidate's flags; `Delta`'s bundle name is never matched against `Alpha`'s
  copies. Case rule (plan SI-3, SI-6, PD-19): the case tests call `Listing::scan` directly with the bundle at
  `vst3-user/Old/alpha.vst3` and assert on the listing's matches through both `paths()` and `copies_of("Alpha.vst3")`:
  `detect::tests::listing_case_windows` (gated on Windows) expects `copies_of` to return exactly the
  `vst3-user/Old/alpha.vst3` copy, and `detect::tests::listing_case_exact_unix` (gated on `cfg(unix)`) expects `paths()`
  and `copies_of` both empty (exact comparison). `detect::tests::listing_skips_symlinked_dirs_unix`
  (gated on `cfg(unix)`): a symlink to a directory holding a matching bundle is not entered, and a link loop
  (`a/loop -> ..`) terminates; added (plan SI-17): a directory `vst3-user/locked` with mode `0o000` holding
  `Alpha.vst3` is skipped with no `warn!` and no match (FR-006), its mode restored before the `TempDir` drops; the case
  first asserts that `read_dir` of that directory fails (it succeeds for root, so a root run fails loudly with that cause
  rather than passing vacuously). `detect::tests::listing_skips_junction_windows` (gated on Windows; plan SI-20): inside
  the temp root, a junction loop `vst3-user/a/loop` → `vst3-user/a`, and a junction `vst3-user/out` → a temp directory
  outside every root that holds `Alpha.vst3`, both made with `cmd /C mklink /J <link> <target>` (no admin right needed;
  the one process this test body may start, acting inside its `TempDir`, tasks TA-1); expected: the listing terminates,
  reports no copy through either junction, and the root's `examined` count is finite (capture logger `debug!` line).
  Threshold: every case. Local; PENDING-CI (`all`; `macos` and `linux` lists for the two unix tests).
- **SC-030 (the listing caps are honoured, FR-006).** `detect::tests::listing_caps_honoured`: with `ListingLimits {
  max_depth: 2, max_entries: 5 }` over a tree of more than 5 entries, the root stops at 5 examined entries, a match
  found before the cut is kept and one after it is not, exactly one `warn!` `E_DETECT_LISTING_CAP` is captured for the
  capped root and none for an uncapped one, and a depth-3 match is not reported with no warning. Entry order is name
  order, so the cut is the same on every host. `detect::tests::listing_production_limits` asserts
  `LISTING_MAX_DEPTH == 4` and `LISTING_MAX_ENTRIES == 20_000`. `status::tests::listing_runs_once_per_root`: a
  `status::build` over `tenth-plugin.json` (ten plugins) captures exactly one `debug!` `listing:` line per listed root,
  not one per plugin. Threshold: every case. Local; PENDING-CI (`all`).
- **SC-031 (installed elsewhere, FR-043, ruled Q1).** `detect::tests::installed_elsewhere`, through `assemble` for the
  three OSes: managed absent and a user copy `1.1.0` → `installed: elsewhere`, `installed_version: 1.1.0`, `update:
  not_installed`; two other copies (the first with an unknown version, the second `1.0.0`) → the version of the first
  copy with a known version in candidate order; only unknown-version other copies → `elsewhere` with
  `installed_version: null`; a nested copy alone → `elsewhere`; AU copies only (macOS) → `not_installed`; no copy →
  `not_installed`; the managed copy present beside other copies → `version` (never `elsewhere`). The golden
  `installed-elsewhere` (SC-012) and the vitest rendering (SC-019, with the version and with the word `unknown`) cover the
  view and the text. Threshold: every case. Local; PENDING-CI (`all`).
- **SC-032 (the release-page URL check, FR-049).** `status::tests::release_page_url_cases`. Accepted:
  `https://github.com/example/repo/releases/tag/alpha/v1.2.0` (the fixture shape), `https://GitHub.com/example/x`
  (returned lower-cased), `https://github.com:443/example/x` (returned without the port). Refused (`None`): `http://`
  with the same host, `https://github.com.example.test/x`, `https://example.test/github.com`, the host with an `api.`
  prefix (built with `format!("api.{}", status::RELEASE_PAGE_HOST)`), a URL with a user name or a user name and password,
  `https://github.com:8443/x`, `javascript:alert(1)`, `file:///x`, `//github.com/x`, the empty string, `not a url`, and a
  2,049-byte `https://github.com/...`. Over `full-v2.json`, `assemble` gives Alpha a `release_page_url` equal to its
  `release_notes_url`; over `v1-only.json` every entry has `null`. Threshold: every case. Local; PENDING-CI (`all`).
- **SC-033 (the opener is confined and not granted to the webview, FR-064, FR-065).**
  `opener_is_confined_and_not_granted_to_the_webview` passes on the real tree and `opener_guard_reports_plants` reports
  each plant; `real_src_tauri_config_passes_the_guard` (SC-021) passes with the five commands. Recorded in compliance:
  `cargo tree -p krate-manager --locked -e normal --prefix none` lists `tauri-plugin-opener v2.7.2`, the same command for
  `-p krate-manager-core` lists none, `npm ls @tauri-apps/plugin-opener` finds nothing. Threshold: 0 findings on the
  real tree, every plant reported, the three recorded results as stated. Local; PENDING-CI (`all`).
- **SC-034 (the release-page button, FR-078).** `src/status.test.ts` cases `renders the open release page button`,
  `open release page calls open_release_page once`, `open release page shows a rejection` and `no navigation path but
  the release page button`: a card whose `release_page_url` is a string holds exactly one `button` reading `Open release
  page`, a card with `null` holds none; with `mockIPC`, a click calls `open_release_page` once with `{ url }` equal to the
  view's, the button is disabled while the call is pending, a second click adds no call, a rejection shows `Could not
  open the release page: <text>`; a source scan of the non-test `src/*.ts` finds 0 hits of `window.open`, `location.href`,
  `location.assign`, `href`, `<a` and `@tauri-apps/plugin-opener` (and reports a planted hit of each in an in-memory
  copy). Threshold: every case. Local; PENDING-CI (`vitest`).
- **SC-035 (the pending state, FR-070, FR-073).** `src/main.test.ts` cases `shows the pending text until the first
  result`, `check now stays disabled until the first result` and `check now is enabled after a rejected first load`:
  with `mockIPC` holding `plugin_status` pending, the section reads `Checking for plugins…` and the Check now button is
  disabled; once it resolves the pending text is gone, the cards render and the button is enabled; once it rejects, the
  FR-074 text shows and the button is enabled; the recorded call list is exactly `["startup_error", "app_version",
  "plugin_status"]` (no second status call). Threshold: every case. Local; PENDING-CI (`vitest`).

---

## Safety

- **Injected roots.** Detection reads only the candidate paths FR-001 builds from `ctx.roots.plugin_roots()`, the
  settings' Linux root and a validated Inno `InstallLocation`, plus `<state_dir>/manifests/` and the bounded listing of FR-006 under the
  injected VST3 roots (at most 4 levels and 20,000 entries per root). Every test builds
  `AppContext` from `fresh_temp_roots()` (Phase 4), whose sandbox checks the three injected paths that do not come
  from `fresh_temp_roots()`'s own default roots: the configured Linux root (`with_configured_plugin_root` panics on a
  root outside the temp base, `roots.rs:136-144`); the Linux managed root, which is that configured root or
  `vst3_user()` (`settings.rs:28-32`); and the `InstallLocation` from a fake record, which `detect_plugin` passes
  through `Roots::check_sandboxed` before anything is stat'ed (FR-005, `outside_install_location_panics`, SC-008). The
  plug-in roots and the state dir are the sandbox's own subdirectories. So no test can read a real plug-in folder,
  even through a fake record that names one.
- **What is read, and how.** Detection is read-only by construction: `detect.rs`, `status.rs`, `notes.rs` name no write,
  rename, remove, create, permission or process token, and only `detect.rs` names a listing token (FR-084, FR-006). Every file read is bounded (1 MiB for a
  moduleinfo or plist, 4 MiB for a manifest). Feed strings reach paths only as `PlainName` values that passed
  `check_install_values` (Phase 5 FR-026); the registry's `InstallLocation` passes the lexical check of FR-003 before it
  is joined.
- **The host readers.** `OsHostIo` opens the registry key with `KEY_READ | KEY_WOW64_64KEY` and reads it with
  `RegGetValueW`, closing it with `RegCloseKey` (no write API is named). The macOS `Platform` runs `/usr/sbin/pkgutil`
  with the two read-only options `--pkg-info` and `--files` and an id that matched `^com\.krateaudio\.[a-z0-9]+$`
  (CONTRACT.md:75) as its own argv element, and `sw_vers` and `getconf` with fixed arguments, all argv built from the
  FR-033 constants and handed to `HostIo::run`; children are bounded and never killed (`run_bounded`, roadmap line
  993). No test names `OsHostIo` (FR-083, FR-088); tests use `FakeHostFacts` (cross-OS tables) or `FakeHostIo` (the
  per-OS `Platform` methods), neither of which starts a process or touches the registry.
- **The webview.** Two new read-only commands and a third that opens one validated page, with their three permissions;
  the CSP, the navigation guard and the markup-sink ban are unchanged. Release notes become a typed tree in Rust and DOM
  nodes through `textContent`; links and images are reduced to text, so the window shows no remote content and has
  nothing to navigate to except through the release-page button: its URL passes core's `https` + `github.com` check
  twice (in the view, in the command) before the system browser opens it, and the opener plugin's own permissions are
  not granted to the webview (FR-049, FR-064, FR-065, FR-075).
- **Verified before extraction or execution.** Nothing is downloaded, extracted or executed in this phase (Phase 7).
- **The elevated helper.** Untouched: still Phase 4's stub; nothing calls it.
- **What tests must never touch.** Real plug-in folders (including the dev box's `%LOCALAPPDATA%\Programs\Common\VST3`
  copies, roadmap line 978), `C:\Program Files\Common Files\VST3`, `/Library/Audio/Plug-Ins`, `~/.vst3`, `ProgramData`,
  the registry, `pkgutil` receipts, the real state dir, and any non-loopback network address. No test launches the app.

---

## Edge cases

| Case | Behaviour | Where |
|---|---|---|
| Bundles that still say `0.8.0` at top level (released before each plugin's next release) | Class `Version` decides (`1.0.4.0` → `1.0.4`); the `0.8.0` stamp is logged at `debug!`, never `warn!` | D5, FR-013, SC-005 |
| A real `0.8.0` release (class `0.8.0.x`) | `0.8.0`, no warning | FR-013, SC-005 |
| Top-level and class disagree on another value | Class wins; one `E_DETECT_TOP_LEVEL` | FR-013, SC-005 |
| Classes disagree, or one class version unparseable | "Installed, version unknown"; one `E_DETECT_CLASSES` | FR-013, SC-005 |
| Partial install: bundle folder without `moduleinfo.json`, or the installer's record without the bundle | `NoModuleInfo` → "Installed, version unknown"; record without files → "Not installed" plus the record notice | FR-011, FR-044, SC-005, SC-008 |
| File locked by a running DAW (Windows) | A DAW maps the plug-in DLL, not `moduleinfo.json`; if a read still fails (a sharing violation), that copy is "version unknown (cannot be read)" with one warning and the rest of the run continues | FR-011, SC-016 |
| Corrupt or oversized `moduleinfo.json` / AU `Info.plist` | That copy "version unknown", one warning; never a crash, never more than 1 MiB read | FR-011, FR-012, FR-020, SC-005, SC-013 |
| Single-file legacy `.vst3` (deprecated since 3.6.10) | Listed, "version unknown: single-file plug-in" | FR-010, SC-005 |
| A per-user copy beside, or instead of, the installer's global copy (this dev box) | The managed copy decides when it exists, and the user copy is listed with "hosts that follow Steinberg's scan order load this copy first"; with no managed copy the card reads "Installed outside the manager's location: <version>" and "Available to install" | D1, D11, FR-043, FR-044, SC-006, SC-031 |
| Copies in two roots, same or different versions | Every copy listed with its own version | FR-001, FR-046, SC-006 |
| Inno installed to a folder the user chose | When the key records the `InstallLocation`, the copy there is the managed copy wherever the folder lies (ruled Q2). When the key records none, the default vendor path is the managed location; a custom copy inside an injected VST3 root is found by the listing, one outside every root is not found | D1, D2, FR-001, FR-003, FR-006, SC-002, SC-008, SC-029 |
| Downgrade: the feed offers a version older than the installed one (stale edge copy, rollback) | "Installed version is newer than the latest release"; never an update | FR-043, FR-071, Phase 5 FR-032 |
| Corrupt feed | Phase 5 rejects it; the cached copy is shown with "Could not refresh the list: …", detection runs over the cached feed | FR-040, FR-074, SC-018 |
| Offline | Same, with the offline error; without a cache, "Could not load the plugin list: …" and no cards | FR-074, SC-018 |
| Interrupted feed download | Phase 5: `E_FEED_BODY`, cached copy stale; as "offline" here | Phase 5 FR-044, SC-018 |
| Disk full | Detection writes nothing; a feed-cache write failure is Phase 5's `cache_error`, logged, and the fresh list is still shown | FR-048, Phase 5 FR-052 |
| Feed `schema_version` 1 (today's live feed) | Every plugin listed with its version, "Not checked", "Not available through the manager" | D3, FR-042, SC-009 |
| Feed `schema_version` 2 | Detection per entry from its `install` block | FR-042, SC-009 |
| Feed `schema_version` 3 or more | Phase 5's `E_FEED_FUTURE_SCHEMA` ("a newer Krate Manager is needed") in the feed line; cached list if any | FR-074, SC-012 |
| A tenth plugin in the feed with a matching bundle | Detected with no manager release | SC-009 |
| A plugin whose asset needs a newer OS, or has no asset for this machine (Intel Mac, arm64-only plugin) | "Needs macOS 11.0 or newer" / "Not available for this Mac"; installed copies still listed | FR-045, FR-071, SC-010 |
| The running OS version cannot be read | Never gates (Phase 5 FR-037) | FR-036, FR-037 |
| `pkgutil` hangs | Abandoned after 5 s (never killed); record error `E_RECORD_TIMEOUT` | FR-030, FR-034, SC-008, SC-025 |
| `pkgutil` missing or failing to start | Reads as "no receipt" (`run_bounded` cannot tell a spawn failure from a non-zero exit); the record is informational only | FR-031, SC-025 |
| Registry key absent / key present without the values / access denied | No record / a record with no version and no location / `E_RECORD_READ` notice | FR-030, FR-032, FR-034, SC-007, SC-025 |
| Linux manifest written by a newer manager | `E_RECORD_FUTURE`; files still detected | FR-028, SC-007 |
| An AU `.component` without a version in its plist (every shipped template) | "AU: … (version unknown: version not recorded in the AU bundle)" | D6, FR-020, SC-013 |
| Release notes with links, images or raw HTML | Text only; nothing navigable or remote | FR-052, FR-075, SC-011, SC-019 |
| Release notes over 64 KiB | One paragraph "Release notes are too long to show here." | FR-053, SC-011 |
| "Check now" clicked twice | The second click does nothing while the first is pending | FR-073, SC-020 |
| A non-UTF-8 path on Linux | Shown with replacement characters (`Path::display`) | FR-046 |
| A copy in a subfolder of a VST3 root (`<root>\Old\<bundle>`, `<root>\Krate Audio\<bundle>`) | Found by the bounded listing (at most 4 levels, 20,000 entries per root), listed with `nested: true`, never managed | D2, FR-006, SC-029 |
| A VST3 root with more than 20,000 entries | Listing of that root stops at the cap; one `warn!` `E_DETECT_LISTING_CAP`; copies found before the cap stay listed | FR-006, SC-030 |
| A symlinked directory inside a root | Not entered, so a link loop ends; a symlink named like the bundle is still probed as a candidate | FR-006, SC-029 |
| The feed's release page is not an https `github.com` URL | No `Open release page` button; nothing is opened | FR-049, FR-078, SC-032 |
| The system browser cannot be started | The command returns `E_OPEN_FAILED: …`; the card shows `Could not open the release page: …` | FR-064, FR-078, SC-034 |
| Before `plugin_status` returns (offline: up to 30 s) | `Checking for plugins…` and a disabled Check now button | FR-070, FR-073, SC-035 |

---

## New components

| Path | Purpose |
|---|---|
| `apps/krate-manager/crates/core/src/detect.rs` + `detect_tests.rs` | `candidates`, `same_path`, `Listing` (FR-006), `probe`, the moduleinfo and AU readers, the trailing-comma pass, the version rule, `HostFacts`, `PlatformFacts`, `RecordRead`, the record parsers, the host mappers, the Linux manifest reader, the OS-version parsers, `detect_plugin` (FR-001 to FR-036); the tests include the per-OS `Platform` host-method tests (SC-025) |
| `apps/krate-manager/crates/core/src/status.rs` + `status_tests.rs` | `StatusView`, `assemble`, `build`, `release_page_url` (FR-040 to FR-049) |
| `apps/krate-manager/crates/core/src/notes.rs` + `notes_tests.rs` | `Block`, `Inline`, `blocks` over `pulldown-cmark` (FR-050 to FR-054) |
| `apps/krate-manager/crates/core/src/platform/mod.rs` (changed) | `VENDOR_DIR` (FR-002); `HostIo`, `RunOutcome`, `RegistryRead` (FR-031); the three new `trait Platform` methods (FR-030) |
| `apps/krate-manager/crates/core/src/platform/{windows,macos,linux}.rs` (changed) | The host methods and the FR-033 constants per OS (FR-030, FR-033) |
| `apps/krate-manager/crates/core/src/roots.rs` (changed) | `check_sandboxed` (FR-005) |
| `apps/krate-manager/crates/core/src/test_support.rs` (changed) | `FakeHostFacts`, `FakeHostIo` (FR-083) |
| `apps/krate-manager/src-tauri/src/os_facts.rs` | `OsHostIo`: the raw `run` over `run_bounded`, the registry open / read / close (Windows), `RtlGetVersion` (Windows); no decision logic (FR-032) |
| `apps/krate-manager/src-tauri/src/commands.rs`, `lib.rs`, `build.rs`, `capabilities/main.json` (changed) | `plugin_status`, `check_now`, `open_release_page` (the opener's free fn `open_url`, no plugin registration, plan SI-15) and the five permissions (FR-060, FR-061, FR-064) |
| `apps/krate-manager/src/view.ts`, `src/status.ts`, `src/notes.ts`, `src/status.test.ts` (new); `src/main.ts`, `src/main.test.ts`, `src/styles.css` (changed) | The view types, the status section, the notes renderer, the check control and their tests (FR-070 to FR-077) |
| `apps/krate-manager/tests/fixtures/detect/` | Committed moduleinfo, plist and `pkgutil` output texts for invented plugins (FR-086) |
| `apps/krate-manager/tests/fixtures/status/<scenario>.json` | The golden views shared by `cargo test` and `vitest` (FR-086, SC-012, SC-019) |
| `apps/krate-manager/tests/fixtures/.gitattributes` | `detect/** -text` and `status/** -text`, so Git never rewrites the byte-compared fixtures on a `core.autocrlf=true` checkout (plan SI-7, PD-11) |
| `apps/krate-manager/Cargo.toml`, `crates/core/Cargo.toml`, `Cargo.lock` (changed) | `pulldown-cmark`, `plist` (macOS), the `windows-sys` features (FR-080, FR-081); `tauri-plugin-opener` in `src-tauri` (FR-065) |
| `apps/krate-manager/crates/core/tests/injection.rs`, `config_guard.rs`, `roster.rs` (changed) | The dependency, platform-literal, confinement, test-source, token and real-location guards; four commands and permissions; the install-identifier guard (FR-033, FR-063, FR-065, FR-080 to FR-084, FR-088, FR-089) |
| `apps/krate-manager/tests/required-tests.json` (changed) | The new test names (FR-085) |
| `.github/workflows/manager-ci.yml`, `tools/feed-contract/tests/manager-ci-shape.test.js` (changed) | The `core dependency tree` step and its shape-test entries (FR-080, FR-090) |
| `specs/Krate-manager-roadmap.md` (changed) | The Phase 6 status line and the one sentence added to Phase 10 after the unchanged `run_inno_uninstaller` clause (FR-090, FR-092; plan SI-14) |

---

## Traceability

| Roadmap statement (line) | FR / SC |
|---|---|
| Scan the injected plug-in roots: Steinberg user and global VST3 folders, the Windows `Krate Audio\<P>` subfolder, macOS `Components` (562-563) | D2 (fixed candidates plus a bounded listing, ruled Q3), FR-001 to FR-006, SC-002, SC-003, SC-027, SC-029, SC-030 |
| Read `Contents/Resources/moduleinfo.json` with trailing commas tolerated (563) | D4, FR-011, FR-012, SC-004 |
| Installed version from the class `Version` reduced to three parts; all class versions reduce to one value or "version unknown" (563-566) | D5, FR-013, SC-005 |
| Top-level `Version` only a consistency check, logged; no class `Version` → top-level, flagged (566-567) | D5, FR-013(c)(d), SC-005 |
| Read the AU `Info.plist` version on macOS (568) | D6, FR-020, FR-021, SC-013 |
| Report every copy found so shadowing is visible (568-570; STRATEGY line 23; Not in v1 971-972) | D1, D2, D11, FR-001, FR-006, FR-043, FR-044, FR-046, FR-071, SC-006, SC-029, SC-031 |
| Installer-record facts, read only: Inno key (64-bit HKLM view), `pkgutil` receipt, Linux manifest, behind the platform abstraction with fakes (571-572) | D7, FR-025 to FR-034 (`Platform::inno_values` / `pkg_outputs` per OS over `HostIo`), SC-007, SC-008, SC-014, SC-015, SC-025, SC-026 |
| No "legacy" state (572; Q4 line 27) | FR-042, FR-043 (an old `.exe` / `.pkg` install is simply installed) |
| One screen listing every plugin in the feed with installed / available version and notes from `notes_markdown` with no remote content, read-only (573-574; Positioning line 40) | D9, D10, D15, FR-040 to FR-054, FR-064, FR-065, FR-070 to FR-078, SC-011, SC-012, SC-019, SC-031 to SC-035 |
| "Check now" control with the ten-minute text (`feed::EDGE_MAX_AGE_SECS`) (575-576) | D13, FR-060, FR-070, FR-073, SC-018, SC-020, SC-035 |
| Read the running OS version and pass it to `version::resolve` (576-577) | D7, D8: `Platform::os_version(io)` per OS (FR-030, FR-036), its parsers and mapper (FR-034, FR-035), `status::build` → `resolve` (FR-037); SC-010, SC-025 (per OS leg), SC-026 |
| Fixture trees in temp dirs produce the specified installed-state table (580-584) | SC-005, SC-006 |
| Fake registry / fake `pkgutil`; no test reads the real registry or real folders (585) | D7, FR-005, FR-083, FR-086, FR-088, SC-008, SC-014, SC-024, SC-025 |
| UI test (vitest) renders each state (586) | FR-076, SC-019 |
| Detection is feed-driven; an invented plugin is reported installed; a v1-only entry is "not available through the manager" (587-589; ROSTER line 33) | D3, FR-042, SC-009 |
| PENDING-CI macOS leg `cargo test -p krate-manager-core detect::` (AU plist, pkgutil "run through the macOS `Platform` implementation"); Linux leg over `~/.vst3`-shaped temp roots (590-593) | SC-003, SC-013, SC-014 (`MacosPlatform::pkg_outputs` over `FakeHostIo`), SC-015, SC-025, SC-027, SC-023 |
| Does NOT install, write outside the state dir, or call the helper (595) | FR-048, FR-084, SC-017, Non-goals |
| Cross-cutting: never touch the real system; one `Platform` abstraction with a Windows, macOS and Linux implementation, each with tests (999-1001); cross-platform PENDING-CI; zero warnings; parallel tests; the feed is the roster, AppIds and pkg identifiers included (975-1010; ROSTER line 33) | D7, FR-005, FR-030, FR-065, FR-083, FR-086 to FR-089, SC-001, SC-022 to SC-025, SC-033 |

**Additions and departures.** Additions, accepted by the clarification session of 2026-10-31:
- the managed-location rule (D1): any usable recorded Inno `InstallLocation` is managed (Q2);
- the `elsewhere` installed value (Q1);
- "not checked" for entries without install facts (D3);
- the `debug!` level for the `0.8.0` stamp (D5; the line is still logged, so line 566 holds);
- the typed notes tree (D9), shown collapsed (Q7);
- the golden views (D10);
- the Windows `InstallLocation` candidate (FR-001, FR-003);
- the bounded listing of the injected VST3 roots for nested copies (D2, FR-006, Q3);
- the `HostIo` / `PlatformFacts` layering (D7);
- the user-facing record diagnostics: the view flags `record_without_files`, `record_version_differs` and
  `record_error` (FR-044, FR-046) and the three record-notice sentences on each card (FR-071 "record notices" row),
  kept by the user (Q5);
- the pending text and the disabled Check now button until the first result (FR-070, FR-073, Q4);
- the `Open release page` button with the opener plugin (D15, Q8).

**Departures from roadmap wording:**
1. Line 568's AU `Info.plist` version does not exist in the shipped template (D6), so the AU read reports presence
   and, only when `CFBundleShortVersionString` is a three-part version, a version (Q6).
2. Lines 569-570 state the shadow direction as "global over user"; Steinberg's order makes it user over global (D11),
   and the spec implements Steinberg's order.
3. Resolved by Q3: lines 562 and 568-570 ("scan the injected plug-in roots", "report every copy found") are met by the
   fixed candidates plus the bounded listing. What stays unseen: a copy outside every injected VST3 root, deeper than 4
   levels or beyond a root's first 20,000 entries, and an AU copy outside the fixed AU candidates.
4. The roadmap's Phase 10 `run_inno_uninstaller` sentence (lines 714-717) keeps its "under the compiled-in Windows
   plug-in root" clause and gains one sentence sending a managed copy outside that root to Windows Settings (FR-092;
   Q2 re-ruled by Q-C (a), plan SI-14).
5. Phase 4 FR-004 (no Tauri plugin crate) is narrowed by Q8 to exactly `tauri-plugin-opener`, with no plugin permission
   granted (FR-063, FR-065; plan SI-2).

---

## Review notes

- **The AU version gap (D6)** is a plugin-build fact, not a manager defect: the AUv2 template sets constant
  `CFBundleVersion` and `AudioComponents.version` values, while the AUv3 path derives `kAUcomponentVersion` from
  `version.json` (`KratePlugin.cmake:105-113`, `plugins/shared/resources/auv3/Shared/Info.plist:49`). A plugin-side fix
  (a `CFBundleShortVersionString` in `au-info.plist` filled from the version) would make Phase 6's AU read report a real
  version with no manager change; the user ruled it out (Q6): no plugin-side change is made and no follow-up is recorded.
- **`InstallLocation`** is a documented Uninstall-key value but its presence in an Inno-written key is unconfirmed
  (per_os_install_mechanics.md:107). FR-026 treats it as optional, so the managed location falls back to the default
  vendor path when it is missing. Phase 10's `helper-elevated (windows-latest)` job installs a fixture Inno setup and is
  the first place a real key could be read; recording it there is that phase's choice.
- **Only the raw `OsHostIo` calls have no automated evidence** (D7). Every decision, meaning which argv, which subkey
  and every mapping from a raw answer to a record or a version, is in core and tested: the per-OS `Platform` methods
  on their own leg (SC-025, SC-014) and the mappers on every host (SC-026). The untested remainder is the FFI and
  `Command` glue of FR-032, which no test may run because it would read the real system (roadmap line 585). The user
  can audition the screen on this box with `npm run tauri dev` (read-only against the real folders and registry);
  that is a user step, not this phase's exit criterion.
- **D1 and Phase 10 (resolved, Q2).** The user ruled that any usable recorded `InstallLocation` is the managed location,
  so FR-001 no longer tests it against the vendor folder. Re-ruled by Q-C (a) for Phase 10 only (plan SI-14): the
  roadmap's Phase 10 `run_inno_uninstaller` rule is not widened; it keeps "under the compiled-in Windows plug-in root"
  and gains one sentence (FR-092): a managed copy whose recorded `InstallLocation` lies outside that root is never
  uninstalled by the helper, and the manager tells the user to uninstall it from Windows Settings, because the folder a
  user chose, and the `unins*.exe` / `unins*.dat` in it, can be writable without elevation. Phase 10's own spec must
  keep the helper's re-read of that value and its `unins*.exe` name check.
- **Revision 2 (review round).** The review's twenty-three issues were applied as follows:
  - **Architecture.** D7 was rebuilt so the OS readers sit behind `Platform` (fidelity, testability, reality).
  - **Registry reader.** It now opens the key first, so "key without values" is reachable (FR-026, FR-032).
  - **Departures.** D2's narrowing is departure (3). The `0.8.0` stamp is logged at `debug!` instead of silenced, so
    no departure from line 566 remains.
  - **Phase 10 consequence.** It is recorded for D1, and the managed `InstallLocation` is restricted to the vendor
    folder.
  - **Guards and sandbox.** The record notices are listed as additions. FR-089 guards AppIds and pkg identifiers.
    FR-005 sandboxes the `InstallLocation`.
  - **Evidence.** The PENDING-CI closing evidence matches the counts-only notice. A `core dependency tree` CI step
    carries the per-leg `cargo tree` check.
  - **Test definitions.** The fixture cases use `tenth-plugin.json`'s Delta. SC-012 / SC-019 coverage is mechanical
    (`STATUS_TEXTS`). Cross-host path rules are stated. SC-006's Linux row and SC-005's one-sandbox-per-row are
    defined. Every new guard is named and listed in FR-085. The missing cases were added: `concurrent_builds`, the
    9-deep quote and emphasis cases, the content markers, the missing `Info.plist`, the attribute walk and
    `probe_metadata_error_unix`. SC-003 builds its configured root through `reassemble_with_os`, and SC-024 is the
    `test_sources_hold_no_real_location` guard.
  - **Citations.** The `Cargo.toml` citations were corrected.
  - **One premise corrected, issue applied anyway.** The "reality" issue on `os_facts.rs` literals said the
    `FORBIDDEN_OUTSIDE_PLATFORM` scan covers `src-tauri/src`. It does not: `core_sources_hold_no_production_literal`
    reads only `crates/core/src` (`injection.rs:1981-1984`), and `scan_core_file` is called on nothing else
    (`:2001`). The literals have moved into `platform/` constants under `PLATFORM_LITERALS` (FR-033) in any case,
    because the `Platform` rebuild needs them there.
  - **Rejected.** No issue was rejected.
- **Revision 3 (clarification session, 2026-10-31).** The eight rulings of Clarifications were applied: Q1 `elsewhere`
  (FR-043, FR-046, FR-071), Q2 any recorded `InstallLocation` managed (FR-001, FR-092), Q3 the bounded listing (FR-006,
  FR-084), Q4 the pending state (FR-070, FR-073), Q5 and Q6 kept as written (FR-020 tightened to a three-part version),
  Q7 collapsed notes (FR-072, FR-075), Q8 the release-page button (FR-049, FR-064, FR-065, FR-078).
- **Revision 4 (compliance, 2026-10-31).** FR-090 contradicted FR-085: the shape test's `checkRequired`
  (`manager-ci-shape.test.js:739-748`) deep-compares `required-tests.json` with `EXPECTED_REQUIRED` (`:337`), so
  FR-085's additions force the same additions to that constant (as Phase 5 did, `krate-manager-phase5/spec.md:910`).
  FR-090 now names that edit; the shape test still changes in nothing else.

---

## Open questions

None. The roadmap defers no decision to this spec by name. D1-D15 are this spec's own decisions, confirmed or ruled in
the clarification session of 2026-10-31; the departures from roadmap wording (D6, D11, the Phase 10 sentence) and the
additions are listed under Traceability.

---

## Clarifications

### Session 2026-10-31

The user was interviewed on the eight open clarifications of the clarify scan. Each decision is carried by the FR / SC
rows named in brackets.

- Q: With no copy at the managed location but another copy found, what do Installed and the update state say? → B: a
  new Installed value `elsewhere` ("Installed outside the manager's location: <version>"), update state `not_installed`,
  all copies listed; one enum value, one `STATUS_TEXTS` row, one golden; Phases 9, 10 and 12 still act on the managed
  location only. [FR-043, FR-046, FR-071, SC-006, SC-012, SC-019, SC-031]
- Q: A re-run of an Inno installer (`UsePreviousAppDir=yes`) lands in a custom folder: which managed-location rule? → B:
  any usable recorded Inno `InstallLocation` (64-bit HKLM key, admin-only) is the managed location; FR-001 drops the
  `within` vendor-folder test; the roadmap's Phase 10 `run_inno_uninstaller` rule is widened now to "an `unins*.exe` in
  the install dir the helper reads from the same 64-bit HKLM key". [FR-001, FR-044, FR-092, SC-002, SC-008, SC-028]
  (re-ruled Q-C 2026-10-31: the Phase 10 widening is withdrawn; the rule keeps "under the compiled-in Windows plug-in
  root" and a managed copy outside it is sent to Windows Settings; the managed-location rule for detection stands;
  plan SI-14)
- Q: May detection list directories so nested copies are reported? → B: a bounded listing of each injected VST3 root, at
  most 4 levels below the root and 20,000 entries per root, matching only `facts.vst3_bundle` names (ASCII
  case-insensitive on Windows); `read_dir` is allowed in `detect.rs` only (the FR-004 and FR-084 guards narrowed to
  that); a nested copy is reported and the cap is honoured, logged once when hit. [FR-004, FR-006, FR-084, SC-029,
  SC-030]
- Q: What does the screen show before `plugin_status` returns, and may the first render wait on the network? → A: as
  written, plus a fixed pending text (`Checking for plugins…`) shown until `plugin_status` returns, and the Check now
  button disabled until the first result arrives; no cache-first command, no parallel detection. [FR-040, FR-070,
  FR-073, FR-084, SC-035]
- Q: Keep the record diagnostics (three view flags, three card sentences)? → A: keep the three flags
  (`record_without_files`, `record_version_differs`, `record_error`) in `StatusView` and their three sentences on the
  card. [FR-044, FR-046, FR-071, SC-008, SC-012]
- Q: The AU copy, whose shipped plist carries no plugin version? → A: read the AU `Info.plist` with the `plist` crate
  (macOS only), reporting a version only when `CFBundleShortVersionString` is a three-part version, else "version not
  recorded in the AU bundle"; no plugin-side follow-up. [FR-020, FR-021, SC-013]
- Q: How are release notes presented in the 800 × 600 window? → B: collapsed per card in `details` / `summary`
  ("Release notes"), closed by default; FR-075's element and attribute lists gain `details`, `summary` and `open`.
  [FR-072, FR-075, SC-019]
- Q: May the screen link to the release page? → C (the user's choice, not the recommendation): one `Open release page`
  button per card opens the feed's `release_notes_url` in the system browser through the Tauri opener plugin
  (`tauri-plugin-opener` 2.7.2, Rust `OpenerExt::open_url`; npm `@tauri-apps/plugin-opener` 2.7.2 `openUrl` is its
  JavaScript counterpart and is not used), with core validating the URL against the allow-listed `https` host
  `github.com` before the open; no `<a>` element and no notes link destination (D9 for notes unchanged); the button is the
  only navigation path. Recorded interpretation: the capability permission added is the app's own
  `allow-open-release-page` (the command that wraps the opener); the opener's own `opener:*` permissions are not granted to
  the webview, because that would let it bypass the core check. [FR-049, FR-061, FR-063, FR-064, FR-065, FR-075, FR-078,
  SC-021, SC-032, SC-033, SC-034]

### Session 2026-10-31 (plan preconditions)

The user ruled on the four preconditions of plan §10 and tasks.md "Preconditions", all as recommended.

- Q-A: after a rejected "Check now", keep the previous cards or clear them? → keep the previous cards and show the error
  in the feed line (plan SI-11, PD-13). [FR-073, SC-035]
- Q-B: add a `CHANGELOG.md` `### Added` bullet for the status screen? → no; no task touches
  `apps/krate-manager/CHANGELOG.md`.
- Q-C (a re-ruling of Q2 for Phase 10 only): may Phase 10 run the `unins*.exe` in a recorded `InstallLocation` outside
  the compiled-in Windows plug-in root? → (a) no: keep the roadmap's "under the compiled-in Windows plug-in root"
  clause; a copy installed elsewhere is still shown as the managed copy (Q2 stands for detection), but the manager never
  runs its uninstaller and tells the user to uninstall it from Windows Settings (plan SI-14). [FR-092, SC-028, D1]
- TA-1: may two tests start a process? → yes, exactly `mkfifo` in `detect::tests::probe_metadata_error_unix` and
  exactly `cmd` (`/C mklink /J`) in `detect::tests::listing_skips_junction_windows`, both acting inside the test's
  `TempDir`, through a body-scoped allow list in `injection.rs`. [FR-086, SC-027, SC-029]
