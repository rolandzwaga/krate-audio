# Krate Manager Phase 5 (krate-manager-phase5) compliance

Overall status: COMPLETE_PENDING_CI (110 pass, 20 pending-ci, 0 fail; main-loop close lane 2026-10-31)

## Honest gap list (first)

The four gaps the comply stage listed were closed by the main-loop close lane, close-lane.log (F:/tmp/gate/krate-manager-phase5-close/20261031-141516, last line `CLOSE LANE GREEN`):

1. SC-001: B2 `step tauri build last="...\bundle\nsis\Krate Manager_0.1.0_x64-setup.exe (1.32 MiB)" exit=0 wall=318.0s`; B3 `::notice title=build warnings (Windows)::0 warning lines in the release build`. Now pass.
2. SC-016 (Windows half): B5 `::notice title=roster absent (Windows)::9 names, 0 hits in 4 files` over this phase's release build, saved to evidence/roster-absent.txt. Windows half now pass; macOS and Ubuntu legs stay pending-ci.
3. T018 / L-GUARD: the fixture lock was regenerated in the main loop (tasks.md "Defects found", G4 / T018 row); A1e `::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0`. Closed.
4. L-WSL: A11 `WSL LANE GREEN (150 passed, 148 s)`, `cfg(not(windows)) tests seen ok: 13/13` (evidence/wsl-core.txt). An early signal only: it closes no pending-ci row.

Remaining: the 20 pending-ci rows below (macOS / Linux behaviour), proven only by the manager-ci legs.

## Compliance table

Evidence is condensed from the per-item verification. File:line citations and test outputs are kept as reported. "Local" means the Windows host, `cargo test --workspace --locked`, exit 0 (core lib `116 passed; 0 failed`).

