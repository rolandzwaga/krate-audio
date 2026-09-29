# T014 — before-CPU baseline: resolution (main loop, 2026-09-27)

`before_cpu.log` (the workflow's T014 agent) reports `VoragoEngine_CpuBudget` clause (i) RED:
engine 3.89567e+06 / with Cavern 4.02016e+06 ns/block (125.6 % of `kReferenceNs`), and a
"re-run alone" at 3.97439e+06 / 4.09888e+06. Both readings violate the CPU protocol: the first sat
at the end of a 5-hour `[long]`+`[perf]` lane (`node tools/run-cpu-tests.js dsp_systems_tests`
runs the whole lane) and the second followed it immediately with ~10-20 % background load
(TGitCache, VS Code), i.e. not idle, not cooled, not alone.

A/B on an idle machine (2 % load, 15 min idle after the last render, P-core pinned via
`tools/pin-perf-cores.ps1`, one case at a time, 5 min settle between):

| binary | log | engine ns/block | + Cavern | clause (i) ≤ 3.2e6 | clause (ii) |
|---|---|---|---|---|---|
| `05d04f66` (last pushed, pre-Phase-14; worktree `f:/tmp/p13b_ab`, same preset) | `before_cpu_ab_05d04f66.log` | 2.78455e+06 | 2.90905e+06 | PASS | 103.3 % of baseline |
| current tree (T002 friend lines + T005 registration only) | `before_cpu_isolated.log` | 2.75016e+06 | 2.87466e+06 | PASS | 102.1 % of baseline |

Verdict: no code regression — the two binaries agree within 1.2 % and both sit ~14 % under the
reference; the red figures measured the machine. **The FR-032 / SC-016 "before" value is
2.75016e+06 engine / 2.87466e+06 with Cavern (`before_cpu_isolated.log`).** FR-032 stands as
written ("stays green"); no re-scope.

The six other `[perf]` failures in `before_cpu.log` (VoragoVoice_CompositionOverhead 1.388 vs 1.15,
vector_mixer_tests 0.0506 % vs 0.05 %, seraphis_perf ×2, resonance_drift_network_perf,
feedback_ecology_perf) were measured in the same hot lane and are re-measured alone below
(`before_perf_isolated_*.log`); T030 (after-CPU) repeats the isolated protocol.

## The two cases still red alone on a cool machine (2026-09-27, 20:31-20:44)

Both fail on the pre-phase binary as well, so neither is caused by Phase 13b; both are owned here as open reds, never labelled pre-existing.

**`VoragoVoice_CompositionOverhead`** (Phase 10 SC-003, bound 1.15, `vorago_perf_test.cpp:1612`) — whole-voice ns/block over the sum of nine standalone terms. Alternating, pinned, 60 s settles: current tree 1.37373 (`before_perf_isolated_VoragoVoice_CompositionOverhead.log`), `05d04f66` 1.15629 (`before_perf_ab_05d04f66_VoragoVoice_CompositionOverhead.log`), current 1.21115 (`before_perf_co_new_run2.log`), `05d04f66` 1.21179 (`before_perf_co_old_run2.log`), current 1.17104 (`before_perf_co_new_run3.log`). History: Phase 10 1.07678 / 1.11993 / 1.1408; Phase 10a 1.15834 in-lane, 1.14024 alone. Old and new binaries overlap (1.16-1.21 vs 1.17-1.37); the ratio scatters by 0.2 between runs of the same binary minutes apart, so it is measuring the machine more than the composition; still, every reading today is over the bound.

**`VectorMixer: 512 samples mono performance benchmark (SC-003)`** (`vector_mixer_tests.cpp:1077`, bound 0.05 % of one core = 5.3 us per 512-sample block, one 10 000-iteration loop after a single warm-up block, `high_resolution_clock`): hot lane 0.0505706 %, alone and pinned 0.0785983 % (`before_perf_isolated_VectorMixer_SC003.log`); earlier records 0.0514528 / 0.0668556 / 0.0772086 %. A single-loop micro-benchmark at this scale measures boost state and clock resolution, not the mixer.

## Perf-fix diagnosis (2026-09-27, 21:09-21:14, alone, pinned, after a 5-min cool-down)

- **VectorMixer benchmark** rewritten to warm-up 400 blocks + best-of-25 trials of 500 blocks on `steady_clock` (bound unchanged, 0.05 %): **0.0144858 % and 0.0142395 %** (`perf_fix_VectorMixer_run1.log`, `_run2.log`), against the single-loop readings of 0.0506-0.0786 %. The original 2025 compliance figure for this bound was 0.035 % (`specs/_archive2_/031-vector-mixer/compliance.md` SC-003). The loop was timing interrupts and boost transitions, not the mixer.
- **VoragoVoice_CompositionOverhead** instrumented (gate untouched): whole 545515 / 549789 ns per block; sum of the nine independently minimised parts 445425 / 450660 → ratio **1.22471 / 1.21996** (red); the nine parts run back-to-back inside ONE timed block, minimised as a whole exactly like the voice: 544788 / 534561 → ratio **1.00133 / 1.02849** (`perf_fix_CompositionOverhead_run1.log`, `_run2.log`). The voice adds at most 3 % over its parts; the 22 % is the sum-of-minima estimator (each part's best trial rarely coincides, and each part alone runs with its own state hot in cache), biased low and increasingly so on a noisier machine. This is the same code Phase 10 measured at 1.08-1.14 when the machine was quieter.

## Perf fix — final readings and gates (2026-09-27 22:53, freshly booted machine after a crash, alone, pinned)

- `VoragoVoice_CompositionOverhead`, gate now whole / parts-back-to-back (user ruling, bound 1.15 unchanged): **ratio whole / parts 1.07392, PASS**; the printed sum-of-minima ratio read 1.08514 on this quieter boot (`perf_fix_CompositionOverhead_gated_run1.log`, "All tests passed (6 assertions in 1 test case)"). Absolute figures (whole 774007 ns) sit above the earlier session's 545000 because the machine was still settling after the restart; the ratio is what the gate measures.
- `VectorMixer` benchmark, best-of-25: **0.0241017 %, PASS** (`perf_fix_VectorMixer_run3.log`).
- Phase 10 spec SC-003 amended with the ruling; the test's header comment records the old reference and why it moved.

## T030 — after-CPU (main loop, 2026-09-29 05:50-06:17, machine idle from 05:34, 2-5 % load)

Pinned perf roster alone (`after_cpu_final2.log`, `pin-perf-cores.ps1`, filter `[performance],[perf],[.perf],[benchmark],[!benchmark]`): 52 of 54 cases pass.

| case | roster reading | before (T014, alone) | verdict |
|---|---|---|---|
| `VoragoEngine_CpuBudget` (FR-032 / SC-016) | engine **2.45563e+06** ns/block, + Cavern 2.58012e+06 = 80.6 % of the 3.2e6 reference; clause (ii) 91.1 % of baseline | 2.75016e+06 / 2.87466e+06 (`before_cpu_isolated.log`) | **PASS** — after is 89.3 % of before |
| `VoragoVoice_CompositionOverhead` (Phase 10 SC-003, parts-back-to-back gate 1.15) | PASS in the roster | 1.07392 (`perf_fix_CompositionOverhead_gated_run1.log`) | PASS |
| SympatheticResonance SIMD benchmark (`[sympathetic][simd]`, bound SIMD ≥ 0.9× scalar) | 0.71× in the roster (back-to-back, 4.5 min into it) | 1.02× (`before_cpu.log`) | re-run alone after a 5-min settle: **0.93× PASS** (`after_perf_isolated_SympatheticResonance_SIMD.log`) — the roster's back-to-back heat, not the code (untouched by this phase) |
| `SeraphisVoice_CompositionOverhead` (Seraphis SC-002, sum-of-eight estimator, bound 1.1) | 1.16887 | 1.01677 (`before_perf_isolated_SeraphisVoice_CompositionOverhead.log`) | alone after a 5-min settle: 1.18631 FAIL (`after_perf_isolated_SeraphisVoice_CompositionOverhead.log`); alternating A/B, alone, pinned, 5-min settles: **`05d04f66` (pre-phase binary) 1.11308 FAIL, current tree 1.07634 PASS** (`after_perf_ab_{05d04f66,new}_SeraphisVoice_CompositionOverhead.log`). The verdict flips between runs of BOTH binaries (old 1.017 → 1.113; new 1.169 → 1.186 → 1.076) and the newer binary reads lower in the paired run: this is the sum-of-minima estimator bias already diagnosed for `VoragoVoice_CompositionOverhead` (this file, perf-fix section; fixed for Vorago in `e0beed68` by gating on parts-back-to-back), now showing on the Seraphis twin. Not a Phase 13b regression — no Seraphis or shared-component source changed in this phase (dsp/ production diff: `vorago_voice.h`, `vorago_engine.h` only). Surfaced under FR-017 for a ruling: apply the `e0beed68` estimator fix to `seraphis_perf_test.cpp` in its own commit, as was done for Vorago |
