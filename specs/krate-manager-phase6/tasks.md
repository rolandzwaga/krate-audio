# Tasks: Krate Manager Phase 6 — Detection and the Status Screen

**Spec:** `specs/krate-manager-phase6/spec.md` (DRAFT revision 3: D1-D15, FR-001 to FR-092, SC-001 to SC-035;
Clarifications Q1-Q8 of 2026-10-31, `spec.md:1358-1400`) · **Plan:** `specs/krate-manager-phase6/plan.md` (revision 2:
PD-1 to PD-22, SI-1 to SI-21, open questions Q-A to Q-C, `plan.md:30-89`, `:1257-1276`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 6 (lines 557-595); Phase 10 `run_inno_uninstaller` (lines 714-719);
Cross-Cutting Constraints (lines 975-1016)
**Status:** DRAFT task list, 2026-10-31, written against `feat/krate-manager` at `3efe70de` (`git rev-parse --short
HEAD`; the only untracked path is `specs/krate-manager-phase6/`). `3efe70de` is also the last commit touching
`apps/krate-manager/` (`git log --format='%h %ci' -1 -- apps/krate-manager` = `3efe70de 2026-10-31 15:24:50 +0100`), so
every file:line the plan cites at `3efe70de` holds; the ones this file quotes were re-read this session.
**Spec state:** none of plan §0.2's edits is in `spec.md` yet (this session, `grep -c` over `spec.md`: `root_index` 0,
`OS_FACTS_RS` 0, `listing_case_exact_unix` 0, `listing_skips_junction_windows` 0, `toStatusView` 0, `managed_rank` 0,
`/usr/bin/getconf` 0, `tauri-plugin-fs` 0). The plan asks for them before the build (`plan.md:15-18`); T001 applies
them in parallel with T002 (disjoint files), as Phase 5's T001 did (`specs/krate-manager-phase5/tasks.md:307`).
**Lanes in this phase:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm
typecheck / lint / test, the version lint, `tauri build`, the `cargo tree` records), the **pipeline lane** (`node --test`
over `tools/feed-contract/tests/`, because `manager-ci-shape.test.js` changes; YAML parse; the repo lints) and **L-WSL**
(core clippy + tests in WSL Ubuntu: the only local compile of the `cfg(target_os = "linux")` / `cfg(unix)` arms, plan
§8; a main-loop lane). No file under `plugins/`, `dsp/`, `cmake/` or `extern/` changes (spec FR-090), so there is **no
C++ lane**: no `run-gate.js`, no `header-consumers.js`, no `check-portability.js`, no clang-tidy, no pluginval, and no
`lint-plugin-editor-baseline.js` (no plugin editor changes; plan §8). `npm run tauri build`, L-WSL and the full close
procedure are main-loop lanes (T010 writes their scripts, T013 hands them over).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks at the same time in one working tree. A task is
  **[P]-safe** when no other task in its group touches any of its files and none of its files is a **shared file**:
  any `Cargo.toml`, `Cargo.lock`, `clippy.toml`, `crates/core/src/lib.rs`, `src-tauri/src/lib.rs`, a `mod.rs`,
  `package.json`, `tauri*.conf.json`, a capability file, `src-tauri/build.rs`, a workflow, `tests/required-tests.json`,
  `manager-ci-shape.test.js`, `spec.md`, the roadmap or `specs/INDEX.md`. **Every app-side shared file is edited in one
  task only, T002, before anything else**: it writes every `pub mod` / `mod` line, every manifest entry, the trait
  extension of `platform/mod.rs` with all three implementations, the full public types of the three new core modules
  with `todo!()` bodies, the test fakes, and the final `src-tauri` wiring, so the nine G2 tasks touch only their own
  files. The files a G2 task owns are either placeholders T002 created for it, new files, or an existing file that
  exactly one task in the group edits (`roots.rs` → T003; `injection.rs` → T007; `config_guard.rs` → T008; `roster.rs`
  → T009; `src/main.ts`, `src/main.test.ts`, `src/styles.css` → T011). `spec.md` is touched only by T001 (G1) and T013
  (G3, the SI-14 rows only); the workflow, `required-tests.json` and the shape test only by T012 (G3).
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block is too long to copy without drift (a type list,
  a test table, an algorithm), the section names the `plan.md` section, the §7.1 row or the spec FR / SC line range
  that holds it; that text is then **binding, verbatim**, as amended by plan §0.2 (SI rows, `plan.md:57-81`) and this
  file's TA rows. Where this file and the plan differ, this file wins and the TA row says why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the notes quote the failing line. The
  red run normally uses T002's placeholder, so the test compiles and fails on the `todo!()` panic ("not yet
  implemented") or on its assertions. A test that cannot run on this box (`cfg(target_os = "macos")`,
  `cfg(target_os = "linux")`, `cfg(unix)`) has no local red: its red-first is recorded as "not compiled here" and it is
  read twice against its SC before the task ends (Rule 15).
- **In-group expected reds.** In G2, tasks depend on each other's code inside the group (status tests run detection,
  detect tests go through `status::assemble`, the guards scan the modules, the frontend imports the goldens). A test that
  can go green only when another G2 task lands is listed under "Expected red until" in its task, with the task that turns
  it green and the only failure text that counts as expected. **Every such red is gone by the G2 gate** (all of G2 has
  landed by then); any red at a gate is a real red. No red is carried across a group boundary.
