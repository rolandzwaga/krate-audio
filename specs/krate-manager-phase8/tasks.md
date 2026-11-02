# Tasks: Krate Manager Phase 8 — Safe Extraction and Staging (Linux tar.gz)

**Spec:** `specs/krate-manager-phase8/spec.md` (DRAFT revision 3: D1-D9, FR-001 to FR-091, SC-001 to SC-031;
Clarifications Q1-Q8 of 2026-11-01, `spec.md:1031-1069`) · **Plan:** `specs/krate-manager-phase8/plan.md` (DRAFT
revision 2: PD-1 to PD-11 `plan.md:34-46`, SI-1 to SI-12 `plan.md:48-63`, "Not an issue" `plan.md:65-79`, the
precondition table R-1 / R-2 `plan.md:914-920`, review notes `plan.md:923-950`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 8 (lines 634-659); Phase 9, the consumer (lines 661-703);
Cross-Cutting Constraints (lines 982-1023)
**Status:** DRAFT task list, revision 1, 2026-11-01. Written against `feat/krate-manager` at `2499fb13` (`git rev-parse
--short HEAD` this session; `git status --porcelain` shows only `?? specs/krate-manager-phase8/`; the last commit touching
`apps/krate-manager/` is `2499fb13` itself). Every app file:line quoted below was re-read this session at `2499fb13`.
**Spec state:** none of plan §0.2's edits is in `spec.md` yet. Counted this session with `grep -c` over `spec.md`:
`read_archive` 0, `mklink` 0, `TwoPhaseArchive` 0, `U+FEFF` 0, `kind_of(extract)` 0, `symlink_file` 0, `cargo update
--workspace` 0 (the one `PD-1` hit is Phase 7's). The plan calls SI-1 to SI-4 blocking (`plan.md:15-20`) because the
spec, *as text*, would build a red tree; every task section below states its assertions from the plan's §0.2 / §4 / §7.1
rows, which are binding, so no build task waits for the spec text. T002 writes the edits into `spec.md` in G2, in
parallel with the code (the Phase 7 precedent, `specs/krate-manager-phase7/tasks.md` T001).
**Lanes in this phase:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm
typecheck / lint / test, the version lint, the `cargo tree` record), the **pipeline lane** (`node --test` over
`tools/feed-contract/tests/`, because `manager-ci-shape.test.js`'s `EXPECTED_REQUIRED` changes; the repo lints) and
**L-WSL** (core clippy + tests in WSL Ubuntu: the only local compile of the `cfg(unix)` code; a main-loop lane). No file
under `plugins/`, `dsp/`, `cmake/`, `extern/` or `.github/workflows/` changes (spec FR-091, `spec.md:568`; plan §2
`plan.md:157-158`), so there is **no C++ lane**: no `run-gate.js`, no `header-consumers.js`, no `check-portability.js`,
no clang-tidy, no pluginval, no `lint-plugin-editor-baseline.js` (no plugin editor changes); and **no workflow edit**:
`manager-ci.yml` already runs every step this phase needs on all three OSes (plan §8 `plan.md:881-883`), so the per-OS CI
additions are the `required-tests.json` lists (T007). No `src-tauri` or webview change (FR-090), so `tauri build` is not
re-run locally (TA-11). L-WSL and the full close procedure are main-loop lanes (T006 writes their scripts, T008 hands
them over).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks at the same time in one working tree. A task is
  **[P]-safe** when no other task in its group touches any of its files and none of its files is a **shared file**:
  any `Cargo.toml`, `Cargo.lock` (the workspace's and `tests/fixtures/clippy-guard/Cargo.lock`), `clippy.toml`,
  `crates/core/src/lib.rs`, anything under `src-tauri/`, a capability file, `tauri*.conf.json`, `package.json`, a
  workflow, `tests/required-tests.json`, `manager-ci-shape.test.js`, the roadmap or `specs/INDEX.md`. **Every app-side
  shared file is edited in one task only, T001, which is G1 on its own (sequential)**: it writes the `pub mod extract;`
  line, both manifests, the clippy-guard lock, the three one-line visibility / helper changes in existing core files,
  the public surface of `extract.rs` that other G2 tasks compile against (with `todo!()` bodies), the public surface of
  the Phase 8 test doubles in `test_support.rs` (with `todo!()` bodies), and every existing guard expectation those
  edits move. The G2 tasks then touch only their own files: a G2 file is either new in this phase (created by T001 as a
  placeholder for exactly one G2 task, or created by that task) or an existing non-shared file that exactly one G2 task
  edits (`spec.md` → T002; `extract.rs` + `extract_tests.rs` → T003; `test_support.rs` + `test_support_tests.rs` →
  T004; `tests/injection.rs` → T005; the three evidence scripts → T006). `required-tests.json` and the shape test are
  touched only by T007 (G3).
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block is too long to copy without drift (a type
  list, a test table, an algorithm), the section names the `plan.md` section or §7.1 row, or the spec FR / SC line
  range, that holds it; that text is then **binding, verbatim**, as amended by plan §0.2 (`plan.md:48-63`) and this
  file's TA rows. Where this file and the plan differ, this file wins and the TA row says why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the notes quote the failing line. The red
  run normally uses T001's placeholder, so the test compiles and fails on the `todo!()` panic ("not yet implemented") or
  on its assertions. A test that cannot run on this box (`cfg(unix)`, `cfg(target_os = "macos")`) has no local red: its
  red-first is recorded as "not compiled here", and it is read twice against its SC before the task ends (Rule 15).
- **In-group expected reds.** In G2, T003's rows that run over a test double (`MemExtractIo`, `RecordingExtractIo`,
  `FaultyExtractIo`, `snapshot`) fail with `not yet implemented` raised **from `crates/core/src/test_support.rs`** until
  T004 lands; that is the only failure text that counts as expected, and it is listed again in T003. **Every such red is
  gone by the G2 gate** (all of G2 has landed by then); any red at a gate is a real red. No red is carried across a group
  boundary. A **compile error is never an expected red**: every core unit test lives in one test binary, so a file that
  does not compile stops every G2 task's loop (Rule 10).
- **Main-loop lanes.** Steps longer than ~5 minutes, or that need WSL, run only in the main loop, from scripts T006
  writes: `specs/krate-manager-phase8/evidence/wsl-lane.js` (L-WSL; Phase 7 measured `WSL LANE GREEN (260 passed, 40
  s)`, 46.3 s wall, `specs/krate-manager-phase7/evidence/timings.md` row A10; this phase's first WSL run compiles the new
  module and the `cfg(unix)` code cold) and `specs/krate-manager-phase8/evidence/close-lane.js` (L-CLOSE: the plan §7.4
  procedure, L-WSL and the records; Phase 7's took 157.0 s including a 102.5 s `tauri build` that this phase does not
  run, TA-11). T008 returns with notes "deferred to the main loop at close: run `node
  specs/krate-manager-phase8/evidence/close-lane.js`".
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system.** Every test builds its roots from `test_support::fresh_temp_roots()`
   (`crates/core/src/test_support.rs:140-144`, returns `(TempDir, Roots)` sandboxed to the `TempDir`) or from one of
   FR-089's constructors (`temp_roots_via_link`, `temp_roots_raw`, T004) on a fresh `TempDir`, and writes only inside
   that `TempDir`; paths outside the staging dir a test needs (a `canary/` directory, a link target) are built with
   `Path::join` from the same base. The `AppContext` is a literal over those roots (the `download_tests.rs:148-156`
   pattern: `Settings::default()`, `HttpConfig::loopback(…)`, a `https://feed.example.invalid/…` URL that is never
   fetched, `signing_key: kat_signing_key()` or a `TestSigner` key). **Every refusal row stages over
   `RecordingExtractIo::fail_closed()`** (FR-087, `spec.md:538-547`): an archive naming `/etc/evil`, `C:\evil` or
   `\\server\share\evil` can create nothing and open no SMB connection; only rows whose named paths all resolve inside
   the row's own `TempDir` also run over `RecordingExtractIo::forwarding(&FsExtractIo)`. No test opens a socket, calls
   `github.com`, `githubusercontent.com`, `krateaudio.com` or `api.github.com`, acquires the real state lock, reads or
   writes a real VST3 / Components / preset folder, `ProgramData`, the registry or the real state dir. The only
   processes a `crates/core` test starts are `sleep` / `printf` (`TEST_PROGRAMS`, `crates/core/tests/injection.rs:392`),
   the four existing TA-1 programs in their four bodies (`injection.rs:396-409`) and, after T001, `/usr/bin/xattr` in
   exactly `extract::tests::staged_tree_has_no_xattrs_macos` and `cmd` (for `mklink /J`, both paths inside the test's
   `TempDir`) in exactly `extract::tests::symlink_entry_unsupported_windows` (plan SI-1, SI-10). No test calls
   `std::env::temp_dir`, `var`, `var_os`, `home_dir` (`apps/krate-manager/clippy.toml`). No private key is written to
   any file (FR-084): `TestSigner` keys live in memory, the KAT key is read in place. Committed fixtures under
   `tools/feed-contract/fixtures/` are read in place and never modified or copied into the repo (FR-085); a test copies
   one into its `TempDir` when it must sign or verify it there. No task launches the built app, the elevated helper or
   an installer.
2. **No lint suppression added, of any lint.** `[workspace.lints.clippy] allow_attributes = "deny"`,
   `allow_attributes_without_reason = "deny"` (`apps/krate-manager/Cargo.toml:50-52`): never `#[allow]`. This phase adds
   **no `#[expect(…)]`** either (SC-001, `spec.md:588-593`). No new `clippy::disallowed_methods` site: `D6_SITES` /
   `D6_TOTAL` stay at 6 files / 8 sites (`injection.rs:92-100`); T004's `roots_at` refactor *moves* the existing
   `#[expect(clippy::disallowed_methods, reason = "temp_roots is the test constructor")]` (`test_support.rs:110-113`) with
   `Roots::new` and keeps its reason text, so the per-file count of `test_support.rs` stays 2. No `// @ts-ignore`, no
   `eslint-disable`. A warning is fixed in the code, never silenced.
3. **Unit tests live in `<stem>_tests.rs`**, declared at the end of `<stem>.rs` as `#[cfg(test)] #[path =
   "<stem>_tests.rs"] mod tests;` (an inline `mod tests {` is reported, `injection.rs:1182-1187`).
4. **Literal hygiene.** (a) `extract.rs` holds, comments included, none of `FORBIDDEN_OUTSIDE_PLATFORM`
   (`injection.rs:23-31`: `"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`, `krateaudio.com`, `api.github.com`), no
   `release.json` literal (FR-034), and no `Display` of a `Path` (FR-052, plan §4.3.12). (b) **`test_support.rs` is in
   `FR088_FILES`** (`injection.rs:245-252`), so nothing T001 or T004 adds there, comments included, may match any
   `FR088_PATTERNS` entry (`injection.rs:254-266`: `--test-threads`, `127.0.0.1:<port>`, `localhost:<port>`,
   `OsHostIo`, `HKEY_`, `/Library`, `/usr/`, `%NAME%`, `AppData`, `ProgramData`, `Program Files`) (TA-7). (c) The token
   `sandboxed` appears only in `roots.rs` (`injection.rs:1210-1212`); call `ctx.roots.check_sandboxed(…)`. (d) **No file
   under `apps/krate-manager/` names any of the nine shipped plugins** (`crates/core/tests/roster.rs`
   `roster_and_hosts_absent_from_sources`, `:130`): invented names `Alpha`, `Bravo`, … only; the KAT's `KatPlugin` is
   read from `tools/feed-contract/fixtures/kat/` in place. (e) `TcpStream`, `TcpListener`, `ureq`, `rustls` stay in
   their files (`CONFINED_TOKENS`, `injection.rs:270-293`).
5. **Zero warnings.** `cargo fmt --all --check`; every `cargo clippy … -- -D warnings` with 0 `warning:` lines in both
   the lib and the test builds; `npm run typecheck`; `npm run lint`; `node --check`.
6. **Parallel tests.** Never `--test-threads`, `--no-file-parallelism`, `--fileParallelism=false`, `--maxWorkers`, `-j
   1`, `--test-concurrency=1`. `cargo test` uses its default threads; `vitest run` its parallel files; `node --test` runs
   files in parallel. No new test holds a process-wide mutex or a `static` lock (the static-lock rule,
   `injection.rs:1938-1967`, covers test sources; `test_support.rs` keeps the same discipline, plan §4.5), a fixed port,
   or a sleep; SC-024's ordering comes from a `std::sync::Barrier` pair local to the test (plan §7.1 SC-024).
7. **`cargo test` is `cargo test --workspace --locked` or `cargo test -p krate-manager-core --locked …`, never with
   `--all-targets`** (that builds a harness for `src-tauri`'s lib and bin despite `test = false`; Phase 4 ruling P-4).
   `cargo clippy --workspace --all-targets` is correct. `cargo test --workspace` also runs `extract.rs`'s doctests
   (SC-016).
8. **Lockfiles.** Only T001 changes `tests/fixtures/clippy-guard/Cargo.lock` (one added line, TA-1); **nothing changes
   `apps/krate-manager/Cargo.lock`** (it already lists `flate2` under `krate-manager-core`, `Cargo.lock:2049-2070`,
   dev-dependencies included; plan §4.1). Every other cargo command in every task uses `--locked`. **No task adds a
   dependency or a feature**: a task that finds it needs one stops, returns `blocked` and names the crate and why.
9. **Placeholder discipline (TA-2, TA-3).** T001's placeholders compile with zero warnings in the lib build and the test
   build: every parameter of a `todo!()` body is `_`-prefixed; a placeholder struct whose real fields belong to a G2
   task holds only `_private: ()` (or `_private: std::marker::PhantomData<&'a ()>` when it carries a lifetime); a
   placeholder imports only what it uses; every `todo!` names its owner (`todo!("T003")`, `todo!("T004")`). The owning
   task renames parameters, replaces `_private` and removes the `todo!`s as it implements. At close no `todo!(` or
   `unimplemented!(` remains in any workspace `.rs` file outside `*_tests.rs` and `tests/` (T008 step 4).
10. **One working tree, many writers (G2).** Write each file whole, so that it compiles at every save (types and
    signatures first, bodies after; a test that names an item you have not written yet is saved only after the item).
    Never edit a file another task owns; when `cargo` fails on a file you do not own, wait 60 s and retry, three times,
    then record it in your notes and continue with what you can verify. Run your own-module loop (L-OWN) in the
    **target slot** your task names, with its `CARGO_BUILD_JOBS` (TA-9): the shared `apps/krate-manager/target` for
    T003 (16 jobs, warm from T001, the long pole); slot `F:/tmp/km8-target/side` for T004 and T005 (12 jobs; the two
    queue on its build lock, which is accepted). The first run in the slot pays one cold build of core's dependency graph;
    record its wall clock. Before the first build run `df -h /f` (6.2 GB free this session): with less than 4 GB free,
    use the shared target instead of the slot and say so in the notes. The last of T004 / T005 to finish deletes
    `F:/tmp/km8-target/` (check `ls F:/tmp/km8-target/` and the other task's state first).
11. **Scope fence (spec FR-090, FR-091, `spec.md:567-571`; plan §2 `plan.md:139-158`).** Outside `apps/krate-manager/**`
    only these paths change: `specs/krate-manager-phase8/**`, `specs/INDEX.md` (regenerated only, T008),
    `tools/feed-contract/tests/manager-ci-shape.test.js` (`EXPECTED_REQUIRED` only, T007), and in
    `specs/Krate-manager-roadmap.md` only the Phase 8 status line (main loop at close). Inside `apps/krate-manager/` the
    only files that change are plan §2's: `Cargo.toml` (one comment line), `crates/core/Cargo.toml`,
    `crates/core/src/{lib,extract,extract_tests,verify,verify_tests,contract,detect,test_support,test_support_tests}.rs`,
    `crates/core/tests/injection.rs`, `tests/fixtures/clippy-guard/Cargo.lock`, `tests/required-tests.json`
    (`verify_tests.rs` and `test_support_tests.rs` gain one test each, TA-6). No change to `Cargo.lock`, `src/`,
    `src-tauri/**`, `index.html`, `package*.json`, `crates/helper/**`, `clippy.toml`, `eslint.config.mjs`,
    `vite.config.ts`, `tsconfig*.json`, `CHANGELOG.md`, `version.json`, or any workflow.
12. **Line endings.** This checkout runs `core.autocrlf=true` (`git config core.autocrlf` = `true` this session).
    Measured this session (`tr -cd '\r' < f | wc -c` against `wc -l`): **CRLF** — `apps/krate-manager/Cargo.toml` 59/59,
    `Cargo.lock` 5438/5438, `tests/fixtures/clippy-guard/Cargo.lock` 5337/5337, `crates/core/Cargo.toml` 49/49,
    `crates/core/src/lib.rs` 28/28, `crates/core/src/test_support.rs` 587/587, `crates/core/tests/injection.rs` 4894/4894,
    the roadmap 1059/1059; **LF** — `crates/core/src/verify.rs` (0/354), `verify_tests.rs` (0/514), `detect.rs` (0/1071),
    `contract.rs` (0/1066), `test_support_tests.rs` (0/421), `tests/required-tests.json` (0/330),
    `tools/feed-contract/tests/manager-ci-shape.test.js` (0/1010), `specs/krate-manager-phase8/spec.md` (0/1069). An edit
    keeps the file's ending on every line it adds. New files are written LF. A file `cargo` rewrites (the clippy-guard
    lock) is checked with `git diff --numstat` (autocrlf normalises the ending), never by a byte compare.
    `specs/INDEX.md` is written only by `tools/gen-specs-index.js`.
13. **Shell discipline.** Every command runs in the Bash tool (Git Bash), foreground, output to a log under
    `F:/tmp/gate/km8-<task id>/`, then `tail`; never `cat` a whole log, never a drive-wide `find` / `du`. **No heredoc in
    a Bash command** (a repo hook blocks them): write scripts with the Write tool and run them with `node`. Never chain
    git commands. All app-lane commands run from `apps/krate-manager/`; repo-lane commands from the repo root.
14. **No invented APIs.** Every crate API used is one plan §1.2 verified (`plan.md:125-135`) or one listed here:
    `flate2` 1.1.10 `bufread::GzDecoder::{new, into_inner, get_mut}` (registry source `src/gz/bufread.rs:238`, `:284`,
    `:289`; https://docs.rs/flate2/1.1.10/flate2/bufread/struct.GzDecoder.html) and, in tests only,
    `write::GzEncoder::{new, finish}` with `Compression` (`src/gz/write.rs:70`, `:148`;
    https://docs.rs/flate2/1.1.10/flate2/write/struct.GzEncoder.html); `tempfile` 3.27.0 `Builder::{new, prefix,
    tempdir_in}` and `TempDir::keep(self) -> PathBuf` (`src/lib.rs:326`, `:621`; `src/dir/mod.rs:418`;
    https://docs.rs/tempfile/3.27.0/tempfile/struct.Builder.html); std 1.99.1 (`fs::{create_dir, create_dir_all,
    read_dir, symlink_metadata, canonicalize, remove_dir_all, remove_file, set_permissions, read_link}`,
    `OpenOptions::create_new`, `os::unix::fs::{symlink, PermissionsExt, MetadataExt}`, `io::Seek::rewind`,
    `OsStr::as_encoded_bytes`, `thread::scope`, `sync::Barrier`; https://doc.rust-lang.org/std/). `regex` is reused through
    `contract::reserved_stem_re()` only. An API that turns out to differ is a defect of the plan: record it under
    "Defects found" with the crate source line, and use the real API.
15. **`cfg` discipline.** The five `_unix` tests and the `#[cfg(unix)]` blocks of `kat_archive_stages`,
    `entry_path_rejects_every_bad_form` (`cfg(not(windows))`) and `phase8_doubles_cases` compile only in L-WSL and CI;
    the two `_macos` tests and `temp_roots_raw` only on the macOS CI leg (plan §7.3, §4.9). Write them with the same
    care as the Windows code, read each against its SC row twice, and gate every `use` the way its only users are gated
    (an unused import is a warning on the leg that compiles it). `FsExtractIo`'s `cfg(unix)` and `cfg(not(unix))` bodies
    (plan §4.3.2 table, `plan.md:312-327`) must both compile: no binding unused on either side. The Windows-only
    `remove_dir_all` fallback of `remove_entry` (PD-7) is `#[cfg(windows)]`.
16. **Every claim cites what was read.** A task's notes cite file:line for every fact about existing code it relied on,
    read in that task's session, and quote the test output lines (`test result:` and the named tests) that close it.

### Lanes (referenced by every task)

| Lane | Commands | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked 2>&1 \| tee F:/tmp/gate/km8-<task>/cargo-test.txt` (Rule 7; this also runs the doctests) | fmt exit 0; clippy exit 0 with 0 `warning:` lines; every test binary and the doctests `0 failed` |
| **L-OWN** | `CARGO_TARGET_DIR=<slot> CARGO_BUILD_JOBS=<n> cargo test -p krate-manager-core --locked --lib <filter>` (or `--test injection` for T005, `--doc` for T003's doctests) and `CARGO_TARGET_DIR=… CARGO_BUILD_JOBS=… cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings` (Rule 10; a working loop, not a gate). `<slot>` per Rule 10; for T003 unset (shared target) | the task's own tests and 0 `warning:` lines |
| **L-APP** | `CARGO_BUILD_JOBS=16 cargo clippy -p krate-manager --all-targets --locked -- -D warnings` (shared target) | exit 0, 0 `warning:` lines |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (`.github/workflows/manager-ci.yml:97-115`), extracted into a file by a throwaway Node script (Rule 13) and executed with `"C:/Program Files/Git/bin/bash.exe"` from `apps/krate-manager/` with `RUNNER_TEMP=F:/tmp/gate/km8-<task>` and `RUNNER_OS=Windows` | a `::notice title=clippy-guard (Windows)::rc=<non-zero> disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=<≥1> other-errors=0` line (Phase 7's reading: `rc=101 … lint-named=5`, `specs/krate-manager-phase7/evidence/timings.md` row A1d), no `::error`; a `--locked` refusal ("the lock file … needs to be updated") is red |
| **L-TS** | `npm run typecheck`; `npm run lint`; `npm test` | each exit 0 (the frontend is unchanged, FR-090: 29 vitest titles pass) |
| **L-NODE** | `node --check <each new or changed .js>`; `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km8-<task>/all.tap` from the repo root (quoted glob); read with `tail -12` | TAP `# fail 0`; no `Warning:` / `DeprecationWarning` / `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km8-<task>/all.tap` | exit 0 and `TAP plan=1..59 top-level=59` (only `EXPECTED_REQUIRED`'s data changes, no new `node:test` case; `.github/workflows/feed-tools.yml:86` `PLAN = "1..59"`) |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root) | exit 0, no stderr |
| **L-INDEX** | `node tools/gen-specs-index.js --check` (repo root) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase8/evidence/scope-check.js` (T006) | exit 0 |
| **L-WSL** (main loop) | `node specs/krate-manager-phase8/evidence/wsl-lane.js` (T006; TA-10): in WSL, `CARGO_TARGET_DIR=$HOME/km-target` (Phases 5-7's), `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings && cargo test -p krate-manager-core --locked` | last line `WSL LANE GREEN …` and the five `_unix` names of FR-086 seen `ok`; it never closes a PENDING-CI row (plan §7.4 `plan.md:871-873`) |
| **L-CLOSE** | `node specs/krate-manager-phase8/evidence/close-lane.js` (T006; **main loop only**) | last line `CLOSE LANE GREEN` |

**Group gates** (run by the workflow's gate reporter). After G1: L-RS (nothing else executing), L-APP, L-DEVFEED,
L-GUARD, L-TS, L-VLINT. After G2: L-RS, L-APP, L-DEVFEED, L-GUARD, L-TS, L-VLINT, L-SCOPE, `node --check` and
`--self-test` on T006's three scripts, and L-WSL beside it (main loop, WSL's own cores: the first Linux compile of the
module and of every `cfg(unix)` test). After G3: everything T008 lists, then L-CLOSE (main loop).

---

## Preconditions — rulings before the build stage

Plan §10 (`plan.md:914-920`) leaves two decisions for the user, each with a recommendation; this file is written to
both recommendations:

- **R-1 (PD-1): open, recommendation applied.** Pass 1 defers per-entry rule failures so the first code equals
  `listing.js`'s on every archive (two `Option<ExtractError>` slots; SI-5's three two-defect rows in SC-006). T003
  implements it (plan §4.3.6 `plan.md:441-458`).
- **R-2 (SI-4): open, recommendation applied.** SC-024's `FsExtractIo` variant stages `ustar-prefix` (no link) on every
  OS, and SC-027 stages `exec-and-symlink` over `MemExtractIo` and `real-shaped` over `FsExtractIo`: one test body, no
  `cfg` branches, no lane change. T003 implements it; T002 writes it into SC-024 / SC-027.

Starting the build stage after the user's review of spec, plan and tasks is their acceptance of both unless they
overrule one.

**Precondition check (T001 runs it first, T002 too; on failure the task changes nothing and returns `blocked`, quoting
the item):** in `spec.md` `## Clarifications` (`spec.md:1031-1069`), a bullet that rules **against** R-1's or R-2's
recommendation, or that marks any plan SI row or any TA row of this file rejected. (Absence of a bullet is acceptance.)
The main loop then re-plans the named sections and regenerates the affected tasks.

### Task-list amendments (TA)

Rows marked **spec** change spec text (T002 writes them, tagged `(tasks TA-n)`); the others explain why a task differs
from a plan block.

| Id | Amendment | Why | Spec? |
|---|---|---|---|
| TA-1 | **The clippy-guard fixture is re-locked with `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 > /dev/null`** (from `apps/krate-manager/`), not plan §4.1's / SI-3's `cargo update --workspace --manifest-path …` (`plan.md:233`, `:54`). The expected result is exactly one added line, `"flate2",`, in `krate-manager-core`'s dependency array (`tests/fixtures/clippy-guard/Cargo.lock:2022-2039`, between `"core-foundation",` and `"log",`), checked with `git diff --numstat -- tests/fixtures/clippy-guard/Cargo.lock` = `1	0` and no `[[package]]` added (`flate2` 1.1.10 is already locked there, plan SI-3) | the workflow's ruled re-lock command (it fixed the L-GUARD `--locked` refusal of Phases 5 and 6, commit `2f3c93d7`; Phase 7 TA-5); `cargo metadata` resolves the fixture's missing entry and changes nothing it already has; the fixture reaches core through a path dependency (`tests/fixtures/clippy-guard/Cargo.toml:6-8`), whose dev-dependencies it does not lock, so moving `flate2` to `[dependencies]` **will** change it (SI-3) | spec: FR-080 (SI-3's text names this command) |
| TA-2 | **Foundation placeholders (Rule 9).** T001's `extract.rs` holds only the items another G2 task compiles against: `NodeKind`, `ArchiveRead`, `ExtractFile`, the `ExtractIo` trait (plan §4.3.2 `plan.md:282-306`, verbatim), `FsExtractIo` with a placeholder `impl ExtractIo for FsExtractIo` (14 `todo!("T003")` bodies; `supports_symlinks` too), `EntryType`, `ListingEntry` (plan §4.3.5 `plan.md:407-410`) and `CanonicalListing { lines: Vec<String> }` with its four accessors `lines`, `text`, `files`, `digest` **real** (no constructor; `from_entries` is T003's). `digest` is the one reader of the new `pub(crate) verify::Sha256Stream`, which would otherwise warn `dead_code` at the G1 gate. Everything else of plan §3 and §4.3 (errors, limits, env, the reader, the rules, `plan_archive`, `stage`, `prune`, `StagedTree`, `discard`, the doctests) is written by T003 | a `pub(crate)` item without a reader, or a private placeholder item, warns `dead_code` at the zero-warning G1 gate (Phase 7 D-2, same cause); the doubles (T004) and the guards (T005) need only these names | no |
| TA-3 | **The test-double API is fixed by T001** (plan §4.5 `plan.md:613-683` names the types and their powers but not their constructors, builders or accessors). The exact signatures are T001 step 7. Notable choices: builder methods are named `with_failure`, `with_remove_tree`, `with_read_fault` (an inherent `remove_tree(self, …)` would be picked over the trait method `ExtractIo::remove_tree(&self, …)` on a value, and plan §4.5's struct fields would make `FaultyExtractIo` constructible only by field literal); `MemExtractIo::new(base)` plants `base` (the test's staging dir) as a directory and treats every proper ancestor of `base` as an existing directory that canonicalises to itself, so the work-dir checks of plan §4.3.10 step 8 run over Mem; `plant_dir` / `plant_file` / `plant_link` / `node` / `paths_under` are the "builder methods to plant nodes … accessors to read nodes back" of `plan.md:639-640`; Mem's `create_work_dir` names `<prefix><6-digit zero-padded counter>` (`000001`, …: six alphanumerics, so PD-8's name check holds) | T003 writes its tests against these names while T004 writes their bodies; an unnamed API would be a compile error between two parallel writers | no |
| TA-4 | **`'static` handles and `Sync` wrappers.** `ExtractIo::create_file` returns `Box<dyn ExtractFile>` and `open_archive` `Box<dyn ArchiveRead>`, both `'static` by elision, so a Mem file or a recording file cannot borrow its creator: `MemExtractIo` keeps its tree and its call record in `Arc<Mutex<…>>` (plan §4.5 says `Mutex<MemFs>`, `plan.md:634`), and `RecordingExtractIo` / `FaultyExtractIo` keep their record and counters in `Arc<Mutex<…>>`. Their `inner` is `&'a (dyn ExtractIo + Sync)` (plan: `&'a dyn ExtractIo`, `plan.md:649`), so the wrappers are `Sync` and SC-024's scoped threads can share one (`&dyn ExtractIo` alone is not `Send`). Every override closure is `Box<dyn Fn(…) + Send + Sync + 'a>` (as `plan.md:664-665` already says). No `static` anywhere | without them `create_file`'s recording file and SC-024 do not compile | no |
| TA-5 | **T004 owns `roots_at`, `temp_roots_via_link`, `temp_roots_raw` and the `snapshot` body.** T001 declares `snapshot` (every OS's tests call it) but not the two FR-089 constructors, which only `cfg(unix)` / `cfg(target_os = "macos")` tests call: no Windows build in G2 needs them, and declaring them in G1 would add two `todo!()`s that only L-WSL and CI compile | keeps G1 short; T003's `_unix` / `_macos` tests are first compiled at the G2 gate's L-WSL, after T004 has landed | no |
| TA-6 | **Two tests outside `required-tests.json`:** `verify::tests::sha256_stream_cases` (T001, proves `Sha256Stream`) and `test_support::tests::phase8_doubles_cases` (T004, proves the doubles' non-delegating paths). The required totals stay plan §4.7's: `all` 248, `windows` 11, `macos` 35, `linux` 30, `vitest` 29 (`plan.md:701-703`) | Phase 7 TA-2 / TA-3 precedent (`test_support::tests::phase7_helpers_cases` is not required either); FR-086 lists SC test names only | no |
| TA-7 | **`test_support.rs` text obeys FR-088's eleven patterns** (Rule 4b): `test_support.rs` is one of `FR088_FILES` (`injection.rs:245-252`), checked by `test_sources_hold_no_real_location` (`injection.rs:4229-4240`); the plan's §4.5 additions say nothing about it, and a doc comment such as "the raw `/usr/…` path" would turn that test red | a guard the plan did not list | no |
| TA-8 | **Guard rows move with the code that needs them.** T001: `FR007_DEPENDENCIES` 13 → 14 with `"flate2"` (`injection.rs:106-120`), the dev set of `core_dependencies_are_the_fr007_set` → `{minisign}` (`:3360-3364`), `const EXTRACT_TESTS_RS` beside `DOWNLOAD_TESTS_RS` (`:150`), `TEST_PROGRAM_BODIES` 4 → 6 (`:396-409`, plan SI-1 / SI-10), and `EXTRACT_TESTS_RS` appended to the `verify_file` callers (`:2579-2583`, FR-088). T005: `const EXTRACT_RS`, `CONFINED_TOML_KEYS` 12 → 13 (`:299-318`) and every new rule and plant of plan §4.6 | the G1 gate runs the guard binary against T001's manifest; an unused `const EXTRACT_RS` in the `injection` test crate would warn `dead_code` until T005's rules read it; the TA-1 program rule checks only that a `Command::new` literal sits in a listed body, so the two rows are safe before the tests exist (Phase 7 TA-7) | no |
| TA-9 | Rule 10's target slots: shared target for T003, one slot `F:/tmp/km8-target/side` for T004 and T005 | three G2 tasks build core; `df -h /f` shows 6.2 GB free this session and `apps/krate-manager/target` already holds 13 GB (`du -sh`); one slot is one cold build (~1.5 GB, Phase 7 TA-9's measure) and leaves T003, the long pole, on the warm shared target | no |
| TA-10 | L-WSL runs the full `cargo test -p krate-manager-core --locked` (Phase 7's form) instead of plan §7.4's `extract::` filter (`plan.md:871-873`), and its information line counts the five `_unix` names of FR-086 (`spec.md:530-532`) | the guard binaries lex sources and walk the locks, which can differ on Linux (`cfg(not(windows))` `rquickjs` graph); the full run is a superset at about 40 s warm (Phase 7 row A10) | no |
| TA-11 | **No local `tauri build`** in L-CLOSE (Phase 7 ran it as rows B2-B7). Phase 8 changes no `src-tauri` file, no webview file and no capability (FR-090); the plan's lanes re-run `tauri build` only as part of the unchanged CI leg (`plan.md:12-13`), so its evidence is PENDING-CI row 1-3's `tauri build` / `build warnings` steps | a 100 s release build that cannot see this phase's code (nothing in `src-tauri` calls `extract`) | no |
| TA-12 | **The `flate2` TOML plant goes in `ureq_is_confined_to_http_rs`** (`injection.rs:3481`), beside the existing `ureq` TOML plant (`:3571-3584`, `replace_in(&manifests, "src-tauri/Cargo.toml", …)` then `assert_reported(…, RULE_TOML_KEY, …)`), not in `injection_scanner_reports_planted_violations` as plan §4.6 says (`plan.md:691`) | `grep -n RULE_TOML_KEY injection.rs` this session: `:499` (the const), `:1700` (the rule), `:3583` (the only plant) — `injection_scanner_reports_planted_violations` (`:3166`) holds no TOML plant (D-1) | no |
| TA-13 | **No blanket impl of `ArchiveRead` or `ExtractFile`.** `extract.rs` implements them only for its own private types or `std::fs::File`; `test_support.rs` only for its own types (`TwoPhaseArchive` and private ones) | two parallel writers implementing a local trait for the same foreign type (for example `io::Cursor<Vec<u8>>`) would be a coherence error (E0119) that neither sees alone | no |

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | **sequential** (1) | T001 precondition, the two manifests and the clippy-guard lock, `pub mod extract;`, `verify::Sha256Stream`, the two visibility changes, the public surface of `extract.rs` and of the test doubles, the guard rows those edits move | Preconditions |
| G2 Spec, module, doubles, guards, scripts | **[P]** (5) | T002 spec amendments · T003 `core::extract` + its 32 tests + doctests (the long pole) · T004 the Phase 8 test doubles and FR-089's constructors · T005 `injection.rs` guards · T006 evidence scripts | G1 |
| G3 Integration | sequential (2, dependent) | T007 `required-tests.json` + the shape test (the per-OS CI additions) and the local `required tests ran` step · T008 INDEX, lints, scope, greps, the PENDING-CI list, hand-over of L-CLOSE | G2 |

**Why this shape.** T001 is the only task that edits shared files, so it is a group of its own; it fixes every shared
line and every name a second G2 task compiles against, so G2 holds the spec, the module, the doubles, the guards and the
scripts side by side (five at once). The long pole is `extract` (T003), which cannot be split across writers: every SC
test is named `extract::tests::*` in one module over one file (FR-086 `spec.md:509-536`, plan §7 `plan.md:784`), so
there is one `extract_tests.rs`; and the guards pin single-file facts of `extract.rs` (FR-082: `flate2` in non-test code
only in `extract.rs`; FR-083: the mutating tokens only inside `impl ExtractIo for FsExtractIo`, `create_dir_all` and
`tempfile` only in its `fn create_work_dir`; `StagedTree {` only there; plan §4.6), so a second production file would
change the spec's guard text, not add parallelism. What can leave T003 has left it: the doubles (T004), the guards
(T005), the spec text (T002) and the scripts (T006) run beside it, and T001's API freeze means none of them waits for
T003 to compile. The in-group dependencies run one way only and settle before the gate: T003's double-backed rows run
T004's bodies, T005's real-tree rules scan T003's file. G3 is two dependent tasks (T008 checks what T007 wrote). No group
is a chain of independent tasks, and no G2 task adds a line to a shared file.

**SC owners:** SC-001 T001 (G1 zero-warning foundation) + T008 + L-CLOSE · SC-002 to SC-015, SC-016 doctests, SC-019 to
SC-024, SC-027 to SC-031 T003 · SC-016 / SC-025 `impl StagedTree` and no-`pub`-field rules, SC-017, SC-026 T005 (SC-017's
`FR007_DEPENDENCIES` and dev-set rows T001) · SC-018 T007 (+ CI) · FR-080, FR-081 T001 (+ L-CLOSE's `cargo tree` record) ·
FR-082, FR-083, FR-034 rule T005 · FR-084, FR-085 T003 · FR-086 T007 · FR-087 T001 (surface) + T004 (bodies) · FR-088
T001 · FR-089 T004 · FR-090, FR-091 T006 (scope check) + T008 · spec text of SI-1 to SI-12 and TA-1 T002.

---

## G1 — Foundation (sequential: one task, every shared file)

### T001 — Precondition, manifests, the clippy-guard lock, `pub mod extract;`, `Sha256Stream`, visibility, the public surfaces of `extract.rs` and the test doubles, the guard rows (spec FR-080, FR-087 surface, FR-088; SC-017 part; plan §2, §4.1, §4.2, §4.3.2 trait, §4.3.5 types, §4.4, §4.5 surface, §4.6 rows 2 and 4-5; SI-1, SI-3, SI-10; TA-1 to TA-8, TA-13)

This task makes every edit to a shared app file the phase needs (except `required-tests.json` and the shape test, T007),
so G2 touches only its own files.

**Files (edit):** `apps/krate-manager/Cargo.toml` (CRLF), `crates/core/Cargo.toml` (CRLF),
`tests/fixtures/clippy-guard/Cargo.lock` (re-locked by cargo), `crates/core/src/lib.rs` (CRLF),
`crates/core/src/verify.rs` (LF), `crates/core/src/verify_tests.rs` (LF), `crates/core/src/detect.rs` (LF),
`crates/core/src/contract.rs` (LF), `crates/core/src/test_support.rs` (CRLF), `crates/core/tests/injection.rs` (CRLF,
tables only). **Files (new, LF):** `crates/core/src/extract.rs`, `crates/core/src/extract_tests.rs` (one `//!` line).

**Precondition.** Run the check of "Preconditions" above first; on failure return `blocked`, changing nothing. Then
`df -h /f` into the notes (6.2 GB free this session; below 4 GB, return `blocked` naming the number). ODR check (CLAUDE.md
"ODR Prevention"): `grep -rnE "(struct|enum|trait) (MemExtractIo|RecordingExtractIo|FaultyExtractIo|TwoPhaseArchive|ExtractIo|FsExtractIo|StagedTree|CanonicalListing|Sha256Stream|XOp|XKind|MemNode|ListingEntry|NodeKind|EntryType|ArchiveRead|ExtractFile|RemoveTreeMode|ReadFault|ExtractError|ListingCode|ExtractLimits|ExtractEnv)\b" apps/ tools/ --include=*.rs`
is empty before the first write (empty this session); record it. (`fn snapshot` exists privately in
`detect_tests.rs:1931`; the new `test_support::snapshot` does not clash unless a file both glob-imports `test_support`
and defines its own, which `detect_tests.rs` resolves to its local item.)

**Test first.**
- **The guards are the first red.** Edit the `injection.rs` rows of step 1 **before** the manifests, then run
  `cargo test -p krate-manager-core --locked --test injection core_dependencies_are_the_fr007_set` (shared target) and
  quote the red line (`crates/core/Cargo.toml [dependencies]` lacks `flate2`; the dev set still holds `flate2`). It goes
  green when step 2 lands.
- `verify_tests.rs`: `verify::tests::sha256_stream_cases` (TA-6; not in `required-tests.json`): `Sha256Stream::new()`
  with no update → `finish()` equals `sha256_of(b"")` and, through `hex_lower`,
  `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; updates `b"a"`, `b""`, `b"bc"` → equals
  `sha256_of(b"abc")` = `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` (FIPS 180-2); a 1 MiB
  buffer fed in 7-byte slices equals `sha256_of` of the whole. Red: written against the step 3 signatures with
  `todo!()` bodies → `not yet implemented`; quote it.

**Implement.**
1. **Guard rows (TA-8), written first** (`crates/core/tests/injection.rs`, CRLF): `FR007_DEPENDENCIES` (`:106-120`) →
   `[&str; 14]` with `"flate2"` after `"base64"`; `core_dependencies_are_the_fr007_set`'s dev set (`:3360-3364`) →
   `BTreeSet::from(["minisign".to_owned()])`; `const EXTRACT_TESTS_RS: &str = "crates/core/src/extract_tests.rs";` after
   `DOWNLOAD_TESTS_RS` (`:150`); `TEST_PROGRAM_BODIES` (`:396-409`) → `[(&str, &str, &str); 6]` appending
   `(EXTRACT_TESTS_RS, "staged_tree_has_no_xattrs_macos", "/usr/bin/xattr")` and `(EXTRACT_TESTS_RS,
   "symlink_entry_unsupported_windows", "cmd")` (plan SI-1, SI-10, `plan.md:52`, `:61`); the `verify_file` callers of
   `minting_route_findings` (`:2579-2583`) → `[VERIFY_RS, DOWNLOAD_RS, VERIFY_TESTS_RS, DOWNLOAD_TESTS_RS,
   EXTRACT_TESTS_RS]` (FR-088, `spec.md:548-553`). Nothing else in the file changes here (TA-8: `EXTRACT_RS` and
   `CONFINED_TOML_KEYS` are T005's).
2. **Manifests and the fixture lock (FR-080, plan §4.1 `plan.md:225-235`, TA-1).** `crates/core/Cargo.toml`: delete
   `flate2 = { workspace = true }` from `[dev-dependencies]` (`:35`) and add it to `[dependencies]` between `base64`
   (`:16`) and `log` (`:17`) under the comment `# Phase 8 D1: extract.rs only (FR-082)`; `[dev-dependencies]` keeps
   only `minisign` with its Phase 7 comment (`:36-37`). `apps/krate-manager/Cargo.toml`: one comment line above
   `flate2 = "1.1.10"` (`:17`): `# Phase 8 D1: core's extract.rs (gzip); the tar reader is in-crate` (no version
   change). Then, from `apps/krate-manager/`: `cargo metadata --locked --format-version 1 > /dev/null` (the workspace
   lock must not change; `git diff --numstat -- Cargo.lock` empty), then the fixture re-lock `cargo metadata
   --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 > /dev/null` and `git diff --numstat --
   tests/fixtures/clippy-guard/Cargo.lock` = `1	0`, `git diff -- tests/fixtures/clippy-guard/Cargo.lock` showing exactly
   `+ "flate2",` inside `krate-manager-core`'s array (TA-1). Any other change: restore the file from `git show
   HEAD:…`, record it and return `blocked`. Then `cargo tree -p krate-manager-core -e normal --locked --prefix none
   --format "{p}"` into the notes: `flate2 v1.1.10` present, no line starting `tar ` (FR-082), none of `tauri`, `tokio`,
   `reqwest`, `hyper`, `dirs`, `home` (plan §7.4 step 6); and `cargo tree --workspace --target all -e features -i
   zlib-rs --locked` prints "nothing to print" (plan "Not an issue", `plan.md:69-77`).
3. **`verify.rs` (plan §4.2 `plan.md:237-248`).** After `hex_lower` (`verify.rs:289-299`): `pub(crate) struct
   Sha256Stream(Sha256);` with `pub(crate) fn new() -> Sha256Stream`, `pub(crate) fn update(&mut self, bytes: &[u8])`,
   `pub(crate) fn finish(self) -> [u8; 32]`, doc comment "A streaming SHA-256 for `extract.rs` (Phase 8 FR-082): `sha2`
   stays confined to this file." It uses the existing `use sha2::{Digest, Sha256};` (`:9`). `verify_file` and
   `sha256_of` (`:302-307`) are unchanged.
4. **Visibility (plan §2 `plan.md:146-147`, §4.4 `plan.md:607-611`).** `detect.rs:692` `fn moduleinfo_version` →
   `pub(crate) fn moduleinfo_version`; `contract.rs:1024` `fn reserved_stem_re` → `pub(crate) fn reserved_stem_re`.
   Bodies, logs and callers unchanged; both stay used inside their files, so no reader is needed in G1.
5. **`crates/core/src/lib.rs`** (CRLF): `pub mod extract;` after `pub mod download;` (`lib.rs:6`).
6. **`crates/core/src/extract.rs` (new, LF; TA-2).** `//!` line: "Phase 8: safe extraction and staging of a verified
   Linux tar.gz (plan §4.3)." Then, verbatim from the plan: `NodeKind`, `ArchiveRead`, `ExtractFile` and the `ExtractIo`
   trait with its 14 methods and doc comments (`plan.md:282-306`); `pub struct FsExtractIo;` and `impl ExtractIo for
   FsExtractIo` whose 14 bodies are `todo!("T003")` with `_`-prefixed parameters; `EntryType` and `ListingEntry`
   (`plan.md:407-410`); `#[derive(Clone, Debug, PartialEq, Eq)] pub struct CanonicalListing { lines: Vec<String> }`
   with real `pub fn lines(&self) -> &[String]`, `pub fn text(&self) -> String` (every line followed by `'\n'`),
   `pub fn files(&self) -> u64` (the line count) and `pub fn digest(&self) -> [u8; 32]` (`Sha256Stream` over `text()`'s
   bytes); and at the end `#[cfg(test)] #[path = "extract_tests.rs"] mod tests;`. Imports only what these use
   (`std::ffi::OsString`, `std::io::{self, Read}`, `std::path::{Path, PathBuf}`, `crate::verify::Sha256Stream`). No
   private item, no constant, no `ExtractError`, no other `pub fn` (T003 writes them). `extract_tests.rs` (new, LF):
   one line, `//! Phase 8 extract tests (plan §7; T003).`
7. **`test_support.rs` Phase 8 surface (CRLF; FR-087; TA-3, TA-4, TA-7, TA-13).** Append after `DownloadRig`
   (`test_support.rs:556-583`), before `#[cfg(test)] mod tests` (`:585-587`), importing
   `crate::extract::{ArchiveRead, ExtractFile, ExtractIo, ListingEntry, NodeKind}`, `std::io::Read` and
   `std::marker::PhantomData` (the file already imports `io`, `Path`, `PathBuf`, `Arc`, `Mutex`, `:18-21`). **Real in
   T001** (plain data, no I/O): `XKind` (15 variants) and `XOp` (15 variants, each carrying its path; `Write { path,
   bytes }`) exactly as `plan.md:617-626`, with `impl XOp { pub fn kind(&self) -> XKind; pub fn path(&self) ->
   Option<&Path>; pub fn is_mutating(&self) -> bool }` (`path()` is `None` only for `Rewind`; `CreateWorkDir` →
   `extract`, `Symlink` → `link`; `is_mutating` is true for exactly `CreateWorkDir`, `CreateDir`, `CreateFile`, `Write`,
   `SetMode`, `Symlink`, `RemoveTree`, `RemoveEntry`, FR-087); `#[derive(Clone, Debug, PartialEq, Eq)] pub enum MemNode {
   Dir { mode: u32 }, File { bytes: Vec<u8>, mode: u32 }, Link { target: String } }`; `#[derive(Clone, Copy, Debug,
   PartialEq, Eq)] pub enum RemoveTreeMode { Forward, Noop, Fail(io::ErrorKind) }`; `#[derive(Clone, Copy, Debug,
   PartialEq, Eq)] pub struct ReadFault { pub pass: u8, pub after: u64, pub kind: io::ErrorKind }`; `pub struct
   TwoPhaseArchive` (private fields `first`, `after_rewind`, `pos`, `rewound`) with `pub fn new(first: Vec<u8>,
   after_rewind: Vec<u8>) -> TwoPhaseArchive`, `impl Read` (serves `first` until the first `rewind`, then `after_rewind`)
   and `impl ArchiveRead` (`rewind` sets `pos = 0`, `rewound = true`). **Placeholders (`todo!("T004")`, Rule 9):**
   - `pub struct MemExtractIo { _private: () }`; `impl MemExtractIo { pub fn new(base: &Path) -> MemExtractIo; pub fn
     plant_dir(&self, path: &Path, mode: u32); pub fn plant_file(&self, path: &Path, bytes: &[u8], mode: u32); pub fn
     plant_link(&self, path: &Path, target: &str); pub fn node(&self, path: &Path) -> Option<MemNode>; pub fn
     paths_under(&self, root: &Path) -> Vec<PathBuf>; pub fn calls(&self) -> Vec<XOp>; pub fn mutating_calls(&self) ->
     usize }`; `impl ExtractIo for MemExtractIo` (14 methods).
   - `pub struct FaultyExtractIo<'a> { _private: PhantomData<&'a ()> }`; `impl<'a> FaultyExtractIo<'a> { pub fn
     new(inner: &'a (dyn ExtractIo + Sync)) -> FaultyExtractIo<'a>; pub fn with_failure(self, kind: XKind, nth: usize,
     error: io::ErrorKind) -> Self; pub fn with_remove_tree(self, mode: RemoveTreeMode) -> Self; pub fn
     with_read_fault(self, fault: ReadFault) -> Self; pub fn calls(&self) -> Vec<XOp>; pub fn mutating_calls(&self) ->
     usize }`; `impl ExtractIo for FaultyExtractIo<'_>`.
   - `pub struct RecordingExtractIo<'a> { _private: PhantomData<&'a ()> }`; `impl<'a> RecordingExtractIo<'a> { pub fn
     forwarding(inner: &'a (dyn ExtractIo + Sync)) -> RecordingExtractIo<'a>; pub fn with_open_archive(self, f: Box<dyn
     Fn(&Path) -> io::Result<Box<dyn ArchiveRead>> + Send + Sync + 'a>) -> Self; pub fn with_read_tree(self, f: Box<dyn
     Fn(&Path, &mut Vec<ListingEntry>) + Send + Sync + 'a>) -> Self; pub fn with_canonicalize(self, f: Box<dyn Fn(&Path,
     io::Result<PathBuf>) -> io::Result<PathBuf> + Send + Sync + 'a>) -> Self; pub fn with_kind_of(self, f: Box<dyn
     Fn(&Path, io::Result<Option<NodeKind>>) -> io::Result<Option<NodeKind>> + Send + Sync + 'a>) -> Self; pub fn
     calls(&self) -> Vec<XOp>; pub fn mutating_calls(&self) -> usize; pub fn canonicalized(&self) -> Vec<PathBuf> }`;
     `impl RecordingExtractIo<'static> { pub fn fail_closed() -> RecordingExtractIo<'static> }`; `impl ExtractIo for
     RecordingExtractIo<'_>`.
   - `pub fn snapshot(root: &Path) -> Vec<(String, char, u64)>`.
   A placeholder method's `ExtractFile` / `ArchiveRead` return types keep those imports used.
8. **Lanes.** L-RS, L-APP, L-DEVFEED, L-GUARD, L-TS, L-VLINT; record each wall clock (Phase 7's warm `cargo test` was
   26.8 s, row A1e).

**Done when:** the G1 gate lanes are green with 0 `warning:` lines; `core_dependencies_are_the_fr007_set` and
`sha256_stream_cases` pass; every pre-existing test passes (`test result:` lines quoted); the clippy-guard lock diff is
exactly the one line and `Cargo.lock` is unchanged; the `cargo tree` records are in the notes; `git status --porcelain`
lists only Rule 11's files. **Lane:** L-RS, L-APP, L-DEVFEED, L-GUARD, L-TS, L-VLINT.

---

## G2 — Spec, module, doubles, guards and scripts [P] (five tasks, disjoint files, Rule 10 slots)

### T002 [P] — Precondition check and the spec amendments (plan §0.2 SI-1 to SI-12, §10 R-1 / R-2; TA-1)

**Files:** `specs/krate-manager-phase8/spec.md` (edit, LF). Nothing else.

**Test first:** record the needle counts of "Spec state" above (`grep -c` for `read_archive`, `mklink`,
`TwoPhaseArchive`, `U+FEFF`, `kind_of(extract)`, `symlink_file`, `ustar-prefix` in SC-024's paragraph, `"flate2",`
in FR-080, `cmd` in FR-087); each is 0 (or, for `ustar-prefix`, absent from SC-024 `spec.md:794-803`) before the edit.

**Implement:** run the Preconditions check first. Then apply, in place, each "Proposed spec edit" of plan §0.2
(`plan.md:52-63`, binding wording), each tagged `(plan SI-n)`: SI-1 (FR-087 and New components: the two
`TEST_PROGRAM_BODIES` rows, `cmd` allowed in `symlink_entry_unsupported_windows`; FR-087's first sentence then names
both programs), SI-2 (FR-083's method-name exception and SC-017's planted case), SI-3 (FR-080: the clippy-guard lock
gains `"flate2"`, re-locked with **`cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml
--format-version 1 > /dev/null`** (tasks TA-1, replacing SI-3's `cargo update` command), committed with the phase; the
New components row loses "only if"), SI-4 (SC-024 / SC-027, R-2), SI-5 (FR-015's deferral sentence and SC-006's three
runtime rows, R-1), SI-6 (FR-040 / FR-049: one critical section), SI-7 (FR-045: step `read_archive`), SI-8 (FR-057: the
decode / BOM / `CoreVersion` sentence; SC-028 (g), (h)), SI-9 (FR-058 / FR-040 and SC-029 / SC-020's linked-`extract/`
rows), SI-10 (FR-058: the Windows `remove_dir_all` fallback; SC-023's junction block), SI-11 (FR-085: the added runtime
archives of plan §7.1), SI-12 (the SC-003, SC-004, SC-009, SC-010, SC-012, SC-020, SC-023, SC-030, SC-031 additions and
the FR-041, FR-082, FR-083, FR-087 additions it lists). Update the header's status line to "DRAFT, revision 4 (plan §0.2
SI-1 to SI-12 and tasks TA-1 applied)" and add one Review-notes bullet naming the revision. Change no FR / SC id, no
test name and no lane count (plan §4.7 totals stay 248 / 11 / 35 / 30 / 29); do not touch `## Clarifications`.

**Done when:** every needle count of "Test first" is ≥ 1 (`ustar-prefix` inside SC-024), `grep -c "cargo update
--workspace" spec.md` is 0, and `grep -nE "FR-0[0-9]{2}|SC-0[0-9]{2}" spec.md | wc -l` is not lower than before (record
both numbers). **Lane:** none (doc only); T008's L-INDEX reads the spec title.

### T003 [P] — `core::extract`: the reader, the C4 rules, the listing, the layout check, `FsExtractIo`, the registry and prune, `stage`, `StagedTree` / `discard`, the doctests, and all 32 SC tests (spec FR-001 to FR-059, FR-084, FR-085; SC-002 to SC-016 doctests, SC-019 to SC-024, SC-027 to SC-031; plan §3, §4.3, §6, §7.1, §7.2; PD-1 to PD-11; SI-4 to SI-12 behaviour; TA-2 to TA-4, TA-13)

**Files:** `crates/core/src/extract.rs` (T001's placeholder; LF) and `crates/core/src/extract_tests.rs` (T001's
placeholder; LF). Nothing else (the doubles are T004's: if a double's behaviour blocks a row, write the row to its plan
assertions, record "expected red until T004" and continue).

**Target:** the shared `apps/krate-manager/target`, `CARGO_BUILD_JOBS=16` (Rule 10). L-OWN: `cargo test -p
krate-manager-core --locked --lib extract::` and `cargo test -p krate-manager-core --locked --doc extract`.

**Expected red until T004 lands** (the only one): any row over `MemExtractIo`, `RecordingExtractIo`,
`FaultyExtractIo` or `snapshot` failing with `not yet implemented` at `crates/core/src/test_support.rs:<line>`. Rows
over plain `FsExtractIo` and the pure functions (`plan_archive`, `CanonicalListing::from_entries`,
`check_staged_layout`, `entry_path`) depend on nothing outside this task.

**Test first** (`extract_tests.rs`, module `extract::tests`; written before the bodies they test, in the order of the
implementation steps below, each batch run red against T001's / this task's `todo!()`s and quoted).
- **Shared helpers** (plan §7 `plan.md:784-789`, binding): `use super::*;` plus explicit imports; `ustar(entries) ->
  Vec<u8>` (512-byte headers with a correct checksum, optional GNU `L` / `K`, pax `x` / `g` records, the end block),
  `gz(bytes) -> Vec<u8>` (`flate2::write::GzEncoder::new(Vec::new(), Compression::default())`, `finish()`);
  `context(roots, key) -> AppContext` (the `download_tests.rs:148-156` literal); `signed(bytes, entry)` → the bytes
  written into the test's `TempDir` (outside the staging dir), signed in memory with `TestSigner::sign_c3`
  (`crate::test_signer`), verified with `verify::verify_file(&path, &Expected { … }, &signer.key())` into a
  `VerifiedArtifact` (FR-084: never `SigningKey::from_text`); `alpha_entry(version, payload)` = `test_feed(&[TestPlugin
  { name: "Alpha", key: PlatformKey::LinuxX86_64, … }])` (`test_support.rs:300-374`) with the Linux asset's `payload`
  replaced (the fields are `pub`, `feed.rs:77-80`); `literal_payload(text) -> Payload` (`files` = line count,
  `sha256` = `Sha256Stream` over the literal text written in the test, FR-085); `fixture(name) -> Vec<u8>` reading
  `tools/feed-contract/fixtures/listings/{good,bad}/<name>` in place via `env!("CARGO_MANIFEST_DIR")`. The KAT archive
  is copied into the `TempDir` and verified with `kat_signing_key()` (`test_support.rs:284-289`).
- **The 32 tests, each its plan §7.1 row verbatim** (`plan.md:793-825`, inputs and assertions binding, amended by SI-4
  to SI-12 as written there). Every `stage` row asserts `code()` only (plus the fields the row names); every refusal row
  runs over `RecordingExtractIo::fail_closed()` and asserts `mutating_calls() == 0` where the row says so (Rule 1).
  `cargo.all` (every OS, no `cfg`): `good_fixtures_match_committed_listings` (SC-002), `bad_fixtures_fail_with_expected_code`
  (SC-003: the 22 bad fixtures; the 33 reject rows and the accept rows of `c4-listing-tar.test.js:196-231`, `:236-280`
  ported to `ListingEntry` values through `CanonicalListing::from_entries`; three streaming rows),
  `kat_archive_stages` (SC-004, with its `#[cfg(unix)]` mode block), `real_shaped_round_trip`,
  `exec_and_symlink_round_trip_logic`, `dot_slash_round_trip` (SC-005), `crafted_archives_are_refused` (SC-006 incl. the
  SI-5 two-defect rows and the header flood), `payload_and_layout_mismatches` (SC-007), `entry_binding_cases` (SC-008:
  `RecordingExtractIo::fail_closed().calls()` empty, not even `OpenArchive`), `changed_archive_is_refused` (SC-009 (a)-(f),
  (d)-(f) over `forwarding(&FsExtractIo).with_open_archive(…)` returning a `TwoPhaseArchive`),
  `io_failures_leave_no_tree` (SC-010 (i)-(iv): `FaultyExtractIo::new(&FsExtractIo).with_failure(XKind::Write, 1,
  StorageFull)`, `.with_remove_tree(RemoveTreeMode::Noop)` / `Fail(PermissionDenied)`, `.with_read_fault(ReadFault {
  pass, after: 1024, kind })`, under `install_capture` / `take_mine`), `pax_xattr_records_are_ignored` (SC-013 every-OS
  half), `case_fold_collision` (SC-014, pinned per target), `discard_and_prune` (SC-015), `tree_mismatch_is_refused`
  (SC-019), `entry_path_rejects_every_bad_form` (SC-020 with its `#[cfg(windows)]` reserved-name block and
  `#[cfg(not(windows))]` acceptance block), `work_dir_must_be_inside_staging` (SC-020 Mem rows incl. SI-9 and the two
  FR-046 rows), `stage_logs_one_line_per_outcome` (SC-021), `concurrent_stage_is_safe` (SC-024, `std::thread::scope`, a
  test-local gate wrapper over a `&(dyn ExtractIo + Sync)` with a `Barrier` pair; Fs variant B = `ustar-prefix`, R-2),
  `file_hashes_match_written_bytes` (SC-027: `real-shaped` over Fs, `exec-and-symlink` over
  `forwarding(&mem)`, R-2), `moduleinfo_version_must_match` (SC-028 (a)-(h)), `prune_removes_by_kind` (SC-029 Mem),
  `path_length_bounds` (SC-030 incl. (c)'s real-root assertions), `trailing_zero_padding_is_accepted` (SC-031 incl. the
  gzip-layer rows (g1)-(g6)). `#[cfg(unix)]` (macOS + Linux): `prune_removes_real_link_unix` (SC-029 real link + the SI-9
  production-body block), `posix_modes_and_links_unix` (SC-011, rebuilt digest `baad7502…`),
  `special_bits_and_mtime_not_applied_unix` (SC-022), `escape_cases_unix`, `symlinked_staging_root_unix` (SC-012 (1),
  (2) incl. the dangling-link and link-outside-its-top rows; roots from `temp_roots_via_link`, T004).
  `#[cfg(target_os = "macos")]`: `staged_tree_has_no_xattrs_macos` (SC-013; `Command::new("/usr/bin/xattr").args(["-lr",
  …])` on the staged root only, the program literal of T001's row), `raw_temp_dir_staging_root_macos` (SC-012 (3); roots
  from `temp_roots_raw`, T004). `#[cfg(windows)]`: `symlink_entry_unsupported_windows` (SC-023 incl. the junction block
  (j1), (j2), whose `cmd /C mklink /J` closure is copied from `download_tests.rs:2952-2964`, plain non-`\\?\` paths, both
  inside the test's `TempDir`).
  The name of each test is exactly FR-086's (`spec.md:509-536`); no other `#[test]` in this file (helper fns only).
- **Doctests** in `extract.rs` (SC-016, plan §4.3.11 `plan.md:558-576`): the two `compile_fail` literals (the second
  naming every field, `todo!()` where a type has no public constructor) and the `no_run` accessor read.

**Implement** (plan sections binding; each step's tests turn green before the next):
1. **Error model and limits** (plan §3 `plan.md:164-219`, §4.3.1 `plan.md:254-277`): `ListingCode`, `ExtractError` with
   `code()`, `ExtractLimits::PRODUCTION` (`max_moduleinfo_bytes: crate::detect::MAX_MODULEINFO_BYTES`, `detect.rs:18`),
   `ExtractEnv`, `LOG_TARGET`, `EXTRACT_DIR`, the private constants. FR-045's `io_error(step, entry, e)` mapping.
2. **The raw reader and the gzip member loop** (§4.3.3 `plan.md:331-359`: `Hashed` tap, `BufReader`, `GzMembers` over
   `flate2::bufread::GzDecoder`, `Budget`; PD-4's tap decides `read_archive` vs `E_LISTING_CORRUPT`).
3. **The tar header reader** (§4.3.4 `plan.md:361-402`, a port of `tools/feed-contract/listing.js:163-274`).
4. **The rule engine and `CanonicalListing::from_entries`** (§4.3.5 `plan.md:404-439`; PD-11: one implementation for
   the pure builder and the stream).
5. **`plan_archive`** (§4.3.6 `plan.md:441-458`; PD-1's deferral and resolution order, R-1).
6. **`check_staged_layout`** (§4.3.7 `plan.md:460-468`) and **`entry_path` / `joined_len`** (§4.3.8 `plan.md:470-487`,
   PD-9; the `cfg(windows)` reserved-stem / trailing dot-space branch through `crate::contract::reserved_stem_re()`).
7. **`FsExtractIo`** (§4.3.2 table `plan.md:310-329`; PD-6's `create_work_dir` directory check, PD-7's
   `#[cfg(windows)]` `remove_entry` fallback; `tempfile::Builder::new().prefix(prefix).tempdir_in(extract)?.keep()`; the
   only place any FR-083 token, `create_dir_all` or `tempfile` appears; no `sync_*`, no owner / group / time / xattr
   call, FR-043, FR-055). `ArchiveRead` / `ExtractFile` implemented only for `std::fs::File` or a private type (TA-13).
8. **Live registry and `prune`** (§4.3.9 `plan.md:489-504`; PD-2: prune, `create_work_dir` and the insert in one
   critical section; poison → `into_inner`, the `download.rs:646-650` model).
9. **`stage`** (§4.3.10 `plan.md:506-554`, steps 1-15 in order: binding before any I/O, pass 1 with the raw hash,
   layout, symlink support, joined bound, sandbox + critical section, work-dir check, then `cleanup` returning the
   original error, directories, pass 2 with the per-entry comparison **on the header before any create**, symlinks
   last, the physical post-check, the tree listing, the version check of PD-5 through
   `crate::detect::moduleinfo_version(text, Path::new("Contents/Resources/moduleinfo.json"))`, the result).
10. **`StagedTree` and `discard`** (§4.3.11 `plan.md:556-598`; PD-8; no `Clone` / `Default` / `From`, every accessor
    `&self`) and **logging** (§4.3.12 `plan.md:600-605`: one `info!` per `stage`, no `Path` display anywhere).

**Done when:** L-OWN green for `extract::` (every `cargo.all` and `windows` name `ok`, quoted) and the doctests; the
`cfg(unix)` / `cfg(target_os = "macos")` tests read twice against their rows and recorded "not compiled here" (Rule 15);
L-RS green once T004 has landed (no `not yet implemented` left); `grep -n "todo!(" crates/core/src/extract.rs` empty.
**Lane:** L-OWN, then L-RS.

### T004 [P] — The Phase 8 test doubles and FR-089's constructors (spec FR-087, FR-089; plan §4.5; TA-3 to TA-7, TA-13)

**Files:** `crates/core/src/test_support.rs` (CRLF; the T001 surface and the `temp_roots_in` refactor only) and
`crates/core/src/test_support_tests.rs` (LF; one new test). Nothing else.

**Target:** slot `F:/tmp/km8-target/side`, `CARGO_BUILD_JOBS=12` (Rule 10, TA-9). L-OWN: `cargo test -p
krate-manager-core --locked --lib test_support::`.

**Test first:** `test_support::tests::phase8_doubles_cases` (TA-6; not in `required-tests.json`), on a
`fresh_temp_roots()` base whose staging dir is `staging`, written against T001's signatures and run red (`not yet
implemented`, quote it):
- **Mem** (`MemExtractIo::new(&staging)`): `kind_of(staging)` and `kind_of(staging.parent())` → `Some(Dir)`;
  `kind_of(staging/x)` → `None`; `create_work_dir(staging/extract, "ab.")` → `staging/extract/ab.000001`, a second call
  → `…/ab.000002`, `node(staging/extract)` is a `Dir`; with `plant_link(staging/l, "t")` and `plant_dir(staging/t,
  0o755)`, `create_work_dir(staging/l, "p")` → `Err(InvalidInput)` (PD-6 semantics); `create_dir(w/d)` → `Ok`, again →
  `AlreadyExists`, `create_dir(staging/missing/d)` → `NotFound`; `create_file(w/f)` then `write_all(b"he")`,
  `write_all(b"llo")` → `node(w/f) == File { bytes: b"hello", mode: 0o644 }`; `create_file` over a dangling planted link
  → `AlreadyExists` (`create_new`); `set_mode(w/f, 0o755)` → the node's mode; `symlink("f", w/l)` → `Link { target: "f"
  }`; `canonicalize(w/l)` → `w/f`; `canonicalize` of a link to an absent name → `NotFound`; `read_tree(w)` → entries
  with `/`-joined relative byte paths, `mode: Some(..)` (links `Some(0o777)`) and the link's target bytes;
  `read_file(w/f, 2)` → at most 3 bytes; `list_dir(w)` → the children with their kinds, `list_dir(staging/absent)` →
  `Ok(None)`; `remove_entry(w/l, NodeKind::Symlink)` removes the link only (`w/f` stays); `remove_tree(w)` removes `w`
  and every descendant; `open_archive` of a real file in the `TempDir` reads its bytes and `rewind` restarts them;
  `calls()` holds one `XOp` per call in order, `Write` with the exact slices, and `mutating_calls()` counts the eight
  FR-087 kinds only; nothing was created on disk under `staging` (`std::fs::read_dir(staging)` unchanged).
- **Recording** over `forwarding(&mem)`: delegation plus records; `with_read_tree` mutates the returned list;
  `with_canonicalize` and `with_kind_of` map the inner result; `with_open_archive` returns a `TwoPhaseArchive` and the
  record still holds `OpenArchive(path)`; `canonicalized()` lists the `canonicalize` arguments in order.
  `fail_closed()`: each of `create_work_dir`, `create_dir`, `create_file`, `set_mode`, `symlink`, `remove_tree`,
  `remove_entry` on paths under the `TempDir` → `Err` with kind `Other` and message `fail-closed`, each recorded,
  `mutating_calls() == 7`, and `snapshot(td.path())` before == after (its read-only methods forward to `FsExtractIo`,
  T003's; they are not called here).
- **Faulty** over `&mem`: `with_failure(XKind::CreateDir, 2, PermissionDenied)` → first `create_dir` `Ok`, second
  `Err(PermissionDenied)`; `with_failure(XKind::Write, 1, StorageFull)` → the first `write_all` of any file fails;
  `with_remove_tree(Noop)` → `Ok` and the node remains, `Fail(k)` → `Err(k)`, `Forward` removes; `with_read_fault(ReadFault
  { pass: 1, after: 4, kind: TimedOut })` over a 10-byte archive → `read_to_end` fails with `TimedOut` after exactly 4
  bytes; `pass: 2` → pass 1 reads all 10, after `rewind` the read fails after 4.
- **`TwoPhaseArchive`**: serves `first`, after `rewind` serves `after_rewind`, a second `rewind` restarts
  `after_rewind`.
- **`snapshot`** of a `TempDir` holding `a/` and `a/f` (3 bytes) → `[("a", 'd', _), ("a/f", 'f', 3)]` sorted, relative
  with `/`; `#[cfg(unix)]`: a symlink appears as `'l'` and is not followed.
- **`Sync`**: `fn sync<T: Sync>() {}` holds for `MemExtractIo`, `RecordingExtractIo<'static>`, `FaultyExtractIo<'static>`
  (TA-4).
- **`#[cfg(unix)]` block (FR-089):** `temp_roots_via_link(td.path())` → every root under `<canonical td>/via`,
  `canonicalize(staging_dir()) != staging_dir()`, `<canonical td>/real` holds the created directories,
  `check_sandboxed(staging_dir().join("x"))` does not panic. (`temp_roots_raw` is `cfg(target_os = "macos")` and is
  exercised by `raw_temp_dir_staging_root_macos` on CI; record "not compiled here".)

**Implement:** replace T001's placeholders with plan §4.5's behaviour (`plan.md:615-683`, binding) under TA-3 / TA-4's
API: `MemExtractIo` over `Arc<Mutex<BTreeMap<PathBuf, MemNode>>>` + `Arc<Mutex<Vec<XOp>>>` + a counter, absolute keys,
paths outside `base` (other than its ancestors) are `NotFound`, `create_dir` / `create_file` require an existing parent
directory and an absent name (dangling links included), new dirs mode `0o755` and files `0o644`, `canonicalize` resolves
links component by component (dangling → `NotFound`), `supports_symlinks() == true`; `FaultyExtractIo` (the
`FaultyStagingIo` model, `test_support.rs:378-545`) with its fails list, `RemoveTreeMode`, `ReadFault` counting
`ArchiveRead`, and a wrapping `ExtractFile` that records and counts `Write`; `RecordingExtractIo` with `forwarding`
(delegate and record), `fail_closed` (FR-087: the eight mutating methods record and return `io::Error::other("fail-closed")`
without touching `std::fs`; `open_archive`, `list_dir`, `kind_of`, `read_tree`, `canonicalize`, `read_file`,
`supports_symlinks` forward to `crate::extract::FsExtractIo`), the four overrides and a recording `ExtractFile`;
`snapshot` via `symlink_metadata` (never following), sorted. **FR-089:** factor `fn roots_at(base: &Path) -> Roots`
out of `temp_roots_in` (`test_support.rs:83-132`: the `RootPaths`, `Roots::new` under its existing `#[expect]` with
the same reason, `.sandboxed(base)`, the `create_dir_all` loop), so `temp_roots_in` = `check_under_temp` + `roots_at`
(behaviour unchanged; every existing `test_support::tests::*` stays green); `#[cfg(unix)] pub fn
temp_roots_via_link(td: &Path) -> Roots` and `#[cfg(target_os = "macos")] pub fn temp_roots_raw(td: &Path) -> Roots`
exactly as `plan.md:676-683` (check first, panic before creating anything; no new `std::env::temp_dir` call, so
`D6_SITES` stays). Rule 4b / TA-7: no FR-088 pattern in any added line. TA-13: `ArchiveRead` / `ExtractFile` impls only
for this file's own types.

**Done when:** `phase8_doubles_cases` and every existing `test_support::tests::*` pass in L-OWN; L-OWN clippy 0
`warning:`; `grep -n 'todo!("T004")' crates/core/src/test_support.rs` empty; the `cfg(unix)` block read twice against
FR-089 (`spec.md:554-563`) and recorded "not compiled here"; then L-RS (T003's double-backed rows go green with it).
**Lane:** L-OWN, then L-RS.

### T005 [P] — `injection.rs`: the extract confinement, token, `tempfile`, `tar`, `StagedTree` and `release.json` guards (spec FR-034, FR-055, FR-082, FR-083; SC-016 guard, SC-017, SC-025, SC-026; plan §4.6 rows 1, 3, 4-5 rules; SI-2, SI-12; TA-8, TA-12)

**Files:** `crates/core/tests/injection.rs` (CRLF). Nothing else.

**Target:** slot `F:/tmp/km8-target/side`, `CARGO_BUILD_JOBS=12`. L-OWN: `cargo test -p krate-manager-core --locked
--test injection`.

**Test first (the plants):** extend the existing planted-case tests (FR-086: no new test name) so every new rule has a
plant that must be reported, then run them red (the rule does not exist yet, so `assert_reported` fails; quote it):
- `verify_confinement_scanner_reports_plants` (`injection.rs:4642`, its `plants` array grows): the plan §4.6 row 3
  plants (`plan.md:692`, binding): `use flate2::read::GzDecoder;` in `feed.rs`; `use sha2::Sha256;` in `extract.rs`;
  `fn f() { std::fs::create_dir("x").ok(); }` in `extract.rs` outside the impl (SI-2); `fn f(x: std::fs::File) {
  x.sync_all().ok(); }` in `extract.rs`; `fn f() { let _ = tempfile::tempdir(); }` in `extract.rs`; `fn f(p:
  &std::path::Path) { let _ = std::fs::File::options().write(true).create(true).open(p); }` and `fn f() { let _ =
  std::fs::DirBuilder::new().create("x"); }` in `extract.rs` outside the impl; `tar = "0.4"` in `crates/core/Cargo.toml`;
  a lock text `[[package]]\nname = "tar"\nversion = "0.4.44"` appended to a copy of `Cargo.lock`'s text; `pub struct
  StagedTree { pub root: PathBuf }` in `extract.rs`; `impl StagedTree { pub fn into_bundle(self) -> PathBuf { self.root }
  }` in `extract.rs` (SC-016 / SC-025); `"release.json"` as a string literal in `extract.rs` (FR-034). Plus a quiet
  case that must **not** be reported: `trait ExtractIo { fn create_dir(&self, p: &Path) -> io::Result<()>; fn
  symlink(&self, t: &str, l: &Path) -> io::Result<()>; }` and `fn g(io: &dyn ExtractIo) { io.create_dir(p).ok(); }` in
  `extract.rs` (SI-2's exception).
- `verified_artifact_scanner_reports_plants` (`:4345`): `fn f() { let _ = verify_file(p, e, k); }` in `extract.rs`
  (reported: `extract.rs` is not a caller) and `fn f() { let _ = SigningKey::from_text(t); }` in `extract_tests.rs`
  (reported, FR-084) (plan §4.6 row 5).
- `ureq_is_confined_to_http_rs` (`:3481`, TA-12): beside the `ureq` TOML plant (`:3571-3584`), `replace_in(&manifests,
  "src-tauri/Cargo.toml", "[dependencies]\n", "[dependencies]\nflate2 = \"1\"\n")` → reported as `RULE_TOML_KEY`
  `flate2`.

**Implement:**
1. `const EXTRACT_RS: &str = "crates/core/src/extract.rs";` beside `DOWNLOAD_RS` (`:161`); `CONFINED_TOML_KEYS`
   (`:299-318`) → `[_; 13]` with `("flate2", &["Cargo.toml", "crates/core/Cargo.toml"])` (FR-080).
2. A new `fn extract_findings(files: &[(String, String)], locks: &[(String, String)]) -> Vec<Finding>` (or the
   `manifests` / lock text passed the way `ureq_findings` takes them), over the lexer of the file (`lex`, `tokenized`,
   `spanned_tokens`, `seq_at`, `injection.rs:758-813`), holding every rule of plan §4.6 row 3 (`plan.md:692`, binding)
   as amended by SI-2: `flate2` in non-test files (`!is_test_source`, `:1441-1448`) only in `EXTRACT_RS`; the token list
   (FR-083's eleven plus `File :: options`, `DirBuilder`, `remove_dir`, `soft_link`, `symlink_file`, `symlink_dir`,
   `set_len`) only inside `impl ExtractIo for FsExtractIo` in `extract.rs`, except `fn create_dir` / `fn symlink` as a
   method declaration inside `trait ExtractIo` or any `impl ExtractIo for …` and `. create_dir (` / `. symlink (` as a
   method call; `create_dir_all` only in `fn create_work_dir` of that impl; `tempfile` only inside that same fn, where
   the only `tempfile` method token is `tempdir_in` (so `use tempfile` at file scope is reported); `hard_link`,
   `Command`, `TcpStream`, `xattr`, `thread :: spawn`, `sync_all`, `sync_data`, `sync_file`, `sync_dir` nowhere in
   `extract.rs`, and `trait ExtractIo`'s body declares no `sync_*`; `tar` is a key in no workspace manifest and no
   `[[package]]` with `name = "tar"` is in `apps/krate-manager/Cargo.lock` or `tests/fixtures/clippy-guard/Cargo.lock`;
   `StagedTree {` in non-test files only in `extract.rs`; the body of `pub struct StagedTree` holds no `pub` token;
   `impl StagedTree` blocks hold no `fn` whose first parameter is `self` / `mut self` and none named `into_*`, `take_*`,
   `rename`, `persist`, `release`; no string literal in `extract.rs` holds `release.json`. Absence is never a finding
   (an "only in" rule passes on T001's placeholder and on any state of T003's file that obeys it).
3. The real-tree half in `verify_crates_are_confined` (`:4620`): `assert!(extract_findings(..).is_empty(), …)` beside the
   existing base assertion (`:4637-4638`), and `EXTRACT_RS` added to its "was scanned" list (`:4622-4636`).

**Expected red:** none planned. A real-tree finding while T003 is mid-write is T003's defect to fix (T003 owns the
file; record the finding text and tell nobody to change the rule); by the G2 gate the real tree must be green.

**Done when:** every plant reported, the quiet case not reported, the real tree green, every other `injection` test
green in L-OWN; clippy 0 `warning:`. **Lane:** L-OWN, then L-RS.

### T006 [P] — Evidence scripts: scope check, L-WSL, L-CLOSE (spec SC-001, SC-018 record, FR-081, FR-090, FR-091; plan §7.4, §8; TA-10, TA-11)

**Files (new, LF):** `specs/krate-manager-phase8/evidence/scope-check.js`, `specs/krate-manager-phase8/evidence/wsl-lane.js`,
`specs/krate-manager-phase8/evidence/close-lane.js`. Node only, no dependencies, no cargo command run by this task.

**Test first:** each script's `--self-test` (pure checks over synthetic inputs and the real workflow text), written
before its main path and run red (quote the first failing check):
- `scope-check.js --self-test`: Rule 11's allowed paths accepted (each `apps/krate-manager/` file Rule 11 names,
  `specs/krate-manager-phase8/**`, `specs/INDEX.md`, `tools/feed-contract/tests/manager-ci-shape.test.js`); refused:
  `apps/krate-manager/Cargo.lock`, `apps/krate-manager/src/main.ts` (tracked change **and** untracked new file),
  `apps/krate-manager/src-tauri/src/lib.rs`, `src-tauri/tauri.conf.json`, `apps/krate-manager/package.json`,
  `apps/krate-manager/clippy.toml`, `apps/krate-manager/crates/core/src/download.rs`, `.github/workflows/manager-ci.yml`,
  `plugins/x`, `tools/feed-contract/listing.js`, `tools/feed-contract/fixtures/listings/good/real-shaped.tar.gz`; a
  roadmap hunk only on the Phase 8 status line accepted, any other roadmap hunk refused; a shape-test hunk outside
  `EXPECTED_REQUIRED` refused.
- `wsl-lane.js --self-test`: `toWslPath` and the verdict over sample cargo output (5/5 FR-086 `_unix` names `ok` →
  GREEN with `Linux-run Phase 8 tests seen ok: 5/5`; one missing → still GREEN with `4/5` (information only); any
  `FAILED` / `warning:` → RED).
- `close-lane.js --self-test`: `extractStep` over the real `manager-ci.yml` for every step it runs; the `cargo tree`
  verdicts (A1h below) over sample trees; the suppression scan (A1j) over a sample `diff -U0` (an added
  `#[expect(clippy::x, reason = "y")]` line → RED; an added comment `// see allow(` outside an attribute → GREEN; an added
  `// @ts-ignore` → RED).

**Implement:** clone the three Phase 7 scripts (`specs/krate-manager-phase7/evidence/{scope-check,wsl-lane,close-lane}.js`,
read whole first) and change only what this phase needs:
- **scope-check.js:** `BASE = '2499fb13'`; Rule 11's allowed set (tracked diff against the base plus `git ls-files
  --others --exclude-standard`); one roadmap window, the Phase 8 status line under `### Phase 8: Safe Extraction and
  Staging (Linux tar.gz)` (roadmap line 634; Phase 7's form at line 601); the shape-test hunks must lie inside
  `EXPECTED_REQUIRED` (`manager-ci-shape.test.js:337` to its closing brace).
- **wsl-lane.js:** the log default `F:/tmp/gate/km8-wsl/`; the five `_unix` names of FR-086 (`spec.md:530-532`); the same
  `CARGO_TARGET_DIR=$HOME/km-target`; a 40 min ceiling (the first run builds the module cold).
- **close-lane.js:** Phase 7's rows **minus** A0c, A1i and B2-B7 (the base binary, Phase 7's SC-024 timing delta and the
  `tauri build` rows, TA-11), with: `BASE = '2499fb13'`; `REQUIRED_NOTICE = '::notice title=required tests ran
  (Windows)::259 cargo + 29 vitest'` (248 `all` + 11 `windows`, plan §4.7); A1h `cargo tree -p krate-manager-core -e
  normal --locked --prefix none --format "{p}" > evidence/core-tree-phase8.txt` with `flate2 v1.1.10` present and no
  line starting `tar `, `tauri `, `tokio `, `reqwest `, `hyper `, `dirs ` or `home ` (FR-081, FR-082, plan §7.4 step 6);
  a new row A1l: `cargo tree --workspace --target all -e features -i zlib-rs --locked` prints "nothing to print" and
  `cargo tree --workspace --target all -i tar --locked` reports that `tar` matched no package (plan `plan.md:69-79`);
  A1j `git diff -U0 2499fb13 -- apps/krate-manager > evidence/diff-u0.txt` plus the untracked files read whole, searched
  for added `allow(` / `expect(` attributes, `@ts-ignore`, `eslint-disable` (expected none, SC-001); A1k (FR-090) `git
  diff --name-only 2499fb13 -- apps/krate-manager/src apps/krate-manager/src-tauri apps/krate-manager/index.html` and the
  untracked files under those paths both empty; A1m: `git diff --numstat 2499fb13 -- apps/krate-manager/Cargo.lock`
  empty and `git diff --numstat 2499fb13 -- apps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock` = `1	0` (TA-1);
  A9 the TA-1 / Rule 9 grep; A10 calls this phase's `wsl-lane.js`; B1 the `required tests ran` step against
  `REQUIRED_NOTICE`; the raw log root `F:/tmp/gate/krate-manager-phase8-close/`; `timings.md` and `close-lane.log`
  written under `evidence/`; C3 `node tools/clean-test-output.js --apply` last. Every child under a wall-clock ceiling;
  independent rows concurrent (A1 and A2 as chains, the rest beside them), as in Phase 7.

**Done when:** `node --check` and `--self-test` green for all three (`SELF-TEST GREEN (<n> cases)`); `node
specs/krate-manager-phase8/evidence/scope-check.js` exits 0 against the current tree (or reports only paths a G2 task is
still writing, recorded). Not run here: `wsl-lane.js` and `close-lane.js` main paths (main loop). **Lane:** L-NODE
(`node --check` only), the three self-tests.

---

## G3 — Integration (sequential: T007, then T008)

### T007 — `required-tests.json` and the shape test's `EXPECTED_REQUIRED`: the per-OS CI additions (spec FR-086, FR-091; SC-018; plan §4.7, §7.3)

No workflow file changes (FR-091): `manager-ci.yml`'s `required tests ran` step (`.github/workflows/manager-ci.yml:153-178`)
already asserts on each leg that every name of `cargo.all` plus that leg's list passed exactly once (the regex at
`:166`), so these lists **are** the per-OS CI additions for this phase's SCs.

**Files:** `apps/krate-manager/tests/required-tests.json` (LF), `tools/feed-contract/tests/manager-ci-shape.test.js` (LF;
only inside `EXPECTED_REQUIRED`, `:337` to its closing brace).

**Test first:** append FR-086's names (`spec.md:509-536`) to `EXPECTED_REQUIRED` only: `cargo.all` + the 24 names in
FR-086's order, `cargo.macos` + the 5 `_unix` names then the 2 `_macos` names, `cargo.linux` + the 5 `_unix` names,
`cargo.windows` + `extract::tests::symlink_entry_unsupported_windows` (plan §4.7 `plan.md:701-703`); run L-NODE: the
shape test's deep-equal (`manager-ci-shape.test.js:816`, `util.isDeepStrictEqual`, order matters) reports that
`required-tests.json` differs (quote it).

**Implement:**
1. `required-tests.json`: exactly the same additions, appended at the end of each array (resulting lengths `all` 248,
   `windows` 11, `macos` 35, `linux` 30, `vitest` 29 unchanged; today 224 / 10 / 28 / 25 / 29, counted with `node` this
   session; check with `node -e` and record). No name of a test that does not exist: verify each `cargo.all` and
   `cargo.windows` name against `cargo test --workspace --locked -- --list 2>&1 | tee F:/tmp/gate/km8-T007/list.txt`
   (shared target; Rule 7): every name appears exactly once across the binaries; the `macos` / `linux` names against
   `grep -n "fn <name>"` in `extract_tests.rs` under the right `cfg` (`#[cfg(unix)]` for the `_unix` names,
   `#[cfg(target_os = "macos")]` for the `_macos` names).
2. Run the `required tests ran` step locally: `cargo test --workspace --locked 2>&1 | tee
   F:/tmp/gate/km8-T007/cargo-test.txt`, `npm test -- --reporter=default --reporter=json
   --outputFile.json=F:/tmp/gate/km8-T007/vitest.json`, then the step's `run:` text through Git Bash with
   `RUNNER_TEMP=F:/tmp/gate/km8-T007` and `RUNNER_OS=Windows` (the step's script lives in the workflow; extract it into a
   file with a throwaway Node script, Rule 13) → exactly `::notice title=required tests ran (Windows)::259 cargo + 29
   vitest` and no `::error` (`symlink_entry_unsupported_windows` among the 259, so SC-023's Windows evidence is local as
   well as CI).

**Done when:** L-NODE `# fail 0`, L-TAP `TAP plan=1..59 top-level=59`, L-LINT green; the notice recorded. **Lane:**
L-NODE, L-TAP, L-LINT.

### T008 — INDEX, lints, scope, greps, cleanup, the PENDING-CI list and the close hand-over (spec FR-090, FR-091, SC-001 local part; plan §7.3, §7.4, §8)

**Files:** `specs/INDEX.md` (regenerated only). Nothing else is edited; this task runs and records.

**Test first:** `node tools/gen-specs-index.js --check` (quote its exit code and, if red, the first differing line: Phase
8 is not yet in `specs/INDEX.md`, whose last Krate Manager row is Phase 7 at `:208`).

**Implement:**
1. `node tools/gen-specs-index.js`, then `--check` → exit 0.
2. Repo lints: L-LINT (`node tools/lint-serial-test-runs.js`), L-VLINT. `node tools/check-portability.js` and `node
   tools/lint-plugin-editor-baseline.js` are **not** run: no C++ file and no plugin editor changed (Rule 11); record the
   evidence, `git diff --name-only 2499fb13` plus `git status --porcelain`, showing nothing under `plugins/`, `dsp/`,
   `cmake/`, `extern/`, `.github/`.
3. L-SCOPE (T006's script) → exit 0.
4. The Rule 9 grep: no `todo!(` or `unimplemented!(` in `crates/` or `src-tauri/src` outside `*_tests.rs` and `tests/`;
   no `_private` left in the Phase 8 doubles of `test_support.rs` (`grep -n "_private" crates/core/src/test_support.rs`
   holds only lines that predate `2499fb13`, checked against `git show 2499fb13:…`). The Rule 2 grep: `git diff -U0
   2499fb13 -- apps/krate-manager | grep -n '^+.*#!\?\[\(expect\|allow\)('` empty, and the same over the two new files
   (`extract.rs`, `extract_tests.rs`) read whole.
5. Check the PENDING-CI list below against `required-tests.json` (every `cargo.macos` / `cargo.linux` / `cargo.windows`
   name of FR-086 appears in a row) and record "PENDING-CI: N item(s)" with N = the row count.
6. Scratch cleanup: delete `F:/tmp/km8-target/` (if a slot is left) and the `F:/tmp/gate/km8-*` logs once their results
   are in the notes; `node tools/clean-test-output.js --apply` (repo rule) runs inside L-CLOSE (row C3).

**Done when:** L-INDEX, L-LINT, L-VLINT, L-SCOPE green; step 4's greps empty; the notes end with "deferred to the main
loop at close: run `node specs/krate-manager-phase8/evidence/close-lane.js`" (L-WSL and the full plan §7.4 procedure;
Phase 7's took 157 s with a `tauri build` this phase does not run, plus this phase's cold WSL compile of the module), and
the main loop then writes the roadmap's Phase 8 status line `**Status: ✅ COMPLETE (PENDING-CI: N item(s)) (<date>)** —
see specs/krate-manager-phase8/compliance.md` under `### Phase 8: Safe Extraction and Staging (Linux tar.gz)` (roadmap
line 634, the form of line 601). **Lane:** L-INDEX, L-LINT, L-VLINT, L-SCOPE.

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

Every row closes only on a pushed commit's `manager-ci.yml` run (plan §7.3 `plan.md:839-855`): the committed
`tests/required-tests.json` lists the name, the leg's `required tests ran` step passed with no `::error title=required
tests ran (<os>)::…` line, and its notice reads `len(cargo.all) + len(cargo.<os>)` cargo and `len(vitest)` vitest:
**`::notice title=required tests ran (macOS)::283 cargo + 29 vitest`** on `check (macos-latest)`,
**`::notice title=required tests ran (Linux)::278 cargo + 29 vitest`** on `check (ubuntu-24.04)` and
**`::notice title=required tests ran (Windows)::259 cargo + 29 vitest`** on `check (windows-latest)` (plan §4.7 totals).
The evidence for each name is the step `cargo test` (`cargo test --workspace --locked`, `manager-ci.yml:117-120`). A red
there is a Phase 8 defect fixed before close (the `cfg(unix)` / macOS code is compiled for the first time on those legs;
L-WSL is only an early signal for the Linux rows).

| # | SC | Job | Assertion that closes it |
|---|---|---|---|
| 1 | SC-001 (macOS) | `check (macos-latest)` | steps `cargo fmt`, `clippy`, `clippy dev-feed`, `clippy-guard` (`rc≠0`, `disallowed=3`, `other-errors=0`, and no `--locked` refusal: the TA-1 lock), `cargo test`, `typecheck`, `lint`, `vitest`, `tauri build`, `build warnings` (`warnings=0`) green (TA-11: the release build is CI-only this phase) |
| 2 | SC-001 (Linux) | `check (ubuntu-24.04)` | the same steps green |
| 3 | SC-001 (Windows CI), SC-023 | `check (windows-latest)` | the same steps green and the `259 cargo + 29 vitest` notice, including `extract::tests::symlink_entry_unsupported_windows ... ok` once (the junction block (j1), (j2), PD-6 / PD-7's Windows branch; also run locally by T003 / T007) and `extract::tests::entry_path_rejects_every_bad_form ... ok` (its `cfg(windows)` reserved-name block) |
| 4 | SC-002 to SC-010, SC-014 to SC-021, SC-024, SC-027 to SC-031 on APFS (the 24 `cargo.all` names) | `check (macos-latest)` | the `283 cargo + 29 vitest` notice, no `::error`; every path check runs under `/var/folders/…` (the `/var` → `/private/var` link, roadmap line 655) |
| 5 | the same on ext4 | `check (ubuntu-24.04)` | the `278 cargo + 29 vitest` notice, no `::error` |
| 6 | SC-004 real modes | both legs | `test extract::tests::kat_archive_stages ... ok` (its `#[cfg(unix)]` block: `0o644` files, `0o755` dirs, umask-independent) |
| 7 | SC-011 | both legs | `test extract::tests::posix_modes_and_links_unix ... ok` once (exec bits per the listing, `read_link`, rebuilt digest `baad7502…`) |
| 8 | SC-012 (1), (2) | both legs | `test extract::tests::escape_cases_unix ... ok` and `test extract::tests::symlinked_staging_root_unix ... ok` once each (incl. the dangling-link and link-outside-its-top rows, FR-046) |
| 9 | SC-022 | both legs | `test extract::tests::special_bits_and_mtime_not_applied_unix ... ok` once |
| 10 | SC-029 real link, SI-9 production body | both legs | `test extract::tests::prune_removes_real_link_unix ... ok` once (`stage` → `E_EXTRACT_IO { step: "create_work_dir", kind: InvalidInput }`, canary snapshot unchanged) |
| 11 | SC-012 (3) | `check (macos-latest)` | `test extract::tests::raw_temp_dir_staging_root_macos ... ok` once (both sides canonicalised under the raw `/var/folders` spelling) |
| 12 | SC-013 | `check (macos-latest)` | `test extract::tests::staged_tree_has_no_xattrs_macos ... ok` once (`/usr/bin/xattr -lr <root>` prints nothing) |
| 13 | SC-014 per OS | all three legs | `test extract::tests::case_fold_collision ... ok`: macOS and Windows on the case-insensitive branch (`E_EXTRACT_COLLISION`), Linux on the case-sensitive branch (both files); the precondition assertion fails rather than taking the other branch |
| 14 | SC-020 per-OS blocks | `check (macos-latest)`, `check (ubuntu-24.04)` | `test extract::tests::entry_path_rejects_every_bad_form ... ok` (the `cfg(not(windows))` acceptance block, and its `stage` of `presets/aux.vstpreset` over `forwarding(&FsExtractIo)`) |
| 15 | SC-018 / FR-081 (tree) | all three legs | step `core dependency tree`: `::notice title=core dependency tree (<os>)::<n> crates, none excluded`, no `::error` (`flate2` counted; no `tar`) |

L-WSL (TA-10) gives an early local signal for rows 2, 5-10 and 14 before the push; it closes none of them.

---

## Defects found (in the plan, while writing this list)

- **D-1 (TA-12).** Plan §4.6 (`plan.md:691`) puts the `flate2 = "1"` TOML plant in the "existing TOML-plant test,
  `injection_scanner_reports_planted_violations`". `grep -n RULE_TOML_KEY crates/core/tests/injection.rs` this session
  finds the only TOML plant at `:3583`, inside `ureq_is_confined_to_http_rs` (`:3481`); T005 adds the plant there.
- **D-2 (TA-4).** Plan §4.5 (`plan.md:634`, `:649`) gives `MemExtractIo` a `Mutex<MemFs>` and the wrappers an `inner:
  &'a dyn ExtractIo`. `ExtractIo::create_file` / `open_archive` return `'static` boxes (plan §4.3.2 `plan.md:290`,
  `:296`), so a returned file cannot borrow its creator's state, and `&dyn ExtractIo` is not `Send`, so SC-024's scoped
  threads cannot share a wrapper. The state moves into `Arc<Mutex<…>>` and the inner becomes `&'a (dyn ExtractIo +
  Sync)`.
- **D-3 (TA-3).** Plan §4.5 names the doubles' powers but not their constructors, builders or node accessors
  (`plan.md:639-640` "builder methods to plant nodes … accessors to read nodes back"), and its `FaultyExtractIo` is a
  field literal; two parallel writers need one API, which T001 fixes.
- **D-4 (TA-7).** The plan's `test_support.rs` additions do not mention that the file is in `FR088_FILES`
  (`injection.rs:245-252`), whose eleven patterns (`:254-266`) apply to comments too.
- **D-5 (TA-1).** Plan §4.1 / SI-3 (`plan.md:233`, `:54`) re-lock with `cargo update --workspace --manifest-path …`;
  the workflow's ruled command is `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version
  1 > /dev/null`. T001 uses it; T002 writes it into FR-080.
- **D-6 (TA-2).** Plan §4.2 adds `pub(crate) Sha256Stream` whose only reader is `extract.rs`; as a foundation item it
  would warn `dead_code` at the G1 gate unless `CanonicalListing::digest` is written for real in G1, which TA-2 does.
- **D-7 (minor, no content impact).** Plan §1.1 cites `test_support.rs:376-543` for `FaultyStagingIo`
  (`plan.md:99`); at `2499fb13` `pub enum Op` is at `:378` and `impl StagingIo for FaultyStagingIo` at `:500` (file 587
  lines). Plan §1.1 records no line endings; Rule 12 holds this session's measurement (several core sources are LF,
  unlike Phase 7's CRLF-only set).
