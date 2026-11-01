# Feature Specification: Krate Manager Phase 9 — Linux Install, Update and Uninstall (unprivileged)

**Spec slug:** `krate-manager-phase9`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 9 (lines 663-705); Decisions Q3 (line 26), Q7 (line 30), Q8 (line 31),
ROSTER (line 33), STRATEGY (line 23: "Linux: the manager extracts the existing `tar.gz` itself and tracks it in a
per-plugin manifest"); Per-OS table, Linux row (line 118); "Repo fact that shapes the strategy" (lines 120-126);
"Where state lives" (lines 158-163: "one install manifest per Linux plugin … the swap journal (Phase 9, Linux)");
Phase 10, the next consumer of the `InstallDriver` trait (lines 753-759); Phase 12, "Update all" over it (lines
882-886); Dependency Graph edges 4→9, 6→9, 8→9, 9→10 (lines 956-957); Cross-Cutting Constraints (lines 984-1025).
**Depends on:** Phase 4 (`Roots`, `Settings::linux_plugin_root_or_default`, `AppContext`, the atomic-write pattern of
`settings::save_with`, `test_support::fresh_temp_roots`, `manager-ci.yml`), Phase 5 (`PluginEntry`, `InstallFacts`
with `vst3_bundle` and `preset_subdir`), Phase 6 (`detect::read_manifest` and the three manifest keys it reads, the
managed Linux candidate, `status::build`, the status screen), Phase 7 (`download::obtain_and`, `download::discard`,
`download::command_request`, `VerifiedArtifact`, the `Channel` progress adapter), Phase 8 (`extract::stage`,
`StagedTree`, `extract::discard`, `extract::prune`, `CanonicalListing::from_entries`).
**Consumed by:** Phase 10 (implements the same `InstallDriver` trait for Windows and macOS, edge 9→10), Phase 12
("Update all" and the settings screen run over the trait and the settings model).
**Status:** DRAFT, revision 3 (plan SI-1 to SI-27 encoded): specification only, no implementation. Revision 2 resolves the 2026-11-01 challenge
review (fidelity, testability, reality lenses). Decisions D1 to D16 were settled by the 2026-11-01 clarification session (see Clarifications); OQ-1
is ruled (Q3).
**Date:** 2026-11-01 · verified against `feat/krate-manager` at `abdd2c9e` ("feat(krate-manager): Phase 8 safe
extraction and staging of the Linux tar.gz", `git rev-parse --short HEAD`, clean tree); `specs/krate-manager-phase9/`
did not exist before this file.

---

## Overview

Phase 9 is the first phase that writes outside the manager's state dir. On Linux, which has no installer, the manager
turns a verified, staged release tarball (Phase 8 `StagedTree`) into an installed plug-in: the bundle goes to
`<Linux plug-in root>/<vst3_bundle>` (default `~/.vst3`, no prompt) and the factory presets to
`~/Documents/Krate Audio/<Name>[/<preset_subdir>]`, the user preset folder the plugins already scan
(`preset_paths.cpp:8-31`) and the place the Linux README tells users to copy them
(`plugins/membrum/installers/linux/README.txt:36-40`). Every path the manager places is recorded with its hash in a
per-plugin install manifest at `<state>/manifests/<stem>.json`, the file Phase 6 already reads (`detect.rs:883-914`),
so Remove is exact and never touches a user preset.

A new module `core::install` holds the per-OS `InstallDriver` trait (one batch of install / update / uninstall actions,
one named result per action), its Linux implementation, the install planner, the journal, the manifest writer, the
start-up recovery pass and the command-level flow. Because `<P>.vst3` is a directory that `rename` cannot replace while
non-empty (`rename(2)`, External facts), the bundle is replaced by a **journaled two-rename swap**: the new tree is
copied beside the target (`<bundle>.krate-new`) and synced, the intent is written to `<state>/journal/<stem>.json` and
flushed, the old bundle is renamed aside (`<bundle>.krate-old`), the new one renamed in, the journal marked done and the
old bundle deleted. The factory presets ride the same journal as `.krate-new` siblings renamed over their targets after
the bundle commits, so a crash at any step leaves, after the recovery pass that runs at every start, exactly the old or
exactly the new install, bundle, presets and manifest together.

The status screen gains Install / Update / Remove per card on Linux, with progress through a Tauri `Channel`, a named
outcome, the "restart or rescan your DAW" notice after an install or update, and a refresh. On Windows and macOS no
driver exists until Phase 10, so their cards show no install action. Nothing in Phase 9 runs an installer, elevates,
or writes outside the injected roots.

---

## Scope

**In scope (roadmap Phase 9 components, lines 671-691):**

1. `core::install`: the `InstallDriver` trait (install, update, uninstall, each with a named result; lines 671-672) and
   its Linux implementation: an install plan from (the Phase 8 staged tree, the Phase 4 settings model, `Roots`) to
   destinations, the bundle to `<settings root>/<vst3_bundle>` and the factory presets to `<user preset root>/<Name>`
   plus `preset_subdir` when set (lines 672-676; D1 to D5).
2. The journaled bundle swap and the recovery pass at every start (lines 677-684; D6, D7).
3. Install manifests in the state dir, every placed path with its hash; update removes what the previous manifest had
   and the new payload lacks (lines 685-686; D8).
4. Uninstall, one Remove action per plugin, driven only by manifest paths under configured roots; user presets never
   touched; an edited placed file left and reported; no "Reinstall previous version" (lines 687-690; D9).
5. UI actions Install / Update / Remove per card, with progress, over the `InstallDriver` trait (line 691; D11 to D13,
   D15, D16): all three reach `InstallDriver::run` through the driver resolver (FR-090, FR-094).

**Non-goals (owned elsewhere):**

| Not in Phase 9 | Owner |
|---|---|
| Running an Inno `.exe` or a `.pkg`, the elevated helper, the job file, UAC / osascript launchers, the Windows and macOS `InstallDriver` implementations, the locked-file measurement | Phase 10 (roadmap lines 707-808) |
| "Update all", the download queue, a bound on parallel downloads, the one confirmation listing a **multi-item** batch | Phase 12 (lines 882-886); Phase 9's one-item batches carry their own one-item confirmation (D15, FR-099) |
| The settings **screen** (changing the Linux plug-in root), the Linux graphics toggle UI | Phase 12 (lines 887-889); Phase 9 only reads the Phase 4 settings model |
| Moving or re-rooting an existing install after the Linux plug-in root setting changes | Nowhere: no move or re-root code in any phase. Phase 12's settings screen refuses to change the Linux root while any manifest exists (the user removes first), recorded in the roadmap by FR-123. Phase 9 refuses Update and Remove on such a manifest (FR-064) |
| Removing a plugin that has a manifest but is no longer in the feed (orphan manifest) | Phase 12 (recorded in the roadmap by FR-123); Phase 9 Remove needs a live or cached feed entry (FR-094 step 1) |
| Removing shadowing or stale copies outside the managed location (e.g. `/usr/lib/vst3/<P>.vst3`) | Not in v1 (roadmap line 980); Phase 6 still lists them |
| "Reinstall previous version", rollback, a beta channel | Not in v1 (Q8, Q9; lines 971, 978) |
| DAW process detection, closing a DAW | Never (Q7; line 1000) |
| A system-wide Linux install (`/usr/lib/vst3`, `/usr/share/krate-audio`), any elevation on Linux | Never in v1 (Q3; per-OS table line 118: "None") |
| Self-update | Phase 11 |
| Any change to `plugins/`, `dsp/`, `cmake/`, `extern/`, `release.yml`, `feed.yml`, `ci.yml`, `manager-ci.yml`, `tools/feed-contract/` contracts, schemas, `listing.js` or committed fixtures | Never in this phase (FR-121) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 9 does with it |
|---|---|---|
| `specs/Krate-manager-roadmap.md:663-705` | Phase 9 components, outcomes, "Does NOT" | Source of every FR / SC (Traceability) |
| `specs/Krate-manager-roadmap.md:26` (Q3), `:118`, `:120-126` | Linux root `~/.vst3` with no prompt; factory presets to `~/Documents/Krate Audio/<P>` plus `preset_subdir`, tracked by the manifest, appearing as user presets; no elevation | ✅ D1, D2, FR-010 to FR-016 |
| `specs/Krate-manager-roadmap.md:31` (Q8) | One Remove button per plugin; removes the plug-in and its factory presets, never user presets; no reinstall-previous | ✅ FR-060 to FR-068 |
| `specs/Krate-manager-roadmap.md:30` (Q7), `:1000-1004` | No DAW is ever closed or detected; every install ends with a "restart or rescan your DAW" notice | ✅ FR-095 |
| `specs/Krate-manager-roadmap.md:33` (ROSTER), `:1017-1019` | Every per-plugin fact from the feed entry; no plugin name in code | ✅ FR-003, SC-019 |
| `specs/Krate-manager-roadmap.md:753-759` | Phase 10 implements "the trait from Phase 9", builds one batch job from verified downloads, maps each result to a named outcome | ✅ Why the trait takes a batch (D10) |
| `apps/krate-manager/crates/core/src/detect.rs:19-22`, `:883-914` | `MAX_MANIFEST_BYTES` 4 MiB; `MANIFESTS_DIR = "manifests"`; `read_manifest(state_dir, stem, plugin)` reads `<state>/manifests/<stem>.json`, requires `manifest_version` integer 1 (above 1 → `E_RECORD_FUTURE`), `plugin` equal to the entry name (`E_RECORD_MISMATCH`), `version` a string; unknown keys ignored | ✅ The manifest's location and its three fixed keys (FR-040); 🔶 none of it changes |
| `specs/krate-manager-phase6/spec.md:443-449` (FR-028) | "**Phase 9 writes this file** and may add keys; it never renames, retypes or removes these three" | ✅ The compatibility rule of FR-040 |
| `detect.rs:1009-1013`, `:1053`; `detect.rs:435-460` | Linux record = the manifest under `stem` (= `vst3_bundle` minus `.vst3`); the managed Linux candidate is `<linux_plugin_root_or_default>/<vst3_bundle>` | ✅ The install target is exactly the managed candidate (FR-010), so detection after an install reports it `managed` |
| `detect.rs:692` (`pub(crate) fn moduleinfo_version`) | Phase 6's moduleinfo reader | ✅ Not called by Phase 9 (Phase 8 FR-057 already checked the staged version) |
| `crates/core/src/settings.rs:20-33` | `Settings { linux_plugin_root, linux_graphics_workaround }`; `linux_plugin_root_or_default(&Roots)` = stored path, else `Roots::vst3_user()` | ✅ The plug-in root (FR-010); read only |
| `settings.rs:156`, `:228-231`, `:265-272`, `:281` | `save` over `save_with`: create dir → temp file → write → `sync_all` → `NamedTempFile::persist` → (Unix) directory `sync_all` | ✅ The step order FR-044 follows for the manifest and the journal, through `InstallIo` instead of `NamedTempFile` |
| `crates/core/src/roots.rs:24-28`, `:172-185`, `:191-201`, `:210-216` | `PresetRoots { factory, user }`; `check_sandboxed` panics on a test `Roots` for a path outside the sandbox, **lexically** (`starts_with`), with the message `install location … is outside the test sandbox …` (`:179-183`); `presets()`, `state_dir()`, `staging_dir()`; `vst3_user()` | ✅ The preset root is `presets().user` (D2); every destination is passed through `check_sandboxed` (FR-110) |
| `crates/core/src/platform/linux.rs:30-60` | Linux `default_roots`: `Vst3User` `~/.vst3`, four `Vst3Global` roots, `presets.factory` = `presets.user` = `~/Documents/Krate Audio`, state dir under `$XDG_DATA_HOME` or `~/.local/share` | ✅ The production roots Phase 9 writes under; unchanged |
| `crates/core/src/context.rs:50-60`, `:84-115` | `AppContext { roots, settings, feed_url, http, signing_key }`; `assemble_with_os`: lock, load settings, fold the configured Linux root, return | 🔶 Calls `install::recover` after the fold (FR-080) |
| `crates/core/src/state_lock.rs:1-53` | One process per state dir (`manager.lock`, exclusive `try_lock`) | ✅ Why journals and manifests have one writer process (D7, FR-085) |
| `crates/core/src/feed.rs:44-52`, `:121-128`, `:136-149` | `PluginEntry { name, version_text, version, …, release }`; `InstallFacts { vst3_bundle, au_component, preset_subdir, windows_uninstall_key, macos_pkg_id }`; `PlainName` with no public constructor | ✅ `vst3_bundle` and `preset_subdir` drive every destination (FR-010, FR-011) |
| `crates/core/src/contract.rs:137`, `:267-293`, `:1037-1049` | Plugin name `^[A-Z][A-Za-z0-9]{0,63}$`; `check_install_value` refuses `/`, `\`, `:`, `..`, a trailing space in `preset_subdir`, reserved names; `vst3_bundle` and `preset_subdir` patterns | ✅ Every feed string that becomes a path segment already passed these; FR-012 re-checks "one normal component" before the join |
| `tools/feed-contract/CONTRACT.md:73`, `:258-268` | `install.preset_subdir`: one segment, read by "manager (preset folder)"; C4 root = `<vst3_bundle>`, `presets`, `README.txt`, `CHANGELOG.txt` | ✅ D3 (which root items are placed) |
| `crates/core/src/extract.rs:1606-1611`, `:1617-1640`, `:2063-2114`, `:2129-2150` | `prune(env) -> usize`; `stage(env, entry, &artifact) -> Result<StagedTree, ExtractError>`; `StagedTree` accessors `root`, `bundle`, `presets`, `name`, `version`, `vst3_bundle`, `listing`, `archive_sha256`, `file_hashes` (no move API, Phase 8 FR-054); `discard(tree, io)` | ✅ The only source Phase 9 installs from (FR-001); Phase 9 copies out of it and discards it |
| `extract.rs:964-1037` | `EntryType`, `ListingEntry`, `CanonicalListing::from_entries`, `lines`, `digest` | ✅ The listing of an installed bundle in SC-002 / SC-003 |
| `extract.rs:224-231`, `:241-258` | `pub enum NodeKind { Dir, File, Symlink, Other }`; `ExtractIo` (`&self` methods, every filesystem step injected, `kind_of` → `io::Result<Option<NodeKind>>` never follows a link, `set_mode` a no-op off Unix) | ✅ The pattern and the kind type of `InstallIo` (D12, FR-028) |
| `specs/krate-manager-phase8/spec.md:450-457` (FR-054, FR-055), `:458-464` (FR-056), `:486-501` (FR-058) | `StagedTree` is a read-only source; Phase 8 syncs nothing ("Phase 9 syncs what it places"); per-file SHA-256 of the written bytes; `extract::prune` is what "Phase 9's start-up recovery pass calls" | ✅ FR-020 (copy, never move), FR-023 (sync), FR-022 (hash on copy), FR-081 (prune at start) |
| `crates/core/src/download.rs:653-671` | `InFlight` registry: a second in-flight call for the same key returns at once | ✅ The model of the per-plugin action registry (FR-085) |
| `download.rs:1319-1340`, `:1480-1516`, `:1518-1546`, `:1569-1580`, `:1582-1591` | `obtain_and(env, feed, request, progress, next: FnOnce(VerifiedArtifact) -> T)`; `discard(artifact, io)`; `command_request` (feed check, `request_for`, `expected_version` → `E_DOWNLOAD_VERSION_CHANGED`); `for_command`; `channel_progress` | ✅ FR-090's flow: the install runs inside `obtain_and`'s `next`, the artifact is discarded after success |
| `crates/core/src/version.rs:66-94` | `Installed`, `UpdateState` (`NotInstalled`, `UpToDate`, `UpdateAvailable`, `InstalledNewer`, `Unknown`, `:75-81`), `compare` ("An older available version is never an update") | ✅ FR-093's action table |
| `version.rs:200-208`, `:211-240` | `platform_candidates(os, arch)`; `resolve(entry, os, arch, running)`; `resolve_native` fixes `NativePlatform.os()` and `Arch::native()` | ✅ Why the download leg is native-keyed and the test feeds put a `tar.gz` asset under the native key (FR-090) |
| `download.rs:753-768` | `request_for` resolves availability with `version::resolve_native`; an unknown name is `E_DOWNLOAD_UNKNOWN_PLUGIN` (`:761`, code table `:443`) | ✅ Unchanged (FR-120); FR-090 step 2 |
| `crates/core/src/feed.rs:741-743`, `:748-757` | `feed::check(ctx, mode, now)` over `check_with`, which always performs `http::get(&ctx.http, &ctx.feed_url, …)` (Normal mode has no cache-first path) and falls back to the cached copy on any failure | ✅ Every command makes exactly one feed request (SC-008, SC-016) |
| `crates/core/src/verify.rs:92-105`, `:113` | `VerifiedArtifact`'s compile-fail doctests (no forging); `#[derive(Clone, Debug)]` | ✅ `next` clones the artifact before it moves into `Action` (FR-026) |
| `crates/core/src/status.rs:57-71`, `:330-370`, `:176-199`, `:213-219` | `PluginView` fields; the managed copy decides installed and update state; `build(ctx, mode, now, host)` (`:176`) fixes `let os = NativePlatform.os();` (`:179`) and passes `Arch::native()` (`:197`); `assemble(os, arch, check, running, detections)` (`:213`) is pure and takes the OS | 🔶 `PluginView` gains `actions`; `build` becomes a wrapper over `build_with_os`; `assemble` over `assemble_with_gates` (FR-100) |
| `detect.rs:181-185`, `:976-982` | `HostFacts` has `os_version`, `inno_values`, `pkg_outputs` and no `os()`; `detect_plugin(os, ctx, entry, host, listing)` takes the OS | ✅ The OS reaches detection as an input (D16) |
| `context.rs:73-79`, `:84-90` | `assemble_context` passes `NativePlatform.os()` to `assemble_with_os(os, …)` | ✅ The pattern D16 follows |
| `apps/krate-manager/src-tauri/src/commands.rs:58-100` | `download_plugin(state, name, expected_version, on_progress: Channel<Progress>)`: clones the context, `spawn_blocking`, a thin wrapper over `download::for_command` | 🔶 Two more commands of the same shape (FR-096) |
| `src-tauri/build.rs:1-12`; `src-tauri/capabilities/main.json`; `src-tauri/src/lib.rs:87-94`; `src-tauri/permissions/autogenerated/*.toml` (6 files) | Six registered commands, six `allow-*` permissions, six handlers | 🔶 Eight of each (FR-096) |
| `crates/core/tests/config_guard.rs:152`, `:649`, `:679-691` | `HANDLER_LIST`, the capability permission list and the `build.rs` command list, each asserted exactly | 🔶 Extended to the eight commands (FR-096) |
| `apps/krate-manager/src/status.ts:87`, `:177-204`, `:207-243`, `:267-318`; `src/view.ts:71-86` | `Invoker`; the `Open release page` button pattern (disable while running, error line); `renderCard`; the `Check now` refresh; `PluginView` type | 🔶 Action buttons, progress, outcome and refresh (FR-097 to FR-099) |
| `node_modules/@tauri-apps/api/core.d.ts:103-115` (2.12.4) | `class Channel<T> { id; constructor(onmessage?) }` | ✅ The webview side of progress (External facts) |
| `crates/core/tests/injection.rs:152`, `:226-246` (`FR084_TOKENS`), `:325-367` (`EXTRACT_FS_TOKENS`, `EXTRACT_BANNED`), `:447` (`TEST_PROGRAM_BODIES`), `:2428-2433` (`is_fr084_file`), `:2642-2651` (`verify_file` callers), `:2990-3020` (`extract_findings`: `flate2` only in `extract.rs`, `StagedTree {` only in `extract.rs`) | The token and confinement rules | 🔶 An `install_findings` rule for `install.rs` (FR-112); `INSTALL_TESTS_RS` joins the `verify_file` callers (FR-113) |
| `crates/core/src/extract_tests.rs:106-138`, `:154-271` | Fixture helpers `sha` (`:106`), `unhex` (`:112`), `literal_payload` (`:131`); the archive builder `gz` (`:154`), `gunzip` (`:160`), `moduleinfo(version)` (`:168`), `octal_field` (`:177`), `reseal` (`:185`), `struct Tar` + `impl Tar` (`:195-271`); every item private (no `pub`) | 🔶 Moved to a shared `#[cfg(test)]` file and made `pub(crate)` so `install_tests.rs` builds versioned archives (FR-114) |
| `crates/core/tests/injection.rs:359-368` (`TEMPFILE_REAL_TEMP`), `:2982-3000` | The `tempfile` entry points that create under the real `env::temp_dir()`; `flate2` confined to `extract.rs` in **non-test** code only | 🔶 FR-112 bans the `TEMPFILE_REAL_TEMP` tokens from `install.rs`; the moved `gz` stays legal in a `_tests.rs` file |
| `.github/workflows/manager-ci.yml:162` | `required tests ran` matches `/^test (\S+)(?: - should panic)? \.\.\. ok$/` | ✅ A doctest line (spaces in its name) can never be a required name (FR-116) |
| `crates/core/src/test_support.rs:98-131`, `:175` | `roots_at`: `plugins/vst3-user`, `plugins/vst3-global`, `presets/{factory,user}`, `state`, sandboxed; `fresh_temp_roots()` | ✅ Every Phase 9 test installs under a `fresh_temp_roots()` TempDir |
| `apps/krate-manager/tests/required-tests.json` (`cargo.all` 248, `windows` 11, `macos` 35, `linux` 30; `vitest`); `.github/workflows/manager-ci.yml:117-120`, `:153-178` | Step `cargo test` runs `cargo test --workspace --locked`; step `required tests ran` requires each name of `cargo.all` plus the leg's lane exactly once, and each `vitest` title exactly once | 🔶 The Phase 9 SC test names (FR-116); the workflow is unchanged |
| `plugins/shared/src/platform/preset_paths.cpp:8-31`, `:33-51` | User presets `~/Documents/Krate Audio/<pluginName>` on every OS; Linux factory `/usr/share/krate-audio/<lowercase>` | ✅ Why the Linux destination is the user folder (Q3) |
| `plugins/shared/src/preset/preset_manager.cpp:112-121`, `:316-330` | Scans the user dir, then the factory dir; saves a user preset at `<userDir>/<subcategory>/<name>.vstpreset` | ✅ Factory and user presets share directories on Linux, so directories are removed only when empty and a same-named user file is never overwritten (D5, FR-062) |
| `plugins/membrum/src/preset/membrum_preset_config.h:24`; `plugins/membrum/version.json:8` | Membrum's kit `pluginName` is `"Membrum/Kits"`; `"preset_subdir": "Kits"` | ✅ The `preset_subdir` join (FR-011) |
| `plugins/membrum/installers/linux/README.txt:29-46`; `plugins/iterum/installers/linux/README.txt:36-37` (and the seven other READMEs, same lines ±1) | `cp -r presets/* ~/Documents/Krate\ Audio/Membrum/Kits/`; `cp -r presets/* ~/Documents/Krate\ Audio/Iterum/` | ✅ The exact mapping FR-011 automates |
| `.github/workflows/release.yml:416-435` | Linux staging: `<Name>.vst3`, `presets`, `README.txt`, `CHANGELOG.txt`, then `tar -czvf` | ✅ What a staged tree holds; unchanged |

---

## External facts used (verified this session)

The roadmap names no crate, npm package or Tauri plugin for Phase 9, and Phase 9 adds none (FR-111). The facts below
are the standard-library and OS semantics the design rests on.

| Fact | Source |
|---|---|
| `std::fs::rename`: "Renames a file or directory to a new name, replacing the original file if `to` already exists. This will not work if the new name is on a different mount point." "On Unix, when `from` is a directory and `to` exists, `to` must be an empty directory." "On Windows 10 version 1607 and above, the behavior is the same as Unix if the filesystem supports `FileRenameInfoEx`. Otherwise on Windows, `from` can be anything but `to` must not be a directory." Stable 1.0.0 | https://doc.rust-lang.org/std/fs/fn.rename.html (std 1.99.1, 2026-10-13) |
| `rename(2)`: "If newpath already exists, it will be atomically replaced"; for a directory, "newpath must either not exist, or it must specify an empty directory"; `EXDEV` "oldpath and newpath are not on the same mounted filesystem"; `ENOTEMPTY` / `EEXIST` "newpath is a nonempty directory" | https://man7.org/linux/man-pages/man2/rename.2.html (Linux man-pages 6.19, 2026-02-08) |
| `unlink(2)`: "If the name was the last link to a file but any processes still have the file open, the file will remain in existence" until the last descriptor is closed | https://man7.org/linux/man-pages/man2/unlink.2.html (Linux man-pages 6.19) |
| `File::sync_all`: "Attempts to sync all OS-internal file content and metadata to disk … ensure that all in-memory data reaches the filesystem before returning"; `sync_data` "might not synchronize file metadata". Stable 1.0.0 | https://doc.rust-lang.org/std/fs/struct.File.html#method.sync_all (std 1.99.1) |
| `std::fs::remove_dir_all`: "This function does **not** follow symbolic links and it will simply remove the symbolic link itself"; "On most platforms, `fs::remove_dir_all` protects against symlink TOCTOU races by default" (not on Miri, QNX, Redox, VxWorks) | https://doc.rust-lang.org/std/fs/fn.remove_dir_all.html (std 1.99.1) |
| `std::fs::OpenOptions::create_new` (atomic; fails on an existing file or dangling symlink), `std::fs::canonicalize`, `std::os::unix::fs::{symlink, PermissionsExt}` | As cited and verified by Phase 8 (`specs/krate-manager-phase8/spec.md:132-135`, std 1.99.1) |
| Tauri channels: "The Tauri channel is the recommended mechanism for streaming data … to the frontend"; Rust `tauri::ipc::Channel<T>` as a command argument, `channel.send(…)`; JS `import { invoke, Channel } from '@tauri-apps/api/core'`, `new Channel<T>()`, `onmessage`, passed as an `invoke` argument; "Channels are designed to be fast and deliver ordered data"; async commands with borrowed `State` return a `Result` | https://v2.tauri.app/develop/calling-rust/ and https://v2.tauri.app/develop/calling-frontend/ (Tauri 2; workspace `tauri` 2.12.5, `Cargo.lock:3833-3834`; `@tauri-apps/api` 2.12.4, `package.json`) |
| Toolchain `rustc 1.99.1`, workspace `rust-version = "1.90"`, edition 2024 | `apps/krate-manager/Cargo.toml:6-11`; Phase 8 External facts |

---

## Decisions taken in this spec (all confirmed in the 2026-11-01 clarification session)

The roadmap defers no decision to this spec by name. D1 to D16 (D6, D7, D10, D14, D15 explicitly accepted by the user) settle what its Phase 9 text names but does not pin down.

- **D1, the plug-in destination is the managed candidate.** The bundle goes to
  `ctx.settings.linux_plugin_root_or_default(&ctx.roots).join(<vst3_bundle>)`, exactly the path Phase 6 marks managed
  on Linux (`detect.rs:435-460`, `:1053`). Using the feed's `vst3_bundle` (not `<Name>.vst3`) keeps the install, the
  Phase 8 layout check (Phase 8 FR-033) and detection on one name. The plug-in root is created (`create_dir_all`) when
  absent, and is never recorded in the manifest or removed.
- **D2, the preset destination is `presets().user/<Name>[/<preset_subdir>]`.** On Linux `presets().factory` and
  `presets().user` are the same path (`linux.rs` default roots); `user` is chosen because Q3 rules the presets "appear
  as user presets". `<Name>` is the feed key, which equals each plugin's `pluginName` prefix (`iterum_preset_config.h:21`
  … ; Membrum `"Membrum/Kits"`, `membrum_preset_config.h:24`) and passed the plugin-name pattern (`contract.rs:137`).
  The tree under the staged `presets/` maps one to one below that folder, as the READMEs' `cp -r presets/*` does.
- **D3, `README.txt` and `CHANGELOG.txt` are not placed.** The roadmap places "the plug-in bundle" and "the factory
  presets" (lines 673-676) and nothing else; the two text files stay in the staged tree and are discarded with it.
- **D4, plan before write.** Every destination, every collision and every keep / replace / adopt decision is computed
  by a read-only planner before the first write, as Phase 8 D3 does for archives. A refusal found by the planner
  writes nothing (zero mutating `InstallIo` calls).
- **D5, preset placement rules (no user file is ever overwritten).** Factory and user presets share folders on Linux
  (`preset_manager.cpp:316-330` saves user presets into the same subcategory folders). Per destination preset file:
  absent → placed; present, regular file, and listed in the previous manifest with an equal hash → replaced; present
  with content equal to the new file → adopted (recorded, not written; this covers a manual README-style install);
  anything else present (a user-edited factory preset, a user file of the same name, a symlink, a directory) → left in
  place, not recorded, and reported as `kept`. Directories are recorded only when Phase 9 owns them: a `d` entry for
  every directory this action creates, plus every directory the previous manifest recorded that the new layout still
  has; a pre-existing directory the manager did not create is never recorded (so never removed) (plan SI-17). Stale files (in the previous manifest, absent from the new payload) are
  removed only when their current hash equals the recorded one; otherwise kept and reported. The bundle is not
  file-by-file: it is one unit on both paths (clarification Q2). Update replaces it as a whole by the swap (D6), and the
  old bundle, including any file the user put inside it, is deleted, as the roadmap's "delete old" (line 680) states;
  Remove deletes the whole `<vst3_bundle>` tree with `remove_tree`, never following a link (FR-061, FR-122).
  Keep-and-report applies to preset files only. A preset the user edited since install stays, the new factory version
  is not placed, and the path is reported as kept (Q7, SC-005).
- **D6, the journal is written before the first byte is staged.** The roadmap lists the swap as "stage …; write the
  intent … and flush it; rename old …; rename new …; mark the journal done; delete old" (lines 678-681). Phase 9 adds
  one earlier journal write (step `begun`) naming the target, the `.krate-new` and `.krate-old` paths and every preset
  sibling, so a crash while staging leaves leftovers the recovery pass can attribute exactly; with that record, the
  manager never deletes a `.krate-new` / `.krate-old` it did not journal (a stray one is a collision, FR-014). The
  roadmap's journal write is step `staged`. Every crash point the roadmap names (line 696-698) is covered, plus the
  extra ones (SC-002).
- **D7, presets commit with the bundle.** New preset files are written as `<dest>.krate-new` siblings while staging and
  renamed over their destinations only after the bundle is renamed in. The commit point of the whole install is the
  `new → target` rename (FR-024 step 6, `new_in`): before it, recovery and the in-process roll-back roll everything
  back (rename `.krate-old` back when `old_aside` ran, remove the siblings and `.krate-new`); from it on, they roll
  everything forward (rename the remaining siblings in, write the pending manifest kept in the journal, remove stale
  files, delete `.krate-old`). The recovery pass tells the two apart from the journal's `step` and `had_old` and from
  what is on disk (FR-071). So bundle, presets and manifest are always one version.
  **Deviation from roadmap line 682, accepted in clarification Q1; the roadmap is amended to match (FR-123).** The roadmap's recovery rule completes the swap on
  "new present, target missing → rename new in". With the commit point at `new_in`, that state (an update cut after
  `old_aside`, or a first install cut after `journal_staged`) is rolled **back** instead: the old bundle is renamed back,
  or a first install ends absent. Reason: the same disk state is what an in-process failure of the `new_in` rename
  leaves (EACCES, EBUSY on the parent), and only a roll-back can always return the user to a working install there;
  rolling forward would retry the rename that just failed. The roadmap's outcome (exactly old or exactly new, by C4
  listing digest, no leftovers, lines 696-698) is unchanged; only which side a cut between `journal_staged` and `new_in`
  lands on differs. SC-002 lists the verdict per cut.
