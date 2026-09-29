# Integration gates on the final (L5) tree — main-loop record (2026-09-28)

| task | gate | evidence | result |
|---|---|---|---|
| T031 | per-push regression, all 8 suites, filter `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` | `t031_regression_<suite>.log`, build `t031_build_status.txt` (exit 0, 0 warnings) | dsp_core 1588248/565, dsp_primitives 4561487/1489, dsp_processors 10697080/3311, dsp_systems 6061179/1412 (includes the re-pinned `VoragoEngine_GhostExtensionWiring`), dsp_effects 115009/495, vorago 3992093/67, seraphis 444660/109, shared 6420/460 — all "All tests passed", EXIT 0 |
| T032 | SC-015 no Seraphis file; SC-017 no parameter-surface file (FR-026–FR-028) | `after_scope_odr.log` (`git diff --name-only 6cef994b`) | 0 Seraphis files; 0 of plugin_ids.h / param_table_expected.h / param_routes.h / *_params.h; production edits confined to `vorago_voice.h` + `vorago_engine.h` (plus the T028 fixture and the committed perf-gate fix `e0beed68`) |
| T033 | FR-024 ODR re-sweep | `after_scope_odr.log` | `VoragoEcosystemLeverProbe`: one forward declaration (`vorago_voice.h:173`) + one definition (`vorago_ecosystem_lever_test.cpp:60`); no other new class name |
| T034 | clang-tidy dsp + vorago | `after_clang_tidy_dsp.log`, `after_clang_tidy_vorago.log` | first pass 2 + 1 warnings (enum base, smart-pointer `reset()` ambiguity, nested conditional) — fixed; re-run: dsp 372 files 0/0, vorago 47 files 0/0 |
| T035 | pluginval strictness 5 on the final plugin build | `after_pluginval.log` | plugin build exit 0, 0 warnings; "Completed tests in pluginval / Restoring default layout", EXIT 0, 0 FAILED |
| T036 | check-portability + `wsl --shutdown` | `after_portability.log` | first attempt skipped on a cold WSL boot; re-run "all clear -- 5 compiled." (both lever TUs, the probe TU, and the two perf-fix TUs), wsl-shutdown=0 |
| T029 | `[long]` suites | `after_long.log`, `after_vorago_long.log` | pending (running) |
| T030 | after-CPU, pinned perf roster alone after ≥ 15 min idle | `after_cpu.log` | pending (after T029) |
| T027 | Gate 2 tables, t0 pinned | `after_table_default.log`, `after_table_lifemax.log` | pending (running) |
| T028 | final Gate-1 pair + fingerprint re-pin | `after_gate1m_default.log` (0.759 PASS), `after_gate1m_lifemax.log` (0.742 PASS), `t028_ghost_fingerprint_*.log` | done |

Note: the three clang-tidy fixes (test files only) post-date the T031 regression and the T029/T027 runs in flight; both test binaries are rebuilt and the touched cases (`VoragoVoice_EcosystemLeverReset`, a short probe run) re-run once those binaries are free.

Tidy-fix verification, plugin side: `tidyfix_vorago_build_status.txt` (exit 0, 0 warnings); `tidyfix_probe_exercise.log` exercises both yardstick paths of the rewritten if-chain — unpinned diagnostic mode (t0 = t0on 4.4218, "All tests passed (63773 assertions in 1 test case)") and the pinned gate mode (t0 = 4.0438, GATE1 1.125 PASS, "All tests passed (159432 assertions in 1 test case)"). The systems-side rebuild and the `VoragoVoice_EcosystemLeverReset` re-run follow T029 (the binary is in use by the long suite).

## T029 [long] suites (10:55-14:58)

