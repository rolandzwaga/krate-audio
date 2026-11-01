# Implementation Plan: Krate Manager Phase 9 — Linux Install, Update and Uninstall (unprivileged)

**Spec:** `specs/krate-manager-phase9/spec.md` (DRAFT revision 2, decisions D1-D16 and clarifications Q1-Q8 of
2026-11-01 encoded, `spec.md:18-20`, `:1079-1094`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 9 (lines 663-705); Decisions STRATEGY (23), Q3 (26), Q7 (30), Q8 (31),
ROSTER (33); per-OS table, Linux row (118); "Repo fact that shapes the strategy" (120-126); "Where state lives" (158-163);
Phase 10, the next `InstallDriver` (753-759); Phase 12 "Update all" (882-886); edges 4→9, 6→9, 8→9, 9→10 (956-957);
Cross-Cutting Constraints (984-1025)
**Status:** DRAFT plan, revision 2, 2026-11-01 (revision 1 plus the plan-review fixes: PD-24 to PD-30, SI-20 to
SI-27, three new test names, the off-Linux feed rule of §7), verified against `feat/krate-manager` at `abdd2c9e`
(`git rev-parse --short HEAD`; untracked: `specs/krate-manager-phase9/`).
**Lanes:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm typecheck / lint /
test, the version lint) and the **pipeline lane** (`node --test tools/feed-contract/tests/`, because
`manager-ci-shape.test.js` changes). No C++ file changes, so there is no `run-gate.js` / `header-consumers.js` lane.

**Build preconditions.** The spec edits of §0.2 land before `tasks.md`. SI-1 to SI-4 are **blocking**: built as the
spec reads, SI-1 makes every `cargo.all` command-level SC unpassable on Windows and macOS (the feed contract refuses a
`tar.gz` asset under a `windows-*` or `darwin-*` key, so the "native key" feed FR-090 asks for fails `feed::parse`);
SI-2 makes every command-level SC fail with `E_DOWNLOAD_URL` (asset URLs are `https://…example.invalid` and the only
way to point a request at the fixture server is confined to `download.rs` / `download_tests.rs`); SI-3 makes every
successful command return `E_DOWNLOAD_BUSY` from the `download::discard` FR-026 places inside `next`; SI-4 turns
`status::tests::golden_views` and the vitest golden suite red (the 13 status goldens lack the new `actions` key). §10
lists the decisions the user may want to overrule.

---

## 0. What this plan adds to the spec