- **D8, the manifest format.** Manifest `manifest_version: 1` keeps Phase 6's three keys and adds `vst3_bundle`,
  `archive_sha256`, `plugin_root`, `preset_root` (absolute, UTF-8) and `files` (each `{root: "plugin" | "presets", type:
  "f" | "d" | "l", path, sha256?, target?}`, `path` relative POSIX to its root). Absolute roots are recorded so a later
  change of the Linux root setting is detected instead of followed (FR-064); entries are relative so no manifest string
  is ever a full path on its own. A file whose name is not UTF-8 cannot occur (C4 names are UTF-8); a root that is not
  UTF-8 is refused before any write (FR-016).
- **D9, Remove is per path, idempotent and resumable.** Remove deletes manifest paths only (files and links whose state
  matches the record, then directories bottom-up when empty), deletes the manifest last, and journals itself (op
  `remove`) so the recovery pass finishes an interrupted Remove. The bundle is deleted whole (FR-122); a preset file the user changed is
  kept and reported (roadmap line 689; the keep rule covers presets only, Q2).
- **D10, `InstallDriver` takes a batch.** `fn run(&self, env: InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult>`
  with `Action::{Install, Update, Uninstall}` and one named `ActionResult` per action, in order. Phase 10 needs one
  elevation prompt per batch and one result per operation (roadmap lines 711-713, 751-756); per-plugin methods would
  force one prompt per plugin. The Linux driver runs the batch sequentially and one failure never stops the rest.
- **D11, one command per user intent, re-checked in core.** `install_plugin(name, expected_version, action:
  "install" | "update", on_progress)` and `remove_plugin(name, on_progress)`. The core parses `action` itself
  (`E_INSTALL_ACTION`), re-runs the feed check (`download::command_request`, Phase 7 FR-065; `feed::check` for Remove)
  and detection, recomputes the action with the same function the status screen uses (D13), and refuses a mismatch
  with what the UI showed (`E_INSTALL_STATE_CHANGED`) before any download. `download_plugin` stays (Phase 7 FR-066).
- **D12, every filesystem step behind `InstallIo`.** As `ExtractIo` (`extract.rs:241-258`) and `StagingIo` do:
  production `FsInstallIo`; tests inject faults, a crash cut (every mutating call after a chosen step returns an error
  and cleanup does not run: the state a killed process leaves; never a panic, which unwinds and runs destructors,
  Phase 8 D5), a fail-closed recorder and an in-memory `MemInstallIo` seeded from a `MemExtractIo`-staged tree (Windows
  evidence for links and modes, as Phase 8 D4).
