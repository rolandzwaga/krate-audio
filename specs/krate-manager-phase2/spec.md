# Feature Specification: Krate Manager Phase 2 — Release Assets, Hashes, Signatures and the Backfill

**Spec slug:** `krate-manager-phase2`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 2 (lines 335-416); Decisions log STRATEGY (line 23), Q9 (line 32),
ROSTER (line 33), Q10 (line 34); Architecture "Feed and artifact model" (lines 126-149); Key design decisions 4 and 5
(lines 175-181); Reuse Inventory (lines 192-211); Repository Layout (lines 213-249); Cross-Cutting Constraints (lines 954-995);
Dependency Graph (lines 902-935)
**Depends on:** Phase 1 (✅, roadmap line 255): the C2 schema, the C3 grammar and binding, the C4 listing, the rsign2
known-answer fixture and `feed-tools.yml`. Phase 3 depends on this phase (`release.json` on every release: a real
release with signing not configured fails before `Create Release`, D6, so no unsigned release is cut after this phase).
No Part B phase needs this phase's output except the opt-in live test (roadmap lines 929-932).
**Status:** DRAFT — specification only, no implementation; revised 2026-10-30 after the spec review (Review notes) and clarified 2026-10-30 (Clarifications)
**Date:** 2026-10-30 · verified against `feat/krate-manager` at `1dec384c`

---

## Clarifications

### Session 2026-10-30

The user ruled on the seven open clarifications. Each line is the question, the decision, and the FR/SC that enforce it.

- Q1. When a real `release.yml` run cannot produce the manager files (signing not configured per D6, or any facts
  failure such as `E_FACTS_ARCH_MISMATCH`, `E_FACTS_NO_MINOS`, `E_FACTS_NO_GLIBC`), what happens? → (a) Fail closed for
  both: the release fails and no escape input exists. The D6 consequence is written into the phase's merge
  instructions: the user creates the signing secrets and commits `krate-signing.pub` before merging.
  [FR-029, FR-044, FR-047a, FR-062, SC-014, SC-022, SC-029]
- Q2. How does `backfill-sign.yml` get dispatched for SC-019? → (a) SC-019 stays PENDING-CI until the user merges the
  phase to main; the phase closes without it. No extra commit on main, no temporary trigger.
  [FR-059a, SC-014, SC-019]
- Q3. Does the signing key get an empty password (D4)? → No: a password-protected key, the password coming from a second
  GitHub secret, `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`. Because rsign2 0.6.7 reads a password only from a terminal,
  `tools/minisign.js` gains secret-key decryption (scrypt plus the minisign XOR and checksum), which reverses FR-009: a
  Node step decrypts the key with the password secret and writes the unencrypted key only to `RUNNER_TEMP`, rsign2
  0.6.7 then signs as the roadmap says, and every signature is verified against the committed public key by the Node
  verifier before upload. A known-answer test (a key encrypted by rsign2 with a known password), a wrong-password test
  and the temp-file cleanup are required. Phase 11 passes the same password to Tauri through
  `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`. D4 is superseded.
  [FR-001, FR-007, FR-009, FR-009a, FR-009b, FR-029, FR-044, FR-045, FR-050, FR-058, FR-059, FR-060, FR-061, FR-064,
  SC-014, SC-022, SC-027, SC-028]
- Q4. Which key does the backfill dry run sign with (D11)? → (a) The real secret and its password secret; the result is
  checked against the committed public key and only a workflow artifact is uploaded. Precondition: both secrets exist
  and `krate-signing.pub` is committed. [FR-055, FR-058, SC-014, SC-019]
- Q5. Should `Create Release` pin the tag to the commit it read (D12)? → (b) Add `target_commitish: ${{ github.sha }}`,
  so the tag is always the commit that was built and described. [FR-047, SC-014]
- Q6. Where does the committed public key live (D5)? → (a) `tools/feed-contract/keys/krate-signing.pub` (two-line
  minisign text), checked against every CI signature before upload. [FR-024, FR-044, FR-055, FR-061, FR-080, SC-014,
  SC-020]
- Q7. Attestation action and scope (D10)? → (b) `actions/attest-build-provenance` as the roadmap names it, over the three
  installers only, real runs only (dry runs skip it because attestation writes to a public transparency log). No
  roadmap deviation. [FR-046, SC-014]

### Session 2026-10-30 (plan)

- Q: Who makes `tools/feed-contract/fixtures/phase2/kat-secret/kat.key` (SI-6)? → A: The main loop, with rsign2 0.6.7
  itself, at a pseudo-terminal: WSL Ubuntu 24.04, `cargo install rsign2 --version 0.6.7 --locked` (Rust 1.99.1), each
  rsign command run under `script -qec` with the published test password `krate-kat-test-password` fed to the pty, in a
  fresh `mktemp -d` dir, steps 1-5 of the tasks.md Preconditions exactly. Done 2026-10-30: the four files are in
  `kat-secret/`, all LF. `rsign --version`: `rsign2 0.6.7`. Step 4 output (WSL): `Signature and comment signature
  verified` / `Trusted comment: krate:1:KatPlugin:1.2.3:linux-x86_64:message.txt`; the same two lines from the Windows
  rsign2 0.6.7 (exit 0). Public key: `RWQ5Atb3FxOGY19eL6lGVIKLgfU5J0qV58UHKSdQafpKhtsB/LOndMnN`. FR-064 "made by rsign2 at a
  terminal" holds (a pty is a terminal). [Q-A]
- Q: Accept the plan's spec amendments SI-1 to SI-12 (plan §0.2)? → A: Yes, all twelve as stated; T001 writes them into
  the FR/SC rows. [Q-B]
- Q: Put both signing secrets in a `signing` GitHub Environment (deployment branches: `main` only), with
  `environment: signing` on the `release` job and both `apply-*` jobs (PD-22)? → A: Yes. Every `release.yml` run, dry
  runs included, can start its `release` job only from `main`; the user creates the environment and its two secrets
  before the merge (Merge instructions). [Q-C]
- Note for T001 (plan wording, no ruling needed): an encrypted minisign key XORs the whole keynum_sk, key id included, so
  FR-060 / plan §4.11's "its key id is asserted" is checked through `decryptSecretKey` (tasks.md T009), not by reading
  the stored bytes; and plan §4.8's flow-mapping YAML snippets with `${{ }}` do not parse, so block style applies
  (tasks.md rule 14).
- Q: The backfill extracts and signs an asset whose GitHub `digest` is null after a size check only (compliance
  CC-verify). Accept that residual, or refuse such an asset? → A: Refuse it. `checkDownloaded` raises `E_BACKFILL_DIGEST`
  for a null digest, so the release lands in the download report and is never unpacked or signed. Measured before the
  ruling: `gh api repos/{owner}/{repo}/releases` over every stable release, 264 installer assets (.exe, .pkg, .tar.gz),
  0 with a null digest. The fixture now gives every asset a digest; the refusal has its own subtest. Amends FR-053.
  [CC-verify]

---
## Overview

Phase 2 makes every plugin release carry what the manager needs to trust it, without changing what the release ships.
`release.yml` keeps building and uploading the same three installer assets (Inno `.exe`, unsigned `.pkg`, Linux
`.tar.gz`). Next to them each release gains `SHA256SUMS`, one minisign signature per asset whose trusted comment binds
plugin, version, platform and asset name (Phase 1 C3), and a `release.json` manifest in the Phase 1 C2 schema. The
release jobs also record two facts nobody has checked yet: the macOS architectures and minimum macOS version of the
shipped Mach-O binaries, and the minimum glibc of the Linux plug-in. Release creation gets a `make_latest: false`
setting, a build-provenance attestation, and a `dry_run` input that builds and signs everything with a throwaway key
without tagging, releasing or deploying. CI signs with `rsign2` 0.6.7 using a password-protected key that `tools/minisign.js` decrypts into the runner's temp dir. Node tools and tests verify with
`tools/minisign.js`, an in-repo minisign implementation over `node:crypto`, which is checked against the Phase 1 KAT
and against rsign2 in both directions. A one-time `backfill-sign.yml` (Q10) attaches the same three kinds of file to
each plugin's current release, so all nine plugins can be managed at the manager's first release with no plugin
re-release.

Nothing in this phase changes an installer's content, regenerates or deploys the feed, publishes a release by itself,
adds a codesign step, changes `ci.yml`, or touches plugin source. The nine shipped plugins keep reading the live v1
feed.

---

## Scope

**In scope (roadmap Phase 2 components, lines 339-389):**

1. `tools/minisign.js`: keygen, sign and verify of the prehashed `ED` minisign format over `node:crypto`, public-key
   text read/write, secret-key decryption (scrypt) with a `decrypt-key` CLI, a `verify` CLI (roadmap lines 358-367).
2. `tools/release-facts.js`: the macOS facts (Mach-O architecture set, minimum macOS) and Linux facts (minimum glibc,
   tarball-versus-extracted-tree listing check), measured from the release assets themselves (roadmap lines 344-348,
   384-386). A new file the roadmap's tool list (roadmap line 211) does not name; see Traceability "Additions to the
   roadmap".
3. `tools/gen-release-manifest.js`: `release.json` (C2) and `SHA256SUMS` from the staged assets, their `.minisig`s and
   the facts, a mode that prints the C3 trusted comment for each asset (roadmap lines 349-350), and the signing
   configuration decision `release.yml` consumes (FR-029, D6).
4. `.github/workflows/release.yml`: fact recording in the macOS and Linux jobs; signing, verification, `SHA256SUMS`,
   `release.json`, attestation and `make_latest: false` in the `release` job; the `dry_run` input (roadmap lines
   368-376).
5. `.github/workflows/backfill-sign.yml` over `tools/backfill-sign.js` (roadmap lines 377-387), including the per-release
   facts loop and its report (FR-053a).
6. The committed public key file contract (`tools/feed-contract/keys/`, a roadmap addition, Traceability) and the CI
   secret contract (roadmap lines 388-389, Key decision 4).
7. Tests, fixtures and lane changes: new `node --test` files under `tools/feed-contract/tests/`, the write-guard
   allowlist extended to `rsign`, `feed-tools.yml` path filter and TAP threshold, one additive export from
   `tools/gen-feed.js`, and `CONTRACT.md` sections for the new error codes.

**Non-goals (owned elsewhere):**

| Not in Phase 2 | Owner |
|---|---|
| `feed.yml`, replacing the inline generator at `release.yml:645-685`, any Pages deploy or feed regeneration | Phase 3 |
| `release.yml` `deploy-pages` calling `feed.yml` with `dry_run` | Phase 3 |
| Verifying signatures in Rust, downloading assets in the manager, the compiled-in public key | Phase 7 (and Phase 11 for the updater `pubkey`) |
| Passing `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` to the Tauri updater signer | Phase 11 (named in `keys/README.md`, FR-061) |
| SC-019, the `backfill-sign.yml` dry run | PENDING-CI until the user merges the phase to main (FR-059a); the phase closes without it |
| Platform-key resolution at runtime, "not available for this Mac" for an arm64-only plugin | Phase 5 |
| Reading `moduleinfo.json` / the `0.8.0` fallback | Phase 6 |
| Extracting the Linux tarball for an install | Phase 8 |
| The live `backfill-sign.yml` run (`dry_run: false`) | A user step after the green dry run (roadmap lines 408-410), not this phase's exit |
| Making the plugins build universal, or setting `CMAKE_OSX_DEPLOYMENT_TARGET` | Outside this roadmap (roadmap lines 805-808) |
| Any macOS or Windows codesign step | Never (Key decision 5, roadmap lines 177-181) |
| Changing `ci.yml`, an installer script, `version.json`, plugin `src/` or the in-plugin parser | Never in this phase (roadmap lines 415-416) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 2 does with it |
|---|---|---|
| `.github/workflows/release.yml:26-48` | `workflow_dispatch` inputs: `plugin` choice (`:29-44`, options `:34-43`), `prerelease` boolean (`:45-48`) | 🔶 Adds a third input, `dry_run` (boolean, default `false`). |
| `.github/workflows/release.yml:51-52` | `concurrency: group: release-${{ inputs.plugin }}` | ✅ Unchanged; a dry run queues with a real run of the same plugin. |
| `.github/workflows/release.yml:57-90` | `version` job: `VERSION`, `PLUGIN_NAME` from `version.json` with `jq`; `TAG="${PLUGIN}/v${VERSION}"` (`:76`) | ✅ Unchanged; its outputs name the assets and the tag in every new step. |
| `.github/workflows/release.yml:93-98` | `build` calls `./.github/workflows/ci.yml` | ✅ Unchanged (roadmap line 416: no `ci.yml` change). |
| `.github/workflows/release.yml:189-250` | `windows-installer`: Inno build `Minionguyjpro/Inno-Setup-Action@v1.2.7` (`:239-243`), uploads `<Name>-<ver>-Windows-x64.exe` (`:245-250`) | ✅ Unchanged (Windows records no `min_os`, Phase 1 FR-016). |
| `.github/workflows/release.yml:253-344` | `macos-installer` on `macos-latest`: stages VST3, component, optional AUv3 app (`:285-298`), presets under `preset_subdir` (`:306-312`), identifier `.identifier // "com.krateaudio.<name lowercased>"` (`:321`), `pkgbuild` (`:331-337`), uploads the pkg (`:339-344`) | 🔶 Adds steps **after** `pkgbuild`: expand the built pkg, record the macOS facts, cross-check with `lipo -archs`, upload the facts artifact. Staging and `pkgbuild` lines unchanged. |
| `.github/workflows/release.yml:347-410` | `linux-archive` on `ubuntu-latest`: stages `<Name>.vst3`, `presets/`, `README.txt`, `CHANGELOG.txt` (`:379-395`); `cd staging` / `tar -czvf ../<Name>-<ver>-Linux-x64.tar.gz *` (`:402-403`) | 🔶 Adds steps **after** the archive: extract to a temp dir, `objdump -T`, record the Linux facts and the listing check, upload the facts artifact. Archive lines unchanged. |
| `.github/workflows/release.yml:413-526` | `release` job on `ubuntu-latest`, `permissions: contents: write` (`:417-418`), `actions/checkout@v6` (`:424`, no `ref:`, so the dispatch ref) with `fetch-depth: 0` (`:426`), downloads the three installers (`:428-444`), release notes (`:450-511`), `softprops/action-gh-release@v2` (`:514-526`) with `tag_name` (`:518`), `draft: false` (`:520`), `prerelease` (`:521`), `files` = the three assets (`:523-526`), no `target_commitish` today | 🔶 Adds the facts downloads, the signing-configuration check, signing, `SHA256SUMS`, `release.json`, verification, attestation; `Create Release` gains `make_latest: false`, `target_commitish: ${{ github.sha }}`, `fail_on_unmatched_files: true` and the extra files and is skipped on a dry run; a dry-run artifact upload. Permissions gain `id-token: write`, `attestations: write`. |
| `.github/workflows/release.yml:531-692` | `deploy-pages`: landing page (`:596-643`), inline v1 feed generator (`:645-685`), `peaceiris/actions-gh-pages@v4` with `keep_files: true` (`:687-692`) | 🔶 Gains only `if: ${{ !inputs.dry_run }}`; otherwise unchanged (Phase 3 owns the feed step). |
| `.github/workflows/feed-tools.yml:5-21` | Path filter (push and pull_request): `tools/gen-feed.js`, `tools/gen-release-manifest.js`, `tools/minisign.js`, `tools/feed-contract/**`, the workflow | 🔶 Adds `tools/release-facts.js`, `tools/backfill-sign.js`, `.github/workflows/release.yml`, `.github/workflows/backfill-sign.yml`. |
| `.github/workflows/feed-tools.yml:40-76` | Node 24 (`:40`), `npm ci --prefix tools/feed-contract` (`:42`), cached `cargo install rsign2 --version 0.6.7 --locked` (`:44-56`), `node --test` with TAP check `n < 21` on the `# tests` line (`:63-72`), `rsign verify (KAT)` (`:75-76`), `Checkout unchanged` (`:78-80`). Measured at `1dec384c`: the TAP plan line is `1..24` (24 top-level tests) but the summary is `# tests 350` (it counts every subtest), so the current check cannot see a missing file | 🔶 TAP check replaced by a top-level count and name check (FR-073); everything else unchanged. rsign2 is already installed on all three legs, so the interop test (FR-008) runs there. |
| `tools/feed-contract/contract.js:611-635` | Exports: `CODES`, `PLATFORM_KEYS`, `validateRelease`, `checkReleaseManifest` (`:489`), `checkInstallValues`, `formatTrustedComment` (`:287`), `parseTrustedComment` (`:265`), `checkBinding` (`:298`), `parseMinisigText` (`:329`), `toLfMinisig` (`:353`), `assetNameFor` (`:601-605`), `perTagUrl` (`:607-609`), `compareSemVer` | ✅ Reused by every new tool. `checkReleaseManifest` is the last check before any `release.json` is written. |
| `tools/feed-contract/listing.js:384-394` | Exports `listingFromTarGz` (`:277-287`), `listingFromDirectory` (`:290`, throws `E_LISTING_NO_POSIX_MODES` on Windows), `digest` (`:328`), `toText`, `checkLinuxLayout` (`:359`) | ✅ Reused: the `payload` and its listing come from the tarball's own headers. |
| `tools/feed-contract/release.schema.json` | C2: closed objects; `assets` has exactly three keys, `windows-x86_64`, `linux-x86_64` and one `darwin-*` key; `min_os` required on `darwin-*` and `linux-*`; `payload {files, sha256, list}` | ✅ The output contract of `gen-release-manifest.js`. |
| `tools/feed-contract/CONTRACT.md` §4-§6, §8 | C2 cross-field rules (asset names `release.yml:524-526`, per-tag URL), C3 binding, the `.minisig` text form (LF, final LF), the KAT; fixture signatures are structural only (§8) | 🔶 Gains a section listing this phase's tools and error codes (FR-074). |
| `tools/feed-contract/fixtures/kat/` | `KatPlugin-1.2.3-Linux-x64.tar.gz` (452 bytes), its `.minisig` (trusted comment `krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz`), `kat.pub` (key id `440FA75168B852F7`), `.listing` (10 lines, digest `db287e49…7ad6`) | ✅ The known answer for `tools/minisign.js` (FR-006) and the Linux asset of the manifest fixture set (FR-036). |
| `tools/feed-contract/tests/helpers/write-guard.js:14`, `:235` | Every test's first `require`; patches `node:fs` writes to `os.tmpdir()` and turns `child_process` into an allowlist of `git` and `tar` only (`command !== 'git' && command !== 'tar'`, `:235`; a spawn without `cwd` is refused, `:233-234`) | 🔶 Allowlist gains `rsign` in four fixed argument shapes (FR-071). |
| `tools/feed-contract/tests/hygiene.test.js:34-46`, `:57`, `:65-69`, `:104-133`, `:218-240` | Offline source scan over `tools/gen-feed.js` and `tools/feed-contract/**/*.js`; `FETCH_RE` (`:57`) flags any `\bfetch\s*(` in a scanned file, global or not; `CHILD_EXEMPT` set; the write-guard-first rule; "no product file names a shipped plugin" | 🔶 Scan and roster lists extended to the four new tools; `backfill-sign.js` joins `CHILD_EXEMPT` (FR-072). No new tool names anything `fetch` (FR-051). |
| `tools/feed-contract/tests/hygiene.test.js:289-298`, `:301-316`, `:359-394` | `CONTRACT_HEADINGS` (eight fixed `## ` headings, each must exist; `markdownSections` rejects only a duplicate heading, so a ninth heading is allowed); `CONTRACT.md documents every code and field` reads the code table **only** from `## 7.` and fails any `CODES` key without a row there whose cells include its class, and any row naming a code `CODES` lacks | 🔶 Unchanged in logic. Every new code gets its row in the §7 table (FR-074). |
| `tools/gen-feed.js:418-447`, `:760-772` | `selectReleases(name, releases)`: the Q9 rule (drafts dropped, stable = highest non-prerelease); not exported today. The module exports `main` (`:763`) and runs `main(process.argv.slice(2), process)` only when it is the entry point (`:760`): the in-process CLI pattern FR-076 copies | 🔶 Added to `module.exports` (one line) so the backfill picks the same release gen-feed would; the rule itself stays in gen-feed.js (roadmap line 297: "applied here and only here"). |
| `tools/gen-feed.js:336-340` | `extractAppId(setupIssText)` → `{<GUID>}` from the first `AppId={{…}` line | ✅ Reused for `install.windows_uninstall_key`. |
| `plugins/<p>/version.json` (e.g. `plugins/membrum/version.json`: `"name": "Membrum"`, `"version": "0.12.2"`, `"preset_subdir": "Kits"`); no `version.json` carries `identifier` today (grep) | Name, version, `preset_subdir`, optional `identifier` | ✅ Read only, for the C2 `install` block. |
| `plugins/<p>/installers/windows/setup.iss` (e.g. `plugins/membrum/installers/windows/setup.iss:18` `AppId={{B7E3C9D1-…}`) | The Inno AppId | ✅ Read only. |
| `.github/workflows/ci.yml:604` | macOS configure: `cmake -S . -B build -G Xcode … -DSMTG_CODE_SIGN_IDENTITY_MAC="-" …`; no `CMAKE_OSX_ARCHITECTURES` or `CMAKE_OSX_DEPLOYMENT_TARGET` in any `CMakeLists.txt`, `*.cmake`, workflow or JSON file (grep, this session) | ✅ Unchanged. It is why the arch set and the minimum macOS are measured, not assumed. |
| `.github/workflows/ci.yml:1062` | `build-linux` `runs-on: ubuntu-latest` | ✅ Unchanged; the glibc floor is measured on the binary this produces. |
| `specs/krate-manager/probes/results.md:10` | `tauri signer` key files are base64 of standard minisign files (`rsign encrypted secret key`); rsign and Tauri signatures verify both ways | The secret contract (FR-052, D4). |
| `plugins/shared/src/update/update_checker.cpp:222-262` | The in-plugin parser | ✅ Not touched; Phase 2 writes no feed. |

