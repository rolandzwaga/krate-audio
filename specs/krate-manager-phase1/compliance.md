# Krate Manager Phase 1 (krate-manager-phase1) Compliance Report

**Overall status: COMPLETE (PENDING-CI).** Zero fail or partial items. 13 items are pending-ci (FR-032, FR-052, FR-061, SC-004, SC-008, SC-009, SC-012, SC-013, SC-015, SC-016, SC-017, SC-018, SC-021). Each has a Windows-local result and a macOS/Linux or CI leg that only the user's push can run. Nothing was committed or pushed by this phase.

Key local numbers: `node --test` over `tools/feed-contract/tests/*.test.js` gave `# tests 350`, `# pass 350`, `# fail 0` (wall 1.77 s). `GATE GREEN shared_tests cases=8 assertions=122` for `[update]`. `SCOPE GREEN`.

## Compliance table

Evidence is condensed from the verification pass; quoted strings are verbatim tool output.

| ID | Verdict | Evidence |
|---|---|---|
| FR-001 | pass | versions.v2.schema.json:2-3 `$schema` 2020-12 and `$id` https://krateaudio.com/schemas/versions.v2.schema.json; :8 `schema_version {const: 2}`; :26 required `[schema_version, plugins]`; :14-24 `manager` optional. contract.js:360-362 compiles with Ajv2020 strict, no network $ref. |
| FR-002 | pass | :11 `propertyNames` -> `$defs/pluginName`; :64-69 pattern `^[A-Z][A-Za-z0-9]{0,63}$` plus `not reservedStem`; :59-63 CON/PRN/AUX/NUL/COM[0-9]/LPT[0-9]. No roster: valid/tenth-plugin.json accepted, invalid/plugin-key-reserved.json fails `/plugins not`. |
| FR-003 | pass | :213-228 `entry` required fields, `release_notes const ""`, `channel const stable`; :158 `notes_markdown` string, no maxLength; :162 `dependentRequired` platforms<->install. Fixtures fail as expected. |
| FR-004 | pass | :28-32 bounded `core`, used for `version` :218; same CORE_PART at contract.js:86-94 and gen-feed.js:343-346/:425. Fixtures version-suffix, version-ten-digits, version-leading-zero fail `/version pattern`; reject-tag-ten-digits gives E_TAG_FORMAT. |
| FR-005 | pass | :33-37 `tagStable`; :43-47 `pubDate`; :48-53 `url` `^https://[^\s]+$` with `not /releases/latest/`, referenced by all URL fields. url-https, url-latest, url-whitespace rejected. |
| FR-006 | pass | :131-149 platform key pattern and per-prefix format binding; :107-126 required url, size, sha256, signature, format; `size` 1..2147483647; darwin/linux `min_os` required, Windows optional. |
| FR-007 | pass | :54-58 four-line LF regex; contract.js:329-350 `parseMinisigText` (74-byte blob, `ED`, 64-byte global sig); feedCodeFindings (:395-418) runs it plus parseTrustedComment. C3 parse skipped for unknown platform keys (:412). signature-legacy-Ed, -blob-length, -cr give E_SIGNATURE_TEXT. |
| FR-008 | pass | :75-106 five install patterns; :163-211 darwin and windows-x86_64 conditionals; contract.js:192-208 `checkInstallValues` rechecks `/ \ : ..`, trailing space, reserved stem. |
| FR-009 | pass | :117-129 `payload {files>=1, sha256}` required on tar.gz, forbidden otherwise. payload-tar-gz-required, payload-inno-exe-forbidden, payload-pkg-forbidden fail as expected. |
| FR-010 | pass | :229-243 `sibling` via `entryCommon`, `channel const beta`, `version/download_url/release_notes: false`; :38-42 `tagPre`. prerelease-plain/-suffixed accepted; sibling-version-forbidden fails `false schema`. |
| FR-011 | pass | :14-24 `manager` requires `latest_json_url` (url ref + `/manager/latest\.json$`). Three manager fixtures rejected. |
| FR-012 | pass | versions.v1.schema.json:6-21; contract.js:363 compiles it; gen-feed.js:697 self-checks both. `C1 feed fixtures` passed (350/350). |
| FR-013 | pass | CONTRACT.md:47-102 one row per C1 path (Path/Type/Required/Rule/Writer/Reader); §3 (:114-142) reader rules 1-4. Test `CONTRACT.md documents every code and field` passed. |
| FR-014 | pass | contract.js:215-237 `findInstallDuplicates` (ASCII fold for vst3_bundle, au_component, macos_pkg_id); :427-442 emits E_INSTALL_DUPLICATE naming field and both plugins. Five dup fixtures give the code. CONTRACT.md:128-135. |
| FR-015 | pass | release.schema.json:7 `additionalProperties: false`; :47 required tag, name, version, pub_date, assets, install; :143 install requires four names; no channel property. channel-stable/channel-beta give E_MANIFEST_SCHEMA. |
| FR-016 | pass | release.schema.json:13-44 five-key enum, min/max 3, windows+linux required, per-prefix format/min_os; `asset` :84-111. Departure stated CONTRACT.md:163-167. gen-feed.js:590-597 `checkAssembledEntry` degrades on failure. |
| FR-017 | pass | contract.js:489-596 `checkReleaseManifest` (tag, asset names, bundle names, per-tag URL, binding, listing files/digest/layout). Fixtures tag-mismatch, asset-name, url-other-tag, binding, files-count, digest, layout each give their code. CONTRACT.md:174-189. |
| FR-018 | pass | No channel in release.schema.json (closed :7); checkReleaseManifest never reads it; gen-feed.js:558/:568 set channel from `row.isPrerelease` only. pre-plain and pre-plain-promoted manifests byte-identical (cmp: same-bytes). |
| FR-020 | pass | contract.js:265-284 `parseTrustedComment`: CR/LF rejected, 6 `:` fields, `krate:1`, name/version/platform/asset checks. |
| FR-021 | pass | contract.js:287-295 `formatTrustedComment`; :265 parse; :329-350 `parseMinisigText` returns `{untrustedComment, algorithm:'ED', keyId, trustedComment}`; exported :611-635. `C3 trusted comments` passed. |
| FR-022 | pass | contract.js:298-309 `checkBinding` throws E_TC_BINDING with `{field}`. CLI: `gen-feed: WARN E_TC_BINDING: ... trusted comment platform is "linux-x86_64", expected "windows-x86_64"`, exit=3, output byte-equal to expected.v2.json. |
| FR-023 | pass | fixtures/kat/ holds KatPlugin-1.2.3-Linux-x64.tar.gz (452 B), .minisig (0 CR bytes), kat.pub, README.md (WSL GNU tar 1.35, rsign2 0.6.7, 2026-10-30). Re-run: `Signature and comment signature verified` / `Trusted comment: krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz`, exit=0. Key dir deleted. |
| FR-024 | pass | c3-trusted-comment.test.js:256-312 `KAT binds and no secret key is committed`; rules a/b/c; KAT and fixture signatures not flagged; synthetic x.key, rule-b.txt, rule-c.txt each flagged by exactly their rule. `find ... -name '*.key'` empty. |
| FR-030 | pass | listing.js:104-108 `modeOf`; :152-158 sort/format; :323-330 `toText`/digest. KAT digest db287e49... in CONTRACT.md:303-306. `C4 listings from tar headers` passed. |
| FR-031 | pass | listing.js path, type, symlink, through-symlink, duplicate and chain rules (:31-151). Probes gave THROUGH_SYMLINK, SYMLINK_FORM, SYMLINK_ESCAPE, E_LISTING_TYPE (hardlink), DUPLICATE. Every listings/bad/*.expected-error matches. |
| FR-032 | pending-ci | listing.js:384-394 exports; tar reader verified on Windows (all good/bad fixtures, c4-listing-tar.test.js:120-154). `listingFromDirectory` throws E_LISTING_NO_POSIX_MODES on win32 (asserted). POSIX branch proven by feed-tools.yml `node-test (ubuntu-latest)` and `node-test (macos-latest)`, test `C4 listings from extracted trees`. |
| FR-033 | pass | listing.js:359-382 `checkLinuxLayout` -> E_LAYOUT; fixtures layout-extra-root-file, layout-missing-presets; gen-feed case degrade-layout. |
| FR-034 | pass | CONTRACT.md:260-263 "Windows and macOS have no archive layout (FR-034)..."; schema `else payload: false` (:129). |
| FR-040 | pass | gen-feed.js:245-282 `parseTagsText`/`buildLegacyFeed`, gitVersionCompare :204-240. `gen-feed legacy reproduces the 2026-10-29 live feed` (byte-identical) and `legacy tag order matches git versionsort` passed. |
| FR-041 | pass | gen-feed.js:72-75 v2 flags; readManifestsDir :156-168; only node:crypto/fs/path plus contract.js, no git/network. |
| FR-042 | pass | gen-feed.js:418-447 `selectReleases`; compareSemVer contract.js:143-161. `Q9 prerelease never moves the v1 version` passed. |
| FR-043 | pass | gen-feed.js:550-600 `assembleEntry` key order, notes, release_notes_url; checkManifest :494-521 checks 1 and 2 (AppId `{GUID}_is1`); manager block :694. Cases degrade-preset-subdir/-pkg-id/-uninstall-key/-no-appid/-no-setup-iss, clean-version-ahead; tests 11, 12 passed. |
| FR-044 | pass | gen-feed.js:318-330 `cutChangelogSection` (literal `## [<version>]`); `notes_markdown is one uncapped section` passed; notes-14000.json 14148 bytes. |
| FR-045 | pass | gen-feed.js:695-705 self-check -> E_OUTPUT_SCHEMA; `writeAtomic` :174-190. Probe: reject-abort-plus-degrade left pre-existing out2.json = `prev`, no .tmp. |
| FR-046 | pass | contract.js:28-72 CODES classes; gen-feed.js:349-391 abort codes, main :749-756 prints `gen-feed: <CODE>: <message>`; `--bogus` -> E_USAGE exit=2; abort case `gen-feed: E_V1_TYPE: alpha/version.json: plugin Alpha: "url" is 42...` exit=1. CONTRACT.md:419-472 table. |
| FR-047 | pass | gen-feed.js:287-304 `projectV1`; `--project-v1` :727-731; `v1 projection of v2 equals legacy` passed. |
| FR-048 | pass | gen-feed.js shebang, USAGE, `main` returns 0/1/2/3, `require.main` guard, exports. Grep for the nine plugin names in gen-feed.js, contract.js, listing.js: rc=1 (none). `generator ships no plugin roster` passed. |
| FR-049 | pass | gen-feed.js:578-584, 524-530, 675-691, 743-748. degrade-tc-binding exit=3 with one WARN, equals golden; reject-abort-plus-degrade exit=1, out2.json untouched. `gen-feed degrades a bad manifest` passed. |
| FR-050 | pass | test_update_checker.cpp:394 `UpdateChecker - feed schema v2 is read as v1` `[update]`; clean rebuild of shared_tests exit 0, 0 warnings; by-name run `All tests passed (40 assertions in 1 test case)`. macOS/Linux covered by SC-016. |
| FR-051 | pass | `git diff --stat` touches only .gitattributes, .gitignore, test_update_checker.cpp, specs/INDEX.md. `node tools/header-consumers.js --tag '[update]'` ended `CONSUMERS OUTSIDE [update]: none`. `GATE GREEN shared_tests cases=8 assertions=122 wall=1.2s workers=30`. |
| FR-052 | pending-ci | Per-OS variable (APPDATA/HOME/XDG_CONFIG_HOME) test_update_checker.cpp:28-32, `writeEnv` :60-65, `ScopedSettingsDirRedirect`, pre-write `REQUIRE(isUnder(...))` in all four cases. Windows verified: [update] gate GREEN (8 cases, 122 assertions); real %APPDATA% Iterum/Ruinae update_state.json mtime/size unchanged. macOS HOME and Linux XDG branches: ci.yml `macOS tests (shard N)` / `Linux tests (shard N)`, needs a PR to main. |
| FR-055 | pass | fixtures/feeds/cases.json 89 rows (79 invalid, 10 valid); all five platform keys in full-v2; every listed case present. `C1 feed fixtures` passed. |
| FR-056 | pass | fixtures/release/cases.json 46 rows; 6 valid manifests, none with `channel`; invalid set includes all listed codes. `C2 release.json fixtures` passed. |
| FR-057 | pass | trusted-comments.json 6 valid, 20 invalid with codes per category. `C3 trusted comments` passed. |
| FR-058 | pass | listings/good 9 archives (.tar.gz/.listing/.sha256); bad 22 archives with .expected-error; make-bad.js, README.md; raw-record asserts c4-listing-tar.test.js:120-154. `C4 listings from tar headers` passed. |
| FR-059 | pass | fixtures/gen-feed 74 cases: clean, prerelease, reject, degrade (one per raisable code), E_OUTPUT_SCHEMA via PD-16 hook. Tests 8-18 passed. |
| FR-060 | pass | feed-tools.yml:3-22 triggers, :24-25 `contents: read`, :28-34 matrix `node-test (${{ matrix.os }})`, steps checkout@v6, setup-node@v4 node 24, `npm ci --prefix tools/feed-contract`, `cargo install rsign2 --version 0.6.7 --locked`, TAP check `# fail 0` and tests>=21, rsign verify KAT, `test -z "$(git status --porcelain)"`. Addition: actions/cache@v5 for rsign. Green on all legs is SC-018. |
| FR-061 | pending-ci | write-guard.js:138-188 wraps fs write families; :192-274 child_process allowlist; all 10 test files require guard first. Windows: `tools stay offline` (a) and (c) passed; A2d git status 929 lines before/after. POSIX branch (macOS /var -> /private/var): feed-tools.yml `node-test (macos-latest)`/`(ubuntu-latest)`, subtest `(c) the guard has teeth` plus `Checkout unchanged`. |
| FR-062 | pass | tools/feed-contract/package.json `private: true`, ajv 8.20.0 exact; lockfileVersion 3; `require.resolve('ajv/dist/2020', ...)` -> `F:\projects\iterum\tools\feed-contract\node_modules\ajv\dist\2020.js`; root has no ajv. .gitignore diff only anchors `/package.json` and `/package-lock.json`. |
| FR-063 | pass | .gitattributes:24-26 `tools/feed-contract/fixtures/** -text`; `git check-attr text -- .../kat/kat.pub` -> `text: unset`. |
| FR-070 | pass | `git status`: modified .gitattributes, .gitignore, test_update_checker.cpp, specs/INDEX.md; untracked feed-tools.yml, specs/krate-manager-phase1/, tools/feed-contract/, tools/gen-feed.js. `node tools/gen-specs-index.js --check` -> `specs/INDEX.md is up to date.`; scope-check.js -> `SCOPE GREEN`. |
| FR-071 | pass | snapshot-2026-10-29: 9 version.json + 9 setup.iss match repo copies (strip-trailing-cr), tags.txt 88 lines, releases.json 88 rows, live-versions.json, README with capture commands. SC-001 passed. |
| SC-001 | pass | tap.log:1308 `ok 14 - gen-feed legacy reproduces the 2026-10-29 live feed`; `Buffer.equals`, 0 bytes differ; fixture matches `git show 84c6aab1:` blobs by sha256. |
| SC-002 | pass | tap.log:1381 `ok 15 - v1 projection of v2 equals legacy`; 10 cases (threshold >=8) all equal. |
| SC-003 | pass | tap.log:1436 `ok 16 - Q9 prerelease never moves the v1 version`; 6 of 6 rows. |
| SC-004 | pending-ci | Windows: tap.log:2051 `ok 19 - legacy tag order matches git versionsort`; 24 sets (>=20) all ok. macOS/Linux: feed-tools.yml `node-test (ubuntu-latest)`/`(macos-latest)`, step `node --test`; proof is all 24 set subtests ok with `# fail 0`. |
| SC-005 | pass | tap.log:28 `ok 1 - C1 feed fixtures`; 89 rows (>=29), 5 mention E_INSTALL_DUPLICATE. |
| SC-006 | pass | tap.log:114 `ok 3 - C2 release.json fixtures`; 7 subtests ok. |
| SC-007 | pass | tap.log:175 `ok 4 - C3 trusted comments`; 9 subtests ok. |
| SC-008 | pending-ci | Windows: tap.log:743 `ok 7 - C4 listings from tar headers`; 9 good, 22 bad, KAT, layout all ok; KAT listing sha256 db287e49...7ad6 equals sidecar. macOS/Linux: feed-tools.yml `node-test (ubuntu-latest)`/`(macos-latest)`; proof is `C4 listings from tar headers` ok with `# fail 0`. |
| SC-009 | pending-ci | Windows: tap.log:242 `ok 6 - C4 listings from extracted trees`, 9 subtests asserting `E_LISTING_NO_POSIX_MODES`. POSIX extraction branch (c4-listing-dir.test.js:47-60) runs only on feed-tools.yml `node-test (ubuntu-latest)`/`(macos-latest)`; proof is the 9 `extracted <name> lists as its committed .listing` subtests ok. |
| SC-010 | pass | tap.log:882 `ok 8 - gen-feed rejects`, 22 subtests ok incl. usage error exit 2. |
| SC-011 | pass | tap.log:1785 `ok 17 - release_notes is always empty`; 57 per-case subtests ok. |
| SC-012 | pending-ci | Windows: tap.log:1894 `ok 18 - gen-feed is deterministic`; 16 case subtests plus CRLF subtest ok. macOS/Linux: feed-tools.yml `node-test (ubuntu-latest)`/`(macos-latest)`; proof is `gen-feed is deterministic` ok. |
| SC-013 | pending-ci | Windows: tap.log:181 `ok 5 - KAT binds and no secret key is committed`; 3 of 3 synthetic flagged, 0 false positives; local rsign verify printed `Signature and comment signature verified`, exit=0. CI: `rsign verify (KAT)` step (feed-tools.yml:75-76) on all three legs. |
| SC-014 | pass | tap.log:2101 `ok 21 - generator ships no plugin roster`; tenth plugin {v2:10, legacy:10}, snapshot {v2:9, legacy:9}, 0 name hits. |
| SC-015 | pending-ci | Windows: tap.log:2076 `ok 20 - tools stay offline` with (a)(b)(c) ok; (d) `git status --porcelain` 9 lines before and after, empty diff; close-lane.log:4 `A2d GREEN ... (929 lines before, 929 after)`. CI: `Checkout unchanged` (feed-tools.yml:78-80) on all three legs. |
| SC-016 | pending-ci | Windows re-run: shared_tests rebuilt exit 0, 0 warnings, 0 errors; `GATE GREEN shared_tests cases=8 assertions=122 wall=1.1s workers=30`; by-name `All tests passed (40 assertions in 1 test case)`; junit failures=0, both sections run. close-lane A1/B1/B2 are stale (file mtime later than the lane); re-run covers it. macOS/Linux: ci.yml `test-macos`/`test-linux` shards, PR to main only; proof is the shared_tests shard reporting the case passed. |
| SC-017 | pending-ci | Code: test_update_checker.cpp:398 REQUIRE before any checker, state-file check under the temp dir, env restore asserted at the end. Windows by-name run green. macOS/Linux with SC-016. |
| SC-018 | pending-ci | feed-tools.yml:28-34 matrix; :59-73 TAP gate; Local Windows `# tests 350`, `# pass 350`, `# fail 0`, `1..24`. Proof: all three jobs green each with `TAP tests=350`; needs the user's push. |
| SC-019 | pass | Wall 1.77 s, TAP `# duration_ms 1685.1767`, `# tests 350`; close lane `wall=2.1s`; tap.log:2107 `ok 22 - no serial test flags`, 0 hits. |
| SC-020 | pass | scope-check.js: `--- filtered (0 out of scope)` x2, `SCOPE GREEN`; `gen-specs-index.js --check` exit 0; `git check-ignore` of the two package files exits 1; release.yml and plugins/*/src unchanged. HEAD 966ef217 is one commit past 84c6aab1 (workflow harness only). |
| SC-021 | pending-ci | Code: guard + REQUIRE opens each of the 4 `[update]` cases. Stats before/after unchanged (Iterum mtime=2026-10-29 18:36:03.023402500 +0100 size=72, Ruinae 18:36:02.478868800 +0100 size=72). macOS/Linux: ci.yml `test-macos`/`test-linux` with SC-016. |
| SC-022 | pass | tap.log:907 `ok 9 - channel comes from the release list only` plus C2 part (a) tap.log:102. |
| SC-023 | pass | tap.log:1178 `ok 10 - gen-feed degrades a bad manifest`, 44 subtests ok; each degrade asserts exit 3. |
| SC-024 | pass | tap.log:1215 `ok 11 - install cross-check is gated on version`, parts (a)-(d) ok. |
| SC-025 | pass | tap.log:1258 `ok 12 - windows_uninstall_key equals the setup.iss AppId`, parts (a)-(d) ok. |
| SC-026 | pass | tap.log:65 `ok 2 - feed rejects shared install facts`, 5 subtests ok. |
| SC-027 | pass | tap.log:1289 `ok 13 - notes_markdown is one uncapped section`, 4 subtests ok. |
| CC-system | pass | No app (`apps/krate-manager`: No such file or directory). Grep for real plug-in/ProgramData/registry/elevation paths: only fixture bundle names and toolchain paths. No test requires http/https or calls fetch; github.com only as string templates. Only network step is the agent-run capture-snapshot.js. Writes guarded by write-guard.js; C++ redirect points update_state.json at a temp dir; `[update]` gate green. |
| CC-verify | pass | Vacuous: no download/extract/execute path in Phase 1. Contract in place (C3 binding, KAT verify rc=0 locally, CI step). Download->verify->extract ordering must be re-verified in Phases 7/8. |
| CC-zipslip | pass | No extraction in Phase 1. listing.js guards (checkCharacters :47-52, canonicalPath :65-70, checkLink :75-95, through-symlink :126/:149) with committed bad archives and entry-level cases, all in `# pass 350`. POSIX extracted-tree branch is PENDING-CI. |
| CC-helper | pass | Vacuous: no elevated helper; no apps/ dir; grep finds only Node test helpers. Verify at roadmap Phase 10. |
| CC-capabilities | pass | Vacuous: no Tauri project, no Cargo.toml/tsconfig.json, no capability file. Real check from Phase 4. |
| CC-signing | pass | No certificate needed; feed-tools.yml has `contents: read`, no secrets. KAT key made in os.tmpdir() and deleted; no key files committed; only kat.pub (`minisign public key: 440FA75168B852F7`). |
| CC-warnings | pass | Rust/TS gates cannot run (no app; start Phase 4). shared_tests rebuild rc=0, `grep -c warning` = 0. clang-tidy on test_update_checker.cpp: rc=0, 71 `warning:` lines, all Catch2 REQUIRE/CHECK false positives filtered by run-clang-tidy.ps1:381-385; other warnings/errors 0. `node --check` passes on all phase .js files. |
| CC-lints | pass | `lint-serial-test-runs: clean (64 files)`; `self-test OK (8 serial runs caught, 11 allowed)`; `check-portability: all clear -- 1 compiled.`; header-consumers `CONSUMERS OUTSIDE [update]: none`. The roadmap's literal gate command is refused by run-gate.js (`GATE RED usage: the filter must exclude timing tests (~[performance] missing)`); re-run with the full filter gave `GATE GREEN shared_tests cases=8 assertions=122 wall=1.2s workers=30`. |
| CC-feed-compat | pass | update_checker.cpp:228/:237/:251-255 reads only plugins, version, download_url, release_notes. `cmp live-versions.json expected.legacy.json` BYTE-IDENTICAL; `--project-v1` -> `v1 projection of v2 == live: true`; C++ case `All tests passed (40 assertions in 1 test case)`; [update] gate GREEN 8 cases / 122 assertions. |

