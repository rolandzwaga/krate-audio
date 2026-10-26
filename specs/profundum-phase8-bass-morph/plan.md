# Implementation Plan: Profundum Phase 8 — Bass Morph

**Spec:** `specs/profundum-phase8-bass-morph/spec.md` (Draft 2026-10-19, reviewed; Q1–Q8 ruled)
**Roadmap:** `specs/Profundum-roadmap.md` Part A, Phase 8 (lines 573–607); KDD 4 (lines 137–144)
**Phase base:** commit `964045a4` on `synth/profundum`
**Status:** Planned 2026-10-19. Section S1 lists the refinements this plan makes inside the freedom the spec leaves to
the plan, the points where it amends a spec sentence (each marked **FOR RATIFICATION**), and one ⚠ RISK escalation
(C-9) that a probe decides before any gated render is trusted. The drafted values the spec hands to the plan stage
(SC-008(a) ε, the SC-015 bars, D-9, D-10, D-11, D-12) are **adopted as drafted** (C-15); no bar is relaxed anywhere.

Two new Layer 3 headers, both header-only, `Krate::DSP`:

- `dsp/include/krate/dsp/systems/morph_state.h` — `MorphState` (plain data), its field-descriptor table, validation and
  versioned serialisation.
- `dsp/include/krate/dsp/systems/bass_morph_engine.h` — `BassMorphEngine`: two endpoints, the position ramp, the
  per-field laws, the Anchor morph modes, the selection machine and the runtime-state round trip.

No existing header changes (FR-088). Everything else is test code under `dsp/tests/unit/systems/`.

---

## S0. Verification ledger (every fact below was read in this session, 2026-10-19)

Paths are relative to `dsp/include/krate/dsp/` unless stated.

