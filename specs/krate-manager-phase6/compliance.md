# Krate Manager Phase 6 compliance (krate-manager-phase6)

Overall status: **COMPLETE (PENDING-CI)**. Zero fail or partial items. 18 items are pending-ci (8 FR, 10 SC); main-loop close lane green 2026-10-31. Nothing was pushed, so the macOS and Linux legs have not run.

Open points for the human loop, stated first:
- 18 pending-ci items, listed below (FR-004, 006, 020, 021, 030, 033, 036, 080 and SC-003, 013, 014, 015, 022, 023, 024, 025, 027, 029). The roadmap status line says 18.
- Main-loop close lane: evidence/close-lane.log (2026-10-31T18:07:45Z, wall=117.5s, 38 GREEN steps, last line `CLOSE LANE GREEN`). The first run (18:04Z) was red only on A7b / C2 L-SCOPE: `specs/Krate-manager-roadmap.md hunk @@ -933 +938 @@ ... is outside the FR-090 windows`, a `Phase 6✅` tick the build added to the dependency diagram. The main loop reverted that tick (Phase 5's box carries none either) and re-ran the whole lane.
- B2 `tauri build ... Krate Manager_0.1.0_x64-setup.exe (2.44 MiB)` exit=0 wall=98.4s; B3 `0 warning lines in the release build`; A10 L-WSL `WSL LANE GREEN (192 passed, 29 s)`, `Linux-run Phase 6 tests seen ok: 6/6` (early signal only, closes no pending-ci row).
- B7: the release binary is 7,650,304 bytes against the 4,314,624-byte Phase 5 base, +3,335,680 bytes (+77.3 %). No FR or SC bounds the size; recorded, not gated.
- SC-001 vitest: from a lowercase drive-letter cwd (`f:/...`) vitest reports `Vitest failed to find the current suite`; the close lane ran from `F:\projects\iterum` and A2d is green.
- T002's L-GUARD block (the clippy-guard fixture `Cargo.lock`) was fixed in the main loop, tasks.md "Defects found" D-6; close-lane A1d `rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0`.

## Compliance table

Legend: pass = verified locally this phase; pending-ci = needs the macOS or Linux leg.

| ID | Verdict | Evidence (verbatim where quoted) |
|---|---|---|
| FR-001 | pass | `crates/core/src/detect.rs:363-477` `candidates()` is pure. Windows :369-399, macOS :400-434, Linux :435-460, nested after the fixed table :462-475. `test detect::tests::candidate_table ... ok` (f:/tmp/km6-comply/cargo-test.txt). |
| FR-002 | pass | `platform/mod.rs:15` `pub const VENDOR_DIR: &str = "Krate Audio"`; `detect.rs:376` uses it; `grep -c "Krate Audio" detect.rs` gives 0. |
| FR-003 | pass | `detect.rs:492-506` `usable_install_location`; :509-511 `location_loggable`; :1029-1041 one warn `E_RECORD_LOCATION`. `record_parsers ... ok`. |
| FR-004 | pending-ci | `detect.rs:536-550` `stat()`. Windows covers NotFound and readable paths (`installed_state_table ... ok`). The non-NotFound metadata error is unix-only: `probe_metadata_error_unix` (detect_tests.rs:2287), proven by `required tests ran` on macos-latest and ubuntu-24.04. |
| FR-005 | pass | `roots.rs:175-185` `check_sandboxed`; `detect.rs:1043-1045` calls it. `test detect::tests::outside_install_location_panics - should panic ... ok`. |
| FR-006 | pending-ci | `detect.rs:237-248`, `list_root :284-332` (one `read_dir` at :297), constants :24-26 (4 and 20_000). Windows: `nested_copies_listed`, `listing_caps_honoured`, `listing_production_limits`, `listing_case_windows`, `listing_skips_junction_windows` all ok. Unix halves `listing_skips_symlinked_dirs_unix`, `listing_case_exact_unix` pending on `required tests ran`. |
| FR-010 | pass | `detect.rs:554-556`: `meta.is_file()` gives `Unknown(SingleFile)`, no log. `installed_state_table ... ok`. |
| FR-011 | pass | `detect.rs:561-581`, `read_bounded` :614-635, `MAX_MODULEINFO_BYTES` = 1 MiB (:18). `locked_moduleinfo_windows ... ok`. |
| FR-012 | pass | `detect.rs:639-688` `strip_trailing_commas` (SI-16 rule), `moduleinfo_version` :693-709. `trailing_comma_cases ... ok`. |
| FR-013 | pass | `detect.rs:711-781`, cases (a) to (d). `installed_state_table ... ok`. |
| FR-014 | pass | `detect.rs:113-121` `DetectedCopy`; :104-111 `CopyVersion`; :85-89 `VersionSource`; :91-102 nine `UnknownReason` variants. |
| FR-020 | pending-ci | `detect.rs:584-598`, :786-823 (`#[cfg(target_os="macos")]`). `au_plist_cases_macos` (detect_tests.rs:1587) pending on check (macos-latest). |
| FR-021 | pending-ci | `status.rs:331-333` filters format Vst3; plist dependency in the macOS table only (`crates/core/Cargo.toml:40`). macOS compile and `native_layout_macos` pending on check (macos-latest). |
| FR-025 | pass | `detect.rs:181-185` `HostFacts`; :124-141 `RecordRead`, `InnoValues`, `PkgOutputs`; :188-204 `PlatformFacts { pub io: &dyn HostIo }`. |
| FR-026 | pass | `detect.rs:826-838` `inno_record`; :995-997. `record_parsers ... ok`. |
| FR-027 | pass | `detect.rs:842-881` `parse_pkg_info`, `count_pkg_files`, `pkg_record`. `record_parsers ... ok`. |
| FR-028 | pass | `detect.rs:884-914` `read_manifest`; `read_bounded` refuses non-regular file (:622-625). `record_parsers` (detect_tests.rs:1268-1304) ok. |
| FR-029 | pass | `detect.rs:989-1021`, :1043-1045. `records_through_fake_host ... ok`. |
| FR-030 | pending-ci | `platform/mod.rs:86-95`; `windows.rs:62-77`; `macos.rs:64-81` (PKGUTIL_WAIT = 5 s); `linux.rs:62-75`. Windows `host_facts_through_fake_io_windows ... ok`. `_macos` and `_linux` pending on `required tests ran`. |
| FR-031 | pass | `platform/mod.rs:63-84`: `RunOutcome`, `RegistryRead`, `HostIo`; no decision logic. |
| FR-032 | pass | `src-tauri/src/os_facts.rs`: `OsHostIo` :8, `run` :11-23, `registry_values` :78-107 (`KEY_READ \| KEY_WOW64_64KEY`), `read_value` :111-155, Drop closes key :65-73, `rtl_version` :157-170. `grep RegSet\|RegCreate\|RegDelete` gives 0. `cargo clippy --workspace --all-targets -- -D warnings` EXIT 0. |
| FR-033 | pending-ci | Constants in windows.rs:10-14, macos.rs:12-17, linux.rs:13-15; re-exported `platform/mod.rs:100-113`; `injection.rs:46` `PLATFORM_LITERALS: [&str; 37]`. `core_sources_hold_no_production_literal ... ok` on Windows. macOS and Linux re-exports compile only on their legs. |
| FR-034 | pass | `detect.rs:917-956` mappers. `host_read_mappers ... ok`. |
| FR-035 | pass | `detect.rs:959-973` parsers. `os_version_parsers ... ok`. |
| FR-036 | pending-ci | Windows `host_facts_through_fake_io_windows` asserts 10.0.26200 and None ... ok. macOS and Linux versions pending on `host_facts_through_fake_io_macos` / `_linux`. |
| FR-037 | pass | `status.rs:178-197`, :293. `running_os_gates ... ok`. |
| FR-040 | pass | `status.rs:176-210` `build`. `feed_driven_detection`, `build_cases`, `listing_runs_once_per_root ... ok`. |
| FR-041 | pass | `status.rs:213-258` pure `assemble`. `golden_views ... ok`. |
| FR-042 | pass | `detect.rs:976-1067`, :1053, :1057-1060. |
| FR-043 | pass | `status.rs:331-364`. `installed_elsewhere`, `installed_state_table ... ok`. |
| FR-044 | pass | `status.rs:417`, :368-370. |
| FR-045 | pass | `status.rs:293-300`. |
| FR-046 | pass | `status.rs:23-172`, :392-422; `golden_views` compares 13 goldens byte for byte ... ok. |
| FR-047 | pass | `status.rs:204-208`; parse errors log line and column only (detect.rs:696-702). `build_cases ... ok`. |
| FR-048 | pass | `status::tests::concurrent_builds ... ok`. |
| FR-049 | pass | `status.rs:262-275`, :315. `release_page_url_cases ... ok`. |
| FR-050 | pass | `notes.rs:64` `Options::empty()`; pulldown-cmark 0.13.6, `default-features = false` (Cargo.toml:23; Cargo.lock:2738-2739). |
| FR-051 | pass | `notes.rs:17-49` `Block` and `Inline`. |
| FR-052 | pass | `notes.rs:315-361`. `blocks_cases ... ok`. |
| FR-053 | pass | `notes.rs:53-59`, MAX_NOTES_BYTES 65,536, MAX_DEPTH 8. `blocks_cases ... ok`. |
| FR-054 | pass | `notes.rs:60-62`; `status.rs:316-319`. |
| FR-060 | pass | `src-tauri/src/commands.rs:21-44`. |
| FR-061 | pass | `build.rs:3-9`, `capabilities/main.json:3-11`, `lib.rs:86-92`. `real_src_tauri_config_passes_the_guard ... ok`. |
| FR-062 | pass | `git status --porcelain` on tauri.conf.json, tauri.*.conf.json, index.html, both navigation.rs prints nothing. |
| FR-063 | pass | `config_guard.rs:42`, :44, plants :1535-1584. `planted_config_changes_fail_the_guard`, `real_src_tauri_config_passes_the_guard ... ok`. |
| FR-064 | pass | `commands.rs:46-52` `open_release_page(url: String)`; `lib.rs:91`; no `.plugin(opener)`; clippy EXIT 0. |
| FR-065 | pass | `Cargo.toml:35`, `src-tauri/Cargo.toml:27`, `Cargo.lock:3791-3792`. `opener_is_confined_and_not_granted_to_the_webview`, `opener_guard_reports_plants ... ok`. |
| FR-070 | pass | `main.ts:9-24`; `status.ts:320-331`; vitest pending-text and notice cases pass (30/30). |
| FR-071 | pass | `status.ts:9-243`; vitest 13 render cases plus `every status text is rendered by a scenario`. |
| FR-072 | pass | `status.ts:229-241`; `notes.ts:16-71`. `renders notes-rendering` passes. |
| FR-073 | pass | `status.ts:267-317`; vitest check-now cases pass. |
| FR-074 | pass | `status.ts:249-264`. `renders cached-stale`, `renders no-feed` pass. |
| FR-075 | pass | eslint `--max-warnings 0` EXIT 0; config_guard source scan ok; `no navigation path but the release page button` passes. |
| FR-076 | pass | `src/view.ts:5-94`, `toStatusView :299-313`; status.test.ts:8-20, :212, :261. |
| FR-077 | pass | styles.css diff adds only `.status`, `.plugin`, `.copies`, `.record-notice`, `.open-error/.open-release`, `details.notes` rules; no font-family, `@font-face`, `url()`, image. |
| FR-078 | pass | `status.ts:177-205`; four vitest cases pass (status.test.ts:358-362 source scan). |
| FR-080 | pending-ci | `crates/core/Cargo.toml:19`, :40; `injection.rs:101`, :2654; `manager-ci.yml:122-136`. Windows local run: 79 unique crates, 0 regex hits (f:/tmp/km6-comply/names.txt). macOS (plist table) and Linux pending on `::notice title=core dependency tree (macOS\|Linux)`. |
| FR-081 | pass | `Cargo.toml:42` windows-sys 0.61.2 with five features; `injection.rs:394-402`. `reader_dependencies_are_the_ruled_set ... ok`. |
| FR-082 | pass | grep confinement of pulldown_cmark, plist, Reg*, OsHostIo; `injection.rs:246`. `notes_and_plist_crates_are_confined`, `host_io_symbols_are_confined`, `confinement_scanner_reports_plants ... ok`. |
| FR-083 | pass | `test_support.rs` FakeHostFacts / FakeHostIo; `injection.rs:3369-3387`. `test_sources_never_name_os_host_io`, `test_source_scanner_reports_planted_os_host_io ... ok`. |
| FR-084 | pass | `read_dir` once (detect.rs:297 in `list_root`). `detect_status_notes_hold_no_write_or_listing_token`, `token_scanner_reports_planted_read_dir ... ok`. |
| FR-085 | pass | required-tests.json: cargo.all 165, windows 7, macos 24, linux 22, vitest 29. Every cargo.all and cargo.windows name has exactly one `... ok` line (0 misses). All 26 new vitest titles pass. |
| FR-086 | pass | `status_tests.rs:57` reads feeds in place; 13 goldens; no shipped plugin name (0 hits); `.gitattributes` sets `detect/**` and `status/**` to `-text`. |
| FR-087 | pass | `roster_and_hosts_absent_from_sources ... ok`; Windows binary check on target/debug/krate-manager.exe plus fresh vite dist: '9 names, 0 hits in 4 files'. |
| FR-088 | pass | `injection.rs:198-219`, :2363-2374. `test_sources_hold_no_real_location`, `real_location_scanner_reports_plants ... ok`. |
| FR-089 | pass | `roster.rs:270-295`. `install_identifiers_absent_from_sources`, `install_identifier_scanner_reports_plants ... ok`. |
| FR-090 | pass | `git diff --stat 3efe70de -- . ':!apps/krate-manager'` touches 4 tracked files (manager-ci.yml +16, specs/INDEX.md, roadmap +3, manager-ci-shape.test.js +85); required-tests.json diff 79 added, none removed. Shape test: `ℹ tests 26 ℹ pass 26 ℹ fail 0`. `scope-check: 82 paths (70 under apps/krate-manager/, 12 outside), 1 roadmap hunks: OK (base 3efe70de)`; after the status line, close-lane A7b / C2: `scope-check: 95 paths (70 under apps/krate-manager/, 25 outside), 2 roadmap hunks: OK (base 3efe70de)`. |
| FR-091 | pass | `node tools/gen-specs-index.js --check` prints 'specs/INDEX.md is up to date.' |
| FR-092 | pass | `specs/Krate-manager-roadmap.md:720-722` adds the sentence 'A managed copy whose recorded `InstallLocation` lies outside the compiled-in Windows plug-in root is never uninstalled by the helper: ...'. |
| SC-001 | pass | Windows lane: `== fmt: exit=0 warning-lines=0`; `== clippy1: exit=0`; `== clippy2: exit=0`; `== test: exit=0 warning-lines=0 8.9s` (core lib `144 passed; 0 failed`, config_guard `7 passed`, injection `18 passed`, roster `4 passed`); `== tauri-build: exit=0 129.4s` (`Finished 1 bundle ... Krate Manager_0.1.0_x64-setup.exe (2.44 MiB)`); typecheck, lint, version-lint, shape-test (26/26) exit 0. Vitest `npm test` from F: gave `Test Files 2 passed (2) Tests 30 passed (30)`. 0 suppressions. macOS and Linux legs PENDING-CI (tasks.md:929-930). Main-loop close lane (evidence/close-lane.log (2026-10-31T18:07:45Z, wall=117.5s, 38 GREEN steps, last line `CLOSE LANE GREEN`)): A1a-A1e fmt / clippy / clippy dev-feed / clippy-guard / `cargo test binaries=10 passed=178 binaries-with-failures=0`; A2b-A2d typecheck, lint, vitest exit 0; B2 `Krate Manager_0.1.0_x64-setup.exe (2.44 MiB)` exit=0; B3 `0 warning lines in the release build`; B4 `dev-feed absent ... in target/release/krate-manager.exe=0`; B6 `artifact version (Windows)::0.1.0 (nsis=present)`. |
| SC-002 | pass | `test detect::tests::candidate_table ... ok` (detect_tests.rs:327-593). |
| SC-003 | pending-ci | Windows `native_layout_windows ... ok`. `native_layout_macos` (:619) and `native_layout_linux` (:641) are cfg-gated. |
| SC-004 | pass | `test detect::tests::trailing_comma_cases ... ok` (:698-749). |
| SC-005 | pass | `test detect::tests::installed_state_table ... ok` (:772-995), 20 rows. |
| SC-006 | pass | `test detect::tests::duplicates_in_two_roots ... ok` (:1000-1113). |
| SC-007 | pass | `test detect::tests::record_parsers ... ok` (:1118-1310). |
| SC-008 | pass | `records_through_fake_host ... ok`; `outside_install_location_panics - should panic ... ok`. |
| SC-009 | pass | `test status::tests::feed_driven_detection ... ok`. Linux Available branch re-runs on ubuntu via cargo.all. |
| SC-010 | pass | `os_version_parsers ... ok`; `running_os_gates ... ok`. |
| SC-011 | pass | `test notes::tests::blocks_cases ... ok` (notes_tests.rs:96-241). |
| SC-012 | pass | `test status::tests::golden_views ... ok`; 13 files. |
| SC-013 | pending-ci | `au_plist_cases_macos` (detect_tests.rs:1585-1709), cfg macos. |
| SC-014 | pending-ci | `pkg_receipt_macos` (:1711-1796), cfg macos. |
| SC-015 | pending-ci | `linux_detection_linux` (:1800-1885), cfg linux. |
| SC-016 | pass | `test detect::tests::locked_moduleinfo_windows ... ok`. |
| SC-017 | pass | `detection_leaves_the_tree_unchanged ... ok`; `detect_status_notes_hold_no_write_or_listing_token ... ok`. |
| SC-018 | pass | `build_cases ... ok`; `concurrent_builds ... ok`. |
| SC-019 | pass | vitest verbose: 13 `renders <golden>` cases, `does not call status commands when another instance is running`, `every status text is rendered by a scenario` (Tests 30 passed (30)). |
| SC-020 | pass | vitest: four check-now cases pass (main.test.ts:127-194). |
| SC-021 | pass | `real_src_tauri_config_passes_the_guard ... ok`; `planted_config_changes_fail_the_guard ... ok`; CSP unchanged since 3efe70de. |
| SC-022 | pending-ci | Windows guards all ok; local `cargo tree` n=79, regex hits 0. Pending: `::notice title=core dependency tree (<os>)::<n> crates, none excluded` on macOS and Linux. |
| SC-023 | pending-ci | Local replica: `::notice title=required tests ran (Windows)::172 cargo + 29 vitest`, exit 0. Pending: macOS `189 cargo + 29 vitest`, Linux `187 cargo + 29 vitest`. |
| SC-024 | pass (Windows); pending-ci (macOS, Ubuntu) | Windows `test_sources_hold_no_real_location`, `roster_and_hosts_absent_from_sources ... ok`. Close-lane B5 over this phase's release build: `::notice title=roster absent (Windows)::9 names, 0 hits in 4 files` (positive control found), saved to evidence/roster-absent.txt. macOS and Ubuntu legs pending-ci. |
| SC-025 | pending-ci | Windows `host_facts_through_fake_io_windows ... ok`; macOS (:2071) and Linux (:2161) cfg-gated. |
| SC-026 | pass | `host_read_mappers ... ok`. |
| SC-027 | pending-ci | `probe_metadata_error_unix` (:2285-2348), cfg unix; no Windows test by design. |
| SC-028 | pass | Needles: `"in the plugin's install dir under the compiled-in Windows plug-in root" count 1 want 1`; `"is never uninstalled by the helper" count 1 want 1`. One roadmap hunk `@@ -719,0 +720,3 @@`. The Phase 6 status-line hunk is still the main loop's to add. |
| SC-029 | pending-ci | Windows `nested_copies_listed`, `listing_case_windows`, `listing_skips_junction_windows ... ok`. Unix `listing_case_exact_unix` (:2505), `listing_skips_symlinked_dirs_unix` (:2522) pending. |
| SC-030 | pass | `listing_caps_honoured`, `listing_production_limits`, `listing_runs_once_per_root ... ok`. |
| SC-031 | pass | `installed_elsewhere ... ok`. |
| SC-032 | pass | `release_page_url_cases ... ok`. |
| SC-033 | pass | `opener_is_confined_and_not_granted_to_the_webview`, `opener_guard_reports_plants ... ok`; `cargo tree ... grep -i opener` gives `tauri-plugin-opener v2.7.2` for the app, 0 lines for core. |
| SC-034 | pass | vitest: `renders the open release page button`, `open release page calls open_release_page once`, `open release page shows a rejection`, `no navigation path but the release page button`. |
| SC-035 | pass | vitest: `shows the pending text until the first result`, `check now stays disabled until the first result`, `check now is enabled after a rejected first load`. |
| CC-system | pass | Real registry read only in `os_facts.rs:84-90` (read-only), built only at `commands.rs:40`. Tests use `FakeHostIo`. Guards: `injection: 18 passed`, `roster: 4 passed`, `config_guard: 7 passed`. Caveat: the FR-088 scan covers 6 files; `platform/*_tests.rs` were cleared by manual read. |
| CC-verify | pass | Not applicable: no artifact download path in Phase 6 (Phase 7). Only `http::get` is the feed fetch at `feed.rs:757`. |
| CC-zipslip | pass | Not applicable: no extraction code (Phase 8). Junction/symlink non-following in listing tested (detect_tests.rs:2575-2610). Check at Phase 8. |
| CC-helper | pass | `crates/helper/src/main.rs` exits 2 with 'no operations in this build'; helper diff empty; nothing invokes it. |
| CC-capabilities | pass | `capabilities/main.json:5-11` exactly five permissions; opener used as a free fn only (`commands.rs:51`), not registered; URL validated in core first. |
| CC-signing | pass | `tauri.conf.json:22` `signingIdentity "-"` (ad-hoc); no certificate in tauri*.json or manager-ci.yml; the diff adds no signing step. |
| CC-warnings | pass | `cargo clippy --workspace --all-targets -- -D warnings` EXIT 0 (note: cached fingerprint replay); `cargo fmt --all --check` exit 0; typecheck exit 0; `eslint --max-warnings 0` exit 0; no suppressions in production code. |
| CC-lints | pass | `lint-serial-test-runs: clean (64 files)`. No C++ or CMake changed, so check-portability is not required. |
| CC-feed-compat | pass | Feed unchanged; only `manager-ci-shape.test.js` (+85) matched the feed grep; `update_checker.cpp:215-239` still reads v1 keys. |

## PENDING-CI list

The user pushes; nothing here was pushed. Each item closes on a named CI step.

| Item | CI job and assertion |
|---|---|
| FR-004, SC-027 | `required tests ran` on check (macos-latest) and check (ubuntu-24.04): `detect::tests::probe_metadata_error_unix` |
| FR-006, SC-029 | same two legs: `listing_case_exact_unix`, `listing_skips_symlinked_dirs_unix` |
| FR-020, SC-013 | check (macos-latest): `au_plist_cases_macos` |
| FR-021, SC-003 | check (macos-latest): macOS compile and `native_layout_macos`; check (ubuntu-24.04): `native_layout_linux` |
| SC-014 | check (macos-latest): `pkg_receipt_macos` |
| SC-015 | check (ubuntu-24.04): `linux_detection_linux` |
| FR-030, FR-036, SC-025 | `host_facts_through_fake_io_macos` / `_linux` in `required tests ran` |
| FR-033 | macOS and Linux compile of the platform constant re-exports (build and clippy steps) |
| FR-080, SC-022 | `::notice title=core dependency tree (macOS)` and `(Linux)`, `<n> crates, none excluded`, no `::error` |
| SC-023 | `required tests ran`: macOS `::notice ...::189 cargo + 29 vitest`; Linux `::notice ...::187 cargo + 29 vitest`; no `::error title=required tests ran` |
| SC-024 | `roster absent` step green on each check leg, plus cargo.all re-run |
| SC-001 (partial leg) | clippy, cargo test, tauri build and build warnings on macOS and Linux (tasks.md:929-930) |

## Implementation notes (deviations reported by task agents)

- T001: Rule 12's line-ending table was wrong. spec.md was LF at start and was converted to CRLF (1501/1501). specs/INDEX.md is LF. `grep -c $'\r$'` is unreliable in this Git Bash; count CR with node.
- T002: Blocked on the clippy-guard fixture `Cargo.lock` needing the 40 new packages (`cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 --offline`, as in Phase 5). The file is now modified in the tree. Lockfile change is additive (454 to 494 pairs, 0 removed). GETCONF is `/usr/bin/getconf`. Working-tree line endings are mixed; each edit kept its file's own ending.
- T003: `PathBuf::push` folds `..` on a `\\?\` path, so a test builds that text with `format!`. The FIFO probe logs on a helper thread, so the test drains the logger there. WSL run passed 20 of 20 detect tests.
- T004: SC-011's 9-deep emphasis wording ("alternating `*` and `_` around one word") cannot nest nine deep in CommonMark. The test uses `*a _b *c _d *e _f *g _h *x*_*_*_*_*` instead (eight Emphasis levels). SC-011 wording may want aligning.
- T005: Test-first was not followed for status.rs (written before status_tests.rs). Goldens were harvested from `golden_views` failure output after T004 passed. Three scenario inputs were adjusted (Juliet install None, notes-rendering Markdown, Charlie notes URL).
- T006: macOS and Linux `cfg(not(windows))` arms first compile on CI.
- T007: Both red tests were the expected T003 red. Host literals match by value.
- T008: Source rules scan all `.rs` under src-tauri/src, crates/core/src and crates/helper/src, wider than the task wording, to meet FR-065 "no source".
- T009: tasks.md cites `platform/mod.rs:9` for the APP_IDENTIFIER line; it is at :13. The exemption matches by prefix.
- T010: Phase 5's SC-021 parallel-flag grep (A9) not carried over. A1f was red until T012, A11 red until T013's needles file. C3 runs `clean-test-output.js --apply` last.
- T011: Plan §1.1 claim that tsconfig.json has no `types` key is wrong under TypeScript 6.0.3; status.test.ts carries `/// <reference types="node" />`. FR-071 has no state-line cell for an `available` plugin with `update` null (no `p.state` rendered).
- T012: FR-090 as written allowed only three lists in the shape test, but `checkRequired` deep-compares `EXPECTED_REQUIRED`. The same 35/5/7/6/26 additions were mirrored into it. Spec revision 4 amended FR-090 to allow this. If ruled the other way, FR-085's additions must be dropped.
- T013: Ran `close-lane.js` by accident via `require` (no main guard); cut off after about 1 s, its `sc028.txt` and log dir were deleted. Consider adding a main guard to `close-lane.js`.

## Remaining steps for the human loop

1. Review the working tree and `specs/krate-manager-phase6/` (spec, plan, tasks, this file).
2. Done 2026-10-31: `node specs/krate-manager-phase6/evidence/close-lane.js`, `CLOSE LANE GREEN` (evidence/close-lane.log); the roadmap status line is written and the diff shows exactly two hunks.
3. Commit (one new commit, no amend).
4. The user pushes; CI then proves the pending-ci rows above on check (macos-latest) and check (ubuntu-24.04).