---

## External facts used (verified this session)

| Fact | Source |
|---|---|
| minisign signature: line 2 = base64(`ED` \|\| key id (8) \|\| ed25519(Blake2b-512(file))), line 4 = base64(ed25519(signature \|\| trusted comment)); public key = base64(`Ed` \|\| key id (8) \|\| pk (32)); new implementations must use `ED` | https://jedisct1.github.io/minisign/ ("Signature format", "Public key format"), minisign 0.12 (as cited by Phase 1) |
| Node v24.7.0 on this box: `crypto.getHashes()` includes `blake2b512`; a public key built as SPKI DER (`302a300506032b6570032100` \|\| pk) and `crypto.verify(null, blake2b512(file), key, sig)` plus the global check over `sig \|\| trusted comment` **accept the Phase 1 KAT**; a one-byte change to the file is rejected; the key id as rsign prints it is the 8 key-id bytes reversed, upper-case hex (`440FA75168B852F7`) | measured 2026-10-30, throwaway script in the scratchpad (not committed) |
| Ed25519 in Node: `generateKeyPair('ed25519')`, `sign(null, data, privateKey)`, `verify(null, data, publicKey, signature)`; DER input needs an explicit `type` (`'spki'`) | https://nodejs.org/docs/latest-v24.x/api/crypto.html ("Examples", "PEM and DER", "Importing keys") |
| rsign2 0.6.7 CLI: `sign [-s <sec>] [-W] [-t <trusted comment>] [-x <sig file>] <FILE>`; `generate [-p] [-s] [-W] [--unencrypted]`; `verify [-p <pub>] [-x <sig file>] [-q] <FILE>` | `rsign --version` = `rsign2 0.6.7`; `rsign sign --help`, `rsign generate --help`, `rsign verify --help` on this box |
| rsign2 0.6.7 `sign`: tries the key as unencrypted, else decrypts it with the **empty** password when `-W` is given, else prompts (`src/bin/rsign/main.rs:103-113`); the prompt goes through `rpassword`, which reads `/dev/tty` on Unix and `CONIN$` on Windows (rpassword 7.5.4 `src/unix.rs:11`, `src/windows.rs:15`) — so a key with a non-empty password cannot be used by rsign in CI | crate sources in `~/.cargo/registry/src/*/rsign2-0.6.7`, `minisign-0.9.1/src/secret_key.rs:62-64,255-275`, `rpassword-7.5.4` |
| `rsign generate -W` writes `untrusted comment: rsign encrypted secret key` with KDF `Sc` (scrypt, empty password); `rsign sign -W -t "<C3 comment>"` with stdin from `/dev/null` signs, and `rsign verify` prints `Signature and comment signature verified` | measured 2026-10-30, throwaway key in the scratchpad, deleted |
| minisign secret-key file: base64 of `sig_alg` (2, `Ed`) \|\| `kdf_alg` (2, `Sc` = scrypt; zero bytes = unencrypted) \|\| `cksum_alg` (2, `B2`) \|\| `kdf_salt` (32) \|\| `kdf_opslimit` (8, LE) \|\| `kdf_memlimit` (8, LE) \|\| `keynum_sk` (104 = key id 8 \|\| Ed25519 secret key 64 \|\| checksum 32); the stored `keynum_sk` is XORed with an scrypt-derived keystream, and the checksum is Blake2b-256 over `sig_alg` \|\| key id \|\| secret key | https://jedisct1.github.io/minisign/ ("Secret key format"); `minisign-0.9.1/src/secret_key.rs`. Recorded from memory of those sources in the 2026-10-30 clarification, not re-measured: the implementer confirms it against both, and the encrypted-key KAT (FR-064, SC-027) is the check |
| `tauri signer generate`: `-p/--password`, `-w/--write-keys`, `-f/--force`, `--ci`; `tauri signer sign`: `-k`/`TAURI_SIGNING_PRIVATE_KEY`, `-f`/`TAURI_SIGNING_PRIVATE_KEY_PATH`, `-p`/`TAURI_SIGNING_PRIVATE_KEY_PASSWORD`, `--app-version` (no free trusted comment) | https://v2.tauri.app/reference/cli/ (Tauri v2 CLI reference) |
| `softprops/action-gh-release` v2 (final v2 is v2.6.2): `make_latest` "Can be `true`, `false`, or `legacy`"; `files` newline-delimited globs; `fail_on_unmatched_files` default false; `overwrite_files` default `'true'`; `runs.using: node20` | https://raw.githubusercontent.com/softprops/action-gh-release/v2.6.2/action.yml |
| `actions/attest-build-provenance`: "As of version 4 … simply a wrapper on top of" `actions/attest`, which new implementations should use; it wraps `actions/attest@… # v4.2.2`; `subject-path` "May contain a glob pattern or list of paths (total subject count cannot exceed 1024)" | https://github.com/actions/attest-build-provenance (README), `action.yml` on `main` |
| Build provenance for binaries needs `permissions: id-token: write, contents: read, attestations: write`; example `uses: actions/attest@v4`; humans verify with `gh attestation verify <file> -R <owner>/<repo>` | https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations |
| GitHub immutable releases (GA 2025-10-28): once a release is published as immutable its assets cannot be added, modified or deleted; existing releases stay mutable | https://github.blog/changelog/2025-10-28-immutable-releases-are-now-generally-available/ |
| `gh release list`: `-L/--limit` **default 30**; JSON fields `createdAt, isDraft, isImmutable, isLatest, isPrerelease, name, publishedAt, tagName` | https://cli.github.com/manual/gh_release_list |
| `gh release view [<tag>] --json` fields include `assets`, `isImmutable`, `isDraft`, `isPrerelease`, `publishedAt`, `tagName`; each exported asset has `name`, `size`, `url` (browser download URL), `digest` (nullable) | https://cli.github.com/manual/gh_release_view; `pkg/cmd/release/shared/fetch.go` (`ReleaseAsset`, `ExportData`), cli/cli `trunk`, fetched 2026-10-30 |
| `gh release download [<tag>] -p/--pattern … -D/--dir …`; `gh release upload <tag> <files>...`, `--clobber` "Delete and re-upload existing assets of the same name" | https://cli.github.com/manual/gh_release_download, https://cli.github.com/manual/gh_release_upload |
| `lipo -archs`: "Display only the architecture names present in a single input file", one word per architecture | https://keith.github.io/xcode-man-pages/lipo.1.html |
| `pkgutil --expand-full pkg-path dir-path`: expands the flat package "including expanding Payload contents of each package" | https://keith.github.io/xcode-man-pages/pkgutil.1.html |
| Mach-O: `MH_MAGIC 0xfeedface` ("the mach magic number", 32-bit), `MH_CIGAM 0xcefaedfe`, `MH_MAGIC_64 0xfeedfacf`, `MH_CIGAM_64 0xcffaedfe` (the `CIGAM` values are `NXSwapInt` of the magic, i.e. the opposite byte order), `LC_VERSION_MIN_MACOSX 0x24`, `LC_BUILD_VERSION 0x32`, `PLATFORM_MACOS 1`; `build_version_command {cmd, cmdsize, platform, minos, sdk, ntools}`, `version_min_command {cmd, cmdsize, version, sdk}`, versions "encoded in nibbles xxxx.yy.zz" | https://raw.githubusercontent.com/apple-oss-distributions/cctools/main/include/mach-o/loader.h |
| Fat files: `FAT_MAGIC 0xcafebabe`, `FAT_MAGIC_64 0xcafebabf`, `fat_arch {cputype, cpusubtype, offset, size, align}` (64-bit: `uint64_t` offset and size, plus `reserved`), "always written and read … in big-endian order" | https://raw.githubusercontent.com/apple-oss-distributions/cctools/main/include/mach-o/fat.h |
| `CPU_ARCH_ABI64 0x01000000`, `CPU_TYPE_X86_64 = 7 \| ABI64`, `CPU_TYPE_ARM64 = 12 \| ABI64` | https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/mach/machine.h |
| Workflow `permissions:` (top-level and `jobs.<job_id>.permissions`) take literal access levels; neither key has a row in the "Context availability" table, so no `${{ inputs.* }}` expression can make a job's permissions conditional, while `jobs.<job_id>.if` accepts `github, needs, vars, inputs` | https://docs.github.com/en/actions/reference/workflows-and-actions/contexts ("Context availability", fetched 2026-10-30); https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax ("permissions") |
| `objdump -T` / `--dynamic-syms`: "Print the dynamic symbol table entries of the file" | https://sourceware.org/binutils/docs/binutils/objdump.html |
| `node --test "<glob>"` runs files in parallel child processes by default; a directory argument runs nothing on Node 24 | Phase 1 spec "External facts" (measured 2026-10-29) |

---

## Decisions taken in this spec (open to challenge in the clarify scan)

The roadmap explicitly leaves one choice to this spec: how the backfill derives the macOS and Linux facts from
downloaded assets ("the spec chooses how", roadmap lines 384-386). D1 answers it. D2-D13 close points the roadmap
names but does not pin down; each is binding unless the grill overturns it.

- **D1, facts come from the asset itself, by one code path.** Both the release jobs and the backfill measure the
  facts from the finished asset, never from the build tree, so a backfilled release is measured exactly like a new
  one. macOS: `pkgutil --expand-full <pkg> <dir>`, then `tools/release-facts.js darwin`, which walks the expanded
  tree (never following a link), takes every regular file whose first four bytes are a Mach-O magic of either width or
  byte order, or a fat magic (FR-012), and reads its architecture set (fat header or `cputype`) and minimum macOS (`LC_BUILD_VERSION` with
  `platform == PLATFORM_MACOS`, else `LC_VERSION_MIN_MACOSX`) from the load commands. The roadmap's `lipo -archs` runs
  on every Mach-O the tool lists and must report the same set, so the Node reader and Apple's tool cross-check each
  other. Reading load commands in Node rather than parsing `vtool`/`otool` text keeps the reader testable on Windows
  with synthetic Mach-O bytes (the `vtool` man page documents no output format). Linux: `tar -xzf` into a temp dir,
  `objdump -T <Name>.vst3/Contents/x86_64-linux/<Name>.so` (the C4 bundle binary, `CONTRACT.md` §6), and the
  highest `GLIBC_<n>.<n>[.<n>]` version named in that output is the floor.
- **D2, the darwin platform key from the architecture set.** Every Mach-O in the pkg must report the same set: `{arm64,
  x86_64}` → `darwin-universal`, `{arm64}` → `darwin-aarch64`, `{x86_64}` → `darwin-x86_64`. Mach-Os that disagree are
  `E_FACTS_ARCH_MISMATCH`, any other architecture (`arm64e`, a thin 32-bit file `MH_MAGIC` / `MH_CIGAM`, a big-endian
  `MH_CIGAM_64` file, a 32-bit or unknown fat slice) is `E_FACTS_ARCH`, and a pkg with no Mach-O is `E_FACTS_NO_MACHO`.
  A 32-bit file is never silently skipped as "not a Mach-O". A release that fails any of them is not signed (release job) or is reported and skipped
  (backfill). Taking the intersection instead would advertise a platform on which part of the pkg cannot load.
- **D3, `min_os` text.** macOS: the highest `minos` over every Mach-O and every slice, written `X.Y` when the patch
  nibble is 0 and `X.Y.Z` otherwise. Linux: the glibc version as named (`2.34`, `2.2.5`). Both match the C2 pattern
  `^[0-9]+(\.[0-9]+){1,3}$`. Windows records none (C2 makes it optional, Phase 1 FR-016).
