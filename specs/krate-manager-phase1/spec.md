# Feature Specification: Krate Manager Phase 1 — Feed v2 Schema and Generator

**Spec slug:** `krate-manager-phase1`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 1 (lines 251-329); Decisions log Q9 (line 32), ROSTER (line 33),
STRATEGY (line 23), Q10 (line 34); Architecture "Feed and artifact model" (lines 126-146); Repository Layout (lines 211-245);
Cross-Cutting Constraints (lines 950-992); Review notes "Blocker (contracts in Phase 1)" and "Blocker, known-answer fixture" (lines 1003-1009)
**Depends on:** nothing. Phases 2, 3, 5, 7 and 8 depend on this phase's contracts and fixtures (roadmap Dependency Graph, lines 897-931)
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-10-29 · verified against `feat/krate-manager` at `84c6aab1`

---

## Overview

Krate Manager will install, update and remove Krate Audio plugins from a feed. Before any app code exists, Phase 1 fixes
**every contract** that later phases build or test against, and builds the generator that will produce the feed. It
delivers four contracts and the code around them, all in `tools/feed-contract/`:

- **C1**, the `versions.json` **schema_version 2** schema and its prose contract;
- **C2**, the per-release `release.json` schema;
- **C3**, the trusted-comment grammar that binds each minisign signature to one plugin, version, platform and asset, with
  an `rsign2` known-answer fixture;
- **C4**, the Linux `tar.gz` layout and its canonical listing, whose digest the feed carries as `payload`.

It also adds `tools/gen-feed.js`, a pure generator from files to `versions.json`. A legacy mode reproduces today's v1 output
byte for byte. The Q9 rule for the v1 `version` key lives in this generator and nowhere else. Phase 1 adds one
guard test (two cases) to the in-plugin parser's suite, and a CI lane, `feed-tools.yml`, that runs the Node tests on
three OSes.

Nothing in this phase publishes, signs with a real key, edits `release.yml`, or changes plugin source code. The nine shipped
plugins keep reading the live v1 feed. The only test-code change is in `plugins/shared/tests/`.

---

## Scope

**In scope (roadmap Phase 1 components, lines 258-307):**

1. C1: `tools/feed-contract/versions.v2.schema.json` (JSON Schema 2020-12) and `tools/feed-contract/CONTRACT.md` (the prose
   contract: field, type, required, who reads it).
2. C2: `tools/feed-contract/release.schema.json`.
3. C3: the grammar in `CONTRACT.md` plus its parser/formatter in `tools/feed-contract/contract.js`, and the known-answer
   fixture under `tools/feed-contract/fixtures/kat/`.
4. C4: the Linux layout rule and canonical listing in `CONTRACT.md`, plus `tools/feed-contract/listing.js` (listing from
   entries, from a `.tar.gz`, and from a POSIX directory tree; digest; layout check).
5. `tools/gen-feed.js` with its v2 mode, its legacy-compat mode and its v1 projection.
6. Fixture sets for C1-C4 and the generator (`tools/feed-contract/fixtures/`), read in place by Phases 2, 3, 5, 7 and 8.
7. One `TEST_CASE` with two `SECTION`s in `plugins/shared/tests/test_update_checker.cpp` (a v2 document and a
   `schema_version: 99` document), plus the FR-052 temp-dir guard on the three existing `[update]` cases (test-only, same file).
8. `.github/workflows/feed-tools.yml` (`node --test` on `ubuntu-latest`, `macos-latest`, `windows-latest`).
9. Supporting repo edits these require: the `ajv` devDependency in a tracked `tools/feed-contract/package.json` /
   `package-lock.json`, the anchored root ignore lines in `.gitignore`, and a `.gitattributes` rule for the fixtures
   (FR-062, FR-063) (Clarifications 2026-10-30 Q-D).

**Non-goals (owned by later phases):**

| Not in Phase 1 | Owner |
|---|---|
| Any edit to `.github/workflows/release.yml` (SHA256SUMS, `.minisig`s, `release.json` upload, `dry_run`, `make_latest`) | Phase 2 |
| `tools/gen-release-manifest.js`, `tools/minisign.js`, `backfill-sign.yml`, any real signature | Phase 2 |
| `feed.yml`, replacing the inline generator at `release.yml:645-685`, any Pages deploy | Phase 3 |
| The Rust feed client, version model, platform-key resolution at runtime | Phase 5 |
| Detection, download, verification in Rust, extraction, installs | Phases 6-10 |
| `manager/latest.json` and the manager's own release | Phase 11 |
| Changing the in-plugin parser (`update_checker.cpp`) or any plugin header | never in this roadmap (roadmap line 52-54, "Additive") |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 1 does with it |
|---|---|---|
| `.github/workflows/release.yml:645-685` | Today's feed generator: inline Node over `ls plugins/*/version.json` (`:653`), tag = `git tag -l "<name.toLowerCase()>/v*" --sort=-v:refname \| head -1` (`:662-666`), plugin skipped without a tag (`:668-671`), entry `{version, download_url: data.url, release_notes: ""}` (`:674-678`), `{schema_version: 1, plugins}` (`:681`), written as `JSON.stringify(output, null, 2) + "\n"` (`:683`) | Reproduced exactly by `gen-feed.js --legacy` (FR-040). Not edited. |
| `.github/workflows/release.yml:76` | Tag `${PLUGIN}/v${VERSION}`, with `PLUGIN` the lowercase directory name | The tag grammar of C1/C2 (`<lowercase name>/v<version>`). |
| `.github/workflows/release.yml:45-48`, `:521` | `prerelease` input, passed to `softprops/action-gh-release` | Source of the release list's `isPrerelease`; drives Q9. |
| `.github/workflows/release.yml:524-526` | Asset names `<Name>-<ver>-Windows-x64.exe`, `<Name>-<ver>-macOS.pkg`, `<Name>-<ver>-Linux-x64.tar.gz` | The asset-name rule of C2/C3 (FR-022). |
| `.github/workflows/release.yml:285-312`, `:306`, `:321` | macOS staging: `<Name>.vst3`, `<Name>.component`, optional AUv3 app; `preset_subdir` read from `version.json`; pkg identifier `.identifier // "com.krateaudio.<name lowercased>"` | `install.vst3_bundle`, `install.au_component`, `install.preset_subdir`, `install.macos_pkg_id` value rules (FR-008). |
| `.github/workflows/release.yml:379-395`, `:402-403` | Linux staging (`<Name>.vst3`, `presets/`, `README.txt`, `CHANGELOG.txt`), then `cd staging; tar -czvf ../<Name>-<ver>-Linux-x64.tar.gz *` (no wrapping folder) | The C4 root layout (FR-033). |
| `.github/workflows/release.yml:462-463` | CHANGELOG section extraction: lines after `## [VERSION]` up to the next `## [` | The `notes_markdown` source rule (FR-044). |
| `plugins/shared/src/update/update_checker.cpp:222-262` | `UpdateChecker::parseResponse`: needs `plugins` object, `plugins[<name>]` object, `version` string; reads `download_url` / `release_notes` only if strings; never reads `schema_version`; any exception gives an empty result | The compatibility contract. Not edited. |
| `plugins/shared/src/update/update_checker.h:77` | `parseResponse` is **private** | The guard test reaches it the way the existing tests do: `MockUpdateChecker` + `checkForUpdate(true)` (FR-050). No header change. |
| `plugins/shared/src/update/version_compare.h:24-51` | `SemVer::parse` accepts only `MAJOR.MINOR.PATCH` (trailing whitespace allowed), each part read with `std::from_chars` into `uint32_t` (`:19-21`, `:28`, `:34`, `:40`), so a part above 4294967295 fails, and leading zeros parse; a prerelease suffix such as `1.1.0-beta.1` returns `nullopt` | Why the v1 `version` must be a plain three-part version with bounded parts (FR-004). Today's generator would publish a prerelease tag's version, and every shipped banner would then go silent. |
| `plugins/shared/src/update/update_checker.cpp:297-311`, `plugins/shared/src/platform/preset_paths.cpp:53-79` | `saveState` writes `update_state.json` under `getAppSettingsDirectory(pluginName)`: `%APPDATA%\Krate Audio\<P>` (Windows), `$HOME/Library/Application Support/Krate Audio/<P>` (macOS), `$XDG_CONFIG_HOME/krate-audio/<P>` or `$HOME/.config/krate-audio/<P>` (Linux) | The new cases redirect it to a temp dir (FR-052). Observed: the **existing** `[update]` cases (`TEST_CASE`s at `test_update_checker.cpp:79`, `:146`, `:181`) use `pluginName = "Iterum"` (`:80`, `:147`, `:182`) and, in the JSON-parsing case, `"Ruinae"` (`:113`), and so write the real `%APPDATA%\Krate Audio\Iterum\update_state.json` and `%APPDATA%\Krate Audio\Ruinae\update_state.json` on this box. It holds `"dismissed_version": "0.14.0"`, the value `test_update_checker.cpp:200` dismisses. Ruled in Clarifications Q2: the FR-052 guard is applied to those three cases too (FR-052, SC-021). |
| `plugins/shared/tests/test_update_checker.cpp:15-36`, `:79-144` | `MockUpdateChecker` (overrides `fetchJson`), `waitForResult`, `TEST_CASE("UpdateChecker - JSON parsing", "[update]")` | Pattern the new case follows. |
| `plugins/shared/tests/CMakeLists.txt:31` | `test_update_checker.cpp` is in `shared_tests` | No CMake change. |
| `plugins/iterum/src/update/iterum_update_config.h:12` | Endpoint `https://rolandzwaga.github.io/krate-audio/versions.json` (same in all nine) | Unchanged. Observed 2026-10-29: that URL answers `301` to `https://krateaudio.com/versions.json`, which serves the v1 file with `Cache-Control: max-age=600` (`curl -sIL`). This matters to Phase 3 and Phase 5, not here. |
| `plugins/*/version.json` (nine) | `name`, `version`, `url`, optional `preset_subdir` (Membrum `"Kits"`), no `identifier` today | Generator input: feed key = `name`, `download_url` = `url` (as `release.yml:676`). |
| `plugins/<p>/installers/windows/setup.iss:18` (Iterum `:27`) | `AppId={{<GUID>}` (Inno escapes `{` by doubling, so the AppId is `{<GUID>}`), all GUIDs upper-case hex | `install.windows_uninstall_key` = `{<GUID>}_is1` (FR-008). The key name is the AppId with `_is1` appended (per_os_install_mechanics.md line 74). gen-feed reads this file and requires the manifest's key to equal `{<GUID>}_is1` (FR-043 check 2, SC-025). |
| `tools/check-bundle.js:28-30`, `:50-55` | JSON5 comma strip; class `Version` equals `version.json` or starts with `"<ver>."` | Not used by Phase 1. Its rule is why C4 requires `Contents/Resources/moduleinfo.json` in the bundle (FR-033). |
| `.github/workflows/ci.yml:3-23` | Triggers only on `push` and `pull_request` to `branches: [main]`; path filters list `plugins/**` but no `tools/**` | The C++ test edit triggers `ci.yml`, but only on a pull request to `main` or a push to `main`, never on a feature-branch push. The Node tools need their own lane (FR-060). |
| `.github/workflows/ci.yml:55`, `:85-86`, `:623`, `:1127`, `:1404-1406`, `:1441-1443` | `detect-changes` output `shared` (filter `plugins/shared/**`); `build-macos` and `build-linux` add `shared_tests` to their targets when it is true; jobs `test-macos` (name `macOS tests (shard ${{ matrix.shard }})`) and `test-linux` (`Linux tests (shard ${{ matrix.shard }})`) run the built suites through `tools/ci-test-shard.js` | Where the macOS/Linux evidence for SC-016/SC-017 comes from (PENDING-CI, needs a PR to `main`). |
| `.github/workflows/ci.yml:164-166` | `actions/setup-node@v4`, `node-version: '20'` | `feed-tools.yml` uses the same action. The Node version is decided in FR-060. |
| `package.json`, `package-lock.json` (lockfile v3) | Root devDependencies `fast-xml-parser`, `saxon-js`, `xslt3`; no JSON Schema validator. Both are **gitignored** (`.gitignore:93-94`, unanchored, so they hide every `package.json` in the tree) | Untouched and untracked; `ajv` goes to `tools/feed-contract/package.json` instead (FR-062) (Clarifications 2026-10-30 Q-D). |
| `.gitattributes` | LF pins for generated artifacts. This checkout has `core.autocrlf=true` and `core.symlinks=false` (`git config`, 2026-10-29) | Gains the fixture rule (FR-063): byte-exact fixtures would otherwise be CRLF-converted on Windows checkout, which breaks the KAT signature and every digest. |
| `tools/run-gate.js`, `tools/header-consumers.js:11-33` | Sharded Catch2 gate; consumer finder over `git status` of `dsp/include dsp/src plugins/shared tests/test_helpers` | The C++ gate (SC-016). |

Real data read this session (2026-10-29) for the snapshot fixture (FR-071):
- `git ls-remote --tags origin`: 88 tags. `gh release list --limit 200 --json tagName,isPrerelease,isDraft,isLatest,publishedAt`:
  88 releases with the same tag names, **no prerelease, no draft**. `isLatest` is `innexus/v1.0.4`.
- The live `versions.json` (fetched through the redirect) is exactly what `release.yml:645-685` produces from those tags:
  nine plugins, v1 keys only.
- `gh release view vorago/v1.0.2 --json url,assets`: release page `https://github.com/rolandzwaga/krate-audio/releases/tag/vorago/v1.0.2`,
  asset URLs `https://github.com/rolandzwaga/krate-audio/releases/download/vorago/v1.0.2/<asset>`. The slash in the tag is literal,
  not `%2F`, which settles feed_and_ci.md's "Hosting" gap.