- **D13, actions are computed in core, by one function.** `install::allowed_actions(driver_present, gate, availability,
  update_state)` (FR-093) is the only place an action is allowed: `status` calls it to fill `PluginView.actions`
  (FR-100) and `for_command` / `remove_for_command` call it to accept or refuse (FR-090, FR-094). Its `gate` input,
  `install::action_gate` (FR-093), reads the pending journal (FR-052), the full manifest (FR-041), the recorded roots
  (FR-064) and the entries' resolution (FR-063), read-only. So the UI offers exactly what the core accepts for the same
  disk state; the UI renders buttons from the list and nothing else. Off Linux the list is empty in Phase 9 (no driver).
- **D14, recovery runs inside `assemble_with_os`**, after the state lock and the settings fold, so it is core code under
  test and runs before any command can be served. It never fails startup: a problem is logged and the affected plugin's
  actions are refused with `E_INSTALL_JOURNAL_PENDING` until it is resolved.
- **D15, every action is a one-item batch with its one confirmation.** Cross-Cutting line 1000: "Any batch of
  installs, updates or removals asks for one confirmation listing every item". Every Phase 9 action runs as a batch of
  one (`driver.run(env, vec![action])`, D10), so Install, Update and Remove each ask one in-page confirmation that lists
  its one item (plugin, version, what happens) before the command is invoked (FR-099). The user confirmed this (Q6): a single-card click is not itself the confirmation. Phase 12's "Update all" (lines
  882-883) extends the same confirmation to a list of many items; nothing in Phase 9 narrows the rule.
- **D16, the OS and the driver are inputs.** `status::build` fixes `NativePlatform.os()` (`status.rs:179`) and
  `HostFacts` has no `os()` (`detect.rs:181-185`), so a native-only command flow could test the Linux driver end to end
  only on the ubuntu leg. Following `context::assemble_with_os` (`context.rs:84`) and `detect::detect_plugin(os, …)`
  (`detect.rs:976`), the command env carries `os` and a driver resolver (`install::driver_for` in production, a
  counting or recording fake in tests, FR-004), and `status` gains `build_with_os(os, arch, …)` (FR-100). Every SC
  passes its `os` explicitly; the production wiring (`NativePlatform.os()`, `driver_for`, `FsInstallIo`, `status::build`)
  is proven per OS by SC-029.

---

## Functional Requirements

### A. The trait, the source and the inputs (roadmap lines 671-676; D10)

- **FR-001** `install::InstallDriver` is a public trait with `fn os(&self) -> Os` and `fn run(&self, env:
  InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult>`. `Action::Install { entry: PluginEntry, artifact:
  VerifiedArtifact }`, `Action::Update { entry, artifact }`, `Action::Uninstall { entry: PluginEntry }`. `run` returns
  exactly one `ActionResult { name, action, outcome: Result<Outcome, InstallError> }` per action, in batch order; a
  failed action never stops the next. Nothing in the trait takes a path, a URL or bytes from the caller.
- **FR-002** The Linux driver installs only from a `StagedTree` it obtains itself through `extract::stage(…, entry,
  &artifact)` (Phase 8 FR-001); a `VerifiedArtifact` is the only input (its only constructor is verification,
  `verify.rs:113-162`). An action whose artifact `format()` is not `TarGz` is refused with `E_INSTALL_FORMAT` before
  `stage` is called. An `ExtractError` is returned as the action's error unchanged (its code first).
- **FR-003** Every per-plugin fact comes from the action's `PluginEntry` (ROSTER): `name`, `version_text`,
  `release.install.vst3_bundle`, `release.install.preset_subdir`. An entry with no `release` or no `install` is
  `E_INSTALL_NOT_MANAGEABLE`. The core holds no plugin name, bundle name or folder name of its own (SC-019).