## PENDING-CI list

The user pushes; nothing here was pushed.

feed-tools.yml jobs `node-test (ubuntu-latest)` and `node-test (macos-latest)` (and `windows-latest` for SC-018), step `node --test`, plus `rsign verify (KAT)` and `Checkout unchanged`:
- FR-032 / SC-009: `C4 listings from extracted trees`, 9 subtests `extracted <name> lists as its committed .listing` ok.
- FR-061 / SC-015: `tools stay offline` subtest `(c) the guard has teeth`; `Checkout unchanged` step (`test -z "$(git status --porcelain)"`).
- SC-004: `legacy tag order matches git versionsort`, 24 sets ok, `# fail 0`.
- SC-008: `C4 listings from tar headers` ok, `# fail 0`.
- SC-012: `gen-feed is deterministic` ok (goldens match on all legs).
- SC-013: `rsign verify (KAT)` step (feed-tools.yml:75-76) on all three legs.
- SC-018: all three jobs green, each `TAP tests=350`.

ci.yml `test-macos` (`macOS tests (shard N)`) and `test-linux` (`Linux tests (shard N)`), which trigger only on a PR to main or a push to main:
- FR-052, SC-016, SC-017, SC-021: every shard green, with the shared_tests shard passing `UpdateChecker - feed schema v2 is read as v1` and the three existing `[update]` cases; `REQUIRE(isUnder(getAppSettingsDirectory("FeedV2Probe"), settings.root()))` holds on HOME and XDG_CONFIG_HOME.

