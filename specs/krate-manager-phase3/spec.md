# Feature Specification: Krate Manager Phase 3 — Feed Workflow and Pages Deploy

**Spec slug:** `krate-manager-phase3`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 3 (lines 423-457); Decisions log Q9 (line 32), ROSTER (line 33),
Q10 (line 34); Positioning "What stays exactly as it is" (lines 47-58); Architecture "Feed and artifact model" (lines
128-148); Reuse Inventory rows "Release workflow" and "Feed generator" (lines 198-199); Repository Layout (line 231);
Dependency Graph (lines 909-942); Cross-Cutting Constraints (lines 961-1002)
**Depends on:** Phase 1 (✅, roadmap line 258): the C1 schema, `tools/gen-feed.js` (v2 mode, `--project-v1`, exit
code 3), the snapshot fixture `fixtures/gen-feed/snapshot-2026-10-29/`, the C++ v2 guard case and `feed-tools.yml`.
Phase 2 (✅, roadmap line 340): `release.json` on every new release, the backfill that attaches it to the current
releases, `release.yml`'s `dry_run` input, and the fake-`gh` test helper. Phase 11 depends on this phase
(`release-manager.yml` calls `feed.yml` to publish `manager/latest.json`, roadmap lines 796-803, 940).
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-10-30 · verified against `feat/krate-manager` at `4688d20e` (Phase 2); HEAD is now `4f4c4175`
("chore(krate-manager): add the release signing public key", `git log --oneline -3`), the base SC-012 diffs against

---

## Overview

Phase 3 puts the v2 feed live. A new reusable workflow, `.github/workflows/feed.yml`, lists the repository's releases
with `gh`, downloads the `release.json` of each plugin's selected releases, runs the Phase 1 generator
(`tools/gen-feed.js`), copies the manager's `latest.json` when a manager release exists, and deploys the result to
GitHub Pages with `peaceiris/actions-gh-pages@v4`, `keep_files: true` and `cname: krateaudio.com`. A `dry_run` input
(default `true` for a manual dispatch) builds and checks the same files, compares their v1 projection with the live
`versions.json`, and uploads them as a workflow artifact without deploying. `release.yml` stops generating the feed
inline and calls `feed.yml` after each release, passing its own `dry_run`.

The nine shipped plugins poll this file (`plugins/iterum/src/update/iterum_update_config.h:12`), so the phase is built
around one rule: the v1 keys they read (`plugins.<Name>.version`, `download_url`, `release_notes`,
`plugins/shared/src/update/update_checker.cpp:215-242`) never change except when a newer release explains the change,
and a generator fault is seen in an artifact, not by users. Plugins whose newest release has no `release.json` keep
their v1 entry with no `platforms`; after the Phase 2 backfill all nine have one.

Nothing in this phase builds the manager app, changes the landing page, moves the feed to another domain, changes an
installer, or changes plugin source. The only C++ change is one added test case.

---

## Scope

**In scope (roadmap Phase 3 components, lines 427-444):**

1. `.github/workflows/feed.yml` (`workflow_call` + `workflow_dispatch`, one `dry_run` input): collect, generate,
   check, compare, upload, deploy (roadmap lines 428-433).
2. The `dry_run` path: build, validate against C1, compare the v1 projection with the live file, upload the artifact,
   never call `peaceiris/actions-gh-pages` (roadmap lines 434-439).
3. `release.yml`: the inline "Generate Update Checker Endpoint" step is removed and a job calls `feed.yml` with
   `dry_run` passed through (roadmap lines 440-441).
