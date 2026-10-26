# Implementation Plan: Profundum Phase 6 — Mass & Dynamics

**Spec:** `specs/profundum-phase6-mass-dynamics/spec.md` (Clarified 2026-10-17)
**Roadmap:** `specs/Profundum-roadmap.md` Part A, Phase 6 (lines 463–486)
**Phase base:** commit `02fecee1` on `synth/profundum`
**Status:** Planned 2026-10-17, revised the same day after review. **Section S1 RATIFIED 2026-10-17** (user: C-1 (a), C-2 (a),
C-3 residual method, C-7 (i) 20 ms bound / 40 ms traversal with no caps, C-13 PlateauEnd — each the recommended option; C-4 … C-12
and the task-list decisions accepted; every ruling is in its spec row and in Clarifications "Plan S1 rulings"). The original
note follows. Five items were flagged ⚠ RE-RULE, because the model (S12) shows that a gated bar cannot be met as written, or because the plan changes a
spec measurement method:
- **C-1** — SC-003's K-weighted span (≤ 3 dB) cannot be met at E0, and very probably not at E1, by any Mass table that
  also meets SC-001(c) and SC-002. The cause is the BS.1770 RLB high-pass, not the design.
- **C-2** — SC-008(c) at the 5 ms duration on a sine at E1 cannot reach ±15 %. The sine's first peak arrives 6.07 ms
  after the onset, so no amplitude detector can see the level in time.
- **C-3** — SC-010's two moving windows (100 ms) cannot be read with the lattice `aliasedPower` helper, and the
  [1 s, 2 s] steady window is too short for the helper's frame at E0. The plan proposes a residual-against-ideal
  method for the moving windows and a longer steady frame.
- **C-7** — SC-004 uses one quantity, "the D-7 traversal time", in both of its clauses. Reading it as 20 ms in the
  per-interval bound and 40 ms in the completion clause reinterprets a gate, so it goes to the user.
- **C-13** — SC-007 measures recovery "from each event's own peak GR". The plan measures it from the end of the 1 s
  event's GR plateau (the last sample within 0.1 dB of the peak). That changes how a gated ratio is computed.

**Until a ruling is written into the SC row (FR-048), every test is built to the spec bar as written.** Each item has
one `constexpr` in its test TU (or, for C-7, in `MassLaw`), and its default is the spec's reading:
`kGateKWeightAt = {true, true, true, true}` (C-1); `kGate5msAt = {true, true}` for {E1, C3} (C-2);
`kSc010Method = Sc010Method::Spec` (C-3); `MassLaw::kSlewBoundRefMs = kMassSweepMs`, one T in both SC-004 clauses (C-7);
`kRecoveryFrom = RecoveryFrom::Peak` (C-13). A ruling flips that one constant. The recommended options are built and
printed beside each default, never in place of it, so FR-048's "never relaxed in place" holds for an unratified build.

Everything else in S1 is a refinement inside the spec's freedom (the plan "chooses and records"), listed so the main loop
can accept it in one pass.

**Model:** `specs/profundum-phase6-mass-dynamics/mass-dynamics-model.js` (Node; per the root CLAUDE.md, no Python).
Run it with `node specs/profundum-phase6-mass-dynamics/mass-dynamics-model.js [kweight|shaper|comp|all]`; it takes about
20 s for everything. Its figures are quoted in S12. They are sizing figures, not compliance evidence.

---

## S0. Verification ledger (every fact below was read in this session)

Paths are relative to `dsp/include/krate/dsp/` unless stated.

| Fact | Where |
|---|---|
| `EnvelopeFollower`, L2, includes `core/db_utils.h` and `primitives/biquad.h` only | `processors/envelope_follower.h:2, 25–26` |
| `enum class DetectionMode : uint8_t { Amplitude = 0, RMS = 1, Peak = 2 }` | `envelope_follower.h:41–45` |
| `kMinAttackMs = 0.1f`, `kMaxAttackMs = 500.0f`, `kMinReleaseMs = 1.0f`, `kMaxReleaseMs = 5000.0f` | `envelope_follower.h:88–91` |
| `void prepare(double sampleRate, size_t maxBlockSize) noexcept` ignores `maxBlockSize`, recomputes coefficients, calls `reset()` | `envelope_follower.h:106–124` |
| `[[nodiscard]] float processSample(float input) noexcept` — "Does NOT validate input"; flushes denormals of `envelope_` | `envelope_follower.h:163–188` |
| Amplitude mode: `envelope_ = rectified + coeff * (envelope_ - rectified)`, attack if `rectified > envelope_` | `envelope_follower.h:296–307` |
| Peak mode: instant only when `attackTimeMs_ <= kMinAttackMs + 0.01f`, otherwise the attack coefficient | `envelope_follower.h:329–346` |
| `setAttackTime(float ms)` / `setReleaseTime(float ms)` clamp with `std::clamp`, then recompute with `detail::constexprExp` | `envelope_follower.h:220–230, 359–373` |
| Coefficient `exp(-2π / (timeMs · 0.001 · fs))`: the time is a ~99 % settling time; τ = T / 2π | `envelope_follower.h:355–365` |
| `getCurrentValue()` returns the envelope without advancing | `envelope_follower.h:192–194` |
| `DynamicsProcessor`, L2 + peer `EnvelopeFollower`; includes `db_utils`, `delay_line`, `smoother`, `biquad`, `envelope_follower` | `processors/dynamics_processor.h:19–23, 61` |
| `enum class DynamicsDetectionMode : uint8_t { RMS = 0, Peak = 1 }` | `dynamics_processor.h:38–41` |
| Ranges: threshold −60…0, ratio 1…100, knee 0…24, attack 0.1…500, release 1…5000, makeup ±24, lookahead 0…10, sidechain 20…500 | `dynamics_processor.h:69–99` |
| `prepare(double, size_t)`: EF prepare, attack/release, detection mode, sidechain biquad, `gainSmoother_.configure(5.0f, sampleRate_)`, `updateLookahead()`, `reset()` | `dynamics_processor.h:121–146` |
| `processSample`: NaN → 0, ±Inf → ±1e10; optional sidechain biquad on the given sample; `gainToDb(envelope)`; `computeGainReduction`; smoother; `currentGainReduction_ = flushDenormal(-smoothedGR)`; output = `input · dbToGain(−GR) · dbToGain(makeup)` | `dynamics_processor.h:166–214` |
| Setters `setThreshold` 243, `setRatio` 251, `setKneeWidth` 258, `setAttackTime` 265, `setReleaseTime` 272 (forwards to the EF, no smoothing), `setMakeupGain` 279, `setAutoMakeup` 285, `setDetectionMode` 291, `setLookahead` 299, `setSidechainEnabled` 306, `setSidechainCutoff` 312 | `dynamics_processor.h` |
| `[[nodiscard]] float getCurrentGainReduction() const noexcept` (≤ 0, smoothed) | `dynamics_processor.h:345–347` |
| `computeGainReduction`: ratio ≤ 1 returns 0; hard knee; quadratic soft knee `slope · x² / (2 · knee)` | `dynamics_processor.h:367–398` |
| `updateLookahead()` calls `lookaheadDelay_.prepare(...)` when lookahead > 0 (allocates) | `dynamics_processor.h:429–437` |
| Members: one `EnvelopeFollower`, one `OnePoleSmoother gainSmoother_`, one `DelayLine`, one `Biquad` — mono | `dynamics_processor.h:465–468` |
| `OnePoleSmoother` class 134; `configure(float smoothTimeMs, float sampleRate)` 160; `setTarget` 170 (NaN → 0 and resets current); `process()` 197 snaps when `|current − target| < kCompletionThreshold (1e-4)`; `snapTo` 263; `reset` 275 | `primitives/smoother.h:55, 134–284` |
| One-pole coefficient `exp(−5000 / (smoothTimeMs · fs))` (99 % at the given time) | `smoother.h:77–93` |
| `SlewLimiter` class 468; `configure(float riseRatePerMs, float fallRatePerMs, float sampleRate)` 501; `configure(float ratePerMs, float sampleRate)` 512; `setTarget` 519 (NaN → 0 and resets current); `process()` 545 (step by the rate, snap within it); `snapTo` 591; rate per sample `unitsPerMs / (sampleRate · 0.001f)` | `smoother.h:114–120, 468–613` |
| `detail::isFinite(float)` (fast-math-immune barrier read) 118; `isNaN` 99; `isInf` 136; `flushDenormal` (|x| < 1e-15 → 0) 263 | `core/db_utils.h` |
| `dbToGain(float)` 289 → `constexprPow10` → `constexprExp`, which returns exactly 1.0f for 0 (line 221); `gainToDb(float)` 313 returns −144 for 0, negative or NaN | `core/db_utils.h:216–252, 289–320` |
| `Biquad` 308; `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` 330; `process(float)` 352 resets and returns 0 on a non-finite input, and flushes `z1_` / `z2_` with `detail::flushDenormal` on every sample (365–366); `reset()` 385; `getZ1()` 391; `kButterworthQ` 56; `FilterType::Highpass` 70; `BiquadCascade<N>` with `setButterworth` | `primitives/biquad.h` |
| `PolyphaseHalfbandOversampler` (L1, zero heap): `kMaxFactor = 4` 34, `setFactor` 49, `factor()` 54, `reset()` 57, `upsample(float x, float* out)` 70, `stateFinite()` 105; LF group delay ≈ 1.9 samples | `primitives/polyphase_halfband_oversampler.h:21–186` |
| `HarmonicFold`'s detector: per-channel true-peak estimate (4× `detOs` + parabolic interpolation of local maxima of \|u\|, `d ≥ |x|`), 8-segment ring max over `kDetectorHoldMs`, instant attack, release with hysteresis — **all private** (`advanceDetector`) | `processors/harmonic_fold.h:94–100, 144–147, 385–436` |
| Its D-4 evidence: a plain windowed sample maximum of a periodic signal with a non-integer period steps as the window slides; the window ripple on a 500-partial saw dropped "from 4e-2 to 0" with the true-peak estimate; before it, the stepping read as −74.9 dBFS aliasing against −80 | `harmonic_fold.h:388–401` |
| `HarmonicFold`: `kCalibrationPeak = 0.3552343859f` 83, `kDcCornerHz = 2.0f` 91, `kAmountSmoothMs = 10.0f` 92, `prepare(double, size_t)` 121, `setAmount(float)` 168 (valid before prepare), `setFundamentalHz(float)` 178, `processBlock(float*, float*, size_t)` 188, mono link 197–205, `latencySamples() == 0` 271 | `processors/harmonic_fold.h` |
| `TruePeakLimiter`: `kDefaultCeilingDb = -1.0f`, `kDefaultReleaseMs = 80.0f` 46–47; `prepare` allocates (`osBufL_.assign`) 58–75; `reset` 78; `setCeilingDb` 85; `setReleaseMs` 91 (`1 − exp(−1/τ)`); `getCeilingLinear()` 98; `processBlock(float* left, float* right, int numSamples)` 104, chunked by `maxBlockSize_`; linked true-peak over raw + 4 oversampled phases; instant attack, one-pole release `currentGain_ += (required − currentGain_) · releaseCoeff_` 157; no NaN sanitisation (comment) | `processors/true_peak_limiter.h` |
| `DCBlocker`: `prepare(double sampleRate, float cutoffHz = 10.0f)` 135 (R = exp(−2π fc / fs)); `reset` 155; `setState` 165; `process(float)` 200 (`y = x − x1 + R·y1`, flushes y1); `processBlock(float*, size_t)` 228 | `primitives/dc_blocker.h` |
| `HilbertTransform` (L1): Niemitalo 4 + 4 allpass, 5-sample latency, "At 44.1 kHz approximately 40 Hz to 20 kHz", bandwidth scales with the rate | `primitives/hilbert_transform.h:81–82, 123–128` |
| `SpectralShapeRecipe::Controls { depth 0.5 [0,1]; body 0.5 [0,1]; bodyCurvature 0.5; bodyEmphasis 0 [−1,0]; edge 0.5; shift 0 [−1,1] }`; `SpectralShapeRecipe::kCenterPanGain = 0.7071067811865476f` is a static member of the class, not of `Controls` (line 52); the Phase 3 test spells it `SpectralShapeRecipe::kCenterPanGain` (profundum_anchor_chain_test.cpp:578, 745), and so does every use in this plan | `processors/spectral_shape_recipe.h:41–52` |
| Named states: `kDefaultControls` (all mid), `kSineAnchor` (depth 1, body 0, edge 0), `kSawAnchor` (depth 0, body 0, edge 0), `kTriangleAnchor`, `kHeavy`, `kHollow`, `kGrowl` | `spectral_shape_recipe.h:78–84, 227–248` |
| `ProfundumCore`: class 92; `kDefaultPartials = 64` 96; `kControlInterval = 32` 97; `kMinF0Hz = 8.0f` 99; `prepare(double, int)` 129 (not RT-safe); `reset` 161; `setControls` 173; `noteOn(float)` 208; `processBlock(float*, float*, size_t, const float* = nullptr)` 237; `intervalPhase_` tracked across calls 285–286; `deliveredGains()` 298 | `systems/profundum_core.h` |
| `AnchorOscillator`: `kControlInterval = 32`; `kMaxCoreH1Amplitude = 0.3552343859f`; `kDefaultLevel = 0.5f`; `kLevelSmoothMs = 5.0f`; `struct Controls { Waveform; Mode; float level; punchBoostDb; punchTimeMs; ghostTimeMs }`; `prepare(double)` 123; `setControls` 224 (latched at offset 0 of the next block); `setCoreH1Amplitude(float)` 262 (non-finite keeps previous; clamps); `noteOn(float, bool)` 276; `processBlock(..., f0PerSample, followRatioPerSample)` 285 | `systems/anchor_oscillator.h:55–106, 220–300` |
| The Phase 3 core + Anchor feed pattern: core block, then `setCoreH1Amplitude(deliveredGains()[0] · SpectralShapeRecipe::kCenterPanGain)`, then the Anchor block | `dsp/tests/unit/systems/profundum_anchor_chain_test.cpp:575–583, 742–746` |
| `applyModCurve(ModCurve, float)` (clamps x to [0, 1]; Linear, Exponential x², SCurve x²(3 − 2x), Stepped) | `core/modulation_curves.h:38–57` |
| `enum class ModCurve : uint8_t { Linear = 0, Exponential = 1, SCurve = 2, Stepped = 3 }` | `core/modulation_types.h:79–84` |
| `VoragoMacroRow { macro; owner; target; float base; float amount; ModCurve curve; }` — the row pattern | `systems/vorago_macro_matrix.h:233–257` |
| Test helpers: `renderedHarmonicPowers` 235, `describeRendered` 251, `aliasFftLength` 257 (pow2 ≥ max(8192, 64·fs/f0)), `AliasResult` / `aliasedPower` 265, 273 (BH7, ±8 bins around every n·f0 and bins 0–8 excluded), `rmsDbfs` 453 | `dsp/tests/unit/systems/profundum_core_test_helpers.h` |
| `ShapeCharacterTest::ClickCount` 44, `differentialClicks(...)` 76 and 137, `steadyDeltaMax` 147, `centroidOct` 193, `levelDb` 227 | `dsp/tests/unit/processors/shape_character_metric_helpers.h` |
| `ShapeCharacterTest::Descriptors` 93, `describe(const float*, size_t, double fs, double f0)` 107, `separate` 171, `amountMoved` 202, `levelMatch` 215, note constants 50–55 | `dsp/tests/unit/systems/shape_character_test_helpers.h` |
| `TestUtils::fingerprintRender` 73, `compareFingerprints` 122 | `tests/test_helpers/render_fingerprint.h` |
| `TestHelpers::AllocationScope` 111 | `tests/test_helpers/allocation_detector.h` |
| `DynamicsProcessor::reset()`: `envelopeFollower_.reset()`, `sidechainFilter_.reset()`, `gainSmoother_.reset()`, `lookaheadDelay_.reset()`, `currentGainReduction_ = 0.0f` | `processors/dynamics_processor.h:150–156` |
| `AnchorOscillator::pendingControls()` returns the latched-pending `Controls` (`pending_`), "C-11 read-back" | `systems/anchor_oscillator.h:361` |
| `AnchorOscillator` has no public a₁ accessor: `a1Held_` / `a1S_` are private (setCoreH1Amplitude 265–266; members 1164–1165) | `systems/anchor_oscillator.h` |
| `TestUtils::kLowFrequencyFftSize = 262144` (5.46 s at 48 kHz, 5.94 s at 44.1 kHz) | `tests/test_helpers/low_frequency_metrics.h:64` |
| `ShapeCharacterTest::describe` does `REQUIRE(n >= kFrame)` with kFrame = `kLowFrequencyFftSize`, and reads the last kFrame samples | `dsp/tests/unit/systems/shape_character_test_helpers.h:107–116` |
| `renderedHarmonicPowers` always reads exactly one `kLowFrequencyFftSize` frame | `dsp/tests/unit/systems/profundum_core_test_helpers.h:235–240` |
| `profundum_core.h` includes `harmonic_oscillator_bank.h`, `harmonic_types.h`, `spectral_shape_recipe.h` and four L0 / L1 headers | `systems/profundum_core.h:19–26` |
| `test_helpers` is an INTERFACE library; adding a header there needs no CMake change | `tests/test_helpers/CMakeLists.txt:7–18` |
| Probe precedent: `struct detail::BassCharacterStageProbe;` forward-declared, `friend` in the class, the probe in a test header | `systems/bass_character_stage.h:71, 513`; `dsp/tests/unit/systems/bass_character_stage_probe.h` |
| Test registration: `dsp_processors_tests` Phase 5 block 280–290; `dsp_systems_tests` Phase 5 block 691–707; the `-fno-fast-math` `set_source_files_properties` block ends at line 1200 | `dsp/tests/CMakeLists.txt` |