The spec fixes requirements, names, codes and test names. This plan fixes the module boundaries and exact signatures,
the algorithms the spec states in prose (the planner, the copy, the journal states, the two roll paths, the recovery
table, the gate), the test doubles and the crash-cut mechanism, every change an existing guard or golden needs to stay
green, the per-SC test design and the local close procedure. Where the tree contradicts a spec sentence, the conflict
is in §0.2.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **The download leg resolves for `env.os` / `env.arch`, not the native platform.** `CommandEnv` gains `arch: Arch`; `download.rs` gains `pub fn request_for_platform(feed, name, os, arch, running)` and `pub fn command_request_for(ctx, name, expected_version, now_unix, host, os, arch)`; the existing `request_for` / `command_request` become one-line delegations passing `NativePlatform.os()` and `Arch::native()`. Production passes the native pair, so behaviour is unchanged and every Phase 7 test runs unchanged. | `versions.v2.schema.json:136-146` binds `^windows-` to `format: inno-exe`, `^darwin-` to `pkg`, `^linux-` to `tar.gz`; `contract.rs:12` compiles that schema into `feed::parse`; `test_support.rs:360-364` follows the binding. A `tar.gz` under `windows-x86_64` is therefore an invalid feed, and `request_for` resolves only `version::resolve_native` (`download.rs:753-768`, `version.rs:233-240`). | FR-004, FR-090, FR-120, SC preamble (SI-1) |
| PD-2 | **A test-only request seam.** `CommandEnv` holds `#[cfg(test)] pub(crate) request_seam: Option<&'a dyn Fn(DownloadRequest) -> DownloadRequest>`; `for_command` applies it to the request `command_request_for` returned, before `obtain_and`. The seam closures live in `install_tests.rs` and call `with_fixture_url` there; only `install_tests.rs` ever assigns the field a `Some` (PD-30). | Asset URLs are `https://assets.example.invalid/<lower>/…` (`test_support.rs:352`); a test transport refuses them (`download_tests.rs:2635-2650`, `E_DOWNLOAD_URL`); `DownloadRequest::with_fixture_url` is `#[cfg(test)] pub(crate)` (`download.rs:70-96`) and `fixture_seam_findings` allows its name only in `download.rs` / `download_tests.rs` (`injection.rs:3359-3366`). Phase 7's own command test splits `command_request` → `with_fixture_url` → `run_command` for the same reason (`download_tests.rs:2583-2598`). | FR-004, FR-090, FR-112 (SI-2) |
| PD-3 | **`download::discard` runs after `obtain_and` returns, never inside `next`.** `next` returns `(ActionResult, VerifiedArtifact)` (the clone); on an `Ok` outcome the command calls `download::discard(clone, env.download.io)` after `obtain_and` returned. A discard failure is logged once (`warn!`, its code) and does not change the `Ok` result: the install is committed and recorded; the next Phase 7 prune removes the directory. | `obtain_body` takes the in-flight guard (`download.rs:1359`) and calls `next(artifact)` in its return expression (`:1403`, `:1447`) while the guard is alive; `discard` takes the same guard (`:1497`) and returns `DiscardBusy` → `E_DOWNLOAD_BUSY` (`:1498-1503`, `:449`). | FR-026, FR-090 step 5 (SI-3) |
| PD-4 | **One shared sink for download and install progress.** `for_command` wraps the caller's `progress` in a `RefCell<&mut dyn FnMut(InstallProgress)>`; the `obtain_and` progress closure and the `next` closure each borrow it for one call. They never overlap: `obtain_and` emits nothing while `next` runs (`download.rs:1401-1403`, `:1443-1447`). | `obtain_and` takes `progress: &mut dyn FnMut(Progress)` and `next: impl FnOnce` at once (`download.rs:1319-1325`); two closures cannot both hold `&mut progress`. | none (mechanism) |
| PD-5 | **The resolver is a trait, not `&dyn Fn`.** `pub trait DriverResolver { fn driver_for(&self, os: Os) -> Option<&dyn InstallDriver>; }`; production `pub struct NativeDrivers;` delegates to `install::driver_for` (its `&'static` result shortens to `&self`'s lifetime); test resolvers return `Some(&self.driver)`. `CommandEnv.drivers: &'a dyn DriverResolver`. | A trait object's `Output` binding is invariant, so `&install::driver_for` (output `Option<&'static dyn InstallDriver>`) does not coerce to the spec's `&'a dyn Fn(Os) -> Option<&'a dyn InstallDriver>`, while a test's recording driver is not `'static`. | FR-004, FR-090, FR-096 (SI-10) |
| PD-6 | **`install::native_env` is the one production `CommandEnv` builder**, in core: `os: NativePlatform.os()`, `arch: Arch::native()`, `drivers: &NativeDrivers`, `request_seam: None` under test. `src-tauri` calls it; SC-029 tests it directly. | `src-tauri`'s lib has `test = false` (`src-tauri/Cargo.toml:10-14`), so "the `CommandEnv` that `src-tauri` builds" (SC-029) is testable only if the builder lives in core. | FR-096, SC-029 |
| PD-7 | **The `begun` journal already holds the whole plan**: target, `.krate-new`, `.krate-old`, `had_old`, every planned sibling, every planned directory to create, the pending manifest and the previous manifest. Step 4 (`journal_staged`) rewrites only `step`. | FR-024 step 3 creates preset directories "recorded in the journal", but the only journal written before step 3 is step 1's; a cut at SC-002 rows 4 or 5 would leave unrecorded directories. The planner knows every one of these before the first write (D4). | FR-024, FR-050 (SI-11) |
| PD-8 | **Crash cuts are counted calls, step boundaries are read from a fault-free recording.** `FaultyInstallIo::crash_after(k)` makes every mutating call after the k-th fail (`io::Error::other("crash")`) and never runs cleanup; `install_tests.rs` `fn boundaries(calls) -> BTreeMap<Cut, usize>` derives each SC-002 cut index from the recorded call shapes (§4.5). `InstallIo` gets no step hook. | FR-028 fixes the surface "exactly"; D12 says the cut is "every mutating call after a chosen step returns an error and cleanup does not run". Recording-derived boundaries keep the trait as specified and make the cut table data, not code. | D12 (mechanism) |
| PD-9 | **`sync_file` opens the file `write(true)`** (no create, no truncate) before `sync_all`, on every OS. | `File::sync_all` on Windows calls `FlushFileBuffers`, whose "file handle must have the GENERIC_WRITE access right" (https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers); a read-only `File::open` handle fails there, and every `cargo.all` row runs on Windows. Unix `fsync` accepts either. | FR-028 (SI-7) |
| PD-10 | **I/O error kinds map as Phases 7 and 8 do:** `StorageFull` / `QuotaExceeded` → `E_INSTALL_FULL`; `PermissionDenied` / `ReadOnlyFilesystem` → `E_INSTALL_DENIED`; else `E_INSTALL_IO`. | `extract.rs:171-185`, `download.rs:347-351`; `QuotaExceeded` stable 1.85.0, `ReadOnlyFilesystem` 1.83.0 (https://doc.rust-lang.org/std/io/enum.ErrorKind.html, std 1.99.1); workspace `rust-version = "1.90"` (`Cargo.toml:9`). | FR-029 (SI-8) |
| PD-11 | **`InstallProgress::Download { event: Progress }`** (struct variant), the enum `#[serde(tag = "kind", rename_all = "snake_case")]`. | `Progress` is itself internally tagged on `kind` (`download.rs:228-251`); a newtype variant `Download(Progress)` under the same tag would serialize two `kind` keys. | FR-092 (SI-9) |
| PD-12 | **`install::channel_progress`** mirrors `download::channel_progress` for `InstallProgress`. | `download::channel_progress` is typed `FnMut(Progress)` (`download.rs:1582-1590`); FR-120 keeps `download.rs` behaviour unchanged. | FR-092 (SI-5) |
| PD-13 | **One update-state function.** `status.rs` gains `pub(crate) fn update_state(entry: &PluginEntry, detection: Option<&Detection>) -> Option<UpdateState>` holding `plugin_view`'s managed-copy rule (`status.rs:330-364`); `plugin_view` calls it; `for_command` calls it. | D13 requires the card and the command to compute the action from the same function; today the update state is computed only inside the private `plugin_view`. | FR-100, FR-120 (SI-6) |
| PD-14 | **`detect::parse_manifest(bytes, plugin) -> RecordRead<InstallerRecord>`** extracted (pure) from `read_manifest` (`detect.rs:891-913`); `read_manifest` calls it; `install::read_manifest_full` reads the file through `InstallIo::read_file` and applies it, so the three-key codes are one implementation. `read_manifest_full` takes `io` as its first argument. | FR-028 makes `InstallIo` the only filesystem route of `install.rs`; FR-041 requires `detect`'s codes. Behaviour of `read_manifest` unchanged. | FR-041, FR-120 |
| PD-15 | **Gates only where a driver exists.** `status::build_with_os` computes `action_gate` per entry only when `drivers.driver_for(os)` is `Some`; otherwise every card's `actions` is `[]` with no journal or manifest read. | Same output as FR-100 (with no driver `allowed_actions` is empty); no extra I/O on Windows and macOS until Phase 10. | FR-100 |
| PD-16 | **`action_gate` reads the manifest only for `Os::Linux`**; the journal check runs on every OS. | The manifest is the Linux record only (`detect.rs:1009-1013`); journals are OS-neutral (FR-080 runs recovery everywhere). | FR-093 |
| PD-17 | **Resolution of not-yet-existing paths (FR-063) and of a vanished recorded root (FR-064).** FR-063 canonicalizes the deepest existing ancestor of the entry's parent (its remaining segments are `Component::Normal` by FR-012, so they cannot climb) and requires it inside the canonical root; an entry whose parent does not exist is "already removed" for Remove. FR-064 compares a recorded root with the configured one as equal when the two strings are equal, else when both canonicalize to the same path; a canonicalize failure with unequal strings is `E_INSTALL_ROOT_CHANGED`. | FR-063 / FR-064 state the comparison but not the missing-path case; `canonicalize` fails on a missing path. | FR-063, FR-064 (SI-16) |
| PD-18 | **Directories are recorded only when Phase 9 owns them**: a `d` entry for every directory this action creates, plus every directory the previous manifest recorded that the new layout still has. A pre-existing directory the manager did not create is never recorded (so never removed). | FR-062 "nothing … not listed in the manifest is ever removed"; D5 defines adoption for files only. | FR-040, D5 (SI-17) |
| PD-19 | **`remove_tree` by kind**: `kind_of` first; `Dir` → `remove_dir_all`; `Symlink` / `File` / `Other` → `remove_file`; `None` → `Ok`. No Windows link fallback: Phase 9 never creates a link on Windows (`supports_symlinks() == false`, planner step 4), so a link at a `remove_tree` path there is outside interference, and `remove_file`'s error is returned as the named step's `E_INSTALL_*` code (nothing followed, nothing lost). | `remove_dir_all` "does **not** follow symbolic links" but "will fail if the `path` is _not_ a directory" and is "not idempotent" (https://doc.rust-lang.org/std/fs/fn.remove_dir_all.html, std 1.99.1). | FR-028 |
| PD-20 | **FR-112 is extended to reads**: outside `impl InstallIo for FsInstallIo`, `install.rs` holds none of `fs :: read`, `fs :: read_dir`, `fs :: metadata`, `fs :: symlink_metadata`, `fs :: canonicalize`, `fs :: read_link`, `File :: open`, `. exists (`, `. try_exists (`, `. is_dir (`, `. is_file (`, `. is_symlink (`, `. metadata (`, `. symlink_metadata (`. | FR-028 "the only way `install.rs` touches the filesystem"; FR-112 confines only mutating tokens, so a planner `path.exists()` would bypass every test double unseen. | FR-112 (SI-18) |
| PD-21 | **A manifest over `detect::MAX_MANIFEST_BYTES` is refused before any write** (`E_INSTALL_MANIFEST { reason: "too large" }`); the journal bound is `MAX_JOURNAL_BYTES = 2 * MAX_MANIFEST_BYTES + 64 KiB`. | Phase 6 refuses a larger manifest as `E_RECORD_READ` (`detect.rs:884-889`); writing one would make the install undetectable. | FR-040 |
| PD-22 | **UI: all new code stays in `status.ts` / `view.ts`; new strings in a new `INSTALL_TEXTS` table; channels through an injected `ChannelMaker`.** `createStatusSection(invoke, makeChannel = tauriChannel)` and `renderCards(view, invoke, makeChannel = tauriChannel)` gain an optional parameter, so existing callers and tests compile unchanged. | `status.test.ts:380-383` asserts the non-test sources are exactly `main.ts`, `notes.ts`, `status.ts`, `view.ts`; `status.test.ts:260-269` requires every `STATUS_TEXTS` part to appear in a golden render; `Channel`'s constructor needs `__TAURI_INTERNALS__.transformCallback` (`node_modules/@tauri-apps/api/core.js:116-125`). | FR-097 (mechanism) |
| PD-23 | **The destinations are computed and sandbox-checked first.** Right after the per-plugin guard, `for_command` / `remove_for_command` build `Destinations::of(ctx, entry)` (pure: FR-010 to FR-012, FR-016 checks, `check_sandboxed` on every path, FR-110) before `action_gate`, detection or any `InstallIo` call. | SC-020 asserts zero `InstallIo` calls, read or mutating, before the panic; and `detect::candidates` `debug_assert!`s that the Linux managed root is a plug-in root (`detect.rs:452-456`), which an unfolded outside root would trip first with another message. | FR-110, SC-020 |
| PD-24 | **Placements are contained like removals.** Planner step 5b (read-only): (a) every existing directory on the chain from `preset_root` (exclusive) to `preset_dir` (inclusive) has `kind_of == Some(Dir)`, else `Collision { presets, <rel> }`; (b) every planned destination (bundle node under `new`, preset file, preset link, preset directory, `.krate-new` sibling) has the deepest existing ancestor of its parent canonicalized (PD-17's rule) and required equal to or under `canonicalize(plugin_root)` / `canonicalize(preset_root)`, else `OutsideRoot`. A root that does not exist yet makes (b) vacuous for its side (everything under it is created by this action). | `kind_of` is `symlink_metadata`, which follows every non-final component (§4.1.2), so with `preset_subdir` set (FR-011) a symlinked `<user>/<Name>` makes `kind_of(<user>/<Name>/Kits)` a `Dir`; FR-063 (`spec.md:477-483`) checks only previous-manifest entries, and `check_sandboxed` is lexical (`roots.rs:172-183`). Placing through the link would record entries every later Update / Remove refuses with `E_INSTALL_OUTSIDE_ROOT`. | FR-014, FR-063 (SI-21) |
| PD-25 | **One destinations constructor for feed entries and journals.** `Destinations::from_parts(ctx, name, vst3_bundle, preset_subdir: Option<&str>)` runs every `Destinations::of` check (`one_segment` on all three strings, UTF-8, `check_sandboxed`); `of(ctx, entry)` reads the three facts from `entry.release.install` and calls it; `recover` calls it with the journal's `plugin`, `vst3_bundle` and `preset_subdir`. The journal gains those two fields. | `recover` has no feed entry (`recover(ctx, io, extract)`, FR-070) and `AppContext` holds no feed (`context.rs:50-60`); the FR-050 journal (`spec.md:454-457`) records no `preset_subdir`, so `check_journal`'s "under `preset_dir`" rule could not be computed for Membrum-style entries, and on-disk strings would reach the roots unchecked. | FR-050, FR-073 (SI-20) |
| PD-26 | **A sibling commits only over the pre-state it was planned against.** Each journal sibling records `pre`: `absent` (Place) or the previous-manifest `sha256` (Replace). Before each step-7 rename and each `roll_forward` rename, `dest` is re-read (`kind_of`, then `hash_file` for a file); a mismatch removes `<dest>.krate-new`, adds `dest` to `kept`, drops the entry from the manifest about to be written, and is not an error. | `rename` replaces an existing `to` (§1.2); D5 is decided at planning time only, and `roll_forward` may run days after a crash, so a user preset saved at a Place destination in between would be overwritten, against §6 "a user file is never overwritten". | FR-024 step 7, FR-050, FR-071 (SI-22) |
| PD-27 | **The driver's downgrade re-check also covers a bundle with no manifest.** Planner step 2, `Update`, target present, `previous == None`: `io.read_file(<target>/Contents/Resources/moduleinfo.json, detect::MAX_MODULEINFO_BYTES)`, UTF-8, `detect::moduleinfo_version(text, path)` (the Phase 6 parser with its class-`Version` fallback, `detect.rs:692`); `Known` and not ranked older than the staged version by `version::compare` → `NotNewer`; `Unknown` or unreadable → `StateChanged { "update", "unknown" }`. | FR-025's driver re-check exists for callers other than `for_command` (`spec.md:360-367`), yet the manifest-only rule lets such a caller replace a newer manual install (FR-021) with an older verified artifact. | FR-025 (SI-23) |
| PD-28 | **State subdirectories are real directories or absent, everywhere.** `write_atomic`, `read_manifest_full`, `action_gate`'s journal probe and planner step 0 require `kind_of(<state>/journal)` and `kind_of(<state>/manifests)` to be `None` or `Some(Dir)`; any other kind is `Journal` / `Manifest { reason: "state directory is not a directory" }` before any write. `recover` already reads `journal/` only when it is a `Dir`. | `write_atomic` (§4.1.5 revision 1) wrote through a symlinked subdirectory that `recover` then never reads, while `action_gate`'s probe followed the link: a permanent `E_INSTALL_JOURNAL_PENDING` nothing clears. | FR-044, FR-093 (SI-24) |
| PD-29 | **A link target never leaves its top-level entry lexically.** For each `l` line `<T>/<s1>/…/<sk>/<name> -> <target>` with `u` leading `..` segments, the planner requires `u ≤ k` (the resolution never reaches the archive root), else `Path { reason: "link target leaves its top-level entry" }`, zero writes. A placed preset link is created as `symlink(target, <dest>.krate-new)` in step 3 and renamed in step 7. | C4 accepts a target that climbs to the archive root and re-enters its own top-level entry by name (`CONTRACT.md:317-323`); Phase 8's "lexical and physical resolution agree" (`CONTRACT.md:330-333`) holds for the archive layout, not after `presets` is remapped to `preset_dir`, where `presets/a/l -> ../../presets/b` resolves to `parent(preset_dir)/presets/b`. With `u ≤ k` the remap is transparent. | FR-012, FR-015 (SI-25) |
| PD-30 | **`test_support` builds `CommandEnv` seam-free.** `InstallRig::command_env(os, arch, io, drivers)` initialises `#[cfg(test)] request_seam: None` in its struct literal (the pattern of `download.rs:775-776`); a test that needs the fixture server sets `env.request_seam = Some(&seam)` in `install_tests.rs`. `fixture_seam_findings` allows `request_seam` in `install.rs` (declaration directly after `#[cfg(test)]`, the `native_env` initialiser and the `for_command` application, each under `#[cfg(test)]`), in `test_support.rs` only as a `#[cfg(test)]`-attributed field initialiser, and in `install_tests.rs`; nowhere else. | `test_support` compiles under `cfg(any(test, feature = "test-support"))` (`crates/core/src/lib.rs:26-27`), and `src-tauri` enables `test-support` as a dev-dependency (`src-tauri/Cargo.toml:34`), so `cargo clippy --workspace --all-targets` builds core with the feature on and `cfg(test)` off; a seam parameter or an unattributed field there fails to compile (E0560), and widening the field's cfg would ship the redirect seam outside tests. | FR-112 (SI-27) |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before tasks)

| Id | Blocking | Issue (evidence) | Proposed spec edit |
|---|---|---|---|
| SI-1 | **yes** | FR-090 (last paragraph) and the SC preamble (`spec.md:742-746`) have test feeds "carry the plugin's `tar.gz` asset under both `linux-x86_64` … and the native platform key". The v2 schema binds format to prefix (`versions.v2.schema.json:136-146`, compiled in at `contract.rs:12`), so on Windows (`windows-x86_64`) and macOS (`darwin-aarch64`) that feed fails `feed::parse` with `E_FEED_INVALID`, and every command-level `cargo.all` SC (SC-001, 007, 008, 009, 014, 021, 022, 027, 030) is red on two legs. | PD-1. FR-004: `CommandEnv` gains `arch: Arch`. FR-090 step 2: `download::command_request_for(env.ctx, name, expected_version, now_unix, host, env.os, env.arch)`; delete "`os` never picks the downloaded asset … native-keyed" and the native-key paragraph; add "the download leg resolves the asset for `(env.os, env.arch)`; production passes the native pair (`install::native_env`, PD-6), so production behaviour is Phase 7's". FR-120: "`download.rs` gains `request_for_platform` and `command_request_for`; `request_for` and `command_request` delegate to them with `NativePlatform.os()` / `Arch::native()`; no other change". SC preamble: Linux rows pass `Os::Linux`, `Arch::X86_64` and a feed with only a `linux-x86_64` asset. |
| SI-2 | **yes** | No route reaches the fixture server for an asset: feed asset URLs are `https://assets.example.invalid/…` (`test_support.rs:352`), a test transport refuses them (`download_tests.rs:2635-2650`), and `with_fixture_url` may appear only in `download.rs` / `download_tests.rs` (`injection.rs:3359-3366`, required name `fixture_url_seam_scanner_reports_plants`). | PD-2. FR-004: `CommandEnv` gains `#[cfg(test)] pub(crate) request_seam`. FR-112: `fixture_seam_findings` allows `with_fixture_url` in `install_tests.rs` too and gains `request_seam` (declared only in `install.rs`, directly after `#[cfg(test)]`, never named in `src-tauri/`), with two plants. |
| SI-3 | **yes** | FR-026 and FR-090 step 5 call `download::discard(clone, …)` inside `next`. `next` runs under `obtain_body`'s in-flight guard (`download.rs:1359`, `:1403`, `:1447`) and `discard` takes the same guard (`:1497-1503`), so every successful action would return `E_DOWNLOAD_BUSY`. | PD-3. FR-026: "`next` returns the action's result and the clone; after `obtain_and` returned, an `Ok` outcome is followed by `download::discard(clone, env.download.io)`; a discard error is logged once (`warn!`) and the action stays `Ok`". FR-090 step 5 the same. |
| SI-4 | **yes** | FR-100 adds `actions` to `PluginView`. `status::tests::golden_views` compares `serde_json::to_string_pretty(&view)` byte for byte with the 13 files of `tests/fixtures/status/` (`status_tests.rs:977-989`), and `view.ts` `object()` refuses a missing or extra key (`view.ts:122-128`); New components lists none of the 13 goldens. Every scenario is built through `assemble` (`status_tests.rs:335-344`), so each card's value is `[]`. | New components: "`tests/fixtures/status/*.json` (13 files): every plugin object gains `"actions": []` as its last key (the field is the last of `PluginView`)"; FR-121 unaffected (these are app fixtures, not `tools/feed-contract/`). |
| SI-5 | no | `download::channel_progress` is typed to `Progress` (`download.rs:1582-1590`). | FR-092: "through `install::channel_progress`, the same adapter for `InstallProgress`" (PD-12). |
| SI-6 | no | FR-090 step 4 and FR-100 need the same `UpdateState`, but it is computed only inside the private `plugin_view` (`status.rs:330-364`), and FR-120 lets `status.rs` gain only FR-100. | FR-120: `status.rs` also gains `pub(crate) fn update_state(entry, detection)` extracted from `plugin_view`, which calls it (PD-13). |
| SI-7 | no | `sync_file` as "open, `sync_all`" fails on Windows (`FlushFileBuffers` needs `GENERIC_WRITE`). | FR-028: "`sync_file` opens the file for writing (no create, no truncate) and calls `sync_all`" (PD-9). |
| SI-8 | no | FR-029 maps `StorageFull` and `PermissionDenied` only; Phases 7 and 8 also map `QuotaExceeded` and `ReadOnlyFilesystem`. | FR-029 per PD-10. |
| SI-9 | no | `Download(Progress)` under `tag = "kind"` double-tags. | FR-092: `Download { event: Progress }` (PD-11). |
| SI-10 | no | `driver_for: &'a dyn Fn(Os) -> Option<&'a dyn InstallDriver>` cannot take `&install::driver_for` (invariant `Output`). | FR-004, FR-090, FR-094, FR-096: `drivers: &'a dyn DriverResolver` (PD-5); SC-016 / SC-027 / SC-029 read "the resolver" for "`driver_for`". |
| SI-11 | no | FR-024 step 3's directories are not in any journal written before them. | FR-024 step 1 / FR-050: the `begun` journal holds every planned sibling, directory and both manifests (PD-7). |
| SI-12 | no | SC-017's Remove total ("up to the entry count") is undefined for a bundle removed whole (FR-122). | FR-092: "`Removing { done, total }`: `total` = 1 (the bundle) + the manifest's `presets` entries; `done` advances after the bundle and after each preset entry". |
| SI-13 | no | FR-010 names no code for a `plugin_root` that exists as a file. | FR-010: `E_INSTALL_COLLISION { path: { root: "plugin", path: "" } }`, zero writes; SC-011 gains the row. |
| SI-14 | no | The test-source guards do not cover the new test files, and two TS guards constrain SC-024's text: `FR088_FILES` (`injection.rs:250-256`) bans `/usr/`, `127.0.0.1:<port>` in listed test files; `status.test.ts`'s `NAVIGATION_NEEDLES` include `"<" + "a"` and `"h" + "ref"` (`status.test.ts:356-363`) over non-test sources. | FR-112: `FR088_FILES` and `PHASE7_TEST_FILES` (`injection.rs:186-191`) gain `install_tests.rs` and `tar_builder_tests.rs`; `roster.rs` `identifier_files()` (`roster.rs:247-271`) gains `crates/core/src/install.rs`. SC-024: the `elsewhere` fixture path is `/opt/vst3/Alpha.vst3`, never under `/usr/`. |
| SI-15 | no | SC-023 needs roots whose `presets().user` holds byte `0xff`, but `roots_at` creates every root (`test_support.rs:125-137`) and APFS refuses non-UTF-8 names. | FR-115 / New components: `test_support::temp_roots_with_user_presets(td, leaf: &OsStr) -> Roots` (spells that root, does not create it), through the one `Roots::new` site. |
| SI-16 | no | FR-063 / FR-064 for missing paths. | PD-17's sentences. |
| SI-17 | no | Whether pre-existing directories are adopted. | FR-040 / D5: PD-18's sentence. |
| SI-18 | no | FR-028 is not mechanical for reads. | FR-112: PD-20's token list; SC-019 gains the plant `fn f(p: &std::path::Path) -> bool { p.exists() }` in `install.rs`. |
| SI-19 | no | SC-020 runs `assemble_with_os` and `for_command` "inside one `catch_unwind`": the first panics in the fold, so the second never runs. | SC-020: two `catch_unwind` blocks in the one test: (a) `assemble_with_os` with an outside configured root → the `with_configured_plugin_root` message; (b) `for_command` over an unfolded context whose `settings.linux_plugin_root` lies outside → the `check_sandboxed` message, zero `InstallIo` calls, 1 feed / 0 asset requests (PD-23); then, after (b), the same plugin's `for_command` over a context whose root is back inside the sandbox (same state dir) is not `E_INSTALL_BUSY` (FR-085 "on unwind"). |
| SI-20 | no | The journal cannot rebuild its own destinations: FR-050 records no `vst3_bundle` / `preset_subdir`, and `recover` has no feed entry (FR-070; `context.rs:50-60`). | FR-050: the journal gains `"vst3_bundle"` and `"preset_subdir": string \| null`; FR-073: "the destinations are recomputed by `Destinations::from_parts` from the journal's `plugin`, `vst3_bundle`, `preset_subdir` (the FR-012 / FR-016 / FR-110 checks of `Destinations::of`); `stem` equals the file stem and `vst3_bundle` minus `.vst3`; both manifests' `vst3_bundle` equal the journal's; any failure is `refused`" (PD-25). SC-013 gains the forged-`preset_subdir` / out-of-`preset_dir` sibling row. |
| SI-21 | no | FR-063 checks only previous-manifest entries, so a placement can follow a symlinked ancestor of `preset_dir` out of the preset root. | FR-063: "the canonical check also runs on every planned placement (bundle, preset files, links, directories, siblings) before the first write"; FR-014: "an existing non-directory (a symlink included) on the chain `preset_root` → `preset_dir` is `E_INSTALL_COLLISION`" (PD-24). SC-011 gains the symlinked-ancestor rows. |
| SI-22 | no | FR-024 step 7 and FR-071's roll-forward rename over whatever is at `dest`, so a user file saved after planning (or after a crash) is overwritten. | FR-050: each sibling records `pre` (`"absent"` or `{"sha256": …}`); FR-024 step 7 / FR-071 roll-forward: "before each sibling rename `dest` is re-read; a pre-state mismatch removes `<dest>.krate-new`, reports `dest` in `kept` and leaves it out of the manifest" (PD-26). SC-005 gains the plant-before-rename row; SC-013 gains `recovery_table_rows`. |
| SI-23 | no | FR-025's downgrade re-check needs a manifest, so a driver-level `Update` replaces a newer manual install with an older artifact. | FR-025: "with no previous manifest, the installed bundle's version (Phase 6's moduleinfo parser with the class `Version` fallback) must be ranked older, else `E_INSTALL_NOT_NEWER`; an unreadable version is `E_INSTALL_STATE_CHANGED { shown: "update", current: "unknown" }`" (PD-27). |
| SI-24 | no | FR-044 does not say what a symlinked `<state>/journal` or `<state>/manifests` does. | FR-044 / FR-093: PD-28's sentence. |
| SI-25 | no | FR-015 creates links verbatim, and C4's containment is relative to the archive layout, not the remapped preset directory. | FR-015: PD-29's `u ≤ k` rule (`E_INSTALL_PATH`) and the placement of a preset link as a `.krate-new` sibling. |
| SI-26 | no | Several FRs have no reaching test (FR-001 batch semantics, FR-025 driver re-check, FR-071's no-crash-cut and `refused` rows, FR-002 / FR-003 / FR-015 refusal codes, FR-014's stray `.krate-old`). | SC-027 gains `install::tests::batch_runs_every_action`; SC-008 gains `install::tests::driver_rechecks_state` and a 13th row (schema-1-shaped entry: `remove_for_command` → `E_INSTALL_NOT_MANAGEABLE`, `for_command` → `E_DOWNLOAD_UNAVAILABLE`); the edge-case table's "a forged call is `E_INSTALL_NOT_MANAGEABLE`" (`spec.md:1020`) is corrected to "a forged Remove is `E_INSTALL_NOT_MANAGEABLE`, a forged Install / Update `E_DOWNLOAD_UNAVAILABLE` (FR-090 step 2 runs first; `versions.v2.schema.json:162`)"; SC-013 gains `install::tests::recovery_table_rows`; SC-011 gains the stray `.krate-old` row; FR-116 totals per §4.9 (`cargo.all` 276). |
| SI-27 | no | FR-112's `request_seam` rule does not say how `test_support.rs` (compiled under the `test-support` feature too) builds a `CommandEnv`. | FR-112: PD-30's allow-list and its plant. |

Not an issue, recorded: the new `E_INSTALL_*` codes do not collide with the feed contract's `E_INSTALL_VALUE` and
`E_INSTALL_DUPLICATE` (`contract.rs:65`, `:106`); no Phase 9 code reuses either. Arity: the widest new functions take
seven arguments (`for_command`, `command_request_for`, `status::build_with_os`, `status::assemble_with_gates`), the
default `too_many_arguments` threshold, which the workspace never lowers or suppresses (`clippy.toml`;
`injection.rs:5172`).

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-11-01 at `abdd2c9e`)

| Fact | Where |
|---|---|
| `MAX_MANIFEST_BYTES` 4 MiB, `MANIFESTS_DIR = "manifests"`; `read_manifest` reads `<state>/manifests/<stem>.json`, codes `E_RECORD_READ` / `_PARSE` / `_FUTURE` / `_MISMATCH`, unknown keys ignored | `apps/krate-manager/crates/core/src/detect.rs:18-22`, `:883-914` |
| `HostFacts { os_version, inno_values, pkg_outputs }` (no `os()`); `Listing::scan(roots, names, limits)`, `copies_of`; Linux managed candidate = `linux_managed_root.join(bundle)` with a `debug_assert!` that it is a plug-in root | `detect.rs:181-185`, `:237`, `:252`, `:436-460` |
| `detect_plugin(os, ctx, entry, host, listing)`: stem = `vst3_bundle` minus `.vst3`; Linux record = `read_manifest`; `linux_managed_root: ctx.settings.linux_plugin_root_or_default(&ctx.roots)` | `detect.rs:976-1066` (`:986-987`, `:1009-1013`, `:1053`) |
| `PluginView` fields (`notes` last); `build` fixes `NativePlatform.os()` and `Arch::native()`; `assemble(os, arch, check, running, detections)`; the managed copy decides `update` via `version::compare` | `crates/core/src/status.rs:56-72`, `:176-210` (`:179`, `:197`), `:213-258`, `:330-364` |
| The 13 goldens are built through `assemble(Os::Windows, …)` and compared byte for byte (`to_string_pretty` + LF) | `status_tests.rs:335-344`, `:977-989` |
| `status::build` writes nothing but the feed cache (a snapshot test) | `detect_tests.rs:1980-2005` |
| `Settings { linux_plugin_root, linux_graphics_workaround }`, `linux_plugin_root_or_default`; `save` → `save_with`: create dir, temp, write, sync, persist, Unix `sync_dir` = `File::open(dir)?.sync_all()` | `settings.rs:20-33`, `:156`, `:268-271`, `:281-303` |
| `PresetRoots { factory, user }`; `with_configured_plugin_root` panics "configured plug-in root … is outside the test sandbox …"; `check_sandboxed` (`pub(crate)`, lexical) panics "install location … is outside the test sandbox …"; accessors; `vst3_user()` | `roots.rs:24-28`, `:118-160` (`:139`), `:175-185`, `:191-213` |
| Linux defaults: `Vst3User` `~/.vst3`, four `Vst3Global`, `presets.factory == presets.user == ~/Documents/Krate Audio`, state under `$XDG_DATA_HOME` or `~/.local/share` | `platform/linux.rs:30-61` |
| `AppContext { roots, settings, feed_url, http, signing_key }`; `assemble_with_os`: lock → `settings::load` → `fold` → `AppState`; `assemble_context` passes `NativePlatform.os()` | `context.rs:50-60`, `:73-120` (`:105-106`) |
| `assemble_cases (b)`: after assembly exactly `state/manager.lock` appears | `context_tests.rs:126-146` |
| One process per state dir (`manager.lock`, exclusive `try_lock`) | `state_lock.rs:53-71` |
| `PluginEntry`, `StableRelease`, `PlatformAsset { …, format, payload }`, `AssetFormat::{InnoExe, Pkg, TarGz}`, `InstallFacts`, `PlainName` (no public constructor) | `feed.rs:44-153` |
| `feed::check` always performs one `http::get` and falls back to the cache | `feed.rs:741-760` |
| Feed schema: `platform.format` enum; `tar.gz` requires `payload`; `^windows-` → `inno-exe`, `^darwin-` → `pkg` + `min_os`, `^linux-` → `tar.gz` + `min_os` | `tools/feed-contract/versions.v2.schema.json:107-147`; compiled in at `contract.rs:12` |
| `test_feed_document`: asset URLs `https://assets.example.invalid/<lower>/v<ver>/<asset>`; format by prefix; `tar.gz` payload `{files: 1, sha256: <asset sha>}`; `install.vst3_bundle = "<Name>.vst3"` | `test_support.rs:347-405` (`:352`, `:360-372`) |
| `DownloadRequest` private fields, `#[cfg(test)] fetch_url`; `with_fixture_url` (`#[cfg(test)] pub(crate)`, loopback http only) | `download.rs:41-108` |
| `DownloadEnv` (four borrows, `Copy`); `DownloadPolicy::production` | `download.rs:180-214` |
| `request_for` → `version::resolve_native`; `live_hashes` resolves native | `download.rs:742-784` |
| `obtain_and`; in-flight guard at step 1; cached hit calls `next`; fresh path calls `next` in the return | `download.rs:1319-1474` (`:1359`, `:1398-1408`, `:1443-1449`) |
| `discard(artifact, io)` takes the in-flight guard (`DiscardBusy`); `command_request`; `run_command`; `for_command`; `channel_progress` typed `Progress` | `download.rs:1480-1590` |
| `Progress` internally tagged `kind`; `DownloadSummary`; `DownloadError` (`Clone`, codes) | `download.rs:227-270`, `:358-460` |
| Phase 7's command test splits `command_request` → `with_fixture_url` → `run_command`; a full `for_command` against https gets `E_DOWNLOAD_URL` | `download_tests.rs:2459-2650` |
| `ExtractLimits::PRODUCTION`, `ExtractEnv { ctx, io, limits }` (`Copy`), `NodeKind`, `ExtractIo` | `extract.rs:190-262` |
| `io_error` mapping (Full / Denied / Collision / Io) | `extract.rs:171-185` |
| `EntryType`, `ListingEntry`, `CanonicalListing { from_entries, lines, text, files, digest }` | `extract.rs:963-1040` |
| `prune(env) -> usize` (creates nothing); `stage(env, entry, &artifact)`; `StagedTree` accessors incl. `file_hashes`; `discard(tree, io)` | `extract.rs:1606-1640`, `:2063-2150` |
| C4 line grammar `<type> <mode> <path>[ -> <target>]`; path rule 1 refuses NUL/CR/LF and ` -> ` in an entry path; layout root `<vst3_bundle>`, `presets`, `README.txt`, `CHANGELOG.txt` | `tools/feed-contract/CONTRACT.md:262-298`, `:311` |
| `install.preset_subdir` one segment; reader rule 8 "re-checks the patterns before it joins a value" | `CONTRACT.md:73`, `:141-142` |
| `VerifiedArtifact` (`Clone`, no public constructor, `format()`); `verify_file` `pub(crate)`; `Expected`; `Sha256Stream` `pub(crate)` | `verify.rs:90-162`, `:62-71`, `:302-316` |
| `UpdateState`, `compare`; `Arch::native`; `platform_candidates` (`(Os::Linux, X86_64)` → `[LinuxX86_64]`, Linux on `Aarch64` → `[]`); `resolve`, `resolve_native` | `version.rs:66-120`, `:200-240` |
| Six commands in `build.rs`, `capabilities/main.json`, `permissions/autogenerated/*.toml`, `generate_handler!`; `download_plugin` shape (`spawn_blocking`, `channel_progress`) | `src-tauri/build.rs:1-12`; `src-tauri/capabilities/main.json`; `src-tauri/src/lib.rs:87-94`; `src-tauri/src/commands.rs:58-100` |
| `src-tauri` lib and bin have `test = false`; core dev-dependency with `test-support` | `src-tauri/Cargo.toml:10-36` |
| `HANDLER_LIST`; capability list; `build.rs` list; planted handler removal | `crates/core/tests/config_guard.rs:152`, `:640-692`, `:1993-2005` |
| `FR007_DEPENDENCIES` (14); `PHASE7_TEST_FILES`; `FR084_TOKENS`; `FR088_FILES` / `FR088_PATTERNS`; `EXTRACT_FS_TOKENS` (18), `EXTRACT_IO_METHODS`, `EXTRACT_BANNED`, `TEMPFILE_REAL_TEMP`; `TEST_PROGRAM_BODIES` | `injection.rs:106-120`, `:186-198`, `:224-244`, `:250-269`, `:325-370`, `:447` |
| `is_test_source`; static-lock rule covers test sources and `fixture_server.rs` only | `injection.rs:1504-1511`, `:1985-2030` |
| `verify_file` callers; `verify_confinement_findings` (`sha2` only in `verify.rs`, `test_signer` declaration) | `injection.rs:2642-2655`, `:2909-2948` |
| `extract_findings` (impl-body confinement, `io_method_site`, banned lists) | `injection.rs:2990-3110` |
| `fixture_seam_findings` (`with_fixture_url`, `fetch_url`) | `injection.rs:3323-3377`, tests `:5264-5306` |
| Roster scans every `crates/core/src` file; `identifier_files()` is a fixed list | `crates/core/tests/roster.rs:101-127`, `:247-271` |
| `fresh_temp_roots`, `roots_at` (one `#[expect(clippy::disallowed_methods)]` `Roots::new` site), `FakeHostFacts`, `kat_signing_key`, `native_key`, `TestPlugin`, `test_feed`, `DownloadRig`, `MemExtractIo` (`node`, `plant_*`), `RecordingExtractIo`, `snapshot` | `test_support.rs:86-180`, `:245-332`, `:335-409`, `:581-618`, `:1095-1176`, `:1562-1660`, `:1749` |
| Archive helpers to move: `sha` `:106`, `unhex` `:112`, `literal_payload` `:131`, `gz` `:154`, `gunzip` `:160`, `moduleinfo` `:168` (hard-codes `"Alpha"`), `octal_field` `:177`, `reseal` `:185`, `struct Tar` + `impl Tar` `:195-271`; `signed` helper `:464-469` | `extract_tests.rs` |
| `TestSigner::{new, key, sign, sign_c3}`, `c3_comment` | `test_signer.rs:16-76` |
| `Invoker`; `STATUS_TEXTS` / `ACTION_TEXTS`; `releaseButton` pattern; `renderCard`; `createStatusSection`; `mountStatus` | `src/status.ts:9-62`, `:87`, `:177-205`, `:207-247`, `:267-331` |
| `PluginView` TS type and the exact-key `object()` check | `src/view.ts:69-84`, `:122-128` |
| vitest guards: "every status text is rendered by a scenario"; navigation needles; non-test sources list | `src/status.test.ts:260-269`, `:356-383` |
| `Channel` constructor uses `transformCallback`; `mockIPC` installs one | `node_modules/@tauri-apps/api/core.js:116-125`; `mocks.js:133-164` (2.12.4) |
| `required-tests.json`: `cargo.all` 248, `windows` 11, `macos` 35, `linux` 30, `vitest` 29 | `node -e` this session |
| `manager-ci.yml`: `cargo test --workspace --locked \| tee`; `required tests ran` requires each name exactly once (`/^test (\S+)(?: - should panic)? \.\.\. ok$/`) | `.github/workflows/manager-ci.yml:117-120`, `:153-178` |
| `EXPECTED_REQUIRED` deep-compared with `required-tests.json` | `tools/feed-contract/tests/manager-ci-shape.test.js:337`, `:853` |
| User preset dir `~/Documents/Krate Audio/<pluginName>`; Linux factory `/usr/share/krate-audio/<lower>` | `plugins/shared/src/platform/preset_paths.cpp:8-51` |
| Scan user dir then factory dir; a user preset saves into `<userDir>/<subcategory>` | `plugins/shared/src/preset/preset_manager.cpp:112-121`, `:316-330` |
| Membrum `pluginName = "Membrum/Kits"`; `"preset_subdir": "Kits"`; README `cp -r presets/* ~/Documents/Krate\ Audio/Membrum/Kits/` | `plugins/membrum/src/preset/membrum_preset_config.h:24`; `plugins/membrum/version.json:8`; `plugins/membrum/installers/linux/README.txt:29-40` |
| Linux release staging: `<Name>.vst3`, `presets`, `README.txt`, `CHANGELOG.txt`, `tar -czvf` | `.github/workflows/release.yml:416-436` |
| Workspace pins and lints (`allow_attributes*` denied; release `panic = "abort"`); core `[dependencies]` 14 keys, `[dev-dependencies] minisign` | `apps/krate-manager/Cargo.toml:1-66`; `crates/core/Cargo.toml:14-33` |

### 1.2 External facts (verified 2026-11-01)

| Fact | Source |
|---|---|
| `std::fs::rename`: "replacing the original file if `to` already exists"; "On Unix, when `from` is a directory and `to` exists, `to` must be an empty directory"; "On Windows 10 version 1607 and above, the behavior is the same as Unix if the filesystem supports `FileRenameInfoEx`. Otherwise on Windows, `from` can be anything but `to` must not be a directory." Stable 1.0.0 | https://doc.rust-lang.org/std/fs/fn.rename.html (std 1.99.1, 2026-10-13) |
| `std::fs::remove_dir_all`: "does **not** follow symbolic links and it will simply remove the symbolic link itself"; TOCTOU-protected except Miri, QNX, Redox, VxWorks; "will fail if the `path` is _not_ a directory"; "not idempotent" | https://doc.rust-lang.org/std/fs/fn.remove_dir_all.html (std 1.99.1) |
| `OpenOptions::create_new`: "No file is allowed to exist at the target location, also no (dangling) symlink"; "atomic"; needs write access. Stable 1.9.0 | https://doc.rust-lang.org/std/fs/struct.OpenOptions.html#method.create_new (std 1.99.1) |
| `File::sync_all`: "Attempts to sync all OS-internal file content and metadata to disk". Stable 1.0.0 | https://doc.rust-lang.org/std/fs/struct.File.html#method.sync_all (std 1.99.1) |
| `FlushFileBuffers`: "The file handle must have the GENERIC_WRITE access right" | https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers |
| `io::ErrorKind::StorageFull` 1.83.0, `QuotaExceeded` 1.85.0, `ReadOnlyFilesystem` 1.83.0, `DirectoryNotEmpty` 1.83.0, `CrossesDevices` 1.85.0 | https://doc.rust-lang.org/std/io/enum.ErrorKind.html (std 1.99.1) |
| `rename(2)` (atomic replace; `EXDEV`; directory target must be empty), `unlink(2)` (an open file survives its last name) | as cited by the spec (`spec.md:153-154`, Linux man-pages 6.19) |
| Tauri channels: Rust `tauri::ipc::Channel<T>` command argument, `on_event.send(…)`, the example type derives `Clone` + `Serialize`; JS `import { invoke, Channel } from '@tauri-apps/api/core'`, `new Channel<T>()`, `onmessage`, passed under the parameter's camelCase name; "Channels are designed to be fast and deliver ordered data" | https://v2.tauri.app/develop/calling-frontend/ (Tauri 2; workspace `tauri` 2.12.5, `@tauri-apps/api` 2.12.4) |
| Toolchain `rustc 1.99.1`, `rust-toolchain.toml` channel `stable`; workspace `rust-version = "1.90"`, edition 2024 | `apps/krate-manager/rust-toolchain.toml`; `Cargo.toml:6-11`; Phase 8 plan §1.2 |

---

## 2. File layout

| File | Change |
|---|---|
| `apps/krate-manager/crates/core/src/install.rs` | 🆕 the whole module (§4.1) |
| `crates/core/src/install_tests.rs` | 🆕 `#[cfg(test)] #[path = "install_tests.rs"] mod tests;` at the end of `install.rs`; every `install::tests::*` SC (§7) |
| `crates/core/src/tar_builder_tests.rs` | 🆕 the FR-114 moved set, each item `pub(crate)`, bodies unchanged; declared in `lib.rs` as `#[cfg(test)] mod tar_builder_tests;` |
| `crates/core/src/extract_tests.rs` | 🔶 the moved items deleted; `use crate::tar_builder_tests::{sha, unhex, literal_payload, gz, gunzip, moduleinfo, octal_field, reseal, Tar};` |
| `crates/core/src/lib.rs` | 🔶 `pub mod install;` (after `pub mod http;`), `#[cfg(test)] mod tar_builder_tests;` (after `mod test_signer;`) |
| `crates/core/src/download.rs` | 🔶 `request_for_platform`, `command_request_for`; `request_for` / `command_request` delegate (PD-1) |
| `crates/core/src/detect.rs` | 🔶 `pub(crate) fn parse_manifest` extracted from `read_manifest` (PD-14) |
| `crates/core/src/status.rs` | 🔶 `ActionView`, `PluginView.actions` (last field), `build_with_os`, `assemble_with_gates`, `update_state` (FR-100, PD-13, PD-15) |
| `crates/core/src/context.rs` | 🔶 the recovery call in `assemble_with_os` (FR-080) |
| `crates/core/src/test_support.rs` | 🔶 §4.5 |
| `crates/core/src/status_tests.rs`, `context_tests.rs` | 🔶 `actions_per_state`, `assemble_runs_install_recovery` |
| `crates/core/tests/injection.rs`, `config_guard.rs`, `roster.rs` | 🔶 §4.8 |
| `apps/krate-manager/tests/fixtures/status/*.json` (13) | 🔶 `"actions": []` per plugin, last key (SI-4) |
| `src-tauri/src/commands.rs`, `src-tauri/src/lib.rs`, `src-tauri/build.rs`, `src-tauri/capabilities/main.json` | 🔶 two commands (§4.6) |
| `src-tauri/permissions/autogenerated/install_plugin.toml`, `remove_plugin.toml` | 🆕 generated by `tauri-build` on the first build (the six existing files are its output), committed |
| `apps/krate-manager/src/status.ts`, `src/view.ts`, `src/main.ts`, `src/status.test.ts` | 🔶 §4.7 |
| `apps/krate-manager/tests/required-tests.json`, `tools/feed-contract/tests/manager-ci-shape.test.js` | 🔶 §4.9 |
| `specs/Krate-manager-roadmap.md` | 🔶 the three FR-123 sentences (§4.10), by the build stage |

No change to `plugins/`, `dsp/`, `cmake/`, `extern/`, any workflow, any `Cargo.toml`, either lock, `package.json`,
`tools/feed-contract/` contracts, schemas, `listing.js` or fixtures (FR-111, FR-121).

---

## 3. Error model

```rust
/// The root a relative path belongs to (FR-040, FR-091).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RootTag { Plugin, Presets }

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct KeptPath { pub root: RootTag, pub path: String }        // relative POSIX path only

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Step {                                                     // FR-024 / FR-061 step names
    JournalBegun, BundleStaged, PresetsStaged, JournalStaged, OldAside, NewIn, PresetsIn, ManifestWritten,
    StaleRemoved, JournalDone, OldDeleted, JournalRemoved, RemoveBegun, RemoveEntries, ManifestRemoved,
}
impl Step { pub fn as_str(self) -> &'static str { /* "journal_begun", …, "manifest_removed" */ } }

#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum InstallError {
    #[error("E_INSTALL_ACTION: {given:?} is not install or update")]            Action { given: String },
    #[error("E_INSTALL_NO_DRIVER: no install driver on this system")]           NoDriver,
    #[error("E_INSTALL_BUSY: {name} has an action running")]                    Busy { name: String },
    #[error("E_INSTALL_NO_FEED: {cause}")]                                      NoFeed { cause: &'static str },
    #[error("E_INSTALL_UNKNOWN_PLUGIN: the feed has no plugin {name}")]         UnknownPlugin { name: String },
    #[error("E_INSTALL_NOT_MANAGEABLE: {name} is not available through the manager")] NotManageable { name: String },
    #[error("E_INSTALL_STATE_CHANGED: shown {shown}, now {current}")]           StateChanged { shown: &'static str, current: &'static str },
    #[error("E_INSTALL_NOT_NEWER: installed {installed}, offered {offered}")]   NotNewer { installed: String, offered: String },
    #[error("E_INSTALL_NOT_MANAGED: {name} was not installed by the manager")]  NotManaged { name: String },
    #[error("E_INSTALL_JOURNAL_PENDING: {name} has an unfinished install record")] JournalPending { name: String },
    #[error("E_INSTALL_JOURNAL: {reason}")]                                     Journal { reason: &'static str },
    #[error("E_INSTALL_MANIFEST: {reason}")]                                    Manifest { reason: &'static str },
    #[error("E_INSTALL_ROOT_CHANGED: installed under another {which} root; set the Linux plug-in root back to the one it was installed under")]
                                                                                RootChanged { which: &'static str },
    #[error("E_INSTALL_OUTSIDE_ROOT: {path:?} resolves outside its root")]      OutsideRoot { path: KeptPath },
    #[error("E_INSTALL_PATH: {reason}")]                                        Path { reason: &'static str },
    #[error("E_INSTALL_PATH_NOT_UTF8: a {which} root is not UTF-8")]           PathNotUtf8 { which: &'static str },
    #[error("E_INSTALL_COLLISION: {path:?} is in the way")]                     Collision { path: KeptPath },
    #[error("E_INSTALL_UNSUPPORTED: this filesystem cannot hold a symlink")]    Unsupported,
    #[error("E_INSTALL_FORMAT: the artifact is not a tar.gz")]                  Format,
    #[error("E_INSTALL_CHANGED: {path:?} changed after staging")]               Changed { path: KeptPath },
    #[error("E_INSTALL_FULL: step {}", .step.as_str())]                         Full { step: Step },
    #[error("E_INSTALL_DENIED: step {}", .step.as_str())]                       Denied { step: Step },
    #[error("E_INSTALL_IO: step {}: {kind:?}", .step.as_str())]                Io { step: Step, kind: io::ErrorKind },
    #[error("{0}")]                                                             Record(&'static str),       // E_RECORD_* (FR-041)
    #[error("{0}")]                                                             Download(Box<DownloadError>),
    #[error("{0}")]                                                             Extract(ExtractError),
}
impl InstallError { pub fn code(&self) -> &'static str { /* own code, or the inner code for the three wrappers */ } }
fn io_err(step: Step, e: &io::Error) -> InstallError { /* PD-10 */ }
```

- Every `Display` begins with its code (FR-029); tests assert `code()` and the structured fields.
- `KeptPath` fields are the root kind and a relative path; `StateChanged.current` is the `UpdateView` spelling
  (`"not_installed"`, `"update_available"`, `"unknown"`); no variant holds an absolute path (FR-091, FR-117).
- `Download` is boxed so `Result<_, InstallError>` stays under clippy's `result_large_err` default of 128 bytes
  whatever `DownloadError`'s size; `ExtractError` is ≈ 56 bytes (Phase 8 plan §3) and stays inline.
- `NoFeed.cause` is `FeedError::code()` (`feed.rs:663`) of the failed check.

---

## 4. Module design

### 4.1 `install.rs`

#### 4.1.1 Constants and public types

```rust
pub const LOG_TARGET: &str = "krate_manager_core::install";
pub const JOURNAL_DIR: &str = "journal";
pub const NEW_SUFFIX: &str = ".krate-new";
pub const OLD_SUFFIX: &str = ".krate-old";
pub const MAX_JOURNAL_BYTES: u64 = 2 * detect::MAX_MANIFEST_BYTES + (64 << 10);   // PD-21

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)] #[serde(rename_all = "snake_case")]
pub enum ActionKind { Install, Update, Remove }

/// No variant can be built without a `VerifiedArtifact`, which only verification makes (SC-007's compile-fail doctest
/// forges `Action::Install { entry, artifact: VerifiedArtifact { .. } }`).
#[derive(Debug)]
pub enum Action {
    Install { entry: PluginEntry, artifact: VerifiedArtifact },
    Update { entry: PluginEntry, artifact: VerifiedArtifact },
    Uninstall { entry: PluginEntry },
}
impl Action { pub fn kind(&self) -> ActionKind; pub fn entry(&self) -> &PluginEntry }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Outcome { pub version: Option<String>, pub placed: u64, pub removed: u64, pub kept: Vec<KeptPath> }
#[derive(Debug)]
pub struct ActionResult { pub name: String, pub action: ActionKind, pub outcome: Result<Outcome, InstallError> }

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)] #[serde(rename_all = "snake_case")]
pub enum Notice { RescanDaw }
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct ActionSummary { pub name: String, pub action: ActionKind, pub version: Option<String>,
                           pub kept: Vec<KeptPath>, pub notice: Option<Notice> }

#[derive(Clone, Debug, PartialEq, Eq, Serialize)] #[serde(tag = "kind", rename_all = "snake_case")]
pub enum InstallProgress { Download { event: Progress }, Staging, Placing { done: u64, total: u64 }, Committing,
                           Removing { done: u64, total: u64 }, Done }

pub trait InstallDriver: Sync {
    fn os(&self) -> Os;
    fn run(&self, env: InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult>;
}
pub trait DriverResolver { fn driver_for(&self, os: Os) -> Option<&dyn InstallDriver>; }   // PD-5
pub struct NativeDrivers;                                    // impl: install::driver_for(os)
pub struct LinuxDriver;                                      // impl InstallDriver, os() == Os::Linux
static LINUX: LinuxDriver = LinuxDriver;
pub fn driver_for(os: Os) -> Option<&'static dyn InstallDriver> { matches!(os, Os::Linux).then_some(&LINUX) }

pub struct InstallEnv<'a> {
    pub ctx: &'a AppContext, pub io: &'a dyn InstallIo, pub extract: ExtractEnv<'a>,
    pub progress: &'a mut dyn FnMut(InstallProgress),
}
pub struct CommandEnv<'a> {
    pub os: Os, pub arch: Arch, pub ctx: &'a AppContext, pub io: &'a dyn InstallIo,
    pub extract: ExtractEnv<'a>, pub download: DownloadEnv<'a>, pub drivers: &'a dyn DriverResolver,
    #[cfg(test)] pub(crate) request_seam: Option<&'a dyn Fn(DownloadRequest) -> DownloadRequest>,   // PD-2
}
pub fn native_env<'a>(ctx: &'a AppContext, io: &'a dyn InstallIo, extract: ExtractEnv<'a>,
                      download: DownloadEnv<'a>) -> CommandEnv<'a>;                              // PD-6
pub fn channel_progress<E>(send: impl FnMut(InstallProgress) -> Result<(), E>) -> impl FnMut(InstallProgress); // PD-12
```

`LinuxDriver::run` loops over the batch in order, calls `run_one(env, action)` (fresh `&mut` reborrow of the progress
per action) and pushes one `ActionResult`; a failure never stops the next (FR-001, D10). The driver logic is
OS-neutral; only `driver_for` decides where it is offered (FR-004).

#### 4.1.2 `InstallIo` and `FsInstallIo` (FR-028)

```rust
pub trait InstallIo: Sync {
    // read
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>>;
    fn read_link(&self, path: &Path) -> io::Result<PathBuf>;
    fn hash_file(&self, path: &Path) -> io::Result<[u8; 32]>;
    fn read_dir(&self, dir: &Path) -> io::Result<Vec<(OsString, NodeKind)>>;
    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>>;
    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf>;
    fn supports_symlinks(&self) -> bool;
    // mutating
    fn create_dir_all(&self, path: &Path) -> io::Result<()>;
    fn create_dir(&self, path: &Path) -> io::Result<()>;
    fn copy_new(&self, src: &Path, dest: &Path, sink: &mut dyn FnMut(&[u8])) -> io::Result<u64>;
    fn write_new(&self, path: &Path, bytes: &[u8]) -> io::Result<()>;
    fn symlink(&self, target: &str, link: &Path) -> io::Result<()>;
    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()>;
    fn sync_file(&self, path: &Path) -> io::Result<()>;
    fn sync_dir(&self, dir: &Path) -> io::Result<()>;
    fn rename(&self, from: &Path, to: &Path) -> io::Result<()>;
    fn remove_file(&self, path: &Path) -> io::Result<()>;
    fn remove_dir(&self, path: &Path) -> io::Result<()>;
    fn remove_tree(&self, path: &Path) -> io::Result<()>;
}
pub struct FsInstallIo;
```

`FsInstallIo` bodies (`std::fs` only here, PD-20): `kind_of` = `symlink_metadata` (`NotFound` → `None`); `hash_file`
refuses a non-regular file (`symlink_metadata` first, `InvalidInput`), then streams through `verify::Sha256Stream` in
64 KiB chunks; `read_dir` sorted by name, kind from the entry's non-following `file_type()`; `read_file` reads at most
`max + 1` bytes; `create_dir_all` then `symlink_metadata(path).is_dir()` (else `InvalidInput`, the Phase 7
`FsStagingIo` rule, `download.rs:578-585`); `copy_new` opens `src` read-only, `dest` with `OpenOptions::new().write(true)
.create_new(true)`, copies in 64 KiB chunks handing each to `sink` before `write_all`; `write_new` the same with
`bytes`; `symlink` = `std::os::unix::fs::symlink` under `cfg(unix)`, `Unsupported` elsewhere; `set_mode` =
`set_permissions(PermissionsExt::from_mode(mode))` under `cfg(unix)`, no-op elsewhere; `sync_file` per PD-9;
`sync_dir` = `File::open(dir)?.sync_all()` under `cfg(unix)`, `Ok(())` elsewhere (the caller never calls it off Unix,
FR-023); `rename` = `fs::rename`; `remove_file` = `fs::remove_file` (no Windows link fallback, PD-19);
`remove_dir` = `fs::remove_dir`; `remove_tree` per PD-19; `supports_symlinks` = `cfg!(unix)`. No owner, group,
timestamp or extended attribute is set anywhere (FR-015).

#### 4.1.3 Destinations, `dest_path` and the listing reader (FR-010 to FR-016)

```rust
pub(crate) struct Destinations { plugin_root: PathBuf, target: PathBuf, new: PathBuf, old: PathBuf,
                                 preset_root: PathBuf, preset_dir: PathBuf, stem: String }
impl Destinations {
    pub(crate) fn of(ctx: &AppContext, entry: &PluginEntry) -> Result<Destinations, InstallError>;   // PD-25
    pub(crate) fn from_parts(ctx: &AppContext, name: &str, vst3_bundle: &str, preset_subdir: Option<&str>)
        -> Result<Destinations, InstallError>;
}
pub(crate) fn dest_path(root: &Path, rel: &str) -> Result<PathBuf, InstallError>;
fn one_segment(s: &str) -> bool;                     // one Component::Normal, no '/', '\\', ':', NUL, not "." / ".."
struct Line<'a> { kind: char, mode: u32, path: &'a str, target: Option<&'a str> }
fn parse_line(line: &str) -> Result<Line<'_>, InstallError>;   // `<t> <4 octal> <path>[ -> <target>]`
```

`Destinations::of` (pure, PD-23): facts from `entry.release.install` (`NotManageable` when absent, FR-003), then
`from_parts` (PD-25), which does everything below; `recover` calls `from_parts` with the journal's strings, so a
journal read from disk passes the same checks as a feed entry. `<Name>`,
`vst3_bundle`, `preset_subdir` each through `one_segment` (FR-012, `Path` otherwise); `stem` = `vst3_bundle` minus
`.vst3` (a `vst3_bundle` without that suffix is `Path`); `plugin_root =
ctx.settings.linux_plugin_root_or_default(&ctx.roots)`; `target = plugin_root/<vst3_bundle>`, `new` / `old` = target
name + `NEW_SUFFIX` / `OLD_SUFFIX` in the same directory (so every rename stays on one filesystem, `EXDEV`);
`preset_root = ctx.roots.presets().user`, `preset_dir = preset_root/<Name>[/<preset_subdir>]`; `to_str()` on
`plugin_root`, `preset_root`, `preset_dir` (`PathNotUtf8 { which }`, FR-016); `ctx.roots.check_sandboxed` on every
path it returns (FR-110). `dest_path` splits on `/`, requires each segment `one_segment` and the result a strict
descendant of `root` (`Path` otherwise); it is the only join of an archive or manifest string (FR-012). `parse_line`
splits the fixed `"<t> <mode> "` prefix (7 bytes) and, for `l`, at the one ` -> ` (C4 refuses ` -> ` in entry paths,
`CONTRACT.md:311`, so the split is unambiguous).

#### 4.1.4 The manifest (FR-040 to FR-044, PD-14, PD-18, PD-21)

```rust
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Manifest {
    pub manifest_version: u64, pub plugin: String, pub version: String, pub vst3_bundle: String,
    pub archive_sha256: String, pub plugin_root: String, pub preset_root: String, pub files: Vec<ManifestEntry>,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ManifestEntry {
    pub root: RootTag, #[serde(rename = "type")] pub kind: EntryTag, pub path: String,
    #[serde(skip_serializing_if = "Option::is_none", default)] pub sha256: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none", default)] pub target: Option<String>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum EntryTag { #[serde(rename = "f")] F, #[serde(rename = "d")] D, #[serde(rename = "l")] L }
pub fn read_manifest_full(io: &dyn InstallIo, state_dir: &Path, stem: &str, plugin: &str)
    -> Result<Option<Manifest>, InstallError>;
```

`read_manifest_full`: `kind_of(<state>/manifests)` → `None` → `Ok(None)`, `Some(Dir)` → go on, any other kind →
`Manifest { reason: "state directory is not a directory" }` (PD-28);
`io.read_file(<state>/manifests/<stem>.json, MAX_MANIFEST_BYTES)` (`NotFound` → `Ok(None)`; any
other error or more than the bound → `Record(E_RECORD_READ)`); `detect::parse_manifest(bytes, plugin)` (`Failed(code)`
→ `Record(code)`); then `serde_json::from_slice::<Manifest>` (unknown keys ignored, Phase 6 FR-028) and the Phase 9
checks: every `path` through `dest_path` against a dummy root (segment rules), roots absolute + UTF-8 + normal,
`sha256` / `archive_sha256` 64 lower-case hex, `f` has `sha256`, `l` has `target`, `d` has neither, `vst3_bundle`
equal to the entry's; any failure `Manifest { reason }` (FR-041). Bytes are written by `serde_json::to_vec_pretty`
plus LF (key order = field order, so the three Phase 6 keys come first). The bundle's entries are its whole staged
listing under `<vst3_bundle>/` (the `vst3_bundle` line itself included, path relative to `plugin_root`); preset
entries per D5 and PD-18.

#### 4.1.5 Atomic write (FR-044)

`fn write_atomic(io, dir: &Path, name: &str, bytes: &[u8], step: Step) -> Result<(), InstallError>`: `kind_of(dir)` →
`None` → `create_dir(dir)` (the state dir exists: the lock created it, `state_lock.rs:58-60`); `Some(Dir)` → go on;
any other kind (a symlink included) → `Journal` / `Manifest { reason: "state directory is not a directory" }` (by
`dir`'s name) before any write (PD-28); temp =
`dir/.<name>.tmp`; `kind_of(temp)` is a file → `remove_file(temp)`; `write_new(temp, bytes)`; `sync_file(temp)`;
`rename(temp, dir/<name>)`; on Unix `sync_dir(dir)`. Each `io::Error` → `io_err(step, …)`. On an error after
`write_new` the temp is removed best-effort (`remove_file`, its error only logged) so the in-process paths leave no
`.*.json.tmp`; a crash leftover is removed by `recover` (FR-070).

#### 4.1.6 The journal (FR-050 to FR-052, PD-7)

```rust
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct Journal {
    journal_version: u64, op: JournalOp, plugin: String, stem: String, vst3_bundle: String,
    preset_subdir: Option<String>, step: JournalStep,                                        // PD-25
    target: String, new: String, old: String, had_old: bool, siblings: Vec<Sibling>,
    created_dirs: Vec<String>, pending_manifest: Option<Manifest>, previous_manifest: Option<Manifest>,
}
#[serde(rename_all = "snake_case")] enum JournalOp { Install, Update, Remove }
#[serde(rename_all = "snake_case")] enum JournalStep { Begun, Staged, Done }
struct Sibling { dest: String, new: String, pre: SiblingPre }                                 // PD-26
#[serde(rename_all = "snake_case")] enum SiblingPre { Absent, Sha256(String) }   // "absent" | {"sha256": "<64 hex>"}
```

Written only through `write_atomic` into `<state>/journal/<stem>.json` (FR-043). `created_dirs` in creation order
(parents first). `check_journal(j, file_stem, dest)` (FR-051, FR-073), where `dest =
Destinations::from_parts(ctx, &j.plugin, &j.vst3_bundle, j.preset_subdir.as_deref())` (PD-25: `one_segment` on all
three strings, UTF-8, `check_sandboxed`; its error refuses the journal): `j.stem` equals `file_stem` and `dest.stem`;
`pending_manifest` / `previous_manifest`, when present, have `vst3_bundle == j.vst3_bundle` and `plugin == j.plugin`;
`target` equals `dest.target`, `new` / `old` equal `dest.new` / `dest.old`; every sibling `dest` equals
`dest_path(preset_root, rel)` for some rel under `dest.preset_dir`, `new == dest + NEW_SUFFIX`, and a `Sha256` pre is
64 lower-case hex; every created dir lies on or under `dest.preset_dir`'s chain strictly below `preset_root`; then
FR-063's canonical check on every parent (PD-17). A failing journal is `refused`, nothing touched.

#### 4.1.7 The planner (D4, D5, FR-014, FR-020, FR-021, FR-025, FR-063)

```rust
struct InstallPlan {
    kind: ActionKind, dest: Destinations, had_old: bool,
    bundle: Vec<Copy>,                 // listing order: Dir / File { src, sha256, mode } / Link { target }
    presets: Vec<PresetOp>,            // Place | Replace (copy to <dest>.krate-new) | Adopt | Kept
    create_dirs: Vec<PathBuf>,         // FR-011 chain + new preset subdirectories, parents first
    stale: Vec<StaleOp>,               // Remove | Kept, deepest first
    stale_dirs: Vec<PathBuf>,          // previous-manifest dirs absent from the new layout, deepest first
    pending: Manifest, kept: Vec<KeptPath>, placed_files: u64,
}
fn plan(io: &dyn InstallIo, dest: &Destinations, tree: &StagedTree, entry: &PluginEntry,
        kind: ActionKind, previous: Option<&Manifest>) -> Result<InstallPlan, InstallError>;
```

Read-only (`kind_of`, `hash_file`, `read_dir`, `read_file`, `canonicalize` only). In order, first failure wins, zero
writes:
0. State subdirectories (PD-28): `kind_of(<state>/journal)` and `kind_of(<state>/manifests)` are `None` or
   `Some(Dir)`, else `Journal` / `Manifest { reason: "state directory is not a directory" }`, so a symlinked
   `manifests/` is refused here rather than at step 8, after the commit point.
1. `plugin_root`: `kind_of` → `None` (planned `create_dir_all`), `Dir`, or `Symlink` whose canonical target is a
   directory (SC-028); anything else `Collision { plugin, "" }` (SI-13).
2. Target: `kind_of(target)`: `Some(Dir)` → `had_old`; `None` → not; any other kind → `Collision` (FR-014). FR-025's
   structural re-check here: `Install` with a present target → `StateChanged { "install", "installed" }`; `Update`
   with an absent one → `StateChanged { "update", "not_installed" }`; `Update` whose `previous.version` is not ranked
   older than the staged version by `version::compare` → `NotNewer`; `Update` with a present target and `previous ==
   None` (a manual install, FR-021): the target's version through PD-27 (`read_file` of its `moduleinfo.json`,
   `detect::moduleinfo_version`, class-`Version` fallback) must be ranked older, else `NotNewer { installed, offered }`;
   an unreadable or `Unknown` version → `StateChanged { "update", "unknown" }`.
3. `new` and `old` must be absent (a journal for this plugin would have blocked the action already, FR-052):
   otherwise `Collision` (FR-014).
4. `supports_symlinks()` false and the listing holds an `l` line → `Unsupported` (FR-015).
5. `previous` resolution (FR-063, PD-17): every entry through `dest_path` and the canonical check; failure →
   `OutsideRoot`.
5a. Link targets (PD-29): every `l` line `<T>/<s1>/…/<sk>/<name> -> <target>` has at most `k` leading `..` segments
   (its lexical resolution never reaches the archive root, so mapping `<T>` to its destination cannot change where it
   points), else `Path { reason: "link target leaves its top-level entry" }`.
5b. Placement containment (PD-24): (a) every existing node on the chain `preset_root` (exclusive) → `preset_dir`
   (inclusive) is `Some(Dir)` (a `Symlink` or any other kind → `Collision { presets, <its path relative to
   preset_root> }`); (b) after steps 6-8 have computed the destinations, every planned destination (bundle node under
   `new`, preset file, preset link, preset directory, `.krate-new` sibling) has the deepest existing ancestor of its
   parent canonicalized and required equal to or under `canonicalize(plugin_root)` (bundle side) or
   `canonicalize(preset_root)` (preset side), else `OutsideRoot { path }`; a root that does not exist makes (b) vacuous
   for its side. (b) runs before step 10, still before any write.
6. Bundle lines: every listing line whose path is `<vst3_bundle>` or starts with `<vst3_bundle>/`, mapped into `new`
   (`dest_path(new_parent, path)` with the first segment renamed to `<vst3_bundle>.krate-new`); file hashes from
   `tree.file_hashes()` (a missing hash is `Path`, impossible for a Phase 8 tree).
7. Preset lines: every line under `presets/`, `presets/<rel>` → `dest_path(preset_dir, rel)`; the `presets` line →
   `preset_dir`. Directories: absent → planned create (recorded, PD-18); present real directory → nothing to do
   (recorded only if `previous` recorded it); any other kind → `Collision` (FR-014). Files, per D5: absent → `Place`;
   `<dest>.krate-new` present → `Collision`; present regular file listed in `previous` with an equal current hash →
   `Replace`; present with content equal to the new file (`hash_file == file_hashes`) → `Adopt`; anything else
   (edited, user file, link, directory) → `Kept` (not recorded, reported). Links (none in today's preset trees, C4
   allows them): absent → `Place`; present link with the same target → `Adopt`; else `Kept`. Every `Place` and
   `Replace` records its journal `pre` (PD-26): `Absent` for `Place`, `Sha256(<previous-manifest hash>)` for `Replace`.
8. FR-011 chain: the missing directories between `preset_root` (exclusive) and `preset_dir` (inclusive) are planned
   creates; `preset_root` itself absent → planned `create_dir_all`, never recorded.
9. Stale: every `previous` `presets` file entry not in the new set → `Remove` when its current kind is a regular file
   with the recorded hash, `Kept` otherwise; a missing one is already gone; a stale link likewise by target. Stale
   directories: every recorded directory the new layout lacks, removed later only when empty.
10. The pending manifest (`version = entry.version_text`, `archive_sha256 = hex(tree.archive_sha256())`, the two
    roots as UTF-8 strings), refused when its encoding exceeds `MAX_MANIFEST_BYTES` (PD-21).

An existing bundle without a manifest (a manual README install) plans like any update: the bundle is swapped, presets
adopt or keep by content (FR-021, SC-005).

#### 4.1.8 Executing an install or update (FR-022 to FR-024, FR-027)

`run_install(env, entry, artifact, kind) -> Result<Outcome, InstallError>`:
0. `artifact.format() != TarGz` → `Format` (before `stage`, FR-002); `Destinations::of`; `(env.progress)(Staging)`;
   `extract::stage(env.extract, &entry, &artifact)` → `Extract(e)`; `read_manifest_full` → `previous`; `plan(…)`.
   The tree is always discarded at the end (`extract::discard(tree, env.extract.io)`, error logged).
1. `journal_begun`: `write_atomic(journal/<stem>.json, Journal { step: Begun, … the whole plan … })`.
2. `bundle_staged`: for each bundle `Copy` in listing order: `Dir` → `create_dir`; `File` → `copy_new(src, dst, sink)`
   with `sink` feeding a `Sha256Stream`, then compare with the staged hash (`Changed { path }` on a difference,
   FR-022), `set_mode(0o755 or 0o644)`, `sync_file`, `(progress)(Placing { done, total })`; `Link` → `symlink`. Then on
   Unix `sync_dir` of every created directory, deepest first. Directory modes `0o755` set after creation.
3. `presets_staged`: `create_dir_all(preset_root)` if planned; each planned directory `create_dir` + `set_mode(0o755)`;
   each `Place` / `Replace` file `copy_new(src, <dest>.krate-new)` + hash check + `set_mode` + `sync_file` + `Placing`;
   each `Place` link `symlink(target, <dest>.krate-new)` (PD-29; its target verbatim, never followed);
   then on Unix `sync_dir` of every created directory and every sibling's parent.
4. `journal_staged`: `write_atomic` with `step: Staged`.
5. `old_aside`: when `had_old`, `rename(target, old)`.
6. `new_in`: `(progress)(Committing)`; `rename(new, target)`; on Unix `sync_dir(plugin_root)`. **Commit point.**
7. `presets_in`: for each sibling, first re-read `dest` against its `pre` (PD-26: `Absent` needs `kind_of(dest) ==
   None`; `Sha256(h)` needs a regular file whose `hash_file` is `h`); a match → `rename(<dest>.krate-new, dest)`; a
   mismatch (a user saved or edited the file after planning) → `remove_file(<dest>.krate-new)`, `dest` pushed to
   `kept`, its entry dropped from `pending`, no error. Then on Unix `sync_dir` of each sibling parent (deduped).
8. `manifest_written`: `write_atomic(manifests/<stem>.json, pending)`.
9. `stale_removed`: each stale `Remove` → `remove_file`; each stale dir → `remove_dir` when `read_dir` is empty
   (`DirectoryNotEmpty` / `AlreadyExists` from `remove_dir` means "not empty": left, not an error).
10. `journal_done`: `write_atomic` with `step: Done`.
11. `old_deleted`: `remove_tree(old)` when `had_old`.
12. `journal_removed`: `remove_file(journal)`. `(progress)(Done)`.

Failure handling (FR-027): a failure in steps 1-6 runs `roll_back(io, &journal_state)`: when step 5 ran, `rename(old,
target)` first; only if that succeeds (or step 5 never ran): `remove_tree(new)`, each sibling `remove_file`, each
created dir (reverse order) `remove_dir` if empty, the journal temp and the journal. A failing roll-back step stops
the roll-back, logs its code once (`warn!`) and leaves the journal. A failure in steps 7-9 runs `roll_forward(io,
&journal)` once (the recovery pass's function, §4.1.9); success → the action returns `Ok` with one `warn!` naming the
first failure's code; failure → the first failure's error, journal left at `Staged`. A failure in step 11 or 12 →
`Ok`, one `warn!`, journal left at `Done`. The returned error is always the first failure's (FR-027). The
`Outcome.kept` holds the planner's `Kept` presets and stale `Kept` files.

#### 4.1.9 Recovery (FR-070 to FR-075, D14)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum Decision { RolledBack, Completed, Refused }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Recovered { pub plugin: String, pub decision: Decision, pub code: Option<&'static str> }
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RecoveryReport { pub journals: Vec<Recovered>, pub temp_removed: usize, pub extract_pruned: usize }
pub fn recover(ctx: &AppContext, io: &dyn InstallIo, extract: ExtractEnv<'_>) -> RecoveryReport;
```

`recover` creates nothing (so `assemble_cases (b)` stays exact, `context_tests.rs:126-146`): `kind_of(<state>/journal)`
must be `Dir` (else nothing is read; any other kind logs `E_INSTALL_JOURNAL` once); `read_dir`; each `<stem>.json` is
read (`read_file`, `MAX_JOURNAL_BYTES`), parsed (`Journal`, `journal_version == 1`), its `Destinations` rebuilt by
`Destinations::from_parts(ctx, &j.plugin, &j.vst3_bundle, j.preset_subdir.as_deref())` (PD-25; the on-disk strings go
through `one_segment` before any join, and the recorded `target` / `new` / `old` must equal the rebuilt ones: a changed
root refuses the journal) and `check_journal`ed (§4.1.6); then FR-071's table on `kind_of(new)`, `kind_of(target)`,
`kind_of(old)` (never following):

| `step` | `new` | `target` | `had_old` / `old` | Action | Decision |
|---|---|---|---|---|---|
| `begun` | any | any | any | `roll_back` without the rename back (target, old, manifest as found) | `rolled_back` |
| `staged` | present | present | any | `roll_back` (no rename back) | `rolled_back` |
| `staged` | present | absent | `true`, old present | `rename(old, target)`, then `roll_back` | `rolled_back` |
| `staged` | present | absent | `false` | `roll_back` | `rolled_back` |
| `staged` | absent | present | `false`, or old present | `roll_forward`: steps 7-12 from the journal; a sibling whose `<dest>.krate-new` is gone and whose `hash_file(dest)` equals the pending entry's hash was already renamed in and is skipped; a sibling whose `<dest>.krate-new` is present is renamed only when `dest` still matches its `pre` (PD-26), else its `.krate-new` is removed, its entry dropped from the manifest written at step 8, and `dest` logged once (`warn!`, `KeptPath`) | `completed` |
| `staged` | absent | absent | old present | `rename(old, target)`, then `roll_back` | `rolled_back` |
| `done` | any | any | any | `remove_tree(old)` if present; remove the journal | `completed` |
| anything else | | | | nothing | `refused` |

A `remove` journal re-runs FR-061 from the manifest on disk (no manifest → remove the journal) → `completed`. A
`refused` journal or a failing step leaves the journal, logs once `E_INSTALL_JOURNAL` with the plugin (FR-074).
Then every `.<stem>.json.tmp` in `manifests/` and `journal/` is removed (`temp_removed`), then `extract::prune(extract)`
(FR-081). One `info!` per journal (plugin, decision, code). Recovery never panics on input and never fails startup.

#### 4.1.10 Uninstall (FR-060 to FR-065, FR-122)

`run_uninstall(env, entry) -> Result<Outcome, InstallError>`: `Destinations::of`; `read_manifest_full` → `None` →
`NotManaged`; FR-064 roots (PD-17) → `RootChanged`; FR-063 resolution → `OutsideRoot`; `total = 1 + presets
entries`; `(progress)(Removing { done: 0, total })`; journal `{op: remove, step: begun, previous_manifest: Some(m)}`
(`remove_begun`); `remove_tree(target)` (`remove_entries`, a missing target counts as removed); then every `presets`
entry, deepest path first: `f` removed iff `kind_of` is `File` and `hash_file` equals the record; `l` iff `Symlink` and
`read_link` equals the record; `d` iff a real directory and `read_dir` is empty (a non-empty real directory is left, not reported: spec FR-061 as
ruled 2026-11-01); anything else → `kept`; a missing
path counts as removed; `Removing { done, total }` after each; then `remove_file(manifest)` (`manifest_removed`), then
`remove_file(journal)` (`journal_removed`); `Done`. Nothing outside the bundle and the manifest's `presets` entries is
removed; `preset_root`, `plugin_root` and user files survive (FR-062). Idempotent (FR-065).

#### 4.1.11 The gate and the action table (FR-093, D13)

```rust
#[derive(Debug)]
pub enum ActionGate { Blocked(InstallError), Open { manifest: Option<Manifest> } }
pub fn action_gate(os: Os, ctx: &AppContext, io: &dyn InstallIo, entry: &PluginEntry) -> ActionGate;
pub fn allowed_actions(driver_present: bool, gate: &ActionGate, availability: &Availability<'_>,
                       update: Option<UpdateState>) -> Vec<ActionKind>;
fn refusal(action: ActionKind, update: Option<UpdateState>, installed: Option<&str>, offered: &str) -> Option<InstallError>;
```

`action_gate`, read-only, in order: `Destinations::of` (a failure is `Blocked` with its code); `kind_of(<state>/journal)`
neither `None` nor `Some(Dir)` → `Blocked(Journal { "state directory is not a directory" })` (PD-28, so the probe never
follows a link `recover` would not read); `kind_of(journal file)` present → `Blocked(JournalPending)`; on `Os::Linux` only (PD-16): `read_manifest_full` error → `Blocked(it)`; FR-064 →
`Blocked(RootChanged)`; FR-063 → `Blocked(OutsideRoot)`; else `Open { manifest }`. `allowed_actions` returns `[]` when
`!driver_present` or `Blocked`; else `Install` iff `Available` and `NotInstalled`, `Update` iff `Available` and
`UpdateAvailable`, `Remove` iff `manifest.is_some()`, in that order. `refusal` is FR-093's (action × state) table.

#### 4.1.12 The per-plugin registry (FR-085)

`static BUSY: Mutex<BTreeSet<(PathBuf, String)>>` keyed `(state_dir, stem)`; `struct Busy { key }` with `Drop`
removing the key (poison → `into_inner`, as `download.rs:646-650`); `Busy::enter(state_dir, stem) ->
Option<Busy>` built after the lock is released (the `InFlight` rule, `download.rs:661-668`). Because the key is
removed in `Drop`, an unwind (a panic between `Busy::enter` and the return, e.g. `check_sandboxed` in
`Destinations::of`, PD-23) releases it too; SC-020 (b) proves that.

#### 4.1.13 The commands (FR-090, FR-094)

```rust
pub fn for_command(env: CommandEnv<'_>, name: &str, expected_version: &str, action: &str, now_unix: u64,
                   host: &dyn HostFacts, progress: &mut dyn FnMut(InstallProgress))
    -> Result<ActionSummary, InstallError>;
pub fn remove_for_command(env: CommandEnv<'_>, name: &str, now_unix: u64,
                          progress: &mut dyn FnMut(InstallProgress)) -> Result<ActionSummary, InstallError>;
```

`for_command`: (1) parse `action` → `ActionKind::{Install, Update}` else `Action`; (2) `download::command_request_for(
env.ctx, name, expected_version, now_unix, host, env.os, env.arch)` → `Download`; `install` facts → `NotManageable`;
`Busy::enter` → `Busy`; `Destinations::of` (PD-23); (3) `env.drivers.driver_for(env.os)` → `NoDriver`; (4)
`action_gate`; `Listing::scan(ctx.roots.plugin_roots(), &[bundle], ListingLimits::PRODUCTION)`;
`detect::detect_plugin(env.os, ctx, entry, host, &listing)`; `status::update_state(entry, Some(&detection))`;
`version::resolve(entry, env.os, env.arch, host.os_version().as_ref())`; `allowed_actions` must contain the kind, else
the gate's code or `refusal`'s; (5) `#[cfg(test)]` apply `request_seam`; the `RefCell` sink (PD-4); `obtain_and(
env.download, &feed, &request, &mut |p| sink(Download { event: p }), |a| { let keep = a.clone(); let action = …(a);
let mut r = driver.run(InstallEnv { …, progress: &mut |e| sink(e) }, vec![action]); (r.remove(0), keep) })`; (6) after
it returns: `Ok` outcome → `download::discard(keep, env.download.io)` (PD-3); map to `ActionSummary { notice:
Some(RescanDaw) }` (FR-091); one `info!` (FR-117). `host.os_version()` is read once and reused. Order consequence
(kept from FR-090): the asset is resolved for `(env.os, env.arch)` in step 2, before the resolver in step 3, so an
off-Linux call reaches `NoDriver` only when the feed entry has an asset for that OS's key (§7 "off-Linux rows"); with
none it ends at step 2 with Phase 7's `E_DOWNLOAD_UNAVAILABLE`, which is production-correct (nothing to install
there either). `remove_for_command`:
`feed::check(ctx, Normal, now)` (`NoFeed { cause }` when no snapshot), `UnknownPlugin`, `NotManageable`, `Busy`,
`Destinations::of`, `NoDriver`, `action_gate` (`Blocked` → code; `Open { None }` → `NotManaged`), then exactly one
`driver.run(…, vec![Action::Uninstall { entry }])`, `notice: None`. Neither makes an asset request before step 5;
Remove makes none (FR-094).

#### 4.1.14 Logging (FR-117)

One `info!(target: LOG_TARGET, "{name} {version} {action}: {ok|code}, placed {p}, removed {r}, kept {k}")` per action
result (in `run`), one per recovered journal, `warn!` lines for roll-back / discard / roll-forward failures. Paths only
as `KeptPath` (`plugin:` / `presets:` + relative path); no `Path::display` anywhere in the module.

### 4.2 `download.rs` (PD-1)

```rust
pub fn request_for_platform(feed: &Feed, name: &str, os: Os, arch: Arch, running: Option<&OsVersion>)
    -> Result<DownloadRequest, DownloadError>;          // body of today's request_for with version::resolve(entry, os, arch, running)
pub fn request_for(feed: &Feed, name: &str, running: Option<&OsVersion>) -> Result<DownloadRequest, DownloadError> {
    request_for_platform(feed, name, NativePlatform.os(), Arch::native(), running)
}
pub fn command_request_for(ctx: &AppContext, name: &str, expected_version: &str, now_unix: u64,
                           host: &dyn HostFacts, os: Os, arch: Arch) -> Result<(Feed, DownloadRequest), DownloadError>;
pub fn command_request(ctx, name, expected_version, now_unix, host) -> … { command_request_for(…, NativePlatform.os(), Arch::native()) }
```

`live_hashes` (the prune) keeps resolving the native platform; on a Windows or macOS test leg a Linux artifact is
spared by the in-flight registry while its own command runs (`download.rs:1359`, the prune runs after it at `:1386`),
which is all SC-009's retry needs.

### 4.3 `status.rs`, `detect.rs`, `context.rs`

- `status.rs`: `#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)] #[serde(rename_all = "snake_case")] pub enum
  ActionView { Install, Update, Remove }` with an exhaustive `fn action_view(ActionKind) -> ActionView` (the PD-21
  pattern of Phase 6, no per-variant rename, `status.rs:142-157`); `PluginView.actions: Vec<ActionView>` as the last
  field; `pub fn build_with_os(os, arch, ctx, mode, now_unix, host, io: &dyn InstallIo) -> StatusView` (today's `build`
  body with `os`, `arch` and, when `install::NativeDrivers.driver_for(os)` is `Some`, one `install::action_gate` per
  entry with an `install` block, PD-15); `build` calls it with `NativePlatform.os()`, `Arch::native()`,
  `&install::FsInstallIo`; `pub fn assemble_with_gates(os, arch, check, running, detections, gates: &BTreeMap<String,
  ActionGate>, driver_present: bool)` (`assemble`'s body; each card's `actions` from `allowed_actions(driver_present,
  gate, &availability, update_state(entry, detection))`, `[]` for an entry without a gate); `assemble` calls it with an
  empty map and `false`; `pub(crate) fn update_state` (PD-13). `build_with_os` resolves the driver through
  `NativeDrivers` (production) — the tests call it with explicit `os`, and SC-016 passes `Os::Linux` on every leg.
  Only reads are added; the snapshot test `detect_tests.rs:1980-2005` keeps proving that.
- `detect.rs`: `pub(crate) fn parse_manifest(bytes: &[u8], plugin: &str) -> RecordRead<InstallerRecord>` = today's
  `read_manifest` from `serde_json::from_slice` on (`detect.rs:891-913`); `read_manifest` = bounded read +
  `parse_manifest` (PD-14).
- `context.rs` `assemble_with_os`, after the fold and before returning: build the `AppContext`, then
  `let report = install::recover(&ctx, &install::FsInstallIo, ExtractEnv { ctx: &ctx, io: &FsExtractIo, limits:
  &ExtractLimits::PRODUCTION });` and `install::log_recovery(&report)`. `reassemble_with_os` is unchanged (no I/O).

### 4.4 `tar_builder_tests.rs` (FR-114)

Exactly the moved set with `pub(crate)` added (the `Tar` methods `new`, `record`, `file`, `dir`, `link`, `long_file`,
`pax`, `finish` too); no body change. `install_tests.rs` adds its own `fn moduleinfo_for(name, version) -> Vec<u8>`
(the moved `moduleinfo` names `Alpha` only, `extract_tests.rs:168-175`) and `fn bundle_archive(spec: &ArchiveSpec) ->
(Vec<u8>, Vec<String>)` building `<Name>.vst3/Contents/{Resources/moduleinfo.json, x86_64-linux/<Name>.so}`, the
`presets` tree, `README.txt`, `CHANGELOG.txt`, optionally an exec bit on the `.so` and `l
<Name>.vst3/Contents/x86_64-linux/lib<name>.so -> <Name>.so`, and returning the listing it built, written literally by
the spec's rows (FR-085 model) for `literal_payload`.

### 4.5 `test_support.rs` additions (D12, FR-115)

- **Call record**: `#[derive(Clone, Debug, PartialEq, Eq)] pub enum IOp { KindOf(PathBuf), ReadLink(PathBuf),
  HashFile(PathBuf), ReadDir(PathBuf), ReadFile(PathBuf), Canonicalize(PathBuf), CreateDirAll(PathBuf),
  CreateDir(PathBuf), CopyNew { src: PathBuf, dest: PathBuf }, WriteNew(PathBuf), Symlink { target: String, link:
  PathBuf }, SetMode(PathBuf, u32), SyncFile(PathBuf), SyncDir(PathBuf), Rename { from: PathBuf, to: PathBuf },
  RemoveFile(PathBuf), RemoveDir(PathBuf), RemoveTree(PathBuf) }` with `is_mutating()` (the twelve FR-028 mutating
  kinds) and `kind()`.
- `MemInstallIo<'a>::new(base: &Path, staged: &'a MemExtractIo)`: `Mutex<BTreeMap<PathBuf, MemNode>>` (Phase 8's
  `MemNode`) plus a record; reads of paths under `<staging>/extract/` go to `staged.node(..)` (read only); everything
  else in its own map; every lookup follows a Mem link in a non-final component, as the OS does (so `kind_of` /
  `read_link` / `remove_file` leave only the final component unfollowed, and PD-24's symlinked-ancestor rows mean the
  same on Mem as on a POSIX filesystem); `canonicalize` resolves Mem links component by component; `supports_symlinks() == true`;
  `rename` of a directory moves its subtree and refuses a non-empty directory target (`DirectoryNotEmpty`, the Unix
  rule); `remove_dir` refuses a non-empty directory; builders `plant_*`, accessors `node`, `paths_under`, `calls`.
- `FaultyInstallIo<'a>::new(inner: &'a dyn InstallIo)` with `fail(kind, nth, ErrorKind)` (one-shot), `fail_from(kind,
  nth, ErrorKind)` (persistent from the n-th), `crash_after(k)` (every mutating call after the k-th mutating call
  returns `io::Error::other("crash")`, PD-8), `on_call(kind, nth, hook: impl Fn(&dyn InstallIo) + Sync)` (runs `hook`
  with the inner IO just before the n-th call of `kind` is forwarded: SC-010's staged-source rewrite, SC-005's
  plant-before-rename), `without_symlinks()` (`supports_symlinks()` returns false, everything else forwarded: the
  FR-015 `Unsupported` row over Mem), `calls()`, `mutating_calls()`.
- `RecordingInstallIo<'a>`: `forwarding(inner)` and `fail_closed()` (mutating calls recorded and return
  `io::Error::other("fail-closed")`; reads forward to `FsInstallIo`), `calls()`, `mutating_calls()` (FR-115).