- `Vorago-1.0.1-Linux-x64.tar.gz` (listed with `tar -tvzf`, not extracted, then deleted): GNU tar format (magic `ustar  \0`),
  61 entries, root `CHANGELOG.txt`, `README.txt`, `Vorago.vst3/`, `presets/`; bundle `Contents/x86_64-linux/Vorago.so`,
  `Contents/Resources/{moduleinfo.json,editor.uidesc}`; files `0644` (the `.so` included, so there are no exec bits), directories `0755`, preset names with spaces,
  longest path 46 bytes.

## External facts used (verified this session)

| Fact | Source |
|---|---|
| minisign signature file: `untrusted comment: …` / `base64(<alg> \|\| <key_id> \|\| <signature>)` / trusted comment / `base64(<global_signature>)`; `alg` `Ed` (legacy) or `ED` (hashed, `ed25519(Blake2b-512(file))`); key id 8 bytes; `global_signature = ed25519(<signature> \|\| <trusted_comment>)`; "New implementations must use the hashed signature format"; downloads are version 0.12 | https://jedisct1.github.io/minisign/ ("Signature format", "Public key format"), minisign 0.12 |
| The trusted-comment line prefix is exactly `trusted comment: ` (17 bytes) and the comment is the rest of the line, untrimmed; lines are split with `str::lines()`; alg bytes `0x45 0x44` = `ED` | `minisign-verify` source, https://raw.githubusercontent.com/jedisct1/rust-minisign-verify/master/src/lib.rs (`Signature::decode`, `trusted_comment()`) |
| `rsign2` newest version 0.6.7, published 2026-09-25 | https://crates.io/api/v1/crates/rsign2 |
| `rsign` 0.6.7 CLI: `generate -p <pub> -s <sec> -W` (passwordless), `sign -s <sec> -W -t "<trusted comment>" <file>` (writes `<file>.minisig`; `-H` "ignored (for backwards compatibility only)", so it always writes `ED`), `verify -p <pub> <file>` (legacy `Ed` only with `-l`) | `rsign --help` / `rsign <cmd> --help`, rsign2 0.6.7 installed on this box (`rsign --version`) |
| `rsign sign` on Windows writes the `.minisig` with **CRLF** line endings (throwaway key in the scratchpad, deleted; `od -c`) and `rsign verify` accepts it | measured 2026-10-29 |
| `ajv` latest 8.20.0 (modified 2026-04-24); `require("ajv/dist/2020")` exports `Ajv2020` with meta-schema `https://json-schema.org/draft/2020-12/schema` | `npm view ajv` 2026-10-29; https://unpkg.com/ajv@8.20.0/dist/2020.js |
| Ajv ships no `format` validators ("they can be added with ajv-formats plugin") | https://ajv.js.org/json-schema.html |
| `node --test`: each test file runs in its own child process by default (`--test-isolation` default `'process'`), with up to `os.availableParallelism() - 1` files at once (`--test-concurrency`) | https://nodejs.org/docs/latest-v24.x/api/cli.html |
| Node v24.7.0 on this box | `node --version` |
| On Node v24.7.0, `node --test tests/` (a directory argument) does **not** run the files in it: it loads the directory as a module and reports one failing test, `tests` (`'test failed'`). `node --test "tests/*.test.js"` (quoted glob, expanded by Node, not the shell) runs them. `--test-reporter=tap` ends with `# tests N` / `# pass N` / `# fail N` lines, and `N` counts subtests (two top-level tests, one with two subtests, gave `# tests 4`) | measured 2026-10-29 in the scratchpad (Git Bash, non-TTY), files deleted |
| `rsign` 0.6.7: a secret-key file starts `untrusted comment: rsign encrypted secret key` (also with `-W`) and its second line decodes to bytes beginning `EdScB2` (signature alg `Ed`, KDF `Sc`, checksum `B2`); a signature file starts `untrusted comment: signature from rsign secret key` and its second line decodes to 74 bytes beginning `ED` | measured 2026-10-29, throwaway key in the scratchpad, deleted; secret-key layout `<sig_alg> \|\| <kdf_alg> \|\| <cksum_alg> \|\| …` per https://jedisct1.github.io/minisign/ ("Secret key format") |

---

## Decisions taken in this spec (open to challenge in the clarify scan)

These close points the roadmap names but does not pin down. Each is binding unless the grill overturns it.

