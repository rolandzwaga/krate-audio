# Implementation Plan: Profundum Phase 7 — Bass Stereo Field

**Spec:** `specs/profundum-phase7-stereo-field/spec.md` (Clarified 2026-10-18, Q1–Q8 ruled)
**Roadmap:** `specs/Profundum-roadmap.md` Part A, Phase 7 (lines 521–548)
**Phase base:** commit `4599d180` on `synth/profundum`
**Status:** Planned 2026-10-18, revised after review the same day. **Section S1 ratified 2026-10-18** (spec
Clarifications "Session 2026-10-18 (plan-stage rulings)"): C-1, C-2 and C-17 were ruled to the recommended reading, so
the three test switches are `kGateMidBitExact = false`, `kGateLrBitExactAnyState = false` and
`kGateMixedInterpolationPairs = false` (the spec-as-written reading is still computed and printed beside each gate);
C-9 option (A), C-10, C-15 and C-18 are ratified; the C-13 ⚠ RISK escalation floors stand (the harvest stops and asks
if no pair with g_floor ≥ 0.1 and K_wide ≥ 0.5 passes); R-1 is ruled only if T001 measures a cadence problem. Every
other S1 item is a refinement inside the freedom the spec leaves to the plan and is encoded as written.

**Model:** `specs/profundum-phase7-stereo-field/stereo-field-model.js` (Node, per the root CLAUDE.md). Run it with
`node specs/profundum-phase7-stereo-field/stereo-field-model.js`; it finishes in under a second. Its figures are quoted
in S12. They are sizing figures, not compliance evidence.

---

## S0. Verification ledger (every fact below was read in this session)

Paths are relative to `dsp/include/krate/dsp/` unless stated.

| Fact | Where |
|---|---|
| `CrossoverLR4` is L2 and includes `primitives/biquad.h` and `primitives/smoother.h` only | `processors/crossover_filter.h:2, 24–25` |
| `enum class TrackingMode : uint8_t { Efficient, HighAccuracy }` is at namespace scope | `crossover_filter.h:41–44` |
| Class-scope constants: `kMinFrequency = 20.0f`, `kMaxFrequencyRatio = 0.45f`, `kDefaultSmoothingMs = 5.0f`, `kDefaultFrequency = 1000.0f`, `kHysteresisThreshold = 0.1f` | `crossover_filter.h:102–106` |
| Non-copyable, movable | `crossover_filter.h:119–155` |
| `void prepare(double sampleRate) noexcept` configures the smoother from `smoothingTimeMs_`, **snaps it** to the clamped stored target, calls `updateCoefficients` and resets the 4 biquads. It is documented "NOT real-time safe", but its body contains no allocation | `crossover_filter.h:169–192` |
| `void reset() noexcept` clears the 4 biquads only. The frequency smoother is **not** snapped | `crossover_filter.h:200–205` |
| `setCrossoverFrequency(float)` clamps to [20, 0.45·fs], stores the atomic and calls `frequencySmoother_.setTarget` | `crossover_filter.h:218–222` |
| `setSmoothingTime(float ms)` reconfigures the smoother when prepared | `crossover_filter.h:231–236` |
| `getCrossoverFrequency()` returns the stored **target** | `crossover_filter.h:255–257` |
| `process(float)` runs `frequencySmoother_.process()` every sample. In Efficient mode it recomputes coefficients when the smoothed value has moved ≥ 0.1 Hz from `lastCoefficientFreq_` (306–311). In HighAccuracy mode it recomputes only while `!frequencySmoother_.isComplete()`, tested **after** `process()` (298–303), so a step that the smoother snaps in one call (< 1e-4 off the target, smoother.h:199–201, 232–234) never reaches the coefficients. LP = lpStage2(lpStage1(x)), HP = hpStage2(hpStage1(x)). It returns {0, 0} when unprepared (290–292) | `crossover_filter.h:289–323` |
| `updateCoefficients(freq)` configures all 4 biquads (2 LP, 2 HP) at `sampleRate_` | `crossover_filter.h:355–366` |
| `clampFrequency` is `std::clamp(freq, kMinFrequency, sampleRate_ · kMaxFrequencyRatio)`, so a sample rate of 0 gives lo > hi (UB for `std::clamp`). The class's own default is `double sampleRate_ = 44100.0`, with `prepared_ = false` | `crossover_filter.h:349–352, 382, 384` |
| Members: 4 × `Biquad` and one `OnePoleSmoother frequencySmoother_` | `crossover_filter.h:373–379` |
| `Biquad` is TDF2. A non-finite input `reset()`s it and returns 0. z1/z2 pass through `flushDenormal` | `primitives/biquad.h:4, 352–368` |
| `kMinSmoothingTimeMs = 0.1f`, `kMaxSmoothingTimeMs = 1000.0f`, `kCompletionThreshold = 0.0001f` | `primitives/smoother.h:55–61` |
| One-pole coefficient `exp(-5000 / (clampedMs · fs))` | `smoother.h:77–93` |
| `OnePoleSmoother`: `configure(ms, fs)` 160. `setTarget` maps NaN to 0 (current to 0 as well) and ±Inf to ±1e10, 170–181. `process()` snaps to the target within 1e-4; otherwise it computes `target + coeff·(current − target)` and then `flushDenormal`, 197–211. `isComplete` 232. `advanceSamples(n)` uses `std::pow`, 243–254. `snapTo` 263–272 | `smoother.h` |
| `LinearRamp`: `configure(ms, fs)` 329. `setTarget` **recomputes the increment from the remaining distance** on every call, 342–354. `process()` clamps on overshoot, so it lands **exactly** on the target, 370–389. `isComplete` is `current_ == target_`, 409. `snapTo` 421 | `smoother.h` |
| `MidSideProcessor::prepare(float sampleRate, size_t)` 96. `process(...)` **fuses** encode → gains → width → solo → decode in one per-sample loop. Encode: `mid = (L + R) * 0.5f; side = (L - R) * 0.5f;` 200–201. Decode: `leftOut = mid + side; rightOut = mid - side;` 225–226. There is no separate encode or decode entry point | `processors/midside_processor.h:96, 183–228` |
| `BrownianDrift : public ModulationSource`, L2. Constants: `kTauMin = 0.2f`, `kTauMax = 30.0f`, `kInternalStd = 0.5f`, `kDriftOutputSmoothMs = 150.0f`, `kControlRateInterval = 32`, `kDefaultDriftSeed = 0xB17Eu` | `processors/brownian_drift.h:94–109` |
| `prepare(double)` 121–129. `reset()` re-seeds and snaps ("the same seed re-renders identically") 133–135. `setSeed(uint32_t)` 145. `setSmoothness(float)` clamps to [0, 1] and recomputes, 152–155. `setDepth` 159. `setMean` 165. `processBlock(size_t)` runs `advanceControlStep` when `samplesUntilControl_ <= 0`, then `advanceSamples`, 194–206. `getCurrentValue()` clamps to [−1, 1], 212–214 | `brownian_drift.h` |
| τ mapping: `tau = kTauMin + smoothness · (kTauMax − kTauMin)`, `a = exp(−dt/τ)` with dt = 32 / fs | `brownian_drift.h:230–240` |
| `initState()` sets `samplesUntilControl_ = 0`, so the first `processBlock` advances the walk immediately | `brownian_drift.h:242–247` |
| `LFO` (L1) has **no base class**. Namespace-scope constants: `kMinFrequency = 0.01f`, `kMaxFrequency = 20.0f`, `kMinBPM = 1.0f`, `kMaxBPM = 999.0f` | `primitives/lfo.h:34–37, 63` |
| `enum class Waveform : uint8_t { Sine = 0, … }` is at namespace scope | `lfo.h:47–54` |
| `prepare(double)` 83–92. `reset()` is deterministic (seed 12345), 95–107. `[[nodiscard]] float process()` 114. `setFrequency(hz)` clamps to [0.01, 20], 218–223. `setTempoSync(bool)` 237. `setTempo(bpm)` clamps to [1, 999], 246. `setNoteValue(NoteValue, NoteModifier)` 255 | `lfo.h` |
| Synced frequency `= (bpm / 60) / getBeatsForNote(note, mod)`, clamped to [0.01, 20]. Phase increment `= freq / sampleRate_` | `lfo.h:488–504` |
| `NoteValue { QuadrupleWhole = 0, TripleWhole, DoubleWhole, Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond, … }` and `NoteModifier { None, Dotted, Triplet }`. `getBeatsForNote` is constexpr. `kMinTempoSyncBPM = 20.0`, `kMaxTempoSyncBPM = 300.0` | `core/note_value.h:38–47, 54–58, 95–101, 200, 203` |
| `ModulationSource`: `virtual float getCurrentValue() const noexcept = 0` and `virtual std::pair<float,float> getSourceRange() const noexcept = 0` | `core/modulation_source.h:31–41` |
| `detail::isFinite(float)` / `(double)` are bit-pattern tests and survive fast-math | `core/db_utils.h:118–129` |
| `HarmonicOscillatorBank::kFundamentalSpreadScale = 0.25f`, `kCenterPanGain = 0.7071067811865476f` | `processors/harmonic_oscillator_bank.h:104, 107` |
| `applyPanOffsets` runs over all `kMaxPartials`: `newPos = clamp(panPosition_[i] + offsets[i], −1, 1)`, `angle = π/4 + newPos·π/4`, `panLeft_ = cos`, `panRight_ = sin`. There is no smoothing | `harmonic_oscillator_bank.h:647–659` |
| `restoreCenterPan()` refills both tables with `kCenterPanGain` | `harmonic_oscillator_bank.h:708–711` |
| Bank stereo convention: odd harmonics get `direction = −1` (left), even harmonics `+1` (right) | `harmonic_oscillator_bank.h:1155–1156` |
| `ProfundumCore`: `kDefaultPartials = 64`, `kControlInterval = 32`, `kPitchUpdateInterval = 16`, `kDefaultF0Hz = 55.0f` | `systems/profundum_core.h:95–99` |
| `setPartialPanOffsets(const std::array<float,kMaxPartials>&)`: a non-finite element becomes 0, each element is bit-compared, `panDirty_` is set only on a change, and `panNonZero_` is tracked | `profundum_core.h:218–235` |
| `processBlock` grid loop: `controlUpdate(f)` runs at `intervalPhase_ == 0` and calls `forwardPan()` (the call is at 605) | `profundum_core.h:237–290, 605` |
| `forwardPan()`: a non-zero vector goes to `bank_.applyPanOffsets`. A return to zero after a non-zero apply calls `restoreCenterPan()` | `profundum_core.h:614–625` |
| Reset noteOn: `bank_.reset()`, `intervalPhase_ = 0`, `panDirty_ = panNonZero_`, `panAppliedNonZero_ = false` | `profundum_core.h:545–553` |
| `currentFrequency()` is the f0 of the last pitch update. Also `numPartials()` and `deliveredGains()` | `profundum_core.h:298–305` |
| `ExcitationControls { mix = 0, drive = 0.5, asymmetry = 0 }` and `setExcitation(const ExcitationControls&)` | `profundum_core.h:334–338, 356` |
| `BassCharacterStage` is L3 and includes L0–L2 only. `enum class Algorithm : std::uint8_t { Tube, Fold, Rectify, Bite, Crush, Fracture }`. `kParamSmoothMs = 10.0f`. `kMaxChannelOffset = 0.15f` | `systems/bass_character_stage.h:2, 37, 76–85` |
| `prepare(double, size_t)` 176. `reset()` 204. `setAlgorithm` 241. `setAmount` 258. `setChannelOffsets(float amountOffset, float toneOffset)`: L = centre + offset, R = centre − offset, and a non-zero offset unlinks the channels, 271–282. `setFundamentalHz` 287. `processBlock` 334 | `bass_character_stage.h` |
| `HarmonicFold::prepare(double, size_t)` 121, `setAmount` 168, `setFundamentalHz` 178, `processBlock(float*, float*, size_t)` 188 | `processors/harmonic_fold.h` |
| `ShapeMorph::prepare` 135, `setShape(float)` 194, `setFundamentalHz` 209, `processBlock` 221 | `systems/shape_morph.h` |
| `AnchorOscillator::kDefaultLevel = 0.5f` 72, `prepare(double)` 123 | `systems/anchor_oscillator.h` |
| `TruePeakLimiter::prepare(double, size_t)` 60, `setCeilingDb` 86, `processBlock(float*, float*, int)` 105 | `processors/true_peak_limiter.h` |
| Phase 6 chain helpers (all `inline`, namespace `Krate::DSP::MassDynamicsTest`, opened at 74 and closed at 924):<br>• `kGrid` 81 (static_assert that it equals the core and MassDynamics interval, 82);<br>• `gridDriver(total, hostBlock, fn)` 103–114, `ChainConfig` 173–193, `ChainTaps` 195–211 (coreL/R, foldL/R, anchorL/R, sumL/R, dynL/R, dcL/R, outL/R: **no post-character tap**, because the Phase 6 chain has no character stage), `allocateTaps` 213, `chainTargets` 248;<br>• `coreControlsFor` 275, `unbiasedH1Amplitude` 288, `anchorControlsFor` 294;<br>• `processChainTail` 302–340: sum → MassDynamics → DC → limiter, with **no insertion point** between MassDynamics and the DC blockers;<br>• `makeChainDynamics` 348–362, `prepareChainLaw` 364, `renderChain` 372–437 (at a grid point: controls, then `noteOn` at `noteOnSample`, which must be a grid point, 376, 413–416), `renderPoly` 454 | `dsp/tests/unit/systems/mass_dynamics_test_helpers.h` |
| Consumers of `mass_dynamics_test_helpers.h`: `mass_chain_test.cpp`, `mass_compressor_chain_test.cpp`, `mass_dynamics_aliasing_test.cpp`, `mass_dynamics_helpers_test.cpp`, `mass_dynamics_perf_test.cpp`, `mass_law_automation_test.cpp`, `mass_law_spectral_test.cpp` and `processors/transient_shaper_test_helpers.h` | grep, this session |
| Core test helpers (namespace `Krate::DSP::ProfundumTest`, 47–686): `requireLREqual` 61, `goertzelPhase` 157, `bh7PowerSpectrum` 206, `renderedHarmonicPowers` 235, `aliasedPower` 273, `SidebandResult` 307, `sidebandPower(x, fs, f0, controlInterval, pitchInterval, amRateHz, vibratoPeakHz, totalRef)` 323, `rmsDbfs` 453, `renderReference` 498 (mono, centre pan gain) | `dsp/tests/unit/systems/profundum_core_test_helpers.h` |
| Phase 5 fixtures (namespace `Krate::DSP::ShapeCharacterTest`, 42–351): `kE0Hz … kC3Hz` 50–55; `kFixtures` (saw = `kSawAnchor`, growl = `kGrowl`) 66–70. Its header comment (9) only cites the click helper | `dsp/tests/unit/systems/shape_character_test_helpers.h` |
| Click helper: `struct ClickCount` 44; `differentialClicks(transition, refStart, refEnd, n, fs, f0)` 76 and `differentialClicks(transition, reference, n, fs, f0)` 137, namespace `Krate::DSP::ShapeCharacterTest` (38) | `dsp/tests/unit/processors/shape_character_metric_helpers.h` |
| `TruePeakLimiter` is stereo-linked: one gain from `max(|L|, |R|)` | `processors/true_peak_limiter.h:40, 142` |
| `fingerprintRender(std::span<const float>)` 73, `compareFingerprints` 122 | `tests/test_helpers/render_fingerprint.h` |
| `AllocationScope` with `getAllocationCount()` | `tests/test_helpers/allocation_detector.h:111–122` |
| Phase 5 ceiling evidence: Rectify and Fracture read −0.56 … −0.61 dB at 0.10 and −1.10 … −1.46 dB at 0.15 | `specs/profundum-phase5-shape-character/evidence/sc013-offset-ceilings.txt` |
| The systems test list enumerates the Phase 5 and Phase 6 TUs at 701–742. The `-fno-fast-math` block holds the Phase 6 non-finite TUs at 1235–1237. Perf TUs carry `[performance][.perf]` | `dsp/tests/CMakeLists.txt`; `mass_dynamics_perf_test.cpp:249` |
| ODR sweep (this session): `BassStereoField`, `FieldChainConfig`, `renderFieldChain` and `PanReference` have 0 hits under `dsp plugins tests tools`, and no `bass_stereo_field*` file exists | grep |