- `CountingDriver<'a> { inner: &'a dyn InstallDriver, runs: AtomicUsize }`, `RecordingDriver { batches:
  Mutex<Vec<Vec<(ActionKind, String)>>>, kept: Vec<KeptPath> }` (records, returns `Ok` outcomes), `OneDriver<'a>(Option<&'a
  dyn InstallDriver>)` implementing `DriverResolver` for every OS (`Some` only for the OS it was built for).
- `InstallRig { ctx, staging: FaultyStagingIo, extract_limits, policy, cancel }` with `command_env(os, arch, io,
  drivers) -> CommandEnv<'_>`, the one place a test builds a `CommandEnv` (the `DownloadRig` model,
  `test_support.rs:591-618`). Seam-free (PD-30): its struct literal holds `#[cfg(test)] request_seam: None` (the
  `download.rs:775-776` pattern), so the builder compiles under the `test-support` feature with `cfg(test)` off
  (`lib.rs:26-27`, `src-tauri/Cargo.toml:34`); `install_tests.rs` sets `env.request_seam = Some(&seam)` where a row
  needs the fixture server.
- `pub fn temp_roots_with_user_presets(td: &Path, leaf: &OsStr) -> Roots` (SI-15): `roots_at` gains a private
  `roots_at_with(base, user_leaf: Option<&OsStr>)`; the single `#[expect(clippy::disallowed_methods)]` `Roots::new`
  site and the `D6_SITES` count stay as they are (`injection.rs:92-100`).