- **Main-loop lanes.** Steps longer than ~5 minutes run only in the main loop, from scripts T010 writes:
  `specs/krate-manager-phase6/evidence/wsl-lane.js` (L-WSL; Phase 5 measured 158.6 s warm,
  `specs/krate-manager-phase5/evidence/timings.md` row A11) and `specs/krate-manager-phase6/evidence/close-lane.js`
  (L-CLOSE: the release `npm run tauri build`, 318 s in Phase 5's row B2, plus L-WSL and the full plan §7.4 procedure).
  T013 returns with notes "deferred to the main loop at close: run `node
  specs/krate-manager-phase6/evidence/close-lane.js`".
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system.** Every test builds its roots and `AppContext` from `test_support::fresh_temp_roots()`
   (`crates/core/src/test_support.rs:125-129`, returns `(TempDir, Roots)`, sandboxed to the `TempDir`) and writes only
   inside that `TempDir`. Every host fact comes from `test_support::FakeHostFacts` or `FakeHostIo` (T002; spec FR-083):
   no test names `OsHostIo`, reads the registry, runs `pkgutil`, `sw_vers` or `getconf`, or reads a real plug-in,
   Components, preset or receipt folder, `ProgramData` or `%LOCALAPPDATA%`. Every feed comes from a fixture file read in
   place (`tools/feed-contract/fixtures/feeds/valid/`, `feed_tests.rs:50-53`) or a `FixtureServer` on loopback
   (`fixture_server.rs:115-165`; `HttpConfig::loopback`); no fixed port, no global mutex (FR-088). The only processes a
   `crates/core` test starts are `sleep` and `printf` (Phase 5, `injection.rs:232-233`) and the two TA-1 programs in
   their two test bodies. No test calls `std::env::temp_dir`, `var`, `var_os`, `home_dir` (`apps/krate-manager/clippy.toml`
   bans them; `test_support::canonical_temp_dir` is the one site). No task launches the built app or the elevated
   helper. The user may audition with `npm run tauri dev` (read-only against the real folders); that is never a task.
2. **Suppressions.** `[workspace.lints.clippy] allow_attributes = "deny"` (`apps/krate-manager/Cargo.toml:38-40`): never
   `#[allow]`; any `#[expect(…, reason = "…")]` needs a reason the reviewer accepts. No new `clippy::disallowed_methods`
   site: `D6_SITES` / `D6_TOTAL` stay at 6 files / 8 sites (`injection.rs:73-81`). No `// @ts-ignore`, no
   `eslint-disable`.
3. **Unit tests live in `<stem>_tests.rs`**, declared in `<stem>.rs` as `#[cfg(test)] #[path = "<stem>_tests.rs"] mod
   tests;` (the existing pattern, e.g. `platform/mod.rs:98-100`; inline `mod tests {` is reported by `injection.rs`
   `RULE_INLINE_TESTS`).
4. **Literal hygiene.** (a) No non-test `crates/core/src` file outside `platform/` holds, comments included, the
   substrings `"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`, `krateaudio.com`, `api.github.com`
   (`injection.rs:15-23`); write doc comments without a leading quote before such a path. (b) Every string literal in a
   non-test `platform/` file is one of `PLATFORM_LITERALS` (37 after T002). (c) No `"Krate Audio"` literal outside
   `platform/` (FR-002; use `platform::VENDOR_DIR`). (d) The six FR-088 files hold none of its eleven regexes (no
   `/usr/`, `/Library`, `AppData`, `ProgramData`, `Program Files`, `HKEY_`, `%NAME%`, `127.0.0.1:<port>`,
   `localhost:<port>`, `--test-threads`, `OsHostIo`). (e) **No file under `apps/krate-manager/` names any of the nine
   shipped plugins**, tests and fixtures included (ROSTER; `roster.rs:128`): invented names `Alpha` … `Juliet` only.
   (f) No non-test source holds `com.krateaudio.<x>` (except `APP_IDENTIFIER`, `platform/mod.rs:9`), an Inno GUID shape
   or `_is1` (FR-089); tests and fixtures may.
5. **Zero warnings.** `cargo fmt --all --check`; every `cargo clippy … -- -D warnings` with 0 `warning:` lines;
   `npm run typecheck`; `npm run lint` (`eslint . --max-warnings 0`, `package.json:9`); `node --check`. Every
   `#[expect]` must be fulfilled.
6. **Parallel tests.** Never `--test-threads`, `--no-file-parallelism`, `--fileParallelism=false`, `--maxWorkers`,
   `-j 1`, `--test-concurrency=1`. `cargo test` uses its default threads; `vitest run` its parallel files; `node --test`
   runs files in parallel.
7. **`cargo test` is `cargo test --workspace --locked` or `cargo test -p krate-manager-core --locked …`, never with
   `--all-targets`** (that builds a test harness for `src-tauri`'s lib and bin despite `test = false`,
   `src-tauri/Cargo.toml:13,19`; Phase 4 ruling P-4). `cargo clippy --workspace --all-targets` is correct.
8. **Lockfile.** Only T002 changes `Cargo.lock`, once, additively (plan §4.1); every other cargo command in every task
   uses `--locked`. Never `cargo update` except T002's `--precise` revert of plan §4.1. **No task after T002 adds a
   dependency or a feature**: a task that finds it needs one stops, returns `blocked` and names the crate and why.
9. **Placeholder discipline (TA-4).** T002's placeholders compile with zero warnings: every parameter of a `todo!()`
   body is `_`-prefixed; a placeholder declares only `pub` items, or `pub(crate)` items a `pub` item of the same file
   uses; a placeholder struct has no private field nothing reads (TA-4 gives `Listing` the field `_private: ()`); a
   placeholder imports only what it uses. The owning task renames parameters as it implements. At close no `todo!(` or
   `unimplemented!(` remains in any workspace `.rs` file outside `*_tests.rs` and `tests/` (L-CLOSE row A10).
10. **One working tree, many writers (G2).** Write each file whole, so that it compiles at every save (types and
    signatures first, bodies after). Never edit a file another task owns; when `cargo` fails on a file you do not own,
    wait 60 s and retry, three times, then record it in your notes and continue with what you can verify. Run your
    own-module loop (L-OWN) in the **target slot** your task names, with its `CARGO_BUILD_JOBS` (TA-10): slot `detect`
    (T003, 10 jobs), slot `model` (T004 and T005, 8 jobs), slot `guard` (T007, T008, T009, 6 jobs), and the shared
    `apps/krate-manager/target` for T006 (6 jobs, the only G2 user of it, warm `tauri`). Tasks sharing a slot queue on
    its build lock; that is accepted. The first run in a slot pays one cold build of core's dependencies; record its
    wall clock. Before the first build run `df -h /f` (13 GB free this session): with less than 6 GB free, use slot
    `model` instead of your own and say so in the notes. Delete your slot directory when the last task using it ends
    (the slot's last finisher deletes it: check `ls F:/tmp/km6-target/` and the group's task states in your notes).
11. **Scope fence (spec FR-090).** Outside `apps/krate-manager/**` only these paths change:
    `.github/workflows/manager-ci.yml` (one step, T012), `tools/feed-contract/tests/manager-ci-shape.test.js` (that
    step in three lists, T012), `specs/krate-manager-phase6/**`, `specs/INDEX.md` (regenerated only), and in
    `specs/Krate-manager-roadmap.md` only the Phase 10 sentence of Q-C (T013) plus the Phase 6 status line (main loop at
    close). Inside `apps/krate-manager/`: no change to `index.html`, `tauri*.conf.json`, `package.json`,
    `package-lock.json`, `crates/helper/**`, `eslint.config.mjs`, `vite.config.ts`, `tsconfig*.json`, `clippy.toml`,
    `CHANGELOG.md` (Q-B, plan §10) or `version.json` (plan §2, `plan.md:202-204`).
12. **Line endings.** This checkout runs `core.autocrlf=true`. Every pre-existing file this phase edits is CRLF in the
    working tree (measured this session, `grep -c $'\r$'` = `wc -l`): `Cargo.toml` 47/47, `crates/core/Cargo.toml`
    40/40, `src-tauri/Cargo.toml` 46/46, `crates/core/src/lib.rs` 21/21, `platform/mod.rs` 100/100, `platform/windows.rs`
    55/55, `platform/macos.rs` 53/53, `platform/linux.rs` 55/55, `roots.rs` 207/207, `test_support.rs` 194/194,
    `tests/injection.rs` 2747/2747, `tests/config_guard.rs` 1655/1655, `tests/roster.rs` 189/189, `src-tauri/src/lib.rs`
    91/91, `src-tauri/src/commands.rs` 14/14, `src-tauri/build.rs` 8/8, `capabilities/main.json` 6/6, `src/main.ts`
    26/26, `src/main.test.ts` 70/70, `src/styles.css` 48/48, `tests/required-tests.json` 182/182, `manager-ci.yml`
    292/292, `manager-ci-shape.test.js` 856/856, the roadmap 1052/1052, `spec.md` 1400/1400, `specs/INDEX.md` 377/377.
    An edit keeps the file's ending on every line it adds. New files are written LF. The committed fixtures under
    `tests/fixtures/detect/` and `tests/fixtures/status/` are LF and protected by T002's `tests/fixtures/.gitattributes`
    (`-text`, PD-11). `specs/INDEX.md` is written only by `tools/gen-specs-index.js`.
13. **Shell discipline.** Every command runs in the Bash tool (Git Bash), foreground, output to a log under
    `F:/tmp/gate/km6-<task id>/`, then `tail`; never `cat` a whole log, never a drive-wide `find` / `du`. No heredoc in
    a Bash command (a repo hook blocks them): write scripts with the Write tool and run them with `node`. All app-lane
    commands run from `apps/krate-manager/`; repo-lane commands from the repo root.
14. **No invented APIs.** Every crate / npm API used is one plan §1.2 verified (`plan.md:140-152`): `pulldown-cmark`
    0.13.6 (`Parser::new_ext`, `Options::empty()`, `Event`, `Tag`, `TagEnd`, `HeadingLevel` with explicit
    discriminants), `plist` 1.10.3 (`Value::from_reader`, `as_dictionary`, `as_string`, `Dictionary::get`; tests:
    `Value::to_writer_binary`), `windows-sys` 0.61.2 (`RegOpenKeyExW`, `RegGetValueW`, `RegCloseKey`, `HKEY`,
    `HKEY_LOCAL_MACHINE`, `KEY_READ`, `KEY_WOW64_64KEY`, `RRF_RT_REG_SZ`, `ERROR_SUCCESS`, `ERROR_FILE_NOT_FOUND`,
    `STATUS_SUCCESS`, `RtlGetVersion`, `OSVERSIONINFOW`), `tauri-plugin-opener` 2.7.2 (the free fn
    `tauri_plugin_opener::open_url(url, None::<&str>)` only, PD-20), `tauri` 2.12.5 (`async_runtime::spawn_blocking`),
    `url` 2.5.8, `@tauri-apps/api` 2.12.4 (`invoke`, `mocks.mockIPC`, `clearMocks`). An API that turns out to differ is
    a defect of the plan: record it under "Defects found" with the crate source line, and use the real API.
15. **`cfg` discipline.** The `cfg(target_os = "macos")` code (`read_au_plist`, `MacosPlatform`'s methods, the macOS
    `pub use` line, every `_macos` test) is never compiled on this box, and `os_facts.rs`'s `cfg(not(windows))` stubs
    and the `cfg(target_os = "linux")` / `cfg(unix)` code compile only in L-WSL (core only) and CI (plan §7.3, §8).
    Write them with the same care as the Windows arms, read each against its FR / SC twice, and gate every `use` the way
    its only users are gated (an unused import is a warning on the leg that compiles it). Rust 2024
    (`apps/krate-manager/Cargo.toml:8`) warns `unsafe_op_in_unsafe_fn`: every unsafe operation sits in its own
    `unsafe { }` block with a `// SAFETY:` line, like `src-tauri/src/os_proxy.rs`.
16. **Bounded waits are hang guards, not speed bars.** SC-027's FIFO case waits with `recv_timeout(5 s)`; `pkgutil` /
    `sw_vers` bounds are the code's `PKGUTIL_WAIT` / `OS_VERSION_WAIT`. Never widen a bound; a timing red while other
    tasks compile is rerun once alone before it is called a defect, and the notes record both runs.

### Lanes (referenced by every task)

| Lane | Commands | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked 2>&1 \| tee F:/tmp/gate/km6-<task>/cargo-test.txt` (Rule 7) | fmt exit 0; clippy exit 0 with 0 `warning:` lines; every test binary `0 failed` |
| **L-OWN** | `CARGO_TARGET_DIR=F:/tmp/km6-target/<slot> CARGO_BUILD_JOBS=<n> cargo test -p krate-manager-core --locked <filter>` and `CARGO_TARGET_DIR=… CARGO_BUILD_JOBS=… cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings` (Rule 10; a working loop, not a gate) | the task's own tests and 0 `warning:` lines |
| **L-APP** | `CARGO_BUILD_JOBS=6 cargo clippy -p krate-manager --all-targets --locked -- -D warnings` (shared target; T002, T006) | exit 0, 0 `warning:` lines |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (`:97-115`), executed with `C:/Program Files/Git/bin/bash.exe -c` from `apps/krate-manager/` with `RUNNER_TEMP=F:/tmp/gate/km6-<task>` and `RUNNER_OS=Windows` | `::notice title=clippy-guard (Windows)::rc=<non-zero> … other-errors=0` (Phase 5's form, `phase5/evidence/timings.md` row A1e; unchanged by this phase) |
| **L-TS** | `npm run typecheck`; `npm run lint`; `npm test` | each exit 0 |
| **L-NODE** | `node --check <each new or changed .js>`; `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km6-<task>/all.tap` from the repo root (quoted glob); read with `tail -12` | TAP `# fail 0`; no `Warning:` / `DeprecationWarning` / `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km6-<task>/all.tap` | exit 0 and `TAP plan=1..59 top-level=59` (the shape test changes only inside existing cases, plan §4.13; `feed-tools.yml:86` `PLAN = "1..59"`) |
| **L-YAML** | `npx --yes yaml@2.6.1 valid < .github/workflows/manager-ci.yml` | exit 0 |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root) | exit 0, no stderr |
| **L-INDEX** | `node tools/gen-specs-index.js --check` (repo root) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase6/evidence/scope-check.js` (T010) | exit 0 |
| **L-WSL** (main loop) | `node specs/krate-manager-phase6/evidence/wsl-lane.js` (T010; TA-8): in WSL, `CARGO_TARGET_DIR=$HOME/km-target` (exists from Phase 5, `ls` this session), `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings && cargo test -p krate-manager-core --locked` | last line `WSL LANE GREEN …` and the six Linux-run names of TA-8 seen `ok`; it never closes a PENDING-CI row (plan §7.3, §8) |
| **L-WSL-OWN** | T003 only: `timeout 300 wsl -e bash -lc 'cd /mnt/f/projects/iterum/apps/krate-manager && CARGO_TARGET_DIR=$HOME/km-target cargo test -p krate-manager-core --locked detect::'` | the `_linux` / `_unix` tests; a timeout (exit 124: the first run downloads and builds `pulldown-cmark`) is not a red, the notes record it and the verdict is L-WSL's |
| **L-CLOSE** | `node specs/krate-manager-phase6/evidence/close-lane.js` (T010; **main loop only**) | last line `CLOSE LANE GREEN` |

**Group gates** (run by the workflow's gate reporter). After G1: L-RS (nothing else executing), L-APP, L-DEVFEED,
L-GUARD, L-TS, L-VLINT; beside it the main loop may run L-WSL (WSL's own cores): the first compile of `linux.rs`'s new
`os_version` and of the Linux arms of the fakes. After G2: L-RS, L-APP, L-DEVFEED, L-GUARD, L-TS, L-VLINT, `node --check`
on T010's three scripts, and L-WSL beside it (main loop). After G3: everything T013 lists, then L-CLOSE (main loop).

---

## Preconditions — rulings needed before the build stage

Plan §10 (`plan.md:1257-1276`) leaves three questions with recommended answers, and this file adds one (TA-1). Starting
the build stage after the user's review of spec, plan and tasks is their acceptance of every recommended answer they do
not overrule.

- **Q-A (plan SI-11, PD-13):** a rejected "Check now" keeps the previous cards and shows the error in the feed line.
  Recommended; T011 builds it.
- **Q-B:** no `CHANGELOG.md` bullet for the status screen unless the user asks (Phase 5 added none). Recommended; no
  task touches `CHANGELOG.md`.
- **Q-C (plan SI-14, a re-ruling of Q2):** the folder HKLM `InstallLocation` names can be user-chosen and unelevated-
  writable, so Phase 10's elevated uninstall must not trust it. Recommended (a): keep the roadmap's "under the
  compiled-in Windows plug-in root" clause and add one sentence sending copies outside it to Windows Settings. **No
  Phase 6 code depends on it**; only the spec's FR-092 / SC-028 / D1 rows and the roadmap hunk do (T013).
- **TA-1 (new, this file):** the plan's two process-starting test cases (a FIFO made by `mkfifo` for SC-027, NTFS
  junctions made by `cmd /C mklink /J` for SC-029) contradict spec FR-086 ("No test … starts a process other than the
  test binary") and Phase 5's guard (`injection.rs:1669-1686`: a `crates/core` test may start only `sleep` and
  `printf`). Recommended: allow exactly `mkfifo` in `detect::tests::probe_metadata_error_unix` and exactly `cmd` in
  `detect::tests::listing_skips_junction_windows`, both acting inside the test's `TempDir`, through a body-scoped allow
  list in `injection.rs` (T007). The alternative is to drop both cases (and with them the only observable proof of
  PD-22 and the only Windows listing-link test).

**Precondition check (T001 and T002 run it first; on failure they change nothing and return `blocked`, quoting the
item):** in `spec.md` `## Clarifications`, a bullet that rules **against** Q-A's recommendation, against TA-1's
recommendation, or that marks any plan SI row or this file's TA row rejected. (Absence of a bullet is acceptance, see
above.) The main loop then re-plans the named sections and regenerates the affected tasks. **Q-C is checked by T001 and
T013 only**: when `## Clarifications` holds no bullet recording Q-C's answer, T001 skips SI-14 and notes "SI-14 deferred
to T013"; T013 returns `blocked` (the last task of the phase, so everything else has landed) until it does.

### Task-list amendments (TA)

Rows marked **spec** change spec text (T001 writes them, tagged `(tasks TA-n)`); the others explain why a task differs
from a plan block.

