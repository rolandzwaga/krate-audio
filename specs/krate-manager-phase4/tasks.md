# Tasks: Krate Manager Phase 4 — Scaffold, Platform Abstraction and the Manager's CI

**Spec:** `specs/krate-manager-phase4/spec.md` (D1-D24, FR-001 to FR-107, SC-001 to SC-031; Clarifications Q1-Q7,
2026-10-30) · **Plan:** `specs/krate-manager-phase4/plan.md` (PD-1 to PD-27, SI-1 to SI-11, §10 open question 1)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 4 (lines 463-519); Decisions Q1, Q3, Q5, Q6, ROSTER (lines 24-33);
Cross-Cutting Constraints (lines 961-1002)
**Status:** DRAFT task list, 2026-10-30, written against `feat/krate-manager` at `10593783` (untracked at start:
`specs/krate-manager-phase4/`; `ls apps` this session: no such directory)
**Lanes in this phase:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm
typecheck / lint / test, the version lint) and the **pipeline lane** (`node --test` over `tools/feed-contract/tests/`,
YAML parse, the repo lints). No file under `plugins/`, `dsp/`, `tests/` or CMake changes (spec FR-090), so there is **no
C++ lane**: no `run-gate.js`, no `header-consumers.js`, no `check-portability.js` (plan §8). `npm run tauri build` is a
main-loop lane (T019).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks in parallel; every task in it creates only new files of its
  own, and no two tasks in it share a file. Every other group is **sequential**: its tasks run one after another in the
  order listed. Every task that creates or edits a shared file (any `Cargo.toml`, `Cargo.lock`, `clippy.toml`,
  `crates/core/src/lib.rs`, `platform/mod.rs`, `package.json`, `tauri.conf.json`, the capability file, a workflow,
  `hygiene.test.js`, `spec.md`, the roadmap) sits in a sequential group.
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block of text is too long to copy without drift risk
  (a manifest, the workflow, the test-name list), the section names the exact `plan.md` section that holds it; that
  section is then **binding, verbatim**. Where this file and the plan differ, this file wins and says why (the TA rows of
  T001).
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the task's notes quote the failing line.
  For Rust, the red run uses a module whose public items exist with `todo!()` bodies, so the test compiles and fails on
  its assertions or on the `todo!()` panic. A `#[should_panic]` test always names `expected = "<substring>"` (given in
  its task), so a `todo!()` panic ("not yet implemented") fails it instead of passing it.
- **Expected reds.** A test written in one task that can only go green in a later one is named in its task under
  "Expected red until", with the task that turns it green. Any other red at a gate is a real red.
- **Main-loop lane.** Only T019's `evidence/close-lane.js` holds steps that may run past ~5 minutes (the release
  `npm run tauri build` with LTO and the NSIS toolset download, plus the full §7.4 close procedure). T019 writes it and
  returns with notes "deferred to the main loop at close: run `node specs/krate-manager-phase4/evidence/close-lane.js`".
  The first debug compile of the Tauri tree (T012) is part of implementing, not a measurement; T012 records its wall
  clock in its notes.
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system.** Rust tests get a `Roots` only from `test_support::fresh_temp_roots()` /
   `temp_roots()` (clippy makes every other constructor an error, FR-015) and write only through APIs that take that
   `&Roots` (plan PD-25). No test reads the process environment except D6 site 7 (`CI`). No test, task or gate writes
   `%LOCALAPPDATA%\com.krateaudio.krate-manager`, `~/Library/Application Support/com.krateaudio.krate-manager`, a VST3 /
   Components / preset folder, `ProgramData` or the registry, and none launches the built app, the NSIS installer or the
   helper elevated (spec Safety, FR-076). Test vectors name only `C:\km-fixture\…`, `/Users/u`, `/home/u` or paths inside
   a fresh `TempDir` (spec Safety). Network use is limited to package registries (crates.io, npm) and the read-only
   `gh api` template reads of T003 and T012; no test touches the network.
2. **The seven D6 suppression sites** (plan §4.1 table) are the only `clippy::disallowed_methods` suppressions in the
   workspace, each `#[expect(clippy::disallowed_methods, reason = "<the plan's text>")]` on one statement. Never
   `#[allow]` anything (`allow_attributes` and `allow_attributes_without_reason` are denied, FR-002); never an `expect`
   of `clippy::style`, `clippy::all` or `warnings` (plan PD-22). Which task adds which site: sites 4-7 T004, sites 1-3
   T012.
3. **Unit tests live in `<stem>_tests.rs`**, declared in `<stem>.rs` as `#[cfg(test)] #[path = "<stem>_tests.rs"] mod
   tests;` (FR-021). Never an inline `mod tests {` in a `crates/core/src/` file.
4. **Literal hygiene in core** (FR-021, plan §4.10; enforced from T014 on, obeyed from T004 on): in every
   `crates/core/src/` file except `test_support.rs` and `*_tests.rs`, outside `platform/`: none of the raw substrings
   `"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`, `krateaudio.com`, `api.github.com` (comments included). In
   `platform/` non-test files: every string literal outside an attribute is a member of `PLATFORM_LITERALS` (plan
   §4.10), no raw string outside an attribute, so no `expect("…")`, `format!("…")` or error text there. In `platform/`
   and `roots.rs`: none of the identifiers `fs`, `File`, `OpenOptions`, `canonicalize`, `metadata`,
   `symlink_metadata`, `exists`, `try_exists`, `is_dir`, `is_file`, `read_dir`, `read_link`, `create_dir`,
   `create_dir_all` in code. The identifier `sandboxed` appears only in `roots.rs` and `test_support.rs`.
5. **Zero warnings.** `cargo fmt --all --check`, `cargo clippy … -- -D warnings` with 0 `warning:` lines, `npm run
   typecheck`, `npm run lint` (`--max-warnings 0`), `node --check`. No `// @ts-ignore`, no `@ts-nocheck`, no
   `eslint-disable` without ` -- <reason>` (FR-045 scans `src/` and `index.html` for them).
6. **Parallel tests.** Never `--test-threads`, `--no-file-parallelism`, `--fileParallelism=false`, `--maxWorkers=1`,
   `-j 1`, `fileParallelism: false`, a `pool` / `maxWorkers` override, `--test-concurrency=1` or
   `--test-isolation=none` (FR-076, SC-021). `cargo test` uses its default threads; Vitest and `node --test` run files in
   parallel.
7. **`cargo test` is `cargo test --workspace --locked`, never with `--all-targets`** (TA-2: measured this session,
   `--all-targets` builds and runs the lib and bin test harnesses of a package whose `[lib]` and `[[bin]]` set `test =
   false`, which would link `tauri` into a test binary, the D12a hazard). `cargo clippy --workspace --all-targets` is
   correct (it checks, it never links or runs a test binary).