**ODR sweep (run 2026-10-17, `grep -rnE "(class|struct|enum class|enum|using) <Name>\b" dsp/ plugins/ tests/ tools/`
and `find dsp plugins tests -iname "*<file>*"`): 0 hits** for `TransientShaper`, `MassLaw`, `MassDynamics`,
`BassCompressor`, `PeriodPeakDetector`, `TransientShaperProbe`, `MassDynamicsProbe`, `BassCompressorProbe`, `MassLawProbe`,
`MassChainFixture`, `KWeightingFilter`, and for the files `transient_shaper*`, `mass_law*`, `mass_dynamics*`,
`period_peak*` and `k_weight*`. A broader sweep for `(class|struct) [A-Za-z]*PeakDetector` and for any `class`/`struct`
named `Mass*` under `dsp/include` also returned 0 hits.

---

## S1. Spec conflicts and refinements — FOR RATIFICATION

### C-1 ⚠ RE-RULE (SC-003, FR-028): the K-weighted span cannot be met at E0, and very probably not at E1

**Finding.** The BS.1770 RLB stage is a 2nd-order high-pass at about 38 Hz with Q ≈ 0.5. On a steady tone at equal
unweighted RMS, the model (S12.1) reads these K-weighted offsets:

| Note | pure sine | 1/n saw, 64 partials | saw − sine |
|---|---|---|---|
| E0 (20.60 Hz) | −12.88 dB | −5.94 dB | **6.95 dB** |
| E1 (41.20 Hz) | −5.32 dB | −2.90 dB | 2.42 dB |
| E2 (82.41 Hz) | −1.64 dB | −0.82 dB | 0.81 dB |
| C3 (130.81 Hz) | −0.66 dB | −0.11 dB | 0.55 dB |

The spec forces the endpoints apart:
- SC-002 makes Mass 0 essentially a sine on the reference voice (THD ≤ −30 dB).
- SC-002 / SC-011 make Mass 100 rich (THD ≥ THD(50) + 6 dB, audibility ≥ 3 dB).
- SC-001(c) forbids the fundamental energy of the sum from falling as Mass rises.
- The only level lever, the Anchor level, must rise with Mass (D-2), and the core's level is fixed by the recipe
  normalisation.

So the unweighted level cannot fall from Mass 0 to Mass 100, and the K-weighted level rises by roughly the saw − sine
column at fixed unweighted level.

Worked estimate at E0, with the Anchor sized for SC-001(c) and the core at its −12 dBFS calibration:
- Mass 0: −12 − 12.9 ≈ −24.9 (K-weighted).
- Mass 100: the h1 energy is held at the Mass 0 value, giving −24.9 K-weighted. The harmonics add about −16 + 0.5 dB.
  The total is ≈ −15.5.
- The span is ≈ **9 dB**, against a 3 dB bar.

At E1 the same estimate gives ≈ 3.9 dB. At E2 and C3 it gives ≈ 2.1 dB and ≈ 1.8 dB, which are feasible.

**Options.**
- **(a) Recommended.** Gate the K-weighted span and |ΔP| ≤ 0.2 dB per unit at **E2 and C3**. At E0 and E1, gate the
  unweighted span (≤ 6 dB) and record P. The spec already notes that K-weighting "under-reads below about 38 Hz"
  (Descriptor definitions). This option treats that roll-off as the reason P is not a loudness proxy at E0 / E1.
- **(b)** The user rules wider K-weighted spans at E0 and E1 (for example ≤ 10 dB and ≤ 4.5 dB), set after the T001
  calibration pass measures the real table.
- **(c)** Keep the bar at all notes and accept that SC-003 fails at E0 (FR-048 then reports it as a ruling).

The plan builds the test so that all three options are a one-line change (S13.3, SC-003 row). **Until the ruling is
written into the SC-003 row, the default is the spec bar**, `kGateKWeightAt = {true, true, true, true}`. An unratified
build therefore gates E0 and E1, and FR-048 reports the measured shortfall. The calibration target (S8.2 step 3) is
written against the spec bar, with option (a) as the conditional alternative.

### C-2 ⚠ RE-RULE (SC-008(c)): the 5 ms duration point is waveform-limited at E1

**Finding.** The model (S12.2) uses the S4 design and one calibration table entry per duration, chosen so that the mean
log error over the four SC-008 cells is zero:

| Duration | sine E1 | saw E1 | sine C3 | saw C3 |
|---|---|---|---|---|
| 5 ms | **+30.0 %** | −11.3 % | −1.7 % | −11.7 % |
| 10 ms | +9.4 % | −10.8 % | −6.0 % | +9.2 % |
| 20 ms | +4.6 % | −5.3 % | −2.9 % | +4.0 % |
| ≥ 40 ms | ≤ 3.6 % | | | |

At 5 ms the four cells span 4.42 to 6.50 ms. The best possible single entry (minimax) is still about ±19 %. The cause:
- The burst starts at a zero crossing, and a sine at E1 reaches its first peak 6.07 ms later (a quarter period).
- Until then no amplitude detector can know the level.
- The Hilbert alternative cannot rescue it. `HilbertTransform` is accurate only from about 40 Hz at 44.1 kHz, and its
  band scales up with the rate (hilbert_transform.h:123–125), so it fails at E0–E1, and at 192 kHz its floor is about
  175 Hz.

**Options.**
- **(a) Recommended.** Gate SC-008(c)'s 5 ms point at C3 only (sine and saw), and record E1. Every other duration is
  gated at both notes.
- **(b)** Gate the five durations {10, 20, 40, 100, 250} ms at both notes, and record 5 ms.
- **(c)** Keep the bar. FR-048 then reports E1-sine at 5 ms as a failure.

**Default until ruled:** the spec bar, the 5 ms point gated at E1 and C3 (`kGate5msAt = {true, true}` for {E1, C3}).
Option (a) is `{false, true}`; option (b) drops 5 ms from the gated set. Every cell's error is printed either way.

### C-3 ⚠ RE-RULE (SC-010 method): the moving windows need a residual-against-ideal measurement; the steady frame is longer

**Finding 1: the steady window.** `aliasFftLength` needs ≥ 64·fs/f0 samples: 262144 at E0 and 48 kHz, which is 5.46 s
(profundum_core_test_helpers.h:257). A [1 s, 2 s] window holds at most a 32768-sample power-of-two frame. That frame's
±8-bin exclusion is ±11.7 Hz around harmonics 20.6 Hz apart, so it excludes everything and the reading is vacuous.

**Finding 2: the moving windows.** The 100 ms windows ([0, 100 ms] after the burst onset, and the step transition)
have the same problem, worse. In addition, a gain that moves in a 5 ms transient spreads legitimate AM sideband energy
far beyond ±f₀/8. The lattice helper would count that energy as "aliased" although none of it folds.

**Proposed method.**
- **Steady.** The frame is `aliasFftLength(fs, f₀)` samples starting at 1 s after onset. The render is 1 s + 5.46 s at
  E0, and shorter elsewhere. The gate is unchanged: A_out ≤ max(−80 dBFS, A_in + 0.5 dB) via `aliasedPower`, on the same
  frame position for in and out.
- **Moving windows.** The aliasing a time-varying gain adds is r = y − LPF_{fs/2}(g_c · x_c), where g_c and x_c are the
  band-limited interpolations of the recorded gain trace g[n] (from the diagnostic taps) and the input x[n].
  - It is computed offline in double: an 8× FFT zero-pad upsample of x and g over [t₀ − 50 ms, t₀ + 150 ms] with
    50 ms Hann tapers, the product, a brick-wall cut at fs/2, and decimation.
  - A_r = 10·log10(mean(r²) / 0.5) dBFS over [t₀, t₀ + 100 ms], using the same "re 0.5" normalisation as `aliasedPower`.
  - The input is band-limited, so A_in is 0 by construction. The gate becomes **A_r ≤ −80 dBFS**.
  - A positive control proves the method has teeth: a hard 0 → +12 dB gain step applied to a C3 saw must read above
    −80 dBFS (repaired 2026-10-18, M-2: applied to the **Fold-100** C3 saw; on the plain saw it read −82.5 dBFS at 96 kHz, under
    the bar by construction).
  - The Fold-100 input is recorded, not gated (user ruling 2026-10-18, G9-1; spec SC-010 row): the dynamics' sample-rate-native gain
    folds the sidebands of an input whose content reaches Nyquist, and the 4× true-peak estimate of that wave wobbles with its
    sub-sample phase at E2; the oversampled fix is a Phase 9 chain-budget decision.

FFT lengths and resolutions are printed per cell (S13.3).

**Default until ruled:** the spec's method (`kSc010Method = Sc010Method::Spec`): `aliasedPower` on the [1 s, 2 s] steady
window and on the two 100 ms windows, gated A_out ≤ max(−80 dBFS, A_in + 0.5 dB). A cell whose frame excludes every
lattice bin (Finding 1) is flagged `vacuous`, reported under FR-048 and not counted as a pass. The proposed method above
is built and run beside it, and its readings are printed in every cell, so the ruling has measurements in hand. The
ruling flips the constant to `Sc010Method::Residual`.

**Group 7 refinement (2026-10-17, main loop; spec SC-010 row).** Over leading exact-zero input the recorded gain trace is held at
its value on the first non-zero sample before the 8× band-limiting: the shaper's silence-branch step (S4.3 step 5) is unobservable
on zero samples, and band-limiting it alone read −67 dBFS on an output that is exactly g · x (held: −120.67 dBFS).

### C-4 (refinement, new L2 header): `PeriodPeakDetector`, a public copy of HarmonicFold's private D-4 detector

Both the shaper and the compressor need a detector that is ripple-free on a steady bass note, which FR-004 and SC-006(a)
require. The model (S12.3) shows that the compressor's steady GR ripple at E0 is 3.20 dB peak-to-peak without a
one-period hold and 0.002 dB with one. HarmonicFold's D-4 note (harmonic_fold.h:388–401) shows a plain windowed maximum
still steps, and reads as −74.9 dBFS aliasing, unless the sample peak is refined to a true-peak estimate.

That detector exists, but only privately inside `HarmonicFold` (`advanceDetector`, harmonic_fold.h:402–436). FR-044 and
the Non-Goals forbid changing `HarmonicFold`. The plan therefore adds `processors/period_peak_detector.h` (L2, ODR 0 hits),
which ports the same algorithm with a settable hold. This is a **conscious, recorded duplication**. Making `HarmonicFold`
compose `PeriodPeakDetector` is noted as a follow-up for the user. It is not done in this phase, and it is not done
silently.

