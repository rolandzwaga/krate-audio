# Tasks: Krate Manager Phase 9 — Linux Install, Update and Uninstall (unprivileged)

**Spec:** `specs/krate-manager-phase9/spec.md` (DRAFT revision 2: D1-D16, FR-001 to FR-124, SC-001 to SC-030;
Clarifications Q1-Q8 of 2026-11-01, `spec.md:1079-1094`) · **Plan:** `specs/krate-manager-phase9/plan.md` (DRAFT
revision 2: PD-1 to PD-30 `plan.md:35-68`, SI-1 to SI-27 `plan.md:70-100`, "Not an issue" `plan.md:102-106`, the
precondition table R-1 to R-3 `plan.md:1129-1135`, review notes `plan.md:1139-1179`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 9 (lines 663-705); Phase 10, the next `InstallDriver` (lines 707-808);
Phase 12 (lines 877-930); Cross-Cutting Constraints (lines 984-1025)
**Status:** DRAFT task list, revision 1, 2026-11-01. Written against `feat/krate-manager` at `abdd2c9e` (`git rev-parse
--short HEAD` this session; `git status --porcelain` shows only `?? specs/krate-manager-phase9/`; the last commit touching
`apps/krate-manager/` is `abdd2c9e` itself). Every app file:line quoted below was re-read this session at `abdd2c9e`.
**Spec state:** none of plan §0.2's edits is in `spec.md` yet. Counted this session with `grep -c` over `spec.md`:
`command_request_for` 0, `request_seam` 0, `DriverResolver` 0, `QuotaExceeded` 0, `Download { event` 0,
`install::channel_progress` 0, `parse_manifest` 0, `from_parts` 0, `temp_roots_with_user_presets` 0,
`driver_rechecks_state` 0, `recovery_table_rows` 0, `batch_runs_every_action` 0. The plan calls SI-1 to SI-4 blocking
(`plan.md:16-23`) because the spec, *as text*, would build a red tree. Every task below states its assertions from the
plan's §0.1 / §0.2 / §4 / §7 rows, which are binding, so no build task waits for the spec text; T002 writes the edits into
`spec.md` in G2 beside the code (the Phase 8 precedent, `specs/krate-manager-phase8/tasks.md` T002).
**Lanes in this phase:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm
typecheck / lint / test, the version lint, the `cargo tree` record), the **pipeline lane** (`node --test` over
`tools/feed-contract/tests/`, because `manager-ci-shape.test.js`'s `EXPECTED_REQUIRED` changes; the repo lints; the
evidence scripts' `--self-test`) and **L-WSL** (core clippy + tests in WSL Ubuntu: the only local compile of the
`cfg(unix)` code; a main-loop lane). No file under `plugins/`, `dsp/`, `cmake/`, `extern/` or `.github/workflows/`
changes (spec FR-121, `spec.md:726-728`; plan §2 `plan.md:211-212`), so there is **no C++ lane**: no `run-gate.js`, no
`header-consumers.js`, no `check-portability.js`, no clang-tidy, no pluginval, no `lint-plugin-editor-baseline.js` (no
plugin editor changes). **No workflow edit**: `manager-ci.yml` already runs every step this phase needs on all three OSes
(plan §8 `plan.md:1101-1102`), so the per-OS CI additions are the `required-tests.json` lanes (T001) and their check
(T009). **No dependency changes**: no `Cargo.toml`, no `Cargo.lock`, no `tests/fixtures/clippy-guard/Cargo.lock`, no
`package.json` (spec FR-111, plan §5 `plan.md:930`), so no task re-locks the clippy-guard fixture; a task that finds it
needs a crate stops and returns `blocked` (Rule 8).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks at the same time in one working tree. A task is
  **[P]-safe** when no other task in its group touches any of its files and none of its files is a **shared file**:
  any `Cargo.toml`, `Cargo.lock` (the workspace's and `tests/fixtures/clippy-guard/Cargo.lock`), `clippy.toml`,
  `crates/core/src/lib.rs`, anything under `src-tauri/` (commands, `lib.rs`, `build.rs`, the capability file, the
  permission files, `tauri*.conf.json`), `package.json`, a workflow, `tests/required-tests.json`,
  `manager-ci-shape.test.js` or `specs/INDEX.md`. **Every shared file this phase edits is edited in one task only,
  T001, which is G1 on its own (sequential)**: it writes the two `lib.rs` lines, the whole public surface of
  `install.rs` (placeholder bodies), the public surface of the Phase 9 test doubles (placeholder bodies), the small real
  changes in `download.rs`, `detect.rs` and `status.rs` that other tasks compile against, the 13 status goldens and the
  `view.ts` field that keep both golden suites green, the archive-helper move, the two `src-tauri` commands with their
  registration and guard, the guard rows those edits move, and `required-tests.json` with `EXPECTED_REQUIRED` (TA-1).
  The G2 tasks then touch only their own files: a G2 file is either new in this phase (created by T001 as a placeholder
  for exactly one G2 task) or an existing non-shared file that exactly one G2 task edits (`spec.md` + the roadmap →
  T002; `install.rs` + `install_tests.rs` + `context.rs` + `context_tests.rs` → T003; `test_support.rs` +
  `test_support_tests.rs` → T004; `tests/injection.rs` + `tests/roster.rs` → T005; `status_tests.rs` → T006;
  `src/status.ts` + `src/view.ts` + `src/status.test.ts` + `src/main.ts` → T007; the three evidence scripts → T008).
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block is too long to copy without drift (a type
  list, a test table, an algorithm), the section names the `plan.md` section or §7.1 row, or the spec FR / SC line
  range, that holds it; that text is then **binding, verbatim**, as amended by plan §0.1 / §0.2 (`plan.md:35-100`) and
  this file's TA rows. Where this file and the plan differ, this file wins and the TA row says why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the notes quote the failing line. The red
  run normally uses T001's placeholder, so the test compiles and fails on the `todo!()` panic ("not yet implemented: T00n")
  or on its assertions. A test that cannot run on this box (`cfg(unix)`, `cfg(target_os = "linux" | "macos")`) has no
  local red: its red-first is recorded as "not compiled here", and it is read twice against its SC row before the task
  ends (Rule 15).
- **In-group expected reds (G2 only).** (a) T003's rows that run over a Phase 9 double (`MemInstallIo`,
  `FaultyInstallIo`, `RecordingInstallIo`, `CountingDriver`, `RecordingDriver`, `OneDriver`, `InstallRig`) fail with
  `not yet implemented: T004` raised **from `crates/core/src/test_support.rs`** until T004 lands. (b) T004's
  `phase9_doubles_cases` rows that forward to `FsInstallIo` fail with `not yet implemented: T003` raised **from
  `crates/core/src/install.rs`** until T003's `FsInstallIo` lands. (c) T006's `status::tests::actions_per_state` fails
  with `not yet implemented: T003` (from `install.rs`) or `not yet implemented: T004` (from `test_support.rs`) until both
  land. (d) T005's real-tree rows report T003's in-progress `install.rs` whenever it really breaks a rule; that is a real
  finding for T003 to fix, not an expected red. Those three panic texts are the only failure texts that count as
  expected, and **every one is gone by the G2 gate** (all of G2 has landed by then); any red at a gate is a real red. A
  **compile error is never an expected red**: every core unit test lives in one test binary, so a file that does not
  compile stops every G2 Rust task's loop (Rule 10).