---

## S1. Spec conflicts and refinements — FOR RATIFICATION

### C-1 RULED 2026-10-18 (recommended reading; FR-025 bullet 2, SC-005 gate): "the stage's M output is bit-identical to its input M" cannot hold in float

Topology (a) decodes `L' = fl(M + S')` and `R' = fl(M − S')`. The measured output mid is (L' + R')/2 = M + (e₁ + e₂)/2,
where e₁ and e₂ are the two decode roundings, each ≤ ½ ulp of |M ± S'|. They cancel only when S' = 0, or by accident. No
re-ordering helps: whenever the exact value R' = L + R − L' is not representable, L' + R' ≠ L + R. "Bit-identical M" is
therefore impossible for any stage that changes S, in any topology.

What the topology does guarantee:
- **(i) Structural.** The decode uses the encoded float M unmodified. No filter, gain or smoother touches it.
- **(ii) Numeric.** Per sample, |M_out − M_in| ≤ 2⁻²³ · (|M| + |S'|): two half-ulp decode roundings plus the encode
  rounding. That is about −138 dB relative to the sample.
- **(iii) Bit-exact where S' = 0.** This covers SOLID once settled, and an L == R input with zero side state (C-2).

**Recommended ruling:** FR-025 bullet 2 and SC-005's gate read: "M is bit-untouched inside the stage; the measured
(L_out + R_out)/2 equals (L_in + R_in)/2 within 2⁻²³·(|M| + |S'|) per sample; bit-identical wherever S' = 0".
Test switch: `kGateMidBitExact`, **ruled → `false`**: the numeric bound is the gate, the bit-difference count is printed. (`true` was the spec as written, red on any non-zero side.)

### C-2 RULED 2026-10-18 (recommended reading; FR-025 bullet 3, FR-062, SC-004(b), SC-004(c)): "L == R in → L == R out, in any mode" needs a zero side state

With an L == R input, S = 0 exactly. `CrossoverLR4` still holds biquad state from earlier side content, though. Its LP
and HP outputs ring down (TDF2, `biquad.h:359–366`) until `flushDenormal` takes them to exact zero. Until then S' ≠ 0,
so L' ≠ R'. This is correct behaviour (it is the tail of real earlier side content), but it is not bit-identity.

**Recommended ruling:** the bit-identity clauses hold "from `reset()`, or while the side path is idle (SOLID settled), or
once the side state has decayed to exact zero".
- The Anchor-only gate (SC-004(c)) and SC-004(b) run from `reset()` in every mode and at every setting, and again after a
  SOLID crossfade.
- A record (not a gate) reports, per crossover, how long the side state takes to reach exact zero after side content
  stops.

Test switch: `kGateLrBitExactAnyState`, **ruled → `false`**: the dirty-state arm records the decay to exact zero per crossover; the from-reset and after-SOLID arms are gated. (`true` was the spec as written: assert from the first L == R sample after L ≠ R content.)

### C-3 (refinement, FR-022): `MidSideProcessor` cannot be composed into topology (a), so its encode and decode are inlined

`MidSideProcessor::process` fuses encode → gains → width → solo → decode in one loop (`midside_processor.h:183–228`) and
has no entry point between encode and decode. Topology (a) needs the side split *between* them, so the class cannot be
called. The stage uses the class's exact formulas inline:
- encode `mid = (L + R) * 0.5f` and `side = (L - R) * 0.5f` (200–201);
- decode `L = mid + side` and `R = mid - side` (225–226).

It also follows the class's side-gain smoothing convention for g_side (`OnePoleSmoother` at 10 ms, 73 and 149).
`MidSideProcessor` stays untouched (FR-073). This is a reuse-inventory correction, not a new component.

### C-4 (refinement, FR-028, SC-012): `reset()` snaps the crossover's smoother by calling `CrossoverLR4::prepare`

`CrossoverLR4::reset()` clears the biquads but leaves `frequencySmoother_` where it was (200–205), and the class has no
public snap. After a render that moved the crossover, a reset would start the next render from a different smoothed
frequency, which breaks SC-012's "two renders from `reset()` are bit-identical". FR-073 forbids adding a snap.

The field's `reset()` therefore calls `crossover_.setCrossoverFrequency(f_s)` and then `crossover_.prepare(sampleRate_)`.
Its body (169–192) only configures the smoother (`constexprExp`), snaps it, computes 4 biquad coefficient sets and
clears state. It allocates nothing and takes no lock or I/O. The header's "NOT real-time safe" note is conservative, and
SC-011 (`AllocationScope` around `reset()`) gates the call. The field's `prepare()` does the same.

### C-5 (refinement for ratification, FR-012, Q5): the exact law family and what `curve` does

The ruling fixes a raised cosine in log-frequency: c = 0 at and below a lower knee, c = 1 at and above f_c times a fixed
upper factor, Gravity as its slope, and `curve` as a continuous shape parameter of this one family. A half-cosine ramp in
log f has only two parameters, its knees. The ruling fixes the upper knee and Gravity takes the lower knee, so `curve`
must act as a **warp of the ramp coordinate**. The warp keeps both knees, monotonicity and continuity:

```
u  = log2(f)                         (f = (k+1)·f0)
uL = log2(f_c,s) − kLowerKneeMaxOctaves · (1 − G)      lower knee: 0.25 oct below f_c at G = 0, at f_c at G = 1
uH = log2(f_c,s) + kUpperKneeOctaves                    upper knee: 2 oct above f_c (fixed factor 4)
x  = clamp((u − uL) / (uH − uL), 0, 1)
γ  = 4^curve                                            curve 0 → γ = 1 (the plain raised cosine), 1 → γ = 4
c  = 0.5 − 0.5 · cos(π · x^γ)
```

Why these numbers (model S12.3):
- At Gravity 0 and γ = 1, c(f_c) = 0.030. Any partial below f_c therefore sits at |p| ≤ 0.030. Its own correlation is
  cos(π·0.015) = 0.9989 ≥ 0.99 and its own mono-sum ratio is −0.0024 dB.
- SC-001(a) and SC-002 at the generator output (SC-001(c), SC-002's second clause) therefore hold **by construction for
  any spectrum**, at every Gravity, curve and crossover.
- γ < 1 was rejected. At γ = 0.5, c(f_c) reaches 0.25, and a strong partial just below f_c could then push ρ under 0.99.
- Gravity steepens the ramp from 1/2.25 to 1/2 per octave and moves the knee up to f_c. The safety stage carries the
  rest of Gravity's effect.

The default is `curve = 0.5` (γ = 2).

### C-6 (refinement, FR-015): Δp_max = 0.003 per control interval, derived

For a constant-power pan, |∂(g_L, g_R)/∂p| = π/4 at every p (`angle = π/4 + p·π/4`, bank:655–657). `applyPanOffsets`
writes a new vector at once, so a per-interval step Δp held for one interval leaves a zero-mean sawtooth residual. Its
power relative to the partial's own power is (π/4)² · Δp² / 12, independent of the pan position and of the hold length.
Summed over partials, the ratio relative to the total is the same or lower, because h1 never moves.

−60 dB needs Δp ≤ 0.0044. The plan takes `kMaxPanStepPerInterval = 0.003f`, which gives −63.4 dB, a 3.4 dB margin
(model S12.2). Consequences, recorded:
- A full-range Spread or mode change slews in ⌈1/0.003⌉ = 334 intervals: 0.22 s at 48 kHz, 0.24 s at 44.1 kHz.
- Cycle at the 1 Hz bar, depth 1 and κ = 0.5 moves p by at most π·32/fs per interval. That is 2.28e-3 at 44.1 kHz, below
  0.003, so the slew never bends Cycle at any supported rate.
- Drift's per-interval change can exceed 0.003 only in its fastest moves (its 150 ms output smoother bounds it,
  brownian_drift.h:41–59). There the slew binds and the bar still holds.
- A 48 st Drop slews where a partial crosses the ramp. The lag converges once the Drop ends.

### C-7 (refinement, FR-001, FR-074): the call order is an explicit `advanceControl()`

`advanceControl()` advances the field's global control-rate state by one 32-sample interval. The owner calls it at every
absolute sample index that is a multiple of `kControlInterval`. The call comes **before** that interval's voice law
queries and before `processBlock` runs on that interval's samples.

`processBlock` itself has no grid. It works per sample, and its per-sample smoothers move toward targets that only
`advanceControl()` sets. Each interval therefore sees one set of values in the voices and in the stage (SC-009(c),
SC-012), and partition invariance follows from the owner's grid (FR-074). Phase 9's engine loop and the test fixtures
both use `gridDriver` (mass_dynamics_test_helpers.h:103).

### C-8 (refinement, FR-001): the exact law query signature takes the partial count

`void computePanVector(VoiceState& state, float f0Hz, std::array<float, kMaxPartials>& out, int numPartials = kDefaultPartials) const noexcept`.

The spec's drafted shape has no N, but FR-010 needs one ("entries k ≥ N are 0"). The field keeps its own
`kDefaultPartials = 64` and `kControlInterval = 32` and **does not include `profundum_core.h`**. The test TUs
`static_assert` that both equal `ProfundumCore`'s values (profundum_core.h:96–97).

### C-9 (refinement; the synced fold RATIFIED 2026-10-18 as option (A), FR-002, FR-031, FR-035, FR-036): MOTION rate units and the synced bound

**Rate.** `motionRate` is normalised to [0, 1] and is one control for both sources, so Phase 8 can interpolate it
linearly.
- Cycle: f = kCycleMinHz · (kCycleMaxHz / kCycleMinHz)^rate, i.e. 0.05 … 1.0 Hz.
- Drift: τ = kDriftTauMaxSec · (kDriftTauMinSec / kDriftTauMaxSec)^rate, i.e. 30 … 0.5 s. It is passed as
  `setSmoothness((τ − 0.2) / 29.8)`, the inverse of brownian_drift.h:231–234.
- The rate ratios between the extremes are 20 (Cycle) and 60 (Drift), both ≥ SC-010's 4.

**Synced Cycle bound.** `LFO`'s synced rate is (bpm/60)/beats (lfo.h:488–498), and nothing bounds it by our 1 Hz bar: a
1/1 note at 300 BPM is 1.25 Hz.
- The field keeps sync *through* `setTempoSync`, `setNoteValue` and `setTempo` (FR-031), but passes the LFO an
  **octave-folded tempo**: bpm_eff = bpm / 2^j, with the smallest j ≥ 0 such that (bpm_eff/60)/beats ≤ kCycleMaxHz.
- Halving the tempo doubles the cycle length, so the cycle still lands on bar lines; it simply becomes a longer synced
  value.
- The fold loop is bounded at ≤ 16 iterations (the shortest note value at 999 BPM needs 9).
- Tempo is clamped to [kMinTempoSyncBPM, kMaxTempoSyncBPM] = [20, 300] (note_value.h:200–203).
- Every synced setting is therefore ≤ 1 Hz.

**What the fold costs, stated plainly.** FR-036 says the synced Cycle "follows the tempo and note value". Under the fold,
every note value whose unfolded rate exceeds 1 Hz lands on a rate in (0.5, 1] Hz, so several note values alias onto the
same rate. At 120 BPM, 1/2, 1/4, 1/8, 1/16 and 1/32 all give 1 Hz. SC-008's "tempo-derived rate" is then read as the
folded rate. Nothing in the spec rules this, so the fold goes to the main loop against the alternative:
- **(A) Fold (recommended, built).** Every exposed note value stays selectable and always obeys the 1 Hz bar; fast values
  alias. The aliasing is stated in the Phase 11 and Phase 12 handoffs.
- **(B) Restrict.** Expose only note values whose rate is ≤ 1 Hz at the top of the tempo range: beats ≥ 5 at 300 BPM
  (1/1 dotted and longer), or beats ≥ 10/3 at the 200 BPM of SC-008 (1/1 and longer). No fold, no aliasing, a short list,
  and the Phase 11 dropdown is a subset of `kNoteValueDropdownMapping`. A tempo above the list's design point would
  still need a clamp or a fold.

Either way SC-008 adds a record, `evidence/sc008-sync-fold.csv`: the unfolded and the folded rate of every note value and
modifier at 120 and 200 BPM, so the aliasing is visible in the compliance evidence.

**LFO clocking.** The LFO is prepared at the control rate fs / 32 and `process()`ed once per interval, so one call gives
exactly the interval's value. It runs Sine only (FR-031), so `reset()` is deterministic (lfo.h:95–107).

**Both sources advance every interval**, whichever is selected, so switching sources never re-seeds or rewinds either.

### C-10 (refinement RATIFIED 2026-10-18, FR-040, FR-041): WIDE offset signs

`wideOffsets(a) = { +K, −K }`, with K = ceiling(a) · Spread · K_wide. Amount goes up on L and down on R, and Tone goes the
other way, so L is hotter and darker and R cooler and brighter. The two channels' level and brightness changes then
partly offset each other. |offset| = K ≤ ceiling holds by construction (K_wide ≤ 1, Spread ≤ 1). One constant serves
both offsets.

### C-11 (refinement, FR-021): the side-gain law and seeds

g_side(G) = kSideFloorGain · (1 − G).
- g(0) = kSideFloorGain ∈ (0, 1).
- g(1) = 0 **exactly**: the `OnePoleSmoother` snaps to its target within 1e-4 (smoother.h:199–201), so the per-sample
  gain lands on 0.0f.
- g is strictly decreasing between the two.

Seeds: kSideFloorGain = 0.5 (−6 dB) and K_wide (`kWideOffsetScale`) = 1.0. Both are harvested (S9).

### C-12 (refinement, FR-070): the Phase 7 chain composes the Phase 6 helpers without modifying their header

`processChainTail` (mass_dynamics_test_helpers.h:302–340) has no insertion point between `MassDynamics` and the DC
blockers, and `renderChain` has no ShapeMorph or character stage. Adding hooks would *modify* a shared test header with
seven consumer TUs, three of them `[long]` grid suites. The change-set rule (root CLAUDE.md, "Run only the tests the
change set can reach") would then pull all of them into the Phase 7 lanes.

The plan instead writes `renderFieldChain` in the new `bass_stereo_field_test_helpers.h`. It is built from the Phase 6
inline pieces, unchanged: `gridDriver`, `ChainConfig`, `allocateTaps`, `chainTargets`, `prepareChainLaw`,
`coreControlsFor`, `anchorControlsFor`, `unbiasedH1Amplitude` and `makeChainDynamics`. Only the per-run body is new
(about 70 lines: the extra stages and the stage tap).

A helper self-test, `BassStereoField_ChainMatchesPhase6`, sets the field to bypass, ShapeMorph and the character stage
off, and Fold from the law. In that configuration it proves that `renderFieldChain`'s limiter output equals
`renderChain`'s bit for bit, so nothing in the Phase 6 chain is re-derived.

### C-13 ⚠ RISK (SC-001 / SC-002 at Gravity 0 on the WIDE dirty arm): the harvest may force a low floor

Phase 5 measured Q = −0.56 … −0.61 dB below 2·f₀ for Rectify and Fracture at offset 0.10
(sc013-offset-ceilings.txt). Q = M²/(M² + S²), so S²/M² there is about −8.3 dB. Whether that fails SC-002 depends on
what the side content is:
- If it is uncorrelated with M, then ρ = (M² − S²)/(M² + S²) ≈ 0.74. ρ ≥ 0.99 then needs S²/M² ≤ −23 dB, i.e. at least
  15 dB of stage attenuation at Gravity 0 (kSideFloorGain ≤ 0.18).
- If it is mostly a level difference of the same waveform, ρ stays near 1, because Pearson correlation is scale-invariant.
  The 0.5 seed then passes.

The fixtures decide. The harvest (S9) searches K_wide first, which keeps the floor as high as possible and the Gravity
lever audible, and then the floor.

**Escalation:** if no pair with kSideFloorGain ≥ 0.1 and K_wide ≥ 0.5 passes SC-001, SC-002 and SC-010 together, the
build stops and asks the user rather than shipping a quietly relaxed value. The options to put to the user: lower K_wide
further, accept a lower floor, or re-read SC-002 on the WIDE arm.

**It happened (2026-10-18, ruled G8-1).** The seed floor 0.5 passed 3 / 7 / 11 of 144 WIDE cells at K_wide 1 / 0.75 / 0.5,
and the per-cell data showed the floor, not K_wide, is the lever (the SC-010 WIDE lever fails at K_wide 0.5). The
measured need: −7 … −13 dB for most cells, −16 … −24 dB for Crush / Rectify / Bite, and three anti-phase-fundamental
cells at −30 … −41 dB. SC-001(b)'s −3.11 dB octave bar failed on 78 cells and on the SPREAD dirty arm too (random-phase
cross products of a per-channel nonlinearity after the parity pan). Ruling: K_wide ∈ {1, 0.75} chosen on SC-007 and the
SC-010 lever (mono sum recorded); the floor candidates extend to {…, 0.071, 0.05, 0.035, 0.03} with the escalation floor
at 0.03; the anti-phase cells (character-tap ρ_pre < 0) are recorded; the octave Q is gated on the clean core only and
recorded on the nonlinear arms; a band is read only above −60 dBFS.

### C-14 (refinement, SC-019): the phase check skips one-sided harmonics

At p = ±1, one pan gain is `cos(π/2)` in float = −4.4e-8 (−147 dB). Its phase is meaningless, and flipped.
SC-019's "phase on L minus phase on R within 0.01 rad" is therefore read only for harmonics whose weaker channel is within
60 dB of the stronger one. The skipped harmonics are counted and printed.

### C-15 (refinement RATIFIED 2026-10-18, FR-011 with FR-015 / FR-052): SOLID's zero vector is reached by slew

FR-011 says "in SOLID at any Spread, every entry is exactly 0.0f". Under FR-015's slew that holds for the law's
**target** at once and for the emitted vector only after the slew. The plan builds the slewed behaviour and proposes the
FR-011 text: "In SOLID and at Spread 0 the law's target is exactly 0.0f; the emitted vector reaches exactly 0.0f within
⌈1/kMaxPanStepPerInterval⌉ = 334 control intervals of the switch and stays there".
- A render that starts there emits exact zeros from the first interval.
- A switch into SOLID slews the emitted vector to exact 0.0f within 334 intervals; the slew lands exactly on the target
  once it is within one step (S4.3). Meanwhile the safety stage already gives L == R from the end of its 10 ms crossfade
  (FR-052).
- The handoff after the landing is the all-zero vector, so the core restores the exact centre tables
  (profundum_core.h:614–625).
- **Gate** (`BassStereoField_SolidZeroVector`, S13.3): from Spread 1 at every n with the largest |p| (= 1), switch to
  SOLID, and from SPREAD set Spread 0. Every entry of the emitted vector is bit-zero (`std::bit_cast<std::uint32_t>` = 0,
  so −0 fails) at the 334th query after the switch's `advanceControl()` and at every query after it. A start in SOLID is
  bit-zero from the first query. The landing interval is printed. The float accumulation of 333 steps of Δ is far
  below the last step's 0.001 remainder, so 334 is exact.

### C-16 (refinement, SC-009(a)): how the held and interpolated references get their oscillators

`renderReference` (profundum_core_test_helpers.h:498) renders a mono centre-pan sum, so it cannot pan per partial. The
zipper residual (held − interpolated) depends only on the pan-gain difference times the partial signal, so second-order
errors in the oscillator model barely move it. The Phase 7 reference is built as follows:
- **Oscillators.** Each harmonic is synthesised in double as a_n · sin(φ_n + 2π n ∫f₀). (a_n, φ_n) are fitted by complex
  Goertzel at n·f₀ over whole periods of a settled Spread 0 render of the same note (the mono product, with the reset
  seeds).
- **Held reference.** It applies the product's per-interval vector for the whole interval.
- **Interpolated reference.** It interpolates p linearly between interval centres and computes cos / sin per sample.
- **Validity arm.** On a static note, product − held must be ≤ −60 dB relative to the total on each channel. This proves
  the fit reproduces the bank before any residual is trusted.

### C-17 RULED 2026-10-18 (recommended reading; SC-021, the interpolation bar): mixed control pairs cannot keep W_n between their endpoints

SC-021 gates "the linear interpolation of two valid Controls at t ∈ {0.25, 0.5, 0.75}: … its W_n lies between the two
endpoints' W_n within 0.5 dB". c is monotone in each control separately (S4.2), but not jointly along a line that moves
two controls in opposite width directions. Counter-example: endpoints (Spread 0, crossover 60 Hz) and (Spread 1,
crossover 250 Hz). A partial in the 60 Hz ramp but below the 250 Hz lower knee has W_n at the −120 dB floor at both
endpoints (Spread 0 at the first, c = 0 at the second), while the midpoint (Spread 0.5, crossover 155 Hz) gives it a
non-zero c, so W_n rises far above both. No law of this family meets the bar on such pairs.

**Recommended ruling:** SC-021's bar applies to pairs that move one control, or several controls in the same width
direction (Spread up, Gravity down, crossover down, curve down); on mixed pairs the interpolated set is valid, the vector
finite and ≤ 1, and the W_n path is continuous (largest change per 0.05 of t recorded), but W_n is not bounded by the
endpoints. The mixed-pair excursion beyond the endpoints is printed.

Test switch: `kGateMixedInterpolationPairs`, **ruled → `false`**: mixed pairs are gated on validity, finiteness, |p| ≤ 1 and continuity, with their excursion printed; single-control and same-direction pairs keep the 0.5 dB bar. (`true` was the spec as written.)

### C-18 (refinement RATIFIED 2026-10-18, FR-024, SC-009(c)): the stage keeps `Efficient` tracking; the corner's bound is stated

FR-024 asks the plan to verify "the tracking mode this needs". Both modes were read (crossover_filter.h:298–311):
- **Efficient** recomputes when the smoothed value has moved ≥ 0.1 Hz from the last coefficient frequency. The
  coefficient corner therefore trails the smoothed value by < 0.1 Hz, always: the error accumulates against
  `lastCoefficientFreq_` until it triggers.
- **HighAccuracy** recomputes only while `!isComplete()`, tested after `process()`. A handoff step the smoother snaps in
  one call (< 1e-4 Hz off, smoother.h:199–201) never reaches the coefficients. A slow automation that moves the stage
  target by < 1e-4 Hz per interval (< 0.075 Hz/s at 48 kHz) is a run of such steps, so the corner can drift from the
  handed value **without bound**. It also recomputes on every sample of a smoothing run, which costs more (S12.6).

So the stage keeps `Efficient`, and the plan states the combined bound instead of relying on `getCrossoverFrequency()`
alone: one interval after a handoff, the coefficient corner is within **ε_s(fs) + 0.1 Hz** of the handed value, where
ε_s(fs) is the smoother residual after 32 samples (S12.4: 6.7e-14 Hz at 44.1 kHz, 1.3e-12 Hz at 48 kHz, 2.2e-5 Hz at
96 kHz, 0.091 Hz at 192 kHz). Once the smoother has settled, the corner is within 0.1 Hz. Worst case 0.191 Hz at
192 kHz, 0.16 % of the lowest f_s = 120 Hz, which moves the LR4 high band at f_c by ≤ 0.06 dB against SC-006's 0.6 dB
margin (S12.1).

**Proposed FR-024 / SC-009(c) text:** "the stage's smoother settles on the handed value within 0.1 Hz inside one control
interval; its coefficient corner is within that residual plus the 0.1 Hz `Efficient` hysteresis". SC-009(c) checks it by
computation at every rate (S13.3).

---

## S2. Architecture

```
 per voice (Phase 9; test fixture here), every 32-sample interval j:
   field.advanceControl()                 ← once per interval, globally, before any voice
   field.computePanVector(vs[v], f0_v, pan, N)   → core_v.setPartialPanOffsets(pan)
   field.wideOffsets(alg_v)               → character_v.setChannelOffsets(a, t)
   core_v → ShapeMorph → HarmonicFold → BassCharacterStage (+ AnchorOscillator) ─┐
                                                                                   Σ voices
   MassDynamics ─→ field.processBlock(L, R, n)  [safety stage]  ─→ DCBlocker ×2 ─→ TruePeakLimiter
```

`BassStereoField` (L3, `systems/bass_stereo_field.h`, header-only, `Krate::DSP`) is one class with two halves:
- a `const` per-voice law that keeps no state of its own (the per-voice memory is the caller's `VoiceState`);
- a global, stateful safety stage.

Includes:

| Include | Layer | For |
|---|---|---|
| `core/db_utils.h` | 0 | `detail::isFinite` |
| `core/modulation_source.h` | 0 | `MotionOutput` |
| `core/note_value.h` | 0 | `NoteValue`, `NoteModifier`, `getBeatsForNote`, tempo bounds |
| `primitives/smoother.h` | 1 | `OnePoleSmoother`, `LinearRamp`, `kMinSmoothingTimeMs` |
| `primitives/lfo.h` | 1 | Cycle |
| `processors/brownian_drift.h` | 2 | Drift |
| `processors/crossover_filter.h` | 2 | the side split |
| `processors/harmonic_types.h` | 2 | `kMaxPartials` |
| `systems/bass_character_stage.h` | 3 (peer) | `Algorithm` and `kMaxChannelOffset` only (spec "Layer discipline") |

There is no L4 include, no `profundum_core.h` (C-8) and no `midside_processor.h` (C-3: its formulas are cited in a
comment).

---

## S3. `BassStereoField` public API (FR-001–FR-004)

```cpp
namespace Krate::DSP {

class BassStereoField {
public:
    enum class ImageMode : std::uint8_t { Solid, Spread, Wide };            // FR-002, nested (ODR row)
    enum class MotionSource : std::uint8_t { Drift, Cycle };                // FR-031, nested

    // ---- constants (S9 lists seeds and harvests) ----
    static constexpr std::size_t kControlInterval = 32;          // == ProfundumCore / MassDynamics (static_assert in tests)
    static constexpr int kDefaultPartials = 64;                  // == ProfundumCore::kDefaultPartials
    static constexpr float kMinCrossoverHz = 60.0f;              // Q5
    static constexpr float kMaxCrossoverHz = 250.0f;
    static constexpr float kDefaultCrossoverHz = 120.0f;
    static constexpr float kStageCrossoverMultiple = 2.0f;       // Q3, FR-024
    static constexpr float kUpperKneeOctaves = 2.0f;             // C-5
    static constexpr float kLowerKneeMaxOctaves = 0.25f;         // C-5
    static constexpr float kMaxCurveExponent = 4.0f;             // C-5: γ = 4^curve
    static constexpr float kMaxPanStepPerInterval = 0.003f;      // C-6
    static constexpr float kMotionExcursion = 0.5f;              // κ, Q7
    static constexpr float kCycleMinHz = 0.05f;                  // C-9
    static constexpr float kCycleMaxHz = 1.0f;                   // SC-008 bar
    static constexpr float kDriftTauMaxSec = 30.0f;              // SC-008 bar
    static constexpr float kDriftTauMinSec = 0.5f;               // SC-008 bar
    static constexpr float kDefaultTempoBpm = 120.0f;
    static constexpr float kSideFloorGain = 0.5f;                // g_floor, seed (C-11, harvested S9.1)
    static constexpr float kWideOffsetScale = 1.0f;              // K_wide, seed (C-11, harvested S9.1)
    static constexpr float kWideCeilingRectifyFracture = 0.10f;  // Phase 5 handoff
    static constexpr float kSideGainSmoothMs = 10.0f;            // FR-027 (MidSideProcessor's 10 ms)
    static constexpr float kModeCrossfadeMs = 10.0f;             // FR-052, D-6
    static constexpr float kCrossoverSmoothMs = 20.0f;           // law f_c smoothing (control rate)
    static constexpr float kStageCrossoverSmoothMs = kMinSmoothingTimeMs;   // 0.1 ms (FR-024, S7.2)
    static constexpr float kDefaultF0Hz = 55.0f;
    static constexpr std::uint32_t kDefaultDriftSeed = BrownianDrift::kDefaultDriftSeed;

    struct Controls {                                            // FR-002: plain aggregate
        ImageMode mode = ImageMode::Spread;
        float spread = 0.5f;            // [0, 1]
        float gravity = 0.5f;           // [0, 1]
        float crossoverHz = kDefaultCrossoverHz;   // [60, 250]
        float curve = 0.5f;             // [0, 1], continuous (Q5)
        float motionDepth = 0.0f;       // [0, 1]
        float motionRate = 0.3f;        // [0, 1] (C-9)
        MotionSource motionSource = MotionSource::Drift;
        bool cycleSync = false;
        NoteValue cycleNote = NoteValue::Whole;
        NoteModifier cycleModifier = NoteModifier::None;
    };

    struct VoiceState {                                          // FR-001: one per voice, caller-owned
        std::array<float, kMaxPartials> previous{};              // last emitted vector (FR-015)
        float lastF0Hz = kDefaultF0Hz;                           // used when a query's f0 is not valid
        float lastCrossoverHz = 0.0f;                            // f_c,s the last query used (SC-009(c))
        std::uint64_t handoffCount = 0;                          // FR-016 / SC-013(c)
        bool primed = false;                                     // false → next query lands on target (Reset exemption)
        void reset() noexcept { previous.fill(0.0f); lastF0Hz = kDefaultF0Hz; lastCrossoverHz = 0.0f; primed = false; }
        // handoffCount survives reset(): it is a diagnostic counter (the caller zeroes it).
    };

    struct ChannelOffsets { float amount = 0.0f; float tone = 0.0f; };   // FR-040

    class MotionOutput final : public ModulationSource {         // FR-034
    public:
        [[nodiscard]] float getCurrentValue() const noexcept override { return value_; }
        [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }
    private:
        friend class BassStereoField;
        float value_ = 0.0f;
    };

    BassStereoField() noexcept;                                  // fills log2Harmonic_[k] = log2(k + 1)
    BassStereoField(const BassStereoField&) = delete;            // CrossoverLR4 / LFO are non-copyable
    BassStereoField& operator=(const BassStereoField&) = delete;

    // ---- lifecycle ----
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;   // not RT-safe (FR-001); maxBlockSize unused (no scratch, S7)
    void reset() noexcept;                                                // RT-safe (C-4)

    // ---- controls ----
    void setControls(const Controls& c) noexcept;     // clamps; non-finite field keeps previous; latched until advanceControl
    [[nodiscard]] const Controls& controls() const noexcept;      // the latched (pending) set
    [[nodiscard]] static bool isValidControls(const Controls& c) noexcept;   // FR-003
    void setTempoBpm(float bpm) noexcept;              // FR-036: non-finite or <= 0 keeps previous; clamp [20, 300]
    void setDriftSeed(std::uint32_t seed) noexcept;    // FR-033; restored by reset()

    // ---- control rate (C-7) ----
    void advanceControl() noexcept;

    // ---- per-voice law (FR-010–FR-019, FR-030) ----
    void computePanVector(VoiceState& state, float f0Hz, std::array<float, kMaxPartials>& out,
                          int numPartials = kDefaultPartials) const noexcept;
    [[nodiscard]] float widthCurve(float frequencyHz) const noexcept;   // c(f) of the current interval (tests, Phase 12)

    // ---- WIDE (FR-040–FR-044) ----
    [[nodiscard]] ChannelOffsets wideOffsets(BassCharacterStage::Algorithm a) const noexcept;
    [[nodiscard]] static float wideCeiling(BassCharacterStage::Algorithm a) noexcept;

    // ---- safety stage (FR-020–FR-028) ----
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept;
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }
    [[nodiscard]] static constexpr float sideGainForGravity(float gravity) noexcept;   // kSideFloorGain · (1 − g)

    // ---- observers ----
    [[nodiscard]] float smoothedCrossoverHz() const noexcept;   // f_c,s (FR-024)
    [[nodiscard]] float stageCrossoverHz() const noexcept;      // crossover_.getCrossoverFrequency() (the handed target)
    [[nodiscard]] float motionValue() const noexcept;           // m, [-1, 1]
    [[nodiscard]] const ModulationSource& motionSource() const noexcept { return motionOutput_; }
    [[nodiscard]] float modeFade() const noexcept;              // k, the side crossfade position
    [[nodiscard]] float currentSideGain() const noexcept;       // g_side, smoothed
    [[nodiscard]] bool sideIdle() const noexcept;               // SOLID settled
    [[nodiscard]] float driftTauSeconds() const noexcept;       // the τ configured (SC-008)
    [[nodiscard]] float cycleFrequencyHz() const noexcept;      // after the octave fold (SC-008)
};

}  // namespace Krate::DSP
```

**State layout.** Every member is fixed-size; there is no heap.
- Controls: `pending_` and `applied_`, plus `tempoBpm_`, `driftSeed_`, `double sampleRate_ = 44100.0` (the same
  default as `CrossoverLR4`, crossover_filter.h:382) and `bool prepared_ = false`.
- `log2Harmonic_` (`std::array<float, kMaxPartials>`).
- The per-interval law cache: `uLow_`, `invSpan_`, `gamma_`, and `magnitudeScale_` (= Spread · (1 − κμ + κμ·m), or 0 in
  SOLID).
- The law crossover: `fcSmoother_` (`OnePoleSmoother` at control rate) and `fcS_`.
- MOTION: `drift_` (`BrownianDrift`), `lfo_` (`LFO`), `cycleValue_`, `m_` and `motionOutput_`.
- The stage: `crossover_` (`CrossoverLR4`), `sideGainSm_` (`OnePoleSmoother`), `modeRamp_` (`LinearRamp`) and
  `sideIdle_`.
- Change caches (`lastRate_`, `lastSync_`, …) so the setters on the reused sources run only when a value changes.

**`setControls`** (RT-safe):
- a non-finite float field keeps its previous value (`detail::isFinite`);
- floats clamp to their ranges;
- an out-of-range enum value keeps the previous one.

The result goes to `pending_`. Nothing audible changes until `advanceControl()` (FR-074).

**`isValidControls`** returns true iff every float is finite and inside its closed range ([0, 1], except the crossover at
[60, 250]) and every enum is a declared value. Because the ranges are boxes, a linear interpolation of two valid sets is
valid (SC-021).

**`prepare(fs, maxBlock)`**, in order:
1. `sampleRate_ = fs`, `applied_ = pending_` and `prepared_ = true`.
2. `fcSmoother_.configure(kCrossoverSmoothMs, fs / 32)` and `snapTo(applied_.crossoverHz)`.
3. The crossover: `setSmoothingTime(kStageCrossoverSmoothMs)`, `setTrackingMode(Efficient)`,
   `setCrossoverFrequency(2·f_c)`, then `prepare(fs)`.
4. Drift: `prepare(fs)`, `setSeed(driftSeed_)`, `setDepth(1)`, `setMean(0)`, and the smoothness from the rate.
5. Cycle: `lfo_.prepare(fs / 32)`, Sine, and the frequency or sync from the rate and tempo.
6. `sideGainSm_.configure(10 ms, fs)` and `modeRamp_.configure(10 ms, fs)`.
7. `reset()`.

Re-preparing at another rate and back leaves no trace (SC-016): every rate-derived value is recomputed and every state is
re-initialised.

**`reset()`** (FR-028), in order:
1. `applied_ = pending_`.
2. Snap `fcSmoother_` to the crossover control, and snap the stage crossover as in C-4 **only if `prepared_`**. Before
   the first `prepare()` the crossover is left unprepared, so `CrossoverLR4::prepare` never runs with an unset rate
   (crossover_filter.h:349–352), and its `process` returns {0, 0} until `prepare()` (290–292).
3. `drift_.reset()` (which re-seeds, brownian_drift.h:133) and `lfo_.reset()`.
4. `sideGainSm_.snapTo(g_side(G))` and `modeRamp_.snapTo(solid ? 0 : 1)`.
5. `sideIdle_ = solid` and `m_ = 0`.
6. Recompute the law cache.

After `reset()` the next `advanceControl()` is the first interval.

---

## S4. The width law (FR-010–FR-019)

### S4.1 Per-interval precompute (inside `advanceControl`)

f_c,s is the smoothed law crossover, G the gravity, u the curve, μ the depth and m the selected MOTION value:

```
uLow_   = log2(f_c,s) − 0.25·(1 − G)
uHigh   = log2(f_c,s) + 2
invSpan_ = 1 / (uHigh − uLow_)          ∈ [1/2.25, 1/2]
gamma_  = exp2(2·u)                     (= 4^u)
magnitudeScale_ = (mode == Solid) ? 0 : Spread · (1 − κμ + κμ·m)      κ = 0.5
```

### S4.2 Per partial (inside `computePanVector`; k = 1 … N−1, while k = 0 and k ≥ N give 0)

```
x  = (log2(f0) + log2Harmonic_[k] − uLow_) · invSpan_       (no per-partial log: log2(k+1) is a table)
c  = x <= 0 ? 0 : x >= 1 ? 1 : 0.5 − 0.5·cos(π · exp2(gamma_ · log2(x)))
s  = (k % 2 == 0) ? −1 : +1             (k even = odd harmonic = left, bank:1155–1156)
target_k = s · magnitudeScale_ · c
```

Properties, each with the test that reads it:
- **h1 centred** (FR-013): k = 0 is never computed, and `out[0] = 0.0f` (SC-018, SC-019).
- **Monotone in f** (FR-012): x increases with f, and both x^γ and the half-cosine are increasing on [0, 1] (SC-003(a)).
- **Monotone in Spread** at μ = 0 (SC-003(b)).
- **Non-increasing in Gravity and in curve** (SC-003(c)): uLow_ rises with G, so x falls; γ rises with curve, so x^γ
  falls for x ∈ (0, 1).
- **Continuous** in every control and in f (SC-021): S4.1 is continuous, and the clamps meet the ramp at its end values.
- **|p| ≤ 1 without the bank clamp** (FR-018): c ≤ 1, Spread ≤ 1, and 1 − κμ + κμ·m ∈ [1 − 2κμ, 1] ⊂ [0, 1] (Q7: κ ≤ 0.5).
- **No side swap** (FR-014, SC-008, SC-019): the factor (1 − κμ + κμ·m) is ≥ 0, so sign(target) is s or the target is 0.
- **Pan only** (FR-014): the vector is the only output, and nothing else reaches the bank.
- **Generator sub band** (SC-001(c), SC-002): below f_c, c ≤ c(f_c; G = 0, γ = 1) = 0.0302 (C-5, S12.3).
- **Width at n·f₀ ≥ 4·f_c is exactly Spread** (c = 1), so Gravity cannot move W_n there at the generator (SC-003(c)).
- **Sample-rate independence** (SC-016): the law works in Hz, so W_n does not depend on fs.
- **f₀ guard:** a non-finite or non-positive f0Hz uses `state.lastF0Hz`; a valid one is stored. log2(f0) is computed once
  per query.

### S4.3 Slew and handoff count (FR-015, FR-016), in `computePanVector`

```
if (!state.primed) { out = target; state.primed = true; }                         // Reset exemption
else for each k: d = target_k − previous_k;
                 out_k = |d| <= Δ ? target_k : previous_k + copysign(Δ, d);      // lands exactly on the target
if (any out_k bit-differs from previous_k) ++state.handoffCount;                   // FR-016 (std::bit_cast compare)
state.previous = out; state.lastCrossoverHz = fcS_;
```

Δ is `kMaxPanStepPerInterval`.
- **No zero crossing.** The previous value and the target share sign s or are 0, and the step stops at the target, so the
  slew never crosses 0.
- **Settling.** A static note at constant controls with μ = 0 reaches its target and from then on emits a bit-identical
  vector. The handoff counter then stops (SC-013(c)), and the core sets nothing dirty (profundum_core.h:222–234).
- **Cost.** One log2 per query, plus one log2, one exp2 and one cos for each partial inside the ramp. That is typically
  5–15 partials, because the ramp is 2–2.25 octaves wide.

### S4.4 Excitation interaction (FR-019)

Nothing in the law changes. Once the vector is non-zero, the core's excitation runs per channel
(profundum_core.h:1077–1099), and whatever it spreads below f_c is the stage's to catch (SC-006 arm (ii)).

---

## S5. MOTION (FR-030–FR-036)

**Advance.** Inside `advanceControl()`, both sources advance every interval:
1. `drift_.processBlock(kControlInterval)`, then `driftValue = drift_.getCurrentValue()` (∈ [−1, 1]).
2. `cycleValue_ = lfo_.process()` (the LFO is prepared at fs/32 and runs Sine).
3. `m_ = (source == Drift) ? driftValue : cycleValue_`, and `motionOutput_.value_ = m_`.

**Rate changes.** On a change of `motionRate`:
- Drift: τ = 30·(0.5/30)^rate, passed as `setSmoothness((τ − 0.2)/29.8)`. The smoothness always stays inside
  [0.0101, 1], so τ ∈ [0.5, 30] s and `kTauMin = 0.2` is never reached (SC-008).
- Cycle: f = 0.05·20^rate, passed as `setFrequency(f)`.

**Sync changes.** On a change of sync, note value, modifier or tempo, the field calls `setTempoSync`, `setNoteValue` and
`setTempo(bpm_eff)`, with bpm_eff from the C-9 octave fold.

**The moving vector** is FR-030's `p_k(t) = s_k · P_k · (1 − κμ + κμ·m(t))` (S4.1 `magnitudeScale_`), then slewed (S4.3).
MOTION never rotates M/S (FR-032; D-4 adopted as drafted). Only the pan vector moves, so SC-001 holds with MOTION at
maximum.

**Exposure:** `motionValue()` and `motionSource()`, a `ModulationSource` (FR-034). The field routes nothing itself.

---

## S6. WIDE offsets (FR-040–FR-044)

```
wideCeiling(a) = (a == Rectify || a == Fracture) ? 0.10f : BassCharacterStage::kMaxChannelOffset   // 0.15
wideOffsets(a) = (applied_.mode != Wide || applied_.spread == 0) ? {0, 0}
               : { +K, −K },  K = wideCeiling(a) · applied_.spread · kWideOffsetScale             // C-10
```

`wideOffsets` is `const` and pure, and it reads only the applied controls, so a voice's offsets change on the grid.
**G7-2 (ruled 2026-10-18):** K is also multiplied by `wideEngage_`, a scalar `advanceControl()` slews toward 1 in WIDE
and toward 0 otherwise by kMaxPanStepPerInterval per interval (exact landing within 334 intervals, the FR-015 slew
length), so a SPREAD ↔ WIDE step is as slow as a pan slew and the Phase 6 shaper sees no attack; `wideOffsets` returns
{0, 0} while the scalar is 0. `prepare()` / `reset()` snap it to the applied mode.
- **FR-042.** On an algorithm switch the caller passes the new algorithm's offsets. The ceiling is applied before they
  are returned, and the stage's own 10 ms smoothing (bass_character_stage.h:84) carries the change.
- **FR-043.** Leaving WIDE returns {0, 0}. The stage stays unlinked until its `reset()`, so the fixture's SOLID
  bit-identity cases either reset or start in SOLID.
- **FR-044.** `computePanVector` reads the mode only to zero the vector in SOLID, so WIDE and SPREAD give the same vector
  bit for bit (SC-017).

---

## S7. The safety stage (FR-020–FR-028, FR-051, FR-052, FR-072)

### S7.1 Per-interval targets (inside `advanceControl`)

- `crossover_.setCrossoverFrequency(kStageCrossoverMultiple · fcS_)`: f_s = 2·f_c,s ∈ [120, 500] Hz, which is
  ≤ 0.45·44100.
- `sideGainSm_.setTarget(sideGainForGravity(applied_.gravity))`, only on a change (cheap either way: it is a one-pole
  target).
- `modeRamp_.setTarget(applied_.mode == Solid ? 0 : 1)`, **only when the target changes**. `LinearRamp::setTarget`
  recomputes the increment from the remaining distance (smoother.h:342–354), so re-setting it every interval would
  stretch the ramp into a decaying curve.
- Leaving SOLID sets `sideIdle_ = false`. The ramp starts at 0 and the crossover state is zero, so the crossover's
  start-up is faded in.

### S7.2 Crossover smoothing (FR-024)

- **Law crossover.** f_c,s is smoothed at `kCrossoverSmoothMs = 20 ms` on the control-rate grid (fs/32).
- **Stage crossover.** Each interval hands 2·f_c,s to the crossover, whose own smoother runs at
  `kMinSmoothingTimeMs = 0.1 ms`, the smallest time `configure` accepts (smoother.h:58, 82).
- **Settling.** A full-range handoff from 120 to 500 Hz then settles within 0.1 Hz inside one interval at every supported
  rate. The residual is 380·e^(−5000·32/(0.1·fs)): from 6.7e-14 Hz at 44.1 kHz up to 0.091 Hz at 192 kHz (S12.4).
- **Tracking mode: `Efficient`** (C-18). It recomputes coefficients on a move of ≥ 0.1 Hz from the last coefficient
  frequency (crossover_filter.h:306–311), so the corner trails the smoothed value by < 0.1 Hz. HighAccuracy was rejected:
  it can leave the corner unbounded under slow sweeps (298–303).
- **Combined bound.** The coefficient corner is within ε_s(fs) + 0.1 Hz of the handed value one interval after a handoff,
  and within 0.1 Hz once settled: 0.191 Hz worst case, at 192 kHz.
- **SC-009(c).** `getCrossoverFrequency()` returns the handed target, so |f_stage − 2·f_c,law| on it is exactly 0 by
  construction and proves only the relation. The corner is checked separately, by computation (S13.3).

### S7.3 Per sample (`processBlock`)

```
for i in [0, n):
    lIn = left[i], rIn = right[i]
    badL = !isFinite(lIn), badR = !isFinite(rIn)                 // FR-072 mask
    l = badL ? 0 : lIn,  r = badR ? 0 : rIn
    M = (l + r) * 0.5f,  S = (l − r) * 0.5f                       // midside_processor.h:200–201
    k = modeRamp_.process(),  g = sideGainSm_.process()
    if (sideIdle_) { Sp = 0 }
    else { auto [lo, hi] = crossover_.process(S);  Sp = k * (g * lo + hi); }
    outL = M + Sp,  outR = M − Sp                                  // midside_processor.h:225–226
    if (!sideIdle_ && k == 0.0f && modeRamp_.getTarget() == 0.0f) { sideIdle_ = true; crossover_.reset(); }
    left[i]  = badL ? 0.0f : outL
    right[i] = badR ? 0.0f : outR
```

When `right == left` there is one channel and no side. The loop still runs the two smoothers, so their state matches a
stereo call, and it writes `badL ? 0 : lIn` (the non-finite guard only).

Consequences:
- **Topology (a), zero latency** (FR-022, FR-001): M is never filtered, the split is on S only, and nothing is delayed.
- **g_side(1) = 0 exactly**, so Sp = hi = S_high (SC-020). At f ≤ f_c = f_s/2 the LR4 high band is ≤ −24.6 dB (S12.1), so
  A(B) ≥ 24 dB over [20 Hz, f_c) (SC-006).
- **SOLID** (FR-051): `LinearRamp` clamps onto its target (smoother.h:380–383), so k reaches exactly 0. From that sample
  on, Sp is ±0 and outL == outR == M bit for bit — one sample earlier than SC-004(a2) requires. The side path then idles
  and its filter state is cleared (FR-052).
- **L == R input:** S = 0, so the output is M = l = r (FR-025 bullet 3, under C-2's zero-state reading).
- **Non-finite** (FR-072, SC-014): the guarded encode keeps the state finite. The mask writes exactly 0.0f on the bad
  channel's output sample, and the other channel's output is the clean M ± S' of the guarded inputs. The crossover sees
  only finite values, so it never resets itself (biquad.h:354–357).
- **No scratch buffers:** the stage is one pass per sample, so `maxBlockSize` is unused and any block size works.

### S7.4 Phase behaviour (FR-023, SC-005 record)

The LR4 low and high bands are in phase (LP4 = 1/D², HP4 = s⁴/D², with D = s² + √2 s + 1) and sum to the all-pass
(s² − √2 s + 1)/(s² + √2 s + 1) (crossover_filter.h:5). So S' = (g·LP4 + HP4)·S:
- at g = 1 it is an all-pass, at −180° at f_s, with a group delay of 2√2/(2π f_s) (S12.5): 3.75 ms at f_s = 120 Hz,
  1.88 ms at 240 Hz and 0.90 ms at 500 Hz;
- at g = 0 it is the high band.

M_out / M_in is 1, within C-1's numeric bound. These are the analytic expectations printed beside SC-005's measurements.

---

## S8. Image modes and transitions (FR-050–FR-054)

| Event | Law | Offsets | Stage |
|---|---|---|---|
| SPREAD ↔ WIDE | same vector (FR-044) | target ±K ↔ 0, through the character stage's 10 ms smoothing | unchanged |
| → SOLID | target 0, slewed (C-15) | 0 | k ramps 1 → 0 over 10 ms, then the side idles and the crossover resets |
| SOLID → | target from Spread, slewed from 0 | as the mode gives | the side path wakes with zero state and k ramps 0 → 1 |
| Spread / Gravity / crossover / curve / MOTION | continuous (FR-053): slew, 10 ms g smoothing, 20 ms f_c smoothing | — | no mode crossfade |

The mode is global (FR-054): `advanceControl` latches it once per interval for every voice. After a switch into SOLID,
the downstream DC and limiter states converge rather than match (FR-052). SC-004(a3) measures the convergence, and the
Phase 9 re-synchronisation decision is handed over (Handoffs).

---

## S9. Calibration (FR-021, FR-041): seeds, harvests, evidence

Both harvests run **inside `dsp_systems_tests`** (the consuming binary) under the hidden tag `[.harvest]`. Each prints one
CSV and writes `specs/profundum-phase7-stereo-field/evidence/harvest-*.txt` from the run log. The harvested value is then
written into the header constant, and the gated tests verify it. The evidence row names the commit it was harvested on.

### S9.1 `BassStereoField_WideAndFloorHarvest` (K_wide, then g_floor — C-13 order)

1. **K_wide.** Try the candidates {1.0, 0.75, 0.5} with g_floor fixed at the 0.5 seed. For each candidate, run SC-001(a)
   and SC-002 (sub band, stage output) on the WIDE dirty arm. Cover every algorithm, Gravity 0, crossovers
   {60, 120, 250}, notes E0, E1, E2 and C3, and Spread {0.5, 1}. Also run SC-007 and SC-010's WIDE-versus-SPREAD lever.
   Take the largest K_wide that passes all of them.
2. **g_floor.** Try the candidates {0.71, 0.5, 0.35, 0.25, 0.18, 0.125, 0.1} with K_wide from step 1. For each
   candidate, run SC-001(a) and SC-002 at Gravity 0 on all three arms (clean, dirty, Anchor), the full SC-001 grid at
   Gravity 0, and SC-010's Gravity lever. Take the largest g_floor that passes.
3. **Escalation.** If step 1 fails at 0.5, or step 2 fails at 0.1, stop and escalate (C-13). Never go below the escalation
   floor silently.

### S9.2 Evidence files

- `evidence/harvest-wide-floor.txt`: the candidate table and the chosen pair.
- `evidence/sc001-grid.csv`, `evidence/sc005-phase.csv`, `evidence/sc006-attenuation.csv`, `evidence/sc008-motion.csv`
  and `evidence/sc013-cpu.txt`.
- `evidence/sc004-a3-convergence.csv`.
- `evidence/side-decay-record.csv`: C-2's record.

---

## S10. Validation-chain fixture and test helpers (FR-070; test code)

The new header `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h` lives in its own namespace,
`Krate::DSP::StereoFieldTest`. The helpers it builds on are in three namespaces (S0), and same-named helpers exist across
them, so the new header **qualifies every call and type** and opens no `using namespace`:
- `MassDynamicsTest::` (mass_dynamics_test_helpers.h:74): `ChainConfig`, `ChainTaps`, `kGrid`, `gridDriver`,
  `allocateTaps`, `chainTargets`, `coreControlsFor`, `anchorControlsFor`, `unbiasedH1Amplitude`, `makeChainDynamics`,
  `prepareChainLaw`, `kSettleSeconds`, `renderChain` (C-12 self-test);
- `ShapeCharacterTest::` (shape_character_test_helpers.h:42, shape_character_metric_helpers.h:38): `kFixtures`,
  `kE0Hz … kC3Hz`, `differentialClicks`, `ClickCount`;
- `ProfundumTest::` (profundum_core_test_helpers.h:47): `requireLREqual`, `goertzelPhase`, `renderedHarmonicPowers`,
  `aliasedPower`, `sidebandPower`, `renderReference`.

It includes `mass_dynamics_test_helpers.h`, `profundum_core_test_helpers.h`, `shape_character_test_helpers.h` and
`../processors/shape_character_metric_helpers.h` **read-only** (C-12).

```cpp
struct FieldChainConfig {
    MassDynamicsTest::ChainConfig chain;                 // Phase 6: f0, fs, seconds, hostBlock, attachAnchor, Mass, overrides
    BassStereoField::Controls field{};
    std::function<BassStereoField::Controls(std::size_t sample)> fieldAt;   // empty → `field`; read at each grid point
    bool bypassField = false;                            // C-12 self-test: stage not run, vector not handed
    bool shapeOn = true;  float shape = 1.0f;            // dirty arm: SHAPE 1 (BITE)
    std::optional<float> foldAmount;                     // dirty arm: 100 (else the law's)
    std::optional<BassCharacterStage::Algorithm> algorithm;   // empty → character stage off
    float characterAmount = 1.0f;
    ProfundumCore::ExcitationControls excitation = ProfundumCore::kDefaultExcitation;
    bool forceSubCentre = false;      // SC-006 precondition: entries with f_k < f_c set to 0 before the handoff
    std::optional<float> generatorGravity;               // SC-006: the vector comes from a second field at this Gravity
    std::function<void(std::size_t sample, BassStereoField&)> perInterval;   // test hook (steps, seeds, tempo)
};
struct FieldChainTaps : MassDynamicsTest::ChainTaps {    // stage taps (SC-001/002/006 read in and out on one render)
    std::vector<float> characterL, characterR;           // dirty-arm output: post-character, pre-Anchor (FR-070)
    std::vector<float> stageOutL, stageOutR;             // safety-stage output, before DC ×2
    std::vector<std::array<float, kMaxPartials>> vectorPerInterval;          // the emitted vectors (SC-008, SC-009(a))
    std::vector<BassStereoField::ChannelOffsets> offsetsPerInterval;
    std::vector<float> lawCrossoverPerInterval, stageCrossoverPerInterval;   // SC-009(c)
};
FieldChainTaps renderFieldChain(const FieldChainConfig& cfg);
FieldChainTaps renderFieldPoly(std::span<const double> f0s, const FieldChainConfig& common);   // POLY record
```

**Taps.** The inherited taps keep their Phase 6 meaning in the longer chain:
- `coreL/R`: the generator output (SC-001(c), SC-002's clause on the generator, SC-003, SC-019);
- `foldL/R`: after ShapeMorph and HarmonicFold, before the character stage;
- `characterL/R` (new): the **dirty-arm output**, after BassCharacterStage and before the Anchor is added. SC-018's
  post-character sub-band M record and FR-077's dirty-arm and WIDE-offset readings use this tap;
- `dynL/R`: after MassDynamics. This **is** the safety-stage input; there is no separate stage-input tap;
- `stageOutL/R` (new): the safety-stage output, before the DC blockers;
- `dcL/R` and `outL/R`: after the DC blockers, and the limiter output (the full chain).

At each grid point, in this order:
1. `field.setControls(fieldAt(start))`, then `perInterval`, then `field.advanceControl()`.
2. The Phase 6 law targets (`MassDynamicsTest::chainTargets`) and the core, Fold and Anchor controls, as in
   `renderChain` (mass_dynamics_test_helpers.h:400–412).
3. If this grid point is a note-on (`noteOnSample`, or a retrigger from `perInterval`): `core.noteOn`,
   `anchor.noteOn` and `vs.reset()` — **before** the law query, so the query at the note-on interval is the unprimed
   one and lands on the new target (FR-015's Reset exemption).
4. `computePanVector(vs, f0, pan, N)`, with f0 = `core.currentFrequency()`, or the note's f0 at and before the first
   note-on.
5. The `forceSubCentre` and `generatorGravity` adjustments.
6. `core.setPartialPanOffsets(pan)` and `character.setChannelOffsets(wideOffsets(alg))`.

With this order the vector the core forwards at the note-on interval is already the new target, so the bank, freshly
reset (profundum_core.h:545–553), never carries the old pan, and from the next interval the slew runs from that target.
Querying before the note-on would forward the old slewed vector at the note-on and then land on the target unslewed one
interval later, mid-note: a step larger than Δ with audio playing. The Phase 9 handoff states the same order.

Per run: core → ShapeMorph → HarmonicFold → BassCharacterStage (**tap `characterL/R`**) → + Anchor → sum →
MassDynamics (**tap `dynL/R` = stage input**) → `field.processBlock` (**tap `stageOutL/R`**) → DC ×2 → limiter
(`outL/R`), using the Phase 6 constants from `MassDynamicsTest::makeChainDynamics`.

Descriptor helpers (offline, double):
- `zeroPhaseBandMask(x, fs, fLo, fHi)`: FFT, zero the bins outside [fLo, fHi), inverse FFT. It adds no phase of its own.
- `monoSumRatioDb(L, R, fs, fLo, fHi)`: Q_band (spec descriptor block).
- `correlation(L, R, fs, fLo, fHi)`: ρ, after the mask.
- `sideToMidDb(L, R, fs, fLo, fHi)`, and per harmonic `widthDb(L, R, fs, f0, N)`: W_n from `renderedHarmonicPowers` of M
  and S.
- `octaveBands(fc)`: octave bands from f_c to 10 kHz. `thirdOctaves(20, fc)`.
- `goertzelComplex(x, L, fs, hz)`: amplitude and phase (C-16; `goertzelPhase` gives phase only).
- `renderPanReference(fs, total, f0At, harmonics, vectors, Mode::Held | Mode::Interpolated)`: C-16.
- Clicks: `ShapeCharacterTest::differentialClicks(...)` returning `ClickCount`, the Phase 5 definition, included directly
  from `../processors/shape_character_metric_helpers.h` (76, 137). It is not copied.

Self-tests (`BassStereoField_HelpersSelfTest`):
- Q of a hard-panned in-phase tone reads −3.0103 dB.
- Q of an anti-phase pair reads −∞ (floored).
- ρ of L = R is 1.
- The band mask passes a tone in band within 0.01 dB and removes one outside the band by ≥ 120 dB.
- `renderPanReference` gives held == interpolated for a constant vector.
- C-12's chain equality.

---

## S11. RT safety, determinism, portability, denormals

- **RT safety** (FR-071, SC-011):
  - nothing allocates after construction, and every member is fixed-size;
  - nothing takes a lock (`CrossoverLR4`'s atomics are relaxed loads and stores);
  - nothing throws, and every method is `noexcept`;
  - the trig in the law and in `CrossoverLR4::prepare` (inside `reset`, C-4) is bounded work.
- **Non-finite** (FR-072):
  - controls are checked with `detail::isFinite` (db_utils.h:118) in `setControls` and `setTempoBpm`;
  - samples go through the S7.3 guard and mask;
  - there is no `std::isnan` anywhere;
  - the non-finite TU builds with `-fno-fast-math` (S14). The other TUs build in the shipping FP mode, so the guards are
    proved under `-ffast-math` on the macOS leg.
- **Determinism** (FR-033, FR-074, SC-012):
  - Drift is seeded and `reset()` re-seeds it; the LFO's Sine resets deterministically;
  - the per-voice law is a pure function of the applied controls, the interval state, f0 and the VoiceState;
  - every control lands at an `advanceControl()` on the absolute grid;
  - the bit-compare in S4.3 uses `std::bit_cast<std::uint32_t>`, never `==`, which would treat −0 and +0 as equal and NaN
    as unequal.
- **Denormals**: FTZ / DAZ is the host's precondition, and the test main enables it. `OnePoleSmoother`, `LinearRamp` and
  `Biquad` also flush denormals themselves (smoother.h:208, 386; biquad.h:365–366). The law has no recursive state.
- **Portability traps, checked by name:**
  - designated initialisers in every brace init with mixed types, so nothing narrows;
  - `float` literals throughout (`0.5f`, `kMaxPanStepPerInterval`);
  - `std::exp2`, `std::log2` and `std::cos` from `<cmath>` only, and no `long double`;
  - no SIMD (the code is scalar), so alignment never arises;
  - no namespace-scope name besides `BassStereoField`. The nested names avoid `lfo.h`'s namespace-scope `kMinFrequency`
    and `Waveform`, crossover_filter.h's `TrackingMode` and `stereo_field.h`'s `StereoMode`;
  - every constant that has a namespace-scope twin is referenced with its class qualifier.

  `node tools/check-portability.js` and `node tools/lint-apple-globals.js` run before the close (SC-015).

---

## S12. Model evidence (`stereo-field-model.js`, run 2026-10-18, under a second)

### S12.1 LR4 bands (r = f / f_s)

| r | Where | HP | LP |
|---|---|---|---|
| 0.5 | f = f_c | −24.61 dB | −0.53 dB |
| 0.475 | 0.95·f_c | −26.30 dB | — |
| 0.25 | — | −48.20 dB | — |
| 1 | f = f_s | −6.02 dB | −6.02 dB |
| 2.5 | — | −0.22 dB | −32.05 dB |

**The SC-006 margin at the band edge is 0.6 dB** (R-3).

### S12.2 Pan staircase residual (zero-mean sawtooth, re total)

| Δp | Residual |
|---|---|
| 0.003 | −63.35 dB |
| 0.0039 | −61.07 dB |
| 0.0044 | −60.02 dB |

Cycle per-interval step at 1 Hz, depth 1, κ = 0.5: 2.280e-3 at 44.1 kHz, 2.094e-3 at 48 kHz, 1.047e-3 at 96 kHz and
5.236e-4 at 192 kHz. All are below Δ.

### S12.3 Law bounds (lower knee 0.25 oct, upper knee 2 oct)

| γ | c(f_c), G = 0 | c(2f_c), G = 0 | c(2f_c), G = 1 |
|---|---|---|---|
| 1 | 0.0302 (ρ floor of that partial 0.99888) | 0.587 | 0.500 |
| 2 | 0.0004 | 0.217 | 0.146 |
| 4 | ≈ 0 | 0.022 | 0.010 |

- Q of a single partial at p = 0.03, 0.09 and 1: −0.0024, −0.0217 and −3.0103 dB.
- Change of W_n at 2.5·f_s from Gravity min to max (side-gain part only): 0.110 dB at g_floor 0.5, 0.055 dB at 0.25 and
  0.040 dB at 0.18. All are inside SC-003(c)'s 0.5 dB.

### S12.4 Stage crossover settle (380 Hz handoff, 0.1 ms, 32 samples)

6.65e-14 Hz at 44.1 kHz, 1.27e-12 Hz at 48 kHz, 2.20e-5 Hz at 96 kHz and 9.13e-2 Hz at 192 kHz. All are ≤ 0.1 Hz.

### S12.5 Side all-pass group delay at f_s

3.751 ms at f_s = 120 Hz, 1.876 ms at 240 Hz and 0.900 ms at 500 Hz.

### S12.6 CPU (an estimate; T001 measures it before anything else, R-1)

- **Stage.** Encode / decode, 4 biquads, 2 smoothers and 1 ramp come to about 40 flops per sample, or roughly 0.05–0.1 %
  of a core at 48 kHz.
- **Stage crossover recomputes (not in the ~40 flops).** Each recompute is 4 `Biquad::configure` calls
  (crossover_filter.h:355–366). With `Efficient` tracking and the 0.1 ms smoother (per-sample coefficient 0.354 at
  48 kHz), a stage handoff step of D Hz costs about ⌈ln(D / 0.1) / 1.04⌉ + 1 recomputes, all in the first samples of
  the interval:
  - a full-range 120 → 500 Hz stage step: first-interval step ≈ 58 Hz (380 · (1 − 0.846), the 20 ms control-rate
    smoother), ≈ 7 recomputes, then fewer as the steps shrink, for about 38 intervals;
  - a continuous automation sweep of 380 Hz in 100 ms: ≈ 2.5 Hz per interval, ≈ 4 recomputes per interval, for as long
    as the sweep lasts (Phase 8 morph, automation);
  - at 192 kHz the per-sample coefficient is 0.771, so a step keeps recomputing for most of the 32 samples.
  The sweep cost is bounded (≤ 32 recomputes per interval) but larger than the static estimate, so SC-013(a) gains a
  sweep arm (S13.3) instead of trusting this count.
- **Global control work.** The per-interval log2, exp2, drift step, LFO lookup and f_c smoother are negligible next to
  the stage.
- **Per voice.** `applyPanOffsets` costs 96 cos + 96 sin per changed vector (bank:651–658), at 1500 intervals per second
  at 48 kHz. At about 10 ns per scalar libm call that is 0.29 % of a core, **above SC-013(b)'s 0.25 %**, unless MSVC
  vectorises the loop (SVML) or libm is faster. This is the phase's main risk (R-1).
- **Measured (T001, 2026-10-18, isolated, pinned, medians of 3 x 3 s; evidence/run-log/T001.txt, T001-bench.txt).**
  per voice (law_1v − base_1v) **0.198 %** (≤ 0.25), four voices **0.692 %** (≤ 1.0), stage_static **0.073 %**,
  stage_sweep **0.095 %** (each ≤ 0.5), stage_sweep_192k 0.395 % (recorded). Handoffs == queries, so this is the worst
  cadence. **Decision: PROCEED with the plan design;** R-1 is not triggered and FR-016's one-handoff-per-interval
  cadence stands. The estimate above overstated the libm cost (about 6.5 ns per call measured, not 10).

---

## S13. Test plan (file · TEST_CASE · assertion strategy)

### S13.1 Files, targets, tags

All files live in `dsp/tests/unit/systems/`, are registered in `dsp_systems_tests` and are tagged
`[systems][profundum][stereo-field]`.

| File | Holds |
|---|---|
| `bass_stereo_field_test_helpers.h` | S10 |
| `bass_stereo_field_helpers_test.cpp` | `BassStereoField_HelpersSelfTest`, `BassStereoField_ChainMatchesPhase6` |
| `bass_stereo_field_test.cpp` | Interface, controls, and the law-level and stage-level unit cases: FR-011 (C-15), FR-034, FR-053, the SC-005 gate parts, SC-009(c), SC-011, SC-012, SC-017, SC-018 (law), SC-019 (law), SC-020 (law), SC-021 |
| `bass_stereo_field_spectral_test.cpp` | SC-001 and SC-002 (one set of shards, both asserted on each render), SC-003, the SC-005 record, SC-006, SC-010, SC-016, SC-020 rendered; grids `[long]` |
| `bass_stereo_field_motion_test.cpp` | SC-008, SC-009(a); 60 s renders `[long]` |
| `bass_stereo_field_chain_test.cpp` | SC-004, SC-007, SC-009(b), SC-018 rendered, SC-019 rendered, the `[.harvest]` case |
| `bass_stereo_field_nonfinite_test.cpp` | SC-014, in the `-fno-fast-math` block |
| `bass_stereo_field_perf_test.cpp` | SC-013, `[performance][.perf]` |

`[long]` follows the root CLAUDE.md convention: it marks only multi-minute render grids whose failure mode is not
toolchain-specific. SC-004, SC-011, SC-012, SC-014, the SC-015 portability gate and the law-level cases stay in the
per-push lane.

### S13.2 Shared conventions

- **Renders.** Every renderer starts from `prepare` + `reset`. It settles for `kSettleSeconds = 1.0`
  (mass_dynamics_test_helpers.h:87) before any descriptor window, and descriptor windows are whole periods of f₀ (spec
  descriptor block).
- **Fixtures.**
  - Clean core: `kSawAnchor` and `kGrowl`, with the excitation off unless stated.
  - Dirty arm: SHAPE 1 → Fold 100 → the character stage at each algorithm, Amount 1.
  - Anchor arm: the dirty arm plus the Anchor on Sine / LOCK at `kDefaultLevel`.
- **Notes and rates.** E0, E1, E2 and C3 (C2 and C4 where the SC says so), at 48 kHz unless the SC names other rates.
- **Records.** Every gated reading is printed (`UNSCOPED_INFO` plus a CSV line in the run log), so compliance rows can cite
  log lines.
- **No bit-exact float goldens.** Bit-identity is asserted only **within one render** (L against R) or between two
  renders of the same build (SC-012). Cross-build comparisons use `fingerprintRender` / `compareFingerprints` with
  measured tolerances.

### S13.3 Per criterion

| SC | TEST_CASE | Strategy |
|---|---|---|
| SC-001 | `BassStereoField_MonoSumPerBand_<shard>` `[long][sc-001][sc-002]` | **Grid:** the spec grid (3 modes × Spread {0, .5, 1} × Gravity {0, .5, 1} × crossover {60, 120, 250} × curve {0, 1} × depth {0, 1} × source {Drift, Cycle} × E0, E1, E2, C3) on three arms.<br>**Per render, gated:** (a) Q_sub, `REQUIRE(abs(Q) <= 1.0)`, and (b) Q per octave band from f_c to 10 kHz `>= -3.11`, each at the **stage output** (`stageOutL/R`) and at the **limiter output** (`outL/R`, the full chain; the limiter is stereo-linked, true_peak_limiter.h:40, 142); (c) the same readings on the core tap (`coreL/R`, the generator output).<br>**Per render, recorded (FR-077 coverage):** Q_sub and the octave Q at the dirty-arm output (`characterL/R`) and at the stage input (`dynL/R`). On the WIDE dirty shards the per-cell difference of the `characterL/R` reading against the same cell in SPREAD is printed; it isolates the WIDE offsets. These rows are FR-077's dirty-arm and WIDE-offset mono-sum tests.<br>**SC-002 on the same render** (next row).<br>**Sharding:** the grid is split by mode × arm into separate TEST_CASEs without SECTIONs (`…_Clean`, `…_Dirty_<alg>`, `…_Anchor`), so the [long] lane can run them as concurrent shards.<br>Depth > 0 renders run 60 s and gate the worst window. CSV: `sc001-grid.csv`, one line per cell and tap. |
| SC-002 | asserted inside every `BassStereoField_MonoSumPerBand_<shard>` (the spec's `BassStereoField_SubBandCorrelation` is that shard set, tag `[sc-002]`) | ρ_sub is computed on **every** cell of the SC-001 grid from the same render, and asserted there: ρ_sub ≥ 0.99 at the stage output on all three arms, and at the core tap (the generator output alone). It is also recorded at the limiter output, the dirty-arm output and the stage input. No cell is skipped and no re-render ranks cells by Q, because Q and ρ measure different things (L = x, R = 0.5x gives ρ = 1 and Q = −0.46 dB; equal levels with uncorrelated side give ρ ≈ 0.74 at Q ≈ −0.6 dB, C-13). The sub-band side-to-mid ratio is printed per cell, and the worst-ρ cell per shard is printed beside the worst-Q cell. |
| SC-003 | `BassStereoField_WidthMonotonic` | **(a)** W_n non-decreasing in n·f₀ (1e-3 dB float tolerance), and octave ρ non-increasing.<br>**(b)** The same over 11 Spread steps.<br>**(c)** Gravity 0 → 1 never raises W_n or an octave side-to-mid ratio (0.01 dB tolerance), and at n·f₀ ≥ 5·f_c the drop is ≤ 0.5 dB.<br>**(d)** At Spread 0 every W_n is at the −120 dB floor, and `requireLREqual` holds.<br>**WIDE dirty arm:** per octave band, monotone within 0.5 dB. Both taps are read. |
| SC-004 | `BassStereoField_SolidBitIdentical`, `BassStereoField_AnchorBitIdentical` | **(a1)** Full chain from reset in SOLID, host blocks {1, 17, 32, 512}: `requireLREqual` at the stage output and at the limiter output from sample 0.<br>**(a2)** Start in SPREAD or WIDE (offsets at the ceilings) and switch at a grid point: `requireLREqual` on the stage output from `crossfadeEnd + 1`. The crossfade end is the first sample with `modeFade() == 0`, recorded by a per-sample probe.<br>**(a3)** At the limiter output, max\|L − R\| ≤ −120 dBFS from crossfadeEnd + 500 ms; the first crossing time is printed.<br>**(b)** L == R input from reset, every mode / Gravity / crossover: output bit-equal to the input and L == R. The C-2 switch adds the dirty-state arm.<br>**(c)** Anchor-only input, every mode and setting, from reset and after a SOLID crossfade. |
| SC-005 | `BassStereoField_SafetyStagePhase` | **Recorded:** the swept-sine / impulse response of the stage on a pure-S and a pure-M input at f_c {60, 120, 250} (f_s = 2·f_c), Gravity {0, 1}. The S_out/S_in phase from 20 Hz to 10 kHz and the group delay at f_s are printed beside S7.4's analytic values, along with M_out/M_in. Q_band on stereo noise with ρ = 0.5 (seeded) is printed.<br>**Gated:** `latencySamples() == 0` (a static_assert, plus a runtime check in every mode); M within C-1's bound (and bit-exact while `kGateMidBitExact` is set); SC-001 and SC-006 hold at every crossover (asserted in their own cases). |
| SC-006 | `BassStereoField_SafetyStageAttenuation` `[long]` | **Fixtures:** (i) the WIDE dirty arm with offsets at the ceilings, Spread 1, every algorithm; (ii) excitation mix 1 / drive 1, Spread 1; (iii) synthetic side-only tones at √(20·f_c) and 0.95·f_c. All run with `forceSubCentre = true` and `generatorGravity = 1`.<br>**Cell grid:** notes {E0, E1, E2, C3} × f_c {60, 120, 250} × arm, stage at Gravity max.<br>**Which cells carry content.** A single-voice periodic render has content only at n·f₀, so B = [20 Hz, f_c) holds a harmonic only where f₀ < f_c. Expected to carry distortion-born side content: E0 (20.60 Hz) and E1 (41.20 Hz) at every f_c; E2 (82.41 Hz) at f_c ∈ {120, 250}; C3 (130.81 Hz) at f_c = 250. Expected empty: E2 at 60, C3 at 60 and 120.<br>**Validity rule (band level).** If D_side(B) at the stage input (`dynL/R`) is below −100 dBFS, the cell is printed as "no distortion-born content in B", counted, and not gated. The same rule applies per third octave inside B. Every cell's validity is printed beside its expectation, and a mismatch is flagged in the log.<br>**Non-vacuity:** `REQUIRE` that fixtures (i) and (ii) each have at least one valid gated cell **per crossover** (at f_c = 60 that means E0 or E1).<br>**Gate:** on every valid cell, A(B) = D_side(B) at the stage input minus D_side(B) at the stage output, on the same render, is ≥ 24 dB on B and on each valid third octave inside B. Each positive-control tone (iii) must be attenuated ≥ 24 dB at every cell, or the arm is flagged invalid (REQUIRE fails).<br>**Recorded / monotone:** the Gravity 0 readings are printed, and A(B) is non-decreasing over 11 Gravity steps (0.01 dB tolerance). |
| SC-007 | `BassStereoField_WideAliasing` `[long]` | WIDE dirty arm, each algorithm at Amount 1, offsets at the ceilings, Fold 100, E0–E2, at 44.1, 48 and 96 kHz: `aliasedPower` per channel ≤ −80 dBFS, with Phase 5's helper and FFT length. |
| SC-008 | `BassStereoField_MotionBounded` `[long]` | Law-level, so no audio render is needed: `advanceControl` + `computePanVector` per interval for 60 s (90 000 intervals at 48 kHz).<br>• **Excursion:** ≤ κμP + 1e-6 and \|p\| ≤ P; Cycle non-vacuity ≥ 0.99·κμP.<br>• **Cycle rate:** the spectral peak of p_k(t) (BH7 over 60 s at the control rate) is ≤ 1 Hz and within one bin of `cycleFrequencyHz()`.<br>• **Drift τ:** `driftTauSeconds()` ∈ [0.5, 30] at rate {0, .25, .5, .75, 1}; the fitted 1/e autocorrelation is within ×2 for τ ≤ 6 s.<br>• **Fixed partials:** h1 and every c = 0 partial bit-equal throughout.<br>• **No sign change.**<br>• **Inward motion:** the time-averaged \|p\| printed for μ {0, .5, 1} and checked for Cycle.<br>• **Sync:** 120 and 200 BPM with the fastest note value; the folded rate (C-9, pending ratification) within one bin. A NaN or −1 tempo keeps the previous value.<br>• **Fold record:** `evidence/sc008-sync-fold.csv` lists the unfolded and the folded rate of every note value × modifier at 120 and 200 BPM, and which note values alias onto the same rate (C-9). |
| SC-009 | `BassStereoField_NoZipper` `[long]`, `BassStereoField_TransitionsClickFree` | **(a)** C-16 references.<br>• Spread sweep and MOTION: `sidebandPower(residual, fs, f0, 32, 0, 0, 0, productTotal)` ≤ −60 dB on L and R.<br>• 48 st Drop: the broadband residual re the product total ≤ −60 dB, and a differential click count of 0.<br>• Validity arm: product − held ≤ −60 dB.<br>**(a′) Reset retrigger** (law level, `bass_stereo_field_test.cpp`): mid-render, with the vector settled at Spread 1, a Reset note-on together with a changed target (Spread 0.2 and crossover 250 at the same grid point), in S10's order (note-on and `vs.reset()` before the query). The note-on interval's emitted vector equals the new target, and from the next interval on no emitted step exceeds Δ on any k. A positive-control arm with the old order (query before the reset) must show a step > Δ after the note-on interval.<br>**(b)** Mode, Gravity and crossover steps, host blocks {64, 512}, E0 / E2 / C3, `kSawAnchor`: a differential click count (`ShapeCharacterTest::differentialClicks`) of 0. The positive controls (an unsmoothed step and a hard switch) must detect.<br>**(c)** After a 60 ↔ 250 Hz step, from one interval on: `stageCrossoverPerInterval == 2·lawCrossoverPerInterval` within 0.1 Hz (the relation; exact by construction). **The corner, by computation (C-18):** the test drives a real `OnePoleSmoother` configured as the stage's (0.1 ms, fs) through the recorded per-interval handoff sequence and applies the `Efficient` rule of crossover_filter.h:306–311 per sample, giving the coefficient frequency at every interval end. At 44.1, 48, 88.2, 96 and 192 kHz, on the full-range step and on a slow sweep (stage target moving 5e-5 Hz per interval for 10 s): the smoother residual after one interval is ≤ 0.1 Hz (FR-024), and |corner − handed| ≤ ε_s(fs) + 0.1 Hz at every interval end. The worst value per rate is printed. The same simulation with the HighAccuracy rule (298–303) is printed beside it on the slow sweep, as the evidence for C-18. **Ruled 2026-10-18 (G7-1):** on the chain output (b) is gated by the sample-delta bound (≤ 1.1 × the steady max |Δy| over ≥ 21 traversed settings per channel) with the differential count recorded; the count stays gated at 0 at the core tap; positive controls unchanged. |
| SC-010 | `BassStereoField_LeversAudible` `[long]` | Showcase: SPREAD, `kSawAnchor`, E1 and C3.<br>• **Static levers**, each alone between its extremes: the best octave band moves ≥ 3 dB in side-to-mid, or ≥ 0.1 in ρ.<br>• **MOTION depth:** the standard deviation over time of W_oct(t) (4096-sample BH7, hop 1024 at 48 kHz, scaled with fs) rises by ≥ 1 dB.<br>• **MOTION rate:** the dominant modulation frequency (Cycle) or the fitted 1/e time (Drift) changes by a ratio ≥ 4.<br>One table with every lever is printed. |
| SC-011 | `BassStereoField_NoAllocation` | `AllocationScope` around `setControls`, `advanceControl`, `computePanVector`, `wideOffsets`, `processBlock` (1, 32 and 4096 samples), `reset`, every mode switch, a source switch, `setTempoBpm` and `setDriftSeed`. `getAllocationCount() == 0`.<br>**Fresh-field arm:** `reset()` (and `processBlock`, `advanceControl`, `computePanVector`) on a freshly constructed field that was never prepared, inside the scope: no allocation, every output finite (the stage outputs M ± 0 while the crossover is unprepared, S3), and a following `prepare` + `reset` render is bit-equal to one from a field that never saw the early calls (SC-012). |
| SC-012 | `BassStereoField_Deterministic` | A field-only grid render (`gridDriver`, seeded stereo noise in) with a mode switch, a Spread ramp and MOTION on (each source), at host blocks {1, 17, 32, 64, 512}:<br>• outputs and emitted vectors bit-identical across partitions (memcmp);<br>• two renders from `reset()` bit-identical, including after a render that moved the crossover (the C-4 case);<br>• seeds 0xB17E and 0x1234 give different outputs. |
| SC-013 | `BassStereoField_Cpu` `[performance][.perf]` | **(a)** The stage at Gravity max + MOTION (the dearer source, picked by a first timing pass) + `advanceControl`, 48 kHz, 60 s of audio, pinned (tools/pin-perf-cores.ps1, as the Phase 6 perf TU documents): ≤ 0.5 % of one core.<br>**(a-sweep)** The same setup with the crossover control ramping continuously 60 ↔ 250 Hz (a triangle with 100 ms legs, so the stage recomputes coefficients on every interval, S12.6): ≤ 0.5 % of one core at 48 kHz, gated; 192 kHz printed. A step-train arm (60 ↔ 250 Hz every 50 ms) is printed. All three readings go to `evidence/sc013-cpu.txt`.<br>**(b)** One and four `ProfundumCore`s at Spread 1, depth 1 and the fastest rate (each source), minus the same at Spread 0: ≤ 0.25 % per voice and ≤ 1.0 % at 4 voices.<br>**(c)** Handoffs per second on a static note with MOTION off, after settling: 0. Printed here, and also asserted in `bass_stereo_field_test.cpp` as a non-perf case. |
| SC-014 | `BassStereoField_NonFinite` | 16-sample NaN / +Inf / −Inf bursts (bit patterns via `std::bit_cast`, volatile) on L only, R only and both; non-finite values in every `Controls` float and in `setTempoBpm`.<br>• Every output is finite (bit test).<br>• Masked samples are exactly 0.0f.<br>• The state is finite after one block (the next block is finite).<br>• After 100 ms the output is within −60 dB of a clean render. |
| SC-015 | gate | The change-set lanes (S14.2). `git diff --name-only` shows none of the five frozen headers and no Phase 1–6 test helper. The portability and Apple-globals lints pass. clang-tidy runs on the new TUs by file list. |
| SC-016 | `BassStereoField_SampleRates` `[long]` | At 44.1, 48, 88.2, 96 and 192 kHz:<br>• output finite and bounded;<br>• E1 W_n within 0.25 dB of 48 kHz;<br>• SC-006's A(B) within 1 dB;<br>• a prepare(48k) → prepare(96k) → prepare(48k) render bit-equal to a fresh 48 kHz render (same build, memcmp). |
| SC-017 | `BassStereoField_WideOffsetCeilings` | Every algorithm × 11 Spread steps × every algorithm switch:<br>• \|offset\| ≤ ceiling exactly (float compare), and = ceiling·Spread·K_wide within 1e-6;<br>• zeros in SOLID and SPREAD and at Spread 0;<br>• the vector bit-identical in WIDE and SPREAD (memcmp) over a 2 s MOTION-on control sequence. |
| SC-018 | `BassStereoField_H1Centred` | **Law:** `out[0] == 0.0f` for every mode, setting and note E0 … C4.<br>**Rendered:**<br>• Goertzel h1 on L and on R against `deliveredGains()[0] · kCenterPanGain`, within 0.01 dB;<br>• h1 power in M at the core tap, and the Anchor output power, against the Spread 0 render within 0.1 dB in every mode;<br>• the post-character sub-band M printed per mode. |
| SC-019 | `BassStereoField_PanOnlyDirection` | **Law:** the sign pattern (odd harmonics −, even +) is identical across reset, VoiceState instances and seeds. **Per interval:** in SPREAD and WIDE (and SOLID, where every p_k is 0 and nothing is read), at Spread {0.5, 1}, E1 and C2, MOTION depth {0, max} with each source, over 60 s of control intervals: for every interval and every k with p_k ≠ 0, sign(p_k) == s_k (bit-level sign, so −0 counts as no entry). The count of checked (interval, k) pairs is printed and must be > 0 per configuration.<br>**Rendered:** the per-harmonic L − R phase \|Δφ\| ≤ 0.01 rad (C-14 skip rule, count printed); the harmonic-resolved Q ≥ −3.11 dB. |
| SC-020 | `BassStereoField_GravityFloor` | **Law:** `sideGainForGravity` is non-increasing over 11 steps, with g(0) = kSideFloorGain ∈ (0, 1) and g(1) == 0.0f. The per-sample `currentSideGain()` lands on 0.0f at Gravity 1 within 10·kSideGainSmoothMs.<br>**Rendered** (in the spectral TU): A(B) at Gravity 0 > 0 dB, and SC-001 / SC-002 at Gravity 0 on all arms. This re-asserts the harvest's verdict with the committed constant. |
| SC-021 | `BassStereoField_CurveContinuous` | curve 0 … 1 (11 steps) × crossover {60, 120, 250} × Gravity {0, 1}:<br>• `isValidControls` is true, and out-of-range values clamp;<br>• the vector is finite and ≤ 1;<br>• interpolated control sets at t {.25, .5, .75} are valid, and their W_n lies between the endpoints' within 0.5 dB. The pair set holds single-control pairs, same-direction pairs and mixed pairs (including C-17's counter-example). While `kGateMixedInterpolationPairs` is `true` (the spec reading, default) every pair is gated; the recommended reading (C-17: same-direction pairs gated, mixed pairs recorded with their excursion beyond the endpoints) is computed and printed beside it;<br>• the largest adjacent-step W_n change is printed. |
| FR-011 | `BassStereoField_SolidZeroVector` | C-15's gate: the emitted vector is bit-zero at the 334th query after a switch into SOLID (and after Spread → 0) and on every query after it; a SOLID start is bit-zero from the first query. |
| FR-034 | `BassStereoField_MotionOutputConformance` | Over 10 000 intervals with depth 1, for each source and across a source switch: `motionSource().getCurrentValue() == motionValue()` (bit-equal) on every interval, both in [−1, 1]; `getSourceRange()` returns exactly {−1, 1}; the value is used through a `const ModulationSource&`, so the concept is exercised as Phase 9 will use it. |
| FR-053 | `BassStereoField_ContinuousControlsNoCrossfade` | In SPREAD and in WIDE, from a settled state: step Spread, Gravity, crossover, curve, motion depth, motion rate and source, each alone and all at once, at grid points over a 2 s stage render. `modeFade() == 1.0f` (bit-equal) on every sample, read by a per-sample probe. |

---

## S14. Build integration

### S14.1 `dsp/tests/CMakeLists.txt`

1. In the `dsp_systems_tests` source list, after the Phase 6 block (line 742), add a commented, enumerated
   "Profundum Phase 7 (specs/profundum-phase7-stereo-field)" block listing the seven TUs of S13.1 with their SC ids.
2. In the `-fno-fast-math` block (after line 1237), add `unit/systems/bass_stereo_field_nonfinite_test.cpp` with the
   Phase 7 comment. The perf TU and the other TUs stay out, in the shipping FP mode.

No other CMake file changes. The header is header-only and lives in the existing KrateDSP include tree. T002 confirms
whether `dsp/CMakeLists.txt` lists headers explicitly; if it does, `systems/bass_stereo_field.h` is added there in the
same commented style.

### S14.2 Commands (Windows, full CMake path) and parallelism

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
# ONE rebuild (only dsp_systems_tests contains Phase 7 TUs; no shared header changes):
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
# Phase lane = the phase's own TUs by tag, excluding timing-sensitive cases; log captured, never re-run for output:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[stereo-field]~[performance]~[perf]~[benchmark]~[long]" 2>&1 | tee <log>
# [long] lane: one shard per [stereo-field][long] TEST_CASE, list GENERATED from the exe (--list-tests), run
# concurrently (cores − 2), each to its own log — filtered to the phase's own TUs only (root CLAUDE.md change-set rule):
#   dsp_systems_tests.exe "BassStereoField_MonoSumPerBand_Dirty_Rectify" …
# Harvest (once, before the gated spectral cases are trusted; concurrent with nothing timing-sensitive):
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.harvest][stereo-field]" 2>&1 | tee <log>
# CPU lane ALONE, after everything has finished and the machine has idled:
node tools/run-cpu-tests.js dsp_systems_tests     # filter to [stereo-field] per the runner's argument form
# gates: node tools/check-portability.js; node tools/lint-apple-globals.js; wsl --shutdown afterwards;
# clang-tidy on the new TUs by file list (per-file timeout), after reconfiguring build/windows-ninja.
```

**Which suites the change set reaches.**
- New files only: `systems/bass_stereo_field.h` and the eight test files.
- Modified files: `dsp/tests/CMakeLists.txt` (registration) and the spec folder.
- No shared header is modified (FR-073, C-12), so no consumer suite joins the lanes: not Ruinae, Seraphis, Vorago or
  Innexus, and not the Phase 1–6 Profundum TUs.
- pluginval does not apply, because no plugin code changes.

**Measure before running (root CLAUDE.md).** T001 records the wall clock of one SC-001 shard and of the harvest before
the full grid is launched. The grid is sharded so the [long] lane finishes in roughly the time of its longest shard.

---

## S15. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | SC-013(b): the 192 libm calls `applyPanOffsets` makes per changed vector may exceed 0.25 % per voice (S12.6) | **Measure first.** T001 runs before the law is built: a micro-bench of `applyPanOffsets` at the worst cadence.<br>**The only lever.** The bank is frozen (FR-073), so the only lever is the handoff cadence (FR-016). The plan's slew bound is per handoff (C-6), so a 2-interval MOTION cadence keeps −63 dB but halves the slew rate. That would bend Cycle above about 0.7 Hz (π·64/fs > 0.003 at 1 Hz), which changes a ruled bar (SC-008's 1 Hz), so it goes to the user.<br>The CPU ceiling is never relaxed. |
| R-2 | C-13: the Gravity-0 floor on the WIDE dirty arm | The S9.1 order and escalation floor. |
| R-3 | SC-006's margin is 0.6 dB at the f_c band edge (LR4 HP −24.61 dB at f_s/2) | The biquad coefficients are float, and the coefficient corner is within ε_s(fs) + 0.1 Hz of the handed value (C-18; ≤ 0.191 Hz at 192 kHz, 0.16 % of the lowest f_s, ≤ 0.06 dB at f_c): both negligible against 0.6 dB. The third octave just below f_c sits wholly below f_c, so its worst bin is ≥ 24.6 dB. If a reading lands within 0.1 dB of the bar, print the per-bin table and investigate. The Q3 ruling fixed the multiple at 2.0, so the multiple is not tuned. |
| R-4 | The C-1 / C-2 tests go red as written | They are built to the spec as written, behind one switch each, and go to the user before the build stage closes. |
| R-5 | SC-021's mixed interpolation pairs: c is monotone in each control separately, so a pair whose controls move in opposite width directions puts the midpoint's W_n outside the endpoints' range | Promoted to C-17 (⚠ RE-RULE). The test gates every pair (`kGateMixedInterpolationPairs = true`, the spec reading) and prints the recommended reading beside it, until the main loop rules. |
| R-6 | W_n in dB is ill-conditioned near p = 0 (20·log₁₀\|tan(πp/4)\| → −∞) | The −120 dB floor (spec). The monotonicity comparisons use the floored values with a 1e-3 dB float tolerance. |
| R-7 | Partition invariance depends on the owner calling `advanceControl()` on the absolute grid | `gridDriver` in every test. The Phase 9 handoff says so, and SC-012 proves it with host blocks 1 and 17. |
| R-8 | Calling `CrossoverLR4::prepare` inside `reset()` contradicts its "NOT real-time safe" comment | C-4: the body was read and is allocation-free, and SC-011 gates `reset()`. If the header changes later, SC-011 catches it. |
| R-9 | MSVC-only leniency (narrowing, missing includes, `std::copysign` on float) | `check-portability.js` and `lint-apple-globals.js` before the commit. The header includes `<cmath>`, `<array>`, `<bit>`, `<cstdint>` and `<utility>` explicitly. |
| R-10 | Long 60 s MOTION renders across the grids blow up the [long] lane | SC-008 is law-level, with no audio. SC-001 renders 60 s only for depth > 0 and gates only the worst window. The shards run concurrently. |
| R-11 | Re-entering `LinearRamp::setTarget` stretches the mode crossfade | S7.1 sets the target only on a change. SC-004(a2) records the measured crossfade length and asserts 10 ms ± 1 sample. |

---

## S16. Task outline (for tasks.md)

1. **T001** CPU probe (R-1): micro-bench `applyPanOffsets` at one change per interval, on 1 and 4 cores, isolated. Record
   `evidence/sc013-cpu.txt`, and decide whether R-1's escalation is needed **before** building the law.
2. **T002** Re-run the ODR sweep. Add the CMake registration block (all seven TUs plus the non-finite entry) with empty
   TUs. Check whether `dsp/CMakeLists.txt` lists headers. The click helper is `ShapeCharacterTest::differentialClicks`
   (processors/shape_character_metric_helpers.h:76, 137), included directly.
3. **T003** Failing tests first: interface, controls, `isValidControls`, SC-011, SC-021 (law-level), SC-017, and the
   law parts of SC-018, SC-019 and SC-020.
4. **T004** Header skeleton: constants, `Controls`, `VoiceState`, `MotionOutput`, `setControls`, `isValidControls`,
   `prepare`, `reset` (C-4) and `advanceControl` (S4.1, S5, S7.1).
5. **T005** The law: `computePanVector`, `widthCurve`, the slew and the handoff count (S4). T003's law cases go green,
   plus FR-011 (C-15), FR-034, FR-053, SC-009(a′) and SC-019's per-interval law check.
6. **T006** WIDE offsets (S6). SC-017 goes green.
7. **T007** The safety stage `processBlock` (S7.3), with the SC-005 gates, SC-004(b)(c), SC-020 (stage) and the SC-014 TU.
8. **T008** The test helpers (S10) and their self-tests, including C-12's chain equality.
9. **T009** The harvest (S9.1). The constants are committed in the header, with evidence.
10. **T010** Spectral TU: SC-001 with SC-002 on the same shard renders (every cell, every tap), SC-003, SC-006 (with the
    band validity rule and non-vacuity), SC-010, SC-016, SC-020 rendered.
11. **T011** Motion TU: SC-008, SC-009(a).
12. **T012** Chain TU: SC-004(a1)(a2)(a3), SC-007, SC-009(b)(c), and SC-018 / SC-019 rendered.
13. **T013** Determinism (SC-012) and the perf TU (SC-013, including the crossover-sweep arm). The CPU lane runs alone,
    last.
14. **T014** Zero warnings, clang-tidy on the new TUs, the portability and Apple lints, and the compliance table with
    log-line citations.

---

## Handoffs (FR-080)

**Phase 8**
- `BassStereoField::Controls` is the morphable set.
- `spread`, `gravity`, `crossoverHz` (60–250), `curve` ([0, 1], one family), `motionDepth` and `motionRate` interpolate
  linearly.
- `mode` is stepped-with-crossfade through FR-052: the field's 10 ms side crossfade plus the vector slew.
- `motionSource`, `cycleSync`, the note value and the modifier are stepped.
- `isValidControls` is static, for `isValidMorphState` to call.

**Phase 9**
- Per interval, in this order: `field.advanceControl()` once; then, per voice, any note-on at this grid point (the
  core's `noteOn` and, under the Reset policy, `VoiceState::reset()`); then `computePanVector(voiceState, resolvedF0,
  pan, N)` → `core.setPartialPanOffsets(pan)`, and `wideOffsets(alg)` → `character.setChannelOffsets`.
- The note-on (with `VoiceState::reset()`) must come **before** that voice's law query at the same grid point (S10).
  The other order forwards the old slewed vector at the note-on and lands unslewed one interval later, mid-note
  (SC-009(a′)).
- The stage sits after `MassDynamics` and before DC ×2 and the limiter (roadmap line 607).
- `setTempoBpm` is fed from the process context.
- The full-engine mono-sum check reuses SC-001's helpers.
- A per-voice CPU line, "stereo-field law + pan forwarding", at SC-013(b)'s measured figure, charged to the headroom
  (FR-079).
- Decide whether to re-synchronise the DC and limiter states at the end of a SOLID crossfade. SC-004(a3) gives the
  converged time.
- `motionSource()` is a `ModulationSource`, if the Motion bus wants it.

**Phase 11** — IDs 800–899, with defaults from S3:
- Image Mode (stepped, 3 values);
- Spread, Gravity, Crossover (60–250 Hz, default 120), Curve (continuous [0, 1]);
- Motion Depth, and Motion Rate ([0, 1], displayed as Hz for Cycle and seconds for Drift through C-9's maps);
- Motion Source (Drift / Cycle), Cycle Sync (on / off), and Cycle Note Value (the `kNoteValueDropdownMapping` list).
- **Synced aliasing (C-9, if the fold is ratified):** note values whose rate exceeds 1 Hz are octave-folded, so several
  of them give the same rate (at 120 BPM, 1/2 … 1/32 are all 1 Hz; `evidence/sc008-sync-fold.csv`). Phase 11 either
  shows the effective rate beside the note value or, if C-9's alternative (B) is ruled, exposes only the note values at
  or below 1 Hz at the top tempo.

**Phase 12** — mono-guarantee labels:
- Spread, curve, crossover and MOTION can never cancel the mono sum: the vector is pan only and constant-power, and the
  generator's sub band is at ρ ≥ 0.9989 by construction.
- Gravity's minimum still attenuates side content below 2·f_c by the harvested floor.
- WIDE's offsets are the one control whose sub-band correlation depends on the harvested pair (S9.1).
- Synced MOTION rates above 1 Hz are folded into (0.5, 1] Hz (C-9); the label must not promise a distinct rate per note
  value.

## Review notes

- Nothing in this plan changes `HarmonicOscillatorBank`, `ProfundumCore`, `BassCharacterStage`, `CrossoverLR4`,
  `MidSideProcessor` or any Phase 1–6 test helper.
- Open for the main loop:
  - C-1, C-2 and C-17 (re-rules);
  - C-9 (the synced octave fold versus restricting the exposed note values);
  - C-15 (FR-011's wording under the slew: target exactly 0, emitted vector exactly 0 within 334 intervals);
  - C-18 (FR-024 / SC-009(c): `Efficient` tracking with the stated combined corner bound);
  - C-5 (the curve-warp reading);
  - C-13 (the escalation floor values);
  - R-1, if T001 shows the per-voice ceiling cannot be met at the 1 Hz Cycle bar.
- Review 2026-10-18, applied: SC-002 now runs on every SC-001 cell and tap; SC-006 has a band-level validity rule, the
  expected-content cells and a non-vacuity REQUIRE; R-5 is promoted to C-17; FR-077 readings at the dirty-arm, stage
  input and limiter taps (limiter gated); `characterL/R` tap added and `stageInL/R` dropped (= `dynL/R`); C-15 and C-9
  raised for ratification with gates and records; the S10 grid order puts the note-on before the query; FR-034, FR-053
  and SC-019 per-interval cases added; `reset()` before `prepare()` is safe and gated; the crossover-sweep CPU arm;
  the helper namespace is `StereoFieldTest` with qualified Phase 5 / 6 calls; `differentialClicks` is included, not
  copied.
- Review 2026-10-18, **rejected in part:** the suggestion to switch the stage crossover to `TrackingMode::HighAccuracy`.
  HighAccuracy recomputes only while `!isComplete()`, tested after `process()` (crossover_filter.h:298–303), so a run of
  sub-1e-4 Hz handoff steps (a slow sweep) never reaches the coefficients and the corner drifts without bound; it also
  costs more per sweep. The issue's other option is taken instead: the combined bound is stated (C-18) and checked by
  computation at every rate in SC-009(c), not through `getCrossoverFrequency()`.
- Review 2026-10-18, naming: the spec names SC-002's case `BassStereoField_SubBandCorrelation`. It is realised as the
  SC-001 shard set carrying the tag `[sc-002]`, because a separate case would either re-render the whole grid or pick
  cells by Q. The compliance row cites the shard logs.