- **D1, schema openness.** C1 objects stay **open** (no `additionalProperties: false`): every feed change must be additive
  (roadmap line 54) and older readers must keep accepting newer feeds. The **generator** is strict about what it emits,
  and its golden outputs catch typos. C2 (`release.json`, read only by Krate's own tools) is **closed**.
- **D2, validator.** Validation uses `ajv` 8.20.0 (`Ajv2020`, `strict: true`, `allErrors: true`), pinned exactly as a root
  devDependency. The schemas use `pattern`, never `format`, so `ajv-formats` is not needed. A hand-written validator would
  duplicate the schema in code, and the Rust side (Phase 5) can read the same schema files.
- **D3, mode normalisation in C4.** The listing records `0755` or `0644` for a file (by `mode & 0o111`), `0755` for every
  directory and `0777` for every symlink. Raw modes vary with the runner's umask, and extraction applies the extractor's
  umask. Windows has no POSIX modes at all. The exec bit is the only mode fact Phase 8 checks ("keep exec bits").
- **D4, listings on Windows.** NTFS cannot carry modes or (with `core.symlinks=false`) symlinks. The listing fixtures are
  therefore committed `.tar.gz` files, listed from their tar headers on every OS. A listing from an extracted directory tree runs only on the
  macOS and Linux legs (PENDING-CI). Phase 2 needs the tar-header reader anyway ("computed from the tarball itself",
  roadmap line 339).
- **D5, `notes_markdown` source.** It is the `## [<version>]` section of `plugins/<p>/CHANGELOG.md`, cut with the same rule
  as `release.yml:462-463` (feed_and_ci.md line 64: "optional inline copy of the CHANGELOG section").
- **D6, `release_notes_url`, `pub_date`.** They come from the release list (`publishedAt`) and the `--repo` argument
  (`https://github.com/<repo>/releases/tag/<tag>`, the form `gh release view` returns), not from `release.json`.
- **D7, signature text.** A `signature` value in C1/C2 is the full four-line `.minisig` text with **LF** line endings and
  a final LF. Readers accept CRLF in a `.minisig` file. Writers normalise to LF, which changes no signed byte. Base64
  wrapping (Tauri's `.sig` style) is not used for plugin assets.
- **D8, the asset file name of a feed `platforms` entry** is the last path segment of its `url`. The feed carries no
  separate `name`; Phase 7 compares the C3 comment's asset name with it.
- **D9, `feed-tools.yml` triggers.** Push on **any** branch and `pull_request`, both path-filtered, plus
  `workflow_dispatch`. The user's push of a feature branch is how the Node PENDING-CI rows get their evidence; `ci.yml`'s
  `branches: [main]` would never run them before a PR or merge. The C++ rows (SC-016, SC-017) are the exception: their
  macOS/Linux evidence is `ci.yml`'s, so it arrives only with a pull request to `main` (or a push to `main`).

---

## Clarifications

### Session 2026-10-29

- Q: Which rule decides a release's channel, given that a real prerelease has a plain three-part version? → A: Drop
  `channel` from C2 (`release.json`) entirely. The GitHub release list's `isPrerelease` is the only source, and gen-feed
  writes the C1 `channel` from it; a SemVer suffix stays optional; FR-017's channel rule and FR-043's channel match are
  removed; promoting a prerelease is unticking the GitHub flag and the uploaded `release.json` stays valid.
  [FR-015, FR-017, FR-018, FR-043, SC-003, SC-022]
- Q: Do the existing `[update]` cases keep writing the real Iterum and Ruinae `update_state.json`? → A: No. Apply the
  FR-052 temp-dir guard (`APPDATA` / `HOME` / `XDG_CONFIG_HOME` to a temp dir) and the pre-write `REQUIRE` to the three
  existing `[update]` cases too, a test-only edit in the same file, so no `shared_tests` run writes the real per-user file.
  [FR-052, SC-016, SC-021]
- Q: How far does one plugin's bad manifest reach? → A: It degrades only that plugin to a v1-only entry (v1 keys kept,
  `platforms` and `install` dropped), prints a named warning on stderr, writes the feed, then exits with the distinct
  non-zero code 3, so `feed.yml` can deploy the safe feed and still turn the run red. Input errors that affect v1 keys
  (`E_TAG_FORMAT`, `E_V1_TYPE`, `E_PLUGIN_NAME`) still abort with exit 1 and write nothing. [FR-046, FR-049, SC-010, SC-023]
- Q: Which `version.json` does FR-043's `preset_subdir` / `macos_pkg_id` cross-check compare against? → A: The check runs
  only when the current `version.json` `version` equals the manifest's `version`; for a plugin whose `version.json` is
  ahead of its newest release it is skipped. [FR-043, SC-024]
- Q: How is `windows_uninstall_key` protected from a copied AppId? → A: Both. (1) gen-feed reads each plugin's
  `installers/windows/setup.iss`, extracts the AppId GUID and requires `windows_uninstall_key` to equal `{GUID}_is1`
  (`E_MANIFEST_MISMATCH` otherwise), and the snapshot fixture copies the nine `setup.iss` files. (2) `contract.js`
  `validateFeed` rejects two plugins sharing `vst3_bundle`, `au_component`, `windows_uninstall_key` or `macos_pkg_id` with
  the new code `E_INSTALL_DUPLICATE`, and Phase 5's Rust feed client inherits the rule. The nine AppIds are unique today
  (measured). [FR-014, FR-043, FR-045, FR-055, FR-071, SC-025, SC-026]
- Q: What does `notes_markdown` carry? → A: One CHANGELOG section, the entry's own version, uncapped, a string. A
  multi-version view can come later as a separate additive key. [FR-003, FR-044, SC-027]

### Session 2026-10-30

- Q: What does gen-feed do with a `release.json` that is not valid JSON, and with two manifest files for the same `tag`?
  → A: Degrade, as in the Q3 ruling of 2026-10-29: WARN on stderr, exit 3, the feed is still written. Two manifests
  with the same `tag` raise the new code `E_MANIFEST_DUPLICATE`; an unparseable file's WARN names the file, because no
  plugin can be named from it. Not abort. [PD-8]
- Q: Does Phase 1 regenerate `specs/INDEX.md`? → A: Yes. Regenerate it in this change (`node tools/gen-specs-index.js`)
  and add `specs/INDEX.md` to the FR-070 path list and SC-020, so ci.yml's "Generated Artifacts In Sync" job stays
  green. [PD-9]
- Q: How are the four `E_INSTALL_DUPLICATE` fields covered, given that `vst3_bundle` and `au_component` duplicates always
  fail FR-017's name rule first? → A: Amend FR-059: the gen-feed duplicate cases cover `windows_uninstall_key` and
  `macos_pkg_id`; add a case `degrade-copied-bundle-name` in which only the copying plugin degrades, with
  `E_MANIFEST_SCHEMA`; all four fields stay covered at `validateFeed` level by SC-026. No test-only seam into gen-feed.
  [FR-059 dup]
- Q: The root `package.json` and `package-lock.json` are gitignored (unanchored lines that hide every `package.json`),
  so `feed-tools.yml`'s `npm ci` would find nothing on a fresh checkout. Where does `ajv` live? → A: Its own tracked
  package, `tools/feed-contract/package.json` + lock, installed in CI with `npm ci --prefix tools/feed-contract`; the
  `.gitignore` lines are anchored to `/package.json` and `/package-lock.json`, so the user's local root file stays
  untracked (and Phase 4's `apps/krate-manager/package.json` can be tracked); the root loses its `ajv` line. Amends
  FR-060, FR-062, FR-070 and SC-020. [Q-D, compliance FR-060/FR-062/SC-018]

## Functional Requirements

### A. C1 — `versions.json` schema_version 2 (roadmap lines 259-273, 126-136)

- **FR-001** `tools/feed-contract/versions.v2.schema.json` is a JSON Schema 2020-12 document
  (`$schema: "https://json-schema.org/draft/2020-12/schema"`) with `$id` `https://krateaudio.com/schemas/versions.v2.schema.json`. The
  `$id` is an identifier, never fetched. A document must have `schema_version` `const 2` and `plugins` (object);
  `manager` is optional.
- **FR-002** Plugin keys (`propertyNames`) match `^[A-Z][A-Za-z0-9]{0,63}$`, the shape of every `version.json` `name`
  today. A plugin key is also never a Windows reserved device name (`CON`, `PRN`, `AUX`, `NUL`, `COM0`-`COM9`,
  `LPT0`-`LPT9`), in any case, with or without an extension (a `not` clause on the `reservedStem` pattern, so `Nul` fails)
  (plan PD-14). The schema enumerates no plugin names (ROSTER, line 33): a fixture with an invented tenth plugin validates.
- **FR-003** Every plugin entry requires the three v1 keys at their v1 path and type (`update_checker.cpp:228-256`):
  `version` (string), `download_url` (string), `release_notes` (string, `const ""`, roadmap line 135). The entry also
  requires `tag`, `pub_date`, `channel` (`const "stable"`) and `release_notes_url`. `notes_markdown` (string, no `maxLength`, never an array; FR-044) is optional.
  `platforms` and `install` are optional, but present together or absent together (`dependentRequired` both ways). An entry
  without them is a **v1-only entry**, listed by the manager as "not available through the manager" (ROSTER).
- **FR-004** `version` matches `^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$`. That is a **subset** of
  what `SemVer::parse` accepts (`version_compare.h:24-51`): no whitespace, no leading zeros (which `parse` would accept),
  and at most 9 digits per part, so every accepted part fits the `uint32_t` that `std::from_chars` fills (`:19-21`,
  `:28-40`; a part above 4294967295 would make `parse` fail and silence every shipped banner). A prerelease or build
  suffix is invalid in the v1 key. The same bounded core (`<core>` below) is used wherever this spec says "version
  pattern" or "SemVer": FR-005, FR-010, FR-015, FR-020 and the generator's tag parse (FR-042), so a 10-digit part is
  rejected everywhere (`E_TAG_FORMAT` in the generator, `E_TC_VERSION` in C3).
- **FR-005** `tag` matches `^[a-z0-9]+/v<version-pattern>$` (the `release.yml:76` form). `pub_date` matches the RFC 3339 UTC
  form `^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$` (what `gh` emits as `publishedAt`). Every URL field
  (`download_url`, `release_notes_url`, `platforms.*.url`, `manager.latest_json_url`) matches `^https://[^\s]+$` and has a
  `not` clause rejecting any value that contains `/releases/latest/` (roadmap line 139-141).
- **FR-006** `platforms` is an object. Its keys match `^(windows|darwin|linux)-[a-z0-9_]+$`, so a future architecture is
  additive. The five keys this contract defines are `windows-x86_64`, `darwin-aarch64`, `darwin-x86_64`, `darwin-universal`
  and `linux-x86_64` (roadmap line 261). Each entry requires `url`, `size` (integer, `1 ≤ size < 2147483648`, the
  Releases 2 GiB per-file limit, feed_and_ci.md "Hosting"), `sha256` (`^[0-9a-f]{64}$`), `signature` (FR-007) and
  `format` (`inno-exe` | `pkg` | `tar.gz`). The format is fixed per key: `windows-*` → `inno-exe`, `darwin-*` → `pkg`,
  `linux-*` → `tar.gz`. `min_os` matches `^[0-9]+(\.[0-9]+){1,3}$`. It is **required** for `darwin-*` (macOS product
  version) and `linux-*` (minimum glibc version), the two facts Phase 2 records (roadmap lines 340-343), and optional
  for `windows-*`, which no Phase 2 step records.
- **FR-007** `signature` is the four-line minisign text (D7). It matches
  `^untrusted comment: [^\r\n]*\n[A-Za-z0-9+/=]+\ntrusted comment: [^\r\n]+\n[A-Za-z0-9+/=]+\n$`, and the second line
  decodes to 74 bytes beginning `ED` (2 algorithm + 8 key id + 64 signature bytes; checked by `contract.js`, since
  JSON Schema cannot decode base64). Its trusted comment parses under C3 (FR-020).
- **FR-008** `install` (C1) holds `vst3_bundle` (required, `^[A-Za-z0-9][A-Za-z0-9 _.-]*\.vst3$`),
  `au_component` (`^[A-Za-z0-9][A-Za-z0-9 _.-]*\.component$`), `preset_subdir`
  (`^[A-Za-z0-9]([A-Za-z0-9 _-]*[A-Za-z0-9_-])?$`, one segment, the `version.json` value, never ending in a space)
  (plan PD-14), `windows_uninstall_key`
  (`^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}_is1$`) and `macos_pkg_id`
  (`^com\.krateaudio\.[a-z0-9]+$`, the pattern Phase 10's `uninstall_pkg` enforces, roadmap line 699). No pattern admits
  `/`, `\`, `:` or a value of `.` or `..`, and each has a `not` clause rejecting `..` anywhere. Each name (`vst3_bundle`,
  `au_component`, `preset_subdir`) also has a `not` clause rejecting a Windows reserved device stem (`CON`, `PRN`, `AUX`,
  `NUL`, `COM0`-`COM9`, `LPT0`-`LPT9`, any case, alone or followed by an extension), so `Nul.vst3`, `Con.component` and a
  preset folder `Aux` fail (plan PD-14). The manager never turns a
  feed string into a path unchecked (roadmap line 262). `au_component` and `macos_pkg_id` are required when a `darwin-*`
  platform is present. `windows_uninstall_key` is required when `windows-x86_64` is present (`if`/`then`).
- **FR-009** `payload` (object `{files, sha256}`; `files` integer ≥ 1, `sha256` hex-64) is **required** on a
  `format: "tar.gz"` entry and **forbidden** on `inno-exe` and `pkg` entries (`if`/`then`/`else`; roadmap line 264).
- **FR-010** `prerelease` (optional sibling inside a plugin entry, Q9) has the plugin entry's shape minus the v1 keys:
  required `tag`, `pub_date`, `channel` (`const "beta"`), `release_notes_url`; optional `notes_markdown`, `platforms` and
  `install` (together, as FR-003). `version`, `download_url` and `release_notes` are **not allowed** in it
  (`properties: {version: false, download_url: false, release_notes: false}`; same meaning as a `not: anyOf required`
  clause, which fails Ajv strict compile) (plan PD-1), so no v1 reader can mistake it. The prerelease's version is the `tag`'s version part. That part
  may carry a SemVer prerelease suffix but need not: a plain three-part prerelease (`iterum/v1.2.0` flagged `isPrerelease`) is the normal case (FR-018) (`tag` pattern `^[a-z0-9]+/v<core>(-<prerelease>)?$`, SemVer 2.0.0 §9).
- **FR-011** `manager` (optional, top level) requires `latest_json_url`, an https URL whose path ends in
  `/manager/latest.json` (roadmap line 135-138). Other keys are left open for Phase 11.
- **FR-012** `tools/feed-contract/versions.v1.schema.json` encodes **exactly** what the shipped parser needs
  (`plugins` object, each entry object with string `version`, optional string `download_url` / `release_notes`) and
  nothing else. It documents the compatibility floor. Every v2-valid document is also v1-valid (SC-005 checks it on every
  valid fixture).
- **FR-013** `tools/feed-contract/CONTRACT.md` gives, for C1, one table row per field: path, type, required/optional,
  value rule, who writes it (`gen-feed.js`, from which input), and who reads it (in-plugin parser; manager phase). It also
  states the reader rules: **ignore unknown keys**, resolve the native platform key and then `darwin-universal` (Phase 5),
  ignore `prerelease` in manager v1 (Q9), and the asset name of an entry is its URL's last segment (D8).

- **FR-014** **Install uniqueness (Clarifications Q5).** `contract.js` `validateFeed` rejects, with the new code
  `E_INSTALL_DUPLICATE` naming the field and both plugin keys, a feed in which two different plugin entries share a value
  of `install.vst3_bundle`, `install.au_component`, `install.windows_uninstall_key` or `install.macos_pkg_id`.
  `vst3_bundle`, `au_component` and `macos_pkg_id` are compared after ASCII case folding (`Alpha.vst3` and `ALPHA.vst3`
  name one folder on NTFS and default APFS); `windows_uninstall_key` is compared as an exact string (its pattern is
  upper-case hex only) (plan PD-13). An entry's own `install` and its `prerelease` sibling's `install` are one plugin and are not
  compared with each other; a v1-only entry has no `install` and takes no part. JSON Schema cannot express it, so it is
  code, stated in `CONTRACT.md` as a reader rule, and Phase 5's Rust feed client inherits it from there. The generator's
  self-check (FR-045) runs it; the generator degrades the plugins involved before it can fire (FR-049). The nine AppIds
  are unique today (measured, Clarifications Q5).

### B. C2 — `release.json` (roadmap lines 274-276)

- **FR-015** `tools/feed-contract/release.schema.json` (2020-12, closed objects) requires `tag`, `name` (plugin key),
  `version` (SemVer 2.0.0 with optional prerelease suffix), `pub_date`, `assets` and
  `install` (as FR-008). `install` requires all four of `vst3_bundle`, `au_component`, `windows_uninstall_key` and
  `macos_pkg_id`: every C2 manifest carries a `windows-x86_64` and a `darwin-*` asset (FR-016), so FR-008's two
  conditional requirements always apply to the entry it becomes, and requiring the four names is FR-008 applied, not a
  new rule (plan PD-10). There is **no `channel` key** (FR-018): the object is closed, so a manifest that carries one is
  `E_MANIFEST_SCHEMA`.
- **FR-016** `assets` has exactly `windows-x86_64`, `linux-x86_64` and one `darwin-*` key (`required` + `maxProperties: 3` +
  `propertyNames` enum of the five keys). This is the "three existing assets" rule. A release missing one gets no
  `release.json` at all (roadmap lines 402-403). Each asset has `name`, `url`, `size`, `sha256`, `signature` and
  `format` (all required) under the FR-006 rules, including the per-key `format` binding, stated in C2 as
  `patternProperties` on `assets` (`^windows-` → `inno-exe`, `^darwin-` → `pkg`, `^linux-` → `tar.gz`), the same shape
  as C1's `platforms`. gen-feed also checks each assembled entry on its own against C1 (`checkAssembledEntry`) before the
  document-level self-check, and a failure there degrades that plugin with `E_MANIFEST_SCHEMA` instead of reaching the
  `E_OUTPUT_SCHEMA` abort (plan PD-10). `min_os` (FR-006 pattern) is **required** on the `darwin-*` and
  `linux-x86_64` assets and **optional** on `windows-x86_64`. This is a deliberate departure from the roadmap's C2 field
  list (roadmap line 274-275, which marks only `payload` optional): no Phase 2 step records a Windows minimum (roadmap
  lines 340-344 record only the macOS product version and the glibc version), so requiring it would force Phase 2's
  `gen-release-manifest.js` and the backfill to invent a value. `CONTRACT.md` states the rule and the departure. The
  `tar.gz` asset adds `payload {files, sha256, list}`, with `list` an
  array of C4 listing lines without terminators (`minItems: 1`). The other two assets have no `payload`.
- **FR-017** `CONTRACT.md` states the C2 cross-field rules that JSON Schema cannot express. `contract.js`
  `checkReleaseManifest(doc, {repo})` enforces them (also used by Phase 2's `gen-release-manifest.js`):
  `tag === name.toLowerCase() + "/v" + version`; each asset
  `name` is `<name>-<version>-Windows-x64.exe` / `-macOS.pkg` / `-Linux-x64.tar.gz` for its key (`release.yml:524-526`);
  each `url === "https://github.com/" + repo + "/releases/download/" + tag + "/" + asset.name` (per-tag, literal slash);
  each signature's trusted comment equals `formatTrustedComment(name, version, key, asset.name)` (FR-020);
  `payload.files === list.length` and `payload.sha256 === digest(list)`; `list` passes the C4 layout check with
  `install.vst3_bundle` (FR-033); `install.vst3_bundle === name + ".vst3"` and, when present, `au_component === name + ".component"`.

- **FR-018** **A release's channel has one source (Clarifications Q1).** `release.json` carries no channel, and neither
  `checkReleaseManifest` nor gen-feed reads one from it. The GitHub release list's `isPrerelease` is the only source:
  gen-feed writes C1 `channel` `"stable"` on the stable entry and `"beta"` on the `prerelease` sibling from that flag
  (FR-042, FR-043). A SemVer suffix on the version stays optional: a prerelease may be `iterum/v1.2.0` with
  `isPrerelease: true`, and its sibling is `channel: "beta"` with that plain version in its `tag`. Promoting a prerelease
  is unticking the GitHub flag: its already-uploaded `release.json` stays valid byte for byte, and the next feed run
  emits it as the stable entry. A suffixed version on a non-prerelease release remains `E_TAG_FORMAT` (FR-042).
  `CONTRACT.md` states the rule.

### C. C3 — trusted-comment grammar and the known-answer fixture (roadmap lines 277-288)

- **FR-020** Grammar: exactly one line (no CR, no LF), `krate:1:<Name>:<version>:<platform-key>:<asset-file-name>`, six
  `:`-separated fields. `<Name>` follows FR-002. `<version>` is SemVer 2.0.0 (core plus optional prerelease, no build
  metadata, which keeps `+` out). `<platform-key>` is one of the **five** C1 keys (stricter than the feed's open
  `propertyNames`: a signature is only ever made for a defined key). `<asset-file-name>` matches
  `^[A-Za-z0-9][A-Za-z0-9._-]*$` and is neither `.` nor `..` (so no `/`, `\` or `:`). In the `.minisig` it sits on the line
  `trusted comment: <comment>`.
- **FR-021** `contract.js` exports `parseTrustedComment(str) → {name, version, platform, asset}` or throws a named error
  (FR-046 codes `E_TC_*`), plus `formatTrustedComment(name, version, platform, asset)`. `formatTrustedComment` validates
  its inputs, and parsing its output returns them unchanged. `parseMinisigText(text)` (LF or CRLF) returns
  `{untrustedComment, algorithm, keyId, trustedComment}` without verifying cryptography. Cryptography is Phase 2's
  `tools/minisign.js` and Phase 7's Rust.
- **FR-022** The **binding check** `checkBinding(trustedComment, {name, version, platform, asset})` fails with
  `E_TC_BINDING` naming the first differing field. A valid comment for asset A presented with asset B's metadata fails.
- **FR-023** Known-answer fixture in `tools/feed-contract/fixtures/kat/`: a small file
  `KatPlugin-1.2.3-Linux-x64.tar.gz` (a real gzip tar in the C4 layout, so it doubles as a listing fixture), its
  `KatPlugin-1.2.3-Linux-x64.tar.gz.minisig` made by `rsign sign -s <tmp>/kat.key -W -t
  "krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz"` with rsign2 0.6.7, the public key file
  `kat.pub`, and `README.md`. The README records the exact commands, the rsign2 version, the OS, the date, the
  CRLF→LF normalisation of the `.minisig` (D7, measured CRLF output on Windows) and the `rsign verify` output on the
  normalised file. The private key is generated in a fresh `fs.mkdtemp` directory outside the repo and deleted right after
  signing. It is never committed (Cross-Cutting "Security", roadmap lines 960-966).
- **FR-024** No **secret-key file** may exist under `tools/feed-contract/`. The check detects key files, not
  signatures: every `.minisig` legitimately starts `untrusted comment: signature from rsign secret key` (measured, see
  External facts), so the words `secret key` alone prove nothing. A file is flagged when **any** of these holds:
  (a) its name ends in `.key`; (b) a line matches `^untrusted comment: (rsign|minisign) encrypted secret key`;
  (c) a line matches `^untrusted comment: .*secret key\r?$` and the next line base64-decodes to a blob whose bytes 0-1
  are `Ed` and bytes 4-5 are `B2` (the secret-key layout `<sig_alg> || <kdf_alg> || <cksum_alg> || …`; a signature
  blob is 74 bytes beginning `ED`, a public key 42 bytes). A test enforces it (SC-013), including a negative case
  showing that the KAT `.minisig` and every fixture `signature` value are **not** flagged, and positive cases (one per
  rule a-c) on synthetic key-shaped files the test writes to a temp dir (for (c): a header line plus a base64 line
  decoding to `EdScB2` followed by filler bytes), so the scan cannot pass by flagging nothing.

### D. C4 — Linux archive layout and canonical listing (roadmap lines 289-294)

- **FR-030** A canonical listing is one line per entry: `<type> <mode> <path>` for `f` and `d`, and
  `l <mode> <path> -> <target>` for a symlink. `<mode>` is four octal digits per D3. `<path>` is the relative POSIX path
  without a leading `./` and without a trailing `/`. Lines are sorted by the UTF-8 bytes of `<path>` (`Buffer.compare`),
  and each line ends with LF, the last one included. The digest (`payload.sha256`) is the lowercase hex SHA-256 of that
  byte string. `payload.files` is the number of lines.
- **FR-031** Path rules (each violation a named error): valid UTF-8; no NUL, CR or LF; no `\`; no leading `/`; no Windows drive
  prefix (`^[A-Za-z]:`); no empty, `.` or `..` segment after the leading `./` is dropped; no ` -> ` substring; no duplicate
  path. A symlink target must be relative (the same character rules as a path: no leading `/`, no drive prefix, no `\`,
  no NUL/CR/LF) and, resolved lexically from the link's parent directory, must land strictly inside the link's **own
  top-level entry** (the first segment of its path). A symlink inside `<vst3_bundle>/` therefore resolves within that
  bundle, matching the Phase 8 extractor that builds on this contract (roadmap lines 614-615, "symlinks whose target
  escapes the bundle"): `<Name>.vst3/Contents/x -> ../../README.txt` stays inside the archive root but is
  `E_LISTING_SYMLINK_ESCAPE`, as is a target above the root. Three further symlink rules make lexical and physical
  resolution agree (plan PD-12): (1) a link target holds `..` only as a leading run, i.e. zero or more leading `..`
  segments, then one or more segments none of which is empty, `.` or `..` (`E_LISTING_SYMLINK_FORM`; `a/../b`, `./a`,
  `a//b`, `..`, `../..` and a trailing `/` fail); (2) no entry (`f`, `d` or `l`) has an `l` entry's path as a proper
  ancestor (`E_LISTING_THROUGH_SYMLINK`, the entry would be written through the link); (3) neither a link's resolved
  target nor any prefix of it is the path of an `l` entry (`E_LISTING_SYMLINK_CHAIN`: a link to a link, through a link,
  to itself, or a loop). `E_LISTING_DOTDOT` is for entry paths only; a `..` in a link target is governed by rule (1) and
  the escape rule (plan PD-12). A symlink at the root itself is an error. Hard links, character or block devices, FIFOs and
  every other type are errors. An archive root entry (`.` or `./`) is dropped. Every ancestor directory implied by a path
  but absent as an entry is added as `d 0755`, so an archive listing and the listing of its extracted tree agree. A
  path that is a file in one entry and an implied directory in another is a duplicate.
- **FR-032** `tools/feed-contract/listing.js` exports `listingFromEntries(entries)`, `listingFromTarGz(buffer)`,
  `listingFromDirectory(root)`, `digest(lines)` and `checkLinuxLayout(lines, {vst3Bundle})`. `listingFromTarGz` decompresses
  with `node:zlib` and reads ustar headers (including the ustar `prefix` field), GNU long-name/long-link records (`L`,
  `K`) and pax `x` records (`path`, `linkpath`), and ignores pax `g` records. Each of these code paths has its own good
  fixture (FR-058). The real tarballs are GNU format. It verifies each header checksum, and a
  truncated or corrupt archive is a named error. `listingFromDirectory` uses `lstat` and never follows a link. On
  `win32` it throws `E_LISTING_NO_POSIX_MODES` (D4).
- **FR-033** `checkLinuxLayout`: the root holds exactly `<vst3_bundle>` (d), `presets` (d), `README.txt` (f) and
  `CHANGELOG.txt` (f), the layout users extract today (`release.yml:384-395`, `:402-403`; roadmap line 289-291). The bundle
  holds `Contents/Resources/moduleinfo.json` (f, the detection source, Key decision 6) and
  `Contents/x86_64-linux/<Name>.so` (f, `<Name>` = bundle name minus `.vst3`), as the real tarball shows. Anything else
  at the root, or a missing item, is `E_LAYOUT` naming the item.
- **FR-034** `CONTRACT.md` documents C4. It states that Windows and macOS have **no** archive layout: the manager runs the
  `.exe` / `.pkg` verbatim (STRATEGY), so their feed entries carry no `payload`.

### E. `tools/gen-feed.js` (roadmap lines 295-300)

- **FR-040** **Legacy mode**: `node tools/gen-feed.js --legacy --plugins-dir <dir> --tags <file> --out <file>`. `--tags`
  holds one **bare** tag name per line, the form `git tag -l` prints and `release.yml:665` reads (LF or CRLF, an optional
  final newline). A line that is empty (other than the final one), contains whitespace or `^`, or starts with `refs/`
  is `E_INPUT` naming the line, so raw `git ls-remote` output (`<sha>\trefs/tags/<tag>`, `^{}` peel lines) is rejected
  rather than misparsed. It reproduces `release.yml:645-685` byte for byte. Plugins are taken in order of their
  subdirectory names (bytewise; the nine lower-case directory names sort the same as `ls`). The tag for each plugin is the
  first of `<name.toLowerCase()>/v*` under **git's `-v:refname` order**, which follows git's `versioncmp` with no
  `versionsort.suffix` configured. A prerelease-suffixed tag therefore sorts above its release, as today. A plugin without
  a tag is skipped. The entry is `{version, download_url: url, release_notes: ""}`, the document is `{schema_version: 1, plugins}`,
  and the output is `JSON.stringify(doc, null, 2) + "\n"`.
- **FR-041** **v2 mode**: `node tools/gen-feed.js --plugins-dir <dir> --releases <file> --manifests <dir> --repo <owner/name>
  [--manager-latest-url <url>] --out <file>`. `--releases` is the JSON array `gh release list --json
  tagName,isPrerelease,isDraft,publishedAt` emits. `--manifests` holds zero or more `release.json` files (`*.json`). `--plugins-dir/<p>/installers/windows/setup.iss` is read for each plugin that has a manifest (FR-043 check 2). There
  is no network access and no `git` call: inputs are files (roadmap line 295-296).
- **FR-042** **Q9 rule, applied here and only here**: for each plugin, drafts are ignored. Among the remaining releases whose tag is
  `<name lowercased>/v<version>`, the **stable** entry is the non-prerelease with the highest SemVer 2.0.0 precedence; its
  version is the v1 `version`. A prerelease never changes it. A non-prerelease release whose version has a
  prerelease suffix is `E_TAG_FORMAT` (the v1 key would break `SemVer::parse`). The `prerelease` sibling is the prerelease
  with the highest precedence **greater than** the stable version. Absent such a prerelease, there is no sibling. A plugin with no
  stable release is omitted from `plugins`, like a plugin without a tag today (`release.yml:668-671`). Tags that match no
  plugin (`v0.8.0`, a future `manager/v…`) are ignored.
- **FR-043** Entry assembly (key order fixed: `version`, `download_url`, `release_notes`, `tag`, `pub_date`, `channel`,
  `release_notes_url`, `notes_markdown`, `platforms`, `install`, `prerelease`; `platforms` keys sorted bytewise; plugins
  in subdirectory order as FR-040). `download_url` is `version.json` `url`. `release_notes` is `""`. `pub_date` is `publishedAt`.
  `release_notes_url` is `https://github.com/<repo>/releases/tag/<tag>`. `channel` is `"stable"` on the entry and `"beta"` on the `prerelease` sibling, from the release's `isPrerelease` alone
  (FR-018). When a `release.json` with that tag exists, it
  must pass C2 (FR-015 to FR-017); `channel` plays no part. Its `install` block must
  also agree with the plugin's own files under `--plugins-dir`, which gen-feed already reads. **Check 1 (Clarifications
  Q4, gated on version):** only when `version.json` `version` equals the manifest's `version` (exact string), the
  `version.json` facts must match (roadmap Reuse Inventory line 201: `name`,
  `version`, `preset_subdir` and the optional `identifier` "feed the `install` block"): `install.preset_subdir` equals
  `version.json` `preset_subdir` (both absent, or both present and equal), and `install.macos_pkg_id` equals
  `version.json` `identifier` when that is present, or `"com.krateaudio." + name.toLowerCase()` when it is absent (the
  rule of `release.yml:321`, `jq -r '.identifier // "com.krateaudio.\(.name | ascii_downcase)"'`). When `version.json` `version` differs from the manifest's (an unreleased bump ahead of this release, or a
  prerelease-suffixed manifest), check 1 is skipped silently, with no warning, and `install` is used as the manifest has it.
  **Check 2 (Clarifications Q5, not version-gated):** read `<plugin dir>/installers/windows/setup.iss`, take the `AppId=`
  line, undo Inno's doubled opening brace (`{{` becomes `{`) to get `{<GUID>}`, and require
  `install.windows_uninstall_key === "{<GUID>}_is1"` (exact, upper-case hex). A missing file, a file with no `AppId=`
  line, or a differing value fails check 2. A failed check is `E_MANIFEST_MISMATCH` naming the field (and, for check 2,
  the file), a per-plugin manifest failure that degrades per FR-049. Under ROSTER the feed entry is the manager's only source for these facts, so a
  manifest that dropped Membrum's `"Kits"` or carried another pkg id would misplace Linux presets or point uninstall at
  the wrong receipt. Then `platforms` holds each asset minus `name` and minus `payload.list`, and `install` is the
  manifest's `install` verbatim. Without a manifest the
  entry is v1-only (FR-003). The top level is `schema_version: 2`, `plugins`, then `manager` when
  `--manager-latest-url` is given.
- **FR-044** `notes_markdown` comes from `<plugin dir>/CHANGELOG.md` when present. It is the lines after the line
  `## [<version>]…` up to the next line starting `## [`. The section header is matched as the literal string
  `## [<version>]` at line start, not as `release.yml:462-463`'s awk regex (whose `.` matches any character); for every
  real header (`## [1.0.4] - 2026-10-29`) both cut the same lines (plan PD-4). CRLF is normalised to LF, and
  leading and trailing blank lines are trimmed. The key is omitted when the file or the section is missing or the section is empty.
  The prerelease sibling uses its own version's section.
  **One section, uncapped (Clarifications Q6):** the value is a single string holding exactly that one section, the
  entry's own version (the sibling's own version for the sibling), with no size limit, no truncation and no marker, and
  never an array of sections or versions. A multi-version view, if ever wanted, would be a separate additive key; none is
  defined here.