- No `static` lock anywhere in `test_support.rs` or `install_tests.rs` (the static-lock rule, `injection.rs:1985-2030`).

`install_tests.rs` `fn boundaries(calls: &[IOp], ctx) -> BTreeMap<Cut, usize>` maps each SC-002 cut to the index of
the last mutating call of that step in a fault-free recording: `journal_begun` = the first `Rename` onto
`journal/<stem>.json` (plus its Unix `SyncDir`); "first file of step 2" = the first `CopyNew` into `<bundle>.krate-new`;
`bundle_staged` = the last call whose path is under `<bundle>.krate-new`; "first file of step 3" = the first `CopyNew`
onto a `*.krate-new` sibling; `presets_staged` = the last mutating call before the second journal `WriteNew`;
`journal_staged` = the second journal `Rename` (+ `SyncDir`); `old_aside` = `Rename { target → old }`; `new_in` =
`Rename { new → target }` (+ `SyncDir`); `presets_in` = the last sibling `Rename` (+ `SyncDir`s); `manifest_written` =
the manifest `Rename` (+ `SyncDir`); `stale_removed` = the last call before the third journal `WriteNew`;
`journal_done` = the third journal `Rename` (+ `SyncDir`); `old_deleted` = `RemoveTree(old)`; `journal_removed` =
`RemoveFile(journal)`. A boundary the recording lacks (step 5 on a first install) is the previous cut, as SC-002 row 7
states.

