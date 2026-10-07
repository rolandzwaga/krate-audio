# Phase 13d lever table (FR-050)

| feature | cells served | lever (every mechanism) | before (sweep 5 / T002 base) | candidates → reading → log | ruled value | after (final tree) | override string | re-open count |
|---|---|---|---|---|---|---|---|---|
| E1 Partial → bloom | E1 (Bloom Colony) | partialBloomGain / partialMutationGain (lane gains, arm A and B), parentCount / childrenPerEvent (arm C), companion 1300 | 1.1823 (attrib 1.1035); spawn-limited (R_k spawn 1 spawned 0 refused 2) | companion 1300=0.0 (no lever) → d 1.1035 / attrib 1.1035 (`e1_companion_00.log`); companion 1300=0.2 (no lever) → d 1.4852 / attrib 1.1035 (`e1_companion_02.log`); premise partialMutationGain=0 → d 1.1476 / attrib 1.0454 (`e1_premise_mut0.log`); a1 2.0/2.0 + 0.2 → d 1.7056 / attrib 1.7712 (`e1_a1_gate.log`); a2 2.0/2.0 + 0.0 → d 1.7712 / attrib 1.7712 (`e1_a2_gate.log`); a3 3.0/3.0 + 0.2 → d 1.7056 / attrib 1.7712 (`e1_a3_gate.log`); a4 3.0/3.0 + 0.0 → d 1.7712 / attrib 1.7712 (`e1_a4_gate.log`); a5 4.0/4.0 + 0.2 → d 1.7056 / attrib 1.7712 (`e1_a5_gate.log`); a6 4.0/4.0 + 0.0 → d 1.7712 / attrib 1.7712 (`e1_a6_gate.log`); b1 bloom 2.0 + 0.2 → d 1.7056 / attrib 1.7712 (`e1_b1_gate.log`); b2 bloom 3.0 + 0.2 → d 1.7056 / attrib 1.7712 (`e1_b2_gate.log`); b3 bloom 4.0 + 0.2 → d 1.7056 / attrib 1.7712 (`e1_b3_gate.log`); b4 bloom 2.0 + mutation 0.5 + 0.2 → d 1.7056 / attrib 1.7712 (`e1_b4_m050_gate.log`); b4 mutation 0.25 → d 1.7053 / attrib 1.7707 (`e1_b4_m025_gate.log`); b4 mutation 0 → d 1.6579 / attrib 1.7272 (`e1_b4_m000_gate.log`); c1 a1 + parentCount=3 → d 1.2845 / attrib 1.4150 (`e1_c1_p3_gate.log`); c1 a1 + parentCount=2 → d 6.8712 / attrib 6.8706 (`e1_c1_p2_gate.log`); c1 a1 + parentCount=1 (spawned 1) → d 4.8289 / attrib 4.9939 (`e1_c1_p1_gate.log`); c2 a1 + childrenPerEvent=3 → d 2.0373 / attrib 2.1082 (`e1_c2_k3_gate.log`); c2 a1 + childrenPerEvent=4 → d 0.6813 / attrib 0.7727 (`e1_c2_k4_gate.log`) — no rung reaches d ≥ 4.0 AND d ≥ attrib + 1.5; arms green on every rung | RULED 2026-10-07: engine-limited, not adopted (user) | unchanged (1.1823 / attrib 1.1035) | - | 0 |
| E4 Feedback → loop wake | E4 (Feeding Loops, 1× events) | | 2.9182 (attrib 0.9851); 1× 3.71 arm 2 red | | | | | 0 |
| E6.hi / E7.hi lanes | E6.hi, E7.hi (Colony Pulse, Life raised) | | 1.3700 / 1.3430 | | | | | 0 |
| M2 Age row | M2 (Erosion) | | 3.2191 | | | | | 0 |
| M5 Gravity row | M5 (Stone Gravity) | | 3.1864 | | | | | 0 |
| M4 Movement row + rho | M4 (Drifting Strata); SweepAxes Movement rho | | 0.8745; rho 0.8333 | | | | | 0 |
| M10 Life row | M10 (Teeming; companion 900) | | 0.9943 | | | | | 0 |

## CPU (FR-033)

| binary | VoragoEngine_CpuBudget (i) ns | (ii) ns | Vorago_ProcessorCpu P/D | Vorago_PresetCpu worst | log |
|---|---|---|---|---|---|
| base (T006) | 3.10765e6 (≤ 3.2e6; 97.114 % of the reference) | 2.98315e6 (≤ 4.04172e6; 110.713 % of the baseline) | 0.960344 (PF/D 0.991988) | Entropic Hum 1.1056 (default surface 3024934 ns/block) | `cpu_base_dsp_systems_tests_full.log:3168-3176`; `cpu_base_vorago_tests_full.log:22,32,635`; lane reds `cpu_base_dsp_reds.txt` |
| final | | | | | |
