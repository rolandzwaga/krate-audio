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
   Also (T2.20 `e720284c`): 4 bands × 4 Formant nodes at the morph centre measure 14.27 % and 4 bands × mixed
   cross-family types 21.66 % even after control-rate routing cut the per-engine cost 3.64× (0.669 % vs 2.437 %).
   The fix plan's 5 % band-level budget for that case was not adopted (unmeasured, not a spec number); probe
   `[.][t220-cpu-probe]`.
   — T5.09 (2026-11-09): `integration/processor_cpu_test.cpp` now times `Processor::process` (44.1 kHz, 512,
   [performance]). Budgets: 5 % for 4 bands at 4x one node per band (003 PT-DI-002; PT-DI-003 is 8 bands, so the
   "10 %" above misread it), 15 % for 4 bands x 4 nodes (specs-overview / 009 SC-001 / PT-DI-004). Four
   bit-identical optimisations (morph weights cached, engine and adapter block paths, BiquadCascade sample-major
   then SSE2-skewed with a stereo pair in the IIR Oversampler). CPU lane, baseline -> final: HardClip x4 at 4x
   17.70 -> 4.02 % GREEN; 4 Formant 21.72 -> 11.35 % GREEN; HardClip/SineFold/FullRectify/RingSaturation at 4x
   38.53 -> 14.88 % GREEN (2 % margin); Formant/Chaos/Granular/Fractal 47.36 -> 26.98 % **RED, needs a ruling**.
   Profile of the red case: FractalDistortion's Harmonic mode (the default node: mode slot 0.5 = Harmonic, about 5
   iterations, two 8-term Chebyshev Clenshaw sums plus two waveshapers per iteration per sample) 35 %, Granular 21 %,
   Waveshaper / tanh 15 %, the oversampler about 3 %. Fractal alone at zero cost would leave about 18 %; no
   bit-identical refactor closes it. Options: cheaper Fractal Harmonic / Granular algorithms (a sound change beyond
   float noise, needs a ruling), or lighter defaults for those nodes (also a sound change).
   — T5.09 near-identical pass (user ruling 2026-11-09: float-noise changes allowed at <= -100 dB residual; also bring
   the mixed 4x case to <= 13.5 %): STOPPED, nothing committed but the probe. `[t509-render-dump]` now also renders
   each node type of both cases alone at 44.1 and 96 kHz. (1) Fractal Harmonic's two Chebyshev mixes as a Horner
   polynomial in double (mathematically the same mix, closer to exact): residual -52.8 dB (Fractal alone, 44.1 kHz),
   -48.6 dB (96 kHz), -80.8 dB in the budget mix, max |diff| 4.9e-3. The Harmonic recursion (residual x drive x
   scale, about 5 levels) amplifies a 1e-7 change to -50 dB, so no float-level rewrite of anything feeding a Fractal
   node meets -100 dB; only bit-identical changes qualify there. CPU of that rejected change: budget mix 40.0 ->
   35.7 % on a loaded machine (-11 %). (2) processRaw forced inline into the adapter span loop: bit-identical, no
   gain (cross 15.19 -> 15.45 %, noise). Remaining near-identical candidates (RingSaturation phase reciprocal, one
   finiteness test in OnePoleLP, GrainPool active list) are 1-2 % each, inside the A/B noise. Sound-changing options
   for the red case, estimated from the profile (Fractal Harmonic 35 %, Granular 21 %, waveshapers 12 %, nodes about
   85 % of the case): Experimental-family types at 1x instead of 2x (oversampling choice; about -11 points, lands near
   15 %); fewer default Fractal iterations (cost is linear in iterations: 5 -> 3 about -4 points); the Horner rewrite
   above (about -3 points); a lower default grain density (about -2 to -3 points). The mixed 4x case stays at 14.88 %
   (budget 15 %): its time is the 4x oversampler (10 %), RingSaturation (15 %), the adapter dispatch (11 %) and the
   per-node tone / DC filters (10 %), none with a near-identical lever above the noise.
   — USER RULED 2026-11-09: log it and move on. The Formant/Chaos/Granular/Fractal budget case stays RED in the CPU
   lane (not per-push CI); choosing among the sound-changing options above waits for a sound-design session where the
   user can audition renders. Budget and workload stay as they are.
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
   — done in T2.10 (commit "fix(disrumpo): report and compensate oversampling and Spectral latency"; a commit
   cannot name its own hash).
6. **Fades under modulation.** Each oversampling factor change now fades the band down and up over 5 + 5 ms.
   If morph modulation keeps crossing a factor boundary this could read as tremolo. Not measured yet.
   — Measured by T6.06 (2026-11-09), needs a ruling: the plan's level bar on a cross-family morph sweep through the
   Processor (every 20 ms window within 1 dB of the straight dB line) is RED on the dip alone. 2-node SoftClip ->
   SineFold, Linear1D, 220 Hz -12 dBFS, cursor 0 -> 1 over 2 s: the band switches 2x -> 4x at 0.576 s (x = 0.04, as
   soon as SineFold's weight passes the 0.001 threshold) and the 20 ms window holding the 5 + 5 ms dip reads
   -1.77 dB (a full dip to 0: (10 + 10/3) / 20 of the energy); with Oversample Max 1x or 2x the sweep reads -0.32 dB.
   Jump ratio 1.01 and 0 clicks (asserted). Options: (a) crossfade the two factors' outputs instead of dipping (both
   paths run for 5-10 ms: a CPU spike, and the old factor's state must keep running); (b) hysteresis on the
   morph-weighted factor so a sweep switches once and late (does not remove the dip); (c) accept the dip by ruling and
   re-scope the level bar to the windows away from a factor switch. Held case: `f:/tmp/disrumpo-fix/p6b/
   t606-held-bars.patch`; probe `[.][p6b-morph]` in `tests/dsp/morph_continuity_test.cpp`. (A same-factor morph that
   flipped factors 12 times in 2 s was a rounding defect, fixed: "Done outside the plan".)
   — CLOSED 2026-11-09 (user ruling: option (a), crossfade; commit "fix(disrumpo): oversampling factor and
   alignment changes crossfade instead of dipping"). Each BandProcessor holds two lanes (shapers, morph engines,
   2x / 4x oversamplers, fractional delays; the second on the heap, prepared with the first); a change of factor or
   of alignment (latency target, 4x context, reference shift) runs the idle lane at the new settings beside the
   running one, its processing state cleared and its morph cursor taken over, for 10 ms, and crossfades LINEARLY
   (the lanes carry the same signal: equal-power measured +1.18 dB on the 2-node sweep, +1.65 dB on the band case,
   +2.29 dB on the context case, jump ratio 1.39-1.40; linear -0.31 / +0.001 / -0.31 dB). Output stage and sweep
   smoother advance once per host sample. The processor's dip remains only where the REPORTED latency changes (and
   on a band-count change, D-023); an alignment-context change at an unchanged latency crossfades the bands and the
   dry path (two dry allpasses). Before audio and at the dip's bottom the running lane takes the change at once.
   A latent node in the cleared incoming lane (Spectral, Granular) outputs nothing for its type latency, so the
   incoming gain holds at 0 until it has passed (case "A factor crossfade into a lane with a Spectral node waits
   for its latency", fft 512: HEAD's dip left a fully silent 10 ms window at 4x -> 1x and -1.71 dB at 1x -> 4x; a
   crossfade without the hold fell 15 dB 7-13 ms after the jump; now -0.41 / -0.33 dB).
   2-node sweep -1.77 -> -0.31 dB (asserted <= 1 dB); new cases "BandProcessor: factor switch crossfades the two
   factors without a dip or a step" (-4.15 -> +0.001 dB outside the before/after range) and "An alignment-context
   change at an unchanged latency crossfades without a dip" (-8.54 -> -0.31 dB). Cost: steady state unchanged (one
   lane runs); a block that starts a 4x <-> 2x crossfade 569 us against 132 us steady (4.3x, 5.3 % of a 512-sample
   block at 48 kHz, once per switch); memory: a prepared BandProcessor 24.85 MB at 48 kHz with two lanes, so about
   +12.4 MB per band (+50 MB per 4-band instance; a prepared MorphEngine is 5.22 MB). Still held: the 4-node
   sweep's level and clicks (items 12 and 84).
