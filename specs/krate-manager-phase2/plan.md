# Implementation Plan: Krate Manager Phase 2 — Release Assets, Hashes, Signatures and the Backfill

**Spec:** `specs/krate-manager-phase2/spec.md` (DRAFT, reviewed, Clarifications Q1-Q7 of 2026-10-30)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 2 (lines 335-416); Cross-Cutting Constraints (lines 954-995)
**Status:** DRAFT plan, 2026-10-30, revised after review round 1 (Review notes R-1 to R-18), verified against `feat/krate-manager` at `1dec384c` (untracked: `specs/krate-manager-phase2/`)
**Lanes:** pipeline lane only (Node tools, YAML, fixtures). No app code, no Tauri, no Rust, no C++ in this phase.

---

## 0. What this plan adds to the spec

The spec fixes the requirements and the success criteria. This plan fixes the module boundaries, the exact public APIs,
the byte layouts the Node code reads and writes, the workflow steps, the test-file map and the gate lane. Where the plan
had to depart from a spec sentence, or decide something the spec leaves open, it is listed here and repeated at its use
site.

### 0.1 Plan decisions

| Id | Plan decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | `tools/minisign.js` carries its own ~70-line BLAKE2b (RFC 7693) with a configurable digest length, used **only** for the 32-byte secret-key checksum. The file prehash (Blake2b-512) stays `crypto.createHash('blake2b512')`. | Measured 2026-10-30 on this box (Node v24.7.0, OpenSSL 3.5.2): `crypto.createHash('blake2b512', {outputLength: 32})` throws `ERR_OSSL_EVP_NOT_XOF_OR_INVALID_LENGTH`. BLAKE2b-256 is not a truncation of BLAKE2b-512 (the digest length is in the parameter block, RFC 7693 §2.5), and minisign's checksum is `Blake2b::new(CHK_BYTES = 32)` (`minisign-0.9.1/src/secret_key.rs:96-104`, `constants.rs:20`). | FR-001 ("over `node:crypto`, `node:fs` and `contract.js` only"): still true, no new module or dependency; the function is in-file |
| PD-2 | `decryptSecretKey` ports minisign's `raw_scrypt_params` exactly and calls `crypto.scryptSync(password, salt, 104, {N, r, p, maxmem: 2 * 128 * N * r})`. It rejects (`E_MINISIG_SECRET_KEY`) `memlimit > 1 GiB`, `log2(N) > 20` (minisign's own rejections) and a cost `128 * N * r * p > 2 GiB` (the spec's ceiling). | `minisign-0.9.1/src/helpers.rs:45-74` (`raw_scrypt_params`), `secret_key.rs:130-138`, `constants.rs:26-29` (`MEMLIMIT 33_554_432`, `OPSLIMIT 1_048_576`, `MEMLIMIT_MAX 1_073_741_824`, `N_LOG2_MAX 20`). For the default key the port gives N = 2^15, r = 8, p = 1. Measured: `scryptSync` with those parameters **fails** with the default `maxmem` and with `maxmem` 32 MiB (`memory limit exceeded`) and passes in 47 ms with 64 MiB. | FR-009a ("a `maxmem` large enough"): made concrete |
| PD-3 | "A secret key whose public half does not match its key id" (FR-009a) is implemented as: the 32-byte public half stored in the secret key (`sk[32..64]`) must equal the public key derived from the seed (`sk[0..32]`). | minisign's 64-byte secret key is seed ‖ public key (`minisign-0.9.1/src/crypto/ed25519.rs:57-70`, `keypair.rs:29-33`); the key id is random (`keypair.rs:33-34`) and cannot be derived from the key. | FR-009a wording |
| PD-4 | `linuxFacts` takes an optional second argument `{listDirectory}` (default `listing.listingFromDirectory`), and `backfill-sign.js` `deps` gains an optional `listDirectory`. On win32 the backfill tests (SC-010 to SC-013) pass a lister that returns the tarball's own listing; on POSIX legs they use the real extraction (real `tar -xzf` through the test helper) and the real lister. | `listingFromDirectory` throws `E_LISTING_NO_POSIX_MODES` on win32 (`tools/feed-contract/listing.js:290-293`), yet SC-010 runs `facts --os linux` on Windows. Without an injection point SC-010 cannot pass locally. The extracted-tree comparison itself is still proven by SC-008 on the POSIX legs and by the `release.yml` dry run. | FR-015, FR-051 (deps shape), SC-010 (see SI-1) |
| PD-5 | Every test that spawns a real tool (`rsign`, `tar`) does so through one helper, `tools/feed-contract/tests/helpers/external-tools.js`. `CHILD_EXEMPT` gains exactly two entries: `tools/backfill-sign.js` (FR-072) and that helper. | `hygiene.test.js:65-69` flags `child_process` in **every** scanned file, test files included; SC-003, SC-008 (POSIX half), SC-025 and SC-027 spawn tools from three test files. One exempt helper keeps the exemption list short and the shapes in one place. | FR-072 (see SI-2) |
| PD-6 | The FR-060 one-path secret-key exemption is made in `tools/feed-contract/tests/c3-trusted-comment.test.js:275`, where the Phase 1 FR-024 scan lives, not in `hygiene.test.js`. | `c3-trusted-comment.test.js:206-275` holds `secretKeyRules` / `findSecretKeyFiles` and the `assert.deepEqual(findSecretKeyFiles(CONTRACT_DIR), [])` the KAT key would trip (rules (a) `.key`, (b) `rsign encrypted secret key`, (c) `Ed…B2` blob). `hygiene.test.js` has no secret-key scan (grep, this session). | FR-072, SC-015, SC-016 (see SI-3) |
| PD-7 | The SC-014 roster check on `release.yml` is scoped to the lines this phase adds: the step blocks whose `name:` is in the plan's fixed Phase 2 step list (§4.8) plus the `dry_run` input block. `backfill-sign.yml` is checked whole. | The unchanged `release.yml` already names plugins outside `options:`: `:22` (comment), `:44` (`default: iterum`), `:144` (FetchContent cache key), `:161`, `:163` (preset-generator `case`). A whole-file check fails on lines FR-081 forbids touching. | SC-014 (see SI-4) |
| PD-8 | The workflow-shape tests read YAML as text with an indentation-based block extractor (`jobBlock`, `stepBlocks`), not with a YAML library. | No YAML package in `tools/feed-contract/package.json` (ajv 8.20.0 only); both files use 2-space jobs, 4-space `steps:`, 6-space `- name:` (`release.yml:57`, `:413`, `:450`). A new dependency for two owned files is not worth it. | none |
| PD-9 | `apply` runs in three passes: (1) per planned release: facts, downloads, `git show`, sign, **verify each new signature against `--public-key` at once** (a failure is `E_SIGN_KEY_MISMATCH`, abort), `buildManifest`; (2) uploads; (3) the FR-057a audit. Nothing is uploaded before pass 1 has finished for every release. | `buildManifest` itself verifies against `--public-key` (FR-024) and would turn a wrong secret into nine per-release `E_MINISIG_KEY_ID` reports (exit 3) instead of the abort FR-055 requires. Checking right after `deps.sign` gives the abort its own code. | FR-054, FR-055 ordering made concrete |
| PD-10 | `plan` aborts with `E_TAG_FORMAT` (exit 1, plan file not written, 0 uploads) when `gen-feed.selectReleases` throws it; a `null` result (no stable release, `gen-feed.js:440`) is the `skipped-no-stable` entry, never dereferenced. Both are SC-012 subtests (§7.1). | `selectReleases` throws `E_TAG_FORMAT` for a malformed `<p>/v…` tag (`tools/gen-feed.js:418-433`) and returns `null` when there is no stable release (`:440`); the backfill must pick exactly what gen-feed would, and gen-feed aborts on it (`CONTRACT.md` §7, `E_TAG_FORMAT` class `abort`). | FR-052 (unstated case), FR-057 abort list (see SI-9) |
| PD-11 | The `release` job adds one check after writing the manifest: `jq -r .tag installers/release.json` must equal `needs.version.outputs.tag`. | The workflow tags `<dir>/v<ver>` (`release.yml:73-76`, `PLUGIN` is the directory name); `checkReleaseManifest` requires `<name lower-cased>/v<ver>` (`contract.js` `checkReleaseManifest`, step 6). Today the two agree for all nine plugins, but a disagreement would ship URLs pointing at a tag that does not exist. | none (an added assertion) |
| PD-12 | Attestation runs **after** `Create Release`, not before. | An attestation is written to a public log (spec D10); attesting first would leave a provenance record for assets of a release that then failed to be created. A failed attestation after a created release loses only the human-facing provenance (roadmap line 372: "never consulted by the manager"). | FR-046 (order unstated) |
| PD-13 | Both fact-recording steps (`Record Linux facts` in `release.yml`, the `facts-linux` job of `backfill-sign.yml`) run `umask 000` before extracting, so the extracted modes are the archive's modes. | GNU tar run by a non-root user applies the umask to extracted modes unless `-p` is given; a tarball member with mode `0775` would extract as `0755` under umask `022` and trip `E_FACTS_LISTING_MISMATCH` on correct data. The test-side tar shape stays `['-xzf', src, '-C', dest]` (`write-guard.js:218-223`). | FR-042, FR-053a (exec args unchanged) |
| PD-14 | The `apply-dry` and `apply-live` jobs check out with `fetch-depth: 0`. | D12 reads `git show <tag>:plugins/<dir>/version.json`; a shallow checkout has no tags. FR-058 names `fetch-depth: 0` for `plan` only. | FR-058 |
| PD-15 | The FR-073 TAP check is a bash heredoc (`node - <<'TAPCHECK'`) in the `node --test` step, and `specs/krate-manager-phase2/evidence/tap-check.js` runs the **same** script locally by extracting the heredoc body from `feed-tools.yml`. | One of the 22 names contains an apostrophe (`Linux payload digest equals the extracted tree's listing`, SC-008), which cannot sit inside today's single-quoted `node -e '…'` (`feed-tools.yml:63-72`). Extracting the body keeps the local close and CI on one rule. | FR-073 ("carried in the step's check script": still true) |
| PD-16 | `decrypt-key` writes the unencrypted key in the layout `generate_unencrypted_keypair` writes: `kdf_alg`, salt, opslimit and memlimit all zero, the plaintext checksum, untrusted comment `krate decrypted secret key (runner temp only)`. | `minisign-0.9.1/src/keypair.rs:29-55`; rsign 0.6.7 `cmd_sign` tries `into_unencrypted_secret_key` first (`rsign2-0.6.7/src/bin/rsign/main.rs:103-113`), which checks only `kdf_alg == KDF_NONE` and the checksum (`secret_key.rs:237-243`, `:279-292`). | FR-009b (layout made concrete) |
| PD-17 | The SC-003 / SC-027 assertions on `rsign verify` read **stderr**. | rsign/minisign print `Signature and comment signature verified` and `Trusted comment: <tc>` with `eprintln!` (`minisign-0.9.1/src/lib.rs:202-204`). | SC-003 ("prints its trusted comment") |
| PD-18 | `minisign.js` exports one extra member, `_internal: {blake2b, rawScryptParams}`, for the SC-027 anchors only; no tool or production caller uses it. | The anchors (BLAKE2b at lengths 0/1/127/128/129/1000 and the RFC 7693 vector; the scrypt-parameter known answer) cannot be written against private functions, and `decryptSecretKey` only ever hashes a fixed 74-byte input, so a length-dependent BLAKE2b bug would be invisible through it. SC-026 checks the first line, `main`, the usage exit and the code literals (spec SC-026), so an extra export does not trip it. | FR-001 export list (see SI-7) |
| PD-19 | `decrypt-key --out` containment compares **canonical** paths on both sides (the write guard's `canonicalFollow` / `fold` rule, `tools/feed-contract/tests/helpers/write-guard.js:40-41`, `:77-110`): raw `--out` with a `..` segment → `E_INPUT`; the parent directory must exist (else `E_INPUT`); `realpathSync.native(dirname(resolve(out)))` joined with the basename must equal or sit under `realpathSync.native(RUNNER_TEMP \|\| os.tmpdir())`, case-folded on win32; an existing entry at the canonical out path (`lstat`, any type) → `E_INPUT`. The temp file and the rename use the canonical path. | `path.resolve` against a realpath'd root rejects the valid macOS path (`/var/folders/…` vs `/private/var/folders/…`) and the Windows runner's 8.3 `RUNNER~1` temp path, and cannot see a symlinked directory under `RUNNER_TEMP` that points outside it. | FR-009b wording made concrete; SC-028 gains subtests (see SI-8) |
| PD-20 | The decrypted key lives only while signatures are made. `release.yml`: `Delete signing key (after signing)` runs directly after `Sign assets`; the `if: always()` `Delete signing key` at the end stays as the backstop; no later step runs while `krate.key` exists. `backfill-sign.js apply` removes the `--key` file itself as soon as pass 1 ends (and in a `finally` on any abort inside pass 1), so passes 2 and 3 (uploads, audit) run without it; the workflow's `if: always()` delete stays as the backstop. | Steps after signing (`Write release.json`, verify, `softprops/action-gh-release@v2` at `release.yml:516`, `actions/attest-build-provenance@v4`) are third-party or need only `krate.pub`; this key is the trust root of every installer the Phase 10 helper runs and of manager self-update (roadmap Key decision 4). | FR-045, FR-054 (order made concrete; see SI-10) |
| PD-21 | A real `release.yml` run refuses a tag that already exists: new step `Tag must not exist` (`if: ${{ !inputs.dry_run }}`, first Phase 2 step of the `release` job) fails when `git rev-parse -q --verify refs/tags/$TAG` succeeds or when the tag is in `gh release list --limit 1000 --json tagName` (drafts included; 1000 rows = fail, the `E_BACKFILL_LIST_TRUNCATED` rule; a `gh` failure fails the step: fail closed). `Create Release` also gets `overwrite_files: false` as a second layer. | The `version` job builds `TAG` from `version.json` with no existence check (`release.yml:72-76`); softprops v2.6.2 `overwrite_files` "Overwrite existing files with the same name. Defaults to true" (§1.2), so a re-dispatch of a released version would swap published, signed assets. A dry run creates nothing, and SC-018 is dispatched on a version that may already be released, so the check skips on dry runs. | FR-047a-adjacent addition; SC-014 gains assertions (see SI-10) |
| PD-22 | Both signing secrets are **environment secrets** of a GitHub Environment `signing` whose deployment-branch policy is "Selected branches and tags: `main`"; the `release` job and both `apply-*` jobs carry `environment: signing`. Consequence: every `release` job run, dry or real, starts only from `main` (SC-018 and SC-019 are dispatched after the merge, so they are unaffected). | Without it, a `workflow_dispatch` of an edited copy of either workflow on any branch receives the repository secrets. GitHub docs: "Secrets stored in an environment are only available to workflow jobs that reference the environment"; "Selected branches and tags: Only branches and tags that match your specified name patterns can deploy to the environment", matched against `GITHUB_REF` (https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments, fetched 2026-10-30). | FR-058, FR-061, FR-062 (user step), SC-014, SC-029 (see SI-10) |
| PD-23 | `signingConfig` (real mode) and `backfill-sign.js apply` refuse with `E_SIGNING_CONFIG` a public key whose key id **or** 32-byte Ed25519 key equals that of `fixtures/kat/kat.pub` or `fixtures/phase2/kat-secret/kat.pub` (read relative to `__dirname`; either file unreadable in real mode → `E_SIGNING_CONFIG`, fail closed). | The repo carries a test secret key with a published password (FR-064) and the Phase 1 KAT key id `440FA75168B852F7`; a `krate-signing.pub` copied from either would pass every release check and ship assets signed with a key anyone can use. Comparing the key bytes as well as the id catches a relabelled copy. | FR-029 (one more refusal), SC-022 7 failures, FR-057 abort list (see SI-9) |
| PD-24 | A failing `gh release upload` in `apply` pass 2 follows FR-057: that release is reported `E_INPUT` (naming `gh release upload <tag>` and the status, never a path's contents), its remaining upload for that release is not made (release.json is always last, D8), the other releases continue, the pass 3 audit still runs, exit 3. If the failed call left some of its names on the release, the audit reports `E_BACKFILL_ASSETS_CHANGED` (exit 1), and a rerun's `plan` classifies the release `E_BACKFILL_PARTIAL`. | FR-057 lists aborts and "a gh failure outside a single release"; an upload is inside one release. Reusing `E_INPUT` (as `facts` does for a non-zero `exec`) keeps FR-074's 27 codes. | FR-056, FR-057 (made concrete) |
| PD-25 | Manifest mode writes its two outputs as one unit: both temp files (`wx`, same directory) first; then each target is `lstat`-checked (absent or a regular file, else `E_INPUT`, nothing renamed); then an existing `SHA256SUMS` is renamed to a backup name, the new `SHA256SUMS` renamed in, `release.json` renamed in; if that last rename fails, the new `SHA256SUMS` is removed and the backup renamed back (rename keeps bytes and `mtimeMs`); every temp and backup is removed in a `finally`. | Two sequential `writeAtomic` calls leave a replaced `SHA256SUMS` beside an old `release.json` when the second write fails (the spec's "Disk full / unwritable output" edge case), which FR-026 forbids. | FR-026 (made concrete), SC-007 gains two subtests |
| PD-26 | Downloaded or built Linux tarballs are listed and layout-checked **before** anything extracts them. `release-facts.js` gains mode `linux-precheck --tarball <file>` (`listingFromTarGz` + the single-bundle rule + `checkLinuxLayout`; exit 0 silent, exit 1 with the code); `Record Linux facts` runs it before `tar -xzf`; `backfill-sign.js facts` (linux) runs the same functions in-process first, and runs `objdump` only when the listing holds `<Name>.vst3/Contents/x86_64-linux/<Name>.so` as a regular-file entry. | Extracting first left the archive's safety to GNU tar's defaults and opened a `.so` path that could be a link; `listingFromTarGz` already rejects absolute paths, `..` and escaping links (`listing.js:277`). | FR-010, FR-013 (one more mode), FR-053a order (see SI-11) |
| PD-27 | The TAP check (PD-15) fails on CI when any line inside the SC-003, SC-025 or SC-027 test block (from its column-0 `# Subtest: <name>` line to its own `ok N - <name>` line) carries `# SKIP`, besides the top-level rule; `evidence/tap-check.js` runs the extracted script with `process.env.CI = '1'` in its sandbox, so the local close applies the same strict rule. | D13 ("A skipped local run does not close the local row") was not enforced: the script's skip rule ran only with `CI` set and only on column-0 lines, and SC-027's rsign case is a subtest. | FR-073 (rule made concrete), D13 |
| PD-28 | GitHub's per-asset `digest` is a best-effort integrity check; `size` is the guaranteed one. `download` and `apply` log `digest checked: <n> of <m>` per format, and SC-019's evidence records those lines. | The REST release-asset schema has `digest` "required, string or null" (https://docs.github.com/en/rest/releases/assets, fetched 2026-10-30); gh exports it as nullable (`pkg/cmd/release/shared/fetch.go`, spec External facts line 169). No docs page gives a minimum gh version or says old assets carry a digest. | FR-053 (null branch already in the spec). **Superseded 2026-10-30 (spec Clarifications, CC-verify):** a null digest is refused with `E_BACKFILL_DIGEST`; it is never unpacked or signed. |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before build)

Each is resolved in this plan as stated; the spec text should follow so the compliance rows match.

- **SI-1 (SC-010 on Windows):** SC-010 runs `facts --os linux` locally, but `linuxFacts` reads the extracted tree with
  `listingFromDirectory`, which throws on win32 (`listing.js:290-293`). Resolved by PD-4; SC-010's Windows run uses an
  injected lister and its POSIX legs use the real one. Suggested spec edit: FR-051 deps gain optional `listDirectory`;
  SC-010 says which lister each leg uses.
- **SI-2 (CHILD_EXEMPT):** FR-072 exempts only `backfill-sign.js`, but three new test files spawn `rsign` / `tar`, and the
  offline scan covers test files (`hygiene.test.js:34-46`, `:65-69`). Resolved by PD-5.
- **SI-3 (where the secret-key scan is):** FR-072, SC-015 and SC-016 place the FR-060 exemption in `hygiene.test.js`; the
  scan is in `c3-trusted-comment.test.js:206-275`. Resolved by PD-6; SC-016's "`git diff` of those two files" becomes
  three files (`write-guard.js`, `hygiene.test.js`, `c3-trusted-comment.test.js`).
- **SI-4 (SC-014 roster on `release.yml`):** the whole-file check fails on unchanged lines `:22`, `:44`, `:144`, `:161`,
  `:163`. Resolved by PD-7.
- **SI-5 (Review notes are stale):** the spec's Review notes say the TAP threshold is "43" and that `target_commitish` is
  "left to the user (Open questions)"; FR-073 says 46 (24 + 22, recounted this session: `^test(` per file sums to 24) and
  Q5 ruled `target_commitish`. The plan uses 46 and Q5.
- **SI-6 (kat.key generation needs a terminal):** FR-064 has rsign2 make the encrypted KAT key "at a terminal with a
  password". rsign reads passwords only through `rpassword` (`/dev/tty` / `CONIN$`, spec External facts), so no agent
  can do it unattended. The plan makes it a user step (§4.12.3, Open question 1).
- **SI-7 (FR-001 export list):** PD-18 adds `_internal: {blake2b, rawScryptParams}`. Suggested spec edit: FR-001 lists
  `_internal` as a test-only export; SC-027 names the BLAKE2b and scrypt-parameter anchors.
- **SI-8 (FR-009b / SC-028 containment):** PD-19. Suggested spec edit: FR-009b states the canonical comparison, the
  existing-parent and no-existing-entry rules; SC-028's success case runs with `RUNNER_TEMP` exactly as
  `fs.mkdtempSync(path.join(os.tmpdir(), …))` returns it (un-canonicalised), and its failures become 7: the four listed,
  plus a missing parent directory, an existing out entry, and (POSIX legs) a symlinked directory inside `RUNNER_TEMP`
  pointing outside it. SC-027 gains three rejection subtests (CRLF accept, cost limit, public-half mismatch, §7.1).
- **SI-9 (FR-057 abort list, FR-029):** add `E_TAG_FORMAT` (`plan`, PD-10) and `E_SIGNING_CONFIG` (`apply`, PD-23) to the
  abort list; state that an upload failure is a per-release report (PD-24); SC-022 becomes 4 + 1 + 7 (the KAT public key
  refused); SC-012 gains the no-stable, `E_TAG_FORMAT`, upload-failure, KAT-public-key and unsafe-tarball subtests.
- **SI-10 (release job and backfill key lifetime, existing tags, environment):** PD-20, PD-21, PD-22. Suggested spec
  edit: FR-045 / FR-054 say the key is deleted directly after the last signature; a new FR (or FR-047a addition) for
  `Tag must not exist` and `overwrite_files: false`; FR-058 / FR-061 / FR-062 name the `signing` environment and its
  main-only policy as a user step; SC-014 and SC-029 gain the matching assertions (§7.1).
- **SI-11 (FR-010 / FR-013 / FR-053a):** PD-26 adds `release-facts.js linux-precheck` and moves the listing check before
  extraction in both places.
- **SI-12 (SC-006, SC-007, SC-021 coverage):** SC-006 asserts `install` by deep equality and gains a `preset_subdir` /
  `identifier` subtest; SC-007 gains the `E_LISTING_DIGEST` and two-file-write subtests (PD-25); SC-021's second clause
  gets a mechanism (an SC-026 subtest, §7.2).

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-10-30)

| Fact | Source |
|---|---|
| `release.yml` inputs `plugin` (options `:35-43`, `default: iterum` `:44`) and `prerelease` (`:45-48`); `concurrency` `:51-52` | `.github/workflows/release.yml:26-52` |
| `version` job: `PLUGIN` = directory name, `TAG="${PLUGIN}/v${VERSION}"` | `release.yml:57`, `:72-76` |
| Plugin names outside `options:` in unchanged lines | `release.yml:22`, `:44`, `:144`, `:161`, `:163` |
| `windows-installer` `:189-250` (Inno build `:239-243`, upload `:245-250`); `macos-installer` `:253-344` (`Build Installer` `:314-337`, identifier `:321`, `Upload Installer` `:339`); `linux-archive` `:347-410` (`Create Archive` `:397-403`, `Upload Archive` `:405`) | `release.yml` (grep -n of job and step names) |
| `release` job `:413`, `permissions: contents: write` `:417-418`, checkout `fetch-depth: 0` `:423-426`, three downloads into `installers/` `:428-444`, `Generate Release Notes` `:450`, `Create Release` `:514-526` (`softprops/action-gh-release@v2` `:516`, three `files` lines `:523-526`, no `target_commitish`, no `make_latest`) | `release.yml:413-526` |
| `deploy-pages` `:531`, `needs: [version, release]`, `Generate Update Checker Endpoint` `:645`, `peaceiris/actions-gh-pages@v4` `:688` | `release.yml:531-692` |
| `feed-tools.yml`: path filters `:4-21` (no `release-facts.js`, `backfill-sign.js`, `release.yml`, `backfill-sign.yml`), Node 24, `npm ci --prefix tools/feed-contract`, cached `cargo install rsign2 --version 0.6.7 --locked` keyed `rsign2-0.6.7-${{ runner.os }}-${{ runner.arch }}`, TAP check `n < 21` on `# tests N` inside `node -e '…'`, `rsign verify (KAT)`, `Checkout unchanged` | `.github/workflows/feed-tools.yml:1-80` |
| `contract.js`: `ContractError(code, message, where)` `:15`; `CODES` `:28-71` (codes → class); `parseTrustedComment` `:265`, `formatTrustedComment` `:287`, `checkBinding` `:298`, `parseMinisigText` `:329` (returns `{untrustedComment, algorithm, keyId, trustedComment}`, **not** the signature bytes), `toLfMinisig` `:353`, `checkReleaseManifest(doc, {repo, file})` `:489`, `assetNameFor` `:601`, `perTagUrl` `:607`, exports `:611-635` | `tools/feed-contract/contract.js` |
| `release.schema.json`: closed objects; `assets` exactly three keys incl. `windows-x86_64`, `linux-x86_64`; per-key `format` const; `min_os` required on `darwin-*` / `linux-*`, pattern `^[0-9]+(\.[0-9]+){1,3}$`; `size` 1..2147483647; `payload` required iff `tar.gz`; **no `channel` key**; top-level keys `tag, name, version, pub_date, assets, install` | `tools/feed-contract/release.schema.json` |
| `listing.js`: `listingFromTarGz(buffer)` `:277` (buffer, gunzip in memory), `listingFromDirectory(root)` `:290` (throws `E_LISTING_NO_POSIX_MODES` on win32, `lstat`, never follows links), `digest(lines)` `:328`, `checkLinuxLayout(lines, {vst3Bundle})` `:359` (root = bundle, `presets`, `README.txt`, `CHANGELOG.txt`; bundle holds `Contents/Resources/moduleinfo.json` and `Contents/x86_64-linux/<stem>.so`), exports `:384` | `tools/feed-contract/listing.js` |
| `gen-feed.js`: `writeAtomic` (temp `wx` + rename, `E_INPUT` on failure) `:174-190`; `APP_ID_RE` `:310`; `extractAppId` `:336`; `selectReleases(name, releases)` `:418` (drafts dropped, stable = highest non-prerelease, throws `E_TAG_FORMAT`); `macos_pkg_id` rule = `identifier` or `com.krateaudio.<name lower>` `:508`; entry point `:760`; exports `:762-772` (no `selectReleases`) | `tools/gen-feed.js` |
| Write guard: fs writes only under `realpath(os.tmpdir())`; child processes: `exec`/`execSync`/`fork` refused, spawn needs a `cwd` under tmp, no `shell`, only `git` (subcommands `init, commit, update-ref, tag`) and `tar` (`['-xzf', src, '-C', dest]`) `:192`, `:218-235` | `tools/feed-contract/tests/helpers/write-guard.js` |
| Hygiene: `scannedFiles()` `:34`, completeness list `:110`, `FETCH_RE` `:57`, `CHILD_EXEMPT` `:65-69`, teeth subtest `:145`, roster `products` `:236`, `CONTRACT_HEADINGS` `:289-298`, `markdownSections` rejects only duplicates `:301`, code table read from §7 only `:359-395`, §7 exit-code items `:396-401` | `tools/feed-contract/tests/hygiene.test.js` |
| Phase 1 secret-key scan (rules a/b/c) and `findSecretKeyFiles(CONTRACT_DIR)` = `[]` | `tools/feed-contract/tests/c3-trusted-comment.test.js:206-275` |
| Top-level test count at `1dec384c`: `^test(` per file = 2+1+2+1+1+6+1+4+1+5 = **24** | grep -c over `tools/feed-contract/tests/*.test.js` |
| `make-fixtures.js` `check()` compares only its `SETS` dirs (`feeds`, `release`, `gen-feed/<case>`); `fixtures/phase2/` is outside them, so new fixtures cannot trip `fixtures are in sync` | `tools/feed-contract/fixtures/make-fixtures.js:1197-1208`, `:1242-1265` |
| `.gitattributes`: `tools/feed-contract/fixtures/** -text` (covers `fixtures/phase2/`) | `.gitattributes:26` |
| KAT: `kat.pub` = `untrusted comment: minisign public key: 440FA75168B852F7` / `RWT3Urho…czoo`; archive 452 bytes; made with `rsign generate -W` / `rsign sign -W -t …` | `tools/feed-contract/fixtures/kat/kat.pub`, `fixtures/kat/README.md` |
| `CONTRACT.md` §7 code table header `| Code | Class | Raised by | Meaning |` `:418-419`; §8 `:474` is the last section | `tools/feed-contract/CONTRACT.md` |
| `tools/feed-contract/package.json`: devDependency `ajv` 8.20.0 only | `tools/feed-contract/package.json` |
| `version.json` count 9; none carries `identifier` | `ls plugins/*/version.json`, `grep -l identifier` (no hit) |
| `specs/INDEX.md` and `tools/gen-specs-index.js` exist; Phase 1 evidence has `scope-check.js` | `ls` |

### 1.2 External facts (verified 2026-10-30)

| Fact | Source |
|---|---|
| minisign secret key: `sig_alg` `Ed`, `kdf_alg` `Sc` or `[0,0]`, `chk_alg` `B2`, salt 32, opslimit 8 LE, memlimit 8 LE, `keynum_sk` = keynum 8 ‖ sk 64 ‖ chk 32 (158 bytes decoded); checksum = Blake2b-**32-byte** over `sig_alg ‖ keynum ‖ sk`; keystream (104 bytes) XORed over keynum ‖ sk ‖ chk in that order; wrong checksum after decrypt = "Wrong password" | `~/.cargo/registry/src/*/minisign-0.9.1/src/secret_key.rs:77-128`, `:170-203`, `:220-296`; `constants.rs:19-41` (minisign 0.9.1 is rsign2 0.6.7's dependency, `rsign2-0.6.7/Cargo.toml:55-56`) |
| scrypt parameter derivation (`raw_scrypt_params`), default key costs, `MEMLIMIT_MAX`, `N_LOG2_MAX` | `minisign-0.9.1/src/helpers.rs:45-74`, `keypair.rs:65-75`, `constants.rs:26-29` |
| Unencrypted key = `kdf_alg [0,0]`, zero salt / limits, plaintext checksum; secret key = seed ‖ public key | `minisign-0.9.1/src/keypair.rs:29-55`, `crypto/ed25519.rs:57-70` |
| rsign 0.6.7 `sign` tries the key as unencrypted first, then the empty password with `-W`, else prompts | `rsign2-0.6.7/src/bin/rsign/main.rs:103-113` |
| `rsign verify` prints `Signature and comment signature verified` and `Trusted comment: …` on stderr | `minisign-0.9.1/src/lib.rs:202-204` |
| Node v24.7.0 / OpenSSL 3.5.2: `createHash('blake2b512', {outputLength: 32})` throws `ERR_OSSL_EVP_NOT_XOF_OR_INVALID_LENGTH`; `scryptSync(…, 104, {N: 32768, r: 8, p: 1})` fails at default and 32 MiB `maxmem`, passes at 64 MiB (47 ms) | measured, `scratchpad/probe-crypto.js` (not committed) |
| DER input to `createPrivateKey` / `createPublicKey` needs an explicit `type` (`'pkcs8'` / `'spki'`) | https://nodejs.org/docs/latest-v24.x/api/crypto.html ("PEM and DER") |
| BLAKE2b: parameter block byte 0 = digest length, IV, `G` rotations 32/24/16/63, 12 rounds, test vector Appendix A | https://www.rfc-editor.org/rfc/rfc7693 (RFC 7693) |
| `softprops/action-gh-release` v2.6.2: `make_latest` "Can be `true`, `false`, or `legacy`"; `fail_on_unmatched_files` "Fails if any of the `files` globs match nothing. Defaults to false"; `target_commitish` "Commitish value that determines where the Git tag is created from" with the note that `github.token` "may not have permission" for a new tag on an older commit; `overwrite_files` default `'true'`; `runs.using: node20` | https://raw.githubusercontent.com/softprops/action-gh-release/v2.6.2/action.yml |
| `actions/attest-build-provenance`: "As of version 4 … simply a wrapper on top of `actions/attest`"; its `action.yml` wraps `actions/attest@1e69f48… # v4.2.2`; `actions/attest` `subject-path` "May contain a glob pattern or list of paths (total subject count cannot exceed 1024)" | https://github.com/actions/attest-build-provenance (README), `…/main/action.yml`; https://raw.githubusercontent.com/actions/attest/main/action.yml |
| `actions/download-artifact`: inputs `name`, `path`, `pattern`, `merge-multiple` (default `'false'`), `runs.using: node24` | https://raw.githubusercontent.com/actions/download-artifact/main/action.yml |
| `gh release view --json` fields include `assets`, `isDraft`, `isImmutable`, `isPrerelease`, `publishedAt`, `tagName`; each exported asset carries `name`, `size`, `url` and a nullable `digest` (the manual page does not list asset sub-fields; the source does) | https://cli.github.com/manual/gh_release_view; cli/cli `pkg/cmd/release/shared/fetch.go` (`ReleaseAsset`, `ExportData`) as cited in the spec's External facts (spec line 169) |
| REST release asset object: `digest` "required", "string or null"; no minimum `gh` version and no statement that older assets carry one | https://docs.github.com/en/rest/releases/assets (fetched 2026-10-30) |
| Environments: "Secrets stored in an environment are only available to workflow jobs that reference the environment"; deployment branch policy "Selected branches and tags: Only branches and tags that match your specified name patterns can deploy to the environment", matched against `GITHUB_REF`; the page says nothing about a `GITHUB_TOKEN` permission for a job that references an environment | https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments (fetched 2026-10-30) |
| `softprops/action-gh-release` v2.6.2 `overwrite_files`: "Overwrite existing files with the same name. Defaults to true"; the action.yml states no general rule for an existing release beyond `draft` / `append_body` | https://raw.githubusercontent.com/softprops/action-gh-release/v2.6.2/action.yml (fetched 2026-10-30) |
| `gh release upload <tag> <files>... [flags]`; `--clobber` "Delete and re-upload existing assets of the same name"; behaviour on a same-name asset without `--clobber` undocumented | https://cli.github.com/manual/gh_release_upload |
| Mach-O constants: `CPU_SUBTYPE_MASK 0xff000000` (feature flags), `CPU_SUBTYPE_ARM64_ALL 0`, `_V8 1`, `ARM64E 2`, `CPU_SUBTYPE_X86_64_ALL 3`, `_H 8` | https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/mach/machine.h |
| Every other external fact (minisign signature / public-key format, rsign CLI shapes, Mach-O / fat structures, `lipo`, `pkgutil`, `objdump`, workflow `permissions` contexts, immutable releases, `gh release list` default limit 30, `gh release download`) | the spec's "External facts used" table, rows verified on 2026-10-30 by the spec author; this plan relies on them as cited there |

---

## 2. File layout

```
tools/
  minisign.js                         NEW  keygen (memory), sign/verify ED, public-key text, secret-key decrypt, CLI
  release-facts.js                    NEW  Mach-O facts, glibc floor, Linux pre-extract check, tarball vs extracted tree, CLI
  gen-release-manifest.js             NEW  release.json + SHA256SUMS, trusted-comment mode, signing-config mode, CLI
  backfill-sign.js                    NEW  plan / download / facts / apply over injected deps, CLI
  gen-feed.js                         CHG  module.exports gains selectReleases (one line, FR-075)
  feed-contract/
    contract.js                       CHG  CODES gains 27 codes, class 'none' (FR-074)
    CONTRACT.md                       CHG  27 rows in the §7 table; new "## 9. Phase 2 tools"
    keys/README.md                    NEW  key format, user steps, merge order (FR-061, FR-062)
    keys/krate-signing.pub            USER committed by the user (FR-061); never by an agent
    fixtures/phase2/
      KatPlugin/version.json          NEW  {"name": "KatPlugin", "version": "1.2.3"}
      KatPlugin/installers/windows/setup.iss   NEW  one AppId={{…} line (synthetic GUID)
      objdump/{glibc-mixed,glibc-numeric,no-glibc}.txt  NEW  objdump -T samples (SC-005)
      backfill/state.json             NEW  fake gh state: nine synthetic plugins' releases (SC-010 to SC-013)
      backfill/plugins/<Synth>/version.json + installers/windows/setup.iss  NEW  nine synthetic plugins
      kat-secret/{kat.key,kat.pub,message.txt,message.txt.minisig,README.md}  NEW (USER-generated key, §4.12.3)
    tests/
      minisign.test.js                NEW  SC-001, SC-002, SC-003, SC-023, SC-027, SC-028
      release-facts.test.js           NEW  SC-004, SC-005, SC-008, SC-024
      gen-release-manifest.test.js    NEW  SC-006, SC-007, SC-009, SC-022
      backfill-sign.test.js           NEW  SC-010, SC-011, SC-012, SC-013, SC-025
      workflow-shape.test.js          NEW  SC-014, SC-029
      hygiene.test.js                 CHG  FR-072 lists, CHILD_EXEMPT +2, teeth +2 refusals, SC-026 test
      c3-trusted-comment.test.js      CHG  one-path kat.key exemption (PD-6)
      helpers/write-guard.js          CHG  rsign allowlist, four shapes (FR-071)
      helpers/macho.js                NEW  synthetic thin / fat Mach-O builders
      helpers/external-tools.js       NEW  the only test-side spawner: rsign gate + generate/sign/verify, tar extract
      helpers/fake-gh.js              NEW  fake gh / git / exec / sign deps over backfill/state.json
.github/workflows/
  release.yml                         CHG  dry_run input; facts steps; signing, manifest, verify, attest, dry-run artifact
  backfill-sign.yml                   NEW  plan, facts-macos, facts-linux, apply-dry, apply-live
  feed-tools.yml                      CHG  path filter +4; TAP check replaced (heredoc, PD-15)
specs/krate-manager-phase2/
  plan.md, tasks.md (next stage), evidence/scope-check.js, evidence/tap-check.js
specs/INDEX.md                        CHG  regenerated with node tools/gen-specs-index.js
```

Nothing under `apps/`, `plugins/`, `dsp/`, `ci.yml` or any installer script changes (FR-080, FR-081).

---

## 3. Error model

### 3.1 One error type

Every new tool throws `contract.ContractError(code, message, where)` (`contract.js:15`) and nothing else for an expected
failure; an unexpected exception (a bug) propagates and Node prints its stack with exit 1, exactly as `gen-feed.js:752`
re-throws non-`ContractError`s. `main` catches `ContractError`, writes one line `<tool>: <CODE>: <message>` to
`io.stderr` and returns the tool's exit code. A usage error writes `usage: node tools/<file> …` and returns 2. Messages
name the file, plugin, tag or field involved; **no message ever contains a secret value, a password, a key byte or a
base64 key line** (FR-009a, FR-009b, FR-029, FR-050). Where a message would quote untrusted input it goes through
`JSON.stringify`, as `contract.js` `show` does.

### 3.2 New codes (all class `none`), for `contract.js` `CODES` and the `CONTRACT.md` §7 table

| Code | Raised by | Meaning |
|---|---|---|
| `E_MINISIG_PUBKEY` | `minisign.js` `parsePublicKey` | public-key text: first line not `untrusted comment: `, second line not canonical base64, not 42 bytes, algorithm not `Ed` |
| `E_MINISIG_SECRET_KEY` | `minisign.js` `decryptSecretKey` | secret-key text malformed: not 158 bytes decoded, unknown `sig_alg` / `kdf_alg` / `chk_alg`, scrypt cost above the limits (PD-2), public half ≠ the seed's public key (PD-3) |
| `E_MINISIG_PASSWORD` | `minisign.js` `decryptSecretKey` | checksum mismatch after decryption: wrong password or damaged key |
| `E_MINISIG_KEY_ID` | `minisign.js` `verifyBuffer` / `verifyFile` | the signature's key id differs from the public key's |
| `E_MINISIG_SIGNATURE` | `minisign.js` | the Ed25519 signature over Blake2b-512 of the data does not verify |
| `E_MINISIG_GLOBAL` | `minisign.js` | the global signature over signature ‖ trusted comment does not verify |
| `E_FACTS_NO_MACHO` | `release-facts.js` | the expanded pkg holds no Mach-O |
| `E_FACTS_ARCH` | `release-facts.js` | a thin 32-bit or big-endian 64-bit Mach-O, an unknown `cputype`, an arm64 subtype other than ALL/V8, a 32-bit or unknown fat slice |
| `E_FACTS_ARCH_MISMATCH` | `release-facts.js` | two Mach-Os in one pkg report different architecture sets |
| `E_FACTS_NO_MINOS` | `release-facts.js` | a slice has neither `LC_BUILD_VERSION` (macOS) nor `LC_VERSION_MIN_MACOSX` |
| `E_FACTS_MACHO_CORRUPT` | `release-facts.js` | a header, slice or load command runs past the buffer; `nfat_arch` 0; `cmdsize` < 8 |
| `E_FACTS_NO_GLIBC` | `release-facts.js` | `objdump -T` output names no `GLIBC_<n>.<n>[.<n>]` |
| `E_FACTS_LISTING_MISMATCH` | `release-facts.js` | the extracted tree's listing differs from the tarball's (names the first differing line) |
| `E_FACTS_LIPO_MISMATCH` | `backfill-sign.js` `facts` | `lipo -archs` disagrees with the Node architecture set (names the file) |
| `E_FACTS_STALE` | `gen-release-manifest.js` | a facts file's `asset`, `size` or `sha256` differs from the asset in `--assets` |
| `E_FACTS_MISSING` | `backfill-sign.js` `apply` | a planned release has no darwin or linux facts file and no facts-report entry |
| `E_ASSET_MISSING` | `gen-release-manifest.js` | one of the three assets or its `.minisig` is absent from `--assets` |
| `E_VERSION_MISMATCH` | `backfill-sign.js` `apply` | the tag's `version.json` `version` differs from the tag's version |
| `E_SIGNING_CONFIG` | `gen-release-manifest.js` `signingConfig`; `backfill-sign.js` `apply` (PD-23) | a real run without a non-empty key secret, a non-empty password secret and a parsable committed public key; or a public key equal to a test key (`fixtures/kat/kat.pub`, `fixtures/phase2/kat-secret/kat.pub`), or those files unreadable in real mode |
| `E_BACKFILL_LIST_TRUNCATED` | `backfill-sign.js` `plan` | `gh release list --limit 1000` returned 1000 rows |
| `E_BACKFILL_ASSET_MISSING` | `backfill-sign.js` `plan` | a current release lacks one of its three asset names |
| `E_BACKFILL_IMMUTABLE` | `backfill-sign.js` `plan` | the release is immutable (`isImmutable: true`) |
| `E_BACKFILL_PARTIAL` | `backfill-sign.js` `plan` | `SHA256SUMS` or a `.minisig` exists without `release.json` |
| `E_BACKFILL_SIZE` | `backfill-sign.js` `download` | a downloaded asset's size differs from the release listing |
| `E_BACKFILL_DIGEST` | `backfill-sign.js` `download` | a downloaded asset's SHA-256 differs from GitHub's `digest` |
| `E_BACKFILL_ASSETS_CHANGED` | `backfill-sign.js` `apply` (audit) | a release's asset list after the run differs from `assets_before` (+ uploads on a live run) |
| `E_SIGN_KEY_MISMATCH` | `backfill-sign.js` `apply` | a signature the run produced does not verify against `--public-key` |

Existing codes reused with their Phase 1 meaning: `E_USAGE`, `E_INPUT`, `E_TAG_FORMAT`, `E_LAYOUT`, `E_LISTING_*`,
`E_SIGNATURE_TEXT`, `E_TC_*`, `E_MANIFEST_SCHEMA`, `E_URL_NOT_PER_TAG`. Their class cells stay as they are; the §7 "Raised
by" cells of `E_INPUT`, `E_SIGNATURE_TEXT`, `E_TC_BINDING`, `E_LAYOUT` and `E_LISTING_DIGEST` gain the Phase 2 tools that
also raise them (text only; the check reads the class cell, `hygiene.test.js:381-389`).

### 3.3 Exit codes (go into `CONTRACT.md` §9, not §7)

| Tool | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| `minisign.js verify` | verified | rejected (`E_MINISIG_*`, `E_SIGNATURE_TEXT`) or unreadable input (`E_INPUT`) | usage | — |
| `minisign.js decrypt-key` | key written | `E_INPUT`, `E_MINISIG_PASSWORD`, `E_MINISIG_SECRET_KEY`; nothing left behind | usage | — |
| `release-facts.js darwin\|linux` | facts written | any `E_FACTS_*`, `E_LAYOUT`, `E_LISTING_*`, `E_INPUT`; `--out` untouched | usage | — |
| `release-facts.js linux-precheck` (PD-26) | listing and layout pass | `E_LAYOUT`, `E_LISTING_*`, `E_INPUT` | usage | — |
| `gen-release-manifest.js` (all modes) | written / printed | abort, nothing written or changed (FR-026, PD-25), `E_SIGNING_CONFIG` | usage | — |
| `backfill-sign.js plan` | loop finished (per-release failures recorded) | abort (`E_BACKFILL_LIST_TRUNCATED`, `E_TAG_FORMAT` (PD-10), a `gh release list` failure) | usage | — |
| `backfill-sign.js download\|facts` | loop finished (per-release failures recorded) | abort | usage | — |
| `backfill-sign.js apply` | nothing reported | abort (`E_SIGNING_CONFIG` (PD-23), `E_SIGN_KEY_MISMATCH`, `E_BACKFILL_ASSETS_CHANGED`, a missing facts report `E_INPUT`, a `gh` failure outside one release) | usage | ≥ 1 release reported (an upload failure included, PD-24), the rest signed |

---

## 4. Module design

All four tools share one form (FR-076): first line `#!/usr/bin/env node`; a header comment with the WHY, the usage and
the exit codes (as `gen-feed.js:1-25`); `'use strict'`; requires of `node:` built-ins and `./feed-contract/*.js` only
(plus `node:child_process` in `backfill-sign.js` alone); `function main(argv, io)` returning the exit code;
`if (require.main === module) process.exitCode = main(process.argv.slice(2), process);` (`gen-feed.js:760`);
`module.exports = {…}` last. Argument parsing is a small `parseArgs(argv, spec)` per tool that rejects unknown, missing
and repeated flags with `E_USAGE` (the `gen-feed.js` pattern), not `util.parseArgs`, so a repeated flag is an error.
Outputs are written through a local `writeAtomic(path, data, {mode})` identical in behaviour to `gen-feed.js:174-190`
(temp file `wx` in the same directory, rename, temp removed on failure); `gen-release-manifest.js` manifest mode uses
`writePair` instead (PD-25). Tools call `fs.*` and `crypto.*` through the module objects, never destructured, so the
in-process wrappers of SC-007 subtest 10 (`fs.renameSync`) and SC-027 (b) (`crypto.scryptSync`) see the calls.

### 4.1 `tools/minisign.js`

```js
/** @typedef {{keyId: Buffer /* 8 bytes as stored */, publicKey: crypto.KeyObject}} PublicKeyInfo */
/** @typedef {{keyId: Buffer, privateKey: crypto.KeyObject}} SecretKeyInfo */
function generateKeyPair(): {keyId: Buffer, publicKey: KeyObject, privateKey: KeyObject}
function formatPublicKey({keyId, publicKey}): string           // 'untrusted comment: minisign public key: <KEYID>\n<b64>\n'
function parsePublicKey(text: string): PublicKeyInfo            // E_MINISIG_PUBKEY
function signBuffer(data: Buffer, sk: SecretKeyInfo, {trustedComment, untrustedComment?}): string
function signFile(path: string, sk: SecretKeyInfo, opts): string  // streams the file into blake2b512
function verifyBuffer(data: Buffer, minisigText: string, publicKeyText: string): {trustedComment: string, keyId: string}
function verifyFile(path: string, minisigText: string, publicKeyText: string): {trustedComment, keyId}
function decryptSecretKey(keyText: string, password: string): SecretKeyInfo
function main(argv: string[], io: {stdout, stderr, env}): number
const _internal = {blake2b, rawScryptParams}   // PD-18: exported for the SC-027 anchors only; no production caller
```

**Byte layouts** (minisign 0.12 format, spec External facts; `contract.js:310-312` constants):

- Public key text line 2 = base64(`'Ed'` ‖ keyId(8) ‖ pk(32)), 42 bytes. `<KEYID>` = `Buffer.from(keyId).reverse()`
  upper-case hex (measured, spec External facts; `kat.pub` line 1 `440FA75168B852F7`).
- Key import: public = `createPublicKey({key: Buffer.concat([SPKI_PREFIX, pk]), format: 'der', type: 'spki'})`,
  `SPKI_PREFIX = 302a300506032b6570032100` (spec External facts, measured accept of the KAT); private from a seed =
  `createPrivateKey({key: Buffer.concat([PKCS8_PREFIX, seed]), format: 'der', type: 'pkcs8'})`,
  `PKCS8_PREFIX = 302e020100300506032b657004220420` (the RFC 8410 Ed25519 PKCS#8 header; correctness is proven end to end
  by SC-027's "signature made with the returned key passes `verifyBuffer` against `kat.pub`").
  `generateKeyPair` exports the raw 32-byte public key from `publicKey.export({format: 'der', type: 'spki'}).subarray(12)`.
- `.minisig`: line 1 `untrusted comment: <uc>` (default `signature from krate minisign.js`), line 2
  base64(`'ED'` ‖ keyId ‖ sig), `sig = crypto.sign(null, blake2b512(data), privateKey)`, line 3
  `trusted comment: <tc>`, line 4 base64(`crypto.sign(null, sig ‖ utf8(tc), privateKey)`); LF, final LF. A `tc` with CR
  or LF is a `TypeError` (programming error; callers pass C3 comments from `formatTrustedComment`, which already rejects
  them, `contract.js:287-296`).
- **Verify order (FR-005):** `contract.parseMinisigText(text)` (`E_SIGNATURE_TEXT`) → `parsePublicKey` →
  key id compare (`E_MINISIG_KEY_ID`) → decode line 2 again for the 64-byte `sig` (`parseMinisigText` returns only the key
  id, `contract.js:345-350`), `crypto.verify(null, blake2b512(data), pub, sig)` (`E_MINISIG_SIGNATURE`) → decode line 4,
  `crypto.verify(null, sig ‖ utf8(tc), pub, global)` (`E_MINISIG_GLOBAL`). Return `{trustedComment, keyId: <KEYID hex>}`.
  CRLF input is accepted because `parseMinisigText` splits on `\r?\n` (`contract.js:331`).

**Secret-key decryption (FR-009a, PD-1 to PD-3):**

```
input  := keyText trimmed of a final line end
if input has no 'untrusted comment: ' first line and is a single base64 line:
    input := base64decode(input) as UTF-8      // the TAURI_SIGNING_PRIVATE_KEY form (probes line 10)
lines  := input.split(/\r?\n/); need lines[0] startsWith 'untrusted comment: ' and lines[1] canonical base64
raw    := base64decode(lines[1]); need raw.length === 158                     else E_MINISIG_SECRET_KEY
sigAlg := raw[0..2] == 'Ed'; kdfAlg := raw[2..4] ∈ {'Sc', 00 00}; chkAlg := raw[4..6] == 'B2'   else E_MINISIG_SECRET_KEY
salt   := raw[6..38]; ops := u64le(raw[38..46]); mem := u64le(raw[46..54]) (BigInt, then range-checked)
blob   := raw[54..158]                                      // keynum 8 | sk 64 | chk 32
if kdfAlg == 'Sc':
    {N, r, p} := rawScryptParams(mem, ops)                  // exact port of helpers.rs:45-74, PD-2 limits
    stream := scryptSync(utf8(password), salt, 104, {N, r, p, maxmem: 2*128*N*r})
    blob   := blob XOR stream
chk' := blake2b(sigAlg ‖ blob[0..8] ‖ blob[8..72], 32)       // PD-1, in-file RFC 7693
if chk' != blob[72..104]: E_MINISIG_PASSWORD (encrypted) | E_MINISIG_SECRET_KEY (unencrypted, "corrupted")
seed := blob[8..40]; pkHalf := blob[40..72]
privateKey := pkcs8(seed); need derived raw public key == pkHalf                       else E_MINISIG_SECRET_KEY (PD-3)
return {keyId: blob[0..8], privateKey}
```

`rawScryptParams(memlimit, opslimit)`: `opslimit = max(32768, opslimit)`, `r = 8`; if `opslimit < memlimit / 32`
(integer division) `p = 1`, `maxn = opslimit / (r * 4)`; else `maxn = memlimit / (r * 128)`; `n_log2` = the smallest
value ≥ 1 with `2^n_log2 > maxn / 2` (capped at 63); in the else branch `p = min(0x3fffffff, (opslimit / 4) / 2^n_log2) /
r`. Then the limits: `memlimit > 1_073_741_824`, `n_log2 > 20`, or `128 * N * r * p > 2^31` → `E_MINISIG_SECRET_KEY`. All
arithmetic in BigInt, converted to Number only after the limits hold. Known answer used in tests: `(memlimit 33554432,
opslimit 1048576)` → `{N: 32768, r: 8, p: 1}` (derived by hand from `helpers.rs:45-74` this session; SC-027 proves it
against rsign's own key).

`blake2b(data, outLen)`: RFC 7693 §3.2 compression over 128-byte blocks, BigInt 64-bit words masked with
`0xffffffffffffffffn`, parameter word `0x01010000 ^ (keyLen << 8) ^ outLen` with keyLen 0. It runs on ≤ 74 bytes per
call in production (one block), so BigInt speed does not matter. Its correctness anchors (SC-027 subtests, called through
`_internal.blake2b`, PD-18): output length 64 equals `createHash('blake2b512')` on inputs of length 0, 1, 127, 128, 129
and 1000 (block boundaries); the RFC 7693 Appendix A vector (BLAKE2b-512 of `"abc"`); and the end-to-end anchor, the
rsign-made `kat.key` checksum after decryption. The scrypt-parameter known answer is asserted through
`_internal.rawScryptParams`.

**`decrypt-key` CLI (FR-009b, PD-16):** reads `io.env.KRATE_SIGNING_KEY` and `io.env.KRATE_SIGNING_KEY_PASSWORD` (either
absent or empty → `E_INPUT` naming the **variable**, never its value); checks `--out` by PD-19, every failure `E_INPUT`
with nothing written:

```
raw     := --out as given; any path segment equal to '..' → E_INPUT
root    := fold(realpathSync.native(io.env.RUNNER_TEMP || os.tmpdir()))      // missing root → E_INPUT
abs     := path.resolve(raw); parent := path.dirname(abs)
parent must exist and be a directory (lstat after realpath)                  else E_INPUT
target  := path.join(realpathSync.native(parent), path.basename(abs))        // links in the parent chain followed
need fold(target) === root or fold(target).startsWith(root + path.sep)       else E_INPUT
need lstat(target) throws ENOENT (no file, directory or link there yet)      else E_INPUT
fold(s) := win32 ? s.toLowerCase() : s                                       // as write-guard.js:40-41
```

then `decryptSecretKey`; serialises the unencrypted layout of PD-16; `writeAtomic(target, text, {mode: 0o600})` (the temp
file is created in `dirname(target)` with `mode: 0o600` and `flag: 'wx'`, then renamed to `target`); prints `decrypted:
key id <KEYID>\n`. Every failure path removes the temp file in a `finally`; the out path is never created before the
rename. A symlinked directory under `RUNNER_TEMP` pointing outside it fails the containment line, and the macOS
`/var` → `/private/var` link and the Windows runner's 8.3 temp name resolve identically on both sides.

`minisign.js` requires `contract.js`, which loads ajv at module load (`contract.js:13`, `:358-364`); that is accepted
(Review notes R-3): the `decrypt-key` process therefore runs ajv's lockfile-pinned packages beside the key, and PD-20
bounds how long the decrypted key exists.

`verify` CLI: `verify -p <pub> -x <sig> <file>`; reads all three; `verifyFile`; prints `verified: <tc>\n`.

### 4.2 `tools/release-facts.js`

```js
function machoFacts(buffer: Buffer): {archs: string[], minOs: string, slices: {arch: string, minOs: string}[]}
function darwinFactsFromTree(root: string): {platform_key, min_os, archs: string[], macho: {path, archs, min_os}[]}
function platformKeyForArchs(archs: string[]): 'darwin-universal' | 'darwin-aarch64' | 'darwin-x86_64'  // else E_FACTS_ARCH
function glibcFloor(objdumpText: string): string                       // '2.34'
function linuxFacts({tarball: string, extracted: string, objdumpText: string},
                    {listDirectory = listing.listingFromDirectory} = {}): {asset, size, sha256, min_os, payload: {files, sha256}}
function linuxPrecheck(buf: Buffer): {lines: string[], vst3Bundle: string, soPath: string | null}  // PD-26, no fs, no exec
function main(argv, io): number
```

**`machoFacts` (FR-011):** read the first 4 bytes. Big-endian `0xcafebabe` / `0xcafebabf` → fat: `nfat_arch` (BE u32,
0 → `E_FACTS_MACHO_CORRUPT`), then `nfat_arch` entries of 20 bytes (`fat_arch`) or 32 bytes (`fat_arch_64`: u64 offset
and size, plus `reserved`), all big-endian; each slice `[offset, offset+size)` must lie inside the buffer and is parsed as
a thin file whose arch must equal the entry's `cputype`/`cpusubtype` decode. Little-endian `0xfeedfacf` → thin 64-bit.
LE `0xfeedface`, LE `0xcefaedfe` (32-bit either order) and LE `0xcffaedfe` (big-endian 64-bit) → `E_FACTS_ARCH`.
Thin 64-bit header (32 bytes): `cputype`, `cpusubtype`, `filetype`, `ncmds`, `sizeofcmds` (LE u32 at 4, 8, 12, 16, 20).
Arch: `0x01000007` → `x86_64`; `0x0100000c` with `cpusubtype & ~0xff000000` ∈ {0, 1} → `arm64`; anything else
`E_FACTS_ARCH` (arm64e = subtype 2 with the `0x80000000` ABI flag, machine.h). Load commands: walk `ncmds` commands from
offset 32 within `sizeofcmds` and the buffer; `cmdsize < 8` or past the end → `E_FACTS_MACHO_CORRUPT`; the first
`LC_BUILD_VERSION` (`0x32`) with `platform == 1` gives `minos` (offset 12 of the command), else the first
`LC_VERSION_MIN_MACOSX` (`0x24`) gives `version` (offset 8); neither → `E_FACTS_NO_MINOS`. Version text (D3): `X =
v >>> 16`, `Y = (v >>> 8) & 0xff`, `Z = v & 0xff`; `X.Y` when `Z === 0`, else `X.Y.Z`. The file's `minOs` is the highest
slice `minOs` (numeric, part by part). x86_64h (subtype 8) decodes as `x86_64` per FR-011; `lipo -archs` prints
`x86_64h` for it, so the FR-041 / FR-053a cross-check fails such a file closed.

**`darwinFactsFromTree` (FR-012):** recursive walk with `fs.readdirSync(…, {withFileTypes: true})` and `fs.lstatSync`;
symbolic links are skipped (never followed, never counted); for each regular file read 4 bytes with `fs.openSync` /
`fs.readSync`; on a magic match read the whole file and run `machoFacts`. Paths relative to `root`, POSIX separators,
sorted bytewise. Zero files → `E_FACTS_NO_MACHO`. All files' arch sets must be equal (`E_FACTS_ARCH_MISMATCH`, naming
two paths and their sets); `platform_key = platformKeyForArchs(set)`; `min_os` = the highest file `minOs`.

**`glibcFloor` (FR-014):** every match of `/\bGLIBC_([0-9]+(?:\.[0-9]+){1,2})\b/g`; the highest by numeric parts; none →
`E_FACTS_NO_GLIBC`. `GLIBC_PRIVATE` does not match the pattern.

**`linuxPrecheck` (PD-26):** `lines = listingFromTarGz(buf)` (rejects absolute paths, `..`, drive letters and escaping
links, `E_LISTING_*`); the bundle = the single root-level `d` entry ending `.vst3` (zero or several → `E_LAYOUT`, naming
them); `checkLinuxLayout(lines, {vst3Bundle})`; `soPath` = `<stem>.vst3/Contents/x86_64-linux/<stem>.so` when the
listing holds it as a regular-file entry, else `null`. It runs on every OS (no extraction).

**`linuxFacts` (FR-015, PD-4):** `buf = fs.readFileSync(tarball)`; `{lines, vst3Bundle} = linuxPrecheck(buf)`; then
`ext = listDirectory(extracted)`; first index where `ext[i] !== lines[i]` (or a length difference)
→ `E_FACTS_LISTING_MISMATCH` naming that line from both sides; `min_os = glibcFloor(objdumpText)`; returns `{asset:
basename(tarball), size: buf.length, sha256: sha256(buf), min_os, payload: {files: lines.length, sha256:
listing.digest(lines)}}`.

**CLI (FR-010, FR-013):** `darwin --pkg <file> --expanded <dir> --out <json>` writes `{asset, size, sha256, platform_key,
min_os, archs, macho}` (`size` / `sha256` of the pkg, streamed hash) and prints one line per Mach-O
`<archs.join(' ')>\t<path>`; `linux --tarball <file> --extracted <dir> --objdump <txt> --out <json>` writes the
`linuxFacts` result; `linux-precheck --tarball <file>` (PD-26) runs `linuxPrecheck` and additionally fails `E_LAYOUT`
when `soPath` is `null`, printing nothing on success. JSON is `JSON.stringify(doc, null, 2) + '\n'`, keys in the order
listed. No `child_process`.

### 4.3 `tools/gen-release-manifest.js`

```js
/** Pure core used by main and by backfill-sign apply. Throws ContractError; writes nothing. */
function buildManifest({
  versionJsonText: string, setupIssText: string,
  assetsDir: string,                       // holds the three assets and their .minisig
  darwinFacts: object, linuxFacts: object, // parsed facts files
  repo: string, pubDate: string, publicKeyText: string,
}): {doc: object, releaseJsonText: string, sumsText: string}
function trustedComments({versionJsonText, assetsDir, darwinFacts}): {asset: string, platform: string, comment: string}[]
function signingConfig({dryRun: boolean, secretPresent: boolean, passwordPresent: boolean,
                        publicKeyText: string | null,
                        testPublicKeyTexts: (string | null)[]}): 'throwaway' | 'on'   // else E_SIGNING_CONFIG (PD-23)
function refuseTestKey(publicKeyText: string, testPublicKeyTexts: (string | null)[]): void  // shared with backfill apply
function main(argv, io): number   // manifest mode | --trusted-comments | --signing-config
```

**`buildManifest` order (FR-026, the first failure is the code):**

1. Parse `versionJsonText` (`E_INPUT` on bad JSON; `name` through `contract.isPluginName`, `version` through the C2 semver
   pattern, else `E_INPUT`). `darwinFacts.platform_key` must be a `darwin-*` member of `PLATFORM_KEYS` (`E_INPUT`).
   `keys = ['windows-x86_64', <darwin key>, 'linux-x86_64']`; names via `contract.assetNameFor`.
2. FR-021: each asset and `<asset>.minisig` exists in `assetsDir` (`E_ASSET_MISSING`, naming it).
3. FR-023: darwin facts `asset/size/sha256` equal the pkg's (`E_FACTS_STALE`); linux facts the same for the tarball;
   `lines = listingFromTarGz(tarball)`; `linuxFacts.payload` must equal `{files: lines.length, sha256: digest(lines)}`
   (`E_LISTING_DIGEST`); `payload.list = lines`.
4. FR-024, keys in bytewise order (`darwin-*`, `linux-x86_64`, `windows-x86_64`): `minisign.verifyFile(asset, sigText,
   publicKeyText)`, then `contract.checkBinding(tc, {name, version, platform: key, asset})` (`E_TC_BINDING`).
5. FR-025: `appId = genFeed.extractAppId(setupIssText)` (undefined → `E_INPUT`); `install = {vst3_bundle, au_component,
   [preset_subdir], windows_uninstall_key: appId + '_is1', macos_pkg_id}` with `macos_pkg_id = versionJson.identifier ??
   'com.krateaudio.' + name.toLowerCase()` (the rule of `release.yml:321` and `gen-feed.js:508`).
6. Assemble `{tag: name.toLowerCase() + '/v' + version, name, version, pub_date, assets, install}`; per asset `{name, url:
   perTagUrl(repo, tag, name), size, sha256, signature: toLfMinisig(sigText), format, min_os?, payload?}`; then
   `contract.checkReleaseManifest(doc, {repo})`.
7. `sumsText` = three lines `<sha256>  <name>\n`, sorted bytewise by name (D7).

Sizes come from `fs.statSync`; hashes from a streamed `createHash('sha256')`. `main` (manifest mode) reads the files named
on the command line, calls `buildManifest`, then `writePair(--sums-out, sums, --out, releaseJson)` (PD-25):

```
tS := <sums-out>.<rand>.tmp, tR := <out>.<rand>.tmp      both written with flag 'wx' first
for t in [sums-out, out]: lstat(t) must be ENOENT or a regular file          else E_INPUT (nothing renamed)
if sums-out exists: rename(sums-out, bS := <sums-out>.<rand>.bak)
rename(tS, sums-out)
try rename(tR, out)
catch: unlink(sums-out); if bS: rename(bS, sums-out); throw E_INPUT naming --out
finally: unlink tS, tR, bS when still present
```

Nothing is written before every check passed, and a failure at any point leaves both pre-existing outputs' bytes and
`mtimeMs` as they were (SC-007 subtests 7, 9 and 10). `buildManifest` imports `extractAppId` from `../gen-feed.js` (already exported,
`gen-feed.js:770`).

**`--trusted-comments`** prints `<asset>\t<comment>\n` per key in bytewise key order, each comment from
`contract.formatTrustedComment`; it reads only names (no file must exist besides `--version-json` and `--darwin-facts`;
`--assets` is accepted for symmetry and not read).

**`--signing-config --dry-run <true|false> --public-key <file>`:** `publicKeyText` = the file's text, or `null` when
`fs.existsSync` is false; `secretPresent = (io.env.KRATE_SIGNING_KEY ?? '') !== ''`, same for the password; prints
`signing=<mode>\n`. The `E_SIGNING_CONFIG` message lists, in this order, `secret`, `password`, `public key absent` or
`public key: <E_MINISIG_PUBKEY message>`, then `see tools/feed-contract/keys/README.md`. A `--dry-run` value other than
`true` / `false` is `E_USAGE`. In real mode, once the public key parses, `refuseTestKey` (PD-23) compares its key id and
its 32-byte Ed25519 key with those of `path.join(__dirname, 'feed-contract/fixtures/kat/kat.pub')` and
`…/fixtures/phase2/kat-secret/kat.pub` (`main` reads them; an unreadable or unparsable one is passed as `null`, which
refuses: fail closed); a match is `E_SIGNING_CONFIG` with `public key is a test key (<fixture path>)`. Dry-run mode never
reads them.

### 4.4 `tools/backfill-sign.js`

```js
/** deps: every effect the tool has on the world. */
/** @typedef {{
 *   gh(argv: string[]): {status: number, stdout: string, stderr: string},
 *   git(argv: string[]): {status: number, stdout: string},
 *   exec(command: 'pkgutil'|'lipo'|'tar'|'objdump', argv: string[]): {status, stdout, stderr},
 *   sign(file: string, trustedComment: string, keyFile: string): string,   // returns the .minisig text it wrote
 *   log(line: string): void,
 *   listDirectory?: (root: string) => string[],                             // PD-4, default listing.listingFromDirectory
 * }} Deps */
function plan({pluginsDir, repo, out}, deps): number
function download({plan, format, dir, repo}, deps): number
function facts({plan, os, dir, work, out}, deps): number
function apply({plan, pluginsDir, facts, key, publicKey, repo, work, outDir, dryRun}, deps): number
function productionDeps(workDir: string, repoDir: string): Deps
function cliDeps(argv): Deps   // productionDeps(<--work or os.tmpdir()>, <--plugins-dir or os.tmpdir()>)
function main(argv, io, deps = cliDeps(argv)): number
```

`productionDeps(workDir, repoDir)` uses `child_process.spawnSync(cmd, argv, {cwd: workDir, encoding: 'utf8', stdio:
['ignore', 'pipe', 'pipe'], shell: false, maxBuffer: 64 MiB})` for `gh`, the four `exec` tools (anything else throws a
`TypeError` before spawning) and `rsign`, and the same with `cwd: repoDir` for `git` (FR-051: D12's `git show <tag>:…`
needs the checkout and its tags; CI's `--work` is `$RUNNER_TEMP/work`, outside the checkout, so a `git` in `--work`
exits 128). `sign` runs `rsign sign -W -s <keyFile> -t <comment> -x <file>.minisig <file>`
and returns `fs.readFileSync(<file>.minisig, 'utf8')`; status ≠ 0 throws `E_INPUT` naming the file (never the key path's
contents). No identifier in the file matches `FETCH_RE` (`hygiene.test.js:57`); no shell string anywhere.

**`plan.json`** (written with `writeAtomic`, `JSON.stringify(…, null, 2) + '\n'`):

```json
{
  "repo": "<owner/name>",
  "releases": [
    { "name": "<Name>", "dir": "<plugins/ subdirectory>", "status": "planned",
      "tag": "<name>/v1.2.3", "version": "1.2.3", "publishedAt": "2026-…Z",
      "assets": [ {"name": "<Name>-1.2.3-Windows-x64.exe", "format": "inno-exe", "size": 123, "digest": "sha256:…" | null}, … ],
      "assets_before": ["<sorted asset names at plan time>"] },
    { "name": "…", "dir": "…", "status": "skipped-signed" | "skipped-no-stable" | "reported",
      "code": "E_BACKFILL_…", "message": "…", "tag": "…", "assets_before": [ … ] }
  ]
}
```

Plugins come from `<pluginsDir>/*/version.json` at the checkout (ROSTER); entries are sorted by `name`. `plan` runs one
`gh release list --repo <repo> --limit 1000 --json tagName,isPrerelease,isDraft,publishedAt` (1000 rows →
`E_BACKFILL_LIST_TRUNCATED`, abort), `genFeed.selectReleases(name, rows)` (`E_TAG_FORMAT` aborts, PD-10), then per stable
release `gh release view <tag> --repo <repo> --json tagName,publishedAt,isImmutable,assets`. Classification order per
release: has `release.json` → `skipped-signed`; else missing asset name → `E_BACKFILL_ASSET_MISSING`; `isImmutable` →
`E_BACKFILL_IMMUTABLE`; `SHA256SUMS` or any `<asset>.minisig` present → `E_BACKFILL_PARTIAL`; else `planned`. Asset names
are always rebuilt with `contract.assetNameFor(name, version, key)`; listed names are compared, never used as paths.

**`download`:** for each planned release, the asset whose `format` matches; `fs.mkdirSync(<dir>/<tag-dashed>)`
(`tag.replace('/', '-')`); `gh release download <tag> --repo <repo> --pattern <asset> --dir <that dir>`; size via
`fs.statSync` (`E_BACKFILL_SIZE`), SHA-256 streamed (lower-case hex) vs `digest.slice('sha256:'.length)` when not null
after lower-casing (`E_BACKFILL_DIGEST`); a non-null digest not of the form `sha256:<64 hex>` is `E_BACKFILL_DIGEST`
too.
Failures are collected and written as `<dir>/download-report.json` (an array, always written). The last log line is
`digest checked: <n> of <m>` for the format (PD-28: `size` is the guaranteed check, `digest` best-effort).

**`facts` (FR-053a):** reads `download-report.json` (missing → `E_INPUT` abort) and copies its entries into
`<out>/facts-report.<os>.json`; for every other planned release: darwin → `exec('pkgutil', ['--expand-full', pkg,
<work>/<tag-dashed>-x])`, `darwinFactsFromTree`, `exec('lipo', ['-archs', file])` per listed Mach-O (sorted words vs
the Node set, `E_FACTS_LIPO_MISMATCH`), then the FR-013 object written to `<out>/<name>-<version>.darwin.json`; linux →
**first** `releaseFacts.linuxPrecheck(fs.readFileSync(tarball))` (PD-26; any `E_LISTING_*` / `E_LAYOUT`, or `soPath`
`null` as `E_LAYOUT`, reports the release with no `exec` made), then `exec('tar', ['-xzf', tarball, '-C',
<work>/<tag-dashed>-x])` (directory created first), `exec('objdump', ['-T', <x>/<soPath>])`, `linuxFacts({…},
{listDirectory: deps.listDirectory})`, written to `<name>-<version>.linux.json`. A non-zero `exec` is recorded as `E_INPUT` with the command and status; a `ContractError`
under its own code. The report is always written, `[]` when empty.

**`apply` (FR-054 to FR-057a, PD-9):**

1. Read `plan.json` and both `facts-report.darwin.json` / `.linux.json` (missing → `E_INPUT`, abort, 0 uploads);
   parse `--public-key` and run `refuseTestKey` on it (PD-23; `E_SIGNING_CONFIG`, abort, 0 uploads).
2. Pass 1, per planned release not in a report: `download` its three assets into `<work>/<tag-dashed>/assets`;
   `git show <tag>:plugins/<dir>/version.json` and `…/installers/windows/setup.iss` (non-zero → `E_INPUT` for that
   release); tag version ≠ `version.json` version → `E_VERSION_MISMATCH`; both facts files present
   (`E_FACTS_MISSING`); `trustedComments` → `deps.sign(asset, comment, key)` per asset, the returned text written as
   `<asset>.minisig` through `toLfMinisig`; **immediately** `minisign.verifyFile` against `--public-key`; any failure →
   `E_SIGN_KEY_MISMATCH`, abort, exit 1, 0 uploads; then `buildManifest` with `pubDate = publishedAt` (D9); outputs
   (`SHA256SUMS`, three `.minisig`, `release.json`) copied to `<outDir>/<tag-dashed>/`. Any `ContractError` here reports
   that release and attaches nothing. The download step logs `digest checked: <n> of <m>` (PD-28).
   **End of pass 1 (PD-20):** `fs.rmSync(key, {force: true})`; the same removal sits in a `finally` around pass 1, so an
   abort inside it removes the key too. No `gh` call of passes 2 and 3 runs while the key file exists.
3. Pass 2 (live only): per signed release, `gh release upload <tag> --repo <repo> SHA256SUMS <3 .minisig>` then
   `gh release upload <tag> --repo <repo> release.json` (D8), each argument an absolute path in `<outDir>`; before each call
   assert no `--clobber` and no basename in `assets_before`. A call that returns status 0 adds its basenames to that
   release's `uploaded`. A failing call (PD-24) reports that release `E_INPUT` (`gh release upload <tag>` and the status),
   skips that release's remaining call, and the loop continues with the next release.
4. Pass 3, the audit, always run (dry, live, and after PD-24 reports): `gh release view <tag> --json assets` for every
   release in the plan; sorted names must equal `assets_before` (dry) or `assets_before ∪ uploaded` (live), else
   `E_BACKFILL_ASSETS_CHANGED`, exit 1 (this also catches a failed call that left part of its files behind).
5. Summary (`deps.log`): one line per plugin, `<Name>: signed | skipped (already signed) | skipped (no stable release) |
   reported <CODE>: <message>`, then `uploads: <n>`. Exit 0 / 3 / 1 per §3.3.

### 4.5 `tools/gen-feed.js` (FR-075)

One line: `selectReleases,` added to `module.exports` (`gen-feed.js:762-772`). Nothing else changes.

### 4.6 `contract.js` and `CONTRACT.md`

`CODES` gains the 27 keys of §3.2 with value `'none'`, after `E_SCHEMA` (`contract.js:70`). `CONTRACT.md` §7's table gains
the 27 rows of §3.2 (cells `` `E_…` | none | <Raised by> | <Meaning> ``). New `## 9. Phase 2 tools` after §8 holds: the
four CLIs with their usage lines; the §3.3 exit-code table; the `.minisig` / public-key / secret-key byte layouts of
§4.1 (with the minisign source lines); the facts JSON shapes; `plan.json`; the `SHA256SUMS` form (D7); the signing
configuration rule (D6). The eight `CONTRACT_HEADINGS` stay unchanged (`hygiene.test.js:289-298`).

### 4.7 `tools/feed-contract/keys/README.md` (FR-061, FR-062, SC-029)

Sections, in this order: (1) what `krate-signing.pub` is (two-line minisign public-key text, what `rsign verify -p` reads;
the `.pub` that `tauri signer generate` writes is base64 of it); (2) user steps: `tauri signer generate -w <path> -p
'<a non-empty password>'` (https://v2.tauri.app/reference/cli/, `-p/--password`, `-w/--write-keys`); store the private key
file content as `TAURI_SIGNING_PRIVATE_KEY` and the password as `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`; base64-decode the
`.pub` into `tools/feed-contract/keys/krate-signing.pub` and commit it; the password must be non-empty; Phase 11 passes
the same password to Tauri through `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`; create the GitHub Environment `signing` with the
deployment-branch policy "Selected branches and tags" = `main`, and store both values as **environment secrets** of
`signing`, not as repository secrets (PD-22; any later job that uses these secrets, such as Phase 11's manager release,
must reference the same environment);
never use a fixture key (`fixtures/kat/`, `fixtures/phase2/kat-secret/`): such a `krate-signing.pub` is refused (PD-23);
(3) merge instructions: create the environment and its secrets,
commit `krate-signing.pub`, then merge, because from the merge on every real `release.yml` run fails with
`E_SIGNING_CONFIG` until both are done, and on any facts failure; SC-018 / SC-019 are dispatched after the merge, from
`main` (the `signing` environment admits no other ref); a real run refuses a version whose tag or release already exists
(PD-21), so a release left half-created by a failed `Create Release` is deleted (release and tag) before re-dispatching,
and an already published version is never re-released (bump `version.json` instead). The text
never shows a secret-key file's header at a line start (the FR-024 scan, rule (b)), and contains no empty-password
instruction.

### 4.8 `.github/workflows/release.yml`

Only additions, except the `Create Release` step's new inputs and `if:`, the `release` job's `permissions` and new
`environment: signing` (PD-22), and `deploy-pages`'s `if:`. No line in `:189-250`, `:285-337`, `:379-403` changes
(FR-081). The **Phase 2 step names** (the fixed list PD-7 and SC-014 use): `Setup Node (facts)`, `Install feed-contract
deps`, `Record macOS facts`, `Upload macOS facts`, `Record Linux facts`, `Upload Linux facts`, `Tag must not exist`,
`Download macOS facts`, `Download Linux facts`, `Setup Node (signing)`, `Cache rsign2`, `Install rsign2`, `Check rsign2
version`, `Signing configuration and key`, `Sign assets`, `Delete signing key (after signing)`, `Write release.json and
SHA256SUMS`, `Verify signatures and sums`, `Attest build provenance`, `Stage dry-run artifact`, `Upload dry-run
artifact`, `Verify dry-run artifact`, `Delete signing key`.

**Key lifetime (PD-20):** in the `release` job, `krate.key` exists from `Signing configuration and key` to `Delete signing
key (after signing)`, and only `Sign assets` runs in between; every later step (`Write release.json and SHA256SUMS`,
`Verify signatures and sums`, `Create Release`, `Attest build provenance`, the dry-run steps) needs only `krate.pub`.

```yaml
# on.workflow_dispatch.inputs, after prerelease (:45-48)
      dry_run:
        description: 'Build, sign with a throwaway key and upload as workflow artifacts only: no tag, no release, no Pages deploy'
        type: boolean
        default: false
```

`macos-installer`, between `Build Installer` (ends `:337`) and `Upload Installer` (`:339`):

```yaml
      - name: Setup Node (facts)
        uses: actions/setup-node@v4
        with:
          node-version: '24'

      - name: Install feed-contract deps
        run: npm ci --prefix tools/feed-contract

      - name: Record macOS facts
        env:
          PKG: ${{ needs.version.outputs.plugin_name }}-${{ needs.version.outputs.version }}-macOS.pkg
        run: |
          set -euo pipefail
          pkgutil --expand-full "$PKG" "$RUNNER_TEMP/pkg-x"
          node tools/release-facts.js darwin --pkg "$PKG" --expanded "$RUNNER_TEMP/pkg-x" --out darwin-facts.json > "$RUNNER_TEMP/macho.tsv"
          while IFS=$'\t' read -r want file; do
            got=$(lipo -archs "$RUNNER_TEMP/pkg-x/$file" | tr ' ' '\n' | sed '/^$/d' | LC_ALL=C sort | paste -sd ' ' -)
            if [ "$got" != "$want" ]; then echo "::error::lipo -archs '$file' = '$got', release-facts = '$want'"; exit 1; fi
          done < "$RUNNER_TEMP/macho.tsv"
          jq -r '"platform_key=\(.platform_key) min_os=\(.min_os)"' darwin-facts.json

      - name: Upload macOS facts
        uses: actions/upload-artifact@v7
        with:
          name: macos-facts
          path: darwin-facts.json
          if-no-files-found: error
```

`linux-archive`, between `Create Archive` (ends `:403`) and `Upload Archive` (`:405`): `Setup Node (facts)`, `Install
feed-contract deps`, then

```yaml
      - name: Record Linux facts
        env:
          PLUGIN_NAME: ${{ needs.version.outputs.plugin_name }}
          VERSION: ${{ needs.version.outputs.version }}
        run: |
          set -euo pipefail
          umask 000   # PD-13: extracted modes = archive modes
          TARBALL="${PLUGIN_NAME}-${VERSION}-Linux-x64.tar.gz"
          node tools/release-facts.js linux-precheck --tarball "$TARBALL"   # PD-26: listing + layout before tar runs
          mkdir -p "$RUNNER_TEMP/tar-x"
          tar -xzf "$TARBALL" -C "$RUNNER_TEMP/tar-x"
          objdump -T "$RUNNER_TEMP/tar-x/${PLUGIN_NAME}.vst3/Contents/x86_64-linux/${PLUGIN_NAME}.so" > "$RUNNER_TEMP/objdump.txt"
          node tools/release-facts.js linux --tarball "$TARBALL" --extracted "$RUNNER_TEMP/tar-x" --objdump "$RUNNER_TEMP/objdump.txt" --out linux-facts.json
          jq -r '"glibc_floor=\(.min_os) payload_files=\(.payload.files)"' linux-facts.json
```

and `Upload Linux facts` (`linux-facts`, `if-no-files-found: error`).

`release` job: `permissions` becomes `contents: write`, `id-token: write`, `attestations: write` (FR-046), and the job
gains `environment: signing` (PD-22) beside `runs-on`. After `Generate Release Notes` and before `Create Release`:

```yaml
      - name: Tag must not exist                 # PD-21: never replace a published release's assets
        if: ${{ !inputs.dry_run }}
        env:
          GH_TOKEN: ${{ github.token }}
          TAG: ${{ needs.version.outputs.tag }}
        run: |
          set -euo pipefail
          if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then echo "::error::tag $TAG already exists"; exit 1; fi
          gh release list --repo "${{ github.repository }}" --limit 1000 --json tagName | jq -r '.[].tagName' > "$RUNNER_TEMP/release-tags.txt"
          test "$(wc -l < "$RUNNER_TEMP/release-tags.txt")" -lt 1000
          if grep -Fxq -- "$TAG" "$RUNNER_TEMP/release-tags.txt"; then echo "::error::release $TAG already exists"; exit 1; fi
      - name: Download macOS facts
        uses: actions/download-artifact@v8
        with: { name: macos-facts, path: facts }
      - name: Download Linux facts
        uses: actions/download-artifact@v8
        with: { name: linux-facts, path: facts }
      - name: Setup Node (signing)
        uses: actions/setup-node@v4
        with: { node-version: '24' }
      - name: Install feed-contract deps
        run: npm ci --prefix tools/feed-contract
      - name: Cache rsign2                       # as feed-tools.yml
        id: rsign-cache
        uses: actions/cache@v5
        with:
          path: |
            ~/.cargo/bin/rsign
            ~/.cargo/bin/rsign.exe
          key: rsign2-0.6.7-${{ runner.os }}-${{ runner.arch }}
      - name: Install rsign2
        if: steps.rsign-cache.outputs.cache-hit != 'true'
        run: cargo install rsign2 --version 0.6.7 --locked
      - name: Check rsign2 version
        run: test "$(rsign --version)" = "rsign2 0.6.7"

      - name: Signing configuration and key
        env:
          KRATE_SIGNING_KEY: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}
          KRATE_SIGNING_KEY_PASSWORD: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD }}
        run: |
          set -euo pipefail
          umask 077
          MODE=$(node tools/gen-release-manifest.js --signing-config --dry-run ${{ inputs.dry_run }} --public-key tools/feed-contract/keys/krate-signing.pub)
          echo "$MODE"
          case "$MODE" in
            signing=throwaway) rsign generate -W -p "$RUNNER_TEMP/krate.pub" -s "$RUNNER_TEMP/krate.key" < /dev/null ;;
            signing=on)
              node tools/minisign.js decrypt-key --out "$RUNNER_TEMP/krate.key"
              cp tools/feed-contract/keys/krate-signing.pub "$RUNNER_TEMP/krate.pub" ;;
            *) echo "::error::unexpected signing mode"; exit 1 ;;
          esac

      - name: Sign assets
        env: { PLUGIN: ${{ needs.version.outputs.plugin }} }
        run: |
          set -euo pipefail
          node tools/gen-release-manifest.js --trusted-comments --version-json "plugins/${PLUGIN}/version.json" \
            --assets installers --darwin-facts facts/darwin-facts.json > "$RUNNER_TEMP/comments.tsv"
          test "$(wc -l < "$RUNNER_TEMP/comments.tsv")" -eq 3
          while IFS=$'\t' read -r asset comment; do
            rsign sign -W -s "$RUNNER_TEMP/krate.key" -t "$comment" -x "installers/${asset}.minisig" "installers/${asset}" < /dev/null
          done < "$RUNNER_TEMP/comments.tsv"

      - name: Delete signing key (after signing)  # PD-20: no later step runs while the key exists
        run: rm -f "$RUNNER_TEMP/krate.key"

      - name: Write release.json and SHA256SUMS
        env:
          PLUGIN: ${{ needs.version.outputs.plugin }}
          TAG: ${{ needs.version.outputs.tag }}
        run: |
          set -euo pipefail
          node tools/gen-release-manifest.js --version-json "plugins/${PLUGIN}/version.json" \
            --setup-iss "plugins/${PLUGIN}/installers/windows/setup.iss" --assets installers \
            --darwin-facts facts/darwin-facts.json --linux-facts facts/linux-facts.json \
            --repo "${{ github.repository }}" --pub-date "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
            --public-key "$RUNNER_TEMP/krate.pub" --out installers/release.json --sums-out installers/SHA256SUMS
          test "$(jq -r .tag installers/release.json)" = "$TAG"   # PD-11

      - name: Verify signatures and sums
        run: |
          set -euo pipefail
          cd installers
          while IFS=$'\t' read -r asset comment; do
            node ../tools/minisign.js verify -p "$RUNNER_TEMP/krate.pub" -x "${asset}.minisig" "${asset}"
            rsign verify -p "$RUNNER_TEMP/krate.pub" -x "${asset}.minisig" "${asset}"
          done < "$RUNNER_TEMP/comments.tsv"
          sha256sum -c SHA256SUMS
```

`Create Release` gains `if: ${{ !inputs.dry_run }}`, `make_latest: false`, `target_commitish: ${{ github.sha }}`,
`fail_on_unmatched_files: true`, `overwrite_files: false` (PD-21, second layer behind `Tag must not exist`), and four
lines in `files` after the three unchanged ones: `installers/SHA256SUMS`, the
three `installers/<asset>.minisig`, `installers/release.json` (seven files, one call, D8). After it:

```yaml
      - name: Attest build provenance              # PD-12: after the release exists
        if: ${{ !inputs.dry_run }}
        uses: actions/attest-build-provenance@v4
        with:
          subject-path: |
            installers/${{ needs.version.outputs.plugin_name }}-${{ needs.version.outputs.version }}-Windows-x64.exe
            installers/${{ needs.version.outputs.plugin_name }}-${{ needs.version.outputs.version }}-macOS.pkg
            installers/${{ needs.version.outputs.plugin_name }}-${{ needs.version.outputs.version }}-Linux-x64.tar.gz

      - name: Stage dry-run artifact
        if: ${{ inputs.dry_run }}
        run: |
          mkdir -p "$RUNNER_TEMP/release-dry-run"
          cp installers/* "$RUNNER_TEMP/release-dry-run/"
          cp "$RUNNER_TEMP/krate.pub" "$RUNNER_TEMP/release-dry-run/krate.pub"
      - name: Upload dry-run artifact
        if: ${{ inputs.dry_run }}
        uses: actions/upload-artifact@v7
        with: { name: release-dry-run, path: '${{ runner.temp }}/release-dry-run', if-no-files-found: error }
      - name: Verify dry-run artifact            # FR-048: the Node verifier over the real assets
        if: ${{ inputs.dry_run }}
        run: |
          set -euo pipefail
          cd "$RUNNER_TEMP/release-dry-run"
          while IFS=$'\t' read -r asset comment; do node "$GITHUB_WORKSPACE/tools/minisign.js" verify -p krate.pub -x "${asset}.minisig" "${asset}"; done < "$RUNNER_TEMP/comments.tsv"

      - name: Delete signing key
        if: always()
        run: rm -f "$RUNNER_TEMP/krate.key"
```

The staged directory never holds `krate.key` (it is created only under `$RUNNER_TEMP`, not `installers/`). The `release`
job keeps `needs: [version, windows-installer, macos-installer, linux-archive]` and gains no job-level `if:`, so a failed
facts job stops it (FR-047a). `deploy-pages` gains `if: ${{ !inputs.dry_run }}`.

### 4.9 `.github/workflows/backfill-sign.yml` (FR-058, FR-059, FR-059a)

```yaml
name: Backfill sign
on:
  workflow_dispatch:
    inputs:
      dry_run: { description: 'Sign and upload as workflow artifacts only (no gh release upload)', type: boolean, default: true }
concurrency: { group: backfill-sign, cancel-in-progress: false }
permissions: { contents: read }
env:
  GH_TOKEN: ${{ github.token }}
jobs:
  plan:          # ubuntu-latest; checkout fetch-depth 0; setup-node 24; npm ci;
                 # node tools/backfill-sign.js plan --plugins-dir plugins --repo ${{ github.repository }} --out "$RUNNER_TEMP/plan/plan.json"
                 # upload-artifact backfill-plan
  facts-macos:   # needs plan; macos-latest; checkout; node; npm ci; download backfill-plan;
                 # backfill-sign.js download --format pkg --dir "$RUNNER_TEMP/dl" --repo …
                 # backfill-sign.js facts --plan … --os darwin --dir "$RUNNER_TEMP/dl" --work "$RUNNER_TEMP/work" --out "$RUNNER_TEMP/facts"
                 # upload-artifact backfill-facts-darwin (if-no-files-found: error)
  facts-linux:   # same on ubuntu-latest with --format tar.gz / --os linux, step starts with `umask 000` (PD-13);
                 # upload-artifact backfill-facts-linux
  apply-dry:
    if: ${{ inputs.dry_run }}
    needs: [plan, facts-macos, facts-linux]
    runs-on: ubuntu-latest
    environment: signing          # PD-22: the secrets exist only for jobs on main that reference it
    permissions: { contents: read }
    steps: <APPLY_STEPS with --dry-run>
  apply-live:
    if: ${{ !inputs.dry_run }}
    needs: [plan, facts-macos, facts-linux]
    runs-on: ubuntu-latest
    environment: signing
    permissions: { contents: write }
    steps: <APPLY_STEPS without --dry-run>
```

`plan`, `facts-macos` and `facts-linux` reference no environment and receive no signing secret.

`APPLY_STEPS` (identical text in both jobs except the one `--dry-run` token): checkout `fetch-depth: 0` (PD-14); setup-node
24; `npm ci --prefix tools/feed-contract`; `Cache rsign2` / `Install rsign2` / `Check rsign2 version` as §4.8;
download `backfill-plan` to `$RUNNER_TEMP/plan`, `backfill-facts-*` with `pattern: backfill-facts-*` and
`merge-multiple: true` to `$RUNNER_TEMP/facts` (download-artifact inputs, §1.2); the **key step** with `env:
KRATE_SIGNING_KEY: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}` and `KRATE_SIGNING_KEY_PASSWORD: ${{
secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD }}`, running `umask 077`, `node tools/gen-release-manifest.js --signing-config
--dry-run false --public-key tools/feed-contract/keys/krate-signing.pub` and `node tools/minisign.js decrypt-key --out
"$RUNNER_TEMP/krate.key"`; `node tools/backfill-sign.js apply --plan "$RUNNER_TEMP/plan/plan.json" --plugins-dir plugins
--facts "$RUNNER_TEMP/facts" --key "$RUNNER_TEMP/krate.key" --public-key tools/feed-contract/keys/krate-signing.pub --repo
${{ github.repository }} --work "$RUNNER_TEMP/work" --out-dir "$RUNNER_TEMP/out" [--dry-run]` (which removes
`--key` itself when pass 1 ends, before its first upload or audit call, PD-20); upload `$RUNNER_TEMP/out` as
`backfill-sign-output` with `if: always()`; `Delete signing key` with `if: always()` as the backstop.

Both facts jobs write the report even when releases fail, so they stay green and one bad old pkg cannot stop the other
eight (FR-058); `apply` turns a report entry into exit 3.

### 4.10 `.github/workflows/feed-tools.yml` (FR-073, PD-15)

Path filters (push and pull_request) gain `tools/release-facts.js`, `tools/backfill-sign.js`,
`.github/workflows/release.yml`, `.github/workflows/backfill-sign.yml`. The `node --test` step becomes:

```yaml
      - name: node --test
        shell: bash
        run: |
          set -o pipefail
          node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" | tee "$RUNNER_TEMP/tap.txt"
          node - "$RUNNER_TEMP/tap.txt" <<'TAPCHECK'
          const lines = require("fs").readFileSync(process.argv[2], "utf8").split(/\r?\n/);
          const PLAN = "1..46";
          const NAMES = [ /* the 22 names of §7.1, exactly */ ];
          const bad = [];
          if (!lines.includes("# fail 0")) bad.push('no "# fail 0" line');
          const plans = lines.filter((l) => /^1\.\.\d+$/.test(l));
          if (plans.length !== 1 || plans[0] !== PLAN) bad.push(`top-level plan ${JSON.stringify(plans)}, want ${PLAN}`);
          const top = lines.filter((l) => /^(not )?ok \d+ - /.test(l));
          const nameOf = (l) => l.replace(/^(not )?ok \d+ - /, "").replace(/ # (SKIP|TODO)\b.*$/, "");
          for (const n of NAMES) if (!top.some((l) => l.startsWith("ok ") && nameOf(l) === n)) bad.push(`missing: ${n}`);
          const RSIGN = [ /* the SC-003, SC-025 and SC-027 names of §7.1, exactly */ ];
          if (process.env.CI) {
            for (const l of top) if (/ # SKIP\b/.test(l)) bad.push(`skipped on CI: ${l}`);
            for (const n of RSIGN) {   // PD-27: the whole block, subtests included
              const start = lines.indexOf(`# Subtest: ${n}`);
              const end = lines.findIndex((l, i) => i > start && /^(not )?ok \d+ - /.test(l) && nameOf(l) === n);
              if (start < 0 || end < 0) { bad.push(`no TAP block: ${n}`); continue; }
              for (const l of lines.slice(start, end + 1)) if (/ # SKIP\b/.test(l)) bad.push(`skipped rsign case: ${n}: ${l.trim()}`);
            }
          }
          console.log(`TAP plan=${plans.join(",")} top-level=${top.length}`);
          if (bad.length) { console.error("TAP check failed:\n" + bad.join("\n")); process.exit(1); }
          TAPCHECK
```

No serial flag (SC-021). `specs/krate-manager-phase2/evidence/tap-check.js` reads `feed-tools.yml`, extracts the text
between `<<'TAPCHECK'` and the closing `TAPCHECK` line (de-indented by the step's indentation), and runs it with
`new Function`-free `vm.runInNewContext` over a TAP file given on its command line, with a sandbox `process` whose `env`
is `{CI: '1'}` (PD-27), so the local close applies the CI rule: a local skip of SC-003, SC-025 or SC-027, top-level or
in any subtest, fails the local check and the row stays open (D13). The TAP reporter escapes `#` and `\` in names (none
of the 22 contains either).

### 4.11 Phase 1 test-file changes

- **`helpers/write-guard.js` (FR-071):** `checkSpawn` admits `rsign` beside `git` / `tar` (`:235`), with
  `checkRsign(op, args, cwd)` accepting exactly: `['--version']`; `['generate', '-W', '-p', P, '-s', P]`; `['sign', '-W',
  '-s', P, '-t', T, '-x', P, P]`; `['verify', '-p', P, '-x', P, P]`, where every `P` is `rejectDotDot`-checked and
  `assertUnderTmp(path.resolve(cwd, P))`-checked and `T` is any string without CR/LF. Any other shape throws
  `E_WRITE_OUTSIDE_TMP` ("rsign shape"). The header comment's allowlist sentence gains rsign.
- **`hygiene.test.js` (FR-072, SC-015, SC-026):** completeness list (`:110`) and `scannedFiles()` single list (`:34-46`)
  gain the four tools; `CHILD_EXEMPT` (`:65-69`) gains `tools/backfill-sign.js` and
  `tools/feed-contract/tests/helpers/external-tools.js` (PD-5); the roster `products` list (`:236`) gains the four tools;
  the teeth subtest (`:145`) gains `spawnSync('rsign', ['sign', '-W', '-s', k, '-t', 'x', '-x', s, f, '--extra'], {cwd:
  tmp})` and `spawnSync('rsign', ['--version'], {cwd: repoDir})`, both refused; one new top-level test, SC-026.
- **`c3-trusted-comment.test.js` (PD-6):** `:275` becomes a comparison with exactly one allowed path,
  `fixtures/phase2/kat-secret/kat.key`, and asserts for that file: its first line is the rsign encrypted-key header
  (rule (b)); its base64 line decodes to 158 bytes
  starting `EdScB2`, and its key id bytes equal those of `kat-secret/kat.pub`. No other assertion changes.

### 4.12 Test helpers and fixtures

#### 4.12.1 `helpers/macho.js`

`thin({cputype, cpusubtype = 0, commands: [{cmd, body: Buffer}], magic = 0xfeedfacf})` → a 32-byte header plus load
commands (each `cmd`, `cmdsize` LE, body, padded to 8); `buildVersion(minos, platform = 1)`, `versionMin(version)`;
`fat(slices, {wide = false})` → BE fat header and `fat_arch` / `fat_arch_64` entries with slices aligned to 4096. Named
constants mirror `loader.h` / `fat.h` / `machine.h` (spec External facts; §1.2).

#### 4.12.2 `helpers/external-tools.js` (the only test-side spawner, PD-5)

`rsignGate(t)`: runs `rsign --version` with `cwd` a temp dir; returns `true` when stdout is exactly `rsign2 0.6.7\n`;
otherwise, when `process.env.CI` is unset, calls `t.skip('<found version or ENOENT>')` and returns `false`, and when `CI` is
set throws (D13). `rsignGenerate(dir)`, `rsignSign(dir, sec, comment, sig, file)`, `rsignVerify(dir, pub, sig, file)`
(returns `{status, stdout, stderr}`), `extractTarGz(src, dest)` (`tar -xzf src -C dest`, POSIX legs only). Every call
passes `cwd` = the test's temp dir and `stdio: ['ignore', 'pipe', 'pipe']`; the shapes are exactly FR-071's. Tests copy
any repo fixture an rsign call reads (`kat.pub`, the KAT archive) into their temp dir first, because the guard requires
every path argument under the temp root.

#### 4.12.3 Fixtures under `fixtures/phase2/`

- `KatPlugin/`: `version.json` `{"name": "KatPlugin", "version": "1.2.3"}`; `installers/windows/setup.iss` with
  `AppId={{0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0}` (synthetic). Together with the Phase 1 KAT tarball they give SC-006's
  asset set (stub `.exe` 1,000 bytes and stub `.pkg` 2,000 bytes are written at test time).
- `objdump/`: three hand-written `objdump -T` excerpts in GNU binutils layout (`DF *UND*  0000000000000000 (GLIBC_2.34)
  memcpy`), as SC-005 lists.
- `backfill/`: nine synthetic plugins (`Alphaplug` … `Iotaplug`, names checked against no `release.yml` option) with
  `version.json` and `setup.iss` (`version.json` = `{"name": "<Synth>", "version": "<current stable version>"}`, except
  `Gammaplug`, which carries `"preset_subdir": "Kits"` and `"identifier": "com.krateaudio.gammakit"`; the fake `git show
  <tag>:…` returns these files' text, so the two fields reach Gammaplug's `release.json` only through D12's tag read);
  `state.json` = per plugin one current stable release, one older stable, one newer prerelease, one draft, each with
  asset names; every asset's `digest` is `"auto"` except all of `Iotaplug`'s, which are `null` (the null branch):
  `fake-gh.js` replaces `"auto"` with `sha256:<lower-case hex>` of the bytes it generates, so the positive digest path
  runs for eight plugins (sizes are filled in by the helper the same way); `fake-gh.js`
  generates the asset bytes into the test's temp dir: stub `.exe` / `.pkg` bytes, and per plugin a C4-layout tarball
  (`<Name>.vst3/…`, `presets/`, `README.txt`, `CHANGELOG.txt`, modes `0644` / `0755`) written as GNU tar headers through
  `zlib.gzipSync`, the technique `fixtures/listings/make-bad.js` already uses. Its `gh release upload` fake appends the
  uploaded names to the release in the temp state, so the audit and the second run (SC-011) see them.
- `kat-secret/` (FR-064, SI-6): **user step**, once, at a terminal on this box, in a fresh `mktemp`-style temp dir:
  `rsign generate -p kat.pub -s kat.key` (typing the published test password `krate-kat-test-password` twice), `rsign
  sign -s kat.key -t "krate:1:KatPlugin:1.2.3:linux-x86_64:message.txt" -x message.txt.minisig message.txt` (password
  again), `rsign verify -p kat.pub -x message.txt.minisig message.txt`; then copy the four files into
  `kat-secret/` and normalise the `.minisig` to LF. `README.md` records date, `rsign --version`, the commands and outputs,
  and the password, without reproducing the key file's header line at a line start. The implementer writes everything
  else first; SC-027 and SC-028 stay red until the files exist.

### 4.13 Evidence scripts

- `specs/krate-manager-phase2/evidence/scope-check.js` (SC-020): `git diff --name-only <base>..` against the FR-080 list
  (globs), and `git diff -U0 <base> -- .github/workflows/release.yml` hunks: any `-` line, or a `+` line whose
  pre-image position falls inside `:189-250`, `:285-337`, `:379-403`, is a protected-range violation. Prints counts;
  exit 1 on any. Copied in form from `specs/krate-manager-phase1/evidence/scope-check.js`.
- `specs/krate-manager-phase2/evidence/tap-check.js` (PD-15).

---

## 5. Dependencies

| Dependency | Version | Where | Why / source |
|---|---|---|---|
| Node.js built-ins `node:crypto` (`createHash('blake2b512')`, `sign`/`verify` Ed25519, `scryptSync`, `createPublicKey`/`createPrivateKey` DER), `node:fs`, `node:path`, `node:os`, `node:zlib` (via `listing.js`), `node:child_process` (`backfill-sign.js`, `external-tools.js` only) | Node 24 (CI `setup-node` `'24'`; this box v24.7.0) | all tools | https://nodejs.org/docs/latest-v24.x/api/crypto.html; measured §1.2 |
| `ajv` | 8.20.0 (unchanged) | `contract.js` | already the only package (`tools/feed-contract/package.json`) |
| **No new npm package.** No YAML library (PD-8), no minisign npm package (none exists as a verifier, roadmap line 359), no BLAKE2 package (PD-1, in-file) | — | — | Cross-cutting: prefer the standard library |
| `rsign2` | 0.6.7, `cargo install --locked` | CI signing, interop tests | roadmap lines 352-357; https://github.com/jedisct1/rsign2; source read in `~/.cargo/registry/src/*/rsign2-0.6.7` (depends on `minisign` 0.9.1) |
| `softprops/action-gh-release` | `@v2` (unchanged major; v2.6.2 is the final v2) | `release.yml` | §1.2 |
| `actions/attest-build-provenance` | `@v4` | `release.yml` | roadmap line 371, spec Q7; §1.2 |
| `actions/checkout@v6`, `actions/setup-node@v4`, `actions/cache@v5`, `actions/upload-artifact@v7`, `actions/download-artifact@v8` | the versions this repo already pins | both workflows | `release.yml:68`, `:208`, `:245`; `feed-tools.yml`; download-artifact `pattern` / `merge-multiple` §1.2 |
| `gh` CLI | preinstalled on GitHub-hosted runners | `backfill-sign.yml` only | https://cli.github.com/manual/ (§1.2) |
| `pkgutil`, `lipo` (macOS runner), `tar`, `objdump`, `jq`, `sha256sum` (Ubuntu runner) | runner images | workflow steps | spec External facts |

---

## 6. Safety design

- **Injected roots.** No Phase 2 tool has a default path: every input and output is a command-line argument; the only
  environment reads are `KRATE_SIGNING_KEY`, `KRATE_SIGNING_KEY_PASSWORD` and `RUNNER_TEMP`. Tests run under the write guard
  (writes only under `realpath(os.tmpdir())`, spawns only in fixed shapes) and pass fakes for `gh`, `git`, `exec`. Nothing
  touches a plug-in, preset or state folder, ProgramData or the registry, on this box or a runner. There is no app, no
  helper and no elevation in this phase.
- **Verify before use.** Release job order: facts measured from the finished assets (with their SHA-256) → sign → `release.json`
  only after every signature verified against the committed key and bound by `checkBinding`, the payload digest
  recomputed from the tarball and `checkReleaseManifest` passed → independent re-verification by `minisign.js` and
  `rsign verify` plus `sha256sum -c` → only then `Create Release`, and only for a tag that has no git tag and no release
  yet (PD-21), with `overwrite_files: false`. Backfill: GitHub's size (always) and `digest` (when GitHub supplies one,
  PD-28, the count logged) checked on every download before signing; every new signature verified against the committed key before the first upload (PD-9); an
  asset audit after the run. Nothing downloaded is executed: pkgs are only expanded, tarballs only listed and extracted
  into runner temp dirs, binaries only read.
- **Archive paths.** The only archive extracted (by `tar` on a runner, into `$RUNNER_TEMP`) is the release's own tarball;
  its listing passes Phase 1's canonical-listing rules (`listingFromTarGz` rejects absolute paths, `..`, drive letters,
  escaping links, `E_LISTING_*`) and `checkLinuxLayout` **before `tar` runs** (PD-26: `release-facts.js linux-precheck` in
  `Record Linux facts`, `linuxPrecheck` in-process in `backfill-sign.js facts`), and `objdump` opens only a `.so` the
  listing shows as a regular file; the extracted tree is then compared with the listing. The manager-side zip-slip guard
  is Phase 8's.
- **Keys.** Real key: password-protected secret plus password secret, both **environment secrets** of `signing`, which
  only jobs on `main` that reference it receive (PD-22); decrypted only by `decrypt-key` into a mode-0600 file under
  `$RUNNER_TEMP` (`umask 077` on the step as a second layer; the out path checked canonically, PD-19); it exists only
  while signatures are made: deleted by `Delete signing key (after signing)` right after `Sign assets` in `release.yml`,
  and by `backfill-sign.js apply` itself when pass 1 ends, with the `if: always()` delete as the backstop in each job
  (PD-20); never uploaded (the staged dry-run directory is built from `installers/*` and `krate.pub` only). Secrets
  appear only in the `env` of one step per job; neither value is ever an argument, printed or in an error message
  (SC-022, SC-028 assert 0 leaks). The backfill's dry run holds the secret in a job whose token is `contents: read`. A
  committed public key equal to a fixture key is refused (PD-23). Tests use throwaway keys in memory or in temp dirs; the
  one committed secret key is the test-only, password-published `kat-secret/kat.key`.
- **Strings never become paths unchecked.** Asset names come from `assetNameFor` over a validated name and version; tags
  become directory names only through `replace('/', '-')` after the C2 tag pattern held; `decrypt-key --out` must
  resolve, through the realpath of its existing parent, under the realpath of `RUNNER_TEMP` (PD-19).
- **Tauri capability set, elevated helper.** Not applicable in this phase (Phases 4 and 10).

---

## 7. Test plan

All Node tests: `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` (quoted glob, files in parallel
child processes). Every new test file's first statement is `require('./helpers/write-guard.js');`. Each Phase 2 test
file holds only its SC-named top-level tests; everything else is a subtest (FR-073).

### 7.1 The 22 named top-level tests

| SC | File | Test name (exact) | Assertion strategy |
|---|---|---|---|
| SC-001 | `minisign.test.js` | `minisign.js verifies the rsign2 KAT` | `verifyFile` on `fixtures/kat/` → comment + `440FA75168B852F7`; 4 subtests on temp copies: archive byte, decoded line-2 sig byte (re-encoded), key-id byte, one comment char → the named codes |
| SC-002 | `minisign.test.js` | `minisign.js sign and verify round trip` | `generateKeyPair` in memory; 1 MiB random temp file; round trip, LF/final-LF, `parseMinisigText`; second pair → `E_MINISIG_KEY_ID`; second pair with first key id → `E_MINISIG_SIGNATURE`; legacy `Ed` → `E_SIGNATURE_TEXT`; CRLF file verifies; 4 malformed public keys → `E_MINISIG_PUBKEY` |
| SC-003 | `minisign.test.js` | `minisign.js and rsign2 interoperate` | `rsignGate`; (a) `formatPublicKey` + `signFile` in temp → `rsignVerify` status 0, stderr has `Trusted comment: <tc>` (PD-17); (b) `rsignGenerate` + `rsignSign` → `verifyFile` returns the comment |
| SC-004 | `release-facts.test.js` | `release-facts reads Mach-O arch sets and minimum macOS` | `helpers/macho.js` buffers and temp trees for every listed case; the symlink subtest runs where `fs.symlinkSync` works (POSIX legs), skipped as a **subtest** on win32 |
| SC-005 | `release-facts.test.js` | `release-facts reads the glibc floor` | the three `objdump/` samples → `2.34`, `2.10`, `E_FACTS_NO_GLIBC` |
| SC-006 | `gen-release-manifest.test.js` | `gen-release-manifest builds a verified release.json` | stub assets + KAT tarball in a temp `assets/`; throwaway key signs with `trustedComments` output; facts files built by `release-facts` functions (darwin facts written directly from a synthetic `{platform_key: 'darwin-universal', …}` with the stub pkg's size/hash); `main` exit 0; independent `statSync` / `createHash('sha256')`; `verifyBuffer`; `checkReleaseManifest`; payload = `{10, db287e49…7ad6}`; `SHA256SUMS` exact; `install` **deep-equals** `{vst3_bundle: 'KatPlugin.vst3', au_component: 'KatPlugin.component', windows_uninstall_key: '{0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0}_is1', macos_pkg_id: 'com.krateaudio.katplugin'}` (no `preset_subdir` key); subtest: a temp `version.json` adding `"preset_subdir": "Kits"` and `"identifier": "com.krateaudio.katkit"` gives `install.preset_subdir === 'Kits'` and `macos_pkg_id === 'com.krateaudio.katkit'` (FR-025); 3 tamper subtests with fresh output dirs |
| SC-007 | `gen-release-manifest.test.js` | `gen-release-manifest refuses what it cannot bind` | six named failures each with "no output file exists"; subtest 7 pre-creates outputs and compares bytes and `mtimeMs`, and lists the directory for leftover `.tmp` files; subtest 8 (FR-023): linux facts with the right `asset`/`size`/`sha256` but `payload.files` changed, and separately `payload.sha256` changed → exit 1 `E_LISTING_DIGEST`, no output; subtest 9 (PD-25): pre-created outputs, `--out` replaced by a directory → exit 1 `E_INPUT`, `SHA256SUMS` keeps bytes and `mtimeMs`, no `.tmp` / `.bak` left; subtest 10 (PD-25): pre-created outputs, `fs.renameSync` wrapped in-process to throw on the rename onto `release.json` → exit 1, both outputs keep bytes and `mtimeMs`, no `.tmp` / `.bak` left |
| SC-008 | `release-facts.test.js` | `Linux payload digest equals the extracted tree's listing` | win32: `buildManifest`'s payload and list vs the committed `KatPlugin-1.2.3-Linux-x64.tar.gz.listing` and `listing.digest` of that file's lines; POSIX: `extractTarGz` of the KAT into temp, `main(['linux', …])` exit 0 with the same payload; plus one extra file → exit 1 `E_FACTS_LISTING_MISMATCH` (POSIX half as subtests, skipped on win32 with the Phase 1 D4 reason); `linux-precheck` subtests (all OSes): the KAT → exit 0, no stdout; a generated tarball with a `../x` member, and one with a symlink escaping the root → exit 1 `E_LISTING_*` (PD-26) |
| SC-009 | `gen-release-manifest.test.js` | `gen-release-manifest prints the C3 comments` | `main(['--trusted-comments', …])` stdout = 3 exact lines; `parseTrustedComment` per line |
| SC-010 | `backfill-sign.test.js` | `backfill signs nine current releases` | `helpers/fake-gh.js` over `backfill/state.json` in a temp copy; `plan` → `download` (both formats) → `facts` darwin (fake `pkgutil` writes `macho.js` fat files into the work dir, fake `lipo` echoes the set) and linux (win32: the fake `tar` only creates the directory and `listDirectory` returns the tarball's own listing, PD-4; POSIX: the fake delegates `tar` to `extractTarGz` and the real lister runs; fake `objdump` returns a `fixtures/phase2/objdump/` sample) → `apply` with a throwaway-key `sign`; fake records every argv: 9 × 2 uploads, `--clobber` 0, no name in `assets_before`, `git show <tag>:…` calls present; 27 signatures verify + bind; 9 `checkReleaseManifest`; audit clean; the eight releases with non-null digests (all but `Iotaplug`) are `signed`, and the download logs read `digest checked: 8 of 9` per format (the positive FR-053 path); `Gammaplug`'s `release.json` `install` has `preset_subdir: 'Kits'` and `macos_pkg_id: 'com.krateaudio.gammakit'` (from the fake `git show`); the fake asserts at every `gh release upload` and audit `gh release view` call that the `--key` file no longer exists (PD-20) |
| SC-011 | `backfill-sign.test.js` | `backfill second run uploads nothing` | SC-010's flow, then `plan` + `apply` again on the mutated fake state: all `skipped (already signed)`, `uploads: 0`, exit 0 |
| SC-012 | `backfill-sign.test.js` | `backfill reports defective releases by name` | one subtest per listed defect, each on a fresh fake state; codes, exit codes (3 or 1) and per-release upload counts as the SC lists; added subtests: (a) a tenth plugin (temp copy of `backfill/plugins/` plus `Kappaplug`) with only a prerelease and a draft → summary `Kappaplug: skipped (no stable release)`, the nine signed, exit 0 (PD-10); (b) a stable tag `<name>/v1.2` → `plan` exit 1 `E_TAG_FORMAT`, no plan file, 0 uploads (PD-10); (c) the fake's second `gh release upload` (release.json) fails with nothing left behind for one release → that release `reported E_INPUT`, the other eight signed, audit run and clean (expected = `assets_before` ∪ the four names of its first call), exit 3; a following `plan` on that state classifies it `E_BACKFILL_PARTIAL` (PD-24); (d) `--public-key` a copy of `fixtures/kat/kat.pub` → `apply` exit 1 `E_SIGNING_CONFIG`, 0 uploads (PD-23); (e) a release whose fake tarball has a `../x` member, and one with an escaping symlink → `facts --os linux` reports `E_LISTING_*` and the fake records no `tar` and no `objdump` exec for it (PD-26) |
| SC-013 | `backfill-sign.test.js` | `backfill dry run uploads nothing` | SC-010 with `--dry-run`: 0 `release upload` argv, 9 complete out dirs, audit = `assets_before` |
| SC-014 | `workflow-shape.test.js` | `release.yml and backfill-sign.yml keep the Phase 2 shape` | text checks via `jobBlock` / `stepBlocks` (PD-8); roster check scoped per PD-7; the two apply jobs' `steps:` compared after removing ` --dry-run`; added subtests: every §4.8 Phase 2 step name exists in its job; `Record macOS facts` contains `lipo -archs` and `release-facts.js darwin`; `Record Linux facts` contains `release-facts.js linux-precheck` before `tar -xzf`, `objdump -T` and `release-facts.js linux`; `Verify signatures and sums` contains `minisign.js verify`, `rsign verify` and `sha256sum -c` and comes before `Create Release`; `Upload dry-run artifact` and `Verify dry-run artifact` carry `if:` on `inputs.dry_run`; key order (PD-20): the index of `Delete signing key (after signing)` is `Sign assets` + 1 and lower than those of `Write release.json and SHA256SUMS` and `Create Release`; `Tag must not exist` (PD-21) carries `if:` on `!inputs.dry_run`, contains `git rev-parse -q --verify` and `gh release list` with `--limit 1000`, and comes before `Signing configuration and key`; `Create Release` carries `overwrite_files: false`; `environment: signing` (PD-22) on the `release`, `apply-dry` and `apply-live` jobs and on no other job |
| SC-022 | `gen-release-manifest.test.js` | `signing configuration is throwaway, on or a failure` | `main(['--signing-config', …], {env})` for 4 + 1 + 7 cases (the seventh, PD-23: real run, both secrets set, `--public-key` a temp copy of `fixtures/kat/kat.pub` → exit 1 `E_SIGNING_CONFIG` naming `test key`; a dry run with that file stays `throwaway`); stdout/stderr scanned for each secret value |
| SC-023 | `minisign.test.js` | `minisign.js verify CLI prints and exits by contract` | captured `io.stdout` / `io.stderr`; exact `verified: …\n`; flipped byte → exit 1 prefix; two usage exits |
| SC-024 | `release-facts.test.js` | `release-facts darwin CLI writes facts and lipo lines` | temp `--expanded` tree with two fat files at the SC's paths; exact JSON and two exact stdout lines; usage exit; `E_FACTS_NO_MACHO` with no `--out` |
| SC-025 | `backfill-sign.test.js` | `backfill production signer drives rsign2` | `rsignGate`; `rsignGenerate` into temp; `productionDeps(tmp).sign(file, comment, key)`; text equals file; `verifyFile` + `checkBinding` |
| SC-026 | `hygiene.test.js` | `Phase 2 tools follow the tools/*.js form` | per tool: first line, `typeof main === 'function'`, `main([], io)` = 2 with `usage: node tools/<file>`; every `'E_[A-Z0-9_]+'` literal ∈ `CODES`; the 27 codes present with class `none`; subtest (SC-021 second clause): every `tools/feed-contract/tests/**/*.js` file scanned for `/concurrency:\s*(1\|false)\b/` → 0 hits |
| SC-027 | `minisign.test.js` | `minisign.js decrypts the rsign2 secret-key KAT` | `kat-secret/` with the published password: key id = `kat.pub`'s; a `signBuffer` with the result verifies against `kat.pub`; Tauri single-line form; unencrypted passthrough; 4 rejections; three more (FR-009a): (a) `kat.key` with CRLF line ends decrypts to the same key id; (b) `kat.key` with its memlimit field (bytes 46-54, not covered by the checksum) raised to 2^30 + 1 → `E_MINISIG_SECRET_KEY`, with `crypto.scryptSync` wrapped in-process to record that it was **not** called; (c) an unencrypted-form key whose `sk[32..64]` is replaced and whose checksum is recomputed with `_internal.blake2b` → `E_MINISIG_SECRET_KEY` (PD-3); BLAKE2b and scrypt-parameter anchors (§4.1) through `_internal` (PD-18) as subtests; last subtest gated by `rsignGate`: `decrypt-key` output → `rsignSign` → `rsignVerify` against a temp copy of `kat.pub` (a skip of it fails the TAP check on CI and locally, PD-27) |
| SC-028 | `minisign.test.js` | `minisign.js decrypt-key writes only under RUNNER_TEMP and cleans up` | `main(['decrypt-key', '--out', …], {env: {KRATE_SIGNING_KEY, KRATE_SIGNING_KEY_PASSWORD, RUNNER_TEMP: tmp}})` with `tmp` exactly as `fs.mkdtempSync(path.join(os.tmpdir(), 'km2-'))` returns it, **not** realpath'd, so the macOS leg passes through `/var` → `/private/var` and the Windows CI leg through the 8.3 `RUNNER~1` name (PD-19); exact stdout; mode `0600` on POSIX (subtest skipped on win32); 7 failures with directory listings showing no out and no temp file: the 4 of the spec, a missing parent directory, an existing entry at `--out`, and (POSIX legs; skipped as a subtest on win32) `--out` through a symlinked directory inside `tmp` that points to a second temp dir outside it → exit 1 `E_INPUT`, the second dir stays empty; leak scan |
| SC-029 | `workflow-shape.test.js` | `keys README states the secrets contract and the merge order` | string presence and `indexOf` order over `keys/README.md` (now including the `signing` environment, `main`, and "environment secrets", before the merge step, PD-22); regex for an empty-password instruction (`-p ''`, `-p ""`, "empty password" not preceded by "non-") → 0 hits |

Top-level count: 24 (Phase 1) + 22 = **46**.

### 7.2 FR → test map (FRs without their own SC)

| FR | Covered by |
|---|---|
| FR-001, FR-009 (module form, no child process, no keygen/encrypt writes) | SC-015 (scan), SC-026, SC-028 (only `decrypt-key` writes) |
| FR-010, FR-016 | SC-004, SC-005, SC-024 |
| FR-020, FR-021, FR-022, FR-025, FR-026, FR-028 | SC-006, SC-007 |
| FR-040 to FR-050, FR-047a | SC-014 (shape), SC-018 (PENDING-CI run) |
| FR-051 to FR-057a | SC-010 to SC-013, SC-025 |
| FR-058, FR-059, FR-059a | SC-014, SC-019 (PENDING-CI) |
| FR-060, FR-064 | SC-015 + the PD-6 assertion in `c3-trusted-comment.test.js` |
| FR-061, FR-062 | SC-029 |
| FR-070, FR-071, FR-072 | SC-015, SC-016 |
| SC-021 (both clauses) | clause 1: the unchanged Phase 1 `no serial test flags` over the changed `feed-tools.yml` (`hygiene.test.js:265-277`); clause 2: the SC-026 `concurrency:` subtest over `tools/feed-contract/tests/**/*.js` |
| FR-009b containment, PD-19 to PD-28 | SC-028 (PD-19); SC-014 + SC-010 key-file assertion (PD-20); SC-014 (PD-21, PD-22); SC-022 + SC-012 (d) (PD-23); SC-012 (c) (PD-24); SC-007 subtests 9-10 (PD-25); SC-008 + SC-012 (e) + SC-014 (PD-26); local `tap-check.js` + SC-017 (PD-27); SC-010 digest log + SC-019 evidence (PD-28) |
| FR-073 | SC-017 (CI), local `evidence/tap-check.js` |
| FR-074 | SC-026, unchanged `CONTRACT.md documents every code and field` |
| FR-075 | SC-016 (Phase 1 goldens byte-identical) |
| FR-076 | SC-026, SC-023 |
| FR-080, FR-081, FR-082 | SC-020 (`evidence/scope-check.js`) |

### 7.3 PENDING-CI rows (exact job and assertion)

| Row | Job / step | Assertion |
|---|---|---|
| SC-003, SC-025, SC-027 (rsign subtest) on all three OSes | `feed-tools.yml` `node-test (ubuntu-latest \| macos-latest \| windows-latest)`, step `node --test` | the TAP check (FR-073): plan `1..46`, `# fail 0`, the 22 names `ok`, no top-level `# SKIP` with `CI` set, and no `# SKIP` on any line of the SC-003 / SC-025 / SC-027 blocks (PD-27) |
| SC-004 symlink subtest, SC-008 POSIX half, SC-028 mode `0600` and symlinked-parent subtest, SC-010 to SC-013 with the real lister | `node-test (ubuntu-latest)`, `node-test (macos-latest)` | same TAP check (the subtests run, not skip, on POSIX) |
| SC-028 success case through a non-canonical temp path (PD-19) | `node-test (macos-latest)` (`/var` → `/private/var`), `node-test (windows-latest)` (8.3 `RUNNER~1`) | `ok` for `minisign.js decrypt-key writes only under RUNNER_TEMP and cleans up` in the TAP check |
| SC-017 | `feed-tools.yml` three legs | TAP check green, `rsign verify (KAT)` green, `Checkout unchanged` green |
| SC-018 | `release.yml` dispatch with `dry_run: true` (user) | steps `Record macOS facts` (lipo cross-check, `platform_key`, `min_os` printed), `Record Linux facts`, `Verify signatures and sums`, `Upload dry-run artifact`, `Verify dry-run artifact` green; `Create Release`, `Attest build provenance`, `deploy-pages` skipped; tag/release lists before = after |
| SC-019 | `backfill-sign.yml` dispatch with `dry_run: true` after the merge (user, FR-059a) | `apply-dry` summary: 9 `signed`, `uploads: 0`, exit 0; both facts reports `[]`; audit clean; the `digest checked: <n> of <m>` lines of both facts jobs and of `apply-dry` copied into the compliance row (PD-28; `n` is recorded, not gated); the `apply-dry` log shows the key file removed before the audit (PD-20) |

### 7.4 Local close procedure (Windows)

1. `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > f:/tmp/km2-tap.txt` (one parallel run; the
   Phase 1 run of 24 tests took seconds, so no sharding is needed; the measured wall clock goes into compliance).
2. `node specs/krate-manager-phase2/evidence/tap-check.js f:/tmp/km2-tap.txt` (PD-15, PD-27) — the script runs the CI rule
   with `CI = '1'` in its sandbox, so a skip anywhere in the SC-003, SC-025 or SC-027 block (a missing or mismatched local
   rsign) fails the check and the local rows stay open (D13; rsign2 0.6.7 is installed on this box, spec External facts).
3. `node specs/krate-manager-phase2/evidence/scope-check.js 1dec384c`.
4. `node tools/gen-specs-index.js` and check `git status` shows only FR-080 paths.

---

## 8. Build integration and gate lanes

- **Workspace layout:** none new; Phase 2 adds flat `tools/*.js` and files under `tools/feed-contract/`. No
  `apps/krate-manager/` exists yet (Phase 4), so there is no cargo, vitest, `tsc` or clippy run in this phase and no
  `package.json` script change; `tools/feed-contract/package.json` is unchanged.
- **CI workflow files:** `release.yml` (changed), `backfill-sign.yml` (new), `feed-tools.yml` (changed). `ci.yml` is not
  touched (FR-080); `manager-ci.yml` does not exist yet.
- **Gate lane:** the **pipeline lane** only: the single parallel `node --test` run of §7.4 plus the TAP check and the scope
  check. No C++ changes, so no `run-gate.js` lane and no `header-consumers.js` run (FR-082). The app lane does not exist
  yet.
- **What runs where:** local Windows: every Node SC except the POSIX-only subtests; the user's push: the three
  `feed-tools.yml` legs (path filter now includes both workflows, so a `release.yml` edit re-runs SC-014 on CI); the user's
  dispatches after the merge (FR-062 step 4): SC-018 (`release.yml`, `dry_run: true`) and SC-019 (`backfill-sign.yml`,
  `dry_run: true`, FR-059a).

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| The in-file BLAKE2b (PD-1) is wrong and the decrypt checksum fails on a real key | Three anchors in SC-027, called directly through `_internal.blake2b` (PD-18): 64-byte output equal to OpenSSL's `blake2b512` at block boundaries, RFC 7693 Appendix A vector, and rsign's own `kat.key`; plus the CI subtest where rsign signs with the decrypted key |
| A job with `environment: signing` and a narrow token (`apply-dry`: `contents: read`) cannot start, because the docs page (§1.2) says nothing about a token permission for environment jobs | Observed at the first post-merge dispatches (SC-018, SC-019); a refusal there is a job-start error before any key is decrypted; the fix is a permission line, never dropping the environment |
| `Tag must not exist` (PD-21) blocks a "re-run all jobs" after `Create Release` succeeded but a later step (`Attest build provenance`) failed | Intended: the published assets are never rebuilt. The lost attestation is human-facing only (PD-12); a release left half-created by a failed `Create Release` is deleted by the user (release and tag) before a re-dispatch, recorded in `keys/README.md`'s merge section |
| PD-23 fails closed when `fixtures/phase2/kat-secret/kat.pub` is absent, so SC-022's `signing=on` case and a real release both need the user-made fixture | The fixture is committed with the phase (§4.12.3) before any real release can run; until it exists SC-022 stays red with SC-027/SC-028, never skipped |
| The scrypt port (PD-2) derives different parameters from minisign for some key | Production keys come from `tauri signer generate` / rsign, which both use the default limits (`keypair.rs:65-75`); the KAT is made by rsign with those limits; a mismatch shows as `E_MINISIG_PASSWORD` at the first dry run, before any release |
| `rsign` writes CRLF `.minisig` on Windows (Phase 1 spec External facts, line 118) | Readers accept CRLF (`contract.js:331`); `release.json` and the backfill's out dir store `toLfMinisig` output; CI signs on Ubuntu |
| `target_commitish: ${{ github.sha }}` on a commit that is no longer the branch head: the action notes `github.token` "may not have permission" for an older commit (§1.2) | The failure is at `Create Release`, before anything is published and before attestation (PD-12); re-dispatch from the current head. Recorded for SC-018/first real release |
| A tarball member mode altered by the runner's umask produces a false `E_FACTS_LISTING_MISMATCH` | PD-13 `umask 000` in both Linux fact steps |
| An old release's pkg contains a file starting `0xcafebabe` that is not a fat Mach-O (a Java class) | `machoFacts` fails closed (`E_FACTS_MACHO_CORRUPT` / `E_FACTS_ARCH`); the backfill reports that release, the release job stops; no Krate pkg ships Java |
| Frameworks inside an AUv3 `.app` use symlinks (`Versions/Current`) | `lstat` walk skips links (FR-012), so each Mach-O is counted once through its real path |
| `gh release upload` without `--clobber` on a name that exists: behaviour undocumented (§1.2) | Never reached: names in `assets_before` are refused before the call (FR-056), and `E_BACKFILL_PARTIAL` keeps half-signed releases out of the plan |
| Immutable releases enabled on the repo later | `E_BACKFILL_IMMUTABLE` before any upload; the release job attaches all seven files in its one create call (D8) |
| D6 blocks every plugin release from the merge until the key step is done (user ruling Q1) | Merge instructions in `keys/README.md` and the spec; `E_SIGNING_CONFIG` names the missing part and the README |
| `kat.key` cannot be generated by an agent (SI-6) | User step with exact commands (§4.12.3); the rest of the phase is built first; SC-027/SC-028 stay red until it exists, never skipped |
| Heredoc TAP check drift between CI and local | `evidence/tap-check.js` executes the heredoc text it extracts from `feed-tools.yml` (PD-15) |
| App-side risks the roadmap tracks (WebKitGTK on Linux, Windows file locks while a DAW runs, admin prompts, offline behaviour) | Not reachable in this phase: no app, no install, no elevation, no feed client. Owners: Phases 4/11 (WebKitGTK), 10 (locks, prompts), 5 (offline) |

---

## 10. Open questions (for the user)

1. **Who makes `kat-secret/kat.key` (SI-6)?** (a, recommended, matches FR-064) You run the three `rsign` commands of
   §4.12.3 once at a terminal on this box, with the published test password; (b) the implementer makes it with
   `tauri signer generate -p <password> -w <tmp>` through `npx @tauri-apps/cli` (non-interactive; it would also exercise
   the Tauri single-line form with a real Tauri key), which changes FR-064's "made by rsign2" and needs a network fetch
   of the Tauri CLI on this box.
2. **Spec edits SI-1 to SI-5:** confirm the plan's resolutions (PD-4 injected lister, PD-5 two `CHILD_EXEMPT` entries,
   PD-6 exemption in `c3-trusted-comment.test.js`, PD-7 scoped roster check, the stale Review notes) so the spec rows are
   updated before the build stage; likewise SI-7 to SI-12 from review round 1.
3. **The `signing` environment (PD-22):** you create it (main-only deployment branches) and put both secrets there instead
   of in repository secrets. Consequence: `release.yml`'s `release` job, dry run included, then starts only from `main`.
   Confirm, or rule that dry runs from a branch must stay possible (that would need the environment on real runs only,
   which this plan has not verified an expression form for).

## Review notes

Review round 1 (2026-10-30), resolutions:

- **R-1 (decrypt-key containment, three reports: spec-coverage, safety-security, xplat-reality):** applied as PD-19
  (canonical parent on both sides, case fold on win32, existing-parent and no-existing-entry rules) and SC-028's
  un-canonicalised `RUNNER_TEMP` plus three new failure subtests; SI-8.
- **R-2 (private BLAKE2b / scrypt helpers vs direct anchors):** option (a), PD-18 `_internal` export; SI-7. Option (b)
  was not taken because it leaves a length-dependent BLAKE2b bug undetectable.
- **R-3 (key lifetime):** applied as PD-20 (delete right after `Sign assets`; `apply` removes `--key` after pass 1;
  SC-014 order and SC-010 key-file assertions). **Rejected in part:** the optional lazy `require('ajv/dist/2020')` in
  `contract.js`. `contract.js:358` compiles the schemas at load on purpose ("so a strict-mode violation in a schema file
  fails every require of this module"), a Phase 1 property every consumer relies on; making it lazy would move that
  failure to first use. ajv and its dependencies come from the committed lockfile through `npm ci`
  (`tools/feed-contract/package-lock.json`), and PD-20 limits the window in which any code can read a decrypted key to
  the `decrypt-key` and `Sign assets` steps.
- **R-4 (TAP skip enforcement):** PD-27 (block scan of the three rsign tests; local run with `CI = '1'`). The alternative
  of a separate top-level rsign test was not taken, so FR-073's 46 / 22 stay.
- **R-5 (backfill digests):** non-null `sha256:` digests for eight fixture plugins, `null` for one, SC-010 assertions.
- **R-6 (FR-025 values):** SC-006 deep equality plus a `preset_subdir` / `identifier` subtest; Gammaplug carries both
  through `git show`.
- **R-7 (FR-009a behaviours):** SC-027 subtests (a)-(c).
- **R-8 (`E_LISTING_DIGEST`):** SC-007 subtest 8.
- **R-9 (no stable release, `E_TAG_FORMAT`):** SC-012 subtests (a), (b); PD-10 extended; SI-9.
- **R-10 (upload failure):** followed FR-057 (per-release report, continue, audit runs, exit 3), PD-24, SC-012 (c).
- **R-11 (two-file write):** PD-25 with rollback; the reviewer's order (rename `SHA256SUMS`, then `release.json`) alone
  would still replace `SHA256SUMS` when the `release.json` rename fails, which its own suggested subtest would catch, so
  the plan adds the target pre-check and the backup-and-restore step; SC-007 subtests 9, 10.
- **R-12 (SC-021 clause 2):** SC-026 subtest; mapped in §7.2.
- **R-13 (SC-014 step contents):** added subtests (§7.1).
- **R-14 (existing tag):** PD-21. The reviewer's `gh release view … 2>&1` test was replaced by a `gh release list` lookup,
  because a `view` failure for any reason (auth, network) would read as "no such release" and pass the check; the list
  form fails closed. Skipped on dry runs, since SC-018 may be dispatched on an already released version.
- **R-15 (pre-extraction listing):** PD-26 (`linux-precheck` mode and in-process precheck in `facts`); SC-008, SC-012 (e).
- **R-16 (environment-scoped secrets):** PD-22. Correction to the suggestion: the `environment` applies to the whole
  `release` job, so `release.yml` dry runs too can start only from `main`; SC-018 is post-merge, so it is unaffected.
- **R-17 (KAT public key as production key):** PD-23 (key id and key bytes, both fixtures); SC-022 7 failures; SC-012 (d).
- **R-18 (`digest` field unverified):** cited the REST schema and the gh source (§1.2); PD-28 makes `digest` best-effort
  with the count logged and recorded in SC-019. No minimum gh version is documented, so none is claimed.
