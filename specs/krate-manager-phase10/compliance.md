# Compliance report: Phase 10 (krate-manager-phase10)

**Overall status: COMPLETE (PENDING-CI: 1 item(s))** (resume R1 / T020 written by the main loop 2026-11-01 from Manager CI run 41342540379 (`dba1dff9`); close lane green: evidence/close-lane.log (`--after-m1`, 2026-11-01T19:57:21Z, wall=225.9s, last line `CLOSE LANE GREEN`)). 0 fail, 0 partial. 9 former PENDING-CI items are proven by the green `helper-elevated` (Windows, macOS), `inno-locked-file` and `check (ubuntu-24.04)` jobs; the rest wait on the Windows / macOS `check` legs, red on 'cargo test' in that run (tasks.md D-13, fixed here). Update 2026-11-01: Manager CI run 41344479028 (`4f04dc03`, all seven jobs success) proved 14 more; the only open row is SC-031's skip half (a push that touches no gated path skips both elevated jobs; every push so far touched them).

## Honest gap list (fail and partial items first)

| Item | Verdict | Gap |
|------|---------|-----|
| FR-061 | pass | M-1 recorded by the main loop at the resume R1 (T020): spec.md "## M-1: the locked-file measurement (recorded 2026-11-01)" (date, Manager CI run 41342540379 (`dba1dff9`), every value, the ten verbatim log lines); `tests/fixtures/helper/inno-locked/` exit_code.txt `5`, seconds.txt `4.3`, classes.json `{"dll":"old","unlocked":"old"}`, locked_line.txt `DeleteFile: The existing file appears to be in use (5).`, log.txt (the ten lines); .gitkeep removed; `.gitattributes` pins them `-text`. Close lane G4 `M-1 written in (FR-061, FR-062): ... missing=none`. |
| FR-062 | pass | classify.rs `LOCKED_EXIT = 5`, `LOCKED_LINE = "DeleteFile: The existing file appears to be in use (5)."`; `classify_inno`: 0 -> Installed, LOCKED_EXIT with LOCKED_LINE in the log -> Locked, else Failed. Measurement shows none of the re-ruling cases (exit 5, DLL old, no pending rename, 4.3 s). `classify::tests::recorded_locked_case ... ok` (asserts LOCKED_LINE == locked_line.txt, LOCKED_EXIT == exit_code.txt, the line removed -> Failed, other codes Failed, 500 rollback lines after the line -> Locked); `install::tests::unrecognised_log_falls_back ... ok` (exit 5 without the line -> `InstallerFailed { exit_code: 5 }`). Red before the rule: `error[E0425]: cannot find value LOCKED_LINE`. |
| FR-102a | pass | manager-ci.yml `inno-locked-file` step 'locked-file measurement': reads `$m1/exit_code.txt`, `$m1/locked_line.txt`, `$m1/classes.json` and emits `::error title=inno-locked-file (M-1 changed)::` per mismatch (exit code, the line missing from the log, DLL class, unlocked-file class), then exits 1. Shape test rule "does not compare with inno-locked/<file> (FR-102a)" plus mutation "FR-102a classes comparison removed from inno-locked-file": `node --test manager-ci-shape.test.js` tests 48 pass 48. First hosted run of the assertions: PENDING-CI on the next push. |
| FR-104 | pass | Main-loop close lane evidence/close-lane.log (2026-11-01T18:18:37Z, wall=203.6s, last line `CLOSE LANE GREEN`), B1 `BUNDLE LANE GREEN`: L1 `npm run bundle` exit=0 (116.2 s); L2 build warnings 0; L3 staged sidecar `src-tauri/binaries/krate-manager-helper-x86_64-pc-windows-msvc.exe` 460288 bytes, `--ci-roots` x0; L4 release helper `target/release/krate-manager-helper.exe` 460288 bytes, `--ci-roots` x0; L5 NSIS setup `target/release/bundle/nsis/Krate Manager_0.1.0_x64-setup.exe` (never run). The 7z listing / sha256 half of `helper bundled` is CI-only (TA-16, D-6: 7z absent on this box) and stays in the PENDING-CI list with the macOS half. CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): 'helper bundled' (Windows) extracted the helper from the NSIS setup with 7z, sha256 equal to the staged sidecar; (macOS) present + codesign verify exit 0. |
| FR-123 | pass | (a), (c)-(h) as before; (b) roadmap Phase 10 bullet "Measure the locked-file behaviour (measured 2026-11-01, Phase 10 spec M-1)" records exit 5, the four DeleteFile retries, Abort, nothing replaced, no restart scheduled, no hang, and the mapping; the status line reads COMPLETE. L-SCOPE `--after-m1` `7 roadmap hunks: OK`. |
| SC-016 | pass | Post-M-1 half: `classify::tests::recorded_locked_case`, `ops::tests::recorded_locked_case_through_runner` (the recorded log + exit 5 through the runner and the production classifier -> `locked E_HELPER_LOCKED`; without the line -> `failed E_HELPER_EXIT`), `install::tests::recorded_locked_case_maps_to_locked` (loopback -> `InstallError::Locked`), `install::tests::unrecognised_log_falls_back` each `... ok` once (close lane G5); `provisional_rule` retired from required-tests.json (cargo.all 331; L-REQ `348 cargo + 57 vitest + 3 node`). |
| SC-032 | pass | Main-loop close lane evidence/close-lane.log (2026-11-01T18:18:37Z, wall=203.6s, last line `CLOSE LANE GREEN`): G4 `M-1 fence (SC-032): apps/krate-manager/tests/fixtures/helper/inno-locked/ absent; LOCKED_EXIT / LOCKED_LINE words: none`; G5 `test classify::tests::provisional_rule ... ok` 1 time; spec M-1 section reads "Not yet measured."; roadmap Phase 10 status line reads "stopped at the M-1 fence"; the open-items report is this file's status line and gap list (five M-1 items). |

