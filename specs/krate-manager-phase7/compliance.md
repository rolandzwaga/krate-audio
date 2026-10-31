# Krate Manager Phase 7 (krate-manager-phase7) - Compliance Report

**Overall status: COMPLETE_PENDING_CI** (table: 86 pass, 11 pending-ci, 0 fail; main-loop close lane green 2026-10-31)

## Honest gap list (open items first)

1. Eleven items are PENDING-CI (list below). The local Windows side of each is green.
2. SC-001's release build was closed by the main-loop close lane, evidence/close-lane.log (2026-10-31T22:47:37Z, wall=157.0s, 38 GREEN steps, last line `CLOSE LANE GREEN`): B2 `tauri build ... Krate Manager_0.1.0_x64-setup.exe (2.51 MiB)` exit=0 wall=102.5s; B3 `0 warning lines in the release build`; B7 release binary 7,850,496 bytes, +200,192 (+2.6 %) over the Phase 6 base.
3. The first close-lane run (22:42:53Z) was red on two rows, both fixed in `evidence/close-lane.js` by the main loop:
   - B1 wanted `233 cargo + 29 vitest` but the step printed `234`: the remediation step split SC-037 (`slow_moving_download_is_not_an_error`), so `cargo.all` is 224, not 223. The expected notice is now 234; the per-OS counts below are corrected (224 all; Windows 234, macOS 252, Linux 249).
   - A1i (SC-024) read `delta=+36.30s` from A1e's cargo test, which runs beside the WSL lane and node --test (core lib 31.98 s under that load). SC-024 is a wall-clock measurement, so A1i now re-runs `cargo test --workspace --locked` after every A lane has finished: `Phase 7 core lib=10.24s injection=6.45s config_guard=0.53s sum=17.22s; delta=+7.84s (want < 20 s)`.
4. No other item is fail or partial.

## Compliance table

Rows quote the verifier evidence. Where a row says "Ran ok", the cited test ran in `cargo test --workspace --locked` (core lib 193/194 passed, 0 failed).

