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

18. **`setActive(true)` does not clear DSP state from the previous render** (T1.15 probe): after rendering patch B,
    the first ≈410 samples of a fresh render of patch A differ by up to 0.556 from a clean A render; loading A
    without rendering B in between is bit-identical. A reset gap in BandProcessor/MorphEngine/adapters on
    activation (reset() should run on setActive(true) or setupProcessing).
19. **Custom sweep curve parameters never reach the audio live** (T1.17): the processor applies the custom curve only
    from `setState`; a curve edited in the UI or loaded through the preset browser is not applied until the next
    state load. Also, a controller that never received a component state writes the registered defaults as a
    2-point curve (0,0)→(1/7,1/7). Relates to D-069 / T2.22 (custom-curve editor view unregistered).
20. **Follow-up 15 measured again after the codec (T1.17 `a1bfa30d`)**: fresh processor still drifts at bytes
    108/166/175/194; on Dirty_Sub / Rectified_Drums / Radial_Blend the drift sits at bytes 136 and 149 (LFO 1/2 rate)
    before the codec and at byte 92 (sweep LFO rate) after. Probe: `[.][t117-ulp-probe]`.

21. **Tube slot-2 labels name tube types ("12AX7, EL84, 6L6, 300B") but the DSP runs 1–4 cascaded `TubeStage`
    passes** (`stages = satStage + 1`). Count matches, meaning does not (T1.19 survey). Relabel ("1 stage … 4
    stages") or implement tube models.
22. **HalfRectify DC-block ToggleButton on slot 2**: range is now 0..1 after T1.19 (`473a18d1`), but its registered
    default is 0.5 — whether that draws as on or off (and which the DSP applies) was not measured.
23. **`MorphNode::shapeSlots` and the Shape-slot registration defaults are still 0.5**: a SergeFold node that never
    passes through `ShapeShadowStorage` decodes slot 2 as Buchla259, not Serge. Decide the per-type defaults once
    (T2.06 territory) and apply them in both places.

24. **Activation burst on silent input** (T1.22 survey, 2 s of digital silence right after activation): Tube
    −31 dBFS peak 0.88, Tape −27 dBFS peak 0.86, SineFold/SergeFold −31 dBFS peak 0.99, BitwiseMangler −30 dBFS,
    Fuzz/AsymFuzz −38…−42 dBFS, TriangleFold −43 dBFS, Fractal −52 dBFS — all die out within the first second
    and read exact 0 afterwards. Root cause not identified (uninitialised smoother/filter state at prepare?).
    Relates to item 18 (`setActive` reset gap). Fractal keeps a −126.6 dBFS floor.
25. **Tube runs `satStage + 1` passes through ONE `TubeStage`** (distortion_adapter.cpp), so its smoothers and
    internal DC blocker see interleaved samples — the reason Tube read DC −0.047 even at bias 0. T1.22
    (`abb5fd57`) masks it with the adapter's 10 Hz DC blocker; the cascade needs one `TubeStage` per pass.
    — done in T2.02 (commit "fix(disrumpo): Tube cascade runs one TubeStage per stage"; a commit cannot name its
    own hash).
26. **`DynamicNodeSelector::rebuildSegments(1)` calls `setSelectedSegment(0)` which divides 0 by 0** (T1.20 left
    it; the new sync code skips one-segment selectors).
27. **`Controller::morphPads_` holds raw view pointers** like the selectors T1.21 fixed; they live in the expanded
    strip (not a switching page) so no UAF today, but the same `viewWillDelete` guard belongs there.
28. **High-contrast mode on first editor open**: a Shape page built before `didOpen` reads `accessibilityPrefs_`
    before they are set (T1.21 note); previously only selectors alive at open got it and lost it on tab switch.

29. **AU: `aufx` effect with a MIDI event input is untested** (T1.18 `abbae8a3` added `addEventInput("MIDI In", 16)`
    to the processor). The AU v2/v3 wrappers may need `audiounitconfig.h` / plist changes (or the type to become
    `aumf`) for auval to pass; the macOS CI leg is the first check. Also untested: a real host's
    `IMidiLearn::onLiveMIDIControllerInput` calls.
30. **`granular_filter.h:398` derives a grain's slot from its address / sizeof(Grain) mod 64** — should use the new
    `GrainPool::indexOf` (T1.23 `39691539`).
31. **Fast-math hazards in `dsp/tests/unit/processors/granular_distortion_test.cpp`** lines 81 (`std::isnan/isinf`)
    and 1039/1044 (infinity constants) — flagged by `macos-fastmath-repro.sh` (`-Wnan-infinity-disabled`); existing
    lines, outside T1.23's scope; belongs to the IEEE-test split (`dsp_ieee_tests`).
32. **`getState` (nothing pending) and `setState` (filling fields a short stream lacks) still read live DSP objects
    on the host thread** while the audio thread may write them via parameter changes — a read-only race left after
    T1.24 (`a1c96514`); the two-thread TSan run is T5.08.

33. **GranularDistortion sums up to 8 overlapping grains with no normalisation** (shared header): ≈ +13 dB of gain at
    density slot 1.0 (T2.04 `7a720c87` measured −20 dBFS in → −6.9 dBFS out after neutralising drive). Decide a
    per-grain scaling (1/√N or 1/N) in the shared processor; Ruinae consumes it too.
34. **Two bit-depth controls for Bitcrush**: the shape slot (4–16 since T2.05 `1e1e3ce8`) and a separate per-node
    `kNodeBitDepth` parameter (4–24, processor_params.cpp). Decide which one is the control and retire the other.
35. **Factory presets with an active Feedback node now sound different** (T2.03 `8156e34c`): Thr direction flipped
    (default slot 0.8 goes from −19.2 dB to −4.8 dB) and Curve is now live (default 0.5 = ReciprocalSqrt instead of
    the fixed Tanh). Not scanned yet; belongs with T3.01 preset re-levelling — same for T2.05's Bitcrush shift (16
    presets, +1.5–2 bits) and T1.08's +3 dB centre-pan correction.