### 4.6 `src-tauri` (FR-096)

`commands.rs` gains `install_plugin(state, name: String, expected_version: String, action: String, on_progress:
tauri::ipc::Channel<InstallProgress>) -> Result<ActionSummary, String>` and `remove_plugin(state, name: String,
on_progress: Channel<InstallProgress>) -> Result<ActionSummary, String>`, each the `download_plugin` shape
(`commands.rs:58-100`): clone the context under the read guard, `spawn_blocking`, build `DownloadPolicy::production()`,
`CancelToken::new()`, `DownloadEnv { …, io: &FsStagingIo }`, `ExtractEnv { ctx: &ctx, io: &FsExtractIo, limits:
&ExtractLimits::PRODUCTION }`, `install::native_env(&ctx, &FsInstallIo, extract, download)`, call the core function
with `&PlatformFacts { io: &OsHostIo }` and `&mut install::channel_progress(|e| on_progress.send(e))`, map errors with
`to_string()`, and a `spawn_blocking` join error as `DownloadError::Task { detail }.to_string()`, as `download_plugin`
does (`commands.rs:93-99`), so no new error code is needed. `lib.rs`
`generate_handler!` lists eight commands (the two last); `build.rs` the eight names; `capabilities/main.json` gains
`allow-install-plugin`, `allow-remove-plugin`; the two `permissions/autogenerated/*.toml` files come from the build.
No plugin, CSP or other capability change; no `fs` / `shell` / `http` permission (FR-096).