| Fact | Where |
|---|---|
| `SpectralShapeRecipe` is L2; `struct Controls { float depth = 0.5f; body = 0.5f; bodyCurvature = 0.5f; bodyEmphasis = 0.0f /*[-1,0]*/; edge = 0.5f; shift = 0.0f /*[-1,+1]*/; }` — "Plain aggregate (Phase 8 interpolates it)" | `processors/spectral_shape_recipe.h:2, 40–48` |
| Named states are declared in-class and defined `inline constexpr` after the class, because "Clang and GCC reject in-class nested-aggregate NSDMIs (plan R-7)" | `spectral_shape_recipe.h:79–85, 226–262` |
| `kSineAnchor = {1, 0, 0.5, 0, 0, 0}`; `kGrowl = {0, 0.5, 0, 0, 1, 1}` (edge is already 1) | `spectral_shape_recipe.h:233–234, 247–248` |
| `[[nodiscard]] static Controls sanitize(const Controls& c) noexcept` | `spectral_shape_recipe.h:89, 268` |
| `ProfundumCore::kControlInterval = 32` | `systems/profundum_core.h:97` |
| `prepare` sets `maxStepPerInterval_ = 375·√P0·kControlInterval/fs` (0.125594 at 48 kHz) | `profundum_core.h:142–147` |
| `void setControls(const SpectralShapeRecipe::Controls&) noexcept`: no-op before prepare; non-finite keeps the pending field; clamps; a bitwise-unchanged set returns without dirtying | `profundum_core.h:173–193` |
| `struct ExcitationControls { float mix = 0.0f; float drive = 0.5f; float asymmetry = 0.0f; }`; `static const ExcitationControls kDefaultExcitation;`, defined `inline constexpr … kDefaultExcitation{}` after the class | `profundum_core.h:334–340, 1192` |
| `kExcitationSmoothMs = 5.0f`; `void setExcitation(const ExcitationControls&) noexcept` (no-op before prepare) | `profundum_core.h:348, 356–366` |
| `[[nodiscard]] std::span<const float> shapeGains() const noexcept` | `profundum_core.h:295` |
| **Precedent for a nested-aggregate NSDMI member:** `ExcitationControls pendingExcitation_{};` inside the class that nests `ExcitationControls` compiles on every CI platform | `profundum_core.h:1177–1178` |
| `HarmonicFold::kAmountSmoothMs = 10.0f`; `void setAmount(float amount) noexcept`: "[0, 100]; non-finite keeps the previous value; a denormal becomes 0 … Valid before prepare" | `processors/harmonic_fold.h:92, 166–171` |
| `ShapeMorph::kShapeSmoothMs = 10.0f`; `void setShape(float position) noexcept` [0, 1]; `float shape_ = 0.0f; // ROUND by default`; `bool enabled_ = true` | `systems/shape_morph.h:107, 193–199, 620, 622` |
| `BassCharacterStage::Algorithm : std::uint8_t { Tube, Fold, Rectify, Bite, Crush, Fracture }`; `kNumAlgorithms = 6`; `kSwitchCrossfadeMs = 10.0f`; `kParamSmoothMs = 10.0f` | `systems/bass_character_stage.h:78–84` |
| `setAlgorithm(Algorithm)`: ignores out-of-range; while in flight the last request is queued (`pending_`), a request equal to `target_` cancels the queue; otherwise a switch starts if `a != current_` | `bass_character_stage.h:241–255` |
| `setAmount(float)` / `setTone(float)`: [0, 1], non-finite keeps the previous value; defaults `amount_ = 0.0f`, `tone_ = 0.5f`, `current_ = target_ = Algorithm::Tube` | `bass_character_stage.h:258–268, 1538–1539, 1555–1556` |
| `DropEnvelope`: `kMaxAmountSemitones = 48`, `kMinTimeMs = 1`, `kMaxTimeMs = 4000`, `kMin/kMaxSampleRate = 22050 / 192000`; defaults Amount 0 / Time 100 / Shape 0 / Rebound 0; setters are no-ops before prepare, non-finite keeps the previous value, "Latched at the next trigger (D-4)"; `trigger(bool snap)` latches; `currentOffset()` | `processors/drop_envelope.h:31–39, 80–98, 103–120, 148` |
| `kMinEnvelopeTimeMs = 0.1f`, `kMaxEnvelopeTimeMs = 10000.0f` | `primitives/envelope_utils.h:50–51` |
| `ADSREnvelope` setters `setAttack / setDecay / setRelease(float ms)` clamp to the envelope range, `setSustain` to [0, 1]; `setAttackCurve(float)` (and decay / release) clamps to [−1, 1] and **regenerates a 256-entry table** (`generatePowerCurveTable`) on every call | `primitives/adsr_envelope.h:115–136, 157–176` |
| `ADSREnvelope` member defaults: attack 10 ms, decay 50 ms, sustain 0.5, release 100 ms, curve amounts 0.7 ("Default matches EnvCurve::Exponential") | `adsr_envelope.h:521–533` |
| `BassStereoField::ImageMode : uint8 { Solid, Spread, Wide }`, `MotionSource : uint8 { Drift, Cycle }`; `kControlInterval = 32`; crossover [60, 250], default 120; `kModeCrossfadeMs = 10.0f` | `systems/bass_stereo_field.h:63–72, 91` |
| `struct Controls { ImageMode mode = Spread; float spread = 0.5f; gravity = 0.5f; crossoverHz = 120; curve = 0.5f; motionDepth = 0.0f; motionRate = 0.3f; MotionSource motionSource = Drift; bool cycleSync = false; NoteValue cycleNote = Whole; NoteModifier cycleModifier = None; }` | `bass_stereo_field.h:99–111` |
| `[[nodiscard]] static bool isValidControls(const Controls&) noexcept` is public; the `isDeclared(...)` helpers it uses are **private** | `bass_stereo_field.h:263–270, 504–519` |
| `setControls` latches until `advanceControl()`; `advanceControl()` runs at every multiple of 32 | `bass_stereo_field.h:234–257, 291–293` |
| `bass_stereo_field.h` already includes `primitives/lfo.h`, `core/note_value.h` and `systems/bass_character_stage.h` (so those headers already coexist in one TU) | `bass_stereo_field.h:36–45` |
| `NoteValue : uint8 { QuadrupleWhole = 0, …, Quarter = 5, …, SixtyFourth = 9 }`; `NoteModifier : uint8 { None, Dotted, Triplet }` | `core/note_value.h:38–58` |
| `lfo.h`, namespace scope: `kMinFrequency = 0.01f`, `kMaxFrequency = 20.0f`, `enum class Waveform : uint8_t { Sine, Triangle, Sawtooth, Square, SampleHold, SmoothRandom }`; `setPhaseOffset` wraps to [0, 360); `setFadeInTime(ms)` clamps only below (`std::max(0, ms)`) | `primitives/lfo.h:34–54, 225–233, 295–298` |
| `AnchorOscillator::Waveform : uint8 { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine }`, `Mode : uint8 { Lock, Follow, Punch, Ghost }`; `kDefaultLevel = 0.5f`; `kLevelSmoothMs = 5`, `kWaveformCrossfadeMs = 5`, `kModeBridgeMs = 5`; punch boost [0, 12] dB, punch time [5, 250] ms, ghost time [1, 100] ms | `systems/anchor_oscillator.h:57–58, 72–88` |
| `struct Controls { Waveform waveform = Sine; Mode mode = Lock; float level = kDefaultLevel; float punchBoostDb = 6.0f; float punchTimeMs = 40.0f; float ghostTimeMs = 15.0f; }`; `setControls` is a no-op before prepare, keeps the pending value for an undeclared enum, sanitises floats and skips a bitwise-unchanged set | `anchor_oscillator.h:99–106, 224–245` |
| `MassLaw`: `kMinMass 0`, `kMaxMass 100`, `kNeutralMass 50`, `kControlInterval 32`, `kMassSweepMs 40`; `struct Targets {depth, body, transientDurationMs, foldAmount, compressionAmount, anchorLevel, outputTrimDb}` | `systems/mass_law.h:70–90` |
| `AnchorLevel` row: lo 0, hi 1, amountDown 0.5, amountUp 0.5, Linear | `mass_law.h:110` |
| `static float evaluateRow(const Row&, float mass, float base) noexcept`: u = (mass − 50)·0.02; at Mass 50 a Linear row returns `clamp(base, lo, hi)` | `mass_law.h:136–142` |
| `prepare(double)` configures `slew_.configure(kMaxMass / kMassSweepMs, fs / kControlInterval)` **then calls `reset()`**; `setMass`, `setBase`, `advanceInterval()` | `mass_law.h:154–181` |
| `MassDynamics::setTransientAttackDb / setTransientSustainDb(float dB)`; `setTransientDurationMs`, `setCompressionAmount`, `setOutputTrimDb`, `setDetectorKeyHz` | `systems/mass_dynamics.h:504–531` |
| `TransientShaper::kMinGainDb = -12`, `kMaxGainDb = 12`, `kDefaultAttackDb = 6`, `kDefaultSustainDb = 0`, `kDefaultDurationMs = 40` | `processors/transient_shaper.h:78–84` |
| `SlewLimiter::configure(float ratePerMs, float sampleRate)`; rate per step = `calculateSlewRate(unitsPerMs, sampleRate) = unitsPerMs / (sampleRate · 0.001f)` | `primitives/smoother.h:114–121, 512–514` |
| `SlewLimiter::setTarget`: NaN snaps **both** current and target to 0; ±Inf becomes ±1e10 | `smoother.h:519–530` |
| `SlewLimiter::process()`: `delta > rise → current += rise`; `delta < −fall → current −= fall`; else `current_ = target_` (exact landing); then `flushDenormal(current_)` | `smoother.h:545–561` |
| `SlewLimiter::snapTo(float)` sets current and target; `configure` never moves current or target | `smoother.h:497–514, 591–602` |
| `detail::isFinite(float)` / `(double)` and `detail::isNaN` are fast-math-immune (opaque bit barrier) | `core/db_utils.h:99–129` |
| `detail::flushDenormal(float)` zeroes any value whose magnitude is below `kDenormalThreshold`; `kDenormalThreshold = 1e-15f` (namespace `Krate::DSP`, not FLT_MIN) | `core/db_utils.h:38, 263–264` |
| `serializeSpectralState` / `deserializeSpectralState`: version byte at 0, named constant offsets in `detail::state_io`, `std::memcpy` of bit patterns, `static_assert(std::endian::native == std::endian::little, …)`, decode into a local and copy only on success | `processors/spectral_state.h:178–300` |
| `equalPowerGains(float, float&, float&)` is cos / sin of `position·π/2`: an audio-gain pair | `core/crossfade_utils.h:50–52` |
| Test grid: `MassDynamicsTest::kGrid`, `kSettleSeconds = 1.0`, `descriptorSeconds`, `renderLength`, `gridDriver(total, hostBlock, fn(start, len, atGrid))` | `dsp/tests/unit/systems/mass_dynamics_test_helpers.h:80–110` |
| `ChainConfig`, `coreControlsFor(base, Targets)`, `unbiasedH1Amplitude(base, f0, fs)` (= `evaluate(...)[0] · kCenterPanGain`), `processChainTail`, `makeChainDynamics`, `prepareChainLaw` | `mass_dynamics_test_helpers.h:173–196, 274–289, 302–370` |
| `SubBand` (`lpRmsDb` = one-period RMS of a 4th-order Butterworth LP at 1.5·f0), `subBand(y, n, fs, f0)` | `mass_dynamics_test_helpers.h:545–552, 641` |
| `StereoFieldTest::kFloorDb = -120`, `kSubBandQBarDb = 1.0`, `kSubBandRhoBar = 0.99`, `bitEqual(vector, vector)` | `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h:89–110` |
| `FieldChainConfig` (embeds `MassDynamicsTest::ChainConfig chain`), `FieldChainTaps : MassDynamicsTest::ChainTaps` (adds the character and stageOut taps), `makeField`, `advanceFieldAt`, `allocateFieldTaps`, `processStageAndOutput`, `processFieldTail` (sum = **character tap + anchor tap**) | `bass_stereo_field_test_helpers.h:114–265` |
| `renderFieldChain` order: core → ShapeMorph → HarmonicFold → BassCharacterStage; ShapeMorph and the character stage are configured **before** prepare (snapped); WIDE offsets `field->wideOffsets(alg)` → `character->setChannelOffsets`; pan vector → `core->setPartialPanOffsets` | `bass_stereo_field_test_helpers.h:267–425` |
| `BandReading { q, rho, sideToMidDb, meanEnergy, valid }`, `bandReading(L, R, n, fs, fLo, fHi)`; `evaluateMonoSumCell(const FieldChainTaps&, const FieldChainConfig&)`, `monoSumCellPasses`; `wideAliasedDbfs(y, fs, f0, alg)` | `bass_stereo_field_test_helpers.h:711–741, 998–1068, 1120–1126` |
| `ShapeCharacterTest::maxDeltaAround(x, n, i, N)` and `differentialClicks(transition, refStart, refEnd, n, fs, f0)` | `dsp/tests/unit/processors/shape_character_metric_helpers.h:53, 76` |
| `ProfundumTest::renderedHarmonicPowers(x, fs, f0, N)`, `describeRendered(x, fs, f0, N)` (`eSub`, `eTotal`, `centroidOct`) | `dsp/tests/unit/systems/profundum_core_test_helpers.h:235, 251` |
| `ProfundumTest::shapeOf(controls, N)` = `evaluateShape` | `dsp/tests/unit/processors/spectral_shape_recipe_test_helpers.h:168–172` |
| `anchorMidiHz(int)` | `dsp/tests/unit/systems/anchor_oscillator_test_helpers.h:88` |
| Phase 3 fold feed: `out = HarmonicFold(core + g·A) + A`, the Anchor added **before** the Fold and scaled by g (a static g in that fixture) | `dsp/tests/unit/systems/profundum_anchor_chain_test.cpp:1015–1062` |
| `kWeightedLevelDb(x, n, fs, measureFrom)` | `tests/test_helpers/k_weighting.h:111` |
| `TestUtils::kSampleTolerance = 5e-4f`, `kMetricTolerance = 2.5e-4` | `tests/test_helpers/render_fingerprint.h:58–61` |
| `TestHelpers::AllocationScope` (used by the Phase 7 RT test) | `tests/test_helpers/allocation_detector.h:111`; `bass_stereo_field_test.cpp:3063` |
| Phase 7 TUs are enumerated in `dsp_systems_tests` at `dsp/tests/CMakeLists.txt:743–763`; the `-fno-fast-math` block lists `bass_stereo_field_nonfinite_test.cpp` at 1259 and closes with `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at 1260 | `dsp/tests/CMakeLists.txt` |
| Gate filter: every Profundum TEST_CASE carries `[profundum]`; gates run `'[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'` | `.claude/workflows/profundum-phase.js:549–550` |

**ODR sweep for the names this plan adds beyond the spec's table** (`grep -rnw <name> dsp plugins tests tools`,
`.h/.cpp/.hpp/.js`, 2026-10-19): `forEachMorphLeaf`, `kFieldTable`, `kLeafOffsets`, `FieldInfo`, `SelectionGroup`,
`leafBitsEqual`, `kThetaUp`, `kPunchExponent`, `MorphChainTaps`, `randomValidState`, `morph_state_io`, `BassMorphTest`,
`MorphState`, `BassMorphEngine`: **0 hits each**. The nested names (`Law`, `Axis`, `Kind`, `SelectionGroup`,
`FieldInfo`, `Source`, `Target`, `Polarity`) live inside `MorphState` or `MorphState::MotionConfig` and add no
namespace-scope name; the namespace-scope helpers live in the new `detail::morph_state_io`.

---

## S1. Spec conflicts and refinements

### C-1 (refinement, RESOLVED by the main loop 2026-10-19, FR-030): the field-descriptor table lives in `morph_state.h`

FR-030 puts the law table "in `BassMorphEngine`" and, in the same bullet, requires that the serializer's offsets in
`detail::morph_state_io` are derived from it and that `static_assert(kMorphStateBytes == 1 + Σ row sizes)` holds **in
`morph_state.h`**. `morph_state.h` cannot see a table declared in `bass_morph_engine.h` (which includes it). Resolution:

- `Law`, `Axis`, `Kind` and `SelectionGroup` are nested enums of `MorphState` (nested types do not affect the aggregate,
  trivially-copyable or structured-binding properties).
- The one table is `detail::morph_state_io::kFieldTable` (namespace scope, after `MorphState` is complete).
- `BassMorphEngine` re-exports it: `using Law = MorphState::Law; using Axis = MorphState::Axis;` and
  `static constexpr const auto& kLawTable = detail::morph_state_io::kFieldTable;`. The spec spellings
  `BassMorphEngine::Law` / `BassMorphEngine::Axis` therefore compile, and there is still exactly one table.

### C-2 (refinement, RATIFIED 2026-10-19, FR-034 rule 3, FR-062): one selection state per governing-amount group

FR-034 rule 3 says that when rule 2 stops applying inside the band the field keeps "the selection rule 2 had forced".
Rule 2 is per governing amount, so two discrete fields can be forced to different sides at the same p (showcase pair at
p = 0.3: the character algorithm is forced to B because A's Amount is 0, while the `motion.*` selectors, whose amounts
are 0 on both sides, sit on hysteresis at A). One shared "main selection" cannot remember both. The plan keeps one
selection **per group**:

| Group | Discrete fields | Governing amount | Position |
|---|---|---|---|
| `Character` | `characterAlgorithm` | `characterAmount` | p |
| `ImageMode` | `field.mode` | `field.spread` | p |
| `FieldMotion` | `field.motionSource`, `field.cycleSync`, `field.cycleNote`, `field.cycleModifier` | `field.motionDepth` | p |
| `Motion` | `motion.source`, `target`, `cycleShape`, `cycleSync`, `cycleNote`, `cycleModifier`, `cycleRetrigger`, `polarity` | `motion.amount` | p |
| `Anchor` | `anchor.waveform`, `anchor.mode` | `anchor.level` | p_a |

FR-062's byte count is kept: the runtime blob still carries "the main selection and the Anchor selection, one
`std::uint8_t` each". The main byte is a bitmask (bit `groupIndex(g)` = 1 means group g selects B, S4.2; g ∈ {Character, ImageMode,
FieldMotion, Motion}; valid values 0x00–0x0F); the Anchor byte is 0 or 1. The update rule (S4.5) and the restore check
(S4.8) are per group. With one shared selection the spec's own rule-3 sentence could not hold, so this is the reading
that satisfies FR-034 as written.

### C-3 (refinement, FR-050 vs FR-062): p_a's target is c_m(ramped p), recomputed every interval

FR-050 says "p_a's target is c_m(p)" (p is the ramped value by the descriptor block's definition); FR-062 says "The p_a
target is not stored, because it is c_m(target)". The plan uses **c_m of the ramped p after this interval's step**:

```
p   ← posSlew_.process()                 // rate-limited toward the target
p_a ← anchorSlew_.setTarget(c_m(p)); anchorSlew_.process()
```

Consequences, all intended: under host automation (target ≈ p) the two readings coincide; under a jump, Punch keeps its
designed late-yielding shape *during* the ramp (c′(p) = 3p² exceeds 1 near p = 1, so p_a lags c(p) there and catches up
after p lands, never stepping); Blend gives p_a == p bitwise (S4.4). Both readings derive p_a's target from stored
values, so FR-062's "not stored" holds either way.

### C-4 (amendment, RATIFIED 2026-10-19, FR-031, FR-032, FR-033): every law is evaluated in double and rounded once

FR-031 says linear is "evaluated in float". Float evaluation of `x_A + p·(x_B − x_A)` has a worst-case error of
0.5 ulp(d) + 0.5 ulp(p·d) + 0.5 ulp(result) with |d| up to 2·max(|x_A|, |x_B|), i.e. up to **2.5 ulp** of max(|x_A|, |x_B|)
(opposite-sign endpoints such as `shift` −1 → +1). That exceeds SC-019(b)'s 2-ulp bar. In double the evaluation is
**not** claimed exact: `x_B − x_A` is exact only when the endpoints' exponents lie within about 29 binades of each
other (a denormal x_A against x_B = 100, both accepted by the validators, is not), and `p·(x_B − x_A)` and the sum each
round once more. Each double operation rounds by at most 2^−53 relative, so after the single rounding to float the
result is within **0.5 ulp(float) plus a double-rounding error of order 2^−53 relative** to max(|x_A|, |x_B|) (a
small constant multiple, about 2^−29 ulp(float)), far inside SC-019(b)'s 2-ulp bar. The same bound holds for
equal-power (the radicand's products and sum each round once in double; `std::sqrt` is correctly rounded). The log law caches
`log2(x + o)` of both endpoints in double at `setEndpoint` (S4.6), so each interval costs one `std::exp2(double)` per
differing log field. The stored and returned values are floats, as FR-031 requires of the state. Cost: S10.

### C-5 (refinement, FR-011): `prepare` ends with `reset()`

`MassLaw::prepare` configures its slew and then calls `reset()` (mass_law.h:154–159). `BassMorphEngine::prepare` does
the same: clamp fs to [22 050, 192 000], configure both rate limiters at the interval rate, set `prepared_`, `reset()`.
The endpoints, the position target, the mode and the ramp time are preserved (Edge case "Sample-rate change").

### C-6 (refinement, FR-015, FR-082, SC-018): resolution is cached

The resolver is a pure function of (A, B, p, p_a, the five group selections). `advanceInterval` re-resolves only when one
of them changed bitwise since the last resolve (a `dirty_` flag set by `setEndpoint`, `restoreRuntime` and any change of
p, p_a or a selection). Determinism and partition invariance are unaffected (same inputs, same outputs). SC-018 measures
the uncached worst case (every interval moving, every field differing).

### C-7 (refinement, FR-013, FR-022, FR-050): position and Anchor-curve targets below the flush threshold are +0

`SlewLimiter::process()` lands on the target (`current_ = target_`, smoother.h:553) and then flushes `current_` through
`detail::flushDenormal` (smoother.h:557). `flushDenormal` zeroes every |x| < `kDenormalThreshold` (db_utils.h:263–264),
and `kDenormalThreshold` is **1e-15f** (db_utils.h:38, at `Krate::DSP` namespace scope, not in `detail`), not FLT_MIN
(≈ 1.18e-38). A target in (0, 1e-15) therefore leaves `current_ = 0 ≠ target_` for good: FR-022's bitwise landing fails
and `isComplete()` (smoother.h:580) never turns true. `snapTo` does not flush (smoother.h:591–602), so a snapped
sub-threshold value is published once and flushed by the next `process()`, a step with no position change. (An earlier
draft of this entry used FLT_MIN; review 2026-10-19 corrected it.) The plan normalises against the real threshold, in
**both** slews:

- `setPosition(p)`: non-finite → return; `p = (p < kDenormalThreshold) ? 0.0f : (p >= 1.0f ? 1.0f : p);` — this catches −0,
  negative values, denormals of either sign and every normal value below 1e-15. `restoreRuntime` rejects such stored
  values instead of normalising them (S4.8).
- `curve(m, p)` returns its raw value through `detail::flushDenormal` (S4.4). The Punch target p³ is below 1e-15 for
  every landed p < 1e-5; flushed, it and the `reset()` `snapTo(curve(mode_, target_))` value are already +0, so p_a lands
  bitwise on c(p) and `reset()` publishes the p_a the next `process()` keeps. Blend's c(p) = p is already +0 or ≥ 1e-15
  by the first bullet, so p_a == p bitwise still holds. The flush is monotone on [0, 1] and fixes 0 and 1, so FR-051's
  c(0) = 0, c(1) = 1, monotone c and c(p) ≤ p are unaffected.

### C-8 (refinement, FR-004): amp-envelope defaults are `ADSREnvelope`'s member defaults

FR-004 does not list them. `AmpEnvelopeFields` defaults to attack 10 ms, decay 50 ms, sustain 0.5, release 100 ms and
curve amounts 0.7 (adsr_envelope.h:521–533). Phase 9 handoff (S12): the class's default runs `EnvCurve::Exponential`
without the table, while `setAttackCurve(0.7f)` switches to table processing; Phase 9 states which one it treats as the
neutral value.

### C-9 ⚠ RISK (SC-008(d) Blend / Punch, SC-015 Anchor clause): the sum-tap sub band depends on the core's h1 at the character tap

E_sub is read on the **sum** tap (character tap + Anchor). The Anchor's a1 equals the resolved recipe's unbiased h1
(FR-070), so at the character tap the core contributes c·e^{iθ} relative to an Anchor at level 1 (c, θ unknown after Fold
80 + character Fold 0.8 + SHAPE BITE). Predictions (in-phase arithmetic, then the general |·|):

- SC-015 Anchor clause, Lock (1.0) against Blend (0.7) at p = 1: 20·log10(|1 + c·e^{iθ}| / |0.7 + c·e^{iθ}|) ≥ 2 dB. In phase
  this holds only for **c ≤ 0.46**.
- SC-008(d) Blend at p = 1: |0.7 + c·e^{iθ}| ≥ 10^(−1/20) = 0.891 (F_lock = 1.0). In phase it holds for c ≥ 0.19; near
  anti-phase it fails for c ≈ 0.7.

Neither c nor θ has been measured on this chain. **Mitigation:** task group 1 runs a hidden probe
(`BassMorph_SumTapCoherenceProbe`, `[.]`) that renders the showcase pair at p ∈ {0, 0.5, 1} at E1 and C3, reports c and θ
at the character tap and the predicted SC-015 / SC-008(d) margins, and writes them to `evidence/coherence-probe.txt`.
If any prediction is red the build **stops and asks** (no relaxation). The remediation space the spec leaves open
(Q7: "remediated in the interpolation laws or the showcase pair") is the showcase pair's B (e.g. its `anchor.level`, Fold
amount or SHAPE); the concept example 1.0 → 0.7 stays on SC-005's own pair, which is unaffected.

### C-10 (refinement, SC-004): the base state and the direct-setter reference

- **Base state** for every SC-004 case: the FR-004 default `MorphState` with `characterAmount = 0.8` on both sides (at
  the default 0 the stage is bypassed and every algorithm pair would pass trivially), `anchor.level = 1.0` on both sides,
  everything else at the defaults; the cycle cases set `field.motionSource = Cycle`, `cycleSync = true`,
  `motionDepth = 1` as the spec says. Both governing amounts are then non-zero, so rule 3 (θ_up = 0.52) decides.
- **Direct-setter reference:** the same fixture with p held at 0 and an automation event at the grid sample g_sw where
  the transition render's selection switched, replacing **both** endpoints with A′ = A with that one discrete field set to
  B's value. At p = 0 the identity rule resolves A′ at g_sw, so the component receives B's value through exactly the same
  setter path at the same grid point.

### C-11 (refinement, FR-070, Edge case "Equal-power with one endpoint at 0"): the fold-feed gain is ramped in the fixture

The spec's edge case names "the receiving smoothers (5 ms excitation, Anchor feed)". No Anchor-feed smoother exists: the
Phase 3 fixture applies a static g (profundum_anchor_chain_test.cpp:1015–1062). The FR-070 fixture applies
`anchorFoldFeed` as a per-sample linear ramp from the previous interval's value to the new one across each 32-sample
interval (test code). Phase 9 handoff (S12): the voice owns a feed smoother.

### C-12 (refinement, SC-005, SC-006, SC-024): splitting fast and render-bound cases

The engine-level clauses run in the per-push lane; the render-bound clauses that exceed ~15 s get `[long]`:
`BassMorph_AnchorCurves` (a, b, c, e engine part) and `BassMorph_AnchorCurvesRendered` (a's E_sub, d, e's D_nl,
`[long]`); `BassMorph_Idempotent` (a) and `BassMorph_IdempotentRender` (b, `[long]`); `BassMorph_EndpointEditMidMorph`
(continuous) and `BassMorph_EndpointEditMidMorphRender` (discrete render). The test-name prefixes match the spec's
sketches, so the traceability holds.

### C-13 (refinement, FR-006): a `bool` byte other than 0 / 1 is rejected

Reading a byte with value 2 into a `bool` through `std::memcpy` is undefined. `deserializeMorphState` reads every enum and
bool leaf as `std::uint8_t`; a bool byte > 1 rejects the stream; an enum byte > the row's `maxEnum` fails validation.

### C-14 (refinement, FR-012): `endpoint(e)` with an undeclared `e` returns A

`setEndpoint` with an undeclared `e` returns false (FR-012). The const accessor cannot fail, so an undeclared value reads A.

### C-15 (adoption): drafted values adopted as written

SC-008(a) ε = 0.25 dB (dB descriptors), 0.02 oct (D_br), 2 % (D_t); SC-015 bars 3 dB / 0.25 oct / ratio 2 / Lock − Blend
≥ 2 dB; D-9 click bound 10^(0.5/20); D-10 CPU ≤ 0.05 % of one core at 48 kHz; D-11 crossover logarithmic; D-12 transient
gains included, linear in dB. None is relaxed by this plan.

### C-16 (refinement, RATIFIED 2026-10-19, SC-017(a)(b), FR-086, FR-034 rule 2): the algorithm-crossing arms use a pair whose governing amounts are both non-zero

SC-017's max-nonlinear pair has A at Character Tube, **Amount 0**, and B at Fracture, Amount 1. Under FR-034 rule 2
(S4.5), exactly one governing amount is 0, so B's algorithm is forced at every q ∈ (0, 1), and `reset()` applies the same
rule. The two "arms of the algorithm crossing" SC-017(a) asks for (p = θ_up − 0.005 with A's algorithm selected, p = θ_up
with B's) therefore both render Fracture on that pair: the A arm cannot occur. Its real switch is at the first interval
with p > 0, with Tube at Amount ≈ 0, so the pair never crossfades between two non-zero-amount nonlinear arms, which is
what FR-086's "through the algorithm crossfade" is meant to cover. The Q1 ruling (rule 2) came after SC-017 was drafted;
this entry resolves the contradiction without relaxing any bar:

- The 21-point grid stays on `maxNonlinearPair` exactly as the spec words it (every p ∈ (0, 1) reads Fracture there).
- The two crossing arms are measured on a second pair, `algorithmCrossingPair()` (S5.1): both sides at the max-nonlinear
  settings (Fold 100, excitation mix 1, drive 1, Character Amount 1, Tone 0.5), A Tube and B Fracture. Both governing
  amounts are non-zero, so rule 3 switches at θ_up. Before reading aliasing the test asserts
  `selection(Character) == A` at θ_up − 0.005 reached from below and `== B` at θ_up; each arm is then a frozen steady
  render under the same −80 dBFS bar.
- SC-017(b)'s recorded Hann window is placed at the switch grid sample g_sw of `algorithmCrossingPair`'s 1 s ramp (the
  recorded `discreteSetterCalls` entry), spanning [g_sw, g_sw + one period of character pre-roll + 10 ms crossfade]. The
  same record is also written for `maxNonlinearPair`, whose switch sits at its first interval with p > 0.

The alternative, ratifying the removal of the two arm points for this pair because rule 2 removes the crossing, is
rejected: it would leave FR-086's crossfade between two live nonlinear arms ungated.

---

## S2. Architecture

```
Phase 9 voice / engine (later)                     this phase
──────────────────────────────                     ─────────────────────────────────────────────
host Morph position ─► setPosition ─┐
ramp ms ───────────────► setPositionRampMs        BassMorphEngine (L3, control domain only)
anchor morph mode ─────► setAnchorMorphMode        ├─ A, B : MorphState (validated snapshots)
Edit A / Edit B ───────► setEndpoint(A|B, s)       ├─ posSlew_   (SlewLimiter @ fs/32)
                                                   ├─ anchorSlew_ (SlewLimiter @ fs/32, target c_m(p))
