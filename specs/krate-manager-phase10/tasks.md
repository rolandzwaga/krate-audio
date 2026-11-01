# Tasks: Krate Manager Phase 10 — The Elevated Helper and the Windows / macOS Install Drivers

**Spec:** `specs/krate-manager-phase10/spec.md` (DRAFT revision 3: D1-D19, FR-001 to FR-135, SC-001 to SC-032;
Clarifications Q1-Q8 and D2/D7/D8/D11 of 2026-11-01, `spec.md:1361-1376`) · **Plan:**
`specs/krate-manager-phase10/plan.md` (DRAFT revision 2: PD-1 to PD-27 `plan.md:36-66`, SI-1 to SI-31 `plan.md:68-102`,
module design §4 `plan.md:250-960`, test plan §7 `plan.md:1017-1100`, CI §8 `plan.md:1104-1142`, risks §9, preconditions
§10 `plan.md:1167-1173`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 10 (lines 709-810); Phase 4 line 477-479 (the launcher, FR-123 (a));
Phase 11 lines 827-838 and 864 (FR-123 (d)); Phase 12 lines 906-912 (FR-123 (g)); Cross-Cutting Constraints lines
987-1028 (the hosted-runner exception, 994-996)
**Status:** DRAFT task list, revision 1, 2026-11-01. Written against `feat/krate-manager` at `4a8b3e2a` (`git rev-parse
--short HEAD` this session; `git status --porcelain` shows only `?? specs/krate-manager-phase10/`). Every app file:line
quoted below was re-read this session at `4a8b3e2a`.
**Spec state:** none of plan §0.2's edits is in `spec.md` yet: `spec.md:1104` still names
`main::tests::ci_roots_flag_is_usage_error`, `spec.md:1105-1106` `main::tests::ci_roots_file_is_read`, `spec.md:738`
`ubuntu-latest`, `spec.md:797` FR-109 "before `required tests ran`". Plan §10 (`plan.md:1171`) asks the main loop to rule
SI-1 to SI-31 into the spec "before tasks". Following the Phase 8 / Phase 9 precedent
(`specs/krate-manager-phase9/tasks.md:12-18`), every task below states its assertions from the plan's §0.1 / §0.2 / §4 / §7
rows, which are binding as written, so no build task waits for the spec text; T002 writes the edits into `spec.md` in G2
beside the code.
**Lanes in this phase:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy ci-temp-roots / clippy
manual-elevation example / clippy-guard / cargo test, the helper's feature test run, npm typecheck / lint / test, `node
--test "scripts/*.test.js"`, the version lint), the **pipeline lane** (`node --test` over `tools/feed-contract/tests/`
plus the feed-tools TAP check, because `manager-ci-shape.test.js` changes; `npx yaml valid` over `manager-ci.yml`; the
repo lints; the evidence scripts' `--self-test`) and three **main-loop lanes** (L-WSL: core **and helper** clippy + tests
in WSL, the only local compile of the `cfg(unix)` code; L-BUNDLE: `npm run bundle`; L-CLOSE). No file under `plugins/`,
`dsp/`, `cmake/` or `extern/` changes (spec FR-122, `spec.md:830-831`), so there is **no C++ lane**: no `run-gate.js`, no
`header-consumers.js`, no `check-portability.js`, no clang-tidy, no pluginval, no `lint-plugin-editor-baseline.js`.

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks at the same time in one working tree. A task is
  **[P]-safe** when no other task in its group touches any of its files and none of its files is a **shared file**: any
  `Cargo.toml`, `Cargo.lock` (the workspace's, `tests/fixtures/clippy-guard/Cargo.lock`), `clippy.toml`, a crate root
  (`crates/core/src/lib.rs`, `crates/helper/src/lib.rs`, `src-tauri/src/lib.rs`), `src-tauri/src/commands.rs`, any
  `tauri*.conf.json`, the capability file, `package.json`, `tsconfig*.json`, `.gitignore`, a workflow,
  `tests/required-tests.json`, `manager-ci-shape.test.js`, `crates/core/tests/config_guard.rs`, `specs/INDEX.md`.
- **Every shared file G2 needs is edited in one task, T001, which is G1 on its own (sequential).** It writes every
  manifest, lockfile, config and crate-root line of the phase, **every `pub mod` / `mod` line** (with a compiling
  placeholder file per module), the whole public surface of the helper crate and of the new `install.rs` items
  (placeholder bodies, `todo!("T0nn")` naming the owner), the call-site changes those signatures force, the small real
  pieces other tasks compile against, the guard rows its own edits trip **and the allow-list homes G2 code will need**,
  and `required-tests.json` with `EXPECTED_REQUIRED` (TA-1). The sixteen G2 tasks then touch only their own files: a G2
  file is either new in this phase (created by T001 as a placeholder for exactly one G2 task, or created by that task) or
  an existing non-shared file that exactly one G2 task edits. The file-ownership table is in "Dependency overview".
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block is too long to copy without drift (a type list,
  a test table, an algorithm), the section names the `plan.md` section or §7.1 row, or the spec FR / SC line range, that
  holds it; that text is then **binding, verbatim**, as amended by plan §0.1 / §0.2 (`plan.md:36-102`) and this file's TA
  rows. Where this file and the plan differ, this file wins and the TA row says why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test was
  run and failed for the stated reason before the implementation existed; the notes quote the failing line. The red run
  normally uses T001's placeholder, so the test compiles and fails on the `todo!()` panic ("not yet implemented: T0nn") or
  on its assertions. A test that cannot run on this box (`cfg(unix)`, `cfg(target_os = "macos")`) has no local red: it is
  recorded "not compiled here", read twice against its SC row before the task ends, and first runs in L-WSL (Rule 15).
- **In-group expected reds (G2 only).** A G2 test whose path crosses another G2 task's placeholder fails with that
  task's panic text until it lands. The complete list (each text is raised **from the named file**):
  (a) helper: T003's `core_written_jobs_parse` → `T009` (`crates/core/src/install.rs`), `T004`, `T005`, `T006`, `T007`,
  `T008`; T004's `roots::tests::*` / `cli::tests::*` → `T007` (`seams.rs`), `T008` (`os.rs`); T005's `validate::tests::*`
  → `T003`, `T004`, `T006`, `T007`, `T008`; T006's `ops::tests::*` → `T003`, `T005`, `T007`, `T008`; T007's
  `seams::tests::fake_world_cases` → `T008`. (b) core: T009's loopback rows (SC-003 twin, SC-014,
  `job_hash_is_the_verified_digest`) → `T010` (`install_loopback_tests.rs`) and every helper task; T010's
  `install::loopback::loopback_smoke` → `T009` and every helper task; T011's two tests → `T009`. (c) T012's real-tree rows
  report another task's file whenever it really breaks a rule: that is a real finding for that task, not an expected red.
  Those panic texts are the only failure texts that count as expected, and **every one is gone by the G2 gate**; any red
  at a gate is a real red. A **compile error is never an expected red** (Rule 10).
- **Main-loop lanes.** Steps longer than ~5 minutes or that need WSL run only in the main loop, from scripts T017 writes
  under `specs/krate-manager-phase10/evidence/`: `wsl-lane.js` (L-WSL; Phase 9 measured `WSL LANE GREEN (338 passed,
  64 s)`, 64.6 s, `specs/krate-manager-phase9/evidence/timings.md` row A10; this phase's first run compiles the helper
  crate and every new `cfg(unix)` test cold), `bundle-lane.js` (L-BUNDLE: the helper's release build plus `tauri build`;
  Phase 9's `tauri build` row B2 took 108.0 s) and `close-lane.js` (L-CLOSE: plan §7.4; Phase 9's took 169.1 s). T019
  returns with notes "deferred to the main loop at close: run `node specs/krate-manager-phase10/evidence/close-lane.js`".
- **The M-1 fence (spec FR-063, SC-032; Clarifications Q2).** This build stops once G3 is green. The resume task R1 (after
  the PENDING-CI list) is **blocked** until the user has pushed and `inno-locked-file (windows-latest)` has run; it is not
  part of this build, and Phase 10 is not closed before it.
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system.** No test, gate or agent runs the helper elevated, raises a UAC or osascript prompt,
   runs an Inno setup, `installer`, `pkgbuild`, `pkgutil --forget` or `killall`, or reads or writes a real HKLM uninstall
   key, `C:\Program Files\Common Files\VST3`, `C:\ProgramData\Krate Audio`, `C:\Windows\Temp`, `/Library/…`,
   `/Applications`, `~/.vst3` or the real state dir (spec FR-110, `spec.md:805-808`). Helper tests run unelevated over
   `tempfile::TempDir` roots through `seams::fake` (whose `TempIo` panics "outside the test sandbox" on any write, open
   or read outside its base, plan §4.1.9 `plan.md:466-471`); every loopback root, Windows `run_base` included, lies under
   the test's temp base (plan §4.4 `plan.md:711-720`). Core tests build roots from `test_support::fresh_temp_roots()`
   (`crates/core/src/test_support.rs:203`) or T001's `fresh_elevated_roots()`, use `FakeElevation` / the loopback and
   `FakeHostIo`, never `NativeElevation` or `OsHostIo`. `AllowedRoots::production()` is called in tests only for its
   read-only known-folder lookups (SC-007). The only processes local tests start: the helper binary **with no
   arguments** (SC-023), `argv-echo` (SC-015), and on Unix only (L-WSL, CI) `/usr/bin/mkfifo` and `/bin/sleep 30` inside a
   `TempDir` (killed by the test). The only network peer is the loopback `FixtureServer`; no test calls `github.com`,
   `krateaudio.com` or `api.github.com`. No private key is written to any file (`TestSigner` keys live in memory). No task
   runs `src-tauri/examples/manual_elevation.rs` (FR-105: a person runs it by hand), launches the built app, or runs the
   bundled helper, an NSIS setup or a fixture installer.
2. **No lint suppression added.** `[workspace.lints.clippy] allow_attributes = "deny"`, `allow_attributes_without_reason
   = "deny"` (`apps/krate-manager/Cargo.toml:51-53`): never `#[allow]`. No new `#[expect(…)]` is expected (SC-026,
   `spec.md:1145-1150`); one that proves unavoidable carries its `reason = "…"` and is listed in the task's notes for the
   reviewer. The `#[expect(clippy::disallowed_methods)]` site count stays as it is
   (`crates/core/tests/injection.rs:3873-3895`). No `// @ts-ignore`, `@ts-nocheck`, `eslint-disable`. A warning is fixed in
   the code, never silenced.
3. **Unit tests live in `<stem>_tests.rs`**, declared at the end of `<stem>.rs` as `#[cfg(test)] #[path =
   "<stem>_tests.rs"] mod tests;` (an inline `mod tests {` is reported, `injection.rs:1301-1306`). That holds for the
   helper crate too (plan PD-1, `plan.md:40`).
4. **Literal hygiene.** (a) Core files outside `platform/` hold none of `FORBIDDEN_OUTSIDE_PLATFORM`
   (`injection.rs:23-31`: `"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`, `krateaudio.com`, `api.github.com`), comments
   included; the `killall` argv lives only in `platform/mod.rs` (`KILLALL_ARGV`, PD-14). (b) **`FR088_FILES`**
   (`injection.rs:253-262`: `detect_tests.rs`, `status_tests.rs`, `notes_tests.rs`, `test_support.rs`,
   `src/status.test.ts`, `src/main.test.ts`, `install_tests.rs`, `tar_builder_tests.rs`) may match no `FR088_PATTERNS`
   entry (`:264-276`: `--test-threads`, `127\.0\.0\.1:[0-9]+`, `localhost:[0-9]+`, `OsHostIo`, `HKEY_`, `/Library`,
   `/usr/`, `%[A-Z_]+%`, `AppData`, `ProgramData`, `Program Files`), comments included (TA-12). (c) Test sources
   (`is_test_source`, `injection.rs:1559-1567`: `crates/*/src/**/*_tests.rs`, `crates/*/tests/*.rs`) hold no
   `HOST_LITERALS` value (`:217-227`) except where T001 / T012 exempt a named file; compare with the constant
   (`ops::PKGUTIL`, `ops::UNINSTALL_KEY`, `platform::KILLALL_ARGV`). (d) **No file under `apps/krate-manager/` names any
   of the nine shipped plugins** (`crates/core/tests/roster.rs:128-176`): invented names `Alpha`, `Beta`; the CI fixture
   name `Fixture` / `com.krateaudio.fixture` only under `tests/fixtures/` and in the two elevated jobs (FR-113). (e)
   `install.rs` never holds `_is1` (`roster.rs:200-207`, PD-13) or a `com\.krateaudio\.` / `{XXXXXXXX-XXXX-` literal. (f)
   Non-test TypeScript holds no `h`+`ref`, no `<`+`a`, no `window.`+`open`, no `innerHTML` family, no `http://` /
   `https://` literal (`specs/krate-manager-phase9/tasks.md:126-129`).
5. **Zero warnings.** `cargo fmt --all --check`; every `cargo clippy … -- -D warnings` with 0 `warning:` lines (lib, test,
   feature and example builds); `npm run typecheck`; `npm run lint`; `node --check` on every new `.js`.
6. **Parallel tests.** Never `--test-threads`, `--no-file-parallelism`, `--test-concurrency=1`, `-j 1`. `cargo test` uses
   its default threads; `vitest run` its parallel files; `node --test` runs files in parallel. No test holds a `static`
   lock, a fixed port or a real sleep; time in the helper tests is the fake clock (`seams::fake::World`); each test owns
   its `World`, its `TempDir` and (core) its own `FakeElevation`, whose guard is per instance (PD-9), so parallel tests
   never contend.
7. **`cargo test` is `cargo test --workspace --locked`, `-p <crate> --locked …`, never with `--all-targets`.** `cargo
   clippy --workspace --all-targets` is correct.
8. **Lockfiles and dependencies.** Only T001 changes a `Cargo.toml` dependency or feature, `Cargo.lock`,
   `tests/fixtures/clippy-guard/Cargo.lock` or `package.json`; T016 creates the two new fixture crates' own lockfiles.
   Every other cargo command uses `--locked`. A task that finds it needs a crate, a feature or an npm package stops,
   returns `blocked` and names it. Phase 10 adds no crate (spec "External facts", `spec.md:157-158`; plan §5).
9. **Placeholder discipline.** T001's placeholders compile with zero warnings in every build: every parameter of a
   `todo!()` body is `_`-prefixed; a placeholder struct whose real fields belong to a G2 task holds only `_private: ()`
   (or a `PhantomData` when it carries a lifetime); every `todo!` names its owner; **every helper item the plan marks
   `pub(crate)` is `pub`** (TA-2). The owning task renames parameters, replaces `_private` and removes its `todo!`s. At
   close no `todo!(` or `unimplemented!(` remains outside `*_tests.rs` and `tests/` (T019 step 6), except
   `src-tauri/examples/manual_elevation.rs`, which T013 makes real too.
10. **One working tree, many writers (G2).** Write each file whole, so that it compiles at every save (types and
    signatures first, bodies after). Never edit a file another task owns; when `cargo` fails on a file you do not own,
    wait 60 s and retry, three times, then record it and continue with what you can verify. Target slots (TA-9): the
    shared `apps/krate-manager/target` with `CARGO_BUILD_JOBS=16` for T009, T010, T011, T012, T013 (T009 is the long pole
    and stays warm); `CARGO_TARGET_DIR=F:/tmp/km10-target/helper` with `CARGO_BUILD_JOBS=12` for T003-T008 (they queue on
    its build lock, accepted); `CARGO_TARGET_DIR=F:/tmp/km10-target/fixtures` with `CARGO_BUILD_JOBS=4` for T016. Before
    the first build in a slot run `df -h /f` (5.0 GB free this session, `apps/krate-manager/target` 14 GB): with less
    than 4 GB free use the shared target and say so. The last of T003-T008 / T016 to finish deletes its slot (check the
    other tasks' state first); T019 confirms `F:/tmp/km10-target/` is gone.
11. **Scope fence (spec FR-121, FR-122, `spec.md:828-831`; plan §2 `plan.md:174-208`).** Outside `apps/krate-manager/**`
    only these paths change: `specs/krate-manager-phase10/**`; `specs/INDEX.md` (regenerated only, T019);
    `tools/feed-contract/tests/manager-ci-shape.test.js` (`EXPECTED_REQUIRED`, T001; the shape checks, T018);
    `.github/workflows/manager-ci.yml` (T018); `specs/Krate-manager-roadmap.md` (FR-123 (a), (d), (e), (f), (g) by T002;
    the Phase 10 status line, FR-123 (c) + (h), by T019). Inside `apps/krate-manager/` only plan §2's files change, plus
    this file's TA additions (`crates/helper/src/seams_tests.rs`, `install_loopback_tests.rs`'s smoke test). No change to
    `extract.rs`, `download.rs`, `verify.rs`, `feed.rs`, `detect.rs`, `context.rs`, `feed-tools.yml`, `release.yml`,
    `ci.yml`, any plugin installer script, `tools/feed-contract/` beyond the shape test.
12. **Line endings.** This checkout runs `core.autocrlf=true`. Measured this session (`tr -cd '\r' < f | wc -c` against
    `wc -l`): **CRLF** — root `Cargo.toml` 60/60, `crates/core/Cargo.toml` 50/50, `src-tauri/Cargo.toml` 48/48,
    `crates/core/src/test_support.rs` 3115/3115, `crates/core/tests/injection.rs` 5666/5666, `src-tauri/src/lib.rs` 99/99,
    `specs/Krate-manager-roadmap.md` 1064/1064; **LF** — `crates/helper/Cargo.toml` (0/14), `package.json` (0/25),
    `tsconfig.node.json` (0/12), `.gitignore` (0/5), `install.rs` (0/3179), `install_tests.rs` (0/4260), `status.rs`
    (0/629), `status_tests.rs` (0/1894), `roots.rs` (0/221), `platform/mod.rs` (0/142), `config_guard.rs` (0/2494),
    `roster.rs` (0/366), `src-tauri/src/commands.rs` (0/199), `src/status.ts` (0/538), `src/view.ts` (0/461),
    `src/status.test.ts` (0/791), `tests/required-tests.json` (0/431), `manager-ci.yml` (0/308),
    `manager-ci-shape.test.js` (0/1111), `spec.md` (0/1376), the helper's `lib.rs` / `main.rs` / `tests/stub.rs`. An edit
    keeps the file's ending on every line it adds. New files are written LF. `specs/INDEX.md` is written only by
    `tools/gen-specs-index.js`.