| ID | Verdict | Evidence |
|---|---|---|
| FR-001 | pass | contract.rs:11-14 include_str! of versions.v2/v1 schema in place; :149-150 OnceLock; :497-566 `_ => return Err(refuse())` gives ContractError::Unsupported; :578-585 `$ref` only `#/$defs/<name>`; :612 regex compile failure is Unsupported. `embedded_schemas_compile ... ok`, `unsupported_schema_constructs_are_refused ... ok`. |
| FR-002 | pass | contract.rs:399-401 integer rule; :800-821 json_equal by value; :666-667 unanchored `is_match`; :312-363 ecma_pattern_to_rust (`\s` ECMA set, `.` excludes \n \r U+2028 U+2029, `\S \d \D \w \W \b \B` refused); :768-795 additionalProperties/propertyNames; :707-723 if/then/else. `ecma_pattern_translation`, `keyword_semantics_cases` ok. |
| FR-003 | pass | contract.rs:823-832 Finding{code "E_SCHEMA", instance_path, keyword}; :298-304 pointer escapes; :628-632 "false schema"; :714-722 `if`; :787-793 `propertyNames`; allErrors. `c1_cases_reproduce ... ok`. |
| FR-004 | pass | contract.rs:836-866 feed_code_findings over entry and prerelease sibling; :868-896 signature checks; :850-861 install field checks; :900-960 duplicates (bytewise sort :902, ASCII fold except windows_uninstall_key :928-932). `c1_cases_reproduce` passed. |
| FR-005 | pass | contract.rs:167-219 parse_minisig_text (4 lines, prefixes, canonical base64, 74-byte line 2 starting 0x45 0x44, 64-byte line 4). `minisig_text_cases ... ok`. |
| FR-006 | pass | contract.rs:223-263 trusted comment E_TC_FIELDS/NAME/VERSION/PLATFORM/ASSET. `trusted_comment_cases ... ok`. |
| FR-007 | pass | contract.rs:148-164 `validate`, code findings only for Schema::V2 (:160-162). `gen_feed_outputs_accepted ... ok`. |
| FR-008 | pass | contract.rs:56-68, :105-106 code strings; feed.rs:645-646, :665 E_INPUT. |
| FR-020 | pass | feed.rs:275-294 parse (non-UTF-8 and JSON failure E_INPUT); :298-320 classify_schema_version; :290-293 verdict. `schema_version_dispatch ... ok`. |
| FR-021 | pass | feed.rs:37-133 model types; v1 release None (:426), v2 Some (:476). `model_of_full_v2 ... ok`. |
| FR-022 | pass | feed.rs:432-485 reads named keys only; :451-453 skips unknown platform keys. `unknown_keys_do_not_change_the_model ... ok`. |
| FR-023 | pass | version.rs:217-219 NotThroughManager on empty platforms; feed.rs:364-372. `resolve_table ... ok`. |
| FR-024 | pass | feed.rs:283-289 sibling findings partitioned, one warn each (target krate_manager_core::feed); :324-339 sibling path rule; v2_entry never reads `prerelease`. `broken_sibling_never_blocks_stable`, `sibling_prefix_filter`, `prerelease_sibling_is_ignored` ok. |
| FR-025 | pass | feed.rs:205-213 AssetName::from_url; :497-499 refusal drops asset with MODEL_ASSET_NAME. `asset_name_cases ... ok`. |
| FR-026 | pass | feed.rs:141,161,179,197 private-field newtypes, pub(crate) constructors; four compile_fail doctests ok; sha256 decoded to [u8;32] (:569-586). `feed_model_seams_hold ... ok`. |
| FR-027 | pass | feed.rs:399-428 v1_entry, E_MODEL_V1_ENTRY; BTreeMap order. `roster_and_hosts_absent_from_sources`, `v1_entries_are_sanitized`, `roster_follows_the_feed` ok. |
| FR-028 | pass | feed.rs:373-382 manager latest_json_url into ManagerInfo; never fetched. |
| FR-029 | pass | feed.rs:342-354, :497-527 MODEL_RANGE / MODEL_ASSET_NAME / MODEL_V1_ENTRY as one warn, never a Finding. `model_range_cases ... ok`; evidence/model-range-node.txt Node findings=0. |
| FR-030 | pass | version.rs:18-53 core_part, three parts, Display, derived Ord. `parse_core_cases ... ok`. |
| FR-031 | pass | version.rs:56-65 reduce_class_version. `reduce_class_version_cases ... ok`. |
| FR-032 | pass | version.rs:84-94 compare. `compare_cases ... ok`. |
| FR-033 | pass | version.rs:97-114 Arch, native() via cfg!. `native_candidates_windows ... ok`; macos/linux in required-tests.json. |
| FR-034 | pass | version.rs:200-208 platform_candidates. `resolve_table ... ok`. |
| FR-035 | pass | version.rs:211-230 resolve. `needs_newer_os_table ... ok`. |
| FR-036 | pass | version.rs:233-240 resolve_native, only native call site. |
| FR-037 | pass | version.rs:125-183 OsVersion parse/Ord/Display; :226-228. `os_version_cases ... ok`. |
| FR-038 | pass | version.rs:193-196 NeedsNewerOs separate from NotForThisMachine; resolve is pure. |
| FR-040 | pass | Cargo.toml:33 ureq 3.5.0 [platform-verifier, socks-proxy]; :23 rustls 0.23.46; `cargo tree --locked` on three targets: ureq v3.5.0, one rustls v0.23.46, rustls-platform-verifier v0.7.1; plan.md:211, :239 updater record. |
| FR-041 | pass | http.rs:27-82 UrlPolicy, HttpConfig, production(), loopback(), builders. `production_values ... ok`. |
| FR-042 | pass | http.rs:254-322 check_url/check_proxy; get checks before any socket (:946-951). `url_policy_cases`, `loopback_proxy_only`, `https_only_refuses_before_connecting` ok. |
| FR-043 | pass | http.rs:630-650 agent_config; :355-382 next_hop; :1012-1023 redirects/status; :1074-1089 read_body. `next_hop_cases`, `redirects_are_rechecked`, `status_and_body_errors`, `agent_trusts_the_os_store` ok. |
| FR-044 | pass | http.rs:665-681 map_error; :659-662 rustls downcast; :1084. `status_and_body_errors`, `no_response_class_cases` ok. |
| FR-045 | pass | lib.rs:6-7 fixture_server cfg(any(test, feature="test-support")); injection.rs:124-149 CONFINED_TOKENS. `ureq_is_confined_to_http_rs ... ok`. |
| FR-046 | pass | http.rs:109-128, :196-206, :614-619, :848-855, :495-502. `proxy_path_connect`, `proxy_path_http`, `no_proxy_bypass`, `proxy_scheme_cases`, `loopback_proxy_only` ok. |
| FR-048 | pass | http.rs:527-594 resolve_proxy; :177-193 ProxyEnv::from_vars; env names only in src-tauri/src/proxy_env.rs:8-21. `resolve_proxy_cases`, `proxy_env_from_vars_cases` ok. |
| FR-049 | pending-ci | Parsers pass on every host (sysproxy.rs:58-454 and local `windows/macos/linux_proxy_parser_cases`, `win_ie_strings_are_read_and_freed_once`, `linux_desktop_choice_cases`, `kioslaverc_path_cases`, `kde_file_read_cases` ok). macOS and Linux real readers compile and run only on CI. |
| FR-064 | pending-ci | pac.rs:70-132, :771, :1031-1175, :535-542, :594-604. Local Windows: `pac_helper_cases`, `pac_clock_cases`, `fake_pac_host_answers_from_its_table` ok. Engine half (cfg(not(windows))) proven only on macOS/Linux legs. |
| FR-065 | pending-ci | http.rs:325-343, :816-842, :386-394, :777. `pac_policy_cases ... ok` locally. Fetch path cfg(not(windows)). |
| FR-066 | pending-ci | os_proxy.rs:387-393; pac.rs:39 WPAD_DNS_URL, :173-175; http.rs:779-810. `wpad_candidate_cases ... ok` locally; walk needs CI. |
| FR-067 | pending-ci | pac.rs:179-249, http.rs:891-933, :1030-1071. Local `find_proxy_result_cases`, `winhttp_answer_cases`, `os_pac_walk`, `no_response_class_cases` ok; engine-path walk needs CI. |
| FR-068 | pending-ci | Windows double warn fixed: os_proxy.rs:283-293 `os_error` logs nothing; only `log::warn!` left in `win_find_proxy` is :402 (context leak). Core logs once at http.rs:723-734 (called :928). Local `os_pac_failure_cases ... ok`, `os_pac_walk ... ok`; http_tests.rs:1713-1714 asserts `pac_lines.len() == 1`. `pac::tests::script_failure_cases` is cfg(not(windows)). |
| FR-069 | pending-ci | http.rs:540-554, :779-785, :913-926, :536-539; os_proxy.rs:387-393. Local `resolve_proxy_cases`, `os_pac_failure_cases` ok; engine fallback needs CI. |
| FR-092 | pass | os_proxy.rs:319-526 full WinHTTP sequence per PD-35; http.rs:858-888 os_find bounded; windows-sys features pinned (Cargo.toml:36). `cargo clippy --workspace --all-targets --locked -- -D warnings` gave `clippy exit 0`, 0 `^warning` lines. `winhttp_answer_cases`, `os_pac_walk`, `os_pac_failure_cases` ok. Real OS call not run by any test (FR-087). |
| FR-093 | pass | specs/Krate-manager-roadmap.md:902 RustSec line (also evidence/rustsec-line.txt). |
| FR-094 | pending-ci | pac.rs:852-897 DnsMemo; http.rs:1093-1137 std_resolve bounded. cfg(not(windows)); `pac::tests::dns_bound_cases` on CI. |
| FR-095 | pending-ci | Double warn fixed: os_proxy.rs:210-226 readers log nothing; `pac::clock_civil` pac.rs:375-392 logs once per session (OnceCell, Rc<Cell<bool>>). Local `pac_clock_cases`, `parse_utc_offset_cases`, `offset_range_cases` ok. Real CFTimeZone and `date +%z` readers compile only on macOS/Linux CI. |
| FR-050 | pass | feed.rs:741-798 check; MAX_FEED_BYTES 4 MiB. `check_fresh_writes_cache`, `offline_returns_cached_flagged_stale` ok. |
| FR-051 | pass | feed.rs:753-756 CheckNow no-cache headers; :22 EDGE_MAX_AGE_SECS 600. `check_now_sends_no_cache ... ok`. |
| FR-052 | pass | feed.rs:18, :801-847 atomic cache write. `cache_write_fault_keeps_previous ... ok`. |
| FR-053 | pass | feed.rs:852-894 read_cache/decode_cache. `cache_rules ... ok`. |
| FR-054 | pass | feed.rs:757-782 cache written only in Ok arm. `rejected_feed_keeps_last_good_copy ... ok`. |
| FR-055 | pass | feed.rs:782-796. `offline_returns_cached_flagged_stale ... ok`. |
| FR-056 | pass | feed.rs:766-790 one info/warn line per check. `check_logs_one_line ... ok`. (Windows double PAC warn recorded under FR-068/FR-092, fixed.) |
| FR-057 | pass | feed.rs:731-733, :741-798, no lock. `concurrent_checks_leave_one_whole_file ... ok`. |
| FR-058 | pass | feed.rs:760-781. `fresh_lower_version_replaces_cache ... ok`. |
| FR-059 | pass | feed.rs:757; http.rs:986-1071, :954-957. `one_attempt_per_check`, `chain_shares_one_deadline` ok. |
| FR-060 | pass | context.rs:55, :69-74, :82-106, :165-166. `http_config_is_carried ... ok`. |
| FR-061 | pass | src-tauri/src/lib.rs:111-123; dev_feed.rs:177-183; http.rs:597-611. `config_for_dev_url_cases`, `real_src_tauri_config_passes_the_guard` ok. |
| FR-062 | pass | git diff touches only Cargo.toml, dev_feed.rs, lib.rs plus new os_proxy.rs, proxy_env.rs under src-tauri; invoke_handler still app_version and startup_error (lib.rs:154-157). |
| FR-063 | pass | config_guard.rs:43-61, :913-915, :1332-1392; injection.rs:73-81 D6_TOTAL=8; proxy_env.rs:38-42. `planted_config_changes_fail_the_guard`, `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots`, `disallowed_methods_expectations_are_the_seven_d6_sites` ok. |
| FR-070 | pass | fixture_server.rs:115-122 binds, :16-50 Reply variants, :363, :372-374, :323, :76/:170. `replies_behave_as_scripted`, `binds_loopback_port_zero`, `connect_to_remote_host_is_refused` ok. |
| FR-071 | pass | contract_tests.rs:11, feed_tests.rs:52 read fixtures in place; 116 core tests in 2.43 s, default threading. |
| FR-072 | pass | live_feed.rs:5 `#![cfg(feature = "live-network")]`, :44 `#[ignore = ...]`. `manager-ci.yml does not name live-network (FR-084)` ok. |
| FR-073 | pending-ci | Manifests and `cargo tree` match (ureq features, windows-sys [Win32_Foundation, Win32_Networking_WinHttp], system-configuration 0.8.0 macOS only, rquickjs under cfg(not(windows)), one rustls 0.23.46, one core-foundation 0.10.1). Windows release build 4,316,160 bytes vs base 4,269,056 (evidence/base-binary-size.txt). Builds on macOS and Linux with no system library needs CI. |
| FR-074 | pending-ci | sysproxy.rs:457-459, :192-256, :535-549; os_proxy.rs:62-73, :143-151, :218-235. Local `linux_desktop_choice_cases`, `kioslaverc_path_cases`, `kde_file_read_cases`, `parse_utc_offset_cases` ok. Linux leg for `bounded_run_abandons_and_reaps`. |
| FR-075 | pending-ci | core Cargo.toml:36-37; lib.rs:17-18; sysproxy_cf.rs:16-58. Local `offset_range_cases`, `core_dependencies_are_the_fr007_set` ok; `from_cf_proxies_cases` on macOS CI. |
| FR-080 | pass | injection.rs:87-98 ten dependencies, :2222-2226 dev-deps {flate2}; `cargo tree` shows no tauri*, tokio, reqwest, hyper, dirs*, directories*, home. `core_dependencies_are_the_fr007_set ... ok`. |
| FR-081 | pass | injection.rs:124-163 confinement tables, plants :2342-2428. `ureq_is_confined_to_http_rs ... ok`. |
| FR-082 | pass | roster.rs:22-189. `roster_and_hosts_absent_from_sources`, `roster_scanner_reports_planted_names` ok. |
| FR-083 | pending-ci | manager-ci.yml:196-218 `roster absent` step. Windows: close-lane B5 over this phase's release build, `9 names, 0 hits in 4 files` (evidence/roster-absent.txt). macOS/Linux binaries on CI. |
| FR-084 | pass | manager-ci.yml:11, :17 `tools/feed-contract/**`; shape test `tests 26, pass 26, fail 0`; feed-tools.yml PLAN `1..59` unchanged. |
| FR-085 | pass | required-tests.json cargo.all 130 names; windows 2, macos 17, linux 16; cross-check against the local log found every name passing. |
| FR-086 | pass | No --test-threads / --no-file-parallelism; port 0 only (fixture_server.rs:116,122); no static Mutex<()>. |
| FR-087 | pass | injection.rs:179-231 body rules, plants :2569-2609. `production_config_only_in_allowed_tests ... ok`. |
| FR-088 | pass | test_support.rs:137-191; set_logger only at test_support.rs:173. `startup_errors_are_logged`, `check_logs_one_line` ok. |
| FR-090 | pass | git status outside apps/: manager-ci.yml, manager-ci-shape.test.js, specs/INDEX.md, roadmap, results.md, specs/krate-manager-phase5/. `scope-check: 16 paths, 3 roadmap hunks, specs/krate-manager/probes/results.md 1 0: OK (base a145271b)`. |
| FR-091 | pass | `node tools/gen-specs-index.js --check` gives `specs/INDEX.md is up to date.` |
| SC-001 | pass | Passed this session: `fmt exit 0`; `clippy exit 0` (workspace, live-network, dev-feed variants; 0 warnings); `test exit 0` (core lib `116 passed; 0 failed`, config_guard 5, injection 9, live_feed 0, roster 2, helper 1, doc-tests 4); `npm ci`, `tsc exit 0`, `lint exit 0`, vitest `Tests 4 passed (4)`; `lint-manager-version: ok, version 0.1.0 everywhere`; todo!/unimplemented! grep 0 lines; evidence/clippy-guard.txt holds 3 `disallowed method` errors. Release build (main-loop close lane, close-lane.log (F:/tmp/gate/krate-manager-phase5-close/20261031-141516, last line `CLOSE LANE GREEN`)): B2 `step tauri build ... Krate Manager_0.1.0_x64-setup.exe (1.32 MiB)" exit=0 wall=318.0s`; B3 `0 warning lines in the release build`; B4 `dev-feed absent ... in target/release/krate-manager.exe=0`; B6 `artifact version (Windows)::0.1.0 (nsis=present)`; B7 release binary 4314624 bytes (+45568, 1.1 % over the Phase 4 base). |
| SC-002 | pass | `c1_cases_reproduce: 90 rows ... ok`; `accept_rows_parse_through_the_client: 11 accept rows ... ok`. CI re-run: `required tests ran`. |
| SC-003 | pass | `gen_feed_outputs_accepted: 57 expected.v2.json, 16 expected.legacy.json ... ok`; `gen_feed_outputs_parse` same counts. |
| SC-004 | pass | feed_tests.rs:419 `schema_version_dispatch ... ok`. |
| SC-005 | pass | feed_tests.rs:526 `model_of_full_v2`, :614, :619 all ok. |
| SC-006 | pass | version_tests.rs:247 `resolve_table ... ok`. |
| SC-007 | pending-ci | Windows `native_candidates_windows ... ok`. macOS/Linux tests not compiled here; CI `check (macos-latest)` / `check (ubuntu-24.04)` step `required tests ran`. x86_64 macOS branch unproven until an Intel host runs it. |
| SC-008 | pass | `parse_core_cases`, `reduce_class_version_cases`, `compare_cases` ok. |
| SC-009 | pass | `check_fresh_writes_cache`, `check_now_sends_no_cache` ok. |
| SC-010 | pass | `rejected_feed_keeps_last_good_copy: 71 rejection rows ... ok`; `sibling_prefix_filter`, `broken_sibling_never_blocks_stable` ok. Minor gap: the warns() helper filters by level, not target. |
| SC-011 | pass | `offline_returns_cached_flagged_stale: 2.1387104s` (threshold < 5 s) ok. |
| SC-012 | pass | url_policy_cases, next_hop_cases, redirects_are_rechecked, status_and_body_errors (`HeadersThenStall elapsed 1.0069918s`), https_only_refuses_before_connecting all ok. |
| SC-013 | pass | `cache_rules`, `cache_write_fault_keeps_previous` ok (Windows runs 5 steps; SyncDir is cfg(unix)). |
| SC-014 | pass | `roster_follows_the_feed ... ok`. |
| SC-015 | pass | `9 plugin names read, 38 files scanned, 0 hits`; `roster_and_hosts_absent_from_sources`, `roster_scanner_reports_planted_names`, `core_sources_hold_no_production_literal` ok. |
| SC-016 | pass (Windows); pending-ci (macOS, Ubuntu) | CI step exists (manager-ci.yml:196-217). Windows: close-lane B5 over this phase's release build: `::notice title=roster absent (Windows)::9 names, 0 hits in 4 files` (positive control found), saved to evidence/roster-absent.txt (close-lane.log (F:/tmp/gate/krate-manager-phase5-close/20261031-141516, last line `CLOSE LANE GREEN`)). macOS and Ubuntu legs are pending-ci. |
| SC-017 | pass | `core_dependencies_are_the_fr007_set`, `ureq_is_confined_to_http_rs` (16 plants) ok; `cargo tree`: `ureq v3.5.0`, `rustls v0.23.46`, `rustls-platform-verifier v0.7.1`, webpki-roots v1.0.11; plan.md:239-249 `Verdict: agree`. |
| SC-018 | pass | `http_config_is_carried`, `planted_config_changes_fail_the_guard`, `real_src_tauri_config_passes_the_guard`, `config_for_dev_url_cases` ok. |
| SC-019 | pass | `-- --list`: 136 `: test` lines, 0 naming live_feed_parses; with `--features live-network --test live_feed -- --list --ignored`: `live_feed_parses: test` / `1 test, 0 benchmarks`. Test itself not run. |
| SC-020 | pass | `trusted_comment_cases`, `minisig_text_cases` ok. Minor: KAT comment checked by parsing, not text equality. |
| SC-021 | pass | No --test-threads; `binds_loopback_port_zero`, `replies_behave_as_scripted`, `connect_to_remote_host_is_refused`, `production_config_only_in_allowed_tests` (23 plants) ok. |
| SC-022 | pass | `check_logs_one_line`, `startup_errors_are_logged` ok. |
| SC-023 | pass | `1..59`, `# tests 752`, `# pass 747`, `# fail 0`, `# skipped 5`; `TAP plan=1..59 top-level=59`. |
| SC-024 | pass | `concurrent_checks_leave_one_whole_file: 6 of 8 lost the rename race ... ok`. |
| SC-025 | pass | `ecma_pattern_translation ... ok`; Node reference `url FEFF false | url 0085 true | CON.txt true | CON.CR false | CON.2028 false`. |
| SC-026 | pass | `agent_trusts_the_os_store ... ok` (RootCerts::PlatformVerifier for every purpose). |
| SC-027 | pass | `os_version_cases`, `needs_newer_os_table` ok. |
| SC-028 | pass | `resolve_proxy_cases`, `loopback_proxy_only` ok. |
| SC-029 | pass | `windows_proxy_parser_cases`, `macos_proxy_parser_cases`, `linux_proxy_parser_cases` ok on Windows; no test calls a real reader. |
| SC-030 | pass | `proxy_path_connect`, `proxy_path_http`, `no_proxy_bypass`, `check_through_proxy_writes_cache`, `connect_to_remote_host_is_refused` ok. |
| SC-031 | pass | `fresh_lower_version_replaces_cache ... ok`. |
| SC-032 | pass | `one_attempt_per_check ... ok`. |
| SC-033 | pending-ci | Runnable rows green (`pac_helper_cases`, `find_proxy_result_cases` ok). Engine rows `url_argument_cases`, `script_world_cases` are cfg(not(windows)); no WSL evidence (evidence/wsl-core.txt absent). |
| SC-034 | pending-ci | `pac_policy_cases ... ok`; `pac_path_proxy`, `pac_path_direct`, `pac_list_walk`, `pac_per_hop`, `pac_fetched_once_never_via_proxy` not compiled here. Windows twin `os_pac_walk` passed. |
| SC-035 | pending-ci | `wpad_candidate_cases ... ok`, `resolve_proxy_cases ... ok`; `wpad_discovery_walk`, `pac_fallback_to_static_proxy` cfg(not(windows)). |
| SC-036 | pending-ci | `script_failure_cases`, `pac_body_cap`, `check_survives_pac_failure` all cfg(not(windows)); no WSL evidence. Windows twin `os_pac_failure_cases` passed. |
| SC-037 | pass | `model_range_cases ... ok`; evidence/model-range-node.txt `row 1 v2 findings=0` ... `row 6 v1 findings=0`. |
| SC-038 | pass | `reader_dependencies_are_the_ruled_set ... ok` (5 plants); windows tree rquickjs count 0; linux tree shows rquickjs, rquickjs-core, rquickjs-sys v0.14.0; one core-foundation v0.10.1, one rustls v0.23.46. |
| SC-039 | pass | `linux_desktop_choice_cases`, `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` ok. |
| SC-040 | pending-ci | `from_cf_proxies_cases` is macOS-only. Runs everywhere: `core_dependencies_are_the_fr007_set ... ok`. |
| SC-041 | pass | `os_pac_walk`, `os_pac_failure_cases` (`os_pac_failure_cases blocking row elapsed 2.00825s: Err(Timeout)`, bound 3 s), `winhttp_answer_cases` ok. |
| SC-042 | pending-ci | `dns_bound_cases` is cfg(not(windows)); no WSL evidence. |
| SC-043 | pass | `pac_clock_cases`, `parse_utc_offset_cases`, `offset_range_cases` ok. |
| SC-044 | pass | roadmap.md:902 single rustsec.org line; results.md numstat `1 0`; scope-check OK. |
| CC-system | pass | Real paths only in the injected OS layer and the one FEED_URL (src-tauri/src/lib.rs:23); tests use loopback and TempDir; live test behind feature plus #[ignore]; guard suites ran (config_guard 5, injection 9, roster 2). |
| CC-verify | pass | Not applicable: no artifact download this phase (Phase 7). PAC script is executed in a sandbox (memory 32 MiB, 2 s interrupt, macOS/Linux only). Re-verify in Phase 7. |
| CC-zipslip | pass | Not applicable: no extraction code (Phase 8). Cargo.lock has no tar or zip crate; flate2 is dev-only. |
| CC-helper | pass | Helper unchanged stub (`git diff --stat HEAD -- apps/krate-manager/crates/helper` empty). |
| CC-capabilities | pass | capabilities/main.json grants only allow-app-version, allow-startup-error; no capability, config or frontend diff. |
| CC-signing | pass | Only ad-hoc macOS identity `-`; no new key or secret (git status grep for key/secret files returned nothing). |
| CC-warnings | pass | fmt, clippy -D warnings (default, --locked, dev-feed), tsc, eslint all exit 0 with 0 warnings. cfg(not(windows)) and macOS code lints only in CI (manager-ci.yml:83-86). |
| CC-lints | pass | `lint-serial-test-runs: clean (64 files)`; check-portability not required (no C++ changed). |
| CC-feed-compat | pass | Feed unchanged; the manager reads the same schema_version 1 fields as update_checker.cpp:215-227. |

