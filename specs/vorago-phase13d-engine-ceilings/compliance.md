# Phase 13d — Engine Ceilings: compliance

Tree: `502e5243` base → final `25207073` (+ the T058 lane logs). Every row cites a log under `artifacts/` (line numbers where the log is long); figures are copied from the logs, not paraphrased. Verdicts: ✅ met · ☑ recorded by user ruling (`artifacts/rulings.md`) · ❌ not met · ⏳ pending the named lane.

## 1. Outcome in one paragraph

Four engine levers were measured on the roster: E4 (feedback → loop wake) ships as W2 `kLoopWakeLaneGain = 2.5` under FR-010d (Feeding Loops d 5.9128 at 1× events, every loop-bus take ≥ −60 dBFS); the M2 Age row (CavernDecaySeconds −18: Erosion 4.5037) and the M10 Life row (EcosystemDepth 0.65 with the companion 900 = 0.15: Teeming 4.6265) ship under FR-010d on one combined build; E1, E6.hi / E7.hi, M5, the M4 cell and the Movement rho are recorded engine-limited by user ruling after their ladders (`rulings.md`). The confirming pass re-authored the three showcase-subset presets with one witness claim each and transcribed the E4 / M10 companions; sweep 6 reads verified primaries 39 (the three ruled) and SUBSET pairs 0.

## 2. Functional requirements