13. **Shell discipline.** Every command runs in the Bash tool (Git Bash), foreground, output to a log under
    `F:/tmp/gate/km10-<task id>/`, then `tail`; never `cat` a whole log, never a drive-wide `find` / `du`. **No heredoc in
    a Bash command** (a repo hook blocks them): write scripts with the Write tool and run them with `node`. Never chain git
    commands. App-lane commands run from `apps/krate-manager/`; repo-lane commands from the repo root.
14. **No invented APIs.** Every crate, std and Win32 API used is one plan §1.2 / §5 (`plan.md:159-170`, `:964-980`) or
    spec "External facts" (`spec.md:155-191`) verified: serde 1.0.229 derive (`deny_unknown_fields`, `duplicate_field`),
    serde_json 1.0.154, sha2 0.11.1 (`Sha256::new` / `update` / `finalize`), windows-sys 0.61.2 (`ShellExecuteExW`,
    `SHELLEXECUTEINFOW`, `SHGetKnownFolderPath`, `FOLDERID_*`, `CoInitializeEx`, `CoTaskMemFree`, `CoUninitialize`,
    `WaitForSingleObject`, `GetExitCodeProcess`, `CreateDirectoryW`, `ConvertStringSecurityDescriptorToSecurityDescriptorW`,
    `LocalFree`, `RegOpenKeyExW` / `RegGetValueW` / `RegCloseKey`, `FILE_FLAG_OPEN_REPARSE_POINT`), std 1.99.1
    (`fs::canonicalize`, `symlink_metadata`, `OpenOptions::create_new`, `OpenOptionsExt::custom_flags`,
    `os::unix::fs::{MetadataExt, DirBuilderExt, PermissionsExt, symlink}`, `os::windows::fs::{symlink_file, symlink_dir}`,
    `process::Command`, `Child::try_wait`, `CommandExt::raw_arg`, `hash_map::RandomState`), tempfile 3.27.0, Node 24
    built-ins (`node:test`, `node:fs`, `node:path`, `node:crypto`, `node:child_process`, `node:url`). An API that turns out
    to differ is a defect of the plan: record it under "Defects found" with the source line, and use the real API.
15. **`cfg` discipline.** The `_unix` tests compile only in L-WSL and on the macOS / Linux CI legs; the `_macos` ones only
    on macOS; the `_windows` ones only here and on `check (windows-latest)`. Write them with the same care, read each
    against its SC row twice, and gate every `use` the way its only users are gated (an unused import is a warning on the
    leg that compiles it). `os.rs`'s `cfg(unix)` and `cfg(windows)` bodies (PD-7) must both compile with no binding unused
    on either side; the hand-defined `O_NONBLOCK` / `O_NOFOLLOW` constants follow PD-6 exactly (`plan.md:45`), with a
    `compile_error!` for any other Unix target.
16. **Every claim cites what was read.** A task's notes cite file:line for every fact about existing code it relied on,
    read in that task's session, and quote the test output lines (`test result:` and the named tests) that close it.

### Lanes (referenced by every task)

| Lane | Commands | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked 2>&1 \| tee F:/tmp/gate/km10-<task>/cargo-test.txt` (Rule 7) | fmt exit 0; clippy exit 0 with 0 `warning:` lines; every test binary `0 failed` |
| **L-OWN** | `CARGO_TARGET_DIR=<slot> CARGO_BUILD_JOBS=<n> cargo test -p <crate> --locked --lib <filter>` (or `--test <name>`) and the same `cargo clippy -p <crate> --all-targets --locked -- -D warnings` (Rule 10; a working loop, not a gate) | the task's own tests and 0 `warning:` lines |
| **L-HFEAT** | `cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings`; `cargo test -p krate-manager-helper --features ci-temp-roots --locked 2>&1 \| tee F:/tmp/gate/km10-<task>/cargo-test-ci-roots.txt` (FR-103a; its own log) | exit 0, 0 `warning:` lines; from T004 on the log holds `test cli::tests::ci_roots_file_is_read ... ok` once |
| **L-APP** | `CARGO_BUILD_JOBS=16 cargo clippy -p krate-manager --all-targets --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-MANUAL** | `cargo clippy -p krate-manager --example manual_elevation --features manual-elevation --locked -- -D warnings` (FR-105; compiled and linted, never run) | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (`.github/workflows/manager-ci.yml:97-115`), extracted into a file by a throwaway Node script (Rule 13) and executed with `"C:/Program Files/Git/bin/bash.exe"` from `apps/krate-manager/` with `RUNNER_TEMP=F:/tmp/gate/km10-<task>` and `RUNNER_OS=Windows` | `::notice title=clippy-guard (Windows)::rc=<non-zero> disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=<≥1> other-errors=0` (Phase 9's reading: `rc=101 … lint-named=5`, `specs/krate-manager-phase9/evidence/timings.md` row A1d), no `::error`; a `--locked` refusal is red |
| **L-TS** | `npm run typecheck`; `npm run lint`; `npm test` | each exit 0; vitest 46 titles before T014, 57 after |
| **L-NAPP** | `node --check` on each new `scripts/*.js`; `node --test --test-reporter=tap "scripts/*.test.js" > F:/tmp/gate/km10-<task>/scripts.tap` from `apps/krate-manager/` (quoted glob) | TAP `# fail 0`; the three node names of TA-1 each `ok` once; no `Warning:` line |
| **L-NODE** | `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km10-<task>/all.tap` from the repo root; read with `tail -12` | TAP `# fail 0`; no `Warning:` / `DeprecationWarning` / `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km10-<task>/all.tap` (it runs the TAPCHECK body of `feed-tools.yml` itself, `tap-check.js:4-7`) | exit 0 and `TAP plan=1..59 top-level=59` (every new shape check is a `t.test` subtest, plan §4.8.4 `plan.md:898-905`) |
| **L-YAML** | `npx --yes yaml@2.6.1 valid < .github/workflows/manager-ci.yml` (the Phase 4 form, `specs/krate-manager-phase4/tasks.md:123`) | exit 0 |
| **L-FIX** | `CARGO_TARGET_DIR=F:/tmp/km10-target/fixtures cargo clippy --manifest-path tests/fixtures/<crate>/Cargo.toml --all-targets --locked -- -D warnings` for `argv-recorder` (with `ARGV_RECORDER_OUT=F:/tmp/gate/km10-T016/argv.json`) and `locked-dll` (with `LOCKED_DLL_VERSION=1.0.0`, then `1.0.1`) | exit 0, 0 `warning:` lines each |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root) | exit 0, no stderr |
| **L-INDEX** | `node tools/gen-specs-index.js --check` (repo root) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase10/evidence/scope-check.js` (T017) | exit 0 |
| **L-REQ** | the `run:` text of `manager-ci.yml` step `required tests ran` (after T018: it also reads the `node` list and the TAP file) extracted as L-GUARD does and run over L-RS's `cargo-test.txt`, a `npm test -- --reporter=default --reporter=json --outputFile.json=<dir>/vitest.json` report and L-NAPP's TAP, `RUNNER_OS=Windows` | `::notice title=required tests ran (Windows)::345 cargo + 57 vitest + 3 node` (328 `all` + 17 `windows`, TA-1), no `::error` |
| **L-WSL** (main loop) | `node specs/krate-manager-phase10/evidence/wsl-lane.js` (T017): in WSL, `CARGO_TARGET_DIR=$HOME/km-target`, `cargo clippy -p krate-manager-core -p krate-manager-helper --all-targets --locked -- -D warnings && cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings && cargo test -p krate-manager-core -p krate-manager-helper --locked` (plan §7.4 step 4, SI-22) | last line `WSL LANE GREEN …`, information line `Linux-run Phase 9+10 tests seen ok: 13/13` (TA-11); it closes no PENDING-CI row |
| **L-BUNDLE** (main loop) | `node specs/krate-manager-phase10/evidence/bundle-lane.js` (T017; plan §7.4 step 5) | last line `BUNDLE LANE GREEN` |
| **L-CLOSE** (main loop) | `node specs/krate-manager-phase10/evidence/close-lane.js` (T017) | last line `CLOSE LANE GREEN` |

**Group gates** (run by the workflow's gate reporter). After G1: L-RS (nothing else executing), L-HFEAT, L-APP,
L-DEVFEED, L-MANUAL, L-GUARD, L-TS, L-NODE + L-TAP, L-VLINT. After G2: the same, plus L-NAPP, L-FIX, L-SCOPE, `node
--check` and `--self-test` on T017's four scripts, and L-WSL beside it (main loop, WSL's own cores). After G3: everything
T019 lists, then L-BUNDLE and L-CLOSE (main loop).

---

## Preconditions — rulings before the build stage

- **Spec rulings (plan §10 row 1, `plan.md:1171`).** The plan asks the main loop to rule SI-1 to SI-31 into the spec
  before tasks. This file is written to every SI row as the plan words it; T002 writes the text. **Precondition check
  (T001 and T002 run it first; on failure the task changes nothing and returns `blocked`, quoting the item):** in
  `spec.md` `## Clarifications` (`spec.md:1361-1376`), a bullet that rules **against** a plan PD / SI row or a TA row of
  this file, or that reverses Q1-Q8. Absence of a bullet is acceptance. The main loop then re-plans the named sections.
