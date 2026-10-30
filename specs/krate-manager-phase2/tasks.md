# Tasks: Krate Manager Phase 2 — Release Assets, Hashes, Signatures and the Backfill

**Spec:** `specs/krate-manager-phase2/spec.md` (Clarifications Q1-Q7, 2026-10-30) · **Plan:** `specs/krate-manager-phase2/plan.md`
(revised after review round 1, PD-1 to PD-28, SI-1 to SI-12)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 2 (lines 335-416); Cross-Cutting Constraints (lines 954-995)
**Status:** DRAFT task list, 2026-10-30, written against `feat/krate-manager` at `1dec384c` (untracked at start:
`specs/krate-manager-phase2/`)
**Lanes in this phase:** pipeline lane only (Node tools and tests, fixtures, three workflow YAML files). There is no
`apps/krate-manager/` yet (Phase 4) and no C++ change (spec FR-082), so there is no app lane (no `cargo`, `tsc`,
`vitest`, Tauri) and no `run-gate.js` / `header-consumers.js` lane in any task.

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks in parallel; every task in it creates only new files and no
  two tasks in it share a file. Every other group is **sequential**: its tasks run one after another in the order listed.
  A task that edits a pre-existing or shared file (`spec.md`, `contract.js`, `CONTRACT.md`, `gen-feed.js`,
  `write-guard.js`, `hygiene.test.js`, `c3-trusted-comment.test.js`, a workflow, a test file another task created) is
  always in a sequential group.
- Each task section is the executor's authoritative instruction. It names the files, the failing test to write first
  (file, test name, assertions), the implementation, and the lane that verifies it. Where a table is too large to copy
  without drift risk, the section names the exact `plan.md` section that holds it; that section is then binding. Where
  this file and the plan differ, this file wins and says why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed, and the task's notes say so.
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.
- No task in this phase is a main-loop lane: the whole `node --test` set ran in seconds at Phase 1 close
  (`specs/krate-manager-phase1/tasks.md:949-950`), and nothing here builds C++, Tauri or a WSL tree. T020 runs the close
  commands itself and records their wall clock.

### Rules every task obeys (a task that breaks one is defective)

1. **Write guard first.** Every new `tools/feed-contract/tests/*.test.js` starts with an optional `'use strict';` line
   followed by `require('./helpers/write-guard.js');` as its first statement (`hygiene.test.js` subtest (b),
   `tools/feed-contract/tests/hygiene.test.js:135-143`). Tests write only under
   `fs.mkdtempSync(path.join(os.tmpdir(), 'km2-<name>-'))` directories, removed in `t.after` / `finally`. Repo fixtures
   are only read; anything a spawned `rsign` / `tar` reads is first copied into the temp dir (the guard requires every
   path argument under the temp root, plan §4.12.2).
2. **No network in tests or tools.** No `http`, `https`, `http2`, `net`, `tls`, `dgram` module and no `fetch(` (not even
   in a comment) in any scanned file (`hygiene.test.js:52-57`). No test calls github.com, api.github.com or the feed;
   `gh` exists only in `backfill-sign.js` `productionDeps`, which tests replace with a fake (spec Safety "Network").
   The only network steps in this phase are agent actions, never tests: `npx --yes yaml@2.9.1` (L-YAML).
3. **Child processes.** Only `tools/backfill-sign.js` and `tools/feed-contract/tests/helpers/external-tools.js` may
   contain the string `child_process` (PD-5; `hygiene.test.js:65-69` flags it in every other scanned file, comments
   included). Every test-side spawn of `rsign` or `tar` goes through `external-tools.js`. Never `exec`, `execSync`,
   `fork` or `shell: true`.
4. **Module rule.** Call `fs.<fn>`, `crypto.<fn>` and `childProcess.<fn>` through the module object at call time, never
   destructured (`hygiene.test.js:59-64`; plan §4 intro: SC-007 subtest 10 wraps `fs.renameSync` and SC-027 (b) wraps
   `crypto.scryptSync` in-process). CommonJS, `'use strict'`, LF line endings, like `tools/gen-feed.js`.
5. **Tool form (FR-076).** Each of the four new tools is a flat file whose first line is `#!/usr/bin/env node`, then a
   header comment (WHY, usage, exit codes, as `tools/gen-feed.js:1-25`), `'use strict'`, `function main(argv, io)`
   returning the exit code, `if (require.main === module) process.exitCode = main(process.argv.slice(2), process);`
   (`tools/gen-feed.js:760`), `module.exports` last. Missing or unknown arguments: a stderr line starting
   `usage: node tools/<file>` and return 2. Tests call `main` in-process (the guard admits no `node` child,
   `tools/feed-contract/tests/helpers/write-guard.js:235`).
6. **One error type.** Expected failures throw `contract.ContractError(code, message, where)`
   (`tools/feed-contract/contract.js:15`); `main` prints `<tool>: <CODE>: <message>` on stderr. No message, stdout or
   stderr line ever contains a secret value, a password, a key byte or a base64 key line (plan §3.1).
7. **No plugin roster in product code (ROSTER).** The four tools and both new workflows' Phase 2 blocks contain none of
   the nine shipped plugin names as a case-insensitive **substring** (`hygiene.test.js:233-249` uses
   `src.toLowerCase().includes(name)`), not even in a comment or an example. Fixture plugins are `KatPlugin` and the
   synthetic `Alphaplug` … `Iotaplug`, `Kappaplug` (plan §4.12.3).
8. **Never touch the real system.** No task writes to a VST3, Components or preset folder, `ProgramData`, the registry,
   `%APPDATA%\Krate Audio\…`, or any path outside the repo's FR-080 paths, `F:/tmp` and an `fs.mkdtemp` directory. No
   task runs an installer, `gh` against GitHub, or the real signing secret.
9. **Keys.** No private key is generated into or committed to the repo (FR-060). Tests make throwaway keys in memory
   (`minisign.generateKeyPair()`) or with `rsign generate -W` into an `fs.mkdtemp` dir that is removed. The only
   committed secret-key file is the user-made `fixtures/phase2/kat-secret/kat.key` (FR-064, Precondition Q-A).
   `tools/feed-contract/keys/krate-signing.pub` is committed by the user, never by an agent (FR-061).
10. **Top-level tests.** A Phase 2 test file holds only the SC-named top-level tests listed in its task (exact names
    from plan §7.1); everything else is a subtest (`t.test`). The final TAP plan is `1..46` (FR-073).
11. **Docs under `tools/feed-contract/`** (`keys/README.md`, `kat-secret/README.md`, `CONTRACT.md`) never contain
    `--test-concurrency=1`, `--test-concurrency 1`, `--test-isolation=none` or `--test-isolation none`
    (`hygiene.test.js:265-277` scans every `*.md` there), never a line starting `untrusted comment: rsign encrypted secret
    key` or `untrusted comment: minisign encrypted secret key`, and never an `untrusted comment: … secret key` line
    followed by a base64 line (the FR-024 scan rules (b), (c), `RULE_B` / `RULE_C` at
    `tools/feed-contract/tests/c3-trusted-comment.test.js:213-216`).
12. **Scope fence (FR-080).** Only these paths change: `tools/minisign.js`, `tools/release-facts.js`,
    `tools/gen-release-manifest.js`, `tools/backfill-sign.js`, `tools/gen-feed.js` (one export line),
    `tools/feed-contract/**` (no Phase 1 fixture file changes), `.github/workflows/release.yml`,
    `.github/workflows/backfill-sign.yml`, `.github/workflows/feed-tools.yml`, `specs/INDEX.md`,
    `specs/krate-manager-phase2/**`. In `release.yml` no line of `:189-250`, `:285-337`, `:379-403` changes (FR-081).
13. **Zero warnings.** `node --check` clean on every new or changed `.js`; the TAP output carries no `Warning:`,
    `DeprecationWarning` or `ExperimentalWarning` line from Node; L-YAML clean on every changed workflow. Never silence a
    warning without a one-line reason the reviewer accepts.
14. **YAML block style for expressions.** Any mapping value containing `${{ … }}` is written in block style (or as a
    quoted scalar), never inside a `{ … }` flow mapping. This corrects plan §4.8's `env: { PLUGIN: ${{
    needs.version.outputs.plugin }} }` and `with: { node-version: '24' }`-style lines that carry an expression: in a YAML
    flow mapping a plain scalar cannot contain `{` / `}`, so the expression breaks the parse. Flow mappings without an
    expression (`with: { name: macos-facts, path: facts }`) are allowed. L-YAML enforces it.

### Lanes (referenced by every task)

| Lane | Commands | Pass |
|---|---|---|
| **L-NODE** | `node --check <each new or changed .js>`; the task's own file(s): `node --test --test-reporter=tap "tools/feed-contract/tests/<file>.test.js" > f:/tmp/km2-<task>.tap`; then the whole set: `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > f:/tmp/km2-all.tap` (quoted glob, files in parallel child processes; never `--test-concurrency=1` or `--test-isolation=none`); read results with `tail -12` of the file | every `node --check` exits 0; TAP `# fail 0`; no Node warning line |
| **L-YAML** | `npx --yes yaml@2.9.1 valid < <workflow file>` (the Phase 1 lane, `specs/krate-manager-phase1/tasks.md:64`; the workflow harness runs `yaml@2.6.1`, `.claude/workflows/krate-manager-phase.js:598`) | exit 0 |
| **L-LINT** | `node tools/lint-serial-test-runs.js` | exit 0 |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js f:/tmp/km2-all.tap` (exists from T019) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase2/evidence/scope-check.js` (exists from T020) | exit 0 |

---

## Preconditions — rulings and one user step needed before the build stage

The plan leaves three questions to the user (plan §10). **T001 stops the build if any ruling, or the Q-A fixture, is
missing.** The main loop records each ruling in `spec.md` `## Clarifications` under a new `### Session 2026-10-30
(plan)` heading, one bullet per question, `Q: … → A: …`, tagged `[Q-A]`, `[Q-B]`, `[Q-C]`.