- **FR-045** **Self-check before writing**: the generated document must pass C1 (FR-001 to FR-011, FR-014, plus FR-007's base64
  check) and v1 (FR-012). A failure is `E_OUTPUT_SCHEMA` (a generator bug guard). Output is UTF-8, LF,
  `JSON.stringify(doc, null, 2) + "\n"`, written to a temp file in the `--out` directory and renamed into place, so a failed
  run never leaves a partial or replaced file.
- **FR-046** **Rejections, in two classes** (Clarifications Q3), by whether the failing input can reach a v1 key. Usage
  errors exit 2 with the usage line and write nothing.
  **Abort class** (exit code 1; first stderr line `gen-feed: <CODE>: <message naming the plugin, file and field>`; nothing
  written): `E_INPUT` (missing or unparseable input), `E_PLUGIN_NAME` (`name` not a string or not FR-002), `E_V1_TYPE`
  (any input that would put a non-string in a v1 key, e.g. a non-string `url`), `E_DUPLICATE_PLUGIN` (two plugin
  directories whose `name`s are equal after `name.toLowerCase()`, so `Alpha` and `ALPHA` collide) (plan PD-13), `E_TAG_FORMAT`,
  `E_URL_NOT_HTTPS` and `E_URL_LATEST` when the offending URL is a `version.json` `url` (the v1 `download_url`), and
  `E_OUTPUT_SCHEMA` (a generator bug guard).
  **Degrade class** (FR-049: exit code 3, the feed is still written): a failure found while checking one plugin's
  `release.json` or its install facts: `E_MANIFEST_SCHEMA`, `E_MANIFEST_MISMATCH`, `E_SIGNATURE_TEXT`, `E_TC_FIELDS` /
  `E_TC_NAME` / `E_TC_VERSION` / `E_TC_PLATFORM` / `E_TC_ASSET` / `E_TC_BINDING`, `E_LISTING_*` (including
  `E_LISTING_SYMLINK_FORM`, `E_LISTING_SYMLINK_CHAIN` and `E_LISTING_THROUGH_SYMLINK`, FR-031) (plan PD-12), `E_LAYOUT`,
  `E_INSTALL_VALUE` (a path separator, `..` or a pattern violation in any `install` value), `E_URL_NOT_HTTPS` /
  `E_URL_LATEST` / `E_URL_NOT_PER_TAG` when the URL is in a `release.json` (`/releases/latest/` is refused in any URL,
  input or output), `E_INSTALL_DUPLICATE` (FR-014), and `E_MANIFEST_DUPLICATE` (two manifest files with the same
  `tag`: both are dropped and, when that tag is a selected stable or sibling tag, its plugin degrades, with one WARN
  naming the plugin and both files) (Clarifications 2026-10-30 Q-A). A `release.json` that is not valid JSON (or whose
  `tag` is not a string) is `E_MANIFEST_SCHEMA`, degrade class, not `E_INPUT`: the file is ignored and its WARN line
  names the file, because no plugin can be named from it (Clarifications 2026-10-30 Q-A). The code list, the class of each code and the exit codes
  (0, 1, 2, 3) are in `CONTRACT.md`. Phase 5's Rust client reuses the names where it rejects the same thing.
