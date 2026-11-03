# Tasks: Krate Manager Phase 12 — Product Completion and First Release Readiness

**Spec:** `specs/krate-manager-phase12/spec.md` (DRAFT revision 3: D1-D17, F-1 to F-8, FR-001 to FR-111, SC-001 to
SC-032, Clarifications of 2026-11-02, `spec.md:1025-1064`) · **Plan:** `specs/krate-manager-phase12/plan.md` (DRAFT
revision 2: PD-1 to PD-28 in §0.1, SI-1 to SI-22 in §0.2, error model §3, module design §4, test plan §7, lanes §8, risks
§9 R-1 to R-13, preconditions §10 P-1 to P-3) · **Roadmap:** `specs/Krate-manager-roadmap.md` Phase 12 (lines 894-949),
Reuse Inventory line 200, Cross-Cutting Constraints lines 1005-1046.
**Status:** DRAFT task list, revision 1, 2026-11-02. Written against `feat/krate-manager` at `7c4b6837`
(`git rev-parse --short HEAD`; `git status --porcelain` shows only `?? specs/krate-manager-phase12/`), the commit the
plan was verified at, so every plan citation stands. Every file:line below was read this session at `7c4b6837`.
**Spec state:** none of plan §0.2 is in `spec.md` yet (`spec.md:300` still says "at most 64 entries", `spec.md:773`
"65 items", `spec.md:541` `needs: feed`). Following the Phase 10 / 11 precedent (`specs/krate-manager-phase11/tasks.md:30-38`),
every task states its assertions from the plan's §0.1 / §0.2 / §4 / §7 rows, which are **binding as written** as amended
by this file's TA rows, and T002 writes SI-1 to SI-22 into `spec.md` in G2 beside the code.
**Lanes in this phase:** the **app lane** and the **pipeline lane** (plan header "Lanes"), plus three **main-loop lanes**
(L-WSL, L-CLOSE, and the network reading L-RUSTSEC). No file under `plugins/`, `dsp/`, `cmake/` or `extern/` changes
(spec FR-111), so there is **no C++ lane**: no `run-gate.js`, no clang-tidy, no pluginval, no `check-portability.js`, no
`lint-plugin-editor-baseline.js` (no plugin editor changes); `header-consumers.js` is run once to record `none` (T017).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks at the same time in one working tree; a task is
  **[P]-safe** when no other task in its group touches any of its files and none of its files is a **shared file**: any
  `Cargo.toml` / `Cargo.lock`, a crate root (`crates/core/src/lib.rs`, `src-tauri/src/lib.rs`), `src-tauri/src/commands.rs`,
  `src-tauri/build.rs`, any `tauri*.conf.json`, `src-tauri/capabilities/main.json`, `package.json`, `.gitignore`,
  `.gitattributes`, any existing workflow under `.github/workflows/`, `tests/required-tests.json`,
  `tools/feed-contract/tests/manager-ci-shape.test.js`, `tools/feed-contract/tests/hygiene.test.js`. Paths without a
  prefix are under `apps/krate-manager/`.
- **Every shared file the code groups need is edited by T001 (G1, sequential, alone)**: the `mod batch` line and its
  placeholder file, every new public signature other G2 tasks compile against (placeholder bodies), the small real
  pieces two or more G2 tasks share (`install::manifest_heads`, `fixture_server::Reply::Held`, the `view.ts` parsers,
  `StatusView.orphans` with the goldens, `StatusSection.lastOs` / `refresh`), all of `src-tauri` (glue, no test target),
  the guard pins its own edits trip, the required-test lists and the `.gitattributes` line. The fourteen G2 tasks then
  touch only their own files (ownership table in "Dependency overview"). No dependency is added in this phase (spec
  FR-061, SC-023; plan §5), so no task edits a `Cargo.toml` or a lockfile and
  `tests/fixtures/clippy-guard/Cargo.lock` is never re-locked; a task that finds it must add a dependency stops and
  returns `blocked` (that would be a spec change).
- **Every edit of an existing workflow is in G3** (shared-file rule): T016 is the one task that edits `release.yml`,
  `docs.yml`, `release-manager.yml`, `feed-tools.yml` and the two shared node test files, and its expected values (the
  TAP plan, the new step names, the `gen-root-index.js` CLI) depend on G2's files. This is also the integration group
  the phase needs (the per-OS CI additions of this phase's SCs, written, never pushed).
- **Fence U-1** (spec FR-076, D11) sits between G3 and G4: the build stops there until the user has recorded FR-103,
  then FR-074 (a)-(c) and (f); G5 closes the readiness ledger after the user's final CI runs. G4 is post-release (spec
  revision 2026-11-03, D17 R-POST-4: no Mac before release): it writes the macOS text once both FR-073 rows exist,
  after the first real dispatch.
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block is too long to copy without drift, the section
  names the `plan.md` row (PD-n, SI-n, §4.x, §7.1 SC row) that holds it; that text is then **binding, verbatim**, as
  amended by plan §0.2 and this file's TA rows. Where this file and the plan differ, this file wins and the TA row says
  why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the task's notes quote the failing line.
  The red run normally uses T001's placeholder, so the test compiles and fails on `not yet implemented: T0nn` or on its
  assertions. A pure refactor (no new behaviour) has no red: its proof is the named existing suite green before and
  after, both runs' summary lines quoted in the notes.
- **In-group expected reds**, each raised from the named file and gone by its group gate: (a) G2: T009's
  `update_all_excludes_manager_line` until T008's Update-all control exists (it clicks `button.update-all`, which T008
  writes); (b) G2: T007's real-tree rows of `plugin_commands_take_the_root_ticket` cannot be red (T001 wrote
  `commands.rs`), and its `stage_seam` rule reads T001's declaration only. Those are the only expected reds; **any
  other red at a gate is a real red**, and a compile error is never an expected red.
- Never commit, never push, never trigger or dispatch CI, never run `release-manager.yml`, `release.yml`, `docs.yml`
  or `backfill-sign.yml`. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system** (spec Safety, `spec.md:823-843`; roadmap lines 1007-1014). No test, gate or agent
   writes under `C:\Program Files`, `C:\ProgramData`, `%LOCALAPPDATA%`, `%APPDATA%`, `/Applications`, `/Library`, a real
   VST3 / Components / preset folder, the registry or the real state dir; none runs the helper elevated, an installer,
   `npm run bundle`, `tauri build`, `tauri signer`, `rsign`, `pkgbuild` or `sudo`. Core tests build roots from
   `test_support::fresh_temp_roots()` / `fresh_elevated_roots()` / `temp_roots(base)` (`crates/core/src/test_support.rs:206-225`,
   `check_sandboxed` panics on an outside path) and use `FakeElevation` (`:417`) and `FakeHostIo`, never
   `NativeElevation` or `OsHostIo`. The only network peer of any test is the loopback `FixtureServer`
   (`crates/core/src/fixture_server.rs`). No test calls `github.com`, `api.github.com`, `krateaudio.com` or `rustsec.org`
   (URLs naming them are **data**). No key file is written: signing keys come from `test_signer::TestSigner` in memory
   (core) or `tools/minisign.js` in memory / under `os.tmpdir()` (node). The only network reads of the phase are the
   main-loop lane L-RUSTSEC (`evidence/rustsec-check.js`, T015) and nothing else.
2. **No lint suppression added.** `[workspace.lints.clippy] allow_attributes = "deny"`,
   `allow_attributes_without_reason = "deny"` (`apps/krate-manager/Cargo.toml`, `[workspace.lints.clippy]`): never
   `#[allow]`; no new `#[expect(…)]` is planned in this phase (one that proves unavoidable carries `reason = "…"` and is
   listed in the task's notes). No `// @ts-ignore`, `@ts-nocheck`, `eslint-disable`.
3. **Unit tests live in `<stem>_tests.rs`**, mounted at the end of `<stem>.rs` as `#[cfg(test)] #[path =
   "<stem>_tests.rs"] mod <name>;` (an inline `mod tests {` is reported, `crates/core/tests/injection.rs:708`,
   `:1420-1421`). The batch tests are `install::batch::<name>` from `install_batch_tests.rs`, mounted like
   `install_loopback_tests.rs` (`crates/core/src/install.rs:4301-4303`).
4. **Literal hygiene.** (a) `crates/core/src/**` outside `platform/` holds none of `FORBIDDEN_OUTSIDE_PLATFORM`
   (`injection.rs:23`), comments included; (b) a non-test `platform/` file holds only `PLATFORM_LITERALS` string
   literals (`injection.rs:51`), so a placeholder there is a bare `todo!()`, never `todo!("T006")`; (c)
   `PHASE7_HOST_NEEDLES` (`github.com`, `githubusercontent.com`, `krateaudio.com`, `--test-threads`, `injection.rs:258-263`)
   occur in none of `PHASE7_TEST_FILES` (`:249-256`), which T007 widens to `install_batch_tests.rs`; (d) `FR088_FILES`
   (`:319-328`, widened by T007 to `install_batch_tests.rs`) match none of `FR088_PATTERNS` (`:330-342`:
   `127\.0\.0\.1:[0-9]+`, `localhost:[0-9]+`, `AppData`, `Program Files`, `/usr/`, `/Library`, `HKEY_`, …), comments
   included: use `server.url(path)`, never a literal loopback address; (e) no file under `src/` (tests included) or
   `index.html` holds `http://` or `https://`, and none uses `innerHTML` / `outerHTML` / `insertAdjacentHTML` /
   `document.write` (`config_guard.rs`, `check_frontend`, `:180-183`); (f) use invented plugin names (`Alpha`, `Beta`),
   never one of the nine shipped plugins.
5. **Zero warnings.** `cargo fmt --all --check`; every `cargo clippy … -- -D warnings` with 0 `warning:` lines;
   `npm run typecheck`; `npm run lint` (`eslint . --max-warnings 0`); no `Warning:` / `DeprecationWarning` line from
   `node --test`.
6. **Parallel tests, never serial.** `cargo test` with its default threads; `npm test` (vitest, parallel files);
   `node --test` (files in parallel). Never `--test-threads=1`, `--no-file-parallelism`, `--test-concurrency=1`.
7. **Placeholders.** A T001 placeholder body is `let _ = (<every parameter>); todo!("T0nn")` (outside `platform/`) or
   `let _ = (<every parameter>); todo!()` (inside it), naming the owner task; the parameter names stay the plan's (a
   bare `todo!()` body warns `unused variable` on every parameter: measured this session with `rustc --edition 2024` on a
   two-parameter `todo!("T003")` function, two `warning: unused variable` lines). The owner replaces the whole body.
8. **Line endings.** Every existing file this phase edits is CRLF in this working tree (`core.autocrlf=true`; counted
   this session: e.g. `install.rs` 4307/4307 lines, `release.yml` 865/865); keep each file's endings. Files under
   `tools/feed-contract/fixtures/**` are `-text` (`.gitattributes:29`) and are written **LF**, byte-exact; a snapshot of
   a committed file is taken with `git show 7c4b6837:<path>` (the LF blob), never by copying the CRLF working copy.