No open items remain. The M-1 items (FR-061, FR-062, FR-102a, FR-123 (b), SC-016) were written at the resume R1 (T020) from Manager CI run 41342540379 (`dba1dff9`).

**Main-loop close lane:** evidence/close-lane.log (2026-11-01T18:18:37Z, wall=203.6s, last line `CLOSE LANE GREEN`). A1g `cargo test --workspace --locked` 12 binaries, 383 passed, 0 failed; B2 `::notice title=required tests ran (Windows)::345 cargo + 57 vitest + 3 node`; A10 L-WSL `WSL LANE GREEN (409 passed, 83 s)`, `Linux-run Phase 9+10 tests seen ok: 13/13` (early signal; closes no PENDING-CI row); A7/C2 L-SCOPE OK; C1 git status unchanged. The first run was red on A10 only, with every WSL test passing: `wsl-lane.js` took the helper's test line `test result::tests::log_tail_bounds ... ok` for a `test result:` summary line (tasks.md D-11); fixed in the lane script, self-test case added, the whole close lane re-run.

## Compliance table

Evidence is condensed from the verifying agents' reports; every file:line, test name and output string below is theirs.

### Functional requirements

| ID | Verdict | Evidence |
|----|---------|----------|
| FR-001 | pass | job.rs:51-71 RawJob/RawOp `#[serde(deny_unknown_fields)]`; :116-156 op() requires exactly the kind's keys; :162 job_version != 1, empty or >32 ops refused; :167 op.target() must equal os, HelperOs::Linux refuses all; :172-174 -> E_HELPER_JOB_PARSE; lib.rs:115,126-130 -> exit 3. `job::tests::parse_refuses_malformed_jobs`, `parse_checks_the_target_os`, `no_field_adds_a_root` ... ok. |
| FR-002 | pass | job.rs:51-71 typed pass (duplicate_field error); :104-113 objects_only rejects array-form. job_tests.rs:150-180 duplicate op/file/job_version/ops rows; 45/45 helper unit tests. |
| FR-003 | pass | lib.rs:98-104 create_new reservation (AlreadyExists = exit 4 E_HELPER_RESULT_EXISTS, else E_HELPER_RESULT_WRITE); lib.rs:117-122 ResultFile; result.rs:186-215, :223-227 write_all then sync_all, :249-264 last 40 lines, each <=512 bytes. `result::tests::log_tail_bounds`, `write_error_is_reported` ... ok. |
| FR-004 | pass | ops.rs:148-184 run_batch continues after failed/refused; only Step.stops (:313-318) turns later ops NotRun. `ops::tests::batch_of_three_middle_locked` ... ok. |
| FR-005 | pass | cli.rs:35-57 parse_args exactly --job and --job-sha256; --ci-roots only under cfg(feature "ci-temp-roots"); digest.rs:292-302 parse_hex64; cli.rs:80-86 USAGE once, exit 2. `cli::tests::parse_args_accepts_exactly_the_two_flags`, `malformed_hash_is_usage_error`, `ci_roots_flag_is_usage_error`, `usage.rs helper_usage_exits_2` ok. |
| FR-020 | pass | validate.rs:172-205 source(): absolute, no NUL, no '..', canonicalize == path, strictly under staging_root, lstat File, extension; E_HELPER_SOURCE -> refused. `validate::tests::path_rules`, `rejects_crafted_jobs`, `verbatim_paths_windows` ... ok. |
| FR-021 | pass | validate.rs:79-108 job_location (canonical, `^job-[0-9a-f]{32}\.json$`, parent 'jobs', grandparent STATE_DIR_NAME); failure -> lib.rs:93-96 exit 4 E_HELPER_JOB_LOCATION before the result file is reserved. |
| FR-022 | pass | Only std::env use in non-test helper sources is main.rs:12 args_os; no env::var/home_dir/temp_dir; roots.rs:63-112 known folders/literals only; clippy workspace 0 warnings. `roots::tests::binary_roots_ignore_job_and_settings` ... ok. |
| FR-023 | pass | lib.rs:61-77 read_job bounded 64 KiB, E_HELPER_JOB_READ / E_HELPER_JOB_HASH before parse. validate_tests.rs:923-924 rows '64 KiB + 1 byte', '4 x 64 KiB'. |
| FR-024 | pass | grep for xattr/chown/SetNamedSecurityInfo/SetFileSecurity/SetSecurityInfo/com.apple.quarantine over non-test helper sources: 0 hits; only ACL is creation DACL os.rs:477-512. injection tests 31 passed. |
| FR-025 | pass | validate.rs:111-168 state_dir_ok (UnderProfiles / SameOwnerNonRoot / Exact). `validate::tests::rejects_crafted_jobs`, `performs_valid_jobs` ... ok. |
| FR-026 | pass | roots.rs:219-246 canonical(); :251-273 canonical_path; lib.rs:88-91 exit 4 on failure. `roots::tests::canonical_keeps_missing_tails`, `canonical_refuses_non_plain_components`, `validate::tests::symlinked_parent_root_windows` ... ok. |
| FR-027 | pass | validate.rs:398-412 component_eq: VerbatimDisk vs Disk differ; install.rs:939, :1236 core writes verbatim forms. `validate::tests::verbatim_paths_windows`, `windows_quoting.rs windows_parameters_parse_back_windows` ... ok. |
| FR-030 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`) and run 41341359391 (`f6022bd5`): `helper-elevated (windows-latest)` success: "run directory DACL protected (flags P), exactly (A;OICI;FA;;;SY)(A;OICI;FA;;;BA), no inherited ACE"; "stub argv equals SC-002's Inno argv"; "stub current_exe ... lies in the run directory"; "the stub job left no run-* directory". |
| FR-031 | pass | ops.rs:338-379; validate.rs:209-251 uninstaller; argv [exe, /VERYSILENT, /SUPPRESSMSGBOXES, /NORESTART]; 500 ms poll within 120 s. `ops::tests::uninstall_polls_until_gone`, `uninstall_poll_times_out`, `uninstaller_never_exits`, `exact_argv` ... ok. |
| FR-032 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `helper-elevated (macos-latest)` success: "run_pkg_installer placed the four fixture trees"; "helper exit 0; result file ... present"; "preinstall stat of the run directory: 0 drwx------ 700". |
| FR-033 | pass | Defect fixed: ops.rs:392-408 pkgutil() maps `Err(_) => Err(error::SPAWN)`; read_receipt (ops.rs:411-428) returns spawn error before E_HELPER_PKG_RECEIPT; --forget run (:473-475) gives E_HELPER_SPAWN. Test ops_tests.rs:1562-1600 in `pkg_failure_paths`. Windows `cargo test --workspace --locked`: `test ops::tests::pkg_failure_paths ... ok`, `test ops::tests::spawn_error_is_named ... ok`, exit 0. WSL `cargo test -p krate-manager-core -p krate-manager-helper --locked` exit 0. Clippy (3 runs) 0 warnings; vitest `Tests 58 passed (58)`. Logs under F:/tmp/gate/km10-reverify/. |
| FR-034 | pass | validate.rs:285-303, :339-374. `validate::tests::pkg_plan_classifies`, `ops::tests::pkg_paths_classified` ... ok. |
| FR-035 | pass | ops.rs:62-92 copy_hashed (64 KiB chunks, hash over written bytes, sync_all). `ops::tests::runs_the_copy_never_the_original` ... ok. |
| FR-036 | pass | job.rs:15-20 four ops only; Registry trait read-only; job.rs:166-167 Linux refuses all; bundle.js:89-98 no Linux helper. `job::tests::parse_checks_the_target_os` ... ok. |
| FR-040 | pass | ops.rs:153-181 single loop; spawn-then-wait per op. `ops::tests::batch_of_three_middle_locked` ... ok. |
| FR-041 | pass | io.now() deltas; Instant only in os.rs among non-test helper sources; injection rules Instant::now (4) / SystemTime (3). |
| FR-042 | pass | ops.rs:312-318 timeout -> E_HELPER_TIMEOUT, child never killed; :155-162 later ops NotRun E_HELPER_AFTER_TIMEOUT. `ops::tests::batch_timeout_stops_the_rest` ... ok. |
| FR-043 | pass | ops.rs:182, :243-256 run dir removal. `ops::tests::run_dir_removed_after_job` ... ok. |
| FR-050 | pass | cli.rs:60 RunFn; :79-94 main_with; lib.rs:40-50 Exit; main.rs minimal. `roots::tests::binary_roots_ignore_job_and_settings` ... ok. |
| FR-051 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): `required tests ran` (Windows) `348 cargo + 57 vitest + 3 node`, (macOS) `380 cargo + 57 vitest + 3 node`, (Linux) `374 cargo + 57 vitest + 3 node` (the step fails unless every required name passes exactly once); roots::tests::production_roots_macos is a cargo.macos name. |
| FR-052 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): 'test ci-temp-roots' "cli::tests::ci_roots_file_is_read passed once" and 'clippy ci-temp-roots' "clean" on Windows, macOS and Linux. |
| FR-053 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): `required tests ran` (Windows) `348 cargo + 57 vitest + 3 node`, (macOS) `380 cargo + 57 vitest + 3 node`, (Linux) `374 cargo + 57 vitest + 3 node` (the step fails unless every required name passes exactly once); the three os::tests::*_unix names are cargo.macos and cargo.linux names. |
| FR-060 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `inno-locked-file (windows-latest)` success, every FR-060 value annotated (spec M-1), the log lines in the new step 'locked-file log lines' (the first run lost them to the ten-notices-per-step limit, tasks.md D-12). |
| FR-061 | pass | M-1 recorded by the main loop at the resume R1 (T020): spec.md "## M-1: the locked-file measurement (recorded 2026-11-01)" (date, Manager CI run 41342540379 (`dba1dff9`), every value, the ten verbatim log lines); `tests/fixtures/helper/inno-locked/` exit_code.txt `5`, seconds.txt `4.3`, classes.json `{"dll":"old","unlocked":"old"}`, locked_line.txt `DeleteFile: The existing file appears to be in use (5).`, log.txt (the ten lines); .gitkeep removed; `.gitattributes` pins them `-text`. Close lane G4 `M-1 written in (FR-061, FR-062): ... missing=none`. |
| FR-062 | pass | classify.rs `LOCKED_EXIT = 5`, `LOCKED_LINE = "DeleteFile: The existing file appears to be in use (5)."`; `classify_inno`: 0 -> Installed, LOCKED_EXIT with LOCKED_LINE in the log -> Locked, else Failed. Measurement shows none of the re-ruling cases (exit 5, DLL old, no pending rename, 4.3 s). `classify::tests::recorded_locked_case ... ok` (asserts LOCKED_LINE == locked_line.txt, LOCKED_EXIT == exit_code.txt, the line removed -> Failed, other codes Failed, 500 rollback lines after the line -> Locked); `install::tests::unrecognised_log_falls_back ... ok` (exit 5 without the line -> `InstallerFailed { exit_code: 5 }`). Red before the rule: `error[E0425]: cannot find value LOCKED_LINE`. |
| FR-063 | pass | Fence holds: no M-1 values (spec.md:1419-1430), no inno-locked/ fixture, provisional classify_inno (classify.rs:315-321), roadmap :713 '**Status: stopped at the M-1 fence (Phase 10 built; `inno-locked-file` must run after the user's push)**'. manager-ci.yml:863 confirms no FR-102a assertion yet. |
| FR-070 | pass | install.rs:507-513 driver_for for each Os. `install::tests::drivers_per_os` ... ok. |
| FR-071 | pass | install.rs:516-591 InstallEnv/CommandEnv/native_env/Elevation trait/ElevatedCommand/LaunchOutcome. |
| FR-072 | pass | install.rs:1218-1275 installer_op / uninstall_op; refusals keep ActionResult; no op -> no job. `install::tests::pre_job_refusals` ... ok. |
| FR-073 | pass | install.rs:920-975 job::write; :664-677 remove_job_files. `install::tests::job_files_written_and_removed`, `job_hash_is_the_verified_digest` ... ok. |
| FR-074 | pass | install.rs:3379-3401 recover. `install::tests::recover_removes_stale_jobs` ... ok. |
| FR-075 | pass | install.rs:1302-1313 Elevating once before launch. `install::tests::elevating_event_order` ... ok. |
| FR-076 | pass | install.rs:1298-1337, job::ceiling :985-997, job::reports :1060-1100. `install::tests::launch_outcomes_map`, `one_launch_per_batch`, `batch_locked_middle_maps_each` ... ok. |
| FR-076a | pass | install.rs:1179-1191, :1346-1372 confirmed re-detects. `install::tests::not_applied_detected` ... ok. |
| FR-077 | pass | install.rs:1166-1172; platform/mod.rs:18-19 KILLALL_ARGV. `install::tests::macos_install_update_remove` ... ok on Windows. |
| FR-078 | pass | install.rs:605-660 ElevationGuard. `install::tests::concurrent_actions` ... ok. |
| FR-078a | pass | install.rs:1318-1321, :622-627. `install::tests::timed_out_holds_guard` ... ok. |
| FR-079 | pass | install.rs:171-195 new variants with FR-133 texts. `install::tests::new_error_texts` ... ok. |
| FR-080 | pass | install.rs:3640-3755 action_gate / RemoveBasis. `install::tests::gate_remove_basis` ... ok. |
| FR-081 | pass | install.rs:3760-3787. `status::tests::actions_per_state` ... ok. |
| FR-082 | pass | install.rs:3953-3959, :4072-4151. `install::tests::remove_elsewhere_refused`, `commands_run_over_the_driver` ... ok. |
| FR-083 | pass | status.rs:222-245, :73-83, :423-429; src/view.ts:91,288,306; 11 of 13 status fixtures carry `"remove_hint": null` as last key. |
| FR-084 | pass | install.rs:4056-4067, :4171-4177; Notice::RescanDawMacos :361-365. |
| FR-090 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): `check (macos-latest)` 'clippy' and the release build green (the osascript launcher compiles), 'build warnings' "0 warning lines". |
| FR-091 | pass | install.rs:722-744. `install::tests::windows_parameters_quote`, `windows_quoting.rs windows_parameters_parse_back_windows` ... ok. |
| FR-092 | pass | tauri.helper.conf.json; scripts/bundle.js; package.json; .gitignore; config_guard 9 passed. node 'bundle sidecar name per host tuple' ok. |
| FR-093 | pass | Cargo.toml:50 windows-sys 0.61.2 features; homes src-tauri/Cargo.toml:37-38, helper Cargo.toml:29-30; injection tests ok. |
| FR-100 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `helper-elevated (macos-latest)` and `(windows-latest)` success (notices quoted under FR-030, FR-032, SC-018). |
| FR-101 | pass | tests/fixtures/inno/fixture.iss, tests/fixtures/locked-dll/ (cdylib), excluded from workspace. `ok 2 - fixture inno script keeps the plugin directives`. |
| FR-102 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `inno-locked-file (windows-latest)` success (two fixture compiles, the 1.0.0 install, the holder, the 1.0.1 run, the annotations). |
| FR-102a | pass | manager-ci.yml `inno-locked-file` step 'locked-file measurement': reads `$m1/exit_code.txt`, `$m1/locked_line.txt`, `$m1/classes.json` and emits `::error title=inno-locked-file (M-1 changed)::` per mismatch (exit code, the line missing from the log, DLL class, unlocked-file class), then exits 1. Shape test rule "does not compare with inno-locked/<file> (FR-102a)" plus mutation "FR-102a classes comparison removed from inno-locked-file": `node --test manager-ci-shape.test.js` tests 48 pass 48. First hosted run of the assertions: PENDING-CI on the next push. |
| FR-102b | pass | manager-ci.yml:21, :443-501 `changes` job, gates :506-507, :866-867. Shape test: 47/47 pass locally. |
| FR-103 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): 'ci-temp-roots absent' (Windows) and (macOS) "cargo tree hits=0, marker in source=3, in the bundled helper=0, in the staged sidecar=0". |
| FR-103a | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): 'test ci-temp-roots' "passed once" on all three legs. |
| FR-104 | pass | Main-loop close lane evidence/close-lane.log (2026-11-01T18:18:37Z, wall=203.6s, last line `CLOSE LANE GREEN`), B1 `BUNDLE LANE GREEN`: L1 `npm run bundle` exit=0 (116.2 s); L2 build warnings 0; L3 staged sidecar `src-tauri/binaries/krate-manager-helper-x86_64-pc-windows-msvc.exe` 460288 bytes, `--ci-roots` x0; L4 release helper `target/release/krate-manager-helper.exe` 460288 bytes, `--ci-roots` x0; L5 NSIS setup `target/release/bundle/nsis/Krate Manager_0.1.0_x64-setup.exe` (never run). The 7z listing / sha256 half of `helper bundled` is CI-only (TA-16, D-6: 7z absent on this box) and stays in the PENDING-CI list with the macOS half. CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): 'helper bundled' (Windows) extracted the helper from the NSIS setup with 7z, sha256 equal to the staged sidecar; (macOS) present + codesign verify exit 0. |
| FR-105 | pass | src-tauri/Cargo.toml manual-elevation feature and example; examples/manual_elevation.rs:85-166; clippy -p krate-manager --example manual_elevation --features manual-elevation: 0 warnings. |
| FR-106 | pass | manager-ci.yml check job: no sudo except Linux packages, helper never run; enforced by manager-ci-shape.test.js (47 pass). |
| FR-107 | pass | manager-ci-shape.test.js:404-454, :542-593, mutations :1473-1509. tests 47, pass 47, fail 0. |
| FR-108 | pass | tests/required-tests.json keys cargo{all 328, windows 17, macos 49, linux 43}, vitest 57, node 3; all 345 cargo.all + cargo.windows names pass exactly once locally (bad: 0). |
| FR-109 | pass | manager-ci.yml:181-188, :198-225; local `node --test scripts`: ok 1, ok 2, ok 3 (pass 3, fail 0). |
| FR-110 | pass | Fake seams over temp dirs (seams.rs:70-893), FakeElevation (test_support.rs:417-486); local run raised no prompt and wrote only temp dirs. |
| FR-111 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `helper-elevated` (both) and `inno-locked-file` success; roots under RUNNER_TEMP ("roots under D:\a\_temp\real", "/Users/runner/work/_temp/real"). |
| FR-112 | pass | injection.rs new rules with plants; `injection.rs 31 passed, 0 failed`. |
| FR-113 | pass | roster.rs:104-110, :138, :325-326; `roster.rs 5 passed`. |
| FR-114 | pass | install.rs:1206, :1314, :1009-1042 (log tails debug! only). |
| FR-120 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `check (ubuntu-24.04)` success including 'required tests ran'. Windows / macOS legs: `check` red on 'cargo test' (D-13, fixed; re-run on the next push). |
| FR-121 | pass | extract.rs, download.rs, verify.rs, feed.rs, detect.rs, context.rs unmodified; status.rs diff limited to remove_hint/RemoveHint and gate-over-detection. |
| FR-122 | pass | `git status --short -- plugins dsp cmake extern .github/workflows/{release,feed,ci}.yml tools`: only tools/feed-contract/tests/manager-ci-shape.test.js modified. |
| FR-123 | pass | (a), (c)-(h) as before; (b) roadmap Phase 10 bullet "Measure the locked-file behaviour (measured 2026-11-01, Phase 10 spec M-1)" records exit 5, the four DeleteFile retries, Abort, nothing replaced, no restart scheduled, no hang, and the mapping; the status line reads COMPLETE. L-SCOPE `--after-m1` `7 roadmap hunks: OK`. |
| FR-130 | pass | status.ts:371-389, :436-438; vitest 'renders install on windows and macos' passed; 58/58. |
| FR-131 | pass | status.ts INSTALL_TEXTS / ADMIN_LINES; four confirmation vitest cases passed. |
| FR-132 | pass | status.ts `elevating`; vitest 'shows waiting for administrator permission' passed. |
| FR-133 | pass | install.rs:172, :179, :188, :199-205 verbatim; status.ts outcomeLines AU steps; vitest refused / locked / macos au rescan / stale busy passed. |
| FR-134 | pass | status.ts:461, :436-438; vitest 'outside-root install shows the windows settings hint and no remove' passed. |
| FR-135 | pass | install.rs:1316, :1325; vitest 'refused permission shows nothing changed and keeps the actions' passed. |

### Success criteria

| ID | Verdict | Evidence |
|----|---------|----------|
| SC-001 | pass | `cargo test --workspace --locked` exit 0 (48 s): `validate::tests::rejects_crafted_jobs`, `performs_valid_jobs`, `verbatim_paths_windows` ok; helper lib `45 passed; 0 failed`. Rows read at validate_tests.rs:421-501, :563-1450. |
| SC-002 | pass | `ops::tests::exact_argv ... ok`; ops_tests.rs:398-403, :457-475, :553-583. |
| SC-003 | pass | `batch_of_three_middle_locked`, `batch_timeout_stops_the_rest`, `uninstall_only_job_has_no_run_dir`, `run_dir_failure_fails_installers`, `copy_error_is_named`, `spawn_error_is_named`, `classify_sees_whole_bounded_log`, `install::tests::batch_locked_middle_maps_each` ... ok. |
| SC-004 | pass | `uninstall_polls_until_gone`, `uninstall_poll_times_out`, `uninstaller_never_exits` ... ok; ops_tests.rs:1010-1155. |
| SC-005 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): `required tests ran` (Windows) `348 cargo + 57 vitest + 3 node`, (macOS) `380 cargo + 57 vitest + 3 node`, (Linux) `374 cargo + 57 vitest + 3 node` (the step fails unless every required name passes exactly once). |
| SC-006 | pass | `pkg_paths_classified`, `pkg_failure_paths`, `log_tail_bounds`, `run_dir_removed_after_job` ... ok. |
| SC-007 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): `required tests ran` (Windows) `348 cargo + 57 vitest + 3 node`, (macOS) `380 cargo + 57 vitest + 3 node`, (Linux) `374 cargo + 57 vitest + 3 node` (the step fails unless every required name passes exactly once). |
| SC-008 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): `required tests ran` (Windows) `348 cargo + 57 vitest + 3 node`, (macOS) `380 cargo + 57 vitest + 3 node`, (Linux) `374 cargo + 57 vitest + 3 node` (the step fails unless every required name passes exactly once); validate::tests::symlinked_parent_root_unix passes on macOS and Linux. |
| SC-009 | pass | `install::tests::refused_prompt_changes_nothing ... ok`; vitest 'refused permission shows nothing changed and keeps the actions' ok. |
| SC-010 | pass | `install::tests::one_launch_per_batch ... ok`. |
| SC-011 | pass | `test core_written_jobs_parse ... ok`, `1 passed; 0 failed`. |
| SC-012 | pass | `drivers_per_os`, `gate_remove_basis`, `remove_elsewhere_refused`, `record_outside_sandbox_panics - should panic` ... ok. |
| SC-013 | pass | `status::tests::actions_per_state ... ok`. |
| SC-014 | pass | `windows_install_update_remove`, `macos_install_update_remove`, `job_hash_is_the_verified_digest` ... ok. Note: test uses versions 1.2.0 to 1.3.0 where spec text says 1.0.0 to 1.1.0; asserted behaviour is the same. |
| SC-015 | pass | `launch_outcomes_map`, `pre_job_refusals`, `elevating_event_order`, `osascript_outcome_parses`, `windows_parameters_quote`, `windows_parameters_parse_back_windows` ... ok. |
| SC-016 | pass | Post-M-1 half: `classify::tests::recorded_locked_case`, `ops::tests::recorded_locked_case_through_runner` (the recorded log + exit 5 through the runner and the production classifier -> `locked E_HELPER_LOCKED`; without the line -> `failed E_HELPER_EXIT`), `install::tests::recorded_locked_case_maps_to_locked` (loopback -> `InstallError::Locked`), `install::tests::unrecognised_log_falls_back` each `... ok` once (close lane G5); `provisional_rule` retired from required-tests.json (cargo.all 331; L-REQ `348 cargo + 57 vitest + 3 node`). |
| SC-017 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `helper-elevated (windows-latest)` success: "the process token is elevated (Administrators enabled, High Mandatory Level)"; "fixture install done in 1.45 s ... HKLM (64-bit) ... _is1 written"; helper exit 0 with result files present; SDDL notice as FR-030. |
| SC-018 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `helper-elevated (macos-latest)` success: run directory "owned by uid 0, mode 0700, while the pkg op ran (D7)"; "the run directory is gone after the job"; "no placed path carries com.apple.quarantine"; pkgutil --only-dirs structural entries listed. |
| SC-019 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): 'test ci-temp-roots' "passed once"; 'ci-temp-roots absent' "marker in source=3, in the bundled helper=0, in the staged sidecar=0" (Windows, macOS). |
| SC-020 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): 'helper bundled' (Windows) "entry 'krate-manager-helper.exe' beside 'krate-manager.exe'; extracted sha256 c56e40cf... equals the staged src-tauri/binaries/krate-manager-helper-x86_64-pc-windows-msvc.exe"; (macOS) "Contents/MacOS/krate-manager-helper present", 'codesign verify' "exit 0"; (Linux) "no helper entry ... (D18)". |
| SC-021 | pass | `injection_scanner_reports_planted_violations ... ok`, injection `31 passed`, roster `5 passed`. |
| SC-022 | pass | `job_files_written_and_removed`, `recover_removes_stale_jobs` ... ok. |
| SC-023 | pass | `helper_usage_exits_2 ... ok`; `cli::tests::malformed_hash_is_usage_error ... ok`. |
| SC-024 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): `required tests ran` (Windows) `348 cargo + 57 vitest + 3 node`, (macOS) `380 cargo + 57 vitest + 3 node`, (Linux) `374 cargo + 57 vitest + 3 node` (the step fails unless every required name passes exactly once); install::tests::native_wiring_macos is a cargo.macos name. |
| SC-025 | pass | vitest `Tests 58 passed (58)`; all twelve titles pass once; `install::tests::new_error_texts ... ok`. |
| SC-026 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): fmt, clippy, clippy dev-feed / ci-temp-roots / manual-elevation "clean", typecheck, lint, 'build warnings' "0 warning lines in the release build" on all three legs. |
| SC-027 | pass (CI) | CI Manager CI run 41344479028 (`4f04dc03`, all seven jobs success): `required tests ran` (Windows) `348 cargo + 57 vitest + 3 node`, (macOS) `380 cargo + 57 vitest + 3 node`, (Linux) `374 cargo + 57 vitest + 3 node` (the step fails unless every required name passes exactly once). |
| SC-028 | pass | `launch_program_is_the_resolved_helper`, `helper_file_never_opened`, `helper_resolved_beside_exe` ... ok. |
| SC-029 | pass | `install::tests::timed_out_holds_guard ... ok`. |
| SC-030 | pass | `install::tests::not_applied_detected ... ok`. |
| SC-031 | pending-ci | Local `elevated jobs are path-gated` ok; `tests 47 pass 47 fail 0`; feed-contract suite `# tests 773 # pass 768 # fail 0 # skipped 5`. Not proven: a push touching no gated path must skip both elevated jobs; one touching crates/helper/ must run both. |
| SC-032 | pass | Main-loop close lane evidence/close-lane.log (2026-11-01T18:18:37Z, wall=203.6s, last line `CLOSE LANE GREEN`): G4 `M-1 fence (SC-032): apps/krate-manager/tests/fixtures/helper/inno-locked/ absent; LOCKED_EXIT / LOCKED_LINE words: none`; G5 `test classify::tests::provisional_rule ... ok` 1 time; spec M-1 section reads "Not yet measured."; roadmap Phase 10 status line reads "stopped at the M-1 fence"; the open-items report is this file's status line and gap list (five M-1 items). |