| Req | Verdict | Evidence |
|---|---|---|
| FR-001 cell roster | ✅ | The nine roster rows are the gates of T047: `final_E1.log`, `final_E4.log`, `final_E67.log` (E6.hi + E7.hi), `final_M2.log`, `final_M5.log`, `final_M4.log`, `final_M10.log` plus the Movement row in `final_sweepaxes.log`; no other cell was gated (lever_table.md rows 1–7). |
| FR-002 same instrument | ✅ | Every gate line is `Vorago_PresetPilot_PrimaryProbe` output (`  primary d … -> PASS/FAIL` in each `final_*.log`); the probe's scoring edits in 13d were read-outs only (`preset_pilot_test.cpp` LOOPBUS / BLOOM / LANES / READBACK, T009–T019 notes) and the T002 before-record reproduces sweep 5 with all deltas 0.0000 except E7.hi 0.0001 (`before_tolerance.txt`). |
| FR-003 before-record | ✅ | (i) `before_E1.log` 1.1823 / attrib 1.1035, `before_E4_stored.log` 2.9182 / 0.9851, `before_E4_1x.log` 4.4059 / 3.6649, `before_E67.log` S7 13.9144, E6.hi 1.3700, E7.hi 1.3429, `before_M2.log` 3.2191, `before_M5.log` 3.1864, `before_M4.log` 0.8745, `before_M10.log` 0.9943; repeat `before_E1_repeat.log` 1.1823 (spread 0.0000). (ii) `base_sweepaxes.log` 37 / 38 (Movement rho 0.8333 red), `base_nozipper.log` 24 / 24. (iii) `gate1_base_default.log` ratio 0.998, `gate1_base_lifemax.log` 0.629, `gate2_table_*_base.log`, `gate2_base_counting.txt`. |
| FR-004 lever-cell readouts | ✅ | E4: `loopbus take k seed s: worst10s … dBFS [PASS]` and `looplife` lines in `final_E4.log` (VORAGO_PILOT_LOOPBUS); E1: `bloom R_k … spawn / spawned / refused` lines in `final_E1.log` (VORAGO_PILOT_BLOOM); E6.hi / E7.hi: `secondary E6.hi … d 1.3827 bar 1.5 state ok conjunct ok` in `final_E67.log` with the Life-max table rows in `e67_*_lifemax.log`; M2 / M5 / M4 / M10 readbacks `im*_readback.log`, `ie67_readback.log` (T011–T018 notes). |
| FR-005 default-surface record | ✅ | Before: `default_before_13b.log` M1 RMS −26.97 dBFS, t0on 1.1895; `default_before_p0.log` P0 1.1895. After: `default_after_13b.log` M1 RMS −26.97 dBFS, t0on 1.1783; `default_after_p0.log` P0 1.1783 finite. Cause of the t0on change: the E4 loop-wake lane (the only engine change live on the default surface at Ecology Mix 0.15); §5. |
| FR-010 measure first | ✅ | Every adoption line in `rulings.md` names its ladder logs (`e4_r6_g*_gate.log`, `m2_r*_gate.log`, `m10_r*_gate.log`); every premise was read by a probe before its ruling (`ie1_bloom.log`, `ie4_loopbus_*.log`, `ie67_lanes_*.log`, `im4_member_*.log`, `im10_readback.log`; T011–T022). Ladders have ≥ 3 rungs (lever_table.md). |
| FR-010b fixed lever order | ✅ | E1 → E4 → E6/E7 → M2 → M5 → M4 → M10 in tasks.md Groups 3–9; re-open count 0 on every row (lever_table.md last column); E1 re-read after E4 (`e4_r6_g25_e1row.log` 1.1823) and after the combined build (`combo_reopen_e1.log`, `combo_reopen_e67.log`): no drop, no re-ladder. |
| FR-010c read set | ✅ | E4: roster `e4_r6_g25_roster.log`, read set over all 42 presets `e4_r6_g25_readset.log` + `e4_r6_g25_readset_<preset>.log` (86 rows, 79 verified, 6 already unverified at sweep 5, D10.1 Cathedral Void by `e4_r6_g25_d101_gesture.log`); M2 / M10 / M4: `combo_roster.log`, `combo_m2_read_smeared_horizon.log`, `combo_m10_read_*.log`, `combo_m4_read_wind_through_basalt.log` (22 rows, 0 sunk). |
| FR-010d adoption rule | ✅ | `rulings.md` ADOPTED lines: E4 W2 2.5 (1.5 and 2.0 fail the loop-bus conjunct; gate, read set, arms, −6 dB `e4_r6_g25_minus6.log` 5.9129), M2 r5b (−16 fails at 3.8430, −18 clears 4.4987 / 4.5037; −6 dB 4.5038), M10 r3b (0.45 fails at 3.4343, 0.65 clears 4.6245 / 4.6265; −6 dB 4.6259). User rulings only for stop-and-surface items and the W2 / combine authorisations. |
| FR-011 Vorago-owned code | ✅ | `git_diff_names.txt`: production edits in `vorago_voice.h`, `vorago_engine.h`, `vorago_macro_matrix.h`, `tools/vorago_preset_defs.h`, Vorago presets and Vorago / dsp-systems tests only; 0 forbidden paths (T058 check). |
| FR-012 ladder seams | ✅ | Seams + `VORAGO_PILOT_LEVER` keys for loopWakeBase, loopGainSpan, couplingSpan, partialBloomGain, partialMutationGain, parentCount, childrenPerEvent (`VoragoVoice_CeilingLeverNeutral`, `VoragoEngine_CeilingSeamContract`; T009–T010); the rebuild-laddered families (kLoopWakeLaneGain, kGhostLaneGain, kLeverInputGain, macro rows, kWanderLeverRateCompExponent) declared in plan §4 and run through `p13d_rung.sh`. |
| FR-013 E1 | ☑ | 20 rungs (`e1_*_gate.log`, lever_table.md row 1): best d 6.8712 only with parentCount 2 at attrib 6.8706 (not attributable); nothing reaches d ≥ 4.0 ∧ ≥ attrib + 1.5. User ruling 2026-10-07: engine-limited. Final `final_E1.log` 1.1823 / attrib 1.1035. |
| FR-014 E4 | ✅ | W1 rungs cleared the route bar but not the loop-bus conjunct (`e4_r1…r5*`); W2 authorised by the user; `kLoopWakeLaneGain = 2.5` adopted; `final_E4.log`: `primary d 5.9128 bar 4.0000 attribBase 3.6649 … -> PASS loopsAlive y`, `lb0 −58.83 lb1 −59.38 lb2 −54.51 lb3 −50.57 [PASS]`; FR-034 entries 2, 6, 7 applied (`fr034_surfaced.md`). |
| FR-015 E6.hi / E7.hi | ☑ | Six rebuild rungs (`e67_*_gate.log`): E6.hi ≤ 1.3803, E7.hi ≤ 1.3515, bar 1.5; Life raised changes no lane (EcosystemDepth clamped, `ie67_readback.log`). User ruling 2026-10-08: engine-limited. Final `final_E67.log` 1.3827 / 1.3034, S7 11.0318 PASS. |
| FR-016 M2 | ✅ | `combo_m2_gate.log` `primary d 4.5037 … -> PASS`, `levels: arms [y y y y] arm1@44.1k [y]`; `final_M2.log` 4.5037; row amount −18 in `vorago_macro_matrix.h`. |
| FR-017 M5 | ☑ | Nine rebuild rungs ≤ 3.2244 (`m5_r*_gate.log`); user ruling 2026-10-08: engine-limited; `final_M5.log` 3.1908. |
| FR-018 M4 + Movement rho | ☑ | M4 cell: every member at its clamp at Movement 1 (`m4_headroom`, `im4_member_*.log`), rungs ≤ 1.4940; user ruling: engine-limited (`final_M4.log` 0.8804). Rho: exponent 0.90 read 0.9333 on the pre-W2 tree but 0.8667 with W2 in (`rho_*_sweepaxes.log`; 0.75 … 1.00 all < 0.9); user ruling 2026-10-08: keep 0.75, rho engine-limited; `final_sweepaxes.log` rho 0.8667 endpoint 0.2186. |
| FR-019 M10 | ✅ | `combo_m10_gate.log` `primary d 4.6265 … -> PASS`, arms green; `final_M10.log` 4.6265; row amount 0.65 + companion 900 = 0.15 (transcribed, `t054_roster_transcribed.log` 4.6265 with no override). |
| FR-020 macro gates | ✅ / ☑ | M2 4.5037 ✅, M10 4.6265 ✅ (`final_M2.log`, `final_M10.log`); M5 3.1908, M4 0.8804 ☑ (rulings). |
| FR-021 route gates | ✅ / ☑ | E4 ✅ (`final_E4.log` d 5.9128 ≥ 4.0 and ≥ 3.6649 + 1.5); E1 ☑ (1.1823). |
| FR-021b E4 loops alive | ✅ | `final_E4.log` `loopbus take 0…3: worst10s −58.83 / −59.38 / −54.51 / −50.57 dBFS [PASS]`, `loopsAlive y`; arms 2 and 3 `[yes]` on all four `take j=` lines. |
| FR-022 secondary gates | ☑ | E6.hi 1.3827 / E7.hi 1.3034 < 1.5 (`final_E67.log`), ruled engine-limited. |
| FR-023 Movement row | ☑ | `final_sweepaxes.log` Movement rho 0.8667 (base 0.8333), endpoint 0.2186 ≥ 0.20; rho < 0.9 ruled engine-limited on the shipped tree (`rulings.md` 2026-10-08). |
| FR-024 level arms | ✅ | Every `final_*.log` carries `levels: arms [y y y y] arm1@44.1k [y]` under its verdict line (T047 note). |
| FR-024c not a limiter pass | ✅ | `final_minus6_*.log`: E4 5.9129 (loopsAlive y), M2 4.5038, M10 4.6259, S7 11.0426; E1 1.1829, M5 3.2000, M4 0.8977 (ruled cells unchanged). |
| FR-025 override strings | ✅ | lever_table.md override column: E4 `800=0.5`, M10 `900=0.15`, all others `-`; both transcribed in T054 (`tools/vorago_preset_defs.h`, `t054_tree_green.log`), `kRosterGateOverrides` emptied (`t054_roster_transcribed.log`). |
| FR-025b companions | ✅ | E4 companion alone: `e1_companion_*`-style base reads for M10 `m10_companion_alone.log` 2.2914 < 4.0; E4's `800=0.5` alone at base 4.4059 with the loop bus failing (`before_E4_1x.log`, `ie4_loopbus_1x.log`); itemised in lever_table.md. |
| FR-026 final-tree re-measure | ✅ | `build_final.log` (0 warnings) → every `final_*.log` (T047/T048 notes). |
| FR-027 no UNMET | ✅ | Every red cell is a `rulings.md` line (E1, E6.hi / E7.hi, M5, M4, Movement rho); none is marked UNMET here. |
| FR-030 verified primaries | ✅ | `final_v36_<preset>.log` 36 / 36 PASS; `p13d_readset_vs_sweep5.js` over them: 92 rows, 0 sunk; sweep 6 verified primaries 39 (`sweep6b_aggregate.log:316`). |
| FR-030b secondaries / default-state | ✅ | Same comparison: every sweep-5 S / v cell on a live preset verified (0 sunk); D10.1 Cathedral Void by the gesture case (`e4_r6_g25_d101_gesture.log` pass yes); default pseudo-preset verified in the aggregate (`sweep6b_aggregate.log`, record_42). |
| FR-031 Phase 10 bounds | ✅ / ☑ | `final_nozipper.log` 24 / 24; `final_sweepaxes.log` every base-passing assertion passes (the one FAILED is the Movement rho, red at base); soaks green in `close-lanes/long_dsp_systems_tests.log` (41 / 42; the single red at :1140 is the ruled Movement rho). |
| FR-032 13b / 13c gates | ✅ | GATE1M 0.995 / 0.630 (`final_gate1_*.log`); counting knobs still count (`final_gate2_table_*.log`: leakRate 1.376 / 0.597, syncRate 0.877 at default); 13c tests unedited except FR-034 entries (`git_diff_names.txt`, `fr034_surfaced.md`). |
| FR-033 CPU | ⏳ | T051: `cpu_final_dsp.log`, `cpu_final_vorago.log`, `cpu_ab_final.log` — runs alone after the close lanes. |
| FR-034 suites | ✅ / ☑ | `summary.txt`: eight per-push suites rc=0, long_vorago_nonsweep rc=0, long_dsp_systems_tests 41 / 42 with the ruled Movement rho the only red (`close-lanes/long_dsp_systems_tests.log:1140`); edited tests only under `fr034_surfaced.md` entries 2, 6, 7. |
| FR-035 fingerprints | ✅ | `final_ghost_harvest_1.log`, `_2.log`: `VoragoEngine_GhostExtensionWiring` green twice (27 assertions), no literal emitted → unchanged fingerprint, no re-harvest. |
| FR-036 cross-cutting | ✅ / ⏳ | `portability.log` "all clear -- 6 compiled"; clang-tidy dsp 372 / vorago 57 files 0 warnings (T058 note); `pluginval.log` strictness 5 exit 0, no FAILED. |
| FR-037 Seraphis untouched | ✅ | `git_diff_names.txt`: 0 forbidden paths (444 files, none under `plugins/seraphis/` or the named headers); `close-lanes/suite_seraphis_tests.log:331` "All tests passed (444660 assertions in 109 test cases)". |
| FR-038 ODR | ✅ | `odr_sweep.txt` (T001) and no new class added (T057 `cmake_check.txt`: no new files). |
| FR-039 surface unchanged | ✅ | `param_table_test.cpp` (5 cases, e.g. Vorago_ParamIdMap, Vorago_ParamMapping), `state_v2_test.cpp` (Vorago_StateRoundTripV2) and `state_v3_test.cpp` (6 cases, e.g. Vorago_StateRoundTripV3) ran unedited in the per-push lane: `close-lanes/suite_vorago_tests.log:250` "All tests passed (4060395 assertions in 119 test cases)"; no `plugin_ids.h` / parameter file in `git_diff_names.txt`. |
| FR-040 re-author | ✅ | `affected_set.txt` (all 42); full re-authors `reauthor_*.log` (Smeared Horizon, Hull Resonance, Haunted Colony; confirmed with four takes + −6 dB), Feeding Loops / Teeming transcriptions; the rest re-measured by `final_v36_*.log` with 0 drops > 0.01 / lost verifications. |
| FR-041 showcase subsets | ✅ | `sweep6b_aggregate.log:2199` "SUBSET pairs: 0" (before 51). |
| FR-042 shards + aggregate | ✅ | `sweep6_records_check.log` 43 OK; 42 shards `sweep6_shard_<i>.log` + `sweep6b_shard_9.log`; `sweep6b_aggregate.log` reads back the records; shard vs probe 98 cells, 0 gaps > 0.01 (T055 note). |
| FR-043 T063 re-read | ✅ | `t063_reread.md` (14 rows + out-of-roster secondaries, each with its aggregate / shard line). |
| FR-044 preset CPU | ⏳ | `cpu_presets_final.log` (T056, alone). |
| FR-050 lever table | ✅ | `artifacts/lever_table.md`: every row with mechanisms, before, every rung → reading → log, ruled value, after (final-tree section), override string, re-open count. |
| FR-051 default-render change | ✅ | §5 below. |
| FR-052 CPU delta | ⏳ | §6 below, from `cpu_ab_final.log`. |
| FR-053 rulings log | ✅ | `artifacts/rulings.md`: one line per ladder, adoption and user ruling (E1, W2, E6/E7, M5, M4 / rho, combine, Hull 0.90, FR-041 re-authors). |