### 4.7 UI (`src/status.ts`, `src/view.ts`, `src/main.ts`; FR-097 to FR-100, PD-22)

- `view.ts`: `export type ActionName = "install" | "update" | "remove"`; `PluginView.actions: readonly ActionName[]`
  (parser: `array(o["actions"], …, oneOf(ACTIONS))`, key added to the plugin key list); `export interface
  ActionSummary { name; action; version: string | null; kept: readonly KeptPath[]; notice: "rescan_daw" | null }`,
  `KeptPath { root: "plugin" | "presets"; path: string }`, `toActionSummary(value)`; `export type InstallProgress` (the
  six kinds; `download` carries `event`) and `toInstallProgress(value)`; every incoming value goes through its parser.
- `status.ts`: `export type ChannelMaker = <T>(onMessage: (message: T) => void) => unknown`; `const tauriChannel:
  ChannelMaker = (on) => { const c = new Channel<…>(); c.onmessage = on; return c; }` (`import { Channel } from
  "@tauri-apps/api/core"`); `export const INSTALL_TEXTS = { install: "Install", update: "Update", remove: "Remove",
  confirm: "Confirm", cancel: "Cancel", confirmInstall: "Install {name} {latest}?", confirmUpdate: "Update {name}
  {installed} to {latest}?", confirmRemove: "Remove {name}? The plug-in and its factory presets are removed; your own
  presets are kept.", otherCopy: "Another copy is installed at {path}. Remove it by hand; the manager does not touch
  it.", downloading: "Downloading {done} of {total} bytes", placing: "Placing {done} of {total} files", removing:
  "Removing {done} of {total} files", finishing: "Finishing", installed: "Installed {version}", updated: "Updated to
  {version}", removed: "Removed", rescan: "Restart your DAW or rescan its plug-ins to load the new version.", kept:
  "Kept {path}: changed since install" }` (no string holds the navigation needles, `status.test.ts:356-363`).
- `renderCard` appends, after the release button, a `div.actions` with one `button.action-<name>` per
  `p.actions` entry (none otherwise). A click replaces the row with a `div.confirm` (the FR-099 text,
  for `install` on an `elsewhere` card the `otherCopy` line naming the first non-managed VST3 copy's path, and
  `Confirm` / `Cancel` buttons). `Cancel` restores the row and invokes nothing. `Confirm` disables every action button
  of the card, creates the channel through `makeChannel`, shows a `p.progress` line per event (`download` → `bytes`
  events' `on_disk` / `total`; `placing`; `removing`; `committing` and `done` → `finishing`), and invokes
  `install_plugin` with exactly `{ name, expectedVersion: p.latest, action, onProgress }` or `remove_plugin` with
  `{ name, onProgress }`. On resolve: the outcome lines (`installed` / `updated` / `removed`, `rescan` when `notice`,
  one `kept` line per path); on reject: the rejection text (its code first). Then the section calls `plugin_status` and
  re-renders; the outcome lines live in the section's `Map<string, string[]>` keyed by plugin name and are re-appended
  to that card on every render until that plugin's next action or `Check now` clears them.
- `createStatusSection(invoke, makeChannel = tauriChannel)`, `renderCards(view, invoke, makeChannel = tauriChannel)`,
  `mountStatus(root, invoke, makeChannel = tauriChannel)`; `main.ts` unchanged in behaviour.
- Every string reaches the DOM through `textContent` (the FR-045 frontend scan, `config_guard.rs:140-148`).

### 4.8 Guards

| File | Change | Planted case (existing test name) |
|---|---|---|
| `injection.rs` | `const INSTALL_RS`, `INSTALL_TESTS_RS`, `TAR_BUILDER_TESTS_RS`; `install_findings(files)` called from the function that runs `extract_findings` on the real tree: (1) `EXTRACT_FS_TOKENS` + `create_dir_all`, `sync_all`, `sync_data`, `OpenOptions`, plus PD-20's read tokens, only inside `impl InstallIo for FsInstallIo`, except `fn <m>` inside `trait InstallIo` / `impl InstallIo for …` and `. <m> (` for `m` ∈ `INSTALL_IO_METHODS` = the 19 FR-028 method names that are also tokens (`create_dir`, `create_dir_all`, `symlink`, `rename`, `remove_file`, `remove_dir`, `read_dir`, `read_link`, `canonicalize`); (2) `Command`, `TcpStream`, `UdpSocket`, `xattr`, `thread :: spawn`, `hard_link`, `persist` and every `TEMPFILE_REAL_TEMP` token nowhere in `install.rs`; (3) `flate2` and `sha2` stay confined (existing rules, now also seeing `install.rs`); `StagedTree {` stays only in `extract.rs` (existing) | `injection_scanner_reports_planted_violations` gains, in `install.rs`: `fn f() { std::fs::rename("a", "b").ok(); }`; `fn f() { let _ = std::process::Command::new("x"); }`; `const X: &str = xattr;`-style token `fn xattr() {}`; `use flate2::read::GzDecoder;`; `fn f() { let _ = tempfile::NamedTempFile::new(); }`; `fn f(d: &std::path::Path) { let _ = tempfile::NamedTempFile::new_in(d); }`; `fn f(t: tempfile::NamedTempFile) { let _ = t.persist("x"); }`; `fn f(p: &std::path::Path) -> bool { p.exists() }` (PD-20). Each reported, the real tree green |
| `injection.rs` | `minting_route_findings` `verify_file` callers + `INSTALL_TESTS_RS` (FR-113) | `verified_artifact_scanner_reports_plants` gains `fn f() { let _ = verify_file(p, e, k); }` in `install.rs` (reported) |
| `injection.rs` | `fixture_seam_findings`: `with_fixture_url` allowed in `INSTALL_TESTS_RS`; a third name `request_seam`, declared only in `install.rs`, directly after `#[cfg(test)]` (a `pub(crate)` form as `is_crate_private` handles, `injection.rs:3342-3346`); every other `install.rs` occurrence (the `native_env` initialiser, the `for_command` application) directly after `#[cfg(test)]`; in `test_support.rs` only as a field initialiser directly after `#[cfg(test)]` (PD-30); free in `INSTALL_TESTS_RS`; never in `src-tauri/` or any other file (SI-2, SI-27) | `fixture_url_seam_scanner_reports_plants` gains `struct P { request_seam: Option<u8> }` in `install.rs` (no `cfg(test)`), `fn f() { request_seam(); }` in `src-tauri/src/commands.rs`, and `fn f(e: &mut CommandEnv<'_>) { e.request_seam = None; }` in `test_support.rs` (no `cfg(test)`); each reported |
| `injection.rs` | `FR088_FILES` `[…; 8]` + `INSTALL_TESTS_RS`, `TAR_BUILDER_TESTS_RS`; `PHASE7_TEST_FILES` `[…; 6]` + the same two (SI-14) | `real_location_scanner_reports_plants` / `phase7_host_scanner_reports_plants` unchanged (they plant into a listed file) |
| `config_guard.rs` | `HANDLER_LIST`, the capability list, the `build.rs` list: eight commands; a new check in `check_config`: the `install_plugin` and `remove_plugin` signatures in `commands.rs` take only `State`, `String` and `Channel<InstallProgress>` parameters (no `Path`, `PathBuf`, `OsString`) | `planted_config_changes_fail_the_guard` gains "`commands::install_plugin` removed from the handler list" and "`path: PathBuf` added to `remove_plugin`" |
| `roster.rs` | `identifier_files()` and its must-scan list + `crates/core/src/install.rs` | existing plants unchanged |

Every new check runs inside an existing `#[test]` (no new guard name, as Phases 7 and 8 did).

### 4.9 `tests/required-tests.json` and `EXPECTED_REQUIRED` (FR-116)

Appended in SC order. `cargo.all` + 28: `install::tests::` `install_update_remove_cycle`,
`crash_after_each_step_recovers`, `manifest_outside_roots_refused`, `remove_keeps_user_files`, `update_preset_rules`,
`manifest_reads_back_through_detect`, `unverified_never_installed`, `state_checks_before_download`,
`driver_rechecks_state` (SC-008, SI-26), `io_failures_roll_back`, `changed_staged_file_refused`,
`collisions_refused_before_write`, `remove_crash_resumes`, `startup_recovery`, `recovery_table_rows` (SC-013, SI-26),
`concurrent_actions`, `sync_before_commit`, `no_driver_off_linux`, `progress_events`,
`sandbox_guard_panics`, `artifact_released_after_success`, `logs_one_line_per_action`, `commands_run_over_the_driver`,
`batch_runs_every_action` (SC-027, SI-26), `remove_deletes_bundle_whole`, `cut_before_new_in_rolls_back_in_process`;
`context::tests::assemble_runs_install_recovery`; `status::tests::actions_per_state`. `cargo.macos` and `cargo.linux` + 8 `_unix`: `install_update_remove_cycle_unix`,
`crash_after_each_step_recovers_unix`, `manifest_outside_roots_refused_unix`, `io_failures_roll_back_unix`,
`collisions_refused_before_write_unix`, `non_utf8_root_refused_unix`, `symlinked_plugin_root_unix`,
`remove_bundle_link_unix`; `cargo.macos` + `native_wiring_offers_nothing_macos`; `cargo.linux` +
`native_wiring_installs_linux`; `cargo.windows` + `native_wiring_offers_nothing_windows`; `vitest` + the 17 SC-024
titles. Totals: `all` 276, `windows` 12, `macos` 44, `linux` 39, `vitest` 46. Each suffixed test carries the matching
`#[cfg(unix)]` / `#[cfg(target_os = …)]` so it compiles only on its lanes' legs. `EXPECTED_REQUIRED` gains exactly the
same names. SC-007's compile-fail doctest and SC-018 / SC-019's guard assertions add no name.

### 4.10 Roadmap amendments (FR-123), made by the build stage

- (a) Phase 9 component "Journaled bundle swap", after "…then cleans up.": "The commit point is the rename of new into
  the target: a crash before it rolls back (new present, target missing → rename old back, or a first install ends
  absent); from it on the recovery pass completes the swap (Phase 9 spec D7, Q1)." and "(new present, target missing →
  rename new in)" is replaced by "(new absent, target present → finish the presets, manifest and cleanup)".
- (b) Phase 12 settings screen bullet: "The screen refuses to change the Linux plug-in root while any install manifest
  exists; the user removes those plugins first (Phase 9 FR-064, Q3)."
- (c) Phase 12 components: "🆕 Remove for a plugin that has an install manifest but is no longer in the feed (an orphan
  manifest), driven by the manifest alone (Phase 9 Q8)."

### 4.11 State layout

```
<state dir>/                               Roots::state_dir (injected; production from src-tauri, Phase 4)
  manager.lock                             state_lock.rs
  settings.json                            Phase 4
  feed-cache.json                          Phase 5
  manifests/<stem>.json                    Phase 9 writes (D8, FR-040); Phase 6 reads (detect.rs:883-914)
  manifests/.<stem>.json.tmp               transient (FR-044); removed by recover
  journal/<stem>.json                      Phase 9, at most one per plugin (FR-050, FR-052)
  journal/.<stem>.json.tmp                 transient
  staging/downloads/<hex>/<asset>          Phase 7
  staging/extract/<hex>.<6>/               Phase 8; pruned at start (FR-081)
<Linux plug-in root>/                      settings root, default ~/.vst3 (never recorded, never removed)
  <vst3_bundle>[.krate-new | .krate-old]
<presets().user>/<Name>[/<preset_subdir>]/ … and <file>.krate-new siblings while staging
```

### 4.12 The OS abstraction

Phase 9 adds no `Platform` method. Its OS seams are (1) `InstallIo` / `FsInstallIo` with one `cfg(unix)` body (Linux,
macOS: modes, symlinks, directory fsync) and one `cfg(not(unix))` body (Windows: no mode, `supports_symlinks() ==
false`, no directory fsync, no link fallback in `remove_tree`: PD-19), with the Mem / Faulty / Recording doubles running the same logic
on all three; (2) `driver_for(os)` (`Some` only for Linux); (3) `CommandEnv.os` / `arch` and
`status::build_with_os(os, arch, …)` (D16). Tests per implementation: Windows (`native_wiring_offers_nothing_windows`
and every `cargo.all` row over `FsInstallIo` on NTFS), Linux and macOS (the eight `_unix` names over the real POSIX
filesystem), Linux (`native_wiring_installs_linux`), macOS (`native_wiring_offers_nothing_macos`).

---

## 5. Dependencies

| Crate / package | Version | Use | Verified against | Why |
|---|---|---|---|---|
| std | 1.99.1 (`rust-version` 1.90) | `fs`, `os::unix::fs::{symlink, PermissionsExt}`, `io::ErrorKind`, `sync::{Mutex, mpsc}`, `cell::RefCell` | §1.2 | — |
| `serde`, `serde_json` | 1.0.229, 1.0.154 (existing pins) | manifest, journal, summary, progress | `Cargo.toml:32-33` | — |
| `thiserror`, `log` | 2.0.21, 0.4.34 (existing) | `InstallError`, log lines | `Cargo.toml:40`, `:18` | — |
| `sha2` | 0.11.1 (existing) | only through `verify::Sha256Stream` | `verify.rs:9`, `:302-316` | FR-082 confinement |
| `tempfile` | 3.27.0 (existing) | tests only (`TempDir`); never in `install.rs` (FR-112) | `Cargo.toml:39` | — |
| `tauri` | 2.12.5 (existing) | `tauri::ipc::Channel<InstallProgress>` in `commands.rs` | https://v2.tauri.app/develop/calling-frontend/ | — |
| `@tauri-apps/api` | 2.12.4 (existing) | `Channel` in `status.ts`; `mocks` in tests | `package.json`; `node_modules/@tauri-apps/api/core.js:116-125` | — |

No new crate, npm package or Tauri plugin; neither lock nor `package.json` changes (FR-111).

---

## 6. Safety design

- **Injected roots.** Every destination comes from `ctx` (`linux_plugin_root_or_default`, `presets().user`,
  `state_dir()`, `staging_dir()`); `Destinations::of` runs `check_sandboxed` on every path before any `InstallIo` call
  (PD-23, FR-110), so a mis-rooted test panics before touching anything. Tests use `fresh_temp_roots()` /
  `temp_roots_with_user_presets` only; production roots come from `src-tauri` (`Roots::from_os()`, Phase 4).
- **Verify before extract, extract before place.** The driver's only input is a `VerifiedArtifact` (no public
  constructor, `verify.rs:90-114`; SC-007 adds a compile-fail doctest on `Action`); it installs only from the
  `StagedTree` it makes with `extract::stage`, which rejects zip-slip, links escaping the bundle, hard links and device
  files (Phase 8); every copied byte is re-hashed against `file_hashes()` (FR-022). Nothing is executed.
