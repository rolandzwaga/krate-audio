# Tasks: Krate Manager Phase 3 — Feed Workflow and Pages Deploy

**Spec:** `specs/krate-manager-phase3/spec.md` (Clarifications Q1-Q6, 2026-10-30) · **Plan:** `specs/krate-manager-phase3/plan.md`
(PD-1 to PD-18, SI-1 to SI-11, Review notes R-1 to R-3)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 3 (lines 423-457); Decisions Q9, ROSTER, Q10 (lines 32-34);
Cross-Cutting Constraints (lines 961-1002)
**Status:** DRAFT task list, 2026-10-30, written against `feat/krate-manager` at `4f4c4175` (untracked at start:
`specs/krate-manager-phase3/`)
**Lanes in this phase:** the pipeline lane (one new Node tool, its tests, fixtures, four workflow YAML files) and one C++
guard case in `shared_tests`. There is no `apps/krate-manager/` yet (Phase 4), so there is **no app lane** in any task:
no `cargo`, `tsc`, `vitest` or Tauri command, no `Platform` abstraction, no capability file, no elevated helper (plan
§6 says what each safety item means here).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks in parallel; every task in it creates only new files and no
  two tasks in it share a file. Every other group is **sequential**: its tasks run one after another in the order
  listed. A task that edits a pre-existing or shared file (`spec.md`, `contract.js`, `CONTRACT.md`, `make-fixtures.js`,
  `fake-gh.js`, `hygiene.test.js`, `workflow-shape.test.js`, a workflow, `CMakeLists.txt`, a test file another task
  created, `tools/feed-publish.js` after its creating task) is always in a sequential group.