- **SI-22 (main loop, not a task).** The phase workflow's WSL step runs `-p krate-manager-core` only
  (`.claude/workflows/krate-manager-phase.js:599`). The main loop extends it to `-p krate-manager-core -p
  krate-manager-helper` (and the helper's `--features ci-temp-roots` clippy) before the G2 gate; T017's `wsl-lane.js`
  already runs both, so the close lane does not depend on that edit.
- **The M-1 fence (Q2).** The user accepts that this build ends at "stopped at the M-1 fence" (SC-032) and that R1 runs
  only after their push. Starting the build is that acceptance.

### Task-list amendments (TA)

Rows marked **spec** change spec text (T002 writes them, tagged `(tasks TA-n)`); the others explain why a task differs
from a plan block or fix what the plan left open.

| Id | Amendment | Why | Spec? |
|---|---|---|---|
| TA-1 | **`tests/required-tests.json` and `EXPECTED_REQUIRED` are written in T001** with exactly plan §4.9's names (`plan.md:907-927`), a new top-level `node` key, and these totals: `cargo.all` 276 − 2 + 54 = **328** (remove `helper_stub_exits_2_with_one_line`, `install::tests::no_driver_off_linux`; add 30 helper names, 23 `install::tests::*` names, `fixture_names_confined`), `cargo.windows` 12 − 1 + 6 = **17**, `cargo.macos` 44 − 1 + 6 = **49**, `cargo.linux` 39 + 4 = **43**, `vitest` 46 − 1 + 12 = **57**, `node` **3**. The exact lists are T001 step 12 | both files are shared, so they belong to the one sequential task; counted this session over `required-tests.json` (`all` 276, `windows` 12, `macos` 44, `linux` 39, `vitest` 46; the four removed names sit in `all`, `all`, `windows`, `macos`) | no |
| TA-2 | **Every helper item plan §4.1 marks `pub(crate)` is `pub`** (`CanonicalRoots`, `canonical`, `JobPlace`, `OpCtx`, `run_batch`, `validate::{job_location, source, uninstaller, pkg_plan, same_path, under}`, `Uninstaller`, `PkgPlan`, `os::{open_regular_no_follow, known_folder}`), with `pub` fields | a `pub(crate)` placeholder (and every field of a `pub(crate)` struct) that no G1 code reads warns `dead_code` at the zero-warning G1 gate (Phase 9 TA-2, same cause); in a library crate a `pub` item is never dead, so T001 can freeze the whole helper API with placeholders and six G2 tasks code against it at once. The helper library is a leaf consumed only by its binary and the tests | spec: New components row for `validate.rs` / `roots.rs` / `ops.rs` / `os.rs` says "public" |
| TA-3 | **The `seams::fake` API is fixed by T001** (exact signatures in T001 step 6); plan §4.1.9 (`plan.md:458-478`) names the types and their powers, not every method | T004, T005, T006, T007 and T010 write against it at once; an unnamed API would be a compile error between parallel writers. T007 may add methods, never rename or change these | no |
| TA-4 | **`install::driver_for` stays Linux-only in T001; T009 flips it** (FR-070) in the same change that replaces `no_driver_off_linux`, `native_wiring_offers_nothing_windows` and `_macos` with `drivers_per_os`, `native_wiring_windows` and `_macos` | flipped in G1, those three Phase 9 tests (`install_tests.rs:3850-3925`) go red at the G1 gate while their successors' bodies are placeholders | no |
| TA-5 | **T001 makes the new `InstallError` variants and their `Display` texts real** (plan §3.2 `plan.md:234-246`, PD-15), so T009's `install::tests::new_error_texts` has no red-first; its notes say so and the test is written against the literals of §3.2, never copied from `install.rs` | the variants are matched by `status.rs`, `test_support.rs` and the UI fixtures T001 edits; a text changed later would ripple through other tasks' files | no |
| TA-6 | **`os::known_folder` takes a helper enum, not a `GUID`:** `pub enum KnownFolder { ProgramFilesCommon, ProgramData, UserProfiles, Windows }` (cfg-free, in `os.rs`), `#[cfg(windows)] pub fn known_folder(id: KnownFolder) -> Option<PathBuf>` maps it to `FOLDERID_*` inside `os.rs` | `roots.rs` then names no `windows_sys` item, so plan §4.8.1's `windows_sys` home list (`plan.md:854-856`) narrows to `os.rs` and `os_elevation.rs`; FR-051 ("the functions that build the roots take no argument") is unchanged | spec: FR-051 names `KnownFolder` |
| TA-7 | **G1 widens the allow-list homes G2 code needs** (the Phase 9 TA-8 rule "guard rows move with the code that needs them"): `HOST_FFI` and `CONFINED_TOKENS` `windows_sys` homes, `HOST_LITERALS` homes (the type becomes `(&str, &[&str])`), the test-source host-literal exemption for `crates/helper/src/ops_tests.rs`, the `sha2` token home, `CONFINED_TOML_KEYS`, `READER_PINS` (two homes, D-2), `PLATFORM_LITERALS`, and the `Command::new` exemption for `windows_parameters_parse_back_windows` (D-1). T012 writes the **new** Phase 10 rules and their plants (plan §4.8.1's confinements and bans) and `fixture_names_confined` | without the widened homes the existing real-tree rows go red in every G2 loop the moment T008, T006, T011 or T013 saves its first line; new rules pass on a tree whose code obeys them | no |
| TA-8 | **Placeholder files T001 creates so the manifests never name a missing target:** `crates/core/tests/bin/argv_echo.rs` (real, ten lines), `src-tauri/examples/manual_elevation.rs` (placeholder `fn main` with `todo!("T013")`) | `crates/core/Cargo.toml` gains a `[[bin]]` and `src-tauri/Cargo.toml` an `[[example]]` naming these paths (plan §2 `plan.md:182`, `:193`) | no |
| TA-9 | Rule 10's target slots: shared target for T009-T013; `F:/tmp/km10-target/helper` for T003-T008; `F:/tmp/km10-target/fixtures` for T016 | eleven G2 tasks build Rust; `df -h /f` shows 5.0 GB free and the shared target holds 14 GB; one slot is one cold build of core's dependency graph (about 1.5 GB, Phase 7 TA-9's measure) and keeps T009, the long pole, on the warm target; 16 + 12 + 4 jobs fill the 32-core box | no |
| TA-10 | **The helper binary target is `test = false`** (`[[bin]] name = "krate-manager-helper" path = "src/main.rs" test = false`) | `main.rs` declares no module (plan §4.1.3 `plan.md:323`), so its harness would run zero tests, and with `windows_subsystem = "windows"` it would be a GUI-subsystem test harness; `src-tauri`'s bin sets the same (`src-tauri/Cargo.toml:16-19`) | no |
| TA-11 | L-WSL runs the full `cargo test -p krate-manager-core -p krate-manager-helper --locked` and the helper's feature clippy, and counts 13 Linux-run names: Phase 9's nine (`specs/krate-manager-phase9/tasks.md:894-902`) plus `validate::tests::symlinked_parent_root_unix`, `os::tests::open_source_refuses_links_and_fifos_unix`, `os::tests::run_dir_mode_unix`, `os::tests::wait_timeout_leaves_child_unix` | the guard binaries lex sources and walk the locks, which can differ on Linux; the helper's `cfg(unix)` seams (PD-7) reach no other local gate | no |
| TA-12 | **FR088-safe test text.** `src/status.test.ts` builds the AU-cache phrase as `['~', 'Library', 'Caches', 'AudioUnitCache'].join('/')`, never the literal; `install_tests.rs` compares the registrar argv with `platform::KILLALL_ARGV` only (the literal lives once in `platform/mod.rs`, pinned by `PLATFORM_LITERALS`); `install_tests.rs`, `status_tests.rs` and `test_support.rs` build every Windows-shaped path with `Path::join` from temp components, never `AppData` / `ProgramData` / `Program Files` text | `status.test.ts` and `install_tests.rs` are `FR088_FILES`, which may not contain `/Library` or `/usr/` (`injection.rs:253-276`); plan §4.8.1 (`plan.md:851`) places the `/usr/bin/killall` literal in `install_tests.rs` and SC-025 asserts the `~/Library/Caches/AudioUnitCache` phrase (D-3) | spec: FR-112's `killall` homes are `platform/mod.rs` only |
| TA-13 | **Non-required self-tests:** `seams::tests::fake_world_cases` (T007, `crates/helper/src/seams_tests.rs`) and `install::loopback::loopback_smoke` (T010). Totals stay TA-1's | each fake has no SC row of its own; a self-test makes its items used (no `dead_code` in its own loop) and gives its owner a red-first. Phase 9 TA-6 precedent | no |
| TA-14 | **The Loopback API is fixed by T010's first save** (exact signatures in T010 step 1); T009 writes its loopback rows only after that save exists (`grep -c "pub(super) fn new" crates/core/src/install_loopback_tests.rs` ≥ 1) | plan §4.4 (`plan.md:711-720`) gives the loopback's semantics, not its methods; it is test-only code inside `install`'s `cfg(test)`, so T001 cannot freeze it without unused items warning | no |
| TA-15 | **The roadmap status line (FR-123 (c) + (h)) is written by T019**: "**Status: stopped at the M-1 fence (Phase 10 built; `inno-locked-file` must run after the user's push)**" after `specs/Krate-manager-roadmap.md:711`; no later step of this build may rewrite it as complete | SC-032 needs it before the push (`spec.md:1182-1185`); FR-063 forbids a close with the provisional rule | no |
| TA-16 | **L-BUNDLE checks locally only what this box can read:** the staged sidecar `src-tauri/binaries/krate-manager-helper-x86_64-pc-windows-msvc.exe` exists, its `--ci-roots` count is 0 and so is the release helper's, the NSIS setup exists under `target/release/bundle/nsis/`, and `build warnings` reads 0. The `7z l` / `7z e` half of `helper bundled` is CI-only | `command -v 7z` printed nothing and `/c/Program Files/7-Zip/7z.exe` is absent this session (D-6) | no |
| TA-17 | **The fixture registry key is the full subkey.** `seams::fake::FakeRegistry::set(subkey, …)` takes `format!("{}\\{}{}", ops::UNINSTALL_KEY, app_id, ops::INNO_KEY_SUFFIX)`; every test builds pkgutil argv from `ops::PKGUTIL` | only `ops_tests.rs` is exempt from the test-source host-literal rule (TA-7), so every other test source names the constant | no |

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | **sequential** (1) | T001: every manifest, lockfile, config and crate-root edit; the helper crate's whole surface; the new `install.rs` surface and its call sites; `platform/mod.rs`, `roots.rs`, `status.rs`, the 13 goldens, `view.ts`; the Phase 10 test doubles in `test_support.rs`; `src-tauri` wiring; the guard rows (TA-7); the required-test lists | Preconditions |
| G2 Spec, helper, core, launcher, UI, scripts, fixtures, evidence | **[P]** (16) | T002 spec + roadmap amendments · T003 helper codecs (`job`, `result`, `classify`) + `tests/core_jobs.rs` · T004 helper `roots` + `cli` + `main.rs` + `tests/usage.rs` · T005 helper `validate` · T006 helper `ops` · T007 helper `seams::fake` · T008 helper `os` · T009 core `install` (the long pole) · T010 core loopback · T011 `status::tests::actions_per_state` + `windows_quoting.rs` · T012 guards (`injection.rs` new rules, `roster.rs`) · T013 `os_elevation.rs` + `manual_elevation.rs` · T014 UI · T015 Node scripts + `fixture.iss` · T016 CI Rust fixtures · T017 evidence scripts | G1 |
| G3 Integration | **sequential** (2, in order) | T018 `manager-ci.yml` + `manager-ci-shape.test.js` (the per-OS CI additions, written, not pushed) · T019 L-REQ, INDEX, lints, scope, greps, roadmap status line, the M-1 fence report, PENDING-CI, hand-over | G2 |
| R1 Resume | **blocked** (1) | T020 M-1 record, measured `classify_inno` rule, the replay tests, FR-102a, roadmap (b) and the status rewrite | the user's push and an `inno-locked-file` run |

**File ownership in G2 (each file has exactly one G2 writer).** T002: `spec.md`, `specs/Krate-manager-roadmap.md`.
T003: `crates/helper/src/{job,result,classify}.rs`, `{job,result,classify}_tests.rs`, `crates/helper/tests/core_jobs.rs`.
T004: `crates/helper/src/{roots,cli,main}.rs`, `{roots,cli}_tests.rs`, `crates/helper/tests/usage.rs` (new),
`crates/helper/tests/stub.rs` (deleted). T005: `crates/helper/src/validate.rs`, `validate_tests.rs`. T006:
`crates/helper/src/ops.rs`, `ops_tests.rs`. T007: `crates/helper/src/seams.rs`, `seams_tests.rs`. T008:
`crates/helper/src/os.rs`, `os_tests.rs`. T009: `crates/core/src/install.rs`, `install_tests.rs`. T010:
`crates/core/src/install_loopback_tests.rs`. T011: `crates/core/src/status_tests.rs`, `crates/core/tests/windows_quoting.rs`
(new). T012: `crates/core/tests/injection.rs`, `crates/core/tests/roster.rs`. T013: `src-tauri/src/os_elevation.rs`,
`src-tauri/examples/manual_elevation.rs`. T014: `src/status.ts`, `src/status.test.ts`. T015: `scripts/bundle.js`,
`bundle.test.js`, `write-test-job.js`, `write-test-job.test.js`, `inno-fixture.test.js`, `tests/fixtures/inno/fixture.iss`
(all new). T016: `tests/fixtures/{locked-dll,argv-recorder}/**`, `tests/fixtures/helper/.gitkeep` (all new). T017:
`specs/krate-manager-phase10/evidence/{scope-check,wsl-lane,bundle-lane,close-lane}.js` (new). `crates/helper/src/lib.rs`,
`digest.rs`, `error.rs` are finished in T001 and edited by nobody in G2.

**Why this shape.** T001 is the only task that edits shared files, so it is a group of its own; it writes every `pub mod`
/ `mod` line, every manifest line and the whole public surface up front, so the sixteen G2 tasks run side by side and
none adds a line to a shared file (Phase 4's seven core modules ran one after another because each added its own `pub
mod` line; here none does). The helper crate, which the plan describes as one unit, is cut along its module map (PD-1)
into six writers: codecs, roots / cli / binary, validation, operations, fakes, OS seams; each owns its module and its
`_tests.rs`. The long pole is `install` (T009), which cannot be split across writers: every core SC test is named
`install::tests::*` in one module over one file (plan §4.9), and the guards pin single-file facts of `install.rs`
(FR-112: `osascript`, `administrator privileges`, `runas` constants only there; Phase 9's `Command` / `xattr` bans).
Everything that can leave T009 has left it: the loopback (T010), the status table and the quoting round trip (T011), the
guards (T012), the launcher (T013), the UI (T014), the test doubles (T001). The in-group dependencies run one way per
pair and settle before the gate (the expected-red list above). G3 is two dependent tasks: T018's workflow and shape test
are shared files, and T019's checks read the whole tree after them. No group is a chain of independent tasks.

**SC owners:** SC-001, SC-008, SC-027 (`validate::tests::*`) T005 · SC-002 to SC-006, SC-027 (`ops::tests::*`) T006 ·
SC-005, SC-027 (`os::tests::*`) T008 · SC-006 `log_tail_bounds`, SC-007 `no_field_adds_a_root`, SC-011, SC-016
`provisional_rule` T003 · SC-007 roots rows, SC-019 local, SC-023 T004 · SC-003 core twin, SC-009, SC-010, SC-012, SC-014,
SC-015 (core rows), SC-022, SC-024, SC-028, SC-029, SC-030, `new_error_texts` T009 · SC-013, SC-015
`windows_parameters_parse_back_windows` T011 · SC-021 T001 (homes) + T012 · SC-020 node rows T015 · SC-025 T014 · SC-017 to
SC-020 CI rows, SC-031 T018 · SC-026 every task + T019 + L-CLOSE · SC-032 T019 · SC-016 post-M-1 R1 · FR-123 (a), (d)-(g)
T002, (c) + (h) T019, (b) R1 · FR-090 / FR-091 execution T013 · FR-092 / FR-104 local half L-BUNDLE.

---

## G1 — Foundation (sequential: one task, every shared file)

### T001 — Precondition, manifests and locks, configs, the helper crate surface, the `install.rs` surface and its call sites, `platform` / `roots` / `status` / goldens / `view.ts`, the Phase 10 test doubles, `src-tauri` wiring, the guard rows, the required-test lists (spec FR-050 to FR-053, FR-070, FR-071, FR-079, FR-083, FR-084, FR-092, FR-093, FR-105, FR-108; plan §2, §3, §4.1.1-§4.1.12 surfaces, §4.2.1-§4.2.4 surfaces, §4.3, §4.4, §4.5 wiring, §4.7 `view.ts`, §4.8.2, §4.9; PD-1, PD-4, PD-5, PD-8, PD-9, PD-10, PD-12, PD-14, PD-15, PD-23; SI-1, SI-3 to SI-6, SI-8, SI-21; TA-1 to TA-10, TA-12, TA-17)

Target: the shared `apps/krate-manager/target`, `CARGO_BUILD_JOBS=16`. Logs under `F:/tmp/gate/km10-T001/`.

**Files (edit):** root `Cargo.toml` (CRLF), `Cargo.lock`, `crates/helper/Cargo.toml` (LF), `crates/core/Cargo.toml`
(CRLF), `src-tauri/Cargo.toml` (CRLF), `tests/fixtures/clippy-guard/Cargo.lock`, `package.json`, `.gitignore`,
`tsconfig.node.json`, `crates/helper/src/lib.rs`, `crates/core/src/install.rs`, `install_tests.rs` (call sites only),
`crates/core/src/platform/mod.rs`, `crates/core/src/roots.rs`, `crates/core/src/status.rs`, `status_tests.rs` (call site
only), `crates/core/src/test_support.rs` (CRLF), `test_support_tests.rs` (call sites only), `src-tauri/src/lib.rs`
(CRLF), `src-tauri/src/commands.rs`, `src/view.ts`, `tests/fixtures/status/*.json` (13), `crates/core/tests/injection.rs`
(CRLF), `crates/core/tests/config_guard.rs`, `tests/required-tests.json`,
`tools/feed-contract/tests/manager-ci-shape.test.js` (`EXPECTED_REQUIRED` only). **Create (LF):**
`crates/helper/src/{cli,error,job,result,roots,validate,digest,ops,classify,seams,os}.rs` and
`{cli,job,result,roots,validate,ops,classify,seams,os}_tests.rs`, `crates/core/src/install_loopback_tests.rs`,
`crates/core/tests/bin/argv_echo.rs`, `src-tauri/src/os_elevation.rs`, `src-tauri/examples/manual_elevation.rs`,
`src-tauri/tauri.helper.conf.json`. `crates/helper/src/main.rs` and `tests/stub.rs` stay as they are (T004 replaces them).

**Steps, each with its failing test first:**

1. **Precondition check** (above). Record `git rev-parse --short HEAD` (expected `4a8b3e2a`) and `git status
   --porcelain`; run L-RS once as the baseline (`cargo-test-before.txt`; quote every `test result:` line).
2. **Manifests (plan §2 `plan.md:178-183`, PD-8, FR-093, FR-105).**
   - Root `Cargo.toml` (`:1-4`, `:13-51`): `exclude = ["tests/fixtures/clippy-guard", "tests/fixtures/argv-recorder",
     "tests/fixtures/locked-dll"]`; `krate-manager-helper = { path = "crates/helper" }` after `krate-manager-core`
     (`:19`); the `windows-sys` line (`:49`) gets exactly the five features of today followed by `"Win32_UI_Shell",
     "Win32_System_Com", "Win32_System_Threading", "Win32_Security", "Win32_Security_Authorization",
     "Win32_Storage_FileSystem"`, and its comment (`:48`) gains "; Phase 10 FR-093: eleven features, homes src-tauri and
     crates/helper only, never crates/core".
   - `crates/helper/Cargo.toml` (`:1-14`): `description = "Krate Manager elevated helper: the fixed operation set (Phase
     10)"`; `[lib] path = "src/lib.rs"`; `[[bin]] name = "krate-manager-helper" path = "src/main.rs" test = false`
     (TA-10); `[features] ci-temp-roots = []` with the comment `# FR-052: never default; only the two elevated CI jobs and
     the check job's two FR-103a steps enable it`, `test-support = []` with `# PD-8: the fakes for core's loopback`;
     `[dependencies] serde`, `serde_json`, `sha2` (each `{ workspace = true }`); `[target.'cfg(windows)'.dependencies]
     windows-sys = { workspace = true }`; `[dev-dependencies]` the existing core line plus `tempfile = { workspace = true
     }`; `[lints] workspace = true`.
   - `crates/core/Cargo.toml`: `[[bin]] name = "argv-echo" path = "tests/bin/argv_echo.rs" test = false` with `# Phase
     10 SC-015: windows_quoting.rs's echo target; never shipped`; `[dev-dependencies]` + `krate-manager-helper = {
     workspace = true, features = ["test-support"] }` with `# Phase 10 PD-8: the loopback (install_loopback_tests.rs)`.
   - `src-tauri/Cargo.toml` (`:44-45`): `[features] dev-feed = []` then `manual-elevation = []` with `# FR-105: never
     default; only the check job's clippy manual-elevation example step names it`; `[[example]] name =
     "manual_elevation" path = "examples/manual_elevation.rs" required-features = ["manual-elevation"]`.
   - Re-lock: from `apps/krate-manager/`, `cargo metadata --format-version 1 > /dev/null` (offline is enough: every
     package is already in `Cargo.lock`; add `--offline` if it tries the network), then `cargo metadata --manifest-path
     tests/fixtures/clippy-guard/Cargo.toml --format-version 1 > /dev/null`. Record `git diff --stat Cargo.lock
     tests/fixtures/clippy-guard/Cargo.lock` (expected: the workspace lock's `krate-manager-helper` and
     `krate-manager-core` dependency lists change; no package added or removed).
3. **Configs.** `package.json` `scripts` gains `"bundle": "node scripts/bundle.js"` after `"tauri"` (`package.json:5-12`);
   `.gitignore` gains `/src-tauri/binaries/`; `tsconfig.node.json` gains `"allowJs": true, "checkJs": true` and `include`
   becomes `["vite.config.ts", "scripts/*.js"]` (plan §4.6 `plan.md:784-791`); `src-tauri/tauri.helper.conf.json` is
   exactly `{ "bundle": { "externalBin": ["binaries/krate-manager-helper"] } }` + LF (FR-092).
4. **The helper crate root and finished modules** (plan §4.1.1, §4.1.2, §4.1.8, §3.1). `lib.rs`: one `//!` line, then
   `pub mod classify; pub mod cli; pub mod digest; pub mod error; pub mod job; pub mod ops; pub mod os; pub mod result;
   pub mod roots; pub mod seams; pub mod validate;`, `HelperOs` + `current()` and `Exit` + `From<Exit> for ExitCode`
   (plan `:256-266`, real), and `pub fn run` **real**, exactly the seven steps of §4.1.2 (`plan.md:270-290`), calling the
   placeholders of step 5. `error.rs` real: `HelperError { pub code: &'static str }`, one `pub const` per code and `pub
   const CODES: [&str; 25]` listing the 25 codes of §3.1 (`plan.md:222-232`). `digest.rs` real (§4.1.8 `plan.md:420-421`:
   `Sha256Stream`, `sha256`, `hex`, `parse_hex64`; the only `sha2` user).
5. **The helper placeholders** (signatures verbatim from plan §4.1.3-§4.1.7, §4.1.10-§4.1.12, with TA-2's `pub` and
   TA-6's `KnownFolder`; every module ends with its `#[cfg(test)] #[path = "<stem>_tests.rs"] mod tests;` and each
   `_tests.rs` holds one `//!` line naming its owner). Real in T001: every type, constant and trait; `Op::kind`,
   `Op::target`, `job::KEYS`; `cli::Args`, `USAGE`, `RunFn`; `roots::{StateDirRule, WindowsRoots, MacosRoots, OsRoots,
   AllowedRoots, HELPER_FOLDER}`, `pub const STATE_DIR_NAME: &str = "com.krateaudio.krate-manager"`, `pub struct
   CanonicalRoots { pub roots: AllowedRoots }` (every path canonical); `validate::{Kind, LstatFacts, RUN_DIR_SDDL,
   JobPlace { pub stem, pub result_path, pub staging_root }, Uninstaller { pub exe, pub install_dir }, PkgPlan { pub files,
   pub dirs }}`; `ops`'s ten constants (`plan.md:483-493`) and `pub struct OpCtx<'a> { pub os: HelperOs, pub roots: &'a
   CanonicalRoots, pub place: &'a JobPlace }`; `classify::InnoVerdict`; `result::{ResultFile, OpEntry, OpOutcome}`;
   `seams.rs`'s `WriteSync`, `HelperIo` (the fifteen methods of `plan.md:427-442`), `ChildId`, `ProcessRunner`, `Registry`,
   `Seams`; `os::{OsHelperIo { _private: () }, OsRunner { _private: () }, OsRegistry}` with `OsHelperIo::new()` and
   `OsRunner::new()` real (so T004's binary test never meets a placeholder before argument parsing), `os::KnownFolder`.
   Placeholders: `job::parse`, `result::{write, log_lines, log_tail}`, `classify::classify_inno` (`todo!("T003")`);
   `cli::{parse_args, main_with}`, `roots::{AllowedRoots::production, canonical}`, `#[cfg(feature = "ci-temp-roots")]
   roots::from_ci_file` (`todo!("T004")`); every `validate` function (`todo!("T005")`); `ops::{copy_hashed, run_batch}`
   (`todo!("T006")`); every `seams::fake` body (`todo!("T007")`); the three `impl … for Os*` blocks, `os::
   open_regular_no_follow`, `#[cfg(windows)] os::known_folder` (`todo!("T008")`). **The `seams::fake` surface (TA-3),
   inline in `seams.rs` as `#[cfg(any(test, feature = "test-support"))] pub mod fake { … }`:**
   - `pub enum Event { RemovePath(PathBuf), RemoveKey(String) }` (fires when the clock reaches its time).
   - `pub enum Fault { CreateRunDir(io::ErrorKind), CreateNew { name: OsString, kind: io::ErrorKind }, WriteAfter { name:
     OsString, bytes: u64, kind: io::ErrorKind }, RemoveFile(PathBuf) }` (`name` is the final path component).
   - `pub enum Hook { RewriteAfterCopy { src: PathBuf, bytes: Vec<u8> }, RewriteDuringCopy { src: PathBuf, bytes: Vec<u8>
     }, LinkBeforeOpen { src: PathBuf, target: PathBuf }, #[cfg(unix)] FifoBeforeOpen { src: PathBuf } }`.
   - `#[derive(Clone, Debug, PartialEq, Eq)] pub enum Call { Spawn(Vec<OsString>), Wait { child: ChildId, timeout:
     Duration }, Output(Vec<OsString>), Sleep(Duration), CreateNew(PathBuf), CreateHelperFolder(PathBuf),
     CreateRunDir(PathBuf), RemoveFile(PathBuf), RemoveDir(PathBuf), UninstallValues(String), KeyExists(String) }`.
   - `pub struct World { … }` (`Default`), with `pub fn new() -> World`, `now(&self) -> Duration`, `advance(&self, d:
     Duration)`, `at(&self, when: Duration, event: Event)`, `owner(&self, path: &Path, uid: u32, mode: u32)`,
     `root_owned(&self, dir: &Path)`, `fault(&self, fault: Fault)`, `clear_faults(&self)`, `hook(&self, hook: Hook)`,
     `calls(&self) -> Vec<Call>`, `spawns(&self) -> Vec<Vec<OsString>>`, `removals(&self) -> Vec<PathBuf>`; the registry
     map lives in the world so `Event::RemoveKey` reaches it.
   - `pub struct TempIo<'w>` with `pub fn new(world: &'w World, base: &Path) -> TempIo<'w>` and `impl HelperIo`.
   - `pub enum Match { Prefix(Vec<OsString>), Nth(usize), Any }` (`Nth` counts `spawn` and `output` calls together,
     0-based; the first matching rule in insertion order wins; no rule: exit 0, no time, no effect).
   - `pub struct Script<'w> { pub exit: i32, pub takes: Duration, pub never_exits: bool, pub spawn_error:
     Option<io::ErrorKind>, pub stdout: Vec<u8>, pub effect: Option<Box<dyn Fn(&[OsString]) + Send + Sync + 'w>> }` with
     `Default` (0, `ZERO`, `false`, `None`, empty, `None`).
   - `pub struct ScriptedRunner<'w>` with `pub fn new(world: &'w World) -> Self`, `pub fn on(&self, m: Match, s:
     Script<'w>)`, `impl ProcessRunner`.
   - `pub struct FakeRegistry<'w>` with `pub fn new(world: &'w World) -> Self`, `pub fn set(&self, subkey: &str,
     uninstall_string: Option<&str>, install_location: Option<&str>)` (TA-17), `impl Registry`.
   Pass: `cargo clippy -p krate-manager-helper --all-targets --locked -- -D warnings` and L-HFEAT's clippy 0 warnings;
   `cargo test -p krate-manager-helper --locked` runs `helper_stub_exits_2_with_one_line` green (the stub `main.rs` is
   untouched).
6. **Core: `platform/mod.rs`, `roots.rs`, `install.rs` surface** (plan §3.2, §4.2.1, §4.2.2 surface, §4.2.4 surface,
   PD-9, PD-10, PD-12, PD-14, PD-16 not yet). `platform/mod.rs`: `pub const KILLALL_ARGV: [&str; 3] = ["/usr/bin/killall",
   "-9", "AudioComponentRegistrar"];` (cfg-free, one doc line citing FR-077). `roots.rs`: `pub fn vst3_global(&self) ->
   &Path`, mirroring `vst3_user` (`roots.rs:210`). `install.rs`, real: the ten new `InstallError` variants with their
   `#[error]` texts and `code()` arms (plan §3.2, PD-15, TA-5), `Step::{JobWritten, JobRemoved}` + `as_str`,
   `Notice::RescanDawMacos` (`"rescan_daw_macos"`), `InstallProgress::Elevating` (`{"kind":"elevating"}`),
   `ElevatedCommand`, `LaunchOutcome`, `trait Elevation: Sync` (three methods, PD-9), `pub struct ElevationGuard {
   _private: () }` + `pub const fn new()`, `OSASCRIPT_LINES` (verbatim, `plan.md:581-585`), `pub const KILLALL_WAIT:
   Duration = Duration::from_secs(5)`, `RemoveBasis`, `ActionGate::Open { manifest, remove }`, `pub struct WindowsDriver;`
   `pub struct MacosDriver;` with `impl InstallDriver` (`os()` real, `run` `todo!("T009")`; no `static` yet, TA-4),
   `InstallEnv` + `elevation`, `host_io`, `host` and `CommandEnv` + `elevation`, `host_io` (PD-10), `native_env(ctx, io,
   extract, download, elevation, host_io)`, the `pub mod job { … }` surface (`plan.md:598-612`: constants, `JobOp`,
   `WrittenJob`, `OpReport`, `ResultError` real; `write`, `ceiling`, `read_result`, `helper_code` `todo!("T009")`),
   `action_gate(os, ctx, io, entry, detection: &Detection)` with Phase 9's body (`install.rs:2700-2740`) and `remove`
   = `Manifest` iff a manifest else `NotManaged` (T009 adds the Windows / macOS arms), `allowed_actions` offering `Remove`
   iff `remove` is `Manifest | Inno | Pkg` (FR-081, equivalent to Phase 9's rule under this gate), FR-082's reorder in
   `install_command` (listing and detection before the gate, `install.rs:2925-2938`) and in `remove_for_command(env, name,
   now_unix, host, progress)` (one `Listing::scan` of the entry's `vst3_bundle`, `detect_plugin`, then the gate;
   `NotManaged` → `E_INSTALL_NOT_MANAGED`, `OutsideRoot` → `E_INSTALL_REMOVE_ELSEWHERE`). Placeholders (`todo!("T009")`):
   `helper_beside(os, exe, io)`, `windows_parameters`, `osascript_outcome`. The declaration `#[cfg(test)] #[path =
   "install_loopback_tests.rs"] mod loopback;` before the `mod tests;` line (`install.rs:3177-3179`), and the file with one
   `//!` line naming T010. **Call sites** (mechanical, no behaviour change): `install.rs:364`, `:446`, `:3008`, `:3109`;
   `install_tests.rs:600`, `:632`, `:780`, `:1492`, `:1515`, `:1549`, `:3440`, `:3486`, `:3536`, `:3741`, `:3816`, `:3855`,
   `:3858`, `:3909`, `:3976`, `:4115`, `:4194` (Linux action gates get `&Detection::NotChecked`; new `InstallEnv` fields
   from the rig); `status_tests.rs:1868-1870`; `test_support_tests.rs:1392`, `:1420`. Pass: `cargo clippy -p
   krate-manager-core --all-targets --locked -- -D warnings` 0 warnings; every Phase 9 `install::tests::*` still green.
7. **`argv_echo.rs`** (TA-8): `fn main()` printing `serde_json::to_string(&args)` of `std::env::args_os().skip(1)` mapped
   with `to_string_lossy`, one line, exit 0. No other item.
8. **`status.rs`, the goldens, `view.ts`** (FR-083, plan §4.3, §4.7). Test first: add `pub remove_hint:
   Option<RemoveHint>` as the **last** field of `PluginView` (`status.rs:58-74`, after `actions` at `:73`), with `#[derive(Clone, Copy,
   Debug, PartialEq, Eq, Serialize)] #[serde(rename_all = "snake_case")] pub enum RemoveHint { WindowsSettings }`,
   `Some(WindowsSettings)` iff the entry's gate is `Open { remove: OutsideRoot, .. }`; pass the detection the build
   already holds to `action_gate` (`status.rs:231-232`); run `cargo test -p krate-manager-core --locked --lib
   status::tests::golden_views` → red, `differs` for all 13 files (quote the line). Then add `"remove_hint": null` as each
   plugin object's last key in the 13 `tests/fixtures/status/*.json` (the `to_string_pretty` + LF form the test compares)
   → green. `view.ts`: `PluginView.remove_hint: "windows_settings" | null` in the parser's key list (`view.ts:125-131`
   refuses a missing and an extra key), `ActionNotice` + `"rescan_daw_macos"`, `InstallProgress` + `{ kind: "elevating"
   }`. Pass: `golden_views` and every `status::tests::*` green; L-TS green with 46 titles.
9. **`test_support.rs` doubles** (plan §4.4 `plan.md:697-709`, SI-21, real; Rule 4b, TA-12): `fresh_elevated_roots() ->
   (TempDir, Roots)` (the `temp_roots` layout with the state dir `<base>/<platform::APP_IDENTIFIER>` and staging under
   it); `FakeElevation` with the fields of `plan.md:699-700`, constructors `refusing()`, `returning(outcome)`,
   `missing()`, `with_helper(self, path: PathBuf) -> Self`, `push(&self, s: Scripted)`, accessors `launches()`, `jobs()`,
   and `impl Elevation` (every `launch` reads the file named by `args[1]` with `std::fs::read` and pushes its bytes before
   the scripted outcome; a scripted `result` writes `<job stem>.result.json` beside the job); `Scripted { outcome:
   LaunchOutcome, result: Option<Box<dyn Fn(&Path) -> Option<Vec<u8>> + Send>> }`; `LiveHostFacts { pub os_version:
   Option<OsVersion>, pub inno: Mutex<BTreeMap<String, RecordRead<InnoValues>>>, pub pkg: Mutex<BTreeMap<String,
   RecordRead<PkgOutputs>>> }` keyed by the `PlainName` string (`feed.rs:141-146`), `LiveHostFacts::new(os_version)`,
   `impl HostFacts` (absent key → the "no record" value `FakeHostFacts` returns, `test_support.rs:273-310`);
   `InstallRig` gains `pub elevation: FakeElevation` (`missing()`) and `pub host_io: FakeHostIo`, `command_env(…)` keeps
   its signature (`test_support.rs:3056`) and borrows them, `command_env_with(os, arch, io, extract_io, drivers,
   elevation, host_io)`. Pass: core clippy 0 warnings; `test_support::tests::*` green.
10. **`src-tauri` wiring** (plan §4.5 `plan.md:743-749`, PD-23). `os_elevation.rs`: `pub struct NativeElevation { helper:
    Option<PathBuf>, guard: ElevationGuard }`, `pub fn new(helper: Option<PathBuf>) -> NativeElevation` (the only
    literal construction; T012's rule names `NativeElevation::new(` outside `lib.rs`), `impl Elevation` with `helper()` and
    `guard()` real and `launch` `todo!("T013")`. `lib.rs`: `pub mod os_elevation;`, `pub fn resolve_elevation(exe:
    Option<&Path>) -> os_elevation::NativeElevation` (`NativeElevation::new(install::helper_beside(<native Os>, exe,
    &FsInstallIo))`; `run()` `.manage(Arc::new(resolve_elevation(std::env::current_exe().ok().as_deref())))`.
    `commands.rs:104-199`: `install_plugin` / `remove_plugin` gain `elevation: tauri::State<'_, Arc<NativeElevation>>`,
    clone the `Arc` into the blocking closure, pass `&*elevation` and `&OsHostIo` to `native_env` (`:136`, `:185`) and
    `&PlatformFacts { io: &OsHostIo }` to `remove_for_command` (`:184`). `examples/manual_elevation.rs`: `fn main() {
    todo!("T013") }` (TA-8). No new command, no capability change. Pass: L-APP, L-DEVFEED, L-MANUAL 0 warnings.
11. **Guards T001's edits trip, and the G2 homes (TA-7).** Test first: after steps 2-10 run `cargo test -p
    krate-manager-core --locked --test config_guard --test injection --test roster` and quote every red line. Then:
    `config_guard.rs` per plan §4.8.2 (`plan.md:862-866`): the platform-file rule allows `tauri.helper.conf.json` with
    exactly its one key; src-tauri `[features]` `["dev-feed = []", "manual-elevation = []"]` (`:870-871`) and the
    `[[example]]`; `scripts` + `bundle` (`:1303`); helper `[features]` exactly `["ci-temp-roots = []", "test-support =
    []"]`, no `default` (`:1248-1256`); core's helper dev-dependency and `[[bin]]`; `pub mod os_elevation` in
    `src-tauri/src/lib.rs`; each widened row keeps or gains its plant. `injection.rs`: `READER_PINS` (`:562-582`) —
    `ReaderPin.home` becomes `homes: &'static [(&'static str, &'static str)]` (D-2) and `windows-sys` gets the eleven
    features and the homes `("src-tauri/Cargo.toml", WINDOWS_DEPS)`, `("crates/helper/Cargo.toml", WINDOWS_DEPS)`, a
    planted line in `crates/core/Cargo.toml` reported; `CONFINED_TOML_KEYS` (`:309-327`) `windows-sys`, `sha2` +
    `crates/helper/Cargo.toml`; the `sha2` token rule (`:2966-2974`) + `crates/helper/src/digest.rs`; `PLATFORM_LITERALS`
    (`:51`, 37 → 40) + the three `KILLALL_ARGV` literals; `HOST_FFI` (`:208-213`) homes + `crates/helper/src/os.rs`;
    `CONFINED_TOKENS` `windows_sys` (`:302`) + `src-tauri/src/os_elevation.rs`, `crates/helper/src/os.rs`; `HOST_LITERALS`
    (`:217-227`) becomes `(&str, &[&str])`, pkgutil and the Uninstall key gaining `crates/helper/src/ops.rs` and
    `crates/helper/src/ops_tests.rs`; `test_host_io_findings` (`:2451-2480`) exempts `crates/helper/src/ops_tests.rs`; the
    `Command::new` rule (`:1988-2010`) accepts `Command::new(env!("CARGO_BIN_EXE_argv-echo"))` only inside
    `windows_parameters_parse_back_windows` in `crates/core/tests/windows_quoting.rs` (D-1). Each change with a plant in
    `injection_scanner_reports_planted_violations` (`:3687`) or its row's own planted test. Any other row T001's edits
    trip is fixed the same way and listed in the notes with the line it tripped. Pass: the three guard binaries green.
12. **Required-test lists (TA-1).** `tests/required-tests.json` and `EXPECTED_REQUIRED` (`manager-ci-shape.test.js:337`,
    deep-compared at `:917`), same order in both. Remove `helper_stub_exits_2_with_one_line` and
    `install::tests::no_driver_off_linux` (`all`), `install::tests::native_wiring_offers_nothing_windows` (`windows`),
    `…_macos` (`macos`), vitest `renders no install buttons on windows or macos`. Add — `cargo.all` (54):
    `validate::tests::{rejects_crafted_jobs, performs_valid_jobs, run_dir_ancestors, run_dir_sddl}`;
    `ops::tests::{exact_argv, batch_of_three_middle_locked, batch_timeout_stops_the_rest, uninstall_only_job_has_no_run_dir,
    run_dir_failure_fails_installers, copy_error_is_named, spawn_error_is_named, classify_sees_whole_bounded_log,
    uninstall_polls_until_gone, uninstall_poll_times_out, uninstaller_never_exits, runs_the_copy_never_the_original,
    pkg_paths_classified, pkg_failure_paths, run_dir_removed_after_job, run_dir_refusals_spawn_nothing,
    temp_io_refuses_outside_base}`; `result::tests::log_tail_bounds`; `roots::tests::binary_roots_ignore_job_and_settings`;
    `job::tests::no_field_adds_a_root`; `classify::tests::provisional_rule`; `cli::tests::{ci_roots_flag_is_usage_error,
    malformed_hash_is_usage_error}`; `os::tests::read_bounded_stops_at_bound`; `core_written_jobs_parse`;
    `helper_usage_exits_2`; `install::tests::{batch_locked_middle_maps_each, refused_prompt_changes_nothing,
    one_launch_per_batch, drivers_per_os, gate_remove_basis, remove_elsewhere_refused, record_outside_sandbox_panics,
    windows_install_update_remove, macos_install_update_remove, launch_outcomes_map, pre_job_refusals,
    elevating_event_order, osascript_outcome_parses, windows_parameters_quote, job_files_written_and_removed,
    recover_removes_stale_jobs, launch_program_is_the_resolved_helper, helper_file_never_opened, timed_out_holds_guard,
    not_applied_detected, new_error_texts, job_hash_is_the_verified_digest, helper_resolved_beside_exe}`;
    `fixture_names_confined`. `cargo.windows` (6): `validate::tests::verbatim_paths_windows`,
    `roots::tests::production_roots_windows`, `validate::tests::symlinked_parent_root_windows`,
    `windows_parameters_parse_back_windows`, `install::tests::native_wiring_windows`,
    `os::tests::open_source_refuses_link_windows`. `cargo.macos` (6): `roots::tests::production_roots_macos`,
    `install::tests::native_wiring_macos`, `validate::tests::symlinked_parent_root_unix`,
    `os::tests::open_source_refuses_links_and_fifos_unix`, `os::tests::run_dir_mode_unix`,
    `os::tests::wait_timeout_leaves_child_unix`. `cargo.linux` (4): the last four of the macOS list. `vitest` (12): the
    SC-025 titles (`spec.md:1136-1143`) plus `macos remove confirmation names the uninstaller` (SI-30). New `node` (3):
    `bundle sidecar name per host tuple`, `fixture inno script keeps the plugin directives`, `write test job prints
    verbatim windows paths`. Print the five counts with a throwaway Node script: `328 17 49 43 57 3`.
13. **Gate.** L-RS, L-HFEAT, L-APP, L-DEVFEED, L-MANUAL, L-GUARD, L-TS, L-NODE + L-TAP, L-VLINT; quote each pass line.

**Pass:** every lane of step 13 green; `grep -c 'todo!("T0' ` over the workspace lists only the owners named above.
**Lane:** app lane + pipeline lane.

---

## G2 — Spec, helper, core, launcher, UI, scripts, fixtures, evidence [P] (sixteen tasks, disjoint files, Rule 10 slots)

### T002 [P] — Precondition check, the spec amendments and the roadmap amendments (plan §0.2 SI-1 to SI-31; spec FR-123 (a), (d), (e), (f), (g); TA rows marked spec)

**Files:** `specs/krate-manager-phase10/spec.md` (LF), `specs/Krate-manager-roadmap.md` (CRLF). No code.

**Test first:** a throwaway Node script (Rule 13) that counts, in `spec.md`, the needles this task must change and must
introduce: `main::tests::ci_roots` 2 → 0 and `cli::tests::ci_roots` 0 → ≥ 2 (SI-1); `ubuntu-latest` 1 → 0 (SI-2); `before
\`required tests ran\`` 1 → 0 (SI-15); `ElevationGuard` 0 → ≥ 1 (SI-3); `E_HELPER_SPAWN` 0 → ≥ 1 and
`E_HELPER_RESULT_WRITE` 0 → ≥ 1 (SI-12); `locked_line.txt` 0 → ≥ 1 (SI-26); `macos remove confirmation names the
uninstaller` 0 → 1 (SI-30); `KnownFolder` 0 → ≥ 1 (TA-6); and in the roadmap the five FR-123 needles below 0 → 1. Run it
before editing (red), after (green).

**Steps:**
1. Precondition check. Then apply every SI row of plan §0.2 (`plan.md:70-102`) to the spec text it names, each edit
   tagged `(plan SI-n)`, and TA-2, TA-6, TA-12 tagged `(tasks TA-n)`. Where an SI row says "PD-n", write the PD's rule
   (plan §0.1) into the FR, not a pointer. Revision line: "DRAFT, revision 4 (plan SI-1 to SI-31 and tasks TA-2, TA-6,
   TA-12 encoded)". Do not touch M-1 (`spec.md:1253-1263`); R1 fills it.
2. Roadmap (FR-123, plan §4.10 `plan.md:929-935`): (a) lines 477-479: "and the elevation launcher in Phase 10" becomes
   "and the elevation launcher in Phase 10, which joins `CommandEnv` beside the driver resolver rather than `AppContext`
   (Phase 10 D8)"; (d) the Phase 11 universal bullet (lines 827-838) gains FR-123 (d)'s sentence verbatim
   (`spec.md:835-838`) and the outcome at line 864 gains "and `lipo -archs` on `Contents/MacOS/krate-manager-helper` lists
   `x86_64 arm64`"; (e) after "one path outside refuses the whole op" (line 735) the parenthesis of FR-123 (e)
   (`spec.md:839-841`); (f) Phase 10 Components gains one bullet: "Residual risk, accepted by the user on 2026-11-01: the
   per-user helper can be replaced by a process running as the user before the prompt; no digest check and no
   admin-folder copy; Q5's `currentUser` install stands (Phase 10 D19)"; (g) Phase 12's first-launch documentation
   bullet (lines 906-912) gains "A redirected `%LOCALAPPDATA%` is unsupported on Windows: every elevated batch is refused
   with `E_HELPER_JOB_LOCATION` (Phase 10 D17)". CRLF on every added line. No other roadmap line changes (the status line
   is T019's, (b) is R1's).

**Pass:** the needle script green; `git diff --stat` shows only these two files. **Lane:** pipeline lane (L-INDEX is
T019's; `spec.md` edits do not change `specs/INDEX.md`'s inputs, check with `node tools/gen-specs-index.js --check`).

### T003 [P] — Helper codecs: `job`, `result`, `classify`, and the core ↔ helper agreement test (spec FR-001 to FR-003, FR-036, D10; SC-006 `log_tail_bounds`, SC-007 `no_field_adds_a_root`, SC-011, SC-016 `provisional_rule`; plan §4.1.4, §4.1.5, §4.1.11, PD-2, PD-11, SI-11, SI-28)

Slot `F:/tmp/km10-target/helper`, 12 jobs. Logs `F:/tmp/gate/km10-T003/`.

**Files:** `crates/helper/src/job.rs`, `job_tests.rs`, `result.rs`, `result_tests.rs`, `classify.rs`,
`classify_tests.rs`, `crates/helper/tests/core_jobs.rs` (new).

**Failing tests first** (each red on `not yet implemented: T003` before the bodies):
- `job::tests::no_field_adds_a_root`: `job::KEYS` equals FR-001's four key sets (`spec.md:333-343`); a job whose op
  carries `"roots": []`, `"settings_roots": ["C:\\Windows"]`, `"dest": "/etc"`, `"args"`, `"log"` each fails `parse` with
  `E_HELPER_JOB_PARSE`; plus non-required parse rows: duplicate `op` key (PD-2, serde's `duplicate_field`), `job_version`
  2, 0 and 33 ops, explicit `"app_id": null` on an uninstaller op, an Inno op with `os = Macos`, every op with `os =
  Linux`, `sha256` with upper-case hex.
- `result::tests::log_tail_bounds` (plan §7.1 SC-006 row, `plan.md:1033`): 45 lines → the last 40; 45 lines interleaved
  with empty lines → the last 40 counting the empty ones; a 600-byte line cut to ≤ 512 bytes at a char boundary; invalid
  UTF-8 → U+FFFD; an empty input → `[]`; `log_lines` returns every line with a trailing `\r` stripped; `write` emits FR-003's
  keys in order and calls `sync_all` once (a recording `WriteSync`).
- `classify::tests::provisional_rule`: exit 0 → `Installed`; 1 to 8 and 99 → `Failed`; never `Locked` (D10).
- `crates/helper/tests/core_jobs.rs` `core_written_jobs_parse` (SC-011, `spec.md:1007-1011`; PD-11): over a temp dir
  shaped `<tmp>/com.krateaudio.krate-manager/{jobs,staging}`, `krate_manager_core::install::job::write` writes a job for
  each of the four `JobOp` kinds (the installer files real files under staging); `krate_manager_helper::run` with each
  op's own `os`, `StateDirRule::Exact(<state>)`, a `TempIo` and a `ScriptedRunner` / `FakeRegistry` parses it (no
  `E_HELPER_JOB_PARSE`) and its `job_sha256` equals `WrittenJob.sha256`; a `result::write` output fed to
  `install::job::read_result` yields the expected `OpReport`s; one flipped byte in the job (hash mismatch → exit 3
  `E_HELPER_JOB_HASH`) and in the result (`ResultError::NoResult`) fails; every `error::CODES` entry is known to
  `install::job::helper_code`; the five core timeouts equal `ops::{INNO_INSTALL_TIMEOUT, …}`; `roots::STATE_DIR_NAME ==
  krate_manager_core::platform::APP_IDENTIFIER`.

**Implement:** `parse` per §4.1.4 (`plan.md:343-345`) over PD-2's `RawJob` / `RawOp` / `Slot` (`deny_unknown_fields`);
`write`, `log_lines`, `log_tail` per §4.1.5; `classify_inno` provisional (§4.1.11). Expected red: `core_written_jobs_parse`
until T004-T009 land (the list above).

**Pass:** L-OWN (`cargo test -p krate-manager-helper --locked --lib job:: result:: classify::` and `--test core_jobs`
once the others are in), helper clippy 0 warnings; L-HFEAT clippy. **Lane:** app lane.

### T004 [P] — Helper `roots`, `cli`, the binary and its usage test (spec FR-005, FR-026, FR-050 to FR-052, D3, D15; SC-007 roots rows, SC-019 local, SC-023; plan §4.1.3, §4.1.6, PD-4, PD-5, PD-25, SI-1, SI-17; TA-6)

Slot `F:/tmp/km10-target/helper`. Logs `F:/tmp/gate/km10-T004/`.

**Files:** `crates/helper/src/roots.rs`, `roots_tests.rs`, `cli.rs`, `cli_tests.rs`, `main.rs`, `crates/helper/tests/usage.rs`
(new), `crates/helper/tests/stub.rs` (delete).

**Failing tests first:**
- `roots::tests::binary_roots_ignore_job_and_settings` (SC-007, `spec.md:970-980`): `cli::main_with(args, &seams,
  recording_run)` with `--job <tmp>/com.krateaudio.krate-manager/jobs/job-<32 hex>.json --job-sha256 <hex>` and a
  `settings.json` beside it, both naming `C:\Windows`, `/etc` and a temp dir in every key the manager's settings and FR-001
  know; the `AllowedRoots` the recording `RunFn` receives equal `AllowedRoots::production()` (read-only known-folder
  lookups on Windows; literals elsewhere; `OsRoots::None` on Linux).
- `roots::tests::production_roots_windows` (`cfg(windows)`): `C:\Program Files\Common Files\VST3\Krate Audio`,
  `C:\ProgramData\Krate Audio`, `run_base` `<Windows>\Temp`, `StateDirRule::UnderProfiles(<UserProfiles>)` (compared
  through `known_folder`, and the first two also as literals of this box and the runner). `roots::tests::
  production_roots_macos` (`cfg(target_os = "macos")`): the four D3 literals, `volume` `/`, `run_base` `/Library/Application
  Support/com.krateaudio.krate-manager-helper`, `run_ancestors` `["/Library", "/Library/Application Support"]`,
  `SameOwnerNonRoot` (PD-5). Non-required rows: `canonical` over a temp root whose tail does not exist (longest existing
  ancestor + plain components, FR-026), a missing component `..` → `E_HELPER_ROOTS`.
- `cli::tests::ci_roots_flag_is_usage_error` (`#[cfg(not(feature = "ci-temp-roots"))]`): `--ci-roots x` → exit 2, the
  `RunFn` never called. `cli::tests::malformed_hash_is_usage_error` (PD-25): `xyz`, 63 hex, 64 upper-case hex → exit 2,
  `RunFn` never called, nothing written. `cli::tests::ci_roots_file_is_read` (`#[cfg(feature = "ci-temp-roots")]`, not in
  `required-tests.json`, FR-103a): a `--ci-roots` file in a temp dir yields its roots with `Exact(state_dir)`; a root
  outside the file's directory → exit 4 `E_HELPER_ROOTS`; `run_base`, `run_ancestors`, `volume` stay production.
- `crates/helper/tests/usage.rs` `helper_usage_exits_2` (SC-023, `spec.md:1128-1130`): the built binary
  (`env!("CARGO_BIN_EXE_krate-manager-helper")`, `current_dir` a `TempDir`) with no argument, `--anything`, `--job` only,
  a repeated `--job` → exit 2, empty stdout, exactly one stderr line equal to `cli::USAGE`, the `TempDir` empty afterwards.
  (FR-106 allows only the no-argument run in CI's `check`; locally every row runs unelevated and touches nothing.)

**Implement:** `parse_args` / `main_with` per §4.1.3 (`plan.md:294-308`); `"--ci-roots"` and `from_ci_file` only under
the feature (so the default binary holds the marker zero times, FR-103); `production()` per §4.1.6 (`plan.md:378-381`)
through `os::known_folder(KnownFolder::…)` (TA-6); `canonical` per FR-026; `main.rs` exactly plan `:312-321`
(`#![cfg_attr(windows, windows_subsystem = "windows")]`, no module). Delete `tests/stub.rs` in the same save as
`main.rs`. Expected red: rows that reach `TempIo` (T007) or `known_folder` (T008).

**Pass:** L-OWN (`--lib roots:: cli::`, `--test usage`); L-HFEAT (its log holds `cli::tests::ci_roots_file_is_read ...
ok` once); helper clippy 0 warnings. **Lane:** app lane.

### T005 [P] — Helper `validate`: job location, sources, uninstaller, pkg classification, run-directory checks (spec FR-020 to FR-027, FR-031, FR-034, D7, D16, D17; SC-001, SC-008, SC-027 pure rows; plan §4.1.7, PD-5, SI-16)

Slot `F:/tmp/km10-target/helper`. Logs `F:/tmp/gate/km10-T005/`.

**Files:** `crates/helper/src/validate.rs`, `validate_tests.rs`.

**Failing tests first** (plan §7.1 rows SC-001, SC-008, SC-027, `plan.md:1028`, `:1035`, `:1051`, binding):
- `validate::tests::rejects_crafted_jobs`: one row per line of the SC-001 table (`spec.md:896-915`), each through
  `krate_manager_helper::run` with the `os` its op targets (both for OS-independent rows), temp roots with a planted
  `outside/` sibling, `UnderProfiles(<tmp>/profiles)` (Windows rows, state dir `<tmp>/profiles/u/AppData/Local/
  com.krateaudio.krate-manager` built with `Path::join`) or `SameOwnerNonRoot` with `world.owner(…, 501, 0o755)` on the
  state-dir paths (macOS rows, every leg); each yields its exit and code (`Exit`, PD-4), zero `Call::Spawn`, zero
  removals, and `snapshot` of the temp tree byte-identical before and after; plus SI-12's row (a `Fault::CreateNew` with
  `PermissionDenied` on the result name → exit 4 `E_HELPER_RESULT_WRITE`, nothing written) and the matcher cross-check
  (`is_app_id(k)` iff `krate_manager_core::contract::check_install_value("windows_uninstall_key", &format!("{k}_is1"))`
  accepts, over the table's ids; `is_pkg_id` likewise for `macos_pkg_id`; `contract.rs:267`). The FIFO, symlink and
  `symlink_dir` rows follow Rule 15.
- `validate::tests::performs_valid_jobs`: each of the four ops valid with its own `os` → `done` and the recorded calls;
  the macOS `run_pkg_installer` row reaches `done` on every leg because its run-directory chain is temp `run_ancestors` +
  `run_base` marked `world.root_owned` (PD-5); the Windows row passes verbatim `\\?\` `file`, job path and staging as
  literal strings through `TempIo` (FR-027).
- `validate::tests::verbatim_paths_windows` (`cfg(windows)`): the same with real `std::fs::canonicalize` output of a temp
  dir.
- `validate::tests::symlinked_parent_root_unix` (`cfg(unix)`) and `…_windows` (`cfg(windows)`, `symlink_dir`; fails with
  "symbolic-link privilege not held: enable Developer Mode" when it cannot create the link, never skips, plan
  `:561-565`): the six rows of the SC-008 table (`spec.md:987-994`).
- `validate::tests::run_dir_ancestors` (four `LstatFacts` rows → `Ok`; uid 501, mode `0o775`, `0o757`, kind `Symlink` →
  `E_HELPER_RUN_DIR`) and `validate::tests::run_dir_sddl` (`RUN_DIR_SDDL == "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"`).

**Implement:** every `validate` function per §4.1.7 (`plan.md:385-416`): byte matchers, no regex crate; `same_path` /
`under` component-wise, ASCII case-insensitive on Windows with the `Prefix` included; `pkg_plan` joins onto `volume` +
`location` segments, canonicalizes each entry's parent only. Expected red: every `run`-driven row until T003, T004, T006,
T007, T008 land; the pure rows (`run_dir_ancestors`, `run_dir_sddl`) are green on their own.

**Pass:** L-OWN (`--lib validate::`), helper clippy 0 warnings. **Lane:** app lane.

### T006 [P] — Helper `ops`: the four operations, hash-while-copying, batch execution, timeouts, polling (spec FR-004, FR-030 to FR-036, FR-040 to FR-043, D6, D7, D14; SC-002 to SC-006, SC-027 op rows; plan §4.1.10, PD-17, PD-24, SI-12, SI-28)

Slot `F:/tmp/km10-target/helper`. Logs `F:/tmp/gate/km10-T006/`.

**Files:** `crates/helper/src/ops.rs`, `ops_tests.rs` (the one helper test file allowed host literals, TA-7).

**Failing tests first** (plan §7.1 rows SC-002 to SC-006 and SC-027, `plan.md:1029-1033`, `:1051`, binding; the spec rows
`spec.md:922-969`, `:1151-1159`): `ops::tests::exact_argv` (literal argv vectors; `digest::sha256` of the file at each
spawned installer path equals the job's; no spawn under staging); `batch_of_three_middle_locked` (a test classifier, 99 →
`Locked`, else `classify_inno`; exactly one `CreateRunDir` before the first spawn and one directory removal after the
third); `batch_timeout_stops_the_rest` (two spawns, no kill, the fake clock advanced by exactly `INNO_INSTALL_TIMEOUT`
during op 2, exit 0, result written); `uninstall_only_job_has_no_run_dir`; `run_dir_failure_fails_installers`;
`copy_error_is_named` (`Fault::WriteAfter` `StorageFull`); `spawn_error_is_named` (`Script.spawn_error`);
`classify_sees_whole_bounded_log` (a recording classifier sees every line after a matching line followed by 500 more;
a 3 MiB log hands over only its last MiB, `read_tail`); `uninstall_polls_until_gone` (`Event`s at 3 s: `seconds` 3.0, six
`Call::Sleep(500 ms)`); `uninstall_poll_times_out` (241 checks, 240 sleeps, `seconds` 120.0, the test's own `Instant` wall
time < 5 s, the one place `Instant` appears in a helper test, SI-25); `uninstaller_never_exits` (PD-24);
`runs_the_copy_never_the_original` (the four `Hook` rows; the Unix FIFO row returns in < 1 s of wall time);
`pkg_paths_classified`; `pkg_failure_paths`; `run_dir_removed_after_job`; `run_dir_refusals_spawn_nothing`;
`temp_io_refuses_outside_base` (`#[should_panic(expected = "outside the test sandbox")]`: `TempIo::new(&world, A)` asked
to `create_run_dir` under a second, unrelated `TempDir` B) and its non-panicking companion rows (`catch_unwind` per other
mutating method and per `open_source` / `read_bounded` / `read_tail` outside the base). Every registry subkey and pkgutil
prefix built from the constants except in `exact_argv` (TA-17).

**Implement:** §4.1.10 (`plan.md:483-526`) verbatim: lazy run directory `<run_base>/run-<random_hex>`; macOS
`create_helper_folder` then `run_dir_ancestors_ok` over `run_ancestors` + `run_base` + the new directory; per-op `start =
io.now()`; installer copy as `installer-<i>.<ext>`, log `inno-<i>.log`; `read_tail(log, 1 MiB)` → `classify` over
`log_lines`, `log_tail` for the result; the uninstaller's own wait bounded by `UNINSTALL_POLL_TIMEOUT`; FR-033's
validate-all-then-remove, no `--forget` after a failed removal; FR-043 cleanup. Expected red: until T003, T005, T007, T008
land.

**Pass:** L-OWN (`--lib ops::`), helper clippy 0 warnings. **Lane:** app lane.

### T007 [P] — Helper `seams::fake`: `World`, `TempIo`, `ScriptedRunner`, `FakeRegistry` (spec FR-053; plan §4.1.9, PD-6, PD-8, SI-23; TA-3, TA-13)

Slot `F:/tmp/km10-target/helper`. Logs `F:/tmp/gate/km10-T007/`.

**Files:** `crates/helper/src/seams.rs` (the `fake` module bodies; the traits stay as T001 wrote them),
`crates/helper/src/seams_tests.rs`.

**Failing test first:** `seams::tests::fake_world_cases` (TA-13, not required): the clock advances by `sleep` and by
`wait`'s scripted `takes` (or the full `timeout` when `never_exits`); an `Event::RemovePath` at 3 s removes the real path
when `now()` passes 3 s and not before; `Event::RemoveKey` empties the registry entry; `Match::Nth(1)` scripts the second
call; `Fault::CreateNew` hits only its file name; `WriteAfter` fails after exactly N bytes; `root_owned(d)` makes `lstat`
of `d` and of a directory created under it report uid 0, mode `0o755` / `0o700` for `run-*`; `owner` overrides apply;
every mutating call outside `base` panics with "outside the test sandbox" while `canonicalize`, `lstat` and `list_dir`
do not; `calls()` records in order.

**Implement:** plan §4.1.9 (`plan.md:458-478`) over T001's surface (TA-3): `Mutex`-based `World`; `TempIo` is the real
filesystem under `base` plus the world's overrides, faults and hooks; its `open_source`, `read_bounded` and `read_tail`
call `os::open_regular_no_follow` (PD-6) after the hooks; the sandbox check is lexical (as `Roots::check_sandboxed`,
`crates/core/src/roots.rs:173-184`); counter-based `random_hex` (deterministic); `ScriptedRunner` records every call and
advances the clock; `FakeRegistry` reads the world's map. No `Seams {` construction in this file (T012's rule: non-test
sources build it only in `main.rs`). Expected red: rows that open files until T008's `open_regular_no_follow` lands.

**Pass:** L-OWN (`--lib seams::`), `cargo clippy -p krate-manager-helper --all-targets --features test-support --locked
-- -D warnings` 0 warnings (the feature build of the fake, as core's dev build compiles it). **Lane:** app lane.

### T008 [P] — Helper `os`: the real seams, the no-follow open, known folders, the run directory, the registry (spec FR-030 step 1, FR-041, FR-051, FR-053, D7; SC-005, SC-027 OS rows; plan §4.1.12, PD-6, PD-7, PD-19, SI-25; TA-6)

Slot `F:/tmp/km10-target/helper`. Logs `F:/tmp/gate/km10-T008/`.

**Files:** `crates/helper/src/os.rs`, `os_tests.rs` (the one test source that may build the real seams, T012's rule).

**Failing tests first** (plan §4.1.12 `plan.md:551-565`, binding): `os::tests::read_bounded_stops_at_bound` (all OSes);
`os::tests::open_source_refuses_link_windows` (`cfg(windows)`, `symlink_file`; the named-message rule of T005);
`os::tests::open_source_refuses_links_and_fifos_unix`, `os::tests::run_dir_mode_unix`,
`os::tests::wait_timeout_leaves_child_unix` (`cfg(unix)`; `/usr/bin/mkfifo` and `/bin/sleep 30` inside a `TempDir`,
the child killed and reaped by the test). Non-required: `random_hex` is 32 lower hex and two calls differ; `known_folder`
returns a path for each `KnownFolder` (Windows).

**Implement:** §4.1.12 verbatim: `OsHelperIo { origin: Instant }` (replace `_private`), `std::fs` files, `lstat` with
`MetadataExt::{uid, mode}` on Unix, `create_run_dir` (Unix `DirBuilder::mode(0o700)`; Windows
`ConvertStringSecurityDescriptorToSecurityDescriptorW(RUN_DIR_SDDL, SDDL_REVISION_1, …)` → `SECURITY_ATTRIBUTES` →
`CreateDirectoryW` → `LocalFree`), `create_helper_folder` (`0o755`, PD-26), `random_hex` (PD-19), `sleep` / `now` (the
helper's only `Instant` home, FR-041); `OsRunner { children: Mutex<BTreeMap<u64, Child>> }` (argv, all stdio `null()`,
`try_wait` every 100 ms, `128 + signal` on Unix, never a kill); `OsRegistry` (`RegOpenKeyExW(HKEY_LOCAL_MACHINE, …,
KEY_READ | KEY_WOW64_64KEY)` + `RegGetValueW(RRF_RT_REG_SZ)`, the `src-tauri/src/os_facts.rs:46-107` pattern; `None` /
`false` off Windows); `known_folder` (`SHGetKnownFolderPath`, `CoTaskMemFree` on every path, TA-6);
`open_regular_no_follow` per PD-6 with the `libc-0.2.191` constants of `plan.md:45` and a `compile_error!` for other Unix
targets. Every `unsafe` block carries a `// SAFETY:` comment. Never call the registry reader, `known_folder` or
`create_run_dir` on a production path in a test: `create_run_dir` and `open_*` rows run inside a `TempDir`.

**Pass:** L-OWN (`--lib os::`), helper clippy 0 warnings, L-HFEAT clippy; the `cfg(unix)` rows are "not compiled here"
(Rule 15) and first run in L-WSL. **Lane:** app lane.

### T009 [P] — Core `install`: the drivers, the guard, the job writer and reader, the gate arms, recovery, notices, quoting, osascript outcome, helper resolution, and every `install::tests::*` row (spec FR-070 to FR-084, FR-090 rule, FR-091, FR-114; SC-003 core twin, SC-009, SC-010, SC-012, SC-014, SC-015 core rows, SC-022, SC-024, SC-028 to SC-030; plan §3.2, §4.2.1-§4.2.5, PD-9 to PD-19, PD-23, SI-3, SI-4, SI-11, SI-13, SI-20, SI-24, SI-31; TA-4, TA-5, TA-12, TA-14)

Shared target, 16 jobs (the long pole). Logs `F:/tmp/gate/km10-T009/`.

**Files:** `crates/core/src/install.rs`, `crates/core/src/install_tests.rs`.

**Failing tests first** (plan §7.1 rows SC-003, SC-009, SC-010, SC-012, SC-014, SC-015, SC-022, SC-024, SC-028 to
SC-030, `plan.md:1030`, `:1036-1042`, `:1046`, `:1048-1054`, binding; spec `spec.md:998-1069`, `:1123-1127`,
`:1131-1135`, `:1160-1176`): the 23 `cargo.all` names of TA-1 plus `native_wiring_windows` (`cfg(windows)`) and
`native_wiring_macos` (`cfg(target_os = "macos")`). Notes per row:
- `drivers_per_os` replaces `no_driver_off_linux`; `native_wiring_windows` / `_macos` replace
  `native_wiring_offers_nothing_*` (`install_tests.rs:3850-3925`), each deleted in the same save as `driver_for`'s flip
  (TA-4): `static WINDOWS: WindowsDriver`, `static MACOS: MacosDriver`, `driver_for` returns all three (FR-070).
- `new_error_texts` (TA-5): `to_string()` of each new variant against §3.2's literals; it passes on first run (T001 wrote
  the texts); say so in the notes.
- Loopback rows (`batch_locked_middle_maps_each`, `windows_install_update_remove`, `macos_install_update_remove`,
  `job_hash_is_the_verified_digest`) use T010's `Loopback` (TA-14) over `fresh_elevated_roots()` and verified artifacts at
  `<staging>/downloads/<sha hex>/<asset>` reached through `obtain_and` and the fixture server (SI-20); the macOS row
  compares `FakeHostIo` calls with `platform::KILLALL_ARGV` (TA-12).
- `pre_job_refusals` uses `Fx::verified` artifacts outside staging for `E_INSTALL_PATH` (SI-13,
  `install_tests.rs:724-770`).
- `elevating_event_order` in SI-24's two parts (`plan.md:1042`).
- `launch_outcomes_map` also asserts FR-114 through `test_support::install_capture`: no absolute path in an `info!` line.
- `helper_resolved_beside_exe` over a `TempDir` with `FsInstallIo`, every `Os` on every leg (PD-23, `plan.md:1052`).
- `timed_out_holds_guard` (SC-029): one `FakeElevation` across batches 1-3; a fresh fake for the restart; a concurrent
  batch for `stale: false`.
- `record_outside_sandbox_panics` names an absolute path under a second, unrelated `TempDir`, never a system path.

**Implement:** §4.2.1 (`ElevationGuard { hold: Mutex<Hold> }` replacing `_private`, `enter`, `Held::stale`, `Drop`;
`osascript_outcome`; `windows_parameters` per FR-091), §4.2.2 (`job::write` with PD-19's names, `ceiling` per PD-17,
`read_result` with SI-11's order `job_sha256` → `job_error` → length, `helper_code` over the 25 codes), §4.2.3 (the
eleven steps of `run_elevated`, `plan.md:626-658`; the op's `sha256` is always `verify::hex_lower(artifact.sha256())`,
never a re-hash; `check_sandboxed` on the jobs dir before the write; the registrar kill keyed on the helper's `done`
before re-detection), §4.2.4 (the Windows and macOS `RemoveBasis` arms of `action_gate`, PD-13's `app_id` = the key up to
its first `}`; FR-082's refusals), §4.2.5 (`recover`'s jobs pass with `check_sandboxed`, `RecoveryReport.jobs_removed`,
`log_recovery`), PD-16's notice by `env.os`, `helper_beside` (PD-23). Rule 4a and 4e: no `"/usr/`, no `_is1`, no
`Command`, `TcpStream` or `xattr` token in `install.rs`. Expected red: the loopback rows until T010 and the helper tasks
land.

**Pass:** L-OWN (`cargo test -p krate-manager-core --locked --lib install::`), core clippy 0 warnings, L-APP (src-tauri
compiles against the real bodies). **Lane:** app lane.

### T010 [P] — Core loopback: the `Elevation` that runs the helper library in-process against temp roots (plan §4.4 `install_loopback_tests.rs`, PD-8, SI-19; TA-13, TA-14)

Shared target. Logs `F:/tmp/gate/km10-T010/`.

**Files:** `crates/core/src/install_loopback_tests.rs` (declared by T001 as `install::loopback`, `#[cfg(test)]`).

**Step 1, first save (the API T009 codes against, TA-14), with `todo!("T010")` bodies and the smoke test:**
`pub(super) struct Loopback<'a>`; `pub(super) fn new(os: Os, roots: &Roots, base: &Path, host: &'a LiveHostFacts) ->
Loopback<'a>` (`roots` from `fresh_elevated_roots()`, `base` its `TempDir`); `pub(super) fn classify(self, f: fn(i32,
&[String]) -> InnoVerdict) -> Self` (default `classify_inno`); `pub(super) fn installs(&self, sha256_hex: &str, entry:
&PluginEntry)` (a run of the installer with that digest "installs" `entry` at `entry`'s version: Windows an Inno record in
`host.inno` + a `FakeRegistry` key with `UninstallString` `<install dir>\unins000.exe` and that file + the bundle
`<temp Vst3Global>\Krate Audio\<Name>\<vst3_bundle>` with a `moduleinfo.json` of that version; macOS a pkg record in
`host.pkg`, pkgutil scripts for `--pkg-info` / `--files --only-files` / `--only-dirs`, the VST3 and component bundles; an
uninstall op removes all of it); `pub(super) fn exit_for(&self, spawn_index: usize, code: i32)` (that spawn exits `code`
and installs nothing); `pub(super) fn never_exits(&self, spawn_index: usize)`; `pub(super) fn serve_log(&self,
spawn_index: usize, log: Vec<u8>)`; `pub(super) fn launches(&self) -> Vec<ElevatedCommand>`; `pub(super) fn jobs(&self) ->
Vec<Vec<u8>>`; `pub(super) fn spawns(&self) -> Vec<Vec<OsString>>`; `impl Elevation for Loopback<'_>`.

**Failing test first:** `install::loopback::loopback_smoke` (TA-13, not required): a Windows and a macOS install job
written by `install::job::write` for a staged file whose digest `installs` names, launched through the loopback → `Exited(0)`,
a result file whose op is `done`, the record and bundle present under the temp roots; an uninstall job removes them;
every root the loopback hands the helper lies under `base` (Rule 1).

**Implement:** plan §4.4 (`plan.md:711-720`) verbatim: `AllowedRoots` with every root under `base` (Windows `run_base =
<base>\run`, never `C:\Windows\Temp`; macOS `volume = <base>`, `run_ancestors` and `run_base` under `<base>/Library`, all
`world.root_owned`), `StateDirRule::Exact(state_dir)`, `TempIo::new(&world, base)`, `ScriptedRunner` effects,
`FakeRegistry`; `launch` parses argv with `cli::parse_args`, calls `krate_manager_helper::run`, returns
`Exited(exit.status)`; the guard is its own `ElevationGuard`. Rule 4c / TA-17: constants, not literals. Expected red: until
T009's `job::write` and the helper tasks land.

**Pass:** L-OWN (`--lib install::loopback::`), core clippy 0 warnings. **Lane:** app lane.

### T011 [P] — `status::tests::actions_per_state` (extended) and the Windows quoting round trip (spec FR-083, SC-013, SC-015 `windows_parameters_parse_back_windows`; plan §7.1 SC-013 / SC-015 rows)

Shared target. Logs `F:/tmp/gate/km10-T011/`.

**Files:** `crates/core/src/status_tests.rs`, `crates/core/tests/windows_quoting.rs` (new).

**Failing tests first:**
- `status::tests::actions_per_state` extended for `(Os::Windows, Arch::X86_64)` and `(Os::Macos, Arch::Aarch64)` with
  exactly SC-013's table (`spec.md:1023-1031`): the Windows outside-root record at `<sandbox>\elsewhere\Alpha` (built with
  `Path::join` inside the `fresh_temp_roots()` base, never a real path), `remove_hint` `"windows_settings"`; for every
  omitted action `for_command` / `remove_for_command` refuse with their code before any asset request and with no launch
  (`FakeElevation::refusing()`, launches 0). The Linux rows keep their verdicts.
- `crates/core/tests/windows_quoting.rs` `windows_parameters_parse_back_windows` (`#![cfg(windows)]` on the file's tests):
  `Command::new(env!("CARGO_BIN_EXE_argv-echo")).raw_arg(install::windows_parameters(&args)?)`, the echoed JSON equals
  `args` for the five SC-015 inputs (`spec.md:1067-1069`: a `--job` path with a space, a verbatim `\\?\C:\Users\John
  Smith\…` path, a path ending in `\` and in `\\`, a path with a tab). It spawns only that binary, unelevated (T001's
  `Command::new` exemption, TA-7). This file is not in `FR088_FILES`; T012 must not add it.

**Implement:** tests only. Expected red: both until T009's gate arms and `windows_parameters` land.

**Pass:** L-OWN (`--lib status::`, `--test windows_quoting`), core clippy 0 warnings. **Lane:** app lane.

### T012 [P] — Guards: the Phase 10 confinements and bans, and `fixture_names_confined` (spec FR-112, FR-113, FR-024 guard, SC-021; plan §4.8.1 new rules, §4.8.3, SI-25, SI-29; TA-7)

Shared target. Logs `F:/tmp/gate/km10-T012/`.

**Files:** `crates/core/tests/injection.rs` (CRLF), `crates/core/tests/roster.rs`.

**Failing tests first:** in `injection_scanner_reports_planted_violations` (`injection.rs:3687`), one plant per new rule,
each red until its rule exists (plan §4.8.1 `plan.md:838-851`, binding): `ShellExecuteExW` in `src-tauri/src/commands.rs`;
`runas`, `osascript`, `administrator privileges` outside `os_elevation.rs` / `install.rs`; `/usr/sbin/installer` in core;
`--forget`, `CreateDirectoryW`, `SHGetKnownFolderPath` outside `crates/helper/src/` (tests excepted for `installer` /
`--forget` in `ops_tests.rs`); an `OsRunner::new(` in the helper's `lib.rs`; a `Seams {` in non-test `ops.rs`; an
`OsHelperIo` built in `validate_tests.rs` (only `os_tests.rs` may, SI-25); an `Instant::now()` in `ops.rs` (non-test;
`crates/helper/src/*_tests.rs` exempt); a `SystemTime` in `ops.rs`; `NativeElevation::new(` outside `src-tauri/src/lib.rs`;
one plant per FR-024 token family in `ops.rs` (`xattr` / `setxattr` / `removexattr`, `chown` / `fchown` / `lchown`,
`SetNamedSecurityInfo` / `SetFileSecurity` / `SetSecurityInfo`, `com.apple.quarantine`); a `killall` in `status.rs`; each
reported with its rule name. In `roster.rs`: `fixture_names_confined` requires `Fixture` and `com.krateaudio.fixture` only
under `apps/krate-manager/tests/fixtures/` and in jobs `helper-elevated` / `inno-locked-file` of `manager-ci.yml`, with a
plant (a `Fixture` in `scripts/bundle.js` text) reported.

**Implement:** the rules, scoped to non-test sources where SI-25 says so (`is_test_source`, `injection.rs:1559-1567`). The
real-tree runs must be clean on the finished tree; a hit in another task's file while it is in progress is a real finding
for that task (expected-red rule (c)): record it, tell nobody to change a rule, and re-run before the gate. **No change
to the allow-list rows T001 widened.**

**Pass:** `cargo test -p krate-manager-core --locked --test injection --test roster` green on the real tree once G2's code
has landed; core clippy 0 warnings. **Lane:** app lane.

### T013 [P] — `src-tauri`: the native launcher and the manual elevation example (spec FR-090, FR-091, FR-105, D8, D19; SC-019 L-MANUAL; plan §4.5, PD-18, PD-23, SI-14)

Shared target. Logs `F:/tmp/gate/km10-T013/`.

**Files:** `src-tauri/src/os_elevation.rs`, `src-tauri/examples/manual_elevation.rs`.

**Test first:** none can run here: `src-tauri`'s library is `test = false` (`src-tauri/Cargo.toml:10-13`), and the
launcher is never exercised locally (Rule 1; Phase 12 owns the user-performed checks). The task's evidence is L-APP,
L-DEVFEED and L-MANUAL clean, plus a reading of every branch against FR-090 / FR-091 (`spec.md:631-654`) and plan §4.5
(`plan.md:731-741`) recorded in the notes; the rules that decide anything (`windows_parameters`, `osascript_outcome`,
`helper_beside`) are T009's tested core code.

**Implement:** Windows `launch` exactly `plan.md:731-737` (own thread, `CoInitializeEx(COINIT_APARTMENTTHREADED |
COINIT_DISABLE_OLE1DDE)`, `SHELLEXECUTEINFOW` with `runas`, `lpParameters = install::windows_parameters(args)?`, `fMask
SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI`, `nShow: 0` with the `// SW_HIDE` comment, the
`ERROR_CANCELLED` → `Refused` mapping, `WaitForSingleObject` / `GetExitCodeProcess` / `CloseHandle` / `CoUninitialize`,
`// SAFETY:` on every `unsafe`); macOS `launch` (`/usr/bin/osascript`, `-e` + the three `OSASCRIPT_LINES`, the helper
path and the four args, stdout null, stderr piped, `try_wait` polling from the spawn, `TimedOut` leaves the child,
`install::osascript_outcome`); Linux `Failed("E_ELEVATION_NONE")`. The example per `plan.md:751-771` (SI-14): refuses a
non-empty or linked argument (exit 2), creates exactly two directories, writes the fail-closed job through
`install::job::write` (`UninstallPkg { pkg_id: "com.krateaudio.NOT-VALID" }` / `RunInnoUninstaller { app_id:
"not-a-guid" }`), resolves the helper via `resolve_elevation(Some(&exe_dir.parent()?.join("manual_elevation")))`, launches
with 120 s, prints the outcome and the result, removes only what it recorded. **Never run it.**

**Pass:** L-APP, L-DEVFEED, L-MANUAL 0 warnings; WSL cannot build `src-tauri` (no WebKitGTK), so the macOS / Linux
branches are "not compiled here" (Rule 15) and close on CI's `clippy` steps. **Lane:** app lane.

### T014 [P] — The UI: Windows / macOS actions, confirmations, progress, outcomes, the Windows Settings hint (spec FR-130 to FR-135; SC-009 vitest row, SC-025; plan §4.7, PD-15; SI-30; TA-12)

**Files:** `src/status.ts`, `src/status.test.ts`. (`view.ts` is T001's.)

**Failing tests first:** in `src/status.test.ts`, the twelve titles of TA-1's vitest list, each red before the code
(`renders install on windows and macos` replaces `renders no install buttons on windows or macos`, deleted in the same
save): DOM assertions over the Phase 9 rig (injected `invoke` / `makeChannel`); the rejection strings the fixtures inject
are FR-133's sentences (`spec.md:854-864`), which T001's `Display` texts produce (PD-15); `macos install shows the au
rescan steps` asserts in order "Restart the Mac", "Reset & Rescan Selection", "Full Audio Unit Reset", the
`['~', 'Library', 'Caches', 'AudioUnitCache'].join('/')` phrase (TA-12) and "support.apple.com/en-us/122179".

**Implement:** plan §4.7 (`plan.md:822-832`): `INSTALL_TEXTS` gains `adminWindows`, `adminMacos`,
`confirmRemoveInstaller`, `elevating`, `rescanMacos1` … `rescanMacos4`, `appleGuide`, `removeElsewhere` (FR-131 to
FR-134 verbatim); `confirmLines(os, p, action)`; `progressText` for `elevating`; `outcomeLines` for `rescan_daw_macos`;
`renderCard` shows `removeElsewhere` and no Remove when `remove_hint` is set; `actionRow` receives `os`; errors keep
`rejectionText` (`status.ts:150-154`). Rule 4f.

**Pass:** L-TS green, vitest 57 titles, each of the twelve passing once. **Lane:** app lane.

### T015 [P] — Node scripts and the Inno fixture script (spec FR-092, FR-100 job writer, FR-101; SC-020 node rows; plan §4.6, PD-20; TA-16 for the bundle path)

**Files (new, LF):** `apps/krate-manager/scripts/bundle.js`, `bundle.test.js`, `write-test-job.js`,
`write-test-job.test.js`, `inno-fixture.test.js`, `apps/krate-manager/tests/fixtures/inno/fixture.iss`.

**Step order (plan §4.6 `plan.md:784-791`, binding):** write `scripts/bundle.js` and `bundle.test.js` alone first, run
`npm run typecheck && npm run lint` on them (T001 added `allowJs` / `checkJs` and `scripts/*.js` to `tsconfig.node.json`);
fix any finding in code (JSDoc types, `r.status ?? 1`, explicit `process.argv[n] === undefined` checks), never with a
suppression; only then write the other files.

**Failing tests first** (`node:test`, each red before its export exists):
- `bundle sidecar name per host tuple` (`bundle.test.js`): `sidecarName('x86_64-pc-windows-msvc') ===
  'krate-manager-helper-x86_64-pc-windows-msvc.exe'`, `sidecarName('aarch64-apple-darwin') ===
  'krate-manager-helper-aarch64-apple-darwin'`, `x86_64-unknown-linux-gnu` without extension; `tauriArgs('win32')` and
  `tauriArgs('darwin')` equal `['build', '--config', 'src-tauri/tauri.helper.conf.json']`, `tauriArgs('linux')` equals
  `['build']`.
- `write test job prints verbatim windows paths` (`write-test-job.test.js`): `verbatim` rows (drive → `\\?\` + p, UNC
  `\\server\share\x` → `\\?\UNC\server\share\x`, already verbatim unchanged, any non-win32 platform unchanged); a job
  written into a temp dir with `platform = 'win32'` forced holds `\\?\` `file` values, FR-001's key order, and the printed
  `<path> <sha256>` matches `node:crypto`'s digest of the bytes.
- `fixture inno script keeps the plugin directives` (`inno-fixture.test.js`, FR-101 `spec.md:711-726`): normalises
  `tests/fixtures/inno/fixture.iss` and `plugins/iterum/installers/windows/setup.iss` by each file's own `#define AppName
  "<name>"` line (`setup.iss:16`) → `<APP>`, the AppId → a placeholder, the two roots; requires the listed directive lines
  equal (`PrivilegesRequired=admin`, `UsePreviousAppDir=yes`, `ArchitecturesAllowed=x64compatible`,
  `ArchitecturesInstallIn64BitMode=x64compatible`, every `[Files]` `Flags:` value, the `[UninstallDelete]` entry); no
  fixture line names `{commonpf}`, `{commonappdata}`, `{pf}`, `{sys}` or `{win}`; a planted drift in each listed line is
  reported. The test source names neither the plugin nor the fixture (`fixture_names_confined`, T012).

**Implement:** plan §4.6 verbatim: ESM, `#!/usr/bin/env node`, a usage line, the main-module guard with
`pathToFileURL`, `import.meta.dirname`; `bundle.js` runs `rustc --print host-tuple`, `cargo build -p krate-manager-helper
--release --locked` (not on Linux), copies to `src-tauri/binaries/<sidecarName>`, then `npx tauri …`, `spawnSync` with
`stdio: 'inherit'`, exit code propagated; `write-test-job.js` per `plan.md:799-811` (CI only; takes the ids as
arguments). `fixture.iss`: plan FR-101 text (`{#Root}`, `{#Version}`, `{#Payload}` defines, `DefaultDirName={#Root}\VST3\Krate
Audio\Fixture`, presets at `{#Root}\ProgramData\Krate Audio\Fixture`, a fixed fixture AppId, the DLL at
`Fixture.vst3\Contents\x86_64-win\Fixture.vst3`, an unlocked text file holding the version). **Never run `bundle.js`
(L-BUNDLE is the main loop's), `write-test-job.js` outside its test, or any Inno compiler.**

**Pass:** L-NAPP (three names `ok` once), L-TS (`typecheck`, `lint` clean). **Lane:** app lane.

### T016 [P] — CI Rust fixtures: `locked-dll`, `argv-recorder`, and the M-1 fixture directory (spec FR-100 stub installer, FR-101, FR-102; plan §2 fixtures row, §8.2)

Slot `F:/tmp/km10-target/fixtures`, 4 jobs. Logs `F:/tmp/gate/km10-T016/`.

**Files (new, LF):** `tests/fixtures/locked-dll/{Cargo.toml, Cargo.lock, src/lib.rs}`,
`tests/fixtures/argv-recorder/{Cargo.toml, Cargo.lock, src/main.rs}`, `tests/fixtures/helper/.gitkeep` (empty until M-1,
SC-032).

**Failing check first:** L-FIX on both crates before their sources exist → red (`can't find` the target); quote it.

**Implement:** both crates `edition = "2024"`, `publish = false`, no dependencies (the `clippy-guard` form,
`tests/fixtures/clippy-guard/Cargo.toml`), excluded from the workspace by T001. `locked-dll`: `[lib] crate-type =
["cdylib"]`, one export `#[unsafe(no_mangle)] pub extern "C" fn fixture_version() -> *const core::ffi::c_char` returning a
static NUL-terminated `concat!(env!("LOCKED_DLL_VERSION"), "\0")` (the version embedded at build time, FR-060).
`argv-recorder`: `fn main` writing, to the compile-time path `env!("ARGV_RECORDER_OUT")`, one JSON object with its
`args_os` (lossy), `current_exe()`, and the SDDL of its own directory read with `pwsh -NoProfile -Command
"(Get-Acl -LiteralPath '<dir>').Sddl"` (FR-100), then exit 0. Lockfiles: `cargo generate-lockfile --manifest-path
tests/fixtures/<crate>/Cargo.toml --offline` each (Rule 8). **Never run either binary or load the DLL here** (they run only
in the two elevated CI jobs; FR-101 "No fixture setup is ever run outside these jobs").

**Pass:** L-FIX green for both (the DLL built with 1.0.0 and 1.0.1); `git status` shows no `target/` under the fixtures.
**Lane:** app lane.

### T017 [P] — Evidence scripts: scope check, L-WSL, L-BUNDLE, L-CLOSE (spec FR-121, FR-122, SC-026; plan §7.4, §8; TA-11, TA-16)

**Files (new, LF, CommonJS like `specs/krate-manager-phase9/evidence/*.js`):** `specs/krate-manager-phase10/evidence/
scope-check.js`, `wsl-lane.js`, `bundle-lane.js`, `close-lane.js`.

**Failing tests first:** each script's `--self-test` over synthetic inputs, red before its logic: `scope-check` (Rule 11's
allowed set; `plugins/x`, `dsp/x`, `release.yml`, `feed-tools.yml`, `tools/feed-contract/schemas/x`, `context.rs` each
refused; the allowed paths accepted); `wsl-lane` (`toWslPath`, the verdict over a fake log with the 13 names, a missing
name, a `test result: FAILED`); `bundle-lane` (the `--ci-roots` counter over a byte buffer, the sidecar name, the NSIS
setup glob); `close-lane` (the row runner's verdict and the timings table writer).

**Implement:** adapt Phase 9's `scope-check.js`, `wsl-lane.js` and `close-lane.js` (`specs/krate-manager-phase9/evidence/`):
`wsl-lane.js` runs TA-11's command with a 40-minute ceiling (the first cold helper compile) and the 13-name information
line; `bundle-lane.js` runs `npm run bundle` from `apps/krate-manager/` (log under `F:/tmp/gate/km10-bundle/`), then TA-16's
local checks and the `build warnings` rule (`manager-ci.yml:185-198`), never runs the produced setup or the helper;
`close-lane.js` runs plan §7.4 steps 1-4 and 6 (`plan.md:1081-1097`: the five clippy lines, the clippy-guard replay, both
cargo test runs, npm, `node --test "scripts/*.test.js"`, the feed-contract TAP + `tap-check.js`, the version lint, L-YAML,
L-LINT, L-INDEX, L-SCOPE, L-REQ, the greps of T019 step 6), then L-WSL and L-BUNDLE, writes
`specs/krate-manager-phase10/evidence/timings.md`, and ends with `node tools/clean-test-output.js --apply`. Concurrency as
plan §7.4 says (`plan.md:1098-1100`): the cargo chain, the npm / node chain and L-WSL side by side; L-BUNDLE after the
cargo chain (it holds the build lock). Each script: `#!/usr/bin/env node`, usage line, `--self-test`, writes only its logs
and stdout.

**Pass:** `node --check` and `--self-test` green on all four. **Lane:** pipeline lane. (Running the lanes is the main
loop's.)

---

## G3 — Integration (sequential: T018, then T019)

### T018 — `manager-ci.yml` (`changes`, `helper-elevated`, `inno-locked-file`; the `check` additions) and `manager-ci-shape.test.js` (spec FR-100 to FR-109, FR-111; SC-017 to SC-020 CI rows, SC-031; plan §4.8.4, §8.2, PD-20 to PD-22, PD-27, SI-2, SI-15, SI-18, SI-26, SI-27, SI-29)

Alone in its group, so it edits both shared files. Logs `F:/tmp/gate/km10-T018/`. **Written, never pushed or
dispatched.**

**Files:** `.github/workflows/manager-ci.yml` (LF), `tools/feed-contract/tests/manager-ci-shape.test.js` (LF).

**Failing tests first:** in the shape test (plan §4.8.4 `plan.md:878-905`, binding): `STEPS` / `COMMANDS` /
`ASSERTION_STEPS` gain `clippy ci-temp-roots`, `clippy manual-elevation example` (after `clippy dev-feed`), `test
ci-temp-roots` (after `cargo test`), `node --test` (after `lint`, before `vitest`, PD-22), `helper bundled`, `ci-temp-roots
absent` (after `build warnings`); `tauri build`'s command becomes `npm run bundle 2>&1 | tee "$RUNNER_TEMP/tauri-build.txt"`;
the `required tests ran` lines gain the TAP file, `req.node` (PD-27's regex) and the notice `+ <n> node`; `checkJob`
accepts `['check', 'changes', 'helper-elevated', 'inno-locked-file']`; a new `await t.test('elevated jobs are path-gated',
…)` **subtest** of the existing top-level test (`:1068`) with every check and mutation of `plan.md:882-896` (SC-031,
SI-29) and the parent's `pathGatedRan` assertion. Run L-NODE → red against the unchanged workflow; quote the failing
subtests.

**Implement the workflow** (plan §8.2 `plan.md:1114-1142`, FR-100 to FR-109, binding):
- `check`: the six new steps in the shape test's order; `test ci-temp-roots` writes `$RUNNER_TEMP/cargo-test-ci-roots.txt`
  and asserts one `cli::tests::ci_roots_file_is_read ... ok`; `node --test` tees `$RUNNER_TEMP/node-tap.txt` with `set -o
  pipefail`; `helper bundled` and `ci-temp-roots absent` per `plan.md:1118-1127` (Linux: the `cargo tree` and `cli.rs`
  counts and `::notice::no Linux helper binary is built (D18)`); `required tests ran` reads `req.node` from the TAP file.
- `changes` (PD-21): `ubuntu-24.04`, `actions/checkout@v6` with `fetch-depth: 0`, `permissions: contents: read`, one
  bash step with the three branches and the fail-open, event values through `env:`, the path list of FR-102b plus
  `apps/krate-manager/Cargo.toml` and `Cargo.lock` (SI-27), output `elevated`.
- `helper-elevated` (matrix `macos-latest`, `windows-latest`; `needs: changes`, `if: needs.changes.outputs.elevated ==
  'true'`; `contents: read`; no secret, no `environment:`; `defaults.run.working-directory: apps/krate-manager`): the steps
  of `plan.md:1129-1138` and FR-100 (`spec.md:673-710`): the token-elevated check, temp roots under `$RUNNER_TEMP/real`,
  the two ci-roots files, `write-test-job.js`, the Windows `pwsh` `Start-Process … -Wait -PassThru` runs, the macOS `sudo`
  runs, the fixture pkg with `pkgbuild --scripts` and its `preinstall` record, the `$RUNNER_TEMP/link` case, every
  SC-017 / SC-018 assertion as `::notice` / `::error`, and the `if: always()` `sudo pkgutil --forget com.krateaudio.fixture
  || true` (FR-111).
- `inno-locked-file` (`windows-latest`, gated the same way): two `locked-dll` builds, two fixture compiles (1.0.0 /
  1.0.1) with `Minionguyjpro/Inno-Setup-Action@v1.2.7` (`release.yml:244-247`), the FR-060 / FR-102 measurement in `pwsh`
  steps, every FR-060 value a `::notice` (plus the Inno version the compile step reports, for M-1). **No FR-102a
  assertion yet** (it needs the M-1 fixtures; R1 adds it).
- Every path any job writes lies under `$RUNNER_TEMP` except the run directory, the fixture receipt and the fixture's own
  HKLM key (FR-111). `sudo` only in `helper-elevated`; `--features ci-temp-roots` only in the two jobs and `check`'s two
  steps; `manual-elevation` only in its step; `pkgbuild`, `installer -pkg`, `/usr/sbin/installer`, the Inno action, `ISCC`
  and the helper with arguments only in the two elevated jobs (FR-106).

**Then:** L-NODE + L-TAP (`TAP plan=1..59 top-level=59`: the new checks are subtests), L-YAML, L-LINT, and a local
replay of the new `required tests ran` (L-REQ) over T019-ready logs is T019's.

**Pass:** L-NODE `# fail 0`, L-TAP, L-YAML, L-LINT green; `git diff --name-only` shows only the two files. **Lane:**
pipeline lane.

### T019 — L-REQ, INDEX, lints, scope, greps, the roadmap status line, the M-1 fence report, the PENDING-CI list and the close hand-over (spec FR-063, FR-108, FR-109, FR-121, FR-122, FR-123 (c) + (h); SC-026, SC-032; plan §7.3, §7.4; TA-1, TA-15)

**Files:** possibly `tests/required-tests.json` and `manager-ci-shape.test.js` `EXPECTED_REQUIRED` (only if step 2 finds a
name that differs from the real test; alone in its group, it may), `specs/Krate-manager-roadmap.md` (the status line,
TA-15), `specs/INDEX.md` (only through `node tools/gen-specs-index.js`). Logs `F:/tmp/gate/km10-T019/`.

**Test first:** `node tools/gen-specs-index.js --check` and L-REQ (expected `345 cargo + 57 vitest + 3 node`).

**Steps:**
1. L-RS (nothing else executing; record the wall clock and every `test result:` line), L-HFEAT, L-TS with the vitest
   JSON report, L-NAPP.
2. L-REQ over step 1's logs. A `::error … passing line(s)` names a required name the run lacks: if the test is right
   and the list is wrong, fix both lists (same order, both files) and re-run L-NODE + L-TAP and L-REQ; if the test is
   misnamed, it is its owner's defect: return `blocked` naming it.
3. L-APP, L-DEVFEED, L-MANUAL, L-GUARD, L-FIX, L-NODE + L-TAP, L-YAML, L-VLINT, L-LINT (`node tools/lint-serial-test-runs.js`).
4. L-INDEX: if red, run `node tools/gen-specs-index.js` once and re-check.
5. L-SCOPE; `git status --porcelain` shows no `src-tauri/binaries/` (git-ignored) and no untracked file outside Rule 11's
   set; `F:/tmp/km10-target/` is gone (Rule 10).
6. Greps (Rule 9, SC-026, SC-032): no `todo!(` / `unimplemented!(` outside `*_tests.rs` and `tests/`; no added `#[allow` /
   `#[expect` (`git diff 4a8b3e2a -- apps/krate-manager` plus the untracked files read whole); no `--test-threads`
   anywhere in the app, the workflow or the evidence scripts; no `inno-locked/` directory or `LOCKED_EXIT` /
   `LOCKED_LINE` constant (the M-1 fence); `classify::tests::provisional_rule ... ok` once in step 1's log; the five
   FR-123 needles of T002 one hit each in the roadmap.
7. The roadmap status line (TA-15) after `specs/Krate-manager-roadmap.md:711`, CRLF.
8. Not applicable, recorded with the reason: `node tools/check-portability.js` (no C++ file changed), `node
   tools/lint-plugin-editor-baseline.js` (no plugin editor changed), `run-gate.js` / `header-consumers.js` (no C++ test
   target), clang-tidy and pluginval (no plugin code).
9. Return with: notes "deferred to the main loop at close: run `node specs/krate-manager-phase10/evidence/close-lane.js`"
   (L-CLOSE, which runs L-WSL and L-BUNDLE; expected a few minutes: Phase 9's 169.1 s plus a helper release build and the
   first cold helper compile in WSL); the **M-1 fence report** (SC-032): open items FR-061, FR-062's measured rule,
   FR-102a, `classify::tests::recorded_locked_case`, `ops::tests::recorded_locked_case_through_runner`,
   `install::tests::recorded_locked_case_maps_to_locked`, `install::tests::unrecognised_log_falls_back`, SC-016's post-M-1
   half, roadmap FR-123 (b), all R1's; the PENDING-CI list below; and the user's next step: commit and push (optionally
   `workflow_dispatch` `manager-ci.yml`), after which R1 can run.

**Pass:** every step green or recorded not applicable; the hand-over notes. **Lane:** app lane + pipeline lane; L-CLOSE,
L-WSL and L-BUNDLE deferred to the main loop.

---

## PENDING-CI (macOS and Linux evidence, and every elevated row; only the user can trigger it)

Each `cargo test` row closes on a leg only when the run's committed `tests/required-tests.json` lists its names and that
leg's step `required tests ran` (`.github/workflows/manager-ci.yml:153-178`, as T018 extends it) passed with no
`::error`; the evidence is step `cargo test` (`:117-120`) on job `check (macos-latest)` / `check (ubuntu-24.04)` (matrix
`:28-34`). Elevated rows close on the named job's `::notice` annotations (Phase 4 D21). L-WSL is an early signal only
and closes none of them.

| # | SC / FR | Job(s) | Exact assertion |
|---|---|---|---|
| 1 | SC-001 to SC-007, SC-009 to SC-016 (pre-M-1), SC-022, SC-028 to SC-030 (`cargo.all`), SC-025 (vitest), SC-020 node rows | `check (macos-latest)`, `check (ubuntu-24.04)` (and `check (windows-latest)`) | the 328 `cargo.all` names, the 57 vitest titles and the 3 node names each pass exactly once: `::notice title=required tests ran (macOS)::377 cargo + 57 vitest + 3 node` (328 + 49), `(Linux)::371 cargo + 57 vitest + 3 node` (328 + 43), `(Windows)::345 cargo + 57 vitest + 3 node` (328 + 17) |
| 2 | SC-008 Unix (symlinked parent root, `/var` → `/private/var` on macOS) | both | `validate::tests::symlinked_parent_root_unix ... ok` |
| 3 | SC-005 Unix (final link and FIFO refused by the no-follow, non-blocking open) | both | `os::tests::open_source_refuses_links_and_fifos_unix ... ok` |
| 4 | SC-027 Unix (run directory mode `0o700`; the timed-out child not killed) | both | `os::tests::run_dir_mode_unix ... ok`, `os::tests::wait_timeout_leaves_child_unix ... ok` |
| 5 | SC-007 macOS (production roots) | `check (macos-latest)` | `roots::tests::production_roots_macos ... ok` |
| 6 | SC-024 macOS (native wiring reaches one launch with a pkg op) | `check (macos-latest)` | `install::tests::native_wiring_macos ... ok` |
| 7 | SC-017 (helper elevated, Windows) | `helper-elevated (windows-latest)` | a `::notice` each: token elevated; the stub's argv equals SC-002's Inno argv and its `current_exe` lies in `C:\Windows\Temp\run-*`; the run directory's SDDL has `P` among the `D:` flags, exactly `(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)`, no `ID`; `Fixture.vst3` under `$RUNNER_TEMP\real\root\VST3\Krate Audio\Fixture` and `{fixture AppId}_is1` in HKLM 64-bit after install; uninstall `done`, tree and key gone, `seconds` annotated; the link job accepted the canonical source and refused the linked one (`E_HELPER_SOURCE`); no `run-*` left; every `Start-Process` reported the helper's own exit code and a result file existed |
| 8 | SC-018 (helper elevated, macOS) | `helper-elevated (macos-latest)` | a `::notice` each: four fixture trees placed under `$RUNNER_TEMP/real/root`; the raw `--only-dirs` listing annotated and the structural directories kept; every fixture file gone; `pkgutil --pkgs` lacks `com.krateaudio.fixture`; `xattr -l` shows no `com.apple.quarantine`; the preinstall record (`$1` annotated) shows the run directory uid 0, mode `0700`, a directory under `/Library/Application Support/com.krateaudio.krate-manager-helper/`; the link job as on Windows; no `run-*` left |
| 9 | SC-016 measurement (M-1 input) | `inno-locked-file (windows-latest)` | every FR-060 value annotated: exit code, run time, within 600 s, old / new digests, each installed file's digest and class, `PendingFileRenameOperations` and whether it names the DLL, `DisplayVersion`, the matching log lines, the Inno version. Closes nothing by itself: R1 turns it into M-1 |
| 10 | SC-019 (release build carries no test roots; feature code linted and tested; the example linted) | `check (windows-latest \| macos-latest \| ubuntu-24.04)` | step `ci-temp-roots absent` `::notice` with `cargo tree` hits 0, `--ci-roots` in `cli.rs` ≥ 1, in the bundled and staged helpers 0 (Linux: the notice of D18); steps `clippy ci-temp-roots`, `test ci-temp-roots` (its log holds `cli::tests::ci_roots_file_is_read ... ok` exactly once), `clippy manual-elevation example` green |
| 11 | SC-020 (bundling) | `check (windows-latest)`, `check (macos-latest)`, `check (ubuntu-24.04)` | step `helper bundled`: Windows `7z l` lists `krate-manager-helper.exe` beside `krate-manager.exe` and the extracted SHA-256 equals the staged sidecar's (both annotated; TA-16, not checkable here); macOS `Contents/MacOS/krate-manager-helper` exists and step `codesign verify` passes; Linux `dpkg-deb -c` lists no helper |
| 12 | SC-026 (zero warnings, macOS / Linux) | both | steps `cargo fmt`, `clippy`, `clippy dev-feed`, `clippy ci-temp-roots`, `clippy manual-elevation example`, `typecheck`, `lint` green; `build warnings` `::notice … 0 warning lines in the release build` (it now also sees the helper's release build) |
| 13 | SC-031 (elevated jobs are path-gated) | any push | a push touching none of FR-102b's paths shows `helper-elevated` and `inno-locked-file` skipped; one touching `apps/krate-manager/crates/helper/` runs both; `check` runs on both |
| 14 | Checkout unchanged (no generated file left) | all three `check` legs | step `Checkout unchanged` `::notice title=checkout unchanged (<OS>)::clean` (`manager-ci.yml:299-307`); `src-tauri/binaries/` is git-ignored (T001) |

---

## R1 — Resume after the M-1 fence (BLOCKED: not part of this build)

### T020 — M-1, the measured `classify_inno` rule, the replay tests, FR-102a, and the Phase 10 close (spec FR-061, FR-062, FR-102a, FR-123 (b), (c); SC-016 post-M-1, SC-031 post-M-1, SC-032 resume half; plan §4.1.11, §8.2, SI-26)

**Precondition (else return `blocked` and change nothing):** the user names a `manager-ci.yml` run whose
`inno-locked-file (windows-latest)` job carries every FR-060 `::notice` (row 9 above), read through the jobs API and
check-run annotations (the token cannot read logs, `reference_gh_token_scope_no_logs`). If the measurement shows exit 0
with the DLL class `old`, a scheduled rename, or a hang, stop and return `blocked` for the user's re-ruling (FR-062).

**Files:** `specs/krate-manager-phase10/spec.md` (M-1), `tests/fixtures/helper/inno-locked/{exit_code.txt, log.txt,
seconds.txt, classes.json, locked_line.txt}` (SI-26; `.gitkeep` removed), `crates/helper/src/classify.rs`,
`classify_tests.rs`, `ops_tests.rs`, `crates/core/src/install_tests.rs`, `tests/required-tests.json`,
`manager-ci-shape.test.js` (`EXPECTED_REQUIRED` and the FR-102a shape), `.github/workflows/manager-ci.yml` (FR-102a),
`specs/Krate-manager-roadmap.md` (FR-123 (b) and the status line). Sequential, one task.

**Failing tests first:** `classify::tests::recorded_locked_case` (the fixtures → `Locked`; the same code without the line →
`Failed`; `LOCKED_LINE == locked_line.txt`; a variant with 500 rollback lines after the recorded line),
`ops::tests::recorded_locked_case_through_runner`, `install::tests::recorded_locked_case_maps_to_locked`,
`install::tests::unrecognised_log_falls_back` (plan §7.1 SC-016, `plan.md:1043`), red against the provisional rule.

**Implement:** M-1 written from the annotations (date, run id, every value, the verbatim lines); `LOCKED_EXIT` /
`LOCKED_LINE` and the rule of §4.1.11; `required-tests.json` + `EXPECTED_REQUIRED` swap `classify::tests::provisional_rule`
for the four names (`all` 328 → 331); FR-102a's three equalities in `inno-locked-file` against the committed files, each
mismatch an `::error`, and the matching shape-test rows; roadmap FR-123 (b) and the status line rewritten to the phase
close form. Then L-RS, L-NODE + L-TAP, L-YAML, L-REQ (`348 cargo + 57 vitest + 3 node`), and L-CLOSE (main loop).

---

## Defects found (in the plan, while writing this list)

| Id | Defect | Evidence | Resolution |
|---|---|---|---|
| D-1 | Plan §4.8.1 widens no program-start rule, but every `crates/core/` source that calls `Command::new(` with a program not in `TEST_PROGRAMS` (`["sleep", "printf"]`) or `TEST_PROGRAM_BODIES` is reported, a non-literal as `<not a literal>`; `windows_parameters_parse_back_windows` spawns `env!("CARGO_BIN_EXE_argv-echo")` | `injection.rs:497-520`, `:1988-2010` | TA-7: T001 adds a body-scoped exemption for exactly that form in that one `fn` of `crates/core/tests/windows_quoting.rs` |
| D-2 | Plan §4.8.1 gives `windows-sys` two homes in `READER_PINS`, but `ReaderPin` has one `home: (&str, &str)`; `HOST_LITERALS` likewise has one home per literal | `injection.rs:552-582`, `:217-227` | TA-7: T001 turns both into home lists, each with a plant |
| D-3 | Plan §4.8.1 places the `/usr/bin/killall` literal in `install_tests.rs`, and SC-025 asserts the `~/Library/Caches/AudioUnitCache` phrase in `status.test.ts`; both files are `FR088_FILES`, which may not contain `/usr/` or `/Library` | `injection.rs:253-276` | TA-12 |
| D-4 | Plan §4.1 marks cross-module helper items `pub(crate)`; as G1 placeholders they and their structs' fields warn `dead_code` at the zero-warning G1 gate | Phase 9 TA-2 (`specs/krate-manager-phase9/tasks.md:274`), same cause | TA-2: `pub` |
| D-5 | Plan §4.1.9 and §4.4 give the helper fakes' and the loopback's powers but no method names; six and two parallel writers need them | `plan.md:458-478`, `:711-720` | TA-3 (frozen in T001), TA-14 (T010's first save) |
| D-6 | `7z` is not on this box, so the local close cannot replay `helper bundled`'s NSIS extraction | `command -v 7z` printed nothing; `/c/Program Files/7-Zip/7z.exe` absent (this session) | TA-16: CI-only half, PENDING-CI row 11 |
| D-7 | Plan §4.8.1 lists `roots.rs` as a `windows_sys` home only because `production()` names `FOLDERID_*` | `plan.md:854-856`, `:378-379` | TA-6: `KnownFolder` in `os.rs`, so `roots.rs` holds no FFI and the home list is narrower |
| D-8 | Plan §2 names no `[[bin]]` keys for the helper; its default bin target would build a GUI-subsystem test harness with zero tests | `plan.md:178`, `:313`, `:323` | TA-10: `test = false` |
| D-9 | Plan §4.6 and §8.2 name `required tests ran`'s node input but not its notice; the shape test pins the step's lines | `plan.md:1116-1118`; `manager-ci-shape.test.js` `COMMANDS` | T018: the notice gains `+ <n> node`; L-REQ expects `345 cargo + 57 vitest + 3 node` |
| D-10 | Plan §1 / §4.6 says `scripts/*.js` are linted through `projectService`, but the project service reads only the nearest `tsconfig.json` (`include: ["src"]`), never `tsconfig.node.json`; the G2 gate's `npm run lint` failed 5x "was not found by the project service", and `eslint.config.mjs` is not a plan §2 file | `plan.md:147`, `:784-791`; `eslint.config.mjs:10`; `tsconfig.json` `include`; `@typescript-eslint/types` 8.72.1 `parser-options.d.ts:12-35` (`allowDefaultProject`, `defaultProject`) | G2 fixer: `eslint.config.mjs` gains a `files: ["scripts/*.js"]` block with `projectService: { allowDefaultProject: ["scripts/*.js"], defaultProject: "tsconfig.node.json" }` (5 files, under the default cap of 8); `scope-check.js` moves `eslint.config.mjs` from refused to allowed. No suppression; the scripts stay under `strictTypeChecked` |
| D-11 | (main-loop close lane, 2026-11-01) `wsl-lane.js` counted every line starting `test result:` as a cargo summary; the helper's `result` module prints `test result::tests::log_tail_bounds ... ok`, which has no `0 failed`, so the first close lane read L-WSL red although every WSL test passed | `evidence/wsl-lane.js:78`; `evidence/wsl-core.txt` lines 423, 441 | The summary filter is `/^test result: (ok|FAILED)\./`; the self-test green log carries that test line; close lane re-run green |