- **FR-004** `install::driver_for(os: Os) -> Option<&'static dyn InstallDriver>` returns the Linux driver for
  `Os::Linux` and `None` for `Os::Windows` and `Os::Macos` in Phase 9 (Phase 10 fills them). `InstallEnv<'a> { ctx:
  &AppContext, io: &dyn InstallIo, extract: ExtractEnv<'a>, progress: &mut dyn FnMut(InstallProgress) }` carries the
  driver's inputs; the driver logic is OS-neutral code that compiles and is tested on every OS (D12), and only the
  resolver decides where it is offered.
  The command flow (FR-090, FR-094) takes `CommandEnv<'a> { os: Os, arch: Arch, ctx: &'a AppContext, io: &'a dyn
  InstallIo, extract: ExtractEnv<'a>, download: DownloadEnv<'a>, drivers: &'a dyn DriverResolver, #[cfg(test)]
  pub(crate) request_seam: Option<&'a dyn Fn(DownloadRequest) -> DownloadRequest> }` (D16; `arch` plan SI-1,
  `drivers` plan SI-10, `request_seam` plan SI-2): `os` picks the driver, the detection (`detect::detect_plugin(os, …)`)
  and the action gate; `drivers` is `&NativeDrivers` in production (`src-tauri`, FR-096) and a counting or recording
  resolver in tests. The resolver is a trait, `pub trait DriverResolver { fn driver_for(&self, os: Os) -> Option<&dyn
  InstallDriver>; }`; production `pub struct NativeDrivers;` delegates to `install::driver_for` (its `&'static` result
  shortens to `&self`'s lifetime); test resolvers return `Some(&self.driver)` (plan SI-10). `for_command` applies
  `request_seam` to the request `command_request_for` returned, before `obtain_and`; only `install_tests.rs` ever
  assigns the field a `Some` (plan SI-2). `CommandEnv` is `Copy` (`#[derive(Clone, Copy)]`: every field is a shared
  borrow, `Os`, `Arch`, `ExtractEnv`, `DownloadEnv` or an `Option` of a shared borrow) (tasks TA-4).
  `extract.ctx` and `download.ctx` are the same `ctx`.

### B. Destinations (roadmap lines 672-676, Q3; D1, D2, D3)

- **FR-010** Plug-in destination: `plugin_root = ctx.settings.linux_plugin_root_or_default(&ctx.roots)`, target
  `plugin_root/<vst3_bundle>`. `plugin_root` is created with `create_dir_all` when absent (never recorded, never
  removed); when it exists it must be a directory, read without following a final link only for the check that it is
  not a file (a symlinked `~/.vst3` is a valid root and is used as is). A `plugin_root` that exists as a file is
  `E_INSTALL_COLLISION { path: { root: "plugin", path: "" } }`, zero writes (plan SI-13).
- **FR-011** Preset destination: `preset_dir = ctx.roots.presets().user / <Name>` and, when
  `install.preset_subdir` is `Some(s)`, `/ <s>`. The staged `presets/<rel>` maps to `preset_dir/<rel>` for every listing
  line under `presets/`; the `presets` directory itself maps to `preset_dir`. The directories between
  `presets().user` (exclusive) and `preset_dir` (inclusive) that do not exist are created and recorded as `d` entries;
  `presets().user` itself is created when absent and never recorded or removed.
- **FR-012** Every feed string joined to a root (`<Name>`, `vst3_bundle`, `preset_subdir`) is re-checked to be one
  `Component::Normal` with no `/`, `\`, `:` or NUL before the join (reader rule 8, `CONTRACT.md:141-142`), and every
  staged listing path is joined by one function, `dest_path(root, rel) -> Result<PathBuf, InstallError>`, which splits on
  `/`, requires each segment to be one normal component and the result to be a strict descendant of `root`
  (`E_INSTALL_PATH` otherwise). No other code joins a feed or archive string to a path.
- **FR-013** Only the staged `<vst3_bundle>` subtree and `presets/` subtree are placed; `README.txt` and
  `CHANGELOG.txt` are not (D3).
- **FR-014** Collisions found by the planner (D4) refuse the whole action with `E_INSTALL_COLLISION { path }` (relative
  path and root kind) and zero writes: the target exists and is not a real directory (a symlink, a file or any other
  kind, read with `symlink_metadata`); `<bundle>.krate-new` or `<bundle>.krate-old` exists with no journal of this
  plugin naming it; a preset directory destination exists as a non-directory; a `<dest>.krate-new` sibling exists.
  An existing non-directory (a symlink included) on the chain `preset_root` → `preset_dir` is `E_INSTALL_COLLISION`
  (plan SI-21).
- **FR-015** Placed modes: files `0755` iff the listing line says `0755`, else `0644`; directories `0755`; set with
  `set_permissions(PermissionsExt::from_mode)` on Unix so the umask cannot change them; symlinks created with
  `std::os::unix::fs::symlink(target, link)` with the listing's target. No owner, group, timestamp or extended attribute
  is set; nothing writes a `com.apple.quarantine` attribute (Core Philosophy, line 62-64). On non-Unix `FsInstallIo`
  sets no mode and reports `supports_symlinks() == false`; an action whose staged tree holds a symlink is then refused
  with `E_INSTALL_UNSUPPORTED` before any write (production Linux always supports both).
  A link target never leaves its top-level entry lexically: for each `l` line `<T>/<s1>/…/<sk>/<name> -> <target>`
  with `u` leading `..` segments, the planner requires `u ≤ k` (the resolution never reaches the archive root), else
  `E_INSTALL_PATH { reason: "link target leaves its top-level entry" }`, zero writes. A placed preset link is created as
  `symlink(target, <dest>.krate-new)` in step 3 and renamed in step 7, like every other preset sibling (plan SI-25).
- **FR-016** `plugin_root`, `presets().user` and `preset_dir` must be valid UTF-8 (they are stored in the manifest and
  journal, D8); otherwise `E_INSTALL_PATH_NOT_UTF8` before any write.

### C. Plan and copy (roadmap lines 672-678; D4, D5, D7)

- **FR-020** The planner reads the staged tree only through `StagedTree::listing()` and `file_hashes()` and the
  destinations only through `InstallIo` read methods (`kind_of`, `hash_file`, `read_dir`), and produces an
  `InstallPlan`: the bundle copy (every listing line under `<vst3_bundle>/`), every preset line with its D5 decision
  (`place`, `replace`, `adopt`, `kept`), every directory to create, every stale file with its decision (`remove`,
  `kept`), and the pending manifest. The `StagedTree` is a read-only source (Phase 8 FR-054): Phase 9 copies out of it
  and never renames or moves a part of it.
- **FR-021** Install vs update differ only in the state FR-093 requires; the procedure is the same. The previous
  manifest, when one exists, is read through FR-041's reader and drives D5's replace and stale rules; without one
  (a manual README-style install, or a record whose files were deleted) nothing is stale and every present destination
  file is adopted or kept by content. An existing managed bundle with no manifest is replaced by the swap like any old
  bundle (its version is older, or FR-093 would have refused).
- **FR-022** The bundle is copied into `<bundle>.krate-new` and each `place` / `replace` preset into `<dest>.krate-new`,
  each file through `InstallIo::copy_new` (FR-028: opened `write + create_new`), its bytes read from the staged file
  and hashed with `verify::Sha256Stream` in the `sink` while they are copied; the hash must equal the file's `file_hashes()` entry (Phase 8 FR-056), else `E_INSTALL_CHANGED
  { path }` and the action rolls back (FR-027). No staged file is read twice.
- **FR-023** Durability before commit (Phase 8 FR-055: "Phase 9 syncs what it places"): every file written into
  `.krate-new` or a preset sibling is `sync_all`ed after its last write, and every directory created under
  `.krate-new` and every parent directory of a sibling is synced (Unix: `File::open(dir)?.sync_all()`, as
  `settings.rs:270-272`; not on Windows, as Phase 4's `SyncDir` step) before the journal records `staged`. After the
  renames the parent directories of the target and of every renamed sibling are synced (Unix).
- **FR-024** Order of an install / update (every step through `InstallIo`; step names are those SC-002 cuts at):
  1. `journal_begun`: the journal (FR-050) with step `begun`, written atomically (FR-044). The `begun` journal holds
     every planned sibling, every planned directory to create and both manifests (pending and previous), so step 4
     rewrites only `step` (plan SI-11).
  2. `bundle_staged`: `<bundle>.krate-new` copied and synced.
  3. `presets_staged`: every preset sibling written and synced; every new preset directory created (directories are
     created in place, recorded in the journal, and removed on roll-back only when empty).
  4. `journal_staged`: the journal rewritten with step `staged` (the roadmap's "write the intent … and flush it").
  5. `old_aside`: when the target exists, `rename(target, <bundle>.krate-old)`.
  6. `new_in`: `rename(<bundle>.krate-new, target)` (the commit point, D7).
  7. `presets_in`: each sibling renamed over its destination. Before each sibling rename `dest` is re-read; a pre-state
     mismatch removes `<dest>.krate-new`, reports `dest` in `kept` and leaves it out of the manifest (plan SI-22).
  8. `manifest_written`: the pending manifest written atomically to `<state>/manifests/<stem>.json` (FR-044).
  9. `stale_removed`: every stale `remove` file deleted and every directory of the previous manifest that the new one
     lacks removed when empty.
  10. `journal_done`: the journal rewritten with step `done`.
  11. `old_deleted`: `<bundle>.krate-old` removed with `remove_tree` (never following a link, External facts).
  12. `journal_removed`: the journal file removed.
- **FR-025** Downgrade and equal versions are refused (Q8: no reinstall-previous). The code for every (action, state)
  pair before any download is FR-093's table, and nothing else assigns one. After `stage`, the driver re-checks the
  structural state it can see (an `Action` may come from a caller other than `for_command`, e.g. Phase 12): an
  `Action::Install` whose target exists is `E_INSTALL_STATE_CHANGED { shown: "install", current: "installed" }`; an
  `Action::Update` whose target is absent is `E_INSTALL_STATE_CHANGED { shown: "update", current: "not_installed" }`;
  an `Action::Update` whose previous manifest (FR-041) records a `version` that `version::compare` (`version.rs:83-94`)
  does not rank older than the staged version is `E_INSTALL_NOT_NEWER { installed, offered }`. With no previous
  manifest, the installed bundle's version (Phase 6's moduleinfo parser with the class `Version` fallback) must be
  ranked older, else `E_INSTALL_NOT_NEWER`; an unreadable version is `E_INSTALL_STATE_CHANGED { shown: "update",
  current: "unknown" }` (plan SI-23). Each re-check refuses with zero mutating `InstallIo` calls.
- **FR-026** After a successful action the driver calls `extract::discard(tree, …)` (Phase 8 FR-051); the command flow
  calls `download::discard(artifact, …)` (Phase 7 FR-049, FR-090). Ownership: `obtain_and`'s `next` receives the
  `VerifiedArtifact` by value (`download.rs:1319-1325`) and `Action::{Install, Update}` takes it by value, so `next`
  clones it first (`#[derive(Clone, Debug)]`, `verify.rs:113`) and moves the original into the `Action`. `next`
  returns the action's result and the clone; after `obtain_and` returned, an `Ok` outcome is followed by
  `download::discard(clone, env.download.io)`; a discard error is logged once (`warn!`) and the action stays `Ok`
  (plan SI-3).
  After a failed action the clone is dropped without a discard: the verified artifact stays in staging so a retry makes
  no asset request (Phase 7 FR-046); the staged tree is always discarded.
- **FR-027** In-process roll-back and roll-forward.
  - Any error **before the commit point**, including a failure of the `new_in` rename itself: when `old_aside` (step 5)
    has run, the roll-back **first** renames `<bundle>.krate-old` back to the target; only when that rename succeeds,
    or when `old_aside` never ran, does it remove, through `InstallIo`, the siblings, `.krate-new`, the directories
    this action created (empty only) and any FR-044 temp file it left, and then the journal. The target and the previous manifest end as they were. If
    the rename back (or any later roll-back step) fails, the roll-back stops there, logs the code, and leaves the
    journal at its step: the next start's recovery pass sees `new` present, `target` absent, `old` present and rolls it
    back (FR-071).
  - Any error **from `new_in` on** (steps 7 to 9): the action runs the recovery pass's roll-forward for its own journal
    once, at once (FR-071, the same function). When that completes, the action returns `Ok` (the new version is
    committed and recorded) and logs one `warn!` with the first failure's code. When a roll-forward step fails too, the
    journal stays at step `staged` with its `pending_manifest`, the action returns the first failure's code (FR-029),
    and the next start's `recover` completes it.
  - An error at step 11 (`old_deleted`) or 12 (`journal_removed`) comes after `journal_done`: the new version is
    committed and recorded, so the action returns `Ok`, logs one `warn!` with the code, and leaves the journal at step
    `done` for the next start (FR-071). Until then that plugin's actions are `E_INSTALL_JOURNAL_PENDING` (FR-052).
  - The returned error is always the code of the step that first failed, never a roll-back step's code.
- **FR-028** `InstallIo` is the only way `install.rs` touches the filesystem (D12). Every method takes `&self`, as
  `ExtractIo` (`extract.rs:241-258`); `NodeKind` is Phase 8's (`extract.rs:226-231`). No method maps to an `E_*` code;
  the caller does (FR-029). The surface is exactly:
  - Read: `kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>>` (`symlink_metadata`, `NotFound` → `None`,
    never follows); `read_link(&self, path: &Path) -> io::Result<PathBuf>`; `hash_file(&self, path: &Path) ->
    io::Result<[u8; 32]>` (a regular file only, never through a final link, via `verify::Sha256Stream`); `read_dir(&self,
    dir: &Path) -> io::Result<Vec<(OsString, NodeKind)>>` (sorted by name, never follows); `read_file(&self, path: &Path,
    max: u64) -> io::Result<Vec<u8>>` (reads at most `max + 1` bytes); `canonicalize(&self, path: &Path) ->
    io::Result<PathBuf>`; `supports_symlinks(&self) -> bool`.
  - Mutating: `create_dir_all(&self, path: &Path)` (only for `plugin_root` and `presets().user`, FR-010, FR-011);
    `create_dir(&self, path: &Path)` (one level, fails when present); `copy_new(&self, src: &Path, dest: &Path, sink:
    &mut dyn FnMut(&[u8])) -> io::Result<u64>` (opens `src` read-only and `dest` `write + create_new`, copies in chunks,
    hands each chunk to `sink` before writing it, returns the byte count); `write_new(&self, path: &Path, bytes: &[u8])`
    (`write + create_new`, for the FR-044 temp files); `symlink(&self, target: &str, link: &Path)`; `set_mode(&self,
    path: &Path, mode: u32)` (no-op off Unix); `sync_file(&self, path: &Path)` (`sync_file` opens the file for writing (no create, no truncate) and calls
    `sync_all`, plan SI-7); `sync_dir(&self, dir:
    &Path)` (Unix `File::open(dir)?.sync_all()`; the caller never calls it off Unix, FR-023); `rename(&self, from: &Path,
    to: &Path)`; `remove_file(&self, path: &Path)` (a file, or a link as the link itself); `remove_dir(&self, path:
    &Path)` (empty only); `remove_tree(&self, path: &Path)` (`remove_dir_all`, never follows a link). Each returns
    `io::Result<()>` unless stated.
  `FsInstallIo` implements it over `std::fs`; the test doubles of D12 (`MemInstallIo`, `FaultyInstallIo`,
  `RecordingInstallIo`) implement the same trait and record each call as `(method, path)`.
- **FR-029** `InstallError` codes. Every error has a stable `code()`; its `Display` starts with the code. The codes are
  exactly: `E_INSTALL_ACTION`, `E_INSTALL_NO_DRIVER`, `E_INSTALL_BUSY`, `E_INSTALL_NO_FEED { cause }`,
  `E_INSTALL_UNKNOWN_PLUGIN`, `E_INSTALL_NOT_MANAGEABLE`, `E_INSTALL_STATE_CHANGED { shown, current }`,
  `E_INSTALL_NOT_NEWER { installed, offered }`, `E_INSTALL_NOT_MANAGED`, `E_INSTALL_JOURNAL_PENDING`,
  `E_INSTALL_JOURNAL`, `E_INSTALL_MANIFEST { reason }`, `E_INSTALL_ROOT_CHANGED { which }`, `E_INSTALL_OUTSIDE_ROOT {
  path }`, `E_INSTALL_PATH`, `E_INSTALL_PATH_NOT_UTF8`, `E_INSTALL_COLLISION { path }`, `E_INSTALL_UNSUPPORTED`,
  `E_INSTALL_FORMAT`, `E_INSTALL_CHANGED { path }`, `E_INSTALL_FULL { step }`, `E_INSTALL_DENIED { step }`,
  `E_INSTALL_IO { step, kind }`; plus the wrapped Phase 6 `E_RECORD_*` codes (FR-041), Phase 7 `E_DOWNLOAD_*` codes
  (FR-090) and Phase 8 `E_EXTRACT_*` codes (FR-002), each returned unchanged with its own code first. An `io::Error`
  from a mutating `InstallIo` call maps by `ErrorKind`, as Phases 7 and 8 map them: `StorageFull` or `QuotaExceeded` →
  `E_INSTALL_FULL { step }`; `PermissionDenied` or `ReadOnlyFilesystem` → `E_INSTALL_DENIED { step }`; every other kind
  → `E_INSTALL_IO { step, kind }` (plan SI-8). `step` is the FR-024 step name the
  call belongs to (`journal_begun` … `journal_removed`), or for Remove `remove_begun`, `remove_entries`,
  `manifest_removed`, `journal_removed` (FR-061). `path` fields carry the root kind and the relative path only (FR-091).

### D. The manifest and atomic writes (roadmap lines 685-686; D8)

- **FR-040** The manifest is the JSON object `{ "manifest_version": 1, "plugin": <entry name>, "version":
  <entry.version_text>, "vst3_bundle", "archive_sha256": <lower-case hex>, "plugin_root", "preset_root", "files": [ … ]
  }` at `<state>/manifests/<stem>.json` (`stem` = `vst3_bundle` minus `.vst3`, `detect.rs:1009`). `files` holds one
  object per placed or adopted entry: `root` (`"plugin"` → relative to `plugin_root`, `"presets"` → relative to
  `preset_root` = `presets().user`), `type` (`"f"`, `"d"`, `"l"`), `path` (relative POSIX, the same character rules as
  C4), `sha256` (files only, lower-case hex of the placed bytes), `target` (links only). The bundle's entries are its
  full listing; kept presets (D5) are absent. The three Phase 6 keys keep their name, type and meaning (Phase 6 FR-028).
  Directories are recorded only when Phase 9 owns them: a `d` entry for every directory this action creates, plus every
  directory the previous manifest recorded that the new layout still has; a pre-existing directory the manager did not
  create is never recorded (so never removed) (plan SI-17).
- **FR-041** `install::read_manifest_full(state_dir, stem, plugin)` reads the same file bounded to
  `detect::MAX_MANIFEST_BYTES`, applies `detect::read_manifest`'s three-key rules with the same codes
  (`E_RECORD_READ`, `E_RECORD_PARSE`, `E_RECORD_FUTURE`, `E_RECORD_MISMATCH`), then validates every Phase 9 key:
  each `path` passes FR-012's segment rules, each root is absolute, UTF-8 and normal, each `sha256` is 64 lower-case hex;
  any failure is `E_INSTALL_MANIFEST { reason }`. Update and Remove refuse a manifest that fails (nothing written).
  The three-key rules are one implementation: `detect::parse_manifest(bytes, plugin) -> RecordRead<InstallerRecord>`
  is extracted (pure) from `read_manifest`, which calls it; `read_manifest_full` takes `io` as its first argument,
  reads the file through `InstallIo::read_file` and applies it; `read_manifest`'s behaviour is unchanged (plan PD-14).
- **FR-042** A manifest written by Phase 9 reads back through Phase 6's `detect::read_manifest` as
  `RecordRead::Found(InstallerRecord::Manifest { version_text })` equal to the entry's `version_text` (SC-006).
- **FR-043** No manifest or journal file is written or removed outside `<state>/manifests/` and `<state>/journal/`.
- **FR-044** Atomic write for the manifest and the journal, in the order of `settings::save_with` (`settings.rs:281`)
  but through `InstallIo` (FR-028) so the crash cut and the fault injection reach every step: `create_dir` of
  `<state>/manifests/` or `<state>/journal/` when absent; `write_new` of the temp file `.<stem>.json.tmp` in the same
  directory (a leftover of that name is first removed with `remove_file`: with the state lock and the per-plugin
  registry, FR-085, it can only be a crash leftover); `sync_file`; `rename` over `<stem>.json`; then (Unix) `sync_dir`
  of the directory. No `tempfile` / `NamedTempFile` API is used in `install.rs` (FR-112). A crash before the rename
  leaves the previous file intact; a temp leftover is removed by the recovery pass (FR-070). State subdirectories are
  real directories or absent, everywhere: the atomic write, `read_manifest_full`, `action_gate`'s journal probe and
  planner step 0 require `kind_of(<state>/journal)` and `kind_of(<state>/manifests)` to be `None` or `Some(Dir)`; any
  other kind is `E_INSTALL_JOURNAL` / `E_INSTALL_MANIFEST { reason: "state directory is not a directory" }` before any
  write; `recover` reads `journal/` only when it is a `Dir` (plan SI-24).

### E. The journal (roadmap lines 677-684; D6, D7)

- **FR-050** One journal file per plugin, `<state>/journal/<stem>.json`: `{ "journal_version": 1, "op": "install" |
  "update" | "remove", "plugin", "stem", "vst3_bundle", "preset_subdir": string | null, "step", "target", "new", "old",
  "had_old": bool, "siblings": [{dest, new, pre}], "created_dirs": [ … ], "pending_manifest": { … } | null,
  "previous_manifest": { … } | null }`, every path absolute UTF-8 (`vst3_bundle` and `preset_subdir` plan SI-20). Each
  sibling records `pre`: `"absent"` (Place) or `{"sha256": …}` (Replace, the previous manifest's hash) (plan SI-22).
  The `begun` journal holds every planned sibling, directory and both manifests (plan SI-11). It is written only
  through FR-044.
- **FR-051** A journal names only paths under the action's `plugin_root` and `preset_dir`, and only the names
  `<vst3_bundle>.krate-new`, `<vst3_bundle>.krate-old` and `<file>.krate-new` next to their targets.
- **FR-052** At most one journal per plugin exists; an action on a plugin whose journal exists (a recovery that could
  not finish, FR-074) is refused with `E_INSTALL_JOURNAL_PENDING` before any download.

### F. Uninstall (roadmap lines 687-690, Q8; D9)

- **FR-060** `Action::Uninstall` requires a manifest for the plugin (FR-041); with none it is `E_INSTALL_NOT_MANAGED`
  (a manual install is listed but never removed by the manager; Q4: it becomes managed only through an Update, FR-021, which writes the first manifest, after which Remove deletes the adopted presets too).
- **FR-061** Remove writes the journal (op `remove`, step `begun`), then deletes the bundle (FR-122: the whole
  `<vst3_bundle>` tree, `remove_tree`, never following a link; the manifest's `plugin`-root entries are not walked
  individually), then, for every manifest entry with root `presets`, deepest path first:
  a `f` entry is removed when it is a regular file whose current SHA-256 equals the recorded one; an `l` entry when it
  is a symlink whose target equals the recorded one (removed as the link, never followed); a `d` entry when it is an
  empty real directory. Anything else at a preset entry's path (changed content, another kind) is left
  and reported in the outcome's `kept` list; a non-empty real directory at a `d` entry (it still holds files the
  user saved there) is left without being reported (ruled 2026-11-01, Clarifications "build ruling"); the bundle never appears in `kept`. A missing path counts as already removed. Then the manifest is removed,
  then the journal.
- **FR-062** Nothing outside the bundle and not listed in the manifest is ever removed: user presets beside or below
  placed ones, `presets().user`, `plugin_root`. (A user file inside the bundle goes with the bundle, FR-122.) A directory holding any unlisted entry stays.
- **FR-063** Before any manifest-driven removal (Remove, and an Update's stale-file and preset rules over its previous
  manifest) every entry is resolved: joined by FR-012's `dest_path` to its recorded root, and the canonical form of its
  parent directory (`InstallIo::canonicalize`) must lie inside the canonical configured root; one entry outside refuses
  the whole action with `E_INSTALL_OUTSIDE_ROOT { path }` and nothing written. The check runs in `action_gate`
  (FR-093) and again in the planner before the first write. It canonicalizes the deepest existing ancestor of the
  entry's parent (its remaining segments are `Component::Normal` by FR-012, so they cannot climb) and requires it inside
  the canonical root; an entry whose parent does not exist is "already removed" for Remove (plan SI-16). The canonical
  check also runs on every planned placement (bundle, preset files, links, directories, siblings) before the first
  write (plan SI-21). This canonical check is the guard against a link escape;
  `check_sandboxed` (FR-110) is lexical (`roots.rs:172-185`) and only stops a test from naming a path outside its
  TempDir.
- **FR-064** The configured roots are `linux_plugin_root_or_default` and `presets().user` of the current context. A
  manifest whose `plugin_root` or `preset_root` is not equal to them (compared canonically) is
  `E_INSTALL_ROOT_CHANGED { which }` (`which` = `"plugin"` or `"presets"`): Remove refuses; Update refuses too (it
  would leave the old install untracked). A recorded root compares equal with the configured one when the two strings
  are equal, else when both canonicalize to the same path; a canonicalize failure with unequal strings is
  `E_INSTALL_ROOT_CHANGED` (plan SI-16). Ruled (Q3): the refusal stays and no move, re-root or Remove-against-recorded-roots code exists anywhere; Phase 12's
  settings screen refuses to change the Linux root while any manifest exists (FR-123). Phase 9 has no settings screen,
  so the root changes only when the user edits the settings file by hand. The refusal's message names the recorded root kind and tells the user to set the
  Linux plug-in root back to the one the plugin was installed under.
- **FR-065** Remove is idempotent: running it again after an interruption removes what is left and ends in the same
  state (SC-012).
- **FR-122** The bundle is one unit on both paths (Q2). Remove removes the whole `<vst3_bundle>` directory through
  `InstallIo::remove_tree` (a link inside it is removed as the link, never followed); a file the user added or edited
  inside the bundle is deleted with it and is not reported as `kept`; afterwards no partial `<P>.vst3` remains, so
  detection never reads a half-removed bundle as `UpdateState::Unknown`. `kept` contains preset paths only.
- **FR-123** Roadmap amendments (`specs/Krate-manager-roadmap.md`), made by the build stage and checked by SC-030:
  (a) Phase 9 recovery text (line 682) states the commit point is the `new` to target rename and that a cut between
  `journal_staged` and `new_in` rolls back (Q1, D7); (b) Phase 12 records that the settings screen refuses to change the
  Linux plug-in root while any manifest exists, the user removing first (Q3); (c) Phase 12 records an orphan-manifest
  Remove (a plugin with a manifest but no feed entry; Q8). No other roadmap line changes.
- **FR-124** Install is offered for status `elsewhere` exactly as for `NotInstalled` (FR-093, unchanged); the manager
  never touches the other copy (no write under `/usr`, FR-066). The Install confirmation (FR-099) shows the other
  copy's path from `PluginView.copies` and the sentence telling the user to remove it by hand; a card with no
  `elsewhere` copy shows neither.
- **FR-066** There is no "Reinstall previous version" and no removal of copies outside the managed location (roadmap
  lines 690, 980).

### G. Recovery at every start (roadmap lines 681-684; D7, D14)

- **FR-070** `install::recover(ctx: &AppContext, io: &dyn InstallIo, extract: ExtractEnv<'_>) -> RecoveryReport` lists
  `<state>/journal/`, and for each journal file applies FR-071 to FR-073, then removes manifest and journal temp
  leftovers (FR-044), then calls `extract::prune(extract)` (Phase 8 FR-058). It returns, per journal, the plugin, the
  decision (`rolled_back`, `completed`, `refused`) and any error code.
- **FR-071** Install / update journal, by step, by the journal's `had_old` (the target existed when step 1 wrote the
  journal) and by what is on disk (read with `kind_of`, never following a link). The commit point is `new_in` (D7):
  every state before it rolls back, every state from it on rolls forward.
  - step `begun`: roll back: remove `new` and every listed sibling (by kind), remove `created_dirs` when empty, leave
    `target`, `old` and the manifest as found, remove the journal.
  - step `staged`, `new` present, `target` present: `old_aside` did not run: roll back as for `begun`.
  - step `staged`, `new` present, `target` absent, `had_old` true, `old` present: `old_aside` ran and `new_in` did not:
    roll back: rename `old` → `target`, then as for `begun`.
  - step `staged`, `new` present, `target` absent, `had_old` false: a first install before `new_in`: roll back as for
    `begun` (the plugin ends absent, no manifest).
  - step `staged`, `new` absent, `target` present, and (`had_old` false, or `old` present): `new_in` ran: complete FR-024
    steps 7 to 12 with the journal's `pending_manifest` (a sibling already renamed is skipped when its destination's
    hash equals the pending one). Before each sibling rename `dest` is re-read; a pre-state mismatch removes
    `<dest>.krate-new`, reports `dest` in `kept` and leaves it out of the manifest (plan SI-22).
  - step `staged`, `new` absent, `target` absent, `old` present: no crash cut leaves this (only outside interference
    does), and the old bundle is the only complete one: roll back: rename `old` → `target`, then as for `begun` (the
    roadmap's "new missing … → rename old back", line 682).
  - step `done`: remove `old` if present, remove the journal.
  - any other combination (e.g. `had_old` true and `new`, `target`, `old` all absent; or `had_old` true, `new` absent,
    `target` present, `old` absent): `refused` (FR-074), nothing touched.
  This narrows roadmap line 682's "new present, target missing → rename new in" to a roll-back (D7, recorded deviation).
- **FR-072** Remove journal: re-run FR-061 from the manifest still on disk; when the manifest is already gone, remove
  the journal.
- **FR-073** Before acting on a journal, every path in it must satisfy FR-051 against the current configured roots
  (canonical parents inside the canonical roots); otherwise the journal is `refused`. The destinations are recomputed
  by `Destinations::from_parts` from the journal's `plugin`, `vst3_bundle`, `preset_subdir` (the FR-012 / FR-016 /
  FR-110 checks of `Destinations::of`); `stem` equals the file stem and `vst3_bundle` minus `.vst3`; both manifests'
  `vst3_bundle` equal the journal's; any failure is `refused` (plan SI-20).
