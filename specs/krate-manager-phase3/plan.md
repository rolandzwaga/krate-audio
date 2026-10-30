# Implementation Plan: Krate Manager Phase 3 — Feed Workflow and Pages Deploy

**Spec:** `specs/krate-manager-phase3/spec.md` (DRAFT, reviewed, Clarifications Q1-Q6 of 2026-10-30)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 3 (lines 423-457); Decisions Q9, ROSTER, Q10 (lines 32-34);
Cross-Cutting Constraints (lines 961-1002)
**Status:** DRAFT plan, 2026-10-30, verified against `feat/krate-manager` at `4f4c4175` (untracked:
`specs/krate-manager-phase3/`)
**Lanes:** pipeline lane only (Node tool, YAML, fixtures) plus one C++ test case in `shared_tests`. No app code, no
Tauri, no Rust in this phase, so there is no app lane, no `Platform` abstraction, no Tauri capability file and no
elevated helper to design here (§6 says what each of those safety items means for this phase).

---

## 0. What this plan adds to the spec

The spec fixes requirements and success criteria. This plan fixes the module boundaries, the public API of
`tools/feed-publish.js`, the exact text of `feed.yml` and of the `release.yml` / `feed-tools.yml` / `ci.yml` edits, the
fixture generator, the test-file map and the gate lanes. Where the plan decides something the spec leaves open, or has
to depart from a spec sentence, it is listed here and repeated at its use site.

### 0.1 Plan decisions