## PENDING-CI list

The user pushes. Nothing here was pushed. Jobs are in `.github/workflows/manager-ci.yml` (`check (windows-latest | macos-latest | ubuntu-24.04)`, matrix at :28-34) and `feed-tools.yml` (`node-test (ubuntu-latest | macos-latest | windows-latest)`, :49-54).

| Item | CI job and assertion |
|---|---|
| FR-049 | macOS and Linux legs, `cargo test` plus `required tests ran`: `sysproxy::tests::bounded_run_abandons_and_reaps`, `sysproxy_cf::tests::from_cf_proxies_cases` |
| FR-064 | macOS/Linux `required tests ran`: `pac::tests::script_world_cases`, `url_argument_cases`, `script_failure_cases` |
| FR-065 | macOS/Linux: `http::tests::pac_fetched_once_never_via_proxy`, `pac_body_cap` |
| FR-066 | macOS/Linux: `http::tests::wpad_discovery_walk` |
| FR-067 | macOS/Linux: `http::tests::pac_list_walk`, `pac_per_hop` |
| FR-068 | macOS/Linux `cargo test`: `pac::tests::script_failure_cases ... ok` (one warning per failure plus engine bounds) |
| FR-069 | macOS/Linux: `http::tests::pac_fallback_to_static_proxy` |
| FR-073 | macOS and Linux legs `cargo test` and release build (no system library needed) |
| FR-074 | Linux leg: `sysproxy::tests::bounded_run_abandons_and_reaps` |
| FR-075 | macOS leg: `sysproxy_cf::tests::from_cf_proxies_cases` |
| FR-083 | all three legs, step `roster absent`, `::notice ... 0 hits` |
| FR-094 | macOS/Linux: `pac::tests::dns_bound_cases` |
| FR-095 | macOS and Linux legs, steps `clippy` exit 0 and `cargo test` (real CFTimeZone and `date +%z` readers) |
| SC-007 | macOS/Linux: `version::tests::native_candidates_macos` / `_linux` |
| SC-033 | macOS/Linux: `pac::tests::url_argument_cases`, `script_world_cases` |
| SC-034 | macOS/Linux: `http::tests::pac_path_proxy`, `pac_path_direct`, `pac_list_walk`, `pac_per_hop`, `pac_fetched_once_never_via_proxy` |
| SC-035 | macOS/Linux: `http::tests::wpad_discovery_walk`, `pac_fallback_to_static_proxy` |
| SC-036 | macOS/Linux: `pac::tests::script_failure_cases`, `http::tests::pac_body_cap`, `feed::tests::check_survives_pac_failure` |
| SC-040 | macOS: `sysproxy_cf::tests::from_cf_proxies_cases` |
| SC-042 | macOS/Linux: `pac::tests::dns_bound_cases` |
| SC-016 (other two legs) | macOS and Ubuntu, step `roster absent`, `::notice ... 0 hits` (the Windows half passed in close-lane B5) |