| ID | Verdict | Evidence |
|---|---|---|
| FR-001 | pass | download.rs:41-51 DownloadRequest has private fields incl. `running: Option<OsVersion>` and `#[cfg(test)] fetch_url`. request_for (753-784) resolves with `version::resolve_native(entry, running)` and builds a request only on `Availability::Available`. UnknownPlugin / Unavailable cases covered. `request_for_cases` ran ok. |
| FR-002 | pass | download.rs:100-103 `fetch_target()` returns `self.asset.url.as_str()` outside cfg(test). Guard injection.rs:2885-2950 reports any http(s) literal in download.rs / verify.rs; `download_and_verify_hold_no_url_or_whole_read` ran ok (injection 31 passed). |
| FR-003 | pass | download.rs:122-156 `staged_paths` requires 64-hex hash, one Component::Normal, strict descendant of downloads. `check_sandboxed` at 1370. `staged_paths_cases` ran ok. |
| FR-004 | pending-ci | Windows: FsStagingIo uses only create_dir_all, append/truncate, write_all, sync_all, rename, remove; `writes_nothing_outside_downloads` and `verified_file_has_no_zone_identifier_windows` ran ok. macOS `verified_file_has_no_quarantine_macos` (download_tests.rs:1332-1334, /usr/bin/xattr) runs only on `check (macos-latest)`. |
| FR-005 | pending-ci | prune (download.rs:676-726) keeps only live `<hex>.part` / `<hex>`. Windows junction removal proven by `staged_links_are_not_followed_windows` case (1); `prune_keeps_only_live_artifacts`, `prune_spares_in_flight_artifact`, `two_artifacts_reach_one_consumer` ran ok. Unix symlink branch proven only by `staged_symlinks_are_not_followed` on macOS and Linux CI. |
| FR-006 | pass | Static IN_FLIGHT registry with Drop guard (646-671); obtain_body enters it first, returns Busy on duplicate. `same_artifact_twice_is_busy`, `busy_registry_is_released_on_error`, `crash_after_each_step_recovers` ran ok. |
| FR-010 | pass | http.rs AgentPurpose::Download { stall }; agent_config 634-663; get_streamed at 1264; StreamLimits / ResponseHead / HeadAction / StreamEnd / StreamError match spec. `streamed_body_cases`, `streamed_policy_cases`, `stall_and_deadline_are_named` ran ok. HttpError unchanged. |
| FR-011 | pending-ci | get_streamed reuses prepare, static_route, pac_route, hop_rule; PAC bound `start + min(total_timeout, limits.deadline)`. PAC half is non-Windows: proven only by `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix` on macOS and Linux CI. Phase 5 tests unchanged and green. |
| FR-012 | pass | http.rs:1030 `final_response<E: ChainError>`; run_chain keeps 7 parameters; Phase 5 http::tests pass unchanged. |
| FR-013 | pass | Only extra header is `Range bytes=<from>-`; classify_head (796-835) follows the table; 416 handled at 1196-1198. `classify_head_table`, `range_edge_cases` ran ok. |
| FR-014 | pass | TooLarge removes .part and gives E_DOWNLOAD_SIZE; Size/Encoding verdict removes it; short body keeps partial. `interrupted_download_resumes`, `rejections_never_reach_next_stage` ran ok. |
| FR-015 | pass | budget = attempts; refuse() immediate full request without backoff; hash re-download adds one unit once. `retries_are_bounded`, `range_edge_cases` ran ok. |
| FR-016 | pass | settle (1153-1208) and mapped() (1213-1238) map all stream/http errors; Target carried on transfer variants. `non_retryable_transport_errors`, `stall_and_deadline_are_named`, `redirect_target_never_logged` ran ok. Path{reason} carries no Target (unreachable through obtain_and). |
| FR-017 | pass | retry_or_fail now checks the token at download.rs:1265-1270, before the .part length read, the budget check and the Retrying emit. New case (g) in `cancellation_cases` (download_tests.rs:3186-3219): E_DOWNLOAD_CANCELLED, 1 request, `retryings(&run.events).is_empty()`, 0 nexts, .part kept at 100,000 bytes, elapsed < 1 s. Ran ok (F:/tmp/gate/km7v/test.log). Edge noted: a non-retryable error after the token is set returns that error, not Cancelled (download.rs:1262-1264); still no Retrying. |
| FR-018 | pass | production(): attempts 4, backoff [1s,2s,4s], stall 30 s, attempt_deadline 3600 s; no overall Instant budget. `policy_has_no_overall_budget` ran ok. |
| FR-020 | pass | Progress enum (download.rs:228-251) with serde tag "kind"; RestartReason variants; total always feed size. `retrying_events_are_ordered_and_serialize` ran ok. |
| FR-021 | pass | Started / Verifying / Verified ordering; at most two Verifying. `progress_is_monotonic_and_ends_at_size` ran ok; assert_progress_invariants used in multiple tests. |
| FR-022 | pass | Meter::wrote emits Bytes at >= 256 KiB step or on_disk == size; high_water kept across restarts. Same test ran ok. |
| FR-023 | pass | download.rs:1288-1293 Retrying emitted only after retryable, not-cancelled (1266-1270), budget-left checks, before the wait. cancellation_cases (d) 1 Retrying before the wait; (g) 0 Retrying. `retrying_events_are_ordered_and_serialize`, `retries_are_bounded`, `cancellation_cases` ran ok. |
| FR-030 | pass | verify.rs:165-169 verify_file pub(crate); Expected pub(crate); single production caller Job::verify (download.rs:954-960). `verified_artifact_has_no_public_constructor` ran ok. |
| FR-031 | pass | verify.rs:173-186 symlink_metadata, non-regular gives Io{InvalidInput}, size check before any read. `verify_steps_stop_at_first_failure` ran ok. |
| FR-032 | pass | parse_minisig_text then Signature::decode; failures give SignatureText. Tests ran ok. |
| FR-033 | pass | verify.rs:201-205 `key.public.verify_stream`; UnexpectedKeyId gives KeyId before any read; `public` private with no accessor. |
| FR-034 | pass | One 64 KiB heap buffer feeding Sha256 and StreamVerifier; no whole-file read. `download_and_verify_hold_no_url_or_whole_read` ran ok. |
| FR-035 | pass | Digest mismatch gives Sha256{expected, found} lower hex. `rejections_never_reach_next_stage` ran ok. |
| FR-036 | pass | `verifier.finalize().map_err(...)` after SHA step (verify.rs:245). `kat_verifies_and_one_byte_changes_fail` ran ok. |
| FR-037 | pass | Trusted comment parse failure gives TrustedComment{code, detail}. `invalid_trusted_comment_is_named` ran ok. |
| FR-038 | pass | verify.rs:261-274 binding order name, version, platform, asset; Binding rows h-k at download_tests.rs:1127-1160 and verify_tests.rs:471. |
| FR-039 | pass | VerifiedArtifact: 8 private fields, Clone+Debug only; compile_fail doctests (verify.rs:92, :103) pass; rename and relocate before next. |
| FR-040 | pass | RELEASE_KEY_TEXT include_str! of tools/feed-contract/keys/krate-signing.pub (114 bytes); SigningKey derives Clone/Debug/PartialEq/Eq. `signing_key_text_cases` ran ok. |
| FR-041 | pass | context.rs:59 `pub signing_key: SigningKey`; src-tauri/src/lib.rs:57 passes `SigningKey::release()`; config_guard.rs four-argument check at :876; test contexts use kat_signing_key(). config_guard: 9 passed. |
| FR-042 | pass | verify_tests.rs:73-101 `release_key_is_the_committed_key_and_not_a_test_key`: key id 9FEB36F9114273BC equals the untrusted-comment id and differs from both KAT keys. Ran ok. |
| FR-043 | pass | injection.rs:2479-2626 verified_artifact_findings and minting_route_findings; self-test `verified_artifact_scanner_reports_plants` (4345). Both ran ok. |
| FR-045 | pass | DownloadEnv Clone+Copy; obtain_and signature per spec; obtain_body order exclusion, prune, cached, transfer, verify, rename, next once. `too_many_arguments_is_never_suppressed` ran ok; clippy -D warnings clean. |
| FR-046 | pass | Job::cached (963-998) probes kind_of(<hex>) first; hit gives Obtained{cached:true}. `cached_verified_file_is_reverified`, `staged_links_are_not_followed_windows` case (2) ran ok. |
| FR-047 | pass | Any other verify error removes .part and returns Verify without next; one Sha256 redo (1447-1460). `range_edge_cases` asserts HashMismatchAfterResume (download_tests.rs:1668), ran ok. |
| FR-048 | pass | Row 'i: bound to version 1.2.2' expects field "version" (download_tests.rs:1139-1147), ran ok. |
| FR-049 | pass | download.rs:1474-1509 discard re-derives paths, InFlight guard gives DiscardBusy, remove_dir_all on `<hex>/` only. `discard_cases` ran ok. |
| FR-050 | pending-ci | StagingIo trait and FsStagingIo (download.rs:547-643); read_dir only in read_dir_entries. Windows remove_link proven by `staged_links_are_not_followed_windows`. Unix remove_link (729-732) proven only by `staged_symlinks_are_not_followed` on macOS and Linux CI. |
| FR-051 | pass | StagingCode::of mapping (348-356); write/sync/rename failure handling. `staging_errors_are_named`, `crash_after_each_step_recovers` ran ok. Real read-only dir check (`readonly_staging_dir_unix`, SC-012) is CI-only. |
| FR-052 | pass | DownloadError code() (441-464) covers all codes; VerifyError (verify.rs:310-350) covers IO, SIZE, SIGNATURE_TEXT, KEY_ID, SHA256, SIGNATURE, TRUSTED_COMMENT, TC_BINDING, KEY_TEXT. |
| FR-053 | pass | One info! on success, one warn! on failure at target `krate_manager_core::download`; transport details scrubbed. `redirect_target_never_logged` ran ok. |
| FR-060 | pass | command_request / run_command / for_command (download.rs:1512-1573); commands.rs download_plugin with Channel<Progress>, spawn_blocking. `command_flow_cases` ran ok. |
| FR-061 | pass | build.rs six commands, main.json six permissions ending `allow-download-plugin`, lib.rs handler list. config_guard: 9 passed, 0 failed. tauri.conf.json unmodified. |
| FR-062 | pass | DownloadSummary {name, version, platform, asset, size, resumed}, no path/URL/hash; keys asserted at download_tests.rs:2618. |
| FR-063 | pass | `git status --short apps/krate-manager/src` printed nothing. |
| FR-064 | pass | channel_progress (download.rs:1576-1583) drops send error, names no Tauri type. `channel_adapter_forwards_in_order` ran ok. |
| FR-065 | pass | VersionChanged returned before obtain_and (download.rs:1531-1537); `command_flow_cases` (e) asserts no prune, I/O, event or asset request. |
| FR-066 | pass | download_plugin is a plain #[tauri::command], permanently registered; config_guard pins the six-command set. |
| FR-080 | pass | Cargo.toml pins minisign 0.10.0, minisign-verify 0.3.0, sha2 0.11.1; CI step's cargo tree run locally: windows 88 crates, linux 98, aarch64-apple-darwin 108, 0 excluded hits each. |
| FR-081 | pass | config_guard.rs signing_crate_findings walks Cargo.lock from normal and build roots; `core_has_no_signing_crate`, `signing_crate_scan_reports_plants` ran ok. |
| FR-082 | pass | injection.rs:2843-2950 confinement and download/verify guards; `verify_crates_are_confined`, `download_and_verify_hold_no_url_or_whole_read` and plant self-tests ran ok. |
| FR-083 | pass | test_signer.rs in-memory TestSigner, `#[cfg(test)]` (lib.rs:23-24); `no_secret_key_in_app_tree`, `no_secret_key_scanner_reports_plants` ran ok. |
| FR-084 | pass | Reply::Ranged, Script, Paced in fixture_server.rs. `ranged_cases`, `script_advances_per_request`, `paced_delivers_all_bytes_in_chunks` ran ok. |
| FR-085 | pass | verify_tests.rs:168-173 recomputes KAT_SHA256 5b980e5b...62890; feeds on https://assets.example.invalid/. `fixture_url_seam_cases`, `fixture_url_seam_is_test_only` ran ok. |
| FR-086 | pass | required-tests.json: 58 names added to cargo.all; windows +3, macos +4, linux +3. Every cargo.all and windows entry appears 'ok' in the local log. manager-ci-shape.test.js: 26 pass, 0 fail. |
| FR-087 | pass | PHASE7_TEST_FILES / PHASE7_HOST_NEEDLES guard (injection.rs:183-197, 2801-2840); longest new sleep 100 ms; core lib 193 tests in 11.12 s. `phase7_tests_hold_no_real_host` ran ok. |
| FR-088 | pass | roster.rs includes download.rs and verify.rs in its scans. roster: 4 passed, 0 failed. |
| FR-090 | pass | `git status --short -- plugins dsp cmake extern .github tools specs` shows only `M specs/INDEX.md`, `M tools/feed-contract/tests/manager-ci-shape.test.js`, `?? specs/krate-manager-phase7/`. |
| FR-091 | pass | Outside apps/krate-manager only specs/krate-manager-phase7/**, specs/INDEX.md (`specs/INDEX.md is up to date.`) and manager-ci-shape.test.js (26/26). Roadmap untouched. |
| SC-001 | pass | fmt exit 0; workspace clippy -D warnings exit 0 (0 warnings); dev-feed clippy exit 0; clippy-guard `GUARD rc=101 disallowed=3 other-errors=0`; `cargo test --workspace --locked` exit 0 (core `194 passed; 0 failed ... 10.88s`, config_guard 9, injection 31, roster 4, stub 1, doctests 1 and 6); typecheck/lint exit 0, vitest `Tests 30 passed (30)`; `lint-manager-version: ok, version 0.1.0 everywhere`; shape test 26 pass. Suppression scan: only plant strings in injection.rs self-tests. Release build (main-loop close lane, evidence/close-lane.log (2026-10-31T22:47:37Z, wall=157.0s, 38 GREEN steps, last line `CLOSE LANE GREEN`)): B2 `Krate Manager_0.1.0_x64-setup.exe (2.51 MiB)` exit=0; B3 `0 warning lines in the release build`; B5 `roster absent (Windows)::9 names, 0 hits in 4 files`; A1j `no added allow( / expect( attribute, @ts-ignore or eslint-disable`. CI part PENDING-CI on all three legs. |
| SC-002 | pass | `good_artifact_verifies ... ok` (alone: 1 passed, 0.15s). CI: PENDING-CI all legs. |
| SC-003 | pass | `rejections_never_reach_next_stage ... ok`; 12 rows, each extract==0 && install==0, no verified file, no .part. CI: PENDING-CI all. |
| SC-004 | pass | `kat_verifies_and_one_byte_changes_fail`, `kat_secret_message_verifies`, `invalid_trusted_comment_is_named` ok; `sha256sum` -> `5b980e5b8ae7811a064966a1ec1380df016081a12bdeaf643486026007b62890`, equal to KAT README. |
| SC-005 | pass | `progress_is_monotonic_and_ends_at_size ... ok` (download_tests.rs:948-985). |
| SC-006 | pass | `interrupted_download_resumes` 0.18s, `partial_from_a_previous_run_resumes` 0.12s, `stall_resumes` 0.33s vs < 5 s. |
| SC-007 | pass | `ignored_range_falls_back_to_full`, `truncate_failure_after_ignored_range` ok. |
| SC-008 | pass | `range_edge_cases`, `classify_head_table` ok. |
| SC-009 | pass | `retries_are_bounded`, `stall_and_deadline_are_named`, `non_retryable_transport_errors` ok; whole test 1.80s. Minor: e1/e3/e4 do not count `next` (nexts: 0 at :1719) but assert Err. |
| SC-010 | pending-ci | `streamed_policy_cases ... ok` locally; `streamed_pac_fetch_keeps_the_feed_bound_unix` is #[cfg(not(windows))]. |
| SC-011 | pass | `staging_errors_are_named`, `crash_after_each_step_recovers` ok. |
| SC-012 | pending-ci | `readonly_staging_dir_unix` is #[cfg(unix)] (download_tests.rs:2107-2108); not run on Windows. |
| SC-013 | pass | `prune_skips_a_file_open_without_share_delete_windows ... ok`; all 10 cargo.windows names ok locally. CI windows leg PENDING-CI. |
| SC-014 | pass | `prune_keeps_only_live_artifacts`, `same_artifact_twice_is_busy`, `busy_registry_is_released_on_error`, `prune_spares_in_flight_artifact` ok. |
| SC-015 | pass | compile_fail doctests (verify.rs:92, :103) `... ok`; guard pair ok. |
| SC-016 | pass | `release_key_is_the_committed_key_and_not_a_test_key ... ok`; id 9FEB36F9114273BC differs from 440FA75168B852F7, 63861317F7D60239. |
| SC-017 | pass | `no_secret_key_in_app_tree`, `no_secret_key_scanner_reports_plants`, `phase7_tests_hold_no_real_host`, `phase7_host_scanner_reports_plants` ok (injection 31 passed). |
| SC-018 | pending-ci | All local guards green; CI dependency-tree step reproduced on Windows: `n=88 hits=[]`. macOS and Linux notices outstanding. |
| SC-019 | pass | build.rs six commands, main.json six permissions; `real_src_tauri_config_passes_the_guard`, `planted_config_changes_fail_the_guard`, `request_for_cases` ok. |
| SC-020 | pass | `ranged_cases`, `script_advances_per_request`, `paced_delivers_all_bytes_in_chunks` plus existing fixture_server tests ok. |
| SC-021 | pending-ci | macOS only. cargo.macos has 28 entries, cargo.all 224; all 224 ok locally. Needs `check (macos-latest)` notice `252 cargo + 29 vitest` (224 / 28). |
| SC-022 | pending-ci | Linux only. cargo.linux 25 entries. No WSL lane output recorded in evidence/ (only wsl-lane.js exists). Needs `check (ubuntu-24.04)` notice `249 cargo + 29 vitest` (224 / 25). |
| SC-023 | pending-ci | Windows side ok locally (10 windows names + 224 all; close-lane B1 `234 cargo + 29 vitest`). Needs `check (windows-latest)` notice `234 cargo + 29 vitest` (224 / 10). |
| SC-024 | pass | Close-lane A1i, a quiet `cargo test --workspace --locked` after every A lane (evidence/close-lane.log (2026-10-31T22:47:37Z, wall=157.0s, 38 GREEN steps, last line `CLOSE LANE GREEN`)): `Phase 6 core lib=2.42s injection=6.11s config_guard=0.85s sum=9.38s; Phase 7 core lib=10.24s injection=6.45s config_guard=0.53s sum=17.22s; delta=+7.84s (want < 20 s)`. A main-loop run alone gave 10.98 + 0.59 + 6.54 = 18.11 s (+8.73 s). |
| SC-025 | pass | `scope-check: 39 paths (30 under apps/krate-manager/, 9 outside), 0 roadmap hunks, 4 shape-test hunks: OK (base 2f3c93d7)`, exit 0. |
| SC-026 | pass | `two_artifacts_reach_one_consumer ... ok`. |
| SC-027 | pass | `channel_adapter_forwards_in_order ... ok`. |
| SC-028 | pass | `command_flow_cases ... ok`. |
| SC-029 | pass | `cached_verified_file_is_reverified ... ok`. |
| SC-030 | pass | `redirect_target_never_logged ... ok`. |
| SC-031 | pending-ci | Windows parts ok (`writes_nothing_outside_downloads`, `verified_file_has_no_zone_identifier_windows`, `staged_links_are_not_followed_windows`). `verified_file_has_no_quarantine_macos` (macOS) and `staged_symlinks_are_not_followed` (unix) do not run here. |
| SC-032 | pass | `fixture_url_seam_cases`, `fixture_url_seam_is_test_only`, `fixture_url_seam_scanner_reports_plants` ok. |
| SC-033 | pass | `staged_paths_cases ... ok`. |
| SC-034 | pass | `cancellation_cases ... ok`; alone 0.43s vs < 3 s. |
| SC-035 | pass | `discard_cases ... ok`. |
| SC-036 | pass | `retrying_events_are_ordered_and_serialize ... ok`. |
| SC-037 | pass | `policy_has_no_overall_budget` alone 1.59s / 1.60s / 1.61s; `slow_moving_download_is_not_an_error` 1.50s x3; both under the 3 s bar and 60 % of it (1.8 s). |
| CC-system | pass | Greps of new/changed files found only two test-only hits (`/usr/bin/xattr -l` on a temp file; `cmd /C mklink /J` inside a TempDir). Roots go through sandboxed `fresh_temp_roots()`; fetches go through the loopback-only `with_fixture_url` seam; no helper/elevation reference. config_guard 9 / injection 31 passed. |
| CC-verify | pass | One download path: transfer to `.part`, `verify`, only then rename and `next`; every Err discards and returns Verify without next; cached path verifies before next. 43 passed, 0 failed (10.54s). |
| CC-zipslip | pass | Phase 7 does no extraction (no extract/tar/zip/unpack hits); `staged_paths` guard tested by `staged_paths_cases`. Archive-entry guard is Phase 8's. |
| CC-helper | pass | No change under crates/helper (last commit ac307f7e); stub accepts zero operations, exit 2. |
| CC-capabilities | pass | main.json grants exactly six permissions incl. allow-download-plugin; no core:default, fs, shell, http or opener; config_guard 9 passed. |
| CC-signing | pass | Verifies with the minisign public key 9FEB36F9114273BC only; test keys generated in memory; minisign is dev-dependency only; no private key added; bundling unchanged. |
| CC-warnings | pass | clippy -D warnings exit 0 (all-targets and all-features); `cargo fmt --check` exit 0; tsc exit 0; eslint --max-warnings 0 exit 0; no new allow/expect/ts-ignore. |
| CC-lints | pass | `lint-serial-test-runs: clean (64 files)`; no C++ changed so check-portability not required; Rust tests ran in parallel. |
| CC-feed-compat | pass | No change to .github, plugins, tools/feed-contract/src or schema; update_checker.cpp:215-227 still parses `plugins.<Name>.version`. |

## PENDING-CI list

The user pushes; nothing was pushed here. Each item needs the named job and assertion.

| Item | CI job | Assertion that proves it |
|---|---|---|
| FR-004, SC-031 (macOS part) | `check (macos-latest)` | `verified_file_has_no_quarantine_macos ... ok` counted by `required tests ran (macOS)`, no `::error` line |
| FR-005, FR-050, SC-031 (unix part) | `check (macos-latest)`, `check (ubuntu-24.04)` | `download::tests::staged_symlinks_are_not_followed ... ok` |
| FR-011, SC-010 | `check (macos-latest)`, `check (ubuntu-24.04)` | `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix ... ok`; `streamed_policy_cases` on all three legs |
| SC-012 | `check (macos-latest)`, `check (ubuntu-24.04)` | `download::tests::readonly_staging_dir_unix ... ok` |
| SC-018 | all three legs | `::notice title=core dependency tree (macOS\|Linux)::N crates, none excluded`; guard tests pass |
| SC-021 | `check (macos-latest)` | `required tests ran` notice `252 cargo + 29 vitest` (224 / 28), 0 failed |
| SC-022 | `check (ubuntu-24.04)` | notice `249 cargo + 29 vitest` (224 / 25), 0 failed |
| SC-023 | `check (windows-latest)` | `required tests ran (Windows)` notice `234 cargo + 29 vitest` (224 / 10), 0 failed |
| SC-001 (CI part) | all three legs | green `check` jobs |

All other items marked "CI: PENDING-CI (all)" in their evidence need only a green `cargo test` step on the three legs.

## Implementation notes (deviations reported by task agents)

- **T001:** Eleven extra passages in spec.md were aligned to the applied SI rows (D2 stall, Content-Encoding, SC-021/023 OS lists, SC-031, D5, FR-052 DiscardBusy). Rule 12's CRLF counts were wrong; spec/plan/tasks are LF. The Existing-pieces fixture_server row still omits Reply::Paced.
- **T002:** Only four AppContext literals needed `signing_key`; the other five use `..ctx`. Rule 12 CRLF counts are unreliable in Git Bash; EOL-aware edits kept real endings. FaultyStagingIo keeps counters in a private `Arc<OpCounters>`. Windows uninstall key written as `{N:08X}-0000-4000-8000-{N:012X}`. vitest ran 30 tests, not 29.
- **T003:** `streamed_pac_fetch_keeps_the_feed_bound_unix` never compiled or run on Windows. The stall case uses `limits.stall = 100 ms` as the per-read bound.
- **T004:** `ContractError::Unsupported` is mapped to `TrustedComment{code:"E_SCHEMA_UNSUPPORTED"}`. `verify_steps_stop_at_first_failure` has three extra assertions.
- **T005:** Found and fixed an `InFlight::enter` self-deadlock in its own code. assert_progress_invariants relaxed for CachedInvalid restarts and empty event lists. The sink splits chunks at PROGRESS_STEP boundaries. Redirect-chain routes /r1../r5 requested once, /r6 zero times. Prune `read_dir_entries` failure logged at warn and the download continues. Hash-mismatch redo removes .part strictly. A `discard` staging error carries `Target { url: "" }`.
- **T006:** The `Reply::Script` arm in `answer` is `unreachable!` (a nested Script is unsupported). Paced chunk size 0 is treated as 1.
- **T007:** The arity guard holds plant strings inline, so this phase's diff contains `#[expect(clippy::too_many_arguments` inside string literals. No recorded red for the real-tree tests while scanners were stubs.
- **T008:** The signing-crate walk also reads `[dependencies.<name>]` headers, and reports missing lock roots and unmatched dependency strings. The roster additions have no recorded red.
- **T009:** A1j also scans untracked files. The B rows wait for all A rows except A1i, so a timing red does not cost the build. A9 is only the `todo!(` / `unimplemented!(` grep.
- **T010:** required-tests.json and manager-ci-shape.test.js are LF (Rule 12 said CRLF). Counts: all 223, windows 10, macos 28, linux 25, vitest 29. The `required tests ran` step run locally printed `::notice title=required tests ran (Windows)::233 cargo + 29 vitest`.
- **T011:** The Rule 2 grep for `#[expect|allow](` is not literally empty. It has 4 hits (injection.rs:4788, 4798, 4838, 4839), all inside quoted strings in `too_many_arguments_is_never_suppressed`. A stricter attribute-position grep is empty. Git warns "LF will be replaced by CRLF" for 16 modified files. The reviewer should accept or reject this reading.
- **Late gap closure:** FR-017 cancellation was fixed (check at download.rs:1265-1270 plus test case (g)). SC-037 was split into `policy_has_no_overall_budget` and `slow_moving_download_is_not_an_error`.

## Remaining steps for the human loop

1. Review the diff, especially the T011 Rule 2 reading and the line-ending warnings.
2. Done 2026-10-31: `node specs/krate-manager-phase7/evidence/close-lane.js`, `CLOSE LANE GREEN` (evidence/close-lane.log), and the roadmap Phase 7 status line.
3. Commit (no amend).
4. The user pushes; CI then proves the PENDING-CI items.