9. **No heredocs in Bash** (the repo's `guard-heredoc.js` hook blocks them): write scripts and multi-line inputs with the
   Write tool and run them with `node <file>`; one git operation per command, never chained.
10. **Builds share the box.** F: has 2.5 GB free while `apps/krate-manager/target` holds 14 GB (`df -h /f`,
    `du -sh target`, this session), so no per-task target dir goes on F:. A G2 Rust task's working loop (L-OWN) uses
    `CARGO_TARGET_DIR=C:/km12-target/<Tnnn>` with `CARGO_BUILD_JOBS=6` (five Rust tasks at once on 32 cores) and removes
    that directory before returning; the group gate uses the shared `target/`.
11. **Logs** go under `F:/tmp/gate/km12-<task>/` (small text files only); a task removes any scratch tree it made.

### Lanes (referenced by every task)

| Lane | Commands (from `apps/krate-manager/` unless "repo root") | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked 2>&1 \| tee F:/tmp/gate/km12-<task>/cargo-test.txt` (Phase 11's took 53.6 s, `specs/krate-manager-phase11/evidence/timings.md` row A1h) | fmt exit 0; clippy exit 0 with 0 `warning:` lines; every test binary `0 failed` |
| **L-OWN** | `CARGO_TARGET_DIR=C:/km12-target/<Tnnn> CARGO_BUILD_JOBS=6 cargo test -p krate-manager-core --locked --lib <filter>` (or `--test injection` / `--test config_guard`, or `-p krate-manager-helper --lib job::`) and the same-target `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings` (a working loop, not a gate; Rule 10) | the task's own tests and 0 `warning:` lines |
| **L-APP** | `cargo clippy -p krate-manager --all-targets --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-MANUAL** | `cargo clippy -p krate-manager --example manual_elevation --features manual-elevation --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-E2E** | `KRATE_E2E_FEED_URL=http://127.0.0.1:9/versions.json KRATE_E2E_MARKER_DIR=never-run KRATE_E2E_OLD_VERSION=0.0.0 cargo clippy -p krate-manager --all-targets --features updater-e2e --locked -- -D warnings` (`manager-ci.yml:119` step `clippy updater-e2e`) | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (`.github/workflows/manager-ci.yml:132-152`) extracted into a file by a throwaway Node script and run with `"C:/Program Files/Git/bin/bash.exe"`, `RUNNER_TEMP=F:/tmp/gate/km12-<task>`, `RUNNER_OS=Windows` | `::notice title=clippy-guard (Windows)::… other-errors=0`, no `::error` (Phase 11: `rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0`, `specs/krate-manager-phase11/evidence/clippy-guard.txt`) |
| **L-HARNESS** | `node specs/krate-manager-phase11/evidence/harness-lane.js` (repo root; the Phase 11 refusal harness compiles against core, which this phase changes; 11.2 s at Phase 11's close) | last line `HARNESS LANE GREEN (…)` |
| **L-TS** | `npm run typecheck`; `npm run lint`; `npm test` | each exit 0; vitest 70 titles before G2, 86 after (TA-1) |
| **L-NAPP** | `node --test --test-reporter=tap "scripts/*.test.js" > F:/tmp/gate/km12-<task>/scripts.tap` (quoted glob) | `# fail 0`; the six `node` names of `required-tests.json` each `ok` once (unchanged this phase) |
| **L-NODE** | `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km12-<task>/all.tap` (repo root) | `# fail 0`; no `Warning:` / `DeprecationWarning` / `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km12-<task>/all.tap` (repo root; replays `feed-tools.yml`'s TAP check) | run **only from T016 on** (G2 adds four top-level tests, so `1..64`, `feed-tools.yml:94`, is stale until T016 sets it): exit 0 and `TAP plan=1..<P> top-level=<P>` with T016's measured `<P>` (69 expected, TA-15) |
| **L-YAML** | `npx --yes yaml@2.6.1 valid < .github/workflows/<file>.yml` (repo root) for `release.yml`, `docs.yml`, `release-manager.yml`, `feed-tools.yml` | exit 0 each |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root) | exit 0, no stderr |
| **L-INDEX** | `node tools/gen-specs-index.js --check` (repo root) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase12/evidence/scope-check.js` (repo root; T015) | exit 0 |
| **L-REQ** | the `run:` text of `manager-ci.yml` step `required tests ran` (`:279-310`) extracted as L-GUARD does, run over L-RS's `cargo-test.txt`, a `npm test -- --reporter=default --reporter=json --outputFile.json=<dir>/vitest.json` report and L-NAPP's TAP, `RUNNER_OS=Windows` | `::notice title=required tests ran (Windows)::406 cargo + 86 vitest + 6 node` (389 `all` + 17 `windows`, TA-1), no `::error` |
| **L-WSL** (main loop) | `node specs/krate-manager-phase12/evidence/wsl-lane.js` (T015; core + helper clippy and test in WSL Ubuntu 24.04, `$HOME/km-target`; Phase 11's took 89.2 s) | last line `WSL LANE GREEN (…)`; an early signal only, it closes no PENDING-CI row |
| **L-CLOSE** (main loop) | `node specs/krate-manager-phase12/evidence/close-lane.js` (T015) | last line `CLOSE LANE GREEN` |
| **L-RUSTSEC** (main loop, network) | `node specs/krate-manager-phase12/evidence/rustsec-check.js` (T015) | prints the three page statuses, the three `Cargo.lock` versions and one ready-to-paste `RELEASING.md` row; last line `RUSTSEC NO ADVISORY` or `RUSTSEC ADVISORY: <ids>` |

**Group gates** (the workflow's gate reporter): after G1: L-RS, L-APP, L-DEVFEED, L-MANUAL, L-E2E, L-GUARD, L-TS,
L-NODE, L-VLINT. After G2: the same plus L-NAPP, L-HARNESS, L-SCOPE, `--self-test` of T015's scripts. After G3:
everything T017 lists, then L-CLOSE and L-WSL (main loop). After G4: L-TS, L-NODE, L-TAP, L-SCOPE. G5 is the main loop's.

---

## Preconditions — rulings before the build stage

- **Plan §10 P-1 to P-3 are the user's.** This file is written to their recommendations, as the plan is (plan header):
  P-1 `docs.yml` calls `tools/gen-root-index.js` (PD-24); P-2 the Phase 11 R-7 asset-host check is a manual line in
  `RELEASING.md` and one ledger row; P-3 the batch cap is 32 (`job::MAX_OPS`), with the cross-crate equality test.
  **Precondition check (T001 and T002 run it first; on failure the task changes nothing and returns `blocked`, quoting
  the bullet):** in `spec.md` `## Clarifications`, a bullet dated 2026-11-02 or later that rules P-1, P-2 or P-3
  otherwise than recommended, or rules against a plan PD / SI row or a TA row of this file. Absence of such a bullet
  is acceptance of the recommendation. The main loop then re-plans the named sections.
- **The macOS route (D11).** If the FR-073 records show the pkg route fails, Q5 is re-ruled by the user before G4; T018
  checks and returns `blocked` (it never writes a route the record does not support).

### Task-list amendments (TA)

Rows marked **spec** change spec text (T002 writes them, tagged `(tasks TA-n)`); the others explain why a task differs
from a plan block or close what the plan left open.

| Id | Amendment | Why | Spec? |
|---|---|---|---|
| TA-1 | **`tests/required-tests.json` and `EXPECTED_REQUIRED` are written in T001**, together (`manager-ci-shape.test.js:706` deep-equals the file at `:1430`). Counted this session (`node -e` over the file): `cargo.all` 355, `cargo.windows` 17, `cargo.macos` 49, `cargo.linux` 43, `vitest` 70, `node` 6. After T001: `cargo.all` 355 + 34 = **389** (the 32 names of plan §7.1 / §7.2 plus TA-2's and TA-3's), the per-OS lists unchanged (SI-18: every Phase 12 cargo test runs on all three legs through forced `Os` values, and the checker wants exactly one passing line per name over `all` + the leg's list, `manager-ci.yml:286-291`), `vitest` 70 + 16 = **86**, `node` unchanged (its names are `scripts/*.test.js` titles, `manager-ci.yml:265-272`; this phase adds no script test). The names are listed in T001 step 12 | both files are shared, so they belong to the one sequential task, and they must move together | no |
| TA-2 | **`install::manifest_heads` (PD-7) is real in T001, with its own required test `install::tests::manifest_heads_cases`**: `<state>/manifests/` absent → `Ok(vec![])`; a regular file at that path → `Manifest { reason: NOT_A_DIR }`; 256 entries → read, 257 → `Manifest { reason: "too many manifests" }`; a `.alpha.json.tmp` (the atomic-write temp form, `install.rs:2134`), a `readme.txt` and a sub-directory are skipped; a `manifest_version: 1` file with string `plugin` / `version` → both `Some`; `manifest_version: 2` → `plugin: None`; non-string `version` → `version: None`; an unparsable file → a head with both `None`; heads sorted by `stem`. Read only through `InstallIo` (`install.rs:1437-1459`), each file bounded by `detect::MAX_MANIFEST_BYTES` | three G2 tasks read it (T003 orphan Remove, T004 the root lock, T005 status orphans); real in T001 removes three cross-task reds | no |
| TA-3 | **`fixture_server::Reply::Held` / `HoldGate` (PD-21) are real in T001, with the required test `fixture_server::tests::held_reply_releases`** (in `fixture_server_tests.rs`): two held paths → `wait_open(2, 5 s)` true and `open()` lists both; for 300 ms neither response arrives; `release_one()` returns the older path and only that client receives its body; `release_all()` frees the other; a third client held when the server drops returns within 5 s (the `Drop` releases every hold, `fixture_server.rs:4-6`) | T003 (`download_bound`, `busy_items`) and T004 (`root_gate_cases`) both hold responses | no |
| TA-4 | **`ops_after` (PD-20) lives in `install_batch_tests.rs` (T003)**, its only user. T001 makes the Phase 10 rig of `install_tests.rs` visible to `install::batch` by visibility edits only: `pub(super)` on `Fx` (`install_tests.rs:474`) and `Seam` (`:471`), `Plat` / `WIN` / `MAC` (`:4188-4207`), `installer`, `jobs_left`, `Machine`, `Ev` (`:4240-4352`), `Build`, `scripted`, `entries`, `result_doc`, `all_done`, `done_after` (`:4601-4657`), `EventElevation` (`:4659`), `host` (`:445`), `code_of` (`:452`), `moduleinfo_for` (`:98`), `NOW`, `FEED_ROUTE`, `V12`, `V13`, `ALPHA`, `BETA` (`:39-45`), and every field, method and type their signatures name, until `private_interfaces` / `private_bounds` are quiet. No body changes | PD-1 puts `ops_after` in `install_tests.rs`, which T003 does not own in G1; one owner per file; `pub(super)` on items of `install::tests` is visible to the sibling `install::batch` (both are descendants of `install`) | no |
| TA-5 | **T001 admits `install_batch_tests.rs` to the two seam rules of `fixture_seam_findings`**: `with_fixture_url` (allowed today only in `download.rs`, `download_tests.rs`, `install_tests.rs`, `injection.rs:3951`) and `request_seam` (free only in `install_tests.rs`, `:4020`), via a new `INSTALL_BATCH_TESTS_RS` constant; the failure texts name both test files. T007 then extends the `request_seam` rule to `stage_seam` (PD-6 (3b)) | the batch tests set `env.request_seam` and may build fixture URLs; without the admission T003's file trips an existing guard the moment it uses them (the plan widens only the `request_seam` arm, Defect D-1) | no |
| TA-6 | **`platform::GRAPHICS_ENV` and `PLATFORM_LITERALS` 40 → 42 are T001's** (the two strings under a `// Phase 12 FR-045` comment, `injection.rs:51-93`); `graphics_env` / `apply_graphics_env` are T001 placeholders with bare `todo!()` bodies (Rule 4 (b)); T006 owns the bodies | `lib.rs` (T001) calls `apply_graphics_env`; the literals and the guard pin must land together or the G1 gate is red on `core_sources_hold_no_production_literal` (SI-6) | no |
| TA-7 | **All of `src-tauri` is T001's, real** (`commands.rs` `update_all`, `settings_view`, `settings_save` per plan §4.6; `install_plugin` / `remove_plugin` through `settings::command_context` per PD-11; `lib.rs` the PD-12 graphics block and 13 handlers; `build.rs`; `capabilities/main.json`) together with every `config_guard.rs` pin they trip (13-command `HANDLER_LIST`, build.rs string, capability list, the `UPDATE_ALL_TYPES` / `SETTINGS_SAVE_TYPES` / `settings_view` parameter rules, messages naming "Phase 12 FR-061"). The files under `src-tauri/permissions/autogenerated/` are build outputs, ignored by `apps/krate-manager/.gitignore:4` (`git check-ignore -v`, this session); `tauri-build` regenerates them during L-APP and no task writes them | `config_guard.rs` checks `lib.rs`, `build.rs`, the capability and `commands.rs` against each other (`config_guard.rs:186-189`, `:576-609`, `:784-860`); split, the G1 gate is red; `src-tauri` has no test target, so its proof is `config_guard.rs` (T001) and T007's scans | no |
| TA-8 | **`view.ts` is T001's, real**: `OrphanView`, `StatusView.orphans`, `BatchProgress`, `BatchSummary`, `BatchItemView`, `SettingsView` and the parsers `toBatchProgress`, `toBatchSummary`, `toSettingsView` (exact key sets through `object`, `view.ts:130-136`; `BatchItemView` with both or neither of `summary` / `error` → `TypeError`), plus `toStatusView`'s `orphans` key. Their behavioural tests are T008's and T009's vitest cases (`update_all_malformed`, `orphan_card_renders_remove`, `settings_section_cases`), whose red-first is the UI that does not yet call them | both UI tasks import them; `toStatusView`'s key must change with the 13 status goldens (SI-9) | no |
| TA-9 | **`StatusSection` gains `readonly lastOs: OsName \| null` and `refresh(): Promise<void>` in T001** (PD-14; `refresh` is the existing `host.refresh`, `status.ts:562-570`; `lastOs` set by `render`) | T009's `main.ts` compiles against them while T008 owns `status.ts` | no |
| TA-10 | **`root-index.expected.html` is produced with the unchanged Python in WSL.** Windows has no Python (`python3 --version` / `python --version` / `py --version` this session: "Python was not found" / "No installed Python found!"); `wsl -e python3 --version` → `Python 3.12.3`. T011 extracts the block between `python3 << 'PYEOF'` and `PYEOF` of `release.yml` at `7c4b6837` (`:799-847`), dedents it, and runs it with `REPO=o/r` inside a throwaway git repo (in WSL's `/tmp`) holding the fixture plugin tree, tags and an LF copy of `docs/index-template.html`; the script and the repo stay outside the repository (Node-only rule for committed code). The `docs.yml` block is the same Python byte for byte after dedent (`diff` of `release.yml:798-847` and `docs.yml:133-179` with leading spaces stripped: only `release.yml`'s two comment lines differ, this session), so one fixture pins both callers | plan §7.1 says "on this box"; the box has Python only in WSL (Defect D-6) | no |
| TA-11 | **Plugin order is the Python's: the relative paths `plugins/<dir>/version.json` sorted by code unit**, not the dir names. They differ when one dir name is a prefix of another followed by a character below `/` (`plugins/delta-x/version.json` < `plugins/delta/version.json`, while `delta` < `delta-x`); the fixture tree has `delta` and `delta-x` to pin it | PD-16 says "plugin dirs … sorted by code unit (ASCII names: equal to Python sorted)", which is false in that case (Defect D-3) | no |
| TA-12 | **`gen-root-index.test.js` uses fixture templates only** (`fixtures/landing/index-template.html`, an LF copy of `docs/index-template.html` at `7c4b6837`, and `fixtures/landing/manager-card.fixture.html`); one subtest asserts the live `docs/index-template.html`, `\r\n` → `\n`, equals the fixture copy (FR-081: the template does not change). The real card's `href` set and statements are pinned by T010's `manager-first-launch.test.js` (SC-016a) | the real card template is T010's file; a test of T011 reading it would be a cross-task dependency inside G2 | no |
| TA-13 | **The preflight tests write their own `RELEASING.md` and `Cargo.lock`** into each fixture app dir (a `## RustSec` table whose row is dated the run date and names the real lock's three versions unless a case mutates it), never a copy of the real `RELEASING.md` | PD-19 says "copies a valid `RELEASING.md`", which would tie every earlier preflight case to T010's file and to the real row's age (Defect D-12) | no |
| TA-14 | **The 0.1.0 RustSec row is written before the U-1 dry run, by the main loop.** T010 writes `RELEASING.md` with the `## RustSec` section and the table header but no row; the main loop runs L-RUSTSEC (T015's script) on the day it asks the user for the readiness `release-manager.yml` dry run, and pastes the printed row (that day's UTC date) into the table. G5 re-runs it if the ledger closes more than 7 days later | FR-073 needs a green `release-manager.yml` dry run at the readiness commit, and its `preflight` refuses `E_PREFLIGHT_RUSTSEC` without a row at most 7 days old (FR-104); the plan does not say when the row is written (Defect D-10); a network reading is a main-loop step | no |
| TA-15 | **`feed-tools.yml`'s `PLAN` and `NAMES` are set by T016 from a local TAP run** after every new node test exists (PD-27). Expected: 64 + 5 = 69 top-level tests (`gen-root-index.test.js`, `manager-first-launch.test.js`, `manager-readiness-counts.test.js`, the preflight's "manager release preflight enforces the RustSec record", the workflow-shape "landing pages render with gen-root-index.js"; `release-manager-shape.test.js` adds subtests only, PD-28); the measured count wins | pinned values must follow the tests, never be hand-added | no |
| TA-16 | **The ledger layout is fixed here, for T012 and T013 alike**: headings `## Counts`, `## PENDING-CI rows`, `## Gates`, `## Backfill and live feed`, `## RustSec`, `## Asset host`, `## Post-release rows`; the two row tables have exactly the columns `\| id \| job and assertion \| closing workflow \| run URL \| commit \| verdict \|`; ids `phase<N>:<row id>` (and `phase11:SC-014:publish`); a literal `\|` inside a cell is written `\|` and the parser splits on unescaped pipes only; an empty cell is empty text; verdicts `green`, `post-release` or empty (open) | PD-26 fixes the columns and ids but not the headings or the escaping, and T012 (parser) and T013 (writer) run side by side | no |
| TA-17 | **`header-consumers.js` needs `--tag`** (`tools/header-consumers.js:22-25`: without it, the usage line and exit 2). T017 runs `node tools/header-consumers.js --tag '[krate-manager]'` and records the last line `CONSUMERS OUTSIDE [krate-manager]: none` | plan §7.4 / FR-111 call it bare (Defect D-2) | spec: FR-111 |
| TA-18 | **`update_all_excludes_manager_line` is a `main.test.ts` case (T009)**: it needs the manager update line (`main.ts` / `manager_update.ts`) and the status section in one shell (`renderShell`, `main.ts:6-44`); it is T009's one in-group expected red until T008's Update-all control exists | plan §7.1 SC-030 names no file; the case cannot be written over one section | no |
| TA-19 | **`settings_root_change_rereads_status` is a `settings.test.ts` case (T009)**: a save whose returned `linux_plugin_root` differs from the rendered one calls `onRootChanged` exactly once; a toggle-only save calls it zero times. `main_rereads_status_after_root_change` (PD-14) proves the wiring to one `plugin_status` invoke | the two names of plan §7.1 (SC-027, SC-011 rows) would otherwise test the same thing twice | no |
| TA-20 | **FR-090 (c) (the Phase 12 status line) is not a task**: the workflow writes it after compliance (as Phase 11's TA-15) | a task-written status line would be replaced anyway | no |

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | **sequential** (1) | T001: every shared file and every cross-task surface (see "How to read this file") | Preconditions |
| G2 Code, UI, docs, tools, ledger, evidence | **[P]** (14) | T002 spec + roadmap · T003 core install: batch, split helpers, orphan Remove · T004 core settings command and root gate · T005 core status orphans · T006 core graphics env · T007 guards (`injection.rs`) · T008 UI: Update all and orphan cards · T009 UI: settings section and main · T010 docs: statements, README, RELEASING, CHANGELOG, card, user-checks, first-launch test · T011 `gen-root-index.js` · T012 `manager-readiness-counts.js` · T013 readiness ledger · T014 preflight RustSec · T015 evidence scripts | G1 |
| G3 Integration | **sequential** (2, in order) | T016 workflows (`release.yml`, `docs.yml`, `release-manager.yml`, `feed-tools.yml`) + their shape tests + `hygiene.test.js` + TAP pins · T017 integration checks, INDEX, PENDING-CI list, hand-over | G2 |
| — Fence U-1 | user | FR-103 recorded, then FR-074 (a)-(c) and (f) recorded in `user-checks.md` (and the readiness dry run, TA-14) | G3 + L-CLOSE green |
| G5 Readiness close | **sequential** (1, main loop) | T020 the ledger closed against the user's final runs | U-1 + the user's final CI runs |
| G4 Post-release (R-POST-4) | **[P]** (2) | T018 macOS first-launch text · T019 release body | both FR-073 rows recorded, after the first real dispatch |

**File ownership in G2 (each file has exactly one G2 writer).** T002: `specs/krate-manager-phase12/spec.md`,
`specs/Krate-manager-roadmap.md`. T003: `crates/core/src/install.rs`, `install_batch_tests.rs`, `install_tests.rs`,
`crates/core/src/download.rs`. T004: `crates/core/src/settings.rs`, `settings_tests.rs`, `context.rs`. T005:
`crates/core/src/status.rs`, `status_tests.rs`. T006: `crates/core/src/platform/mod.rs`, `platform/mod_tests.rs`. T007:
`crates/core/tests/injection.rs`. T008: `src/status.ts`, `src/status.test.ts`. T009: `src/settings.ts` (new),
`src/settings.test.ts` (new), `src/main.ts`, `src/main.test.ts`. T010: `tools/feed-contract/first-launch-statements.json`
(new), `README.md` (new), `RELEASING.md` (new), `CHANGELOG.md`, `docs/manager-card-template.html` (new),
`specs/krate-manager-phase12/user-checks.md` (new), `tools/feed-contract/tests/manager-first-launch.test.js` (new),
`tools/feed-contract/fixtures/first-launch/**` (new). T011: `tools/gen-root-index.js` (new),
`tools/feed-contract/tests/gen-root-index.test.js` (new), `tools/feed-contract/fixtures/landing/{plugins/**,
index-template.html, manager-card.fixture.html, root-index.expected.html}` (new),
`specs/krate-manager-phase12/evidence/root-index-python.txt` (new). T012: `tools/manager-readiness-counts.js` (new),
`tools/feed-contract/tests/manager-readiness-counts.test.js` (new), `tools/feed-contract/fixtures/landing/readiness/**`
(new). T013: `specs/krate-manager-phase12/readiness.md` (new). T014: `tools/manager-release-preflight.js`,
`tools/feed-contract/tests/manager-release-preflight.test.js`. T015:
`specs/krate-manager-phase12/evidence/{scope-check,wsl-lane,close-lane,rustsec-check}.js` (new).

**Why this shape.** T001 is the only task that edits shared files, so it is a group of its own; it writes the one `mod
batch` line, every signature a second task compiles against and the four small pieces two G2 tasks share (TA-2, TA-3,
TA-8, TA-9), so fourteen G2 tasks run side by side and none adds a line to a shared file. The core is cut along its
files: `install.rs` has one writer (T003: the batch and the orphan branch both live there, and the guards keyed to
`install.rs`, `INSTALL_FS_EXTRA` / `INSTALL_IO_METHODS` / `INSTALL_BANNED` and the `request_seam` rule, `injection.rs:468-512`,
`:3988-4040`, forbid moving either into another file); settings, status, graphics and guards each have their own files.
The UI is cut into the status section (T008) and the settings section with its mount (T009). Docs, the two new tools,
the ledger, the preflight change and the lane scripts are new or single-writer files. G3 is two dependent tasks: T016
edits the four shared workflows and two shared node tests whose pinned values depend on G2's files, and T017's checks
read the whole tree after it. No group is a chain of independent tasks.

**SC owners:** SC-002 to SC-010, SC-014, SC-027 core rows, SC-031 install row, FR-019 proof T003 · SC-011 core rows,
PD-11 gate T004 · SC-015 core row, SC-031 status row T005 · SC-012 T006 · SC-007, SC-013, SC-030 scan, PD-11 scan,
PD-6 (3b) seam rule T007 · SC-001, SC-005 / SC-006 / SC-010 / SC-015 / SC-027 / SC-030 vitest rows T008 · SC-011
vitest rows, PD-14, SC-030 `update_all_excludes_manager_line` T009 · SC-016a, SC-016b reader, FR-072, FR-080,
FR-082, FR-084 pre-U-1, FR-104 section T010 · SC-029 generator T011 · SC-020 / SC-028 tool T012 · FR-100 ledger
skeleton T013 · SC-021 tool cases T014 · lanes T015 · SC-023 config pins, SI-2 helper test, TA-2, TA-3 T001 · SC-029
shapes, SC-032, FR-085, FR-086 T016 · SC-023 diffs, SC-024 local, SC-025, SC-026 check T017 · SC-016b green,
SC-017, SC-018 cite T018 / T019 · SC-020, SC-021 ledger, SC-022, SC-028 real ledger T020 · PENDING-CI list at the end.

---

## G1 — Foundation (sequential: one task, every shared file)

### T001 — Precondition, the cross-task surfaces and shared pieces, `src-tauri`, the guard pins, the required-test lists, `.gitattributes` (spec FR-001 types, FR-002 cap (SI-2), FR-045 surface, FR-060, FR-061, FR-071; plan §2, §4.1 surface, §4.4 surface, §4.5, §4.6, PD-5, PD-6 (3b) field, PD-7, PD-11 call form, PD-12, PD-13 field, PD-21, PD-27; SI-2, SI-6, SI-9, SI-20; TA-1 to TA-9)

Target: the shared `apps/krate-manager/target`, `CARGO_BUILD_JOBS=16`. Logs under `F:/tmp/gate/km12-T001/`.

**Files (edit):** `crates/core/src/install.rs`, `crates/core/src/install_tests.rs`, `crates/core/src/test_support.rs`,
`crates/core/src/fixture_server.rs`, `crates/core/src/fixture_server_tests.rs`, `crates/core/src/status.rs`, the 13
files `tests/fixtures/status/*.json`, `crates/core/src/settings.rs`, `crates/core/src/platform/mod.rs`,
`crates/core/tests/injection.rs`, `crates/core/tests/config_guard.rs`, `crates/helper/src/job_tests.rs`,
`src-tauri/src/commands.rs`, `src-tauri/src/lib.rs`, `src-tauri/build.rs`, `src-tauri/capabilities/main.json`,
`src/view.ts`, `src/status.ts`, `tests/required-tests.json`, `../../tools/feed-contract/tests/manager-ci-shape.test.js`,
`../../.gitattributes`. **(new):** `crates/core/src/install_batch_tests.rs`.

0. **Precondition check** (Preconditions). Then `cargo test --workspace --locked` once, summary lines quoted (the
   pre-change baseline every refactor below is held to).
1. **SI-2 cap, red first.** In `crates/helper/src/job_tests.rs` add `#[test] fn core_and_helper_op_caps_agree()`:
   `assert_eq!(krate_manager_core::install::job::MAX_OPS, crate::job::MAX_OPS); assert_eq!(crate::job::MAX_OPS, 32);`
   (the helper dev-depends on core with `test-support`, `crates/helper/Cargo.toml` `[dev-dependencies]`). Red: the build
   error naming `MAX_OPS` in `install::job`. Then in core's `pub mod job` (`install.rs:816`) add
   `/// FR-002 (SI-2): the helper's op cap (crates/helper/src/job.rs:11); one job, so one batch, holds at most this many ops.`
   `pub const MAX_OPS: usize = 32;`. Green: `cargo test -p krate-manager-helper --locked --lib job::`.
2. **Batch surface (PD-5, plan §4.1), placeholders owned by T003.** In `install.rs` beside `ActionSummary`: `BatchItem`
   (exactly PD-5's derives and serde attributes), `BatchPolicy` + `BatchPolicy::PRODUCTION`, `pub const MAX_BATCH_ITEMS:
   usize = job::MAX_OPS;`, `BatchProgress` (PD-5 derives, `#[serde(tag = "kind", rename_all = "snake_case")]`, variants
   `Item { name, event: InstallProgress }`, `Elevating`), `BatchItemResult`, `BatchSummary`, `BatchSummaryView`,
   `BatchItemView` (both `Serialize`, plain field names), `impl BatchSummary { pub fn view(&self) -> BatchSummaryView }`
   (placeholder) and `pub fn batch_for_command(…)` with plan §4.1's exact signature (placeholder, Rule 7, `todo!("T003")`).
   Real: `pub fn batch_channel_progress<E>(mut send: impl FnMut(BatchProgress) -> Result<(), E>) -> impl FnMut(BatchProgress)`,
   a twin of `channel_progress` (`install.rs:1424-1432`) with the same doc form.
3. **Stage seam field (PD-6 (3b)).** In `CommandEnv` after `request_seam` (`install.rs:543-545`):
   `#[cfg(test)] pub(crate) stage_seam: Option<&'a dyn Fn(BatchStage)>,` with a doc line; `stage_seam: None` beside
   `request_seam: None` in `native_env` (`:568`) and, as `#[cfg(test)] stage_seam: None,`, in the rig's
   `command_env_with` (`test_support.rs:3313`); `#[cfg(test)] #[derive(Clone, Copy, Debug, PartialEq, Eq)] pub(crate)
   enum BatchStage { ChecksDone, DownloadsDone }`. No other `CommandEnv` literal exists (`grep -n "request_seam: None"`
   → `install.rs:568`, `test_support.rs:3313`, this session).
4. **`manifest_heads` real (TA-2), red first.** Write `install::tests::manifest_heads_cases` in `install_tests.rs`
   (TA-2's rows, over `FsInstallIo` and `fresh_temp_roots()`); red on the missing item; then add PD-7's
   `MAX_MANIFEST_ENTRIES`, `ManifestHead`, `manifest_heads` to `install.rs` exactly as PD-7 reads.
5. **TA-4 visibility** in `install_tests.rs` (no body edits); then the batch mount at the end of `install.rs`, before
   `mod tests` (`:4305`): `#[cfg(test)] #[path = "install_batch_tests.rs"] mod batch;`, and `install_batch_tests.rs`
   with only `//! install::batch (T003): the Update-all cases (plan §7.1 SC-002 to SC-010, SC-027).` (no `use`, so no
   unused-import warning).
6. **`Reply::Held` real (TA-3), red first.** `fixture_server::tests::held_reply_releases` in `fixture_server_tests.rs`;
   then PD-21 in `fixture_server.rs` (each wait in `POLL` slices that also read the stop flag, `fixture_server.rs:78`,
   `:100-113`; `Drop` releases every hold).
7. **`StatusView.orphans` (PD-13 field, SI-9).** `pub orphans: Vec<OrphanView>` as `StatusView`'s last field
   (`status.rs:24-31`) and `OrphanView` (PD-13 derives); every `StatusView { … }` literal (`status.rs:341` and any
   other) gets `orphans: Vec::new()` (T005 fills it in `build_with_os`); each of the 13 goldens gains `"orphans": []`
   as its last key in the existing formatting (`status_tests.rs:5-6`, "which no test writes": by hand, matching the
   file's indentation and EOL); `view.ts` `toStatusView` admits and parses the key (step 10).
8. **Settings surface (plan §4.4, PD-10, PD-11), placeholders owned by T004.** In `settings.rs`: `SettingsView` (plan
   §4.4 fields, `Serialize`, `os: status::OsName`), `SettingsCommandError` real (PD-10 variants and `#[error]` texts,
   `pub fn code(&self) -> &'static str`), `view`, `view_with_os`, `save_for_command`, `save_for_command_with_os`,
   `#[must_use] pub struct RootTicket { state_dir: PathBuf }` (no `Drop` yet), `pub fn command_context(state:
   &SharedState) -> Result<(RootTicket, AppContext), StartupError>`, and `#[cfg(any(test, feature = "test-support"))]
   pub fn hold_save_gate_for_test(state_dir: &Path) -> SaveGateHeldForTest` with its unit `pub struct
   SaveGateHeldForTest;`, each body a Rule 7 placeholder `todo!("T004")`.
9. **Graphics surface (TA-6).** In `platform/mod.rs`: `pub const GRAPHICS_ENV: (&str, &str) =
   ("WEBKIT_DISABLE_DMABUF_RENDERER", "1");` and plan §4.5's two signatures with bare `todo!()` bodies (Rule 7); in
   `injection.rs` `PLATFORM_LITERALS` becomes `[&str; 42]` with the two strings under `// Phase 12 FR-045`.
10. **`view.ts` (TA-8) and `StatusSection` (TA-9)**, real, per plan §4.7 first paragraph and PD-14.
11. **`src-tauri` and its pins (TA-7).** `commands.rs`: `update_all`, `settings_view`, `settings_save` exactly as plan
    §4.6 (`update_all` follows `install_plugin`, `commands.rs:110-168`, line for line: `let (ticket, ctx) =
    settings::command_context(&state).map_err(|e| e.to_string())?;`, `spawn_blocking` whose closure starts with
    `let _ticket = ticket;`, `&FsStagingIo` as `staging`, `&BatchPolicy::PRODUCTION`,
    `install::batch_channel_progress(|e| on_progress.send(e))`, `Ok(summary.view())`, join error as
    `DownloadError::Task`); the two settings commands `async fn` calling the core inline (SI-20); `install_plugin` and
    `remove_plugin` replace their `let ctx = { … guard.context.clone() … };` block with the same `command_context` line
    and move `ticket` into their closure. `lib.rs`: PD-12's two `#[cfg(target_os = "linux")]` statements between
    `let sink_roots = …;` (`lib.rs:48`) and `let inputs = proxy_env::read();` (`:58`), the `// SAFETY:` comment exactly
    "single-threaded: before the first thread (sysproxy.rs:210 can spawn one from :61)", and `generate_handler!` with the
    ten existing commands then `commands::update_all, commands::settings_view, commands::settings_save`. `build.rs` the
    same 13 names; `capabilities/main.json` the ten permissions then `allow-update-all`, `allow-settings-view`,
    `allow-settings-save`, nothing else changed. `config_guard.rs`: `HANDLER_LIST` (`:186`), the capability list
    (`:798-809`), the build.rs string (`:840`) and their messages become the 13; new `UPDATE_ALL_TYPES` (admits
    `tauri::State<…>` / `State<…>`, `Vec<BatchItem>`, `tauri::ipc::Channel<BatchProgress>` / `Channel<BatchProgress>`),
    `SETTINGS_SAVE_TYPES` (`State<…>`, `Option<String>`, `bool`) and `settings_view` (`State<…>` only), each checked like
    `check_install_command_params` (`:576-609`) with failure messages naming "Phase 12 FR-061"; `INSTALL_COMMANDS` stays
    the two; every existing plant still fails (the planted extra permission among them). Then L-APP (it regenerates
    the ignored permission files).
12. **TA-5 seam admission** in `injection.rs` (`INSTALL_BATCH_TESTS_RS = "crates/core/src/install_batch_tests.rs"`,
    admitted where `INSTALL_TESTS_RS` is at `:3951` and `:4020`).
13. **Required tests (TA-1).** Append to `tests/required-tests.json` `cargo.all`, in this order, and to
    `EXPECTED_REQUIRED.cargo.all` identically: `install::batch::{one_launch_windows, one_launch_macos, no_launch_linux,
    download_bound, production_bound_is_three, installs_after_downloads, locked_item_continues, notice_cases,
    download_failure_excluded, staging_full_excluded, state_changed_items, busy_items, input_checks, refused_prompt,
    elevation_busy, registrar_kill_once, progress_mapping, log_lines, batch_level_errors}` (19, each as its full path),
    `install::tests::orphan_remove_cases`, `install::tests::not_manageable_is_not_orphan`,
    `install::tests::manifest_heads_cases`, `fixture_server::tests::held_reply_releases`,
    `settings::tests::save_for_command_cases`, `settings::tests::view_cases`, `settings::tests::root_gate_cases`,
    `platform::tests::graphics_env_cases`, `platform::tests::apply_graphics_env_cases`, `status::tests::orphans_cases`,
    `no_process_enumeration`, `one_set_var_site_before_threads`, `no_cancel_setter`,
    `plugin_commands_take_the_root_ticket`, `job::tests::core_and_helper_op_caps_agree` (34). Append to `vitest` and
    `EXPECTED_REQUIRED.vitest`: `update_all_confirmation_lists_every_update`, `update_all_locked_text`,
    `update_all_macos_notice`, `update_all_linux_notice`, `update_all_disables_actions`, `orphan_card_renders_remove`,
    `update_all_progress_lines`, `update_all_malformed`, `update_all_settle_rereads_status`,
    `update_all_no_cancel_control`, `settings_section_cases`, `settings_root_change_rereads_status`,
    `main_mounts_settings_on_linux`, `main_skips_settings_on_macos`, `main_rereads_status_after_root_change`,
    `update_all_excludes_manager_line` (16). Before the edit, confirm none of the 50 names is already present
    (`node -e` over the file).
14. **`.gitattributes`** (CRLF like the file): after line 25 add
    `# A shebang tool whose test reads it as LF, like gen-manager-latest.js above (Krate Manager Phase 12, PD-16).` and
    `tools/gen-root-index.js         text eol=lf`. No other line changes.

**Red first:** steps 1, 4, 6 as stated. Steps 2, 3, 5, 7-12 add surfaces and pins: their proof is the G1 lanes green
with every existing test unchanged in name and result (compare step 0's summary lines); step 7 is the one existing-test
change (the 13 goldens, by hand).

**Lane:** L-RS, L-APP, L-DEVFEED, L-MANUAL, L-E2E, L-GUARD, L-TS (vitest stays 70 titles, all green), L-NODE
(`manager-ci-shape.test.js` green with the new `EXPECTED_REQUIRED`), L-VLINT. **Done when** all are green and
`grep -c 'todo!(' ` over `install.rs`, `settings.rs`, `platform/mod.rs` gives exactly the placeholders listed here
(install 2: `batch_for_command`, `BatchSummary::view`; settings 6: `view`, `view_with_os`, `save_for_command`,
`save_for_command_with_os`, `command_context`, `hold_save_gate_for_test`; platform 2), quoted in the notes.

---

## G2 — Code, UI, docs, tools, ledger, evidence [P] (fourteen tasks, disjoint files)

### T002 [P] — Spec and roadmap amendments (plan §0.2 SI-1 to SI-22; spec FR-090 (a), (b), (d), (e), (f), (g); TA rows marked spec)

**Files:** `specs/krate-manager-phase12/spec.md`, `specs/Krate-manager-roadmap.md`.

0. Precondition check (Preconditions).
1. **Spec:** write each SI-1 to SI-22 edit at the FR / SC / edge-case row it names, tagged `(plan SI-n)`, and TA-17's
   FR-111 sentence tagged `(tasks TA-17)`; in particular FR-002 "at most 32 entries" and SC-027 "33 items" (SI-2),
   FR-081 / FR-086 / SC-029 / FR-090 (a), (g) naming `docs.yml` as the third caller (SI-1, SI-16), FR-085 / SC-029
   `needs: [preflight, feed]` and `needs: [preflight, feed-dry]` (SI-3), the "A feed change between status and
   Confirm" edge row and SC-009 with `E_DOWNLOAD_VERSION_CHANGED` / `E_INSTALL_NOT_NEWER` (SI-4), the "Self-update
   starts during a batch" row reworded to core's `E_SELF_UPDATE_BUSY` refusal (SI-7), SC-011 "under `cargo.all`"
   (SI-18), FR-060 "`update_all` on `spawn_blocking`; the two settings commands `async`, calling the core inline"
   (SI-20), FR-042 naming the root gate (SI-21), SC-009's `BatchStage::ChecksDone` seam (SI-22), SC-029's fixture path
   `tools/feed-contract/fixtures/landing/root-index.expected.html` (SI-14). Bump the header to revision 4 and add one
   Review-notes bullet listing the SI ids applied. No FR / SC is removed or renumbered.
2. **Roadmap:** exactly the FR-090 hunks (a), (b), (d), (e), (f), (g) as amended by SI-1 (plan §4.11), each at the line
   the FR names (lines 200, 926-927, 932, 921-922, the Phase 12 user-checks list); (c) is the workflow's (TA-20).
   `git diff --stat specs/Krate-manager-roadmap.md` touches only those lines; after (a), (f), (g), neither line 200 nor
   line 927 contains "not in manager scope" or "no other landing change" (SC-026).

**Red first:** not applicable (documents). **Check:** `grep -c "plan SI-" spec.md` = the number of SI rows applied (22
or more); the SC-026 greps above; `git diff -U0 specs/Krate-manager-roadmap.md` read hunk by hunk into the notes.
**Lane:** L-INDEX is not run here (T017 regenerates INDEX).

### T003 [P] — Core install: the split helpers, orphan Remove, `batch_for_command` (spec FR-001 to FR-020, FR-050, FR-051, FR-054; SC-002 to SC-010, SC-014, SC-027 core, SC-031 install row; plan §4.1 to §4.3, PD-2 to PD-6, PD-8, PD-20; SI-2, SI-4, SI-11, SI-12, SI-22; TA-4)

**Files:** `crates/core/src/install.rs`, `crates/core/src/install_batch_tests.rs`, `crates/core/src/install_tests.rs`,
`crates/core/src/download.rs`. Working loop L-OWN with `C:/km12-target/T003` (Rule 10). Rules 1, 3, 4 (c), (d), 7.

1. **Refactors first (no behaviour change).** PD-2 (a) `download::request_in_feed` and `command_request_for` =
   `feed::check` + `request_in_feed`; PD-2 (b) `install::check_item` / `Checked` without its `feed` field, called by
   `install_command`; PD-8's `run_uninstall` → `run_uninstall_at` split. Proof: `cargo test -p krate-manager-core
   --locked --lib install::` and `download::` green before and after, summary lines quoted (FR-019, SC-024).
2. **Orphan Remove, red first.** In `install_tests.rs` write `install::tests::orphan_remove_cases` (plan §7.1 SC-014
   row and spec SC-014: the manifest-only Remove with a user preset, an unplaced file and an edited placed preset kept
   and reported; the same name in the feed → the Phase 9 path; two manifests → `E_INSTALL_MANIFEST: two manifests name
   this plugin`; a file stem differing from its bundle → `E_INSTALL_MANIFEST: the manifest file name is not its
   bundle's`; 257 entries → `E_INSTALL_MANIFEST: too many manifests`, 256 → the normal path; roots changed →
   `E_INSTALL_ROOT_CHANGED`; a held busy key → `E_INSTALL_BUSY`; a pending journal → `E_INSTALL_JOURNAL_PENDING`;
   `Os::Windows` forced → `E_INSTALL_UNKNOWN_PLUGIN`; every refusal leaves every file present; zero asset requests in
   every case, `Fx::counts()`, `install_tests.rs:575-580`) and `install::tests::not_manageable_is_not_orphan` (SC-031:
   a snapshot entry without `install` plus a manifest → Remove and Update `E_INSTALL_NOT_MANAGEABLE`, nothing removed,
   `manifest_heads` not consulted: assert through a `RecordingInstallIo` that no `read_dir` of `<state>/manifests` was
   made). Red: `E_INSTALL_UNKNOWN_PLUGIN` where an orphan Remove is expected. Implement plan §4.3 / PD-8 (heads through
   T001's `manifest_heads`; the summary as `command_remove` builds it, `install.rs:4245-4251`).
3. **Batch, red first.** In `install_batch_tests.rs` write every `install::batch::*` case of plan §7.1 (SC-002 to
   SC-010, SC-027 rows, with spec SC-002 to SC-010 and SC-027's thresholds, SI-2's 33 items, SI-4's codes, SI-22's seam
   sampling): `one_launch_windows`, `one_launch_macos`, `no_launch_linux` (N ∈ {1, 3, 9}, 3), `download_bound` (5 items,
   bound 3 → peak 3, bound 1 → peak 1, `requests()` per asset path = 1, a 30 s watchdog thread that `release_all()`s and
   fails), `production_bound_is_three` (`BatchPolicy::PRODUCTION.parallel_downloads == 3`; a `BatchItem` JSON with a
   third key `parallelDownloads` fails to deserialize, `deny_unknown_fields`), `installs_after_downloads`,
   `locked_item_continues`, `notice_cases`, `download_failure_excluded`, `staging_full_excluded`,
   `state_changed_items`, `busy_items`, `input_checks`, `refused_prompt`, `elevation_busy` (Phase 10's
   `ElevationGuard::hold_for_test`, `install.rs:653`), `registrar_kill_once` (`FakeHostIo.calls`: exactly one `run
   /usr/bin/killall -9 AudioComponentRegistrar` for 3 done, zero for all locked), `progress_mapping`, `log_lines`
   (`install_capture` / `take_mine`, `test_support.rs:267-282`; one FR-020 line per item, the batch line
   `batch of <n>: <accepted> accepted, <verified> verified, <ok> updated`, the elevated driver's `elevated batch of
   <n> op(s)` once on Windows / macOS (SI-12); no line holds the temp root, a fixture URL or an artifact's hex
   SHA-256), `batch_level_errors`. Fixtures: the Phase 10 rig made visible by T001 (`Ev`, `Machine`, `Plat`, `WIN`,
   `MAC`, `done_after`, `entries`, `result_doc`; TA-4) and `ops_after` written here (PD-20); every asset served by the
   fixture server and signed by a throwaway `TestSigner`; every `done` effect writes the new-version bundle and record
   (`Machine::install`), never before stage 1 (spec "Scripted helper effects"). Red: `not yet implemented: T003`.
   Implement PD-3 to PD-6 and plan §4.1 (the `InstallError::Batch` variant and its `code()` arm,
   `install.rs:212-252`; `BatchSummary::view`; the `stage_seam` calls at (3b) and (4b), each under `#[cfg(test)]`).

**Lane:** L-OWN (`install::`, `download::`, `--test injection`); then L-RS with the shared target once at the end.
**Done when** the 21 required names of this task pass, every pre-existing `install::tests::*`, `install::loopback::*`,
`download::tests::*` case passes unchanged, and `cargo clippy -p krate-manager-core --all-targets` is quiet.

### T004 [P] — Core settings command and the root gate (spec FR-040 to FR-044; SC-011 core rows; plan §4.4, PD-9 to PD-11; SI-8 (a), SI-15, SI-17, SI-21)

**Files:** `crates/core/src/settings.rs`, `crates/core/src/settings_tests.rs`, `crates/core/src/context.rs`. L-OWN with
`C:/km12-target/T004`.

**Red first:** in `settings_tests.rs` write `settings::tests::save_for_command_cases`, `settings::tests::view_cases` and
`settings::tests::root_gate_cases` exactly as plan §7.1 rows SC-011 and PD-11 (SI-21) and spec SC-011 (every root built
from the temp dir; rejections with host-appropriate values: `rel/vst3`, `temp.join("..").join("x")`,
`temp.ancestors().last()`, empty; expected file bytes those of `written_bytes_are_d10`, `settings_tests.rs:133`; busy
through `install::hold_busy_for_test` (`install.rs:3934`), in flight through `download::hold_in_flight_for_test`
(`download.rs:691`) on `<staging>/downloads`, self-update through `self_update::InFlightGuard::enter(Some(state_dir))`
(`self_update.rs:375`); the root gate's held install through T001's `Reply::Held`; every refusal leaves the file bytes
and the context's configured root unchanged). Red: `not yet implemented: T004`.

**Implement:** PD-9 (`context::apply_settings_with_os` under one write guard; `apply_settings` its native wrapper, doc
line as PD-9), PD-10, PD-11 (the gate, `RootTicket` with `Drop`, `command_context`, the save's 11 steps in order,
`hold_save_gate_for_test` as `install::hold_busy_for_test` is built), plan §4.4 (`view_with_os` fields; `root_locked_by`
from T001's `manifest_heads` plus `<state>/journal/*.json`, sorted, de-duplicated).

**Lane:** L-OWN (`settings::`, `context::`); L-RS at the end. **Done when** the three names pass and every existing
`settings::tests::*` and `context::tests::*` case passes unchanged (notably `context::tests::release_for_restart_frees_the_lock`).

### T005 [P] — Core status orphans (spec FR-052, FR-054 status row; SC-015 core, SC-031 status row; plan PD-13)

**Files:** `crates/core/src/status.rs`, `crates/core/src/status_tests.rs`. L-OWN with `C:/km12-target/T005`.

**Red first:** `status::tests::orphans_cases` (plan §7.1 SC-015 row; spec SC-015 and SC-031): Linux forced with a cached
feed from the fixture server → orphans sorted by name, each `{ name: plugin, version }`, feed plugins not listed, a
snapshot entry without `install` with a manifest not listed (FR-054); no snapshot → empty; Windows forced → empty; 257
manifest entries → empty and the view still built; a manifest without a string `version` → not listed. Red: the field
is always empty (T001).
**Implement:** PD-13 in `build_with_os` only (`assemble` / `assemble_with_gates` stay pure); any `manifest_heads` error
→ empty. **Lane:** L-OWN (`status::`); L-RS. **Done when** the name passes and the 13 golden comparisons pass unchanged.

### T006 [P] — Core graphics env decision (spec FR-045; SC-012; plan §4.5, PD-12)

**Files:** `crates/core/src/platform/mod.rs`, `crates/core/src/platform/mod_tests.rs`. L-OWN with `C:/km12-target/T006`.

**Red first:** `platform::tests::graphics_env_cases` (`(Linux, on)` → `Some(GRAPHICS_ENV)`; `(Linux, off)`,
`(Windows, on)`, `(Macos, on)` → `None`) and `platform::tests::apply_graphics_env_cases` (a recording closure pushing
`(String, String)`: `(Linux, Some(on))` → exactly `[("WEBKIT_DISABLE_DMABUF_RENDERER", "1")]`; `(Linux, Some(off))`,
`(Linux, None)`, `(Windows, Some(on))`, `(Macos, Some(on))` → `[]`). Red: `not yet implemented`.
**Implement:** plan §4.5's bodies; no new string literal in `platform/mod.rs` (Rule 4 (b)); no filesystem token
(`FS_TOKENS`, `injection.rs:33`). **Lane:** L-OWN (`platform::`, `--test injection`); L-RS.

### T007 [P] — Guards in `injection.rs` (spec FR-018, FR-027, FR-046; SC-007, SC-013, SC-030 scan; plan §7.1 rows SC-007, SC-013, SC-030, PD-11, PD-6 (3b); TA-5 follow-up, Rule 4 (c), (d))

**Files:** `crates/core/tests/injection.rs`. L-OWN with `C:/km12-target/T007` (`--test injection`).

Each case below is written with its plants first, against a finder that does not exist yet (red: the plants are not
reported), then the finder; plants are text edits of in-memory copies of the real files, never of the files.
1. `no_process_enumeration` (SC-007, spec wording exact): case-sensitive whole-token scan of the non-test files
   (`is_scanned`, `injection.rs:1517-1519`) of `crates/core/src`, `crates/helper/src`, `src-tauri/src`; zero of
   `CreateToolhelp32Snapshot`, `Process32First`, `EnumProcesses`, `OpenProcess`, `proc_listpids`, `runningApplications`,
   `sysinfo`, `pgrep`, `pkill`, `tasklist`, `taskkill`, the literal `"ps"`, the substring `/proc/`; exactly one
   `killall`, the literal `"/usr/bin/killall"` at `platform/mod.rs:19`. Plants: a `tasklist` token, a second
   `"/usr/bin/killall"`.
2. `one_set_var_site_before_threads` (SC-013): the lexer (`lex`, `tokenized`) and `close_of` (`injection.rs:1083`) over
   the same files and T001's `src-tauri/src/lib.rs`; exactly one `set_var`, zero `remove_var`, inside the argument list
   of the one `apply_graphics_env (` call, which is preceded by `# [ cfg ( target_os = "linux" ) ]` and whose byte
   offset is below those of `proxy_env :: read (` and `assemble_context (`; no `WEBKIT_DISABLE_DMABUF_RENDERER`
   literal in `src-tauri/src`. Plants: a second site; the call moved after `proxy_env::read(`; an unconditional
   `std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");` statement in `run()`.
3. `no_cancel_setter` (SC-030): no `. cancel (` token sequence in `src-tauri/src` non-test files; no command name
   starting `cancel` in `generate_handler!`, `build.rs` or `capabilities/main.json` (an `allow-cancel*` permission
   included). The webview cannot reach a `CancelToken` except through a command, so no `src/*.ts` rule is added: the
   existing `cancel` identifiers there are the confirmations' dismiss buttons (`status.ts:72`, `:395-397`,
   `manager_update.ts:50`), which FR-027 keeps. Plants: a `cancel.cancel();` line in `commands.rs`; a
   `commands::cancel_batch` handler; an `allow-cancel-batch` permission.
4. `plugin_commands_take_the_root_ticket` (PD-11 scan, plan §4.6): in `src-tauri/src/commands.rs`, the bodies of
   `install_plugin`, `remove_plugin`, `update_all` each hold exactly one `settings :: command_context (` and no
   `context . clone (`. One plant per body restoring the old clone block.
5. **Seam rule (PD-6 (3b)):** the `request_seam` rule of `fixture_seam_findings` (`:3970-4035`) applies to
   `stage_seam` by the same terms, with `install_tests.rs` and `install_batch_tests.rs` the free files; its plant table
   gains one `stage_seam` plant per rule (declared outside `install.rs`; a use outside a `#[cfg(test)]` span in
   `install.rs`; a non-initialiser use in `test_support.rs`; a use in a `src-tauri` file).
6. **Lists:** `PHASE7_TEST_FILES` and `FR088_FILES` gain `INSTALL_BATCH_TESTS_RS` (array lengths 7 and 9).

**Lane:** L-OWN (`--test injection`); L-RS. **Done when** the four names pass, every plant fails as stated, and every
existing `injection.rs` case passes. If item 6 reports a hit in T003's file, that is T003's defect: report it in the
notes, never weaken the list.

### T008 [P] — UI: Update all and orphan cards (spec FR-021 to FR-026, FR-028 (status half), FR-053; SC-001, SC-005, SC-006, SC-010, SC-015, SC-027, SC-030 vitest rows; plan §4.7 `status.ts` paragraph, PD-15)

**Files:** `src/status.ts`, `src/status.test.ts`. Rule 4 (e), (f).

**Red first** (each a vitest `it` with exactly this title, over status fixtures built with `toStatusView` and a fake
`Invoker` recording every call; plan §7.1 rows and spec SC text are binding): `update_all_confirmation_lists_every_update`
(3 `update` cards + 2 others, per `windows` / `macos` / `linux`: one `Update all`; exactly 3 `p.confirm-line` in status
order with `→`; exactly one admin line on Windows / macOS, zero on Linux; Cancel → 0 invokes; Confirm → exactly one,
`update_all`, `items` exactly the three `{ name, expectedVersion }`), `update_all_locked_text`,
`update_all_macos_notice`, `update_all_linux_notice`, `update_all_disables_actions` (every card button, `Check now`
and the control disabled until settle; the control also disabled while a `div.actions.running` row exists, PD-15),
`orphan_card_renders_remove` (`article.plugin.orphan` with `orphanLine`, one `Remove` running the existing confirm and
`remove_plugin`), `update_all_progress_lines`, `update_all_malformed` (a malformed `item` event changes no card; a
malformed summary shows one `p.batch-outcome` rejection line and re-enables the controls),
`update_all_settle_rereads_status` (after `update_all` resolves exactly one `plugin_status` invoke; after that
re-render each listed card still shows its line and the section shows `rescan` once), `update_all_no_cancel_control`
(during a batch no element whose class, name or text is `cancel`, `stop` or `abort` outside the two confirmations).
Red: no `button.update-all`.
**Implement:** plan §4.7's `status.ts` paragraph and PD-15 (texts `updateAll`, `confirmUpdateAll`, `updateAllLine`,
`orphanLine` in `INSTALL_TEXTS`; `batchRunning`; the channel through the existing `ChannelMaker`; `textContent` only).
**Lane:** L-TS. **Done when** the ten titles pass and every pre-existing vitest case passes unchanged.

### T009 [P] — UI: the settings section and its mount (spec FR-047, FR-048, FR-028 (shell half); SC-011 vitest rows, SC-027 `settings_root_change_rereads_status`, SC-030 `update_all_excludes_manager_line`; plan §4.7 `settings.ts` paragraph, PD-14; TA-18, TA-19)

**Files:** `src/settings.ts` (new), `src/settings.test.ts` (new), `src/main.ts`, `src/main.test.ts`. Rule 4 (e), (f).

**Red first:** `settings.test.ts`: `settings_section_cases` (rendered `section.settings` with the toggle button, the
root field, `Save`, `Use default (~/.vst3)` sending `linuxPluginRoot: null`, the checkbox labelled exactly FR-047's
sentence, the locked state from `root_locked_by` (field disabled, `p.root-locked` "Remove Alpha, Beta before changing
the plug-in folder"), an `E_SETTINGS_*` error shown in `p.settings-result`, `invoke("settings_save",
{ linuxPluginRoot, graphicsWorkaround })` with exactly those two keys) and `settings_root_change_rereads_status`
(TA-19). `main.test.ts`: `main_mounts_settings_on_linux`, `main_skips_settings_on_macos`,
`main_rereads_status_after_root_change` (PD-14 verbatim, over `cached-stale.json` / `macos-au-copies.json` with the
`os` they hold), and `update_all_excludes_manager_line` (TA-18; spec SC-030: a manager update offered beside two plugin
updates → the confirmation lists exactly the two plugins, `update_all` gets exactly those two items, and the
control's enabled state equals the same render without a manager update). The four existing call-sequence pins
(`main.test.ts:57`, `:73`, `:110`, `:155`) stay unchanged and green. Red: no `section.settings` /
`createSettingsSection`.
**Implement:** plan §4.7 `settings.ts` paragraph and PD-14 (`settings_view` invoked once, only the first time
`status.lastOs === "linux"`, from `mountStatus`'s settle path; `createSettingsSection(invoke, () => status.refresh())`
mounted below the status section).
**Lane:** L-TS. **Done when** the six titles pass (the last one once T008 has landed, TA-18) and every pre-existing case
passes unchanged.

### T010 [P] — Docs: statements, README, RELEASING, CHANGELOG, card, user-checks, the first-launch test (spec FR-070, FR-072, FR-080, FR-082, FR-084 pre-U-1, FR-104 section; SC-016a, SC-016b reader; plan §4.10, PD-17, PD-18, PD-28; P-2; TA-14)

**Files:** `tools/feed-contract/first-launch-statements.json` (new, LF), `README.md` (new), `RELEASING.md` (new),
`CHANGELOG.md`, `../../docs/manager-card-template.html` (new), `../../specs/krate-manager-phase12/user-checks.md` (new),
`../../tools/feed-contract/tests/manager-first-launch.test.js` (new), `../../tools/feed-contract/fixtures/first-launch/`
`user-checks-{none,one,both,missing-id}.md` (new, LF).

**Red first:** `manager-first-launch.test.js`, first statement `require('./helpers/write-guard.js');`
(`hygiene.test.js:156-164`), one top-level `test('first-launch statements are in the README and the card', …)` with
subtests: the contract has exactly the 13 ids of FR-084 in that order, each `stage` as FR-084 says, every `macos.*`
`text` `null`, every other `text` a non-empty string with no `{{`, `<`, `` ` `` or URL (PD-18); every `pre-u1` `text`
contained in the README (code-span backticks removed, whitespace runs collapsed) and in the card (tags removed, then
`&amp; &lt; &gt; &quot; &#39;` decoded, then whitespace collapsed), SC-016a exactly; `floor` equals FR-080's sentence
(plan §4.10) and is the whole of the README's `## Supported systems` body and of the card's supported-systems
paragraph; the card's `href` set is exactly the four per-tag installer URLs of FR-081 with `{{REPO}}` and
`{{MANAGER_VERSION}}` plus `https://github.com/{{REPO}}/blob/main/apps/krate-manager/README.md#first-launch`, none
containing `.app.tar.gz`, `.sig`, `latest.json`, `/releases/latest/`; the card holds no `<script` and no `style=`; the
SC-016b reader (plan §4.10 row format) over the four fixtures (none / one recorded → the rule "every `post-u1` text
null"; both recorded → "every `post-u1` text non-empty and in both documents", checked against fixture documents; a
missing `FR-073-*` id row → the case fails) and over the real `user-checks.md` (pre-U-1: none recorded, so every
`post-u1` text must be `null`). Never `t.skip` (SC-016b; `feed-tools.yml:150` fails a top-level `# SKIP`). Red: the files do not exist.
**Implement:** the contract (13 statements; the `windows.*`, `linux.*`, `all.rescan` texts from FR-084 (a), (c), (d)
and the External facts rows, `spec.md:131-141`; `linux.reaper` names Preferences > Plug-ins > VST > Edit path list;
`linux.blank-window` the terminal start with `WEBKIT_DISABLE_DMABUF_RENDERER=1`; `linux.graphics` the other rungs as
terminal-only steps); `README.md` (`## First launch` with `### Windows`, `### macOS` holding only "Written from the
recorded checks; see the release notes until then." and no route, `### Linux`, `### Every system`; `## Supported
systems`); the card (PD-17, 16-space indentation, no new CSS); `RELEASING.md` (`## Procedure`: dispatch the dry run,
read its `landing-dry` artifact, the R-7 line "Before each manager release, open one release-asset download URL of the
release and confirm its redirect host is `release-assets.githubusercontent.com` (`RELEASE_ASSET_HOST`,
`crates/core/src/self_update.rs:32`)", then the real dispatch; `## RustSec`: the URL `https://rustsec.org/`, the three
package-page URLs, the three crates, the rule sentence of FR-104, and the table header `| version | date | rquickjs |
rquickjs-core | rquickjs-sys | result |` with **no row** (TA-14)); `CHANGELOG.md` `## [0.1.0] - Unreleased` `### Added`
gains three lines (FR-072: Update all; the Linux settings screen and graphics toggle; orphan Remove); `user-checks.md`
(plan §4.10: one table per check, FR-070 columns plus first column `id` and last column `result`, id rows
`FR-073-sequoia`, `FR-073-tahoe`, `FR-074-a` … `FR-074-f`, `FR-075`, `R-POST-3`, every `result` cell empty, a header
note "Only the user fills a row; agents never perform or fill these checks (FR-070)").
**Lane:** L-NODE (the new file's subtests all `ok`), L-VLINT (CHANGELOG still lints). Note in the handover: the RustSec
row is the main loop's (TA-14).

### T011 [P] — `tools/gen-root-index.js` (spec FR-081 tool, D13; SC-029 generator; plan §4.8, PD-16; SI-14; TA-10, TA-11, TA-12)

**Files:** `tools/gen-root-index.js` (new, LF via T001's `.gitattributes` line, `#!/usr/bin/env node` first line),
`tools/feed-contract/tests/gen-root-index.test.js` (new), `tools/feed-contract/fixtures/landing/plugins/{alpha,beta,
gamma,delta,delta-x}/version.json`, `…/plugins/alpha/docs/index.html`, `…/landing/index-template.html`,
`…/landing/manager-card.fixture.html`, `…/landing/root-index.expected.html` (all LF, Rule 8),
`specs/krate-manager-phase12/evidence/root-index-python.txt`.

1. **Fixture tree.** `alpha` (tagged, has a page; description contains `$&`, `&amp;` and `{{REPO}}`, so the
   `split`/`join` rule and the replacement order are pinned), `beta` (tagged, no page), `gamma` (no tag), `delta` and
   `delta-x` (both tagged; TA-11). Tags: `alpha/v1.2.0`, `alpha/v1.10.0` (version sort picks 1.10.0), `beta/v0.3.0`,
   `delta/v2.0.0`, `delta-x/v0.1.0`. `index-template.html` = `git show 7c4b6837:docs/index-template.html`.
2. **The Python reference (TA-10).** A Node script under the scratchpad (Rule 9) copies the fixture tree and template
   into a fresh `mktemp -d` repo inside WSL, `git init`, commits, creates the tags, writes the dedented Python block of
   `git show 7c4b6837:.github/workflows/release.yml` (`:799-847`) to a file there and runs `REPO=o/r python3 <file>`;
   the output `_site/index.html` becomes `root-index.expected.html` byte for byte. `evidence/root-index-python.txt`
   records the WSL commands, the tag list, `python3 --version` and the output's sha256 and byte count. The WSL temp
   repo is removed.
3. **Red first:** `gen-root-index.test.js` (write guard first), one top-level `test('root index renders the plugin
   cards and the manager card', …)` with subtests over injected `deps` (`gitTags(pattern)` returning the fixture's
   lists in `--sort=-v:refname` order, `repo()`, file functions over the fixture tree as `root`): no `manager/v*` tag →
   output bytes equal `root-index.expected.html`; tags `manager/v0.9.0`, `manager/v0.10.0`, `manager/v0.10.0-rc.1` →
   `newestManagerVersion` `0.10.0` and exactly one `manager-card`, and removing the rendered card plus its `\n` yields
   the expected bytes; `--manager-version 0.2.0` overrides the tags; the card's four installer URLs are
   `https://github.com/o/r/releases/download/manager/v0.2.0/<asset>` for FR-081's four names, `{{REPO}}` filled, no
   `.app.tar.gz` / `/releases/latest/`; `renderManagerCard({ version: null, … }) === ''`; `repo()` `undefined` or `''`
   → `E_ROOT_INDEX_REPO`, exit 1, no `writeFile` call, before any `readFile`; a `version.json` without string `name`
   → `E_ROOT_INDEX_VERSION_JSON`; `--manager-version 1.2` → `E_ROOT_INDEX_VERSION`; a missing template →
   `E_ROOT_INDEX_TEMPLATE`; an unknown flag → exit 2 with the usage line; the live `docs/index-template.html`
   (`\r\n` → `\n`) equals the fixture copy (TA-12); the tool file's bytes hold no `\r` (the `.gitattributes` rule).
   Red: the module does not exist.
4. **Implement** PD-16 exactly (exports of plan §4.8; `productionDeps()` with `execFileSync('git', ['tag', '-l',
   pattern, '--sort=-v:refname'], { encoding: 'utf8' })` and `root: process.cwd()`; no shell), with TA-11's sort.

**Lane:** L-NODE (`hygiene.test.js` does not scan the new tool until T016 adds it; the tool must already satisfy it:
`child_process` is its only exempt module and it is required by its full name). **Done when** every subtest passes and
`node tools/gen-root-index.js --bogus` prints the usage line and exits 2.

### T012 [P] — `tools/manager-readiness-counts.js` (spec FR-100 tool; SC-020, SC-028 tool cases; plan PD-26; TA-16)

**Files:** `tools/manager-readiness-counts.js` (new, `#!/usr/bin/env node`, a header comment with the WHY, the usage
line and the codes as `tools/manager-release-preflight.js:1-22` does), `tools/feed-contract/tests/manager-readiness-counts.test.js`
(new), `tools/feed-contract/fixtures/landing/readiness/{specs/krate-manager-phase{1..11}/compliance.md, roadmap.md,
readiness-ok.md}` (new, LF, small).

**Red first:** one top-level `test('readiness counts and the closed post-release list', …)` (write guard first) with
subtests: over the fixture specs and roadmap the tool prints one `phase<N> file=<a> status=<b>` line per phase and
`total file=<A> status=<B>` over Phases 1-11 (a Phase 12 file, if planted, prints its own line, not in the total); a
compliance row whose verdict cell is `PENDING-CI` in capitals counts; a `pending-ci` word outside a verdict cell does
not; a status line without `PENDING-CI:` gives `status=null`; `--check-ledger readiness-ok.md` (shaped like the real
ledger per PD-26 and TA-16: Phase 11 SC-020 `pending-ci` in the fixture compliance, a PENDING-CI row `phase11:SC-020`
verdict `post-release` with no run, `phase11:SC-014` closed by a green run, the seven D17 ids in Post-release) → exit
0; mutations written in memory to temp copies, each → exit 1: `phase11:SC-020` dropped from PENDING-CI (count short);
present in PENDING-CI but not in Post-release; a repeated id; a run-closed row whose id is also a Post-release id;
a `post-release` row whose id is not a D17 id (`post_release_rows_are_closed`, a planted sixth exemption); a
Post-release row with verdict `green`; a run URL not of the form `https://github.com/<owner>/<repo>/actions/runs/<n>`;
a 6-hex commit; a cell holding an escaped `\|` parses as one cell; the tool opens no path but its arguments (wrap `fs`
reads in the test and list them).
**Implement:** PD-26 with TA-16's layout. **Lane:** L-NODE.

### T013 [P] — The readiness ledger skeleton (spec FR-100 to FR-103, D14, D17; plan §4.10 last bullet; TA-16)

**Files:** `specs/krate-manager-phase12/readiness.md` (new).

**Implement:** TA-16's seven headings. `## PENDING-CI rows`: every row of `specs/krate-manager-phase{1..11}/compliance.md`
whose verdict cell is `pending-ci` (case-insensitive), in phase then file order, id `phase<N>:<row id>`, column 2 the
row's evidence text as written there (pipes escaped), column 3 the workflow that row names (FR-100: never re-mapped;
Phases 1-3 per `phase2/compliance.md:131-146`, `phase3/compliance.md:99-110`), columns 4-6 empty except
`phase11:SC-020`, whose verdict is `post-release` (D17). Counted this session: 13, 15, 6, 14, 20, 17, 11, 19, 18, 1, 17
rows (151; no id repeats within a phase); re-count at writing time and use that count. `## Post-release rows`: the
seven D17 ids, verdict `post-release`, column 3 the run or release that closes each (D17 text). `## Counts`:
"Pasted from `node tools/manager-readiness-counts.js --specs specs --roadmap specs/Krate-manager-roadmap.md` at the
readiness commit (G5)" and nothing else yet. `## Gates`, `## Backfill and live feed`, `## RustSec`, `## Asset host`:
one empty table each with the fields FR-101, FR-103, FR-102 and P-2 name.
**Check:** a throwaway Node script compares the ledger's PENDING-CI id list with a fresh `| pending-ci` extraction
(equal sets, equal count); quote the count in the notes. T012's tool is not run on it here (it is red until G5 by
design).

### T014 [P] — Preflight: `E_PREFLIGHT_RUSTSEC` and `--run-date` (spec FR-104; SC-021 tool cases; plan PD-19; TA-13)

**Files:** `tools/manager-release-preflight.js`, `tools/feed-contract/tests/manager-release-preflight.test.js`.

**Red first:** extend the fixture builder (`manager-release-preflight.test.js:55-78`) to write `RELEASING.md` and
`Cargo.lock` into each app dir (TA-13; the lock holds the three `[[package]]` stanzas in `Cargo.lock` form, default
versions read from the real `apps/krate-manager/Cargo.lock`, 0.14.0 each, `Cargo.lock:3107-3130`; the row dated the
run date), so every existing case keeps its code; then one new top-level `test('manager release preflight enforces the
RustSec record', …)` with subtests (spec SC-021 and plan §7.1 SC-021 row): no row for the version → `E_PREFLIGHT_RUSTSEC`;
a crate version differing from the lock → refused; an advisory `result` without a bump to the locked versions →
refused; locks at `rquickjs 0.14.1`, `rquickjs-core 0.14.0`, `rquickjs-sys 0.14.2` with a `result` naming each crate's
own version → accepted, two swapped or one version for all three → refused; `date` 8 days before `--run-date` →
refused, 7 → accepted, 1 day after → refused; no `## RustSec` section → refused; two rows for the version → refused; a
valid row → accepted on `--dry-run true` and `--dry-run false` (the real run with the KAT key refused later by
`E_SIGNING_CONFIG`, so the order is pinned: RustSec after `E_PREFLIGHT_TAG_EXISTS`, before `E_SIGNING_CONFIG`);
`--run-date 2026-13-01` → usage exit 2. "manager release body is exact" stays unchanged and green.
**Implement:** PD-19 (header comment code list and `USAGE` updated). **Lane:** L-NODE.

### T015 [P] — Evidence scripts (plan §7.4, §8; lanes L-SCOPE, L-WSL, L-CLOSE, L-RUSTSEC; TA-14, TA-17)

**Files:** `specs/krate-manager-phase12/evidence/{scope-check,wsl-lane,close-lane,rustsec-check}.js` (new). Each has a
`--self-test` of its pure parts and writes only under `specs/krate-manager-phase12/evidence/` and `F:/tmp/gate/`.

- `scope-check.js` (copy of `specs/krate-manager-phase11/evidence/scope-check.js` with Phase 12's set): over
  `git diff --name-only 7c4b6837` plus untracked files, every path is under `apps/krate-manager/`,
  `specs/krate-manager-phase12/`, `tools/feed-contract/`, or is one of `tools/gen-root-index.js`,
  `tools/manager-readiness-counts.js`, `tools/manager-release-preflight.js`, `docs/manager-card-template.html`,
  `.github/workflows/{release,docs,release-manager,feed-tools}.yml`, `.gitattributes`, `specs/Krate-manager-roadmap.md`,
  `specs/INDEX.md`; nothing under `plugins/`, `dsp/`, `cmake/`, `extern/` (FR-111); `docs/index-template.html` and
  `apps/krate-manager/{Cargo.toml,Cargo.lock,package.json,package-lock.json}` unchanged (FR-081, SC-023).
- `wsl-lane.js` (copy of the Phase 11 script, `specs/krate-manager-phase11/evidence/wsl-lane.js`, with Phase 12 paths):
  core + helper clippy and `cargo test` in WSL, `$HOME/km-target`, 40-minute ceiling; information line: how many of
  this phase's 34 cargo required names (TA-1; the four `injection.rs` cases are core integration tests, so they run
  there too) were seen `ok`.
- `close-lane.js` (adapted from the Phase 11 script, rows renamed to this file's lanes): A1 chain L-RS, L-APP,
  L-DEVFEED, L-MANUAL, L-E2E, L-GUARD then `cargo test` beside L-HARNESS; A2 chain L-TS (JSON reporter), L-NAPP; A3
  `node --check` of every new / changed `.js`, the `--self-test`s, L-NODE, L-TAP, L-VLINT; A4 L-YAML x4; A5 L-LINT;
  A6 L-INDEX; A7 L-SCOPE; A8 `node tools/header-consumers.js --tag '[krate-manager]'` → last line `CONSUMERS OUTSIDE
  [krate-manager]: none` (TA-17); A10 L-WSL; B2 L-REQ (want TA-1's notice); G greps over the change set (no `todo!(` /
  `unimplemented!(` outside tests; no added `#[allow` / `#[expect` / `@ts-ignore` / `@ts-nocheck` / `eslint-disable`; no
  thread-count or file-parallelism flag; `git check-attr eol tools/gen-root-index.js` → `lf`; no file named `*.key`
  under `apps/krate-manager/`); C1 `git status` before and after equal outside `evidence/`; raw logs removed. Writes
  `evidence/close-lane.log` and `evidence/timings.md`. Last line `CLOSE LANE GREEN` / `CLOSE LANE RED: <rows>`.
- `rustsec-check.js` (L-RUSTSEC, TA-14): Node's global `fetch` of `https://rustsec.org/packages/rquickjs.html`,
  `…/rquickjs-core.html`, `…/rquickjs-sys.html` (status code each; 404 = no advisory page; a 200 page's advisory ids
  `RUSTSEC-\d{4}-\d{4}` listed), the three versions from `apps/krate-manager/Cargo.lock`, and one row
  `| 0.1.0 | <UTC date> | <v> | <v> | <v> | no advisory |` (or the ids, for the user to answer with a bump). Never run
  by an agent or a gate; its `--self-test` parses fixture HTML and a fixture lock without the network.

**Lane:** `--self-test` of each script; `node --check`. Return with notes "deferred to the main loop: L-CLOSE and L-WSL
at close; L-RUSTSEC before the readiness dry run (TA-14)".

---

## G3 — Integration (sequential: T016, then T017)

### T016 — Workflows, their shape tests, `hygiene.test.js`, the TAP pins (spec FR-081 workflow half, FR-085, FR-086, FR-105; SC-029 shapes, SC-032; plan PD-22 to PD-25, PD-27, PD-28, §4.9; SI-1, SI-3, SI-5, SI-13, SI-16; TA-15)

**Files:** `.github/workflows/release.yml`, `.github/workflows/docs.yml`, `.github/workflows/release-manager.yml`,
`.github/workflows/feed-tools.yml`, `tools/feed-contract/tests/workflow-shape.test.js`,
`tools/feed-contract/tests/release-manager-shape.test.js`, `tools/feed-contract/tests/hygiene.test.js`,
`tools/feed-contract/fixtures/landing/{release.yml.pre-phase12, docs.yml.pre-phase12}` (new, LF, `git show 7c4b6837:…`).

1. **Snapshots** (PD-22) from `7c4b6837`, before any workflow edit.
2. **Red first:** `workflow-shape.test.js` gains one top-level `test('landing pages render with gen-root-index.js', …)`:
   `release.yml` (LF-normalised) equals `release.yml.pre-phase12` with exactly PD-23's edit applied (a function in the
   test that replaces the lines from `# Generate plugin cards…` through `PYEOF` with `node tools/gen-root-index.js
   --out _site/index.html` at the same indentation and inserts the `Setup Node` step before "Generate Root Index");
   `docs.yml` likewise with PD-24's edit; the "Generate Root Index" step holds no `python3` and keeps its `REPO` env;
   both `feed-tools.yml` `paths` lists hold the plan §4.9 / FR-086 / SI-16 entries (`docs/manager-card-template.html`,
   `docs/index-template.html`, `apps/krate-manager/README.md`, `apps/krate-manager/RELEASING.md`,
   `tools/gen-root-index.js`, `tools/manager-readiness-counts.js`, `.github/workflows/docs.yml`,
   `specs/krate-manager-phase12/user-checks.md`). `DEPLOY_PAGES_STEPS` (`:727-733`) gains `'Setup Node'` before
   `'Generate Root Index'`. `release-manager-shape.test.js`: `JOBS`, `NEEDS_IF`, `WRITE_JOBS`, `NO_SECRET_JOBS`
   (`:197-230`) per PD-25; subtests pinning `landing` (`needs: [preflight, feed]`, `if: ${{ !inputs.dry_run }}`,
   `contents: write`, checkout `fetch-depth: 0`, the render step's `run` and its `env` exactly `{ VERSION: '${{
   needs.preflight.outputs.version }}', REPO: '${{ github.repository }}' }`, `peaceiris/actions-gh-pages@v4` with
   `keep_files: true`) and `landing-dry` (`needs: [preflight, feed-dry]`, `if: ${{ inputs.dry_run }}`,
   `contents: read`, no deploy step, `actions/upload-artifact@v7` name `manager-landing-dry-run`, path
   `_site/index.html`, `if-no-files-found: error`); subtest `no_advisory_scan_tool` (no `.github/workflows/*.yml` and no
   `tools/*.js` invokes `cargo audit` or `cargo deny`, SC-032). Red: the live workflows differ.
3. **Implement** PD-23, PD-24, PD-25 and FR-086's paths; then `hygiene.test.js`: `tools/gen-root-index.js` into
   `CHILD_EXEMPT` (`:74-81`), `scannedFiles()`'s `single` list (`:34-48`) and the completeness `missing` list
   (`:122-139`); `tools/manager-readiness-counts.js` into `single` and `missing` only.
4. **TAP pins (PD-27, TA-15):** run L-NODE, read the top-level count `<P>` and names, set `feed-tools.yml` `PLAN`
   (`:94`) to `"1..<P>"` and add the new names to `NAMES` (`:95-136`); then L-TAP.

**Lane:** L-NODE, L-TAP, L-YAML x4, L-LINT. **Done when** all green; the notes quote `<P>` (69 expected).

### T017 — Integration checks, INDEX, the PENDING-CI list, the hand-over (spec FR-101 local half, FR-110, FR-111; SC-023, SC-024 local, SC-025, SC-026 check; plan §7.4)

**Files:** `specs/INDEX.md` (regenerated by `node tools/gen-specs-index.js`, then L-INDEX).

1. Run the lanes that take under five minutes: L-RS, L-APP, L-DEVFEED, L-MANUAL, L-E2E, L-GUARD, L-HARNESS, L-TS,
   L-NAPP, L-NODE, L-TAP, L-YAML x4, L-LINT, L-VLINT, L-INDEX, L-SCOPE, L-REQ (want TA-1's notice). The cargo chain and
   the node / npm chains run at the same time (different toolchains).
2. **SC-023:** `git diff --stat 7c4b6837 -- apps/krate-manager/Cargo.toml apps/krate-manager/Cargo.lock
   apps/krate-manager/package.json apps/krate-manager/package-lock.json` empty; and against `d925be34` (the spec's base)
   for `Cargo.lock` and `package.json` (quote the output; if `d925be34` → `7c4b6837` itself changed one, quote that diff
   and compare at `7c4b6837`). **SC-025:** every new core test file builds roots with `fresh_temp_roots()` /
   `fresh_elevated_roots()` / `temp_roots(` (grep each new test function; list any that does not); no new `#[ignore]`.
   **SC-026:** T002's diff check repeated. **FR-111:** `node tools/header-consumers.js --tag '[krate-manager]'` → none.
3. Write the PENDING-CI list (below) into the notes with the job and assertion of each row, and the main-loop steps.

**Return:** done with notes "deferred to the main loop at close: run `node specs/krate-manager-phase12/evidence/close-lane.js`
(includes L-WSL); before asking the user for the readiness dry run: run `node specs/krate-manager-phase12/evidence/rustsec-check.js`
and paste its row into `apps/krate-manager/RELEASING.md` (TA-14); then fence U-1".

---

## Fence U-1 (the user; no agent performs or fills these rows)

In order (spec FR-076, FR-074 precondition): the live `backfill-sign.yml` run after its green dry run and the
`versions.json` summary (FR-103, SC-022, recorded in `readiness.md` `## Backfill and live feed` from the user's
message); a green `manager-ci` and a green `release-manager.yml` `dry_run: true` at the readiness commit (after the
RustSec row, TA-14); FR-074 (a)-(c) and (f), (f) possibly under WSL with WSLg. The user fills `user-checks.md`; the
build resumes at G5. FR-073 and FR-074 (d), (e) are post-release (D17 R-POST-4, R-POST-5; spec revision 2026-11-03:
no Mac before release).

## G4 — Post-release, R-POST-4 [P] (two tasks, disjoint files)

Runs after the first real dispatch, once a Mac has recorded both FR-073 rows against the public 0.1.0 pkg (spec
revision 2026-11-03); its changes ship with the next manager release.

### T018 [P] — The macOS first-launch text (spec FR-073 → FR-084 (b), FR-076; SC-016b green, SC-018 cite; D11)

**Files:** `tools/feed-contract/first-launch-statements.json`, `apps/krate-manager/README.md`,
`docs/manager-card-template.html`.
0. Read `user-checks.md` rows `FR-073-sequoia` and `FR-073-tahoe`. If either is empty → `blocked` ("U-1 not
   recorded"); if either records that Installer.app refused with no Open Anyway, or that the installed app carries
   `com.apple.quarantine` → `blocked` ("Q5 must be re-ruled by the user", D11). Write nothing in either case.
1. **Red first:** run `manager-first-launch.test.js`: with both rows recorded its SC-016b subtest now requires non-empty
   `macos.*` texts in both documents and fails on `null`. Quote the failing line.
2. **Implement:** `macos.sequoia`, `macos.tahoe`, `macos.one-liner` texts stating exactly what the rows record (the
   route per OS; the one-liner `curl -fLO <the per-tag pkg URL with <version>> && sudo installer -pkg … -target /`,
   PD-18 form), `"stage": "post-u1"` kept; README `### macOS` and the card's macOS paragraph carry each verbatim (the
   placeholder sentence of T010 removed). **Lane:** L-NODE, L-TAP. The notes cite each statement to its row
   (SC-018).

### T019 [P] — The release body's macOS line (spec FR-083; SC-017)

**Files:** `tools/manager-release-preflight.js` (`releaseBody`, `:87-111`), `tools/feed-contract/tests/release-body.expected.md`.
0. The same read and `blocked` rules as T018 step 0.
1. **Red first:** rewrite `release-body.expected.md` so the macOS line has no "provisional" and names the route the
   record supports, linking `https://github.com/<repo>/blob/main/apps/krate-manager/README.md#macos`; "manager release
   body is exact" fails against the old `releaseBody`.
2. **Implement** the `releaseBody` lines to match. **Lane:** L-NODE.

## G5 — Readiness close (sequential, main loop)

### T020 — The ledger closed (spec FR-100 to FR-103, D17; SC-020, SC-021 ledger, SC-022, SC-028 real ledger)

**Files:** `specs/krate-manager-phase12/readiness.md`, and `apps/krate-manager/RELEASING.md` only if the RustSec row
must be re-dated (TA-14). Needs: U-1 recorded in the commit the user pushed, and the user's green `manager-ci` (all
legs), `feed-tools.yml` (three legs) and `release-manager.yml` `dry_run: true` runs at that commit or a later one with
no `apps/krate-manager/**` change between (FR-101; the post-U-1 README edit means the runs of U-1 do not count,
Defect D-11).
1. Paste `node tools/manager-readiness-counts.js --specs specs --roadmap specs/Krate-manager-roadmap.md` into
   `## Counts`; per phase, reconcile the file count with the status-line count, explaining each difference item by item
   (spec F-4: Phases 5 and 6 read one more on the status line at `7c4b6837`).
2. Fill each PENDING-CI row's run URL, commit and `green` from the user's runs (run URLs from the user or `gh run
   list`; reading run metadata is not a test); `phase11:SC-014` closed by the green `dry-publish` job; `phase11:SC-020`
   stays `post-release`.
3. `## Gates`: the L-CLOSE run of the readiness commit; `## Backfill and live feed`: FR-103 as recorded at U-1;
   `## RustSec`: L-RUSTSEC re-run today; if the `RELEASING.md` row is older than 7 days, re-date it from this run (a
   change of `apps/krate-manager/**`: then the dry run is re-run by the user before this step completes); `## Asset
   host`: the user's P-2 reading.
4. `node tools/manager-readiness-counts.js --specs specs --roadmap specs/Krate-manager-roadmap.md --check-ledger
   specs/krate-manager-phase12/readiness.md` exits 0 (SC-028). Any other result is reported, never worked around.

---

## PENDING-CI (macOS and Linux evidence, and every hosted-runner row; only the user can trigger it)

| Row | Job | Assertion that proves it |
|---|---|---|
| SC-002 macOS / Linux, SC-003 to SC-010, SC-027 core on macOS / Linux | `manager-ci.yml` `check (macos-latest)`, `check (ubuntu-24.04)` | step `required tests ran` notice lists `install::batch::*` (19 names), each one passing line (`manager-ci.yml:279-310`) |
| SC-011 Linux, SC-012, SC-014, SC-015, SC-031 natively | `check (ubuntu-24.04)` (and `check (macos-latest)` for SC-012) | the same notice: `settings::tests::*`, `platform::tests::*`, `install::tests::orphan_remove_cases`, `install::tests::not_manageable_is_not_orphan`, `status::tests::orphans_cases` each passing once |
| FR-045 / FR-046 Linux block compiles and lints (never compiled on Windows) | `check (ubuntu-24.04)` steps `clippy`, `tauri build` | `cargo clippy --all-targets -- -D warnings` exit 0 on Linux; `build warnings` step clean |
| SC-007, SC-013, SC-030 scans on every OS | `check (*)` | `no_process_enumeration`, `one_set_var_site_before_threads`, `no_cancel_setter`, `plugin_commands_take_the_root_ticket` passing |
| SI-2 | `check (*)` | `job::tests::core_and_helper_op_caps_agree` passing |
| SC-024 | `check (windows-latest)`, `check (macos-latest)`, `check (ubuntu-24.04)`; `feed-tools.yml` `node-test` on its three legs | all green; the TAP check prints `TAP plan=1..<P>` and `# fail 0` |
| SC-016a / SC-016b, SC-020 / SC-028 tool, SC-021 tool, SC-029 generator on macOS / Linux | `feed-tools.yml` `node-test (ubuntu-latest)`, `node-test (macos-latest)` | the five new top-level names `ok` |
| SC-021 | `release-manager.yml` `dry_run: true`, job `preflight` | `::notice title=preflight::passed` with the committed 0.1.0 row |
| SC-029 | `release-manager.yml` `dry_run: true`, job `landing-dry` | artifact `manager-landing-dry-run` `index.html` holds exactly one `manager-card` with the four per-tag URLs of the dispatched version |
| R-POST-1 (D17) | user, after the first real dispatch | `https://krateaudio.com/` shows the card for 0.1.0's four installers |
| R-POST-4, R-POST-5 (D17, spec revision 2026-11-03) | user, on a Mac after the first real dispatch | FR-073 on Sequoia and Tahoe (then G4); FR-074 (d), (e) |
| R-POST-2, R-POST-3 (D17), Phase 11 SC-014 `publish`, Phase 11 SC-020 | user, first real dispatch and the release after it | FR-075; Phase 11 rows as `specs/krate-manager-phase11/compliance.md:97`, `:103` name them |

## Defects found (in the plan, while writing this list)

| Id | Defect | Where handled |
|---|---|---|
| D-1 | The batch tests need the `with_fixture_url` admission, which the plan's seam widening (PD-6 (3b)) omits (`injection.rs:3951`) | TA-5, T001 step 12 |
| D-2 | `header-consumers.js` without `--tag` prints usage and exits 2 (`tools/header-consumers.js:22-25`) | TA-17, T017, T015 A8 |
| D-3 | PD-16's dir-name sort is not the Python's path sort when one name prefixes another | TA-11, T011 fixture `delta` / `delta-x` |
| D-4 | A placeholder `todo!()` body warns `unused variable` per parameter, failing `-D warnings` | Rule 7 |
| D-5 | Plan §2 lists `permissions/autogenerated/*.toml` as files to write; they are ignored build outputs (`apps/krate-manager/.gitignore:4`) | TA-7 |
| D-6 | Plan §7.1 runs the Python "on this box"; Windows has none, WSL has 3.12.3 | TA-10 |
| D-7 | A `todo!("T006")` message is a string literal `PLATFORM_LITERALS` would report in `platform/mod.rs` | Rule 4 (b), Rule 7, TA-6 |
| D-8 | Per-task target dirs on F: would fill it (2.5 GB free, 14 GB target) | Rule 10 |
| D-9 | `update_all_excludes_manager_line` (SC-030) cannot be written over the status section alone | TA-18 |
| D-10 | The plan does not say when the 0.1.0 RustSec row is written; U-1's dry run already needs it | TA-14 |
| D-11 | FR-101's "no `apps/krate-manager/**` change between" makes the post-U-1 README edit invalidate U-1's runs for the ledger | T020 "Needs" |
| D-12 | PD-19's "copies a valid `RELEASING.md`" ties the preflight tests to T010's file and to the real row's date | TA-13 |