- **No path from a string unchecked.** `<Name>`, `vst3_bundle`, `preset_subdir` re-checked as one segment; every
  archive, manifest and journal path through `dest_path` (strict descendant, normal segments); manifest and journal
  parents canonical-checked against canonical roots before any removal (FR-063, PD-17) and every planned placement's
  deepest existing ancestor before any write (PD-24); journals name only their own `.krate-new` / `.krate-old`
  siblings (FR-051) and rebuild their destinations from their own strings through the same checks (PD-25); a link
  target never leaves its top-level entry lexically, so remapping `presets` cannot redirect it (PD-29).
- **Write discipline.** `create_new` for every file (no write through an existing name or dangling link); directories
  one level at a time except the two roots; no placement through a symlinked ancestor (PD-24); links created, never
  followed; removals by kind without following (PD-19); a user file is never overwritten (D5): a sibling renames in
  only over the pre-state it was planned against, in process and in recovery (PD-26; the residual window between that
  re-read and the `rename` is the only one left, Review notes); state subdirectories are never written through a link
  (PD-28); stale and Remove removals only on hash / target equality.
- **Atomicity and durability.** Plan before write (D4); journal before the first staged byte (D6, PD-7); file and
  directory syncs before the journal says `staged` (FR-023); one commit point (D7); recovery at every start.
- **Elevated helper.** Not involved: Linux needs none (per-OS table line 118); `install.rs` names no `Command` (§4.8).
- **Tauri capability set.** Two command permissions added (`allow-install-plugin`, `allow-remove-plugin`), nothing
  else; the commands take a plugin name, a version, an action word and a channel; the core re-checks all of them
  against the feed and the disk (D11). No `fs`, `shell`, `http` or remote content (FR-096; `config_guard.rs`).
- **Never touch the real system in tests.** Fail-closed recorder for every refusal row (FR-115); no process started by
  a Phase 9 test; no socket but the loopback fixture server; asset URLs reach it only through the test-only seam
  (PD-2); `FR088_FILES` / `PHASE7_TEST_FILES` cover the new test files (SI-14). No key on disk: `TestSigner` keys are
  in memory (Phase 7).

---

## 7. Test plan

All Rust tests in `crates/core/src/install_tests.rs` (module `install::tests`) unless named otherwise. Common rig:
`fresh_temp_roots()`; a `FixtureServer` serving `/versions.json` and each asset route; a `TestSigner`; feeds from
`test_feed_document` patched with `serde_json` (`payload` from the archive's literal listing, `preset_subdir` when the
row sets it), with one `linux-x86_64` `tar.gz` asset per version (SI-1); `request_seam` mapping each request to
`server.url(<asset route>)` through `with_fixture_url` (PD-2); `FakeHostFacts { os_version: Some("2.99"), .. }` (Linux
rows; the off-Linux rows below differ);
`CommandEnv { os: Os::Linux, arch: Arch::X86_64, … }` from `InstallRig`. Plugins `Alpha` / `Beta` (never a shipped
name). Driver-level rows sign the archive in memory and verify it with `verify_file` (FR-113). Every refusal row runs
over `RecordingInstallIo::fail_closed()` and asserts `mutating_calls() == 0`. Where a row needs the fixture server,
`install_tests.rs` sets `env.request_seam = Some(&seam)` on the rig's seam-free `CommandEnv` (PD-30).

**Off-Linux rows** (SC-016 `no_driver_off_linux`, SC-029 `native_wiring_offers_nothing_windows` / `_macos`).
`for_command` resolves the asset for `(env.os, env.arch)` at step 2, before the resolver at step 3 (PD-1, §4.1.13), and
`request_for` maps a missing key to `E_DOWNLOAD_UNAVAILABLE` (`download.rs:766-781`, code `:444`). So each such row's
feed entry carries, besides the `linux-x86_64` `tar.gz` asset, the asset the schema binds to the row's OS key
(`versions.v2.schema.json:136-146`): `windows-x86_64` `inno-exe` for `Os::Windows`, `darwin-aarch64` `pkg` for
`(Os::Macos, Arch::Aarch64)`, and for SC-029 the `native_key()` asset (`test_support.rs:327-332`, built with the
format and default `min_os` `test_feed_document` gives its prefix, `test_support.rs:360-367`). Its host facts' `os_version` meets that
asset's `min_os` (`version.rs:226-228`): `Some("15.0")` on macOS rows (darwin's default `min_os` is `15.0`, so the rig's
`2.99` would be `NeedsNewerOs`), any value on Windows rows (`inno-exe` has no default `min_os`). Step 2 then succeeds,
and the row asserts `E_INSTALL_NO_DRIVER` with 1 feed / 0 asset requests and zero mutating calls. `remove_for_command`
resolves no asset, so its rows need no extra key.

### 7.1 SC → test

| SC | Test | Inputs | Assertions |
|---|---|---|---|
| SC-001 | `install_update_remove_cycle` (Fs, every OS: no link, no exec bit); `_unix` (exec bit on `Alpha.so`, `libalpha.so -> Alpha.so`) | both with `preset_subdir` absent and `Kits`; settings root `<td>/plugins/fresh` absent; user presets `presets/user/Alpha[/Kits]/Mine.vstpreset` and `…/Warm Pads/Mine2.vstpreset`; `for_command` install 1.2.0, `for_command` update 1.3.0 (one preset added, one removed, one changed), `remove_for_command` | `snapshot` (path, kind, size; mode under `cfg(unix)`) of `plugins/` and `presets/user` equals the literal expected tables after each step; the two user presets byte-identical throughout; notices `RescanDaw`, `RescanDaw`, `None`; after Remove only the user presets, `plugins/fresh`, `presets/user` remain |
| SC-002 | `crash_after_each_step_recovers` (Mem: `MemInstallIo` over `MemExtractIo`, so links and modes on every OS); `_unix` (Fs) | first install and update, a fault-free recording → `boundaries` (§4.5); per cut `FaultyInstallIo::crash_after(k)` driving `LinuxDriver::run`, then `recover` over a healthy IO | 28 rows exactly as the spec table: decision and side by bundle listing digest (`CanonicalListing::from_entries` over the installed tree), preset destinations equal that side's factory set, manifest `version` (absent for "absent"), `journal/` empty, no `*.krate-new`, `*.krate-old`, `.*.json.tmp` under the roots or the state dir |
| SC-003 | `manifest_outside_roots_refused`; `_unix` (directory symlink inside the root pointing to a sibling `outside/`) | after a real install, the manifest rewritten per row; each row drives `remove_for_command` and `for_command` update over `fail_closed()` | codes per the spec table; zero mutating calls; `snapshot(<td>/outside)` equal before and after |
| SC-004 | `remove_keeps_user_files` | install, then edit one placed preset, save a user preset into a factory subfolder, add `Alpha.vst3/extra.txt`; `remove_for_command` | `kept == [presets: Alpha/Warm Pads/Night Air.vstpreset]`; `kind_of(target) == None`; `detect::detect_plugin` (`Os::Linux`) → no managed copy, `read_manifest` → `Absent`; user files byte-identical; the subfolder holding the user preset remains |
| SC-005 | `update_preset_rules` | Mem rows on every OS (incl. a destination that is a link); a `cfg(unix)` Fs block for the link row; a manual-install row (bundle 1.2.0 at the target, presets copied by hand, one edited, no manifest) → update 1.3.0; a plant-before-rename row (PD-26): `FaultyInstallIo::on_call(Rename, <first sibling rename>, …)` writes a user file at a `Place` destination and edits a `Replace` destination just before step 7 | stale unchanged removed; stale edited kept + reported; edited current kept, reported, absent from the new manifest; identical destination adopted (recorder: no `CopyNew` / `Rename` with that destination) and recorded; differing kept; link / directory kept and never followed (no read of the link target); manual row: bundle swapped, identical presets adopted, edited kept, `detect::read_manifest` → `Found(Manifest { version_text: "1.3.0" })`; plant row: action `Ok`, both planted files byte-identical, both in `kept`, neither in the new manifest, no `*.krate-new` left |
| SC-006 | `manifest_reads_back_through_detect` | install 1.2.0; `status::build_with_os(Os::Linux, Arch::X86_64, …, &FsInstallIo)` against a feed at 1.2.0, then 1.3.0, then after the update | `Found(Manifest { "1.2.0" })`; card `installed: version`, `installed_version: "1.2.0"`, the managed copy at the target, `record.kind: manifest`, `actions: [remove]`; then `update: update_available`, `actions: [update, remove]`; then `[remove]`, `up_to_date` |
| SC-007 | `unverified_never_installed` + a compile-fail doctest on `Action` | three served assets: a one-byte tamper, a signature by another `TestSigner`, a valid signature bound to `Beta`; `CountingDriver` around `LinuxDriver` | each the Phase 7 code read from `DownloadError::code()`: `E_VERIFY_SHA256` (tamper; the feed keeps the original hash), `E_VERIFY_KEY_ID` (other key), `E_VERIFY_TRUSTED_COMMENT` (bound to `Beta`) (`verify.rs:329-349`); `runs == 0`; `staging/extract/` absent; `snapshot(plugins/, presets/)` unchanged |
| SC-008 | `state_checks_before_download` | the 12 spec rows over `fail_closed()`, plus a 13th (SI-26): a schema-1-shaped entry (no `release`, so no `install` block), through `remove_for_command` and through `for_command` | codes; zero mutating calls; server request counts per row (feed / asset) as the table; row 13: `remove_for_command` → `E_INSTALL_NOT_MANAGEABLE`, 1 / 0 requests; `for_command` → `E_DOWNLOAD_UNAVAILABLE` (`NotThroughManager`), 1 / 0 requests, because FR-090 step 2 resolves the asset before the FR-003 check and the v2 schema makes `platforms` and `install` mutually required (`versions.v2.schema.json:162`), so a parsed entry never reaches `for_command`'s FR-003 check with an asset; that check stays as defence and is reached at driver level (`driver_rechecks_state` (j)) |
| SC-008 (FR-025, FR-002, FR-015) | `driver_rechecks_state` | `LinuxDriver::run` called directly (no `for_command`, so `allowed_actions` / `refusal` never run) with verified artifacts: over `RecordingInstallIo::fail_closed()`: (a) `Action::Install` while a target bundle exists; (b) `Action::Update` with no target; (c) `Action::Update` whose manifest records 1.3.0 against a staged 1.3.0; (d) the same against a staged 1.2.0; (e) a manual bundle (no manifest) whose moduleinfo says 1.4.0, `Action::Update` to 1.3.0 (PD-27); (f) a manual bundle with no `moduleinfo.json`, `Action::Update`; (g) `Action::Install` whose `VerifiedArtifact` came from `verify_file` with an `inno-exe` `Expected` (FR-113) — over `FaultyInstallIo::without_symlinks()` around `MemInstallIo` / `MemExtractIo`: (h) `Action::Install` of a tree holding an `l` line; over a plain `FaultyInstallIo` around `MemInstallIo` / `MemExtractIo`: (i) (PD-21) `Action::Install` of a tree of 1 100 empty preset files whose paths are ≈ 3 900 bytes (16 segments of ≤ 240 bytes, inside `max_path_bytes` 4096 / `max_segment_bytes` 255, `extract.rs:203-209`), so the pending manifest's encoding exceeds `MAX_MANIFEST_BYTES` (4 MiB); over `fail_closed()`: (j) (FR-003) `Action::Install` whose entry is Alpha's parsed entry with `release.install` set to `None` (`StableRelease.install` is a public `Option`, `feed.rs:55-62`) | (a) `StateChanged { shown: "install", current: "installed" }`; (b) `StateChanged { "update", "not_installed" }`; (c) `NotNewer { installed: "1.3.0", offered: "1.3.0" }`; (d) `NotNewer { "1.3.0", "1.2.0" }`; (e) `NotNewer { "1.4.0", "1.3.0" }`; (f) `StateChanged { "update", "unknown" }`; (g) `E_INSTALL_FORMAT`, `staging/extract/` absent (refused before `stage`); (h) `E_INSTALL_UNSUPPORTED`; (i) `E_INSTALL_MANIFEST { reason: "too large" }`; (j) `E_INSTALL_NOT_MANAGEABLE`, `staging/extract/` absent; every row `mutating_calls() == 0` and one `ActionResult` |
| SC-009 | `io_failures_roll_back`; `_unix` adds the `symlink`, `set_mode`, `sync_dir` rows | update 1.2.0 → 1.3.0 over `FaultyInstallIo(FsInstallIo)`; each row × `StorageFull`, `PermissionDenied`, `Other` for pre-commit rows | per-row `step` and state as the spec table; for pre-commit rows a retry `for_command` makes 1 feed / 0 asset requests and ends `Ok`; the warn rows assert one captured `warn!` naming the code |
| SC-010 | `changed_staged_file_refused` | `FaultyInstallIo::on_call(CopyNew, <the first `.so` copy>, …)` rewrites the staged source before the copy is forwarded (the staged path read from the `CopyNew { src }` call) | `Changed { path: plugin:Alpha.vst3/Contents/x86_64-linux/Alpha.so }`; SC-009's unchanged-state assertions |
| SC-011 | `collisions_refused_before_write`; `_unix` (target is a symlink to a directory; the PD-24 / PD-28 Fs rows below) | target a file; stray `Alpha.vst3.krate-new` with no journal; stray `Alpha.vst3.krate-old` with no journal, on both Install and Update (SI-26); a preset directory destination that is a file; an existing `<dest>.krate-new`; the plugin root a file (SI-13). Mem rows (every OS): `preset_subdir` `Kits` with `<user>/Alpha` a link to `<td>/outside` (PD-24 a); `preset_subdir` absent with `<user>/Alpha` itself a link (PD-24 a, inclusive end); a preset link `presets/a/l -> ../../presets/b` and a bundle link `Alpha.vst3/Contents/l -> ../../Alpha.vst3/Contents/x` (PD-29). `_unix` Fs rows: the two PD-24 links on a real filesystem; `<state>/journal` a link to `<td>/elsewhere-state/` and, separately, `<state>/manifests` a link (PD-28), each through `for_command` install | each `Collision` with its `KeptPath` (`Alpha.vst3.krate-old` row: `Collision { plugin: "Alpha.vst3.krate-old" }`; PD-24 rows: `Collision { presets: "Alpha" }`); PD-29 rows `E_INSTALL_PATH`; PD-28 rows `E_INSTALL_JOURNAL` / `E_INSTALL_MANIFEST` with reason `state directory is not a directory`, 1 / 0 requests; zero mutating calls; strays, `snapshot(<td>/outside)` and `snapshot(<td>/elsewhere-state)` byte-identical |
| SC-012 | `remove_crash_resumes` | `crash_after(k)` at the journal and at half the preset entries, then `recover` | the tree equals an uninterrupted Remove's `snapshot`; a second `remove_for_command` → `NotManaged`, zero mutating calls |
| SC-013 | `startup_recovery`; `context::tests::assemble_runs_install_recovery` | planted: a `staged` journal of an update cut after `old_aside`, `manifests/.Alpha.json.tmp`, `staging/extract/<64 hex>.abcdef/`; a second plugin's journal naming `/<td>/../elsewhere`; (PD-25, SI-20) a third plugin's journal whose `preset_subdir` is `..`, and a fourth's whose `preset_subdir` is `Kits` with one sibling under `<user>/<Name>/Other/` | `assemble_with_os(Os::Linux, …)` → `context` `Ok`; first journal `rolled_back` (old bundle back, no leftovers); temp and extract entry gone; second, third and fourth journals `refused`, each file still present, one captured `E_INSTALL_JOURNAL` record each, `snapshot` of the roots unchanged for those plugins; `status::build_with_os` gives each `actions: []`; their `for_command` and `remove_for_command` → `JournalPending` |
| SC-013 (FR-071) | `recovery_table_rows` (Mem, every OS) | a valid in-root `staged` journal (taken from a fault-free planned update: the journal written at `journal_staged`) plus each planted disk state: (1) `new` absent, `target` absent, `old` present (outside interference); (2) `had_old` true, `new` / `target` / `old` all absent; (3) `had_old` true, `new` absent, `target` present, `old` absent; (4) `new` absent, `target` present, `old` present, a user file planted at a `Place` sibling's `dest` (PD-26) | (1) `rolled_back`, `target` equals the old side's listing digest, no `*.krate-*`, journal gone; (2) and (3) `Refused`, every node byte-identical to the planted `snapshot`, the journal file still present, `action_gate(Os::Linux, …)` → `Blocked(E_INSTALL_JOURNAL_PENDING)`; (4) `Completed`, the planted file byte-identical, absent from the manifest written, one captured `warn!` naming it as `presets:` + relative path, no `*.krate-new` left |
| SC-014 | `concurrent_actions` | `std::thread::scope`; per thread its own `FakeHostFacts`, `RecordingInstallIo`, rig; `mpsc` `ready` / `go` channels in the progress closure on the first `Staging` | as the spec (three blocks); the fixture server's request log split per call by counting before and after |
| SC-015 | `sync_before_commit` | recorded fault-free update over `RecordingInstallIo::forwarding(&FsInstallIo)` | every `CopyNew` dest under `.krate-new` and every sibling has a later `SyncFile` before the second journal `WriteNew`; `cfg(unix)`: a `SyncDir` of every created directory and sibling parent before that write, and of `plugin_root` after the `new_in` rename; `cfg(windows)`: no `SyncDir` |
| SC-016 | `no_driver_off_linux`; `status::tests::actions_per_state` | `NativeDrivers` with `Os::Windows` / `Os::Macos`; the `for_command` rows use §7's off-Linux feed (a `windows-x86_64` `inno-exe` asset for `(Os::Windows, Arch::X86_64)`, a `darwin-aarch64` `pkg` asset for `(Os::Macos, Arch::Aarch64)`, host `os_version` `Some("15.0")` on the macOS row); the Phase 6 fixture states plus journal pending, manifest failing FR-041, manifest root changed | `driver_for` `None` ×2; `NoDriver`, zero mutating calls, 1 / 0 requests; the table for `(Linux, X86_64)` (each omitted action refused by its FR-093 / FR-094 code before any asset request) and `[]` everywhere for `(Windows, X86_64)` and `(Macos, Aarch64)` |
| SC-017 | `progress_events` | update over Fs; Remove | `Download { … }*`, `Staging`, `Placing { done }` 1..=total (= placed files), `Committing`, `Done`; Remove `Removing { done: 0..=total }` (total = 1 + preset entries, SI-12), `Done` |
| SC-018 | `config_guard.rs` existing names | — | eight commands; the two new plants reported |
| SC-019 | `roster_and_hosts_absent_from_sources`, `install_identifiers_absent_from_sources`, `injection_scanner_reports_planted_violations`, `verified_artifact_scanner_reports_plants`, `fixture_url_seam_scanner_reports_plants` (existing) | §4.8 plants | real tree green; each plant reported |
| SC-020 | `sandbox_guard_panics` | two `catch_unwind(AssertUnwindSafe(..))` blocks (SI-19); after (b), a context with `settings.linux_plugin_root` back inside the sandbox and the same state dir, and a healthy IO | (a) `Err`, message holds `outside the test sandbox`; (b) the same, recorder `calls()` empty, 1 feed / 0 asset requests; then the same plugin's `for_command` (outside any `catch_unwind`) returns a result whose code is not `E_INSTALL_BUSY` (`Ok` accepted): the guard taken before the panic was released on unwind (FR-085, §9) |
| SC-021 | `artifact_released_after_success` | one success, one failure (`copy_new` 1st `StorageFull`); a discard-failure row (PD-3): the rig's `FaultyStagingIo::failing(Op::RemoveDir, n, PermissionDenied)` with `n` the discard's `remove_dir_all` (the last `Op::RemoveDir` of a fault-free run of the same row, `download.rs:1504`, `test_support.rs:569-572`) | after success `staging/downloads/<hex>/` and the `extract/` entry are gone; after failure the `<hex>/` directory remains, the `extract/` entry is gone; discard-failure row: summary `Ok`, exactly one captured `warn!` holding the discard's `E_DOWNLOAD_*` code, the manifest present and reading back through `detect::read_manifest` |
| SC-022 | `logs_one_line_per_action` | `install_capture`; install, update, a refused update | `take_mine()` filtered to `krate_manager_core::install`: one `Info` per action naming plugin, version, action, `ok` or code; no record holds the raw or canonical TempDir path |
| SC-023 | `non_utf8_root_refused_unix` | `temp_roots_with_user_presets(td, OsStr::from_bytes(b"u\xff"))` (SI-15) | `PathNotUtf8 { which: "presets" }`, zero mutating calls |
| SC-024 | vitest, `src/status.test.ts`, the 17 titles | `PluginView` literals built through `toStatusView`; a recording `Invoker`; a fake `ChannelMaker` that records `onMessage` and returns `{ id: n }`; deferred promises to hold an action | per title, as the spec; "confirmed install sends …" asserts exactly `{ name, expectedVersion, action, onProgress: <the fake channel> }`; the `elsewhere` copy path is `/opt/vst3/Alpha.vst3` (SI-14) |
| SC-025 | the `_unix`, `_linux`, `_macos` names on their legs | — | §7.3 |
| SC-026 | the gate commands (§7.4) | — | exit 0, no `warning:`; no new `#[allow` / `#[expect` (the one `#[expect]` stays in `test_support.rs`); no `--test-threads` |
| SC-027 | `commands_run_over_the_driver` | `OneDriver(Some(&recording))` | `run` called once per command with one action (`Install`, `Update`, `Uninstall`, entry `Alpha`); notices `RescanDaw`, `RescanDaw`, `None`; Remove `kept` equals the driver's; Remove made 0 asset requests |
| SC-027 (FR-001, D10) | `batch_runs_every_action` | `LinuxDriver::run` (the real driver, not the recorder) over `FaultyInstallIo::fail(CopyNew, 1, StorageFull)` around `FsInstallIo` (so `detect::read_manifest` can read the result) with the batch `[Install Alpha, Install Beta, Uninstall Alpha]`, verified artifacts for both | exactly three `ActionResult`s in batch order, names `Alpha`, `Beta`, `Alpha`, actions `Install`, `Install`, `Remove`; outcomes `E_INSTALL_FULL` (step `bundle_staged`), `Ok`, `E_INSTALL_NOT_MANAGED` (Alpha rolled back, so no manifest); Beta's manifest reads back through `detect::read_manifest` as `Found(Manifest { version_text: <Beta's> })`; nothing of Alpha under the roots |
| SC-028 | `symlinked_plugin_root_unix` | settings root `<td>/vst3-link -> <td>/vst3-real` | SC-001 snapshots read through the link target; the recorder's `CopyNew` / `Rename` paths for `.krate-new` / `.krate-old` lie beside the bundle in the link's directory spelling; neither Update nor Remove is `RootChanged`; `symlink_metadata(<td>/vst3-link)` is still a link to the same target |
| SC-029 | `native_wiring_installs_linux` (`cfg(target_os = "linux")`), `native_wiring_offers_nothing_windows`, `native_wiring_offers_nothing_macos` | `install::native_env` + `request_seam` (set in `install_tests.rs`, PD-30); `status::build`; Windows / macOS: §7's off-Linux feed with the `native_key()` asset, host `os_version` `Some("15.0")` on macOS | Linux: `["install"]`, install `Ok`, then `["remove"]`; Windows / macOS: every card `[]`, both commands `NoDriver`, `snapshot` of the roots unchanged |
| SC-030 | `remove_deletes_bundle_whole`, `cut_before_new_in_rolls_back_in_process`, `remove_bundle_link_unix`; `rg` check of §7.4 step 10 | recorder over a Remove; `rename` of `new_in` failing after `old_aside`; a link inside the bundle to a canary outside | exactly one `RemoveTree(<target>)`, no `RemoveFile` under the target; old install restored (SC-002 rows 6-7 sides); the link removed, the canary intact; the three FR-123 sentences present |

### 7.2 FRs without their own SC

| FR | Covered by |
|---|---|
| FR-001 (batch: one result per action, in order, a failure never stops the next) | `batch_runs_every_action` (SC-027 row) |
| FR-002 (`E_INSTALL_FORMAT` before `stage`) | `driver_rechecks_state` (g) |
| FR-003 (`E_INSTALL_NOT_MANAGEABLE`) | `state_checks_before_download` row 13 (`remove_for_command`); `driver_rechecks_state` (j) |
| FR-015 (`E_INSTALL_UNSUPPORTED` before any write) | `driver_rechecks_state` (h) |
| FR-025 (driver-side re-check, incl. PD-27's manual install) | `driver_rechecks_state` (a)-(f) |
| FR-071 (no-crash-cut and `refused` rows) | `recovery_table_rows` (1)-(3) |
| FR-012 (`dest_path`) | SC-003 (`..`, absolute), SC-011; a table inside `collisions_refused_before_write` over `dest_path` (empty, `.`, `..`, `a/../b`, `C:`, `a\b`, NUL, a trailing `/`); PD-29's link rule: SC-011 Mem rows |
| PD-3 (discard failure stays `Ok`) | `artifact_released_after_success` discard-failure row |
| PD-21 (manifest over 4 MiB refused before any write) | `driver_rechecks_state` (i) |
| PD-24 (placement containment), PD-28 (state subdirectories) | `collisions_refused_before_write` Mem rows and `_unix` rows |
| PD-25 (journal destinations) | `startup_recovery` third and fourth journals |
| PD-26 (sibling pre-state) | `update_preset_rules` plant row; `recovery_table_rows` (4) |
| FR-013 (`README.txt`, `CHANGELOG.txt` not placed) | SC-001 snapshots (neither appears) |
| FR-015 (modes, no xattr) | SC-001 `_unix` modes; `install_findings` bans `xattr` |
| FR-042 | SC-006 |
| FR-043 | SC-002 / SC-013 path assertions under the state dir |
| FR-066 | SC-024 (no reinstall button), SC-016 table |
| FR-080, FR-081 | SC-013 |
| FR-111 | `core_dependencies_are_the_fr007_set` (existing, unchanged set) |
| FR-120, FR-121 | `git diff --name-only` at the close (§7.4 step 11) against §2 |
| FR-124 | SC-024 two `elsewhere` titles |

### 7.3 PENDING-CI rows (exact job and assertion)

Each closes on a leg only when the run's committed `tests/required-tests.json` lists its names and that leg's step
`required tests ran` (`manager-ci.yml:153-178`) passed with no `::error`; the evidence is step `cargo test`
(`cargo test --workspace --locked`, `:117-120`).

| Row | Job(s) | Names / step |
|---|---|---|
| SC-001 to SC-022, SC-024, SC-026, SC-027, SC-030 (`cargo.all`, vitest) | `check (windows-latest)`, `check (macos-latest)`, `check (ubuntu-24.04)` | the 28 `cargo.all` names; the 17 vitest titles; steps fmt, clippy, clippy dev-feed, clippy-guard, cargo test, typecheck, lint, vitest |
| SC-001, 002, 003, 009, 011, 023, 028, 030 Unix rows (real POSIX filesystem: modes, links, directory renames, directory fsync) | `check (macos-latest)`, `check (ubuntu-24.04)` | the 8 `_unix` names |
| SC-029 Linux | `check (ubuntu-24.04)` | `install::tests::native_wiring_installs_linux` |
| SC-029 macOS | `check (macos-latest)` | `install::tests::native_wiring_offers_nothing_macos` |
| SC-029 Windows | `check (windows-latest)` (also local) | `install::tests::native_wiring_offers_nothing_windows` |
| Roadmap line 702 (`cargo test -p krate-manager-core install::`) | `check (ubuntu-24.04)` | the `install::tests::` subset of the same `cargo test` log |
| FR-116 | all three | `required tests ran` counts 276 + 12 / 44 / 39 cargo names and 46 vitest titles |

### 7.4 Local close procedure (Windows, this box)

From `apps/krate-manager/` (Git Bash), each command once, output to a log under the session scratchpad:
1. `cargo fmt --all --check`
2. `cargo clippy --workspace --all-targets --locked -- -D warnings`
3. `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` (steps 2 and 3 are also
   PD-30's check: both build `krate-manager-core` with `test-support` on and `cfg(test)` off, through `src-tauri`'s
   dev-dependency, so a `test_support.rs` that names `request_seam` outside `#[cfg(test)]` fails them)