## Implementation notes (deviations reported by task agents)

- T003: package.json and package-lock.json were git-ignored (.gitignore:93-94), so the plan's `npm ci` in CI could not work. Resolved by anchoring those two lines and putting ajv in `tools/feed-contract/package.json` (FR-062, SC-020 pass). `npm audit` reports 5 vulnerabilities in the root's pre-existing dependencies (axios, fast-xml-builder, fast-xml-parser, follow-redirects, form-data), not in ajv; untouched.
- T004: write-guard checks `GIT_DIR`/`GIT_WORK_TREE`/`GIT_INDEX_FILE` in the effective child env, refuses `--separate-git-dir` and out-of-tmp `git init <dir>`, and fails closed on unresolvable paths. hygiene.test.js loads child_process as `'child' + '_process'` so its own scan passes; later tasks spawning git under a hook should strip those three env keys.
- T005: contract.js also exports ASSET_SUFFIX; E_MANIFEST_DUPLICATE is degrade class per the Q-A ruling; extra invalid trusted-comment rows beyond FR-057.
- T006: WSL scripts fed via `wsl -e bash -s` on stdin; extra-root-item subtest follows tasks.md, not plan §7.1; bad-archive names chosen by the agent and pinned; the pax `size` keyword is ignored (Phase 2 must handle files over 8 GiB); good archives cannot be rebuilt byte for byte; fixtures/README.md owes a listings section (listings/README.md carries it).
- T007: the spec's External-facts row (spec.md:117) and FR-023 wording say rsign writes CRLF on Windows. Measured with rsign2 0.6.7 via Node spawnSync: LF only. The spec text needs amending. A first make-kat run failed before key generation (cross-drive path.relative bug), its empty key dir was removed, and a second run succeeded.
- T008: capture refused to write unless the data matched the spec's recorded counts (88 tags/releases, 9 plugins). README records capture time 2026-10-30T07:28:30Z under a 2026-10-29 directory name.
- T009: schema deviations for Ajv strict mode: platform/asset refs sit in their own `patternProperties` entry; C2 `windows-x86_64`/`linux-x86_64` required pair moved into an `allOf` subschema; `type: object` on all `properties` subschemas; v1 schema requires top-level `plugins`.
- T010: §4 order of checkReleaseManifest documented; §6 holds the KAT listing digest.
- T011: unknown platform keys skip C3 comment parsing (D1); `validateFeed` returns `{valid, errors}`; root-level Ajv pair has a leading space.
- T012: `C2_VALID_RULES` separate from `C2_RULES`; rows carry `{error, ajv}`; listing.js required lazily inside checkReleaseManifest to avoid a require loop.
- T014: gitVersionCompare is an independent strverscmp implementation checked against real git 2.49.0.windows.1; v2 mode was added in T015.
- T015: publishedAt pattern checked on every non-draft release row (stricter than plan). **Open gap, needs a ruling:** a manifest whose `name` differs from the plugin only in case (ALPHA vs Alpha) passes every planned check, so the entry would carry signatures binding `ALPHA`; an E_MANIFEST_MISMATCH on name would close it.
- T016: gen-feed.js unchanged; reject-input-releases uses a malformed row, not unparseable JSON (OS-independent expected.error); reject-reserved-name lives in directory `reserved` (Windows cannot create `nul`); degrade cases have no expected.legacy.json.
- T017: shared check lambda `checkReadAsV1` in each section; `update_state.json` removed after the reference run so `exists()` proves the document's own run.
- T018: added `Install rsign2` step name and an actions/cache@v5 step for the rsign binary.
- T020: reported BLOCKED on one scope item, `.claude/workflows/krate-manager-phase.js` (harness edit). The later verification shows that file is committed as HEAD 966ef217 and `SCOPE GREEN`, so this no longer blocks. close-lane.js adds step B2 (single case run by name, junit) because run-gate shard logs do not name passing cases. close-lane.log A1/B1/B2 are stale relative to the final test_update_checker.cpp; SC-016 was re-run directly.
- Pre-existing note: `?? specs/Krate-manager-roadmap.md` is an allowed untracked baseline from the roadmap stage.

## Remaining steps for the human loop

1. Review the diff and this report; rule on the T015 case-only manifest-name gap and fix the spec.md:117 / FR-023 CRLF wording (T007).
2. Run the main-loop lanes in `specs/krate-manager-phase1/evidence/`: `node specs/krate-manager-phase1/evidence/close-lane.js` (re-run it, since its A1/B1/B2 are stale) and `node specs/krate-manager-phase1/evidence/scope-check.js`. Clean test output afterwards with `node tools/clean-test-output.js --apply`.
3. Commit (new commit, no amend).
4. Push the branch and open a PR to main. That runs feed-tools.yml on the three OS legs and the ci.yml macOS/Linux shards, which proves every PENDING-CI item above.