every 32 samples ──────► advanceInterval() ───────►├─ group selections (5, hysteresis + governing amounts)
                                                   └─ resolve(): per-leaf law from kFieldTable
                                     │
                                     ▼  const Resolved& { MorphState state; p; p_a; anchorMass }
          ProfundumCore::setControls / setExcitation, HarmonicFold::setAmount, ShapeMorph::setShape,
          BassCharacterStage::setAlgorithm/Amount/Tone, AnchorOscillator::setControls, DropEnvelope setters,
          ADSR setters (change-only), BassStereoField::setControls + advanceControl, MassLaw base/mass,
          MassDynamics transient gains   ← the voice (Phase 9) or the FR-070 test fixture (this phase)
```

- **Layer and includes.** `morph_state.h` (L3) includes L0 `core/db_utils.h`, `core/note_value.h`; L1
  `primitives/envelope_utils.h`, `primitives/lfo.h`; L2 `processors/spectral_shape_recipe.h`,
  `processors/drop_envelope.h`, `processors/transient_shaper.h`; L3 peers `systems/profundum_core.h`,
  `systems/bass_character_stage.h`, `systems/bass_stereo_field.h`, `systems/anchor_oscillator.h`, `systems/mass_law.h`
  (aggregates, enums and range constants only). `bass_morph_engine.h` (L3) includes `systems/morph_state.h`,
  `core/db_utils.h`, `primitives/smoother.h`. No L4 include; no processing method of any component is called.
- **No audio path** (FR-010, FR-038): no buffer parameter, no `process*` member; SC-009(a) asserts it with
  `requires`-expressions.
- **Global** (FR-016): one engine instance per instrument; voices read `resolved()`.

---

## S3. `MorphState` (FR-001–FR-007) — `systems/morph_state.h`

### S3.1 Type

```cpp
namespace Krate::DSP {

struct MorphState {
    // ---- nested vocabulary (C-1) ----
    enum class Law : std::uint8_t { Linear, Logarithmic, EqualPower, Stepped };
    enum class Axis : std::uint8_t { SineComplex, CleanNonlinear, MonoStereo, ShortLong, DarkBright, Anchor, Motion };
    enum class Kind : std::uint8_t { F32, Enum8, Bool };
    enum class SelectionGroup : std::uint8_t { None, Character, ImageMode, FieldMotion, Motion, Anchor };
    static constexpr std::size_t kNumSelectionGroups = 5;   // excluding None
    // The ONE enum -> array-index / bit mapping (S4.2): None = 0 is not a slot. Precondition g != None.
    [[nodiscard]] static constexpr std::size_t groupIndex(SelectionGroup g) noexcept {
        return static_cast<std::size_t>(g) - 1;
    }

    struct AmpEnvelopeFields {                     // FR-002 (Q4); defaults C-8
        float attackMs = 10.0f;                    // [kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs]
        float decayMs = 50.0f;
        float sustain = 0.5f;                      // [0, 1]
        float releaseMs = 100.0f;
        float attackCurve = 0.7f;                  // [-1, +1]
        float decayCurve = 0.7f;
        float releaseCurve = 0.7f;
    };

    struct MotionConfig {                          // FR-002 MotionConfig table (Q4), exact member order
        enum class Source : std::uint8_t { Off, Env, Cycle, Drift };
        enum class Target : std::uint8_t { Mass, Depth, Body, Edge, Shift };
        enum class Polarity : std::uint8_t { Bipolar, Unipolar };
        Source source = Source::Off;
        Target target = Target::Mass;
        float amount = 0.0f;                       // [-1, 1]
        float depth = 1.0f;                        // [0, 1]
        float cycleRateHz = 1.0f;                  // [kMinFrequency, kMaxFrequency] (lfo.h:34-35)
        ::Krate::DSP::Waveform cycleShape = ::Krate::DSP::Waveform::Sine;   // lfo.h:47; no new Waveform
        bool cycleSync = false;
        NoteValue cycleNote = NoteValue::Quarter;
        NoteModifier cycleModifier = NoteModifier::None;
        float cyclePhaseDeg = 0.0f;                // [0, 360)
        bool cycleRetrigger = true;
        float cycleFadeInMs = 0.0f;                // [0, 5000], log law on (t + 1 ms)
        Polarity polarity = Polarity::Bipolar;
        float driftSmoothness = 0.5f;              // [0, 1]
        float envAttackMs = 10.0f;                 // [0.1, 10000]
        float envDecayMs = 300.0f;                 // [0.1, 10000]
    };

    SpectralShapeRecipe::Controls recipe = SpectralShapeRecipe::kDefaultControls;
    ProfundumCore::ExcitationControls excitation = ProfundumCore::kDefaultExcitation;
    float foldAmount = 0.0f;                                         // [0, 100]
    float shape = 0.0f;                                              // [0, 1]; ShapeMorph reset value (shape_morph.h:620)
    BassCharacterStage::Algorithm characterAlgorithm = BassCharacterStage::Algorithm::Tube;
    float characterAmount = 0.0f;                                    // [0, 1]
    float characterTone = 0.5f;                                      // [0, 1]
    float dropAmountSt = DropEnvelope::kDefaultAmountSemitones;      // [0, 48]
    float dropTimeMs = DropEnvelope::kDefaultTimeMs;                 // [1, 4000]
    float dropShape = DropEnvelope::kDefaultShape;                   // [0, 1]
    float dropRebound = DropEnvelope::kDefaultRebound;               // [0, 1]
    AmpEnvelopeFields ampEnv{};
    BassStereoField::Controls field{};
    MotionConfig motion{};
    float mass = MassLaw::kNeutralMass;                              // [0, 100]
    float transientAttackDb = TransientShaper::kDefaultAttackDb;     // [-12, 12]
    float transientSustainDb = TransientShaper::kDefaultSustainDb;   // [-12, 12]
    AnchorOscillator::Controls anchor{};
    float anchorFoldFeed = 0.0f;                                     // [0, 1]
};
static_assert(std::is_trivially_copyable_v<MorphState>);
static_assert(std::is_aggregate_v<MorphState>);
static_assert(MorphState::groupIndex(MorphState::SelectionGroup::Character) == 0
              && MorphState::groupIndex(MorphState::SelectionGroup::Motion) == 3
              && MorphState::groupIndex(MorphState::SelectionGroup::Anchor) == MorphState::kNumSelectionGroups - 1);
```

- 19 top-level members; **62 leaf fields** (46 floats, 13 enums, 3 bools): `kMorphLeafFieldCount = 62`.
- The nested-aggregate NSDMIs `ampEnv{}`, `field{}`, `motion{}`, `anchor{}` follow the precedent at
  profundum_core.h:1177. **No** `static constexpr MorphState` is declared inside the struct (the R-7 hazard); named
  states (`showcase…`) live in test code only.
- The order of members is the serialised order. It is frozen by FR-007 and by SC-019(a)'s structured bindings.

### S3.2 Field-descriptor table (FR-030, C-1)

```cpp
// nested in MorphState (declared after the enums, before the data members):
struct FieldInfo {
    const char* name;             // "recipe.depth" — used by the FR-003 exclusion test
    Kind kind;
    Law law;
    Axis axis;
    SelectionGroup group;         // Stepped rows only; None otherwise
    bool anchorBlock;             // resolves at p_a (FR-050)
    float lo, hi;                 // F32 rows; the box is closed except hiOpen
    bool hiOpen;                  // motion.cyclePhaseDeg only: [0, 360)
    float logOffset;              // FR-032 o; 1 ms for motion.cycleFadeInMs, else 0
    std::uint8_t maxEnum;         // Enum8 rows: last declared enumerator; Bool: 1
    std::uint8_t bytes;           // 4 (F32) or 1 (Enum8, Bool)
};
```

`FieldInfo` is declared as a nested struct of `MorphState` (aggregate, all members initialised by designated
initialisers). The table `inline constexpr std::array<MorphState::FieldInfo, kMorphLeafFieldCount>
detail::morph_state_io::kFieldTable` is defined after `MorphState`. The rows, in member order (offset = byte offset in
the version-1 stream, derived, listed here so a reorder is a visible edit):

| # | Leaf | Kind | Law | Axis | Group | Range / maxEnum | Offset |
|---|---|---|---|---|---|---|---|
| 0 | recipe.depth | F32 | Linear | SineComplex | — | [0, 1] | 1 |
| 1 | recipe.body | F32 | Linear | SineComplex | — | [0, 1] | 5 |
| 2 | recipe.bodyCurvature | F32 | Linear | SineComplex | — | [0, 1] | 9 |
| 3 | recipe.bodyEmphasis | F32 | Linear | SineComplex | — | [−1, 0] | 13 |
| 4 | recipe.edge | F32 | Linear | DarkBright | — | [0, 1] | 17 |
| 5 | recipe.shift | F32 | Linear | DarkBright | — | [−1, 1] | 21 |
| 6 | excitation.mix | F32 | EqualPower | CleanNonlinear | — | [0, 1] | 25 |
| 7 | excitation.drive | F32 | Linear | CleanNonlinear | — | [0, 1] | 29 |
| 8 | excitation.asymmetry | F32 | Linear | CleanNonlinear | — | [0, 1] | 33 |
| 9 | foldAmount | F32 | Linear | CleanNonlinear | — | [0, 100] | 37 |
| 10 | shape | F32 | Linear | DarkBright | — | [0, 1] | 41 |
| 11 | characterAlgorithm | Enum8 | Stepped | CleanNonlinear | Character | 5 | 45 |
| 12 | characterAmount | F32 | Linear | CleanNonlinear | — | [0, 1] | 46 |
| 13 | characterTone | F32 | Linear | DarkBright | — | [0, 1] | 50 |
| 14 | dropAmountSt | F32 | Linear | ShortLong | — | [0, 48] | 54 |
| 15 | dropTimeMs | F32 | Logarithmic | ShortLong | — | [1, 4000] | 58 |
| 16 | dropShape | F32 | Linear | ShortLong | — | [0, 1] | 62 |
| 17 | dropRebound | F32 | Linear | ShortLong | — | [0, 1] | 66 |
| 18 | ampEnv.attackMs | F32 | Logarithmic | ShortLong | — | [0.1, 10000] | 70 |
| 19 | ampEnv.decayMs | F32 | Logarithmic | ShortLong | — | [0.1, 10000] | 74 |
| 20 | ampEnv.sustain | F32 | Linear | ShortLong | — | [0, 1] | 78 |
| 21 | ampEnv.releaseMs | F32 | Logarithmic | ShortLong | — | [0.1, 10000] | 82 |
| 22 | ampEnv.attackCurve | F32 | Linear | ShortLong | — | [−1, 1] | 86 |
| 23 | ampEnv.decayCurve | F32 | Linear | ShortLong | — | [−1, 1] | 90 |
| 24 | ampEnv.releaseCurve | F32 | Linear | ShortLong | — | [−1, 1] | 94 |
| 25 | field.mode | Enum8 | Stepped | MonoStereo | ImageMode | 2 | 98 |
| 26 | field.spread | F32 | Linear | MonoStereo | — | [0, 1] | 99 |
| 27 | field.gravity | F32 | Linear | MonoStereo | — | [0, 1] | 103 |
| 28 | field.crossoverHz | F32 | Logarithmic | MonoStereo | — | [60, 250] | 107 |
| 29 | field.curve | F32 | Linear | MonoStereo | — | [0, 1] | 111 |
| 30 | field.motionDepth | F32 | Linear | MonoStereo | — | [0, 1] | 115 |
| 31 | field.motionRate | F32 | Linear | MonoStereo | — | [0, 1] | 119 |
| 32 | field.motionSource | Enum8 | Stepped | MonoStereo | FieldMotion | 1 | 123 |
| 33 | field.cycleSync | Bool | Stepped | MonoStereo | FieldMotion | 1 | 124 |
| 34 | field.cycleNote | Enum8 | Stepped | MonoStereo | FieldMotion | 9 | 125 |
| 35 | field.cycleModifier | Enum8 | Stepped | MonoStereo | FieldMotion | 2 | 126 |
| 36 | motion.source | Enum8 | Stepped | Motion | Motion | 3 | 127 |
| 37 | motion.target | Enum8 | Stepped | Motion | Motion | 4 | 128 |
| 38 | motion.amount | F32 | Linear | Motion | — | [−1, 1] | 129 |
| 39 | motion.depth | F32 | Linear | Motion | — | [0, 1] | 133 |
| 40 | motion.cycleRateHz | F32 | Logarithmic | Motion | — | [0.01, 20] | 137 |
| 41 | motion.cycleShape | Enum8 | Stepped | Motion | Motion | 5 | 141 |
| 42 | motion.cycleSync | Bool | Stepped | Motion | Motion | 1 | 142 |
| 43 | motion.cycleNote | Enum8 | Stepped | Motion | Motion | 9 | 143 |
| 44 | motion.cycleModifier | Enum8 | Stepped | Motion | Motion | 2 | 144 |
| 45 | motion.cyclePhaseDeg | F32 | Linear | Motion | — | [0, 360) | 145 |
| 46 | motion.cycleRetrigger | Bool | Stepped | Motion | Motion | 1 | 149 |
| 47 | motion.cycleFadeInMs | F32 | Logarithmic (o = 1) | Motion | — | [0, 5000] | 150 |
| 48 | motion.polarity | Enum8 | Stepped | Motion | Motion | 1 | 154 |
| 49 | motion.driftSmoothness | F32 | Linear | Motion | — | [0, 1] | 155 |
| 50 | motion.envAttackMs | F32 | Logarithmic | Motion | — | [0.1, 10000] | 159 |
| 51 | motion.envDecayMs | F32 | Logarithmic | Motion | — | [0.1, 10000] | 163 |
| 52 | mass | F32 | Linear | CleanNonlinear | — | [0, 100] | 167 |
| 53 | transientAttackDb | F32 | Linear | ShortLong | — | [−12, 12] | 171 |
| 54 | transientSustainDb | F32 | Linear | ShortLong | — | [−12, 12] | 175 |
| 55 | anchor.waveform | Enum8 | Stepped | Anchor | Anchor (p_a) | 4 | 179 |
| 56 | anchor.mode | Enum8 | Stepped | Anchor | Anchor (p_a) | 3 | 180 |
| 57 | anchor.level | F32 | Linear (p_a) | Anchor | — | [0, 1] | 181 |
| 58 | anchor.punchBoostDb | F32 | Linear (p_a) | Anchor | — | [0, 12] | 185 |
| 59 | anchor.punchTimeMs | F32 | Logarithmic (p_a) | Anchor | — | [5, 250] | 189 |
| 60 | anchor.ghostTimeMs | F32 | Logarithmic (p_a) | Anchor | — | [1, 100] | 193 |
| 61 | anchorFoldFeed | F32 | EqualPower (p_a) | Anchor | — | [0, 1] | 197 |

`kMorphStateBytes = 201` (= 1 + 46·4 + 16·1). Every range is written with the source constant, not a literal, where one
exists (`kMinEnvelopeTimeMs`, `DropEnvelope::kMinTimeMs`, `BassStereoField::kMinCrossoverHz`,
`AnchorOscillator::kMinPunchTimeMs`, `kMinFrequency`, `TransientShaper::kMinGainDb`, `MassLaw::kMaxMass`, …); the literals
are 100 (Fold, harmonic_fold.h:166–168), 5000 (fade-in, drafted D-14), 360 and the [−1, 1] / [0, 1] boxes.

### S3.3 Leaf visitor and compile-time drift detection

```cpp
namespace detail::morph_state_io {
template <std::size_t I> using Leaf = std::integral_constant<std::size_t, I>;

/// Calls f(Leaf<i>{}, s.<leaf i>...) for i = 0..61 in table order; S... are MorphState or const MorphState.
template <class F, class... S>
constexpr void forEachMorphLeaf(F&& f, S&... s) {
    f(Leaf<0>{}, s.recipe.depth...);
    f(Leaf<1>{}, s.recipe.body...);
    // … one line per row of S3.2, in order …
    f(Leaf<61>{}, s.anchorFoldFeed...);
}
}
```

A template visitor (not a table of function pointers) keeps every access inlined: the resolver, validator, serializer
and bit-equality all expand to straight-line code (S10). Drift is caught at compile time in `morph_state.h`:

```cpp
consteval bool leavesMatchTable() {
    MorphState s{};  std::size_t n = 0;  bool ok = true;
    forEachMorphLeaf([&](auto leaf, auto& v) {
        constexpr std::size_t i = decltype(leaf)::value;
        ok = ok && i == n && kFieldTable[i].kind == kindOf<std::remove_cvref_t<decltype(v)>>();
        ++n;
    }, s);
    return ok && n == kMorphLeafFieldCount;
}
static_assert(leavesMatchTable());
static_assert(kMorphStateBytes == 1 + sumOfRowBytes());       // FR-030
static_assert(kMorphStateBytes == 201);                        // visible edit on any change
static_assert(kLeafOffsets[61] == 197);
```

`kindOf<float>() = F32`, `kindOf<bool>() = Bool`, any enum → `Enum8` (with `static_assert(sizeof(E) == 1)`).
`kLeafOffsets` is a `consteval`-built `std::array<std::size_t, 62>`: offset[0] = 1, offset[i] = offset[i−1] + bytes[i−1].

### S3.4 Validation (FR-005)

```cpp
[[nodiscard]] inline bool isValidMorphState(const MorphState& s) noexcept;
```

Per leaf, from the table: F32 → `detail::isFinite(v) && lo <= v && (hiOpen ? v < hi : v <= hi)`; Enum8 →
`static_cast<std::uint8_t>(v) <= maxEnum`; Bool → always true in memory (a stream byte > 1 is rejected at decode, C-13).
Then `&& BassStereoField::isValidControls(s.field)` (the spec names it for the field block; the table's field rows carry
the same ranges and a unit test asserts the two agree). `−0.0f` passes [0, x] boxes. Denormals are valid finite values.

### S3.5 Serialisation (FR-006, FR-007)

```cpp
inline constexpr std::uint8_t kMorphStateFormatVersion = 1;
inline constexpr std::size_t kMorphLeafFieldCount = 62;
inline constexpr std::size_t kMorphStateBytes = 201;
[[nodiscard]] inline std::size_t serializeMorphState(const MorphState& s, std::byte* dest, std::size_t capacity) noexcept;
[[nodiscard]] inline bool deserializeMorphState(const std::byte* src, std::size_t size, MorphState& out) noexcept;
```

The `spectral_state.h:178–300` pattern: `dest == nullptr || capacity < kMorphStateBytes || !isValidMorphState(s)` → 0 and
nothing written; otherwise version byte, then per leaf a `std::memcpy` of the float's 4 bytes or of
`static_cast<std::uint8_t>(v)` at `kLeafOffsets[i]`. Decode into a local `MorphState decoded{}`; version ≠ 1 → false; per
leaf: F32 memcpy; Enum8 read a byte and `static_cast<E>(byte)` (every byte value is representable in a uint8-based enum);
Bool read a byte, > 1 → false, else `byte != 0`; finally `isValidMorphState(decoded)` → `out = decoded`. The round trip is
exact because the bytes are stored values. `static_assert(std::endian::native == std::endian::little, ...)`. FR-007: the
version byte gates the layout; future versions append rows to the table, bump the version, and the decoder fills the
appended leaves from `MorphState{}` when reading an older version (not exercised in this phase; only version 1 exists).

### S3.6 Bit equality (FR-005a)

`detail::morph_state_io::leafBitsEqual(const MorphState&, const MorphState&)`: F32 leaves by
`std::bit_cast<std::uint32_t>`, enums and bools by value. Used by `setEndpoint`'s no-op rule and by the resolver's
"equal endpoints" identity. The test helper `BassMorphTest::bitEqual` is the serialised-bytes comparison the spec
specifies (plus `std::bit_cast` of the three `Resolved` floats); a test asserts that the two definitions agree on 10 000
random pairs.

---

## S4. `BassMorphEngine` (FR-010–FR-062) — `systems/bass_morph_engine.h`

### S4.1 Public API

```cpp
class BassMorphEngine {
public:
    enum class Endpoint : std::uint8_t { A, B };
    enum class AnchorMorphMode : std::uint8_t { Lock, Blend, Punch };
    using Law = MorphState::Law;
    using Axis = MorphState::Axis;
    using SelectionGroup = MorphState::SelectionGroup;