- **FR-074** A `refused` journal, or one whose steps fail, is left in place, logged once with its plugin and code
  (`E_INSTALL_JOURNAL`), and makes that plugin's actions return `E_INSTALL_JOURNAL_PENDING` (FR-052); other plugins are
  unaffected. Recovery never fails startup.
- **FR-075** After recovery, for every journal it completed or rolled back, the target holds exactly the old or exactly
  the new bundle (by the bundle's C4 listing, Phase 8 FR-020), the preset destinations hold exactly the old or the new
  factory set, the manifest matches that version (or is absent for a rolled-back first install), and no
  `.krate-new` / `.krate-old` name and no journal remains (SC-002).

### H. Concurrency and the command flow (roadmap line 691; D11)

- **FR-080** `context::assemble_with_os` calls `install::recover` with `FsInstallIo` and an `ExtractEnv` over
  `FsExtractIo` and `ExtractLimits::PRODUCTION`, once, after the lock and the settings fold and before returning the
  `AppState`, on every OS (journals exist only on Linux in production; the pass finds none elsewhere). Its report is
  logged; it does not change `AppState`.
- **FR-081** The start-up `extract::prune` of FR-070 is the call Phase 8 FR-058 reserves for Phase 9.
- **FR-085** In-process exclusion per plugin: a registry keyed by `(state_dir, stem)` (model `download.rs:653-671`) held
  by a drop guard for the whole action. The guard is taken in `for_command` / `remove_for_command` right after the
  entry is resolved from the feed (FR-090 step 2, FR-094 step 2), and so **before** the action gate, detection, the
  first `InstallProgress` event and the first mutating `InstallIo` call; it is released when the command returns, on
  success, on error and on unwind. A second action on the same plugin while one runs is `E_INSTALL_BUSY` at once (its
  one feed request already made; no asset request, no `InstallIo` write).
  Actions on different plugins may run in parallel; their destinations are disjoint (distinct `vst3_bundle`, distinct
  `<Name>` preset folders). One process per state dir (`state_lock.rs`) makes the registry sufficient.
- **FR-090** `install::for_command(env: CommandEnv<'_>, name, expected_version, action: &str, now_unix, host,
  progress) -> Result<ActionSummary, InstallError>`, in this order, each step ending the command on its error:
  1. parse `action`: `"install"` or `"update"`, anything else `E_INSTALL_ACTION` (no request, no write);
  2. `download::command_request_for(env.ctx, name, expected_version, now_unix, host, env.os, env.arch)` (plan SI-1;
     Phase 7 FR-065: exactly one
     `feed::check` in `FetchMode::Normal`, i.e. one feed request, falling back to the cached copy; `expected_version` →
     `E_DOWNLOAD_VERSION_CHANGED`; its other `E_DOWNLOAD_*` codes unchanged); then the entry's `release.install`
     (`E_INSTALL_NOT_MANAGEABLE` when absent, FR-003), and the per-plugin guard (FR-085, `E_INSTALL_BUSY`);
  3. `env.drivers.driver_for(env.os)` (plan SI-10): `None` → `E_INSTALL_NO_DRIVER`;
  4. `install::action_gate(env.os, env.ctx, env.io, entry)` and the detection of the plugin
     (`detect::detect_plugin(env.os, …)`), then `install::allowed_actions` (FR-093; the availability is `available`, as step 2 resolved
     it): the action is accepted or refused with FR-093's code;
  5. `download::obtain_and(env.download, &feed, &request, …, next)`, where `next` clones the artifact (FR-026), builds
     the `Action`, runs `driver.run(InstallEnv { … }, vec![action])`, takes its one `ActionResult`, and returns the
     action's result and the clone; after `obtain_and` returned, an `Ok` outcome is followed by
     `download::discard(clone, env.download.io)`; a discard error is logged once (`warn!`) and the action stays `Ok`
     (plan SI-3);
  6. map the result to the `ActionSummary` (FR-091): `notice: Some(RescanDaw)`.
  Steps 1 to 4 make no asset request and no mutating `InstallIo` call; the only request before step 5 is step 2's feed
  request. The download leg resolves the asset for `(env.os, env.arch)`; production passes the native pair
  (`install::native_env`, PD-6), so production behaviour is Phase 7's (plan SI-1).
- **FR-091** `ActionSummary { name, action, version, kept: Vec<KeptPath>, notice: Option<Notice> }` carries no absolute
  path: `KeptPath { root: "plugin" | "presets", path }` with the relative path only. `notice` is set by the command flow,
  not the driver: `Some(RescanDaw)` after every successful install or update (FR-095) and `None` after a Remove.
- **FR-092** `InstallProgress` (serialized for the `Channel`): `Download { event: Progress }` (plan SI-9: a struct
  variant, the enum `#[serde(tag = "kind", rename_all = "snake_case")]`; the Phase 7 events unchanged),
  `Staging`, `Placing { done, total }` (files copied, monotonically increasing, ending at `total`), `Committing`,
  `Removing { done, total }`, `Done`. `Staging` is sent before `extract::stage` and so before the first mutating
  `InstallIo` call of an install or update; `Removing { done: 0, total }` is sent before the first removal of a Remove.
  `Removing { done, total }`: `total` = 1 (the bundle) + the manifest's `presets` entries; `done` advances after the
  bundle and after each preset entry (plan SI-12).
  Each event is sent in order through `install::channel_progress`, the same adapter for `InstallProgress` (plan SI-5);
  a send error is dropped and the action goes on.
- **FR-093** The action gate and the action table: one function decides, for the status screen (FR-100) and for the
  commands (FR-090, FR-094).
  - `install::action_gate(os, ctx, io, entry) -> ActionGate`, read-only: `Blocked(code)` when a journal for the stem
    exists (`E_INSTALL_JOURNAL_PENDING`, FR-052), or a manifest exists and fails `read_manifest_full` (its FR-041 code),
    or names other roots (`E_INSTALL_ROOT_CHANGED`, FR-064), or holds an entry that resolves outside its root
    (`E_INSTALL_OUTSIDE_ROOT`, FR-063); otherwise `Open { manifest: Option<Manifest> }`. Checked in that order; the
    first failing check gives the code. State subdirectories are real directories or absent: the journal probe and
    `read_manifest_full` require `kind_of(<state>/journal)` and `kind_of(<state>/manifests)` to be `None` or
    `Some(Dir)`; any other kind is `E_INSTALL_JOURNAL` / `E_INSTALL_MANIFEST { reason: "state directory is not a
    directory" }` (plan SI-24).
  - `install::allowed_actions(driver_present, gate, availability, update_state) -> Vec<ActionKind>`: empty when
    `driver_present` is false or the gate is `Blocked`; else `install` when the availability is `available` and the
    update state (Phase 6, `status.rs:330-370`) is `NotInstalled`; `update` when it is `available` and
    `UpdateAvailable`; `remove` when the gate's `manifest` is `Some`.
  - A refused install or update returns, by the (action, update state) pair: `install` × `UpdateAvailable` or `Unknown`
    → `E_INSTALL_STATE_CHANGED { shown: "install", current }`; `install` × `UpToDate` or `InstalledNewer` →
    `E_INSTALL_NOT_NEWER`; `update` × `NotInstalled` or `Unknown` → `E_INSTALL_STATE_CHANGED { shown: "update",
    current }`; `update` × `UpToDate` or `InstalledNewer` → `E_INSTALL_NOT_NEWER`. A `Blocked` gate returns its code
    for every action, before the table. `NotInstalled` × `install` and `UpdateAvailable` × `update` are accepted.
  - A manifest is optional for install and update: when the gate holds one it is the previous manifest (D5 replace and
    stale rules); when absent there is none (a manual README-style install, or a first install).
  - A plugin with a pending journal (FR-052), or with a manifest that fails FR-041, FR-063 or FR-064, refuses every
    action, with that check's code.
- **FR-094** `install::remove_for_command(env: CommandEnv<'_>, name, now_unix, progress) -> Result<ActionSummary,
  InstallError>`, in this order:
  1. `feed::check(env.ctx, FetchMode::Normal, now_unix)`: exactly one feed request, the cached copy accepted when it
     fails (Phase 5); no snapshot → `E_INSTALL_NO_FEED { cause }`; `name` not in the snapshot →
     `E_INSTALL_UNKNOWN_PLUGIN`;
  2. the entry's `release.install` (`E_INSTALL_NOT_MANAGEABLE` when absent: the stem comes from `vst3_bundle`), then the
     per-plugin guard (FR-085, `E_INSTALL_BUSY`);
  3. `env.drivers.driver_for(env.os)` (plan SI-10): `None` → `E_INSTALL_NO_DRIVER`;
  4. `action_gate` (FR-093): `Blocked(code)` → that code; `Open { manifest: None }` → `E_INSTALL_NOT_MANAGED` (FR-060);
  5. `driver.run(InstallEnv { … }, vec![Action::Uninstall { entry }])`, exactly once, and its one `ActionResult`
     mapped to the `ActionSummary` (`kept` from the outcome, `notice: None`).
  Remove makes no asset request: steps 1 to 4 make no mutating `InstallIo` call, and the Linux driver's uninstall
  reads only the manifest and the disk. Phase 10's Windows and macOS drivers receive the same `Action::Uninstall`
  through the same call (roadmap lines 753-757).

### I. User-facing behaviour (roadmap line 691, Q7, Q8; D13)

- **FR-095** After every successful install or update the card shows the "restart or rescan your DAW" notice (Q7,
  Cross-Cutting line 1003-1004): the running DAW keeps the old library mapped until it reloads (External facts,
  `unlink(2)`), and a rescan or restart picks up the new bundle. The manager detects and closes no process.
- **FR-096** `src-tauri`: two commands, `install_plugin(state, name: String, expected_version: String, action: String,
  on_progress: Channel<InstallProgress>) -> Result<ActionSummary, String>` and `remove_plugin(state, name: String,
  on_progress: Channel<InstallProgress>) -> Result<ActionSummary, String>`, each a thin wrapper over
  `install::for_command` / `install::remove_for_command` on `spawn_blocking`, shaped like `download_plugin`
  (`commands.rs:58-100`), building `CommandEnv { os: NativePlatform.os(), drivers: &NativeDrivers, io: &FsInstallIo,
  … }` (plan SI-10). `action` is passed through as the string; the core parses it (FR-090 step 1, `E_INSTALL_ACTION`). `build.rs`, the
  capability (`allow-install-plugin`, `allow-remove-plugin`), the autogenerated permission files and `generate_handler!`
  list exactly eight commands; no other capability, permission or plugin is added; the webview gains no `fs`, `shell`
  or `http` capability.
- **FR-097** UI: each card renders one button per `PluginView.actions` entry, labelled `Install`, `Update` or `Remove`;
  no button is rendered for an action not listed. A click opens the card's confirmation (FR-099); confirming disables
  every action button of that card, creates a `Channel<InstallProgress>`, invokes the command with exactly `{ name,
  expectedVersion: latest, action, onProgress }` (Remove: `{ name, onProgress }`), and shows progress text per event
  (`Downloading n of m bytes`, `Placing n of m files`, `Removing n of m files`, `Finishing`). Cancelling invokes
  nothing.
- **FR-098** UI on completion: success shows the outcome line (`Installed <version>`, `Updated to <version>`,
  `Removed`), the DAW notice when `notice` is set, and one line per `kept` path (`Kept <path>: changed since install`);
  failure shows the error text, whose code comes first. Then the status is refreshed through `plugin_status`, and the
  outcome lines stay visible on the refreshed card for that plugin until the next action or `Check now`.
- **FR-099** Every action asks once for confirmation in the page before the command is invoked (D15, Cross-Cutting
  line 1000): a confirm control in the card, no native dialog, listing its one item. Install: `Install <Name>
  <latest>?`; Update: `Update <Name> <installed> to <latest>?`; Remove: names the plugin and states that the plug-in and
  its factory presets are removed and user presets are kept (Q8). For Install when the plugin's status is `elsewhere` (its `PluginView.copies` hold a copy outside the managed root,
  Q5), the confirmation also names that other copy's path and tells the user to remove it by hand (FR-124). The
  confirmation text lists exactly one plugin and
  version; Phase 12's multi-item list (lines 882-883) is not built here.
- **FR-100** `status::PluginView` gains `actions: Vec<ActionView>` (`"install" | "update" | "remove"`), the output of
  `install::allowed_actions` (FR-093) for the plugin, so the list is by construction exactly what the commands accept
  for the same disk state (D13). How the inputs reach `status`:
  - `status::build_with_os(os, arch, ctx, mode, now_unix, host, io: &dyn InstallIo) -> StatusView` holds `build`'s body
    with `os`, `arch` and `io` as inputs; `build(ctx, mode, now_unix, host)` keeps its signature and calls it with
    `NativePlatform.os()`, `Arch::native()` and `&FsInstallIo` (the pattern of `assemble_with_os`, `context.rs:84`).
  - `build_with_os` computes, per feed entry with an `install` block, `install::action_gate(os, ctx, io, entry)` (the
    pending journal, `read_manifest_full`, the FR-064 roots and the FR-063 resolution; read-only), and passes the gates
    with `driver_present = driver_for(os).is_some()` to `status::assemble_with_gates(os, arch, check, running,
    detections, gates: &BTreeMap<String, ActionGate>, driver_present)`, which is `assemble`'s body plus the `actions`
    field.
  - `assemble(os, arch, check, running, detections)` keeps its signature (its callers `status.rs:197`,
    `detect_tests.rs:263`, `status_tests.rs`) and calls `assemble_with_gates` with no gates and `driver_present`
    false: a plugin with no gate has `actions: []` (fail-closed).
  `view.ts` gains the field with its parser check.

### J. Safety, guards, fixtures and lanes (Cross-Cutting lines 984-1025)

- **FR-110** Every destination path, journal path and manifest root is passed through `ctx.roots.check_sandboxed`
  (`roots.rs:175-185`) before its first use, so on a test `Roots` any path outside the test's TempDir panics before it is
  touched (production `Roots` have no sandbox: no effect).
- **FR-111** No new dependency: `crates/core/Cargo.toml` `[dependencies]` and `[dev-dependencies]`, both lock files and
  `injection.rs` `FR007_DEPENDENCIES` (14 names) are unchanged.
- **FR-112** `injection.rs` gains `INSTALL_RS` / `INSTALL_TESTS_RS` and an `install_findings` rule over `install.rs`:
  - **Confined** to `impl InstallIo for FsInstallIo`: the mutating filesystem tokens of `EXTRACT_FS_TOKENS`
    (`injection.rs:325-344`, `set_len` among them), plus `sync_all` and `sync_data`; except declarations and method
    calls of `InstallIo`'s own method names (FR-028) that coincide with a token (`create_dir`, `symlink`, `rename`,
    `remove_file`, `remove_dir`), as `EXTRACT_IO_METHODS` does for `extract.rs` (`injection.rs:346`, used at `:3060`).
  - **Nowhere** in `install.rs`: `Command`, `TcpStream`, `UdpSocket`, `xattr`, `thread :: spawn`, `hard_link`,
    `persist`, and every `TEMPFILE_REAL_TEMP` token (`injection.rs:361-368`: `tempfile :: tempdir`, `. tempdir (`,
    `TempDir :: new`, `tempfile (`, `NamedTempFile`), so nothing in it can create under the real `env::temp_dir()`
    (FR-044 writes its temp files through `InstallIo::write_new`). Each token sits in exactly one of the two lists.
  - `flate2` and `sha2` stay confined in non-test code (`extract.rs`, `verify.rs`); `StagedTree {` stays only in
    `extract.rs` (`injection.rs:3005-3013`).
  - **Reads confined too** (plan SI-18): outside `impl InstallIo for FsInstallIo`, `install.rs` holds none of
    `fs :: read`, `fs :: read_dir`, `fs :: metadata`, `fs :: symlink_metadata`, `fs :: canonicalize`, `fs :: read_link`,
    `File :: open`, `. exists (`, `. try_exists (`, `. is_dir (`, `. is_file (`, `. is_symlink (`, `. metadata (`,
    `. symlink_metadata (`.
  - **Test-source lists** (plan SI-14): `FR088_FILES` and `PHASE7_TEST_FILES` (`injection.rs:186-191`) gain
    `install_tests.rs` and `tar_builder_tests.rs`; `roster.rs` `identifier_files()` (`roster.rs:247-271`) gains
    `crates/core/src/install.rs`.
  - **The fixture seam** (plan SI-2): `fixture_seam_findings` allows `with_fixture_url` in `install_tests.rs` too and
    gains `request_seam` (declared only in `install.rs`, directly after `#[cfg(test)]`, never named in `src-tauri/`),
    with two plants: `struct P { request_seam: Option<u8> }` in `install.rs` (no `cfg(test)`) and `fn f() {
    request_seam(); }` in `src-tauri/src/commands.rs`, each reported.
  - **`test_support` builds `CommandEnv` seam-free** (plan SI-27): `InstallRig::command_env(os, arch, io, drivers)`
    initialises `#[cfg(test)] request_seam: None` in its struct literal; a test that needs the fixture server sets
    `env.request_seam = Some(&seam)` in `install_tests.rs`. `fixture_seam_findings` allows `request_seam` in
    `install.rs` (declaration directly after `#[cfg(test)]`, the `native_env` initialiser and the `for_command`
    application, each under `#[cfg(test)]`), in `test_support.rs` only as a `#[cfg(test)]`-attributed field
    initialiser, and in `install_tests.rs`; nowhere else. Its plant: `fn f(e: &mut CommandEnv<'_>) { e.request_seam =
    None; }` in `test_support.rs` (no `cfg(test)`), reported.
  Planted cases prove each rule reports, among them a `NamedTempFile::new()` and a `NamedTempFile::new_in(dir)` in
  `install.rs` (SC-019).