- **Q-A (plan §10 Q1, SI-6): who makes `fixtures/phase2/kat-secret/kat.key`?** rsign2 reads a password only from a
  terminal (spec External facts, rpassword), so no agent can make it. Recommended (a): **the user**, once, at a terminal
  on this box, in a fresh empty temp dir outside the repo:
  1. `rsign generate -p kat.pub -s kat.key` (type the published test password `krate-kat-test-password` twice);
  2. write `message.txt` with the content `krate kat-secret message\n` (LF);
  3. `rsign sign -s kat.key -t "krate:1:KatPlugin:1.2.3:linux-x86_64:message.txt" -x message.txt.minisig message.txt`
     (password again);
  4. `rsign verify -p kat.pub -x message.txt.minisig message.txt` (prints `Signature and comment signature verified`);
  5. copy `kat.key`, `kat.pub`, `message.txt`, `message.txt.minisig` into
     `tools/feed-contract/fixtures/phase2/kat-secret/`, converting any CRLF in `message.txt.minisig` and `kat.pub` to LF,
     and paste the `rsign --version` line and the step 4 output into the main loop so T002 can record them.
  Alternative (b): the implementer makes it with `tauri signer generate -p … -w …` via `npx @tauri-apps/cli`
  (changes FR-064's "made by rsign2"; needs a network fetch of the Tauri CLI). **Why this blocks the build:** PD-23
  makes `signingConfig` (real mode) and `backfill-sign.js apply` fail closed when `kat-secret/kat.pub` is unreadable, so
  SC-010 to SC-013, SC-022 (the `signing=on` case), SC-027 and SC-028 cannot pass without the fixture; and the moment
  `kat.key` exists, the Phase 1 secret-key scan (`c3-trusted-comment.test.js:275`,
  `assert.deepEqual(findSecretKeyFiles(CONTRACT_DIR), [])`) turns red until T002 lands, which is why T002 is the
  second task.
- **Q-B (plan §10 Q2): spec edits SI-1 to SI-12.** Confirm the plan's resolutions so T001 can write them into the spec
  rows (the compliance stage checks rows, not the plan). Recommended: accept all twelve as stated in plan §0.2.
- **Q-C (plan §10 Q3, PD-22): the `signing` GitHub Environment.** Both secrets become environment secrets of `signing`
  (deployment branches: `main` only); the `release` job and both `apply-*` jobs carry `environment: signing`.
  Consequence: every `release.yml` run, dry run included, can start its `release` job only from `main`. Recommended:
  confirm (SC-018 and SC-019 are dispatched after the merge anyway, FR-062 step 4).

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | sequential | T001 rulings + spec rows · T002 kat-secret README + Phase 1 scan exemption · T003 27 codes + `CONTRACT.md` §7 rows · T004 `selectReleases` export · T005 rsign in the write guard + `external-tools.js` | Preconditions |
| G2 Modules | **[P]** | T006 `minisign.js` core · T007 `release-facts.js` + `macho.js` + objdump samples · T008 KatPlugin fixture | G1 |
| G3 Manifest | sequential | T009 `minisign.js` secret-key decryption · T010 `gen-release-manifest.js` · T011 SC-008 Windows subtest | G2 |
| G4 Backfill | sequential | T012 `backfill-sign.js` plan / download / facts · T013 `backfill-sign.js` apply | G3 |
| G5 Workflows and keys | sequential | T014 `release.yml` + SC-014 part 1 · T015 `backfill-sign.yml` + SC-014 part 2 · T016 `keys/README.md` + SC-029 | G4 |
| G6 Integration | sequential | T017 hygiene lists + SC-026 · T018 `CONTRACT.md` §9 · T019 `feed-tools.yml` + `tap-check.js` · T020 scope check, index, lints, close run, PENDING-CI | G1-G5 |

Every SC appears in exactly one task as its owner: SC-001/002/003/023 T006; SC-004/005/024 T007; SC-008 T007 + T011;
SC-027/028 T009; SC-006/007/009/022 T010; SC-025 T012; SC-012 T012 + T013; SC-010/011/013 T013; SC-014 T014 + T015;
SC-029 T016; SC-015/026 (and SC-021 clause 2) T017; SC-016 T005 + T017 + T020; SC-017/018/019 PENDING-CI (T020 list);
SC-020/021 T020.

---

## G1 — Foundation (sequential)

### T001 — Rulings check and the plan's spec amendments into `spec.md`

**Files:** `specs/krate-manager-phase2/spec.md` (edit).

**Precondition (STOP AND SURFACE).** (1) Read `spec.md` `## Clarifications`; if it lacks a ruling for each of Q-A, Q-B
and Q-C (Preconditions above), return status `blocked` with notes quoting the three questions verbatim from this file,
and change nothing. (2) Under the Q-A (a) ruling, check that `tools/feed-contract/fixtures/phase2/kat-secret/` holds
`kat.key`, `kat.pub`, `message.txt`, `message.txt.minisig`; copy the four into a fresh `fs.mkdtemp`-style temp dir
(Bash: `mktemp -d`) and run `rsign verify -p kat.pub -x message.txt.minisig message.txt` there (exit 0); check
`head -1 kat.key` starts `untrusted comment: rsign encrypted secret key` and that its second line base64-decodes to 158
bytes beginning `EdScB2` (`node -e` reading the file). Any miss: `blocked`, naming what is missing. Under Q-A (b),
note that T009 makes the fixture and continue.

**Test first.** Docs-only task: no test file. Before editing, record in the notes which rows below already carry the
change (expected: none).

**Implement.** Edit each row in place, appending `(plan PD-n)` or `(plan SI-n)` to the changed sentence; none relaxes a
threshold. The source text is plan §0.1 / §0.2, which is binding:

| Row | Change | Source |
|---|---|---|
| FR-015, FR-051 | `linuxFacts` takes an optional `{listDirectory}` (default `listing.listingFromDirectory`); `deps` gains optional `listDirectory` | PD-4, SI-1 |
| SC-010 | the win32 run injects a lister returning the tarball's own listing; POSIX legs use real `tar -xzf` and the real lister | PD-4, SI-1 |
| FR-072, SC-015, SC-016 | `CHILD_EXEMPT` gains `tools/feed-contract/tests/helpers/external-tools.js` besides `tools/backfill-sign.js`; the FR-060 one-path exemption lives in `c3-trusted-comment.test.js` (not `hygiene.test.js`); SC-016's "two files" becomes three (`write-guard.js`, `hygiene.test.js`, `c3-trusted-comment.test.js`) | PD-5, PD-6, SI-2, SI-3 |
| SC-014 | the `release.yml` roster check covers only the Phase 2 step blocks (plan §4.8 list) and the `dry_run` input block; `backfill-sign.yml` whole; adds the PD-20, PD-21, PD-22 assertions of plan §7.1 SC-014 | PD-7, SI-4, SI-10 |
| Review notes | "43" → "46"; the `target_commitish` bullet records Q5 (ruled, FR-047) instead of "left to the user" | SI-5 |
| FR-064 | the encrypted KAT key is made per the Q-A ruling (user at a terminal, or (b)) | SI-6 |
| FR-001, SC-027 | test-only export `_internal: {blake2b, rawScryptParams}`; SC-027 names the BLAKE2b anchors (lengths 0, 1, 127, 128, 129, 1000 vs `blake2b512`; RFC 7693 Appendix A) and the scrypt-parameter known answer `(33554432, 1048576) → {N: 32768, r: 8, p: 1}`, plus subtests (a) CRLF accept, (b) cost limit without calling scrypt, (c) public-half mismatch | PD-18, PD-2, PD-3, SI-7, SI-8 |
| FR-009a | `maxmem = 2 * 128 * N * r`; rejects `memlimit > 1 GiB`, `log2(N) > 20`, cost `> 2 GiB`; "public half" = `sk[32..64]` vs the seed's public key | PD-2, PD-3 |
| FR-009b, SC-028 | canonical containment (realpath of the existing parent vs realpath of `RUNNER_TEMP`, case-folded on win32), `..` refused, parent must exist, no existing entry at `--out`; unencrypted layout per PD-16; SC-028 runs with an un-canonicalised `RUNNER_TEMP` and has 7 failures (adds missing parent, existing entry, POSIX symlinked parent) | PD-16, PD-19, SI-8 |
| FR-029, SC-022 | real mode refuses a public key whose key id or 32-byte key equals `fixtures/kat/kat.pub` or `fixtures/phase2/kat-secret/kat.pub` (either unreadable → refuse); SC-022 becomes 4 + 1 + 7 | PD-23, SI-9 |
| FR-052, FR-057 | `plan` aborts on `E_TAG_FORMAT`; abort list gains `E_TAG_FORMAT` and `E_SIGNING_CONFIG`; a failing `gh release upload` is a per-release `E_INPUT` report (exit 3); SC-012 gains subtests (a)-(e) of plan §7.1 | PD-10, PD-23, PD-24, SI-9 |
| FR-045, FR-054 | the decrypted key is deleted right after the last signature (`Delete signing key (after signing)`; `apply` removes `--key` at the end of pass 1); `apply` runs three passes | PD-9, PD-20, SI-10 |
| FR-047 (new FR-047b) | `Tag must not exist` step on real runs; `Create Release` gets `overwrite_files: false` | PD-21, SI-10 |
| FR-058, FR-061, FR-062, SC-029 | the `signing` environment (main-only deployment branches), environment secrets, `environment: signing` on `release`, `apply-dry`, `apply-live`; README and merge steps name it; `apply-*` check out with `fetch-depth: 0` | PD-14, PD-22, SI-10 |
| FR-010, FR-013, FR-053a | `release-facts.js linux-precheck --tarball <file>`; the listing and layout are checked before any extraction, and `objdump` opens only a `.so` the listing shows as a regular file | PD-26, SI-11 |
| SC-006, SC-007, SC-021 | SC-006 `install` deep equality + `preset_subdir` / `identifier` subtest; SC-007 subtests 8 (`E_LISTING_DIGEST`), 9, 10 (two-file write, PD-25); SC-021 clause 2 is an SC-026 subtest | PD-25, SI-12 |
| FR-073 | the TAP check is a bash heredoc run locally by `evidence/tap-check.js`; a `# SKIP` anywhere in the SC-003 / SC-025 / SC-027 blocks fails on CI and locally | PD-15, PD-27 |
| FR-042, FR-053a | `umask 000` before extraction in both Linux fact steps | PD-13 |
| FR-053 | `digest` is best-effort; `download` / `apply` log `digest checked: <n> of <m>` | PD-28 |

**Done when:** every row above carries its change and its source tag, and the notes list each edited row id (the spec
is untracked, so there is no git diff to cite). **Lane:** none (docs).

### T002 — `kat-secret/README.md` and the one-path secret-key exemption (FR-060, FR-064, PD-6)

**Files:** `tools/feed-contract/fixtures/phase2/kat-secret/README.md` (new),
`tools/feed-contract/tests/c3-trusted-comment.test.js` (edit, Phase 1 file). The four key files are the user's (Q-A);
this task never writes them.

**Test first.** Run L-NODE for `c3-trusted-comment.test.js` with the user's `kat.key` in place: the Phase 1 scan fails
at `:275` with `findSecretKeyFiles(CONTRACT_DIR)` returning the `kat.key` path (the recorded red). Under Q-A (b) the
fixture does not exist yet; then write the exemption so it fails while the file is absent (the assertions below require
it), record that red, and leave this test red until T009 makes the fixture (say so in the notes).

**Implement.**
- `c3-trusted-comment.test.js:275`: replace `assert.deepEqual(findSecretKeyFiles(CONTRACT_DIR), [])` with a comparison to
  exactly one allowed path, `path.join(CONTRACT_DIR, 'fixtures', 'phase2', 'kat-secret', 'kat.key')` (normalise
  separators the way `findSecretKeyFiles` returns them), inside the existing test `KAT binds and no secret key is
  committed` (`:256`), and for that file assert: its first line matches `RULE_B` (`:215`); its second line base64-decodes to 158 bytes whose first six bytes are `EdScB2` (ASCII). No other line of the
  file changes. **Correction to plan §4.11:** the plan also compares the key file's key-id bytes with `kat.pub`, but in
  an encrypted minisign key the whole `keynum_sk` (key id ‖ secret key ‖ checksum, decoded bytes 54..157) is XORed with
  the scrypt keystream (plan §4.1 pseudo-code, `blob := blob XOR stream`), so the stored key-id bytes are not the
  plaintext id and that comparison would always fail. The key-id half of FR-060's exemption assertion is added by T009,
  through `decryptSecretKey`, once it exists.
- `README.md`: date of generation, the `rsign --version` line, the five Q-A steps as run, the step 4 verify output, the
  published test password `krate-kat-test-password`, the statement "test-only: signs nothing that ships; the one
  committed secret-key file (spec FR-064)", and that PD-23 refuses this `kat.pub` as a production key. Rule 11: never
  write the key file's header line at a line start (quote it inline, e.g. inside backticks mid-sentence).

**Done when:** L-NODE green for `c3-trusted-comment.test.js` (red first recorded); `git diff -U0
tools/feed-contract/tests/c3-trusted-comment.test.js` shows only the `:275` replacement and the added assertions;
the whole-set L-NODE run is green (it is the Phase 1 set plus nothing yet).

### T003 — The 27 new codes and their `CONTRACT.md` §7 rows (FR-074)

**Files:** `tools/feed-contract/contract.js` (edit `CODES` only), `tools/feed-contract/CONTRACT.md` (edit §7 table only).

**Test first.** Add the 27 keys to `CODES` first (after `E_SCHEMA: 'none'`, `tools/feed-contract/contract.js:70`, each
with value `'none'`), run L-NODE for `hygiene.test.js`: the unchanged test `CONTRACT.md documents every code and field`
(`hygiene.test.js:359-394`) fails naming each of the 27 codes without a §7 row (the recorded red).

**Implement.** The 27 codes are, exactly: `E_MINISIG_PUBKEY`, `E_MINISIG_SECRET_KEY`, `E_MINISIG_PASSWORD`,
`E_MINISIG_KEY_ID`, `E_MINISIG_SIGNATURE`, `E_MINISIG_GLOBAL`, `E_FACTS_NO_MACHO`, `E_FACTS_ARCH`,
`E_FACTS_ARCH_MISMATCH`, `E_FACTS_NO_MINOS`, `E_FACTS_MACHO_CORRUPT`, `E_FACTS_NO_GLIBC`, `E_FACTS_LISTING_MISMATCH`,
`E_FACTS_LIPO_MISMATCH`, `E_FACTS_STALE`, `E_FACTS_MISSING`, `E_ASSET_MISSING`, `E_VERSION_MISMATCH`,
`E_SIGNING_CONFIG`, `E_BACKFILL_LIST_TRUNCATED`, `E_BACKFILL_ASSET_MISSING`, `E_BACKFILL_IMMUTABLE`,
`E_BACKFILL_PARTIAL`, `E_BACKFILL_SIZE`, `E_BACKFILL_DIGEST`, `E_BACKFILL_ASSETS_CHANGED`, `E_SIGN_KEY_MISMATCH`
(spec FR-074). In `CONTRACT.md` §7's table (header `| Code | Class | Raised by | Meaning |`,
`tools/feed-contract/CONTRACT.md:418-419`) append one row each, `` | `E_…` | none | <Raised by> | <Meaning> | ``, with the
Raised-by and Meaning cells of plan §3.2 (binding). Extend the Raised-by cells (text only, class cells unchanged) of
`E_INPUT`, `E_SIGNATURE_TEXT`, `E_TC_BINDING`, `E_LAYOUT` and `E_LISTING_DIGEST` with the Phase 2 tools that also raise
them (plan §3.2 last paragraph). No other `contract.js` line changes (FR-080: `CODES` only); no new `## ` heading in this
task (§9 is T018).