**Group 2 gate ruling (user, 2026-10-17; spec FR-004 row, Clarifications "Group 2 gate ruling").** The D-4 fixture's 1e-4 bar is
HarmonicFold's follower reading (0.5 % release hysteresis, 125 ms hold), not a raw held-value figure. `PeriodPeakDetector` has no
follower, so at the consumers' 50 ms hold the raw estimate on Gibbs peaks reads 1.43e-3 / 1.44e-3 / 7.39e-4 (44.1 / 48 / 96 kHz) on the
wide saw and ≤ 1.25e-5 on 64 partials. Ruled: 64 partials keep 1e-4; the wide arm is gated at 2.3e-3 (0.02 dB peak-to-peak, one tenth
of SC-009's 0.2 dB). The algorithm is unchanged.

### C-5 (refinement, FR-002 / FR-008): the shaper's slow follower is ticked once per 8 samples

- On a step onset the gain falls 3 dB below its peak when the slow envelope is within 9 dB of the fast one. That happens
  at t = τ · ln(1 / (1 − 10^(−9/20))) = 0.438 τ, with τ = T / 2π (envelope_follower.h:359–365).
- A 250 ms duration therefore needs a 3.59 s attack. `EnvelopeFollower` clamps its attack at 500 ms
  (envelope_follower.h:89, 221).
- The slow follower is therefore `prepare(fs)`-ed but advanced once every `kSlowTickSamples = 8` samples. Its time
  argument is then real time ÷ 8, so the real range is 0.8 ms … 4 s.
- Between ticks the slow value is linearly interpolated, so the gain has no 8-sample staircase.
- The model's harvested argument for 250 ms is 444.3 ms, which is under the clamp with 11 % margin.
- The tick phase is part of the state. It is reset by `reset()` and tracked across calls, so the shaper stays
  partition-invariant.

### C-6 (refinement, FR-011 / FR-012 / FR-013): the compressor's detection signal

D-6's form (b), made concrete:
1. Each channel is high-passed by its own `Biquad` (RBJ high-pass, Q = `kButterworthQ`) at the keytracked corner.
2. Each HP output gets the `PeriodPeakDetector` true-peak estimate.
3. d = max(tp_L, tp_R) feeds a hold of exactly one period of the key f₀ (hold = 2 / corner).
4. The held value drives the shared `DynamicsProcessor` in `DynamicsDetectionMode::Peak` with its internal sidechain off.

The high-pass sees signed samples, as FR-011 requires. The hold is what makes M_sub ripple-free (S12.3: 0.002–0.041 dB
peak-to-peak at E0–C3).

The model checks SC-007 with this hold (S12.3):

| | E1 | C3 |
|---|---|---|
| short / long recovery under the D-5 rule | 0.227 | 0.201 |
| fixed 275 ms positive control | 1.022 | 0.999 |

The rule passes the ½ bar, and the positive control fails it, as it must.

### C-7 ⚠ RE-RULE (FR-025 / SC-004 with D-7): one traversal time in SC-004, or two

SC-004 bounds each target's change per interval by `range × (32/fs) / T`, and requires the traversal to complete within
T ± one control interval. Both clauses name the same quantity, "the D-7 traversal time". D-7 ratified T ≥ 20 ms. The
plan's slew is `kMassSweepMs = 40.0f`, so a full 0 → 100 traversal takes 40 ms.

With a bipolar law, one side of the sweep (Mass 50 → 100, half the Mass travel) can move a target by up to its full
range when the base sits at the range end (the Fold and compression amount bases are 0). Per interval the change is
amount · maxSlope · 2 · (32/fs) / T. With one consistent T, the bound therefore holds only when
amount · maxSlope ≤ (hi − lo) / 2, and a full-range row fails it by 2× at any T.

**Options.**
- **(i) Recommended: the dual reading.** The per-interval bound uses the D-7 minimum, 20 ms; the completion clause uses
  the plan's 40 ms. Rows may reach their full range: max(|aUp|, |aDown|) · maxSlope ≤ hi − lo. Because this reads one
  spec quantity two ways, it needs the ruling written into SC-004's row.
- **(ii) The spec as written: one T = 40 ms in both clauses.** Every row is capped so that
  max(|aUp|, |aDown|) · maxSlope(curve) ≤ (hi − lo) / 2. Fold then reaches 50 and compression 0.5 above their 0 bases at
  Mass 100.

**Default until ruled: (ii)**, through `MassLaw::kSlewBoundRefMs = kMassSweepMs`. One `static_assert` covers both
options: max(|aUp|, |aDown|) · maxSlope(curve) ≤ (hi − lo) · kMassSweepMs / (2 · kSlewBoundRefMs), with maxSlope = 1, 2
and 1.5 for Linear, Exponential and SCurve; `Stepped` is forbidden. The S8.2 seed obeys the (ii) cap. A ruling for (i)
sets `kSlewBoundRefMs = 20.0f`, which relaxes the cap to the full range and lets the seed's Fold and compression rows
return to +60 and +1. Either way the ruled form is written into SC-004's row before the test constant changes.

### C-8 (refinement, FR-015): placement option (b)

The composition goes in **`systems/mass_dynamics.h`** (L3), as two classes:
- `BassCompressor`: the `DynamicsProcessor` configuration, the detector, the release rule, the amount table and the
  keytracked corner.
- `MassDynamics`: TransientShaper → BassCompressor, with its own control grid.

`MassLaw` is in `systems/mass_law.h` (L3). Both class names and both file names swept 0 hits (S0). The order is
TransientShaper → compressor, as FR-015 requires. No measurement reverses it.

### C-9 (refinement, FR-022 / FR-027): the row form

Each row is `{target, lo, hi, amountDown, amountUp, curve}`. With u = (mass − 50) / 50 and s = applyModCurve(curve, |u|):

`target = clamp(base + (u ≥ 0 ? amountUp · s : −amountDown · s), lo, hi)`

- **Monotonic.** `amountUp · amountDown ≥ 0` and the sign is the D-2 direction. Both are `static_assert`-ed per row,
  so the output is monotone in Mass, with clamped runs allowed.
- **Neutral.** At mass == 50 exactly, u == 0, s == 0 (Linear, Exponential and SCurve all return 0 for 0), and the
  result is base + 0.0f == base bit-exactly. The base is in range because it is clamped by `setBase` (FR-029).
- The asymmetric amounts are the minimum generalisation of the Vorago row (vorago_macro_matrix.h:233–257) that lets
  Mass 0 purify and Mass 100 thicken by different amounts.

### C-10 (refinement): constants the spec leaves to the plan

| Constant | Value | Why |
|---|---|---|
| Shaper default duration (`kDefaultDurationMs`) | 40 ms | The spec defaults attack / sustain only. 40 ms matches `AnchorOscillator::Controls::punchTimeMs` (anchor_oscillator.h:104), the instrument's other transient time |
| Shaper hold (`kHoldMs`) | 50 ms | ≥ the E0 period (48.54 ms); the shaper has no f₀ input (FR-001) |
| Shaper fast follower | Amplitude, attack 0.5 ms, release 40 ms | Continuous gain at the onset (no instant step) |
| Shaper slow release (real) | 60 ms (argument 7.5 ms) | ≥ the fast release, so c = 0 on decays (S4.4) |
| Shaper gain-parameter smoothing | `OnePoleSmoother`, 10 ms | SC-008(d) clicks; `HarmonicFold::kAmountSmoothMs` precedent (harmonic_fold.h:92) |
| Detector high-pass | one RBJ `Biquad` HP per channel, Q = `kButterworthQ` | FR-011. Steeper (`BiquadCascade<2>`) only if SC-006 fails at E0 (R-4) |
| Fallback corner, no voice (FR-018) | 80 Hz | `DynamicsProcessor::kDefaultSidechainHz` / `EnvelopeFollower::kDefaultSidechainHz` (dynamics_processor.h:99; envelope_follower.h:96) |
| Corner smoothing | `OnePoleSmoother` on log₂(corner), 20 ms, run at the interval rate fs/32 | Continuous across a Drop sweep (SC-024(b)) |
| Compressor attack | 10 ms, constant across amounts (`DynamicsProcessor::kDefaultAttackMs`, line 83) | Above `kMinAttackMs`, so the EF Peak mode uses its coefficient (envelope_follower.h:335–341) |
| Makeup smoothing | `OnePoleSmoother`, 5 ms | Matches the GR smoother (dynamics_processor.h:140) |
| FR-018 setter name | `BassCompressor::setDetectorKeyHz(float f0Hz)` / `MassDynamics::setDetectorKeyHz` | ODR: member functions, no namespace-scope name |
| Fixture DC blocker | `DCBlocker::prepare(fs, 5.0f)` per channel | −0.25 dB at E0; Phase 9 rules the engine's value |
| Fixture limiter | `TruePeakLimiter`, defaults (−1 dBFS, 80 ms), `prepare(fs, 512)` | FR-033 |
| The s(t) second reading | `BiquadCascade<2>::setButterworth(Lowpass)` at 1.5·f₀ (4th-order Butterworth) | The spec says "≤ 1.5·f₀". Its analog group delay at f₀ is 3.4046 / (2π · 1.5 f₀) = **0.361 periods** (17.5 ms at E0, 8.8 ms at E1, 4.4 ms at E2, 2.8 ms at C3). The test prints the measured digital value |

### C-11 (refinement, FR-049 / SC-018): where the grid lives and how the tests hit it

- `MassLaw` has no audio clock. Its caller (Phase 9's engine; the Phase 6 fixture) advances it once per 32-sample
  interval of an absolute sample counter.
- `MassDynamics` and `BassCompressor` each keep an `intervalPhase_`. It is reset by `prepare` and `reset`, and advanced
  across `processBlock` calls (the `ProfundumCore` pattern, profundum_core.h:285–286). The two grids are aligned by
  construction.
- Setters on those two classes write a pending value that is latched at the next interval boundary.
- SC-018's mid-render Mass step and release-rule change are issued at sample **8704**: lcm(17, 32, 64, 512), with 1
  dividing everything. At that sample every partition makes the call at the same absolute position.

### C-12 (record): smoothing per Mass target (FR-025)

| Target | Smoothed by | Double-smoothed? |
|---|---|---|
| all six | `MassLaw`'s `SlewLimiter` on Mass (40 ms full range) | — |
| Depth, Body | `ProfundumCore`'s recipe slew (profundum_core.h:142–147) | No |
| Fold amount | `HarmonicFold`'s 10 ms `amountSmoother_` (harmonic_fold.h:92, 168–171) | No |
| Anchor level | `AnchorOscillator`'s 5 ms `levelS_` (anchor_oscillator.h:75) | No |
| Duration | none: the slow EF's attack coefficient changes once per interval; its state is continuous | No |
| Compression amount | threshold, ratio and knee change once per interval and are smoothed by `DynamicsProcessor`'s 5 ms GR smoother; makeup by the new 5 ms makeup smoother | Makeup only, new (it had no smoother) |

### C-13 ⚠ RE-RULE (SC-007): where the recovery time starts

**Finding.** SC-007 measures recovery "from each event's own peak GR". On the 1 s event the GR reaches its peak within
the 10 ms attack and then sits on a plateau for about 1 s, with 0.008–0.041 dB of ripple (S12.3). "The peak" is then
whichever plateau sample the ripple happens to make largest. Starting from its first occurrence counts almost the whole
plateau toward the long event's recovery time. That inflates the long time and lets short ≤ ½ long pass for a reason
unrelated to the release rule.

**Proposed reading.** Recovery starts at the last sample within 0.1 dB of the event's peak GR, the end of the plateau,
and ends where GR has recovered by half the peak (in dB). The S12.3 model figures (0.227 / 0.201 with the rule, 1.022 /
0.999 for the fixed-release control) use this reading. The model's first run used the first-occurrence reading and was
corrected (Review notes).

**Default until ruled:** the spec's wording, taken as the first sample at which |GR| is maximal
(`kRecoveryFrom = RecoveryFrom::Peak`). The C-13 reading is computed and printed in every cell. The ruling is written
into SC-007's row in spec.md first, and then flips the constant to `RecoveryFrom::PlateauEnd`.

---

## S2. Architecture

```
            Phase 6 test fixture (FR-033)  ── Phase 9 owns the real wiring ──
 MassLaw (L3, systems/mass_law.h)
   setMass / setBase ──► advanceInterval() once per 32 samples ──► Targets{depth, body, durationMs, fold, compression, anchorLevel}
        │ voice-side                                           │ global
        ▼                                                       ▼
 ProfundumCore.setControls(depth, body) ─► core L/R ─► HarmonicFold.setAmount(fold) ─┐
        │ a1 = recipe h1 of the voice's OWN controls (pre-bias)·kCenterPanGain (G8-3)    │ (+ plain float add)
 AnchorOscillator.setControls(level) + setCoreH1Amplitude(a1) ─► anchor L/R ─────────┤
                                                                                      ▼
 MassDynamics (L3, systems/mass_dynamics.h): TransientShaper (L2) ──► BassCompressor ──► output trim (FR-021, G8-5) ──► DCBlocker ×2 ──► TruePeakLimiter
        setTransientDurationMs(durationMs)        setCompressionAmount(compression)
                                                  setDetectorKeyHz(f0 of the lowest sounding voice)

 TransientShaper:  x ─ sanitise ─► PeriodPeakDetector (tp L, tp R, max, 50 ms hold) ─► m
                   m ─► fast EF (per sample) ┐
                   m ─► slow EF (every 8 samples, interpolated) ┴► c = clamp((dB(fast/slow))/12, 0, 1)
                   gain_dB = A·c + S·(1 − c) ─► dbToGain ─► L·g, R·g
 BassCompressor:   x ─ sanitise ─► HP_L, HP_R (2·f0 keytracked) ─► PeriodPeakDetector (tp, max, hold 1/f0)
                   ─► DynamicsProcessor::processSample(held)   (Peak mode, internal sidechain off; output unused)
                   ─► getCurrentGainReduction() + makeup(smoothed) ─► one gain ─► L·g, R·g
                   every 32 samples: release rule (D-5), latched amount row, latched key → corner smoother
```

New headers: `processors/period_peak_detector.h` (L2), `processors/transient_shaper.h` (L2), `systems/mass_dynamics.h`
(L3) and `systems/mass_law.h` (L3). The new test-side header is `tests/test_helpers/k_weighting.h`. **No existing
header is modified** (FR-044).

---

## S3. `PeriodPeakDetector` (Layer 2, `processors/period_peak_detector.h`, NEW — C-4)

Includes: `core/db_utils.h` (L0) and `primitives/polyphase_halfband_oversampler.h` (L1), plus `<algorithm>`, `<array>`,
`<cmath>` and `<cstddef>`.

```cpp
namespace Krate::DSP {
class PeriodPeakDetector {
public:
    static constexpr std::size_t kSegments = 8;              // == HarmonicFold::kDetectorSegments (test static_assert)
    static constexpr std::size_t kPeakOversampling = 4;      // == HarmonicFold::kDetectorPeakOversampling
    static constexpr float kMinHoldMs = 1.0f;
    static constexpr float kMaxHoldMs = 125.0f;              // HarmonicFold::kDetectorHoldMs (8 Hz floor)
    static constexpr std::size_t kMaxChannels = 2;

    void prepare(double sampleRate, float holdMs) noexcept;  // allocation-free; sets segLen; reset()
    void reset() noexcept;                                    // zero ring, segment, upsampler and parabola state
    void setHoldMs(float holdMs) noexcept;                    // non-finite keeps previous; clamp; applied at the next segment boundary
    [[nodiscard]] float process(float x) noexcept;            // one channel: held true-peak of |x|
    [[nodiscard]] float process(float xL, float xR) noexcept; // held max(tp_L, tp_R)
    [[nodiscard]] float held() const noexcept;                // last output
    [[nodiscard]] float holdMs() const noexcept;
    [[nodiscard]] bool stateFinite() const noexcept;
private:
    struct Estimator { PolyphaseHalfbandOversampler os; std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{}; float prev1 = 0.0f; float prev2 = 0.0f; };
    [[nodiscard]] float truePeak(Estimator& e, float x) noexcept;   // HarmonicFold advanceDetector's first half, verbatim
    [[nodiscard]] float pushHeld(float d) noexcept;                  // its ring-max half, with a pending segLen
    std::array<Estimator, kMaxChannels> est_{};
    std::array<float, kSegments> ring_{};
    float ringMax_ = 0.0f, segMax_ = 0.0f, held_ = 0.0f;
    std::size_t segCount_ = 0, ringPos_ = 0, segLen_ = 1, pendingSegLen_ = 1;
    double sampleRate_ = 48000.0;
    float holdMs_ = 50.0f;
    bool prepared_ = false;
};
}
```

**`truePeak`** is a line-for-line port of harmonic_fold.h:403–417:
- d = |x|;
- upsample 4× (`os.setFactor(kPeakOversampling)` in prepare);
- for each phase, take the larger of |u| and, at a local maximum of the |u| sequence, the parabolic vertex
  `b − 0.25 (a − d0) · offset`, with `offset = 0.5 (a − d0) / (a − 2b + d0)`.

The denominator is negative whenever the branch runs (b ≥ a and b > d0), so the division is safe. The result is ≥ |x|.

**`pushHeld`** ports harmonic_fold.h:418–427, with two additions:
- When a segment completes, `segLen_ = pendingSegLen_`, so a hold change takes effect at a boundary.
- The output is max(segMax_, ringMax_).

`segLen = max(1, ceil(round(hold · fs) / 8))`, as harmonic_fold.h:145–147. The hold therefore covers between W and
9W/8. There is no follower, attack or release here: the followers belong to the `EnvelopeFollower`s downstream, as
FR-002 and FR-010 require.

**Contract.**
- Fixed size, zero heap.
- `prepare` is allocation-free (`PolyphaseHalfbandOversampler` is heap-free, polyphase_halfband_oversampler.h:21–23).
- The input must be finite; the callers sanitise first.
- FTZ / DAZ is a precondition. The oversampler is not flushed (its header, line 24). Ring values are ≥ 0 and bounded.
- Latency 0 on the audio path. The detector's own ≈ 1.9-sample upsampler delay only affects the peak estimate, and
  d ≥ |x| always holds.

---

## S4. `TransientShaper` (Layer 2, `processors/transient_shaper.h`)

Includes: `core/db_utils.h`, `primitives/smoother.h`, `processors/envelope_follower.h` (peer L2; the precedent is
dynamics_processor.h:23) and `processors/period_peak_detector.h` (peer L2).

### S4.1 Public API (FR-001, FR-006, FR-009)

```cpp
namespace Krate::DSP {
namespace detail { struct TransientShaperProbe; }
class TransientShaper {
public:
    // ---- D-3 ranges and FR-009 defaults ----
    static constexpr float kMinGainDb = -12.0f, kMaxGainDb = 12.0f;
    static constexpr float kMinDurationMs = 5.0f, kMaxDurationMs = 250.0f;
    static constexpr float kDefaultAttackDb = 6.0f, kDefaultSustainDb = 0.0f, kDefaultDurationMs = 40.0f;   // C-10
    // ---- FR-008 law ----
    static constexpr float kDifferentialRangeDb = 12.0f;     // K (ratified Q5)
    // ---- detector design (S4.3, C-5, C-10) ----
    static constexpr float kHoldMs = 50.0f;
    static constexpr float kFastAttackMs = 0.5f, kFastReleaseMs = 40.0f;
    static constexpr float kSlowReleaseMs = 60.0f;           // real time
    static constexpr std::size_t kSlowTickSamples = 8;
    static constexpr float kGainSmoothMs = 10.0f;
    // ---- FR-008 duration calibration: duration (ms) -> slow EF attack argument (ms), log-log interpolation ----
    struct DurationPoint { float durationMs; float slowAttackArgMs; };
    static constexpr std::array<DurationPoint, 6> kDurationTable{{   // SEED from the model (S12.2); T010 harvests and replaces
        {.durationMs = 5.0f, .slowAttackArgMs = 7.308f},   {.durationMs = 10.0f, .slowAttackArgMs = 15.351f},
        {.durationMs = 20.0f, .slowAttackArgMs = 33.314f}, {.durationMs = 40.0f, .slowAttackArgMs = 67.690f},
        {.durationMs = 100.0f, .slowAttackArgMs = 175.324f}, {.durationMs = 250.0f, .slowAttackArgMs = 444.323f}}};

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;   // maxBlockSize unused (per-sample core)
    void reset() noexcept;
    void setAttackGainDb(float dB) noexcept;      // non-finite keeps previous; clamp; valid before prepare (stored)
    void setSustainGainDb(float dB) noexcept;
    void setTransientDurationMs(float ms) noexcept;
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept;   // in place; right == left -> one channel
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }
    // ---- FR-006 diagnostic taps (read-only, allocation-free) ----
    [[nodiscard]] float currentGainDb() const noexcept;        // gain applied to the last sample
    [[nodiscard]] float transientFraction() const noexcept;    // c of the last sample
    [[nodiscard]] float fastEnvelope() const noexcept;         // fast EF value used for the last sample
    [[nodiscard]] float slowEnvelope() const noexcept;         // interpolated slow value used for the last sample
    [[nodiscard]] float smoothedAttackDb() const noexcept;     // A_s used for the last sample (SC-008(f))
    [[nodiscard]] float smoothedSustainDb() const noexcept;    // S_s used for the last sample (SC-008(f))
    [[nodiscard]] float attackGainDb() const noexcept;         // sanitised targets
    [[nodiscard]] float sustainGainDb() const noexcept;
    [[nodiscard]] float transientDurationMs() const noexcept;
    [[nodiscard]] bool stateFinite() const noexcept;
private:
    friend struct detail::TransientShaperProbe;
    ...
};
}
```

### S4.2 State layout (fixed size, no heap)

`PeriodPeakDetector det_` (≈ 0.2 KB) · `EnvelopeFollower fast_, slow_` · `float slowPrev_, slowCur_` ·
`std::size_t tickPhase_` · `OnePoleSmoother attackS_, sustainS_` · `float attackDb_, sustainDb_, durationMs_` (sanitised
targets) · the taps `lastGainDb_, lastC_, lastFast_, lastSlow_, lastA_, lastS_` · `double sampleRate_` · `bool prepared_`
· the probe-only switches `bool probeHoldBypass_ = false, probeGainSmootherBypass_ = false`. Only
`detail::TransientShaperProbe` (test header, S13.1) writes them. At default construction they leave the production path
untouched (the S5.1 / Phase 5 probe precedent, bass_character_stage.h:71, 513).

### S4.3 Per-sample contract

For n in [0, numSamples), with one channel when `right == left`:
1. **Sanitise (FR-007).** xL = isFinite(L[n]) ? L[n] : 0.0f, and the same for R. Every later step sees only xL and xR.
2. **Detection (FR-004, FR-005).** m = det_.process(xL, xR), or det_.process(xL) for mono. This is the held true-peak of
   max(|L|, |R|). The shaper has no high-pass, so FR-005's signed-signal clause is not engaged.
   **Probe branch** (`probeHoldBypass_`, SC-009's negative control): m = max(|xL|, |xR|), fed straight to the EFs; the
   detector is skipped.
3. **Fast follower.** f = fast_.processSample(m). The fast EF is in Amplitude mode, attack `kFastAttackMs`, release
   `kFastReleaseMs`.
4. **Slow follower (C-5).** If tickPhase_ == 0: slowPrev_ = slowCur_ and slowCur_ = slow_.processSample(m). The slow EF
   is in Amplitude mode, attack = interp(kDurationTable, durationMs_), release = kSlowReleaseMs / kSlowTickSamples.
   Then s = slowPrev_ + (slowCur_ − slowPrev_) · (tickPhase_ / 8.0f), and tickPhase_ = (tickPhase_ + 1) & 7.
5. **c (FR-008).** If s ≤ `kSilenceFloorLin` (1e-7, −140 dB): c = (f > 1e-7) ? 1 : 0. Otherwise
   c = clamp(gainToDb(f / s) / K, 0, 1). The log is taken of a ratio, so there is one log per sample.
6. **Gain.** A = attackS_.process() and S = sustainS_.process(), then gain_dB = A · c + S · (1 − c) and
   g = dbToGain(gain_dB). When A == 0.0f and S == 0.0f, gain_dB == 0.0f exactly, g == 1.0f exactly (db_utils.h:221), and
   the output is bit-identical to the input (SC-013). lastA_ = A and lastS_ = S.
   **Probe branch** (`probeGainSmootherBypass_`, SC-008(d)'s positive control): A = attackDb_ and S = sustainDb_, the
   raw targets; the smoothers are still advanced so that clearing the switch is continuous.
7. **Output.** L[n] = xL · g and R[n] = xR · g: one gain for both channels (FR-005, SC-012). The taps are written.

**Setters.**
- `setTransientDurationMs` interpolates the table:
  log(arg) = lerp(log arg_k, log arg_{k+1}, (log d − log d_k) / (log d_{k+1} − log d_k)).
  It then calls `slow_.setAttackTime(arg)`, which is one `constexprExp` per call. `MassDynamics` calls it at most once
  per interval.
- The gain setters set their smoother targets.
- Before `prepare`, every setter stores the sanitised value and `prepare` applies it (the HarmonicFold convention).

**`prepare(fs, ·)`.**
- `det_.prepare(fs, kHoldMs)`.
- `fast_.prepare(fs, 0)`, setMode(Amplitude), attack and release.
- `slow_.prepare(fs, 0)`, setMode(Amplitude), the table argument, and release kSlowReleaseMs / 8.
- `attackS_.configure(kGainSmoothMs, fs)`, `sustainS_` likewise.
- `reset()`.

**`reset()`** zeroes the detector, the EFs, slowPrev_, slowCur_ and tickPhase_, and snaps the smoothers to their
targets (FR-047).

**Lifecycle.** Before prepare, with a null channel, or with 0 samples, the buffers are left untouched (FR-047).

### S4.4 Why the law behaves as the spec needs

- **Steady state.** m is constant to the true-peak estimate's precision; HarmonicFold's measured window ripple was
  "4e-2 → 0" (harmonic_fold.h:394–396). Both EFs converge to m, so f / s → 1, c → 0 and the gain equals the sustain
  gain (SC-008(a)). Nothing rides individual cycles, because the EFs never see the waveform (SC-009).
- **Onset from silence.** s ≈ 0 while f > 0, so c = 1 and the full attack gain is applied (SC-008(b), (e)). The gain is
  a convex combination of A and S, so it can never exceed max(A, S), and SC-008(e)'s "no spike" holds by construction.
- **Decay.** The slow release (60 ms) is ≥ the fast release (40 ms), so on a falling programme f ≤ s and c = 0. No false
  "transient" boost appears on decays.
- **Duration.** For a step onset, t_3dB = 0.438 · τ_real with τ_real = 8 · arg / 2π, so arg = 2π · d / (0.438 · 8).
  The model's harvested arguments sit within 1–19 % of that closed form (S12.2). The table absorbs the waveform-limited
  rise.
- **Known limitation (recorded, not gated).** A legato note at the same level, or a re-attack within the 50 ms hold, is
  not a transient for a level-based shaper. Phase 9 records this in the audibility table.

---

## S5. `BassCompressor` (Layer 3, `systems/mass_dynamics.h`)

Includes: `core/db_utils.h`, `primitives/biquad.h`, `primitives/smoother.h`, `processors/dynamics_processor.h` and
`processors/period_peak_detector.h`.

### S5.1 Public API (FR-010–FR-019)

```cpp
namespace Krate::DSP {
namespace detail { struct MassDynamicsProbe; }
class BassCompressor {
public:
    static constexpr std::size_t kControlInterval = 32;                  // FR-049 (== ProfundumCore::kControlInterval: static_assert in mass_chain_test.cpp, S11)
    static constexpr float kMinCornerHz = 20.0f, kMaxCornerHz = 500.0f;  // FR-018 (== DynamicsProcessor::kMin/MaxSidechainHz)
    static constexpr float kFallbackCornerHz = 80.0f;                    // C-10
    static constexpr float kCornerSmoothMs = 20.0f;
    static constexpr float kAttackMs = 10.0f;
    static constexpr float kMinReleaseMs = 50.0f, kMaxReleaseMs = 500.0f;          // D-5
    static constexpr float kRuleWindowMs = 300.0f, kRuleThresholdDb = 1.0f;       // D-5
    static constexpr std::size_t kMaxRuleIntervals = 1800;              // 300 ms / 32 samples at 192 kHz
    static constexpr float kMakeupSmoothMs = 5.0f;
    struct AmountRow { float amount; float thresholdDb; float slope; float kneeDb; float makeupDb; };   // slope = 1 − 1/ratio
    static constexpr std::array<AmountRow, 5> kAmountTable{{   // SEED (S8.3); T020 harvests thresholds and makeup
        {.amount = 0.00f, .thresholdDb = 0.0f,   .slope = 0.00f, .kneeDb = 0.0f, .makeupDb = 0.0f},
        {.amount = 0.25f, .thresholdDb = -12.0f, .slope = 0.25f, .kneeDb = 6.0f, .makeupDb = 0.0f},
        {.amount = 0.50f, .thresholdDb = -16.0f, .slope = 0.40f, .kneeDb = 6.0f, .makeupDb = 0.0f},
        {.amount = 0.75f, .thresholdDb = -19.0f, .slope = 0.55f, .kneeDb = 6.0f, .makeupDb = 0.0f},
        {.amount = 1.00f, .thresholdDb = -22.0f, .slope = 0.67f, .kneeDb = 6.0f, .makeupDb = 0.0f}}};

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;   // calls DynamicsProcessor::prepare (lookahead 0: no allocation)
    void reset() noexcept;
    void setAmount(float amount01) noexcept;        // FR-016; non-finite keeps previous; clamp; latched at the next interval
    void setDetectorKeyHz(float f0Hz) noexcept;     // FR-018; f0 <= 0 or non-finite = "no voice"; latched at the next interval
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept;   // in place
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }  // FR-014
    // ---- taps ----
    [[nodiscard]] float currentGainDb() const noexcept;          // applied gain of the last sample (GR + makeup)
    [[nodiscard]] float currentGainReductionDb() const noexcept; // DynamicsProcessor::getCurrentGainReduction()
    [[nodiscard]] float currentReleaseMs() const noexcept;       // the D-5 rule output in effect
    [[nodiscard]] float programFraction() const noexcept;        // f in [0, 1]
    [[nodiscard]] float detectorCornerHz() const noexcept;       // smoothed corner in effect
    [[nodiscard]] float detectorLevel() const noexcept;          // held detection value fed to the DynamicsProcessor
    [[nodiscard]] float amount() const noexcept;
    [[nodiscard]] bool stateFinite() const noexcept;
private:
    friend struct detail::MassDynamicsProbe;
    void controlStep() noexcept;   // S5.4
    DynamicsProcessor dyn_;
    std::array<Biquad, 2> hp_{};
    PeriodPeakDetector det_;
    OnePoleSmoother cornerLog2S_, makeupS_;
    std::array<std::uint8_t, kMaxRuleIntervals> ruleRing_{};
    std::uint32_t ruleSum_ = 0, ruleCount_ = 0;
    std::size_t rulePos_ = 0, ruleIntervals_ = 450, intervalPhase_ = 0;
    float pendingAmount_ = 0.0f, amount_ = 0.0f, pendingKeyHz_ = 0.0f, cornerHz_ = kFallbackCornerHz;
    float cornerTargetLog2_ = 6.32192809f, makeupTargetDb_ = 0.0f;     // latched targets: log2(kFallbackCornerHz), row makeup
    float releaseMs_ = kMinReleaseMs, gainDb_ = 0.0f;
    double sampleRate_ = 48000.0;
    bool prepared_ = false;
    // probe-only switches (positive / negative controls): fixed release, detector HP bypass, rectified-feed mode
    float probeFixedReleaseMs_ = 0.0f; bool probeHpBypass_ = false; bool probeRectifiedFeed_ = false;
};
}
```

The probe switches are private fields that only `detail::MassDynamicsProbe` (test header) writes. At default
construction they leave the production path untouched. This is the Phase 5 probe precedent
(bass_character_stage.h:71, 513).

### S5.2 `prepare(fs, maxBlock)`

1. `dyn_.prepare(fs, maxBlock)`, which is allocation-free because the lookahead stays 0 (dynamics_processor.h:429–437).
   Then `setDetectionMode(DynamicsDetectionMode::Peak)`, `setSidechainEnabled(false)`, `setLookahead(0.0f)` (never
   non-zero, FR-014), `setAttackTime(kAttackMs)`, `setReleaseTime(kMinReleaseMs)`, `setMakeupGain(0.0f)` (makeup is
   applied by the composition, FR-016), and `setAutoMakeup(false)`.
2. `hp_[c].configure(FilterType::Highpass, kFallbackCornerHz, kButterworthQ, 0.0f, fs)`.
3. `det_.prepare(fs, 2000 / kFallbackCornerHz)`, i.e. 25 ms.
4. `cornerLog2S_.configure(kCornerSmoothMs, static_cast<float>(fs / 32.0))` and
   `makeupS_.configure(kMakeupSmoothMs, static_cast<float>(fs))`. `OnePoleSmoother::configure` takes a float rate
   (smoother.h:160); passing the double unconverted raises C4244.
5. ruleIntervals_ = clamp(round(0.3 · fs / 32), 1, kMaxRuleIntervals): 413 at 44.1 kHz, 450 at 48 kHz, 900 at 96 kHz,
   1800 at 192 kHz.
6. Apply the pending amount and key, then `reset()`.

The sample rate is clamped to [22050, 192000], as `AnchorOscillator` does (kMinSampleRate / kMaxSampleRate,
anchor_oscillator.h:66–67). A `static_assert` checks that kMaxRuleIntervals ≥ round(0.3 · 192000 / 32).

**`reset()`** (FR-047; allocation-free, SC-016 covers it). In order:
1. `dyn_.reset()`, which clears the envelope, the sidechain biquad, the GR smoother, the delay line and
   `currentGainReduction_` (dynamics_processor.h:150–156). Then releaseMs_ = kMinReleaseMs (or the probe's fixed value)
   and `dyn_.setReleaseTime(releaseMs_)`, because f = 0 once the ring is cleared.
2. `hp_[0].reset()` and `hp_[1].reset()` (biquad.h:385), configured at the latched corner exp2(cornerTargetLog2_).
3. `det_.reset()`, then `det_.setHoldMs(2000 / corner)` for that corner.
4. Release rule: `ruleRing_.fill(0)`, ruleSum_ = 0, ruleCount_ = 0, rulePos_ = 0. Zeroing rulePos_ matters across a
   re-prepare. ruleIntervals_ changes with the rate (413 / 450 / 900 / 1800), and a rulePos_ left ≥ the new
   ruleIntervals_ would leave stale ring entries inside the 300 ms window.
5. intervalPhase_ = 0.
6. `cornerLog2S_.snapTo(cornerTargetLog2_)` and cornerHz_ = exp2(cornerTargetLog2_); `makeupS_.snapTo(makeupTargetDb_)`.
   Both targets are the latched ones (S5.4 step 1).
7. gainDb_ = 0.0f.

**Lifecycle (FR-047).** Before `prepare` (`prepared_ == false`), with a null `left` or `right`, or with
`numSamples == 0`, `processBlock` returns at once: the buffers are untouched and no state (intervalPhase_, smoothers,
ring) advances. Before `prepare` the setters store their sanitised pending value, and `prepare` applies it (step 6).

### S5.3 The amount mapping (FR-016)

- amount → the bracketing rows i and i + 1, with t = (amount − a_i) / (a_{i+1} − a_i).
- threshold, slope, knee and makeup are interpolated linearly in t, and ratio = 1 / (1 − slope).
- Monotone: threshold is non-increasing in the rows, slope, knee and makeup are non-decreasing, and all four are
  `static_assert`-ed.
- Amount 0 maps to exactly ratio 1.0f, so `computeGainReduction` returns 0 (dynamics_processor.h:369–371), and to
  makeup 0.0f.
- Applied in `controlStep` via `dyn_.setThreshold`, `setRatio`, `setKneeWidth`, and `makeupS_.setTarget`. Each is called
  only when its value changed bitwise.

### S5.4 `controlStep()`, at intervalPhase_ == 0 (FR-012, FR-018, FR-049)

1. **Latch.** amount_ = pendingAmount_, then the S5.3 mapping. Corner target:
   - "no voice" (pendingKeyHz_ ≤ 0 or non-finite): log₂(kFallbackCornerHz);
   - otherwise: log₂(clamp(2 · pendingKeyHz_, 20, 500)).
2. **Corner.** c₂ = cornerLog2S_.process(); corner = exp2(c₂). If the corner changed bitwise: reconfigure both `hp_`
   (two trig evaluations), and `det_.setHoldMs(2000 / corner)`. The hold is one period of the key f₀ when unclamped
   (C-6).
3. **Release rule (D-5).**
   - ruleSum_ += ruleCount_ − ruleRing_[rulePos_]; ruleRing_[rulePos_] = ruleCount_;
     rulePos_ = (rulePos_ + 1) % ruleIntervals_; ruleCount_ = 0.
   - f = ruleSum_ / (ruleIntervals_ · 32), from integer counts, so it is exact and deterministic.
   - releaseMs_ = 50 + 450 · f (or the probe's fixed value). If it changed bitwise: `dyn_.setReleaseTime(releaseMs_)`.
     That is one exp per interval, inside SC-017.

### S5.5 Per-sample contract

1. **Sanitise (FR-017).** A non-finite sample becomes 0.0f, in the audio path and the detection path. dyn_'s own
   NaN / Inf branch is therefore never reached.
2. If intervalPhase_ == 0: `controlStep()`.
3. **Detection (FR-011 (b)).** hL = hp_[0].process(xL) and hR = hp_[1].process(xR), on signed samples. The probe HP
   bypass uses xL and xR instead.
   - held = det_.process(hL, hR), the held true-peak of max(|HP L|, |HP R|).
   - The probe's rectified-feed negative control (SC-006(e)) instead feeds max(|xL|, |xR|) into `dyn_` with
     `setSidechainEnabled(true)` at the corner. That is exactly the struck form, kept only to prove the fixture has
     teeth.
4. **Gain computer.** (void)dyn_.processSample(held), and gr = dyn_.getCurrentGainReduction() (≤ 0, already 5 ms
   smoothed, dynamics_processor.h:190–196). If gr < −kRuleThresholdDb, then ++ruleCount_.
5. **Gain.** mk = makeupS_.process() and gainDb_ = gr + mk.
   - If gainDb_ == 0.0f: output = input (the hard bypass, FR-016 / SC-013; gr is exactly +0.0f there because
     `flushDenormal(−0.0f)` returns the literal 0.0f, db_utils.h:263–265).
   - Otherwise g = dbToGain(gainDb_), and L = xL · g, R = xR · g. One gain for both channels (FR-013).
6. intervalPhase_ = (intervalPhase_ + 1) % 32.

Mono (`right == left`): the detector is single-channel and only L is written.

**Cost note.** `DynamicsProcessor::processSample` computes and discards its own output, which costs two `dbToGain`
calls (dynamics_processor.h:206–211). That waste cannot be removed without a header change (FR-044). It is part of the
SC-017 measurement, and T001 measures it first (R-1).

---

## S6. `MassDynamics` (Layer 3, `systems/mass_dynamics.h`)

```cpp
class MassDynamics {
public:
    static constexpr std::size_t kControlInterval = 32;
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;
    // global Mass targets (FR-020): latched at the next interval
    void setTransientDurationMs(float ms) noexcept;
    void setCompressionAmount(float amount01) noexcept;           // -> BassCompressor::setAmount
    void setDetectorKeyHz(float f0Hz) noexcept;                   // -> BassCompressor::setDetectorKeyHz (FR-018 handoff)
    // user-facing shaper gains (FR-009; Phase 11 registers them in 700-799): immediate, smoothed in the shaper
    void setTransientAttackDb(float dB) noexcept;
    void setTransientSustainDb(float dB) noexcept;
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept;
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }
    [[nodiscard]] const TransientShaper& shaper() const noexcept;
    [[nodiscard]] const BassCompressor& compressor() const noexcept;
    [[nodiscard]] bool stateFinite() const noexcept;
private:
    TransientShaper shaper_;
    BassCompressor comp_;
    float pendingDurationMs_ = TransientShaper::kDefaultDurationMs;
    std::size_t intervalPhase_ = 0;
    bool prepared_ = false;
};
```

`processBlock` splits the call into runs that end on its 32-sample grid. At each run start with intervalPhase_ == 0 it
latches `shaper_.setTransientDurationMs(pendingDurationMs_)`, then runs `shaper_.processBlock(run)` and
`comp_.processBlock(run)`. Both are per-sample and the compressor reads only the shaper's output, so run-wise order
equals sample-wise order. The compressor's own grid is aligned with this one by construction (C-11). The order is fixed,
shaper → compressor (FR-015).

**`prepare(fs, maxBlock)`** prepares `shaper_` and `comp_` at fs, calls `reset()`, then sets `prepared_`.
**`reset()`** (FR-047; allocation-free): `shaper_.setTransientDurationMs(pendingDurationMs_)`, `shaper_.reset()`,
`comp_.reset()`, intervalPhase_ = 0.
**Lifecycle.** Before `prepare`, with a null channel pointer, or with 0 samples, `processBlock` is a no-op: the buffers
are untouched and no phase advances. Setters before `prepare` store the sanitised pending value.

---

## S7. `MassLaw` (Layer 3, `systems/mass_law.h`)

Includes: `core/db_utils.h` (isFinite), `core/modulation_curves.h` (applyModCurve, ModCurve), `primitives/smoother.h`
(SlewLimiter) and `processors/transient_shaper.h` (duration range constants only).

### S7.1 Public API (FR-020–FR-029)

```cpp
namespace Krate::DSP {
class MassLaw {
public:
    enum class Target : std::uint8_t { Depth, Body, TransientDuration, FoldAmount, CompressionAmount, AnchorLevel, OutputTrim };   // append-only; OutputTrim = the FR-021 trim (G8-5, 2026-10-17)
    static constexpr std::size_t kNumTargets = 7;                     // FR-021 as amended: six + the calibrated output trim
    static constexpr float kMinMass = 0.0f, kMaxMass = 100.0f, kNeutralMass = 50.0f;
    static constexpr std::size_t kControlInterval = 32;               // FR-049
    static constexpr float kMassSweepMs = 40.0f;                      // C-7 (D-7: >= 20 ms)
    static constexpr float kSlewBoundRefMs = kMassSweepMs;            // C-7 default (ii): one T in both SC-004 clauses; ruling (i) sets 20.0f
    /// The six targets, in component units. Doubles as the base (the user's own values = Mass 50, D-1).
    struct Targets {
        float depth = 0.5f;               // SpectralShapeRecipe::Controls::depth [0, 1]
        float body = 0.5f;                // ::body [0, 1]
        float transientDurationMs = TransientShaper::kDefaultDurationMs;   // [5, 250]
        float foldAmount = 0.0f;          // HarmonicFold::setAmount [0, 100]
        float compressionAmount = 0.0f;   // BassCompressor::setAmount [0, 1]
        float anchorLevel = 0.5f;         // AnchorOscillator::Controls::level [0, 1] (== kDefaultLevel, test static_assert)
        float outputTrimDb = 0.0f;        // MassDynamics::setOutputTrimDb [-12, 12] dB (FR-021 output trim, G8-5)
    };
    struct Row { Target target; float lo; float hi; float amountDown; float amountUp; ModCurve curve; };
    static constexpr std::array<Row, kNumTargets> kRows{{ /* S8.2 seed; T030 harvests */ }};

    // ---- pure (FR-026): same inputs -> bit-identical outputs; no state ----
    [[nodiscard]] static float evaluateRow(const Row& row, float mass, float base) noexcept;
    [[nodiscard]] static Targets evaluate(float mass, const Targets& base) noexcept;
    // ---- stateful part (FR-025, FR-029) ----
    void prepare(double sampleRate) noexcept;   // SlewLimiter at the interval rate: configure(100.0f / kMassSweepMs, static_cast<float>(fs / 32.0))
    void reset() noexcept;                      // snapTo(pending mass)
    void setMass(float mass) noexcept;          // non-finite keeps previous; clamp [0, 100]
    void setBase(const Targets& base) noexcept; // per field: non-finite keeps previous; clamp to the row's [lo, hi]
    [[nodiscard]] float advanceInterval() noexcept;   // one SlewLimiter step; returns the smoothed Mass
    [[nodiscard]] Targets currentTargets() const noexcept;   // evaluate(smoothedMass(), base())
    [[nodiscard]] float smoothedMass() const noexcept;
    [[nodiscard]] float pendingMass() const noexcept;
    [[nodiscard]] const Targets& base() const noexcept;
private:
    SlewLimiter slew_;
    float pendingMass_ = kNeutralMass;
    Targets base_{};
};
}
```

### S7.2 Evaluation

`evaluateRow`:
- If mass is non-finite it becomes kNeutralMass, and a non-finite base becomes the `Targets{}` default for that field.
  The pure function has no "previous"; the keep-previous rule of FR-029 lives in `setMass` / `setBase`.
- mass is clamped to [0, 100], and u = (mass − 50) · 0.02f.
- s = applyModCurve(curve, |u|).
- The result is `std::clamp(base + (u >= 0 ? amountUp * s : -amountDown * s), lo, hi)`.

`evaluate` applies the six rows to the six fields in `Target` order. There is no neutral fast path, matching Vorago's
`evaluateAll` (vorago_macro_matrix.h:62), so the arithmetic is the same at every Mass.

**Compile-time table checks** (`static_assert` over `kRows`, the VoragoMacroMatrix style):
- exactly one row per target, in enum order;
- lo < hi;
- amountUp · amountDown ≥ 0;
- the sign of amountUp is the D-2 direction: Depth ≤ 0, the other five ≥ 0;
- curve ≠ Stepped;
- max(|aUp|, |aDown|) · maxSlope(curve) ≤ (hi − lo) · kMassSweepMs / (2 · kSlewBoundRefMs) (C-7);
- the duration row's [lo, hi] == TransientShaper's range.

**Slew.** `SlewLimiter::configure(100.0f / kMassSweepMs, static_cast<float>(fs / 32.0))`; the overload takes a float rate
(smoother.h:512), so the double is cast explicitly (C4244). The rate per step is 2.5 · 32000 / fs Mass units: 1.667
at 48 kHz, so a full sweep is 60 steps = 40 ms. The step is ≤ 100 / (40 ms · fs/32) Mass units per interval at every
rate, which bounds every target's per-interval change at
max(|a|) · maxSlope · 2 · (32/fs) / 40 ms ≤ range · (32/fs) / kSlewBoundRefMs (C-7: 40 ms under the default (ii), 20 ms
under (i)).

---

## S8. Calibration (FR-008, FR-016, FR-022, SC-022) — seeds, procedure, evidence

The procedure of the Phase 5 precedent applies. Each table is harvested by a `[.calibration]` TEST_CASE inside the
consuming test binary (never a standalone dump: the root rule "goldens harvested inside the consuming test binary").
The output is printed with `std::setprecision(9)` to `specs/profundum-phase6-mass-dynamics/evidence/<table>.txt`, pasted
into the header, and cited in the header comment with the evidence file and date.

### S8.1 Shaper duration table (T010, `TransientShaper_DurationTableHarvest`, processors exe)

- **Durations:** {5, 7.5, 10, 15, 20, 30, 40, 60, 100, 150, 250} ms. Each argument is bisected (geometric, 40 steps) so
  that the mean log error over the four SC-008 cells (sine and `kSawAnchor` at E1 and C3, 48 kHz) is 0.
- **Records:** per-cell errors. The E1-sine cells at 5 and 7.5 ms are recorded (C-2, extended to 7.5 ms by user ruling 2026-10-18,
  G9-2); any other cell beyond ±15 % is a `STOP AND SURFACE` line, never a retune.
- **Seeds** are in S4.1, from S12.2.

### S8.2 Mass law rows (T030, `MassLaw_TableHarvest`, systems exe) — seed

| Target | lo | hi | amountDown | amountUp | curve | At Mass 0 / 100 on the reference base |
|---|---|---|---|---|---|---|
| Depth | 0 | 1 | −0.5 | −0.5 | Linear | 1.0 / 0.0 |
| Body | 0 | 1 | +0.5 | +0.4 | Linear | 0.0 / 0.9 |
| TransientDuration (ms) | 5 | 250 | +30 | +110 | Linear | 10 / 150 |
| FoldAmount | 0 | 100 | +50 | +50 | Linear | 0 (clamped) / 50 (C-7 (ii) cap; +60 / 60 under (i)) |
| CompressionAmount | 0 | 1 | +0.5 | +0.5 | Linear | 0 (clamped) / 0.5 (C-7 (ii) cap; +1 / 1 under (i)) |

Every seed row obeys the C-7 `static_assert` under the default (ii): max(|aDown|, |aUp|) ≤ (hi − lo) / 2 for Linear.
| AnchorLevel | 0 | 1 | +0.5 | +0.5 | Linear | 0 / 1 |
| OutputTrim (dB) | −12 | +12 | harvested (≤ 0) | harvested (≤ 0) | Linear | +d / 0 (G8-5, 2026-10-17: d = the reference voice's K-weighted rise from Mass 0 to 50 at the gated notes; the Mass 50 → 100 rise, if any, is cancelled the same way) |

The harvest adjusts the amounts, never the directions (D-2), in this order (**Group 8 ruling, user 2026-10-17, R-6**: the
Anchor's level is relative to the core's h1, anchor_oscillator.h:340, so the seed's Depth travel to 0 removed about 20 dB of h1
that an Anchor adding at most (1 + level) could not carry; measured on 2026-10-17 the sum's w₁ fell 21 dB from Mass 30 to 100):
0. The Anchor travel (amountDown = amountUp = a, the largest in [0, 0.5]) whose full-lattice unweighted span(L) stays ≤ 6 dB on
   every Ref / Saw cell (SC-003). The pure-sine cells are recorded (G8-2 ruling 2026-10-17: the sidechain high-pass at 2·f₀ cannot see a
   sub-only voice at any note, so the FR-016 makeup alone spans ≈ 5.7 dB there).
1. The Depth travel, direction unchanged (falling): the largest |amountUp| in [0, 0.5] keeping the sum's w₁ (at the fold + anchor sum
   tap, `sumL`; G8-2 ruling 2026-10-17) within 5.5 dB of its Mass 0 value (the 6 dB bar less the harvest margin; G8-4 ruling 2026-10-17) over Mass 50..100, and the largest |amountDown| doing so over 0..50, the lower half first because Mass 0 depends on it (SC-001(c)), given the Anchor travel of step 0 and
   the current Fold.
2. Fold amountUp and Body for SC-002's +6 dB THD steps (smallest amounts that meet them).
3. Steps 0–2 repeat until the table is stable (at most kMlsMaxRounds rounds); then the FR-021 output trim (G8-5 ruling 2026-10-17):
   one Linear row, 0 dB at Mass 50, amountDown = −max(0, mean P_ref(50) − P_ref(0)) and amountUp = −max(0, mean P_ref(100) − P_ref(50))
   over the gated notes (K-weighted; only rises are cancelled, FR-024; the trim sits after the dynamics so SC-001(c) and SC-002 do not see
   it); then SC-003 against the spec bar on all four notes:
   K-weighted span ≤ 3 dB and |ΔP| ≤ 0.2 dB per unit at E2 and C3 (C-1 (a)), the unweighted span ≤ 6 dB everywhere; the best
   E0 / E1 K-weighted span reached is recorded as the FR-048 measurement.

The curves stay Linear unless SC-003's |ΔP| ≤ 0.2 dB per unit needs a bend; that check runs at the gate's resolution, every Mass
unit on the gated Ref / Saw × E2 / C3 cells (main-loop refinement 2026-10-18, M-1: on the 11-point lattice a 1.0 dB rise over Mass 50 → 58
read 0.10 dB per unit and the bend step never triggered, while the 101-point shard read 0.2178). Harvested 2026-10-18: **Fold SCurve**
adopted (Ref C3 0.1303 dB per unit; the other rows unchanged). If no table meets SC-001(c) and SC-003
together, that goes to the user as a FR-021 ruling (R-6). Nothing is added silently. Under this order Mass 100's richness comes
from Fold, Body and compression; the Depth travel is whatever the Anchor can carry.

### S8.3 Compression amount table (T020, `MassCompressor_AmountTableHarvest`, systems exe)

- The seed is in S5.1.
- **One calibration voice for thresholds and makeup.** The Mass reference voice at Mass 50 (every target at its base),
  with the compression amount set by `targetOverride[CompressionAmount]` (S9). That is the voice SC-006 and SC-022 gate.
  The corner is keytracked at the note under test. Steady GR is the mean over [1 s, 3 s] of a 3 s render.
- **Harvest targets**, one per gated cell:
  1. **SC-006(d):** steady GR ≥ 4 dB (the 3 dB floor plus 1 dB of margin) at amount 1 on the calibration voice at
     **E0, E1, E2 and C3**. The lowest-reading note sets the amount-1 threshold. A design ceiling of ≤ 8 dB at E1 / C3
     is recorded, not gated.
  2. **SC-002:** steady GR(100) ≥ 3.5 dB (3 dB plus 0.5 dB of margin) on the Mass 100 voice at **E1 and C3**. That voice
     takes all six targets from the law at Mass 100, so its compression amount is the law's Mass 100 value: 0.5 under
     C-7 (ii), 1 under (i). If target 1 alone does not meet it, the threshold of the row the law reaches at Mass 100 is
     lowered further, keeping the rows monotone.
  3. **SC-022:** |makeup − GR| ≤ 0.5 dB on the calibration voice at **E1 and C3**, for every row and for amount ∈ {0,
     0.25, 0.5, 0.75, 1}. makeupDb := the mean of the E1 and C3 GR, so the E1 / C3 spread must be ≤ 1 dB (harvest target
     ≤ 0.8 dB). The keytracked corner and hold make detection scale-invariant to first order.
- **Intermediate rows.** Thresholds are interpolated so that the steady GR is monotone in amount.
- **Records:** `evidence/amount-table.txt` holds, for every row, the steady GR on the calibration voice at E0, E1, E2
  and C3, the Mass 100 GR at E1 and C3, and the makeup residual per row and note. A target the harvest cannot meet is
  reported under FR-048. It is never met by moving its bar.

### S8.4 Evidence files

`evidence/duration-table.txt`, `evidence/amount-table.txt`, `evidence/mass-law-table.txt`, `evidence/model-2026-10-17.txt`
(the S12 output), and `evidence/phase9-handoff.txt` (FR-035).

---

## S9. Validation-chain fixture (FR-031–FR-034; test code, `dsp/tests/unit/systems/mass_dynamics_test_helpers.h`)

Namespace `Krate::DSP::MassDynamicsTest`. It includes `shape_character_test_helpers.h` (descriptors, notes),
`profundum_core_test_helpers.h`, `k_weighting.h` and `mass_dynamics_probe.h`.

```cpp
/// Descriptor renders must hold describe()'s full frame: REQUIRE(n >= kLowFrequencyFftSize)
/// (shape_character_test_helpers.h:111-113). seconds <= 0 selects descriptorSeconds(fs).
inline constexpr double kSettleSeconds = 1.0;
inline double descriptorSeconds(double fs) {
    return kSettleSeconds + static_cast<double>(TestUtils::kLowFrequencyFftSize) / fs;   // 6.46 s at 48 kHz
}
using TargetOverride = std::function<std::optional<float>(std::size_t sample)>;   // empty or nullopt = law value
struct ChainConfig { SpectralShapeRecipe::Controls base; double f0; double fs = 48000.0; double seconds = 0.0;
                     std::size_t hostBlock = 512; bool attachAnchor = true; bool massLawDetached = false;
                     std::function<float(std::size_t sample)> massAt;                       // Mass schedule (absolute sample)
                     std::array<TargetOverride, MassLaw::kNumTargets> targetOverride{}; };   // indexed by MassLaw::Target
struct ChainTaps { std::vector<float> coreL, coreR, foldL, foldR, anchorL, anchorR, sumL, sumR, dynL, dynR, outL, outR;
                   std::vector<float> a1PerInterval, anchorLevelPerInterval, shaperGainDb, compGainDb;
                   std::vector<AnchorOscillator::Controls> anchorControlsPerInterval; };   // pendingControls() read-back
ChainTaps renderChain(const ChainConfig&);
```

`renderChain`:
- Owns `ProfundumCore`, `AnchorOscillator` (Sine, LOCK), `HarmonicFold`, `MassLaw`, `MassDynamics`, two `DCBlocker`s and
  a `TruePeakLimiter`, each prepared at fs.
- Issues a Reset noteOn(f₀) at sample 0.
- Walks the host blocks, each split at the absolute 32-sample grid. At each grid point:
  1. `law.setMass(massAt(n))`, then `law.advanceInterval()` and t = `law.currentTargets()`. Then, for each target k
     whose `targetOverride[k]` is set and returns a value at n, field k of t is replaced by that value. The override
     replaces the law's output for that target only; the others still come from the law. Tests pass in-range values,
     and the components clamp in any case (FR-029).
  2. `core.setControls({base with depth = t.depth, body = t.body})`, `fold.setAmount(t.foldAmount)` and
     `anchor.setControls({…, level = t.anchorLevel})`. Then `anchor.pendingControls()` (anchor_oscillator.h:361) is
     read back into `anchorControlsPerInterval` (SC-014(a)).
  3. `md.setTransientDurationMs(t.transientDurationMs)`, `md.setCompressionAmount(t.compressionAmount)` and
     `md.setDetectorKeyHz(f₀)`.
- On each run: the core block; then a1 = `SpectralShapeRecipe::evaluate(base, f0, fs)[0] · kCenterPanGain` (the voice's own
  controls before the Mass biases; FR-031 as amended by the G8-3 ruling 2026-10-17, replacing `deliveredGains()[0]`) and
  `anchor.setCoreH1Amplitude(a1)`
  (FR-031; once per 32-sample core block, the Phase 3 order, profundum_anchor_chain_test.cpp:577–582); the Anchor
  block; `fold.processBlock(core)`; sum = fold + anchor as a plain float add (FR-032); `md.processBlock(sum)`; the DC
  blockers; `limiter.processBlock(…, int)`.
- Every stage's output is copied to the taps.
- `massLawDetached` writes `law.base()` directly instead of the evaluated targets (SC-023).
- Render length: `seconds` as given, or `descriptorSeconds(fs)` when it is ≤ 0 (6.46 s at 48 kHz, 6.94 s at 44.1 kHz,
  3.73 s at 96 kHz, 2.37 s at 192 kHz). Every row that calls `describe`, `renderedHarmonicPowers` or `kWeightedLevelDb`
  on the last `kLowFrequencyFftSize` samples uses the default. Rows that read only time-domain windows state their own
  length.

**Other helpers:**
- `subBand(...)`: s(t), the sliding single-bin DFT over one period with hop ≤ period/8, plus the C-10 low-pass reading.
- `mSub(...)`.
- `transientDuration(...)` and `dTr(...)`: SC-008(c) and D_tr.
- `deltaLStep(...)`: ΔL_step on the −24 → −12 dBFS step fixture.
- `burst(kind, f0, fs, ...)`: the burst fixture with a 0.5 ms raised cosine at a zero crossing and the 2 s pre-roll arm.
- `aliasResidual(...)`: the C-3 moving-window method.
- `gridDriver(...)`: splits host blocks at the 32 grid.
- `lowestSoundingF0(std::span<const float> f0s)`: the engine-side FR-018 selection rule, the minimum of the positive
  finite entries, 0 ("no voice") when there are none. SC-024(d) drives it with a truth table and a POLY render. The
  FR-035 handoff record names it as the rule Phase 9's engine adopts.

The `[long]` grids shard as S14.2 lists.

---

## S10. Algorithm choices — why

| Choice | Rejected alternative | Reason |
|---|---|---|
| Held true-peak (`PeriodPeakDetector`) ahead of both detectors | EF directly on \|x\| (the "fast follower rides cycles" defect FR-004 names) | The model gives 3.20 dB of GR ripple at E0 without a hold, and 0.002 dB with one (S12.3) |
| True-peak estimate in the hold | Plain sample maximum | HarmonicFold D-4: the plain maximum steps and reads as −74.9 dBFS aliasing (harmonic_fold.h:388–396) |
| Sliding RMS over a fixed window | — | A window that holds a non-integer number of E0 periods ripples, and a fixed window cannot track the period |
| `HilbertTransform` envelope | — | Accurate only from about 40 Hz at 44.1 kHz, and the band scales with the rate (hilbert_transform.h:123–125) |
| `SidechainFilter` | — | It is a dynamic SVF that allocates in prepare; the spec's correction (spec "Reuse-inventory corrections") |
| `TransientDetector` | — | A `ModulationSource`, not a gain stage, and an ODR-taken name |
| Slow EF ticked /8 | Cascaded EFs | A 3-pole cascade still needs a 744 ms attack for 250 ms, and it changes the onset law |
| Slow EF ticked /8 | A hand-written one-pole | FR-002 names the `EnvelopeFollower` pair |
| One log per sample: gainToDb(f / s) | gainToDb(f) − gainToDb(s) | The same value with one series log instead of two (db_utils.h:160–209) |
| Compressor gain from `getCurrentGainReduction()` | dyn_'s audio output | FR-013: one gain on both channels from one shared instance (D-6) |
| Release rule from integer per-sample counts in a per-interval ring | A float leaky average | Exact, deterministic (SC-018), and literally "the fraction of the last 300 ms" (D-5) |
| Corner smoothed in log₂ | Linear Hz | A Drop is an exponential pitch move, and the corner should move uniformly in octaves |
| Mass law as data rows + `applyModCurve` | Per-target code | FR-022: the table is the record of the tuning (Vorago pattern) |

---

## S11. RT safety, determinism, portability, denormals

- **RT (FR-040).**
  - No heap in any new class: fixed arrays only, and `PeriodPeakDetector` is fixed-size.
  - `DynamicsProcessor::prepare` stays allocation-free because the lookahead is never non-zero.
  - Every setter, `reset`, `processBlock`, `MassLaw::advanceInterval`, `evaluate` and the release rule are
    allocation-free and noexcept. SC-016 runs them inside `AllocationScope`.
  - `TruePeakLimiter::prepare` and `ProfundumCore::prepare` are fixture-prepare only.
- **Non-finite (FR-007, FR-017, FR-029).**
  - Every sample and every setter argument goes through `detail::isFinite` (db_utils.h:118). It survives
    `-ffast-math`; no `std::isnan`.
  - `std::clamp` is only ever applied to finite values (`EnvelopeFollower` / `DynamicsProcessor` clamp without a NaN
    guard, envelope_follower.h:221, dynamics_processor.h:244).
  - `OnePoleSmoother::setTarget` and `SlewLimiter::setTarget` zero their state on NaN (smoother.h:170–181, 519–529), so
    they are never handed a non-finite value.
- **Determinism (SC-018).** No random source. All control state lives on the 32 grid, with the phase tracked across
  calls. The release-rule counts are integers. The 8704-sample common boundary holds for the mid-render calls.
- **Denormals.**
  - FTZ / DAZ is the precondition (the dsp test main sets it).
  - The EFs flush (envelope_follower.h:184–185), smoothers flush (smoother.h:208), and `DCBlocker` flushes y1
    (dc_blocker.h:214).
  - The `PeriodPeakDetector` ring holds maxima ≥ 0, which decay only by replacement. Its oversampler is unflushed by
    design (polyphase_halfband_oversampler.h:24).
  - The detector high-pass `Biquad`s flush `z1_` / `z2_` on every sample (biquad.h:365–366, inside `process()`). Only
    the `PolyphaseHalfbandOversampler` sections are unflushed (polyphase_halfband_oversampler.h:24), and they rely on
    the FTZ / DAZ precondition.
- **Portability.**
  - Designated initialisers in every aggregate (`AmountRow`, `DurationPoint`, `Row`, `Targets`).
  - No narrowing in braces: every float literal has the `f` suffix.
  - No SIMD, so no aligned loads.
  - `std::exp2` and `std::log2` are used only in `controlStep` (per interval). `<cmath>` is included explicitly.
  - `constexpr std::array` tables are defined in-class with an aggregate of PODs. Phase 1's R-7 issue was nested
    aggregates with NSDMIs inside the class (spectral_shape_recipe.h:80–81); `Row` and `AmountRow` have no NSDMIs.
    `Targets` has NSDMIs but is not used in an in-class constant.
  - `node tools/check-portability.js` and `node tools/lint-apple-globals.js` run before commit (FR-046). No anonymous
    namespace in any header, and no namespace-scope `Rect` / `MAC` / MacTypes names (memory: macOS-only collisions).
- **Layer discipline.** `period_peak_detector.h` and `transient_shaper.h` include L0, L1 and peer L2 only.
  `mass_dynamics.h` and `mass_law.h` include L0–L2 only; no L3 peer is needed (the Anchor's level range is the literal
  [0, 1], asserted in a test against `AnchorOscillator::kDefaultLevel` and its clamp). The
  `BassCompressor::kControlInterval == ProfundumCore::kControlInterval`, `MassDynamics::kControlInterval == …` and
  `MassLaw::kControlInterval == …` `static_assert`s live in `mass_chain_test.cpp`, which already includes both sides.
  Neither new L3 header includes `systems/profundum_core.h`, which would pull the additive core
  (`harmonic_oscillator_bank.h`, `spectral_shape_recipe.h`, profundum_core.h:19–26) into every `MassDynamics` consumer.

---

## S12. Model evidence (`mass-dynamics-model.js`, run 2026-10-17)

### S12.1 K-weighting (`node … kweight`, 1.4 s)

The 997 Hz full-scale sine reads **−3.010 LKFS** (BS.1770 reference −3.01; the helper's validation, SC-026). The
offsets are in the C-1 table.

### S12.2 TransientShaper onset (`node … shaper`, 11.2 s)

Settings: attack +12 dB, sustain 0 dB, hold 50 ms, fast 0.5 / 40 ms, slow ticked /8 with release 60 ms, K = 12. Peak
gain was 12.00 dB in every cell, which is SC-008(b) / (e) feasible.

| d (ms) | harvested argument (ms) | step closed form | t₃dB sine E1 / saw E1 / sine C3 / saw C3 (ms) | error % |
|---|---|---|---|---|
| 5 | 7.308 | 8.962 | 6.50 / 4.44 / 4.92 / 4.42 | +30.0 / −11.3 / −1.7 / −11.7 |
| 10 | 15.351 | 17.923 | 10.94 / 8.92 / 9.40 / 10.92 | +9.4 / −10.8 / −6.0 / +9.2 |
| 20 | 33.314 | 35.847 | 20.92 / 18.94 / 19.42 / 20.79 | +4.6 / −5.3 / −2.9 / +4.0 |
| 40 | 67.690 | 71.693 | 40.08 / 41.44 / 38.58 / 39.94 | +0.2 / +3.6 / −3.5 / −0.2 |
| 100 | 175.324 | 179.233 | 100.15 / 101.25 / 98.65 / 99.98 | ≤ 1.4 |
| 250 | 444.323 | 448.082 | 250.23 / 250.98 / 248.73 / 250.08 | ≤ 0.5 |

### S12.3 Bass compressor (`node … comp`, 8.1 s; kSawAnchor; seed threshold −24 dB re the detector; ratio 4; knee 6; attack 10 ms)

**SC-007** (short / long half-recovery, measured from the last sample within 0.1 dB of each event's peak GR):

| Note | Hold | D-5 rule | Fixed 275 ms |
|---|---|---|---|
| E1 | none | 5.4 / 30.3 ms = 0.178 | 0.646 |
| E1 | one period | 19.4 / 85.6 ms = **0.227** | **1.022** |
| C3 | one period | 17.2 / 85.6 ms = **0.201** | **0.999** |

With the hold, the D-5 rule passes the ½ bar and the fixed release, the positive control, fails it.

**Steady GR ripple, peak-to-peak:**

| | E0 | E1 | E2 | C3 |
|---|---|---|---|---|
| no hold | 3.204 dB | 1.473 dB | 0.644 dB | 0.358 dB |
| one-period hold | 0.002 dB | 0.008 dB | 0.010 dB | 0.041 dB |

That is the SC-006(a) M_sub ≤ 0.5 dB headroom. The model uses a plain |x| inside the hold, so the true-peak estimate can
only lower these figures. The seed's mean GR (≈ 17 dB) is far above the S8.3 target, so T020 raises the threshold.

### S12.4 CPU (estimate; T001 measures before anything else — R-1)

**Measured (T001, main loop, 2026-10-17, pinned through tools/pin-perf-cores.ps1, three runs 60 s apart, nothing else running; evidence/run-log/T001.txt, T001-bench.txt):** medians in % of one core at 48 kHz, variant base: shaper 0.3575, compressor 0.3921, **total 0.7694 (the gated SC-017 cell, bar 1.0 %)**, dc_limiter 1.3156 (recorded for Phase 9, not in Total), linked 0.6469, silence 0.5217. DECISION: PROCEED with the plan's design; the TICK4 and TP2X fallbacks are not needed. The unpinned smoke had read 1.54 %; only the pinned median decides (Phase 5 precedent). The linked cell confirms that a mono-link fast path could not lower the gated distinct-pair cell.

**Measured on the shipped code (close, 2026-10-17 / 18; evidence/run-log/T018.txt, evidence/run-log/close/stage-bench.txt):** `MassDynamics_CpuBudget` isolated and pinned read 1.047 / 1.183 / 1.270 %; the prototype figure did not carry over (clang vs the test build's MSVC, and the prototype's own dB math). A per-stage profile of the shipped headers under the MSVC flags read 1.145 % for the gated cell with six run-time `gainToDb` / `dbToGain` series evaluations per sample pair at ≈ 24 ns each. Refinement M-3 (spec FR-045): the three conversions the Phase 6 headers own use `std::log2` / `std::exp2` forms (`detail::gainToDbFast` / `dbToGainFast`), `setTransientDurationMs` early-outs on an unchanged value; the profile reads 0.802 %. The R-1 fallbacks (2) and (3) are not applied. The gate remains `cpu-lane.sh` alone at close.

Per stereo sample:
- shaper: 2 × 4× upsamplers, 1 `gainToDb`, 1 `dbToGain`, 2 EFs;
- compressor: 2 biquads, 2 upsamplers, `DynamicsProcessor::processSample` (1 `gainToDb`, 2 discarded `dbToGain`), our
  `dbToGain`.

That is about six series transcendentals at roughly 50–60 flops each (db_utils.h:160–252), plus four upsamplers: about
550 flops in all. That is ≈ 0.5–0.8 % of a 3 GHz core at 48 kHz, against the 1 % bar. The margin is thin. R-1 lists the
fallbacks.

---

## S13. Test plan (file · TEST_CASE · assertion strategy)

### S13.1 Files, targets, tags

| File | Target | Holds |
|---|---|---|
| `tests/test_helpers/k_weighting.h` | (INTERFACE) | `TestUtils::KWeightingFilter` (BS.1770-4 two biquads; the coefficients are derived for any fs by the Recommendation's analog prototype: shelf f0 = 1681.97 Hz, G = 3.99984 dB, Q = 0.7072; RLB f0 = 38.135 Hz, Q = 0.5003), `kWeightedLevelDb(x, n, fs)` = −0.691 + 10·log10(mean square) |
| `dsp/tests/unit/processors/period_peak_detector_test.cpp` | `dsp_processors_tests` | Constants equal HarmonicFold's; steady E0 saw and sine held value constant to 1e-4 relative (true-peak); hold change applied at a segment boundary; `held ≥ |x|`; no allocation |
| `dsp/tests/unit/processors/transient_shaper_test.cpp` | `dsp_processors_tests` | SC-008, SC-009, SC-011 (shaper), SC-012 (shaper arm), SC-013 (shaper arm), SC-016/018/021 (shaper arms), SC-025, lifecycle, `TransientShaper_DurationTableHarvest` `[.calibration]` |
| `dsp/tests/unit/processors/transient_shaper_nonfinite_test.cpp` | `dsp_processors_tests` (`-fno-fast-math`) | SC-019 shaper arm |
| `dsp/tests/unit/systems/mass_dynamics_probe.h` | — | `detail::MassDynamicsProbe` (static: setFixedRelease, setHpBypass, setRectifiedFeed, unsmoothed Mass-step and hard-gain-switch helpers) and `detail::TransientShaperProbe` (static: `setHoldBypass(TransientShaper&, bool)` → `probeHoldBypass_`, SC-009's negative control; `setGainSmootherBypass(TransientShaper&, bool)` → `probeGainSmootherBypass_`, SC-008(d)'s positive control; S4.2 / S4.3). SC-008(f)'s smoothed A_s and S_s are the public taps `smoothedAttackDb()` / `smoothedSustainDb()` (S4.1), not probe reads |
| `dsp/tests/unit/systems/mass_dynamics_test_helpers.h` | — | S9 |
| `dsp/tests/unit/systems/k_weighting_helper_test.cpp` | `dsp_systems_tests` | SC-026 |
| `dsp/tests/unit/systems/mass_compressor_test.cpp` | `dsp_systems_tests` | SC-006, SC-007, SC-022, SC-024, SC-011 (compressor), SC-013 (compressor arm), `MassCompressor_Lifecycle` (FR-047), `MassCompressor_AmountTableHarvest` `[.calibration]` |
| `dsp/tests/unit/systems/mass_law_test.cpp` | `dsp_systems_tests` | SC-001(a), SC-004, SC-023 (law level), SC-025 (no attack/sustain field), table `static_assert` mirror |
| `dsp/tests/unit/systems/mass_law_spectral_test.cpp` | `dsp_systems_tests` | SC-001(b)(c) `[long]`, SC-002, SC-003 `[long]`, SC-005 `[long]`, SC-011 (Mass), SC-023 (rendered), `MassLaw_TableHarvest` `[.calibration]` |
| `dsp/tests/unit/systems/mass_chain_test.cpp` | `dsp_systems_tests` | SC-010 `[long]`, SC-012, SC-014, SC-015, SC-016, SC-018, SC-021, `MassDynamics_Lifecycle` (FR-047), the `kControlInterval` `static_assert`s (S11), FR-035 handoff record |
| `dsp/tests/unit/systems/mass_dynamics_nonfinite_test.cpp` | `dsp_systems_tests` (`-fno-fast-math`) | SC-019 (compressor, MassDynamics, MassLaw arms) |
| `dsp/tests/unit/systems/mass_dynamics_perf_test.cpp` | `dsp_systems_tests` `[performance][.perf]` | SC-017 |

Tags:
- `[processors][profundum][transient-shaper]` (and `[period-peak]` for the detector TU);
- `[systems][profundum][mass-dynamics]` and `[systems][profundum][mass-law]`.

`[long]` goes only on the multi-minute grids. The non-finite, bounded-grid and state cases never get it (root
CLAUDE.md).

### S13.2 Shared conventions

- **Notes.** E0, E1, E2 and C3 come from `ShapeCharacterTest::kE0Hz` and the related constants. fs is 48 kHz unless the
  row says otherwise.
- **Repeat noise (SC-001(b)).** ε per descriptor per cell = max |Δ| over 8 start offsets (k · period/8) × block sizes
  {64, 512}, capped at 0.1 dB / 0.005 oct. τ = ε. A step passes if Δ ≥ −τ in the documented direction. Every ε is
  printed.
- **Descriptors.** These are `ShapeCharacterTest::describe` (E_low, E_high, THD, C, L), w₁ via
  `ProfundumTest::renderedHarmonicPowers`, P via `kWeightedLevelDb`, and L via `rmsDbfs` on whole periods. Every
  descriptor is printed with `std::setprecision(6)` (the root cross-platform rule).
- **No bit-exact float goldens.** Bit-exact comparisons are only between two renders in the same binary (SC-012(a),
  SC-013, SC-014(b)(e), SC-015(b), SC-018, SC-021 restart, SC-023 stages). There are no stored sample goldens.

### S13.3 Per criterion

**Gate constants for the unratified items (S1 Status).** Each has one `constexpr`, set to the spec bar until the
ruling is written into the spec's SC row (FR-048): `kGateKWeightAt` (C-1, SC-003), `kGate5msAt` (C-2, SC-008(c)),
`kSc010Method` (C-3, SC-010), `MassLaw::kSlewBoundRefMs` (C-7, SC-004) and `kRecoveryFrom` (C-13, SC-007). A ruling
flips that constant and nothing else. Every alternative reading is computed and printed beside the gated one.

| SC | TEST_CASE | Assertion strategy |
|---|---|---|
| SC-001(a) | `MassLaw_TargetsMonotonic` | For each of the 3 fixture bases (the stored reference base, kSawAnchor, kSineAnchor, each with the Targets defaults for the non-recipe fields): `evaluate(m, base)` at m = 0, 0.1, …, 100 (1001 points). Each field is monotone in its D-2 direction (non-strict). Clamped runs are counted and printed |
| SC-001(b) | `MassLaw_RenderedTargetsMonotonic` `[long]` | `massAt` ≡ 0 and all seven targets overridden (the compression lever's others at their Mass 50 base values, G8-7). The driven target k gets `targetOverride[k]` = `evaluateRow(kRows[k], m, base_k)`; every other target j gets `evaluateRow(kRows[j], 0, base_j)`, its Mass 0 value. 101 points of m at E1 and C3. Static renders of `descriptorSeconds(fs)` (6.46 s at 48 kHz), with descriptors on the last `kLowFrequencyFftSize` samples. The descriptor of each row is the spec's (duration → D_tr with the shaper at +6 / 0 dB; compression → ΔL_step on the step fixture). Every adjacent step must have Δ ≥ −ε. Shards: one TEST_CASE per target × note (12) |
| SC-001(c) | `MassLaw_FundamentalStrengthJointSweep` `[long]` | All six targets from the law via `renderChain` (no overrides); 101 points × 3 fixtures × {E0, E1, E2, C3}; renders of `descriptorSeconds(fs)`, w₁ of `sumL` (the fold + anchor sum tap; G8-2 ruling 2026-10-17) on the last `kLowFrequencyFftSize` samples. w₁ at every Mass point ≥ its Mass 0 value − 6 dB − ε (bounded fall, G8-4 ruling 2026-10-17); adjacent steps recorded. Shards: one TEST_CASE per fixture × note (12) |
| SC-002 | `MassLaw_AnchorPoints` | Reference voice at E1 and C3, chain at Mass {0, 50, 100}. THD(0) ≤ −30 dB and GR(0) == 0.0f (the compressor tap). THD(50) ≥ THD(0) + 6 and E_low(50) ≥ E_low(0) + 3. THD(100) ≥ THD(50) + 6 and steady GR(100) ≥ 3 dB |
| SC-003 | `MassLaw_LevelSlopeBounded` `[long]` | 101 points × 3 fixtures × 4 notes (Ref and Saw gated; the pure-sine fixture recorded, G8-2 ruling 2026-10-17: the 2·f₀ sidechain cannot see it and the makeup alone spans ≈ 5.7 dB); renders of `descriptorSeconds(fs)`, P and L on the last `kLowFrequencyFftSize` samples. Per C-1, `constexpr std::array<bool, 4> kGateKWeightAt` = {true, true, true, true}, the spec bar and the default until C-1 is ruled into the SC-003 row (option (a) flips it to {false, false, true, true}). Where gated, span(P) ≤ 3 dB and max \|ΔP\| ≤ 0.2 dB; span(L) ≤ 6 dB everywhere. P is printed at every cell. Shards: one TEST_CASE per fixture × note (12) |
| SC-004 | `MassLaw_SlewBounded` | `MassLaw` alone, prepared at 48 kHz: setMass(0), reset, setMass(100), then `advanceInterval()` until done. Each step's \|Δtarget\| ≤ (hi − lo) · (32/fs) / (kSlewBoundRefMs · 0.001) + 1 ulp. The traversal count equals ceil(kMassSweepMs · 0.001 · fs / 32) ± 1. Per C-7 the default is option (ii), one T = 40 ms in both clauses (`kSlewBoundRefMs == kMassSweepMs`). A ruling for (i) sets `kSlewBoundRefMs = 20.0f`, and the row then reads 20 ms in the bound and 40 ms in the completion clause; that ruled form is written into spec SC-004 first. Run through `gridDriver` at host blocks {1, 17, 64, 512}: the per-interval target sequences are bit-identical |
| SC-005(a) | `MassLaw_AutomationContinuous` `[long]` | Chain with kSawAnchor at E0, E2 and C3; Mass steps 0 ↔ 100 and 20 ms ramps; blocks {64, 512}. `differentialClicks(transition, refStart, refEnd, …)` counts 0. Positive controls (probe: an unsmoothed Mass step that writes `evaluate(100)` at once, and a hard 0 → +12 dB gain switch) must each be flagged ≥ 1 |
| SC-005(b) | (same file) `MassLaw_StaticLattice` `[long]` | 101-point static sweep, renders of `descriptorSeconds(fs)` with descriptors on the last `kLowFrequencyFftSize` samples: \|ΔL\| ≤ 0.5 dB, \|ΔC\| ≤ 0.1 oct and \|ΔE_low\|, \|ΔE_high\| ≤ 1 dB. Every other descriptor's max step is recorded |
| SC-005(c) | `MassLaw_AutomationDescriptorTrajectory` `[long]` | The 40 ms traversal (and the 20 ms host ramp) at E0, E2 and C3. One-period windows slide one period at a time, each resampled onto ⌊P⌋ points (Kaiser sinc 32 / β 10) before its DFT (Group 6 ruling 2026-10-17; spec SC-005(c)). Each per-window change ≤ (b)'s max step × (Mass units per window) + ε |
| SC-006(a)(b)(d) | `MassCompressor_NoSubPumping` | The Mass reference voice through `renderChain` with `massAt` ≡ 50 (every target at its base) and `targetOverride[CompressionAmount]` ≡ 1. Corner keytracked at the note, at E0, E1, E2 and C3. Two separate renders per note, so the Anchor step never falls inside (a)'s window. **Render A** (`seconds = 3.0`, Anchor level at its base): (a) M_sub over [1 s, 3 s] ≤ 0.5 dB with both s(t) readings, the low-pass group delay printed; (d) mean GR over [1 s, 3 s] ≥ 3 dB. **Render B** (`seconds = 4.0`, `targetOverride[AnchorLevel]` = 0.25 for n < 2 s and 1 from 2 s; Mass, Body and every other target unchanged): (b) mean GR over [1.5 s, 2 s] vs [3 s, 3.5 s] differs by ≤ 1 dB |
| SC-006(c) | (same) | Probe `setHpBypass(true)` on the same two renders: at E0, (a) or (b) exceeds its bound (`CHECK`); the verdict is printed |
| SC-006(e) | `MassCompressor_RectifiedFeedNegativeControl` | Probe `setRectifiedFeed(true)` on the same two renders per note: fails (b) at E2 or (d) at E0 / E1. Per-note verdicts are printed. If it passes everywhere, `FAIL("fixture lacks teeth")` |
| SC-007 | `MassCompressor_ProgramDependentRelease`, `MassCompressor_FixedReleasePositiveControl` | Amount 1 (forced via `setAmount(1)`), kSawAnchor at E1 and C3. The base is scaled so the detector tap reads the amount-1 threshold; events are +12 dB as read at `detectorLevel()`, 20 ms and 1 s. Recovery runs to the point where GR has recovered by half its peak (dB), from the start point `kRecoveryFrom` selects (C-13): until ruled, `RecoveryFrom::Peak`, the spec's "each event's own peak GR", taken as the first sample at which \|GR\| is maximal. The C-13 reading (`RecoveryFrom::PlateauEnd`, the last sample within 0.1 dB of the peak, the S12.3 model definition) is computed and printed in every cell. short ≤ 0.5 · long. Law tap: `programFraction()` 0 gives 50 ms and 1 gives 500 ms; monotone over a ramped f. The positive control with probe fixed 275 ms has a ratio > 0.5 |
| SC-008(a)(b)(f) | `TransientShaper_GainsMatchParameters` | The spec's burst fixture unchanged (held 1 s, onset at t = 0 of the plain arm), sine and kSawAnchor at E1 and C3. (a) 5 × 5 grid of {−12, −6, 0, 6, 12}² at the default duration, 40 ms: slow argument 67.69 ms, so τ_real = 8 · 67.69 / 2π = 86.2 ms. Steady window **[0.85 s, 1.00 s]** after onset: it starts after 4·τ_real + 0.5 s = 0.845 s (9.9 τ_real) and ends at the burst's release, so no release-tail sample is read. The duration is fixed for (a) because at longer settings the settled window would start after the 1 s hold ends (250 ms: τ_real ≈ 566 ms, start ≈ 2.76 s). \|mean(gain tap) − S\| ≤ 0.25 dB. (b) Max gain over the onset within 0.5 dB of A. (f) Every sample: \|tap − (A_s · c + S_s · (1 − c))\| ≤ 0.1 dB, with c recomputed from `fastEnvelope()` / `slowEnvelope()` and A_s, S_s read from the `smoothedAttackDb()` / `smoothedSustainDb()` taps |
| SC-008(c) | (same) | A = +12, S = 0, durations {5, 20, 40, 100, 250} ms; t₃dB within ±15 %. Per C-2, `kGate5msAt` = {true, true} for {E1, C3}, the spec bar and the default until C-2 is ruled into the SC-008(c) row (option (a) flips it to {false, true}). Every cell's error is printed |
| SC-008(d) | (same) | Step each setting mid-sustain: `differentialClicks` = 0. Positive control: `TransientShaperProbe::setGainSmootherBypass(true)` (S4.3 step 6) must be flagged ≥ 1 |
| SC-008(e) | (same) | 2 s silence pre-roll after `reset()`: peak within ±0.5 dB of A; max(g) ≤ A + 0.5 dB |
| SC-009 | `TransientShaper_NoWaveformRateRipple` | Steady sine and kSawAnchor at E0, E1 and E2 at all 25 grid settings, at the default duration and at 5 ms: gain tap max − min over [3 s, 4 s] ≤ 0.2 dB. A negative control (`TransientShaperProbe::setHoldBypass(true)`, S4.3 step 2: the EFs fed max(\|xL\|, \|xR\|) directly) exceeds it at E0 |
| SC-010 | `MassDynamics_AliasingE0E2` `[long]` | Shaper at 5 ms, A = ±12; compressor at amount 1 (fastest = the table attack, release 50 ms); inputs kSawAnchor and `fold100(kSawAnchor)`; E0–E2; fs {44.1, 48, 96} kHz. **Gated by `kSc010Method` (C-3), default `Spec` until ruled:** `aliasedPower` on the [1 s, 2 s] steady window and on [0, 100 ms] and [step, step + 100 ms], in and out: A_out ≤ max(−80, A_in + 0.5); a cell whose frame excludes every lattice bin is flagged `vacuous`, reported (FR-048) and not counted as a pass. **Run and printed beside it (`Residual`, the gate once ruled):** steady `aliasedPower` on an `aliasFftLength` frame at 1 s, in and out, A_out ≤ max(−80, A_in + 0.5); moving `aliasResidual` ≤ −80 dBFS over the two 100 ms windows; the positive control reads > −80. FFT length and bin Hz are printed per cell for both methods |
| SC-011 | `MassLaw_Audibility`, `TransientShaper_Audibility`, `MassCompressor_Audibility` | Reference voice at E1. Mass 0 vs 100: `separate()` ≥ 3 dB or 0.25 oct. Each target alone moves its SC-001(b) descriptor by ≥ 3 dB. Shaper A over −12…+12 dB and S over −12…+12 dB move D_tr and the steady L by ≥ 3 dB. Duration 5 → 250 at the defaults moves D_tr by ≥ 3 dB, and the 0 / 0 inert case is printed. Compression: ΔL_step(0) − ΔL_step(1) ≥ 3 dB, and GR is printed |
| SC-012 | `MassDynamics_MonoSum` | (a) L == R input, every grid point of SC-008 and amounts {0, 0.5, 1}: `std::memcmp(outL, outR) == 0`. (b) The kSawAnchor pair at 1 : 0.5: the gain taps are a single value by construction, so the test asserts outL[n] · inR[n] == outR[n] · inL[n] within 1 ulp-scaled, plus side / mid energy ratio within 0.01 dB |
| SC-013 | `MassDynamics_TransparentAtNeutral` | Shaper 0 / 0 and amount 0, with kSawAnchor and the reference voice at E0 and C3. From 100 ms: `memcmp(out, in) == 0` |
| SC-014 | `MassChain_CoreAnchorValidated` | Mass {0, 25, 50, 75, 100} × {E0, E1, E2, C3}. (a) Product read-back, not the fixture's own writes: `anchorControlsPerInterval[k]` (`AnchorOscillator::pendingControls()`, anchor_oscillator.h:361) differs from the `Controls` the fixture started from only in `level`, and that `level` equals the law's Anchor-level target at k. Compile time: `static_assert(sizeof(MassLaw::Targets) == 6 * sizeof(float))` and a `constexpr` count over `MassLaw::kRows` of rows whose target is `Target::AnchorLevel` == 1, so Mass owns exactly one Anchor field. a₁ against an independent observation: on static-Mass renders, the h1 amplitude of the `coreL` tap, measured by a single-bin DFT over the last 64 whole periods, matches the recorded a₁ within 0.05 dB. That reading comes from the core's rendered output, so a wrong gain index, a wrong pan constant or a stale block fails it. (The `AnchorOscillator` has no public a₁ accessor, S0; the recorded-value equality is a fixture self-check and is not counted as SC-014 evidence.) (b) The core taps are memcmp-equal with `attachAnchor` false and true. (c) Finite (bit-pattern) and \|out\| ≤ `getCeilingLinear()`. (d) mean(out) over the last 2 s ≤ −80 dBFS. (e) RMS(anchor tap) / (a1 / √2) is monotone in the Anchor-level target, and sum == fold + anchor memcmp-equal. w₁ is printed |
| SC-015 | `MassChain_LimiterIdleSteadyState` | The reference and saw fixtures (the sine fixture recorded, G8-6 ruling 2026-10-17) and every Mass in {0, 25, 50, 75, 100}. GR from the limiter's in/out taps on \|in\| > 1e-4: (a) ≥ −0.01 dB from 1 s; (b) cells that never engaged are memcmp-equal from 1 s. Attack GR (peak, duration) is printed |
| SC-016 | `MassDynamics_NoAllocation` | `AllocationScope` around `processBlock` (shaper, compressor, MassDynamics), every setter (finite and non-finite), `reset`, `MassLaw::{setMass, setBase, advanceInterval, evaluate}`, a Mass step and a forced release change: count == 0 |
| SC-017 | `MassDynamics_CpuBudget` `[performance][.perf]` | 48 kHz stereo (distinct L/R kSawAnchor pair), MassDynamics at A = +12 / S = −6, duration 40 ms, amount 1, key E1, plus one `MassLaw::advanceInterval` + `evaluate` per 32 samples. 10 s of audio, best of 5, `steady_clock`: ≤ 1.0 % of real time. DCBlocker ×2 + TruePeakLimiter are printed separately. It runs only through `node tools/run-cpu-tests.js dsp_systems_tests` |
| SC-018 | `MassDynamics_PartitionInvariant` | The full chain at E1, with the Mass step and a forced release change (probe) at sample 8704 (C-11), at blocks {1, 17, 32, 64, 512}: memcmp-equal outputs. Two renders from `reset()` are memcmp-equal |
| SC-019 | `MassDynamics_NonFinite` (and `TransientShaper_NonFinite`) | 16-sample bursts of NaN, +Inf and −Inf bit patterns (`std::bit_cast<float>(0x7FC00000u)` and the like, in the `-fno-fast-math` TU). Every output is finite (bit test), and samples whose input was non-finite are exactly 0.0f. `stateFinite()` holds after 1 block. L is within 0.1 dB of the pre-burst steady state within 100 ms (single-stage arms) or at 600 ms = 100 ms past the longest D-5 release (whole chain, bursts 1 s apart, 100 ms recorded; Group 7 ruling 2026-10-17, spec SC-019). Non-finite `setMass`, each `setBase` field and every setter keep the previous value |
| SC-021 | `MassDynamics_SampleRates` | {44.1, 48, 88.2, 96, 192} kHz: finite and bounded. At E1, the SC-008(a)(b) gains and the SC-006(a) M_sub are within 0.25 dB of 48 kHz. prepare(48k) → prepare(96k) → prepare(48k), then render, is memcmp-equal to a fresh 48 kHz render |
| SC-022 | `MassCompressor_MakeupMatchesGr` | The S8.3 calibration voice: `massAt` ≡ 50 and `targetOverride[CompressionAmount]` ≡ the amount under test, `seconds = 3.0`, steady GR = mean over [1 s, 3 s]. For every `kAmountTable` row and amount ∈ {0, 0.25, 0.5, 0.75, 1} at E1 and C3: \|makeupDb − steady GR\| ≤ 0.5 dB. Amount 0: makeup == 0.0f |
| SC-023 | `MassLaw_NeutralAtFifty` | Law: for the 3 bases, `memcmp(evaluate(50, b), b) == 0`. Rendered: the chain at Mass 50 vs `massLawDetached`: the MassDynamics output is memcmp-equal stage for stage, given equal input; the voice descriptors are within ε. Edge bases (depth 1 with Mass 100 pushing down, Fold 100 base with Mass 0) are exercised and printed |
| SC-024 | `MassCompressor_KeytrackedCorner` | (a) key E1 → `detectorCornerHz()` settles within 1 % of 2·f₀ by 200 ms. (b) Key 5 Hz → 20 Hz, key 300 Hz → 500 Hz; a 48-semitone log ramp over 1 s gives a per-interval corner step ≤ the smoother's (1 − exp(−5000 / (20 · fs/32))) × the target jump. (c) Keys 0, −1, NaN and +Inf → settle at 80 Hz. (d) A POLY fixture: two `ProfundumCore` voices at E1 and C3 summed into one `MassDynamics`, keyed once per interval by `lowestSoundingF0({…})` (S9, the engine-side rule Phase 9 adopts, FR-035); `detectorCornerHz()` settles within 1 % of 2 · E1. Teeth: the helper's truth table, {E1, C3} → E1, {C3, E1} → E1, {C3, 0, E1} → E1, {NaN, C3} → C3, {} → 0, fails a first-voice, last-voice or maximum rule, and the voice order of the POLY render is also swapped. (e) SC-018 at blocks {1, 17, 512} with the key at the grid: memcmp-equal |
| FR-047 | `TransientShaper_Lifecycle`, `MassCompressor_Lifecycle`, `MassDynamics_Lifecycle` | For each class: (1) `processBlock` before `prepare` leaves a sentinel-filled buffer memcmp-equal and the taps at their defaults; (2) after `prepare`, a null `left` / `right` and `numSamples == 0` are no-ops: the buffer is untouched, and a following render is memcmp-equal to one without the call, so no phase advanced; (3) `reset()` after 2 s of shaped / compressed kSawAnchor at E1 (A = +12, amount 1, the release rule driven to f > 0.5), then a render, is memcmp-equal to the same render on a freshly prepared instance with the same setters; (4) for `BassCompressor` and `MassDynamics`: `prepare(96k)`, 2 s at f > 0.5, then `prepare(48k)` gives `programFraction() == 0`, `currentReleaseMs() == 50` and a render memcmp-equal to a fresh 48 kHz one (the rulePos_ case, S5.2). All inside `AllocationScope` (SC-016) |
| SC-025 | `TransientShaper_RangesAndDefaults` | Clamping at ±12 dB and 5 / 250 ms; non-finite keeps the previous value; defaults +6 / 0 / 40; 0 / 0 is the identity (SC-013 arm); `MassLaw::Targets` has no attack/sustain field (`static_assert(sizeof(Targets) == 6 * sizeof(float))`) |
| SC-026 | `KWeighting_ReferenceLevels` | 997 Hz full-scale sine at 48 kHz: −3.01 ± 0.1 LKFS (model −3.010). An E1 sine at equal RMS reads ≥ 1 dB lower (model −5.32 dB). The 44.1 kHz and 96 kHz coefficient derivations give the same 997 Hz reading within 0.1 dB |
| FR-035 | `MassChain_Phase9HandoffRecord` | Prints latency 0, the SC-017 cell, the Mass table, the 4-voice Mass-100 poly-sum GR (4 detuned voices summed into MassDynamics, recorded, not gated), limiter attack GR, the corner contract and the shaper parameter table to `evidence/phase9-handoff.txt` |

---

## S14. Build integration

### S14.1 `dsp/tests/CMakeLists.txt` (enumerated, each block with a "Profundum Phase 6" comment)

- **`dsp_processors_tests`**, after the Phase 5 block (line 290): `unit/processors/period_peak_detector_test.cpp`,
  `unit/processors/transient_shaper_test.cpp` and `unit/processors/transient_shaper_nonfinite_test.cpp`.
- **`dsp_systems_tests`**, after line 706 (`shape_character_perf_test.cpp`): `unit/systems/k_weighting_helper_test.cpp`,
  `mass_compressor_test.cpp`, `mass_law_test.cpp`, `mass_law_spectral_test.cpp`, `mass_chain_test.cpp`,
  `mass_dynamics_nonfinite_test.cpp` and `mass_dynamics_perf_test.cpp`.
- **The `-fno-fast-math` block** (it ends at line 1200) gets `unit/processors/transient_shaper_nonfinite_test.cpp` and
  `unit/systems/mass_dynamics_nonfinite_test.cpp`, with the comment "Profundum Phase 6: injects NaN/Inf bit patterns
  (SC-019). The perf TU stays out (shipping FP mode)."
- `tests/test_helpers/CMakeLists.txt` is unchanged (INTERFACE). No `dsp/CMakeLists.txt` change: KrateDSP is header-only
  for these.
- After the test-CMake change, `build/windows-ninja` is reconfigured before clang-tidy (memory: stale compile database).

### S14.2 Commands (Windows, full CMake path) and parallelism

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
# ONE rebuild for both touched executables (no shared-header change, FR-044):
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests dsp_systems_tests
B=build/windows-x64-release/bin/Release
X="~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"
# FULL suites CONCURRENT (not timing-sensitive); capture logs, never re-run for output:
$B/dsp_processors_tests.exe "$X" > logs/p6_proc.log 2>&1 &
$B/dsp_systems_tests.exe    "$X" > logs/p6_sys.log  2>&1 &
# [long] lane, concurrent, one shard per [long] TEST_CASE, list GENERATED from each exe.
# G9-4 (user, 2026-10-18): the list is FILTERED to the phase's own TUs (plus the suites of every consumer of a
# modified shared header), not every [long] case of the executable: as written below it ran 175 shards for about
# 45 Phase 6 cases (4 h) and its one red was an untouched Vorago case's wall-clock budget under load (recorded).
for exe in dsp_processors_tests dsp_systems_tests; do
  while IFS= read -r t; do
    [ -n "$t" ] && $B/$exe.exe "$t" > "logs/p6_long_${exe}_${t//[^A-Za-z0-9]/_}.log" 2>&1 &
  done < <($B/$exe.exe --list-tests "[long]~[performance]~[perf]~[benchmark]~[!benchmark]" --verbosity quiet)
done; wait
# Calibration harvests (on demand, concurrent with each other, never with the CPU lane):
$B/dsp_processors_tests.exe "TransientShaper_DurationTableHarvest" > logs/p6_cal_dur.log 2>&1 &
$B/dsp_systems_tests.exe "MassCompressor_AmountTableHarvest" > logs/p6_cal_amt.log 2>&1 &
$B/dsp_systems_tests.exe "MassLaw_TableHarvest" > logs/p6_cal_law.log 2>&1 & wait
# CPU lane ALONE, after everything has finished and the machine has idled:
node tools/run-cpu-tests.js dsp_systems_tests
# gates: node tools/check-portability.js; node tools/lint-apple-globals.js;
# clang-tidy on the new TUs by file list (per-file timeout), after reconfiguring build/windows-ninja.
```

**Render length.** Every descriptor render is `descriptorSeconds(fs)` = 1 s settle + 262144 samples: 6.46 s at 48 kHz,
6.94 s at 44.1 kHz, 3.73 s at 96 kHz and 2.37 s at 192 kHz (S9). The earlier 3 s sizing could not hold
`describe`'s frame and is withdrawn.

**Sharding.** One `[long]` TEST_CASE per shard, so the list generated above is the shard list:
- SC-001(b): per target × note (6 × 2 = 12 shards of 101 renders);
- SC-001(c) and SC-003: per fixture × note (3 × 4 = 12 shards each, 101 renders each);
- SC-005(a), (b) and (c): per note (E0, E2, C3: 3 shards each);
- SC-010: per fs (3 shards).

**Estimate.** The long lane holds ≈ 1212 (SC-001(b)) + 1212 (SC-001(c)) + 1212 (SC-003) + ≈ 300 (SC-005(b)) ≈ 3950
static renders of ≈ 6.5 s, plus the SC-005(a)(c) automation renders and SC-010. The earlier figure, about 1200 renders
of 3 s, undercounted both the number and the length. At roughly 0.22 s per render (the earlier 0.1 s per 3 s scaled by
2.15×, plus one 262144-point FFT per descriptor) that is ≈ 15 min of CPU. A shard of 101 renders takes ≈ 22–25 s. With
the ≈ 45 shards concurrent on the 32-core machine, the wall clock is ≈ 2–4 min, bounded by the longest shard and
SC-010's E0 frames. Measured figures replace this after the first run, and the shard map is revised from them.

**FR-044 / SC-020.** No reused header changes, so `git diff --stat dsp/include` must list only the four new headers.
If that ever stops holding, the FR-044 suites (`dsp_effects_tests`, `plugin_tests`, `approval_tests`, `ruinae_tests`,
`seraphis_tests`, `vorago_tests`, `innexus_tests`) are rebuilt in one batch and run concurrently.

---

## S15. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | **CPU (SC-017) over 1 %.** The estimate is 0.5–0.8 % (S12.4), and `DynamicsProcessor` wastes 2 exps per sample | **T001 benchmarks first**: a throwaway bench TU with the S5 / S4 per-sample loops, before any test is written (the Phase 5 precedent). Fallbacks, recorded in this order: (1) a mono-link fast path in both detectors (HarmonicFold pattern, harmonic_fold.h:197–205); (2) the compressor's DynamicsProcessor detector tick at /4 on the held value (piecewise-constant input), with its times scaled; (3) the shaper's true-peak at 2× instead of 4×, re-checked against SC-009 / SC-010. The bar is never relaxed (FR-045) |
| R-2 | C-1, C-2, C-3, C-7 and C-13 are not ratified as recommended | The gate constants default to the spec bar (S13.3), so an unratified build cannot ship a relaxed bar; once a ruling is in the SC row, it is a one-constant change. Nothing is relaxed without a ruling (FR-048) |
| R-3 | Float drift between MSVC and Clang in the dB-domain gain law makes SC-012(a) / SC-018 differ across toolchains | Those are same-binary comparisons only, with no stored goldens; cross-toolchain spread affects only printed descriptors (`setprecision(6)`) |
| R-4 | SC-006(b) at E0: the 12 dB/oct high-pass leaves the sub −12.3 dB in the detector (\|H\|² = r⁴ / (1 + r⁴) at r = 0.5), so an Anchor step 0.25 → 1 may move GR by about 0.7–1 dB | The model's peak-sum estimate is ≈ 0.65 dB at a 4:1 ratio. If T020's measurement exceeds 1 dB at E0, `BiquadCascade<2>::setButterworth` (24 dB/oct, sub at −24 dB) replaces the single biquad in the composition only (FR-011's own fallback). The header is not modified |
| R-5 | Biquad coefficient updates per interval during a Drop sweep click in TDF2 | The corner moves by ≤ (1 − exp(−5000 / (20 · fs/32))) of the jump per interval (smoothed in log₂), which is small at the interval rate. SC-005 / SC-024(b) clicks cover it. A larger smoothing time is a constant change |
| R-6 | No Mass table meets SC-001(c) (non-decreasing w₁ while Fold thins h1 up to −12 dB) and SC-003 (unweighted span ≤ 6 dB) together | The S8.2 harvest order targets SC-001(c) first. If the Anchor at level 1 cannot carry the thinned h1, the measured shortfall goes to the user as a FR-021 ruling (for example, cap the Fold amountUp). It is never handled by an Anchor compensation (FR-032). **Occurred 2026-10-17 (Group 8):** the Anchor level is relative to h1, so the Depth and Anchor travel became harvest levers (S8.2 steps 0–1); resolved after five harvest rounds by the G8-3 unbiased Anchor keying, the G8-4 bounded-fall SC-001(c) and the G8-5 output trim |
| R-7 | The hold delays the compressor's recovery by up to one period plus a segment, so SC-007 is tighter at low notes | The model shows 0.227 at E1 (bar 0.5). E0 is not an SC-007 cell; it is recorded |
| R-8 | Denormals in the unflushed upsamplers after silence | FTZ / DAZ is the precondition (polyphase_halfband_oversampler.h:24). The 2 s silence arm (SC-008(e)) and SC-019 run under the test main's FTZ. The perf TU also measures a silent second |
| R-9 | `std::exp2` / `std::log2` / `std::cos` in `controlStep` differ across libms | They are per-interval control values, compared only within one binary; no golden pins them |
| R-10 | Re-attack inside the 50 ms hold, or a legato note at the same level, is not shaped | Recorded limitation (S4.4); Phase 9's audibility table records it. It does not affect any Phase 6 gate (the bursts start from silence or from a −24 dB step) |
| R-11 | The duplicated detector algorithm (C-4) drifts from HarmonicFold's | `period_peak_detector_test.cpp` asserts the constants equal HarmonicFold's public ones and runs the same wide-saw window-ripple probe as D-4 (64 partials ≤ 1e-4; wide saw ≤ 2.3e-3 raw, Group 2 ruling 2026-10-17). A follow-up item proposes that HarmonicFold compose it |

---

## S16. Task outline (for tasks.md)

1. **T001** CPU bench first (R-1) and the model output to `evidence/model-2026-10-17.txt`.
2. **T002** `k_weighting.h` and SC-026, failing first.
3. **T003–T005** `PeriodPeakDetector` (tests first).
4. **T006–T012** `TransientShaper`: the SC-008 / 009 / 012 / 013 / 016 / 018 / 019 / 021 / 025 arms; then
   T010, the duration harvest.
5. **T013–T022** `BassCompressor` and `MassDynamics`: SC-006, SC-007, SC-022, SC-024, the FR-047 lifecycle cases and the compressor arms; then
   T020, the amount harvest.
6. **T023–T030** `MassLaw`: SC-001(a), SC-004, SC-023 (law); then T030, the table harvest on the chain.
7. **T031–T040** Chain fixture and SC-001(b)(c), SC-002, SC-003, SC-005, SC-010, SC-011, SC-014, SC-015, SC-018.
8. **T041** SC-017 in the CPU lane, alone.
9. **T042** Gates: the full suites (concurrent), the `[long]` shards, check-portability, lint-apple-globals and
   clang-tidy by file list.
10. **T043** Comply, with the FR-035 handoff record.

Every task goes build → zero warnings → its tests; the test is written first and must fail before the implementation.

---

## Review notes

- 2026-10-17: plan written. The model was run three ways (kweight, shaper, comp); the outputs are quoted in S12. The
  SC-007 recovery definition in the model was corrected during the run: recovery is measured from the plateau's last
  sample within 0.1 dB of the peak, not from its first. That correction changes a gated ratio, so it is now S1 C-13
  (⚠ RE-RULE). Until it is ruled, the SC-007 row gates the spec's reading and prints the corrected one.
- 2026-10-17, review round (spec-coverage, rt-layers, reuse-reality lenses). All issues accepted; none rejected.
  - **ChainConfig overrides (major).** `targetOverride` per `MassLaw::Target` (S9). SC-001(b), SC-006(a)–(e), SC-022 and
    S8.3 are rewritten on it: Mass held at 50, compression overridden to 1, Anchor level stepped 0.25 → 1 in a separate
    4 s render B.
  - **SC-008(a) window (major).** Duration fixed at the 40 ms default, window [0.85 s, 1.00 s] on the spec's 1 s burst.
    The fixture is not changed, so nothing is recorded as a deviation.
  - **Calibration voice (major).** S8.3 uses one voice for thresholds and makeup and names a harvest target for every
    gated cell (SC-006(d) at four notes, SC-002 at E1 / C3, SC-022 at E1 / C3).
  - **Unratified relaxations (major).** C-1, C-2, C-3 now default to the spec bar; C-7 and the SC-007 recovery start
    (new C-13) are promoted to ⚠ RE-RULE with spec-literal defaults. C-7's default (ii) caps the Fold and compression
    seed rows at half range (S8.2); this follows the spec's single traversal time, and it is not a threshold change.
  - **Descriptor render length (major).** 3 s renders could not satisfy `describe`'s `REQUIRE(n >= 262144)`. The
    default is now `descriptorSeconds(fs)`, and the S14.2 estimate and shard map are redone.
  - **Minors.** Fixture-only assertions in SC-014(a) and SC-024(d) replaced by product read-back
    (`pendingControls()`), an independent a₁ measurement and a `lowestSoundingF0` truth table. TransientShaper probe
    fields and taps declared (S4.1–S4.3). `BassCompressor` / `MassDynamics` `reset()` and lifecycle written out
    (S5.2, S6) with FR-047 tests. The `kControlInterval` `static_assert` moved to a test TU (S11). The denormal bullet
    corrected: `Biquad::process` flushes z1 / z2 (biquad.h:365–366). `kCenterPanGain` qualified. `static_cast<float>`
    on every smoother `configure` rate.
