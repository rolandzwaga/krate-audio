# Implementation Plan: Krate Manager Phase 1 — Feed v2 Schema and Generator

**Spec:** `specs/krate-manager-phase1/spec.md` (DRAFT, reviewed, Clarifications Q1-Q6 of 2026-10-29)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 1 (lines 251-329), Cross-Cutting Constraints (lines 950-992 in the spec's
numbering; section "Cross-Cutting Constraints (apply to every spec)")
**Status:** DRAFT plan, 2026-10-29, revised 2026-10-30 for the plan review (PD-10 to PD-18, "Review notes"), verified against `feat/krate-manager` at `84c6aab1` (untracked: `specs/Krate-manager-roadmap.md`,
`specs/krate-manager-phase1/`)
**Lanes:** pipeline lane only (Node tools, YAML, one C++ test TU). No app code, no Tauri, no Rust in this phase.

---

## 0. What this plan adds to the spec

The spec fixes the contracts and the success criteria. This plan fixes the module boundaries, the exact public APIs,
the error-code table, the fixture build procedure, the test-file map and the gate lanes. Where the plan had to depart
from a spec sentence, or decide something the spec leaves open, it is listed here and repeated at its use site.

| Id | Plan decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | The `prerelease` sibling forbids the v1 keys with `properties: {version: false, download_url: false, release_notes: false}`, not `not: {anyOf: [{required: [...]}]}` | Measured 2026-10-29 with `ajv` 8.20.0 `Ajv2020({strict: true})`: the `not/anyOf/required` form **fails to compile**, `strict mode: required property "version" is not defined at "#/not/anyOf/0" (strictRequired)`; the `false`-schema form compiles and rejects with `keyword: "false schema"`, `instancePath: "/version"`. Same meaning (the key may not be present) | FR-010's parenthetical only; the rule is unchanged |
| PD-2 | Schema authoring rule: **every subschema that uses `required` declares the same names in its own `properties`** (as `true` when the real constraint sits elsewhere), and every subschema using `pattern` / `properties` carries `type` | Measured with the same Ajv: `then: {required: ["payload"]}` fails `strictRequired` even when the parent declares `payload`; `then: {properties: {payload: true}, required: ["payload"]}` compiles and enforces; `{properties: {v: {pattern: …}}}` fails `strictTypes` (`missing type "string" for keyword "pattern"`). Ajv's strict mode page: `strict: true` turns every restriction on (https://ajv.js.org/strict-mode.html) | none (D2 kept: `strict: true`) |
| PD-3 | A schema compile failure is a test failure: `contract.js` compiles all three schemas at module load, so every test file that requires it fails on a strict-mode violation | Makes PD-2 mechanical | none |
| PD-4 | The CHANGELOG section header is matched as the literal string `## [<version>]` at line start | `release.yml:462-463` builds an awk **regex** from `${VERSION}`, so its `.` matches any character; for every real header (`## [1.0.4] - 2026-10-29`, `plugins/innexus/CHANGELOG.md:8`) both cut the same lines | FR-044 wording "as `release.yml:462-463` cuts it" — same output for real data, documented in `CONTRACT.md` |
| PD-5 | The C++ pre-write `REQUIRE` sits immediately after the guard, **before any `MockUpdateChecker` is constructed** | `dismissVersion` writes state (`update_checker.cpp:69-76` calls `saveState()`), and the dismiss case calls it (`test_update_checker.cpp:187`, `:200`) *before* its first `checkForUpdate`; the spec's "before its first `checkForUpdate`" (SC-021) would come too late there | SC-021 wording: the REQUIRE is earlier, which is stricter |
| PD-6 | The `E_LISTING_*` family and the C2 cross-field codes get the concrete names in §2.1 | The spec defines the families ("each violation a named error", FR-031/FR-046) and leaves the names to `CONTRACT.md` | none |
| PD-7 | Fixture JSON (C1/C2 documents, trusted comments, gen-feed **inputs**) is produced by a committed deterministic builder, `fixtures/make-fixtures.js`, with a drift test; gen-feed **goldens** are produced once by `gen-feed.js` and then reviewed by hand (§4.7) | ~30 feed documents and ~20 manifests carry 74-byte-blob signatures and long hex; hand-written copies drift. A builder with `--check` makes drift a red test | FR-055 to FR-059: same files, same paths |
| PD-8 | **Needs a ruling (Open question 1):** an unparseable or unattributable `release.json`, and two manifests claiming the same tag, are **degrade-class** (`E_MANIFEST_SCHEMA` / `E_MANIFEST_DUPLICATE`, WARN, exit 3), never abort | Clarifications Q3's principle ("a bad manifest degrades only that plugin, never the feed"); the spec names no behaviour for these two inputs | FR-046 gains one code, `E_MANIFEST_DUPLICATE` |
| PD-9 | **Needs a ruling (Open question 2):** `specs/INDEX.md` is regenerated in the same change | `node tools/gen-specs-index.js --check` exits 1 **now** ("specs/INDEX.md is stale", run 2026-10-29) because `specs/krate-manager-phase1/spec.md` exists; `ci.yml:171-172` runs that check in job `Generated Artifacts In Sync` (`ci.yml:155-157`), which the C++ edit triggers on the PR to `main`. FR-070's path list does not include `specs/INDEX.md` | FR-070 / SC-020 path list |
| PD-10 | C2 carries every C1 rule that the copied `platforms` / `install` data must satisfy: the per-key `format` const (`patternProperties` on `^windows-` / `^darwin-` / `^linux-` in `assets`), and `install` requires all four of `vst3_bundle`, `au_component`, `windows_uninstall_key`, `macos_pkg_id`. gen-feed then checks each assembled entry on its own against C1 (`checkAssembledEntry`, degrade `E_MANIFEST_SCHEMA`) before the document-level self-check | Review 2026-10-30: a manifest with a `windows-x86_64` asset of `format: "pkg"`, or without `macos_pkg_id` while `version.json` is ahead (check 1 skipped), passed every degrade check and then aborted at the self-check with `E_OUTPUT_SCHEMA`, which breaks FR-049. A C2 manifest always carries `windows-x86_64` and a `darwin-*` key (FR-016), so FR-008's two conditional requirements always apply to it: requiring the four names is FR-008 applied, not a new rule | FR-016 ("under the FR-006 rules") and FR-015 ("`install` (as FR-008)") made explicit; FR-056 gains invalid rows, FR-059 a degrade case |
| PD-11 | `cases.json` rows (feeds and release) carry a `rule` id (e.g. `"FR-008/darwin-requires-au_component"`) and, for `accept-without-platforms`, a `plugin` key; the C1 and C2 tests assert that every id of a fixed rule list has at least one invalid row (§4.7) | Review 2026-10-30: the planned row shape `{file, schema, expect}` had no field to assert category coverage on, and several C1 rules (the FR-008 conditionals, FR-011's suffix, FR-006 `size` bounds, …) had no invalid row | FR-055 row shape gains `rule` and `plugin` (additive) |
| PD-12 | Symlink rules tightened with three named codes: a link target may hold `..` only as a leading run (`E_LISTING_SYMLINK_FORM`); no entry may have an `l` entry as a proper ancestor (`E_LISTING_THROUGH_SYMLINK`); neither a link's resolved target nor any prefix of it may be an `l` entry (`E_LISTING_SYMLINK_CHAIN`, which also rejects self-links and loops). `E_LISTING_DOTDOT` is for entry paths only | Review 2026-10-30 reproduced (node + WSL) a two-link chain whose links each pass the lexical check while `readlink -m` lands outside the root: `l A/b/c/s -> ../../d` normalises to `A/d`; `l A/b/c/t -> s/../../../x` normalises to `A/x` but resolves to `<root>/../x` on disk. A later `f A/b/c/t/evil` wrote through the link | FR-031 gains three rules; FR-046 gains three `E_LISTING_*` names (the family is already degrade class); FR-058's bad set gains one archive per rule |
| PD-13 | Install-fact uniqueness folds ASCII case for `vst3_bundle`, `au_component` and `macos_pkg_id` (`windows_uninstall_key` stays exact: its pattern is upper-case hex only); `E_DUPLICATE_PLUGIN` compares `name.toLowerCase()` | `Alpha.vst3` and `ALPHA.vst3` are one folder on NTFS and on default (case-insensitive) APFS, so Remove on one plugin would delete the other's files; gen-feed already lower-cases the name to select tags (§4.5.4 step 2), so `Alpha` and `ALPHA` would claim the same `alpha/v*` releases | FR-014 "compared as exact strings" → case-folded for three fields; FR-046 `E_DUPLICATE_PLUGIN` wording |
| PD-14 | The plugin-name pattern and the stems of `vst3_bundle` / `au_component` / `preset_subdir` reject the Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM0`-`COM9`, `LPT0`-`LPT9`, any case, with or without an extension); `preset_subdir` may not end in a space | https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file ("Naming Conventions", read 2026-10-30): "Do not use the following reserved names … CON, PRN, AUX, NUL, COM1 … LPT9 … Also avoid these names followed immediately by an extension; for example, NUL.txt and NUL.tar.gz are both equivalent to NUL", and "Do not end a file or directory name with a space or a period". `COM0` / `LPT0` are rejected too (stricter than the list, harmless). Written as explicit `[Cc][Oo][Nn]` alternations because Ajv compiles patterns with the `u` flag (§1.2) and Phase 5's Rust reader must compile the same text | FR-002 and FR-008 patterns tightened |
| PD-15 | The write guard rejects any path argument holding a `..` segment, checks both paths of `link`, wraps `chown*` / `lchown*` / `lutimes*` / `mkdtempDisposable*` too, and narrows `child_process` to an allowlist (`git`, `tar` with fixed argument shapes; `exec*` with a shell string, `shell: true` and `fork` refused) | Review 2026-10-30 reproduced in WSL that `path.resolve` collapses `<tmp>/L/../evil` to `<tmp>/evil` while the kernel follows `L` first; a hard link `fs.linkSync(<repo file>, <tmp>/x)` passed because only the new path was checked; the cwd-only spawn check let `tar -C <repo>` and shell redirects through. `fs.mkdtempDisposableSync` and `fs.promises.mkdtempDisposable` exist in Node v24.7.0 here (measured `typeof … === 'function'`) | FR-061's wrapped list and child_process rule tightened |
| PD-16 | `--manager-latest-url`, the `E_OUTPUT_SCHEMA` self-check and the `--project-v1` CLI get their own subtests; the self-check is reached through a test-only `hooks.beforeSelfCheck(doc)` argument of `main` / `buildFeed` | Review 2026-10-30: the fixtures never produce an invalid document, so a `buildFeed` that skipped `validateFeed` would pass every test | none |
| PD-17 | `contract.js` exports the §3.2 code table as `CODES`; `hygiene.test.js` asserts that `CONTRACT.md` names every code with its class, the exit codes 0-3, and every property path of `versions.v2.schema.json` in the C1 field table | Review 2026-10-30: FR-013/FR-017/FR-034/FR-046 are statements about `CONTRACT.md` content, and Phase 5 inherits reader rules from it, but the only mechanical check was "contains `E_INSTALL_DUPLICATE`" | none (adds one named test, `CONTRACT.md documents every code and field`) |
| PD-18 | A good fixture `ustar-prefix` (`--format=ustar`, a path 101-255 bytes long split into `prefix` + `name`) joins FR-058's good set | Review 2026-10-30: every GNU-format fixture carries the old-GNU magic, where the prefix is ignored by design, and GNU tar's `--format=pax` puts long names in a pax `path` record; no planned fixture reached the `prefix + "/" + name` branch FR-032 names | FR-058 good set and SC-008's list gain one fixture |

PD-10 to PD-18 tighten spec text (FR-002, FR-008, FR-014, FR-031, FR-046, FR-055, FR-058, FR-061). None relaxes a
threshold. The plan is the stage that found them, so the spec rows are amended to match **before** implementation starts
(the "ruling goes in the FR/SC row first" rule): the implementer edits each named FR row in `spec.md` in the same change,
citing the PD id.

There is **no OS abstraction, no injected root, no state directory, no elevated helper and no Tauri capability** in this
phase: nothing it builds touches a plug-in, preset or state folder (spec "Safety"). §6 states which roadmap-wide safety
items apply here and which are forward contracts.

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line read 2026-10-29)

| Fact | File:line | Used by |
|---|---|---|
| Today's generator: `ls plugins/*/version.json` (`:653-654`), tag `git tag -l "${pluginLower}/v*" --sort=-v:refname \| head -1` (`:662-666`), skip without tag (`:668-671`), `version = tag.replace(/.*\/v/, "")` (`:672`), entry `{version, download_url: data.url, release_notes: ""}` (`:674-678`), `{schema_version: 1, plugins}` (`:681`), `JSON.stringify(output, null, 2) + "\n"` (`:683`) | `.github/workflows/release.yml:645-685` | `buildLegacyFeed` (§4.5.3) |
| Tag `TAG="${PLUGIN}/v${VERSION}"`, `PLUGIN` = directory name | `release.yml:73-76` | tag grammar |
| CHANGELOG cut: awk `/^## \[${VERSION}\]/{found=1; next} /^## \[/{if(found) exit} found` | `release.yml:458-464` | `notes_markdown` (PD-4) |
| Asset names `<Name>-<ver>-Windows-x64.exe`, `-macOS.pkg`, `-Linux-x64.tar.gz` | `release.yml:524-526` | C2/C3 name rule |
| `prerelease: ${{ inputs.prerelease }}`, `draft: false` | `release.yml:520-521` | `isPrerelease` source |
| macOS `PRESET_SUBDIR=$(jq -r '.preset_subdir // empty' …)` | `release.yml:306` | FR-043 check 1 |
| macOS `IDENTIFIER=$(jq -r '.identifier // "com.krateaudio.\(.name \| ascii_downcase)"' …)` | `release.yml:321` | FR-043 check 1 |
| Linux staging `<Name>.vst3`, `presets`, `README.txt`, `CHANGELOG.txt`; `cd staging; tar -czvf ../<Name>-<ver>-Linux-x64.tar.gz *` | `release.yml:384-395`, `:402-403` | C4 layout |
| `parseResponse`: `plugins` object, `plugins[name]` object, `version` string, `SemVer::parse` both sides, update only if `remote > current`, then `download_url` / `release_notes` if strings; `catch (...)` resets the result | `plugins/shared/src/update/update_checker.cpp:222-262` | C++ case expectations: the result fields are filled only when an update is available, so the case uses a current version below the feed's |
| `parseResponse` is private; `fetchJson` is the protected virtual seam | `update_checker.h:62-65`, `:77` | C++ case reaches it through `MockUpdateChecker` |
| Constructor calls `loadState()` (read only); `checkForUpdate` spawns the worker; `performCheck` calls `saveState()` at `:208` **before** the result is published; destructor joins the worker | `update_checker.cpp:23-33`, `:35-61`, `:191-210` | C++ guard lifetime: guard outlives every checker; `update_state.json` exists once `waitForResult` returns |
| `dismissVersion` calls `saveState()` | `update_checker.cpp:69-76` | PD-5 |
| `getStateFilePath()` = `getAppSettingsDirectory(pluginName) / "update_state.json"` | `update_checker.cpp:319-324` | C++ assertion path |
| `getAppSettingsDirectory`: `APPDATA` (Windows), `HOME` + `Library/Application Support/Krate Audio/<P>` (macOS), `XDG_CONFIG_HOME` + `krate-audio/<P>`, else `HOME/.config/...` (Linux); read with `std::getenv` at call time | `plugins/shared/src/platform/preset_paths.cpp:53-79` | guard env var per OS |
| `getAppSettingsDirectory` declared | `plugins/shared/src/platform/preset_paths.h:33` | test includes `platform/preset_paths.h` |
| `shared_tests` include dir `${CMAKE_CURRENT_SOURCE_DIR}/../src`, `/W4` on MSVC, `-Wall -Wextra` elsewhere; `test_update_checker.cpp` is in the target | `plugins/shared/tests/CMakeLists.txt:31`, `:59-64`, `:68-80` | no CMake change; zero-warning code (C4996 on `std::getenv`, see below) |
| MSVC-safe env read pattern: `getenv_s` under `_MSC_VER` "avoids C4996", `std::getenv` elsewhere | `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp:215-227`, `plugins/profundum/tests/integration/preset_browser_test.cpp:626-636` | guard's `readEnv` |
| Existing `[update]` cases: `TEST_CASE` at `:79` (`Iterum`, and `Ruinae` at `:113`), `:146` (`Iterum`), `:181` (`Iterum`); fixtures inline raw strings `:53-73`; `MockUpdateChecker` `:15-37`; `waitForResult` `:40-47` | `plugins/shared/tests/test_update_checker.cpp` | C++ edit |
| `SemVer::parse`: `std::from_chars` into `uint32_t` per part, trailing whitespace allowed, no suffix | `plugins/shared/src/update/version_compare.h:17-51` | FR-004 bound |
| `run-gate.js` refuses a filter without the four timing exclusions | `tools/run-gate.js:52-54` | SC-016 filter |
| `header-consumers.js`: with no file args takes modified tracked files under `dsp/include dsp/src plugins/shared tests/test_helpers`; a TU whose every `TEST_CASE` carries the tag is covered; last line `CONSUMERS OUTSIDE <tag>: none` | `tools/header-consumers.js:11-33`, `:74-95` | SC-016 (every case in the file carries `[update]`, `:79`, `:146`, `:181`, so the expected line is `none`) |
| Nine `plugins/*/version.json`; Membrum `"preset_subdir": "Kits"`; none has `identifier`; no `plugins/shared/version.json` | `ls plugins/*/version.json`, each file read | ROSTER, check 1 |
| AppIds: `AppId={{<GUID>}` at line 18 of eight `setup.iss`, Iterum at line 27; all upper-case hex, nine distinct | `plugins/*/installers/windows/setup.iss:18` (`iterum:27`) | FR-043 check 2, SC-025 |
| Every plugin has `CHANGELOG.md`; headers `## [x.y.z] - date` | `plugins/*/CHANGELOG.md` (e.g. `innexus/CHANGELOG.md:8,22,28`) | FR-044 |
| `ci.yml` triggers: `push`/`pull_request` to `branches: [main]` with path filters, no `tools/**`, no `apps/**` | `.github/workflows/ci.yml:3-23` | D9, SC-016 |
| `ci.yml` filter `shared: 'plugins/shared/**'` (`:84-86`); `shared_tests` added to macOS/Linux targets (`:621-623`, `:1125-1127`); jobs `test-macos` "macOS tests (shard …)" (`:1403-1407`), `test-linux` "Linux tests (shard …)" (`:1441-1445`) | `ci.yml` | PENDING-CI rows SC-016/017/021 |
| `ci.yml` job `generated-artifacts` runs `node tools/gen-specs-index.js --check` | `ci.yml:150-172` | PD-9 |
| Workflows today: `ci.yml docs.yml long-tests-nightly.yml release.yml valgrind-nightly.yml`; they use `actions/checkout@v6`, `actions/setup-node@v4`, `actions/cache@v5` | `.github/workflows/` (grep) | `feed-tools.yml` actions |
| Root `package.json` devDependencies `fast-xml-parser ^5.5.5`, `saxon-js ^2.7.0`, `xslt3 ^2.7.0`; lockfile v3 | `package.json`, `package-lock.json:1-12` | FR-062 |
| `.gitattributes` holds LF pins only; `core.autocrlf=true`, `core.symlinks=false` on this checkout | `.gitattributes`, `git config` | FR-063 |
| `node_modules/` ignored (`.gitignore:92`); `tools/*.txt` ignored only at top level — `git check-ignore --no-index` on four fixture paths (`…/tags.txt`, `kat.pub`, `*.tar.gz`, `*.listing`) returned none ignored | `.gitignore:54-57`, `:92` | fixture paths are committable |
| `gen-repo-map.js --check` and `gen-symbols.js --check` exit 0 now; `gen-symbols.js` indexes only `dsp/include/krate/dsp` | `tools/gen-symbols.js:18-26` | the new C++ helper class does not enter the ODR index |
| No class or function named `ScopedSettingsDirRedirect` exists in `dsp/ plugins/ tests/` (grep, 0 hits) | — | ODR check for the C++ helper |
| `.clang-tidy` disables `concurrency-mt-unsafe`; `run-clang-tidy.ps1 -Target shared` covers `plugins/shared/src` and `plugins/shared/tests` | `.clang-tidy:31-41`, `tools/run-clang-tidy.ps1:168-175` | tidy lane |

### 1.2 External facts (verified 2026-10-29)

| Fact | Source |
|---|---|
| `ajv` latest `8.20.0` (modified 2026-04-24); runtime deps `fast-deep-equal ^3.1.3`, `fast-uri ^3.0.1`, `json-schema-traverse ^1.0.0`, `require-from-string ^2.0.2` | `npm view ajv version time.modified dist-tags`, `npm view ajv@8.20.0 dependencies` |
| `require("ajv/dist/2020")` returns the `Ajv2020` class (`module.exports = exports = Ajv2020`; also `.default`, `.Ajv2020`); meta-schema `https://json-schema.org/draft/2020-12/schema` | https://unpkg.com/ajv@8.20.0/dist/2020.js |
| Ajv supports all 2020-12 keywords incl. `dependentRequired`; `pattern` is compiled with `new RegExp(value, "u")`; Ajv ships no `format` validators | https://ajv.js.org/json-schema.html |
| Strict mode: `strict: true` sets every restriction; `strictSchema` (unknown keywords, incomplete `if/then/else`), `strictTypes` (applicable `type` missing), `strictRequired` (required property not defined in `properties`) | https://ajv.js.org/strict-mode.html; measured behaviour in PD-1/PD-2 |
| `yaml` npm 2.9.1 (published 2026-09-11) has a CLI `yaml valid` that exits 0 on a valid file and non-zero on a broken one | `npm view yaml version bin time`; measured in the scratchpad (installed, run on `docs.yml` → exit 0 and on `a: [1` → throws, exit 1; deleted) |
| ustar header: `name[100]@0`, `mode[8]@100`, `size[12]@124`, `chksum[8]@148`, `typeflag@156`, `linkname[100]@157`, `magic[6]@257`, `prefix[155]@345`; checksum = simple sum of the header bytes with `chksum` taken as spaces; `TMAGIC "ustar"` + NUL; `OLDGNU_MAGIC "ustar  "` (7 chars + NUL, uses magic **and** version fields); `GNUTYPE_LONGLINK 'K'`, `GNUTYPE_LONGNAME 'L'`, `XHDTYPE 'x'`, `XGLTYPE 'g'`; in the old-GNU header the bytes from 345 hold `atime`/`ctime`/offsets, **not** a name prefix | https://www.gnu.org/software/tar/manual/html_node/Standard.html (GNU tar 1.35 manual) |
| ustar `version` field is `"00"`; name and prefix "shall produce the pathname"; pax extended record format `"%d %s=%s\n", <length>, <keyword>, <value>`; keywords `path`, `linkpath` | https://pubs.opengroup.org/onlinepubs/9799919799/utilities/pax.html (POSIX.1-2024) |
| git `versioncmp` is glibc `strverscmp`; the prerelease-suffix swap runs only when `versionsort.suffix` (or the older `versionsort.prereleasesuffix`) is configured, otherwise the result is the plain state machine | https://raw.githubusercontent.com/git/git/master/versioncmp.c |
| `strverscmp` semantics: alternating digit / non-digit runs; non-digit runs lexicographic; digit runs: more leading zeros sorts smaller, otherwise the longer number is larger, equal length compared digit by digit; zero-prefixed runs with equal zero count lexicographic | https://sourceware.org/glibc/manual/latest/html_node/String_002fArray-Comparison.html |
| minisign signature format (`untrusted comment`, `base64(alg ‖ key_id ‖ sig)`, `trusted comment`, `base64(global_sig)`; `ED` prehashed; 8-byte key id; global signature over `sig ‖ trusted_comment`) | https://jedisct1.github.io/minisign/ (minisign 0.12), as cited by the spec |
| `rsign2` 0.6.7 installed here (`rsign --version` → `rsign2 0.6.7`); CLI per the spec's External facts | local, spec |
| GNU tar 1.35 available in WSL (`wsl -e tar --version`) | local |
| Node v24.7.0 here; `zlib.gunzipSync` of a truncated gzip throws `Z_BUF_ERROR "unexpected end of file"`; `new TextDecoder('utf-8', {fatal: true})` throws `ERR_ENCODING_INVALID_ENCODED_DATA` on invalid UTF-8; `fs.renameSync` replaces an existing file on Windows; `require('fs') === require('node:fs')` and `fs.promises === require('fs/promises')`; `crypto.getHashes()` has `blake2b512` | measured in the scratchpad, files deleted; APIs: https://nodejs.org/docs/latest-v24.x/api/zlib.html, https://nodejs.org/docs/latest-v24.x/api/util.html (TextDecoder), https://nodejs.org/docs/latest-v24.x/api/fs.html |
| `node --test` runs each file in its own process, up to `availableParallelism() - 1` at once; a quoted glob is expanded by Node; a directory argument runs nothing on Node 24 | https://nodejs.org/docs/latest-v24.x/api/cli.html; measured (spec External facts) |
| Hosted images: `ubuntu-latest` = Ubuntu 24.04, `macos-latest` = macOS 26 arm64, `windows-latest` = Windows Server 2025; each ships Rust/Cargo (1.99.x on Ubuntu 24.04 and Windows 2025, 1.99.0 on macOS 26); macOS 26 `tar` is **bsdtar 3.5.3** (GNU tar as `gtar`) | https://github.com/actions/runner-images (README; `images/macos/macos-26-arm64-Readme.md`, `images/ubuntu/Ubuntu2404-Readme.md`, `images/windows/Windows2025-Readme.md`) |

---

## 2. File layout

```
tools/gen-feed.js                                  CLI + pure core (buildFeed, buildLegacyFeed, projectV1, main)
tools/feed-contract/
  CONTRACT.md                                      prose contract C1-C4, reader rules, error codes, exit codes
  versions.v2.schema.json                          C1 (2020-12, open objects, D1)
  versions.v1.schema.json                          compat floor (FR-012)
  release.schema.json                              C2 (2020-12, closed objects)
  contract.js                                      validators, SemVer, C3, URL / install-name rules, ContractError
  listing.js                                       C4: entries / tar.gz / directory → canonical listing, digest, layout
  tests/
    helpers/write-guard.js                         FR-061 guard (required first by every test file)
    helpers/fixtures.js                            fixture paths + loaders + in-process main() runner (no writes)
    c1-feed.test.js            c2-release.test.js            c3-trusted-comment.test.js
    c4-listing-tar.test.js     c4-listing-dir.test.js        git-versionsort.test.js
    gen-feed-legacy.test.js    gen-feed-v2.test.js           gen-feed-errors.test.js
    hygiene.test.js
  fixtures/
    README.md                                      what each fixture set is for, who reads it (Phases 2, 3, 5, 7, 8)
    make-fixtures.js                               PD-7 builder: `node make-fixtures.js --out <dir>` / `--check`
    feeds/{valid,invalid}/*.json, feeds/cases.json
    release/{valid,invalid}/*.json, release/cases.json
    trusted-comments.json
    git-versionsort.json                           SC-004 corpus (≥ 20 tag sets)
    listings/README.md, listings/make-bad.js
    listings/good/<name>.tar.gz|.listing|.sha256
    listings/bad/<name>.tar.gz|.expected-error
    kat/KatPlugin-1.2.3-Linux-x64.tar.gz(.minisig|.listing|.sha256), kat/kat.pub, kat/README.md
    gen-feed/<case>/case.json, plugins/<p>/{version.json,CHANGELOG.md,installers/windows/setup.iss},
                    releases.json, tags.txt, manifests/*.json, expected.v2.json | expected.legacy.json | expected.error
    gen-feed/snapshot-2026-10-29/  (+ live-versions.json, README.md)
plugins/shared/tests/test_update_checker.cpp       guard on the three existing cases + the new case
.github/workflows/feed-tools.yml                   three-OS node --test lane
package.json, package-lock.json                    ajv 8.20.0 (exact)
.gitattributes                                     tools/feed-contract/fixtures/** -text
specs/INDEX.md                                     regenerated (PD-9, only after the ruling)
```

All JS is CommonJS (`'use strict'`, `require`), like every `tools/*.js` today (e.g. `tools/header-consumers.js:17-20`);
no `"type": "module"` is added to the root `package.json`.

**Module rule that the write guard depends on:** every module in this phase calls `fs.<fn>` / `fs.promises.<fn>` /
`childProcess.<fn>` through the module object at call time and never destructures them at load
(`const { writeFileSync } = require('fs')` would capture the unguarded function if it ran before the guard). A source
scan in `hygiene.test.js` enforces it: no `{ … } = require('(node:)?fs')` and no `{ … } = require('(node:)?child_process')`
in the scanned files. The scanned files are named explicitly (the same list as SC-015(a), §7.1): `tools/gen-feed.js`,
`tools/feed-contract/*.js`, `tools/feed-contract/tests/*.test.js`, **`tools/feed-contract/tests/helpers/*.js`**
(`write-guard.js` and `fixtures.js`; `fixtures.js` runs `main()` in-process for every gen-feed test, so it is inside the
guard's reach and must not capture an unguarded function), `tools/feed-contract/fixtures/make-fixtures.js` and
`tools/feed-contract/fixtures/listings/make-bad.js`. `write-guard.js` is the one file that may require `fs` and
`child_process` before patching them (it is the patcher); `fixtures.js` gets no exemption.

---

## 3. Error model

### 3.1 `ContractError`

```js
// tools/feed-contract/contract.js
class ContractError extends Error {
  /** @param {string} code  e.g. 'E_TC_BINDING'
   *  @param {string} message  names the plugin, file and field where known
   *  @param {{plugin?: string, file?: string, field?: string, instancePath?: string, keyword?: string}} [where] */
  constructor(code, message, where = {}) { super(message); this.name = 'ContractError'; this.code = code; Object.assign(this, where); }
}
```

Every function that rejects throws a `ContractError` (first failure, in a fixed check order) except the two validators,
which return all findings so Phase 5 and the fixtures can see every error:

```js
/** @returns {{valid: true} | {valid: false, errors: Array<{code: string, instancePath: string, keyword?: string, message: string}>}} */
validateFeed(doc, { schema = 'v2' } = {})
validateRelease(doc)
```

Ajv findings come back as `{code: 'E_SCHEMA', instancePath, keyword, message}` (Ajv's own `instancePath` and `keyword`);
code-level findings carry their own code. `cases.json` rows name either a code (`{"error": "E_INSTALL_DUPLICATE"}`) or an
Ajv pair (`{"error": "/plugins/Alpha/version pattern"}`); a row passes when that code or pair is **among** the errors
(with `allErrors: true` Ajv also reports the enclosing `if` / `anyOf` wrappers, measured: the `if/then` probe returned
both the `required` error and the `if` wrapper error).

**Row shape (PD-11).** `feeds/cases.json` and `release/cases.json` rows are
`{rule, file, schema?, expect, plugin?}`. `rule` is the id of the schema or code rule the row exists for
(`"FR-008/darwin-requires-au_component"`, `"FR-055/full-v2"`); every row has one, valid rows included. The test files
declare a fixed rule list (`C1_RULES` in `c1-feed.test.js`, `C2_RULES` in `c2-release.test.js`, §4.7) and assert that
every id in it has at least one row whose `expect` is an error, so a schema that drops a rule and a fixture table that
drops its row both turn the test red. A row naming an id that is not in the list is also a failure (no orphan ids).

**The three `expect` values (FR-055):**
- `"accept"`: `validateFeed(doc, {schema})` is valid; for `schema: "v2"` the same document is also valid under `"v1"`
  (FR-012).
- `"accept-without-platforms"`: as `"accept"`, **and** the row's `plugin` names an entry for which
  `definedPlatformKeys(doc.plugins[plugin])` returns `[]` for the entry and for its `prerelease` sibling, i.e. the
  entry has no platform under any of the five defined keys, so a manager reader treats that plugin as "not available
  through the manager" (FR-003 ROSTER rule, D1's unknown-key case). A row with this value and no `plugin`, or a `plugin`
  absent from the document, is a test failure. `definedPlatformKeys(entryOrSibling) → string[]` (the `PLATFORM_KEYS`
  present in its `platforms`, in `PLATFORM_KEYS` order) lives in `contract.js`, so Phase 5 inherits the definition.
- `{"error": …}`: as above.

A file that is not valid JSON (FR-055 "malformed JSON") is reported by the test's loader as the finding
`{code: 'E_INPUT'}`, the code gen-feed uses for an unparseable input, so that row is `{"error": "E_INPUT"}`.

### 3.2 Code table (goes verbatim into `CONTRACT.md`)

| Code | Raised by | gen-feed class | Meaning |
|---|---|---|---|
| `E_USAGE` | gen-feed CLI | usage, exit 2 | bad or missing flags, conflicting modes (stderr: usage line) |
| `E_INPUT` | gen-feed | abort, exit 1 | missing / unreadable / unparseable input file, malformed `releases.json` row, bad `--tags` line (FR-040), bad `--repo`, bad `--manager-latest-url`, duplicate `tagName` in `releases.json` |
| `E_PLUGIN_NAME` | gen-feed | abort | `version.json` `name` not a string, not `^[A-Z][A-Za-z0-9]{0,63}$`, or a Windows reserved device name in any case (PD-14) |
| `E_V1_TYPE` | gen-feed | abort | an input that would put a non-string in a v1 key (non-string `version.json` `url`) |
| `E_DUPLICATE_PLUGIN` | gen-feed | abort | two plugin directories whose `name`s are equal after `toLowerCase()` (PD-13: `Alpha` and `ALPHA` name one folder on NTFS / default APFS and select the same `alpha/v*` tags) |
| `E_TAG_FORMAT` | gen-feed, contract | abort | a non-draft release whose tag is `<p>/v…` but whose version fails the bounded core (stable) or core+suffix (prerelease); a suffixed version on a non-prerelease |
| `E_URL_NOT_HTTPS`, `E_URL_LATEST` | contract `checkUrl` | abort when the URL is a `version.json` `url`; degrade when it is in a `release.json` | not `^https://[^\s]+$`; contains `/releases/latest/` |
| `E_URL_NOT_PER_TAG` | `checkReleaseManifest` | degrade | asset URL ≠ `https://github.com/<repo>/releases/download/<tag>/<asset name>` |
| `E_OUTPUT_SCHEMA` | gen-feed self-check | abort | generated document fails C1, v1 or `validateFeed` code rules (a generator bug) |
| `E_MANIFEST_SCHEMA` | `checkReleaseManifest`, `checkAssembledEntry` | degrade | C2 schema failure, a `channel` key, unparseable manifest file (PD-8), tag / name / version / asset-name / `vst3_bundle` / `au_component` naming rules of FR-017; an assembled entry that fails C1 on its own (PD-10 backstop, §4.5.4 step 4) |
| `E_MANIFEST_DUPLICATE` | gen-feed | degrade | **PD-8, needs ruling:** two manifest files with the same `tag` (both ignored, the plugin degrades) |
| `E_MANIFEST_MISMATCH` | gen-feed | degrade | FR-043 check 1 (`preset_subdir`, `macos_pkg_id`) or check 2 (`windows_uninstall_key` vs `setup.iss` AppId, missing file, no `AppId=` line) |
| `E_SIGNATURE_TEXT` | `parseMinisigText` | degrade | not four lines, a missing prefix, bad base64, blob ≠ 74 bytes, algorithm ≠ `ED`, global signature ≠ 64 bytes, CR in a C1/C2 value |
| `E_TC_FIELDS` | `parseTrustedComment` | degrade | not exactly 6 `:` fields, field 0 ≠ `krate`, field 1 ≠ `1`, a CR / LF in the comment |
| `E_TC_NAME`, `E_TC_VERSION`, `E_TC_PLATFORM`, `E_TC_ASSET` | `parseTrustedComment` | degrade | the field fails FR-020 (empty included) |
| `E_TC_BINDING` | `checkBinding` | degrade | a field differs from the expected metadata (names the first differing field) |
| `E_INSTALL_VALUE` | `checkInstallValues` | degrade | an `install` value with `/`, `\`, `:`, `..`, a reserved device stem or a trailing space (PD-14), or outside its FR-008 pattern |
| `E_INSTALL_DUPLICATE` | `validateFeed`, gen-feed | degrade (every plugin sharing the value) | FR-014; `vst3_bundle`, `au_component`, `macos_pkg_id` compared after ASCII case folding, `windows_uninstall_key` exactly (PD-13) |
| `E_LAYOUT` | `checkLinuxLayout` | degrade | FR-033 root or bundle item missing or extra (names the item) |
| `E_LISTING_PATH` | listing | degrade | invalid UTF-8, NUL / CR / LF, empty — in an entry path or a link target; a ` -> ` substring in an entry path |
| `E_LISTING_ABSOLUTE`, `E_LISTING_DRIVE`, `E_LISTING_BACKSLASH` | listing | degrade | leading `/`; `^[A-Za-z]:`; a `\` — in an entry path or a link target |
| `E_LISTING_DOTDOT` | listing | degrade | an empty, `.` or `..` segment in an **entry path** (after one leading `./` is dropped). Never raised for a link target: targets have their own `..` rule (`E_LISTING_SYMLINK_FORM`) |
| `E_LISTING_DUPLICATE` | listing | degrade | the same path twice, or a file that is also an implied directory |
| `E_LISTING_ROOT_SYMLINK` | listing | degrade | a symlink whose path has one segment |
| `E_LISTING_SYMLINK_FORM` | listing | degrade | **PD-12:** a link target whose `/`-segments are not "zero or more leading `..`, then one or more segments none of which is empty, `.` or `..`" (so `a/../b`, `./a`, `a//b`, `..`, `../..` and a trailing `/` are all rejected) |
| `E_LISTING_SYMLINK_ESCAPE` | listing | degrade | the lexically resolved target is not strictly inside the link's own top-level entry (FR-031) |
| `E_LISTING_SYMLINK_CHAIN` | listing | degrade | **PD-12:** the resolved target, or any proper prefix of it, is the path of an `l` entry (a link to a link, through a link, to itself, or a loop) |
| `E_LISTING_THROUGH_SYMLINK` | listing | degrade | **PD-12:** an entry (`f`, `d` or `l`) whose path has an `l` entry's path as a proper prefix (the entry would be written through the link) |
| `E_LISTING_TYPE` | listing | degrade | hard link `1`, devices `3`/`4`, FIFO `6`, contiguous `7`, GNU `D`/`S`/`V`/`M`, any other typeflag |
| `E_LISTING_CHECKSUM`, `E_LISTING_TRUNCATED`, `E_LISTING_CORRUPT` | `listingFromTarGz` | degrade | bad header checksum; stream ends inside a header, inside file data, or before the end-of-archive block; gzip error, non-octal / base-256 numeric field, malformed pax record |
| `E_LISTING_NOT_CANONICAL`, `E_LISTING_COUNT`, `E_LISTING_DIGEST` | `checkReleaseManifest` | degrade | a `payload.list` that is not its own canonical form; `files ≠ list.length`; `sha256 ≠ digest(list)` |
| `E_LISTING_NO_POSIX_MODES` | `listingFromDirectory` | — | called on `win32` (D4) |
| `E_SCHEMA` | `validateFeed`, `validateRelease` | — | an Ajv finding (carries `instancePath`, `keyword`) |

Exit codes of `gen-feed.js`: 0 clean, 1 abort (nothing written), 2 usage (nothing written), 3 feed written with ≥ 1
degraded plugin. `--legacy` and `--project-v1` return only 0, 1 or 2.

The table is also data (PD-17): `contract.js` exports `CODES`, a frozen object from each code to its class
(`'usage' | 'abort' | 'degrade' | 'abort-or-degrade' | 'none'`; `abort-or-degrade` for `E_URL_NOT_HTTPS` /
`E_URL_LATEST`, `none` for `E_LISTING_NO_POSIX_MODES` and `E_SCHEMA`). gen-feed maps a thrown or warned code to its exit
through `CODES`, and `hygiene.test.js` checks `CONTRACT.md` against it (§7.1).

---

## 4. Module design

### 4.1 Schemas

**`versions.v2.schema.json`** (C1; FR-001 to FR-011): `$schema` 2020-12, `$id`
`https://krateaudio.com/schemas/versions.v2.schema.json`, `$defs` for reuse:

| `$defs` name | Content |
|---|---|
| `core` | pattern `^(0\|[1-9][0-9]{0,8})\.(0\|[1-9][0-9]{0,8})\.(0\|[1-9][0-9]{0,8})$` (FR-004) |
| `tagStable` | `^[a-z0-9]+/v` + core body + `$` |
| `tagPre` | `^[a-z0-9]+/v<core>(-<pre>)?$`, `<pre>` = SemVer 2.0.0 §9 identifiers `(0\|[1-9][0-9]*\|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(\.(…))*` |
| `pubDate` | `^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$` |
| `url` | `{type: string, pattern: "^https://[^\\s]+$", not: {pattern: "/releases/latest/"}}` (measured: compiles under strict) |
| `signature` | FR-007 pattern (LF only) |
| `reservedStem` | PD-14: `{type: string, pattern: "^([Cc][Oo][Nn]\|[Pp][Rr][Nn]\|[Aa][Uu][Xx]\|[Nn][Uu][Ll]\|[Cc][Oo][Mm][0-9]\|[Ll][Pp][Tt][0-9])(\\..*)?$"}`: a reserved device name alone or followed by an extension (`NUL.txt` is `NUL`, MS naming page); used only under `not` |
| `pluginName` | `{type: string, pattern: "^[A-Z][A-Za-z0-9]{0,63}$", not: {$ref: reservedStem}}` (FR-002 + PD-14); referenced by `plugins.propertyNames` here and by C2 `name` |
| `installName` | per FR-008 field, each with `not: {pattern: "\\.\\."}` and `not: {$ref: reservedStem}` (so `Nul.vst3`, `Con.component`, preset folder `Aux` fail); `preset_subdir` pattern `^[A-Za-z0-9]([A-Za-z0-9 _-]*[A-Za-z0-9_-])?$` (no trailing space, PD-14). Two `not`s are combined as `allOf: [{not: …}, {not: …}]` |
| `install` | object, `properties` for the five names, `required: ["vst3_bundle"]` |
| `platform` | object with `url, size (integer, 1..2147483647), sha256, signature, format, min_os, payload`; `required` the five; `if: {properties: {format: {const: "tar.gz"}}}` / `then: {properties: {payload: true}, required: ["payload"]}` / `else: {properties: {payload: false}}` (PD-2 form, measured) |
| `platforms` | object, `propertyNames` `^(windows\|darwin\|linux)-[a-z0-9_]+$`, `additionalProperties: {$ref: platform}`, `patternProperties` binding format per prefix (`^windows-` → `format const inno-exe`, `^darwin-` → `pkg` + `required: ["min_os"]` with `properties: {min_os: true}`, `^linux-` → `tar.gz` + `min_os`) |
| `entryCommon` | `tag, pub_date, channel, release_notes_url, notes_markdown (type string, no maxLength), platforms, install`; `dependentRequired: {platforms: ["install"], install: ["platforms"]}`; the install conditions: `if` "some platform key starts with `darwin-`" = `{properties: {platforms: {type: "object", not: {propertyNames: {not: {pattern: "^darwin-"}}}}}, required: ["platforms"]}` → `then: {properties: {install: {type: "object", properties: {au_component: true, macos_pkg_id: true}, required: ["au_component", "macos_pkg_id"]}}}`; same shape for `windows-x86_64` → `windows_uninstall_key` (both compiled in the probe form, PD-2) |
| `entry` | `entryCommon` + `version` (core), `download_url` (url), `release_notes` (`const ""`), `channel` `const "stable"`, `prerelease` (`$ref: sibling`); required v1 keys + `tag, pub_date, channel, release_notes_url` |
| `sibling` | `entryCommon` + `tag` (`tagPre`), `channel` `const "beta"`, `version: false, download_url: false, release_notes: false` (PD-1) |

Top level: `type: object`, `required: ["schema_version", "plugins"]`, `schema_version` `const 2`, `plugins`
(`propertyNames: {$ref: pluginName}`, `additionalProperties: {$ref: entry}`), optional `manager`
(`latest_json_url` url with pattern ending `/manager/latest\.json$`). No `additionalProperties: false` anywhere (D1).

**`versions.v1.schema.json`** (FR-012): `plugins` object of objects with `required: ["version"]`, `version` string,
`download_url` / `release_notes` string when present. Nothing else.

**`release.schema.json`** (C2; FR-015, FR-016): `additionalProperties: false` at every object level; required `tag, name,
version, pub_date, assets, install`; `name` is `$ref` to the same `pluginName` definition as C1 (copied into this
schema's own `$defs`, so neither schema fetches the other); `version` = core with optional `-<pre>`; `assets` `propertyNames` enum of the five
keys, `required: ["windows-x86_64", "linux-x86_64"]` (declared in `assets.properties`, PD-2), `minProperties: 3`,
`maxProperties: 3`: three keys, two of them fixed, so exactly one other, which the enum forces to be a `darwin-*` key
(FR-016); each asset `name, url, size, sha256, signature, format`
required, with `url`, `size`, `sha256`, `signature`, `min_os` under the same definitions as C1's `platform`.
**Per-key bindings (PD-10), through `patternProperties` on `assets`, the same shape as C1's `platforms`:** `^windows-` →
`{properties: {format: {const: "inno-exe"}}}`; `^darwin-` → `{properties: {format: {const: "pkg"}, min_os: true},
required: ["min_os"]}`; `^linux-` → `{properties: {format: {const: "tar.gz"}, min_os: true}, required: ["min_os"]}`.
So a `windows-x86_64` asset with `format: "pkg"` fails C2 (`E_MANIFEST_SCHEMA`, degrade) instead of passing here and
tripping the C1 const at the self-check. `payload {files, sha256, list}` with the
PD-2 `if/then/else` on `format`; `list` `type: array, minItems: 1, items: {type: string}`.
**`install`** (FR-015 "as FR-008"): the five properties under C1's `installName` definitions, `additionalProperties: false`,
and `required: ["vst3_bundle", "au_component", "windows_uninstall_key", "macos_pkg_id"]` (`preset_subdir` optional).
That is FR-008 applied to C2, not a new rule: every C2 manifest has `windows-x86_64` and a `darwin-*` asset (FR-016), so
both of FR-008's conditional requirements always hold for the entry it becomes (PD-10). **No `channel` property**: a
manifest carrying one fails `additionalProperties: false` (FR-015, FR-018).

**C2 ⊇ C1 for copied data.** Every C1 constraint on the `platforms` / `install` that gen-feed copies from a manifest
(FR-006 to FR-009, FR-014's single-entry part) has a C2 counterpart above or in `checkReleaseManifest`. A test in
`c2-release.test.js` (`C2 release.json fixtures`, part "projection") checks it mechanically: for every **valid** C2
fixture, the entry gen-feed would assemble from it (`assembleEntry`, exported by `gen-feed.js`, the code §4.5.4 step 4 runs) passes
`checkAssembledEntry`. The step-4 backstop (§4.5.4) covers any rule this misses.

### 4.2 `tools/feed-contract/contract.js`

```js
'use strict';
const PLATFORM_KEYS = Object.freeze(['windows-x86_64', 'darwin-aarch64', 'darwin-x86_64', 'darwin-universal', 'linux-x86_64']);
const ASSET_SUFFIX = Object.freeze({ windows: '-Windows-x64.exe', darwin: '-macOS.pkg', linux: '-Linux-x64.tar.gz' }); // release.yml:524-526
const PLUGIN_NAME_RE, CORE_RE, SEMVER_RE, ASSET_NAME_RE;               // from FR-002 / FR-004 / FR-020

// --- versions
parseSemVer(str) → {major: bigint, minor: bigint, patch: bigint, pre: Array<string|bigint>} | null   // SemVer 2.0.0, no build metadata
compareSemVer(a, b) → -1 | 0 | 1                                        // precedence, https://semver.org/spec/v2.0.0.html §11
isStableCore(str) → boolean                                              // FR-004 bounded core

// --- schemas (compiled once at load with new Ajv2020({strict: true, allErrors: true}); PD-3)
validateFeed(doc, {schema: 'v2' | 'v1'}) → {valid, errors}               // v2: Ajv + signature blobs (FR-007) + C3 parse of each
                                                                          //   signature's trusted comment + FR-014 uniqueness
validateRelease(doc) → {valid, errors}                                   // Ajv only
checkReleaseManifest(doc, {repo}) → void | throws ContractError          // order: shape → checkInstallValues → checkUrl (each
    // asset) → parseMinisigText (each) → Ajv (E_MANIFEST_SCHEMA) → tag === name.toLowerCase()+"/v"+version, asset names,
    // vst3_bundle === name+".vst3", au_component === name+".component" (E_MANIFEST_SCHEMA) → per-tag URL (E_URL_NOT_PER_TAG)
    // → trusted comment === formatTrustedComment(name, version, key, asset.name) (E_TC_* / E_TC_BINDING) → payload:
    // checkCanonicalListing, files, digest (E_LISTING_*) → checkLinuxLayout (E_LAYOUT)
checkInstallValues(install) → void | throws E_INSTALL_VALUE              // runs before Ajv so the specific code wins (FR-046)
checkUrl(url, {field}) → void | throws E_URL_NOT_HTTPS | E_URL_LATEST
findInstallDuplicates(plugins) → Array<{field, value, plugins: [string, string]}>   // FR-014; entry + own sibling = one plugin;
    // vst3_bundle / au_component / macos_pkg_id compared as value.toLowerCase() (ASCII fold; the patterns admit ASCII
    // only), windows_uninstall_key exactly (PD-13); `value` reports the first plugin's spelling
isPluginName(str) → boolean                                              // FR-002 pattern and not a reserved device stem (PD-14);
                                                                          //   gen-feed's E_PLUGIN_NAME uses it, the schemas encode the same rule
definedPlatformKeys(entryOrSibling) → string[]                           // the PLATFORM_KEYS present in its platforms (§3.1)
checkAssembledEntry(name, entry) → void | throws E_MANIFEST_SCHEMA       // validateFeed({schema_version: 2, plugins: {[name]: entry}})
    // must be valid; else E_MANIFEST_SCHEMA carrying the first finding's instancePath (PD-10 backstop, §4.5.4 step 4)
const CODES;                                                              // §3.2 table as data (PD-17)

// --- C3
formatTrustedComment(name, version, platform, asset) → string            // validates inputs, throws E_TC_*
parseTrustedComment(str) → {name, version, platform, asset}              // throws E_TC_*
checkBinding(trustedComment, {name, version, platform, asset}) → void    // throws E_TC_BINDING naming the first differing field
parseMinisigText(text) → {untrustedComment, algorithm: 'ED', keyId: Buffer(8), trustedComment}  // LF or CRLF; no crypto
toLfMinisig(text) → string                                               // D7 normalisation for writers (Phase 2 reuses it)

module.exports = { ContractError, CODES, PLATFORM_KEYS, parseSemVer, compareSemVer, isStableCore, validateFeed, validateRelease,
  checkReleaseManifest, checkInstallValues, checkUrl, findInstallDuplicates, isPluginName, definedPlatformKeys,
  checkAssembledEntry, formatTrustedComment, parseTrustedComment,
  checkBinding, parseMinisigText, toLfMinisig, assetNameFor /* (name, version, key) */, perTagUrl /* (repo, tag, asset) */ };
```

`parseMinisigText` splits on `/\r?\n/`, requires exactly four non-empty lines plus an optional trailing empty string,
prefix `untrusted comment: ` on line 1 and `trusted comment: ` (17 bytes, untrimmed rest of line, as
`minisign-verify` reads it, spec External facts) on line 3, base64 via `Buffer.from(s, 'base64')` with a round-trip
check (`buf.toString('base64') === s`, so non-canonical base64 is rejected), 74-byte blob starting `0x45 0x44`, 64-byte
global signature. In C1/C2 values a CR anywhere is `E_SIGNATURE_TEXT` (the schema pattern already forbids it; the code
check gives the named code for `validateFeed`).

### 4.3 `tools/feed-contract/listing.js`

```js
'use strict';
/** @typedef {{type: 'f'|'d'|'l'|'other', mode: number, path: string|Buffer, target?: string|Buffer, typeflag?: string}} Entry
 *  A Buffer path / target is decoded here with TextDecoder('utf-8', {fatal: true}) (E_LISTING_PATH on failure), so the
 *  UTF-8 rule lives in the shared function; 'other' (with the raw typeflag) is rejected here as E_LISTING_TYPE */
listingFromEntries(entries: Entry[]) → string[]   // canonical lines, no terminators: validate (FR-031), drop root '.',
                                                  // strip one leading './', add implied ancestors as 'd 0755', normalise
                                                  // modes (D3: f → 0755 if mode & 0o111 else 0644, d → 0755, l → 0777),
                                                  // detect duplicates / file-dir clashes, sort by Buffer.compare(utf8(path))
listingFromTarGz(buffer: Buffer) → string[]       // zlib.gunzipSync, then readTarEntries, then listingFromEntries
listingFromDirectory(root: string) → string[]     // win32: throw E_LISTING_NO_POSIX_MODES first; lstat walk, readlink, never follows
digest(lines: string[]) → string                  // sha256 hex of lines.map(l => l + '\n').join('') as UTF-8
toText(lines) → string                            // the committed .listing form (LF after each line)
parseListingLine(line) → Entry                    // '<t> <mmmm> <path>[ -> <target>]'; path = everything after the second space
checkCanonicalListing(lines) → void               // parse every line, re-canonicalise, require identical (E_LISTING_NOT_CANONICAL)
checkLinuxLayout(lines, {vst3Bundle}) → void      // FR-033, E_LAYOUT naming the item
module.exports = { listingFromEntries, listingFromTarGz, listingFromDirectory, digest, toText, parseListingLine,
                   checkCanonicalListing, checkLinuxLayout, readTarEntries /* exported for the record-presence asserts */ };
```

**Tar reader (`readTarEntries(tarBuffer)`)**, pure, reads headers and skips data, never writes:

1. Walk 512-byte blocks. An all-zero block ends the archive; the stream must contain one (`E_LISTING_TRUNCATED`
   otherwise). A block that is not all zero but shorter than 512 bytes, or data that runs past the buffer end, is
   `E_LISTING_TRUNCATED`.
2. Checksum: parse `chksum[8]@148` as octal (NUL/space terminated); compute the unsigned byte sum with bytes 148-155 as
   0x20; accept when equal (also accept the signed-char sum, which historic tars wrote; GNU tar accepts both).
   Otherwise `E_LISTING_CHECKSUM`.
3. Numeric fields (`mode`, `size`) are octal digits terminated by NUL/space; a base-256 (high bit set) or non-octal
   field is `E_LISTING_CORRUPT`.
4. Name: `name[100]` NUL-terminated. **Only when `magic[6]@257` is `"ustar\0"` and `version[2]@263` is `"00"`** (POSIX)
   is a non-empty `prefix[155]@345` joined as `prefix + "/" + name`. With the old-GNU magic `"ustar  \0"` the prefix
   bytes are `atime`/`ctime` (GNU manual, §1.2), so they are ignored. The join is tested by the `ustar-prefix` good
   fixture (PD-18). Names and link targets are passed on as `Buffer`s; `listingFromEntries` decodes them with
   `TextDecoder('utf-8', {fatal: true})` → `E_LISTING_PATH` on failure.
5. Typeflag: `'0'` / `'\0'` → f; `'5'` → d; `'2'` → l (target = `linkname`); `'L'` / `'K'`: the data (NUL-terminated) is
   the next header's name / link target; `'x'`: parse records `"<len> <key>=<value>\n"` (length counts the whole record,
   POSIX pax), apply `path` / `linkpath` to the next header, ignore other keys, a malformed record (non-decimal length,
   a length that overruns the data or does not end at an LF, no `=`) is `E_LISTING_CORRUPT`; `'g'`: skipped (global
   header ignored, FR-032); every other typeflag becomes `{type: 'other', typeflag}`, which `listingFromEntries`
   rejects with `E_LISTING_TYPE` (so the in-memory table of §7.1 SC-008 reaches the same check).
6. Precedence for the next real header: pax `path` > GNU `L` > ustar name(+prefix); same for link targets.
   A pending `L`/`K`/`x` with no following header is `E_LISTING_TRUNCATED`.

The record-presence asserts (FR-058) call `readTarEntries` with a `{raw: true}` option that also returns the sequence of
typeflags seen and, per header, the raw `magic`, `version` and `prefix` bytes, so `long-name` must show `L`,
`long-link` `K`, `pax-path` an `x` whose records carry both `path` and `linkpath`, `pax-global` a `g`, and
`ustar-prefix` (PD-18) a regular-file header with magic `ustar\0` at 257, version `00` at 263 and a non-empty `prefix`
at 345, whose joined path equals the `.listing` line (so a missing `/`, a swapped join or an inverted magic gate fails).

**FR-031 checks** run in `listingFromEntries`, so all three sources share them, in this order (the first failure wins,
which fixes the code each bad fixture expects):

1. **Per entry, path:** decode (`E_LISTING_PATH` on invalid UTF-8); empty, NUL / CR / LF or ` -> ` → `E_LISTING_PATH`;
   leading `/` → `E_LISTING_ABSOLUTE`; `^[A-Za-z]:` → `E_LISTING_DRIVE`; `\` → `E_LISTING_BACKSLASH`; after one leading
   `./` is dropped (and a single trailing `/` on a `d` entry), an empty, `.` or `..` segment → `E_LISTING_DOTDOT`;
   `type: 'other'` → `E_LISTING_TYPE`.
2. **Per `l` entry, link:** path with one segment → `E_LISTING_ROOT_SYMLINK`. Target: decode, empty / NUL / CR / LF →
   `E_LISTING_PATH`; leading `/` → `E_LISTING_ABSOLUTE`; drive → `E_LISTING_DRIVE`; `\` → `E_LISTING_BACKSLASH` (the
   FR-031 character rules); then the **form rule (PD-12)**: split on `/`, the segments must be zero or more `..`
   followed by at least one segment that is not empty, `.` or `..`, else `E_LISTING_SYMLINK_FORM`; then resolve
   lexically, `path.posix.normalize(path.posix.join(path.posix.dirname(linkPath), target))`: the result must not start
   with `../`, must not be `.`, its first segment must equal the link's own first segment **and** it must have more
   than one segment (strictly inside), else `E_LISTING_SYMLINK_ESCAPE`. The spec's fixture
   `<Name>.vst3/Contents/x -> ../../README.txt` passes the form rule (leading `..` only) and fails containment, so it
   proves `E_LISTING_SYMLINK_ESCAPE`, as FR-058 requires.
3. **Across entries**, in this order: (a) an entry (`f`, `d` or `l`) whose path has an `l` entry's path as a proper
   segment prefix → `E_LISTING_THROUGH_SYMLINK` (checked **before** implied ancestors are added, so `l A/b/c/t` plus
   `f A/b/c/t/evil` reports this code, never a file-dir clash); (b) implied ancestors added, then duplicates /
   file-dir clashes → `E_LISTING_DUPLICATE`; (c) an `l` whose resolved target, or any proper segment prefix of it,
   equals the path of an `l` entry (itself included) → `E_LISTING_SYMLINK_CHAIN`.

Why the three PD-12 rules close the hole: with no entry under a link (rule 3a), every ancestor of a link is a real
directory, so the leading `..` run (the only `..` the form rule allows) resolves the same lexically and on disk; the
remaining segments descend, and since no prefix of the result is a link (rule 3c), the descent never passes through
one. Lexical and physical resolution therefore agree for every accepted listing. The reviewer's chain
(`l A/b/c/s -> ../../d`, `l A/b/c/t -> s/../../../x`) fails the form rule on `t` (a `..` after `s`); a variant
`l A/b/c/t -> s/x` fails the chain rule; `f A/b/c/t/evil` fails the through rule. `CONTRACT.md` §6 states all of this
as the listing-time contract **and** says that the Phase 8 extractor must apply the same rules to the archive it
actually extracts and then check, after extraction, that `fs.realpath` (Rust: `std::fs::canonicalize`) of every
extracted entry stays under the staging root: the listing rules make an escape impossible on paper, the physical check
makes it impossible on disk.

**`listingFromDirectory`**: recursive `fs.readdirSync(dir, {withFileTypes: true})` + `fs.lstatSync`; relative paths are
built from the walk (`path.posix.join` of entry names), never from `realpath`, so macOS `/var` → `/private/var` cannot
appear in a line (SC-009); a symlink contributes `fs.readlinkSync` as its target; any other `lstat` type
(`isFIFO`, `isSocket`, block / char device) is `E_LISTING_TYPE`.

### 4.4 `CONTRACT.md` outline

1. Scope and readers (the nine shipped parsers, `gen-feed.js`, Phase 2 tools, Phase 5 Rust client, Phases 7-8).
2. C1 field table: path · type · required · rule · writer (gen-feed input) · reader (FR-013), one row per field, the
   `prerelease` sibling, `manager`.
3. Reader rules: ignore unknown keys; platform resolution (native key, then `darwin-universal`); manager v1 ignores
   `prerelease`; asset name = URL's last segment (D8); install uniqueness (FR-014, `E_INSTALL_DUPLICATE`, case-folded
   for `vst3_bundle` / `au_component` / `macos_pkg_id`, PD-13, inherited by Phase 5); a v1-only entry, or an entry with
   no platform under a defined key (`definedPlatformKeys` empty), means "not available through the manager"; feed
   names never name a Windows reserved device (PD-14).
4. C2 schema summary, the cross-field rules of FR-017, the `min_os` departure (FR-016), no `channel` (FR-018: the release
   list's `isPrerelease` is the only channel source; promotion = unticking the GitHub flag); C2 ⊇ C1 for copied data
   (PD-10).
5. C3 grammar, the binding rule, `.minisig` text form (D7), the KAT and how it was made.
6. C4 layout, canonical listing grammar, mode normalisation (D3), path and symlink rules in their check order (§4.3,
   including PD-12's form, through-symlink and chain rules), digest; the Phase 8 obligations (same rules on the archive
   it extracts, plus a physical `realpath` containment check after extraction); Windows and macOS have no archive
   layout (FR-034).
7. gen-feed: modes, the Q9 rule, the CHANGELOG cut (PD-4), the code table of §3.2 (one row per `CODES` key with its
   class), the exit codes 0 / 1 / 2 / 3.

Headings 1-8 use fixed titles (`## 1. Scope and readers`, …, `## 8. Fixture signatures`), which `hygiene.test.js`
checks together with the code table and the C1 field table (PD-17, §7.1).
8. Fixture signatures are **structurally valid only**: their blobs are zero-filled, so later phases must not expect them
   to verify cryptographically; the KAT is the only cryptographic fixture.

### 4.5 `tools/gen-feed.js`

#### 4.5.1 CLI

```
usage: node tools/gen-feed.js --plugins-dir <dir> --releases <file> --manifests <dir> --repo <owner/name>
                              [--manager-latest-url <url>] --out <file>
       node tools/gen-feed.js --legacy --plugins-dir <dir> --tags <file> --out <file>
       node tools/gen-feed.js --project-v1 <feed>
```

`#!/usr/bin/env node`, a header comment in the `tools/*.js` style (`tools/header-consumers.js:1-16`), the usage line
above. Argument parsing is a hand loop like `tools/run-gate.js:41-47`; an unknown flag, a missing value, a repeated flag
or two modes at once is `E_USAGE` (exit 2, usage on stderr, nothing written).

```js
/** @param {string[]} argv  @param {{stdout: {write(s: string): void}, stderr: {write(s: string): void}}} io
 *  @param {{beforeSelfCheck?: (doc: object) => void}} [hooks]  test-only (PD-16), passed through to buildFeed
 *  @returns {0|1|2|3} */
function main(argv, io, hooks = {}) { … }
if (require.main === module) process.exitCode = main(process.argv.slice(2), process);
module.exports = { main, buildFeed, buildLegacyFeed, projectV1, gitVersionCompare, readPluginsDir, cutChangelogSection,
                   extractAppId, assembleEntry };
```

`hooks.beforeSelfCheck` is the only seam into the self-check: it receives the assembled document right before step 7
(§4.5.4) and may mutate it; the script entry never passes it, so production runs are unaffected. It exists so a test
can prove that the document that would be written is the document that was checked (SC-010 `E_OUTPUT_SCHEMA` subtest).

`main` does all file IO: it reads inputs into plain values, calls the pure core, and writes `--out` with
`writeAtomic(outPath, text)`: `fs.writeFileSync(tmp, text)` where `tmp = path.join(dirname(out), '.' + basename(out) +
'.' + process.pid + '.' + crypto.randomBytes(4).toString('hex') + '.tmp')`, then `fs.renameSync(tmp, out)` (replaces on
Windows, measured); on any throw the temp file is unlinked and the error is reported as `E_INPUT` naming `--out`.
`--project-v1` writes to stdout only. Stderr lines: `gen-feed: <CODE>: <message>` (abort) as the **first** line,
`gen-feed: WARN <CODE>: <message>` (degrade) one per degraded plugin.

#### 4.5.2 Pure core

```js
/** @typedef {{dir: string, versionJson: object, changelog?: string, setupIss?: string}} PluginInput
 *  dir = subdirectory name; versionJson = parsed; changelog / setupIss = file text when the file exists */
readPluginsDir(dir, {withSetupIss: boolean}) → PluginInput[]   // IO helper used by main: subdirectories that contain
    // version.json, sorted by Buffer.compare of the name; parses JSON (E_INPUT naming the file)

/** @returns {{doc: object, text: string, warnings: Array<{code, plugin, file?, field?, message}>}} */
buildFeed({ plugins: PluginInput[], releases: Array<{tagName, isPrerelease, isDraft, publishedAt}>,
            manifests: Array<{file: string, text: string}>, repo: string, managerLatestUrl?: string }, hooks = {})
assembleEntry({ plugin: PluginInput, stable, sibling?, stableManifest?, siblingManifest?, repo })
    → {entry: object, warning?: {code, plugin, file?, field?, message}}  // §4.5.4 step 4 for one plugin, checks included
buildLegacyFeed({ plugins: PluginInput[], tagsText: string }) → string
projectV1(doc) → string                                   // FR-047, formatted like FR-040
cutChangelogSection(text, version) → string | undefined   // PD-4; CRLF → LF; trims leading/trailing blank lines
extractAppId(setupIssText) → string | undefined           // first /^AppId=\{\{([0-9A-F-]{36})\}[ \t]*$/m on CRLF-normalised
    // text → "{GUID}" (Inno's doubled "{{" read as one "{", SC-025d). A single-brace "AppId={GUID}" does not match
    // (Inno would read "{" as the start of a constant), so it counts as "no AppId line" → E_MANIFEST_MISMATCH
```

`buildFeed` throws `ContractError` for abort-class codes; degrade-class findings become `warnings`. `main` maps a
thrown code to exit 1, a non-empty `warnings` to exit 3.

#### 4.5.3 Legacy mode (FR-040) — byte-exact emulation of `release.yml:645-685`

1. `tagsText` → lines (`/\r?\n/`, one optional final empty line). A line that is empty (not last), contains whitespace,
   contains `^`, or starts with `refs/` → `E_INPUT` naming the 1-based line.
2. For each plugin in directory order: `pluginLower = versionJson.name.toLowerCase()`; candidates = tags starting with
   `pluginLower + '/v'` (what `git tag -l "<p>/v*"` matches for these names); sort descending with `gitVersionCompare`;
   take the first; none → skip. `version = tag.replace(/.*\/v/, '')` (the same greedy regex, `release.yml:672`).
3. `plugins[name] = {version, download_url: versionJson.url, release_notes: ''}` — built exactly so, so an absent
   `url` is dropped by `JSON.stringify` exactly as today; a repeated `name` overwrites in place as the JS object does
   today. Legacy mode validates nothing else: its job is to show what today's step would publish.
4. `JSON.stringify({schema_version: 1, plugins}, null, 2) + '\n'`.

`gitVersionCompare(a, b)`: an **independent implementation of the documented `strverscmp` semantics** (§1.2, glibc
manual), not a transcription of git's or glibc's code (git's `versioncmp.c` is GPLv2-relicensed glibc code; the repo
must not copy it). Algorithm: walk both strings to the first differing byte; if the byte position is not inside a digit
run in both strings, return the byte difference (with end-of-string as 0, so a string that is a prefix of the other
sorts first: `1.1.0` < `1.1.0-beta.1`, which is why git ranks the suffixed tag above its release, spec FR-040); if both
sides are in digit runs, extend both runs backwards over the common digits and forwards to their ends, then apply the
leading-zero rules (more leading zeros → smaller; no leading zeros → longer run larger, equal length → first differing
digit; equal positive zero count → lexicographic with the shorter-run extension rule). SC-004 checks it against real
`git` on ≥ 20 sets, including leading zeros and `-rc1` / `-beta.1` / `-beta.10`, on three OSes; that test, not the
prose, is the authority.

#### 4.5.4 v2 mode (FR-041 to FR-049)

Per run:

1. **Validate inputs (abort class).** Each plugin: `name` passes `isPluginName` (FR-002 + PD-14, `E_PLUGIN_NAME`); `url` is a string (`E_V1_TYPE`)
   and passes `checkUrl` (`E_URL_NOT_HTTPS` / `E_URL_LATEST`, abort because it is the v1 `download_url`); names unique
   after `toLowerCase()` (`E_DUPLICATE_PLUGIN` naming both directories, PD-13). `releases` is an array of objects with string `tagName`, boolean `isPrerelease` / `isDraft`,
   unique `tagName`s (`E_INPUT`); `--repo` matches `^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$` (`E_INPUT`).
2. **Select releases (FR-042, the Q9 rule, only here).** Drop drafts. For each plugin, releases whose tag starts with
   `<name lowercased>/v`; version = the rest. A non-prerelease must be a bounded core, a prerelease core + optional
   suffix (`E_TAG_FORMAT` naming tag and plugin); a non-draft `publishedAt` must match the `pub_date` pattern (`E_INPUT`).
   Stable = highest `compareSemVer` among non-prereleases; sibling = highest prerelease **strictly above** stable. No
   stable → plugin omitted. Tags matching no plugin are ignored.
3. **Index manifests.** Parse each `manifests/*.json` (sorted bytewise). Unparseable (not JSON, or JSON whose `tag` is
   not a string) → the file is ignored and one **unattributed** WARN is printed, `gen-feed: WARN E_MANIFEST_SCHEMA:
   manifests/<file>: <reason>` (PD-8): the file cannot be tied to a plugin, so the line names the file and no plugin,
   and `buildFeed` records it as `{code, plugin: null, file}`. Map by `doc.tag`; two files with one tag → both dropped;
   when that tag is a selected stable or sibling tag, its plugin degrades with `E_MANIFEST_DUPLICATE` (one WARN naming
   the plugin and both files), otherwise both are ignored silently like any unmatched manifest. A manifest whose tag is
   not a selected stable or sibling tag is ignored silently (spec Edge cases). Exit 3 is "at least one WARN line", which
   equals "at least one degraded plugin" except for an unattributed WARN; the PD-8 ruling (Open question 1) decides
   whether SC-023's "naming the plugin" is amended to "naming the plugin, or the file when no plugin can be named".
4. **Assemble each entry** in the FR-043 key order: `version, download_url, release_notes: "", tag, pub_date, channel:
   "stable", release_notes_url: "https://github.com/" + repo + "/releases/tag/" + tag, notes_markdown?, platforms?,
   install?, prerelease?`; the sibling: `tag, pub_date, channel: "beta", release_notes_url, notes_markdown?, platforms?,
   install?`. `notes_markdown` = `cutChangelogSection(changelog, ownVersion)`, omitted when absent or empty. For the
   stable manifest and then the sibling manifest: `checkReleaseManifest(doc, {repo})`, check 1 (gated on
   `versionJson.version === manifest.version`), check 2 (`extractAppId(setupIss)`, `windows_uninstall_key === AppId +
   "_is1"`). The first failure in either manifest degrades the plugin: no `platforms` / `install` on the entry **or** the
   sibling, one WARN line. Otherwise `platforms[key]` = asset in the order `url, size, sha256, signature, format, min_os?,
   payload? {files, sha256}` (no `name`, no `list`), keys sorted bytewise; `install` = the manifest's object as parsed.
   **Last degrade-class check (PD-10 backstop):** `checkAssembledEntry(name, entry)`, i.e. the assembled entry, with its
   sibling, validated on its own as a one-plugin C1 document. A failure degrades the plugin with `E_MANIFEST_SCHEMA`
   (WARN naming the plugin and the first finding's `instancePath`), exactly like a C2 failure, so a manifest rule that
   C2 misses can never reach the step-7 abort. With PD-10's C2 bindings no current fixture reaches it; it is tested
   directly (`gen-feed rejects` → subtest `assembled entry failing C1 degrades`, §7.1).
5. **Uniqueness (FR-014, FR-049).** `findInstallDuplicates` over the non-degraded plugins; every plugin in a reported pair
   degrades with `E_INSTALL_DUPLICATE` (one WARN per plugin, naming field and both plugin keys). Degrading removes
   `install`, so one pass suffices.
6. **Document.** `{schema_version: 2, plugins, manager?: {latest_json_url}}` (`--manager-latest-url` passes `checkUrl` and
   the `/manager/latest.json` suffix, else `E_INPUT`).
7. **Self-check (FR-045).** `validateFeed(doc, {schema: 'v2'})` and `validateFeed(doc, {schema: 'v1'})` must both be valid,
   else `E_OUTPUT_SCHEMA` with the first finding. `hooks.beforeSelfCheck?.(doc)` runs immediately before this step
   (PD-16). `text = JSON.stringify(doc, null, 2) + '\n'`.

Abort beats degrade: every abort-class check (steps 1, 2, 6, 7) runs before anything is written, so a run with both
returns 1 and writes nothing (FR-049, SC-023).

#### 4.5.5 Projection (FR-047)

`projectV1(doc)`: `{schema_version: 1, plugins: {k: {version, download_url, release_notes}}}` in `doc.plugins` order,
copying only keys present (a v2 entry always has all three), formatted like FR-040. The CLI form reads the file (`E_INPUT`
on parse failure or a missing `plugins` object).

### 4.6 Write guard — `tools/feed-contract/tests/helpers/write-guard.js`

```js
'use strict';
// Installs once per process (globalThis[Symbol.for('krate.writeGuard')]).
const TMP = fs.realpathSync.native(os.tmpdir());   // lower-cased on win32
function toPathString(p) → string     // string | Buffer | URL (fileURLToPath)
function rejectDotDot(s, op)          // PD-15 (a): any '..' segment (split on '/', and also '\' on win32), or a win32
                                      // '\\?\' / '\\.\' prefix (they bypass Win32 normalisation) → E_WRITE_OUTSIDE_TMP,
                                      // before anything is resolved
function canonicalFollow(s) → string  // path.resolve (safe: no '..' left), then realpath.native of the nearest existing
                                      // ancestor (the path itself when it exists) + the remaining components; a final
                                      // symlink is followed, as the write itself would follow it
function canonicalEntry(s) → string   // canonicalFollow(dirname) + '/' + basename: for operations that act on the
                                      // directory entry itself and never follow it (unlink, rm, rmdir, rename, the new
                                      // path of symlink / link, lchmod, lutimes, lchown)
function assertUnderTmp(p, op, {entry = false} = {})  // throws Error with .code 'E_WRITE_OUTSIDE_TMP' unless the
                                      // canonical form === TMP or startsWith(TMP + path.sep)
```

Because `..` is refused before resolution, `path.resolve` can no longer collapse `<tmp>/L/../evil` lexically while the
kernel follows `L` first (the bypass the 2026-10-30 review reproduced in WSL); an intermediate symlink `L` that points
outside makes `canonicalFollow` land outside, so the call throws. Test code builds paths with `path.join`, which never
leaves a `..` segment in a path under a temp root, so no legitimate call is refused.

Wrapped (sync, callback and `fs.promises` forms where they exist): `writeFile`, `appendFile`, `mkdir`, `mkdtemp` and
`mkdtempDisposable` (the prefix's directory; both exist in Node v24.7.0, measured), `rename` (both paths, entry form),
`copyFile` / `cp` (destination), `rm`, `rmdir`, `unlink` (entry form), `symlink` (the new path, entry form), **`link`
(the new path, entry form, and the existing path, follow form: a hard link to a repo file would otherwise let every
later write in tmp modify the repo file's inode, PD-15 (b))**, `truncate`, `chmod`, `lchmod` (entry form; macOS only),
**`chown`, `lchown` (entry form)**, `utimes`, `lutimes` (entry form), `createWriteStream`, `open` with any flag other
than `'r'` (string flags other than `r`, or numeric flags with `O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND`).
fd-based calls (`fchmod`, `fchown`, `futimes`, `write`, `ftruncate`) act on a descriptor that only a guarded `open`
can have produced for writing, so they are not wrapped.

**`child_process` (PD-15 (c)), an allowlist, not a cwd check alone:**
- `exec`, `execSync` (shell strings) and `fork` always throw; any call with `options.shell` truthy throws.
- `spawn`, `spawnSync`, `execFile`, `execFileSync`: `options.cwd` must be given and pass `assertUnderTmp`; the command
  must be exactly `'git'` or `'tar'` (no path, no other executable); `options.env` must not set `GIT_DIR`,
  `GIT_WORK_TREE` or `GIT_INDEX_FILE`.
- `git`: the arguments may start with `-c user.name=…` / `-c user.email=…` pairs only, then a subcommand from
  `init`, `commit`, `update-ref`, `tag` (what SC-004 runs, §7.1); `-C`, `--git-dir`, `--work-tree` (with or without
  `=`) anywhere throw.
- `tar`: the arguments must be exactly `['-xzf', <source>, '-C', <dest>]` (what SC-009 runs) with `<dest>` passing
  `assertUnderTmp`; `<source>` is read-only and may be a committed fixture in the checkout.

The guard file is the only scanned file allowed to `require('child_process')` besides the two SC-004 / SC-009 test
files (SC-015a); `tests/helpers/fixtures.js` is scanned and has no exemption (§2).

### 4.7 Fixtures

**Builder (PD-7) — `fixtures/make-fixtures.js`.** `node tools/feed-contract/fixtures/make-fixtures.js --out <dir>` writes
the JSON fixtures of `feeds/`, `release/`, `trusted-comments.json`, `git-versionsort.json` and every gen-feed case's
**inputs** (`case.json`, `plugins/…/version.json`, `CHANGELOG.md`, `setup.iss`, `releases.json`, `tags.txt`,
`manifests/`) from tables in the script; `--check` writes into an `fs.mkdtemp` dir and compares byte-for-byte with the
committed tree (exit 1 naming the first differing file). Synthetic signatures:
`untrusted comment: krate fixture signature (not cryptographic)\n` + base64(`ED` ‖ 8 × 0x00 ‖ 64 × 0x00) + `\ntrusted
comment: <C3 comment>\n` + base64(64 × 0x00) + `\n`. sha256 values are the real SHA-256 of a stated synthetic content
string, so later phases can build matching stub assets. It is not run by the gate except through the drift test; it
imports nothing outside `node:` modules and `../contract.js` (for `formatTrustedComment`, `assetNameFor`, `perTagUrl`), and
it contains no plugin name of the real nine (fixture names: `Alpha`, `Bravo`, …, the KAT's `KatPlugin`, the C++ case's
`FeedV2Probe`; SC-014 scans only the five product files, but keeping fixtures roster-free avoids a confusing hit).

**C1 invalid rows (PD-11), one per schema or code rule.** Each id below is in `C1_RULES` (`c1-feed.test.js`), and
`make-fixtures.js` writes at least one `feeds/invalid/<id-slug>.json` for it: a valid full-v2 base document with exactly
one change. The expected value is the code or the Ajv `instancePath keyword` pair the row carries; the table gives the
change only. FR-055's minimum set is a subset (marked ★).

| FR | Rule ids (`FR-0xx/…`) and the one change each row makes |
|---|---|
| FR-001 | `malformed-json` ★ (`E_INPUT`); `schema_version-const` (`schema_version: 1` in a v2 row); `plugins-required` (no `plugins`) |
| FR-002 | `plugin-key-pattern` (key `alpha`); `plugin-key-reserved` (key `Nul`, PD-14) |
| FR-003 | `version-type` ★ (number); `download_url-required`; `release_notes-const` ★ (`"x"`); `tag-required`; `pub_date-required`; `channel-required`; `release_notes_url-required`; `channel-stable-const` (`"beta"` on the entry); `notes_markdown-type` ★ (array); `platforms-requires-install` ★; `install-requires-platforms` |
| FR-004 | `version-suffix` ★ (`1.1.0-beta.1`); `version-ten-digits` ★ (`4294967296.0.0`); `version-leading-zero` ★ (`01.2.3`) |
| FR-005 | `tag-pattern` (`Alpha/v1.0.0`); `tag-stable-suffix` (`alpha/v1.0.0-rc1` on the entry); `pub_date-pattern` (`2026-10-29T00:00:00+01:00`); `url-https` ★ (`http://` asset URL); `url-whitespace` (a space in `download_url`); `url-latest` ★ (`/releases/latest/`) |
| FR-006 | `platform-key-pattern` (`macos-arm64`); `platform-url-required`; `platform-size-zero` (`0`); `platform-size-max` (`2147483648`); `platform-size-type` (`1.5`); `platform-sha256-length` (63 hex); `platform-sha256-hex` (upper-case / `g`); `format-windows` ★ (`windows-x86_64` `pkg`); `format-darwin` (`darwin-universal` `inno-exe`); `format-linux` (`linux-x86_64` `pkg`); `min_os-darwin-required` ★; `min_os-linux-required`; `min_os-pattern` (`14`) |
| FR-007 | `signature-legacy-Ed` ★ (`Ed` blob, `E_SIGNATURE_TEXT`); `signature-cr` (CRLF text); `signature-blob-length` (a 73-byte blob, `E_SIGNATURE_TEXT`); `signature-noncanonical-base64` (`E_SIGNATURE_TEXT`); `signature-trusted-comment` (a trusted comment failing C3, `E_TC_FIELDS`); `signature-no-final-lf` |
| FR-008 | `install-dotdot` ★, `install-slash` ★, `install-backslash` ★ (`E_INSTALL_VALUE`); `vst3_bundle-required`; `uninstall-key-pattern` ★ (lower-case hex); `pkg-id-pattern` ★ (`com.other.alpha`); `darwin-requires-au_component`; `darwin-requires-macos_pkg_id`; `windows-requires-uninstall-key`; `bundle-reserved-stem` (`Nul.vst3`, PD-14); `component-reserved-stem` (`Con.component`); `preset_subdir-reserved` (`Aux`); `preset_subdir-trailing-space` (`Kits `, PD-14) |
| FR-009 | `payload-tar-gz-required` ★; `payload-inno-exe-forbidden` ★; `payload-pkg-forbidden`; `payload-files-min` (`files: 0`); `payload-sha256-pattern` |
| FR-010 | `sibling-version-forbidden` ★; `sibling-download_url-forbidden`; `sibling-release_notes-forbidden`; `sibling-channel-const` (`"stable"`); `sibling-tag-required`; `sibling-tag-pattern`; `sibling-platforms-requires-install` |
| FR-011 | `manager-latest_json_url-required`; `manager-latest_json_url-suffix` (`…/manager/feed.json`); `manager-latest_json_url-https` (`http://`) |
| FR-012 | `v1-version-required` and `v1-version-type` (`schema: "v1"` rows: the compatibility floor rejects what the shipped parser cannot read) |
| FR-014 | `dup-vst3_bundle` ★, `dup-au_component` ★, `dup-uninstall-key` ★, `dup-pkg-id` ★ (`E_INSTALL_DUPLICATE`, SC-026); `dup-vst3_bundle-casefold` (`Alpha.vst3` / `ALPHA.vst3`, PD-13) |

FR-013 is prose (`CONTRACT.md`, checked by the PD-17 hygiene test). Valid rows carry ids too (`FR-055/v1-only`,
`FR-055/full-v2`, `FR-055/mixed`, `FR-055/prerelease-suffixed`, `FR-055/prerelease-plain`, `FR-055/notes-14000`,
`FR-055/tenth-plugin`, `FR-055/unknown-platform-key` with `expect: "accept-without-platforms"` and `plugin` naming the
entry whose only platform is `windows-aarch64`, `FR-055/unknown-extra-keys`, `FR-014/sibling-same-install`). The
SC-005 row count rises from ≥ 29 to the table's size; the ≥ 29 threshold stays as a floor.

**C2 invalid rows (PD-11), `C2_RULES` in `c2-release.test.js`.** Each `release/invalid/<id-slug>.json` is a valid
manifest with one change; expected code in brackets.

| FR | Rule ids and changes |
|---|---|
| FR-015 | `tag-required`, `name-required`, `version-required`, `pub_date-required`, `assets-required`, `install-required` [`E_MANIFEST_SCHEMA`]; `channel-stable`, `channel-beta` (FR-018) and `unknown-key` [`E_MANIFEST_SCHEMA`]; `name-reserved` (`Nul`) [`E_MANIFEST_SCHEMA`] |
| FR-016 | `missing-asset` (no `linux-x86_64`); `fourth-asset`; `two-darwin-keys`; `format-windows-pkg`, `format-darwin-inno-exe`, `format-linux-pkg` (PD-10); `min_os-linux-required`, `min_os-darwin-required`; `payload-tar-gz-required`, `payload-pkg-forbidden`; `list-empty` [all `E_MANIFEST_SCHEMA`]; `install-missing-au_component`, `install-missing-macos_pkg_id`, `install-missing-uninstall-key`, `install-missing-vst3_bundle` (PD-10) [`E_MANIFEST_SCHEMA`]; `install-dotdot` [`E_INSTALL_VALUE`] |
| FR-017 | `tag-mismatch`, `asset-name`, `vst3_bundle-name`, `au_component-name` [`E_MANIFEST_SCHEMA`]; `url-other-tag` [`E_URL_NOT_PER_TAG`]; `url-latest` [`E_URL_LATEST`]; `url-http` [`E_URL_NOT_HTTPS`]; `signature-text` [`E_SIGNATURE_TEXT`]; `binding` [`E_TC_BINDING`]; `list-not-canonical` (the `list` reversed, `payload.sha256` = `digest` of the reversed lines and `files` = its length, so only canonicality fails) [`E_LISTING_NOT_CANONICAL`]; `list-bad-path` (a line with `..`) [`E_LISTING_DOTDOT`]; `files-count` [`E_LISTING_COUNT`]; `digest` [`E_LISTING_DIGEST`]; `layout` [`E_LAYOUT`] |

Valid C2 rows: one per macOS key variant, one without `min_os` on `windows-x86_64`, one with a prerelease-suffixed
version, one plain three-part (FR-056). The C2 test also runs the "C2 ⊇ C1" projection part over every valid row (§4.1).

**gen-feed `case.json`:**
`{"group": "clean"|"prerelease"|"reject"|"degrade", "modes": ["v2","legacy"], "repo": "rolandzwaga/krate-audio",
"managerLatestUrl"?: "…", "exit": 0|1|3, "error"?: "E_…", "warn"?: [{"code": "E_…", "plugin": "Name" | null, "file"?:
"manifests/<f>.json"}], "legacyExpect"?: {...SC-003 column}}`. `plugin: null` (with `file`) is an unattributed WARN
(PD-8, §4.5.4 step 3); the test matches it against a stderr line that names the file. The snapshot case uses the same
shape.

**Cases added by this revision** (inputs in the `make-fixtures.js` tables, goldens per "Goldens" below):

| Case | Group | Inputs | Expected |
|---|---|---|---|
| `degrade-c2-missing-pkg-id` | degrade | `Alpha` `version.json` `1.3.0` (ahead of release `1.2.0`, so check 1 is skipped, FR-043), manifest without `install.macos_pkg_id`; healthy `Bravo` | exit 3, WARN `E_MANIFEST_SCHEMA` `Alpha` (PD-10: C2 requires it; before PD-10 this aborted with `E_OUTPUT_SCHEMA`) |
| `degrade-c2-wrong-format` | degrade | `Alpha` manifest whose `windows-x86_64` asset says `format: "pkg"`; healthy `Bravo` | exit 3, WARN `E_MANIFEST_SCHEMA` `Alpha` |
| `degrade-manifest-unparseable` | degrade | `manifests/broken.json` = `{"tag": ` (truncated); healthy `Alpha` with its own manifest | exit 3, WARN `[{code: E_MANIFEST_SCHEMA, plugin: null, file: "manifests/broken.json"}]`, `Alpha` entry equals its golden (**PD-8, after the Open question 1 ruling**; if ruled abort: group `reject`, exit 1, `error: E_INPUT`) |
| `degrade-manifest-duplicate-tag` | degrade | two files both `tag: "alpha/v1.2.0"`; healthy `Bravo` | exit 3, WARN `E_MANIFEST_DUPLICATE` `Alpha` naming both files (**PD-8, after the ruling**; if ruled abort: `reject`, `E_INPUT`) |
| `duplicate-plugin-case` | reject | directories `alpha1` (`name: "Alpha"`) and `alpha2` (`name: "ALPHA"`) | exit 1, `E_DUPLICATE_PLUGIN` (PD-13) |
| `reject-reserved-name` | reject | `name: "Nul"` | exit 1, `E_PLUGIN_NAME` (PD-14) |
| `manager-block` | clean | `managerLatestUrl: "https://rolandzwaga.github.io/krate-audio/manager/latest.json"` (the Pages endpoint, roadmap line 811) | exit 0; the golden carries top-level `manager.latest_json_url` after `plugins` (PD-16); its v1 projection still equals legacy (SC-002) |

**Goldens.** `expected.v2.json` / `expected.legacy.json` are written once by running `gen-feed.js` on each case, then
**reviewed field by field** against FR-043 (key order, `release_notes_url`, `channel`, `platforms` minus `name` and
`list`) and recorded in `fixtures/README.md` ("reviewed 2026-…, by …"). A self-blessed golden cannot hide a defect alone,
because the tests also assert the rules directly (SC-002 projection equality against legacy, SC-003's table, SC-011,
SC-022 to SC-027 structural asserts) — the goldens add byte-stability (SC-012), not correctness.

**Listing archives (FR-058).** Good set and the two layout archives: built once in WSL from POSIX trees under
`/tmp/<mkdtemp>` (never under `/mnt/f`, so NTFS cannot strip modes or links), with
`tar --owner=0 --group=0 --numeric-owner --mtime='2026-10-29 00:00:00Z' --sort=name --format=gnu -cf - -C <tree> <items> | gzip -n -9 > <name>.tar.gz`
(`gzip -n` keeps the file name and timestamp out of the gzip header; `--format=pax` for `pax-path` and `pax-global`;
`pax-global` adds `--pax-option=comment=krate-fixture`, which GNU tar 1.35 writes as a global `g` header before the first
entry — measured in WSL 2026-10-29: typeflag byte 156 of the first header is `g`, name `/tmp/GlobalHead.1`). The `.listing` / `.sha256` files are produced by `listing.js` from the archive and cross-checked once
against `tar -tvzf` output by eye (recorded in `listings/README.md`). Bad set: `make-bad.js` (Node only, `node:zlib` +
hand-built 512-byte headers with its own checksum), run once, output committed; it writes only under a `--out` dir.

**`ustar-prefix` (PD-18), good set.** Built with the same flags but `--format=ustar`, from a tree holding
`<B>.vst3/Contents/Resources/` + a directory chain that makes one file path 101-255 bytes long with a `/` such that the
part after it is ≤ 100 bytes and the part before it ≤ 155 bytes (GNU tar's ustar writer then splits it into
`prefix` + `name`; it refuses `--format=ustar` for a path that cannot be split, so the build itself fails loudly if
the split is not possible). The record-presence assertion (§4.3) proves the split happened: magic `ustar\0`, version
`00`, non-empty `prefix` at byte 345. Added to FR-058's good set, SC-008's list and `listings/README.md`.

**Bad archives added by this revision** (`make-bad.js`, expected code in `.expected-error`):

| Fixture | Content | Code |
|---|---|---|
| `pax-malformed` | an `x` header whose record length is not decimal (`"1x path=a\n"`) | `E_LISTING_CORRUPT` |
| `base256-size` | a regular-file header whose `size` field has the high bit set (GNU base-256) | `E_LISTING_CORRUPT` |
| `no-end-block` | valid headers and data, gzip complete, but no all-zero end-of-archive block | `E_LISTING_TRUNCATED` |
| `symlink-dotdot-chain` | the review's chain: `l A/b/c/s -> ../../d`, `l A/b/c/t -> s/../../../x` (inside a C4 root) | `E_LISTING_SYMLINK_FORM` (on `t`) |
| `symlink-chain` | `l <B>.vst3/Contents/s -> Resources`, `l <B>.vst3/Contents/t -> s/moduleinfo.json` | `E_LISTING_SYMLINK_CHAIN` |
| `symlink-self` | `l <B>.vst3/Contents/l -> l` | `E_LISTING_SYMLINK_CHAIN` |
| `through-symlink` | `l <B>.vst3/Contents/t -> Resources`, then `f <B>.vst3/Contents/t/evil` | `E_LISTING_THROUGH_SYMLINK` |

The other FR-031 rules that need no archive (invalid UTF-8, NUL / CR / LF, ` -> `, target absolute / drive /
backslash, empty and `.` segments, FIFO) are rows of an in-memory table in `c4-listing-tar.test.js` (§7.1 SC-008); the
archive set keeps one archive per rule that only a real header stream can express.

**KAT (FR-023).** Made once on this box, procedure in `kat/README.md`: (1) build `KatPlugin-1.2.3-Linux-x64.tar.gz` in WSL
with the good-set flags and the C4 layout (`KatPlugin.vst3/Contents/Resources/moduleinfo.json`,
`KatPlugin.vst3/Contents/x86_64-linux/KatPlugin.so` (a few bytes of text), `presets/Default.preset`, `README.txt`,
`CHANGELOG.txt`); (2) `node -e` creates `fs.mkdtempSync(os.tmpdir() + '/kat-')`; (3) `rsign generate -p <tmp>/kat.pub -s
<tmp>/kat.key -W`; (4) `rsign sign -s <tmp>/kat.key -W -t "krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz"
<fixture>/KatPlugin-1.2.3-Linux-x64.tar.gz`; (5) copy `kat.pub`; (6) normalise the `.minisig` CRLF → LF with `toLfMinisig`;
(7) `rsign verify -p kat.pub KatPlugin-1.2.3-Linux-x64.tar.gz` on the normalised file, output pasted; (8) `rm -rf <tmp>`
and record that the directory is gone. The `.listing` / `.sha256` of the KAT archive join the SC-008 set.

**Snapshot (FR-071).** Captured once with network by the implementing agent (never by a test): copies of the nine
`version.json` and `setup.iss` (byte copies, LF because `-text` is set before they are added), `git ls-remote --tags
origin` → the recorded Node one-liner → `tags.txt`; `gh release list --limit 200 --json tagName,isPrerelease,isDraft,publishedAt`
→ `releases.json`; `curl -sL https://rolandzwaga.github.io/krate-audio/versions.json` → `live-versions.json`. The capture
commands, the date and the counts (88 tags, 88 releases, 0 prerelease, 0 draft, spec "Real data") go in its README.

**`.gitattributes` (FR-063)** lands **before** any fixture file is added to the index, so no fixture is ever stored
CRLF-converted:

```
# Byte-exact fixtures: signatures, SHA-256 digests and golden outputs compare raw bytes, and this checkout runs
# core.autocrlf=true, which would rewrite them on Windows checkout.
tools/feed-contract/fixtures/** -text
```

### 4.8 C++ change — `plugins/shared/tests/test_update_checker.cpp`

Added near the top (test-only code, anonymous namespace, so no ODR exposure; name checked unique, §1.1):

```cpp
#include "platform/preset_paths.h"
#include <optional>
#include <random>
#include <string>
#include <vector>
#include <cstdlib>

namespace {

#if defined(_WIN32)
constexpr const char* kSettingsEnvVar = "APPDATA";          // preset_paths.cpp:56-61
#elif defined(__APPLE__)
constexpr const char* kSettingsEnvVar = "HOME";             // preset_paths.cpp:62-67
#else
constexpr const char* kSettingsEnvVar = "XDG_CONFIG_HOME";  // preset_paths.cpp:68-72
#endif

std::optional<std::string> readEnv(const char* name);        // _MSC_VER: getenv_s size query then read (no C4996,
                                                             // pattern of ecosystem_rule_probe_test.cpp:215-227); else std::getenv
void writeEnv(const char* name, const std::optional<std::string>& value);  // _WIN32: _putenv_s(name, v or "") ("" removes);
                                                                           // else setenv(name, v, 1) / unsetenv(name)
bool isUnder(const std::filesystem::path& p, const std::filesystem::path& root);  // component-wise prefix of lexically_normal()

/// Redirects the settings root that getAppSettingsDirectory reads to a fresh temp directory for its lifetime.
class ScopedSettingsDirRedirect {
public:
    ScopedSettingsDirRedirect();   // root_ = temp_directory_path() / ("krate-update-test-" + 16 hex from std::random_device);
                                   // create_directories; previous_ = readEnv(var); writeEnv(var, root_.string())
    ~ScopedSettingsDirRedirect();  // writeEnv(var, previous_); remove_all(root_, ec) (no throw)
    ScopedSettingsDirRedirect(const ScopedSettingsDirRedirect&) = delete;
    ScopedSettingsDirRedirect& operator=(const ScopedSettingsDirRedirect&) = delete;
    ScopedSettingsDirRedirect(ScopedSettingsDirRedirect&&) = delete;
    ScopedSettingsDirRedirect& operator=(ScopedSettingsDirRedirect&&) = delete;
    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
private:
    std::filesystem::path root_;
    std::optional<std::string> previous_;
};

}  // namespace
```

Each of the three existing `TEST_CASE`s opens with:

```cpp
    ScopedSettingsDirRedirect settings;
    REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("Iterum"), settings.root()));
    REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("Ruinae"), settings.root()));  // :79 case only
```

placed before any `MockUpdateChecker` is constructed (PD-5). Catch2 re-enters the `TEST_CASE` body once per `SECTION`, so
each section run gets a fresh temp root; the guard is declared before every checker, so the checker's destructor (which
joins the worker, `update_checker.cpp:28-33`) runs first and no `getenv` races the restore. A side effect, recorded:
the existing cases stop sharing the real `update_state.json` across runs (today `"dismissed_version": "0.14.0"` written
by `:200` is read back by every later constructor), so they become hermetic; their assertions do not change.

New case (FR-050), two inline raw-string documents (`kFeedV2Json`, `kFeedV1OnlyJson`) with plugin key `FeedV2Probe`:
v1-only entry `{"version": "1.1.0", "download_url": "https://example.com/feedv2probe", "release_notes": ""}`; the v2
document adds `tag`, `pub_date`, `channel: "stable"`, `release_notes_url`, `platforms` (`windows-x86_64` `inno-exe`,
`darwin-universal` `pkg` with `min_os`, `linux-x86_64` `tar.gz` with `min_os` and `payload`), `install`, a top-level
`manager`, and `prerelease` `{tag: "feedv2probe/v1.2.0-beta.1", channel: "beta", …}`; section (b) is the same text with
`"schema_version": 99` (built by string replacement of `"schema_version": 2`, asserted to have replaced exactly one).

```cpp
TEST_CASE("UpdateChecker - feed schema v2 is read as v1", "[update]") {
    const auto envBefore = readEnv(kSettingsEnvVar);
    {
        ScopedSettingsDirRedirect settings;
        REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("FeedV2Probe"), settings.root()));
        UpdateCheckerConfig config{.pluginName = "FeedV2Probe", .currentVersion = "1.0.0",
                                   .endpointUrl = "https://example.com/versions.json"};
        // reference: the v1-only document
        // SECTION("v2 document") and SECTION("schema_version 99"): each runs a MockUpdateChecker on its document,
        // REQUIRE(waitForResult), then CHECK result == reference on updateAvailable (true), latestVersion ("1.1.0", never
        // "1.2.0-beta.1"), downloadUrl, releaseNotes (""); CHECK(exists(getAppSettingsDirectory("FeedV2Probe") /
        // "update_state.json")) and isUnder(that path, settings.root())
    }
    CHECK(readEnv(kSettingsEnvVar) == envBefore);    // SC-017: restored after the guard's scope
}
```

`currentVersion = "1.0.0"` is required because `parseResponse` fills `latestVersion`, `downloadUrl` and `releaseNotes`
only when `remote > current` (`update_checker.cpp:246-256`); with an equal version every field would stay empty and the
v2/v1 comparison would be vacuous.

### 4.9 `.github/workflows/feed-tools.yml`

```yaml
name: Feed tools
on:
  push:
    paths:
      - 'tools/gen-feed.js'
      - 'tools/gen-release-manifest.js'
      - 'tools/minisign.js'
      - 'tools/feed-contract/**'
      - 'package.json'
      - 'package-lock.json'
      - '.github/workflows/feed-tools.yml'
  pull_request:
    paths:
      - 'tools/gen-feed.js'
      - 'tools/gen-release-manifest.js'
      - 'tools/minisign.js'
      - 'tools/feed-contract/**'
      - 'package.json'
      - 'package-lock.json'
      - '.github/workflows/feed-tools.yml'
  workflow_dispatch:
permissions:
  contents: read
jobs:
  node-test:
    name: node-test (${{ matrix.os }})
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
    steps:
      - uses: actions/checkout@v6
      - uses: actions/setup-node@v4
        with: { node-version: '24' }
      - run: npm ci
      - name: Cache rsign2
        id: rsign-cache
        uses: actions/cache@v5
        with:
          path: |
            ~/.cargo/bin/rsign
            ~/.cargo/bin/rsign.exe
          key: rsign2-0.6.7-${{ runner.os }}-${{ runner.arch }}
      - if: steps.rsign-cache.outputs.cache-hit != 'true'
        run: cargo install rsign2 --version 0.6.7 --locked
      - name: node --test
        shell: bash
        run: |
          set -o pipefail
          node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" | tee "$RUNNER_TEMP/tap.txt"
          node -e "<inline check: '# fail 0' present and '# tests N' with N >= 21, else exit 1; print N>"
      - name: rsign verify (KAT)
        run: rsign verify -p tools/feed-contract/fixtures/kat/kat.pub tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz
      - name: Checkout unchanged
        shell: bash
        run: test -z "$(git status --porcelain)"
```

The TAP check is an inline
`node -e` reading `$RUNNER_TEMP/tap.txt` (outside the checkout, so the porcelain step stays empty). The rsign cache is
the "optimise every run" item: a cold `cargo install` compiles rsign2 and its deps on each leg; the cache keys on the exact
version, so a stale binary cannot be reused for another version. `~/.cargo/bin` is on `PATH` on all three hosted images
(Cargo ships with each, §1.2).

### 4.10 `package.json` / `package-lock.json` (FR-062)

> **Superseded at close (spec Clarifications 2026-10-30 Q-D):** the root `package.json` / `package-lock.json` are
> gitignored, so `ajv` lives in a tracked `tools/feed-contract/package.json` + lock (`npm install --prefix tools/feed-contract`;
> CI `npm ci --prefix tools/feed-contract`), and `.gitignore`'s two lines are anchored to `/package.json` / `/package-lock.json`.
> Read every root `package.json` / `package-lock.json` below as `tools/feed-contract/package.json` / `package-lock.json`.

`npm install --save-dev --save-exact ajv@8.20.0` at the repo root (adds `"ajv": "8.20.0"` and updates the v3 lockfile with
ajv and its four runtime dependencies). Nothing else.

---

## 5. Dependencies

| Dependency | Version | Where | Verified against | Why |
|---|---|---|---|---|
| Node.js | 24 (CI `node-version: '24'`; v24.7.0 local) | runtime of every tool and test | https://nodejs.org/docs/latest-v24.x/api/ (`test`, `zlib`, `fs`, `crypto`, `util.TextDecoder`, `child_process`) | repo rule: Node for tooling |
| `ajv` | **8.20.0** exact, root devDependency | `contract.js` | `npm view ajv` (latest 8.20.0); https://unpkg.com/ajv@8.20.0/dist/2020.js; https://ajv.js.org/json-schema.html; https://ajv.js.org/strict-mode.html; strict-mode behaviour measured (PD-1, PD-2) | the one third-party package: a JSON Schema 2020-12 validator so the schema files are the single source for Node now and Rust in Phase 5 (D2); hand-written validation would duplicate the schema |
| (ajv runtime deps) | `fast-deep-equal ^3.1.3`, `fast-uri ^3.0.1`, `json-schema-traverse ^1.0.0`, `require-from-string ^2.0.2` | lockfile only | `npm view ajv@8.20.0 dependencies` | transitive |
| `rsign2` | 0.6.7 | CI `cargo install` + one-time KAT creation; not a repo dependency | spec External facts; `rsign --version` here | the tool the probes proved against a `tauri signer` key (probes results.md, "Are `tauri signer` keys minisign-compatible?") |
| GNU tar | 1.35 (WSL) | one-time fixture build | `wsl -e tar --version` | writes GNU `L`/`K`, pax `x`/`g` on request |
| bsdtar / GNU tar | system `tar` on the POSIX legs (bsdtar 3.5.3 on macOS 26, GNU tar on Ubuntu) | SC-009 extraction in a temp dir | runner-images READMEs (§1.2) | proves listing-from-disk equals listing-from-headers |
| git | system git on every leg and locally | SC-004 only, in a temp repo | `versioncmp.c` source (§1.2) | the reference for the legacy tag order |
| `yaml` CLI | 2.9.1 via `npx --yes yaml@2.9.1 valid` | local pipeline-lane YAML parse only; **not** added to `package.json` | `npm view yaml`, measured | no YAML parser in the repo; FR-062 forbids adding a dependency |
| `actions/checkout@v6`, `actions/setup-node@v4`, `actions/cache@v5` | as used in the repo's workflows | `feed-tools.yml` | `.github/workflows/*.yml` grep | match existing pins |

No Rust crate, no Tauri plugin, no other npm package.

---

## 6. Safety design

| Roadmap safety item | In Phase 1 |
|---|---|
| Injected roots | Not applicable: nothing here reads or writes a plug-in, preset or state root. gen-feed touches only the paths on its command line and `--out` (temp + rename). Tests use `fs.mkdtemp(os.tmpdir())` and are **forced** there by the write guard (§4.6, SC-015c). The C++ case redirects the one per-user path the shipped parser writes (§4.8). |
| Never touch the real system | The guard blocks writes outside `os.tmpdir()` (refusing `..` segments before resolution, checking both paths of a hard link, PD-15) and allows only `git` and `tar` spawns in their fixed shapes with a temp `cwd` and, for `tar`, a temp `-C` destination (no shell strings, no `fork`); `hygiene.test.js` bans `http`, `https`, `http2`, `net`, `tls`, `dgram` and `fetch(` in every scanned file; system `tar` runs only on committed good fixtures into a temp dir on the POSIX CI legs; the C++ `REQUIRE`s abort before the first possible write (PD-5); SC-021 stats the two real `update_state.json` files before and after the gate. |
| Verify before extract / execute | Forward contract: C1 gives each asset `size`, `sha256` and a C3-bound `signature`; Phase 7 verifies all three before Phase 8 extracts or Phase 10 executes. Phase 1 extracts nothing (`listingFromTarGz` reads headers only) and verifies no signature cryptographically (the KAT is verified by `rsign`, a separate tool, at creation and in CI). |
| Zip-slip guard | C4's FR-031 is the contract the Phase 8 extractor implements: absolute, drive, backslash, `..`, duplicate, root-symlink and bundle-escaping-symlink paths are rejected at listing time, and PD-12 adds the three rules that make lexical and physical resolution agree (target `..` only as a leading run, no entry under a link, no link through or to a link), each with a crafted archive (§4.7) or an in-memory row (§7.1). `CONTRACT.md` obliges Phase 8 to apply the same rules to what it extracts **and** to check physical `realpath` containment after extraction (§4.3). |
| Elevated helper op set | Not built here. The feed bounds what it may later receive: `windows_uninstall_key` is a GUID key name equal to the plugin's own `setup.iss` AppId (check 2), `macos_pkg_id` matches Phase 10's pattern, and no two entries share an install fact (FR-014), compared case-folded for the names a case-insensitive file system would merge (`vst3_bundle`, `au_component`, `macos_pkg_id`) and with plugin names unique case-folded in gen-feed (PD-13), so a feed value cannot carry a path, an argument or another plugin's uninstaller. |
| Tauri capability set | Not applicable (no app). |
| Keys | Only the throwaway KAT key, generated in an `fs.mkdtemp` dir outside the repo and deleted after signing; FR-024's scan (three rules + synthetic positives) proves no secret-key file is committed. No CI secret is read: `feed-tools.yml` has `permissions: contents: read` and no `secrets.*`. |
| Feed strings never become paths | `install` values and plugin names are single names checked by pattern **and** by `checkInstallValues` / `isPluginName` before the schema; none may be a Windows reserved device name (with or without an extension) and `preset_subdir` may not end in a space (PD-14), so a pattern-valid value names exactly one ordinary folder on every OS; listing paths by FR-031. |

---

## 7. Test plan

Run locally: `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` from the repo root (quoted glob,
default process isolation and concurrency). Every test file's first statement is `require('./helpers/write-guard.js')`.

### 7.1 Named tests (≥ 21 required by SC-018; this plan has 24)

| SC | File | Test name | Assertion strategy |
|---|---|---|---|
| SC-001 | `gen-feed-legacy.test.js` | `gen-feed legacy reproduces the 2026-10-29 live feed` | `buildLegacyFeed(snapshot)` === `live-versions.json` bytes (`Buffer.equals`); on failure print the first differing line |
| SC-002 | `gen-feed-v2.test.js` | `v1 projection of v2 equals legacy` | one subtest per `group: "clean"` case (≥ 8, snapshot included): `projectV1(buildFeed(case).doc)` === `buildLegacyFeed({plugins, tagsText: tagNames.join('\n') + '\n'})` |
| SC-003 | `gen-feed-v2.test.js` | `Q9 prerelease never moves the v1 version` | one subtest per `group: "prerelease"` case; asserts the v2 `version` / sibling `tag` / `channel` and the legacy pick exactly as the SC-003 table (`case.json` `legacyExpect`), and byte equality for the lower-core row |
| SC-004 | `git-versionsort.test.js` | `legacy tag order matches git versionsort` | one temp repo (`git init`, one empty commit with `-c user.name=t -c user.email=t@t`), all corpus tags created with one `git update-ref --stdin`, then per set `git tag -l "<p>/v*" --sort=-v:refname` and compare the first line with `buildLegacyFeed`'s choice; env `GIT_CONFIG_GLOBAL` / `GIT_CONFIG_SYSTEM` = empty files in the temp dir, `GIT_CONFIG_NOSYSTEM=1`; every spawn has `cwd` = the temp repo |
| SC-005 | `c1-feed.test.js` | `C1 feed fixtures` | every `feeds/cases.json` row (≥ 29; the §4.7 C1 table) through `validateFeed`, asserted per the three `expect` meanings of §3.1 (`accept-without-platforms` through `definedPlatformKeys`); every id in `C1_RULES` (all FR-001 to FR-014 constraints of §4.7, the FR-055 ★ categories included) has ≥ 1 error row, and no row names an id outside the list |
| SC-006 | `c2-release.test.js` | `C2 release.json fixtures` | every `release/cases.json` row through `validateRelease` + `checkReleaseManifest(doc, {repo})`, expected code; every id in `C2_RULES` (the §4.7 C2 table: FR-015, FR-016 incl. PD-10's format and `install` rows, FR-017) has ≥ 1 error row; part "projection": every valid row's `assembleEntry` result passes `checkAssembledEntry` (C2 ⊇ C1, §4.1) |
| SC-007 | `c3-trusted-comment.test.js` | `C3 trusted comments` | valid set parses and round-trips through `formatTrustedComment`; invalid set gives its `E_TC_*`; A-vs-B binding gives `E_TC_BINDING` naming `asset` |
| SC-008 | `c4-listing-tar.test.js` | `C4 listings from tar headers` | each good archive + the KAT: `toText(listingFromTarGz(buf))` === `.listing`, `digest` === `.sha256` trimmed; record-presence asserts via `readTarEntries(…, {raw: true})`; each bad archive throws its `.expected-error`; `checkLinuxLayout` accepts `real-shaped`, rejects both layout archives with `E_LAYOUT`. Good set includes `ustar-prefix` (PD-18, magic / version / prefix asserted). Subtest `FR-031 rules on in-memory entries`: one row per rule calling `listingFromEntries` directly, no archive — invalid UTF-8 (a `Buffer` path `0xC3 0x28`), NUL, CR, LF, ` -> ` in a path, empty path [`E_LISTING_PATH`]; target `/etc/x` [`E_LISTING_ABSOLUTE`], `C:x` [`E_LISTING_DRIVE`], `a\b` [`E_LISTING_BACKSLASH`]; path `a//b` and `a/./b` [`E_LISTING_DOTDOT`]; targets `a/../b`, `./a`, `..` [`E_LISTING_SYMLINK_FORM`]; `{type: 'other', typeflag: '6'}` (FIFO) [`E_LISTING_TYPE`]; the PD-12 chain, self-link and through-symlink entries [their codes]; plus accept rows (`<B>.vst3/Contents/x86_64-linux/a -> b.so`, `../Resources/moduleinfo.json` from one level down). Subtest `FR-033 layout items`: `checkLinuxLayout` on `real-shaped`'s lines minus `Contents/Resources/moduleinfo.json`, minus `Contents/x86_64-linux/<Name>.so`, with the `.so` renamed `Other.so`, and with an extra bundle item, each `E_LAYOUT` naming the item |
| SC-009 | `c4-listing-dir.test.js` | `C4 listings from extracted trees` | one subtest per good archive on every OS. POSIX: `spawnSync('tar', ['-xzf', abs, '-C', tmp], {cwd: tmp})`, `listingFromDirectory(tmp)` === `.listing`; asserts the `l` line and a `0755` file line where present, and no line contains `/private/`. win32: asserts `E_LISTING_NO_POSIX_MODES` (same subtest names, so `# tests` matches) |
| SC-010 | `gen-feed-errors.test.js` | `gen-feed rejects` | one subtest per abort code (`E_V1_TYPE`, `E_URL_LATEST`, `E_URL_NOT_HTTPS`, `E_TAG_FORMAT`, `E_PLUGIN_NAME`, `E_DUPLICATE_PLUGIN`, `E_INPUT` incl. raw `ls-remote` tags), via in-process `main`: exit 1, stderr first line `gen-feed: <CODE>:`, a pre-existing `--out` (written by the test) byte-identical, no `*.tmp` in the out dir; the abort+degrade case exits 1; a usage error exits 2. Added (PD-13, PD-14, PD-16): `duplicate-plugin-case` → `E_DUPLICATE_PLUGIN`; `reject-reserved-name` → `E_PLUGIN_NAME`; three bad `--manager-latest-url` subtests (`http://…/manager/latest.json`, `https://…/releases/latest/download/manager/latest.json`, `https://…/manager/feed.json`) → exit 1, `E_INPUT`, nothing written; `E_OUTPUT_SCHEMA`: `main(argv, io, {beforeSelfCheck: doc => { doc.plugins.Alpha.release_notes = 'x'; }})` on a clean case → exit 1, first line `gen-feed: E_OUTPUT_SCHEMA:`, pre-existing `--out` byte-identical, no `*.tmp`; `--project-v1` on a valid v2 feed → exit 0 and stdout bytes === that case's `expected.legacy.json`; on malformed JSON and on a document without `plugins` → exit 1, `E_INPUT`, stdout empty; `assembled entry failing C1 degrades`: `checkAssembledEntry` on an entry built by hand with a `darwin-universal` platform and no `au_component` throws `E_MANIFEST_SCHEMA` (the PD-10 backstop, which no manifest fixture can reach once C2 carries the binding) |
| SC-011 | `gen-feed-v2.test.js` | `release_notes is always empty` | every non-reject case in legacy, v2 and projection: every `plugins.*.release_notes === ""` |
| SC-012 | `gen-feed-v2.test.js` | `gen-feed is deterministic` | two runs byte-identical; a temp copy with CRLF `CHANGELOG.md` and `version.json` gives the same bytes; every case matches its committed golden |
| SC-013 | `c3-trusted-comment.test.js` | `KAT binds and no secret key is committed` | parse the KAT `.minisig`, `checkBinding` with `{KatPlugin, 1.2.3, linux-x86_64, <file name>}`, algorithm `ED`; FR-024 scan of `tools/feed-contract/**` finds 0 key files and does not flag the KAT `.minisig` or any fixture `signature` value; three synthetic key-shaped files written to a temp dir are each flagged (rule a, b, c) |
| SC-014 | `hygiene.test.js` | `generator ships no plugin roster` | `tenth-plugin` case yields 10 entries; snapshot yields 9; case-insensitive grep of `tools/gen-feed.js`, `contract.js`, `listing.js` and both `versions.*.schema.json` + `release.schema.json` for the nine names (read from the snapshot's `version.json` copies, not typed into the test) finds 0 hits |
| SC-015 | `hygiene.test.js` | `tools stay offline` | (a) the FR regex scan over the §2 list: `tools/gen-feed.js`, `tools/feed-contract/*.js`, `tools/feed-contract/tests/*.test.js`, **`tools/feed-contract/tests/helpers/*.js`**, `fixtures/make-fixtures.js`, `fixtures/listings/make-bad.js` (`write-guard.js` keeps its `child_process` exemption, `fixtures.js` has none); the patterns are built from pieces (`'child' + '_process'`, `/\bfetch\s*\(/`) so the scanner does not match itself; (b) first statement check; (c) teeth: five fs writes and one `spawnSync` with a repo `cwd` each throw `E_WRITE_OUTSIDE_TMP`, target absent after; plus (PD-15) with `L` = a symlink (a `'junction'` on win32, which needs no privilege) created in tmp and pointing at a repo directory: `fs.writeFileSync(<tmp>/L/../x)` throws (the `..` refusal) and `fs.writeFileSync(<tmp>/L/x)` throws (`canonicalFollow` lands in the repo), and neither `<repo dir>/x` nor `<tmp>/x` exists after; `fs.linkSync(<repo file>, <tmp>/x)` throws and `<tmp>/x` is absent; `spawnSync('tar', ['-xzf', <fixture>, '-C', <repo dir>], {cwd: tmp})` throws; `execSync('echo x')`, `spawnSync('node', [], {cwd: tmp})`, `spawnSync('git', ['-C', <repo>, 'status'], {cwd: tmp})` and `spawnSync('tar', […], {cwd: tmp, shell: true})` each throw; (d) is the CI porcelain step + the local before/after record |
| SC-019 | `hygiene.test.js` | `no serial test flags` | scan `feed-tools.yml` and every `*.md` under `tools/feed-contract/` for the four flag spellings: 0 hits |
| SC-022 | `gen-feed-errors.test.js` | `channel comes from the release list only` | parts (a)-(d) of SC-022; manifest file hashed before and after the promotion run |
| SC-023 | `gen-feed-errors.test.js` | `gen-feed degrades a bad manifest` | one subtest per `group: "degrade"` case: exit 3; `--out` passes C1 and v1; degraded entry (and sibling) lacks `platforms` / `install` and equals the same run with that manifest removed; healthy entry equals its golden; WARN lines as listed in `case.json` (an unattributed `plugin: null` row matches a line naming its `file`); no temp file; clean run 0 without WARN; `--legacy` / `--project-v1` never 3. Includes the §4.7 cases `degrade-c2-missing-pkg-id` (version.json ahead, manifest without `macos_pkg_id`: exit **3**, not 1) and `degrade-c2-wrong-format` (PD-10), and, once Open question 1 is ruled degrade, `degrade-manifest-unparseable` and `degrade-manifest-duplicate-tag` (PD-8) |
| SC-024 | `gen-feed-errors.test.js` | `install cross-check is gated on version` | parts (a)-(d) |
| SC-025 | `gen-feed-errors.test.js` | `windows_uninstall_key equals the setup.iss AppId` | `extractAppId` on the nine snapshot copies → nine distinct `{GUID}` matching FR-008; (b)-(d) through gen-feed cases |
| SC-026 | `c1-feed.test.js` | `feed rejects shared install facts` | the four duplicate documents → `E_INSTALL_DUPLICATE` naming field and both keys; the case-fold document (`Alpha` with `Alpha.vst3`, `ALPHA` with `ALPHA.vst3`) → `E_INSTALL_DUPLICATE` on `vst3_bundle` (PD-13); full v2 / mixed pass; entry + own sibling identical install passes; v1-only entries ignored; `CONTRACT.md` contains `E_INSTALL_DUPLICATE` |
| SC-027 | `gen-feed-errors.test.js` | `notes_markdown is one uncapped section` | long-section case: string, byte-equal to its section, ≥ 14,000 bytes, other sections absent; sibling has its own; array-typed fails C1; the schema files define no `maxLength` and no `array` type under `notes_markdown` (JSON walk of the schema) |
| PD-7 | `hygiene.test.js` | `fixtures are in sync with make-fixtures.js` | `make-fixtures.js --check` logic run in-process into a temp dir: 0 differing files |
| FR-013/017/034/046 (PD-17) | `hygiene.test.js` | `CONTRACT.md documents every code and field` | read `CONTRACT.md` (read-only): (a) every key of `contract.CODES` appears in a code-table row together with its class word; (b) the exit codes `0`, `1`, `2`, `3` each appear in the exit-code list; (c) a JSON walk of `versions.v2.schema.json` (`properties` / `patternProperties` / `$defs` references, expanded to dotted paths such as `plugins.<Name>.platforms.<key>.payload.sha256` and `plugins.<Name>.prerelease.tag`) yields a path set, every member of which appears in the C1 field table; (d) the eight fixed section headings of §4.4 are present, so the C2 cross-field rules, the `min_os` departure, the no-channel rule and "no archive layout" each have their section |

### 7.2 FR → test map (FRs without their own SC)

| FR | Covered by |
|---|---|
| FR-001-FR-013 | SC-005 rows, one per `C1_RULES` id (§4.7 table, coverage asserted, PD-11), schema compile at load (PD-3), SC-026; FR-013 by `CONTRACT.md documents every code and field` (PD-17) |
| FR-014 | SC-026, SC-023 duplicate cases |
| FR-015-FR-018 | SC-006 (`C2_RULES` coverage, PD-11; C2 ⊇ C1 projection, PD-10), SC-022, SC-023 (`degrade-c2-*` cases); FR-017 / FR-018 prose by the PD-17 test |
| FR-020-FR-022 | SC-007 |
| FR-023-FR-024 | SC-013 |
| FR-030-FR-034 | SC-008 (archives, the in-memory FR-031 table, the FR-033 layout-item rows), SC-009, SC-006 (manifest payload rows incl. `list-not-canonical`), FR-034 by the PD-17 heading check and review |
| FR-040 | SC-001, SC-004, SC-010 (`E_INPUT` tags) |
| FR-041-FR-049 | SC-002, SC-003, SC-010-SC-012, SC-022-SC-027; FR-041's `--manager-latest-url` by the `manager-block` case and SC-010's three bad-URL subtests; FR-045 by SC-010's `E_OUTPUT_SCHEMA` hook subtest; FR-046's code list by the PD-17 test; FR-047's CLI by SC-010's `--project-v1` subtests (PD-16) |
| FR-050-FR-052 | SC-016, SC-017, SC-021 |
| FR-055-FR-059 | SC-005, SC-006, SC-007, SC-008, the PD-7 drift test |
| FR-060 | SC-018 (PENDING-CI), local YAML parse |
| FR-061 | SC-015 (b), (c) |
| FR-062-FR-063 | SC-020 diff review; `npm ci` in CI; SC-013/SC-008 byte checks fail if `-text` is missing on a CRLF checkout |
| FR-070-FR-071 | SC-020 (the §8 lane step 8 scope check, output quoted in the compliance row), SC-001 |

### 7.3 C++ rows

| SC | Local (Windows) | PENDING-CI |
|---|---|---|
| SC-016 | build `shared_tests` (0 warnings: count `warning` in the build log), `node tools/header-consumers.js --tag '[update]'` → `CONSUMERS OUTSIDE [update]: none`, `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests` → `GATE GREEN`, shard log lists `UpdateChecker - feed schema v2 is read as v1` with both sections | `ci.yml` jobs `test-macos` ("macOS tests (shard …)") and `test-linux` ("Linux tests (shard …)"), on the user's PR to `main`: every shard green, the shard that ran `shared_tests` reports the case passed |
| SC-017 | same gate run (the `REQUIRE`, the `exists` check, the env-restored `CHECK`) | same jobs |
| SC-021 | read of the file citing the guard + `REQUIRE` line in each of the four cases; `stat -c '%Y %s'` of `%APPDATA%\Krate Audio\Iterum\update_state.json` and `...\Ruinae\update_state.json` before and after the gate (missing stays missing), both readings quoted | same jobs |

### 7.4 PENDING-CI list (Node)

| SC | Job | Assertion |
|---|---|---|
| SC-004, SC-008, SC-012, SC-018 | `feed-tools.yml` `node-test (ubuntu-latest)`, `node-test (macos-latest)`, `node-test (windows-latest)` (user's push of the branch) | step `node --test` green, TAP `# fail 0`, `# tests` ≥ 21 and equal to the local run's value on all three legs |
| SC-009 | `node-test (ubuntu-latest)`, `node-test (macos-latest)` | `C4 listings from extracted trees` subtests pass (Windows leg asserts the refusal) |
| SC-013 | all three legs | step `rsign verify (KAT)` exits 0 |
| SC-015 (d) | all three legs | step `Checkout unchanged` prints nothing |

---

## 8. Build integration and gate lanes

**Pipeline lane (this phase's only lane), in this order, with what runs concurrently:**

| # | Command | Concurrency |
|---|---|---|
| 1 | `"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests -- /nr:false` (log captured to a file; `/nr:false` per the MSBuild node-reuse note in memory) | start first; it is the long pole |
| 2 | `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` (wall clock and `# tests` recorded for SC-019) | **while 1 builds** (Node uses `availableParallelism() - 1` processes; the MSBuild compile of one TU leaves the box mostly idle) |
| 3 | `npx --yes yaml@2.9.1 valid < .github/workflows/feed-tools.yml` | while 1 builds |
| 4 | `node tools/header-consumers.js --tag '[update]'` | while 1 builds (reads only) |
| 5 | `node tools/run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests`, bracketed by the SC-021 `stat` readings | after 1 |
| 6 | `clang-tidy -p build/windows-ninja plugins/shared/tests/test_update_checker.cpp` (the one TU, per the "tidy the phase TUs by file list" memory; reconfigure `build/windows-ninja` first only if a test CMake file changed — none does) | after 1, beside 5 |
| 7 | `node tools/lint-serial-test-runs.js`; `node tools/gen-specs-index.js --check` (PD-9, after the ruling) | any time |
| 8 | **Scope check (SC-020, FR-070):** `git -c core.quotepath=off status --porcelain --untracked-files=all` and `git diff --name-only 84c6aab1..`, each piped into one inline `node -e` filter that reads stdin, takes the path of each line (the part after the two status columns; both sides of a `->` rename), drops every path matching the FR-070 list — exactly `tools/gen-feed.js`, `plugins/shared/tests/test_update_checker.cpp`, `.github/workflows/feed-tools.yml`, `package.json`, `package-lock.json`, `.gitattributes`, and the prefixes `tools/feed-contract/`, `specs/krate-manager-phase1/` (plus `specs/INDEX.md` only if PD-9 is ruled in) — and the one start-of-phase baseline line `?? specs/Krate-manager-roadmap.md` (untracked before this phase began, §0 header, and not changed by it), then prints what is left and exits 1 if anything is. Required: both commands print nothing after filtering; the raw outputs and the filtered (empty) outputs are quoted in the SC-020 compliance row | last, after every other step (a step that leaves a stray file must show up here) |

Not run, with reason: pluginval (test-only C++ change, CLAUDE.md "Skip for … test-only changes"); `tools/run-cpu-tests.js`
(no timing case touched); `tools/check-portability.js` (no plugin source or header changed; the new code is
`#if`-guarded test code that `ci.yml`'s macOS/Linux legs compile); app lane (no app).

**Workspace** (superseded by Q-D: `tools/feed-contract/package.json` serves the tools)**:** the root `package.json` / `node_modules` serve the tools (no new package directory; `apps/krate-manager/`
starts in Phase 4). `package.json` gains no `scripts` entry: the command above is the documented entry point, as for the
other `tools/*.js` (spec FR-060 names it), and a script entry would be one more place for a serial flag to creep in.

**Workflow files:** new `.github/workflows/feed-tools.yml`. No other workflow changes (FR-070). `ci.yml` picks up the C++
edit through its existing `plugins/**` filter on a PR to `main`.

**Ordering for the implementer:** `.gitattributes` → `package.json` (ajv) → schemas + `contract.js` → `listing.js` →
fixtures (builder, WSL archives, `make-bad.js`, KAT, snapshot) → `gen-feed.js` → goldens (generated, reviewed) → tests
(each written failing first against a stub module, then made green) → C++ edit (test first: the new case compiles and
fails before the documents are correct) → `feed-tools.yml` → `CONTRACT.md` → lanes.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Ajv strict mode rejects natural schema idioms (`not/required`, conditional `required`) | PD-1/PD-2 authoring rules, both measured; schemas compile at load so any violation is a red test (PD-3) |
| Self-blessed goldens hide a generator bug | Goldens add byte-stability only; correctness is asserted structurally (SC-002/003/011/022-027) and the goldens are reviewed and the review recorded (§4.7) |
| `gitVersionCompare` drifts from git on an edge (leading zeros, suffixes) | Independent implementation of the documented semantics, checked against real git on ≥ 20 sets on three OSes (SC-004); a failing set names both orders |
| Old-GNU vs POSIX header confusion (`prefix` bytes are `atime` in GNU format) | §4.3 step 4: prefix only with POSIX magic + version `00`. The old-GNU side (prefix ignored) is covered by every `--format=gnu` fixture, `long-name` included; the POSIX join is covered **only** by `ustar-prefix` (PD-18), whose record-presence assertion requires magic `ustar\0`, version `00` and a non-empty prefix, and whose `.listing` line requires the joined path. `pax-path` exercises the pax `path` record, not the prefix (GNU tar's pax writer puts long names in records, and its path is over 255 bytes by design) |
| Lexical symlink resolution disagrees with the file system (chained links, entries under a link) | PD-12's form, through-symlink and chain rules make the two agree for every accepted listing (§4.3); one crafted archive per rule; `CONTRACT.md` obliges Phase 8 to add a physical `realpath` containment check after extraction |
| Case-insensitive file systems merge two feed names | PD-13 case-folded uniqueness for plugin names (gen-feed) and for `vst3_bundle` / `au_component` / `macos_pkg_id` (FR-014) |
| A pattern-valid name is a Windows device name or ends in a space | PD-14 `not` clauses on every name pattern; one invalid C1 row per rule |
| bsdtar on macOS 26 (`tar` = bsdtar 3.5.3) extracts differently from GNU tar (implied dirs, modes) | D3 normalises modes; implied directories are added by `listingFromEntries` on both sides; the `implicit-dirs` and `dot-slash` fixtures exercise exactly this on the macOS leg |
| CRLF conversion corrupts byte-exact fixtures (this checkout `core.autocrlf=true`; Windows runners convert by default) | `.gitattributes` `-text` committed before any fixture; SC-008/SC-012/SC-013 compare raw bytes on the Windows leg, so a missing rule fails loudly |
| The write guard misses a write path (a destructured import, an unwrapped fs function, a `..` through a symlink, a hard link to a repo file, a spawn argument) | Module rule + scan (§2, helpers included); the guard wraps every writing entry point listed in FR-061 and PD-15 including `fs.promises`, refuses `..` before resolving, checks both paths of `link`, and allowlists `git` / `tar` argument shapes; SC-015(c) proves teeth for each of these forms; CI's porcelain step catches anything else on three OSes |
| `setenv` / `_putenv_s` race with the update worker thread's `getenv` | Guard declared before every checker; the checker's destructor joins the worker before the guard restores the variable; Catch2 runs one case at a time per process; `run-gate.js` shards are separate processes |
| macOS `HOME` redirect affects other code in the case | Only for the guard's scope; `temp_directory_path()` reads `TMPDIR`, not `HOME`; the restore is asserted (SC-017) |
| MSVC C4996 on `std::getenv` breaks the zero-warning build | `getenv_s` under `_MSC_VER`, the repo's existing pattern (§1.1) |
| `specs/INDEX.md` staleness turns `Generated Artifacts In Sync` red on the PR | PD-9, Open question 2 |
| `cargo install rsign2` slow or broken on a leg | Exact version + `--locked`; cache on `~/.cargo/bin`; Cargo is on all three images (§1.2) |
| Snapshot data changes after capture (a new release) | The snapshot is frozen data; SC-001 compares against the frozen `live-versions.json`, never the network; Phase 3's `dry_run` handles the live comparison |
| One bad `release.json` in Phase 3 would previously kill the feed | Degrade class + exit 3 (FR-049); PD-8 extends it to unparseable and duplicate manifests |
| WebKitGTK quirks, Windows file locks while a DAW runs, admin prompts | Not applicable in Phase 1: no app, no install, no elevation (owners: Phases 4, 10, 11) |
| Offline behaviour | gen-feed needs no network by construction (inputs are files, SC-015); the only network steps are the one-time snapshot capture, `npm ci`, `cargo install`, and the local `npx yaml` lane, none of which is a test |

---

## 10. Open questions (for the user)

1. **PD-8 — unparseable and duplicate manifests.** The spec defines no behaviour for a `release.json` that is not valid
   JSON, or for two manifest files with the same `tag`. Plan: both are degrade-class (WARN, exit 3, the feed is still
   written), the duplicate case under a new code `E_MANIFEST_DUPLICATE`, following Clarifications Q3. Alternative: abort
   (`E_INPUT`, exit 1, nothing written). Either way the ruling lands in FR-046 / FR-059 / SC-023 first, and the two
   §4.7 cases `degrade-manifest-unparseable` and `degrade-manifest-duplicate-tag` take the ruled group, exit and code.
   If degrade: an unparseable file cannot be tied to a plugin, so its WARN names the file (`plugin: null` in
   `case.json`) and SC-023's "naming the plugin" becomes "naming the plugin, or the file when none can be named".
2. **PD-9 — `specs/INDEX.md`.** `node tools/gen-specs-index.js --check` already fails because this phase's spec exists, and
   `ci.yml`'s `Generated Artifacts In Sync` job runs that check on the PR to `main`. FR-070 does not list
   `specs/INDEX.md`. Plan: regenerate it in this change and add it to FR-070 / SC-020's path list. Alternative: leave it
   for the commit that adds the specs and accept that job red until then. The §8 step-8 scope check admits
   `specs/INDEX.md` only if this is ruled in, so the ruling is visible in that lane's output.

---

## Review notes

**Plan review, 2026-10-30 (16 issues: 5 major, 11 minor).** None rejected; every issue is applied in place:

| Issue | Resolution |
|---|---|
| C2 omits C1's per-key `format` binding and FR-008's install requirements (major) | PD-10: §4.1 C2 `patternProperties` bindings + `install` requires four names; §4.5.4 step 4 `checkAssembledEntry` backstop; §4.7 C2 rows and cases `degrade-c2-missing-pkg-id` (exit 3) / `degrade-c2-wrong-format`; SC-006 projection part |
| FR-001-FR-013 rows never listed; no category field (major) | PD-11: §3.1 row shape with `rule`; §4.7 C1 table, one id per rule; `C1_RULES` / `C2_RULES` coverage asserted in SC-005 / SC-006 |
| No fixture reaches the ustar `prefix` join (major) | PD-18: `ustar-prefix` good fixture with a magic / version / prefix assertion (§4.3, §4.7); §9 risk row corrected |
| FR-031 / FR-033 codes without a test (major) | §7.1 SC-008 in-memory FR-031 table and FR-033 layout-item rows; §4.7 `pax-malformed`, `base256-size`, `no-end-block`; C2 row `list-not-canonical`; Buffer paths and `'other'` types reach `listingFromEntries` (§4.3) |
| Chained symlinks escape the root (major) | PD-12: form, through-symlink and chain rules with check order (§4.3), three codes (§3.2), four crafted archives including the reviewer's exact chain (§4.7), Phase 8 physical `realpath` obligation in `CONTRACT.md` (§4.4, §6) |
| `accept-without-platforms` undefined (minor) | §3.1 meaning + `plugin` key + `definedPlatformKeys` in `contract.js` |
| PD-8 behaviour untested; unattributed WARN (minor) | §4.5.4 step 3 WARN form, `plugin: null` in `case.json`, two cases gated on Open question 1, SC-023 wording change named there |
| `--manager-latest-url`, `E_OUTPUT_SCHEMA`, `--project-v1` untested (minor) | PD-16: `manager-block` case, `hooks.beforeSelfCheck`, SC-010 subtests |
| CONTRACT.md content unchecked (minor) | PD-17: `CODES` export, named test `CONTRACT.md documents every code and field` (24 named tests) |
| `tests/helpers/*.js` outside the scan (minor) | §2 module rule and SC-015(a) list name it; `fixtures.js` gets no exemption |
| No lane runs the SC-020 scope check (minor) | §8 step 8 with the filter list and the start-of-phase baseline |
| Write guard `..` / hard link / chown gaps (minor) | PD-15 (a)-(c): `..` refused before resolution, entry vs follow canonical forms, both `link` paths, `chown*` / `lchown*` / `mkdtempDisposable*`; SC-015(c) teeth cases |
| Spawn guard checks only `cwd` (minor) | PD-15: `exec*` / `fork` / `shell: true` refused, `git` / `tar` allowlist with fixed argument shapes; teeth cases |
| `E_LISTING_DOTDOT` row contradicted the target rule (minor) | §3.2 split: `E_LISTING_DOTDOT` for entry paths only; targets have `E_LISTING_SYMLINK_FORM`; check order in §4.3 fixes each fixture's code |
| Case-only name collisions (minor) | PD-13: `E_DUPLICATE_PLUGIN` and `findInstallDuplicates` case-fold; C1 row and gen-feed case added |
| Windows reserved names, trailing space (minor) | PD-14: `reservedStem` `not` clauses, `preset_subdir` pattern, C1 rows, gen-feed case |

**Spec rows to amend before implementation** (PD-10 to PD-18 tighten them; none relaxes a threshold): FR-002, FR-008
(reserved stems, `preset_subdir` end), FR-014 (case folding), FR-015 / FR-016 (C2 bindings stated), FR-031 (PD-12
rules), FR-046 (new codes, `E_DUPLICATE_PLUGIN` folding, and the PD-8 ruling), FR-055 (row shape), FR-058 (`ustar-prefix`
and the added bad archives), FR-061 (guard rules), SC-008 (good-set list), SC-023 (only if PD-8 is ruled degrade).