**Done when:** L-NODE green for `hygiene.test.js` and the whole set; `git diff -U0 tools/feed-contract/contract.js`
shows 27 added lines inside `CODES` and nothing else.

### T004 — `selectReleases` exported from `gen-feed.js` (FR-075)

**Files:** `tools/gen-feed.js` (edit `module.exports` only, `tools/gen-feed.js:762-772`).

**Test first.** `node -e "const g=require('./tools/gen-feed.js'); if (typeof g.selectReleases!=='function') process.exit(1)"`
exits 1 (the recorded red; the function exists at `tools/gen-feed.js:418` but is not exported).

**Implement.** Add one line `  selectReleases,` to `module.exports`. Nothing else in the file changes; the Q9 rule stays
in gen-feed.js alone (roadmap line 301).

**Done when:** the same `node -e` exits 0; `git diff -U0 tools/gen-feed.js` shows exactly one added line; L-NODE whole
set green (Phase 1 goldens byte-identical, SC-016).

### T005 — rsign in the write guard, `external-tools.js`, `CHILD_EXEMPT` and the teeth (FR-071, FR-072 part, PD-5, SC-015 part, SC-016 part)

**Files:** `tools/feed-contract/tests/helpers/write-guard.js` (edit), `tools/feed-contract/tests/helpers/external-tools.js`
(new), `tools/feed-contract/tests/hygiene.test.js` (edit: `CHILD_EXEMPT` and subtest (c) only).