4. `CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path tests/fixtures/clippy-guard/Cargo.toml --all-targets --locked -- -D warnings` (expected exit ≠ 0 with exactly the three disallowed-method errors, as the CI step counts; no lock change, Phase 9 adds no dependency)
5. `cargo test --workspace --locked` (default threads; record every `test result:` line and the wall clock; every binary `0 failed`)
6. `cargo tree -p krate-manager-core -e normal --locked --prefix none --format "{p}"` (no tauri / tokio / reqwest / hyper / dirs / home)
7. `npm run typecheck`, `npm run lint`, `npm test`
8. `node ../../tools/lint-manager-version.js`
9. from the repo root: `node --test tools/feed-contract/tests/` (parallel files by default)
10. from the repo root: `rg -n "commit point is the rename of new into" specs/Krate-manager-roadmap.md`, `rg -n "refuses to change the Linux plug-in root while any install manifest" specs/Krate-manager-roadmap.md`, `rg -n "orphan manifest" specs/Krate-manager-roadmap.md` (each one hit, SC-030)
11. `git diff --name-only` / `git status --short` against §2 (FR-120, FR-121)

Steps 2-4, 7-9 run while step 5 runs (separate processes; cargo serialises only shared build units). Optional early
signal, not evidence: in WSL, `cargo test -p krate-manager-core install::` with `CARGO_TARGET_DIR` on the WSL
filesystem (never `/mnt/f`, which drops modes and links), recorded as "WSL, informative"; the rows stay PENDING-CI.

---

## 8. Build integration and gate lanes

- Workspace: unchanged members (`core`, `helper`, `src-tauri`); `install` is a module of `krate-manager-core`.
- `package.json` scripts: unchanged (`typecheck`, `lint`, `test`).
- CI workflow files: **none change** (FR-121). `manager-ci.yml` already runs fmt, clippy, cargo test, typecheck, lint,
  vitest, `required tests ran` and `tauri build` on three legs; `feed-tools.yml` runs `manager-ci-shape.test.js`.
- Lanes: the app lane (§7.4 steps 1-8) and the pipeline lane (step 9). No C++ lane (no file under `plugins/`, `dsp/`
  or a C++ test changes, so `tools/header-consumers.js` / `tools/run-gate.js` have nothing to run).
- Cost: the largest Phase 9 case is SC-002 (28 cuts × a small archive, each on its own TempDir) and SC-009 (≈ 20 rows ×
  3 kinds); all run as parallel test threads inside the existing `cargo test` step.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| WebKitGTK quirks on Linux (the first leg where the buttons do something) | The UI adds only buttons, a confirm row and text through the same `invoke` + `Channel` path `download_plugin` already uses; no new web API, no dialog (FR-099 forbids a native one); the ubuntu leg runs `tauri build`; the Phase 12 graphics toggle covers the NVIDIA case |
| A DAW has the old `.so` mapped while Linux replaces the bundle | `rename` / `unlink` leave the open file alive until it is closed (`unlink(2)`); the DAW notice (FR-095) tells the user to rescan or restart; no process is detected or closed (Q7) |
| Windows file locks | Production installs on Windows are Phase 10's (the Inno installer); Phase 9 writes on Windows only in tests, inside a TempDir. Antivirus or the indexer can briefly hold a just-written `.krate-new` file and fail a `rename` with `PermissionDenied`; the in-process roll-back (FR-027) handles it as SC-009 does, and a test that sees it fails with a named step, never silently. If it flakes on `windows-latest`, the defect is recorded, not retried away |
| Admin prompts | None in Phase 9: Linux needs no elevation; nothing runs the helper or an installer |
| Offline | Install / Update reuse a cached verified artifact (Phase 7 FR-046) and the cached feed; without either the command fails before any write with the Phase 5 / 7 code; Remove works from the cached feed and the manifest (FR-094) |
| `~/.vst3` on another filesystem than the state dir | `.krate-new` / `.krate-old` sit beside the target (no `EXDEV`); the manifest and journal renames stay inside the state dir |
| A filesystem whose directory `fsync` fails (some FUSE / network mounts return `EINVAL`) | Surfaces as `E_INSTALL_IO { step, kind }` before the commit point, old install intact; §10 R-3 asks whether to tolerate it |
| Case-insensitive volumes (macOS default APFS) for presets differing only by case | C4 and Phase 8 already refuse such trees on case-insensitive filesystems (Phase 8 SC-014); production Linux (ext4 / btrfs) is case-sensitive |
| A crash while the journal says `staged` and the user edited a file meanwhile | Recovery acts only on journaled names, by kind, and refuses any other combination (FR-071 last row, FR-074); the plugin is blocked, others unaffected |
| Concurrent `download_plugin` of the same asset | `obtain_and`'s in-flight guard returns `E_DOWNLOAD_BUSY` unchanged; the per-plugin registry refuses a second install action (FR-085) |
| The test profile unwinds (`panic = "abort"` only in release, `Cargo.toml:58-66`) | Crash cuts are I/O errors after k calls (PD-8), never panics; SC-020's panics are caught, and its follow-up `for_command` for the same plugin is not `E_INSTALL_BUSY`, which proves the registry released on unwind (§7.1 SC-020) |
| A user saves a preset at a destination between the PD-26 re-read and the `rename` | The window is one `kind_of` / `hash_file` + `rename` on the user's own machine; `std::fs::rename` has no no-replace mode (§1.2), and `hard_link`-based no-replace is banned in `install.rs` (§4.8); recorded in the Review notes as the residual window |

---

## 10. Precondition table (for the user)

| Id | Question | Recommendation | Effect if overruled |
|---|---|---|---|
| R-1 | PD-1 / SI-1: let the command's download leg resolve the asset for the command's `os` / `arch` (two delegating functions added to `download.rs`) | **Yes** (production passes the native pair; it is the only way the OS-neutral driver gets Windows-local and macOS evidence through the commands) | The command-level SCs (SC-001, 007-009, 014, 021, 022, 027, 030) move to `_linux` names and close only as PENDING-CI on the ubuntu leg; FR-090 keeps its native wording minus the impossible feed |
| R-2 | PD-3 / SI-3: discard the downloaded artifact after `obtain_and` returns, and keep the action `Ok` if that discard fails | **Yes** (the install is committed; the next Phase 7 prune removes the leftover) | Discard inside `next` is impossible without changing `download.rs`'s guard; a failed discard turning the action into an error would report a failure for a completed install |
| R-3 | Directory `fsync` refused by the filesystem (`EINVAL` / `Unsupported`) | **Keep strict** (refuse, roll back, named step) | Tolerate those two kinds for `sync_dir` only, logged once; installs then succeed on such mounts with weaker crash safety |

---

## Review notes

- Measured this session: `versions.v2.schema.json:136-146` (format bound to the key prefix) and `contract.rs:12` (the
  schema compiled into `feed::parse`); `download.rs:1359` / `:1403` / `:1447` / `:1497-1503` (`next` under the in-flight
  guard, `discard` taking it); `download_tests.rs:2583-2650` (Phase 7's own seam split, `E_DOWNLOAD_URL` without it);
  `injection.rs:3359-3366` (`with_fixture_url` confinement); `status_tests.rs:977-989` (byte-for-byte goldens built
  through `assemble`, `:335-344`); `status.test.ts:260-269`, `:356-383` (text coverage, navigation needles, the
  non-test source list); `required-tests.json` counts 248 / 11 / 35 / 30 / 29.
- External docs fetched this session: `std::fs::rename`, `remove_dir_all`, `OpenOptions::create_new`,
  `File::sync_all`, `io::ErrorKind` (std 1.99.1); `FlushFileBuffers` (Microsoft Learn); Tauri "Calling the frontend"
  channels (v2.tauri.app).
- Revision 2 (plan review, 2026-11-01). Every review issue was applied; none was rejected. Where the review offered
  alternatives, these are the ones taken:
  - Off-Linux `for_command` rows: FR-090's order stays as it is (asset resolution in step 2, the resolver in step 3),
    and those rows get a feed with the OS's own asset key (§7 "off-Linux rows"), so the spec's 1 feed / 0 asset counts
    stand. Moving the resolver ahead would change those counts and FR-090's step order and gain nothing in production,
    where the feeds carry every OS's key. Read this revision: `download.rs:766-781` (`request_for` maps a missing key
    to `Unavailable`, code `E_DOWNLOAD_UNAVAILABLE` at `:444`), `test_support.rs:360-367` (darwin default `min_os`
    15.0, Windows none), `version.rs:200-240` (`platform_candidates`, `resolve`'s `min_os` gate).
  - `batch_runs_every_action` runs over `FaultyInstallIo(FsInstallIo)`, not `MemInstallIo`. Its assertion reads Beta's
    manifest through `detect::read_manifest`, which reads the real filesystem (`detect.rs:883-914`).
  - `request_seam` in `test_support.rs`: the builder is seam-free and initialises the field under `#[cfg(test)]` (PD-30,
    the `download.rs:775-776` pattern), instead of gating all of `InstallRig::command_env`. That keeps the rig usable
    under the `test-support` feature (`lib.rs:26-27`, `src-tauri/Cargo.toml:34`). Issues 16 and 17 describe the same
    defect, and this one decision resolves both.
  - PD-19's Windows link fallback was dropped rather than tested. Phase 9 never creates a link on Windows, so no leg
    could ever reach that code.
  - The FR-025 / FR-002 / FR-015 / PD-21 rows are collected in one driver-level test (`driver_rechecks_state`)
    attached to SC-008, and the FR-071 rows are attached to SC-013 (`recovery_table_rows`). FR-116's "every Rust test
    name of SC-001 to SC-029" therefore still covers them (SI-26).
  - `E_INSTALL_NOT_MANAGEABLE`: the review asked for a `for_command` row with that code, but `for_command` cannot reach
    it from a parsed feed. An entry without `install` has no `platforms` either (`dependentRequired`,
    `versions.v2.schema.json:162`; `StableRelease`, `feed.rs:55-62`), so FR-090 step 2 ends first with
    `E_DOWNLOAD_UNAVAILABLE` (`version.rs:217-219`, `download.rs:778`). Asserting `NOT_MANAGEABLE` there would make
    the row red on every leg. The code is therefore asserted where it is reachable: through `remove_for_command`
    (row 13) and at driver level (`driver_rechecks_state` (j)). The `for_command` row asserts its real code, and SI-26
    corrects the spec's edge-case sentence.
  - Residual risk, recorded here and not closed: PD-26 re-reads a sibling's destination just before its `rename`, but
    `std::fs::rename` replaces an existing `to` (§1.2). A user file written in the microseconds between the re-read and
    the rename is still replaced. Closing that window would need a no-replace rename
    (`renameat2(RENAME_NOREPLACE)`), which std does not offer, or `hard_link`, which `install.rs` bans (§4.8).