- **FR-113** The `verify_file` callers of `minting_route_findings` (`injection.rs:2642-2651`) gain `INSTALL_TESTS_RS`;
  nothing else in `verified_artifact_findings` changes.
- **FR-114** The `#[cfg(test)]` archive helpers move from `extract_tests.rs` into a new
  `crates/core/src/tar_builder_tests.rs` (`#[cfg(test)] mod`, a name ending in `_tests.rs` so it stays a test source
  for `injection.rs`, `:1504-1511`) used by both `extract_tests.rs` and `install_tests.rs`. The moved set is exactly:
  `sha` (`:106-110`), `unhex` (`:112-118`), `literal_payload` (`:131-137`), `gz` (`:154-158`), `gunzip` (`:160-166`),
  `moduleinfo` (`:168-175`), `octal_field` (`:177-183`), `reseal` (`:185-193`), `struct Tar` and `impl Tar`
  (`:195-271`). Each moved item gains `pub(crate)` (they are private today, so the move is not byte-unchanged); no other
  change to their bodies. `extract_tests.rs` imports them; the fixture readers that read committed Phase 8 fixtures
  (`read_fixture`, `good`, `bad`, `good_listing`, `good_payload`, `:87-128`) stay where they are. Committed fixtures are read in place and never modified; versioned
  archives (`Alpha` 1.2.0 and 1.3.0, with and without `preset_subdir`, with an exec bit and a link) are built at test run
  time in a temp dir, each with a payload taken from a listing written literally in the test (Phase 8 FR-085).
- **FR-115** No Phase 9 test starts a process, opens a socket other than the local fixture server
  (`fixture_server.rs`, Phase 5), or touches a path outside its own `fresh_temp_roots()` TempDir; tests run in parallel
  with cargo's default threads. Every row whose expected outcome is a refusal runs over a fail-closed recording
  `InstallIo` (mutating methods record and return `io::Error::other("fail-closed")`, read methods forward), as Phase 8
  FR-087. `test_support::temp_roots_with_user_presets(td, leaf: &OsStr) -> Roots` spells a `presets().user` root
  named `leaf` (it does not create that root), through the one `Roots::new` site (plan SI-15).
- **FR-116** `tests/required-tests.json` gains every Rust test name of SC-001 to SC-029 and the SC-024 vitest titles,
  in disjoint lanes: names without an OS suffix under `cargo.all`; `_unix` names under `cargo.macos` and `cargo.linux`;
  `_windows` under `cargo.windows`; `_macos` under `cargo.macos`; `_linux` under `cargo.linux`; the vitest titles under
  `vitest`. `tools/feed-contract/tests/manager-ci-shape.test.js` `EXPECTED_REQUIRED` gains exactly the same names (the
  Phase 7 and 8 precedent). SC-007's compile-fail doctest is run by `cargo test --workspace` and adds no required name:
  a doctest's result line holds spaces and can never match `required tests ran`'s `/^test (\S+)(?: - should panic)?
  \.\.\. ok$/` (`manager-ci.yml:162`); the Phase 8 precedent (`specs/krate-manager-phase8/spec.md:597-599`: its doctests "run inside existing required names … and add no name").
  Totals after Phase 9 (plan §4.9): `cargo.all` 276, `cargo.windows` 12, `cargo.macos` 44, `cargo.linux` 39, `vitest`
  46 (plan SI-26).
- **FR-117** Logs: one `info!` line per action outcome (target `krate_manager_core::install`): plugin, version, action,
  the outcome or error code, the number of files placed / removed / kept; paths only as root kind plus relative path;
  one `info!` per recovered journal. No absolute path appears in a log line.

### K. Scope fences

- **FR-120** No change to `extract.rs`, `download.rs`, `verify.rs`, `detect.rs` behaviour (the FR-114 move touches only
  `extract_tests.rs`); `status.rs` gains only FR-100 (`actions`, `build_with_os`, `assemble_with_gates`; `build` and
  `assemble` keep their signatures and their output except the new field); `context.rs` gains only FR-080.
  `download.rs` gains `request_for_platform` and `command_request_for`; `request_for` and `command_request` delegate
  to them with `NativePlatform.os()` / `Arch::native()`; no other change (plan SI-1). `status.rs` also gains
  `pub(crate) fn update_state(entry, detection)` extracted from `plugin_view`, which calls it (PD-13) (plan SI-6).
- **FR-121** No change to `plugins/`, `dsp/`, `cmake/`, `extern/`, `release.yml`, `feed.yml`, `ci.yml`,
  `manager-ci.yml`, or `tools/feed-contract/` except `tests/manager-ci-shape.test.js` (FR-116); no Windows or macOS
  install code; nothing elevates.

---

## Success Criteria

Rust SCs run locally on Windows with `cargo test --workspace --locked` from `apps/krate-manager/` (default test
threads); a run counts only with every binary reporting `0 failed`. Vitest SCs run with `npm test` (parallel files).
macOS and Linux rows are **PENDING-CI** on `manager-ci.yml` jobs `check (macos-latest)` and `check (ubuntu-24.04)`,
step `cargo test` (`cargo test --workspace --locked`, `manager-ci.yml:117-120`), and close on a leg only when the
committed `tests/required-tests.json` lists the row's names and that leg's `required tests ran` step
(`manager-ci.yml:153-178`) passed with no `::error` line. The roadmap's Linux evidence (`cargo test -p
krate-manager-core install::`, line 702) is the subset of that run under `install::tests::`.

Every SC that runs `for_command`, `remove_for_command` or `status` passes its `os` (and for status its `arch`)
explicitly through `CommandEnv` / `status::build_with_os` (D16), so the `cargo.all` rows give the same verdict on all
three legs. The Linux rows pass `Os::Linux`, `Arch::X86_64` and a feed with only a `linux-x86_64` asset (plan
SI-1). Only SC-029 exercises the native wiring. "Requests" are those the local
fixture server records, split into the feed path and asset paths.

- **SC-001 (install → update → uninstall, roadmap line 694-695).** `install::tests::install_update_remove_cycle`
  (Windows FS, real-shaped-style archives with no link) and `install::tests::install_update_remove_cycle_unix` (an
  exec bit and an in-bundle link): in temp roots, with `preset_subdir` absent and `Kits`, a plugin root that does not
  exist yet, and two user presets planted in the preset folder (one beside, one inside a factory subfolder): after
  install 1.2.0 the tree under `plugins/vst3-user` and `presets/user` equals an expected snapshot (path, kind, size,
  mode on Unix); after update to 1.3.0 (one preset added, one removed, one changed) the same; after Remove only the user
  presets, `plugins/vst3-user` and `presets/user` remain. The three steps run through the commands (`for_command` with
  `os: Os::Linux` over a fixture feed offering 1.2.0, then 1.3.0; `remove_for_command`), and the returned
  `ActionSummary.notice` is `Some(RescanDaw)` after install 1.2.0, `Some(RescanDaw)` after the update to 1.3.0 and
  `None` after Remove (FR-091). Threshold: snapshot equality at all three steps; the two user presets byte-identical
  throughout; the three notices as stated. PENDING-CI for the `_unix` row on both legs.
- **SC-002 (crash safety, roadmap lines 696-698).** `install::tests::crash_after_each_step_recovers` (and `_unix`): for
  a first install and for an update, a crash cut (D12) after each FR-024 step 1 to 12 and after the first file of step 2
  and of step 3, then a fresh `install::recover` over a healthy IO: the target's bundle listing digest equals exactly
  the side the table gives (old, new, or absent), the preset destinations hold exactly that side's factory set, the
  manifest's `version` matches it (absent for "absent"), `<state>/journal/` is empty, and no path named `*.krate-new` /
  `*.krate-old` / `.*.json.tmp` exists under the roots or the state dir. Each case asserts its own row:

  | # | Cut after | Update (`had_old` true): recover decision → side | First install (`had_old` false): decision → side |
  |---|---|---|---|
  | 1 | step 1 `journal_begun` | `rolled_back` → old | `rolled_back` → absent |
  | 2 | first file of step 2 | `rolled_back` → old | `rolled_back` → absent |
  | 3 | step 2 `bundle_staged` | `rolled_back` → old | `rolled_back` → absent |
  | 4 | first file of step 3 | `rolled_back` → old | `rolled_back` → absent |
  | 5 | step 3 `presets_staged` | `rolled_back` → old | `rolled_back` → absent |
  | 6 | step 4 `journal_staged` (new and target present) | `rolled_back` → old | `rolled_back` → absent (new present, target absent) |
  | 7 | step 5 `old_aside` (new present, target absent, old present) | `rolled_back` → old | step 5 does not run: same state as #6, `rolled_back` → absent |
  | 8 | step 6 `new_in` (commit point) | `completed` → new | `completed` → new |
  | 9 | step 7 `presets_in` | `completed` → new | `completed` → new |
  | 10 | step 8 `manifest_written` | `completed` → new | `completed` → new |
  | 11 | step 9 `stale_removed` | `completed` → new | `completed` → new |
  | 12 | step 10 `journal_done` | `completed` (step `done`) → new | `completed` (step `done`) → new |
  | 13 | step 11 `old_deleted` | `completed` (step `done`) → new | `completed` (step `done`) → new |
  | 14 | step 12 `journal_removed` | no journal, no entry in the report → new | no journal, no entry → new |

  Threshold: 28 cases (14 cut points × 2), each equal to its row (decision and side); cuts 1 to 7 yield old (first
  install: absent), cuts 8 to 14 yield new (D7). PENDING-CI `_unix` on both legs.
- **SC-003 (manifest entry outside the roots, roadmap line 699).** `install::tests::manifest_outside_roots_refused`
  (and `install::tests::manifest_outside_roots_refused_unix` for the link case), each defect driving Remove and
  Update, each asserting its own code for both:

  | Manifest defect | Code (Remove and Update) |
  |---|---|
  | an absolute `path` | `E_INSTALL_MANIFEST` (FR-041) |
  | a `..` segment in `path` | `E_INSTALL_MANIFEST` (FR-041) |
  | a `path` through a directory symlink inside the root that points outside (`_unix`) | `E_INSTALL_OUTSIDE_ROOT { path }` (FR-063: proves the canonical check ran) |
  | a `plugin_root` other than the configured one | `E_INSTALL_ROOT_CHANGED { which: "plugin" }` (FR-064) |
  | a `preset_root` other than the configured one | `E_INSTALL_ROOT_CHANGED { which: "presets" }` (FR-064) |

  For every row the fail-closed recorder shows zero mutating calls, and a planted `outside/` sibling directory of the
  roots is byte-identical before and after.
- **SC-004 (Remove keeps user files, roadmap line 700).** `install::tests::remove_keeps_user_files`: after install, one
  placed preset edited, one user preset saved into a factory subfolder, one file added inside the bundle: Remove
  returns `kept` = the edited preset only (relative path), removes every other placed path and the whole bundle
  directory including the added bundle file (FR-122; `kind_of` of the bundle path is absent, no partial `<P>.vst3`,
  and `detect` then reports the plugin not installed with no manifest), keeps the subfolder holding the user preset,
  and leaves the user presets byte-identical.
- **SC-005 (preset placement rules, D5).** `install::tests::update_preset_rules`: on update, a stale unchanged preset is
  removed; a stale edited preset is kept and reported; a current preset the user edited is kept, reported and absent from
  the new manifest; a destination already holding identical content (manual install) is adopted without a write (the
  recorder shows no write to it) and recorded; a destination holding other content is kept and reported; a destination
  that is a symlink (Unix row) or a directory is kept and never followed. An update over a manual install (bundle
  1.2.0 at the target, presets copied by hand, no manifest) replaces the bundle, adopts the identical presets, keeps a
  hand-edited one, and writes a manifest that `detect::read_manifest` reads as 1.3.0. A plant-before-rename row (plan
  SI-22): just before step 7 a user file is written at a `Place` destination and a `Replace` destination is edited; the
  action is `Ok`, both planted files are byte-identical, both are in `kept`, neither is in the new manifest, and no
  `*.krate-new` is left.
- **SC-006 (Phase 6 reads Phase 9's manifest, FR-042).** `install::tests::manifest_reads_back_through_detect`: after
  install, `detect::read_manifest` returns `Found(Manifest { version_text: "1.2.0" })`, and
  `status::build_with_os(Os::Linux, Arch::X86_64, …)` over a fixture feed reports the plugin `installed: version 1.2.0`,
  `managed` copy at the target, `record: manifest`, `actions: ["remove"]`; with the feed then offering 1.3.0 it reports
  `update: update_available`, `actions: ["update", "remove"]`; after that update is installed, `actions: ["remove"]`
  and `update: up_to_date`.
- **SC-007 (verified only, Core Philosophy line 69-70).** `install::tests::unverified_never_installed`: with the fixture
  server serving a tampered archive, a wrong-key signature and a wrong-plugin signature, `for_command` (`os:
  Os::Linux`, `driver_for` a counting wrapper around the Linux driver) returns the Phase 7 code, the wrapper's `run`
  call count is 0, `extract::stage` is never reached, and the roots are unchanged. Separately, `Action` cannot be built
  without a `VerifiedArtifact` (a compile-fail doctest on `Action`; it adds no required name, FR-116).
- **SC-008 (state checks before any download, FR-090, FR-093, FR-094).** `install::tests::state_checks_before_download`
  (`os: Os::Linux`, fail-closed recorder, FR-115). Each row returns exactly its code, the recorder shows zero mutating
  calls, and the fixture server records exactly the stated requests:

  | Call and state | Code | Feed / asset requests |
  |---|---|---|
  | `for_command` with `action` `"reinstall"` | `E_INSTALL_ACTION` | 0 / 0 |
  | `for_command` `install` with a changed `expected_version` | `E_DOWNLOAD_VERSION_CHANGED` | 1 / 0 |
  | `update` when not installed | `E_INSTALL_STATE_CHANGED { shown: "update", current: "not_installed" }` | 1 / 0 |
  | `install` when installed and an update is available (1.2.0 managed, feed 1.3.0) | `E_INSTALL_STATE_CHANGED { shown: "install", current: "update_available" }` | 1 / 0 |
  | `install` when up to date | `E_INSTALL_NOT_NEWER` | 1 / 0 |
  | `update` to an equal version (up to date) | `E_INSTALL_NOT_NEWER` | 1 / 0 |
  | `update` to an older version (installed newer) | `E_INSTALL_NOT_NEWER` | 1 / 0 |
  | `install` on a plugin with a pending journal | `E_INSTALL_JOURNAL_PENDING` | 1 / 0 |
  | `remove_for_command` with no manifest | `E_INSTALL_NOT_MANAGED` | 1 / 0 |
  | `remove_for_command` with a pending journal | `E_INSTALL_JOURNAL_PENDING` | 1 / 0 |
  | `remove_for_command` for a name not in the feed | `E_INSTALL_UNKNOWN_PLUGIN` | 1 / 0 |
  | `remove_for_command` with the feed path answering 500 and no cached feed | `E_INSTALL_NO_FEED` | 1 / 0 |
  | (plan SI-26) a schema-1-shaped entry (no `release`, so no `install` block): `remove_for_command` | `E_INSTALL_NOT_MANAGEABLE` | 1 / 0 |
  | (plan SI-26) the same entry: `for_command` | `E_DOWNLOAD_UNAVAILABLE` (FR-090 step 2 runs first; `versions.v2.schema.json:162`) | 1 / 0 |

  `install::tests::driver_rechecks_state` (plan SI-26) calls `LinuxDriver::run` directly (no `for_command`) with
  verified artifacts and asserts the driver-side refusals, each with zero mutating calls and one `ActionResult`:
  `Action::Install` over an existing target → `E_INSTALL_STATE_CHANGED { shown: "install", current: "installed" }`;
  `Action::Update` with no target → `E_INSTALL_STATE_CHANGED { shown: "update", current: "not_installed" }`; a manifest
  version not ranked older → `E_INSTALL_NOT_NEWER`; a manual bundle (no manifest) whose version is not older →
  `E_INSTALL_NOT_NEWER`, one whose version is unreadable → `E_INSTALL_STATE_CHANGED { shown: "update", current:
  "unknown" }` (FR-025); an artifact whose format is not `TarGz` → `E_INSTALL_FORMAT` before `stage` (FR-002); a tree
  holding a link where symlinks are unsupported → `E_INSTALL_UNSUPPORTED` (FR-015); a pending manifest over
  `detect::MAX_MANIFEST_BYTES` → `E_INSTALL_MANIFEST { reason: "too large" }` (plan PD-21); an entry with no `install`
  → `E_INSTALL_NOT_MANAGEABLE` (FR-003).
- **SC-009 (I/O failures roll back or roll forward, FR-027 to FR-029).** `install::tests::io_failures_roll_back` (and
  `_unix`, which adds the `symlink`, `set_mode` and `sync_dir` rows). An update 1.2.0 → 1.3.0 is first run fault-free
  over a recorder to list, per mutating method, its calls and the FR-024 step each belongs to; then each row injects
  its fault into a fresh rig at the call the row names ("middle" = call ⌈n/2⌉ of the n calls of that method before
  `new_in`; its expected `step` is the one the fault-free recording gives). Every pre-commit row runs three times, with
  `StorageFull` → `E_INSTALL_FULL { step }`, `PermissionDenied` → `E_INSTALL_DENIED { step }` and
  `io::Error::other` → `E_INSTALL_IO { step, kind }` (FR-029).

  | Method, call (one-shot fault unless stated) | Expected `step` | Expected state after the action |
  |---|---|---|
  | `write_new`, 1st | `journal_begun` | **old**: target bundle digest, preset files and manifest byte-identical to before; no journal, no `.krate-*`, no `.*.json.tmp` |
  | `rename`, 1st (journal persist) | `journal_begun` | old, as above |
  | `create_dir`, 1st and middle | from the recording | old, as above |
  | `copy_new`, 1st | `bundle_staged` | old, as above |
  | `copy_new`, middle | from the recording | old, as above |
  | `copy_new`, last (a preset sibling) | `presets_staged` | old, as above |
  | `sync_file`, 1st and middle | from the recording | old, as above |
  | `write_new`, 2nd | `journal_staged` | old, as above |
  | `rename`, the `old_aside` call | `old_aside` | old, as above |
  | `rename`, the `new_in` call (after `old_aside` succeeded) | `new_in` | old, as above: `.krate-old` was renamed back first (FR-027) |
  | `rename`, the `new_in` call and the rename back of `.krate-old` | `new_in` | journal left at step `staged`; then `install::recover` over a healthy IO → `rolled_back`, old, as above |
  | `write_new`, every call after `new_in` (persistent) | `manifest_written` | journal left at step `staged` holding `pending_manifest`, target holds the new bundle digest; then `recover` over a healthy IO → `completed`: new bundle, new presets, manifest `version` 1.3.0, no journal, no leftovers |
  | `rename`, every `presets_in` call (persistent) | `presets_in` | as the row above |
  | `write_new`, the 3rd call only (the manifest temp, one-shot) | none: `Ok` | the in-process roll-forward completes (FR-027): new version recorded, no journal, no leftovers, one `warn!` naming `E_INSTALL_FULL` (StorageFull run) |
  | `remove_tree`, the `old_deleted` call (persistent) | none: `Ok` | new version recorded; journal at step `done`, `.krate-old` present, one `warn!`; then `recover` → `completed`, no leftovers |

  In every pre-commit row the verified artifact is still cached: a retry `for_command` makes 1 feed and 0 asset
  requests and succeeds.
- **SC-010 (staged tree changed after stage, FR-022).** `install::tests::changed_staged_file_refused`: a staged file
  rewritten between `stage` and the copy yields `E_INSTALL_CHANGED { path }` and SC-009's unchanged-state assertions.
- **SC-011 (collisions before any write, FR-014).** `install::tests::collisions_refused_before_write` (and `_unix` for a
  symlinked target): target is a file; target is a symlink to a directory; a stray `<bundle>.krate-new` with no journal;
  a preset directory destination that is a file; an existing `<dest>.krate-new`: each `E_INSTALL_COLLISION`, zero
  mutating calls, the stray entries untouched. Further rows: the plugin root a file → `E_INSTALL_COLLISION { path: {
  root: "plugin", path: "" } }` (plan SI-13); with `preset_subdir` `Kits`, `<user>/<Name>` a symlink to a directory
  outside the preset root, and with `preset_subdir` absent, `<user>/<Name>` itself a symlink (the symlinked-ancestor
  rows) → `E_INSTALL_COLLISION`, the link target untouched (plan SI-21); a stray `<bundle>.krate-old` with no journal,
  on both Install and Update → `E_INSTALL_COLLISION` (plan SI-26).
- **SC-012 (Remove resumes and is idempotent, FR-065, FR-072).** `install::tests::remove_crash_resumes`: a crash cut
  after the journal and after half of the entries, then `recover`: the Remove completes (same tree as an uninterrupted
  Remove); a second Remove after completion is `E_INSTALL_NOT_MANAGED` with no write.
- **SC-013 (start-up recovery, FR-070, FR-080).** `install::tests::startup_recovery` and a `context_tests` case
  `assemble_runs_install_recovery`: `assemble_with_os` over temp roots holding an unfinished journal, a manifest temp
  leftover and an `extract/` leftover returns `AppState` with `context` `Ok`, the journal resolved, the leftovers
  removed; a journal naming a path outside the roots is left in place, logged once with `E_INSTALL_JOURNAL`, the
  plugin's `actions` (`status::build_with_os(Os::Linux, Arch::X86_64, …)`) equal `[]`, and its `for_command` and
  `remove_for_command` return `E_INSTALL_JOURNAL_PENDING`. The same holds for a journal whose `preset_subdir` is forged
  (`..`) and for one with a sibling outside `preset_dir` (plan SI-20): each `refused` and left in place.
  `install::tests::recovery_table_rows` (plan SI-22, SI-26) plants a valid in-root `staged` journal beside each disk
  state the crash table never produces: `new` absent, `target` absent, `old` present → `rolled_back` to the old side;
  `had_old` true with `new`, `target`, `old` all absent, and `had_old` true, `new` absent, `target` present, `old`
  absent → `refused`, every node untouched, the journal still present, the plugin's gate
  `E_INSTALL_JOURNAL_PENDING`; `new` absent, `target` present, `old` present with a user file planted at a `Place`
  sibling's `dest` → `completed`, the planted file byte-identical and absent from the manifest written, one `warn!`
  naming it by root kind and relative path.
- **SC-014 (concurrency, FR-085).** `install::tests::concurrent_actions`, deterministic, no timing window (the Phase 7
  SC-014 pattern, `specs/krate-manager-phase7/spec.md:1042-1046`):
  - Same plugin: thread A runs `for_command` `install` with a progress closure that, on its first `Staging` event
    (FR-092: before the first mutating `InstallIo` call), sends `ready` on an `mpsc` channel and blocks on a second
    channel `go`. The test thread waits for `ready` (A holds the guard, FR-085), then calls `for_command` for the same
    plugin over its own fail-closed recorder → exactly `E_INSTALL_BUSY`, 0 mutating `InstallIo` calls for that call,
    exactly 1 feed and 0 asset requests for it; the same with `remove_for_command` → `E_INSTALL_BUSY`. Then it sends
    `go`, and A ends `Ok`.
  - Two plugins: thread A installs `Alpha` and thread B installs `Beta`, each held on its first `Staging` event the same
    way; the test waits for both `ready` signals (both are in flight at once), sends both `go`, and both end `Ok`; both
    manifests read back through `detect::read_manifest`.
  - Guard released after failure: an action whose `InstallIo` fails at `copy_new` 1st returns its `E_INSTALL_FULL`; a
    following action on the same plugin over a healthy IO is not `E_INSTALL_BUSY` and ends `Ok`.
- **SC-015 (sync before commit, FR-023).** `install::tests::sync_before_commit` (recorded call order): every file
  written under `.krate-new` and every sibling has a `sync_file` after its `copy_new` (FR-028) and before the journal's
  `staged` write (the `write_new` of its temp file); on Unix every created directory and sibling parent has a `sync_dir` before it, and the target's parent
  one after `new_in`; on Windows no `sync_dir` call is made.
- **SC-016 (no driver off Linux, FR-004, FR-093, FR-100).** `install::tests::no_driver_off_linux` and
  `status::tests::actions_per_state`: `driver_for(Os::Windows)` and `driver_for(Os::Macos)` are `None`;
  `for_command` and `remove_for_command` with `CommandEnv { os: Os::Windows, drivers: &NativeDrivers, … }` (the
  resolver, plan SI-10) and
  with `os: Os::Macos` each return `E_INSTALL_NO_DRIVER`, with 0 mutating `InstallIo` calls (fail-closed recorder) and
  exactly 1 feed and 0 asset requests. The `actions` table from `status::build_with_os` over Phase 6's fixture states
  (not installed, elsewhere, up to date, update available, installed newer, version unknown, not available, needs newer
  OS, manifest present / absent, journal pending, **manifest fails FR-041**, **manifest root changed (FR-064)**) equals
  the expected table for `(Os::Linux, Arch::X86_64)`, with `[]` for each of the last three rows, and is `[]` in every
  row for `(Os::Windows, Arch::X86_64)` and `(Os::Macos, Arch::Aarch64)`. For each Linux row, every action the table
  omits is refused by `for_command` / `remove_for_command` with its FR-093 / FR-094 code before any asset request (the
  equality D13 states; the listed actions are accepted end to end by SC-001, SC-005 and SC-006).
- **SC-017 (progress, FR-092).** `install::tests::progress_events`: for an update, the observed events are
  `Download(…)*`, `Staging`, `Placing { done }` strictly increasing from 1 to `total` = the number of placed files,
  `Committing`, `Done`, in that order; for Remove `Removing { done }` up to the entry count, then `Done`.
- **SC-018 (command surface, FR-096).** `config_guard.rs` (existing required names, updated assertions): `build.rs`,
  `capabilities/main.json`, the permission files and `generate_handler!` list exactly the eight commands; the planted
  removal of `commands::install_plugin` is reported. Neither command takes a path argument.
- **SC-019 (roster and real-path guards).** The existing roster test (Phase 5) passes with `install.rs` built in (no
  plugin name outside fixtures); `injection.rs` reports each planted FR-112 violation (`std::fs::rename` outside
  `FsInstallIo`, a `Command`, an `xattr` token, `flate2` in `install.rs`, a `NamedTempFile::new()` and a
  `NamedTempFile::new_in(dir)` in `install.rs`, a `persist` in `install.rs`, and the read plant `fn f(p:
  &std::path::Path) -> bool { p.exists() }` in `install.rs` (plan SI-18)) and is clean on the real tree (existing
  required names `injection_scanner_reports_planted_violations`, `verified_artifact_scanner_reports_plants`).
- **SC-020 (sandbox guard, FR-110).** `install::tests::sandbox_guard_panics`, not `#[should_panic]` (a test that ends
  at the panic can assert nothing after it): two `catch_unwind` blocks in the one test (plan SI-19): (a)
  `context::assemble_with_os` with an outside configured root → `Err` whose panic message is
  `with_configured_plugin_root`'s "configured plug-in root … is outside the test sandbox …" (`roots.rs:139-141`); (b)
  `for_command` (`os: Os::Linux`) over a `RecordingInstallIo` and an unfolded context whose `settings.linux_plugin_root`
  lies outside → `Err` whose panic message is the `check_sandboxed` message (`roots.rs:179-183`, containing `outside
  the test sandbox`), zero `InstallIo` calls, read or mutating, and 1 feed / 0 asset requests; then, after (b), the same
  plugin's `for_command` over a context whose root is back inside the sandbox (same state dir) is not
  `E_INSTALL_BUSY` (FR-085 "on unwind"). The guard is lexical (`roots.rs:172-185`): this row proves only that a path the
  test names outside its TempDir is never touched; link escapes are FR-063's canonical check (SC-003).