### Cross-cutting constraints

| ID | Verdict | Evidence |
|----|---------|----------|
| CC-system | pass | Greps for system locations / HKLM / github.com / ShellExecuteEx / runas: every hit in the OS layer, compiled-in roots, compare-only tests, inert strings, or fixtures under {#Root}. injection.rs:170-182 confines real seams. Gate log km-test.log:306-341 'injection ... test result: ok. 31 passed; 0 failed'. |
| CC-verify | pass | download.rs:1446 verify before rename; VerifiedArtifact private constructor; job sha256 from verified digest (install.rs:1244); helper hashes while copying (ops.rs:278-279) before spawn. |
| CC-zipslip | pass | extract.rs:1068-1180 guards; `crafted_archives_are_refused`, `escape_cases_unix`, `symlinked_staging_root_unix` passed (km-wsl.log:86, :91, :106); helper `rejects_crafted_jobs ... ok`. macOS leg from CI. |
| CC-helper | pass | Fixed four-op set, deny_unknown_fields, fixed argv, no shell, validation; `cargo test -p krate-manager-helper` `45 passed; 0 failed`; with `--features test-support,ci-temp-roots` 45 + core_jobs 1 + usage 1 ok. Elevated proof is PENDING-CI. |
| CC-capabilities | pass | capabilities/main.json: eight allow-* permissions only, unchanged this phase; CSP default-src 'self'. |
| CC-signing | pass | tauri.conf.json:22 signingIdentity "-"; no certificate, no `secrets.` in manager-ci.yml; fixture pkg built without --sign. |
| CC-warnings | pass | `cargo clippy --all-targets -- -D warnings` EXIT=0; CI variants 0 warnings; `cargo fmt --check` 0; tsc 0; eslint 0; WSL lane 0 '^warning'. macOS-only cfg arms linted only by CI. |
| CC-lints | pass | `lint-serial-test-runs: clean (64 files)`; `--self-test` 'self-test OK (8 serial runs caught, 11 allowed)'; no C++/CMake file changed. |
| CC-feed-compat | pass | Feed unchanged in Phase 10; only manager-ci.yml and manager-ci-shape.test.js modified; plugins/shared/src/update unmodified. |

## PENDING-CI list

The user pushes; nothing here was pushed. Each item closes on a hosted-runner job.

| Item(s) | CI job | Assertion that proves it |
|---------|--------|--------------------------|
| FR-030, SC-017 | `helper-elevated (windows-latest)`, step 'helper jobs (Windows)' | Stub argv matches; SDDL flags P with exactly `(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)` (manager-ci.yml:670-686) |
| FR-031 (real HKLM leg) | `helper-elevated (windows-latest)` | Fixture uninstall (manager-ci.yml:708-714) |
| FR-032, FR-100, SC-018 | `helper-elevated (macos-latest)`, steps 'helper jobs (macOS)' and 'fixture pkg (macOS)' | Outcomes 'done:' and four placed trees (:802-806); '0 drwx------ 700' (:817-820); link case `done:,refused:E_HELPER_SOURCE,done:` |
| FR-051, SC-007 | `check (macos-latest)` | `roots::tests::production_roots_macos ... ok` via 'cargo test' and 'required tests ran' |
| FR-052, FR-103a, SC-019 | `check` (all legs), steps 'test ci-temp-roots', 'clippy ci-temp-roots' | `cli::tests::ci_roots_file_is_read passed once` |
| FR-053, SC-005, SC-027 | `check (macos-latest)` and `check (ubuntu-24.04)` | `os::tests::open_source_refuses_links_and_fifos_unix`, `run_dir_mode_unix`, `wait_timeout_leaves_child_unix` ok in 'required tests ran' |
| SC-008 | `check (macos-latest)`, `check (ubuntu-24.04)` | `validate::tests::symlinked_parent_root_unix` ok |
| FR-060, FR-102 | `inno-locked-file (windows-latest)`, step 'locked-file measurement' | ::notice annotations for every FR-060 value (the M-1 input) |
| FR-090 | `check (macos-latest)`, step 'clippy' | Passes (macOS launcher compiles) |
| FR-103 | `check (windows-latest / macos-latest)`, step 'ci-temp-roots absent' | Notice 'in the bundled helper=0, in the staged sidecar=0' |
| FR-111 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `helper-elevated` (both) and `inno-locked-file` success; roots under RUNNER_TEMP ("roots under D:\a\_temp\real", "/Users/runner/work/_temp/real"). |
| FR-120 | pass (CI) | CI Manager CI run 41342540379 (`dba1dff9`): `check (ubuntu-24.04)` success including 'required tests ran'. Windows / macOS legs: `check` red on 'cargo test' (D-13, fixed; re-run on the next push). |
| SC-020 (and FR-104 macOS/Linux half) | `check (windows-latest / macos-latest)`, step 'helper bundled' | 7z entry with sha256 equal to staged sidecar; macOS presence + codesign verify |
| SC-024 | `check (macos-latest)` | `install::tests::native_wiring_macos` ok |
| SC-026 | `check (macos-latest / ubuntu-24.04)` | fmt, clippy, clippy ci-temp-roots, clippy manual-elevation, typecheck, lint, 'build warnings' zero |
| SC-031 | job `changes` (manager-ci.yml:443-501) | Push touching no gated path skips both elevated jobs; push touching crates/helper/ runs both |
| T019 row 14 | `check` (all three legs) | 'Checkout unchanged' |

## Implementation notes (deviations reported by task agents)

- T001: `command_env_with` signature is `(os, arch, io, extract_io, drivers, (elevation, host_io))` (clippy too_many_arguments). `InstallRig.host_io` is `Mutex<FakeHostIo>`. FakeElevation has extra pub field `otherwise`. Compile-forced edits outside the file list: detect_tests.rs, src/status.ts, src/status.test.ts. Display texts for E_INSTALL_ELEVATION / NO_RESULT / INSTALLER_FAILED chosen by the agent. CanonicalWindows / CanonicalMacos types do not exist. A failing `result::write` after reservation exits 4 E_HELPER_RESULT_WRITE. `cargo fmt --all` rewrites CRLF files to LF; CRLF restored on test_support.rs, injection.rs, src-tauri/src/lib.rs.
- T002: roadmap line numbers after :735 shifted by +2 to +10.
- T003: `objects_only` pre-check added (serde accepts a struct as a JSON array). `core_jobs.rs` is gated `#![cfg(feature = "test-support")]` and runs only in `cargo test --workspace` or with that feature.
- T004: `canonical` leaves `run_base`, `run_ancestors`, `volume` uncanonicalized (spec D7, FR-027), departing from plan §4.1.2.
- T005: absent `staging/` accepted under SameOwnerNonRoot (symlinked staging still refused); removals under `run_base` allowed in SC-001 rows; plan sample pkg id replaced by `com.krateaudio.Alpha`.
- T006: batch rows go through `run` with `StateDirRule::Exact` (adds T004 as expected-red source); a `wait` error stops the batch; FR-043 "job_error when no op ran" not expressible, cleanup failures ignored; unix FIFO row uses `Instant` in `*_tests.rs`.
- T007: faults stay until `clear_faults`; hooks fire once; fake `TempIo` creates non-run-dir entries at 0o755.
- T008: lstat reports `mode & 0o7777`; OsRunner::output drains stdout on a reader thread; Apple O_* constants gated on `target_vendor = "apple"`; unix test targets not compiled locally.
- T009: result `index` assumed 0-based; negative osascript numbers other than -128 map to `E_ELEVATION_OSASCRIPT`; empty argument is quoted; a Remove with `record_error` is NotApplied; `native_wiring_macos` is cfg(macos) so uncompiled here.
- T010: loopback spawn index counts every `spawn` (not `output`); pkgutil 2-arg non-`--pkg-info` call is treated as forget.
- T011: Windows not-available feed rows patched with a separate `{FFFFFFFF-...}` key family.
- T012: elevation-word allowed files include install_tests.rs; real-seams rule reports any mention of OsRunner/OsRegistry/OsHelperIo outside os.rs, main.rs, os_tests.rs; `Fixture` in `.rs` counts only in strings/comments.
- T013: `?` unusable in `launch`, errors mapped to `Failed(code)`; example resolves helper and op before creating anything and canonicalizes `<dir>`.
- T014: `rescanMacos1` carries the intro sentence; admin line on Install, Update and Remove; Remove hidden whenever `remove_hint` is set.
- T015: bundle.js runs the Tauri CLI via `process.execPath` on `tauri.js`; reads helper from `$CARGO_TARGET_DIR/release` when set; `write-test-job.js` canonicalizes `--file` on win32 so the link-spelled job cannot be produced with it (T018 wrote that job in pwsh). eslint.config.mjs fix for scripts/*.js was proposed to the main loop; final T019 run shows `npm run lint` exit 0.
- T016: argv-recorder output keys are `args`, `current_exe`, `sddl`.
- T017: scope-check also accepts `.claude/workflows/krate-manager-phase.js` and `crates/core/src/detect_tests.rs`; the five FR-123 needles in close-lane G6 are the agent's own choice; Phase 9's "lockfiles unchanged" row dropped.
- T018: Inno `options` is a multi-line block plus `/O${{ runner.temp }}\setups`; link-case Windows job written in pwsh; Inno version read from ISCC.exe ProductVersion; `Class` renamed `Get-FileClass`.
- T019: `specs/INDEX.md` regenerated (277 specs); the fence grep was read as covering production `classify::LOCKED_*` only; test-local `TEST_LOCKED_*` constants were later renamed to satisfy the literal grep (see SC-032); `F:/tmp/km10-target` deleted.

## Remaining steps for the human loop

1. Review the working tree (nothing is committed or pushed).
2. ~~Run the main-loop lanes~~ done: evidence/close-lane.log (2026-11-01T18:18:37Z, wall=203.6s, last line `CLOSE LANE GREEN`).
3. Commit (the user authorises; no amend).
4. Push, optionally `workflow_dispatch` on manager-ci.yml, to run the PENDING-CI proof above.
5. ~~R1 (T020)~~ done in the main loop from Manager CI run 41342540379 (`dba1dff9`): M-1, the fixtures, the measured rule, FR-102a, the four replay tests, roadmap FR-123 (b) and the status line.