| Id | Plan decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **Concurrency is job-level, on `build` and `deploy`**: each carries `concurrency: {group: krate-feed, cancel-in-progress: false, queue: max}`; `feed.yml` has no top-level `concurrency`, and `verdict` has none. | FR-012 asks the plan to confirm from GitHub's docs that a called workflow's top-level `concurrency` (and `queue`) is honoured. The docs do not say so: the reusable-workflows page speaks only of "`jobs.<job_id>.concurrency.group` in the called and caller workflows" and warns against sharing that value (fetched 2026-10-30, https://docs.github.com/en/actions/reference/workflows-and-actions/reusable-workflows). Two third-party reports (z-shell/.github PR #507, github/gh-aw PR #43078) describe deadlock cancellations that imply top-level groups are evaluated in called workflows, but they are not documentation. The job level is documented in a called workflow, and `queue` is documented for "jobs or workflow runs" ("`queue: max`: Up to 100 jobs or workflow runs can be `pending` in the concurrency group"; FIFO order; `queue: max` with `cancel-in-progress: true` is a validation error; https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax, fetched 2026-10-30). **Why both jobs:** with the group on `deploy` alone, a run B whose `build` finished first could deploy before an older run A and A would then overwrite the newer feed. With the group on `build` and `deploy`, builds run one at a time and each `deploy` is enqueued at the moment its own `build` ends, before the next build can end, so deploys run in build order (§4.3.2). `verdict` holds no write and needs no slot. | FR-002 ("and the D9 concurrency block"), FR-012, D9, SC-010 (placement assertion, §7.1 T9) |
| PD-2 | Steps pass the repository as `"$GITHUB_REPOSITORY"` (the runner's default environment variable), not `"${{ github.repository }}"`. | FR-014 already keeps every expression out of `run:` text; the default variable carries the same `owner/name` value without template interpolation into a script. Both spell the same value; the capture pattern of SC-010 is unchanged. | FR-004, FR-006 (spelling only) |
| PD-3 | File writes are **not** routed through `deps`. `deps` carries exactly the effects a test must replace: `gh` (wrapped by `withGhRetry`) and `genFeedMain`. Files are written with `node:fs` only at paths given on the command line, through `writeAtomic` / `appendLine` helpers, as `tools/backfill-sign.js:84-103` and `tools/gen-feed.js:174-190` do. | The spec's "every effect (`gh`, file writes) through an injected `deps` object" (FR-020) is not how either sibling tool works; the write guard (`tests/helpers/write-guard.js`) already confines every test write to `os.tmpdir()`, and an injected fs would be a second abstraction for a guarantee that exists. | FR-020 wording (see SI-2) |
| PD-4 | `withGhRetry`'s returned function returns `{status, stdout, stderr, attempts}`; `collect` adds `attempts` to `gh_calls`. The production `gh` never throws: a spawn error becomes `{status: -1, …}`, so a `gh` that "fails to run" is retried like a non-zero exit (FR-023a). The production `sleep` is `Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms)`. | FR-026 counts every attempt, but the attempts happen inside `deps.gh`; returning the count is the only way `collect` sees it without parsing logs. `Atomics.wait` on Node's main thread: measured on this box (Node v24.7.0) `timed-out 259 ms` for a 250 ms wait; MDN's "cannot be used in the main thread" is the browser rule (https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Atomics/wait). Keeping `deps.gh` synchronous keeps `main` returning a number (`process.exitCode = main(…)`, FR-020) as in `backfill-sign.js:866`. | FR-023a, FR-026 (mechanism) |
| PD-5 | The retry log label is `list` for `release list` and `<subcommand> <tag>` for `view` / `download` (e.g. `feed-publish: WARN gh attempt 1/3 failed: view alphaplug/v1.2.0 (exit 1); retrying in 5 s`). | FR-023a's `<subcommand> <tag or "list">` read literally gives `list list` for the list call. | FR-023a (see SI-6) |
| PD-6 | **No staging directory.** `collect` refuses (`E_INPUT`, before any `gh` call) an `--out-dir` that already holds any of `releases.json`, `manifests`, `manager`, `collect-report.json`; it then downloads straight to the FR-022 / FR-024 paths (`--output <out-dir>/manifests/<tag with "/" as "-">.json`, `--output <out-dir>/manager/latest.json`, each built with `path.join(outDir, …)`), and writes `releases.json` and `collect-report.json` with `writeAtomic` only after the last check. A `finally` that runs unless the run completed removes every one of the four names this run created (`fs.rmSync(…, {recursive: true, force: true})`); because a pre-filled out-dir was refused, that removes only this run's output. | FR-023 requires that an abort leaves no `releases.json`, `manifests/`, `manager/` or temp file; with the refusal, "remove what this run created" meets it without departing from FR-022's and FR-024's `--output` text, so SC-003 / SC-004(a) can compare the argv list exactly (a random `mkdtemp` suffix in `--output` could not appear in an expected list written before the run; the fake records the path argument verbatim, `tests/helpers/fake-gh.js:232-242`). `writeAtomic` removes its own temp file on failure (`tools/backfill-sign.js:87-102`). Refusing a pre-filled out-dir also keeps a stale manifest from an earlier run out of gen-feed's `--manifests` (gen-feed reads every `*.json` there, `tools/gen-feed.js:156-168`). | FR-022, FR-023, FR-024 (made concrete; argv text unchanged) |
| PD-7 | `collect` checks the plugin names before any `gh` call: each `version.json` `name` must pass `contract.isPluginName` (`E_PLUGIN_NAME`), no two may be equal after `toLowerCase()` (`E_DUPLICATE_PLUGIN`), and none may lower-case to `manager` (`E_PLUGIN_NAME`). | `selectReleases` calls `name.toLowerCase()` (`tools/gen-feed.js:419`) and would crash on a non-string; two names that fold together would view and download one release twice into one file; a plugin named `Manager` would select the manager's own `manager/v*` releases as its own. gen-feed raises the first two itself later (`CONTRACT.md` §7 rows `E_PLUGIN_NAME`, `E_DUPLICATE_PLUGIN`); `collect` must not spend `gh` calls first. | FR-021 (unstated inputs) |
| PD-8 | The `gh release list` rows are shape-checked by `collect` (array; each row an object with a non-empty string `tagName` and boolean `isPrerelease`, `isDraft`) before `selectReleases` sees them (`E_INPUT`); `publishedAt` is left to gen-feed's `checkReleaseRows` (`tools/gen-feed.js:394-415`, abort `E_INPUT`). A view must return `{tagName: <the tag>, assets: [...]}` whose assets each have a string `name`, a non-negative safe-integer `size` and a `digest` that is absent, `null` or `sha256:<64 hex>` (case-insensitive) — else `E_INPUT`. | gen-feed does not export `checkReleaseRows`, and `tools/gen-feed.js` is byte-frozen (FR-060); this is `backfill-sign.js`'s `checkListRows` rule (`tools/backfill-sign.js:160-172`), copied, not imported (its codes are backfill codes, spec Existing pieces row `backfill-sign.js`). | FR-023 ("output that is not the expected JSON shape") |
| PD-9 | `generate` requires the second gen-feed run to give the **same exit code and the same stderr text** as the first, besides byte-identical output; any difference is `E_FEED_NONDETERMINISTIC`. | A run whose WARN set differs between two runs on the same inputs is as non-deterministic as differing bytes, and `verdict` prints the WARN lines from `gen-feed.log` (the first run's stderr). gen-feed's WARN messages name only plugins, `manifests/<file>` labels and fields (`tools/gen-feed.js:533-548`, `:650-656`, `:683-688`), never the `--out` path, so equal stderr is the expected case. | FR-009, FR-029a (one more equality) |
| PD-10 | One `drift` line per plugin: `drift <Name> <kind>[; <kind>…] <severity>`, kinds in the fixed order of §4.2's table, `<severity>` = `block` if any kind is `block`, else `warn`. `<n> drift (<n> block, <n> warn)` counts plugins. | FR-027 prints "one line per plugin" yet a plugin can have two kinds (FR-040: "A plugin with several drift kinds is blocking if any of its kinds is"). SC-007 spells single-kind lines without brackets (`drift … version lowered block`), so the `[<severity>]` of FR-027 is read as a placeholder, not literal brackets. | FR-027, FR-040 (see SI-4) |
| PD-11 | `compare` never calls `compareSemVer` on a value that is not SemVer: a live `version` that `parseSemVer` rejects can match no `newer`, `q9-corrected` or `version lowered` case, so it lands in `version changed without a release` (`warn`). | `compareSemVer` throws a `TypeError` on a non-SemVer string (`tools/feed-contract/contract.js:161-165`, `toSemVer`); the live file was written by the inline generator from `tag.replace(/.*\/v/, "")` (`release.yml:886`), which can yield any string. The new feed's versions are SemVer by C1 (FR-041 runs first). | FR-040 (unstated input) |
| PD-12 | `verdict`'s deploy reason applies only when `--build` is `success`: on a failed build the `skipped` deploy adds no second reason. Order of reasons printed: build, ref, deploy, the three flags (feed, manager, drift), evidence. | FR-029c lists the reasons with "otherwise" only between the ref clause and the deploy clause; a red build always skips `deploy` (`needs: build`), so a deploy reason there is noise. SC-018 asserts the 0/1 table, which this does not change. | FR-029c (made concrete) |
| PD-13 | `compare --report` holds exactly the result lines (per-plugin lines and the summary line). With `--annotate`, stdout is those lines followed by the annotation lines; without it, stdout equals the report. On `E_INPUT` / `E_FEED_SCHEMA` there are no result lines and the report is written empty. | FR-027 says `--report` writes "exactly those stdout lines", and FR-029e adds workflow commands to stdout; annotation lines are not report lines. | FR-027, FR-029e (made concrete) |
| PD-14 | **The mixed fixture and the Membrum manifest are built by the fixture builder.** New module `tools/feed-contract/fixtures/phase3/make-phase3.js` exports `files({fixtureSignature})`; `fixtures/make-fixtures.js` registers a sixth set `phase3` (`dirs: ['phase3']`, `keep: /^phase3\/make-phase3\.js$/`) whose files are `phase3/membrum-v0.12.2.json`, the SC-007 triples under `phase3/compare/`, and `feeds/valid/one-v2-eight-v1.json`; `feeds()` appends the `Phase3/one-v2-eight-v1` row to `feeds/cases.json`. The feed is produced by running `gen-feed.buildFeed` in-process on the snapshot and the manifest. | `make-fixtures.js` owns the whole `feeds/` directory and builds `feeds/cases.json` (`fixtures/make-fixtures.js:523-541`, `:1197-1208`); its `check()` reports any file under an owned directory that it did not build as `stray` and any byte difference as `differs` (`:1242-1265`). A hand-added `one-v2-eight-v1.json` plus a hand-edited `cases.json` would turn the Phase 1 test `fixtures are in sync with make-fixtures.js` (`tests/hygiene.test.js:307-311`) red. | FR-044, FR-045 (generator location; see SI-1) |
| PD-15 | `make-phase3.js` names no plugin: it picks the snapshot plugin whose `version.json` carries `preset_subdir` (exactly one does today, asserted) and derives every name, the tag (`<name lower>/v<version>`) and the file name from it. | The roster hygiene (`tests/hygiene.test.js:249-280`) and `make-fixtures.js`'s own rule "Contains no shipped plugin name" (`fixtures/make-fixtures.js:17`). `grep -l preset_subdir` over the snapshot `plugins/*/version.json` finds only `membrum/version.json` (this session). | FR-044 (selection rule in code) |
| PD-16 | `feed-publish.js`'s `tools/*.js` form is checked in a new subtest `(e)` of the existing `tools stay offline`, with `main([], io, stubDeps)` (it cannot use the Phase 2 form test, which calls `main([], io)` with two arguments, `tests/hygiene.test.js:523`). | FR-020 makes a two-argument `main` throw; SC-011 keeps the hygiene test names unchanged. | SC-011 (where) |
| PD-17 | A downloaded `release.json` whose asset record has `digest: null` is accepted on size alone, as FR-022 says; `collect` counts it and prints `collect: digest checked <n> of <m>` (stdout only, not a report field). | FR-022 is explicit. Phase 2 refuses a null digest for the **installers** it signs (`tools/backfill-sign.js:314-317`, Phase 2 Clarifications CC-verify); `release.json` is not the trust root here: the manager verifies each asset's minisign signature against the committed key (Phase 7), so a substituted `release.json` cannot make a forged installer verify. The count makes a run that checked fewer digests visible. | FR-022 (see SI-5) |
| PD-18 | **Data never forms a workflow command or a markdown escape.** (a) `compare` refuses (`E_INPUT`, exit 1) a live file in which any `plugins` key, or any string `version`, `download_url` or `release_notes` value, holds a code unit U+0000-U+001F or U+007F: `live plugins.<JSON.stringify(key)> holds a control character`. (b) In every line the tool builds, a data-derived value is printed raw only when it passes its own validator (`contract.isPluginName` for a name, `parseSemVer` non-null for a version, `contract.checkUrl` not throwing for a URL); any other value is printed as `JSON.stringify(value)`. (c) Every line the tool writes goes through one writer, `emit(sink, line)`, which prints `JSON.stringify(line)` instead of `line` when the line holds a U+0000-U+001F / U+007F code unit or starts with `::`; this covers `collect` / `generate` / `verdict` lines that quote data (a `latest.json` value in an `E_MANAGER_LATEST` message, a returned `tagName`, the `gen-feed.log` and `compare.txt` lines `verdict` quotes, `manager.message` of `collect-report.json`, gen-feed's stderr that `generate` echoes). `annotate` (§4.1.6) is the only writer that starts a line with `::`, and its message is built from lines that already passed (c). (d) `--summary` wraps the lines in a fence of `max(3, longest backtick run in the lines + 1)` backticks. | The live file arrives over the network from whatever can write `gh-pages` (`docs.yml`, `release.yml` `deploy-pages`) or answer for the domain; PD-11 notes a live `version` can be any string; §4.2 takes names from the union of `L` and `N` keys without `isPluginName`. The runner reads a line that starts with `::` as a workflow command, so a live key `"X\n::notice title=feed-verdict::green"` would plant a fake green verdict, and `::stop-commands::` would hide every later annotation, which are this phase's only evidence channel (§9; SC-014, SC-015). A fixed three-backtick fence is closed by any three-backtick run in a value. The rule does not depend on which stream the runner reads commands from: stdout and stderr lines go through `emit` alike. | FR-027, FR-029c, FR-029e (stricter input and output; see SI-10) |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before build)

Each is resolved in this plan as stated; the spec text should follow so the compliance rows match.

- **SI-1 (FR-044 / FR-045, fixture ownership):** `make-fixtures.js` owns `feeds/` and builds `feeds/cases.json`, so the
  mixed feed and its `cases.json` row must come from the builder, or `fixtures are in sync with make-fixtures.js`
  fails (`stray: feeds/valid/one-v2-eight-v1.json`, `differs: feeds/cases.json`). Resolved by PD-14. Suggested spec
  edit: FR-044's "committed generator in the `make-fixtures.js` style" becomes `fixtures/phase3/make-phase3.js`,
  registered as the `phase3` set of `make-fixtures.js`; FR-045's file and row are built by it; SC-008 adds "and
  `make-fixtures.js --check` passes".
- **SI-2 (FR-020 "file writes through deps"):** neither sibling tool injects file writes; PD-3. Suggested edit: "every
  `gh` call and the gen-feed run through an injected `deps` object; files only at paths given as flags".
- **SI-3 (FR-012 placement):** the docs do not confirm top-level `concurrency` in a called workflow; PD-1 moves the
  block onto `build` and `deploy` (not `verdict`). Suggested edit: FR-002 drops "and the D9 concurrency block"; FR-012
  and SC-010 name the two jobs and assert no top-level `concurrency`.
- **SI-4 (FR-027 drift line):** `[<severity>]` is read as a placeholder, and a multi-kind plugin prints its kinds joined
  by `; ` (PD-10). Suggested edit to FR-027 and SC-007's "a plugin with a `warn` and a `block` drift" subtest: the exact
  line is `drift <Name> <kind A>; <kind B> block`.
- **SI-5 (FR-022 null digest vs Phase 2's CC-verify):** kept as the spec says (PD-17); flagged so the user sees that
  Phase 3 accepts on size alone what Phase 2 refuses for installers. If the user wants the Phase 2 rule here too, the
  change is one branch in `checkDownloaded` (`E_FEED_DOWNLOAD`) and one SC-004 case.
- **SI-6 (FR-023a log label):** `list` instead of `list list` (PD-5).
- **SI-7 (blocking kinds that cannot self-heal):** the C1 schema fixes `release_notes` to `""` (`versions.v2.schema.json:220`
  `"const": ""`), and FR-041 validates the new feed before the rule, so `release_notes not empty` can only describe the
  **live** file; likewise `removed` describes a plugin no longer in the roster. Both are `block` (Q1), so if either ever
  appears, no `feed.yml` run can repair the live file: the recovery is a hand commit on `gh-pages` that removes the
  bad key from `versions.json`, then a dry run (`same`) and a real run. Today no writer besides the removed inline step
  produces `versions.json` (`grep versions .github/workflows/*.yml`: only `release.yml:861-898`), so the case is latent.
  Recorded in §9 and in `CONTRACT.md` §10; the user may prefer to re-rule (no plan change needed either way beyond the
  severity table). The same hand-commit recovery applies to a live file that PD-18 (a) refuses.
- **SI-8 (SC-012 path list):** FR-060 omits `specs/krate-manager-phase3/**` (spec, plan, tasks, evidence, compliance),
  which the phase necessarily writes, and the roadmap's Phase 3 status line that the close updates. Suggested edit:
  SC-012 allows those two.
- **SI-9 (FR-052 count):** the parenthesis "(SC-001 to SC-012, SC-017, SC-018)" over-counts: SC-009 is C++, SC-011 adds
  no top-level test (names unchanged), SC-012 is a git check. The new top-level tests are eleven (§7.1), so `PLAN`
  becomes `1..57` (46 today, recounted: `grep -c "^test("` over `tools/feed-contract/tests/*.test.js` sums to 46).
- **SI-10 (FR-027 / FR-029c / FR-029e, data in workflow output):** the spec does not say what `compare` does with a
  live key or value that holds a control character, nor how data-derived values are printed, so a live file could
  inject workflow commands or close the summary fence (PD-18). Suggested spec edit: FR-027 adds PD-18 (a) as an
  `E_INPUT` abort and (b)-(d) as the output rule; FR-029e adds "no line other than an annotation starts with `::`";
  SC-007 adds the two §7.1 T7 cases (live key `"X\n::notice title=feed-verdict::green"` → exit 1 `E_INPUT`; live key
  ``"X```"`` → printed as its `JSON.stringify` form inside a four-backtick summary fence) and the assertion that no
  stdout line starts with `::` except the tool's own fixed-title annotations.
- **SI-11 (SC-009m/l, fixture-only pushes):** `ci.yml`'s workflow-level `paths` (`ci.yml:3-23`) do not list
  `tools/feed-contract/**`, so a push that changes only the two fixture directories starts no `ci.yml` run, and the
  FR-062 `shared:` lines take effect only in a run something else started. This plan does not add the fixture globs to
  the workflow-level paths (Review notes R-1 gives the cost); SC-009m/l state the gap instead (§7.3). Suggested spec
  edit: SC-009m/l add "a push that changes only `tools/feed-contract/fixtures/{feeds,gen-feed}/**` is knowingly not
  proven on macOS and Linux by this case".

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-10-30 at `4f4c4175`)

| Fact | Source |
|---|---|
| `release.yml` inputs `plugin` / `prerelease` / `dry_run` (`dry_run` boolean, default `false`) | `.github/workflows/release.yml:26-52` |
| Top-level `concurrency: group: release-${{ inputs.plugin }}`, `cancel-in-progress: false`; no top-level `permissions:` | `release.yml:55-57`; `grep -n '^permissions' .github/workflows/*.yml` (no `release.yml` hit) |
| `release` job: `needs: [version, windows-installer, macos-installer, linux-archive]`, `environment: signing` | `release.yml:475-480` |
| `Create Release` (`if: ${{ !inputs.dry_run }}`, softprops v2, eight files incl. `installers/release.json`) | `release.yml:682-704` |
| `deploy-pages`: `needs: [version, release]`, `if: ${{ !inputs.dry_run }}`, `permissions: contents: write`; steps Checkout (`fetch-depth: 0`) `:753`, Generate Plugin Landing Page `:758`, Generate Signal Flow Diagram `:787`, Generate Root Index `:799`, **Generate Update Checker Endpoint `:859-899`**, Deploy to gh-pages `:901-906` (`keep_files: true`, no `cname`); file ends at `:906` | `release.yml:744-906` |
| The inline generator: `git tag -l "${pluginLower}/v*" --sort=-v:refname \| head -1` `:878`; `tag.replace(/.*\/v/, "")` `:886`; `fs.mkdirSync("_site", { recursive: true })` `:896` | `release.yml:859-899` |
| `docs.yml` deploys with `peaceiris/actions-gh-pages@v4`, `keep_files: true`, `cname: krateaudio.com`; `concurrency: group: docs-deploy, cancel-in-progress: true` | `.github/workflows/docs.yml:234-240`, `:14-16` |
| No workflow other than `release.yml` writes `versions.json` | `grep -n versions .github/workflows/*.yml` (only `release.yml:861`, `:897`, `:898`) |
| `ci.yml` workflow-level paths `:3-23`; `detect-changes` paths-filter `shared:` key with one line `- 'plugins/shared/**'` | `.github/workflows/ci.yml:3-23`, `:85-86` |
| `feed-tools.yml`: path filters (last lines `- '.github/workflows/backfill-sign.yml'` at `:16`, `:29`); `node --test` step `:67`; `PLAN = "1..46"` `:74`; `NAMES` `:75-98`; RSIGN list ends `:110` | `.github/workflows/feed-tools.yml:1-122` |
| Action versions in use: `actions/checkout@v6` (26), `actions/setup-node@v4` (10), `actions/upload-artifact@v7` (46), `actions/download-artifact@v8` (20), `peaceiris/actions-gh-pages@v4` (2) | `grep -ho 'uses: …@v…' .github/workflows/*.yml \| sort \| uniq -c` |
| `backfill-sign.yml` uses `gh` on `ubuntu-latest` with `env: GH_TOKEN: ${{ github.token }}` | `.github/workflows/backfill-sign.yml:32-38` |
| gen-feed: usage and exit codes `:10-24`; `readPluginsDir(dir, {withSetupIss})` `:133-154`; `readManifestsDir` (every `*.json` file) `:156-168`; `writeAtomic` (temp `wx` in `dirname(out)`, rename, no `mkdir`) `:174-190`; `projectV1(doc)` returns text, throws `E_INPUT` without a `plugins` object `:287-304`; `checkReleaseRows` (not exported) `:394-415`; `selectReleases(name, releases)` `:418-447`; `checkManifest` cross-checks (`preset_subdir`, `macos_pkg_id` when versions are equal; `windows_uninstall_key` = `extractAppId(setup.iss) + "_is1"`) `:494-522`; `main(argv, io, hooks)` returns 0/1/2/3, rethrows non-`ContractError` `:717-758`; exports incl. `main`, `buildFeed`, `projectV1`, `readPluginsDir`, `extractAppId`, `selectReleases` `:762-773` | `tools/gen-feed.js` |
| `backfill-sign.js`: `writeAtomic` copy `:84-103`; `ghJson` `:149-157`; `checkListRows` `:160-172`; `LIST_LIMIT 1000`, `LIST_FIELDS` `:64-65`; `DIGEST_RE` `:75`; `checkDownloaded` (refuses a null digest) `:306-323`; `productionDeps` (spawnSync, `shell: false`, throws on `r.error`) `:735-776`; `parseArgs` `:785`; `main(argv, io, deps = cliDeps(argv))` `:837`; entry point `:866` | `tools/backfill-sign.js` |
| `contract.js`: `ContractError(code, message, where)` `:15-25`; `CODES` `:28-99` (no `E_FEED_*`, `E_STEP_FAILED`, `E_V1_DRIFT`, `E_MANAGER_LATEST`); `parseSemVer` `:152`; `compareSemVer` throws `TypeError` on non-SemVer `:161-189`; `isPluginName` `:196`; `checkUrl` (`E_URL_NOT_HTTPS`, `E_URL_LATEST`) `:203-213`; `validateFeed(doc, {schema})` returns `{valid, errors[{instancePath, message}]}` `:478-486`; `checkReleaseManifest(doc, {repo, file})` `:516`; `formatTrustedComment` `:314`; `assetNameFor` `:628`; `perTagUrl` `:634`; exports `:638-662` | `tools/feed-contract/contract.js` |
| C1 v2 fixes `release_notes` to `""` | `tools/feed-contract/versions.v2.schema.json:220` |
| `CONTRACT.md`: §1 readers row "Phase 3 (`feed.yml`) — runs gen-feed, deploys the feed; exit 3 deploys and turns the run red" `:27`; §7 code table header `\| Code \| Class \| Raised by \| Meaning \|`; §7 exit-code list; §8 `:501`; §9 Phase 2 tools `:517`; last line `:778` | `tools/feed-contract/CONTRACT.md` |
| Hygiene: `scannedFiles()` singles `:34-50`, completeness list `:116-128`, `FETCH_RE` `:61`, `CHILD_EXEMPT` `:69-75`, destructured-`fs` rule `:139`; first statement = the write guard `:145-153`; roster `products` list `:258-269`; `fixtures are in sync` `:307-311`; `CONTRACT_HEADINGS` (eight, `## 9` not among them) `:315-324`; §7 code table read from the §7 section only, class cell must equal `CODES` `:399-420`; Phase 2 form test calls `main([], io)` `:523`, code-literal check `:529-534` | `tools/feed-contract/tests/hygiene.test.js` |
| Top-level tests today: 5+2+1+2+1+1+6+1+4+4+1+6+6+4+2 = **46** | `grep -c "^test(" tools/feed-contract/tests/*.test.js` |
| `fake-gh.js`: `STATE_FILE` = `fixtures/phase2/backfill/state.json`; `VIEW_JSON` = `{'tagName,publishedAt,isImmutable,assets', 'assets'}` `:39`; strict argv shapes for `list`, `view` (4 flags), `download` (`--pattern … --dir` only), `upload` `:217-268`; `assetBytes(tag, asset)` `:133-143`; `throwawaySigner()` `:146-157`; `makeWorld({tmp, mutate, hooks})` returns `{deps, calls, state, logs, served, …}`, `mutate(state, ctx)` runs before `auto` sizes / digests are filled `:161-178` | `tools/feed-contract/tests/helpers/fake-gh.js` |
| Backfill fake state: repo `krate-fixture/backfill`, 38 releases over ten tag prefixes (drafts above, prereleases above and below each stable); plugin inputs under `fixtures/phase2/backfill/plugins/` (nine `*plug` dirs) | `node -e` over `state.json` (this session); `ls fixtures/phase2/backfill/plugins` |
| `make-fixtures.js`: header "Contains no shipped plugin name" `:17`; `fixtureSignature(comment)` `:37`, exported `:1289`; `platformFor` / `installFor` / `linuxTree` / `payloadOf` / `releaseFor` builders `:153-190`, `:546-577` (module-private); `feeds()` writes `feeds/valid/<name>.json` and `feeds/cases.json` `:523-541`; `SETS` `:1197-1208`; `check()` reports `missing`, `differs`, `stray` `:1242-1265` | `tools/feed-contract/fixtures/make-fixtures.js` |
| `.gitattributes`: `tools/feed-contract/fixtures/** -text` | `.gitattributes:26` |
| Snapshot: 88 releases (0 prereleases, 0 drafts), `membrum/v0.12.2` published `2026-10-26T22:50:47Z`, nine plugin dirs with `version.json` + `installers/windows/setup.iss` and no `CHANGELOG.md`; only `membrum/version.json` has `preset_subdir` (`"Kits"`), none has `identifier`; Membrum AppId line `AppId={{B7E3C9D1-4A5F-4F2B-9E8C-3D6F1A8B2C5E}` | `fixtures/gen-feed/snapshot-2026-10-29/` (`ls`, `node -e`, `grep`, this session) |
| **Measured:** gen-feed v2 on the snapshot with an empty manifests dir exits 0, its output equals `expected.v2.json` byte for byte, and its `--project-v1` output equals `live-versions.json` byte for byte | this session, temp dir under `mktemp -d`, removed after |
| `update_checker.cpp` `parseResponse` (`plugins` object, `version` string, `download_url` / `release_notes` only when strings, `catch (...)` → empty) | `plugins/shared/src/update/update_checker.cpp:209-248` |
| `test_update_checker.cpp`: `kSettingsEnvVar` per OS `:26-33`; `readEnv` `:35`; `isUnder` `:70-78`; `ScopedSettingsDirRedirect` (fresh temp root, env restored in the destructor) `:81-114`; `MockUpdateChecker(config, mockResponse)` overriding `fetchJson` `:121-144`; `waitForResult` `:147-154`; Phase 1 case `:394-456`; the file does not include `<nlohmann/json.hpp>` | `plugins/shared/tests/test_update_checker.cpp` |
| `shared_tests` links `nlohmann_json::nlohmann_json` `:48-57`, uses `${CMAKE_SOURCE_DIR}/tests` as an include dir `:59-64`, has no `target_compile_definitions` | `plugins/shared/tests/CMakeLists.txt` |
| Precedent for a fixture-dir define: `MEMBRUM_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"` | `plugins/membrum/tests/CMakeLists.txt:222-225` |
| Tools on this box: `build/windows-x64-release/bin/Release/shared_tests.exe` exists; `rsign2 0.6.7`; `gh 2.89.0`; Node v24.7.0; 32 cores; `tools/run-clang-tidy.ps1` takes `-Target`, `-BuildDir`, no file list | `ls`, `--version`, `nproc`, `grep` (this session) |
| Phase 2 local TAP check script reads the `TAPCHECK` heredoc of `feed-tools.yml` and runs it with `CI=1` | `specs/krate-manager-phase2/evidence/tap-check.js:1-20` |

### 1.2 External facts (verified 2026-10-30)

| Fact | Source |
|---|---|
| `concurrency`: one running job or run per group; default cancels the pending one; `queue: single` (default) / `queue: max` ("Up to 100 jobs or workflow runs can be `pending`"; full queue cancels further ones); `queue: max` + `cancel-in-progress: true` is a validation error; FIFO order; "You can also specify `concurrency` at the job level" | https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax |
| `defaults.run.shell`: unspecified on Linux runs `bash -e {0}` ("runs a different command to when `bash` is specified explicitly"); `bash` runs `bash --noprofile --norc -eo pipefail {0}` | same page |
| Reusable workflows: "don't use the same value for `jobs.<job_id>.concurrency.group` in the called and caller workflows"; "A called workflow uses the name of its caller workflow in `${{ github.workflow }}`"; `GITHUB_TOKEN` permissions "can be only downgraded (not elevated) by the called workflow"; "the `github` context is always associated with the caller workflow"; no statement on a called workflow's top-level `concurrency` | https://docs.github.com/en/actions/reference/workflows-and-actions/reusable-workflows |
| Reports (not documentation) of GitHub cancelling a caller/callee pair as a concurrency deadlock when both declare one group | https://github.com/z-shell/.github/pull/507, https://github.com/github/gh-aw/pull/43078 |
| `actions/upload-artifact@v7`: `if-no-files-found` `warn` (default) / `error` / `ignore`; `include-hidden-files` default `false`; `overwrite` default `false`; `runs.using: node24` | https://raw.githubusercontent.com/actions/upload-artifact/v7/action.yml |
| `gh release download`: `-O/--output <file>` "The file to write a single asset to"; `--clobber` "Overwrite existing files of the same name"; `-p/--pattern`; the page says nothing about an existing output file without `--clobber` or about a pattern matching nothing | https://cli.github.com/manual/gh_release_download |
| `Atomics.wait(typedArray, index, value, timeout)` returns `"timed-out"` after `timeout` ms | https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Atomics/wait; Node main-thread behaviour measured (PD-4) |
| Every other external fact (peaceiris v4 inputs, reusable-workflow keyword list, `workflow_call` `GITHUB_SHA` / `GITHUB_REF`, `workflow_dispatch` default-branch rule, `needs.<job>.result` values, rate limits, `gh release list` / `view` fields, workflow commands and `GITHUB_OUTPUT`, the Tauri static JSON format, tauri-action's `uploadUpdaterJson`, Pages `max-age=600`) | the spec's "External facts used" table (spec lines 111-128), verified 2026-10-30 by the spec author; this plan relies on them as cited there |

---

## 2. File layout

```
.github/workflows/
  feed.yml                         NEW   reusable + dispatchable feed build, check and Pages deploy (§4.3)
  release.yml                      EDIT  remove "Generate Update Checker Endpoint" (:859-899); add job `feed` (§4.4)
  feed-tools.yml                   EDIT  two path-filter lines per trigger; PLAN 1..57; eleven NAMES (§4.5)
  ci.yml                           EDIT  two lines under the `shared:` paths-filter key (§4.5, FR-062)
tools/
  feed-publish.js                  NEW   collect | generate | outcome | compare | verdict (§4.1, §4.2)
  feed-contract/
    contract.js                    EDIT  seven CODES keys (§3.1)
    CONTRACT.md                    EDIT  seven §7 rows; new "## 10. Phase 3 tools" (§4.6)
    fixtures/
      make-fixtures.js             EDIT  registers set `phase3`; feeds() appends one cases.json row (§4.7)
      phase3/
        make-phase3.js             NEW   builds every phase3 fixture in memory (§4.7)
        membrum-v0.12.2.json       NEW*  the C2 manifest of the mixed fixture (FR-044)
        compare/<case>/{live,feed,releases}.json  NEW*  SC-007 triples
        compare/cases.json         NEW*  per case: flags, expected exit, stdout, report, annotations
      feeds/valid/one-v2-eight-v1.json  NEW*  (FR-045)
      feeds/cases.json             EDIT* one row appended
    tests/
      feed-publish-collect.test.js NEW   SC-003, SC-004, SC-006
      feed-publish-compare.test.js NEW   SC-007
      feed-publish-steps.test.js   NEW   SC-017, SC-018
      feed-pipeline.test.js        NEW   SC-001, SC-002, SC-005
      phase3-fixtures.test.js      NEW   SC-008
      workflow-shape.test.js       EDIT  Phase 3 block (SC-010); Phase 2 test untouched (SC-016)
      hygiene.test.js              EDIT  scan list, CHILD_EXEMPT, roster products, subtest (e) (SC-011)
      helpers/fake-gh.js           EDIT  additive: view field set, --output download, snapshot switch, Phase 2 file set,
                                         manager release helper (§4.8)
plugins/shared/tests/
  CMakeLists.txt                   EDIT  target_compile_definitions SHARED_FIXTURES_DIR (FR-046)
  test_update_checker.cpp          EDIT  one TEST_CASE + #include <nlohmann/json.hpp> (FR-046)
specs/krate-manager-phase3/evidence/
  scope-check.js                   NEW   SC-012 (git diff --name-only 4f4c4175 + status vs the FR-060 list)
```
`*` = written by `node tools/feed-contract/fixtures/make-fixtures.js --out tools/feed-contract/fixtures` (never by
hand). Untouched and checked unchanged by SC-012: `tools/gen-feed.js`, `docs.yml`, `backfill-sign.yml`, every
`setup.iss`, every `version.json`, `fixtures/gen-feed/snapshot-2026-10-29/**`.

---

## 3. Error model

### 3.1 New codes (`contract.js` `CODES` and `CONTRACT.md` §7 rows)

Every expected failure is a `ContractError(code, message, where)` (`contract.js:15-25`), printed once to stderr as
`feed-publish: <CODE>: <message>` (degrade: `feed-publish: WARN <CODE>: <message>`).

| Code | Class | Raised by | Meaning |
|---|---|---|---|
| `E_FEED_LIST_TRUNCATED` | none | `feed-publish.js collect` | `gh release list --limit 1000` returned 1000 rows; releases may be missing |
| `E_FEED_DOWNLOAD` | none | `feed-publish.js collect` | a downloaded `release.json` / `latest.json` differs from its release asset record in size, or in SHA-256 where GitHub lists a digest |
| `E_FEED_SCHEMA` | none | `feed-publish.js compare` | the generated feed fails C1 v2 or v1 (`validateFeed`); the message names the first `instancePath` |
| `E_FEED_NONDETERMINISTIC` | none | `feed-publish.js generate` | a second gen-feed run on the same inputs gave different bytes, exit code or stderr (PD-9) |
| `E_STEP_FAILED` | none | `feed-publish.js outcome` | a step's tool exited other than 0 or 3 |
| `E_V1_DRIFT` | none | `feed-publish.js compare` | a v1 key of a plugin differs from the live feed and no FR-040 case explains it |
| `E_MANAGER_LATEST` | degrade | `feed-publish.js collect` | the manager release's `latest.json` fails the FR-025 check; nothing is copied, the plugin feed still deploys |

Reused codes: `E_USAGE` (exit 2), `E_INPUT` (unreadable / non-JSON / wrong-shape input, a `gh` failure after its last
attempt, a non-writable output), `E_PLUGIN_NAME`, `E_DUPLICATE_PLUGIN` (PD-7), `E_TAG_FORMAT` (from `selectReleases`).
`E_V1_TYPE` (`contract.js:32`) is unrelated. The hygiene code-literal check (every `'E_…'` literal in the tool is a
`CODES` key, `tests/hygiene.test.js:529-534` pattern) applies to `feed-publish.js` through PD-16's subtest.

### 3.2 Exit codes (go into `CONTRACT.md` §10, not §7)

| Subcommand | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| `collect` | complete | abort: nothing of `releases.json`, `manifests/`, `manager/`, `collect-report.json` left in `--out-dir`; a pre-filled `--out-dir` refused untouched (PD-6) | usage | complete, manager degraded (`E_MANAGER_LATEST`) |
| `generate` | gen-feed 0, deterministic | gen-feed 1, `E_FEED_NONDETERMINISTIC`, a manager-copy / report failure, a `<site-dir>/manager` that already exists (`E_INPUT`, refused untouched); no `<site-dir>/manager/` created by this run is left | usage (own, or gen-feed's 2) | gen-feed 3, deterministic |
| `outcome` | continue (flag appended) | fail (`E_STEP_FAILED`, nothing appended) | usage | — |
| `compare` | no drift | a `block` drift; any drift with `--dry-run true`; `E_INPUT` (incl. a control character in the live file, PD-18); `E_FEED_SCHEMA` | usage (incl. missing / non-boolean `--dry-run`) | `--dry-run false` and every drift `warn` |
| `verdict` | green | red | usage | — |

---

## 4. Module design

### 4.1 `tools/feed-publish.js`

Form (FR-020, roadmap line 1000; as `tools/backfill-sign.js:1-35`): first line `#!/usr/bin/env node`; header comment
with WHY, the usage block (§4.1.7) and the exit codes; `'use strict'`; requires `node:child_process` (only inside
`productionDeps`), `node:crypto`, `node:fs`, `node:path`, `./feed-contract/contract.js`, `./gen-feed.js`; no network
module, no `fetch(`, no destructured `fs` require (`tests/hygiene.test.js:61-67`, `:139`); `module.exports` last; names no
plugin.

#### 4.1.1 Public API

```js
/** @typedef {{write(s: string): unknown}} Sink */
/** @typedef {{stdout: Sink, stderr: Sink}} Io */
/** @typedef {{status: number, stdout: string, stderr: string, attempts?: number}} GhResult */
/**
 * @typedef {{
 *   gh: (argv: string[]) => GhResult,                          // already wrapped in withGhRetry in production
 *   genFeedMain: (argv: string[], io: Io) => 0 | 1 | 2 | 3,    // tools/gen-feed.js main (hooks never passed)
 * }} Deps
 */

/** CLI. Throws TypeError('feed-publish: main(argv, io, deps): deps is required …') before reading argv when deps is
 *  not a non-null object. Returns the exit code of §3.2. Non-ContractError exceptions propagate (a tool bug). */
function main(argv, io, deps) {}

/** FR-021 to FR-026. @returns {0 | 1 | 3} (throws ContractError for aborts; main maps it to 1) */
function collect({ pluginsDir, repo, outDir, annotate }, io, deps) {}

/** FR-029a. @returns {0 | 1 | 2 | 3} */
function generate({ inDir, pluginsDir, repo, siteDir, annotate }, io, deps) {}

/** FR-029b. tool ∈ {'collect','generate','compare'}, exit a non-negative integer. @returns {0 | 1} */
function outcome({ tool, exit, githubOutput }, io) {}

/** FR-027, FR-040, FR-041. dryRun boolean. @returns {0 | 1 | 3} */
function compare({ live, feed, releases, dryRun, report, summary, annotate }, io) {}

/**
 * The pure rule (§4.2), exported for the SC-007 subtests that need no files.
 * @param {{liveDoc: object, feedDoc: object, rows: Array<{tagName, isPrerelease, isDraft, publishedAt}>}} args
 * @returns {{lines: string[], summaryLine: string,
 *            plugins: Array<{name: string, kind: 'same'|'newer'|'added'|'q9-corrected'|'drift',
 *                            drift?: {kinds: string[], severity: 'block'|'warn'}, line: string}>,
 *            counts: {same, newer, added, q9, drift, block, warn}}}
 */
function compareFeeds({ liveDoc, feedDoc, rows }) {}

/** FR-029c. results ∈ {'success','failure','cancelled','skipped'}; flags 'true' | 'false' | ''. @returns {0 | 1} */
function verdict({ dryRun, ref, build, deploy, feedDegraded, managerDegraded, driftWarned, evidence, annotate }, io) {}

/** FR-023a. @returns {(argv: string[]) => GhResult & {attempts: number}} */
function withGhRetry(gh, { sleep, log }) {}

/** The only code that spawns: gh, fixed argv, shell: false, wrapped in withGhRetry (sleep = Atomics.wait, log = stderr). */
function productionDeps() {}

module.exports = { main, collect, generate, outcome, compare, compareFeeds, verdict, withGhRetry, productionDeps,
  GH_ATTEMPTS, GH_BACKOFF_MS, MANAGER_TAG_RE, MANAGER_LATEST_URL };

if (require.main === module) process.exitCode = main(process.argv.slice(2), process, productionDeps());
```

Constants: `LIST_LIMIT = 1000`, `LIST_FIELDS = 'tagName,isPrerelease,isDraft,publishedAt'`, `VIEW_FIELDS =
'tagName,assets'`, `GH_ATTEMPTS = 3`, `GH_BACKOFF_MS = [5000, 15000]`, `MANAGER_TAG_RE =
/^manager\/v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$/u` (FR-024), `MANAGER_LATEST_URL =
'https://krateaudio.com/manager/latest.json'`, `DIGEST_RE = /^sha256:([0-9a-f]{64})$/u` (applied to the lower-cased
digest, as `backfill-sign.js:315`), `EVIDENCE_FILES = ['releases.json', 'collect-report.json', 'gen-feed.log',
'live-versions.json', 'compare.txt']`.

`withGhRetry`:

```js
function withGhRetry(gh, { sleep, log }) {
  return (argv) => {
    for (let attempt = 1; ; attempt += 1) {
      const r = gh(argv);
      if (r.status === 0 || attempt === GH_ATTEMPTS) return { ...r, attempts: attempt };
      const wait = GH_BACKOFF_MS[attempt - 1];
      log(`feed-publish: WARN gh attempt ${attempt}/${GH_ATTEMPTS} failed: ${ghLabel(argv)} (exit ${r.status}); ` +
          `retrying in ${wait / 1000} s`);
      const out = flagValue(argv, '--output');          // FR-023a: remove a partial download before the retry
      if (out !== undefined) fs.rmSync(out, { force: true });
      sleep(wait);
    }
  };
}
```

Only the exit status decides a retry; `withGhRetry` never parses `stdout`, so shape, size, digest, `E_TAG_FORMAT` and
1000-row results (decided by `collect` after a successful call) are never retried (FR-023a).

`productionDeps`:

```js
function productionDeps() {
  const rawGh = (argv) => {
    const r = childProcess.spawnSync('gh', argv, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'],
                                                  shell: false, maxBuffer: 64 * 1024 * 1024 });
    if (r.error) return { status: -1, stdout: '', stderr: String(r.error.code || r.error.message) };
    return { status: r.status === null ? -1 : r.status, stdout: r.stdout, stderr: r.stderr };
  };
  const sleep = (ms) => { Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms); };
  return { gh: withGhRetry(rawGh, { sleep, log: (line) => process.stderr.write(`${line}\n`) }),
           genFeedMain: genFeed.main };
}
```

No `cwd` option: every path the tool hands `gh` is absolute or relative to the step's working directory (the
checkout), as the workflow passes them.

#### 4.1.2 `collect` (FR-021 to FR-026, PD-6 to PD-8, PD-17)

1. `names` ← `genFeed.readPluginsDir(pluginsDir, {withSetupIss: false})` → each `versionJson.name`; PD-7 checks;
   sort bytewise.
2. `fs.mkdirSync(outDir, {recursive: true})`; refuse (`E_INPUT`, `<outDir> already holds <name>`) when any of
   `releases.json`, `manifests`, `manager`, `collect-report.json` exists in it (no `gh` call has been made yet, and
   nothing in `outDir` is touched); `mkdir <outDir>/manifests`. From here to the end of step 7 the body runs in
   `try { … complete = true } finally { if (!complete) removeOwn() }`, where `removeOwn` does
   `fs.rmSync(path.join(outDir, n), {recursive: true, force: true})` for each of the four names: after the refusal,
   any of them that exists was created by this run (PD-6).
3. `rows` ← `ghJson(['release','list','--repo',repo,'--limit','1000','--json',LIST_FIELDS], 'gh release list')`, PD-8
   row check; `rows.length >= 1000` → `E_FEED_LIST_TRUNCATED`.
4. `picks` ← `names.map((n) => [n, genFeed.selectReleases(n, rows)])` (all first: an `E_TAG_FORMAT` aborts before any
   view, FR-021).
5. Per plugin in order: `null` → `{name, status: 'no-stable'}`. Else for `release` of `[stable, sibling]` (sibling only
   when not `null`): `view` ← `ghJson(['release','view',tag,'--repo',repo,'--json',VIEW_FIELDS])` with the PD-8 view
   check (`view.tagName !== tag` → `E_INPUT`, the returned value printed per PD-18 (b)); if an asset is named
   `release.json`: `file` ← `path.join(outDir, 'manifests', tag.replace('/', '-') + '.json')` (the FR-022 path; T3
   builds its expected argv with the same `path.join` on its own out-dir), `gh(['release','download',tag,'--repo',repo,'--pattern',
   'release.json','--output',file])` (status ≠ 0 after the retries → `E_INPUT … after <n> attempts`), then
   `checkDownloaded(file, asset)`: size ≠ → `E_FEED_DOWNLOAD`; digest string and SHA-256 ≠ → `E_FEED_DOWNLOAD`; status
   `downloaded`. No such asset: `no-manifest`.
6. Manager (FR-024, FR-025): candidates = rows with `!isDraft && !isPrerelease && MANAGER_TAG_RE.test(tagName)`;
   highest by `compareSemVer` on the captured core; none → `{status: 'none'}`. Else view (same check); no `latest.json`
   asset → `none`; else `mkdir <outDir>/manager` and download to `path.join(outDir, 'manager', 'latest.json')`
   (the FR-024 path; FR-022 check, an abort on mismatch), then
   `checkLatestJson(text, version)`: JSON object; `version` string equal to the tag version after stripping one
   leading `v`; `platforms` a non-empty object; each value an object with a string `signature` and a string `url` that
   passes `contract.checkUrl` (`E_URL_*` is re-thrown as `E_MANAGER_LATEST` with the original code in the message).
   On failure: `fs.rmSync(<outDir>/manager, {recursive: true, force: true})`, `{status: 'degraded', tag, message}`
   (values from the file in `message` printed per PD-18 (b)), WARN line on stderr, exit 3 at the end.
   Success: `{status: 'copied', tag}`.
7. `writeAtomic(<outDir>/releases.json, JSON.stringify(rows, null, 2) + '\n')`, then
   `writeAtomic(<outDir>/collect-report.json, …)` (§4.1.3); `complete = true`.
8. stdout: per plugin `collect: <Name> stable <tag> (<status>)[, sibling <tag> (<status>)]` or `collect: <Name>
   no-stable`; then `collect: digest checked <n> of <m>` (PD-17); then `manager: none | copied <tag> | degraded
   <tag>: <message>`, each line written through `emit` (PD-18 (c)). With `--annotate`: `::notice
   title=feed-collect::<n> downloaded, <n> no-manifest, <n> no-stable, manager <status>` (FR-029e), messages
   single-lined (§4.1.6).

Request count for nine plugins without siblings or manager: 1 list + 9 views + ≤ 9 downloads = ≤ 19 (D2).

#### 4.1.3 `collect-report.json` (FR-026)

```json
{
  "plugins": [
    { "name": "<Name>", "stable": "<tag>", "sibling": "<tag> | null",
      "manifests": [ { "tag": "<tag>", "status": "downloaded" } ] },
    { "name": "<Name>", "status": "no-stable" }
  ],
  "manager": { "status": "none" },
  "gh_calls": 10
}
```
`manager` is `{status: "copied", tag}` or `{status: "degraded", tag, message}` in the other cases. Plugins bytewise by
name; `manifests` in stable-then-sibling order. 2-space JSON, final LF, written atomically.

#### 4.1.4 `generate` (FR-029a, PD-9)

1. `report` ← parse `<inDir>/collect-report.json` (`E_INPUT` if unreadable / not JSON / `manager.status` outside the
   three values).
2. `fs.mkdirSync(siteDir, {recursive: true})` (gen-feed does no `mkdir`, `tools/gen-feed.js:174-190`); refuse
   (`E_INPUT`, `<siteDir>/manager already exists`, nothing touched) when `<siteDir>/manager` exists, as `collect`
   refuses a pre-filled out-dir (PD-6). So nothing under `<siteDir>/manager` was made by anyone but this run.
3. `argv` = `['--plugins-dir', pluginsDir, '--releases', <inDir>/releases.json, '--manifests', <inDir>/manifests,
   '--repo', repo, ...(copied ? ['--manager-latest-url', MANAGER_LATEST_URL] : []), '--out', <siteDir>/versions.json]`.
4. Run 1: `code1 = deps.genFeedMain(argv, captureIo)`; `stderr1` captured; write `<inDir>/gen-feed.log` (atomic) and
   echo it to `io.stderr` line by line through `emit` (PD-18 (c)). If `code1` is 1 or 2 → return `code1` (step 6 has
   not run, so no `<siteDir>/manager` exists).
5. Run 2: same argv with `--out <inDir>/versions.again.json`; require `code2 === code1`, `stderr2 === stderr1` and
   the two files byte-equal (`Buffer.equals`); else `E_FEED_NONDETERMINISTIC` (exit 1; step 6 has not run).
6. When `copied`: `mkdir <siteDir>/manager` (sets `createdManager`), `fs.copyFileSync(<inDir>/manager/latest.json,
   <siteDir>/manager/latest.json, fs.constants.COPYFILE_EXCL)`. Any `fs` error here is caught: when `createdManager`,
   `fs.rmSync(<siteDir>/manager, {recursive: true, force: true})`; then `E_INPUT` naming the path and the error code
   (a `ContractError`, so `main` returns 1 instead of rethrowing, §4.1.1).
7. `--annotate`: `::notice title=feed-generate::exit <code1>, deterministic` and one `::warning
   title=feed-degrade::<line>` per stderr line starting `gen-feed: WARN ` (each line through PD-18 (c) first).
8. Return `code1` (0 or 3).

#### 4.1.5 `outcome` and `verdict` (FR-029b, FR-029c, PD-12)

`outcome`: `FLAG = {collect: 'manager_degraded', generate: 'feed_degraded', compare: 'drift_warned'}`; `--exit`
must match `/^(0|[1-9][0-9]*)$/` (else usage 2). `0` → append `<flag>=false\n`, `3` → `<flag>=true\n`
(`fs.appendFileSync`, never a rewrite); anything else → stderr `feed-publish: E_STEP_FAILED: <tool> exited <n>`,
append nothing, return 1.

`verdict` reasons, in this order, each printed as `verdict: red <reason>`:

| # | Condition | Reason text |
|---|---|---|
| 1 | `build !== 'success'` | `build <result>` |
| 2 | `!dryRun && ref !== 'refs/heads/main'` | `real run from <ref>` |
| 3 | `build === 'success'` and not #2: real run and `deploy !== 'success'`, or dry run and `deploy` ∉ {`success`, `skipped`} | `deploy <result>` |
| 4 | `feedDegraded === 'true'` | `feed degraded:` then each `gen-feed: WARN` line of `<evidence>/gen-feed.log` on its own line |
| 5 | `managerDegraded === 'true'` | `manager latest.json degraded: <manager.message of collect-report.json>` |
| 6 | `driftWarned === 'true'` | `v1 drift deployed: <drift … warn lines of compare.txt joined by " / ">` (Q1) |
| 7 | `build === 'success'` and an `EVIDENCE_FILES` entry is missing under `--evidence` | `evidence missing <file>` |

No reason → `verdict: green`, exit 0. Flag values other than `true`, `false`, `''`, results outside the four values,
`--dry-run` other than `true` / `false`, and an empty `--ref` are usage (2). Evidence files are read only when a reason
needs them; an unreadable file prints `(unreadable)` in place of its lines. Every quoted evidence line (#4, #6) and the
`manager.message` of #5 go through PD-18 (c) before they are printed or joined. `--annotate`: `::notice
title=feed-verdict::green` or one `::error title=feed-verdict::<reason>` per reason (multi-line reasons single-lined).

#### 4.1.6 Annotation output

`annotate(io, level, title, message)` writes `::<level> title=<title>::<message>\n` to `io.stdout`, where
`message.replace(/[\r\n]+/gu, ' ')` (FR-029e: no escaping is relied on). Titles are the fixed tokens
`feed-collect`, `feed-generate`, `feed-degrade`, `feed-validate`, `feed-compare`, `feed-drift`, `feed-verdict`.
`annotate` is the only function that writes a line starting with `::`; every other line goes through `emit` (PD-18
(c)), and every data-derived piece of `message` has already passed PD-18 (b) / (c), so the only line breaks the
replace collapses are the tool's own (verdict's multi-line reasons).

#### 4.1.7 CLI (usage block, `CONTRACT.md` §10.1)

```
usage: node tools/feed-publish.js collect --plugins-dir <dir> --repo <owner/name> --out-dir <dir> [--annotate]
       node tools/feed-publish.js generate --in-dir <dir> --plugins-dir <dir> --repo <owner/name> --site-dir <dir>
            [--annotate]
       node tools/feed-publish.js outcome --tool <collect|generate|compare> --exit <n> --github-output <file>
       node tools/feed-publish.js compare --live <file> --feed <file> --releases <file> --dry-run <true|false>
            [--report <file>] [--summary <file>] [--annotate]
       node tools/feed-publish.js verdict --dry-run <true|false> --ref <ref> --build <result> --deploy <result>
            --feed-degraded <true|false|""> --manager-degraded <true|false|""> --drift-warned <true|false|"">
            [--evidence <dir>] [--annotate]
```
`parseArgs` follows `backfill-sign.js:785-822`: a known subcommand; every flag of the subcommand's required set
present once; optional flags at most once; `--annotate` a switch; values non-empty and not starting `--`, **except**
the three verdict flags, which accept `''`; `--repo` must match `^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$`. A `usage` class
error prints the code line then the usage block and returns 2 (`tools/gen-feed.js:751-755`).

### 4.2 The comparison rule (`compareFeeds`, FR-040, FR-041, PD-10, PD-11)

`compare` reads the three files (`E_INPUT` on unreadable / not JSON), validates `--feed` with
`validateFeed(doc, {schema: 'v2'})` then `{schema: 'v1'}` (`E_FEED_SCHEMA: <schema> <instancePath>: <message>`),
checks `--releases` rows (PD-8 rule, `E_INPUT`), projects both with `JSON.parse(genFeed.projectV1(doc))` (`E_INPUT`
from `projectV1` when the live file has no `plugins` object), refuses a live key or string `version` /
`download_url` / `release_notes` holding a U+0000-U+001F / U+007F code unit (PD-18 (a), `E_INPUT`, before any result
line exists), then calls `compareFeeds`. In every line of the table below, `<n>`, `<L.v>` and `<old>` are printed
raw only when they pass `isPluginName` / `parseSemVer` / `checkUrl`, else as `JSON.stringify(value)` (PD-18 (b));
the new feed's values passed C1 (FR-041) and the `<tag>` / `<publishedAt>` come from a row that `stableRow` matched,
and are printed through the same rule (`<tag>` by `tagName === `${lower}/v${v}``, `<publishedAt>` by
`/^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:.]+Z$/u`).

Definitions, for plugin name `n` (`lower = n.toLowerCase()`): `stableRow(v)` = a row with `!isDraft && !isPrerelease
&& tagName === `${lower}/v${v}``; `preRow(v)` = the same with `isPrerelease === true`; `gt(a, b)` = both `parseSemVer`
non-null and `compareSemVer(a, b) > 0` (PD-11); `sel` = `genFeed.selectReleases(n, rows)` (lazily, only for the q9
cases). Names are the bytewise-sorted union of `L` and `N` keys. Per name, the first matching case wins:

| # | Case | Line |
|---|---|---|
| 1 | in both; version, download_url, release_notes all equal | `same <n> <v>` |
| 2 | in both; `gt(N.version, L.version)`; `stableRow(N.version)`; release_notes equal | `newer <n> <L.v> -> <N.v> (<tag> <publishedAt>)`[` download_url <old> -> <new>`] |
| 3 | in both; `preRow(L.version)` and no `stableRow(L.version)`; `sel?.stable.version === N.version`; download_url and release_notes equal | `q9-corrected <n> <L.v> -> <N.v> (prerelease <lower>/v<L.v>)` |
| 4 | in `L` only; `preRow(L.version)`; `sel === null` | `q9-corrected <n> <L.v> -> absent (prerelease <lower>/v<L.v>)` |
| 5 | in `N` only; `stableRow(N.version)` | `added <n> <N.v> (<tag> <publishedAt>)` |
| 6 | anything else | `drift <n> <kinds> <severity>` |

Drift kinds (fixed order, severity), collected for case 6:

| Kind | Severity | Applies when |
|---|---|---|
| `release_notes not empty` | block | in both and `L.release_notes` or `N.release_notes` is not `""` |
| `version lowered` | block | in both and `gt(L.version, N.version)` |
| `removed` | block | in `L` only (case 4 did not hold) |
| `version changed without a release` | warn | in both, versions differ, and neither `version lowered` nor case 2's release condition holds |
| `download_url changed without a newer release` | warn | in both, URLs differ, and not `gt(N.version, L.version)` with a stable row |
| `added without a release` | warn | in `N` only and no `stableRow(N.version)` |

Summary: `compare: <same> same, <newer> newer, <added> added, <q9> q9-corrected, <drift> drift (<block> block,
<warn> warn)`. Exit: 0 when `drift === 0`; 1 when `block > 0` or (`dryRun` and `drift > 0`); else 3. Stderr: one
`feed-publish: E_V1_DRIFT: <line>` per drift that fails the run (all on a dry run, `block` ones on a real run) and
`feed-publish: WARN E_V1_DRIFT: <line>` per `warn` drift of a real run. `--summary`: `fs.appendFileSync` of
``fence + "text\n" + lines.join('\n') + "\n" + fence + "\n"``, where `fence` is `` '`'.repeat(Math.max(3, r + 1)) ``
and `r` the longest run of backticks in `lines` (PD-18 (d)), so no value can close the fence. `--annotate`: `::notice title=feed-validate::v2 ok, v1 ok` (or one
`::error title=feed-validate::…` on `E_FEED_SCHEMA`), `::notice title=feed-compare::<summary line>`, then per drift
`::error title=feed-drift::<line>` (failing) or `::warning title=feed-drift::<line>` (real-run warn).

### 4.3 `.github/workflows/feed.yml` (FR-001 to FR-015, PD-1, PD-2)

#### 4.3.1 Full text

```yaml
name: Feed

# Builds versions.json (schema_version 2) from the GitHub releases and the release.json each carries, checks it
# against C1, against itself (determinism) and against the live feed, then deploys it to GitHub Pages.
# Spec: specs/krate-manager-phase3/spec.md FR-001 to FR-015; plan §4.3. Every decision is tools/feed-publish.js's.
# dry_run (default true on a manual dispatch) builds and checks only: deploy is skipped and nothing is pushed.

on:
  workflow_call:
    inputs:
      dry_run:
        description: 'Build and check the feed only: no Pages deploy'
        type: boolean
        required: true
  workflow_dispatch:
    inputs:
      dry_run:
        description: 'Build and check the feed only: no Pages deploy'
        type: boolean
        default: true

permissions:
  contents: read

jobs:
  build:
    runs-on: ubuntu-latest
    permissions:
      contents: read
    concurrency:
      group: krate-feed
      cancel-in-progress: false
      queue: max
    env:
      GH_TOKEN: ${{ github.token }}
    outputs:
      manager_degraded: ${{ steps.collect.outputs.manager_degraded }}
      feed_degraded: ${{ steps.generate.outputs.feed_degraded }}
      drift_warned: ${{ steps.compare.outputs.drift_warned }}
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Setup Node
        uses: actions/setup-node@v4
        with:
          node-version: '24'

      - name: Install feed-contract deps
        shell: bash
        run: |
          set -euo pipefail
          npm ci --prefix tools/feed-contract

      - name: Collect releases
        id: collect
        shell: bash
        run: |
          set -euo pipefail
          rc=0; node tools/feed-publish.js collect --plugins-dir plugins --repo "$GITHUB_REPOSITORY" --out-dir "$RUNNER_TEMP/feed-in" --annotate || rc=$?
          node tools/feed-publish.js outcome --tool collect --exit "$rc" --github-output "$GITHUB_OUTPUT"

      - name: Fetch live feed
        shell: bash
        run: |
          set -euo pipefail
          curl --fail --silent --show-error --location --retry 3 https://krateaudio.com/versions.json --output "$RUNNER_TEMP/feed-in/live-versions.json"

      - name: Generate feed
        id: generate
        shell: bash
        run: |
          set -euo pipefail
          rc=0; node tools/feed-publish.js generate --in-dir "$RUNNER_TEMP/feed-in" --plugins-dir plugins --repo "$GITHUB_REPOSITORY" --site-dir _site --annotate || rc=$?
          node tools/feed-publish.js outcome --tool generate --exit "$rc" --github-output "$GITHUB_OUTPUT"

      - name: Compare with the live feed
        id: compare
        shell: bash
        env:
          DRY_RUN: ${{ inputs.dry_run }}
        run: |
          set -euo pipefail
          rc=0; node tools/feed-publish.js compare --live "$RUNNER_TEMP/feed-in/live-versions.json" --feed _site/versions.json --releases "$RUNNER_TEMP/feed-in/releases.json" --dry-run "$DRY_RUN" --report "$RUNNER_TEMP/feed-in/compare.txt" --summary "$GITHUB_STEP_SUMMARY" --annotate || rc=$?
          node tools/feed-publish.js outcome --tool compare --exit "$rc" --github-output "$GITHUB_OUTPUT"

      - name: Upload site
        uses: actions/upload-artifact@v7
        with:
          name: feed-site
          path: _site/
          if-no-files-found: error

      - name: Upload evidence
        if: always()
        uses: actions/upload-artifact@v7
        with:
          name: feed-evidence
          path: ${{ runner.temp }}/feed-in/
          if-no-files-found: warn

  deploy:
    needs: build
    if: ${{ !inputs.dry_run && github.ref == 'refs/heads/main' }}
    runs-on: ubuntu-latest
    permissions:
      contents: write
    concurrency:
      group: krate-feed
      cancel-in-progress: false
      queue: max
    steps:
      - name: Download site
        uses: actions/download-artifact@v8
        with:
          name: feed-site
          path: _site

      - name: Deploy to gh-pages
        uses: peaceiris/actions-gh-pages@v4
        with:
          github_token: ${{ github.token }}
          publish_dir: ./_site
          keep_files: true
          cname: krateaudio.com

  verdict:
    needs: [build, deploy]
    if: always()
    runs-on: ubuntu-latest
    permissions:
      contents: read
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Setup Node
        uses: actions/setup-node@v4
        with:
          node-version: '24'

      - name: Install feed-contract deps
        shell: bash
        run: |
          set -euo pipefail
          npm ci --prefix tools/feed-contract

      - name: Download evidence
        uses: actions/download-artifact@v8
        continue-on-error: true
        with:
          name: feed-evidence
          path: ${{ runner.temp }}/evidence

      - name: Verdict
        shell: bash
        env:
          DRY_RUN: ${{ inputs.dry_run }}
          REF: ${{ github.ref }}
          BUILD_RESULT: ${{ needs.build.result }}
          DEPLOY_RESULT: ${{ needs.deploy.result }}
          FEED_DEGRADED: ${{ needs.build.outputs.feed_degraded }}
          MANAGER_DEGRADED: ${{ needs.build.outputs.manager_degraded }}
          DRIFT_WARNED: ${{ needs.build.outputs.drift_warned }}
        run: |
          set -euo pipefail
          node tools/feed-publish.js verdict --dry-run "$DRY_RUN" --ref "$REF" --build "$BUILD_RESULT" --deploy "$DEPLOY_RESULT" --feed-degraded "$FEED_DEGRADED" --manager-degraded "$MANAGER_DEGRADED" --drift-warned "$DRIFT_WARNED" --evidence "$RUNNER_TEMP/evidence" --annotate
```

Notes on the text:
- `rc=0; cmd || rc=$?` under `-e`: a command on the left of `||` does not trip `-e`, so `rc` holds the exit and
  `outcome` decides (D13). No `run:` script contains `|` except the `||` operator; SC-010's "no `|`" rule is tested as
  "no `|` that is not part of `||`" (§7.1 T9).
- The job ids are the job names (no `name:`), so a call from `release.yml` shows `feed / build`, `feed / deploy`,
  `feed / verdict` (SC-015).
- `deploy`'s `if:` carries no status function, so it also implies `success()`: a red `build` skips it. The `feed-site`
  artifact exists only when every `build` step before it succeeded (FR-011).
- `Upload evidence` excludes dot-files (`include-hidden-files` default `false`), i.e. only a leftover `writeAtomic` /
  gen-feed `.…tmp` would be dropped; none survives a normal run (`tools/gen-feed.js:174-190`; `collect` has no
  staging directory, PD-6).
- No `secrets.`, no `environment:`, no plugin name (FR-002, FR-015).

#### 4.3.2 Why `krate-feed` on `build` and `deploy` keeps deploys in order (PD-1)

Runs A and B in the order GitHub enqueues them. `build(A)` holds the group; `build(B)` waits (FIFO). When `build(A)`
ends, `deploy(A)` is enqueued behind `build(B)`; `build(B)` runs, ends, enqueues `deploy(B)` behind `deploy(A)`. So the
deploy order is the build order, and each later build's release list is a superset of the earlier one's (releases are
only added). `build(B)` may compare against the live file from before `deploy(A)`; the D5 rule explains that by the
newer release (`newer`), the same as the edge-cache case (D6). With `queue: max` nothing is cancelled until 100 jobs
wait (§1.2). The caller job in `release.yml` declares no `concurrency`, so no group is shared between caller and callee
(the reusable-workflows warning, §1.2).

### 4.4 `.github/workflows/release.yml` (FR-030 to FR-033)

1. Delete the step block from `      - name: Generate Update Checker Endpoint` (`:859`) through the blank line before
   `      - name: Deploy to gh-pages` (`:900`). Nothing else in `deploy-pages` changes.
2. Append after `deploy-pages` (end of file, after `:906`):

```yaml

  # versions.json (the in-plugin update checker and the manager read it) is built, checked and deployed by the
  # reusable feed workflow (specs/krate-manager-phase3/spec.md FR-030; plan §4.4). On a dry run feed.yml checks only.
  feed:
    needs: [release, deploy-pages]
    if: ${{ !cancelled() && needs.release.result == 'success' }}
    permissions:
      contents: write
    uses: ./.github/workflows/feed.yml
    with:
      dry_run: ${{ inputs.dry_run }}
```

No `secrets:` and no `concurrency:`. Checked against the Phase 2 shape test (SC-016): it never lists `release.yml`'s
jobs, `stepBlocks` of a step-less job is `[]`, the `continue-on-error` ban (`tests/workflow-shape.test.js:320`) is on
`release.yml` only (the `continue-on-error` of §4.3 is in `feed.yml`), `environment:` stays on `release` alone, and
`deploy-pages` keeps its `if: ${{ !inputs.dry_run }}`.

### 4.5 `feed-tools.yml` and `ci.yml`

> **Superseded for `ci.yml` (spec Clarifications 2026-10-30 (plan), Q6 re-ruled (a)):** `ci.yml` is not changed; the
> `ci.yml` paragraph below, file-layout line "ci.yml EDIT" and §8's "`ci.yml` (two lines)" no longer apply. R-1's cost
> (a `shared` change rebuilds all nine plugins) is why.

`feed-tools.yml` (FR-052): after `:16` and after `:29`, the two lines `      - 'tools/feed-publish.js'` and
`      - '.github/workflows/feed.yml'`. In the heredoc: `const PLAN = "1..57";` and the eleven §7.1 names appended to
`NAMES` (none contains an apostrophe, so the heredoc needs no quoting change). The RSIGN list and every other line stay.

`ci.yml` (FR-062): after `:86` (`              - 'plugins/shared/**'`), exactly

```yaml
              - 'tools/feed-contract/fixtures/feeds/**'
              - 'tools/feed-contract/fixtures/gen-feed/**'
```

### 4.6 `contract.js` and `CONTRACT.md` (FR-053)

`contract.js`: the seven §3.1 keys appended to `CODES` after `E_SIGN_KEY_MISMATCH` (six `none`, `E_MANAGER_LATEST:
'degrade'`). No other change.

`CONTRACT.md`: seven §7 table rows (§3.1 text); §1's Phase 3 reader row is left as it is (it already states the exit-3
behaviour). New final section **`## 10. Phase 3 tools`**:
- 10.1 Command lines (§4.1.7) and the `feed.yml` job graph (build → deploy → verdict; deploy only on a real run from
  `refs/heads/main`, Q2; `krate-feed` on build and deploy, PD-1).
- 10.2 Exit codes (§3.2 table).
- 10.3 `collect`: the request sequence (list, per plugin per selected release view [+ download], manager view
  [+ download]), the gh retry (3 attempts, 5 s / 15 s, never on a decided result, FR-023a), `collect-report.json`
  (§4.1.3), the FR-025 `latest.json` check.
- 10.4 `outcome` table and `verdict` reason table (§4.1.5).
- 10.5 The comparison rule (§4.2) with the severity table and the self-heal note of SI-7.
- 10.6 Annotation titles (§4.1.6).
- 10.7 User steps: after the green dry run, the live dispatch; after the live backfill, dry run then live run (FR-061).

The hygiene heading list (`CONTRACT_HEADINGS`, eight entries) is not extended: §9 is not in it either, and the code
table is read from §7 only.

### 4.7 Fixtures: `fixtures/phase3/make-phase3.js` (FR-044, FR-045, PD-14, PD-15)

```js
// Builds the Phase 3 fixtures in memory (spec specs/krate-manager-phase3/spec.md FR-044, FR-045, SC-007, SC-008;
// plan §4.7). Registered as make-fixtures.js set `phase3`; never run on its own. Names no plugin.
'use strict';
/** @param {{fixtureSignature: (comment: string) => string}} helpers  make-fixtures.js's own builder
 *  @returns {Record<string, string>}  relPath (under fixtures/) -> content */
function files({ fixtureSignature }) {}
module.exports = { files };
```

- **The manifest** (`phase3/<lower>-v<version>.json`, today `phase3/membrum-v0.12.2.json`): plugin = the one snapshot
  `version.json` with `preset_subdir` (assert exactly one); `tag = <lower>/v<version>` (assert a non-draft,
  non-prerelease row in the snapshot `releases.json`, and that it is `selectReleases`' stable pick); `pub_date` = that
  row's `publishedAt`; `assets` for `darwin-universal`, `linux-x86_64`, `windows-x86_64` (bytewise), each `{name:
  assetNameFor(…), url: perTagUrl('rolandzwaga/krate-audio', tag, name), size, sha256, signature:
  fixtureSignature(formatTrustedComment(name, version, key, asset)), format, min_os}` with the stated synthetic
  content rule of `make-fixtures.js:148-151` (`krate fixture asset <asset>\n`), `min_os` `11.0` / `2.31` / `10.0`;
  Linux `payload` = `{files, sha256: listing.digest(list), list}` for `listing.listingFromEntries(<the five-entry tree
  of make-fixtures.js:547-556 for <Name>>)`; `install` = `{vst3_bundle: <Name>.vst3, au_component: <Name>.component,
  preset_subdir: <version.json preset_subdir>, windows_uninstall_key: gen-feed.extractAppId(setup.iss) + '_is1',
  macos_pkg_id: 'com.krateaudio.' + lower}` (the cross-checks of `tools/gen-feed.js:494-522`). The module asserts
  `contract.checkReleaseManifest(doc, {repo})` passes before returning.
- **The mixed feed** (`feeds/valid/one-v2-eight-v1.json`): `genFeed.buildFeed({plugins: readPluginsDir(<snapshot
  plugins>, {withSetupIss: true}), releases: <snapshot releases.json>, manifests: [{file: 'manifests/<lower>-v<v>.json',
  text: <the manifest text>}], repo: 'rolandzwaga/krate-audio'})`; asserts `warnings.length === 0`, then the `text`.
- **SC-007 triples** under `phase3/compare/<case>/` (`live.json`, `feed.json`, `releases.json`) and
  `phase3/compare/cases.json` (`[{case, dryRun, exit, stdout: [...], stderr: [...], annotations: [...]}]`): invented
  plugins `Alpha`, `Bravo`, `Charlie`; the new feeds are full v2 entries (they must pass `validateFeed`); expected
  lines are literal strings in the module's case table, never computed by `feed-publish.js`. One case per §4.2 row
  and drift kind, plus the SC-007 list (URL with and without a newer release; both modes for every severity; mixed
  severities in one plugin and across two; v1 and v2 live; not-JSON live; no-`plugins` live; a v2-failing feed; the
  edge-cache case; PD-11's non-SemVer live version; PD-18's control-character live keys and value, and the
  backtick key).

`make-fixtures.js`: `const phase3 = require('./phase3/make-phase3.js');` (it also starts requiring `../../gen-feed.js`
through `make-phase3.js`; the header line "Requires only node: modules, ../contract.js and ../listing.js" gains
"and, for the phase3 set, ../../gen-feed.js"); `SETS` gains `{ name: 'phase3', dirs: ['phase3'], keep:
/^phase3\/make-phase3\.js$/u, files: () => phase3.files({ fixtureSignature }) }`; `feeds()` pushes one row after the
loops: `{ rule: 'Phase3/one-v2-eight-v1', file: 'valid/one-v2-eight-v1.json', schema: 'v2', expect: 'accept' }`. The
`feeds/valid/one-v2-eight-v1.json` file itself comes from the `phase3` set (two sets may write into one directory;
`builtFiles` throws only when two sets write the same path, `:1210-1219`). Regenerate with `node
tools/feed-contract/fixtures/make-fixtures.js --out tools/feed-contract/fixtures`; prove with `--check`.

### 4.8 `tests/helpers/fake-gh.js` changes (FR-050, all additive)

- `VIEW_JSON` gains `'tagName,assets'`.
- `download` accepts a second shape: `release download <tag> --repo <repo> --pattern <name> --output <file>` (exactly
  six flags); it writes the served bytes to `<file>` (whose directory must exist, as `gh -O` needs) and nothing else;
  the `--dir` shape is unchanged.
- `makeWorld({tmp, mutate, hooks, world = 'backfill'})`: `world: 'snapshot'` builds `state` from
  `fixtures/gen-feed/snapshot-2026-10-29/releases.json` (`repo: 'rolandzwaga/krate-audio'`, each row `isImmutable:
  false` and the three installer assets `<Name>-<ver>-Windows-x64.exe`, `-macOS.pkg`, `-Linux-x64.tar.gz` with `size` /
  `digest` `auto`, `<Name>` from the snapshot `version.json` whose lower-cased name is the tag prefix). Default is the
  Phase 2 world, so every Phase 2 caller is unchanged.
- `addPhase2Files(state, ctx, tag, {tmp, signer, pluginsDir})`: for use inside `mutate`; writes the three served
  installer bytes into `<tmp>/<tag-dashed>/`, signs each with `signer.sign(file, formatTrustedComment(…))`, records
  facts in-process (`darwinFactsFor`-style object; `releaseFacts.linuxFacts({tarball, extracted: tmp, objdumpText},
  {listDirectory: () => listing.listingFromTarGz(bytes)})`, as `tests/gen-release-manifest.test.js:66-84`), calls
  `genReleaseManifest.buildManifest({versionJsonText: <version.json with version set to the tag's>, setupIssText,
  assetsDir, darwinFacts, linuxFacts, repo: state.repo, pubDate: <row publishedAt>, publicKeyText:
  signer.publicKeyText})`, then appends the five asset records (`SHA256SUMS`, three `.minisig`, `release.json`, `size`
  / `digest` `auto`) and `ctx.setServed`s their bytes. No child process (FR-051).
- `addManagerRelease(state, ctx, {tag, isDraft = false, isPrerelease = false, publishedAt, latestJson})`: one row with
  a `latest.json` asset (`latestJson` a string or `null` for "no asset").
- Failure injection stays in the tests: a test-local `flaky(deps.gh, {match, failures, partial})` wrapper calls the
  fake and returns `{status: 1}` for the first `failures` matching calls (for a download it lets the fake write the
  file first, so the partial file exists), and records whether the output file existed at each retry. Tests compose
  `withGhRetry(flaky(world.deps.gh, …), {sleep: recordSleep, log: (l) => io.stderr.write(l + '\n')})`; they never call
  `productionDeps` (FR-051).

### 4.9 C++ guard case (FR-046, FR-054)

`plugins/shared/tests/CMakeLists.txt`, after `target_compile_features(shared_tests …)`:

```cmake
# Krate Manager Phase 3 (FR-046): the committed feed fixtures the "one v2 plugin among eight v1 plugins" case reads.
target_compile_definitions(shared_tests
    PRIVATE
        SHARED_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tools/feed-contract/fixtures"
)
```
(`${CMAKE_SOURCE_DIR}` is already how this file reaches the repo root, `:61`.)

`test_update_checker.cpp`: `#include <nlohmann/json.hpp>` among the includes; at the end:

```cpp
#ifndef SHARED_FIXTURES_DIR
#error "SHARED_FIXTURES_DIR is defined by plugins/shared/tests/CMakeLists.txt"
#endif

namespace {
std::string readFixture(const std::filesystem::path& rel) {
    std::ifstream in(std::filesystem::path(SHARED_FIXTURES_DIR) / rel, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_CASE("UpdateChecker - one v2 plugin among eight v1 plugins is read as v1", "[update]") {
    const std::string mixed = readFixture(std::filesystem::path("feeds") / "valid" / "one-v2-eight-v1.json");
    const std::string live = readFixture(std::filesystem::path("gen-feed") / "snapshot-2026-10-29" / "live-versions.json");
    REQUIRE_FALSE(mixed.empty());
    REQUIRE_FALSE(live.empty());

    std::vector<std::string> names;  // from the live file: no plugin name in this source
    for (const auto& item : nlohmann::json::parse(live).at("plugins").items()) names.push_back(item.key());
    REQUIRE(names.size() == 9);

    const auto envBefore = readEnv(kSettingsEnvVar);
    {
        ScopedSettingsDirRedirect settings;  // declared before every checker (its destructor comment, :97)
        for (const auto& name : names) {
            INFO("plugin " << name);
            const auto settingsDir = Krate::Plugins::Platform::getAppSettingsDirectory(name);
            REQUIRE(isUnder(settingsDir, settings.root()));
            const UpdateCheckerConfig config{.pluginName = name, .currentVersion = "0.0.1",
                                             .endpointUrl = "https://example.com/versions.json"};
            const auto resultOf = [&config](const std::string& document) {
                MockUpdateChecker checker(config, document);
                checker.checkForUpdate(true);
                REQUIRE(waitForResult(checker));
                return checker.getResult();
            };
            // After each check, re-derive the state path (a lost redirect would move it) and prove the write landed
            // under the temp root; the state of the live run is removed first, so existence after the mixed run proves
            // the mixed run wrote it (as the Phase 1 case, test_update_checker.cpp:409-412, :432-433).
            const auto statePathNow = [&name] {
                return Krate::Plugins::Platform::getAppSettingsDirectory(name) / "update_state.json";
            };
            const auto onLive = resultOf(live);
            const auto afterLive = statePathNow();
            REQUIRE(isUnder(afterLive, settings.root()));
            REQUIRE(std::filesystem::exists(afterLive));
            std::filesystem::remove(afterLive);
            REQUIRE_FALSE(std::filesystem::exists(afterLive));
            const auto onMixed = resultOf(mixed);
            const auto afterMixed = statePathNow();
            REQUIRE(isUnder(afterMixed, settings.root()));
            REQUIRE(std::filesystem::exists(afterMixed));
            CHECK(onMixed.updateAvailable);
            CHECK(onMixed.updateAvailable == onLive.updateAvailable);
            CHECK(onMixed.latestVersion == onLive.latestVersion);
            CHECK(onMixed.downloadUrl == onLive.downloadUrl);
            CHECK(onMixed.releaseNotes.empty());
            CHECK(onMixed.releaseNotes == onLive.releaseNotes);
        }
    }
    CHECK(readEnv(kSettingsEnvVar) == envBefore);
}
```
`isUnder`, `readEnv`, `kSettingsEnvVar`, `ScopedSettingsDirRedirect`, `MockUpdateChecker` and `waitForResult` are the
file's existing helpers (`:26-154`). The `exists` checks hold because `performCheck` calls `saveState()` on every
non-empty document before the result is published (`plugins/shared/src/update/update_checker.cpp:178-195`), and
`saveState` creates the parent directory (`:284-290`). No header or non-test source changes; `header-consumers.js`
therefore ends `none`.

### 4.10 Evidence script

`specs/krate-manager-phase3/evidence/scope-check.js` (copied shape of `specs/krate-manager-phase2/evidence/scope-check.js`):
spawns `git diff --name-only 4f4c4175` and `git status --porcelain`, fails on any path outside the FR-060 list plus
`specs/krate-manager-phase3/**` and `specs/Krate-manager-roadmap.md` (SI-8), checks the byte-frozen list is unchanged,
and prints `git diff 4f4c4175 -- .github/workflows/ci.yml` asserting exactly two `+` lines under `shared:` and no `-`
line. It applies the same technique to `release.yml` (FR-032, FR-033): it runs `git diff --unified=0 4f4c4175 --
.github/workflows/release.yml` and asserts that the `-` lines are exactly the `4f4c4175` text of `release.yml:859-900`
(the "Generate Update Checker Endpoint" step through the blank line before "Deploy to gh-pages", read with `git show
4f4c4175:.github/workflows/release.yml`), that the `+` lines are exactly the §4.4 block (blank line, the two comment
lines and the `feed` job), and that no other hunk exists. A dropped `keep_files: true`, an edited root-index step or a
changed input fails it. It is evidence, not a test (it spawns `git`, which the write guard would refuse in a test).

---

## 5. Dependencies

| Dependency | Version | Status | Source / justification |
|---|---|---|---|
| Node.js | 24 (CI `setup-node`), v24.7.0 locally | existing | `feed-tools.yml` `node-version: '24'`; `node -v` |
| `ajv` (npm, devDependency of `tools/feed-contract`) | 8.20.0 | existing, unchanged | `tools/feed-contract/package.json`; used through `contract.js` `validateFeed` |
| `gh` CLI | runner image (2.89.0 locally) | existing | already used by `backfill-sign.yml` on `ubuntu-latest`; `gh release list/view/download` flags per the spec's External facts and §1.2 |
| `curl` | runner image | existing | one call (FR-005) |
| `actions/checkout@v6`, `actions/setup-node@v4`, `actions/upload-artifact@v7`, `actions/download-artifact@v8`, `peaceiris/actions-gh-pages@v4` | as listed | existing in the repo (§1.1 counts) | upload-artifact inputs §1.2; peaceiris inputs per the spec's External facts (`action.yml` at `v4`) |
| `nlohmann_json` (C++) | the version the build already fetches | existing, already linked into `shared_tests` | `plugins/shared/tests/CMakeLists.txt:56` |

No new npm package, crate, Tauri plugin or action. No Rust in this phase.

---

## 6. Safety design

- **Injected roots and endpoints.** Every path `feed-publish.js` touches is a flag; tests give temp dirs (the write
  guard refuses writes outside `os.tmpdir()`) and the fake `gh`. The live-feed URL and `gh` exist only in `feed.yml`.
  The C++ case reads two committed files and writes only under `ScopedSettingsDirRedirect`'s temp root, `REQUIRE`d per
  real plugin name before that name's first checker (FR-046). There is no manager binary, so no `Platform` / `Roots`
  abstraction is involved.
- **Verify before publish (the order this phase owns).** download → size (+ SHA-256 where GitHub lists a digest)
  against the asset record (FR-022) → gen-feed's full C2 / C3-binding / C4-listing / install cross-checks (degrade on
  failure) → gen-feed self-check against C1 → determinism (second run, PD-9) → independent C1 v2 + v1 validation
  (FR-041) → the v1 comparison against the live file (FR-040) → only then `feed-site` exists → only a real run on
  `main` deploys it. Nothing downloaded is extracted or executed in this phase (no zip-slip surface; the C4 path rules
  are applied to listings by gen-feed, not to files on disk).
- **Who can write Pages.** Only `deploy`: `contents: write`, no checkout, no Node, no repository code, two steps
  (download the artifact, run peaceiris). `build` (which runs repository code and `gh`) holds `contents: read`; the
  token cannot be elevated by the called workflow (§1.2). `keep_files: true` preserves landing pages and manuals;
  `cname: krateaudio.com` preserves the custom domain the plugins' 301 and the manager depend on.
- **No key, no secret, no environment** in `feed.yml` (FR-002). Signatures are only parsed (by gen-feed), never made.
- **No shell strings from data.** Feed and release data never reach a shell: `gh` is spawned with a fixed argv and
  `shell: false`; workflow values reach scripts only as environment variables (PD-2, FR-014).
- **No workflow commands from data.** The live file is network data; PD-18 refuses control characters in it, prints
  any value that fails its validator as a JSON string, routes every non-annotation line through `emit` (no line it
  writes starts with `::`) and sizes the summary fence past any backtick run, so the `feed-*` annotations that SC-014 /
  SC-015 read cannot be forged or silenced by feed content.
- **Elevated helper / Tauri capability set:** not involved (Phases 4 and 10).

---

## 7. Test plan

All Node tests run with `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` (quoted glob,
parallel files, no serial flag); each new test file's first statement is `require('./helpers/write-guard.js');`.

### 7.1 The eleven new top-level tests

| # | File | Test name | SC | Assertion strategy |
|---|---|---|---|---|
| T1 | `feed-pipeline.test.js` | `feed pipeline reproduces the live v1 feed from the 2026-10-29 snapshot` | SC-001 | `makeWorld({world: 'snapshot'})`; `collect` → 0, `releases.json` deepEqual snapshot rows, empty `manifests/`, report nine `no-manifest` + `manager: none`, `calls` argv list = 1 list + 9 views (bytewise names), `gh_calls` 10; `generate` into a non-existent `--site-dir` → 0, dir created; `projectV1(versions.json)` Buffer-equals `live-versions.json`; `compare --dry-run true` → nine `same` lines, exit 0 |
| T2 | `feed-pipeline.test.js` | `feed pipeline is deterministic` | SC-002 | two full collect + generate runs in two temp dirs on the same Phase 2 world (with `addPhase2Files` on some releases and one manager release with a valid `latest.json`, so a successful `generate` creates `<site-dir>/manager/latest.json`) → byte-equal `releases.json`, manifest names and bytes, `versions.json`, `manager/latest.json`; on that same world a `genFeedMain` wrapper that rewrites `releases.json` between calls → `E_FEED_NONDETERMINISTIC`, exit 1, and no `<site-dir>/manager` (non-vacuous: the deterministic run on these inputs creates it) |
| T3 | `feed-publish-collect.test.js` | `collect downloads only the selected manifests` | SC-003 | Phase 2 world; `addPhase2Files` on stable, sibling, an older release and a draft; one manager release; exact expected argv list, written before the run (list, per plugin views + `--pattern release.json --output <path.join(outDir, 'manifests', '<tag-dashed>.json')>` downloads, manager view + `--pattern latest.json --output <path.join(outDir, 'manager', 'latest.json')>`), compared with `deepStrictEqual` to the recorded `calls` (no path is read back from the recording; PD-6 puts no random component in any argv); no `--clobber`, `api`, host; `gh_calls` = list length; a no-manager subtest records no manager call; a `digest: null` subtest accepts on size and prints `digest checked <n-1> of <n>` |
| T4 | `feed-publish-collect.test.js` | `collect aborts cleanly` | SC-004 | every FR-023 abort (list exit, non-array, 1000 rows, other-tag view, truncated download, digest mismatch, `E_TAG_FORMAT`, plus PD-7's three name cases) → 1, first stderr line `feed-publish: <CODE>:`, out-dir listing after the run equals its listing before (none of the four names, no temp file); usage → 2; a **pre-filled out-dir** subtest per name (`releases.json`, `manifests`, `manager`, `collect-report.json` each pre-created in turn, with sentinel content) → `main` 1, first stderr line `feed-publish: E_INPUT:`, zero recorded `gh` calls, the pre-existing entry byte-identical and the listing unchanged (PD-6); retry subtests (a) once, (b) twice, (c) three times, (d) never-retried results, with a recording `sleep` (`[5000]`, `[5000, 15000]`), WARN line count, `gh_calls`, partial-file removal before the retry; in (a) the expected argv list (failing argv twice, then SC-003's list) is written before the run with the same `path.join` rule as T3 and compared exactly |
| T5 | `feed-pipeline.test.js` | `a release without release.json stays v1-only` | SC-005 | Phase 2 world, Phase 2 files on every selected release except one plugin's stable → that plugin `no-manifest`, `gen-feed.log` without `WARN`, entry without `platforms` / `install`, v1 keys = `{version: selectReleases stable, download_url: version.json url, release_notes: ''}`; every other plugin has both |
| T6 | `feed-publish-collect.test.js` | `collect copies only a valid manager latest.json` | SC-006 | none / prerelease-only / draft-only → `none`, no `--manager-latest-url` (asserted on the argv a recording `genFeedMain` wrapper sees), no `manager` block; **tag-pattern subtests (FR-024), every row non-draft and non-prerelease, each with a valid `latest.json`:** `manager/v1.2.0-rc.1`, `manager/v01.0.0` and `managerx/v9.0.0` each alone → ignored (`manager: none`, no manager view recorded, no `manager/`); `manager/v0.9.0` with `manager/v0.10.0` (listed in that order and reversed) → `v0.10.0` viewed and copied (a lexical sort would pick `v0.9.0`); two releases → higher copied byte-equal, `generate` stages it and emits `manager.latest_json_url`; each FR-025 failure → exit 3, `WARN E_MANAGER_LATEST`, no `manager/`, other outputs written, `generate` stages none |
| T7 | `feed-publish-compare.test.js` | `compare explains every v1 difference or fails` | SC-007 | one subtest per `phase3/compare/cases.json` row: exact stdout, stderr, exit, report bytes (also on exit 1), `--summary` appended to a pre-filled file, annotation lines with `--annotate`; plus `--dry-run` missing / `maybe` → 2; **PD-18 cases:** a live key `"X\n::notice title=feed-verdict::green"` (and one with `\r`, and a live `version` holding U+0007) → exit 1, first stderr line `feed-publish: E_INPUT: live plugins."X\n::notice title=feed-verdict::green" holds a control character` (the key in its `JSON.stringify` form), no result line, empty report; a live key ``X```y`` (no control character, fails `isPluginName`) → its drift line prints the key as ``"X```y"`` and the `--summary` block uses a four-backtick fence; in **every** case, with `--annotate`, no stdout or stderr line starts with `::` except the tool's own lines with one of the §4.1.6 fixed titles |
| T8 | `phase3-fixtures.test.js` | `Phase 3 mixed feed fixture is real-shaped` | SC-008 | gen-feed `main` on (snapshot plugins, snapshot releases, a temp manifests dir with a copy of the manifest, repo) → 0, output Buffer-equals the fixture; `validateFeed` v2 + v1 valid; only one plugin (the `preset_subdir` one) has `platforms` and `install`; `--project-v1` Buffer-equals `live-versions.json`; `cases.json` row exists; `checkReleaseManifest` passes; `make-fixtures.check().ok` |
| T9 | `workflow-shape.test.js` | `feed.yml and release.yml keep the Phase 3 shape` | SC-010 | the existing text helpers (`jobNames`, `jobBlock`, `stepBlocks`, `field`, `nested`, `runText`): every SC-010 clause, with PD-1's placement (`concurrency` block on `build` and `deploy`, none at top level or on `verdict`; no other workflow file contains `group: krate-feed`), the `|` rule as `/(?<!\|)\|(?!\|)/` on each script, `release.yml` `feed` job exact, `deploy-pages` steps = `['Checkout', 'Generate Plugin Landing Page', 'Generate Signal Flow Diagram', 'Generate Root Index', 'Deploy to gh-pages']`, no other `versions.json` writer, `ci.yml` `shared:` block = the three lines; **and the FR text SC-010 does not enumerate:** `build`'s `env` is exactly `GH_TOKEN: ${{ github.token }}` (FR-003); the "Fetch live feed" script is exactly `set -euo pipefail` plus the one `curl --fail --silent --show-error --location --retry 3 https://krateaudio.com/versions.json --output "$RUNNER_TEMP/feed-in/live-versions.json"` line of §4.3.1 (FR-005); `deploy`'s first step is `uses: actions/download-artifact@v8` with `name: feed-site` and `path: _site` (FR-013), and its peaceiris step carries `github_token: ${{ github.token }}` |
| T10 | `feed-publish-steps.test.js` | `feed-publish tests never build real effects` | SC-017 | `main(argv, io)` and `main(argv, io, null)` throw `TypeError` naming `deps` with nothing written; a static scan of the five new test files and the edited helpers: 0 `productionDeps(` tokens, every `main(` call of `feed-publish.js` has three arguments |
| T11 | `feed-publish-steps.test.js` | `feed-publish decides every workflow branch` | SC-018 | `outcome` per tool × exit {0, 3, 1, 2, 127, `x`} appended to a pre-filled file; `verdict` full table 2 × 2 × 4 × 4 × 3³ × 2 = 1,728 rows against an oracle written from FR-029c's text, the ref-only reason case, the drift and WARN printing from fixture evidence; `generate` site-dir creation and gen-feed exits 1 / 2 / 3; `generate` with a pre-created `<site-dir>/manager/sentinel` (on a world whose manager is `copied`) → exit 1, first stderr line `feed-publish: E_INPUT:`, the sentinel byte-identical, no `latest.json` beside it, `genFeedMain` never called; `--annotate` lines single-lined for messages containing `\n`; `verdict` with a `gen-feed.log` / `compare.txt` / `manager.message` evidence line holding `\r` or starting `::` → printed in its `JSON.stringify` form, no output line starting `::` other than `feed-verdict` annotations |

`hygiene.test.js` (SC-011, names unchanged): `tools/feed-publish.js` and `fixtures/phase3/make-phase3.js` join
`scannedFiles()` and the completeness list; `tools/feed-publish.js` joins `CHILD_EXEMPT` and the roster `products`
list; subtest `(e) tools/feed-publish.js follows the tools/*.js form` of `tools stay offline` checks the first line,
`main([], io, stubDeps) === 2` with a `usage: node tools/feed-publish.js` stderr line, and every `'E_…'` literal in the
file a `CODES` key (PD-16). `CONTRACT.md documents every code and field` then covers the seven new codes with no change.

Existing tests that must stay green unchanged: `release.yml and backfill-sign.yml keep the Phase 2 shape` (SC-016),
`C1 feed fixtures` (reads the new `cases.json` row), `fixtures are in sync with make-fixtures.js`, every backfill test
(the fake's default world is unchanged).

### 7.2 FR → test map (FRs without their own SC)

| FR | Test |
|---|---|
| FR-020 (form, deps required, entry point) | T10; hygiene (e) |
| FR-023a (retry) | T4 (a)-(d) |
| FR-026 (report, `gh_calls`) | T1, T3, T4 |
| FR-027 / FR-028 (exit codes) | T7, T11, T4 |
| FR-029a-e | T2, T6, T11, T7 |
| FR-001-FR-015, FR-030, FR-031, FR-062 | T9 (FR-003, FR-005, FR-013 by the T9 clauses beyond SC-010) |
| FR-032 / FR-033 (nothing else in `release.yml` changes) | `evidence/scope-check.js` `release.yml` diff check (§4.10); T9 for the `deploy-pages` step names and the `feed` job |
| PD-18 (control characters, `::` lines, summary fence) | T7 PD-18 cases; T11 `verdict` evidence-line case |
| PD-6 (pre-filled out-dir refusal) / generate's `<site-dir>/manager` refusal | T4 pre-filled subtests; T11 sentinel subtest |
| FR-040 / FR-041 | T7 |
| FR-044 / FR-045 | T8 |
| FR-046 / FR-054 | SC-009 run (§7.4) |
| FR-050 / FR-051 | T10, hygiene, every test using the fake |
| FR-052 | the TAP check (§7.4 step 2) |
| FR-053 | hygiene `CONTRACT.md documents every code and field`; review of §10 |
| FR-060 / FR-061 | `evidence/scope-check.js` (SC-012); T9 (no new trigger) |

### 7.3 PENDING-CI rows (exact job and assertion)

| Row | Job | Assertion |
|---|---|---|
| SC-013m / SC-013l | `feed-tools.yml` `node-test (macos-latest)` / `node-test (ubuntu-latest)` (user's push) | step `node --test` green: TAP check prints `TAP plan=1..57 top-level=57`; `Checkout unchanged` empty |
| SC-009m / SC-009l | `ci.yml` `macOS (Clang)` / `Linux (GCC)` build of `shared_tests`, run by `macOS tests (shard N)` / `Linux tests (shard N)` on the user's PR to `main` | the case `UpdateChecker - one v2 plugin among eight v1 plugins is read as v1` passes (a shard's Catch2 totals with 0 failed); the `shared` filter is set by `plugins/shared/**` in that PR. **Known gap (SI-11, stated, not closed):** a later push that changes only `tools/feed-contract/fixtures/{feeds,gen-feed}/**` starts no `ci.yml` run (its workflow-level `paths`, `ci.yml:3-23`, do not list `tools/**`, and `ci.yml` has no `workflow_dispatch`, `:3-38`), so such a push is knowingly not proven on macOS and Linux by this case. The fixture data itself is proven on all three OSes by T8 in `feed-tools.yml` (its paths include `tools/feed-contract/**`, `feed-tools.yml:9`, `:22`); the C++ case re-runs on macOS and Linux at the next push or PR that touches `plugins/shared/**` (the manual route is a PR that touches `plugins/shared/tests/test_update_checker.cpp`) |
| SC-014 | `feed.yml` `workflow_dispatch`, `dry_run: true`, on `main` after the merge | jobs API: `build` success, `deploy` skipped, `verdict` success; annotations `feed-validate` `v2 ok, v1 ok`, `feed-compare` `… 0 drift (0 block, 0 warn)` with the counts summing to the plugin count, `feed-collect` `… manager none`, `feed-verdict` `green`; no `feed-drift` / `feed-degrade`; artifact list has `feed-site` and `feed-evidence`; no gh-pages commit after the run start |
| SC-015 | `release.yml` dispatch with `dry_run: true` after the merge | `deploy-pages` skipped; `feed / build` success; `feed / deploy` skipped; `feed / verdict` success with `feed-verdict` `green`; no gh-pages commit |

### 7.4 Local close procedure (Windows)

1. `node tools/feed-contract/fixtures/make-fixtures.js --check` → `committed fixtures match the builder`.
2. `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > f:/tmp/km3-all.tap` (≈ 20 s; the Phase 2
   run of 46 tests took 14-18 s, `specs/krate-manager-phase2/compliance.md:7`), then `node
   specs/krate-manager-phase2/evidence/tap-check.js f:/tmp/km3-all.tap` (it extracts the `TAPCHECK` heredoc from
   `feed-tools.yml` and runs it with `CI=1`; `rsign2 0.6.7` is installed here, so the rsign blocks do not skip) →
   `TAP plan=1..57 top-level=57`.
3. `"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests` (zero warnings), in parallel
   with step 2 (independent).
4. `node tools/run-gate.js --filter '[update]' shared_tests` → `GATE GREEN`.
5. `build/windows-x64-release/bin/Release/shared_tests.exe "UpdateChecker - one v2 plugin among eight v1 plugins is
   read as v1" -s > f:/tmp/km3-sc009.log` (the one direct single-case run CLAUDE.md allows): `9 == 9`, nine of each
   per-name assertion (including the two post-check `isUnder` / `exists` pairs), `test cases: 1 | 1 passed`.
6. `node tools/header-consumers.js --tag '[update]'` → last line `none`.
7. Reconfigure the Ninja tree (`"$CMAKE" --preset windows-ninja` or the `clang-tidy-setup` skill's command: the test
   CMake changed, and a stale compile database gives phantom findings), then `./tools/run-clang-tidy.ps1 -Target
   shared -BuildDir build/windows-ninja` → zero findings for `test_update_checker.cpp`.
8. `node specs/krate-manager-phase3/evidence/scope-check.js` (SC-012).
9. `node tools/clean-test-output.js --apply`.

No pluginval (test-only C++ change, FR-054). No CPU lane (no timing test).

---

## 8. Build integration and gate lanes

- **Workspace:** no app workspace in this phase. The pipeline code is the flat `tools/*.js` set plus
  `tools/feed-contract/` (its own `package.json`, `ajv` only).
- **Scripts:** none added to any `package.json`; the tools are run as `node tools/feed-publish.js …`.
- **Workflow files changed:** `feed.yml` (new), `release.yml`, `feed-tools.yml`, `ci.yml` (two lines).
- **Lanes:** the **pipeline lane** (`node --test` set, the TAP check, `make-fixtures --check`, scope check) and a
  **C++ guard lane** for one case (build `shared_tests`, `run-gate.js --filter '[update]'`, the `-s` single-case run,
  `header-consumers.js`, clang-tidy). No app lane. The two lanes are independent and run concurrently (§7.4 step 3
  beside step 2). `feed-tools.yml` is the per-push gate for the Node side on all three OSes; `ci.yml` covers the C++
  case on macOS and Linux whenever `plugins/shared/**` or (after FR-062) the two fixture directories change in a run
  that `ci.yml`'s own workflow-level paths start. A fixture-only push is knowingly not proven on macOS and Linux by
  the C++ case (SI-11; stated in SC-009m/l, §7.3, with the route that does prove it; Review notes R-1 says why the
  workflow-level paths are not widened).

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| A generator or tool fault reaches nine shipped editors that poll the live file | Dry run first (default on a dispatch), `block` drift fails the build before `feed-site` exists, C1 re-validation and determinism before compare, deploy only on `main`; SC-001 proves the first v2 deploy is v1-identical to today's file (measured byte-identical this session) |
| A blocking drift that cannot self-heal (`release_notes not empty`, `removed`) wedges every run (SI-7) | Latent today (no other `versions.json` writer); recovery is a hand edit of `gh-pages`' `versions.json`, then a dry run and a real run; documented in `CONTRACT.md` §10.5 |
| Concurrent feed runs deploy out of order | PD-1: `krate-feed` with `queue: max` on `build` and `deploy`; order argument in §4.3.2 |
| `docs.yml` or `release.yml`'s `deploy-pages` pushes gh-pages at the same moment (different groups) | A rejected push fails that run (no retry, Q3); re-dispatch `feed.yml` (idempotent, `keep_files` on all writers) |
| A deploy drops the custom domain | `cname: krateaudio.com` on every `feed.yml` deploy (as `docs.yml:240`); SC-010 asserts it. `release.yml`'s `deploy-pages` has no `cname` today and keeps the existing `CNAME` through `keep_files: true`; unchanged by this phase |
| Re-running a failed `feed.yml` job in the same run collides with an existing artifact name (`upload-artifact` `overwrite` default `false`) | Not verified whether GitHub keeps a previous attempt's artifact for a job re-run; the documented recovery is a fresh dispatch, not "Re-run jobs"; recorded in `CONTRACT.md` §10.7 |
| This environment's token cannot read logs or download artifacts | Every verdict is also a check-run annotation (FR-029e); SC-014 / SC-015 are measured through the jobs and annotations APIs |
| `workflow_dispatch` runs only the default branch's file, so SC-014 / SC-015 cannot run before the merge | Recorded as PENDING-CI after the merge; local proof is T1-T11 |
| A transient GitHub API error fails a release's feed | Bounded retry (3 attempts, 5 s / 15 s, FR-023a); a still-failing run is red before deploy and re-dispatchable |
| The live file is unreachable (Pages down, offline runner) | `curl --fail --retry 3`; on failure the run is red before deploy, the live feed stays as it was |
| `latest.json` from a future `release-manager.yml` has a shape the FR-025 check rejects | Degrade: the plugin feed deploys, the run is red, the previously published `manager/latest.json` stays (`keep_files`); Phase 11 owns the format |
| Windows legs of `feed-tools.yml`: removing `manifests/` after an abort can fail with `EPERM` while a scanner holds a file | Low risk on hosted runners; the abort is already exit 1 (red leg); a cleanup failure is reported by the T4 "listing unchanged" assertion, never hidden |
| A live file (network data) carries a workflow command or a fence-closing backtick run | PD-18: control characters refused (`E_INPUT`), invalid values printed as JSON strings, every non-annotation line through `emit`, summary fence longer than any backtick run; T7, T11 |
| Roadmap risk list items (WebKitGTK quirks, a DAW holding plug-in files, admin prompts, the manager offline) | Not applicable to this phase: no app runs and nothing is installed; they belong to Phases 4, 10 and 5 |

---

## 10. Open questions (for the user)

None that block the build. Two recorded choices the user may want to revisit: SI-5 (Phase 3 accepts a `release.json`
whose asset record has no digest, checked by size alone) and SI-7 (the two blocking drift kinds that no `feed.yml` run
can clear). SI-10 (PD-18's stricter live-file input rule) and SI-11 (the stated fixture-only CI gap) need spec text
before build, as the other SIs do.

---

## Review notes

- **R-1 (review issue "4.5 ci.yml / 8 Lanes / 7.3 SC-009m/l": the first resolution is rejected, the second is
  applied).** The plan does not add `tools/feed-contract/fixtures/{feeds,gen-feed}/**` to `ci.yml`'s workflow-level
  `on.push.paths` / `on.pull_request.paths`. In `ci.yml` a true `shared` output does not run `shared_tests` alone:
  45 plugin jobs' `if:` include `needs.detect-changes.outputs.shared == 'true'` (`ci.yml:490-554`, `:785-1049`,
  `:1284-1348`). A fixture-only push would therefore build and test all nine plugins on every OS to re-run one parse
  case whose code did not change. That breaks the project rule "Run only the tests the change set can reach"
  (CLAUDE.md). The case's `MockUpdateChecker` overrides `fetchJson` (`test_update_checker.cpp:121-144`), so a fixture
  change cannot reach the per-OS fetch paths (the macOS and Linux branches of `fetchJson`, `update_checker.cpp:169-175`). The fixture data is proven on all three
  OSes by T8 in `feed-tools.yml`. As the issue's second resolution asks, SC-009m/l state the gap (§7.3, SI-11) and
  name the route that proves it.
- **R-2 (review issue "PD-6 / §4.1.2 step 5": resolved by a third route).** Neither suggested route is taken as
  written. A deterministic `<out-dir>/.collect-stage` would still put `.collect-stage/` into every `--output`, which
  departs from FR-022's text as far as `mkdtemp` did. The SI route would amend FR-022 and SC-003. PD-6 now drops
  staging instead: it refuses a pre-filled out-dir and downloads straight to the FR-022 / FR-024 paths. On an abort,
  a `finally` removes the four names, and after the refusal every one of them that exists was created by this run.
  The argv matches FR-022's text exactly, T3 / T4 write their expected lists before the run, and no spec edit is
  needed.
- **R-3 (review issue "§4.1.4 generate": the flag is applied where it can matter).** Steps 4 and 5 no longer remove
  `<siteDir>/manager`. With the step-2 refusal, both steps return before step 6, the only step that creates the
  directory, so a removal there could only delete a directory this run did not make. `createdManager` guards the one
  window in which this run created the directory and a later call failed (the copy in step 6).