7. **Allpass compensation filters are still float32 Biquads** (Crossover3Way/4Way and Disrumpo's
   CrossoverNetwork, now also the dry-path chain from T1.09 `a2f7fe51`). Measured: at 192 kHz the dry/wet
   alignment residual is −59.55 dB (bar −60 dB; 44.1–96 kHz read −78 to −83 dB), flatness still 0.0128 dB; a
   double-precision allpass chain of the same design reads −123 dB in the probe (`[.][t109-probe]`). Fix =
   double-precision allpass stages for both chains. Also from T1.09: the LR4s glide on a frequency change while
   every allpass jumps (brief mismatch, settles flat), and the dry term skips input gain (T2.09 territory).
   — closed by crossover dedup step 4 (commit "fix(dsp): MultibandCrossover allpasses in double precision, tracking
   the gliding crossovers"): double-precision allpasses that follow each LR4's current frequency; dry/wet residual
   −141 to −167 dB at 44.1–192 kHz, 192 kHz back in the D-018 test. (The dry-term input gain remark is T2.09's.)
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
    — T6.06 (2026-11-09): through the Processor the same 4-node diagonal sweep (drive 3, 220 Hz -12 dBFS) reads max
    level deviation +2.77 dB (start -3.40, end -17.83 dB): +1.31 / +1.33 dB with the band held at 2x / 1x (the law),
    the rest the 2x -> 4x switch dip at 0.555 s (item 6). Held with the plan's 1 dB bar in
    `f:/tmp/disrumpo-fix/p6b/t606-held-bars.patch`; the jump ratio (1.00) is asserted.
    — After item 6's crossfade (2026-11-09, probe `[.][p6b-morph]`): +2.47 dB (start -3.40, end -17.15 dB); the switch
    no longer dips. What remains: this blend law (+1.31 / +1.33 dB with the band held at 2x / 1x, where the end reads
    -14.01 / -14.00 dB) and the RingSaturation end node being 3.15 dB quieter at 4x than at 1x / 2x (-17.15 against
    -14.0 dB), which tilts the straight dB line the windows are measured against. Still held (blend law unchanged by
    ruling); the RingSat 4x level belongs with item 84.
    — After item 84's fix (2026-11-10): +1.33 dB (start -3.40, end -14.14 dB): the switch no longer moves the RingSat
    end node; what remains is this blend law (+1.31 / +1.33 dB with the band held at 2x / 1x). Still held.
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
    — CLOSED 2026-11-09 with item 87 (user ruling: one precedence, the slot wins; no state-format change).
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
    — USER RULED 2026-11-09: intended behaviour (the dither keeps running on silence). "Type render matrix: Quantize"
    exempts Quantize from the -90 dBFS bars on silence (self-noise -40.5 dBFS, tail -40.7 dBFS) and asserts the rest.
40. **Quantize at Levels 0 is now a 3-level grid with a dead zone** (T2.07 `c24c8cfd`): a −6 dBFS sine's fundamental
    goes from +2.1 dBFS (old 2-level square) to −21.6 dBFS. Intended by the mid-tread design, but it changes that
    setting's character; audition.

41. **Global mix 50 % with an oversampling band: −4.35 dB at 18 kHz** (T2.10 `94141096`). The dry path is delayed
    to the reported latency, but a pure delay cannot match the oversampler's IIR phase near the top of the band
    (−0.09 dB at 5 and 10 kHz). Matching it means running the dry path through the same oversampler filters, which
    only works when every band runs at one factor. Decide whether that matters.
42. **Delay jumps without a dip** (T2.10): crossing drive 0 changes a band's factor without the fade, so its delay
    jumps by up to 7 samples; a node-type or Spectral FFT-size change moves the band's own delay at once (other
    bands change at the bottom of the global dip). Route both through the existing 5 ms dip.
43. **Reported latency is 0 before `setupProcessing`** (oversamplers measured in prepare); pluginval's info line
    shows 0. Harmless if hosts re-query after setup; check a real host's PDC.

44. **Tape spikes on the parallel morph path**: 2 nodes SoftClip/Tape in steady state spike from ≈0.24 to 0.68 within
    5 samples, twice per cycle (same on the original code; 1-node Tape does not). Found by T2.13's measurements.
45. **Click at the end of the oversampling dip**: a parallel→shared switch that also changes the factor
    (SoftClip/SineFold → SoftClip) leaves 1 click at 9.98 ms, where the band's fade-in ends (T2.13).
46. **Linear1D: UI and oversampling still use the corner positions** (T2.11 `6f9790cb` moved the DSP to i/(N−1)):
    the morph pad draws nodes at the corners, and the band's oversampling choice weights nodes by 2-D distance.
47. **Spectral switch still clicks twice** (T2.13 `866c042e` measured): at the switch and where the first frame
    leaves the 2047-sample latency gap (47.65 ms) — the type-change half of item 42.

48. **FeedbackEcology CPU budget is red in the CPU lane, before and after T2.17** (Vorago/bloom/ecosystem engines):
    reference arm ≈113.5k ns vs the 106,667 ns line in alternating old/new pairs run alone; the wander-off arm got
    ≈11 % slower with T2.17 (87.7k → 98.4k ns) but stays inside its limits. Not a Disrumpo task — needs the CPU
    lane owner's look (never relax the budget).
49. **EnvelopeFollower RMS mode still latches NaN from a FINITE input whose square overflows** (|x| > 1.8e19): T2.17's
    input check (`879dfef3`) does not cover it. Clamp before squaring or check the squared value.
50. **Drive is a plain input gain below the shaper's knee** (T2.18 `37a64ede` measurements): SoftClip at −20 dBFS reads
    −63 dBFS at drive 0.01 and −43 dBFS at 0.1, so the level fall now sits inside drive 1e-3 → 1e-2 (−39 dB) instead of
    at the gate. Whether low drive should keep unity level (drive-dependent makeup) is a design question; ties to
    items 9/17 (Tube) and 37 (fold normalisation).
    — High-drive half, USER RULED 2026-11-09 (follow-up 79): Fuzz, AsymmetricFuzz, FullRectify and HalfRectify kept
    rising with drive (T6.01, -6 dBFS 1031.25 Hz sine, peak at drive 7.5 / 10: Fuzz +7.48 / +9.42, AsymmetricFuzz
    +7.02 / +8.94, FullRectify +4.98 / +7.48, HalfRectify +7.78 / +10.26 dBFS against the +6 dBFS bar). Commit
    "fix(disrumpo): Fuzz, AsymmetricFuzz and the rectifiers stop following drive above 5": the adapter scales
    their output by 5 / drive above drive 5 (T3.01's digital-type normalisation with the knee at 5 instead of 1).
    Drive 1 / 2.5 / 5 unchanged (bit-identical branch), 7.5 -3.52 dB, 10 -6.02 dB. After: Fuzz +3.96 / +3.40,
    AsymmetricFuzz +3.49 / +2.92, FullRectify +1.46 / +1.46, HalfRectify +4.26 / +4.24 dBFS. Full Rectify is
    scale-invariant, so its Drive now has no audible effect above 5. Presets: 9 of 120 change, all quieter by
    0.01 to 0.40 dB RMS (list in the commit message); none was more than 3 dB quieter, so no Output Gain was
    restored. The low-drive half (unity level below the knee) is still open.

51. **Innexus still adopts a stored SharedDisplayBridge instance ID the old way** (duplicate-ID collision when two
    instances restore the same stream). T2.19 (`7dd33372`) added `registerInstanceIfFree` /
    `unregisterInstance(id, data)` to the shared bridge; Innexus should use them too.
52. **A duplicated Disrumpo instance's controller still reads the other processor's instance ID** from the shared
    stream; only the processor side was fixed in T2.19. Send the live ID processor → controller by message.
53. **Tier-3-only hosts (no DataExchange) never deliver mod offsets to the slider overlay** — same as before T2.19
    (the old pointer message also needed a connection). Feed the overlay from the Tier-3 shared memory too.

54. **uidesc `visible="false"` on the expanded band containers does nothing** (VSTGUI has no such attribute): all four
    expanded panels start visible headlessly; a real host hides them through a delayed update with a collapse
    animation, so they may flash on editor open (T2.26 note). Not verified in a host.
55. **Older visibility controllers are stored with `IPtr = new …` and never released** (small controller leak; new
    observers use `owned()`). Found in T2.26.
56. **33 registered defaults still differ from what a fresh processor reports** (T2.24 `a59f2da4` diagnostic; the
    defaults probe cannot see them because each starts on its own registered default): Crossover 1–3, Sweep Falloff
    (registered Hard, processor Smooth — the new segment control shows Hard until state arrives), Sweep LFO Note /
    LFO 1-2 Note Value (the editor shows "1/64T"), LFO 1-2 Retrigger, Env Source, Chaos Speed (also not converted
    to a range parameter), Rungler Rate (fresh processor reports 1.0 vs registered 0.08) / Depth / Bits, Curve P1
    X/Y, all 16 node Folds. Make the processor's initial values and the registration agree, one table.
    Chaos Speed's default and range are done (item 75); the rest stays open.
57. **Review the T2.22 sweep-row layout** (`acd45a32`): the row grew 20 px taller instead of wider (no mockup detail;
    the 1000×600 editor is specified) and now sits directly under the band panels; screenshots under
    `f:/tmp/disrumpo-fix/t222-shots/` (also sent to the user). The hidden `[.][screenshots]` case now exists for
    Disrumpo; `tools/gen-screenshots.js` does not list Disrumpo yet.
58. **`controller/views/custom_curve_editor.{h,cpp}` is no longer referenced but still built** (T2.22 deleted its
    creation). Delete the files or re-enable the editor when follow-up 19 (custom curve applied live) is done.

59. **Factory preset loudness spread is 22 dB** after T3.01 (`d91c4b7e`, 2 s pink noise peak −12 dBFS): min −27.2
    dBFS (Morph/Rectify_Blend), median −12.4, max −5.65 (Experimental/Alien_Voice). T3.01 only lowered gains to meet
    peak ≤ −0.3 dBFS (66 presets, −0.1 to −9.8 dB); evening out loudness is a voicing decision. Per-preset table:
    `f:/tmp/disrumpo-fix/t301-preset-table.md`.
60. **Follow-up 16 measured on the final presets** (T3.01): with the global Sweep Morph Link active, Formant_Vowels'
    link contribution is as loud as the preset itself (difference −10.15 dBFS), Ease_In_Morph +3.3 dB louder,
    Wide_Sweep −15 dBFS difference; Step_Scanner / Wah_Wah_Drive / Wide_Sweep would peak +0.5 / +1.3 / +1.5 dBFS
    if the link were set to None (their trims assume the link). Decide with item 16.
61. **Granular outputs DC on noise but not on a sine** (T3.01 found it while tracing preset DC; now masked by the
    adapter's 10 Hz blocker, which T3.01 extended to SineFold/TriangleFold/SergeFold/Chaos/Granular — kept by my
    ruling: a distortion stage should not emit DC). Root cause not investigated.

62. **Vendored-code warnings in a full Release build** (T3.04 `9d436a46` measured; first-party code is at 0): 4 × C4244
    in pffft `pf_cic.cpp`, 2 × C4267 in vstgui `ctexteditor.cpp`, 1 × C4388 in vstgui `cshadowviewcontainer.cpp`.
    Decide: patch the SDK/pffft, or mark their includes SYSTEM the way T3.04 did for Highway.
63. **`disrumpo_uidesc_inventory` ctest does not gate in CI** (T3.03 `67c5d787`): CI does not run ctest. Add a lint
    step to `ci.yml` (needs `fast-xml-parser` installed) to make the uidesc check (with the 19-entry allow list
    `plugins/disrumpo/tests/uidesc_overlaps_allowed.json`) gate pushes.

64. **Follow-up 46, pad half still open after T4.02** (`1ecbb6d6` fixed the oversampling half): the morph pad still
    draws nodes at the corners in Linear1D, and nothing in the controller calls `MorphPad::setMorphMode`, so the
    pad always shows Planar2D weights whatever the band's mode.
65. **LFO note-value default disagrees**: the registration default is 1/8 (T4.04 `a60137c9`), the processor's LFOs
    start on 1/4 — a host reset and a fresh instance differ (part of item 56).
66. **Spec says Symmetry 0–1, the code maps it −1..1** (T4.06 `6feb4498` left it). Rule which is right.
67. **Shape-slot values the adapter maps further show as a percent of the stored value** (e.g. Feedback Threshold,
    which runs in dB, and filter frequency) — the descriptor table from T4.03 (`db3a5e7a`) can carry units/curves.

68. **AllpassChain under full fast-math is not bit-identical to the pre-T4.16 code** (max 1.0e-3 in the macOS repro;
    the test TU is IEEE on macOS CI and passes there). A macOS shipping build of Disrumpo's AllpassResonant /
    AllpassChain may not reproduce its previous output bit for bit. Listen-check only; no correctness defect.
69. **`AllpassSaturator::updateChainFrequencies()` / `updateMatrixFrequencies()` are declared but never defined**
    (pre-existing dead declarations, `allpass_saturator.h`).
70. **The [long] lane was not run for T4.13–T4.17** (nightly CI covers it); the Profundum evidence writers were, and
    rewrote 123 CSVs with content identical to HEAD (no evidence commit needed after T4.13/T4.17).
71. **`mass_dynamics_oversampling_test.cpp` assertion count depends on run order** (found by the run-gate interleave
    fix, `b1768fed`): `MassDynamics_Oversampling{NoAllocation,Partition,Transfer,StateFinite}` each record 6 fewer
    assertions when run after the earlier cases of the old contiguous shard 27 (dsp_systems_tests 16040734 vs
    16040758 assertions, same case set). Cause not found; some state outlives a case.
72. **Other sharders still cut contiguous chunks**: `tools/run-close-lanes.js` (`--suite-shards`) and
    `tools/ci-test-shard.js` (CI, spans machines). run-gate.js does not export its dealing logic. The `*-phase.js`
    workflow prompts still describe `--shard-count/--shard-index` (left unedited: changing a prompt breaks resume
    of an in-flight phase run).
73. **clang-tidy coverage of plugin tests is inconsistent (all plugins)**: `tools/run-clang-tidy.sh` tidies every
    plugin's `tests/` folder, `tools/run-clang-tidy.ps1` tidies only `src/`, and only `dsp/tests/.clang-tidy`
    disables the Catch2-macro checks (`avoid-do-while`), so the `.sh` reports Catch2 do-whiles across plugin test TUs
    (seen on Disrumpo's crossover_network_test.cpp / processor_audio_output_test.cpp in dedup step 3). User ruled
    2026-11-09: log it, decide the repo-wide lint policy later (options: one shared override under `plugins/`, or
    per-site fixes).
74. **A custom-curve point at exactly x = 0 or 1 has no defined order against the endpoint** (T4.20): `CustomCurve`
    sorts by x alone (`std::sort`, not stable), so a stream holding an interior point at x = 1.0, e.g. (0, 0.1),
    (1, 0.5), (1, 0.9), loads as (0, 0.1), (1, 1.0), (1, 0.9) on MSVC: the default endpoint y lands in the interior
    slot and the segment below x = 1 heads for 1.0, not 0.5. Load terminates and the curve is well formed; the
    values of such a degenerate curve are implementation-defined. Rule whether interior x is kept strictly inside
    (0, 1) or ties break by role.
75. **Chaos Speed's registered plain value is not what the processor runs** (T5.02, measured by "Registered plain
    value matches the processor's applied value"): it is registered as a plain 0..1 `Parameter` (no range, no unit)
    while the processor decodes `kChaosSpeedRange` (0.05..20); at norm 0 / 0.25 / 0.5 / 1 the host shows 0 / 0.25 /
    0.5 / 1 and the processor runs 0.05 / 5.04 / 10.03 / 20. The fix is one registration line in the D-169 pattern
    (`makeLinearRangeParameter` over `kChaosSpeedRange`, plus a unit), plugin source and so outside Phase 5's
    test-only scope. The case pins it (CHECK that it still disagrees): the fix turns it red and moves Chaos Speed into
    the checked set. Part of item 56's table. The same case leaves the 16 custom-curve point parameters unchecked
    (item 19: not applied live).
    — closed by commit "fix(disrumpo): Chaos Speed registered over the range the processor runs" (user ruled
    2026-11-09: fix it): a linear range parameter over `kChaosSpeedRange`, unit "x", default 1x
    (`ChaosModSource::kDefaultSpeed`), same ID, stepCount 0. The T5.02 pin is gone and Chaos Speed is in the checked
    set (RED 4 of 1296 comparisons wrong, GREEN 0 of 1296); "Registered plain value matches processor
    denormalization" has a Chaos Speed row (RED 3 of 72, GREEN 0 of 72); new case "Chaos Speed's registered default
    is what a fresh processor runs" (RED 0.048 vs 1, GREEN 1 vs 1).
76. **Test-support headers are outside clang-tidy's header filter** (found while de-duplicating the headless
    editor, 2026-11-09): `.clang-tidy`'s `HeaderFilterRegex` covers `plugins/*/src` only, so
    `plugins/disrumpo/tests/test_support/*.h` is never reported. Tidying editor_lifecycle_test.cpp with
    `--header-filter=".*test_support.*"` shows 14 hits in `disrumpo_rig.h` (implicit bool → TBool ×4, isolate-declaration
    ×2, signed/unsigned comparison ×2, non-virtual public destructor ×2, nested conditional, make-member-const,
    const_cast, and `res == kResultOk || res == kResultTrue` at :294, harmless since both are 0); the new
    `headless_editor.h` reads 0. Decide with item 73's policy whether test-support headers are tidied, then fix
    `disrumpo_rig.h`.
77. **Innexus has the same unconditional `[!mayfail]` on its DataExchange delivery cases** (T5.07, report D-128
    context): `plugins/innexus/tests/integration/test_data_exchange_pipeline.cpp:213, :234`. Disrumpo's case now
    asserts delivery under `#ifdef _WIN32` and the Tier-3 FIFOs everywhere; Innexus would need the same split
    (outside this audit's plugin).
78. **The two-thread setState case cannot fail** (T5.08, "setState from a second thread during processing keeps
    the output finite", `[long]`): with setState applying on the calling thread again (the pre-T1.24 race, local
    break `deferred = false`) it stayed green in 8 runs: 3 paced (200 loads, each applied between blocks) and 5
    back to back (200 loads during ~18 blocks), 0 non-finite samples, no crash. The DSP objects are preallocated,
    so the race mixes parameters without producing a non-finite sample or a fault. No `build-asan` exists, so it
    has no ASan run either. It is kept as the plan's smoke test and is not counted as coverage; the
    deterministic D-003 guard is still "setState during processing does not touch live DSP objects". A detector
    that can fail would record the thread of every live-DSP write (e.g. the `debugSetNodesCalls` /
    `debugSetBandCountCalls` hooks noting `std::this_thread::get_id()`) and require the audio thread. It also runs
    in under 1 s, so CLAUDE.md's `[long]` rule (> ~15 s) does not call for the tag the plan gave it.
79. **T6.01 type render matrix: 10 type cases held back RED, need a ruling** (2026-11-09). `tests/dsp/
    type_render_matrix_test.cpp` asserts every plan bar for the 16 green types; the cases of these 10 types are in
    `f:/tmp/disrumpo-fix/p6a/t601-held-types.patch` (adds the 10 `TEST_CASE` lines, nothing else). Values at drive
    2.5 / 7.5 / 10, 48 kHz, 1031.25 Hz at -6 dBFS:
    - Non-harmonic > -50 dB: Tape -26.7 / -26.2 / -27.1 dB (the default Hysteresis tape model in the shared
      TapeSaturator; Simple model -87.5 dB, flutter 0 -26.0, 1x -24.0, so neither flutter nor aliasing); Fractal
      -40.9 / -32.4 / -32.2 at 2x (1x -24.6; 4x would grow follow-up 1's red Fractal CPU case); TriangleFold -43.6 at
      drive 10 (1x -16.3); HalfRectify -42.3 at drive 2.5 (1x -29.5); RingSaturation -33.6 / -29.4 / -30.2 (ring
      modulation is inharmonic by design: Fixed carrier -0.7 dB, depth 0 -129.6 dB; propose listing it with the
      noise-by-design types).
    - Peak > +6 dBFS: Fuzz +7.50 / +9.44, AsymmetricFuzz +7.03 / +8.96, FullRectify +7.50 (drive 10), HalfRectify
      +7.80 / +10.28 dBFS. Drive is input gain into stages that do not saturate (follow-up 50, D-144); the audit read
      them 3 dB lower through the centre-pan loss and the mis-rated tone filter, both since fixed.
    - Quantize self-noise -40.5 dBFS, tail -40.7 dBFS: the dither on silence of follow-up 39.
    - Stochastic |DC| 0.00253 at drive 7.5 / 10 and bias -1 / +1 over 1.02 s (0.00042 at drive 2.5): slow random
      drift below the adapter's 10 Hz blocker; over the probe's 0.17 s window it read -0.0016 to +0.0007. A
      noise-by-design type: rule the bar or the window.
    Each needs a sound/DSP decision (shared TapeSaturator or Fractal/fold oversampling, level design, dither gating) or a
    ruling to exempt the type from that bar; none has an obvious Disrumpo-only fix.
    — USER RULED 2026-11-09 and applied: RingSaturation (non-harmonic), Stochastic (DC) and Quantize (the -90 dBFS bars
    on silence) exempt as intended behaviour (`500ed9bd`); Fuzz, AsymmetricFuzz, FullRectify and HalfRectify normalised
    above drive 5 (`61512a54`, follow-up 50) and the first three cases committed (`9eebcbfa`). Still held back RED,
    on the non-harmonic bar only: Tape, TriangleFold, HalfRectify, Fractal. Investigated (no fix), hidden probes
    `tests/dsp/phase6_ruling_probes_test.cpp` ([p6r-split], [p6r-tape], [p6r-tape-sag], [p6r-adaa]): the node as a
    DistortionAdapter clocked at 48 kHz x N with the Processor's node params, its non-harmonic power read below
    23.9 kHz (an ideal decimator), a lower test tone, and a periodicity check (y[n + 8192 N] - y[n], the sampled tone
    repeats every 8192 N samples). For every type the Processor's IIR-oversampled reading equals the ideal reading at
    the same factor (HardClip control at 4x: -79.3 vs -79.8 dB), so the oversampler filters do not leak.
    - HalfRectify (re-measured after the normalisation: -42.3 / -51.7 / -54.1 dB, unchanged): aliasing of a step.
      The default threshold 0.5 makes the output jump 0 -> 0.5 at every crossing (and a -6 dBFS sine at drive <= 1
      never reaches it: the fresh node is silent there). Periodic to -343 dB; drive 2.5 falls 6 dB per doubling (1x
      -29.5, 2x -36.0, 4x -42.2, 8x -48.2, 16x -54.3) and with a lower tone (4x: 515.6 Hz -45.6, 257.8 Hz -48.7).
      Options: (a) first-order ADAA on the shaper (antiderivative (x^2 - t^2)/2 above t): bare shaper at 4x, drive
      2.5 -36.4 -> -60.8 dB, drive 10 -48.4 -> -73.1; Disrumpo-only, one antiderivative and one division per clocked
      sample, half a clocked sample of delay and about -0.5 dB at 20 kHz at 4x, otherwise the same sound. (b) A
      continuous transfer (x - t above t): -71.1 dB plain at 4x, but no step and 0.5 less output: a sound change.
      (c) 16x: bands stop at 4x, and 4x the cost. (d) Exempt drive 2.5 (ruling).
    - TriangleFold (-65.6 / -51.2 / -43.3 dB at 4x): aliasing of the fold corners (folds 2, threshold 0.5, x2 makeup;
      more corners per cycle as drive rises). Periodic to -337 dB; drive 10 falls about 12 dB per doubling (1x -16.6,
      2x -32.8, 4x -43.4, 8x -58.1, 16x -68.4) and with a lower tone (4x: 515.6 Hz -56.8, 257.8 Hz -67.1). Options:
      (a) ADAA1 on the triangle fold (closed-form periodic antiderivative): bare shaper at 4x, drive 10 -43.7 -> -65.5
      dB, drive 7.5 -48.1 -> -70.1; same cost as above; the fold lives in the shared WavefolderProcessor, so either
      Disrumpo wraps its Simple-model path in the adapter or the shared primitive gains an ADAA mode (CROSS-PLUGIN).
      (b) 8x for the fold types (-58.1 dB): needs an 8x band oversampler (bands stop at 4x), about twice the 4x band
      cost (estimated, not measured) and a longer latency. (c) Drive normalisation into the fold (follow-up 37) caps the fold
      count: a sound change. (d) Exempt drive 10 (ruling).
    - Fractal (-40.6 / -32.5 / -30.0 dB at 2x): aliasing of the default Harmonic mode (mode 2, 5 iterations, scale
      0.6, blend 2: an iterated high-order polynomial). Falls 7-9 dB per doubling (drive 10: 1x -24.7, 2x -30.5, 4x
      -35.8, 8x -48.4, 16x -55.5; drive 2.5: 4x -48.2, 8x -56.0) and with a lower tone (2x, drive 10: 515.6 Hz -39.3,
      257.8 Hz -43.7). A non-periodic part (periodicity residual -61 to -121 dB) is far below the bar. 4x passes no
      drive, 8x misses drive 7.5 / 10 by 1.6-1.8 dB, only 16x passes. Options: (a) band-limit inside the iteration
      (oversample or low-pass each level of the shared FractalDistortion): shared DSP, and CPU on top of follow-up 1's
      red Fractal case (Harmonic is already 35 % of it); (b) fewer default iterations or a lower scale (cost is linear
      in iterations; a sound change); (c) 4x for Fractal: not enough and twice its cost; (d) exempt Fractal (ruling).
    - Tape (-26.7 / -26.3 / -26.9 dB at 2x): aliasing caused by a defect in the shared TapeSaturator's Hysteresis model;
      not memory, intermodulation or DC asymmetry. The earlier "not aliasing (1x -24.0)" read was wrong: the model
      changes with the rate (below), so 1x vs 2x does not isolate aliasing. The bare saturator is periodic to -340 dB
      (no noise, limit cycles or sub-harmonics; the node's flutter adds 5.5 Hz sidebands inside the harmonic lobes);
      bias 0 reads the same as the node (its bias is 0). Its output sits on the +/-Sag x Ms clamp of the magnetisation
      for 48-65 % of every cycle (peak 0.509 at Sag 0.5): a hard-clipped, square-ish wave whose aliases fall about
      6 dB per doubling (bare, as the node: 1x -17.7, 2x -19.6, 4x -25.6, 8x -35.9, 16x -42.3 dB; 2x at 257.8 Hz -27.6).
      Cause: H = 1000 x against the Langevin a = 22 makes the anhysteretic curve a near step (linear only for |x| below
      about 0.07), and the explicit RK4 Jiles-Atherton step (reversible term c (Man - M) with c = 1.7, no delta-M gating)
      overshoots; the clamp is what keeps it bounded. At Sag 1 (clamp at Ms) it is not bounded: follow-up 82. Also:
      TScale (dH x 44100 / rate) makes the model rate-dependent (H1 -3.9 -> -8.4 dBFS and H3 -9.5 -> +0.7 dB re H1
      from 1x to 16x; with TScale held at 1 they stay -3.9..-4.3 dBFS / -8.6..-9.6 dB), and the NR4 / NR8 solvers
      return near-noise (H1 -48 to -54 dBFS, all odd harmonics equal). Options: (a) node default model Simple (bare
      -111 dB at 2x, -87.5 dB through the node): Disrumpo-only, one default, a different character for fresh Tape
      nodes (presets keep their stored model). (b) Repair the shared Hysteresis model (input scaled into the
      Langevin's linear region, the standard reversible term c dMan/dH with c <= 1 and delta-M gating, an implicit or
      sub-stepped solver, no TScale on dH, a soft limit instead of the clamp): CROSS-PLUGIN (DistortionRack,
      TapeMachine, Seraphis, Vorago, Ruinae use TapeSaturator), a sound change in every consumer, the largest job.
      (c) More oversampling: not enough (16x -42.3 dB bare). (d) Exempt Tape (ruling); follow-up 82 needs a fix
      either way.
    — USER RULED 2026-11-09 and applied: ADAA1 on HalfRectify (Disrumpo adapter) and TriangleFold (opt-in
    ADAA1 on WavefolderProcessor's Simple model), active when the band is oversampled. Through the Processor at
    drive 2.5 / 7.5 / 10: HalfRectify -42.3 / -51.7 / -54.1 -> -72.4 / -81.7 / -84.2 dB, TriangleFold -65.6 / -51.2 /
    -43.6 -> -89.7 / -79.5 / -72.9 dB; both cases committed GREEN. A/B ([p6s-ab-*]): level within 0.014 dB, H1..H9
    of a 99.6 Hz tone within 0.011 dB. Tape and Fractal stay held (Tape: separate task; Fractal: follow-up 1).
80. **SergeFold reads identical to SineFold at its defaults** (T6.01 matrix: non-harmonic, DC and peak equal to every
    printed digit at all three drives). Same class as D-157 (SineFold == TriangleFold, fixed in T2.06); relates to
    follow-up 23 (the per-type slot defaults). Not asserted by the matrix; decide whether a fresh SergeFold node should
    differ.
81. **T6.05: two latency assertions held back RED, need a ruling** (2026-11-09). CLOSED 2026-11-09 (ruling below); flatness bar
    still needs a ruling.
    - Group delay vs reported latency: HardClip / SoftClip at node mix 0 (only the oversampler's IIR filters) read a
      1 kHz group delay of 3.01 samples at 2x and 6.48 at 4x, against getLatencySamples 4 / 7 (= the impulse-peak
      delay T2.10 reports and "getLatencySamples equals the measured impulse delay at every Oversample Max" asserts,
      +/-1). Off by 0.99 / 0.52 samples: outside the probe's 0.5-sample bar, inside 1 sample. An IIR's delay depends on
      frequency, so no integer report equals it everywhere (same root as follow-up 41). Rule: keep the impulse-peak
      definition (and the bar for the group-delay check), or report the group delay. Patch adding the check (with
      `groupDelaySamples()` in `test_support/audit_metrics.h`): `f:/tmp/disrumpo-fix/p6a/t605-latency-group-delay.patch`.
    - Per-band arrival of the probe's pairs: band 0 HardClip (4x) against a Bitcrush band (1x) and against a SoftClip
      band (2x), both nodes at drive 0.1 and Tone 8 kHz, reference the same nodes at drive 0 / Oversample Max 1x: band
      0 arrives at 8 samples, band 1 at 7 (getLatencySamples 7), so the relative arrival is 1 sample in both pairs
      (baseline 4 and 3; bar 0). Not isolated: the impulse-peak index of the broad low-band response moves with the
      sub-sample delays of the node path (tone filter, oversampler), and a peak index cannot resolve them. The T2.10
      pair (1x passthrough vs 4x HardClip at node mix 0) reads 0 and stays asserted. Patch extending "Bands at different
      oversampling factors arrive on the same sample" with the two pairs: `f:/tmp/disrumpo-fix/p6a/t605-band-arrival.patch`.
    — Investigated 2026-11-09 (probes [p6r-latency], [p6r-arrival], [p6r-xsum]; nothing changed, needs a ruling):
    - (a) getLatencySamples is not a group delay: it is the impulse-peak index of the oversampler's up/down IIR pair
      alone (BandProcessor::measureImpulseDelay in prepare()) plus the type latency (Spectral). The crossover and its
      allpass compensation are not part of it (the dry path runs through the same compensation, so they are not latency
      against the dry signal). Bare oversamplers at 48 kHz: 2x impulse 0, .0066, .1175, .5442, .5923 (peak at index 4,
      0.05 above index 3), group delay 3.001 (20 Hz), 3.006 (1 kHz), 3.12 (5 kHz), 3.56 (10 kHz), 4.61 (15 kHz), 6.16
      (18 kHz); 4x peak 7, group delay 6.474 (20 Hz), 6.482 (1 kHz), 6.68 (5 kHz), 7.42 (10 kHz), 9.22 (15 kHz). The 2x
      figure is one whole sample above its low and mid-frequency group delay; the 4x group delay sits on the half sample,
      where no integer is within 0.5. A 0.5-sample bar on |gd(1 kHz) - L| means "report round(gd(1 kHz))": meaningful
      only as that definition, and at 4x a coin flip on 0.02 samples. What the figures do: dry + wet at Mix 50 %
      (dry delayed by L), 2x: L = 4 (now) -0.44 dB at 5 kHz, -1.34 at 10 and 15 kHz, -0.46 at 18 kHz; L = 3 -0.001 /
      -0.06 / -0.88 / -4.08. 4x: L = 7 (now) -0.10 / -0.10 / -0.25 / -4.35; L = 6 -0.14 / -1.15 / -9.17 / -6.57.
      Crossover sum of a 1x band and a 2x band (oversampler only) against all-1x: -0.07 dB at a 2 kHz crossover, -0.44
      at 5 kHz, -1.34 at 10 kHz; with a 4x band -0.02 / -0.10 / -0.10. Options: (1) keep the impulse peak and assert
      |gd(1 kHz) - L| < 1 (0.994 / 0.52 today; the 2x row passes by 0.006); (2) report the integer nearest the group
      delay at a mid-band reference, e.g. 5 kHz (2x 3.12 -> 3, 4x 6.68 -> 7 unchanged): 2x bands and the 2x dry path
      then line up to 0.01 sample below 5 kHz and the 2x Mix 50 % sum is flat to -0.06 dB at 10 kHz, for -4.1 dB at
      18 kHz; bars |gd(5 kHz) - L| <= 0.5 plus Mix 50 % and crossover-sum flatness; (3) a fractional (allpass or Thiran)
      delay on the faster bands and the dry path to remove the sub-sample residue.
    - (b) The probe pairs' 1-sample reading is an impulse-peak index of fractional offsets. As group delays (150 Hz in
      band 0, 1.5-6 kHz in band 1) against the probe's drive-0 reference: band 0 (HardClip, 4x, Tone 8 kHz) 7.31
      samples = the 4x oversampler 6.47 + its node's 8 kHz Tone 0.84; the Bitcrush band 7.00 (digital types skip the
      tone filter by design; compensated by 7); the SoftClip band (2x) 6.56-6.71 = 3 + the 2x oversampler 3.0-3.1 + its
      Tone. So the Bitcrush pair is +0.31 apart (-0.53 from the 4x rounding plus 0.84 of the node's own Tone, an effect,
      not latency) and the SoftClip pair about +0.6 (+0.46 from the 4x-vs-2x rounding, the rest the tone filter clocked
      at 4x vs 2x). With both nodes at mix 0 (oversampler only), against the compensation target: 4x band -0.53, 2x band
      -0.99 (one sample early: (a)), 1x band 0. Against a reference that keeps each node's path (same drive and Tone at
      Oversample Max 1x) both pairs read 0 by peak index (7 / 7), but the fractional offsets are -0.23 and +0.4 to
      +0.57, so a bar of 0 by peak index passes or fails on sub-sample rounding. Not a rounding of the type latency per
      factor, not the allpass and not the dry delay line: the residue is the integer compensation of the oversamplers'
      fractional group delays, plus the 2x one-sample error of (a), which neither probe pair exercises. The arrival case
      stays held; its bar needs a ruling together with (a), e.g. fractional inter-band group delay <= 0.5 sample with
      the node path kept in the reference.
    — USER RULED 2026-11-09 and applied (CLOSED): align every path to the group delay with fractional delays. The
    reported latency L keeps its T2.10 definition (impulse peak + type latency; unchanged: 4 at 2x, 7 at 4x). Each band
    waits L - its group delay at the factor it runs at: the oversampler's first-moment group delay (3.00 / 6.47 samples,
    measured in prepare()), the type latency and ADAA1's half clocked sample (HalfRectify / TriangleFold, follow-up
    79). Whole host samples run through the existing compensation line, the rest through a first-order allpass
    (`OnePoleAllpass`, Thiran: a = (1 - d) / (1 + d), d kept in [0.5, 1.5)) after a short integer line at the
    clocked rate, on the upsampled input before the shaper. Flat magnitude by construction; phase-delay error
    0.07 clocked samples at 0.23 of the clocked rate for d = 0.5, 0.22 at d = 1.5 (20 kHz at 2x 44.1 kHz; 0.11 of
    the rate at 4x). The 1x bands and the dry path need whole samples only (L is an integer). Linear interpolation
    (cos(pi f / fs) droop) and `DelayLine::readAllpass` (an integer delay: follow-up 83) were not used. Node tone /
    DC / smooth filters are part of each type's sound and stay unaligned; in a morph that mixes ADAA1 and plain nodes
    the plain ones arrive half a clocked sample early (0.125 host samples at 4x); node Mix mixes the undelayed node
    dry with ADAA1 (the same 0.125). Results (RED before, GREEN after; held cases rewritten to group-delay bars):
    arrival of differently oversampled bands (1x/2x, 1x/4x, 2x/4x, 4x/1x at 44.1 / 48 / 96 kHz) -0.99 / -0.52 /
    +0.48 / +0.53 -> within 0.010 sample of each other and of L; T2.10 latency group delay at 1 kHz 3.006 / 6.482 ->
    4.005 / 7.008 (bar |gd - L| <= 0.1); Mix 0 vs 1 group delay 4.000 / 3.006 and 7.000 / 6.482 -> 4.000 / 4.005 and
    7.000 / 7.008; a TriangleFold band vs a SineFold band at 4x +0.125 -> 0.000; the BandProcessor factor-switch case
    6.01 / 6.48 -> 7.00 / 7.01 against 7. "Global mix 50 % stays flat with Oversample Max 4x" asserted an integer
    best-match lag 0; now a group delay bar (the wet energy centres a sample later, lag 1).
    STILL OPEN, needs a ruling: the ruled flatness bar (band sum and Mix 50 % within 0.1 dB from 20 Hz to 20 kHz at
    44.1 / 48 / 96 kHz) is not reachable by any delay at 44.1 / 48 kHz. Held patch
    `f:/tmp/disrumpo-fix/p6s/t605-flatness-held.patch` (two cases), probe [p6s-flatness]. Crossover sum (1x + 2x / 4x
    at 5 kHz): within 0.06 dB to 14 kHz at 44.1 / 48, within 0.001 dB to 20 kHz at 96; above 14 kHz it equals the
    oversampled band's own |H| (2x: -0.96 / -6.85 dB at 18 / 20 kHz at 44.1, -0.17 / -1.45 at 48; 4x: -2.43 / -13.46
    and -0.54 / -3.47), which no delay changes. Mix 50 % against the same at Oversample Max 1x, before (L at 48 kHz,
    from [p6r-latency]) -> after: 2x 5 kHz -0.44 -> -0.001, 10 kHz -1.34 -> -0.056, 18 kHz
    -0.46 -> -4.07, 20 kHz -0.75 -> -16.2; 4x 5 kHz -0.10 -> -0.002, 10 kHz -0.10 -> -0.16, 18 kHz -4.35 -> -24.1, 20 kHz
    -13.5 -> -3.9 dB. 44.1 kHz after: 2x within 0.1 dB to 10 kHz (-0.10), 4x to 8 kHz (-0.07; 10 kHz -0.28); 96 kHz:
    2x -0.056 at 20 kHz (passes), 4x -0.16 at 20 kHz. The IIR half-band filters' group delay rises toward Nyquist
    (4x at 48 kHz: 6.47 at DC, 7.42 at 10 kHz, 11.9 at 18 kHz), so a delay that matches it at low frequencies leaves
    a phase difference that reaches 180 degrees in the top octave; integer L happened to sit nearer the 4x path's
    10 kHz group delay, which is why its notch was higher. Options: (a) bars up to the oversamplers' passband edge
    (e.g. 10 kHz at 44.1 / 48, 20 kHz at 96), which the two held cases then pass except 4x Mix 50 % at 10 kHz (-0.16
    / -0.28); (b) give the dry path and the 1x bands the oversampler's phase (run them through a passthrough
    oversampler of the worst factor, or an allpass fitted to it): exact match, the dry path then has the oversampler's
    |H| too (-1.45 dB at 20 kHz at 2x 48 kHz) and costs a stereo oversampler; (c) linear-phase (FIR) oversampling:
    constant group delay, larger latency and CPU; (d) align at a mid-band frequency instead of DC.
    CPU (run-cpu-tests.js disrumpo_tests, alone): HardClip x4 at 4x 3.76 -> 4.47 % (budget 5), 4 Formant 10.46 ->
    10.60 % (15), HardClip/SineFold/FullRectify/RingSaturation at 4x 13.89 -> 14.34 % (15), the ruled-red
    Formant/Chaos/Granular/Fractal case 24.93 -> 25.70 % (15, follow-up 1).

    — USER RULED 2026-11-09 (second ruling), MINIMAX, applied: each oversampled band arrives at L + its minimax
    offset (probe [p6s-minimax]: the delay minimising the worst Mix 50 % / crossover-sum deviation over 20 Hz - 20 kHz,
    each band's own roll-off divided out); a 2x band takes the 1x + 2x optimum when no band of the patch can reach 4x,
    the joint 1x / 2x / 4x optimum when one can. Offsets (samples, 2x alone / 2x with 4x / 4x): 44.1 kHz -0.999 / -0.269
    / -0.496; 48 -0.759 / -0.409 / -0.506; 88.2 -0.159 / -0.159 / -0.266; 96 -0.129 / -0.129 / -0.216; 176.4 -0.039 /
    -0.039 / -0.066; 192 -0.029 / -0.029 / -0.056 (linear in the rate between). The processor sets the context (4x
    reachable anywhere; reference shift) with the latency, at the silent bottom of its latency dip; a context change
    alone (L held by a Spectral band) also takes the dip: jump ratio 1.007, 0 clicks. ADAA1 clamp: when a band's
    minimax target needs a negative wait (ADAA1 at 4x at 44.1 / 48 kHz: 7 - 6.474 - 0.125 - 0.506 = -0.105; a 2x ADAA1
    band without 4x at 44.1 kHz: -0.25), every path takes that much extra delay (dry and 1x bands through a host-rate
    allpass, d in [1, 1.5)); the reported latency stays L, so the output is that fraction (at most 0.25 sample) later
    than reported. Measured through the Processor (worst dB @ Hz; arrival at 1 kHz vs the offset):
    | rate | Mix 2x alone | Mix 2x w/4x | Mix 4x | xsum 1x+2x alone | 1x+2x w/4x | 1x+4x | 2x+4x | arrival 2x a / w / 4x |
    | 44.1 | -1.714 @19.9k | -9.592 @19.3k | -15.111 @17.7k | -2.014 @19.9k | -29.819 @19.9k | -17.659 @18.2k | -9.038 @19.9k | -0.993 / -0.264 / -0.486 |
    | 48 | -0.863 @19.9k | -3.298 @19.9k | -15.140 @19.3k | -0.868 | -3.326 | -17.276 @19.9k | -3.996 | -0.754 / -0.404 / -0.498 |
    | 88.2 | -0.007 | -0.007 | -0.019 @10.2k | -0.007 | -0.007 | -0.019 | -0.003 | -0.158 / -0.158 / -0.264 |
    | 96 | -0.004 | -0.004 | -0.010 | -0.004 | -0.004 | -0.010 | -0.002 | -0.128 / -0.128 / -0.214 |
    | 176.4 / 192 | < 0.001 | < 0.001 | < 0.001 | < 0.001 | < 0.001 | < 0.001 | < 0.001 | -0.039 / -0.039 / -0.065; -0.029 / -0.029 / -0.055 |
    ffd51f47 (low-frequency match) in the same case: 44.1 kHz -17.85 / -17.85 / -27.29 | -39.21 / -39.21 / -33.95 /
    -14.25; 48 kHz -14.23 / -14.23 / -25.76 | -14.90 / -14.90 / -29.62 / -5.00; 88.2 kHz 4x -0.271, 96 kHz 4x -0.157
    (over the 0.1 bar). The old integer alignment (model, probe): 44.1 kHz Mix 2x -4.31, Mix 4x -15.52, 1x+2x -2.01,
    1x+4x -20.80, 2x+4x -37.81; 48 kHz -1.57, -15.09, -1.57, -19.13, -33.07; 96 kHz -1.33, -0.14, -1.33, -0.14, -0.66;
    192 kHz -0.43, -0.10, -0.43, -0.10, -0.12. Bars as ruled: 0.1 dB at 88.2 kHz and up; at 44.1 / 48 kHz the measured
    worst cases above (identical on a repeat run, no margin); arrival = offset +/- 0.1. The two held flatness cases
    (raw deviation against flat) are replaced by "Minimax alignment of Mix 50 % and the crossover sums with arrival per
    rate and factor". Open point for the user: at 44.1 kHz a 2x band next to a 4x band reads -29.8 dB against the dry /
    a 1x band (the joint optimum trades it for 2x + 4x, -9.0 dB).
    CPU lane not verified for this change: two runs alone read uniformly high under 11-19 % background load (HardClip x4
    4x 5.54 %, 4 Formant 15.39 %, mixed 4x 19.44 %, experimental 37.52 %); re-run on an idle machine.
    — Re-run 2026-11-09 on the idle machine (background ~3 %, WSL shut down; `node tools/run-cpu-tests.js disrumpo_tests`
    alone, at 374f8096): HardClip x4 4x 4.51 % (best 10-block run 4.01 %, budget 5) GREEN; 4 Formant 13.15 % (10.71 %,
    budget 15) GREEN; HardClip/SineFold/FullRectify/RingSaturation 4x 16.82 % (12.89 %, budget 15) **RED** (14.34 %
    after ffd51f47, 13.89 % before it); Formant/Chaos/Granular/Fractal 28.26 % (23.18 %) RED by ruling (item 1). The
    mixed 4x red is not changed in this batch (cause not investigated; one run, not repeated). Log `f:/tmp/disrumpo-fix/p6b/cpu-lane-1-full.log`. The same run's
    `[long]` preset-level case "Every factory preset renders finite, bounded, non-silent audio" read Dynamic/Pitch_Tracker
    peak +0.03 dBFS (bar 0 dBFS; 119 of 120 ok): see T6.08.
82. **Tape Hysteresis at Sag 1 is silent or noise** (found 2026-11-09 by the follow-up 79 probes, [p6r-tape],
    [p6r-tape-sag]). Shared `TapeSaturator` Hysteresis model, CROSS-PLUGIN. Its explicit RK4 Jiles-Atherton step
    overshoots at audio rates and only the magnetisation clamp at +/-Saturation x Ms keeps it bounded; at Saturation 1
    the clamp no longer holds it: the bare saturator at 48 kHz breaks down (non-harmonic +35 dB re the harmonics,
    then exact zeros: its last 8192 samples are 0), at 96 kHz it is noise (H1 -51 dBFS, non-harmonic +10 dB). Through the Processor (Tape node, Sag slot 1, drive 2.5
    / 7.5 / 10): Oversample Max 1x silent (-240 dBFS), band at 2x fundamental 21.7 / 14.9 / 14.3 dB against 62 dB at Sag
    0.5, non-harmonic +5.2 / +10.5 / +10.5 dB; Sag 0.9 is still normal (peak -0.9 dBFS, non-harmonic -28 dB). The
    Sag knob reaches 1. Needs a task: a stable solver / the model repair of follow-up 79 (Tape option b) in the shared
    processor, or as a stopgap a Disrumpo-side Sag range below 1 (check the other TapeSaturator consumers' saturation
    ranges). Same probes: TScale rate dependence and the broken NR4 / NR8 solvers (follow-up 79, Tape).
83. **`DelayLine::readAllpass` is an integer delay** (found 2026-11-09 while choosing the follow-up 81 interpolator).
    `delay_line.h` computes `y = x0 + a * (state - x1)` with x0 / x1 the samples at floor(d) and floor(d) + 1 and
    state the previous output: y[n] = x[n-k] - a x[n-k-1] + a y[n-1], so Y = z^-k X, a pole-zero cancellation; the
    fraction of d only sets how a transient decays. A first-order allpass interpolator is y = a x[n-k] + x[n-k-1] -
    a y[n-1]. The tests (`delay_line_test.cpp` "readAllpass ...") check unity gain and settling, which an integer
    delay passes. CROSS-PLUGIN: `makeAllpassTap` / `processFixedTap` share it; consumers allpass_saturator.h
    (Disrumpo AllpassResonant), diffusion_network.h, karplus_strong.h, waveguide_resonator.h, waveguide_string.h,
    whose tunings then round their fractional delays down. Not fixed (a sound change in every consumer: needs a
    task). Follow-up 81 uses `OnePoleAllpass` instead.
84. **Clicks in a 4-node cross-family morph sweep with RingSaturation at 4x** (T6.06, 2026-11-09, needs investigation and
    a ruling). Through the Processor, 4 nodes SoftClip / SineFold / FullRectify / RingSaturation at drive 3, Planar2D,
    cursor (0, 0) -> (1, 1) over 2 s, 220 Hz -12 dBFS: the shared ClickDetector (5 sigma, 512 / 256 frames) finds 321
    clicks (plan bar 0; audit baseline 54); jump ratio 1.00 (asserted). From about 1.70 s (cursor 0.60) the output has
    2-sample falls of 0.03-0.04 once per half period (every 109 samples) where a parked render at the same cursor steps
    at most 0.020 once it has run 0.25 s; parked renders show the same 0.037-0.047 steps (0-23 clicks) only in their first
    0.25 s, so something in the RingSaturation node at 4x starts a settling transient again during the sweep. Not the
    smoothing (10 ms: 319 clicks), not the weight law. 0 clicks with Oversample Max 1x or 2x, with RingSat's Harmonic
    carrier, or with RingSat replaced by SoftClip; still 331 with the Fixed carrier, 292 / 316 with FullRectify / SineFold
    replaced. Held with the level bar in `f:/tmp/disrumpo-fix/p6b/t606-held-bars.patch`; probe `[.][p6b-morph]`.
    — INVESTIGATED 2026-11-09 (temporary probes, not committed); not a crossfade or morph defect; ruling needed.
    The default Ring Saturation node runs carrier type Square (slot 3 = 0.5) in freq mode Track (slot 5 = 0.5, the
    carrier follows the input frequency), 3 stages. (1) The clicks are the square carrier's edges: Fixed + Square
    alone clicks at every factor in a steady render (834 / 828 / 794 clicks in 1.5 s at 1x / 2x / 4x, step 0.11,
    -17.2 dB); a Sine carrier never clicks. With Track the carrier runs at 220 Hz, so its edges fall once per input
    half period (the 109-sample period of the 2-sample falls) at a fixed input phase; at a quiet phase the shared
    ClickDetector finds nothing (RingSat alone from a clean start: 0 clicks, step 0.009, -14.0 dB at every factor),
    at a loud one it finds ~500 per 1.5 s. (2) That relative phase is set by the moment the carrier was last reset:
    a factor switch (the old dip's bottom and now the crossfade's incoming lane alike) clears the adapter, so the
    carrier restarts at phase 0 wherever the input is. RingSat alone at 4x, Oversample Max 4x -> 2x -> 4x at four
    moments 64 samples apart: 1 stage -8.27 / -14.43 / -17.97 / -17.95 dB with 528 / 525 / 0 / 0 clicks after the
    return (-7.98 dB, 0 clicks before); 3 stages -22.45 / -15.89 / -24.55 / -17.40 dB, 515-526 clicks (and already
    50 clicks at -14.73 dB before the switch, rendered in 64-sample blocks: that sensitive). In the
    4-node sweep the 2x -> 4x switch at 0.555 s lands on a loud phase: the end node reads -17.15 dB (1x / 2x, no
    switch: -14.0 dB) and clicks to the end; with Oversample Max 1x / 2x there is no switch, hence 0 clicks. (3) In
    the shared primitive (dsp/include/krate/dsp/primitives/ring_saturation.h, Disrumpo its only consumer)
    processStage() runs the zero-crossing tracker and advances the carrier phase once per STAGE, so with 2-4 stages
    the tracker compares different stages' signals and the carrier advances `stages` times per sample. Options: (a)
    carry the oscillator state (carrier phase, tracked frequency) into the incoming lane at a factor crossfade (an
    accessor on RingSaturation, copied by the adapter / MorphEngine), so a factor switch does not re-phase the
    carrier; (b) update the tracker and the phase once per sample in RingSaturation; (c) measure the T6.06 4-node
    click bar with a Sine carrier (a square carrier's edges are its sound), or default fresh RingSat nodes to Sine
    (a sound change). The level bar's remainder is item 12 plus this re-phase (see item 12).
    — CLOSED 2026-11-10 (user ruling: options (a) and (b); commit "fix(dsp): Ring Saturation's carrier and tracker run once
    per sample and survive a factor crossfade"). (b) RingSaturation tracks the input and advances the carrier once per
    sample; every stage multiplies by that sample's carrier (RED: 1 kHz Fixed carrier at 2 / 3 / 4 stages crossed its
    mean 1999 / 3000 / 4000 times in 0.5 s instead of 1000; Track read 4407 / 4795 / 4826 Hz for 440 Hz; GREEN 1000 and
    440.0 at every stage count). (a) a factor crossfade's incoming lane takes the running lane's carrier state
    (RingSaturation::getCarrierState / setCarrierState): phase, moved back by the carrier frequency times the lanes'
    shaper-input lag; tracked frequency; the tracker holds 2 ms (the cleared input path) and re-syncs on its first
    crossing (RED: default node 4x -> 2x -> 4x -14.17 -> -29.83 dB, Sine / Track -16.69 -> -28.81; GREEN -14.13 / -16.42).
    T6.06 4-node sweep: clicks 326 -> 222, level +2.47 -> +1.33 dB, end node -17.15 -> -14.14 dB (no switch: -14.00).
    Both 4-node bars stay held: the level bar is item 12's blend law (+1.31 / +1.33 dB at a fixed 2x / 1x); the click
    bar: new item 90.
85. **T6.07 full sweep automation: the plan's jump-ratio bar compares against the sweep off** (2026-11-09, needs a
    ruling). 4 bands SoftClip drive 3, four sines at -18 dBFS (150 / 700 / 3200 / 11000 Hz), sweep on at 100 %, its
    frequency automated 100 Hz -> 10 kHz over 2 s, LFO 5 Hz depth 1, envelope on: the largest sample step with the
    sweep on is 1.63 x the largest with the sweep off (bar 1.3; audit baseline 0.48, when the sweep still muted the
    bands away from its centre). Since T1.11 (D-019) the sweep raises the drive of the bands near its centre (x 1.5 at
    100 %, x 2 at 200 %), so a steeper waveform is the intended result: the sweep-on step (0.514, at 1.232 s, mid-block)
    is below the same bands' step at drive 4.5 with the sweep off (0.614; drive 6: 0.790), and the shared ClickDetector
    finds 0 clicks. Finite (0 non-finite) and RMS within 3 dB (+1.26 dB) are asserted. Options: (a) compare against the
    static render at the highest drive the sweep reaches, as "Sweep crossing band edges is click-free" does (step <=
    max(static drive, drive x the sweep's peak multiplier) x 1.05, plus 0 clicks); (b) keep the sweep-off reference and
    rule the bar not applicable since D-019. Held patch `f:/tmp/disrumpo-fix/p6b/t607-held-jump.patch`; probe
    `[.][p6b-auto]` in `integration/processor_automation_test.cpp`.
    — CLOSED 2026-11-09 (user ruling: option (a) with the static render at the equivalent drive, 4.5, as the
    reference; bar unchanged, jump ratio <= 1.3 and 0 clicks). "Full sweep automation stays finite and keeps the
    level" asserts it: largest step 0.5135 against 0.6141 at drive 4.5, ratio 0.84, 0 clicks (shared ClickDetector).
    RED under a temporary break (each sweep intensity change above 0.05 reset the morph engines' state): 5 clicks,
    ratio 1.15. The RMS bar keeps the sweep-off reference (+1.26 dB). Commit "test(disrumpo): T6.07 full sweep
    automation against the static render at the sweep's drive".
86. **T6.08: an active load does not render like the host-order load** (2026-11-09, needs a ruling). Reference: each
    factory preset loaded by setState before setupProcessing / setActive (44.1 kHz, 440 Hz -12 dBFS, 0.5 s). (a) The
    plan's literal bar (setState on an active processor, rendered at once, within 1e-5 of the reference): 119 of 120
    differ, max 1.87 (Morph/Radial_Blend), still 1.48 after the first 0.1 s (Pads/Spectral_Pad). An active load glides
    from the previous patch by design (T1.24: "straight after a load while active ... recorded, not gated"); LFO /
    chaos / feedback state keeps the difference long after the glide. Rule: keep the T1.24 design (and drop the literal
    bar), or make a load into a processor that has rendered nothing yet start on its targets. (b) With setupProcessing
    + setActive after the active load (what a host's rate or block-size change does), 103 of 120 match within 1e-5 and
    17 differ: Morph/Digital_Organic 0.89, Experimental/Stochastic_Burst 0.68, Pads/Granular_Wash 0.47,
    Experimental/Broken_Radio 0.43, Pads/Stochastic_Drift 0.22, Pads/Spectral_Pad 0.20, Lo-Fi/Bit_Mangler 0.15,
    Pads/Formant_Pad 0.058, Pads/Tape_Haze 0.044, Experimental/Granular_Crush 0.020, Morph/Spectral_Morph 0.0045,
    Pads/Evolving_Texture 0.0018, Experimental/All_Types_Morph 3.7e-4, Lo-Fi/Digital_Decay 5.4e-5, Experimental/
    Fractal_Noise 2.2e-5, Lo-Fi/Warm_Lo-Fi 1.5e-5, Pads/Slow_Morph_Pad 1.1e-5. The only difference between the two
    paths is one extra prepare / activation and a 0-sample process() before the render, so some state survives
    setupProcessing (relates to item 18, `setActive(true)` does not clear DSP state); cause not isolated. Held with (a)
    in `f:/tmp/disrumpo-fix/p6b/t608-held-bars.patch`; the readings print in the committed case.
    — (b) INVESTIGATED 2026-11-09, cause found, fix proposed (cross-plugin, not applied). Load-order probe over all 120
    presets against the host-order load: host order plus one more setupProcessing + setActive differs for exactly the
    same 17 presets and values; host order plus a second setupProcessing alone (no audio, no state change) as well;
    host order plus setActive(false) / setActive(true) alone differs for none. So prepare() is not idempotent. The
    shared KrateDSP `RandomSource` (dsp/include/krate/dsp/processors/random_source.h, the modulation engine's Random
    source) draws `currentTarget_ = rng_.nextFloat()` in prepare() and restarts its generator in neither prepare() nor
    reset(): each setupProcessing moves its sequence one value on (and any audio before a reset moves it further).
    With `rng_.seed(98765)` added to its prepare() and reset() (temporary probe build, reverted) all 120 presets match
    in every load order (0 of 120 differ, from 17). LFO already restarts its sequence on reset (D-165); `Rungler`
    draws in prepare() and reset() the same way (not routed by these presets; re-activation would move it), and
    `SampleHoldSource` / `GranularDistortion` do not reseed on reset either. Proposed: reseed these generators in
    prepare() and reset() (CROSS-PLUGIN: `ModulationEngine` serves Disrumpo and Ruinae; renders after a reset change
    to a fixed sequence), then assert (b) at 1e-5. (a), the active load rendered at once, glides from the previous
    patch by design (T1.24): proposed to drop that literal bar and keep (b).
    — CLOSED 2026-11-10 (user ruling: reseed deterministically on prepare() and reset(); drop (a), assert (b); commit
    "fix(dsp): random modulation sources and Granular restart their sequence on prepare and reset"). RandomSource
    (98765), SampleHoldSource (54321), Rungler (seed(), default 1) and GranularDistortion (seed(), default 12345, its
    scheduler too) reseed at the top of prepare() and reset(). (b) asserted at 1e-5 in the T6.08 case: RED on
    45fdfc23 17 of 120 presets (the same 17, max 0.89 Morph/Digital_Organic), GREEN 0 of 120 (active-load diff 0 for
    all). Output changes once for renders that use these sources: Disrumpo 19 of 120 factory presets (host-order load,
    0.5 s; up to 0.52 Experimental/Stochastic_Burst), 22 with Oversample Max cycling (a Granular crossfade lane now
    restarts its grains); Ruinae 16 of 168 factory presets (held C4, 2 s; up to 1.89 Arp_Euclidean/Granular_Samba).
    Experimental/Stochastic_Burst peaks -0.24 dBFS on the [D-144] signal after it (was -0.41): re-levelled in its own
    commit.
87. **T6.08 / D-160: replaying a preset's parameters does not render like its setState** (2026-11-09, needs a ruling).
    Every parameter `Controller::setComponentState` sets from the file (586), replayed through processParameterChanges
    then setupProcessing, against the host-order load: 72 of 120 within 1e-4, 48 differ (31 of them only here), up to
    0.62 (Morph/Formant_Shift; Sweep/Slow_Scan 0.048, Bass/Tape_Warmth 0.016, Morph/Four_Corners 0.013). Diagnosed
    (temporary probe: getState of the replayed processor, read back by the controller, against the recorded values): in
    76 presets kNodeBias (0x?004), kNodeFolds (0x?005) or kNodeBitDepth (0x?006) and their band display proxies come back
    different, e.g. Formant_Shift band 0 node 0 bias 0.5 -> 0.55, Serge_Bass folds 0 -> 0.0909, Dirty_Sub bias 0.5 ->
    0.75. Each of those settings has two controls: the node parameter and the type's own shape slot (RingSaturation
    Bias, wavefold Folds, Bitcrush Bits; follow-up 34 for the bit depth), and setState and the parameter path end up
    with different values for the pair (the precedence in each path not traced further). Rule which control owns each setting (then the other is derived, or retired), and the
    parity bar can be asserted. Held in `f:/tmp/disrumpo-fix/p6b/t608-held-bars.patch`.
    — Precedence CLOSED 2026-11-09 (user ruling: one precedence, the slot wins; commit "fix(disrumpo): Bias, Folds
    and Bit Depth: the type's shape slot wins over the node parameter"). setState already mapped the slots last;
    processParameterChanges now re-applies the type's slots after kNodeBias / kNodeFolds / kNodeBitDepth, and the
    controller's state load emits those three from the slots (v9+), so the node parameters mirror the slot. Cases:
    "Bias, Folds and Bit Depth: the type's shape slot wins over the node parameter in any order" (RED: a node
    parameter after the slot won, Tube bias -0.8 / Sine Fold folds 1 / Bitcrush bits 24 instead of 0.6 / 6.5 / 10)
    and "Loading a factory preset sets Bias, Folds and Bit Depth to what the processor runs" (RED 76 of 120 presets,
    225 of 5760 values; GREEN 0). Factory preset renders unchanged: setState and parameter replay (the plugin's own
    preset browser) both bit-identical before / after for all 120.
    — The D-160 RENDER bar stays HELD; the diagnosis above was wrong about the renders. The precedence fix changed no
    replay render: the controller already sent Bias before the slots, so the slot already won in the replay. The
    replay against setState through the same load sequence (both into an active processor, then setupProcessing;
    printed as "vs setState" in the T6.08 case) still differs for 39 presets (Morph/Formant_Shift 0.62, Pads/
    Spectral_Pad 0.236, Sweep/Slow_Scan 0.048, Bass/Tape_Warmth 0.016, ...); against the host-order load 48, the 17
    extra being item 86. Cause (temporary probes): the two processors' states decode to the same parameters within
    1e-6 (normalized); the byte differences are last-bit rounding of floats that go through a normalized double on
    the parameter path (band pans, crossover frequencies, sweep frequency) plus the per-type shadow slots (not
    rendered); reloading each processor's own getState into a fresh host-order instance reproduces the same
    differences. Self-oscillating and chaotic types (AllpassResonant in Formant_Shift, Spectral_Pad, Slow_Scan,
    Tape_Warmth; the Chaos presets) and quantizers grow that rounding into audible divergence (Formant_Shift: 0.046 in
    the first 25 ms, 0.53-0.62 in every 25 ms window after). Options: (a) make the parameter path round-trip floats exactly (normalize
    and denormalize in double on both sides), then re-measure; (b) assert parameter parity (decoded values within
    1e-6) instead of render parity for presets with such types.
    — New (2026-11-09): the band header's Bias knob (Band.NodeBias, the selected node's kNodeBias) no longer changes
    the sound for types with their own Bias slot (Tube, Tape, Fuzz, Asymmetric Fuzz, the three wavefolders, Ring
    Saturation): the slot wins on the live parameter path too, which is the load-replay path. Options: route that
    knob to the type's Bias slot in the controller (and show the slot's value), or hide it for those types.
    — Header Bias knob CLOSED 2026-11-10 (user ruling: the knob edits the type's Bias slot; commit "fix(disrumpo):
    the band header's Bias knob edits the type's Bias slot"). NodeSelectionController links the Bias proxy and the
    type's Bias slot proxy (biasShapeSlot): either knob edits the node's slot, kNodeBias mirrors it, the other knob
    follows, slot automation moves both, kNodeBias automation no longer moves the knob for these types. Case "The band
    header's Bias knob edits the Bias slot of the types that have one": RED on c9278472 for all 8 types (slot stays
    0.5, header 0.2 vs 0.8 renders identical, max diff 0), GREEN: slot 0.2, render diff 0.59-2.36, knobs in sync.
    — Exact round trip DONE 2026-11-10 (user ruling: option (a); commit "fix(disrumpo): plain-stored values round-trip
    exactly through the parameter path"). The values the state stores plain go plain -> normalized -> plain in double on
    both sides (controller: crossover Hz, node tone and bias, routing amount; processor: band gain and pan, crossover
    Hz, node drive / tone / bias, morph smoothing). Case "Replaying a factory preset's parameters stores exactly what
    setState stores": RED 53 of 120 presets, 130 values (e.g. crossover 0.3656366467 vs 0.3656367064, a band pan
    0.424999997 vs 0.4250000119); GREEN 0. setState renders unchanged (its path did not change). The D-160 RENDER
    bar (replay vs setState within 1e-4) stays HELD: 39 -> 9 presets above it, max 0.62 -> 0.065 (new item 91).
88. **T6.09: the 4-band chain is 1.40 dB quieter at 192 kHz, all of it the Tape band** (2026-11-09, needs a ruling with
    follow-up 79's Tape item). The probe's chain (bands 0-3 node 0 Tube / SoftClip / Fuzz / Tape, drive 4, 1 kHz at
    -12 dBFS, last 0.5 s of 1 s) against 48 kHz: 44.1 kHz +0.02 dB / centroid 1.006, 96 kHz -0.77 dB / 1.019 (asserted);
    192 kHz -1.40 dB (bar 1 dB) / 0.920 (audit baseline 2.50 dB / 0.824). Each band soloed, 48 / 96 / 192 kHz: Tube
    -79.19 / -79.16 / -79.15, SoftClip -20.46 / -20.45 / -20.44, Fuzz -4.40 / -4.40 / -4.41, Tape -13.85 / -14.60 /
    -16.11 dBFS. The shared TapeSaturator's Hysteresis model scales dH by 44100 / rate (TScale), so its level moves
    with the rate it is clocked at (follow-up 79, Tape: H1 -3.9 -> -8.4 dBFS from 1x to 16x). Disrumpo-only options
    exist (fresh Tape nodes on the Simple model) but each is a sound change; the repair of the shared model is
    CROSS-PLUGIN. Held patch `f:/tmp/disrumpo-fix/p6b/t609-held-192k.patch`.

89. **A DistortionAdapter is 1.04 MB prepared, whatever type it runs** (found 2026-11-10 by the slim-spare-lane ruling's
    breakdown, 48 kHz, buffers for 4x): each adapter holds every type's processor, heap per adapter Spectral 630 KB (FFT
    4096 plus every smaller size), Granular 145 KB (in the object), FeedbackDist 129 KB, AllpassResonant 65 KB, Chaos
    34 KB, the latency delays 32 KB, the rest < 2 KB each. A band's running lane holds 12 (2 single-type adapters the
    Processor never runs, morph is always on there; 4 nodes and the identical-type one per channel), so a 4-band
    instance is 84.2 MB (heap, 48 kHz / 512) of which about 50 MB are the running lanes' adapters. The slim spare lane
    (8 adapters per band) is at the floor for adapters that can run any type without allocating; going lower needs
    per-type processor storage (a node slot holding only its type's processor, provisioned without audio-thread
    allocation) or dropping the single-type adapters from the Processor's bands. Not started (scope cap).

90. **T6.06 4-node click bar after item 84: the Square carrier's edges** (2026-11-10, needs a ruling). After the per-sample
    carrier and the crossfade carry, the 4-node planar sweep (SoftClip / SineFold / FullRectify / RingSaturation, drive
    3, 220 Hz -12 dBFS) still reads 222 clicks (was 326; 0 with Oversample Max 1x / 2x, no switch). The default Ring
    Saturation node (Square carrier, Track, 3 stages) alone, 222.66 Hz -12 dBFS, clicks at its square edges at every
    factor in a steady render with no switch: 304 in 0.68 s at 1x / 2x / 4x (was the same before the fix). Whether the
    edges land on a quiet or a loud input phase depends on the carrier's phase against the input, which the reset at
    activation sets; the carry keeps it within 0.3-0.5 dB of level but not edge-exact. Options: measure the click bar
    with a Sine carrier (follow-up 84 option (c)), or default fresh Ring Saturation nodes to Sine (a sound change).
    Also found (not fixed, scope cap): RingSaturation's curve crossfade ramp (setSaturationCurve) still advances once
    per stage, so its 10 ms crossfade lasts 10 / stages ms.

91. **D-160 render bar: 9 presets still render apart after an exact parameter replay** (2026-11-10, needs investigation
    and a ruling). After the exact round trip (item 87) the processor holds bit-identical parameters after setState and
    after the controller's parameters are replayed (every one of 120 presets, every parameter, exact), yet replay vs
    setState through the same load sequence (both into an active processor, then setupProcessing; T6.08 "vs setState")
    reads Sweep/Slow_Scan 0.065, Morph/Formant_Shift 0.0066, Leads/Chaos_Lead 0.0026, Pads/Allpass_Shimmer 8.5e-4,
    Chaos/Rossler_Morph 5.4e-4, Dynamic/Pitch_Tracker 4.8e-4, Lo-Fi/Aliasing_Harsh 1.9e-4, Pads/Gentle_Warmth 1.3e-4,
    Experimental/Alien_Voice 1.1e-4 (bar 1e-4; the other 111 at or below it). So the difference is in the order the
    values arrive (586 parameter changes one by one: intermediate node types, morph and sweep states, smoother
    targets) and in DSP state that survives setupProcessing (follow-up 18), not in the values. Not traced (scope cap).
    Options: find the state a parameter-by-parameter load leaves that setupProcessing does not clear; or assert parameter
    parity (the new exact case) and drop the render bar for these types.

## Done outside the plan

- `68255845` fix(dsp): CrossoverLR4 stages in double precision — latent float32 defect (DC gain error up to
  1.18 dB at 20 Hz / 192 kHz; 0.12 dB at 112 Hz / 192 kHz) exposed when T1.03 removed the start-up glide that
  had masked it. CROSS-PLUGIN (every crossover consumer).
- `e498752d` test(profundum): regenerated evidence after that fix (109 CSVs; residual floors dropped 20–45 dB,
  e.g. sc017 aliased_L_dbfs −88.96 → −132.54; nine pass cells flipped 0 → 1, none 1 → 0).
- Crossover dedup step 1 (`crossover-dedup-plan.md`; commit "refactor(dsp): add MultibandCrossover, the shared
  N-band LR4 splitter"): `MultibandCrossover<MaxBands>` in `crossover_filter.h` next to the old classes; no caller
  moved yet. A/B vs `Crossover3Way` / `Crossover4Way` configured before prepare: bit-identical (0 difference).
- Crossover dedup step 2 (commit "refactor(dsp): FractalDistortion Multiband on MultibandCrossover; drop
  Crossover3Way/4Way"): Fractal Multiband compensated (ruling 3; band sum −0.772 → ±0.0004 dB), snaps on prepare
  (ruling 2), `setCrossoverFrequency` works after prepare (ruling 6); the old classes and their FR-016 clamp cases
  are gone (ruling 5). Old vs new Fractal at Disrumpo's defaults, 48 kHz pink noise: +0.19 dB, difference −11.4 dB.
  User ruling (2026-11-09, step 2): "FractalDistortion sustained 3s: max stress parameters" Multiband no longer uses
  `worstDelta < 1.2 × peak` (the steady square-edge step is 1.41× the peak once compensation aligns the saturated
  bands; the old pass rested on a 2.71 start-up-glide peak, 1.25× without the glide even uncompensated). It checks
  for a sporadic click instead: the worst step of each 100 ms window against the median of those (steady edge 3.058),
  bar 1.25; real output 1.053, a one-sample 2×-peak click 1.43 (red). Other four modes keep the 1.2× bar. Limit: a
  click must exceed the steady edge; a 1.5×-peak spike reads 1.06–1.43 depending on its position.
- Crossover dedup step 3 (commit "refactor(disrumpo): CrossoverNetwork wraps MultibandCrossover"): the network is
  Disrumpo's band policy over one `MultibandCrossover<4>`; allpass Q 0.7071f → 1/√2 (ruling 1). A/B vs a frozen copy
  of the old network: identical with the old copy at Q 1/√2; with 0.7071f the band sum differs by −123 to −138 dB and
  the dry path by −66 dB (192 kHz, 4 bands) to −107 dB. A re-prepare now also clears the allpass state.
- Crossover dedup step 4 (commit "fix(dsp): MultibandCrossover allpasses in double precision, tracking the gliding
  crossovers"): closes follow-up 7. `CrossoverLR4::Section` lifted to `detail::BiquadSectionD`, read-only
  `CrossoverLR4::getCurrentFrequency()`. Steady dry-vs-band-sum residual −86.7 → −146.0 dB (48 kHz), −65.2 →
  −146.1 dB (192 kHz); during a 632 Hz → 1.2 kHz glide −26.2 → −52.8 dB (48 kHz). A glide cannot reach the static
  floor: in lockstep the time-varying LR4 pair and allpass still differ (−81 dB for 632 → 650 Hz, −50 dB to 2 kHz).
- Crossover dedup step 5 (commit "docs: crossover dedup — MultibandCrossover in the architecture docs; same-layer
  includes"): layer-2-processors.md, plugin-architecture.md, processors/README.md describe `MultibandCrossover`
  and the `CrossoverNetwork` wrapper; dsp/CLAUDE.md records ruling 7 (a layer may include its own layer, no cycles).
- T4.18 / D-024 closed by `9ced9310` (user ruling 2026-11-09): its test "MultibandCrossover allpasses track a
  gliding crossover" is the regression guard (dry − band sum over 50 ms after a 632 Hz → 2 kHz move: RED −20.4 dB
  on the jumping float allpasses, GREEN −49.7 dB, bar −45 dB). The plan's STFT test is dropped: 3 bands, crossovers
  200 / 500 Hz, crossover 1 → 2 kHz in one block, tones on the odd bins of a 256-sample frame, max per-bin deviation
  from the input in the first frame after the step: new network 1.26 dB, frozen pre-dedup copy 1.25 dB, an ideal
  gliding allpass chain 1.34 dB (the glide's phase motion, not a defect, and no old/new separation). The defect
  shows in the time domain (band sum vs the ideal chain, 50 ms after the step): crossover 1 2 kHz → 500 Hz −42.9 dB
  new vs −26.0 dB old; crossover 0 400 Hz, crossover 1 500 Hz → 2 kHz −45.6 dB vs −32.9 dB. The unused
  `CrossoverNetwork::kDefaultSmoothingMs` is deleted (the LR4 smoothing is `CrossoverLR4`'s 5 ms).
- Follow-up 75 closed (commit "fix(disrumpo): Chaos Speed registered over the range the processor runs", user ruling
  2026-11-09): Chaos Speed is a linear range parameter over `kChaosSpeedRange` (0.05–20 x, default 1x) at the same
  ID; T5.02's pin is replaced by a checked read-back. Details under item 75.
- Found by the T6.02 parity probe (commit "fix(disrumpo): a Drive change re-routes Temporal and Stochastic"): those
  two types read the Drive inside `DistortionAdapter::routeParamsToProcessor()`, and `MorphEngine::setNodes` sets a
  node's type params before its common params, so on a 2+-node band one Drive change reached them only with the next
  node update. `setCommonParams` now re-routes them when the Drive changes. Test "One Drive change reaches a Temporal
  and a Stochastic node": Temporal with a SoftClip node B RED 3.51 dB low (-6.96 vs -3.45 dBFS), Stochastic 0.52 dB
  (-18.33 vs -17.81); GREEN 0.00 dB for all four rows.
- Found by the T6.06 morph sweep through the Processor (commit "fix(disrumpo): a morph between types of one factor
  keeps that factor"): `calculateMorphOversampleFactor` rounded the float weighted mean up, and the weights of
  `MorphEngine::computeWeights` sum to 1 only within a few ULPs, so two 2x nodes read 2.0000002 and ran at 4x. A
  SoftClip -> Tube sweep switched 2x -> 4x -> 2x 12 times in 2 s (each a 10 ms D-034 dip; level -4.72 dB off the
  straight line). The factor is now capped at the largest factor among the weighted nodes. Test "a morph between types
  of one factor keeps that factor": RED wrong factor at 288 of 3003 Linear1D and 1924 of 20402 Planar2D / Radial2D
  positions (1x and 2x groups alike), GREEN 0 / 0; the sweep: 0 factor switches, level within +0.81 dB. No factory
  preset band changes factor at its stored cursor (all 120 checked).
- Found by T6.07 (commit "fix(disrumpo): solo and mute fade a band in and out of the sum"): `Processor::process` added a
  band to the sum or left it out per block (`shouldBandContribute`), so a solo or mute change stepped the output on the
  block's first sample. The audit probe's "automation steps" read them ok (0.92-1.00) because its event window started
  on that first sample and so left out the step into it; with the step included (`stepContinuity(..., lead = 1)`) band 1
  solo on / off read 1.31 / 2.53 (1 click) and band 0 mute on 1.25. Each band's share of the sum now runs through a
  15 ms one-pole (`contributionSmoothers_`), exactly 1 or 0 once settled; at 1 the sum is the old arithmetic.
  GREEN 1.00 for every mute / solo step.
- Found by the CPU lane's `[long]` cases (2026-11-09, commit "fix(disrumpo): seven factory presets re-trimmed under
  -0.3 dBFS after the band alignment"): "Every factory preset renders finite, bounded, non-silent audio" (T3.01) was RED
  on Dynamic/Pitch_Tracker, peak +0.03 dBFS (bar 0 dBFS). Gates exclude `[long]`, so no commit gate saw it. Cause:
  374f8096 (minimax band alignment): the same tree with 374f8096 reverted reads -0.77 dBFS (RMS -7.90 vs -7.97 dBFS:
  the sub-sample alignment changes how the bands' peaks coincide, not the level). Under T3.01's rule (peak <= -0.3
  dBFS on its pink-noise signal, Output Gain only lowered, 0.1 dB steps) seven presets above -0.3 dBFS at HEAD 45d5863d
  got lower trims: Pitch_Tracker +0.03 -> -0.37 (trim -6.4 -> -6.8 dB), Coupled_Chaos -0.21 -> -0.31 (-5.6 -> -5.7;
  -1.96 without 374f8096, a chaotic preset), Soft_to_Hard -0.14 -> -0.34 (-2.9 -> -3.1; -0.30 without 374f8096),
  Chaos_Engine -0.08 -> -0.38 (-0.5 -> -0.8), Temporal_Edge -0.05 -> -0.35 (0 -> -0.3), Slow_Morph_Pad -0.09 -> -0.39
  (-2.4 -> -2.7), Slow_Scan -0.19 -> -0.39 (-4.9 -> -5.1). Chaos_Engine, Temporal_Edge, Slow_Morph_Pad and Slow_Scan were
  already above -0.3 dBFS before 374f8096 (-0.06 / -0.05 / -0.12 / -0.08; drift since T3.01's -0.31 maximum, not
  bisected further). After: 120 / 120 GREEN, highest peak -0.3018 dBFS (Drums/Punchy_Clip). Every other preset file
  is byte-identical. Rule for later batches: run that `[long]` case by name after any change to the summed output.
- Found by T6.09's denormal tail case (commit "fix(disrumpo): process() runs with FTZ / DAZ whatever the host sets"):
  Disrumpo set no FTZ / DAZ (Innexus, Membrum, Profundum and Seraphis use `Krate::DSP::ScopedDenormalMode`). A Tube
  node's 1 s silence tail after 0.25 s of full-scale noise ran 1.41 / 1.45 / 1.53x slower with the caller's FTZ / DAZ
  cleared (three runs alone, idle machine; bar 1.3; audit baseline 1.12); FeedbackDist, AllpassResonant and the SoftClip
  control 0.91-1.09x. `Processor::process` now opens with the guard. New case "process() flushes denormals whatever the
  caller's floating-point mode": a denormal input on a clean band, test thread FTZ / DAZ cleared: RED 8192 of 8192
  output samples denormal, GREEN 0, the caller's mode restored. The timing case after the fix: Tube 1.08x.
- Slim spare lane (user ruling 2026-11-10, commit "perf(disrumpo): the crossfade's second lane holds only the shapers
  it runs"): the 12.4 MB the crossfade added per band were 12 DistortionAdapters (2 single-type, 2 x 5 morph) at
  1.04 MB each; oversamplers 58 KB, fractional delays < 1 KB. The adapters are now the band's: the running lane keeps
  one per slot (12), the other lane only the spares its path runs (the active nodes, or the one identical-type /
  single-type adapter): 4 per channel, 8 per band, attached when a crossfade starts and swapped into the home set when
  it ends. BandProcessor 25.14 -> 20.98 MB (heap, 48 kHz, prepared), the spare 12.4 -> 8.3 MB; a 4-band instance
  about 100.8 -> 84.2 MB. Renders: 253 of 254 dumps bit-identical (120 factory presets plain and with Oversample Max
  cycling 4x / 2x / 1x / 4x, the morph sweeps, Oversample Max cycling over six type mixes, reversals, mid-crossfade
  node-count changes, a 4-band moving patch, the single-type band); the one difference (9.3e-3, decaying with the
  DC blocker over ~20 ms) is a node-type change to identical types during a crossfade: the identical-type adapter is
  now the slot-0 spare, whose Fuzz processor already held node 0's settings, so its bias / drive smoothers start on
  them instead of gliding from the processor's previous targets. Switch block (4 cross-family nodes, 4x <-> 2x, 512):
  median 561-576 us against 592-631 us before (steady 200-218 us). Remaining floor: follow-up 89.
- Re-level after the random-source reseed (2026-11-10, commit "fix(disrumpo): Stochastic_Burst re-trimmed under -0.3
  dBFS after the random-source reseed"): 54a9c925 moved Experimental/Stochastic_Burst from -0.41 to -0.24 dBFS peak on
  the [D-144] signal (its Random source now restarts on reset). T3.01 rule (lower Output Gain only, 0.1 dB steps):
  trim -9.0 -> -9.1 dB, peak -0.34 dBFS; 120 / 120 ok, highest peaks -0.30 (Punchy_Clip, Modulated_Edge, Wide_Sweep,
  unchanged).
- Re-level for the Ring Saturation carrier fix (2026-11-10, commit "fix(disrumpo): Alien_Voice re-trimmed under -0.3 dBFS
  for the Ring Saturation carrier fix"): with the per-sample carrier (next commit) Experimental/Alien_Voice peaks +0.09
  dBFS on the [D-144] signal (was -0.37; the case's 0 dBFS bar RED). T3.01 rule: trim -1.5 -> -1.9 dB, peak -0.31
  dBFS; 120 / 120 ok, highest -0.30 (Punchy_Clip, Modulated_Edge, Wide_Sweep). Committed first so the fix lands green.
