# Tuning ladder — main-loop record (2026-09-28)

| rung | spans (noise dB / peak dB / loop) | surface | t0 (seed twin, eco on) | d(on, off) | d/t0 | GATE1 | M1 RMS | GR max |
|---|---|---|---|---|---|---|---|---|
| before | — (no levers, bases 0.50/0.50) | default | 1.7128 | 0.6311 | 0.368 | FAIL | -26.10 | 0.00 |
| before | — | Life max | 2.1601 | 2.1323 | 0.987 | FAIL | -25.84 | — |
| L1+L2 | 9 / 12 / 0.15, bases 0.45/0.45 | default | 1.1952 | 2.2202 | 1.858 | FAIL | -26.34 | 0.00 |
| L1+L2 | 9 / 12 / 0.15 | Life max | 2.6192 | 2.8674 | 1.095 | FAIL | -26.09 | — |
| L4 step 1 | 12 / 18 / 0.18 | default | 2.6599 | 2.9203 | 1.098 | FAIL | -26.52 | 0.00 |
| L4 step 1 | 12 / 18 / 0.18 | Life max | 3.8270 | 3.4630 | 0.905 | FAIL | -26.25 | — |

Logs: `L2_gate1_*.log`, `L4_gate1_*.log`. L3 skipped (default RMS change -0.24 dB at L2, inside the ±1.5 dB band). Lever roster on the L4 tree: `L4_lever_tests.log` — "All tests passed (23294 assertions in 12 test cases)"; `VoragoEngine_EcosystemLeverBounded` wall clock **208.9 s alone** (ruling R-2 threshold 240 s: within).