- **D4, the signing secrets (superseded 2026-10-30, Q3).** Two repository secrets: `TAURI_SIGNING_PRIVATE_KEY` (the name
  the Phase 11 Tauri build reads), holding the key file content that `tauri signer generate` writes (base64 of a standard
  minisign secret-key file, probes line 10), and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`, holding that key's **non-empty**
  password. The key is password-protected; an empty password is not accepted. Because rsign2 0.6.7 reads a password only
  from the terminal (External facts), `tools/minisign.js` gains secret-key decryption (FR-009a, FR-009b): a Node step
  decrypts the key with the password secret and writes the unencrypted key only to a mode-0600 file under
  `$RUNNER_TEMP`; `rsign sign -W` then signs with that file, as the roadmap says; and every signature is verified
  against the committed public key by the Node verifier before upload (FR-024, FR-045, FR-055). Phase 11 passes the same
  password to Tauri through `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`. The decrypted key lives for the steps between the
  decrypt step and the `if: always()` cleanup step of an ephemeral runner (FR-045, FR-050).
- **D5, the public key is committed.** `tools/feed-contract/keys/krate-signing.pub` holds the matching public key as
  the two-line minisign text (`untrusted comment: minisign public key: <KEYID>` / base64 line, what `rsign verify -p`
  reads; the `.pub` that `tauri signer generate` writes is base64 of this text, probes line 10). The user commits it
  with the secret. Every signature CI makes is verified against this file before anything is uploaded, so a wrong or
  rotated secret can never publish signatures the manager would reject. Phases 7 and 11 compile in the same key.
- **D6, every real release is signed; a real run that cannot produce the manager files fails (Q1, fail closed).** The
  roadmap's `release` job component is unconditional (roadmap lines 368-373: `SHA256SUMS`, one signature per asset made
  with the CI secret, `release.json` uploaded), and Q10 calls the backfill one-time (roadmap lines 34, 378). So there is
  no unsigned branch. "Signing configured" means both secrets (`TAURI_SIGNING_PRIVATE_KEY` and
  `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`) are non-empty **and** `tools/feed-contract/keys/krate-signing.pub` exists and
  parses as a minisign public key (`minisign.parsePublicKey`). The decision is one Node function,
  `gen-release-manifest.signingConfig` (FR-029), which the workflow step calls, so every branch is tested (SC-022): dry
  run → `throwaway` (whatever is configured, roadmap lines 374-375); real run, configured → `on`; real run with the key
  secret, the password secret or the public key missing or unparsable, alone or together → the job fails with
  `E_SIGNING_CONFIG` before `Create Release`, naming each missing part and `tools/feed-contract/keys/README.md`. The same
  holds for the facts: a facts failure (`E_FACTS_ARCH_MISMATCH`, `E_FACTS_NO_MINOS`, `E_FACTS_NO_GLIBC`, any other
  `ContractError` of FR-041 / FR-042) fails the real release, and no input releases without the manager files
  (FR-047a). Consequence, ruled by the user 2026-10-30: no plugin release can be cut between merging this phase and doing
  the FR-061 key step, so the phase's merge instructions (FR-062) tell the user to create both secrets and commit
  `krate-signing.pub` **before merging**. A deleted or emptied secret stops releases instead of quietly shipping a
  release the manager cannot manage (Phase 3's "not available through the manager yet", roadmap line 438; ROSTER, line
  33). The in-editor update checker and the existing installers are unaffected; a dry run needs no key.
- **D7, `SHA256SUMS`** covers the three installer assets only, one line `<64 lower-case hex>  <asset name>` (two
  spaces, the GNU `sha256sum` text form), lines sorted bytewise by name, LF, final LF, so `sha256sum -c SHA256SUMS`
  and `shasum -a 256 -c SHA256SUMS` work on a user's download folder. It is not signed: C3 has no platform key for it,
  and every asset it lists already carries its own bound signature.
- **D8, one upload call per release, `release.json` last in the backfill.** The `release` job attaches all seven files
  in the single `Create Release` step (immutable releases accept no later upload, External facts). The backfill runs
  `gh release upload <tag> SHA256SUMS <3 .minisig>` and then a separate `gh release upload <tag> release.json`, so a
  `release.json` on a release always means the full set is there, and a set without it is visibly partial.
- **D9, `pub_date` in `release.json`.** Release job: the UTC time at which the manifest is generated
  (`YYYY-MM-DDTHH:MM:SSZ`), because the release does not exist yet. Backfill: the release's `publishedAt`. gen-feed
  takes the feed's `pub_date` from the release list, not from `release.json` (Phase 1 D6), so the two never conflict.
- **D10, attestation with `actions/attest-build-provenance@v4`** (Q7), as the roadmap names it, over the three
  installer assets only (not `release.json`, `SHA256SUMS` or the `.minisig`s), on real runs only. Its README says v4 is
  a wrapper over `actions/attest`; the roadmap's action is kept, so there is no deviation to record. A dry run skips it,
  because an attestation is written to GitHub's attestation store (and, for a public repository, a public transparency
  log, feed_and_ci.md line 175) for artifacts that were never released.
- **D11, the backfill dry run signs with the real secret and its password secret** (Q4). It writes every `.minisig`,
  `SHA256SUMS` and `release.json`, verifies them against the committed public key, uploads them only as a workflow
  artifact and calls no `gh release upload`. That proves the real key (D4, D5) before the user's live run.
  `release.yml`'s dry run keeps the throwaway key the roadmap names (roadmap lines 374-375). This adds a precondition the
  roadmap's backfill dry run (roadmap lines 409-410) does not state: the user creates both secrets and commits
  `krate-signing.pub` before dispatching it (SC-019).
- **D12, install facts at the release's own commit (Q5: the tag is pinned).** The `release` job reads `version.json` and
  `setup.iss` from its checkout, which is the **dispatch ref** (`actions/checkout@v6` at `release.yml:424` has no `ref:`).
  `Create Release` now passes `target_commitish: ${{ github.sha }}` (FR-047), the commit of that dispatch ref, so the tag
  is always the commit that was built, checked out and described; a branch dispatch tags a branch commit. The backfill
  reads them with `git show <tag>:plugins/<dir>/version.json` and `git show <tag>:plugins/<dir>/installers/windows/setup.iss`,
  so a `version.json` that has moved on since the release (new version, new `preset_subdir`) cannot leak into an old
  release's manifest. The tag's `version.json` `version` must equal the tag's version (`E_VERSION_MISMATCH`).
- **D13, where the Phase 2 tests live.** In `tools/feed-contract/tests/`, so the existing quoted glob, the write guard,
  the hygiene scan and the `feed-tools.yml` lane cover them with no new runner. The tests that spawn the real `rsign`
  (SC-003, SC-025, SC-027) first run `rsign --version` and require exactly `rsign2 0.6.7`; when `rsign` is absent or reports
  another version they skip with a printed reason if `CI` is unset, and fail if `CI` is set (every `feed-tools.yml` leg
  installs rsign2 0.6.7). A skipped local run does not close the local row.

---

## Functional Requirements

### A. `tools/minisign.js` (roadmap lines 358-367)

- **FR-001** `tools/minisign.js` is a CommonJS module over `node:crypto`, `node:fs` and `tools/feed-contract/contract.js`
  only (no new npm dependency, no network module, no `child_process`). It exports `main`, `generateKeyPair`,
  `formatPublicKey`, `parsePublicKey`, `signFile`, `signBuffer`, `verifyFile`, `verifyBuffer`, `decryptSecretKey`, and
  the test-only `_internal: {blake2b, rawScryptParams}`, which only the SC-027 anchors use and no tool or production
  caller calls (plan PD-18, plan SI-7); `main(argv, io)` is the FR-007 CLI (FR-076).
- **FR-002** `generateKeyPair()` makes an Ed25519 pair with `crypto.generateKeyPairSync('ed25519')` and an 8-byte key id
  from `crypto.randomBytes(8)`, and returns `{keyId, publicKey, privateKey}` (KeyObjects in memory; nothing is written).
  `formatPublicKey({keyId, publicKey})` returns `untrusted comment: minisign public key: <KEYID>\n<base64('Ed' || keyId
  || raw pk)>\n`, `<KEYID>` being the key-id bytes reversed in upper-case hex, as rsign prints it (External facts).
- **FR-003** `parsePublicKey(text)` accepts LF or CRLF, requires a first line starting `untrusted comment: ` and a second
  line of canonical base64 decoding to 42 bytes beginning `Ed` (`0x45 0x64`), and returns `{keyId, publicKey}` with the
  key imported as SPKI DER. Anything else is `E_MINISIG_PUBKEY`.
- **FR-004** `signBuffer(data, {keyId, privateKey}, {trustedComment, untrustedComment})` and `signFile(path, …)` return the
  four-line `.minisig` text in the Phase 1 D7 form (LF, final LF): line 2 = base64(`ED` || keyId ||
  ed25519(Blake2b-512(data))), line 4 = base64(ed25519(signature || UTF-8 trusted comment)). `signFile` hashes the file as
  a stream (`crypto.createHash('blake2b512')`), so asset size never bounds memory. The trusted comment must contain no CR
  or LF. The output passes `contract.parseMinisigText`.
- **FR-005** `verifyBuffer(data, minisigText, publicKeyText)` / `verifyFile(path, …)` run, in this order, and throw the
  first failure as a `ContractError`: `parseMinisigText` (`E_SIGNATURE_TEXT`: shape, legacy `Ed`, CR in a value);
  `parsePublicKey` (`E_MINISIG_PUBKEY`); key id of the signature equals the public key's (`E_MINISIG_KEY_ID`); the
  Ed25519 signature over Blake2b-512 of the data (`E_MINISIG_SIGNATURE`); the global signature over
  signature || trusted comment (`E_MINISIG_GLOBAL`). On success they return `{trustedComment, keyId}`. They never check
  the C3 binding themselves; callers run `contract.checkBinding` on the returned comment.
- **FR-006** `minisign.js` verifies the Phase 1 KAT (`tools/feed-contract/fixtures/kat/`) with `kat.pub`, and rejects it
  after a one-byte change to the archive, to the decoded signature on line 2, to the key id, or to the trusted comment
  (roadmap lines 395-396).
- **FR-007** CLI: `node tools/minisign.js verify -p <public key file> -x <.minisig file> <file>` prints
  `verified: <trusted comment>` and exits 0, or prints `minisign: <CODE>: <message>` on stderr and exits 1; bad arguments
  print the usage line and exit 2. Besides `decrypt-key` (FR-009b) there is no CLI for keygen or signing: CI signs with rsign2 (roadmap lines 352-357),
  and tests use the module API. Shebang and usage line per FR-076.
- **FR-008** **Interop, both ways (roadmap lines 398-400).** With rsign2 0.6.7: (a) a `minisign.js` key pair written to a
  temp dir as `formatPublicKey` text, and a `signFile` signature carrying a C3 trusted comment, pass `rsign verify -p
  <pub> -x <sig> <file>`, whose output names the same trusted comment; (b) a fresh `rsign generate -W` key and an
  `rsign sign -W -t "<C3 comment>"` signature pass `verifyFile`, which returns that comment. (b) also proves
  `parsePublicKey` reads rsign's public-key file.
- **FR-009** (reverses the roadmap's "unencrypted keys only", line 365, by user ruling Q3) `minisign.js` reads a minisign
  secret-key file only to decrypt it (FR-009a) and writes a secret-key file only through the `decrypt-key` CLI
  (FR-009b). It never generates, encrypts or writes any other secret-key file; the keys of `generateKeyPair` exist only
  as in-memory KeyObjects in the process that made them.
- **FR-009a** `decryptSecretKey(keyText, password)` accepts the secret-key file text (LF or CRLF: an `untrusted comment: `
  line and the base64 line) or the single base64 line that a `TAURI_SIGNING_PRIVATE_KEY` secret holds (the file text,
  base64-encoded once more), plus the password as a string. It parses `sig_alg` `Ed`, `kdf_alg` `Sc`, `cksum_alg` `B2`,
  the salt, `kdf_opslimit` and `kdf_memlimit`; derives the 104-byte keystream with `crypto.scrypt`, mapping
  opslimit/memlimit to the scrypt cost the way minisign does and passing `maxmem = 2 * 128 * N * r`; before scrypt runs,
  `memlimit > 1 GiB`, `log2(N) > 20` (minisign's own limits) and a cost `128 * N * r * p > 2 GiB` are each
  `E_MINISIG_SECRET_KEY` (plan PD-2); XORs it onto `keynum_sk`; checks the Blake2b-256 checksum; and returns
  `{keyId, privateKey}` (a KeyObject). A file whose `kdf_alg` is two zero bytes is unencrypted and is returned without
  using the password. A checksum mismatch (wrong password or damaged key) is `E_MINISIG_PASSWORD`; any other malformed
  input (wrong decoded length, unknown algorithm id, a secret key whose public half does not match its key id) is
  `E_MINISIG_SECRET_KEY`; "public half" means the 32 bytes `sk[32..64]` stored in the secret key, which must equal the
  public key derived from the seed `sk[0..32]`, because minisign's key id is random and cannot be derived from the key
  (plan PD-3). No message contains the password or any key byte.
- **FR-009b** CLI `node tools/minisign.js decrypt-key --out <file>`: reads the key from the environment variable
  `KRATE_SIGNING_KEY` and the password from `KRATE_SIGNING_KEY_PASSWORD` (from `io.env`; neither is ever an argument,
  printed, logged or put into an error message; either one absent or empty is exit 1, `E_INPUT`). `--out` must resolve
  under `io.env.RUNNER_TEMP` (the OS temp dir when it is unset); any other path is exit 1, `E_INPUT`, and nothing is
  written. Containment compares **canonical** paths on both sides (plan PD-19, plan SI-8): a raw `--out` with a `..`
  segment is refused; the parent directory of `--out` must exist; `realpathSync.native` of that parent joined with the
  basename must equal or sit under `realpathSync.native(RUNNER_TEMP || os.tmpdir())`, case-folded on win32; an existing
  entry at the canonical out path (`lstat`, any type) is refused; each refusal is exit 1, `E_INPUT`, and the temp file
  and the rename use the canonical path. On success it writes the **unencrypted** form of the key (the same file layout with `kdf_alg` two zero bytes,
  the form that `rsign sign` accepts without a password, External facts; concretely the layout minisign's
  `generate_unencrypted_keypair` writes: `kdf_alg`, salt, opslimit and memlimit all zero, the plaintext checksum, and
  the untrusted comment `krate decrypted secret key (runner temp only)`, plan PD-16) with mode 0600, through a temp file in the same
  directory and a rename, prints `decrypted: key id <KEYID>` and exits 0. On any failure (including `E_MINISIG_PASSWORD`)
  no `--out` file and no temp file remains; the error line is `minisign: <CODE>: <message>` with exit 1.
### B. `tools/release-facts.js` (roadmap lines 344-348, 384-386; D1-D3)

- **FR-010** `tools/release-facts.js` exports `main(argv, io)`, `machoFacts(buffer)`, `darwinFactsFromTree(root)`,
  `glibcFloor(objdumpText)`, `platformKeyForArchs(archs)` and `linuxFacts({tarball, extracted, objdumpText})`, and
  `main` has three CLI modes:
  `node tools/release-facts.js darwin --pkg <file> --expanded <dir> --out <json>`,
  `node tools/release-facts.js linux --tarball <file> --extracted <dir> --objdump <txt> --out <json>` and
  `node tools/release-facts.js linux-precheck --tarball <file>` (FR-013; plan PD-26, plan SI-11). It uses no network
  module and no `child_process`: the workflow runs `pkgutil`, `tar`, `lipo` and `objdump`, and the tool reads their
  results from disk.
- **FR-011** `machoFacts(buffer)` reads: a fat file (`0xcafebabe` / `0xcafebabf` big-endian) and each slice it lists,
  or a thin 64-bit little-endian file (`0xfeedfacf` read little-endian); a thin 32-bit file (`0xfeedface` or
  `0xcefaedfe` read little-endian) or a big-endian 64-bit file (`0xcffaedfe`) is `E_FACTS_ARCH`; per slice the `cputype` (`0x01000007` → `x86_64`,
  `0x0100000c` with subtype `0` or `1` (`ALL`/`V8`) → `arm64`; any other type or subtype is `E_FACTS_ARCH`), then the
  load commands within `sizeofcmds`: the first `LC_BUILD_VERSION` (`0x32`) with `platform == 1`, else the first
  `LC_VERSION_MIN_MACOSX` (`0x24`), its `minos` / `version` decoded from the `xxxx.yy.zz` nibbles. A slice with neither
  is `E_FACTS_NO_MINOS`; a slice or command that runs past the buffer, a zero `nfat_arch`, or a `cmdsize` below 8 is
  `E_FACTS_MACHO_CORRUPT`. It returns `{archs: [sorted], minOs, slices: [{arch, minOs}]}`.
- **FR-012** `darwinFactsFromTree(root)` walks `root` with `lstat` (a link is skipped, never followed), reads the first
  4 bytes of each regular file, runs `machoFacts` on every file whose first four bytes, read little-endian, are
  `0xfeedfacf`, `0xfeedface`, `0xcefaedfe` or `0xcffaedfe`, or, read big-endian, `0xcafebabe` or `0xcafebabf` (so a
  32-bit or big-endian Mach-O raises `E_FACTS_ARCH` instead of being skipped), applies D2 and D3, and returns
  `{platform_key, min_os, archs, macho: [{path, archs, min_os}]}` with `path` relative to `root`, POSIX separators,
  sorted bytewise. Zero Mach-Os is `E_FACTS_NO_MACHO`.
- **FR-013** The `darwin` CLI writes `{asset, size, sha256, platform_key, min_os, archs, macho}` (`asset` the pkg's file
  name, `size` and `sha256` of the pkg file itself) to `--out` (temp file + rename), and prints one line per Mach-O,
  `<archs joined by a space>\t<path relative to --expanded>`, for the workflow's `lipo -archs` cross-check (FR-041).
  The `linux-precheck` CLI runs `listingFromTarGz` on `--tarball`, the single-bundle rule and `checkLinuxLayout` (the
  archive half of FR-015), writes nothing, and exits 0 with no output or exits 1 with `release-facts: <CODE>: <message>`;
  the workflow runs it before `tar -xzf` (FR-042), so the listing and the layout are checked before any extraction
  (plan PD-26, plan SI-11).
- **FR-014** `glibcFloor(text)` returns the highest version among every `GLIBC_<n>(.<n>){1,2}` token in `objdump -T`
  output, compared numerically part by part; `GLIBC_PRIVATE` and other non-numeric names are ignored; no token is
  `E_FACTS_NO_GLIBC`.
- **FR-015** `linuxFacts` (which the `linux` CLI and the backfill's `facts` both call), in this order: computes
  `listingFromTarGz` of `--tarball` and checks `checkLinuxLayout` with the bundle named in the archive (so a layout
  failure such as a missing `README.txt` is `E_LAYOUT` before the extracted tree is read, on any OS); then computes
  `listingFromDirectory(--extracted)` and requires it to equal the tarball's listing line
  for line (`E_FACTS_LISTING_MISMATCH`, naming the first differing line); takes the glibc floor from `--objdump`; and
  writes `{asset, size, sha256, min_os, payload: {files, sha256}}` to `--out`. This is the roadmap's "payload digest equals
  the C4 listing digest of the extracted tarball" (line 401) on the real asset. The extracted-tree half runs on POSIX
  only (Phase 1 D4). `linuxFacts` takes an optional second argument `{listDirectory}` (default
  `listing.listingFromDirectory`), the function that lists `--extracted`, so a win32 test can inject a lister (plan
  PD-4, plan SI-1).
- **FR-016** Every error is a `ContractError` with a code from FR-074, printed as `release-facts: <CODE>: <message>` on
  stderr with exit 1; usage errors exit 2 (FR-076).

### C. `tools/gen-release-manifest.js` (roadmap lines 349-350)

- **FR-020** Manifest mode:
  `node tools/gen-release-manifest.js --version-json <file> --setup-iss <file> --assets <dir> --darwin-facts <json>
  --linux-facts <json> --repo <owner/name> --pub-date <YYYY-MM-DDTHH:MM:SSZ> --public-key <file> --out <release.json>
  --sums-out <SHA256SUMS>`. `name` and `version` come from `--version-json`; the tag is
  `<name lower-cased>/v<version>` (`release.yml:76`). The module exports `main(argv, io)` (FR-076), `buildManifest`
  (the in-process manifest logic the backfill calls, FR-054), `trustedComments` (FR-027) and `signingConfig` (FR-029).
- **FR-021** `--assets` must hold exactly the three asset names `contract.assetNameFor` gives for `windows-x86_64`,
  `linux-x86_64` and the darwin key from `--darwin-facts`, and each one's `<asset>.minisig`. A missing file is
  `E_ASSET_MISSING`, naming it. Other files in the directory are ignored.
- **FR-022** Per asset: `size` from `fs.stat`; `sha256` streamed; `signature` = the `.minisig` text through
  `toLfMinisig`; `format` per key (`inno-exe`, `pkg`, `tar.gz`); `url` = `contract.perTagUrl(repo, tag, name)`;
  `min_os` from the facts for `darwin-*` and `linux-x86_64`, none for Windows.
- **FR-023** Facts belong to these assets: the `asset`, `size` and `sha256` in each facts file must equal the file in
  `--assets`, else `E_FACTS_STALE`. The Linux `payload {files, sha256}` must equal the digest `listingFromTarGz`
  computes from the tarball in `--assets` (`E_LISTING_DIGEST`); `payload.list` is that listing.
- **FR-024** Every signature is verified before the manifest is written: `minisign.verifyFile(asset, sig,
  --public-key)` (FR-005 codes), then `contract.checkBinding(trustedComment, {name, version, platform: key, asset})`
  (`E_TC_BINDING`). A signature that verifies cryptographically but carries another asset's comment fails, so one asset's
  signature can never stand in for another's (roadmap line 397).
- **FR-025** `install` = `{vst3_bundle: "<name>.vst3", au_component: "<name>.component", preset_subdir?,
  windows_uninstall_key: extractAppId(setup.iss) + "_is1", macos_pkg_id}`, `preset_subdir` copied from `version.json` when
  present, `macos_pkg_id` = `version.json` `identifier` or `com.krateaudio.<name lower-cased>` (`release.yml:321`). A
  `setup.iss` without an `AppId={{…}` line is `E_INPUT`.
- **FR-026** The checks run in this fixed order, and the first failure is the reported code: FR-021 (files present) →
  FR-023 (facts belong to the assets, Linux payload) → FR-024 (each signature, then its binding; assets in bytewise
  platform-key order) → FR-025 (install block) → `contract.checkReleaseManifest(doc, {repo})`. The document (keys in the
  order `tag, name, version, pub_date, assets, install`; `assets` keys bytewise; per asset `name, url, size, sha256,
  signature, format, min_os, payload`) must pass all of them before anything is written; a failure exits 1 with its
  code. Then `SHA256SUMS` (D7) and `release.json` (`JSON.stringify(doc, null, 2) + "\n"`) are each written to a temp
  file in the output directory and renamed into place, `release.json` last. A failed run leaves neither output file new
  or changed: an output that existed before the run keeps its bytes and its mtime.
- **FR-027** Trusted-comment mode: `node tools/gen-release-manifest.js --trusted-comments --version-json <file>
  --assets <dir> --darwin-facts <json>` prints three lines `<asset name>\t<C3 comment>` (one per platform key, keys
  bytewise), each from `contract.formatTrustedComment`. It reads the assets' names only and requires no `.minisig`.
  The signing loop in CI consumes these lines, so the platform key and the comment are computed in one place.
- **FR-028** The tool contains no plugin name (ROSTER); every name comes from `--version-json`. It uses no network
  module and no `child_process`. Exit codes: 0 written, 1 abort (nothing written), 2 usage.
- **FR-029** Signing-configuration mode (D6): `signingConfig({dryRun, secretPresent, passwordPresent, publicKeyText})`
  returns `'throwaway'` when `dryRun`; `'on'` when not `dryRun`, `secretPresent` and `passwordPresent` are true and
  `publicKeyText` (the file's text, or `null` when the file is absent) passes `minisign.parsePublicKey`; otherwise it
  throws `E_SIGNING_CONFIG` naming every missing or invalid part (`secret`, `password`, `public key absent`, or the
  `E_MINISIG_PUBKEY` message) and `tools/feed-contract/keys/README.md`. Real mode also refuses with `E_SIGNING_CONFIG`
  (naming `test key`) a public key whose key id or 32-byte Ed25519 key equals that of `fixtures/kat/kat.pub` or
  `fixtures/phase2/kat-secret/kat.pub` (read relative to the tool's `__dirname`); either of those files unreadable in
  real mode is `E_SIGNING_CONFIG` too, fail closed (plan PD-23, plan SI-9). CLI: `node tools/gen-release-manifest.js
  --signing-config --dry-run <true|false> --public-key <file>` reads `secretPresent` as "the environment variable
  `KRATE_SIGNING_KEY` is non-empty" and `passwordPresent` as "`KRATE_SIGNING_KEY_PASSWORD` is non-empty" (from `io.env`;
  neither value is ever printed, logged or put on a command line), prints `signing=<throwaway|on>` and exits 0, or
  prints `gen-release-manifest: E_SIGNING_CONFIG: <message>` and exits 1.
### D. `release.yml` (roadmap lines 340-348, 368-376)

- **FR-040** A third `workflow_dispatch` input, `dry_run` (`type: boolean`, `default: false`, description "Build, sign
  with a throwaway key and upload as workflow artifacts only: no tag, no release, no Pages deploy").
- **FR-041** `macos-installer`, after `Build Installer` (`:314-337`) and before `Upload Installer`: `actions/setup-node`
  (Node 24) and `npm ci --prefix tools/feed-contract`; `pkgutil --expand-full <pkg> "$RUNNER_TEMP/pkg-x"`;
  `node tools/release-facts.js darwin --pkg <pkg> --expanded "$RUNNER_TEMP/pkg-x" --out darwin-facts.json`; for every
  printed line, `lipo -archs` on that file, its words sorted, must equal the printed set, else the step fails naming the
  file; the step prints `platform_key` and `min_os`; `actions/upload-artifact@v7` uploads `darwin-facts.json` as
  `macos-facts` (`if-no-files-found: error`). The staging and `pkgbuild` lines are unchanged.
- **FR-042** `linux-archive`, after `Create Archive` (`:397-403`): Node 24 and `npm ci` as above;
  `node tools/release-facts.js linux-precheck --tarball <tarball>` (FR-013, before any extraction; plan PD-26); `umask
  000`, so the extracted modes are the archive's modes (GNU tar run by a non-root user applies the umask; plan PD-13);
  `tar -xzf <tarball> -C "$RUNNER_TEMP/tar-x"`; `objdump -T "$RUNNER_TEMP/tar-x/<Name>.vst3/Contents/x86_64-linux/<Name>.so" > objdump.txt`;
  `node tools/release-facts.js linux --tarball <tarball> --extracted "$RUNNER_TEMP/tar-x" --objdump objdump.txt --out
  linux-facts.json`; upload it as `linux-facts`. The staging and `tar` lines are unchanged.
- **FR-043** `release` job, before `Create Release`: download `macos-facts` and `linux-facts`; Node 24, `npm ci --prefix
  tools/feed-contract`; rsign2 0.6.7 installed as `feed-tools.yml:44-56` does (cache keyed `rsign2-0.6.7-<os>-<arch>`,
  `cargo install rsign2 --version 0.6.7 --locked`), and a check that `rsign --version` prints `rsign2 0.6.7`.
- **FR-044** Signing configuration (D6), one step, placed before every signing step and before `Create Release`: its
  `env` carries `KRATE_SIGNING_KEY: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}` and `KRATE_SIGNING_KEY_PASSWORD: ${{
  secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD }}`; it runs `node tools/gen-release-manifest.js --signing-config --dry-run
  ${{ inputs.dry_run }} --public-key tools/feed-contract/keys/krate-signing.pub` (FR-029), and a non-zero exit fails the
  job, so a real run without a complete signing configuration never reaches `Create Release`. On `signing=throwaway`:
  `rsign generate -W -p "$RUNNER_TEMP/krate.pub" -s "$RUNNER_TEMP/krate.key"` (roadmap line 375). On `signing=on`:
  `umask 077`, `node tools/minisign.js decrypt-key --out "$RUNNER_TEMP/krate.key"` (FR-009b; the password reaches it only
  through the environment, and a wrong password fails the job with `E_MINISIG_PASSWORD` before any signing), and the
  committed public key copied to `$RUNNER_TEMP/krate.pub`. There is no unsigned branch: every later signing step runs on
  every run, real or dry.
- **FR-045** Signing: for each line of FR-027's output, `rsign sign -W -s "$RUNNER_TEMP/krate.key" -t "<comment>" -x
  "installers/<asset>.minisig" "installers/<asset>"` with stdin from `/dev/null`. Then FR-020 with `--pub-date` the
  current UTC time (D9), `--repo ${{ github.repository }}`, `--public-key "$RUNNER_TEMP/krate.pub"`, writing
  `installers/release.json` and `installers/SHA256SUMS`. Then, independently, `node tools/minisign.js verify -p "$RUNNER_TEMP/krate.pub" -x <asset>.minisig <asset>` for each
  asset (the Node verifier against the committed public key on a real run), `rsign verify -p "$RUNNER_TEMP/krate.pub"
  -x <asset>.minisig <asset>` for each asset and `sha256sum -c SHA256SUMS` in `installers/`. The decrypted key is
  deleted right after the last signature: the step `Delete signing key (after signing)` runs directly after `Sign assets`
  and removes `$RUNNER_TEMP/krate.key`, so every later step (the manifest, the verification, `Create Release`, the
  attestation, the dry-run steps) runs with `krate.pub` only; a step with `if: always()` (`Delete signing key`) deletes
  `$RUNNER_TEMP/krate.key` again at the end as the backstop (plan PD-20, plan SI-10).
- **FR-046** Attestation (D10, Q7), real runs only: `actions/attest-build-provenance@v4` with `subject-path` the three
  installer assets and nothing else. The `release` job's permissions become `contents: write`, `id-token: write`,
  `attestations: write`.
- **FR-047** `Create Release` runs only when `!inputs.dry_run`. It gains `make_latest: false` (report line 54),
  `target_commitish: ${{ github.sha }}` (Q5, D12: the tag is always the commit that was built and described),
  `fail_on_unmatched_files: true` (the action's default is false, External facts, so a renamed `.minisig` or a mistyped
  path would otherwise be dropped silently and ship a half-signed release, Q10), `overwrite_files: false` (FR-047b,
  plan PD-21), and `SHA256SUMS`, the three
  `.minisig`s and `release.json` in `files`, all in this one call (D8). Its other inputs (`tag_name`, `name`, `draft`,
  `prerelease`, `body_path`) and the three asset lines are unchanged.
- **FR-047b** (plan PD-21, plan SI-10) A real `release.yml` run refuses a tag that already exists: a new step `Tag must
  not exist` (`if: ${{ !inputs.dry_run }}`, the first Phase 2 step of the `release` job, before `Signing configuration
  and key`) fails when `git rev-parse -q --verify refs/tags/$TAG` succeeds or when the tag is in `gh release list
  --limit 1000 --json tagName` (drafts included; 1000 rows fails the step, the `E_BACKFILL_LIST_TRUNCATED` rule; a `gh`
  failure fails the step: fail closed). `Create Release` also gets `overwrite_files: false` (the action's default is
  `true`, External facts) as a second layer, so a re-dispatch of a released version cannot replace published, signed
  assets. A dry run creates nothing, so the check is skipped on dry runs.
- **FR-047a** (Q1, fail closed) A real `release.yml` run that cannot produce the manager files fails the release before
  `Create Release`. Signing not configured fails at the FR-044 step. Any facts failure (`E_FACTS_ARCH_MISMATCH`,
  `E_FACTS_NO_MINOS`, `E_FACTS_NO_GLIBC`, `E_FACTS_NO_MACHO`, `E_FACTS_ARCH`, `E_FACTS_MACHO_CORRUPT`,
  `E_FACTS_LISTING_MISMATCH`, `E_LAYOUT`, a `lipo` cross-check mismatch) fails `macos-installer` or `linux-archive`, and
  the `release` job `needs` both with no condition that lets it run after a failed need. No facts or signing step has
  `continue-on-error`, no step turns one of these failures into a warning, and no input (such as `allow_unmanaged`)
  releases without the manager files. A dry run fails on the same facts failures.
- **FR-048** Dry run: instead of `Create Release`, `actions/upload-artifact@v7` uploads `installers/` (three assets,
  three `.minisig`, `SHA256SUMS`, `release.json`) and the throwaway `krate.pub` as `release-dry-run`, then the job runs
  `node tools/minisign.js verify` on each asset against that `krate.pub` (the Node verifier on the real assets). No
  tag, no release, no attestation.
- **FR-049** `deploy-pages` gains `if: ${{ !inputs.dry_run }}` and is otherwise unchanged (Phase 3 replaces its feed
  step).
- **FR-050** The secrets appear only in the FR-044 step's `env` (as `KRATE_SIGNING_KEY` and
  `KRATE_SIGNING_KEY_PASSWORD`); no step echoes either secret or the key file, neither is ever an argument of any
  command, and the decrypted key is written only under `$RUNNER_TEMP` (FR-009b) and removed by the `if: always()` step
  of FR-045; it is never uploaded (the artifact paths of FR-048 name `installers/` and `krate.pub` only).
### E. Backfill (roadmap lines 377-387; Q10, line 34)

- **FR-051** `tools/backfill-sign.js` exports `main(argv, io)` (FR-076), `plan`, `download`, `facts`, `apply` and
  `productionDeps(workDir, repoDir)`, each subcommand taking explicit options and a `deps` object `{gh(argv) → {status, stdout,
  stderr}, git(argv) → {status, stdout}, exec(command, argv) → {status, stdout, stderr}, sign(file, trustedComment,
  keyFile) → minisig text, log, listDirectory?}`, where the optional `listDirectory` is passed to `linuxFacts` (default
  `listing.listingFromDirectory`; plan PD-4, plan SI-1); `exec` admits only `pkgutil`, `lipo`, `tar` and `objdump`.
  `productionDeps` spawns
  `gh`, `git`, those four tools and `rsign` with fixed argument arrays (never a shell string, never `shell: true`), each
  with `cwd` = the given work directory except `git`, which runs with `cwd` = `repoDir`; the CLI (`main`'s default
  `deps`, `cliDeps(argv)`) passes `--work` and `--plugins-dir`, so D12's `git show <tag>:plugins/<dir>/…` runs inside
  the checkout, where the tags are (`backfill-sign.yml` passes `--work "$RUNNER_TEMP/work"`, outside
  `$GITHUB_WORKSPACE`, and `--plugins-dir plugins` from the workspace; test `backfill production git runs in
  --plugins-dir, not in --work`, and the workflow-shape subtest `both apply steps pass --plugins-dir plugins from the
  checkout`); its `sign` runs `rsign sign -W -s <keyFile> -t <comment> -x <file>.minisig
  <file>` with stdin ignored (`/dev/null`) and returns the `.minisig` text it wrote. Tests pass fakes, except SC-025,
  which drives the production `sign`. No identifier in the file matches the hygiene scan's `FETCH_RE`
  (`hygiene.test.js:57`), which is why the download subcommand is `download`. It contains no plugin name: the plugin
  set is the `version.json` files under `--plugins-dir` (ROSTER).
- **FR-052** `plan --plugins-dir <dir> --repo <owner/name> --out <plan.json>`: one `gh release list --repo <repo> --limit
  1000 --json tagName,isPrerelease,isDraft,publishedAt` (a returned count of 1000 is `E_BACKFILL_LIST_TRUNCATED`, exit 1,
  because the default limit of 30 would already miss releases); per plugin the current release =
  `gen-feed.selectReleases(name, releases).stable` (the Q9 rule, newest non-prerelease, drafts ignored); then `gh release
  view <tag> --repo <repo> --json tagName,publishedAt,isImmutable,assets`. A plugin with no stable release is listed as
  skipped: `selectReleases` returns `null` for it (`tools/gen-feed.js:440`) and `plan` never dereferences that result.
  `selectReleases` throwing `E_TAG_FORMAT` for a malformed `<p>/v…` tag (`tools/gen-feed.js:427-428`) aborts `plan`:
  exit 1, no plan file written, 0 uploads, because the backfill must pick exactly what gen-feed would (plan PD-10, plan
  SI-9). A release whose assets include `release.json` is **skipped** (idempotence, roadmap line 384). Otherwise it is
  **reported** and gets nothing when: one of the three asset names is missing (`E_BACKFILL_ASSET_MISSING`, naming it);
  `isImmutable` is true (`E_BACKFILL_IMMUTABLE`); it already carries `SHA256SUMS` or a `<asset>.minisig` without
  `release.json` (`E_BACKFILL_PARTIAL`: an earlier upload stopped midway; a human removes the strays). Every remaining
  release is **planned** with its tag, version, `publishedAt` and the three assets' `name`, `size` and `digest`. For
  every release it viewed (planned, skipped or reported), `plan.json` records the full sorted list of asset names the
  release carried at plan time (`assets_before`), the baseline of FR-057a.
- **FR-053** `download --plan <plan.json> --format <inno-exe|pkg|tar.gz> --dir <dir> --repo <owner/name>`:
  `gh release download <tag> --repo <repo> --pattern <asset> --dir <dir>/<tag with "/" as "-">` per planned release,
  then the file's size must equal the plan's `size` (`E_BACKFILL_SIZE`) and its SHA-256 must equal `digest` minus the
  `sha256:` prefix (`E_BACKFILL_DIGEST`). An asset whose `digest` is `null` (the REST schema allows it) is refused with
  `E_BACKFILL_DIGEST` ("has no digest on GitHub"): without a trusted reference hash it is never unpacked, measured or
  signed (Clarifications 2026-10-30, CC-verify; measured: all 264 stable installer assets carry a digest). A failing
  release gets an entry in `<dir>/download-report.json`; the others continue. `download` and `apply` log
  `digest checked: <n> of <m>` per format, and SC-019's evidence records those lines (plan PD-28).
- **FR-053a** `facts --plan <plan.json> --os <darwin|linux> --dir <download dir> --work <dir> --out <facts dir>`, the
  per-release facts loop (in the tool, not in workflow shell, so it is tested): a planned release with a
  `download-report.json` entry gets that entry copied into the facts report (below) unchanged; for every other planned
  release, `darwin`: `exec('pkgutil', ['--expand-full', <pkg>, <work>/<tag>-x])`,
  `darwinFactsFromTree`, then `exec('lipo', ['-archs', <file>])` for every listed Mach-O, whose sorted words must equal
  the Node set (`E_FACTS_LIPO_MISMATCH`, naming the file); `linux`: first, in-process, `listingFromTarGz` of the
  tarball, the single-bundle rule and `checkLinuxLayout` (the `linux-precheck` checks, FR-013), so an unsafe or
  ill-formed archive is reported before anything extracts it; then `exec('tar', ['-xzf', <tarball>, '-C',
  <work>/<tag>-x])`, `exec('objdump', ['-T', <bundle .so>])` only when the listing holds
  `<Name>.vst3/Contents/x86_64-linux/<Name>.so` as a regular-file entry, and `linuxFacts` (FR-015) (plan PD-26, plan
  SI-11). The `facts-linux` job runs `umask 000` in the step that runs `facts --os linux`, so the extracted modes are
  the archive's modes; the `exec` argument arrays are unchanged (plan PD-13). Success writes
  `<out>/<name>-<version>.<darwin|linux>.json` in the FR-013 / FR-015 form; any failure of one release (a
  `ContractError` from release-facts, a non-zero `exec`, a lipo mismatch) writes no facts file for it and appends
  `{tag, code, message}` to `<out>/facts-report.<os>.json`, and the loop continues. The facts report is the contract
  between the facts jobs and `apply`: a written file always holds a JSON array (empty when nothing failed). Exit 0 when
  the loop finished (per-release failures included), 1 on an abort (unreadable plan, unwritable `--out`), 2 on usage.
- **FR-054** `apply --plan <plan.json> --plugins-dir <dir> --facts <dir> --key <file> --public-key <file> --repo
  <owner/name> --work <dir> --out-dir <dir> [--dry-run]`: reads every `facts-report.<os>.json` in `--facts` (a facts
  job's missing report file is an abort, `E_INPUT`, because it means that job never ran its loop) and reports each
  listed release under the code the entry carries; for every other planned release, downloads the three assets (FR-053
  checks); reads the tag's `version.json` and `setup.iss` through `git show` (D12; a tag version mismatch is
  `E_VERSION_MISMATCH`); requires both `<facts>/<name>-<version>.darwin.json` and `.linux.json` (either missing is
  `E_FACTS_MISSING`, naming it); signs each asset with the FR-027 comment through `deps.sign`; runs
  `gen-release-manifest.buildManifest` in-process with `--pub-date` = the release's `publishedAt` (D9) and those facts
  files; copies the outputs to `<out-dir>/<tag with "/" as "-">/`. Any facts, layout, binding or manifest failure
  reports that release by name and code and attaches nothing to it. `apply` runs in three passes (plan PD-9): (1) per
  planned release: facts, downloads, `git show`, sign, verify each new signature against `--public-key` at once
  (FR-055), `buildManifest`; (2) the uploads; (3) the FR-057a audit; nothing is uploaded before pass 1 has finished for
  every release. The decrypted key is deleted right after the last signature: `apply` removes the `--key` file itself
  as soon as pass 1 ends (and in a `finally` on any abort inside pass 1), so passes 2 and 3 run without it; the
  workflow's `if: always()` delete stays as the backstop (plan PD-20, plan SI-10).
- **FR-055** Before the first upload of a run, `apply` verifies every signature it produced against `--public-key`; any
  failure (a secret that does not match the committed key, D5) is `E_SIGN_KEY_MISMATCH`, exit 1, **zero** uploads.
- **FR-056** Without `--dry-run`, per signed release: `gh release upload <tag> --repo <repo> SHA256SUMS
  <asset>.minisig ×3`, then `gh release upload <tag> --repo <repo> release.json` (D8). No call carries `--clobber`, and no
  uploaded name equals a name the plan recorded on that release. With `--dry-run`, `apply` makes **no** `gh release
  upload` call.
- **FR-057** `apply` prints a summary: per plugin `signed`, `skipped (already signed)`, `skipped (no stable release)` or
  `reported <CODE>`, then `uploads: <n>`. Exit codes: 0 when nothing was reported, 3 when at least one release was
  reported (the others are still signed; the run turns red), 1 on an abort (`E_BACKFILL_LIST_TRUNCATED`,
  `E_TAG_FORMAT` in `plan` (FR-052, plan PD-10), `E_SIGNING_CONFIG` in `apply` when `--public-key` is a test key as
  FR-029 refuses it (plan PD-23), `E_SIGN_KEY_MISMATCH`, `E_BACKFILL_ASSETS_CHANGED`, a missing facts report, a `gh`
  failure outside a single release), 2 on usage (plan SI-9). A failing `gh release upload` is inside one release, so it
  is a per-release report: that release is reported `E_INPUT` (naming `gh release upload <tag>` and the status, never a
  file's contents), its remaining upload is not made (`release.json` is always last, D8), the other releases continue,
  the FR-057a audit still runs, and the exit is 3; if the failed call left some of its names on the release, the audit
  reports `E_BACKFILL_ASSETS_CHANGED` (exit 1), and a rerun's `plan` classifies that release `E_BACKFILL_PARTIAL` (plan
  PD-24, plan SI-9).
- **FR-057a** Asset audit, last step of every `apply` run (dry or live): for every release in `plan.json`,
  `gh release view <tag> --repo <repo> --json assets`, and its sorted asset-name list must equal `assets_before`
  (FR-052) with a dry run, and `assets_before` plus exactly the names `apply` uploaded to that release with a live run.
  Any other difference is `E_BACKFILL_ASSETS_CHANGED` naming the release and the differing names, exit 1. The audit
  makes no upload and needs only read access.
- **FR-058** `.github/workflows/backfill-sign.yml`: `workflow_dispatch` only, input `dry_run` (`type: boolean`, `default:
  true`); `concurrency: group: backfill-sign, cancel-in-progress: false`; top-level `permissions: contents: read`;
  `GH_TOKEN: ${{ github.token }}`. Jobs: `plan` (`ubuntu-latest`, checkout `fetch-depth: 0`, `backfill-sign.js plan`,
  plan uploaded as an artifact); `facts-macos` (`macos-latest`: `backfill-sign.js download --format pkg`, then
  `backfill-sign.js facts --os darwin` (FR-053a); the facts directory, with `facts-report.darwin.json`, uploaded as an
  artifact); `facts-linux` (`ubuntu-latest`: `download --format tar.gz`, then `facts --os linux`; facts artifact with
  `facts-report.linux.json`). A failure of one release lands in the facts report and the job stays green, so one bad old
  pkg cannot stop the other eight; `apply` turns it into a named report (FR-054, exit 3). Then two apply jobs, because
  `permissions:` takes only literal values and cannot depend on `inputs.dry_run` (External facts):
  `apply-dry` (`if: ${{ inputs.dry_run }}`, `permissions: contents: read`) and `apply-live` (`if: ${{ !inputs.dry_run
  }}`, `permissions: contents: write`), both `ubuntu-latest`, needing all three earlier jobs, with the same steps: rsign2
  0.6.7 as FR-043; the key step (the secrets in its `env` as `KRATE_SIGNING_KEY` and `KRATE_SIGNING_KEY_PASSWORD`, decrypted as FR-044; both secrets must be
  non-empty and the committed public key must parse, checked with `gen-release-manifest.js --signing-config --dry-run
  false`, else exit 1); `backfill-sign.js apply`, with `--dry-run` in `apply-dry` only; the out-dir uploaded as the
  artifact `backfill-sign-output`; key file deleted in an `if: always()` step. The two jobs' `steps:` are identical
  except for that `--dry-run` argument, so the dry run exercises the live job's steps with a token that cannot write.
  Both `apply-dry` and `apply-live` check out with `fetch-depth: 0`, because D12's `git show <tag>:…` needs the tags
  (plan PD-14), and both carry `environment: signing`: the two secrets are environment secrets of the GitHub
  Environment `signing`, whose deployment-branch policy is "Selected branches and tags: `main`", so no job run from
  another branch receives them (plan PD-22, plan SI-10).
- **FR-059** The workflow lists no plugin names (ROSTER), contains no `--clobber`, references
  `secrets.TAURI_SIGNING_PRIVATE_KEY` only in the key steps of `apply-dry` and `apply-live` (twice in the file) and
  `secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD` only in the same two steps (twice); neither the key nor the password is an
  argument of any command.
- **FR-059a** (Q2) `backfill-sign.yml` has `workflow_dispatch` as its only trigger: no `push`, `pull_request`,
  `schedule` or other temporary trigger is added, and the phase makes no commit to main (the user's merge is the only
  way the file reaches the default branch). SC-019 is therefore PENDING-CI until the user merges the phase to main,
  because GitHub runs a `workflow_dispatch` only for a workflow file on the default branch, and the phase closes
  without it.
### F. Keys

- **FR-060** No private key is generated into, written into or committed to the repository (Cross-Cutting "Security",
  roadmap lines 964-974). Tests make throwaway keys with `minisign.generateKeyPair()` in memory or with `rsign generate -W`
  into an `fs.mkdtemp(os.tmpdir())` directory that is removed. Phase 1's FR-024 secret-key scan over
  `tools/feed-contract/` keeps passing, now including `keys/`, with exactly one path exempt: the FR-064 KAT secret key (its exact path, the
  `rsign encrypted secret key` header and its key id are asserted, so the exemption cannot hide another key).
- **FR-061** `tools/feed-contract/keys/krate-signing.pub` is added by the user together with the secrets (D5, Q6); this
  phase ships `tools/feed-contract/keys/README.md` stating the file's format, the user steps and the merge instructions
  (FR-062). The user steps: `tauri signer generate` with a non-empty password (`-p`), create the GitHub Environment
  `signing` with the deployment-branch policy "Selected branches and tags: `main`", store the private key file content
  as its environment secret `TAURI_SIGNING_PRIVATE_KEY` and the password as its environment secret
  `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` (the `release` job of `release.yml` and the `apply-dry` / `apply-live` jobs carry
  `environment: signing`, so every `release` job run, dry or real, starts only from `main`; plan PD-22, plan SI-10),
  base64-decode the `.pub` into `krate-signing.pub` and commit it. It states that the password must be non-empty (D4)
  and that Phase 11 passes the same password to Tauri through `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`. No test requires the
  real public key file to exist. `krate-signing.pub` is the one path under `tools/feed-contract/` the user commits on
  the phase branch (FR-080, SC-020). The path is new to the roadmap: its Repository Layout (roadmap lines 213-249),
  Phase 7's "embedded public key" (roadmap line 591) and Phase 11 (roadmap line 784 on) must name it when this spec is
  encoded into the roadmap (Traceability "Additions to the roadmap").
- **FR-062** (Q1, merge instructions) The phase's merge instructions (this spec's "Merge instructions" section and
  `keys/README.md`) state, in this order: (1) create the GitHub Environment `signing` (deployment branches: `main` only)
  and in it the two environment secrets `TAURI_SIGNING_PRIVATE_KEY` and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` (plan
  PD-22, plan SI-10); (2) commit `tools/feed-contract/keys/krate-signing.pub` on the branch; (3) only
  then merge to main, because from the merge on every real `release.yml` run fails with `E_SIGNING_CONFIG` until (1) and
  (2) are done, and fails on any facts failure (FR-047a, D6); (4) SC-018 and SC-019 are dispatched after the merge.