**Test first.** In `hygiene.test.js` subtest `(c) the guard has teeth` (`:145`), add, with `k`, `s`, `f` paths inside
the subtest's `tmp`: `assert.throws(() => childProcess.spawnSync('rsign', ['sign', '-W', '-s', k, '-t', 'x', '-x', s, f,
'--extra'], {cwd: tmp}), outside)` and `assert.throws(() => childProcess.spawnSync('rsign', ['--version'], {cwd:
repoDir}), outside)`; and a positive control `childProcess.spawnSync('rsign', ['--version'], {cwd: tmp})` does **not**
throw a guard error (it may return ENOENT when rsign is absent). Run: the positive control throws `command not allowed`
(`write-guard.js:235`) — the recorded red.

**Implement.**
- `write-guard.js` (plan §4.11 first bullet, binding): `checkSpawn` admits `rsign` beside `git` / `tar` with
  `checkRsign(op, args, cwd)` accepting exactly `['--version']`; `['generate', '-W', '-p', P, '-s', P]`; `['sign', '-W',
  '-s', P, '-t', T, '-x', P, P]`; `['verify', '-p', P, '-x', P, P]`, every `P` through `rejectDotDot` and
  `assertUnderTmp(path.resolve(cwd, P))`, `T` any string without CR or LF; anything else throws
  `E_WRITE_OUTSIDE_TMP` with reason `rsign shape`. The `cwd` rule and the `shell: true` refusal stay as they are. The
  header comment's allowlist sentence (`write-guard.js:15`) names rsign.
- `external-tools.js` (plan §4.12.2, binding): `rsignGate(t)` (spawns `rsign --version` with `cwd` a fresh temp dir;
  `true` when stdout is exactly `rsign2 0.6.7` plus a line end; otherwise `t.skip('<found version or ENOENT>')` and
  `false` when `process.env.CI` is unset, and throws when it is set: D13); `rsignGenerate(dir)` → `{pub, sec}` paths;
  `rsignSign(dir, sec, comment, sig, file)`; `rsignVerify(dir, pub, sig, file)` → `{status, stdout, stderr}`;
  `extractTarGz(src, dest)` (`tar ['-xzf', src, '-C', dest]`, POSIX only, throws on win32). Every spawn:
  `childProcess.spawnSync(cmd, args, {cwd: <temp dir>, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']})`, exactly
  the FR-071 shapes. It requires `child_process` through the module object (rule 4).
- `hygiene.test.js:65-69`: `CHILD_EXEMPT` gains `'tools/backfill-sign.js'` and
  `'tools/feed-contract/tests/helpers/external-tools.js'`. Nothing else in the file changes in this task.

**Done when:** L-NODE green for `hygiene.test.js` (red first recorded; the scan in subtest (a) already reaches
`external-tools.js` through `helpers/*.js`, `hygiene.test.js:45`, and passes because of the exemption); `node -e`
loading `external-tools.js` after `write-guard.js` and calling `rsignGate` with a stub `t` prints `true` on this box
(rsign2 0.6.7 is installed); L-NODE whole set green.

---

## G2 — Modules [P] (each task creates only its own new files)

### T006 [P] — `tools/minisign.js` core: keygen, public key, sign, verify, `verify` CLI (FR-001 to FR-008, SC-001, SC-002, SC-003, SC-023)

**Files (new):** `tools/minisign.js`, `tools/feed-contract/tests/minisign.test.js`.

**Test first — `minisign.test.js`**, four top-level tests (exact names; assertions are spec SC-001, SC-002, SC-003,
SC-023 and plan §7.1, binding):
- `minisign.js verifies the rsign2 KAT`: `verifyFile` on `tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz`
  with its `.minisig` text and `kat.pub` returns `{trustedComment:
  'krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz', keyId: '440FA75168B852F7'}`; four subtests on
  temp copies: one archive byte flipped → `E_MINISIG_SIGNATURE`; one byte of the decoded line-2 signature (bytes 10..73)
  flipped and re-encoded → `E_MINISIG_SIGNATURE`; one key-id byte (decoded bytes 2..9) flipped → `E_MINISIG_KEY_ID`; one
  trusted-comment character changed → `E_MINISIG_GLOBAL`. Assert codes with `assert.throws(fn, {code: '…'})`.
- `minisign.js sign and verify round trip`: every assertion of spec SC-002 (1 MiB random temp file; the four
  `E_MINISIG_PUBKEY` cases: first line not `untrusted comment: `, non-canonical base64 (trailing `=` and a non-alphabet
  character), 41 bytes, 42 bytes with `ED` in place of `Ed`).
- `minisign.js and rsign2 interoperate`: `rsignGate(t)` first (`external-tools.js`); subtest (a) `formatPublicKey` text
  and a `signFile` signature with a C3 comment written into the temp dir → `rsignVerify` status 0 and **stderr**
  contains `Trusted comment: <tc>` (PD-17); subtest (b) `rsignGenerate` + `rsignSign` with a C3 comment → `verifyFile`
  returns that comment (also proves `parsePublicKey` reads rsign's `.pub`).
- `minisign.js verify CLI prints and exits by contract`: `main(['verify', '-p', kat.pub, '-x', sig, tarball], io)` with
  captured `io.stdout` / `io.stderr` (objects with a `write` method) and `io.env = {}`: stdout exactly
  `verified: krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz\n`, return 0; a temp copy with one
  flipped archive byte → 1, stderr starts `minisign: E_MINISIG_SIGNATURE:`; `-x` missing → 2, stderr starts
  `usage: node tools/minisign.js`; unknown subcommand → 2, same prefix.
Run before `minisign.js` exists: `MODULE_NOT_FOUND` (recorded red).

**Implement `tools/minisign.js`** per plan §4.1 (API block, "Byte layouts", "Verify order"; binding) and the tool form
(rule 5): exports `main`, `generateKeyPair`, `formatPublicKey`, `parsePublicKey`, `signFile`, `signBuffer`,
`verifyFile`, `verifyBuffer`; `decryptSecretKey` and `_internal` are added by T009. Requires
`node:crypto`, `node:fs`, `node:path` and `./feed-contract/contract.js` only (FR-001). Key facts to get right:
SPKI prefix `302a300506032b6570032100`; PKCS#8 prefix `302e020100300506032b657004220420`; `<KEYID>` = key-id bytes
reversed, upper-case hex; prehash `crypto.createHash('blake2b512')` (streamed in `signFile` / `verifyFile`);
`parseMinisigText` (`contract.js:329`) returns no signature bytes, so decode line 2 again for the 64-byte `sig`;
`.minisig` output LF with a final LF, default untrusted comment `signature from krate minisign.js`; a trusted comment
with CR or LF is a `TypeError`. The only CLI subcommand now is `verify` (FR-007); `decrypt-key` comes in T009.

**Done when:** L-NODE green for `minisign.test.js` (red first recorded) with SC-003 **not** skipped on this box (TAP
shows no `# SKIP` in its block); L-NODE whole set green.

### T007 [P] — `tools/release-facts.js`, `helpers/macho.js` and the objdump samples (FR-010 to FR-016, PD-26, SC-004, SC-005, SC-024, SC-008 part)

**Files (new):** `tools/release-facts.js`, `tools/feed-contract/tests/release-facts.test.js`,
`tools/feed-contract/tests/helpers/macho.js`, `tools/feed-contract/fixtures/phase2/objdump/glibc-mixed.txt`,
`…/objdump/glibc-numeric.txt`, `…/objdump/no-glibc.txt`.

**Test first — `release-facts.test.js`**, four top-level tests (exact names; assertions are spec SC-004, SC-005, SC-024,
SC-008 and plan §7.1, binding):
- `release-facts reads Mach-O arch sets and minimum macOS`: every case of spec SC-004 built with `helpers/macho.js`
  (thin arm64 `LC_BUILD_VERSION` `0x000F0000` → `{archs: ['arm64'], minOs: '15.0'}`; fat x86_64 `LC_VERSION_MIN_MACOSX`
  `0x000A0D00` + arm64 `LC_BUILD_VERSION` `0x000B0000` → `['arm64', 'x86_64']`, slices `10.13` / `11.0`; fat + thin
  arm64 tree → `E_FACTS_ARCH_MISMATCH`; two fat files → `darwin-universal`, `min_os` `11.0`; `0x000F0601` → `15.6.1`;
  arm64 subtype 2 → `E_FACTS_ARCH`; a tree with a thin `0xfeedface` file (`cputype` 7) beside a valid arm64 file →
  `E_FACTS_ARCH` from `darwinFactsFromTree`; thin `0xcffaedfe` → `E_FACTS_ARCH`; fat with an i386 slice →
  `E_FACTS_ARCH`; no version command → `E_FACTS_NO_MINOS`; a command past the end → `E_FACTS_MACHO_CORRUPT`; plain
  files → `E_FACTS_NO_MACHO`; `platformKeyForArchs` for the three sets). The symlink subtest (a link to a Mach-O inside
  the tree is not counted) runs where `fs.symlinkSync` works and is skipped **as a subtest** on win32.
- `release-facts reads the glibc floor`: the three samples → `'2.34'`, `'2.10'`, `E_FACTS_NO_GLIBC`.
- `release-facts darwin CLI writes facts and lipo lines`: spec SC-024 exactly (two fat files at
  `Payload/Library/Audio/Plug-Ins/VST3/K.vst3/Contents/MacOS/K` and `Payload/Library/Audio/Plug-Ins/Components/K.component/Contents/MacOS/K`;
  exact `--out` JSON `{asset, size, sha256, platform_key: 'darwin-universal', min_os: '11.0', archs: ['arm64',
  'x86_64'], macho}`; stdout exactly two lines `arm64 x86_64\t<path>` in bytewise order; unknown mode → 2 with the
  usage line; a tree without a Mach-O → 1, stderr starts `release-facts: E_FACTS_NO_MACHO:`, no `--out` file).
- `Linux payload digest equals the extracted tree's listing` (created here, completed by T011): subtests for every OS —
  `main(['linux-precheck', '--tarball', <temp copy of the KAT tarball>], io)` → 0, empty stdout; a temp copy of
  `tools/feed-contract/fixtures/listings/bad/dotdot-path.tar.gz` → 1 with stderr starting
  `release-facts: <contents of dotdot-path.expected-error>:`; the same for `symlink-escape-root.tar.gz` and
  `layout-missing-presets.tar.gz` (each expected code read from its committed `.expected-error`). POSIX-only subtests
  (skipped on win32 with the reason "Phase 1 D4: listingFromDirectory needs POSIX modes"): `extractTarGz` of the KAT
  into a temp dir, then `main(['linux', '--tarball', …, '--extracted', …, '--objdump', <glibc-mixed.txt copy>, '--out',
  …])` → 0 and the written `payload` is `{files: 10, sha256:
  'db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6'}`; the same tree plus one extra file → 1,
  `E_FACTS_LISTING_MISMATCH`, no `--out`.
Run before the module exists: `MODULE_NOT_FOUND` (recorded red).

**Implement.**
- `helpers/macho.js` per plan §4.12.1 (`thin`, `buildVersion`, `versionMin`, `fat` with `wide`; constants mirroring
  `loader.h` / `fat.h` / `machine.h` as in spec External facts rows 173-175 and plan §1.2). Read-only module: no fs
  writes, no child process.
- `objdump/*.txt`: hand-written excerpts in GNU binutils `objdump -T` layout (`0000000000000000      DF *UND*
  0000000000000000 (GLIBC_2.34) memcpy`); `glibc-mixed` names `GLIBC_2.2.5`, `GLIBC_2.17`, `GLIBC_2.34`,
  `GLIBC_PRIVATE`; `glibc-numeric` names `GLIBC_2.9`, `GLIBC_2.10`; `no-glibc` names only `CXXABI_1.3` /
  `GCC_3.0` symbols.
- `tools/release-facts.js` per plan §4.2 (API block, `machoFacts`, `darwinFactsFromTree`, `glibcFloor`, `linuxPrecheck`,
  `linuxFacts` with `{listDirectory}`, CLI with modes `darwin`, `linux`, `linux-precheck`; binding). Requires
  `node:fs`, `node:path`, `node:crypto`, `./feed-contract/contract.js`, `./feed-contract/listing.js` only; no
  `child_process`, no network. Outputs through a local `writeAtomic` identical in behaviour to `tools/gen-feed.js:174-190`.
  `linuxFacts` reads the tarball once (`fs.readFileSync`), runs `linuxPrecheck` before touching `extracted` (so
  `E_LAYOUT` / `E_LISTING_*` come first on any OS), then compares `listDirectory(extracted)` line by line.

**Done when:** L-NODE green for `release-facts.test.js` (red first recorded; on win32 the POSIX subtests show as skipped
subtests, never as a skipped top-level test); L-NODE whole set green.

### T008 [P] — KatPlugin plugin fixture (FR-070)

**Files (new):** `tools/feed-contract/fixtures/phase2/KatPlugin/version.json`,
`tools/feed-contract/fixtures/phase2/KatPlugin/installers/windows/setup.iss`.

**Test first.** `node -e "const g=require('./tools/gen-feed.js'); const fs=require('fs'); console.log(g.extractAppId(fs.readFileSync('tools/feed-contract/fixtures/phase2/KatPlugin/installers/windows/setup.iss','utf8')))"`
fails (`ENOENT`, the recorded red).

**Implement.** `version.json` = `{"name": "KatPlugin", "version": "1.2.3"}` plus a final LF. `setup.iss` = a three-line
Inno-style stub: `[Setup]`, `AppId={{0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0}`, `AppName=KatPlugin`, LF endings (the GUID
must be upper-case hex to match `APP_ID_RE`, `tools/gen-feed.js:310`).

**Done when:** the `node -e` above prints `{0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0}`. **Lane:** L-NODE whole set green
(fixtures under `fixtures/phase2/` are outside `make-fixtures.js` `SETS`, plan §1.1 row "make-fixtures.js").

---

## G3 — Manifest (sequential)

### T009 — `minisign.js` secret-key decryption and the `decrypt-key` CLI (FR-009 to FR-009b, FR-064, PD-1 to PD-3, PD-16, PD-18, PD-19, SC-027, SC-028)

**Files:** `tools/minisign.js` (edit, created by T006), `tools/feed-contract/tests/minisign.test.js` (edit, created by
T006), `tools/feed-contract/tests/c3-trusted-comment.test.js` (edit: one assertion added to T002's exemption block,
`decryptSecretKey(<kat.key text>, 'krate-kat-test-password').keyId` equals the key id `parsePublicKey(<kat-secret/kat.pub
text>)` returns, so the one exempt path is proven to be the published test key, FR-060). Under Q-A (b) only: also `tools/feed-contract/fixtures/phase2/kat-secret/{kat.key,kat.pub,message.txt,message.txt.minisig}`
(new) made per the ruling's procedure in an `fs.mkdtemp` dir, then copied, and T002's red becomes green here.

**Test first — two more top-level tests in `minisign.test.js`** (exact names; assertions are spec SC-027 / SC-028 as
amended by T001, and plan §7.1 rows SC-027 / SC-028, binding):
- `minisign.js decrypts the rsign2 secret-key KAT`: with `kat-secret/` and the password `krate-kat-test-password`:
  `decryptSecretKey` returns the key id of `kat-secret/kat.pub`; a `signBuffer` over a small buffer with the returned
  key passes `verifyBuffer` against `kat.pub`; the Tauri single-line form (`Buffer.from(fileText).toString('base64')`)
  decrypts to the same key id; an unencrypted-form file built in the test (PD-16 layout) decrypts with any password;
  rejections: wrong password → `E_MINISIG_PASSWORD` and the message contains neither password; one flipped byte of the
  encrypted `keynum_sk` (decoded bytes 54..157) → `E_MINISIG_PASSWORD`; a truncated file → `E_MINISIG_SECRET_KEY`; an
  unknown `kdf_alg` → `E_MINISIG_SECRET_KEY`; (a) CRLF `kat.key` → same key id; (b) memlimit field (decoded bytes 46..53)
  set to `2^30 + 1` → `E_MINISIG_SECRET_KEY` and a wrapper installed on `crypto.scryptSync` for the subtest records 0
  calls; (c) an unencrypted-form key with `sk[32..64]` replaced and its checksum recomputed with `_internal.blake2b` →
  `E_MINISIG_SECRET_KEY`; anchors: `_internal.blake2b(buf, 64)` equals `crypto.createHash('blake2b512')` for random
  inputs of length 0, 1, 127, 128, 129, 1000; the RFC 7693 Appendix A BLAKE2b-512 vector of `"abc"`;
  `_internal.rawScryptParams(33554432n, 1048576n)` (or Numbers, as implemented) → `{N: 32768, r: 8, p: 1}`; last
  subtest gated by `rsignGate`: `decrypt-key` output (via `main`) → `rsignSign` with a C3 comment → `rsignVerify`
  against a temp copy of `kat-secret/kat.pub` exits 0.
- `minisign.js decrypt-key writes only under RUNNER_TEMP and cleans up`: `main(['decrypt-key', '--out', <tmp>/k.key],
  io)` with `io.env = {KRATE_SIGNING_KEY: <kat.key text>, KRATE_SIGNING_KEY_PASSWORD: 'krate-kat-test-password',
  RUNNER_TEMP: tmp}` where `tmp` is exactly what `fs.mkdtempSync(path.join(os.tmpdir(), 'km2-dk-'))` returned (not
  realpath'd): return 0, stdout exactly `decrypted: key id <KEYID>\n`, `decryptSecretKey(<out text>, 'anything')`
  returns the same key id; POSIX subtest: `(fs.statSync(out).mode & 0o777) === 0o600`; seven failures, each returning 1
  with stderr starting `minisign: <CODE>:` and a directory listing of the target dir showing no out file and no temp
  file: wrong password (`E_MINISIG_PASSWORD`), `--out` outside `RUNNER_TEMP` (`E_INPUT`), password variable absent and
  empty (`E_INPUT`), key variable absent (`E_INPUT`), missing parent directory (`E_INPUT`), an existing entry at `--out`
  (`E_INPUT`), POSIX subtest: `--out` through a symlinked directory inside `tmp` pointing at a second temp dir outside
  it (`E_INPUT`, the second dir stays empty); leak scan: no captured stdout / stderr contains the password, the key
  file's base64 line or the hex of any decrypted key byte.
Run before the implementation: `decryptSecretKey is not a function` (recorded red).

**Implement** per plan §4.1 "Secret-key decryption", `rawScryptParams`, `blake2b`, "`decrypt-key` CLI" (binding):
in-file RFC 7693 BLAKE2b with configurable output length, used only for the 32-byte checksum (PD-1: Node v24.7.0 refuses
`createHash('blake2b512', {outputLength: 32})`); exact port of minisign's `raw_scrypt_params` in BigInt with the PD-2
limits, `crypto.scryptSync(password, salt, 104, {N, r, p, maxmem: 2 * 128 * N * r})`; PD-3 public-half check;
`E_MINISIG_PASSWORD` vs `E_MINISIG_SECRET_KEY` as plan §4.1 states; `decrypt-key` reads the two variables from `io.env`
only, applies the PD-19 containment check, writes the PD-16 unencrypted layout (untrusted comment `krate decrypted
secret key (runner temp only)`, `kdf_alg`, salt, limits zero, plaintext checksum) through a `0o600` `wx` temp file in
the target directory and a rename, removing the temp file in a `finally` on every failure. Export `decryptSecretKey` and
`_internal: {blake2b, rawScryptParams}` (PD-18). FR-009: no function that encrypts, generates or writes any other
secret-key file.

**Done when:** L-NODE green for `minisign.test.js` (red first recorded), with the rsign-gated subtest run, not skipped,
on this box; L-NODE green for `c3-trusted-comment.test.js` with the added key-id assertion (compare the two 8-byte
`keyId` Buffers with `.equals`); L-NODE whole set green.

### T010 — `tools/gen-release-manifest.js` (FR-020 to FR-029, PD-11 (tool side), PD-23, PD-25, SC-006, SC-007, SC-009, SC-022)

**Files (new):** `tools/gen-release-manifest.js`, `tools/feed-contract/tests/gen-release-manifest.test.js`.

**Test first — `gen-release-manifest.test.js`**, four top-level tests (exact names; assertions are spec SC-006,
SC-007, SC-009, SC-022 as amended by T001, and plan §7.1 rows, binding). Shared setup per subtest: a fresh temp
`assets/` with a 1,000-byte stub `KatPlugin-1.2.3-Windows-x64.exe`, a 2,000-byte stub `KatPlugin-1.2.3-macOS.pkg`, a
copy of the KAT tarball; a throwaway pair from `minisign.generateKeyPair()` whose `formatPublicKey` text is written to
`<tmp>/pub`; each asset signed with `minisign.signFile` using the comment `trustedComments` prints for it; darwin facts
written directly as `{asset, size, sha256, platform_key: 'darwin-universal', min_os: '11.0', archs: ['arm64',
'x86_64'], macho: []}` from the stub pkg; linux facts from `releaseFacts.linuxFacts({tarball, extracted: tmp,
objdumpText}, {listDirectory: () => listing.listingFromTarGz(fs.readFileSync(tarball))})` (PD-4 on win32);
`--version-json` / `--setup-iss` from T008's fixture; `--repo krate-test/krate-audio`; `--pub-date
2026-10-30T12:00:00Z`.
- `gen-release-manifest builds a verified release.json`: `main` returns 0; every `size` / `sha256` equals `fs.statSync`
  and an independent `crypto.createHash('sha256')`; every `signature` passes `minisign.verifyBuffer` against the
  throwaway public key and `contract.checkBinding`; `checkReleaseManifest(doc, {repo})` passes; Linux `payload` =
  `{files: 10, sha256: 'db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6'}` with the 10-line `list`; `SHA256SUMS` = three sorted
  GNU lines (`<hex>  <name>\n`) equal to the independent hashes; `install` deep-equals `{vst3_bundle: 'KatPlugin.vst3',
  au_component: 'KatPlugin.component', windows_uninstall_key: '{0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0}_is1',
  macos_pkg_id: 'com.krateaudio.katplugin'}`; subtest with a temp `version.json` adding `"preset_subdir": "Kits"` and
  `"identifier": "com.krateaudio.katkit"` → `preset_subdir === 'Kits'`, `macos_pkg_id === 'com.krateaudio.katkit'`;
  three tamper subtests (fresh out dirs): `.exe` byte flipped after signing → 1, `E_MINISIG_SIGNATURE`; `.pkg` flipped →
  1, `E_FACTS_STALE`; `.tar.gz` flipped → 1, `E_FACTS_STALE`; neither output exists afterwards.
- `gen-release-manifest refuses what it cannot bind`: the six named failures of spec SC-007 (`E_TC_BINDING` naming
  `asset`; `E_MINISIG_SIGNATURE`; `E_ASSET_MISSING`; `E_FACTS_STALE`; `E_INPUT` for a `setup.iss` without `AppId`;
  `E_MINISIG_KEY_ID`), each with no output file; subtest 7 (pre-existing outputs keep bytes and `mtimeMs`, no `.tmp`
  left); subtest 8 (`payload.files` changed, and separately `payload.sha256` changed → `E_LISTING_DIGEST`); subtest 9
  (`--out` replaced by a directory → `E_INPUT`, `SHA256SUMS` unchanged, no `.tmp` / `.bak`); subtest 10 (`fs.renameSync`
  wrapped in-process to throw on the rename onto `release.json` → 1, both outputs keep bytes and `mtimeMs`, no `.tmp` /
  `.bak`; restore the original in `finally`).
- `gen-release-manifest prints the C3 comments`: `main(['--trusted-comments', '--version-json', …, '--assets', …,
  '--darwin-facts', …], io)` stdout is exactly three lines in key order `darwin-universal`, `linux-x86_64`,
  `windows-x86_64`, each `<asset>\t<comment>` with `parseTrustedComment(comment)` returning plugin, version, key, asset.
- `signing configuration is throwaway, on or a failure`: `main(['--signing-config', '--dry-run', <bool>,
  '--public-key', <file>], {stdout, stderr, env})`: four dry-run cases (with and without each variable and the key file)
  → `signing=throwaway\n`, 0; real run, both variables non-empty and a valid throwaway key file → `signing=on\n`, 0;
  seven failures, each 1 with stderr starting `gen-release-manifest: E_SIGNING_CONFIG:`: key file absent (names `public
  key absent`), key secret empty (names `secret`), password empty, password absent (names `password`), everything
  missing (names all three), malformed key file (carries the `E_MINISIG_PUBKEY` message), and a temp copy of
  `tools/feed-contract/fixtures/kat/kat.pub` (names `test key`); a dry run with the KAT key copy stays `throwaway`; no
  stdout or stderr contains either variable's value. Requires `kat-secret/kat.pub` (Precondition Q-A) for the `on` case.
Run before the module exists: `MODULE_NOT_FOUND` (recorded red).

**Implement** per plan §4.3 (API block, `buildManifest` order 1-7, `writePair`, `--trusted-comments`,
`--signing-config`, `refuseTestKey`; binding) and the tool form. Requires `node:fs`, `node:path`, `node:crypto`,
`node:os`, `./minisign.js`, `./feed-contract/contract.js`, `./feed-contract/listing.js`, `./gen-feed.js` (for
`extractAppId`, `tools/gen-feed.js:770`). No `child_process`, no network, no plugin name (FR-028). Exports `main`,
`buildManifest`, `trustedComments`, `signingConfig`, `refuseTestKey`. Document key order `tag, name, version, pub_date,
assets, install`; assets bytewise; per asset `name, url, size, sha256, signature, format, min_os, payload`; output
`JSON.stringify(doc, null, 2) + '\n'`. `refuseTestKey` compares both the key id and the 32-byte Ed25519 key against
`path.join(__dirname, 'feed-contract', 'fixtures', 'kat', 'kat.pub')` and `…/fixtures/phase2/kat-secret/kat.pub`; an
unreadable one refuses (fail closed); dry-run mode never reads them.

**Done when:** L-NODE green for `gen-release-manifest.test.js` (red first recorded); L-NODE whole set green.

### T011 — SC-008 Windows subtest: payload equals the committed listing (FR-023, SC-008)

**Files:** `tools/feed-contract/tests/release-facts.test.js` (edit, created by T007).

**Test first.** Add to the existing top-level test `Linux payload digest equals the extracted tree's listing` a subtest
that runs on every OS: run `genReleaseManifest.buildManifest` on T010's KAT asset set (same setup as SC-006, rebuilt in
this file's own temp dir) and assert `doc.assets['linux-x86_64'].payload.files === 10`, `payload.sha256 ===
listing.digest(<lines of the committed tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.listing>)`,
and `payload.list` deep-equals those committed lines. Red first: write it against a deliberately wrong expected file
name (`…listing.missing`), see `ENOENT`, then point it at the real file (record both in the notes). The anchor is the
committed listing, not `listingFromTarGz` (spec SC-008: comparing with FR-023's own computation proves nothing).

**Implement.** Test-only change; no product change is expected. If the assertion fails, the defect is in T010's
`buildManifest` payload path: fix it there and rerun T010's file.

**Done when:** L-NODE green for `release-facts.test.js` and `gen-release-manifest.test.js`; still exactly four
top-level tests in `release-facts.test.js`.

---

## G4 — Backfill (sequential)

### T012 — `tools/backfill-sign.js` plan, download, facts, `productionDeps`; the fake world (FR-051 to FR-053a, PD-4, PD-10, PD-26, PD-28, SC-025, SC-012 part)

**Files (new):** `tools/backfill-sign.js`, `tools/feed-contract/tests/backfill-sign.test.js`,
`tools/feed-contract/tests/helpers/fake-gh.js`, `tools/feed-contract/fixtures/phase2/backfill/state.json`,
`tools/feed-contract/fixtures/phase2/backfill/plugins/<Synth>/version.json` and
`…/plugins/<Synth>/installers/windows/setup.iss` for the nine synthetic plugins `Alphaplug`, `Betaplug`, `Gammaplug`,
`Deltaplug`, `Epsilonplug`, `Zetaplug`, `Etaplug`, `Thetaplug`, `Iotaplug`, plus `…/plugins-extra/Kappaplug/…` for
SC-012 (a).

**Fixtures** (plan §4.12.3 `backfill/`, binding): each `version.json` `{"name": "<Synth>", "version": "<current stable
version>"}` except `Gammaplug`, which adds `"preset_subdir": "Kits"` and `"identifier": "com.krateaudio.gammakit"`; each
`setup.iss` with its own upper-case synthetic `AppId={{…}` GUID; `state.json` = per plugin one current stable release,
one older stable, one newer prerelease, one draft (tags `<lower name>/v<ver>`, `publishedAt` in the C2 pattern), each
with its three asset names (`contract.assetNameFor`), `isImmutable: false`; every asset `digest` `"auto"` except all of
`Iotaplug`'s, which are `null`; sizes `"auto"`. Check no synthetic name contains a shipped plugin name (rule 7).

**`helpers/fake-gh.js`** (read-only module except writes under the temp dir it is given; no `child_process`):
`makeWorld({tmp, mutate})` copies `state.json` into `tmp`, generates per release the asset bytes into `<tmp>/served/`
(stub `.exe` / `.pkg` bytes, and a C4-layout tarball with `<Name>.vst3/Contents/Resources/moduleinfo.json`,
`<Name>.vst3/Contents/x86_64-linux/<Name>.so`, `presets/`, `README.txt`, `CHANGELOG.txt`, modes `0644` / `0755` only,
written as GNU tar headers through `zlib.gzipSync`, the technique of `fixtures/listings/make-bad.js`), replaces `"auto"`
sizes and digests (`sha256:<lower hex>`), applies `mutate(state)` for a defect case, and returns `{deps, calls,
state}`. `deps.gh(argv)` answers `release list`, `release view <tag> --json …` (current state), `release download
<tag> --pattern <asset> --dir <d>` (copies the served bytes) and `release upload <tag> … <files>` (appends the
basenames to that release; a `mutate` hook can make one call fail); `deps.git(['show', '<tag>:plugins/<dir>/<file>'])`
returns the fixture file text (a hook can change it); `deps.exec('pkgutil', ['--expand-full', pkg, dir])` writes two
fat Mach-Os (`helpers/macho.js`) into `dir` and records their arch words; `deps.exec('lipo', ['-archs', file])` echoes
the recorded words (a hook can disagree); `deps.exec('tar', …)` on win32 only creates the directory, on POSIX
delegates to `external-tools.extractTarGz`; `deps.exec('objdump', …)` returns `fixtures/phase2/objdump/glibc-mixed.txt`;
`deps.listDirectory` on win32 returns the tarball's own listing (PD-4), on POSIX is omitted (the real lister runs);
`deps.sign` (set by the test) signs with a throwaway `minisign.js` key and writes `<file>.minisig`; `deps.log` collects
lines. `calls` records every argv in order.

**Test first — `backfill-sign.test.js`**, two top-level tests now (exact names; the other three come in T013):
- `backfill production signer drives rsign2` (spec SC-025, binding): `rsignGate(t)`; `rsignGenerate` into a temp dir;
  `backfillSign.productionDeps(tmp).sign(file, comment, sec)` on a temp file with a C3 comment; the returned text
  equals the `.minisig` on disk, passes `minisign.verifyFile` against the generated `.pub`, and its comment passes
  `contract.checkBinding`.
- `backfill reports defective releases by name`, the plan / download / facts subtests (each on a fresh world), asserting
  `plan.json`, `download-report.json` and `facts-report.<os>.json` contents and exit codes: release missing its `.pkg` →
  plan entry `reported` `E_BACKFILL_ASSET_MISSING` naming the asset; `isImmutable` → `E_BACKFILL_IMMUTABLE`;
  `SHA256SUMS` without `release.json` → `E_BACKFILL_PARTIAL`; a release already carrying `release.json` →
  `skipped-signed`; size off by one → `download-report.json` entry `E_BACKFILL_SIZE`; digest mismatch →
  `E_BACKFILL_DIGEST`; `Iotaplug` (null digests) passes and the log ends `digest checked: 8 of 9` (superseded by CC-verify, 2026-10-30: every fixture asset has a digest, the log reads `9 of 9`, and a null digest is refused with `E_BACKFILL_DIGEST` in its own subtest); tarball without
  `README.txt` → `facts --os linux` writes an `E_LAYOUT` entry and no linux facts file; two Mach-Os with different sets →
  `E_FACTS_ARCH_MISMATCH` entry; no Mach-O → `E_FACTS_NO_MACHO`; disagreeing lipo → `E_FACTS_LIPO_MISMATCH`; a
  `download-report.json` entry is copied unchanged into the facts report; the release list returning 1000 rows → `plan`
  returns 1, `E_BACKFILL_LIST_TRUNCATED`, no plan file; (a) plan half: `Kappaplug` (only a prerelease and a draft, run
  with a temp plugins dir = the nine plus `plugins-extra/Kappaplug`) → `skipped-no-stable`; (b) a stable tag
  `<name>/v1.2` → `plan` returns 1, `E_TAG_FORMAT`, no plan file; (e) a release whose served tarball is a copy of
  `fixtures/listings/bad/dotdot-path.tar.gz`, and one of `symlink-escape-root.tar.gz` → facts report entries with the
  codes in their `.expected-error`, and `calls` holds no `tar` and no `objdump` exec for either; every recorded
  `pkgutil` / `tar` / `objdump` argv is an array (never a shell string). Every report file is a JSON array, `[]` when
  empty.
Run before the module exists: `MODULE_NOT_FOUND` (recorded red).

**Implement** per plan §4.4 (Deps typedef, `productionDeps`, `plan.json` shape, `plan`, `download`, `facts`;
binding) and the tool form: `main(argv, io, deps = productionDeps(<--work or os.tmpdir()>))` with subcommands `plan`,
`download`, `facts`, `apply` (`apply` writes the usage line and returns 2 until T013 implements it); constructing
`productionDeps` spawns nothing (SC-026 calls `main([], io)`). `productionDeps` uses `childProcess.spawnSync(cmd, argv,
{cwd: workDir, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], shell: false, maxBuffer: 64 * 1024 * 1024})` for
`gh`, `git`, the four `exec` tools (anything else throws `TypeError` before spawning) and `rsign`; `sign` runs exactly
`rsign sign -W -s <keyFile> -t <comment> -x <file>.minisig <file>` (the FR-071 shape). Plugins come from
`<pluginsDir>/*/version.json` (ROSTER); the current release is `genFeed.selectReleases(name, rows).stable` (`null` →
`skipped-no-stable`; `E_TAG_FORMAT` aborts, PD-10). Asset names are rebuilt with `contract.assetNameFor`, never taken
from a listing as a path. Linux facts: `releaseFacts.linuxPrecheck` in-process **before** any `tar` / `objdump` exec
(PD-26). No identifier matches `FETCH_RE` (the subcommand is `download`). No plugin name.

**Done when:** L-NODE green for `backfill-sign.test.js` (red first recorded; SC-025 not skipped on this box); L-NODE
whole set green.

### T013 — `backfill-sign.js apply`: three passes, uploads, audit (FR-054 to FR-057a, PD-9, PD-20, PD-23, PD-24, SC-010, SC-011, SC-013, SC-012 rest)

**Files:** `tools/backfill-sign.js` (edit), `tools/feed-contract/tests/backfill-sign.test.js` (edit),
`tools/feed-contract/tests/helpers/fake-gh.js` (edit only if a hook is missing). All three were created by T012.

**Test first — three more top-level tests and the rest of SC-012** (exact names; assertions are spec SC-010, SC-011,
SC-012, SC-013 as amended by T001 and plan §7.1 rows, binding):
- `backfill signs nine current releases`: `plan` → `download --format pkg` and `--format tar.gz` → `facts --os darwin`
  and `--os linux` → `apply` (throwaway-key `deps.sign`, `--public-key` its `formatPublicKey` text) all return 0; per
  plugin the selected tag is the newest stable (never the prerelease, draft or older stable); `calls` shows per release
  exactly two `release upload` calls, `[SHA256SUMS, 3 × .minisig]` then `[release.json]`; 27 uploaded `.minisig` verify
  against the throwaway key and bind (`checkBinding`); 9 `release.json` pass `checkReleaseManifest`; no argv contains
  `--clobber`; no uploaded basename is in that release's `assets_before`; `git show <tag>:plugins/<dir>/version.json`
  and `…/setup.iss` calls are present; the audit passes; `Gammaplug`'s `install` has `preset_subdir: 'Kits'` and
  `macos_pkg_id: 'com.krateaudio.gammakit'`; logs include `digest checked: 9 of 9` (CC-verify); the fake asserts at every `release
  upload` and every audit `release view` that the `--key` file no longer exists (PD-20).
- `backfill second run uploads nothing`: after the first flow, `plan` + `apply` on the mutated state → every plugin
  `skipped (already signed)`, `uploads: 0`, return 0, 0 upload calls.
- `backfill dry run uploads nothing`: the first flow with `--dry-run` → 0 `release upload` argv, nine out dirs each with
  `SHA256SUMS`, three `.minisig`, a C2-valid `release.json`; audit = `assets_before`.
- `backfill reports defective releases by name`, apply subtests (each on a fresh world): the T012 per-release defects
  now also run `apply` → that release `reported <CODE>` in the summary, 0 uploads for it, the other eight `signed`,
  return 3; a facts dir missing one `.darwin.json` with no report entry → `E_FACTS_MISSING`; a `git show` hook
  returning another version → `E_VERSION_MISMATCH`; a missing `facts-report.darwin.json` → 1, `E_INPUT`, 0 uploads;
  a signer whose key differs from `--public-key` → 1, `E_SIGN_KEY_MISMATCH`, 0 uploads in the whole run; a post-run
  asset list gaining a name `apply` did not upload → 1, `E_BACKFILL_ASSETS_CHANGED`; (a) `Kappaplug` → summary
  `Kappaplug: skipped (no stable release)`, nine signed, return 0; (c) the `release.json` upload failing for one release
  with nothing left behind → that release `reported E_INPUT`, the other eight signed, audit clean (expected =
  `assets_before` ∪ the four names of its first call), return 3, and a following `plan` classifies it
  `E_BACKFILL_PARTIAL`; (d) `--public-key` a temp copy of `fixtures/kat/kat.pub` → 1, `E_SIGNING_CONFIG`, 0 uploads.
Run: `apply` is not implemented yet (usage exit 2), so the new tests fail (recorded red).

**Implement** per plan §4.4 "`apply`" steps 1-5 (binding): read the plan and both facts reports (missing → `E_INPUT`
abort); parse `--public-key` and `genReleaseManifest.refuseTestKey` (PD-23); pass 1 per planned release not in a
report: download three assets (FR-053 checks), `git show` both files (D12), `E_VERSION_MISMATCH`, `E_FACTS_MISSING`,
`trustedComments` → `deps.sign` → `toLfMinisig` → **immediately** `minisign.verifyFile` against `--public-key`
(`E_SIGN_KEY_MISMATCH` aborts, PD-9), `buildManifest` with `pubDate = publishedAt` (D9), outputs copied to
`<outDir>/<tag with "/" as "-">/`; `fs.rmSync(key, {force: true})` at the end of pass 1 and in a `finally` around it
(PD-20); pass 2 (live only) two `gh release upload` calls per signed release with absolute paths, asserting before each
call no `--clobber` and no basename in `assets_before`, a failing call reported per PD-24; pass 3 the audit for every
plan release (dry: `assets_before`; live: `assets_before ∪ uploaded`); summary lines `<Name>: signed | skipped (already
signed) | skipped (no stable release) | reported <CODE>: <message>`, then `uploads: <n>`; exit 0 / 3 / 1 per plan §3.3.

**Done when:** L-NODE green for `backfill-sign.test.js` with exactly five top-level tests (red first recorded); L-NODE
whole set green.

---

## G5 — Workflows and keys (sequential; each workflow is a shared file)

### T014 — `release.yml`: facts, signing, manifest, verification, attestation, dry run (FR-040 to FR-050, FR-047a, FR-081, PD-7, PD-11 to PD-13, PD-20 to PD-22, PD-26, SC-014 part 1)

**Files:** `tools/feed-contract/tests/workflow-shape.test.js` (new), `.github/workflows/release.yml` (edit). Written,
never pushed or dispatched.

**Test first — `workflow-shape.test.js`**, top-level test `release.yml and backfill-sign.yml keep the Phase 2 shape`
with the `release.yml` subtests now (the `backfill-sign.yml` subtests come in T015). Helpers in the file (PD-8, plan
§7.1 SC-014 row): `jobBlock(text, job)` (2-space job keys), `stepBlocks(jobText)` (6-space `- name:` items, returning
`{name, text, index}`). Assertions (spec SC-014 `release.yml` part as amended by T001, plan §7.1, binding): `dry_run`
boolean input with default `false`; no input besides `plugin`, `prerelease`, `dry_run`; `Create Release` carries `if:`
on `!inputs.dry_run`, `make_latest: false`, `target_commitish: ${{ github.sha }}`, `fail_on_unmatched_files: true`,
`overwrite_files: false`, its three original asset lines and the four new `files` lines; `deploy-pages` carries `if:`
on `!inputs.dry_run`; `actions/attest-build-provenance@v4` exactly once with an `if:` excluding dry runs and a
`subject-path` naming the three installers and no other file, and no `actions/attest@`; the whole tokens
`secrets.TAURI_SIGNING_PRIVATE_KEY` and `secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD` each exactly once, both in
`Signing configuration and key`, which runs `gen-release-manifest.js --signing-config` with `--public-key
tools/feed-contract/keys/krate-signing.pub` and `minisign.js decrypt-key`, and comes before `Create Release` and every
`rsign sign`; `KRATE_SIGNING_KEY_PASSWORD` never inside a `run:` text; no `signing=off`, no `::warning::` about
unsigned releases, no `continue-on-error`; the `release` job's `needs` include `macos-installer` and `linux-archive`, and
its job-level `if:` (if any) contains none of `always()`, `failure()`, `cancelled()`; a step deleting `krate.key` with
`if: always()` in the `release` job; every plan §4.8 Phase 2 step name exists in its job; `Record macOS facts` contains
`lipo -archs` and `release-facts.js darwin`; `Record Linux facts` contains `release-facts.js linux-precheck` before
`tar -xzf`, then `objdump -T` and `release-facts.js linux`, and `umask 000`; `Verify signatures and sums` contains
`minisign.js verify`, `rsign verify`, `sha256sum -c` and comes before `Create Release`; `Upload dry-run artifact` and
`Verify dry-run artifact` carry `if:` on `inputs.dry_run`; `Delete signing key (after signing)` index = `Sign assets`
index + 1 and lower than `Write release.json and SHA256SUMS` and `Create Release`; `Write release.json and SHA256SUMS`
contains the PD-11 `jq -r .tag` check; `Tag must not exist` carries `if:` on `!inputs.dry_run`, contains `git rev-parse
-q --verify` and `gh release list` with `--limit 1000`, and comes before `Signing configuration and key`; `environment:
signing` on the `release` job (and, checked in T015, on `apply-dry` / `apply-live` and no other job); roster (PD-7): the
Phase 2 step blocks and the `dry_run` input block contain none of the `options:` entries (read from the file's own
`options:` list, `release.yml:34-43`) as case-insensitive whole words. (FR-081's protected ranges are not a
`workflow-shape.test.js` assertion: the test cannot run `git show`, the write guard admits only the `init`, `commit`,
`update-ref`, `tag` git subcommands; they are checked by this task's "Done when" diff and by T020's scope check, SC-020.)
Run against the unchanged `release.yml`: red on the first assertion (recorded).

**Implement** `.github/workflows/release.yml` per plan §4.8 (binding), applying rule 14 (block style wherever a value
holds `${{ }}`, e.g. `env:` / `PLUGIN: ${{ needs.version.outputs.plugin }}` on two lines):
- the `dry_run` input after `prerelease` (`release.yml:45-48`);
- `macos-installer`: between `Build Installer` (ends `:337`) and `Upload Installer` (`:339`): `Setup Node (facts)`,
  `Install feed-contract deps`, `Record macOS facts`, `Upload macOS facts`;
- `linux-archive`: between `Create Archive` (ends `:403`) and `Upload Archive` (`:405`): `Setup Node (facts)`, `Install
  feed-contract deps`, `Record Linux facts` (`umask 000`, `linux-precheck` before `tar`, PD-13, PD-26), `Upload Linux
  facts`;
- `release` job (`:413`): `permissions` `contents: write`, `id-token: write`, `attestations: write`; `environment:
  signing` beside `runs-on`; after `Generate Release Notes` and before `Create Release`, in order: `Tag must not exist`,
  `Download macOS facts`, `Download Linux facts`, `Setup Node (signing)`, `Install feed-contract deps`, `Cache rsign2`,
  `Install rsign2`, `Check rsign2 version`, `Signing configuration and key`, `Sign assets`, `Delete signing key (after
  signing)`, `Write release.json and SHA256SUMS`, `Verify signatures and sums`; `Create Release` gains the five inputs and
  four `files` lines; after it `Attest build provenance`, `Stage dry-run artifact`, `Upload dry-run artifact`, `Verify
  dry-run artifact`, `Delete signing key` (`if: always()`); no job-level `if:` added, `needs` unchanged (`:415`);
- `deploy-pages` (`:531`) gains `if: ${{ !inputs.dry_run }}`, nothing else.
Action pins as the repo uses them (`actions/setup-node@v4`, `actions/cache@v5`, `actions/upload-artifact@v7`,
`actions/download-artifact@v8`, `release.yml:141`, `:182`, `:201`; `.github/workflows/feed-tools.yml:38`).

**Done when:** L-NODE green for `workflow-shape.test.js` (red first recorded); L-YAML on `release.yml` exit 0;
`git diff -U0 .github/workflows/release.yml` shows no `-` line inside `:189-250`, `:285-337`, `:379-403` (FR-081);
L-NODE whole set green.

### T015 — `backfill-sign.yml` (FR-058, FR-059, FR-059a, PD-13, PD-14, PD-20, PD-22, SC-014 part 2)

**Files:** `.github/workflows/backfill-sign.yml` (new workflow, so sequential), `tools/feed-contract/tests/workflow-shape.test.js`
(edit, created by T014). Written, never pushed or dispatched.

**Test first.** Add the `backfill-sign.yml` subtests to `release.yml and backfill-sign.yml keep the Phase 2 shape`
(spec SC-014 `backfill-sign.yml` part and plan §7.1, binding): no `--clobber`; `dry_run` with default `true`; the only
trigger is `workflow_dispatch` (no `push`, `pull_request`, `schedule`); top-level `permissions: contents: read`; jobs
`plan`, `facts-macos`, `facts-linux`, `apply-dry`, `apply-live`; `apply-dry` has `if:` on `inputs.dry_run`,
`permissions: contents: read`, no `contents: write`; `apply-live` has `if:` on `!inputs.dry_run` and is the only job with
`contents: write`; both carry `environment: signing` and no other job does (also the `release.yml` half: only
`release`); both check out with `fetch-depth: 0`; their `steps:` texts are equal after removing the one ` --dry-run`
token from `apply-dry`; `secrets.TAURI_SIGNING_PRIVATE_KEY` and `secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD` (whole
tokens) each exactly twice, once in each apply job's key step, which runs `gen-release-manifest.js --signing-config
--dry-run false --public-key tools/feed-contract/keys/krate-signing.pub` and `minisign.js decrypt-key`; a `Delete signing
key` step with `if: always()` in each apply job; `facts-linux`'s fact step runs `umask 000` first; the file names none
of `release.yml`'s `options:` entries as case-insensitive whole words (ROSTER). Run with no file: red (recorded).

**Implement** `.github/workflows/backfill-sign.yml` per plan §4.9 (job outline, `APPLY_STEPS`; binding) with rule 14:
`name: Backfill sign`; `workflow_dispatch` input `dry_run` (boolean, default `true`); `concurrency` group
`backfill-sign`, `cancel-in-progress: false`; top-level `permissions: contents: read`; `env: GH_TOKEN: ${{ github.token
}}` (block style); `plan` (`ubuntu-latest`, checkout `fetch-depth: 0`, setup-node 24, `npm ci --prefix
tools/feed-contract`, `backfill-sign.js plan --plugins-dir plugins --repo ${{ github.repository }} --out
"$RUNNER_TEMP/plan/plan.json"`, upload `backfill-plan`); `facts-macos` (`macos-latest`, needs `plan`: `download
--format pkg`, `facts --os darwin`, upload `backfill-facts-darwin` with `if-no-files-found: error`); `facts-linux`
(`ubuntu-latest`: `umask 000`, `download --format tar.gz`, `facts --os linux`, upload `backfill-facts-linux`);
`apply-dry` / `apply-live` (`ubuntu-latest`, `needs: [plan, facts-macos, facts-linux]`, `environment: signing`, the
`if:` / `permissions` above) with identical `APPLY_STEPS` (checkout `fetch-depth: 0`; setup-node; `npm ci`; rsign2
cache / install / version check as in T014; download `backfill-plan` and `backfill-facts-*` with `pattern` and
`merge-multiple: true`; key step with the two secrets in `env` as `KRATE_SIGNING_KEY` / `KRATE_SIGNING_KEY_PASSWORD`,
`umask 077`, the signing-config check, `decrypt-key --out "$RUNNER_TEMP/krate.key"`; `backfill-sign.js apply …
[--dry-run]`; upload `$RUNNER_TEMP/out` as `backfill-sign-output` with `if: always()`; `Delete signing key` with `if:
always()`). No plugin name, no `--clobber`, no other trigger (FR-059a).

**Done when:** L-NODE green for `workflow-shape.test.js` (red first recorded); L-YAML on `backfill-sign.yml` exit 0;
L-NODE whole set green.

### T016 — `tools/feed-contract/keys/README.md` (FR-061, FR-062, PD-22, PD-23, SC-029)

**Files:** `tools/feed-contract/keys/README.md` (new), `tools/feed-contract/tests/workflow-shape.test.js` (edit).

**Test first.** Add the top-level test `keys README states the secrets contract and the merge order` (spec SC-029 as
amended by T001 and plan §7.1, binding): the README names `TAURI_SIGNING_PRIVATE_KEY`,
`TAURI_SIGNING_PRIVATE_KEY_PASSWORD`, `tools/feed-contract/keys/krate-signing.pub`, `tauri signer generate` with a
non-empty password, the Phase 11 pass-through of the password, the `signing` environment, `main`, and "environment
secrets"; the merge order by `indexOf`: create the environment and its secrets < commit `krate-signing.pub` < merge,
with the `E_SIGNING_CONFIG` consequence stated after the merge step; no empty-password instruction (regexes for
`-p ''`, `-p ""`, and "empty password" not preceded by "non-"). Run with no README: red (recorded).

**Implement** the README per plan §4.7 sections (1)-(3) (binding) and rule 11 (no secret-key header at a line start,
no serial-flag strings, no base64 key example). It also says: never use a fixture key as `krate-signing.pub` (refused,
PD-23); a real run refuses a version whose tag or release exists (PD-21), so a half-created release is deleted (release
and tag) before re-dispatching and a published version is never re-released; SC-018 / SC-019 are dispatched after the
merge, from `main`.

**Done when:** L-NODE green for `workflow-shape.test.js` with exactly two top-level tests; L-NODE whole set green (the
Phase 1 secret-key scan and `no serial test flags` now also see `keys/`).

---

## G6 — Integration (sequential)

### T017 — Hygiene: FR-072 lists and `Phase 2 tools follow the tools/*.js form` (FR-072, FR-074, FR-076, SC-015, SC-021 clause 2, SC-026)

**Files:** `tools/feed-contract/tests/hygiene.test.js` (edit).

**Test first.** (1) Add the four tools to `scannedFiles()`'s single list (`hygiene.test.js:34-46`), to the completeness
list of subtest (a) (`:110-118`) and to the roster `products` list (`:237-244`); then rerun: green is expected (the
tools were written to rule 2-7); a red names the tool and line to fix **in the tool**, never by loosening the test.
Record the result. (2) New top-level test `Phase 2 tools follow the tools/*.js form` (plan §7.1 SC-026 row, binding):
for each of `tools/minisign.js`, `tools/release-facts.js`, `tools/gen-release-manifest.js`, `tools/backfill-sign.js`:
first line exactly `#!/usr/bin/env node`; `typeof require(<tool>).main === 'function'`; `main([], io)` returns 2 and
stderr starts `usage: node tools/<file>`; every `'E_[A-Z0-9_]+'` string literal in the source is a `CODES` key; and the
27 FR-074 codes are `CODES` keys with class `'none'`; subtest (SC-021 clause 2): every `tools/feed-contract/tests/**/*.js`
file has 0 matches of `/concurrency:\s*(1|false)\b/`. Red first: run the new test with one tool name misspelled in the
list (`MODULE_NOT_FOUND`), then fix the list (record both).

**Implement.** Test-only change; product fixes only where a red names a real defect.

**Done when:** L-NODE green for `hygiene.test.js` (6 top-level tests); `git diff -U0 tools/feed-contract/tests/hygiene.test.js`
shows only additions (T005's and this task's; no Phase 1 assertion removed or loosened, SC-016); L-NODE whole set green.

### T018 — `CONTRACT.md` §9 "Phase 2 tools" (FR-074)

**Files:** `tools/feed-contract/CONTRACT.md` (edit: append a section after §8, `tools/feed-contract/CONTRACT.md:474`).

**Test first.** Before editing, `grep -c '^## 9. Phase 2 tools$' tools/feed-contract/CONTRACT.md` prints `0` (the
recorded red); the regression tests that must stay green after the edit are `CONTRACT.md documents every code and
field` (reads codes from §7 only, `hygiene.test.js:359-394`), the §7 exit-code item check (`:395-401`), the duplicate
heading rule (`:301-316`), `no serial test flags` (`:265-277`) and the Phase 1 secret-key scan.

**Implement.** Append `## 9. Phase 2 tools` holding, per plan §4.6 (binding): the four CLIs with their exact usage
lines (as each tool prints them); the plan §3.3 exit-code table (as a table, not as `- \`0\`: …` items, so nothing
resembles §7's list); the `.minisig`, public-key and secret-key byte layouts of plan §4.1 with the minisign source
lines cited there (offsets only, no base64 example, rule 11); the darwin and linux facts JSON shapes; the `plan.json`
shape; the `SHA256SUMS` form (D7); the signing-configuration rule (D6, PD-23). The eight `CONTRACT_HEADINGS` stay
unchanged.

**Done when:** the `grep -c` prints `1`; L-NODE whole set green.

### T019 — `feed-tools.yml`: path filter and the TAP check; `evidence/tap-check.js` (FR-073, PD-15, PD-27, SC-017 local half, SC-021)

**Files:** `.github/workflows/feed-tools.yml` (edit), `specs/krate-manager-phase2/evidence/tap-check.js` (new).

**Test first.** Write `tap-check.js` with `--self-test` first: it builds four synthetic TAP texts in memory and runs the
extracted check on each with `CI = '1'` in its sandbox: (1) plan `1..46`, `# fail 0`, the 22 names as `ok` lines →
pass; (2) one name missing → fail naming it; (3) a `# SKIP` on a subtest line inside the `minisign.js decrypts the
rsign2 secret-key KAT` block → fail (PD-27); (4) plan `1..45` → fail. Run `--self-test` against the current
`feed-tools.yml`: it fails (no `<<'TAPCHECK'` heredoc to extract) — the recorded red.

**Implement.**
- `feed-tools.yml`: both `paths:` lists (`.github/workflows/feed-tools.yml:5-21`) gain `tools/release-facts.js`,
  `tools/backfill-sign.js`, `.github/workflows/release.yml`, `.github/workflows/backfill-sign.yml`; the `node --test`
  step's `node -e '…'` block (`:64-73`) is replaced by the heredoc of plan §4.10 (binding), with `NAMES` = the 22 exact
  test names of plan §7.1 (SC-001 to SC-014, SC-022 to SC-029) and `RSIGN` = the SC-003, SC-025 and SC-027 names.
  Everything else in the file is unchanged. No serial flag.
- `tap-check.js` per plan §4.10 last paragraph: reads `feed-tools.yml`, extracts the text between the `<<'TAPCHECK'`
  line and the closing `TAPCHECK` line, de-indents it by the step's indentation, and runs it with
  `vm.runInNewContext` over the TAP file given as its argument, with a sandbox `process` whose `env` is `{CI: '1'}` and
  whose `argv[2]` is the file, and a `require` that admits only `fs`; exit 0 / 1 as the script decides. Header in the
  `specs/krate-manager-phase1/evidence/scope-check.js:1-11` style.

**Done when:** `node specs/krate-manager-phase2/evidence/tap-check.js --self-test` exit 0 (red first recorded); L-YAML
on `feed-tools.yml` exit 0; `grep -n 'test-concurrency\|test-isolation' .github/workflows/feed-tools.yml` prints
nothing; the full L-NODE run then L-TAP exit 0 (plan `1..46`, 22 names, no skip in the three rsign blocks); L-LINT exit 0.

### T020 — Scope check, specs index, repo lints, the close run and the PENDING-CI list (SC-016, SC-020, SC-021, FR-080 to FR-082)

**Files:** `specs/krate-manager-phase2/evidence/scope-check.js` (new), `specs/INDEX.md` (regenerated).

**Test first.** `scope-check.js --self-test` (form of `specs/krate-manager-phase1/evidence/scope-check.js`): synthetic
inputs (one clean status + diff; one with `?? tools/stray.js`; one with a `release.yml` hunk deleting a line at pre-image
line 300; one with `+` lines in `tools/gen-feed.js` beyond the single `selectReleases,` line) → clean passes, the other
three print the offence and exit 1. Run before the filter exists: red (recorded).

**Implement `scope-check.js`** (plan §4.13, binding): base `1dec384c`; `git -c core.quotepath=off status --porcelain
--untracked-files=all` and `git diff --name-only 1dec384c` paths against the FR-080 list (exact:
`tools/minisign.js`, `tools/release-facts.js`, `tools/gen-release-manifest.js`, `tools/backfill-sign.js`,
`tools/gen-feed.js`, `.github/workflows/release.yml`, `.github/workflows/backfill-sign.yml`,
`.github/workflows/feed-tools.yml`, `specs/INDEX.md`; prefixes `tools/feed-contract/`, `specs/krate-manager-phase2/`);
no Phase 1 fixture path changed (`tools/feed-contract/fixtures/` outside `fixtures/phase2/`); `git diff -U0 1dec384c --
.github/workflows/release.yml`: any `-` line, or a hunk whose pre-image start falls inside `:189-250`, `:285-337`,
`:379-403`, is a protected-range violation; `git diff -U0 1dec384c -- tools/gen-feed.js` adds exactly `selectReleases,`;
`git diff -U0 1dec384c -- tools/feed-contract/tests/{helpers/write-guard.js,hygiene.test.js,c3-trusted-comment.test.js}`
shows deletions only at `c3-trusted-comment.test.js:275` and the `write-guard.js` lines `checkSpawn` replaces (list them
in the script; SC-016). Prints counts; exit 1 on any. The user-committed `keys/krate-signing.pub` is in scope via the
prefix (SC-020).

**Run in this task (each short; record wall clock):**
1. `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > f:/tmp/km2-all.tap` (one parallel run;
   wall clock, `# tests`, `# pass`, `# fail` into the notes);
2. L-TAP on it (plan `1..46`, `# fail 0`, the 22 names, no skip in the SC-003 / SC-025 / SC-027 blocks: D13, a local
   skip leaves those rows open);
3. L-YAML on `release.yml`, `backfill-sign.yml`, `feed-tools.yml`;
4. L-LINT (`node tools/lint-serial-test-runs.js`, exit 0);
5. `node tools/gen-specs-index.js`, then `node tools/gen-specs-index.js --check` (exit 0);
6. `scope-check.js --self-test`, then `scope-check.js` (exit 0, nothing out of scope).
Not run, with reason in the notes: `node tools/check-portability.js` (no C++ file changed, FR-082),
`node tools/lint-plugin-editor-baseline.js` (no plugin editor changed), `run-gate.js` / `header-consumers.js` (no C++).

**Done when:** every command above passes; both evidence scripts pass `node --check`; the PENDING-CI table below
matches the job and step names in `.github/workflows/feed-tools.yml`, `release.yml` and `backfill-sign.yml` (grep each
name; job display names `macOS Installer`, `Linux Archive`, `Create Release`, `Deploy Landing Page`,
`release.yml:253-254`, `:347-348`, `:413-414`, `:531-532`).

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

`feed-tools.yml` runs on the user's push of the feature branch (its path filter now lists every Phase 2 file).
`release.yml` and `backfill-sign.yml` are dispatched by the user **after the merge to `main`** (FR-062 step 4; the
`signing` environment admits only `main`, PD-22; GitHub dispatches only workflow files on the default branch,
FR-059a).

| SC | Workflow · job (exact name) · step | Assertion that closes the row |
|---|---|---|
| SC-003, SC-025, SC-027 (rsign subtest) | `feed-tools.yml` · `node-test (ubuntu-latest)`, `node-test (macos-latest)`, `node-test (windows-latest)` · `node --test` | the heredoc TAP check passes with `CI` set: plan `1..46`, `# fail 0`, the 22 names `ok`, no top-level `# SKIP`, no `# SKIP` on any line of the three rsign blocks (PD-27) |
| SC-004 symlink subtest, SC-008 POSIX half (extracted KAT `payload` equality and `E_FACTS_LISTING_MISMATCH`), SC-010 to SC-013 with real `tar` and the real lister, SC-028 mode `0600` and symlinked-parent subtests | `feed-tools.yml` · `node-test (ubuntu-latest)`, `node-test (macos-latest)` · `node --test` | the same TAP check; those subtests ran (not skipped) on both POSIX legs |
| SC-028 success through a non-canonical temp path (PD-19) | `feed-tools.yml` · `node-test (macos-latest)` (`/var` → `/private/var`), `node-test (windows-latest)` (8.3 `RUNNER~1`) · `node --test` | `ok … - minisign.js decrypt-key writes only under RUNNER_TEMP and cleans up` |
| SC-017 | `feed-tools.yml` · all three `node-test (…)` legs | steps `node --test` (TAP check), `rsign verify (KAT)` and `Checkout unchanged` green |
| SC-018 | `release.yml` · `workflow_dispatch`, one plugin, `dry_run: true`, from `main` · jobs `macOS Installer` (step `Record macOS facts`), `Linux Archive` (step `Record Linux facts`), `Create Release` (steps `Verify signatures and sums`, `Upload dry-run artifact`, `Verify dry-run artifact`) | those steps green; `platform_key`, `min_os` and the glibc floor printed and recorded in compliance; artifact `release-dry-run` holds three assets, three `.minisig`, `SHA256SUMS`, `release.json`, `krate.pub`; steps `Tag must not exist`, `Create Release`, `Attest build provenance` and job `Deploy Landing Page` skipped; `git ls-remote --tags origin` and `gh release list --limit 1000 --json tagName,isDraft,isPrerelease,publishedAt` byte-identical before and after (user check) |
| SC-019 | `backfill-sign.yml` · `workflow_dispatch`, `dry_run: true`, from `main`, after the user created the `signing` environment, both secrets and committed `krate-signing.pub` · jobs `plan`, `facts-macos`, `facts-linux`, `apply-dry` (`apply-live` skipped) | `plan` lists nine plugins × three assets; `facts-report.darwin.json` and `facts-report.linux.json` are `[]`; `apply-dry` summary nine `signed`, `uploads: 0`, exit 0; artifact `backfill-sign-output` holds nine dirs each with a C2-valid `release.json`, `SHA256SUMS`, three `.minisig` verified against the committed key; audit clean; the `digest checked: <n> of <m>` lines recorded (PD-28); the log shows the key file removed before the audit (PD-20). Any reported release, exit 3 or fewer than nine planned fails the row |

Every Windows row closes locally from T020's run (TAP file, L-TAP, L-YAML, L-LINT, scope check).

## Main-loop rulings and fixes at close (2026-10-30)

- **CC-verify (user ruling: refuse):** `checkDownloaded` (`tools/backfill-sign.js`) raises `E_BACKFILL_DIGEST` ("has no
  digest on GitHub") for a null digest, so such an asset is never unpacked, measured or signed. Measured first: 264 stable
  installer assets, 0 null digests. The `state.json` fixture now gives every asset a digest (`digest checked: 9 of 9`),
  and the subtest `an asset GitHub lists without a digest is refused with E_BACKFILL_DIGEST, never unpacked or signed`
  was red before the code change, green after. FR-053 and Clarifications amended.
- **FR-061:** the roadmap names `tools/feed-contract/keys/krate-signing.pub` (Repository Layout, Phase 7, Phase 11 with
  `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`) and lists `tools/release-facts.js`. FR-080 and `scope-check.js` now allow the
  roadmap for these lines (main loop only).
- **TAP plan:** remediation had added a 47th top-level test (`backfill production git runs in --plugins-dir, not in
  --work`). It is now a subtest of SC-025 `backfill production signer drives rsign2` (with the signer half as a second
  subtest, rsign-gated), so `tap-check.js` reads `TAP plan=1..46 top-level=46`. Full suite: 547 tests, 542 pass,
  0 fail, 5 skipped (POSIX-only, PENDING-CI).
