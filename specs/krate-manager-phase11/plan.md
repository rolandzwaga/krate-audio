# Implementation Plan: Krate Manager Phase 11 — Self-Update and the Manager's Release Pipeline

**Spec:** `specs/krate-manager-phase11/spec.md` (DRAFT revision 3; decisions D1-D17, clarifications of 2026-11-01,
`spec.md:21`, `:1120-1137`, and the plan-precondition rulings `spec.md:1138-1145`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 11 (lines 818-888); Decisions Q1 (24), Q5 (28), Q6 (29); "Self-update
path" (150-156); feed model `manager/latest.json` (139-140); Key design decisions 1 and 4 (167-169, 175-176); Repository
Layout (213-244); Phase 10's handover (845-848, 875); edges 3→11, 4→11, 7→11, 11→12 (971-972); Cross-Cutting Constraints
(999-1040, hosted-runner exception 1006-1008).
**Status:** DRAFT plan, revision 3, 2026-11-01 (revision 2 plus the user's six plan-precondition rulings,
`spec.md:1138-1145`: PD-30 to PD-33, SI-30 to SI-33, PD-2 / PD-23 / PD-24 / SI-21 / SI-23 / SI-28 rewritten, §10 now the
precondition table; see "Review notes" at the end), plus revision 4 (the second plan-challenge round, 14 issues:
PD-34 to PD-38, SI-34 to SI-40, R-20 to R-22; PD-23 / PD-27 / PD-28 / PD-31 / SI-20 rewritten; see "Review notes").
Revision 2 was verified against `feat/krate-manager` at `f6022bd5`;
the files revision 3 adds citations for were read at `30878b36` (`git rev-parse --short HEAD`; untracked:
`specs/krate-manager-phase11/` only).
**Lanes:** the **app lane** (fmt / clippy / clippy-guard / the new `clippy updater-e2e` (PD-32) and `clippy
updater-refusals` / `cargo test --workspace` / the refusal harness, on Windows or, under D9's fallback, in WSL (PD-33) /
npm typecheck / lint / vitest / `node --test "scripts/*.test.js"` / the version lint) and the
**pipeline lane** (`node --test "tools/feed-contract/tests/*.test.js"` with the TAP plan check of `feed-tools.yml`
replayed locally, YAML parse of `release-manager.yml`, `manager-ci.yml`, `feed-tools.yml`). No C++, CMake, plugin or DSP
file changes (FR-092), so there is no `run-gate.js` / `header-consumers.js` lane.

**Build preconditions.** The spec edits of §0.2 land before `tasks.md`. SI-1 to SI-4 are **blocking**: built as the spec
reads, SI-1 does not compile (E0004 on a `#[non_exhaustive]` enum), SI-2 and SI-4 turn three existing guards red the
moment the dependency and the two host rules land (`injection.rs:3192-3210`, `injection.rs:23-31`, `roster.rs:11`, and
the release-binary scan `manager-ci.yml:344-366`), and SI-3 adds a dependency (`zip`) whose stated reason is false.
SI-20 (where the real key may exist) and SI-21 (no self-update of a `.deb` install) are also **blocking**: built as the
spec reads, the real signing key sits in the environment of every third-party build script, proc macro and npm build
step on three legs, and a `.deb` self-update hands root a file that nothing re-verifies (§6). The user ruled both on
2026-11-01 (SI-20 as planned; SI-21 with the banner's "Download the new .deb" action instead of a refusing button), and
ruled the four other open points (§10 rows P-1 to P-6, all **decided**). SI-30 to SI-33 encode those rulings into spec
text and land with SI-1 to SI-29 (T002); revision 4's SI-34 to SI-40 (review fixes, no new user decision) land in the
same edit. One new decision is the user's: **R-17** (§10), the WebKitGTK packages in WSL
that ruling P-5's local WSL harness run needs; the plan is written to its recommendation.

---

## 0. What this plan adds to the spec

The spec fixes requirements, names, codes, scenarios and test names. This plan fixes: the module map and signatures on
the core and `src-tauri` sides, how the updater's reqwest client is bent to the core's URL, redirect, proxy and timeout
rules, the busy / in-flight state and its four gates, the harness crate's process model and Windows manifest, the
`updater-e2e` driver, the bundle script's release mode, the two new Node tools and the one changed one, every job of
`release-manager.yml` step by step, every guard that widens and why, and the per-SC test design. It also fills the items
the spec deferred to the plan by name (D9 manifest, D12 `APPIMAGE_EXTRACT_AND_RUN`, D17's reused core step, FR-011a's
asset host, FR-015's in-flight record, FR-016's clone proof, FR-027's `read_timeout` page, FR-072's observable process,
FR-076's install folder, FR-080's main-thread question, SC-021's element). Where the tree or a dependency contradicts a
spec sentence, the conflict is in §0.2.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **Module map.** Core: `crates/core/src/self_update.rs` + `self_update_tests.rs` (Tauri-free logic, D8). `src-tauri/src/self_update.rs` (updater adapter, kept-update slot, installer trait, error map, the `updater-e2e` module). UI: new `src/manager_update.ts` + `src/manager_update.test.ts`; `main.ts` mounts it. Harness: `tests/fixtures/updater-refusals/` (`Cargo.toml`, `Cargo.lock`, `build.rs`, `src/main.rs`, `src/scenarios.rs`, `src/keys.rs`, `src/map_errors.rs`, `src/confinement.rs`). Node: `tools/gen-manager-latest.js`, `tools/manager-release-preflight.js`, `apps/krate-manager/scripts/e2e-update-server.js`. Workflow: `.github/workflows/release-manager.yml`. | Core tests in `<stem>_tests.rs`, the core convention, skipped by the roster and literal scans (`roster.rs:39-41`; `injection.rs:1386-1390`) | New components (adds `src/manager_update.ts`) |
| PD-2 | **Updater dependency: `tauri-plugin-updater = { version = "=2.14.1", default-features = false, features = ["rustls-tls"] }`.** No `zip`, an exact pin. **Decided** (ruling P-6, `spec.md:1145`). | SI-1 (the drift protection moves to the pin), SI-3 (`zip` is not needed for v2 artifacts). `rustls-tls` keeps the Phase 5 record (`specs/krate-manager-phase5/plan.md:239-250`); `system-proxy` off as D1 says | D1, FR-001 |
| PD-3 | **Error map: one `match` naming every one of the 2.14.1 variants plus one `_` arm** → `E_SELF_UPDATE_INSTALL { detail: "unmapped updater error: {e}" }`. The table (§3.2) is pinned row by row by the harness line `map-errors` (§4.7: `map_error` over a constructed instance of every variant constructible outside the crate, 31 of the 32, each asserted to its §3.2 code and reason; `Reqwest` is reached by the network scenarios) and the variant list by PD-2's exact pin. | `#[non_exhaustive]` on `Error` (`error.rs:9-11` at `updater-v2.14.1`): outside the crate a `match` without `_` is E0004 | FR-013 (SI-1), FR-082 (SI-23) |
| PD-4 | **No banned host literal in any scanned source or the release binary.** The `ApiHost` rule matches the host's DNS labels against `const API_HOST_LABELS: [&str; 3] = ["api", "github", "com"]` (ASCII case-insensitive, a trailing root dot ignored); the endpoint-hop host rule takes `endpoint_host` as a parameter (`HopRules`), which `src-tauri` reads from the compiled `plugins.updater.endpoints[0]` at run time. Comments in the two `self_update.rs` files spell neither host. | `injection.rs:23-31` bans `krateaudio.com` and `api.github.com` as raw text in every core file outside `platform/` (`:1418-1422`); `roster.rs:1-2`, `:11` bans `api.github.com` in core, helper, `src-tauri/src`, capabilities, `src/`, `tauri*.conf.json`; `manager-ci.yml:358-359` greps the release binary for it (FR-096). The guards keep their meaning (no code path *calls* the host); nothing is exempted | FR-011, FR-011a (implementation only; SI-4 records the conflict) |
| PD-5 | **One redirect policy for both fetches, its decision a pure core function.** `configure_client` is applied to the check and to the download alike (`updater.rs:604`, `:859`; the `Update` carries the builder's context, `:739`). The closure is one call, `core::self_update::redirect_decision(&rules, &endpoint, attempt.previous(), attempt.url())`, which (1) refuses `Url { Policy }` once `previous.len() > http::MAX_REDIRECTS` (5, `http.rs:24`), (2) classifies the fetch with `hop_kind(previous[0], endpoint)` (`Endpoint` iff the chain's first URL equals the endpoint; reqwest 0.13.5 `redirect.rs:135`: "The first URL in the previous is the initial URL"), (3) runs `check_update_hop`. Both are cargo-tested by `redirect_hops` (SC-001). A refusal becomes `attempt.error(HopRefused(..))`; the map of PD-3 walks `std::error::Error::source()` of `Error::Reqwest` and downcasts to `HopRefused`. | reqwest 0.13.5 `redirect::Policy::custom` (`redirect.rs:102-109`), `Attempt::{url, previous, follow, error}` (`:167-207`), `ClientBuilder::redirect` (`async_impl/client.rs:1392-1395`) | FR-011a |
| PD-6 | **D17's reused core step.** `http.rs` gains `pub fn route_choice(config: &HttpConfig, url: &Url) -> Result<ProxyChoice, HttpError>`: `prepare` (URL check, static-proxy check, `:948-966`), then `open_pac` (`:749`) when `config.pac` is set, then the hop's choice list from `static_route` (`:861-868`) or `pac_route` (`:904-946`), first entry. `pac_route` is split into `pac_choices(…) -> Result<Vec<ProxyChoice>, HttpError>` (the PAC answer and its static fallback, as `ProxyChoice`) and the existing ureq conversion over it, so `get`'s route closure (`:976-982`) is byte-for-byte the same decision. `self_update::proxy_choice` calls `route_choice` with the PAC deadline `config.total_timeout` and **returns its `Result`**: where `route_choice` errs (a static proxy refused by `check_proxy` in `prepare`, `http.rs:948-966`; a PAC-answered proxy refused, `:927-934`) the core's own client would not connect, so the self-update makes no request either and fails `E_SELF_UPDATE_NETWORK { detail }` (never `Direct`, which would bypass the configured proxy). With PD-30 every `Proxy` choice `route_choice` can return (the four schemes `check_proxy` admits, `http.rs:253-254`, `:305-322`) is one the updater's client can use. | The spec forbids re-implementing PAC (D17); this reuses every existing step and only moves the ureq conversion; failing closed keeps D17's "the decision the core's own client makes" | D17, FR-012 (SI-26) |
| PD-7 | **`SelfUpdateInFlight` is a process registry keyed by state dir, like `install::BUSY` (`install.rs:3841`) and `download::IN_FLIGHT` (`download.rs:647`).** `static SELF_UPDATING: Mutex<BTreeSet<Option<PathBuf>>>`; `InFlightGuard::enter(state_dir: Option<&Path>)` inserts, `Drop` removes; `self_update::in_flight(state_dir: &Path) -> bool`. The action gate `self_update::action_gate(state_dir: &Path) -> Result<(), SelfUpdating>` is called first in `download::for_command`, `install::for_command`, `install::remove_for_command` and `install::elevate` (before `guard().enter`, `install.rs:1299`). `SelfUpdating` (unit struct, `Display` = `"E_INSTALL_SELF_UPDATING: Krate Manager is updating itself. Try again after it restarts."`) converts into `InstallError::SelfUpdating` and `DownloadError::SelfUpdating`; the literal code lives once, in `self_update.rs`. | Keying by state dir keeps parallel cargo tests and concurrent harness scenarios independent, exactly as BUSY does; a field in `AppContext` would break its `PartialEq` derive (`context.rs:52`) and every construction site; `download_plugin` returns `DownloadError` (`commands.rs:64-103`), not `InstallError` | FR-017, FR-018 (SI-15) |
| PD-8 | **Busy rule inputs.** `ElevationGuard::is_held(&self) -> bool` (hold ≠ `Free`, so `Running` and `Stale`, `install.rs:593-603`); `install::any_busy(state_dir: &Path) -> bool` over `BUSY`; `download::any_in_flight(downloads: &Path) -> bool` over `IN_FLIGHT` (the existing in-flight record, FR-015's "names the existing record"). `self_update_busy(elevation: &dyn Elevation, ctx: Option<&AppContext>) -> bool`. | Each reads an existing lock; none adds state | FR-015 |
| PD-9 | **One `HttpConfig` for the self-update in both modes, chosen by a core function.** `lib.rs` clones `http` before `assemble_context` consumes it (`lib.rs:64-70`; `failed()` drops it, `context.rs:134-140`) and manages `SelfUpdateHttp(http.clone())`, the fallback. Each command calls `core::self_update::self_update_http(ctx.as_ref(), &fallback) -> HttpConfig`: `ctx.http` with a context (reassembly keeps `http`, `context.rs:188`), the fallback (`HttpConfig::production()` plus Phase 5's resolved proxy, `lib.rs:64-67`) with none, as FR-026 asks. `no_context_defaults` tests the choice; an `injection.rs` rule pins that `lib.rs` passes exactly `SelfUpdateHttp(http.clone())`, before `assemble_context(roots, &feed_url, http,` (plant: `SelfUpdateHttp(HttpConfig::production())`). | FR-026 must work with `StartupError::Roots` / `LockIo`; SC-025's test must sit in core, which can name neither `lib.rs` nor `src-tauri` types (`src-tauri` `[lib] test = false`, `src-tauri/Cargo.toml:10-19`) | FR-026, SC-025 (SI-24) |
| PD-10 | **`release_for_restart` proof.** The only clone of `AppState.lock` is `reassemble_with_os` (`context.rs:165`), reached only through `apply_settings` (`:198-204`), which has no caller in `src-tauri/src` (grep, this session). `StateLock` has no `Drop`; closing its `File` releases the lock (`state_lock.rs:38-43`). `release_for_restart(state: &SharedState) -> LockRelease { Released, NoLock, StillShared }` takes the write guard, `take()`s the `Arc` and `Arc::into_inner`s it. `StillShared` is logged (`E_SELF_UPDATE_LOCK_SHARED` as a log code only) and the restart goes on: the new process then shows `LOCKED_NOTICE` and quits (`context.rs:17-19`), which is unreachable today and is Phase 12's to keep unreachable (R-9). | FR-016's "the plan proves no other clone is held" | FR-016 |
| PD-11 | **Harness process model: one child process per scenario, all spawned at once** (`--scenario <name>`), the parent collecting and sorting lines (a plant run spawns only `good`, PD-38). Each child gets its redirected `LOCALAPPDATA`, `APPDATA`, `USERPROFILE`, `HOME`, `TMP`, `TEMP`, `TMPDIR`, `XDG_*_HOME`, `XDG_RUNTIME_DIR` through `Command::envs` before it starts, builds its mock app on its own main thread, its own fixture servers and its own process-wide registries (`BUSY`, `IN_FLIGHT`, `SELF_UPDATING`, the rustls default provider the updater installs, `updater.rs:578-582`). Throwaway keys exist only in memory. **Windows limit:** the redirected variables do not move known-folder lookups: dirs-sys 0.5.0 resolves `FOLDERID_RoamingAppData` / `FOLDERID_LocalAppData` with `SHGetKnownFolderPath` (`dirs-sys-0.5.0/src/lib.rs:150-181`, locked `Cargo.lock:803-804`), which ignores them; so the parent also snapshots, read-only, the app-identifier folders under both known folders before and after the run (§4.7, SI-29). | (1) the macOS main-thread question cannot be measured on this box; `MockRuntime::new` has no thread check (`tauri-2.12.5 src/test/mock_runtime.rs:1174-1216`) but the child form needs no answer; (2) edition 2024 makes `std::env::set_var` `unsafe`; (3) no shared static between scenarios | FR-080, FR-081 (b) (SI-8, SI-9) |
| PD-12 | **Harness Windows manifest:** the harness `build.rs` embeds tauri-build 2.7.4's default manifest (`src/windows-app-manifest.xml`: one `dependentAssembly` on `Microsoft.Windows.Common-Controls` 6.0.0.0, `publicKeyToken="6595b64144ccf1df"`) with `tauri_winres::WindowsResource::new().set_manifest(MANIFEST).compile()` (tauri-winres 0.3.7 `src/lib.rs:530`, already in the lock as tauri-build's dependency), `#[cfg(windows)]` target check via `CARGO_CFG_TARGET_OS`. `compile()` links into binaries, and the harness is a binary. | tauri PR #16320 (read 2026-11-01): the load failure is `TaskDialogIndirect` needing Common Controls v6, and tauri-build links the manifest only into binaries of the crate that runs it (`tauri-build-2.7.4 src/lib.rs:795-816`); #13948 is exactly a second bin crate importing the app lib | D9 (fills it) |
| PD-13 | **macOS `pkg-installed` observable: the `SecurityAgent` process.** The updater runs `do shell script … with administrator privileges` **in-process** through `osakit` (`updater.rs:1594-1597`; osakit 0.3.1 "Uses `OSAScript` class from `OSAKit Framework` directly", https://docs.rs/osakit/0.3.1/osakit/), so no `osascript` child exists before authorization; the inner `/usr/bin/osascript -l JavaScript` runs only after it, as root (`updater.rs:1688-1691`). The authorization dialog is drawn by `SecurityAgent`. The job polls `pgrep -x SecurityAgent` (≤ 60 s) and cancels with `sudo pkill -x SecurityAgent`, repeated each second until `error-<code>` appears or 30 s pass. | FR-072 asks the plan to name it from the source | FR-072 (SI-13) |
| PD-14 | **The `updater-e2e` driver runs in the `setup` hook, not the webview**, and reads the version from `app.package_info().version` (the merged `--config` version), never `CARGO_PKG_VERSION` (equal for "old" and "new", which share `Cargo.toml`). | A webview that fails to start on a runner must not hide the self-update verdict; `commands.rs:18-21` returns `CARGO_PKG_VERSION` | D10 |
| PD-15 | **Release asset location: fixed bundler dir + one glob per asset**, zero or more than one match `E_BUNDLE_ASSET` (exit 1). Dirs: `target/release/bundle/nsis/*_<v>_x64-setup.exe` (and `.sig`); `target/universal-apple-darwin/release/bundle/macos/Krate Manager.app.tar.gz` (and `.sig`), `…/macos/Krate Manager.app`; `target/release/bundle/appimage/*_<v>_amd64.AppImage`, `…/deb/*_<v>_amd64.deb` (and `.sig`s). | Bundler names carry spaces and the product name (`manager-ci.yml:374`, `:386`); FR-032 | FR-032 |
| PD-16 | **The two new Node tools are not `PHASE2_TOOLS` and add no code to `contract.js` `CODES`.** Their own tests assert the `tools/*.js` form (shebang, `main(argv, io)`, `usage: node tools/<file>` on exit 2). They join `hygiene.test.js`'s offline scan (`scannedFiles`, `:34-46`, and its completeness list, `:119-134`). | `hygiene.test.js:575-603` requires every code of a `PHASE2_TOOLS` tool in `CODES`, and `CONTRACT.md documents every code` (`:452`) would then need `CONTRACT.md`, which FR-092 fences | FR-091 |
| PD-17 | **`checkLatestJson(bytes, tag, version, repo)`** gains the repo and is exported; `collectManager` passes `collect`'s `repo` (`feed-publish.js:376-393`, `:1015`). | FR-043, FR-059 (`module.exports`, `:1029-1043`, does not export it today) | FR-043 |
| PD-18 | **`ManagerUpdateView` gains `os: "windows" \| "macos" \| "linux"`** (and, by PD-31, `AvailableView.action`). | FR-024's macOS-only sentence must render when `plugin_status` rejected and there is no `StatusView.os` (`view.ts:95`) | FR-014 (SI-6) |
| PD-19 | **Two feed jobs:** `feed` (`needs: publish`, `if: ${{ !inputs.dry_run }}`, `dry_run: false`) and `feed-dry` (`needs: dry-publish`, `if: ${{ inputs.dry_run }}`, `dry_run: true`); both grant `permissions: contents: write`, because a called workflow cannot hold more than its caller grants and `feed.yml`'s `deploy` declares `contents: write` (`feed.yml:102-107`) even when its `if` skips it. | GitHub: "permissions can only be maintained or reduced—not elevated—throughout the chain" (https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows, read 2026-11-01); `deploy` runs only on `!inputs.dry_run && refs/heads/main` (`feed.yml:104`) | FR-058 |
| PD-20 | **`bundle.js --release` also accepts `--features updater-e2e`** (that exact value; any other `--features` is exit 2), appended to the `tauri build` argv. The e2e job builds through `bundle.js` so the helper sidecar is staged exactly as in a release build. | FR-071 builds with `--features updater-e2e`; `tauri.helper.conf.json` needs the staged sidecar (`bundle.js:89-98`) | FR-030 (SI-17) |
| PD-21 | **`gen-manager-latest.js --only <windows\|macos\|linux>`**, allowed only together with `--repo-url-base` (else exit 2), restricts D6's asset table to one OS for the e2e job. | Each `updater-e2e` leg has only its own OS's assets; FR-042 would otherwise refuse with `E_LATEST_ASSETS` | FR-040, FR-071 (SI-18) |
| PD-22 | **The e2e macOS leg builds `--target universal-apple-darwin` too**, so the swap it proves is the release artifact's; both e2e builds share the cargo cache (only the merged config differs). | Cheaper alternatives (a native-arch build) would gate a different `.app.tar.gz` than users get | FR-071, FR-072 |
| PD-23 | **The real key never meets a build.** **Decided** (ruling P-2, `spec.md:1141`: accepted as written). Every `build` leg, dry or real, bundles with a per-leg throwaway key made and used inside its one bundler step (no environment, no secret, `KRATE_BUNDLE_EXTRA_CONFIG` empty on both kinds of run, so the built app always carries the committed pubkey). A new job `sign` (`needs: [preflight, build]`, `if: !inputs.dry_run`, `environment: signing`) downloads the three legs' updater artifacts and re-signs them in the plugin pipeline's way: `node tools/minisign.js decrypt-key` into `$RUNNER_TEMP`, `rsign sign -W -t "timestamp:<n>\tfile:<name>\tversion:<v>"`, `.sig` = `base64 -w0` of the `.minisig`, key deleted in the next step. Its twin `sign-dry` (`if: inputs.dry_run`, no environment) runs the same steps with `rsign generate -W` instead of the decrypt. `rsign2` is compiled fresh in both (no cache restore, PD-35), and the same signing steps sign the "new" asset of `updater-e2e`, so the updater's own verifier checks an rsign-made `.sig` before any release ships one (PD-35). A gate job `main-only` (`needs: preflight`, `environment: signing`, one `echo` step, no secret reference) keeps D14's "dry runs start only from `main`" before any build. | Tauri CLI 2.12.4 only *warns* when the signing key does not match `plugins.updater.pubkey` (`crates/tauri-cli/src/bundle.rs:313-318`, `log::warn!`), so a throwaway-key build succeeds and the release signature can be made afterwards; Tauri's `.sig` is base64 of the minisign box with that trusted comment (`helpers/updater_signature.rs:161-186`); `release.yml:607-655` already signs this way with this key ("PD-20: no later step runs while the key exists", `:655`); keys README: "Only jobs that reference the environment receive these secrets" (`tools/feed-contract/keys/README.md:44`) | D14, FR-052, FR-053, FR-055, FR-057, FR-059, FR-091 (SI-20) |
| PD-24 | **No self-update of a system-package install; the banner offers the new `.deb` instead.** **Decided, changed by ruling P-3** (`spec.md:1142`). Core `enum Packaging { SystemPackage, SelfContained }` and `self_install_supported(p) -> Result<(), SelfUpdateError>` (`SystemPackage` → `E_SELF_UPDATE_UNSUPPORTED`); `src-tauri` maps `tauri::utils::platform::bundle_type()` (`Some(Deb \| Rpm)` → `SystemPackage`, else `SelfContained`) into `UpdateInputs.packaging`. `check` still runs, so a `.deb` user sees the banner, but the view's `AvailableView.action` is `OpenReleasePage { url }` (PD-31) and the UI shows "Download the new .deb", which calls the existing `open_release_page` command; no "Update and restart" button is shown. `install` still refuses first, before the busy check, any request or the slot (`E_SELF_UPDATE_UNSUPPORTED`): the backstop for a caller that is not the UI (the e2e driver, a forged IPC call). `latest.json` drops `linux-x86_64-deb` and the release drops the `.deb.sig`; the `.deb` stays a first-install asset. | The updater picks its Linux install path from the same `bundle_type()` (`updater.rs:31`, `:1213-1219`; `tauri-utils-2.10.2/src/platform.rs:352-378`; `tauri` re-exports it as `tauri::utils`, `tauri-2.12.5/src/lib.rs:124`); for a `.deb` it writes the verified bytes as the user to `<temp>/tauri_deb_update*/package.deb` and runs `pkexec dpkg -i`, then zenity / kdialog → `sudo -S`, then `sudo` (`updater.rs:1329-1410`): root installs a user-writable file nothing re-verifies, which the roadmap's own elevated-install rule forbids (roadmap line 23: "the helper re-hashes it as it copies it into an admin-only directory") | D6, FR-013, FR-041, FR-072 (SI-21) |
| PD-25 | **Self-update progress through a channel.** `manager_update_install` takes `on_progress: tauri::ipc::Channel<SelfUpdateProgress>` (as `download_plugin` does, `commands.rs:68`, `:92`); core `#[derive(Serialize)] enum SelfUpdateProgress { Downloading, Installing }`. `install` sends `Downloading` after the in-flight flag is set and `Installing` only after FR-019's re-check passes, just before the installer. | FR-024's two progress texts need the download → install boundary, which the one `Result<(), String>` return never reports (on success the process ends) | FR-024 (SI-22) |
| PD-26 | **`updater-e2e` code sites.** The `#[cfg(feature = "updater-e2e")] pub mod e2e` in `src-tauri/src/self_update.rs` holds all e2e code, including the `compile_error!` against `dev-feed` (`#[cfg(feature = "dev-feed")] compile_error!(…)` inside the module); `lib.rs` holds exactly two single-statement call sites, `#[cfg(feature = "updater-e2e")] let (feed_url, http) = self_update::e2e::override_feed(feed_url, http);` after `lib.rs:69` and `#[cfg(feature = "updater-e2e")] self_update::e2e::start(app.handle());` in `setup`. The existing `dev-feed` arms (`lib.rs:4-5`, `:17-18`, `:48-55`, `:63-69`, `:75-78`) are untouched. An `injection.rs` rule counts `feature = "updater-e2e"` per file: `self_update.rs` 1, `lib.rs` 2, every other file under `crates/*/src`, `src-tauri/src` 0 (plant: an extra arm in `commands.rs`). | The swap needs some site outside the module; two named call sites plus a counting guard keep every e2e branch findable | FR-070 (SI-25) |
| PD-27 | **The package URL has a fixed origin: this project's own release of the announced version** (revision 4 adds the repo and version halves). `check_update_package_url(policy, url, announced)`: `check_update_url` first; then, under both policies, `per_tag_segments(url)` must yield exactly 7 non-empty segments `[owner, repo, "releases", "download", "manager", "v<x.y.z>", name]` with `owner/repo` equal to the compiled `MANAGER_REPO` (`"rolandzwaga/krate-audio"`, the origin remote, §1.1) and `v<x.y.z>` string-equal to `"v" + announced` (`Update.version`), no query, no fragment; under `HttpsOnly` the host must also equal `GITHUB_HOST` (under `LoopbackOnly` any loopback host that passed `check_update_url`); anything else `Url { Host }`. Applied to `Update.download_url` before `download`, replacing the plain `check_update_url` call, and inside `update_action` (PD-31). `manager-release-preflight.js` refuses `E_PREFLIGHT_REPO` unless its `--repo` (`${{ github.repository }}`, the value every `latest.json` URL is built from) equals `MANAGER_REPO` read from `crates/core/src/self_update.rs`, so the compiled constant and the release workflow cannot drift. | Revision 1 constrained the host only on redirects (§4.1 `check_update_hop`), so a `latest.json` naming any https host was fetched directly; the per-tag form is what `feed-publish.js` already requires of `latest.json` (FR-043). Revision 3 left `owner/repo` and the tag free: an unsigned `latest.json` could point the `.deb` button (PD-31), which no signature protects, at any GitHub release page, or announce 9.9.9 while naming an older tag (a downgrade through the UI), or make the self-contained path fetch any GitHub asset up to the updater's in-memory body before the signature check (§1.2 `download`). `feed-publish.js` takes the repo only as `--repo "$GITHUB_REPOSITORY"` (`feed.yml:60`, `feed-publish.js:66`, `:958-959`) and holds no repo constant, so the pin is the preflight's, not `config_guard`'s | FR-011, FR-011a (SI-27, SI-40), FR-051 (SI-40) |
| PD-28 | **Busy before the slot.** `install` checks FR-015's busy rule before it takes the kept update, so an FR-015 refusal leaves the update in `KeptUpdate` and the retry after the plugin action finishes installs it; only FR-019's post-download refusal (and every later error) leaves the slot empty. | Spec edge case "E_SELF_UPDATE_BUSY, nothing downloaded; retry when it finishes" (`spec.md:1008`). FR-023's wording ("cleared after a failed install (including FR-019's busy refusal)", `spec.md:471`) does not exclude an FR-015 refusal, which is a failed `manager_update_install` call too, so SI-36 rewords it (revision 3 said no SI was needed; it was) | FR-015, FR-023 (SI-36) |
| PD-29 | **Timeouts and the self-update `HttpConfig` live in core.** `core::self_update::UpdateTimeouts { check, download, stall }` with `const fn production()` (30 s, 3600 s, 30 s) and `self_update_http` (PD-9); `src-tauri` only forwards them. `src-tauri`'s `prepare_download(&mut Update, &UpdateInputs)` (package URL, `Update.timeout`, proxy) is the one place `Update.timeout` is set, and the harness `good` scenario runs it with `UpdateTimeouts::production()` and asserts the kept `Update.timeout == Some(DOWNLOAD_TIMEOUT)`. | SC-024 / SC-025's tests are core cargo tests; `UpdateInputs` and `build_updater` are `src-tauri` types that core cannot name | FR-027, SC-024, SC-025 (SI-24) |
| PD-30 | **SOCKS reaches the updater's client** (ruling P-1, `spec.md:1140`; overturns revision 2's "built without it", §10 Q1 and R-6). `src-tauri/Cargo.toml` `[dependencies]` holds `reqwest = { version = "=0.13.5", default-features = false, features = ["socks"] }`. Cargo unifies the features of the one `reqwest` 0.13.5 in the graph, so the client the updater builds in `check` and `download` (`reqwest::Proxy::all(proxy.as_str())`, `updater.rs:599`, `:855` at `updater-v2.14.1`) speaks SOCKS4 and SOCKS5. The feature adds **no crate**: in 0.13.5 it is `socks = []` (`Cargo.toml:178`), gating reqwest's own code over hyper-util's `SocksV4` / `SocksV5` (`src/connect.rs:1877-1960`), and reqwest already enables hyper-util's `client-proxy` on every target (`Cargo.toml:347-355`; hyper-util 0.1.21, `Cargo.lock:1600-1601`, `src/client/legacy/connect/proxy/mod.rs:2-5`), so no lockfile changes because of it (T001 re-locks all three and asserts so). DNS matches the core: reqwest's `socks4` / `socks5` resolve the target locally (`connect.rs:538-541`, `DnsResolve::Local`), as ureq 3.5.0 does for those schemes (`ureq src/proxy.rs:56-63`); `socks4a` / `socks5h` stay refused by `check_proxy` (`http.rs:253-254`). Proven by the harness scenario `socks-proxy` (§4.7). It is a harness scenario, not a unit row: the harness can host a SOCKS listener (a std-only relay, `src/socks.rs`), and no unit test can reach the updater's client (core cannot name `reqwest`, FR-094; `src-tauri` has no test target, `src-tauri/Cargo.toml:10-19`; `FixtureServer` speaks only HTTP and CONNECT, `fixture_server.rs:1-61`). | Without the feature, `IntoProxy` still accepts a `socks5://` URL (`proxy.rs:124-137`) but `connect_via_proxy` skips the SOCKS branch (`connect.rs:783-792`) and speaks HTTP-proxy protocol to the SOCKS port, so a SOCKS-only network could never self-update while its plugin downloads work (core `ureq` has `socks-proxy`, `apps/krate-manager/Cargo.toml:47`) | FR-001, FR-022, D17, FR-080, FR-082 (SI-28, SI-30) |
| PD-31 | **`AvailableView.action: UpdateAction`** (ruling P-3, `spec.md:1142`). `#[serde(tag = "kind", rename_all = "snake_case")] enum UpdateAction { InstallAndRestart, OpenReleasePage { url: String } }`, chosen by the core function `update_action(packaging, policy, download_url, announced) -> Result<UpdateAction, SelfUpdateError>`: `SelfContained` → `InstallAndRestart`; `SystemPackage` → the release page of the announced version in this project's repository, `https://github.com/<MANAGER_REPO>/releases/tag/manager/v<announced>`, built only after the checked `Update.download_url` passed PD-27's full rule (`MANAGER_REPO`, tag = `v<announced>`; host `GITHUB_HOST` under `HttpsOnly`, any loopback host under `LoopbackOnly`), never from the URL's own `owner/repo` or tag, built on `status::RELEASE_PAGE_HOST` and kept only if `status::release_page_url` returns it unchanged, which is exactly the check `open_release_page` applies (`commands.rs:54-59`; `status.rs:19-21`, `:350-365`: https, host `github.com`, no userinfo, no port, ≤ 2048 bytes). A download URL without that path → `Err(Url { Host })`: a check error, no banner. The page form is the plugin feed's (`tools/gen-feed.js:475`, `https://github.com/${repo}/releases/tag/${tag}`). | The ruling lets the view carry the packaging or an action enum; the action keeps the URL rule in core beside PD-27, so the UI only renders (it may name no URL, `config_guard.rs:1117-1160`) and gets an address `open_release_page` is known to admit | FR-014, FR-024, FR-072 (SI-31) |
| PD-32 | **`manager-ci.yml` `check` lints the `updater-e2e` build** (ruling P-4, `spec.md:1143`; replaces revision 2's local-only TA-14). Step `clippy updater-e2e` directly after `clippy manual-elevation example` (`manager-ci.yml:109-116`) and before `clippy-guard`, on all three legs, in that step's form: step `env:` `KRATE_E2E_FEED_URL: 'http://127.0.0.1:9/versions.json'`, `KRATE_E2E_MARKER_DIR: 'never-run'`, `KRATE_E2E_OLD_VERSION: '0.0.0'` (dummies: compiled and linted, never run); `if cargo clippy -p krate-manager --all-targets --features updater-e2e --locked -- -D warnings; then` → `::notice title=clippy updater-e2e ($RUNNER_OS)::clean (compiled and linted, never run)`, else `::error` and exit 1. It joins `manager-ci-shape.test.js` `STEPS` (`:167-201`), `ASSERTION_STEPS` (`:211-229`, it prints a notice) and `COMMANDS` (`:232-373`), and a new rule keeps `--features updater-e2e` and the three `KRATE_E2E_*` names, within `check`, in that step only (as `CI_FEATURE_STEPS` does for `ci-temp-roots`, `:438-443`). It writes only debug-profile check metadata, so `updater-e2e absent` (`cargo tree` without the feature, and the release binaries) reads the same. | A break in the e2e module shows on push, not first in a release dispatch; the form copies `clippy dev-feed` / `clippy manual-elevation example` (`:91-116`) | FR-073, FR-083 (SI-32) |
| PD-33 | **D9's fallback runs without a stop** (ruling P-5, `spec.md:1144`; supersedes the stop of `spec.md:1134` and `:266-270`). T014's first Windows run is the measurement; T014 writes `specs/krate-manager-phase11/evidence/d9-windows.txt`, one line, `D9 WINDOWS LOADS` or `D9 WINDOWS LOAD FAILURE exit=<code> <loader message>`. On a failure: (a) nothing stops and T014 does not return `blocked`; the harness is still formatted and clippy-linted on Windows (the failure is at load, not at compile); (b) its run and both plants move to WSL Ubuntu 24.04, run by `harness-lane.js` itself (`CARGO_TARGET_DIR=$HOME/km-target-harness`), which needs the CI Linux leg's packages there (R-17); (c) `manager-ci.yml`'s `updater refusals` step gets `if: runner.os != 'Windows'` (T017 reads the d9 file and writes both the step and the shape test's `HARNESS_ON_WINDOWS` constant from it, §4.11), and `clippy updater-refusals` stays on all three legs; (d) compliance records SC-007's Windows row as a gap quoting the d9 line, never as a pass. On `LOADS` none of (b)-(d) applies. | the user's ruling; the d9 file turns the G3 edit (T017) and the close verdict into functions of a recorded measurement | D9, FR-080, FR-084, SC-007 (SI-33) |
| PD-34 | **Linux AppImage builds set `NO_STRIP: 'true'`; the Linux `updater-e2e` leg stays on the bare `ubuntu-24.04` runner.** The Linux `bundle` step of `build (linux)` and of `updater-e2e (ubuntu-24.04)` (both builds) carries step `env:` `NO_STRIP: 'true'`, pinned equal by the shape test (a plant drops it from one leg). The e2e leg does not move into the `ubuntu:22.04` container. The build-host difference (24.04 e2e build, 22.04 container release build) is accepted for Linux, unlike macOS (PD-22), and recorded. | linuxdeploy's strip step has been reported to fail on GitHub's Ubuntu images with `unknown type [0x13] section .relr.dyn` (tauri-apps/tauri issue #14796, 2026-01-19, closed; the reporter saw it on `ubuntu-latest` and 22.04; maintainer FabianLars: "the requirement to build with `NO_STRIP=true` set is not news but it shouldn't be required on Ubuntu, only rolling release distros"). linuxdeploy skips stripping when `NO_STRIP` is set at all (`src/core/appdir.cpp:247-249`, linuxdeploy master `6bcfbfe`), and the Tauri bundler starts linuxdeploy with the inherited environment plus `OUTPUT`, `ARCH`, `APPIMAGE_EXTRACT_AND_RUN` (`crates/tauri-bundler/src/bundle/linux/appimage/linuxdeploy.rs:172-176`, tag `tauri-cli-v2.12.4`), so the step env reaches it. Setting it on both legs keeps the e2e AppImage and the release AppImage alike in this respect (the cost: unstripped bundled libraries, a larger AppImage). The container is refused for the e2e leg: FR-072 / FR-076 run the AppImage with `libfuse2t64` and without `APPIMAGE_EXTRACT_AND_RUN` "through the same runtime path as a user's double-click" (`spec.md:650-652`) and FR-091 bans that variable in `updater-e2e` (`spec.md:779`), while a job `container:` has no FUSE device, so the AppImage could only run extracted there. What the e2e leg proves (the updater's AppImage replace and relaunch) is the runtime's and the updater's, not the build host's; that the release AppImage itself runs on 24.04 is `first-install (ubuntu-24.04)`'s (FR-076). No push-time lane builds an AppImage (`tauri.linux.conf.json` = `["deb"]`, §1.1), so the first AppImage build anywhere is a `release-manager.yml` dispatch (R-21) | FR-064, FR-071, FR-072 |
| PD-35 | **The release signing steps are proven by the updater's own verifier, and `rsign2` is never restored from a cache where the real key will exist.** (a) `sign`, `sign-dry` and `updater-e2e` share four steps, `install rsign2` (`cargo install rsign2 --version 0.6.7 --locked`, then `test "$(rsign --version)" = "rsign2 0.6.7"`), `updater asset list`, `sign updater artifacts` and `delete signing key (after signing)`, with byte-equal `name` and `run` text (differing only in step `env:` values), and `updater-e2e` also carries `sign-dry`'s `throwaway signing key` step byte-equal; the shape test asserts the equality (plant: one character changed in `updater-e2e`'s copy). `updater-e2e`'s old build trusts that rsign throwaway key (its `--config` pubkey = `base64 -w0` of the rsign `.pub`), its CLI build key is a second per-build throwaway (as on the `build` legs), and its `latest.json` is built from the rsign `.sig`s, so the self-update each leg performs feeds an rsign-made `.sig` to `Update::download` / `verify` (minisign-verify, `verify_signed_version`). (b) The harness scenario `rsign-format` does the same locally with a committed rsign-made fixture (§4.7). (c) Neither `sign` nor `sign-dry` uses `actions/cache` or `Swatinem/rust-cache`; `rsign2` is compiled fresh before the key step (plant: a cache step added back). | (a) Revision 3's e2e signed with the Tauri CLI inside `bundle.js --release`, and its harness signs with the `minisign` crate (§4.7), so nothing fed an rsign `.sig` (rsign's untrusted comment, its `-t` handling, the `base64 -w0` framing) to the updater's verifier before users would; only `tools/minisign.js` checked them. (c) The cache key `rsign2-0.6.7-${{ runner.os }}-${{ runner.arch }}` is also saved by `feed-tools.yml` (`:65-76`), which runs `npm ci --prefix tools/feed-contract` with lifecycle scripts on in the same job before the cache step (`:62`), and by `backfill-sign.yml` (`:161-175`, `:243-257`); a default-branch entry written by a compromised dependency there would be restored into the job that then hands the binary the decrypted key, and `rsign --version` (`release.yml:623-624`) proves only what the binary prints. The same exposure in `release.yml:610-624` is FR-092-fenced: recorded for the user (R-15) | FR-052, FR-055, FR-071, FR-072, FR-080, FR-091, SC-007, SC-015 (SI-20, SI-34) |
| PD-36 | **The production inputs are pinned at their one construction site.** `UpdateInputs::production(http)`'s body is exactly `Self { http, policy: UrlPolicy::HttpsOnly, timeouts: UpdateTimeouts::production(), packaging: packaging() }`; `commands.rs` builds inputs only as `UpdateInputs::production(`; the only other `UpdateInputs` value in `src-tauri/src` is the e2e module's `UpdateInputs { policy: UrlPolicy::LoopbackOnly, ..UpdateInputs::production(loopback_http) }` form (§4.5), so the e2e build runs production's timeouts and packaging and the e2e `deb` case proves production's `packaging()` on a real `.deb`; `UrlPolicy::LoopbackOnly` occurs in `src-tauri/src/self_update.rs` only inside `mod e2e`. Pinned by `injection.rs` rules with a plant each (§4.12) and by the harness line `production-inputs`, which calls `UpdateInputs::production(HttpConfig::production())` and asserts `policy == HttpsOnly` and `timeouts == UpdateTimeouts::production()` (and `packaging == packaging()`, which in an unbundled harness reads `SelfContained` on both sides: the `.deb` half is the body pin's and the e2e `deb` case's) | Revision 3's harness and e2e built their own `UpdateInputs` and the SI-24 pins checked only that `build_updater` / `prepare_download` read `inputs.timeouts.*`, so a production constructor passing `LoopbackOnly`, a 300 s stall or a constant `SelfContained` stayed green everywhere (FR-011 "production passes `HttpsOnly`", `spec.md:411`; FR-027 "production passes 30 s", `spec.md:497`) | FR-011, FR-027, SC-024 (SI-35) |
| PD-37 | **macOS: no self-update the app could not relaunch from.** `install` step 1b (`#[cfg(target_os = "macos")]`): `tauri::process::current_binary(&app.env())`; an `Err` → `Err(Unsupported)` before the busy check, the slot or any request. The `process-relaunch-dangerous-allow-symlink-macos` feature stays off: `config_guard` already requires `tauri = { workspace = true }` in `src-tauri` and `features = []` on the workspace `tauri` entry (`config_guard.rs:931-960`), so adding it is red; no code change there. A symlinked launch path is a known limit, not an e2e case (R-22) | `AppHandle::restart` → `process::restart`, which calls `current_binary` → `tauri_utils::platform::current_exe` and, on an `Err`, skips the spawn and reaches `exit(0)` (`tauri-2.12.5/src/process.rs:48-56`, `:74-88`); `current_exe` "will return an error if the original path contained any symlinks" on macOS unless that feature is set (`tauri-utils-2.10.2/src/platform.rs:116-118`, `:180`). Without the pre-check a manager started through a symlinked path would replace itself on disk and quit without relaunching after the UI said "Krate Manager will restart." | FR-013 (SI-39) |
| PD-38 | **A harness plant runs no scenario.** With `--plant committed-key` or `--plant bind-any` the parent spawns exactly one child, `good` with that flag, and no other process; the child refuses at its first key check or first bind, before any fixture server, relay or mock app exists; the parent prints that child's first stdout line and exits 1 (any other line, or exit 0, is `not ok plant <name>: <line>`, exit 1, which the CI check reads as red) | FR-081 (f) / SC-007 say the plant exits "before any scenario runs" (`spec.md:747-748`); revision 3 started all 24 other scenarios and only held back their lines, which the stdout check could not tell apart | FR-081 (f), SC-007 |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before tasks)

- **SI-1 (blocking).** FR-013: "a total `match` with no wildcard arm, so a new variant … fails to compile" cannot be
  built: `tauri_plugin_updater::Error` is `#[non_exhaustive]` (`src/error.rs:9-11` at tag `updater-v2.14.1`), and Rust
  refuses a non-exhaustive match on it outside the crate (E0004). Fix: PD-3's explicit arms plus one `_` arm, and the
  drift protection by PD-2's exact pin `=2.14.1`, which `config_guard` pins (FR-003 gains "(f) the workspace entry is
  exactly `version = \"=2.14.1\"`"). The variant count is 32 without `zip`, as the spec says (33 with it on Windows:
  `Extract` is `#[cfg(all(target_os = "windows", feature = "zip"))]`, `error.rs:64-67`; revision 1 of this plan
  miscounted 30). FR-013 also gains "the table is pinned by the harness line `map-errors` (FR-080, SI-23)".
- **SI-2 (blocking).** Phase 8's guard refuses any locked package named `tar` (`injection.rs:3192-3210`, over
  `Cargo.lock` and the clippy-guard lock, `:3128-3135`). `tauri-plugin-updater` depends on `tar = "0.4"` and
  `flate2 = "1"` **unconditionally on macOS** (its `Cargo.toml`, `[target."cfg(target_os = \"macos\")".dependencies]`), and
  Cargo locks every target's dependencies, so the lock gains `tar` whatever the features. FR-094 must widen the rule: a
  locked `tar` is allowed iff every lock path from a workspace member to it passes through `tauri-plugin-updater`; no
  workspace manifest names `tar` (unchanged); plants: `tar` under core's `[dependencies]`; a lock where
  `krate-manager-core` reaches `tar`.
- **SI-3 (major).** D1: "`zip` is required: it is what brings `tar` + `flate2`, without which the macOS `.app.tar.gz` cannot
  be unpacked" is false: macOS gets both without it (SI-2). `zip` adds only the `zip` crate for Windows `.zip` updater
  bundles (`updater.rs:1121-1160`) and gzip-compressed AppImages (`:1227-1235`), the v1 formats; `createUpdaterArtifacts:
  true` writes the raw setup `.exe`, the raw `.AppImage` and the `.app.tar.gz` (D6's own table). PD-2 drops it.
- **SI-4 (blocking, implementation).** FR-011's `api.github.com` and FR-011a's `krateaudio.com` written as literals in
  `crates/core/src/self_update.rs` fail `injection.rs:23-31` / `:1418-1422`, `roster.rs:11` and the binary scan
  `manager-ci.yml:358-359`. No spec text changes if PD-4 is adopted; FR-094 states that the three scans stay unchanged
  and gain no exemption.
- **SI-5.** FR-027 "the core's fixture server gains a scripted route that … stalls": it exists,
  `Reply::HeadersThenStall { declared_len, sent, stall }` (`fixture_server.rs:39-44`, Phase 7 SI-22). Drop the
  `fixture_server.rs (🔶)` half of the New components row.
- **SI-6.** FR-014 / FR-024: add `os` to `ManagerUpdateView` (PD-18).
- **SI-7.** FR-081 (a): the harness cannot call `Roots::new`: `clippy.toml:3` disallows it (and `std::env::var`,
  `temp_dir`, `:4-9`) for every crate under `apps/krate-manager/`, the harness included, as the clippy-guard step counts
  (`manager-ci.yml:118-136`). Reword: "built from its temp dir with `krate_manager_core::test_support::temp_roots`"
  (`test_support.rs:206`), which panics outside the temp dir; temp dirs come from `tempfile::TempDir::new()`.
- **SI-8.** FR-081 (b): reword "Before any thread starts, it sets …" to "every scenario process is started with …"
  (PD-11).
- **SI-9.** FR-080's fallback sentence becomes the chosen form (PD-11): "the harness runs one child process per scenario
  (`--scenario <name>`), all started at once".
- **SI-10.** FR-056 / SC-022 on Linux: no helper is staged on Linux (`bundle.js:89`, Phase 10 D18), so the Linux build
  leg asserts `src-tauri/binaries/` holds no `krate-manager-helper-*` and `cargo tree` lists no `ci-temp-roots`, as
  `manager-ci.yml:306-313` does.
- **SI-11.** FR-094 "the workspace members and `exclude` gain `tests/fixtures/updater-refusals`": `exclude` only (a
  member would link `tauri` into `cargo test --workspace`, D12a). The workspace `exclude` (`Cargo.toml:3`) and
  `apps/krate-manager/.gitignore` (`/tests/fixtures/updater-refusals/target/`, beside the clippy-guard line) gain it.
- **SI-12.** SC-004 and SC-012 name `config_guard::tests::updater_config_pinned`; integration tests in
  `crates/core/tests/*.rs` print bare names (`required-tests.json` holds `real_src_tauri_config_passes_the_guard`, not a
  path). The required names are bare: `updater_config_pinned`, `planted_updater_config_fails_the_guard`, and so on (§4.13).
- **SI-13.** FR-072: name `SecurityAgent` (PD-13).
- **SI-14.** FR-076: the per-user install is `$LOCALAPPDATA\Krate Manager\krate-manager.exe` (NSIS template
  `installer.nsi:546-547` at `tauri-cli-v2.12.4`; `MAINBINARYNAME` `:53` is the cargo bin `krate-manager`,
  `src-tauri/Cargo.toml` `[[bin]]`).
- **SI-15.** FR-018 "the code joins the Phase 9/10 install error table": it joins two tables, `InstallError` and
  `DownloadError` (PD-7). FR-094's "produced only by FR-018's action gate" reads "the code literal occurs only in
  `core::self_update`, and `SelfUpdating` is constructed only in `action_gate`".
- **SI-16.** SC-002: "the decision the core's own client records" — the core records none. Reword: "equals the route the
  core's own `http::get` takes for the same URL, observed by which fixture server (the proxy or the target) receives
  the request".
- **SI-17.** FR-030: add `--features updater-e2e` to `bundle.js --release`'s accepted arguments (PD-20); SC-008 gains the
  row.
- **SI-18.** FR-040 / FR-071: add `--only <os>` (PD-21) beside `--repo-url-base`; the shape test confines both to
  `updater-e2e`.
- **SI-19 (superseded by SI-20).** Revision 1 made one dry-run throwaway key in `preflight` and shipped the private key
  and its password to the build legs as an artifact. Withdrawn: a workflow artifact needs only read access to the
  repository to download ("Read access to the repository is required to perform these steps", GitHub Docs,
  https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts, read 2026-11-01), which
  for this public repository is anyone, and the artifact held the key and its password together, so the key's
  encryption protected nothing. What remains of it: FR-040 gains `--platform-subset <os>` (only with
  `--check-only`), used by each build leg against its own throwaway key (§4.9).
- **SI-20 (blocking, safety).** D14 / FR-052 / FR-053 / FR-055 / FR-057 / FR-059 / FR-091 / SC-019, and keys README
  §2: the real key moves out of the build (PD-23). Rewordings: D14 "Every job that touches the real key carries
  `environment: signing`" stays, but the jobs are `main-only`, `sign` and `publish`; `build` carries no environment and
  every leg, dry or real, bundles with a throwaway key made inside its bundler step (no `--config` pubkey:
  `KRATE_BUNDLE_EXTRA_CONFIG` is empty on every build leg); new jobs `main-only` (gate), `sign` (real run) and `sign-dry`
  (dry run) as §4.10. FR-052: "the only `env` carrying the two secrets is the `sign` job's `signing key` step". FR-053:
  each leg verifies its `.sig`s against **its own** throwaway public key, and the `sign` / `sign-dry` job verifies the
  release `.sig`s against the committed / its throwaway key. FR-055: "The real key reaches only the `sign` job's
  `signing key` step env, which writes it decrypted under `$RUNNER_TEMP` for the next step only, `rsign sign`, after
  which the following step deletes it; that job checks out no app source, runs no `npm` other than `npm ci --prefix
  tools/feed-contract --ignore-scripts` and no `cargo` other than an uncached `cargo install rsign2 --version 0.6.7
  --locked` (no `actions/cache` or `Swatinem/rust-cache` step in the job, PD-35), both before the key step, and no
  `bundle.js` / `tauri`". FR-057: `publish` `needs` gains `sign` and uploads
  the `sign` job's `.sig`s. FR-059: `dry-publish` `needs` gains `sign-dry` and uses its public key. FR-091's shape
  test: "`environment: signing` exactly on `main-only`, `sign` and `publish`"; "every `secrets.TAURI_SIGNING_*`
  reference sits in the env of the `sign` job's `signing key` step"; "no step of `sign` / `sign-dry` runs `npm` (but the
  one `npm ci --prefix tools/feed-contract --ignore-scripts`), `cargo` (but the `rsign2` install), `bundle.js`, `tauri`
  or `npx`, and none of those follows the key step"; "neither job uses `actions/cache` or `Swatinem/rust-cache`" (PD-35);
  "the step after `sign updater artifacts` deletes the key"; "the two
  jobs' step lists are equal but for the key step"; "`main-only` runs only `echo`"; plants: a secret added to a `build`
  step; an `npm ci` added after the key step; the delete step removed. Keys README §2 (`README.md:47-48`) becomes
  "Phase 11's `sign` job (the manager's own release) decrypts the same key with `tools/minisign.js decrypt-key` and
  signs manager updates with `rsign`, as `release.yml` does". SC-019's last sentence reads "every `.sig` in
  `manager-release-dry-run` verifies against `sign-dry`'s throwaway public key and does not verify against
  `krate-signing.pub` (`E_MINISIG_KEY_ID`)".
- **SI-21 (blocking, safety).** D6 / FR-013 / FR-041 / FR-072 / Edge cases: a `.deb` (or `.rpm`) install never
  self-updates (PD-24). D6: the `.deb` row loses its `.sig` and its `latest.json` key (seven keys: `windows-x86_64`,
  `windows-x86_64-nsis`, `darwin-aarch64`, `darwin-x86_64`, `darwin-universal`, `linux-x86_64`,
  `linux-x86_64-appimage`); FR-041 / SC-009 "D6's eight keys" becomes seven. FR-013: `E_SELF_UPDATE_UNSUPPORTED` also
  covers "installed by the system package manager", and its sentence becomes "Krate Manager cannot update itself on this
  computer. Install the new version from its release page." FR-072 Linux `.deb` case: "old" installed with `sudo apt-get
  install -y ./…deb`, launched under `xvfb-run`; asserts `<marker>/error-E_SELF_UPDATE_UNSUPPORTED` within 300 s, no
  `launched-<new>`, `dpkg-query -W -f '${Version}' krate-manager` still "old", and zero package GETs in the server log.
  Edge-case table gains "Manager installed from the `.deb` → banner shown with the new version, its notes and a
  "Download the new .deb" button that opens the release page (no "Update and restart"); a direct
  `manager_update_install` is refused `E_SELF_UPDATE_UNSUPPORTED` before any download; the user installs the new
  `.deb`" (ruling P-3; the UI half is SI-31). FR-080 gains scenario `deb-install` (SI-23).
- **SI-22.** FR-024 / FR-020: `manager_update_install` takes one `tauri::ipc::Channel<SelfUpdateProgress>` (PD-25) besides
  managed state and the `AppHandle`; SC-003 gains vitest `shows downloading then installing progress`.
- **SI-23.** FR-080 / FR-082 / FR-084 / SC-007: the scenario table gains four rows here and a fifth by SI-30
  (`socks-proxy`), so the line count becomes **25** (24 scenarios plus `confinement`; revision 4: 27 by SI-35) and the summary `harness: 25 ok, 0
  not ok`:
  `endpoint-latest-path` (the mock config's endpoint is `http://127.0.0.1:<port>/o/r/releases/latest/download/latest.json`:
  `check` → `E_SELF_UPDATE_URL { LatestPath }`, zero requests on the fixture server and the trap proxy);
  `redirect-loop` (the package route answers 302 to itself: `download` → `E_SELF_UPDATE_URL { Policy }` after exactly
  `MAX_REDIRECTS + 1` = 6 requests to that path); `deb-install` (`UpdateInputs.packaging = SystemPackage`: install →
  `E_SELF_UPDATE_UNSUPPORTED`, zero package requests); `map-errors` (§4.7; no server). Also: `busy` gains a second
  install after the guard is released, with no new check, which reaches the refusing stub (`E_SELF_UPDATE_INSTALL`)
  after exactly one package request, so `busy` uses the refusing stub too (FR-080's "every scenario but …" list gains
  `busy`); `busy-after-download` and `stall` assert, from the watcher thread while the package route is being served,
  `self_update::in_flight(state_dir)` and `action_gate(state_dir) == Err(SelfUpdating)` for the `state_dir` passed to
  `install`; `good` asserts `Update.timeout == Some(DOWNLOAD_TIMEOUT)` after `prepare_download` with
  `UpdateTimeouts::production()`; `failed-install-clears` asserts the progress events `[Downloading, Installing]`,
  `busy-after-download` `[Downloading]`. FR-081 (d): the committed-key refusal runs in every scenario child after key
  generation and before any server starts, and `--plant committed-key` makes the `good` child adopt the committed public
  key through the same key-producing function (§4.7).
- **SI-24.** SC-024 / SC-025: `timeouts` pins `UpdateTimeouts::production()` and the three constants in core;
  "the production builder passes them and sets no retry" is held by the harness `good` assertion and three `injection.rs`
  rules on `src-tauri/src/self_update.rs` (`build_updater` holds `.timeout(inputs.timeouts.check)` and
  `.read_timeout(inputs.timeouts.stall)`; `prepare_download` holds `update.timeout = Some(inputs.timeouts.download)`; no
  `retry` token; a plant each). `no_context_defaults` tests `self_update_http(None, &fallback) == fallback`, and that
  `fallback` is built as `lib.rs:64-67` builds it, plus the `lib.rs` pin of PD-9 (PD-29).
- **SI-25.** FR-070: "Code behind it lives in `src-tauri/src/self_update.rs` inside one `#[cfg(feature =
  "updater-e2e")]` module, which contains the marker string `krate-e2e-marker-v1` and uses it at run time (FR-073), plus
  exactly two single-statement `#[cfg(feature = "updater-e2e")]` call sites in `src-tauri/src/lib.rs` (the feed / HTTP
  override and the `setup` call) that only call into that module; pinned by a per-file count (FR-094)". FR-073 gains:
  the `updater-e2e` job asserts the marker occurs at least once in its own e2e `krate-manager` binary (step `marker
  present`), so the release legs' zero count is known to be meaningful.