8. **Lockfiles.** After a task adds a workspace member or a dependency, it updates `Cargo.lock` once without `--locked`
   (`cargo metadata --format-version 1 > /dev/null`, which resolves only what changed), then runs every gate command
   with `--locked`. Never `cargo update` or `cargo generate-lockfile` after T002. `package-lock.json` is generated once
   from an empty `node_modules` (FR-050, npm/cli#4828) and never regenerated with `node_modules` present.
9. **Node files** (`tools/lint-manager-version.js` and the two new tests): CommonJS, `'use strict'`, LF line endings;
   every test file's first statement after an optional `'use strict';` is `require('./helpers/write-guard.js');`
   (`tools/feed-contract/tests/hygiene.test.js:80`, subtest (b) `:150-158`); tests write only under
   `fs.mkdtempSync(path.join(os.tmpdir(), 'km4-<name>-'))`, removed in `t.after` / `finally`. No `http`, `https`,
   `http2`, `net`, `tls`, `dgram` module and no `fetch(` (not even in a comment), no `child_process` (none is
   `CHILD_EXEMPT`, `hygiene.test.js:71-78`), and `fs` called through the module object, never destructured
   (`hygiene.test.js:59-70`). No `concurrency: 1|false` text in a test file (`hygiene.test.js:612-620`).
10. **No plugin roster (ROSTER).** No file under `apps/krate-manager/` and no new Phase 4 Node file names any of the nine
    shipped plugins, in code, comments or fixtures (spec Traceability, ROSTER row).
11. **Scope fence (spec FR-090).** Outside `apps/krate-manager/**` only these paths change:
    `.github/workflows/manager-ci.yml` (new), `.github/workflows/feed-tools.yml`, `tools/lint-manager-version.js` (new),
    `tools/feed-contract/tests/lint-manager-version.test.js` (new), `tools/feed-contract/tests/manager-ci-shape.test.js`
    (new), `tools/feed-contract/tests/hygiene.test.js`, `specs/krate-manager-phase4/**`, `specs/INDEX.md`
    (regenerated), and in `specs/Krate-manager-roadmap.md` only the four FR-090 places (T018; the status line is the main
    loop's at close).
12. **Line endings of edited files.** This checkout runs `core.autocrlf=true` (`git config core.autocrlf` = `true`). The
    edited pre-existing files are CRLF in the working tree (measured 2026-10-30, `grep -c $'\r$'` = `wc -l`):
    `feed-tools.yml` 144/144, `hygiene.test.js` 622/622, `Krate-manager-roadmap.md` 1038/1038, `spec.md` 1265/1265. An
    edit keeps the file's ending on every line it adds (after the edit `grep -c $'\r$' <file>` equals `wc -l < <file>`).
    New files are written LF. `specs/INDEX.md` is pinned `eol=lf` (`.gitattributes:18`) and written only by
    `tools/gen-specs-index.js`.
13. **Shell discipline.** Every command runs in the Bash tool (Git Bash), foreground, output to a log under
    `F:/tmp/gate/km4-<task>/`, then `tail`; never `cat` a whole log, never a drive-wide `find` / `du`. No heredoc in a
    Bash command (a repo hook blocks them): write a script with the Write tool and run it with `node`.

### Lanes (referenced by every task)

All app-lane commands run from `apps/krate-manager/`. Until T012 creates `src-tauri/`, there is no
`src-tauri/Cargo.toml`: run the cargo commands from `apps/krate-manager/` (the root `Cargo.toml` is the workspace) and
report `src-tauri` as "not yet created (later group)".

| Lane | Commands | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked` (Rule 7) | fmt exit 0; clippy exit 0 with 0 `warning:` lines (every `#[expect]` fulfilled); every test binary `0 failed` except the task's named expected reds |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` (from T012) | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (plan §4.16), executed in Git Bash from `apps/krate-manager/` with `RUNNER_TEMP=F:/tmp/gate/km4-<task>` and `RUNNER_OS=Windows` (from T013; until T016 creates the workflow, the same lines are copied from plan §4.16 into a scratch script outside the repo) | prints `::notice title=clippy-guard (Windows)::rc=<non-zero> disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=<n≥1> other-errors=0` (record the actual `rc`) |
| **L-TS** | `npm ci` (when `node_modules` is absent or `package-lock.json` changed); `npm run typecheck`; `npm run lint`; `npm test` (from T003) | each exit 0; lint reports 0 problems; Vitest reports every test passed |
| **L-XCHECK** | only under ruling `[P-3]` (a) and only when `rustup target list --installed` lists both targets: `cargo clippy -p krate-manager-core --all-targets --locked --target x86_64-unknown-linux-gnu -- -D warnings` and the same with `--target aarch64-apple-darwin` | exit 0, 0 `warning:` lines. A compile proxy for the `cfg`'d macOS / Linux code: it never closes a PENDING-CI row. A failure whose cause is the toolchain (a build script that needs a C compiler for the target) is recorded verbatim and the lane is skipped; a code error is a red |
| **L-NODE** | `node --check <each new or changed .js>`; the task's own file: `node --test --test-reporter=tap "tools/feed-contract/tests/<file>.test.js" > F:/tmp/gate/km4-<task>/own.tap`; the whole set: `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km4-<task>/all.tap` (quoted glob; files in parallel child processes); read with `tail -12` | every `node --check` exits 0; TAP `# fail 0` except named expected reds; no `Warning:`, `DeprecationWarning` or `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km4-<task>/all.tap` (file mode: it extracts the live `TAPCHECK` heredoc from `feed-tools.yml` and runs it with `CI=1`, `tap-check.js:1-11`) | exit 0 and `TAP plan=1..59 top-level=59` (from T017 on; before T017 the live heredoc still says `1..57`, so L-TAP is not run) |
| **L-YAML** | `npx --yes yaml@2.6.1 valid < <workflow file>` (the version the phase workflow's gate runs) | exit 0 |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root; from T015) | exit 0, no stderr |
| **L-SCOPE** | `node specs/krate-manager-phase4/evidence/scope-check.js` (from T018) | exit 0 |
| **L-CLOSE** | `node specs/krate-manager-phase4/evidence/close-lane.js` (from T019; **main loop only**) | last line `CLOSE LANE GREEN` |

---

## Preconditions — rulings needed before the build stage

The plan leaves one choice to the user (§10.1) and eleven spec edits for the main loop (§0.2); this file adds four
amendments of its own (TA rows, T001) and two environment questions. **T001 stops the build if the record below is
missing.** The main loop records each ruling in `spec.md` `## Clarifications` under a new `### Session 2026-10-30
(plan)` heading, one bullet per item, `Q: … -> A: …`, tagged `[P-1]` to `[P-4]`.

- **P-1: accept the plan's spec edits SI-1 to SI-11 and this file's TA-1 to TA-8** (T001's tables), so T001 writes the
  spec-changing ones into the rows the compliance stage checks. None relaxes a threshold. Recommended: accept all.
- **P-2 (plan §10.1): one manual launch of the built app on Windows before close?** Nothing in Phase 4 runs the app
  (FR-076), so the CSP, the navigation guard, the window and the "already running" path are proven statically only
  (SC-015, SC-016, SC-017, SC-026). (a) Close without a launch; (b) after L-CLOSE the user launches
  `apps/krate-manager/target/release/krate-manager.exe` once, then a second time while the first is open, which creates
  the real state dir `%LOCALAPPDATA%\com.krateaudio.krate-manager` (with `manager.lock`, `logs/` and WebView2 data). No
  task launches it under either answer; under (b) compliance records the user's observation as a user check, not as a
  test. Recommended: (a); the first screen worth auditioning is Phase 6's status screen, and (b) is cheap to do then.
- **P-3: may the build install two Rust cross-compilation targets on this box** (`rustup target add
  x86_64-unknown-linux-gnu aarch64-apple-darwin`, standard-library components of the existing stable toolchain, about
  tens of MB each; not a second toolchain)? Under (a) L-XCHECK compile-checks the `cfg(target_os = "macos" | "linux")`
  code of `krate-manager-core` locally before the user's push, catching a typo in `macos.rs` / `linux.rs` that the
  Windows build never compiles; it proves nothing at run time and closes no PENDING-CI row. Under (b) L-XCHECK is
  skipped everywhere and the macOS / Linux code is first compiled on the hosted runners. Recommended: (a). No task runs
  `rustup target add`; the main loop does, once, after the ruling.
- **P-4: the phase workflow's generic app lane runs `cargo test --manifest-path src-tauri/Cargo.toml --all-targets`**
  (`.claude/workflows/krate-manager-phase.js:590`). With this phase's `test = false` on `src-tauri` (D12a), that flag
  builds and runs a test harness for the `src-tauri` lib and bin that links `tauri` (TA-2), which the spec designed out
  because such a binary can fail to load on Windows (`STATUS_ENTRYPOINT_NOT_FOUND`, tauri#13419). (a) The main loop
  edits that one line of the workflow to `cargo test --workspace --locked` (from `apps/krate-manager/`) before starting
  the build stage (a changed prompt only affects calls after it, and no build-stage call exists yet); or (b) leave the
  workflow, and gates treat a load failure (`0xc0000139`) of a `krate_manager_lib-*.exe` / `krate_manager-*.exe` test
  harness under `--all-targets` as the documented D12a hazard (an expected red, Rule 7), never as a defect of this
  phase. Recommended: (a). No task edits the workflow file (it is outside FR-090).

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | sequential | T001 rulings + spec rows | Preconditions |
| G2 Scaffold | sequential | T002 Rust workspace skeleton + helper stub · T003 frontend toolchain + window shell | G1 |
| G3 Core modules | sequential (each edits `crates/core/src/lib.rs`) | T004 `roots` + `test_support` · T005 `platform` · T006 `settings` · T007 `state_lock` · T008 `log_sink` · T009 `navigation` · T010 `context` | G2 |
| G4 App shell | sequential (manifests, `tauri.conf.json`, capability) | T011 `config_guard.rs` (red first) · T012 `src-tauri` · T013 clippy-guard fixture | G3 |
| G5 Guards and the version lint | **[P]** | T014 `crates/core/tests/injection.rs` · T015 `tools/lint-manager-version.js` + its test | G4 |
| G6 CI | sequential (workflows, `hygiene.test.js`) | T016 `manager-ci.yml` + `required-tests.json` + the shape test · T017 `feed-tools.yml` + `hygiene.test.js` | G5 |
| G7 Integration | sequential | T018 roadmap edits, `specs/INDEX.md`, `evidence/scope-check.js` · T019 close lane, short close run, PENDING-CI | G1-G6 |

**SC owners:** SC-001 T019 (L-CLOSE; every gate runs its non-build half) · SC-002, SC-003, SC-004 T005 · SC-005 T004
(a) + T005 (b) · SC-006 T013 (local evidence), T016 (CI step) · SC-007 T004 · SC-008 T014 · SC-009, SC-010, SC-011 T006 ·
SC-012 T015 · SC-013 T016 (PENDING-CI) · SC-014 T016 + T019 · SC-015 T011 + T012 · SC-016 T009 · SC-017 T003 · SC-018
T002 · SC-019 T016 · SC-020 T016 + T019 · SC-021 T016 (shape) + T019 (grep) · SC-022 T017 · SC-023 T018 + T019 · SC-024,
SC-026, SC-028, SC-030, SC-031 (Rust part) T010 · SC-025 T007 · SC-027 T004 (roots part) + T010 (context part) · SC-029
T008 · SC-031 (CI part) T016 + T019. The plan's added tests (SI-11) belong to the task that owns their module.

---

## G1 — Foundation (sequential)

### T001 — Rulings check and the plan's spec amendments into `spec.md`

**Files:** `specs/krate-manager-phase4/spec.md` (edit; CRLF, Rule 12).

**Precondition (STOP AND SURFACE).** Read `spec.md` `## Clarifications`. If it lacks a `### Session 2026-10-30 (plan)`
bullet for each of `[P-1]`, `[P-2]`, `[P-3]` and `[P-4]` (Preconditions above), return status `blocked` with notes
quoting the four items verbatim from this file, and change nothing.

**Test first.** Docs-only task: no test file. Before editing, record in the notes which rows below already carry the
change (expected: none; `grep -n "apply_settings\|MissingRoot\|NotNormal\|sandbox\|should panic" spec.md` prints no
FR/SC row). Also record `rustc --version` (expected `rustc 1.99.1 (58cf8f9c3 2026-10-13)`, measured this session) and
`node --version` (`v24.7.0`), which SI-6 writes.

**Implement.** Edit each row in place, appending `(plan PD-n)`, `(plan SI-n)` or `(tasks TA-n)` to the changed
sentence. Plan §0.1 / §0.2 are the binding source of the SI wording; the TA rows are below.

| Row(s) | Change | Source |
|---|---|---|
| FR-077 | "exactly one line `test <name> ... ok` or `test <name> - should panic ... ok`" | SI-1, PD-12 |
| FR-103, SC-028 | add `context::apply_settings(shared: &SharedState, settings: Settings)` (read guard dropped before the write guard is taken) to FR-103; SC-028's last case reads "after `context::apply_settings(&shared, s)`" | SI-2, PD-2 |
| D18 | the TOML subset adds `key = true` / `key = false` and `[[table]]` headers | SI-3, PD-13 |
| FR-013, SC-005 | `RootsError::MissingRoot { kind }` for a missing `Vst3User` or `Vst3Global` root; SC-005 rejects both | SI-4, PD-4 |
| Existing repo pieces, `ci.yml` row | cite `:41-43` | SI-5 |
| External facts, "This box" row | `rustc` / `cargo` 1.99.1, clippy 0.1.99 (the values T001 recorded) | SI-6 |
| FR-017, FR-103 | "`run()` assembles the state before the builder and registers it with `tauri::Builder::manage(SharedState::new(state))`; `.setup` only schedules the locked-instance exit" | SI-7, PD-1 |
| FR-013, FR-083, SC-027 | the private `sandbox: Option<PathBuf>`, set by `test_support::temp_roots_in` through the crate-private `Roots::sandboxed`; `with_configured_plugin_root` on a sandboxed value panics outside the sandbox base; FR-083's parenthesis; SC-027's sandbox case (direct, through `assemble_with_os`, through `reassemble_with_os`) | SI-8, PD-24 |
| FR-017, FR-031, FR-032, FR-100, FR-104, FR-105 | the `&Roots` signatures of plan SI-9 | SI-9, PD-25 |
| FR-012, FR-013, FR-033, FR-102, SC-005, SC-010, SC-027 | normal paths: a `..` component or no normal component rejected (`Env { var }`, `NotNormal { which }`, `Invalid`), with the plan §7.1 cases | SI-10, PD-26 |
| SC-008, SC-015, SC-027, SC-029 | the added tests of plan SI-11 (named there); FR-077's `all` list holds them | SI-11 |
| FR-071, SC-001 | after `cargo test --workspace --locked` add: "never with `--all-targets`, which builds a test harness for `src-tauri`'s lib and bin despite `test = false` (D12a)" | TA-2 |
| SC-023 | the scope check lists modified **and untracked** paths (`git diff --name-only c66c8557` plus `git ls-files --others --exclude-standard`), and the roadmap `git diff -U0` hunks touch only the four FR-090 places | TA-5, plan §4.19 |
| FR-001 | "generated from" reads "built from the `template-vanilla-ts` and `_base_` files at create-tauri-app commit `c6f5cf68`, the commit External facts cites, read with `gh api`" | TA-6 |
| FR-031 | `validate` checks UTF-8 first, so a non-UTF-8 value is `NotUtf8` even when it is also relative | TA-7 |

The TA rows that change no spec text (their final state equals the spec), recorded here so the reviewer sees why a task
differs from a plan block:

| Id | Amendment | Why |
|---|---|---|
| TA-1 | `clippy.toml` grows with the items it names: T002 writes the six `std::env` entries, T004 adds `krate_manager_core::roots::Roots::new`, T012 adds `krate_manager_lib::os_roots::FromOs::from_os`. The final file is plan §4.1's exactly | A `disallowed-methods` path naming a missing item of a resolvable crate prints `warning: … does not refer to a reachable function` (spec External facts, row "An unresolvable `disallowed-methods` path"), which would make the G2 and earlier G3 gates show a warning line |
| TA-2 | Rule 7 | Measured 2026-10-30 (cargo 1.99.1, scratch crate whose `[lib]` and `[[bin]]` set `test = false`, deleted after): `cargo test --all-targets` printed `Running unittests src\lib.rs` and `Running unittests src\main.rs`; plain `cargo test` ran no harness |
| TA-3 | Workspace `members` grow: T002 `["crates/core", "crates/helper"]`, T012 adds `"src-tauri"` (final = FR-002) | Cargo refuses a workspace whose member directory has no `Cargo.toml` |
| TA-4 | `crates/core/src/lib.rs` gains one `pub mod` line per G3 task, in dependency order (final content in T010) | Each module task compiles and tests on its own |
| TA-8 | The helper stub's stderr is the D20 line plus `\n` on every OS (plan §4.11 says "the platform newline") | `eprintln!` is `println!` to stderr, and "On all platforms, the newline is the LINE FEED character (`\n`/U+000A) alone" (https://doc.rust-lang.org/std/macro.println.html) |

**Done when:** every row of the first table carries its change and its source tag; the notes list each edited row id
and the two recorded versions. **Lane:** none (docs).

---

## G2 — Scaffold (sequential)

### T002 — Rust workspace skeleton and the helper stub (spec FR-001 part, FR-002, FR-003, FR-006, FR-061, FR-062, D4, D20, SC-018; plan §4.1, §4.11, PD-14, PD-15, TA-1, TA-3, TA-8)

**Files (all new, under `apps/krate-manager/`):** `Cargo.toml`, `Cargo.lock`, `clippy.toml`, `rust-toolchain.toml`,
`.gitignore`, `version.json`, `CHANGELOG.md`, `crates/core/Cargo.toml`, `crates/core/src/lib.rs`,
`crates/helper/Cargo.toml`, `crates/helper/src/lib.rs`, `crates/helper/src/main.rs`, `crates/helper/tests/stub.rs`.

**Test first.** `crates/helper/tests/stub.rs`, one test `helper_stub_exits_2_with_one_line`: for each argument list
`[]` and `["--anything"]`, run `std::process::Command::new(env!("CARGO_BIN_EXE_krate-manager-helper"))` with
`.output()`, and assert `output.status.code() == Some(2)`, `output.stdout.is_empty()`, and
`output.stderr == b"krate-manager-helper: no operations in this build\n"` (TA-8). Red first: with
`src/main.rs` = `fn main() {}`, `cargo test --workspace` fails (exit code `Some(0)`); quote the assertion line.

**Implement.**
- Root `Cargo.toml`: plan §4.1 verbatim except `members = ["crates/core", "crates/helper"]` (TA-3). `[profile.release]`
  sits here only (PD-14). `[workspace.dependencies]` already lists `tauri` and `tauri-build`; unused entries are fine.
- `clippy.toml`: plan §4.1's six `std::env::*` entries only, with their `reason` (TA-1).
- `rust-toolchain.toml`: `[toolchain]` `channel = "stable"`, `components = ["clippy", "rustfmt"]`, `profile =
  "minimal"` (D4). `rustup show` from `apps/krate-manager/` must name the stable toolchain as active "overridden by
  …rust-toolchain.toml"; record it.
- `.gitignore`: the five PD-15 lines `/target/`, `/dist/`, `/src-tauri/gen/`, `/src-tauri/permissions/autogenerated/`,
  `/tests/fixtures/clippy-guard/target/`.
- `version.json`: `{"version": "0.1.0", "name": "Krate Manager"}` plus final LF (FR-061), two-space JSON like
  `plugins/vorago/version.json`.
- `CHANGELOG.md`: lines 1-6 of `plugins/vorago/CHANGELOG.md` with `Vorago` replaced by `Krate Manager` (read this
  session: `# Changelog`, blank, `All notable changes to Vorago will be documented in this file.`, blank, the two Keep a
  Changelog / SemVer lines), a blank line, `## [0.1.0] - Unreleased`, blank, `### Added`, blank, `- Scaffold: Tauri 2
  app, platform abstraction, settings model and the manager CI.` (FR-062, plan §4.1).
- `crates/core/Cargo.toml`: plan §4.1 verbatim (FR-007's six dependencies, `[features] test-support = []`, `[lints]
  workspace = true`). `crates/core/src/lib.rs`: a crate doc comment only (`//! krate-manager-core: platform roots,
  settings, startup state (Tauri-free).`); no `krateaudio.com` text (Rule 4).
- `crates/helper/`: plan §4.11 (`Cargo.toml` with workspace-inherited fields, `[lints] workspace = true`, no
  `[dependencies]`, `[dev-dependencies] krate-manager-core = { workspace = true, features = ["test-support"] }`;
  `src/lib.rs` a crate doc comment only; `src/main.rs` the plan's five lines).
- `Cargo.lock`: `cargo generate-lockfile` once from `apps/krate-manager/` (the only time, Rule 8).

**Zero warnings / green:** L-RS. Then `git -C ../.. status --porcelain -- apps/krate-manager` lists only the files
above (no `target/`: the `.gitignore` works); record it.

**Done when:** L-RS green; the stub test passes; the notes hold the red line, `rustup show` and the status listing.

### T003 — Frontend toolchain and the window shell (spec FR-046, FR-050 to FR-054, SC-017, D1-D3; plan §4.14, PD-18, TA-6)

**Files (all new, under `apps/krate-manager/`):** `package.json`, `package-lock.json`, `tsconfig.json`,
`tsconfig.node.json`, `vite.config.ts`, `eslint.config.mjs`, `index.html`, `src/main.ts`, `src/styles.css`,
`src/main.test.ts`.

**Template read (TA-6).** Into `F:/tmp/km4-template/` (outside the repo), read with
`gh api "repos/tauri-apps/create-tauri-app/contents/templates/template-vanilla-ts/<file>?ref=c6f5cf68942f187ba7bb738a49ee505c28e6ca5a" -H "Accept: application/vnd.github.raw"`
the files `tsconfig.json` and `vite.config.ts.lte`. If the `.lte` holds template directives, resolve them for the v2 /
npm / vanilla-ts case and quote the resolved text in the notes. This is an agent read, not a test.

**Test first.** `src/main.test.ts` (FR-054, SC-017, PD-18): `import { afterEach, expect, test } from "vitest";`,
`import { clearMocks, mockIPC } from "@tauri-apps/api/mocks";`, `import { renderShell } from "./main";`,
`afterEach(clearMocks)`. Each test builds `const root = document.createElement("main")`, a `const calls: string[] = []`,
a `mockIPC((cmd) => { calls.push(cmd); … })`, then `await renderShell(root)`. Four tests, titles exact:
1. `shows the title and the version`: `startup_error` → `null`, `app_version` → `"0.1.0"`; `root.querySelector("h1")
   ?.textContent === "Krate Manager"`, `root.querySelector("p.version")?.textContent === "0.1.0"`, no `.notice`,
   `calls` deep-equals `["startup_error", "app_version"]`.
2. `shows "version unavailable" when app_version fails`: `app_version` throws `new Error("x")`; `p.version` text
   `version unavailable`.
3. `shows only the notice when another instance is running`: `startup_error` → `"Krate Manager is already running"`;
   the `h1`, one `p.notice` with exactly that text, no `p.version`, `calls` deep-equals `["startup_error"]`.
4. `shows the version when startup_error fails` (PD-18): `startup_error` throws; `p.version` reads `0.1.0`, no
   `.notice`, `calls` deep-equals `["startup_error", "app_version"]`.
The test file holds no URL, no `innerHTML` / `outerHTML` / `insertAdjacentHTML` / `document.write` text and no
`@ts-` or `eslint-disable` comment (FR-045 scans all of `src/`). Red first: `src/main.ts` =
`export async function renderShell(_root: HTMLElement): Promise<void> {}` → tests 1-4 fail on their DOM assertions;
quote one.

**Implement.**
- `package.json`: plan §4.14 verbatim (exact versions, six scripts, no `version` key, no `@tauri-apps/plugin-*`).
- `npm install` from an empty `node_modules` (Rule 8) writes `package-lock.json`; then `rm -rf node_modules && npm ci`
  to prove the lock is complete.
- `tsconfig.json`: the template's `compilerOptions` (plan §1.2 row "create-tauri-app templates" lists them) plus
  `"noUncheckedIndexedAccess": true`, `"exactOptionalPropertyTypes": true`, `"include": ["src"]`, no `types`; plain
  JSON without comments (T011's guard parses it with `serde_json`). `tsconfig.node.json`: plan §4.14 verbatim.
- `vite.config.ts`: first line `/// <reference types="vitest/config" />`, then the template body with the
  `// @ts-expect-error …` line deleted (FR-051), plus `test: { environment: "jsdom", include: ["src/**/*.test.ts"] }`;
  no `fileParallelism`, `pool`, `maxWorkers` (FR-053).
- `eslint.config.mjs`: plan §4.14 verbatim (FR-052).
- `index.html`, `src/styles.css`, `src/main.ts`: plan §4.14 (stylesheet linked from HTML; no `url(` / `@import` in the
  CSS; `main.ts` verbatim).
- `npm run build` once (writes `dist/`, ignored by `/dist/`), to prove `beforeBuildCommand` works; record its last line.

**Zero warnings / green:** L-TS (lint 0 problems with `vite.config.ts` and `eslint.config.mjs` present and ignored per
FR-052); L-RS still green (no regression). `git status --porcelain -- apps/krate-manager` shows no `node_modules/` or
`dist/`.

**Done when:** all four Vitest cases pass; L-TS and L-RS green; the notes hold the red line, the resolved
`vite.config.ts` template text, and the `npm run build` line.

---

## G3 — Core modules (sequential; each task adds its own `pub mod` line to `crates/core/src/lib.rs`, TA-4)

Every G3 task: red first with `todo!()` bodies (How to read this file), then implement; lane **L-RS** (plus L-XCHECK
where named); the test names are the plan §4.18 names exactly (`required-tests.json` pins them, T016). Every
`thiserror` `Display` begins with the error's code where the error has one (plan §3).

### T004 — `roots` and `test_support` (spec FR-013, FR-020, FR-022, FR-102 roots part, SC-005 (a), SC-007, SC-027 roots part, SC-030 `logs_dir`; plan §3, §4.3, §4.4, PD-4, PD-6, PD-24, PD-26, TA-1)

**Files:** `crates/core/src/roots.rs`, `crates/core/src/roots_tests.rs`, `crates/core/src/test_support.rs`,
`crates/core/src/test_support_tests.rs` (new); `crates/core/src/lib.rs`, `apps/krate-manager/clippy.toml` (edit).

**Test first.**
- `roots_tests.rs` (module `roots::tests`). A helper `fn try_new(p: RootPaths) -> Result<Roots, RootsError>` holds the
  one `Roots::new` call with D6 site 6's `#[expect]` (plan §4.1 table, reason `"the Roots::new validation test"`).
  - `new_validates_and_touches_nothing` (SC-005 (a), PD-4, PD-26): in a `fresh_temp_roots()` base (the canonical base
    is `roots.state_dir().parent()`), a `RootPaths` naming non-existent subdirectories of it is `Ok`, and the base's
    recursive listing (relative names, kind file/dir, size) is identical before and after. Then each rejection: a
    relative plugin root → `Relative { which: "plugin_root" }`; no `Vst3User` entry → `MissingRoot { kind:
    RootKind::Vst3User }`; no `Vst3Global` entry → `MissingRoot { kind: RootKind::Vst3Global }`; `staging_dir` ≠
    `state_dir/staging` → `Staging`; `<base>/a/../b` as a plugin root → `NotNormal { which: "plugin_root" }`;
    `base.ancestors().last()` (the filesystem root) as `presets.user` → `NotNormal { which: "presets.user" }`.
  - `with_configured_plugin_root_cases` (SC-027 roots part, PD-26): `X = <canonical base>/configured`, `Y =
    <canonical base>/other` (inside the sandbox, never created). `Some(X)` → `plugin_roots()[0] == PluginRoot { kind:
    Configured, path: X }` and the rest equal the input in order (`Vst3User` included); `None` on a value without a
    `Configured` entry → equal to the input; `Some(X)` then `Some(Y)` on the result → only `Y` as `Configured`, first;
    `Some(<vst3-user path>)` → equal to the input (no second entry); a relative path → `Relative { which: "configured"
    }`; `<vst3-user>/../vst3-user` → `NotNormal { which: "configured" }`; in every case `presets()`, `state_dir()`,
    `staging_dir()` are unchanged.
  - `logs_dir_is_state_logs`: `roots.logs_dir() == roots.state_dir().join("logs")` (SC-030's path half).
  - `sandboxed_roots_refuse_an_outside_configured_root` (PD-24): `o = canonical_temp_dir().join("km-outside-sandbox")`
    (absolute, outside the `TempDir`, never created); `std::panic::catch_unwind(AssertUnwindSafe(||
    roots.with_configured_plugin_root(Some(&o))))` is `Err` and its payload string contains `outside the test
    sandbox`; `<canonical base>/configured` is accepted; `o` still does not exist afterwards.
- `test_support_tests.rs` (module `test_support::tests`), SC-007's six tests exactly as the spec describes them, all
  against an injected fake temp root `td/fake_tmp` inside a fresh `tempfile::TempDir` `td`, with `td/fake_tmp/sub/` and a
  sibling `td/outside/`:
  `check_under_temp_cases`, `check_under_temp_symlink_escape` (link `td/fake_tmp/link -> td/outside` with
  `std::os::unix::fs::symlink` / `std::os::windows::fs::symlink_dir`; on raw OS error 1314 with `CI` unset: `eprintln!`
  and return; with `CI` set the failure fails the test; the `CI` read is D6 site 7, reason `"symlink privilege failures
  fail only on CI"`), `temp_roots_in_outside_panics` (`#[should_panic(expected = "temp_roots refused")]`),
  `temp_roots_in_refusal_creates_nothing` (`catch_unwind` around `temp_roots_in` for `td/outside` and the symlink base;
  each `Err`; `td/outside`'s listing stays empty), `temp_roots_on_temp_dir_itself_panics`
  (`#[should_panic(expected = "temp_roots refused")]`, `temp_roots(&canonical_temp_dir())`),
  `temp_roots_inside_temp_ok` (every root of `fresh_temp_roots().1` starts with `canonical_temp_dir()` and with the
  `TempDir`'s canonical path).
Red first: both modules with `todo!()` bodies (and the four `#[expect]`s already in place, so their fulfilment is
checked from the first green run); run `cargo test -p krate-manager-core roots::tests test_support::tests` and quote
one failing line.

**Implement.**
- `roots.rs`: plan §4.3 verbatim API (`STAGING_DIR`, `LOGS_DIR`, `RootKind`, `PluginRoot`, `PresetRoots`, `RootPaths`,
  `Roots` with the private `sandbox`, the eight methods) and its check order (absolute → normal → missing kinds →
  staging); `RootsError` per plan §3 (`Env`, `Relative`, `NotNormal`, `MissingRoot`, `Staging`). The sandbox panic
  message contains `outside the test sandbox` and names both paths. No filesystem token (Rule 4), no `Default` / `From`
  / public field.
- `test_support.rs`: plan §4.4 (`canonical_temp_dir` with D6 site 5, `check_under_temp` strict descendant after
  canonicalizing both sides, `temp_roots_in` check-first then `Roots::new(…)` (D6 site 4) and `.sandboxed(base)` in
  the same statement, then `create_dir_all` per root, one `join` per component (PD-6), macOS-only AU dirs; its panic
  message begins `temp_roots refused` and names both paths; `temp_roots`, `fresh_temp_roots`). `GuardError` per plan §3.
- `lib.rs`: `pub mod roots;` and `#[cfg(any(test, feature = "test-support"))] pub mod test_support;` (FR-020, FR-022).
- `clippy.toml`: add the `krate_manager_core::roots::Roots::new` entry with its plan §4.1 reason (TA-1).

**Green:** L-RS (the four `#[expect]`s fulfilled: 0 `warning:` lines). Record whether the symlink case made the link
locally: `cargo test -p krate-manager-core check_under_temp_symlink_escape -- --nocapture` (the `eprintln!` line, or
none).

**Done when:** the ten tests pass; L-RS green; the notes hold the red line and the symlink record.

### T005 — `platform` with the Windows, macOS and Linux implementations (spec FR-010 to FR-012, D7-D9, SC-002, SC-003, SC-004, SC-005 (b); plan §4.2, PD-5, PD-6, PD-26)

**Files (new):** `crates/core/src/platform/mod.rs`, `mod_tests.rs`, `windows.rs`, `windows_tests.rs`, `macos.rs`,
`macos_tests.rs`, `linux.rs`, `linux_tests.rs`. **Edit:** `crates/core/src/lib.rs` (`pub mod platform;`).

**Test first.**
- `platform/windows_tests.rs` → `platform::windows::tests::default_roots_from_env` (SC-002 exactly, plus PD-26):
  the four `C:\km-fixture\…` variables yield the full D8 Windows `RootPaths` (vectors compared whole, order included),
  `state_dir` `C:\km-fixture\Users\u\AppData\Local\com.krateaudio.krate-manager`, `staging_dir` its `\staging`; the same
  with lower-cased names gives the same result; each variable missing, empty, or `Local` (relative) gives `Env { var }`
  naming it; `LOCALAPPDATA=C:\km-fixture\..\x` gives `Env { var: "LOCALAPPDATA" }`.
- `platform/macos_tests.rs` → `platform::macos::tests::default_roots_from_env` (SC-003 plus `HOME=/Users/u/../x` →
  `Env { var: "HOME" }`); plugin roots in the order `Vst3User`, `Vst3Global`, `AuUser`, `AuGlobal` (plan §4.2 table).
- `platform/linux_tests.rs` → `platform::linux::tests::default_roots_from_env` (SC-004 plus `HOME=/home/u/../x` →
  `Env { var: "HOME" }` and `XDG_DATA_HOME=/data/../x` falling back to `/home/u/.local/share/…`); `Vst3Global` in the
  plan's order `/usr/lib64/vst3`, `/usr/lib/vst3`, `/usr/local/lib64/vst3`, `/usr/local/lib/vst3`.
- `platform/mod_tests.rs` → `platform::tests::default_roots_touches_nothing` (SC-005 (b)): an `OsEnv` whose every D9
  variable of the native OS points into a `fresh_temp_roots()` base (`<base>/env/LocalAppData`, `<base>/env/Common
  Files`, `<base>/env/ProgramData`, `<base>/env/UserProfile` on Windows; `HOME=<base>/env/home`,
  `XDG_DATA_HOME=<base>/env/xdg` on Unix) passed to `NativePlatform.default_roots` is `Ok`, and the base's recursive
  listing (names, kinds, sizes) is identical before and after.
Each per-OS test file is declared from its own `.rs` (Rule 3), so it compiles only on its OS. Red first (native OS
only): `default_roots` = `todo!()`; quote the failing line of `platform::windows::tests::default_roots_from_env` and
`platform::tests::default_roots_touches_nothing`.

**Implement.** Plan §4.2 verbatim: `APP_IDENTIFIER`, `Os`, `OsEnv` (`from_pairs`, `get`, ASCII-case-insensitive under
`#[cfg(windows)]`), the `Platform` trait with exactly `os` and `default_roots`, the three `cfg`'d modules re-exported as
`NativePlatform` with `ENV_KEYS`, `required_abs` (missing, empty, relative or `..` → `Env { var }`; pure), the per-OS
table (absolute roots as whole literals, env-derived roots one `push` per component, PD-6), `roots::STAGING_DIR` for
`staging_dir`, required variables read in `ENV_KEYS` order; Linux `XDG_DATA_HOME` optional (missing, empty, relative or
with `..` → `$HOME/.local/share`). Rule 4 binds every platform file: only `PLATFORM_LITERALS` (plan §4.10) outside
attributes, no filesystem token, no `expect("…")`.

**Green:** L-RS; L-XCHECK (under `[P-3]` (a)): both cross targets compile the macOS and Linux files and their tests with
0 warnings.

**Done when:** on Windows the two native tests pass; L-RS green; L-XCHECK green or recorded skipped with its reason;
the notes say the macOS and Linux tests are PENDING-CI (`check (macos-latest)` / `check (ubuntu-24.04)`, step "required
tests ran", T016).

### T006 — `settings`: schema v1, atomic persistence, refusal over a newer file (spec FR-030 to FR-036, D10, SC-009, SC-010, SC-011; plan §4.5, PD-7, PD-8, PD-9, PD-25, PD-26, TA-7)

**Files (new):** `crates/core/src/settings.rs`, `crates/core/src/settings_tests.rs`. **Edit:** `lib.rs` (`pub mod
settings;`).

**Test first** (module `settings::tests`, every file inside a `fresh_temp_roots()` state dir; listings are recursive
`(name, kind, size)` sets; "bytes and mtime unchanged" = `fs::read` and `metadata().modified()` equal before / after):
- SC-009: `round_trip_values` (`save` then `load` equal for `{None, false}` and `{Some("/home/u/custom"), true}`; on
  Windows `{Some("C:\\vst"), true}`), `written_bytes_are_d10` (the file is exactly plan §4.5's four-line literal plus
  LF for `Settings::default()`), `missing_file_gives_defaults_and_writes_nothing` (no file → defaults, `error: None`,
  state-dir listing unchanged), `plugin_root_or_default_uses_vst3_user` (`None` → `roots.vst3_user()`; `Some(p)` → `p`).
- SC-010 (+ PD-8, PD-26): `load_rejections_keep_defaults_and_file` (each planted file of SC-010's list gives
  `Settings::default()` with its code, plus `schema_version: -1` → `E_SETTINGS_INVALID`, a `linux_plugin_root` of
  `<canonical base>/a/../b` (JSON-escaped with `serde_json::to_string`) → `E_SETTINGS_INVALID`, the filesystem root
  `base.ancestors().last()` → `E_SETTINGS_INVALID`; bytes and mtime unchanged each time),
  `save_rejects_invalid_values_and_writes_nothing` (non-UTF-8: Unix `OsStr::from_bytes(b"/x\xff")`, Windows
  `OsString::from_wide(&[0xD800])` → `E_SETTINGS_PATH_NOT_UTF8` (TA-7: the UTF-8 check runs first); relative, empty,
  `..`, filesystem root → `E_SETTINGS_INVALID`; listing unchanged, not even the state dir created when it was removed
  first), `save_refuses_future_version_file` (planted `schema_version: 2`, and a `schema_version: 2` file with an
  unknown field → `E_SETTINGS_FUTURE_VERSION`; bytes, mtime and listing unchanged, no temp file),
  `save_overwrites_corrupt_and_invalid_files` (over not-JSON and over `schema_version: 0` → `Ok` and the file then holds
  the D10 bytes of the saved value; over a valid v1 file and into an empty state dir → `Ok`).
- SC-011 (PD-9): `fault_at_each_step_keeps_previous_file` (`FaultIo { at, mode: Fail }` over a state dir holding a valid
  previous file, for every `SettingsStep` (six on Windows, seven on Unix): `E_SETTINGS_IO` whose `op` names the step;
  `settings.json` byte-identical except at `SyncDir`, where it holds the complete new bytes; at `ReadExisting` the
  listing is unchanged), `abort_leaves_previous_file_and_stray_temp_is_ignored` (`Mode::AbortAfter` at `Write` and at
  `SyncTemp`: the temp stays (`NamedTempFile::keep`), `settings.json` byte-identical, the next `load` returns the
  previous values with `error: None`, ignoring the `.settings.json.*` file).
Red first with `todo!()` bodies; quote one failing line.

**Implement.** Plan §4.5 verbatim: constants, `Settings` + `linux_plugin_root_or_default`, `validate` (UTF-8 first, then
empty, relative, `..`, filesystem root), `LoadOutcome`, `load(&Roots)`, `save(&Roots, &Settings)`, the private
`SettingsFileV1` with `#[serde(deny_unknown_fields)]`, `classify` in the PD-8 order with the explicit PD-7 key-set
check, `encode` (`serde_json::to_vec_pretty` + `b'\n'`), the crate-private `SettingsIo` trait with `StdSettingsIo`, and
`save_with` in the order `validate → read_existing → FutureVersion refusal → create_dir → create_temp → write →
sync_temp → persist → (Unix) sync_dir`. `SettingsError` per plan §3 with stable `code()`.

**Green:** L-RS; L-XCHECK (the Unix `sync_dir` path and `OsStrExt` compile).

**Done when:** the ten tests pass on Windows; L-RS green; the notes record that the Unix-only branches (`SyncDir`, the
`from_bytes` vector) are PENDING-CI on both hosted legs.

### T007 — `state_lock` (spec FR-100, D22, SC-025; plan §4.6, PD-25)

**Files (new):** `crates/core/src/state_lock.rs`, `crates/core/src/state_lock_tests.rs`. **Edit:** `lib.rs` (`pub mod
state_lock;`).

**Test first** (module `state_lock::tests`, each over its own `fresh_temp_roots()`):
`second_acquire_is_held_while_first_lives` (first `Ok`, second `Err(LockError::Held)`, after dropping the first a third
`Ok`), `acquire_creates_state_dir_and_lock_file_only` (after `remove_dir_all(roots.state_dir())`, `acquire` leaves the
listing differing by exactly `state/` and a zero-byte `state/manager.lock`), `lock_file_survives_drop` (after the lock
is dropped `state/manager.lock` still exists, zero bytes), `acquire_io_error_is_named` (state dir removed and a regular
file written at its path → `Err(LockError::Io { op: LockStep::CreateDir, .. })`). Red first with `todo!()`.

**Implement.** Plan §4.6 verbatim (`LOCK_FILE`, `StateLock { _file }`, `acquire(&Roots)` with `create_dir_all`,
`OpenOptions` read + write + create + `truncate(false)`, `try_lock` mapping `WouldBlock` → `Held`; `LockError`,
`LockStep` per plan §3; no `Drop` impl; the file is never written or deleted). `File::try_lock` is stable since Rust
1.89, inside `rust-version = "1.90"` (spec D22), so clippy's `incompatible_msrv` stays quiet.

**Green:** L-RS; L-XCHECK. **Done when:** the four tests pass; the notes record that POSIX `flock` semantics are
PENDING-CI.

### T008 — `log_sink`: the line format and the rotating writer (spec FR-104, D23, SC-029; plan §4.7, PD-10, PD-25)

**Files (new):** `crates/core/src/log_sink.rs`, `crates/core/src/log_sink_tests.rs`. **Edit:** `lib.rs` (`pub mod
log_sink;`).

**Test first** (module `log_sink::tests`):
- `format_line_cases`: one line per record, LF-terminated; CR, LF and tab in the message become spaces (every
  `char::is_control`); level and target present in the form `<unix_secs> <LEVEL> <target>: <message>\n`; a
  10 000-byte message gives a line of at most `MAX_LINE_BYTES` bytes, LF included, still valid UTF-8 when the cut
  falls inside a multi-byte character (use a message of `"é"` repeated).
- `rotating_log_cases` (each case in its own `fresh_temp_roots()`, `open_with_cap(&roots, 200)`): the logs dir is
  absent at first and created; after enough lines the dir holds exactly `krate-manager.log` and `krate-manager.log.1`,
  each ≤ 200 bytes, the newest line in `krate-manager.log`; a third rotation replaces `.1` with the second generation's
  content; no other file appears; reopening appends rather than truncates; the PD-10 over-long-line case (a 300-byte
  line ending in multi-byte characters with the cut inside one, after a short line: no file over 200 bytes, both
  `String::from_utf8`-valid); the PD-10 failed-rotation case (`logs/krate-manager.log.1` made a directory holding one
  file: the `write_line` that would rotate returns `Err`, `krate-manager.log` is byte-identical and ≤ 200 bytes; after
  the directory is removed the next `write_line` rotates and succeeds).
Red first with `todo!()`.

**Implement.** Plan §4.7 verbatim (constants, `format_line`, `cut_at_boundary` with `str::is_char_boundary`,
`RotatingLog { dir, file, size, cap }`, `open(&Roots)`, `open_with_cap(&Roots, cap)` writing `roots.logs_dir()`,
`write_line` in the three PD-10 steps: cut, rotate with the handle closed first and reopened in both outcomes, write).

**Green:** L-RS; L-XCHECK. **Done when:** both tests pass.

### T009 — `navigation`: the app origin and the origin check (spec FR-044 core part, D12, SC-016; plan §4.9)

**Files (new):** `crates/core/src/navigation.rs`, `crates/core/src/navigation_tests.rs`. **Edit:** `lib.rs` (`pub mod
navigation;`).

**Test first** (module `navigation::tests`): `is_app_origin_cases` and `app_origin_cases`, exactly the SC-016 tables
(the foreign hosts, `https://krateaudio.com/` included, live only in `navigation_tests.rs`, which the FR-021 scan skips).
Red first with `todo!()`.

**Implement.** Plan §4.9 verbatim: `DEV_URL = "http://localhost:1420"`, `app_origin(dev, os)` (`DEV_URL` when `dev`,
else `http://tauri.localhost` for `Os::Windows`, `tauri://localhost` otherwise; `Url::parse(..).expect("constant URL")`),
`is_app_origin` comparing scheme, `host_str` and `port_or_known_default`. No `krateaudio.com` text in `navigation.rs`
(Rule 4).

**Green:** L-RS. **Done when:** both tests pass.

### T010 — `context`: startup assembly, the locked instance, live reassembly, startup logging, the dev-feed parser (spec FR-017, FR-101 to FR-104 (`log_startup_errors`), FR-106 (`parse_dev_feed_url`), D17, D22, SC-024, SC-026, SC-027 context part, SC-028, SC-030, SC-031 Rust part; plan §4.8, PD-1, PD-2, PD-3, PD-24, TA-4)

**Files (new):** `crates/core/src/context.rs`, `crates/core/src/context_tests.rs`. **Edit:** `lib.rs` (`pub mod
context;`). After this task `lib.rs` is the crate doc comment plus, in `cargo fmt` order, `pub mod context;`, `pub mod
log_sink;`, `pub mod navigation;`, `pub mod platform;`, `pub mod roots;`, `pub mod settings;`, `pub mod state_lock;`,
and `#[cfg(any(test, feature = "test-support"))] pub mod test_support;` (TA-4).

**Test first** (module `context::tests`; feed URL `http://127.0.0.1:9/versions.json`, never contacted; every listing
recursive over the `fresh_temp_roots()` base):
- `assemble_cases` (SC-024 (a), (b), (b2), (c)): (a) `e = NativePlatform.default_roots(&OsEnv::from_pairs([]))
  .unwrap_err()` → `assemble_context(Err(e), url)` gives `Err(StartupError::Roots(RootsError::Env { var }))` naming
  `LOCALAPPDATA` on Windows, `HOME` elsewhere, `code()` `E_STARTUP_ROOTS`, `settings_error: None`, `lock: None`; (b)
  `Ok(roots)` → `Ok(AppContext)` whose roots equal the input, settings default, `feed_url == url`, `settings_error:
  None`, `lock: Some(_)`, the listing differing by exactly `state/manager.lock`; (b2) after `remove_dir_all(state_dir)`
  the listing differs from the post-removal one by exactly `state/` and `state/manager.lock`; (c) a planted corrupt
  `settings.json` → `Ok`, default settings, `settings_error` code `E_SETTINGS_CORRUPT`, the file byte-identical.
- `assemble_locked_instance` and `startup_notice_cases` (SC-026): a held `StateLock` plus a planted corrupt
  `settings.json` → `Err(StartupError::Locked)` (`E_STARTUP_LOCKED`), `settings_error: None`, `lock: None`, the file
  byte-identical, no new file; `startup_notice` is `Some("Krate Manager is already running")` for that state and `None`
  for a healthy, a `Roots`-error and an `E_STARTUP_LOCK_IO` state; `LOCKED_EXIT_DELAY == Duration::from_secs(5)`.
- `configured_root_cases` (SC-027 context part): `X = <canonical base>/configured`; `assemble_with_os(Os::Linux, …)`
  over a planted v1 `settings.json` naming `X` gives `plugin_roots()[0] == {Configured, X}` and the rest equal the input
  in order; the same with `null` equals the input; `Os::Windows` and `Os::Macos` with `X` planted equal the input; `X`
  equal to the `Vst3User` path adds no entry; presets, `state_dir`, `staging_dir` unchanged throughout.
- `reassemble_cases` (SC-028, PD-2): from an assembled `Os::Linux` state, `reassemble_with_os` with new settings gives
  roots carrying the new `Configured` entry first, the new settings, an equal `feed_url`, `Arc::ptr_eq` on the lock (a
  further `StateLock::acquire` is still `Held`), and an identical listing; to `linux_plugin_root: None` removes the
  entry; a relative root returns the previous context unchanged with `settings_error` `E_SETTINGS_INVALID`; an `Err`
  context comes back unchanged; `apply_settings(&shared, s)` on a `SharedState` then `shared.read()` sees the new
  roots.
- `sandbox_holds_through_assemble_and_reassemble` (PD-24): `o = canonical_temp_dir().join("km-outside-sandbox")`
  (never created); under `catch_unwind(AssertUnwindSafe(…))` both (a) `assemble_with_os(Os::Linux, Ok(roots), url)` over
  a planted `settings.json` naming `o` and (b) `reassemble_with_os(Os::Linux, &prev, Settings { linux_plugin_root:
  Some(o), .. })` panic with a payload containing `outside the test sandbox`; the listing outside `state/` is unchanged.
- `startup_errors_are_logged` (SC-030): the only test that installs a logger: a capture `log::Log` set once with
  `log::set_logger` and `log::set_max_level(LevelFilter::Trace)` (the runtime level starts `Off`, plan §1.2), keeping
  records whose target is `STARTUP_LOG_TARGET` and whose thread is the test's `ThreadId`; `log_startup_errors` emits one
  `Error` record containing `E_STARTUP_ROOTS` for a Roots-error state, one containing `E_STARTUP_LOCKED` for a locked
  state, one containing `E_SETTINGS_CORRUPT` for a corrupt-settings state, none for a healthy state.
- `parse_dev_feed_url_cases` (SC-031): the spec's table exactly.
Red first with `todo!()`.

**Implement.** Plan §4.8 verbatim: the three constants, `AppContext` (`Clone, Debug, PartialEq`), `AppState`,
`SharedState = std::sync::RwLock<AppState>`, `assemble_context` / `assemble_with_os` (lock → load → Linux fold →
return), `reassemble` / `reassemble_with_os` (no I/O; `Err` cloned, PD-3), `apply_settings` (read guard dropped before
the write guard, `PoisonError::into_inner`), `startup_notice`, `log_startup_errors` (one `log::error!(target:
STARTUP_LOG_TARGET, "{}", e)` per error, Display beginning with the code), `parse_dev_feed_url` (trim, ≤ 2048 bytes,
single line, `http` / `https` with a non-empty host, returns the trimmed text). `StartupError` (`Clone`; `LockIo`
carrying `Arc<io::Error>`) per plan §3. No `krateaudio.com` text in `context.rs` (Rule 4): the production URL is
`src-tauri`'s (T012).

**Green:** L-RS; L-XCHECK.

**Done when:** the eight tests pass; L-RS green; the notes confirm that 38 of the 48 plan §4.18 `all` names now exist
and pass (every name except T011's five `config_guard.rs` tests and T014's five `injection.rs` tests).

---

## G4 — App shell (sequential)

### T011 — `crates/core/tests/config_guard.rs`, written before `src-tauri` (spec FR-045, SC-015; plan §4.10 `config_guard.rs` paragraphs, PD-23)

**Files (new):** `crates/core/tests/config_guard.rs`.

**Test first (this whole task is the failing test).** Plan §4.10's `config_guard.rs` design verbatim: `ConfigFiles` (with
`workspace_cargo_toml`), `check_config`, `check_frontend`, `AppMetaFiles`, `check_app_meta`, and five tests:
`real_src_tauri_config_passes_the_guard`, `planted_config_changes_fail_the_guard` (every SC-015 config / `Cargo.toml` /
`lib.rs` mutation plus the four PD-23 dependency mutations, each applied to a clone and each yielding its message),
`frontend_source_scan_reports_planted_strings` (the four SC-015 strings), `real_app_metadata_passes_the_guard`,
`planted_app_metadata_changes_fail_the_guard` (the plan's fourteen manifest / `package.json` / `tsconfig.json` /
ESLint / CHANGELOG mutations). Paths come from `env!("CARGO_MANIFEST_DIR")` (`crates/core`) joined with `../..`
(the app root) and `../../src-tauri`. Reading rules:
- `read_to_string` for text and JSON, `read` (bytes) for the `LSFileQuarantineEnabled` walk of every file under
  `src-tauri/` (the icons are binary);
- the `Info.plist` walk also covers all of `src-tauri/`.
- If the walk finds the string in a file `tauri-build` generated (`src-tauri/gen/`), STOP AND SURFACE with the file and
  offset: FR-045 says "no file under `src-tauri/`", and narrowing it is the user's call.
- Text checks on `Cargo.toml` use a section splitter over `str::lines()` (CRLF checkouts, Rule 12).
- `lib.rs` strings are counted with `str::matches(..).count()` over the non-test files of `src-tauri/src/`.
The file reads only; it creates nothing.

**Expected red until T012:** `real_src_tauri_config_passes_the_guard`, `planted_config_changes_fail_the_guard` and
`real_app_metadata_passes_the_guard` (no `src-tauri/`; `members` has two entries, not three). The two frontend /
app-meta mutation tests may also fail at load for the same reason. Run `cargo test -p krate-manager-core --test
config_guard` and quote each failure line. A failure for any other reason (a frontend or manifest fact from T002 /
T003 that does not hold) is a real red: fix the checker if it misreads the file; if the file itself is wrong, record it
in the notes for the gate's fixer (the file belongs to T002 / T003).

**Green:** `cargo fmt --all --check` and `cargo clippy --workspace --all-targets --locked -- -D warnings` (the test
compiles warning-free); `cargo test` red only as above.

**Done when:** the file compiles with 0 warnings and its reds are exactly the documented ones.

### T012 — `src-tauri`: the thin shell (spec FR-004, FR-005, FR-014, FR-017 glue, FR-040 to FR-044, FR-101 glue, FR-105, FR-106, D5, D6 sites 1-3, D11-D15, D24; plan §4.12, PD-1, PD-14, PD-21, PD-25, TA-1, TA-3, TA-6)

**Files (new, under `apps/krate-manager/src-tauri/`):** `Cargo.toml`, `build.rs`, `tauri.conf.json`,
`tauri.windows.conf.json`, `tauri.macos.conf.json`, `tauri.linux.conf.json`, `capabilities/main.json`, `icons/` (the
sixteen template files), `src/main.rs`, `src/lib.rs`, `src/os_roots.rs`, `src/commands.rs`, `src/navigation.rs`,
`src/log_sink.rs`, `src/dev_feed.rs`. **Edit:** `apps/krate-manager/Cargo.toml` (`members` gains `"src-tauri"`,
TA-3), `apps/krate-manager/Cargo.lock` (Rule 8), `apps/krate-manager/clippy.toml` (the `from_os` entry, TA-1).

**Test first.** The failing tests are T011's three `real_*` / `planted_config_*` reds; nothing new is written here
(`src-tauri` has no test, D12a).

**Implement.**
1. Icons (TA-6): read each of the sixteen files of `templates/_base_/src-tauri/icons/` at commit `c6f5cf68` with `gh api
   … -H "Accept: application/vnd.github.raw" > <file>` and check each size equals the listing read this session:
   `128x128.png` 1593, `128x128@2x.png` 2861, `32x32.png` 567, `Square107x107Logo.png` 1340, `Square142x142Logo.png`
   1691, `Square150x150Logo.png` 1738, `Square284x284Logo.png` 3115, `Square30x30Logo.png` 536,
   `Square310x310Logo.png` 3447, `Square44x44Logo.png` 777, `Square71x71Logo.png` 1020, `Square89x89Logo.png` 1181,
   `StoreLogo.png` 848, `icon.icns` 98451, `icon.ico` 37710, `icon.png` 5664 bytes.
2. `Cargo.toml`, `build.rs`, `tauri.conf.json`, the three platform files, `capabilities/main.json`, `src/main.rs`,
   `src/lib.rs`, `src/os_roots.rs`, `src/commands.rs`, `src/navigation.rs`, `src/log_sink.rs`, `src/dev_feed.rs`: plan
   §4.12 verbatim. D6 sites 1-3 carry the plan §4.1 reasons. `krateaudio.com` appears once, in `lib.rs`'s `FEED_URL`.
   `var_os` appears only in `os_roots.rs`. Exactly one `eprintln!` and one `set_logger` in `src-tauri/src/`.
   `dev-feed-url.txt` appears only in `dev_feed.rs`.
3. If clippy raises `let_and_return` on `os_roots.rs`'s attributed `let roots = Roots::new(paths); roots`, restructure
   the statement (a `match` on the call), keeping the `#[expect]` on the call (plan §4.12 note); never an `allow`.
4. If rustc rejects the `#[cfg(feature = "dev-feed")]` on the `if feed_url != FEED_URL { … }` statement in `run()`,
   move the `warn!` into a `#[cfg(feature = "dev-feed")]` block statement. All FR-045 `lib.rs` strings must survive.
5. `Cargo.toml` root `members = ["crates/core", "crates/helper", "src-tauri"]`; `clippy.toml` gains `{ path =
   "krate_manager_lib::os_roots::FromOs::from_os", reason = "production roots only in app setup; tests use temp_roots()"
   }` as its first entry (the file now equals plan §4.1).
6. `cargo metadata --format-version 1 > /dev/null` once (Rule 8), then `--locked` everywhere.

**Green:** L-RS, with T011's five tests now passing; L-DEVFEED; L-TS (no regression). Record the wall clock of the
first `cargo clippy --workspace --all-targets` (the first debug compile of the Tauri tree). `cargo tree -p
krate-manager-core -e normal` shows no `tauri`, `dirs`, `directories` or `home` crate (FR-007; record the command's
first and last lines). `git status --porcelain -- apps/krate-manager` lists no `src-tauri/gen/` or
`src-tauri/permissions/autogenerated/` path (PD-15).

**Done when:** all five `config_guard.rs` tests pass; L-RS and L-DEVFEED green with 0 warnings (all seven D6
`#[expect]`s fulfilled); the notes hold the wall clock and the `cargo tree` record.

### T013 — The clippy-guard fixture (spec FR-016, FR-072 local run, SC-006 local evidence; plan §4.13, PD-16, PD-17)

**Files (new, under `apps/krate-manager/tests/fixtures/clippy-guard/`):** `Cargo.toml`, `Cargo.lock`, `src/lib.rs`;
`specs/krate-manager-phase4/evidence/clippy-guard.txt` (new).

**Test first.** The test is the guard itself: a lint run that must fail in exactly one way. Write `src/lib.rs` per plan
§4.13 verbatim (three `#[test]`s calling `Roots::from_os()`, `Roots::new(RootPaths { … })` with literal paths (PD-17),
and `env::var("HOME")` through `use std::env;`; no suppression), and `Cargo.toml` per plan §4.13 (path deps on
`../../../src-tauri` and `../../../crates/core`, `edition = "2024"`, `publish = false`, no `[lints]`, no `[workspace]`).

**Implement.**
- `Cargo.lock` per PD-16: copy `apps/krate-manager/Cargo.lock` to the fixture directory, then run `cargo metadata
  --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 --offline > /dev/null` once.
- Then run L-GUARD (the plan §4.16 step text in a scratch script outside the repo, `RUNNER_TEMP=F:/tmp/gate/km4-T013`,
  `RUNNER_OS=Windows`, from `apps/krate-manager/`).
- Copy `$RUNNER_TEMP/guard.txt` to `specs/krate-manager-phase4/evidence/clippy-guard.txt`, prefixed by one line with the
  command and the date.
- If the output holds any diagnostic besides the three `use of a disallowed method` errors and clippy's `could not
  compile` summary, fix the root cause in the fixture (never an allow). If the extra diagnostic comes from a path
  dependency compiled for the fixture, STOP AND SURFACE with it verbatim: FR-072's "no other error" rule would then need
  a spec ruling.

**Green:** L-GUARD prints the `::notice` with `disallowed=3 from_os=1 Roots::new=1 std::env::var=1 other-errors=0`; L-RS
still green (the fixture is excluded from the workspace, FR-002); `git status --porcelain -- apps/krate-manager` shows no
fixture `target/`.

**Done when:** the notice line is in the notes and in the evidence file; the fixture lockfile pins equal the workspace's
(`git diff --no-index --stat` of the two locks shows only the added `clippy-guard` package entry).

---

## G5 — Guards and the version lint [P] (each task creates only its own new files)

### T014 [P] — `crates/core/tests/injection.rs` (spec FR-021, SC-008; plan §4.10 `injection.rs` paragraphs, PD-5, PD-22, PD-24)

**Files (new):** `crates/core/tests/injection.rs`.

**Test first.** Plan §4.10 verbatim: the lexer (comments, nested block comments, strings with escapes, byte strings, char
literals vs lifetimes, raw strings, attribute spans with `in_attribute`), `scan_core_file`, the rules, `PLATFORM_LITERALS`,
and five tests: `core_sources_hold_no_production_literal`, `injection_scanner_reports_planted_violations` (SC-008's five
in-memory strings plus `PathBuf::from("/etc")` in a `platform/` file reported, `#[cfg(target_os = "windows")]` and
`#[cfg(test)] #[path = "windows_tests.rs"] mod tests;` not reported, and `.sandboxed(` in a planted `context.rs`
reported), `disallowed_methods_expectations_are_the_seven_d6_sites` (the plan §4.1 per-file table over `crates/*/src`,
`crates/*/tests`, `src-tauri/src` and `tests/fixtures/clippy-guard/src`; 0 everywhere else; no `clippy::style`,
`clippy::all` or `warnings` lint-level attribute, `cfg_attr(…)` included; the two in-memory plants reported),
`core_dependencies_are_the_fr007_set`, `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots`. Red first: the
scanning and counting functions as stubs returning empty vectors / zero counts → the planted-case tests and the D6
count test fail; quote one line. Then implement them.

**If a real-tree test is red after the implementation** (a core, `src-tauri` or fixture file breaks a rule), do not edit
that file (it belongs to an earlier task and this group is parallel): quote the finding in the notes for the gate's
fixer.

**Green:** `cargo test -p krate-manager-core --test injection --locked` all pass; `cargo clippy --workspace
--all-targets --locked -- -D warnings` 0 warnings (this file holds `disallowed_methods` only inside string literals).

**Done when:** the five tests pass on the real tree.

### T015 [P] — `tools/lint-manager-version.js` and its test (spec FR-060, FR-063, D18, SC-012; plan §4.15, §4.17 `lint-manager-version.test.js` paragraph, PD-13)

**Files (new):** `tools/lint-manager-version.js`, `tools/feed-contract/tests/lint-manager-version.test.js`.

**Test first.** `lint-manager-version.test.js` (Rule 9): first statement the write guard; `function makeTree(overrides)
-> dir` writes the all-equal tree under `fs.mkdtempSync(path.join(os.tmpdir(), 'km4-vlint-'))`. The tree holds:
- root `Cargo.toml` with `[workspace]` `members` naming the three members and `[workspace.package]` with `version =
  "0.1.0"` and `publish = false`;
- `crates/core/Cargo.toml` with a literal `version = "0.1.0"`; `crates/helper` and `src-tauri` with
  `version.workspace = true`;
- `src-tauri/tauri.conf.json` with `"version": "0.1.0"`, and the three platform files without `version`;
- `package.json` without `version`, and `version.json`.

One top-level test, `lint-manager-version accepts equal versions and names every drift`, with one `t.test` per case, each
calling `main(['--root', dir], io)` with a capturing `io` and asserting the exit code and the exact first stderr line
(`lint-manager-version: <CODE>: <file>: <message>`, root-relative `/` paths):
- the all-equal tree → 0, no stderr;
- its CRLF variant → 0;
- every SC-012 case (`tauri.conf.json`, the root `[workspace.package]`, each of the three members differing →
  `E_VERSION_MISMATCH` naming the file; missing `version.json` → `E_VERSION_FILE`; `"0.1"`, `"v0.1.0"`,
  `"0.1.0-beta.1"`, `"01.0.0"` → `E_VERSION_FORMAT`; `version` in `tauri.macos.conf.json` and in `package.json` →
  `E_VERSION_SOURCE`; a glob in `members` → `E_CARGO_TOML`; a member with no version line → `E_CARGO_TOML`; `version =
  { workspace = true }` → `E_TOML_UNSUPPORTED`; two drifts at once → both lines, exit 1; an unknown argument → exit 2
  with the usage line);
- the four plan §4.17 extras (`tauri.conf.json` deleted, `{`, without `version` → `E_TAURI_CONF`; `crates/helper/
  Cargo.toml` deleted while `members` names it → `E_CARGO_TOML` naming it).

`t.after` removes every tree. Red first: `tools/lint-manager-version.js` exporting `main` that returns 0 → every drift
subtest fails; quote one.

**Implement.** Plan §4.15 verbatim: shebang, header (WHY, the usage line, exit codes 0 / 1 / 2, form of
`tools/lint-ieee-flag-sets.js:1-17`), `'use strict'`, only `node:fs` and `node:path` through their module objects,
`VERSION_RE`, `parseTomlSubset` (CRLF-safe; the PD-13 subset; `E_TOML_UNSUPPORTED` with the line number), `lint(root)`
in the fixed check order reporting every error, `main(argv, io)` (default root `apps/krate-manager` relative to the repo
root, resolved from `__dirname`), `module.exports = { lint, main, parseTomlSubset }`, the `require.main` entry line. No
plugin name, no `fetch(`, no `child_process`.

**Green:** L-NODE (own file `# fail 0`; whole set `# fail 0`, now 58 top-level tests; L-TAP is not run before T017);
L-VLINT on the real tree exits 0 (all three members and `tauri.conf.json` exist since T012).

**Done when:** every subtest passes; the real tree passes; the notes record the whole-set `# tests` / `# pass` lines.

---

## G6 — CI (sequential)

### T016 — `manager-ci.yml`, `required-tests.json` and the shape test (spec FR-070 to FR-077, FR-080, FR-107 CI part, D16, D19, D21, SC-006 CI, SC-013, SC-014, SC-019, SC-020, SC-021, SC-031 CI part; plan §4.16, §4.17 `manager-ci-shape.test.js` paragraph, §4.18, PD-11, PD-12, PD-20, PD-27)

**Files (new):** `tools/feed-contract/tests/manager-ci-shape.test.js`, `apps/krate-manager/tests/required-tests.json`,
`.github/workflows/manager-ci.yml`.

**Test first.** `manager-ci-shape.test.js` (Rule 9; PD-20: its own small YAML-text helpers copied in form from
`tools/feed-contract/tests/workflow-shape.test.js:18-113`, normalising `\r\n`; no new helper file): one top-level test
`manager-ci.yml keeps the Phase 4 shape`. It reads `.github/workflows/manager-ci.yml`,
`apps/krate-manager/tests/required-tests.json`, `apps/krate-manager/package.json` and, when it exists,
`.github/workflows/release-manager.yml`, and runs a pure `checkManagerCi(files) -> string[]` asserting everything
plan §4.17 lists:
- the FR-070 triggers with the three paths in both lists; the three-entry matrix with `ubuntu-24.04`;
- `permissions: contents: read`, the concurrency block, `shell: bash` and the working directory;
- the §4.16 step names in order and each step's command text;
- the clippy-guard assertions including `= "3"`, `build warnings` (with the `printf '\033'` strip and no `\x1b`, PD-11),
  the Windows `ProductVersion` line, the four macOS steps gated `runner.os == 'macOS'`;
- a `::notice` and a `::error` in every assertion step; `required tests ran` after `vitest`, reading
  `tests/required-tests.json`;
- no `secrets.`, `environment:`, FR-076 flag or `| grep -q` (PD-27); `dev-feed` only in the two named steps and never
  on the `tauri build` line;
- `package.json` scripts free of FR-076 flags and of `dev-feed`; `release-manager.yml`, if present, free of `dev-feed`;
- `required-tests.json` deep-equal to an `EXPECTED_REQUIRED` constant (plan §4.18 verbatim).

Subtests: the real files give `[]`. Each SC-019 mutation and the two plan-review mutations, applied as string edits to
in-memory copies, gives a non-empty list. The SC-019 mutations: an `ubuntu-latest` leg; no clippy-guard step; `cargo
test -- --test-threads=1`; a `secrets.` reference; `contents: write`; the macOS codesign step removed; `required tests
ran` removed; `clippy dev-feed` removed; `dev-feed absent` removed; `--features dev-feed` on the `tauri build` line; the
`macos` key without `platform::macos::tests::default_roots_from_env`. The plan-review mutations: the codesign step as
`codesign -dv "$app" 2>&1 | grep -q 'Signature=adhoc'`; the strip as `sed 's/\x1b\[[0-9;]*m//g'`.

Red first: run it before the workflow exists → it fails reading `manager-ci.yml`; quote the line.

**Implement.**
- `required-tests.json`: plan §4.18 verbatim (48 `all` names, one per OS key, SC-017's three titles).
- `manager-ci.yml`: plan §4.16 verbatim (no `${{ }}` in any `run:` text; LF).

**Local checks of the workflow's logic (Windows, from `apps/krate-manager/`).**
1. `cargo test --workspace --locked 2>&1 | tee F:/tmp/gate/km4-T016/cargo-test.txt`.
2. `npm test -- --reporter=default --reporter=json --outputFile.json=F:/tmp/gate/km4-T016/vitest.json`.
3. A node script written with the Write tool to `F:/tmp/gate/km4-T016/required.js` runs the `<<'REQUIRED'` heredoc
   text, extracted from the workflow file (never retyped), with `process.argv` `[cargo log, vitest json, "Windows"]`
   and cwd `apps/krate-manager/`. It must print `::notice title=required tests ran (Windows)::49 cargo + 3 vitest`.

**Green:** L-NODE (own file and whole set `# fail 0`, 59 top-level); L-YAML on `manager-ci.yml`; L-LINT; L-RS and L-TS
(no regression).

**Done when:** the shape test passes on the real files and on every mutation; the required-tests notice above is in the
notes; L-YAML exit 0.

### T017 — `feed-tools.yml` path lists and TAP plan; `hygiene.test.js` scan lists (spec FR-081, FR-082, SC-022; plan §4.17, PD-19)

**Files (edit, CRLF, Rule 12):** `.github/workflows/feed-tools.yml`, `tools/feed-contract/tests/hygiene.test.js`.

**Test first.** Before editing, run L-NODE's whole set and L-TAP: L-TAP fails with `top-level plan ["1..59"], want
1..57` and `missing:` nothing new (the live heredoc names only Phase 1-3 tests); quote it. That is the red this task
turns green.

**Implement.**
- `feed-tools.yml`: both `paths` lists (`:4-18` push, `:19-33` pull_request; entries end with `'.github/workflows/feed.yml'` at `:18` and `:33`) gain
  `'tools/lint-manager-version.js'`, `'.github/workflows/manager-ci.yml'`, `'apps/krate-manager/package.json'`,
  `'apps/krate-manager/tests/required-tests.json'`. `:78` `const PLAN = "1..59";`. `NAMES` (`:79-113`) gains
  `"lint-manager-version accepts equal versions and names every drift"` and `"manager-ci.yml keeps the Phase 4
  shape"`, appended after `"feed-publish decides every workflow branch"` (`:112`).
- `hygiene.test.js`: `path.join(TOOLS_DIR, 'lint-manager-version.js'),` in `single` (`:35-45`, after the
  `feed-publish.js` line) and `'tools/lint-manager-version.js',` in `missing` (`:119-133`, after
  `'tools/feed-publish.js',`) (FR-082, PD-19). Nothing else changes; `CHILD_EXEMPT` is untouched.

**Green:** L-NODE whole set `# fail 0` (the hygiene scan now covers the lint: no network module, no `fetch(`, no
`child_process`, no destructured `fs`); L-TAP prints `TAP plan=1..59 top-level=59`; L-YAML on `feed-tools.yml`; L-LINT;
`grep -c $'\r$'` equals `wc -l` for both files.

**Done when:** all of the above, with the L-TAP line in the notes.

---

## G7 — Integration (sequential)

### T018 — Roadmap edits, `specs/INDEX.md`, and `evidence/scope-check.js` (spec FR-090, FR-091, SC-023; plan §4.19, TA-5)

> **Main-loop note (2026-10-30):** the scope base is `c66c8557`, not `10593783`. Ruling P-4 changed one line of
> `.claude/workflows/krate-manager-phase.js` (outside FR-090), committed on its own as `c66c8557` before the build stage,
> so every SC-023 diff (`--name-only`, `-U0`) starts there.

**Files:** `specs/Krate-manager-roadmap.md` (edit, CRLF), `specs/INDEX.md` (regenerated),
`specs/krate-manager-phase4/evidence/scope-check.js` (new).

**Test first.** `node tools/gen-specs-index.js --check` exits non-zero (`specs/INDEX.md` lacks the Phase 4 row); quote
it. Write `scope-check.js` with a `--self-test` first.

`scope-check.js` (form of `specs/krate-manager-phase3/evidence/scope-check.js`: read it first; shebang, header, `'use
strict'`, `main(argv)` → exit 0 / 1 / 2) collects the changed paths outside `apps/krate-manager/`: `git diff
--name-only c66c8557` plus `git ls-files --others --exclude-standard` (TA-5: a never-committed file is invisible to the
name-only diff). It runs git through `child_process.spawnSync('git', [...])` with an argument array, never a shell
string; it lives under `specs/`, outside the hygiene scan. Every path must be in Rule 11's set. It then parses `git diff
-U0 c66c8557 -- specs/Krate-manager-roadmap.md` hunk headers (`@@ -a[,b] +c[,d] @@`) and requires every old-side range
to lie in the allowed windows:
- an insertion-only hunk (`b == 0`) at old line 463 or 464 (the status line, main loop at close);
- old line 473;
- old line 497;
- old lines 882-887 (the Phase 12 first-launch bullet).

It prints `scope-check: <n> paths, <m> roadmap hunks: OK` or one line per breach. `--self-test` runs the two pure
checkers on synthetic path lists and hunk headers: allowed passes; `plugins/x.cpp`, `.github/workflows/ci.yml`, a hunk at
old line 496 and one at 500 each fail.

**Implement.**
- The roadmap edits, plan §4.19 exactly:
  - `:473`: replace the one sentence "Feed URL, HTTP client base and the elevation launcher are injected the same
    way." with the plan's replacement;
  - `:497`: replace only the parenthetical (the edit stays one line; `:496` byte-identical);
  - Phase 12 first-launch bullet (`:882-887`): append the plan's Windows-floor sentence after "…the per-plugin
    installers stay listed beside it.";
  - leave the Phase 4 status line to the main loop at close.
- Keep CRLF on every changed or added line.
- Then `node tools/gen-specs-index.js` (writes LF, `.gitattributes:18`) and `--check` → exit 0.

**Green:** `node --check scope-check.js`; `scope-check.js --self-test` exit 0; L-SCOPE exit 0; `grep -c $'\r$'
specs/Krate-manager-roadmap.md` equals `wc -l`; L-LINT.

**Done when:** all of the above; the notes quote the `git diff -U0` hunk headers.

### T019 — The close lane, the short close run and the PENDING-CI list (spec SC-001, SC-006, SC-014 Windows, SC-020, SC-021, SC-022, SC-023; plan §7.3, §7.4, §8)

**Files (new):** `specs/krate-manager-phase4/evidence/close-lane.js`.

**Write `close-lane.js`.** Use the form of `specs/krate-manager-phase3/evidence/close-lane.js` (read it first):
- a header listing every row below;
- `run()` with a per-child wall-clock ceiling and `killTree`, and `record()`;
- raw logs under `F:/tmp/gate/krate-manager-phase4-close/<timestamp>/`;
- the summary in `specs/krate-manager-phase4/evidence/close-lane.log`, plus `specs/krate-manager-phase4/evidence/
  timings.md` with each row's wall clock (plan §7.4: "recorded for the next run's planning");
- last line `CLOSE LANE GREEN | CLOSE LANE RED`, exit 0 / 1.

**Running the workflow's own step text.** Wherever a row says "step `<name>`", the script extracts that step's `run:`
block from `.github/workflows/manager-ci.yml`, removes its indentation, and runs it with `C:/Program
Files/Git/bin/bash.exe -c`. The environment adds `RUNNER_OS=Windows` and `RUNNER_TEMP=<log dir>`, and the cwd is
`apps/krate-manager/`, so local and CI logic cannot drift.

Rows:
- **A0:** `rustc --version`, `node --version`, free space on the repo's drive (`df -h` of the repo root only; Rule 13).
  Records only.
- **A0b:** `git status --porcelain` captured, for C2.
- **A (concurrent):**
  - **A1**, one chain (the cargo commands share one target-dir lock): step `cargo fmt` → step `clippy` → step `clippy
    dev-feed` → step `clippy-guard` (its `::notice`; copy `guard.txt` over `evidence/clippy-guard.txt`) → step `cargo
    test` → `cargo test -p krate-manager-core check_under_temp_symlink_escape -- --nocapture` (records whether the link
    was made) → `cargo tree -p krate-manager-core -e normal > evidence/core-tree.txt` (no `tauri`, `dirs`,
    `directories`, `home`).
  - **A2**, one chain: `npm ci` → step `typecheck` → step `lint` → step `vitest`.
  - **A3**, from the repo root: `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` to a TAP file,
    then L-TAP on it (`TAP plan=1..59 top-level=59`); L-VLINT.
  - **A4:** L-YAML on `manager-ci.yml` and `feed-tools.yml`.
  - **A5:** L-LINT.
  - **A6:** `node tools/gen-specs-index.js --check`.
  - **A7:** `scope-check.js --self-test`, then L-SCOPE.
  - **A8:** L-XCHECK when both targets are installed, else the row reads `SKIPPED (targets not installed; ruling
    [P-3])`.
  - **A9:** the SC-021 grep `grep -rnE -- '--test-threads|--no-file-parallelism|fileParallelism|maxWorkers|pool:'
    apps/krate-manager --exclude-dir=node_modules --exclude-dir=target` prints nothing.
- **B (after A1 and A2):**
  - **B1:** step `required tests ran` (`::notice title=required tests ran (Windows)::49 cargo + 3 vitest`).
  - **B2:** step `tauri build` (40 min ceiling; it downloads the SHA-1-pinned NSIS toolset into `%LOCALAPPDATA%\tauri` on
    first use, spec Safety).
  - **B3:** step `build warnings`.
  - **B4:** step `dev-feed absent`.
  - **B5:** step `artifact version` (`ProductVersion` `0.1.0` and the NSIS file `target/release/bundle/nsis/Krate
    Manager_0.1.0_x64-setup.exe`).
- **C (last):**
  - **C1:** `git status --porcelain` again, required equal to A0b. The builds and tests add no tracked or unignored
    file. Step `Checkout unchanged` itself is not run locally: the phase's own uncommitted files would fail it until
    the user commits.
  - **C2:** L-SCOPE again.

**Run in this task.** Each of these is short. Record each one's wall clock and last result line:
- L-NODE whole set and L-TAP;
- L-YAML on both workflows;
- L-LINT;
- `node tools/gen-specs-index.js --check`;
- `scope-check.js --self-test` and L-SCOPE;
- L-RS, L-DEVFEED and L-TS once (each well under 5 minutes once T012 has warmed `target/`);
- L-GUARD;
- the SC-021 grep;
- `node --check` on both evidence scripts.

**Not run here**, with the reason in the notes:
- `npm run tauri build` and B3-B5: deferred to the main loop at close: run `node
  specs/krate-manager-phase4/evidence/close-lane.js`.
- `node tools/check-portability.js`: no C++ file changed (FR-090).
- `node tools/lint-plugin-editor-baseline.js`: no plugin editor changed; `apps/` is outside its `plugins/` walk, spec
  "Existing repo pieces".
- `run-gate.js` / `header-consumers.js`: no C++ lane, plan §8.
- pluginval: no plugin binary changed.
- the CPU lane: no timing test.
- clang-tidy: no C++.

Then run `node tools/clean-test-output.js --apply` and remove `F:/tmp/km4-template/`.

**Done when:**
- every command above passes, and `close-lane.js` passes `node --check`;
- the notes say "deferred to the main loop at close: run `node specs/krate-manager-phase4/evidence/close-lane.js`";
- the PENDING-CI table below matches the job names in `manager-ci.yml` (`check (${{ matrix.os }})` with matrix
  `[windows-latest, macos-latest, ubuntu-24.04]`) and `feed-tools.yml` (`node-test (${{ matrix.os }})` at `:41`, matrix
  at `:45-46`).

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

`manager-ci.yml` runs on the user's push of the branch: its path filter lists `apps/krate-manager/**`, the workflow
itself and `tools/lint-manager-version.js`. `feed-tools.yml` runs on the same push: after T017 its filter lists the
lint, `manager-ci.yml`, `apps/krate-manager/package.json`, `tests/required-tests.json` and `tools/feed-contract/**`. The
token this environment has reads check-run annotations but not job logs (spec D21), so every row closes on an
annotation or a step result.

A Rust or Vitest row closes only on its leg's `required tests ran (<os>)` `::notice`, whose list holds the row's test
names. A green `cargo test` step alone closes nothing.

| SC | Workflow · job (exact name) · step | Assertion that closes the row |
|---|---|---|
| SC-003 | `manager-ci.yml` · `check (macos-latest)` · `required tests ran` | `::notice title=required tests ran (macOS)::49 cargo + 3 vitest`, whose `macos` list holds `platform::macos::tests::default_roots_from_env` |
| SC-004 | `manager-ci.yml` · `check (ubuntu-24.04)` · `required tests ran` | `::notice title=required tests ran (Linux)::49 cargo + 3 vitest` (`linux` list: `platform::linux::tests::default_roots_from_env`) |
| SC-005, SC-007 to SC-011, SC-015 to SC-018, SC-024 to SC-031 (Rust / Vitest parts) | `check (macos-latest)`, `check (ubuntu-24.04)` · `required tests ran` | the same two notices: `all` (48 names) and `vitest` (3 titles). This covers POSIX rename and directory fsync (SC-011), `flock` (SC-025), the symlink case with `CI` set (SC-007) and macOS `/private/var` canonicalization (SC-007) |
| SC-006 | `check (macos-latest)`, `check (ubuntu-24.04)` · `clippy-guard` | `::notice title=clippy-guard (<os>)::rc=<non-zero> disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=<n≥1> other-errors=0` |
| SC-012, SC-019, SC-022 | `feed-tools.yml` · `node-test (macos-latest)`, `node-test (ubuntu-latest)`, `node-test (windows-latest)` · `node --test` | the step is green and prints `TAP plan=1..59 top-level=59`. That step emits no annotation (a Phase 1 step, unchanged), so the run's job conclusion `success` is read through the jobs API; step `Checkout unchanged` is green. The real tree's version lint: `::notice title=version lint (<os>)` on all three `check` legs |
| SC-013 | `check (macos-latest)` · `plist: LSMinimumSystemVersion`, `plist: no LSFileQuarantineEnabled`, `codesign verify`, `codesign ad-hoc` | four `::notice`s: `15.0`; `absent`; `exit 0`; `Signature=adhoc` |
| SC-014 | `check (macos-latest)`, `check (ubuntu-24.04)` · `artifact version` | `::notice title=artifact version (macOS)::0.1.0 (app)` and `… (Linux)::0.1.0 (deb)`. Windows closes locally (L-CLOSE B5) and on `check (windows-latest)` |
| SC-020 | all three `check` legs · `Checkout unchanged` | `::notice title=checkout unchanged (<os>)::clean` |
| SC-031 (CI part) | `check (macos-latest)`, `check (ubuntu-24.04)` · `clippy dev-feed`, `dev-feed absent` | `::notice title=clippy dev-feed (<os>)::clean` and `::notice title=dev-feed absent (<os>)::marker in source=<n≥1>, in target/release/krate-manager=0` |
| SC-001 (CI half) | `check (windows-latest)` · every step | every step green, with `build warnings (Windows)` `::notice`; the local half closes on L-CLOSE |

**Every Windows row closes locally:**
- SC-001, SC-006, SC-014 (Windows), SC-020 (local half) and SC-021 from L-CLOSE;
- SC-002, SC-005, SC-007 to SC-011 and SC-015 to SC-018 from L-RS / L-TS, plus L-CLOSE B1's
  `required tests ran (Windows)` notice;
- SC-024 to SC-031 (Rust parts) from the same two;
- SC-012, SC-019, SC-022 from L-NODE + L-TAP;
- SC-023 from L-SCOPE.

**User steps after the push** (not exit criteria): read the annotations above. Under ruling `[P-2]` (b), launch the
built app once, then a second instance.

---

## Defects found

None yet. A gate fixer or the main loop appends here: the red, its cause, the task that owns the file, and the fix or
the ruling it needs.
