# Implementation Plan: Krate Manager Phase 12 — Product Completion and First Release Readiness

**Spec:** `specs/krate-manager-phase12/spec.md` (DRAFT revision 3: decisions D1-D17, findings F-1 to F-8, clarifications
of 2026-11-02, `spec.md:1025-1064`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 12 (lines 894-949); Decisions Q3 (26), Q5 (28), Q6 (29), Q7 (30),
Q8 (31), Q10 (34), ROSTER (33); Reuse Inventory "Landing page" (200); edges 10→12, 11→12 (977-978); Not in v1
(988-1003); Cross-Cutting Constraints (1005-1046).
**Status:** DRAFT plan, revision 2, 2026-11-02 (review round 1 applied, see Review notes). Verified against `feat/krate-manager` at `7c4b6837`
(`git rev-parse --short HEAD`; untracked: `specs/krate-manager-phase12/` only). The spec was verified at `624f0d87`;
`git diff --stat 624f0d87 7c4b6837` touches only `.github/workflows/feed-tools.yml` (two `.gitattributes` path lines,
`:26`, `:49`), so every spec citation holds except that the `feed-tools.yml` path lists now run to `:26` / `:49`.
**Lanes:** the **app lane** (`cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `cargo test --workspace`,
`npm run typecheck`, `npm run lint`, `npm test`, `node --test "scripts/*.test.js"`) and the **pipeline lane**
(`node --test "tools/feed-contract/tests/*.test.js"` with `feed-tools.yml`'s TAP check replayed locally, YAML parse of
`release.yml`, `release-manager.yml`, `docs.yml`, `feed-tools.yml`). No C++, CMake, plugin or DSP file changes
(FR-111): `node tools/header-consumers.js` reports `none` and no `run-gate.js` lane runs.

**Build preconditions.** SI-1 and SI-2 (§0.2) are **blocking**: built as the spec reads, SI-1 leaves a third
generator (`docs.yml`) that rewrites `index.html` without the card on every docs push, including the push that adds
the card template, and SI-2 lets a batch of 33 to 64 updates raise the UAC / admin prompt and then fail every item at
the helper's job check. SI-3 (a `needs` that cannot read the version) is a hard CI failure. §10 lists the three
decisions that are the user's (P-1 to P-3); the plan is written to their recommendations.

---

## 0. What this plan adds to the spec

The spec fixes requirements, names, codes and test names. This plan fixes: how the single-item check is split so the
batch reuses it without a second feed request, the stage-2 thread model (scoped workers, one channel back to the
calling thread so the progress callback never crosses a thread), the batch result types and their webview view, the
orphan-manifest reader shared by Remove, the status view and the settings lock, the settings command's race with
`release_for_restart` (Phase 11 R-9), the exact `lib.rs` wiring of the graphics variable, the Node port of the landing
generator (byte rules), the new tools' CLIs, every workflow edit, every guard and pinned list that widens, and the
per-SC test design. Where the tree contradicts a spec sentence the conflict is in §0.2.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **Module map.** Core batch code stays in `crates/core/src/install.rs` (beside `for_command`, whose helpers it shares); its tests in a new `crates/core/src/install_batch_tests.rs`, mounted from `install.rs` as `#[cfg(test)] #[path = "install_batch_tests.rs"] mod batch;`, so every case is `install::batch::<name>` and `cargo test -p krate-manager-core batch::` selects them (SC-002 to SC-010, SC-027). The scripted-effect helpers of `install_tests.rs` (`scripted`, `entries`, `result_doc`, `all_done`, `done_after`, `install_tests.rs:4603-4656`) become `pub(super)` and gain `ops_after` (PD-20). Orphan code in `install.rs`, its cases in `install_tests.rs` (`install::tests::orphan_remove_cases`, `not_manageable_is_not_orphan`). Settings command in `settings.rs` (+ `settings_tests.rs`), `apply_settings_with_os` in `context.rs`, graphics functions in `platform/mod.rs` (+ `platform/mod_tests.rs`), orphans in `status.rs` (+ `status_tests.rs`). UI: `src/status.ts` (Update all, orphan cards), new `src/settings.ts` + `src/settings.test.ts`, `src/view.ts`, `src/main.ts`. | `*_tests.rs` is the core convention the scans skip (`injection.rs:1517-1519`, `:1676-1683`; `roster.rs:80`; `config_guard.rs:366`) | New components (adds `install_batch_tests.rs`, `settings.test.ts`) |
| PD-2 | **The shared per-item check is two functions, not one.** (a) `download.rs` gains `pub fn request_in_feed(feed: &Feed, name: &str, expected_version: &str, host: &dyn HostFacts, os: Os, arch: Arch) -> Result<DownloadRequest, DownloadError>`: the body of `command_request_for` after its snapshot (`download.rs:1554-1583`: `host.os_version()`, `request_for_platform`, the `VersionChanged` compare); `command_request_for` becomes `feed::check` + `request_in_feed` (byte-for-byte the same results). (b) `install.rs` gains `fn check_item<'a>(env: CommandEnv<'a>, feed: &Feed, request: DownloadRequest, kind: ActionKind, host: &dyn HostFacts) -> Result<Checked<'a>, InstallError>`: steps 2-4 of `install_command` after the request (`install.rs:4002-4058`: entry, install facts, `Busy::enter`, `Destinations::of`, `driver_for`, detection, `action_gate`, `allowed_actions`, refusal table). `install_command` keeps its action word and its `command_request_for` call and then calls `check_item`; `Checked` drops its `feed` field (the single path keeps the feed beside it). The batch makes one `feed::check` and per item `request_in_feed` + `check_item`. | FR-005 asks one shared function; the single path's codes must not move (FR-019): a moved version stays `E_DOWNLOAD_VERSION_CHANGED` (`install_tests.rs:2249-2251`, SI-4) and a missing snapshot stays `E_DOWNLOAD_NO_FEED` there | FR-005 (implementation) |
| PD-3 | **Stage 2: scoped workers, one channel to the calling thread.** `std::thread::scope` (Rust 1.63+, "All threads spawned within the scope that haven't been manually joined will be automatically joined before this function returns", https://doc.rust-lang.org/std/thread/fn.scope.html, read for Rust 1.99.1) spawns `min(policy.parallel_downloads, accepted)` workers. Each pulls the next item index from an `AtomicUsize`, builds `DownloadEnv { ctx: env.ctx, io: staging, policy: env.download.policy, cancel: env.download.cancel }` and calls `download::obtain_and(env, &feed, &request, &mut \|p\| tx.send(Msg::Progress(i, p)), \|a\| a)`, then sends `Msg::Done(i, result)`. The calling thread receives until it has one `Done` per accepted item and forwards each `Progress` as `BatchProgress::Item { name, event: InstallProgress::Download { event } }` to `progress`, so the `&mut dyn FnMut` callback, `HostFacts` (`FakeHostFacts` is `RefCell`-based, `test_support.rs:283-291`) and `HostIo` never cross a thread. Sharing is sound: `AppContext` (its `PacHostHandle` is `Arc<dyn PacHost>` with `PacHost: Send + Sync`, `pac.rs:70`, `:110`), `DownloadPolicy`, `CancelToken` (an `AtomicBool`, `http.rs:1223`), `Feed` and `DownloadRequest` are plain data; `VerifiedArtifact` and `DownloadError` are `Send` data (`verify.rs:114-123`, `download.rs:362-363`). A worker panic propagates when the scope joins (same page: "If any of the automatically joined threads panicked, this function will panic"), which `spawn_blocking` reports as `E_DOWNLOAD_TASK` (`commands.rs:162-167`). | `DownloadEnv` "is not required to be `Send`: a thread builds its own from shared parts" (`download.rs:204-208`); the Phase 7 parallel tests already run `obtain_and` from scoped threads (`download_tests.rs:2243-2300`) | FR-008, FR-017 |
| PD-4 | **One staging seam on the batch path.** `batch_for_command` uses its `staging: &(dyn StagingIo + Sync)` for every staging call, the workers' `DownloadEnv.io` and the stage-3 `download::discard(artifact, staging)` alike; `env.download.io` is not used by the batch. Production passes `&FsStagingIo` (`download.rs:568`) as both `CommandEnv.download.io` and `staging`; tests pass the rig's `FaultyStagingIo` (Sync: `Arc<OpCounters>` of atomics, `test_support.rs:667-704`) as both. | `dyn StagingIo` has no `Sync` bound (`download.rs:552`), so `env.download.io` cannot be shared with workers; a second seam used for discard only would let a test fault the wrong one | FR-008, D6 |
| PD-5 | **Batch types.** `#[derive(Clone, Debug, PartialEq, Eq, Deserialize)] #[serde(rename_all = "camelCase", deny_unknown_fields)] pub struct BatchItem { pub name: String, pub expected_version: String }` is the command argument itself (FR-060's `BatchItemArg` is this type; serde `rename_all` "camelCase" and `deny_unknown_fields`, https://serde.rs/container-attrs.html, so the webview's `{ name, expectedVersion }` and nothing else deserializes). `pub struct BatchPolicy { pub parallel_downloads: usize }` with `pub const PRODUCTION: BatchPolicy = BatchPolicy { parallel_downloads: 3 }`. `pub const MAX_BATCH_ITEMS: usize = job::MAX_OPS` (SI-2). `#[derive(Clone, Debug, PartialEq, Eq, Serialize)] #[serde(tag = "kind", rename_all = "snake_case")] pub enum BatchProgress { Item { name: String, event: InstallProgress }, Elevating }`. `pub struct BatchItemResult { pub name: String, pub outcome: Result<ActionSummary, InstallError> }`; `pub struct BatchSummary { pub results: Vec<BatchItemResult>, pub notice: Option<Notice> }` with `pub fn view(&self) -> BatchSummaryView`, where `#[derive(Serialize)] BatchSummaryView { results: Vec<BatchItemView>, notice: Option<Notice> }` and `BatchItemView { name: String, summary: Option<ActionSummary>, error: Option<String> }` (exactly one of `summary` / `error` is `Some`; `error` is `InstallError`'s `Display`, code first). Core tests assert on `InstallError` values; the webview receives the view. | One serialisable shape the TS parser can check with an exact key set (`view.ts:129-136`); `InstallError` is `Clone` (`install.rs:109`) but not `Serialize` | FR-001, D7, FR-060 (SI-10) |
| PD-6 | **`batch_for_command` steps.** (0) FR-002 input checks (`E_INSTALL_BATCH { reason }`: `"no items"`, `"more than 32 items"`, `"a plugin is named twice"`); (1) `self_update::action_gate` (FR-004); (2) one `feed::check(env.ctx, FetchMode::Normal, now_unix)`, no snapshot → `Err(InstallError::NoFeed { cause })` for the batch (as `command_remove`, `install.rs:4169-4179`); `env.drivers.driver_for(env.os)` → `None` is `Err(NoDriver)` for the batch (checked once, before any item); (3) per item `request_in_feed` (its `DownloadError` wrapped `InstallError::Download`), the `#[cfg(test)] env.request_seam` applied as `obtain_and_run` does (`install.rs:4078-4082`), then `check_item(…, ActionKind::Update, …)`; an `Err` is that item's result and an `info!` refusal line (`log_refusal` form, `install.rs:4282-4299`); (3b) the stage boundary: `#[cfg(test)] if let Some(seam) = env.stage_seam { seam(BatchStage::ChecksDone) }`, called exactly once, after the last item's check and before the scope spawns any worker (so before any `obtain_and`, whose `create_dir_all` / `prune` / `Started` come first in every worker, `download.rs:1418-1424`, `:1046-1049`); `stage_seam: Option<&'a dyn Fn(BatchStage)>` is a new `#[cfg(test)] pub(crate)` field of `CommandEnv` beside `request_seam` (`install.rs:543-545`), `None` in `native_env` (`:568`) and in the rig (`test_support.rs:3313`), and `#[cfg(test)] enum BatchStage { ChecksDone, DownloadsDone }`; the `request_seam` guard (`fixture_seam_findings`, `injection.rs:3914`, rule `:3971-4035`, "allowed only in install.rs and install_tests.rs and as a #[cfg(test)] initialiser in test_support.rs") is applied to `stage_seam` by the same rule, and both names are also free in `install_batch_tests.rs` (the guard's `INSTALL_TESTS_RS` arm, `:4020`, becomes the two test files; its plants gain one `stage_seam` plant per rule); (4) stage 2 (PD-3) over the accepted items, a failed `obtain_and` is that item's result (its own `warn!` comes from `obtain_and`, `download.rs:1361-1374`, plus the item's refusal-form `info!`); (4b) after the scope joins, `#[cfg(test)]` `seam(BatchStage::DownloadsDone)` once, before any driver run; (5) stage 3: Windows / macOS one `checked.driver.run(InstallEnv { …, progress: &mut map }, actions)` with one `Action::Update { entry, artifact }` per verified item in item order, `map` turning `InstallProgress::Elevating` into `BatchProgress::Elevating` and any other event of that run into `Item { name, event }` of the op's item (the elevated driver emits only `Elevating`, `install.rs:1353`); Linux one `driver.run(…, vec![action])` per verified item in item order on the calling thread, `map` = `Item { name, event }`; each driver result's `Ok(Outcome)` becomes `ActionSummary { name, action: Update, version, kept, notice: Some(os notice) }` exactly as `obtain_and_run` builds it (`install.rs:4128-4140`), an empty driver result `NoDriver` (`:4114-4118`); (6) per updated item `download::discard(artifact, staging)` (a failure is the `warn!` of `:4125-4127`, not a result); failed items' artifacts stay (D6); (7) the batch `info!` line (FR-020) and `BatchSummary { results in request order, notice }`. Every accepted item's `Checked` (and so its `Busy`) lives in one `Vec` until the function returns (FR-007). The function has 7 parameters, the ceiling `clippy::too_many_arguments` allows and `injection.rs` never lets be suppressed (`test_support.rs:3279-3281`). | Spec D2-D7, FR-001 to FR-020 | FR-001 to FR-020 |
| PD-7 | **Orphan manifests: one bounded reader, three users.** `install.rs` gains `pub(crate) const MAX_MANIFEST_ENTRIES: usize = 256`, `pub(crate) struct ManifestHead { pub stem: String, pub plugin: Option<String>, pub version: Option<String> }` and `pub(crate) fn manifest_heads(io: &dyn InstallIo, state_dir: &Path) -> Result<Vec<ManifestHead>, InstallError>`: `<state>/manifests/` absent → `Ok(vec![])`; not a directory → `Manifest { reason: NOT_A_DIR }`; `io.read_dir` (`install.rs:1442`) with more than 256 entries → `Manifest { reason: "too many manifests" }`; each entry that is `NodeKind::File`, ends in `.json` and does not start with `.` (the atomic-write temp is `.<name>.tmp`, `install.rs:2134`) is read with `io.read_file(path, detect::MAX_MANIFEST_BYTES)` and parsed as a JSON object for its string `plugin` and `version` (each `None` when absent or not a string, `manifest_version` 1 required for `plugin` to count); unreadable or unparsable files give a head with both `None`. Users: FR-050 / FR-051 (orphan Remove: heads whose `plugin == Some(name)`), FR-052 (status orphans: heads whose `plugin` is no snapshot key and whose `version` is `Some`), FR-041 (the root lock: every head, named by `plugin` or by `stem`). | `InstallIo` is the injected, `Sync` seam every core path reads through (`install.rs:1437-1459`); one reader keeps the 256 bound and the temp-file rule in one place | FR-041, FR-050, FR-052 |
| PD-8 | **Orphan Remove does not go through `InstallDriver`.** `Action::Uninstall` needs a `PluginEntry` (`install.rs:320-322`), which an orphan does not have. `run_uninstall` (`install.rs:3584-3626`) is split into `run_uninstall(ctx, io, progress, entry)` = `Destinations::of` + `run_uninstall_at(ctx, io, progress, &dest)` (the rest, unchanged). The orphan branch of `command_remove` (Linux, `name` not a snapshot key): `manifest_heads` → exactly one match (else the FR-050 codes) → `Destinations::from_parts(ctx, &plugin, &manifest.vst3_bundle, None)` with the head's `stem` required equal to `dest.stem` (`E_INSTALL_MANIFEST { reason: "the manifest file name is not its bundle's" }`), `Busy::enter`, then `run_uninstall_at` (which does `check_state_dirs`, the pending-journal refusal, `read_manifest_full`, `roots_match`, `resolve_entries`, `remove_entries`, `install.rs:3591-3625`), then `log_result` (`install.rs:4257-4279`), as `LinuxDriver::run` does for one action (`install.rs:432-475`). `vst3_bundle` comes from the head's manifest, read again by `read_manifest_full`, which checks `m.vst3_bundle == "<stem>.vst3"` (`check_manifest`, `install.rs:2014-2018`). `preset_subdir` is `None`: removal is manifest-relative to `preset_root` (`remove_entries`, `install.rs:3654-3655`), so a plugin whose factory presets sat under `<P>/Kits` loses nothing. | The driver adds nothing on Linux but the loop; constructing a fake `PluginEntry` would invent feed facts the spec forbids (FR-051 "uses no feed field") | FR-050, FR-051 |
| PD-9 | **The settings save cannot leave a second lock `Arc` alive (Phase 11 R-9).** `apply_settings_with_os(os, shared, settings)` takes the **write** guard once, computes `reassemble_with_os(os, &guard, settings)` under it and assigns it, so the cloned `Arc<StateLock>` (`context.rs:165`) and the old one never coexist outside the guard, and `release_for_restart`, which takes the same write guard (`context.rs:245-259`), can never observe `StillShared` because of a save. `apply_settings` (`context.rs:198-204`) becomes the `NativePlatform.os()` wrapper over it; its doc line "the read guard is dropped before the write guard is taken" becomes "one write guard, so no reassembled lock clone outlives it". FR-042's `self_update::action_gate` check stays the first refusal. | Phase 11 plan R-9 (`specs/krate-manager-phase11/plan.md:1609`): "once Phase 12's settings screen calls `apply_settings`"; PD-10 there proves `reassemble_with_os` is the only clone (`plan.md:63`) | FR-043 (implementation; SI-8) |
| PD-10 | **Settings command errors.** `#[derive(Debug, thiserror::Error)] pub enum SettingsCommandError { #[error("E_SETTINGS_ROOT_IN_USE: Remove {} before changing the plug-in folder", names.join(", "))] RootInUse { names: Vec<String> }, #[error("E_SETTINGS_BUSY: an install, download or self-update is running; try again when it finishes")] Busy, #[error("E_SETTINGS_UNSUPPORTED_OS: settings exist on Linux only")] UnsupportedOs, #[error(transparent)] Settings(#[from] SettingsError), #[error(transparent)] Startup(StartupError) }` with `code()`. With no startup context (`AppState.context` an `Err`), view and save return `Startup(e)` (code `E_STARTUP_*` first, `context.rs:28-48`): the spec names no code for it and the context error already has one (SI-17). | FR-044 "error texts begin with their codes" | FR-040 to FR-044 |
| PD-11 | **Settings save order, and the root gate that excludes plugin commands.** *The gate* (in `settings.rs`): `static ROOT_GATE: Mutex<BTreeMap<PathBuf, Gate>>` and `static ROOT_GATE_FREE: Condvar`, keyed by the state dir as `BUSY` is (`install.rs:3890`), with `struct Gate { commands: usize, saving: bool }` (`Mutex::new`, `Condvar::new` and `BTreeMap::new` are `const`, https://doc.rust-lang.org/std/sync/struct.Condvar.html#method.new, Rust 1.99.1 docs; poisoning read through `PoisonError::into_inner` like `BUSY`, `install.rs:3902`). `pub fn command_context(state: &SharedState) -> Result<(RootTicket, AppContext), StartupError>`: (i) read guard → the context's state dir, or the `StartupError` clone (guard dropped); (ii) lock `ROOT_GATE`, `ROOT_GATE_FREE.wait_while(…, \|g\| g[dir].saving)` (https://doc.rust-lang.org/std/sync/struct.Condvar.html#method.wait_while), `commands += 1`, unlock; (iii) read guard → `ctx` clone. `#[must_use] pub struct RootTicket { state_dir: PathBuf }` (plain data, so `Send`) decrements `commands` in `Drop` and `notify_all`s. The state dir is the same across every reassembly (`reassemble_with_os` re-derives only the configured plug-in root, `fold` → `with_configured_plugin_root`, `context.rs:150-152`, `:183-190`), so the key never moves. `src-tauri`'s `install_plugin`, `remove_plugin` (`commands.rs:122-125`, `:180-183`) and `update_all` replace their `let ctx = { guard.context.clone() … }` block with `let (ticket, ctx) = settings::command_context(&state).map_err(\|e\| e.to_string())?;` and move `ticket` into their `spawn_blocking` closure (`let _ticket = ticket;` first), so it is held from before the clone until the core call returns; the error text is the same `StartupError` `Display` as before, so FR-019's single path is unchanged in results. The wait in (ii) lasts at most one save's steps 3-11 below (local reads, one atomic write, one reassembly, no network). Self-update and `download_plugin` take no ticket: neither writes under the plug-in root, the save refuses while either runs (step 5), and PD-9 closes the restart race. *The save* `save_for_command_with_os(os, state, root, graphics)`: (1) `os != Linux` → `UnsupportedOs`; (2) read guard → the state dir, or `Startup` (guard dropped); (3) `SaveGate::try_enter(dir)`: `None` when `commands > 0` or `saving` → `Busy`, else `saving = true` until the `SaveGate` drops at return (so two saves never interleave, and no plugin command holds a context cloned before this save); (4) read guard → the authoritative `ctx` clone (taken under the gate, so a save that finished in between is seen); (5) busy: `self_update::action_gate(state_dir).is_err() \|\| install::any_busy(state_dir) \|\| download::any_in_flight(&staging_dir.join(DOWNLOADS_DIR))` → `Busy` (the same in-flight path as `self_update_busy`, `self_update.rs:354-360`); (6) `next = Settings { linux_plugin_root: root.map(PathBuf::from), linux_graphics_workaround: graphics }`; (7) when `next.linux_plugin_root != ctx.settings.linux_plugin_root`: `manifest_heads` non-empty, or `<state>/journal/` holding any `NodeKind::File` ending `.json` → `RootInUse { names }` (heads' `plugin` or `stem`, then journal stems, sorted, de-duplicated; a `manifest_heads` error names `"manifests"`); (8) `settings::validate(&next)`; (9) `settings::save(&ctx.roots, &next)` (atomic, refuses over a newer file, `settings.rs:279-307`); (10) `apply_settings_with_os`; (11) return `view_with_os(os, state)`. Steps 4-8 make no write; a refusal at any step leaves the file and the context unchanged. *Validation is host-lexical:* `settings::validate` (`settings.rs:101-122`, "Lexical only") uses the host's `Path::is_absolute` / `components`, not the forced `Os`, so a forced `Os::Linux` on Windows judges a root by Windows path rules; tests therefore build every accepted root from the temp dir (host-absolute) and assert the rejections with host-appropriate values (relative `rel/vst3`; `temp.join("..").join("x")`; the filesystem root `temp.ancestors().last()`), never with a `/opt/…` or `C:\…` literal. | FR-041 to FR-043 ("the root check only when the root changes, compared as stored values"); without the gate an install that cloned its context before a root-changing save (`commands.rs:122-125`, then a network feed request before `Busy::enter`, `install.rs:4002-4022`) writes into the old root and records it, after which Remove fails `roots_match` (`install.rs:2057-2082`, `E_INSTALL_ROOT_CHANGED`) and FR-041 refuses the switch back (review round 1) | FR-042, FR-043 (SI-21) |
| PD-12 | **The graphics variable: literals in `platform/mod.rs`, the write in `lib.rs`.** `platform/mod.rs` gains `pub const GRAPHICS_ENV: (&str, &str) = ("WEBKIT_DISABLE_DMABUF_RENDERER", "1");`, `pub fn graphics_env(os: Os, settings: &Settings) -> Option<(&'static str, &'static str)>` and `pub fn apply_graphics_env(os: Os, settings: Option<&Settings>, set: impl FnOnce(&str, &str))`. `injection.rs`'s `PLATFORM_LITERALS` (`:51-93`, "Every string literal a non-test `platform/` file may hold outside an attribute") grows to 42 with those two strings under a `// Phase 12 FR-045` comment (SI-6). `lib.rs`, after `let sink_roots = …;` (`:48`) and before `let inputs = proxy_env::read();` (`:58`): `#[cfg(target_os = "linux")] let loaded = roots.as_ref().ok().map(krate_manager_core::settings::load).and_then(\|o\| o.error.is_none().then_some(o.settings));` then `#[cfg(target_os = "linux")] krate_manager_core::platform::apply_graphics_env(NativePlatform.os(), loaded.as_ref(), \|name, value\| { // SAFETY: single-threaded: before the first thread (sysproxy.rs:210 can spawn one from :61) \n unsafe { std::env::set_var(name, value) } });` (the `// SAFETY:` form of `os_elevation.rs:85-88`). A `cfg` attribute on an expression statement is the form `lib.rs:108-109` already uses. `std::env::set_var` is `unsafe` in edition 2024 (`Cargo.toml:8`; https://doc.rust-lang.org/std/env/fn.set_var.html, spec External facts). It is not a `clippy.toml` disallowed method (`clippy.toml:1-10`) and `env :: set_var` is not an `ENV_READS` token sequence (`injection.rs:621-628`), so neither guard needs a suppression. | D9, FR-045, FR-046; the literal rule of `injection.rs:1428` would otherwise fail on the two strings | FR-045, FR-046 |
| PD-13 | **Status orphans.** `StatusView` gains `pub orphans: Vec<OrphanView>` as its last field, `#[derive(Clone, Debug, PartialEq, Eq, Serialize)] pub struct OrphanView { pub name: String, pub version: String }`. `build_with_os` fills it (Linux, snapshot present, `manifest_heads` through its `io`, `status.rs:211-219`); any error → empty. `assemble` / `assemble_with_gates` stay pure and put `orphans: Vec::new()`; `build_with_os` sets the field after assembling. The 13 status goldens (`tests/fixtures/status/*.json`, "which no test writes", `status_tests.rs:5-6`) gain `"orphans": []` as their last key, by hand, and `toStatusView` its key (SI-9). | FR-052; the goldens are byte-compared and parsed with an exact key set (`view.ts:311-312`) | FR-052, FR-026 |
| PD-14 | **Settings section mounts only after a Linux status.** `StatusSection` gains `readonly lastOs: OsName \| null` (set by `render`) and `refresh(): Promise<void>` (the existing `host.refresh`, `status.ts:562-570`). `main.ts`, in `mountStatus`'s `afterSettle` path, calls `invoke("settings_view")` once, the first time `lastOs === "linux"`, and mounts `createSettingsSection(invoke, () => status.refresh())` below the status section. On Windows and macOS no `settings_view` call is made, so the exact call lists `main.test.ts` pins (`:57`, `:73`, `:110`, `:155`) stay true (their fixtures are `windows`: `fresh-all-states`, `installed-elsewhere`, `no-feed`, imported at `main.test.ts:3-5`). Because `settings.test.ts` exercises `createSettingsSection` alone, the Linux-only mount and the re-read wiring are proven in `main.test.ts` by three new cases over existing status goldens: "main_mounts_settings_on_linux" (`cached-stale.json`, `"os": "linux"`: exactly one `settings_view` invoke, after `plugin_status`, and one `section.settings` in the shell), "main_skips_settings_on_macos" (`macos-au-copies.json`, `"os": "macos"`: no `settings_view` invoke, no `section.settings`) and "main_rereads_status_after_root_change" (Linux; a fake `settings_save` returning a view whose `linux_plugin_root` differs from the first: exactly one further `plugin_status` invoke; a save that keeps the root: none). | D8 (Linux only); `settings_view` on every OS would change four existing assertions for nothing | FR-047, FR-048 |
| PD-15 | **Batch UI state.** `createStatusSection` keeps `batchRunning` beside `actionsEnabled`; the card host's `actionsEnabled()` is `actionsEnabled && !batchRunning`; `setActionsEnabled` and the batch both recompute every card button, `Check now` and the Update-all control. The Update-all control is also disabled while any `div.actions.running` row exists (a single action is running), so a batch never starts over a running item from the page. `Check now` is disabled during a batch: a re-render would drop the listed cards' progress lines. | D5 ("every action button and the Update-all control"); `setActionsEnabled` already disables `Check now` (`status.ts:587-592`) | FR-023 |
| PD-16 | **The landing generator: `tools/gen-root-index.js`, Python-exact.** `main(argv, io, deps)` with a required `deps` (`{ gitTags(pattern) -> string[], repo() -> string \| undefined, readFile, readDir, isFile, writeFile }`; `productionDeps()` runs `childProcess.execFileSync('git', ['tag', '-l', pattern, '--sort=-v:refname'], { encoding: 'utf8' })`, no shell), the `tools/feed-publish.js` pattern (`feed-publish.js:8-9`, `:39`). Byte rules, each matching the Python of `release.yml:800-847` / `docs.yml:133-179`: plugin dirs are `plugins/*/version.json` sorted by code unit (ASCII names: equal to Python `sorted`); per plugin the first line of `gitTags("<dir>/v*")`, skipped when empty; version `tag.split('/v').pop()`; `has_page` = `plugins/<dir>/docs/index.html` is a file; each card the six-line string of `release.yml:823-836`, joined with `\n`; the template read and normalised `\r\n?` → `\n` (Python text mode reads universal newlines; this checkout holds it CRLF, `git ls-files --eol`: `i/lf w/crlf`); `{{PLUGIN_CARDS}}` then `{{REPO}}` replaced with `split(…).join(…)` (all occurrences, as `str.replace`, and no `$` pattern expansion as `String.prototype.replace` would do); written UTF-8. The manager card (D13) is rendered with `{{MANAGER_VERSION}}` and `{{REPO}}` filled, normalised to LF, its trailing newline trimmed, and prepended as `card + '\n' + pluginCards`; with no manager version the output equals the plugin-only output exactly. `newestManagerVersion(tags)` keeps `^manager/v(\d+)\.(\d+)\.(\d+)$` and compares numerically (SC-029). The CLI reads tags with `gitTags('manager/v*')`. `REPO` comes from `deps.repo()` (`productionDeps()` returns the process environment's `REPO`; tests inject it), the Python's `os.environ['REPO']`, which raises on a missing key; a missing or empty value is `E_ROOT_INDEX_REPO`, exit 1, before any file is read, so no page with `https://github.com/undefined/…` links can be written. Hygiene: the file is added to `CHILD_EXEMPT`, `scannedFiles()` and its completeness list (`hygiene.test.js:34-46`, `:74-81`, `:119-134`). | FR-081, SC-029; `hygiene.test.js:149` refuses `child_process` outside the exempt set; the write guard only allows `git init/commit/update-ref/tag` under tmp (`helpers/write-guard.js:193-207`), so tests inject `gitTags` | FR-081, D13 |
| PD-17 | **The manager card fragment.** `docs/manager-card-template.html`: one `<div class="plugin-card manager-card">` with `<h2>Krate Manager</h2>`, a description paragraph, `<span class="version">v{{MANAGER_VERSION}}</span>`, a `<ul>` of the four installer links (FR-081 names), a "Supported systems" paragraph and a `<details><summary>First launch</summary>…</details>` holding every non-null statement of the contract file, one `<p>` each, HTML-escaped, plus the README link. No `<script>`, no inline style, no new CSS (the root `style.css` is not changed: the card reuses `.plugin-card`). Indented with 16 spaces like the plugin cards. | FR-081, FR-082, D12 | FR-081 |
| PD-18 | **Statements carry no URL, repository or version placeholder.** Each `text` is plain text; where a version is meant it is written `<version>`; the repository is written literally (`rolandzwaga/krate-audio`, `MANAGER_REPO`, `self_update.rs`). So the README (Markdown) and the card (HTML, escaped) can each contain every `text` verbatim under SC-016a's normalisation (tags removed, then the five entities decoded, then whitespace collapsed; README code-span backticks removed, whitespace collapsed). The macOS one-liner (post-U-1) is written in that form. | A `{{…}}` in a statement could never appear in the README; the card template still holds `{{REPO}}` / `{{MANAGER_VERSION}}` in its `href`s only | FR-084, D12 |
| PD-19 | **`manager-release-preflight.js` gains `E_PREFLIGHT_RUSTSEC` and `--run-date <YYYY-MM-DD>`** (optional; default the UTC date of `new Date()`). Checked after `E_PREFLIGHT_TAG_EXISTS` and before `E_SIGNING_CONFIG` (`manager-release-preflight.js:225-242`), on dry and real runs: `<app-dir>/RELEASING.md` section `## RustSec` must hold a Markdown table `\| version \| date \| rquickjs \| rquickjs-core \| rquickjs-sys \| result \|` with exactly one row for the version; its three crate versions equal the `version = "…"` lines after `name = "rquickjs"`, `"rquickjs-core"`, `"rquickjs-sys"` in `<app-dir>/Cargo.lock` (`Cargo.lock:3107-3130`); `result` is `no advisory` or matches `^RUSTSEC-\d{4}-\d{4}(, RUSTSEC-\d{4}-\d{4})* answered by bump to rquickjs (\S+), rquickjs-core (\S+), rquickjs-sys (\S+)$`, each captured version equal to that crate's own `Cargo.lock` version (and so to the row's cell for it), so the three crates are compared one by one even when they lock at different versions; `0 ≤ runDate − date ≤ 7` days. The header comment's code list and `USAGE` gain them. The existing preflight tests' app-dir builder (`manager-release-preflight.test.js:35-78`) copies a valid `RELEASING.md` and `Cargo.lock` so every earlier case keeps its code. | FR-104; the tool is offline and reads only files (`:21`) | FR-104 |
| PD-20 | **Scripted helper effects per op.** `install_tests.rs` gains `pub(super) fn ops_after(effects: Vec<(&'static str, Box<dyn Fn() + Send>)>) -> Scripted`: `Exited(0)`, and for op *i* runs `effects[i].1()` and writes outcome `effects[i].0` (`"done"` or `"locked"`; a `locked` op also gets `code: "E_HELPER_LOCKED"` and `exit_code: 5` as the Phase 10 mapping reads them, `install.rs:1241`). A `done` effect writes the new-version bundle under the vst3 global root (`moduleinfo.json` at the entry's version) and the fake installer record into the test's `LiveHostFacts` (`test_support.rs:362-404`), so `confirmed` passes (`install.rs:1396-1422`). | Spec "Scripted helper effects" paragraph; `done_after` writes every op `done` (`install_tests.rs:4643-4655`) | SC-002, SC-005, SC-006, SC-008, SC-027 |
| PD-21 | **A gate server for SC-003.** `fixture_server.rs` (test-support only, `lib.rs:9-10`) gains `Reply::Held { gate: HoldGate, reply: Box<Reply> }` and `#[derive(Clone)] pub struct HoldGate(Arc<HoldState>)` (`Debug` by hand) with `open() -> Vec<String>` (paths whose response is held), `wait_open(n, timeout) -> bool`, `release_one() -> Option<String>` (releases the oldest held path) and `release_all()`. The connection thread records the path as open, waits on the gate's `Condvar` in `POLL` slices that also check the server's stop flag (`fixture_server.rs:78`, `:100-113`), then answers with `reply`. `Drop` of the server releases every hold. | No existing `Reply` can be held and released by the test (`fixture_server.rs:17-63`); the shutdown rule (every wait ends on Drop, `:4-6`) must hold for the new wait too | SC-003 |
| PD-22 | **The release.yml and docs.yml snapshots.** `tools/feed-contract/fixtures/landing/` (covered by `tools/feed-contract/fixtures/** -text`, `.gitattributes`) holds `release.yml.pre-phase12` and `docs.yml.pre-phase12` (the two files at the base commit), the fixture plugin tree for SC-029 and `root-index.expected.html` (SI-14). `workflow-shape.test.js` asserts each live workflow equals its snapshot with exactly the PD-23 / PD-24 edit applied (the inline Python replaced by the Node call, the `setup-node` step inserted), byte for byte after LF normalisation. | SC-029 "every other step of release.yml equals a committed snapshot" names no snapshot that exists (SI-13) | SC-029 |
| PD-23 | **release.yml edit.** In `deploy-pages` (`release.yml:731-855`): a new step `- name: Setup Node` / `uses: actions/setup-node@v4` / `with: node-version: '24'` directly before "Generate Root Index", and in that step the lines from `# Generate plugin cards…` through `PYEOF` (`:798-847`) become `node tools/gen-root-index.js --out _site/index.html`. `DEPLOY_PAGES_STEPS` (`workflow-shape.test.js:729-735`) gains `'Setup Node'` before `'Generate Root Index'`. | FR-081; the step-name list is pinned | FR-081 |
| PD-24 | **docs.yml edit (SI-1, ruling P-1).** In `build-and-deploy` (`docs.yml:19-245`): a `- name: Setup Node` / `uses: actions/setup-node@v4` / `node-version: '24'` step after "Install Pandoc", and inside "Build Manuals and Landing Pages" the `python3 << 'PYEOF'` … `PYEOF` block (`docs.yml:133-179`) becomes `node tools/gen-root-index.js --out _site/index.html`; the `if [ -f "docs/index-template.html" ]` wrapper, the asset copies and "Verify download links" (`:189-230`) are unchanged. Once a `manager/v*` tag exists, "Verify download links" also checks the card's four installer URLs, which is that step's purpose. | `docs.yml` runs on every push to `main` touching `docs/**` (`docs.yml:3-11`) and writes `_site/index.html` with the same Python (`:129-181`), then deploys it (`:232-238`) | FR-081, FR-085 (SI-1) |
| PD-25 | **release-manager.yml jobs.** `landing`: `needs: [preflight, feed]` (SI-3), `if: ${{ !inputs.dry_run }}`, `runs-on: ubuntu-24.04`, `permissions: contents: write`, steps `actions/checkout@v6` with `fetch-depth: 0`, `actions/setup-node@v4` (`'24'`), `render` (`mkdir -p _site/assets && cp docs/assets/style.css _site/assets/ && node tools/gen-root-index.js --out _site/index.html --manager-version "$VERSION"`, step env `VERSION: ${{ needs.preflight.outputs.version }}` and `REPO: ${{ github.repository }}`, the same `REPO` line the two other callers set, `release.yml:790-791`, `docs.yml:37-38`), `peaceiris/actions-gh-pages@v4` with `github_token`, `publish_dir: ./_site`, `keep_files: true` (as `release.yml:849-854`). `landing-dry`: `needs: [preflight, feed-dry]`, `if: ${{ inputs.dry_run }}`, `contents: read`, the same render step, then `actions/upload-artifact@v7` name `manager-landing-dry-run`, path `_site/index.html`, `if-no-files-found: error`. `release-manager-shape.test.js`: `JOBS` (`:197-210`) gains both, `NEEDS_IF` (`:213-226`) their rows, `WRITE_JOBS` (`:229`) gains `landing`, `NO_SECRET_JOBS` (`:230`) both; a subtest pins both render steps' `env` to exactly `{ VERSION: '${{ needs.preflight.outputs.version }}', REPO: '${{ github.repository }}' }`. `workflow-shape.test.js`'s snapshot assertions (PD-22) keep the `REPO` env of the `release.yml` and `docs.yml` steps, since only the inline Python is replaced. | FR-085; GitHub: `needs` "contains outputs from all jobs that are defined as a direct dependency of the current job … doesn't include implicitly dependent jobs" (https://docs.github.com/en/actions/reference/workflows-and-actions/contexts, read 2026-11-02) | FR-085 (SI-3) |
| PD-26 | **`manager-readiness-counts.js` CLI.** `node tools/manager-readiness-counts.js --specs <dir> --roadmap <file> [--check-ledger <readiness.md>]`. Counts: per `krate-manager-phase<N>/compliance.md` under `--specs`, the lines matching `/\|\s*pending-ci\s*\|/i` (the `grep -ci "\| pending-ci"` reading of spec F-4); per `### Phase <N>:` section of the roadmap, the `PENDING-CI: (\d+) item\(s\)` of its `**Status:` line (`null` when none). Output: one line per phase `phase<N> file=<a> status=<b>` then `total file=<A> status=<B>` over Phases 1-11 (FR-100's scope; a Phase 12 file, if present, prints its own line but is not in the total), exit 0. `--check-ledger`: parses the ledger's "PENDING-CI rows" and "Post-release rows" tables (column 1 an id `phase<N>:<row>[:<half>]`; columns: id, job and assertion, closing workflow, run URL, commit, verdict). The PENDING-CI table lists **every** `pending-ci` compliance row of Phases 1-11, each once, under its own id `phase<N>:<row>` (so its row count is the file total, including Phase 11 SC-014 and SC-020). Exit 1 when (a) the PENDING-CI table's row count differs from the Phases 1-11 file total, or an id repeats; (b) a PENDING-CI row is accepted by neither rule: *closed by a run* (a `https://github.com/<owner>/<repo>/actions/runs/<n>` URL, a 7-40 hex commit, verdict `green`) or *deferred* (verdict `post-release`, no run URL, and its id is one of the five D17 ids **and** is an id of the Post-release table); (c) the Post-release table's id set differs from the five D17 ids (`phase11:SC-014:publish`, `phase11:SC-020`, `phase12:R-POST-1`, `phase12:R-POST-2`, `phase12:R-POST-3`) or one of its rows has a verdict other than `post-release`; (d) a PENDING-CI row closed by a run has an id that is also a Post-release id (a row is closed or deferred, never both). How the two Phase 11 rows are recorded: `phase11:SC-014` is one PENDING-CI row closed by the green `dry-publish` run (its dry half, D17), and its `publish` half is the Post-release row `phase11:SC-014:publish` (a different id, so (d) holds); `phase11:SC-020` is a PENDING-CI row with verdict `post-release` and no run, accepted under *deferred* because `phase11:SC-020` is in the Post-release table. Phase 12's R-POST rows appear in the Post-release table only (they are not Phase 1-11 compliance rows). Reads only its arguments, writes nothing. | FR-100, SC-020, SC-028; without the *deferred* rule the real ledger could never pass: Phase 11 SC-020 is `pending-ci` (`specs/krate-manager-phase11/compliance.md:103`) with no run before the release (review round 1) | FR-100, SC-028 (SI-19) |
| PD-27 | **TAP and required-test pins move with the tests.** `feed-tools.yml`'s `PLAN` (`:94`, now `"1..64"`) is set from a local `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` run after the new tests exist, never hand-added; its `NAMES` gains the new top-level names (§7.1). `manager-ci-shape.test.js` `EXPECTED_REQUIRED` (`:706`, deep-equal at `:1430`) is edited together with `tests/required-tests.json` (FR-071). | Both are pinned by tests that would go red on any other change order | FR-071, FR-086 (SI-5) |
| PD-28 | **New top-level node tests (one each, everything else a subtest):** `gen-root-index.test.js` "root index renders the plugin cards and the manager card"; `manager-first-launch.test.js` "first-launch statements are in the README and the card"; `manager-readiness-counts.test.js` "readiness counts and the closed post-release list"; `manager-release-preflight.test.js` gains "manager release preflight enforces the RustSec record"; `workflow-shape.test.js` gains "landing pages render with gen-root-index.js" (release.yml, docs.yml and feed-tools.yml paths); `release-manager-shape.test.js` adds subtests to its existing test. Each new test file's first statement is `require('./helpers/write-guard.js');` (`hygiene.test.js:156-164`). | The TAP plan counts top-level tests only (`gen-manager-latest.test.js:11`) | SC-016a/b, SC-020, SC-021, SC-028, SC-029, SC-032 |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before tasks)

- **SI-1 (blocking): a third landing generator.** `.github/workflows/docs.yml` regenerates `_site/index.html` with the
  same inline Python (`docs.yml:129-181`) and deploys it with `keep_files: true` (`:232-240`) on every push to `main`
  touching `docs/**`, `plugins/*/docs/**`, `plugins/*/MANUAL.md` or `plugins/*/images/**` (`:3-11`) and on manual
  dispatch. Built as the spec reads, that run overwrites `index.html` without the Krate Manager card, and the commit
  that adds `docs/manager-card-template.html` is itself such a push. FR-085's "the newer of the two deploys is always a
  complete page" is false while `docs.yml` keeps the Python. Fix (ruling P-1, recommended): `docs.yml` calls the same
  tool (PD-24); FR-081 gains the third caller, FR-086 adds `.github/workflows/docs.yml` to `feed-tools.yml`'s paths,
  SC-029 adds the `docs.yml` snapshot assertion, FR-090 (a) and (g) name `docs.yml` too. The roadmap's Reuse Inventory
  does not list `docs.yml` (line 200 cites `release.yml:596-643` only).
- **SI-2 (blocking): the batch cap exceeds the helper's op cap.** FR-002 allows 64 items; the helper refuses a job with
  more than `MAX_OPS = 32` ops (`crates/helper/src/job.rs:11`, `:162`), and it reads the job only after the UAC /
  admin prompt was accepted. Core has no matching cap (`job::write`, `install.rs:816` onward). Fix (ruling P-3,
  recommended): `install::job::MAX_OPS: usize = 32` in core, `install::MAX_BATCH_ITEMS = job::MAX_OPS`, a helper test
  `job::tests::core_and_helper_op_caps_agree` asserting `krate_manager_core::install::job::MAX_OPS ==
  krate_manager_helper::job::MAX_OPS` (the helper dev-depends on core, `crates/helper/Cargo.toml` `[dev-dependencies]`);
  FR-002 "at most 32 entries", SC-027 `batch::input_checks` "33 items".
- **SI-3: `landing` cannot read the version.** FR-085 and SC-029 give `landing` `needs: feed` and
  `--manager-version <preflight version>`; `needs.preflight.outputs` exists only for direct dependencies (GitHub
  contexts reference, PD-25). Fix: `needs: [preflight, feed]` and `needs: [preflight, feed-dry]`.
- **SI-4: a moved version is `E_DOWNLOAD_VERSION_CHANGED`.** The shared steps FR-005 requires end in
  `command_request_for`'s compare, which yields `DownloadError::VersionChanged` (`download.rs:1574-1580`), as the
  single path's refusal table asserts (`install_tests.rs:2249-2251`). The edge-case row "A feed change between status
  and Confirm … `E_INSTALL_STATE_CHANGED`" and SC-009's "named codes" should say `E_DOWNLOAD_VERSION_CHANGED` for a moved
  `expected_version` and `E_INSTALL_NOT_NEWER` for "now up to date".
- **SI-5: pinned lists the spec does not name.** No test pins `feed-tools.yml`'s `paths` today (FR-086's "the existing
  workflow-shape test pins those paths"): PD-28's new workflow-shape test does. `feed-tools.yml`'s TAP `PLAN` and
  `NAMES` (`:94-133`) and `manager-ci-shape.test.js` `EXPECTED_REQUIRED` (`:706`, `:1430`) must change with FR-071 and
  the new node tests (PD-27).
- **SI-6: `PLATFORM_LITERALS`.** FR-045's two literals in `platform/mod.rs` fail `core_sources_hold_no_production_literal`
  unless `injection.rs:51-93` admits them (PD-12).
- **SI-7: edge case "Self-update starts during a batch: impossible from the page (controls disabled)".** The manager's
  "Update and restart" button is not disabled by plugin actions (`main.ts:29-33` passes `setEnabled` one way only;
  `manager_update.ts:245-285` disables only its own button). What refuses it is core: `self_update_busy` holds while
  any busy key exists (`self_update.rs:354-360`) and the batch holds its keys for its whole run (FR-007), so the click
  gets `E_SELF_UPDATE_BUSY`. Reword the row.
- **SI-8: Phase 11 handovers the spec does not carry.** (a) R-9 (`phase11/plan.md:1609`), `release_for_restart` seeing
  a second lock clone once `apply_settings` has a caller: closed by PD-9, no spec text needed beyond FR-043 naming the
  write guard. (b) R-7 (`phase11/plan.md:1607`): "before each manager release, read the `Location` of one real
  release-asset download by hand and confirm its host is `RELEASE_ASSET_HOST`" (`self_update.rs:32`), handed to Phase 12's
  user checks and absent from the spec. Ruling P-2 (recommended): one checklist line in `RELEASING.md`'s procedure and
  one ledger row, recorded by the user, not enforced by preflight.
- **SI-9: the status goldens.** FR-052's new field changes the 13 golden files byte-compared by `status_tests.rs`
  (`:5-6`, `:44-58`) and parsed by vitest; the build edits them by hand (PD-13).
- **SI-10:** FR-060's `BatchItemArg` is core's `install::BatchItem` (PD-5); no second type.
- **SI-11:** FR-008's "the injected `env.download` … and `io`" conflicts with its own next sentence; the batch's
  staging I/O is the `staging` parameter only (PD-4).
- **SI-12:** FR-020 "one line per batch": the elevated driver already logs `elevated batch of <n> op(s): launch …` on the
  install target (`install.rs:1364`); `batch::log_lines` expects it once on Windows / macOS besides the FR-020 lines.
- **SI-13:** SC-029's "committed snapshot" does not exist; PD-22 defines it.
- **SI-14:** `tools/feed-contract/tests/root-index.expected.html` would check out CRLF on this box (`.gitattributes`
  makes only `tools/feed-contract/fixtures/**` `-text`); it moves to `tools/feed-contract/fixtures/landing/`.
- **SI-15:** FR-042 does not say what a save does with no startup context; PD-10 returns the startup error.
- **SI-16:** FR-086 should also list `.github/workflows/docs.yml` (SI-1), `specs/krate-manager-phase12/user-checks.md`
  (SC-016b switches on its FR-073 rows, so the commit that records them must run the TAP check) and
  `tools/feed-contract/fixtures/landing/**` (already under `tools/feed-contract/**`).
- **SI-18:** SC-011 lists `save_for_command_cases` "under `cargo.linux`"; it runs on every leg (forced `Os`), and the
  required-tests checker wants one passing line per name over `all` + the leg's list, so it belongs under `cargo.all`
  only (§7.2).
- **SI-17:** FR-040's `error: Option<String>` for the startup `settings_error` (`context.rs:68`): `SettingsError` is not
  `Clone` (`settings.rs:61`), so the view carries its `Display` text, read under the guard.
- **SI-19:** FR-100 / SC-020 "lists exactly that many compliance rows … each with … a green run URL and commit, except
  the D17 rows" is met only if the D17 Phase 1-11 rows stay in the PENDING-CI table (verdict `post-release`) and also
  appear in "Post-release rows"; PD-26 states that layout and the acceptance rules, so SC-028's "on the real
  `readiness.md` … exits 0" is reachable. The spec could say so in FR-100's last sentence.
- **SI-20:** FR-060 "three thin commands on `spawn_blocking`": `settings_view` and `settings_save` cannot be, because
  `spawn_blocking` needs a `'static` closure and `SharedState` is managed by value (`lib.rs:94`,
  `.manage(SharedState::new(state))`), so only a borrowed `tauri::State<'_, SharedState>` exists in the command; the
  save must hold the state's write guard (PD-9) and therefore runs inline in the `async fn` (§4.6). Only `update_all`
  is on `spawn_blocking`. Amend FR-060 to "`update_all` on `spawn_blocking`; the two settings commands `async` and
  calling the core inline".
- **SI-21:** FR-042 should name the root gate (PD-11): a save is also `E_SETTINGS_BUSY` while a plugin command holds a
  `RootTicket`, and `install_plugin`, `remove_plugin` and `update_all` take their context through
  `settings::command_context`, waiting out a running save. No new code; the gate is the one rule that keeps a plugin
  command from writing into a root that a concurrent save replaced.
- **SI-22:** SC-009's "sampled at the end of stage 1 through the progress callback's first `Download` event" samples too
  late: each worker calls `create_dir_all` and `prune` before its first event (`download.rs:1418-1424`, `Started` at
  `:1046-1049`) and up to three workers may have sent their GET before the calling thread sees the event. The plan
  samples at the explicit `BatchStage::ChecksDone` seam (PD-6 (3b)); SC-009 should name it.

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-11-02 at `7c4b6837`)

| Fact | Where |
|---|---|
| `Action { Install, Update, Uninstall }`, every arm but `Uninstall` holds a `VerifiedArtifact` | `install.rs:310-323` |
| `Notice { RescanDaw, RescanDawMacos }`, `ActionSummary`, `InstallProgress` (7 kinds, `Elevating` once per elevated batch) | `install.rs:362-401` |
| `InstallDriver: Sync { os, run(env, batch) }`; `LinuxDriver::run` loops in order, one `log_result` each | `install.rs:403-476` |
| `driver_for(os)` offers all three drivers | `install.rs:510-517` |
| `InstallEnv`, `CommandEnv` (`Copy`, `download: DownloadEnv`, `#[cfg(test)] request_seam`) | `install.rs:519-546` |
| `run_elevated`: ops, at most one `elevate`, registrar kill once, `Locked` mapping, `confirmed` | `install.rs:1166-1263` |
| `elevate`: `self_update::action_gate`, guard, job, `Elevating` (`:1353`), one launch, `info!` "elevated batch of … op(s)" (`:1364`) | `install.rs:1340-1378` |
| `job::ceiling` sums per-op timeouts | `install.rs:1033-1046` |
| Helper `MAX_OPS = 32`, refused when `ops.len() > MAX_OPS` | `crates/helper/src/job.rs:11`, `:162` |
| `InstallIo: Sync` with `read_dir`, `read_file(path, max)` | `install.rs:1437-1459` |
| `Destinations::from_parts(ctx, name, vst3_bundle, preset_subdir)`; `check_sandboxed` on every path | `install.rs:1752-1820` |
| `Manifest`, `read_manifest_full`, `check_manifest` (bundle = `<stem>.vst3`) | `install.rs:1929-2045` |
| `roots_match`, `resolve_entries`, `check_state_dirs` | `install.rs:2057-2108`, `:2164-2172` |
| `run_uninstall`, `remove_entries` (manifest-relative, deepest first) | `install.rs:3584-3684` |
| `action_gate`, `allowed_actions`, `refusal` | `install.rs:3710-3724`, `:3810-3872` |
| `BUSY` registry, `Busy::enter`, `any_busy` | `install.rs:3888-3924` |
| `for_command`, `Checked`, `install_command`, `obtain_and_run` (driver inside `next`, `discard` after) | `install.rs:3943-4141` |
| `remove_for_command`, `command_remove` (step 1 `E_INSTALL_UNKNOWN_PLUGIN`) | `install.rs:4145-4252` |
| `log_result`, `log_refusal` | `install.rs:4257-4299` |
| `DownloadPolicy::production`, `DownloadEnv` (four borrows, not `Send`) | `download.rs:179-215` |
| `StagingIo` (no `Sync`), `FsStagingIo` | `download.rs:551-568` |
| `IN_FLIGHT`, `any_in_flight(downloads)` | `download.rs:650-681` |
| `request_for_platform`, `obtain_and`, `discard`, `command_request_for` (one `feed::check`, `VersionChanged`) | `download.rs:777-810`, `:1354-1376`, `:1515-1550`, `:1554-1584` |
| `CancelToken(AtomicBool)` with `cancel()` | `http.rs:1222-1238` |
| `PacHost: Send + Sync`, `PacHostHandle(Arc<dyn PacHost>)` | `pac.rs:70`, `:110` |
| `AppContext`, `AppState { context, settings_error, lock }`, `SharedState = RwLock<AppState>` | `context.rs:51-72` |
| `reassemble` hard-codes the OS; `reassemble_with_os` clones the lock; `apply_settings` read-then-write | `context.rs:155-204` |
| `release_for_restart` takes the write guard and `try_unwrap`s the lock | `context.rs:245-260` |
| `Settings`, `validate`, `load` (never writes), `save` / `save_with` (refuses newer) | `settings.rs:20-33`, `:101-158`, `:279-307` |
| `self_update::action_gate`, `SelfUpdating`, `self_update_busy` | `self_update.rs:354-360`, `:393-408` |
| `StatusView` fields, `build_with_os` (one feed check, gates when a driver exists) | `status.rs:24-31`, `:211-269` |
| `KILLALL_ARGV`, the only whole-word process-tool hit in non-test sources (scan run this session over `crates/*/src`, `src-tauri/src`) | `platform/mod.rs:19` |
| `PLATFORM_LITERALS` (40) | `injection.rs:51-93` |
| `ENV_READS`, `ENV_READ_FILES`, `src_tauri_env_findings` | `injection.rs:621-629`, `:2248-2278` |
| `HANDLER_LIST`, `INSTALL_COMMANDS`, parameter check, capability list, build.rs list | `config_guard.rs:186-189`, `:576-609`, `:784-829`, `:831-860` |
| `run()` order: `Roots::from_os` (`:46`), `sink_roots` (`:48`), feed URL (`:49-56`), `proxy_env::read` (`:58`), `assemble_context` (`:74`), handler list (`:112-123`); a `cfg`-ed expression statement (`:108-109`) | `src-tauri/src/lib.rs:41-126` |
| `install_plugin` / `remove_plugin`: context clone, `spawn_blocking`, `native_env` | `src-tauri/src/commands.rs:110-224` |
| `capabilities/main.json` ten permissions; `build.rs` ten commands; ten autogenerated permission files | `src-tauri/capabilities/main.json`, `src-tauri/build.rs:1-17`, `src-tauri/permissions/autogenerated/` |
| `INSTALL_TEXTS` incl. `rescan`, `rescanMacos1-4`, `appleGuide`, `adminWindows/Macos`, `elevating` | `src/status.ts:67-100` |
| `actionRow`, `outcomeLines`, `createStatusSection`, `setActionsEnabled` | `src/status.ts:346-447`, `:535-617` |
| `object(value, path, keys)` exact keys; `toStatusView`, `toActionSummary`, `toInstallProgress` | `src/view.ts:129-136`, `:311-326`, `:398-470` |
| `main.test.ts` pins exact invoke sequences on `windows` fixtures | `src/main.test.ts:57`, `:73`, `:110`, `:155` |
| `required-tests.json` keys `cargo.{all,windows,macos,linux}`, `vitest`, `node`; checked in `manager-ci.yml` "required tests ran" (one passing line each) | `tests/required-tests.json`; `.github/workflows/manager-ci.yml:279-310` |
| `EXPECTED_REQUIRED` deep-equals `required-tests.json` | `tools/feed-contract/tests/manager-ci-shape.test.js:706`, `:1430` |
| `release.yml` `deploy-pages`, "Generate Root Index" Python | `.github/workflows/release.yml:731-855`, `:789-847` |
| `docs.yml` regenerates the root index with the same Python and deploys it | `.github/workflows/docs.yml:3-11`, `:129-181`, `:232-240` |
| `docs/index-template.html` stored LF, checked out CRLF here | `git ls-files --eol docs/index-template.html` |
| `.gitattributes`: `tools/feed-contract/fixtures/** -text`, nothing for `tools/feed-contract/tests/` | `.gitattributes` |
| `release-manager.yml` jobs and `feed` / `feed-dry` | `.github/workflows/release-manager.yml:33-1750` |
| shape-test tables `JOBS`, `NEEDS_IF`, `WRITE_JOBS`, `NO_SECRET_JOBS` | `release-manager-shape.test.js:197-230` |
| `DEPLOY_PAGES_STEPS` | `workflow-shape.test.js:729-735` |
| `feed-tools.yml` paths, TAP `PLAN` `1..64`, `NAMES` | `.github/workflows/feed-tools.yml:3-49`, `:89-150` |
| hygiene: offline scan, `CHILD_EXEMPT`, write-guard-first rule | `hygiene.test.js:60-164` |
| preflight checks and order; `releaseBody` macOS line `(provisional: …)` | `tools/manager-release-preflight.js:87-111`, `:160-245` |
| `rquickjs`, `-core`, `-sys` 0.14.0 | `apps/krate-manager/Cargo.lock:3107-3130` |

### 1.2 External facts (verified 2026-11-02)

| Fact | Source |
|---|---|
| Command arguments arrive "as a JSON object with camelCase keys"; arguments may be any `serde::Deserialize` type; `tauri::ipc::Channel` is "the recommended mechanism for streaming data … to the frontend" | https://v2.tauri.app/develop/calling-rust/ (last updated 2026-06-08; Tauri 2, the workspace pins `tauri` 2.12.5, `Cargo.toml` `[workspace.dependencies]`) |
| serde container attributes `rename_all` ("camelCase", …) and `deny_unknown_fields` ("Always error during deserialization when encountering unknown fields") | https://serde.rs/container-attrs.html (`serde` 1.0.229 in the workspace) |
| `std::thread::scope` joins every unjoined thread before returning and panics if one panicked; stable since 1.63.0 | https://doc.rust-lang.org/std/thread/fn.scope.html (Rust 1.99.1 docs; workspace `rust-version = "1.90"`) |
| GitHub Actions `needs` holds outputs of direct dependencies only | https://docs.github.com/en/actions/reference/workflows-and-actions/contexts |
| Tauri Linux graphics ladder, rung 3 `WEBKIT_DISABLE_DMABUF_RENDERER=1`, "set in `main()` before the webview is created"; `set_var` sound only single-threaded; RustSec package pages | spec "External facts used" (`spec.md:131-141`), fetched 2026-11-02 by the spec stage; not re-fetched |

---

## 2. File layout

```
apps/krate-manager/
  crates/core/src/install.rs              + BatchItem, BatchPolicy, MAX_BATCH_ITEMS, BatchProgress, BatchItemResult,
                                            BatchSummary(+View), batch_for_command, check_item (split), ManifestHead,
                                            manifest_heads, run_uninstall_at (split), orphan branch, job::MAX_OPS,
                                            #[cfg(test)] CommandEnv.stage_seam + BatchStage
  crates/core/src/install_batch_tests.rs  new: install::batch::* (SC-002 to SC-010, SC-027 core cases)
  crates/core/src/install_tests.rs        helpers pub(super), ops_after; orphan_remove_cases, not_manageable_is_not_orphan
  crates/core/src/download.rs             + request_in_feed (split of command_request_for)
  crates/core/src/settings.rs             + SettingsView, SettingsCommandError, view(_with_os), save_for_command(_with_os),
                                            root gate: RootTicket, command_context, SaveGate, hold_save_gate_for_test
  crates/core/src/settings_tests.rs       + save_for_command_cases, view_cases, root_gate_cases
  crates/core/src/test_support.rs         + stage_seam: None in the rig's CommandEnv (#[cfg(test)])
  crates/core/src/context.rs              + apply_settings_with_os (write guard only); apply_settings wraps it
  crates/core/src/platform/mod.rs         + GRAPHICS_ENV, graphics_env, apply_graphics_env
  crates/core/src/platform/mod_tests.rs   + graphics_env_cases, apply_graphics_env_cases
  crates/core/src/status.rs               + OrphanView, StatusView.orphans
  crates/core/src/status_tests.rs         + orphans_cases
  crates/core/src/fixture_server.rs       + Reply::Held, HoldGate (test-support)
  crates/core/tests/injection.rs          + no_process_enumeration, one_set_var_site_before_threads, no_cancel_setter,
                                            plugin_commands_take_the_root_ticket; PLATFORM_LITERALS 42; the request_seam
                                            guard covers stage_seam and admits install_batch_tests.rs
  crates/core/tests/config_guard.rs       13 commands everywhere; UPDATE_ALL / SETTINGS param rules
  crates/helper/src/job_tests.rs          + core_and_helper_op_caps_agree
  src-tauri/src/commands.rs               + update_all, settings_view, settings_save; install_plugin / remove_plugin take
                                            their context through settings::command_context (PD-11)
  src-tauri/src/lib.rs                    + the Linux graphics block; 13 handlers
  src-tauri/build.rs, capabilities/main.json, permissions/autogenerated/{update_all,settings_view,settings_save}.toml
  src/status.ts, src/view.ts, src/main.ts, src/settings.ts (new), *.test.ts (main.test.ts: + three PD-14 cases)
  tests/fixtures/status/*.json            + "orphans": []
  tests/required-tests.json               + every Phase 12 test
  README.md (new), RELEASING.md (new), CHANGELOG.md (+3 Added lines)
docs/manager-card-template.html           new
tools/gen-root-index.js                   new
tools/manager-readiness-counts.js         new
tools/manager-release-preflight.js        E_PREFLIGHT_RUSTSEC, --run-date, releaseBody macOS line (post-U-1)
tools/feed-contract/first-launch-statements.json   new
tools/feed-contract/fixtures/landing/     new: release.yml.pre-phase12, docs.yml.pre-phase12, plugins/ tree,
                                            root-index.expected.html, readiness/ (fixture compliance files, roadmap)
tools/feed-contract/tests/                gen-root-index, manager-first-launch, manager-readiness-counts (new);
                                            workflow-shape, release-manager-shape, manager-release-preflight,
                                            manager-ci-shape, hygiene (edited); release-body.expected.md (post-U-1)
.github/workflows/release.yml             deploy-pages: Setup Node + one node call
.github/workflows/docs.yml                Setup Node + one node call (SI-1)
.github/workflows/release-manager.yml     + landing, landing-dry
.github/workflows/feed-tools.yml          + FR-086 paths, TAP PLAN / NAMES
specs/Krate-manager-roadmap.md            FR-090 (a)-(g)
specs/krate-manager-phase12/              user-checks.md, readiness.md, evidence/
```

---

## 3. Error model

New codes, each `Display` beginning with its code:

| Code | Type | Raised by |
|---|---|---|
| `E_INSTALL_BATCH: {reason}` | `InstallError::Batch { reason: &'static str }` (`code()` arm added, `install.rs:212-252`) | FR-002 input checks; reasons `"no items"`, `"more than 32 items"`, `"a plugin is named twice"` |
| `E_INSTALL_MANIFEST: two manifests name this plugin` / `too many manifests` / `the manifest file name is not its bundle's` | existing `InstallError::Manifest { reason }` (`install.rs:136-137`) | FR-050, FR-051 |
| `E_SETTINGS_ROOT_IN_USE`, `E_SETTINGS_BUSY`, `E_SETTINGS_UNSUPPORTED_OS` | `SettingsCommandError` (PD-10) | FR-041, FR-042 |
| `E_PREFLIGHT_RUSTSEC: <why>` | `PreflightError` | PD-19 |
| `E_ROOT_INDEX_*` (`E_ROOT_INDEX_REPO`, `E_ROOT_INDEX_TEMPLATE`, `E_ROOT_INDEX_VERSION_JSON`, `E_ROOT_INDEX_VERSION`) exit 1; usage exit 2 | `tools/gen-root-index.js` | a missing or empty `REPO` environment value (checked first), a missing / unreadable template or card template, a `version.json` without string `name` / `description`, a `--manager-version` not `x.y.z` |

Every other batch result reuses existing codes (`E_INSTALL_SELF_UPDATING`, `E_INSTALL_NO_FEED`, `E_INSTALL_NO_DRIVER`,
the `E_DOWNLOAD_*` set, `E_INSTALL_BUSY`, `E_INSTALL_NOT_MANAGEABLE`, `E_INSTALL_STATE_CHANGED`, `E_INSTALL_NOT_NEWER`,
`E_INSTALL_REFUSED`, `E_INSTALL_ELEVATION_BUSY`, `E_INSTALL_LOCKED`, `E_INSTALL_NOT_APPLIED`, `E_INSTALL_JOURNAL_PENDING`
(`install.rs:132`), …). The root gate (PD-11) adds no code: a save it refuses is `E_SETTINGS_BUSY`; a plugin command
waits instead of failing. Batch-level errors
(`Err` of `batch_for_command`): `E_INSTALL_BATCH`, `E_INSTALL_SELF_UPDATING`, `E_INSTALL_NO_FEED`, `E_INSTALL_NO_DRIVER`;
everything else is an item result. `src-tauri` maps `Err(e)` to `e.to_string()` and returns `Ok(summary.view())`, as
`install_plugin` does (`commands.rs:159`).

---

## 4. Module design

### 4.1 `install::batch_for_command` (FR-001 to FR-020; PD-2 to PD-6)

```rust
pub const MAX_BATCH_ITEMS: usize = job::MAX_OPS;                 // 32 (SI-2)
pub struct BatchPolicy { pub parallel_downloads: usize }
impl BatchPolicy { pub const PRODUCTION: BatchPolicy = BatchPolicy { parallel_downloads: 3 }; }

pub fn batch_for_command(
    env: CommandEnv<'_>,
    staging: &(dyn StagingIo + Sync),
    policy: &BatchPolicy,
    items: &[BatchItem],
    now_unix: u64,
    host: &dyn HostFacts,
    progress: &mut dyn FnMut(BatchProgress),
) -> Result<BatchSummary, InstallError>;
```

Internals (private): `enum Slot<'a> { Refused(InstallError), Checked { checked: Checked<'a>, request: DownloadRequest },
Verified { checked, artifact: VerifiedArtifact }, Done(Result<ActionSummary, InstallError>) }`, one per item in request
order; `enum Msg { Progress(usize, Progress), Done(usize, Result<VerifiedArtifact, DownloadError>) }`. A
`policy.parallel_downloads` of 0 is treated as 1 (tests never pass it; no setting can, D3). The stage-2 event order per
item is `obtain_and`'s; across items it is arrival order. The batch line:
`info!(target: LOG_TARGET, "batch of {n}: {accepted} accepted, {verified} verified, {ok} updated")`.

Notice (D7, FR-016): `Some(RescanDawMacos)` if `env.os == Macos` and some item is `Ok`, `Some(RescanDaw)` for the other
two OSes with some `Ok`, else `None`.

### 4.2 The single path after the split (FR-019)

`for_command` → `install_command` (action word, `command_request_for`, `check_item`) → `obtain_and_run` (unchanged:
driver inside `next`, `discard` after). Its results, codes, log lines and progress are byte-identical; the existing
`install::tests::*` and `install::loopback::*` suites are the proof (SC-024).

### 4.3 Orphans (FR-050 to FR-054; PD-7, PD-8)

`command_remove` step 1 becomes: snapshot → `match snapshot.feed.plugins.get(name)`: `Some(entry)` → the Phase 9 path
unchanged (so a feed entry without `install` keeps `E_INSTALL_NOT_MANAGEABLE`, FR-054); `None` and `env.os == Linux` →
`orphan_remove(env, name, progress, ran)`; `None` otherwise → `E_INSTALL_UNKNOWN_PLUGIN`. `orphan_remove`: heads with
`plugin == Some(name)`: 0 → `UnknownPlugin`, 2+ → `Manifest { "two manifests name this plugin" }`, 1 → read its
`vst3_bundle` through `read_manifest_full(io, state, &head.stem, name)` (a `None` here is `UnknownPlugin`),
`from_parts`, stem check, `Busy::enter` (`E_INSTALL_BUSY`), `ran.set(true)`, `run_uninstall_at`, `log_result`; the
summary is `ActionSummary { name, action: Remove, version, kept, notice: None }` as `command_remove` builds it
(`install.rs:4245-4251`). No asset request, no feed field beyond the key test.

### 4.4 Settings (FR-040 to FR-044, FR-048; PD-9 to PD-11)

```rust
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct SettingsView { pub os: OsName, pub linux_plugin_root: String, pub root_is_default: bool,
                          pub graphics_workaround: bool, pub root_locked_by: Vec<String>, pub error: Option<String> }
pub fn view(state: &SharedState) -> Result<SettingsView, SettingsCommandError>;              // native OS
pub(crate) fn view_with_os(os: Os, state: &SharedState) -> Result<SettingsView, SettingsCommandError>;
pub fn save_for_command(state: &SharedState, root: Option<String>, graphics: bool)
    -> Result<SettingsView, SettingsCommandError>;                                          // native OS
pub(crate) fn save_for_command_with_os(os: Os, state: &SharedState, root: Option<String>, graphics: bool)
    -> Result<SettingsView, SettingsCommandError>;
#[must_use] pub struct RootTicket { /* state_dir */ }                                     // PD-11 root gate
pub fn command_context(state: &SharedState) -> Result<(RootTicket, AppContext), StartupError>;
#[cfg(any(test, feature = "test-support"))]
pub fn hold_save_gate_for_test(state_dir: &Path) -> SaveGateHeldForTest;                  // as hold_busy_for_test
// context.rs
pub(crate) fn apply_settings_with_os(os: Os, shared: &SharedState, settings: Settings);
```

`view_with_os` reads under the read guard: `linux_plugin_root` = `ctx.settings.linux_plugin_root_or_default(&ctx.roots)`
as lossy text (`settings.rs:27-32`), `root_is_default` = stored `None`, `graphics_workaround` = stored bool,
`root_locked_by` = the FR-041 names (PD-11 step 5, computed even when nothing changes), `error` = the
`settings_error`'s `Display` (SI-17), `os` from `os`. Off Linux the view is still returned (the UI never asks).
`settings.rs` imports `install`, `download`, `self_update` and `context` (module cycles are allowed inside one crate).

### 4.5 Graphics (FR-045, FR-046; PD-12)

```rust
pub const GRAPHICS_ENV: (&str, &str) = ("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
pub fn graphics_env(os: Os, settings: &Settings) -> Option<(&'static str, &'static str)> {
    (os == Os::Linux && settings.linux_graphics_workaround).then_some(GRAPHICS_ENV)
}
pub fn apply_graphics_env(os: Os, settings: Option<&Settings>, set: impl FnOnce(&str, &str)) {
    if let Some((name, value)) = settings.and_then(|s| graphics_env(os, s)) { set(name, value) }
}
```

`platform/mod.rs` imports `crate::settings::Settings` (it touches no filesystem: `FS_TOKENS`, `injection.rs:33-48`,
stay absent).

### 4.6 `src-tauri` (FR-060, FR-061)

```rust
#[tauri::command]
pub async fn update_all(state: tauri::State<'_, SharedState>, items: Vec<BatchItem>,
    on_progress: tauri::ipc::Channel<BatchProgress>, elevation: tauri::State<'_, Arc<NativeElevation>>)
    -> Result<BatchSummaryView, String>;
#[tauri::command]
pub async fn settings_view(state: tauri::State<'_, SharedState>) -> Result<SettingsView, String>;
#[tauri::command]
pub async fn settings_save(state: tauri::State<'_, SharedState>, linux_plugin_root: Option<String>,
    graphics_workaround: bool) -> Result<SettingsView, String>;
```

`update_all` follows `install_plugin` (`commands.rs:110-168`) line for line, with its context taken through
`settings::command_context` and the ticket moved into its `spawn_blocking` closure (PD-11; the same edit is made to
`install_plugin` and `remove_plugin`, and `injection.rs` case `plugin_commands_take_the_root_ticket` requires in
`src-tauri/src/commands.rs` exactly one `settings :: command_context (` in each of the three bodies and no
`context . clone (` in them, with one plant per body restoring the old clone block), plus `&FsStagingIo` as `staging`,
`&BatchPolicy::PRODUCTION` and `install::batch_channel_progress(|e| on_progress.send(e))` (a twin of
`channel_progress`, `install.rs:1424-1432`). The two settings commands are `async fn` taking `tauri::State<'_,
SharedState>`, the form the existing async commands use (`commands.rs:33-36`), and call the core function directly on
`&state`: the work is a guard read, at most one small atomic file write and a reassembly, so no `spawn_blocking` (Tauri
runs `async` commands off the main thread, "Asynchronous commands are preferred in Tauri to perform heavy work",
calling-rust page above). They cannot be on `spawn_blocking` as FR-060 says: its closure must be `'static`, and the
command holds only a borrowed `tauri::State<'_, SharedState>` (`SharedState` is managed by value, `lib.rs:94`), while
the save needs that state's write guard (PD-9); SI-20 asks the main loop to amend FR-060. Their `Err` is `e.to_string()`. `lib.rs` lists 13 handlers in this order: the ten existing,
then `update_all`, `settings_view`, `settings_save`.

Parameter rules (`config_guard.rs`): `INSTALL_COMMANDS` stays `install_plugin`, `remove_plugin`; a new
`UPDATE_ALL_TYPES` admits `State<…>`, `Vec<BatchItem>`, `Channel<BatchProgress>`; `SETTINGS_SAVE_TYPES` admits
`State<…>`, `Option<String>`, `bool`; `settings_view` admits `State<…>` only. `HANDLER_LIST`, the build.rs string and
the capability list (`config_guard.rs:186`, `:840`, `:798-809`) gain the three names; each failure message names
"Phase 12 FR-061".

### 4.7 UI (FR-021 to FR-028, FR-047, FR-053; PD-14, PD-15)

`view.ts`: `OrphanView { name, version }`, `StatusView.orphans`; `BatchProgress` = `{ kind: "item"; name; event:
InstallProgress } | { kind: "elevating" }`; `BatchSummary { results: BatchItemView[]; notice: ActionNotice | null }`,
`BatchItemView { name; summary: ActionSummary | null; error: string | null }` (exactly one non-null, else
`TypeError`); `SettingsView` with the six keys. Parsers `toBatchProgress`, `toBatchSummary`, `toSettingsView`.

`status.ts`: new texts `INSTALL_TEXTS.updateAll` "Update all", `confirmUpdateAll` "Update these plugins?",
`updateAllLine` "{name} {installed} → {latest}", `orphanLine` "{name} {version}: no longer offered; installed by Krate
Manager". `div.update-all` (button `update-all`) is rendered by `render(view)` when any plugin's `actions` contains
`update`; click → `div.confirm-all` with `confirmUpdateAll`, one `p.confirm-line` per offered update in status order,
the OS admin line once, `confirm-ok`, `confirm-cancel`. Confirm → `batchRunning = true`, recompute buttons, one
`invoke("update_all", { items, onProgress })` with `items = [{ name, expectedVersion: latest }]` of exactly the listed
cards; events: `item` → that card's `p.progress` (text from the existing `progressText`, so the same three FR-024
texts); `elevating` → one section `p.batch-progress` with `INSTALL_TEXTS.elevating`. Settle: per item `host.outcomes`
gets `outcomeLines({ ...summary, notice: null })` or `[error]`; the section's `p.batch-outcome` lines are `rescan` once
(plus `rescanMacos1-4`, `appleGuide` for `rescan_daw_macos`) when `notice` is set, kept until the next action or
`Check now`; then `host.refresh()` (one `plugin_status` invoke, `status.ts:562-570`; it re-renders from `host.outcomes`,
so the per-card lines survive, and the section re-attaches its `p.batch-outcome` lines after the re-render),
`batchRunning = false`. A malformed `item` event changes nothing; a malformed result
is one `p.batch-outcome` rejection line (FR-026). Orphan cards: `article.plugin.orphan` with `orphanLine` and one
`Remove` running the existing confirm (`INSTALL_TEXTS.confirmRemove`, `status.ts:76`) and `remove_plugin`. No element
named or classed `cancel` exists outside the two confirmations' dismiss buttons; no keyboard handler (FR-027).

`settings.ts`: `createSettingsSection(invoke, onRootChanged): { element, render(view) }`; `section.settings` with a
`Settings` toggle button opening `div.settings-body`: `input.root` (text), `Save`, `Use default (~/.vst3)` (sends
`linuxPluginRoot: null`), a checkbox labelled with FR-047's sentence, `p.root-locked` "Remove {names} before changing
the plug-in folder" (field disabled when `root_locked_by` is non-empty), `p.settings-result`. Save →
`invoke("settings_save", { linuxPluginRoot, graphicsWorkaround })`; on a root change that succeeded `onRootChanged()`
once (FR-048). A save made while a card action or a batch runs is refused by core (`E_SETTINGS_BUSY`, PD-11) and the
text is shown in `p.settings-result`; the page does not otherwise couple the two sections.

### 4.8 Node tools (FR-081, FR-100, FR-104; PD-16, PD-19, PD-26)

- `tools/gen-root-index.js`: exports `newestManagerVersion`, `renderManagerCard`, `renderPluginCards`,
  `renderRootIndex`, `collectPlugins(deps, root)`, `main(argv, io, deps)`, `productionDeps`. Usage
  `node tools/gen-root-index.js --out <file> [--manager-version <x.y.z>]` (exit 2 on anything else); reads `REPO` from
  the environment (the Python's `os.environ['REPO']`); `#!/usr/bin/env node` first line, LF in `.gitattributes` beside
  `tools/gen-manager-latest.js` (the d925be34 lesson).
- `tools/manager-readiness-counts.js`: PD-26; exports `countCompliance`, `countStatusLines`, `checkLedger`, `main`.
- `tools/manager-release-preflight.js`: PD-19; `releaseBody` gains the post-U-1 macOS text (FR-083) and the
  `release-body.expected.md` fixture changes with it (SC-017).

### 4.9 Workflows (FR-081, FR-085, FR-086; PD-22 to PD-25)

`release.yml` PD-23; `docs.yml` PD-24; `release-manager.yml` PD-25; `feed-tools.yml`: both `paths` lists gain
`docs/manager-card-template.html`, `docs/index-template.html`, `apps/krate-manager/README.md`,
`apps/krate-manager/RELEASING.md`, `tools/gen-root-index.js`, `tools/manager-readiness-counts.js`,
`.github/workflows/docs.yml`, `specs/krate-manager-phase12/user-checks.md` (SI-16: the commit that records the FR-073
rows flips SC-016b's branch, so it must run the TAP check); the PD-28 workflow-shape test pins both lists with these
entries; `PLAN` / `NAMES` per PD-27. `manager-ci.yml` is unchanged (its `apps/krate-manager/**`
filter already covers every app file; `required tests ran` reads the new names).

### 4.10 Docs and records (FR-070 to FR-084, FR-100 to FR-104)

- `tools/feed-contract/first-launch-statements.json`: `{ "statements": [ { "id", "stage", "text" } ] }`, the 13 ids of
  FR-084 in that order; `macos.*` `"stage": "post-u1", "text": null` until U-1. The `floor` statement's `text` is
  exactly FR-080's supported-systems sentence, as plain text: `Windows 10 22H2 and Windows 11 x64; macOS 15 and later
  (Apple silicon and Intel); Linux x86_64 at the glibc of the ubuntu:22.04 build.` The README's `## Supported systems`
  section and the card's "Supported systems" paragraph (PD-17) are that statement and nothing else, so SC-016a's
  verbatim check pins all three OS floors in both documents.
- `apps/krate-manager/README.md`: `## First launch` (`### Windows`, `### macOS`, `### Linux`, `### Every system`),
  `## Supported systems` (the `floor` statement), every non-null statement verbatim (a statement's `text` may sit in a sentence or list item,
  never split across lines with Markdown markup inside it).
- `apps/krate-manager/RELEASING.md`: `## Procedure` (dispatch dry run, read its `landing-dry` artifact, the R-7 asset-host
  check line of P-2, dispatch the real run), `## RustSec` (the URLs, the three crates, the rule, the table).
- `specs/krate-manager-phase12/user-checks.md`: one table per check (FR-073 Sequoia, FR-073 Tahoe, FR-074 (a)-(f),
  FR-075 / R-POST-2, R-POST-3), the FR-070 columns plus a first column `id` and a last column `result`. Agents write
  each heading, each table's header and its id rows with an empty `result` cell (the FR-073 ids are `FR-073-sequoia`
  and `FR-073-tahoe`); only the user fills a row. A row counts as recorded when its `result` cell, trimmed, is
  non-empty. SC-016b reads the two FR-073 rows by id: fewer than two recorded → every `post-u1` `text` must be `null`;
  both recorded → every `post-u1` `text` must be non-empty and present in both documents. A missing `FR-073-*` id row
  or a duplicate id fails the case (the file is malformed, never treated as "not recorded").
- `specs/krate-manager-phase12/readiness.md`: "Counts" (the tool's output, pasted), "PENDING-CI rows" (every Phase 1-11
  row, the two deferred Phase 11 rows included with verdict `post-release`, PD-26), "Gates" (FR-101),
  "Backfill and live feed" (FR-103), "RustSec" (FR-102), "Asset host" (P-2), "Post-release rows" (the five D17 ids).

### 4.11 Roadmap amendments (FR-090), by the build stage

(a) lines 200 and 926: `release.yml:596-643` → "the 'Generate Root Index' step of `release.yml` and the root-index step
of `docs.yml` (`tools/gen-root-index.js`), also run by `release-manager.yml` job `landing`"; (b)-(f) as FR-090; (g)
line 927 names all three callers (SI-1). SC-026 checks no other line changed.

### 4.12 State layout

Unchanged: `<state>/settings.json` (Phase 4 D10 bytes), `<state>/manifests/<stem>.json`, `<state>/journal/<stem>.json`,
`<state>/jobs/`, `<staging>/downloads/<hex>/`. The batch adds no file: verified artifacts of failed items stay in
`downloads/` (D6) and are pruned by the next `obtain_and` against a feed that no longer offers them (Phase 7 FR-005).

### 4.13 The OS abstraction

Every OS decision of this phase goes through an `Os` value: `CommandEnv.os` (batch stage 3 split, orphan branch),
`save_for_command_with_os` / `view_with_os`, `graphics_env(os, …)`, `build_with_os(os, …)`. The only `cfg` is the
`lib.rs` call site of PD-12. All three OS behaviours are tested on the Windows box by forced `Os` values; the native
legs re-run the same tests on `manager-ci` (§7.3).

---

## 5. Dependencies

No new crate, npm package, Tauri plugin or GitHub Action version (FR-061, SC-023). Used as they are: `tauri` 2.12.5,
`serde` 1.0.229, `serde_json` 1.0.154, `thiserror` 2.0.21, `log` 0.4.34 (`Cargo.toml` `[workspace.dependencies]`);
`@tauri-apps/api` 2.12.4, `vitest` 5.0.5, `typescript` 6.0.3, `eslint` 10.14.0 (`package.json`); Node 24 built-ins
(`node:fs`, `node:path`, `node:child_process` in `gen-root-index.js` only); `actions/setup-node@v4`,
`actions/checkout@v6`, `actions/upload-artifact@v7`, `peaceiris/actions-gh-pages@v4`, already used by the edited
workflows (`release-manager.yml:41-47`, `:1725`; `release.yml:849`). `std::thread::scope` and `std::sync::mpsc` are std.

---

## 6. Safety design

- **Injected roots.** Every new core path reads roots from `AppContext` (`Roots`, built by `temp_roots()` in tests,
  `check_sandboxed` panicking on an outside path, `roots.rs:175`); new tests use `fresh_temp_roots()` /
  `InstallRig` and `FakeElevation` / `FakeHostIo`; the fixture server is loopback-only. No test writes a real plug-in,
  preset, ProgramData or registry location, runs the helper elevated or calls github.com (SC-025).
- **One root per command.** A plugin command (`install_plugin`, `remove_plugin`, `update_all`) holds a `RootTicket`
  from before it clones its context until its core call returns, and a settings save holds the per-state-dir save gate
  from before its authoritative context read until it returns (PD-11). So no command writes into a root that a
  concurrent save has replaced, no manifest records a root other than the configured one, and two saves never leave
  the file and the live context holding different roots (which a restart would turn into the same lock-out).
- **Verify before execute.** Stage 3 accepts only `VerifiedArtifact`s, which only verification constructs
  (`install.rs:292-309` compile-fail doctest); on Windows / macOS the helper re-hashes as it copies (Phase 10,
  unchanged); Linux extraction keeps Phase 8's path checks. A batch never passes an unverified file, a URL or a path from
  the webview: the command takes names and versions only (PD-5, `deny_unknown_fields`).
- **One prompt, fixed ops.** The batch puts at most 32 `run_*_installer` ops in one job (SI-2); no op, field or argument
  is added to the helper.
- **Webview.** Three `allow-*` permissions of the app's own; no `fs` / `shell` / `http` / `dialog`; CSP unchanged; the
  settings root is a string validated by `settings::validate` before any write.
- **Environment.** One `set_var` of one fixed pair, Linux, opt-in, before the first thread (PD-12, SC-013).
- **Orphan Remove** acts only on a manifest the manager wrote, under the configured roots (`roots_match`,
  `resolve_entries`), never on user presets (manifest-relative removal).
- **Pages.** The three generators write the same page from the same tool; the card links only per-tag installer URLs
  (SC-016a), never `/releases/latest/` or the updater archive.

---

## 7. Test plan

### 7.1 SC → test

| SC | Test (file :: name) | Strategy |
|---|---|---|
| SC-001 | `src/status.test.ts` "update_all_confirmation_lists_every_update" | status fixture built with `toStatusView` (3 `update`, 2 other cards) per OS; fake `Invoker` recording calls; assert lines, admin line count, zero / one invoke and its exact args |
| SC-002 | `install_batch_tests.rs` `one_launch_windows`, `one_launch_macos`, `no_launch_linux` | `InstallRig` + fixture server serving N verified test-signed assets (Phase 7 `TestSigner`, a throwaway key per run); `FakeElevation` scripted with `ops_after` (all `done`); count `launches`, parse `jobs[0]` op list order; N ∈ {1, 3, 9} |
| SC-003 | `install_batch_tests.rs` `download_bound`, `production_bound_is_three` | `Reply::Held` gate (PD-21), injected `DownloadPolicy` with `stall` 60 s; test thread: `wait_open(min(bound, left), 30 s)`, sleep 300 ms asserting `open().len()` unchanged, `release_one()` repeatedly; peak = max open; `requests()` per path = 1 (no retry); whole case under a 30 s watchdog thread that `release_all()`s and fails |
| SC-004 | `installs_after_downloads` | a recording `DriverResolver` whose driver stamps an `AtomicUsize` sequence at entry/exit; the channel progress callback stamps each `Verified`; assert first driver stamp > last `Verified` stamp; Linux N non-overlapping intervals |
| SC-005 | `locked_item_continues`; vitest "update_all_locked_text" | `ops_after([done, locked, done])`; detection afterwards via `status::build_with_os` on the same temp roots |
| SC-006 | `notice_cases`; vitest "update_all_macos_notice", "update_all_linux_notice" | forced `Os` × {≥1 updated, none} |
| SC-007 | `tests/injection.rs` `no_process_enumeration` | the existing lexer (`lex`, `tokenized`) over non-test files of the three crates; whole-token rule; the two plants of the SC |
| SC-008 | `download_failure_excluded`, `staging_full_excluded` | a wrong-hash asset route; `FaultyStagingIo::failing(Op::Write, 1, StorageFull)` as both `staging` and `env.download.io` (PD-4) |
| SC-009 | `state_changed_items` | `env.stage_seam` (PD-6 (3b)) records, at `BatchStage::ChecksDone`, the fixture server's asset-request count (expected 0), the feed-request count (expected 1) and the `FaultyStagingIo` / `RecordingInstallIo` mutating-call counts (`Op::CreateDir`, writes, renames, removes: expected 0, `test_support.rs:651-704`); the hook runs on the calling thread before any worker exists, so the reading is exact; codes per SI-4 |
| SC-010 | `busy_items`; vitest "update_all_disables_actions" | `hold_busy_for_test` (`install.rs:3933-3938`); stage 2: a `for_command` from a second scoped thread while a `Reply::Held` keeps the batch downloading → `E_INSTALL_BUSY`; stage 3: a recording driver behind `OneDriver` (`test_support.rs:3219`) whose `run` asserts `install::any_busy(state_dir)` and that a `for_command` on a batch item is `E_INSTALL_BUSY`, then delegates to `ops_after`'s scripted effects; after `batch_for_command` returns, `any_busy` is `false`; Phase 11's self-update test hold |
| SC-011 | `settings::tests::save_for_command_cases`, `settings::tests::view_cases`, `settings::tests::root_gate_cases`; vitest "settings_section_cases"; vitest (`main.test.ts`) "main_mounts_settings_on_linux", "main_skips_settings_on_macos", "main_rereads_status_after_root_change" (PD-14) | temp roots, `Os::Linux` forced; every root string built from the temp dir and every rejection from host-appropriate values (PD-11, validation is host-lexical); manifest / journal files written by the test; expected bytes = `written_bytes_are_d10`'s (`settings_tests.rs:133`). `view_cases`: a corrupt `settings.json` at assembly → `error` starts with its `E_SETTINGS_*` code; stored `None` → `root_is_default: true` and `linux_plugin_root` the `~/.vst3` default under the temp home; a manifest present → `root_locked_by == [plugin]` from `view_with_os` alone, no save. `root_gate_cases`: (1) a thread takes `command_context` and runs `install::for_command` (Linux, forced) whose feed fetch is a `Reply::Held` (PD-21); once `wait_open(1)` holds, a root-changing save → `E_SETTINGS_BUSY`, file bytes and context root unchanged; release; the install finishes into the configured root and its manifest records it; the same save afterwards → `E_SETTINGS_ROOT_IN_USE` naming the plugin; (2) `hold_save_gate_for_test` held (a save in progress): a save → `E_SETTINGS_BUSY` (saves never interleave), and a `command_context` on another thread has not returned after 300 ms and returns `Ok` within 5 s of the drop; (3) a ticket held: `any_busy` false, no download in flight, yet a save → `E_SETTINGS_BUSY` (the window between the clone and `Busy::enter`) |
| SC-012 | `platform::tests::graphics_env_cases`, `apply_graphics_env_cases` | recording closure (`Vec<(String, String)>`) |
| SC-013 | `injection.rs` `one_set_var_site_before_threads` | lexer + `close_of` (`injection.rs:1083`) to find the `apply_graphics_env(` argument span; byte offsets of `proxy_env::read(` and `assemble_context(`; the cfg attribute on the statement; three plants |
| SC-014 | `install::tests::orphan_remove_cases` | Phase 9 fixture install placed by `LinuxDriver` in temp roots, then a feed without the plugin; 257 / 256 dummy files in `manifests/`; a held busy key for the orphan's stem (`hold_busy_for_test`, `install.rs:3933-3938`) → `E_INSTALL_BUSY`, every file still present; a journal file `<state>/journal/<stem>.json` present → `E_INSTALL_JOURNAL_PENDING` (`install.rs:132`, raised by `run_uninstall_at`, `:3592-3599`), every file still present; zero asset requests in every case |
| SC-015 | `status::tests::orphans_cases`; vitest "orphan_card_renders_remove" | cached feed through the fixture server, `FsInstallIo` |
| SC-016a / SC-016b | `manager-first-launch.test.js` | reads the contract, README, card; `floor` = FR-080's sentence (§4.10); SC-016b subtest reads `user-checks.md`'s `FR-073-sequoia` / `FR-073-tahoe` rows by their `result` cells (§4.10 row format), the reader also run over fixture copies under `tools/feed-contract/fixtures/` with none, one and both rows recorded and with an id row missing (never `# SKIP`) |
| SC-017 | `manager-release-preflight.test.js` "manager release body is exact" | updated `release-body.expected.md` (post-U-1) |
| SC-018, SC-019 | USER | `user-checks.md` rows, cited in compliance |
| SC-020, SC-028 | `manager-readiness-counts.test.js` | fixture compliance files and roadmap under `fixtures/landing/readiness/`; a planted sixth exemption; a ledger shaped like the real one (Phase 11 SC-020 `pending-ci` in the fixture compliance, a PENDING-CI row `phase11:SC-020` with verdict `post-release` and no run, `phase11:SC-014` closed by a green dry-publish run, the five D17 ids in Post-release) → exit 0; the same with `phase11:SC-020` removed from the PENDING-CI table (count short) → 1; with it in the PENDING-CI table but not in Post-release → 1; a run-closed row whose id is also a Post-release id → 1; a `post-release` row whose id is not a D17 id → 1 |
| SC-021 | `manager-release-preflight.test.js` "manager release preflight enforces the RustSec record" | `--run-date` injected; fixture `Cargo.lock`, including one where the three crates lock at different versions (`rquickjs 0.14.1`, `rquickjs-core 0.14.0`, `rquickjs-sys 0.14.2`): a `result` naming each crate's own version passes; one with two versions swapped, or naming one version for all three, is `E_PREFLIGHT_RUSTSEC` |
| SC-022 | USER | ledger |
| SC-023 | `config_guard.rs` existing guard tests (13-command lists, planted extra permission); `cargo tree -p krate-manager` diff vs `d925be34`; `git diff d925be34 -- apps/krate-manager/package.json`; package-lock top-level key set | |
| SC-024 | the FR-110 commands | §7.4 |
| SC-025 | existing scans; review of every new test's roots | |
| SC-026 | `git diff specs/Krate-manager-roadmap.md` read at close | |
| SC-027 | `install_batch_tests.rs` `input_checks` (33 items, SI-2), `refused_prompt`, `elevation_busy`, `registrar_kill_once`, `progress_mapping`, `log_lines` (`install_capture` / `take_mine`, `test_support.rs:265-282`, which collects the calling thread only, so worker `download` lines do not interfere), `batch_level_errors`; vitest "update_all_progress_lines", "update_all_malformed", "update_all_settle_rereads_status", "settings_root_change_rereads_status" | `batch_level_errors`: a fixture server answering the feed with a corrupt body and no cache → `Err(E_INSTALL_NO_FEED)`, zero asset requests, `any_busy` false, the stage seam never called; a `DriverResolver` returning `None` → `Err(E_INSTALL_NO_DRIVER)` with the stage seam never called and no item check run (no busy key, no refusal `info!` line). "update_all_settle_rereads_status": after `update_all` resolves, exactly one `plugin_status` invoke follows; after that re-render each listed card still shows its `Updated to <v>` or error line and the section shows the rescan line once |
| SC-029 | `gen-root-index.test.js`; `workflow-shape.test.js` "landing pages render with gen-root-index.js"; `release-manager-shape.test.js` subtests | fake `deps.gitTags` and `deps.repo`; byte compare with `fixtures/landing/root-index.expected.html`; `deps.repo()` `undefined` or `''` → `E_ROOT_INDEX_REPO`, exit 1, no `writeFile` call; the `landing` / `landing-dry` render steps' `env` pinned to `VERSION` and `REPO` (PD-25) |
| SC-030 | `injection.rs` `no_cancel_setter` (no `. cancel (` token sequence in `src-tauri/src`; no command name starting `cancel` in `HANDLER_LIST`, build.rs or capability); vitest "update_all_no_cancel_control", "update_all_excludes_manager_line" | |
| SC-031 | `install::tests::not_manageable_is_not_orphan`; `status::tests::orphans_cases` | |
| SC-032 | `release-manager-shape.test.js` subtest `no_advisory_scan_tool` (scans `.github/workflows/*.yml` and `tools/*.js` for `cargo audit` / `cargo deny`) | |
| SI-2 | `crates/helper/src/job_tests.rs` `core_and_helper_op_caps_agree` | |
| PD-11 (SI-21) | `injection.rs` `plugin_commands_take_the_root_ticket`; `settings::tests::root_gate_cases` (SC-011 row) | lexer over `src-tauri/src/commands.rs`; one plant per wrapper |
| PD-6 (3b) | `injection.rs` existing `request_seam` guard extended to `stage_seam` (`fixture_seam_findings`, `injection.rs:3914`) | the existing plant table gains the `stage_seam` plants |

`root-index.expected.html` is produced once, at build time, by running the **unchanged** Python of
`release.yml:800-847` (copied to the scratchpad, never into the repo, so the Node-only rule holds for committed code)
over `fixtures/landing/plugins/` with a fixture tag list, on this box; the run's command and output digest go into
the task's evidence. If Python is unavailable on the box the fixture is produced the same way by the `ubuntu-24.04`
`feed-tools` leg in a one-off branch run the user triggers.

### 7.2 Required tests (FR-071)

Every name above goes into `tests/required-tests.json`: core cases under `cargo.all` as their full paths
(`install::batch::one_launch_windows`, …, `settings::tests::save_for_command_cases`, …), `injection.rs` /
`config_guard.rs` cases by bare name, `job::tests::core_and_helper_op_caps_agree` under `cargo.all`, the vitest titles
under `vitest`; `EXPECTED_REQUIRED` mirrors it (PD-27). Every Phase 12 cargo test runs on all three legs (the Linux
behaviour is forced through `Os`, §4.13), so every name goes under `cargo.all` and none under `cargo.linux`: the
checker counts passing lines over `all` + the leg's list and wants exactly one per name (`manager-ci.yml:286-291`),
and the spec's "listed under `cargo.linux`" (SC-011) would be redundant. The Linux PENDING-CI evidence for SC-011 and
SC-014 is the `check (ubuntu-24.04)` leg's "required tests ran" notice.

### 7.3 PENDING-CI rows (exact job and assertion)

| Row | Job | Assertion |
|---|---|---|
| SC-002 macOS / Linux, SC-012, SC-013, SC-014, SC-015, SC-011 Linux | `manager-ci.yml` `check (macos-latest)`, `check (ubuntu-24.04)` | "required tests ran" notice lists the Phase 12 names, each one passing line (`manager-ci.yml:279-310`) |
| SC-024 | `check (windows-latest)`, `check (macos-latest)`, `check (ubuntu-24.04)`; `feed-tools.yml` `node-test` × 3 | green; TAP check `PLAN` and `# fail 0` |
| SC-021 | `release-manager.yml` `dry_run: true`, job `preflight` | `::notice title=preflight::passed` with the committed 0.1.0 row |
| SC-029 | `release-manager.yml` `dry_run: true`, job `landing-dry` | artifact `manager-landing-dry-run` `index.html` holds one `manager-card` with the four per-tag URLs of the dispatched version |
| R-POST-1 | user, after the real dispatch | `https://krateaudio.com/` shows the card for 0.1.0 |
| R-POST-2, R-POST-3 | user, after the next release | FR-075 |

### 7.4 Local close procedure (Windows, this box)

From `apps/krate-manager`: `cargo fmt --check`; `cargo clippy --all-targets --locked -- -D warnings`;
`cargo test --workspace --locked` (default test threads); `npm run typecheck`; `npm run lint`; `npm test`;
`node --test "scripts/*.test.js"`. From the repo root: `node --test --test-reporter=tap
"tools/feed-contract/tests/*.test.js" > f:/tmp/km12-tap.txt` then the `feed-tools.yml` TAP check script run on that file;
`node tools/header-consumers.js` → `none`. The cargo and the node suites run at the same time (different toolchains,
independent outputs).

---

## 8. Build integration and gate lanes

- **Workspace:** unchanged members (`Cargo.toml` `[workspace] members`); no new crate, feature or bin.
- **Scripts:** `package.json` unchanged (`typecheck`, `lint`, `test`, `bundle`).
- **Workflows changed:** `release.yml`, `docs.yml`, `release-manager.yml`, `feed-tools.yml`. `manager-ci.yml` unchanged.
- **Gate lanes:** app lane after every core / UI task; pipeline lane after every tool / workflow / docs task; both at
  close. No C++ lane (FR-111).
- **Cost:** `cargo test --workspace` is the long pole (minutes, already parallel per binary); the new batch tests bound
  every wait (30 s watchdog, PD-21) and use the test `DownloadPolicy` (`test_policy()`, `test_support.rs:819`), so they
  add seconds. `node --test` runs files in parallel by default; no `--test-concurrency=1` (`hygiene.test.js:362-374`).

---

## 9. Risks and mitigations

| Id | Risk | Mitigation |
|---|---|---|
| R-1 | **Windows: a DAW holds a plug-in while the batch runs.** | Inno with `/NOCLOSEAPPLICATIONS` exits 5 after ~4 s and replaces nothing (roadmap Phase 10 M-1, measured on run 41342540379); that item is `E_INSTALL_LOCKED`, later ops still run; the user record FR-074 (b) checks the real case |
| R-2 | **The user refuses the one prompt.** | Every stage-3 item `E_INSTALL_REFUSED`, nothing changed, artifacts kept; retry re-verifies from disk without the network (D6, SC-027 `refused_prompt`) |
| R-3 | **Offline or a stalled network mid-batch.** | Stage 1 runs on the cached feed (Phase 5); each download follows Phase 7's policy (4 attempts, 30 s stall); zero verified → no prompt (FR-011); no cancel in v1, so a stall waits out the policy (D15) |
| R-4 | **Linux: WebKitGTK blank window on NVIDIA.** | The opt-in toggle (rung 3 only) at next launch; the README's terminal start for an already-blank window and the other rungs (F-8); never on by default (SC-012, SC-013) |
| R-5 | **`set_var` with another thread alive.** | The call precedes `proxy_env::read` and `assemble_context`, the first places that can start threads (`sysproxy.rs:210` via `lib.rs:61`); SC-013 pins the order; a future earlier thread fails that scan only if it is spawned through those calls, so the `// SAFETY:` line names the invariant for review |
| R-6 | **Disk: a 9-item Linux batch stages every archive before installing.** | Each artifact is discarded right after its successful install (D6, per item on Linux); a full disk is that item's `E_STAGING_FULL` (SC-008) |
| R-7 | **Two Pages deploys at once** (plugin release, manager release, docs push). | All write the complete page from one tool (SI-1 / P-1); a rejected `gh-pages` push turns its job red and re-running it is safe (`keep_files: true`); the user dispatches one release at a time |
| R-8 | **The Node port differs from the Python by a byte.** | PD-16 rules, the fixture produced by the unchanged Python (§7.1), the snapshot assertions (PD-22); the first real deploy is read by the user (R-POST-1) |
| R-9 | **A settings save races a self-update restart.** | PD-9 (one write guard); FR-042's gate first |
| R-10 | **A batch item's artifact pruned by a concurrent single download.** | Not reachable from the page (PD-15); in core the helper's re-hash (Windows / macOS) or the extract's I/O error (Linux) refuses, never runs a different file |
| R-11 | **macOS admin prompt text and AU rescan cannot be automated.** | User records FR-074 (d), (e); `registrar_kill_once` covers the core half |
| R-12 | **The macOS GUI pkg route fails on Sequoia or Tahoe.** | Fence U-1 stops before any macOS text; Q5 is re-ruled first (D11) |
| R-13 | **A Linux root change races a plugin command** (Save clicked while an install's feed request is in flight). | The root gate (PD-11): the save is `E_SETTINGS_BUSY` while any ticket is held, and a command waits out a running save before cloning its context; `root_gate_cases` (SC-011) and `plugin_commands_take_the_root_ticket` pin both halves |

---

## 10. Precondition table (for the user)

| Id | Question | Recommendation | Blocks |
|---|---|---|---|
| P-1 | `docs.yml` regenerates `index.html` with the same Python (SI-1). Port it to `tools/gen-root-index.js` as the third caller? | **Yes** (PD-24): otherwise every docs push, including the one that adds the card template, removes the card | T-tasks for FR-081 / FR-085 / FR-086 and the roadmap hunks (a), (g) |
| P-2 | Phase 11 R-7's hand check of the release-asset redirect host before each manager release (SI-8 (b)): add it? | **Yes, as a manual line** in `RELEASING.md`'s procedure and one `readiness.md` row; no preflight enforcement, no network test | RELEASING.md content, ledger layout |
| P-3 | Batch cap 32 (the helper's `MAX_OPS`) instead of the spec's 64 (SI-2)? | **Yes**, plus the cross-crate equality test | FR-002, SC-027 |

---

## Review notes

Revision 1: no review applied yet.

Revision 2 (review round 1, 15 issues, all applied, none rejected):

- Blocker PD-26: the PENDING-CI table now lists every Phase 1-11 row (the file total); a row closes by a green run or
  is *deferred* (verdict `post-release`, a D17 id that is also in Post-release); old rule (d) now covers run-closed
  rows only; SC-014 and SC-020's recording is spelled out; fixture cases include the real-ledger shape (SI-19).
- Major SC-009: an explicit `#[cfg(test)]` `stage_seam` (PD-6 (3b), (4b)) replaces sampling at the first `Download`
  event; the existing `request_seam` guard is extended to cover it (SI-22).
- Major PD-14: three `main.test.ts` cases prove the Linux-only mount and the re-read wiring.
- Major PD-25 / gen-root-index: `REPO` in both render steps, pinned by the shape test; a missing `REPO` is
  `E_ROOT_INDEX_REPO`.
- Major PD-11: the per-state-dir root gate (`RootTicket` / `SaveGate`) excludes plugin commands from a root-changing
  save and serialises saves; `root_gate_cases` and `plugin_commands_take_the_root_ticket` test it (SI-21, R-13, §6).
  Chosen over the "compare after `Busy::enter`" alternative because core's `install_command` holds an `&AppContext`,
  not the `SharedState`, so it cannot read the live root without a new `AppContext` field; and because a plain
  `RwLock` read guard cannot be held across the wrappers' `.await`, the ticket is a `Send` RAII value instead.
- Minors: SC-010 stage-3 probe; `batch_level_errors`; `view_cases`; FR-060 deviation recorded as SI-20; user-checks.md
  row format and the FR-086 path (SI-16); `floor` = FR-080's sentence; per-crate RustSec bump grammar with a
  differing-versions fixture; `update_all_settle_rereads_status`; orphan busy / pending-journal cases; validation is
  host-lexical and test roots come from the temp dir.
