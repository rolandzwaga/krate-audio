# Krate Manager Phase 3 (krate-manager-phase3) - Compliance Report

**Overall status: COMPLETE_PENDING_CI**

## Honest gap list (first)

1. **SC-012 was FAIL in the workflow's close lane (`20261030-172306`), now pass.** The CC-lints fix had edited
   `tools/check-portability.js` (not an FR-060 path) to define `KRATE_FEED_FIXTURES_DIR`. Main-loop fix: the macro is
   renamed `SHARED_FIXTURES_DIR`, which `check-portability.js:121` already defines for every plugin directory, and the
   `check-portability.js` edit is reverted (spec Clarifications, "Build close"). Re-run close lane `20261030-172700`:
   `C2 scope-check exit=0 last="SCOPE GREEN"`, `A6 check-portability: all clear -- 1 compiled.`, `CLOSE LANE GREEN`.
2. Open pending-ci items: SC-009m, SC-009l, SC-013m, SC-013l, SC-014, SC-015 (see PENDING-CI list).

## Compliance table

Evidence is condensed from the task agents' and comply agents' observations. Quoted strings are verbatim. "ok" means a TAP `ok` line seen in the agent's run.

| ID | Verdict | Evidence |
|---|---|---|
| FR-020 | pass | `tools/feed-publish.js:1` shebang; `main(argv, io, deps)` throws a `TypeError` naming deps (:981-987) before `parseArgs`; entry point alone calls `productionDeps()` (:1027); the only spawn is `spawnSync('gh', argv, {shell:false})` (:202-207); `writeAtomic` temp+rename (:101-117). Grep: no http/https/net/tls/dns, no `fetch(`, no plugin name. Hygiene subtest (d) and 'feed-publish tests never build real effects' (a) ok. |
| FR-021 | pass | `pluginNames` (:232-255) enforces `isPluginName`, `manager` reserved, duplicates; runs at :398 before any gh call. List argv :417; row-shape check :135-147; `selectReleases` for all plugins before the first view (:423); view check :283-299; rows written via `JSON.stringify(rows,null,2)+'\n'`. Collect-aborts subtests ok. |
| FR-022 | pass | View argv :303; `release.json` asset else `no-manifest` (:434-441); `downloadChecked` (:311-331) refuses null/absent digest with `E_FEED_DOWNLOAD '... has no digest on GitHub'` before gh, then size (:322) and SHA-256 (:325-329). Subtest '(10) a release.json asset with digest null is refused before any download (P-2)' ok. |
| FR-023 | pass | Pre-filled out-dir refused before any gh call (:405-409); gh failure -> `E_INPUT`; `rows.length >= 1000` -> `E_FEED_LIST_TRUNCATED` (:419-421); size/digest -> `E_FEED_DOWNLOAD`; outputs written atomically last (:449-450); `finally` cleanup (:452-454). No `--clobber`, no `gh api` (grep 0). Subtests '(1) every FR-023 abort exits 1 ...' (14 cases) and '(3) a pre-filled out-dir is refused untouched ...' ok. |
| FR-023a | pass | `GH_ATTEMPTS=3`, `GH_BACKOFF_MS=[5000,15000]` (:61-62); `withGhRetry` logs `feed-publish: WARN gh attempt ${attempt}/3 failed: ${ghLabel(argv)} (exit ${r.status}); retrying in ${wait/1000} s` (:182-197); partial `--output` removed before retry (:192-193). Decided failures never retried. Subtests (4a)-(4d) and steps (6) ok. |
| FR-024 | pass | `MANAGER_TAG_RE` (:63); drafts/prereleases skipped, highest by `compareSemVer` (:364-374); `{status:'none'}` with no `manager/` when no row or no asset (:377-393). Manager subtests (1)-(3) ok. |
| FR-025 | pass | `checkLatestJson` (:334-361); every failure `E_MANAGER_LATEST`; degraded -> manager/ removed, `WARN E_MANAGER_LATEST`, exit 3 after writing everything (:465-467, :483). Subtests '(5) every FR-025 failure degrades ...' (9 cases) and '(4) a latest.json version with one leading v is accepted' ok. |
| FR-026 | pass | Report `{plugins, manager, gh_calls}` (:426-450); one `collect:` line per plugin and one `manager:` line (:457-470). Collect subtests (7)-(9) ok. |
| FR-027 | pass | Inputs, `validateFeed` v2 then v1, row checks, `projectV1`, control-character refusal (:825-851); line formats :743-785; fence `max(3, longest backtick run + 1)` (:854-862); exit 0/1/3 (:897-899). All 34 compare subtests ok incl. live-control-lf/cr/bel and live-backtick. |
| FR-028 | pass | Exit codes per subcommand (:483, :565, :599, :606-616, :897-899, :678-688); `main` prints `feed-publish: <CODE>: <message>`, 2 for class `usage`, else 1 (:1016-1024). Steps subtest '(8) generate passes gen-feed 1 and 2 through ...' ok. |
| FR-029a | pass | `generate` (:491-590): reads report, creates site dir, refuses existing `<site-dir>/manager`, argv with `--manager-latest-url https://krateaudio.com/manager/latest.json` only when copied, second run must match exit/stderr/bytes (:567-576), `COPYFILE_EXCL` staging. Steps subtests (7), (9)-(12) ok. |
| FR-029b | pass | `outcome` (:605-617): exit 0 -> `<flag>=false`, exit 3 -> `<flag>=true`, else `E_STEP_FAILED` and 1. Steps subtest '(1) outcome appends the flag for 0 and 3 and fails the step otherwise' ok. |
| FR-029c | pass | `verdict` reason order and evidence checks (:650-689). Steps subtests '(2) verdict: the full 3,456-row table against the FR-029c oracle' and '(3) verdict reason text' ok. (Task agent T005 notes the table is 3,456 rows, not the 1,728 stated in tasks/plan; spec text should say 3,456.) |
| FR-029d | pass | compare returns 1 when `block > 0 \|\| dryRun`, 3 for warn-only real run (:899); feed.yml:84-85 compare followed by `outcome --tool compare`. Compare cases `*-real` vs `*-dry` ok. |
| FR-029e | pass | Seven fixed annotation titles (:80-88); `annotate` is the only `::` writer (:162-167). Subtests 'collect (6) --annotate', 'steps (13)', '(5) PD-18 (c)' ok. Minor wording: the feed-validate error message starts with the schema ("v2 /path: ..."), matching plan §4.2. |
| FR-001 | pass | feed.yml:8-20 `workflow_call` (boolean, required, no default) and `workflow_dispatch` (default true); no other triggers. Shape subtest ok. |
| FR-002 | pass | feed.yml:22-23 top-level `permissions: contents: read`; no top-level concurrency; no `secrets.`, no `environment:`. Shape subtest ok. |
| FR-003 | pass | build job :26-53 (ubuntu-latest, contents read, `GH_TOKEN`, checkout@v6, setup-node@v4 '24', `npm ci --prefix tools/feed-contract`); every `run:` has `shell: bash` and `set -euo pipefail`. Shape subtests 4, 5 ok. |
| FR-004 | pass | feed.yml:55-61 collect step with `\|\| rc=$?` then `outcome --tool collect`. |
| FR-005 | pass | feed.yml:63-67 `curl --fail --silent --show-error --location --retry 3 https://krateaudio.com/versions.json --output ...`. |
| FR-006 | pass | feed.yml:69-75 generate then `outcome`; gen-feed unchanged (`git diff --stat 4f4c4175 -- tools/gen-feed.js` empty). |
| FR-007 | pass | Degraded/warn exits continue to 'Upload site' and deploy; verdict red with WARN lines (feed.yml:87-92, :102-125; feed-publish.js:660-673). Verdict table ok. |
| FR-008 | pass | Test 'a release without release.json stays v1-only' (TAP ok 15), feed-pipeline.test.js:347-360. |
| FR-009 | pass | feed-publish.js:546, :567-576 `E_FEED_NONDETERMINISTIC`; feed.yml has no cmp step. Pipeline subtest ok. |
| FR-010 | pass | feed.yml:77-85 compare with `env: DRY_RUN`, `--dry-run "$DRY_RUN" --report --summary --annotate`, no `--repo`. |
| FR-011 | pass | feed.yml:87-92 'Upload site' (upload-artifact@v7, `if-no-files-found: error`); :94-100 'Upload evidence' (`if: always()`, warn). Shape subtests 6, 7 ok. |
| FR-012 | pass | `group: krate-feed`, `cancel-in-progress: false`, `queue: max` on build (:30-33) and deploy (:108-111) only; grep: `krate-feed` only at feed.yml:31, :109. |
| FR-013 | pass | feed.yml:102-125 deploy: `needs: build`, `if: ${{ !inputs.dry_run && github.ref == 'refs/heads/main' }}`, contents write, download-artifact@v8 + peaceiris@v4 (`keep_files: true`, `cname: krateaudio.com`), no checkout. Shape subtest 8 ok. |
| FR-014 | pass | feed.yml:127-167 verdict job, `if: always()`, seven env values into `node tools/feed-publish.js verdict`. Shape subtest 9 ok. |
| FR-015 | pass | Case-insensitive grep for the nine plugin names in feed.yml: 0 hits. |
| FR-030 | pass | release.yml:868-875 job `feed`: `needs: [release, deploy-pages]`, `if: ${{ !cancelled() && needs.release.result == 'success' }}`, `uses: ./.github/workflows/feed.yml`, `with: dry_run: ${{ inputs.dry_run }}`. Shape subtest 10 ok. |
| FR-031 | pass | `needs: [release, deploy-pages]` ensures release.json exists before collect. |
| FR-032 | pass | `git diff 4f4c4175 -- release.yml` removes only 'Generate Update Checker Endpoint' and adds the feed job. |
| FR-033 | pass | Exactly two hunks; 'release.yml and backfill-sign.yml keep the Phase 2 shape' TAP ok 55. |
| FR-040 | pass | `comparePlugin` cases 1-6 (:728-786), severity table (:694-701). 34 compare subtests ok. |
| FR-041 | pass | v2 then v1 `validateFeed` before the rule (:839-845). Case 'feed-fails-v2' ok. |
| FR-044 | pass | `fixtures/phase3/membrum-v0.12.2.json` built by `make-phase3.js` (no plugin names); set 'phase3' at make-fixtures.js:1212. Subtests '(2)' and '(7)' ok; 'fixtures are in sync with make-fixtures.js' TAP ok 41. |
| FR-045 | pass | gen-feed on snapshot + manifest exit 0, `cmp` IDENT with `feeds/valid/one-v2-eight-v1.json`; projectV1 `PROJ_IDENT` vs snapshot live-versions.json; cases.json row Phase3/one-v2-eight-v1 (accept). Snapshot diff empty. |
| FR-046 | pass | New case 'UpdateChecker - one v2 plugin among eight v1 plugins is read as v1' [update]; measured `All tests passed (130 assertions in 1 test case)`; `9 == 9` once. No header/non-test source changed. |
| FR-050 | pass | Five new test files each `require('./helpers/write-guard.js')` at line 2; workflow-shape block (11 subtests ok); fake-gh.js extended; 'the fake gh serves the Phase 3 shapes' ok. (T003 note: snapshot has 9 pre-monorepo releases with no assets; read FR-050's "no empty assets" as applying to `<plugin>/v<ver>` releases.) |
| FR-051 | pass | hygiene.test.js scans feed-publish.js and make-phase3.js, subtest (d) ok; write-guard.js diff empty; 'feed-publish tests never build real effects' (a),(b) ok. |
| FR-052 | pass | feed-tools.yml path filters and `PLAN = "1..57"` plus 11 NAMES. Measured: 1..57, tests 702, pass 697, fail 0, skipped 5; TAPCHECK printed `TAP plan=1..57 top-level=57`, rc=0. |
| FR-053 | pass | CONTRACT.md:787 `## 10. Phase 3 tools` (10.1-10.7); §7 rows :490-496; contract.js CODES gains the seven keys. 'CONTRACT.md documents every code and field' TAP ok 42. (T017 note: the §7 `E_FEED_DOWNLOAD` row, CONTRACT.md:491, still says "where GitHub lists a digest", which contradicts P-2 and §10.3; no test goes red.) |
| FR-054 | pass | `SHARED_FIXTURES_DIR` define at plugins/shared/tests/CMakeLists.txt:69-72, guard at test_update_checker.cpp:466-468. Close lane 20261030-172700: A1 `shared_tests build exit=0 warnings=0 wall=5.0s`; B1 `GATE GREEN shared_tests cases=9 assertions=252 wall=2.2s workers=30`; A4 `CONSUMERS OUTSIDE [update]: none`; B4c `compile_commands.json entry of plugins/shared/tests/test_update_checker.cpp defines SHARED_FIXTURES_DIR`; B4 `clang-tidy ... exit=0 TU warnings=0 TU errors=0 ... catch2-filtered=94 wall=15.9s` (the 94 are bugprone-chained-comparison and avoid-do-while from Catch2 macros, which the repo runner also drops, run-clang-tidy.ps1:381-385). |
| FR-060 | pass | Change set confined to the allowed paths; frozen files (gen-feed.js, docs.yml, backfill-sign.yml, ci.yml, plugins/*/version.json, installers, snapshot) have empty diffs. `tools/check-portability.js` diff is empty (edit reverted, SC-012). |
| FR-061 | pass | backfill-sign.yml diff empty; feed.yml has only workflow_call/workflow_dispatch; user step documented at CONTRACT.md:1070-1084. |
| FR-062 | pass | `git diff 4f4c4175 -- .github/workflows/ci.yml` empty; shape subtest 11 ok. |
| SC-001 | pass | `ok 1 - feed pipeline reproduces the live v1 feed from the 2026-10-29 snapshot`, pass 1 fail 0. Exact 10-call argv list, 0 downloads, byte-equal v1 projection, nine `same` lines. |
| SC-002 | pass | `ok 1 - feed pipeline is deterministic`, `# pass 3 # fail 0`; byte-identical outputs; differing second run is `E_FEED_NONDETERMINISTIC`. |
| SC-003 | pass | `ok 1 - collect downloads only the selected manifests`, `# pass 12 # fail 0`; full argv list, no `--clobber`/`api`, digest null refused. |
| SC-004 | pass | `ok 1 - collect aborts cleanly`, 39 subtests, `# pass 40 # fail 0`; retry sleeps [5000] / [5000,15000]; exact WARN lines. |
| SC-005 | pass | `ok 1 - a release without release.json stays v1-only`, `# pass 1 # fail 0`. |
| SC-006 | pass | `ok 1 - collect copies only a valid manager latest.json`, 23 subtests, `# pass 24 # fail 0`. |
| SC-007 | pass | `ok 1 - compare explains every v1 difference or fails`, 34 subtests, `# pass 35 # fail 0`. |
| SC-008 | pass | `ok 1 - Phase 3 mixed feed fixture is real-shaped`, `# pass 9 # fail 0`; `make-fixtures: committed fixtures match the builder`; validateFeed v2 and v1 valid; projectV1 equals live-versions.json (1246 bytes). |
| SC-009 | pass | B1 `GATE GREEN shared_tests cases=9 assertions=252 wall=2.0s workers=30` (after rebuild `wall=1.3s`); B2: `"9 == 9" found`, each per-name assertion 9 times, `FAILED lines=0`, `All tests passed (130 assertions in 1 test case)`; 0 warnings; A4 `CONSUMERS OUTSIDE [update]: none`; B4 `TU warnings=0 TU errors=0`; B0/B3 real `update_state.json` stats identical before/after. |
| SC-009m | pending-ci | See PENDING-CI. |
| SC-009l | pending-ci | See PENDING-CI. |
| SC-010 | pass | `ok 1 - feed.yml and release.yml keep the Phase 3 shape`, 11 subtests, `# pass 12 # fail 0`. Interpretation: `\|\| rc=$?` is required by the same SC, so "no \|" is read as no pipe. |
| SC-011 | pass | `ok 1 - tools stay offline` (a)-(d), `# pass 5 # fail 0`; `ok 1 - CONTRACT.md documents every code and field`. feed-publish.js requires only node:child_process, node:crypto, node:fs, node:path, contract.js, gen-feed.js. |
| SC-012 | pass | Close lane 20261030-172700: C1 `scope-check --self-test exit=0 last="SELF-TEST GREEN (11 cases)"`; C2 `scope-check exit=0 last="SCOPE GREEN"` (log F:\tmp\gate\krate-manager-phase3-close\20261030-172700\C2-scope-check.log); `CLOSE LANE GREEN`, exit=0. The first close lane (20261030-172306) was RED on tools/check-portability.js; fixed by the macro rename, see the gap list. |
| SC-013w | pass | `1..57`, `# tests 702`, `# pass 697`, `# fail 0`, `# skipped 5`; TAPCHECK `TAP plan=1..57 top-level=57`, exit 0; git status unchanged around the run. |
| SC-013m | pending-ci | See PENDING-CI. |
| SC-013l | pending-ci | See PENDING-CI. |
| SC-014 | pending-ci | See PENDING-CI. |
| SC-015 | pending-ci | See PENDING-CI. |
| SC-016 | pass | `ok 1 - release.yml and backfill-sign.yml keep the Phase 2 shape`, 30 subtests, `# pass 31 # fail 0`; workflow-shape.test.js diff is additions only (`@@ -628,0 +629,344 @@`). |
| SC-017 | pass | `ok 1 - feed-publish tests never build real effects`, `# pass 3 # fail 0`; `productionDeps(` count 0 in all five Phase 3 tests; every `feedPublish.main(` has three arguments. |
| SC-018 | pass | `ok 1 - feed-publish decides every workflow branch`, 13 subtests, `# pass 14 # fail 0`; 3,456-row verdict table. |
| CC-system | pass | No Tauri app exists; all tests are write-guarded and offline; the C++ case uses `MockUpdateChecker` and `ScopedSettingsDirRedirect`; `git status --porcelain` unchanged (23 before and after). |
| CC-verify | pass | Downloads only via `downloadChecked` (digest refused if null, size and SHA-256 checked) before any parse; nothing extracted or executed. |
| CC-zipslip | pass | No extraction code in the change set (grep exit 1); no Tauri/Rust/TS files. |
| CC-helper | pass | No elevated helper exists; only gh spawned with fixed argv and `shell: false` (feed-publish.js:199-207). |
| CC-capabilities | pass | No Tauri capabilities; workflow permissions least-privilege (contents read; write only on deploy and the release.yml `feed` call); no `secrets.` references. |
| CC-signing | pass | No signing/secret tokens in feed.yml or feed-publish.js; release.yml diff adds none; no private keys in the new fixtures. |
| CC-warnings | pass | cargo/tsc not applicable (apps/krate-manager absent). Forced recompile of the changed TU: exit 0, 0 warning lines; g++ `-fsyntax-only` exit 0; node set `1..57 # tests 702 # pass 697 # fail 0 # skipped 5`; `GATE GREEN shared_tests cases=9 assertions=252 wall=1.4s workers=30`. |
| CC-lints | pass | No check-portability.js change needed: `SHARED_FIXTURES_DIR` is defined by check-portability.js:121. Close lane 20261030-172700: A6 `check-portability plugins/shared/tests/test_update_checker.cpp exit=0 last="check-portability: all clear -- 1 compiled."`; A5 `lint-serial-test-runs: clean (64 files)`; A7 `specs/INDEX.md is up to date.`; A8 `make-fixtures: committed fixtures match the builder`; A3 L-YAML exit=0 on feed.yml, release.yml, feed-tools.yml, ci.yml; A2 `plan=1..57 tests=702 pass=697 fail=0 skipped=5 nodeWarnings=0`; A2t `TAP plan=1..57 top-level=57`. |
| CC-feed-compat | pass | Parser unchanged (`git diff --quiet plugins/shared/src`); mixed fixture keeps each plugin's v1 triple (all 9 `v1-equal`); C++ guard case `All tests passed (130 assertions in 1 test case)`; `[update]` gate green (9 cases, 252 assertions). |

## PENDING-CI list

The user pushes; nothing was pushed here.

| ID | CI job | Assertion that proves it |
|---|---|---|
| SC-009m | ci.yml `macOS (Clang)` (:562, builds shared_tests :623) then `macOS tests (shard N)` (:1405) | Case `UpdateChecker - one v2 plugin among eight v1 plugins is read as v1` passes (`SHARED_FIXTURES_DIR` resolves via the Checkout step). Triggered by path filter `plugins/**` (:8/:18, :85-86). |
| SC-009l | ci.yml `Linux (GCC)` (:1061, shared_tests :1127) then `Linux tests (shard N)` (:1442) | Same case passes. |
| SC-013m | feed-tools.yml `node-test (macos-latest)` | TAPCHECK prints `TAP plan=1..57 top-level=57`, `# fail 0`, all 11 NAMES present, no CI skip of the rsign blocks, empty `Checkout unchanged`. |
| SC-013l | feed-tools.yml `node-test (ubuntu-latest)` | Same TAPCHECK assertions. |
| SC-014 | After merge to main: `workflow_dispatch` of feed.yml with `dry_run: true` | Jobs API: `build` success, `deploy` skipped, `verdict` success; annotations `feed-validate` `v2 ok, v1 ok`, `feed-compare` `... 0 drift (0 block, 0 warn)`, `feed-collect` naming `manager none`, `feed-verdict` green; artifacts feed-site and feed-evidence; no newer gh-pages commit. |
| SC-015 | After Phase 3 is on main: release.yml dispatch with `dry_run: true` | `deploy-pages` skipped, `feed / build` success, `feed / deploy` skipped, `feed / verdict` success with `feed-verdict` green, no gh-pages commit. |

## Implementation notes (deviations reported by task agents)

- **T001:** TA-2 citations in spec are `tools/run-gate.js:52-53` and `close-lane.js:35`; tasks.md still says :51-53 / :32 (not edited). SC-003 and the Safety bullet were edited to match P-2. Open question: FR-024 applies "the FR-022 size / digest check" to `latest.json`; T010 refuses a null/absent digest there too.
- **T002 / T003:** already on disk; checked only. FR-050 "no empty assets array" does not hold for 9 pre-monorepo snapshot releases (no effect on tools). The fake-gh.js diff removes four lines (the `makeWorld` signature and state branch are needed for the `world` parameter).
- **T004:** E_INPUT messages omit file paths and parser text; E_FEED_SCHEMA message repeats the path; E_INPUT stops print no annotations.
- **T005:** the verdict table is 3,456 rows, not 1,728; retry log goes through `emit`; unreadable evidence prints `(unreadable)`.
- **T007:** was reported **blocked** because the new row `Phase3/one-v2-eight-v1` failed the Phase 1 exhaustiveness check in `c1-feed.test.js` (:235-243). The later whole-set runs are green (`1..57`, `# fail 0`), and FR-060 lists c1-feed.test.js among changed files, so it was evidently resolved afterward. No task note records the fix itself.
- **T008:** on E_INPUT/E_FEED_SCHEMA the `--summary` file still gets one empty fenced block.
- **T009:** the Phase 2 backfill fixture plugins lack `url`, which gen-feed v2 mode requires, so tests use a temp copy with `url` added.
- **T010:** absent digest also refused; same refusal applied to the manager `latest.json`; tool header line 5 still says "PD-17" (superseded).
- **T011:** in T5 the excluded plugin's prerelease sibling still gets Phase 2 files.
- **T012:** workflow-shape.test.js and release.yml are LF/CRLF-ambiguous in Git Bash; the test kept the file's real endings.
- **T013:** the `grep -c $'\r$'` CRLF check cannot pass in Git Bash (Msys grep strips CR); use `grep -U`.
- **T015:** deviation from the plan's verbatim code: `nlohmann::json::parse(live).at("plugins").items()` iterates a destroyed temporary (measured `0 == 9`), so a `liveDoc` local was added.
- **T016:** the test file's header comment still calls (d) "a CI step".
- **T018:** feed-tools.yml node-test job name/matrix lines moved to :41 and :46.
- **T019:** the close lane was written and deferred to the main loop.

## Remaining steps for the human loop

1. SC-012: done (macro renamed, close lane 20261030-172700 GREEN).
2. Fix the stale `E_FEED_DOWNLOAD` row wording in CONTRACT.md §7 (:491) to match P-2.
3. Review the diff and the evidence under `specs/krate-manager-phase3/evidence/`.
4. Run the main-loop lanes listed there (`close-lane.js`, `scope-check.js`).
5. Commit.
6. Push (user only) to run the CI proof for SC-009m/l and SC-013m/l, then the user-triggered dispatches for SC-014 and SC-015 after the merge to main.