## 3. Success criteria

| SC | Verdict | Evidence |
|---|---|---|
| SC-001 before-record fidelity | ✅ | `before_tolerance.txt`: every roster cell within max(0.01, 0.005 d) of sweep 5, deltas 0.0000 except E7.hi 0.0001; repeat spread 0.0000 (`before_E1_repeat.log`); `base_sweepaxes.log` Movement rho 0.8333. |
| SC-002 M2 / M4 / M5 / M10 | ✅ ✅ ☑ ☑ | `final_M2.log` 4.5037 PASS, `final_M10.log` 4.6265 PASS, arms `[y y y y] arm1@44.1k [y]`; M5 3.1908 and M4 0.8804 recorded by ruling. |
| SC-003 E1 | ☑ | `final_E1.log` `route arms: d(R_k, R_k0) 1.1823, attribBase 1.1035` → FAIL; ruling 2026-10-07. |
| SC-004 E4 loops alive | ✅ | `final_E4.log` d 5.9128 ≥ 5.1649 at 1× events (800 = 0.5 transcribed), arms 2–3 green on all four takes, loop bus −58.83 / −59.38 / −54.51 / −50.57 dBFS ≥ −60 on every 10 s window (`windows 19`), loopsAlive y. |
| SC-005 E6.hi / E7.hi | ☑ | `final_E67.log` E6.hi 1.3827, E7.hi 1.3034 (bar 1.5); no raised Life (the ladder cleared nothing); S7 11.0318 PASS; ruling 2026-10-08. |
| SC-006 Movement rho | ☑ | `final_sweepaxes.log` rho 0.8667, endpoint 0.2186 (≥ 0.20 ✅; rho < 0.9); ruling 2026-10-08 (`rho_*_sweepaxes.log` ladder). |
| SC-007 not a limiter pass | ✅ | `final_minus6_E4.log` 5.9129 loopsAlive y, `final_minus6_M2.log` 4.5038, `final_minus6_M10.log` 4.6259, `final_minus6_E67.log` S7 11.0426 (E6/E7 1.38/1.30 as ruled). |
| SC-008 verified primaries kept | ✅ | 36 / 36 `final_v36_*.log` PASS; `sweep6b_aggregate.log:316` verified primaries 39. |
| SC-009 secondaries kept | ✅ | 0 sunk in the 92-row comparison (T048 note); D10.1 by the gesture case. |
| SC-010 Phase 10 bounds | ✅ / ☑ | NoZipper 24 / 24, SweepAxes base-passing assertions pass (Movement rho ruled); soaks green (`close-lanes/long_dsp_systems_tests.log`, VoragoEngine_OvernightSoak wall clock 2.7 h at :1059). |
| SC-011 13b gates | ✅ | GATE1M 0.995 (default) / 0.630 (Life max) ≥ 0.5; leakRate and syncRate still count at default, leakRate at Life max (`final_gate2_table_*.log` vs `gate2_base_counting.txt`). |
| SC-012 macro neutrality | ✅ | `combo_neutral.log` `VoragoMacro_NeutralIsIdentity` 104 assertions green; every build 0 warnings (`build_final.log`). |
| SC-013 lever neutrality | ✅ | `VoragoVoice_EcosystemLeverNeutral` and `VoragoVoice_RouteLeverZeroAtZeroLane` green unedited (`e4_shipped_contracts.log`, `combo_perpush_dsp_systems.log`; `fr034_surfaced.md` entry 3 unapplied). |
| SC-014 boundedness | ✅ | `VoragoEngine_CapabilityLeverBounded` (listed by `dsp_systems_tests --list-tests`) in the per-push lane: `close-lanes/suite_dsp_systems_tests.log:1480` "All tests passed (6069572 assertions in 1430 test cases)" on the final binary (W2 2.5 and the macro rows in place). |
| SC-015 CPU | ⏳ | T051 / T056 logs. |
| SC-016 determinism | ✅ | `Vorago_PresetSweep_RendersAreReproducible` in every `sweep6_shard_<i>.log` (34 shards "All tests passed"; the 8 non-zero shards fail only the ablation / long-render CHECKs named in T055); `VoragoEngine_SlotSeedReproducibility` in `close-lanes/suite_dsp_systems_tests.log:1480` (all 1430 cases passed). |
| SC-017 fingerprint re-harvest | ✅ | No fingerprint moved (`final_ghost_harvest_{1,2}.log` green). |
| SC-018 regression suites | ✅ / ☑ | `summary.txt`: 9 of 10 lanes rc=0; long_dsp_systems_tests 41 / 42 (ruled Movement rho). |
| SC-019 default render recorded | ✅ | `default_before_13b.log` / `default_after_13b.log` (M1 RMS −26.97 / −26.97 dBFS, t0on 1.1895 / 1.1783); `default_before_p0.log` / `default_after_p0.log` (P0 1.1895 / 1.1783, finite). |
| SC-020 surface unchanged | ✅ | as FR-039 (`close-lanes/suite_vorago_tests.log:250`). |
| SC-021 confirming pass | ✅ / ☑ | `t054_tree_green.log` + `t054_tree_green_hull090.log` green; `sweep6b_aggregate.log:316` 39 (E1, M4, M5 ruled); shard vs probe 0 gaps > 0.01; `sweep6_records_check.log` 43 OK. |
| SC-022 showcase subsets | ✅ | `sweep6b_aggregate.log:2199` "SUBSET pairs: 0". |
| SC-023 T063 re-read | ✅ | `t063_reread.md`; every unverified cell carries a ruling. |
| SC-024 cross-platform | ✅ | 0 warnings (`final_build.log`), `portability.log` all clear, tidy 0 / 0, `pluginval.log` exit 0. |
| SC-025 Seraphis untouched | ✅ | `git_diff_names.txt` 0 forbidden paths; `close-lanes/suite_seraphis_tests.log:331` 109 / 109. |
| SC-026 adoption tiers | ✅ | `rulings.md` ADOPTED lines (E4, M2, M10, three FR-041 re-authors, Hull 0.90); companions itemised (lever_table.md); no sunk preset; FR-040 re-measure cited in T053. |
| SC-027 re-open termination | ✅ | Re-open count 0 on every row; no second ladder exists (`rulings.md`, lever_table.md). |

## 4. Lever table

See `artifacts/lever_table.md` (FR-050), including the "Final-tree readings" section.

## 5. Default-render change (FR-051)

| Figure | Before (T005, base) | After (T049, final) | Cause |
|---|---|---|---|
| 13b probe M1 stereo RMS | −26.97 dBFS | −26.97 dBFS | — |
| 13b probe t0on | 1.1895 | 1.1783 | the E4 loop-wake lane (`kLoopWakeLaneGain` 2.5) is live at the default Ecology Mix 0.15; the Age / Life rows are at their neutral on the default surface |
| P0 descriptor (Calibrate) | 1.1895 | 1.1783 | same |
| peak | ≤ 0.9661 (finite) | ≤ 0.9661 (finite) | — |

## 6. CPU delta (FR-052)

⏳ filled from `cpu_final_dsp.log`, `cpu_final_vorago.log` and `cpu_ab_final.log` (T051 / T056).