- **SI-26.** FR-012: `proxy_choice(http, url) -> Result<ProxyChoice, SelfUpdateError>` (PD-6); SC-002 gains the row "a
  static proxy the core refuses (scheme `ftp`) → `E_SELF_UPDATE_NETWORK`, zero requests on the target and the proxy".
- **SI-27.** FR-011 / FR-011a / SC-001: the package URL's origin rule (PD-27) and the pure `hop_kind` /
  `redirect_decision` (PD-5); SC-001 gains rows `https://example.org/pkg` → `Host`,
  `https://github.com/o/r/archive/x` → `Host`, `https://github.com/o/r/releases/download/manager/v0.2.0/x` → `Ok` (revision 4, SI-40: `o/r` becomes `MANAGER_REPO`, and `o/r` itself is now `Host`), the
  sixth hop → `Policy`, and both `hop_kind` outcomes.
- **SI-28.** FR-001 / FR-094: FR-001's dependency list gains the direct `src-tauri` line `reqwest = { version =
  "=0.13.5", default-features = false, features = ["socks"] }` (the updater does not re-export reqwest and
  `redirect::Policy` and `ClientBuilder::read_timeout` must be named, §4.3; `socks` by ruling P-1, PD-30); FR-094's confinements gain "`reqwest` only in
  `src-tauri/Cargo.toml` and `src-tauri/src/self_update.rs`, and `config_guard` pins that line exactly"; "`.install(` on
  an `Update`, `download_and_install`, `restart(`, `process::restart` and `NativeUpdateInstaller` in the workspace **and
  the harness**: only inside `impl … for NativeUpdateInstaller` (the harness names none of them)"; the harness source
  names no `dirs::` outside `src/confinement.rs`, no `app.path()` and no `BaseDirectory`; the per-file `updater-e2e`
  count (SI-25); FR-022's "in the workspace" reads "in the workspace and the harness".
- **SI-29.** FR-081 (b) / (e): record the Windows limit (PD-11) and add "(e′) on Windows the parent snapshots, read-only,
  the entries of `<FOLDERID_LocalAppData>\com.krateaudio.krate-manager` and `<FOLDERID_RoamingAppData>\com.krateaudio.krate-manager`
  (`dirs::data_local_dir()` / `dirs::data_dir()`, dirs 7.0.0 `src/lib.rs:122`, `:134`) before spawning and after the
  last child exits; any new entry is `not ok confinement <path>`".
- **SI-30 (ruling P-1).** FR-001: the `src-tauri` `reqwest` line carries `features = ["socks"]` (SI-28), "unified into
  the updater's client; it adds no crate (reqwest 0.13.5 `socks = []`)". D17 / FR-022: "the updater's client takes every
  choice `route_choice` returns: direct, an `http` / `https` proxy, or a `socks4` / `socks5` proxy". FR-080's table gains
  `socks-proxy`: the good path through the harness's own SOCKS relay (`src/socks.rs`), once with `socks5://` and once
  with `socks4://`; `check` → `Some`, `prepare_download` sets `Update.proxy` to the relay URL and `no_proxy` false,
  `download` + `verify` → Ok; the relay records a CONNECT of its version byte (5, then 4) to the fixture server's address
  for each fetch and nothing else; the fixture server records `GET /latest.json` and the package GET once per run; the
  trap proxy records nothing; the panicking installer is never reached. FR-081 (c): the relay binds loopback through the
  same check as the fixture servers and relays only to `127.0.0.1`. FR-082 / SC-007: 25 lines, `harness: 25 ok, 0 not ok` (revision 4: 27 by SI-35).
- **SI-31 (ruling P-3).** FR-014: `AvailableView` gains `action: UpdateAction` (`InstallAndRestart` |
  `OpenReleasePage { url }`, PD-31), chosen by `update_action(packaging, policy, download_url, announced)` (SI-40). FR-024: "when
  `available.action` is `open_release_page` the line shows the version and notes and one button "Download the new .deb",
  which calls `open_release_page` with the view's `url` (no confirmation, no progress, plugin actions stay enabled; an
  open failure shows the existing "Could not open the release page: {text}" line); "Update and restart" is shown only
  for `install_and_restart`". FR-013: `E_SELF_UPDATE_UNSUPPORTED` stays the install backstop. SC-003 gains the core rows
  of §7.1 (`packaging_rule`, `view_from_update`) and vitest `offers the new .deb instead of an update on a system-package
  install`, `a failed .deb release-page open shows its code`. FR-080 `deb-install` also asserts the view's action.
- **SI-32 (ruling P-4).** FR-073 / FR-083: "`manager-ci.yml` `check` runs `clippy updater-e2e` on every leg (PD-32:
  dummy `KRATE_E2E_*` values, compiled and linted, never run)"; FR-091's manager-ci shape test pins it in `STEPS`,
  `ASSERTION_STEPS` and `COMMANDS`, and confines `--features updater-e2e` and `KRATE_E2E_*` in `check` to that step.
- **SI-33 (ruling P-5).** D9's fallback paragraph (`spec.md:266-270`), the D9 clarification (`spec.md:1134`), FR-084's
  last sentence and SC-007's title: "if the first local run shows the bin fails to load on Windows with the manifest
  embedded, the build goes on: the measurement (exit code, loader message) is recorded in
  `evidence/d9-windows.txt`; the harness then runs on macOS and Linux only, locally in WSL and on `check
  (macos-latest)` / `check (ubuntu-24.04)`; it is still built and linted on Windows; SC-007's Windows row is a recorded
  gap quoting the load error, never a pass".