- **FR-064** (Q3) The decrypt known-answer fixtures live in `tools/feed-contract/fixtures/phase2/kat-secret/`: `kat.key`
  (a secret key made once by rsign2 0.6.7 `generate` at a terminal with a password, which the test file publishes as a
  constant; made per the Q-A ruling (Clarifications, Session 2026-10-30 (plan)): by the main loop with rsign2 0.6.7 at a
  pseudo-terminal, WSL Ubuntu 24.04 under `script -qec`, with the published test password `krate-kat-test-password`,
  following the tasks.md Preconditions steps 1-5 (plan SI-6)), its `kat.pub`, a `.minisig` made with it over a small committed file, and a `README.md` recording the
  generation steps. The key is test-only, signs nothing that ships, and is the only secret-key file in the repository
  (FR-060). Test-time throwaway keys are still made in memory or with `rsign generate -W`.
### G. Tests, lane and contract plumbing

- **FR-070** New test files in `tools/feed-contract/tests/`: `minisign.test.js`, `release-facts.test.js`,
  `gen-release-manifest.test.js`, `backfill-sign.test.js`, `workflow-shape.test.js`; each requires
  `./helpers/write-guard.js` first. Fixtures under `tools/feed-contract/fixtures/phase2/`: a `KatPlugin` plugin dir
  (`version.json` `{name: "KatPlugin", version: "1.2.3"}`, `installers/windows/setup.iss` with one `AppId={{…}` line),
  `objdump -T` text samples, and a backfill set of nine synthetic plugins (names not shipped plugins) with the fake `gh`
  state as JSON. Synthetic Mach-O bytes are built in a test helper (`helpers/macho.js`) from the structures in External
  facts; stub `.exe` / `.pkg` bytes are written into the test's temp dir. No fixture holds a real signature other than
  the Phase 1 KAT; every Phase 2 signature is made at test time with a throwaway key. The one secret-key fixture is FR-064's.