- Each task section is the executor's authoritative instruction: the files, the failing test to write first (file, test
  name, assertions), the implementation intent, and the lane that verifies it. Where a table is too large to copy
  without drift risk, the section names the exact `plan.md` section that holds it; that section is then binding. Where
  this file and the plan differ, this file wins and says why (the TA-n rows of T001).
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the task's notes quote the failing line.
- **Expected reds.** A few tests are written in one task and can only go green in a later one. Each such case is named
  in its task under "Expected red until", with the task that turns it green. Any other red at a gate is a real red.
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.
- **Main-loop lane.** Only T019's `evidence/close-lane.js` holds steps that may run past ~5 minutes (the Ninja
  reconfigure and clang-tidy). T019 writes it and returns with notes "deferred to the main loop at close: run `node
  specs/krate-manager-phase3/evidence/close-lane.js`". Every other command in this file is short (the whole Phase 2
  `node --test` set ran in 14-18 s, `specs/krate-manager-phase2/compliance.md:7`; the Phase 1 close lane, which built
  `shared_tests`, ran its `[update]` gate and clang-tidy on one TU, took 15.4 s,
  `specs/krate-manager-phase1/evidence/close-lane.log`).

### Rules every task obeys (a task that breaks one is defective)

1. **Write guard first.** Every new `tools/feed-contract/tests/*.test.js` starts with an optional `'use strict';` line
   followed by `require('./helpers/write-guard.js');` as its first statement (`tools/feed-contract/tests/hygiene.test.js:145`,
   subtest (b)). Tests write only under `fs.mkdtempSync(path.join(os.tmpdir(), 'km3-<name>-'))` directories, removed in
   `t.after` / `finally`. Repo fixtures are only read.
2. **No network, no real `gh`, no spawn in a test.** No `http`, `https`, `http2`, `net`, `tls`, `dgram` module and no
   `fetch(` (not even in a comment) in any scanned file (`hygiene.test.js:56-61`). No test calls github.com,
   api.github.com, krateaudio.com or any host; `gh` exists only inside `tools/feed-publish.js` `productionDeps`, which no
   test constructs (spec FR-051, SC-017). Phase 3 tests spawn nothing: the write guard's allowlist does not change
   (`tests/helpers/write-guard.js`, spec FR-051).
3. **Child processes.** The string `child_process` may appear only in files of `CHILD_EXEMPT`
   (`hygiene.test.js:69-75`); T016 adds `tools/feed-publish.js` there. No Phase 3 test file contains the string, comments
   included. Never `exec`, `execSync`, `fork` or `shell: true` anywhere.
4. **Module rule.** Call `fs.<fn>`, `crypto.<fn>`, `childProcess.<fn>` through the module object, never destructured
   (`hygiene.test.js:63-68`). CommonJS, `'use strict'`, LF line endings in new files.
5. **Tool form (spec FR-020, plan §4.1).** `tools/feed-publish.js`: first line `#!/usr/bin/env node`; header comment (WHY,
   the plan §4.1.7 usage block, the plan §3.2 exit codes); `'use strict'`; `main(argv, io, deps)` that throws a
   `TypeError` naming `deps` **before reading `argv`** when `deps` is not a non-null object; the entry line
   `if (require.main === module) process.exitCode = main(process.argv.slice(2), process, productionDeps());`;
   `module.exports` last. Expected failures are `contract.ContractError(code, message, where)`
   (`tools/feed-contract/contract.js:15-25`), printed once as `feed-publish: <CODE>: <message>` (degrades:
   `feed-publish: WARN <CODE>: <message>`). Every line the tool writes goes through `emit` or `annotate` (plan PD-18).
6. **No plugin roster in product code (ROSTER).** `tools/feed-publish.js`, `fixtures/phase3/make-phase3.js`,
   `.github/workflows/feed.yml`, the `release.yml` `feed` job block and every Phase 3 test file contain none of the nine
   shipped plugin names as a case-insensitive substring (the rule of `hygiene.test.js:249-280`), not in a comment or an
   example either. Names come from `version.json` files; compare fixtures use the invented `Alpha`, `Bravo`, `Charlie`.
   The C++ case takes its names from the live fixture file (spec FR-046).
7. **Never touch the real system.** No task writes to a VST3, Components or preset folder, `ProgramData`, the registry,
   `%APPDATA%\Krate Audio\…` (the real plugins' `update_state.json`), the `gh-pages` branch, or any path outside the
   FR-060 paths, `F:/tmp` and an `fs.mkdtemp` directory. No task runs `gh` against GitHub, `curl`, an installer, or a
   workflow. The C++ case writes only under `ScopedSettingsDirRedirect`'s temp root
   (`plugins/shared/tests/test_update_checker.cpp:81-114`).
8. **No key.** Phase 3 signs nothing and generates no key into the repo. The only keys are throwaway in-memory ones from
   `fake-gh.js` `throwawaySigner()` (`tests/helpers/fake-gh.js:146-157`).
9. **Top-level tests.** A Phase 3 test file holds only the top-level tests its task names, with the exact plan §7.1
   names; everything else is a subtest (`t.test`). The final TAP plan is `1..57` (46 today + 11, plan SI-9).
10. **Scope fence (spec FR-060 as amended by T001).** Only these paths change: `.github/workflows/feed.yml` (new),
    `.github/workflows/release.yml`, `.github/workflows/feed-tools.yml` (not `ci.yml`: Q6 re-ruled, it stays byte-identical),
    `tools/feed-publish.js` (new), `tools/feed-contract/**` (no file of `fixtures/gen-feed/snapshot-2026-10-29/**` and no
    Phase 1 / Phase 2 fixture except `feeds/cases.json`), `plugins/shared/tests/CMakeLists.txt`,
    `plugins/shared/tests/test_update_checker.cpp`, `specs/krate-manager-phase3/**`, `specs/INDEX.md` (regenerated), and
    the roadmap's Phase 3 status line (main loop at close). `tools/gen-feed.js`, `docs.yml`, `backfill-sign.yml`, every
    `setup.iss`, every `version.json` and every plugin `src/` file stay byte-identical.
11. **Line endings of edited files.** This checkout runs `core.autocrlf=true`: `release.yml`, `ci.yml`, `CMakeLists.txt`,
    `test_update_checker.cpp` and the edited `tools/feed-contract/*.js|*.md` files are CRLF in the working tree (`grep -c
    $'\r$'` equals `wc -l` for each, measured 2026-10-30). An edit keeps the file's existing ending on every line it adds:
    after the edit `grep -c $'\r$' <file>` equals `wc -l < <file>` again. `tools/feed-contract/fixtures/**` is `-text`
    (`.gitattributes:26`): fixture bytes are written only by `make-fixtures.js` (LF), never by hand.
12. **YAML.** Any mapping value holding `${{ … }}` is written in block style, never inside a `{ … }` flow mapping. Workflow
    values reach `run:` scripts only through `env:` (plan PD-2, FR-014). Every `run:` step of `feed.yml` has `shell:
    bash`, starts `set -euo pipefail` and contains no `|` other than `||` (spec D13, FR-003).
13. **Zero warnings.** `node --check` clean on every new or changed `.js`; TAP output with no `Warning:`,
    `DeprecationWarning` or `ExperimentalWarning` line from Node; L-YAML clean on every changed workflow; the C++ build
    with zero warnings. Never silence a warning without a one-line reason the reviewer accepts.
14. **No serial flags.** Never `--test-concurrency=1`, `--test-isolation=none`, `--test-threads=1`, or a test executable
    run as one serial process (CLAUDE.md "Every non-timing test run is sharded"); the one direct C++ run is a single case
    by name.

### Lanes (referenced by every task)

| Lane | Commands | Pass |
|---|---|---|
| **L-NODE** | `node --check <each new or changed .js>`; the task's own file(s): `node --test --test-reporter=tap "tools/feed-contract/tests/<file>.test.js" > f:/tmp/km3-<task>.tap`; then the whole set: `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > f:/tmp/km3-all.tap` (quoted glob, files in parallel child processes); read with `tail -12` | every `node --check` exits 0; TAP `# fail 0` except the task's named expected reds; no Node warning line |
| **L-FIX** | `node tools/feed-contract/fixtures/make-fixtures.js --check` | prints `make-fixtures: committed fixtures match the builder`, exit 0 |
| **L-YAML** | `npx --yes yaml@2.6.1 valid < <workflow file>` (the version the gate runs, `.claude/workflows/krate-manager-phase.js:598`) | exit 0 |
| **L-LINT** | `node tools/lint-serial-test-runs.js` | exit 0 |
| **L-CXX** | `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests -- /nr:false > f:/tmp/km3-cxx-build.log 2>&1` (tail it; count `warning` lines); `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests > f:/tmp/km3-gate.log 2>&1` (tail; `run-gate.js:51-53` refuses a filter without the four timing exclusions, TA-2) | build exit 0, 0 warnings; last gate line `GATE GREEN` |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js f:/tmp/km3-all.tap` (file mode only: it extracts the live `TAPCHECK` heredoc from `feed-tools.yml` and runs it with `CI=1`, `specs/krate-manager-phase2/evidence/tap-check.js:1-11`) | exit 0, prints `TAP plan=1..57 top-level=57` (from T018 on) |
| **L-SCOPE** | `node specs/krate-manager-phase3/evidence/scope-check.js` (exists from T006) | exit 0 |
| **L-CLOSE** | `node specs/krate-manager-phase3/evidence/close-lane.js` (exists from T019; **main loop only**) | last line `CLOSE LANE GREEN` |

---

## Preconditions — rulings needed before the build stage

The plan leaves two choices to the user and eleven spec edits for the main loop (plan §0.2, §10). **T001 stops the
build if the record below is missing.** The main loop records each ruling in `spec.md` `## Clarifications` under a new
`### Session 2026-10-30 (plan)` heading, one bullet per item, `Q: … -> A: …`, tagged `[P-1]`, `[P-2]`, `[P-3]`.

- **P-1: accept the plan's spec edits SI-1 to SI-11 and this file's TA-1 to TA-4** (T001's table), so T001 writes them
  into the spec rows the compliance stage checks. Recommended: accept all.
- **P-2 (plan SI-5, PD-17): a downloaded `release.json` whose GitHub asset record has `digest: null`.** (a) Accept it on
  size alone, as spec FR-022 says, and print `collect: digest checked <n> of <m>` (plan default); or (b) refuse it with
  `E_FEED_DOWNLOAD`, as Phase 2 ruled for installers (`specs/krate-manager-phase2/tasks.md` "CC-verify (user ruling:
  refuse)"; `tools/backfill-sign.js:306-323`). Under (b) T010 changes one branch of `checkDownloaded` and its T3
  `digest: null` subtest asserts the refusal instead. Recommended: (a), because `release.json` is not the trust root
  (Phase 7 verifies every asset's minisign signature against the committed key, plan PD-17).
- **P-3 (plan SI-7): the two `block` drift kinds no `feed.yml` run can clear** (`release_notes not empty`, `removed`).
  (a) Keep them `block` as ruled in Q1, with the documented recovery (a hand commit on `gh-pages` that fixes
  `versions.json`, then a dry run and a real run), written in `CONTRACT.md` §10.5 (T017); or (b) re-rule their severity.
  Recommended: (a).

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | sequential | T001 rulings + spec rows · T002 seven codes + `CONTRACT.md` §7 rows · T003 `fake-gh.js` Phase 3 shapes | Preconditions |
| G2 Modules | **[P]** | T004 `make-phase3.js` + `phase3-fixtures.test.js` · T005 `feed-publish.js` core + `feed-publish-steps.test.js` · T006 `evidence/scope-check.js` | G1 |
| G3 Fixtures | sequential | T007 `make-fixtures.js` registers set `phase3`; fixtures regenerated | G2 |
| G4 Subcommands | sequential | T008 `compare` · T009 `generate` · T010 `collect` · T011 pipeline tests | G3 |
| G5 Workflows | sequential | T012 `feed.yml` + the Phase 3 shape test · T013 `release.yml` · T014 `ci.yml` stays unchanged | G4 |
| G6 C++ guard | sequential | T015 `SHARED_FIXTURES_DIR` + the one-v2 / eight-v1 case | G3 |
| G7 Integration | sequential | T016 hygiene · T017 `CONTRACT.md` §10 · T018 `feed-tools.yml` · T019 index, close lane, short close run, PENDING-CI | G1-G6 |

Every SC has one owner: SC-001, SC-002, SC-005 T011 (SC-002's unit half T009); SC-003, SC-004, SC-006 T010 (the fake's
half of SC-003 T003); SC-007 T008 (its fixtures T004); SC-008 T004 + T007; SC-009 T015 (its clang-tidy and real-state
rows in L-CLOSE, T019); SC-010 T012 + T013 + T014; SC-011 T016; SC-012 T006 + T019; SC-013w T019; SC-016 T013 + T019
(regression); SC-017 T005; SC-018 T005 + T009; SC-009m/l, SC-013m/l, SC-014, SC-015 PENDING-CI (T019 list).

---

## G1 — Foundation (sequential)

### T001 — Rulings check and the plan's spec amendments into `spec.md`

> **Rulings confirmed (2026-10-30, after the first T001 stop):** P-1, P-2 (refuse a null-digest `release.json`) and P-3 are
> informed user rulings; the user has had `release.json` explained and asked to continue (spec Clarifications, Session
> 2026-10-30 (plan), last note). Q6 is re-ruled (a), `ci.yml` unchanged. Do not stop on these; write the rows. T002 is
> already done on disk (seven codes in `CONTRACT.md` §7 and `contract.js` `CODES`): do not duplicate them.

**Files:** `specs/krate-manager-phase3/spec.md` (edit).

**Precondition (STOP AND SURFACE).** Read `spec.md` `## Clarifications`. If it lacks a `### Session 2026-10-30 (plan)`
bullet for each of `[P-1]`, `[P-2]` and `[P-3]` (Preconditions above), return status `blocked` with notes quoting the
three items verbatim from this file, and change nothing.

**Test first.** Docs-only task: no test file. Before editing, record in the notes which rows below already carry the
change (expected: none; `grep -n "make-phase3\|queue: max\|digest checked" specs/krate-manager-phase3/spec.md` prints
only the existing `queue: max` lines).

**Implement.** Edit each row in place, appending `(plan PD-n)`, `(plan SI-n)` or `(tasks TA-n)` to the changed sentence.
None relaxes a threshold. The source text is plan §0.1 / §0.2 (binding) and the TA rows below:

| Row(s) | Change | Source |
|---|---|---|
| FR-044, FR-045, SC-008, New components | the generator is `tools/feed-contract/fixtures/phase3/make-phase3.js`, registered as set `phase3` of `fixtures/make-fixtures.js`; FR-045's file and the `feeds/cases.json` row are built by it; it picks the one snapshot `version.json` with `preset_subdir` and names no plugin; SC-008 adds "and `make-fixtures.js --check` passes" | SI-1, PD-14, PD-15 |
| FR-020 | "every effect (`gh`, file writes) through an injected `deps`" becomes "every `gh` call and the gen-feed run through an injected `deps` object (`gh`, `genFeedMain`); files only at paths given as flags" | SI-2, PD-3 |
| FR-002, FR-012, SC-010, D9 | the `krate-feed` block (`cancel-in-progress: false`, `queue: max`) sits on jobs `build` and `deploy`; no top-level `concurrency`, none on `verdict`; SC-010 asserts that placement | SI-3, PD-1 |
| FR-027, FR-040, SC-007 | one drift line per plugin: `drift <Name> <kind>[; <kind>…] <severity>` (kinds in plan §4.2's fixed order; `[...]` and `<severity>` are placeholders, not literal brackets); the summary counts plugins; SC-007's mixed-kind subtest expects `drift <Name> <kind A>; <kind B> block` | SI-4, PD-10 |
| FR-022 | per the `[P-2]` ruling; under (a) add "`collect` prints `collect: digest checked <n> of <m>`" | SI-5, PD-17 |
| FR-023a | the retry log label is `list` for `release list`, `<subcommand> <tag>` for `view` / `download` | SI-6, PD-5 |
| FR-040, Edge cases | per `[P-3]`; under (a) add the non-self-healing note and the hand-commit recovery | SI-7 |
| FR-060, SC-012 | the allowed set gains `specs/krate-manager-phase3/**`, the roadmap's Phase 3 status line, and `specs/INDEX.md` (regenerated) | SI-8, TA-1 |
| FR-052 | `PLAN` becomes `1..57`; the new top-level tests are the eleven of plan §7.1 (SC-009, SC-011, SC-012 add none) | SI-9 |
| FR-027, FR-029c, FR-029e, SC-007 | PD-18 (a) as an `E_INPUT` abort (a control character U+0000-U+001F / U+007F in a live `plugins` key or string `version` / `download_url` / `release_notes`), (b)-(d) as the output rule; FR-029e adds "no line other than an annotation starts with `::`"; SC-007 adds plan §7.1 T7's PD-18 cases | SI-10, PD-18 |
| SC-009m / SC-009l | "a push that changes only `tools/feed-contract/fixtures/{feeds,gen-feed}/**` is knowingly not proven on macOS and Linux by this case" | SI-11, R-1 |
| FR-021, FR-023, SC-004 | `collect` refuses (`E_INPUT`, before any `gh` call, nothing touched) an `--out-dir` already holding `releases.json`, `manifests`, `manager` or `collect-report.json`, downloads straight to the FR-022 / FR-024 paths, and on an abort removes the four names this run created; it checks names first (`E_PLUGIN_NAME`, `E_DUPLICATE_PLUGIN`, a name lower-casing to `manager`) and the list / view shapes (PD-8) | PD-6, PD-7, PD-8 |
| FR-029a, FR-009 | `generate` refuses an existing `<site-dir>/manager` (`E_INPUT`, untouched, gen-feed never run); the determinism check also requires equal exit code and equal stderr | PD-9, plan §4.1.4 step 2 |
| FR-040 | a live `version` that `parseSemVer` rejects lands in `version changed without a release` (`warn`) | PD-11 |
| FR-029c | reason order build, ref, deploy, the three flags, evidence; the deploy reason only when `--build` is `success`; one `evidence missing <file>` reason per missing file | PD-12 |
| FR-027 | `--report` holds the result lines only; with `--annotate` stdout is the result lines then the annotation lines; on `E_INPUT` / `E_FEED_SCHEMA` the report is written empty | PD-13 |
| FR-004, FR-006, FR-010 | steps pass `"$GITHUB_REPOSITORY"` instead of `"${{ github.repository }}"` | PD-2 |
| SC-009, FR-054 | the gate command is `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests`: `tools/run-gate.js:51-53` refuses a filter without the four timing exclusions (`GATE RED usage`), and `~[.]` keeps hidden probes out (the Phase 1 close lane's filter, `specs/krate-manager-phase1/evidence/close-lane.js:32`) | TA-2 |
| FR-054, SC-009 (3) | clang-tidy runs on the changed TU only, `clang-tidy -p build/windows-ninja plugins/shared/tests/test_update_checker.cpp`, after reconfiguring `build/windows-ninja` (the test CMake changed); the Phase 1 lane ran the same TU in 12 s (`specs/krate-manager-phase1/evidence/close-lane.log`, row B4), a full `-Target shared` sweep reaches nothing more for this change (CLAUDE.md "Run only the tests the change set can reach") | TA-3 |
| SC-011 | the `tools/*.js` form check of `feed-publish.js` is subtest `(d)` of `tools stay offline` (plan PD-16 says `(e)`, but the test has only `(a)`, `(b)`, `(c)`, `hygiene.test.js:111`, `:145`, `:155`) | TA-4 |

**Done when:** every row above carries its change and its source tag; the notes list each edited row id (the spec is
untracked, so there is no git diff to cite). **Lane:** none (docs).

### T002 — The seven new codes and their `CONTRACT.md` §7 rows (spec FR-053, plan §3.1)

**Files:** `tools/feed-contract/contract.js` (edit), `tools/feed-contract/CONTRACT.md` (edit: §7 table only).

**Test first.** Append the seven §7 table rows first (plan §3.1 table, binding: `E_FEED_LIST_TRUNCATED`,
`E_FEED_DOWNLOAD`, `E_FEED_SCHEMA`, `E_FEED_NONDETERMINISTIC`, `E_STEP_FAILED`, `E_V1_DRIFT` class `none`;
`E_MANAGER_LATEST` class `degrade`; columns `| Code | Class | Raised by | Meaning |`) directly after the
`E_SIGN_KEY_MISMATCH` row (`tools/feed-contract/CONTRACT.md:489`). Run L-NODE for `hygiene.test.js`: `CONTRACT.md
documents every code and field` fails with "the code table names codes that contract.js CODES does not define" listing
the seven (the bidirectional check, `hygiene.test.js:408-420`). Record that red.

**Implement.** Append the seven keys to `CODES` after `E_SIGN_KEY_MISMATCH: 'none',` (`contract.js:98`), six `'none'`
and `E_MANAGER_LATEST: 'degrade'`. No other change to either file (§10 is T017).

**Done when:** L-NODE green for `hygiene.test.js` and the whole set; `git diff -U0 tools/feed-contract/contract.js`
shows seven added lines only.

### T003 — `fake-gh.js`: the Phase 3 shapes, all additive (spec FR-050, plan §4.8)

> **Already done on disk (2026-10-30, first build run):** `tests/helpers/fake-gh.js` has the Phase 3 shapes and
> `tests/feed-publish-collect.test.js` exists. Verify them against this section and the rows T001 just wrote; change only
> what those rows now require. Do not duplicate them.

**Files:** `tools/feed-contract/tests/helpers/fake-gh.js` (edit), `tools/feed-contract/tests/feed-publish-collect.test.js`
(new; T010 extends it).

**Test first.** Create `feed-publish-collect.test.js` (rule 1) with the top-level test `collect downloads only the
selected manifests` (its exact plan §7.1 T3 name) holding, for now, one subtest `the fake gh serves the Phase 3
shapes`, which asserts:
1. Default world (`makeWorld({tmp})`, no `world` key): `state.repo === 'krate-fixture/backfill'` and
   `state.releases.length === 38` (the Phase 2 world is unchanged).
2. `gh(['release','view',<a stable tag>,'--repo',repo,'--json','tagName,assets'])` returns `status 0` and a document
   whose keys are exactly `['tagName','assets']`.
3. `gh(['release','download',tag,'--repo',repo,'--pattern',<asset>,'--output',<file>])` writes exactly the served bytes
   to `<file>` and nothing else (the directory listing before and after differs by that one name); an asset the release
   lacks returns `status 1` and writes nothing; an `<file>` whose directory does not exist returns `status 1`, stderr
   naming the directory, and creates nothing (as `gh -O` needs an existing directory, plan §4.8). The `--dir` shape still
   works (the Phase 2 backfill tests stay green).
4. `makeWorld({tmp, world: 'snapshot'})`: `state.repo === 'rolandzwaga/krate-audio'`; the list rows
   (`tagName, isPrerelease, isDraft, publishedAt`) deep-equal `fixtures/gen-feed/snapshot-2026-10-29/releases.json`
   (88 rows); every release has exactly the three asset names `<Name>-<ver>-Windows-x64.exe`, `-macOS.pkg`,
   `-Linux-x64.tar.gz` (`<Name>` the `name` of the snapshot `version.json` whose lower-cased name is the tag prefix,
   `<ver>` the tag's version), each with a numeric `size` and a `sha256:<64 hex>` `digest` (no `auto` left).
5. `addPhase2Files(state, ctx, tag, {tmp, signer, pluginsDir})` called inside `mutate` on one Phase 2 world release:
   the release then lists the three installers plus exactly `SHA256SUMS`, the three `<installer>.minisig` and
   `release.json`; the served `release.json` parses and passes `contract.checkReleaseManifest(doc, {repo: state.repo})`;
   each served `.minisig` verifies (`minisign.verify…` of `tools/minisign.js`, over the served installer bytes written to
   a temp file) against `signer.publicKeyText`; each `SHA256SUMS` line equals the SHA-256 of the served installer.
6. `addManagerRelease(state, ctx, {tag: 'manager/v0.1.0', publishedAt, latestJson: '{"x":1}\n'})` adds one non-draft,
   non-prerelease row whose only asset is `latest.json` with exactly those bytes; `latestJson: null` adds a row with no
   assets; `isDraft` / `isPrerelease` options set those flags.

Run it: red (`world` ignored, `addPhase2Files` / `addManagerRelease` not exported, `tagName,assets` refused). Record it.

**Implement** (plan §4.8, binding; every change additive, so every Phase 2 caller keeps its shapes):
- `VIEW_JSON` (`fake-gh.js:39`) gains `'tagName,assets'`.
- `download` (`fake-gh.js:232-242`) accepts a second shape: exactly six flags `--repo <repo> --pattern <name> --output
  <file>`; writes the served bytes to `<file>` without `mkdir`; the `--dir` shape is unchanged.
- `makeWorld({tmp, mutate, hooks, world = 'backfill'})`: `world: 'snapshot'` builds `state` from the snapshot
  `releases.json` as in assertion 4 (`isImmutable: false`, assets `size` / `digest` `'auto'`, so the existing fill loop at
  `fake-gh.js:170-177` sizes them); any other `world` value throws.
- Export `addPhase2Files(state, ctx, tag, {tmp, signer, pluginsDir})`: writes the three served installer bytes into
  `<tmp>/<tag with "/" as "-">/`, signs each with `signer.sign(file, comment)` where the comments come from
  `genReleaseManifest.trustedComments({versionJsonText, assetsDir, darwinFacts})` (`tools/gen-release-manifest.js:270-279`),
  records facts in-process exactly as `tests/gen-release-manifest.test.js:66-84` does (`darwinFactsFor`-shaped object
  for the pkg; `releaseFacts.linuxFacts({tarball, extracted: <tmp dir>, objdumpText: <OBJDUMP_SAMPLE text>},
  {listDirectory: () => listing.listingFromTarGz(bytes)})`), calls `genReleaseManifest.buildManifest({versionJsonText,
  setupIssText, assetsDir, darwinFacts, linuxFacts, repo: state.repo, pubDate: <the row's publishedAt>,
  publicKeyText: signer.publicKeyText})` (`tools/gen-release-manifest.js:168-177`) with the plugin's `version.json`
  (from `pluginsDir`, `version` set to the tag's) and `setup.iss`, then appends the five asset records (`size` /
  `digest` `'auto'`) and `ctx.setServed`s their bytes. No child process (spec FR-051).
- Export `addManagerRelease(state, ctx, {tag, isDraft = false, isPrerelease = false, publishedAt, latestJson})`.
- Header comment: the new exports and `world`. Rule 6: name no shipped plugin (the snapshot names come from files).

**Done when:** L-NODE green for `feed-publish-collect.test.js` and `backfill-sign.test.js` (Phase 2, unchanged), then the
whole set; `git diff -U0 tools/feed-contract/tests/helpers/fake-gh.js` removes no line except the `VIEW_JSON` line and
the one `download` shape test it widens.

---

## G2 — Modules [P] (each task creates only its own new files)

### T004 [P] — `fixtures/phase3/make-phase3.js` and the SC-008 test (spec FR-044, FR-045, SC-007 fixtures, SC-008; plan §4.7, PD-14, PD-15)

**Files:** `tools/feed-contract/fixtures/phase3/make-phase3.js` (new), `tools/feed-contract/tests/phase3-fixtures.test.js`
(new). It does not edit `make-fixtures.js` (T007) and writes no fixture file.

**Test first.** `phase3-fixtures.test.js` (rule 1), top-level test `Phase 3 mixed feed fixture is real-shaped` (plan
§7.1 T8, binding), subtests:
1. `require('../fixtures/phase3/make-phase3.js').files({fixtureSignature})` (with `fixtureSignature` from
   `require('../fixtures/make-fixtures.js')`, `make-fixtures.js:1289`) returns an object whose keys are exactly the
   manifest path `phase3/<lower>-v<version>.json`, `feeds/valid/one-v2-eight-v1.json`, `phase3/compare/cases.json` and
   the `phase3/compare/<case>/{live,feed,releases}.json` triples of every case row.
2. The committed manifest `fixtures/phase3/<lower>-v<version>.json` (the one snapshot plugin whose `version.json`
   carries `preset_subdir`, found by reading the snapshot, never spelled) equals the built text byte for byte and passes
   `contract.checkReleaseManifest(doc, {repo: 'rolandzwaga/krate-audio'})`.
3. `gen-feed` `main` (through `tests/helpers/fixtures.js` `runMain`) on `--plugins-dir <snapshot plugins> --releases
   <snapshot releases.json> --manifests <a temp dir holding a copy of the committed manifest> --repo
   rolandzwaga/krate-audio --out <temp>` exits 0 and its output Buffer-equals `fixtures/feeds/valid/one-v2-eight-v1.json`.
4. `validateFeed` accepts that file under `v2` and `v1`; it has nine plugins; exactly one (the `preset_subdir` plugin)
   has `platforms` and `install`, the other eight neither.
5. `gen-feed --project-v1` of it Buffer-equals `fixtures/gen-feed/snapshot-2026-10-29/live-versions.json`.
6. `fixtures/feeds/cases.json` holds the row `{rule: 'Phase3/one-v2-eight-v1', file: 'valid/one-v2-eight-v1.json',
   schema: 'v2', expect: 'accept'}`.
7. `require('../fixtures/make-fixtures.js').check().ok === true`.
8. Every compare case row has the fields `case, dryRun, exit, stdout, stderr, annotations` and its `feed.json` passes
   `validateFeed` v2 unless the row is the `feed-fails-v2` case.

Run it: red (module missing). Record it.

**Expected red until T007:** subtests 2, 3, 5 (if run before), 6 and 7 (the committed files do not exist and `check()`
does not know the set). Subtests 1, 4 (on the built text) and 8 (on the built rows) are green at the end of this task.

**Implement** `make-phase3.js` per plan §4.7 (binding): header comment (WHY, "Registered as make-fixtures.js set `phase3`;
never run on its own. Names no plugin."), `'use strict'`, requires `node:crypto`, `node:fs`, `node:path`,
`../../contract.js`, `../../listing.js`, `../../../gen-feed.js`; exports only `files({fixtureSignature})`, returning
`{relPath: content}` (LF text, 2-space JSON plus final LF where JSON):
- **The manifest**: plugin = the one snapshot `version.json` with `preset_subdir` (assert exactly one); `tag =
  <lower>/v<version>`; assert a non-draft, non-prerelease snapshot row with that tag that is `genFeed.selectReleases`'
  stable pick; `pub_date` = that row's `publishedAt`; `assets` for `darwin-universal`, `linux-x86_64`, `windows-x86_64`
  (bytewise), each built as `make-fixtures.js` `releaseAsset` / `platformFor` build theirs (`make-fixtures.js:155-170`,
  `:562-567`: `name: contract.assetNameFor(…)`, `url: contract.perTagUrl('rolandzwaga/krate-audio', tag, name)`, size and
  `sha256` of the stated synthetic content `krate fixture asset <asset>\n` (`:148-151`), `signature:
  fixtureSignature(contract.formatTrustedComment(name, version, key, asset))`, `format`, `min_os` `11.0` / `2.31` /
  `10.0`); the Linux `payload` = `{files, sha256: listing.digest(list), list}` of
  `listing.listingFromEntries(<the five-entry tree of make-fixtures.js:547-556 (linuxTree) for <Name>>)`; `install` = `{vst3_bundle:
  <Name>.vst3, au_component: <Name>.component, preset_subdir: <version.json preset_subdir>, windows_uninstall_key:
  genFeed.extractAppId(<snapshot setup.iss>) + '_is1', macos_pkg_id: 'com.krateaudio.' + <lower>}` (the cross-checks of
  `tools/gen-feed.js:494-522`). Assert `contract.checkReleaseManifest(doc, {repo})` before returning.
- **The mixed feed**: `genFeed.buildFeed({plugins: genFeed.readPluginsDir(<snapshot plugins>, {withSetupIss: true}),
  releases: <snapshot releases.json>, manifests: [{file: 'manifests/<lower>-v<v>.json', text: <manifest text>}], repo:
  'rolandzwaga/krate-audio'})`; assert `warnings.length === 0`; the content is its `text`.
- **The SC-007 triples** under `phase3/compare/<case>/` and `phase3/compare/cases.json` (`[{case, dryRun, exit, stdout,
  stderr, annotations}]`, `stdout` = the result lines, `annotations` = the `--annotate` lines that follow them, plan
  PD-13). Invented plugins `Alpha`, `Bravo`, `Charlie`; new feeds are full C1 v2 documents with v1-only entries shaped
  like `fixtures/gen-feed/snapshot-2026-10-29/expected.v2.json`'s (`version, download_url, release_notes: "", tag,
  pub_date, channel: "stable", release_notes_url`). Expected lines are **literal strings** in the module's case table,
  in the exact formats of plan §4.2 and §4.1.6, never computed by `feed-publish.js`. One row per case:
  `same`; `newer`; `newer-url` (`newer … download_url <old> -> <new>`); `added`; `q9-corrected-v1-live`;
  `q9-corrected-v2-live`; `q9-corrected-absent`; `edge-cache` (live one stable release behind → `newer`, exit 0); for each
  of the six drift kinds a `-real` row (`dryRun: false`) and a `-dry` row (`dryRun: true`): `block` kinds
  (`release-notes-not-empty`, `version-lowered`, `removed`) exit 1 in both, `warn` kinds (`url-without-release`,
  `version-without-release`, `added-without-release`) exit 3 real / 1 dry, the `url-without-release-real` summary
  `compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)`; `version-lowered-no-prerelease`;
  `mixed-kinds-one-plugin` (one plugin with a `warn` and a `block` kind → `drift <Name> <kind A>; <kind B> block`, exit 1
  real); `mixed-two-plugins` (one of each, exit 1 real); `non-semver-live` (PD-11 → `version changed without a release
  warn`); `live-not-json` and `live-without-plugins` (`E_INPUT`, exit 1, no result line); `feed-fails-v2`
  (`E_FEED_SCHEMA` naming its `instancePath`); `live-control-lf` (key `"X\n::notice title=feed-verdict::green"`),
  `live-control-cr` (a `\r` key) and `live-control-bel` (a `version` holding U+0007), each `E_INPUT` with first stderr
  line `feed-publish: E_INPUT: live plugins.<JSON.stringify(key)> holds a control character` (PD-18 (a)); `live-backtick`
  (a live-only key ``X```y`` → `drift "X```y" removed block`, the key in its `JSON.stringify` form, PD-18 (b)). The
  stderr arrays hold the `feed-publish: E_V1_DRIFT: …` / `WARN E_V1_DRIFT: …` lines of plan §4.2.

**Done when:** L-NODE for `phase3-fixtures.test.js` shows subtests 1, 4, 8 green and only the expected reds above;
`node --check` on both files; rule 6 holds (`grep -il` of the nine snapshot `version.json` names over both files finds
nothing, the names read with `node -e` from the snapshot).

### T005 [P] — `tools/feed-publish.js` core: form, `main`, `outcome`, `verdict`, `withGhRetry`, output writers (spec FR-020, FR-023a mechanism, FR-028, FR-029b, FR-029c, FR-029e, SC-017, SC-018 part; plan §4.1.1, §4.1.5, §4.1.6, §4.1.7, PD-4, PD-5, PD-12, PD-18)

**Files:** `tools/feed-publish.js` (new), `tools/feed-contract/tests/feed-publish-steps.test.js` (new; T009 extends it).

**Test first.** `feed-publish-steps.test.js` (rule 1), two top-level tests (plan §7.1 T10, T11):
- `feed-publish tests never build real effects` (SC-017): (a) `main(['outcome','--tool','collect','--exit','0',
  '--github-output',<tmp file>], io)` and the same with `null` as `deps` each throw a `TypeError` whose message names
  `deps`, and the temp file is untouched (no byte appended); (b) static scan of exactly the five files
  `feed-publish-collect.test.js`, `feed-publish-compare.test.js`, `feed-publish-steps.test.js`, `feed-pipeline.test.js`,
  `phase3-fixtures.test.js` plus `helpers/fake-gh.js`: each exists, holds 0 `productionDeps(` tokens, and every
  `main(` / `.main(` call on the `feed-publish.js` module object has three arguments (count top-level commas of the call;
  the test names its rule). The scan's tokens are assembled from pieces so this file's own source does not match.
- `feed-publish decides every workflow branch` (SC-018, this task's part), subtests:
  1. `outcome`: for each tool (`collect` → `manager_degraded`, `generate` → `feed_degraded`, `compare` →
     `drift_warned`), `--exit 0` appends exactly `<flag>=false\n` to a pre-filled file (prior content kept) and returns
     0; `--exit 3` appends `<flag>=true\n`, 0; `--exit 1`, `2`, `127` append nothing, print `feed-publish: E_STEP_FAILED:
     <tool> exited <n>` and return 1; `--exit x` returns 2.
  2. `verdict` full table: `--dry-run` {true, false} × `--ref` {`refs/heads/main`, `refs/heads/other`} × `--build` ×
     `--deploy` (each `success`, `failure`, `cancelled`, `skipped`) × the three flags {`true`, `false`, `''`} × evidence
     {complete, missing `compare.txt`} = 1,728 rows, each against an **oracle written in the test from spec FR-029c's
     text** (not imported): exit 0 only for (dry run, any ref, build success, deploy success or skipped, no flag true,
     evidence complete) and (real run, `refs/heads/main`, build success, deploy success, no flag true, evidence complete);
     exit 1 otherwise. Evidence fixtures are written into a temp dir by the test: `releases.json`, `collect-report.json`
     (with a degraded `manager.message`), `gen-feed.log` (two `gen-feed: WARN …` lines and one other line),
     `live-versions.json`, `compare.txt` (one `drift … warn` line, one `same` line).
  3. Reason text: real run, `refs/heads/other`, build success, deploy skipped → stdout exactly `verdict: red real run
     from refs/heads/other` (not also a `deploy` reason), with `--annotate` plus `::error title=feed-verdict::real run
     from refs/heads/other`; `--drift-warned true` → `verdict: red v1 drift deployed: <the drift … warn lines>`;
     `--feed-degraded true` → `verdict: red feed degraded:` then each `gen-feed: WARN` line; `--manager-degraded true` →
     `verdict: red manager latest.json degraded: <manager.message>`; a missing evidence file → `verdict: red evidence
     missing <file>`, one per missing file in `EVIDENCE_FILES` order; reasons in plan PD-12 order; green prints
     `verdict: green` (with `--annotate` `::notice title=feed-verdict::green`).
  4. Usage: a flag value outside `true` / `false` / `''`, a result outside the four, `--dry-run maybe`, a missing or
     empty `--ref` → 2 with a `usage: node tools/feed-publish.js` line.
  5. PD-18 (c): evidence lines holding `\r`, starting `::`, and a `manager.message` holding `\n` are printed in their
     `JSON.stringify` form; across every subtest no stdout or stderr line starts with `::` except the tool's own
     annotations with one of the plan §4.1.6 fixed titles; every annotation is one line.
  6. `withGhRetry(stubGh, {sleep: rec, log: rec})` (plan §4.1.1, PD-4, PD-5): a stub returning statuses `[1, 0]` → one
     sleep `5000`, one log line `feed-publish: WARN gh attempt 1/3 failed: list (exit 1); retrying in 5 s` for a list
     argv, result `attempts: 2`; `[1, 1, 0]` → sleeps `[5000, 15000]`; `[1, 1, 1]` → three attempts, `status 1`,
     `attempts: 3`, two sleeps; `[0]` → no sleep, `attempts: 1`; a `view <tag>` / `download <tag>` argv logs
     `view <tag>` / `download <tag>`; for a download argv whose `--output` file the failed attempt left, the file is
     gone when the next attempt starts (the stub records `fs.existsSync` at each call); a spawn-error result
     (`status: -1`) is retried like a non-zero exit.

Run both: red (module missing). Record it.

**Expected red until T011:** T10 subtest (b) (it requires all five files; `feed-publish-compare.test.js` arrives in T008,
`feed-pipeline.test.js` in T011).

**Implement** `tools/feed-publish.js` per rule 5 and plan §4.1.1 (public API and constants, binding), §4.1.5
(`outcome`, `verdict` and its reason table), §4.1.6 (`annotate`), §4.1.7 (usage block and `parseArgs`, which follows
`tools/backfill-sign.js:785-822`: known subcommand, required flags once, optional at most once, `--annotate` a switch,
values non-empty and not starting `--` except the three verdict flags which accept `''`, `--repo` matching
`^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$`; a usage error prints the code line then the usage block and returns 2), and PD-18:
`emit(sink, line)` prints `JSON.stringify(line)` for a line holding U+0000-U+001F / U+007F or starting `::`;
`annotate` is the only writer of `::` lines and single-lines its message (`/[\r\n]+/gu` → space). `withGhRetry` and
`productionDeps` exactly as plan §4.1.1's code (spawnSync `gh`, `shell: false`, `stdio: ['ignore','pipe','pipe']`,
`maxBuffer` 64 MiB, a spawn error becomes `status: -1`, `sleep` = `Atomics.wait` on a fresh `SharedArrayBuffer`,
`genFeedMain: genFeed.main`). In this task `main` dispatches `outcome` and `verdict`; `collect`, `generate` and
`compare` are parsed by `parseArgs` and wired by T010, T009 and T008 (until then `main` returns 2 for them with the
usage block, which no test asserts as final behaviour). Exports as plan §4.1.1 lists them (the three later functions
are added by their tasks).

**Done when:** L-NODE for `feed-publish-steps.test.js` green except the named expected red; `node --check
tools/feed-publish.js`; `node tools/feed-publish.js` (no args, run once by hand) prints the usage block and exits 2; rule
6 holds for the tool.

### T006 [P] — `evidence/scope-check.js` (spec SC-012, FR-060 as amended, FR-032, FR-033, FR-062; plan §4.10)

**Files:** `specs/krate-manager-phase3/evidence/scope-check.js` (new).

**Test first.** Write `--self-test` first (form of `specs/krate-manager-phase2/evidence/scope-check.js:1-30`): the checks
run on synthetic inputs (status text, name lists, diff texts, the `4f4c4175` `release.yml` text) held in the script:
(1) a clean listing → pass; (2) `?? tools/stray.js` → offence naming it; (3) a `ci.yml` diff with a `-` line → offence;
(4) a `ci.yml` diff with any `+` line → offence (Q6 re-ruled: `ci.yml` is byte-identical); (5) a
`release.yml` diff that deletes a line outside the pre-image range 859-900 (e.g. `keep_files: true`) → offence; (6) a
`release.yml` `+` block differing from plan §4.4's by one character → offence; (7) a changed
`tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/README.md` → offence; (8) a changed
`tools/feed-contract/fixtures/phase2/backfill/state.json` → offence. Run `--self-test` before the checks exist: red.
Record it.

**Implement** (plan §4.10, binding, with the T001 amendments): base `4f4c4175`; `git -c core.quotepath=off status
--porcelain --untracked-files=all` and `git diff --name-only 4f4c4175`; allowed exact paths `.github/workflows/feed.yml`,
`.github/workflows/release.yml`, `.github/workflows/feed-tools.yml`, `tools/feed-publish.js`,
`plugins/shared/tests/CMakeLists.txt`, `plugins/shared/tests/test_update_checker.cpp`, `specs/INDEX.md`,
`specs/Krate-manager-roadmap.md`; allowed prefixes `tools/feed-contract/`, `specs/krate-manager-phase3/`; under
`tools/feed-contract/fixtures/` only `phase3/**`, `feeds/valid/one-v2-eight-v1.json`, `feeds/cases.json` and
`make-fixtures.js` may change; `git diff 4f4c4175 -- tools/gen-feed.js .github/workflows/docs.yml
.github/workflows/backfill-sign.yml` empty, and no `setup.iss` / `version.json` path in the listings; `ci.yml`
(`git diff --unified=0 4f4c4175 -- .github/workflows/ci.yml`): empty (Q6 re-ruled); `release.yml`: the `-` lines are exactly the
`git show 4f4c4175:.github/workflows/release.yml` lines 859-900 and the `+` lines exactly plan §4.4's block (blank line,
two comment lines, the `feed` job), no other hunk. Prints counts and every offence; last line `SCOPE GREEN` or
`SCOPE RED`; exit 0 / 1 / 2 (git error). It spawns `git` (evidence, not a test: the write guard would refuse it in a
test).

**Done when:** `node specs/krate-manager-phase3/evidence/scope-check.js --self-test` exit 0 (red first recorded);
`node --check` on it. Running it against the tree is T019's.

---

## G3 — Fixtures (sequential)

### T007 — `make-fixtures.js` registers set `phase3`; the fixtures are generated (spec FR-044, FR-045, SC-008; plan §4.7, PD-14)

> **Main-loop fix at the first T007 stop (2026-10-30):** `tests/c1-feed.test.js:235-243` (Phase 1) requires every
> `feeds/cases.json` row id to be in `C1_RULES`; the plan missed that, so `Phase3/one-v2-eight-v1` was added to
> `C1_VALID_RULES` (spec FR-045, plan PD-14). `C1 feed fixtures`: 11 pass, 0 fail. T007's own work was complete.

**Files:** `tools/feed-contract/fixtures/make-fixtures.js` (edit); generated, never hand-written:
`tools/feed-contract/fixtures/phase3/**`, `tools/feed-contract/fixtures/feeds/valid/one-v2-eight-v1.json`,
`tools/feed-contract/fixtures/feeds/cases.json`.

**Test first.** The red is T004's expected red: run L-NODE for `phase3-fixtures.test.js` (subtests 2, 3, 6, 7 red) and
L-FIX (exit 0 today: nothing knows the set). Record both.

**Implement** (plan §4.7 last paragraph, binding): `const phase3 = require('./phase3/make-phase3.js');`; the header line
"Requires only node: modules, ../contract.js and ../listing.js" (`make-fixtures.js:15`) gains "and, for the phase3 set,
../../gen-feed.js (through phase3/make-phase3.js)"; `SETS` (`:1197-1208`) gains `{ name: 'phase3', dirs: ['phase3'],
keep: /^phase3\/make-phase3\.js$/u, files: () => phase3.files({ fixtureSignature }) }`; `feeds()` (`:523-541`) pushes
`{ rule: 'Phase3/one-v2-eight-v1', file: 'valid/one-v2-eight-v1.json', schema: 'v2', expect: 'accept' }` after its two
loops, before `add('feeds/cases.json', …)`. Then run `node tools/feed-contract/fixtures/make-fixtures.js --out
tools/feed-contract/fixtures` once.

**Done when:** L-FIX exit 0; `git status --porcelain tools/feed-contract/fixtures` lists only `make-fixtures.js`,
`feeds/cases.json` (modified) and the new `phase3/` and `feeds/valid/one-v2-eight-v1.json` (any other modified fixture
is a builder defect: fix the builder, never the file); `git diff tools/feed-contract/fixtures/feeds/cases.json` adds
exactly the one row; L-NODE green for `phase3-fixtures.test.js` (all eight subtests), `c1-feed.test.js` (`C1 feed
fixtures` reads the new row), `hygiene.test.js` (`fixtures are in sync with make-fixtures.js`), then the whole set
(only T005's named expected red remains).

---

## G4 — Subcommands (sequential; each task edits `tools/feed-publish.js`)

### T008 — `compare` and `compareFeeds` (spec FR-027, FR-040, FR-041, SC-007; plan §4.2, PD-10, PD-11, PD-13, PD-18)

**Files:** `tools/feed-publish.js` (edit), `tools/feed-contract/tests/feed-publish-compare.test.js` (new).

**Test first.** `feed-publish-compare.test.js` (rule 1), top-level test `compare explains every v1 difference or fails`
(plan §7.1 T7, binding): one subtest per `fixtures/phase3/compare/cases.json` row, each run **twice**, without and with
`--annotate`, through `main(['compare','--live',…,'--feed',…,'--releases',…,'--dry-run',<row.dryRun>,'--report',<tmp>,
'--summary',<pre-filled tmp>, …], io, stubDeps)` (`stubDeps` = `{gh: () => { throw … }, genFeedMain: () => { throw … }}`:
`compare` uses neither): exact stdout (`row.stdout`, then `row.annotations` when annotating), exact stderr lines
(`row.stderr`), the exit code, the `--report` file equal to `row.stdout.join('\n') + '\n'` (empty on `E_INPUT` /
`E_FEED_SCHEMA`) on exit 0, 1 **and** 3, the `--summary` file equal to its prior content plus one fenced block (fence
`max(3, longest backtick run + 1)` backticks, so the `live-backtick` row's fence is four backticks); plus subtests
`--dry-run` missing and `--dry-run maybe` → 2; in every row, with `--annotate`, no stdout or stderr line starts with `::`
except the tool's own fixed-title annotation lines. Also one subtest that calls the exported `compareFeeds({liveDoc,
feedDoc, rows})` on the `same` and `mixed-kinds-one-plugin` triples and checks `counts` and `plugins[].drift`.

Run it: red (`compare` not wired, `main` returns 2). Record it.

**Implement** `compare` and `compareFeeds` per plan §4.2 (binding): read the three files (`E_INPUT`); `validateFeed`
v2 then v1 on `--feed` (`E_FEED_SCHEMA: <schema> <instancePath>: <message>`, `contract.js:478-486`); PD-8 row check on
`--releases`; `JSON.parse(genFeed.projectV1(doc))` on both (`tools/gen-feed.js:287-304`); PD-18 (a) refusal; the case
table (cases 1-6, first match wins) and the drift-kind table with severities; one drift line per plugin (PD-10); PD-11
(`gt` only on two `parseSemVer`-valid values, `contract.js:152-189`); PD-18 (b) printing rule; summary line; exit 0 / 1 /
3; stderr `feed-publish: E_V1_DRIFT: <line>` per failing drift and `feed-publish: WARN E_V1_DRIFT: <line>` per real-run
`warn` drift; `--report` written atomically (temp + rename, as `tools/backfill-sign.js:84-103`) whatever the exit code;
`--summary` appended (`fs.appendFileSync`); annotations `feed-validate`, `feed-compare`, `feed-drift` (`::error` for a
failing drift, `::warning` for a real-run `warn`). Wire `compare` into `main`; export `compare`, `compareFeeds`.

**Done when:** L-NODE green for `feed-publish-compare.test.js` and `feed-publish-steps.test.js`, then the whole set (T005's
named expected red remains until T011).

### T009 — `generate` (spec FR-029a, FR-009, SC-002 unit half, SC-018 part; plan §4.1.4, PD-9)

**Files:** `tools/feed-publish.js` (edit), `tools/feed-contract/tests/feed-publish-steps.test.js` (edit: subtests added
to `feed-publish decides every workflow branch`; no new top-level test).

**Test first.** New subtests of `feed-publish decides every workflow branch`, each on an in-dir the test writes itself
(`releases.json` from the Phase 2 backfill `state.json` rows, an empty `manifests/`, a `collect-report.json`; plugins
from `fixtures/phase2/backfill/plugins/`; repo `krate-fixture/backfill`), with `deps.genFeedMain` = `genFeed.main` or a
recording wrapper around it:
1. A `--site-dir` that does not exist is created; exit 0; `versions.json` written; `<in-dir>/gen-feed.log` holds
   gen-feed's stderr; `<in-dir>/versions.again.json` Buffer-equals `versions.json`.
2. gen-feed exit 1 and exit 2 (a wrapper returning them after writing nothing) → `generate` returns that code and no
   `<site-dir>/manager/` exists; exit 3 (a wrapper that runs the real `main` then returns 3 with a `gen-feed: WARN …`
   stderr line) → returns 3, `versions.json` written, the WARN line in `gen-feed.log` and echoed to stderr.
3. `manager.status: "copied"` with `<in-dir>/manager/latest.json` present → the wrapper saw `--manager-latest-url
   https://krateaudio.com/manager/latest.json` in both runs' argv, `<site-dir>/manager/latest.json` Buffer-equals the
   in-dir file; `"none"` and `"degraded"` → no such flag, no `<site-dir>/manager`.
4. A pre-created `<site-dir>/manager/sentinel` on a `copied` report → exit 1, first stderr line `feed-publish:
   E_INPUT:`, the sentinel byte-identical, no `latest.json` beside it, `genFeedMain` never called.
5. Non-determinism (PD-9): a wrapper whose second call writes different bytes → `E_FEED_NONDETERMINISTIC`, exit 1;
   whose second call returns a different exit code → same; whose second call prints a different stderr → same; in each,
   no `<site-dir>/manager` left.
6. A `collect-report.json` that is missing, not JSON, or has `manager.status: "other"` → `E_INPUT`, exit 1.
7. `--annotate`: `::notice title=feed-generate::exit <n>, deterministic` and one `::warning title=feed-degrade::<WARN
   line>` per WARN line, single-lined even when the wrapper's stderr line holds an embedded `\r`.

Run them: red (`generate` not wired). Record it.

**Implement** `generate` per plan §4.1.4 steps 1-8 (binding): parse the report; `mkdirSync(siteDir, {recursive:
true})` (gen-feed does no `mkdir`, `tools/gen-feed.js:174-190`); refuse an existing `<siteDir>/manager`; argv with
`--manager-latest-url` exactly when `copied`; run 1 with a capturing io, `gen-feed.log` written atomically and echoed
through `emit`; return on 1 / 2; run 2 into `<inDir>/versions.again.json` with equal code, stderr and bytes
(`Buffer.equals`); stage `manager/latest.json` with `COPYFILE_EXCL`, removing a directory this run created on any `fs`
error (`E_INPUT`); annotations; return `code1`. Wire into `main`; export `generate`.

**Done when:** L-NODE green for `feed-publish-steps.test.js`, then the whole set (T005's named expected red remains).

### T010 — `collect` (spec FR-021 to FR-026, FR-023a, SC-003, SC-004, SC-006; plan §4.1.2, §4.1.3, PD-6, PD-7, PD-8, PD-17)

**Files:** `tools/feed-publish.js` (edit), `tools/feed-contract/tests/feed-publish-collect.test.js` (edit: T003's file).

**Test first.** In `feed-publish-collect.test.js` (plan §7.1 T3, T4, T6, binding; every world from `fake-gh.js`
`makeWorld`, `deps = {gh: <world gh, or withGhRetry(flaky(world gh), {sleep: record, log})>, genFeedMain: genFeed.main}`;
`flaky` is test-local per plan §4.8 last bullet; never `productionDeps`):
- In `collect downloads only the selected manifests` (T3), beside T003's subtest: Phase 2 world with `addPhase2Files`
  on a chosen subset of stable, sibling, an older release and a draft, plus `addManagerRelease` with a valid
  `latest.json`; the expected argv list is **written before the run** (1 list with `--limit 1000 --json
  tagName,isPrerelease,isDraft,publishedAt`; per plugin bytewise by name one `view <tag> --repo <repo> --json
  tagName,assets` per selected release followed by one `download <tag> --repo <repo> --pattern release.json --output
  <path.join(outDir, 'manifests', tag.replace('/', '-') + '.json')>` where it lists `release.json`; then the manager view
  and `download … --pattern latest.json --output <path.join(outDir, 'manager', 'latest.json')>`) and `deepStrictEqual`s
  the recorded `gh` calls; the downloaded manifests are exactly those of `selectReleases`' stable and sibling tags that
  list `release.json` (per plugin); no argv holds `--clobber`, `api` or a host name; `gh_calls` in
  `collect-report.json` equals the list length; a no-manager world records no manager call; the report shape is plan
  §4.1.3's (plugins bytewise, `manifests` stable then sibling, `manager`, `gh_calls`, 2-space JSON, final LF); a
  `digest: null` asset record on one `release.json` → per the `[P-2]` ruling: (a) accepted on size, stdout `collect:
  digest checked <n-1> of <n>`; (b) `E_FEED_DOWNLOAD`, exit 1.
- `collect aborts cleanly` (T4): each FR-023 abort (list exit 1 after three attempts, a non-array list, 1000 rows →
  `E_FEED_LIST_TRUNCATED`, a view for another tag, a truncated download → size, a digest mismatch → `E_FEED_DOWNLOAD`,
  an `E_TAG_FORMAT` tag, PD-7's invalid name, duplicate-after-lower-case and `manager` name) → `main` 1, first stderr
  line `feed-publish: <CODE>:`, the out-dir listing after equals the listing before (none of the four names, no temp
  file); usage → 2 with the usage text; a pre-filled out-dir subtest per name (`releases.json`, `manifests`, `manager`,
  `collect-report.json`, each pre-created with sentinel content) → 1, first stderr line `feed-publish: E_INPUT:`, zero
  recorded `gh` calls, the sentinel byte-identical; retry subtests through `withGhRetry` with a recording `sleep`:
  (a) a list, a view and a download each failing once then succeeding → `collect` 0, the failing argv recorded twice then
  the T3 list (written before the run), `sleep` `[5000]`, exactly one `feed-publish: WARN gh attempt 1/3 failed: …`
  stderr line, `gh_calls` counting both attempts, and for the download the failed attempt's partial file gone before the
  retry; (b) failing twice → `[5000, 15000]`, two WARN lines; (c) three times → 1, `feed-publish: E_INPUT: …` naming the
  subcommand, the tag and `3 attempts`, the out-dir clean, exactly three attempts; (d) a view for another tag, a size
  mismatch, a digest mismatch, a non-array list and 1000 rows each attempted exactly once, `sleep` never called.
- `collect copies only a valid manager latest.json` (T6): none / prerelease-only / draft-only manager → `manager:
  none`, no `manager/`, and `generate` (T009) on the result passes no `--manager-latest-url` (a recording `genFeedMain`
  wrapper sees the argv) and the feed has no `manager` block; tag-pattern rows (each non-draft, non-prerelease, valid
  `latest.json`) `manager/v1.2.0-rc.1`, `manager/v01.0.0`, `managerx/v9.0.0` each alone → ignored, no manager view
  recorded; `manager/v0.9.0` with `manager/v0.10.0` listed in both orders → `v0.10.0` viewed and copied; two valid
  releases → the higher copied byte-equal, `generate` stages it and emits `manager.latest_json_url ===
  "https://krateaudio.com/manager/latest.json"`; each FR-025 failure (not JSON, version ≠ tag, no `platforms`, empty
  `platforms`, a platform without `signature`, an `http://` URL, a `/releases/latest/` URL) → exit 3, a `feed-publish:
  WARN E_MANAGER_LATEST: …` line, no `manager/` in the out-dir, report `"degraded"`, every other output written, and
  `generate` then stages no `manager/` and passes no URL. `--annotate` → `::notice title=feed-collect::<n> downloaded,
  <n> no-manifest, <n> no-stable, manager <status>`, single-lined.

Run them: red (`collect` not wired). Record it.

**Implement** `collect` per plan §4.1.2 steps 1-8 and §4.1.3 (binding): PD-7 name checks with
`genFeed.readPluginsDir(pluginsDir, {withSetupIss: false})`; out-dir refusal, then `try { … complete = true } finally {
if (!complete) removeOwn() }`; `ghJson` and the PD-8 shape checks (copied from `tools/backfill-sign.js:149-172`, not
imported); `rows.length >= 1000` → `E_FEED_LIST_TRUNCATED`; all `selectReleases` picks before any view; per plugin view
and download with `checkDownloaded` (size; digest when a string, per `[P-2]` when null; `DIGEST_RE` on the lower-cased
digest as `tools/backfill-sign.js:315`); the manager release (`MANAGER_TAG_RE`, highest by `compareSemVer`) and
`checkLatestJson` (FR-025; `E_URL_*` from `contract.checkUrl` re-thrown as `E_MANAGER_LATEST`, `contract.js:203-213`);
`releases.json` and `collect-report.json` written atomically last; stdout lines through `emit`; `gh_calls` = the sum of
`attempts`. A `gh` result with `status !== 0` (after `withGhRetry`'s attempts) is `E_INPUT … after <n> attempts`. Wire
into `main`; export `collect`.

**Done when:** L-NODE green for `feed-publish-collect.test.js`, `feed-publish-steps.test.js`, `backfill-sign.test.js`,
then the whole set (T005's named expected red remains until T011).

### T011 — The pipeline tests (spec SC-001, SC-002, SC-005, FR-008; plan §7.1 T1, T2, T5)

**Files:** `tools/feed-contract/tests/feed-pipeline.test.js` (new). If a red names a defect in `tools/feed-publish.js`,
fix it there and say so in the notes (that file is in this sequential group).

**Test first / implement** (this task's product is the test; the code exists). `feed-pipeline.test.js` (rule 1), three
top-level tests (exact names, plan §7.1, binding):
- `feed pipeline reproduces the live v1 feed from the 2026-10-29 snapshot`: `makeWorld({tmp, world: 'snapshot'})`;
  `collect --plugins-dir <snapshot plugins> --repo rolandzwaga/krate-audio --out-dir <tmp>/in` → 0; `releases.json`
  deep-equals the snapshot's; `manifests/` exists and is empty; the report lists nine `no-manifest` plugins and
  `manager: none`; the recorded calls equal exactly 1 list + 9 `view <tag> --repo rolandzwaga/krate-audio --json
  tagName,assets` in bytewise plugin-name order (the stable tags read from the snapshot rows with `selectReleases`), 0
  downloads, `gh_calls === 10`; `generate` into a non-existent `<tmp>/site` → 0, directory created; `genFeed.projectV1`
  of `versions.json` Buffer-equals `live-versions.json`; `compare --dry-run true` against `live-versions.json` → nine
  `same` lines, exit 0.
- `feed pipeline is deterministic`: two full collect + generate runs in two temp dirs on the same Phase 2 world
  (`addPhase2Files` on several releases, one manager release with a valid `latest.json`) → byte-equal `releases.json`,
  identical manifest names and bytes, byte-equal `versions.json` and `manager/latest.json` (non-vacuous: the run creates
  `<site-dir>/manager/latest.json`); then on the same world a `genFeedMain` wrapper that rewrites `<in>/releases.json`
  between its two calls → `E_FEED_NONDETERMINISTIC`, exit 1, no `<site-dir>/manager`.
- `a release without release.json stays v1-only`: Phase 2 world with `addPhase2Files` on every selected release except
  one plugin's stable → that plugin `no-manifest`, the others `downloaded`; `generate` 0; `gen-feed.log` holds no `WARN`
  line; that entry has no `platforms` / `install` and its v1 keys equal `{version: <selectReleases stable version>,
  download_url: <its fixture version.json url>, release_notes: ''}` computed from the state and the fixture; every other
  plugin has both `platforms` and `install`.

Red first: run the file once with the T1 expected call list deliberately wrong by one view (e.g. reversed order) and
record the red, then correct it. **Done when:** L-NODE green for `feed-pipeline.test.js` and `feed-publish-steps.test.js`
(T005's expected red T10 (b) is now green: all five files exist), then the whole set `# fail 0`.

---

## G5 — Workflows (sequential; each workflow is a shared file)

### T012 — `.github/workflows/feed.yml` and the Phase 3 shape test (spec FR-001 to FR-015, SC-010; plan §4.3, PD-1, PD-2)

**Files:** `.github/workflows/feed.yml` (new), `tools/feed-contract/tests/workflow-shape.test.js` (edit: one new
top-level test after the Phase 2 ones; no Phase 2 line changes).

**Test first.** New top-level test `feed.yml and release.yml keep the Phase 3 shape` (plan §7.1 T9, binding), built on
the file's existing helpers (`readWorkflow`, `jobNames`, `jobBlock`, `stepBlocks`, `field`, `nested`, `runText`,
`stepByName`, `topLevel`, `jobPermissions`; `tests/workflow-shape.test.js:18-216`), subtests for every SC-010 clause:
1. triggers exactly `workflow_call` and `workflow_dispatch`, each with the single input `dry_run` (`type: boolean`;
   call `required: true`, no `default`; dispatch `default: true`); no `push` / `pull_request` / `schedule` line;
2. top-level `permissions` is `contents: read`; no top-level `concurrency`; `build` and `deploy` each carry exactly
   `group: krate-feed`, `cancel-in-progress: false`, `queue: max`; `verdict` has none; no other file in
   `.github/workflows/` contains `group: krate-feed` (PD-1, as amended by T001);
3. jobs exactly `build`, `deploy`, `verdict`; only `deploy` holds `contents: write`; no `environment:` key and no
   `secrets.` anywhere; no shipped plugin name (the nine from the snapshot `version.json` files, case-insensitive);
4. `build`: `runs-on: ubuntu-latest`, `env` exactly `GH_TOKEN: ${{ github.token }}`, `outputs` mapping
   `manager_degraded` / `feed_degraded` / `drift_warned` to `steps.collect` / `steps.generate` / `steps.compare`
   outputs; steps exactly, in order, `Checkout` (`actions/checkout@v6`, no `with`, so no `ref` and no `fetch-depth`),
   `Setup Node` (`actions/setup-node@v4`, Node `'24'`), `Install feed-contract deps`, `Collect releases` (`id:
   collect`), `Fetch live feed`, `Generate feed` (`id: generate`), `Compare with the live feed` (`id: compare`,
   `env: DRY_RUN: ${{ inputs.dry_run }}`), `Upload site`, `Upload evidence`;
5. every `run:` step of `build` and `verdict` has `shell: bash`, its script's first non-empty line is `set -euo
   pipefail`, and no script matches `/(?<!\|)\|(?!\|)/`;
6. the three capture steps are exactly `rc=0; node tools/feed-publish.js <sub> … || rc=$?` then `node
   tools/feed-publish.js outcome --tool <sub> --exit "$rc" --github-output "$GITHUB_OUTPUT"`, with the plan §4.3.1 command
   lines verbatim (`--repo "$GITHUB_REPOSITORY"`; compare with `--dry-run "$DRY_RUN"`, `--report
   "$RUNNER_TEMP/feed-in/compare.txt"`, `--summary "$GITHUB_STEP_SUMMARY"`, `--annotate`); the `Fetch live feed` script
   is exactly `set -euo pipefail` plus the one `curl --fail --silent --show-error --location --retry 3
   https://krateaudio.com/versions.json --output "$RUNNER_TEMP/feed-in/live-versions.json"` line; no step outside
   `Generate feed` names `_site/manager` or runs `mkdir` / `cmp`;
7. `Upload site`: `actions/upload-artifact@v7`, `name: feed-site`, `path: _site/`, `if-no-files-found: error`, no `if:`;
   `Upload evidence`: `if: always()`, `name: feed-evidence`, `path: ${{ runner.temp }}/feed-in/`, `if-no-files-found:
   warn`;
8. `deploy`: `needs: build`, `if: ${{ !inputs.dry_run && github.ref == 'refs/heads/main' }}`, `runs-on:
   ubuntu-latest`, no `run:` and no checkout; steps exactly `Download site` (`actions/download-artifact@v8`, `name:
   feed-site`, `path: _site`) and `Deploy to gh-pages` (`peaceiris/actions-gh-pages@v4`, `github_token: ${{ github.token
   }}`, `publish_dir: ./_site`, `keep_files: true`, `cname: krateaudio.com`, no `allow_empty_commit`, no `if:`);
9. `verdict`: `needs: [build, deploy]`, `if: always()`, `permissions: contents: read`; steps in order `Checkout`,
   `Setup Node`, `Install feed-contract deps`, `Download evidence` (`actions/download-artifact@v8`, `continue-on-error:
   true`, `name: feed-evidence`, `path: ${{ runner.temp }}/evidence`), `Verdict` whose script is the plan §4.3.1 line and
   whose seven values (`DRY_RUN`, `REF`, `BUILD_RESULT`, `DEPLOY_RESULT`, `FEED_DEGRADED`, `MANAGER_DEGRADED`,
   `DRIFT_WARNED`) come only through `env:` from the plan §4.3.1 expressions (no `${{` inside the script);
10. `release.yml`: the FR-030 `feed` job exactly (plan §4.4 block: `needs: [release, deploy-pages]`, `if: ${{
    !cancelled() && needs.release.result == 'success' }}`, `permissions: contents: write`, `uses:
    ./.github/workflows/feed.yml`, `with: dry_run: ${{ inputs.dry_run }}`, no `secrets:`, no `concurrency:`), placed
    after `deploy-pages`; no step named `Generate Update Checker Endpoint`; no line writing `versions.json`
    (`versions.json` appears only in the `feed` job's comment); `deploy-pages` steps exactly `['Checkout', 'Generate
    Plugin Landing Page', 'Generate Signal Flow Diagram', 'Generate Root Index', 'Deploy to gh-pages']` with its `if: ${{
    !inputs.dry_run }}` and `keep_files: true` kept;
11. `ci.yml`: the `shared:` block of the `dorny/paths-filter` `filters:` text is still exactly the one line
    `- 'plugins/shared/**'` (Q6 re-ruled: `ci.yml` is not changed; spec FR-062).

Run it: red (`feed.yml` missing). Record it.

**Expected red until T013:** subtest 10. **Expected red until T014:** subtest 11.

**Implement** `.github/workflows/feed.yml` as plan §4.3.1's full text (binding), LF, rule 12.

**Done when:** L-YAML on `feed.yml` exit 0; L-NODE for `workflow-shape.test.js`: the Phase 2 test green unchanged
(SC-016) and the Phase 3 test green except subtests 10 and 11; `git diff -U0 tools/feed-contract/tests/workflow-shape.test.js`
removes no line; L-LINT exit 0.

### T013 — `release.yml`: drop the inline feed step, add the `feed` job (spec FR-030 to FR-033; plan §4.4)

**Files:** `.github/workflows/release.yml` (edit).

**Test first.** T012's subtest 10 is the red (no `feed` job, the inline step present). Record it.

**Implement** (plan §4.4, binding; rule 11, the file is CRLF in the working tree): delete from the line `      - name:
Generate Update Checker Endpoint` (`release.yml:859`) through the blank line before `      - name: Deploy to gh-pages`
(`:900`); append after the last line (`:906`, `          keep_files: true`) the plan §4.4 block: one blank line, the two
comment lines, and the `feed` job. Nothing else changes.

**Done when:** L-YAML on `release.yml` exit 0; L-NODE for `workflow-shape.test.js`: Phase 2 test green unchanged
(SC-016: "deploy-pages is skipped on dry runs", "dry_run boolean input, default false, and no other new input", the
`continue-on-error` ban on `release.yml`), Phase 3 subtests 10 and 11 green; `node
specs/krate-manager-phase3/evidence/scope-check.js` prints no offence (any offence it prints is fixed now);
`grep -c $'\r$' .github/workflows/release.yml` equals `wc -l < .github/workflows/release.yml`.

### T014 — `ci.yml` stays unchanged (spec FR-062, Q6 re-ruled (a))

**Files:** none (verification only; do NOT edit `.github/workflows/ci.yml`).

**Why:** a true `shared` output in `ci.yml` builds and tests all nine plugins on every OS (plan R-1), so the feed
fixtures are not routed through it (spec Clarifications, Session 2026-10-30 (plan), Q6 again).

**Done when:** `git diff 4f4c4175 -- .github/workflows/ci.yml` is empty; L-NODE for `workflow-shape.test.js` fully
green, including T012's subtest 11 (`shared:` still exactly `- 'plugins/shared/**'`); L-SCOPE reports no `ci.yml` or
`release.yml` offence.

---

## G6 — C++ guard (sequential; shared CMake)

### T015 — `SHARED_FIXTURES_DIR` and the one-v2 / eight-v1 case (spec FR-046, FR-054, SC-009; plan §4.9)

**Files:** `plugins/shared/tests/test_update_checker.cpp` (edit: includes + one case at the end),
`plugins/shared/tests/CMakeLists.txt` (edit: one `target_compile_definitions` block). Reads only the fixtures T007
committed. No header and no non-test source changes.

**Test first.** Add to `test_update_checker.cpp` `#include <nlohmann/json.hpp>`, `<iterator>` and `<vector>` among the
includes (`:1-13`; the file includes none of them today) and, at the end, plan §4.9's `#ifndef SHARED_FIXTURES_DIR /
#error` guard, `readFixture` and `TEST_CASE("UpdateChecker - one v2 plugin among eight v1 plugins is read as v1",
"[update]")` verbatim (binding: names from the live file's `plugins` keys with `nlohmann::json`, `REQUIRE(names.size()
== 9)`, one `ScopedSettingsDirRedirect` around the loop, per name `REQUIRE(isUnder(getAppSettingsDirectory(name),
settings.root()))` before the first `MockUpdateChecker`, the live run, the state path re-derived and `REQUIRE`d under the
temp root and existing, removed, the mixed run, the state path `REQUIRE`d again, the six `CHECK`s, then
`CHECK(readEnv(kSettingsEnvVar) == envBefore)` after the scope). Build with L-CXX's build command **before** touching
`CMakeLists.txt`: the build fails with `SHARED_FIXTURES_DIR is defined by plugins/shared/tests/CMakeLists.txt` (the
recorded red; quote the line from `f:/tmp/km3-cxx-build.log`).

**Implement:** in `plugins/shared/tests/CMakeLists.txt` after `target_compile_features(shared_tests PRIVATE cxx_std_20)`
(`:66`), plan §4.9's comment line and `target_compile_definitions(shared_tests PRIVATE
SHARED_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tools/feed-contract/fixtures")` block (rule 11: CRLF). Rebuild.

**Run (each short; record the wall clock of each):**
1. L-CXX build: exit 0, zero `warning` lines in `f:/tmp/km3-cxx-build.log`.
2. L-CXX gate: last line `GATE GREEN` (case count one higher than Phase 1's 8, `specs/krate-manager-phase1/evidence/close-lane.log` row B1).
3. The one direct single-case run CLAUDE.md permits: `build/windows-x64-release/bin/Release/shared_tests.exe
   "UpdateChecker - one v2 plugin among eight v1 plugins is read as v1" -s > f:/tmp/km3-sc009.log 2>&1`; from the log
   record: the expansion `9 == 9`; each per-name assertion (the `isUnder(settingsDir, settings.root())` guard, the two
   post-check `isUnder` / `exists` pairs, `latestVersion`, `downloadUrl`, `releaseNotes`) exactly nine times (count the
   expression lines with `grep -c`); the totals line `test cases: 1 | 1 passed` (or `All tests passed`) with 0 failed
   assertions.
4. `node tools/header-consumers.js --tag '[update]'`: last line ends `none`.

If the build in step 1 runs longer than ~5 minutes (a full regeneration of the Visual Studio tree), stop waiting for it in
this task: record the elapsed time and return done with notes "deferred to the main loop at close: run `node
specs/krate-manager-phase3/evidence/close-lane.js`" (T019's lane repeats steps 1-4). Clang-tidy, the Ninja reconfigure,
`check-portability` and the real-`update_state.json` stat are L-CLOSE rows (T019), not this task's.

**Done when:** steps 1-4 pass with the counts recorded; `git diff -U0 plugins/shared/tests/` shows only additions (the
includes, the case, the CMake block); `grep -c $'\r$'` equals `wc -l` for both files.

---

## G7 — Integration (sequential)

### T016 — Hygiene: scan lists, `CHILD_EXEMPT`, roster, and subtest (d) (spec FR-051, SC-011; plan §7.1 hygiene paragraph, PD-16, TA-4)

**Files:** `tools/feed-contract/tests/hygiene.test.js` (edit).

**Test first.** (1) Add `path.join(TOOLS_DIR, 'feed-publish.js')` and `path.join(CONTRACT_DIR, 'fixtures', 'phase3',
'make-phase3.js')` to `scannedFiles()`'s `single` list (`hygiene.test.js:35-43`) and to the completeness list of subtest
(a) (`:116-128`), **without** touching `CHILD_EXEMPT`; run L-NODE for `hygiene.test.js`: subtest (a) fails with
`tools/feed-publish.js: child process module` (its `productionDeps` requires `node:child_process`). Record that red.
(2) Add `'tools/feed-publish.js'` to `CHILD_EXEMPT` (`:69-75`) and `path.join(TOOLS_DIR, 'feed-publish.js')` to the roster
`products` list (`:258-269`). (3) New subtest `(d) tools/feed-publish.js follows the tools/*.js form` of `tools stay
offline` (after `(c)`, `:155`; TA-4): first line exactly `#!/usr/bin/env node`; `typeof main === 'function'`;
`main([], io, stubDeps) === 2` with a stderr line starting `usage: node tools/feed-publish.js` (`stubDeps` throws if
used); every `'E_[A-Z0-9_]+'` literal in the source is a `CODES` key; the seven T002 codes are `CODES` keys with their
plan §3.1 class. Red first for (3): run it once with the tool path misspelled (`MODULE_NOT_FOUND`), then fix (record
both).

**Implement:** test-only change; a red that names a real defect is fixed **in the tool**, never by loosening the test.

**Done when:** L-NODE green for `hygiene.test.js` (top-level test names unchanged, SC-011); `git diff -U0
tools/feed-contract/tests/hygiene.test.js` shows additions only; the whole set green.

### T017 — `CONTRACT.md` §10 "Phase 3 tools" (spec FR-053, FR-061; plan §4.6)

**Files:** `tools/feed-contract/CONTRACT.md` (edit: append after §9, whose last line is `:778`).

**Test first.** `grep -c '^## 10. Phase 3 tools$' tools/feed-contract/CONTRACT.md` prints `0` (the recorded red). The
regressions that must stay green: `CONTRACT.md documents every code and field` (§7 only, `hygiene.test.js:385-420`), the
heading rules (the eight `CONTRACT_HEADINGS` stay unchanged, `:315-324`), `no serial test flags` (`:291-305`).

**Implement** `## 10. Phase 3 tools` with 10.1 to 10.7 per plan §4.6 (binding): the command lines (plan §4.1.7) and the
job graph (build → deploy → verdict; deploy only on a real run from `refs/heads/main`; `krate-feed` with `queue: max`
on `build` and `deploy`); the plan §3.2 exit-code table as a table (never `- \`0\`: …` items, so nothing resembles §7's
list); `collect`'s request sequence, the gh retry (3 attempts, 5 s / 15 s, never on a decided result), the
`collect-report.json` shape (plan §4.1.3), the FR-025 `latest.json` check; the `outcome` table and the `verdict` reason
table (plan §4.1.5); the comparison rule (plan §4.2) with the severity table and, per `[P-3]`, the non-self-healing note
and hand-commit recovery; the annotation titles (plan §4.1.6); the user steps (after the green dry run the live dispatch
and banner check; after the live backfill a dry run then a live run, FR-061; a failed run is recovered by a fresh
dispatch, not "Re-run jobs", plan §9). No serial flag text, no plugin name.

**Done when:** the `grep -c` prints `1`; L-NODE green for `hygiene.test.js`, then the whole set.

### T018 — `feed-tools.yml`: path filters and the TAP plan (spec FR-052; plan §4.5)

**Files:** `.github/workflows/feed-tools.yml` (edit).

**Test first.** Run the whole set to `f:/tmp/km3-all.tap`, then L-TAP: it fails with `top-level plan ["1..57"], want
1..46` (the recorded red; the heredoc still says `const PLAN = "1..46";`, `feed-tools.yml:74`).

**Implement** (plan §4.5, binding): after `:16` and after `:29` (the last line of each `paths:` list, `-
'.github/workflows/backfill-sign.yml'`), the two lines `      - 'tools/feed-publish.js'` and `      -
'.github/workflows/feed.yml'`; `const PLAN = "1..57";`; append to `NAMES` (before `:98`'s `];`) the eleven plan §7.1 names
in T1-T11 order: `feed pipeline reproduces the live v1 feed from the 2026-10-29 snapshot`, `feed pipeline is
deterministic`, `collect downloads only the selected manifests`, `collect aborts cleanly`, `a release without
release.json stays v1-only`, `collect copies only a valid manager latest.json`, `compare explains every v1 difference or
fails`, `Phase 3 mixed feed fixture is real-shaped`, `feed.yml and release.yml keep the Phase 3 shape`, `feed-publish
tests never build real effects`, `feed-publish decides every workflow branch` (none holds an apostrophe, so the
`<<'TAPCHECK'` heredoc needs no quoting change). The `RSIGN` list and every other line stay. Keep the file's line
endings (rule 11).

**Done when:** L-YAML on `feed-tools.yml` exit 0; L-TAP exit 0 printing `TAP plan=1..57 top-level=57`; `grep -n
'test-concurrency\|test-isolation' .github/workflows/feed-tools.yml` prints nothing; L-LINT exit 0.

### T019 — Specs index, the close lane, the short close run and the PENDING-CI list (spec SC-012, SC-013w, SC-016, FR-054, FR-060; plan §7.4, §8)

**Files:** `specs/INDEX.md` (regenerated), `specs/krate-manager-phase3/evidence/close-lane.js` (new).

**Test first.** `node tools/gen-specs-index.js --check` exits 1 (`specs/INDEX.md is stale`, measured 2026-10-30 with
`specs/krate-manager-phase3/` present): the recorded red. Then `node tools/gen-specs-index.js` and `--check` → 0.

**Write `close-lane.js`** in the form of `specs/krate-manager-phase1/evidence/close-lane.js` (read it first: header,
`run()` with a per-child wall-clock ceiling and `killTree`, `record()`, a summary file, last line `CLOSE LANE GREEN |
CLOSE LANE RED`, exit 0 / 1; raw logs under `F:/tmp/gate/krate-manager-phase3-close/<timestamp>/`, summary
`specs/krate-manager-phase3/evidence/close-lane.log`). Rows:
- **A (concurrent):** A1 L-CXX build (`-- /nr:false`, 30 min ceiling, 0 warnings); A2 `node --test --test-reporter=tap
  "tools/feed-contract/tests/*.test.js"` to a TAP file, with `git status --porcelain` captured before and after and
  required equal (the tests write nothing in the repo), then A2t L-TAP on it (`TAP plan=1..57 top-level=57`); A3 L-YAML
  on `feed.yml`, `release.yml`, `feed-tools.yml`, `ci.yml`; A4 `node tools/header-consumers.js --tag '[update]'` (last
  line ends `none`); A5 L-LINT; A6 `node tools/check-portability.js plugins/shared/tests/test_update_checker.cpp` (explicit
  file: its default diffs against `origin/main`, which would sweep in Phases 1-2); A7 `node tools/gen-specs-index.js
  --check`; A8 L-FIX.
- **B (after A1 succeeds):** B0 stat (mtime, size, or absent) of the nine real `%APPDATA%\Krate Audio\<Name>\update_state.json`
  files, names read from `fixtures/gen-feed/snapshot-2026-10-29/live-versions.json` (spec Safety "What tests must never
  touch"; the Phase 1 lane's SC-021 row did this for two names); B1 the L-CXX gate; B2 the single case by name with
  `-s` to a log, asserting the T015 step 3 counts (`9 == 9`, nine of each per-name assertion, 1 passed, 0 failed); B3
  the stat again, equal to B0; beside B1-B3, B4: reconfigure `build/windows-ninja` (`cmake --preset windows-ninja` inside
  the VS developer environment, located with `vswhere.exe -latest -property installationPath` and
  `VC\Auxiliary\Build\vcvars64.bat`; the `clang-tidy-setup` skill's step 3), then require the
  `compile_commands.json` entry of `plugins/shared/tests/test_update_checker.cpp` to contain `SHARED_FIXTURES_DIR`
  (else RED "stale compile database"; never pass the define with `--extra-arg`), then `C:/Program
  Files/LLVM/bin/clang-tidy.exe -p build/windows-ninja plugins/shared/tests/test_update_checker.cpp` (15 min ceiling) with
  0 `warning:` / `error:` lines for that TU (TA-3; filter Catch2-internal lines as the Phase 1 lane's B4 row does). If
  the VS environment cannot be entered headless, B4 is RED with the reason and the main loop runs the skill's command
  from a Developer PowerShell.
- **C (last):** `scope-check.js --self-test`, then `scope-check.js` (L-SCOPE).

**Run in this task (each short; record the wall clock and the last result line of each):** the whole set to
`f:/tmp/km3-all.tap` (`# tests`, `# pass`, `# fail 0`); L-TAP; L-FIX; L-YAML on the four workflow files; L-LINT; `node
tools/gen-specs-index.js --check`; `scope-check.js --self-test` and `scope-check.js`; `node --check` on both evidence
scripts. Not run here, with the reason in the notes: the L-CXX rows, `check-portability`, clang-tidy and the stat rows
(deferred to the main loop at close: run `node specs/krate-manager-phase3/evidence/close-lane.js`); `node
tools/lint-plugin-editor-baseline.js` (no plugin editor changed); pluginval (test-only C++ change, FR-054); the CPU lane
(no timing test). Do **not** run `specs/krate-manager-phase2/evidence/tap-check.js --self-test`: its synthetic cases are
pinned to Phase 2's `1..46` (`tap-check.js:139-152`) and it is a closed phase's evidence; L-TAP uses its file mode only.
Then `node tools/clean-test-output.js --apply`.

**Done when:** every command above passes; `close-lane.js` passes `node --check` and its header lists every row above;
the notes say "deferred to the main loop at close: run `node specs/krate-manager-phase3/evidence/close-lane.js`"; the
PENDING-CI table below matches the job names in `.github/workflows/feed-tools.yml` (`node-test (${{ matrix.os }})`,
`:37`, matrix `:42`), `ci.yml` (`macOS (Clang)` `:562`, `Linux (GCC)` `:1061`, `macOS tests (shard N)` `:1405`, `Linux
tests (shard N)` `:1442`) and `feed.yml` / `release.yml` (`deploy-pages` displays as `Deploy Landing Page`,
`release.yml:745`).

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

`feed-tools.yml` runs on the user's push of the feature branch (its path filter lists `tools/feed-contract/**` and, after
T018, `tools/feed-publish.js` and `feed.yml`). `ci.yml` runs on the user's pull request to `main` (it touches
`plugins/shared/**`). `feed.yml` and `release.yml` can be dispatched only **after the merge to `main`**
(`workflow_dispatch` runs only the default branch's file, spec External facts).

| SC | Workflow · job (exact name) · step | Assertion that closes the row |
|---|---|---|
| SC-013m, SC-013l | `feed-tools.yml` · `node-test (macos-latest)`, `node-test (ubuntu-latest)` · `node --test` | the heredoc TAP check prints `TAP plan=1..57 top-level=57`, `# fail 0`, the eleven Phase 3 names `ok`; step `Checkout unchanged` empty |
| SC-009m, SC-009l | `ci.yml` · `macOS (Clang)` / `Linux (GCC)` (build `shared_tests`), run by `macOS tests (shard N)` / `Linux tests (shard N)` · the user's PR to `main` | the shard holding `UpdateChecker - one v2 plugin among eight v1 plugins is read as v1` reports 0 failed (proves `SHARED_FIXTURES_DIR` and the fixture read on both OSes). Known gap (SI-11, stated, not closed): a later push touching only `tools/feed-contract/fixtures/{feeds,gen-feed}/**` starts no `ci.yml` run (`ci.yml:3-23`); the fixture data itself is proven on all three OSes by `Phase 3 mixed feed fixture is real-shaped` in `feed-tools.yml` |
| SC-014 | `feed.yml` · `workflow_dispatch`, `dry_run: true`, on `main` · jobs `build`, `deploy`, `verdict` | jobs API (`gh api repos/rolandzwaga/krate-audio/actions/runs/<id>/jobs`): `build` success, `deploy` skipped, `verdict` success; check-run annotations of `build`: `feed-validate` `v2 ok, v1 ok`, `feed-compare` `compare: <n> same, <n> newer, <n> added, <n> q9-corrected, 0 drift (0 block, 0 warn)` with the four counts summing to the plugin count, `feed-collect` ending `manager none`, no `feed-drift` and no `feed-degrade`; `verdict`'s `feed-verdict` `green`; artifacts `feed-site` and `feed-evidence` listed; no `gh-pages` commit after the run start |
| SC-015 | `release.yml` · `workflow_dispatch`, one plugin, `dry_run: true`, from `main` after the merge · jobs `Deploy Landing Page`, `feed / build`, `feed / deploy`, `feed / verdict` | `Deploy Landing Page` skipped; `feed / build` success; `feed / deploy` skipped; `feed / verdict` success with the `feed-verdict` `green` annotation; no `gh-pages` commit |

Every Windows row closes locally: SC-001 to SC-008, SC-010, SC-011, SC-016 to SC-018 from T019's TAP file; SC-009 and
its safety rows from L-CLOSE; SC-012 from `scope-check.js`; SC-013w from L-TAP.

**User steps after SC-014 is green (not exit criteria, spec "User step"):** dispatch `feed.yml` with `dry_run: false`
from `main` and check one plugin's update banner; after the live backfill, a dry run then a live run (FR-061).
