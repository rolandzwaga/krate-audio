# Compliance Report — Vorago Phase 13c (vorago-phase13c-capability-audibility)

## Status: CLOSED BY RULINGS (main loop, 2026-10-06) — 7 of 15 roster primaries at the bar, the other 8 and 11 secondaries surfaced under FR-027, no cell UNMET

Every row below was read from a checked-in log at `artifacts/` on 2026-10-06 (T064; FR-040 to FR-042). Rulings are
`artifacts/rulings.md` (one line each) and spec.md "Clarifications" B-1 to B-20. The base tree is production
`64f57e1a` (HEAD `ae149a1c`, `artifacts/base_tree.txt`); the final tree is this working copy. An item that no measured
lever brought to its bar is written as **surfaced** (FR-027), never as UNMET and never as met.

**What ships (the lever set, FR-018):** bloom always-on cloud floor (`kCloudRichnessFloor` 0.6346 → 14 active partials)
+ child gain 1.5 (ceiling 2.0); route sizes unchanged with the Ghost lane driving density (`kGhostDensitySpan` 1.2);
ghost tap make-up 21 dB; default ghost peak 0 (B-14) with six presets pinned at 0.60; ecology wet make-up 30 dB
(ceiling 36); attack u⁴ (`kAttackShapePower` 4); breath lane gain 3.0 and tide rate 0.8; M9 rung 2 and M12 rung 1;
preset defs: Feedback Mire breathing 0 (B-12), Wind Through Basalt breathing 0 and Smeared Horizon bloom 0 (B-15),
Choir of Absence unchanged (B-18: the trim was measured, declined and reverted; the take-3 arm-1 red is surfaced) (B-18).

**Headline numbers (final tree, stored seed, as compiled, nothing else running — F1 `final_<cell>.log`, T056):**

| cell | preset | before (sweep 4 / `before_<cell>.log`) | final | −6 dB master (`final_minus6_<cell>.log`) | verdict |
|---|---|---|---|---|---|
| S4 | Feedback Mire | 1.7072 | **4.7751** (`final_S4.log:8`) | 4.7763 (:9) | PASS |
| S6 | Slow Bloom | 1.4453 | **4.3549** (`final_S6.log:8`) | 4.3552 (:9) | PASS |
| S9 | Choir of Absence | 1.8349 | **4.3383** (`final_S9.log:8`) | 4.4586 (:9) | PASS (stored-take arms green :9; take 3 arm 1 hi −5.58 dB [NO] :13 — surfaced, B-18) |
| M2 | Erosion | 2.4545 | 2.7945 (`final_M2.log:8`) | 2.7946 | surfaced (B-13) |
| M3 | Crowded Dark | 3.9226 | **4.1136** (`final_M3.log:8`) | 4.1133 | PASS |
| M4 | Drifting Strata | 3.0007 | 0.8745 (`final_M4.log:8`) | 0.8886 | surfaced (B-13) |
| M5 | Stone Gravity | 3.5289 | 3.1864 (`final_M5.log:8`) | 3.1954 | surfaced (B-13); arms all green, see SC-005 |
| M9 | Fogbound | 1.8229 | **4.4115** (`final_M9.log:8`) | 4.4112 | PASS |
| M10 | Teeming | 0.3794 | 0.9943 (`final_M10.log:8`) | 0.9943 | surfaced (B-13) |
| M12 | Monolith | 3.3670 | **5.7413** (`final_M12.log:8`) | 6.1659 | PASS |
| E1 | Bloom Colony | 0.0568 (attrib 0.0568) | 1.1823, attrib 1.1035 (`final_E1.log:8-9`) | 1.1829 | surfaced (B-11) |
| E3 | Swarm Breath | 0.6496 (attrib 0.0760) | 1.1652, attrib 0.6422 (`final_E3.log:8-9`) | 1.1648 | surfaced (B-19) |
| E4 | Feeding Loops | 1.2203 (attrib 0.4488) | 2.9182, attrib 0.9851 (`final_E4.log:8-9`) | 2.9184 | surfaced (B-19) |
| E5 | Haunted Colony | 1.1920 (attrib 0.3296) | 3.9093, attrib 0.8873 (`final_E5.log:8-9`) | 3.9128 | surfaced (B-19) |
| D9.1 | Sudden Chasm | d_att 2.7661, d_Sus 4.3038 | **d_att 9.6066**, d_Sus 4.3035, W_end registered 25 s / measured 23 s (`final_D9.1.log:8,11`) | 9.6051 | PASS |

Secondaries (FR-023, `final_secondaries.log`): D14.1 Slow Bloom **2.5610 VERIFIED** (:42); S7 Teeming 10.1021 VERIFIED
(:28); E7.hi Colony Pulse 1.3429 (:13), D13.1 Teeming 0.8560 (:27), D13.2 Colony Pulse 0.1942 (:11), D14.2 Drifting
Strata 0.3131 (:57) — surfaced (B-12, B-13, B-16).