- `after_vorago_long.log`: "All tests passed (1632462 assertions in 3 test cases)", EXIT 0 (5 min).
- `after_long.log` (`dsp_systems_tests "[long]" -d yes`, 3 h 58 min; OvernightSoak 7596.9 s, ConfigurationFuzz 1783.9 s, EcosystemEngine_HostileFuzz 1529.7 s, VoragoMacro_SweepAxes 1300.1 s): "test cases: 42 | 40 passed | 2 failed", EXIT 2. The two reds:
  1. `VoragoVoice_EcosystemRouting` (Phase 10 SC-019 clause 1, `vorago_voice_longrun_test.cpp:706`): "Resonator family: [0..11] 0.45 vs base 0.5" at control step 0. The test carries `kShippedPeakWakeBase = 0.50f` / `kShippedLoopWakeBase = 0.50f` as literals (`:181-182`, because the voice exposes no getter for them); Phase 13b FR-018 retuned both to 0.45 → **FR-031(b): old voicing pinned as data; surfaced for ruling** before any edit.
  2. `VoragoMacro_NoZipper` (Phase 10 SC-010, `vorago_macro_test.cpp:1587`): macro index 3 (Movement) ramp max-delta 0.00404388 vs 1.5 × reference 0.00249609 = 0.00374413 (ratio 1.62); macros 9-11 at 0.95 / 0.95 / 1.17. A behavioural bound → **FR-031(a) defect, owned**; isolation by experiment (lever spans) follows.

## Zipper isolation and routing fix (15:05-15:10)

- `after_routing_named_bases.log`: `VoragoVoice_EcosystemRouting` with `kShippedPeakWakeBase/kShippedLoopWakeBase` = `VoragoVoice::kPeakWakeBase/kLoopWakeBase` (now public): "All tests passed (23 assertions in 1 test case)".
- `zipper_e1_wander0.log` (E1: `kFreqWanderLeverSpanSemis` 6 → 0, everything else shipped): `VoragoMacro_NoZipper` macro 3 (Movement) ratio **0.981** (shipped tree: 1.620) — "All tests passed (24 assertions in 1 test case)". Cause isolated to the freq-wander lever: its depth steps once per 64-sample chunk, up to 6 st × 64 / (48000 × 0.05 s) = 0.16 st per step at the 50 ms slew.

## Zipper fix — wander lever span sweep (15:11-15:22)

| kFreqWanderLeverSpanSemis | Movement ramp ratio (bound 1.5) | log |
|---|---|---|
| 6 (L4 step 2) | 1.6200 FAIL | `after_long.log` |
| 6, slew 0.5 s | 1.6204 FAIL — the slew is not the mechanism | `zipper_e2_slew500ms.log` |
| 4 | 1.5474 FAIL | `zipper_e3_span4.0.log` |
| 3 | **1.3793 PASS** | `zipper_e3_span3.0.log` |
| 2 | 1.1717 PASS | `zipper_e3_span2.0.log` |
| 0 | 0.9810 | `zipper_e1_wander0.log` |

**Shipped: 3 st** (the most colony-driven peak motion that clears the bound; other macros 0.85-1.21). The shipped tree therefore changes after the Gate-1/Gate-2/T029/T031 runs above; every gate is re-run on the final tree below ("final_*" logs).

## Final-tree [long] run (17:31-21:58; `final_long.log`, `final_vorago_long.log`)

- `final_vorago_long.log`: "All tests passed (1632462 assertions in 3 test cases)".
- `final_long.log`: "test cases: 42 | 41 passed | 1 failed" (OvernightSoak 9203.9 s, HostileFuzz 1948.3 s, ConfigurationFuzz 1869.3 s, SweepAxes 1198.6 s). The one red: `VoragoMacro_SweepAxes` (Phase 10 SC-008 monotonicity, `vorago_macro_test.cpp:1318`): "Movement per-band total variation rho=0.866667 endpoint=0.329173 threshold=0.2" — `CHECK((result.rho * expectedRhoSign(row.kind)) >= 0.9)` fails by one adjacent inversion over the five macro steps; the endpoint clause passes. On the 10:55 run (wander span 6 st, base 0.75 st) this case passed; the span 6 → 3 change (SC-010 zipper fix) is what changed the Movement row's ordering. Behavioural bound → FR-031(a), owned; fix candidate: restore `kFreqWanderBaseSemis` to Phase 10's 1.5 st (L5 lowered it to 0.75 for contrast), re-measure SweepAxes (20 min) and the zipper bound, then the audibility gates on that tree.

