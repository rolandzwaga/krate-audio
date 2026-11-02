# Krate Manager Phase 11 (krate-manager-phase11) - Compliance

Overall status: COMPLETE (PENDING-CI: 22 item(s))

## Honest gap list (first)

1. RESOLVED 2026-11-02 (user ruling: Departure 9 accepted, signature only). CC-verify was PARTIAL. The manager's own update package gets minisign verification only (no size, no SHA-256). That departs from the Cross-Cutting "size, SHA-256, minisign signature" wording (roadmap lines 1013-1014). It is recorded as spec.md:392-401 Departure 9: "(Found at compliance 2026-11-02; NOT YET RULED, needs the user.)" Until the user rules, the rule is not met for the manager's own update bytes and has no ruled exception. The spec offers a sha256/size field written by gen-manager-latest.js and checked on `Update::raw_json`.
2. RESOLVED in the main loop 2026-11-02: map_error sends a reqwest error whose source chain holds a serde_json::Error to Manifest (harness lane 27 ok), and feed-pipeline.test.js was added to the scope allow list (GitHub serves `releases/download/<plugin>/v<x>/` with a raw slash); close lane GREEN. Task notes vs item evidence conflict (stale notes, flagged for review). T018 (status "blocked") reported two reds: L-HARNESS `not ok corrupt-manifest truncated JSON: check error E_SELF_UPDATE_NETWORK ...` (26/27) and L-SCOPE `tools/feed-contract/tests/feed-pipeline.test.js is not a Rule 11 path`. The later verified item evidence (FR-080, FR-082, SC-007, FR-092) reports `harness: 27 ok, 0 not ok` and `scope-check: 80 paths (48 under apps/krate-manager/, 32 outside): OK (base 30878b36)`. I did not re-run them. The human loop should re-run `harness-lane.js` and `scope-check.js` to confirm both reds are closed (map_error decode to Manifest; the feed-pipeline.test.js:123 one-line fixture edit).

## Compliance table

