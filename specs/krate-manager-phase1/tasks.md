# Tasks: Krate Manager Phase 1 — Feed v2 Schema and Generator

**Spec:** `specs/krate-manager-phase1/spec.md` · **Plan:** `specs/krate-manager-phase1/plan.md` (revised 2026-10-30, PD-1 to PD-18)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 1 (lines 251-329)
**Status:** DRAFT task list, 2026-10-30, written against `feat/krate-manager` at `84c6aab1` (untracked at start:
`specs/Krate-manager-roadmap.md`, `specs/krate-manager-phase1/`)
**Lanes in this phase:** pipeline lane only (Node tools and tests, one workflow YAML, one C++ test TU). There is no
`apps/krate-manager/` yet (Phase 4), so there is no app lane: no `cargo`, `tsc`, `vitest` or Tauri step in any task.

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks in parallel; every task in it creates only new files, and no
  two tasks share a file. Every other group is **sequential**: its tasks run one after another in the order listed.
  A task that edits a shared or pre-existing file (`package.json`, `package-lock.json`, `.gitattributes`, `spec.md`, a
  workflow, `test_update_checker.cpp`) or a file that another task also edits (`contract.js`, `make-fixtures.js`,
  `gen-feed.js`, `hygiene.test.js`, `c2-release.test.js`, `c3-trusted-comment.test.js`, `c4-listing-tar.test.js`,
  `fixtures/README.md`) is always in a sequential group.
- Each task section is the executor's authoritative instruction. It names the files, the failing test to write first
  (file, test name, assertions), the implementation, and the lane that verifies it. Where a table is too large to copy
  without drift risk, the section names the exact `plan.md` section that holds it; that table is then binding.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed, and the task's notes say so.
- Never commit, never push, never trigger CI. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Write guard first.** Every `tools/feed-contract/tests/*.test.js` file starts with an optional `'use strict';` line
   followed by `require('./helpers/write-guard.js');` as its first statement (FR-061, SC-015b). Tests write only under
   `fs.mkdtemp(path.join(os.tmpdir(), '<prefix>-'))` directories and remove them in `t.after` / `finally`.
2. **No network in tests or tools.** No `http`, `https`, `http2`, `net`, `tls`, `dgram`, no `fetch(` in any scanned file
   (SC-015a). The only network steps in this phase are agent actions, never tests: `npm install` (T003), the one-time
   snapshot capture (T008), `npx yaml` (lane), `cargo install` (CI only).
3. **Module rule (plan §2).** Call `fs.<fn>`, `fs.promises.<fn>` and `childProcess.<fn>` through the module object at
   call time. Never `const { writeFileSync } = require('fs')` or the same for `child_process` (the guard patches module
   properties; a destructured binding would bypass it). `write-guard.js` is the only exemption.
4. **CommonJS** (`'use strict'`, `require`), like every `tools/*.js` (`tools/header-consumers.js:17-20` per plan §2).
   No `"type": "module"`. Header comment in the `tools/*.js` style for executable scripts.
5. **No plugin roster in product code.** `tools/gen-feed.js`, `contract.js`, `listing.js` and the three schema files
   contain none of the nine plugin names (SC-014). Fixture names are `Alpha`, `Bravo`, `Charlie`, … , `KatPlugin`,
   `FeedV2Probe`; only the snapshot fixture (T008) holds real names, as copied data.
6. **Never touch the real system.** No task writes to a VST3, Components or preset folder, `ProgramData`, the registry,
   `%APPDATA%\Krate Audio\…` or any path outside the repo, `F:/tmp` and an `fs.mkdtemp` directory. No task runs an
   installer. The C++ `[update]` cases are never run before T017's guard is in place (they wrote
   `%APPDATA%\Krate Audio\Iterum\update_state.json` until now, spec "Existing repo pieces" row for
   `update_checker.cpp:297-311`).
7. **Keys.** The only private key this phase ever creates is the throwaway KAT key (T007), made in an `fs.mkdtemp`
   directory under `os.tmpdir()` (outside the repo) and deleted right after signing.
8. **Scope fence (FR-070).** Only these paths change: `tools/gen-feed.js`, `tools/feed-contract/**`,
   `plugins/shared/tests/test_update_checker.cpp`, `.github/workflows/feed-tools.yml`, `package.json`,
   `package-lock.json`, `.gitattributes`, `specs/krate-manager-phase1/**`, and `specs/INDEX.md` only if the PD-9 ruling
   puts it in scope (T001).
9. **Zero warnings.** Node: `node --check` clean, and the TAP output carries no `Warning:` / `DeprecationWarning` /
   `ExperimentalWarning` line from Node. C++: 0 compiler warnings under `/W4` (MSVC) and `-Wall -Wextra` elsewhere
   (`plugins/shared/tests/CMakeLists.txt:59-80` per plan §1.1), and clang-tidy clean on the one TU. Never silence a
   warning without a one-line reason the reviewer accepts.

### Lanes (referenced by every task)