Every roster cell gives the same verdict with the master gain 6 dB lower (FR-024c / SC-023) and every stored take's
four arms and 44.1 kHz arm 1 read green (`levels: arms [y y y y] arm1@44.1k [y]`, line 9 of each `final_<cell>.log`,
:10 for E cells, :12 for D9.1).

## 1. Lever table (FR-040) — one row per feature; every candidate measured, ladders in `artifacts/`

| feature (cells) | lever (every mechanism in the set) | before | candidates (value → reading, log) | ruled | after | override string |
|---|---|---|---|---|---|---|
| bloom (S6, E1; feeds M3, M10) | (1) always-on cloud floor: `VoragoVoice::kCloudRichnessFloor = 0.6346f` drives `HarmonicCloud` to 14 active partials regardless of richness, the user rolloff reproduced through `updateSpectrumTarget` (`wantTarget = live > 0 \|\| userCount < active`); (2) `kBloomChildGain = 1.5f` with `BloomEngine::kMaxChildGain = 2.0f` (append-only, B-3) | S6 1.4453, E1 0.0568 | child gain 0.35 → 1.4453 (`l1_bloom_0.35_S6.log`); 0.50 → 2.1524; 0.70 → 2.9515; 1.00 → 3.8440 (B-3 ceiling raise); 1.20 → 4.3091; **1.50 → 4.8405**; 2.00 → 5.4501 (`l1_bloom_<g>_S6.log`); E1 on the same rungs 0.1567 / 0.2636 / 0.4448 / 0.7528 / 0.9469 / 1.1989 / 1.4978 (`l1_bloom_<g>_E1.log`); parent drop at 1.5: 0.44 dB (`l1_sentinel_1.50.log`); floor CPU 62886 ns at 14 partials vs 69500 at 5 on the base (`cpu_floor_final.log:19`, `cpu_floor_base.log:19`) | 1.5 (B-4) | S6 4.3549 final (the later levers moved it from 4.8405; B-12 re-read 4.3820); E1 1.1823 surfaced | as compiled |
| routes (E1, E3, E4, E5; feed E7.hi, D13.x, M10) | sizes `kPartialLaneGain` / `kGhostLaneGain` / `kNoiseLevelLeverSpanDb` / `kLoopGainLeverSpan` / `kCouplingLeverSpan` **unchanged** 1.0 / 1.0 / 12 / 0.18 / 0.30; Ghost lane second destination `VoragoEngine::kGhostDensitySpan = 1.2f` (T070, B-7); E1 Partial-lane extension measured and **declined** (B-11) | E1 0.0568, E3 0.6496, E4 1.2203, E5 1.1920 | L2 (`l2_ladder_summary.txt`): pg 1.5 / 2.0 / 3.0 → E1 1.1989 each; ns 15 / 18 → E3 1.3495 / 1.7309; lg 0.24 / 0.30 → E4 1.7688 / 1.7686; cs 0.38 / 0.44 → 1.7687; gg 1.5 / 2.0 → E5 1.5818 / 1.5882; swings `l2_swing.log`; T069 extension (`l2x_ladder_summary.txt`) s0.02/0.04 × g0.25/0.5 → E1 0.9312–1.1421, S6 falls to 1.27–2.50; density span 0.7 / **1.2** / 1.7 → E5 2.7928 / 3.7997 / 3.6205 (`l3_span_ladder_summary.txt`) | sizes stay (B-5); span 1.2 (B-7); extension declined (B-11) | E1 1.1823, E3 1.1652, E4 2.9182, E5 3.9093 — all surfaced (B-11, B-19) | as compiled |
| ghost (S9, E5) | `kGhostTapMakeupDb` 12 → **21** (`kGhostTapMakeupGain` 11.220185); density 0.30 at rest, Ghost lane spans it to 1.5 (`kGhostDensitySpan` 1.2); default ghost peak `kGhostDefaultPeakLevel = 0` (B-14, SC-011) with `kGhostBurstPeak` 0.60 kept for the triggers | S9 1.8349 | 15 / 18 / 21 / 24 dB → S9 2.4284 / 3.2996 / **4.1022** / 4.6989 (`l3_ghost_<n>_S9.log`; 24 dB clips at 44.1 kHz, `l3_ghost_24_S9_minus6.log`); E5 1.6729 / 2.2776 / 2.2835 / 2.3183; density 0.45 / 0.60 / 1.0 / 1.25 / 1.5 / 1.75 / 2.0 / 3.0 → E5 2.0830 / 2.4587 / 3.6273 / 4.0218 / 5.6118 / 2.5635 / 4.1135 / 2.4543 (`l3_density_<d>_E5.log`); sizing `l3_ghost_level_probe.log` (tap RMS −16.99 dBFS vs drone −19.03, loudest second −12.31, 120/120 s sounding) | 21 dB (B-6), span 1.2 (B-7), default peak 0 (B-14) | S9 4.3383; E5 3.9093 | as compiled; B-18 candidates `0=0.5,1400=0.8/0.7/0.6` measured and declined (`b18_ladder_summary.txt`) |
| S4 feedback ecology | `kEcologyWetMakeupDb` 0 → **30**; `FeedbackEcology::kMaxWetGainDb` 24 → 36 (append-only, B-8); Feedback Mire def `kLifeBreathingDepthId` 0 (B-12) | 1.7072 | 6 / 12 / 18 / 24 / **30** / 36 dB → 2.8171 / 2.8372 / 2.9627 / 3.6021 / 4.6618 / 5.7175 (`l4_ecology_<n>.log`; 30 at −6 dB 4.6626); under breath 3.0 / tide 0.8 3.9445 → breathing 0 override 4.7751 (`l6_s4_bisect_summary.txt`) | 30 dB (B-9); Feedback Mire breathing 0 (B-12) | 4.7751 | as compiled (def edit) |
| attack (D9.1; D8.2 via the window) | option (b): `kAttackStageCurve = Linear`, `kAttackShapePower = 4` (u⁴ over stage 0's own sample count); registered defaults unchanged; measured-reach window for D9.1 / D8.2 | d_att 2.7661 (reach P_rev 6 s vs P 4 s) | n = 1 / 2 / **4** / 5 / 6 / 7 → 4.2491 / 5.9340 / 8.1243 / 10.1966 / 9.1264 / reach outside capture (`l5_attack_ladder_summary.txt`, `l5_attack_n<k>.log`); option (a) 4.5618 vs d_Sus 5.3442 FAIL (`l5_attack_optionA.log`); D8.2 7.6879, W_end registered 65 s / measured 35 s (`l5_ruled_D8.2.log`) | n = 4, floor 0.75 confirmed (B-10) | 9.6066, W_end 25 / 23 s | as compiled |
| life modulators (D14.1, D14.2) | `kBreathGravityLaneGain` 1.0 → **3.0**; `kTidalFogLaneGain` 1.0; `kTidalRate` 0.25 → **0.8** (new constant, the literal made a lever) | D14.1 0.6556, D14.2 0.0150 | breath 1.5 / 2.0 / **3.0** → D14.1 0.4820 / 1.5908 / 2.6558 (S6 3.7906 / 3.5884 / 4.3820; `l6_ladder_summary.txt`); 2.5 → 2.0911 (S6 3.9998); tidal gain 1.5 / 2.0 / 3.0 → D14.2 0.6573 / 0.6592 / 0.6643 (`l6_life_tidal_*.log`); tide rate 0.6 / **0.8** / 1.0 → 0.2621 / 0.3131 / 0.2341 (`l6_tide_ladder_summary.txt`); space mix 1.0 override 0.1904; fog swing premise 0.0878 (`l6_premise.log`) | 3.0 / 0.8 (B-12); D14.2 surfaced, cavern fog is the ceiling | D14.1 2.5610; D14.2 0.3131 | as compiled |
| M2 Age | rows unchanged (tilt −4.0, damping 0.55) | 2.4545 | r1 tilt −7 → 2.7865; r2 damping 0.70 → 2.7900; r3 + Age→mutation row → 2.8273 (`l7_M2_ladder_summary.txt`); r0 shipped 2.7945 | surfaced (B-13) | 2.7945 | as compiled |
| M3 Density | shipped rows | 3.9226 | r0 **4.1136**; r1 3.7086; r2 3.7124 (`l7_T052_ladder_summary.txt`, `l7_M3_r*.log`) | shipped rows (B-13) | 4.1136 | as compiled |
| M4 Movement | rows unchanged | 3.0007 | r1 0.8741; r2 0.8533; r3 0.8599 (each with 12 macro-test reds, `l7_M4_r*_macro_tests.log`); r0 0.8745 | surfaced (B-13) | 0.8745 | as compiled |
| M5 Gravity (+ FR-015 arm 3) | rows unchanged (ResonanceGravity + OctaveLock); arm 3 met on the tree with no row change | 3.5289; arm 3 +15.7 dB | cause table `m5_cause.log` (arm 3 +1.12 dB on the final tree; seams reverted +10.49, `l7_gravity_ladder_summary.txt`); r1 3.1393; r2 3.1864 | surfaced (B-13); FR-015 met as compiled | 3.1864; arm 3 +1.12 / +2.26 / +3.90 / +2.14 dB on takes 0–3 (`final_M5.log:10-13`) | as compiled |
| M9 Fog | rung 2: Fog→CavernFog amount 0.70; new row Fog→SmearDecoherence base 0.20 amount +0.40 (`kNumRows` 51) | 1.8229 | r1 3.1581; **r2 4.4115**; r3 4.4498 (`l7_M9_r*.log`) | rung 2 (B-13) | 4.4115 | as compiled |
| M10 Life | rows unchanged | 0.3794 | r0 0.9943 (D13.1 0.8560, S7 10.1021); r1 0.9054; r2 0.8226 (`l7_life_ladder_summary.txt`) | surfaced (B-13) | 0.9943 | as compiled |
| M12 Mass | rung 1: Mass→SubToneLevelOffsetDb amount +6.0 | 3.3670 | **r1 5.7413**; r2 5.7430; r3 5.7774 (`l7_M12_r*.log`) | rung 1 (B-13) | 5.7413 | as compiled |

Declined candidates are in the table (ecology 36 dB "wash", ghost 24 dB clipping, attack option (a), the T069
extension, M3 r1/r2, M9 r3, M12 r2/r3); nothing was adopted unmeasured. Every roster gate was read **as compiled, no
`VORAGO_PILOT_OVERRIDE`** (FR-025), so the FR-042 override strings are empty: Phase 14's T048 transcription is the
def edits listed under "What ships".

## 2. FR-030 / SC-007 — the 27 sweep-4 primaries on the final tree (`final_verified27.log`, as compiled)

21 of 27 PASS (:211 "254 assertions"; rows :10–:209). Six fell under 4.0 and were ruled B-15: S1 Wind Through Basalt
3.0600 (:10) and S3 Smeared Horizon 3.2882 (:24) **fixed by def** (breathing 0 → 4.9774 `final_b15_S1.log`; bloom depth
+ spawn 0 → 4.0128 `final_b15_S3.log`); S2 Resonant Shaft 2.6851 (:17), M1 Lightless 1.7830 (:59), E2 Singing Colony
3.7914 (:95), D3.3 Spore Drift 1.8135 (:191) **surfaced**. The bisect `f2_regress_bisect_summary.txt` names child gain
1.5 / breath 3.0 / ghost 21 dB; `f2_childgain_summary.txt` shows no common child gain serves both sides (0.8 / 1.0 /
1.2: Slow Bloom 2.74 / 3.35 / 3.93 while Lightless 2.56 / 2.21 / 2.01). The full sweep-4 vs final record diff is
`final_sweep_record_diff.txt` (T062 sweep: verified primaries 27 → 30, `final_long_vorago_sweep_aggregate.log`).

## 3. FR-030b / SC-007b — secondary and default-state verifications (`final_secondaries_030b.log`, 14 hosts)

Before = the sweep-4 coverage matrix (`specs/vorago-phase14-presets-release/artifacts/sweep4_aggregate.log:47-174`,
ruling T017: the before runs were header-only logs). After, verified on the final tree: D3.1 Dead Air 3.0600 (:27),
D4.1–D4.3 / D4.7 Wind Through Basalt and D4.9 Dead Air (state-only, :28–31, :46–49), D5.1 (:99, :115), D5.2 (:129),
D6.1–D6.3 Monolith (:143–145), D7.1 (:159), D7.2 (:114, :173), D7.3 (:187), D11 / D12.1 Choir of Absence (:215–216);
the measured default-state set green (`final_default_state.log:92`, `VORAGO_SWEEP_SHARD=42/43`); D7.1 is unverified at
sweep 4 and reported separately (:159 reads VERIFIED on the pilot). **De-verified, surfaced (B-20):** E6.hi Colony
Pulse 1.3700 (:12), Spore Drift D3.3 1.4396 conjunct FAIL (:63), Erosion D4.4–D4.6 (:64–66), Fogbound D4.8 / D4.10–12
via S1 1.4403 (:80–84), Cathedral Void D10.1 (:201). Choir of Absence is unchanged after B-18, so its host readout (:206–216) stands.

## 4. Compliance table — functional requirements

| ID | Verdict | Evidence (read 2026-10-06) |
|---|---|---|
| FR-001 | pass | Roster unchanged: 15 primaries + E7.hi, D13.1, D13.2, D14.1, D14.2 (tasks.md roster; `before_tolerance.txt` rows; `final_f1_summary.txt`). No cell added, dropped or re-roled. |
| FR-002 | pass | Every gate figure is `Vorago_PresetPilot_PrimaryProbe` (`preset_pilot_test.cpp:845`), stored-seed take, M1–M3 twins, C-7.2 descriptor; `VORAGO_PILOT_LEVER` is a test-only engine tweak REQUIREd held on every capture (`t066_tweak_identity.log`, `t067_lever_check.log:11` 10/10); the one window change is the ruled measured-reach window (B-10). Gate runs print `lever: none` (`final_<cell>.log:8`). |
| FR-003 | pass | (i) 15 primaries on the unmodified binary within tolerance, all \|delta\| = 0 (`before_tolerance.txt`, `before_<cell>.log:6`); repeat spread 0.0000 (`before_repeat.log:6`); (ii) reporting binary re-read, 20/20 within tolerance (`before2_<cell>.log:8-9`, `before_tolerance.txt` T015 block); (iii) `base_sweepaxes.log:322` 36/38, `base_nozipper.log:200` 24/24, `gate1_base_{default,lifemax}.log:26`, `gate2_table_{default,lifemax}_base.log:172/:140`. |
| FR-004 | pass | Secondary readout `VORAGO_PILOT_SECONDARY=1` prints d, bar 1.5, state, conjunct (`final_secondaries.log:11-13,27-28,42-43,57`); all-take arms and `arm1@44.1k` on every gate run (`final_<cell>.log:9-13`); hidden `[.probe]`. |
| FR-005 | pass | 13b probe default surface: before t0 5.9161, M1 RMS −26.89 dBFS (`default_before_13b.log:11,14`); after t0 1.1895, M1 RMS −26.97 dBFS (`default_after_13b.log:11,14`). Pilot P0: before t_K 5.9161 / 2.7422 / 0.6132 / 0.8084 (`default_before_p0.log:15`), after 1.1895 / 1.5869 / 1.1334 / 1.5285 (`default_after_p0.log:15`). |
| FR-010 | pass | Every lever has a checked-in probe reading before its ruling (section 1; `rulings.md`); the bloom mechanism was confirmed with `VoragoVoice_BloomCountsProbe` before the lever (`bloom_mechanism_l1.log:4-11`: 6 children live, 6 sounding, every slot < active 14). |
| FR-010b | pass | Order bloom (B-3/B-4, 10-01) → routes (B-5, 10-02) → ghost (B-6/B-7) → S4 (B-8/B-9) → attack (B-10) → life (B-12) → macros (B-13, Life last) in `rulings.md`; S4 re-opened under breath 3.0 and re-ruled (B-12, `l6_s4_bisect_summary.txt`). |
| FR-011 | pass | Always-on floor + child gain (section 1); `VoragoVoice_BloomChildrenAudible` 182/182 (`l1_sentinel_ruled.log:1556`); `VoragoVoice_CloudRichnessFloor`, `…FloorNeutral`, `…CloudFloorEdge` registered and green (`test_registration.txt`, `final_suite_dsp_systems_tests.log:1480`); parent drop logged (`l1_sentinel_1.50.log`). |
| FR-012 | pass (E1/E3/E4/E5 surfaced) | Sizes measured and kept (B-5); invariants: `VoragoVoice_RouteLeverZeroAtZeroLane` green, `VoragoEngine_GhostDensityRoute` green (`l7_b14_unit.log`), lever tests in the per-push suite (`final_suite_dsp_systems_tests.log:1480`); extension declined (B-11, `l2x_declined_suite.log`). Cells surfaced B-11 / B-19. |
| FR-013 | pass | 21 dB + density route + default peak 0 (section 1); sizing figures `l3_ghost_level_probe.log`; `VoragoEngine_GhostConfiguration` reads `kGhostDefaultPeakLevel` (FR-034 entry 6). |
| FR-014 | pass (M2/M4/M5/M10 surfaced) | M9 rung 2, M12 rung 1 (`vorago_macro_matrix.h`, `kNumRows` 51); predicates compile (`final_build.log`, 0 warnings); neutral identity 104/104 (`l7_ruled_neutral.log:4`); Life ruled last (`l7_life_ladder_summary.txt`, 10-03 18:10–20:04). |
| FR-015 | pass | Cause table before ruling (`m5_cause.log`, `l7_gravity_cause_seams.log`); arm 3 on all four takes +1.12 / +2.26 / +3.90 / +2.14 dB, arm 1 hi −7.20 / −6.20 / −10.42 / −6.95 dB, 44.1 kHz arm 1 [y] (`final_M5.log:9-13`); met as compiled (B-13). |
| FR-016 | pass | Option (b) shipped, option (a) measured and declined (`l5_attack_optionA.log`); `VoragoVoice_AttackTracksStageTime` reach 19.0 s in [15, 20] (`l5_ruled_summary.txt`); D9.1 W_end registered 25 s / measured 23 s (`final_D9.1.log:8`); D8.2 7.6879, 65 / 35 s (`l5_ruled_D8.2.log`); defaults unchanged (SC-019 row). |
| FR-017 | pass | 30 dB make-up, arm 2 green on the same printout (`final_S4.log:9-13`: lo −40.55 / −37.34 / −41.33 / −41.75 dB). |
| FR-017b | pass (D14.2 surfaced) | Breath 3.0 → D14.1 2.5610 VERIFIED (`final_secondaries.log:42`); tide 0.8 → D14.2 0.3131 (:57), surfaced (B-12); `VoragoVoice_LifeLaneDepthZero` green (`test_registration.txt`, suite). |
| FR-018 | pass | One row per feature in section 1, every mechanism itemised, every candidate present. |
| FR-019 | pass | Production diff (`git_diff_names.txt`): `vorago_voice.h`, `vorago_engine.h`, `vorago_macro_matrix.h`; `bloom_engine.h` append-only `kMaxChildGain` (B-3); `feedback_ecology.h` `kMaxWetGainDb` 36 (B-8, the one Phase 5 constant); `ghost_params.h` default 0 (B-14); preset defs and four regenerated presets (B-12/B-14/B-15/B-18). No Seraphis-consumed header. |
| FR-020 | pass / surfaced | S4, S6, S9, M3, M9, M12 ≥ 4.0; M2, M4, M5, M10 surfaced (headline table; B-13). |
| FR-021 | surfaced | E1 1.1823, E3 1.1652, E4 2.9182, E5 3.9093 (`final_E<k>.log:8-9`); B-11, B-19. |
| FR-022 | pass | d_att 9.6066 ≥ 4.0 and ≥ d_Sus + 1.5 (5.8035) (`final_D9.1.log:8,11`). |
| FR-023 | pass / surfaced | D14.1 VERIFIED; E7.hi 1.3429, D13.1 0.8560, D13.2 0.1942, D14.2 0.3131 surfaced (`final_secondaries.log`; B-12, B-13, B-16). |
| FR-024 | pass | `levels: arms [y y y y]` on every roster gate printout (`final_<cell>.log:9`; E :10; D9.1 :12). |
| FR-024b | pass | `arm1@44.1k [y]` on the same lines; Stone Gravity's before red (−5.97 dBFS, `before2_M5.log:7`) is now −7.09 dB [PASS] (`l7_gravity_ladder_summary.txt`) and [y] (`final_M5.log:9`). |
| FR-024c | pass | Every roster cell keeps its verdict at −6 dB (`final_minus6_<cell>.log:9`, headline table). |
| FR-025 | pass | Every gate as compiled (`lever: none`, no override env in any `final_*.log` START line); ladders used `VORAGO_PILOT_LEVER` / rebuilds; the def edits are the hand-off. |
| FR-026 | pass | F1 re-run once on the final binary, alone, 2026-10-05 (`final_f1_summary.txt`; every row cites `final_*.log`). S9 was re-read under the B-18 candidates after F1 (`b18_*.log`); the candidates were declined and the def restored, so `final_S9.log` is the final-tree reading. |
| FR-027 | pass | No UNMET: 8 primaries and the secondaries are surfaced with readings (B-11, B-12, B-13, B-15, B-16, B-19, B-20); no bar relaxed. |
| FR-030 | pass (4 surfaced) | Section 2: 21/27 as compiled; 2 fixed by def, 4 surfaced (B-15). |
| FR-030b | pass (5 surfaced) | Section 3 (B-20). |
| FR-031 | pass | `final_nozipper.log:200` 24/24; `final_sweepaxes.log:320` 37/38 vs base 36/38 (`base_sweepaxes.log:322`): the one red is the base tree's own Movement rho 0.8333 (both :86-89), every base-passing assertion passes (B-14, `l7_sc011_ruled.log:75` 61/62); soaks green (`final_long_vorago_tests.log:524`, `final_long_dsp_systems_tests.log:1227` 41/42 with that same red at :1140). |
| FR-032 | pass (Life-max knobs surfaced) | Gate 1 default 0.998 / Life max 0.629 (`final_gate1_{default,lifemax}.log:26`; before 0.614 / 0.578 on the base); Gate 2 default PASS (`gate2_table_default.log:172`; syncRate counts); Life max syncRate 0.413 / selfAffinity 0.004 of t0 surfaced (B-16, `gate2_table_lifemax.log`); `VoragoVoice_EcosystemLever*`, `WakeCombineRule`, `AgentReductionRule` in the green per-push suite. |
| FR-033 | pass (FeedbackEcology surfaced) | A/B alone, pinned, idle (`cpu_final_ab_{base,final}_run{1,2}.log:23-24,30`): clause (i) base 2.95245e6 / 2.89776e6, final 3.09089e6 / 3.02242e6 ns (≤ 3.2e6; +4.7 % / +4.3 %); clause (ii) final 2.96639e6 / 2.89792e6 ≤ 4.04172e6; `Vorago_ProcessorCpu` P/D 0.9757 (`cpu_ab_final_Vorago_ProcessorCpu.log`, 93/93). `FeedbackEcology_CpuBudget` red on both binaries (base 179093 > final 167645 > 160000) — surfaced, B-17. |
| FR-034 | pass | Eight per-push suites green (section 5, SC-017); `[long]` lanes as cited; every edited test listed first in `artifacts/fr034_surfaced.md` entries 1–7 with reason. |
| FR-035 | pass | Eighth harvest in the consuming binary, two runs byte-identical (`atmosphere_ghost_fixtures.h:713` md5 32c7a7d17c12077e08316fa923ae05f8; `reharvest_run{1,2}.log`), verify 27/27 (`reharvest_verify.log:41`), new PROVENANCE block (:675). |
| FR-036 | pass | 0 compiler warnings (`final_build.log`); portability all clear 42 compiled (`final_portability.log`), `wsl --shutdown` rc 0; clang-tidy dsp 0 and vorago 0 findings after the eight were fixed (`final_tidy_dsp.log`, `final_tidy_vorago.log`; the first pass read 1 + 7, `b18_summary.txt`); pluginval strictness 5 exit 0, 19 test groups completed (`final_pluginval.log:116-117`). |
| FR-037 | pass | `git_diff_names.txt`: no `plugins/seraphis/**`, no `harmonic_cloud.h`, `atmosphere_engine.h`, `continuous_body.h`, `aether_reverb.h`, `entropy_processor.h`, life modulator or `seraphis_*.h`; `seraphis_tests` 444660/444660 (`final_suite_seraphis_tests.log:331`). |
| FR-038 | pass | `odr_sweep.txt` T023 / T030 / T034 sweeps before `kCloudRichnessFloor`, `kBloomChildGain`, `kPartialLaneGain`, `kGhostLaneGain`, `kGhostDensity*` were written. |
| FR-040 | pass | Section 1. |
| FR-041 | pass | Default render change recorded as intentional voicing: FR-005 row (t0 5.9161 → 1.1895, M1 RMS −26.89 → −26.97 dBFS); cause: the default ghost peak 0 (B-14) removed the stochastic ghost from the default surface; the always-on cloud floor and the Phase 10 axes restored (`l7_sc011_ruled.log`). |
| FR-042 | pass | Section 6. |

## 5. Compliance table — success criteria

| ID | Verdict | Evidence (read 2026-10-06) |
|---|---|---|
| SC-001 | pass | `before_tolerance.txt`: 20 values \|delta\| = 0.0000 within max(0.01, 0.005·d); repeat spread 0.0000 (`before_repeat.log:6`); reporting binary 20/20 (`before2_*.log`). |
| SC-002 | pass / surfaced | S4 4.7751, S6 4.3549, S9 4.3383, M3 4.1136, M9 4.4115, M12 5.7413 with arms green; M2 / M4 / M5 / M10 surfaced (headline table). |
| SC-003 | surfaced | E1 / E3 / E4 / E5 (`final_E<k>.log:8-9`), B-11 / B-19. |
| SC-004 | pass | d_att 9.6066 ≥ 5.8035; option (b) shipped, option (a) 4.5618 measured; P_rev reach 17 s; W_end registered 25 s / measured 23 s (`final_D9.1.log:8-11`, `l5_ruled_summary.txt`). |
| SC-005 | pass | Stone Gravity arm 3 +1.12 / +2.26 / +3.90 / +2.14 dB and arm 1 hi −7.20 / −6.20 / −10.42 / −6.95 dB on takes 0–3, 44.1 kHz arm 1 [y] (`final_M5.log:9-13`); cause table `m5_cause.log`. |
| SC-006 | pass / surfaced | D14.1 2.5610 VERIFIED (`final_secondaries.log:42`); E7.hi 1.3429, D13.1 0.8560, D13.2 0.1942, D14.2 0.3131 surfaced (:13, :27, :11, :57). |
| SC-007 | pass (4 surfaced) | Section 2 (`final_verified27.log`). |
| SC-007b | pass (5 surfaced) | Section 3. |
| SC-008 | pass | `VoragoVoice_BloomChildrenAudible` 182/182 (`l1_sentinel_ruled.log:1556`); probe at richness 0.00: 6 live / 6 sounding, slots 8–13 < active 14 (`bloom_mechanism_l1.log:4-11`); parent drop logged not compensated (`l1_sentinel_1.50.log`). |
| SC-009 | pass | `VoragoVoice_EcosystemLeverNeutral` in the green per-push systems suite (`final_suite_dsp_systems_tests.log:1480`), unedited (not in `fr034_surfaced.md`); `VoragoVoice_RouteLeverZeroAtZeroLane` green. |
| SC-010 | pass | `VoragoMacro_NeutralIsIdentity` 104/104 (`l7_ruled_neutral.log:4`); predicates compile (`final_build.log` 0 warnings). |
| SC-011 | pass | NoZipper 24/24; SweepAxes 37/38 vs base 36/38, the red common to both (FR-031 row). |
| SC-012 | pass | GATE1M 0.998 default / 0.629 Life max (`final_gate1_{default,lifemax}.log:26`). |
| SC-012b | pass default / surfaced Life max | `gate2_table_default.log:172` PASS, syncRate counted; Life max surfaced (B-16). |
| SC-013 | pass | `VoragoEngine_CapabilityLeverBounded` 72/72 at 44.1 / 48 / 96 kHz, Gravity 0 and 1, six voices: peak ≤ 0.2055, 0 non-finite, 0 allocations, bytes unchanged after prepare (`t055_bounded.log:4-9,12`; 304 s accepted, ruling T055); soaks green (FR-031 row); plugin `soak_test.cpp` in `final_long_vorago_tests.log:524`. |
| SC-014 | pass (FeedbackEcology surfaced) | FR-033 row; delta vs base +0.138e6 / +0.125e6 ns (+4.7 % / +4.3 %); floor cost 62886 ns at 14 partials (`cpu_floor_final.log:19`). |
| SC-015 | pass | `VoragoEngine_SlotSeedReproducibility` 11/11 (`final_named_cases.log:10`); `Vorago_PresetSweep_RendersAreReproducible` green in every shard (`final_long_vorago_sweep_shard_*.log`). |
| SC-016 | pass | FR-035 row. |
| SC-017 | pass | `final_suite_dsp_core_tests.log:5` 1588248; `…primitives:74` 4561487; `…processors:194` 10697080; `…systems:1480` 6061626; `…effects:1544` 115009; `final_suite_vorago_tests.log:250` 4059807; `…seraphis:331` 444660; `…shared:5` 6420 — all "All tests passed"; `[long]` lanes per FR-031 and T062 (the sweep cases are Phase 14's gate and fail as at sweep 4: `final_long_vorago_sweep_aggregate.log`, section 2); test edits only under `fr034_surfaced.md`. |
| SC-018 | pass | FR-005 row; output finite, peak ≤ 0.9661 on every P0 take (`default_after_p0.log`), `default_after_13b.log:11`. |
| SC-019 | pass | 12 param-table / state cases green, `kCurrentStateVersion == 3` (`final_named_cases.log:2-5`); registered defaults unchanged except the ruled ghost peak default (B-14, `param_table_expected.h:172`). |
| SC-020 | pass | `arm1@44.1k [y]` on every roster gate line (FR-024b row). |
| SC-021 | pass | FR-036 row. |
| SC-022 | pass | FR-037 row. |
| SC-023 | pass | FR-024c row. |

## 6. Phase 14 hand-off (FR-042) and FR-041

- **Override strings:** none — every gate is as compiled. T048 transcribes the def edits: Feedback Mire
  `kLifeBreathingDepthId` 0; Tectonic Floor, Drifting Strata, Entropic Hum, Teeming, Endless Descent, Steam Vent
  `kGhostPeakLevelId` 0.60; Wind Through Basalt `kLifeBreathingDepthId` 0; Smeared Horizon `kBloomDepthId` 0 and
  `kBloomSpawnRateId` 0; Choir of Absence unchanged (B-18: the trim was measured, declined and reverted; the take-3 arm-1 red is surfaced).
- **Default surface (E0 / P0):** t0 5.9161 → 1.1895, M1 RMS −26.89 → −26.97 dBFS; the pilot's calibration reads
  `ruled K = NONE` on the 13c tree because Glass Well's t_K rose (`default_after_p0.log:21,24`) — handed over.
- **Surfaced, with readings (all in `specs/vorago-phase14-presets-release/tasks.md`):** M2, M4, M5, M10 (B-13); E1
  (B-11); E3, E4, E5 (B-19); E7.hi, D13.1, D13.2 (B-13), D14.2 (B-12); S2, M1, E2, D3.3 (B-15); E6.hi, Spore Drift
  D3.3, Erosion D4.4–6, Fogbound D4.8 / D4.10–12, Cathedral Void D10.1 (B-20); SC-012b Life max (B-16);
  `FeedbackEcology_CpuBudget` (B-17); the phase-close runtime MUST (parallel lanes, render cache).

## 7. Honesty notes

- The `vorago_tests` `[long]` lane was run as 8 concurrent sweep shards plus the three non-sweep cases, not as one
  serial process (`final_long_vorago_tests_serial_stopped.log` is the stopped serial run); sharding partitions preset
  indices only. The sweep's own cases fail on the 13c tree as they did at sweep 4 (Phase 14 is paused at T048 on
  them); they are not 13c gates and are reported in section 2.
- `final_build.log` exit code is 1 from 55 MSB3073 post-build copy failures (Seraphis presets into `C:\ProgramData`);
  compile and link warnings are 0.
- `FeedbackEcology_CpuBudget` is red on this machine on both the base and the final binary; it is surfaced, not fixed.
- The pilot and the sweep disagree on Cathedral Void D10.1 (pilot conjunct FAIL `final_secondaries_030b.log:201`, sweep
  aggregate S `final_long_vorago_sweep_aggregate.log:164`); both are cited and the cell is surfaced.
- The CPU lane's first pass (`cpu_final_dsp.log`, `cpu_final_vorago.log`) carried five reds; four were machine noise
  on re-run alone (`cpu_final_rerun_summary.txt`, `cpu_ab_rerun_summary.txt`) and `VoragoVoice_BloomSlotAccounting`
  was re-specced (FR-034 entry 7, `slot_accounting_respec.log:21`).
