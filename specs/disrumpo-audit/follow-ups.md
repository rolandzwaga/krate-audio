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