**Reading:** raising the spans raised d(on, off) on both surfaces (2.22 → 2.92, 2.87 → 3.46) but raised the seed-twin distance t0 faster (1.20 → 2.66, 2.62 → 3.83), so the ratio FELL (1.86 → 1.10, 1.10 → 0.91). t0 is measured with the ecosystem ON: once the colony drives audible levers, two seeds differ through those same levers, so the denominator grows with the numerator and the ratio saturates near 1. Against the pre-phase t0 (1.71 / 2.16 — the instrument's randomness when the ecosystem was inaudible, i.e. approximately its non-ecosystem randomness), L4 reads 1.71 / 1.60. Surfaced to the user (FR-017) before any further rung.

## L4 in the new unit (t0 = true-off seed twin, ruling 2026-09-28)

| surface | t0on | **t0 (off twin)** | d(on, off) | d/t0 | GATE1 |
|---|---|---|---|---|---|
| default | 2.6599 | 2.8708 | 2.9203 | 1.017 | FAIL |
| Life max | 3.8270 | 2.9155 | 3.4630 | 1.188 | FAIL |

Logs re-made with the new probe: `L4_gate1_default.log`, `L4_gate1_lifemax.log`. The true-off seed-twin distance is LARGER than the pre-phase ecosystem-on twin (1.71 / 2.16), so the ecosystem now moves the sound about as much as reseeding everything else, on both surfaces.

## Scale calibration (descriptor distances computed from the logged DESCRIPTOR lines, 2026-09-28)

| change | d |
|---|---|
| Life macro 0 → 1, before the phase (before_gate1_default vs before_gate1_lifemax, "default" descriptors) | 2.095 |
| Life macro 0 → 1 at L4 | 1.953 |
| Life macro 0 → 1 with the ecosystem OFF at L4 ("trueoff" descriptors) | 0.125 |
| the phase's voicing change on the default surface (before default vs L4 default) | 2.122 |
| the phase's change with the ecosystem OFF (before trueoff vs L4 trueoff) | 0.199 |
| reseed with the ecosystem OFF at L4 (t0, the ruled yardstick) | 2.871 / 2.916 |
| ecosystem on vs off at L4 | 2.920 (default) / 3.463 (Life max) |

Reading: in this descriptor a distance of 2-3 is a large musical change (the Life macro's full travel is 2.0; a reseed of everything but the colony is 2.9). At L4 the colony's on/off effect already exceeds the Life macro's whole travel and equals a full reseed, and with the colony silenced the Life macro barely moves the sound (0.13) — almost all of Life's audible effect now flows through the colony. A gate at 2 × the off-reseed (5.7-5.8) asks the colony to matter twice as much as everything else combined varies between seeds.

## Seed dependence of the ecosystem effect (L4 tree, default surface, 2026-09-28)

`VORAGO_PROBE_SURFACE=2=<n/15>` sets `kSeedId` on every render (base, twin, true-off, true-off twin), so those runs' twin lines read 0 and only d(on, off) is a datum (`L4_onoff_seed{2,3,4,5}.log`).

| seed index | d(on, off) | default M1 RMS | off M1 RMS |
|---|---|---|---|
| default (0) | 2.9203 | -26.52 | -25.61 |
| 2 | 0.6841 | -26.16 | -25.57 |
| 3 | 0.9437 | -25.31 | -25.55 |
| 4 | 0.6160 | -26.75 | -25.82 |
| 5 | 1.2841 | -25.98 | -25.79 |

Median 0.94, mean 1.29; the default seed is 2-5× the others. The colony's kind mix is stratified (balanced for every seed, ecosystem_engine.h:2187-2236), so the spread comes from the colony's dynamics — how much energy its agents reach, hence how far up the lever spans they push. A gate read at one seed measures that seed's colony, not the instrument; the lane survey (Q(0.50) ≈ 0.40-0.45 across kinds) says the typical lane sits at less than half the lever span, which is what FR-019a lane shaping (ladder L6) exists to fix.

## L6 — lane shaping gain 2.0 (Resonator/Noise/Feedback), default surface, six seeds (2026-09-28 09:24-09:31)

`L6_gate1m_default.log` (`VORAGO_PROBE_SEEDS=6`): d(on, off) per seed 0-5 = 3.4108, 2.2961, 0.4714, 1.2894, 1.8424, 1.4804 → **median 1.6614**, min 0.4714; off-reseed pairs 2.8706, 1.2234, 4.0389, 5.0228, 4.5564 → median 4.0389; **ratio 0.411** (2.0 verdict FAIL). Versus L4 at the same seeds: seed 0 2.92 → 3.41, seed 2 0.68 → 0.47, seed 3 0.94 → 1.29, seed 4 0.62 → 1.84, seed 5 1.28 → 1.48 (median of seeds 2-5: 0.81 → 1.38). Lever roster on the L6 tree: `L6_lever_tests.log` — "All tests passed (23307 assertions in 13 test cases)", `LaneShapingFidelity` live; Bounded wall clock 202.6 s (< 240 s, R-2).

Next rung: L6 gain 3.0 (the ladder's last shaping value), then L4 step 2 (ring coupling / freq wander) if needed.

## L6 gain 3.0 — measured and rejected (09:38-09:45)

`L6b_gate1m_default.log`: d(on, off) per seed 0-5 = 3.2151, 1.4833, 1.1116, 1.0026, 1.4573, 1.1607 → median **1.3090** (gain 2: 1.6614), min 1.0026 (gain 2: 0.4714); ratio 0.324. Lever roster green (`L6b_lever_tests.log`, Bounded 213.3 s). Reading: at gain 3 most lanes clamp at 1, so the levers act as a near-constant boost; the descriptor normalises band energy, so a uniform boost cancels and the on/off distance falls for the seeds that were high. **Gain 2 is kept.** Next: L4 step 2, the ring-coupling and freq-wander levers (motion/flux rather than level).

## L4 step 2 — ring-coupling and freq-wander levers (spans 0.30 / 6 st, slew 50 ms), shaping gain 2, default surface, six seeds (09:54-10:01)

`L4s2_gate1m_default.log`: d(on, off) per seed 0-5 = 3.1242, 3.7721, 2.5386, 0.9202, 2.7112, 2.5894 → **median 2.6503** (L6: 1.6614), min 0.9202 (seed 3); off-reseed median 4.0381; **ratio 0.656**. Lever roster: `L4s2_lever_tests.log` — "All tests passed (23571 assertions in 13 test cases)" (Mapping now checks the ring coupling every chunk and the settled wander after 10 min; Neutral and Reset cover both); Bounded 208.9 s. Production: `vorago_voice.h` gains kFreqWanderBaseSemis 1.5 / kRingCouplingBase 0.12 (the former prepare literals), kCouplingLeverSpan 0.30, kFreqWanderLeverSpanSemis 6.0 slewed at 50 ms, freqWanderApplied_/ringCouplingApplied_ shadows, snapped in installIdentityNeutral (plan S2.8).

## L5 — wander base 1.5 → 0.75 st, ring coupling base 0.12 → 0.06 (spans and shaping as L4 step 2), default surface, six seeds (10:07-10:14)

`L5_gate1m_default.log`: d(on, off) per seed 0-5 = 4.5495, 3.6427, 3.3087, 0.8594, 2.8297, 0.7476 → **median 3.0692** (L4 step 2: 2.6503), min 0.7476 (seed 5; seed 3 0.8594); off-reseed pairs 3.5375, 1.1549, 4.0438, 5.1766, 4.5807 → median 4.0438; **ratio 0.759**. Lever roster `L5_lever_tests.log`: "All tests passed (23571 assertions in 13 test cases)", Bounded 219.0 s. Build 0 warnings. The median rose and the spread widened: two of six colonies now move the sound by less than one unit while the strongest moves it by 4.5 (more than an off-reseed). The voice-owned rungs (L3 skipped by rule, L4 at cap, L5, L6 gain 2) are exhausted; L8 (parameter 301) remains pre-authorised.

## L5 — Life max six seeds and the SC-005 guard (10:15-10:26)

`L5_gate1m_lifemax.log` (`VORAGO_PROBE_SURFACE=109=1 VORAGO_PROBE_SEEDS=6`): d(on, off) per seed 0-5 = 4.3520, 4.8690, 3.4185, 1.1930, 2.6111, 1.2860 → **median 3.0148**, min 1.1930; off-reseed pairs 3.5834, 1.4365, 4.0656, 5.0758, 4.4448 → median 4.0656; **ratio 0.742**. `L5_gate1_default.log` (default seed, GR twin): default M1 RMS **-26.13 dBFS** vs -26.10 before (SC-005: within ±3 dB), **GR max 0.0000 dB**, 0 attack events (≤ 1 dB); single-seed d(on,off) 4.5495 vs t0 3.5375 (1.286), t0on 4.4218.

Scale for the threshold ruling: Life macro full travel 2.0 (before) / 1.95 (L4); the phase's voicing change 2.1; off-reseed medians 4.04 / 4.07; pre-phase on/off 0.63 (default) / 2.13 (Life max, single seed).

## Threshold ruling f = 0.5 (2026-09-28, after the ladder)

Gate 1 passes at d(on, off) ≥ 0.5 × the six-seed median off-reseed on both surfaces; Gate 2 counts an extreme at d ≥ 0.5·t0 and calls it OFF-LIKE below 0.25·t0, with t0 pinned to the GATE1M median off-reseed of the same tree and surface (`VORAGO_PROBE_T0`). Rationale: half an off-reseed (≈ 2.0) is the Life macro's whole travel. Shipped tree = L5 (spans 12 dB / 18 dB / 0.18, coupling span 0.30 on base 0.06, wander span 6 st on base 0.75 st slewed 50 ms, shaping gain 2, wake bases 0.45/0.45): 0.76 default, 0.74 Life max. Final logs: `after_gate1m_default.log`, `after_gate1m_lifemax.log`, `after_table_default.log` (t0 pinned 4.0438), `after_table_lifemax.log` (t0 pinned 4.0656).

## Gate 2 on the shipped tree (f = 0.5, t0 pinned to the GATE1M medians; 10:46-11:41)

| surface | log | t0 (pinned) | bar f·t0 | counted knobs (best counted extreme d) | audible non-kill | GATE2 (N ≥ 4) |
|---|---|---|---|---|---|---|
| default | `after_table_default.log` | 4.0438 | 2.0219 | moveRate 0.5: 4.0188; leakRate 0: 2.6330 | **2 of 14** | FAIL |
| Life max | `after_table_lifemax.log` | 4.0656 | 2.0328 | syncRate 0.5: 4.1970; moveRate 0.5: 3.6086; freqDrift 2e-4: 2.5375 | **3 of 14** | (default-surface gate) |

Kills / OFF-LIKE (d large but within f·t0/2 of ecosystem-off): grazeRate → 0 (4.55 / 4.35, dOff 0), syncRate → 0.5 on the default surface (dOff/t0 0.246, a hair under 0.25). Nearest misses: default kernelSigma 1.88, predation 1.78, forageRate 1.73; Life max forageRate 2.02, kernelSigma 1.92. Union across the two surfaces: **moveRate, leakRate, syncRate, freqDrift = 4 distinct knobs**. Single-seed GATE1 lines on the same runs: 1.125 / 1.070 (seed 0). Surfaced (FR-017).

## Ruling (2026-09-28): Gate 2 counts a knob on either surface → 4 distinct knobs, PASS

Roster handed to Phase 14 Q2 (FR-034): **moveRate, leakRate, syncRate, freqDrift**. Kills at the default surface (never roster candidates for that surface): grazeRate → 0, syncRate → 0.5.

## Post-ladder fix: wander span 6 → 3 st (Phase 10 SC-010 zipper bound; see t031_t037_gates.md). Final-tree gates re-run as `final_*` logs.

## Final tree (span 3 st) — gates so far (15:25 → 19:33; `final_*` logs)

| gate | reading | verdict |
|---|---|---|
| build (9 targets) | `final_build_status.txt`: exit 0, 0 warnings | ok |
| lever roster | `final_lever_tests.log`: All tests passed (23571 / 13) | ok |
| zipper + routing | `final_zipper_routing.log`: All tests passed (47 / 2) | ok |
| Gate 1 default (six seeds) | `final_gate1m_default.log`: ratio **0.656** (was 0.759 at 6 st) | PASS (f 0.5) |
| Gate 1 Life max (six seeds) | `final_gate1m_lifemax.log`: ratio **0.638** (was 0.742) | PASS |
| Gate 2 default (t0 4.0439) | `final_table_default.log`: **1 of 14** — syncRate 2.16; nearest miss selfAffinity 1.95 (0.481), predation 1.36; moveRate now 0.98 | FAIL alone |
| Gate 2 Life max (t0 4.0645) | `final_table_lifemax.log`: **2 of 14** — selfAffinity 2.64, syncRate 2.14; moveRate 1.54; leakRate 5.34 but OFF-LIKE | union = 2 (< 4) |
| SC-005 guard | `final_gate1_default_gr.log`: default M1 RMS -26.47 dBFS (before -26.10, Δ -0.37 dB), GR max 0.0000, 0 attacks | ok |
| regression (8 suites) | all "All tests passed" except dsp_systems 1411/1412: `VoragoEngine_GhostExtensionWiring` clause (a) (fingerprint harvested at 6 st; tree moved) | re-pin pending |
| pluginval | `final_pluginval.log`: completed, strictness 5 | ok |
| clang-tidy dsp | `final_clang_tidy_dsp.log`: 372 files 0/0 | ok |
| portability | `final_portability.log`: all clear -- 6 compiled | ok |
| [long] suites | `final_long.log` running since 17:31 (80 cases at 19:33); `final_vorago_long.log` pending | pending |

Surfaced: the zipper fix (wander span 6 → 3 st) lowered Gate 1 (still ≥ 0.5) and took Gate 2 from 4 counted knobs (union) to 2.

## Ruling (2026-09-28 19:40): ship 3 st; Gate 2 recorded UNMET at 2 of 4; Phase 14 Q2 roster = syncRate, selfAffinity

## Phase 10 SC-008 on the 3 st tree (22:13 → 23:23): no base/span pair passes both bounds

`VoragoMacro_SweepAxes` (Movement per-band total variation, mean Spearman over seeds 101/202/303, bound 0.9) after the 6 → 3 st zipper fix:

| base | span | zipper (SC-010, ≤ 1.5×) | sweep Movement rho | log |
|---|---|---|---|---|
| 0.75 | 3 | 1.38 PASS | **0.8667** FAIL | `final_long.log` |
| 1.5 | 3 | (span sweep: 3 st passes) | **0.8667** FAIL | `base15_sweepaxes.log` |
| 1.5 | 2 | 1.3636 PASS | **0.8667** FAIL | `cand_b1.5_s2.0_{zipper,sweepaxes}.log` |
| 1.0 | 3 | 1.4556 PASS | **0.8667** FAIL | `cand_b1.0_s3.0_{zipper,sweepaxes}.log` |
| 0.75 | 6 | 1.62 FAIL | 1.0000 PASS | `after_long.log`, `zipper_e3_span6.log` |

The mean series is monotone every time (e.g. 1.10, 1.25, 1.36, 1.41, 1.47); one seed inverts two of the top points, where the macro's own increments are ~0.05 and the colony's wander, multiplied by the macro's 1 Hz rate, adds more variation than that. Before the phase the series was 1.03 … 1.25 (`before_cpu.log`).

## Rung: wander lever rate compensation (FR-018b, 2026-09-28 23:30)

Span scaled by min(1, 0.03 Hz / resonance wander rate) (`VoragoVoice::wanderLeverRateComp`), read per control chunk from `resonance_.getWanderRate()`. Depth × rate stays constant across the Movement macro, so the lever's motion is the same at every sweep point and the macro's ordering is its own again; at 1 Hz the lever adds 3 % of the span, inside the zipper bound's room (span 0 read 0.98×). Base 0.75 st, span 3 st, slew 50 ms kept (the ruled tree). Every Gate 1 / Gate 2 surface runs at the 0.03 Hz default, where the factor is exactly 1: those renders are unchanged. Test: `VoragoVoice_EcosystemLeverRateCompensation` (SC-022). Logs: `ratecomp_build_status.txt`, `ratecomp_lever_tests.log`, `ratecomp_zipper_routing.log`, `ratecomp_sweepaxes.log`.

### Exponent 1 reading (23:28 → 23:49)

Lever roster 14/14 (`ratecomp_lever_tests.log`, settled wander 3.75 / 0.84 / 1.05 / 3.75 / 3.75 st at 0.03 / 1 / 0.3 / 0.03 / 0.01 Hz); zipper 0.99× (`ratecomp_zipper_routing.log`, was 1.38× unscaled). Sweep (`ratecomp_sweepaxes.log`): Movement rho **0.9667 PASS**, endpoint **0.1899 FAIL** (bound 0.20; before the phase 0.2173, unscaled span 3 0.33). Series 1.098, 1.153, 1.201, 1.263, 1.306: the lever adds a near-constant +0.05…+0.08 at every point, which lifts the baseline and shrinks the relative rise. Exponent `kWanderLeverRateCompExponent` added: comp = (0.03 / rate)^k, k = 1 constant motion, k = 0 unscaled. Candidates k = 0.5 and 0.75 measured next (`ratecomp_e<k>_{lever,zipper,sweepaxes}.log`).

### Exponent candidates (2026-09-28 23:52 → 2026-09-29 00:29) and ruling

| k | comp(1 Hz) | settled wander at 1 Hz | zipper (≤ 1.5×) | sweep Movement rho (≥ 0.9) | endpoint (≥ 0.20) | mean series | logs |
|---|---|---|---|---|---|---|---|
| 1 | 0.030 | 0.84 st | 0.99 | 0.9667 | **0.1899 FAIL** | 1.098 1.153 1.201 1.263 1.306 | `ratecomp_{lever_tests,zipper_routing,sweepaxes}.log` |
| 0.5 | 0.173 | 1.27 st | 1.02 | 0.9667 | 0.2294 | 1.098 1.179 1.234 **1.352 1.350** | `ratecomp_e0.5_*.log` |
| **0.75** | 0.072 | 0.97 st | **1.0033** | **1.0000** | **0.2089** | 1.098 1.165 1.207 1.271 1.327 | `ratecomp_e0.75_*.log` |

Shipped k = 0.75: the only candidate on which every seed's series is monotone (rho 1.000) with the endpoint above the bound; k = 0.5 clears the bound by more but its mean series' top pair is inverted, the sign of the same seed noise that failed the unscaled lever. The endpoint margin (0.209 vs 0.20) is the same order as before the phase (0.217). Gate 1 / Gate 2 surfaces run at 0.03 Hz where comp = 1 exactly, so their renders are those of the ruled 3 st tree. Final evidence chain (`final2_*` logs) launched on this tree.

## Final tree (k = 0.75) — every gate re-run (2026-09-29 00:31 → 05:34; `final2_*` logs)

| gate | reading | verdict |
|---|---|---|
| build (9 targets) | `final2_build_status.txt`: exit 0, 0 warnings | ok |
| lever roster (14 cases incl. RateCompensation) | `ratecomp_lever_tests.log` (k = 1 tree) + `ratecomp_e0.75_lever.log`: All tests passed | ok |
| zipper + routing | `ratecomp_zipper_routing.log` 0.99×; `ratecomp_e0.75_zipper.log` **1.0033×** (bound 1.5) | ok |
| Gate 1 default (six seeds) | `final2_gate1m_default.log`: median d(on,off) 2.6543, median off-reseed 4.0439, ratio **0.656** | PASS (f 0.5) |
| Gate 1 Life max (six seeds) | `final2_gate1m_lifemax.log`: 2.5914 / 4.0645, ratio **0.638** | PASS |
| Gate 2 default (t0 4.0439) | `final2_table_default.log`: **1 of 14** — syncRate 0.5: 2.1606 (0.534) | FAIL alone |
| Gate 2 Life max (t0 4.0645) | `final2_table_lifemax.log`: **2 of 14** — syncRate 0.5: 2.1446 (0.528) + selfAffinity; union = 2 (< 4) | UNMET as ruled |
| SC-005 guard | `final2_gate1_default_gr.log`: M1 RMS −26.47 dBFS (before −26.10, Δ −0.37 dB), GR max 0.0000 dB, 0 attacks | ok |
| regression (8 suites, per-push filter) | `final2_regression_*.log`: all "All tests passed" (dsp_systems 1413 cases incl. the fifth-harvest fingerprint) | ok |
| pluginval strictness 5 | `final2_pluginval.log`: completed, exit 0 | ok |
| clang-tidy dsp / vorago | `final2_clang_tidy_dsp.log` 372 files 0/0; `final2_clang_tidy_vorago.log` 47 files 0/0 | ok |
| portability | `final2_portability.log`: all clear — 6 compiled; wsl --shutdown 0 | ok |
| [long] systems (incl. SweepAxes Movement rho 1.0000 / endpoint 0.2089, ClickFreeNatural) | `final2_long.log`: All tests passed (42 cases, 01:57 → 05:29) | ok |
| [long] vorago | `final2_vorago_long.log`: All tests passed (3 cases) | ok |
| after-CPU (T030) | `after_cpu_final2.log` (pinned perf roster alone, 16 min idle, 05:50-05:54): `VoragoEngine_CpuBudget` engine **2.45563e+06** ns/block, + Cavern 2.58012e+06 (80.6 % of the 3.2e6 reference; before `before_cpu_isolated.log` 2.75016e+06 / 2.87466e+06 → 89.3 %); `VoragoVoice_CompositionOverhead` PASS. Roster 52/54: `SeraphisVoice_CompositionOverhead` 1.169 (bound 1.1) and the SympatheticResonance SIMD benchmark 0.71× (bound 0.9×) went red inside the back-to-back roster — both untouched by this phase, both passed alone before it (1.017, 1.02×); re-run alone per the CPU protocol: Sympathetic **0.93× PASS**; Seraphis 1.186 alone, then A/B alone: pre-phase binary `05d04f66` **1.113 FAIL**, current tree **1.076 PASS** — the estimator flips on both binaries (`t014_cpu_resolution.md`, T030 section), not a 13b regression; surfaced | PASS (FR-032 / SC-016) |