- **SC-021 (artifact and tree release, FR-026).** `install::tests::artifact_released_after_success`: after a successful
  install the artifact's `downloads/<hex>/` directory and the staged tree's `extract/` entry are gone; after a failed
  install the artifact remains and the `extract/` entry is gone.
- **SC-022 (logs, FR-117).** `install::tests::logs_one_line_per_action`: one `info!` line per action with the plugin,
  version, action and code; no captured line contains the TempDir's absolute path.
- **SC-023 (non-UTF-8 root, FR-016).** `install::tests::non_utf8_root_refused_unix`: a preset user root whose name holds
  the byte `0xff` yields `E_INSTALL_PATH_NOT_UTF8` with zero mutating calls. PENDING-CI on both legs.
- **SC-024 (UI, FR-097 to FR-099).** vitest in `src/status.test.ts`: `renders install for not-installed linux`,
  `renders update and remove for update-available linux`, `renders no install buttons on windows or macos`,
  `install asks for confirmation naming the plugin and version`, `install on an elsewhere copy names the other copy and says to remove it by hand`, `install without an elsewhere copy shows no other-copy line`, `update asks for confirmation naming both versions`,
  `remove asks for confirmation first`, `cancelling a confirmation invokes nothing`, `confirmed install sends name,
  version, action and a channel`, `disables the card's buttons while an action runs`, `shows placing progress`,
  `shows the outcome and the DAW notice after an install`, `shows the outcome and the DAW notice after an update`,
  `shows kept files after remove`, `shows the error code on failure`, `refreshes the status after an action and keeps
  the outcome`. Each title passes exactly once (`required tests ran`). The invoker is a recording fake (`Invoker`,
  `status.ts:87`): "invokes nothing" means it recorded no call. The `elsewhere` fixture path is
  `/opt/vst3/Alpha.vst3`, never under `/usr/` (plan SI-14).
- **SC-025 (Linux evidence, roadmap lines 701-703).** PENDING-CI `check (ubuntu-24.04)` and `check (macos-latest)`:
  every `install::tests::*_unix` name passes once (real POSIX filesystem: modes, links, `rename` of directories,
  directory `fsync`), and `check (ubuntu-24.04)` passes every `_linux` name, `check (macos-latest)` every `_macos`
  name (SC-029); the Windows-local run passes every `cargo.all` and `cargo.windows` name.
- **SC-026 (zero warnings, Cross-Cutting line 1012-1013).** `cargo fmt --check`, `cargo clippy --all-targets -- -D
  warnings`, `npm run typecheck`, `npm run lint` clean locally on Windows; no new `#[allow]` / `#[expect]` without a
  one-line reason; no `--test-threads` anywhere. macOS / Linux PENDING-CI on the same legs' `cargo fmt`, `clippy`,
  `typecheck`, `lint` steps.
- **SC-027 (every action over the trait, FR-090, FR-094; roadmap line 691).** `install::tests::commands_run_over_the_driver`:
  `CommandEnv { os: Os::Linux, drivers: <the resolver, returning a recording driver>, … }` (plan SI-10), whose `run`
  records each call's batch and
  returns `Ok` outcomes. `for_command` `install` and `update` and `remove_for_command` each call `run` **exactly once**
  with a batch of exactly one action: `Action::Install`, `Action::Update` and `Action::Uninstall` respectively, each
  with the entry whose `name` is the plugin's. The returned summaries carry `notice: Some(RescanDaw)`,
  `Some(RescanDaw)` and `None`, and the Remove summary's `kept` equals the recording driver's outcome's `kept`.
  `remove_for_command` makes no asset request. `install::tests::batch_runs_every_action` (plan SI-26, FR-001, D10):
  the real `LinuxDriver::run` over an `InstallIo` failing the 1st `copy_new` with `StorageFull`, with the batch
  `[Install Alpha, Install Beta, Uninstall Alpha]`, returns exactly three `ActionResult`s in batch order (`Alpha`,
  `Beta`, `Alpha`; `Install`, `Install`, `Remove`) with outcomes `E_INSTALL_FULL` (step `bundle_staged`), `Ok`,
  `E_INSTALL_NOT_MANAGED` (Alpha rolled back, so no manifest); Beta's manifest reads back through
  `detect::read_manifest`, and nothing of Alpha remains under the roots.