- **FR-047** **v1 projection**: `node tools/gen-feed.js --project-v1 <feed>` prints `{schema_version: 1, plugins: {<k>:
  {version, download_url, release_notes}}}` with plugin order preserved, formatted as FR-040. It is what a v1 reader sees
  (`schema_version` is set to 1 because the parser never reads it, `update_checker.cpp:222-262`). Phase 3's `dry_run`
  comparison uses it.
- **FR-048** The tool follows the `tools/*.js` convention (roadmap line 988): `#!/usr/bin/env node`, a usage line, and a
  pure exported core (`buildFeed`, `buildLegacyFeed`, `projectV1`) that the CLI wraps. The CLI itself is an exported
  `main(argv, {stdout, stderr})` returning the exit code, so tests run it in-process under the FR-061 write guard; the
  script entry only calls `process.exitCode = main(process.argv.slice(2), process)`. It contains no plugin name
  (ROSTER): every name comes from `version.json`.

- **FR-049** **A bad manifest degrades its plugin, never the feed (Clarifications Q3).** When a degrade-class error
  (FR-046) is found for a plugin, the run does not abort. That plugin's entry is emitted **v1-only**: `version`,
  `download_url`, `release_notes` and the other non-install keys (`tag`, `pub_date`, `channel`, `release_notes_url`,
  `notes_markdown`) are kept exactly as they would be without a manifest, and `platforms` and `install` are dropped from
  the entry and from its `prerelease` sibling. Other plugins are unaffected. For each degraded plugin gen-feed prints one
  stderr line `gen-feed: WARN <CODE>: <message naming the plugin, file and field>`. It then self-checks (FR-045), writes
  the feed (temp file and rename) and exits **3**. A run with no degrade exits 0 and prints no `WARN` line. An abort-class
  error (FR-046) anywhere in the same run still wins: exit 1, nothing written, a pre-existing `--out` file untouched.
  `E_INSTALL_DUPLICATE` degrades **every** plugin that shares the value, since neither can be trusted to name the
  uninstaller. The exit codes are 0 (clean), 1 (abort), 2 (usage), 3 (feed written, at least one plugin degraded), so
  `feed.yml` (Phase 3) can deploy the safe feed and still turn the run red. `--legacy` and `--project-v1` never exit 3.

### F. Fixtures (roadmap measurable outcomes, lines 310-319; Phases 2, 3, 5, 7 and 8 read them in place)

- **FR-055** `tools/feed-contract/fixtures/feeds/` holds `valid/` and `invalid/` documents and `cases.json`, whose rows
  are `{rule, file, schema?, expect, plugin?}` (plan §3.1): `rule` is the id of the schema or code rule the row exists for
  (e.g. `"FR-008/darwin-requires-au_component"`, valid rows included), `schema` is `"v1"|"v2"`, `expect` is `"accept"` |
  `"accept-without-platforms"` | `{"error": <code or "instancePath keyword">}`, and `plugin` names the entry an
  `accept-without-platforms` row is about (plan PD-11). The
  minimum set: v1-only (today's live shape), full v2 (all five platform keys across plugins, a Membrum-like
  `preset_subdir`), mixed (v2 + v1-only entries), v2 with a `prerelease` sibling (one with a suffixed tag, one with a plain three-part tag, FR-018), an entry whose `notes_markdown` is over 14,000 bytes (accepted, FR-044), invented tenth plugin, an
  unknown platform key (accepted, D1), and unknown extra keys (accepted, D1). Invalid documents cover: malformed JSON, `version` not a string, `version`
  `1.1.0-beta.1`, `version` `4294967296.0.0` (a 10-digit part, FR-004), `version` `01.2.3`, `release_notes` not `""`, `install` value with `..`, with `/`, with `\`; a `/releases/latest/` URL;
  an `http://` asset URL; a `tar.gz` entry without `payload`; an `inno-exe` entry with `payload`; a `darwin-*` entry
  without `min_os`; `platforms` without `install`; a `prerelease` sibling carrying `version`; a wrong `format` for its key;
  a signature line with `Ed` (legacy); a bad uninstall key; a pkg id outside the pattern; four documents in which two plugins share `vst3_bundle`,
  `au_component`, `windows_uninstall_key` or `macos_pkg_id` (`E_INSTALL_DUPLICATE`, FR-014); a `notes_markdown` that is an
  array (FR-003).
- **FR-056** `fixtures/release/` likewise for C2. It includes one valid manifest per platform-key variant for macOS, one
  valid manifest whose `windows-x86_64` asset has no `min_os` (FR-016), plus an invalid manifest whose `linux-x86_64`
  asset has no `min_os`, and invalid cases for each FR-016/FR-017 rule (missing asset, a fourth asset, a wrong asset name, a URL on another tag, a URL
  with `/releases/latest/`, a manifest carrying a `channel` key (`"stable"` and `"beta"`, `E_MANIFEST_SCHEMA`, FR-015), a binding mismatch, `payload.files` ≠ `list.length`, a digest
  mismatch, a layout violation). The valid manifests include one with a prerelease-suffixed version and one with a plain
  three-part version; none carries a `channel` key.
- **FR-057** `fixtures/trusted-comments.json`: valid comments (stable, prerelease version, each of the five keys) and
  invalid ones with their expected code. The invalid set: five and seven fields, the prefix `krate:2`, an unknown platform key
  `windows-aarch64`, versions `1.2`, `01.2.3`, `1.2.3+b1`, `1.2.4294967296` (FR-004 bound), asset names containing `/`, `\`, `:` and `..`, an empty field,
  an embedded CR.
- **FR-058** `fixtures/listings/`: committed `.tar.gz` files with their `.listing` and `.sha256` (bad archives carry an
  `.expected-error` with their code instead). `fixtures/listings/README.md` records every command and which fixture
  each produced. Two tools make them, each run once:
  - **GNU tar in WSL** builds the well-formed archives from POSIX trees with `--owner=0 --group=0 --numeric-owner
    --mtime=<fixed>` and `--format=gnu` unless stated: the good set `real-shaped` (FR-033 layout, spaces in preset
    names), `exec-and-symlink` (a `0755` file and an in-bundle relative symlink), `long-name` (a path over 100 bytes,
    which forces a GNU `L` record), `long-link` (a symlink whose target is over 100 bytes, which forces a GNU `K`
    record), `pax-path` (`--format=pax`, a path over 255 bytes, so it cannot fit the ustar `name`/`prefix` split, and a
    symlink target over 100 bytes, giving a pax `x` header with `path` and `linkpath`), `pax-global` (`--format=pax`
    with a global extended header, a pax `g` record that must be ignored), `implicit-dirs` (no directory entries),
    `dot-slash` (`./`-prefixed entries), `ustar-prefix` (`--format=ustar`, a path 101-255 bytes long split into the
    ustar `prefix` + `name` fields) (plan PD-18); and the two bad **layout** archives, `layout-extra-root-file` and
    `layout-missing-presets` (well-formed archives, `E_LAYOUT`).
  - **`tools/feed-contract/fixtures/listings/make-bad.js`** (committed, Node, no dependencies) writes every malformed
    archive from raw ustar header bytes (512-byte blocks, its own checksum computation) and gzips it with `node:zlib`,
    because GNU tar cannot write them from a POSIX tree (it strips a leading `/` without `-P`, needs root `mknod` for a
    device, and never writes a drive or backslash path, a bad checksum or a cut stream). It produces: absolute path,
    `..` path, Windows drive path, backslash path, symlink escaping the root, **symlink escaping the bundle but inside
    the root** (`<Name>.vst3/Contents/x -> ../../README.txt`, `E_LISTING_SYMLINK_ESCAPE`, FR-031), root-level symlink,
    hard link, character device, duplicate entry, file/dir clash, truncated archive (a tar stream cut mid-header inside a
    valid gzip), bad header checksum, `pax-malformed`, `base256-size`, `no-end-block` (plan PD-18), and one archive per
    FR-031 symlink rule: `symlink-dotdot-chain` (the two-link chain whose links each pass the lexical check while the
    physical resolution lands outside the root), `symlink-chain`, `symlink-self`, `through-symlink` (plan PD-12).
  The test of each special-record fixture also asserts, from the raw decompressed bytes, that the record it exists for
  is present (typeflag `L` in `long-name`, `K` in `long-link`, `x` with both keys in `pax-path`, `g` in `pax-global`), so
  a tar that chose another encoding fails the case instead of passing it vacuously.
- **FR-059** `fixtures/gen-feed/<case>/`: inputs (`plugins/<p>/version.json`, optional `CHANGELOG.md`, `releases.json`,
  `tags.txt`, `manifests/`) and goldens (`expected.v2.json`, `expected.legacy.json` and/or `expected.error`). A case's
  `tags.txt` is its `releases.json` tag names, drafts included. The cases include:
  - **without prerelease or draft** (SC-002): no prerelease; several stable releases with multi-digit parts
    (`1.9.0`, `1.10.0`, `2.0.0`); a plugin with no release at all (skipped by both modes); a plugin with a manifest and
    one without; a `version.json` carrying `identifier` (its manifest's `macos_pkg_id` equals it); a tenth invented
    plugin; CHANGELOG with the section; CHANGELOG without the section; a CHANGELOG whose section for the entry's version is over
    14,000 bytes, beside two other sections (SC-027); non-plugin tags; a plugin whose `version.json` `version` is ahead of
    its newest release, with a manifest whose `preset_subdir` and `identifier` differ from `version.json` (check 1
    skipped, SC-024); every case that has a manifest also carries its plugin's `installers/windows/setup.iss` (FR-043
    check 2); and the snapshot (FR-071);
  - **with a prerelease or draft** (SC-003): a prerelease newer than stable (`1.2.0-beta.1` over `1.1.0`); an older
    prerelease with the **same core** (`1.1.0-beta.1` beside `1.1.0`); an older prerelease with a **lower core**
    (`1.0.0-beta.1` beside `1.1.0`); a plugin with only prereleases; a draft (non-prerelease, higher version than the
    newest published stable release); a plain-version prerelease (`1.2.0` with `isPrerelease: true`, stable `1.1.0`) with a
    `release.json`, and the same case re-run with `isPrerelease: false` and the same manifest bytes (promotion, SC-022);
  - **rejections** (SC-010): one case per FR-046 abort-class code the generator can raise, including an `E_INPUT` case
    whose `tags.txt` is raw `git ls-remote` output (FR-040), and a case that holds an abort-class error together with a
    plugin whose manifest fails (abort wins, nothing written, FR-049);
  - **degrades** (SC-023): one case per FR-046 degrade-class code the generator can raise, each beside a healthy second
    plugin, including: `E_MANIFEST_MISMATCH` for each FR-043 rule, check 1 (a manifest without Membrum-like
    `preset_subdir` `"Kits"`; a manifest whose `macos_pkg_id` is not `com.krateaudio.<name lowercased>` when
    `version.json` has no `identifier`, in both cases with `version.json` `version` equal to the manifest's) and check 2
    (a `windows_uninstall_key` that differs from the `setup.iss` AppId, a `setup.iss` with no `AppId=` line, a missing
    `setup.iss`); `E_INSTALL_DUPLICATE` for `windows_uninstall_key` and for `macos_pkg_id` (two manifests share the
    value; both plugins degrade), plus a case `degrade-copied-bundle-name` in which a manifest carries another plugin's
    `vst3_bundle` and `au_component` and only the copying plugin degrades, with `E_MANIFEST_SCHEMA`: FR-017 binds
    `vst3_bundle` / `au_component` to the plugin name, so through gen-feed those two duplicates always fail FR-017
    first; all four fields stay covered at `validateFeed` level by SC-026, with no test-only seam into gen-feed
    (Clarifications 2026-10-30 Q-C); `E_INSTALL_VALUE` for `/`, `\` and `..`; and a case
    whose `prerelease` sibling's manifest fails (the entry and the sibling both lose `platforms` and `install`).

### G. C++ guard test (roadmap lines 301-303, 318-319)

- **FR-050** One `TEST_CASE("UpdateChecker - feed schema v2 is read as v1", "[update]")` in
  `plugins/shared/tests/test_update_checker.cpp`, through the existing `MockUpdateChecker` and `checkForUpdate(true)` path
  (`parseResponse` is private, `update_checker.h:77`). It has two `SECTION`s:
  (a) a v2 document with `schema_version: 2`, `platforms` (all three formats, with `payload` on the `tar.gz`), `install`,
  top-level `manager`, `channel` and a `prerelease` sibling whose tag version is higher than `version`. The result must
  equal the one from the same entry's v1-only document: `updateAvailable`, `latestVersion` (the stable `version`, never
  the prerelease), `downloadUrl` and `releaseNotes == ""`;
  (b) the same document with `schema_version: 99`, which must give the identical result.
  The documents are inline raw-string fixtures in the test file, as `kValidJson` is (`:53-59`).
- **FR-051** The case changes no header and no non-test source. `node tools/header-consumers.js --tag '[update]'` ends
  with `CONSUMERS OUTSIDE [update]: none`.
- **FR-052** The case writes nothing outside a temp dir. A scoped guard in the test file sets the settings-directory
  variable that `getAppSettingsDirectory` reads (`APPDATA` on Windows, `HOME` on macOS, `XDG_CONFIG_HOME` on Linux,
  `preset_paths.cpp:56-72`) to a fresh `std::filesystem::temp_directory_path()` subdirectory. It uses `_putenv_s` on
  Windows and `setenv` on POSIX, under `#ifdef` as test-only code. The guard restores the previous value and removes the
  directory on exit. **Before** `checkForUpdate(true)` the case `REQUIRE`s that
  `Krate::Plugins::Platform::getAppSettingsDirectory("FeedV2Probe")` starts with the guard's temp dir (the
  function is declared in `platform/preset_paths.h:33`, which the test includes; no header changes), so a failed
  redirect aborts the case before anything is written. After the check it asserts that `update_state.json` was written
  **under that temp dir**. Its plugin key is a fixture-only name (`FeedV2Probe`): even a redirect failure that slipped
  past the `REQUIRE` would create a new `FeedV2Probe` folder rather than overwrite a real plugin's state, but it would
  still write under the real settings root, which is why the pre-write `REQUIRE` is the safety check and the name is not.
  The same guard and pre-write `REQUIRE` are also applied to the three existing `[update]` cases for the
  names they use (`Iterum`, `Ruinae`), a test-only edit in the same file (Clarifications Q2, SC-021), so no
  `shared_tests` run writes the real per-user `update_state.json` again.