| ID | Verdict | Evidence (abridged from verified record) |
|---|---|---|
| FR-001 | pass | Cargo.toml:44-45 `tauri-plugin-updater = { version = "=2.14.1", default-features = false, features = ["rustls-tls"] }`; src-tauri/Cargo.toml:34-35. `cargo clippy --workspace --all-targets -- -D warnings` exit 0, 0 warnings; `cargo test --workspace --locked` exit 0. |
| FR-002 | pass | tauri.conf.json:25-33 plugins.updater (pubkey, endpoint https://krateaudio.com/manager/latest.json, requireSignedVersion true, allowDowngrades false, windows passive); pubkey equals base64 of krate-signing.pub, no CR byte. |
| FR-003 | pass | config_guard.rs:1151-1290 `check_updater_config`; injection.rs:6605-6612; 18 plants. `cargo test ... --test config_guard --test injection`: 11 passed, 39 passed, 0 failed. |
| FR-004 | pending-ci | manager-ci.yml:507-517 and release-manager.yml:344-355 exit 1 if `plutil -p` shows LSFileQuarantineEnabled. Proof: `check (macos-latest)` and `build (macos-latest)` notice `plist: no LSFileQuarantineEnabled::absent`. |
| FR-010 | pass | core self_update.rs (508 lines), imports core/serde/std/url only; manager-ci `core dependency tree` step (:239-253) refuses tauri/tokio/reqwest. |
| FR-011 | pass | self_update.rs:220-228 check_update_url, :255-271 check_update_package_url; harness `ok api-host`, `ok latest-path`, `ok endpoint-latest-path`. |
| FR-011a | pass | self_update.rs:288-343 hop checks and redirect_decision (MAX_REDIRECTS 5); src-tauri :153-161 Policy::custom; harness `ok redirect-to-api-host`, `ok redirect-to-http`, `ok redirect-loop`. |
| FR-012 | pass | core :346-350 proxy_choice -> http::route_choice; callers return before any request on Err; harness `ok socks-proxy`. |
| FR-013 | pass | core :62-79 eight variants with `E_SELF_UPDATE_*` sentences; src-tauri :296-343 map_error names 32 variants plus fallback; harness `ok map-errors` (31 rows). |
| FR-014 | pass | core :411-496 ManagerUpdateView / UpdateAction / view / update_action; harness `ok deb-install`, `ok good`. |
| FR-015 | pass | core :354-360 self_update_busy; src-tauri :250-252, :271-273; harness `ok busy`. |
| FR-016 | pending-ci | Core test `context::tests::release_for_restart_frees_the_lock` passed. Relaunch taking the lock is proven only by `updater-e2e (macos-latest)` case user-placed and `updater-e2e (ubuntu-24.04)` case appimage asserting `launched-<new>`. |
| FR-017 | pass | core :363-391 SELF_UPDATING registry; src-tauri :262 InFlightGuard; test in_flight_flag; harness busy-after-download. |
| FR-018 | pass | core :394-408 SelfUpdating + action_gate; gates first in download/install/remove/elevate; injection.rs:6620-6625; test install::tests::refuse_while_self_updating. |
| FR-019 | pass | src-tauri :266-273 busy re-check after download; harness `ok busy-after-download`. |
| FR-020 | pass | commands.rs manager_update_check / manager_update_install; build.rs:4-13, lib.rs:112-123 ten commands; HANDLER_LIST pinned. |
| FR-021 | pass | capabilities/main.json:5-16 ten permissions; config_guard.rs:1267-1288 bans updater:/process:/shell:/fs:/http:. |
| FR-022 | pass | src-tauri self_update.rs build_updater :135-165, UpdateInstaller :58-65, only `update.install(bytes)` at :77; injection.rs:6595-6601 plants. |
| FR-023 | pass | src-tauri :187-215, :254-260; harness `ok recheck-replaces`, `ok failed-install-clears`, `ok busy`. |
| FR-024 | pass | manager_update.ts:40-51, :216-239; `npx vitest run` 71 passed; tsc --noEmit ok. |
| FR-025 | pass | manager_update.ts:265-289; status.ts setActionsEnabled; the two vitest cases are among the 71 passing. |
| FR-026 | pass | main.ts:11-18; commands.rs self_update_http; vitest cases for rejected/succeeded/locked. |
| FR-027 | pass | core :19-24, :167-176; src-tauri :145, :155, :221; injection.rs:6628-6635; harness `ok stall`, `ok good`. |
| FR-030 | pass | bundle.js parseArgs/releaseTauriArgs; `node --test bundle.test.js` pass ('bundle release mode argv'). |
| FR-031 | pass | bundle.js stageUniversalHelper (lipo -create); universalSidecars; bundle.test.js 4/4 pass. |
| FR-032 | pass | bundle.js assetTable/copyAssets/resolveAsset `E_BUNDLE_ASSET <glob> matched <n>`; 'bundle release asset table' passes. |
| FR-033 | pending-ci | release-manager.yml:319-334 lipo step. Proof: `build (macos-latest)` notices `lipo::krate-manager: arm64 x86_64` and `...krate-manager-helper: arm64 x86_64`. |
| FR-040 | pass | gen-manager-latest.js flags and loopback rule; no network/child process; node --test 'writes per-tag latest.json' and 'refuses what it cannot verify' ok. |
| FR-041 | pass | ASSET_TABLE seven keys, no .deb key; deterministic JSON. |
| FR-042 | pass | verifySig :142-168, checkAssetSet :115-139; writeAtomic only after all checks. |
| FR-043 | pass | feed-publish.js checkLatestJson(bytes, tag, version, repo); subtest (7); 'collect copies only a valid manager latest.json' ok. |
| FR-044 | pass | manager-release-preflight.js:72-82 changelogSection; gen-manager-latest.js:305 copies notes as-is. |
| FR-050 | pass | release-manager.yml:12-25 workflow_dispatch only, dry_run default true, contents: read, concurrency; shape mutations reported. |
| FR-051 | pass | release-manager.yml:33-82 preflight; preflight() :151-222 ordered checks; 'manager release preflight refuses by name' ok. |
| FR-052 | pass | release-manager.yml:97-661 build/sign/sign-dry/main-only structure, no secrets in build; shape test ok. |
| FR-053 | pass | release-manager.yml:265-317 and :515-531, :631-647 verification steps. |
| FR-054 | pass | release-manager.yml macOS steps :319-378 present (runtime result is SC-013 on CI). |
| FR-055 | pass | sign job :432-513 ordering, delete key step; shape plants reported. |
| FR-056 | pass | release-manager.yml :161-263; local `cargo tree ... \| grep -c updater-e2e` printed 0; helper tree ci-temp-roots count 0. |
| FR-057 | pass | publish job :1496-1607 (draft, no --clobber, SHA-256 compare, `--latest=false`). |
| FR-058 | pass | feed :1733-1740, feed-dry :1742-1749. |
| FR-059 | pass | dry-publish :1610-1730, no gh/tag/Pages step. |
| FR-060 | pass | tauri.conf.json:23 nsis currentUser unchanged. |
| FR-061 | pass | package-macos :664-776 pkgbuild, no --scripts/--ownership. |
| FR-062 | pass | package-macos :714-762 payload and pkg facts checks (runtime result SC-013/SC-021). |
| FR-063 | pass | manager-release-preflight.js:85-110 releaseBody; 'manager release body is exact' ok. |
| FR-064 | pending-ci | build (linux) in ubuntu:22.04 with appimage,deb; proof is `first-install (ubuntu-22.04-container)` install (:1404-1417) and launch 'running after 12 s'. |
| FR-065 | pending-ci | 'glibc floor' step :380-394; proof is `build (linux)` notice `glibc floor::<floor> (container <have>)` (SC-018). |
| FR-070 | pass | updater-e2e feature not default; one `mod e2e` in self_update.rs; lib.rs 2 cfg sites; injection.rs:6652. |
| FR-071 | pass | updater-e2e job :780-1256 structure; e2e-update-server binds 127.0.0.1; 'e2e update server serves only its directory' ok. |
| FR-072 | pending-ci | Cases exist (windows, user-placed, pkg-installed, appimage, deb). Whether the hosted runner can raise and cancel the SecurityAgent prompt is unmeasured. Proof: `updater-e2e (windows-latest)`, `(macos-latest)`, `(ubuntu-24.04)` (SC-015). |
| FR-073 | pending-ci | Local: updater-e2e cargo tree count 0, marker count 1 in source, close-lane A1f clippy `warning-lines=0 exit=0`. Release-binary zero count needs `check (...)` step 'updater-e2e absent' notice. |
| FR-074 | pass | 'annotate' step :1235-1245; per-leg `::error title=$leg::` messages; logs uploaded. |
| FR-075 | pass | first-install :1259-1295 matrix and artifacts. |
| FR-076 | pending-ci | Per-leg install/launch code present (:1297-1493). Proven only by `first-install (<leg>)` on CI (SC-016). |
| FR-077 | pass | launch steps assert process presence only; no gh/curl/wget in first-install/updater-e2e run steps; shape plant reported. |
| FR-078 | pass | 'block live host' on every first-install leg with self-check; shape plant reported. |
| FR-080 | pass | updater-refusals harness: 27 `ok` lines plus confinement, `harness: 27 ok, 0 not ok`, `harness: wall 2210 ms`, rc=0 (Windows). |
| FR-081 | pass | temp_roots confinement; `--plant committed-key` printed `E_HARNESS_KEY: the harness key is the committed release key` rc=1; `--plant bind-any` printed `E_HARNESS_BIND: the harness binds 127.0.0.1 only, not 0.0.0.0:0` rc=1. |
| FR-082 | pass | main.rs LINE_COUNT 27; output `harness: 27 ok, 0 not ok`, rc=0. |
| FR-083 | pass | harness clippy `Finished dev profile` rc=0, fmt --check exit 0; manager-ci shape test ok. |
| FR-084 | pending-ci | Windows: 27 ok, 0 not ok, plants rc=1 (d9-windows.txt `D9 WINDOWS LOADS`). Proof for other legs: `check (macos-latest)`/`check (ubuntu-24.04)` notice `updater refusals::27 ok, 0 not ok; plants exit 1`. |
| FR-090 | pass | Only panicking/refusing installers locally; real installers/hosts edits only in release-manager.yml hosted jobs. |
| FR-091 | pass | feed-tools.yml wired; shape test mutation table; `node --test` of the 5 Phase 11 test files: tests 219, pass 219, fail 0. |
| FR-092 | pass | `git diff --name-only 30878b36 -- <fenced paths>` printed nothing; `scope-check: 80 paths (48 under apps/krate-manager/, 32 outside): OK (base 30878b36)`. |
| FR-093 | pass | Roadmap edits (a)-(c),(e) present; status line (d) is written only by the workflow's mark-roadmap step after zero fail/partial (krate-manager-phase.js:766-773), so absent now by design. |
| FR-094 | pass | config_guard.rs:963 [features] pin; injection.rs:6586-7180 rules with plants; injection 39 passed, 0 failed. |
| FR-095 | pass | required-tests.json updated; close-lane B2 `::notice title=required tests ran (Windows)::372 cargo + 70 vitest + 6 node`. |
| FR-096 | pending-ci | 'roster absent' unchanged; local target/release/krate-manager.exe is older than self_update.rs so proves nothing. Proof: `check (windows/macos/ubuntu)` step 'roster absent' notice on the updater-linked binary. |
| FR-097 | pass | `.gitattributes` line added; `git check-attr -a tools/feed-contract/keys/krate-signing.pub` printed `text: unset`. |
| SC-001 | pass | `cargo test --workspace --locked` exit 0, core 296 passed 0 failed; url_policy, redirect_hops ok. Deviation: spec row `https://API.GITHUB.COM/x` is tested as `https://API.GITHUB.COM./x` (trailing dot); dotless form covered by url::Url lowercasing. |
| SC-002 | pass | proxy_choice_matches_core ok; `assert_eq!(matched, 5)` passed (5/5). |
| SC-003 | pass | codes_and_sentences, view_from_update, packaging_rule ok; vitest 71 passed. |
| SC-004 | pass | updater_config_pinned, planted_updater_config_fails_the_guard, endpoint_matches_feed_tool ok (config_guard 11 passed). |
| SC-005 | pass | busy_rule ok. |
| SC-006 | pass | release_for_restart_frees_the_lock ok (spec's `Locked` is code variant `Held`). |
| SC-007 | pass (Windows); macOS/Linux pending-ci | Windows: 27 ok lines, `harness: 27 ok, 0 not ok`, plants rc=1; macOS/Linux legs via manager-ci.yml step `updater refusals` (:199-235). |
| SC-008 | pass | `node --test scripts/*.test.js`: 'bundle release mode argv', 'bundle release asset table' ok, tests 6 pass 6 fail 0. |
| SC-009 | pass | `node --test tools/feed-contract/tests/*.test.js`: tests 869, pass 864, fail 0, skipped 5 (POSIX-only pre-existing). |
| SC-010 | pass | 'collect copies only a valid manager latest.json' ok incl. group (7). |
| SC-011 | pass | 'manager release preflight refuses by name' and 'manager release body is exact' ok. |
| SC-012 | pass | All plant tests ok; shape test 38 mutation subtests ok; clippy/fmt/tsc/eslint all exit 0, 0 warnings. |
| SC-013 | pending-ci | Needs `build (macos-latest)` lipo/plist/codesign steps. |
| SC-014 | pending-ci | Needs `dry-publish` (:1610) and `publish` (:1496); `checkLatestJson` at :1713-1717. |
| SC-015 | pending-ci | Needs `updater-e2e (<os>)` jobs; local early signal `ok rsign-format`. |
| SC-016 | pending-ci | Needs `first-install (<leg>)` jobs. |
| SC-017 | pending-ci | Needs manager-ci.yml `check` on every leg: 'clippy updater-e2e', 'updater refusals', 'updater-e2e absent'. Local updater-e2e clippy exit=0, 0 warnings. |
| SC-018 | pending-ci | Needs `build (linux)` 'glibc floor' notice. |
| SC-019 | pending-ci | Needs a `dry_run: true` dispatch by the user; ls-remote before/after readings do not exist yet. |
| SC-020 | pending-ci | Phase 12 exit criterion; needs the user's first real dispatch. |
| SC-021 | pending-ci | Needs `package-macos`; evidence/packageinfo-dry-run.xml does not exist yet. |
| SC-022 | pending-ci | Needs `build (...)` marker/ci-temp-roots steps and updater-e2e 'marker present'. Static half held locally. |
| SC-023 | pass | in_flight_flag and refuse_while_self_updating ok; harness `ok busy-after-download`, `ok stall`; vitest cases ok. |
| SC-024 | pass | timeouts ok; harness `ok stall`, `ok production-inputs`; injection pins green. |
| SC-025 | pass | no_context_defaults ok; vitest rejected/succeeded/locked cases ok. |
| SC-026 | pending-ci | Needs `first-install` every leg 'block live host' self-checks. |
| CC-system | pass | No real-system paths/APIs in Phase 11 files; harness uses TempDir roots, trap proxy, PanickingInstaller; `HARNESS LANE GREEN (Windows, 27 ok, 2225 ms)`. |
| CC-verify | pass (ruled departure) | Plugin-path ordering unchanged. Self-update path verifies minisign before install (updater.rs:891, :923, :1907; requireSignedVersion true). Gap: no size/SHA-256 check on the manager's own update bytes; Departure 9 recorded but NOT RULED (spec.md:392-401). Ruled 2026-11-02: the user accepted Departure 9 (signature only); spec.md Departure 9 and Clarifications record it. |
| CC-zipslip | pass | extract.rs unchanged; `entry_path_rejects_every_bad_form` and `bad_fixtures_fail_with_expected_code` ok, `2 passed; 0 failed`. |
| CC-helper | pass | crates/helper unchanged; `cargo test -p krate-manager-helper`: 46 passed; 0 failed, 1 passed. |
| CC-capabilities | pass | main.json exactly 10 permissions; config_guard 11 passed; allow-download-plugin justified by Phase 7 FR-066/Q6 ruling (spec.md:539-545). |
| CC-signing | pass | Ad-hoc macOS signing, no cert/notarize/signtool; only minisign secrets; no private key in tree. |
| CC-warnings | pass | clippy -D warnings (workspace, updater-e2e, harness), cargo fmt --check, `npm run typecheck`, `npm run lint` all EXIT 0. |
| CC-lints | pass | `lint-serial-test-runs: clean (64 files)`; `--self-test`: `self-test OK (8 serial runs caught, 11 allowed)`; no C++ changes. |
| CC-feed-compat | pass | versions.json schema unchanged; `node --test tools/feed-contract/tests/*.test.js`: tests 869, pass 864, fail 0, skipped 5. |

## PENDING-CI list (user pushes; nothing pushed here)

- FR-004, SC-013: release-manager.yml `build (macos-latest)`, manager-ci.yml `check (macos-latest)`: notice `plist: no LSFileQuarantineEnabled::absent`; lipo `arm64 x86_64`; LSMinimumSystemVersion 15.0; codesign verify + adhoc.
- FR-016, FR-072, SC-015: `updater-e2e (windows-latest|macos-latest|ubuntu-24.04)`: `launched-<new>` markers, Windows ProductVersion old to new, pkg-installed SecurityAgent prompt then `error-E_SELF_UPDATE_INSTALL`, deb `error-E_SELF_UPDATE_UNSUPPORTED` with 0 package GETs.
- FR-033, FR-065, FR-064, SC-018: `build (macos-latest)` lipo notices; `build (linux)` `glibc floor` notice; `first-install (ubuntu-22.04-container)` install + 'running after 12 s'.
- FR-073, SC-022, SC-017: `check (...)` and `build (...)` 'updater-e2e absent' / 'ci-temp-roots absent'; `updater-e2e` 'marker present'; 'clippy updater-e2e' notice.
- FR-084, SC-007 (macOS/Linux): `check (macos-latest|ubuntu-24.04)` 'updater refusals' notice `27 ok, 0 not ok; plants exit 1`.
- FR-076, SC-016, SC-026: `first-install (<leg>)` all four legs (block live host, install, launch).
- FR-096: `check (...)` 'roster absent' on the updater-linked binary.
- SC-014, SC-019: dry_run dispatch of release-manager.yml (clean, drifted version, existing tag, removed CHANGELOG heading); `dry-publish` checkLatestJson; `sigs not committed key` 3 of 3 E_MINISIG_KEY_ID.
- SC-020: first real dispatch (Phase 12).
- SC-021: `package-macos` payload + `count(//relocate/*)` = 0; copy PackageInfo to evidence/packageinfo-dry-run.xml.
- Also from T018: FR-055 key half (`sign-dry` rsign2 0.6.7, verify, delete key), SI-40 (`--repo` == `rolandzwaga/krate-audio`), PD-34/R-21 (NO_STRIP linuxdeploy), src-tauri `not(windows)` paths on macOS/Linux clippy.

## Implementation notes (deviations reported by task agents)

- T001: three `#[expect(dead_code)]` placeholders (to be removed by T003/T006); `UpdateFacts` derives Clone/Debug/PartialEq/Eq; `SelfUpdateError::Url` uses one sentence for all reasons; free space on F: was under 4 GB so the shared target was used. Lock additions (filetime, hyper-rustls, objc2-osa-kit, osakit, tar, tauri-plugin-updater, tokio-rustls, xattr) with 0 removed/changed.
- T002: old D9 clarification kept with an appended note that ruling P-5 supersedes it; SI-30..33 line cites are revision-3 numbers; FR-080 still says keys are "made in a temp dir" though PD-11 says in memory (no SI/TA row covers it).
- T003: SC-003/plan "serialized notes hold no `<`" cannot hold with `notes == notes::blocks(body)`, since notes::blocks keeps inline HTML as literal text. The test asserts `<` appears only inside a `text` value. Ruling needed: change spec wording to "no `<` outside a text inline". Also `redacted` applies redact_userinfo per word; empty redirect chain returns Policy.
- T004: used `Arc::try_unwrap` instead of `Arc::into_inner` so the lock can be put back (StillShared).
- T005: used full `crate::self_update::action_gate` path because install.rs has its own private `action_gate`.
- T006: calls `UpdateInstaller::install(installer, ...)` to satisfy the token rule; added `#[derive(Clone)]` to UpdateInputs; AvailableView date is the raw `pub_date` string. Non-Windows restart branch and macOS current_binary check not compiled on Windows (pending-ci). Per T014/T018, map_error originally sent reqwest decode errors to Network; the later item evidence shows the harness at 27 ok (see gap 2).
- T007: notes parsed via toStatusView one-card trick (view.ts toBlock stays private); `mountStatus` gained an `onMount(section)` parameter; Confirm/Cancel labels chosen to match INSTALL_TEXTS.
- T008: macOS asset rows always read the universal dir; Linux without `--bundles` gets both AppImage and .deb rows; `CARGO_TARGET_DIR` mapped for `target/`.
- T009: request paths matched exactly, no decoding; symlinks refused outright.
- T010: gen-manager-latest `--check-only` requires only version/tag/assets/pubkey; `Krate-Manager-app.zip` must not be in the assets dir (TA-11).
- T011: tags file of wrong shape fails closed as E_PREFLIGHT_TAG_LIST; D2 endpoint is a constant in the tool, not imported from feed-publish.js.
- T012: feed-pipeline.test.js:123 fixture used `manager%2Fv`; one-line change to `manager/v` is needed (outside T012's list; handled later per item evidence).
- T013: `production_config_only_in_allowed_tests` red on T003/T004 test bodies at the time (policy call on allow-lists); the final injection run reports 39 passed, 0 failed.
- T014: Update obtained via `build_updater(...).check()` in good/socks-proxy/rsign-format (2 GETs of latest.json, package GET still exactly once); committed-key path uses six `../`; panics reported as printed `not ok` lines.
- T015: package-macos ditto zip travels as a separate artifact `release-app-macos`; sign/sign-dry both upload the signing public key; updater-e2e Linux uses a persistent Xvfb; added `base64 -w0 (macOS)` step; APPIMAGE_EXTRACT_AND_RUN banned file-wide. Untested until CI: empty `container:` on Windows/macOS legs, background server across steps on Windows, SecurityAgent observability on hosted macOS, WebKitGTK startup on ubuntu-24.04.
- T016: harness-lane fallback runs the Windows and WSL chains concurrently; close-lane cleanup of F:\tmp left to the main loop; roadmap allowed as a whole file in scope-check. Incident: `gen-specs-index.js --help` rewrote specs/INDEX.md and was restored with `git restore`.
- T017: hygiene.test.js has no TAP plan so only scan lists changed; feed-tools.yml PLAN 1..64.
- T018: status "blocked" with the two reds noted in gap 2; INDEX.md regenerated (278 specs).

## Remaining steps for the human loop

1. Rule on Departure 9 (CC-verify): accept minisign-only with an exception, or add a sha256/size field to latest.json checked on `Update::raw_json`.
2. Rule on the T003 wording change (no `<` outside a text inline).
3. Re-run `node specs/krate-manager-phase11/evidence/harness-lane.js` and `node specs/krate-manager-phase11/evidence/scope-check.js` to confirm the two T018 reds are closed.
4. Review the diff; run the main-loop lanes in specs/krate-manager-phase11/evidence/ (`close-lane.js`, `wsl-lane.js`); check F: free space first (1.7 GB at T018) and run `node tools/clean-test-output.js --apply`.
5. Commit (no amend), then push so CI proves the PENDING-CI list; dispatch release-manager.yml with `dry_run: true` for SC-014/SC-019/SC-021.