- Fingerprint fifth harvest verified: `final_repin_build_status.txt` (exit 0, 0 warnings), `final_ghost_fingerprint_after_repin.log` "All tests passed (27 assertions in 1 test case)"; `final_regression_dsp_systems_tests.log` re-run: "All tests passed (6061179 assertions in 1412 test cases)".
- 22:12 fix for the SweepAxes red: `kFreqWanderBaseSemis` 0.75 → 1.5 st (Phase 10's value; L5 had lowered it). Measured next on that tree: `base15_zipper.log`, `base15_sweepaxes.log`; if both pass, the audibility gates, regression and [long] suites are re-run on it (`final2_*` logs).

- clang-tidy dsp on the restored-base tree: 372 files, 0/0 (`final_clang_tidy_dsp.log`).
- `base15_zipper.log` (base 1.5 st, span 3 st): Movement ratio **1.5496 FAIL** — the zipper statistic follows the wander CEILING (base + span): 3.75 st passed (1.38), 4.0 (base 0.75 + 4 span) 1.55, 4.5 (1.5 + 3) 1.55. `base15_sweepaxes.log` pending (informational: does the base move SC-008?). Candidates queued with both bounds: base 1.5 / span 2 (ceiling 3.5) and base 1.0 / span 3 (ceiling 4.0) → `cand_b*_s*_{zipper,sweepaxes}.log`.

- `base15_sweepaxes.log` (base 1.5 / span 3): Movement rho **0.8667 FAIL** — identical to base 0.75 / span 3 (`final_long.log`), so the base does not move SC-008; the printed five-point means rise monotonically in all runs ([1.117, 1.256, 1.388, 1.431, 1.451]); rho is the MEAN over the three sweep seeds {101, 202, 303} (`vorago_macro_test.cpp:646, :1251-1254`), so 0.8667 is one seed with one inversion. Span 6 st read rho 1.0 (`after_long.log`); span 3 fails. The two Phase 10 bounds pull the wander lever in opposite directions: SC-010 (zipper) needs the wander ceiling ≤ ~3.75 st, SC-008 (Movement monotonicity) held only at span 6 so far.
- Candidate base 1.5 / span 2: zipper **1.3636 PASS** (`cand_b1.5_s2.0_zipper.log`); sweep pending.
- Candidate base 1.5 / span 2: sweep Movement rho **0.8667 FAIL** (`cand_b1.5_s2.0_sweepaxes.log`); candidate base 1.0 / span 3: zipper 1.4556 PASS, sweep rho **0.8667 FAIL** (`cand_b1.0_s3.0_*.log`). No unscaled base/span pair passes both Phase 10 bounds.

## FR-018b — rate-compensated wander lever (2026-09-28 23:28 → 2026-09-29 00:29)

Span scaled by (0.03 Hz / resonance wander rate)^k above the default rate (`VoragoVoice::wanderLeverRateComp`, `kWanderLeverRateCompExponent`); base 0.75 st / span 3 st / slew 50 ms kept. New test `VoragoVoice_EcosystemLeverRateCompensation` (SC-022).

| k | zipper (≤ 1.5×) | SweepAxes Movement rho (≥ 0.9) | endpoint (≥ 0.20) | logs |
|---|---|---|---|---|
| 1 | 0.9912 | 0.9667 | **0.1899 FAIL** | `ratecomp_zipper_routing.log`, `ratecomp_sweepaxes.log` |
| 0.5 | 1.0232 | 0.9667 (mean top pair inverted) | 0.2294 | `ratecomp_e0.5_*.log` |
| **0.75** | **1.0033** | **1.0000** | **0.2089** | `ratecomp_e0.75_*.log` |

**Shipped: k = 0.75.** Gate 1 / Gate 2 run at 0.03 Hz where the factor is 1, so the final-tree readings (`final2_*`, table in `ladder_record.md`) equal the 3 st tree's: Gate 1 0.656 / 0.638 PASS, Gate 2 union 2 (syncRate, selfAffinity) UNMET as ruled; regression 8 suites, pluginval, clang-tidy 0/0 ×2, portability, [long] 42 + 3 cases all green; `final2_long.log` carries SweepAxes rho 1.0000 / endpoint 0.2089.