- **SC-028 (symlinked plug-in root, FR-010, FR-064).** `install::tests::symlinked_plugin_root_unix` (lanes `cargo.macos`
  and `cargo.linux`; PENDING-CI `check (ubuntu-24.04)` and `check (macos-latest)`, step `cargo test`): the configured
  Linux root is a symlink, inside the TempDir, to a real directory inside the TempDir. Install 1.2.0 → update 1.3.0 →
  Remove (through the commands, `os: Os::Linux`) ends in the SC-001 snapshots (read through the link's target);
  `<bundle>.krate-new` and `<bundle>.krate-old`, observed through a recorder, are created in the link target's directory
  beside the bundle; the manifest's recorded `plugin_root` compares equal under FR-064, so neither Update nor Remove is
  `E_INSTALL_ROOT_CHANGED`; the link itself is never removed or replaced.
- **SC-030 (decisions of the clarification session, FR-122 to FR-124, FR-123).** `install::tests::remove_deletes_bundle_whole` is SC-004's bundle half and `install::tests::cut_before_new_in_rolls_back_in_process` (`new_in` rename failing in process after `old_aside` returns the user to the old install, SC-009 and SC-002 rows 6-7 asserted old / absent); `roadmap_amendments_present` (node script or `rg` check, run by the comply step) finds in `specs/Krate-manager-roadmap.md` the three FR-123 statements (a, b, c). FR-122's `remove_tree` call shows in the recorder as exactly one `remove_tree` of the bundle path and no per-file unlink under it; a link inside the bundle is removed as the link and its target is intact (Unix row `remove_bundle_link_unix`). Each name is in `tests/required-tests.json` (FR-116).
- **SC-029 (production wiring per OS, D16).** Over `fresh_temp_roots()` and a fixture feed, with the native wiring
  (`NativePlatform.os()`, the resolver `NativeDrivers` over `install::driver_for` (plan SI-10), `FsInstallIo`,
  `status::build`):
  - `install::tests::native_wiring_installs_linux` (lane `cargo.linux`; PENDING-CI `check (ubuntu-24.04)`, step `cargo
    test`): `status::build` offers `["install"]` for the plugin, and the `CommandEnv` that `src-tauri` builds (FR-096)
    installs it through `for_command`; `status::build` then offers `["remove"]`.
  - `install::tests::native_wiring_offers_nothing_windows` (lane `cargo.windows`, the local run) and
    `install::tests::native_wiring_offers_nothing_macos` (lane `cargo.macos`; PENDING-CI `check (macos-latest)`):
    `status::build` gives `actions: []` for every plugin, and `for_command` / `remove_for_command` with the native
    `CommandEnv` return `E_INSTALL_NO_DRIVER` with zero writes under the roots.

---

## Safety

- **Injected roots.** Every destination comes from the injected `AppContext`: `linux_plugin_root_or_default` (settings,
  else `Roots::vst3_user()`), `presets().user`, `state_dir()` (manifests, journal), `staging_dir()` (Phase 7 / 8). Tests
  build them with `fresh_temp_roots()` only; `check_sandboxed` makes any escape a panic before the first touch
  (FR-110). Production builds them once in `src-tauri` (`Roots::from_os()`, Phase 4).
- **Verified before anything touches it.** Phase 9 installs nothing but a `StagedTree` made by Phase 8 from a
  `VerifiedArtifact` (size, SHA-256, minisign signature, C3 binding: Phase 7), re-checks every copied byte against the
  staged per-file hashes (FR-022), and never executes anything.
- **No elevation, no process.** Linux needs none (per-OS table line 118); `install.rs` names no `Command` (FR-112);
  nothing touches `/usr`, `/etc` or another user's files: the destinations are the user's own `~/.vst3` (or the
  configured root) and `~/Documents/Krate Audio`.
- **Feed strings never become paths unchecked.** `<Name>`, `vst3_bundle`, `preset_subdir` passed the contract patterns
  and are re-checked as single normal components; every archive path goes through `dest_path` (FR-012).
- **Removal is bounded.** Remove and stale-file removal delete only manifest entries whose state matches the record,
  under canonically checked configured roots (FR-061 to FR-064); recovery deletes only names its own journal holds
  (FR-051, FR-073); `.krate-old` removal never follows a link (External facts).
- **What tests never touch.** The real `~/.vst3`, `/usr/lib/vst3`, `~/Documents/Krate Audio`, the real state dir, any
  VST3 / Components / preset folder, ProgramData, the registry; no installer, no helper, no network beyond the local
  fixture server (FR-115).

---

## Edge cases

| Case | Behaviour | Where |
|---|---|---|
| Interrupted download | Phase 7 resumes it; nothing is staged or installed until it verifies | Phase 7; FR-090 |
| Disk full while staging | `E_INSTALL_FULL`; `.krate-new` and siblings removed; old install, presets and manifest unchanged; artifact kept for retry | FR-027, SC-009 |
| Disk full while writing the manifest (after `new_in`) | Roll forward: the journal keeps the pending manifest; the next start retries the write | FR-027, FR-071 |
| Crash / power loss at any step | The recovery pass at the next start yields exactly old or new, no leftovers | FR-070 to FR-075, SC-002 |
| File in use by a running DAW | Linux allows the renames; the DAW keeps the old library mapped until it reloads; the DAW notice tells the user to rescan or restart (Windows locking is Phase 10's) | FR-095; `unlink(2)` |
| Partial install (an earlier crash whose journal is lost or refused) | Strays named `.krate-*` are collisions, never deleted by guess; a refused journal blocks only that plugin's actions | FR-014, FR-074 |
| Manual README-style install already present (bundle in `~/.vst3`, presets in the user folder, no manifest) | Status shows it installed (managed copy, no record); no Remove (nothing recorded); when the feed is newer, Update replaces the bundle by the swap, adopts preset files whose content equals the new ones, keeps and reports differing ones, and writes the first manifest, after which the plugin is managed like any other | FR-021, FR-093, D5, SC-005 |
| Downgrade attempt (feed older than installed) | `InstalledNewer`: no action offered; a forged call is `E_INSTALL_NOT_NEWER` | FR-025, FR-093, SC-008 |
| Feed moved on since the screen was drawn | `E_DOWNLOAD_VERSION_CHANGED` (Phase 7) or `E_INSTALL_STATE_CHANGED`, before any download | FR-090, SC-008 |
| Corrupt feed | Phase 5 rejects it and keeps the last good copy; actions use that snapshot or are refused with `E_DOWNLOAD_NO_FEED` | Phase 5; FR-090 |
| Offline | Install / Update need the artifact (cached verified artifact accepted, Phase 7); Remove works from the cached feed and the manifest | FR-093, FR-090 |
| Feed `schema_version` 1 entry (no `release`, no `install`) | `not_through_manager`: no action; a forged Remove is `E_INSTALL_NOT_MANAGEABLE`, a forged Install / Update `E_DOWNLOAD_UNAVAILABLE` (FR-090 step 2 runs first; `versions.v2.schema.json:162`) (plan SI-26) | FR-003, FR-100 |
| A plugin dropped from the feed | Not listed (ROSTER), so no Remove; its manifest and files stay until it returns; an orphan-manifest Remove is Phase 12's (ruled, Q8) | FR-093, FR-094, FR-123 |
| Bundle that still says `0.8.0` at top level | Detection takes the class `Version` (Phase 6); Phase 8 checked the staged class version; the update decision uses Phase 6's state | Phase 6 FR-012; Phase 8 FR-057 |
| Linux root setting changed after install (in Phase 9 only by editing the settings file by hand) | Status offers no action for the plugin; a forged Update or Remove is `E_INSTALL_ROOT_CHANGED`, whose message tells the user to set the root back; Phase 12's settings screen will refuse the change while a manifest exists (ruled, Q3) | FR-064, FR-123, FR-093, SC-016 |
| Manifest written by a newer manager (`manifest_version` 2) | Detection reports `E_RECORD_FUTURE` (Phase 6); Update and Remove refuse with the same code | FR-041 |
| Two plugins installed at once | Independent; same plugin twice is `E_INSTALL_BUSY` | FR-085, SC-014 |
| `~/.vst3` is a symlink to another disk | Used as the root; `.krate-new` sits beside the target in the same directory, so every rename stays on one filesystem; the recorded root compares equal canonically | FR-010, FR-064; `rename(2)` `EXDEV`; SC-028 |
| `rename` of `.krate-new` onto the target fails in process (EACCES, EBUSY) after the old bundle was moved aside | `.krate-old` is renamed back first; the old install stays; if that fails too, the journal stays and the next start rolls back | FR-027, FR-071, SC-009 |
| A file the user added or edited inside the installed bundle | Update deletes it with the old bundle; Remove deletes the whole bundle; neither keeps or reports it (Q2) | FR-122, D5 |
| The only copy is outside the managed root (`elsewhere`) | Install is offered; its confirmation names the other copy and tells the user to remove it by hand; the manager never touches it (Q5) | FR-124, FR-099 |
| A user preset named like a new factory preset | Kept and reported; the factory file is not placed | D5, SC-005 |

---

## New components

| Module / file | Purpose |
|---|---|
| `apps/krate-manager/crates/core/src/install.rs` | `InstallDriver`, `Action`, `ActionResult`, `Outcome`, `ActionSummary`, `InstallError` and codes, `InstallEnv`, `CommandEnv`, `InstallProgress`, `InstallIo` + `FsInstallIo`, `driver_for`, `ActionGate`, `action_gate`, `allowed_actions`, the Linux driver, the planner, `dest_path`, the manifest writer and `read_manifest_full`, the journal, `recover`, the per-plugin registry, `for_command`, `remove_for_command` |
| `crates/core/src/install_tests.rs` | Every `install::tests::*` SC case |
| `crates/core/src/tar_builder_tests.rs` | The archive helpers moved from `extract_tests.rs` (FR-114) |
| `crates/core/src/test_support.rs` (🔶) | `MemInstallIo`, `FaultyInstallIo` (planned failures and the crash cut), `RecordingInstallIo` (forwarding and fail-closed), a counting and a recording `InstallDriver`, a test-context builder for two-plugin feeds; `temp_roots_with_user_presets(td, leaf: &OsStr) -> Roots` (spells that root, does not create it), through the one `Roots::new` site (plan SI-15) |
| `crates/core/src/lib.rs` (🔶) | `pub mod install;` |
| `crates/core/src/status.rs` (🔶), `src/view.ts` (🔶) | `PluginView.actions`; `build_with_os`, `assemble_with_gates` (FR-100) |
| `crates/core/src/context.rs` (🔶) | The recovery call in `assemble_with_os` |
| `src-tauri/src/commands.rs`, `build.rs`, `src/lib.rs`, `capabilities/main.json`, `permissions/autogenerated/{install_plugin,remove_plugin}.toml` (🔶) | The two commands |
| `src/status.ts`, `src/status.test.ts` (🔶) | Action buttons, confirmation, progress, outcome, refresh |
| `crates/core/tests/injection.rs`, `crates/core/tests/config_guard.rs` (🔶) | FR-112, FR-113, FR-096 guards |
| `tests/required-tests.json`, `tools/feed-contract/tests/manager-ci-shape.test.js` (🔶) | The SC test names (FR-116) |
| `tests/fixtures/status/*.json` (13 files) (🔶) | Every plugin object gains `"actions": []` as its last key (the field is the last of `PluginView`); FR-121 unaffected (these are app fixtures, not `tools/feed-contract/`) (plan SI-4) |

---

## Traceability

| Roadmap statement (line) | Requirements | Success criteria |
|---|---|---|
| `InstallDriver` trait, install / update / uninstall with named results (671-672) | D10, D16, FR-001 to FR-004, FR-029, FR-091 | SC-007, SC-016, SC-027, SC-029 |
| Install plan from staged tree, settings model, `Roots`; bundle to `<settings root>/<P>.vst3`; presets to `~/Documents/Krate Audio/<P>` + `preset_subdir` (672-676) | D1 to D5, FR-010 to FR-016, FR-020 to FR-022 | SC-001, SC-005, SC-023 |
| Journaled bundle swap: stage beside target, journal, two renames, done, delete old (677-681) | D6, D7, D12, FR-023, FR-024, FR-027, FR-028, FR-044, FR-050 to FR-052 | SC-002, SC-009, SC-015, SC-028 |
| Recovery pass at every start completes or rolls back (681-683) | D14, FR-070 to FR-075, FR-080, FR-081 | SC-002, SC-012, SC-013 |
| Manifests: every placed path with its hash; update removes what the new payload lacks (685-686) | D8, FR-040 to FR-044 | SC-001, SC-005, SC-006 |
| Remove driven only by manifest paths under configured roots; user presets never touched; edited file left and reported; no reinstall-previous (687-690) | D9, FR-060 to FR-066, FR-094; OQ-1 | SC-003, SC-004, SC-012, SC-027 |
| UI actions Install / Update / Remove with progress over the trait (691) | D11, D13, D15, D16, FR-085, FR-090 to FR-100 | SC-008, SC-014, SC-016 to SC-018, SC-024, SC-027, SC-029 |
| Outcome: install → update → uninstall exact trees, with / without `preset_subdir` (694-695) | FR-010 to FR-013, FR-061 | SC-001 |
| Outcome: crash after each journaled step → exactly old or new, no leftovers (696-698) | FR-024, FR-071, FR-075 | SC-002 |
| Outcome: manifest entry outside the roots refused, nothing outside touched (699) | FR-063, FR-064 | SC-003 |
| Outcome: edited placed preset survives Remove and is reported; unplaced file survives (700); the bundle is removed whole (Q2) | FR-061, FR-062, FR-122 | SC-004, SC-030 |
| Clarification session 2026-11-01 (Q1 to Q8, D6, D7, D10, D14) | FR-071, FR-099, FR-122 to FR-124 | SC-002, SC-004, SC-024, SC-030 |
| Outcome: OS-neutral logic on Windows; PENDING-CI Linux leg (701-703) | D12, FR-004, FR-116 | SC-025 |
| Does NOT run an installer, elevate or self-update (705) | FR-121 | SC-019 |
| Q7: never close a DAW; DAW notice after every install (30, 1000-1004) | FR-091, FR-095 | SC-001, SC-024, SC-027 |
| Cross-Cutting: any batch asks one confirmation listing every item (1000) | D15, FR-097, FR-099 | SC-024 |
| ROSTER: every fact from the feed (33, 1017-1019) | FR-003 | SC-019 |
| Never touch the real system; verified before use; parallel tests; zero warnings (984-1016) | FR-110 to FR-117 | SC-007, SC-019 to SC-022, SC-026 |

---

---

## Clarifications

### Session 2026-11-01

- Q1: Is the commit point `new_in` (D7), so a cut between `journal_staged` and `new_in` rolls back, and roadmap line 682 is amended? Decision: yes (A); SC-002 rows 6-7 end old / absent; a failed in-process `new_in` rename always returns the user to the old install. [FR-071, FR-123, SC-002, SC-009, SC-030]
- Q2: What happens to a user-added or edited file inside the bundle on Remove and Update? Decision: the bundle is one unit on both paths; Remove deletes the whole `<vst3_bundle>` tree (`remove_tree`, never following a link) like Update's swap; keep-and-report applies to preset files only; SC-004's added bundle file is removed, not kept. [FR-061, FR-062, FR-122, SC-004, SC-030]
- Q3 (OQ-1): What happens when the recorded roots differ from the configured roots? Decision: keep FR-064's `E_INSTALL_ROOT_CHANGED` refusal; roadmap Phase 12 records that the settings screen refuses to change the Linux root while any manifest exists (the user removes first); no move or re-root code anywhere. [FR-064, FR-123, SC-016, SC-030]
- Q4: Take over a manual README-style install? Decision: as written; no Remove while unmanaged; Update (only when the feed is newer) replaces the bundle, adopts byte-identical presets, keeps differing ones and writes the first manifest; a later Remove also deletes the adopted presets. [FR-021, FR-060, FR-093, SC-005]
- Q5: Is Install offered when the only copy is `elsewhere`? Decision: yes (B); the FR-099 confirmation names the other copy and tells the user to remove it by hand; the manager never touches `/usr`. [FR-124, FR-099, FR-066, SC-024]
- Q6: Do single-card Install, Update and Remove confirm? Decision: all three ask a one-item in-page confirmation (D15); the SC-024 confirmation titles stay. [FR-099, FR-097, SC-024]
- Q7: What is placed when an update changes a factory preset the user edited? Decision: the user's file stays, the new factory version is not placed, the path is reported as kept. [D5, SC-005]
- Q8: Must Remove have a feed entry? Decision: Phase 9 stays feed-only (a live or cached feed must list the plugin; ROSTER unchanged); an orphan-manifest Remove is added to roadmap Phase 12. [FR-094, FR-123, SC-030]
- D6: The journal is written at step `begun`, before any staging; the manager never deletes a `.krate-new` or `.krate-old` it did not record. Accepted. [FR-014, FR-024, FR-050, FR-051, FR-071]
- D7 / D10: Presets are staged as `.krate-new` siblings and committed with the bundle under one journal; `InstallDriver::run(env, Vec<Action>) -> Vec<ActionResult>` is batch-shaped for Phase 10's one prompt per batch. Accepted. [FR-001, FR-024, FR-071, SC-027]
- D14: The recovery pass runs inside `context::assemble_with_os` on every OS before any command, never fails startup; an unrecoverable journal blocks only that plugin's actions (`E_INSTALL_JOURNAL_PENDING`). Accepted. [FR-052, FR-074, FR-080]

### Session 2026-11-01 (plan preconditions)

The user ruled on the three preconditions of plan §10 and tasks.md "Preconditions", all as recommended.

- R-1 (PD-1 / SI-1): the command's download leg resolves the asset for the command env's os / arch through
  download::request_for_platform / command_request_for; request_for / command_request delegate with the native pair.
- R-2 (PD-3 / SI-3): download::discard runs after obtain_and returns; a discard failure is one warn! and the action
  stays Ok.
- R-3: a directory fsync refused by the filesystem (EINVAL / Unsupported) stays strict: it maps to its FR-029 code and
  rolls back before the commit point; no error kind is tolerated.

### Session 2026-11-01 (build ruling)

- FR-061 vs SC-004: on Remove, is a non-empty real directory at a manifest `d` entry (a factory preset folder that
  still holds the user's own presets) reported in `kept`? → no, as recommended: it is left silently; `kept` lists
  only manager-installed entries that were left (changed content, another kind). FR-061 amended; SC-004 stands as
  written; the code (install.rs `remove_entries`) already behaves so. [FR-061, SC-004]