4. Plugins without a `release.json` stay v1-only (roadmap lines 442-444).
5. A new Node tool, `tools/feed-publish.js`, holding the `gh` side of the workflow (`collect`), the generator run and
   its determinism check (`generate`), the exit-code-to-job-output mapping (`outcome`), the v1 comparison
   (`compare`) and the final verdict (`verdict`), so every decision the workflow makes is tested offline (D1, D13; an
   addition to the roadmap's tool list, Traceability).
6. The one-v2 / eight-v1 fixture feed and its C++ guard case (roadmap lines 449-451), read in place by Phase 5.
7. Tests, lane and contract plumbing: new `node --test` files, `feed-tools.yml` path filter and TAP plan,
   `CONTRACT.md` rows for the new codes.

**Non-goals (owned elsewhere):**

| Not in Phase 3 | Owner |
|---|---|
| The live dispatch of `feed.yml` (`dry_run: false`) and checking a plugin's update banner against the live feed | A user step after the green dry run (roadmap lines 454-455), not this phase's exit criterion |
| The landing page, its Krate Manager card, `docs.yml` | Phase 12 (roadmap line 885-887); `docs.yml` is not touched |
| Running the backfill live, so all nine plugins get `platforms` | User step after Phase 2's dry run (Phase 2 spec "Merge instructions" step 4); readiness check in Phase 12 (roadmap lines 888-890) |
| Building or releasing the manager, producing `latest.json` | Phase 11 (`release-manager.yml`) |
| Reading the feed in Rust, the offline cache, platform-key resolution | Phase 5 |
| Cryptographically verifying plugin asset signatures | Phase 2 verified each at signing time (Phase 2 FR-045, FR-055); Phase 7 verifies on the client |
| Any change to gen-feed's selection rule (Q9), C1, C2, C3 or C4 | Phase 1 (frozen contracts) |
| Changing the in-plugin parser, an installer script, `version.json`, `ci.yml`, or plugin `src/` | Never in this phase (roadmap line 457 for the installers; the parser, `version.json` and plugin `src/` are this spec's fence, FR-060; `ci.yml` is not changed, FR-062) |
| Moving the feed off `krateaudio.com` / GitHub Pages | Never in this phase (roadmap line 457) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 3 does with it |
|---|---|---|
| `.github/workflows/release.yml:26-52` | `workflow_dispatch` inputs `plugin` (choice, `:29-44`), `prerelease` (`:45-48`), `dry_run` (boolean, default `false`, `:49-52`) | ✅ Unchanged. `dry_run` is passed through to `feed.yml` (FR-030). |
| `.github/workflows/release.yml:55-57` | `concurrency: group: release-${{ inputs.plugin }}`, `cancel-in-progress: false` | ✅ Unchanged; `feed.yml` uses its own literal group (FR-012). |
| `.github/workflows/release.yml:475-741` | `release` job: signs, writes `release.json`, `Create Release` (`:682-704`, skipped on dry runs, uploads `installers/release.json` among eight files) | ✅ Unchanged. The release it creates, with its `release.json`, exists before `feed.yml` runs (FR-031). |
| `.github/workflows/release.yml:744-906` | `deploy-pages`: `needs: [version, release]`, `if: ${{ !inputs.dry_run }}` (`:746-747`), `permissions: contents: write` (`:749-750`); steps Checkout `fetch-depth: 0` (`:753-756`), Generate Plugin Landing Page (`:758-785`), Generate Signal Flow Diagram (`:787-797`), Generate Root Index (inline Python, `:799-857`), **Generate Update Checker Endpoint** (inline Node v1 feed, `:859-899`), Deploy to gh-pages (`peaceiris/actions-gh-pages@v4`, `publish_dir: ./_site`, `keep_files: true`, no `cname`, `:901-906`) | 🔶 The Generate Update Checker Endpoint step is removed; every other step is unchanged (FR-032). The roadmap's `release.yml:645-685` / `:688-692` citations predate Phase 2's insertions; these are the current lines. |
| `.github/workflows/release.yml:859-899` (the inline generator) | Per `plugins/*/version.json`, the highest `git tag -l "<name lowercased>/v*" --sort=-v:refname` tag, `{schema_version: 1, plugins: {Name: {version, download_url: data.url, release_notes: ""}}}` | 🔶 Removed. `tools/gen-feed.js --legacy` reproduces it byte for byte (Phase 1 SC-001), and the v2 mode replaces it. |
| `.github/workflows/docs.yml:234-240` | Deploy to gh-pages: `peaceiris/actions-gh-pages@v4`, `keep_files: true`, `cname: krateaudio.com` | ✅ Unchanged. The `cname` line `feed.yml` copies (roadmap line 431). `docs.yml` has `concurrency: group: docs-deploy, cancel-in-progress: true` (`:14-16`). |
| `.github/workflows/feed-tools.yml:3-30` | Path filters (push, pull_request) and `workflow_dispatch` | 🔶 Adds `tools/feed-publish.js`, `.github/workflows/feed.yml`. |
| `.github/workflows/feed-tools.yml:67-122` | `node --test` with the TAP check: `PLAN = "1..46"` (`:74`), the `NAMES` list (`:75-98`), `# fail 0` | 🔶 `PLAN` and `NAMES` gain this phase's top-level tests (FR-052). |
| `tools/gen-feed.js:10-24` | Usage: v2 mode `--plugins-dir --releases --manifests --repo [--manager-latest-url] --out`; `--legacy`; `--project-v1`; exit 0 clean, 1 abort (nothing written), 2 usage, 3 feed written with ≥ 1 degraded plugin | ✅ Run unchanged by `feed.yml` (FR-006). |
| `tools/gen-feed.js:155-168` | `readManifestsDir`: every `*.json` file in `--manifests`, bytewise by name | ✅ `collect` writes one file per manifest there (FR-022). |
| `tools/gen-feed.js:418-447`, `:762-773` | `selectReleases(name, releases)`, the Q9 rule (drafts dropped, stable = highest non-prerelease, sibling = highest prerelease above it, `null` when no stable); exported | ✅ `collect` imports it, so the release whose `release.json` is downloaded is the one gen-feed selects (FR-021). The rule stays in gen-feed (roadmap line 304). |
| `tools/gen-feed.js:574-588` | A plugin without a manifest gets no `platforms` / `install` and no warning | ✅ Is "keep their v1 entry and get no `platforms`" (roadmap lines 442-444, FR-008). |
| `tools/gen-feed.js:311`, `:619-631`, `:694` | `--manager-latest-url` must pass `checkUrl` and end `/manager/latest.json`; emits `manager: {latest_json_url}` | ✅ `feed.yml` passes `https://krateaudio.com/manager/latest.json` only when it copied a `latest.json` (FR-026). |
| `tools/gen-feed.js:287-304` | `projectV1(doc)`: `{schema_version: 1, plugins: {<k>: {version, download_url, release_notes}}}`, plugin order kept | ✅ The comparison's projection of both sides (FR-040). |
| `tools/gen-feed.js:174-190` | `writeAtomic` (temp file + rename) | ✅ Pattern copied by `feed-publish.js` (FR-020). |
| `tools/backfill-sign.js:149-157`, `:160-172`, `:233-237`, `:735-783` | `ghJson`, `checkListRows`, `gh release list --limit 1000 --json tagName,isPrerelease,isDraft,publishedAt` with a refusal at 1000 rows, `productionDeps` spawning `gh` with fixed argv and no shell | ✅ The pattern `feed-publish.js` copies (FR-020, FR-023). Not imported: its codes are backfill codes. |
| `tools/feed-contract/contract.js:478` (`validateFeed`), `CODES` (classes `usage`, `abort`, `abort-or-degrade`, `degrade`, `none`) | C1/v1 validation; the code table | ✅ `compare` validates the generated feed with it (FR-041); new codes join `CODES` (FR-053). |
| `tools/feed-contract/CONTRACT.md:27` | "Phase 3 (`feed.yml`) — runs gen-feed, deploys the feed; exit 3 deploys and turns the run red" | ✅ Binding (Phase 1 FR-049); FR-007. |
| `tools/feed-contract/CONTRACT.md` `## 7.`, `## 9. Phase 2 tools` (`:349`, `:517`) | Code table read by the hygiene test; Phase 2 tool section | 🔶 Gains `## 10. Phase 3 tools` and §7 rows for the new codes (FR-053). |
| `tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/` | Frozen real inputs: 9 `version.json` + `setup.iss`, `releases.json` (88 releases, 0 prereleases, 0 drafts), `tags.txt` (88), `live-versions.json` (the live v1 file, 9 plugins), README "never edit a file here" | ✅ Read in place by SC-001 and the mixed fixture (FR-045). Never edited. `git ls-remote --tags origin` this session: still 88 tags, none `manager/*`. |
| `tools/feed-contract/fixtures/feeds/valid/`, `cases.json` | C1 fixtures (e.g. `mixed.json`: four invented plugins, three with `platforms`) read by Phase 5 | 🔶 Gains `one-v2-eight-v1.json` and its `cases.json` row (FR-045). |
| `tools/feed-contract/tests/helpers/fake-gh.js:30-44` | Fake `gh` over `fixtures/phase2/backfill/state.json` (nine invented plugins with drafts and prereleases); list and view JSON shapes | 🔶 Gains the `release download -p release.json -O <file>` and manager-release shapes this phase calls (FR-050). |
| `tools/feed-contract/tests/helpers/write-guard.js:225-245` | Child-process allowlist (`git`, `tar`, `rsign` shapes); writes only under `os.tmpdir()` | ✅ Unchanged: no Phase 3 test spawns anything (FR-051). |
| `tools/feed-contract/tests/hygiene.test.js:34-49`, `:57`, `:69-75` | Scanned files list, `FETCH_RE`, `CHILD_EXEMPT` | 🔶 `tools/feed-publish.js` joins the scan and `CHILD_EXEMPT` (FR-051). |
| `tools/feed-contract/tests/workflow-shape.test.js:226-275` | Phase 2 shape test of `release.yml`, including "deploy-pages is skipped on dry runs" (`:271-273`) and "dry_run boolean input … no other new input" (`:234-240`) | ✅ Must stay green (SC-016); a Phase 3 shape test is added beside it (FR-050). |
| `plugins/shared/src/update/update_checker.cpp:209-248` | `parseResponse`: `plugins` object (`:215`), `version` string (`:224`), `download_url` / `release_notes` only when strings (`:238-242`), `catch (...)` → empty result (`:245-248`); never reads `schema_version` | ✅ The compatibility contract; not touched. |
| `plugins/shared/tests/test_update_checker.cpp:121-123`, `:394` | `MockUpdateChecker(config, mockResponse)`; Phase 1 case "UpdateChecker - feed schema v2 is read as v1" (`[update]`) with inline JSON | 🔶 One added case reading the mixed fixture file (FR-046). |
| `plugins/shared/tests/CMakeLists.txt:7-31` | `add_executable(shared_tests …)` incl. `test_update_checker.cpp`; no `target_compile_definitions` today | 🔶 Adds `SHARED_FIXTURES_DIR` (FR-046), as `plugins/membrum/tests/CMakeLists.txt:223-225` does for `MEMBRUM_RESOURCES_DIR`. |
| `plugins/iterum/src/update/iterum_update_config.h:12` | Endpoint `https://rolandzwaga.github.io/krate-audio/versions.json` (all nine plugins) | ✅ It 301-redirects to `krateaudio.com` (Phase 1 spec line 86), which is why `cname` must survive every deploy. |

---

## External facts used (verified this session)

| Fact | Source |
|---|---|
| `peaceiris/actions-gh-pages` `v4`: `runs.using: node24`; `keep_files` "If existing files in the publish branch should be not removed before deploying" (default `false`); `cname` "Set custom domain"; `allow_empty_commit` "If empty commits should be made to the publication branch" (default `false`); `publish_dir` default `public`; `exclude_assets` default `.github` | https://raw.githubusercontent.com/peaceiris/actions-gh-pages/v4/action.yml (fetched 2026-10-30) |
| A job calling a reusable workflow may use only `name`, `uses`, `with`, `secrets`, `strategy`, `needs`, `if`, `concurrency`, `permissions`, `cache-mode` (so no `steps`); `GITHUB_TOKEN` permissions "can be only downgraded (not elevated) by the called workflow"; the `github` context is the caller's; caller and callee should not share a `concurrency.group` when the caller cancels in progress | https://docs.github.com/en/actions/reference/workflows-and-actions/reusable-workflows |
| A same-repo `./` reusable workflow is "from the same commit as the caller workflow"; inputs types are `boolean`, `number`, `string`; up to ten levels of workflows | https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows |
| `workflow_call`: `GITHUB_SHA` and `GITHUB_REF` "Same as the caller workflow"; `workflow_dispatch` "will only trigger a workflow run if the workflow file exists on the default branch" | https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows |
| `concurrency`: "at most one running job or workflow in a concurrency group at any time"; a queued run cancels the existing pending one (`queue: single` default) | https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax |
| `jobs.<id>.needs`: "If a job fails or is skipped, all jobs that need it are skipped" unless a conditional expression lets it continue; `needs.<job_id>.result` is `success`, `failure`, `cancelled` or `skipped` | workflow-syntax (above); https://docs.github.com/en/actions/reference/workflows-and-actions/contexts |
| `workflow_dispatch` / `inputs` context "preserves Boolean values as Booleans"; `permissions:` takes literal levels and cannot depend on `inputs` | workflow-syntax (above); Phase 2 spec "External facts" (contexts availability table) |
| `GITHUB_TOKEN` rate limit "1,000 requests per hour per repository"; unauthenticated 60 per hour | https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api |
| `gh release list`: `-L/--limit` default 30, `-O/--order` default `desc`, `--json` fields `createdAt, isDraft, isImmutable, isLatest, isPrerelease, name, publishedAt, tagName` (no assets) | https://cli.github.com/manual/gh_release_list |
| `gh release download`: `-p/--pattern` glob, `-O/--output <file>` "The file to write a single asset to", `-D/--dir`, `--clobber`, `--skip-existing`; the page does not state what happens when no asset matches, so presence is decided from `gh release view --json assets` first | https://cli.github.com/manual/gh_release_download |
| `gh release view --json assets`: each asset has `name`, `size`, `url`, `digest` (nullable) | Phase 2 spec "External facts" (https://cli.github.com/manual/gh_release_view; `pkg/cmd/release/shared/fetch.go`) |
| Tauri updater static JSON: required `version`, `platforms.[target].url`, `platforms.[target].signature`; `notes`, `pub_date` optional; "Tauri will validate the whole file before checking the version field"; the documented GitHub endpoint is `…/releases/latest/download/latest.json`, i.e. tauri-action uploads the file as the release asset `latest.json` | https://v2.tauri.app/plugin/updater/ |
| `tauri-apps/tauri-action` (`v1` in its README usage; `runs.using: node24`): `uploadUpdaterJson` "Whether to upload a static JSON file for the updater using GitHub Releases as the CDN" (default `true`); with `tagName` set the JSON points at the tag instead of `releases/latest` | https://raw.githubusercontent.com/tauri-apps/tauri-action/dev/action.yml, `README.md` (dev branch, fetched 2026-10-30) |
| Pages answers `Cache-Control: max-age=600` on `https://krateaudio.com/versions.json` | Phase 1 spec line 86 (observed 2026-10-29) |

---

## Decisions taken in this spec (open to challenge in the clarify scan)

The roadmap leaves no decision to this spec explicitly. D1-D13 close points the roadmap names but does not pin down;
each is binding unless the grill overturns it. D5's real-run gate is a roadmap deviation, ruled in the Clarifications
(Q1): a real run gates on blocking drift only.

- **D1, one new tool, `tools/feed-publish.js`, with two subcommands.** The roadmap gives `feed.yml` four jobs to do
  ("lists releases with `gh release list` … downloads each plugin's newest `release.json`, runs `tools/gen-feed.js`,
  copies `manager/latest.json`", lines 428-431, and "runs the v1 projection comparison", lines 436-437) but names no
  tool for the glue. Shell in a workflow cannot be tested offline, so the glue is a Node tool in the `tools/*.js` form
  with injected effects (the `backfill-sign.js` pattern), and every branch the workflow takes is a decision of that
  tool, not of YAML or shell: `collect` (every `gh` call), `generate` (runs gen-feed's exported `main` in-process,
  `tools/gen-feed.js:760-773`, twice for determinism, and stages the manager file), `outcome` (maps a subcommand's
  exit code to "continue / fail" and to the job outputs), `compare` (pure) and `verdict` (the final job's decision).
  The shell left in `feed.yml` is a fixed capture pattern (D13) that SC-010 asserts statically. The subcommand is not
  called `fetch`, because the hygiene scan's `FETCH_RE` flags any `fetch(` (Phase 2 renamed backfill's for the same
  reason, Phase 2 spec Review notes).
- **D2, only the selected releases' manifests are downloaded.** Per plugin, `collect` picks the stable release and the
  prerelease sibling with gen-feed's own `selectReleases`, views each (`gh release view --json assets`) and downloads
  `release.json` only where the release lists it. With nine plugins and no siblings that is 1 list + 9 views + at most
  9 downloads = 19 requests, the roadmap's "~20 of the 1,000 hourly `GITHUB_TOKEN` requests" (line 429). Downloading
  every release's manifest (88 releases today) would not change the output, because gen-feed ignores a manifest whose
  tag it did not select (Phase 1 spec edge cases, line 899).
- **D3, the manager release.** "A manager release exists" means a non-draft, non-prerelease release tagged
  `manager/v<core>` (`<core>` per C1's bounded core, the Phase 4 tag form `manager/vX.Y.Z`, roadmap line 492) whose
  assets include `latest.json`; the highest by SemVer wins. Its `latest.json` is copied byte for byte to
  `_site/manager/latest.json` after a structural check (FR-025), and gen-feed then gets
  `--manager-latest-url https://krateaudio.com/manager/latest.json` (roadmap line 823). A failed check degrades:
  nothing is copied, no `manager` block is emitted, the plugin feed still deploys, and the run turns red (D4). The
  file already on Pages, if any, stays (`keep_files`). No manager release exists today (`git ls-remote --tags origin`,
  this session).
- **D4, a degraded feed deploys, then the run fails.** gen-feed exit 3 (a plugin degraded to v1-only, Phase 1 FR-049)
  and a D3 manager degrade both mean the written feed is safe but incomplete. `feed.yml` deploys it (real run) or
  uploads it (dry run), then the final `verdict` job fails naming each degrade. A real run's warn-severity v1 drift
  (D5, FR-040) takes the same path: deployed, then red. This is the behaviour Phase 1 assigned
  to Phase 3 (`CONTRACT.md:27`; Phase 1 spec line 509 and edge case line 906). gen-feed exit 1 or 2 fails the build
  before the `feed-site` artifact is uploaded or anything deployed (the evidence is still uploaded, D12).
- **D5, the v1 comparison; the rule is the roadmap's, gating real runs on blocking drift is a ruled deviation (Q1).** The
  roadmap's rule (lines 436-437) is "every v1 key equal, except where a release newer than the live file explains the
  difference", stated for the dry run. FR-040 encodes it for **every** v1 key: per plugin, the v1 projection of the
  new feed equals the live one; or `version` is raised to the version of a non-draft, non-prerelease release
  `<name lowercased>/v<version>` in the run's release list that is SemVer-newer than the live version, in which case
  `version` **and** `download_url` may both differ (gen-feed takes `download_url` from the checked-out commit's
  `version.json` `url`, `tools/gen-feed.js:554`, so a `url` edit lands with that plugin's next release); or the
  plugin is new to the feed (its first stable release); or the difference is the one-time Q9 correction of a
  prerelease version that today's inline generator put into the live file (`q9-corrected`, FR-040). Anything else is
  `E_V1_DRIFT`. **Deviation (ruled, Q1):** this spec also runs the comparison in a real run, because a real run is
  exactly when nine editors would see a generator fault, but it gates the deploy only on **blocking** drift: every
  drift kind carries a severity (FR-040). The blocking kinds are the ones that make an editor show a wrong or missing
  update, `version lowered`, `removed` and `release_notes not empty`; they fail the `build` job before the `feed-site`
  artifact exists, so nothing deploys. The other kinds (`warn`) deploy, and the `verdict` job then turns the run red
  naming them (FR-029c), like a D4 degrade. In particular a `version.json` `url` edit on `main` for plugin X
  (`download_url changed without a newer release`) no longer blocks plugin Y's release from being advertised: the
  feed deploys and the run is red until X releases. A **dry run** keeps failing on any drift of either severity
  (FR-027, FR-029d).
- **D6, the live file is read from `https://krateaudio.com/versions.json`**, the address the manager uses (roadmap
  line 55), with `curl --fail`. An unreachable or non-JSON live file fails the run before deploy (`E_INPUT` from
  `compare`). The Pages edge cache (`max-age=600`) can only make the live file older, which the D5 rule already
  explains by the newer release.
- **D7, three jobs, one writer, and only from `main`.** `build` (`permissions: contents: read`) checks out, collects,
  generates, checks, compares and uploads; `deploy` (`permissions: contents: write`, `if: ${{ !inputs.dry_run &&
  github.ref == 'refs/heads/main' }}`, Q2) only downloads the `build` artifact and runs
  `peaceiris/actions-gh-pages@v4`: no checkout, no Node, no generator code next to the write token; `verdict`
  (`if: always()`, `permissions: contents: read`) checks out, downloads `feed-evidence` and runs
  `feed-publish.js verdict`, which fails on any D4 degrade, a warn-severity drift, a real run from a ref other than
  `main`, or a failed `build` / `deploy`. Permissions cannot depend on `inputs` (External facts), so the split, not a
  condition, keeps a dry run read-only. A `workflow_dispatch` can pick any branch, and that branch's own `feed.yml`
  and tools would then build the feed: so a real run from another ref builds and uploads but never deploys, and its
  verdict is red (`verdict: red real run from <ref>`, FR-029c). Called from `release.yml` the ref is the caller's, so a
  release dispatched from a branch builds its feed and does not deploy it. `feed.yml` uses no secret and no
  environment (no `pages` environment, Q2): `gh` reads public releases with `github.token`.
- **D8, the `release.yml` call.** A job that calls a reusable workflow cannot have steps (External facts), so the call
  is a new job `feed` (not a step of `deploy-pages`): `needs: [release, deploy-pages]`,
  `if: ${{ !cancelled() && needs.release.result == 'success' }}`, `uses: ./.github/workflows/feed.yml`,
  `with: dry_run: ${{ inputs.dry_run }}`, `permissions: contents: write` (the ceiling `deploy` needs; `build`
  downgrades). `deploy-pages` keeps `if: ${{ !inputs.dry_run }}`, so on a dry run it is skipped and `feed` still runs
  with `dry_run: true` (roadmap line 441). `needs: deploy-pages` only orders the two gh-pages pushes; a failed landing
  page does not stop the feed.
- **D9, serialisation (ruled, Q3).** `feed.yml`'s jobs `build` and `deploy` each declare `concurrency: { group:
  krate-feed, cancel-in-progress: false, queue: max }`; there is no top-level `concurrency` and none on `verdict`
  (plan SI-3, PD-1). The group is a literal (a called workflow's `github.workflow` is the caller's,
  External facts). With `queue: max` (GitHub documents up to 100 pending runs, not combinable with
  `cancel-in-progress: true`) every feed run executes, in order, and no caller run ends `cancelled`. The caller job in
  `release.yml` declares no concurrency. The reusable-workflows page documents the shared-group hazard but does not
  state that a called workflow's top-level `concurrency` (and its `queue` key) is honoured; it documents only
  `jobs.<job_id>.concurrency.group`, so the plan moved the block onto `build` and `deploy`, and SC-010 asserts that
  placement (FR-012; plan SI-3, PD-1). There is no deploy retry: `docs.yml` pushes gh-pages under its own group, and a push race between the two
  stays a red run that a re-dispatch recovers (both deploy with `keep_files`).
- **D10, the checkout is the run's own commit, shallow.** `actions/checkout@v6` with no `ref:` and no `fetch-depth`
  (roadmap line 433: "needs no `fetch-depth: 0`"; gen-feed reads releases, not tags). Called from `release.yml` that is
  the release's commit (`GITHUB_SHA` "Same as the caller workflow"); dispatched, it is the chosen ref. `version.json`
  (`url`), `CHANGELOG.md` (`notes_markdown`) and `setup.iss` (the AppId cross-check) come from that commit.
- **D11, the mixed fixture is real-shaped.** The roadmap's "one v2 plugin and eight v1-only plugins" (line 449) is
  built from the frozen snapshot (nine real `version.json` + `setup.iss`, 88 real releases) plus one committed C2
  manifest for Membrum's current release `membrum/v0.12.2` (chosen because it carries `preset_subdir: "Kits"`), with
  structural signatures (`CONTRACT.md` §8). Its v1 projection is then exactly the live file of 2026-10-29, which gives
  the C++ case a reference that needs no plugin name in code.
- **D12, artifacts.** A run whose `build` checks all pass uploads `feed-site` (exactly the tree `deploy` publishes:
  `versions.json` and, when copied, `manager/latest.json`). **Every** run, red or green, uploads `feed-evidence` (the
  release list, the manifests, `live-versions.json`, the comparison report, gen-feed's stderr, the collect report,
  whatever of them exists) with `if: always()`, so the evidence of exactly the failing runs is kept. Called from
  `release.yml` the names share that run's artifact namespace; none of its existing names (`windows-installer`,
  `macos-installer`, `linux-archive`, `macos-facts`, `linux-facts`, `release-dry-run`, …) is reused. The token this
  environment has cannot download artifacts or read logs (memory `reference_gh_token_scope_no_logs.md`, 2026-10-09:
  `gh run download` and job logs 403; the jobs API, check-run annotations and the artifact listing work), so every
  verdict the compliance step needs is also emitted as a check-run annotation (FR-029e).
- **D13, shell discipline.** A `run:` step with no `shell:` runs as `bash -e {0}`, while explicit `shell: bash` runs
  `bash --noprofile --norc -eo pipefail {0}` (https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax,
  `defaults.run.shell` table: Unspecified "Note that this runs a different command to when `bash` is specified
  explicitly"). Without pipefail, `node … compare … | tee …` exits with `tee`'s 0 on a drift, and under `-e` a bare
  `node … collect` that exits 3 ends the step before any output is written. So every `run:` step in `feed.yml` declares
  `shell: bash` and its script begins `set -euo pipefail` (as `.github/workflows/feed-tools.yml:68-71` and
  `release.yml`'s `set -euo pipefail` steps do); no step pipes a tool's output (the tools write their own report,
  summary and output files); and a step whose tool may legitimately exit non-zero captures the code with
  `rc=0; node … || rc=$?` and hands it to `feed-publish.js outcome`, which decides.

---

## Functional Requirements

### A. `tools/feed-publish.js` (D1, D2, D3)

- **FR-020** The tool follows the `tools/*.js` form (roadmap line 1000; as `tools/backfill-sign.js:1-35`):
  `#!/usr/bin/env node`, a header comment with a usage block, an exported `main(argv, io, deps)` returning the exit
  code, every `gh` call and the gen-feed run through an injected `deps` object (`gh`, `genFeedMain`), files written only
  at paths given as flags (plan SI-2, PD-3), `productionDeps` the only code that spawns
  (`gh` only, fixed argument arrays, `shell: false`), atomic writes (temp file + rename). `deps` is **required**:
  `main` throws a `TypeError` naming `deps` before parsing `argv` when it is absent or not an object (unlike
  `backfill-sign.js`'s default parameter `main(argv, io, deps = cliDeps(argv))`, `tools/backfill-sign.js:837`, which
  builds real effects for a test that forgets the third argument). The entry point alone builds them:
  `if (require.main === module) process.exitCode = main(process.argv.slice(2), process, productionDeps())`. It names no plugin (ROSTER, roadmap line 33): every name
  comes from a `version.json` under `--plugins-dir`. It uses no network module and no `fetch(`.
- **FR-021** `collect --plugins-dir <dir> --repo <owner/name> --out-dir <dir>` reads the plugin names with gen-feed's
  `readPluginsDir`, runs `gh release list --repo <repo> --limit 1000 --json tagName,isPrerelease,isDraft,publishedAt`,
  and writes the rows verbatim (parsed, re-serialised with 2-space indent and a final LF) to
  `<out-dir>/releases.json`, the file gen-feed's `--releases` reads. For each plugin it calls gen-feed's
  `selectReleases(name, rows)`; an `E_TAG_FORMAT` from it aborts before any release is viewed. Before any `gh` call it
  checks the names: each `version.json` `name` must pass contract.js `isPluginName` (`E_PLUGIN_NAME`), no two may be
  equal after `toLowerCase()` (`E_DUPLICATE_PLUGIN`), and none may lower-case to `manager` (`E_PLUGIN_NAME`) (plan PD-7).
  The list rows are shape-checked before `selectReleases` sees them: an array, each row an object with a non-empty
  string `tagName` and boolean `isPrerelease` and `isDraft` (`E_INPUT`; `publishedAt` is left to gen-feed); a view must
  return `{tagName: <the tag>, assets: [...]}` whose assets each have a string `name`, a non-negative safe-integer
  `size` and a `digest` that is absent, `null` or `sha256:<64 hex>` (case-insensitive), else `E_INPUT` (plan PD-8).
- **FR-022** For each selected release (stable, then sibling), `collect` runs `gh release view <tag> --repo <repo>
  --json tagName,assets`. When the assets include `release.json`, it runs `gh release download <tag> --repo <repo>
  --pattern release.json --output <out-dir>/manifests/<tag with "/" as "-">.json`, then checks the written file's size
  against the asset's `size` and its SHA-256 against `sha256:<hex>`. A `release.json` asset whose `digest` is `null` is
  refused with `E_FEED_DOWNLOAD` ("has no digest on GitHub") before any download, the Phase 2 CC-verify rule
  (Clarifications 2026-10-30 (plan) P-2; plan SI-5, PD-17, whose acceptance on size alone that ruling supersedes). When the
  assets do not include `release.json`, nothing is downloaded and the plugin is reported `no-manifest`.
  `<out-dir>/manifests/` always exists after a successful `collect`, empty or not.
- **FR-023** `collect` aborts (exit 1, the out-dir's `releases.json` and `manifests/` not written or removed) on: a
  `gh` call that still exits other than 0 after its last attempt (FR-023a; `E_INPUT`, naming the argv's subcommand and
  tag and the attempt count), output that is not the expected JSON
  shape (`E_INPUT`, the FR-021 list and view checks), 1000 list rows (`E_FEED_LIST_TRUNCATED`: releases may be missing), a view whose `tagName` differs
  from the tag asked for (`E_INPUT`), a downloaded manifest whose size or digest differs (`E_FEED_DOWNLOAD`), and a
  FR-021 name check (`E_PLUGIN_NAME`, `E_DUPLICATE_PLUGIN`, before any `gh` call). It
  never passes `--clobber`, never calls `gh api`, and never names a host. There is no staging directory (plan PD-6):
  `collect` refuses (`E_INPUT`, before any `gh` call, nothing in the out-dir touched) an `--out-dir` that already holds
  any of `releases.json`, `manifests`, `manager` or `collect-report.json`; it downloads straight to the FR-022 / FR-024
  `--output` paths; it writes `releases.json` and `collect-report.json` atomically only after the last check; and on
  an abort it removes each of those four names that this run created (after the refusal, any of them that exists is
  this run's), so no `releases.json`, `manifests/`, `manager/`, `collect-report.json` or temp file is left (plan PD-6,
  PD-7, PD-8).
- **FR-023a** (Q5) Transient `gh` failures are retried, in the tool, before FR-023 aborts. `productionDeps` wraps its
  one `gh` spawn, for `release list`, `release view` and `release download` alike, in an exported
  `withGhRetry(gh, {sleep, log})`: at most **3 attempts**, a fixed backoff of **5 s** before the second and **15 s**
  before the third (`sleep(5000)`, `sleep(15000)`), and each failed attempt logged to stderr as `feed-publish: WARN
  gh attempt <n>/3 failed: <label> (exit <code>); retrying in <s> s`, where `<label>` is `list` for `release list` and
  `<subcommand> <tag>` for `view` / `download` (e.g. `view alphaplug/v1.2.0`) (plan SI-6, PD-5). Only a `gh` process that
  fails to run or exits non-zero is retried. A shape mismatch (`E_INPUT`), a view for another tag, a size or digest
  mismatch (`E_FEED_DOWNLOAD`), `E_FEED_LIST_TRUNCATED` and `E_TAG_FORMAT` are decided on a call that succeeded and are
  **never** retried. Before a download is retried, any partial output file the failed attempt left is deleted (`gh`
  without `--clobber` refuses an existing file). `gh_calls` (FR-026) counts every attempt, and the exact argv lists of
  SC-001 and SC-003 list every attempt in order. Tests apply the same exported `withGhRetry` to the fake `gh` with a
  recording `sleep` (never `productionDeps`, FR-051).
- **FR-024** Manager release (D3): from the same rows, `collect` takes the non-draft, non-prerelease rows whose
  `tagName` matches `^manager/v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$`, highest by
  `compareSemVer`; views it; when its assets include `latest.json`, downloads it with `--pattern latest.json --output
  <out-dir>/manager/latest.json` and the FR-022 size / digest check. No such row, or no `latest.json` asset: no
  `manager/` directory is written, and the report says `manager: none`.
- **FR-025** The `latest.json` check (D3): the file is JSON; an object with a string `version` equal to the tag's
  version (a leading `v` stripped, for tools that write one); `platforms` a non-empty object; each platform entry an
  object with a string `signature` and a string `url` that passes contract.js `checkUrl` (https, no whitespace, no
  `/releases/latest/`). The first failure is `E_MANAGER_LATEST` (degrade): the file is deleted, `manager/` is not left
  behind, the report says `manager: degraded <message>`, and `collect` exits **3** after writing everything else. The
  check reads no other key (Tauri owns the format; https://v2.tauri.app/plugin/updater/).
- **FR-026** `collect` writes `<out-dir>/collect-report.json`: per plugin (bytewise by name) `{name, stable, sibling,
  manifests: [{tag, status: "downloaded" | "no-manifest"}]}` or `{name, status: "no-stable"}`; then
  `manager: {status: "none" | "copied" | "degraded", tag?, message?}`; and the request count `gh_calls` (every attempt counts, FR-023a). It prints one
  line per plugin and one `manager:` line. The report's `manager.status` is the only channel by which `generate`
  (FR-029a) learns whether to pass `--manager-latest-url`; no stdout line is parsed by the workflow.
- **FR-027** `compare --live <file> --feed <file> --releases <file> --dry-run <true|false> [--report <file>]
  [--summary <file>] [--annotate]` is pure and offline apart from its own output files. It validates `--feed` with contract.js `validateFeed` against
  `v2` and then `v1` (`E_FEED_SCHEMA` naming the first `instancePath` on failure), projects both files with gen-feed's
  `projectV1`, and applies the D5 rule (FR-040). It prints one line per plugin, bytewise by name: `same <Name>
  <version>`, `newer <Name> <live> -> <new> (<tag> <publishedAt>)` (with ` download_url <old> -> <new>` appended when
  the URL changed too), `added <Name> <new> (<tag> <publishedAt>)`, `q9-corrected <Name> <live> -> <new|absent>
  (prerelease <live tag>)`, or one `drift <Name> <kind>[; <kind>…] <severity>` line per plugin (its kinds in the fixed
  order of plan §4.2's kind table, joined by `; `; `<severity>` `block` if any of its kinds is `block`, else `warn`,
  FR-040; `[...]` and `<severity>` are placeholders, not literal brackets) (plan SI-4, PD-10); then
  `compare: <n> same, <n> newer, <n> added, <n> q9-corrected, <n> drift (<n> block, <n> warn)`, which counts plugins
  (plan SI-4, PD-10). `--report <file>`
  writes exactly those result lines, the per-plugin lines and the summary line (atomic, LF), whatever the exit code, so
  the workflow needs no `tee`; with `--annotate` stdout is the result lines followed by the annotation lines, without
  it stdout equals the report; on `E_INPUT` / `E_FEED_SCHEMA` there are no result lines and the report is written
  empty (plan PD-13). `--summary
  <file>` appends the same lines in a fenced block (the step passes `$GITHUB_STEP_SUMMARY`). **Data never forms a
  workflow command or a markdown escape (plan SI-10, PD-18):** (a) `compare` refuses (`E_INPUT`, exit 1, before any
  result line exists) a live file in which any `plugins` key, or any string `version`, `download_url` or
  `release_notes` value, holds a code unit U+0000-U+001F or U+007F, with the message `live plugins.<JSON.stringify(key)>
  holds a control character`; (b) in every line the tool builds, a data-derived value is printed raw only when it passes
  its own validator (`contract.isPluginName` for a name, `parseSemVer` non-null for a version, `contract.checkUrl` not
  throwing for a URL), any other value as `JSON.stringify(value)`; (c) every line the tool writes, on stdout or stderr,
  goes through one writer that prints `JSON.stringify(line)` instead of `line` when the line holds a U+0000-U+001F /
  U+007F code unit or starts with `::` (annotation lines excepted, FR-029e); (d) `--summary` wraps the lines in a fence
  of `max(3, longest backtick run in the lines + 1)` backticks, so no value can close it. `--dry-run` is required
  (a missing or non-boolean value is usage, 2) and is the only mode switch (Q1). Exit 0 with no `drift` line; **1** with
  any `block` drift, and with **any** drift (either severity) when `--dry-run true` (`E_V1_DRIFT`, one stderr line per
  drift); **3** when `--dry-run false` and every drift is `warn` (complete: the feed may deploy, the verdict turns
  red, FR-029c); 1 for `E_INPUT` (a file unreadable or not JSON, the live file has no `plugins` object, or a live
  key or value holds a control character, (a) above) or `E_FEED_SCHEMA`; 2 for usage.
- **FR-028** Exit codes: `collect` 0 complete, 3 complete with a manager degrade, 1 abort, 2 usage; `generate` 0, 3
  (gen-feed exit 3), 1 (gen-feed exit 1, a determinism mismatch or a staging failure), 2 usage (its own, or gen-feed
  exit 2); `outcome` 0 continue, 1 fail; `compare` 0, 1, 2, 3 (FR-027); `verdict` 0 green, 1 red, 2 usage. Errors go to stderr as
  `feed-publish: <CODE>: <message>`, degrades as `feed-publish: WARN <CODE>: <message>`.
- **FR-029a** `generate --in-dir <dir> --plugins-dir <dir> --repo <owner/name> --site-dir <dir> [--annotate]` reads
  `<in-dir>/collect-report.json` and runs gen-feed's exported `main(argv, io)` (`tools/gen-feed.js:760-773`, reached
  through `deps.genFeedMain`, which `productionDeps` and the tests both set to that export; a test may wrap it)
  in-process with `--plugins-dir`, `--releases <in-dir>/releases.json`, `--manifests <in-dir>/manifests`, `--repo`,
  `--manager-latest-url https://krateaudio.com/manager/latest.json` exactly when the report's `manager.status` is
  `"copied"`, and `--out <site-dir>/versions.json`. It **creates `<site-dir>/` first** (`mkdir` recursive): gen-feed
  does no `mkdir` (`writeAtomic`, `tools/gen-feed.js:174-190`, writes its temp file in `path.dirname(outPath)` with
  flag `wx`, so a missing `_site/` is `E_INPUT … cannot write (ENOENT)`), and the inline step this phase removes
  created it itself (`fs.mkdirSync("_site", {recursive: true})`, `release.yml:896`). It then refuses (`E_INPUT`,
  exit 1, `<site-dir>/manager` left untouched, gen-feed never run) a `<site-dir>/manager` that already exists, as
  `collect` refuses a pre-filled out-dir, so nothing under `<site-dir>/manager` was made by anyone but this run (plan
  PD-9, plan §4.1.4 step 2). gen-feed's stderr is written to
  `<in-dir>/gen-feed.log` and echoed. On gen-feed exit 0 or 3 it runs gen-feed a second time on the same inputs into
  `<in-dir>/versions.again.json` and requires the two files byte-identical, the same exit code and the same stderr
  text (`E_FEED_NONDETERMINISTIC` otherwise, exit 1; roadmap line 447; plan PD-9). When the report says `"copied"` it creates `<site-dir>/manager/` and copies
  `<in-dir>/manager/latest.json` to `<site-dir>/manager/latest.json` byte for byte. Exit: gen-feed's 0 / 3 / 1 / 2,
  or 1 for the checks above. On any exit other than 0 / 3, `<site-dir>/manager/` is not left behind.
- **FR-029b** `outcome --tool <collect|generate|compare> --exit <n> --github-output <file>` is the only place an exit
  code is turned into a workflow decision. `--exit 0` appends `<flag>=false`, `--exit 3` appends `<flag>=true` (flag
  `manager_degraded` for `collect`, `feed_degraded` for `generate`, `drift_warned` for `compare`) to `--github-output` in the `name=value` form
  (https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-commands, "echo "{name}={value}" >>
  "$GITHUB_OUTPUT""), and returns 0; any other `--exit` value appends nothing, prints `feed-publish: E_STEP_FAILED:
  <tool> exited <n>` and returns 1. A non-integer `--exit` is usage (2).
- **FR-029c** `verdict --dry-run <true|false> --ref <ref> --build <result> --deploy <result> --feed-degraded
  <true|false|""> --manager-degraded <true|false|""> --drift-warned <true|false|""> [--evidence <dir>] [--annotate]`
  (`<result>` one of `success`, `failure`, `cancelled`, `skipped`, the values of `needs.<job_id>.result`, External
  facts; `<ref>` the run's `github.ref`, required, a missing one is usage 2) returns 1, printing one
  `verdict: red <reason>` line per reason, when: `--build` is not `success`; `--dry-run false` and `--ref` is not
  `refs/heads/main` (reason `verdict: red real run from <ref>`, Q2, which replaces the generic deploy reason for the
  `skipped` deploy that ref causes); otherwise `--deploy` is not `success` on a real run, or neither `success` nor
  `skipped` on a dry run; any of the three flags (`--feed-degraded`, `--manager-degraded`, `--drift-warned`) is `true`
  (reason `verdict: red v1 drift deployed: <drift lines>` for the last, the `drift ... warn` lines read from
  `compare.txt` under `--evidence`, Q1); or `--build` is `success` but
  `--evidence` lacks one of `releases.json`, `collect-report.json`, `gen-feed.log`, `live-versions.json`,
  `compare.txt` (one `verdict: red evidence missing <file>` reason per missing file). The deploy reason applies only
  when `--build` is `success` (a failed build's `skipped` deploy adds no second reason), and the reasons print in the
  order build, ref, deploy, the three flags (feed, manager, drift), evidence (plan PD-12). With `--evidence` it prints each `WARN` line of `gen-feed.log` and the `manager.message` of
  `collect-report.json` under the matching reason; every quoted evidence line and the `manager.message` go through the
  FR-027 (c) output rule before they are printed or joined (plan SI-10, PD-18). Otherwise it prints `verdict: green` and returns 0. An empty flag of
  the three (the job output was never written because `build` failed early) counts as not degraded; the `build` result
  already makes that run red.
- **FR-029d** The real-run gate (D5, Q1) is `compare --dry-run false`'s exit code read by FR-010: exit 1 (a `block`
  drift) fails the `build` job before `feed-site` exists; exit 3 (`warn` drift only) continues through `outcome` with
  `drift_warned=true`, the feed deploys, and `verdict` turns red (FR-029c). A dry run (`--dry-run true`) fails on any
  drift, so a dry run's `build` is red on a `warn` drift too and nothing relies on `outcome` there.
- **FR-029e** `--annotate` (on `collect`, `generate`, `compare`, `verdict`) prints GitHub workflow commands, which
  "create an annotation" (https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-commands,
  `::notice … title={title}::{message}` / `::error …`), so every verdict is readable through the check-run
  annotations API: `collect` `::notice title=feed-collect::<n> downloaded, <n> no-manifest, <n> no-stable, manager
  <status>`; `generate` `::notice title=feed-generate::exit <n>, deterministic` and one `::warning
  title=feed-degrade::<WARN line>` per gen-feed `WARN`; `compare` `::notice title=feed-validate::v2 ok, v1 ok` (or
  `::error title=feed-validate::<instancePath> …`), `::notice title=feed-compare::<summary line>`, and one `::error
  title=feed-drift::<drift line>` per drift that fails the run (every drift on a dry run, `block` drift on a real run)
  and one `::warning title=feed-drift::<drift line>` per `warn` drift of a real run; `verdict`
  `::notice title=feed-verdict::green` or one `::error title=feed-verdict::<reason>` per reason. Every message is one line: the tool replaces any CR or LF in a message
  with a space before printing (the workflow-commands page documents no escaping, so the tool never relies on one).
  No line other than an annotation with one of the fixed titles above starts with `::`, on stdout or stderr (FR-027
  (c); plan SI-10, PD-18).

### B. `.github/workflows/feed.yml` (roadmap lines 428-439)

- **FR-001** Triggers: `workflow_call` with one input `dry_run` (`type: boolean`, `required: true`, no default, so
  every caller states it) and `workflow_dispatch` with one input `dry_run` (`type: boolean`, `default: true`, roadmap
  line 434). No `push`, `pull_request` or `schedule` trigger. The `workflow_call` interface is what Phase 11's
  `release-manager.yml` calls (roadmap line 803).
- **FR-002** Top-level `permissions: contents: read`, and no top-level `concurrency` (the D9 block sits on jobs `build`
  and `deploy`, FR-012; plan SI-3, PD-1). No `secrets.` reference and no
  `environment:` anywhere in the file (no signing key is involved).
- **FR-003** Job `build` on `ubuntu-latest`, `permissions: contents: read`, `env: GH_TOKEN: ${{ github.token }}`,
  `outputs: manager_degraded: ${{ steps.collect.outputs.manager_degraded }}`, `feed_degraded:
  ${{ steps.generate.outputs.feed_degraded }}`, `drift_warned: ${{ steps.compare.outputs.drift_warned }}`:
  `actions/checkout@v6` with no `ref:` and no `fetch-depth` (D10);
  `actions/setup-node@v4` Node `24`; `npm ci --prefix tools/feed-contract`. **Every `run:` step of every job in
  `feed.yml` declares `shell: bash` and its script's first line is `set -euo pipefail` (D13); no `run:` script
  contains a `|` pipe.**
- **FR-004** `build` step "Collect releases" (`id: collect`): `rc=0; node tools/feed-publish.js collect --plugins-dir
  plugins --repo "$GITHUB_REPOSITORY" --out-dir "$RUNNER_TEMP/feed-in" --annotate || rc=$?` (the runner's default
  variable, not `"${{ github.repository }}"`: no template interpolation into a script; plan PD-2) followed by
  `node tools/feed-publish.js outcome --tool collect --exit "$rc" --github-output "$GITHUB_OUTPUT"`. `outcome`
  (FR-029b) decides: exit 0 or 3 continues with `manager_degraded` written, any other exit fails the step.
- **FR-005** `build` step "Fetch live feed": `curl --fail --silent --show-error --location --retry 3
  https://krateaudio.com/versions.json --output "$RUNNER_TEMP/feed-in/live-versions.json"` (D6). The only network
  call outside `gh`.
- **FR-006** `build` step "Generate feed" (`id: generate`): `rc=0; node tools/feed-publish.js generate --in-dir
  "$RUNNER_TEMP/feed-in" --plugins-dir plugins --repo "$GITHUB_REPOSITORY" --site-dir _site --annotate ||
  rc=$?` (`"$GITHUB_REPOSITORY"` as in FR-004, plan PD-2) followed by `node tools/feed-publish.js outcome --tool generate --exit "$rc" --github-output
  "$GITHUB_OUTPUT"`. `generate` (FR-029a) runs `tools/gen-feed.js` (unchanged) with
  `--manager-latest-url https://krateaudio.com/manager/latest.json` exactly when `collect` copied a `latest.json`,
  creates `_site/` itself and captures gen-feed's stderr to `$RUNNER_TEMP/feed-in/gen-feed.log`. Exit 0 continues;
  exit 3 continues with `feed_degraded=true`; any other exit fails the step (no `feed-site` artifact is uploaded,
  nothing deploys; `feed-evidence` is still uploaded, FR-011).
- **FR-007** Degrade handling (D4): a run with `feed_degraded`, `manager_degraded` or (real run only) `drift_warned` still uploads `feed-site` and,
  on a real run, deploys it; the `verdict` job then fails, printing each `WARN` line from `gen-feed.log` and the
  manager message.
- **FR-008** Plugins without a `release.json` on their selected release get exactly gen-feed's v1-only entry (no
  `platforms`, no `install`; `tag`, `pub_date`, `channel`, `release_notes_url` and `notes_markdown` as gen-feed emits
  them), no `WARN` line, and no effect on the exit code (roadmap lines 442-444).
- **FR-009** Determinism (roadmap line 447) is `generate`'s second gen-feed run into
  `$RUNNER_TEMP/feed-in/versions.again.json` and its byte comparison with `_site/versions.json` (FR-029a), which also
  requires the same exit code and the same stderr text from both runs (plan PD-9); a mismatch
  fails the "Generate feed" step. There is no separate shell `cmp` step.
- **FR-010** `build` step "Compare with the live feed" (`id: compare`, `env: DRY_RUN: ${{ inputs.dry_run }}`): `rc=0;
  node tools/feed-publish.js compare --live "$RUNNER_TEMP/feed-in/live-versions.json" --feed _site/versions.json
  --releases "$RUNNER_TEMP/feed-in/releases.json" --dry-run "$DRY_RUN" --report "$RUNNER_TEMP/feed-in/compare.txt"
  --summary "$GITHUB_STEP_SUMMARY" --annotate || rc=$?` followed by `node tools/feed-publish.js outcome --tool compare
  --exit "$rc" --github-output "$GITHUB_OUTPUT"` (the FR-004 capture pattern; no pipe). Per FR-029d/FR-029b: exit 0
  continues; exit 3 (real run, `warn` drift only) continues with `drift_warned=true`; any other exit (a `block` drift,
  any drift on a dry run, `E_INPUT`, `E_FEED_SCHEMA`) fails the step and so the job, before any artifact named
  `feed-site` exists, so `deploy` has nothing to publish (Q1). `compare` takes no `--repo`, so the
  `"$GITHUB_REPOSITORY"` spelling of FR-004 / FR-006 (plan PD-2) has no use here; the step's one workflow value,
  `inputs.dry_run`, reaches the script only through `env: DRY_RUN`.
- **FR-011** `build` steps, in this order after FR-010: "Upload site" (`actions/upload-artifact@v7`, name `feed-site`
  from `_site/`, `if-no-files-found: error`, default `if:` so only when every earlier step succeeded) and "Upload
  evidence" (`actions/upload-artifact@v7`, name `feed-evidence` from `$RUNNER_TEMP/feed-in/`, `if: always()`,
  `if-no-files-found: warn`, D12; FR-029c makes a green `build` with missing evidence red). `_site/` holds only
  `versions.json` and, when `generate` copied it, `manager/latest.json` (FR-029a): no landing page, no CNAME file
  (peaceiris writes it, FR-013). No step creates `_site/` or `_site/manager/` outside `generate`.
- **FR-012** Concurrency (D9, Q3): jobs `build` and `deploy` each carry `concurrency: group: krate-feed`,
  `cancel-in-progress: false`, `queue: max`, so every feed run executes in order and none ends `cancelled`; `feed.yml`
  has no top-level `concurrency`, and `verdict` has none (it holds no write) (plan SI-3, PD-1). GitHub's
  reusable-workflows page documents only `jobs.<job_id>.concurrency.group` in a called workflow, not a top-level block,
  so the block sits at the documented job level. With it on both jobs, builds run one at a time and each `deploy` is
  enqueued when its own `build` ends, so deploys run in build order (plan §4.3.2). The group is a literal that no other
  workflow in `.github/workflows/` uses. SC-010 asserts that placement. No step retries the deploy (Q3).
- **FR-013** Job `deploy`: `needs: build`, `if: ${{ !inputs.dry_run && github.ref == 'refs/heads/main' }}` (Q2),
  `runs-on: ubuntu-latest`,
  `permissions: contents: write`; steps exactly `actions/download-artifact@v8` (`name: feed-site`, `path: _site`) and
  `peaceiris/actions-gh-pages@v4` with `github_token: ${{ github.token }}`, `publish_dir: ./_site`,
  `keep_files: true`, `cname: krateaudio.com` (roadmap lines 430-432; `docs.yml:234-240`). No checkout, no `run:`
  step. `allow_empty_commit` stays at its default `false`, so a run whose output equals the published files pushes no
  commit (idempotent, roadmap line 432).
- **FR-014** Job `verdict`: `needs: [build, deploy]`, `if: always()`, `runs-on: ubuntu-latest`,
  `permissions: contents: read`; steps `actions/checkout@v6` (no `ref:`, no `fetch-depth`), `actions/setup-node@v4`
  Node `24`, `npm ci --prefix tools/feed-contract`, `actions/download-artifact@v8` (`name: feed-evidence`, `path:
  ${{ runner.temp }}/evidence`, `continue-on-error: true`, since a run that failed at checkout has none), and one
  `run:` step `node tools/feed-publish.js verdict --dry-run "$DRY_RUN" --ref "$REF" --build "$BUILD_RESULT" --deploy
  "$DEPLOY_RESULT" --feed-degraded "$FEED_DEGRADED" --manager-degraded "$MANAGER_DEGRADED" --drift-warned
  "$DRIFT_WARNED" --evidence "$RUNNER_TEMP/evidence" --annotate`, the seven values passed through the step's `env:` from
  `${{ inputs.dry_run }}`, `${{ github.ref }}` (the caller's ref when called, Q2), `${{ needs.build.result }}`,
  `${{ needs.deploy.result }}`, `${{ needs.build.outputs.feed_degraded }}`,
  `${{ needs.build.outputs.manager_degraded }}` and `${{ needs.build.outputs.drift_warned }}` (never interpolated into
  the script). The decision is FR-029c's; the
  YAML holds no condition beyond `if: always()`. A dry run whose build is green and clean ends green with `deploy`
  skipped; a real run from a ref other than `refs/heads/main` ends red (`verdict: red real run from <ref>`) with
  `deploy` skipped.
- **FR-015** `feed.yml` names no plugin (ROSTER): no plugin name or plugin directory name from `release.yml`'s
  `options:` list (`:34-43`) appears in it.

### C. `release.yml` (roadmap lines 440-441; D8)

- **FR-030** A new job `feed` after `deploy-pages` in file order: `needs: [release, deploy-pages]`,
  `if: ${{ !cancelled() && needs.release.result == 'success' }}`, `permissions: contents: write`,
  `uses: ./.github/workflows/feed.yml`, `with: dry_run: ${{ inputs.dry_run }}`, no `secrets:` and no `concurrency:`.
  Because `feed.yml`'s `deploy` is gated on `github.ref == 'refs/heads/main'` (FR-013), a `release.yml` real run
  dispatched from another ref builds its feed, does not deploy it, and its `feed / verdict` is red (Q2).
- **FR-031** On a real run the release (with its `release.json`) is created by the `release` job before `feed` starts,
  so the new release is in the list `collect` reads and its manifest is downloaded. On a dry run no release is created;
  the feed dry run reflects the published releases only.
- **FR-032** In `deploy-pages` the step "Generate Update Checker Endpoint" (`release.yml:859-899`) is removed. Every
  other line of `deploy-pages` (its `needs`, `if: ${{ !inputs.dry_run }}`, permissions, the landing-page, signal-flow
  and root-index steps, and its `peaceiris/actions-gh-pages@v4` step with `keep_files: true`) is unchanged, so its
  deploys keep the `versions.json` already on gh-pages and `feed` replaces it.
- **FR-033** No other job, input or step of `release.yml` changes; the Phase 2 shape test stays green unchanged
  (SC-016).

### D. The v1 comparison rule (D5)

- **FR-040** For plugin keys `L` (live projection) and `N` (new projection), with release rows `R`, where "a stable
  row for `v`" means a non-draft, non-prerelease row with `tagName === <name lowercased>/v<v>` and "a prerelease row
  for `v`" the same with `isPrerelease === true`, the cases are tried in this order:
  - name in both, `N.version === L.version`, `download_url` and `release_notes` equal → `same`;
  - name in both, `compareSemVer(N.version, L.version) > 0`, `R` has a stable row for `N.version`, and
    `release_notes` equal → `newer` (the roadmap's "a release newer than the live file explains the difference",
    lines 436-437, applied to every v1 key: `download_url` may differ too, and the line then names both URLs);
  - name in both, `R` has a prerelease row for `L.version` and no stable row for it, `N.version` is the version of
    gen-feed's `selectReleases(name, R).stable`, and `download_url` and `release_notes` equal → `q9-corrected
    <Name> <live> -> <new>`; name in `L` only, `R` has a prerelease row for `L.version`, and `selectReleases(name, R)`
    returns no stable release (gen-feed then omits the plugin, Phase 1 spec edge case "A plugin with only prereleases |
    Omitted from `plugins`") → `q9-corrected <Name> <live> -> absent`. Both are exit 0 and printed. They are the
    one-time transition Q9 mandates (roadmap line 32): today's inline generator takes the highest git tag,
    prereleases included (roadmap line 199; `release.yml:878`, `git tag -l "<p>/v*" --sort=-v:refname | head -1`),
    so a plugin prerelease published before `feed.yml` replaces it puts a prerelease version into the live v1 file,
    and the first Q9-correct feed lowers or drops it. The snapshot has 0 prereleases
    (`fixtures/gen-feed/snapshot-2026-10-29/releases.json`), so the case is latent today;
  - name in `N` only, and `R` has a stable row for `N.version` → `added`;
  - anything else → `drift`, naming the case, each with a **severity** (Q1):

    | Drift case | Severity | Real run (`--dry-run false`) |
    |---|---|---|
    | `version lowered` | `block` | `build` fails, nothing deploys |
    | `removed` | `block` | `build` fails, nothing deploys |
    | `release_notes not empty` | `block` | `build` fails, nothing deploys |
    | `download_url changed without a newer release` | `warn` | deploys; `verdict` red |
    | `version changed without a release` | `warn` | deploys; `verdict` red |
    | `added without a release` | `warn` | deploys; `verdict` red |

    The three `block` kinds are the ones the user ruled make an editor show a wrong or missing update (a version
    lowered, a plugin removed, release_notes not empty); the ruling names `download_url changed without a newer
    release` as the `warn` example, and the other two kinds, which it does not name, are classed `warn` with it as the
    "added-type" kinds of the clarify scan's option (c). On a **dry run** every kind fails the run (FR-027). A plugin
    with several drift kinds is blocking if any of its kinds is. The severity is part of the printed line (FR-027),
    and a plugin with several kinds prints one line naming them all (plan SI-4, PD-10).

    A live `version` that `parseSemVer` rejects is never passed to `compareSemVer`: it can match no `newer`,
    `q9-corrected` or `version lowered` case, so it lands in `version changed without a release` (`warn`) (plan PD-11).

    **Two `block` kinds cannot self-heal (Clarifications P-3, ruled (a); plan SI-7).** C1 fixes the new feed's
    `release_notes` to `""` and FR-041 validates it first, so `release_notes not empty` can only describe the **live**
    file; `removed` describes a plugin no longer in the roster. Both stay `block`, so if either appears no `feed.yml`
    run can repair the live file. The recovery is a hand commit on `gh-pages` that fixes `versions.json` (removes the
    bad key), then a dry run (`same`) and a real run, documented in `CONTRACT.md` §10.5. The same recovery applies to a
    live file that FR-027 (a) refuses. Today no writer besides the inline step this phase removes produces
    `versions.json`, so the case is latent.
  `release_notes` must be `""` on both sides (roadmap line 137). A live file with `schema_version` 1 or 2 is projected
  the same way (`projectV1` ignores `schema_version`, `tools/gen-feed.js:287-304`).
- **FR-041** Before the rule, the new feed must validate against C1 v2 and v1 (`validateFeed`, roadmap line 435:
  "validates it against the Phase 1 C1 schema"). gen-feed already self-checks (Phase 1 FR-045); this is the
  independent check on the file that will be published.

### E. Fixtures and the C++ guard (roadmap lines 449-451; D11)

- **FR-044** A committed C2 manifest `tools/feed-contract/fixtures/phase3/membrum-v0.12.2.json` for the snapshot's
  Membrum release: `tag: "membrum/v0.12.2"`, `version: "0.12.2"`, `install` equal to what gen-feed's cross-checks
  derive from the snapshot's `membrum/version.json` and `setup.iss` (`preset_subdir: "Kits"`,
  `macos_pkg_id: "com.krateaudio.membrum"`, `windows_uninstall_key` from the snapshot `setup.iss` AppId), per-tag
  asset URLs under `rolandzwaga/krate-audio`, structural signatures with valid C3 trusted comments, and a C4 Linux
  payload for a `Membrum.vst3` layout. It passes `checkReleaseManifest` with repo `rolandzwaga/krate-audio`. Generated
  by `tools/feed-contract/fixtures/phase3/make-phase3.js`, registered as set `phase3` of `fixtures/make-fixtures.js`
  (which owns `feeds/` and reports any file it did not build as stray), reproducible byte for byte. The generator
  names no plugin: it picks the one snapshot plugin whose `version.json` carries `preset_subdir` (exactly one does,
  asserted) and derives every name, the tag and the file name from it (plan SI-1, PD-14, PD-15).
- **FR-045** `tools/feed-contract/fixtures/feeds/valid/one-v2-eight-v1.json` is byte-identical to `gen-feed` v2 output
  on (snapshot `plugins/`, snapshot `releases.json`, a manifests dir holding only FR-044's file, repo
  `rolandzwaga/krate-audio`, no manager URL), exit 0. It has nine plugins; exactly `Membrum` has `platforms` and
  `install`; the other eight have neither. Its `--project-v1` output is byte-identical to the snapshot's
  `live-versions.json`. `fixtures/feeds/cases.json` gains its row (`expect: "accept"`, `schema: "v2"`, rule
  `"Phase3/one-v2-eight-v1"`), which Phase 5 reads in place (roadmap line 451, 536). Both the file and the
  `cases.json` row are built by `make-phase3.js` through `make-fixtures.js` (gen-feed's `buildFeed` run in-process on
  the snapshot and FR-044's manifest; `feeds()` appends the row), never by hand (plan SI-1, PD-14). The snapshot directory is not edited.
- **FR-046** `plugins/shared/tests/CMakeLists.txt` adds `target_compile_definitions(shared_tests PRIVATE
  SHARED_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tools/feed-contract/fixtures")`. `test_update_checker.cpp` adds one
  `TEST_CASE("UpdateChecker - one v2 plugin among eight v1 plugins is read as v1", "[update]")` that reads
  `feeds/valid/one-v2-eight-v1.json` and `gen-feed/snapshot-2026-10-29/live-versions.json` from that directory
  (`REQUIRE` both opened and non-empty), takes the plugin names from the live file's `plugins` keys with
  `nlohmann::json` (no plugin name in the source), `REQUIRE(names.size() == 9)`, and for each name with
  `currentVersion = "0.0.1"` asserts that `MockUpdateChecker` on the mixed document gives the same `updateAvailable`
  (true), `latestVersion`, `downloadUrl` and empty `releaseNotes` as on the live document, with an `INFO` naming the
  plugin scoped to that name's assertions. **The names are real installed plugins on this dev box**, and
  `UpdateChecker::saveState` writes `update_state.json` to `getAppSettingsDirectory(config_.pluginName)`
  (`plugins/shared/src/update/update_checker.cpp:284-311`), which is the user's real per-plugin settings folder
  (`plugins/shared/src/platform/preset_paths.cpp:53-79`). So, as the Phase 1 case does
  (`test_update_checker.cpp:398`, `:416`, `:437`): the whole loop runs inside **one** `ScopedSettingsDirRedirect`
  scope; for each name `REQUIRE(isUnder(getAppSettingsDirectory(name), settings.root()))` comes **before** the first
  `MockUpdateChecker` for that name is constructed; after each check, `REQUIRE(isUnder(statePath, settings.root()))`
  holds for that name's `getAppSettingsDirectory(name) / "update_state.json"`; and after the scope the case checks
  that the redirect's environment variable is restored (`CHECK(readEnv(kSettingsEnvVar) == envBefore)`, as `:455`).
  No header and no non-test source changes.

### F. Tests, lane and contract plumbing

- **FR-050** Tests live in `tools/feed-contract/tests/` (first `require` is the write guard), run by the existing
  quoted glob: `feed-publish-collect.test.js`, `feed-publish-compare.test.js`, `feed-publish-steps.test.js`
  (`generate`, `outcome`, `verdict`, `--annotate`), `feed-pipeline.test.js` (collect → generate → compare
  in-process over the fake `gh` and the fixtures), `phase3-fixtures.test.js`, and a Phase 3 block in
  `workflow-shape.test.js` (static reads of `feed.yml` and `release.yml`). `fake-gh.js` changes, all additive so the
  Phase 2 tests keep their shapes:
  - the view field set `tagName,assets` joins `VIEW_JSON` (today only `tagName,publishedAt,isImmutable,assets` and
    `assets`, `tools/feed-contract/tests/helpers/fake-gh.js:39`);
  - the download shape `release download <tag> --repo <repo> --pattern <name> --output <file>` joins the `--dir`
    shape (today the only one, `fake-gh.js:232-242`), writing only the named file under `os.tmpdir()`;
  - a manager release (tag `manager/vX.Y.Z`, a `latest.json` asset whose bytes a test sets with `ctx.setServed`);
  - a snapshot switch that serves the snapshot's 88 releases, each with the three installer asset names
    `release.yml` uploads for that plugin and version (`<Name>-<ver>-Windows-x64.exe`, `-macOS.pkg`,
    `-Linux-x64.tar.gz`, `.github/workflows/release.yml:697-699`; `<Name>` from the snapshot `version.json` whose
    lowercased name is the tag prefix), so no release has an empty assets array;
  - a helper that adds the full Phase 2 eight-file set (the three installers, `SHA256SUMS`, three `*.minisig`,
    `release.json`, `release.yml:697-704`) to a chosen release, with a `release.json` built by
    `gen-release-manifest.js`'s exported `buildManifest` (offline, "no network, no child process",
    `tools/gen-release-manifest.js:1-10`, `:163-177`) from the fake world's served asset bytes (`assetBytes`,
    `fake-gh.js:133-143`; the `.tar.gz` is a real C4 tarball), the plugin's `version.json` / `setup.iss` from
    `fixtures/phase2/backfill/plugins/`, facts recorded in-process as `tests/gen-release-manifest.test.js:66-84` does
    (a darwin facts object; `release-facts.linuxFacts` with `listing.listingFromTarGz` as the lister, no `tar`
    spawn), and a `throwawaySigner()` key (`fake-gh.js:146-157`), plus the matching `SHA256SUMS` and `.minisig`
    bytes, all set with `ctx.setServed`. `release.json` documents are
    therefore always the generator's own output, never hand-forged C2.
- **FR-051** `tools/feed-publish.js` joins `hygiene.test.js`'s scanned files and `CHILD_EXEMPT` (its `productionDeps`
  requires `node:child_process`, as `backfill-sign.js` does) and its roster check; tests never construct
  `productionDeps` and spawn nothing (the FR-050 manifest helper calls `buildManifest`, never the backfill `apply`
  flow, whose POSIX tar leg spawns through `helpers/external-tools.js`, `fake-gh.js:7-8`), so the write-guard
  allowlist does not change. A test that called `main` without `deps` throws (FR-020); a test that somehow built real
  deps would still be stopped by the write guard's spawn check (`write-guard.js:248-259`, `command not allowed` for
  `gh`).
- **FR-052** `feed-tools.yml`: both path filters gain `tools/feed-publish.js` and `.github/workflows/feed.yml`; the TAP
  check's `PLAN` becomes `1..57` (46 today plus 11) and `NAMES` gains each new top-level test name: the eleven of plan
  §7.1 (T1-T11, for SC-001 to SC-008, SC-010, SC-017 and SC-018); SC-009 (C++), SC-011 (hygiene names unchanged) and
  SC-012 (a git check) add none (plan SI-9).
- **FR-053** `CONTRACT.md` gains `## 10. Phase 3 tools` (command lines, exit codes, the collect report, the
  `outcome` / `verdict` decision tables, the annotation titles of FR-029e, the comparison rule of FR-040 with its severity table, the gh retry of FR-023a, the `main`-only deploy of FR-013, and
  the post-backfill user step of FR-061) and a §7
  table row for each new code, with its class: `E_FEED_LIST_TRUNCATED`, `E_FEED_DOWNLOAD`, `E_FEED_SCHEMA`,
  `E_FEED_NONDETERMINISTIC`, `E_STEP_FAILED`, `E_V1_DRIFT` (class `none`, like the Phase 2 tool codes) and
  `E_MANAGER_LATEST` (`degrade`). `contract.js` `CODES` gains the same seven keys (none exists today; `E_V1_TYPE`,
  `contract.js:32`, is a different code).
- **FR-054** The C++ change follows the repo gate: build `shared_tests` with zero warnings, run
  `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests`
  (`tools/run-gate.js:52-53` refuses a filter without the four timing exclusions, `GATE RED usage`, and `~[.]` keeps
  hidden probes out, the Phase 1 close lane's filter, `specs/krate-manager-phase1/evidence/close-lane.js:35`) (tasks
  TA-2), run `node tools/header-consumers.js` to confirm no shared
  header changed, and clang-tidy the changed TU only, `clang-tidy -p build/windows-ninja
  plugins/shared/tests/test_update_checker.cpp`, after reconfiguring `build/windows-ninja` (the test CMake changed),
  with zero findings; a full `-Target shared` sweep reaches nothing more for this change (CLAUDE.md "Run only the
  tests the change set can reach"; the Phase 1 lane ran the same TU in 12 s, `specs/krate-manager-phase1/evidence/close-lane.log`
  row B4) (tasks TA-3).
  Pluginval does not apply (test-only change).

### G. Scope fences

- **FR-060** The phase changes only: `.github/workflows/feed.yml` (new), `.github/workflows/release.yml` (FR-030,
  FR-032), `.github/workflows/feed-tools.yml` (FR-052), `tools/feed-publish.js` (new), `tools/feed-contract/**`
  (contract.js codes, CONTRACT.md, fixtures, tests, helpers), `plugins/shared/tests/CMakeLists.txt` and
  `plugins/shared/tests/test_update_checker.cpp`, `specs/krate-manager-phase3/**` (spec, plan, tasks, evidence,
  compliance), the roadmap's Phase 3 status line, and `specs/INDEX.md` (regenerated) (plan SI-8, tasks TA-1), and
  nothing in `.github/workflows/ci.yml` (FR-062).
  `tools/gen-feed.js`, `docs.yml`, `backfill-sign.yml`, every installer, every `version.json` and the snapshot fixture
  directory are byte-identical before and after, and `ci.yml` is byte-identical (FR-062).
- **FR-061** (Q4) Nothing publishes the feed after the live backfill automatically: `backfill-sign.yml` is unchanged,
  `feed.yml` gains no `release`, `push` or `schedule` trigger (FR-001), and `release.yml` gains no call beyond FR-030.
  The step is a documented user step: after the live backfill (`apply-live`) has attached `release.json` to the nine
  current releases, the user dispatches `feed.yml` with `dry_run: true`, reviews its result (SC-014), then dispatches it
  with `dry_run: false`. It is written in `CONTRACT.md` §10 (FR-053) and in the "User step" paragraph below.
- **FR-062** (Q6, re-ruled (a)) `.github/workflows/ci.yml` is not changed. A true `shared` output in `ci.yml` builds
  and tests all nine plugins on every OS (45 plugin jobs carry `needs.detect-changes.outputs.shared == 'true'`,
  `ci.yml:490-554`, `:785-1049`, `:1284-1348`), so routing the feed fixtures through `shared` would rebuild every
  plugin to re-run one parse case whose code a fixture edit does not change. `feed-tools.yml` (SC-008) is the feed
  fixtures' per-push gate on all three OSes; the FR-046 case re-runs on macOS and Linux whenever `ci.yml` runs with
  `shared` set (`plugins/shared/**`). SC-012 checks that `ci.yml` is byte-identical, and SC-010's test asserts the
  `shared:` filter is still exactly `- 'plugins/shared/**'`.

---

## Success Criteria

All Node SCs run locally on Windows with `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` (quoted
glob, parallel files). A run counts only with `# fail 0` and the FR-052 `PLAN` line. macOS and Linux rows are
**PENDING-CI** on `feed-tools.yml` jobs `node-test (macos-latest)` and `node-test (ubuntu-latest)`, which the user's push
of the branch triggers; the workflow rows are PENDING-CI on dispatches only the user can make after the merge to main
(`workflow_dispatch` runs only from the default branch, External facts).

- **SC-001 (snapshot pipeline = today's live feed).** With the fake `gh`'s snapshot switch (the snapshot's 88 releases,
  each carrying its three installer assets and no `release.json`, FR-050) and `--plugins-dir` = the snapshot's
  `plugins/`: `collect` exits 0, writes `releases.json` equal (parsed) to the snapshot's, an empty `manifests/`,
  reports nine `no-manifest` plugins and `manager: none`, and records exactly 10 `gh` calls whose argv list equals the
  expected list (1 list, then 9 `view <tag> --repo … --json tagName,assets` in bytewise plugin-name order, 0
  downloads); `generate` on its output into a `--site-dir` that does not exist yet exits 0 and creates it; `projectV1`
  of `versions.json` is byte-identical to `live-versions.json`; `compare` against `live-versions.json` prints nine
  `same` lines and exits 0. Test `feed pipeline reproduces the live v1 feed from the 2026-10-29 snapshot`. Threshold: 0
  differing bytes, the exact 10-call argv list. (Roadmap line 448.)
- **SC-002 (determinism).** Two complete pipeline runs (collect + generate) on the same fake state, in two temp dirs,
  give byte-identical `releases.json`, the same manifest file names with identical bytes, and byte-identical
  `versions.json`; `generate`'s own FR-009 check passes in each, and a test that makes gen-feed's second run differ
  (a `deps.genFeedMain` wrapper that rewrites `releases.json` between the two calls) gets `E_FEED_NONDETERMINISTIC`,
  exit 1. Test `feed pipeline is deterministic`. Threshold: 0 differing bytes. (Roadmap line 447.)
- **SC-003 (selection and exact request sequence).** Over `fixtures/phase2/backfill/state.json` (nine invented
  plugins, drafts, prereleases above and below their stable release) with the FR-050 eight-file set (three
  installers, `SHA256SUMS`, three `*.minisig`, `release.json`) added to a chosen subset of selected and non-selected
  releases, and one manager release with `latest.json`: the manifests downloaded are exactly those of the
  `selectReleases` stable and sibling tags that list `release.json` (asserted per plugin); no draft's or older
  release's manifest is fetched; every download argv names `--pattern release.json` (or `latest.json` for the manager)
  exactly, never `SHA256SUMS`, a `.minisig` or any other asset; no argv contains `--clobber`, `api`, or a host name;
  and the recorded `calls` array equals the expected argv list in order: 1 list (`--limit 1000`, the four fields), then
  per plugin bytewise by name one view per selected release followed by one download per listed `release.json`, then
  1 manager view and 1 manager download. `gh_calls` in the report equals that list's length (a retried call appears once per attempt, FR-023a; this
  case has no failing call, so each call appears once), and a state with no manager row records no manager call; a
  `release.json` asset record with `digest: null` is refused with `E_FEED_DOWNLOAD`, exit 1, before any download
  (FR-022, Clarifications P-2). Test `collect downloads only the selected manifests`. Threshold: exact equality
  of the argv list, every plugin.
- **SC-004 (aborts write nothing).** Each FR-023 abort, driven through the fake: a list exit 1, a non-array list, 1000
  rows (`E_FEED_LIST_TRUNCATED`), a view for another tag, a truncated download (size), a digest mismatch
  (`E_FEED_DOWNLOAD`), an `E_TAG_FORMAT` tag, and the FR-021 name checks (an invalid name, two names equal after
  lower-casing, a name lower-casing to `manager`; plan PD-7): `main` returns 1, stderr's first line is `feed-publish: <CODE>: …`, and the
  out-dir holds no `releases.json`, no `manifests/`, no `manager/`, no `collect-report.json` and no temp file
  afterwards (its listing equals the listing before the run); a usage error returns 2
  with the usage text. A **pre-filled out-dir** (FR-023, plan PD-6), once per name (`releases.json`, `manifests`,
  `manager`, `collect-report.json` each pre-created in turn with sentinel content): `main` returns 1, stderr's first
  line is `feed-publish: E_INPUT: …`, zero `gh` calls are recorded, and the pre-existing entry is byte-identical and the
  listing unchanged. A `gh` exit 1 on a list, a view and a download is a retried case (FR-023a, Q5): the fake `gh`
  wrapped in `withGhRetry` with a recording `sleep` (a) **fails once, then succeeds**: `collect` returns 0, the
  recorded `calls` hold the failing argv twice then continue as in SC-003, `sleep` was called once with 5000, the
  stderr holds exactly one `feed-publish: WARN gh attempt 1/3 failed: …` line (its label `list`, `view <tag>` or
  `download <tag>`, FR-023a), `gh_calls` counts both attempts, and for
  the download case the partial file of the failed attempt was removed before the retry; (b) **fails twice, then
  succeeds**: `sleep` called with 5000 then 15000, two WARN lines; (c) **fails three times**: `main` returns 1,
  `feed-publish: E_INPUT: …` names the subcommand, tag and 3 attempts, the out-dir is clean as in the abort cases above,
  the argv was attempted exactly 3 times; (d) a view for another tag, a size mismatch, a digest mismatch, a non-array
  list and 1000 rows are each attempted **exactly once** (never retried), `sleep` never called. Test `collect aborts
  cleanly` (the retry cases are subtests of it). Threshold: every case.
- **SC-005 (no manifest = v1-only, no warning).** Over the backfill state with the FR-050 eight-file set (its
  `release.json` built by `buildManifest`, FR-050) on every selected release except one plugin's stable release:
  collect reports that plugin `no-manifest` and the others `downloaded`; `generate` exits 0 and `gen-feed.log` holds no
  `WARN` line; that entry has no `platforms` / `install`, and its v1 keys equal `{version: <its selectReleases stable
  version>, download_url: <its version.json url>, release_notes: ""}` computed from the state and the fixture
  `version.json`; every other plugin has `platforms` and `install`. Test `a release without release.json stays
  v1-only`. (Roadmap lines 442-444.)
- **SC-006 (manager latest.json).** Cases through the fake: no `manager/v*` release → no `manager/`, report
  `manager.status === "none"`, `generate` passes no `--manager-latest-url` and the feed has no `manager`; a
  prerelease-only and a draft-only manager release → same as none; two manager releases → the higher SemVer is copied,
  byte-identical to the served asset, report `"copied"`, and `generate` stages `<site-dir>/manager/latest.json`
  byte-identical to it and emits `manager.latest_json_url === "https://krateaudio.com/manager/latest.json"`; each
  FR-025 failure (not JSON, version ≠ tag, no `platforms`, empty `platforms`, a platform without `signature`, an
  `http://` URL, a `/releases/latest/` URL) → exit 3, `WARN E_MANAGER_LATEST`, no `manager/` left in the in-dir, report
  `"degraded"`, every other output written, and `generate` then stages no `manager/` and passes no URL. Test `collect
  copies only a valid manager latest.json`. Threshold: every case.
- **SC-007 (comparison rule).** `compare` over committed live/new/releases triples, one subtest per FR-040 case
  (`same`, `newer`, `added`, both `q9-corrected` forms, each `drift` kind) plus: `download_url` changed with a newer
  stable release → `newer … download_url <old> -> <new>`, exit 0; `download_url` changed with no newer release →
  `drift … download_url changed without a newer release warn`, exit 3 with `--dry-run false` (and the summary line
  `1 drift (0 block, 1 warn)`), exit 1 with `--dry-run true`; **severity cases (Q1, FR-040), each run with both
  `--dry-run true` and `--dry-run false`**: `version lowered`, `removed` and `release_notes not empty` print `block`
  and exit 1 in both modes; `download_url changed without a newer release`, `version changed without a release` and
  `added without a release` print `warn`, exit 3 with `--dry-run false` and 1 with `--dry-run true`; a plugin with a
  `warn` and a `block` drift, and two plugins one of each, exit 1 with `--dry-run false`, the first printing exactly one
  line `drift <Name> <kind A>; <kind B> block` (kinds in plan §4.2's fixed order) and counted as one drift in the
  summary (plan SI-4, PD-10); a missing or non-boolean
  `--dry-run` exits 2; with `--annotate` a `block` drift (and every drift on a dry run) prints `::error
  title=feed-drift::` and a real-run `warn` drift prints `::warning title=feed-drift::`; a live version that is a prerelease row's version
  and a new feed carrying the Q9 stable value → `q9-corrected`, exit 0, for both a v1 legacy live file and a v2 live
  file; a plugin in the live file with only prerelease rows in `R` and absent from the new feed → `q9-corrected …
  -> absent`, exit 0; a live version lowered with no prerelease row for it → `drift … version lowered block`, exit 1; a v2
  live file (projected); a live file that is not JSON (`E_INPUT`); a live file without `plugins` (`E_INPUT`); a new
  feed failing v2 (`E_FEED_SCHEMA` with its `instancePath`); the edge-cache case (live one release behind → `newer`,
  exit 0); **the FR-027 data-safety cases (plan SI-10, PD-18; plan §7.1 T7):** a live key
  `"X\n::notice title=feed-verdict::green"` (and one with `\r`, and a live `version` holding U+0007) → exit 1, first
  stderr line `feed-publish: E_INPUT: live plugins."X\n::notice title=feed-verdict::green" holds a control character`
  (the key in its `JSON.stringify` form), no result line, an empty report; a live key ``X```y`` (no control character,
  fails `isPluginName`) → its drift line prints the key as ``"X```y"`` and the `--summary` block uses a four-backtick
  fence; and in **every** case, with `--annotate`, no stdout or stderr line starts with `::` except the tool's own
  annotation lines with a fixed FR-029e title. Each asserts the exact stdout lines, the summary line, the exit code, that `--report` holds exactly the
  stdout lines on exit 0 **and** exit 1, that `--summary` was appended to (not overwritten), and with `--annotate` the
  exact FR-029e lines. Test `compare explains every v1 difference or fails`. Threshold: every case.
- **SC-008 (one v2 / eight v1 fixture).** FR-045 holds: the fixture is byte-identical to the gen-feed run named there;
  `validateFeed` accepts it as v2 and v1; exactly one plugin (`Membrum`) has `platforms`; its `--project-v1` output is
  byte-identical to `live-versions.json`; the `cases.json` row exists and the Phase 1 test `C1 feed fixtures` still
  passes with it; FR-044's manifest passes `checkReleaseManifest`, and its generator reproduces it byte for byte; and
  `node tools/feed-contract/fixtures/make-fixtures.js --check` passes (plan SI-1, PD-14). Test
  `Phase 3 mixed feed fixture is real-shaped`. (Roadmap line 449.)
- **SC-009 (C++ guard, Windows).** Three measurements. (1) `node tools/run-gate.js --filter
  '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests` (the FR-054 filter: `tools/run-gate.js:52-53`
  refuses one without the four timing exclusions; tasks TA-2)
  prints `GATE GREEN` (the gate verdict; its shards run without `-s`, `tools/run-gate.js:102`, and a passing shard
  prints only Catch2's totals line, `:87-95`, so it cannot show the case's names). (2) The single-case direct run
  CLAUDE.md permits, `build/windows-x64-release/bin/Release/shared_tests.exe "UpdateChecker - one v2 plugin among
  eight v1 plugins is read as v1" -s` ("To see successful, as well as failing, test results just pass this option",
  https://github.com/catchorg/Catch2/blob/devel/docs/command-line.md), captured to a log: it shows `names.size() == 9`
  expanded to `9 == 9`, each per-name assertion of FR-046 (the `isUnder` guard of `getAppSettingsDirectory(name)`,
  `latestVersion`, `downloadUrl`, `releaseNotes`, the `statePath` guard) exactly nine times, and `test cases: 1 | 1
  passed` with 0 failed assertions. (3) The build shows zero warnings; `node tools/header-consumers.js --tag '[update]'`
  ends `none`; clang-tidy on that one TU, `clang-tidy -p build/windows-ninja
  plugins/shared/tests/test_update_checker.cpp` after reconfiguring `build/windows-ninja`, reports zero findings (FR-054;
  tasks TA-3). Threshold: `GATE GREEN`, the nine-fold
  counts, 0 failures, 0 warnings, 0 findings. (Roadmap lines 449-450.)
- **SC-009m / SC-009l (C++ guard, macOS / Linux).** PENDING-CI on the user's pull request to `main` (`ci.yml`'s path
  filter lists `plugins/**`, `.github/workflows/ci.yml:14-23`, and `detect-changes` sets `shared` for
  `plugins/shared/**`, `ci.yml:85-86`): `shared_tests` is built in jobs `macOS (Clang)` and `Linux (GCC)`
  (`ci.yml:562`, `:1061`; the `shared_tests` target at `:623`, `:1127`) and run by `macOS tests (shard N)` / `Linux
  tests (shard N)` (`ci.yml:1405`, `:1442`); the FR-046 case passes in them, proving the `SHARED_FIXTURES_DIR`
  path and the fixture read on both. **Filter (Q6 re-ruled, FR-062):** `ci.yml` is unchanged, so an edit to only
  `one-v2-eight-v1.json` or `live-versions.json` does not re-run this case on macOS and Linux; `feed-tools.yml`
  (SC-008) is those fixtures' per-push gate on all three OSes, and a fixture edit does not change the parser code.
  A push that changes only `tools/feed-contract/fixtures/{feeds,gen-feed}/**` is knowingly not proven on macOS and
  Linux by this case: `ci.yml`'s workflow-level `paths` (`.github/workflows/ci.yml:3-23`) do not list `tools/**`, so
  such a push starts no `ci.yml` run
  (plan SI-11, R-1).
- **SC-010 (feed.yml and release.yml shape).** A `workflow-shape.test.js` block asserts: `feed.yml` triggers are
  exactly `workflow_call` and `workflow_dispatch`, each with the single input `dry_run` (boolean; call `required: true`
  without default; dispatch `default: true`); top-level `permissions` is `contents: read`; the D9 concurrency block
  (`group: krate-feed`, `cancel-in-progress: false`, `queue: max`) on jobs `build` and `deploy`, with no top-level
  `concurrency` and none on `verdict` (FR-012; plan SI-3, PD-1),
  and no other workflow file uses group `krate-feed`; jobs are exactly `build`, `deploy`, `verdict`; only `deploy`
  holds `contents: write`; `deploy` has `if: ${{ !inputs.dry_run && github.ref == 'refs/heads/main' }}`, no checkout
  and no `run:`, exactly one `peaceiris/actions-gh-pages@v4` step (no retry step, no `if: failure()`) with
  `keep_files: true`, `cname: krateaudio.com`, `publish_dir: ./_site`, and no `allow_empty_commit`; no `environment:`
  key anywhere in the file; `ci.yml`'s `shared` filter is still exactly `- 'plugins/shared/**'` (FR-062); `build`'s checkout has no `fetch-depth` and no `ref`; `build`'s steps are exactly, in order,
  checkout, setup-node, `npm ci`, "Collect releases" (`id: collect`), "Fetch live feed", "Generate feed" (`id:
  generate`), "Compare with the live feed", "Upload site", "Upload evidence"; `build.outputs` maps `manager_degraded`,
  `feed_degraded` and `drift_warned` to `steps.collect` / `steps.generate` / `steps.compare`; **every `run:` step of `build` and `verdict` has `shell:
  bash`, its script's first non-empty line is `set -euo pipefail`, and no script contains `|`**; "Collect releases",
  "Generate feed" and "Compare with the live feed" (`id: compare`) each have exactly the capture pattern `rc=0; node
  tools/feed-publish.js <sub> … || rc=$?` followed by `node tools/feed-publish.js outcome --tool <sub> --exit "$rc"
  --github-output "$GITHUB_OUTPUT"`; the compare command is the FR-010 one with `--dry-run "$DRY_RUN"` (`DRY_RUN` from
  the step's `env:`), `--report`, `--summary "$GITHUB_STEP_SUMMARY"` and `--annotate`; no
  step outside `generate` names `_site/manager` or runs `mkdir`/`cmp`; "Upload site" has no `if:` and
  `if-no-files-found: error`, "Upload evidence" has `if: always()` and `if-no-files-found: warn`; `verdict` has
  `if: always()`, `permissions: contents: read`, the FR-014 steps in order (download with `continue-on-error: true`)
  and its seven values (`--dry-run`, `--ref`, `--build`, `--deploy` and the three flags) only through `env:`; no `secrets.`, `environment:` or plugin name appears; `release.yml` has the
  FR-030 `feed` job exactly, no step named "Generate Update Checker Endpoint", no other `versions.json` writer, and its
  `deploy-pages` steps equal the pre-phase list minus that one step. Test `feed.yml and release.yml keep the Phase 3
  shape`.
- **SC-011 (hygiene).** The hygiene suite, extended per FR-051, passes: `feed-publish.js` loads no network module and
  has no `fetch(`, names none of the nine plugins, follows the `tools/*.js` form, and every new code is a `CODES` key
  with its §7 row (`CONTRACT.md documents every code and field`). The `tools/*.js` form check of `feed-publish.js` is
  a new subtest `(d)` of `tools stay offline` (plan PD-16 says `(e)`, but the test has only `(a)`, `(b)`, `(c)`,
  `hygiene.test.js:111`, `:145`, `:155`; tasks TA-4). Test names unchanged; threshold `# fail 0`.
- **SC-012 (scope).** `git diff --name-only 4f4c4175` (the HEAD this spec was written on, header; the commit the
  phase branches from) plus `git status --porcelain` for untracked files lists only FR-060's paths (which include
  `specs/krate-manager-phase3/**`, the roadmap's Phase 3 status line and the regenerated `specs/INDEX.md`; plan SI-8,
  tasks TA-1); `tools/gen-feed.js`,
  `docs.yml`, `backfill-sign.yml`, every `setup.iss`, every `version.json` and
  `fixtures/gen-feed/snapshot-2026-10-29/**` are unchanged; `git diff 4f4c4175 -- .github/workflows/ci.yml` (Q6,
  FR-062) is empty (checked by the compliance step, recorded with the command output).
- **SC-013w / SC-013m / SC-013l (Node legs).** The full `node --test` set with the FR-052 TAP check: Windows local
  (`# fail 0`, the new `PLAN`); PENDING-CI `feed-tools.yml` `node-test (macos-latest)` and `node-test (ubuntu-latest)`
  green with the same `PLAN` and names, and `Checkout unchanged` empty.
- **SC-014 (feed.yml dry run, PENDING-CI, user-triggered after the merge).** A `workflow_dispatch` of `feed.yml` with
  `dry_run: true` on `main` is green, measured only through what this environment's token can read (D12): the per-job
  conclusions from `gh api repos/rolandzwaga/krate-audio/actions/runs/<id>/jobs` (`build` success, `deploy` skipped,
  `verdict` success); the check-run annotations from `gh api
  repos/rolandzwaga/krate-audio/check-runs/<build job id>/annotations`: `feed-validate` reads `v2 ok, v1 ok`,
  `feed-compare` reads `compare: <n> same, <n> newer, <n> added, <n> q9-corrected, 0 drift (0 block, 0 warn)` with `same + newer + added +
  q9-corrected` equal to the plugin count, no `feed-drift` and no `feed-degrade` annotation, and `feed-collect` names
  `manager none` (no manager release exists yet); `verdict`'s annotations read `feed-verdict` `green`; the artifact
  listing (`gh api repos/rolandzwaga/krate-audio/actions/runs/<id>/artifacts`) shows `feed-site` and `feed-evidence`;
  and `gh api repos/rolandzwaga/krate-audio/commits?sha=gh-pages&per_page=1` shows no commit newer than the run's
  start. Downloading `feed-site` to re-run `validateFeed` locally is a user step, needed only if an annotation is
  missing. (Roadmap lines 452-453.)
- **SC-015 (release.yml dry run reaches feed.yml, PENDING-CI, user-triggered).** A `release.yml` dispatch with
  `dry_run: true`, made **after Phase 3 is on `main`** (a dispatch made before has no `feed` job; `workflow_dispatch`
  runs the default branch's file, External facts), shows through the jobs API `deploy-pages` skipped, `feed / build`
  success, `feed / deploy` skipped, `feed / verdict` success with the `feed-verdict` `green` annotation, and no gh-pages
  commit (as SC-014). That same run also serves as Phase 2's SC-018 evidence (still `pending-ci`,
  `specs/krate-manager-phase2/compliance.md:104`) only if it is made then; Phase 2's SC-018 does not depend on it.
- **SC-016 (Phase 2 shape unchanged).** `release.yml and backfill-sign.yml keep the Phase 2 shape` passes unchanged,
  including "deploy-pages is skipped on dry runs" and "dry_run boolean input, default false, and no other new input".
- **SC-017 (offline tests).** No Phase 3 test opens a socket or spawns a process: the write guard (unchanged) and the
  hygiene scan (FR-051) pass; a test asserts `main(argv, io)` without `deps` (and with `deps: null`) throws the FR-020
  `TypeError` before writing anything; and a static check over the new test files finds 0 `productionDeps(` tokens and
  every `main(` / `.main(` call of `feed-publish.js` with three arguments. Backstop, not the proof: the write guard
  refuses a `gh` spawn (`write-guard.js:248-259`). Test `feed-publish tests never build real effects`.
- **SC-018 (workflow decisions, offline).** `outcome`: `--tool collect --exit 0` → appends exactly
  `manager_degraded=false`, returns 0; `--exit 3` → `manager_degraded=true`, 0; the same for `--tool generate` with
  `feed_degraded` and for `--tool compare` with `drift_warned` (Q1); `--exit 1`, `2`, `127` → appends nothing, prints `E_STEP_FAILED`, returns 1; `--exit x` → 2. The
  appended text is checked against an existing file's content (append, never overwrite). `verdict`: the full table of
  `--dry-run` × `--ref` (`refs/heads/main`, `refs/heads/other`) × `--build` × `--deploy` (four results each) × the
  three flags `--feed-degraded`, `--manager-degraded`, `--drift-warned` (`true`, `false`, `""`) × evidence present /
  missing returns 1 exactly where FR-029c says, and 0 only for (dry run, any ref, build success, deploy skipped or
  success, no flag true, evidence complete) and (real run, `refs/heads/main`, build success, deploy success, no flag
  true, evidence complete); a real run with `--ref refs/heads/other` and `--deploy skipped` prints exactly
  `verdict: red real run from refs/heads/other` (and not also the generic deploy reason) and, with `--annotate`, its
  `::error title=feed-verdict::` line; `--drift-warned true` prints `verdict: red v1 drift deployed:` followed by the
  `warn` drift lines of the fixture `compare.txt`; every red reason has its `verdict: red` line and, with `--annotate`, its `::error title=feed-verdict::`
  line; with `--evidence`, each `WARN` line of a fixture `gen-feed.log` and a degraded `manager.message` is printed.
  `generate`: a missing `--site-dir` is created; on gen-feed exit 1 and 2 (driven through `deps.genFeedMain`)
  `generate` returns that code and leaves no `<site-dir>/manager/`; on exit 3 it returns 3 with `versions.json`
  written. `--annotate` on `collect` and `generate` prints the exact FR-029e lines, single-line even when a message
  holds a newline. Test `feed-publish decides every workflow branch`. Threshold: every row of each table.

**User step, not an exit criterion (roadmap lines 454-455):** after SC-014 is green, the user dispatches `feed.yml` with
`dry_run: false` from `main`, then checks one plugin's update banner against the live feed (the banner's version and
download link unchanged, or the newer release shown). **After the live backfill (Phase 2 `apply-live`) has attached
`release.json` to the nine current releases (Q4, FR-061),** the user dispatches `feed.yml` with `dry_run: true`,
reviews the result, then dispatches it with `dry_run: false`; no trigger does this automatically.

---

## Safety

- **Injected roots and endpoints.** No manager binary exists in this phase. Every tool input is a path given on the
  command line; tests give temp dirs (the write guard refuses any write outside `os.tmpdir()`) and the fake `gh`. The
  live-feed URL and `api`-free `gh` calls exist only in `feed.yml`, on hosted runners.
- **What is checked before anything is published.** Every downloaded `release.json` is size- and SHA-256-checked
  against the release's asset record (one whose asset record has no digest is refused before download, FR-022,
  Clarifications P-2), then fully checked by gen-feed (C2,
  `checkReleaseManifest`, the C3 binding of every signature text, the C4 listing digest and layout, the install
  cross-checks; a failure degrades that plugin, Phase 1 FR-049). The generated feed self-checks (Phase 1 FR-045), is
  validated again against C1 v2 and v1 (FR-041), must be reproducible (FR-009) and must pass the v1 comparison
  (FR-040) before the `feed-site` artifact exists. `latest.json` passes FR-025 or is not copied. Nothing downloaded is
  extracted or executed in this phase.
- **Who can write Pages.** Only `feed.yml`'s `deploy` job, only on a real run from `refs/heads/main`, holding `contents: write` and running no
  repository code (FR-013); `keep_files: true` keeps the landing pages and manuals, `cname: krateaudio.com` keeps the
  custom domain the plugins' 301 and the manager depend on.
- **No key, no secret.** `feed.yml` references no secret and no environment; no step signs anything.
- **Elevated helper.** Not involved (Phase 10).
- **What tests must never touch.** github.com, api.github.com, krateaudio.com or any network; the real `gh`; the gh-pages
  branch; any VST3, Components or preset folder; ProgramData; the registry; the real plugins' `update_state.json`. The
  C++ case reads two committed fixture files and writes only under the Phase 1 settings-dir redirect, which it
  `REQUIRE`s per real plugin name before that name's first checker exists and again for each written state path
  (FR-046).

---

## Edge cases

| Case | Behaviour | Where |
|---|---|---|
| Interrupted download of a `release.json` / `latest.json` | Size or digest mismatch → `E_FEED_DOWNLOAD`, the run fails before `feed-site` exists, nothing deploys, `feed-evidence` is uploaded; re-dispatch (idempotent) | FR-022, FR-023, FR-011, SC-004 |
| Disk full on the runner | A write fails → `collect` / `generate` exit 1 (atomic writes leave no partial file), `outcome` fails the step, nothing deploys | FR-020, FR-023, FR-029b |
| Fresh checkout without `_site/` | `generate` creates it (gen-feed does no `mkdir`) | FR-029a, SC-001 |
| File locked by a running DAW (Windows) | Not applicable: nothing is installed | Phase 10 |
| Partial install | Not applicable. A partial deploy is not possible either: one peaceiris commit carries `versions.json` and `manager/latest.json` together | FR-013 |
| Downgrade attempt (a release deleted, or a fault lowering a version) | `compare` reports `drift … version lowered block`, the run fails before deploy in both modes (D5, Q1); the live file keeps the higher version. Exception: the live version is a prerelease's (today's inline generator) and the new value is the Q9 stable → `q9-corrected`, exit 0 | FR-040, SC-007 |
| A `version.json` `url` edited on `main` | `newer … download_url <old> -> <new>` once that plugin's next release is in the list; before that, every feed run reports `drift … download_url changed without a newer release warn`; a real run still deploys (other plugins' updates are not blocked) and its `verdict` is red, a dry run fails (Q1) | FR-040, FR-029c, D5 |
| Corrupt feed (generator fault) | gen-feed self-check (exit 1), the C1 re-validation (`E_FEED_SCHEMA`) or the comparison stops it before deploy; in a dry run only the artifact shows it | FR-006, FR-041, FR-010 |
| Corrupt `release.json` on one release | That plugin degrades to v1-only, the feed deploys, the run turns red naming it (D4) | FR-007, Phase 1 FR-049 |
| Offline / Pages down when reading the live file | `curl --fail` fails, or `compare` gets `E_INPUT`: the run fails before deploy; re-dispatch later | FR-005, D6 |
| GitHub API unavailable / rate limit | `gh` exit ≠ 0 is retried (3 attempts, 5 s then 15 s backoff, each logged); still failing → `E_INPUT` abort; ~19 calls per run (attempts count) against 1,000 per hour | FR-023, FR-023a, D2, SC-004 |
| More than 999 releases | `E_FEED_LIST_TRUNCATED`; the limit is raised by a later change | FR-023 |
| Feed `schema_version` 1 (today's live file) vs 2 | `compare` projects either; the first v2 deploy equals the live v1 keys (SC-001); the shipped parser never reads `schema_version` (`update_checker.cpp:209-248`) and Phase 1 proved it reads v2 and 99 | FR-040, SC-009 |
| Bundles that still say `0.8.0` | Not a feed fact (detection, Phase 6) | Phase 6 |
| A tag without a GitHub release (or a draft) | Not in the v2 feed (gen-feed reads releases). If that tag was a plugin's newest, the live legacy file named it and `compare` reports `drift` | FR-040 |
| A prerelease newer than the stable release | Only in the `prerelease` sibling; the v1 `version` stays the stable one (Q9). Against a live file that already follows Q9 (any v2 file `feed.yml` deployed) `compare` sees `same`. Against today's legacy live file, which took the highest tag prereleases included (`release.yml:878`), a prerelease published before Phase 3 deploys makes the live `version` the prerelease's: `compare` then reports `q9-corrected <Name> <prerelease> -> <stable>` (or `-> absent` for a plugin with only prereleases), exit 0. The snapshot has 0 prereleases, so this is latent today | Phase 1 FR-042, FR-040, SC-007 |
| Pages edge cache serves an older live file | Explained by the newer release (`newer`) | D6 |
| `release.yml` dry run | `feed` runs with `dry_run: true` over the published releases; the dry-run release is not in the list | FR-031, SC-015 |
| Landing-page deploy fails in a real release | `feed` still runs (`!cancelled() && needs.release.result == 'success'`) | D8 |
| A live file with `release_notes not empty` or a `removed` plugin (`block`, Q1) | No `feed.yml` run can clear it (the new feed's `release_notes` is `""` by C1; `removed` is a plugin gone from the roster), so every run fails before deploy. Recovery: a hand commit on `gh-pages` that fixes `versions.json`, then a dry run (`same`) and a real run (`CONTRACT.md` §10.5); the same for a live file FR-027 (a) refuses. Latent today (Clarifications P-3; plan SI-7) | FR-040, FR-027 |
| Two feed runs at once (two releases back to back, or a dispatch during a release) | Serialised by `krate-feed` with `queue: max` on jobs `build` and `deploy`: every run executes in order, deploys in build order, none is cancelled (plan SI-3, PD-1) | D9, FR-012 |
| `docs.yml` pushing gh-pages at the same moment | Different concurrency group; a rejected push fails that run (no deploy retry), re-dispatch recovers (both deploy with `keep_files`) | D9, FR-012 |
| Manager release with a bad `latest.json` | Degrade: not copied, no `manager` block, the plugin feed deploys, red run; the previously published `manager/latest.json` stays | D3, FR-025 |
| `feed.yml` dispatched from a branch | Builds from that branch's `version.json` / `CHANGELOG.md`; only possible once the file is on the default branch (External facts). A dry run is fine; a real run builds and uploads but `deploy` is skipped and `verdict` reads `verdict: red real run from <ref>`; same for a `release.yml` real run dispatched from a branch | D10, D7, FR-013, FR-029c |

---

## New components

| Path | Purpose |
|---|---|
| `.github/workflows/feed.yml` | Reusable + dispatchable feed build and Pages deploy (FR-001 to FR-015) |
| `tools/feed-publish.js` | `collect` (gh: release list, selected manifests, manager `latest.json`), `generate` (gen-feed in-process, determinism, `_site/` staging), `outcome` (exit code → job output), `compare` (v1 projection rule) and `verdict` (FR-020 to FR-029e) |
| `tools/feed-contract/fixtures/phase3/membrum-v0.12.2.json` + its generator `tools/feed-contract/fixtures/phase3/make-phase3.js` | The one C2 manifest of the mixed fixture (FR-044); the generator is registered as set `phase3` of `fixtures/make-fixtures.js` and also builds the files below (plan SI-1, PD-14, PD-15) |
| `tools/feed-contract/fixtures/feeds/valid/one-v2-eight-v1.json` (+ its `feeds/cases.json` row) | The one-v2 / eight-v1 feed (FR-045), built by `make-phase3.js`, read by the C++ case and by Phase 5 |
| `tools/feed-contract/fixtures/phase3/compare/` | live / new / releases triples for SC-007, built by `make-phase3.js` (plan PD-14) |
| `tools/feed-contract/tests/feed-publish-collect.test.js`, `feed-publish-compare.test.js`, `feed-publish-steps.test.js`, `feed-pipeline.test.js`, `phase3-fixtures.test.js` | SC-001 to SC-008, SC-017, SC-018 |
| `tools/feed-contract/tests/workflow-shape.test.js` (Phase 3 block) | SC-010 |
| `plugins/shared/tests/test_update_checker.cpp` (one case) + `SHARED_FIXTURES_DIR` | SC-009 |

---

## Traceability

| Roadmap statement (line) | FR / SC |
|---|---|
| `feed.yml` `workflow_call` + `workflow_dispatch` (428) | FR-001, SC-010 |
| `gh release list`, ~20 requests (428-429) | FR-021, FR-022, D2, SC-003 |
| downloads each plugin's newest `release.json` (429-430) | FR-022, SC-003, SC-005 |
| runs `tools/gen-feed.js` (430) | FR-006, SC-001 |
| copies `manager/latest.json` when a manager release exists (430) | FR-024, FR-025, FR-011, D3, SC-006 |
| `peaceiris/actions-gh-pages@v4`, `keep_files: true`, `cname: krateaudio.com` (430-432) | FR-013, SC-010 |
| idempotent, no `fetch-depth: 0` (432-433) | FR-009, FR-013, D10, SC-002, SC-010 |
| `dry_run` default `true`, build, validate C1, compare with live, artifact, no peaceiris (434-439) | FR-001, FR-005, FR-010, FR-011, FR-013, FR-041, D5, SC-007, SC-014 |
| `release.yml` `deploy-pages` calls `feed.yml`, `dry_run` passed (440-441) | FR-030 to FR-033, D8, SC-010, SC-015, SC-016 |
| plugins without `release.json` keep their v1 entry (442-444) | FR-008, SC-005 |
| twice → byte-identical (447) | FR-009, SC-002 |
| today's real tag set → v1 keys identical to live (448) | SC-001 |
| one v2 + eight v1 validates C1 and parses in the C++ guard (449-451) | FR-044 to FR-046, SC-008, SC-009 |
| PENDING-CI dry-run dispatch (452-453) | SC-014 |
| live dispatch + banner = user step (454-455) | "User step" line |
| Does NOT: move the feed, change the landing page, touch the installers (457) | FR-032, FR-060, SC-012 |
| ROSTER (33) | FR-015, FR-020, SC-010, SC-011 |
| Q9 (32): v1 `version` = newest stable | FR-021 (gen-feed's `selectReleases`), FR-040 `q9-corrected`, SC-007, edge cases |
| Q10 (34): after the backfill all nine have `release.json` | Non-goals; FR-008 until then |
| Additive feed (56, 997-998) | D5, FR-040 |

**Additions to the roadmap:** `tools/feed-publish.js` with its five subcommands (D1, D13; the roadmap's tool list,
line 211, names none for this phase), the `verdict` job (D4, D7), the severity-gated comparison on real runs (D5, FR-040, a ruled deviation, Q1),
the `main`-only deploy (D7, Q2), the gh retry (FR-023a), `ci.yml` left unchanged (FR-062, Q6 re-ruled), the `q9-corrected` transition case (FR-040), the check-run annotations (D12, FR-029e), and the FR-025
`latest.json` check.

---

## Open questions

None. The roadmap defers no decision to this spec; D1-D13 are this spec's own choices, and the two items that needed the
user's ruling (OQ-1, OQ-2 of the first draft) are decided in the Clarifications below (Q1, Q6).

---

## Review notes

Review round 1 (2026-10-30). Every issue was applied; where the resolution differs from the suggested one, why:

- **v1 comparison rule / real-run gate (major).** Took option (b) for the rule (FR-040 `newer` now covers a changed
  `download_url`, as the roadmap's wording requires) and recorded the real-run gate as OQ-1 rather than settling it,
  since the reviewer is right that nobody ruled on it.
- **Missing `_site/` (major).** Resolved by `generate` creating `_site/` and `_site/manager/` itself instead of
  `mkdir -p` steps, because the review's other major moved every workflow decision into the tool; SC-001 runs
  `generate` into a non-existent `--site-dir`, and SC-010 asserts no step outside `generate` runs `mkdir`.
- **Degrade / verdict branching in YAML (major).** Took the suggested tool route (`outcome`, `verdict`, `generate`) and
  the capture pattern; did not add a user-dispatched degraded dry run, because SC-018 covers every branch offline.
- **`compare` piped through `tee` (blocker).** Both suggested fixes: `compare --report` / `--summary` (no pipe), and
  `shell: bash` + `set -euo pipefail` on every `run:` step, asserted by SC-010.
- **`ci.yml` fixture filter (minor).** Recorded the gap and raised OQ-2 instead of editing `ci.yml`, because FR-060
  forbids it in this phase.
- **SC-012 base (minor).** Named commit `4f4c4175` rather than `git merge-base HEAD origin/main`: the branch carries
  Phases 1 and 2 unmerged (`git log --oneline origin/main..HEAD`: six commits, incl. `e7f64c34` Phase 1 and
  `4688d20e` Phase 2), so a merge-base diff would list their files too.

---

## Clarifications

### Session 2026-10-30

- Q: On a real run, does a v1 comparison drift block the deploy? -> A: (c) Only drift that would make an editor show a wrong or missing update blocks: `version lowered`, `removed`, `release_notes not empty` (severity `block`) fail the `build` job before the deploy. `download_url changed without a newer release` deploys and the `verdict` turns red (severity `warn`). Dry runs keep failing on any drift. FR-040 gets a severity column; `compare` gets `--dry-run` and exit 3; `outcome` and `verdict` carry `drift_warned`. Assumption: the user named three blocking kinds and one warn kind; the two remaining kinds (`version changed without a release`, `added without a release`) are classed `warn`, per the "added-type kinds deploy" wording of option (c). [FR-010, FR-027, FR-028, FR-029b, FR-029c, FR-029d, FR-029e, FR-040, SC-007, SC-018, SC-014]
- Q: May a real run deploy from a ref other than `main`? -> A: (b) `deploy` gets `if: not inputs.dry_run AND github.ref == refs/heads/main`; `verdict` receives the ref and a skipped real deploy from another ref is red (`verdict: red real run from <ref>`). A `release.yml` run dispatched from a branch builds its feed but does not deploy it. No `pages` environment. [FR-002, FR-013, FR-014, FR-029c, FR-030, SC-010, SC-018]
- Q: How are concurrent gh-pages writers handled? -> A: (b) Concurrency group `krate-feed` with `queue: max`, so every feed run executes in order and no caller run ends cancelled. The plan confirms `queue` is honoured in a called workflow's top-level `concurrency`, or moves the block onto the jobs. No deploy retry; a `docs.yml` push race stays a red run plus a re-dispatch. [FR-012, SC-010]
- Q: What publishes the feed after the live backfill? -> A: (a) A documented user step: after the live backfill, dispatch `feed.yml` with `dry_run: true`, review, then with `dry_run: false`. No code change, no new trigger. [FR-061, FR-053, FR-001, SC-010, SC-012]
- Q: Does `collect` retry a transient `gh` failure? -> A: (b) Bounded retry inside `productionDeps` for `gh release list`, `view` and `download`: 3 attempts, fixed backoff 5 s then 15 s, each attempt logged. Shape, size and digest mismatches are never retried. `gh_calls` and the SC-003 argv list count attempts. A new SC-004 case uses a fake `gh` that fails once then succeeds. Detail added: a download retry first deletes the partial output file (no `--clobber`), and tests apply the exported `withGhRetry` wrapper to the fake `gh` because tests never construct `productionDeps`. [FR-023, FR-023a, FR-026, FR-051, SC-003, SC-004]
- Q: Should `ci.yml` re-run `shared_tests` when only the feed fixtures change? -> A: **Superseded by "Q6 again" in Session 2026-10-30 (plan): ci.yml is not changed.** First answer: (b) Add `tools/feed-contract/fixtures/feeds/**` and `tools/feed-contract/fixtures/gen-feed/**` to `ci.yml`'s `shared` path filter; FR-060's `ci.yml` fence is lifted for those two lines only. Residual recorded: `ci.yml`'s workflow-level `on.*.paths` lists are unchanged, so a change touching only those fixtures and no listed path still does not start `ci.yml`. [FR-060, FR-062, SC-009m, SC-012, SC-010]

### Session 2026-10-30 (plan)

- Q: Accept the plan's spec edits SI-1 to SI-11 and tasks.md TA-1 to TA-4? -> A: Yes, all of them; T001 writes them into
  the FR/SC rows. [P-1]
- Q: A `release.json` asset with `digest: null` (plan SI-5, PD-17)? -> A: (b) Refuse it with `E_FEED_DOWNLOAD`, as Phase 2
  ruled for installers (CC-verify); GitHub gives every new upload a digest (264 of 264 measured stable installer assets
  carry one). T010 changes the null branch of `checkDownloaded`, and the T3 `digest: null` subtest asserts the refusal.
  PD-17 and the `collect: digest checked <n> of <m>` line are superseded. Amends FR-022. [P-2]
- Q: The two `block` drift kinds no `feed.yml` run can clear (`release_notes not empty`, `removed`, plan SI-7)? -> A: (a)
  Keep them `block`, with the documented recovery (a hand commit on `gh-pages` that fixes `versions.json`, then a dry
  run and a real run) in `CONTRACT.md` §10.5 (T017). [P-3]
- Q: Q6 again, with the real cost (the first option text said the filter "re-runs shared_tests"; in `ci.yml` a true
  `shared` output builds and tests all nine plugins on Windows, macOS and Linux, plan R-1). -> A: (a) Leave `ci.yml`
  alone. Supersedes the Q6 (b) answer of Session 2026-10-30: FR-062 now says `ci.yml` is not changed; FR-060, SC-009m/l,
  SC-010 and SC-012 follow, and tasks.md T014 checks `ci.yml` is byte-identical. [Q6]
- Note (main loop, after the first build stop): the user asked what `release.json` is after answering P-2; the main loop
  explained it (the per-release record Phase 2 attaches next to the installers, which `collect` downloads and gen-feed
  merges into `versions.json`), and the user replied "ok, cool" and asked to continue. [P-2] stands as recorded: refuse a
  null-digest `release.json`. [P-2]
- Build close (main loop): the CC-lints fix had to teach `tools/check-portability.js` the fixtures macro, and that
  file is not an FR-060 path (SC-012 red). The macro is renamed `SHARED_FIXTURES_DIR`, the repo's
  `<PLUGIN>_FIXTURES_DIR` name that `check-portability.js:121` already defines for every plugin directory, and the
  `check-portability.js` edit is reverted. FR-046, FR-054 and SC-009 refer to that name. [SC-012]