- **Main-loop lanes.** Steps longer than ~5 minutes, or that need WSL, run only in the main loop, from scripts T008
  writes: `specs/krate-manager-phase9/evidence/wsl-lane.js` (L-WSL; Phase 8 measured `WSL LANE GREEN (294 passed,
  70 s)`, 79.5 s wall, `specs/krate-manager-phase8/evidence/timings.md` row A10; this phase's first WSL run compiles the
  new module and every `cfg(unix)` test cold) and `specs/krate-manager-phase9/evidence/close-lane.js` (L-CLOSE: the plan
  §7.4 procedure, the `tauri build` rows (TA-11), L-WSL and the records; Phase 8's took 80.8 s without a `tauri build`,
  Phase 7's 157.0 s with a 102.5 s one). T009 returns with notes "deferred to the main loop at close: run `node
  specs/krate-manager-phase9/evidence/close-lane.js`".
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system.** Every test builds its roots from `test_support::fresh_temp_roots()`
   (`crates/core/src/test_support.rs:175-179`, returns `(TempDir, Roots)` sandboxed to the `TempDir`) or, for SC-023
   only, `temp_roots_with_user_presets` (T004) on a fresh `TempDir`, and writes only inside that `TempDir`; paths outside
   the roots a test needs (`outside/`, `elsewhere-state/`, a canary) are built with `Path::join` from the same base. The
   `AppContext` is a literal over those roots (the `download_tests.rs:148-156` pattern: `Settings::default()` or a
   `Settings { linux_plugin_root: Some(…), .. }` inside the `TempDir`, `HttpConfig::loopback(…)`, the fixture server's
   `/versions.json` URL, `signing_key` a `TestSigner` key). **Every refusal row runs over
   `RecordingInstallIo::fail_closed()`** (spec FR-115, `spec.md:705-709`) and asserts `mutating_calls() == 0`. The only
   network peer is the loopback `FixtureServer` (`crates/core/src/fixture_server.rs:136-197`); asset URLs reach it only
   through the test-only `request_seam` set in `install_tests.rs` (plan PD-2, PD-30). No test calls `github.com`,
   `githubusercontent.com`, `krateaudio.com` or `api.github.com`, acquires the real state lock, reads or writes a real
   `~/.vst3`, `/usr/lib/vst3`, `~/Documents/Krate Audio`, VST3 / Components / preset folder, `ProgramData`, the registry
   or the real state dir. **No Phase 9 test starts a process** (FR-115; `TEST_PROGRAM_BODIES`,
   `crates/core/tests/injection.rs:447-467`, gains no row). No test calls `std::env::temp_dir`, `var`, `var_os`,
   `home_dir` (`apps/krate-manager/clippy.toml`). No private key is written to any file: `TestSigner` keys live in
   memory (`crates/core/src/test_signer.rs`). Committed fixtures under `tools/feed-contract/fixtures/` are read in place
   and never modified (FR-114); versioned archives are built at test time (plan §4.4). No task launches the built app,
   the elevated helper or an installer.
2. **No lint suppression added, of any lint.** `[workspace.lints.clippy] allow_attributes = "deny"`,
   `allow_attributes_without_reason = "deny"` (`apps/krate-manager/Cargo.toml:50-52`): never `#[allow]`. This phase adds
   **no `#[expect(…)]`** (SC-026, `spec.md:951-954`). No new `clippy::disallowed_methods` site: `D6_SITES` / `D6_TOTAL`
   stay as they are (`injection.rs:92-100`); T004's `roots_at_with` refactor keeps the single
   `#[expect(clippy::disallowed_methods, reason = "temp_roots is the test constructor")]` `Roots::new` site
   (`test_support.rs:120-126`) with its reason text. No `// @ts-ignore`, `@ts-nocheck`, `eslint-disable`. A warning is
   fixed in the code, never silenced.
3. **Unit tests live in `<stem>_tests.rs`**, declared at the end of `<stem>.rs` as `#[cfg(test)] #[path =
   "<stem>_tests.rs"] mod tests;` (an inline `mod tests {` is reported by `injection.rs`).
4. **Literal hygiene.** (a) `install.rs` holds, comments included, none of `FORBIDDEN_OUTSIDE_PLATFORM`
   (`injection.rs:23-31`: `"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`, `krateaudio.com`, `api.github.com`),
   no `Path::display` / `{:?}` of a path in a log line (FR-117, plan §4.1.14), and no plugin name. (b)
   **`test_support.rs`, `status_tests.rs`, `src/status.test.ts` are in `FR088_FILES`** (`injection.rs:248-255`), and
   T001 adds `install_tests.rs` and `tar_builder_tests.rs` (TA-8), so nothing in those files, comments included, may
   match an `FR088_PATTERNS` entry (`injection.rs:257-269`: `--test-threads`, `127\.0\.0\.1:[0-9]+`,
   `localhost:[0-9]+`, `OsHostIo`, `HKEY_`, `/Library`, `/usr/`, `%[A-Z_]+%`, `AppData`, `ProgramData`, `Program
   Files`); `test_support.rs`, `install_tests.rs`, `tar_builder_tests.rs` are (after T001) in `PHASE7_TEST_FILES`
   (`injection.rs:186-191`), so none holds `github.com`, `githubusercontent.com`, `krateaudio.com` or `--test-threads`
   (`PHASE7_HOST_NEEDLES`, `:193-198`). The SC-024 `elsewhere` path is `/opt/vst3/Alpha.vst3` (plan SI-14). (c) The token
   `sandboxed` appears only in `roots.rs`; call `ctx.roots.check_sandboxed(…)`. (d) **No file under
   `apps/krate-manager/` names any of the nine shipped plugins** (`crates/core/tests/roster.rs`): invented names `Alpha`,
   `Beta` only. (e) `status.rs` and `detect.rs` gain no `FR084_TOKENS` token (`injection.rs:226-246`: `create_dir`,
   `rename`, `remove_file`, `symlink`, `Command`, `thread :: spawn`, …) in code. (f) Non-test TypeScript (`main.ts`,
   `notes.ts`, `status.ts`, `view.ts`) holds none of `NAVIGATION_NEEDLES` (`src/status.test.ts:356-363`): no `h` + `ref`,
   no `<` + `a` (so no `Array<action…>`, `Promise<any>`, `<a…` generic or tag; capitalised `<ActionName>` is fine), no
   `window.` + `open`; and no frontend file holds `innerHTML`, `outerHTML`, `insertAdjacentHTML`, `document.write`, an
   `http://` / `https://` literal or an `eslint-disable` (`crates/core/tests/config_guard.rs:140-148`, `:1000-1040`).
5. **Zero warnings.** `cargo fmt --all --check`; every `cargo clippy … -- -D warnings` with 0 `warning:` lines in both
   the lib and the test builds; `npm run typecheck`; `npm run lint`; `node --check`.
6. **Parallel tests.** Never `--test-threads`, `--no-file-parallelism`, `--fileParallelism=false`, `--maxWorkers`, `-j
   1`, `--test-concurrency=1`. `cargo test` uses its default threads; `vitest run` its parallel files; `node --test` runs
   files in parallel. No new test holds a process-wide mutex or a `static` lock (the static-lock rule of `injection.rs`
   covers test sources; `test_support.rs` keeps the same discipline, plan §4.5 `plan.md:784`), a fixed port, or a sleep;
   SC-014's ordering comes from `mpsc` channels local to the test (plan §7.1 SC-014). The per-plugin registry key is
   `(state_dir, stem)` (plan §4.1.12), so parallel tests on their own `TempDir`s never contend.
7. **`cargo test` is `cargo test --workspace --locked` or `cargo test -p krate-manager-core --locked …`, never with
   `--all-targets`** (that builds a harness for `src-tauri`'s lib and bin despite `test = false`). `cargo clippy
   --workspace --all-targets` is correct. `cargo test --workspace` also runs the doctests (SC-007's compile-fail
   doctest).
8. **Lockfiles and dependencies.** **No task changes any `Cargo.toml`, `Cargo.lock`, `tests/fixtures/clippy-guard/
   Cargo.lock` or `package.json`** (FR-111; `FR007_DEPENDENCIES` stays 14 names, `injection.rs:106-120`). Every cargo
   command uses `--locked`. A task that finds it needs a crate, a feature or an npm package stops, returns `blocked` and
   names it and why.
9. **Placeholder discipline (TA-2).** T001's placeholders compile with zero warnings in the lib build and the test
   build: every parameter of a `todo!()` body is `_`-prefixed; a placeholder struct whose real fields belong to a G2 task
   holds only `_private: ()` (or `_private: std::marker::PhantomData<&'a ()>` when it carries a lifetime); a placeholder
   imports only what it uses; every `todo!` names its owner (`todo!("T003")`, `todo!("T004")`). The owning task renames
   parameters, replaces `_private` and removes the `todo!`s as it implements. At close no `todo!(` or `unimplemented!(`
   remains in any workspace `.rs` file outside `*_tests.rs` and `tests/` (T009 step 6).
10. **One working tree, many writers (G2).** Write each file whole, so that it compiles at every save (types and
    signatures first, bodies after; a test that names an item you have not written yet is saved only after the item).
    Never edit a file another task owns; when `cargo` fails on a file you do not own, wait 60 s and retry, three times,
    then record it in your notes and continue with what you can verify. Run your own-module loop (L-OWN) in the
    **target slot** your task names, with its `CARGO_BUILD_JOBS` (TA-9): the shared `apps/krate-manager/target` for
    T003 (16 jobs, warm from T001, the long pole); slot `F:/tmp/km9-target/side` for T004, T005 and T006 (12 jobs; they
    queue on its build lock, which is accepted). The first run in the slot pays one cold build of core's dependency
    graph; record its wall clock. Before the first build run `df -h /f` (5.0 GB free this session, `apps/krate-manager/
    target` 14 GB): with less than 4 GB free, use the shared target instead of the slot and say so in the notes. The last
    of T004 / T005 / T006 to finish deletes `F:/tmp/km9-target/` (check `ls F:/tmp/km9-target/` and the other two tasks'
    state first).
11. **Scope fence (spec FR-120, FR-121, `spec.md:723-728`; plan §2 `plan.md:188-212`).** Outside `apps/krate-manager/**`
    only these paths change: `specs/krate-manager-phase9/**`, `specs/INDEX.md` (regenerated only, T009),
    `tools/feed-contract/tests/manager-ci-shape.test.js` (`EXPECTED_REQUIRED` only, T001), and
    `specs/Krate-manager-roadmap.md` (the three FR-123 sentences, T002; the Phase 9 status line, main loop at close).
    Inside `apps/krate-manager/` the only files that change are plan §2's: `crates/core/src/{lib,install,install_tests,
    tar_builder_tests,extract_tests,download,download_tests,detect,detect_tests,status,status_tests,context,
    context_tests,test_support,test_support_tests}.rs`, `crates/core/tests/{injection,config_guard,roster}.rs`,
    `tests/fixtures/status/*.json` (13), `src-tauri/src/{commands,lib}.rs`, `src-tauri/build.rs`,
    `src-tauri/capabilities/main.json`, `src-tauri/permissions/autogenerated/{install_plugin,remove_plugin}.toml` (new,
    generated), `src/{status,view,main,status.test}.ts`, `tests/required-tests.json` (`download_tests.rs`,
    `detect_tests.rs`, `test_support_tests.rs` gain one test each, TA-6). No change to `extract.rs`, `verify.rs`,
    `feed.rs`, `version.rs`, `roots.rs`, `settings.rs`, any `Cargo.*`, `index.html`, `package*.json`, `crates/helper/**`,
    `clippy.toml`, `eslint.config.mjs`, `vite.config.ts`, `tsconfig*.json`, `CHANGELOG.md`, `version.json`, any CSS,
    `tauri*.conf.json`, or any workflow.
12. **Line endings.** This checkout runs `core.autocrlf=true`. Measured this session (`tr -cd '\r' < f | wc -c` against
    `wc -l`): **CRLF** — `crates/core/src/lib.rs` 29/29, `crates/core/src/test_support.rs` 1784/1784,
    `crates/core/tests/injection.rs` 5306/5306, `crates/core/src/context.rs` 222/222, `crates/core/src/context_tests.rs`
    636/636, `src-tauri/src/lib.rs` 97/97, `src/main.ts` 28/28, `specs/Krate-manager-roadmap.md` 1061/1061; **LF** —
    `config_guard.rs` (0/2409), `roster.rs` (0/364), `status.rs` (0/491), `status_tests.rs` (0/1395), `download.rs`
    (0/1593), `download_tests.rs` (0/3551), `detect.rs` (0/1071), `detect_tests.rs` (0/2851), `extract_tests.rs`
    (0/3417), `test_support_tests.rs` (0/885), `src-tauri/src/commands.rs` (0/100), `src-tauri/build.rs` (0/13),
    `src-tauri/capabilities/main.json` (0/13), `src/status.ts` (0/331), `src/view.ts` (0/314), `src/status.test.ts`
    (0/390), `tests/required-tests.json` (0/367), `tests/fixtures/status/fresh-all-states.json` (0/354, the other twelve
    the same), `tools/feed-contract/tests/manager-ci-shape.test.js` (0/1047), `specs/krate-manager-phase9/spec.md`
    (0/1093). An edit keeps the file's ending on every line it adds. New files are written LF. `specs/INDEX.md` is
    written only by `tools/gen-specs-index.js`; the two permission `.toml` files only by `tauri-build`.
13. **Shell discipline.** Every command runs in the Bash tool (Git Bash), foreground, output to a log under
    `F:/tmp/gate/km9-<task id>/`, then `tail`; never `cat` a whole log, never a drive-wide `find` / `du`. **No heredoc in
    a Bash command** (a repo hook blocks them): write scripts with the Write tool and run them with `node`. Never chain
    git commands. All app-lane commands run from `apps/krate-manager/`; repo-lane commands from the repo root.
14. **No invented APIs.** Every crate API used is one plan §1.2 / §5 verified (`plan.md:172-184`, `:918-930`): std 1.99.1
    `fs::{rename, remove_dir_all, remove_dir, remove_file, create_dir, create_dir_all, read_dir, symlink_metadata,
    canonicalize, read_link, set_permissions}`, `OpenOptions::{write, create_new}`, `File::sync_all`,
    `os::unix::fs::{symlink, PermissionsExt}`, `io::ErrorKind::{StorageFull, QuotaExceeded, ReadOnlyFilesystem,
    PermissionDenied, DirectoryNotEmpty, AlreadyExists, NotFound}`, `std::sync::{Mutex, mpsc}`, `std::cell::RefCell`,
    `std::panic::{catch_unwind, AssertUnwindSafe}`, `std::thread::scope` (tests only), `OsStr` / `os::unix::ffi::OsStrExt
    ::from_bytes` (tests only); `serde` / `serde_json` / `thiserror` / `log` at their existing pins; `tauri` 2.12.5
    `tauri::ipc::Channel<T>` (`send`) and `@tauri-apps/api` 2.12.4 `Channel` (`new Channel<T>()`, `onmessage`)
    (https://v2.tauri.app/develop/calling-frontend/, `node_modules/@tauri-apps/api/core.d.ts:103-115`). Inside the crate:
    `verify::Sha256Stream` (`pub(crate)`), `verify::verify_file` (tests), `DownloadRequest::with_fixture_url` (`#[cfg(test)]
    pub(crate)`, returns `Result<DownloadRequest, &'static str>`, `download.rs:70-96`), `extract::{stage, discard, prune,
    StagedTree, CanonicalListing, NodeKind, ExtractEnv, ExtractLimits::PRODUCTION, FsExtractIo}`, `detect::{read_manifest,
    detect_plugin, moduleinfo_version, Listing::scan, MAX_MANIFEST_BYTES}`, `version::{compare, resolve,
    platform_candidates}`. An API that turns out to differ is a defect of the plan: record it under "Defects found" with
    the source line, and use the real API.
15. **`cfg` discipline.** The eight `_unix` tests compile only in L-WSL and on the macOS / Linux CI legs;
    `native_wiring_installs_linux` only on Linux, `native_wiring_offers_nothing_macos` only on macOS,
    `native_wiring_offers_nothing_windows` only on Windows (plan §4.9 `plan.md:868-875`). Write them with the same care as
    the Windows code, read each against its SC row twice, and gate every `use` the way its only users are gated (an
    unused import is a warning on the leg that compiles it). `FsInstallIo`'s `cfg(unix)` and `cfg(not(unix))` bodies
    (plan §4.1.2 `plan.md:375-386`) must both compile with no binding unused on either side.
16. **Every claim cites what was read.** A task's notes cite file:line for every fact about existing code it relied on,
    read in that task's session, and quote the test output lines (`test result:` and the named tests) that close it.

### Lanes (referenced by every task)

| Lane | Commands | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked 2>&1 \| tee F:/tmp/gate/km9-<task>/cargo-test.txt` (Rule 7; this also runs the doctests) | fmt exit 0; clippy exit 0 with 0 `warning:` lines; every test binary and the doctests `0 failed` |
| **L-OWN** | `CARGO_TARGET_DIR=<slot> CARGO_BUILD_JOBS=<n> cargo test -p krate-manager-core --locked --lib <filter>` (or `--test injection` / `--test roster` / `--test config_guard`, `--doc` for T003's doctest) and `CARGO_TARGET_DIR=… CARGO_BUILD_JOBS=… cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings` (Rule 10; a working loop, not a gate). `<slot>` per Rule 10; for T001 and T003 unset (shared target) | the task's own tests and 0 `warning:` lines |
| **L-APP** | `CARGO_BUILD_JOBS=16 cargo clippy -p krate-manager --all-targets --locked -- -D warnings` (shared target; its `build.rs` run is what writes the two permission files, T001) | exit 0, 0 `warning:` lines |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` (with L-APP, it builds core with `test-support` on and `cfg(test)` off: the PD-30 check, `plan.md:1079-1081`) | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (`.github/workflows/manager-ci.yml:97-115`), extracted into a file by a throwaway Node script (Rule 13) and executed with `"C:/Program Files/Git/bin/bash.exe"` from `apps/krate-manager/` with `RUNNER_TEMP=F:/tmp/gate/km9-<task>` and `RUNNER_OS=Windows` | a `::notice title=clippy-guard (Windows)::rc=<non-zero> disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=<≥1> other-errors=0` line (Phase 8's reading: `rc=101 … lint-named=5`, `specs/krate-manager-phase8/evidence/timings.md` row A1d), no `::error`; a `--locked` refusal is red |
| **L-TS** | `npm run typecheck`; `npm run lint`; `npm test` | each exit 0; vitest 29 titles before T007, 46 after |
| **L-NODE** | `node --check <each new or changed .js>`; `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km9-<task>/all.tap` from the repo root (quoted glob); read with `tail -12` | TAP `# fail 0`; no `Warning:` / `DeprecationWarning` / `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km9-<task>/all.tap` | exit 0 and `TAP plan=1..59 top-level=59` (only `EXPECTED_REQUIRED`'s data changes, no new `node:test` case; `.github/workflows/feed-tools.yml:86` `PLAN = "1..59"`) |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root) | exit 0, no stderr |
| **L-INDEX** | `node tools/gen-specs-index.js --check` (repo root) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase9/evidence/scope-check.js` (T008) | exit 0 |
| **L-REQ** | the `run:` text of `manager-ci.yml` step `required tests ran` (`.github/workflows/manager-ci.yml:153-178`) extracted as L-GUARD does and run over L-RS's `cargo-test.txt` and a `npm test -- --reporter=default --reporter=json --outputFile.json=<dir>/vitest.json` report (the step `vitest`, `:148-151`), `RUNNER_OS=Windows` | `::notice title=required tests ran (Windows)::288 cargo + 46 vitest` (276 `all` + 12 `windows`; plan §4.9 `plan.md:873`), no `::error` |
| **L-WSL** (main loop) | `node specs/krate-manager-phase9/evidence/wsl-lane.js` (T008; TA-10): in WSL, `CARGO_TARGET_DIR=$HOME/km-target` (Phases 5-8's), `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings && cargo test -p krate-manager-core --locked` | last line `WSL LANE GREEN …` and the nine Linux-run names (eight `_unix` + `native_wiring_installs_linux`) seen `ok`; it never closes a PENDING-CI row (plan §7.4 `plan.md:1091-1093`) |
| **L-CLOSE** | `node specs/krate-manager-phase9/evidence/close-lane.js` (T008; **main loop only**) | last line `CLOSE LANE GREEN` |

**Group gates** (run by the workflow's gate reporter). After G1: L-RS (nothing else executing), L-APP, L-DEVFEED,
L-GUARD, L-TS, L-NODE + L-TAP, L-VLINT. After G2: L-RS, L-APP, L-DEVFEED, L-GUARD, L-TS, L-NODE + L-TAP, L-VLINT,
L-SCOPE, `node --check` and `--self-test` on T008's three scripts, and L-WSL beside it (main loop, WSL's own cores: the
first Linux compile of the module and of every `cfg(unix)` test). After G3: everything T009 lists, then L-CLOSE (main
loop).

---

## Preconditions — rulings before the build stage

Plan §10 (`plan.md:1129-1135`) leaves three decisions for the user, each with a recommendation; this file is written to
all three recommendations:

- **R-1 (PD-1 / SI-1): open, recommendation applied.** The command's download leg resolves the asset for `(env.os,
  env.arch)` through `download::request_for_platform` / `command_request_for`; `request_for` / `command_request`
  delegate with the native pair. T001 implements it (plan §4.2 `plan.md:701-716`).
- **R-2 (PD-3 / SI-3): open, recommendation applied.** `download::discard` runs after `obtain_and` returns; a discard
  failure is one `warn!` and the action stays `Ok`. T003 implements it; SC-021's discard-failure row proves it.
- **R-3 (directory `fsync` refused by the filesystem): open, recommendation applied (keep strict).** A `sync_dir` error
  of any kind maps to its FR-029 code and rolls back before the commit point; no kind is tolerated. T003 implements it.

Starting the build stage after the user's review of spec, plan and tasks is their acceptance of all three unless they
overrule one.

**Precondition check (T001 runs it first, T002 too; on failure the task changes nothing and returns `blocked`, quoting
the item):** in `spec.md` `## Clarifications` (`spec.md:1079-1094`), a bullet that rules **against** R-1's, R-2's or
R-3's recommendation, or that marks any plan PD / SI row or any TA row of this file rejected. (Absence of a bullet is
acceptance.) The main loop then re-plans the named sections and regenerates the affected tasks.

### Task-list amendments (TA)

Rows marked **spec** change spec text (T002 writes them, tagged `(tasks TA-n)`); the others explain why a task differs
from a plan block or fix what the plan left open.

| Id | Amendment | Why | Spec? |
|---|---|---|---|
| TA-1 | **`tests/required-tests.json` and `EXPECTED_REQUIRED` are written in T001** (G1), with exactly plan §4.9's names (`plan.md:858-875`), not after the tests exist; T009 checks them against the real run (L-REQ) and fixes a name only if a test name differs from the plan | both are shared files, so they belong to the one sequential task; written there, they cost no extra group (Phase 8 needed a G3 task, T007, for them). No gate lane before L-CLOSE / T009 reads the test log against the list: the shape test only deep-compares the two lists (`manager-ci-shape.test.js:337`, `:846-854`), and `injection.rs:4779` only requires that the file is walked | no |
| TA-2 | **What T001 makes real and what it leaves as a placeholder.** Real in T001: `lib.rs`; the FR-114 move; `download.rs` PD-1; `detect.rs` PD-14; all of `status.rs`'s FR-100 / PD-13 / PD-15 changes; the goldens; `view.ts`'s `actions` field; in `install.rs` every type, constant and trait of plan §3 and §4.1.1-§4.1.2 / §4.1.4 / §4.1.9 / §4.1.11 (verbatim), `InstallError::code()`, `Step::as_str()`, `driver_for`, `NativeDrivers`, `allowed_actions` (TA-15), `native_env`, `channel_progress`; the two `src-tauri` commands. Placeholders (`todo!("T003")`): the 19 `impl InstallIo for FsInstallIo` methods, `LinuxDriver::run`, `read_manifest_full`, `action_gate`, `recover`, `for_command`, `remove_for_command`. Every private item of plan §4.1.3, §4.1.5-§4.1.8, §4.1.10, §4.1.12-§4.1.14 (`Destinations`, `dest_path`, `Line`, `parse_line`, `write_atomic`, `Journal`, `InstallPlan`, `plan`, `run_install`, `roll_back`, `roll_forward`, `run_uninstall`, `refusal`, `Busy`, `io_err`, `log_recovery`) is written by T003 | a private or unused `pub(crate)` placeholder warns `dead_code` at the zero-warning G1 gate (Phase 8 TA-2, same cause); `status.rs` (T001) calls `action_gate`, `allowed_actions`, `NativeDrivers` and `FsInstallIo`, `src-tauri` calls `for_command`, `remove_for_command`, `native_env`, `channel_progress`; the doubles (T004) implement `InstallIo`, `InstallDriver`, `DriverResolver` | no |
| TA-3 | **The test-double API is fixed by T001** (plan §4.5 `plan.md:749-784` names the types and their powers, not every constructor and accessor). Exact signatures: T001 step 9. Builders take `self` and are named `fail`, `fail_from`, `crash_after`, `on_call`, `without_symlinks` (none collides with an `InstallIo` method name, so a value never picks the wrong one); call kinds are an `IKind` enum beside plan §4.5's `IOp` | T003 and T006 write their tests against these names while T004 writes their bodies; an unnamed API would be a compile error between parallel writers | no |
| TA-4 | **`CommandEnv` derives `Clone, Copy`** (every field is a shared borrow, `Os`, `Arch`, `ExtractEnv` (Copy, `extract.rs:215`), `DownloadEnv` (Copy, `download.rs:208`) or an `Option` of a shared borrow) | `for_command` / `remove_for_command` take `CommandEnv` by value (plan §4.1.13 `plan.md:668-672`) and SC-001, SC-006, SC-009, SC-014, SC-016, SC-028 run several commands per rig | spec: FR-004 gains "`CommandEnv` is `Copy`" |
| TA-5 | **`MemInstallIo::new(base, roots, staged)`**, not plan §4.5's `new(base, staged)` (`plan.md:757`): `roots` names the directories to plant (every directory `roots_at` creates) and the staging dir whose reads `staged` serves | `MemExtractIo`'s `base` field is private and has no accessor (`test_support.rs:1095-1101`), so the Mem double cannot otherwise know which paths are the staged tree's (D-3) | no |
| TA-6 | **Three tests outside `required-tests.json`:** `download::tests::request_for_platform_cases` and `detect::tests::parse_manifest_cases` (T001) and `test_support::tests::phase9_doubles_cases` (T004). Totals stay plan §4.9's: `all` 276, `windows` 12, `macos` 44, `linux` 39, `vitest` 46 | Phase 8 TA-6 precedent (`verify::tests::sha256_stream_cases`, `test_support::tests::phase8_doubles_cases` are not required); FR-116 lists SC test names only | no |
| TA-7 | **T003 owns `context.rs` and `context_tests.rs`** and adds the FR-080 `recover` call only after `recover`'s body is real | a `recover` placeholder called from `assemble_with_os` (`context.rs:84-118`) would panic every `context_tests.rs` case and every other `assemble_with_os` caller (`detect_tests.rs`) in every G2 task's loop; SC-013's `startup_recovery` in `install_tests.rs` drives `assemble_with_os` too | no |
| TA-8 | **Guard rows move with the code that needs them.** T001 (G1): `const INSTALL_TESTS_RS` and `TAR_BUILDER_TESTS_RS` beside `EXTRACT_TESTS_RS` (`injection.rs:152`); `FR088_FILES` 6 → 8 (`:248-255`) and `PHASE7_TEST_FILES` 4 → 6 (`:186-191`) with those two; `INSTALL_TESTS_RS` appended to the `verify_file` callers of `minting_route_findings` (FR-113); `with_fixture_url` allowed in `INSTALL_TESTS_RS` in `fixture_seam_findings` (plan SI-2). T005 (G2): `const INSTALL_RS`, `install_findings`, the `request_seam` name rule (PD-30), every plant of plan §4.8 rows 1-3, and `roster.rs` | T003's `install_tests.rs` calls `verify_file` and `with_fixture_url` from its first save; without the G1 rows the existing real-tree checks of `verified_artifact_scanner_reports_plants` and `fixture_url_seam_scanner_reports_plants` go red in every G2 loop. An unused `const INSTALL_RS` in the `injection` test crate would warn `dead_code` until T005's rule reads it (Phase 8 TA-8) | no |
| TA-9 | Rule 10's target slots: shared target for T001 and T003; one slot `F:/tmp/km9-target/side` for T004, T005, T006 | four G2 tasks build core; `df -h /f` shows 5.0 GB free and `apps/krate-manager/target` holds 14 GB this session; one slot is one cold build (~1.5 GB, Phase 7 TA-9's measure) and keeps T003, the long pole, on the warm shared target | no |
| TA-10 | L-WSL runs the full `cargo test -p krate-manager-core --locked` (Phase 8's form) instead of plan §7.4's `install::` filter (`plan.md:1091-1093`), and its information line counts the nine Linux-run names | the guard binaries lex sources and walk the locks, which can differ on Linux; the full run is a superset at about 70 s warm (Phase 8 row A10) | no |
| TA-11 | **L-CLOSE runs the `tauri build` and `build warnings` steps** (Phase 7's rows B2-B7 form), which plan §7.4 omits | this phase changes `src-tauri` (two commands, the capability, two permission files, FR-096) and the webview (FR-097 to FR-099), so the release build sees this phase's code; Phase 7 measured 102.5 s for it; it is the local copy of the CI legs' `tauri build` (`manager-ci.yml:180-198`) (D-6) | no |
| TA-12 | **`INSTALL_IO_METHODS` holds exactly the nine `InstallIo` method names that are also tokens of the confined lists**: `create_dir`, `create_dir_all`, `symlink`, `rename`, `remove_file`, `remove_dir`, `read_dir`, `read_link`, `canonicalize` | plan §4.8 row 1 says "the 19 FR-028 method names that are also tokens" and then lists nine (`plan.md:849`); 19 is the size of the whole FR-028 surface (D-4) | no |
| TA-13 | **`snapshot` is not changed.** SC-001 / SC-028's "mode under `cfg(unix)`" (plan §7.1 `plan.md:998`) is read by a `#[cfg(unix)] fn modes(root: &Path) -> Vec<(String, u32)>` local to `install_tests.rs` (`symlink_metadata` + `PermissionsExt::mode() & 0o777`, never following a link, sorted) | `test_support::snapshot` returns `(path, kind, len)` only (`test_support.rs:1747-1780`) and every phase's tests use it; changing its tuple would edit dozens of call sites in other tasks' files (D-2) | no |
| TA-14 | **`src-tauri` and its guard are T001's** (plan §2 lists the files, `plan.md:205-206`, but no group) | every `src-tauri` file is a shared file (this file's definition), so it goes to the one sequential task; the commands compile against T001's `install.rs` surface | no |
| TA-15 | **`install::allowed_actions` is real in T001** (plan §4.1.11 `plan.md:653-655`, verbatim: `[]` when `!driver_present` or `Blocked`; else `Install` iff `Available` and `NotInstalled`, `Update` iff `Available` and `UpdateAvailable`, `Remove` iff `manifest.is_some()`, in that order) | `status::assemble_with_gates` (T001) calls it for every entry with a gate; it is D13's one function, pure and small; T003 and T006 test it through SC-008 / SC-016 | no |
| TA-16 | **The two generated permission files must be committed with the change.** `src-tauri/permissions/autogenerated/install_plugin.toml` and `remove_plugin.toml` are written by `tauri-build` during T001's L-APP run (the six existing files are its output, e.g. `download_plugin.toml`) and are untracked until the user commits them | every CI leg ends with step `Checkout unchanged` (`manager-ci.yml:299-307`: `git status --porcelain` must be empty), and the first CI `cargo clippy` would generate them; T009's hand-over and the PENDING-CI list name them | no |

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | **sequential** (1) | T001 precondition; `lib.rs`; the FR-114 move; the `install.rs` surface; PD-1 in `download.rs`; PD-14 in `detect.rs`; FR-100 / PD-13 / PD-15 in `status.rs` + the 13 goldens + `view.ts`'s field; the test-double surface; `src-tauri` + `config_guard.rs`; the G1 guard rows; `required-tests.json` + `EXPECTED_REQUIRED` | Preconditions |
| G2 Spec, module, doubles, guards, status, UI, scripts | **[P]** (7) | T002 spec + roadmap amendments · T003 `core::install` + `context` + their 38 tests and the doctest (the long pole) · T004 the Phase 9 test doubles · T005 `injection.rs` + `roster.rs` guards · T006 `status::tests::actions_per_state` · T007 the UI (17 vitest titles) · T008 evidence scripts | G1 |
| G3 Integration | sequential (1) | T009 L-REQ (the per-OS CI lists against the real run), INDEX, lints, scope, greps, the PENDING-CI list, hand-over of L-CLOSE | G2 |

**Why this shape.** T001 is the only task that edits shared files, so it is a group of its own; it also fixes every
name and signature a second G2 task compiles against (the `install.rs` surface, the test doubles, the three small
`download` / `detect` / `status` functions), so the seven G2 tasks run side by side and none adds a line to a shared file.
The long pole is `install` (T003), which cannot be split across writers: every SC test is named `install::tests::*` in
one module over one file (FR-116, plan §4.9), so there is one `install_tests.rs`; and the guards pin single-file facts of
`install.rs` (FR-112: the mutating and reading filesystem tokens only inside `impl InstallIo for FsInstallIo`,
`request_seam` only after `#[cfg(test)]` there; plan §4.8), so a second production file would change the spec's guard
text, not add parallelism. Everything that can leave T003 has left it: the doubles (T004), the guards (T005), the status
table test (T006), the UI (T007), the spec and roadmap text (T002) and the scripts (T008) run beside it, and T001's API
freeze means none of them waits for T003 to compile. The in-group dependencies run one way per pair and settle before
the gate: T003's double-backed rows run T004's bodies, T004's forwarding rows and T006 run T003's bodies, T005's
real-tree rules scan T003's file. G3 is one task because each of its checks reads the whole tree after G2. No group is a
chain of independent tasks.

**SC owners:** SC-001 to SC-017, SC-020 to SC-023, SC-025 `install` names, SC-027 to SC-030 T003 (SC-013's context row
and SC-016's `no_driver_off_linux` included) · SC-016 `status::tests::actions_per_state` T006 · SC-018 T001 (+ L-APP) ·
SC-019 T005 · SC-024 T007 · SC-026 every task + T009 + L-CLOSE · SC-030 roadmap sentences T002 (rg check in T009) ·
FR-111 T001 (unchanged set) · FR-112, FR-113 T001 rows + T005 · FR-114 T001 · FR-116 T001 + T009 (L-REQ) · FR-120,
FR-121 T008 (scope check) + T009 · spec text of SI-1 to SI-27 and TA-4 T002.

---

## G1 — Foundation (sequential: one task, every shared file)

### T001 — Precondition, `lib.rs`, the archive-helper move, the `install.rs` surface, PD-1, PD-14, `status` FR-100, the goldens and `view.ts` field, the test-double surface, `src-tauri` + its guard, the G1 guard rows, the required-test lists (spec FR-096, FR-100, FR-111, FR-114, FR-116; SC-018; plan §2, §3, §4.1.1, §4.1.2, §4.1.4, §4.1.9, §4.1.11, §4.2, §4.3, §4.4, §4.5 surface, §4.6, §4.8 rows 4-5, §4.9; PD-1, PD-5, PD-6, PD-11-PD-15, PD-30; SI-1, SI-4, SI-10; TA-1 to TA-4, TA-8, TA-14 to TA-16)

This task makes every edit to a shared file the phase needs and freezes every cross-task API, so G2 touches only its own
files. Target: the shared `apps/krate-manager/target`, `CARGO_BUILD_JOBS=16`. Logs under `F:/tmp/gate/km9-T001/`.

**Files (edit):** `crates/core/src/lib.rs` (CRLF), `crates/core/src/extract_tests.rs` (LF),
`crates/core/src/download.rs` (LF), `crates/core/src/download_tests.rs` (LF), `crates/core/src/detect.rs` (LF),
`crates/core/src/detect_tests.rs` (LF), `crates/core/src/status.rs` (LF), `crates/core/src/test_support.rs` (CRLF),
`crates/core/tests/injection.rs` (CRLF), `crates/core/tests/config_guard.rs` (LF), `tests/fixtures/status/*.json` (13,
LF), `src/view.ts` (LF), `src-tauri/src/commands.rs` (LF), `src-tauri/src/lib.rs` (CRLF), `src-tauri/build.rs` (LF),
`src-tauri/capabilities/main.json` (LF), `tests/required-tests.json` (LF),
`tools/feed-contract/tests/manager-ci-shape.test.js` (LF). **Create (LF):** `crates/core/src/install.rs`,
`crates/core/src/install_tests.rs`, `crates/core/src/tar_builder_tests.rs`. **Generated (by L-APP):**
`src-tauri/permissions/autogenerated/install_plugin.toml`, `remove_plugin.toml`.

**Steps, each with its failing test first:**

1. **Precondition check** (above). Then record `git rev-parse --short HEAD` (expected `abdd2c9e`) and `git status
   --porcelain`, and run L-RS once as the baseline (`cargo-test-before.txt`; quote every `test result:` line).
2. **`lib.rs`** (`crates/core/src/lib.rs`, 29 lines, read this session): add `pub mod install;` after `pub mod http;`
   (line 11) and `#[cfg(test)]` + `mod tar_builder_tests;` after `mod test_signer;` (lines 23-24), CRLF.
3. **FR-114 move** (spec `spec.md:695-704`; plan §4.4 `plan.md:739-747`). Test first: the baseline's `extract::tests`
   pass count (from step 1). Create `crates/core/src/tar_builder_tests.rs` with exactly the moved set, each item and each
   `Tar` method `pub(crate)`, bodies byte-unchanged: `sha` (`extract_tests.rs:106-110`), `unhex` (`:112-118`),
   `literal_payload` (`:131-137`), `gz` (`:154-158`), `gunzip` (`:160-166`), `moduleinfo` (`:168-175`), `octal_field`
   (`:177-183`), `reseal` (`:185-193`), `struct Tar` + `impl Tar` (`:195-271`), plus only the `use` lines those bodies
   need (`Payload` and any `flate2` / `std::io` items they name; read the bodies). Delete them from `extract_tests.rs` and
   add one `use crate::tar_builder_tests::{gunzip, gz, literal_payload, moduleinfo, octal_field, reseal, sha, unhex,
   Tar};` (only the names `extract_tests.rs` still uses, or `unused_imports` warns). `read_fixture`, `good`, `bad`,
   `good_listing`, `good_payload` (`:87-128`) stay. Pass: `cargo test -p krate-manager-core --locked --lib extract::`
   reports the same pass count as the baseline, 0 failed; clippy 0 warnings.
4. **`install.rs` surface** (TA-2). Write plan §3 (`plan.md:218-266`, the whole error model, real: `RootTag`,
   `KeptPath`, `Step` + `as_str` (the fifteen snake_case names), `InstallError` with every variant and `#[error]` text
   verbatim, `code()` returning each variant's code and the inner code for `Record` / `Download` / `Extract`), plan
   §4.1.1 (`plan.md:283-340`, every item verbatim, with TA-4's `#[derive(Clone, Copy)]` on `CommandEnv` and the
   `#[cfg(test)] pub(crate) request_seam` field; `Action::kind` and `Action::entry` real; `driver_for`, `NativeDrivers`'
   `DriverResolver` impl (delegates to `driver_for`) and `static LINUX` real; `impl InstallDriver for LinuxDriver` with
   `os() == Os::Linux` real and `run` `todo!("T003")`; `native_env` real (PD-6: `os: NativePlatform.os()`, `arch:
   Arch::native()`, `drivers: &NativeDrivers`, `#[cfg(test)] request_seam: None`); `channel_progress` real (the body of
   `download::channel_progress`, `download.rs:1582-1590`, retyped to `InstallProgress`)), plan §4.1.2 (`plan.md:348-373`:
   the trait verbatim, `pub struct FsInstallIo;`, `impl InstallIo for FsInstallIo` with 19 `todo!("T003")` bodies), plan
   §4.1.4's three types (`Manifest`, `ManifestEntry`, `EntryTag`, verbatim) and `read_manifest_full` (`todo!("T003")`),
   plan §4.1.9's `Decision`, `Recovered`, `RecoveryReport` and `recover` (`todo!("T003")`), plan §4.1.11's `ActionGate`,
   `action_gate` (`todo!("T003")`) and `allowed_actions` (real, TA-15), plan §4.1.13's `for_command` /
   `remove_for_command` (`todo!("T003")`), the constants of §4.1.1. Doc comments on every pub item (one line each, citing
   its FR). End the file with `#[cfg(test)]` / `#[path = "install_tests.rs"]` / `mod tests;`, and create
   `install_tests.rs` holding only `//! install::tests (T003): every Phase 9 SC row (plan §7.1).` (no `use`, so no
   `unused_imports`). If the lib build warns `field request_seam is never read`, the `for_command` placeholder reads it
   once under `#[cfg(test)]` (`let _ = _env.request_seam;`) before its `todo!`. Pass: `cargo clippy -p krate-manager-core
   --all-targets --locked -- -D warnings` 0 warnings.
5. **PD-1 in `download.rs`** (plan §4.2 `plan.md:701-716`; R-1). Test first in `download_tests.rs`:
   `download::tests::request_for_platform_cases` (TA-6, not required): over `test_feed(&[TestPlugin { name: "Alpha",
   key: PlatformKey::LinuxX86_64, … }])` (`test_support.rs:335-409`) assert `request_for_platform(&feed, "Alpha",
   Os::Linux, Arch::X86_64, Some(&"2.99".parse()?))` is `Ok` with `key() == LinuxX86_64`; with `Os::Windows,
   Arch::X86_64` it is `Err` with `code() == "E_DOWNLOAD_UNAVAILABLE"`; an unknown name is `E_DOWNLOAD_UNKNOWN_PLUGIN`;
   and over a `native_key()` feed (`test_support.rs:327-332`) `request_for(&feed, "Alpha", running)` and
   `request_for_platform(&feed, "Alpha", NativePlatform.os(), Arch::native(), running)` return equal `key()` and
   `asset()`. Add both new functions as `todo!` first; red on the panic. Implement: `request_for_platform` is today's
   `request_for` body (`download.rs:742-784`) with `version::resolve(entry, os, arch, running)` in place of
   `resolve_native`; `request_for` and `command_request` become one-line delegations; `command_request_for` is today's
   `command_request` body over `request_for_platform`. No other line of `download.rs` changes (FR-120 as amended by
   SI-1). Pass: the new test and every existing `download::tests::*` green (`cargo test -p krate-manager-core --locked
   --lib download::`), 0 warnings.
6. **PD-14 in `detect.rs`** (plan §4.3 `plan.md:732-734`). Test first in `detect_tests.rs`:
   `detect::tests::parse_manifest_cases` (TA-6, not required): for five byte strings (a valid Phase 9-shaped manifest with
   extra keys; `manifest_version` 2; `plugin` "Beta" read for "Alpha"; not JSON; `version` missing) written to
   `<state>/manifests/Alpha.json` in a `fresh_temp_roots()` state dir, `parse_manifest(bytes, "Alpha")` equals
   `read_manifest(state_dir, "Alpha", "Alpha")` (`Found(Manifest { version_text })`, `Failed("E_RECORD_FUTURE")`,
   `Failed("E_RECORD_MISMATCH")`, `Failed("E_RECORD_PARSE")`, `Failed("E_RECORD_PARSE")` or whatever code the existing
   reader gives for the last; assert equality, then quote the codes). Placeholder first, red; then extract
   `pub(crate) fn parse_manifest(bytes: &[u8], plugin: &str) -> RecordRead<InstallerRecord>` from `read_manifest`
   (`detect.rs:891-913`, everything from `serde_json::from_slice` on) and make `read_manifest` the bounded read +
   `parse_manifest`. Pass: the new test and every existing `detect::tests::*` green; Rule 4e.
7. **FR-100 / PD-13 / PD-15 in `status.rs`** (plan §4.3 `plan.md:720-731`; SI-4, SI-6). Test first: add
   `pub actions: Vec<ActionView>` as the **last** field of `PluginView` (`status.rs:56-72`, after `notes`) with
   `ActionView` (`#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)] #[serde(rename_all = "snake_case")] pub enum
   ActionView { Install, Update, Remove }`) and an exhaustive `fn action_view(ActionKind) -> ActionView`; set `actions:
   Vec::new()` where `plugin_view` builds the view; run `cargo test -p krate-manager-core --locked --lib
   status::tests::golden_views` → red, `differs` for all 13 files (quote the line). Then add `"actions": []` as each plugin
   object's last key in the 13 files of `tests/fixtures/status/` (the exact `to_string_pretty` + LF form the test compares,
   `status_tests.rs:977-989`: write a throwaway Node script that inserts `,\n      "actions": []` after each plugin
   object's `"notes"` value at the right indentation, run it, delete it; re-run the test until byte-equal). Then
   implement the rest, verbatim from plan §4.3: `pub(crate) fn update_state(entry, detection) -> Option<UpdateState>`
   extracted from `plugin_view`'s managed-copy rule (`status.rs:330-370`), which `plugin_view` now calls;
   `pub fn build_with_os(os, arch, ctx, mode, now_unix, host, io: &dyn InstallIo) -> StatusView` (today's `build` body,
   `status.rs:176-210`, with `os` / `arch` / `io` as inputs and, only when `install::NativeDrivers.driver_for(os)` is
   `Some`, one `install::action_gate(os, ctx, io, entry)` per entry with an `install` block into a `BTreeMap<String,
   ActionGate>`, PD-15); `build` delegates with `NativePlatform.os()`, `Arch::native()`, `&install::FsInstallIo`;
   `pub fn assemble_with_gates(os, arch, check, running, detections, gates, driver_present)` (`assemble`'s body,
   `status.rs:213-258`; each card's `actions` from `allowed_actions(driver_present, gate, &availability,
   update_state(entry, detection))` mapped through `action_view`, `[]` for an entry without a gate, so `allowed_actions`
   is never called without one); `assemble` delegates with an empty map and `false`. Pass: `status::tests::*` and
   `detect::tests::*` green (the snapshot test `detect_tests.rs:1980-2005` proves `build` still writes only the feed
   cache; on Windows `driver_for(Os::Windows)` is `None`, so no gate runs), 0 warnings, Rule 4e.
8. **`view.ts` field** (plan §4.7 `plan.md:815-817`, the `actions` part only). Test first: `npm test` → the vitest
   golden suite over the 13 files is red (`object()` refuses the extra key, `view.ts:122-128`; quote it). Implement:
   `export type ActionName = "install" | "update" | "remove"`, `PluginView.actions: readonly ActionName[]`, the key added
   to the plugin key list, parsed with the file's existing `array` / one-of helpers. Nothing else in `view.ts` (the rest
   is T007's). Pass: L-TS (29 titles), Rule 4f.
9. **The Phase 9 test-double surface in `test_support.rs`** (TA-3, TA-5; plan §4.5 `plan.md:749-784`). Add, after the
   Phase 8 doubles and before `snapshot` (CRLF; Rule 4b; no `static`), with every body `todo!("T004")` except where
   marked real:
   ```rust
   #[derive(Clone, Copy, Debug, PartialEq, Eq)]
   pub enum IKind { KindOf, ReadLink, HashFile, ReadDir, ReadFile, Canonicalize, CreateDirAll, CreateDir, CopyNew,
                    WriteNew, Symlink, SetMode, SyncFile, SyncDir, Rename, RemoveFile, RemoveDir, RemoveTree }
   impl IKind { pub fn is_mutating(self) -> bool }                     // real: the twelve from CreateDirAll on
   #[derive(Clone, Debug, PartialEq, Eq)]
   pub enum IOp { /* plan §4.5 `plan.md:751-755`, verbatim */ }
   impl IOp { pub fn kind(&self) -> IKind; pub fn is_mutating(&self) -> bool }   // real
   pub struct MemInstallIo<'a> { _private: PhantomData<&'a ()> }
   impl<'a> MemInstallIo<'a> {
       /// Plants `base` and every directory `roots_at` creates under it; reads under `roots.staging_dir()` are
       /// served by `staged` (read-only); proper ancestors of `base` exist and canonicalise to themselves (TA-5).
       pub fn new(base: &Path, roots: &Roots, staged: &'a MemExtractIo) -> MemInstallIo<'a>;
       pub fn plant_dir(&self, path: &Path, mode: u32);
       pub fn plant_file(&self, path: &Path, bytes: &[u8], mode: u32);
       pub fn plant_link(&self, path: &Path, target: &str);
       pub fn node(&self, path: &Path) -> Option<MemNode>;          // as spelled, no link resolution
       pub fn paths_under(&self, root: &Path) -> Vec<PathBuf>;     // strictly under, sorted
       pub fn calls(&self) -> Vec<IOp>;
       pub fn mutating_calls(&self) -> usize;
   }
   impl InstallIo for MemInstallIo<'_> { /* 19 methods */ }
   pub struct FaultyInstallIo<'a> { _private: PhantomData<&'a ()> }
   impl<'a> FaultyInstallIo<'a> {
       pub fn new(inner: &'a dyn InstallIo) -> FaultyInstallIo<'a>;
       pub fn fail(self, kind: IKind, nth: usize, error: io::ErrorKind) -> Self;       // one-shot, nth 1-based
       pub fn fail_from(self, kind: IKind, nth: usize, error: io::ErrorKind) -> Self;  // persistent from the nth
       pub fn crash_after(self, k: usize) -> Self;  // every mutating call after the k-th: io::Error::other("crash"), not forwarded
       pub fn on_call(self, kind: IKind, nth: usize, hook: impl Fn(&dyn InstallIo) + Send + Sync + 'a) -> Self;
       pub fn without_symlinks(self) -> Self;
       pub fn calls(&self) -> Vec<IOp>;
       pub fn mutating_calls(&self) -> usize;
   }
   impl InstallIo for FaultyInstallIo<'_> { /* 19 methods */ }
   pub struct RecordingInstallIo<'a> { _private: PhantomData<&'a ()> }
   impl<'a> RecordingInstallIo<'a> {
       pub fn forwarding(inner: &'a dyn InstallIo) -> RecordingInstallIo<'a>;
       pub fn calls(&self) -> Vec<IOp>;
       pub fn mutating_calls(&self) -> usize;
   }
   impl RecordingInstallIo<'static> { pub fn fail_closed() -> RecordingInstallIo<'static> }  // reads forward to FsInstallIo
   impl InstallIo for RecordingInstallIo<'_> { /* 19 methods */ }
   pub struct CountingDriver<'a> { _private: PhantomData<&'a ()> }
   impl<'a> CountingDriver<'a> { pub fn new(inner: &'a dyn InstallDriver) -> CountingDriver<'a>; pub fn runs(&self) -> usize }
   impl InstallDriver for CountingDriver<'_> { /* os, run */ }
   pub struct RecordingDriver { _private: () }
   impl RecordingDriver { pub fn new(os: Os, kept: Vec<KeptPath>) -> RecordingDriver;
                          pub fn batches(&self) -> Vec<Vec<(ActionKind, String)>> }
   impl InstallDriver for RecordingDriver { /* os, run */ }
   pub struct OneDriver<'a>(pub Option<&'a dyn InstallDriver>);
   impl DriverResolver for OneDriver<'_> { /* Some(d) iff d.os() == os */ }        // real
   pub struct InstallRig { pub ctx: AppContext, pub staging: FaultyStagingIo, pub policy: DownloadPolicy,
                           pub cancel: CancelToken, pub extract_limits: ExtractLimits }
   impl InstallRig {
       pub fn new(ctx: AppContext) -> InstallRig;   // real: healthy staging, test_policy(), CancelToken::new(), PRODUCTION limits
       pub fn download_env(&self) -> DownloadEnv<'_>;                                         // real
       pub fn extract_env<'a>(&'a self, io: &'a dyn ExtractIo) -> ExtractEnv<'a>;            // real
       pub fn command_env<'a>(&'a self, os: Os, arch: Arch, io: &'a dyn InstallIo, extract_io: &'a dyn ExtractIo,
                              drivers: &'a dyn DriverResolver) -> CommandEnv<'a>;            // real, PD-30
   }
   ```
   `command_env`'s literal holds `#[cfg(test)]` / `request_seam: None,` (the `download.rs:775-776` pattern; never
   `Some`, PD-30). Use `ExtractEnv`'s real field types (read `extract.rs:215-220`; if `io` is `&'a (dyn ExtractIo +
   Sync)`, use that type in both signatures). `InstallIo: Sync` (plan §4.1.2), so the doubles keep their state in
   `Mutex` / atomics. `temp_roots_with_user_presets` is **not** declared here (T004, `cfg(unix)`, Phase 8 TA-5
   precedent). Pass: L-OWN clippy 0 warnings; L-DEVFEED (`test-support` on, `cfg(test)` off) 0 warnings.
10. **The G1 guard rows in `injection.rs`** (TA-8; CRLF). Test first: none new; the existing `injection` tests are the
    check. Add `const INSTALL_TESTS_RS: &str = "crates/core/src/install_tests.rs";` and `const TAR_BUILDER_TESTS_RS: &str
    = "crates/core/src/tar_builder_tests.rs";` beside `EXTRACT_TESTS_RS` (`injection.rs:152`); `FR088_FILES: [&str; 8]`
    (`:248-255`) and `PHASE7_TEST_FILES: [&str; 6]` (`:186-191`) each + those two; `INSTALL_TESTS_RS` appended to the
    `verify_file` callers of `minting_route_findings` (FR-113; read the function, Phase 8 put `EXTRACT_TESTS_RS` there);
    `INSTALL_TESTS_RS` added to the files where `fixture_seam_findings` allows `with_fixture_url` (read the function; it
    allows `download.rs` / `download_tests.rs` today). Nothing else. Pass: `cargo test -p krate-manager-core --locked
    --test injection` green (the two new files exist and are clean), 0 warnings.
11. **`src-tauri` and its guard** (FR-096; plan §4.6 `plan.md:798-811`, §4.8 row 5 `plan.md:853`; TA-14, TA-16). Test
    first in `config_guard.rs` (LF): `HANDLER_LIST` (`:152`) → the eight commands in order (`…,commands::download_plugin,
    commands::install_plugin,commands::remove_plugin]`); the capability list (`:640-660`, `allow-install-plugin`,
    `allow-remove-plugin` appended); the `build.rs` list and both messages (`:679-691`) → eight names; a new check in
    `check_config_against`: the `install_plugin` and `remove_plugin` signatures in `commands.rs` hold only `State`,
    `String` and `Channel<InstallProgress>` parameters (no `Path`, `PathBuf`, `OsString`); two plants in
    `planted_config_changes_fail_the_guard` beside the `download_plugin` one (`:1994-2003`): "`commands::install_plugin`
    removed from the handler list" and "`path: PathBuf` added to `remove_plugin`". Run `cargo test -p krate-manager-core
    --locked --test config_guard` → red ("does not register exactly …"; quote). Implement: `commands.rs` gains
    `install_plugin` and `remove_plugin` exactly in the `download_plugin` shape (`commands.rs:58-100`: clone the context
    under the read guard, `spawn_blocking`, `DownloadPolicy::production()`, `CancelToken::new()`, `DownloadEnv { …, io:
    &FsStagingIo }`, `ExtractEnv { ctx: &ctx, io: &FsExtractIo, limits: &ExtractLimits::PRODUCTION }`,
    `install::native_env(&ctx, &FsInstallIo, extract, download)`, the core call with `&PlatformFacts { io: &OsHostIo }`
    (install only) and `&mut install::channel_progress(|e| on_progress.send(e))`, errors `to_string()`, a join error as
    `DownloadError::Task { detail }.to_string()`); `src-tauri/src/lib.rs` `generate_handler!` (`:87-94`, CRLF) + the two;
    `build.rs` (`:3-10`) + `"install_plugin"`, `"remove_plugin"`; `capabilities/main.json` + `"allow-install-plugin"`,
    `"allow-remove-plugin"`; no other capability, permission, plugin or CSP change. Then L-APP: its `build.rs` run writes
    `permissions/autogenerated/install_plugin.toml` and `remove_plugin.toml` (check both exist and match the
    `download_plugin.toml` form; never hand-write them). Pass: `config_guard` green, L-APP and L-DEVFEED 0 warnings.
12. **`required-tests.json` + `EXPECTED_REQUIRED`** (FR-116; plan §4.9 `plan.md:858-875`; TA-1). Test first: append the
    names to `EXPECTED_REQUIRED` in `manager-ci-shape.test.js` (`:337` onward, each lane's list in the plan's order):
    `cargo.all` + the 28 names (26 `install::tests::…`, `context::tests::assemble_runs_install_recovery`,
    `status::tests::actions_per_state`); `cargo.macos` and `cargo.linux` + the 8 `install::tests::…_unix`; `cargo.macos` +
    `install::tests::native_wiring_offers_nothing_macos`; `cargo.linux` + `install::tests::native_wiring_installs_linux`;
    `cargo.windows` + `install::tests::native_wiring_offers_nothing_windows`; `vitest` + the 17 SC-024 titles exactly as
    `spec.md:938-945` spells them. Run `node --test tools/feed-contract/tests/manager-ci-shape.test.js` → red
    (`required-tests.json differs …`, `:854`). Then append the same names to `tests/required-tests.json`. Pass: L-NODE +
    L-TAP (`1..59`), and a `node -e` count printing `all:276 windows:12 macos:44 linux:39 vitest:46`.
13. **Close the task:** L-RS, L-APP, L-DEVFEED, L-GUARD, L-TS, L-NODE + L-TAP, L-VLINT, each green; `git status
    --porcelain` lists only Rule 11's paths. Notes: every `test result:` line, the counts of step 12, the two generated
    files' names (TA-16), each red-first line.

**Lane:** app lane (L-RS, L-APP, L-DEVFEED, L-GUARD, L-TS, L-VLINT) and pipeline lane (L-NODE, L-TAP).

---

## G2 — Spec, module, doubles, guards, status, UI and scripts [P] (seven tasks, disjoint files, Rule 10 slots)

### T002 [P] — Precondition check, the spec amendments and the roadmap amendments (plan §0.2 SI-1 to SI-27, §4.10; spec FR-123; TA-4)

**Files:** `specs/krate-manager-phase9/spec.md` (LF), `specs/Krate-manager-roadmap.md` (CRLF). No code, no build.

**Test first:** record the needle counts of "Spec state" above (`grep -c` per needle over `spec.md`, expected 0 each) and,
over the roadmap, `grep -c` of `commit point is the rename of new into`, `refuses to change the Linux plug-in root while
any install manifest`, `orphan manifest` (expected 0 each, read this session). A count already above 0 means the main
loop applied that edit: skip that row and say so.

**Implement:**
1. Precondition check (above).
2. For each of SI-1 to SI-27 (`plan.md:72-100`), apply the "Proposed spec edit" column to the FRs / SCs / sections it
   names, verbatim where it quotes text, each edit tagged `(plan SI-n)`; plus TA-4's FR-004 sentence tagged `(tasks
   TA-4)`. SI-26 includes the edge-case correction of `spec.md:1020`, the three new test names and `cargo.all` 276.
   Update the header status to "DRAFT, revision 3 (plan SI-1 to SI-27 encoded)". Do not change any other sentence.
3. Roadmap (CRLF): the three FR-123 sentences of plan §4.10 (`plan.md:877-886`) verbatim: (a) in Phase 9's "Journaled
   bundle swap" bullet (`Krate-manager-roadmap.md:677-684`), after "…then cleans up." the sentence of (a), and the
   parenthesis "(new present, target missing → rename new in)" replaced by "(new absent, target present → finish the
   presets, manifest and cleanup)"; (b) appended to Phase 12's settings-screen bullet (`:887-889`); (c) a new 🆕 bullet
   after it in Phase 12's components. No other roadmap line changes (FR-123).

**Pass:** each needle count now ≥ 1 (quote them); `git diff --stat` touches only the two files; L-SCOPE once T008's script
exists (else at the G2 gate).
**Lane:** pipeline lane (L-SCOPE, L-INDEX at G3).

### T003 [P] — `core::install`: `FsInstallIo`, destinations, manifest, atomic write, journal, planner, swap, roll paths, recovery, uninstall, gate, registry, commands, logging; the FR-080 call in `context`; all 38 SC tests and the compile-fail doctest (spec FR-001 to FR-095, FR-110, FR-115, FR-117, FR-122, FR-124; SC-001 to SC-017, SC-020 to SC-023, SC-025, SC-027 to SC-030; plan §3, §4.1, §4.3 context bullet, §6, §7; PD-2 to PD-12, PD-16 to PD-29; R-2, R-3; TA-4, TA-7, TA-13)

**Files:** `crates/core/src/install.rs` (LF), `crates/core/src/install_tests.rs` (LF), `crates/core/src/context.rs`
(CRLF), `crates/core/src/context_tests.rs` (CRLF). **Target:** the shared `apps/krate-manager/target`,
`CARGO_BUILD_JOBS=16` (Rule 10). L-OWN: `cargo test -p krate-manager-core --locked --lib install::` (and
`context::`, `--doc install`). Logs under `F:/tmp/gate/km9-T003/`.

**Binding text:** plan §3 and §4.1.1-§4.1.14 (`plan.md:216-699`) for every signature and algorithm, as amended by PD-1 to
PD-30; plan §7 and §7.1 (`plan.md:969-1030`) for every test's inputs and assertions; spec SC-001 to SC-030
(`spec.md:749-978`) for the thresholds; §7.2 (`plan.md:1032-1056`) for the FRs each test also covers. In particular:
the FR-024 step order and the commit point at `new_in` (§4.1.8, D7); the recovery table of §4.1.9 (`plan.md:611-620`);
PD-24 (placement containment), PD-26 (sibling pre-state), PD-27 (manual-install downgrade re-check), PD-28 (state
subdirectories), PD-29 (link targets), PD-17 (missing paths), PD-19 (`remove_tree` by kind), PD-9 (`sync_file` opens
`write(true)`), PD-10 (error kinds), PD-3 (discard after `obtain_and`), PD-4 (the `RefCell` sink), PD-23
(`Destinations::of` before any `InstallIo` call). R-3: no `sync_dir` error kind is tolerated.

**Test first** (`install_tests.rs`, module `install::tests`; written before the bodies they test, in the order below;
each test red first on `not yet implemented: T003` or on its assertions; rows over T004's doubles may also be red on
`not yet implemented: T004` until T004 lands, the expected in-group red):
- **Rig** (no test name): `fresh_temp_roots()`; a `FixtureServer::start` (`fixture_server.rs:144`) serving
  `/versions.json` and each asset route; a `TestSigner`; feeds from `test_feed_document` (`test_support.rs:347-404`)
  patched with `serde_json` (`payload` from the archive's literal listing via `tar_builder_tests::literal_payload`,
  `preset_subdir` when the row sets it), one `linux-x86_64` `tar.gz` asset per version (SI-1); `request_seam` = a closure
  mapping each request to `server.url(<asset route>)` through `with_fixture_url(..).expect(..)`, set as `env.request_seam
  = Some(&seam)` on the rig's seam-free `CommandEnv` (PD-30; the only file that assigns `Some`); `FakeHostFacts {
  os_version: Some("2.99"), .. }` (Linux rows); `InstallRig::command_env(Os::Linux, Arch::X86_64, …)`; plugins `Alpha` /
  `Beta`; `fn moduleinfo_for(name, version)` and `fn bundle_archive(spec) -> (Vec<u8>, Vec<String>)` of plan §4.4
  (`plan.md:742-747`, building `<Name>.vst3/Contents/{Resources/moduleinfo.json, x86_64-linux/<Name>.so}`, `presets`,
  `README.txt`, `CHANGELOG.txt`, optionally an exec bit and `l <Name>.vst3/Contents/x86_64-linux/lib<name>.so ->
  <Name>.so`) with the moved `Tar` / `gz`; `fn boundaries(calls, ctx) -> BTreeMap<Cut, usize>` of plan §4.5
  (`plan.md:786-796`); a Mem listing digest built with `CanonicalListing::from_entries` over `MemInstallIo::paths_under`
  + `node`; TA-13's `#[cfg(unix)] fn modes`. Driver-level rows sign the archive in memory and obtain the
  `VerifiedArtifact` through `verify_file` (FR-113, allowed by T001's row); Mem rows plant the archive bytes into the
  `MemExtractIo` at the artifact's path, as `extract_tests.rs`'s Mem rows do.
- **The 26 `cargo.all` names**, each exactly as plan §7.1's row: `install_update_remove_cycle` (SC-001),
  `crash_after_each_step_recovers` (SC-002, the 28-row table `spec.md:767-785`), `manifest_outside_roots_refused`
  (SC-003, the table `spec.md:790-796`), `remove_keeps_user_files` (SC-004), `update_preset_rules` (SC-005, incl. the
  manual-install and the PD-26 plant-before-rename rows), `manifest_reads_back_through_detect` (SC-006),
  `unverified_never_installed` (SC-007: `E_VERIFY_SHA256`, `E_VERIFY_KEY_ID`, `E_VERIFY_TRUSTED_COMMENT`, `runs == 0`),
  `state_checks_before_download` (SC-008, the 12 spec rows `spec.md:828-841` + plan row 13), `driver_rechecks_state`
  (rows (a)-(j), `plan.md:1006`), `io_failures_roll_back` (SC-009, the table `spec.md:850-866`, every pre-commit row ×
  `StorageFull` / `PermissionDenied` / `Other`), `changed_staged_file_refused` (SC-010), `collisions_refused_before_write`
  (SC-011 incl. the stray `.krate-old`, the plugin root a file, the PD-24 / PD-29 Mem rows and the FR-012 `dest_path`
  table of §7.2), `remove_crash_resumes` (SC-012), `startup_recovery` (SC-013, four planted journals),
  `recovery_table_rows` (SC-013 / FR-071 rows (1)-(4)), `concurrent_actions` (SC-014, `std::thread::scope` + `mpsc`
  `ready` / `go`), `sync_before_commit` (SC-015), `no_driver_off_linux` (SC-016, §7's off-Linux feed: `windows-x86_64`
  `inno-exe` / `darwin-aarch64` `pkg` asset added, `os_version Some("15.0")` on the macOS row), `progress_events`
  (SC-017; Remove `total` = 1 + preset entries, SI-12), `sandbox_guard_panics` (SC-020, two `catch_unwind` blocks and the
  not-`E_INSTALL_BUSY` follow-up, SI-19), `artifact_released_after_success` (SC-021 incl. the PD-3 discard-failure row
  over `FaultyStagingIo::failing(Op::RemoveDir, n, PermissionDenied)`), `logs_one_line_per_action` (SC-022,
  `install_capture` / `take_mine`, `test_support.rs:221-240`), `commands_run_over_the_driver` (SC-027, `OneDriver(Some(&
  recording))`), `batch_runs_every_action` (SC-027 / FR-001 over `FaultyInstallIo::new(&FsInstallIo).fail(IKind::CopyNew,
  1, StorageFull)`), `remove_deletes_bundle_whole` and `cut_before_new_in_rolls_back_in_process` (SC-030).
- **The 8 `_unix` names** (`#[cfg(unix)]`): `install_update_remove_cycle_unix`, `crash_after_each_step_recovers_unix`,
  `manifest_outside_roots_refused_unix`, `io_failures_roll_back_unix` (+ `symlink`, `set_mode`, `sync_dir` rows),
  `collisions_refused_before_write_unix` (symlinked target, the PD-24 / PD-28 Fs rows), `non_utf8_root_refused_unix`
  (SC-023, `temp_roots_with_user_presets(td, OsStr::from_bytes(b"u\xff"))` from T004), `symlinked_plugin_root_unix`
  (SC-028), `remove_bundle_link_unix` (SC-030).
- **The 3 native-wiring names** (SC-029): `native_wiring_installs_linux` (`#[cfg(target_os = "linux")]`),
  `native_wiring_offers_nothing_windows` (`#[cfg(windows)]`), `native_wiring_offers_nothing_macos`
  (`#[cfg(target_os = "macos")]`), over `install::native_env` with `request_seam` set in this file, `status::build`, and
  §7's off-Linux feed with the `native_key()` asset (`test_support.rs:327-332`).
- **The compile-fail doctest** on `Action` (SC-007): a `compile_fail` block that forges `Action::Install { entry,
  artifact: VerifiedArtifact { .. } }`; it adds no required name (FR-116). Red first: write it as a plain (compiling) doc
  block that fails, then mark it `compile_fail` once the type exists (Phase 8 precedent).
- **`context::tests::assemble_runs_install_recovery`** in `context_tests.rs` (CRLF): `assemble_with_os(Os::Linux, …)`
  over temp roots holding an unfinished `staged` journal of an update cut after `old_aside` (planted from a recorded
  fault-free run), `manifests/.Alpha.json.tmp` and `staging/extract/<64 hex>.abcdef/` → `context` `Ok`, the old bundle
  back, the journal and both leftovers gone; and the existing `assemble_cases (b)` still sees exactly
  `state/manager.lock` added (`context_tests.rs:127-146`: `recover` creates nothing).

**Implement** (in this order, each slice after its tests): plan §3's private `io_err` (PD-10); `FsInstallIo`'s 19 bodies
(§4.1.2, PD-9, PD-19; `std::fs` only there, PD-20); `Destinations` / `from_parts` / `of` / `dest_path` / `one_segment`
/ `Line` / `parse_line` (§4.1.3, PD-23, PD-25); `read_manifest_full` (§4.1.4, PD-14, PD-21, PD-28) and the manifest
writer; `write_atomic` (§4.1.5, PD-28); the `Journal` types and `check_journal` (§4.1.6, PD-7, PD-25, PD-26); `plan`
(§4.1.7, steps 0-10, PD-24, PD-27, PD-29); `run_install`, `roll_back`, `roll_forward` (§4.1.8, FR-027); `recover` and
`log_recovery` (§4.1.9); `run_uninstall` (§4.1.10, FR-122); `action_gate` and `refusal` (§4.1.11, PD-16, PD-17);
`Busy` (§4.1.12, released in `Drop`, so on unwind too); `LinuxDriver::run` (§4.1.1: the batch in order, a failure never
stops the next, one `info!` per result); `for_command` / `remove_for_command` (§4.1.13, PD-1 to PD-4, PD-23); logging
(§4.1.14, FR-117: `KeptPath` only, no `Path::display`). Then `context.rs` (TA-7; plan §4.3 `plan.md:735-737`): after the
fold in `assemble_with_os` (`context.rs:105-106`), build the `AppContext`, call `install::recover(&ctx,
&install::FsInstallIo, ExtractEnv { ctx: &ctx, io: &FsExtractIo, limits: &ExtractLimits::PRODUCTION })` and
`install::log_recovery(&report)`, then return the `AppState` as today; `reassemble_with_os` unchanged; `context.rs`
gains nothing else (FR-120). Rule 4a throughout; the guard rules T005 enforces (FR-112 / PD-20 tokens only inside `impl
InstallIo for FsInstallIo`; no `Command`, `TcpStream`, `UdpSocket`, `xattr`, `thread :: spawn`, `hard_link`, `persist`,
`tempfile`, `NamedTempFile`, `TempDir :: new`; `request_seam` only directly after `#[cfg(test)]`) apply from the first
save.

**Pass:** L-OWN: every `install::tests::*` and `context::tests::*` name compiled here (`cargo.all` 26 + 1 +
`native_wiring_offers_nothing_windows`) green, quoted by name; the doctest `… compile fail ... ok`; clippy 0 warnings;
the `cfg(unix)` / `cfg(target_os)` tests read twice against their rows (Rule 15; "not compiled here"). At the G2 gate the
same under L-RS, and L-WSL (main loop) compiles and runs the nine Linux names. Record the wall clock of `cargo test -p
krate-manager-core --locked --lib install::` (SC-002 and SC-009 are the heaviest, plan §8 `plan.md:1105-1106`).
**Lane:** app lane (L-OWN, then L-RS / L-APP / L-DEVFEED at the gate; L-WSL main loop).

### T004 [P] — The Phase 9 test doubles and `temp_roots_with_user_presets` (spec FR-115, D12; plan §4.5, SI-15; TA-3, TA-5)

**Files:** `crates/core/src/test_support.rs` (CRLF), `crates/core/src/test_support_tests.rs` (LF). **Target:** slot
`F:/tmp/km9-target/side`, `CARGO_BUILD_JOBS=12` (Rule 10, TA-9). L-OWN: `cargo test -p krate-manager-core --locked
--lib test_support::`. Logs under `F:/tmp/gate/km9-T004/`.

**Test first:** `test_support::tests::phase9_doubles_cases` (TA-6, not required) in `test_support_tests.rs`, one test
with numbered blocks, red first on `not yet implemented: T004`:
1. `MemInstallIo` over a `MemExtractIo` with one staged file planted under `roots.staging_dir()`: `kind_of` of every
   planted root is `Some(Dir)`; `copy_new(staged, dest, sink)` copies the bytes, feeds the sink the same bytes, returns
   their length; a second `copy_new` to the same `dest` is `AlreadyExists` (`create_new`); `rename` of a directory moves
   its subtree, onto a non-empty directory is `DirectoryNotEmpty`; `remove_dir` of a non-empty directory fails;
   `kind_of` / `read_link` / `remove_file` of a link act on the link; a lookup through a link in a non-final component
   follows it (`<base>/a -> <base>/real`, `kind_of(<base>/a/x)` is `x`'s kind); `canonicalize` resolves Mem links
   component by component; a mutating call under the staging dir is an error and changes nothing; `calls()` records
   every call as its `IOp` in order and `mutating_calls()` counts only `is_mutating()` ones.
2. `FaultyInstallIo`: `fail(IKind::Rename, 2, StorageFull)` fails only the second `rename` with that kind;
   `fail_from(IKind::WriteNew, 1, PermissionDenied)` fails every `write_new`; `crash_after(3)` lets three mutating calls
   through and fails every later one with `io::ErrorKind::Other` without forwarding (the inner Mem is unchanged by them);
   `on_call(IKind::CopyNew, 1, hook)` runs `hook` with the inner IO just before the first `copy_new` is forwarded;
   `without_symlinks()` makes `supports_symlinks()` false and forwards everything else; reads are never counted by
   `crash_after`.
3. `RecordingInstallIo::fail_closed()`: every mutating call returns `io::Error::other("fail-closed")` and is recorded;
   reads forward to `FsInstallIo` (a `kind_of` of a real temp file is `Some(File)`; this row is red on `not yet
   implemented: T003` until T003's `FsInstallIo` lands, the expected in-group red); `forwarding(&mem)` forwards both.
4. `CountingDriver` counts `run` calls and forwards; `RecordingDriver::new(Os::Linux, kept)` records each batch as
   `(ActionKind, name)` and returns one `Ok` outcome per action with `kept`; `OneDriver(Some(&d))` returns `d` only for
   `d.os()`.
5. `InstallRig::command_env` returns an env whose `os`, `arch`, `ctx` are the inputs (the seam is `None`: assert only
   through the public fields).
6. `#[cfg(unix)]` block: `temp_roots_with_user_presets(td, OsStr::from_bytes(b"u\xff"))` spells `presets().user` with
   that leaf, sandboxed to `td`, and does not create it (`symlink_metadata` is `NotFound`).

**Implement:** the bodies of every T001 double per plan §4.5 (`plan.md:751-784`) and TA-5; state in `Mutex` / atomics
(the doubles are `Sync`, `InstallIo: Sync`); no `static`; Rule 4b. `temp_roots_with_user_presets(td: &Path, leaf:
&OsStr) -> Roots` (`#[cfg(unix)]`, SI-15): `roots_at` (`test_support.rs:101-144`) becomes a call to a private
`roots_at_with(base, user_leaf: Option<&OsStr>)` that spells `presets.user` as `presets.join(leaf)` when `Some` and then
skips creating that one directory; the single `#[expect(clippy::disallowed_methods, …)]` `Roots::new` site moves with it,
reason text unchanged (Rule 2); `fresh_temp_roots` and every other caller behave exactly as today.

**Pass:** `phase9_doubles_cases` green once T003's `FsInstallIo` exists (before that, only block 3's first row is red on
the T003 panic); every existing `test_support::tests::*` green; clippy 0 warnings in L-OWN and L-DEVFEED (the doubles
compile under `test-support` with `cfg(test)` off). Delete `F:/tmp/km9-target/` if last of T004 / T005 / T006 (Rule 10).
**Lane:** app lane.

### T005 [P] — `injection.rs` and `roster.rs`: the install confinement, read-token, banned-token, `request_seam` and roster rules with their plants (spec FR-112, SC-019; plan §4.8 rows 1-3 and 6; PD-20, PD-30; SI-2, SI-14, SI-18, SI-27; TA-8, TA-12)

**Files:** `crates/core/tests/injection.rs` (CRLF), `crates/core/tests/roster.rs` (LF). **Target:** slot
`F:/tmp/km9-target/side`, `CARGO_BUILD_JOBS=12`. L-OWN: `cargo test -p krate-manager-core --locked --test injection`
and `--test roster`. Logs under `F:/tmp/gate/km9-T005/`.

**Test first (the plants):** extend the existing planted-case tests (no new test name, plan §4.8 last line
`plan.md:856`) so every new rule has a plant that it reports, and run each red (the plant not reported) before the rule
exists:
- `injection_scanner_reports_planted_violations`, each planted into `install.rs`: `fn f() { std::fs::rename("a",
  "b").ok(); }`; `fn f() { let _ = std::process::Command::new("x"); }`; `fn xattr() {}`; `use flate2::read::GzDecoder;`;
  `fn f() { let _ = tempfile::NamedTempFile::new(); }`; `fn f(d: &std::path::Path) { let _ =
  tempfile::NamedTempFile::new_in(d); }`; `fn f(t: tempfile::NamedTempFile) { let _ = t.persist("x"); }`; `fn f(p:
  &std::path::Path) -> bool { p.exists() }` (PD-20) (plan §4.8 row 1, `plan.md:849`).
- `verified_artifact_scanner_reports_plants`: `fn f() { let _ = verify_file(p, e, k); }` in `install.rs` (reported).
- `fixture_url_seam_scanner_reports_plants`: `struct P { request_seam: Option<u8> }` in `install.rs` (no `cfg(test)`);
  `fn f() { request_seam(); }` in `src-tauri/src/commands.rs`; `fn f(e: &mut CommandEnv<'_>) { e.request_seam = None; }`
  in `test_support.rs` (no `cfg(test)`); each reported (plan §4.8 row 3, `plan.md:851`).

**Implement:** `const INSTALL_RS: &str = "crates/core/src/install.rs";` beside `EXTRACT_RS` (`injection.rs:164`);
`const INSTALL_IO_METHODS: [&str; 9]` (TA-12); `fn install_findings(files)` called from the function that runs
`extract_findings` on the real tree (read it; `fn extract_findings` at `injection.rs:2990`), modelled
on `extract_findings`' impl-body confinement and `io_method_site` handling: (1) `EXTRACT_FS_TOKENS` (`:325-344`) plus
`create_dir_all`, `sync_all`, `sync_data`, `OpenOptions` and PD-20's read tokens (`fs :: read`, `fs :: read_dir`, `fs ::
metadata`, `fs :: symlink_metadata`, `fs :: canonicalize`, `fs :: read_link`, `File :: open`, `. exists (`, `.
try_exists (`, `. is_dir (`, `. is_file (`, `. is_symlink (`, `. metadata (`, `. symlink_metadata (`) only inside `impl
InstallIo for FsInstallIo`, except `fn <m>` inside `trait InstallIo` / any `impl InstallIo for …` and `. <m> (` for `m`
in `INSTALL_IO_METHODS`; (2) `Command`, `TcpStream`, `UdpSocket`, `xattr`, `thread :: spawn`, `hard_link`, `persist` and
every `TEMPFILE_REAL_TEMP` token (`:361-367`) nowhere in `install.rs` (each token in exactly one of the two lists);
(3) the existing `flate2` / `sha2` / `StagedTree {` confinement rules now also see `install.rs` (check they already scan
every `crates/core/src` file; if one has a fixed file list, add `INSTALL_RS`). `fixture_seam_findings` (`injection.rs:3323`):
a third name `request_seam`, allowed in `install.rs` only directly after `#[cfg(test)]` (declaration, the
`native_env` initialiser, the `for_command` application; the `pub(crate)` form as `is_crate_private` handles), in
`test_support.rs` only as a field initialiser directly after `#[cfg(test)]`, free in `INSTALL_TESTS_RS`, nowhere else
(PD-30). `roster.rs`: `identifier_files()` (`roster.rs:247`) and its must-scan list gain
`crates/core/src/install.rs`; existing plants unchanged.

**Pass:** every plant reported; every `injection` and `roster` test green **on the real tree** (`install.rs` from T003 as
it stands; a real finding there is T003's to fix, reported to the main loop with the finding text if T003 has finished);
0 warnings. Delete `F:/tmp/km9-target/` if last (Rule 10).
**Lane:** app lane.

### T006 [P] — `status::tests::actions_per_state` (spec SC-016 status half, FR-093, FR-100, D13; plan §7.1 SC-016 row, PD-15, PD-16)

**Files:** `crates/core/src/status_tests.rs` (LF). **Target:** slot `F:/tmp/km9-target/side`, `CARGO_BUILD_JOBS=12`.
L-OWN: `cargo test -p krate-manager-core --locked --lib status::tests::actions_per_state`. Logs under
`F:/tmp/gate/km9-T006/`.

**Test first, and the whole task:** `status::tests::actions_per_state`, red first on `not yet implemented: T003`
(expected in-group red until T003's `action_gate` / `FsInstallIo` and T004's doubles land). Over `fresh_temp_roots()`
and a `FixtureServer` feed (no asset is ever fetched: no `request_seam` is needed or named here, PD-30), one plugin per
row (`Alpha`, `Beta`, … — invented names; the Phase 6 fixture states of `status_tests.rs`' scenarios rebuilt on disk:
not installed, elsewhere (a copy under another plug-in root of the roots), up to date, update available, installed
newer, version unknown, not available, needs newer OS, manifest present, manifest absent, journal pending (a
`<state>/journal/<stem>.json` planted), manifest failing FR-041 (a `..` path), manifest root changed (another
`plugin_root` string)), call `status::build_with_os(os, arch, &ctx, FetchMode::Normal, now, &host, &FsInstallIo)` and
assert each card's `actions` equals the expected table for `(Os::Linux, Arch::X86_64)` (`[]` for the last three rows,
`["install"]` for not installed and elsewhere (FR-124), `["update", "remove"]` / `["update"]` for update available with /
without a manifest, `["remove"]` for up to date with a manifest, `[]` for not available and needs newer OS) and is `[]` in
every row for `(Os::Windows, Arch::X86_64)` and `(Os::Macos, Arch::Aarch64)` (PD-15: no gate read there). Then, for each
Linux row, every action the table omits is refused by `install::for_command` / `remove_for_command` (a
`InstallRig::command_env(Os::Linux, Arch::X86_64, &RecordingInstallIo::fail_closed(), &FsExtractIo, &NativeDrivers)`)
with its FR-093 / FR-094 code (`refusal`'s table, `spec.md:602-606`; the gate's code for the blocked rows), zero
mutating calls and 0 asset requests (the fixture server's request log). Rule 4b (`status_tests.rs` is in
`FR088_FILES`).

**Pass:** green once T003 and T004 have landed; 0 warnings. Delete `F:/tmp/km9-target/` if last (Rule 10).
**Lane:** app lane.

### T007 [P] — The UI: action buttons, confirmation, progress, outcome, refresh (spec FR-097 to FR-099, FR-124, SC-024; plan §4.7, PD-22; SI-14)

**Files:** `src/status.ts` (LF), `src/view.ts` (LF), `src/status.test.ts` (LF), `src/main.ts` (CRLF; only if a signature
change requires it; plan §4.7: "unchanged in behaviour"). No Rust. Logs under `F:/tmp/gate/km9-T007/`.

**Test first:** the 17 SC-024 titles in `src/status.test.ts`, spelled exactly as `spec.md:938-945` (and T001's
`required-tests.json`), each red first (missing export or failing assertion): `renders install for not-installed linux`,
`renders update and remove for update-available linux`, `renders no install buttons on windows or macos`, `install asks
for confirmation naming the plugin and version`, `install on an elsewhere copy names the other copy and says to remove
it by hand`, `install without an elsewhere copy shows no other-copy line`, `update asks for confirmation naming both
versions`, `remove asks for confirmation first`, `cancelling a confirmation invokes nothing`, `confirmed install sends
name, version, action and a channel`, `disables the card's buttons while an action runs`, `shows placing progress`,
`shows the outcome and the DAW notice after an install`, `shows the outcome and the DAW notice after an update`, `shows
kept files after remove`, `shows the error code on failure`, `refreshes the status after an action and keeps the
outcome`. Inputs per plan §7.1 SC-024 (`plan.md:1023`): `PluginView` literals through `toStatusView`; a recording
`Invoker` (`status.ts:87`); a fake `ChannelMaker` recording `onMessage` and returning `{ id: n }`; deferred promises to
hold an action; the `elsewhere` path `/opt/vst3/Alpha.vst3` (never `/usr/`). "Invokes nothing" = the invoker recorded no
call; "confirmed install sends …" asserts exactly `{ name, expectedVersion, action, onProgress: <the fake channel> }`.

**Implement:** plan §4.7 (`plan.md:813-843`) verbatim: in `view.ts`, `ActionSummary`, `KeptPath`, `toActionSummary`,
`InstallProgress` (six kinds; `download` carries `event`, PD-11) and `toInstallProgress` (every incoming value through
its parser); in `status.ts`, `ChannelMaker`, `tauriChannel` (`import { Channel } from "@tauri-apps/api/core"`),
`INSTALL_TEXTS` (exact strings of `plan.md:822-829`), the `div.actions` row, the in-card `div.confirm`, `p.progress`,
outcome lines kept in the section's `Map<string, string[]>` until that plugin's next action or `Check now`, and the
optional `makeChannel = tauriChannel` parameter on `createStatusSection`, `renderCards`, `mountStatus` (existing callers
and tests compile unchanged). Every string through `textContent`. Rule 4f (no navigation needle, no `innerHTML`, no URL,
no `eslint-disable`); the non-test sources stay exactly `main.ts`, `notes.ts`, `status.ts`, `view.ts`
(`status.test.ts:380-383`); `STATUS_TEXTS` is unchanged so its coverage test (`:260-269`) is unaffected.

**Pass:** L-TS: typecheck and lint exit 0, vitest 46 titles passed (29 + 17), each new title exactly once.
**Lane:** app lane (L-TS).

### T008 [P] — Evidence scripts: scope check, L-WSL, L-CLOSE (spec FR-120, FR-121, SC-025, SC-026; plan §7.3, §7.4, §8; TA-10, TA-11)

**Files (create, LF, Node only, no dependencies):** `specs/krate-manager-phase9/evidence/scope-check.js`,
`specs/krate-manager-phase9/evidence/wsl-lane.js`, `specs/krate-manager-phase9/evidence/close-lane.js`. Clone
`specs/krate-manager-phase8/evidence/{scope-check,wsl-lane,close-lane}.js` (322 / 225 / 870 lines, read this session) and
change only what Phase 9 needs. Logs under `F:/tmp/gate/km9-T008/`.

**Test first:** each script's `--self-test` (pure checks over synthetic inputs and the real workflow text), written and
run red before the checks it tests exist; last lines `SELF-TEST GREEN (<n> cases)` / `WSL LANE SELF-TEST GREEN (<n>
cases)`.

**Implement:**
- `scope-check.js`: `BASE = 'abdd2c9e'`; allowed paths exactly Rule 11's (outside the app: `specs/krate-manager-phase9/`,
  `specs/INDEX.md`, the shape test limited to the `EXPECTED_REQUIRED` window, the roadmap limited to the three FR-123
  windows of plan §4.10 plus the Phase 9 status line); inside the app the Rule 11 list; forbidden prefixes `plugins/`,
  `dsp/`, `cmake/`, `extern/`, `.github/workflows/`, `tools/feed-contract/` except the shape test, and the forbidden app
  files of Rule 11 (`extract.rs`, `verify.rs`, `feed.rs`, `version.rs`, `roots.rs`, `settings.rs`, every `Cargo.*`,
  `package*.json`, …). Self-test cases for each allowed and each forbidden class, and for an out-of-window roadmap hunk.
- `wsl-lane.js`: `LINUX_RUN_TESTS` = the eight `install::tests::*_unix` names + `install::tests::native_wiring_installs_
  linux`; `INFO_PREFIX = 'Linux-run Phase 9 tests seen ok:'`; log `F:/tmp/gate/km9-wsl/wsl-core-<stamp>.txt`; ceiling
  40 min; otherwise Phase 8's form (TA-10).
- `close-lane.js`: Phase 8's rows with Phase 9's values: A1j `git diff -U0 abdd2c9e -- apps/krate-manager` + every
  untracked app file read whole (no added `allow(` / `expect(` attribute, `@ts-ignore`, `eslint-disable`); A1k becomes
  the FR-121 check (no diff under `plugins/`, `dsp/`, `cmake/`, `extern/`, `.github/workflows/`); A1m: `git diff
  --numstat abdd2c9e` of `apps/krate-manager/Cargo.lock` and `tests/fixtures/clippy-guard/Cargo.lock` both **empty**
  (Rule 8); A1h `cargo tree -p krate-manager-core -e normal --locked --prefix none --format "{p}"` with no `tauri` /
  `tokio` / `reqwest` / `hyper` / `dirs` / `home` (plan §7.4 step 6); A1l dropped (Phase 8-specific); A3b `node --test`
  `plan=1..59`; B1 the `required tests ran` step expecting `::notice title=required tests ran (Windows)::288 cargo + 46
  vitest`; **B2-B7 the `tauri build` and `build warnings` steps** (TA-11, Phase 7's rows, `specs/krate-manager-phase7/
  evidence/close-lane.js`), run after A; a row checking the two generated permission files exist (TA-16); the
  `roadmap_amendments_present` row (plan §7.4 step 10: the three `rg` needles, one hit each, SC-030); A9 the `todo!(` /
  `unimplemented!(` grep outside `*_tests.rs` and `tests/`; A10 L-WSL; C1-C3 as Phase 8 (C3 `node
  tools/clean-test-output.js --apply`). It writes `specs/krate-manager-phase9/evidence/timings.md` and copies
  `cargo-test.txt`, `clippy-guard.txt`, `core-tree.txt`, `wsl-core.txt`, `diff-u0.txt` as Phase 8 did. Last line `CLOSE
  LANE GREEN` | `CLOSE LANE RED: <rows>`. Every child under a wall-clock ceiling; independent rows concurrent.

**Pass:** `node --check` on the three files; each `--self-test` green; `node specs/krate-manager-phase9/evidence/
scope-check.js` exit 0 on the tree as it stands (re-run at the G2 gate). Never run `wsl-lane.js` or `close-lane.js`
without `--self-test` here (main-loop lanes).
**Lane:** pipeline lane.

---

## G3 — Integration (sequential: one task)

### T009 — L-REQ (the per-OS CI lists against the real run), INDEX, lints, scope, greps, the PENDING-CI list and the close hand-over (spec FR-116, FR-120, FR-121, SC-025, SC-026, SC-030; plan §7.3, §7.4; TA-1, TA-16)

**Files:** possibly `tests/required-tests.json` and `tools/feed-contract/tests/manager-ci-shape.test.js` (only if step 2
finds a name that differs from the real test; this task is alone in G3, so it may edit shared files), `specs/INDEX.md`
(only through `node tools/gen-specs-index.js`). Logs under `F:/tmp/gate/km9-T009/`.

**Test first:** `node tools/gen-specs-index.js --check` (quote its exit code and, if red, the first differing line) and
L-REQ (expected `288 cargo + 46 vitest`).

**Steps:**
1. L-RS (nothing else executing; record the wall clock and every `test result:` line) and L-TS with the vitest JSON
   report.
2. L-REQ over step 1's logs. A `::error … 0 passing line(s)` names a required name the run lacks: compare it with the
   test's real name; if the test is right and the list is wrong, fix both lists (same order, both files), re-run L-NODE +
   L-TAP and L-REQ; if the test is misnamed, it is a T003 / T006 / T007 defect: return `blocked` naming it.
3. L-APP, L-DEVFEED, L-GUARD, L-NODE + L-TAP, L-VLINT, L-LINT (`node tools/lint-serial-test-runs.js`).
4. L-INDEX: if red, run `node tools/gen-specs-index.js` once and re-check.
5. L-SCOPE (`git diff --name-only` / `git status --short` against Rule 11; FR-120, FR-121) and `git status
   --porcelain` must show the two generated permission files as untracked (TA-16).
6. Greps (Rule 9, SC-026, SC-030): no `todo!(` / `unimplemented!(` outside `*_tests.rs` and `tests/`; no added `#[allow`
   / `#[expect` (`git diff abdd2c9e -- apps/krate-manager` + untracked files); no `--test-threads` anywhere in the app or
   the evidence scripts; the three roadmap needles of plan §7.4 step 10 one hit each.
7. Not applicable, recorded with the reason: `node tools/check-portability.js` (no C++ file changed), `node
   tools/lint-plugin-editor-baseline.js` (no plugin editor changed), `run-gate.js` / `header-consumers.js` (no C++ test
   target), clang-tidy and pluginval (no plugin code).
8. Return with notes "deferred to the main loop at close: run `node specs/krate-manager-phase9/evidence/close-lane.js`"
   (L-CLOSE: the plan §7.4 procedure, the `tauri build` rows, L-WSL; expected a few minutes: Phase 8's 80.8 s + a
   ~100 s release build + the first Linux compile of the module), the PENDING-CI list below, and the user's commit
   reminder for the two generated permission files (TA-16; CI step `Checkout unchanged`).

**Pass:** every step green or recorded not applicable; the hand-over notes.
**Lane:** app lane + pipeline lane; L-CLOSE and L-WSL deferred to the main loop.

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

Each row closes on a leg only when the run's committed `tests/required-tests.json` lists its names and that leg's step
`required tests ran` (`.github/workflows/manager-ci.yml:153-178`) passed with no `::error`; the evidence is step `cargo
test` (`cargo test --workspace --locked`, `:117-120`) on job `check (macos-latest)` / `check (ubuntu-24.04)` (matrix
`manager-ci.yml:28-34`). L-WSL is an early signal only and closes none of them (plan §7.4).

| # | SC / FR | Job(s) | Exact assertion |
|---|---|---|---|
| 1 | SC-001 to SC-022, SC-024, SC-026, SC-027, SC-030 (`cargo.all` + vitest) | `check (macos-latest)`, `check (ubuntu-24.04)` (and `check (windows-latest)`) | the 28 `cargo.all` names and the 17 SC-024 vitest titles each pass exactly once; steps `cargo fmt`, `clippy`, `clippy dev-feed`, `clippy-guard`, `cargo test`, `typecheck`, `lint`, `vitest` green |
| 2 | SC-001 Unix (modes, exec bit, in-bundle link) | both | `install::tests::install_update_remove_cycle_unix ... ok` |
| 3 | SC-002 Unix (crash cuts on a real POSIX filesystem, directory renames) | both | `install::tests::crash_after_each_step_recovers_unix ... ok` |
| 4 | SC-003 Unix (directory symlink escaping the root) | both | `install::tests::manifest_outside_roots_refused_unix ... ok` |
| 5 | SC-009 Unix (`symlink`, `set_mode`, `sync_dir` faults) | both | `install::tests::io_failures_roll_back_unix ... ok` |
| 6 | SC-011 Unix (symlinked target, PD-24 / PD-28 links) | both | `install::tests::collisions_refused_before_write_unix ... ok` |
| 7 | SC-023 (non-UTF-8 preset root) | both | `install::tests::non_utf8_root_refused_unix ... ok` |
| 8 | SC-028 (symlinked plug-in root) | both | `install::tests::symlinked_plugin_root_unix ... ok` |
| 9 | SC-030 Unix (a link inside the bundle removed as the link) | both | `install::tests::remove_bundle_link_unix ... ok` |
| 10 | SC-029 Linux (native wiring installs) | `check (ubuntu-24.04)` | `install::tests::native_wiring_installs_linux ... ok` |
| 11 | SC-029 macOS (native wiring offers nothing) | `check (macos-latest)` | `install::tests::native_wiring_offers_nothing_macos ... ok` |
| 12 | Roadmap line 702 (`cargo test -p krate-manager-core install::`) | `check (ubuntu-24.04)` | the `install::tests::` subset of the same `cargo test` log, every line `ok` |
| 13 | SC-025 / FR-116 totals | all three | the step's notice prints `names.length` = `all` + the leg's lane (`manager-ci.yml:163`, `:177`): `::notice title=required tests ran (macOS)::320 cargo + 46 vitest` (276 + 44), `(Linux)::315 cargo + 46 vitest` (276 + 39), `(Windows)::288 cargo + 46 vitest` (276 + 12) |
| 14 | SC-026 (zero warnings, macOS / Linux) | both | steps `cargo fmt`, `clippy`, `clippy dev-feed`, `typecheck`, `lint` green; `tauri build` + `build warnings` `::notice … 0 warning lines in the release build` (`manager-ci.yml:180-198`) |
| 15 | TA-16 (generated permission files committed) | all three | step `Checkout unchanged` `::notice title=checkout unchanged (<OS>)::clean` (`manager-ci.yml:299-307`) |

---

## Defects found (in the plan, while writing this list)

| Id | Defect | Evidence | Resolution |
|---|---|---|---|
| D-1 | Plan §4.8 row 4 writes `FR088_FILES` `[…; 8]` and `PHASE7_TEST_FILES` `[…; 6]` and cites `injection.rs:250-256` / `:186-191`; today they are `[&str; 6]` at `:248-255` and `[&str; 4]` at `:186-191` | `grep -n` this session | the sizes after adding the two files are 8 and 6; T001 step 10 |
| D-2 | Plan §7.1 SC-001 asserts "mode under `cfg(unix)`" through `snapshot`, which returns `(path, kind, len)` only | `test_support.rs:1747-1780` | TA-13: a local `cfg(unix)` mode reader in `install_tests.rs` |
| D-3 | Plan §4.5's `MemInstallIo::new(base, staged)` cannot route staged reads: `MemExtractIo.base` is private with no accessor | `test_support.rs:1095-1101` | TA-5: `new(base, roots, staged)` |
| D-4 | Plan §4.8 row 1 says `INSTALL_IO_METHODS` is "the 19 FR-028 method names that are also tokens" but lists nine | `plan.md:849` | TA-12: the nine names |
| D-5 | `for_command` / `remove_for_command` take `CommandEnv` by value and tests issue several commands per rig, but plan §4.1.1 gives `CommandEnv` no derive | `plan.md:332-336`, `:668-672` | TA-4: `#[derive(Clone, Copy)]` |
| D-6 | Plan §7.4 has no `tauri build` step although `src-tauri` and the webview change | `plan.md:1074-1093`; FR-096, FR-097 | TA-11: L-CLOSE runs the two CI steps |
| D-7 | Plan §2 lists the two generated permission files as "committed" but no step says that CI's `Checkout unchanged` fails until the user commits them | `plan.md:206`; `manager-ci.yml:299-307` | TA-16: named in T009's hand-over and PENDING-CI row 15 |
- **D-9 (main-loop close lane, 2026-11-01).** The WSL lane (L-WSL) could not compile core's Linux test build: FR-114's
  move of the tar builder left `Tar::mtime` private while the cfg(unix) `extract::tests::special_bits_and_mtime_not_applied_unix`
  sets it (E0616), and `native_commands` / `offers_nothing` in install_tests.rs were dead code on Linux. Fixed: the
  field is `pub(crate)`, the two helpers carry their callers' `#[cfg(any(windows, target_os = "macos"))]`. The
  Windows gate never compiles cfg(unix) code; only L-WSL or CI sees it.
- **D-10 (main-loop close lane, 2026-11-01).** `install::tests::remove_reports_another_kind_at_a_directory_entry` failed
  on Linux with `Io { step: RemoveEntries, kind: NotADirectory }`: `FsInstallIo::kind_of` mapped only NotFound to
  absent, and Linux returns ENOTDIR for a path under a file. Fixed in `kind_of` (NotADirectory is absent too).