    struct Resolved {
        MorphState state{};
        float position = 0.0f;
        float anchorPosition = 0.0f;
        float anchorMass = MassLaw::kNeutralMass;
    };

    static constexpr std::size_t kControlInterval = 32;
    static_assert(kControlInterval == ProfundumCore::kControlInterval && kControlInterval == MassLaw::kControlInterval
                  && kControlInterval == BassStereoField::kControlInterval);
    static constexpr double kMinSampleRate = 22050.0, kMaxSampleRate = 192000.0;
    static constexpr float kMinPositionRampMs = 5.0f, kMaxPositionRampMs = 2000.0f, kDefaultPositionRampMs = 40.0f;
    static constexpr float kThetaUp = 0.52f, kThetaDown = 0.48f;          // D-1 (Q1)
    static constexpr int kPunchExponent = 3;                               // D-4 (Q6)
    static constexpr AnchorMorphMode kDefaultAnchorMorphMode = AnchorMorphMode::Blend;   // D-7
    static constexpr const auto& kLawTable = detail::morph_state_io::kFieldTable;        // C-1
    static constexpr std::uint8_t kMorphRuntimeFormatVersion = 1;
    static constexpr std::size_t kMorphRuntimeBytes = 20;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    bool setEndpoint(Endpoint e, const MorphState& s) noexcept;
    [[nodiscard]] const MorphState& endpoint(Endpoint e) const noexcept;
    void setPosition(float p) noexcept;
    [[nodiscard]] float position() const noexcept;          // ramped p
    [[nodiscard]] float targetPosition() const noexcept;
    [[nodiscard]] float anchorPosition() const noexcept;    // ramped p_a
    void setPositionRampMs(float ms) noexcept;
    [[nodiscard]] float positionRampMs() const noexcept;
    void setAnchorMorphMode(AnchorMorphMode m) noexcept;
    [[nodiscard]] AnchorMorphMode anchorMorphMode() const noexcept;
    [[nodiscard]] Endpoint selection(SelectionGroup g) const noexcept;   // test / UI read (None → A)

    const Resolved& advanceInterval() noexcept;
    [[nodiscard]] const Resolved& resolved() const noexcept;

