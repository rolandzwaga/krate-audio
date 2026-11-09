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