| Lane | Commands | Pass |
|---|---|---|
| **L-NODE** | `node --check <each new or changed .js>`; the task's own file(s): `node --test --test-reporter=tap "tools/feed-contract/tests/<file>.test.js"`; then the whole set: `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` (quoted glob, expanded by Node; a directory argument runs nothing on Node 24, spec External facts; default process isolation and concurrency, never `--test-concurrency=1` or `--test-isolation=none`) | every `node --check` exits 0; TAP `# fail 0`; no Node warning line |
| **L-YAML** | `npx --yes yaml@2.9.1 valid < .github/workflows/feed-tools.yml` (the workflow's own gate runs `yaml@2.6.1`, `.claude/workflows/krate-manager-phase.js:598`; both are fine) | exit 0 |
| **L-LINT** | `node tools/lint-serial-test-runs.js` | exit 0 |
| **L-CPP** | `"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests -- /nr:false > F:/tmp/gate/km1-cpp-build.log 2>&1` then `tail -5` of it; `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests` (`tools/run-gate.js:52-54` refuses a filter without the four timing exclusions) | build exit 0 and 0 lines matching the case-sensitive regex `\bwarning\b` (MSBuild's summary says `Warning(s)` with a capital W and does not match); last line `GATE GREEN` |
| **L-CLOSE** | `node specs/krate-manager-phase1/evidence/close-lane.js` (written by T020, run by the **main loop** at close: it holds the long C++ build, clang-tidy and the measured SC rows) | its last line `CLOSE LANE GREEN` |

---

## Preconditions — rulings needed before the build stage

The plan leaves two questions to the user (plan §10), and writing this task list found a third. **T001 encodes the
rulings into `spec.md` and stops the build if any is missing.** The main loop records each ruling in `spec.md`
`## Clarifications` under a new `### Session 2026-10-30` heading (one bullet per question, `Q: … → A: …`, tagged
`[PD-8]`, `[PD-9]`, `[FR-059 dup]`) before running the build stage.

- **Q-A (PD-8, plan §10 Q1).** An unparseable `release.json`, and two manifest files with the same `tag`: **degrade**
  (WARN, exit 3, the feed is still written; new code `E_MANIFEST_DUPLICATE`; an unparseable file's WARN names the file
  because no plugin can be named) — the plan's recommendation — or **abort** (`E_INPUT`, exit 1, nothing written).
- **Q-B (PD-9, plan §10 Q2).** Regenerate `specs/INDEX.md` in this change and add it to FR-070 / SC-020 (recommended:
  `node tools/gen-specs-index.js --check` already exits 1 because this phase's spec exists, and `ci.yml:156`
  "Generated Artifacts In Sync" runs it at `ci.yml:172` on the PR to `main`), or leave it for the commit that adds the
  specs and accept that job red until then.
- **Q-C (new, FR-059 "E_INSTALL_DUPLICATE once per field").** Through `gen-feed.js` that requirement can be met for
  two of the four fields only. FR-017 makes `checkReleaseManifest` require `install.vst3_bundle === name + ".vst3"` and
  `au_component === name + ".component"`, and gen-feed aborts on two plugin names that are equal case-folded (PD-13),
  so two manifests of different plugins can never share `vst3_bundle` or `au_component` without one of them failing
  FR-017 first (`E_MANIFEST_SCHEMA`, degrading only that plugin). `windows_uninstall_key` (two `setup.iss` files with a
  copied AppId) and `macos_pkg_id` (two `version.json` with the same `identifier`, or check 1 skipped) are reachable.
  Recommended: amend FR-059 so the gen-feed duplicate cases cover `windows_uninstall_key` and `macos_pkg_id`, plus one
  case `degrade-copied-bundle-name` (a manifest that carries another plugin's `vst3_bundle` and `au_component` degrades
  only itself with `E_MANIFEST_SCHEMA`); all four fields stay covered at the `validateFeed` level (SC-026, unchanged).
  Alternative: keep FR-059 literally, which would need a test-only seam into gen-feed's uniqueness step.

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | sequential | T001 spec rows + rulings · T002 `.gitattributes` · T003 `ajv` · T004 write guard · T005 `contract.js` core + C3 | rulings (Preconditions) |
| G2 Contract pieces | **[P]** | T006 C4 `listing.js` + archives · T007 KAT archive + signature · T008 snapshot capture · T009 schemas | G1 |
| G3 Contract validation | sequential | T010 `CONTRACT.md` · T011 C1 validation + fixtures · T012 C2 validation + fixtures · T013 KAT listing + KAT tests | G2 |
| G4 Generator | sequential | T014 legacy mode · T015 v2 mode · T016 rejections and degrades | G3 |
| G5 C++ guard | sequential | T017 `test_update_checker.cpp` | G1 (placed late so earlier gates find no modified shared file) |
| G6 Integration | sequential | T018 `feed-tools.yml` · T019 remaining hygiene tests · T020 lints, close lane, scope check, PENDING-CI | G1-G5 |

---

## G1 — Foundation (sequential)

### T001 — Encode the plan's spec amendments and the three rulings into `spec.md`

**Files:** `specs/krate-manager-phase1/spec.md` (edit).

**Precondition (STOP AND SURFACE).** Read `spec.md` `## Clarifications`. If it does not contain a ruling for each of
Q-A, Q-B and Q-C (Preconditions above), return status `blocked` with notes quoting the three questions verbatim from
this file and change nothing.

**Test first.** Docs-only task: no test file. Before editing, record in the notes which of the rows below already
carry the change (expected: none).

**Implement.** Edit each row in place, appending `(plan PD-n)` or `(Clarifications 2026-10-30 Q-x)` to the changed
sentence. The plan's "Spec rows to amend before implementation" (plan Review notes, last paragraph) is the list; none
relaxes a threshold:

| Row | Change | Source |
|---|---|---|
| FR-002 | plugin key also rejects the Windows reserved device names `CON`, `PRN`, `AUX`, `NUL`, `COM0`-`COM9`, `LPT0`-`LPT9` in any case, with or without an extension | PD-14 |
| FR-008 | each name rejects a reserved device stem; `preset_subdir` pattern becomes `^[A-Za-z0-9]([A-Za-z0-9 _-]*[A-Za-z0-9_-])?$` (no trailing space) | PD-14 |
| FR-010 | the parenthetical `not: anyOf required` becomes `properties: {version: false, download_url: false, release_notes: false}` (same meaning; the old form fails Ajv strict compile) | PD-1 |
| FR-014 | `vst3_bundle`, `au_component`, `macos_pkg_id` compared after ASCII case folding; `windows_uninstall_key` exact | PD-13 |
| FR-015, FR-016 | C2 states the per-key `format` binding (`patternProperties` on `^windows-` / `^darwin-` / `^linux-`) and `install` requires `vst3_bundle`, `au_component`, `windows_uninstall_key`, `macos_pkg_id` (FR-008 applied, since every C2 manifest has a `windows-x86_64` and a `darwin-*` asset); gen-feed's `checkAssembledEntry` backstop degrades with `E_MANIFEST_SCHEMA` | PD-10 |
| FR-031 | three rules: a link target holds `..` only as a leading run (`E_LISTING_SYMLINK_FORM`); no entry has an `l` entry as a proper ancestor (`E_LISTING_THROUGH_SYMLINK`); neither a resolved target nor any prefix of it is an `l` entry (`E_LISTING_SYMLINK_CHAIN`); `E_LISTING_DOTDOT` is for entry paths only | PD-12 |
| FR-044 | the section header is matched as the literal `## [<version>]` at line start (same output as the awk regex for every real header) | PD-4 |
| FR-046 | adds `E_LISTING_SYMLINK_FORM`, `E_LISTING_SYMLINK_CHAIN`, `E_LISTING_THROUGH_SYMLINK` (degrade); `E_DUPLICATE_PLUGIN` compares `name.toLowerCase()`; Q-A: DEGRADE → adds `E_MANIFEST_DUPLICATE` (degrade) and states that an unparseable manifest is `E_MANIFEST_SCHEMA` degrade with a WARN naming the file; ABORT → both are `E_INPUT` abort | PD-12, PD-13, Q-A |
| FR-055 | `cases.json` rows are `{rule, file, schema?, expect, plugin?}` (plan §3.1) | PD-11 |
| FR-058 | good set gains `ustar-prefix`; bad set gains `pax-malformed`, `base256-size`, `no-end-block`, `symlink-dotdot-chain`, `symlink-chain`, `symlink-self`, `through-symlink` | PD-18, PD-12 |
| FR-059 | Q-C ruling (recommended text: the gen-feed `E_INSTALL_DUPLICATE` cases cover `windows_uninstall_key` and `macos_pkg_id`, plus `degrade-copied-bundle-name`, because FR-017 binds `vst3_bundle` / `au_component` to the plugin name) | Q-C |
| FR-061 | the guard refuses any `..` segment before resolution, checks both paths of `link`, wraps `chown*`, `lchown*`, `lutimes*`, `mkdtempDisposable*`; `child_process` is an allowlist (`git`, `tar` in fixed argument shapes; `exec*`, `fork` and `shell: true` refused) | PD-15 |
| FR-070, SC-020 | Q-B: IN → add `specs/INDEX.md`; OUT → unchanged, and a sentence records that `Generated Artifacts In Sync` stays red until the specs commit | Q-B |
| SC-008 | good-set list gains `ustar-prefix` | PD-18 |
| SC-021 | the pre-write `REQUIRE` sits before any `MockUpdateChecker` is constructed (stricter than "before its first `checkForUpdate`": `dismissVersion` writes state, `update_checker.cpp:69-76`) | PD-5 |
| SC-023 | Q-A DEGRADE only: "naming the plugin" becomes "naming the plugin, or the file when no plugin can be named" | Q-A |

**Done when:** every row above carries its change and its source tag, and the notes list each edited row id (the spec
is untracked, so there is no git diff to cite). **Lane:** none (docs).

### T002 — `.gitattributes`: fixtures are byte-exact (FR-063)

**Files:** `.gitattributes` (edit, append only).

**Test first.** `git check-attr text -- tools/feed-contract/fixtures/kat/probe.minisig` prints
`tools/feed-contract/fixtures/kat/probe.minisig: text: unspecified` (the file need not exist). Also confirm
`tools/feed-contract/` does not exist yet (`ls tools/feed-contract` fails): the rule must land before any fixture.

**Implement.** Append after line 22 (the file has 22 lines of LF pins, verified 2026-10-30) one blank line and:

```
# Byte-exact fixtures: signatures, SHA-256 digests and golden outputs compare raw bytes, and this checkout runs
# core.autocrlf=true, which would rewrite them on Windows checkout.
tools/feed-contract/fixtures/** -text
```

**Done when:** the same `git check-attr` command prints `…: text: unset`; `git diff .gitattributes` shows only the four
added lines. **Lane:** none (attributes); verified again by SC-008/SC-012/SC-013 byte comparisons later.

### T003 — Add `ajv` 8.20.0 (FR-062)

> **Superseded at close (spec Clarifications 2026-10-30 Q-D):** the root `package.json` / `package-lock.json` are
> gitignored, so `ajv` lives in a tracked `tools/feed-contract/package.json` + lock (`npm install --prefix tools/feed-contract`;
> CI `npm ci --prefix tools/feed-contract`), and `.gitignore`'s two lines are anchored to `/package.json` / `/package-lock.json`.
> Read every root `package.json` / `package-lock.json` below as `tools/feed-contract/package.json` / `package-lock.json`.

**Files:** `package.json`, `package-lock.json` (edit).

**Test first.** `node -e "require('ajv/dist/2020')"` fails with `MODULE_NOT_FOUND` (root `package.json` has only
`fast-xml-parser`, `saxon-js`, `xslt3`, verified 2026-10-30).

**Implement.** At the repo root: `npm install --save-dev --save-exact ajv@8.20.0` (plan §4.10; version and its four
runtime dependencies verified in plan §1.2 against `npm view ajv@8.20.0` and https://unpkg.com/ajv@8.20.0/dist/2020.js).

**Done when:**
- `node -e "const A=require('ajv/dist/2020'); new A({strict:true,allErrors:true}); console.log(require('ajv/package.json').version)"` prints `8.20.0`;
- `git diff package.json` adds exactly `"ajv": "8.20.0"` under `devDependencies` (no caret);
- the lockfile diff adds only `node_modules/ajv`, `node_modules/fast-deep-equal`, `node_modules/fast-uri`,
  `node_modules/json-schema-traverse`, `node_modules/require-from-string` package entries (list the added
  `"node_modules/…"` keys in the notes);
- `npm ci` exits 0; `git status --porcelain` shows no `node_modules` path (ignored, `.gitignore:92`).

**Lane:** none beyond the checks above.

### T004 — Write guard and `tools stay offline` (FR-061, SC-015 a-c, PD-15)

**Files (new):** `tools/feed-contract/tests/helpers/write-guard.js`, `tools/feed-contract/tests/hygiene.test.js`.

**Test first — `hygiene.test.js`, named test `tools stay offline`** (later tasks add other named tests to this file).
Write it before the guard exists; it fails at its first statement (`MODULE_NOT_FOUND`), which is the recorded red.
Subtests:
- **(a) source scan.** Files: every file that exists among `tools/gen-feed.js`, `tools/feed-contract/*.js`,
  `tools/feed-contract/tests/*.test.js`, `tools/feed-contract/tests/helpers/*.js`,
  `tools/feed-contract/fixtures/make-fixtures.js`, `tools/feed-contract/fixtures/listings/make-bad.js` (T019 later
  asserts all of them exist). Assert 0 matches of
  `(require\s*\(|import\s*\(|from\s+|import\s+)\s*['"](node:)?(http|https|http2|net|tls|dgram)['"]` and of
  `/\bfetch\s*\(/`; outside `tests/git-versionsort.test.js`, `tests/c4-listing-dir.test.js` and
  `tests/helpers/write-guard.js`, 0 matches of `(node:)?child_process`. Assert 0 matches of a destructured
  `{ … } = require('(node:)?fs')`, `require('(node:)?fs/promises')` destructure, or `{ … } = require('(node:)?child_process')`
  outside `write-guard.js` (plan §2 module rule). Build every pattern from pieces (`'child' + '_process'`,
  `'fet' + 'ch'`) so the scanner does not match its own source.
- **(b) first statement.** Every `tools/feed-contract/tests/*.test.js`: after leading comments, blank lines and an
  optional `'use strict';` line, the first statement is exactly `require('./helpers/write-guard.js');`.
- **(c) teeth.** With `tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'km1-guard-'))` and `repoDir` =
  `tools/feed-contract/tests` (absolute), and a random 12-hex `name`: each of `fs.writeFileSync(repoDir/name, 'x')`,
  `await fs.promises.writeFile(repoDir/name, 'x')`, `fs.mkdirSync(repoDir/name)`,
  `fs.renameSync(tmp/a, repoDir/name)` (after writing `tmp/a`), `fs.openSync(repoDir/name, 'w')` throws an error whose
  `.code === 'E_WRITE_OUTSIDE_TMP'`, and `fs.existsSync(repoDir/name)` is false afterwards; `spawnSync('git',
  ['status'], {cwd: repoDir})` throws `E_WRITE_OUTSIDE_TMP`. PD-15 cases: create `L` = `fs.symlinkSync(repoDir, tmp/L,
  process.platform === 'win32' ? 'junction' : 'dir')`; `fs.writeFileSync(tmp + '/L/../' + name)` (string concatenation,
  so the `..` survives) throws; `fs.writeFileSync(path.join(tmp, 'L', name))` throws; neither `repoDir/name` nor
  `tmp/name` exists after. `fs.linkSync(<absolute path of hygiene.test.js>, tmp/name)` throws and `tmp/name` is absent.
  `execSync('echo x')` throws; `spawnSync('node', [], {cwd: tmp})` throws; `spawnSync('git', ['-C', repoDir, 'status'],
  {cwd: tmp})` throws; `spawnSync('tar', ['-xzf', 'x.tar.gz', '-C', repoDir], {cwd: tmp})` throws;
  `spawnSync('tar', ['-xzf', 'x.tar.gz', '-C', tmp], {cwd: tmp, shell: true})` throws. A positive control:
  `fs.writeFileSync(path.join(tmp, 'ok'), 'x')` succeeds. `tmp` is removed in `finally`.
- (d) is not a Node test: the CI porcelain step (T018) and the close lane's before/after record (T020).

**Implement `write-guard.js`** exactly per plan §4.6: installs once per process (`globalThis[Symbol.for('krate.writeGuard')]`);
`TMP = fs.realpathSync.native(os.tmpdir())`, compared case-insensitively on `win32`; `toPathString` (string, Buffer,
`file:` URL); `rejectDotDot` (any `..` segment split on `/`, and `\` on win32, or a `\\?\` / `\\.\` prefix → throw
before resolving); `canonicalFollow` and `canonicalEntry`; `assertUnderTmp` throwing `Error` with
`code = 'E_WRITE_OUTSIDE_TMP'` and a message naming the operation and path. Wrap (sync, callback and `fs.promises`
forms where each exists): `writeFile`, `appendFile`, `mkdir`, `mkdtemp`, `mkdtempDisposable` (both exist in Node
v24.7.0, plan §1.2), `rename` (both paths, entry form), `copyFile` / `cp` (destination), `rm`, `rmdir`, `unlink`
(entry form), `symlink` (new path, entry form), `link` (new path entry form **and** existing path follow form),
`truncate`, `chmod`, `lchmod` (when defined), `chown`, `lchown`, `utimes`, `lutimes`, `createWriteStream`, `open` with
any flag other than `'r'` (string flags, or numeric flags with any of `O_WRONLY | O_RDWR | O_CREAT | O_TRUNC |
O_APPEND`). `child_process`: `exec`, `execSync`, `fork` always throw; any `options.shell` truthy throws; `spawn`,
`spawnSync`, `execFile`, `execFileSync` require `options.cwd` under TMP, the command exactly `'git'` or `'tar'`, no
`GIT_DIR` / `GIT_WORK_TREE` / `GIT_INDEX_FILE` in `options.env`; `git` args: only leading `-c user.name=…` /
`-c user.email=…` pairs, then a subcommand among `init`, `commit`, `update-ref`, `tag`, and no `-C`, `--git-dir`,
`--work-tree` (with or without `=`) anywhere; `tar` args exactly `['-xzf', <source>, '-C', <dest>]` with `<dest>`
under TMP.

**Done when:** L-NODE green for `hygiene.test.js` (red first recorded); L-LINT exit 0.

### T005 — `contract.js` core, C3 grammar, the fixture builder and `C3 trusted comments` (FR-020 to FR-022, FR-057, PD-7, PD-17)

**Files (new):** `tools/feed-contract/contract.js`, `tools/feed-contract/fixtures/make-fixtures.js`,
`tools/feed-contract/fixtures/trusted-comments.json` (generated), `tools/feed-contract/fixtures/README.md`,
`tools/feed-contract/tests/c3-trusted-comment.test.js`.

**Test first — `c3-trusted-comment.test.js`, named test `C3 trusted comments`** (SC-007). It reads
`fixtures/trusted-comments.json` = `{"valid": [<comment>, …], "invalid": [{"comment": …, "code": …, "why": …}, …],
"binding": {…}}` and asserts:
- every valid comment: `parseTrustedComment(c)` returns `{name, version, platform, asset}`, and
  `formatTrustedComment(name, version, platform, asset) === c`. The valid set holds a stable version, a prerelease
  version (`1.2.0-beta.1`) and one comment per key of `PLATFORM_KEYS` (`windows-x86_64`, `darwin-aarch64`,
  `darwin-x86_64`, `darwin-universal`, `linux-x86_64`), each with its FR-017 asset name;
- every invalid comment throws a `ContractError` whose `.code` equals the row's `code`. Rows (FR-057) and codes:
  five fields and seven fields → `E_TC_FIELDS`; prefix `krate:2:…` → `E_TC_FIELDS`; platform `windows-aarch64` →
  `E_TC_PLATFORM`; versions `1.2`, `01.2.3`, `1.2.3+b1`, `1.2.4294967296` → `E_TC_VERSION`; asset with `/`, with `\`,
  and `..` → `E_TC_ASSET`; asset with `:` → `E_TC_FIELDS` (the colon makes seven fields; `why` says so); empty name →
  `E_TC_NAME`; an embedded CR → `E_TC_FIELDS`;
- `formatTrustedComment` on each invalid field value throws the same code as parsing would;
- binding: the comment of asset A (`Alpha-1.2.0-Linux-x64.tar.gz`, `linux-x86_64`) checked by `checkBinding` against
  asset B's metadata (`Alpha-1.2.0-Windows-x64.exe`, `windows-x86_64`) throws `E_TC_BINDING` and the error's `field`
  is `platform` (the first differing field in the order name, version, platform, asset); a version-only difference
  names `version`; an asset-only difference names `asset`;
- `parseMinisigText` on a fixture signature text (built in the test with the T005 builder helper
  `fixtureSignature(comment)`) returns `algorithm === 'ED'`, an 8-byte `keyId`, the comment; the same text with CRLF
  line ends parses identically; a text with three lines, a missing `trusted comment: ` prefix, a blob of 73 bytes, an
  `Ed` blob, or non-canonical base64 throws `E_SIGNATURE_TEXT`; `toLfMinisig(crlfText) === lfText`.

The red: `contract.js` does not exist.

**Implement `contract.js`** per plan §3.1, §3.2 and §4.2, **except** `validateFeed`, `validateRelease`,
`checkReleaseManifest` and `checkAssembledEntry` (T011, T012): `ContractError`; `CODES` (a frozen object of every
code of plan §3.2 → `'usage' | 'abort' | 'degrade' | 'abort-or-degrade' | 'none'`; `E_MANIFEST_DUPLICATE` present only
under the Q-A DEGRADE ruling recorded by T001); `PLATFORM_KEYS`; `ASSET_SUFFIX` (from `release.yml:524-526`);
`parseSemVer` (SemVer 2.0.0, BigInt parts, no build metadata), `compareSemVer` (§11 precedence,
https://semver.org/spec/v2.0.0.html), `isStableCore` (FR-004 bounded core); `isPluginName` (FR-002 + PD-14);
`checkUrl` (`E_URL_NOT_HTTPS` for anything not `^https://[^\s]+$`, `E_URL_LATEST` for `/releases/latest/`);
`checkInstallValues` (`E_INSTALL_VALUE` for `/`, `\`, `:`, `..`, a reserved device stem, a trailing space in
`preset_subdir`, or a value outside its FR-008 pattern as amended by T001); `findInstallDuplicates` (FR-014 as amended:
case-folded for three fields, exact for `windows_uninstall_key`; an entry and its own sibling are one plugin; v1-only
entries take no part); `definedPlatformKeys`; `formatTrustedComment`, `parseTrustedComment`, `checkBinding`,
`parseMinisigText`, `toLfMinisig`, `assetNameFor(name, version, key)`, `perTagUrl(repo, tag, asset)`. The regexes are
written once as constants and reused (the schema files in T009 carry the same pattern text).

**Implement `make-fixtures.js`** (PD-7, plan §4.7): `#!/usr/bin/env node`, CommonJS, `require`s only `node:` modules,
`../contract.js` and (from T012) `../listing.js`. CLI `--out <dir>` (writes every fixture set) and `--check` (builds
into `fs.mkdtemp` under `os.tmpdir()`, compares byte for byte with the committed tree, exits 1 naming the first
differing or stray file, exits 0 otherwise). Exports `build(outDir)` and `check()` (returns `{ok, diff}`) so T019's
drift test can run it in-process. A set registry: each set declares the paths it owns; `--check` compares owned files
and flags a stray file inside an owned directory (gen-feed goldens `expected.*` and the snapshot directory are never
owned). Helper `fixtureSignature(comment)` per plan §4.7 (zero-filled `ED` blob, zero key id, zero global signature,
untrusted comment `krate fixture signature (not cryptographic)`). In this task the registry has one set:
`trusted-comments.json`. Generate it with `node tools/feed-contract/fixtures/make-fixtures.js --out tools/feed-contract/fixtures`
(an agent action, not a test).

**Implement `fixtures/README.md`:** one section per fixture set (what it is, which phase reads it: Phases 2, 3, 5,
7, 8), starting with `trusted-comments.json`; later tasks append their sections.

**Done when:** L-NODE green for `c3-trusted-comment.test.js` and `hygiene.test.js` (red first recorded);
`node tools/feed-contract/fixtures/make-fixtures.js --check` exits 0.

---

## G2 — Contract pieces [P] (parallel: every task creates only its own new files)

### T006 [P] — C4 `listing.js`, the listing archives, and both C4 tests (FR-030 to FR-033, FR-058, SC-008, SC-009, PD-12, PD-18)

**Files (new):** `tools/feed-contract/listing.js`; `tools/feed-contract/fixtures/listings/make-bad.js`;
`tools/feed-contract/fixtures/listings/README.md`; `tools/feed-contract/fixtures/listings/good/<name>.{tar.gz,listing,sha256}`;
`tools/feed-contract/fixtures/listings/bad/<name>.{tar.gz,expected-error}`;
`specs/krate-manager-phase1/evidence/make-good-archives.js`;
`tools/feed-contract/tests/c4-listing-tar.test.js`; `tools/feed-contract/tests/c4-listing-dir.test.js`.

Every listing fixture uses the bundle `Alpha.vst3` (so FR-033's `Contents/x86_64-linux/Alpha.so` applies).

**Test first — `c4-listing-tar.test.js`, named test `C4 listings from tar headers`** (SC-008), red because
`listing.js` does not exist. Subtests:
- one per good archive (names below): `toText(listingFromTarGz(buf))` is byte-equal to `<name>.listing`, and
  `digest(lines)` equals `<name>.sha256` trimmed; on failure print the first differing line;
- record presence via `readTarEntries(tarBytes, {raw: true})`: `long-name` shows typeflag `L`; `long-link` shows `K`;
  `pax-path` shows an `x` whose records hold both `path` and `linkpath`; `pax-global` shows `g`; `ustar-prefix` has a
  regular-file header with magic `ustar\0` at byte 257, version `00` at 263 and a non-empty `prefix` at 345, and its
  joined `prefix + "/" + name` equals a line of `ustar-prefix.listing`;
- one per bad archive: if `.expected-error` is `E_LAYOUT`, `listingFromTarGz` succeeds and
  `checkLinuxLayout(lines, {vst3Bundle: 'Alpha.vst3'})` throws `E_LAYOUT`; otherwise `listingFromTarGz` throws a
  `ContractError` with that code;
- `checkLinuxLayout` accepts `real-shaped`;
- **`FR-031 rules on in-memory entries`**: the table of plan §7.1 row SC-008 through `listingFromEntries` directly:
  invalid UTF-8 (`Buffer.from([0xC3, 0x28])` path), NUL, CR, LF, ` -> ` in a path, empty path → `E_LISTING_PATH`;
  target `/etc/x` → `E_LISTING_ABSOLUTE`; target `C:x` → `E_LISTING_DRIVE`; target `a\b` → `E_LISTING_BACKSLASH`;
  paths `a//b`, `a/./b` → `E_LISTING_DOTDOT`; targets `a/../b`, `./a`, `..` → `E_LISTING_SYMLINK_FORM`;
  `{type: 'other', typeflag: '6'}` → `E_LISTING_TYPE`; the chain (`l A/b/c/s -> ../../d`, `l A/b/c/t -> s/x`) →
  `E_LISTING_SYMLINK_CHAIN`; a self-link → `E_LISTING_SYMLINK_CHAIN`; `l A/t -> b` plus `f A/t/evil` →
  `E_LISTING_THROUGH_SYMLINK`; accept rows: `Alpha.vst3/Contents/x86_64-linux/a -> Alpha.so` and
  `Alpha.vst3/Contents/x86_64-linux/sub/b -> ../../Resources/moduleinfo.json` (with the targets present as entries);
  mode normalisation (D3): a file with mode `0o750` lists `0755`, `0o640` lists `0644`, a directory `0o700` lists
  `0755`, a symlink lists `0777`; implied ancestors are added as `d 0755`; a root `./` entry is dropped;
- **`FR-033 layout items`**: `checkLinuxLayout` on `real-shaped`'s lines minus `Alpha.vst3/Contents/Resources/moduleinfo.json`,
  minus `Alpha.vst3/Contents/x86_64-linux/Alpha.so`, with the `.so` renamed `Other.so`, and with an extra root item:
  each throws `E_LAYOUT` and the message names the item.
The KAT archive is added to this test by T013 (it does not exist yet); nothing here refers to it.

**Test first — `c4-listing-dir.test.js`, named test `C4 listings from extracted trees`** (SC-009): one subtest per
good archive, same subtest names on every OS. POSIX: `tmp = fs.mkdtempSync(…)`,
`childProcess.spawnSync('tar', ['-xzf', <absolute archive path>, '-C', tmp], {cwd: tmp})` exits 0,
`toText(listingFromDirectory(tmp))` equals the committed `.listing`; where the `.listing` has an `l` line or a `f 0755`
line, assert it is present in the directory listing; no line contains `/private/`. `win32`: assert
`listingFromDirectory(tmp)` throws `E_LISTING_NO_POSIX_MODES` (`tmp` from `fs.mkdtempSync`; no extraction). Locally
(Windows) only the refusal branch runs; the POSIX branch is PENDING-CI (`node-test (ubuntu-latest)`,
`node-test (macos-latest)`).

**Implement `listing.js`** per plan §4.3 (API, tar reader steps 1-6, FR-031 checks in the stated order, the old-GNU
vs POSIX magic gate for `prefix`, `listingFromDirectory` built from walk names, never `realpath`).

**Make the archives.**
- `evidence/make-good-archives.js` (Node; one array of commands, run once): for each good archive and the two layout
  archives it builds the POSIX tree inside WSL under a `mktemp -d` directory in WSL `/tmp` (never under `/mnt/f`, which
  would strip modes and links), then runs
  `tar --owner=0 --group=0 --numeric-owner --mtime='2026-10-29 00:00:00Z' --sort=name --format=gnu -cf - -C <tree> <items> | gzip -n -9 > /mnt/f/projects/iterum/tools/feed-contract/fixtures/listings/good/<name>.tar.gz`
  through `wsl -e bash -c '<command>'` (GNU tar 1.35 in WSL, plan §1.2), and removes the WSL tree. Format exceptions:
  `pax-path` and `pax-global` use `--format=pax`, `pax-global` adds `--pax-option=comment=krate-fixture`;
  `ustar-prefix` uses `--format=ustar`. Trees (all at the root: `Alpha.vst3/`, `presets/`, `README.txt`,
  `CHANGELOG.txt`, with `Alpha.vst3/Contents/Resources/moduleinfo.json` and `Alpha.vst3/Contents/x86_64-linux/Alpha.so`
  unless stated): `real-shaped` (FR-033 layout, preset names with spaces, files `0644`, dirs `0755`, as the real
  tarball measured in the spec "Real data"); `exec-and-symlink` (`Alpha.so` mode `0755`, plus
  `Alpha.vst3/Contents/x86_64-linux/libalpha.so -> Alpha.so`); `long-name` (a preset path over 100 bytes);
  `long-link` (an in-bundle symlink whose relative target is over 100 bytes and resolves inside the bundle);
  `pax-path` (a path over 255 bytes and a symlink target over 100 bytes, both inside the bundle); `pax-global`;
  `implicit-dirs` (`--no-recursion` with file paths only, no directory entries); `dot-slash` (`-C <tree> .`, so
  entries are `./`-prefixed and the root `./` entry exists); `ustar-prefix` (one file path 101-255 bytes long with a
  `/` such that the part after it is ≤ 100 bytes and the part before ≤ 155: GNU tar refuses `--format=ustar` otherwise,
  so a bad split fails the build loudly); `layout-extra-root-file` (an extra `NOTES.txt` at the root) and
  `layout-missing-presets` (no `presets/`), both written to `bad/` with `.expected-error` = `E_LAYOUT`.
- `make-bad.js` (committed, Node, no dependencies; `--out <dir>`; writes only under that dir): every malformed archive
  from hand-built 512-byte ustar headers with its own checksum, gzipped with `node:zlib`, one `.expected-error` each:
  absolute path → `E_LISTING_ABSOLUTE`; `..` path → `E_LISTING_DOTDOT`; `C:/x` path → `E_LISTING_DRIVE`; backslash
  path → `E_LISTING_BACKSLASH`; symlink escaping the root (`Alpha.vst3/Contents/x -> ../../../etc`) and
  **`Alpha.vst3/Contents/x -> ../../README.txt`** → `E_LISTING_SYMLINK_ESCAPE`; root-level symlink →
  `E_LISTING_ROOT_SYMLINK`; hard link (typeflag `1`) and character device (`3`) → `E_LISTING_TYPE`; duplicate entry
  and file/dir clash → `E_LISTING_DUPLICATE`; truncated (a tar stream cut mid-header inside a valid gzip) and
  `no-end-block` → `E_LISTING_TRUNCATED`; bad header checksum → `E_LISTING_CHECKSUM`; `pax-malformed`
  (`"1x path=a\n"`) and `base256-size` → `E_LISTING_CORRUPT`; `symlink-dotdot-chain`
  (`l A/b/c/s -> ../../d`, `l A/b/c/t -> s/../../../x`) → `E_LISTING_SYMLINK_FORM`; `symlink-chain`
  (`l Alpha.vst3/Contents/s -> Resources`, `l Alpha.vst3/Contents/t -> s/moduleinfo.json`) and `symlink-self`
  (`l Alpha.vst3/Contents/l -> l`) → `E_LISTING_SYMLINK_CHAIN`; `through-symlink`
  (`l Alpha.vst3/Contents/t -> Resources`, then `f Alpha.vst3/Contents/t/evil`) → `E_LISTING_THROUGH_SYMLINK`.
  Run once: `node tools/feed-contract/fixtures/listings/make-bad.js --out tools/feed-contract/fixtures/listings/bad`.
- `.listing` / `.sha256` of each good archive: produced with `listing.js` from the archive (`toText`, `digest` + LF),
  then cross-checked by eye against `wsl -e tar -tvzf <archive>`; record both outputs' agreement per archive in
  `listings/README.md`, together with every command above, the GNU tar version, the date and which file each made.

**Done when:** L-NODE green for both C4 test files (red first recorded); the 9 good archives, the 2 layout archives
and the 20 malformed archives exist with their sidecar files. **Lane:** L-NODE.

### T007 [P] — KAT archive and signature (FR-023, SC-013 creation half)

**Files (new):** `tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz`,
`tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.minisig`, `tools/feed-contract/fixtures/kat/kat.pub`,
`tools/feed-contract/fixtures/kat/README.md`, `specs/krate-manager-phase1/evidence/make-kat.js`.

**Test first.** The named test (`KAT binds and no secret key is committed`) needs `listing.js` and lands in T013. Here
the check is: before anything exists, `rsign verify -p tools/feed-contract/fixtures/kat/kat.pub tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz`
fails (no files). `rsign --version` must print `rsign2 0.6.7` (installed at `~/.cargo/bin/rsign`, verified 2026-10-30);
if it does not, STOP AND SURFACE.

**Implement `evidence/make-kat.js`** (Node, run once; refuses to run when the `.tar.gz` or `.minisig` already exists, so
a rerun can never replace the committed KAT) per plan §4.7 "KAT": (1) build the archive in WSL with the T006 good-set
flags (`--format=gnu`, `gzip -n -9`) from a WSL `/tmp` tree: `KatPlugin.vst3/Contents/Resources/moduleinfo.json`,
`KatPlugin.vst3/Contents/x86_64-linux/KatPlugin.so` (a few bytes of text), `presets/Default.preset`, `README.txt`,
`CHANGELOG.txt`; (2) `key = fs.mkdtempSync(path.join(os.tmpdir(), 'kat-'))`; (3) `rsign generate -p <key>/kat.pub
-s <key>/kat.key -W`; (4) `rsign sign -s <key>/kat.key -W -t "krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz" <fixture>/KatPlugin-1.2.3-Linux-x64.tar.gz`
(writes `<file>.minisig`, CRLF on Windows, spec External facts); (5) copy `kat.pub` into `fixtures/kat/`;
(6) rewrite the `.minisig` with `toLfMinisig` from `../../../tools/feed-contract/contract.js`; (7) `rsign verify -p
fixtures/kat/kat.pub fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz`, capturing its output; (8)
`fs.rmSync(key, {recursive: true, force: true})` and assert `!fs.existsSync(key)`. It prints every command and output.

**Implement `kat/README.md`:** the exact commands, rsign2 version, OS, date, the CRLF→LF normalisation (D7), the
`rsign verify` output on the normalised file, and the line "the temporary key directory `<path>` was deleted after
signing (existsSync false)".

**Done when:** the `rsign verify` command above exits 0; `node -e` with `parseMinisigText` on the committed
`.minisig` prints algorithm `ED` and the C3 comment; no file whose name ends in `.key` exists under
`tools/feed-contract/` and the temp key directory is gone. **Lane:** none here; T013 adds the named test.

### T008 [P] — Snapshot fixture `snapshot-2026-10-29` (FR-071)

**Files (new):** `tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/{case.json, README.md, tags.txt,
releases.json, live-versions.json}`, `…/snapshot-2026-10-29/plugins/<dir>/version.json` and
`…/plugins/<dir>/installers/windows/setup.iss` for the nine plugin directories,
`specs/krate-manager-phase1/evidence/capture-snapshot.js`.

This is the phase's one network capture. It is an agent action, never a test; tests read only the copies (rule 2).

**Test first.** None runs here (SC-001 in T014 consumes it). Pre-check: the target directory does not exist.

**Implement `evidence/capture-snapshot.js`** (Node, run once, refuses to overwrite an existing snapshot directory):
- for each `plugins/*/version.json` (nine; `plugins/shared` has none) copy the stored blob bytes with
  `git show HEAD:plugins/<dir>/version.json` and `git show HEAD:plugins/<dir>/installers/windows/setup.iss` (blob
  bytes are LF, so the copies are the same on every OS; the working tree is CRLF under `core.autocrlf=true`);
- `git ls-remote --tags origin` → strip each line's `<sha>\trefs/tags/` prefix, drop every `^{}` peel line,
  de-duplicate, keep input order, one bare tag per line, LF, final LF → `tags.txt`; the exact Node code that does this
  goes verbatim into the README;
- `gh release list --limit 200 --json tagName,isPrerelease,isDraft,publishedAt` → `releases.json` (as emitted);
- `curl -sL https://rolandzwaga.github.io/krate-audio/versions.json` → `live-versions.json` (raw bytes);
- `case.json`: `{"group": "clean", "modes": ["v2", "legacy"], "repo": "rolandzwaga/krate-audio", "exit": 0}`;
- README: capture date, commands, counts (tags, releases, prereleases, drafts, plugins in the live file).

**STOP AND SURFACE** (status `blocked`, nothing committed to the directory) if the capture differs from the spec's
recorded real data (spec "Real data read this session": 88 tags, 88 releases with the same names, no prerelease, no
draft, nine plugins in the live file with v1 keys only), because SC-001's name and the snapshot directory name pin
that data; the spec row must be amended first.

**Done when:** the files exist, counts match, `releases.json` and `live-versions.json` parse as JSON, nine AppId lines
exist in the nine `setup.iss` copies (`grep -c '^AppId={{'` = 1 each). **Lane:** none (data).

### T009 [P] — The three JSON Schemas (FR-001 to FR-012, FR-015, FR-016, PD-1, PD-2, PD-10, PD-14)

**Files (new):** `tools/feed-contract/versions.v2.schema.json`, `tools/feed-contract/versions.v1.schema.json`,
`tools/feed-contract/release.schema.json`.

**Test first.** A compile probe, run with `node -e` from the repo root (record the command in the notes): load
`ajv/dist/2020`, `new Ajv2020({strict: true, allErrors: true})`, `compile` each schema file, then validate:
(1) the minimal valid v2 document `{"schema_version": 2, "plugins": {"Alpha": {"version": "1.2.0", "download_url":
"https://example.com/alpha", "release_notes": "", "tag": "alpha/v1.2.0", "pub_date": "2026-10-29T00:00:00Z",
"channel": "stable", "release_notes_url": "https://github.com/o/r/releases/tag/alpha/v1.2.0"}}}` → valid under v2
and v1; (2) the same with a complete `prerelease` sibling (`tag` `alpha/v1.3.0-beta.1`, `pub_date`, `channel: "beta"`,
`release_notes_url`) that also carries `"version": "1.3.0"` → invalid, and among the errors is one with
`keyword === 'false schema'` and `instancePath === '/plugins/Alpha/prerelease/version'` (PD-1); (3) `{"plugins":
{"Alpha": {"version": 1}}}` → invalid under v1. Before the files exist the probe fails (file not found): that is the
red. The real fixture tests are T011 (C1) and T012 (C2); they may correct these files.

**Implement** exactly per plan §4.1: v2 with the `$defs` table (`core`, `tagStable`, `tagPre`, `pubDate`, `url`,
`signature`, `reservedStem`, `pluginName`, `installName`, `install`, `platform`, `platforms`, `entryCommon`, `entry`,
`sibling`), open objects (D1), `$id` `https://krateaudio.com/schemas/versions.v2.schema.json`; v1 as the compatibility
floor (FR-012) and nothing more; C2 closed at every level, `name` via its own copy of `pluginName`, `assets` with the
enum of the five keys, `required` `windows-x86_64` and `linux-x86_64`, `minProperties`/`maxProperties` 3, the PD-10
per-prefix `patternProperties`, `install` requiring the four names, no `channel` property. Authoring rules (measured,
plan PD-2): every subschema that uses `required` declares the same names in its own `properties` (as `true` when the
constraint sits elsewhere); every subschema using `pattern` or `properties` carries `type`; use `pattern`, never
`format` (no `ajv-formats`, D2). Pattern text must compile both with Ajv's `u` flag and in Rust's `regex` (Phase 5): no
look-around, no backreferences; reserved stems as explicit `[Cc][Oo][Nn]` alternations (PD-14).

**Done when:** the probe passes all three checks with 0 strict-mode errors; the schema files contain none of the nine
plugin names. **Lane:** `node --check` is not applicable to JSON; `node -e "JSON.parse(require('fs').readFileSync(f))"`
for each.

---

## G3 — Contract validation (sequential)

### T010 — `CONTRACT.md` and its test (FR-013, FR-017, FR-018, FR-034, FR-046, PD-17)

**Files:** `tools/feed-contract/CONTRACT.md` (new), `tools/feed-contract/tests/hygiene.test.js` (edit: add one named
test).

**Test first — `hygiene.test.js`, named test `CONTRACT.md documents every code and field`** (read-only). Red because
`CONTRACT.md` does not exist. Asserts:
- (a) for every key of `require('../contract.js').CODES`, `CONTRACT.md` has a table row (a line starting `|`) holding
  the code in backticks and its class exactly as `CODES` spells it (`usage`, `abort`, `degrade`, `abort-or-degrade`
  or `none`);
- (b) the exit-code list names `0`, `1`, `2` and `3` each with its meaning;
- (c) a JSON walk of `versions.v2.schema.json` (`properties`, `patternProperties`, `additionalProperties`, `$ref` into
  `$defs`, expanded to dotted paths with placeholders: `plugins.<Name>.version`, `plugins.<Name>.platforms.<key>.payload.sha256`,
  `plugins.<Name>.prerelease.tag`, `manager.latest_json_url`, …) yields a path set, and every member appears in a row of
  the C1 field table (section 2);
- (d) these eight headings exist exactly: `## 1. Scope and readers`, `## 2. C1: versions.json schema_version 2`,
  `## 3. Reader rules`, `## 4. C2: release.json`, `## 5. C3: trusted comments and the known-answer fixture`,
  `## 6. C4: Linux archive layout and canonical listing`, `## 7. gen-feed: modes, rules, codes and exit codes`,
  `## 8. Fixture signatures`;
- (e) section 4 states the `min_os` departure (FR-016) and "no `channel`" (FR-018); section 6 states that Windows and
  macOS have no archive layout (FR-034) and the Phase 8 obligations (same rules on the extracted archive plus a physical
  `realpath` / `std::fs::canonicalize` containment check); section 3 names `E_INSTALL_DUPLICATE` and its case folding.

**Implement `CONTRACT.md`** with the eight sections of plan §4.4: C1 field table (path · type · required · rule ·
writer and input · reader), reader rules (ignore unknown keys; native key then `darwin-universal`; manager v1 ignores
`prerelease`; asset name = URL's last segment, D8; install uniqueness, case-folded for three fields; v1-only or
`definedPlatformKeys` empty = "not available through the manager"; no Windows reserved names), C2 summary and FR-017
cross-field rules, C3 grammar, binding rule, D7 text form and how the KAT was made (from `kat/README.md`), C4 grammar,
D3, path and symlink rules in check order including PD-12, digest, Phase 8 obligations, gen-feed modes, the Q9 rule,
the PD-4 CHANGELOG cut, the plan §3.2 code table (one row per `CODES` key, with its class, as amended by T001) and the
exit codes 0/1/2/3, and section 8 "Fixture signatures are structurally valid only (zero-filled blobs); the KAT is the
only cryptographic fixture". No serial test flags anywhere in the text (SC-019).

**Done when:** L-NODE green for `hygiene.test.js` (red first recorded).

### T011 — C1 validation and the feed fixtures (FR-001 to FR-014, FR-055, SC-005, SC-026, PD-3, PD-11, PD-13)

**Files:** `tools/feed-contract/contract.js` (edit), `tools/feed-contract/versions.v2.schema.json` and
`versions.v1.schema.json` (edit only if a fixture proves them wrong), `tools/feed-contract/fixtures/make-fixtures.js`
(edit: add the `feeds` set), `tools/feed-contract/fixtures/feeds/{valid,invalid}/*.json` and `feeds/cases.json`
(generated), `tools/feed-contract/fixtures/README.md` (edit), `tools/feed-contract/tests/c1-feed.test.js` (new).

**Test first — `c1-feed.test.js`.** Red because `validateFeed` is not exported yet and the fixtures do not exist.
- **Named test `C1 feed fixtures`** (SC-005): `C1_RULES` is a constant array holding exactly the rule ids of plan §4.7
  "C1 invalid rows" (every id in the table's right column, prefixed with its `FR-0xx/`), plus the valid ids listed
  under that table. For every `feeds/cases.json` row `{rule, file, schema?, expect, plugin?}`: load the file (a parse
  failure is the finding `{code: 'E_INPUT'}`), run `validateFeed(doc, {schema: schema ?? 'v2'})`, and assert per plan
  §3.1: `accept` → valid, and for v2 rows also valid under v1 (FR-012); `accept-without-platforms` → as accept, the row
  has a `plugin` present in the document, and `definedPlatformKeys` is `[]` for that entry and its sibling;
  `{error: X}` → invalid and `X` (a code, or the pair `"<instancePath> <keyword>"`) is among the findings. Then assert
  every `C1_RULES` error id has ≥ 1 error row, every row's `rule` is in `C1_RULES`, every FR-055 ★ category is present,
  and the row count is ≥ 29.
- **Named test `feed rejects shared install facts`** (SC-026): the four duplicate documents (`dup-vst3_bundle`,
  `dup-au_component`, `dup-uninstall-key`, `dup-pkg-id`) each give `E_INSTALL_DUPLICATE` with a message naming the field
  and both plugin keys; `dup-vst3_bundle-casefold` (`Alpha` with `Alpha.vst3`, `ALPHA` with `ALPHA.vst3`) gives
  `E_INSTALL_DUPLICATE` on `vst3_bundle` (PD-13); the full-v2 and mixed valid documents pass; `FR-014/sibling-same-install`
  (an entry and its own sibling with identical `install`) passes; v1-only entries never appear in a finding; and
  `CONTRACT.md` (read-only) contains `E_INSTALL_DUPLICATE`.

**Implement in `contract.js`:** compile the three schemas once at module load with `new Ajv2020({strict: true,
allErrors: true})` so a strict-mode violation fails every test that requires the module (PD-3); `validateFeed(doc,
{schema})` returning `{valid, errors}` per plan §3.1: Ajv findings as `{code: 'E_SCHEMA', instancePath, keyword,
message}`, then for v2 the code checks: each platform `signature` through `parseMinisigText` (`E_SIGNATURE_TEXT`, CR
anywhere included) and its trusted comment through `parseTrustedComment` (`E_TC_*`), `checkInstallValues` per `install`
(`E_INSTALL_VALUE`), and `findInstallDuplicates` (`E_INSTALL_DUPLICATE`); `validateRelease(doc)` (Ajv only, used by
T012). Export them.

**Implement the `feeds` set in `make-fixtures.js`:** a full-v2 base document (five platform keys across plugins
`Alpha`, `Bravo`, `Charlie`, a Membrum-like `preset_subdir: "Kits"`, synthetic `fixtureSignature` values whose
comments bind each asset, real SHA-256 of stated synthetic contents) and one `invalid/<id-slug>.json` per §4.7 row with
exactly one change; the valid documents (`v1-only`, `full-v2`, `mixed`, `prerelease-suffixed`, `prerelease-plain`,
`notes-14000` with a `notes_markdown` over 14,000 bytes, `tenth-plugin`, `unknown-platform-key` with
`expect: "accept-without-platforms"`, `unknown-extra-keys`, `sibling-same-install`); and `cases.json`. Regenerate with
`--out tools/feed-contract/fixtures`, confirm `--check` exits 0. Append the `feeds/` section to `fixtures/README.md`.

**Done when:** L-NODE green (red first recorded); `make-fixtures.js --check` exits 0.

### T012 — C2 validation and the release fixtures (FR-015 to FR-018, FR-056, SC-006, SC-022a, PD-10)

**Files:** `tools/feed-contract/contract.js` (edit), `tools/feed-contract/release.schema.json` (edit only if a fixture
proves it wrong), `tools/feed-contract/fixtures/make-fixtures.js` (edit: add the `release` set and `require('../listing.js')`),
`tools/feed-contract/fixtures/release/{valid,invalid}/*.json`, `release/cases.json` (generated),
`tools/feed-contract/fixtures/README.md` (edit), `tools/feed-contract/tests/c2-release.test.js` (new).

**Test first — `c2-release.test.js`, named test `C2 release.json fixtures`** (SC-006). Red because
`checkReleaseManifest` does not exist. `C2_RULES` is a constant array holding exactly the ids of plan §4.7 "C2 invalid
rows" (FR-015, FR-016 including the PD-10 `format-*` and `install-missing-*` rows, FR-017). For every
`release/cases.json` row: run `validateRelease(doc)` and then `checkReleaseManifest(doc, {repo: 'rolandzwaga/krate-audio'})`;
`accept` rows pass both; error rows throw (or report) the expected code (`E_MANIFEST_SCHEMA` for Ajv failures through
`checkReleaseManifest`). Assert every `C2_RULES` id has ≥ 1 error row and no row names an id outside the list. Assert
the valid rows include one per macOS key variant (`darwin-aarch64`, `darwin-x86_64`, `darwin-universal`), one without
`min_os` on `windows-x86_64`, one prerelease-suffixed version and one plain three-part version, and that **no valid
manifest has a `channel` key** while the `channel-stable` and `channel-beta` rows fail with `E_MANIFEST_SCHEMA`
(SC-022a). The "projection" part (C2 ⊇ C1) needs `assembleEntry` and is added by T015; nothing here refers to it.

**Implement `checkReleaseManifest(doc, {repo})`** in `contract.js` in the plan §4.2 order (shape → `checkInstallValues`
→ `checkUrl` per asset → `parseMinisigText` per asset → Ajv (`E_MANIFEST_SCHEMA`) → tag, asset-name, `vst3_bundle`,
`au_component` naming rules (`E_MANIFEST_SCHEMA`) → per-tag URL (`E_URL_NOT_PER_TAG`) → trusted comment equals
`formatTrustedComment(name, version, key, asset.name)` (`E_TC_*` / `E_TC_BINDING`) → `payload`:
`checkCanonicalListing`, `files === list.length` (`E_LISTING_COUNT`), `sha256 === digest(list)` (`E_LISTING_DIGEST`) →
`checkLinuxLayout(list, {vst3Bundle: install.vst3_bundle})` (`E_LAYOUT`)). It throws the first failure as a
`ContractError` with `plugin`, `file` (when given) and `field`.

**Implement the `release` set** in `make-fixtures.js`: valid manifests with real C4 payloads (lines from
`listingFromEntries` over a stated synthetic `Alpha.vst3` tree, `digest`), synthetic asset SHA-256 of stated contents,
`fixtureSignature` text whose comment binds each asset, and one invalid file per §4.7 row with one change (the
`list-not-canonical` row reverses `list` and recomputes `files` and `sha256` so only canonicality fails). Regenerate,
`--check` exits 0, append the `release/` section to `fixtures/README.md` (note: the builder also requires
`../listing.js`, a small extension of plan §4.7's "only `node:` and `../contract.js`").

**Done when:** L-NODE green (red first recorded); `make-fixtures.js --check` exits 0.

### T013 — KAT listing and the KAT tests (FR-023, FR-024, SC-008 KAT part, SC-013)

**Files:** `tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.listing` and `….sha256` (new),
`tools/feed-contract/tests/c4-listing-tar.test.js` (edit), `tools/feed-contract/tests/c3-trusted-comment.test.js`
(edit), `tools/feed-contract/fixtures/README.md` (edit).

**Test first.**
- `c4-listing-tar.test.js`, inside `C4 listings from tar headers`, add subtest `KAT archive`: `toText(listingFromTarGz(kat))`
  equals `KatPlugin-1.2.3-Linux-x64.tar.gz.listing`, `digest` equals the `.sha256`, and `checkLinuxLayout(lines,
  {vst3Bundle: 'KatPlugin.vst3'})` passes. Red: the sidecar files do not exist.
- `c3-trusted-comment.test.js`, new named test **`KAT binds and no secret key is committed`** (SC-013):
  `parseMinisigText` on the committed `.minisig` gives algorithm `ED` and a trusted comment that `checkBinding`
  accepts for `{name: 'KatPlugin', version: '1.2.3', platform: 'linux-x86_64', asset: 'KatPlugin-1.2.3-Linux-x64.tar.gz'}`;
  the `.minisig` bytes contain no CR; a local `findSecretKeyFiles(root)` implementing FR-024 rules (a) name ends `.key`,
  (b) a line matching `^untrusted comment: (rsign|minisign) encrypted secret key`, (c) a line matching
  `^untrusted comment: .*secret key\r?$` whose next line base64-decodes to bytes with `Ed` at 0-1 and `B2` at 4-5,
  scanning every file under `tools/feed-contract/` recursively, returns `[]`; it does not flag the KAT `.minisig` (its
  untrusted comment says `signature from rsign secret key` and its blob starts `ED`); every `signature` value in every
  `fixtures/feeds/**` and `fixtures/release/**` JSON, written to a temp file, is not flagged; three synthetic files in a
  temp dir are each flagged by exactly their rule: `x.key` (a), a file whose first line is
  `untrusted comment: rsign encrypted secret key` (b), a file with `untrusted comment: some secret key` followed by
  `base64("EdScB2" + 100 filler bytes)` (c). Red: the test is new; it fails first if `findSecretKeyFiles` is a stub
  returning `[]` for the synthetic files (record that run).

**Implement** the two sidecar files with `listing.js` (`toText` + LF digest file), then the tests' helper. Append the
`kat/` section to `fixtures/README.md`.

**Done when:** L-NODE green (red first recorded).

---

## G4 — Generator (sequential)

### T014 — `gen-feed.js` legacy mode, CLI shell and git order (FR-040, FR-047, FR-048, SC-001, SC-004)

**Files:** `tools/gen-feed.js` (new), `tools/feed-contract/tests/helpers/fixtures.js` (new),
`tools/feed-contract/fixtures/make-fixtures.js` (edit: add the `git-versionsort` set),
`tools/feed-contract/fixtures/git-versionsort.json` (generated),
`tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/expected.legacy.json` (golden),
`tools/feed-contract/fixtures/README.md` (edit), `tools/feed-contract/tests/gen-feed-legacy.test.js` (new),
`tools/feed-contract/tests/git-versionsort.test.js` (new).

**Test first.**
- `gen-feed-legacy.test.js`, named test **`gen-feed legacy reproduces the 2026-10-29 live feed`** (SC-001):
  `buildLegacyFeed({plugins: readPluginsDir(<snapshot>/plugins, {withSetupIss: false}), tagsText: <tags.txt>})` is
  `Buffer.equals` to `live-versions.json`; on failure print the first differing line. Also: `main(['--legacy',
  '--plugins-dir', …, '--tags', …, '--out', <tmp>/v.json], io)` returns 0 and the written bytes equal the same.
- `git-versionsort.test.js`, named test **`legacy tag order matches git versionsort`** (SC-004): one temp repo
  (`fs.mkdtempSync`), `GIT_CONFIG_GLOBAL` and `GIT_CONFIG_SYSTEM` set to two empty files in the temp dir,
  `GIT_CONFIG_NOSYSTEM=1`; `git init`; `git -c user.name=t -c user.email=t@t commit --allow-empty -m x`; all corpus tags
  created with one `git update-ref --stdin` (`input` option); then per set `git tag -l "<p>/v*" --sort=-v:refname`,
  first line, compared with the tag `buildLegacyFeed` picks for a one-plugin input whose `name` lower-cases to `<p>`.
  Every spawn has `cwd` = the temp repo. The corpus (`git-versionsort.json`, ≥ 20 sets) covers multi-digit parts,
  `-rc1`, `-beta.1`, `-beta.10`, leading zeros (`01`, `001`), unrelated tags, and a plugin prefix that is a prefix of
  another (`alpha/v*` beside `alphabet/v*`). On failure print both orders.
Red: `gen-feed.js` does not exist.

**Implement `tools/gen-feed.js`** (plan §4.5.1, §4.5.3, §4.5.5): `#!/usr/bin/env node`, header comment, the usage
lines of plan §4.5.1 (the v2 line is printed now; its mode lands in T015), hand-loop argument parsing (`E_USAGE`, exit 2,
usage on stderr, nothing written for an unknown flag, missing value, repeated flag or two modes), `main(argv, io,
hooks = {})`, the script entry `if (require.main === module) process.exitCode = main(process.argv.slice(2), process);`,
`readPluginsDir`, `buildLegacyFeed` (the four steps of §4.5.3, including the `E_INPUT` tag-line rules), `gitVersionCompare`
as an **independent** implementation of the documented `strverscmp` semantics (plan §1.2 and §4.5.3; never copy git's
or glibc's GPL code), `projectV1` and the `--project-v1 <feed>` CLI (stdout only; `E_INPUT` on bad JSON or a missing
`plugins` object), and `writeAtomic` (temp file `.<base>.<pid>.<8 hex>.tmp` in the `--out` directory, `renameSync`,
unlink on failure, `E_INPUT` naming `--out`). Exports per plan §4.5.1. No plugin name in the file.

**Implement `tests/helpers/fixtures.js`** (read-only, scanned, no exemption): `FIXTURES` root path; `readJson(rel)`;
`loadCase(name)` → `{dir, caseJson, pluginsDir, releasesPath, tagsPath, manifestsDir | null}`; `runMain(argv, hooks)`
→ `{code, stdout, stderr}` with captured `io`; `v2Argv(caseInfo, outPath, emptyManifestsDir)` (a case without a
`manifests/` directory gets an empty `fs.mkdtemp` dir made **by the test** and passed in, because git stores no empty
directory). It requires `../../../gen-feed.js` lazily.

**Implement the `git-versionsort` set** in `make-fixtures.js`; regenerate; `--check` exits 0. Write
`snapshot-2026-10-29/expected.legacy.json` with `main --legacy` and confirm it equals `live-versions.json` byte for byte.

**Done when:** L-NODE green for both files and the whole set (red first recorded). If SC-001 is red because the live
file was made from a different tag set (not a code defect), STOP AND SURFACE with the first differing line.

### T015 — `gen-feed.js` v2 mode, clean and prerelease cases (FR-041 to FR-045, FR-049 shape, SC-002, SC-003, SC-011, SC-012, PD-10, PD-16)

**Files:** `tools/gen-feed.js` (edit), `tools/feed-contract/fixtures/make-fixtures.js` (edit: gen-feed `clean` and
`prerelease` case inputs), `tools/feed-contract/fixtures/gen-feed/<case>/**` for the cases below (inputs generated;
goldens `expected.v2.json`, `expected.legacy.json`), `tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/expected.v2.json`,
`tools/feed-contract/fixtures/README.md` (edit: golden review record), `tools/feed-contract/tests/gen-feed-v2.test.js`
(new), `tools/feed-contract/tests/c2-release.test.js` (edit: projection part).

**Cases** (`case.json` per plan §4.7; every case with a manifest carries its plugins' `installers/windows/setup.iss`
with a fixture AppId; `repo` `rolandzwaga/krate-audio`):
- group `clean`: `clean-basic` (Alpha with manifest; Bravo without; Charlie with no release; stable versions `1.9.0`,
  `1.10.0`, `2.0.0`; non-plugin tags `v0.8.0` and `manager/v0.1.0`), `clean-identifier` (`version.json` with
  `identifier`; manifest `macos_pkg_id` equals it), `clean-changelog` (Alpha's CHANGELOG has the section, Bravo's lacks
  it), `clean-long-section` (Alpha's section for its version over 14,000 bytes beside two other sections, SC-027),
  `clean-preset-subdir` (`preset_subdir: "Kits"` in `version.json` and manifest), `clean-version-ahead`
  (`version.json` `1.3.0`, release `1.2.0`, manifest `preset_subdir` and `macos_pkg_id` differ from `version.json`:
  check 1 skipped, SC-024b), `tenth-plugin` (ten plugins, one an invented name), `manager-block`
  (`managerLatestUrl: "https://rolandzwaga.github.io/krate-audio/manager/latest.json"`), `pre-plain-promoted`
  (stable `1.1.0` and `1.2.0` with `isPrerelease: false` and the **same manifest bytes** as `pre-plain`), and the
  snapshot (T008);
- group `prerelease` (each with `legacyExpect` = the SC-003 legacy column): `pre-newer` (`1.2.0-beta.1` over `1.1.0`;
  it also carries a manifest for the sibling tag `alpha/v1.2.0-beta.1` whose `install.preset_subdir` differs from
  `version.json`, which check 1 must skip because the versions differ, and a `CHANGELOG.md` with sections for both
  `1.1.0` and `1.2.0-beta.1`; SC-024c and SC-027 read it in T016),
  `pre-same-core` (`1.1.0-beta.1` beside `1.1.0`), `pre-lower-core` (`1.0.0-beta.1` beside `1.1.0`), `pre-only`
  (prereleases only), `draft-above` (a non-prerelease draft above the newest published stable), `pre-plain` (`1.2.0`
  with `isPrerelease: true`, stable `1.1.0`, with a manifest for `alpha/v1.2.0`).

**Test first — `gen-feed-v2.test.js`.** Red: v2 mode is not implemented.
- **`v1 projection of v2 equals legacy`** (SC-002): one subtest per `clean` case (≥ 8, snapshot included):
  `projectV1(buildFeed(inputs).doc)` is byte-identical to `buildLegacyFeed({plugins, tagsText: tagNames.join('\n') + '\n'})`.
- **`Q9 prerelease never moves the v1 version`** (SC-003): one subtest per `prerelease` case asserting exactly the
  SC-003 table: the v2 `version`, the sibling's presence, `tag` and `channel: "beta"`, the entry's `channel: "stable"`,
  the legacy pick (`legacyExpect`), and byte equality of projection and legacy for `pre-lower-core` only; `pre-only`'s
  plugin is absent from `plugins`. Plus a subtest `SemVer 2.0.0 precedence`: `compareSemVer` orders
  `1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0`
  (https://semver.org/spec/v2.0.0.html §11) and `1.9.0 < 1.10.0 < 2.0.0`.
- **`release_notes is always empty`** (SC-011): every non-reject case in legacy, v2 and projection output: every
  `plugins.*.release_notes === ""`.
- **`gen-feed is deterministic`** (SC-012): two `main` runs of every clean and prerelease case into two temp files are
  byte-identical; a temp copy of `clean-changelog` with CRLF `CHANGELOG.md` and `version.json` gives the same bytes as
  the LF original; every case's output equals its committed `expected.v2.json` / `expected.legacy.json`.
- In `c2-release.test.js`, inside `C2 release.json fixtures`, add part **`projection`**: for every valid C2 row,
  `assembleEntry` (from `gen-feed.js`) on a plugin input whose `version.json` matches the manifest yields an entry that
  passes `checkAssembledEntry` (C2 ⊇ C1, plan §4.1).

**Implement v2 mode** in `gen-feed.js` per plan §4.5.2 and §4.5.4 steps 1-7: input validation (abort class:
`E_PLUGIN_NAME` via `isPluginName`, `E_V1_TYPE`, `checkUrl` on `version.json` `url` as abort, `E_DUPLICATE_PLUGIN`
case-folded, `releases` row shapes and unique `tagName`s, `--repo` pattern → `E_INPUT`); release selection (the Q9 rule,
drafts dropped, `E_TAG_FORMAT`, `pub_date` pattern → `E_INPUT`, stable = highest non-prerelease, sibling = highest
prerelease strictly above stable, no stable → omitted); manifest indexing (step 3, with the Q-A branch recorded by T001);
`assembleEntry` (FR-043 key order, `release_notes_url`, `channel` from `isPrerelease` only, `notes_markdown` via
`cutChangelogSection` per PD-4, `checkReleaseManifest`, check 1 gated on `versionJson.version === manifest.version`,
check 2 via `extractAppId` (`/^AppId=\{\{([0-9A-F-]{36})\}[ \t]*$/m` on CRLF-normalised text), `platforms` minus
`name` and `payload.list`, keys sorted bytewise, `install` verbatim, then the `checkAssembledEntry` backstop); export
`checkAssembledEntry` from `contract.js` (it validates `{schema_version: 2, plugins: {[name]: entry}}` and throws
`E_MANIFEST_SCHEMA` with the first finding's `instancePath`); uniqueness (step 5); the document with optional `manager`
(`--manager-latest-url` must pass `checkUrl` and end `/manager/latest.json`, else `E_INPUT`); the self-check (step 7,
`E_OUTPUT_SCHEMA`) with `hooks.beforeSelfCheck?.(doc)` immediately before it; `main` maps thrown codes to exit 1 and
non-empty `warnings` to exit 3 through `CODES`, and prints WARN lines as `gen-feed: WARN <CODE>: <message>`.

**Goldens.** Generate each case's `expected.v2.json` and `expected.legacy.json` with `main`, then review each field by
field against FR-043 (key order, `release_notes_url`, `channel`, `platforms` without `name` and `list`, sibling shape)
and record "reviewed 2026-…, by <agent label>, case: …, checked: …" per case in `fixtures/README.md` (plan §4.7
"Goldens": goldens add byte-stability; correctness is asserted structurally). Regenerate the builder output, `--check`
exits 0.

**Done when:** L-NODE green for `gen-feed-v2.test.js`, `c2-release.test.js` and the whole set (red first recorded).

### T016 — Rejections and degrades (FR-046, FR-049, SC-010, SC-022 to SC-025, SC-027, PD-8, PD-13, PD-14, PD-16)

**Files:** `tools/gen-feed.js` (edit, only for defects the new tests expose), `tools/feed-contract/fixtures/make-fixtures.js`
(edit: `reject` and `degrade` case inputs), `tools/feed-contract/fixtures/gen-feed/<case>/**` for the cases below
(goldens: `expected.error` = the exact first stderr line plus LF for reject cases; `expected.v2.json` for degrade cases),
`tools/feed-contract/fixtures/README.md` (edit), `tools/feed-contract/tests/gen-feed-errors.test.js` (new).

**Cases.**
- group `reject` (`exit: 1`, `error`): `reject-v1-type` (non-string `url`) `E_V1_TYPE`; `reject-url-latest` (`url` with
  `/releases/latest/`) `E_URL_LATEST`; `reject-url-http` (`http://` `url`) `E_URL_NOT_HTTPS`; `reject-tag-suffix`
  (non-prerelease `alpha/v1.2.0-rc1`, SC-022d) and `reject-tag-ten-digits` (`alpha/v4294967296.0.0`) `E_TAG_FORMAT`;
  `reject-plugin-name` (`name: "alpha"`) and `reject-reserved-name` (`name: "Nul"`) `E_PLUGIN_NAME`;
  `reject-duplicate-plugin` (two dirs, `name: "Alpha"` both) and `duplicate-plugin-case` (`Alpha` / `ALPHA`)
  `E_DUPLICATE_PLUGIN`; `reject-input-ls-remote` (modes `["legacy"]`, `tags.txt` = raw `<sha>\trefs/tags/<tag>` lines and
  a `^{}` line) and `reject-input-releases` (malformed `releases.json`) `E_INPUT`; `reject-abort-plus-degrade` (an
  `E_V1_TYPE` plugin beside a plugin whose manifest fails) `E_V1_TYPE`. Under the Q-A ABORT ruling also
  `reject-manifest-unparseable` and `reject-manifest-duplicate-tag` (`E_INPUT`).
- group `degrade` (`exit: 3`, `warn` list; each beside a healthy second plugin): check 1 `degrade-preset-subdir` and
  `degrade-pkg-id` (`version.json` version equal to the manifest's); check 2 `degrade-uninstall-key`,
  `degrade-no-appid`, `degrade-no-setup-iss`; `E_INSTALL_VALUE` `degrade-install-slash`, `degrade-install-backslash`,
  `degrade-install-dotdot`; `E_SIGNATURE_TEXT` `degrade-signature-text`; `E_TC_FIELDS` / `E_TC_NAME` / `E_TC_VERSION`
  / `E_TC_PLATFORM` / `E_TC_ASSET` / `E_TC_BINDING` one case each; `E_LISTING_DOTDOT`, `E_LISTING_NOT_CANONICAL`,
  `E_LISTING_COUNT`, `E_LISTING_DIGEST` one case each, and (compliance remediation 2026-10-30, FR-059 "one case per
  degrade-class code the generator can raise") `E_LISTING_PATH`, `_ABSOLUTE`, `_DRIVE`, `_BACKSLASH`, `_DUPLICATE`,
  `_ROOT_SYMLINK`, `_SYMLINK_FORM`, `_SYMLINK_ESCAPE`, `_SYMLINK_CHAIN`, `_THROUGH_SYMLINK` one case each
  (`degrade-listing-<code>`; `_TYPE` / `_CHECKSUM` / `_TRUNCATED` / `_CORRUPT` cannot come from a listing line); `degrade-layout` (`E_LAYOUT`); `E_URL_NOT_HTTPS`,
  `E_URL_LATEST`, `E_URL_NOT_PER_TAG` in a `release.json`, one case each; `degrade-sibling-manifest` (the sibling's
  manifest fails: entry and sibling both lose `platforms` and `install`); `degrade-c2-missing-pkg-id` and
  `degrade-c2-wrong-format` (`E_MANIFEST_SCHEMA`, plan §4.7, exit **3**); the `E_INSTALL_DUPLICATE` cases **as ruled
  in Q-C** (recommended: `degrade-dup-uninstall-key` — two `setup.iss` with one copied AppId and matching manifests —
  and `degrade-dup-pkg-id` — two `version.json` with the same `identifier` — both plugins degrade, two WARN lines; plus
  `degrade-copied-bundle-name`, where Bravo's manifest carries `Alpha.vst3` / `Alpha.component`: only Bravo degrades,
  `E_MANIFEST_SCHEMA`); under the Q-A DEGRADE ruling also `degrade-manifest-unparseable` (WARN
  `{code: E_MANIFEST_SCHEMA, plugin: null, file: "manifests/broken.json"}`) and `degrade-manifest-duplicate-tag`
  (`E_MANIFEST_DUPLICATE` naming both files).

**Test first — `gen-feed-errors.test.js`** (all runs in-process through `fixtures.js` `runMain`; outputs in temp dirs).
Red: the case inputs do not exist yet (write the test, run it red, then generate the cases).
- **`gen-feed rejects`** (SC-010): one subtest per reject case: exit 1; stderr's first line equals `expected.error` and
  starts `gen-feed: <CODE>:`; a pre-existing `--out` file written by the test stays byte-identical; no `*.tmp` remains in
  the out dir. A usage error (unknown flag) exits 2, prints the usage line and writes nothing. PD-16 subtests: three bad
  `--manager-latest-url` values (`http://…/manager/latest.json`, `https://…/releases/latest/download/manager/latest.json`,
  `https://…/manager/feed.json`) on `clean-basic` → exit 1, `E_INPUT`, nothing written; `E_OUTPUT_SCHEMA`:
  `runMain(argv, {beforeSelfCheck: (doc) => { doc.plugins.Alpha.release_notes = 'x'; }})` on `clean-basic` → exit 1,
  first line `gen-feed: E_OUTPUT_SCHEMA:`, pre-existing `--out` byte-identical, no `*.tmp`; `--project-v1` on
  `clean-basic`'s `expected.v2.json` → exit 0 and stdout equals that case's `expected.legacy.json`; on malformed JSON and
  on `{"schema_version": 2}` → exit 1, `E_INPUT`, empty stdout; `assembled entry failing C1 degrades`:
  `checkAssembledEntry` on a hand-built entry with a `darwin-universal` platform and no `au_component` throws
  `E_MANIFEST_SCHEMA`.
- **`channel comes from the release list only`** (SC-022 b-d): `pre-plain` exits 0 with the entry `channel: "stable"`
  and sibling `tag` `alpha/v1.2.0`, `channel: "beta"`; `pre-plain-promoted` exits 0 with no WARN, entry `version`
  `1.2.0`, `channel: "stable"`, `platforms` from the manifest, no sibling, and the manifest file's SHA-256 is equal before
  and after the run and equal to `pre-plain`'s manifest; `reject-tag-suffix` exits 1 with `E_TAG_FORMAT`.
- **`gen-feed degrades a bad manifest`** (SC-023): one subtest per degrade case: exit 3; the written `--out` passes
  `validateFeed` v2 and v1; the degraded entry (and its sibling) has no `platforms` and no `install` and is byte-equal
  (as `JSON.stringify` of the entry) to the entry from an in-process `buildFeed` with that plugin's manifest(s)
  removed; the healthy entry equals the same entry in `expected.v2.json`; stderr holds exactly one
  `gen-feed: WARN <CODE>:` line per `warn` row, naming the plugin (or, for `plugin: null`, the file); no `*.tmp`
  remains. A clean case returns 0 with no WARN line; `--legacy` and `--project-v1` on every degrade case never return 3.
- **`install cross-check is gated on version`** (SC-024 a-d): `degrade-preset-subdir` and `degrade-pkg-id` → exit 3,
  `E_MANIFEST_MISMATCH` naming the field; `clean-version-ahead` → exit 0, no WARN, entry `install` deep-equal to the
  manifest's; `pre-newer` (its sibling manifest is prerelease-suffixed, never equal to the three-part `version.json`
  version) exits 0 with no WARN and the sibling's `install` deep-equal to that manifest's; `clean-version-ahead` with a wrong `windows_uninstall_key` (a temp copy) → exit 3,
  `E_MANIFEST_MISMATCH`.
- **`windows_uninstall_key equals the setup.iss AppId`** (SC-025): `extractAppId` on the nine snapshot `setup.iss`
  copies returns nine distinct `{GUID}` values each matching `^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$`;
  a manifest with its own `{GUID}_is1` passes (`clean-basic`); `degrade-uninstall-key`, `degrade-no-appid`,
  `degrade-no-setup-iss` → exit 3, WARN naming the file; `extractAppId('AppId={{0A1B…}\r\n')` returns the single-brace
  form, and a single-brace `AppId={…}` line returns `undefined`.
- **`notes_markdown is one uncapped section`** (SC-027): on `clean-long-section` the entry's `notes_markdown` is a
  string byte-equal to its version's section (trimmed, LF), ≥ 14,000 bytes, containing neither other section's header;
  a sibling carries its own version's section (`pre-newer` with a CHANGELOG section for `1.2.0-beta.1`); the feeds
  fixture `notes_markdown-type` (array) fails C1; a JSON walk of `versions.v2.schema.json` and `release.schema.json`
  finds no `maxLength` and no `array` type under any `notes_markdown` property.

**Implement:** generate the cases, run each once, write and review the goldens (record in `fixtures/README.md`), fix
any `gen-feed.js` defect the tests expose. `--check` exits 0.

**Done when:** L-NODE green for `gen-feed-errors.test.js` and the whole set (red first recorded).

---

## G5 — C++ guard (sequential)

### T017 — `test_update_checker.cpp`: settings-dir redirect on every `[update]` case and the v2 guard case (FR-050 to FR-052, SC-016, SC-017, SC-021, PD-5)

**Files:** `plugins/shared/tests/test_update_checker.cpp` (edit only; no header, no CMake, no non-test source).

Verified 2026-10-30: the file has 209 lines; `#include <filesystem>` at `:4`; `MockUpdateChecker` at `:15`;
`waitForResult` at `:40`; `kValidJson` at `:53`; `TEST_CASE`s at `:79` (`Iterum` at `:80`, `Ruinae` at `:113`), `:146`
(`Iterum` `:147`), `:181` (`Iterum` `:182`, `dismissVersion` at `:187` and `:200`). `getAppSettingsDirectory` is declared
at `plugins/shared/src/platform/preset_paths.h:33` and reads `APPDATA` / `HOME` / `XDG_CONFIG_HOME` (else `HOME`) with
`std::getenv` at call time (`preset_paths.cpp:53-79`). The MSVC env-read pattern without C4996 is
`plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp:215-227`.

**Safety first (rule 6).** Before editing, building or running anything, record
`stat -c '%Y %s' "$APPDATA/Krate Audio/Iterum/update_state.json"` and the same for `Ruinae` (print `missing` for a missing
file). Never run any `[update]` case before every `TEST_CASE` in the file opens with the guard and its `REQUIRE`s.

**Test first.**
1. Add to the file, per plan §4.8: `#include "platform/preset_paths.h"`, `<optional>`, `<random>`, `<string>`,
   `<cstdlib>` (and `<algorithm>` for `std::mismatch`); in an anonymous namespace `kSettingsEnvVar` (`APPDATA` /
   `HOME` / `XDG_CONFIG_HOME` under `#if defined(_WIN32)` / `#elif defined(__APPLE__)` / `#else`), `readEnv`
   (`_MSC_VER`: `getenv_s` size query, then read; else `std::getenv`), `writeEnv` (`_WIN32`: `_putenv_s(name, v or "")`;
   else `setenv(name, v, 1)` / `unsetenv(name)`), `isUnder(p, root)` (component-wise prefix of `lexically_normal()`
   forms via `std::mismatch`), and `ScopedSettingsDirRedirect` with deleted copy and move, `root()` accessor. **For the
   red run, the constructor creates `root_` but does not yet call `writeEnv`.**
2. Open each of the three existing `TEST_CASE`s with `ScopedSettingsDirRedirect settings;` and
   `REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("Iterum"), settings.root()));` (the `:79` case also
   for `"Ruinae"`), placed before any `MockUpdateChecker` is constructed (PD-5).
3. Add `TEST_CASE("UpdateChecker - feed schema v2 is read as v1", "[update]")` exactly per plan §4.8: `envBefore =
   readEnv(kSettingsEnvVar)`; inner scope with the guard and the `REQUIRE` for `"FeedV2Probe"`; config
   `{.pluginName = "FeedV2Probe", .currentVersion = "1.0.0", .endpointUrl = "https://example.com/versions.json"}`
   (`currentVersion` below the feed's, because `parseResponse` fills the fields only when `remote > current`,
   `update_checker.cpp:246-256` per plan §1.1); inline raw strings `kFeedV1OnlyJson` (`FeedV2Probe` entry
   `{"version": "1.1.0", "download_url": "https://example.com/feedv2probe", "release_notes": ""}`) and `kFeedV2Json`
   (adds `tag`, `pub_date`, `channel: "stable"`, `release_notes_url`, `platforms` with `windows-x86_64` `inno-exe`,
   `darwin-universal` `pkg` + `min_os`, `linux-x86_64` `tar.gz` + `min_os` + `payload`, `install`, top-level `manager`,
   and `prerelease` `{"tag": "feedv2probe/v1.2.0-beta.1", "channel": "beta", …}`); `SECTION("v2 document")` and
   `SECTION("schema_version 99")` (the v2 text with `"schema_version": 2` replaced, asserting exactly one replacement):
   each runs a `MockUpdateChecker` on its document, `REQUIRE(waitForResult(checker))`, and `CHECK`s `updateAvailable`
   (true), `latestVersion == "1.1.0"` (never `"1.2.0-beta.1"`), `downloadUrl`, `releaseNotes == ""` equal to the
   reference result from `kFeedV1OnlyJson`; then `CHECK` that `getAppSettingsDirectory("FeedV2Probe") /
   "update_state.json"` exists and `isUnder` the guard's root; after the inner scope `CHECK(readEnv(kSettingsEnvVar) ==
   envBefore)` (SC-017).
4. Build with L-CPP's build command; then `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests`
   → expected `GATE RED`, every case failing at its first `REQUIRE` (no checker constructed, so nothing written).
   Re-read both `stat` lines: unchanged. Record the red in the notes.

**Implement.** Make the constructor call `previous_ = readEnv(kSettingsEnvVar); writeEnv(kSettingsEnvVar,
root_.string());` and the destructor `writeEnv(kSettingsEnvVar, previous_); std::filesystem::remove_all(root_, ec);`
(no throw). The guard is declared before every checker in its scope, so each checker's destructor joins its worker
(`update_checker.cpp:28-33` per plan §1.1) before the variable is restored. `ScopedSettingsDirRedirect` is a new name
(0 hits in `dsp/ plugins/ tests/`, plan §1.1 ODR check); keep it in the anonymous namespace.

**Done when:** L-CPP: 0 warnings, `GATE GREEN`, and the shard log lists `UpdateChecker - feed schema v2 is read as v1`
with both sections passed; both `stat` lines are identical before and after every run in this task (quote them in the
notes); `node tools/header-consumers.js --tag '[update]'` ends `CONSUMERS OUTSIDE [update]: none`
(`tools/header-consumers.js:95` prints that line). If the shared_tests build exceeds the 10-minute Bash ceiling, stop
the build, leave the code in place, and return done with notes "deferred to the main loop at close: run
`node specs/krate-manager-phase1/evidence/close-lane.js`" (T020 writes it). clang-tidy on this TU is a close-lane step
(main loop), not this task. Note for the main loop: the workflow's own gate after this group runs
`header-consumers.js --tag '[krate-manager]'` (`.claude/workflows/krate-manager-phase.js:602`), which names the whole
`shared_tests`; the change-set scope (CLAUDE.md "Run only the tests the change set can reach") is the `[update]` filter
above, because the only modified shared file is this TU and every case in it carries `[update]`.

---

## G6 — Integration (sequential)

### T018 — `.github/workflows/feed-tools.yml` (FR-060, D9, SC-018, SC-013 CI half, SC-015d)

**Files:** `.github/workflows/feed-tools.yml` (new; a CI workflow, so sequential). Written, never pushed.

**Test first.** L-YAML on the not-yet-existing file fails (no input): the recorded red. The file's content is also
checked by T019's `no serial test flags` and by the "Done when" greps below.

**Implement** exactly plan §4.9 (Q-D: the path filter lists `tools/feed-contract/package.json` / `package-lock.json`, and the install step is `npm ci --prefix tools/feed-contract`): `name: Feed tools`; `on.push.paths` and `on.pull_request.paths` both listing
`tools/gen-feed.js`, `tools/gen-release-manifest.js`, `tools/minisign.js`, `tools/feed-contract/**`, `package.json`,
`package-lock.json`, `.github/workflows/feed-tools.yml` (push on any branch: no `branches` key, D9);
`workflow_dispatch`; `permissions: contents: read`; job `node-test`, `name: node-test (${{ matrix.os }})`,
`strategy.fail-fast: false`, `matrix.os: [ubuntu-latest, macos-latest, windows-latest]`; steps
`actions/checkout@v6`, `actions/setup-node@v4` with `node-version: '24'`, `npm ci`, the rsign cache
(`actions/cache@v5`, paths `~/.cargo/bin/rsign` and `~/.cargo/bin/rsign.exe`, key `rsign2-0.6.7-${{ runner.os }}-${{ runner.arch }}`),
`cargo install rsign2 --version 0.6.7 --locked` when the cache missed, the `node --test` step (`shell: bash`,
`set -o pipefail`, `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" | tee "$RUNNER_TEMP/tap.txt"`,
then an inline `node -e` that reads `process.env.RUNNER_TEMP + '/tap.txt'`, requires a line `# fail 0` and a line
`# tests N` with `N >= 21`, prints `TAP tests=N`, and exits 1 otherwise), `rsign verify (KAT)`
(`rsign verify -p tools/feed-contract/fixtures/kat/kat.pub tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz`),
and `Checkout unchanged` (`shell: bash`, `test -z "$(git status --porcelain)"`). No `secrets.*`, no release, Pages or
network step beyond the installs. The action pins match the repo's existing workflows (plan §1.1 row "Workflows today").

**Done when:** L-YAML exit 0; `grep -c 'secrets\.' .github/workflows/feed-tools.yml` = 0; `grep -n 'test-concurrency\|test-isolation' .github/workflows/feed-tools.yml` prints nothing; L-LINT exit 0.

### T019 — The remaining hygiene tests (SC-014, SC-019, PD-7, SC-015a completeness)

**Files:** `tools/feed-contract/tests/hygiene.test.js` (edit).

**Test first** (each red before its condition holds; record which red was seen: e.g. run `fixtures are in sync` after
touching a fixture byte in a **temp copy**, never in the repo):
- **`generator ships no plugin roster`** (SC-014): `tenth-plugin` yields 10 entries in v2 and legacy; the snapshot
  yields 9; the nine names are read from the snapshot's `version.json` copies (never typed into the test) and a
  case-insensitive search of `tools/gen-feed.js`, `tools/feed-contract/contract.js`, `tools/feed-contract/listing.js`,
  `versions.v2.schema.json`, `versions.v1.schema.json` and `release.schema.json` finds 0 hits.
- **`no serial test flags`** (SC-019): `.github/workflows/feed-tools.yml` and every `*.md` under `tools/feed-contract/`
  contain none of `--test-concurrency=1`, `--test-concurrency 1`, `--test-isolation=none`, `--test-isolation none`.
- **`fixtures are in sync with make-fixtures.js`** (PD-7): `require('../fixtures/make-fixtures.js').check()` (builds into
  an `fs.mkdtemp` dir in-process, under the guard) returns `{ok: true}`; on failure print the first differing file.
- In `tools stay offline` (a): assert that every file of the scan list exists (`tools/gen-feed.js`,
  `tools/feed-contract/contract.js`, `tools/feed-contract/listing.js`, both helpers, `make-fixtures.js`, `make-bad.js`,
  and ≥ 10 test files), so the scan cannot pass on an empty list.

**Done when:** L-NODE green for the whole set; the TAP summary shows `# tests` ≥ 21 (the 24 named tests of plan §7.1
plus subtests) and `# fail 0`; record the count and the wall clock in the notes (SC-019 measurement, no bar).

### T020 — Close lane, scope check, repo lints and the PENDING-CI list (SC-016, SC-019, SC-020, SC-021, FR-070, PD-9)

**Files:** `specs/krate-manager-phase1/evidence/close-lane.js` (new), `specs/krate-manager-phase1/evidence/scope-check.js`
(new), `specs/INDEX.md` (regenerated **only** under the Q-B IN ruling).

**Test first.** `scope-check.js` has `--self-test`: it feeds three synthetic status/diff texts (one clean, one with
`?? tools/stray.js`, one with a rename `R  a -> plugins/iterum/src/x.cpp`) through its filter and asserts clean → no
output, the other two → the offending path printed and exit 1. Run `--self-test` red before the filter exists, then
green.

**Implement `scope-check.js`** (plan §8 step 8; Q-D: `.gitignore` replaces the root `package.json` / `package-lock.json` in the exact list): run `git -c core.quotepath=off status --porcelain --untracked-files=all`
and `git diff --name-only 966ef217..` (84c6aab1 plus the workflow-harness fix, committed on its own; T020 ruling); take each path (after the two status columns; both sides of a `->`); drop the
FR-070 list (exact: `tools/gen-feed.js`, `plugins/shared/tests/test_update_checker.cpp`, `.github/workflows/feed-tools.yml`,
`package.json`, `package-lock.json`, `.gitattributes`; prefixes `tools/feed-contract/`, `specs/krate-manager-phase1/`;
plus `specs/INDEX.md` only under Q-B IN) and the start-of-phase baseline `?? specs/Krate-manager-roadmap.md`; print the
raw and filtered outputs; exit 1 if anything is left. `release.yml` and `plugins/*/src/**` must not appear (SC-020).

**Implement `close-lane.js`** (Node, no dependencies, main loop only; every child process gets a wall-clock ceiling;
raw logs under `F:/tmp/gate/krate-manager-phase1-close/`, a summary of result lines written to
`specs/krate-manager-phase1/evidence/close-lane.log` and printed; last line `CLOSE LANE GREEN` or `CLOSE LANE RED`):
- concurrently, step A: (A1) the L-CPP build command (ceiling 30 min), its warning count by the case-sensitive regex
  `\bwarning\b` and its exit; (A2) `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` with wall
  clock, `# tests`, `# pass`, `# fail` (SC-019 measurement, SC-018 local count); (A3) L-YAML; (A4)
  `node tools/header-consumers.js --tag '[update]'` (last line must be `CONSUMERS OUTSIDE [update]: none`); (A5) L-LINT;
  (A6) `node tools/check-portability.js plugins/shared/tests/test_update_checker.cpp` (GCC syntax check in WSL, "seconds
  per file", `tools/check-portability.js:5-18`, usage `:22-24`); (A7) under Q-B IN, `node tools/gen-specs-index.js --check`;
- after A1 succeeds, step B: the SC-021 `stat` of both real `update_state.json` files, then
  `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests` (ceiling
  15 min; last line `GATE GREEN`; grep the shard logs for `UpdateChecker - feed schema v2 is read as v1`), then the
  `stat` again (must be identical; quote both); beside it, clang-tidy on the one TU:
  `"C:/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja plugins/shared/tests/test_update_checker.cpp`
  (path from `tools/run-clang-tidy.ps1:92-94`; ceiling 15 min, killed and reported RED on timeout; 0 `warning:` lines
  from this TU required; reconfigure `build/windows-ninja` only if a test CMake file changed, and none does);
- last, step C: `node specs/krate-manager-phase1/evidence/scope-check.js`.

**Run in this task (short, ≤ 5 min each):** `scope-check.js --self-test`; L-LINT; `node tools/check-portability.js
plugins/shared/tests/test_update_checker.cpp`; L-YAML; the full L-NODE run; under Q-B IN, `node tools/gen-specs-index.js`
then `--check` (exit 0); `scope-check.js` (must print nothing after filtering). `node tools/lint-plugin-editor-baseline.js`
is not run: no plugin editor changed (CLAUDE.md "Every Plugin Editor Carries the Update Check" applies to editors only).
The close lane itself is a main-loop lane: return done with notes **"deferred to the main loop at close: run
`node specs/krate-manager-phase1/evidence/close-lane.js`"**.

**Done when:** every short command above passes; both evidence scripts pass `node --check`; the PENDING-CI list below
matches the job names in `.github/workflows/feed-tools.yml` and `.github/workflows/ci.yml` (grep each job name).

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

`feed-tools.yml` runs on the user's push of the feature branch (D9). `ci.yml` runs only on a pull request to `main` or a
push to `main` (`ci.yml:3-23`: `branches: [main]` at `:5` and `:15`), so its rows wait for that PR.

| SC | Workflow · job (exact name) | Assertion that closes the row |
|---|---|---|
| SC-004, SC-008, SC-012, SC-018 | `feed-tools.yml` · `node-test (ubuntu-latest)`, `node-test (macos-latest)`, `node-test (windows-latest)` | step `node --test` green; TAP `# fail 0`; `TAP tests=N` with N ≥ 21 and N equal on all three legs and to the local close-lane count |
| SC-009 | `feed-tools.yml` · `node-test (ubuntu-latest)`, `node-test (macos-latest)` | every subtest of `C4 listings from extracted trees` passes on the POSIX branch (system `tar -xzf` into a temp dir; bsdtar 3.5.3 on macOS 26, GNU tar on Ubuntu 24.04, plan §1.2); the Windows leg passes the refusal branch with the same subtest names |
| SC-013 (CI half) | `feed-tools.yml` · all three `node-test (…)` legs | step `rsign verify (KAT)` exits 0 |
| SC-015 (d) | `feed-tools.yml` · all three `node-test (…)` legs | step `Checkout unchanged` prints nothing |
| SC-016, SC-017, SC-021 (macOS, Linux) | `ci.yml` · `test-macos` "macOS tests (shard ${{ matrix.shard }})" (`ci.yml:1404-1405`), `test-linux` "Linux tests (shard ${{ matrix.shard }})" (`ci.yml:1441-1442`); `shared_tests` is built because `plugins/shared/**` (`ci.yml:86`) sets the `shared` output (`ci.yml:623`, `:1127` add the target) | every shard job green, and the shard log that ran `shared_tests` reports `UpdateChecker - feed schema v2 is read as v1` passed (both sections) |
| PD-9 (Q-B IN only) | `ci.yml` · `Generated Artifacts In Sync` (`ci.yml:156`, step at `:172`) | `node tools/gen-specs-index.js --check` exits 0 on the PR |

Every Windows row closes locally: L-NODE and L-CPP results, and the close lane's `close-lane.log` lines.

## Main-loop rulings and fixes at close (2026-10-30)

- **T020 scope ruling, option (a):** the workflow-harness edit (`.claude/workflows/krate-manager-phase.js`, check-bundle
  limited to the bundles a phase builds) is not Phase 1 work. It was committed on its own as `966ef217` on top of
  `84c6aab1`, and `scope-check.js` diffs from `966ef217..`. FR-070 is unchanged.
- **close-lane B4:** the first run read RED with 73 warnings. 71 were the Catch2 macro checks
  `bugprone-chained-comparison` and `cppcoreguidelines-avoid-do-while`, which `tools/run-clang-tidy.ps1:382-386` drops
  for every `tests/` file (`dsp/tests/.clang-tidy` disables the same set). B4 now applies that filter and prints
  `catch2-filtered=N`. The other 2 were real: `readability-use-concise-preprocessor-directives` at
  `test_update_checker.cpp:27` and `:58` in Phase 1's redirect helper. Both now read `#ifdef _WIN32`.
  Re-run: `CLOSE LANE GREEN`, B4 `TU warnings=0 ... catch2-filtered=71` (`evidence/close-lane.log`).
- **Q-D, `ajv` location (compliance FR-060 / FR-062 / SC-018 fail):** the root `package.json` / `package-lock.json` are
  gitignored (`.gitignore:93-94`, unanchored), so `feed-tools.yml`'s `npm ci` had no package on a fresh checkout. User
  ruling: `tools/feed-contract/package.json` (`private`, `ajv` 8.20.0 exact) + lockfile v3, CI `npm ci --prefix
  tools/feed-contract`, `.gitignore` lines anchored to `/package.json` / `/package-lock.json`, `ajv` removed from the local root.
  Spec rows FR-060, FR-062, FR-070, SC-020 amended; `scope-check.js` exact list swaps the root files for `.gitignore`.