36. **`std::isnan` in `dsp/tests/unit/processors/feedback_distortion_test.cpp` lines 136–169** trigger
    `-Wnan-infinity-disabled` under the macOS fast-math repro (pre-existing since e6680c93); move those cases to the
    IEEE test executable or use `Krate::DSP::detail::isNaN`. Same class as item 31.

37. **Wavefold drive normalisation (D-159 "consider normalising the drive into the fold")** was not done in T2.06
    (`b7ff8881`). With the plan's fix, fresh fold nodes are no longer near-silent (TriangleFold −26.3 → −3.9 dBFS at
    drive 2.5) but peak still falls with drive (SineFold −0.7 → −5.1 dBFS from drive 2.5 to 10) because the tone
    filter removes the upward-moving fold energy. Decide whether to normalise.
38. **T2.06 makeup gain made 20 factory presets louder**: Wide_Sweep +13.4, Chaos_Engine +12.1, Wavefold_Bass +9.5,
    Serge_Madness +4.5, Wavefold_Lead +3.75, Wavefolder_Quad +3.4 dB, … (full list in the `b7ff8881` commit
    message). Must be re-levelled in T3.01 together with item 35.
39. **Quantize dither on silence**: a fresh Quantize node (dither 0.5, offset 0.5) outputs −40.5 dBFS of dither
    noise on digital silence; offset 0 is silent. D-143's own probe (default slots) still fails. Gate the dither
    on input energy or apply it only around non-zero samples.
40. **Quantize at Levels 0 is now a 3-level grid with a dead zone** (T2.07 `c24c8cfd`): a −6 dBFS sine's fundamental
    goes from +2.1 dBFS (old 2-level square) to −21.6 dBFS. Intended by the mid-tread design, but it changes that
    setting's character; audition.

## Done outside the plan

- `68255845` fix(dsp): CrossoverLR4 stages in double precision — latent float32 defect (DC gain error up to
  1.18 dB at 20 Hz / 192 kHz; 0.12 dB at 112 Hz / 192 kHz) exposed when T1.03 removed the start-up glide that
  had masked it. CROSS-PLUGIN (every crossover consumer).
- `e498752d` test(profundum): regenerated evidence after that fix (109 CSVs; residual floors dropped 20–45 dB,
  e.g. sc017 aliased_L_dbfs −88.96 → −132.54; nine pass cells flipped 0 → 1, none 1 → 0).