- **SI-34 (revision 4, PD-35).** FR-071 / FR-072 / SC-015: "the `updater-e2e` job signs the "new" updater asset with
  the release signing steps (`install rsign2`, `throwaway signing key`, `updater asset list`, `sign updater artifacts`,
  `delete signing key (after signing)`, byte-equal to `sign-dry`'s), the "old" build trusts that rsign key, and its
  `latest.json` is built from those `.sig`s; the Tauri CLI's own `.sig`s of the e2e builds are overwritten and never
  served". FR-091's shape test gains the equality and its plant. FR-080's table gains `rsign-format`: the good path with
  the committed rsign fixture of §4.7 (`check` → `Some`, `download` + `verify` → Ok, the fixture key refused if it
  equals the committed key); FR-082 / SC-007 count by SI-35.
- **SI-35 (revision 4, PD-36).** FR-080 gains `production-inputs` (no server, no mock app: `UpdateInputs::production(
  HttpConfig::production())` has `policy == HttpsOnly` and `timeouts == UpdateTimeouts::production()`); with
  `rsign-format` (SI-34) the line count becomes **27** (26 scenarios plus `confinement`) and the summary `harness: 27 ok,
  0 not ok` (FR-082, FR-084, SC-007; supersedes SI-23's and SI-30's 25). FR-094 gains the PD-36 pins: the body of
  `UpdateInputs::production`; `commands.rs` builds inputs only through `UpdateInputs::production(`;
  `UrlPolicy::LoopbackOnly` in `src-tauri/src/self_update.rs` only inside `mod e2e`; SC-024 gains those rows.
- **SI-36 (revision 4).** FR-023: "The kept update is replaced by every check, and cleared after every install failure
  that took the update (FR-019's busy refusal and every later error); an FR-015 refusal, made before the update is taken,
  keeps it (PD-28; edge case "retry when it finishes"); an install never uses an update older than the last check."
  FR-080's `busy` row gains SI-23's second-install assertion (the guard released, no new check, the refusing stub reached
  after exactly one package request).
- **SI-37 (revision 4).** SC-021: "shows the app not relocatable: `xmllint --xpath 'count(//relocate/*)' PackageInfo`
  prints `0` (no `relocate` element, or an empty one; a `<relocate>` with any child is red), and the expanded
  `PackageInfo` of the first dry run is recorded as `specs/krate-manager-phase11/evidence/packageinfo-dry-run.xml`, the
  fixture that names the element". The cited sources' semantics: a populated `<relocate>` makes Installer search for
  the bundle, an empty or absent one limits the install to the install location (§1.2).
- **SI-38 (revision 4).** FR-090's write set for `updater-e2e` and `first-install` gains "the runner user's per-user
  directories of the app identifier `com.krateaudio.krate-manager`: the manager's state dir (`Roots::from_os`: macOS
  `~/Library/Application Support/com.krateaudio.krate-manager`, `platform/macos.rs:34-35`; Linux
  `$XDG_DATA_HOME/com.krateaudio.krate-manager` or `~/.local/share/com.krateaudio.krate-manager`, `platform/linux.rs:31-37`;
  Windows under `%LOCALAPPDATA%`, `platform/windows.rs:37-41`, already listed) and the per-user data and caches the OS
  webview keeps for that app; all on the ephemeral VM". Chosen over redirecting `HOME` / `XDG_DATA_HOME` for the
  launches: the macOS cases launch through `open -n` (LaunchServices starts the app, not the job's shell), so a redirect
  would cover only the Linux and direct-exec cases.
- **SI-39 (revision 4, PD-37).** FR-013: `E_SELF_UPDATE_UNSUPPORTED` also covers "on macOS, the running executable's
  path cannot be resolved without a symlink, so the manager could not relaunch itself"; refused before any download.
- **SI-40 (revision 4, PD-27).** FR-011 / FR-011a: the package URL must be `.../<MANAGER_REPO>/releases/download/manager/
  v<announced>/<name>` (`MANAGER_REPO` a `core::self_update` constant, the tag equal to `"v" + Update.version`) under
  both policies, host `github.com` under `HttpsOnly`; FR-014 / PD-31: the `.deb` release page is
  `https://github.com/<MANAGER_REPO>/releases/tag/manager/v<announced>`. SC-001 / SC-003 gain the rows of §7.1 (foreign
  `owner/repo` → `Url { Host }`; tag version ≠ announced → `Url { Host }`; the correct URL → `Ok` / the page). FR-051
  gains `E_PREFLIGHT_REPO` (`--repo` ≠ the `MANAGER_REPO` line of `crates/core/src/self_update.rs`, both kinds of run),
  checked before `E_PREFLIGHT_TAG_EXISTS`; SC-011 gains its row.

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-11-01 at `f6022bd5`)

| Fact | Where |
|---|---|
| `FEED_URL = "https://krateaudio.com/versions.json"`; `Roots::from_os()` once under `#[expect(clippy::disallowed_methods)]`; proxy resolved once; `HttpConfig::production().with_proxy(..).with_pac(..).with_pac_host(..)`; `assemble_context(roots, &feed_url, http, SigningKey::release())`; `tauri::Builder::default().plugin(navigation::guard()).manage(SharedState).manage(Arc<NativeElevation>).setup(..)`, eight commands | `src-tauri/src/lib.rs:29`, `:41-45`, `:57-62`, `:63-67`, `:70`, `:87-114` |
| Commands clone `ctx` out of the read guard before any await; `download_plugin` returns `DownloadError` text; `install_plugin` / `remove_plugin` take `State<Arc<NativeElevation>>` | `src-tauri/src/commands.rs:38-51`, `:64-103`, `:108-163`, `:168-219` |
| `app_version` returns `env!("CARGO_PKG_VERSION")` | `commands.rs:18-21` |
| `AppContext { roots, settings, feed_url, http, signing_key }` (`Clone, Debug, PartialEq`), no lock; `AppState { context, settings_error, lock: Option<Arc<StateLock>> }`; `failed()` keeps no `http`; the lock's only clone is `reassemble_with_os`; `apply_settings`; `startup_notice` = `Locked` only | `crates/core/src/context.rs:52-62`, `:64-70`, `:134-140`, `:165`, `:198-204`, `:206-209` |
| `StateLock` has no `Drop`; closing the file releases; `acquire` → `Held` when already held, by this or another process | `crates/core/src/state_lock.rs:38-43`, `:49-71` |
| `Elevation: Sync { helper, launch, guard }`; `Hold::{Free, Running, Stale}`; `ElevationGuard::enter` (private); `elevate` enters the guard at `:1299` | `crates/core/src/install.rs:587-591`, `:593-603`, `:617-639`, `:1291-1299` |
| `CommandEnv { os, arch, ctx, io, extract, download, drivers, elevation, host_io, … }` | `install.rs:529-541` |
| `BUSY: Mutex<BTreeSet<(PathBuf, String)>>` keyed by `(state_dir, stem)`; `Busy::enter`; `for_command` / `install_command` / `remove_for_command` / `command_remove` | `install.rs:3841-3868`, `:3871-3887`, `:3941`, `:4072-4085`, `:4119` |
| `IN_FLIGHT: Mutex<BTreeSet<(PathBuf, [u8; 32])>>` keyed by the downloads dir; `InFlight::enter` in `obtain_body`; `download::for_command` | `crates/core/src/download.rs:647-672`, `:1369-1375`, `:1603-1613` |
| `UrlPolicy::{HttpsOnly, LoopbackOnly}`; `HttpConfig { policy, connect_timeout, total_timeout, proxy, pac, pac_host }`; `production()`, `loopback(..)`; `MAX_REDIRECTS = 5`; `check_url` refuses userinfo, non-https (production), non-loopback (`LoopbackOnly`) | `crates/core/src/http.rs:22-24`, `:28-31`, `:35-42`, `:46-67`, `:276-300` |
| The route decision: `PacState`, `open_pac`, `static_route`, `pac_route`, `get`'s route closure, `prepare`, the hop loop with `next_hop` | `http.rs:717-732`, `:749-779`, `:861-868`, `:904-946`, `:948-966`, `:968-991`, `:1029-1060` |
| `ProxyChoice::{Direct, Proxy(ProxyConfig)}` | `crates/core/src/pac.rs:141-144` |
| `Reply::HeadersThenStall`, `Reply::Redirect`, `Reply::Script`; `FixtureServer::{start, url, port, set_route, requests}` | `crates/core/src/fixture_server.rs:17-62`, `:144-195` |
| `fixture_server` and `test_support` under `cfg(any(test, feature = "test-support"))`; `test_signer` `cfg(test)` only | `crates/core/src/lib.rs:9-10`, `:27-30` |
| `temp_roots(base)` (panics outside the temp dir); `FakeElevation` implements `Elevation` | `crates/core/src/test_support.rs:206-208`, `:417`, `:486` |
| `RELEASE_KEY_TEXT = include_str!("../../../../../tools/feed-contract/keys/krate-signing.pub")`; `SigningKey::release()` | `crates/core/src/verify.rs:15-16`, `:50-53` |
| `notes::blocks(markdown) -> Vec<Block>` | `crates/core/src/notes.rs:52` |
| `krate-signing.pub`: two lines, LF, ends in LF (`xxd`, 0x71 bytes); `git ls-files --eol` → `i/lf w/lf attr/`; standard base64 of its bytes = `dW50cnVzdGVkIGNvbW1lbnQ6IG1pbmlzaWduIHB1YmxpYyBrZXk6IDlGRUIzNkY5MTE0MjczQkMKUldTOGMwSVIrVGJybi9pWUNqR2RudFF0N3ZuQm9LcE43VVZPdmhIVWVNa1FpN0t5REdBMkpxeUoK` (`base64 -w0`) | `tools/feed-contract/keys/krate-signing.pub` |
| `.gitattributes`: only `tools/feed-contract/fixtures/** -text` | `.gitattributes:24-26` |
| Workspace: `exclude = [clippy-guard, argv-recorder, locked-dll]`; `tauri = { version = "2.12.5", features = [] }`; `[workspace.lints.clippy] allow_attributes = "deny"` | `apps/krate-manager/Cargo.toml:3`, `:40`, `:52-54` |
| `src-tauri` `[lib] test = false, doctest = false`; `[[bin]] name = "krate-manager"`; `[features] dev-feed = [], manual-elevation = []` | `src-tauri/Cargo.toml:10-19`, `:44-47` |
| `clippy.toml` disallows `FromOs::from_os`, `Roots::new`, `std::env::{var, var_os, vars, vars_os, home_dir, temp_dir}` | `apps/krate-manager/clippy.toml:1-10` |
| `tauri.conf.json`: CSP (`:16`), `macOS { signingIdentity "-", minimumSystemVersion "15.0" }` (`:22`), NSIS `currentUser` (`:23`), no `plugins`; `tauri.linux.conf.json` = `{"bundle":{"targets":["deb"]}}` | `src-tauri/tauri.conf.json:1-25`; `src-tauri/tauri.linux.conf.json:1` |
| `build.rs` lists eight commands; capability lists eight `allow-*` | `src-tauri/build.rs:1-15`; `src-tauri/capabilities/main.json:1-14` |
| `bundle.js`: refuses any argument (exit 2); helper built only off Linux; `CARGO_TARGET_DIR` honoured; `tauri.js` run with this Node | `apps/krate-manager/scripts/bundle.js:84-103` |
| `.gitignore` (app): `/target/`, `/dist/`, `/tests/fixtures/clippy-guard/target/`, `/src-tauri/binaries/` | `apps/krate-manager/.gitignore:1-6` |
| UI: `renderShell` returns after the notice when `startup_error` is non-null (Locked), else `app_version`, then `mountStatus`; `mountStatus` awaits `plugin_status` then enables "Check now"; `checkNow` | `src/main.ts:4-25`; `src/status.ts:507-571`, `:574-587` |
| Guards: `FORBIDDEN_OUTSIDE_PLATFORM` (`krateaudio.com`, `api.github.com`, raw text); core scan skips `test_support.rs` and `*_tests.rs`; `ENV_READS` only in `os_roots.rs`, `proxy_env.rs`; `locks()`; the `tar` rule; `workspace_rs_files` = `crates/*/{src,tests}` + `src-tauri/src`; `manifests()` includes clippy-guard | `crates/core/tests/injection.rs:23-31`, `:606-614`, `:1386-1390`, `:1418-1422`, `:1612-1629`, `:1643-1660`, `:3128-3135`, `:3192-3210` |
| Roster: `HOST = "api.github.com"`; scans core, helper, `src-tauri/src`, capabilities, `src/`, `src-tauri/tauri*.conf.json`, `index.html`; skips `*_tests.rs`, `test_support.rs`, `*.test.ts` | `crates/core/tests/roster.rs:11`, `:39-41`, `:101-125` |
| `config_guard`: `SIGNING_CRATES = [minisign, rpassword, scrypt]` walked over every member's normal graph; `src-tauri/tests/` must not exist; `[lib]`/`[[bin]]` `test = false`; `[features]` pinned to `["dev-feed = []", "manual-elevation = []"]` | `crates/core/tests/config_guard.rs:174`, `:826-828`, `:885-902`, `:1586-1675` |
| `required-tests.json`: `{cargo: {all, windows, macos, linux}, vitest, node}`; integration names are bare (`real_src_tauri_config_passes_the_guard`) | `apps/krate-manager/tests/required-tests.json` |
| `manager-ci.yml`: `check` on three legs, `defaults.run.working-directory: apps/krate-manager`; Ubuntu apt line; clippy-guard step (`CARGO_TARGET_DIR="$PWD/target"`); `cargo test`; required tests ran; `tauri build` = `npm run bundle`; build warnings; `ci-temp-roots absent` (Linux branch); `roster absent`; artifact version; macOS plist / codesign steps; `Checkout unchanged` | `.github/workflows/manager-ci.yml:31-41`, `:49-54`, `:118-136`, `:138-141`, `:195-226`, `:228-246`, `:300-330`, `:344-366`, `:368-389`, `:391-429`, `:431-441` |
| `manager-ci-shape.test.js`: `RELEASE_MANAGER` path; `STEPS` in order; `ASSERTION_STEPS`; `EXPECTED_REQUIRED`; `checkManagerCi` refuses `dev-feed` in `release-manager.yml` | `tools/feed-contract/tests/manager-ci-shape.test.js:24-25`, `:167-201`, `:210-227`, `:600`, `:1270-1283` |
| `feed-tools.yml`: path filter; `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"`; `PLAN = "1..59"`; `NAMES` | `.github/workflows/feed-tools.yml:3-40`, `:79-81`, `:85`, `:86-122` |
| 59 top-level `test(` calls across the 23 test files (one per counted call; `grep -c`) | `tools/feed-contract/tests/*.test.js` |
| `hygiene.test.js`: `scannedFiles`; offline scan completeness list; `PHASE2_TOOLS`; the form test needs every code in `CODES` | `tools/feed-contract/tests/hygiene.test.js:34-53`, `:114-135`, `:534`, `:575-603` |
| `feed-publish.js`: `MANAGER_TAG_RE`, `MANAGER_LATEST_URL`; `checkLatestJson(bytes, tag, version)`; `collectManager(deps, counter, repo, rows, outDir)`; exports (no `checkLatestJson`) | `tools/feed-publish.js:63-64`, `:333-361`, `:376-393`, `:1029-1043` |
| `contract.checkUrl`: https, no whitespace, no `/releases/latest/`; accepts `api.github.com` | `tools/feed-contract/contract.js:210-219` |
| `refuseTestKey(publicKeyText, testPublicKeyTexts)` → `E_SIGNING_CONFIG`; exported | `tools/gen-release-manifest.js:373-394`, `:545-551` |
| `minisign.js`: only the prehashed `ED` form; `verifyBuffer(data, minisigText, publicKeyText) -> {trustedComment, keyId}`; exports `generateKeyPair, formatPublicKey, parsePublicKey, signBuffer, verifyBuffer, …` | `tools/minisign.js:10-12`, `:181-208`, `:585-597` |
| `release-facts.js` exports `machoFacts(buffer)`, `glibcFloor(objdumpText)` | `tools/release-facts.js:134`, `:260-264`, `:452-460` |
| `lint-manager-version.js` exports `{ lint, main, parseTomlSubset }` | `tools/lint-manager-version.js:223`, `:372` |
| `feed.yml`: `workflow_call` with required boolean `dry_run`; `deploy` `if: !inputs.dry_run && refs/heads/main`, `contents: write`, `keep_files: true`, `cname: krateaudio.com` | `.github/workflows/feed.yml:8-14`, `:102-125` |
| `release.yml`: plugin `pkgbuild --root staging … --install-location /`; "Tag must not exist" (real runs, 1000-entry guard); `softprops/action-gh-release@v2` `make_latest: false` | `.github/workflows/release.yml:323-341`, `:578-588`, `:683-692` |
| `release.yml` signing: `npm ci --prefix tools/feed-contract`; `rsign2` 0.6.7 cached / installed / version-checked; `Signing configuration and key` (the only secret refs; `signing=throwaway` → `rsign generate -W`, `signing=on` → `node tools/minisign.js decrypt-key --out "$RUNNER_TEMP/krate.key"`); `Sign assets` (`rsign sign -W -s … -t "$comment" -x …`); `Delete signing key (after signing)` "PD-20: no later step runs while the key exists" | `.github/workflows/release.yml:607-655` |
| `minisign.js` requires `contract.js` (which loads `ajv`, `contract.js:13`); `decrypt-key` reads `KRATE_SIGNING_KEY` / `KRATE_SIGNING_KEY_PASSWORD`; key-id mismatch is `E_MINISIG_KEY_ID` | `tools/minisign.js:22-24`, `:35`, `:186`; `tools/feed-contract/package.json` (`ajv` 8.20.0 only) |
| Keys README §2: "Only jobs that reference the environment receive these secrets"; the Phase 11 sentence | `tools/feed-contract/keys/README.md:44`, `:47-48` |
| Keys README §1 (the committed file is the Tauri `.pub` decoded), §2 (environment `signing`, main only; the two secrets; "Phase 11 … passes the same password to Tauri") | `tools/feed-contract/keys/README.md:9-24`, `:26-62` |
| `CHANGELOG.md` `## [0.1.0] - Unreleased`; `version.json` `0.1.0` | `apps/krate-manager/CHANGELOG.md:8`; `apps/krate-manager/version.json` |
| `package.json`: `@tauri-apps/cli` 2.12.4, `@tauri-apps/api` 2.12.4, vitest 5.0.5, typescript 6.0.3, eslint 10.14.0; scripts `typecheck`, `lint`, `test`, `bundle` | `apps/krate-manager/package.json` |
| Probe launch method: `open -n`, 12 s, `pgrep -f "<app>/Contents/MacOS/<exe>"`, `pkill` | `specs/krate-manager/probes/macos-gatekeeper-probe.js:148-158` |
| Phase 4 D12a (no `tauri`-linked test binary) | `specs/krate-manager-phase4/spec.md:275-282` |
| *Revision 3 (read at `30878b36`):* `RELEASE_PAGE_HOST = "github.com"`, `MAX_RELEASE_PAGE_URL_BYTES = 2048`; `release_page_url(text)`: https, host `RELEASE_PAGE_HOST` (ASCII case-insensitive), no user name, no password, no port, the parser's serialisation | `crates/core/src/status.rs:19-21`, `:350-365` |
| `open_release_page(url: String)`: `status::release_page_url` or `E_OPEN_REJECTED`, then `tauri_plugin_opener::open_url` or `E_OPEN_FAILED: …`; already registered and allowed (`allow-open-release-page`) | `src-tauri/src/commands.rs:53-59`; `config_guard.rs:169` |
| UI: private `releaseButton(url, invoke)` with label `STATUS_TEXTS.openReleasePage` ("Open release page") and failure line `ACTION_TEXTS.openFailed` ("Could not open the release page: {text}") over `rejectionText` | `src/status.ts:49`, `:63`, `:171`, `:250-280` |
| Plugin feed release-page form `https://github.com/${repo}/releases/tag/${tag}` | `tools/gen-feed.js:475` |
| `PROXY_SCHEMES = ["http", "https", "socks4", "socks5"]` ("`socks4a` and `socks5h` are not among them"); `check_proxy`: scheme, host, loopback under `LoopbackOnly` (`is_loopback_host` reads a non-special scheme's `127.0.0.1` host as an address) | `crates/core/src/http.rs:253-266`, `:305-322` |
| Workspace `ureq = { version = "3.5.0", features = ["platform-verifier", "socks-proxy"] }` | `apps/krate-manager/Cargo.toml:47` |
| `FixtureServer`: HTTP only (scripted replies, `ConnectOk` / `ConnectRefused` for CONNECT); `start` binds `127.0.0.1:0`; "It opens no outbound socket" | `crates/core/src/fixture_server.rs:1-61`, `:144-155` |
| `manager-ci.yml` `check` clippy steps in order: `clippy` `:86-89`, `clippy dev-feed` `:91-98`, `clippy ci-temp-roots` `:100-107`, `clippy manual-elevation example` `:109-116` (`::notice …::clean (compiled and linted, never run)`), `clippy-guard` `:118-136`; Linux apt line `:54` | `.github/workflows/manager-ci.yml` |
| `manager-ci-shape.test.js`: `STEPS` `:167-201`, `ASSERTION_STEPS` `:211-229`, `COMMANDS` `:232-373`, `DEV_FEED_STEPS` `:398`, `CI_FEATURE` / `CI_FEATURE_STEPS` `:438-443` | `tools/feed-contract/tests/manager-ci-shape.test.js` |
| WSL, measured this session: Ubuntu 24.04.3 LTS, `cargo` at `~/.cargo/bin/cargo`; `pkg-config` finds none of `webkit2gtk-4.1`, `javascriptcoregtk-4.1`, `libsoup-3.0`; `dpkg -l` finds none of `libwebkit2gtk-4.1-dev`, `libxdo-dev`, `libayatana-appindicator3-dev`, `librsvg2-dev`. Phase 10's lane records the same ("WSL has no WebKitGTK, so src-tauri does not build there") | `wsl.exe -e sh -c 'pkg-config --modversion …; dpkg -l …'`; `specs/krate-manager-phase10/evidence/wsl-lane.js:11` |
| *Revision 4 (read at `30878b36`):* repository slug `rolandzwaga/krate-audio` (`git remote get-url origin` → `https://github.com/rolandzwaga/krate-audio.git`); `feed.yml` passes `--repo "$GITHUB_REPOSITORY"` to `feed-publish.js` (`:60`, `:74`), which holds only the shape `REPO_RE` (`feed-publish.js:66`, checked `:958-959`), no repo constant | `.github/workflows/feed.yml`; `tools/feed-publish.js` |
| `rsign2` cache: key `rsign2-0.6.7-${{ runner.os }}-${{ runner.arch }}`, path `~/.cargo/bin/rsign{,.exe}`, saved by `feed-tools.yml` after `npm ci --prefix tools/feed-contract` (no `--ignore-scripts`) in the same job; the same key in `backfill-sign.yml` and `release.yml` (cache, install on miss, `rsign --version` check) | `feed-tools.yml:62`, `:65-76`; `backfill-sign.yml:161-175`, `:243-257`; `release.yml:610-624` |
| `minisign.js` `signBuffer(data, sk, opts)` → the four-line `.minisig` text (`:171-173`, `signHash` `:156-169`); no `sign` CLI subcommand (the CLI is `verify` and `decrypt-key`, `:19-24`) | `tools/minisign.js` |
| The KAT fixture's trusted comment is `krate:1:KatPlugin:1.2.3:linux-x86_64:…` (no `version:` field), so it cannot pass the updater's `requireSignedVersion` check; `rsign2` 0.6.7 is at `~/.cargo/bin/rsign` on this box (KAT README "Signer") | `tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.minisig`; `tools/feed-contract/fixtures/kat/README.md` |
| State dirs: macOS `$HOME/Library/Application Support/<APP_IDENTIFIER>`; Linux `$XDG_DATA_HOME` (absolute) or `$HOME/.local/share`, plus `<APP_IDENTIFIER>`; Windows `%LOCALAPPDATA%\<APP_IDENTIFIER>` | `crates/core/src/platform/macos.rs:34-35`; `platform/linux.rs:31-37`; `platform/windows.rs:37-41` |
| `config_guard`: `src-tauri` `tauri = { workspace = true }` only, workspace `tauri` entry carries `features = []` | `crates/core/tests/config_guard.rs:931-960` |
| `manager-ci.yml` `build warnings`: ANSI stripped, `grep -c '^warning:'` (case-sensitive) must be 0 and a `Finished \`release\` profile` line present; no exemption | `.github/workflows/manager-ci.yml:246-259` |

### 1.2 External facts (verified 2026-11-01)

| Fact | Source |
|---|---|
| `tauri-plugin-updater` 2.14.1: features `default = ["rustls-tls", "system-proxy", "zip"]`, `zip = ["dep:zip", "dep:tar", "dep:flate2"]`, `rustls-tls = ["reqwest/rustls-no-provider", "dep:rustls"]`; reqwest `0.13` `default-features = false, features = ["json", "stream"]`; `minisign-verify 0.3`, `base64 0.23`, `tempfile 3.20`, `infer 0.22`, `tokio 1`, `semver 1`; Windows `windows-sys 0.61.0` (`Win32_Foundation`, `Win32_UI_WindowsAndMessaging`, `Win32_UI_Shell`), optional `zip 9`; Linux `dirs 7`, optional `tar`, `flate2`; **macOS `tar 0.4`, `flate2 1`, `osakit 0.3` (`full`) unconditional** | `plugins/updater/Cargo.toml` at tag `updater-v2.14.1` (https://github.com/tauri-apps/plugins-workspace/blob/updater-v2.14.1/plugins/updater/Cargo.toml, read with `gh api` 2026-11-01) |
| `Error` is `#[non_exhaustive]`; variants `EmptyEndpoints, Io, Semver, Serialization, ReleaseNotFound, UnsupportedArch, UnsupportedOs, FailedToDetermineExtractPath, UrlParse, Reqwest, TargetNotFound, TargetsNotFound, Network, Minisign, Base64, SignatureUtf8, Extract (cfg windows + zip), TempDirNotOnSameMountPoint, PreviousAppNotRestored, BinaryNotFoundInArchive, TempDirNotFound, AuthenticationFailed, DebInstallFailed, PackageInstallFailed, InvalidUpdaterFormat, Http, InvalidHeaderValue, InvalidHeaderName, FormatDate, InsecureTransportProtocol, SignedVersionMismatch { .. }, MissingSignedVersion, Tauri` | `src/error.rs:9-128`, same tag |
| `UpdaterBuilder::configure_client<F: Fn(ClientBuilder) -> ClientBuilder + Send + Sync + 'static>(self, f: F) -> Self` (stored in the context shared with `Update`); `timeout`, `proxy(Url)`, `no_proxy()`, `endpoints(Vec<Url>) -> Result<Self>`, `executable_path`, `build() -> Result<Updater>` (`EmptyEndpoints`, `UnsupportedArch`, `FailedToDetermineExtractPath`) | `src/updater.rs:151`, `:187-198`, `:330-347`, `:427-433`, `:444-479` |
| `check()`: client = user agent, `danger_*` flags, `timeout`, `no_proxy` / `proxy`, then `configure_client` (`:584-605`); a non-success status logs and sets no error, so it ends in `Err(ReleaseNotFound)` (`:640-661`); a parse error `Serialization`; `update_from_release`: `release.version > current` unless a comparator is set (`:712-720`), `TargetsNotFound` when no `{os}-{arch}-{installer}` / `{os}-{arch}` entry (`:743-775`); `Update.timeout` starts `None` (`:735`) | `src/updater.rs`, same tag |
| `Update` public fields `body, current_version, version, date, target, download_url, signature, raw_json, timeout, proxy, no_proxy, headers` (`:781-813`); `download`: non-https only warned (`:823-833`), `timeout` / `no_proxy` / `proxy` / `configure_client` (`:846-860`), non-success → `Network("Download request failed with status: …")` (`:868-872`), whole body in memory, then `verify` (`:891`); `verify` (`:901-909`); `install` = `verify` + `install_inner` (`:921-925`) | same |
| `verify_signature`: base64-decode pubkey and signature, `public_key.verify(data, &signature, true)`, then `verify_signed_version(trusted_comment, announced, require)` → `SignedVersionMismatch` / `MissingSignedVersion` | `src/updater.rs:1893-1960`, same tag |
| Windows `install_inner`: write the setup to temp, `on_before_exit`, `ShellExecuteW("open", …, SW_SHOW)`, then `std::process::exit(0)`; NSIS args = install-mode args, `/UPDATE`, restart args + `/ARGS` when `restart_after_install` | `src/updater.rs:1009-1050`, `:1052-1080` |
| Linux `install_inner`: `.deb` → `install_deb`, `.rpm`, else AppImage; AppImage: refuses a gzip payload without `zip`, refuses a non-ELF | `src/updater.rs:1213-1240` |
| macOS `install_inner`: temp dirs on the app's volume or `TempDirNotOnSameMountPoint`; un-tar; chmod 0755; `renamex_np(RENAME_SWAP)`, any error but `PermissionDenied` → two renames; `PermissionDenied` → `do shell script … with administrator privileges` through `osakit::Script` on the main thread; failure → `PreviousAppNotRestored` or `Io(PermissionDenied, "Failed to move the new app into place")`; the privileged command runs `/usr/bin/osascript -l JavaScript` for the swap | `src/updater.rs:1486-1636`, `:1688-1691` |
| `extract_path_from_executable`: the exe's dir; on macOS the `.app` for an exe in `Contents/MacOS` | `src/updater.rs:1793-1823` |
| Plugin: `UpdaterExt::updater_builder` pre-fills config, headers, Windows exe args, a downgrade comparator only when `allow_downgrades` (`src/lib.rs:79-125`, `:102-107`); `Builder::build` registers the JS commands `check, download, install, download_and_install` (`:268-298`); `validate_endpoints` refuses a non-https endpoint in a release build unless `dangerousInsecureTransportProtocol`, warns in debug (`src/config.rs:198-221`) | same tag |
| reqwest 0.13.5 (already in `Cargo.lock`, `:2989-2991`): `redirect::Policy::custom`, `Policy::redirect`, "The first URL in the previous is the initial URL", `Attempt::{status, url, previous, follow, stop, error}`; `ClientBuilder::redirect`, `timeout` ("total"), `read_timeout` ("applies to each read operation, and resets after a successful read") | local registry `reqwest-0.13.5/src/redirect.rs:102-136`, `:167-207`; `src/async_impl/client.rs:1392-1395`, `:1450-1465`; https://docs.rs/reqwest/0.13.5/reqwest/struct.ClientBuilder.html#method.read_timeout |
| tauri 2.12.5: `test::{mock_context, mock_builder, mock_app, noop_assets}`; `MockRuntime::new` / `new_any_thread` call `init()` with no thread assertion; `AppHandle::restart(&self) -> !`: on the main thread `cleanup_before_exit` + `process::restart`, else `request_exit(RESTART_EXIT_CODE)` and sleep; `process::restart` spawns `current_binary` then `exit(0)` | local registry `tauri-2.12.5/src/test/mod.rs:108-186`; `src/test/mock_runtime.rs:1174-1216`; `src/app.rs:606-631`; `src/process.rs:74-89` |
| tauri-build 2.7.4: default manifest = Common-Controls 6.0.0.0; `TAURI_CONFIG` "a JSON string that is merged into the parsed configuration file"; Windows resource with `ProductVersion` from the (merged) config version | local registry `tauri-build-2.7.4/src/windows-app-manifest.xml`; `src/lib.rs:71`, `:681`, `:795-827` |
| tauri-winres 0.3.7: `WindowsResource::compile()` links into binaries; `compile_for_everything()` into all targets | local registry `tauri-winres-0.3.7/src/lib.rs:530`, `:586` |
| tauri issues: #13419 (`STATUS_ENTRYPOINT_NOT_FOUND` with `tauri::test` on Windows), #14580 (a lib test referencing `tauri::Window`), #13948 (a child bin crate importing the app lib), PR #16320 (open: "the resource file, which includes the app manifest, is only linked into binaries … the `TaskDialogIndirect` import needs Common Controls v6") | `gh api repos/tauri-apps/tauri/issues/{13419,14580,13948,16320}` (2026-11-01) |
| Tauri CLI 2.12.4 `sign_file`: trusted comment `timestamp:<n>\tfile:<name>[\tversion:<v>]`; `.sig` = base64 of the minisign signature box; `sign_updaters`: no-op without updater settings or updater bundles; `--no-sign` skips; `TAURI_SIGNING_PRIVATE_KEY` may be the key text **or a path to the key file**; empty password under `--ci` | `crates/tauri-cli/src/helpers/updater_signature.rs:131-190`; `crates/tauri-cli/src/bundle.rs:236-300` (tag `tauri-cli-v2.12.4`, `gh api`) |
| Tauri bundler sets `APPIMAGE_EXTRACT_AND_RUN=1` on the linuxdeploy command itself | `crates/tauri-bundler/src/bundle/linux/appimage/linuxdeploy.rs:176` (tag `tauri-cli-v2.12.4`) |
| NSIS template: `currentUser` → `StrCpy $INSTDIR "$LOCALAPPDATA\${PRODUCTNAME}"`; `MAINBINARYNAME` from the main binary name; `CheckIfAppIsRunning`; relaunch with `nsis_tauri_utils::RunAsUser` | `crates/tauri-bundler/src/bundle/windows/nsis/installer.nsi:53`, `:446`, `:546-547`, `:690` (tag `tauri-cli-v2.12.4`) |
| minisign 0.10.0: `KeyPair::generate_unencrypted_keypair()`, `sign(pk, sk, reader, trusted_comment: Option<&str>, untrusted_comment: Option<&str>)`, `PublicKey::to_box()`, `SignatureBox::to_string()` | local registry `minisign-0.10.0/src/keypair.rs:29`, `src/lib.rs:69`, `src/public_key.rs:167`, `src/signature_box.rs:133` |
| GitHub release-asset download host: `release-assets.githubusercontent.com` ("Needed for downloading release assets") | https://docs.github.com/en/actions/reference/runners/self-hosted-runners (2026-11-01) |
| `PackageInfo`'s `relocate` element: a populated `<relocate>` (a `<bundle id=…/>` child) makes Installer search for the bundle; empty or absent limits the install to the install location; `BundleIsRelocatable` lives in the component plist | https://scriptingosx.com/2017/05/relocatable-package-installers-and-quickpkg-update/; https://developer.apple.com/forums/thread/743964 |
| Spec's External facts table (tauri-action 1.0.0, CLI 2.12.0 signed version, `BundleType`, sidecar names, pkgbuild man page, `rename(2)`, GitHub asset renaming, `gh release create`, runner sudo / UAC, `ubuntu-22.04` retirement, the CVEs) | `spec.md:147-172` (verified by the spec stage this session; reused, not re-derived) |
| Tauri CLI 2.12.4 `sign_updaters`: no private key → error "A public key has been found, but no private key"; after signing, a key id that differs from `plugins.updater.pubkey` is only `log::warn!` ("does not match the public key from `plugins > updater > pubkey`") and the build goes on | `crates/tauri-cli/src/bundle.rs:287-290`, `:311-318` (tag `tauri-cli-v2.12.4`, `gh api`, 2026-11-01) |
| Updater 2.14.1 Linux install: `install_inner` dispatches on `installer_for_bundle_type(bundle_type())` (`:1213-1219`); `install_deb` checks `infer::archive::is_deb`, then `try_tmp_locations` writes the bytes to `<tmp>/tauri_deb_update*/package.deb` as the user and `try_install_with_privileges` runs `pkexec dpkg -i`, then a zenity / kdialog password into `sudo -S`, then `sudo` (`:1329-1410`); the `{os}-{arch}-{installer}` then `{os}-{arch}` lookup (`:722-760`) | `src/updater.rs` at tag `updater-v2.14.1` |
| `bundle_type()` reads a binary-patched static: `Deb`, `Rpm`, `AppImage`, `Msi`, `Nsis`, unknown → `App` on macOS else `None`; `tauri` re-exports `tauri_utils as utils`; `BundleType` variants | local registry `tauri-utils-2.10.2/src/platform.rs:352-378`, `src/config.rs:132-147`; `tauri-2.12.5/src/lib.rs:124` (locked `Cargo.lock:4042-4043`) |
| Updater `Error` variants at 2.14.1, payload types: `Io(std::io::Error)`, `Semver(semver::Error)`, `Serialization(serde_json::Error)`, `UrlParse(url::ParseError)`, `Reqwest(reqwest::Error)`, `TargetNotFound(String)`, `TargetsNotFound(Vec<String>)`, `Network(String)`, `Minisign(minisign_verify::Error)`, `Base64(base64::DecodeError)`, `SignatureUtf8(String)`, `PreviousAppNotRestored(PathBuf)`, `Http(http::Error)`, `InvalidHeaderValue`, `InvalidHeaderName`, `SignedVersionMismatch { signed, announced }`, `Tauri(tauri::Error)`, the rest unit variants (32 without `zip`) | `src/error.rs` at tag `updater-v2.14.1`; `tauri::Error::UnknownPath` (`tauri-2.12.5/src/error.rs:134`); locked `semver 1.0.28`, `http 1.5.0`, `minisign-verify 0.3.0`, `base64 0.23.1` (`Cargo.lock:3332-3333`, `:1533-1534`, `:2215-2216`, `:229-230`) |
| dirs 7.0.0 `data_dir()` / `data_local_dir()`; dirs-sys 0.5.0 resolves them with `SHGetKnownFolderPath(FOLDERID_RoamingAppData / FOLDERID_LocalAppData)` | local registry `dirs-7.0.0/src/lib.rs:122`, `:134`; `dirs-sys-0.5.0/src/lib.rs:150-181` (locked `Cargo.lock:794-795`, `:803-804`) |
| GitHub: downloading a workflow artifact needs read access to the repository | https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts (2026-11-01) |
| reqwest 0.13.5 SOCKS (PD-30): feature `socks = []` (`Cargo.toml:178`); hyper-util is a dependency on every non-wasm target with `http1, client, client-legacy, client-proxy, tokio` (`:347-355`); `IntoProxy` accepts `socks4` / `socks4a` / `socks5` / `socks5h` URLs whatever the features, default port 1080 (`src/proxy.rs:124-137`); `connect_via_proxy` sends those four schemes to `connect_socks` only under `cfg(feature = "socks")` (`src/connect.rs:783-792`); `connect_socks`: `socks4` / `socks5` resolve DNS locally, `socks4a` / `socks5h` at the proxy (`:538-541`), TLS over the tunnel for https in the rustls build, plain TCP for http (`:565-609`); `socks::connect` over hyper-util `SocksV4` / `SocksV5`, SOCKS5 user / password from the URL (`:1877-1960`); docs "By enabling the "socks" feature it is possible to use a socks proxy" (`proxy.rs:48-54`) | local registry `reqwest-0.13.5`; https://docs.rs/reqwest/0.13.5/reqwest/struct.Proxy.html |
| hyper-util 0.1.21 (locked, `Cargo.lock:1600-1601`): `client-proxy` exports `SocksV4`, `SocksV5`, `Tunnel` | local registry `hyper-util-0.1.21/src/client/legacy/connect/proxy/mod.rs:2-6` |
| Updater 2.14.1 applies the builder's / the `Update`'s proxy as `request.proxy(reqwest::Proxy::all(proxy.as_str())?)`, or `request.no_proxy()` | `src/updater.rs:594-600` (check), `:852-856` (download) at tag `updater-v2.14.1` (`gh api …/contents/plugins/updater/src/updater.rs?ref=updater-v2.14.1`, 2026-11-01) |
| ureq 3.5.0 `default_resolve_target`: `Socks4` / `Socks5` true ("we must locally resolve before using proxy"), `Socks4A` / `Socks5h` false | local registry `ureq-3.5.0/src/proxy.rs:56-63` |
| Cargo feature unification: "When a dependency is used by multiple packages, Cargo will use the union of all features enabled on that dependency when building it." | https://doc.rust-lang.org/cargo/reference/features.html#feature-unification (2026-11-01) |
| *Revision 4.* Tauri CLI 2.12.4 logger: a record without an `action` key is written as `{:>12}` of `prettyprint_level` (`"Warn"` for `Level::Warn`) then the message, so the key-mismatch `log::warn!` prints as `        Warn The updater secret key …` (after ANSI stripping), never as a `warning:` line | `crates/tauri-cli/src/lib.rs:246-262`, `:310-318`; the warning `crates/tauri-cli/src/bundle.rs:311-318` (tag `tauri-cli-v2.12.4`, `gh api`, 2026-11-01) |
| tauri 2.12.5 `process::current_binary` (the AppImage path on Linux, else `tauri_utils::platform::current_exe`) and `process::restart`, which spawns only `if let Ok(path) = current_binary(env)` and then `exit(0)`; tauri-utils 2.10.2 `current_exe`: "On `macOS`, this function will return an error if the original path contained any symlinks … can be disabled by setting the `process-relaunch-dangerous-allow-symlink-macos` feature, although it is *highly discouraged*" | local registry `tauri-2.12.5/src/process.rs:48-56`, `:74-88`; `tauri-utils-2.10.2/src/platform.rs:116-118`, `:180` |
| linuxdeploy: `if (getenv("NO_STRIP") != nullptr)` → warns and clears the strip operations; the Tauri bundler runs linuxdeploy with the inherited environment plus `OUTPUT`, `ARCH`, `APPIMAGE_EXTRACT_AND_RUN=1` | linuxdeploy `src/core/appdir.cpp:247-249` (master `6bcfbfe`, `gh api`, 2026-11-01); `crates/tauri-bundler/src/bundle/linux/appimage/linuxdeploy.rs:172-176` (tag `tauri-cli-v2.12.4`) |
| tauri-apps/tauri issue #14796 ("Can't build appimage using linuxdeploy", 2026-01-19, closed): linuxdeploy's strip fails on system libraries with `.relr.dyn` (`unknown type [0x13] section`), reported on `ubuntu-latest`, Ubuntu 22.04 and Fedora; maintainer: `NO_STRIP=true` "shouldn't be required on Ubuntu, only rolling release distros" | `gh api repos/tauri-apps/tauri/issues/14796` and its comments (2026-11-01) |

---

## 2. File layout

```
apps/krate-manager/
  Cargo.toml                          🔶 [workspace.dependencies] tauri-plugin-updater (PD-2); exclude += updater-refusals
  Cargo.lock                          🔶 tauri-plugin-updater 2.14.1 and its graph (tar, infer, osakit, …); reqwest's
                                         `socks` adds no package (PD-30)
  .gitignore                          🔶 /tests/fixtures/updater-refusals/target/
  crates/core/src/self_update.rs      🆕 URL, package-origin and hop rules, redirect decision, proxy choice, busy rule,
                                         packaging rule, in-flight registry, action gate, codes and sentences, progress
                                         events, view model, timeouts, HttpConfig choice (FR-010 to FR-019, FR-026, FR-027)
  crates/core/src/self_update_tests.rs 🆕
  crates/core/src/lib.rs              🔶 pub mod self_update;
  crates/core/src/context.rs          🔶 release_for_restart, LockRelease (FR-016)
  crates/core/src/context_tests.rs    🔶 release_for_restart_frees_the_lock
  crates/core/src/http.rs             🔶 route_choice, pac_choices (PD-6)
  crates/core/src/install.rs          🔶 ElevationGuard::is_held, any_busy, InstallError::SelfUpdating, 3 gate calls
  crates/core/src/install_tests.rs    🔶 refuse_while_self_updating
  crates/core/src/download.rs         🔶 any_in_flight, DownloadError::SelfUpdating, 1 gate call
  crates/core/tests/config_guard.rs   🔶 FR-003, FR-094 pins (features, capability, updater config, release conf)
  crates/core/tests/injection.rs      🔶 FR-094 confinements; the tar rule (SI-2); harness-source rule; updater-e2e
                                         cfg count (PD-26); timeout and lib.rs pins (SI-24); production-inputs pins
                                         (PD-36); the macOS current_binary pre-check pin (PD-37)
  src-tauri/Cargo.toml                🔶 tauri-plugin-updater; reqwest =0.13.5 with socks (SI-28, PD-30); [features]
                                         updater-e2e = []
  src-tauri/tauri.conf.json           🔶 plugins.updater (D2)
  src-tauri/tauri.release.conf.json   🆕 {"bundle":{"createUpdaterArtifacts":true}}
  src-tauri/build.rs                  🔶 ten commands
  src-tauri/capabilities/main.json    🔶 two allow-* entries
  src-tauri/src/lib.rs                🔶 pub mod self_update; plugin; managed state; two commands; the two e2e call
                                         sites of PD-26 (override, setup)
  src-tauri/src/commands.rs           🔶 manager_update_check, manager_update_install
  src-tauri/src/self_update.rs        🆕 adapter, kept slot, UpdateInstaller, NativeUpdateInstaller, map_error, e2e mod
  src/manager_update.ts               🆕 the update line, confirmation, progress, failure lines
  src/manager_update.test.ts          🆕
  src/main.ts, src/status.ts          🔶 mount order; setActionsEnabled; releaseButton exported with a label (PD-31)
  src/main.test.ts, src/status.test.ts 🔶 order tests; disabled-actions tests (the end-to-end one in main.test.ts,
                                         FR-025 wiring)
  scripts/bundle.js, bundle.test.js   🔶 --release mode (FR-030 to FR-033, PD-15, PD-20)
  scripts/e2e-update-server.js        🆕 loopback static server for updater-e2e (+ e2e-update-server.test.js)
  tests/fixtures/updater-refusals/    🆕 Cargo.toml, Cargo.lock, build.rs,
                                         src/{main,scenarios,keys,map_errors,confinement,socks}.rs (socks: PD-30);
                                         fixtures/rsign/{package.bin, package.bin.minisig, rsign.pub} and
                                         make-rsign-fixture.js (PD-35, rsign-format; the secret key never committed)
  tests/fixtures/clippy-guard/Cargo.lock 🔶 re-locked (the updater graph; PD-30 adds nothing)
  tests/required-tests.json           🔶 FR-095
tools/gen-manager-latest.js            🆕 FR-040 to FR-042, PD-21
tools/manager-release-preflight.js     🆕 FR-051, FR-063
tools/feed-publish.js                  🔶 checkLatestJson(…, repo), exported (FR-043, PD-17)
tools/feed-contract/tests/gen-manager-latest.test.js, manager-release-preflight.test.js, release-manager-shape.test.js 🆕
tools/feed-contract/tests/feed-publish-collect.test.js, manager-ci-shape.test.js, hygiene.test.js 🔶
tools/feed-contract/tests/fixtures-manager/  🆕 fixture app dirs and CHANGELOGs for the preflight tests (generated in temp
                                       dirs by the test; only the expected release body is committed: release-body.expected.md)
.github/workflows/release-manager.yml  🆕
.github/workflows/manager-ci.yml       🔶 four check steps (clippy updater-e2e PD-32, clippy updater-refusals, updater
                                         refusals (if: by PD-33), updater-e2e absent)
.github/workflows/feed-tools.yml       🔶 paths, PLAN 1..64, NAMES
.gitattributes                          🔶 tools/feed-contract/keys/** -text (FR-097; already present at 30878b36,
                                         `.gitattributes:29`, so no change); apps/krate-manager/tests/fixtures/
                                         updater-refusals/fixtures/** -text (the rsign fixture is byte-exact)
tools/feed-contract/keys/README.md     🔶 §2's Phase 11 sentence (SI-20; README is not in FR-092's fence)
specs/Krate-manager-roadmap.md         🔶 FR-093 (a)-(e), by the build stage
specs/krate-manager-phase11/evidence/d9-windows.txt 🆕 the D9 measurement line (PD-33), by T014
```

The committed expected release body lives at `tools/feed-contract/tests/release-body.expected.md` (not under
`fixtures/`, whose `-text` attribute and `make-fixtures.js` sync check would claim it, `hygiene.test.js:374`); the test
normalises CRLF before comparing.

---

## 3. Error model

### 3.1 Core (`crates/core/src/self_update.rs`)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)] #[serde(rename_all = "snake_case")]
pub enum UrlRefusal { ApiHost, LatestPath, Policy, Host }

#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error, Serialize)]
#[serde(tag = "code", rename_all = "SCREAMING_SNAKE_CASE")]
pub enum SelfUpdateError {
    #[error("E_SELF_UPDATE_NETWORK: {}", NETWORK_SENTENCE)]      Network { detail: String },
    #[error("E_SELF_UPDATE_MANIFEST: {}", MANIFEST_SENTENCE)]    Manifest { detail: String },
    #[error("E_SELF_UPDATE_SIGNATURE: {}", SIGNATURE_SENTENCE)]  Signature { detail: String },
    #[error("E_SELF_UPDATE_URL: {}", url_sentence(*.reason))]    Url { reason: UrlRefusal },
    #[error("E_SELF_UPDATE_BUSY: {}", BUSY_SENTENCE)]            Busy,
    #[error("E_SELF_UPDATE_NONE: {}", NONE_SENTENCE)]            None,
    #[error("E_SELF_UPDATE_INSTALL: {}", INSTALL_SENTENCE)]      Install { detail: String },
    #[error("E_SELF_UPDATE_UNSUPPORTED: {}", UNSUPPORTED_SENTENCE)] Unsupported,
}
impl SelfUpdateError { pub fn code(&self) -> &'static str; pub fn sentence(&self) -> &'static str; }
pub const CODES: [&str; 8];          // SC-003 iterates it
```

The serialized form carries `code`, the variant's fields and (added by a manual `Serialize` wrapper `ErrorView { code,
sentence, reason?, detail? }`) the fixed sentence, so the UI never composes a sentence. `detail` is for the log and the
"Show details" text only; every user line starts with the code (FR-024). Sentences (fixed, tested exact):

| Code | Sentence |
|---|---|
| `E_SELF_UPDATE_NETWORK` | "Could not reach the Krate Manager update server." |
| `E_SELF_UPDATE_MANIFEST` | "The Krate Manager update information is incomplete or has no entry for this computer." |
| `E_SELF_UPDATE_SIGNATURE` | "The Krate Manager update failed its signature check and was not installed." |
| `E_SELF_UPDATE_URL` | "The Krate Manager update points to an address the manager does not use." |
| `E_SELF_UPDATE_BUSY` | "Finish the plugin installs and removals first, then update Krate Manager." |
| `E_SELF_UPDATE_NONE` | "There is no Krate Manager update to install. Check for updates first." |
| `E_SELF_UPDATE_INSTALL` | "Krate Manager could not install its update. It keeps running the current version." |
| `E_SELF_UPDATE_UNSUPPORTED` | "Krate Manager cannot update itself on this computer. Install the new version from its release page." (SI-21: also a `.deb` / `.rpm` install) |

`E_INSTALL_SELF_UPDATING` (FR-018) is the `SelfUpdating` unit struct's `Display`, defined once in this module (PD-7).

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)] #[serde(tag = "event", rename_all = "snake_case")]
pub enum SelfUpdateProgress { Downloading, Installing }       // PD-25, sent over manager_update_install's Channel
```

### 3.2 `src-tauri` error map (`src-tauri/src/self_update.rs`, FR-013, PD-3)

```rust
pub fn map_error(e: &tauri_plugin_updater::Error) -> SelfUpdateError
```

| Updater variant(s) | Code |
|---|---|
| `Reqwest(e)` whose source chain holds `HopRefused(r)` | `Url { reason: r }` |
| `Reqwest(e)` whose source chain holds a `serde_json::Error` (latest.json that is not JSON; reqwest 0.13.5 `Response::json` wraps serde as `decode`, and a stalled body is a `decode` too, so `is_decode()` alone is wrong) | `Manifest` |
| `Reqwest(_)` otherwise (connect, timeout, read timeout, body), `Network(_)` | `Network` |
| `ReleaseNotFound` (also a non-2xx endpoint status, §1.2), `Serialization`, `TargetNotFound`, `TargetsNotFound`, `UrlParse`, `Semver`, `FormatDate` | `Manifest` |
| `Minisign`, `Base64`, `SignatureUtf8`, `SignedVersionMismatch { .. }`, `MissingSignedVersion` | `Signature` |
| `InsecureTransportProtocol` | `Url { reason: Policy }` |
| `UnsupportedArch`, `UnsupportedOs` | `Unsupported` |
| `Io`, `TempDirNotOnSameMountPoint`, `PreviousAppNotRestored`, `BinaryNotFoundInArchive`, `TempDirNotFound`, `AuthenticationFailed`, `DebInstallFailed`, `PackageInstallFailed`, `InvalidUpdaterFormat`, `FailedToDetermineExtractPath`, `EmptyEndpoints`, `Http`, `InvalidHeaderValue`, `InvalidHeaderName`, `Tauri` | `Install { detail }` |
| `_` (non-exhaustive; PD-3) | `Install { detail: "unmapped updater error: …" }` |

`detail` is `e.to_string()` with userinfo redacted through `http::redact_userinfo` where a URL appears.

Every row is pinned by the harness line `map-errors` (§4.7): 31 of the 32 variants are constructed there (`Reqwest`
has no public constructor; its two rows are reached by `redirect-to-api-host`, `redirect-to-http`, `redirect-loop`
(`HopRefused`) and `offline`, `stall` (otherwise)), each asserted to the code (and `reason`) of this table, including
`UnsupportedOs` / `UnsupportedArch` → `E_SELF_UPDATE_UNSUPPORTED` and `InsecureTransportProtocol` → `Url { Policy }`.
The `_` arm is unreachable at the pinned 2.14.1 without `zip`; it is covered by the exact pin, not by a row.

---

## 4. Module design

### 4.1 `core::self_update` (D8; FR-010 to FR-019, FR-026, FR-027)

```rust
pub const CHECK_TIMEOUT: Duration = Duration::from_secs(30);       // FR-027
pub const DOWNLOAD_TIMEOUT: Duration = Duration::from_secs(3600);  // FR-027, set on Update.timeout
pub const STALL_BOUND: Duration = Duration::from_secs(30);         // FR-027, reqwest read_timeout
const API_HOST_LABELS: [&str; 3] = ["api", "github", "com"];       // PD-4
pub const GITHUB_HOST: &str = "github.com";                         // FR-011a package origin
pub const MANAGER_REPO: &str = "rolandzwaga/krate-audio";          // PD-27: owner/repo of every package URL and page;
                                                                    // the preflight's E_PREFLIGHT_REPO reads this line
pub const RELEASE_ASSET_HOST: &str = "release-assets.githubusercontent.com"; // FR-011a, §1.2 GitHub docs
const LATEST_SEGMENT: &str = "/releases/latest/";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UpdateTimeouts { pub check: Duration, pub download: Duration, pub stall: Duration }   // PD-29
impl UpdateTimeouts { pub const fn production() -> Self /* CHECK_TIMEOUT, DOWNLOAD_TIMEOUT, STALL_BOUND */ }
pub fn self_update_http(ctx: Option<&AppContext>, fallback: &HttpConfig) -> HttpConfig;      // PD-9, FR-026

#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum Packaging { SystemPackage, SelfContained } // PD-24
pub fn self_install_supported(p: Packaging) -> Result<(), SelfUpdateError>;                 // SystemPackage → Unsupported

pub fn check_update_url(policy: UrlPolicy, url: &Url) -> Result<(), SelfUpdateError>;
pub fn check_update_package_url(policy: UrlPolicy, url: &Url, announced: &str) -> Result<(), SelfUpdateError>; // PD-27

#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum HopKind { Endpoint, Package }
#[derive(Clone, Debug, PartialEq, Eq)] pub struct HopRules { pub policy: UrlPolicy, pub endpoint_host: String }
pub fn check_update_hop(rules: &HopRules, kind: HopKind, from: &Url, to: &Url) -> Result<(), SelfUpdateError>;
pub fn hop_kind(first: &Url, endpoint: &Url) -> HopKind;                                     // PD-5
pub fn redirect_decision(rules: &HopRules, endpoint: &Url, previous: &[Url], to: &Url) -> Result<(), UrlRefusal>; // PD-5

pub fn proxy_choice(http: &HttpConfig, url: &Url) -> Result<ProxyChoice, SelfUpdateError>; // D17, PD-6: an Err is Network, no request
pub fn self_update_busy(elevation: &dyn Elevation, ctx: Option<&AppContext>) -> bool; // FR-015, PD-8

pub struct InFlightGuard { key: Option<PathBuf> }                      // FR-017, PD-7
impl InFlightGuard { pub fn enter(state_dir: Option<&Path>) -> InFlightGuard; }
impl Drop for InFlightGuard { /* removes the key */ }
pub fn in_flight(state_dir: &Path) -> bool;
#[derive(Clone, Copy, Debug, PartialEq, Eq, thiserror::Error)]
#[error("E_INSTALL_SELF_UPDATING: Krate Manager is updating itself. Try again after it restarts.")]
pub struct SelfUpdating;
pub fn action_gate(state_dir: &Path) -> Result<(), SelfUpdating>;      // FR-018

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct ManagerUpdateView { pub os: OsName, pub current: String, pub available: Option<AvailableView>, pub error: Option<ErrorView> }
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct AvailableView { pub version: String, pub date: Option<String>, pub notes: Vec<notes::Block>, pub action: UpdateAction }
#[derive(Clone, Debug, PartialEq, Eq, Serialize)] #[serde(tag = "kind", rename_all = "snake_case")]
pub enum UpdateAction { InstallAndRestart, OpenReleasePage { url: String } }                 // PD-31
pub fn update_action(packaging: Packaging, policy: UrlPolicy, download_url: &Url, announced: &str) -> Result<UpdateAction, SelfUpdateError>;
pub struct UpdateFacts<'a> { pub version: &'a str, pub date_rfc3339: Option<String>, pub body: Option<&'a str>, pub action: UpdateAction }
pub fn view(os: Os, current: &str, update: Option<UpdateFacts<'_>>, error: Option<SelfUpdateError>) -> ManagerUpdateView; // FR-014
```

**`check_update_url` (FR-011), in this order:** (1) host labels equal `API_HOST_LABELS` → `Url { ApiHost }`;
(2) path contains `/releases/latest/` → `LatestPath`; (3) `http::check_url(policy, url)` → `Policy` on refusal (userinfo
included). Host comparison is on `url.host_str()` lower-cased with one trailing `.` stripped, so `API.GITHUB.COM.` is
caught.

**`check_update_hop` (FR-011a):** `check_update_url(rules.policy, to)?`; then under `LoopbackOnly` any loopback `to` is
accepted (the URL check already required loopback); under `HttpsOnly`: `Endpoint` → `to` host must equal
`rules.endpoint_host`; `Package` → `from` host `GITHUB_HOST` or `RELEASE_ASSET_HOST` and `to` host one of the two (a
github.com → github.com hop is GitHub's own canonicalisation), else `Url { Host }`. `from` is the hop's previous URL.

**`check_update_package_url` (FR-011, FR-011a, PD-27, SI-40):** `check_update_url(policy, url)?`; then, under both
policies, the URL must carry no query and no fragment and the private helper `per_tag_segments(url) -> Option<&str>` must
return its tag segment: `path_segments()` exactly 7 non-empty segments `[owner, repo, "releases", "download",
"manager", "v<x.y.z>", name]` with `format!("{owner}/{repo}") == MANAGER_REPO` (exact, case-sensitive: GitHub's own
canonical form, the one `gen-manager-latest.js --repo ${{ github.repository }}` writes) and `v<x.y.z>` a semver core
version; the tag must equal `format!("v{announced}")` (`announced` = `Update.version`, the updater's
`semver::Version` serialisation, which for a manager release is the core form `x.y.z`, `MANAGER_TAG_RE`
`feed-publish.js:63`; a pre-release or build suffix can never equal a core-only tag); under `HttpsOnly` the host
(lower-cased, one trailing `.` stripped) must also equal `GITHUB_HOST`. Any failure is `Url { Host }`. So a `latest.json`
can name only this repository's release of the version it announces: no foreign repository, no other tag (no
downgrade by URL), and no GitHub asset outside that release is ever fetched.

**`hop_kind` / `redirect_decision` (PD-5):** `hop_kind` is `Endpoint` iff `first == endpoint`, else `Package`.
`redirect_decision`: `previous.len() > http::MAX_REDIRECTS` → `Err(Policy)`; then `check_update_hop(rules,
hop_kind(&previous[0], endpoint), previous.last(), to)` with its `Url { reason }` passed through (any other error
`Host`). `previous` is never empty in a redirect attempt (reqwest 0.13.5 `redirect.rs:135`).

**`proxy_choice` (D17, FR-012):** `route_choice(http, url)` → `Ok(choice)`, or `Err(e)` →
`Err(Network { detail: e.to_string() })`; the caller makes no request on `Err`.

**`self_install_supported` (PD-24):** `SystemPackage` → `Err(Unsupported)`, `SelfContained` → `Ok(())`.

**`update_action` (PD-31, SI-40):** first, for both packagings, `check_update_package_url(policy, download_url,
announced)?` (revision 4: a URL that fails PD-27 is a check error, no banner, on a self-contained install too; the
install-time call in `prepare_download` stays). `SelfContained` → `Ok(InstallAndRestart)`. `SystemPackage`: (1) `let
page = format!("https://{}/{}/releases/tag/manager/v{}", status::RELEASE_PAGE_HOST, MANAGER_REPO, announced)`: built
from the compiled repo and the announced version, never from the URL's segments (which the check above has already
required to be the same values); (2) `status::release_page_url(&page) == Some(page)` (the `open_release_page`
admission, `commands.rs:54-59`) else `Err(Url { Host })`; (3) `Ok(OpenReleasePage { url: page })`. Under `LoopbackOnly`
the page is still the `github.com` form (the e2e server and the harness serve the per-tag path with `MANAGER_REPO`,
§4.9, §4.7), so the harness can assert it and no loopback URL ever reaches the opener. `view` copies
`UpdateFacts.action` into `AvailableView.action`.

**`self_update_http` (PD-9):** `ctx.map_or_else(|| fallback.clone(), |c| c.http.clone())`.

**`self_update_busy` (FR-015):** `elevation.guard().is_held()` ∨ `ctx.is_some_and(|c| install::any_busy(c.roots.state_dir())
∨ download::any_in_flight(&c.roots.staging_dir().join(download::DOWNLOADS_DIR)))`. With `None` only the elevation guard
can be held, and with no context no batch can start, so it reads "not busy" (FR-026).

**`action_gate`:** `if in_flight(state_dir) { Err(SelfUpdating) } else { Ok(()) }`. Called before any network,
filesystem or elevation work: first statement of `download::for_command` (before `command_request`, `download.rs:1611`),
of `install::for_command` (before `install_command`, `install.rs:3881`), of `install::remove_for_command` (before
`command_remove`, `:4080`), and in `elevate` before `elevation.helper()` (`:1298`). No `Busy` entry is made before it, so
none is left (FR-018).

### 4.2 Core changes outside `self_update`

- `http.rs` (PD-6): `pub fn route_choice(config: &HttpConfig, url: &url::Url) -> Result<ProxyChoice, HttpError>`;
  private `fn pac_choices(config, state, hop, deadline, static_cfg: Option<&ProxyConfig>) -> Result<Vec<ProxyChoice>,
  HttpError>`; `pac_route` becomes `pac_choices(..)?.into_iter().map(to_entry)` where `to_entry(Direct) = None`,
  `to_entry(Proxy(p)) = { check_proxy(config.policy, &p)?; Some(to_ureq_proxy(&p)?) }`, and `Failed` / bypass map to
  `static_choice(static_cfg, hop)` = `Proxy(cfg.clone())` unless `bypasses(&cfg.no_proxy, host)`, else `Direct`. The
  existing `http_tests.rs` PAC and proxy cases are the regression net for the split (no expected value changes).
- `context.rs` (FR-016, PD-10): `pub enum LockRelease { Released, NoLock, StillShared }`;
  `pub fn release_for_restart(state: &SharedState) -> LockRelease`.
- `install.rs`: `impl ElevationGuard { pub fn is_held(&self) -> bool }` and, under `cfg(any(test, feature =
  "test-support"))` only, `pub fn hold_for_test(&self) -> GuardHeldForTest<'_>` (§4.7); `pub fn any_busy(state_dir: &Path) -> bool`;
  `InstallError::SelfUpdating(#[from] self_update::SelfUpdating)` with `#[error(transparent)]` and `code()` arm returning
  `self_update::SELF_UPDATING_CODE`; three gate calls (§4.1).
- `download.rs`: `pub fn any_in_flight(downloads: &Path) -> bool` (`registry().iter().any(|(d, _)| d == downloads)`);
  `DownloadError::SelfUpdating(#[from] SelfUpdating)` transparent; `code()` arm; one gate call. `pub const DOWNLOADS_DIR`
  is already the dir name (`download.rs:1370`).
- `lib.rs`: `pub mod self_update;`.

Nothing else of Phases 5-10 changes (FR-092).

### 4.3 `src-tauri/src/self_update.rs` (FR-022, FR-023, D7, D17, FR-027)

```rust
pub struct SelfUpdateHttp(pub HttpConfig);                         // PD-9, managed
pub struct KeptUpdate(Mutex<Option<Update>>);                      // FR-023, managed, single slot
pub struct UpdateInputs { pub http: HttpConfig, pub policy: UrlPolicy, pub timeouts: UpdateTimeouts, pub packaging: Packaging }
impl UpdateInputs { pub fn production(http: HttpConfig) -> Self }  // body pinned exactly (PD-36):
    // Self { http, policy: UrlPolicy::HttpsOnly, timeouts: UpdateTimeouts::production(), packaging: packaging() }
pub fn packaging() -> Packaging;     // tauri::utils::platform::bundle_type(): Some(Deb | Rpm) → SystemPackage, else SelfContained (PD-24)

pub trait UpdateInstaller<R: Runtime>: Send + Sync {
    fn install(&self, app: &AppHandle<R>, update: &Update, bytes: Vec<u8>) -> Result<(), SelfUpdateError>;
}
pub struct NativeUpdateInstaller;                                    // the only Update::install call (FR-094)

pub fn endpoint_of(config: &tauri::Config) -> Result<Url, SelfUpdateError>;  // plugins.updater.endpoints[0]
pub fn build_updater<R: Runtime>(app: &AppHandle<R>, inputs: &UpdateInputs) -> Result<Updater, SelfUpdateError>;
pub async fn check<R: Runtime>(app: &AppHandle<R>, inputs: &UpdateInputs) -> ManagerUpdateView;
pub fn prepare_download(update: &mut Update, inputs: &UpdateInputs) -> Result<(), SelfUpdateError>; // PD-29
pub async fn install<R: Runtime>(
    app: &AppHandle<R>, inputs: &UpdateInputs, installer: &dyn UpdateInstaller<R>,
    busy: &(dyn Fn() -> bool + Sync), state_dir: Option<&Path>,
    progress: &(dyn Fn(SelfUpdateProgress) + Sync),                   // PD-25
) -> Result<(), SelfUpdateError>;
pub fn map_error(e: &tauri_plugin_updater::Error) -> SelfUpdateError;   // §3.2
#[derive(Debug)] struct HopRefused(UrlRefusal);                      // std::error::Error + Display, PD-5
```

**`build_updater`:** `let endpoint = endpoint_of(&app.config())?; check_update_url(inputs.policy, &endpoint)?;` then
`app.updater_builder().timeout(inputs.timeouts.check)`, `proxy_choice(&inputs.http, &endpoint)?` → `.proxy(url)` or
`.no_proxy()`, `.configure_client(move |b| b.read_timeout(inputs.timeouts.stall).redirect(policy(rules.clone(),
endpoint.clone())))`, `.build()` (mapped). `HopRules.endpoint_host` = `endpoint.host_str()`. No `version_comparator`,
no `installer_args`, no `restart_after_install(false)`, no retry of any kind (the updater has none; `injection.rs`
refuses a `retry` token in the file, SI-24).

**Redirect policy (PD-5):** `reqwest::redirect::Policy::custom(move |a| match redirect_decision(&rules, &endpoint,
a.previous(), a.url()) { Ok(()) => a.follow(), Err(reason) => a.error(HopRefused(reason)) })`; every decision is the
core's pure function.
`reqwest` must be a direct `src-tauri` dependency: the updater does not re-export it (its `lib.rs:36-38` re-exports
`Config`, `Error`, `Result` and `updater::*`; `updater.rs` imports `reqwest::ClientBuilder` with a private `use`), and
`redirect::Policy` has to be named. So `src-tauri/Cargo.toml` gains `reqwest = { version = "=0.13.5", default-features =
false, features = ["socks"] }` (the version already locked, `Cargo.lock:2989-2991`: no new resolution; the one new
feature in the unified graph is `socks`, which turns SOCKS on in the updater's own client and adds no crate, PD-30), with
the comment `# Phase 11 PD-5, PD-30: self_update.rs only (redirect::Policy, ClientBuilder::read_timeout); the updater's
own reqwest, socks unified into its client`. No code line changes for SOCKS: `build_updater` and `prepare_download`
already pass the `Proxy(cfg)` URL `proxy_choice` returns (a `socks4://` / `socks5://` one included) to
`UpdaterBuilder::proxy` / `Update.proxy`. FR-094 gains the token home `src-tauri/Cargo.toml` + `src-tauri/src/self_update.rs` for
`reqwest`, and `config_guard` pins the line exactly (both recorded against FR-001 / FR-094 as SI-28). Its version moves only with the updater's (the updater's docs ask
`configure_client` users to pin, `updater.rs:423-426`).

**`check` (D7, FR-023, FR-026):** builds (an endpoint refused by `check_update_url` or a `proxy_choice` error is a view
error with no request, harness `endpoint-latest-path`), `updater.check().await`; `Ok(Some(u))` →
`update_action(inputs.packaging, inputs.policy, &u.download_url, &u.version)` (PD-31, PD-27): `Ok(a)` → stores `u` in `KeptUpdate`
(replacing the previous one) and returns the view with `action: a`; `Err(e)` → clears the slot, view with `error:
Some(e)` and no `available`; `Ok(None)` → clears the slot, view with `available: None`; `Err(e)` → clears
the slot, view with `error: Some(map_error(&e))`. A `build_updater` error is a view error too. Never panics.

**`prepare_download` (PD-29):** `check_update_package_url(inputs.policy, &update.download_url, &update.version)?`
(FR-011 / PD-27 before `download`); `update.timeout = Some(inputs.timeouts.download)`; `proxy_choice(&inputs.http,
&update.download_url)?` → `update.proxy = Some(url); update.no_proxy = false` or `update.proxy = None; update.no_proxy =
true` (D17; public fields, §1.2). It is the only site that sets `Update.timeout` (`injection.rs`, SI-24).

**`install` (D7, FR-015 to FR-019, FR-023, FR-027):**
1. `self_install_supported(inputs.packaging)?` (PD-24: a `.deb` / `.rpm` install, before anything else).
1b. `#[cfg(target_os = "macos")] tauri::process::current_binary(&app.env()).map_err(|_| Unsupported)?;` (PD-37:
   `app.restart()` could not relaunch from a symlinked path, so refuse before the slot, the busy check or any request;
   the `injection.rs` pin requires exactly one `current_binary(` in the file, inside `install`, before `.take()`).
2. `if busy() { return Err(Busy) }` (FR-015, before any request; the kept update stays in the slot, PD-28).
3. `let mut update = kept.take().ok_or(SelfUpdateError::None)?` (from here every `Err` leaves the slot empty: FR-023 as
   reworded by SI-36, "cleared after every install failure that took the update", FR-019's busy refusal included; an
   `Err` from steps 1, 1b or 2 leaves it in place; on success the process ends).
4. `let _flight = InFlightGuard::enter(state_dir);` (FR-017).
5. `progress(SelfUpdateProgress::Downloading)` (PD-25).
6. `prepare_download(&mut update, inputs)?`.
7. `let bytes = update.download(|_, _| {}, || {}).await.map_err(|e| map_error(&e))?;` (verifies; no retry).
8. `if busy() { return Err(Busy) }` (FR-019).
9. `progress(SelfUpdateProgress::Installing)` (only after step 8 passed).
10. `installer.install(app, &update, bytes)` → `Err` returns it (the guard drops: the flag clears; the slot is already
    empty).
On every `Err` after step 4 the `InFlightGuard` drops at scope end (FR-017's "cleared when the install returns an
error").

**`NativeUpdateInstaller::install`:** `update.install(bytes).map_err(map_error)?;` then on Windows this line is never
reached (`exit(0)` inside, §1.2); on macOS / Linux `release_for_restart(&app.state::<SharedState>())` (log a
`StillShared`), then `app.restart()` (`-> !`). The type signature returns `Result<(), _>`; the post-`install` path is
`#[cfg(not(windows))]`.

### 4.4 Commands, registration, config (FR-001, FR-002, FR-020, FR-021)

- `lib.rs`: `.plugin(tauri_plugin_updater::Builder::new().build())` right after `navigation::guard()`;
  `.manage(SelfUpdateHttp(http_copy))`, `.manage(KeptUpdate::default())`; `generate_handler!` gains
  `commands::manager_update_check`, `commands::manager_update_install`; the two e2e call sites of PD-26 (after
  `lib.rs:69`: `#[cfg(feature = "updater-e2e")] let (feed_url, http) = self_update::e2e::override_feed(feed_url, http);`;
  in `setup`: `#[cfg(feature = "updater-e2e")] self_update::e2e::start(app.handle());`, §4.5), and nothing else behind
  that feature.
- `commands.rs`:
  ```rust
  #[tauri::command] pub async fn manager_update_check(app: tauri::AppHandle, http: tauri::State<'_, SelfUpdateHttp>,
      state: tauri::State<'_, SharedState>) -> Result<ManagerUpdateView, String>;
  #[tauri::command] pub async fn manager_update_install(app: tauri::AppHandle, http: tauri::State<'_, SelfUpdateHttp>,
      state: tauri::State<'_, SharedState>, elevation: tauri::State<'_, Arc<NativeElevation>>,
      on_progress: tauri::ipc::Channel<SelfUpdateProgress>) -> Result<(), String>;
  ```
  Neither takes a webview argument. Both clone the context (as `commands.rs:70-73`) and build
  `UpdateInputs::production(self_update_http(ctx.as_ref(), &http.0))` (PD-9), the only way `commands.rs` makes inputs
  (PD-36, pinned). `manager_update_install` passes
  `busy = || self_update_busy(&*elevation, ctx.as_ref())`, `state_dir = ctx.map(|c| c.roots.state_dir())` (the same
  `ctx.roots.state_dir()` expression the four action gates read, §4.1) and `progress = |p| { let _ = on_progress.send(p); }`
  (a send error dropped, as `download::channel_progress` does, `download.rs:1615-1616`); the error string is the
  serialized `ErrorView` JSON (the UI parses `code` from it, as it parses install errors today).
- `build.rs`: the `commands(&[…])` list gains `"manager_update_check", "manager_update_install"` (ten).
- `capabilities/main.json`: `"allow-manager-update-check"`, `"allow-manager-update-install"`; nothing else.
- `tauri.conf.json` gains exactly (D2):
  `"plugins": { "updater": { "pubkey": "dW50cnVzdGVk…eUoK" /* §1.1, full value */, "endpoints":
  ["https://krateaudio.com/manager/latest.json"], "requireSignedVersion": true, "allowDowngrades": false, "windows":
  { "installMode": "passive" } } }`. The `roster` scan reads `tauri*.conf.json` (`roster.rs:116-121`): the endpoint
  holds `krateaudio.com`, which the roster does not ban; the core literal ban (`injection.rs:23-31`) does not read
  conf files.
- `tauri.release.conf.json` = `{"bundle":{"createUpdaterArtifacts":true}}` (FR-003 (d)).
- `src-tauri/Cargo.toml`: `tauri-plugin-updater = { workspace = true }` under `[dependencies]` with the comment
  `# Phase 11 FR-001: self_update.rs only`; `[features]` gains `# Phase 11 FR-070: never default; only release-manager.yml's
  updater-e2e job enables it` and `updater-e2e = []`.

### 4.5 The `updater-e2e` module (FR-070, D10, PD-14, PD-26)

Inside `src-tauri/src/self_update.rs`, one `#[cfg(feature = "updater-e2e")] pub mod e2e { … }` holding the marker
`const MARKER: &str = "krate-e2e-marker-v1";`, `#[cfg(feature = "dev-feed")] compile_error!("updater-e2e and dev-feed
are never combined");` and:

```rust
const ENDPOINT_FEED: &str = env!("KRATE_E2E_FEED_URL");   // loopback versions.json (replaces FEED_URL)
const MARKER_DIR: &str = env!("KRATE_E2E_MARKER_DIR");
const OLD_VERSION: &str = env!("KRATE_E2E_OLD_VERSION");
pub fn override_feed(production_feed: String, production_http: HttpConfig) -> (String, HttpConfig); // drops both;
                                    // returns (ENDPOINT_FEED, HttpConfig::loopback(CONNECT_TIMEOUT, TOTAL_TIMEOUT))
pub fn start<R: Runtime>(app: &AppHandle<R>);              // from setup()
```

`start`: if `SharedState.lock.is_some()` write `<MARKER_DIR>/launched-<package_info().version>` whose **content is
`MARKER`** (`create_dir_all` first); if `package_info().version == OLD_VERSION`, `tauri::async_runtime::spawn` → `check`
(with `UpdateInputs { policy: UrlPolicy::LoopbackOnly, ..UpdateInputs::production(<the loopback config>) }`, the one
other `UpdateInputs` value in `src-tauri/src` and the only `LoopbackOnly` there, PD-36: the e2e build therefore runs
production's timeouts and `packaging()`, and the `deb` case proves the production packaging reading on a real `.deb`)
→ when `available` is set, whatever its `action`, `install(…, &NativeUpdateInstaller, …)`
(so on a `.deb` the e2e `deb` case proves PD-24's backstop, the call the UI never makes); any error →
`<MARKER_DIR>/error-<code>`, content `MARKER`; `available: None` → `error-E_SELF_UPDATE_NONE`. The marker is therefore
used at run time and stays in the e2e binary's data; the `updater-e2e` job asserts the content of every marker file it
reads equals `krate-e2e-marker-v1` and, in step `marker present`, that the old build's `krate-manager` executable (on
macOS the universal `Contents/MacOS/krate-manager`) holds the string at least once (`LC_ALL=C grep -a -o
'krate-e2e-marker-v1' <exe> | wc -l` ≥ 1), the positive control for FR-073's zero count on the release legs. `lib.rs`
calls into the module at exactly the two PD-26 sites; the existing `dev-feed` arms (`lib.rs:48-69`) are untouched. The
`env!` values exist only in the e2e job's environment; a build without them fails to compile, so the feature cannot be
turned on by accident elsewhere.

### 4.6 UI (`src/manager_update.ts`; FR-024 to FR-026; PD-31)

```ts
export interface ManagerUpdateView { os: OsName; current: string; available: AvailableView | null; error: ErrorView | null }
export interface AvailableView { version: string; date: string | null; notes: Block[]; action: UpdateAction }
export type UpdateAction = { kind: "install_and_restart" } | { kind: "open_release_page"; url: string }; // PD-31
export interface ErrorView { code: string; sentence: string; reason?: string; detail?: string }
export function toManagerUpdateView(value: unknown): ManagerUpdateView;      // strict, like toStatusView (view.ts:311)
export interface UpdateLine { element: HTMLElement; check(): Promise<void> }
export function createUpdateLine(invoke: Invoker, actions: { setEnabled(enabled: boolean): void },
  makeChannel?: ChannelMaker /* status.ts:145; default the Tauri Channel, a fake in tests */): UpdateLine;
export const UPDATE_TEXTS: Readonly<{ available: string; button: string; downloadDeb: string; confirm: string;
  confirmMac: string; downloading: string; installing: string; checkFailed: string; yes: string; no: string }>;
// status.ts (PD-31): the existing private releaseButton is exported with a label, default unchanged
export function releaseButton(url: string, invoke: Invoker, label?: string /* = STATUS_TEXTS.openReleasePage */): HTMLButtonElement;
```

**The action decides the button (PD-31, ruling P-3).** `toManagerUpdateView` accepts exactly the two `action` shapes
(an unknown `kind`, a missing or non-string `url` → the parser rejects, and the line shows nothing, as any parser
failure). `install_and_restart` → the "Update and restart" button and its confirmation (below). `open_release_page` →
one button `UPDATE_TEXTS.downloadDeb` = "Download the new .deb", made by `releaseButton(action.url, invoke,
UPDATE_TEXTS.downloadDeb)`: a click calls `invoke("open_release_page", { url })` once, with no confirmation, no
progress text and no `setEnabled(false)` (nothing is installed, plugin actions stay usable); a rejection shows the
existing `ACTION_TEXTS.openFailed` line, "Could not open the release page: {text}" with `rejectionText` (`status.ts:63`,
`:171`), which starts with the command's code (`E_OPEN_REJECTED` / `E_OPEN_FAILED`, `commands.rs:56`, `:58`). The UI
never calls `manager_update_install` for that view. The version line and the notes render the same for both actions.
vitest `offers the new .deb instead of an update on a system-package install`: a view with `os: "linux"` and
`open_release_page` → the "Download the new .deb" button, no "Update and restart" text anywhere, a click records
exactly `[["open_release_page", { url }]]` after the check call and never `manager_update_install`, `setEnabled` never
called. vitest `a failed .deb release-page open shows its code`: `open_release_page` rejects `"E_OPEN_REJECTED"` → the
line "Could not open the release page: E_OPEN_REJECTED" appears after the button, and the button is enabled again.

Texts exactly as FR-024 ("Krate Manager {version} is available.", "Update and restart", "Download the new .deb" (SI-31), the confirmation sentence plus,
when `os === "macos"` only, "macOS may ask for your administrator password.", "Downloading and verifying…", "Installing…
Krate Manager will restart.", "Could not check for a Krate Manager update ({code})."). Notes render through the existing
`notes.ts` block renderer (no HTML). The confirmation is an in-page dialog (two buttons), not `window.confirm`.
Progress (PD-25): on confirmation the line shows `downloading` and calls `invoke("manager_update_install", { onProgress:
makeChannel(onMessage) })`; `onMessage` parses `{ event: "downloading" | "installing" }` strictly and shows
`downloading` / `installing` (a message that fails the parser changes nothing, as the card progress does,
`status.ts:404-410`);
the text never moves back from `installing` to `downloading`. vitest `shows downloading then installing progress`
drives the fake channel with both events and asserts the two exact texts in order, and that a rejection after
`installing` shows the failure line.

Mount order (FR-024, FR-026): `renderShell` keeps `startup_error` → (Locked: notice, return; no check) → `app_version` →
`mountStatus`; `mountStatus` gains an `afterSettle` callback called once its first `plugin_status` settles (success or
rejection); `main.ts` passes `() => updateLine.check()`. `createStatusSection`'s `checkNow` calls `afterSettle` again after
`check_now` settles. `StatusSection` gains `setActionsEnabled(enabled: boolean)`: disables every card action button and
"Check now" (FR-025); the update line calls it with `false` on "Update and restart" confirmation and `true` on failure.
`main.ts` passes the mounted `StatusSection` itself as `createUpdateLine`'s `actions` argument. The wiring is tested
end to end in `src/main.test.ts` :: `disables plugin actions while the manager updates itself` (moved there from
`status.test.ts` in revision 4): `renderShell` with a mocked `invoke` whose `plugin_status` returns a view with one card
that has an action button, `manager_update_check` returns an `install_and_restart` view, and `manager_update_install`
returns a promise the test controls; the test clicks "Update and restart" and confirms, asserts every card action
button and "Check now" are `disabled`, then rejects `manager_update_install` and asserts they are enabled again. A
`main.ts` that passed a no-op or another object fails it. `status.test.ts` keeps `re-enables plugin actions after a
failed manager update` as the unit case of `setActionsEnabled(false)` / `(true)` (the vitest title count is unchanged).
A plugin action rejected with `E_INSTALL_SELF_UPDATING` shows through the existing failure line (FR-025).

### 4.7 The refusal harness (`tests/fixtures/updater-refusals/`; D9, FR-080 to FR-084, PD-11, PD-12, PD-30, PD-33)

`Cargo.toml`:
```toml
[package]  name = "updater-refusals"  edition = "2024"  publish = false
[dependencies]
krate-manager = { path = "../../../src-tauri" }
krate-manager-core = { path = "../../../crates/core", features = ["test-support"] }
tauri = { version = "2.12.5", features = ["test"] }
tauri-plugin-updater = { version = "=2.14.1", default-features = false, features = ["rustls-tls"] }
minisign = "0.10.0"
tempfile = "3.27.0"
serde_json = "1.0.154"
base64 = "0.23.1"
url = "2.5.8"
semver = "1.0.28"          # map-errors: Error::Semver          (all four already locked, §1.2)
http = "1.5.0"             # map-errors: Http, InvalidHeaderValue, InvalidHeaderName
minisign-verify = "0.3.0"  # map-errors: Error::Minisign
dirs = "7.0.0"             # src/confinement.rs only: the Windows known-folder snapshot (SI-29)
[build-dependencies]
tauri-winres = "0.3.7"
[lints.clippy]  allow_attributes = "deny"  allow_attributes_without_reason = "deny"
```
`Cargo.lock` committed (generated with `cargo generate-lockfile` then trimmed to the workspace lock's versions with
`cargo update -p <crate> --precise <v>` for every shared crate; the build stage records the command list).

`src/main.rs` (parent): parse `[--plant committed-key | --plant bind-any] | [--scenario <name> [--plant
committed-key]]`; parent mode: (1) on Windows, `confinement::snapshot()` (`src/confinement.rs`, the only file naming
`dirs::`): the entry names, read-only, of `dirs::data_local_dir()/com.krateaudio.krate-manager` and
`dirs::data_dir()/com.krateaudio.krate-manager` (absent = empty; SI-29); (2) `TempDir::new()`; per scenario a subdir with
`home/`, `tmp/`, `local/`, `roaming/`, `xdg-{config,data,cache,state}/`, `run/`; (3) **plant mode** (`--plant
committed-key` or `--plant bind-any`, PD-38): spawn exactly one child, `--scenario good --plant <name>`, and no other
process; wait for it; print its first stdout line and exit 1 (a first line other than the plant's code, or a child exit
0, prints `not ok plant <name>: <line>` and exits 1); no other scenario runs, so "before any scenario runs" (FR-081 (f))
holds by construction, and the child itself refuses before any of its servers, relay or mock app exist (its key check
precedes every bind; under `bind-any` its first bind is the refused `0.0.0.0:0`); (4) **normal mode**: spawn every
scenario child with `Command::new(current_exe).arg("--scenario").arg(name).envs(redirected)`, all at once; wait for all;
collect stdout lines, sort by name, print, then `ok confinement` / `not ok confinement <path>`
(FR-081 (e): walk each child's redirected dirs and the recorded-writes file each child appends to, every path must start
with the canonical temp dir; on Windows also `confinement::snapshot()` again, every entry not in the first snapshot is
`not ok confinement <path>`), then `harness: <n> ok, <m> not ok`; exit 0 iff `m == 0 && n == 27` (SI-23, SI-30, SI-35).
Wall clock printed (`harness: wall <ms> ms`). A child that exits non-zero without a line is `not ok <name> exit <code>`;
a normal-mode child that prints `E_HARNESS_KEY` / `E_HARNESS_BIND` is `not ok <name> <code>`.

Child (`--scenario`): make every key the scenario uses through one function, `keys::scenario_key(plant) -> ScenarioKey
{ public_box_text, signer: Option<SecretKey> }` (`keys.rs`: `KeyPair::generate_unencrypted_keypair()`; under `--plant
committed-key` it returns the committed `krate-signing.pub` text, read at compile time with `include_str!`, and no
signer), and pass each result to `keys::refuse_committed(&key)` **immediately, before any fixture server starts**: equal
second (base64 key) lines, compared trimmed, print `E_HARNESS_KEY: the harness key is the committed release key` and
exit 1 (FR-081 (d), in every child and for `other-key`'s second key too; `rsign-format`'s fixture key goes through
the same `refuse_committed`). Signing: `sign(Some(&pk), &sk,
Cursor::new(bytes), Some(tc), Some("harness"))` with `tc = "timestamp:0\tfile:pkg\tversion:<v>"` or, for `unversioned`,
without the `version:` field; signature = base64 of `SignatureBox::to_string()`, pubkey = base64 of
`pk.to_box()?.to_string()`, the Tauri forms of §1.2. Then start the scenario's fixture servers through
`bind_loopback(addr) -> Result<FixtureServer, HarnessError>` (refuses non-loopback → `E_HARNESS_BIND`; the
`FixtureServer::start` it wraps binds `127.0.0.1:0`; the SOCKS relay of `socks-proxy` binds through
`bind_loopback_listener(addr) -> Result<TcpListener, HarnessError>`, the same address check, PD-30); start the **trap proxy** (a second `FixtureServer` with no routes);
build `mock_builder().plugin(tauri_plugin_updater::Builder::new().build()).build(ctx)` where `ctx =
mock_context(noop_assets())` with `ctx.config_mut().plugins` set to `{"updater": {"pubkey": <throwaway>, "endpoints":
[<fixture>/latest.json], "requireSignedVersion": true, "allowDowngrades": false, "dangerousInsecureTransportProtocol":
true}}` and `ctx.package_info_mut().version` = the scenario's current version (`0.1.0` unless stated); `inputs =
UpdateInputs { policy: LoopbackOnly, http: HttpConfig::loopback(..).with_proxy(Some(ProxyConfig { url: trap, no_proxy:
vec!["127.0.0.1".into()] })), timeouts: UpdateTimeouts { stall: <1 s for stall>, ..UpdateTimeouts::production() },
packaging: <SystemPackage for deb-install, else SelfContained> }`; run the scenario through
`krate_manager_lib::self_update::{check, install, prepare_download}` with `tauri::async_runtime::block_on`, passing a
`progress` closure that records each `SelfUpdateProgress`; print `ok <name>` or `not ok <name> <reason>`. Installers:
`PanickingInstaller` (default; `panic!` → child exits 101 → parent prints `not ok <name> installer reached`),
`RefusingStub` (records the call, returns `Install { detail: "harness" }`; used by `recheck-replaces`,
`failed-install-clears` and `busy`). Neither calls `Update::install`, `download_and_install` or any restart, and no
harness file names `NativeUpdateInstaller` (`harness_source_findings`, §4.12).

Assertions the scenario table gains (SI-23):
- `good`: `check` → `Some` with `available.action == InstallAndRestart` (PD-31); take the kept update,
  `prepare_download(&mut u, &inputs)`, assert `u.timeout == Some(DOWNLOAD_TIMEOUT)` and `u.no_proxy` (loopback is on
  the bypass list); `download`; `verify` → Ok.
- `failed-install-clears`: progress events of the first install are exactly `[Downloading, Installing]`.
- `busy`: with the guard held, install → `E_SELF_UPDATE_BUSY`, zero package requests; the guard released, a second
  install **with no new check** → `E_SELF_UPDATE_INSTALL` from the refusing stub after exactly one package request (the
  FR-015 refusal kept the update, PD-28).
- `busy-after-download`: the watcher thread, on seeing the package request and before taking the guard, asserts
  `self_update::in_flight(state_dir)` and `action_gate(state_dir) == Err(SelfUpdating)` for the very `state_dir` passed
  to `install` (a mismatch or a missing guard is `not ok busy-after-download in-flight not set`); progress events are
  exactly `[Downloading]`; afterwards `!in_flight(state_dir)`.
- `stall`: the same in-flight assertion while the route stalls; after the error, `!in_flight(state_dir)`.
- `endpoint-latest-path`: the mock config's endpoint is `http://127.0.0.1:<port>/o/r/releases/latest/download/latest.json`;
  `check` → view error `E_SELF_UPDATE_URL { LatestPath }`; zero requests on the fixture server and the trap proxy.
- `redirect-loop`: the package route is `Reply::Redirect { status: 302, location: <the same path> }`; `download` →
  `E_SELF_UPDATE_URL { Policy }` after exactly 6 requests to that path (`MAX_REDIRECTS + 1`); zero on the trap proxy.
- `deb-install`: `packaging: SystemPackage`; `check` → `Some` with `available.action == OpenReleasePage { url }`,
  `url == format!("https://{}/{}/releases/tag/manager/v0.2.0", status::RELEASE_PAGE_HOST, MANAGER_REPO)` and
  `status::release_page_url(&url) == Some(url)` (PD-31, ruling P-3); install (the backstop, called directly) →
  `E_SELF_UPDATE_UNSUPPORTED`, zero package requests, the panicking installer never reached.
- `socks-proxy` (PD-30, SI-30, ruling P-1): two runs in one child, first with `ProxyConfig { url:
  "socks5://127.0.0.1:<relay port>", no_proxy: vec![] }`, then with `socks4://`; each: `check` → `Some`;
  `prepare_download` → `u.proxy == Some(<relay url>)` and `!u.no_proxy`; `download`; `verify` → Ok; the relay recorded
  at least one CONNECT per fetch, every one with that run's version byte (5, then 4) to the fixture server's
  `127.0.0.1:<port>` and no other target, and no non-SOCKS first byte (an HTTP request reaching the relay, which is what
  a build without the `socks` feature sends, `connect.rs:783-792`, is `not ok socks-proxy not socks`); the fixture server
  recorded `GET /latest.json` and the package GET once per run; the trap proxy zero; the panicking installer never
  reached. **The relay** (`src/socks.rs`, std only, no new dependency): `SocksRelay::start(listener)` over a
  `bind_loopback_listener` listener, one thread per connection; SOCKS5: greeting must offer method `00` (reply `05 00`,
  else `05 FF` and close), request `05 01 00 01 <ipv4> <port>` (CONNECT, IPv4 only); SOCKS4: `04 01 <port> <ipv4>
  <userid> 00`; a target other than `127.0.0.1` is answered with the refusal reply (`05 02` / `5B`) and recorded as
  refused, which fails the scenario; an accepted target is dialled with `TcpStream::connect` (the only dial in the
  harness, T013's source rule), the success reply sent, then bytes are copied both ways (`std::io::copy`, one thread per
  direction) until either side closes; `connects() -> Vec<SocksConnect { version: u8, target: SocketAddr }>` and
  `refused()` for the assertions. Its `Drop` stops the accept loop as `FixtureServer` does.
- `map-errors` (`src/map_errors.rs`, no server, no mock app): a table of 31 `(constructed tauri_plugin_updater::Error,
  expected code, expected reason)` rows, every variant of §1.2 but `Reqwest`: the 15 unit variants; `TargetNotFound("x")`,
  `TargetsNotFound(vec![])`, `Network("x")`, `SignatureUtf8("x")`, `SignedVersionMismatch { signed, announced }`,
  `PreviousAppNotRestored(PathBuf)`; `Io(io::Error::other("x"))`; `Serialization(serde_json::from_str::<Value>("{")
  .unwrap_err())`; `UrlParse(Url::parse("").unwrap_err())`; `Base64(<base64 0.23 decode of "!">)`;
  `Semver(semver::Version::parse("x").unwrap_err())`; `Minisign(minisign_verify::PublicKey::from_base64("x").unwrap_err())`;
  `Http(http::Request::builder().uri("\u{0}").body(()).unwrap_err())`; `InvalidHeaderValue(HeaderValue::from_str("\n")
  .unwrap_err())`; `InvalidHeaderName(HeaderName::from_bytes(b" ").unwrap_err())`; `Tauri(tauri::Error::UnknownPath)`.
  Each is mapped with `map_error` and compared with §3.2's row; it prints `ok map-errors` only when all 31 match and the
  table has 31 rows, else `not ok map-errors <variant> → <got>`. The constructors are the build stage's to confirm against
  the locked crates (a constructor that does not compile is fixed, not dropped: the row count is part of the assertion).
- `rsign-format` (PD-35, SI-34): the good path with a signature `rsign2` made, the CLI the `sign` job runs. Inputs are
  the committed fixture `fixtures/rsign/` (bytes compiled in with `include_bytes!`): `package.bin` (fixed bytes),
  `package.bin.minisig` (`rsign sign -W` output over it with trusted comment `timestamp:0\tfile:package.bin\tversion:0.2.0`,
  untrusted comment as rsign writes it) and `rsign.pub` (the throwaway public key, as `rsign generate -W` writes it). The
  child passes the fixture key through `keys::refuse_committed` first; the mock config's `pubkey` is base64 of
  `rsign.pub`'s bytes and `latest.json`'s `signature` is base64 of `package.bin.minisig`'s bytes, exactly the `base64
  -w0` framing of the `sign updater artifacts` step (§4.10); version `0.2.0`, current `0.1.0`. Asserts `check` → `Some`,
  `download` (which verifies) and `verify` → Ok through `minisign-verify` / `verify_signed_version`, one package GET,
  the trap proxy zero, the panicking installer never reached. The fixture is made once by
  `tests/fixtures/updater-refusals/make-rsign-fixture.js` (Node, as `make-kat.js` made the KAT, §1.1): it refuses to run
  when any output exists, runs `rsign generate -W` into an `fs.mkdtemp` dir, signs, copies the public key and the
  signature out, deletes the temp dir with the secret key, and prints every command with its output for the record
  (`rsign --version` = `rsign2 0.6.7`). The secret key is never written under the repository; the files are byte-exact
  (`.gitattributes` `apps/krate-manager/tests/fixtures/updater-refusals/fixtures/** -text`). The KAT fixture cannot serve:
  its trusted comment has no `version:` field (§1.1), which `requireSignedVersion` refuses.
- `production-inputs` (PD-36, SI-35; no server, no mock app): `UpdateInputs::production(HttpConfig::production())` →
  `policy == UrlPolicy::HttpsOnly`, `timeouts == UpdateTimeouts::production()`, `packaging == packaging()`; `not ok
  production-inputs <field>` on any difference.
`busy` / `busy-after-download` pass their own `test_support::FakeElevation::refusing()` (`test_support.rs:417-450`: each
fake owns its own `ElevationGuard`) and hold its guard through a new `test-support`-only seam, `impl ElevationGuard {
#[cfg(any(test, feature = "test-support"))] pub fn hold_for_test(&self) -> GuardHeldForTest<'_> }` (RAII; sets `Hold::Running`,
`Drop` restores `Free`), because `ElevationGuard::enter` is private (`install.rs:617`) and no public way to hold a guard
exists today. `busy-after-download`: the package route is `Reply::Delayed { after: 200 ms, reply: Ok(pkg) }` and a
watcher thread takes `hold_for_test()` as soon as `requests()` shows the package request.

Scenario fixtures: `latest.json` built in the child as Tauri's static format (`version`, `notes`, `pub_date`,
`platforms: { "<os>-<arch>": { url, signature } }`, the target key `tauri_plugin_updater::target()` returns). Every
package URL has the per-tag path the release uses, `<fixture>/<MANAGER_REPO>/releases/download/manager/v<version>/<asset>`
with `<version>` the version `latest.json` announces (PD-27 refuses any other repository or tag under `LoopbackOnly`
too, so every scenario that downloads depends on this path). The
scenario table is FR-080's, assertions as stated there, with request counts from `FixtureServer::requests()` of the
package server and of the trap proxy.

`build.rs` (PD-12):
```rust
fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {   // build script: env read is cargo's API
        tauri_winres::WindowsResource::new().set_manifest(MANIFEST).compile().expect("manifest");
    }
}
const MANIFEST: &str = r#"<assembly …>…Microsoft.Windows.Common-Controls 6.0.0.0 …</assembly>"#; // byte copy of tauri-build 2.7.4's
```
`std::env::var` in a build script is disallowed by `clippy.toml`; the call carries `#[expect(clippy::disallowed_methods,
reason = "a build script reads cargo's CARGO_CFG_TARGET_OS")]`. The guard counting expectation sites
(`disallowed_methods_expectations_are_the_seven_d6_sites`) scans workspace sources only (`injection.rs:1612-1629`) and
is unaffected.

**First local run is the D9 measurement (PD-33, ruling P-5: no stop):** `cargo run --manifest-path
tests/fixtures/updater-refusals/Cargo.toml --locked` on this box, its outcome written to
`specs/krate-manager-phase11/evidence/d9-windows.txt` (`D9 WINDOWS LOADS`, or `D9 WINDOWS LOAD FAILURE exit=<code>
<loader message>` for a `0xc0000139` / `STATUS_ENTRYPOINT_NOT_FOUND` exit or any loader failure before the first output
line). A failure does not stop the build: the harness stays formatted and linted on Windows, and its run and plants go
to WSL (`harness-lane.js`, §7.4, after R-17) and to `check (macos-latest)` / `check (ubuntu-24.04)` (§4.11).

### 4.8 `scripts/bundle.js` release mode (FR-030 to FR-033, PD-15, PD-20)

```js
export function parseArgs(argv, platform) // → { release: boolean, target?: 'universal-apple-darwin', bundles?: string[], features?: 'updater-e2e' } or throws UsageError (exit 2)
export function releaseTauriArgs(platform, opts, extraConfigs) // → string[]
export function universalSidecars() // → ['krate-manager-helper-aarch64-apple-darwin', '…-x86_64-apple-darwin', '…-universal-apple-darwin']
export function assetTable(platform, version, opts) // → [{ dir, glob, name }] (D6 names, PD-15)
export function extraConfigs(env) // KRATE_BUNDLE_EXTRA_CONFIG split on path.delimiter, each must be an existing file
export function resolveAsset(listing, glob) // listing: file names of the row's dir → the one match, or throws E_BUNDLE_ASSET <glob> matched <n>
```
`copyAssets` lists each row's dir and calls `resolveAsset`, so FR-032's refusal is a pure function: `bundle release
asset table` gains three cases per OS row kind (zero matches → `E_BUNDLE_ASSET` naming the glob and `matched 0`; two
matches → `matched 2`; one match → `{ from, to }` with the D6 name). The asset table has no `.deb.sig` row (SI-21): the
CLI still writes one (deb is an updater-enabled bundle type, tauri-cli 2.12.4 `bundle.rs:247-260`), and it is not copied.

No arguments: today's code path unchanged (`tauriArgs`, `bundle.js:36-38`). `--release`: Windows `['build', '--config',
'src-tauri/tauri.helper.conf.json', '--config', 'src-tauri/tauri.release.conf.json', …extra, …features]`; macOS with
`--target universal-apple-darwin` the same plus `'--target', 'universal-apple-darwin'`, helper built twice
(`cargo build -p krate-manager-helper --release --locked --target <t>`), staged under the three names, the universal one
by `lipo -create -output`; Linux `['build', '--config', 'src-tauri/tauri.release.conf.json', '--bundles',
'appimage,deb', …]`. Then `copyAssets` to `target/release-assets/` per `assetTable` (one match per glob or exit 1
`bundle: E_BUNDLE_ASSET <glob> matched <n>`). Refusals (exit 2): unknown argument, `--target` off macOS or with another
value, `--bundles` off Linux, `--features` with any value but `updater-e2e`, any option without `--release`.

### 4.9 Node tools

**`tools/gen-manager-latest.js`** (FR-040 to FR-042, PD-21):
`node tools/gen-manager-latest.js --version <v> --tag manager/v<v> --repo <owner/repo> --notes <file> --pub-date <RFC 3339>
--assets <dir> --pubkey <file> --out <file> [--check-only] [--repo-url-base <http://127.0.0.1:N> --only <os>]`. Exports
`main(argv, io)`, `build({version, tag, repo, notes, pubDate, assets, pubkeyText, urlBase, only})` → `{ json, text }`,
`ASSET_TABLE`. Steps: assets dir must hold exactly the D6 names for the selected OSs (else `E_LATEST_ASSETS` naming
missing / extra); per `.sig`: text trimmed of one trailing LF, base64-decoded to UTF-8, `minisign.verifyBuffer(assetBytes,
decoded, pubkeyText)` (key id, signature, global signature), `trustedComment` split on `\t` must hold exactly one
`version:` field equal to `<v>` (else `E_LATEST_SIGNATURE <asset>: <why>`); output keys sorted bytewise, `JSON.stringify(doc,
null, 2) + "\n"`; written atomically (temp file + rename) only after every check; `--check-only` writes nothing.
`--repo-url-base` requires `--only` and vice versa, and an `http://127.0.0.1:<port>` value (else exit 2).
`--platform-subset <os>` (SI-19) is allowed only with `--check-only` and restricts the asset check to that OS's D6 rows
(a build leg holds only its own assets, signed with its own throwaway key, PD-23). `ASSET_TABLE` is D6 as amended by
SI-21: seven `latest.json` keys, no `.deb.sig`, the `.deb` itself a release asset that `latest.json` does not name. Usage line
`usage: node tools/gen-manager-latest.js …`. No `child_process`, no network module (hygiene offline scan, PD-16).

**`tools/manager-release-preflight.js`** (FR-051, FR-063, D15):
`node tools/manager-release-preflight.js --app-dir <dir> --dry-run <true|false> --tags <file> --repo <owner/repo> [--notes
<out>] [--body <out>] [--github-output <file>]`. Exports `main`, `preflight(opts) -> {version, tag, notes, body, warnings}`,
`releaseBody({version, repo, notes})`, `changelogSection(text, version)`. Checks in order, each a named code, exit 1:
`E_PREFLIGHT_VERSION` (`lint-manager-version.js`'s exported `lint(root)` has errors), `E_PREFLIGHT_UPDATER_CONFIG`
(`tauri.conf.json` `plugins.updater` differs from D2's object with the pubkey computed from the repo's
`tools/feed-contract/keys/krate-signing.pub`), `E_PREFLIGHT_CHANGELOG` (no `## [<v>] - …` heading, or more than one),
`E_PREFLIGHT_CHANGELOG_UNDATED` (heading `Unreleased` on a real run; a dry run prints `::warning` and goes on),
`E_PREFLIGHT_TAG_LIST` (the tags file's release list has ≥ 1000 lines), `E_PREFLIGHT_REPO` (PD-27, SI-40; both kinds of
run: `--repo` differs from the string of the one `pub const MANAGER_REPO: &str = "…";` line of
`<app-dir>/crates/core/src/self_update.rs`, or that line is missing or not unique), `E_PREFLIGHT_TAG_EXISTS` (`manager/v<v>` among
the remote tags or release tags; both kinds of run), `E_SIGNING_CONFIG` (real run only: `refuseTestKey` of
`gen-release-manifest.js`, reused, §1.1). Tags file format: two sections, `# tags` (one `refs/tags/<name>` per line, from
`git ls-remote --tags origin`, `^{}` lines dropped) and `# releases` (one tag name per line). `changelogSection` returns
the lines after the heading up to the next `## [` (exclusive), trimmed of leading / trailing blank lines (FR-044).
`releaseBody` = section + `\n\n## Install\n\n` + the fixed per-OS block naming the D6 assets and the macOS one-liner with
"(provisional: see the Krate Manager first-launch notes when they are published)" (FR-063). Writes `version=`, `tag=`
to `--github-output`.

**`tools/feed-publish.js`** (FR-043, PD-17): `checkLatestJson(bytes, tag, version, repo)` adds, after `contract.checkUrl`:
`const prefix = \`https://github.com/${repo}/releases/download/${tag}/\``; `entry.url.startsWith(prefix)` and the rest is
one non-empty segment without `/`, `?`, `#` (else `bad(… is not a per-tag release asset URL of ${repo})`). Exported.

**`apps/krate-manager/scripts/e2e-update-server.js`** (FR-071): `node scripts/e2e-update-server.js --dir <dir> --port-file
<file> --log <file>`; binds `127.0.0.1:0` only (refuses any `--host`), writes the port, serves `GET /latest.json`,
`GET /versions.json` and `GET /<owner>/<repo>/releases/download/<tag>/<name>` mapped to `<dir>/<name>` (one segment, a
file directly in `<dir>`), 404 otherwise; appends `<method> <path> <status> <bytes>` per request to `--log`. Tested by
`scripts/e2e-update-server.test.js` (`node --test`, `manager-ci`'s `node --test` step, `manager-ci.yml:181-188`).

### 4.10 `release-manager.yml` (FR-050 to FR-078, D14, PD-19 to PD-22)

Top: `on: workflow_dispatch: inputs: dry_run: {type: boolean, default: true}`; `permissions: contents: read`;
`concurrency: {group: krate-manager-release, cancel-in-progress: false}`; `defaults.run.shell: bash`. Jobs:

| Job | Runs on | needs | environment / permissions | Steps (named; every assertion prints `::notice` / `::error` naming the leg) |
|---|---|---|---|---|
| `preflight` | `ubuntu-24.04` | — | none; `contents: read` | checkout (`fetch-depth: 1`); Node 24; `tags file` (`git ls-remote --tags origin`, `gh release list --limit 1000 --json tagName`, `GH_TOKEN: ${{ github.token }}`); `preflight` (the tool, `--repo "${{ github.repository }}"`, so `E_PREFLIGHT_REPO` ties the run's repository to `MANAGER_REPO` before any build, PD-27; `--github-output "$GITHUB_OUTPUT"`); upload `release-notes` (notes + body). No key of any kind. Outputs `version`, `tag`. |
| `main-only` | `ubuntu-24.04` | `preflight` | `environment: signing`; `contents: read` | one step `echo "::notice title=main-only::environment signing admitted ${{ github.ref }}"`; no checkout, no secret reference. It keeps D14's "dry runs start only from `main`" (the environment's main-only rule stops a branch dispatch here, before any build) now that `build` carries no environment (PD-23). |
| `build` (matrix `windows-latest`, `macos-latest`, `linux`) | `linux` → `ubuntu-24.04` + `container: ubuntu:22.04` | `preflight`, `main-only` | **none**; `contents: read`; no `secrets.*` anywhere in the job | checkout; container packages (D12 list + `build-essential curl file binutils ca-certificates git`); Node 24; Rust (`rustup` from `rust-toolchain.toml`; macOS `rustup target add aarch64-apple-darwin x86_64-apple-darwin`); `npm ci`; `extra config empty` (every run: `KRATE_BUNDLE_EXTRA_CONFIG` unset or empty, so the app is built with the committed pubkey on dry and real runs alike); `bundle` (one step, the same on both kinds of run: `PW=$(openssl rand -hex 16)`; `echo "::add-mask::$PW"`; `npx tauri signer generate --ci -p "$PW" -w "$RUNNER_TEMP/leg/k.key"`; then `TAURI_SIGNING_PRIVATE_KEY="$RUNNER_TEMP/leg/k.key"` (a path, §1.2) `TAURI_SIGNING_PRIVATE_KEY_PASSWORD="$PW" node scripts/bundle.js --release [--target universal-apple-darwin \| --bundles appimage,deb] 2>&1 \| tee build.txt`; the password never leaves the step: no step output, no file; on `linux` the step `env:` holds `NO_STRIP: 'true'`, PD-34); `leg key is not the release key` (the ANSI-stripped `build.txt` holds the CLI's "does not match the public key from `plugins > updater > pubkey`" warning, tauri-cli 2.12.4 `bundle.rs:313-318`, at least once and exactly as often as the `.sig` files the CLI wrote under the bundle dirs; the positive evidence that the leg signed with a key other than the committed one); `updater-e2e absent`, `ci-temp-roots absent` (FR-056, SI-10); `build warnings` (FR-053, the `manager-ci.yml:246-259` rule copied unchanged: zero `^warning:` lines, no exemption; the CLI prints the mismatch `log::warn!` as `        Warn The updater secret key …`, `tauri-cli` 2.12.4 `lib.rs:246-262`, `:310-318`, §1.2, which `^warning:` never matches, so the throwaway-key design needs no carve-out; any `warning:` line on the first dry run, from that or from anything else, is red and surfaced); `signatures` (`gen-manager-latest.js --check-only --platform-subset <os> --pubkey <the leg's k.key.pub, base64-decoded>`: the CLI's `.sig` form and `version:` field); `artifact version`; macOS: `lipo`, `plist`, `codesign` (FR-054); Linux: `glibc floor` (FR-065); upload `release-assets-<os>` **without any `.sig`** (`path:` `target/release-assets/` plus `!target/release-assets/*.sig`; macOS adds `Krate-Manager-app.zip`, the universal `.app` as `ditto -c -k --keepParent`, for `package-macos`; it is never a release asset). |
| `sign` | `ubuntu-24.04` | `preflight`, `build` | `environment: signing`; `contents: read`; `if: ${{ !inputs.dry_run }}` | checkout (`sparse-checkout: tools`, no app source); Node 24; `npm ci --prefix tools/feed-contract --ignore-scripts` (`ajv` only, which `minisign.js` loads through `contract.js`); `install rsign2` (PD-35: `cargo install rsign2 --version 0.6.7 --locked`, compiled fresh, then `test "$(rsign --version)" = "rsign2 0.6.7"`; **no `actions/cache` restore**, unlike `release.yml:610-624`, because that cache key is also written by `feed-tools.yml` after an `npm ci` with lifecycle scripts on, §1.1; no `Swatinem/rust-cache` either); download `release-assets-windows`, `-macos`, `-linux` into `assets/`; `updater asset list` (step `env:` `VERSION: ${{ needs.preflight.outputs.version }}`, `ONLY: all`; `node -e` over `require('./tools/gen-manager-latest.js').ASSET_TABLE` writes the names of the rows that carry a `.sig`, for `$ONLY`, one per line, to `$RUNNER_TEMP/updater-assets.txt`, and asserts the count: 3 for `all`, 1 for one OS); `signing key` (the only `secrets.*` references of the workflow: `KRATE_SIGNING_KEY: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}`, `KRATE_SIGNING_KEY_PASSWORD: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD }}`; `umask 077`; `node tools/minisign.js decrypt-key --out "$RUNNER_TEMP/krate.key"`; `cp tools/feed-contract/keys/krate-signing.pub "$RUNNER_TEMP/krate.pub"`); `sign updater artifacts` (step `env:` `VERSION`, `ASSET_DIR: assets`; for each `<name>` of `$RUNNER_TEMP/updater-assets.txt`: `rsign sign -W -s "$RUNNER_TEMP/krate.key" -t "timestamp:$(date +%s)"$'\t'"file:$name"$'\t'"version:$VERSION" -x "$RUNNER_TEMP/$name.minisig" "$ASSET_DIR/$name" < /dev/null`, then `base64 -w0 < "$RUNNER_TEMP/$name.minisig" > "$ASSET_DIR/$name.sig"`, Tauri's `.sig` form, §1.2; the `run:` text is the one `updater-e2e` copies, PD-35); `delete signing key (after signing)` (`rm -f "$RUNNER_TEMP/krate.key"`; nothing that runs third-party code follows); `verify` (`gen-manager-latest.js --check-only --pubkey "$RUNNER_TEMP/krate.pub"` over all three OSs' assets, plus `rsign verify -p … -x …` per asset); upload `release-sigs` (the three `.sig` files only). |
| `sign-dry` | `ubuntu-24.04` | `preflight`, `build` | none; `contents: read`; `if: ${{ inputs.dry_run }}` | the `sign` steps, byte-equal but for the key step (the shape test asserts the two step lists equal except `signing key`): `throwaway signing key` = `rsign generate -W -p "$RUNNER_TEMP/krate.pub" -s "$RUNNER_TEMP/krate.key" < /dev/null` (`release.yml:636`), no `secrets.*`; uploads `release-sigs` and `dry-signing.pub` (the public key only; no private key or password is in any artifact). No `actions/cache` / `Swatinem/rust-cache` (PD-35, shape-tested with a plant). |
| `package-macos` | `macos-latest` | `preflight`, `build` | none; `contents: read` | download `release-assets-macos` (it carries the bundled `Krate Manager.app` as a `ditto -c -k --keepParent` zip made by the build leg, because `upload-artifact` drops modes and symlinks, `ci.yml:960-969` per roadmap line 210); `ditto -x -k` it into a temp dir, then `ditto` the `.app` into `stage/Applications/` (keeps the ad-hoc signature and modes); `pkgbuild --analyze --root stage comp.plist`; `plutil -replace 0.BundleIsRelocatable -bool NO comp.plist` (and assert the key reads `false`); `pkgbuild --root stage --component-plist comp.plist --identifier com.krateaudio.krate-manager --version <v> --install-location / Krate-Manager-<v>-macOS.pkg`; `payload` (`pkgutil --payload-files` all under `./Applications/Krate Manager.app`); `pkg facts` (`pkgutil --expand-full` into a temp dir, `machoFacts` of both executables, `PackageInfo` printed into the job summary and parsed, not substring-matched: `xmllint --xpath 'count(//relocate/*)' PackageInfo` (`/usr/bin/xmllint`, part of macOS) must print `0`, i.e. no `relocate` element or an empty one, any child of any attribute form red; the notice says which of the two it found, `count(//relocate)`; SC-021 as reworded by SI-37); upload `release-pkg-macos` and `packageinfo` (the expanded `PackageInfo`; the first dry run's copy becomes `specs/krate-manager-phase11/evidence/packageinfo-dry-run.xml` at compliance, the named fixture SC-021 asks for, which no local run can make: FR-090 bars `pkgbuild` here). |
| `updater-e2e` (matrix `windows-latest`, `macos-latest`, `ubuntu-24.04`) | as matrix | `preflight` | none; `contents: read` | checkout; toolchains; Linux: manager-ci's apt line + `libfuse2t64 xvfb xauth`; `npm ci` (and `npm ci --prefix tools/feed-contract` for `gen-manager-latest.js`); `install rsign2` and `throwaway signing key` (PD-35: byte-equal copies of `sign-dry`'s two steps; the rsign key pair in `$RUNNER_TEMP/krate.{key,pub}` is this job's release-signing key, never shared with another job, FR-071); `server` (start `e2e-update-server.js` in the background, wait for the port file); `build old` (`KRATE_E2E_*` env, `--config` JSON with the old version, `pubkey` = `base64 -w0 < "$RUNNER_TEMP/krate.pub"` (the rsign key, so the old app trusts what `sign updater artifacts` makes), `endpoints: ["http://127.0.0.1:<port>/latest.json"]`, `dangerousInsecureTransportProtocol: true`; the build itself runs the `build` legs' `bundle` form, a per-build `npx tauri signer generate --ci` key passed as a path, because the CLI refuses to bundle updater artifacts without a private key (§1.2), and on Linux `NO_STRIP: 'true'` (PD-34); `node scripts/bundle.js --release --features updater-e2e [--target …]`; keep the old artifacts aside); `marker present` (FR-073's positive control, §4.5: the old build's `krate-manager` executable holds `krate-e2e-marker-v1` at least once); `build new` (same with patch + 1); `updater asset list` (copied from `sign`, step `env:` `ONLY: <os>`, `VERSION: <new>`), `sign updater artifacts` (copied, `ASSET_DIR` = the new build's `target/release-assets`; overwrites the CLI's `.sig` of the new updater asset with the rsign one) and `delete signing key (after signing)` (copied); `latest.json` (`gen-manager-latest.js --only <os> --repo-url-base http://127.0.0.1:<port> --repo ${{ github.repository }} --pubkey "$RUNNER_TEMP/krate.pub" …` over the new assets and their rsign `.sig`s into the server dir; `preflight`'s `E_PREFLIGHT_REPO` has already tied `github.repository` to `MANAGER_REPO`, so the URLs pass PD-27); per-leg install/launch/assert cases (FR-072, below); `annotate` (FR-074); `request log` uploaded. |
| `first-install` (matrix `windows-latest`, `macos-latest`, `ubuntu-24.04`, `ubuntu-22.04-container`) | the last → `ubuntu-24.04` + `container: ubuntu:22.04` | `build`, `package-macos` | none; `contents: read` | download artifacts (`actions/download-artifact` only); `block live host` (FR-078) right before `launch`; install; assert; `launch` (12 s, process presence only). |
| `publish` | `ubuntu-24.04` | `preflight`, `build`, `sign`, `package-macos`, `updater-e2e`, `first-install` | `environment: signing`; `contents: write`; `if: ${{ !inputs.dry_run }}`; no `secrets.*` (FR-055) | download all artifacts + `release-sigs` + `release-notes`; `gh release create manager/v<v> --draft --latest=false --title … --notes-file body.md --target ${{ github.sha }}`; `gh release upload` (no `--clobber`); `gen-manager-latest.js` (committed key) → upload `latest.json`; `gh release download` into a fresh dir and compare every SHA-256; `gh release edit manager/v<v> --draft=false --latest=false`. |
| `dry-publish` | `ubuntu-24.04` | `preflight`, `build`, `sign-dry`, `package-macos`, `updater-e2e`, `first-install` | none; `contents: read`; `if: ${{ inputs.dry_run }}` | download all artifacts + `release-sigs` + `dry-signing.pub`; `gen-manager-latest.js` over the three legs' assets with `--tag manager/v<v>` and `--pubkey dry-signing.pub`; `sigs not committed key` (SC-019's negative half: for every `.sig`, `base64 -d` into a temp `.minisig`, `node tools/minisign.js verify -p tools/feed-contract/keys/krate-signing.pub -x <tmp> <asset>` must exit 1 with stderr `minisign: E_MINISIG_KEY_ID` (`minisign.js:186`); any other outcome is `::error`; a `::notice` names the count, 3); `checkLatestJson` (`node -e` requiring `tools/feed-publish.js`); upload `manager-release-dry-run`. |
| `feed` / `feed-dry` | `uses: ./.github/workflows/feed.yml` | `publish` / `dry-publish` | `contents: write` both (PD-19) | `with: dry_run: false` / `true` |

**Where keys exist (PD-23, SI-20).** Each `build` leg's throwaway key and its password exist only inside that leg's
`bundle` step and `$RUNNER_TEMP`; they sign signatures nobody publishes (uploads exclude `*.sig`) and the built app
trusts only the committed key. The real key exists only between `sign`'s `signing key` and `delete signing key` steps,
in a job that has no app source, has run only `npm ci --ignore-scripts` of `ajv` and a fresh, uncached `cargo install
rsign2 --version 0.6.7 --locked` before it, and runs only `node tools/minisign.js decrypt-key` and `rsign sign` while it
exists: the plugin pipeline's model (`release.yml:607-655`) without its cache restore (PD-35). The `rsign sign` /
`base64 -w0` path that makes the release `.sig`s is the one `updater-e2e` feeds to the updater's verifier on all three
legs, and the harness's `rsign-format` locally (PD-35). The dry run's one release-signing key is made in `sign-dry` and never leaves it; only its
public key is uploaded. No private key and no password is in any workflow artifact (SI-19 withdrawn). Dry and real
runs build byte-identical configurations, so a dry run's app artifacts are what a real run would ship. The
`--check-only` call in each build leg uses `--platform-subset <os>`, so a leg checks only its own assets.

**`updater-e2e` per-leg cases (FR-072):**
- Windows: `Start-Process -Wait -PassThru <old setup> /S` exit 0; launch `$env:LOCALAPPDATA\Krate Manager\krate-manager.exe`;
  poll ≤ 300 s for `launched-<new>`; `(Get-Item …).VersionInfo.ProductVersion` = new; server log: exactly one GET of
  the setup asset.
- macOS case `user-placed`: `ditto` old `.app` (from its `.app.tar.gz`) to `$RUNNER_TEMP/apps/`; `open -n`; poll ≤ 300 s;
  `CFBundleShortVersionString` new, same path, `codesign --verify --deep --strict`, `xattr -lr` no
  `com.apple.quarantine`. Case `pkg-installed`: old wrapped by the FR-061 `pkgbuild` call (same step text, reused through
  a composite `run` block copied verbatim, asserted equal by the shape test), `sudo installer -pkg … -target /`, SHA-256
  manifest (`find … -type f -print0 | sort -z | xargs -0 shasum -a 256`), `open -n`, PD-13's `SecurityAgent` observe and
  cancel loop, then the FR-072 assertions; "prompt never seen within 60 s" or "accepted without a prompt" (`launched-<new>`
  appears) → `::error` naming the measurement, and the build stage surfaces it (spec FR-072).
- Linux case `appimage`: `chmod +x`, `xvfb-run -a "$RUNNER_TEMP/apps/Krate-Manager.AppImage" &`, poll; afterwards the
  file's embedded version (`--appimage-version` is not Tauri's; instead `launched-<new>` plus SHA-256 of the file equal to
  the new asset). Case `deb` (SI-21, PD-24): `sudo apt-get install -y ./old.deb`, `xvfb-run -a krate-manager &`, poll
  ≤ 300 s for `error-E_SELF_UPDATE_UNSUPPORTED` (content `krate-e2e-marker-v1`); assert no `launched-<new>`,
  `dpkg-query -W -f '${Version}' krate-manager` still = old, and the server log holds `GET /latest.json` but no GET of
  any `.AppImage` / `.deb` asset (the check resolved the AppImage entry through the `linux-x86_64` fallback, §1.2, and
  the install refused before any download). This case also measures that the bundler's `bundle_type()` patch marks the
  `.deb`'s binary `Deb`: a `SelfContained` reading would reach `pkexec` and end `E_SELF_UPDATE_INSTALL`, which reads red.
  The `.deb` package name is the bundler's (`dpkg-deb -f <deb> Package`, read and asserted `krate-manager` in the build
  leg's `artifact version` step). Every marker file's content is asserted equal to `krate-e2e-marker-v1` (§4.5).
- Every leg, before its cases: step `rsign signature served` asserts the server dir's `latest.json` `signature` of the
  leg's entry is byte-equal to `base64 -w0 < "$RUNNER_TEMP/<name>.minisig"`, the file `rsign sign` wrote (a `::notice`
  quotes the decoded untrusted comment line), so a `launched-<new>` is a pass of the updater's verifier over an
  rsign-made `.sig` (PD-35). The
  Linux leg builds and runs on the bare `ubuntu-24.04` runner (PD-34).

**`first-install` (FR-075 to FR-078):** as the spec, with the folder of SI-14. The `block live host` step: macOS `printf
'127.0.0.1 krateaudio.com\n127.0.0.1 www.krateaudio.com\n' | sudo tee -a /etc/hosts`, `sudo dscacheutil -flushcache; sudo
killall -HUP mDNSResponder`, assert `dscacheutil -q host -a name krateaudio.com` shows `127.0.0.1`; Linux `sudo tee -a
/etc/hosts`, `getent hosts krateaudio.com`; container: as root, no `sudo`; Windows (PowerShell): `Add-Content
C:\Windows\System32\drivers\etc\hosts`, `ipconfig /flushdns`, `(Resolve-DnsName krateaudio.com -Type A).IPAddress -contains
'127.0.0.1'`.

### 4.11 `manager-ci.yml` and `feed-tools.yml` (FR-073, FR-084, FR-091)

`manager-ci.yml` `check` gains four steps, in order: **`clippy updater-e2e`** directly after `clippy manual-elevation
example` (PD-32, ruling P-4: all three legs, step `env:` with the three dummy `KRATE_E2E_*` values, the `clippy
dev-feed` form, `::notice title=clippy updater-e2e ($RUNNER_OS)::clean (compiled and linted, never run)`);
`clippy updater-refusals` after `clippy-guard` (FR-083: clippy `-D warnings` and `cargo fmt --check --manifest-path …`;
all three legs whatever D9 measured, PD-33); `updater refusals` after `test ci-temp-roots` (FR-084: build + run with
`CARGO_TARGET_DIR="$PWD/target"`, require every one of the 27 FR-082 names (SI-23, SI-30, SI-35: `rsign-format` and
`production-inputs` included) `ok` exactly once and the final line `harness: 27 ok, 0 not ok`, then both plants exit 1
with their code as the first and only stdout line, no `ok ` line printed (PD-38: a plant run starts only the `good`
child); `if: runner.os != 'Windows'` exactly when `evidence/d9-windows.txt` reads `D9 WINDOWS LOAD
FAILURE`, no `if:` when it reads `D9 WINDOWS LOADS`, PD-33); `updater-e2e absent` after `dev-feed absent` (FR-073).
All four join the shape test's `STEPS` and `ASSERTION_STEPS` (`manager-ci-shape.test.js:167-229`) and `COMMANDS`
(`:232-373`); a constant `HARNESS_ON_WINDOWS` (`true` for `LOADS`, `false` for `LOAD FAILURE`), which T017 writes from
the d9 line and comments with that line verbatim, decides the `updater refusals` `if:` the shape test requires; a new `E2E_FEATURE_STEPS = { 'clippy updater-e2e': 'cargo clippy -p krate-manager ' }`
rule (as `CI_FEATURE_STEPS`, `:438-443`) keeps `--features updater-e2e` and the names `KRATE_E2E_FEED_URL`,
`KRATE_E2E_MARKER_DIR`, `KRATE_E2E_OLD_VERSION` in `check` to that one step, and `dev-feed` out of it. Mutations: each
new step removed; `clippy updater-e2e` moved after `clippy-guard`; its `env:` dropped; `--features updater-e2e` added to
`clippy`; the harness count changed from 27; the plant check dropped; the marker count check dropped; the `updater
refusals` `if:` flipped against the d9 line. No step beyond these four changes; the existing `tauri build` step is SC-017's check
of D3.

`feed-tools.yml`: both path lists gain `tools/gen-manager-latest.js`, `tools/manager-release-preflight.js`,
`.github/workflows/release-manager.yml`; `PLAN` becomes `1..64` (59 + 2 + 2 + 1: §7.1); `NAMES` gains the five FR-091
names.

### 4.12 Guards (FR-003, FR-094)

`config_guard.rs` (bare test names, SI-12): `updater_config_pinned` / `planted_updater_config_fails_the_guard` — FR-003
(a)-(e) plus (f) the workspace entry `tauri-plugin-updater = { version = "=2.14.1", default-features = false, features =
["rustls-tls"] }` exactly; capability holds exactly ten `allow-*` and no `updater:`, `process:`, `shell:`, `fs:`, `http:`
prefix; `[features]` pin becomes `["dev-feed = []", "manual-elevation = []", "updater-e2e = []"]`; `build.rs` lists the
ten names. Plants: each FR-003 field changed, a `dangerousInsecureTransportProtocol` added to `tauri.conf.json`, a CRLF
copy of the key (in memory), `createUpdaterArtifacts` in `tauri.windows.conf.json`, an `updater:default` permission, an
eleventh feature. The `src-tauri` `reqwest` line is pinned exactly as `reqwest = { version = "=0.13.5", default-features =
false, features = ["socks"] }` (PD-30); plants: the `socks` feature dropped, a second feature added (`["socks",
"blocking"]`), the version without `=`.

`injection.rs`: `tauri_plugin_updater` / `tauri-plugin-updater` confined to FR-094's files (the harness counted
separately); `reqwest` only in `src-tauri/Cargo.toml` and `src-tauri/src/self_update.rs` among `src-tauri` files (SI-28;
core's existing ban stays); `.install(` on an `Update` and `download_and_install` only inside `impl … for
NativeUpdateInstaller`; `restart (` only there (`app.restart()`; the existing `process::restart` usage is none);
`E_INSTALL_SELF_UPDATING` only in `self_update.rs`, `SelfUpdating` constructed only in `action_gate` and the four gate
sites hold an `action_gate(` call (plant: the call removed from `elevate`); FR-003 (e)'s tokens outside the e2e module;
the e2e marker only in the e2e module; **the `updater-e2e` cfg count** (PD-26: occurrences of `feature = "updater-e2e"`
per file over `workspace_rs_files`: `src-tauri/src/self_update.rs` exactly 1, `src-tauri/src/lib.rs` exactly 2, every
other file 0; plant: an extra `#[cfg(feature = "updater-e2e")]` arm in `commands.rs`, and a third one in `lib.rs`);
**the timeout pins** (SI-24, over `src-tauri/src/self_update.rs`: `build_updater`'s body holds `.timeout(inputs.timeouts.check)`
and `.read_timeout(inputs.timeouts.stall)`; `update.timeout =` occurs exactly once, inside `prepare_download`, as
`update.timeout = Some(inputs.timeouts.download)`; no `retry` token; a plant each, the call deleted); **the fallback
pin** (PD-9, over `src-tauri/src/lib.rs`: `SelfUpdateHttp(http.clone())` exactly once and before `assemble_context(roots,
&feed_url, http,`; plant: `SelfUpdateHttp(HttpConfig::production())`); **the production-inputs pins** (PD-36, SI-35,
tests `update_inputs_only_from_production` / `planted_update_inputs_fail_the_pin`: the body of `UpdateInputs::production`
in `src-tauri/src/self_update.rs` is exactly `Self { http, policy: UrlPolicy::HttpsOnly, timeouts:
UpdateTimeouts::production(), packaging: packaging() }` (whitespace-normalised, as the other body pins); `commands.rs`
holds `UpdateInputs::production(` exactly twice (the two commands) and no `UpdateInputs {`; in `src-tauri/src` the only
other `UpdateInputs {` literal is inside `mod e2e` and has the `..UpdateInputs::production(` base; `UrlPolicy::LoopbackOnly`
occurs in `src-tauri/src` only inside `mod e2e`; plants: `HttpsOnly` → `LoopbackOnly` in the body, `UpdateTimeouts {
stall: Duration::from_secs(300), ..UpdateTimeouts::production() }` in the body, `packaging: Packaging::SelfContained` in
the body, a `UpdateInputs { … }` literal in `commands.rs`, a `LoopbackOnly` outside `mod e2e`); **the macOS relaunch
pre-check** (PD-37: `current_binary(` exactly once in `src-tauri/src/self_update.rs`, inside `install`, before its
`.take()`; plant: the line removed); the tar rule of SI-2 (graph walk reusing
`config_guard`'s lock parser shape: `lock_package_names` + a BFS over `dependencies`); a new `harness_source_findings`
over `tests/fixtures/updater-refusals/src/**/*.rs` for `Roots :: from_os`, `from_os (`, `SigningKey :: release`,
`assemble_context`, `krate_manager_lib :: run`, **`NativeUpdateInstaller`, `. install (`, `download_and_install`,
`. restart (`, `process :: restart`** (SI-28: the harness must not reach the real installer or a restart, FR-022 /
FR-094 "in the workspace and the harness"), **`app . path (`, `BaseDirectory`, and `dirs ::` outside
`src/confinement.rs`** (SI-29), **`TcpStream :: connect` outside `src/socks.rs`** (PD-30: the relay is the harness's
only outbound dial, and it dials loopback only), a plant each (each token added to a copy of `scenarios.rs` in memory);
the scan must reach the six harness source files of §2, else a completeness finding. `locks()` gains
the harness lock for the tar rule only.

`roster.rs`: unchanged; it now covers `src/manager_update.ts` and both `self_update.rs` files by its directory walk
(`roster.rs:101-125`), which PD-4 satisfies.

`release-manager-shape.test.js` (`release-manager.yml keeps the Phase 11 shape`, revision 4 additions): the steps
`install rsign2`, `updater asset list`, `sign updater artifacts` and `delete signing key (after signing)` exist in
`sign`, `sign-dry` and `updater-e2e` with byte-equal `name` and `run`, and `throwaway signing key` is byte-equal in
`sign-dry` and `updater-e2e` (PD-35; plant: one character of `updater-e2e`'s `sign updater artifacts` run changed);
`updater-e2e`'s `latest.json` step names `--pubkey "$RUNNER_TEMP/krate.pub"` and runs after `sign updater artifacts`
(plant: moved before it); no step of `sign` or `sign-dry` uses `actions/cache` or `Swatinem/rust-cache`, and `install
rsign2` precedes the key step (plant: `actions/cache` with the `rsign2-0.6.7-…` key added to `sign`); the Linux `bundle`
step of `build` and both Linux bundle steps of `updater-e2e` carry `NO_STRIP: 'true'` (PD-34; plant: dropped from one);
`build warnings` holds `grep -c '^warning:'` and no exclusion (plant: a `grep -v`); `package-macos`'s `pkg facts` holds
`count(//relocate/*)` (plant: replaced by a `grep`); `preflight` passes `--repo "${{ github.repository }}"` (SI-40).

### 4.13 `tests/required-tests.json` and `EXPECTED_REQUIRED` (FR-095)

`cargo.all` gains: `self_update::tests::url_policy`, `self_update::tests::redirect_hops`,
`self_update::tests::proxy_choice_matches_core`, `self_update::tests::codes_and_sentences`,
`self_update::tests::view_from_update`, `self_update::tests::endpoint_matches_feed_tool`, `self_update::tests::busy_rule`,
`self_update::tests::in_flight_flag`, `self_update::tests::timeouts`, `self_update::tests::no_context_defaults`,
`self_update::tests::packaging_rule`, `self_update::tests::progress_events_serialize`,
`context::tests::release_for_restart_frees_the_lock`, `install::tests::refuse_while_self_updating`,
`updater_config_pinned`, `planted_updater_config_fails_the_guard`, `self_update_confinement_holds`,
`planted_self_update_confinement_fails`, `updater_tar_only_through_the_plugin`, `updater_e2e_cfg_sites_are_counted`,
`planted_updater_e2e_cfg_site_fails`, `harness_source_names_no_installer_or_root`, and (revision 4, PD-36)
`update_inputs_only_from_production`, `planted_update_inputs_fail_the_pin` (`tasks.md`'s cargo count follows: +2).
`vitest` gains the SC-003 / SC-023 /
SC-025 names (§7.1), `shows downloading then installing progress` included, and the two PD-31 titles `offers the new
.deb instead of an update on a system-package install`, `a failed .deb release-page open shows its code` (13 titles in
all). `node` gains `bundle release mode argv`, `bundle release asset table`, `e2e update server serves only
its directory`. The harness is not a cargo test (D9); its names are checked by the `updater refusals` step itself.
`EXPECTED_REQUIRED` (`manager-ci-shape.test.js:600`) mirrors the file.

### 4.14 Roadmap amendments (FR-093), by the build stage

(a) lines 828-830 and 836-837: "`bundle.js --release` builds each leg (no `tauri-action`: 1.0.0 writes `api.github.com`
asset URLs and dropped the `tagName` rewrite, Phase 11 D4); `gh release upload`; `tools/gen-manager-latest.js` writes
`latest.json` with per-tag github.com URLs and the three darwin keys"; (b) 856-858 as FR-093 (b); (c) Phase 12's checks
gain FR-093 (c)'s item; (d) the Phase 11 status line; (e) as FR-093 (e). Nothing else.

### 4.15 State layout

Phase 11 adds **no persistent state**. In memory: `KeptUpdate` (one `Update`), the `SELF_UPDATING` registry (keyed by
state dir). On disk, the updater itself writes only temp files (Windows: the setup in the user temp dir,
`updater.rs:1164`; macOS: two temp dirs on the app's volume; Linux: a temp dir beside the AppImage), never under the
manager's state dir. The `updater-e2e` build writes `<marker>/launched-<v>` and `<marker>/error-<code>` under
`$RUNNER_TEMP`. The launched apps of `updater-e2e` and `first-install` (release or e2e build) also call
`Roots::from_os` and take the state lock at startup, so on those ephemeral runners they write the runner user's state
dir of the app identifier (macOS `~/Library/Application Support/com.krateaudio.krate-manager`,
`platform/macos.rs:34-35`; Linux `$XDG_DATA_HOME` or `~/.local/share` + `com.krateaudio.krate-manager`,
`platform/linux.rs:31-37`; Windows `%LOCALAPPDATA%\com.krateaudio.krate-manager`, `platform/windows.rs:37-41`) and
whatever per-user data and caches the OS webview keeps for the app; FR-090's write set names them (SI-38; the
redirect alternative was not taken because `open -n` launches through LaunchServices, SI-38). The harness writes only
under its `TempDir`.

### 4.16 The OS abstraction

The self-update path has no OS branch of its own: the per-OS install step is `tauri-plugin-updater`'s (§1.2), selected at
compile time inside the crate; the manager's code differs per OS only in (1) `NativeUpdateInstaller`'s post-install
`#[cfg(not(windows))]` restart, (2) `ManagerUpdateView.os` from `Platform::os()` (`NativePlatform.os()`, as `lib.rs:34`),
(3) the UI's macOS-only sentence, and (4) `install`'s macOS `current_binary` pre-check (PD-37). Each is tested: (1) by
`updater-e2e` on all three legs, (2) by `view_from_update` with each `Os`, (3) by the vitest case with each OS, (4) on
its passing side by the macOS `updater-e2e` cases and the macOS harness run (a real, symlink-free path must not be
refused), its refusing side only by the `injection.rs` pin (R-22). Roots stay injected as in Phases 4-10; no new root.

---

## 5. Dependencies

| Crate / package / action | Version | Where | Docs / source verified | Why |
|---|---|---|---|---|
| `tauri-plugin-updater` | `=2.14.1`, `default-features = false`, `["rustls-tls"]` | workspace, `src-tauri` normal; harness | https://docs.rs/crate/tauri-plugin-updater/2.14.1, source at `updater-v2.14.1` (§1.2) | First-party updater; the roadmap's choice (lines 150-156) |
| `reqwest` | `=0.13.5`, `default-features = false`, `features = ["socks"]` (already locked; `socks = []` adds no crate, PD-30) | `src-tauri` normal, `self_update.rs` only (§4.3) | local registry `reqwest-0.13.5` (`Cargo.toml:178`, `src/connect.rs:783-792`); https://docs.rs/reqwest/0.13.5/reqwest/redirect/struct.Policy.html; https://docs.rs/reqwest/0.13.5/reqwest/struct.Proxy.html | `redirect::Policy::custom`, `read_timeout`; not re-exported by the updater; `socks` unified into the updater's client (ruling P-1; Cargo feature unification, §1.2) |
| `tauri` | 2.12.5 (unchanged); harness adds feature `test` | harness | local registry | mock runtime (D9) |
| `tauri-winres` | 0.3.7 (already locked through tauri-build) | harness build-dependency | local registry `src/lib.rs:530` | the Windows manifest (PD-12) |
| `minisign` | 0.10.0 (already locked as core's dev-dependency) | harness normal dependency | local registry | throwaway keys and Tauri-shaped signatures |
| `tempfile` 3.27.0, `serde_json` 1.0.154, `base64` 0.23.1, `url` 2.5.8 | locked versions | harness | workspace `Cargo.toml:13-50` | — |
| npm | none new | — | — | the UI uses only `@tauri-apps/api` 2.12.4's `invoke` (no `@tauri-apps/plugin-updater`, D1) |
| `@tauri-apps/cli` | 2.12.4 (unchanged) | build | `package.json` | `tauri build`, `tauri signer generate` |
| `actions/checkout@v6`, `actions/setup-node@v4`, `actions/upload-artifact@v7`, `actions/download-artifact@v8`, `Swatinem/rust-cache@v2` | as in the repo's workflows (`manager-ci.yml:42-66`, `feed.yml:109-113`) | `release-manager.yml` | — | no new action; no `tauri-apps/tauri-action` (D4) |
| Node built-ins only | — | the three new scripts | — | repo rule (Node, no deps) |
| Rust `std` only | — | the harness SOCKS relay `src/socks.rs` (PD-30) | — | no SOCKS server crate: the relay is ~100 lines of handshake and copy, and a new dependency would re-lock the harness for a test fixture |

The `socks` feature adds no package to any of the three locks (Cargo.lock records no features; reqwest's SOCKS code
uses hyper-util, already locked). New transitive crates the lock gains through the updater (to be listed by `cargo tree -p krate-manager -e normal` in compliance): `infer`,
`osakit` (+ `objc2-osa-kit`), `tar`, `percent-encoding` (likely present), `semver`/`time` (likely present). None is in
`SIGNING_CRATES` (`config_guard.rs:174`); `tar` is SI-2's.

---

## 6. Safety design

- **Injected roots and hosts.** No new root. The endpoint is compiled config (D2), pinned by `updater_config_pinned` and
  equal to `feed-publish.js:64` (`endpoint_matches_feed_tool`). The e2e build's loopback endpoint, feed and marker dir
  exist only behind `updater-e2e` + its `env!` values; the release legs prove absence (FR-056, FR-073), and the e2e job
  proves the marker is present when the feature is on (`marker present`). The harness's roots come from `temp_roots`,
  its env is redirected per child, its servers bind loopback only, it never names the real installer or a restart
  (`harness_source_findings`), its only outbound dial is the SOCKS relay's, to `127.0.0.1` only (PD-30), and its key is
  never the committed one (FR-081). On Windows, the env redirection does not
  cover known-folder lookups (PD-11); the harness names no path API but the read-only snapshot, and the snapshot reports
  any new entry under the app's real known-folder dirs (SI-29, R-14).
- **Verify before anything runs.** The updater verifies the minisign signature and the signed version in `download`
  (`updater.rs:891`) and again in `install` (`:921-925`), against the compiled pubkey; the manager hands the installer
  only bytes `download` returned (§4.3 step 7 → 10). What the OS step does with them differs: Windows writes the setup to
  the user temp dir and runs it as the user (NSIS `currentUser`); macOS swaps a user-owned bundle as the user, and only a
  root-owned (pkg-installed) bundle goes through the authorization prompt, inside the updater process (PD-13); the
  AppImage is replaced as the user. **The only self-update step that runs as root is the macOS pkg-installed swap**
  (updater-internal, after the admin prompt): the verified `.app.tar.gz` is un-tarred by the user into a temp dir on the
  app's volume and chmodded 0755, still user-owned, and after authorization a root `do shell script` moves that
  user-extracted tree, `Contents/MacOS/krate-manager-helper` included (which Phase 10 later runs as root), into
  `/Applications` with no re-verification (`updater.rs:1486-1636`, `:1688-1691`, §1.2); a process running as the user
  could modify the tree between extraction and the swap. That is the D11 / Phase 10 D19 residual risk the user accepted
  on 2026-11-01 (`spec.md:283-292`; R-20), not a new design decision. The `.deb` route, which would run dpkg as root on
  a user-written file (`updater.rs:1329-1410`: user-written `package.deb`, then `pkexec` / `sudo -S` with a password
  read through zenity / kdialog, or `sudo`), is refused before any download (PD-24, harness `deb-install`, e2e case
  `deb`), and the UI never offers it: a `.deb` view carries only `OpenReleasePage` (PD-31), the `github.com` tag page of
  this project's repository and the announced version, built by core from `MANAGER_REPO` (never from the unsigned
  `latest.json`'s URL segments, PD-27) and re-checked by `open_release_page` (`commands.rs:54-59`), so the user installs
  the new `.deb` from this project's own release with their package manager. That page is only as trustworthy as the
  `latest.json` that announced the version (which no signature covers): it can name no other repository and no other
  release than the announced version's, but it can announce a version this repository has not released, which the user
  then finds missing on the page. The manager's own URL rules run before every request: the endpoint, the package URL's
  fixed origin (PD-27: host, `MANAGER_REPO`, the announced version's tag)
  and every redirect hop with its limit (FR-011, FR-011a, PD-5); a proxy the core would refuse fails closed (PD-6); a
  SOCKS proxy the core chooses is used as chosen (PD-30): the tunnel carries TLS for an https target with the same
  certificate checks (`connect.rs:565-590`), and the signature check does not depend on the route. No comparator, so a downgrade or equal version is never offered (`:712-720`; `lib.rs:102-107`).
  CI verifies every `.sig` with `tools/minisign.js` against the committed key before upload and again before writing
  `latest.json` (FR-042, FR-053), and `feed-publish.js` refuses a non-per-tag `latest.json` (FR-043).
- **Zip-slip.** Not applicable to manager code: the manager extracts nothing for self-update; the updater's macOS
  un-tar of a signature-verified archive is the only extraction (`updater.rs:1519-1534`).
- **The elevated helper** gains no operation; the self-update never calls it. It cannot run while the helper runs
  (FR-015 / FR-019 busy rule), and no elevated batch starts while it runs (FR-018 gate in `elevate`).
- **Capability set.** Ten `allow-*` permissions of the app's own commands (the `.deb` action reuses the existing
  `allow-open-release-page`; PD-31 adds none); no `updater:*` (the plugin's four JS commands
  stay unreachable from the webview, `lib.rs:268-298` of the plugin), no `process:*`, `shell:*`, `fs:*`, `http:*`; the
  two new commands take no webview argument. CSP unchanged (`tauri.conf.json:16`).
- **Keys.** The real private key is the one that also signs every plugin installer the manager later runs elevated
  (roadmap line 23; keys README §2), so it never shares an environment with dependency code: it reaches only the `sign`
  job's `signing key` step env, exists decrypted only until the step after `sign updater artifacts` deletes it, and that job runs no
  build, no app `npm ci`, no `tauri` (PD-23, SI-20; shape-tested). Every `build` leg, dry or real, signs with a per-leg
  throwaway key that never leaves its step; the dry run's release-signing key is made in `sign-dry`; the e2e jobs make
  their own; the harness keys live in memory and every one is checked against the committed key (FR-081 (d)). No
  private key or password is in any workflow artifact. Residual surface: `ajv` (installed `--ignore-scripts`, loaded by
  `minisign.js`) and `rsign2` 0.6.7 with its locked graph (compiled fresh by `cargo install --locked`, never restored
  from a cache another workflow can write, PD-35) run in the `sign` job; R-15.
- **Unsigned reality.** No certificate; `--no-sign` never used (it would also skip ad-hoc signing, External facts);
  `codesign` ad-hoc asserted on the universal bundle (FR-054) and after the e2e swap (FR-072).

---

## 7. Test plan

### 7.1 SC → test

| SC | Test (file :: name) | Assertion strategy |
|---|---|---|
| SC-001 | `crates/core/src/self_update_tests.rs` :: `url_policy`, `redirect_hops` | Table of (policy, URL) → `Ok` / `Url { reason }`, each row asserted by code and reason, over `check_update_url` and `check_update_package_url` with `announced = "0.2.0"` (SI-27 rows: `https://example.org/pkg` → `Host`, `https://github.com/rolandzwaga/krate-audio/archive/x` → `Host`, a query or fragment → `Host`, `https://github.com/rolandzwaga/krate-audio/releases/download/manager/v0.2.0/x` → `Ok`, `http://127.0.0.1:9/rolandzwaga/krate-audio/releases/download/manager/v0.2.0/x` under `LoopbackOnly` → `Ok`; SI-40 rows: a foreign `owner/repo`, `https://github.com/attacker/x/releases/download/manager/v0.2.0/a`, → `Host` under `HttpsOnly` and its loopback twin under `LoopbackOnly` → `Host`; the owner only differing in case, `https://github.com/RolandZwaga/krate-audio/…/v0.2.0/x`, → `Host`; a tag that differs from the announced version, `…/manager/v0.1.9/x` and `…/manager/v9.9.9/x` with `announced = "0.2.0"`, → `Host`; `announced = "0.2.0-rc.1"` against `…/v0.2.0/x` → `Host`; `MANAGER_REPO` itself matches `^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$`, `feed-publish.js:66`'s `REPO_RE`); hop table with `HopRules { endpoint_host: "update.example" }` for the endpoint rows; `redirect_decision` rows: `previous` of length 6 → `Policy` whatever `to` is, length 5 → the hop rule; `hop_kind(endpoint, endpoint)` → `Endpoint`, `hop_kind(other, endpoint)` → `Package`, and a chain starting at the endpoint judged by the endpoint host rule (no banned literal needed outside tests; the tests may name `api.github.com`: `*_tests.rs` is unscanned, `roster.rs:39-41`, `injection.rs:1386-1390`) |
| SC-002 | `self_update_tests.rs` :: `proxy_choice_matches_core` | Two `FixtureServer`s (target, proxy). Four configs: static proxy; static proxy with `no_proxy` = `127.0.0.1`; none; PAC (the Phase 5 fixture PAC returning `PROXY <proxy>`, through the existing engine on macOS/Linux and the `FakePacHost` on Windows). For each: `proxy_choice` result vs. which server's `requests()` got the `http::get` (SI-16). Fifth config (SI-26): a static proxy the core refuses (scheme `ftp`) → `proxy_choice` is `Err(Network)`, `http::get` errs too, and both servers record zero requests. The SOCKS route (PD-30, ruling P-1) is the harness scenario `socks-proxy` (§4.7), not a row here: it is the updater's client, not `http::get`, that must take the `socks4` / `socks5` choice, and only the harness reaches that client |
| SC-003 | `self_update_tests.rs` :: `codes_and_sentences`, `view_from_update`, `packaging_rule` (`SystemPackage` → `E_SELF_UPDATE_UNSUPPORTED` with §3.1's sentence, `SelfContained` → `Ok`; and the PD-31 `update_action` rows, `announced = "0.2.0"`: `SelfContained` with the correct per-tag URL → `InstallAndRestart`, with a foreign-repo or wrong-tag URL → `Err(Url { Host })` (revision 4: no banner for a URL PD-27 refuses); `SystemPackage`, `HttpsOnly`, `https://github.com/rolandzwaga/krate-audio/releases/download/manager/v0.2.0/x` → `OpenReleasePage { url: "https://github.com/rolandzwaga/krate-audio/releases/tag/manager/v0.2.0" }` and that `url` passes `status::release_page_url` unchanged; `SystemPackage`, `LoopbackOnly`, `http://127.0.0.1:9/rolandzwaga/krate-audio/releases/download/manager/v0.2.0/x` → the same `github.com` page; SI-40 rows: `SystemPackage` with `https://github.com/attacker/x/releases/download/manager/v9.9.9/a` and `announced = "9.9.9"` → `Err(Url { Host })` (the attacker-page case: no page is ever built from a foreign repo), with the project's URL for `v0.1.9` and `announced = "0.2.0"` → `Err(Url { Host })` (the downgrade-by-URL case); `SystemPackage` with `https://example.org/pkg`, with `https://github.com/rolandzwaga/krate-audio/archive/x`, and with `http://127.0.0.1:9/x` → `Err(Url { Host })`), `progress_events_serialize` (`{"event":"downloading"}`, `{"event":"installing"}`); harness `map-errors` (every §3.2 row, §4.7) and `deb-install`; `view_from_update` gains the action rows (`{"kind":"install_and_restart"}`, `{"kind":"open_release_page","url":"…"}` serialized under `available.action`); `src/manager_update.test.ts` :: `renders the manager update line`, `confirms before updating`, `manager update confirmation names the admin prompt on macOS only`, `shows a failed update by code`, `shows no banner when the check fails`, `shows downloading then installing progress`, `offers the new .deb instead of an update on a system-package install`, `a failed .deb release-page open shows its code` (§4.6, PD-31); `src/main.test.ts` order tests updated | Exact texts; `notes` through `notes::blocks`, no `<` in any text field; mocked `invoke` records call order; the fake channel drives the two progress events in order |
| SC-004 | `crates/core/tests/config_guard.rs` :: `updater_config_pinned`, `planted_updater_config_fails_the_guard`; `self_update_tests.rs` :: `endpoint_matches_feed_tool` | The guard reads `tauri.conf.json`, the key file (bytes, `\r` check) and computes base64; the core test reads `tools/feed-publish.js` text and extracts `MANAGER_LATEST_URL = '…'` |
| SC-005 | `self_update_tests.rs` :: `busy_rule` | `FakeElevation` held (`hold_for_test`, §4.7) / stale (a timed-out batch through the existing driver test path); a `Busy` entry made through `install::tests`-visible seam (`any_busy` with a planted key via a `#[cfg(test)] pub(crate)` helper); an `InFlight` held through `download`'s test seam; `None` context |
| SC-006 | `context_tests.rs` :: `release_for_restart_frees_the_lock` | `fresh_temp_roots`; assemble; `StateLock::acquire` → `Held`; `release_for_restart` → `Released`; acquire → `Ok` |
| SC-007 | the harness (`tests/fixtures/updater-refusals`), run locally (Windows; under D9's fallback WSL, PD-33) and by `updater refusals` | §4.7; the step greps the 27 `ok` names (SI-23, SI-30, SI-35: `socks-proxy`, `rsign-format` and `production-inputs` included), the summary line `harness: 27 ok, 0 not ok`, both plants' exit 1 with their code as the only stdout line; a plant run spawns only the `good` child, so no scenario runs (PD-38), and that child refuses at its key check (`--plant committed-key`, `E_HARNESS_KEY`) or its first bind (`--plant bind-any`, `E_HARNESS_BIND`) before any server starts. `rsign-format` is SC-007's local proof that an rsign-made `.sig` in the `base64 -w0` framing passes the updater's own verifier (PD-35; its CI twin is SC-015) |
| SC-008 | `apps/krate-manager/scripts/bundle.test.js` :: `bundle release mode argv`, `bundle release asset table` (plus the existing `bundle sidecar name per host tuple`) | pure functions of §4.8; refusals via `parseArgs` throwing with exit code 2; `resolveAsset` with zero and two matches → `E_BUNDLE_ASSET` naming the glob and `matched 0` / `matched 2`, one match → the D6 name (FR-032); no `.deb.sig` row (SI-21) |
| SC-009 | `tools/feed-contract/tests/gen-manager-latest.test.js` :: `gen-manager-latest writes per-tag latest.json`, `gen-manager-latest refuses what it cannot verify` | fixture assets in a temp dir signed with `minisign.js` (`generateKeyPair`, `signBuffer` with trusted comment `timestamp:1\tfile:x\tversion:<v>`), `.sig` = base64 of the minisig text; byte-equal second run; CHANGELOG with three dated sections (via the preflight's notes) |
| SC-010 | `feed-publish-collect.test.js` :: `collect copies only a valid manager latest.json` (extended) | the existing fake `gh` deps; three new cases |
| SC-011 | `manager-release-preflight.test.js` :: `manager release preflight refuses by name`, `manager release body is exact` | fixture app dirs written to a temp dir (copies of the real `version.json`, `tauri.conf.json`, `Cargo.toml`s, mutated); tags files; the KAT key as a test key; body vs `release-body.expected.md`; SI-40 rows inside `refuses by name`: `--repo attacker/x` against the fixture's `MANAGER_REPO` line → `E_PREFLIGHT_REPO`, a fixture `self_update.rs` without the line (and one with two) → `E_PREFLIGHT_REPO`, the real file's line read through the same parser equals `rolandzwaga/krate-audio` |
| SC-012 | `config_guard.rs`, `injection.rs` new tests; `release-manager-shape.test.js` :: `release-manager.yml keeps the Phase 11 shape`; `manager-ci-shape.test.js` :: `manager-ci.yml keeps the Phase 4 shape` (the four new steps, `E2E_FEATURE_STEPS`, `HARNESS_ON_WINDOWS`, §4.11); clippy (with `clippy updater-e2e`, PD-32) / fmt / tsc / eslint | each rule with a planted in-memory mutation (as `manager-ci-shape.test.js:1286-1290` does) |
| SC-013 | PENDING-CI `release-manager.yml` `build (macos-latest)` | steps `lipo`, `plist: LSMinimumSystemVersion`, `plist: no LSFileQuarantineEnabled`, `codesign verify`, `codesign ad-hoc`, `signatures`, `updater-e2e absent`, `ci-temp-roots absent` |
| SC-014 | PENDING-CI `dry-publish` (then `publish`) | `latest.json` key set and URL form, `checkLatestJson` exit 0 |
| SC-015 | PENDING-CI `updater-e2e (windows-latest \| macos-latest \| ubuntu-24.04)`; static half `release-manager-shape.test.js` (the PD-35 step equality) | §4.10 cases; each leg's `rsign signature served` notice, so every `launched-<new>` is the updater's verifier accepting an `.sig` made by the `sign` job's own steps (PD-35, SI-34) |
| SC-016 | PENDING-CI `first-install (…)` four legs | FR-076 assertions; process presence only |
| SC-017 | PENDING-CI `manager-ci.yml` `check (…)`: `tauri build`, `build warnings`, `clippy updater-e2e`, `updater-e2e absent`, `updater refusals` (macOS / Linux; Windows only on `LOADS`) | — |
| SC-018 | PENDING-CI `build (linux)` | `glibc floor` notice; both artifacts and valid `.sig`s |
| SC-019 | PENDING-CI, dispatches of `release-manager.yml` with `dry_run: true` | §7.3 |
| SC-020 | PENDING-CI after the user's real dispatch (Phase 12) | — |
| SC-021 | PENDING-CI `package-macos` | `payload`, `pkg facts`: `xmllint --xpath 'count(//relocate/*)'` = `0` over the expanded `PackageInfo` (SI-37), which is printed into the job summary, uploaded, and recorded at compliance as `evidence/packageinfo-dry-run.xml` |
| SC-022 | PENDING-CI `build (…)` three legs; static half by SC-012; positive control `updater-e2e (…)` step `marker present` | the release legs' zero marker count is meaningful only because the e2e job finds the marker ≥ 1 in its own binary (§4.5, SI-25) |
| SC-023 | `self_update_tests.rs` :: `in_flight_flag`; `install_tests.rs` :: `refuse_while_self_updating`; harness `busy-after-download` and `stall` (in-flight seen from the watcher, SI-23); `src/main.test.ts` :: `disables plugin actions while the manager updates itself` (revision 4: moved from `status.test.ts`, it drives the real `main.ts` wiring, §4.6); `src/status.test.ts` :: `re-enables plugin actions after a failed manager update` | the main test: the mounted shell with a mocked `invoke` (`plugin_status` → one card with an action, `manager_update_check` → an `install_and_restart` view, `manager_update_install` → a test-controlled promise); click "Update and restart", confirm; every card action button and "Check now" `disabled`; reject the install; all enabled again (a no-op or wrong `actions` object in `main.ts` is red). The status test: `setActionsEnabled(false)` then `(true)` on a mounted section, each button's `disabled` read after each call. The install test, rows `download_plugin`, `install_plugin`, `remove_plugin`: sets the flag with `InFlightGuard::enter(Some(state_dir))` on a temp context before calling the entry point, and drives it with the existing test env (`install::tests` builders), asserting the error code, zero fixture-server requests, zero `InstallIo` mutations (the recording fake), zero `FakeElevation` launches, and `!any_busy(state_dir)`. Row **elevated batch**: the flag is clear when `install::for_command` starts (so its own first-statement gate passes) and is set mid-flight by a wrapping `InstallIo` whose `canonicalize` of the staging dir, the first `InstallIo` call of the elevated path (`install.rs:1130`, before `elevate` at `:1157`), enters and keeps an `InFlightGuard`; asserts `E_INSTALL_SELF_UPDATING` from `elevate`'s gate, zero `FakeElevation` launches, no job file under `state_dir.join(job::JOBS_DIR)` (`install.rs:1301`), and `!any_busy(state_dir)` after return. The harness rows assert that the flag `install` sets is the one `action_gate` reads for the same `state_dir` |
| SC-024 | `self_update_tests.rs` :: `timeouts`; harness `good`, `stall` and `production-inputs`; `injection.rs` timeout pins and `update_inputs_only_from_production` / `planted_update_inputs_fail_the_pin` (PD-36) | `production-inputs`: `UpdateInputs::production(HttpConfig::production())` has `policy == HttpsOnly` and `timeouts == UpdateTimeouts::production()`; the PD-36 pins: the exact body of `UpdateInputs::production`, `commands.rs` builds inputs only through it, `LoopbackOnly` only in `mod e2e`, a plant each (§4.12), so the inputs the production commands use are the ones the harness checks; `timeouts`: the three constants and `UpdateTimeouts::production() == UpdateTimeouts { check: 30 s, download: 3600 s, stall: 30 s }`; harness `good`: `Update.timeout == Some(DOWNLOAD_TIMEOUT)` after the production `prepare_download`; the three SI-24 rules (`.timeout(inputs.timeouts.check)`, `.read_timeout(inputs.timeouts.stall)`, the one `update.timeout = Some(inputs.timeouts.download)`, no `retry`), a plant each; `stall`: one package request, `E_SELF_UPDATE_NETWORK` within 10 s |
| SC-025 | `self_update_tests.rs` :: `no_context_defaults`; `injection.rs` fallback pin (PD-9); `src/main.test.ts` :: `checks for a manager update after plugin_status is rejected`, `checks for a manager update after plugin_status succeeds`, `does not check for a manager update when the context is locked` | `no_context_defaults`: a `fallback` built as `lib.rs:64-67` builds it (`HttpConfig::production().with_proxy(p).with_pac(..)`, `p` a fixture proxy) → `self_update_http(None, &fallback) == fallback` and is not `HttpConfig::production()` alone; with an assembled temp context → `ctx.http`; `self_update_busy(&FakeElevation::refusing(), None)` is false; `release_for_restart` of a context-less `SharedState` → `NoLock`. The `lib.rs` pin closes the half core cannot reach |
| SC-026 | PENDING-CI `first-install`, every leg; static half `release-manager-shape.test.js` | — |

Top-level node test count: `gen-manager-latest.test.js` 2, `manager-release-preflight.test.js` 2,
`release-manager-shape.test.js` 1 → 64.

### 7.2 FRs without their own SC

FR-004 (existing manager-ci macOS step + FR-054 in `build (macos-latest)`); FR-010 (module exists: compile); FR-020 /
FR-021 (`updater_config_pinned` capability and `build.rs` pins); FR-022 / FR-023 (harness `recheck-replaces`,
`failed-install-clears`, and `busy`'s second install for SI-36's "an FR-015 refusal keeps it"; FR-094 confinement); FR-030 to FR-033 (SC-008, SC-013); FR-044 (SC-009, SC-011); FR-055
(shape test, SI-20: `secrets.TAURI_SIGNING_*` only in the `sign` job's `signing key` step env, no `echo`, no file
outside `$RUNNER_TEMP`; that job runs no app `npm ci`, `cargo build`, `bundle.js`, `tauri` or `npx` and nothing of the
kind after the key step; the delete step follows `sign updater artifacts`; no `build` step names `secrets.`; `main-only`
runs only `echo`; plants as SI-20);
FR-060 (unchanged config, `config_guard`); FR-063 (SC-011); FR-090 (§7.4 never runs any of them; shape test confines
`sudo` / `installer -pkg` / `apt-get install ./`; the CI jobs' write set as SI-38 widens it, §4.15); FR-092 (`git diff --name-only` at close lists no fenced path);
FR-093 (compliance reads the roadmap diff); FR-096 (existing `roster absent`); FR-097 (`git check-attr -a
tools/feed-contract/keys/krate-signing.pub` → `text: unset`).

### 7.3 PENDING-CI rows (exact job and assertion)

| Row | Job (name as GitHub shows it) | Assertion closing it |
|---|---|---|
| SC-007 macOS / Linux, SC-017 | `Manager CI / check (macos-latest)`, `check (ubuntu-24.04)` | `updater refusals` notice "27 ok, 0 not ok; plants exit 1" (`socks-proxy` among the 27: the macOS / Linux proof of PD-30; `rsign-format` and `production-inputs` too, PD-35, PD-36); `clippy updater-e2e` notice (PD-32); `clippy updater-refusals` notice; `updater-e2e absent` notice; `tauri build` + `build warnings` green |
| SC-007 Windows | on `D9 WINDOWS LOADS`: `Manager CI / check (windows-latest)` `updater refusals` notice as above (the CI twin of the local row); on `LOAD FAILURE`: no CI row, the step is skipped by its `if:` and compliance records the gap quoting `evidence/d9-windows.txt` (PD-33) | — |
| PD-32 Windows | `Manager CI / check (windows-latest)` | `clippy updater-e2e` notice (the e2e module's Windows compile; locally L-E2E) |
| SC-013, SC-022 macOS | `release-manager / build (macos-latest)` | the named step notices, `leg key is not the release key` included |
| SC-018, SC-022 Linux | `release-manager / build (linux)` | `glibc floor` notice `<x.y> (container <x.y>)`, `signatures` notice, `leg key is not the release key` |
| SC-022 Windows | `release-manager / build (windows-latest)` | `updater-e2e absent`, `ci-temp-roots absent`, `leg key is not the release key` notices |
| SC-022 positive control | `release-manager / updater-e2e (windows-latest \| macos-latest \| ubuntu-24.04)` | `marker present` notice (≥ 1 occurrence in the e2e binary) |
| FR-055 / SC-019 key half (real run: Phase 12) | `release-manager / sign` (real) and `sign-dry` (dry) | `verify` notice (3 `.sig`s verified against the committed / the dry key); `delete signing key (after signing)` ran |
| SC-021 | `release-manager / package-macos` | `payload` notice; `pkg facts` notice `count(//relocate/*) = 0` (and whether `relocate` was absent or empty); PackageInfo in the summary and the `packageinfo` artifact, copied to `evidence/packageinfo-dry-run.xml` |
| SC-015 | `release-manager / updater-e2e (windows-latest)`, `(macos-latest)`, `(ubuntu-24.04)` | per-case notices; `rsign signature served` notice on each leg (PD-35: the served `.sig` is the rsign one); the macOS `pkg-installed` measurement |
| PD-34 / R-21 | `release-manager / build (linux)` and `updater-e2e (ubuntu-24.04)` | the `bundle` steps succeed with `NO_STRIP: 'true'` and the AppImage asset exists; a linuxdeploy failure shows as the `bundle` step's non-zero exit (the bundler's "failed to run linuxdeploy"), recorded and surfaced, never retried silently |
| SC-016, SC-026 | `release-manager / first-install (windows-latest \| macos-latest \| ubuntu-24.04 \| ubuntu-22.04-container)` | `block live host`, install, `launch` notices |
| SC-014, SC-019 | `release-manager` dispatched `dry_run: true` from `main` (clean); from a branch with a drifted `version.json`; from `main` with a test tag `manager/v0.1.0` the user creates and deletes; with the CHANGELOG heading removed on a branch → `E_PREFLIGHT_CHANGELOG` | `preflight` `::error` with the code and no `build` job started; the clean run's `manager-release-dry-run` artifact, `feed-dry` green; `git ls-remote` evidence pasted; `dry-publish` step `sigs not committed key` notice (3 of 3 `.sig`s refused by `krate-signing.pub` with `E_MINISIG_KEY_ID`) beside `gen-manager-latest.js`'s success against `dry-signing.pub` (both halves of SC-019's key sentence) |
| SC-020 | the user's first real dispatch (Phase 12) | — |

### 7.4 Local close procedure (Windows, this box)

1. `cd apps/krate-manager`; `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`;
   the clippy-guard command of `manager-ci.yml:122-125`; the `clippy updater-e2e` command with its three dummy
   `KRATE_E2E_*` values (PD-32, the same command CI runs); `cargo clippy --manifest-path tests/fixtures/updater-refusals/Cargo.toml
   --all-targets --locked -- -D warnings` with `CARGO_TARGET_DIR="$PWD/target"`.
2. `cargo test --workspace --locked` (default threads; every binary `0 failed`), output to a log under `f:/tmp/`.
3. Once, before the first harness build (the fixture is committed after): `node
   tests/fixtures/updater-refusals/make-rsign-fixture.js` (PD-35: `~/.cargo/bin/rsign`, a throwaway key in an
   `fs.mkdtemp` dir that the script deletes; its printed record goes to `specs/krate-manager-phase11/evidence/`). Then
   `CARGO_TARGET_DIR="$PWD/target" cargo run --manifest-path tests/fixtures/updater-refusals/Cargo.toml --locked` (D9
   measurement; wall clock recorded) and the two `--plant` runs. Under `D9 WINDOWS LOAD FAILURE` (PD-33, ruling P-5)
   `harness-lane.js` runs the same build, run and plants in WSL Ubuntu 24.04 instead, from `/mnt/f/…/apps/krate-manager`
   with `CARGO_TARGET_DIR=$HOME/km-target-harness` (WSL's own file system, never `/mnt/f`, never the Windows `target/`),
   concurrently with the WSL core lane (its own target dir, so no build-lock contention); it needs R-17's packages; its
   Windows verdict is `GAP` quoting the d9 line, and the lane is green only on a green WSL run.
4. `npm run typecheck`, `npm run lint`, `npm test`, `node --test "scripts/*.test.js"`; `node ../../tools/lint-manager-version.js`.
5. From the repo root: `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > f:/tmp/tap.txt` and the
   `feed-tools.yml` TAPCHECK block replayed against it (PLAN `1..64`, NAMES).
6. YAML parse of the three workflows (`node -e` with the shape tests' own parser).
7. Steps 1-2 and 4-5 are independent: run 2, 3, 4 and 5 concurrently after 1 (one cargo build dir: steps 2 and 3 share
   `target/` and cargo's lock serialises their compile phases only).
Never on this box: `npm run bundle`, `bundle.js --release`, `tauri signer` with any key, `pkgbuild`, `installer`,
`dpkg`, `apt-get install`, `sudo`, the e2e server, the hosts file (FR-090). R-17's WSL `apt-get install` is the user's own
setup step, run by the user before the build; no agent, gate or lane runs it.

---

## 8. Build integration and gate lanes

### 8.1 Workspace and scripts

Workspace members unchanged (`crates/core`, `crates/helper`, `src-tauri`); `exclude` gains `tests/fixtures/updater-refusals`.
`package.json` scripts unchanged (`bundle` still `node scripts/bundle.js`; release mode is invoked by CI with arguments).

### 8.2 Workflows changed

`release-manager.yml` (new), `manager-ci.yml` (four `check` steps, §4.11: `clippy updater-e2e`, `clippy updater-refusals`,
`updater refusals`, `updater-e2e absent`), `feed-tools.yml` (paths, PLAN, NAMES). `feed.yml`,
`release.yml`, `ci.yml`, `docs.yml`, `backfill-sign.yml` unchanged (FR-092).

### 8.3 Gate lanes for this phase

- **App lane:** §7.4 steps 1-4 (step 3 in WSL under D9's fallback, PD-33).
- **Pipeline lane:** §7.4 steps 5-6.
- Both lanes run concurrently (they share no output dir). No C++ lane.

### 8.4 Cost notes (CLAUDE.md "Optimise every build and test run")

- The harness build compiles `tauri` once more with feature `test` (a separate feature hash); sharing
  `CARGO_TARGET_DIR="$PWD/target"` and `Swatinem/rust-cache` keeps it to the delta. Its 26 scenarios run as concurrent
  processes, so its run time is the slowest scenario (`stall`, ~1-2 s) plus process start, not the sum; `socks-proxy`
  runs its two routes in one child, one after the other (two short good paths, well under `stall`).
- `clippy updater-e2e` (PD-32): `updater-e2e = []` changes no dependency's features, so on each leg it re-checks only
  the `krate-manager` crate against the dependency metadata the `clippy` step already built (seconds, not a second graph).
- The `socks` feature changes the feature hash of `reqwest` and the crates above it (the updater, `krate-manager`, the
  harness), so T001's first build after it recompiles those few crates once; nothing below `reqwest` rebuilds.
- Under D9's fallback the WSL harness build is a cold tauri + wry + WebKitGTK compile on WSL's own file system (one-off;
  its wall clock measured and recorded by the first run; ceiling 40 min, as Phase 10's cold WSL lane,
  `specs/krate-manager-phase10/evidence/wsl-lane.js:16-17`, `:35`), run beside the WSL core lane in its own target dir so the
  two share the box's cores instead of queueing on one build lock.
- `release-manager.yml`: after `preflight` and the seconds-long `main-only` gate, `build` (3 legs) and `updater-e2e`
  (3 legs) start together (`updater-e2e` needs only `preflight`); `sign` / `sign-dry` (one ubuntu job, minutes: the
  uncached `rsign2` compile, PD-35, is the bulk; it starts after `build` and is the price of not trusting a shared cache
  where the real key will exist) runs beside `package-macos` and `first-install`, not on the critical path; `updater-e2e` installs `rsign2` fresh on each leg too (a few minutes per
  leg, overlapping nothing on that leg; accepted: that job's builds take far longer, and a cache there would make the
  copied steps differ from `sign`'s); `package-macos`
  waits only for `build (macos-latest)`'s artifact through `needs: build` (GitHub cannot depend on one matrix leg: the
  whole `build` matrix gates it; accepted, the macOS leg is the slowest anyway). `first-install` (4 legs) runs after
  `package-macos`. Expected wall clock ≈ max(universal macOS build, two e2e universal builds) + package + first-install.
- macOS e2e builds universal twice (PD-22); the second differs only in merged config, so cargo rebuilds only the
  `krate-manager` crate and relinks.

---

## 9. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | **WebKitGTK on Linux runners / containers**: the app's webview may fail under `xvfb-run` (GPU, DMA-BUF) | Verdicts do not depend on the webview: `first-install` asserts process presence (FR-077), `updater-e2e` drives the update from the `setup` hook (PD-14). If the process exits on a webview error, the measurement is recorded and surfaced; `WEBKIT_DISABLE_DMABUF_RENDERER=1` is a candidate env for the job only (the Phase 12 user toggle stays the product answer, roadmap lines 900-902) |
| R-2 | **Windows WebView2 in a hosted runner session** | Same as R-1 (marker written in `setup`) |
| R-3 | **Windows file locks**: the NSIS update replaces `krate-manager-helper.exe` and the app's own exe | The app exits before the setup runs (`updater.rs:1050`); the busy rule (FR-015, FR-019) refuses while the helper may run; NSIS `CheckIfAppIsRunning` (`installer.nsi:690`) covers a second instance. Plugin DLLs are not involved |
| R-4 | **Admin prompts**: the pkg-installed macOS first self-update prompts (D11); `.deb` uses `pkexec` / `sudo` | The UI warns on macOS only (FR-024); refused prompt → `E_SELF_UPDATE_INSTALL`, app unchanged (gated by `pkg-installed`); a `.deb` install never self-updates (PD-24): the banner offers "Download the new .deb" (the release page, PD-31) and no install button; a direct install call is refused `E_SELF_UPDATE_UNSUPPORTED` before any download (gated by harness `deb-install`, which also asserts the action, and e2e case `deb`) |
| R-5 | **The macOS prompt cannot be observed or cancelled on a runner** (no GUI session, or `SecurityAgent` respawns) | PD-13's loop; any other outcome is recorded and surfaced to the user (FR-072); never dropped |
| R-6 | **Offline / proxy**: no connection, PAC failure, a SOCKS proxy | Offline → `E_SELF_UPDATE_NETWORK` muted line; PAC failure → `route_choice` falls back like `get` (`http.rs:938-944`); **a `socks4://` / `socks5://` proxy**: decided by ruling P-1 (§10): `src-tauri`'s direct `reqwest` enables `socks`, unified into the updater's client (PD-30), so self-update takes the route plugin downloads take; proven by harness `socks-proxy` wherever the harness runs. Residual: `socks4a` / `socks5h` (proxy-side DNS) stay refused by the core for both paths alike (`http.rs:253-254`). (Revision 2 said `Proxy::all` of a SOCKS URL fails without the feature; it does not, `proxy.rs:124-137`: without the feature the failure is at connect, `connect.rs:783-792`) |
| R-7 | **GitHub changes its release-asset redirect host** away from `release-assets.githubusercontent.com` | The download is refused `E_SELF_UPDATE_URL { Host }` and a manager that cannot update itself needs a manual install. No test is added for it (no FR backs a live test, and none may run in a gate, FR-090); this plan's compliance note hands Phase 12's user-performed checks one item: before each manager release, read the `Location` of one real release-asset download by hand and confirm its host is `RELEASE_ASSET_HOST` |
| R-8 | **Redirect hops through a host on the static proxy's `no_proxy` list** use the first URL's proxy decision (one reqwest client per fetch) | Accepted: GitHub hosts are not on corporate bypass lists in practice; recorded |
| R-9 | **`release_for_restart` sees `StillShared`** once Phase 12's settings screen calls `apply_settings` | PD-10 logs it; Phase 12's settings command must call `action_gate` first (added to Phase 12's inputs by this plan's compliance note) |
| R-10 | **The harness still fails to load on Windows** with the manifest embedded | Ruling P-5 (PD-33): no stop; the measurement goes to `evidence/d9-windows.txt`, the harness is still built and linted on Windows, runs in WSL locally and on the macOS / Linux CI legs, and SC-007's Windows row is a recorded gap quoting the load error |
| R-11 | **Dry-run `feed-dry` with `contents: write`** | `feed.yml`'s `deploy` is skipped for `dry_run: true` (`feed.yml:104`); the shape test pins `with: dry_run: true` on `feed-dry` |
| R-12 | **`bundle.js` asset globs drift with a new bundler** | `E_BUNDLE_ASSET` names the glob; the CLI is pinned (`package.json`) |
| R-13 | **`TAURI_SIGNING_PRIVATE_KEY` as a path (every build leg)** depends on the CLI's "points to a file" branch (`bundle.rs:290-298`) | Every build leg, dry or real, proves it; a regression fails the leg loudly |
| R-14 | **Windows known folders escape the harness's env redirection** (PD-11): a dependency that resolves `FOLDERID_LocalAppData` / `RoamingAppData` would write into the real profile | The harness never reaches a code path that should write there (no install, no restart, no path API: `harness_source_findings`); the read-only snapshot of the app-identifier folders reports a new entry as `not ok confinement`; a write elsewhere under the real known folders is not detectable and is recorded as the check's limit (SI-29) |
| R-15 | **Third-party code in the `sign` job** (`ajv` loaded by `minisign.js`; `rsign2` 0.6.7 and its locked graph) while the real key exists, and **a planted `rsign` binary from a shared cache** | `npm ci --ignore-scripts`; `rsign2` compiled fresh by `cargo install --locked` in the job, with no `actions/cache` / `Swatinem/rust-cache` step (PD-35, shape-tested with a plant), so no binary another workflow wrote reaches the key: the cache key `rsign2-0.6.7-<os>-<arch>` is saved by `feed-tools.yml` after an `npm ci` with lifecycle scripts on (`feed-tools.yml:62`, `:65-76`) and by `backfill-sign.yml` (`:161-175`, `:243-257`), and `rsign --version` proves only what the binary prints; both installs happen before the key exists, and only `minisign.js decrypt-key` and `rsign sign` run while it does (PD-23). Residual: crates.io's `rsign2` 0.6.7 graph at its locked versions, and build scripts that ran before the key step. **Follow-up for the user (out of this phase, FR-092):** `release.yml:610-624` restores the same cache key in the plugin pipeline's signing job, which decrypts the same key; dropping that restore (or `--ignore-scripts` on `feed-tools.yml`'s `npm ci`) is the user's call |
| R-20 | **The macOS pkg-installed self-update swaps a user-extracted, unre-verified tree into `/Applications` as root** (updater 2.14.1 `install_inner`: un-tar into a user-owned temp dir, chmod 0755, then the root `do shell script` swap, `updater.rs:1486-1636`, `:1688-1691`), helper included, which Phase 10 later runs as root; a user-level process could modify the tree between extraction and the swap | Accepted residual risk: D11 / Phase 10 D19, decided 2026-11-01 (`spec.md:283-292`); Phase 11 changes nothing here. The archive is signature-verified before extraction; the window is between the user's extraction and the authorization; after the first such update the bundle is user-owned (D11). The refused-prompt half is gated by `updater-e2e` `pkg-installed`; the accepted-prompt half is Phase 12's user-performed check (FR-093 (c)) |
| R-21 | **linuxdeploy fails to strip `.relr.dyn` libraries** (tauri issue #14796, §1.2) on the first AppImage build anywhere, a `release-manager.yml` dispatch (no push-time lane builds an AppImage) | `NO_STRIP: 'true'` on every Linux `bundle` step (PD-34), shape-pinned on `build (linux)` and both `updater-e2e (ubuntu-24.04)` builds; cost: a larger AppImage (unstripped bundled libraries). Any other linuxdeploy failure fails the `bundle` step loudly (`failed to run linuxdeploy`); the first dry run surfaces it, and a fix touches both legs together, never one |
| R-22 | **A macOS manager started through a symlinked path** cannot self-relaunch (`current_exe` refuses symlinks, §1.2) | PD-37 refuses `E_SELF_UPDATE_UNSUPPORTED` before any download, so the app never updates on disk and then quits silently; the dangerous symlink feature stays off (`config_guard.rs:931-960`). Not exercised on CI (the e2e cases use real paths, which prove the pre-check does not refuse a normal install); recorded as a known limit, the refusal side pinned only statically (§4.12) |
| R-16 | **The CLI's key-mismatch warning becomes an error in a later CLI** (the throwaway-key build would then fail) | The CLI is pinned at 2.12.4 (`package.json`); `leg key is not the release key` reads the warning on every leg, so a changed behaviour shows on the first dry run after a CLI bump |
| R-18 | **The WSL harness run cannot start** under D9's fallback (R-17's packages absent, or WSL unavailable) | `harness-lane.js` reports `HARNESS LANE RED: WSL <reason>`, never a green: the close needs one green local harness run. If the packages cannot be installed, the user re-rules R-17 (alternative (b): CI-only evidence for SC-007) |
| R-19 | **The harness SOCKS relay is simpler than real SOCKS servers**, so `socks-proxy` passes where a real server would fail | The relay implements the subset reqwest sends for `socks4` / `socks5` (no-auth, CONNECT, IPv4 targets, because reqwest resolves locally, `connect.rs:538-541`); the client side is hyper-util's `SocksV4` / `SocksV5`, which the manager does not touch. SOCKS5 user / password auth is not exercised (the relay offers only method `00`); recorded as the scenario's limit |

---

## 10. Precondition table (for the user)

Revision 2's three open questions were ruled by the user on 2026-11-01 (`spec.md:1138-1145`). Rows P-1 to P-6 record each
ruling and where the plan now carries it; R-17 is the one new decision those rulings open (R-numbers continue §9's, so
every id in the plan is unique). The plan is written to R-17's recommendation.

| Id | Question | Status | Ruling / recommendation | Where the plan carries it |
|---|---|---|---|---|
| P-1 | SOCKS proxies for self-update (revision 2 Q1, R-6) | **decided** 2026-11-01 (`spec.md:1140`), overturns "built without it" | Enabled: direct `src-tauri` `reqwest = { version = "=0.13.5", default-features = false, features = ["socks"] }`, unified into the updater's client; the refusal harness gains a SOCKS route case | PD-30, SI-28, SI-30; §4.3, §4.7 `socks-proxy` (a harness scenario, the harness hosts a std-only SOCKS relay; not a unit row, PD-30 says why), §4.12 pin and plants, §5, R-6, R-19; locks re-locked by T001 (no package change: `socks = []`) |
| P-2 | The release key's home (revision 2 Q2 (a), PD-23, SI-20) | **decided** (`spec.md:1141`): accepted as written | Throwaway key per build leg; `sign` (`environment: signing`) re-signs with the real key; `sign-dry`; `main-only` gate | PD-23, SI-20, §4.10, §6 "Keys" (unchanged text) |
| P-3 | A `.deb`-installed manager when an update exists (revision 2 Q2 (b), PD-24, SI-21) | **decided** (`spec.md:1142`), changes the plan's button-that-refuses UI | Never self-update a system-package install; the banner shows the version, the notes and "Download the new .deb", which opens the release page through the existing `open_release_page`; `manager_update_install` still refuses `E_SELF_UPDATE_UNSUPPORTED` as the backstop | PD-24, PD-31 (`UpdateAction`; the page URL admitted by `open_release_page`'s own `status::release_page_url` check, verified `status.rs:350-365`), SI-21, SI-31; §4.1 `update_action`, §4.3 `check`, §4.6, §4.7 `deb-install` / `good`, §7.1 SC-003 rows, R-4 |
| P-4 | Lint the `updater-e2e` feature build in Manager CI (TA-14) | **decided** (`spec.md:1143`), overturns "no other step changes" | `check` gains `clippy updater-e2e` on all three legs, dummy `KRATE_E2E_*` values, never run, pinned by `manager-ci-shape.test.js` `STEPS` / `COMMANDS` / `ASSERTION_STEPS` (it prints a notice) | PD-32, SI-32; §4.11, §7.1 SC-012 / SC-017, §7.3, §7.4 step 1, §8.2, §8.4 |
| P-5 | D9: the harness fails to load on Windows | **decided** (`spec.md:1144`), supersedes "stops the build" | No stop: the harness then runs on macOS and Linux only (locally in WSL, and on those CI legs); compliance records the Windows row as a gap quoting the load error; T014 never returns `blocked` for it | PD-33, SI-33; §4.7 "First local run", §4.11 `updater refusals` `if:`, §7.3 SC-007 Windows row, §7.4 step 3, R-10, R-18 |
| P-6 | Drop `zip` (SI-3, PD-2); T002 writes the SI rows in G2; TA-11; TA-12 | **decided** (`spec.md:1145`): all accepted as planned | `zip` dropped; T002 writes SI-1 to SI-29 (and this revision's SI-30 to SI-33, which encode P-1, P-3, P-4, P-5) in G2; TA-11 and TA-12 as written in `tasks.md` | PD-2, SI-3; `tasks.md` T002, TA-11, TA-12 |
| **R-17** | **WebKitGTK in WSL for P-5's local WSL harness run.** WSL Ubuntu 24.04.3 on this box has cargo but none of the packages a `tauri`-linked binary needs (measured this session, §1.1: no `webkit2gtk-4.1`, `javascriptcoregtk-4.1`, `libsoup-3.0`; Phase 10's lane says the same, `wsl-lane.js:11`), so the harness cannot build in WSL today. Installing them changes the user's environment, so only the user can do it. It matters only if T014 measures `D9 WINDOWS LOAD FAILURE` | **open** (the user's) | **Recommendation (a):** before the build stage, the user runs in WSL the CI Linux leg's exact package line, `sudo apt-get update && sudo apt-get install -y libwebkit2gtk-4.1-dev build-essential curl wget file libxdo-dev libssl-dev libayatana-appindicator3-dev librsvg2-dev` (`manager-ci.yml:53-54`); then P-5's "locally in WSL" holds and a Windows load failure still leaves a local harness pass. **(b)** Do not install: under a load failure SC-007 has no local row at all; its evidence is only `check (macos-latest)` / `check (ubuntu-24.04)` (PENDING-CI) and the close lane records the WSL row as not run. **(c)** Install only if T014 measures a failure (the build then pauses at the G2 gate until the packages exist; this is a stop, which P-5 rules out, so not recommended) | written to (a): PD-33 (b), §7.4 step 3, `harness-lane.js` (`tasks.md` T016) runs the WSL build, R-18 |

---

## Review notes

Plan-challenge round of 2026-11-01 (21 issues: spec-coverage and safety-security lenses). Every issue was applied; none
was rejected, and no threshold was relaxed. Choices where the issue offered options:

- Progress (FR-024): a `Channel<SelfUpdateProgress>` (PD-25, SI-22).
- FR-070's `lib.rs` arms: reworded by SI-25 to "the module plus exactly two named call sites", the `compile_error!`
  moved inside the module, and a per-file count guard (PD-26); the existing `dev-feed` arms are not touched.
- SC-024 / SC-025: `UpdateTimeouts` and `self_update_http` moved into core, plus the harness `good` assertion and
  `injection.rs` pins for the `src-tauri` half (PD-29, SI-24).
- `map_error` table: a counted harness line `map-errors` (31 constructed variants) rather than a separate mode; FR-082's
  count becomes 24 together with the three new scenarios (SI-23). Revision 1's variant count (30) was wrong; the spec's
  32 stands (SI-1).
- FR-081 (d): the key check moved into every child, the plant routed through the key-producing function (SI-23).
- FR-015 / FR-023: busy checked before the slot is taken (PD-28). (Revision 4: the "no SI needed" said here was wrong;
  SI-36 rewords FR-023.)
- The release key (safety): option A, re-signing in a separate `sign` job; verified first that Tauri CLI 2.12.4 only
  warns on a key / pubkey mismatch (`bundle.rs:313-318`), so the CLI does not need the real key (PD-23, SI-20). A
  `main-only` gate keeps D14's main-only dry runs. This also removes the private key from every artifact (the
  `dry-key` issue) and makes dry and real builds identical.
- `.deb` self-update (safety): option (a), refuse (PD-24, SI-21); the alternative ruling is open question 2 (ruled in
  revision 3: P-3, §10).
- Package origin: PD-27 / SI-27. Proxy errors fail closed: PD-6 / SI-26. Harness source bans and the Windows
  known-folder snapshot: SI-28, SI-29.
- R-7's live test: dropped rather than given an FR; its mitigation becomes a Phase 12 manual check.

Revision 3, 2026-11-01: the user's six plan-precondition rulings (`spec.md:1138-1145`), encoded. No threshold was
relaxed and no earlier PD / SI row was dropped; each change traces to one ruling:

- P-1 (SOCKS on): PD-30, SI-28's line, SI-30, the harness scenario `socks-proxy` with a std-only relay (the harness can
  host a SOCKS listener, so it is a harness case, not a unit row), the `reqwest` pin and its plants, R-6 rewritten, R-19.
  Verified first that the feature adds no crate (`socks = []`, reqwest 0.13.5 `Cargo.toml:178`), so the three locks
  are re-locked without a package change. Correction to revision 2's R-6: `Proxy::all` accepts a SOCKS URL without the
  feature; the failure is at connect.
- P-2 (PD-23) and P-6 (`zip`, T002, TA-11, TA-12): marked decided; no other text changed.
- P-3 (`.deb`): PD-24 rewritten, PD-31 (`UpdateAction`, `update_action`), SI-21's edge-case text, SI-31; the UI
  reuses `releaseButton` and `open_release_page`, whose `github.com` allow-list admits the tag page (checked,
  `status.rs:350-365`); two vitest titles (vitest 68 → 70 in `tasks.md`); core rows inside the existing
  `packaging_rule` and `view_from_update` (cargo counts unchanged).
- P-4 (`clippy updater-e2e` in CI): PD-32, SI-32, §4.11's four steps and the shape-test pins; "no other step changes"
  now means no step beyond those four.
- P-5 (D9 without a stop): PD-33, SI-33, the `d9-windows.txt` measurement file, the `if:` rule and
  `HARNESS_ON_WINDOWS`, R-10, R-18; the WSL run it needs opened R-17 (§10), the one new decision for the user.

Revision 4, 2026-11-01: second plan-challenge round (14 issues: spec-coverage, safety-security and xplat-reality
lenses). Every major and every minor was applied; no threshold was relaxed (the one exemption revision 3 had added, in
`build warnings`, is removed). Where an issue offered options, or part of a suggestion was not taken:

- rsign path vs the updater's verifier (major): both halves, PD-35 / SI-34: `updater-e2e` signs its "new" asset with
  `sign-dry`'s exact steps and serves those `.sig`s; harness `rsign-format` over a committed rsign fixture. A new
  fixture rather than the KAT: the KAT's trusted comment has no `version:` field (§1.1).
- Production inputs (major): both options, PD-36 / SI-35: the harness line `production-inputs` and the `injection.rs`
  body pin, plus `LoopbackOnly` confined to `mod e2e` and the e2e inputs built on `UpdateInputs::production`, so the
  e2e `deb` case also proves production's `packaging()`. The harness line alone cannot see `packaging` (both sides read
  `SelfContained` unbundled), which is why the body pin is not optional.
- Plant mode (minor): the first option, PD-38 (only the `good` child is spawned); no SI needed, the spec text stands.
- FR-023 (minor): SI-36; `busy`'s second install was already in SI-23 and is now named in FR-080's row by SI-36.
- `build warnings` exemption (minor): measured from source instead of on a dry run: the CLI prints the warning as
  `Warn …` (`tauri-cli` 2.12.4 `lib.rs:246-262`, `:310-318`), so the exemption is dropped and the manager-ci rule is
  copied unchanged; no SI.
- FR-025 wiring (minor): the end-to-end case moved to `src/main.test.ts` under its existing title (vitest count
  unchanged).
- SC-021 `relocate` (minor): `xmllint` count of `relocate`'s children, and SI-37 rewords SC-021 to "absent or empty",
  the semantics the cited sources give; the named fixture is the first dry run's `PackageInfo`, recorded at compliance,
  because FR-090 bars `pkgbuild` on this box.
- FR-090 write set (minor): the SI option (SI-38), not the `HOME` / `XDG_DATA_HOME` redirect, because the macOS cases
  launch through `open -n`.
- Package origin (major, safety): applied, PD-27 / PD-31 / SI-40, under both URL policies, and `update_action` now
  checks the URL for a self-contained install too. **Changed from the suggestion:** the issue asked to pin
  `MANAGER_REPO` "by `config_guard` against `feed-publish.js`'s repo"; `feed-publish.js` holds no repo constant (it
  takes `--repo "$GITHUB_REPOSITORY"`, `feed.yml:60`; `feed-publish.js:66`, `:958-959`), so the pin is
  `manager-release-preflight.js`'s `E_PREFLIGHT_REPO`, which ties the dispatching repository (the value every
  `latest.json` URL is built from) to the compiled constant before any build. §6 now states what the `.deb` page still
  trusts the unsigned `latest.json` for (the announced version).
- `rsign` cache (major, safety): the first option, PD-35 (c): fresh `cargo install --locked`, no cache, shape-tested.
  The second option (signing with `tools/minisign.js` so no external binary holds the key) was not taken: `minisign.js`
  has no `sign` subcommand (§1.1), so it would mean a new CLI surface on the Phase 2 signing tool, and the manager's
  `.sig`s would then come from a different signer than the plugin pipeline's (`release.yml` signs with rsign); the
  fresh install closes the cache vector the issue names, and PD-35 (a) / (b) now prove rsign's output against the
  updater. Still open to the user as a later hardening. R-15 rewritten, the `release.yml` exposure recorded as a user
  follow-up.
- §6 root sentence (minor): rewritten as suggested; R-20 cites D11 / D19.
- Linux e2e host (major, xplat): **the container option is rejected**: FR-072 / FR-076 require the AppImage to run with
  `libfuse2t64` and without `APPIMAGE_EXTRACT_AND_RUN` (`spec.md:650-652`), FR-091 bans that variable in
  `updater-e2e` (`spec.md:779`), and a job `container:` has no FUSE device, so the e2e AppImage could not run the way a
  user runs it. The second option is applied in full: `NO_STRIP: 'true'` on every Linux `bundle` step, release and
  e2e, shape-pinned (PD-34), and R-21. The build-host divergence is recorded in PD-34 with why it does not weaken what
  the e2e leg proves.
- macOS symlinked relaunch (minor): the first option, PD-37 / SI-39 (refuse `E_SELF_UPDATE_UNSUPPORTED` before any
  download); no e2e symlink case, recorded as R-22; the dangerous feature stays off and `config_guard` already makes
  enabling it red.
- Follow-ups for `tasks.md` (not edited here): cargo required names +2 (§4.13), harness count 27 (T013 / T014 / T017),
  the `make-rsign-fixture.js` step before T014, the new shape-test rules (§4.12), `E_PREFLIGHT_REPO` in the preflight
  task, and the `.gitattributes` line.