- **FR-071** `write-guard.js` admits `rsign` in exactly four shapes, each with the `cwd` and every path argument
  resolving under `os.tmpdir()`: `rsign --version` (no path argument), `rsign generate -W -p <pub> -s <sec>`, `rsign
  sign -W -s <sec> -t <comment> -x <sig> <file>`, `rsign verify -p <pub> -x <sig> <file>`; any other `rsign` argument
  list, and `shell: true`, are refused. The guard's "has teeth" subtest gains one refused `rsign` shape.
- **FR-072** `hygiene.test.js`: the offline scan's file list adds `tools/minisign.js`, `tools/release-facts.js`,
  `tools/gen-release-manifest.js` and `tools/backfill-sign.js` (named in the completeness list); `CHILD_EXEMPT` gains
  exactly two entries, `tools/backfill-sign.js` (it spawns `gh`, `git`, `rsign` in production) and
  `tools/feed-contract/tests/helpers/external-tools.js` (the one test-side spawner of `rsign` and `tar`, plan PD-5, plan
  SI-2), and no other new tool or test file may load `child_process`; the four
  tools join the "no product file names a shipped plugin" list. `backfill-sign.js` is **not** exempt from `FETCH_RE`
  (`hygiene.test.js:57`): FR-051 names nothing `fetch`, so the offline scan applies to it in full. A new top-level test
  in this file, SC-026, checks the four tools' form (FR-076) and that every `E_…` code literal in their source is a
  `contract.js` `CODES` key. The FR-060 secret-key scan gains its one-path exemption for FR-064's `kat.key` in
  `tools/feed-contract/tests/c3-trusted-comment.test.js`, where the Phase 1 FR-024 scan lives
  (`assert.deepEqual(findSecretKeyFiles(CONTRACT_DIR), [])`, `c3-trusted-comment.test.js:275`), not in
  `hygiene.test.js`, which has no secret-key scan (plan PD-6, plan SI-3).
