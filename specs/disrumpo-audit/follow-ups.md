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
   CrossoverNetwork, now also the dry-path chain from T1.09 `a2f7fe51`). Measured: at 192 kHz the dry/wet
   alignment residual is −59.55 dB (bar −60 dB; 44.1–96 kHz read −78 to −83 dB), flatness still 0.0128 dB; a
   double-precision allpass chain of the same design reads −123 dB in the probe (`[.][t109-probe]`). Fix =
   double-precision allpass stages for both chains. Also from T1.09: the LR4s glide on a frequency change while
   every allpass jumps (brief mismatch, settles flat), and the dry term skips input gain (T2.09 territory).
8. **`run-gate.js` comma-OR filter re-runs the whole set in the `[wallclock]` pass** (known pitfall, hit again
   on the evidence run: 127 cases ran twice, +22 min). Fix the runner or always use separate gates.

9. **Tube node output level.** With drive 4 (a legal setting) the Tube node peaks at ≈2.0 before pan (1.41 after
   the centre-pan loss), i.e. above 0 dBFS on a −6 dBFS sweep input. Found by the T1.06 AP-003 probe
   (`[.][t106-ap003-probe]`). Whether the type needs output compensation is not established.
10. **Re-rate spike.** Re-deriving coefficients when a band's clocked factor changes costs ≈145 µs per band at
    48 kHz (≈580 µs if all four bands switch in one block; a 512-sample block is 10.7 ms). Re-rating only the
    active type's processor would cut it. Probe: `[.][t106-cpu-probe]`.
11. **Mixed-family morph can clock Spectral or Fractal at 4x.** Spectral stays prepared at the host rate (its
    prepare allocates) and Fractal clamps its rate at 192 kHz, so both run with a rate mismatch in that case.
    Also: the "8x" Oversample Max entry now maps to 4x (T1.06, `ee0f1e38`) — CHANGELOG entry pending (T4.08).

12. **SC-002 for ≥3-node cross-family mixes.** T1.13 (`a3db3503`) replaced the discontinuous transition gain with
    a linear constant-voltage law (`g_i = w_i`): the spec's "equal-power crossfade" presumes incoherent sources,
    but cross-family node outputs of the same input are coherent (correlation 0.987–0.999), so `sqrt(w)` reads
    +3 dB at the midpoint and fails SC-002 (±1 dB). Linear: identical nodes 3e-5 dB, coherent pairs 0.18–0.65 dB
    — but a 4-node diagonal sweep that includes FullRectify (incoherent output) reads −2.16 dB. No fixed per-weight
    law passes that case; only a signal-adaptive power-matched blend (Σ wᵢ·Pᵢ tracking, pumping risk) does. The
    4-node level case is committed `[!shouldfail]` in unit/morph_transition_test.cpp. Decide: re-scope SC-002 to
    pairs / coherent sets, or design the adaptive blend. Probe: `[.][t113-law-probe]`.
13. **Envelope follower time convention.** The sweep envelope's "attack ms" / "release ms" reach ≈99 % (coefficient
    `exp(−2π/(t·sr))`) at the set time; the 63 % point sits at ≈t/7.5 (10 ms attack → 1.3 ms; 100 ms release →
    31.8 ms to 37 %). Consistent across 44.1/48/96 kHz since T1.10 (`98c17930`), but the UI/spec should state the
    convention, or the mapping should be changed to the usual 63 % time.

14. **`specs/Disrumpo/dsp-details.md` §7.3 is out of date** since T1.14 (`553a24ff`): it still says same-family
    morphs run one algorithm instance with interpolated parameters; the code now does that only for IDENTICAL
    types and blends different types in parallel (linear law). Update the doc (and §… equal-power wording).
15. **Save → load → save is not byte-stable** (found by T1.12's round-trip test): a few floats ahead of the morph
    section move by one ULP per cycle — first at byte 108 (sweep envelope release), also bytes 166/175/194 (mod-source
    block). Looks like a normalise/denormalise round trip in setState/getState; no audit finding covers it. Add a
    whole-stream equality test and fix the offending encode/decode pair.
16. **Global "Sweep Morph Link" now has an effect** (T1.12, `3e8f6b18`): it drives morph X for bands whose own X
    link is None. Eight factory presets have sweep on and a non-None global link (values 1, 2, 3, 4, 6) and will
    now morph with the sweep where before the setting did nothing. Decide: keep (and audition those presets), make
    it drive Y too, or reset those presets' global link to None in T3.01.
17. **Tube node acts as a pure gain stage at −6 dBFS** (T1.14 probe: +6.02 dB output for 2x drive) — extends item 9.

## Done outside the plan

- `68255845` fix(dsp): CrossoverLR4 stages in double precision — latent float32 defect (DC gain error up to
  1.18 dB at 20 Hz / 192 kHz; 0.12 dB at 112 Hz / 192 kHz) exposed when T1.03 removed the start-up glide that
  had masked it. CROSS-PLUGIN (every crossover consumer).
- `e498752d` test(profundum): regenerated evidence after that fix (109 CSVs; residual floors dropped 20–45 dB,
  e.g. sc017 aliased_L_dbfs −88.96 → −132.54; nine pass cells flipped 0 → 1, none 1 → 0).
