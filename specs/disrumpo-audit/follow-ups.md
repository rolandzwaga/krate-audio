# Disrumpo audit — follow-ups surfaced during the fix run

Items the fix plan (`fix-plan.md`) does not cover, found while executing it. Each needs a decision or a new task;
none blocks the remaining plan tasks. Dated 2026-11-06 unless noted.

## Needs a decision

1. **Processor CPU cost after T1.07 (`3531d64a`).** With oversampling really running, 4 HardClip bands at 4x
   (48 kHz, 512-sample blocks, two morph engines per band) cost 20.1–20.3 % of real time, against 3.7–3.9 % before
   (which was 1x, because the factor was never applied). Spec PT-DI-003 says 10 % for 4 bands at 4x. No CPU-lane
   budget test goes through `Processor::process` (they only cover `BandProcessor`), so nothing went red. Needed:
   a processor-level CPU test with the spec budget, then an optimisation pass (candidates: block-rate crossover
   path, per-band engine cost, oversampler filter order). Hidden probe to re-measure: `[.][t107-cpu-probe]`.
2. **Band bypass is a hard switch** (as the plan specified). Toggling bypass off steps the output by 0.743 against
   a steady sample step of 0.050 (≈15x); on by 0.335. Smoothing it (short crossfade) is a design choice.
3. **Fresh-instance morph smoothing default.** T1.03 (`9c427498`) made a fresh instance follow the registered
   parameter default of 0 ms; the engine's old hidden default was 10 ms. If 10 ms is wanted, change the
   registration default (not state layout) — one line plus a defaults test.
4. **Hidden Profundum evidence writers were not regenerated** after `68255845` (CrossoverLR4 double precision):
   Phase 7 side-floor harvests, Phase 13 audibility probes / distinctness pilot / audition renders, Phase 14
   harvests, fingerprints and morph-mod probes. Their CSVs are references other tests check against, so
   regenerating them changes the yardstick; also `phase9 audibility-table.csv` (merge script not in tree) and
   `phase13 coverage-matrix.csv` (shards grew 121 → 161 rows) are stale against their regenerated shards.
   Evidence commit: `e498752d`.

## Needs a task (add to the plan)

5. **Per-band oversampling latency is neither compensated nor reported.** Measured impulse delay at 48 kHz:
   0 samples at 1x, 4 at 2x, 7 at 4x; `getLatencySamples()` reports 0. Bands running at different factors are
   no longer time-aligned at summation (phase smear), and the dry path is not delayed either. T2.10 only covers
   Spectral latency — widen it: delay every band and the dry path to the maximum factor's latency and report
   that to the host. Hidden probe: `[.][t107-probe]`.
6. **Fades under modulation.** Each oversampling factor change now fades the band down and up over 5 + 5 ms.
   If morph modulation keeps crossing a factor boundary this could read as tremolo. Not measured yet.
7. **Allpass compensation filters are still float32 Biquads** (Crossover3Way/4Way and Disrumpo's
   CrossoverNetwork). Likely source of the remaining 0.00075 dB at 192 kHz after `68255845`; low-frequency
   precision of those stages was not in the LR4 test grid.
8. **`run-gate.js` comma-OR filter re-runs the whole set in the `[wallclock]` pass** (known pitfall, hit again
   on the evidence run: 127 cases ran twice, +22 min). Fix the runner or always use separate gates.

## Done outside the plan

- `68255845` fix(dsp): CrossoverLR4 stages in double precision — latent float32 defect (DC gain error up to
  1.18 dB at 20 Hz / 192 kHz; 0.12 dB at 112 Hz / 192 kHz) exposed when T1.03 removed the start-up glide that
  had masked it. CROSS-PLUGIN (every crossover consumer).
- `e498752d` test(profundum): regenerated evidence after that fix (109 CSVs; residual floors dropped 20–45 dB,
  e.g. sc017 aliased_L_dbfs −88.96 → −132.54; nine pass cells flipped 0 → 1, none 1 → 0).