| Id | Amendment | Why | Spec? |
|---|---|---|---|
| TA-1 | `injection.rs` gains `const TEST_PROGRAM_BODIES: [(&str, &str, &str); 2] = [(DETECT_TESTS_RS, "probe_metadata_error_unix", "mkfifo"), (DETECT_TESTS_RS, "listing_skips_junction_windows", "cmd")]` (file, enclosing `fn`, program), and the existing `Command :: new (` rule of `test_source_findings` (`injection.rs:1669-1686`) accepts a literal program that is in `TEST_PROGRAMS` **or** that matches the triple of its enclosing `fn` (`enclosing_fn`, as `BODY_RULES` uses it at `:1636-1648`). `production_config_only_in_allowed_tests` (`:2535`) gains two plants, each reported: `Command::new("mkfifo")` in a planted `fn planted()` of `detect_tests.rs`, and `Command::new("cmd")` in a planted `fn planted()` of `status_tests.rs`. Programs are bare names resolved through `PATH` (FR-088 bans `/usr/` in `detect_tests.rs`) | plan §7.1 rows 027 (PD-22's FIFO) and 029 (SI-20's junctions) start processes; spec FR-086 and the Phase 5 guard forbid that; std has no `mkfifo` and no junction API, and adding a dev-dependency is a ruling (Rule 8) | spec: FR-086 gains "…other than the test binary, except Phase 5's `sleep` / `printf` and, each in exactly one test body acting inside its `TempDir`, `mkfifo` (SC-027) and `cmd /C mklink /J` (SC-029) (tasks TA-1)"; SC-027 and SC-029 name the program |
| TA-2 | FR-083's scan covers `crates/core/src/test_support.rs` **explicitly**, beside the `is_test_source` files minus `injection.rs` | `is_test_source` (`injection.rs:1242-1249`) matches only `*_tests.rs` under `src` and files under `crates/*/tests/`, so plan §4.12's "over `is_test_source` files" would skip `test_support.rs`, which FR-083 lists and which holds the fakes | spec: FR-083 (the file list names the helper) |
| TA-3 | `roots.rs::check_sandboxed` (plan §4.4) lands in **T003** with its first caller, not in T002; T003 owns `roots.rs` in G2 | an uncalled `pub(crate) fn` warns `dead_code`, which would make the G1 gate red | no |
| TA-4 | Rule 9; the placeholder `Listing` is `#[derive(Clone, Debug, Default)] pub struct Listing { _private: () }`; T003 replaces the field with plan §4.5.4's `matches: Vec<ListedMatch>` | the derived `Debug` / `Clone` are not reads, so a private `matches` field in a placeholder warns `dead_code` | no |
| TA-5 | The 13 golden views are written by **T005 in G2**, after T004's `notes::tests::blocks_cases` is green in slot `model` (polled every 60 s, ceiling 45 min); `golden_views` collects every mismatch and panics once, printing each scenario's expected text, so one run yields all files | the goldens depend on `assemble` (T005) and `notes::blocks` (T004) only; writing them in G2 lets T011's vitest cases and every G2 gate run against them, instead of a G3 golden task followed by a G4 frontend task (two more gates, ~45 min of wall clock) | no |
| TA-6 | T002 adds `test_support::tests::fakes_record_calls_and_answer_defaults` (not in `required-tests.json`): every call of both fakes is recorded in the plan §4.8 format and every unlisted input answers its default | ~15 required tests trust the fakes' call lists (SC-008, SC-009, SC-014, SC-018, SC-025); nothing else tests them | no |
| TA-7 | The new `src-tauri/Cargo.toml` entry is written on one line, `tauri-plugin-opener = { workspace = true }`, and **no other line of that file holds the substring `tauri-plugin`**, its full-line comment included (`# Phase 6 D15, Q8: commands.rs only, the free fn open_url; never registered`) | the Phase 4 rule reports every line containing `tauri-plugin` (`config_guard.rs:688-695`); SI-2 narrows it to exactly that one entry line, so plan §4.1's comment wording must not repeat the crate name | no |
| TA-8 | L-WSL runs the full `cargo test -p krate-manager-core --locked` (Phase 5's form, 150 tests in 148 s) instead of plan §8's `detect:: status:: notes::` filter, and its information line counts the Linux-run Phase 6 names `native_layout_linux`, `linux_detection_linux`, `host_facts_through_fake_io_linux`, `probe_metadata_error_unix`, `listing_skips_symlinked_dirs_unix`, `listing_case_exact_unix` | the guard integration tests (`injection`, `config_guard`, `roster`) lex sources and compare paths, which can differ on Linux; the full run is a superset at ~2.5 min | no |
| TA-9 | `test_sources_hold_no_real_location` reports a scanned file it cannot read as `<file>: missing` (a finding), so the guard cannot pass vacuously on a renamed file | FR-088 names exactly six files; a scanner that silently skips a missing one proves nothing | spec: FR-088 ("a file it cannot read is a finding") |
| TA-10 | Rule 10's target slots | nine G2 tasks, seven of them building core: one private slot each would need ~7 cold builds and ~10 GB on a drive with 13 GB free (`df -h /f` this session; the workspace `target/debug` is 5.0 GB, `du -sh`); three shared slots cost three cold builds and ~4.5 GB, with lock queueing only between tasks that share a slot | no |
| TA-11 | Every view struct and enum of plan §4.6 derives `Clone, Debug, PartialEq, Eq, Serialize`; the unit-variant enums also `Copy`; `notes::Block` / `Inline` derive `Clone, Debug, PartialEq, Eq, Serialize` (plan §4.7) | plan §4.6 writes `#[derive(…, Serialize)]`; one list keeps T002's placeholder and the tests consistent | no |
| TA-12 | T002 writes `src-tauri/src/commands.rs` and `lib.rs` in their final form (plan §4.9) against the placeholders | both are shared, glue-only, untested by design (`test = false`); writing them once in G1 keeps G2 free of shared files | no |
| TA-13 | The base binary size is read now from the existing `target/release/krate-manager.exe` (4,314,624 bytes, mtime 2026-10-31 15:22, built by Phase 5's close lane, `phase5/evidence/timings.md` row B7, from the app tree committed as `3efe70de` at 15:24) and written by T001 to `evidence/base-binary-size.txt` before anything rebuilds it | L-CLOSE reports the release-binary delta (Phase 5 row B7's form); the app sources at HEAD equal that build's, so no base rebuild is needed | no |

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | **[P]** (2) | T001 precondition, spec amendments, base records · T002 precondition, manifests, lockfile, every shared line, `Platform` + `HostIo` + three impls, all placeholders, fakes, `src-tauri` wiring, the guard constants the manifests move, `.gitattributes` | Preconditions |
| G2 Modules, guards, screen, scripts | **[P]** (9) | T003 `core::detect` (+ `roots.rs`, detect fixtures) · T004 `core::notes` · T005 `core::status` + the 13 goldens · T006 `src-tauri/src/os_facts.rs` · T007 `injection.rs` guards · T008 `config_guard.rs` guards · T009 `roster.rs` guard · T010 evidence scripts · T011 the status screen (TypeScript) | G1 |
| G3 Integration | sequential | T012 `manager-ci.yml`, the shape test, `required-tests.json` · T013 Q-C rows, the roadmap hunk, INDEX, lints, cleanup, PENDING-CI, hand-over of L-CLOSE | G2 |

Why this shape: T002 fixes every shared line and every public type and signature of the phase up front, so G2 holds
every module, every guard, the screen and the scripts side by side; the long pole is `detect` (T003). The in-group
dependencies run one way only within a group and settle before the gate: `status` and `detect` test each other's code,
the goldens wait for `notes`, the screen waits for the goldens, the guards scan the modules. G3 is two dependent tasks
(T013 runs T012's workflow step through the close lane). No group is a chain of independent tasks.

**SC owners:** SC-001 T013 (+ L-CLOSE) · SC-002, SC-003, SC-004, SC-005 T003 · SC-006 T003 (green with T005) · SC-007,
SC-008 T003 (flags with T005) · SC-009 T005 (with T003) · SC-010 T003 (`os_version_parsers`) + T005
(`running_os_gates`) · SC-011 T004 · SC-012 T005 · SC-013, SC-014, SC-015, SC-016 T003 · SC-017 T003 (snapshot) + T007
(token guard) · SC-018 T005 · SC-019 T011 · SC-020 T011 · SC-021 T002 (tables, plants) · SC-022 T002 (manifests, pins)
+ T007 + T009 + T012 (CI step) + L-CLOSE (`cargo tree` record) · SC-023 T012 + L-CLOSE · SC-024 T007 + T009 (roster
unchanged) · SC-025, SC-026, SC-027 T003 · SC-028 T013 · SC-029, SC-030 T003 (+ T005 `listing_runs_once_per_root`) ·
SC-031 T003 · SC-032 T005 · SC-033 T008 (+ L-CLOSE records) · SC-034, SC-035 T011.

---

## G1 — Foundation [P] (two tasks, disjoint files)

### T001 [P] — Precondition check, the spec amendments and the base records (spec all rows plan §0.2 names; plan §0.2 SI-1 to SI-21, §0.3, §10; TA-1, TA-2, TA-9, TA-13)

**Files:** `specs/krate-manager-phase6/spec.md` (edit, CRLF); new `specs/krate-manager-phase6/evidence/base-binary-size.txt`
(LF); `specs/INDEX.md` (regenerated only, and only if `--check` fails because of this phase's spec files).

**Precondition.** Run the check of "Preconditions" above first; on failure return `blocked`, changing nothing.

**Test first.** Docs: before editing, record in the notes that the amendments are absent (the eight `grep -c` counts
of the header's "Spec state", each `0`); `git rev-parse --short HEAD` (expected `3efe70de`): the **scope base** T010's
scope check diffs against; and `node tools/gen-specs-index.js --check`'s exit code.

**Implement — base records (TA-13).** Before any build can replace it, write into `evidence/base-binary-size.txt` the
output of `ls -l --time-style=long-iso apps/krate-manager/target/release/krate-manager.exe` (expected 4314624 bytes,
2026-10-31 15:22), `git log --format='%h %ci' -1 -- apps/krate-manager` (expected `3efe70de 2026-10-31 15:24:50 +0100`)
and the scope base line `scope base: 3efe70de`. A different size or a missing file is recorded as found, not fixed.

**Implement — spec rows.** A row counts as already applied when the spec text already says what its fix says (note
"already present" and the line). Otherwise edit it in place and append `(plan SI-n)` or `(tasks TA-n)` to the changed
sentence. Plan §0.2's "Proposed fix" column (`plan.md:59-81`) is the binding wording, with these readings and targets:
- SI-1 → FR-082 (`spec.md:725-732`); SI-2 → FR-063 (`:623-626`), FR-065 (`:634-643`) and the Traceability departures
  list (`:1246-1296`); SI-3 → SC-029 (`:1089-1091`, the name `listing_case_exact_unix`) and FR-085 (`:754-762`);
  SI-4 → FR-076 (`:698-699`); SI-5 → FR-001 (`:302`) and FR-006 (`:365-367`); SI-6 → SC-029; SI-7 → New components
  (`:1222-1245`, one row for `apps/krate-manager/tests/fixtures/.gitattributes`); SI-8 → FR-033 (`:491-500`, one
  sentence naming the doubled-backslash source form and PD-4's push-built subkey); SI-9 → SC-011 (`:936-939`); SI-10 →
  SC-019 (`:1014-1016`, "status section"); SI-11 → FR-073 (`:684-688`) and FR-074 (`:689-691`), PD-13's two sentences
  (Q-A as recommended unless ruled otherwise); SI-12 → FR-005 (`:343-349`, the panic text `install location <path> is
  outside the test sandbox <base>`); SI-13 → FR-042 (`:539-542`, the fourth field `managed_rank: Option<u8>`) and FR-044
  (`:552-554`); SI-15 → D15 (`:280-295`), FR-064 (`:627-633`) and FR-065 (the identifier only in `commands.rs`, the
  banned sequences, the extra plant); SI-16 → FR-012 (`:379-382`); SI-17 → the SC rows plan §7.1 rows 004/005, 007, 011,
  013, 019, 027, 029 extend (`plan.md:1114-1139`), each added case written verbatim with its expected result; SI-18 →
  FR-028 (`:437-442`) and SC-007 (`:891-900`); SI-19 → FR-033 (`GETCONF = "/usr/bin/getconf"`); SI-20 → SC-029 (the
  test `detect::tests::listing_skips_junction_windows`) and FR-085's `windows` list; SI-21 → FR-061 (`:617-620`).
- FR-085's three OS lists become exactly plan §4.14's (`plan.md:1005-1010`): `windows` + `native_layout_windows`,
  `locked_moduleinfo_windows`, `host_facts_through_fake_io_windows`, `listing_case_windows`,
  `listing_skips_junction_windows`; `macos` and `linux` each gain `listing_skips_symlinked_dirs_unix` and
  `listing_case_exact_unix` beside their existing names.
- **SI-14 (Q-C):** only when `## Clarifications` records Q-C's answer: for (a), FR-092 (`:800-804`), SC-028
  (`:1075-1079`), D1's parenthesis (`:165-183`), the Q2 clarification bullet (`:1368-1371`, append "(re-ruled Q-C
  2026-10-31: …)" rather than rewriting the ruling record) and the Traceability line `:1314` per SI-14 (a); for (b),
  per SI-14 (b). Without the bullet, change none of these and write "SI-14 deferred to T013" in the notes.
- TA rows marked **spec**: TA-1 (FR-086 `:763-767`, SC-027 `:1068-1074`, SC-029), TA-2 (FR-083 `:733-742`), TA-9
  (FR-088 `:770-777`).

Then `node tools/gen-specs-index.js --check`: when red only because lines naming `krate-manager-phase6/` changed,
`node tools/gen-specs-index.js` once and `--check` again; any other difference is recorded, not fixed.

**Done when:** every row carries its change and tag (or "already present" with its line, or "SI-14 deferred to T013");
`grep -c $'\r$' spec.md` equals `wc -l < spec.md`; the notes list each edited row id, the scope base and the base-size
lines. **Lane:** L-INDEX.

### T002 [P] — Precondition, manifests, lockfile, every shared line, the platform seam, the placeholders, the fakes and the `src-tauri` wiring (spec FR-002, FR-025, FR-030 to FR-033, FR-060 to FR-065 wiring, FR-080, FR-081, FR-083 fakes; SC-021, SC-022 part; plan §2, §4.1 to §4.3, §4.5.1, §4.6 types, §4.7 types, §4.8, §4.9, §4.12 constants, PD-4, PD-5, PD-11, PD-15, PD-16, PD-20, SI-1, SI-2, SI-7, SI-8, SI-15, SI-19, SI-21; TA-3, TA-4, TA-6, TA-7, TA-11, TA-12)

This task makes every edit to a shared app file the phase needs (except the workflow, `required-tests.json` and the
shape test, T012), so G2 touches only its own files.

**Files (edit):** `apps/krate-manager/Cargo.toml`, `crates/core/Cargo.toml`, `src-tauri/Cargo.toml`, `Cargo.lock`,
`crates/core/src/lib.rs`, `crates/core/src/platform/{mod,windows,macos,linux}.rs`, `crates/core/src/test_support.rs`,
`crates/core/src/test_support_tests.rs`, `src-tauri/src/lib.rs`, `src-tauri/src/commands.rs`, `src-tauri/build.rs`,
`src-tauri/capabilities/main.json`, `crates/core/tests/injection.rs` (constants and two existing assertions only),
`crates/core/tests/config_guard.rs` (tables, the `tauri-plugin` rule, the description check, plants).
**Files (new):** placeholders `crates/core/src/{detect,status,notes}.rs` and `crates/core/src/{detect,status,notes}_tests.rs`;
placeholder `src-tauri/src/os_facts.rs`; `apps/krate-manager/tests/fixtures/.gitattributes`.

**Precondition.** Run the check of "Preconditions" above first; on failure return `blocked`, changing nothing. Then
`df -h /f` into the notes (13 GB free this session; below 6 GB, return `blocked` naming the number).

**Test first.**
- **The guards are this task's red.** Edit the guard expectations of step 2 below **before** the manifests and sources,
  then run `cargo test -p krate-manager-core --locked --test injection --test config_guard` (shared target) and quote
  the red lines: `core_dependencies_are_the_fr007_set` (`[dependencies]` keys lack `pulldown-cmark`),
  `reader_dependencies_are_the_ruled_set` (windows-sys features), `real_src_tauri_config_passes_the_guard` (two
  permissions, two commands, five dependency keys, the description), `planted_config_changes_fail_the_guard` (a planted
  sixth permission not reported as such while the real tree differs). They go green when steps 1 and 3-7 land.
- `test_support_tests.rs`: `test_support::tests::fakes_record_calls_and_answer_defaults` (TA-6): a default
  `FakeHostFacts` answers `os_version()` → `None`, `inno_values(k)` → `Absent`, `pkg_outputs(p)` → `Absent`; one with
  `os_version: OsVersion::parse("15.7.9")`, `inno: {"{0A1F0000-0000-4000-8000-000000000001}_is1" →
  Found(InnoValues { display_version: Some("1.2.0"), install_location: None })}` and `pkg: {"com.krateaudio.alpha" →
  Failed("E_RECORD_TIMEOUT")}` answers those, and its `calls` reads exactly `["os_version", "inno_values
  {0A1F0000-0000-4000-8000-000000000001}_is1", "pkg_outputs com.krateaudio.alpha"]` after one call each. The
  `PlainName` arguments come from `PlainName::checked("windows_uninstall_key", &json!(…))` and
  `PlainName::checked("macos_pkg_id", &json!(…))` (`feed.rs:148`, `pub(crate)`; it runs the contract pattern of that
  field, `tools/feed-contract/CONTRACT.md:71-75`, so a key like `"K"` would be refused); never a plugin name. A `FakeHostIo` with `runs: {["a","b"] →
  Output("x")}`, `registry: {"S" → Values([Some("v"), None])}`, `rtl: Some((10, 0, 26200))` answers `run(&["a","b"], 1 s)`
  → `Output("x")`, `run(&["c"], …)` → `NoOutput`, `registry_values("S", ["N1","N2"])` → the values, `registry_values("T",
  …)` → `NoKey`, `rtl_version()` → `Some((10,0,26200))`, and its `calls` reads exactly `["run a b", "run c", "registry S
  N1 N2", "registry T N1 N2", "rtl_version"]`. Red: before the fakes exist it does not compile; quote the error line.

**Implement.**
1. **Manifests** (plan §4.1, TA-7; one line per entry, reasons on a full-line `#` comment above, Phase 5 TA-15).
   Root `[workspace.dependencies]` (`Cargo.toml:13-36`, alphabetical): add `plist = { version = "1.10.3",
   default-features = false }`, `pulldown-cmark = { version = "0.13.6", default-features = false }`,
   `tauri-plugin-opener = "2.7.2"`, each with plan §4.1's comment line (`plan.md:248-253`); `windows-sys` becomes
   `{ version = "0.61.2", features = ["Win32_Foundation", "Win32_System_Registry", "Win32_System_SystemInformation",
   "Wdk_System_SystemServices", "Win32_Networking_WinHttp"] }` (`Win32_Networking_WinHttp` last, PD-16) and its comment
   (`:35`) `# R-2; R-13, R-15; Phase 6 FR-081: exactly five features, no Win32_System_Time`.
   `crates/core/Cargo.toml`: `[dependencies]` gains `pulldown-cmark = { workspace = true }` (after `log`, comment `#
   Phase 6 D12: notes.rs only (FR-082)`); the macOS table (`:35-37`) becomes `core-foundation = { workspace = true }` and
   `plist = { workspace = true }` under the comment `# R-6: sysproxy_cf only; Phase 6: detect.rs AU reader only`.
   `src-tauri/Cargo.toml`: `[dependencies]` (`:24-29`) gains, after `tauri`, the TA-7 comment and `tauri-plugin-opener =
   { workspace = true }`; the Windows table comment (`:34`) becomes `# R-2: os_proxy.rs and os_facts.rs only (Phase 6
   FR-032)`. Then one non-locked `cargo check --workspace` (downloads from crates.io: `pulldown-cmark` and
   `tauri-plugin-opener` are not in the local registry, `ls ~/.cargo/registry/src/*/` this session; `plist-1.10.3` is,
   and `plist 1.10.3` is already in `Cargo.lock`). **Additive check:** a throwaway Node script under
   `F:/tmp/gate/km6-T002/` (Write tool) compares the `[[package]]` (name, version) pairs of `git show
   HEAD:apps/krate-manager/Cargo.lock` with the new file: every old pair must remain; list the added pairs in the notes.
   A missing old pair is restored with `cargo update -p <name> --precise <old>` before anything else runs. Then `cargo
   build --workspace --locked`; record its wall clock (cold for the new crates).
2. **The guard expectations the manifests and sources move** (written first, see Test first; nothing else in either
   file changes here):
   - `injection.rs`: `FR007_DEPENDENCIES` (`:87-98`) → `[&str; 11]` with `"pulldown-cmark"`;
     `core_dependencies_are_the_fr007_set` (`:2218-2264`): the macOS target table expected as `[("core-foundation",
     "{workspace=true}"), ("plist", "{workspace=true}")]`, and two root-pin assertions beside the `rquickjs` one
     (`toml_entry(&workspace, WORKSPACE_DEPS, "pulldown-cmark")` and `…"plist"` each squashed equal to
     `{version="<v>",default-features=false}`, PD-15); `READER_PINS[1].features` (`:283-289`) → the five FR-081 names in
     step 1's order; `CONFINED_TOKENS` (`:124-149`): add `const OS_FACTS_RS: &str = "src-tauri/src/os_facts.rs";` beside
     `OS_PROXY_RS` (`:119`) and the `windows_sys` row becomes `("windows_sys", &[OS_PROXY_RS, OS_FACTS_RS])` (SI-1);
     `CONFINED_TOML_KEYS` (`:153-164`) → `[_; 9]` with `("pulldown-cmark", &["Cargo.toml", "crates/core/Cargo.toml"])`,
     `("plist", &["Cargo.toml", "crates/core/Cargo.toml"])`, `("tauri-plugin-opener", &["Cargo.toml",
     "src-tauri/Cargo.toml"])` (PD-15); `PLATFORM_LITERALS` (`:43-71`) → `[&str; 37]` with the ten FR-033 literals, the
     `UNINSTALL_KEY` entry being its **source text** `"SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall"`
     (PD-4, SI-8) and the Linux one `"/usr/bin/getconf"` (SI-19). The `reader_dependencies_are_the_ruled_set` plant
     anchor `"\"Win32_Networking_WinHttp\"]"` (`:2705-2710`) still matches once (PD-16).
   - `config_guard.rs`: `SRC_TAURI_DEPENDENCIES` (`:32-33`) → `[&str; 6]` sorted, `"tauri-plugin-opener"` last; the
     `tauri-plugin` loop (`:688-695`) accepts exactly one line, `tauri-plugin-opener = { workspace = true }` inside
     `[dependencies]`, and still reports every other line holding `tauri-plugin` (any other crate, that crate in another
     table, or a comment) (SI-2); the capability `permissions` expectation (`:527-532`) → the five FR-061 names; a new
     exact check that `description` equals `The main window may call only the app's own commands.` (SI-21); the
     `build.rs` check (`:547-561`) → `commands(&["app_version","startup_error","plugin_status","check_now",
     "open_release_page"])` with its message naming the five; `LIB_RS_STRINGS` (`:35-50`) unchanged (PD-20). In
     `planted_config_changes_fail_the_guard` (`:1178`): the plant "tauri-plugin-opener in [dependencies]"
     (`:1310-1320`) becomes `tauri-plugin-fs = "2"` in `[dependencies]` (expected `[dependencies] keys` and `names a
     tauri-plugin crate`); the `[build-dependencies]` plant stays; new plants, each reported: a sixth permission
     `"allow-planted"`, `"allow-fs-read"`, `"opener:allow-open-url"` (FR-063), and a description
     `The main window may call only the app's own read-only commands.` (SI-21).
3. **`crates/core/src/lib.rs`** (`:1-21`): `pub mod detect;`, `pub mod notes;`, `pub mod status;` in alphabetical
   place; nothing else.
4. **`platform/mod.rs`** exactly plan §4.2 (`plan.md:274-317`): `VENDOR_DIR`, `OS_VERSION_WAIT`, `RunOutcome`,
   `RegistryRead`, `trait HostIo`, the three new `trait Platform` methods, and the three `pub use` lines replacing
   `:60-71`'s (each module stays private). **`platform/windows.rs`, `macos.rs`, `linux.rs`** exactly plan §4.3
   (`plan.md:323-361`): the FR-033 constants (each `pub`, PD-5's `PKGUTIL_WAIT` in `macos.rs`), the three methods per
   OS, the subkey built with `push_str` / `push('\\')` (PD-4), and no `FS_TOKENS` identifier, `Command`, `read_dir` or
   other FR-084 token. The methods call T003's `detect` mappers, which are placeholders until G2.
5. **Placeholders** (Rule 9, TA-4, TA-11). Each `<stem>.rs` starts with a `//!` line naming its plan section and ends
   with the Rule 3 test declaration; each `<stem>_tests.rs` is one `//!` line. Bodies are `todo!()`.
   - `detect.rs`: everything of plan §4.5.1 (`plan.md:384-442`) **except** `LOG_TARGET` and the `E_…` codes (T003 adds
     them with their users), with `HostFacts`, `PlatformFacts` and its `impl HostFacts` implemented (three one-line
     delegations to `NativePlatform.<method>(self.io, …)`); `ListingLimits` and `PRODUCTION`; `Listing` per TA-4 with
     `scan` and `copies_of` as `todo!()`; the signatures of plan §4.5.2 to §4.5.7 (`candidates`, `same_path`,
     `usable_install_location`, `probe`, `strip_trailing_commas`, `inno_record`, `parse_pkg_info`, `count_pkg_files`,
     `pkg_record`, `read_manifest`, `inno_read_from`, `pkg_read_from`, `os_version_from_run`, `parse_product_version`,
     `parse_glibc_version`, `windows_version`, `detect_plugin`) as `pub fn … { todo!() }`. No private item.
   - `status.rs`: `RELEASE_PAGE_HOST`, `MAX_RELEASE_PAGE_URL_BYTES`, every view type of plan §4.6 (`plan.md:601-624`,
     field order = FR-046 key order, `#[serde(rename_all = "snake_case")]` on every enum and no `rename`, the view's
     `UnknownReasonView::NoModuleinfo` spelling of PD-21), and `build`, `assemble`, `release_page_url` as `todo!()`.
   - `notes.rs`: `MAX_NOTES_BYTES`, `MAX_DEPTH`, `TOO_LONG`, `Block`, `Inline` exactly plan §4.7 (`plan.md:658-676`), and
     `blocks` as `todo!()`.
6. **`test_support.rs`**: `FakeHostFacts` and `FakeHostIo` exactly plan §4.8 (`plan.md:692-713`), implemented in full
   (record the call, then answer: unlisted key → `Absent`, unlisted argv → `NoOutput`, unlisted subkey → `NoKey`); the
   call strings are `os_version`, `inno_values <key>`, `pkg_outputs <id>`, `run <argv joined by ' '>`, `registry <subkey>
   <n1> <n2>`, `rtl_version`. No `static`, no `Mutex` (the FR-086 rule, `injection.rs:1740-1758`).
7. **`src-tauri`** (TA-12): `os_facts.rs` placeholder: `//!` line, `pub struct OsHostIo;` and `impl HostIo for OsHostIo`
   whose three methods are `todo!()` (no `cfg`, no `windows_sys` yet: T006 writes the file whole); `commands.rs` exactly
   plan §4.9 (`plan.md:767-792`: `plugin_status`, `check_now`, the private `status_for`, `open_release_page` with the
   free fn `tauri_plugin_opener::open_url`, no `OpenerExt`, no `AppHandle`) beside the two existing commands, its `//!`
   line becoming "The app's commands (FR-040, FR-042; Phase 6 FR-060, FR-064)"; `lib.rs`: `mod os_facts;` in
   alphabetical place, `generate_handler!` with the five commands of plan §4.9 (`plan.md:794-797`), **no** `.plugin(
   tauri_plugin_opener::init())`; `build.rs`: the five-command `commands(&[…])`; `capabilities/main.json`: the five
   permissions and the SI-21 description, key order unchanged.
8. **`apps/krate-manager/tests/fixtures/.gitattributes`** (PD-11, SI-7): exactly two lines, `detect/** -text` and
   `status/** -text`, LF.

**Done when:** the four guard tests above and `fakes_record_calls_and_answer_defaults` are green; L-RS green with every
existing test still green (no placeholder is reached by any test: the platform tests call `default_roots` only); L-APP,
L-DEVFEED, L-GUARD, L-TS (frontend unchanged), L-VLINT green; `git diff --stat Cargo.lock` and the additive-check
output in the notes; `cargo tree -p krate-manager --locked -e normal --prefix none | grep -c "^tauri-plugin-opener v2.7.2"`
= 1 and the same over `-p krate-manager-core` = 0. **Lane:** L-RS (shared target, alone), L-APP, L-DEVFEED, L-GUARD,
L-TS, L-VLINT.

---

## G2 — Modules, guards, screen and scripts [P] (nine tasks, disjoint files, Rule 10 slots)

### T003 [P] — `core::detect`, `roots.rs::check_sandboxed` and the detect fixtures (spec FR-001 to FR-006, FR-010 to FR-014, FR-020, FR-021, FR-025 to FR-036, FR-042, FR-047 detect lines; SC-002 to SC-008, SC-010 parsers, SC-013 to SC-017, SC-025 to SC-027, SC-029 to SC-031; plan §3, §4.4, §4.5, §4.11 detect fixtures, §7.1 rows 002-008, 010, 013-017, 025-027, 029-031, PD-1 to PD-7, PD-17, PD-19, PD-22, SI-5, SI-6, SI-12, SI-13, SI-16 to SI-20; TA-1, TA-3)

**Files:** `crates/core/src/detect.rs` (replace T002's placeholder bodies; the public types and signatures stay as T002
wrote them, plus `managed_rank` already in `Detection::Checked`), `crates/core/src/detect_tests.rs`,
`crates/core/src/roots.rs` (TA-3: `check_sandboxed` only), new `apps/krate-manager/tests/fixtures/detect/**` (LF).
**Slot:** `detect`, `CARGO_BUILD_JOBS=10`. Filter: `--lib detect::`.

**Test first** (`detect_tests.rs`; every assertion is the spec SC text as amended by T001, plus the plan §7.1 row named;
every tree under `fresh_temp_roots()`; every path built with `Path::join` from the sandbox base, never a literal):
- `candidate_table` (SC-002, row 002): synthetic root lists, no filesystem access; Alpha / Bravo from `full-v2.json`,
  Delta from `tenth-plugin.json` through `feed::parse` (`feed.rs:275`); Windows cases (a)-(c); every cell (path, place,
  managed, rank, order).
- `native_layout_windows` / `native_layout_macos` / `native_layout_linux` (SC-003, row 003), each `#[cfg(target_os =
  …)]`; Linux configured root through `context::reassemble_with_os(Os::Linux, &prev, Settings { linux_plugin_root:
  Some(<base>/custom), .. })` (`context.rs:142`, `pub(crate)`).
- `trailing_comma_cases` (SC-004 as amended by SI-16, row 004): includes `[,]`, `{,}`, `[1,,]` → `Corrupt`; the
  "Iterum-shaped" fixture of SC-004 / plan §4.11 is committed as `moduleinfo/sdk-shape.json` (D-4, Rule 4e: no shipped
  plugin name in a file name or content under `apps/krate-manager/`), same shape: top `1.0.0`, classes `1.0.0.1`,
  trailing commas where the SDK writer puts them (`plan.md:859-863`), names `Alpha`.
- `installed_state_table` (SC-005 table + row 005's eight added rows): per row its own `fresh_temp_roots()`; the 1 MiB
  + 1 byte file and the row 005 test-time inputs written in the test (`plan.md:883-888`); levels, codes and the
  `KM-CONTENT-MARKER` 0-hit rule asserted through `test_support::install_capture()` / `take_mine()`
  (`test_support.rs:171-190`).
- `duplicates_in_two_roots` (SC-006, row 006): `detect_plugin(Os::X, …)` with a `FakeHostFacts`, then
  `status::assemble(Os::X, …)`, exact `PluginView` equality per OS row.
- `record_parsers` (SC-007 as amended by SI-18, row 007): includes the 4 MiB + 1 byte `manifests/Alpha.json` and the
  directory named `manifests/Alpha.json` → `Failed(E_RECORD_READ)`; `E_RECORD_LOCATION` and the marker via the
  capture logger.
- `records_through_fake_host` (SC-008, row 008) and `outside_install_location_panics` with
  `#[should_panic(expected = "outside the test sandbox")]` (FR-005 as amended by SI-12).
- `os_version_parsers` (SC-010 first half).
- `au_plist_cases_macos` (SC-013 + row 013's 1 MiB + 1 byte `Info.plist`), `#[cfg(target_os = "macos")]`; the binary
  plist written at test time with `plist::Value::to_writer_binary`.
- `pkg_receipt_macos` (SC-014, row 014), `#[cfg(target_os = "macos")]`: `PlatformFacts { io: &fake }` with argvs built
  from `platform::PKGUTIL`, `PKG_INFO_FLAG`, `PKG_FILES_FLAG` (FR-083: never a re-spelled literal).
- `linux_detection_linux` (SC-015, row 015), `#[cfg(target_os = "linux")]`.
- `locked_moduleinfo_windows` (SC-016, row 016), `#[cfg(windows)]`: `std::os::windows::fs::OpenOptionsExt::share_mode(0)`
  held across `detect_plugin`.
- `detection_leaves_the_tree_unchanged` (SC-017 snapshot half, row 017): a recursive snapshot (relative path, kind, size,
  mtime) before / after `status::build` with a `FakeHostFacts` and a `FixtureServer` feed; only `state/feed-cache.json`
  and its parent differ.
- `host_facts_through_fake_io_windows` / `_macos` / `_linux` (SC-025, row 025), each gated on its OS: `NativePlatform`
  with a `FakeHostIo`; expected argv / subkey from the re-exported FR-033 constants; the Windows subkey expected as
  `format!("{}\\{}", platform::UNINSTALL_KEY, key)` (a test may hold that literal; `platform/` may not).
- `host_read_mappers` (SC-026, row 026): every arm, a counting `files` closure.
- `probe_metadata_error_unix` (SC-027 as amended by SI-17 and TA-1, row 027), `#[cfg(unix)]`: the `ENOTDIR` case, and
  the FIFO at `Alpha.vst3/Contents/Resources/moduleinfo.json` made with `std::process::Command::new("mkfifo")` (bare
  name, TA-1) inside the temp tree, asserting its exit status first; `probe` on a spawned thread, result over `mpsc`,
  `recv_timeout(Duration::from_secs(5))` → `Unknown(Unreadable)` and one `E_DETECT_MODULEINFO`.
- `nested_copies_listed` (SC-029, row 029), `listing_case_windows` (`#[cfg(windows)]`) and `listing_case_exact_unix`
  (`#[cfg(unix)]`) on `Listing::scan` at `vst3-user/Old/alpha.vst3` through `paths()` and `copies_of("Alpha.vst3")`
  (PD-19), `listing_skips_symlinked_dirs_unix` (`#[cfg(unix)]`; with row 029's `0o000` directory, its mode restored
  before the `TempDir` drops, and the pre-assertion that `read_dir` of it fails), `listing_skips_junction_windows`
  (`#[cfg(windows)]`, SI-20, TA-1: `std::process::Command::new("cmd")` with args `["/C", "mklink", "/J", <link>,
  <target>]`, both paths inside the `TempDir`, exit status asserted first; the loop `vst3-user/a/loop` → `vst3-user/a`
  and `vst3-user/out` → `<base>/outside` holding `Alpha.vst3`; the listing terminates, reports no copy through either
  junction, and the captured `debug!` `listing:` line's count is finite).
- `listing_caps_honoured` and `listing_production_limits` (SC-030 first two, row 030).
- `installed_elsewhere` (SC-031, row 031): through `status::assemble` for the three OSes over synthetic `Detection`s.

Red: each test fails on T002's `todo!()` ("not yet implemented") or does not compile against a missing fixture; quote
one line per test. The macOS-gated tests are "not compiled here" (Rule 15).

**Expected red until** (in-group; only these texts count): `duplicates_in_two_roots`, `records_through_fake_host`,
`nested_copies_listed`, `installed_elsewhere`, `detection_leaves_the_tree_unchanged` panic `not yet implemented` inside
`status.rs` until **T005** lands `assemble` / `build`; `production_config_only_in_allowed_tests` (`injection.rs:2535`)
reports `Command::new("mkfifo")` and `Command::new("cmd")` in `detect_tests.rs` until **T007** lands TA-1;
`detect_status_notes_hold_no_write_or_listing_token` is T007's and checks this file.

**Implement:** plan §4.5.2 to §4.5.7 (`plan.md:444-592`) and §3's codes and log shapes (`plan.md:214-237`): `LOG_TARGET`
and the `E_…` codes as `pub(crate) const`; `same_path` and `candidates` with PD-1's dedupe and PD-2's `root_index`;
`usable_install_location`; the listing with PD-3's case rule, PD-17's sorted read, the one `read_dir` of the phase inside
`fn list_root`, the depth / entry caps and the two log lines; `probe` with PD-7 and `read_bounded` with PD-22 (metadata
and `is_file` before `File::open`, `take(max + 1)`, no `OpenOptions`); `strip_trailing_commas` (both rules of SI-16);
`moduleinfo_version` (FR-013 (a)-(d); `serde_json::Error` logged by `line()` / `column()` only); `read_au_plist`
(`#[cfg(target_os = "macos")]`, the only `plist::` site); the record parsers, mappers and OS-version parsers;
`detect_plugin` with `check_sandboxed` before any candidate (FR-005), PD-6's `E_RECORD_LOCATION`, and `managed_rank`
(SI-13). `roots.rs`: `check_sandboxed` exactly plan §4.4 (`plan.md:365-378`). No `"Krate Audio"` literal (use
`platform::VENDOR_DIR`); none of FR-084's tokens; no `"/usr/` or `"/Library` substring, comments included (Rule 4a).

**Fixtures** (plan §4.11, `plan.md:858-871`, LF, each JSON one holding `"KMContent": "KM-CONTENT-MARKER"`): `moduleinfo/`
`current.json`, `sdk-shape.json` (see above), `legacy-080.json`, `top-103-class-104.json`, `real-080.json`,
`classes-disagree.json`, `class-x.json`, `no-class-top-123.json`, `no-version.json`, `truncated.json`; `plist/`
`template.plist`, `short-120.plist`, `short-12.plist`, `short-1234.plist`, `corrupt.plist`; `pkgutil/`
`alpha-pkg-info.txt`, `alpha-files.txt` (3 lines); `manifest/` `alpha-v1.json`, `alpha-v2.json`, `other-plugin.json`,
`wrong-types.json`. Read through `Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/detect")`. No fixture
names a shipped plugin, in its name or its content (moduleinfo `Name` / class `Name` values are `Alpha` …; Rule 4e).

**Done when:** L-OWN `detect::` green except the expected reds above (each quoted); clippy in the slot 0 warnings;
optionally L-WSL-OWN (record pass, red or timeout); `git status --porcelain` shows only this task's files. **Lane:**
L-OWN (slot `detect`).

### T004 [P] — `core::notes` (spec FR-050 to FR-054; SC-011; plan §4.7, §7.1 row 011, PD-8, PD-9, SI-9, SI-17)

**Files:** `crates/core/src/notes.rs`, `crates/core/src/notes_tests.rs`. **Slot:** `model`, `CARGO_BUILD_JOBS=8`
(shared with T005). Filter: `--lib notes::`.

**Test first:** `notes::tests::blocks_cases`, every case of SC-011 as amended by SI-9 and SI-17 (`spec.md:932-942`;
`plan.md:1121`): exact model equality per case; `serde_json::to_string` of every case's model contains no `http`; the
9-deep list, quote and emphasis cases flattened per PD-8; `<div>\nx\n</div>`, `a\n\n---\n\nb`, `a\nb`, `1. x`; the
65,537-byte input (built in the test) → `[Paragraph [Text TOO_LONG]]`; `notes-14000.json`'s Alpha `notes_markdown`
(read in place, `tools/feed-contract/fixtures/feeds/valid/notes-14000.json`, through `feed::parse`) non-empty; empty
and whitespace-only → `[]`. The Markdown inputs hold no URL other than SC-011's two `https://e.example` destinations,
which the assertion proves are dropped. Red: `not yet implemented`.

**Implement:** plan §4.7 (`plan.md:679-688`): `Parser::new_ext(markdown, Options::empty())`; the block-frame and
inline-frame stacks; PD-8's one flattening rule at depth 9 (block containers collected as one `Paragraph [Text t]`
appended to the innermost allowed container; inline containers appended as `Text`, merged); PD-9's `HtmlBlock` text
(final line break removed) and verbatim code; `Link` / `Image` children only; `SoftBreak` → `Text " "`, `HardBreak` →
`Break`; adjacent `Text` merged when an inline vector closes; `List(Some(1))` → `start: None`; the 65,536-byte bound
before parsing and the empty rule. `pulldown_cmark` appears only in this file and its tests (FR-082).

**Expected red until:** none. **Done when:** L-OWN `notes::` green and clippy 0 warnings in the slot. **Lane:** L-OWN.

### T005 [P] — `core::status` and the 13 golden views (spec FR-037, FR-040 to FR-049; SC-009, SC-010 second half, SC-012, SC-018, SC-030 third, SC-032; plan §4.6, §4.11 status fixtures, §7.1 rows 009, 010, 012, 018, 030, 032, PD-10, PD-21, SI-13; TA-5)

**Files:** `crates/core/src/status.rs`, `crates/core/src/status_tests.rs`, new
`apps/krate-manager/tests/fixtures/status/<scenario>.json` (13, LF). **Slot:** `model`, `CARGO_BUILD_JOBS=8`. Filter:
`--lib status::`.

**Test first** (`status_tests.rs`):
- `feed_driven_detection` (SC-009, row 009): `status::build` against a `FixtureServer` serving `tenth-plugin.json`,
  `v1-only.json` and `mixed.json` in turn (`set_route`), each with a fresh `FakeHostFacts` and a `fresh_temp_roots()`
  context (built as `feed_tests.rs:236-255` builds one: `HttpConfig::loopback(1 s, 1 s)`, `Settings::default()`) holding
  `Juliet.vst3` (top `0.7.0`, class `0.7.0.1`) in the managed location; availability asserted for `NativePlatform.os()`;
  for `v1-only.json` `calls == ["os_version"]` exactly.
- `running_os_gates` (SC-010 second half).
- `golden_views` (SC-012, row 012, PD-10, PD-21): `assemble` on synthetic `FeedCheck`s (fixtures through `feed::parse`;
  `FeedError::Http(HttpError::Offline { … })`, `FeedError::FutureSchema { found: 3 }`) and synthetic `Detection`s whose
  paths are `PathBuf::from("/fixture/…")` literals (allowed: FR-088's `/usr/` and `/Library` regexes do not match
  `/fixture/`); `notes-rendering`'s Markdown set in the test exactly as `plan.md:876-882`; byte equality of
  `serde_json::to_string_pretty(&view)` plus `"\n"` with each file; **every** mismatch or missing file collected and reported
  in one panic that prints each scenario's full expected text (TA-5); the OS-consistency rule over every file; the
  directory holds exactly the 13 names; every variant of every view enum serialised and compared with FR-046's strings
  written as literals (`"no_moduleinfo"` among them).
- `build_cases` and `concurrent_builds` (SC-018, row 018): `Reply::Close` via `set_route`; recorded request headers
  (`Cache-Control: no-cache` for `CheckNow`); the one `info!` line per build through the capture logger; a
  `std::sync::Barrier` local to the test, one `FakeHostFacts` per thread.
- `listing_runs_once_per_root` (SC-030 third, row 030): one `debug!` `listing:` line per listed root over
  `tenth-plugin.json`.
- `release_page_url_cases` (SC-032, row 032): the 2,049-byte URL built in the test; the `api.` host via
  `format!("api.{}", status::RELEASE_PAGE_HOST)`; the `https://` literals here are test data (the frontend scan covers
  `src/`, not core tests).
Red: `not yet implemented`, and `golden_views` reports 13 missing files.

**Expected red until** (in-group): `feed_driven_detection`, `build_cases`, `concurrent_builds`,
`listing_runs_once_per_root` panic `not yet implemented` inside `detect.rs` until **T003** lands; `golden_views` stays red
on its own goldens until this task writes them (below), which it does only after **T004**'s `blocks_cases` is green.

**Implement:** plan §4.6 (`plan.md:636-653`): `build` (one `feed::check`, one `host.os_version()`, one `Listing::scan`
over the de-duplicated `vst3_bundle` names, `detect_plugin` per entry in feed order on the calling thread, `assemble`,
one `info!`; no lock, no write, no thread); `assemble` per FR-043 to FR-046 with `managed_rank` (SI-13) for
`loads_before_managed`; `release_page_url` per FR-049 and plan `plan.md:651-653`; the view enums mapped from the core
enums by exhaustive `match`es (PD-21). No FR-084 token; `status.rs` holds no `rename` identifier (FR-084 as specified,
`spec.md:743-753`).

**Goldens (PD-10, TA-5).** When `notes::tests::blocks_cases` is green in slot `model` (poll every 60 s, ceiling 45 min;
past the ceiling return `blocked` naming T004's state), run `golden_views`, review each printed expected text against
its scenario's description (`spec.md:943-964`) and write the 13 files from it, LF, no BOM: `fresh-all-states`,
`cached-stale`, `no-feed`, `future-schema`, `macos-intel-not-for-this-mac`, `not-for-this-computer-linux`,
`needs-newer-os-macos`, `needs-newer-os-linux`, `needs-newer-os-windows`, `macos-au-copies`, `record-notices`,
`notes-rendering`, `installed-elsewhere`. Invented names only (Rule 4e); no `https://` other than the
`release_page_url` / feed values the fixtures already carry. No test writes the files (FR-086).

**Done when:** L-OWN `status::` green (after T003 lands; poll as in Rule 10); the 13 files exist and `golden_views` is
green; clippy 0 warnings in the slot. **Lane:** L-OWN (slot `model`).

### T006 [P] — `src-tauri/src/os_facts.rs`, the raw host primitives (spec FR-032, FR-036; plan §4.9 `os_facts.rs`, §1.2 windows-sys rows, §6 "The host readers")

**Files:** `src-tauri/src/os_facts.rs` (replace T002's placeholder whole). **Target:** shared `apps/krate-manager/target`,
`CARGO_BUILD_JOBS=6` (the only G2 task using it).

**Test first.** None exists by design: `src-tauri` has `test = false` (`src-tauri/Cargo.toml:13,19`) and FR-083 bans
naming `OsHostIo` in any test. The checks are T007's confinement guard (`host_io_symbols_are_confined`: the four FFI
names only in this file) and the compile. Red-first: with `#[cfg(windows)] mod windows;` declared and empty, L-APP fails
on the unresolved `windows::registry_values`; quote it.

**Implement:** plan §4.9 (`plan.md:717-763`) exactly: `run` over `sysproxy::run_bounded(vec![command], bound)` with the
three-way map and no other logic; `#[cfg(windows)] mod windows` with `wide`, the `RegOpenKeyExW(HKEY_LOCAL_MACHINE, …,
0, KEY_READ | KEY_WOW64_64KEY, &mut hkey)` open (`ERROR_FILE_NOT_FOUND` → `NoKey`, other non-success → `Failed`), the
size query and read per name with `RRF_RT_REG_SZ` (`ERROR_FILE_NOT_FOUND` → `None`; any other error, `ERROR_MORE_DATA`
included → `Failed`), a `Drop` guard that calls `RegCloseKey` on every path after a successful open, `String::from_utf16_lossy`
up to the first NUL; `rtl_version` through `RtlGetVersion` with `dwOSVersionInfoSize` set; the `cfg(not(windows))`
arms `Failed` / `None`. Every `unsafe` block carries a `// SAFETY:` line (Rule 15). No `RegSet*`, `RegCreate*`,
`RegDelete*`; no `match` on a `RunOutcome` value outside the `run` map; no version parsing; no path building; no
environment read; no `"HOME"` literal (`injection.rs:246`).

**Expected red until:** none. **Done when:** L-APP and L-DEVFEED green (0 warnings); the notes state that the
`cfg(not(windows))` arms are first compiled by `check (ubuntu-24.04)` / `check (macos-latest)` (plan §7.3, §8). **Lane:**
L-APP, L-DEVFEED.

### T007 [P] — `injection.rs`: confinement, test-source, token, real-location guards and TA-1 (spec FR-002, FR-082 to FR-084, FR-086 as amended, FR-088; SC-017 guard half, SC-022 part, SC-024; plan §4.12 `injection.rs`, §7.2 FR-002; TA-1, TA-2, TA-9)

**Files:** `crates/core/tests/injection.rs` (the only G2 task editing it). **Slot:** `guard`, `CARGO_BUILD_JOBS=6`.
Filter: `--test injection`.

**Test first** (each new test reuses `workspace_rs_files` `:1192`, `lex` `:589`, `confinement` `:625`, `plant` `:1925`,
`replace_one` `:1936`, `assert_reported` `:1973`, `outer_attributes` `:890`; plants are in-memory copies, nothing is
written):
- `notes_and_plist_crates_are_confined`: `pulldown_cmark` only in `notes.rs` / `notes_tests.rs`; `plist ::` only in
  `detect_tests.rs` and, in `detect.rs`, only inside `fn read_au_plist` whose outer attributes contain `target_os =
  "macos"`.
- `host_io_symbols_are_confined`: `RegOpenKeyExW`, `RegGetValueW`, `RegCloseKey`, `RtlGetVersion` only in `os_facts.rs`;
  the literals `/usr/sbin/pkgutil`, `/usr/bin/sw_vers`, `/usr/bin/getconf`, `GNU_LIBC_VERSION` and the `UNINSTALL_KEY`
  source text only in their `platform/<os>.rs` (literal scan via `strip_comments_and_literals` `:352`; `injection.rs`
  exempt); `OsHostIo` only in `os_facts.rs` and `commands.rs`; and (plan §7.2 FR-002) no non-`platform/` core source
  holds the literal `Krate Audio`.
- `confinement_scanner_reports_plants`: one plant per rule of the two tests above (`use pulldown_cmark::Parser;` in
  `status.rs`; `fn f() { let _ = plist::Value::Boolean(true); }` in `detect.rs` outside the AU fn; `RtlGetVersion` in
  `commands.rs`; `"/usr/sbin/pkgutil"` in `linux.rs`; `OsHostIo` in `lib.rs`; `"Krate Audio"` in `detect.rs`), each
  reported.
- `test_sources_never_name_os_host_io` / `test_source_scanner_reports_planted_os_host_io` (FR-083, TA-2): over the
  `is_test_source` files plus `crates/core/src/test_support.rs`, minus `injection.rs`: no `OsHostIo` token and none of
  the FR-082 platform literals; plants: `OsHostIo` in a test body of `detect_tests.rs` and `"/usr/sbin/pkgutil"` in
  `test_support.rs`, each reported.
- `detect_status_notes_hold_no_write_or_listing_token` / `token_scanner_reports_planted_read_dir` (FR-084): the token
  list over `detect.rs`, `notes.rs`, `status.rs` and the non-test `platform/*.rs`; `read_dir` exactly once, in
  `detect.rs` inside `fn list_root`; the path tokens `fs :: write`, `fs :: copy`, `File :: create`; the identifier
  `rename` in `status.rs`; FR-084's four plants.
- `test_sources_hold_no_real_location` / `real_location_scanner_reports_plants` (FR-088, TA-9): the eleven regexes
  (`regex` crate) over exactly `crates/core/src/detect_tests.rs`, `status_tests.rs`, `notes_tests.rs`,
  `test_support.rs`, `src/status.test.ts`, `src/main.test.ts` (paths from the app root); a file that cannot be read is
  the finding `<file>: missing`; one plant per regex reported.
- TA-1, in the existing `production_config_only_in_allowed_tests` (`:2535`) and `test_source_findings` (`:1611`):
  `TEST_PROGRAM_BODIES` (with `const DETECT_TESTS_RS: &str = "crates/core/src/detect_tests.rs";` and, for the plant,
  `STATUS_TESTS_RS`), the Command rule extended as TA-1 says, and the two plants of TA-1, each reported.
Red: on the real tree before the modules land, the token guard reports `read_dir` 0 time(s) in `detect.rs`; the
plants are reported only once the scanners exist (quote the first compile error or assertion line).

**Expected red until** (in-group): `detect_status_notes_hold_no_write_or_listing_token` reports "`read_dir` 0 time(s)
in detect.rs" until **T003** lands; `test_sources_hold_no_real_location` reports `src/status.test.ts: missing` until
**T011** writes it. Any finding on a file a G2 task has finished is a real red to report to that task's owner in the
notes (do not edit their file).

**Done when:** L-OWN `--test injection` green except the expected reds above; clippy 0 warnings in the slot. **Lane:**
L-OWN (slot `guard`).

### T008 [P] — `config_guard.rs`: the opener guard and the stylesheet rule (spec FR-065 as amended by SI-15, FR-077; SC-033, SC-021 part; plan §4.12 `config_guard.rs` bullets 3-4, §7.2 FR-077, PD-20)

**Files:** `crates/core/tests/config_guard.rs` (the only G2 task editing it; T002's G1 edits stay). **Slot:** `guard`,
`CARGO_BUILD_JOBS=6`. Filter: `--test config_guard`.

**Test first:**
- `opener_is_confined_and_not_granted_to_the_webview` and `opener_guard_reports_plants` exactly plan §4.12
  (`plan.md:938-953`): the identifier `tauri_plugin_opener` only in `src-tauri/src/commands.rs` (`holds_token`
  `:711-721`); no `src-tauri/src` file holds `tauri_plugin_opener :: init`, `tauri_plugin_opener :: Builder` or
  `OpenerExt`; no capability permission starting `opener:`; no `tauri-plugin-opener` key in `crates/core/Cargo.toml` or
  `crates/helper/Cargo.toml`; no `@tauri-apps/plugin-opener` in `package.json` `dependencies` / `devDependencies`; no
  `plugins.opener` key in `tauri.conf.json`; the import rule over the non-`*.test.ts` files of `src/`. Plants: the
  permission `"opener:allow-open-url"`, `tauri_plugin_opener` in `status.rs` and in `lib.rs`,
  `.plugin(tauri_plugin_opener::init())` in `commands.rs`, `use tauri_plugin_opener::OpenerExt;` in `commands.rs`, the
  import line in a planted `src/planted.ts` (each reported), and the same import in `src/planted.test.ts` (not
  reported).
- `check_frontend` (`:860`) gains the FR-077 CSS rule (`plan.md:932-937`): in every `src/` file ending `.css`, a line
  containing (ASCII case-insensitive) `url(`, `@font-face`, `@import` or `image-set(` is reported as `<file>:<line>: a
  font, image or URL in a stylesheet (FR-077)`; `frontend_source_scan_reports_planted_strings` (`:1491`) gains the four
  plants in a `src/planted.css` (`src: url(x.woff2)`, `@font-face {`, `@import "x.css";`, `background: image-set("a.png"
  1x)`), each reported.
Red: the new tests fail to compile until their scanners exist; then the plants assertions (quote).

**Expected red until** (in-group): `real_src_tauri_config_passes_the_guard` runs `check_frontend` over the real
`src/styles.css`, which **T011** edits; a finding there is T011's to fix (record it in the notes, do not edit
`styles.css`).

**Done when:** L-OWN `--test config_guard` green; clippy 0 warnings in the slot. **Lane:** L-OWN (slot `guard`).

### T009 [P] — `roster.rs`: install identifiers stay out of the sources (spec FR-089; SC-022 part, SC-024 roster; plan §4.12 `roster.rs`)

**Files:** `crates/core/tests/roster.rs` (the only task editing it). **Slot:** `guard`, `CARGO_BUILD_JOBS=6`. Filter:
`--test roster`.

**Test first:** `install_identifiers_absent_from_sources` and `install_identifier_scanner_reports_plants` exactly spec
FR-089 (`spec.md:778-786`) and plan §4.12 (`plan.md:955-957`): the scanned non-test sources `crates/core/src/detect.rs`,
`status.rs`, `notes.rs`, `platform/*.rs` (non-`_tests`), `src-tauri/src/os_facts.rs`, `commands.rs`, and `src/*.ts`
minus `*.test.ts`; the `regex` patterns `com\.krateaudio\.` (the `APP_IDENTIFIER` line of `platform/mod.rs:9` exempt),
`\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-`, `_is1`; the three plants in an in-memory `detect.rs` reported and the real
`APP_IDENTIFIER` line not. The Phase 5 tests `roster_and_hosts_absent_from_sources` / `roster_scanner_reports_planted_names`
(`:128`, `:175`) are unchanged and stay green. Red: compile error until the scanner exists (quote).

**Expected red until:** none (a hit in a G2 task's file is reported to its owner in the notes).
**Done when:** L-OWN `--test roster` green; clippy 0 warnings in the slot. **Lane:** L-OWN (slot `guard`).

### T010 [P] — Evidence scripts: `scope-check.js`, `wsl-lane.js`, `close-lane.js` (spec SC-001, SC-022 tree record, SC-023 local, SC-028 record, SC-033 records, FR-090, FR-091; plan §7.4, §8; TA-8, TA-13)

**Files (new, LF, `#!/usr/bin/env node`, header comment with WHY and usage, `'use strict'`, `child_process.spawnSync` /
`spawn` with argument arrays, exit 0 / 1 / 2, `--self-test`):**
`specs/krate-manager-phase6/evidence/scope-check.js`, `wsl-lane.js`, `close-lane.js`. Each is an adapted copy of
Phase 5's script of the same name (`specs/krate-manager-phase5/evidence/{scope-check,wsl-lane,close-lane}.js`, 247 /
187 / 812 lines), keeping its structure (`extractStep` over `manager-ci.yml`, ceilings, `killTree`, the
`timings.md` / `close-lane.log` writers) and changing only what this list says.

**Test first:** each script's `--self-test` written first against stub checkers that return GREEN for every input; it
must fail; quote the line. Then implement until `--self-test` is green.

- **`scope-check.js`** (L-SCOPE, FR-090, FR-091): `BASE = '3efe70de'`; the allowed paths of Rule 11: everything under
  `apps/krate-manager/` except the Rule 11 exclusions (each reported when changed), `.github/workflows/manager-ci.yml`,
  `tools/feed-contract/tests/manager-ci-shape.test.js`, `specs/krate-manager-phase6/**`, `specs/INDEX.md`, and
  `specs/Krate-manager-roadmap.md` with its `-U0` hunks limited to two windows: an insertion after old line 557 or 558
  (the Phase 6 status line and its blank line, between `### Phase 6: Detection and the Status Screen` at 557 and
  `**Spec:** \`krate-manager-phase6\`` at 559, the Phase 5 form of lines 527-531) and an insertion after, or a change
  within, old lines 714-719 (the Phase 10 `run_inno_uninstaller` bullet, which ends at 719); untracked files included
  (`git ls-files --others --exclude-standard`). `--self-test`: an allowed set → clean; a `plugins/x` path, a `tools/feed-contract/schemas/x`
  path, `apps/krate-manager/package.json`, a roadmap hunk at old line 300, and an untracked `dsp/x` → each reported.
- **`wsl-lane.js`** (L-WSL, TA-8): Phase 5's form with the command `cd <wslRoot>/apps/krate-manager && export
  CARGO_TARGET_DIR=$HOME/km-target && cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings && cargo
  test -p krate-manager-core --locked`; default log `F:/tmp/gate/km6-wsl/wsl-core-<timestamp>.txt`; the information line
  counts TA-8's six names as `test <name> ... ok`; ceiling 40 min. `--self-test`: Phase 5's five verdict cases plus a log
  missing one of the six names → GREEN with the info line `5/6`.
- **`close-lane.js`** (L-CLOSE, plan §7.4): `BASE = '3efe70de'`, `LOG_DIR = F:/tmp/gate/krate-manager-phase6-close/<stamp>`,
  `EVID_REL = 'specs/krate-manager-phase6/evidence/'`, `REQUIRED_NOTICE = '::notice title=required tests ran
  (Windows)::172 cargo + 29 vitest'` (plan §4.14), base size from `evidence/base-binary-size.txt` (TA-13). Rows: A0
  records; A0b porcelain snapshot; A0c base size; **A1** (one chain, the cargo commands share the target lock): steps
  `cargo fmt`, `clippy`, `clippy dev-feed`, `clippy-guard` (copied to `evidence/clippy-guard.txt`), `cargo test`
  (copied to `evidence/cargo-test.txt`), **step `core dependency tree`** (from the workflow text T012 adds; its
  `::notice …::<n> crates, none excluded`; `$RUNNER_TEMP/core-tree.txt` copied to `evidence/core-tree.txt`, SC-022),
  `cargo tree -p krate-manager --locked -e normal --prefix none` → `evidence/tree-app.txt` holding exactly one line
  `tauri-plugin-opener v2.7.2` and the same for `-p krate-manager-core` → `evidence/tree-core.txt` holding none (SC-033);
  **A2** (one chain): `npm ci` only if `node_modules` is absent, steps `typecheck`, `lint`, `vitest`, then `npm ls
  @tauri-apps/plugin-opener` → `evidence/npm-ls-opener.txt` reporting no such package (SC-033); **A3**: `node --check`
  on this phase's three scripts, `node --test` TAP (plan 1..59, `# fail 0`), L-TAP, L-VLINT; **A4** L-YAML on
  `manager-ci.yml`; **A5** L-LINT; **A6** L-INDEX (Phase 5's regenerate-once rule for lines naming
  `krate-manager-phase6/`); **A7** scope-check `--self-test` then L-SCOPE; **A9** the TA-4 grep (Rule 9: no `todo!(` /
  `unimplemented!(` in `crates/` or `src-tauri/src` outside `*_tests.rs` and `tests/`); **A10** L-WSL (`wsl-lane.js --out
  evidence/wsl-core.txt`, 6/6 names seen; an early signal, never a PENDING-CI close); **A11** the SC-028 node check
  over the roadmap (whitespace collapsed; the needles and counts of FR-092 / SC-028 **as T001 or T013 amended them per
  Q-C**, read from a two-line `evidence/sc028-needles.json` T013 writes, so the script does not hard-code an unruled
  answer) → `evidence/sc028.txt`; **B** (after A1 and A2 are green): step `required tests ran` (the notice above), step
  `tauri build` (40 min ceiling; 318 s in Phase 5), `build warnings`, `dev-feed absent`, `roster absent` (→
  `evidence/roster-absent.txt`), `artifact version`, the release-binary delta → `evidence/binary-size.txt`; **C1**
  porcelain equal to A0b minus `evidence/`; **C2** L-SCOPE again; then `node tools/clean-test-output.js --apply` (repo
  rule) with its summary in the log. Phase 5's rows A1d, A1g, A1h, A3e, A8, A12 are dropped (no `live-network` change,
  no `model-range-node.js`, no cross-target check this phase, no RustSec row). `--self-test`: `extractStep` over the
  real workflow for `cargo test` and `required tests ran`, the tree verdicts (an opener line in the core tree → RED,
  none in the app tree → RED, exactly one `v2.7.2` → GREEN), and the SC-028 counter over two in-memory texts.

**Expected red until:** none. Running L-WSL or L-CLOSE is **not** this task's (main loop). **Done when:** `node --check`
and `--self-test` of all three exit 0; `node specs/krate-manager-phase6/evidence/scope-check.js` runs against the
current tree and lists only paths this phase may change (record its output; a hit from another G2 task's in-progress
file is not a red). **Lane:** L-NODE (`node --check` only), the self-tests.

### T011 [P] — The status screen (spec FR-070 to FR-078; SC-019, SC-020, SC-034, SC-035; plan §4.10, §7.1 rows 019, 020, 034, 035, PD-12, PD-13, PD-14, PD-18, SI-4, SI-10, SI-11; TA-5)

**Files:** new `src/view.ts`, `src/status.ts`, `src/notes.ts`, `src/status.test.ts`; edit `src/main.ts`, `src/main.test.ts`,
`src/styles.css` (CRLF kept on the edited files; new files LF). Nothing else (no `package.json`, no new npm package,
FR-065).

**Test first:**
- `src/status.test.ts` (SC-019 as amended by SI-10, SC-034; rows 019, 034): the 13 `renders <scenario>` cases, each
  importing its golden from `../tests/fixtures/status/<scenario>.json` (`resolveJsonModule`, `tsconfig.json`) and passing
  it through `toStatusView` (PD-12) before `renderCards` / the section render into a detached element; the FR-071 /
  FR-072 / FR-074 strings, card count, `h2` names in order, the empty `querySelectorAll("a, img, iframe, object, embed,
  link, script, style")`, the attribute walk (`class`; `start` on `ol` only; `disabled` on `button` only; `open` on
  `details` only and absent on a fresh render; every `summary` reads `Release notes`); `renders notes-rendering` also
  pins the FR-072 tag sequence of `plan.md:1129`; `does not call status commands when another instance is running`
  (through `renderShell`); `every status text is rendered by a scenario` (every literal part of every `STATUS_TEXTS`
  template occurs in the `textContent` of at least one rendered status section); the four SC-034 titles `renders the open
  release page button`, `open release page calls open_release_page once`, `open release page shows a rejection`, `no
  navigation path but the release page button` (the URL from the `installed-elsewhere` golden, never a literal: the
  frontend scan bans `https://` in `src/`, `config_guard.rs:860-889`; the source-scan needles built by concatenation,
  e.g. `"@tauri-apps/" + "plugin-opener"`, `"window." + "open"`, `"h" + "ref"`, `"<" + "a"`, over the non-test
  `src/*.ts` read with `node:fs`, plus the in-memory plant of each).
- `src/main.test.ts` (FR-070, SC-020, SC-035; rows 020, 035; plan §4.10 "the four Phase 4 cases"): the four Phase 4 cases
  updated per `plan.md:844-854` (their titles unchanged; the `no-feed` golden through `toStatusView` as the
  `plugin_status` answer); the four SC-020 titles `check now disables the button while pending`, `check now calls
  check_now and re-renders`, `check now recovers from a rejection`, `shows the ten-minute note`; the three SC-035 titles
  `shows the pending text until the first result`, `check now stays disabled until the first result`, `check now is
  enabled after a rejected first load`; `mockIPC` handlers returning deferred promises (`node_modules/@tauri-apps/api/
  mocks.js:126-132`), `afterEach(clearMocks)` as today.
- Red: before the goldens exist, the imports fail (`Failed to resolve import`); quote it, then wait (below). With the
  goldens present and the renderers as stubs, the assertions fail; quote one line per file.

**Wait (TA-5, in-group).** T005 writes the 13 goldens in G2. Write the tests and the implementation first; before the
first full `npm test`, poll every 60 s (ceiling 45 min) until all 13 `tests/fixtures/status/*.json` exist; past the
ceiling return done with notes "vitest unverified: goldens absent (T005)", which the G2 gate then measures.

**Implement:** plan §4.10 (`plan.md:805-842`): `view.ts` (the FR-046 / FR-051 types as string-literal unions and
`readonly` interfaces; `toStatusView(value: unknown): StatusView` checking exact key sets, types and enum values,
throwing `TypeError("<json path>: <expected>")`); `status.ts` (`STATUS_TEXTS` with every FR-071 cell, the FR-074 feed-line
templates, `Check now` and the ten-minute note; `ACTION_TEXTS` with the pending text, `Checking…` and the open-failure
text; `fill`; `Invoker`; `createStatusSection`, `renderCards`, `mountStatus`; PD-13's rejected-`check_now` rule: keep the
cards, replace the feed line; PD-18's append-before-await; every string through `textContent`; the only data-set
properties `ol.start` and `button.disabled`; the release URL only in the click closure; numbers through `String(n)`);
`notes.ts` (`renderBlocks` per FR-072 with the `never`-typed `default` arm; `notes: []` → `p` `No release notes.`);
`main.ts` (`await mountStatus(root, invoke)` after the version line; the locked path returns before it); `styles.css`
(rules for `.status`, `.plugin`, `.copies`, `.record-notice`, `details.notes`; no `url(`, `@font-face`, `@import`,
`image-set(`, FR-077). No element `a`, `img`, `iframe`, `object`, `embed`, `link`, `script`, `style`; no `innerHTML` /
`outerHTML` / `insertAdjacentHTML` (the ESLint ban, `eslint.config.mjs:13-19`); no `window.open`, `location`
assignment, `href` or opener import in non-test `src/*.ts` (FR-078).

**Expected red until** (in-group): the vitest suites until T005's goldens exist (above). **Done when:** L-TS green
(`npm test` with all 26 new titles plus Phase 4's four passing); the notes list the vitest titles that ran. **Lane:**
L-TS.

---

## G3 — Integration (sequential)

### T012 — `manager-ci.yml` step `core dependency tree`, the shape test, `required-tests.json` (spec FR-080 step, FR-085, FR-090; SC-022 CI part, SC-023; plan §4.13, §4.14)

**Files:** `.github/workflows/manager-ci.yml`, `tools/feed-contract/tests/manager-ci-shape.test.js`,
`apps/krate-manager/tests/required-tests.json` (all CRLF, Rule 12).

**Test first:** in the shape test, add `'core dependency tree'` after `'cargo test'` in `STEPS` (`:163-190`), to
`ASSERTION_STEPS` (`:200-211`) and a `COMMANDS` entry (`:214-301`, after `'cargo test'` at `:242`) holding the two trimmed lines `cargo tree -p
krate-manager-core -e normal --locked --prefix none --format "{p}" > "$out"` and `if [ -z "$hits" ] && [ "$n" -ge 1 ];
then` (plan §4.13); run L-NODE: the shape test's existing cases fail on the missing step (quote the line); nothing else
in the file changes (FR-090), so the TAP plan stays `1..59`.

**Implement:**
1. `manager-ci.yml`: insert, right after the `cargo test` step (`:117-120`), the step of plan §4.13 (`plan.md:963-979`)
   verbatim, indented like its neighbours, CRLF.
2. `required-tests.json`: exactly plan §4.14's additions (`plan.md:987-1015`) as amended by T001's FR-085 (35 names to
   `cargo.all`; `cargo.windows` +5; `cargo.macos` +7; `cargo.linux` +6; `vitest` +26, the titles exactly as in T011);
   resulting lengths 165 / 7 / 24 / 22 / 29 (check with `node -e` and record). No name of a test that does not exist:
   verify each `cargo.all` and `cargo.windows` name against `cargo test --workspace --locked -- --list` (shared target)
   and each `vitest` title against `npm test -- --reporter=json --outputFile.json=F:/tmp/gate/km6-T012/vitest.json`;
   the macOS / Linux names against `grep -n "fn <name>" crates/core/src/detect_tests.rs` with the right `cfg`.
3. Run the `required tests ran` step locally: `cargo test --workspace --locked 2>&1 | tee F:/tmp/gate/km6-T012/cargo-test.txt`,
   the vitest JSON above, then the step's `run:` text through Git Bash with `RUNNER_TEMP=F:/tmp/gate/km6-T012` and
   `RUNNER_OS=Windows` → exactly `::notice title=required tests ran (Windows)::172 cargo + 29 vitest` and no `::error`.
4. Run the new `core dependency tree` step's text the same way → `::notice title=core dependency tree (Windows)::<n>
   crates, none excluded` (record `n`; plan §1.1 measured 76 crates before this phase's `pulldown-cmark`).

**Done when:** L-NODE `# fail 0`, L-TAP `TAP plan=1..59 top-level=59`, L-YAML, L-LINT green; the two notices recorded.
**Lane:** L-NODE, L-TAP, L-YAML, L-LINT.

### T013 — Q-C rows, the roadmap hunk, INDEX, lints, cleanup, the PENDING-CI list and the close hand-over (spec FR-090 to FR-092, SC-001 local part, SC-028; plan §4.15, §7.3, §7.4, §8, SI-14)

**Files:** `specs/Krate-manager-roadmap.md` (one hunk), `specs/krate-manager-phase6/spec.md` (the SI-14 rows only, if
T001 deferred them), `specs/INDEX.md` (regenerated), new `specs/krate-manager-phase6/evidence/sc028-needles.json` (LF).

**Precondition (Q-C).** `spec.md` `## Clarifications` records Q-C's answer; otherwise return `blocked`: "Q-C (plan
§10, SI-14) unruled: the FR-092 roadmap hunk and SC-028 cannot be written".

**Test first:** `node -e` (the SC-028 check, whitespace collapsed) over the roadmap **before** the edit: for answer (a)
the count of `is never uninstalled by the helper` is 0 (red) and of `in the plugin's install dir under the compiled-in
Windows plug-in root` is 1; for (b) the full SI-14 (b) sentence's count is 0. Quote the counts.

**Implement:**
1. If T001 deferred SI-14: apply it now per the ruled answer (T001's SI-14 bullet), CRLF, tagged `(plan SI-14)`.
2. Roadmap (plan §4.15, `plan.md:1028-1040`): under (a), keep lines 716-717 word for word and add, after the
   `run_inno_uninstaller` bullet's existing text (it ends at line 719, `…per_os_install_mechanics.md KQ2).`), as
   continuation lines indented four spaces like 715-719, the one sentence "A managed copy whose recorded `InstallLocation` lies
   outside the compiled-in Windows plug-in root is never uninstalled by the helper: the manager shows it as managed and
   tells the user to uninstall it from Windows Settings." (wrapped at the file's ~120 columns, CRLF); under (b), SI-14
   (b)'s sentence. Leave the Phase 6 status line to the main loop at close.
3. `evidence/sc028-needles.json`: `{"needles": [[<text>, <count>], …]}` for the ruled answer (the close lane's A11 reads
   it, T010).
4. `node tools/gen-specs-index.js`, then `--check` → exit 0 (FR-091).
5. Repo lints: L-LINT (`node tools/lint-serial-test-runs.js`). `node tools/check-portability.js` and `node
   tools/lint-plugin-editor-baseline.js` are **not** run: no C++ file and no plugin editor changed (Rule 11; `git diff
   --name-only 3efe70de` shows nothing under `plugins/`, `dsp/`, `cmake/`, `extern/`); record that diff's output.
6. L-SCOPE (T010's script) → exit 0.
7. Check the PENDING-CI list below against `required-tests.json` (every `cargo.macos` / `cargo.linux` name appears in a
   row) and record "PENDING-CI: N item(s)" with N = the row count.
8. Scratch cleanup: delete `F:/tmp/km6-target/` (any slot left) and the `F:/tmp/gate/km6-*` logs once their results are in
   the notes; `node tools/clean-test-output.js --apply` (repo rule) runs inside L-CLOSE.

**Done when:** the SC-028 counts match the ruled needles; L-INDEX, L-LINT, L-SCOPE green; the notes end with "deferred
to the main loop at close: run `node specs/krate-manager-phase6/evidence/close-lane.js`" (the release `tauri build`, L-WSL
and the full plan §7.4 procedure, ~7 min by Phase 5's 414.6 s total), and the main loop then writes the roadmap's Phase 6
status line `**Status: ✅ COMPLETE (PENDING-CI: N item(s)) (<date>)** — see specs/krate-manager-phase6/compliance.md`.
**Lane:** L-INDEX, L-LINT, L-SCOPE.

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

Every row closes only on a pushed commit's `manager-ci.yml` run, under the spec's three-part rule (`spec.md:810-820`):
the committed `required-tests.json` lists the name, the leg's `required tests ran` step passed with no `::error title=
required tests ran (<os>)::…` line, and its notice reads `n = len(cargo.all) + len(cargo.<os>)`, `m = len(vitest)`:
**`::notice title=required tests ran (macOS)::189 cargo + 29 vitest`** on `check (macos-latest)` and
**`::notice title=required tests ran (Linux)::187 cargo + 29 vitest`** on `check (ubuntu-24.04)` (plan §4.14 totals). A
red there is a Phase 6 defect fixed before close (plan §7.3: the macOS arms are compiled for the first time there).

| # | SC | Job | Assertion that closes it |
|---|---|---|---|
| 1 | SC-001 (macOS) | `check (macos-latest)` | steps `clippy`, `cargo test`, `tauri build`, `build warnings` green (0 `warning:` lines) |
| 2 | SC-001 (Linux) | `check (ubuntu-24.04)` | the same four steps green |
| 3 | SC-002, 004-012, 017, 018, 021, 022 (tests), 024, 026, 029 (`all`), 030-033 (macOS) | `check (macos-latest)` | `required tests ran` notice `189 cargo + 29 vitest`, no `::error` |
| 4 | the same SCs (Linux) | `check (ubuntu-24.04)` | `required tests ran` notice `187 cargo + 29 vitest`, no `::error` |
| 5 | SC-003 | both legs | `detect::tests::native_layout_macos` (in `cargo.macos`) / `native_layout_linux` (in `cargo.linux`) under rows 3 / 4 |
| 6 | SC-013 | `check (macos-latest)` | `detect::tests::au_plist_cases_macos` in `cargo.macos` |
| 7 | SC-014 | `check (macos-latest)` | `detect::tests::pkg_receipt_macos` in `cargo.macos` |
| 8 | SC-015 | `check (ubuntu-24.04)` | `detect::tests::linux_detection_linux` in `cargo.linux` |
| 9 | SC-025 (macOS) | `check (macos-latest)` | `detect::tests::host_facts_through_fake_io_macos` |
| 10 | SC-025 (Linux) | `check (ubuntu-24.04)` | `detect::tests::host_facts_through_fake_io_linux` |
| 11 | SC-027 | both legs | `detect::tests::probe_metadata_error_unix` (ENOTDIR and the `mkfifo` FIFO case) |
| 12 | SC-029 (unix) | both legs | `detect::tests::listing_skips_symlinked_dirs_unix` and `listing_case_exact_unix` |
| 13 | SC-022 (tree, macOS) | `check (macos-latest)` | step `core dependency tree`: `::notice title=core dependency tree (macOS)::<n> crates, none excluded`, no `::error`; `n` includes `plist` |
| 14 | SC-022 (tree, Linux) | `check (ubuntu-24.04)` | step `core dependency tree`: `::notice title=core dependency tree (Linux)::<n> crates, none excluded`, no `::error` |
| 15 | SC-019, 020, 034, 035 | both legs | step `vitest` green and the `required tests ran` vitest count `29` |
| 16 | SC-024 (roster absent) | both legs | step `roster absent` notice with 0 hits |
| 17 | SC-021, SC-033 (the `src-tauri` compile with the opener crate and `os_facts.rs`'s `cfg(not(windows))` arms) | both legs | steps `clippy` and `clippy dev-feed` green (the first compile of those arms, plan §7.3, §8) |

L-WSL (TA-8) gives an early local signal for rows 4, 5, 8, 10-12 and 14's core half before the push; it closes none of
them.

---

## Defects found (in the plan, while writing this list)

- **D-1 (TA-1).** Plan §7.1 rows 027 and 029 (SI-17, SI-20) start `/usr/bin/mkfifo` and `cmd /C mklink /J` from
  `detect_tests.rs`, which spec FR-086 forbids, Phase 5's Command rule reports (`injection.rs:1669-1686`,
  `TEST_PROGRAMS` `:233`), and, for `/usr/bin/mkfifo`, FR-088's `/usr/` regex over `detect_tests.rs` reports too. TA-1
  resolves it pending the user's acceptance (Preconditions).
- **D-2 (TA-3).** Plan §4.4 adds `check_sandboxed` in the foundation; uncalled, it warns `dead_code` at the G1 gate.
- **D-3 (TA-2).** Plan §4.12 scans "`is_test_source` files minus `injection.rs`" for FR-083, which misses
  `test_support.rs` (`injection.rs:1242-1249`), a file FR-083 names.
- **D-4 (Rule 4e).** Plan §4.11 names the committed fixture `iterum-shape.json`; the ROSTER rule bans shipped plugin
  names in every file under `apps/krate-manager/`, so T003 commits it as `sdk-shape.json` with the same content shape.
  (`roster.rs` scans sources, not `tests/fixtures/`, so no guard would have caught it.)
- **D-5 (TA-7).** Plan §4.1's comment for the opener pin, copied into `src-tauri/Cargo.toml`, would hold the substring
  `tauri-plugin` on a second line, which SI-2's narrowed rule still reports.
- **D-6 (G1 / T002, found by the build, fixed in the main loop 2026-10-31).** The fixture crate
  `tests/fixtures/clippy-guard` has its own `Cargo.lock` with path dependencies on core and src-tauri; T002's 40 new
  packages (pulldown-cmark, tauri-plugin-opener, zbus, ...) were not in it, so L-GUARD's `--locked` run refused
  (`rc=101 ... other-errors=1`). The same defect as Phase 5 (`specs/krate-manager-phase5/tasks.md` "Defects found").
  Fixed by `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 --offline` from
  `apps/krate-manager` (+492 lines, LF kept; 494 name@version pairs, equal to the workspace lock apart from the two
  crates' own names). L-GUARD then: `::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1
  std::env::var=1 lint-named=5 other-errors=0`.