### H. CI lane and repo plumbing

- **FR-060** `.github/workflows/feed-tools.yml`, triggered per D9. The path filter covers `tools/gen-feed.js`,
  `tools/gen-release-manifest.js` and `tools/minisign.js` (listed now, created in Phase 2), `tools/feed-contract/**`,
  `tools/feed-contract/package.json`, `tools/feed-contract/package-lock.json` (the `ajv` dependency) and the workflow
  file itself (Clarifications 2026-10-30 Q-D). One job `node-test` with
  `strategy.matrix.os: [ubuntu-latest, macos-latest, windows-latest]`, `fail-fast: false`, and job name
  `node-test (${{ matrix.os }})`. Steps: `actions/checkout@v6`; `actions/setup-node@v4` with `node-version: '24'` (this
  box's major, v24.7.0); `npm ci --prefix tools/feed-contract` (Clarifications 2026-10-30 Q-D); `cargo install rsign2 --version 0.6.7 --locked` (roadmap line 353);
  `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` (a **quoted glob**, expanded by Node: a
  directory argument runs no test file on Node 24, see External facts), with the runner's default process isolation and
  concurrency (no `--test-concurrency=1`), its output kept as the step log and its `# tests` / `# pass` / `# fail` lines
  checked per SC-018; and
  `rsign verify -p tools/feed-contract/fixtures/kat/kat.pub tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz`;
  and a last step that fails when `git status --porcelain` prints anything (the tests left no file in the checkout,
  SC-015). Permissions are `contents: read`. The job uses no secrets and makes no release, Pages or network call beyond the package installs.
- **FR-061** Tests live in `tools/feed-contract/tests/*.test.js` (`node:test` + `node:assert/strict`). They write only
  under `fs.mkdtemp(os.tmpdir())` directories and remove them. This is **enforced**, not trusted: every test file's
  first statement is `require('./helpers/write-guard.js')`, which wraps every writing entry point of `node:fs` and
  `node:fs/promises` (`writeFile*`, `appendFile*`, `mkdir*`, `mkdtemp*`, `rename*`, `copyFile*`, `cp*`, `rm*`,
  `rmdir*`, `unlink*`, `symlink*`, `link*`, `truncate*`, `chmod*`, `chown*`, `lchown*`, `utimes*`, `lutimes*`,
  `mkdtempDisposable*`, `createWriteStream`, and `open*` with any flag other than `r`) and throws `E_WRITE_OUTSIDE_TMP`
  for a path that does not resolve under `os.tmpdir()` (compared after `fs.realpathSync` of the tmpdir, so macOS `/var`
  → `/private/var` matches). It refuses any path argument holding a `..` segment before resolution (`path.resolve`
  collapses `<tmp>/L/../evil` while the kernel follows `L` first), and it checks **both** paths of `link*` (a hard link
  from a repo file into tmp would let a later tmp write modify the repo file) (plan PD-15). It also wraps
  `child_process` as an **allowlist**: only `git` and `tar`, each in fixed argument shapes, with `cwd` (and, for `tar`,
  the `-C` destination) resolving under `os.tmpdir()`; `exec*` with a shell string, `fork` and `shell: true` are refused
  (plan PD-15). They do not load `http`,
  `https`, `http2`, `net`, `tls` or `dgram` (with or without the `node:` prefix), and they never call `fetch`.
  `child_process` is used only to run the local `git` in a temp repo for SC-004 and system `tar` for SC-009.
- **FR-062** A new tracked `tools/feed-contract/package.json` (`private: true`) holds `"ajv": "8.20.0"` (exact) under
  `devDependencies`, and its `package-lock.json` (lockfile v3) is written with `npm install --prefix tools/feed-contract`.
  `contract.js` resolves `ajv` from `tools/feed-contract/node_modules`. No other dependency is added. `.gitignore`'s
  `package.json` / `package-lock.json` lines become `/package.json` / `/package-lock.json`, so the root files stay
  untracked and a package below the root can be tracked. The local root `package.json` loses the `ajv` line added
  earlier in this phase (Clarifications 2026-10-30 Q-D).
- **FR-063** `.gitattributes` gains `tools/feed-contract/fixtures/** -text`, with a comment saying why (byte-exact
  signatures, digests and goldens; this checkout runs `core.autocrlf=true`).

### I. Scope fences

- **FR-070** Phase 1 changes only: `tools/gen-feed.js`, `tools/feed-contract/**`, `plugins/shared/tests/test_update_checker.cpp`,
  `.github/workflows/feed-tools.yml`, `.gitignore` (the two anchored lines only) (Clarifications 2026-10-30 Q-D), `.gitattributes`, `specs/INDEX.md`
  (regenerated with `node tools/gen-specs-index.js`, so `ci.yml`'s "Generated Artifacts In Sync" job stays green)
  (Clarifications 2026-10-30 Q-B), and this spec's directory. It
  does not touch `release.yml` or any other workflow, any plugin `src/`, any `version.json`, or any installer, and it
  publishes nothing (roadmap lines 328-329).
- **FR-071** The **snapshot fixture** `fixtures/gen-feed/snapshot-2026-10-29/` freezes real data. It holds copies of the
  nine `plugins/*/version.json`, the nine `plugins/*/installers/windows/setup.iss` (FR-043 check 2, SC-025), `tags.txt` derived from `git ls-remote --tags origin` (each line's `<sha>\trefs/tags/`
  prefix stripped, every `^{}` peel line dropped, de-duplicated, one bare tag name per line, LF, final LF; the exact
  Node one-liner that did this is recorded in the fixture's `README.md`), `releases.json` from `gh release list
  --limit 200 --json tagName,isPrerelease,isDraft,publishedAt`, and `live-versions.json` fetched with `curl -sL` from the
  plugins' endpoint, all captured once while the fixture is made. Tests read only the copies and never the network.

---

## Success Criteria

All Node SCs run locally on Windows with `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` (quoted
glob, parallel files; FR-060). A run counts only if its TAP summary shows `# fail 0` and `# tests` ≥ 21, the number of
named tests in SC-001 to SC-015 and SC-022 to SC-027, so a run that loads no test file cannot pass. Each Node macOS/Linux row is
**PENDING-CI** on the named `feed-tools.yml` job, which the user's push of the feature branch triggers. The C++ rows
(SC-016, SC-017) are PENDING-CI on `ci.yml`, which runs only on a pull request to `main` or a push to `main`.

- **SC-001 (legacy = today's live feed).** `buildLegacyFeed` on the snapshot inputs (FR-071) returns a string
  **byte-identical** to `live-versions.json`. Test `gen-feed legacy reproduces the 2026-10-29 live feed`. Threshold: 0
  differing bytes.
- **SC-002 (v1 keys preserved).** For every non-rejection `fixtures/gen-feed/*` case whose release list has **no
  prerelease and no draft** (the FR-059 first group, the snapshot included: its 88 releases have neither),
  `projectV1(buildFeed(inputs))` is byte-identical to `buildLegacyFeed(same plugins, tags = releases' tagNames)`. Test
  `v1 projection of v2 equals legacy` (one subtest per case, ≥ 8 cases). Threshold: every case equal. This is the
  roadmap outcome "equal where prereleases cannot interfere" (roadmap lines 310-313); the cases where they can are
  SC-003's.
- **SC-003 (Q9).** Test `Q9 prerelease never moves the v1 version`, one subtest per FR-059 prerelease/draft case, each
  asserting the v2 result and the legacy pick (legacy `tags` = all the case's tag names, drafts included):

  | Case | v2 (`buildFeed`) | Legacy (`buildLegacyFeed`, git `-v:refname` order) | v1 projection vs legacy |
  |---|---|---|---|
  | newer prerelease `1.2.0-beta.1`, stable `1.1.0` | `version` `1.1.0`; sibling `tag` `<p>/v1.2.0-beta.1`, `channel: "beta"` | picks `<p>/v1.2.0-beta.1` → `version` `"1.2.0-beta.1"` | differ (the defect Q9 removes) |
  | same-core older prerelease `1.1.0-beta.1`, stable `1.1.0` | `version` `1.1.0`; **no** sibling (SemVer: `1.1.0-beta.1` < `1.1.0`) | picks `<p>/v1.1.0-beta.1` (git ranks the suffixed tag above its release, FR-040) → `"1.1.0-beta.1"` | differ |
  | lower-core older prerelease `1.0.0-beta.1`, stable `1.1.0` | `version` `1.1.0`; no sibling | picks `<p>/v1.1.0` | **equal** (asserted byte-identical) |
  | only prereleases | plugin absent from `plugins` | emits the plugin with the highest prerelease tag's version | differ |
  | draft (non-prerelease, above the newest published stable) | draft ignored; `version` = newest published stable; no sibling | picks the draft's tag | differ |
  | plain-version prerelease `1.2.0` (`isPrerelease: true`), stable `1.1.0` (FR-018) | `version` `1.1.0`; sibling `tag` `<p>/v1.2.0`, `channel: "beta"` | picks `<p>/v1.2.0` → `version` `"1.2.0"` | differ |

  The legacy column is asserted exactly as written, not just recorded. A "differ" row is a contrast with today's
  behaviour only because git order ranks the prerelease (or draft) tag first there; where it does not (the lower-core
  row) the two modes must agree. Threshold: every row as tabled.
- **SC-004 (git order emulation).** For a corpus of ≥ 20 tag sets (multi-digit parts, `-rc1`, `-beta.1`, `-beta.10`,
  leading zeros, unrelated tags), the legacy tag choice equals `git tag -l "<p>/v*" --sort=-v:refname | head -1` run
  in a temp repo with no `versionsort.suffix` configured (`GIT_CONFIG_GLOBAL` and `GIT_CONFIG_SYSTEM` point at empty files). Test `legacy tag order matches git versionsort`. Threshold: all
  sets equal, on Windows locally and on all three `node-test (…)` legs (PENDING-CI for macOS/Linux).
- **SC-005 (C1 fixtures).** Every `fixtures/feeds/cases.json` row yields its expected result: valid documents pass v2 (and,
  per FR-012, v1). Each invalid document is rejected with its listed code or `instancePath`+`keyword`. Test
  `C1 feed fixtures`. Threshold: 100 % of rows, ≥ 29 rows, every FR-055 invalid category present (the four `E_INSTALL_DUPLICATE` documents included, SC-026).
- **SC-006 (C2 fixtures).** Every `fixtures/release/` case passes or fails `release.schema.json` + `checkReleaseManifest`
  with its expected code. Test `C2 release.json fixtures`. Threshold: 100 %, every FR-056 rule covered.
- **SC-007 (C3 grammar).** Every valid comment parses and round-trips through `formatTrustedComment`. Every FR-057
  invalid comment fails with its expected `E_TC_*` code, and asset A's comment checked against asset B's metadata fails
  with `E_TC_BINDING`. Test `C3 trusted comments`. Threshold: 100 %.
- **SC-008 (C4 from archives, every OS).** For each good `fixtures/listings/*.tar.gz` (and the KAT file),
  `listingFromTarGz` equals the committed `.listing` byte for byte and `digest` equals `.sha256`. Each bad archive fails
  with its expected code, and `checkLinuxLayout` accepts `real-shaped` and rejects both layout cases. The good set
  includes `long-name` (GNU `L`), `long-link` (GNU `K`), `pax-path` (pax `x` `path` + `linkpath`) and `pax-global` (pax
  `g`, ignored), each with its record-presence assertion (FR-058), and `ustar-prefix` (ustar `prefix` + `name` join, with
  a magic / version / non-empty `prefix` assertion) (plan PD-18); the bad set includes the bundle-escaping symlink
  (`E_LISTING_SYMLINK_ESCAPE`). Test `C4 listings from tar headers`. Threshold: 100 %, on Windows locally; macOS/Linux
  PENDING-CI on the same legs.
- **SC-009 (C4 from disk, POSIX).** On `node-test (ubuntu-latest)` and `node-test (macos-latest)`, each good fixture is
  extracted with the system `tar -xzf` into an `fs.mkdtemp` dir, and `listingFromDirectory` equals the committed `.listing`.
  The symlink and the exec bit survive, and the macOS run proves `/var` → `/private/var` does not leak into the paths. On
  Windows the same test asserts that `listingFromDirectory` throws `E_LISTING_NO_POSIX_MODES`, with the **same subtests**
  (one per good fixture), so the `# tests` count is equal on all three legs (SC-018). Test `C4 listings from
  extracted trees`. **PENDING-CI** for the two POSIX legs.
- **SC-010 (generator aborts).** For each FR-046 abort-class code the generator can raise, its `fixtures/gen-feed/<case>`
  run (through the exported `main`, FR-048, in-process) returns exit code 1 with that code on stderr's first line. An
  `E_INPUT` subtest feeds raw `git ls-remote` output as `--tags`. A pre-existing `--out` file stays byte-identical, and no
  temp file remains. A case holding an abort-class error and a failing manifest exits 1 and writes nothing (FR-049). A
  usage error exits 2 with the usage line and writes nothing.
  Test `gen-feed rejects` (one subtest per code; at least `E_V1_TYPE`, `E_URL_LATEST` and `E_URL_NOT_HTTPS` (each from a
  `version.json` `url`), `E_TAG_FORMAT`, `E_PLUGIN_NAME`, `E_DUPLICATE_PLUGIN`, `E_INPUT`). The degrade-class codes
  (`E_INSTALL_VALUE`, `E_MANIFEST_MISMATCH`, `E_TC_BINDING`, ...) are SC-023's.
- **SC-011 (`release_notes`).** In every document any test produces (legacy, v2, projection), every plugin's
  `release_notes === ""`. Test `release_notes is always empty`. Threshold: 0 exceptions.
- **SC-012 (determinism).** Two runs on the same inputs give byte-identical output. A copy of a case with CRLF
  `CHANGELOG.md` and `version.json` (made in-test) gives the same bytes as the LF original. The committed goldens match on
  all three legs. Test `gen-feed is deterministic`. Windows local, macOS/Linux PENDING-CI.
- **SC-013 (KAT and keys).** The KAT `.minisig`'s trusted comment parses under C3 and binds to its file name, key
  `linux-x86_64`, `KatPlugin` and `1.2.3`. `parseMinisigText` reports algorithm `ED`. `rsign verify` exits 0, once at
  creation (output pasted in `fixtures/kat/README.md`) and in the `rsign verify` step of each `node-test (…)` leg
  (PENDING-CI). The scan of `tools/feed-contract/**` under the FR-024 rules finds no secret-key file, flags none of the
  KAT `.minisig` or the fixture `signature` values, and flags each synthetic key-shaped file (rules a-c). Test
  `KAT binds and no secret key is committed`. Threshold: 0 key files in the tree, 0 false positives, 3 of 3 synthetic
  positives flagged.
- **SC-014 (roster).** The `fixtures/gen-feed/tenth-plugin` case (a tenth `version.json` with an invented name) yields
  ten entries, and the nine-plugin case yields nine. A test greps `tools/gen-feed.js`, `contract.js`, `listing.js` and
  both schema files for the nine plugin names and finds 0 hits. Test `generator ships no plugin roster`.
- **SC-015 (no network, temp-only writes).** Test `tools stay offline`:
  (a) a source scan of `tools/gen-feed.js`, `tools/feed-contract/*.js`, `tools/feed-contract/fixtures/listings/make-bad.js`
  and every test file finds no match for
  `(require\s*\(|import\s*\(|from\s+|import\s+)\s*['"](node:)?(http|https|http2|net|tls|dgram)['"]`, no `fetch(`, and
  (outside the SC-004 and SC-009 test files and the write guard) no `(node:)?child_process`;
  (b) every `tools/feed-contract/tests/*.test.js` has `require('./helpers/write-guard.js')` as its first statement;
  (c) the guard has teeth: inside the test, a write to a path under the repo root through `fs.writeFileSync`,
  `fs.promises.writeFile`, `fs.mkdirSync`, `fs.renameSync` and `fs.openSync(…, 'w')`, and a `spawnSync` with a repo
  `cwd`, each throw `E_WRITE_OUTSIDE_TMP`, and the target path does not exist afterwards;
  (d) after the whole run, `git status --porcelain` is unchanged from before it: the last step of each `node-test (…)`
  leg (FR-060) asserts it prints nothing, and the local Windows run records the before/after output in the compliance
  row. Threshold: 0 scan hits, every test file guarded, every (c) probe throws, 0 new or modified paths.
- **SC-016 (C++ guard).** `"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests` completes with
  0 warnings and 0 errors (build log line count of `warning` = 0), and `node tools/header-consumers.js --tag '[update]'`
  ends `CONSUMERS OUTSIDE [update]: none`. The existing `[update]` cases are redirected to a temp dir too (Clarifications Q2, FR-052, SC-021), so the local
  gate runs all of them without touching `%APPDATA%\Krate Audio\Iterum\` or `...\Ruinae\`:
  `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests`
  (`run-gate.js:52-54` refuses a filter without the four timing exclusions) prints `GATE GREEN`; the case run once by
  name, `shared_tests.exe "UpdateChecker - feed schema v2 is read as v1" -r junit` (the single-case direct run CLAUDE.md
  allows), reports `All tests passed` and lists both sections (`v2 document`, `schema_version 99`) as run with no
  failure, because `run-gate.js` prints no passing case names and its shard logs therefore cannot list it (see Review
  notes); and SC-021's before/after `stat` of the two real files is unchanged.

  macOS and Linux: **PENDING-CI** in `ci.yml` jobs `test-macos` (`macOS tests (shard …)`, `ci.yml:1404-1406`) and
  `test-linux` (`Linux tests (shard …)`, `:1441-1443`). They run `shared_tests` because the edit sets
  `detect-changes.outputs.shared` (filter `plugins/shared/**`, `:85-86`; `build-macos` and `build-linux` add the target,
  `:623`, `:1127`). `ci.yml` runs only on a **pull request to `main`** or a push to `main` (`:3-23`), never on the
  feature-branch push, so the row stays PENDING-CI until the user opens that PR (or merges). The assertion: every
  `test-macos` / `test-linux` shard job is green, and the shard log that ran `shared_tests` reports
  `UpdateChecker - feed schema v2 is read as v1` passed.
- **SC-017 (redirected state).** Before `checkForUpdate(true)`, the case `REQUIRE`s that
  `getAppSettingsDirectory("FeedV2Probe")` starts with the guard's temp dir, so a failed redirect aborts the case before
  any write (FR-052). Section (a) then asserts that `update_state.json` exists under that temp dir. After the case, the
  guard's env variable is back to its prior value (asserted). Windows local; macOS/Linux PENDING-CI with SC-016 (same
  jobs, same PR-to-`main` trigger).
- **SC-018 (CI lane).** **PENDING-CI**: `feed-tools.yml` jobs `node-test (ubuntu-latest)`, `node-test (macos-latest)` and
  `node-test (windows-latest)` are green. Each runs `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"`,
  the same command and file set as the local Windows run. On every leg and locally the TAP summary shows `# fail 0` and
  `# tests` ≥ 21 (the named tests of SC-001 to SC-015 and SC-022 to SC-027), and the `# tests` value is **equal** on all three legs and the
  local run (SC-009 keeps the subtest structure identical on Windows). The `rsign verify` step and the `git status
  --porcelain` step pass.
- **SC-019 (parallel, measured).** A **recorded measurement, no pass/fail bar**: the local run uses the default
  `node --test` concurrency, and its wall clock and `# tests` count go in the compliance row. The pass/fail part is a
  named test, `no serial test flags`: it scans `.github/workflows/feed-tools.yml` and every `*.md` under
  `tools/feed-contract/` and finds no `--test-concurrency=1`, `--test-concurrency 1`, `--test-isolation=none` or
  `--test-isolation none`. Threshold: 0 hits.
- **SC-020 (scope).** At close, `git diff --name-only <base>..` and `git status --porcelain` list only FR-070 paths,
  `specs/INDEX.md` among them, and `node tools/gen-specs-index.js --check` exits 0 (Clarifications 2026-10-30 Q-B).
  `git ls-files tools/feed-contract/package.json tools/feed-contract/package-lock.json` lists both once staged, and
  `git check-ignore` matches neither (Clarifications 2026-10-30 Q-D).
  `release.yml` and `plugins/*/src/**` are unchanged.
- **SC-021 (existing `[update]` cases redirected, Clarifications Q2).** Every `[update]` `TEST_CASE` in
  `test_update_checker.cpp` (`:79`, `:146`, `:181` and the new one) opens with the FR-052 guard and `REQUIRE`s,
  immediately after the guard and before any `MockUpdateChecker` is constructed (stricter than "before its first
  `checkForUpdate`": `dismissVersion` writes state, `update_checker.cpp:69-76`) (plan PD-5), that `getAppSettingsDirectory(<name>)` starts with the guard's temp dir for every plugin
  name it uses (`Iterum`, `Ruinae`, `FeedV2Probe`); a read of the file confirms each, cited by line in the compliance
  row. Across the SC-016 gate run, the last-write time and size of `%APPDATA%\Krate Audio\Iterum\update_state.json` and
  `%APPDATA%\Krate Audio\Ruinae\update_state.json`, read with a plain `stat` before and after, are unchanged (a missing
  file stays missing), and the compliance row quotes both readings. Windows local; macOS/Linux PENDING-CI with SC-016.
- **SC-022 (channel from the release list only, Q1).** Test `channel comes from the release list only`:
  (a) every valid `fixtures/release/` manifest has no `channel` key and passes C2, and a copy with `channel: "stable"` and
  a copy with `channel: "beta"` each fail with `E_MANIFEST_SCHEMA`; (b) the plain-version prerelease case (FR-059: `1.2.0`,
  `isPrerelease: true`, stable `1.1.0`, with a manifest) exits 0 with the entry `channel: "stable"` and the sibling `tag`
  `<p>/v1.2.0`, `channel: "beta"`; (c) promotion: the same manifest file (byte-identical, hashed before and after) with
  `isPrerelease` flipped to false exits 0 with no `WARN` line, the plugin's entry now `version` `1.2.0`, `channel:
  "stable"`, `platforms` from that manifest, and no sibling; (d) a non-prerelease release whose version carries a suffix
  still exits 1 with `E_TAG_FORMAT`. Threshold: every part as stated.
- **SC-023 (a bad manifest degrades its plugin, Q3).** Test `gen-feed degrades a bad manifest`, one subtest per FR-059
  degrade case: `main` returns **3**; `--out` is written and passes C1 and v1; the failing plugin's entry has `platforms`
  and `install` absent (also on its sibling) and every other key byte-equal to the same run with that plugin's manifest
  removed; the healthy plugin's entry is byte-equal to its golden; stderr holds one `gen-feed: WARN <CODE>:` line per
  degraded plugin naming the plugin, or the file when no plugin can be named (Clarifications 2026-10-30 Q-A), and the
  expected code (two lines for `E_INSTALL_DUPLICATE`, both plugins degraded);
  no temp file remains. A clean run returns 0 with no `WARN` line. A case with an abort-class error and a failing manifest
  returns 1, writes nothing and leaves a pre-existing `--out` byte-identical. `--legacy` and `--project-v1` never return 3.
  Threshold: every case and assertion.
- **SC-024 (install check 1 is version-gated, Q4).** Test `install cross-check is gated on version`: (a) `version.json`
  `version` equal to the manifest's, `preset_subdir` differing: exit 3 with `E_MANIFEST_MISMATCH` on `preset_subdir`; the
  same for a differing `macos_pkg_id`; (b) `version.json` `version` ahead of the manifest's, with `preset_subdir` and
  `identifier` both differing: exit 0, no `WARN` line, and the entry's `install` equals the manifest's verbatim;
  (c) a prerelease-suffixed manifest (never equal to the three-part `version.json` version) is skipped the same way;
  (d) in the case of (b), a wrong `windows_uninstall_key` still fails check 2 (SC-025). Threshold: every part as stated.
- **SC-025 (uninstall key equals the `setup.iss` AppId, Q5).** Test `windows_uninstall_key equals the setup.iss AppId`:
  (a) on the snapshot's nine `setup.iss` copies (FR-071) the extractor returns nine distinct `{GUID}` values, each
  matching the FR-008 GUID pattern; (b) a manifest whose key is `{GUID}_is1` of its own `setup.iss` passes; (c) a differing
  key, a `setup.iss` with no `AppId=` line and a missing `setup.iss` each give `E_MANIFEST_MISMATCH`, exit 3, the `WARN`
  line naming the file; (d) `AppId={{GUID}` (Inno's doubled brace) is read as the single-brace `{GUID}`. Threshold: every
  part as stated.
- **SC-026 (no two plugins share an install fact, Q5).** Test `feed rejects shared install facts`: each of the four FR-055
  invalid documents fails `validateFeed` with `E_INSTALL_DUPLICATE` naming the field and both plugin keys; the full v2 and
  mixed valid fixtures pass; an entry and its own `prerelease` sibling carrying identical `install` values pass; v1-only
  entries take no part; `CONTRACT.md` names `E_INSTALL_DUPLICATE` and the rule, which Phase 5 inherits. Threshold: 4 of 4
  rejected, 0 false positives.
- **SC-027 (`notes_markdown` is one uncapped section, Q6).** Test `notes_markdown is one uncapped section`: on the FR-059
  long-section case, the stable entry's `notes_markdown` is a string byte-equal to its version's section (trimmed, LF) and
  at least 14,000 bytes long, with the other two sections absent from it; a sibling carries only its own version's
  section; the FR-055 array-typed `notes_markdown` fails C1; neither schema file defines `maxLength` or an `array` type for
  `notes_markdown`. Threshold: 0 truncated bytes, 0 foreign sections.

---

## Safety

- **Injected inputs, no roots.** Phase 1 touches no plug-in, preset or state root. The generator reads only the paths
  passed on its command line and writes only `--out` (temp file + rename in that directory). Tests pass `fs.mkdtemp`
  directories under `os.tmpdir()`. The C++ case redirects the settings directory to a temp dir (FR-052).
- **No network in tools or tests.** No test or tool calls github.com, api.github.com or the Pages feed (SC-015). The
  snapshot data was captured once by the agent writing the fixture and is committed (FR-071).
- **Nothing is executed or extracted from a fixture.** `listingFromTarGz` only reads headers and skips file data;
  it writes nothing. System `tar -xzf` runs only on the POSIX CI legs, only on committed known-good fixtures, and only into a temp dir
  (SC-009). The crafted bad archives are only ever parsed, never extracted.
- **Keys.** The only private key this phase creates is the throwaway KAT key. It is generated in a temp dir outside the
  repo and deleted after signing, and a test asserts no secret-key file exists in `tools/feed-contract/` (FR-023, FR-024). No real
  key, no CI secret.
- **Feed strings never become paths.** Every `install` value is a single, pattern-checked name (FR-008). Every listing
  path is checked against FR-031. These contracts are what Phases 8-10 rely on before joining a feed value to a root.
- **What the future helper may receive** is bounded here. `windows_uninstall_key` is a GUID key name and `macos_pkg_id`
  matches Phase 10's pattern, so a feed value cannot carry arguments, paths or roots into the helper's ops. No two feed entries may share an install
  fact (FR-014), and the Windows key must equal the installer's own AppId (FR-043 check 2), so Remove on one plugin cannot
  run another plugin's uninstaller.
- **Verified before use (forward contract).** C1 gives every asset `size`, `sha256` and a C3-bound `signature`. Phase 7
  verifies all three before anything is extracted or executed. Phase 1 verifies no signature cryptographically.

## Edge cases

| Case | Phase 1 behaviour / contract | Later owner |
|---|---|---|
| Interrupted download | Not applicable. C1 gives `size` + `sha256` so Phase 7 can resume and re-check | Phase 7 |
| Disk full / unwritable `--out` | Rename-into-place write; failure exits non-zero (`E_INPUT`/OS error), old file intact, temp removed (FR-045) | — |
| File locked by a running DAW (Windows) | Not applicable (no install) | Phase 10 |
| Partial install | Not applicable. C4's digest and layout rule let Phase 8/9 detect a partial tree | Phases 8-9 |
| Downgrade attempt | C3 binds the version into every signature, so an old valid asset cannot be served under a newer entry. If a release is deleted, the feed's `version` legitimately drops to the newest remaining stable release (generator follows the release list) | Phase 7 (binding), Phase 5 (what to show) |
| Corrupt feed | The generator self-validates and never writes an invalid feed (FR-045). Corrupt fixtures define what readers must reject (FR-055) | Phase 5 |
| Offline | The generator needs no network by design (FR-041) | Phase 5 (cached feed) |
| `schema_version` 1 vs 2 | v1 and v2 schemas both exist; v2 ⊂ v1-valid (FR-012); C++ parser proven indifferent to 2 and 99 (FR-050) | Phase 5 |
| Bundles that still say `0.8.0` | Not a feed fact. C4 requires `moduleinfo.json` in the Linux bundle; versions in it are Phase 6's | Phase 6 |
| A prerelease-suffixed tag on a non-prerelease release | `E_TAG_FORMAT` (would silence every banner, `version_compare.h:24-51`) | — |
| A plugin with only prereleases | Omitted from `plugins` (FR-042) | — |
| `release.json` for a tag that is not in the release list | Ignored (the release list decides what is released) | Phase 3 |
| `version.json` ahead of the newest tag (unreleased bump) | Feed uses the release's version, as today | — |
| CRLF inputs on this Windows checkout | Normalised; identical output (SC-012) | — |
| Preset paths with spaces | Allowed; the listing parses the path as everything after the second space (FR-030) | Phase 8 |
| A path over 100 bytes, a link target over 100 bytes, a pax global header | GNU `L` / `K`, pax `x` `path` / `linkpath` read; pax `g` ignored; each with its own fixture (FR-032, FR-058) | Phase 8 |
| A symlink that stays inside the archive root but leaves its bundle | `E_LISTING_SYMLINK_ESCAPE` (FR-031), as the Phase 8 extractor rejects it | Phase 8 |
| A prerelease promoted by unticking the GitHub flag | Its `release.json` stays valid byte for byte (it has no channel); the next run emits it as the stable entry (FR-018, SC-022) | — |
| One plugin's `release.json` is bad | That plugin degrades to a v1-only entry, a `WARN` line, exit 3, the feed is still written (FR-049, SC-023) | Phase 3 (`feed.yml` deploys, then fails the run) |
| `version.json` ahead of an older release whose `preset_subdir` / `identifier` has since changed | The install cross-check is skipped for that older manifest (FR-043 check 1, SC-024) | — |
| Two plugins share a bundle name, pkg id or uninstall key | `validateFeed` rejects the feed (`E_INSTALL_DUPLICATE`); the generator degrades both plugins (FR-014, FR-049, SC-026) | Phase 5 (client inherits the rule) |

## New components

| Path | Purpose |
|---|---|
| `tools/feed-contract/versions.v2.schema.json` | C1 schema (JSON Schema 2020-12) |
| `tools/feed-contract/versions.v1.schema.json` | What the shipped parser requires (compat floor) |
| `tools/feed-contract/release.schema.json` | C2 schema |
| `tools/feed-contract/CONTRACT.md` | Prose contract C1-C4, reader rules, error codes |
| `tools/feed-contract/contract.js` | `validateFeed`, `validateRelease`, `checkReleaseManifest`, SemVer 2.0.0 parse/compare, `parseTrustedComment` / `formatTrustedComment` / `checkBinding` / `parseMinisigText`, URL and name rules |
| `tools/feed-contract/listing.js` | C4: listing from entries / tar.gz / POSIX tree, digest, Linux layout check |
| `tools/feed-contract/tests/*.test.js` | `node --test` suites for SC-001 to SC-015 and SC-019's `no serial test flags` |
| `tools/feed-contract/tests/helpers/write-guard.js` | FR-061 write and spawn guard, required first by every test file |
| `tools/feed-contract/fixtures/listings/make-bad.js` | FR-058 crafted malformed archives from raw ustar bytes |
| `tools/feed-contract/fixtures/{feeds,release,listings,kat,gen-feed}/`, `trusted-comments.json` | Shared fixtures read in place by Phases 2, 3, 5, 7, 8 |
| `tools/gen-feed.js` | Pure feed generator: v2 mode, `--legacy`, `--project-v1` |
| `.github/workflows/feed-tools.yml` | Three-OS `node --test` lane |
| `plugins/shared/tests/test_update_checker.cpp` (new case) | v2 and `schema_version: 99` guard |

## Traceability

| Roadmap statement | FR / SC |
|---|---|
| C1 schema + prose; `{os}-{arch}` keys; `install` names by pattern; `payload` only on Linux (lines 259-268) | FR-001 to FR-013, SC-005 |
| `channel`, `prerelease` sibling, manager ignores it (lines 269-273; Q9) | FR-003, FR-010, FR-042, SC-003 |
| C2 (lines 274-276) | FR-015 to FR-017, SC-006 |
| C3 grammar, binding (lines 277-283) | FR-020 to FR-022, SC-007 |
| C3 KAT, throwaway key deleted (lines 284-288) | FR-023, FR-024, SC-013 |
| C4 layout + canonical listing (lines 289-294) | FR-030 to FR-034, SC-008, SC-009 |
| gen-feed pure, no network, Q9 here only (lines 295-298) | FR-040 to FR-048, SC-002, SC-003, SC-010, SC-012 |
| Legacy mode byte-identical (lines 299-300) | FR-040, SC-001, SC-004 |
| Catch2 v2 + `schema_version: 99` case (lines 301-303) | FR-050 to FR-052, SC-016, SC-017, SC-021 |
| `feed-tools.yml` three OSes, path-filtered (lines 304-307) | FR-060, SC-018 |
| Rejections: non-string v1 key, `/releases/latest/`, non-https, separator or `..` in `install` (measurable, lines 320-321) | FR-046, SC-010 |
| `release_notes` always `""` (measurable, line 322) | SC-011 |
| Does NOT touch `release.yml`, publish, sign with a real key, change plugin source (lines 328-329) | FR-070, SC-020 |
| ROSTER: no plugin names shipped | FR-002, FR-048, SC-014 |
| Never touch the real system; no network in tests | FR-052, FR-061, SC-015, SC-017, SC-021 |
| Parallel tests | FR-060, SC-019 |
| Clarification Q1: channel only from the release list's `isPrerelease`; C2 has no channel | FR-010, FR-015, FR-017, FR-018, FR-043, SC-003, SC-022 |
| Clarification Q2: the three existing `[update]` cases redirected too | FR-052, SC-016, SC-021 |
| Clarification Q3: a bad manifest degrades its plugin, exit 3 | FR-046, FR-049, SC-010, SC-023 |
| Clarification Q4: install check 1 only when `version.json` version equals the manifest's | FR-043, SC-024 |
| Clarification Q5: AppId check and install uniqueness | FR-014, FR-043, FR-045, FR-071, SC-025, SC-026 |
| Clarification Q6: `notes_markdown` is one uncapped section | FR-003, FR-044, SC-027 |

## Review notes

Challenge review, 2026-10-29 (fidelity, testability and reality lenses): 18 issues, all applied (1 blocker, 7 major,
10 minor), none rejected. The two `ci.yml` trigger issues (testability and reality, SC-016/SC-017) and the two
`tags.txt` format issues (FR-040/FR-071) were duplicates and are resolved once each. Where the resolution departs from
the suggested one, the reason:

- **SC-016/SC-017 CI evidence:** kept on `ci.yml` (PR to `main`) rather than adding a `shared_tests` leg to
  `feed-tools.yml`. Building `shared_tests` needs the full CMake configure and the vendored VST3 SDK on three OSes,
  which duplicates `ci.yml`'s `build-macos` / `build-linux` jobs inside a Node lane; the rows instead stay PENDING-CI
  until the user opens the PR, with the jobs and the assertion named.
- **SC-016 gate filter:** while applying the Clarifications Q2 ruling, `tools/run-gate.js:52-54` turned out to refuse a filter
  without `~[performance]~[perf]~[benchmark]~[!benchmark]`, so the roadmap's literal `--filter '[update]'` could never
  print `GATE GREEN`. SC-016 now gives the full filter (with `~[.]`); the roadmap line is unchanged, and the SC-016 text records the
  difference.
- **SC-016 case evidence (compliance remediation, 2026-10-30):** the row asked the gate's shard logs to list the new case
  with both sections passed, but `run-gate.js` passes only the filter and shard flags to Catch2 (`run-gate.js:102`), whose
  console reporter names no passing case, so no green gate can show it (`shard-log mentions=0` at close). `run-gate.js`
  is outside FR-070. The row now takes the case's own by-name run with `-r junit` for the two sections; the thresholds
  (both sections pass, `GATE GREEN`, 0 warnings, no consumer outside `[update]`) are unchanged.
- **FR-004 overflow bound:** resolved in the pattern (at most 9 digits per part), not by a separate numeric check, so
  the schema alone states the rule and the Rust side (Phase 5) inherits it from the same file. Nine digits is below
  4294967295, so the bound is a little stricter than `uint32_t`; no real version comes near it.
- **FR-058 crafting tool:** `make-bad.js` writes only the **malformed** archives. The two bad-layout archives are
  well-formed and stay GNU-tar-made like the good set. `pax-global` is GNU-tar-made too; the record-presence assertion
  (FR-058) makes the case fail, not pass vacuously, if the tar used emits no `g` header.
- **FR-031 symlink rule:** stated once as "resolves strictly inside the link's own top-level entry", which gives the
  suggested "inside the bundle" rule for bundle links and the "inside its own top-level entry" rule for everything else
  without `listing.js` needing the bundle name; root-level symlinks are rejected outright (none exist in the C4 layout).
- **SC-015 write guard:** each test file `require`s the guard as its first statement (checked by the test) instead of a
  `--require` preload on the command line, so the guard does not depend on how `node --test` forwards flags to its
  child processes. The CLI runs in-process through an exported `main` (FR-048) so its writes pass through the guard.
- **FR-024 key scan:** rules (a)-(c) combine the file-name rule, the encrypted-key header and a decoded-blob check,
  with synthetic positives so a scan that flags nothing fails.