Also on CI for passing rows: every `cargo.all` name via `required tests ran`, and `node-test` x3 for the `1..59` plan (SC-023).

## Implementation notes (deviations reported by task agents)

- T001: spec edits tagged `(plan SI-n)` / `(tasks TA-n)`; wsl-lane.js runs the WSL command under GNU `timeout --kill-after=30 2400`; spec line numbers drifted (:1305 became :1308) and were matched by text.
- T002: the clippy-guard fixture lockfile (`tests/fixtures/clippy-guard/Cargo.lock`) was missed by the plan and made L-GUARD red under `--locked`; `set_logger` also appears in the Phase 4 `src-tauri/src/log_sink.rs:49`; context_tests.rs had 21 call sites, not 27/33; new files were converted from CRLF to LF.
- T003: `ConnectOk` reply has no `Connection: close`; extra 431/400 handling; Gzip sends no Content-Type.
- T004: fixtures live under `tools/feed-contract/fixtures/feeds/valid/`; `NativePlatform` written inline in `resolve_native` to keep the once-only rule; extra test cells added.
- T005: ecma_pattern_to_rust goes beyond PD-11 (escapes `[`, `&`, `~` in classes, refuses empty classes); check_install_value refuses unknown field names; one `cargo fmt -p` run may have touched http.rs (unconfirmed).
- T006: malformed inputs give `SystemProxy::default()` plus one warn; KDE ReversedException drops NoProxyFor; hand-written Debug for WinIeProxyConfig and GnomeRaw; `bounded_run_abandons_and_reaps` has not run anywhere.
- T007: not compiled on Windows; API use checked by reading core-foundation 0.10.1 source; first compile is the macOS CI leg.
- T008: 20 hanging names returns `[Direct]`, not an error (bound still holds); `isInNet(null, ...)` returns false; added `helpers::wants_gmt`; `DnsMemo::lookup` takes a deadline. WSL timings: endless loop 2.000 s, regex 2.001 s, heap 0.291 s, 20 hanging names 2.001 s.
- T009: no red-first run recorded; the plan's SC-012 "every advancing class is E_FEED_OFFLINE" conflicts with FR-044 (Timeout maps to E_FEED_TIMEOUT); the blocking OS-PAC row ends Err(Timeout) because the wait uses the whole chain deadline; the 8 cfg(not(windows)) tests were never compiled by this task.
- T010: CallbackCtx owns the URL buffers and options for the async call; context bound via exposed-provenance usize; added a WinHttpSetStatusCallback failure check. The host-level double warn it reported was later fixed (see FR-068/FR-095).
- T011: live_feed.rs finds FEED_URL by line text (now lib.rs:24, not :16).
- T012: added `check_http_injection` plus 10 plants.
- T013: `use` declarations not counted as call sites for the version.rs once-only rule; test-body attribution via bracket matching; tests/required-tests.json was edited later by T017.
- T014: check_survives_pac_failure uses `loopback(1 s, 4 s)` because the eval deadline is min(2 s, total); values C1 guarantees but the builder refuses are dropped with one `E_MODEL_RANGE` warn; ignored-cache warn carries no code.
- T015: scope-check.js readings (hunk windows, `--no-renames`, hard-coded base a145271b).
- T016: A8 treats only the named `ring` / `rquickjs-sys` build-script failures as the TA-7 skip; A9 uses Phase 4's argument order.
- T017: three files stayed LF though the task listed CRLF; Linux names could not be confirmed against an L-WSL log (none exists).
- T018: status `blocked` on L-GUARD (see gap 3); all other listed commands passed. `node tools/clean-test-output.js --apply` removed F:\tmp\km5-target and F:\tmp\gate logs (1.11 GB), so only the evidence/ copies remain.

## Remaining steps for the human loop

1. Review the diff and the compliance rows above.
2. Done 2026-10-31: `node specs/krate-manager-phase5/evidence/close-lane.js` (full, including A11 L-WSL), `CLOSE LANE GREEN`; log at specs/krate-manager-phase5/evidence/close-lane.log.
3. Commit (no amend).
4. The user pushes; CI then proves the pending-ci rows above.