- **FR-073** `feed-tools.yml`: the path filter adds `tools/release-facts.js`, `tools/backfill-sign.js`,
  `.github/workflows/release.yml` and `.github/workflows/backfill-sign.yml` (both triggers). The TAP check stops reading
  `# tests N`, which counts every subtest (350 at `1dec384c`, against a plan line of `1..24`), and instead requires, on
  the TAP output's column-0 lines: (a) `# fail 0`; (b) the top-level plan line `1..N` with **N = 46** exactly, the 24
  top-level tests measured at `1dec384c` plus the 22 new top-level tests the SCs name (SC-001 to SC-014, SC-022 to
  SC-029); (c) each of those 22 names, carried in the step's check script, appears as a top-level `ok <n> - <name>`
  line; (d) when `CI` is set, no top-level line carries `# SKIP`; (e) no line inside the SC-003, SC-025 or SC-027 test
  block (from its column-0 `# Subtest: <name>` line to its own `ok N - <name>` line) carries `# SKIP` (plan PD-27). The
  check script is a bash heredoc (`node - <<'TAPCHECK'`) in the `node --test` step, and
  `specs/krate-manager-phase2/evidence/tap-check.js` runs the same script locally by extracting the heredoc body from
  `feed-tools.yml` and running it with `CI=1` in its sandbox, so a `# SKIP` in those three blocks fails on CI and in
  the local close alike (plan PD-15, plan PD-27). A renamed, unmatched or unwritten test file therefore
  turns the lane red. A Phase 2 test file holds no top-level tests beyond the ones its SCs name (subtests are free), so
  N stays exact. No `--test-concurrency=1`, `--test-isolation=none` or other serial flag (Cross-Cutting "Parallel
  tests").
- **FR-074** The 27 new codes are `E_MINISIG_PUBKEY`, `E_MINISIG_SECRET_KEY`, `E_MINISIG_PASSWORD`, `E_MINISIG_KEY_ID`, `E_MINISIG_SIGNATURE`, `E_MINISIG_GLOBAL`,
  `E_FACTS_NO_MACHO`, `E_FACTS_ARCH`, `E_FACTS_ARCH_MISMATCH`, `E_FACTS_NO_MINOS`, `E_FACTS_MACHO_CORRUPT`,
  `E_FACTS_NO_GLIBC`, `E_FACTS_LISTING_MISMATCH`, `E_FACTS_LIPO_MISMATCH`, `E_FACTS_STALE`, `E_FACTS_MISSING`,
  `E_ASSET_MISSING`, `E_VERSION_MISMATCH`, `E_SIGNING_CONFIG`, `E_BACKFILL_LIST_TRUNCATED`, `E_BACKFILL_ASSET_MISSING`,
  `E_BACKFILL_IMMUTABLE`, `E_BACKFILL_PARTIAL`, `E_BACKFILL_SIZE`, `E_BACKFILL_DIGEST`, `E_BACKFILL_ASSETS_CHANGED`,
  `E_SIGN_KEY_MISMATCH`. `contract.js` `CODES` lists each with class `none` (not gen-feed outcomes). Their rows go in the
  **existing code table of `## 7. gen-feed: modes, rules, codes and exit codes`** (columns `Code | Class | Raised by |
  Meaning`, `CONTRACT.md:418-419`), each with `none` in the Class cell and the raising tool in Raised by, because the
  unchanged check `CONTRACT.md documents every code and field` reads codes from §7 only (`hygiene.test.js:359-394`).
  The tools' CLI lines and their exit-code lists go in a new ninth heading, `## 9. Phase 2 tools`, after §8: the eight
  `CONTRACT_HEADINGS` stay, `markdownSections` rejects only a duplicate heading (`hygiene.test.js:301-316`), and the
  §7 exit-code list check (`- \`0\`: …`, `:395-401`) keeps reading gen-feed's list, since §9's lists are outside §7.
- **FR-075** `tools/gen-feed.js` adds `selectReleases` to `module.exports` and changes nothing else; every Phase 1 test
  stays green.
- **FR-076** **Tool form (Cross-Cutting "Naming", roadmap lines 988-989).** Each of the four new tools,
  `tools/minisign.js`, `tools/release-facts.js`, `tools/gen-release-manifest.js` and `tools/backfill-sign.js`, is a flat
  file whose first line is `#!/usr/bin/env node`; exports `main(argv, io)` (`io` = `{stdout, stderr, env}`, the shape of
  `process`), which returns the exit code and is run only when the file is the entry point, as `gen-feed.js:760` does;
  and on missing or unknown arguments writes a line starting `usage: node tools/<file>` to stderr and returns 2. Tests
  call `main` in-process, because the write guard admits no `node` child process (`write-guard.js:235`).

### H. Scope fences

- **FR-080** Phase 2 changes only: `tools/minisign.js`, `tools/release-facts.js`, `tools/gen-release-manifest.js`,
  `tools/backfill-sign.js`, `tools/gen-feed.js` (FR-075 only), `tools/feed-contract/**` (tests, helpers, fixtures under
  `fixtures/phase2/`, `keys/README.md`, `keys/krate-signing.pub` (committed by the user, FR-061, not by an agent),
  `CONTRACT.md`, `contract.js` `CODES` only), `.github/workflows/release.yml`,
  `.github/workflows/backfill-sign.yml`, `.github/workflows/feed-tools.yml`, `specs/INDEX.md` (regenerated with
  `node tools/gen-specs-index.js`, as Phase 1 did), `specs/Krate-manager-roadmap.md` (the FR-061 lines only: Repository
  Layout, Phase 7's embedded public key, Phase 11's signing secrets and updater pubkey, written by the main loop) and
  this spec's directory. It does not touch `ci.yml`, any installer
  script, any `version.json`, any plugin source or `plugins/shared/`, or the Phase 1 fixtures, and it publishes nothing.
- **FR-081** In `release.yml`, no existing line of the `windows-installer` job and no existing line of the steps that
  build the pkg (`:285-337`) or the tarball (`:379-403`) changes; new steps are only added after them (roadmap line 415:
  "change the installers' content").
- **FR-082** No C++ file changes, so no `run-gate.js` lane is owed; `node tools/header-consumers.js` is not needed
  because no header is modified (CLAUDE.md "Run only the tests the change set can reach").

---

## Success Criteria

All Node SCs run locally on Windows with `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` (quoted
glob, parallel files). A run counts only if the TAP output meets FR-073: `# fail 0`, the top-level plan line `1..46`,
and each of the 22 SC-named tests as a top-level `ok` line (the `# tests` summary counts subtests and is not used). Each Node
macOS/Linux row is **PENDING-CI** on `feed-tools.yml` jobs `node-test (macos-latest)` and `node-test (ubuntu-latest)`,
triggered by the user's push. Rows on the real assets are **PENDING-CI** on user-dispatched runs of `release.yml` and
`backfill-sign.yml` on GitHub-hosted runners.

- **SC-001 (KAT).** Test `minisign.js verifies the rsign2 KAT`: `verifyFile` on the KAT returns trusted comment
  `krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz` and key id `440FA75168B852F7`; four subtests on
  temp copies each fail with the named code: one archive byte flipped → `E_MINISIG_SIGNATURE`; one byte of the decoded
  line-2 signature flipped and re-encoded → `E_MINISIG_SIGNATURE`; one key-id byte flipped → `E_MINISIG_KEY_ID`; one
  trusted-comment character changed → `E_MINISIG_GLOBAL`. Threshold: 1 accept, 4 named rejections.
- **SC-002 (round trip and rejections).** Test `minisign.js sign and verify round trip`: a fresh pair signs a 1 MiB temp
  file with a C3 comment; `verifyFile` accepts it and returns the comment; the text passes `parseMinisigText`, has LF only
  and a final LF; `parsePublicKey(formatPublicKey(k))` round-trips the key id; a second pair's public key gives
  `E_MINISIG_KEY_ID`; a second pair given the first pair's key id gives `E_MINISIG_SIGNATURE`; a signature whose line-2
  algorithm is changed to legacy `Ed` gives `E_SIGNATURE_TEXT`; a CRLF `.minisig` file verifies; three malformed public
  keys each give `E_MINISIG_PUBKEY`: a first line not starting `untrusted comment: `, a second line that is not canonical
  base64 (a trailing extra `=` or a non-alphabet character), and a key decoding to 41 bytes; a fourth, 42 bytes with the
  algorithm `ED` in place of `Ed`, also gives `E_MINISIG_PUBKEY`. Threshold: all asserts, 4 named public-key rejections.
- **SC-003 (interop, FR-008).** Test `minisign.js and rsign2 interoperate`: it first runs `rsign --version` and requires
  the output `rsign2 0.6.7` (D13: skip with the found version printed when `CI` is unset, fail when it is set); then two
  subtests: `rsign verify` exits 0 on a `minisign.js` signature and prints its trusted comment; `verifyFile` accepts an
  `rsign sign -W -t` signature from an `rsign generate -W` key. Windows: local run, closed only when the version check
  passed and nothing skipped. macOS/Linux and the Windows CI leg:
  PENDING-CI, the same test in `node-test (ubuntu-latest | macos-latest | windows-latest)`, where `CI` is set and a
  missing `rsign` fails the test. Threshold: both subtests pass, not skipped, on every leg.
- **SC-004 (Mach-O facts).** Test `release-facts reads Mach-O arch sets and minimum macOS`, synthetic buffers: thin
  arm64 with `LC_BUILD_VERSION` minos `0x000F0000` → `{archs: [arm64], minOs: "15.0"}`; fat x86_64 (`LC_VERSION_MIN_MACOSX`
  `0x000A0D00`) + arm64 (`LC_BUILD_VERSION` `0x000B0000`) → `[arm64, x86_64]`, slice minimums `10.13` / `11.0`; a tree with
  that fat file and a thin arm64 file → `E_FACTS_ARCH_MISMATCH`; a tree of two fat files → `darwin-universal`, `min_os`
  `11.0`; `minos` `0x000F0601` → `15.6.1`; arm64 subtype `2` (arm64e) → `E_FACTS_ARCH`; a tree holding a thin 32-bit
  file (`0xfeedface`, `cputype` `0x00000007`) beside a valid arm64 file → `E_FACTS_ARCH` from `darwinFactsFromTree` (not
  a skip); a thin `0xcffaedfe` (big-endian 64-bit) buffer → `E_FACTS_ARCH`; a fat file with an i386 slice (`cputype`
  `0x00000007`) → `E_FACTS_ARCH`;
  no version command → `E_FACTS_NO_MINOS`; a command past the end → `E_FACTS_MACHO_CORRUPT`; a tree of plain files →
  `E_FACTS_NO_MACHO`; `platformKeyForArchs` for the three sets. Threshold: every case as listed; on macOS/Linux legs
  (PENDING-CI) a symlink to a Mach-O inside the tree is not counted.
- **SC-005 (glibc floor).** Test `release-facts reads the glibc floor`: an `objdump -T` sample naming `GLIBC_2.2.5`,
  `GLIBC_2.17`, `GLIBC_2.34` and `GLIBC_PRIVATE` → `2.34`; a sample with `GLIBC_2.9` and `GLIBC_2.10` → `2.10` (numeric,
  not lexical); a sample without `GLIBC_` → `E_FACTS_NO_GLIBC`. Threshold: three exact results.
- **SC-006 (manifest from a fixture asset set, roadmap lines 392-394).** Test `gen-release-manifest builds a verified
  release.json`: temp assets = stub `KatPlugin-1.2.3-Windows-x64.exe` (1,000 bytes), stub `KatPlugin-1.2.3-macOS.pkg`
  (2,000 bytes), the KAT tarball; each signed by `minisign.js` with a throwaway key using the FR-027 comments; facts files
  made from the same files; `main()` exits 0; every `size` and `sha256` equals `fs.statSync` and an independent
  `crypto.createHash('sha256')`; every `signature` passes `verifyBuffer` against the throwaway public key and
  `checkBinding`; `checkReleaseManifest` passes; the Linux `payload` is `{files: 10, sha256:
  "db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6"}` with the 10-line listing; `SHA256SUMS` is three
  sorted GNU lines equal to the independent hashes. Three tamper subtests, each a fresh output directory with one byte
  of one asset flipped after signing and fact recording, by the FR-026 check order: `.exe` flipped → exit 1,
  `E_MINISIG_SIGNATURE` (Windows has no facts file); `.pkg` flipped → exit 1, `E_FACTS_STALE` (the darwin facts' `sha256`
  no longer matches, checked before any signature); `.tar.gz` flipped → exit 1, `E_FACTS_STALE`; in each, neither output
  file exists afterwards. `install` deep-equals `{vst3_bundle: 'KatPlugin.vst3', au_component: 'KatPlugin.component',
  windows_uninstall_key: '{0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0}_is1', macos_pkg_id: 'com.krateaudio.katplugin'}` (no
  `preset_subdir` key); a subtest with a temp `version.json` adding `"preset_subdir": "Kits"` and `"identifier":
  "com.krateaudio.katkit"` gives `install.preset_subdir === 'Kits'` and `install.macos_pkg_id ===
  'com.krateaudio.katkit'` (FR-025) (plan SI-12). Threshold: all equalities; `install` deep-equal and the
  `preset_subdir` / `identifier` subtest; 3 tamper subtests with exactly those codes, 0 outputs written.
- **SC-007 (binding and refusals).** Test `gen-release-manifest refuses what it cannot bind`: the `.exe`'s bytes signed
  with the `.pkg`'s trusted comment (cryptographically valid) → `E_TC_BINDING` naming `asset`; the `.exe`'s own `.minisig`
  copied as the `.pkg`'s → `E_MINISIG_SIGNATURE`; a missing `.minisig` → `E_ASSET_MISSING`; darwin facts for another file
  → `E_FACTS_STALE`; a `setup.iss` without `AppId` → `E_INPUT`; a `--public-key` from another pair → `E_MINISIG_KEY_ID`. In
  each case nothing is written. A seventh subtest pre-creates `release.json` and `SHA256SUMS` in the output directory,
  records their bytes and `mtimeMs`, runs the `E_TC_BINDING` case, and asserts both files keep their bytes and
  `mtimeMs` and no temp file is left (FR-026). Subtest 8 (FR-023, plan SI-12): linux facts with the right `asset`,
  `size` and `sha256` but `payload.files` changed, and separately `payload.sha256` changed → exit 1, `E_LISTING_DIGEST`,
  no output. Subtests 9 and 10 (the two-file write, plan PD-25): with pre-created outputs, `--out` replaced by a
  directory → exit 1, `E_INPUT`, `SHA256SUMS` keeps its bytes and `mtimeMs`; with pre-created outputs and
  `fs.renameSync` wrapped in-process to throw on the rename onto `release.json` → exit 1, both outputs keep their bytes
  and `mtimeMs`; neither leaves a `.tmp` or `.bak` file. Threshold: 6 named failures; 2 pre-existing outputs unchanged;
  subtests 8, 9 and 10 as stated (plan SI-12).
- **SC-008 (payload equals the extracted tree, roadmap lines 401-402).** Test `Linux payload digest equals the
  extracted tree's listing`. **Deviation from roadmap lines 401-402, named:** the roadmap asks for the extracted-tarball
  comparison on Windows, but `listingFromDirectory` throws `E_LISTING_NO_POSIX_MODES` on win32 (Phase 1 D4,
  `specs/krate-manager-phase1/spec.md:141`, `:398-399`), so no extracted tree can be listed there. What Windows proves:
  the manifest's `payload` (`files` 10, `sha256` `db287e49…7ad6`) and `payload.list` equal the **committed**
  `KatPlugin-1.2.3-Linux-x64.tar.gz.listing` and its digest, an anchor independent of FR-023's computation, which Phase 1
  SC-009 checks against an extracted tree on POSIX (`specs/krate-manager-phase1/spec.md:732-735`). (Comparing the payload
  with `digest(listingFromTarGz(KAT))` would only repeat FR-023's own computation, so it is not a criterion.) The
  roadmap's "extracted tarball" half is PENDING-CI: on `node-test (ubuntu-latest)` and `node-test (macos-latest)`,
  `tar -xzf` of the KAT into a temp dir and the `release-facts.js linux` CLI (via `main`) on it exit 0 with the same
  `payload`, and a tree with one extra file exits 1 with `E_FACTS_LISTING_MISMATCH`; on the real tarball, the
  `release.yml` dry run's job `Linux Archive`, step `Record Linux facts`, exits 0 (FR-042). Threshold: Windows, payload
  and list equal the committed listing; POSIX legs, both cases as stated.
- **SC-009 (trusted comments).** Test `gen-release-manifest prints the C3 comments`: for darwin facts with
  `platform_key` `darwin-universal`, the mode prints exactly three lines in key order `darwin-universal`,
  `linux-x86_64`, `windows-x86_64`, each `<asset>\t<comment>` with `parseTrustedComment(comment)` returning the plugin,
  version, key and asset. Threshold: 3 exact lines.
- **SC-010 (backfill signs nine releases, roadmap lines 403-405).** Test `backfill signs nine current releases`: the fake
  `gh` serves nine synthetic plugins, each with a stable release, an older stable release, one newer prerelease and one
  draft; `plan` + `apply` (signer = `minisign.js` with a throwaway key, facts from fixtures) exit 0; per plugin the
  selected tag is the newest stable (never the prerelease or draft, nor the older stable); the fake records, per release,
  exactly two upload calls: `[SHA256SUMS, 3 × .minisig]` then `[release.json]`; every uploaded `.minisig` verifies against
  the throwaway public key with its C3 binding; every `release.json` passes `checkReleaseManifest`; no recorded argv
  contains `--clobber`; no uploaded name equals an asset the release already had; the tag's `version.json` / `setup.iss`
  were read through `git show <tag>:…`; the run goes through `download`, `facts` (darwin and linux, with a fake `exec`
  whose `pkgutil` writes synthetic Mach-Os into the work dir and whose `lipo` echoes their sets) and `apply`, and the
  FR-057a audit passes against the fake state. Which lister the linux `facts` leg uses (plan PD-4, plan SI-1): the
  win32 run injects a `listDirectory` that returns the tarball's own listing (the fake `tar` only creates the
  directory); the POSIX legs (PENDING-CI, `node-test (ubuntu-latest | macos-latest)`) extract with the real `tar -xzf`
  through the test helper and use the real lister. Threshold: 9 × 2 upload calls, 27 verified signatures, 9 valid manifests,
  0 `--clobber`, audit clean.
- **SC-011 (idempotence, roadmap line 406).** Test `backfill second run uploads nothing`: rerun against the fake state
  after SC-010's uploads: every plugin `skipped (already signed)`, `uploads: 0`, exit 0. Threshold: 0 upload calls.
- **SC-012 (no half-signed release, roadmap lines 406-407).** Test `backfill reports defective releases by name`: one
  release missing its `.pkg` → reported `E_BACKFILL_ASSET_MISSING` naming the asset, 0 uploads for it, the other eight
  signed, exit 3; `isImmutable: true` → `E_BACKFILL_IMMUTABLE`; `SHA256SUMS` present without `release.json` →
  `E_BACKFILL_PARTIAL`; a downloaded size off by one → `E_BACKFILL_SIZE`; a digest mismatch → `E_BACKFILL_DIGEST`; a
  tarball without `README.txt`, run through the real `facts --os linux` path (`linuxFacts` fails at `checkLinuxLayout`
  before the extracted tree is read, so this runs on Windows) → an `E_LAYOUT` entry in `facts-report.linux.json`, no
  linux facts file, and `apply` reports that release `E_LAYOUT` with 0 uploads; a pkg whose fake expansion holds two
  Mach-Os with different sets, through `facts --os darwin` → `E_FACTS_ARCH_MISMATCH` reported the same way; a pkg whose
  expansion holds no Mach-O → `E_FACTS_NO_MACHO`; a fake `lipo` disagreeing with the Node set → `E_FACTS_LIPO_MISMATCH`;
  a facts directory missing one release's `.darwin.json` with no report entry → `E_FACTS_MISSING`; a fake `git show
  <tag>:…/version.json` carrying another version → `E_VERSION_MISMATCH`, 0 uploads for that release; a missing
  `facts-report.darwin.json` → `E_INPUT`, exit 1, 0 uploads; the release list returning 1000 rows →
  `E_BACKFILL_LIST_TRUNCATED`, exit 1, 0 uploads; a signer key that does not match `--public-key` →
  `E_SIGN_KEY_MISMATCH`, exit 1, 0 uploads in the whole run; a fake whose post-run asset list for one release gains a
  name `apply` did not upload → `E_BACKFILL_ASSETS_CHANGED`, exit 1. Each per-release case leaves the other eight
  signed and exits 3. Added subtests (plan PD-10, PD-23, PD-24, PD-26, SI-9): (a) a tenth plugin with only a prerelease
  and a draft → summary `<name>: skipped (no stable release)`, the nine signed, exit 0; (b) a stable tag `<name>/v1.2`
  → `plan` exit 1, `E_TAG_FORMAT`, no plan file, 0 uploads; (c) the second `gh release upload` (`release.json`) of one
  release fails with nothing left behind → that release `reported E_INPUT`, the other eight signed, the audit runs and
  is clean (expected = `assets_before` plus the four names of its first call), exit 3, and a following `plan` on that
  state classifies it `E_BACKFILL_PARTIAL`; (d) `--public-key` a copy of `fixtures/kat/kat.pub` → `apply` exit 1,
  `E_SIGNING_CONFIG`, 0 uploads; (e) a release whose tarball has a `../x` member, and one with a symlink escaping the
  root → `facts --os linux` reports an `E_LISTING_*` code and no `tar` and no `objdump` exec is recorded for it.
  Threshold: every case with its code, exit code and upload count.
- **SC-013 (backfill dry run).** Test `backfill dry run uploads nothing`: SC-010's fixture with `--dry-run`: 0 `gh release
  upload` calls, nine output directories each holding `SHA256SUMS`, three `.minisig` and a C2-valid `release.json`; the
  FR-057a audit finds every release's asset list equal to `assets_before`. Threshold: 0 upload calls, 9 complete
  directories, audit clean.
- **SC-014 (workflow shape).** Test `release.yml and backfill-sign.yml keep the Phase 2 shape`, text checks on the two
  files. `release.yml`: declares the `dry_run` boolean input with default `false`; declares no `allow_unmanaged` or other
  input beyond `plugin`, `prerelease`, `dry_run`; `Create Release` carries `if:` on `!inputs.dry_run`, `make_latest:
  false`, `target_commitish: ${{ github.sha }}`, `fail_on_unmatched_files: true`, and its three original asset lines;
  `deploy-pages` carries `if:` on `!inputs.dry_run`; `actions/attest-build-provenance@v4` appears exactly once, with an
  `if:` excluding dry runs and a `subject-path` naming the three installer assets and no other file, and
  `actions/attest@` does not appear; the token `secrets.TAURI_SIGNING_PRIVATE_KEY` (whole token) appears exactly once and
  `secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD` exactly once, both in the one step that runs `gen-release-manifest.js
  --signing-config --public-key tools/feed-contract/keys/krate-signing.pub` and `minisign.js decrypt-key`, and that step
  comes before `Create Release` and before every `rsign sign`; `KRATE_SIGNING_KEY_PASSWORD` appears only as an `env:`
  key, never inside a `run:` command line; `release.yml` contains no `signing=off`, no `::warning::` about unsigned
  releases and no `continue-on-error` (D6, FR-047a); the `release` job's `needs` include `macos-installer` and
  `linux-archive` and its job-level `if:` (when present) contains none of `always()`, `failure()`, `cancelled()`; a step
  deleting `krate.key` has `if: always()` in each job that decrypts a key; the Phase 2 step blocks of `release.yml`
  (the step names listed in plan §4.8) and its `dry_run` input block name no plugin outside the existing `options:`
  list (`:34-43`); the unchanged lines that already name plugins (`:22`, `:44`, `:144`, `:161`, `:163`) are outside
  this check, because FR-081 forbids touching them (plan PD-7, plan SI-4). `backfill-sign.yml`, checked whole: no
  `--clobber`; declares `dry_run` with default `true`; only
  `workflow_dispatch` as trigger (no `push`, `pull_request` or `schedule`, FR-059a); top-level `permissions: contents:
  read`; jobs `apply-dry` (`if:` on `inputs.dry_run`, `permissions: contents: read`, no `contents: write` anywhere in
  the job) and `apply-live` (`if:` on `!inputs.dry_run`, the only job carrying `contents: write`); the two jobs' `steps:`
  text equal after removing the one `--dry-run` argument from `apply-dry`; `secrets.TAURI_SIGNING_PRIVATE_KEY` and
  `secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD` (whole tokens) each appear exactly twice, once in each of those jobs' key
  step, each running `gen-release-manifest.js --signing-config --public-key tools/feed-contract/keys/krate-signing.pub`
  and `minisign.js decrypt-key`; the file names **none** of `release.yml`'s `options:` entries (`:34-43`), matched as
  whole words, case-insensitive (ROSTER, FR-059). Added assertions (plan §7.1 SC-014; plan PD-20, PD-21, PD-22, PD-26,
  SI-10): every plan §4.8 Phase 2 step name exists in its job; `Record macOS facts` contains `lipo -archs` and
  `release-facts.js darwin`; `Record Linux facts` contains `release-facts.js linux-precheck` before `tar -xzf`,
  `objdump -T` and `release-facts.js linux`; `Verify signatures and sums` contains `minisign.js verify`, `rsign verify`
  and `sha256sum -c` and comes before `Create Release`; `Upload dry-run artifact` and `Verify dry-run artifact` carry
  `if:` on `inputs.dry_run`; the step `Delete signing key (after signing)` is the step directly after `Sign assets` and
  comes before `Write release.json and SHA256SUMS` and `Create Release` (PD-20); `Tag must not exist` carries `if:` on
  `!inputs.dry_run`, contains `git rev-parse -q --verify` and `gh release list` with `--limit 1000`, and comes before
  `Signing configuration and key` (PD-21); `Create Release` carries `overwrite_files: false` (PD-21); `environment:
  signing` is on the `release`, `apply-dry` and `apply-live` jobs and on no other job (PD-22). Threshold: every check.
- **SC-015 (hygiene).** The existing tests `tools stay offline` and `generator ships no plugin roster` pass with the
  FR-072 lists, and the `(c) the guard has teeth` subtest refuses `rsign sign` with an extra argument and `rsign` with a
  `cwd` outside the temp dir; the FR-024 secret-key scan finds no key file under `tools/feed-contract/` other than FR-064's `kat.key` (including
  `keys/`), the scan and its one-path exemption being in `c3-trusted-comment.test.js` (plan PD-6, plan SI-3);
  `CHILD_EXEMPT` gains exactly `tools/backfill-sign.js` and `tools/feed-contract/tests/helpers/external-tools.js` (plan
  PD-5, plan SI-2); `tools stay offline` runs `FETCH_RE` over `tools/backfill-sign.js` with no exemption and finds no hit.
  Threshold: all subtests pass.
- **SC-016 (Phase 1 tests pass).** Every Phase 1 test passes after FR-075 (`gen-feed` legacy and v2 goldens
  byte-identical), including the unchanged `CONTRACT.md documents every code and field`, which now also sees the 27
  FR-074 rows in §7. The only edits to Phase 1 test files are the FR-071 allowlist in `write-guard.js`, the FR-072
  lists (with the two `CHILD_EXEMPT` entries, plan PD-5) and the SC-026 test in `hygiene.test.js`, and the FR-060
  one-path scan exemption in `c3-trusted-comment.test.js` (plan PD-6, plan SI-3); no Phase 1 assertion is removed or
  loosened. Threshold: 0 failures in the Phase 1 test files; `git diff` of those three files (`write-guard.js`,
  `hygiene.test.js`, `c3-trusted-comment.test.js`) shows only those additions (plan SI-3).
- **SC-017 (three-OS lane, PENDING-CI).** `feed-tools.yml` jobs `node-test (ubuntu-latest)`, `node-test (macos-latest)`,
  `node-test (windows-latest)` are green with the FR-073 check: `# fail 0`, plan line `1..46`, the 22 SC-named
  top-level `ok` lines, no top-level `# SKIP` (so SC-003, SC-025 and SC-027 ran); `rsign verify (KAT)` green; `Checkout
  unchanged` green.
- **SC-018 (release.yml dry run, PENDING-CI, user-triggered; roadmap lines 411-413).** A `workflow_dispatch` of
  `release.yml` with one plugin and `dry_run: true` is green and: the `macOS Installer` job's `Record macOS facts` step
  printed `platform_key` and `min_os` and passed the `lipo -archs` cross-check for every listed Mach-O; the `Linux
  Archive` job printed the glibc floor and passed FR-015's listing check; the artifact `release-dry-run` holds the three
  assets, three `.minisig`, `SHA256SUMS` and `release.json`; `sha256sum -c SHA256SUMS` and `rsign verify` passed for each
  asset; `release.json` passed `checkReleaseManifest` inside FR-020 and its three `sha256` values equal those of the built
  assets; `Create Release` and the attestation step were skipped; `deploy-pages` was skipped. **No tag and no release,
  by before/after comparison** (a user check, because most plugins' current `version.json` version is already tagged,
  e.g. local tags `iterum/v1.0.0`, `membrum/v0.12.2`, `profundum/v1.0.0` for their current versions, so "the tag does
  not exist afterwards" would be wrong whatever the workflow did): immediately before the dispatch and again after it
  finishes, the user saves `git ls-remote --tags origin` and `gh release list --limit 1000 --json
  tagName,isDraft,isPrerelease,publishedAt` (the default limit is 30, External facts); both pairs are byte-identical.
  Recorded in compliance: the measured `platform_key` and `min_os`
  (the first answer to "universal or arm64-only", roadmap lines 344-346) and the glibc floor.
- **SC-019 (backfill dry run, PENDING-CI until the user merges the phase to main; roadmap lines 408-410).** **Status
  ruled by the user (Q2):** GitHub runs a `workflow_dispatch` only for a workflow file on the default branch and
  `origin/main` has no `backfill-sign.yml`, so SC-019 cannot run from the branch. It stays PENDING-CI until the user
  merges the phase to main; the phase closes without it, and no commit to main or temporary trigger is added (FR-059a).
  **Precondition (a user step, an addition to roadmap lines 409-410 under D11, Q4):** before the dispatch the user has
  created the `TAURI_SIGNING_PRIVATE_KEY` secret (password-protected) and the `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`
  secret, and committed `tools/feed-contract/keys/krate-signing.pub` (FR-061, FR-062). A dispatch of `backfill-sign.yml`
  with `dry_run: true` on the real repository, after the merge, then meets all of: the `plan` job lists nine plugins,
  each with its current release and all three assets (nine × three, as the roadmap states); `facts-report.darwin.json`
  and `facts-report.linux.json` are empty arrays; the `apply-dry` job ran, `apply-live` was skipped; the `apply`
  summary lists nine `signed` and ends `uploads: 0`; `apply` exits 0; the artifact `backfill-sign-output` holds nine
  directories, each with a `release.json` that passed `checkReleaseManifest` (C2), `SHA256SUMS` and three `.minisig`,
  signed with the real secret (decrypted with the password secret) and verified against the committed
  `krate-signing.pub` (D11, FR-055); the FR-057a audit, which compares every release's asset-name list after the run
  with `assets_before` in `plan.json`, finds no difference. **Any reported release, an exit 3, or fewer than nine
  planned releases fails SC-019**, and each reported release goes to the user for a ruling before the live run. The live
  run is a user step after this, not this phase's exit criterion.
- **SC-020 (scope).** `git diff --name-only <base>..` lists only FR-080 paths (which include the user-committed
  `tools/feed-contract/keys/krate-signing.pub`, so the SC-019 precondition does not make SC-020 red), and `git diff -U0 <base> --
  .github/workflows/release.yml` shows no deleted or changed line inside `:189-250`, `:285-337` or `:379-403` (only
  added lines after them) (FR-081). Measured by a scope-check script in `specs/krate-manager-phase2/evidence/`, as
  Phase 1's `scope-check.js`. Threshold: 0 out-of-scope paths, 0 changed protected lines.
- **SC-021 (parallel).** Phase 1's `no serial test flags` check passes over the changed `feed-tools.yml`; no new test
  file sets `concurrency: 1` on a `describe`/`test`; that second clause is checked by a subtest of SC-026 that scans
  every `tools/feed-contract/tests/**/*.js` file for `/concurrency:\s*(1|false)\b/` (plan SI-12, plan §7.2). Threshold:
  0 hits.
- **SC-022 (signing configuration, FR-029, D6).** Test `signing configuration is throwaway, on or a failure`, in
  `gen-release-manifest.test.js`, with temp-dir public-key files (a `formatPublicKey` text from a throwaway pair) and
  `main` called in-process with an `io.env`: dry run with and without `KRATE_SIGNING_KEY`, with and without
  `KRATE_SIGNING_KEY_PASSWORD`, with and without the key file → `signing=throwaway`, exit 0; real run, both variables
  non-empty and a valid key file → `signing=on`, exit 0; real run, both set and key file absent → exit 1,
  `E_SIGNING_CONFIG` naming `public key absent`; key secret empty and the rest valid → exit 1, `E_SIGNING_CONFIG` naming
  `secret`; password empty or absent and the rest valid → exit 1, `E_SIGNING_CONFIG` naming `password`; everything
  missing → exit 1, `E_SIGNING_CONFIG` naming all three; both set and a malformed key file → exit 1, `E_SIGNING_CONFIG`
  carrying the `E_MINISIG_PUBKEY` message; real run, both set and `--public-key` a temp copy of `fixtures/kat/kat.pub`
  → exit 1, `E_SIGNING_CONFIG` naming `test key`, while a dry run with that file stays `signing=throwaway` (plan PD-23,
  plan SI-9); in no case does stdout or stderr contain either variable's value. Threshold: 4 dry-run cases
  `throwaway`, 1 `on`, 7 named failures (4 + 1 + 7, plan SI-9), 0 leaks.
- **SC-023 (minisign verify CLI, FR-007).** Test `minisign.js verify CLI prints and exits by contract`, `main` in-process
  with captured `io.stdout` / `io.stderr`: on the KAT (`-p kat.pub -x <.minisig> <tarball>`) stdout is exactly
  `verified: krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz\n` and the exit is 0; on a temp copy
  with one archive byte flipped, exit 1 and stderr starts `minisign: E_MINISIG_SIGNATURE:`; with `-x` missing, and with an
  unknown subcommand, exit 2 and stderr starts `usage: node tools/minisign.js`. Threshold: 1 accept, 1 named
  rejection, 2 usage exits.
- **SC-024 (release-facts darwin CLI, FR-013).** Test `release-facts darwin CLI writes facts and lipo lines`, on
  Windows with synthetic Mach-Os (`helpers/macho.js`): a temp `--expanded` tree with a fat `x86_64 + arm64` file at
  `Payload/Library/Audio/Plug-Ins/VST3/K.vst3/Contents/MacOS/K` and another at `…/Components/K.component/Contents/MacOS/K`,
  each with an x86_64 slice (`LC_VERSION_MIN_MACOSX` `0x000A0D00`) and an arm64 slice (`LC_BUILD_VERSION` `0x000B0000`),
  and a stub `--pkg` file; `main` exits 0; `--out` holds `{asset, size, sha256, platform_key: "darwin-universal",
  min_os: "11.0", archs: ["arm64", "x86_64"], macho}` with `asset`, `size` and `sha256` equal to the stub pkg's; stdout is
  exactly two lines `arm64 x86_64\t<path>` in bytewise path order, with POSIX separators; a run with an unknown mode
  exits 2 with the usage line; a tree with no Mach-O exits 1 with `release-facts: E_FACTS_NO_MACHO:` and writes no
  `--out`. Threshold: exact JSON, exact lines, 1 usage exit, 1 named failure.
- **SC-025 (backfill production signer, FR-051).** Test `backfill production signer drives rsign2`, in
  `backfill-sign.test.js`, gated like SC-003 (`rsign --version` must be `rsign2 0.6.7`; skip with a reason when `CI` is
  unset, fail when it is set): `rsign generate -W` writes a temp key pair; `productionDeps(<temp work dir>).sign` signs a
  temp file with a C3 comment through the real `rsign sign -W -s … -t … -x … <file>` (stdin ignored); the returned text
  equals the `.minisig` file on disk, passes `minisign.verifyFile` against the generated public key, and its comment
  passes `contract.checkBinding`. Threshold: 1 verified, bound signature; not skipped on any CI leg.
- **SC-026 (tool form and codes, FR-076, FR-074).** Test `Phase 2 tools follow the tools/*.js form`, in
  `hygiene.test.js`: for each of `tools/minisign.js`, `tools/release-facts.js`, `tools/gen-release-manifest.js` and
  `tools/backfill-sign.js`, the first line is exactly `#!/usr/bin/env node`; the module exports a `main` function;
  `main([], io)` returns 2 and writes a stderr line starting `usage: node tools/<file>`; every `'E_[A-Z0-9_]+'` string
  literal in the file is a `CODES` key (so, with the unchanged §7 check, it has a `CONTRACT.md` row); and every FR-074
  code is a `CODES` key with class `none`. Threshold: 4 × 4 checks, 27 codes present.

- **SC-027 (secret-key decryption, FR-009a, FR-064, Q3).** Test `minisign.js decrypts the rsign2 secret-key KAT`, in
  `minisign.test.js`, with the FR-064 fixtures and the published test password: `decryptSecretKey` returns the key id of
  `kat.pub`; a signature made with the returned key over a small buffer passes `verifyBuffer` against `kat.pub`; the
  Tauri-style single base64 line of the same file decrypts to the same key; an unencrypted-form file (`kdf_alg` zero)
  decrypts with any password. Four rejection subtests: a wrong password → `E_MINISIG_PASSWORD` (the message contains
  neither password); one flipped byte of the encrypted `keynum_sk` → `E_MINISIG_PASSWORD`; a truncated file →
  `E_MINISIG_SECRET_KEY`; an unknown `kdf_alg` → `E_MINISIG_SECRET_KEY`. Three more subtests (plan PD-2, PD-3, SI-8):
  (a) `kat.key` with CRLF line ends decrypts to the same key id; (b) `kat.key` with its memlimit field (bytes 46-54,
  not covered by the checksum) raised to 2^30 + 1 → `E_MINISIG_SECRET_KEY`, with `crypto.scryptSync` wrapped in-process
  to record that it was not called; (c) an unencrypted-form key whose `sk[32..64]` is replaced and whose checksum is
  recomputed with `_internal.blake2b` → `E_MINISIG_SECRET_KEY` (public-half mismatch). Anchor subtests through
  `_internal` (plan PD-18, SI-7): `_internal.blake2b` with output length 64 equals `crypto.createHash('blake2b512')` on
  inputs of length 0, 1, 127, 128, 129 and 1000; the RFC 7693 Appendix A vector (BLAKE2b-512 of `"abc"`);
  `_internal.rawScryptParams(33554432, 1048576)` returns `{N: 32768, r: 8, p: 1}`. A last subtest, gated like SC-003 (`rsign
  --version` must be `rsign2 0.6.7`; skip with a reason when `CI` is unset, fail when it is set), runs `decrypt-key`'s
  output through the real `rsign sign -W` and `rsign verify` against `kat.pub`. Threshold: 1 key id match, 1 verified
  signature, 1 passthrough, 4 named rejections, subtests (a)-(c) as stated, 6 BLAKE2b length anchors, 1 RFC 7693
  vector, 1 scrypt-parameter known answer (plan SI-7, SI-8), 1 rsign2 signature accepted (not skipped on any CI leg).
- **SC-028 (decrypt-key CLI and cleanup, FR-009b, Q3).** Test `minisign.js decrypt-key writes only under RUNNER_TEMP and
  cleans up`, in `minisign.test.js`, `main` in-process with an `io.env` holding the fixture key, the test password and
  `RUNNER_TEMP` set to a temp dir exactly as `fs.mkdtempSync(path.join(os.tmpdir(), …))` returns it, not
  canonicalised, so the macOS leg passes through `/var` → `/private/var` and the Windows CI leg through the 8.3
  `RUNNER~1` name (plan PD-19, plan SI-8): exit 0, stdout exactly `decrypted: key id <KEYID>\n`, the `--out` file exists and
  `decryptSecretKey` of it (any password) returns the same key id; on POSIX legs (PENDING-CI) its mode is `0600`;
  neither stdout nor stderr contains the password, the secret's base64 or any key byte. Failures each leave no `--out`
  file and no temp file in the directory: wrong password → exit 1, stderr starts `minisign: E_MINISIG_PASSWORD:`; an
  `--out` outside `RUNNER_TEMP` → exit 1, `E_INPUT`; the password variable absent or empty → exit 1, `E_INPUT`; the
  key variable absent → exit 1, `E_INPUT`; an `--out` whose parent directory does not exist → exit 1, `E_INPUT`; an
  existing entry at `--out` → exit 1, `E_INPUT`; on POSIX legs (PENDING-CI; skipped as a subtest on win32) an `--out`
  through a symlinked directory inside `RUNNER_TEMP` that points to a second temp dir outside it → exit 1, `E_INPUT`,
  the second dir stays empty (plan PD-19, plan SI-8). Threshold: 1 success with the exact line, 7 named failures (plan
  SI-8), 0 leaks, 0 leftover files.
- **SC-029 (merge instructions and key contract, FR-061, FR-062).** Test `keys README states the secrets contract and
  the merge order`, in `workflow-shape.test.js`: `tools/feed-contract/keys/README.md` names `TAURI_SIGNING_PRIVATE_KEY`
  and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`, the path `tools/feed-contract/keys/krate-signing.pub`, the `tauri signer
  generate` step with a non-empty password, the Phase 11 pass-through of the password, and the merge order of FR-062
  (create the secrets, commit `krate-signing.pub`, then merge, with the `E_SIGNING_CONFIG` consequence stated), in that
  order; before the merge step it names the GitHub Environment `signing`, its deployment branch `main`, and that both
  secrets are environment secrets (plan PD-22, plan SI-10); it contains no instruction to use an empty password.
  Threshold: every string present, the three merge steps in
  order, 0 empty-password instructions.

---

## Safety

- **Roots and inputs.** No phase-2 tool touches a plug-in, preset or state root: their only inputs are paths on their
  command line, and their only outputs are `--out`, `--sums-out` and `--out-dir` (temp file + rename). Tests write only
  under `fs.mkdtemp(os.tmpdir())`, enforced by the Phase 1 write guard (FR-061 of Phase 1, FR-071 here).
- **Network.** No test or tool on this box calls github.com, api.github.com or the feed: `gh` runs only inside
  `backfill-sign.js`'s production `deps`, which tests replace with a fake, and only in the hosted-runner jobs the user
  dispatches. The hygiene scan keeps network modules and `fetch` out of every tool and test (FR-072).
- **Verified before use.** Every signature CI makes is verified twice, by `tools/minisign.js` against the committed
  public key and by `rsign verify`, plus `checkBinding` and `checkReleaseManifest`, before a release is created or a file
  is uploaded (FR-024, FR-045, FR-055). The backfill checks every downloaded asset's size and GitHub digest before
  signing it (FR-053), so it signs only the bytes GitHub already serves. Facts carry the SHA-256 of the asset they were
  measured from (FR-023). Nothing downloaded is executed: the pkg is only expanded with `pkgutil`, the tarball only
  listed and extracted into a runner temp dir, binaries only read.
- **Keys.** The real private key exists only as the password-protected `TAURI_SIGNING_PRIVATE_KEY` secret (password:
  `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`) and, from the decrypt step to the cleanup step, as an unencrypted
  mode-0600 file in `$RUNNER_TEMP` on an ephemeral GitHub-hosted runner, deleted by an `if: always()` step and never
  uploaded (FR-044, FR-050, FR-058). In the backfill, the job holding the secret on a dry run (`apply-dry`) has a
  read-only token; only `apply-live` can write (FR-058). No private key is generated into or committed to the repo; tests use throwaway keys
  in memory or in temp dirs (FR-060). The committed file is a public key.
- **Elevation.** None. No installer runs, no helper exists in this phase, nothing writes to a real VST3, Components or
  preset folder, ProgramData or the registry, on this box or on a runner.
- **No release by accident.** A dry run never tags, releases, attests or deploys (FR-047 to FR-049). A real run without
  signing configured fails before `Create Release` (D6), so no unsigned release is cut. The backfill's dry run is the
  default (`dry_run: true`), runs in a job whose token cannot write (FR-058), uploads nothing (FR-056), and its asset
  audit proves every release unchanged (FR-057a); its live run attaches files without `--clobber`, changes no existing
  asset and creates no release.
- **Strings never become paths unchecked.** Asset names are rebuilt with `assetNameFor` from a validated name and
  version, never taken from a release listing to name a file on disk; `gh release download` writes into a per-tag
  directory under the job's own temp dir.

## Edge cases

| Case | Phase 2 behaviour | Later owner |
|---|---|---|
| Interrupted download (backfill) | Size or digest mismatch: that release is reported (`E_BACKFILL_SIZE` / `E_BACKFILL_DIGEST`), nothing attached, the run continues and exits 3; a rerun picks it up again | — |
| Interrupted upload (backfill) | `release.json` goes last (D8), so a stop midway leaves a set without `release.json`; the next run reports it `E_BACKFILL_PARTIAL` instead of clobbering; a human deletes the strays | — |
| Disk full / unwritable output | Temp file + rename: the old output stays intact, exit 1 (FR-026) | — |
| File locked by a running DAW (Windows) | Not applicable (no install) | Phase 10 |
| Partial install | Not applicable. The Linux `payload` lets Phase 8/9 detect a partial tree | Phases 8-9 |
| Downgrade attempt | Each signature binds the asset's own version (C3); the backfill signs only each plugin's current stable release with that release's version, so an old asset can never carry a newer version's comment | Phase 7 (binding check), Phase 5 |
| Corrupt feed | Phase 2 writes no feed. A corrupt `release.json` cannot be uploaded: `checkReleaseManifest` runs before every write (FR-026) | Phase 3 (gen-feed degrades a bad manifest) |
| Offline | The tools need no network; only the dispatched CI jobs talk to GitHub through `gh` | Phase 5 |
| Feed `schema_version` 1 vs 2 | Unchanged: the live feed stays v1 until Phase 3; `release.json` is C2, read only by gen-feed | Phase 3 |
| Bundles that still say `0.8.0` | Not read: the backfill signs old releases' assets as they are and never parses `moduleinfo.json` | Phase 6 |
| A plugin binary that is arm64-only | Recorded as `darwin-aarch64` (D2); the manager on an Intel Mac shows it "not available for this Mac" | Phase 5 (roadmap lines 805-808) |
| Mach-Os in one pkg disagree on architectures | `E_FACTS_ARCH_MISMATCH`: the release job stops before signing; the backfill reports that release | — |
| A plugin `min_os` above the manager's macOS floor (15.0) | Recorded as measured; nothing here rejects it | Phase 5 (compatibility with the running OS) |
| An older release's tarball lacks a C4 item | `checkLinuxLayout` fails (`E_LAYOUT`); the release is reported and not signed | — |
| An immutable release | Reported `E_BACKFILL_IMMUTABLE` before any upload; the release job attaches everything in its one create call, which immutability permits (D8) | — |
| More than 1000 releases | `E_BACKFILL_LIST_TRUNCATED`, abort, nothing uploaded | — |
| Wrong or missing password secret | `decrypt-key` fails with `E_MINISIG_PASSWORD` (or `E_SIGNING_CONFIG` when the password secret is empty) before any signing; a real release fails before `Create Release`, a backfill dry run shows it before any live run (FR-009b, FR-044, D11) | — |
| Secret and committed public key do not match | Release job: verification fails before `Create Release`. Backfill: `E_SIGN_KEY_MISMATCH`, zero uploads (FR-055) | — |
| No key secret, no password secret, no public key, or only some of them | A real release fails with `E_SIGNING_CONFIG` before `Create Release`, naming the missing half and `keys/README.md`; nothing is tagged or uploaded; a dry run still works with its throwaway key (D6, FR-029) | — |
| A prerelease run of `release.yml` | Gets `release.json` like any release (C2 allows a suffix); the feed puts it in the `prerelease` sibling | Phase 3 |
| A stable release older than a newer prerelease (backfill) | The stable one is signed (Q9 via `selectReleases`); the prerelease is not the current release | — |
| `version.json` ahead of the newest release | The backfill reads the tag's own `version.json` (D12) | — |
| `release.yml` dispatched from a branch other than the default | The `release` job reads `version.json` / `setup.iss` from the dispatch ref and `Create Release` tags that same commit (`target_commitish: ${{ github.sha }}`, FR-047, D12): a branch dispatch tags a branch commit | — |
| A facts failure on a real `release.yml` run | The release fails before `Create Release`; no escape input exists (FR-047a, D6) | — |
| A facts job fails for one old release (backfill) | `facts` writes a `{tag, code, message}` entry to its facts report and continues; `apply` reports that release under the code (FR-053a, FR-054) | — |
| Asset names with a version the tag does not carry | `assetNameFor` names the expected file; a mismatch is `E_BACKFILL_ASSET_MISSING` / `E_ASSET_MISSING` | — |

## New components

| Path | Purpose |
|---|---|
| `tools/minisign.js` | minisign keygen (in memory), sign and verify of the `ED` format over `node:crypto`; public-key text; `verify` CLI |
| `tools/release-facts.js` | Mach-O arch set and minimum macOS from an expanded pkg; glibc floor from `objdump -T`; tarball vs extracted-tree listing check |
| `tools/gen-release-manifest.js` | `release.json` (C2) and `SHA256SUMS` from staged, signed assets and facts; prints the C3 comments for signing; the signing-configuration decision (FR-029) |
| `tools/backfill-sign.js` | `plan` / `download` / `facts` / `apply` over injected `gh`, `git`, `exec` and signer; asset audit |
| `.github/workflows/backfill-sign.yml` | One-time, manual, dry run by default: plan, macOS and Linux facts, `apply-dry` (read token) or `apply-live` (write token) |
| `tools/feed-contract/keys/README.md` | The public-key file contract, the user's key-creation step and the merge instructions (D4, D5, FR-061, FR-062) |
| `tools/feed-contract/keys/krate-signing.pub` | The real public key, committed by the user (FR-061); not created by this phase's implementation |
| `tools/feed-contract/tests/{minisign,release-facts,gen-release-manifest,backfill-sign,workflow-shape}.test.js` | SC-001 to SC-014, SC-022 to SC-025, SC-027 to SC-029 (SC-026 is a new test in the existing `hygiene.test.js`) |
| `tools/feed-contract/tests/helpers/macho.js` | Builds synthetic thin and fat Mach-O buffers for SC-004 |
| `tools/feed-contract/fixtures/phase2/` | KatPlugin plugin dir, `objdump -T` samples, nine-plugin backfill fixture and fake `gh` state; `kat-secret/`, the encrypted secret-key KAT (FR-064) |
| `specs/krate-manager-phase2/evidence/scope-check.js` | SC-020 |

## Traceability

| Roadmap statement | FR / SC |
|---|---|
| No new archives; the three assets downloaded verbatim (lines 340-343) | FR-081, SC-014, SC-020 |
| macOS `lipo -archs` and minimum OS from load commands; Linux glibc via `objdump -T` (lines 344-348) | FR-010 to FR-016, FR-041, FR-042, SC-004, SC-005, SC-018 |
| `gen-release-manifest.js` writes C2 with sizes, SHA-256, signatures, Linux payload from the tarball (lines 349-350) | FR-020 to FR-028, SC-006, SC-009 |
| CI signs with rsign2 0.6.7, `cargo install … --locked` (lines 352-357) | FR-043, FR-045, FR-058 |
| `tools/minisign.js` over `node:crypto`, `ED`, global signature, key id check, unencrypted keys only (lines 358-365) | FR-001 to FR-005, FR-007, FR-009 to FR-009b (reversed by Q3), SC-002, SC-027, SC-028, SC-023 |
| minisign.js checked against the KAT (lines 366-367) | FR-006, SC-001 |
| `SHA256SUMS`, one signature per asset with a C3 comment, upload, attestation, `make_latest: false` (lines 368-373), on every release | FR-029, FR-044 to FR-047a, FR-062, D6, D7, D10, SC-014, SC-018, SC-022 |
| `dry_run` with a throwaway key, artifacts only (lines 374-376) | FR-040, FR-044, FR-048, FR-049, SC-018 |
| Backfill: current stable release, same manifest and signing, `gh release upload` without `--clobber`, idempotent, facts from the downloaded assets (lines 377-387; Q10) | FR-051 to FR-059 (incl. FR-053a, FR-057a), D1, D8, D11, D12, SC-010 to SC-013, SC-019, SC-025 |
| Payload digest equals the extracted tarball's listing, `node --test` on Windows (lines 401-402) | FR-015, FR-023, SC-008. **Deviation:** Windows proves payload = the committed `.listing` (Phase 1 SC-009 ties that file to an extracted tree on POSIX); the extracted-tarball comparison itself is PENDING-CI on `node-test (ubuntu-latest \| macos-latest)` and the `release.yml` dry run's `Record Linux facts` step, because `listingFromDirectory` cannot run on win32 (Phase 1 D4) |
| Backfill dry run lists nine plugins × three assets, C2-valid manifests, uploads nothing (lines 409-410) | SC-019 (PENDING-CI until the merge, FR-059a; with the D11 precondition: real secret and committed public key, an addition) |
| Private key is a user-created secret; tests use throwaway keys (lines 388-389; Key decision 4) | FR-060 to FR-062, FR-064, D4, D5, SC-015, SC-027 to SC-029 |
| Measurable: tamper fails, binding check, interop both ways, payload equals extracted listing, backfill fixture outcomes, dry runs (lines 392-413) | SC-001 to SC-013, SC-017 to SC-019 |
| Does NOT: change installers, regenerate the feed, publish, codesign, change `ci.yml` (lines 415-416) | FR-049, FR-080 to FR-082, SC-020 |
| ROSTER: no plugin names in tools or the backfill | FR-028, FR-051, FR-059, FR-072, SC-014, SC-015 |
| Q9: the current release is the newest non-prerelease | FR-052, FR-075, SC-010 |
| Cross-Cutting: never touch the real system; no network in tests; parallel tests; zero secrets in the repo | FR-060, FR-070 to FR-073, SC-015, SC-021 |
| Cross-Cutting "Naming": Node tools follow `tools/*.js` (flat file, `#!/usr/bin/env node`, usage line, `node --test`) (lines 988-989) | FR-076, SC-026 |

**Additions to the roadmap** (to be recorded when this spec is encoded; none changes a ruled decision):

| Addition | Roadmap text it extends | Why |
|---|---|---|
| `tools/release-facts.js`, a fifth new Node tool | New-file list (line 211), Repository Layout (lines 234-240) | The roadmap leaves the facts method to the spec (line 386); one tool shared by the release jobs and the backfill keeps D1's "one code path" |
| `tools/feed-contract/keys/krate-signing.pub` (user-committed) and `keys/README.md` | Repository Layout (lines 234-240); Phase 7 "embedded public key" (line 591); Phase 11 (line 784 on) | The one place CI verifies against (D5); Phases 7 and 11 must compile in the key from this path, so the roadmap's Layout and those phases must name it |
| A real release without signing configured, or with a facts failure, fails (D6, Q1) | Phase 2 `release` job (lines 368-373) | Not an addition in behaviour, a reading of the unconditional roadmap text; recorded because it blocks plugin releases until the user's key step (merge instructions, FR-062) |
| A password-protected signing key, a second secret `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`, and `tools/minisign.js` secret-key decryption (Q3) | "Unencrypted keys only" (line 365); Phase 11 (line 784 on) | rsign2 0.6.7 reads a password only from a terminal; Phase 11 passes the same password to Tauri through `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`; the encrypted-key KAT fixture is the one committed secret-key file (FR-064) |
| `target_commitish: ${{ github.sha }}` on `Create Release` (Q5) | Phase 2 `release` job (lines 368-373) | The tag is always the commit that was built and described |
| The backfill dry run needs the real secret and committed key (D11) | Backfill dry-run outcome (lines 409-410) | Proves the real key before the live run |

## Open questions

None. Every question the clarify scan raised is decided (Clarifications, 2026-10-30); D1 answers the one point the
roadmap defers to this spec (the facts derivation, roadmap lines 384-386).

## Merge instructions

The user does these in this order (FR-062); the first two are why D6 blocks releases until they are done.

1. Create the GitHub Environment `signing` with the deployment-branch policy "Selected branches and tags: `main`", and
   in it the environment secrets `TAURI_SIGNING_PRIVATE_KEY` (the key file content from `tauri signer generate` with a
   non-empty password) and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` (that password) (plan PD-22, plan SI-10).
2. Commit `tools/feed-contract/keys/krate-signing.pub` on the branch.
3. Only then merge to main. From the merge on, every real `release.yml` run fails with `E_SIGNING_CONFIG` until steps 1 and 2
   are done, and fails on any facts failure (Q1, FR-047a).
4. After the merge, dispatch SC-018 (the `release.yml` dry run) and SC-019 (the `backfill-sign.yml` dry run, FR-059a).
## Review notes

Resolutions of the 2026-10-30 review, where a choice was made between offered options:

- **D6 / FR-044 (fidelity, major) and the signing-branch testability issue (major):** took option (a), no unsigned
  branch, which matches roadmap lines 368-373 and the header's "`release.json` on every release". The decision moved
  into `gen-release-manifest.signingConfig` (FR-029) instead of a new `tools/signing-config.js`, so the change adds no
  sixth tool to the roadmap's list; SC-022 covers every branch.
- **FR-058 permissions (testability, major; reality, minor):** split into `apply-dry` / `apply-live`, confirmed against
  the GitHub contexts and workflow-syntax pages (External facts). Keeping one always-write job was rejected because the
  default dry run would hold a write token next to the real secret.
- **FR-074 code table (testability, major; reality, major):** took option (a): rows in the §7 table, CLI lines and exit
  codes in `## 9. Phase 2 tools`, so the Phase 1 check stays unchanged. The suggested "subtest that fails when a Phase 2
  code has no row" is met by the unchanged §7 check plus SC-026's "every code literal is a `CODES` key".
- **FR-051 `fetch` export (reality, major):** renamed to `download` rather than exempting the file from `FETCH_RE`, so
  the offline scan keeps full force on the one tool that talks to GitHub.
- **FR-073 count (testability, major):** the threshold is the exact top-level plan count (46: the 24 Phase 1 top-level
  tests plus the 22 SC-named ones, FR-073; it read 43 here before the plan's recount, plan SI-5) plus a name check; the
  measured `1..24` / `# tests 350` at `1dec384c` is recorded in Existing repo pieces.
- **D12 / `target_commitish` (reality, minor):** D12 reworded to the dispatch ref; the user then ruled the fix in
  (Clarifications Q5): `Create Release` pins `target_commitish: ${{ github.sha }}` (FR-047), so the tag is always the
  commit that was built and described (plan SI-5).
- No issue was rejected.