    [[nodiscard]] std::size_t serializeRuntime(std::byte* dest, std::size_t capacity) const noexcept;
    [[nodiscard]] bool restoreRuntime(const std::byte* src, std::size_t size) noexcept;
    // no copy of an audio buffer anywhere; no process* member (FR-010, SC-009(a))
};
```

The runtime constants are class-scope (`BassMorphEngine::kMorphRuntimeBytes`), which keeps the spec names without a new
namespace-scope symbol. Copyable and movable (all members are values).

### S4.2 State layout (fixed-size, no heap)

| Member | Type | Note |
|---|---|---|
| `a_`, `b_` | `MorphState` | the endpoints (default `MorphState{}`) |
| `logA_`, `logB_` | `std::array<double, kMorphLeafFieldCount>` | `log2(x + o)` of each Logarithmic leaf, refreshed by `setEndpoint` (C-4); unused rows 0 |
| `posSlew_`, `anchorSlew_` | `SlewLimiter` | at the interval rate |
| `target_` | `float` | the position target |
| `rampMs_` | `float` | default 40 |
| `intervalRate_` | `float` | fs / 32, as float |
| `mode_` | `AnchorMorphMode` | default Blend |
| `selections_` | `std::array<Endpoint, MorphState::kNumSelectionGroups>` | per group (C-2), default A; indexed **only** through `groupIndex(g)` |
| `resolved_` | `Resolved` | |
| `prepared_`, `dirty_` | `bool` | |

**Group index mapping.** `SelectionGroup` is `{ None = 0, Character = 1, ImageMode = 2, FieldMotion = 3, Motion = 4,
Anchor = 5 }` (S3.1), so an enum value is **not** an array index: `selections_[g]` would read Character from ImageMode's
slot and index Anchor at element 5, out of bounds (unchecked in Release). Every access goes through
`MorphState::groupIndex(g) = static_cast<std::size_t>(g) − 1` (S3.1): the resolver (S4.6), `updateSelections` (S4.5),
`selection(g)` (which returns A for `None` without indexing) and the S4.8 bitmask encode / decode (main-byte bit
`groupIndex(g)`: Character bit 0, ImageMode 1, FieldMotion 2, Motion 3; the Anchor, index 4, is byte 19, not a bit).
Compile-time guards: `static_assert(MorphState::groupIndex(SelectionGroup::Anchor) < std::tuple_size_v<decltype(selections_)>);`
in the class, and a `consteval` sweep of `kFieldTable` asserting every `Stepped` row has `group != None` and every other
row has `group == None`, so the precondition holds for every resolver read.

### S4.3 Lifecycle (FR-011)

- `prepare(fs)`: `fs = std::clamp(fs, kMinSampleRate, kMaxSampleRate)`; `intervalRate_ = static_cast<float>(fs /
  kControlInterval)`; `configureRate()`; `prepared_ = true`; `reset()` (C-5).
- `configureRate()`: `posSlew_.configure(1.0f / rampMs_, intervalRate_)` and the same for `anchorSlew_`. Per-interval
  step = (1/rampMs) / (intervalRate·0.001) = 32 000 / (rampMs·fs) = Δp_max (FR-020): 0.016667 at 40 ms / 48 kHz (60
  intervals), 0.13333 at 5 ms (8 intervals), 3.333e-4 at 2000 ms (3000 intervals).
- `reset()`: `posSlew_.snapTo(target_)`; `anchorSlew_.snapTo(curve(mode_, target_))`; `updateSelections(p, p_a)` with the
  FR-034 rule (S4.5: inside the band the selection is **kept**); `dirty_ = true`; `resolve()`.
- Before `prepare`: setters store; `advanceInterval()` performs the `reset()` snap (no ramp) and resolves (FR-011, SC-021(a)).

### S4.4 Position, ramp and the Anchor curve (FR-013, FR-020–FR-022, FR-050, FR-051)

- `setPosition(p)` (C-7): `if (!detail::isFinite(p)) return;` `p = (p < kDenormalThreshold) ? 0.0f : (p >= 1.0f ? 1.0f :
  p);` (db_utils.h:38: the threshold `SlewLimiter::process` flushes at, smoother.h:557) `target_ = p;
  posSlew_.setTarget(p);` — the NaN path of `SlewLimiter::setTarget` is never reached (FR-013).
- `setPositionRampMs(ms)`: non-finite → return; clamp to [5, 2000]; store; `if (prepared_) configureRate();`. `configure`
  moves neither current nor target (smoother.h:497–514), so p is untouched and the next `advanceInterval` uses the new
  rate (SC-022).
- `setAnchorMorphMode(m)`: undeclared (> Punch) → ignored; else `mode_ = m` (p_a re-targets at the next interval).
- `curve(m, p)`: Lock → `0.0f`; Blend → `p`; Punch → `p * p * p`; the value is returned through
  `detail::flushDenormal` (C-7), so a Punch value below 1e-15 (p < 1e-5) is +0 (float; c(0) = 0 and c(1) = 1 exactly;
  float multiplication and the flush are monotone, so c is monotone and c(p) ≤ p on [0, 1]; c(0.5) = 0.125 exactly).
- `advanceInterval()` (prepared): `p = posSlew_.process(); anchorSlew_.setTarget(curve(mode_, p)); pa =
  anchorSlew_.process();` then `updateSelections(p, pa)`, `if (changed) dirty_ = true`, `if (dirty_) resolve()`, return
  `resolved_`.
- **Exact landing (FR-022):** `SlewLimiter::process` assigns `current_ = target_` when within one step, so p reaches 0,
  1 or any target bitwise; p_a reaches c(p) bitwise. Both hold only because every target is +0 or ≥ `kDenormalThreshold`
  (C-7), so the flush after landing (smoother.h:557) is a no-op. In Blend, p_a == p bitwise at every interval once equal: the
  p_a delta is `fl(p_old + r) − p_old`, and either branch of `process()` yields `fl(p_old + r)`.
- **Lock** needs no special case: c ≡ 0, so p_a lands on +0 and every Anchor-block leaf (and `anchorMass`) resolves to A
  by the identity rule (FR-052). A Blend → Lock change at p = 1 slews p_a to 0 over the ramp time (Edge case).

### S4.5 Selection machine (FR-034, FR-055, C-2)

Per group g with position q (p for the four main groups, p_a for Anchor) and governing amounts `gA = gov_g(a_)`,
`gB = gov_g(b_)`:

```
forced = (gA == 0.0f && gB != 0.0f) ? B : (gB == 0.0f && gA != 0.0f) ? A : none   // rule 2 (pure in A, B)
if      (q <= 0.0f)            s = A          // rule 1 (identity)
else if (q >= 1.0f)            s = B
else if (forced != none)       s = forced     // rule 2, any direction
else if (q >= kThetaUp)        s = B          // rule 3, hysteresis
else if (q <= kThetaDown)      s = A
// else: keep s (inside the band; includes "the selection rule 2 had forced")
```

`reset()` and `advanceInterval()` both call this; `restoreRuntime` checks against it (S4.8). Discrete leaves resolve to
the selected endpoint's value. `== 0.0f` matches ±0, and the test is `!= 0` (not `> 0`), so a negative `motion.amount`
is non-zero. Group g's selection lives at `selections_[MorphState::groupIndex(g)]` (S4.2). Governing amounts: Character `characterAmount`, ImageMode
`field.spread`, FieldMotion `field.motionDepth`, Motion `motion.amount` (range [−1, 1]; 0 routes nothing), Anchor
`anchor.level`.

### S4.6 Resolver (FR-030–FR-040, C-4)

`resolve()` writes `resolved_.state` leaf by leaf through `forEachMorphLeaf(f, resolved_.state, a_, b_)`:

```
q = row.anchorBlock ? pa : p
Stepped:      out = (selections_[MorphState::groupIndex(row.group)] == A) ? xa : xb   // row.group != None (S4.2)
otherwise:    if (q == 0.0f || bitsEqual(xa, xb))  out = xa;            // FR-035 identity by selection
              else if (q == 1.0f)                   out = xb;
              else {
                const double t = q;                                        // exact
                double y;
                Linear:      y = double(xa) + t * (double(xb) - double(xa));
                EqualPower:  y = std::sqrt((1.0 - t) * double(xa) * xa + t * double(xb) * xb);   // FR-033
                Logarithmic: y = std::exp2(logA_[i] + t * (logB_[i] - logA_[i])) - row.logOffset;   // FR-032
                out = std::clamp(static_cast<float>(y), std::min(xa, xb), std::max(xa, xb));          // FR-036
              }
resolved_.position = p; resolved_.anchorPosition = pa;
resolved_.anchorMass = (pa == 0 || bitsEqual(a_.mass, b_.mass)) ? a_.mass : (pa == 1 ? b_.mass : linear(a_.mass, b_.mass, pa));   // FR-054
dirty_ = false;
```

- `setEndpoint(e, s)`: undeclared e or `!isValidMorphState(s)` → false; `leafBitsEqual(s, current)` → true (no-op);
  else copy, refresh that endpoint's `log2(x + o)` cache (11 `std::log2(double)` calls, outside the interval path),
  `dirty_ = true`, true. The change appears at the next `advanceInterval` (FR-061: "at the next interval", no engine ramp).
- Monotonicity (FR-037): for linear and equal-power the argument holds (correctly rounded `+ − × sqrt` in double are
  monotone in t, and rounding to float and clamping are monotone). For the log law it does **not** hold by argument:
  `std::exp2(double)` is not required to be correctly rounded, and the standard does not promise it is monotone. FR-037
  is therefore **gated** for every law by `BassMorph_MonotoneLaws` (S7.4) with zero tolerance; a reversal found there is
  a defect of the law evaluation, fixed in the engine and never toleranced.
- The `exp2` / `sqrt` clamp is the only clamp; the identity branches never touch arithmetic, so FR-035 holds under
  `/fp:fast` and `-ffast-math` too.

### S4.7 The five perceptual axes (FR-040)

The axis is a column of `kFieldTable` (S3.2). FR-040's assignment is reproduced exactly; SC-019(b) proves the law per
row and SC-008 proves the axis descriptors move monotonically on the showcase pair. The resolver does not read the axis
column (it is metadata for tests, Phase 12's "what the user sees", and future laws).

### S4.8 Runtime round trip (FR-062, C-2)

Layout, version 1, 20 bytes, offsets as private `static constexpr` members:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `kMorphRuntimeFormatVersion` |
| 1 | 4 | target (float bits) |
| 5 | 4 | ramped p |
| 9 | 4 | ramped p_a |
| 13 | 1 | Anchor morph mode |
| 14 | 4 | ramp ms |
| 18 | 1 | main selections bitmask, bit `groupIndex(g)` (S4.2): Character = bit 0, ImageMode = 1, FieldMotion = 2, Motion = 3 |
| 19 | 1 | Anchor selection (0 = A, 1 = B) |

`serializeRuntime`: null or short → 0; otherwise write and return 20. `restoreRuntime`: decode into locals; reject (return
false, change nothing) if `src == nullptr`, `size < 20`, version ≠ 1, any float non-finite, target / p / p_a outside
[0, 1], negative zero, or 0 < |x| < `kDenormalThreshold` (1e-15, db_utils.h:38; the engine can never hold such a value,
C-7), ramp outside [5, 2000], mode > Punch,
main byte > 0x0F, Anchor byte > 1, or a selection that contradicts S4.5 at the stored position for the **current**
endpoints: `q <= 0` requires A; `q >= 1` requires B; rule 2 active requires the forced side; `q >= θ_up` (rule 2 inactive)
requires B; `q <= θ_down` requires A; inside the band either is accepted. On success: `rampMs_ = …; if (prepared_)
configureRate(); mode_ = …; target_ = …; posSlew_.snapTo(p); posSlew_.setTarget(target_); anchorSlew_.snapTo(pa);
selections_ = …; dirty_ = true; resolve();` — no `advanceInterval`, no `reset()`. At the next `advanceInterval` both the
live and the restored engine call `anchorSlew_.setTarget(curve(mode_, p_new))` before `process()`, so their p_a
trajectories are bitwise identical although the slew's stored target differed (C-3).

### S4.9 RT safety (FR-080, FR-081)

Every method is `noexcept`, allocation-free and lock-free; `prepare` only configures. `advanceInterval` is bounded: 62
leaves, at most 11 `exp2`, 2 `sqrt`. `setEndpoint` is bounded (62 compares, a 201-byte-class copy, ≤ 11 `log2`). The
non-finite guards use `detail::isFinite` (fast-math-immune). No `std::isnan`.

### S4.10 Receiver inventory (FR-039): smoothed, latched or immediate

FR-039 asks the plan to record which receivers apply a new value immediately. Below: every `MorphState` leaf, its
receiver in the S5 fixture (and in Phase 9's voice), and how a per-interval change reaches the audio. "Immediate" means a
change handed over at one grid point steps the receiver's output at that sample.

| Leaves | Receiver | Class | Constant · file:line (read 2026-10-19) |
|---|---|---|---|
| `recipe.*` (6) | `ProfundumCore::setControls` | rate-limited: per-interval shape-step ceiling Δmax = 375·√P0·32/fs | profundum_core.h:142–147 |
| `excitation.*` (3) | `ProfundumCore::setExcitation` | smoothed 5 ms | `kExcitationSmoothMs`, profundum_core.h:350 |
| `foldAmount` (via MassLaw) | `HarmonicFold::setAmount` | smoothed 10 ms | `kAmountSmoothMs`, harmonic_fold.h:92 |
| `shape` | `ShapeMorph::setShape` | smoothed 10 ms | `kShapeSmoothMs`, shape_morph.h:107 |
| `characterAlgorithm` | `BassCharacterStage::setAlgorithm` | crossfade, last request queued | bass_character_stage.h:241 (setter); R-14 |
| `characterAmount`, `characterTone` | `BassCharacterStage::setAmount / setTone` | smoothed 10 ms | `kParamSmoothMs`, bass_character_stage.h:84 |
| `drop*` (4) | `DropEnvelope` setters | **latched at trigger** (next note) | drop_envelope.h:80–81; SC-023 |
| `ampEnv.attackMs / decayMs / releaseMs` | `ADSREnvelope::setAttack / setDecay / setRelease` | **latched at stage entry** in table mode (the fixture's mode: its first interval calls the float curve setters). The setters only recompute coefficients (adsr_envelope.h:115–136 → 401–433, which write `logPhaseInc_` only for the enum `Logarithmic` curve); the table-mode increment is set at stage entry (adsr_envelope.h:456, 476, 498). A mid-stage change alters nothing until the next stage | as cited |
| `ampEnv.sustain` | `ADSREnvelope::setSustain` (adsr_envelope.h:127–130, no smoothing) | **immediate in Decay**: the table path computes `output = s·pk + (start − s·pk)·table(phase)` (adsr_envelope.h:311–322), so it steps by Δs·pk·(1 − table(phase)); **smoothed in Sustain** (one-pole, adsr_envelope.h:243–246, coefficient from `kSustainSmoothTimeMs` = 5 ms, envelope_utils.h:53, adsr_envelope.h:72); not read by the Release table path (adsr_envelope.h:350–358) | as cited |
| `ampEnv.attackCurve / decayCurve / releaseCurve` | `ADSREnvelope::setAttackCurve(float)` etc. | **immediate**: the setter regenerates the 256-entry table (adsr_envelope.h:160–179) and the running stage reads it at the same phase, a step of range·Δtable(phase) in that stage | as cited |
| `field.mode` | `BassStereoField::setControls` → `advanceControl` | latched at `advanceControl` (`applied_ = pending_`, bass_stereo_field.h:294), then a 10 ms mode crossfade and the WIDE engage at `kWideEngageStep` per interval | bass_stereo_field.h:91, 92, 296–301 |
| `field.spread`, `gravity`, `curve`, `motionDepth` | per-voice pan law | latched at `advanceControl`; pan vector slewed per partial at `kMaxPanStepPerInterval` = 0.003 per interval; side gain smoothed 10 ms | bass_stereo_field.h:77, 90, 322–327 |
| `field.crossoverHz` | law f_c | latched at `advanceControl`, then a 20 ms control-rate smoother | `kCrossoverSmoothMs`, bass_stereo_field.h:93, 304–308 |
| `field.motionRate`, `motionSource`, `cycleSync`, `cycleNote`, `cycleModifier` | motion value m | latched at `advanceControl`; a source switch steps m in that interval (bass_stereo_field.h:315), which reaches audio only through the pan law's 0.003-per-interval slew (above) | as cited |
| `mass` | `MassLaw::setMass` | slewed, 0 → 100 in 40 ms | `kMassSweepMs`, mass_law.h:77, 154 |
| `transientAttackDb`, `transientSustainDb` | `MassDynamics::setTransient*Db` (mass_dynamics.h:530–531) | smoothed 10 ms | `kGainSmoothMs`, transient_shaper.h:93 |
| `anchor.level` | `AnchorOscillator::setControls` | smoothed 5 ms | `kLevelSmoothMs`, anchor_oscillator.h:75 |
| `anchor.waveform`, `anchor.mode` | `AnchorOscillator::setControls` | 5 ms crossfade / 5 ms bridge | anchor_oscillator.h:76, 79 |
| `anchor.punchBoostDb`, `punchTimeMs`, `ghostTimeMs` | `AnchorOscillator::setControls` → `pending_` (anchor_oscillator.h:224–245) | **latched at the control latch, consumed when a program is built**: `latchControls()` copies `active_ = pending_` (anchor_oscillator.h:437–446; called at offset 0 of the next block when dirty, :296–297, at a note-on, :482, and in `reset()`, :213) and reads no punch / ghost field; `punchTimeMs` / `ghostTimeMs` are read only by the arm programs `snapArm` / `bridgedArm` (:1011–1058), built at the next re-arming note-on (:505, :514); `punchBoostDb` is read there through `boostGain()` (:861–863) and also caps the Hermite bridge fit (`bridgeLength`, :994–995) of the FR-044 mode bridge (`bridgeToRest`, :445–446, :1062–1069). A running program is never stepped: none is immediate | anchor_oscillator.h:84–86, 103–105, 224–245, 437–446, 505, 514, 861–863, 994–995, 1011–1069; `evidence/receiver-inventory.txt` (T005) |
| `anchorFoldFeed` | fixture feed (C-11) | per-sample linear ramp across each interval (fixture); a voice smoother in Phase 9 | C-11 |
| `motion.*` (16) | none in Phase 8 (the Motion component is Phase 9's) | — | S12 |

**Immediate-apply set:** `ampEnv.sustain` while the envelope is in Decay, and each `ampEnv` curve amount while its stage
runs. Everything else is smoothed, slewed, crossfaded or latched. The set is a Phase 9 handoff (S12). It is measured by
`BassMorph_AmpEnvelopeImmediateApply` (S7.4), which proves each "immediate" and "latched" claim above against the real
`ADSREnvelope` and records the per-sample step against the SC-004(a)-style bound. It does not gate that bound: the
amp-envelope class is Phase 9's choice (FR-070 step 8), and FR-039 adds no engine-side smoother.

---

## S5. Test fixture (FR-070, FR-071) — `dsp/tests/unit/systems/bass_morph_test_helpers.h`

Namespace `Krate::DSP::BassMorphTest`. It includes `bass_stereo_field_test_helpers.h` (which brings the Phase 6 helpers),
`profundum_core_test_helpers.h`, `anchor_oscillator_test_helpers.h`, `../processors/shape_character_metric_helpers.h`,
`tests/test_helpers/k_weighting.h`, `allocation_detector.h`; the Phase 6 and 7 helper headers are **read-only**.

### S5.1 Named pairs (FR-071) and the random generator

- `showcasePair()` → `{A, B}` exactly as the spec's descriptor block (A: `kSineAnchor`, mix 0, Fold 0, SHAPE 0, Tube /
  0 / 0.5, Drop 0, env 1 / 150 / 0 / 50, SOLID, Spread 0, Motion Off, Mass 50, Anchor Sine / Lock / 1.0, feed 0; B:
  `kGrowl` (edge 1), mix 1 / drive 1 / asym 0.5, Fold 80, SHAPE 1, Fold / 0.8 / 0.8, Drop 0, env 1 / 2000 / 0.8 / 800,
  WIDE, Spread 1, Motion Off, Mass 50, Anchor Sine / Lock / 0.7). Curves stay at the C-8 defaults on both sides.
- `massOnlyPair()` → `MorphState{}` with A.mass = 0 and B.mass = 100 (SC-005(e)).
- `anchorCurvePair()` → `MorphState{}` with A.anchor.level = 1.0, B.anchor.level = 0.7, Mass 50 (SC-005(a–d)).
- `maxNonlinearPair()` → A: Fold 0, Tube, Amount 0; B: Fold 100, Fracture, Amount 1, excitation mix 1, drive 1 (SC-017
  grid).
- `algorithmCrossingPair()` → A and B both Fold 100, excitation mix 1, drive 1, Character Amount 1, Tone 0.5; A Tube,
  B Fracture; every other leaf equal (SC-017 crossing arms and (b) window, C-16).
- `randomValidState(Rng&)`: one draw per table row. **Portable**: it maps raw `std::mt19937` words itself
  (`u = (w >> 8) · 2^−24`) instead of `std::uniform_*_distribution`, whose output differs between libstdc++, libc++ and
  MSVC. F32: uniform on [lo, hi] (log rows: log-uniform on [lo + o, hi + o] − o; the phase row: [0, 360) with 360 → 0);
  Enum8: `w % (maxEnum + 1)`; Bool: `w & 1`. With probability 0.15 per governing amount it is set to exactly 0, and with
  probability 0.05 per F32 leaf to exactly lo or hi, so rules 1–3 and the box edges are exercised. Seeds are fixed
  literals per test.
- `randomValidPair(Rng&, float pEqual = 0.1)`: two states, then each leaf of B copied from A with probability pEqual.

### S5.2 Config and taps

```cpp
struct MorphEvent {
    enum class Kind : std::uint8_t { Position, RampMs, Mode, EndpointA, EndpointB };
    std::size_t sample = 0;    // applied at the first grid point >= sample
    Kind kind = Kind::Position;
    float value = 0.0f;        // Position, RampMs
    BassMorphEngine::AnchorMorphMode mode = BassMorphEngine::kDefaultAnchorMorphMode;
    MorphState state{};        // EndpointA / EndpointB
};

struct MorphChainConfig {
    MorphState a{}, b{};
    BassMorphEngine::AnchorMorphMode mode = BassMorphEngine::kDefaultAnchorMorphMode;
    float initialPosition = 0.0f;                      // engine.reset() at this p before the first interval
    float rampMs = BassMorphEngine::kDefaultPositionRampMs;
    double f0 = 0.0, fs = 48000.0, seconds = 0.0;      // seconds <= 0 -> MassDynamicsTest::descriptorSeconds(fs)
    std::size_t hostBlock = 512;
    std::size_t noteOnSample = 0;                      // multiple of 32
    std::optional<std::size_t> noteOffSample;
    bool ampEnvelope = true;                           // false = the steady render (unity gain, no ADSR at all)
    bool drop = false;                                 // true: per-sample f0 track from the voice's DropEnvelope
    bool muteCharacter = false;                        // SC-016 discriminating read: characterL/R zeroed before the sum
    std::vector<MorphEvent> automation;                // sorted by sample
    StereoFieldTest::FieldChainConfig tail{};          // tail options only; chain.f0/fs/seconds/hostBlock are overwritten
};

struct MorphChainTaps : StereoFieldTest::FieldChainTaps {
    std::vector<BassMorphEngine::Resolved> resolvedPerInterval;
    std::vector<float> anchorLevelHanded;              // Mass-biased level handed to the Anchor (FR-054)
    std::vector<std::size_t> advanceSamples;           // absolute sample of each advanceInterval call (SC-010)
    struct SetterCall { std::size_t sample; std::uint8_t leaf; std::uint8_t value; };
    std::vector<SetterCall> discreteSetterCalls;       // setAlgorithm / field mode / anchor waveform+mode issues (SC-004(d), SC-024)
    std::vector<std::vector<BassMorphEngine::Resolved>> perVoiceResolved;   // renderMorphPoly only (SC-010)
};

[[nodiscard]] MorphChainTaps renderMorphChain(const MorphChainConfig& cfg);
[[nodiscard]] MorphChainTaps renderMorphPoly(std::span<const double> f0s, const MorphChainConfig& common);   // 4 voices
```

### S5.3 Construction (before the first sample)

1. `BassMorphEngine engine; engine.setEndpoint(A, cfg.a); engine.setEndpoint(B, cfg.b); engine.setPositionRampMs(cfg.rampMs);
   engine.setAnchorMorphMode(cfg.mode); engine.setPosition(cfg.initialPosition); engine.prepare(cfg.fs);` (prepare
   resets, C-5) → `r0 = engine.resolved()`.
2. Components configured from `r0` **before** prepare where prepare snaps (the `renderFieldChain` pattern,
   bass_stereo_field_test_helpers.h:291–311): `ShapeMorph::setShape(r0.shape)`, `BassCharacterStage::setAlgorithm /
   setAmount / setTone`, `HarmonicFold::setAmount`; then their `prepare(fs, 512)`. The field: a `FieldChainConfig` copy
   with `field = r0.state.field` passed to `StereoFieldTest::makeField`. `ProfundumCore`, `AnchorOscillator`,
   `DropEnvelope` and `ADSREnvelope` are prepared first and then given `r0` (their setters are no-ops before prepare).
3. `MassLaw law; law.prepare(fs); law.setBase(baseFor(r0)); law.setMass(r0.state.mass); law.reset();` and
   `MassDynamicsTest::makeChainDynamics(chainCfg)` with `shaperAttackDb / shaperSustainDb` from `r0`.

`baseFor(r)`: `Targets{ .depth = r.recipe.depth, .body = r.recipe.body, .foldAmount = r.foldAmount, .anchorLevel =
r.anchor.level }` with `transientDurationMs`, `compressionAmount`, `outputTrimDb` at the `Targets{}` defaults (FR-070 step 2).

### S5.4 Per grid point (in this order; FR-070 steps 1–10)

1. Apply every due automation event (Position → `setPosition`, RampMs → `setPositionRampMs`, Mode →
   `setAnchorMorphMode`, EndpointA/B → `setEndpoint`); `const Resolved& r = engine.advanceInterval();` **exactly once**;
   record `r` and the sample.
2. `law.setBase(baseFor(r.state)); law.setMass(r.state.mass); (void)law.advanceInterval(); t = law.currentTargets();`
   Anchor level `lvl = MassLaw::evaluateRow(MassLaw::kRows[AnchorLevel], r.anchorMass, r.state.anchor.level)`.
3. `core.setControls(MassDynamicsTest::coreControlsFor(r.state.recipe, t)); core.setExcitation(r.state.excitation);`
4. `fold.setAmount(t.foldAmount); shapeMorph.setShape(r.state.shape);`
5. `if (r.state.characterAlgorithm != lastAlgorithm) { character.setAlgorithm(…); record; }` then `setAmount`, `setTone`.
6. `anchor.setControls({…r.state.anchor, .level = lvl})` (record a call when waveform or mode changed); the feed target
   for this interval is `r.state.anchorFoldFeed` (ramped per sample from the previous one, C-11).
7. `DropEnvelope` setters with the four Drop leaves (latched by the component at its next trigger).
8. `ADSREnvelope` setters, **change-only** per leaf (bit compare with the last applied value; the first interval applies
   all seven), so `setAttackCurve(float)`'s table rebuild runs only on change.
9. `field.setControls(r.state.field); field.advanceControl();` (record a call when `mode` changed), then the pan vector
   (`computePanVector` → `core.setPartialPanOffsets`) and `field.wideOffsets(r.state.characterAlgorithm)` →
   `character.setChannelOffsets` (the Phase 7 step 4–6 handoffs).
10. `md.setTransientAttackDb/SustainDb(r.state.transient…)`, `setTransientDurationMs(t.transientDurationMs)`,
    `setCompressionAmount(t.compressionAmount)`, `setOutputTrimDb(t.outputTrimDb)`, `setDetectorKeyHz(fNow)`;
    `a1Next = MassDynamicsTest::unbiasedH1Amplitude(r.state.recipe, fNow, fs)` (recomputed every interval from the
    resolved recipe, never stale).
11. At `noteOnSample`: `core.noteOn`, `anchor.noteOn`, `drop.trigger(true)`, `adsr.gate(true)`, field voice state reset;
    at `noteOffSample`: `adsr.gate(false)`.

### S5.5 Per rendered block

- With `cfg.drop`, `d[n] = drop.process()` and `f0[n] = f0 · 2^(d[n]/12)`; the track `f0p` is passed to the core, the
  Anchor, ShapeMorph and the character stage (the `f0At` pattern, bass_stereo_field_test_helpers.h:278–283); otherwise
  `f0p = nullptr` for the whole render (bitwise-consistent references).
- core → `coreL/R`; Anchor (`setCoreH1Amplitude(a1)`) → `anchorL/R`; `foldIn = ShapeMorph(core) + feed[n] · anchor[n]`
  (the Phase 3 pre-Fold feed, mono anchor added to both channels); Fold; character → `characterL/R`.
- Amp envelope (`ampEnvelope = true`): `g[n] = adsr.process()` multiplies `characterL/R` **and** `anchorL/R` in place
  (the Anchor is part of the voice and is released with it). With `ampEnvelope = false` no multiplication happens at all
  (unity, bitwise).
- `StereoFieldTest::processFieldTail(taps, tailCfg, md, dcL, dcR, limiter, &field, start, len, total)` (sum = character
  + anchor → dynamics → stage → DC ×2 → limiter).
- With `muteCharacter = true` the character tap is zeroed (after it is recorded) before the sum, so the tail processes the
  Anchor alone while the field, its side-only stage, dynamics, DC blockers and limiter all still run (SC-016).

### S5.6 `renderMorphPoly`

The Phase 7 poly structure (bass_stereo_field_test_helpers.h:463): one core / ShapeMorph / Fold / character / Anchor /
Drop / ADSR per voice, all reading the **same** `engine.resolved()` reference at each grid point; each voice copies the
`Resolved` it applied into `perVoiceResolved[v]`; one shared tail.

### S5.7 Descriptor readers (spec descriptor block)

| Function | Reads | Definition |
|---|---|---|
| `dCxDb(taps, f0, fs)` | core tap mid, read window | 10·log10(Σ_{n≥2} wₙ / w₁), `ProfundumTest::renderedHarmonicPowers`, each power floored at −120 dB |
| `dNlDb(taps, f0, fs)` | character and core taps, mid | 10·log10(Σ_{n≥9} wₙ(char) / Σ_{n≥9} wₙ(core)), floored |
| `dStDb(taps, fc, fs)` | output L/R | `StereoFieldTest::bandReading(L, R, n, fs, 2·f_c, fs/2).sideToMidDb` |
| `dBrOct(taps, f0, fs)` | output mid | `ProfundumTest::describeRendered(…).centroidOct` |
| `lKDb(taps, fs)` | output, both channels | mean of `kWeightedLevelDb` L and R over the read window (`measureFrom` = settle) |
| `eSubDb(L, R, f0, fs)` | any tap pair (mid) | mean of `MassDynamicsTest::subBand(mid…).lpRmsDb` over the read window |
| `dTMs(taps, f0, fs)` | output mid, gate render | Σ e_k²·P_k / e_peak² over whole periods; `REQUIRE` the 80 dB fall-off is reached before the length limit |

Read window: `[kSettleSeconds·fs, +TestUtils::kLowFrequencyFftSize)` trimmed to whole periods of f₀. Gate renders start
at `noteOffSample + 2 s`, then extend by 1 s up to 12 s; a position that never reaches −80 dB fails with the reading
printed (no capped reading is ever used).

---

## S6. Algorithm notes (enough that nothing is guessed)

- **Position step:** Δp_max = 32 000 / (rampMs · fs) per interval (S4.3). Traversal 0 → 1: ⌈rampMs·fs / 32 000⌉
  intervals ± 1 (float accumulation of `current_ += rate`).
- **Linear:** y = x_A + t·(x_B − x_A) in double (C-4). Worst |Δy| per interval = |x_B − x_A|·Δp_max.
- **Logarithmic:** y = exp2(L_A + t·(L_B − L_A)) − o with L = log2(x + o) cached in double. Per-interval bound
  (max + o)·|((x_B + o)/(x_A + o))^Δp − 1|.
- **Equal-power:** y = √((1 − t)·m_A² + t·m_B²), in double; constant when m_A = m_B (and handled by identity anyway);
  bound √Δp·max(m_A, m_B).
- **Punch:** c(p) = p³ (float). Blend: c(p) = p. Lock: c ≡ 0. p_a is rate-limited by the same Δp_max.
- **Anchor Mass split (FR-054):** `anchorMass` = linear(A.mass, B.mass) at p_a with identity at 0 / 1; the fixture
  evaluates the AnchorLevel row at it and every other row at the slewed `mass` (MassLaw's own 40 ms slew, as Phase 9 will).
- **Selection:** S4.5. The 0.48 / 0.52 band is 0.04 wide; 5 Hz jitter of ±0.01 around θ_up never leaves the band.

---

## S7. Test plan (file · TEST_CASE · assertion strategy)

### S7.1 Files, tags, targets

All TUs live in `dsp/tests/unit/systems/`, register in `dsp_systems_tests`, and every TEST_CASE carries
`[systems][profundum][bass-morph]` plus its `[sc-0xx]` / `[fr-0xx]` tags.

| File | Contents | Lane |
|---|---|---|
| `morph_state_test.cpp` | FR-001–FR-007, SC-007(a)(b), SC-019(a)(c), table / range agreement | per-push |
| `bass_morph_engine_test.cpp` | SC-001, SC-002, SC-003, SC-005 engine, SC-006(a), SC-007(c)(d), SC-011, SC-013, SC-014, SC-019(b) (incl. `BassMorph_LawTable_GoverningAmount`), SC-020, SC-021, SC-022, SC-024 continuous, SC-004(c); FR-012 / FR-014 / FR-037 / C-7 cases `BassMorph_EndpointApi`, `BassMorph_AnchorModeApi`, `BassMorph_MonotoneLaws`, `BassMorph_SubThresholdLanding` | per-push |
| `bass_morph_nonfinite_test.cpp` | SC-012 | per-push, **`-fno-fast-math` block** |
| `bass_morph_test_helpers.h` | S5 fixture, pairs, readers | — |
| `bass_morph_helpers_test.cpp` | fixture self-tests (S7.3) | per-push |
| `bass_morph_chain_test.cpp` | SC-009, SC-010, SC-015, SC-023, SC-024 render, FR-039 `BassMorph_AmpEnvelopeImmediateApply`; `BassMorph_AnchorCurvesRendered`, `BassMorph_IdempotentRender` `[long]` | mixed |
| `bass_morph_click_test.cpp` | SC-004(a)(b)(d) shards `[long]` | `[long]` |
| `bass_morph_trajectory_test.cpp` | SC-008 shards `[long]` | `[long]` |
| `bass_morph_monosum_test.cpp` | SC-016 incl. `BassMorph_MonoSum_AnchorThroughTail_<Note>` `[long]` | `[long]` |
| `bass_morph_aliasing_test.cpp` | SC-017 `[long]` | `[long]` |
| `bass_morph_perf_test.cpp` | SC-018 `[performance][.perf]`; FR-083 worst-interval record `[performance][.perf]` | CPU lane alone |
| `bass_morph_probe_test.cpp` | `BassMorph_SumTapCoherenceProbe` `[.]` (C-9) | hidden, main loop |

### S7.2 Shared conventions

- "Bitwise equal" is always `BassMorphTest::bitEqual` (FR-005a). No `memcmp` of `MorphState` or `Resolved`.
- No bit-exact float goldens of renders: render comparisons are either *same-build, same-process* references
  (A static, B static, direct-setter, unmorphed Drop) or measured tolerances; `TestUtils::kSampleTolerance` is the bound
  for the fixture-equivalence self-test.
- Notes: E0 = `anchorMidiHz(16)`, E1 = 28, E2 = 40, C3 = 48. Rates 44.1 / 48 / 96 kHz unless stated.
- Every render test that reads the core or the character stage sets FTZ/DAZ for its scope (the components'
  precondition); the engine-only tests run without it.
- ulp(x) = `std::nextafter(|x|, +inf) − |x|` in float.

### S7.3 Fixture self-tests (`bass_morph_helpers_test.cpp`)

- `BassMorphHelpers_StaticEqualsFieldChain`: A == B == a state expressible by `renderFieldChain` (the showcase B's chain
  settings, Mass 50, amp envelope off); `renderMorphChain` output vs `renderFieldChain` output: max |Δ| ≤
  `kSampleTolerance` per sample, on the character, sum and out taps (the setter order differs, so bit equality is not
  claimed).
- `BassMorphHelpers_GridCalls`: `advanceSamples` are exactly 0, 32, 64, … and their count is ⌈total / 32⌉ at host
  blocks 1, 7, 512.
- `BassMorphHelpers_ChangeOnlyEnvelopeSetters`: a probe counter wrapper confirms each ADSR curve setter fires once at
  start and once per actual change.
- `BassMorphHelpers_DescriptorsOnSines`: `dCxDb` of a pure sine ≤ −100 dB; `eSubDb` of a sine of amplitude a equals
  20·log10(a/√2) within 0.05 dB; `dTMs` of a gated constant tone of length T equals T within one period.

### S7.4 Per criterion

| SC | TEST_CASE | File | Assertion strategy |
|---|---|---|---|
| FR-001/004/005/006 | `MorphState_TypeTraits`, `MorphState_Defaults`, `MorphState_DefaultIsValid`, `MorphState_TableMatchesComponentRanges` | morph_state | `static_assert` trivially copyable / aggregate; defaults equal `kDefaultControls`, `kDefaultExcitation`, `BassStereoField::Controls{}`, `AnchorOscillator::Controls{}` bitwise, the cited scalar defaults; the table's field rows equal `BassStereoField` constants; `isValidControls` and the table agree on 10 000 random field blocks |
| SC-001 | `BassMorph_EndpointIdentity` | engine | seed 0x8001; 10 000 random pairs + showcase + every named recipe pair, × 3 modes; reset at 0 → `bitEqual(resolved.state, A)`; setPosition(1), advance until `position() == 1` and p_a landed → equals B, Anchor block (leaves 55–61) equal to A in Lock and B in Blend / Punch; **0 mismatches** |
| FR-012, C-14 | `BassMorph_EndpointApi` | engine | fresh engine: `endpoint(A)` and `endpoint(B)` bitEqual `MorphState{}`; after `setEndpoint(A, sa)` / `(B, sb)` (seed 0x8012 random valid states) each reads back bitEqual to what was set; `setEndpoint(static_cast<Endpoint>(2), s)` returns false with both endpoints bitwise unchanged, and `endpoint(static_cast<Endpoint>(2))` bitEqual `endpoint(A)` (C-14); re-setting a bitwise-identical state mid-ramp returns true and leaves `resolved()` and the next 16 intervals bitEqual to a twin engine that received no call |
| FR-014, D-7 | `BassMorph_AnchorModeApi` | engine | fresh engine: `anchorMorphMode() == Blend`; mid-ramp `setAnchorMorphMode(static_cast<AnchorMorphMode>(3))` and `(255)`: mode unchanged and the p_a trajectory over the next 64 intervals bitEqual to a twin engine that received no call |
| FR-037 | `BassMorph_MonotoneLaws` | engine | seed 0x8037; 100 random valid pairs × 3 modes; p over 10 001 points k / 10 000 (`setPosition` + `reset()` at each, ascending) plus every interval of a 0 → 1 traversal at `kMaxPositionRampMs` (3 000 intervals at 48 kHz); for every continuous leaf: x_B ≥ x_A → resolved non-decreasing in p, else non-increasing, **zero tolerance** (float compare); Anchor-block leaves and `anchorMass` read against p_a = c_m(p) in each mode; any reversal fails with the leaf, p pair and values printed |
| C-7, FR-022 | `BassMorph_SubThresholdLanding` | engine | each mode: from p = 0.5, `setPosition(1e-20f)`, `setPosition(std::nextafter(1e-15f, 0.0f))` → `targetPosition()` is +0 bitwise; `setPosition(1e-15f)` keeps 1e-15f; after landing `position() == targetPosition()` bitwise and 8 further intervals leave it unchanged; Punch, `setPosition(5e-6f)`: after landing `anchorPosition()` is +0 == `curve(Punch, position())` bitwise and stays so for 8 intervals; `reset()` at 5e-6 in Punch publishes p_a = +0 and the next `advanceInterval` leaves the resolved Anchor block bitwise unchanged |
| SC-002 | `BassMorph_EqualEndpointsConstant` | engine | 1 000 random states A == B; reset at p ∈ {0, 0.01, …, 1} and every interval of a 0 → 1 → 0 ramp: equals A; 0 mismatches |
| SC-003 | `BassMorph_BoundedStep` | engine | jumps 0 → 1, 1 → 0, 0.3 → 0.7 and 30 s of seeded random automation (seed 0x8003) × fs {44.1, 48, 96, 192} k × ramp {5, 40, 2000} ms; (a) \|Δp\|, \|Δp_a\| ≤ Δp_max·(1 + 4ε), mode changes inserted mid-morph; (b) per continuous leaf the S6 analytic bound × (1 + 4ε) + k·ulp(max(\|x_A\|, \|x_B\|)), k = 2 lin / equal-power, 8 log; (c) traversal count ⌈rampMs·fs/32 000⌉ ± 1 |
| SC-004 (a)(b) | `BassMorph_DiscreteClickFree_Algorithm`, `_ImageMode`, `_AnchorWaveform`, `_AnchorMode`, `_FieldMotionSource`, `_FieldCycleSync`, `_FieldCycleModifier`, `_FieldCycleNote` (`[long]`, one shard each) | click | C-10 base; Blend; E1 and C3 at 48 kHz; ramp {5, 40, 2000} ms; renders per (pair, note): A-static, B-static (shared across ramp times), per ramp: transition + direct-setter reference; window = [switch grid sample, transition end + one period], transition end = switch + one period (character pre-roll) + 10 ms (character / image mode) or + 5 ms (Anchor waveform / mode) or + the field's pan slew (motion cases, read from `field` slew completion); (a) `maxDeltaAround` over the window on outL and outR ≤ 10^(0.5/20) × the reference's; (b) `differentialClicks(transition, refStart, refEnd, n, fs, f0).counted == 0` on both channels |
| SC-004 (c) | `BassMorph_Hysteresis` | engine | default ramp, 48 kHz; target θ_up + 0.01·sin(2π·5t) for 2 s after approaching from 0 (selection A), and θ_down + 0.01·sin(…) after approaching from 1 (selection B); per-interval `selection(Character)` switches ≤ 1; positive control: θ = 0.52 single threshold on the recorded p sequence switches ≥ 10 |
| SC-004 (d) | `BassMorph_GoverningAmountRule` (engine part, per-push) + `BassMorph_DiscreteClickFree_GoverningAmount` (`[long]`) | engine / click | rows: Character (Tube / 0 → Fold / 0.8 and reverse), ImageMode (Spread 0 one side), Anchor waveform and mode (level 0 one side); 0 → 1 → 0 ramp; non-silent side at every interval with q ∈ (0, 1), A at 0 and B at 1 bitwise; `discreteSetterCalls` exactly one per direction at the first interval with q > 0 (rising) and at the landing on 0 (falling); render meets (a)(b); positive control with both amounts non-zero switches at θ_up / θ_down |
| SC-005 (a)(b)(c)(e) | `BassMorph_AnchorCurves` | engine | `anchorCurvePair`, reset at p ∈ {0, 0.05, …, 1}: Lock level bitwise 1.0; Blend \|level − (1 − 0.3p)\| ≤ 1e-6; Punch \|level − (1 − 0.3p³)\| ≤ 1e-6 and ≥ Blend; (e) `massOnlyPair` in Lock: `evaluateRow(AnchorLevel, anchorMass, level)` bitwise constant; in Blend it rises and is printed per p |
| SC-005 (a)(d)(e) rendered | `BassMorph_AnchorCurvesRendered` `[long]` | chain | steady renders E1, 48 kHz, 21 p × 3 modes: Lock E_sub(anchor tap) max − min ≤ 0.05 dB; Anchor w₁ (`renderedHarmonicPowers` on the anchor tap, relative to the p = 0 Lock render) within 0.1 dB of 20·log10(level); (e) D_nl moves ≥ 3 dB on the Mass-only pair (Lock) |
| SC-006 (a) | `BassMorph_Idempotent` | engine | 200 random pairs (seed 0x8006) × 3 modes: 0 → 1 → 0 ramp and random automation ending at 0: final `resolved()` bitEqual initial; `serializeRuntime` bytes equal |
| SC-006 (b) | `BassMorph_IdempotentRender` `[long]` | chain | 8 constrained random pairs × 3 modes (motionDepth 0, anchor.mode ∈ {Lock, Follow}, Drop 0), steady render; A→B→A vs A alone, same note-on and host blocks; window [t_land + 1 s, + kLowFrequencyFftSize) on the mid; harmonics 1–16 within 60 dB of the strongest agree within 0.1 dB |
| SC-007 (a)(b) | `BassMorph_Serialisation_RoundTrip`, `_Rejects` | morph_state | 10 000 random states: size == 201, deserialised bitEqual; null, short capacity / size (0 … 200), version 0 / 2 / 255, each leaf NaN / ±Inf / below lo / above hi / undeclared enum / bool byte 2, truncated: serialise 0 with the guard bytes unchanged, deserialise false with `out` bitwise unchanged |
| SC-007 (c)(d) | `BassMorph_RuntimeRoundTrip`, `BassMorph_RuntimeRejects` | engine | live engines under seeded automation, stops at 200 random points, band points from below / above, mid-ramp, an in-flight mode change, the showcase pair at 0.3 (rule 2), each mode, ramp 333 ms; fresh engine prepared at the same fs + `setEndpoint` ×2 + `restoreRuntime` → `resolved()` bitEqual to the **live** one, then 4 096 further intervals under identical automation bitEqual; (d) short, version, non-finite, out-of-range, −0 / denormal, undeclared mode, mask > 0x0F, Anchor byte 2, a selection contradicting S4.5 → false and `resolved()` unchanged |
| SC-008 | `BassMorph_ShowcaseTrajectory_<Mode>_<Note>` (6 shards, `[long]`) | trajectory | 21 static p, 48 kHz; steady render per (mode, p) gives D_cx, D_nl, D_st, D_br, L_K, E_sub (sum tap), E_sub,out; gate render per p gives D_t; Lock render's anchor tap gives F_lock(p) (computed once per note and shared by the three mode shards via a static cache keyed by note); (a) each descriptor non-decreasing within ε (C-15); no gate render hits 12 s; (b) net travel ≥ the SC-015 bars; (c) L_K(p) ≥ min(L_K(0), L_K(1)) − 1.5 dB; (d) E_sub(p) ≥ F_lock(p) − 1.0 dB in every mode, and Punch ≥ Blend − 0.1 dB; E_sub,out and the dynamics gain printed per p |
| SC-009 | `BassMorph_RecipeDomain` | chain | (a) `static_assert(!requires(BassMorphEngine& e, float* x, std::size_t n) { e.processBlock(x, x, n); })` and the `process(float)` / `processBlock(float*, std::size_t)` variants; (b) showcase recipes, p ∈ {0.25, 0.5, 0.75}, Mass 50: core prepared, `setControls(resolved.recipe)` every interval until `shapeGains()` stops changing for 8 intervals; `shapeGains()` bitwise equal to `ProfundumTest::shapeOf(resolved.recipe, N)`; positive control: 0.5·render(A) + 0.5·render(B) differs from render(p = 0.5) by ≥ 1 dB in at least one of harmonics 1–16 (core tap) |
| SC-010 | `BassMorph_Global` | chain | `renderMorphPoly` 4 voices (E1, A1, E2, C3) 0 → 1 ramp, host blocks {7, 512}: `advanceSamples.size() == ⌈total/32⌉`, each `% 32 == 0`; `perVoiceResolved[v][i]` bitEqual across v for every i |
| SC-011 | `BassMorph_RealTimeSafety` | engine | `TestHelpers::AllocationScope` around 10 s (48 kHz grid) of random automation, endpoint replacement every 100 intervals, `serializeRuntime` / `restoreRuntime` every 500, `serializeMorphState` / `deserializeMorphState`, `reset()`: count 0; `static_assert(noexcept(...))` on every member and free function |
| SC-012 | `BassMorph_NonFinite` | nonfinite | bit-pattern NaN (quiet and signalling), ±Inf, ±denormal, −0 positions; for each F32 leaf: NaN, +Inf, −Inf, `nextafter(lo, −inf)`, `nextafter(hi, +inf)`; each enum leaf `maxEnum + 1`; corrupted state and runtime bytes; `restoreRuntime` with target, p or p_a = 1e-20f, 1e-16f, `std::nextafter(1e-15f, 0.0f)` or −0 → false (C-7, S4.8); after every call: `resolved()` finite and `isValidMorphState`, invalid endpoints rejected, previous target kept |
| SC-013 | `BassMorph_Partition` | engine | one sample-stamped event list (seed 0x8013, 20 s); host blocks {1, 7, 32, 64, 511, 4096} and two engines; all `Resolved` sequences bitEqual |
| SC-014 | `BassMorph_SampleRates` | engine | fs {22.05, 44.1, 48, 96, 192} k × ramp {5, 40, 2000}: SC-001 identity on 1 000 pairs; traversal ⌈rampMs·fs/32 000⌉ ± 1; prepare(8000) and prepare(384000) traverse like 22 050 and 192 000 |
| SC-015 | `BassMorph_Audibility` | chain | showcase, E1 and C3, 48 kHz, p ∈ {0, 1}, Blend: D_cx, D_nl, D_st each move ≥ 3 dB, D_br ≥ 0.25 oct, D_t ratio ≥ 2; p = 1: E_sub(Lock) ≥ E_sub(Blend) + 2 dB (subject to C-9) |
| SC-016 | `BassMorph_MonoSum_<Note>`, `BassMorph_MonoSum_AnchorThroughTail_<Note>` `[long]` | monosum | 21 p + a ramp through SOLID → WIDE at the default ramp, E1 and C3: `bitEqual(anchorL, anchorR)` per render (the spec's clause; structurally true at the tap, because the fixture writes the mono Anchor to both channels and scales both by the same gain, S5.5); `evaluateMonoSumCell` with the resolved field at that p, `monoSumCellPasses` (Q_sub ≤ 1.0 dB, ρ ≥ 0.99). **Discriminating read** (`_AnchorThroughTail`): the same 21 positions and the SOLID → WIDE ramp with `muteCharacter = true`, so the full tail (field stage, dynamics, DC, limiter) processes the Anchor alone: side-to-mid of `outL/outR` over the read window ≤ −120 dB (the floor) at every position, `bitEqual(outL, outR)` printed; positive control: the same read with the character unmuted at p = 1 (WIDE, Spread 1) ≥ −60 dB, so the read can fail |
| SC-017 | `BassMorph_Aliasing_<fs>` `[long]` | aliasing | E0 / E1 / E2. (a) grid: `maxNonlinearPair`, 21 frozen points: `wideAliasedDbfs(characterL/R, fs, f0, selectedAlgorithm)` ≤ −80 dBFS per channel (rule 2 selects Fracture at every p ∈ (0, 1), C-16); crossing arms on `algorithmCrossingPair` (C-16): fresh engine ramped up from 0, `REQUIRE(selection(Character) == A)` at θ_up − 0.005 and `REQUIRE(selection(Character) == B)` at θ_up, then each a frozen steady render under the same bar (f = f₀/2 for the Fracture arm). (b) recorded: a 1 s 0 → 1 ramp render of `algorithmCrossingPair`; Hann window [g_sw, g_sw + one period + 10 ms] at the recorded switch grid sample g_sw; non-harmonic in-band energy excluding ±(2 / T_window) of every k·f₀, printed per (fs, note) to `evidence/aliasing-crossfade.txt`; the same record for `maxNonlinearPair` (switch at its first interval with p > 0) |
| SC-018 | `BassMorph_Cpu` `[performance][.perf]` | perf | random pair with every leaf differing and no governing amount 0; 2000 ms ramp re-targeted 0 ↔ 1 so every interval moves; 10⁶ intervals timed, median of 3; ns per interval / (32/48 000 s) ≤ 0.05 % (≤ 333 ns) |
| FR-083 record | `BassMorph_WorstTransitionIntervalRecord` `[performance][.perf]` | perf | `renderMorphPoly` 4 voices, every discrete group switching at the same interval; the worst interval's wall time printed against Phase 5's 2.65 % crossfade cell; not gated |
| SC-019 | `BassMorph_LawTable` (b), `MorphState_LeafArity` (a)(c) | engine / morph_state | (a) structured bindings `auto& [m1, …, m19] = s;` (MorphState), 7 (AmpEnvelopeFields), 16 (MotionConfig), 6 / 3 / 11 / 6 (recipe, excitation, field, anchor) + `static_assert(13 + 7 + 16 + 6 + 3 + 11 + 6 == kMorphLeafFieldCount)`; (c) table names contain none of the FR-003 exclusions (`outputTrim`, `glide`, `legato`, `priority`, `poly`, `voice`, `partial`, `guard`, `seed`, `transientDuration`, `compression`, `wideOffset`, `memory`, `retriggerPhase`); (b) per leaf, Blend, reset at 0.25: only that leaf changed, value within 2 ulp (lin / equal-power) or 8 ulp (log) of the double closed form; stepped rows at θ_up − 0.005 from below → A, at θ_up → B. **Governing-amount rows** (`BassMorph_LawTable_GoverningAmount`), **all five C-2 groups**: Character (`characterAmount`), ImageMode (`field.spread`), FieldMotion (`field.motionDepth`; the four selectors differ), Motion (`motion.amount`; the eight selectors differ; the non-zero side is −0.5 in one variant and +0.5 in another, and the zero side is −0.0f in one variant, proving `!= 0` and ±0; `motion.depth` stays 1 on both sides, so a `motion.depth` accessor fails), Anchor (`anchor.level`, at p_a in Blend). For each group, four cases: 0 on A only, 0 on B only, both non-zero (0.8, or ±0.5 for Motion), both 0; every discrete leaf of the group differs between A and B, every other leaf is equal. Fresh engine, reset at 0, then p ∈ {0, 0.01, 0.25, θ_up − 0.005, 0.99, 1} visited **ascending** by `setPosition` + advance until landed (so θ_up − 0.005 is reached from below), then the same points descending. At each landed point `selection(g)` and every discrete leaf of the group are asserted bitwise: rule-2 cases → the non-silent side at every q ∈ (0, 1), A at 0, B at 1; both non-zero and both 0 → A at 0.01, 0.25, θ_up − 0.005 ascending, B at 0.99 and 1, B kept at θ_up − 0.005 descending (inside the band), A at 0.25, 0.01, 0; the other four groups' selections follow rule 3 (A below θ_up, B at or above it; T015-1 corrected the earlier "stay A's throughout") values throughout |
| FR-039 | `BassMorph_AmpEnvelopeImmediateApply` | chain | ADSR-only harness on the grid driver (engine + `ADSREnvelope` with the fixture's change-only setters, no core), 48 kHz, ramp `kMinPositionRampMs` (Δp_max = 0.1333), gate on at 0, morph 0 → 1 started at 200 ms. Pairs differ in one leaf each, other `ampEnv` leaves equal (decay 2000 ms on both): (i) sustain 0 → 0.8 during Decay: each interval's output step at the grid sample equals Δs·(1 − table(phase)) within 1e-5 (the "immediate" claim, S4.10); (ii) the same sustain morph after Sustain is reached: max per-sample \|Δg\| < the per-interval Δs (the "smoothed" claim); (iii) decayCurve 0.7 → −0.7 during Decay: the step equals range·Δtable(phase) within 1e-5; (iv) decayMs 50 → 2000 during Decay: output bitEqual to the static-A reference until the Sustain entry (the "latched at stage entry" claim). For (i)–(iii) the max per-sample \|Δg\| over the morph and its ratio to 10^(0.5/20) × the static reference's max \|Δg\| in the same window are printed to `evidence/amp-envelope-immediate.txt` (recorded for Phase 9, not gated, S4.10) |
| SC-020 | `BassMorph_ValidityClosure` | engine | 100 000 random pairs (seed 0x8020) × random p × random mode, reset: `isValidMorphState(resolved)` and every continuous leaf in [min, max] |
| SC-021 | `BassMorph_Lifecycle` | engine | (a) unprepared engine: `setEndpoint` ×2, `setPosition(0.7)`, `advanceInterval()` bitEqual to a prepared engine's `reset()` at 0.7, `position() == 0.7f`; (b) swept 0 → 0.5 then `reset()` keeps A; 1 → 0.5 keeps B; reset at 0.53 → B, 0.47 → A from either side; fresh engine reset at 0.5 → A |
| SC-022 | `BassMorph_RampTimeChange` | engine | mid-traversal change to 5 / 40 / 2000 ms: `position()` bitwise unchanged by the call; next intervals \|Δp\| == new Δp_max within (1 + 4ε) until landing; traversal per SC-003(c); 4.9 → 5, 2001 → 2000, NaN keeps |
| SC-023 | `BassMorph_DropLatched` | chain | DropEnvelope-only harness on the grid driver: A 12 st / 200 ms / 0, B 36 st / 2000 ms / 1; note at p = 0, ramp 0 → 1 during the Drop; per-sample `currentOffset()` bitwise equal to an unmorphed A envelope; second note-on at 2.5 s (p = 1, first Drop idle): bitwise equal to a fresh B envelope triggered at the same sample |
| SC-024 | `BassMorph_EndpointEditMidMorph` (engine), `BassMorph_EndpointEditMidMorphRender` | engine / chain | p = 0.3, Blend, random pair (seed 0x8024, governing amounts non-zero): B → B′ (every continuous leaf different): next-interval change equals law(A, B′) − law(A, B) in double within the SC-019(b) ulp bounds, complete in that interval; discrete: A's algorithm edited → resolved algorithm changes at the next interval, `discreteSetterCalls` +1, render meets SC-004(a)(b) against a direct-setter reference |

---

## S8. Build integration

### S8.1 `dsp/tests/CMakeLists.txt`

1. In the `dsp_systems_tests` source list, after `unit/systems/bass_stereo_field_perf_test.cpp` (line 762), a commented
   "Profundum Phase 8 (specs/profundum-phase8-bass-morph) - ENUMERATED, not globbed" block listing:
   `morph_state_test.cpp`, `bass_morph_engine_test.cpp`, `bass_morph_nonfinite_test.cpp`, `bass_morph_helpers_test.cpp`,
   `bass_morph_chain_test.cpp`, `bass_morph_click_test.cpp`, `bass_morph_trajectory_test.cpp`,
   `bass_morph_monosum_test.cpp`, `bass_morph_aliasing_test.cpp`, `bass_morph_perf_test.cpp`, `bass_morph_probe_test.cpp`.
2. In the `-fno-fast-math` block, after `unit/systems/bass_stereo_field_nonfinite_test.cpp` (line 1259):
   `# Profundum Phase 8: injects NaN/Inf bit patterns (SC-012). The perf TU stays out (shipping FP mode).` +
   `unit/systems/bass_morph_nonfinite_test.cpp`.

One task edits this file (a shared file). No other CMake file changes; no new target.

### S8.2 Commands (Windows, full CMake path) and lanes

```
# ONE rebuild (all Phase 8 TUs are in dsp_systems_tests; no tracked shared header is modified):
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests
# Gate (the workflow's filter; captured to a log on the first run, never re-run for output):
build/windows-x64-release/bin/Release/dsp_systems_tests.exe '[bass-morph]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'
# [long] lane: one shard per [bass-morph][long] TEST_CASE (list generated with --list-tests filtered by [bass-morph][long]),
# run concurrently (cores − 2), each to its own log. Only this phase's TUs: no shared header changed.
# Probe (C-9) before any gated render is trusted:  dsp_systems_tests.exe "BassMorph_SumTapCoherenceProbe"
# CPU lane ALONE, last, after the machine has idled:  node tools/run-cpu-tests.js dsp_systems_tests  (filtered to [bass-morph])
# Gates: node tools/check-portability.js; node tools/lint-apple-globals.js; wsl --shutdown afterwards;
# clang-tidy on the new TUs by file list (per-file timeout), after reconfiguring build/windows-ninja.
```

Lane scope (root CLAUDE.md, user 2026-10-18): `git status` will show only new files under `dsp/include` and
`dsp/tests`, plus the one CMake list edit. No existing header under `dsp/include`, `dsp/src`, `plugins/shared` or
`tests/test_helpers` is modified, so no other suite's per-push set runs. Pluginval does not apply (no plugin code).

---

## S9. Risks and mitigations

| ID | Risk | Mitigation |
|---|---|---|
| R-1 | **C-9** sum-tap coherence can make SC-015's Anchor clause and SC-008(d) red by construction | Probe first (group 1); stop and ask with the measured c, θ and the predicted margins; remediation only in the showcase pair or the laws (Q7), never a relaxed bar |
| R-2 | CPU: 333 ns per interval at 48 kHz is tight (S10) | template visitor (no indirect calls), cached logs, cache-on-unchanged; T-early skeleton timing in group 2 before the full suite; if over, escalate (no budget change) |
| R-3 | Nested-aggregate NSDMI rejected by Clang / GCC (Phase 1 R-7) | no `static constexpr MorphState` in-class; members use the `pendingExcitation_{}` precedent (profundum_core.h:1177); `static_assert`s after the struct; `node tools/check-portability.js` before commit |
| R-4 | `consteval` / generic-lambda constant evaluation differs between MSVC and Clang | use `decltype(leaf)::value` (never the parameter's value) inside the visitor lambda; check-portability compiles the header under clang |
| R-5 | `-ffast-math` folding the non-finite guards | only `detail::isFinite` / `isNaN` (opaque barrier); the non-finite TU in the `-fno-fast-math` block; the identity rules are selections, not arithmetic |
| R-6 | Sub-threshold targets: any target with 0 < \|x\| < `kDenormalThreshold` = **1e-15** (db_utils.h:38), not only IEEE denormals, strands `SlewLimiter` (`flushDenormal` after landing, smoother.h:557), in both `posSlew_` and `anchorSlew_` (Punch p³ < 1e-15 for p < 1e-5) | C-7: `setPosition` normalises below 1e-15 to +0 and `curve()` returns a flushed value; `restoreRuntime` rejects −0 and 0 < \|x\| < 1e-15 (S4.8); gated by `BassMorph_SubThresholdLanding` and SC-012 |
| R-7 | DAZ converting a denormal endpoint leaf to 0 in `float → double` | the resolved value stays valid (0 is inside every box that admits denormals); SC-020 runs without FTZ/DAZ; render tests that set FTZ do not assert endpoint closure |
| R-8 | `std::uniform_*_distribution` differs across standard libraries | `randomValidState` maps raw `std::mt19937` words itself (S5.1) |
| R-9 | `bool` UB on decode | C-13: bytes > 1 rejected before conversion |
| R-10 | macOS global-name collisions | only nested names and `detail::morph_state_io`; `tools/lint-apple-globals.js` before commit |
| R-11 | Namespace-scope `Waveform` from `lfo.h` vs `AnchorOscillator::Waveform` | `MotionConfig::cycleShape` spelled `::Krate::DSP::Waveform`; no new `Waveform` declared |
| R-12 | SC-004 grid render cost (84 ordered pairs × 2 notes × 3 ramps) | A/B static references shared across ramp times; renders start the ramp at 0.25 s and stop one period after the transition end; one `[long]` shard per field family, run concurrently |
| R-13 | SC-008(c) loudness dip > 1.5 dB mid-morph | the trajectory shards print L_K(p); a red is remediated in the laws or the pair (Q7), escalated with the per-p table; no engine gain output exists or is added |
| R-14 | Character-stage switch queue with very short ramps (5 ms) crossing θ twice inside one crossfade | by design (FR-021): the stage queues the last request (bass_character_stage.h:241–255); SC-004 runs at 5 ms |

---

## S10. CPU estimate (sizing, not compliance; SC-018 measures)

Per moving interval with every leaf differing: 11 `std::exp2(double)` (≈ 10–20 ns each with MSVC's CRT) ≈ 110–220 ns;
2 `sqrt` ≈ 5 ns; 33 linear leaves in double ≈ 30–40 ns; 16 selects ≈ 10 ns; selection update and two `SlewLimiter`
steps ≈ 10 ns. Total ≈ 165–285 ns against the 333 ns bar (0.05 % of 666.7 µs). The margin is thin enough that the
skeleton is timed early (R-2). A static position costs ≈ 15 ns (the cache, C-6).

---

## S11. Task outline (for tasks.md)

1. **Group 1 — headers and the risk probe (sequential):** `morph_state.h` (struct, table, visitor, validation,
   serialisation) with `morph_state_test.cpp` failing first; `bass_morph_engine.h` skeleton; the CMake registration
   (single task, all Phase 8 TUs as they are created in later groups get stub files now so the list is edited once);
   the fixture header and its self-tests; `BassMorph_SumTapCoherenceProbe` (C-9) — **the main loop runs the probe and
   records `evidence/coherence-probe.txt` before group 3 starts**.
2. **Group 2 — engine behaviour** (`bass_morph_engine_test.cpp`, `bass_morph_nonfinite_test.cpp`): ramp, laws, selection
   machine (with the `groupIndex` mapping, S4.2), Anchor curves (flushed, C-7), runtime round trip, lifecycle, the
   FR-012 / FR-014 API cases, `BassMorph_MonotoneLaws`, `BassMorph_SubThresholdLanding`, the five-group
   `BassMorph_LawTable_GoverningAmount`; filling the S4.10 Anchor punch / ghost row from anchor_oscillator.h; an early
   skeleton timing of `advanceInterval` (a few seconds, recorded, not gated) for R-2.
3. **Group 3 — rendered criteria** (`[P]` across new TUs): chain (SC-009, SC-010, SC-015, SC-023, SC-024 render,
   SC-005 / SC-006 rendered, FR-039 `BassMorph_AmpEnvelopeImmediateApply`), click (SC-004), trajectory (SC-008), monosum
   (SC-016 incl. the Anchor-through-tail read and the fixture's `muteCharacter` option), aliasing (SC-017 with
   `algorithmCrossingPair`, C-16), perf (SC-018, FR-083 record).
4. **Group 4 — integration (main-loop tasks):** the gate filter run; the `[long]` lane script (shards from
   `--list-tests` filtered to `[bass-morph][long]`, concurrent, logs to `evidence/`); portability and Apple-globals
   lints; clang-tidy by file list; the CPU lane alone, last. Lane-running tasks write their scripts and are deferred to
   the main loop at close.

---

## S12. Handoffs (FR-083)

**Phase 9 (voice / engine):**
- Call `BassMorphEngine::advanceInterval()` once per 32-sample grid point, **before** `BassStereoField::advanceControl()`
  and before every voice's component setters in that interval (FR-015); read `resolved()` from every voice.
- Mass split (FR-054): MassLaw base = resolved depth / body / foldAmount / anchor.level; `setMass(resolved.mass)`; the
  Anchor level = `evaluateRow(AnchorLevel, resolved.anchorMass, resolved.anchor.level)`.
- Change-only setters for the ADSR curve amounts (each call rebuilds a 256-entry table, adsr_envelope.h:160–176), and
  the C-8 neutral-curve decision.
- `AmpEnvelopeFields` and `MotionConfig` are final for version 1; Phase 9 appends only, under a version bump (FR-007),
  and records any field its envelope class or Motion component cannot realise as a spec deviation.
- An Anchor fold-feed smoother in the voice (C-11).
- The FR-039 immediate-apply set (S4.10): `ampEnv.sustain` during Decay and the three `ampEnv` curve amounts during
  their stage reach the output as steps at the grid sample (measured and recorded in
  `evidence/amp-envelope-immediate.txt`); the `ampEnv` times are latched at stage entry. Phase 9's envelope class
  choice decides whether these receivers get a smoother or are accepted with the recorded step.
- The recorded worst-interval measurement (`BassMorph_WorstTransitionIntervalRecord`) for Phase 9's CPU budget.
- The MotionConfig selectors' click-free gate (every ordered pair of `source`, `target`, `cycleShape`, `polarity`,
  `cycleSync`, `cycleRetrigger`, and the SC-004 `cycleNote` × `cycleModifier` subset, amount 1, depth 1, against
  SC-004(a)(b)) once the Motion component exists.
- Drop morphs act on the next note (FR-039).
- Join-point finding (probe, 2026-10-19; spec.md FR-083): the nonlinear chain (Fold, SHAPE, Character) regenerates a
  fundamental whose phase against the Anchor rotates with the settings and the note. Across the B7-2 harvest
  (evidence/harvest/showcase-*.txt, Lock rows) theta spans -148.68 deg (cand 0, E1 p=1, harvest/showcase-0.txt:23) to
  +179.2 deg (cand 24, C3 p=1, harvest/showcase-0.txt:130) and the magnitude ratio c spans 0.232 (:216) to 8.03 (:66);
  cand 24 at C3 p=1 cancels the f0 fully (floor margin -9.8826 dB, :130). The shipped showcase pair reads c 0.872 / theta +27.9 deg at E1 p=1 and
  c 0.881 / theta +9.85 deg at C3 p=1 (evidence/coherence-probe.txt:13, :22). Phase 3's "add coherently" (its FR-010)
  holds at the core tap only; Phase 9 decides the Anchor join point / phase reference with this in hand
  (evidence/phase-handoffs.txt 9.10).

**Phase 11 (state / parameters):** `kMorphStateBytes` (201) × 2 + `BassMorphEngine::kMorphRuntimeBytes` (20) per
instance; restore through `setEndpoint` ×2 + `restoreRuntime`, never `reset()`; the Edit A / Edit B routing (keep a
`MorphState` copy per endpoint, edit one field, `setEndpoint`); IDs 100–199 (position) and 1000–1099 (morph control:
Anchor morph mode, ramp "Morph smoothing" 5–2000 ms, edit target); thread hand-off of endpoint edits to the audio thread.

**Phase 12 (UI):** the S3.2 law / axis table and FR-051's curves as "what the user sees while morphing"; while the
Anchor morph mode is Lock, B's Anchor fields are stored but inaudible, so the UI dims them (Q5); the Anchor morph mode
and `AnchorOscillator::Mode` are two controls with distinct labels.

---

## Review notes

Plan review, 2026-10-19. All issues were applied; none was rejected.

- SC-019(b) governing amount: the S7.4 row now enumerates all five C-2 groups with the four spec cases, including a
  negative `motion.amount`. Applied.
- SC-017 crossing arms vs rule 2: new C-16 FOR RATIFICATION. The 21-point grid stays on `maxNonlinearPair`; the arms and
  the (b) window move to `algorithmCrossingPair`. Applied. The alternative (drop the arms) is recorded in C-16 as
  rejected.
- FR-039 receiver inventory: new S4.10, an S12 handoff, and `BassMorph_AmpEnvelopeImmediateApply`. Applied. The step is
  recorded, not gated: the review allowed either, and the envelope class is Phase 9's choice.
- FR-012 / FR-014 API tests: `BassMorph_EndpointApi` and `BassMorph_AnchorModeApi`. Applied.
- FR-037: `BassMorph_MonotoneLaws` with zero tolerance, and the S4.6 argument narrowed to linear and equal-power.
  Applied.
- SC-016 discriminating read: `BassMorph_MonoSum_AnchorThroughTail_<Note>` with a positive control, and the tap check
  labelled structural. Applied.
- Flush threshold (SlewLimiter): C-7, S4.4, S4.8 and R-6 now use `kDenormalThreshold` = 1e-15 (db_utils.h:38) for both
  slews, gated by `BassMorph_SubThresholdLanding` and new SC-012 restore cases. Applied. One correction to the review
  text: `kDenormalThreshold` is at `Krate::DSP` namespace scope, not `detail::`, and the flush line is smoother.h:557.
  The separate p_a-flush issue suggested the `FLT_MIN` bound; it is covered by the stronger 1e-15 flush in `curve()`,
  which also handles p³ values that are normal but below 1e-15.
- `SelectionGroup` indexing: the two duplicate issues (rt-layers, reuse-reality) are resolved together by
  `MorphState::groupIndex` (S3.1, S4.2), used in the resolver, `updateSelections`, `selection()` and the S4.8 bitmask,
  with static_asserts. `None` is kept in the enum because the table's non-Stepped rows need it.
- C-4 exactness: the wording now gives 0.5 ulp(float) plus a double-rounding error of order 2^−53 relative, and no
  longer claims exactness. Applied.
