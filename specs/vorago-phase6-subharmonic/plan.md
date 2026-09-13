# Implementation Plan: Vorago Phase 6 — Subharmonic Engine

**Spec:** `specs/vorago-phase6-subharmonic/spec.md` (1580 lines, read in full this session, **after**
the review that added the 2026-09-13 Clarifications Q1–Q8, D-10…D-14 and SC-021/SC-022).
**Roadmap:** `specs/Vorago-roadmap.md` Part A → Phase 6 (lines 302–321); reuse row L10 (line 118);
ODR note (127–129); cross-cutting constraints (508–532) including the Dormancy rule (517–525) and
the shared-component rule (530–532); Open Question 4 (541–542).
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/subharmonic_engine.h`
(header-only), four new test TUs, **one** new test-helper header, two edits in
`dsp/tests/CMakeLists.txt`, one edit in `dsp/lint_all_headers.cpp`, and the FR-076 / SC-018
transcription + ruling in the phase's compliance record.
**Test target:** `dsp_systems_tests` (all four new TUs). SC-016 regression targets:
`dsp_core_tests`, `dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11.

**The phase in one sentence:** three shipped `SubOscillator` dividers hung off two synthetic
`PhaseAccumulator` masters, summed with the house three-factor gain, pushed through
`TwoPoleLP → SaturationProcessor → ×trackGain → DCBlocker2`, and **added** to an untouched dry
path — with **no new DSP mathematics anywhere** and **no amendment to any shipped header**.

---

## S0. Verification ledger — every signature below was read this session

Nothing in this plan is quoted from the spec without re-opening the header. Where the spec and the
code disagree, **the code wins**, and the disagreement is recorded in **S14 (Spec corrections)**.

| Claim this plan is built on | Verified at |
|---|---|
| `SubOscillator`: `explicit SubOscillator(const MinBlepTable* = nullptr)` `:118`, `prepare(double)` `:141`, `reset()` `:169`, `setOctave(SubOctave)` `:185`, `setWaveform(SubWaveform)` `:191`, `setMix(float)` `:198`, `process(bool, float)` `:221`, `processMixed(float,bool,float)` `:344`. Copy- and move-assignable (`:126-129`) | `processors/sub_oscillator.h` |
| `SubOscillator::prepare` **hard-fails** to `prepared_ = false` and returns when `table_ == nullptr \|\| !table_->isPrepared() \|\| table_->length() > 64` (`:143-147`); `process()` then returns `0.0f` unconditionally (`:222-224`) | `sub_oscillator.h` |
| `SubOscillator`'s **constructed default waveform is `Square`** (`SubWaveform waveform_ = SubWaveform::Square;` `:381`) and default octave `OneOctave` (`:380`). `prepare()` does **not** reset either | `sub_oscillator.h` |
| The flip-flop toggles only when `masterPhaseWrapped` is true (`:243-259`); `TwoOctaves` toggles the second stage on the first stage's rising edge (`:252-259`). Sine/Triangle sub phase is **re-zeroed on the output flip-flop's rising edge** (`:265-268`) and then advanced by `subPhase_.increment = masterInc / octaveFactor` (`:290-292`, `:307-309`) | `sub_oscillator.h` |
| `SubOscillator::sanitize` maps NaN to `0.0f` via `detail::opaqueFloatBits` and **clamps to `[-2, +2]`** (`:356-364`) — the per-tone output bound is **2.0, not 1.0** (S14 C-4) | `sub_oscillator.h` |
| `SubOscillator` does **not** guard its arguments: `masterPhaseEstimate_ += masterInc` (`:228`) with no finiteness test | `sub_oscillator.h` |
| `SubOctave { OneOctave = 0, TwoOctaves = 1 }` `:48-51`; `SubWaveform { Square = 0, Sine = 1, Triangle = 2 }` `:60-64`. Both at **namespace scope** in `Krate::DSP` | `sub_oscillator.h` |
| `MinBlepTable`: **non-copyable, movable** (`:55-59`), `prepare(size_t oversamplingFactor = 64, size_t zeroCrossings = 8)` `:76` (allocates; `length_ = zeroCrossings * 2`, `table_.resize(length_*oversamplingFactor)` `:216-217`, `blampTable_.resize(tableSize)` `:243`), `length()` `:372`, `isPrepared()` `:377`. Nested `struct Residual` `:400` with `explicit Residual(const MinBlepTable&)` `:403` (`buffer_(table.length(), 0.0f)`) and a **default constructor** `:410` | `primitives/minblep_table.h` |
| `PhaseAccumulator` (a public-member value type): `double phase = 0.0` `:155`, `double increment = 0.0` `:156`, `[[nodiscard]] bool advance()` `:161` (single `-= 1.0` wrap, so `increment` must stay `< 1.0`), `reset()` `:170` (zeroes phase, **preserves** increment), `setFrequency(float,float)` `:177` | `core/phase_utils.h` |
| `calculatePhaseIncrement(float f, float fs)` is exactly `double(f)/double(fs)`, `0.0` when `fs == 0` (`:47-55`) | `core/phase_utils.h` |
| `EnvelopeFollower`: `prepare(double, size_t)` `:106` — **`(void)maxBlockSize;` `:107`, allocates nothing** — `reset()` `:127`, `processSample(float)` `:163` (**"Does NOT validate input" `:162`**), `getCurrentValue()` `:190`, `setMode(DetectionMode)` `:202` (**early-returns when the mode is unchanged, `:203`**), `setAttackTime` `:219`, `setReleaseTime` `:226`, `setSidechainEnabled` `:234`, `getAttackTime()` `:260`, `getReleaseTime()` `:265`. **Default mode is `DetectionMode::Amplitude` (`:380`)** — RMS must be written explicitly | `processors/envelope_follower.h` |
| `EnvelopeFollower::processRMS` (`:309-325`) smooths in the squared domain then `std::sqrt`; a NaN takes the release branch and poisons `squaredEnvelope_` **forever** (`detail::flushDenormal` does not clear a NaN). For a sine of peak `A` the settled reading is `A/√2` | `envelope_follower.h` |
| `EnvelopeFollower` constants: `kMinAttackMs = 0.1f` `:88`, `kMaxAttackMs = 500.0f` `:89`, `kMinReleaseMs = 1.0f` `:90`, `kMaxReleaseMs = 5000.0f` `:91`, `kMinSidechainHz = 20.0f` `:94` | `envelope_follower.h` |
| `TwoPoleLP`: `prepare(double)` `:57`, `setCutoff(float)` `:66`, `getCutoff()` `:74` (**returns the raw requested value, not the Biquad-applied one**), `process(float)` `:83`, `processBlock` `:93`, `reset()` `:102`. `updateCoefficients()` configures `FilterType::Lowpass` at `kButterworthQ` (`:107-117`) | `primitives/two_pole_lp.h` |
| `Biquad::process` resets and returns `0.0f` on a non-finite input, and flushes its state; `detail::clampFrequency` clamps to `[kMinFilterFrequency, kMaxFrequencyRatio * sampleRate]` with **`kMaxFrequencyRatio = 0.495f` applied to the SAMPLE RATE, not to Nyquist** (`:88`, `:155-166`) — so at the 8 kHz floor the applied ceiling is 3960 Hz and this component's own 2000 Hz ceiling always binds first (S14 C-2) | `primitives/biquad.h` |
| `SaturationProcessor`: `prepare(double, size_t)` `:122` (**`dryBuffer_.resize(maxBlockSize)` `:136`**), `reset()` `:149` (snaps its three smoothers, resets its own DC blocker, clears `dryBuffer_`), `process(float*, size_t)` `:175`, `processSample(float)` `:228` (**"Does NOT apply DC blocking" `:225-227`; dry early-exit at `mix < 0.0001f` `:238-240`**), `setType` `:264`, `setInputGain(float dB)` `:273`, `setOutputGain(float dB)` `:283`, `setMix` `:294`, getters `:255-320`. `kMinGainDb = -24.0f` `:104`, `kMaxGainDb = +24.0f` `:105` | `processors/saturation_processor.h` |
| `SaturationProcessor`'s three parameter smoothers are **`OnePoleSmoother`s, not `LinearRamp`s** (`:415-417`), advanced once each per `processSample` (`:233-235`); `mix_` defaults to `1.0f` (`:413`); `type_` defaults to `SaturationType::Tape` (`:410`) | `saturation_processor.h` (S14 C-6 corrects spec A-2) |
| `saturateTape(x)` is `Sigmoid::tanh(x)` (`:343-347`) → `FastMath::fastTanh`, a Padé (5,4) approximant returning **exactly ±1 beyond ±3.5** (`fast_math.h:65-89`). Evaluated at the domain edge `x = 3.5` the Padé gives `0.99924 < 1`, so `|tanh| ≤ 1` holds with no overshoot | `saturation_processor.h`, `core/fast_math.h` |
| `DCBlocker2` (2nd-order **Bessel** high-pass, `Q = 1/√3 = 0.5773502691896258f`, `:363`): `prepare(double, float cutoffHz = 10.0f)` `:298` (`sampleRate_ = max(sampleRate, 1000.0)`), `reset()` `:307`, `setCutoff(float)` `:313`, `process(float)` `:328` (**no finiteness branch — a NaN sticks in `y1_`/`y2_` forever**), `processBlock` `:346`. `calculateCoefficients()` clamps `fc` to `[1, fs/4]` (`:358-360`) and builds the RBJ high-pass (`:373-378`) | `primitives/dc_blocker.h` |
| `DCBlocker2` peak gain, computed from the shipped coefficients at `fc = 18 Hz`, `fs = 48 kHz`: `\|H(e^{jπ})\| = (b0-b1+b2)/(1-a1+a2) = 3.99185/3.99186 = 0.999998`. `Q ≤ 1/√2` ⇒ monotone rise, no resonant peak ⇒ `kInfrasonicFilterPeakGain = 1.0f` is an upper bound, not an approximation | `dc_blocker.h` (arithmetic done this session) |
| `BreathingModulator : public ModulationSource`: `prepare(double)` `:143`, `reset()` `:152` (→ `initState()`, **re-seeds the RNG from `configuredSeed_`**), `setSeed(uint32_t)` `:164`, `setRate(float)` `:169`, `setDepth(float)` `:176`, `setIrregularity(float)` `:183`, `getRate/getDepth/getIrregularity` `:188-190`, `process()` `:197`, `processBlock(size_t)` `:208` (`processBlock(0)` is a documented no-op `:209-211`), `getCurrentValue()` `:221` (**`override`, i.e. virtual**), `getSourceRange()` `:226` → `{-1,+1}` | `processors/breathing_modulator.h` |
| `BreathingModulator::shapeOutput()` returns `std::clamp(depth_ * bipolar, -1, 1)` (`:286`) with `depth_ = kDefaultDepth = 1.0f` (`:112`, `:294`) — so at the library default `getCurrentValue()` **is** the raw bipolar breath, which is exactly what the house affine span (Q7) needs | `breathing_modulator.h` |
| **`drawCycleJitter()` (`:262-271`) is the ONLY consumer of the modulator's RNG, and it draws nothing when `irregularity_ == 0.0f`** — the constructed and post-`prepare` default (`kDefaultIrregularity = 0.0f`, `:113`, `:296`). With irregularity at 0 the seed has **no observable effect** (S14 **C-5**, the blocker this plan closes) | `breathing_modulator.h` |
| `BreathingModulator` constants: `kMinRate = 0.01f` `:108`, `kMaxRate = 0.5f` `:110`, `kDefaultRate = 0.1f` `:111`, `kOutputSmoothMs = 20.0f` `:117`, `kJitterSpan = 0.5f` `:128`, `kMinJitter = 0.1f` `:130`, `kDefaultBreathSeed = 0xB2EAu` `:131` | `breathing_modulator.h` |
| `LinearRamp`: `configure(float rampTimeMs, float sampleRate)` `:329`, `setTarget` `:342` (**a NaN target MUTES to 0 instantly — `target_ = current_ = increment_ = 0` — with no counter and no way back, `:343-348`**), `getTarget` `:358`, `getCurrentValue` `:364`, `process()` `:370` (early-outs on `current_ == target_`, lands **exactly** on the target `:379-383`), `isComplete()` `:409`, `snapToTarget()` `:414`, `snapTo(float)` `:421` | `primitives/smoother.h` |
| `calculateLinearIncrement(delta, rampTimeMs, sampleRate)` = `delta / (rampTimeMs*0.001*sampleRate)` (`:100-108`) — the ramp time is **per transition**, whatever the distance, so every gate move takes exactly `kGainRampMs` | `smoother.h` |
| `OnePoleSmoother`: `configure(float smoothTimeMs, float sampleRate)` `:160`, `setTarget` `:170`, `getCurrentValue` `:191`, `process()` `:197`, `advanceSamples(size_t)` `:243`, `snapTo(float)` `:263`. `calculateOnePoleCoefficient` takes a **per-sample** rate (`:77-93`) | `smoother.h` |
| `dbToGain(float)` `:293` is **`constexpr`** (via `detail::constexprPow10`) and returns `0.0f` for NaN; `gainToDb` `:317`. `detail::isNaN` `:99`, `detail::isFinite(float)` `:118`, `detail::isFinite(double)` `:125`, `detail::flushDenormal` `:245`, `detail::isInf` `:260` | `core/db_utils.h` |
| `deriveStreamSeed(uint32_t base, size_t salt)` `:102-113` — lowbias32, guaranteed non-zero (`Xorshift32::seed(0)` silently substitutes its own default, `:72-74`) | `core/random.h` |
| House pattern actually read: `kControlChunkSamples = 64` + `static_assert` `:135-136`, `kGainRampMs = 50.0f` `:143`, `kOutputClamp = 4.0f` `:144`, `kMinUsableSampleRate = 8000.0` + ordering `static_assert` `:281-296`, nested `PrepareConfig` `:297-306` (designated initialisers mandatory), 13-step `prepare()` `:322-405` (`sanitise(sampleRate, 48000.0)` at `:324`; `setSeed(seed_)` **last**, `:400`), `sanitise(float,float)` `:1225-1227`, `getClampEngagementCount()` `:904`, `getAllocatedBytes()` `:912`, `getMaxBlockSamples()` `:920`, salt table + overlap `static_assert`s `:929-947`, sleep-edge exact `== 0.0f` `:1680` | `systems/resonance_drift_network.h` |
| The three-factor gain, verbatim: `getSourceGain` = `levelRamp.getCurrentValue() * breathGain * gate.getCurrentValue()` (`:932-938`); `updateBreathGain` = `1.0f + kBreathGainSpan * s.breathDepth * b` with `b = laneValue(...)` (`:1842-1844`); `kBreathGainSpan = 0.45f` `:174`; `kGainRampMs = 50.0f` `:178`; `kOutputClamp = 4.0f` `:180`; `laneValue` = `clamp(sanitise(lane.getCurrentValue(), 0.0f), -1, 1)` (`:2247-2249`); `getAllocatedBytes()` `:999` | `systems/noise_organism.h` |
| Two-entry-point tapped render, ONE body: `processBlock(...)` forwards to `processBlockTapped(..., nullptr, n)` (`:913-917`); the guard ladder is null-pointers → `numSamples == 0` → `!prepared_`, in that order (`:944-956`); the control grid is an **absolute residue carried across calls** (`:958-966`) | `systems/feedback_ecology.h` |
| The control-step dormancy/sleep-edge form: skip flag and sleep edge both evaluated inside `updateControl()`, sleep edge **before** any write, gated on `gate.isComplete() && gate.getCurrentValue() == 0.0f` (`:1878-1905`); the "push the filter cutoff only when it really moved" idiom with `kCutoffPushRelative = 1e-4f` (`:205`, `:1936-1940`) | `systems/feedback_ecology.h` |
| The fault-injection probe pattern: `namespace detail { struct FeedbackEcologyNonFiniteProbe; }` declared and never defined (`:166-172`), `friend struct detail::FeedbackEcologyNonFiniteProbe;` (`:1525`) | `systems/feedback_ecology.h` |
| `FFT`: `prepare(size_t fftSize)` `:147` — the doc says `[256, 8192]` but the **code only requires a power of two** (`:150-155`); `forward(const float*, Complex*)` `:186`; `isPrepared()` `:255`; `size()` `:249`. `Complex` `:55` | `primitives/fft.h` |
| `Oversampler<Factor, NumChannels>`: `prepare(double, size_t, OversamplingQuality = Economy, OversamplingMode = ZeroLatency)` `:288`, `upsample(const float*, float*, size_t, size_t channel = 0)` `:344`, `reset()` `:360`. `TruePeakLimiter` uses `Oversampler<4,1> osL_, osR_` (`:172-173`) and folds the raw sample into the max (`processChunk`, `:121-162`); `kDefaultCeilingDb = -1.0f` `:46` | `primitives/oversampler.h`, `processors/true_peak_limiter.h` |
| `render_fingerprint.h`: `kSampleTolerance = 5.0e-4f` `:58`, `kMetricTolerance = 2.5e-4` `:61`, `struct RenderFingerprint` `:63`, `fingerprintRender(std::span<const float>)` `:73`, `compareFingerprints(actual, ref, metricTol, sampleTol)` `:122`. Namespace `Krate::DSP::TestUtils` (`:50-52`) | `tests/test_helpers/render_fingerprint.h` |
| `artifact_detection.h`: `ClickDetectorConfig` `:38` (**`sampleRate` defaults to 44100 — must be overridden**), `ClickDetection` `:72`, `ClickDetector(config)` `:103`, `prepare()` `:106`, `detect(const float*, size_t)` `:130` | `tests/test_helpers/artifact_detection.h` |
| `allocation_detector.h`: `AllocationDetector` `:48`, `AllocationScope` `:111` (`getAllocationCount()`, `hadAllocations()`) | `tests/test_helpers/allocation_detector.h` |
| `spectral_analysis.h` exports `frequencyToBin` `:40`, `calculateAliasedFrequency` `:58`, `willAlias` `:85`, `getHarmonicBins` `:149`, `getAliasedBins` `:167`, `detail::toDb` `:194`, `detail::sumBinPower` `:207` — **and no peak interpolation of any kind** (directory listed and grepped this session) | `tests/test_helpers/spectral_analysis.h` |
| `signal_metrics.h::calculateTHD` `:111` caps its transform at 8192 points and picks harmonics over ±2-bin windows — unusable below ~200 Hz, exactly as spec A-4 states | `tests/test_helpers/signal_metrics.h` |
| `buffer_comparison.h::calculateCorrelation` is `template <size_t N>` over `std::array` (`:200-210`) and lives in namespace **`TestHelpers`** (`:17`), *not* `Krate::DSP::TestUtils` — so a new pointer/length overload in `TestUtils` is a new name in a different namespace, with no overload-resolution interaction | `tests/test_helpers/buffer_comparison.h` |
| `tests/test_helpers/CMakeLists.txt` is `add_library(test_helpers INTERFACE)` with an include directory and **no source list** — a new helper header needs **no CMake edit** | `tests/test_helpers/CMakeLists.txt` |
| `dsp_systems_tests` source list opens `:324`, the Phase-5 TUs sit at `:446-449`, the list closes `:450`. The `-fno-fast-math` block opens `:542`, the Phase-5 non-finite entry is at `:876`, the block's `PROPERTIES` line is `:877` | `dsp/tests/CMakeLists.txt` |
| `dsp/lint_all_headers.cpp` — Vorago Phase 2 include `:179`, Phase 3 `:182`, Phase 5 `:185`, Layer 4 block opens `:187` | `dsp/lint_all_headers.cpp` |
| Perf idiom: ns per 512-sample block at 48 kHz (`:68-76`), best-of-25 × 500 blocks after 400 warm-up (`:220-222`), `[.perf]`, `static_assert`ed baselines evaluated on every CI leg (`:26-28`), the stop-and-surface rule verbatim (`:57-64`), the `bestTrialNs(runBlock)` driver (`:288`), `kBlockPeriodNs = (512/48000)*1e9` (`:200`) | `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp` |
| Lints that exist and must pass: `tools/lint-layers.js`, `lint-odr.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `lint-simd-aligned-loadstore.js`, `tools/check-portability.js`, `tools/run-cpu-tests.js` | `tools/` |

**ODR sweep, re-run this session from the repo root:** `grep -rn "SubharmonicEngine" dsp/ plugins/ tools/`
→ **0 hits**. `ls dsp/include/krate/dsp/systems/subharmonic_engine.h` → no such file. The near-names
`SubharmonicValidator` (`processors/subharmonic_validator.h`), `SubOscillator`, `SubOctave`,
`SubWaveform` exist and do not collide. `SubharmonicEngineNonFiniteProbe`, `SubharmonicEngine::Tone`,
`SubharmonicEngine::ToneState`, `SubharmonicEngine::PrepareConfig` — 0 hits each.

---

## S1. Component: `SubharmonicEngine`

### S1.1 Header, layer, includes, and the one thing the spec does not say (FR-001)

`dsp/include/krate/dsp/systems/subharmonic_engine.h`, `namespace Krate::DSP`, header-only, Layer 3.

```cpp
#pragma once

#include <krate/dsp/core/db_utils.h>          // dbToGain, detail::isFinite/isNaN/isInf, flushDenormal
#include <krate/dsp/core/math_constants.h>    // FR-001's normative list (see the note below)
#include <krate/dsp/core/phase_utils.h>       // PhaseAccumulator, calculatePhaseIncrement
#include <krate/dsp/core/random.h>            // deriveStreamSeed
#include <krate/dsp/primitives/smoother.h>    // LinearRamp
#include <krate/dsp/primitives/minblep_table.h>
#include <krate/dsp/primitives/two_pole_lp.h>
#include <krate/dsp/primitives/dc_blocker.h>  // DCBlocker2
#include <krate/dsp/processors/sub_oscillator.h>
#include <krate/dsp/processors/breathing_modulator.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/saturation_processor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
```

No Layer 3 and no Layer 4 header. `tools/lint-layers.js` must pass. `math_constants.h` is in FR-001's
normative list and is retained; the header itself only needs `std::exp2` / `std::log2` / `std::abs`
from `<cmath>`, so the include is documentation of the dependency FR-001 declares, not a live use.

**THE ONE STRUCTURAL RULE THE SPEC DOES NOT STATE — the class must be non-copyable AND
non-movable** (S14 **C-1**):

```cpp
    /// FR-003: construction is allocation-free but NOT `= default`. The three per-tone
    /// configuration scalars (level, breath rate, breath depth) differ per tone, so they
    /// cannot be expressed as in-class member initialisers on `ToneState`; the constructor
    /// runs the same `applyDefaults()` that prepare() step (9) runs, so every getter reports
    /// the FR-020/FR-021/FR-031/FR-035/FR-040/FR-041/FR-051 default on a default-constructed
    /// object, exactly as FR-003 requires. `applyDefaults()` allocates nothing and must be
    /// safe on an unprepared object (S2.2) — in particular the pushes the forwarding getters
    /// of S8 read back (follower attack/release, breath rate, waveform) are NOT gated on
    /// `prepared_`. Audio state is still unusable until prepare() (FR-050's passthrough).
    SubharmonicEngine() noexcept { applyDefaults(); }
    ~SubharmonicEngine() = default;

    // The three SubOscillators hold `const MinBlepTable*` pointing at THIS object's
    // blepTable_ member (sub_oscillator.h:118, :370). MinBlepTable is non-copyable
    // (minblep_table.h:56-57), so the copy operations are already implicitly deleted —
    // but it IS movable (:58-59), so a MOVE would be implicitly generated, would move
    // the table's vectors out, and would leave every SubOscillator pointing at the
    // MOVED-FROM object. That object then destructs and all three tones read freed
    // memory. Deleting the move is not defensive: it is the only thing standing between
    // this composition and a use-after-free no test would provoke.
    SubharmonicEngine(const SubharmonicEngine&) = delete;
    SubharmonicEngine& operator=(const SubharmonicEngine&) = delete;
    SubharmonicEngine(SubharmonicEngine&&) = delete;
    SubharmonicEngine& operator=(SubharmonicEngine&&) = delete;
```

Phase 10 therefore holds `SubharmonicEngine` by value as a member, never in a reallocating container.

### S1.2 Constants (all `static constexpr`, class scope, `kPascalCase`)

```cpp
    // ---- structure ----------------------------------------------------------
    static constexpr std::size_t kNumTones = 3;                    // FR-010

    /// The shared library-wide control clock (harmonic_cloud.h:144, continuous_body.h:97,
    /// noise_organism.h:150, resonance_drift_network.h:135). A component that drifted off
    /// it would decorrelate the per-voice modulation grid at Phase 10.
    static constexpr std::size_t kControlChunkSamples = 64;        // FR-007
    static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

    /// prepare()'s sample-rate floor (FR-006). NOT 1 Hz: kMasterNyquistRatio * 1 Hz and
    /// kLowpassNyquistRatio * 1 Hz both fall BELOW their paired floors, inverting the
    /// std::clamp bounds — UB, and MSVC's <algorithm> fires _STL_VERIFY.
    static constexpr double kMinUsableSampleRate = 8000.0;         // FR-006

    static constexpr float kGainRampMs  = 50.0f;                   // noise_organism.h:178
    static constexpr float kGlideMs     = 50.0f;                   // S5.3, the cutoff glide
    static constexpr float kOutputClamp = 4.0f;                    // noise_organism.h:180
    static constexpr float kCutoffPushRelative = 1e-3f;            // S5.3 (feedback_ecology.h:205)

    static constexpr std::uint32_t kDefaultSeed = 0x5E6BA51Cu;

    // ---- minBLEP table (FR-004, D-8) ----------------------------------------
    static constexpr std::size_t kBlepOversampling  = 64;
    static constexpr std::size_t kBlepZeroCrossings = 8;
    static_assert(kBlepZeroCrossings * 2 <= 64,
                  "SubOscillator::prepare rejects a table with length() > 64 "
                  "(sub_oscillator.h:143-147) and then returns 0.0f forever");

    // ---- pitch (FR-013) ------------------------------------------------------
    static constexpr float kMinFundamentalHz     = 8.0f;
    static constexpr float kMaxFundamentalHz     = 4186.0f;        // C8, A-5
    static constexpr float kDefaultFundamentalHz = 55.0f;          // A1
    /// Applied to the 4f/3 master, NOT to f: the largest master increment is 0.4.
    static constexpr float kMasterNyquistRatio   = 0.3f;
    static_assert(static_cast<double>(kMasterNyquistRatio) * kMinUsableSampleRate
                      > static_cast<double>(kMinFundamentalHz),
                  "the FR-013 clamp range must stay ordered at the lowest accepted rate");
    static_assert(kMasterNyquistRatio * (4.0f / 3.0f) < 1.0f,
                  "PhaseAccumulator::advance subtracts 1.0 exactly once (phase_utils.h:161-168)");

    // ---- infrasonic handling (FR-016, FR-042, Q2) ---------------------------
    static constexpr float kMinToneHz          = 12.0f;            // far-below backstop gate
    static constexpr float kInfrasonicFilterHz = 18.0f;            // DCBlocker2 corner
    static_assert(kMinToneHz < kInfrasonicFilterHz,
                  "the backstop must sit below the filter it backs up");

    // ---- per-tone level and breathing (FR-020, FR-021, FR-022) --------------
    static constexpr float kMinToneLevelDb = -60.0f;               // exact fader bottom
    static constexpr float kMaxToneLevelDb = +6.0f;
    static constexpr float kBreathGainSpan = 0.45f;                // noise_organism.h:174

    /// FR-003's per-tone defaults, declared HERE rather than only in applyDefaults()'s prose:
    /// they differ per tone, so they cannot be `ToneState` member initialisers, and the
    /// constructor (S1.1) needs them as much as prepare() step (9) does.
    static constexpr std::array<float, kNumTones> kDefaultToneLevelDb{-18.0f, -24.0f, -30.0f};
    static constexpr std::array<float, kNumTones> kDefaultToneBreathRateHz{0.037f, 0.023f, 0.014f};
    static constexpr std::array<float, kNumTones> kDefaultToneBreathDepth{0.35f, 0.25f, 0.45f};

    /// FR-021's rate range. The engine clamps with THESE and then pushes; the modulator clamps
    /// again with its own (breathing_modulator.h:170-172) and `getToneBreathRate` forwards to
    /// `breath.getRate()` (S8), so the two clamps must agree or the getter would contradict the
    /// engine's declared range. The static_asserts are what keep them agreeing.
    static constexpr float kMinBreathRateHz = 0.01f;
    static constexpr float kMaxBreathRateHz = 0.5f;
    static_assert(kMinBreathRateHz == BreathingModulator::kMinRate &&
                  kMaxBreathRateHz == BreathingModulator::kMaxRate,
                  "FR-021's range must mirror the modulator's own (breathing_modulator.h:108-110)");
    /// S14 C-5. WITHOUT a non-zero irregularity the modulator never touches its RNG
    /// (breathing_modulator.h:262-271), the FR-070 seed is inert, and SC-011 (b) is
    /// unreachable. This is the smallest value that makes the seed observable.
    static constexpr float kDefaultBreathIrregularity = 0.25f;

    // ---- tracking (FR-030 – FR-035) -----------------------------------------
    static constexpr float kDefaultTrackingAmount    = 1.0f;
    static constexpr float kMinTrackReferenceDb      = -48.0f;
    static constexpr float kMaxTrackReferenceDb      = 0.0f;
    static constexpr float kDefaultTrackReferenceDb  = -18.0f;
    static constexpr float kDefaultFollowerAttackMs  = 120.0f;
    static constexpr float kDefaultFollowerReleaseMs = 800.0f;
    /// FR-031's ranges, named for the same reason as kMin/kMaxBreathRateHz: the engine clamps
    /// with these, the follower clamps again with its own (envelope_follower.h:219-229), and
    /// `getFollowerAttackMs`/`getFollowerReleaseMs` forward to `follower_.getAttackTime()` /
    /// `getReleaseTime()` (S8). Without the named bounds FR-061's "the getter reports the
    /// applied (clamped) value" has no stated range at this level.
    static constexpr float kMinFollowerAttackMs  = 0.1f;
    static constexpr float kMaxFollowerAttackMs  = 500.0f;
    static constexpr float kMinFollowerReleaseMs = 1.0f;
    static constexpr float kMaxFollowerReleaseMs = 5000.0f;
    static_assert(kMinFollowerAttackMs == EnvelopeFollower::kMinAttackMs &&
                  kMaxFollowerAttackMs == EnvelopeFollower::kMaxAttackMs &&
                  kMinFollowerReleaseMs == EnvelopeFollower::kMinReleaseMs &&
                  kMaxFollowerReleaseMs == EnvelopeFollower::kMaxReleaseMs,
                  "FR-031's ranges must mirror the follower's own (envelope_follower.h:88-91)");
    static_assert(kDefaultFollowerAttackMs != EnvelopeFollower::kDefaultAttackMs &&
                  kDefaultFollowerReleaseMs != EnvelopeFollower::kDefaultReleaseMs,
                  "FR-031's defaults differ from the shipped 10/100 ms, so they MUST be pushed; "
                  "S8's forwarding getters are what make an omitted push detectable");

    // ---- chain (FR-040 – FR-042) --------------------------------------------
    static constexpr float kMinLowpassHz        = 40.0f;
    static constexpr float kMaxLowpassHz        = 2000.0f;
    static constexpr float kDefaultLowpassHz    = 120.0f;
    /// Inert at every accepted rate (0.45 * 8000 = 3600 > kMaxLowpassHz). Written so the
    /// ordering std::clamp requires is visible HERE rather than inferred.
    static constexpr float kLowpassNyquistRatio = 0.45f;
    static_assert(kLowpassNyquistRatio * static_cast<float>(kMinUsableSampleRate) > kMinLowpassHz,
                  "the FR-040 clamp range must stay ordered at the lowest accepted rate");

    static constexpr float kMinDriveDb     = 0.0f;
    static constexpr float kMaxDriveDb     = 12.0f;
    static constexpr float kDefaultDriveDb = 3.0f;
    static_assert(kMaxDriveDb <= 24.0f && kMinDriveDb >= -24.0f,
                  "drive must stay inside SaturationProcessor::kMin/kMaxGainDb (:104-105)");

    // ---- output (FR-050 – FR-054) -------------------------------------------
    static constexpr float kMinWetGainDb     = -60.0f;             // exact fader bottom
    static constexpr float kMaxWetGainDb     = +6.0f;
    static constexpr float kDefaultWetGainDb = 0.0f;

    // ---- the FR-052 structural bounds, as real static_asserts ---------------
    /// SubOscillator::sanitize clamps EVERY tone's output to [-2, +2]
    /// (sub_oscillator.h:356-364) — the Square path's minBLEP residual can and does exceed
    /// 1.0. The spec's FR-052 formula omits this factor (S14 C-4).
    static constexpr float kSubOscillatorOutputBound = 2.0f;
    static constexpr float kMaxPreSaturationMagnitude =
        static_cast<float>(kNumTones) * kSubOscillatorOutputBound *
        dbToGain(kMaxToneLevelDb) * (1.0f + kBreathGainSpan);          // ~= 17.36
    /// FastMath::fastTanh is exactly +/-1 beyond +/-3.5 and 0.99924 at the domain edge;
    /// outputGain = dbToGain(-driveDb) <= 1 for driveDb >= 0.
    static constexpr float kSaturatorOutputBound = 1.0f;
    /// DCBlocker2 at Q = 1/sqrt(3) <= 1/sqrt(2): magnitude rises monotonically from 0 at DC
    /// toward 1 and never exceeds it (verified numerically at 18 Hz / 48 kHz: 0.999998).
    static constexpr float kInfrasonicFilterPeakGain = 1.0f;
    static constexpr float kMaxPreClampMagnitude =
        kSaturatorOutputBound * kInfrasonicFilterPeakGain * dbToGain(kMaxWetGainDb);  // ~= 2.00

    static_assert(kMaxPreSaturationMagnitude > kSaturatorOutputBound,
                  "the saturator, not the clamp, must be the stage that catches the peak");
    static_assert(kMaxPreClampMagnitude < kOutputClamp,
                  "FR-054's clamp is a backstop, not a shaping stage");
```

`dbToGain` is `constexpr` (`db_utils.h:293`, via `detail::constexprPow10`), so every bound above is a
compile-time constant and every `static_assert` is real. **That is the whole of FR-052's rung 1** —
there is no runtime code behind it.

### S1.3 Nested types

```cpp
    /// FR-010. APPEND ONLY — this becomes a persisted plugin parameter at Phase 12.
    /// Nested deliberately: SubOctave and SubWaveform are already at namespace scope in a
    /// header this one includes (sub_oscillator.h:48, :60), and a third Sub* enum out there
    /// is a future ODR liability for no gain. Precedent: EnvelopeFilter::FilterType
    /// (processors/envelope_filter.h:89).
    enum class Tone : std::uint8_t { Div2 = 0, Div4 = 1, FifthBelow = 2 };

    /// FR-003. Callers MUST use designated initialisers — PrepareConfig{.maxBlockSamples = 512}
    /// — so no narrowing conversion hides in a positional brace init (Clang errors where MSVC
    /// does not). Nested, following NoiseOrganism::PrepareConfig (noise_organism.h:190) and
    /// ResonanceDriftNetwork::PrepareConfig (:299).
    struct PrepareConfig {
        /// Clamped [64, 8192], retained and reported. It sizes exactly ONE thing: the
        /// SaturationProcessor::dryBuffer_ this component never reads (FR-073).
        std::size_t maxBlockSamples = 2048;
    };

    [[nodiscard]] static constexpr std::size_t index(Tone t) noexcept {
        return static_cast<std::size_t>(t);
    }
```

Every setter and getter takes `std::size_t tone`, not `Tone`: FR-009's "out-of-range tone index is a
silent no-op" is a statement about an *index*, and Phase 12 will hand this a raw parameter value.
`Tone` exists for call-site readability and for the internal index constants.

### S1.4 Public API — the complete shape the implementer types

```cpp
    // ---- lifecycle ----------------------------------------------------------
    void prepare(double sampleRate, const PrepareConfig& config) noexcept;   // ONLY allocator
    void reset() noexcept;

    // ---- render (FR-050, FR-062) -------------------------------------------
    void processBlock(const float* inL, const float* inR,
                      float* outL, float* outR, std::size_t numSamples) noexcept;
    void processBlockTapped(const float* inL, const float* inR,
                            float* outL, float* outR, float* subTap,
                            std::size_t numSamples) noexcept;

    // ---- setters (FR-060) ---------------------------------------------------
    void setFundamentalHz(float hz) noexcept;
    void setToneLevelDb(std::size_t tone, float db) noexcept;
    void setToneWaveform(std::size_t tone, SubWaveform waveform) noexcept;
    void setToneBreathRate(std::size_t tone, float hz) noexcept;
    void setToneBreathDepth(std::size_t tone, float normalized) noexcept;
    void setTrackingAmount(float normalized) noexcept;
    void setTrackReferenceDb(float db) noexcept;
    void setFollowerAttackMs(float ms) noexcept;
    void setFollowerReleaseMs(float ms) noexcept;
    void setLowpassCutoffHz(float hz) noexcept;
    void setDriveDb(float db) noexcept;
    void setWetGainDb(float db) noexcept;
    void setSubToMainEnabled(bool enabled) noexcept;
    void setSeed(std::uint32_t seed) noexcept;

    // ---- getters (FR-061) — every one reports the APPLIED value -------------
    [[nodiscard]] double      getSampleRate()      const noexcept;
    [[nodiscard]] std::size_t getMaxBlockSamples() const noexcept;
    [[nodiscard]] bool        isPrepared()         const noexcept;

    [[nodiscard]] float getFundamentalHz()                    const noexcept;
    [[nodiscard]] float getToneFrequencyHz(std::size_t tone)  const noexcept;
    [[nodiscard]] float getToneLevelDb(std::size_t tone)      const noexcept;
    [[nodiscard]] SubWaveform getToneWaveform(std::size_t tone) const noexcept;
    [[nodiscard]] float getToneBreathRate(std::size_t tone)   const noexcept;
    [[nodiscard]] float getToneBreathDepth(std::size_t tone)  const noexcept;
    [[nodiscard]] float getToneBreathValue(std::size_t tone)  const noexcept;   // raw b_i
    [[nodiscard]] float getToneCurrentGain(std::size_t tone)  const noexcept;   // FR-022 product
    [[nodiscard]] bool  isToneDormant(std::size_t tone)           const noexcept;
    [[nodiscard]] bool  isToneInfrasonicFloored(std::size_t tone) const noexcept;

    [[nodiscard]] float getTrackingAmount()    const noexcept;
    [[nodiscard]] float getTrackedEnvelope()   const noexcept;   // last envNorm
    [[nodiscard]] float getTrackingGain()      const noexcept;   // the ramp's current value
    [[nodiscard]] float getTrackReferenceDb()  const noexcept;
    [[nodiscard]] float getFollowerAttackMs()  const noexcept;
    [[nodiscard]] float getFollowerReleaseMs() const noexcept;

    [[nodiscard]] float getLowpassCutoffHz()  const noexcept;
    [[nodiscard]] float getDriveDb()          const noexcept;
    [[nodiscard]] float getWetGainDb()        const noexcept;
    [[nodiscard]] bool  getSubToMainEnabled() const noexcept;

    [[nodiscard]] std::uint32_t getSeed()                 const noexcept;
    [[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept;
    [[nodiscard]] std::size_t   getAllocatedBytes()       const noexcept;
```

Out-of-range tone index returns the documented neutral: `0.0f`, `false`, `SubWaveform::Sine`.

### S1.5 Private state layout (exact members, in declaration order)

```cpp
private:
    /// FR-055 fault injection — see S7.5.
    friend struct detail::SubharmonicEngineNonFiniteProbe;

    struct ToneState {
        SubOscillator      osc{};        // re-pointed at &blepTable_ in prepare() step 5
        BreathingModulator breath{};
        LinearRamp         levelRamp{};  // FR-022 factor 1 — retargeted only by setToneLevelDb
        LinearRamp         gate{};       // FR-022 factor 3 — retargeted only at a backstop edge

        // Configuration. These three differ per tone, so they CANNOT carry the FR-020/FR-021
        // defaults as in-class initialisers; the constructor's applyDefaults() call (S1.1)
        // writes kDefaultToneLevelDb[i] / kDefaultToneBreathDepth[i] before any caller can
        // observe them, which is how FR-003 is satisfied. The zeroes below are never read.
        float levelDb        = 0.0f;
        float breathDepth    = 0.0f;
        // NO breathRateHz member: the rate lives in the modulator, which clamps it, and
        // getToneBreathRate() forwards to breath.getRate() (S8) so a stored-but-never-pushed
        // rate is not representable.
        float breathValue    = 0.0f;     // last raw b_i, FR-061
        float breathGain     = 1.0f;     // FR-022 factor 2, held constant across a control chunk
        float lastGateTarget = 1.0f;     // edge detection for the FR-016 backstop
        SubWaveform waveform = SubWaveform::Sine;
        bool  infrasonicFloored = false;
    };

    MinBlepTable                     blepTable_{};      // D-8: ONE table, shared by all three
    std::array<ToneState, kNumTones> tones_{};
    PhaseAccumulator                 masterUnison_{};   // f    -> Div2, Div4
    PhaseAccumulator                 masterFifth_{};    // 4f/3 -> FifthBelow
    float                            incUnison_ = 0.0f; // float cache of masterUnison_.increment
    float                            incFifth_  = 0.0f;

    EnvelopeFollower    follower_{};
    TwoPoleLP           lowpass_{};
    SaturationProcessor saturator_{};
    DCBlocker2          blocker_{};

    LinearRamp trackGainRamp_{};   // FR-032, per-sample, advances even while dormant
    LinearRamp wetGainRamp_{};     // FR-051, per-sample, advances even while dormant
    LinearRamp cutoffGlide_{};     // S5.3, log2-Hz, advanced ONCE PER CONTROL CHUNK

    double        sampleRate_ = 48000.0;
    PrepareConfig config_{};
    float maxFundamentalHz_ = kMaxFundamentalHz;   // cached rate-derived ceiling
    float maxLowpassHz_     = kMaxLowpassHz;       // cached rate-derived ceiling

    float fundamentalHz_     = kDefaultFundamentalHz;
    float trackingAmount_    = kDefaultTrackingAmount;
    float trackReferenceDb_  = kDefaultTrackReferenceDb;
    float trackReferenceRms_ = dbToGain(kDefaultTrackReferenceDb);  // cached denominator
    // NO followerAttackMs_ / followerReleaseMs_ members: both live in the follower, which
    // clamps them, and the two getters forward to it (S8). A build that stored them and
    // never called setAttackTime/setReleaseTime was green on every criterion in an earlier
    // draft of this plan; with the value held in exactly one place that build cannot exist.
    float lowpassHz_         = kDefaultLowpassHz;  // the CLAMPED REQUEST — what the getter reports
    float pushedCutoffHz_    = 0.0f;               // last value handed to lowpass_.setCutoff
    float driveDb_           = kDefaultDriveDb;
    float wetGainDb_         = kDefaultWetGainDb;
    float trackedEnvNorm_    = 0.0f;               // FR-034

    std::uint32_t seed_             = kDefaultSeed;
    std::uint32_t clampEngagements_ = 0;
    std::size_t   controlPhase_     = 0;           // FR-007 ABSOLUTE residue, carried across calls
    std::size_t   allocatedBytes_   = 0;

    bool subToMainEnabled_ = true;   // FR-064
    bool chainActive_      = false;  // FR-025/FR-026 — mirrors FeedbackEcology's engineActive
    bool prepared_         = false;
```

**Why `chainActive_` is a member and not a predicate recomputed per chunk:** FR-026's clear must fire
exactly once, on the *edge*. A recomputed predicate cannot tell the first dormant chunk from the
thousandth, and re-running three `reset()` calls every chunk would erase the FR-025 CPU saving that
SC-013 (c) gates. Same shape as `Loop::engineActive` (`feedback_ecology.h:1901-1904`).

### S1.6 Salt table (FR-072) — APPEND ONLY

```cpp
    static constexpr std::size_t kSaltBreath   = 0;   // + tone index, three entries
    static constexpr std::size_t kSaltNextFree = 8;
    static_assert(kSaltBreath + kNumTones <= kSaltNextFree, "salt table overflow");
```

Renumbering a base silently changes every Phase-6 render. A later phase adding a lane takes
`kSaltNextFree` and moves that constant up.

---

## S2. `prepare()` — the numbered step order (FR-004), and why each step sits where it does

```cpp
void prepare(double sampleRate, const PrepareConfig& config) noexcept
```

1. **Sample rate.** `sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));`
   (`sanitise(double,double)` per `resonance_drift_network.h:1229-1231`);
   `const auto fs = static_cast<float>(sampleRate_);`
2. **Clamp the caller's request ONCE, here.**
   `config_.maxBlockSamples = std::clamp(config.maxBlockSamples, std::size_t{64}, std::size_t{8192});`
   `applyDefaults()` never touches `PrepareConfig` fields (the `noise_organism.h` rule).
3. **Cache the rate-derived ceilings**, so no clamp pair is ever built inline:
   `maxFundamentalHz_ = std::min(kMaxFundamentalHz, kMasterNyquistRatio * fs);`
   `maxLowpassHz_ = std::min(kMaxLowpassHz, kLowpassNyquistRatio * fs);`
   The two `static_assert`s in S1.2 prove both pairs stay ordered at the 8 kHz floor.
4. **`blepTable_.prepare(kBlepOversampling, kBlepZeroCrossings);` — BEFORE any sub-oscillator.**
   A correctness requirement, not tidiness: `SubOscillator::prepare` sets `prepared_ = false` and
   returns when the table is null or unprepared (`sub_oscillator.h:143-147`), after which `process()`
   returns `0.0f` **forever** (`:222-224`). A Sine-only configuration on an unprepared table is
   **silent**, not merely un-BLEPped. `length() = 2 * 8 = 16 ≤ 64`, asserted in S1.2.
5. **The three tone generators.** For `i` in `[0, kNumTones)`:
   `tones_[i].osc = SubOscillator(&blepTable_);` then `tones_[i].osc.prepare(sampleRate_);`
   then the **structural** octave assignment — *not* configuration, no setter exposes it:
   `Div2 → SubOctave::OneOctave`, `Div4 → SubOctave::TwoOctaves`,
   `FifthBelow → SubOctave::OneOctave`.
   The assignment (rather than a constructor argument) is how a re-prepare re-points a live object at
   the same table; `Residual`'s default constructor (`minblep_table.h:410`) makes the assigned-in
   temporary allocation-free, and `osc.prepare()` then allocates the real 16-float residual.
6. **The rest of the composition:** `tones_[i].breath.prepare(sampleRate_)`;
   `follower_.prepare(sampleRate_, config_.maxBlockSamples)` (ignores the block size, allocates
   nothing); `lowpass_.prepare(sampleRate_)`;
   `saturator_.prepare(sampleRate_, config_.maxBlockSamples)` (the one dead heap term, FR-073);
   `blocker_.prepare(sampleRate_, kInfrasonicFilterHz)`.
7. **Configure every smoother.** All `LinearRamp`s except `cutoffGlide_` at
   `configure(kGainRampMs, fs)`. `cutoffGlide_.configure(kGlideMs, fs / float(kControlChunkSamples))`
   — **the single most likely implementer trap in this phase**: `calculateLinearIncrement`
   (`smoother.h:100-108`) divides by `rampTimeMs * 0.001 * sampleRate`, so a ramp advanced once per
   64 samples must be told the *control* rate, not the audio rate. Getting this wrong makes the glide
   64× too slow (3.2 s) and SC-020 (c)'s 52 ms window unreachable.
8. **`prepared_ = true;`** — before step 9, so the setter pushes in `applyDefaults()` are live.
9. **`applyDefaults()`** — S2.2. The ONLY path back to the defaults; `reset()` is
   configuration-preserving (FR-005).
10. **Snap everything to steady state, no ramp in flight** (FR-004 step 7). Per tone:
    `levelRamp.snapTo(toneLevelGain(levelDb))`, `gate.snapTo(gateSteady(i))`,
    `lastGateTarget = gateSteady(i)`, `infrasonicFloored = (gateSteady(i) == 0.0f)`,
    `refreshBreath(i)`. Then `trackGainRamp_.snapTo(1.0f - trackingAmount_)` (the follower reads 0 at
    prepare, so `envNorm = 0`); `wetGainRamp_.snapTo(wetGain(wetGainDb_))`;
    `cutoffGlide_.snapTo(std::log2(lowpassHz_))`; `lowpass_.setCutoff(lowpassHz_)`;
    `pushedCutoffHz_ = lowpassHz_`.
11. **Counters and grid phase:** `controlPhase_ = 0; clampEngagements_ = 0; trackedEnvNorm_ = 0.0f;`
    `chainActive_ = !allTonesDormant(); allocatedBytes_ = computeAllocatedBytes();` (S9).
12. **LAST: `setSeed(seed_);`** — for the same reason `NoiseOrganism` and `ResonanceDriftNetwork` do
    it last. `BreathingModulator::prepare` calls `initState()`, which re-seeds from
    `configuredSeed_`; a seed distributed before step 6 would be discarded by that prepare.
13. **`reset()`** — rewinds audio and modulation state without touching configuration.

### S2.1 The shared clamp helpers

```cpp
    [[nodiscard]] static constexpr float sanitise(float v, float neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }
    [[nodiscard]] static constexpr double sanitise(double v, double neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }
    /// FR-020's fader bottom. dbToGain(-60) is 1e-3, NOT zero — the exact-zero mapping is what
    /// makes FR-023's dormancy test an `== 0.0f` comparison rather than an epsilon, and what
    /// makes SC-014 (a)'s bit-identity reachable at all.
    [[nodiscard]] static constexpr float toneLevelGain(float db) noexcept {
        return (db <= kMinToneLevelDb) ? 0.0f : dbToGain(db);
    }
    [[nodiscard]] static constexpr float wetGain(float db) noexcept {
        return (db <= kMinWetGainDb) ? 0.0f : dbToGain(db);
    }
```

### S2.2 `applyDefaults()` — the normative default table, pushed all the way through

Called by **the constructor** (S1.1, FR-003) and by `prepare()` step (9), and by nothing else. It
allocates nothing, touches no `PrepareConfig` field, and **must be correct on an unprepared object**:
none of the pushes below may be gated on `prepared_`, or a default-constructed engine would report
the composed objects' defaults instead of this component's and FR-003 would be violated. Every push
that is rate-dependent is re-issued at step (9), after step (6) has re-prepared the object that owns
it, so a stale-coefficient window cannot survive `prepare()`.

It writes **every** configuration scalar **and** pushes each into the composed object that owns the
behaviour, because several of those objects ship with a different default than this component wants:

| What | Value | Why it must be written explicitly |
|---|---|---|
| `tones_[i].waveform` | `SubWaveform::Sine` ×3 (FR-015, D-6) | `SubOscillator`'s constructed default is **`Square`** (`sub_oscillator.h:381`) and `prepare()` does not change it. Omit this and the shipped default is a square sub. |
| `tones_[i].levelDb` | `kDefaultToneLevelDb = {-18, -24, -30}` dB (FR-020, Q3) | — |
| breath rate | `kDefaultToneBreathRateHz = {0.037, 0.023, 0.014}` Hz (FR-021) | pushed with `breath.setRate(...)` and **not stored here**; the modulator clamps to `[0.01, 0.5]` (all three are inside) and `getToneBreathRate` forwards to `breath.getRate()` (S8). |
| `tones_[i].breathDepth` | `kDefaultToneBreathDepth = {0.35, 0.25, 0.45}` (FR-021) | held by **this** engine; the modulator stays at its library depth of `1.0` (Q7). **Never call `breath.setDepth`** — that would square the depth. |
| `breath.setIrregularity(kDefaultBreathIrregularity)` | `0.25` ×3 | **S14 C-5.** Without it the seed is inert and SC-011 (b) cannot pass. |
| `follower_.setMode(DetectionMode::RMS)` | RMS (FR-030) | the shipped default is `Amplitude` (`envelope_follower.h:380`). |
| `follower_.setSidechainEnabled(false)` | off (FR-030) | its range starts at 20 Hz and would blind the follower to exactly the low body the subs follow. |
| follower attack / release | `120 / 800` ms (FR-031) | pushed with `setAttackTime` / `setReleaseTime` and **not stored here**. The shipped defaults are `kDefaultAttackMs = 10.0f` / `kDefaultReleaseMs = 100.0f` (`envelope_follower.h:92-93`), so an omitted push leaves the follower **12× / 8× too fast**. Because the two getters forward to the follower (S8), the omission shows up as a wrong getter value in `SubharmonicEngine_ControlSurfaceContract` and SC-019 (b); the audio bracket that also catches it is SC-001 (c2). |
| `fundamentalHz_` | `55.0` Hz (FR-013, A1) | pushed through `setFundamentalHz`, the ONE owner of both accumulators' increments (S2.3). |
| `trackingAmount_` | `1.0` (FR-033) | — |
| `trackReferenceDb_` | `-18.0` dB (FR-035) | also refreshes `trackReferenceRms_`. |
| `lowpassHz_` | `120.0` Hz (FR-040) | pushed to `lowpass_.setCutoff` and to `cutoffGlide_` / `pushedCutoffHz_` in step 10. |
| `driveDb_` | `3.0` dB (FR-041) | pushed as `saturator_.setInputGain(+3)` **and** `setOutputGain(-3)`. |
| `saturator_.setType(SaturationType::Tape)` | Tape (D-1) | already the shipped default (`:410`); written anyway so a future change to that default cannot silently retype this chain. |
| `wetGainDb_` | `0.0` dB (FR-051) | — |
| `subToMainEnabled_` | `true` (FR-064) | — |

`saturator_.setMix` is **never** called: `mix_` stays at its constructed `1.0f` (`:413`), above the
`< 0.0001f` dry early-exit (`:238-240`). `osc.setMix` is **never** called: `mix_` stays `0.0f` and has
no effect on `process()` — only `processMixed` reads it (FR-010).

### S2.3 `setFundamentalHz` — the ONE owner of both master increments (FR-011 – FR-014)

```cpp
void setFundamentalHz(float hz) noexcept {
    if (!detail::isFinite(hz)) return;                       // FR-009: reject, previous stands
    fundamentalHz_ = std::clamp(hz, kMinFundamentalHz, maxFundamentalHz_);
    // PHASE IS NOT TOUCHED (FR-014): PhaseAccumulator::setFrequency writes `increment` only
    // (phase_utils.h:177-179), so a pitch change is phase-continuous.
    masterUnison_.increment = calculatePhaseIncrement(fundamentalHz_,
                                                      static_cast<float>(sampleRate_));
    // *** THE FIFTH-BELOW CONSTRUCTION (FR-012) ***
    // NOT setFrequency(4.0f/3.0f * f, fs): that rounds 4f/3 to float BEFORE the divide and makes
    // the two increments' ratio only float-accurate (~1e-7 relative), which shows up as a slow
    // relative phase creep between the octave and the fifth. ONE double multiply makes the ratio
    // exactly the double-rounded 4/3 (~1e-16), so FR-014's "the two accumulators realign every
    // three cycles of f" is true to double precision and SC-003 (b)'s +/-2 cents (1.2e-3
    // relative) has four orders of margin.
    masterFifth_.increment = masterUnison_.increment * (4.0 / 3.0);
    incUnison_ = static_cast<float>(masterUnison_.increment);
    incFifth_  = static_cast<float>(masterFifth_.increment);
}
```

The clamp ceiling is applied to `f`, and `kMasterNyquistRatio = 0.3` is chosen so the **`4f/3`**
master's increment tops out at `0.4` — inside `PhaseAccumulator::advance`'s single-subtraction wrap
contract (`phase_utils.h:161-168`), which the S1.2 `static_assert` states.

**Tone frequencies are reported FROM the increments, never recomputed from `fundamentalHz_`:**

```cpp
[[nodiscard]] float getToneFrequencyHz(std::size_t tone) const noexcept {
    switch (tone) {
        case 0: return static_cast<float>(masterUnison_.increment * sampleRate_ * 0.5);
        case 1: return static_cast<float>(masterUnison_.increment * sampleRate_ * 0.25);
        case 2: return static_cast<float>(masterFifth_.increment  * sampleRate_ * 0.5);
        default: return 0.0f;
    }
}
```

That is what makes SC-003 (a)'s "`getToneFrequencyHz` agrees with the spectral estimate" a real check
on the rendered path rather than two independent restatements of the same arithmetic. The same helper
feeds the FR-016 backstop test, so the read surface and the gate can never disagree.

---

## S3. `reset()` and `setSeed()`

### S3.1 `reset()` — FR-005, configuration-preserving

```
1. masterUnison_.reset(); masterFifth_.reset();     // zeroes BOTH phases together (FR-014)
2. for each tone: osc.reset(); breath.reset();      // breath.reset() re-seeds from configuredSeed_
3. follower_.reset(); lowpass_.reset(); saturator_.reset(); blocker_.reset();
4. refreshBreath(i) for each tone, from the just-rewound breathers
5. snap every ramp to its steady target:
      levelRamp.snapTo(toneLevelGain(levelDb));
      gate.snapTo(gateSteady(i)); lastGateTarget = gateSteady(i);
      infrasonicFloored = (gateSteady(i) == 0.0f);
      trackGainRamp_.snapTo(1.0f - trackingAmount_);   // the follower reads 0 after reset
      wetGainRamp_.snapTo(wetGain(wetGainDb_));
      cutoffGlide_.snapTo(std::log2(lowpassHz_));
      lowpass_.setCutoff(lowpassHz_); pushedCutoffHz_ = lowpassHz_;
6. controlPhase_ = 0; clampEngagements_ = 0; trackedEnvNorm_ = 0.0f;
   chainActive_ = !allTonesDormant();
```

`saturator_.reset()` re-snaps its own three `OnePoleSmoother`s to `dbToGain(inputGainDb_)` etc.
(`:149-165`), so the drive survives — configuration-preserving, as required. `lowpass_.reset()` and
`blocker_.reset()` keep their coefficients. `allocatedBytes_` is **not** touched (SC-010 asserts it is
unchanged by `reset()`).

### S3.2 `setSeed()` — FR-070, FR-072

```cpp
void setSeed(std::uint32_t seed) noexcept {
    seed_ = seed;                                   // seed 0 is legal (deriveStreamSeed, :102-113)
    for (std::size_t i = 0; i < kNumTones; ++i) {
        tones_[i].breath.setSeed(deriveStreamSeed(seed_, kSaltBreath + i));
        tones_[i].breath.reset();                   // MANDATORY: initState() re-seeds the RNG
        refreshBreath(i);                           // breathValue/breathGain follow immediately
    }
}
```

The `reset()` after `setSeed` is not optional: `BreathingModulator::setSeed` reseeds `rng_` but leaves
`phase_` and the output smoother where they were; `reset()` → `initState()` is what makes the same
seed re-render identically from the top.

---

## S4. The three tones

### S4.1 Mapping — one table, no branching in the render

| index | `Tone` | master | `SubOctave` | rendered frequency | backstop threshold on `f` |
|---|---|---|---|---|---|
| 0 | `Div2` | `masterUnison_` (`f`) | `OneOctave` | `f/2` | floored below `f = 24 Hz` |
| 1 | `Div4` | `masterUnison_` (`f`) | `TwoOctaves` | `f/4` | floored below `f = 48 Hz` |
| 2 | `FifthBelow` | `masterFifth_` (`4f/3`) | `OneOctave` | `(4f/3)/2 = 2f/3` | floored below `f = 18 Hz` |

`Div2` and `Div4` are driven from the **same** accumulator, because `SubOscillator::process` takes the
wrap flag and the increment as *arguments* and keeps its own divider state
(`sub_oscillator.h:221-259`). One `advance()` per master per sample, three `process()` calls.

At the FR-013 default of 55 Hz the three tones are 27.5 / 13.75 / 36.67 Hz and **none** is floored
(`13.75 > kMinToneHz = 12`), which is what makes every "at defaults, all three tones awake" fixture
(SC-013, SC-017, SC-021) reachable at the instrument's own standard drone pitch.

### S4.2 The Sine phase-reset discontinuity, quantified (D-4, SC-004)

`process()` re-zeroes `subPhase_.phase` on every output flip-flop rising edge (`:265-268`) and *then*
advances by `masterInc / octaveFactor` (`:290-292`). Because the master's own wrap carries a
fractional remainder, the accumulated sub phase at the reset instant is not exactly 1.0, so each sub
period opens with a phase discontinuity bounded by `masterInc / octaveFactor` **cycles**.

Worked, at 48 kHz, for the fixtures SC-004 actually renders:

| tone | fundamental | `masterInc` | discontinuity (cycles) | step in `sin(2πφ)` | relative |
|---|---|---|---|---|---|
| `Div2` | 220 Hz | 4.583e-3 | 2.292e-3 | ≤ `2π·2.292e-3` = 1.44e-2 | ≈ −36.8 dB (1.44 %) |
| `Div2` | 110 Hz | 2.292e-3 | 1.146e-3 | 7.20e-3 | ≈ 0.72 % |
| `Div2` | 55 Hz | 1.146e-3 | 5.729e-4 | 3.60e-3 | ≈ 0.36 % |
| `Div4` | 440 Hz | 9.167e-3 | 2.292e-3 | 1.44e-2 | ≈ 1.44 % |
| `FifthBelow` | 220 Hz | 6.111e-3 (fifth master) | 3.056e-3 | 1.92e-2 | ≈ 1.92 % |

The step is a once-per-sub-period impulse, so its energy spreads across the harmonic series rather
than landing on one partial; the measured THD will sit **below** these bounds. This is why SC-004's
2 % ceiling is derived rather than guessed, and why SC-004 (b) asserts the trend is
**monotonically non-increasing as each tone's own fundamental descends** — the discontinuity is
strictly proportional to `masterInc`, i.e. to `f`. If measurement disagrees, FR-071's
stop-and-surface rule applies: the pre-authorised lever is D-4's rejected alternative (a private
`PhaseAccumulator` + `std::sin` for the Sine path), **never** a relaxed ceiling.

---

## S5. The control step

### S5.1 The chunk loop (FR-007) — an absolute residue, not a block-relative grid

```cpp
void processBlockTapped(const float* inL, const float* inR, float* outL, float* outR,
                        float* subTap, std::size_t numSamples) noexcept {
    // GUARD LADDER, in exactly this order (FR-050, Edge Cases):
    //  1. any null channel pointer -> return, writing NOTHING and advancing NOTHING;
    //  2. numSamples == 0          -> return, consuming NO control step (the residue is
    //                                 absolute, so a zero-length call must not move it);
    //  3. !prepared_               -> PASSTHROUGH (in -> out) plus a zeroed subTap, and no
    //                                 state advance. NOTE: this DIFFERS from FeedbackEcology,
    //                                 which fills zeros (:951-955). FR-050 makes passthrough a
    //                                 promise here, not a coincidence of unprepared oscillators.
    if (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) return;
    if (numSamples == 0) return;
    if (!prepared_) {
        // The self-copy MUST be skipped, not relied on: [alg.copy] requires that `result`
        // not lie in `[first, first + n)`, and in-place rendering (outL == inL, SC-012 (b))
        // puts it exactly there. It happens to work today only because MSVC and libstdc++
        // both route trivially-copyable contiguous ranges to memmove — an implementation
        // detail, not a contract, and no sanitizer in this repo's CI flags it. The two
        // reachable preconditions meet whenever a host renders in place before prepare().
        if (outL != inL) std::copy_n(inL, numSamples, outL);
        if (outR != inR) std::copy_n(inR, numSamples, outR);
        if (subTap != nullptr) std::fill_n(subTap, numSamples, 0.0f);
        return;
    }

    std::size_t done = 0;
    while (done < numSamples) {
        if (controlPhase_ == 0) updateControl();
        const std::size_t chunk =
            std::min(numSamples - done, kControlChunkSamples - controlPhase_);
        renderChunk(inL + done, inR + done, outL + done, outR + done,
                    (subTap != nullptr) ? subTap + done : nullptr, chunk);
        controlPhase_ = (controlPhase_ + chunk) % kControlChunkSamples;
        done += chunk;
    }
}

void processBlock(const float* inL, const float* inR, float* outL, float* outR,
                  std::size_t numSamples) noexcept {
    processBlockTapped(inL, inR, outL, outR, nullptr, numSamples);
}
```

**ONE body.** That is what makes FR-063 / SC-012 (c)'s tap-vs-no-tap bit-identity **structural**
rather than a coincidence two parallel bodies would have to maintain. On the passthrough path
in-place rendering is handled by **skipping** the copy (`if (outL != inL)`), never by relying on
`std::copy_n`'s behaviour when source and destination coincide — which is undefined, not benign.

The residue lives in `controlPhase_` across calls, so a `36 + 28` split runs exactly one control step,
as an unsplit 64 does. SC-012 (a) and SC-019 are the criteria that catch a block-relative grid.

### S5.2 `updateControl()` — the order is normative

Called once, at `controlPhase_ == 0`, i.e. exactly every 64 rendered samples.

```
(1) Advance the three breathers by the FULL chunk length:
        tones_[i].breath.processBlock(kControlChunkSamples);
    kControlChunkSamples, NEVER `chunk` — 64 samples always elapse between two updateControl
    calls, whatever the host's partition. Advancing by `chunk` would make the breath rate a
    function of the block size (breathing_modulator.h:208-215 advances phase by numSamples)
    and SC-012 (a) would fail.

(2) refreshBreath(i) for each tone:
        b           = std::clamp(sanitise(breath.getCurrentValue(), 0.0f), -1.0f, 1.0f);
        breathValue = b;                                       // FR-061 reports THIS
        breathGain  = 1.0f + kBreathGainSpan * breathDepth * b; // FR-022, in [0.55, 1.45]
    getCurrentValue() is VIRTUAL (breathing_modulator.h:221, override of ModulationSource):
    three virtual calls per 64 samples, never per sample (FR-024, A-1).

(3) The FR-016 backstop, per tone, EDGE-DETECTED:
        const float steady = gateSteady(i);          // exactly 0.0f or exactly 1.0f
        if (steady != tones_[i].lastGateTarget) {
            tones_[i].gate.setTarget(steady);
            tones_[i].lastGateTarget = steady;
        }
        tones_[i].infrasonicFloored = (steady == 0.0f);
    EDGE-ONLY IS LOAD-BEARING (Q1): LinearRamp::setTarget recomputes the increment from the
    REMAINING distance (smoother.h:342-355), so a ramp retargeted every control step never
    arrives and SC-020 (c)'s "reaches its new target within 52 ms and is monotonic" would be
    unmeasurable. Retargeted only at edges, it lands exactly on the target and stays.

(4) The tracking law (FR-032), with the follower health guard (S7.6):
        float env = follower_.getCurrentValue();
        if (!detail::isFinite(env)) { follower_.reset(); env = 0.0f; }
        trackedEnvNorm_ = std::clamp(env / trackReferenceRms_, 0.0f, 1.0f);
        trackGainRamp_.setTarget((1.0f - trackingAmount_)
                                 + trackingAmount_ * trackedEnvNorm_);
    This ramp IS retargeted every step, deliberately: it is a tracking gain, not a gate, and a
    continuously-retargeted LinearRamp's exponential-approach behaviour is exactly the smoothing
    wanted. No criterion asserts its arrival time.

(5) The low-pass cutoff glide (S5.3).

(6) Dormancy and the sleep edge (S5.4).
```

`gateSteady(i)` is the single owner of the backstop law:

```cpp
[[nodiscard]] float gateSteady(std::size_t tone) const noexcept {
    return (getToneFrequencyHz(tone) < kMinToneHz) ? 0.0f : 1.0f;   // LITERAL 0.0f / 1.0f
}
```

The literal `0.0f` is what makes `infrasonicFloored` and any downstream `== 0.0f` test valid by
construction rather than by luck (the `resonance_drift_network.h:1680` precedent).

### S5.3 The low-pass cutoff glide — a plan-level design addition, and why

FR-040 says only that cutoff writes are "control-surface only, never on the per-sample path". Taken
literally that permits `setLowpassCutoffHz` to call `TwoPoleLP::setCutoff` immediately. **SC-008
forbids it in practice**: it steps "the low-pass cutoff across its full range" (40 ↔ 2000 Hz, a 50×
move) mid-render and requires no click. A direct `Biquad::configure` swaps `b0` from ≈ 1.55e-2 to
≈ 6.8e-6 while `z1_`/`z2_` still hold state sized for the old poles; the first output sample steps by
`(b0_new − b0_old)·x` and the mismatch then relaxes over the new ~4 ms pole time constant. On a
stationary low sine that is exactly the shape `ClickDetector`'s 5-σ derivative test is built to find.

Design:

```cpp
void setLowpassCutoffHz(float hz) noexcept {
    if (!detail::isFinite(hz)) return;                            // FR-009
    lowpassHz_ = std::clamp(hz, kMinLowpassHz, maxLowpassHz_);    // getLowpassCutoffHz reports THIS
    cutoffGlide_.setTarget(std::log2(lowpassHz_));                // control-rate ramp, log2 domain
}
```

and inside `updateControl()` step (5):

```cpp
const float applied = std::exp2(cutoffGlide_.process());   // ONE process() per control step
if (std::abs(applied - pushedCutoffHz_) > kCutoffPushRelative * pushedCutoffHz_) {
    lowpass_.setCutoff(applied);                           // the ONLY caller of setCutoff
    pushedCutoffHz_ = applied;                             // prepare()/reset() seed both
}
```

- **log2 domain**, so a 40 → 2000 Hz move is 5.64 octaves at a constant rate: the glide is
  perceptually even and, more importantly, the per-step coefficient delta is bounded and uniform
  instead of being 50× larger at the top of the range.
- **`kCutoffPushRelative = 1e-3f`** (looser than `FeedbackEcology`'s `1e-4f` because this ramp is
  already control-rate): during a 50 ms glide there are ~37 control steps each moving ~15 %, so the
  push fires every step; when idle the ramp early-outs (`smoother.h:372-374`) and nothing is pushed
  or recomputed. Steady-state cost is one compare per 64 samples.
- Cost: one `std::exp2` and one `LinearRamp::process()` per control step (750/s at 48 kHz), plus one
  `Biquad::configure` per step **only while gliding**. Priced by FR-071 arm (f).
- **`getLowpassCutoffHz()` reports the clamped request, not `applied`.** FR-009 says a getter reports
  the applied *clamp*; the glide is an internal anti-click mechanism, not a second clamp, and a
  second getter for the glide's instantaneous value is surface FR-061's list does not have.

**Rejected alternative, on the record:** step the coefficients directly and add the cutoff to SC-008's
exclusion list beside `setToneWaveform`. Rejected because SC-008 names the cutoff sweep explicitly,
and the spec's exclusions are "declared stepped and rare control events" — a filter cutoff on a synth
is neither.

### S5.4 Dormancy and the sleep edge (FR-025, FR-026, Q5)

```cpp
[[nodiscard]] bool isToneDormant(std::size_t tone) const noexcept {
    if (tone >= kNumTones) return false;
    const LinearRamp& r = tones_[tone].levelRamp;
    return r.getTarget() == 0.0f && r.getCurrentValue() == 0.0f;   // FR-023, exact
}
[[nodiscard]] bool allTonesDormant() const noexcept {
    for (std::size_t i = 0; i < kNumTones; ++i) if (!isToneDormant(i)) return false;
    return true;
}
```

The exact `== 0.0f` is correct **by construction**: `toneLevelGain(kMinToneLevelDb)` returns a literal
`0.0f` (S2.1) and `LinearRamp::process()` lands exactly on its target rather than approaching it
(`smoother.h:379-383`).

`updateControl()` step (6), **before** anything in the chunk is rendered:

```cpp
const bool dormant = allTonesDormant();
if (chainActive_ && dormant) {
    // FR-026, THE SLEEP EDGE, verbatim as the FR names it (the three resets, in this order).
    // WHAT EACH LINE ACTUALLY DOES — checked against the shipped code, because the obvious
    // reading of "clear the chain's audio state" is wrong for one of the three:
    //  * lowpass_.reset() and blocker_.reset() clear real audio tails: the biquad y1_/y2_
    //    (two_pole_lp.h:102, dc_blocker.h:307) that would otherwise replay on the wake.
    //    THESE TWO are what SC-014 (c) measures.
    //  * saturator_.reset() clears NO audio tail reachable from here. processSample
    //    (saturation_processor.h:228-250) touches neither dryBuffer_ nor dcBlocker_, so the
    //    only state its reset() clears that this component can observe is the three
    //    OnePoleSmoothers (:151-153), which hold parameter gains, not signal. It also runs a
    //    std::fill over dryBuffer_ (:159) — maxBlockSamples floats, up to 32 KB — on the
    //    audio thread. Kept because FR-026 names it and the smoother snap is harmless, but
    //    PRICED, not free: see S9 and S12.4.
    lowpass_.reset();
    saturator_.reset();
    blocker_.reset();
    chainActive_ = false;
} else if (!chainActive_ && !dormant) {
    chainActive_ = true;
}
// follower_ is NEVER reset here: it is the sensor and must keep tracking the body while the subs
// sleep (FR-026, last sentence).
```

Evaluating the condition once per control chunk is what makes it partition-invariant, and it is why
"a tone write that ends dormancy takes effect within at most 64 samples" is a contract rather than an
accident. **"For the whole chunk" is satisfied by construction:** dormancy requires the level ramp to
be *parked* at zero (target and current both `0.0f`), and nothing inside a chunk can move it — only a
setter between two `processBlock` calls can, and that is evaluated at the next chunk boundary.

---

## S6. `renderChunk()` — the per-sample law (FR-050, and the whole chain)

```cpp
void renderChunk(const float* inL, const float* inR, float* outL, float* outR,
                 float* tap, std::size_t n) noexcept
{
    for (std::size_t i = 0; i < n; ++i) {
        // (0) READ BOTH INPUTS FIRST. outL may alias inL (SC-012 (b)); every write below
        //     happens after both reads.
        const float xl = inL[i];
        const float xr = inR[i];

        // (1) THE GENERATORS ALWAYS ADVANCE, dormant or not (FR-025's stated deviation).
        //     Freezing a SubOscillator freezes its flip-flops (:243-259) and its sub-phase
        //     accumulator (:290), so a wake would re-enter mid-waveform at a phase unrelated to
        //     the master's; at 12-30 Hz the 50 ms fade is one period or less and cannot mask it.
        //     Cost: 2 adds/compares + 3 std::sin.
        const bool  wu = masterUnison_.advance();
        const bool  wf = masterFifth_.advance();
        const float s0 = tones_[0].osc.process(wu, incUnison_);
        const float s1 = tones_[1].osc.process(wu, incUnison_);
        const float s2 = tones_[2].osc.process(wf, incFifth_);

        // (2) EVERY PER-SAMPLE RAMP ALWAYS ADVANCES (FR-025), so a wake never begins from a
        //     stale gain. LinearRamp::process() early-outs when settled (smoother.h:372-374),
        //     so a parked ramp costs one compare.
        const float g0 = tones_[0].levelRamp.process() * tones_[0].breathGain
                       * tones_[0].gate.process();
        const float g1 = tones_[1].levelRamp.process() * tones_[1].breathGain
                       * tones_[1].gate.process();
        const float g2 = tones_[2].levelRamp.process() * tones_[2].breathGain
                       * tones_[2].gate.process();
        const float tg = trackGainRamp_.process();
        const float wg = wetGainRamp_.process();

        // (3) THE SENSOR ALWAYS ADVANCES (FR-025, FR-026): it must keep tracking the body while
        //     the subs sleep. The isFinite guard is S14 C-3 and is NOT optional — S7.6 has the
        //     exact poisoning trace it closes.
        const float mono = 0.5f * (xl + xr);
        static_cast<void>(follower_.processSample(detail::isFinite(mono) ? mono : 0.0f));

        // (4) FR-025's skip. The chain is the expensive part and it is what gets skipped.
        if (!chainActive_) {
            outL[i] = xl;
            outR[i] = xr;
            if (tap != nullptr) tap[i] = 0.0f;
            continue;
        }

        // (5) THE CHAIN, in the FR-040 order and no other.
        float y = g0 * s0 + g1 * s1 + g2 * s2;   // mono sub sum
        y = lowpass_.process(y);                 // stage 1   (FR-040) — Biquad self-heals
        y = saturator_.processSample(y);         // stage 2   (FR-041) — tanh, mix_ == 1
        y *= tg;                                 // stage 2.5 (FR-032) — AFTER the shaper, so the
                                                 //   drive is level-independent (SC-004)
        y = blocker_.process(y);                 // stage 3   (FR-042) — 18 Hz Bessel HP

        // (6) FR-055 rung 4. The blocker has no finiteness branch and the follower does not
        //     validate; this is the one place both are recovered. Unreachable through the public
        //     API — exercised by the S7.5 probe.
        if (!detail::isFinite(y)) { y = 0.0f; recoverNonFinite(); }

        // (7) FR-062: the tap is post-chain, post-tracking, PRE-wet-gain, PRE-clamp, and is
        //     independent of subToMainEnabled_ (FR-064). Post-tracking is load-bearing — SC-002
        //     measures the FR-032 law through this tap, and a pre-tracking tap would read a flat
        //     line at every body level and pass for a broken implementation.
        if (tap != nullptr) tap[i] = y;

        // (8) FR-054 rung 3: the clamp binds the SUB CONTRIBUTION ONLY (Q6/D-12), never the
        //     summed output. It counts FINITE excursions only — step (6) has already removed the
        //     non-finite case, so the counter stays a pure statement about level.
        float sub = y * wg;
        if (sub > kOutputClamp)       { sub =  kOutputClamp; bumpClampCount(); }
        else if (sub < -kOutputClamp) { sub = -kOutputClamp; bumpClampCount(); }

        // (9) THE ADD (D-7), gated by FR-064. The SAME scalar on both channels — FR-043's
        //     mono-by-construction, which is the whole mechanism behind SC-007.
        const float add = subToMainEnabled_ ? sub : 0.0f;
        outL[i] = xl + add;
        outR[i] = xr + add;
    }
}
```

`bumpClampCount()` saturates rather than wraps:
`if (clampEngagements_ != 0xFFFFFFFFu) ++clampEngagements_;`

`recoverNonFinite()` is the FR-055 recovery and nothing more: `follower_.reset(); lowpass_.reset();
saturator_.reset(); blocker_.reset();`. It does **not** touch the dry path, does **not** move
`clampEngagements_`, and does **not** clear `chainActive_`. Note the cost asymmetry, since this call
sits **inside** the per-sample loop: `saturator_.reset()` carries the same
`std::fill` over `dryBuffer_` as the sleep edge (`saturation_processor.h:159`), i.e. an
O(`maxBlockSamples`) memset per invocation. That is acceptable **only** because rung 4 is
unreachable through the public API (S7.5): the sub chain is generator-driven, so a non-finite *input*
never reaches it, and a shipping audio thread therefore never executes this line. It is priced in S9
and S12.4 rather than assumed free, and it is the reason the probe injects **once** rather than
holding the blocker poisoned across a block.

**The dry path is never touched by anything computed on the sub.** A non-finite input yields a
non-finite output *by contract* (FR-050 is an add), which is exactly what SC-009 (c) is written
against. Sanitising a host's audio behind its back hides the host's bug.

---

## S7. The safety ladder, rung by rung

### S7.1 Rung 1 — structural bounds (FR-052)

Entirely compile-time: the two `constexpr` magnitudes and their `static_assert`s in S1.2. No runtime
code. SC-006 (a) is the measurement that holds the claim to the audio.

### S7.2 Rung 2 — the saturator's `tanh` (FR-053)

`Sigmoid::tanh` → `FastMath::fastTanh`: exactly ±1 beyond ±3.5, and `0.99924` at the domain edge
`x = 3.5` (evaluated this session from the shipped Padé (5,4) coefficients, `fast_math.h:85-88`), so
`|out| ≤ 1` with no overshoot. `outputGain = dbToGain(-driveDb) ≤ 1` for `driveDb ≥ 0`; during a drive
change the two `OnePoleSmoother`s move independently, so the transient bound is
`max(tanh) × max(outputGain) = 1 × 1 = 1`. `kSaturatorOutputBound = 1.0f` is therefore a true upper
bound at every reachable drive, transient included.

### S7.3 Rung 3 — the clamp on the sub contribution only (FR-054, Q6, D-12)

S6 step (8). Because the clamp is sub-only, no dry input at any level can move
`getClampEngagementCount()`, which is exactly what SC-006 (a), SC-009 (c3) and SC-017 (d) read it as.
A dry input above `+12 dBFS` passes through untouched.

### S7.4 Rung 4 — the non-finite trap (FR-055)

S6 step (6). It exists because two composed primitives do not self-heal: `DCBlocker2::process` has no
finiteness branch (`dc_blocker.h:328-341`) and `EnvelopeFollower::processSample` "does NOT validate
input" (`envelope_follower.h:162`); one non-finite sample poisons either for the life of the object,
and `detail::flushDenormal` does not clear a NaN. The two stages *before* the blocker do self-heal
(`Biquad::process` resets and returns `0.0f`; `SubOscillator::sanitize` maps NaN to `0.0f`), which is
why the blocker sits last.

### S7.5 The fault-injection probe (SC-009 (b))

Rung 4 is **unreachable through the public API** — FR-009 rejects non-finite setter arguments,
`SubOscillator::process` sanitises its own output, `Biquad` resets on a non-finite input, and S7.6's
guard keeps the follower clean. It is therefore exercised through a declared, test-only friend, the
shipped `SeraphisEngine` / `FeedbackEcology` pattern:

```cpp
namespace detail {
/// FR-055 fault injection. DECLARED HERE AND NEVER DEFINED BY THE LIBRARY — the only definition
/// lives in dsp/tests/unit/systems/subharmonic_engine_nonfinite_test.cpp, so a shipping build has
/// no way to call it and it adds no public surface. Pattern: systems/feedback_ecology.h:166-172
/// (declaration) and :1525 (friend).
/// ODR: swept this session — zero matches in dsp/, plugins/ or tools/.
struct SubharmonicEngineNonFiniteProbe;
}  // namespace detail
```

The test's definition needs exactly two operations, both reaching private state:
`injectChainNonFinite(SubharmonicEngine&, float nonFinite)` — pushes one bit-pattern non-finite value
directly through `blocker_.process()` so `y1_`/`y2_` are poisoned before the next render — and
`readChainHealth(const SubharmonicEngine&)`, returning the follower's current value and the blocker's
`y1_`. Keep the probe to those two: a probe that can write any member is a second, untested API.

### S7.6 The follower input guard — S14 **C-3**, the one defect path the spec leaves open

**The trace, end to end, all four links verified this session:**

1. `EnvelopeFollower::processSample` does not validate its input (`envelope_follower.h:162`). With a
   NaN sample, `processRMS`'s comparison `squared > squaredEnvelope_` is false, the release branch
   runs, and `squaredEnvelope_ = squared + releaseCoeff_ * (squaredEnvelope_ - squared)` is NaN
   **forever** (`:311-325`). `detail::flushDenormal` returns NaN unchanged (`db_utils.h:245-247`).
2. `updateControl()` then computes `envNorm = clamp(NaN / ref, 0, 1)` = **NaN** — `std::clamp` returns
   `v` when both comparisons are false.
3. `trackGainRamp_.setTarget(NaN)` **does not propagate the NaN. It MUTES**:
   `target_ = current_ = increment_ = 0` instantly, with no counter and no way back
   (`smoother.h:343-348`).
4. The sub is then exactly `0.0f` — **finite** — so FR-055's trap in S6 step (6) never fires, and
   nothing in the component ever recovers. The engine is silently dead for the rest of the session.

Two consequences the spec's own criteria cannot survive without a guard:

- **SC-009 (c2) fails.** It renders a non-finite input window and then requires the final 10 s to
  match a finite reference render within `render_fingerprint.h` tolerances. A permanently muted sub
  does not match.
- **The dormant case is worse:** while `chainActive_` is false the sub is zero anyway, so a
  non-finite input during a dormant stretch poisons the follower with no symptom at all until the
  next wake.

**The fix, two lines, both already placed in S5/S6:**

- S6 step (3): `follower_.processSample(detail::isFinite(mono) ? mono : 0.0f)` — one bit test per
  sample (`detail::isFinite` is a `constexpr` bit-pattern test behind an opaque barrier, so it
  survives `-ffast-math`; `std::isfinite` is forbidden by FR-008 and by
  `tools/lint-nonfinite-symbols.js`).
- S5.2 step (4): reject a non-finite `getCurrentValue()`, reset the follower, and substitute `0.0f`
  **before** the value can reach `setTarget`. Defence in depth against a NaN arriving by any route the
  first guard does not cover, and the same shape as `FeedbackEcology`'s own follower health guard
  (`feedback_ecology.h:1948-1960`, whose comment block documents exactly this
  `LinearRamp`-mutes-silently failure).

**This is not "sanitising the host's audio behind its back."** The dry path is untouched; only the
*sensor's* input is substituted, and the sensor's job is to measure body level, for which a
non-finite sample carries no information. FR-055's stated principle is preserved intact.

Cost: one bit test per sample plus one per control step. Priced by FR-071 arm (e).

### S7.7 FR-009's normative argument contract — one rule, applied everywhere

Every float setter opens with `if (!detail::isFinite(v)) return;` — **rejection**, previous value
stands, getter still reports the old value. Then `std::clamp` to the range, and the getter reports the
clamped value. **Every one of those ranges is a named pair in S1.2, with no exceptions** — including
the three that an earlier draft left unnamed and therefore undefined at this level:
`setFollowerAttackMs` → `[kMinFollowerAttackMs, kMaxFollowerAttackMs]`, `setFollowerReleaseMs` →
`[kMinFollowerReleaseMs, kMaxFollowerReleaseMs]`, `setToneBreathRate` →
`[kMinBreathRateHz, kMaxBreathRateHz]`. Those three then **push** the clamped value into the composed
object (`follower_.setAttackTime` / `setReleaseTime`, `breath.setRate`), which clamps again with the
bounds S1.2 `static_assert`s these against — so the second clamp is idempotent by construction — and
their getters read the composed object back (S8). Pushing without clamping here would leave FR-061's
"the getter reports the applied value" true only by accident of the composed object's own range. Every `std::size_t tone` setter opens with `if (tone >= kNumTones) return;` — silent
no-op. Every getter taking a tone index returns the documented neutral for an out-of-range index.

The rejection is load-bearing, not hygiene: `std::clamp` does **not** reject NaN (with `v = NaN` both
`v < lo` and `hi < v` are false, so `v` is returned unchanged), and a NaN reaching
`masterUnison_.increment` poisons `masterPhaseEstimate_` (`sub_oscillator.h:228`), which
`SubOscillator` never guards.

---

## S8. Read-surface semantics (FR-061)

| Getter | Reports | Note |
|---|---|---|
| `getFundamentalHz` | the clamped `fundamentalHz_` | not the request |
| `getToneFrequencyHz(t)` | derived **from the accumulator increments** (S2.3) | so the read surface and the rendered pitch cannot disagree |
| `getToneLevelDb(t)` | the clamped `levelDb` | `kMinToneLevelDb` reads back as `-60`, and the *gain* is exactly 0 |
| `getToneBreathValue(t)` | the raw `b_i` in `{-1,+1}` | **not** depth-scaled (Q7): the modulator sits at its library depth of 1.0 and the engine's depth lives only in `breathGain`. SC-011 (b) and SC-014 (d) assert on this because `getToneCurrentGain` is a composite that cannot separate a breath difference from a level or gate difference |
| `getToneBreathRate(t)` | **forwards** to `tones_[t].breath.getRate()` (`breathing_modulator.h:188`) | the rate is stored in exactly one place, the object that clamps it. A stored-but-never-pushed rate is not representable, and FR-061's "reports the applied (clamped) value" is satisfied by construction |
| `getFollowerAttackMs` / `getFollowerReleaseMs` | **forward** to `follower_.getAttackTime()` / `getReleaseTime()` (`envelope_follower.h:260`, `:265`) | same reason, and it is the only cheap detector of an omitted FR-031 push: the shipped defaults are 10 / 100 ms, so a build that stores the engine's 120 / 800 and never pushes reports 10 / 100 here and fails `SubharmonicEngine_ControlSurfaceContract`. An *audio* bracket on the attack is not constructible — see SC-001 (b) and S14 C-10 |
| `getToneCurrentGain(t)` | `levelRamp.getCurrentValue() * breathGain * gate.getCurrentValue()` | the FR-022 product, exactly `noise_organism.h:932-938`'s form |
| `isToneDormant(t)` | `levelRamp` target **and** current both exactly `0.0f` | the FR-020 fader bottom, **not** the FR-016 backstop |
| `isToneInfrasonicFloored(t)` | `gateSteady(t) == 0.0f` as latched at the last control step | a backstop-gated tone is silenced via `gate` but is **not** "dormant" |
| `getTrackedEnvelope` | the last computed `envNorm`, clamped `[0,1]` | |
| `getTrackingGain` | `trackGainRamp_.getCurrentValue()` | SC-002 (d) corroborates the audio against these two |
| `getLowpassCutoffHz` | the clamped **request** | the S5.3 glide is internal; no getter exposes the applied value |
| `getClampEngagementCount` | saturating `std::uint32_t`, cleared by `prepare()` and `reset()` | finite excursions only |
| `getAllocatedBytes` | the computed ledger of S9 | self-reported: `AllocationDetector` has no byte accounting |

---

## S9. Allocation ledger (FR-073) — the assertion surface for SC-010

`prepare()` is the only allocating method. The **resident** heap term, computed and stored in
`allocatedBytes_`:

| Term | Count | Bytes |
|---|---|---|
| `MinBlepTable::table_` — `length_ * oversamplingFactor` floats (`minblep_table.h:216-217`), `16 × 64` | 1 | 4096 |
| `MinBlepTable::blampTable_` — same size (`:243`) | 1 | 4096 |
| `MinBlepTable::Residual::buffer_` — `table.length()` = 16 floats (`:405`) | 3 | 192 |
| `SaturationProcessor::dryBuffer_` — `maxBlockSamples` floats (`:136`) | 1 | `4 × maxBlockSamples` |
| `EnvelopeFollower`, `TwoPoleLP`, `DCBlocker2`, `BreathingModulator`, `LinearRamp`, `PhaseAccumulator`, `SubOscillator` beyond its residual | — | **0** |

`allocatedBytes_ = 8384 + 4 * config_.maxBlockSamples`. At the `PrepareConfig` default of 2048 that is
**16 576 B**; at the SC-013 / spectral fixture (`maxBlockSamples = 512`) it is **10 432 B**.

`SaturationProcessor::dryBuffer_` is **dead weight this component never reads** — declared here rather
than hidden, because the alternative (preparing a shipped component with a false block size) is a
trap for any later phase that reaches for its block entry point.

**It is not, however, free weight.** `SaturationProcessor::reset()` ends with a `std::fill` over the
whole buffer (`saturation_processor.h:159`), so every call costs an O(`maxBlockSamples`) memset — up
to 8192 floats / 32 KB at the `PrepareConfig` ceiling. This component calls `reset()` on it from
three places, and only one of them is off the audio thread:

| Call site | Frequency | Cost |
|---|---|---|
| `prepare()` / `reset()` | control thread | irrelevant |
| the FR-026 sleep edge (S5.4) | once per dormancy **edge**, never per chunk (`chainActive_` is an edge latch) | one memset per edge; a patch that toggled dormancy at audio rate would be a different defect, caught by SC-008 |
| `recoverNonFinite()` (S6 step 6) | unreachable through the public API (S7.5); once per injected fault in the probe TU | one memset per injection |

Neither audio-thread site is an allocation, so SC-010 is unaffected; both are recorded here so the
FR-071 accounting is honest about what the dormancy edge costs (S12.4).

`MinBlepTable::prepare` also allocates and frees several **transient** vectors (the windowed sinc, the
window, the cepstral FFT scratch). They are not resident and do not enter the ledger, but they are
allocations and they occur strictly inside `prepare()`, which is all SC-010 requires.

`getAllocatedBytes()` is identical across two `prepare()` calls with the same `PrepareConfig` (it is
computed from `config_`, not measured) and is untouched by `reset()`.

---

## S10. Test plan

### S10.1 The one new test-helper header (A-4)

**`tests/test_helpers/low_frequency_metrics.h`**, namespace `Krate::DSP::TestUtils`, header-only.
One new file, **no CMake edit** (`test_helpers` is an INTERFACE library with no source list), and
**no edit to any shipped helper** — `spectral_analysis.h`, `signal_metrics.h` and
`buffer_comparison.h` are consumed by dozens of TUs and adding to them buys nothing here.

```cpp
namespace Krate::DSP::TestUtils {

/// The frame every low-frequency spectral measurement in Phase 6 uses: 262 144 points at 48 kHz
/// = 0.1831 Hz bins, 5.46 s of signal. FFT::prepare only requires a power of two (fft.h:150-155)
/// — its doc comment's "[256, 8192]" describes the sizes the original spec exercised, not a limit
/// in the code. Every consumer REQUIREs fft.isPrepared() (fft.h:255) before trusting a result.
inline constexpr std::size_t kLowFrequencyFftSize = 262144;

/// SC-003. Hann-windowed magnitude spectrum, peak bin located, then PARABOLIC INTERPOLATION of
/// the LOG magnitude across the peak bin and its two neighbours — the standard estimator for a
/// Hann-windowed sinusoid, residual bias < 0.01 bin (~0.002 Hz) on a stationary tone. Returns a
/// negative sentinel if the analysed frame's RMS is below -60 dBFS.
[[nodiscard]] float estimatePeakFrequencyHz(const float* x, std::size_t n, float sampleRate);

/// SC-004. THD over the same 262 144-point Hann frame: power summed over PEAK BIN +/- 2 (the Hann
/// main lobe) at the fundamental and at each harmonic k = 2..maxHarmonic,
/// THD = sqrt(sum_k P_k) / sqrt(P_1) * 100. At 27.5 Hz the harmonic spacing is 150 bins, so the
/// main lobes never touch and the Hann sidelobe envelope at that distance is below -100 dB
/// (0.001 %) — a leakage floor 60 dB under SC-004's 2 % ceiling, which is exactly what
/// signal_metrics.h:111 calculateTHD cannot provide (8192-point cap, overlapping +/-2-bin windows
/// below ~200 Hz, and a silent 0.0f return on an empty fundamental).
/// Returns a NEGATIVE SENTINEL when the fundamental's summed power is below -60 dBFS, so a silent
/// tone fails rather than reporting 0 %.
[[nodiscard]] float measureLowFrequencyThdPercent(const float* x, std::size_t n,
                                                  float fundamentalHz, float sampleRate,
                                                  int maxHarmonic = 10);

/// SC-005. Peak PICKING, which the criterion needs and neither the spec nor spectral_analysis.h
/// defines. Without a stated rule SC-005 (a) is not implementable: a plain local-maxima scan
/// fails on a CORRECT implementation, because the Hann window's own first sidelobe sits ~2.4
/// bins from the main lobe at -31.5 dB — above SC-005's -40 dB threshold and outside its +/-1
/// bin tolerance — so the 55 Hz fundamental's own leakage would be reported as inharmonic
/// content; and a global-maximum-only scan cannot detect inharmonic content at all.
///
/// THE RULE: over the Hann magnitude spectrum, take every bin that is >= threshold relative to
/// the global maximum, visit the candidates in DESCENDING magnitude order, accept a candidate
/// only if no already-accepted peak lies within +/-kPeakExclusionBins of it. Four bins is wider
/// than the Hann main lobe (+/-2 bins) plus its first sidelobe (~2.4 bins), and the second
/// sidelobe is already at -41.5 dB, below the -40 dB threshold SC-005 scans with. Real content
/// is never merged: 55 Hz harmonics are 300 bins apart in this frame.
inline constexpr std::size_t kPeakExclusionBins = 4;
[[nodiscard]] std::vector<std::size_t> findSpectralPeaks(const float* x, std::size_t n,
                                                         float relativeThresholdDb);

/// SC-006. 4x-oversampled true peak in dBTP, on the same basis TruePeakLimiter::processChunk uses
/// (true_peak_limiter.h:121-146): Oversampler<4,1> per channel, with the raw sample folded into
/// the max so the returned figure is never below the sample peak.
[[nodiscard]] float measureTruePeakDb(const float* l, const float* r, std::size_t n,
                                      double sampleRate);

/// SC-007. Pointer/length zero-lag normalised correlation. The shipped form
/// (buffer_comparison.h:200, namespace TestHelpers) is template<size_t N> over std::array and is
/// unusable on a multi-minute heap render. Different namespace, so no overload interaction.
[[nodiscard]] float calculateCorrelation(const float* a, const float* b, std::size_t n);

}  // namespace Krate::DSP::TestUtils
```

**Each helper is validated against a synthetic signal of known truth in the same TU that consumes
it**, so a helper bug fails as a helper bug:

- `estimatePeakFrequencyHz` within 0.005 Hz of synthetic 27.5 / 36.67 / 110 Hz sines (SC-003 (c));
- `measureLowFrequencyThdPercent` ≤ 0.01 % on a pure sine, and within 1 % *relative* of a
  synthesised third-harmonic reference at a known 5 %;
- `findSpectralPeaks` on a synthetic 55 Hz sine over the same 262 144-point Hann frame returns
  **exactly one** peak, within ±1 bin of 55 Hz, at a −40 dB threshold (the rule's whole purpose: the
  window's own sidelobes must not be reported), and on a synthetic 55 Hz + 82.5 Hz pair returns
  **exactly two**. This validation runs **before** the engine measurement in the same TU, so a
  peak-picker bug fails as a peak-picker bug and not as an inharmonic-divider claim;
- `measureTruePeakDb` within 0.1 dB of 0 dBTP on a full-scale sine, and ≥ the sample peak always;
- `calculateCorrelation` returning 1.0 / −1.0 / ~0.0 on identical, inverted and orthogonal pairs.

`measureTruePeakDb` takes `sampleRate` (the shipped `Oversampler::prepare` needs it) — a deviation
from spec A-4's three-argument sketch, recorded in S14 C-7.

**Memory note for the implementer:** one 262 144-point frame is a 1 MB input buffer plus 131 073
`Complex` bins (≈ 1 MB) plus the `FFT`'s three internal aligned buffers (3 MB). Prepare **one** `FFT`
per TU inside a function-local `static` (or a fixture member) rather than per call site — five
criteria use the same size.

### S10.2 TU assignment (FR-081)

| TU | Criteria | Flags |
|---|---|---|
| `dsp/tests/unit/systems/subharmonic_engine_test.cpp` | SC-001, SC-006, SC-007, SC-008, SC-010, SC-011, SC-012, SC-014, SC-019, SC-020, SC-021, SC-022, plus `SubharmonicEngine_ControlSurfaceContract` (the FR-009 argument contract, the FR-003 default-constructed contract and the FR-050 guard ladder), `SubharmonicEngine_ClampScope` (the Q6/D-12 clamp-scope edge case) and the FR-052/FR-073 ledger case | default |
| `dsp/tests/unit/systems/subharmonic_engine_spectral_test.cpp` | SC-002, SC-003, SC-004, SC-005, SC-017 (A)+(B) | SC-017 tagged `[long]` |
| `dsp/tests/unit/systems/subharmonic_engine_perf_test.cpp` | SC-013 + the FR-071 stage/placement probe | `[.perf]`, **out** of the `-fno-fast-math` block |
| `dsp/tests/unit/systems/subharmonic_engine_nonfinite_test.cpp` | SC-009 only; the sole definition of `detail::SubharmonicEngineNonFiniteProbe` | **in** the `-fno-fast-math` block |

The other three TUs stay **out** of the `-fno-fast-math` block deliberately, so FR-008/FR-009's guards
are also proved in the `/fp:fast` + `-ffast-math` mode the header actually ships in
(the `resonance_drift_network_test.cpp:38-42` precedent).

### S10.3 The isolated-sub fixture, coded once and shared

The spec states it once; the TU should too — a single `struct IsolatedSub` helper in an anonymous
namespace of `subharmonic_engine_spectral_test.cpp`:

```
48 kHz; PrepareConfig{.maxBlockSamples = 512};
setTrackingAmount(0.0f)          -> trackGain == 1 exactly, no body needed
setWetGainDb(0.0f); setLowpassCutoffHz(kMaxLowpassHz); setDriveDb(kMinDriveDb);
exactly ONE tone at setToneLevelDb(t, -20.0f), the other two at kMinToneLevelDb (exact 0 gain);
setToneBreathDepth(t, 0.0f) on the enabled tone;
silent stereo input; measure subTap from processBlockTapped; discard the first 2 s.
GATE (see the scoping note below): the analysed subTap RMS is above -60 dBFS.
```

**The gate is scoped to the stationary single-tone criteria, and SC-002 opts out.** As written in the
spec (`spec.md:829-831`) the gate reads as unconditional, and applied that way it would fail SC-002
on a **correct** implementation. The arithmetic, from this plan's own numbers: at the FR-020 defaults
the settled base sub RMS is ≈ **−22.5 dBFS** (the SC-021 derivation, tone gains 0.1259/0.0631/0.0316
through the 18 Hz Bessel HP factors 0.788/0.42/0.878, powers summed). SC-002's lowest sweep point is
a **−60 dBFS** body, at which `envNorm = 0.001 / 0.12589 = 0.00794` = **−42.0 dB**, so the tap reads
−22.5 − 42.0 = **−64.5 dBFS** — below the gate, and the case would fail before measuring anything.
The −48 dBFS body (−52.5 dBFS tap) is the first point that clears it. Suppressing the low end of the
sweep to satisfy the gate is not an option: those points are the unity-slope region SC-002 (a) exists
to measure.

So:

- **Gate applies, unchanged, to SC-003, SC-004, SC-005 and SC-020 (b)** — the stationary single-tone
  criteria, where a below-gate reading means the tone is silent and the figure is noise. This is R-1's
  general backstop and it keeps its teeth.
- **SC-002 replaces it with an anchor gate**, because there the tap level is the quantity under test
  and a fixed floor is a statement about the sweep, not about aliveness: (i) at the **top** of the
  sweep (a −6 dBFS body, where `envNorm` is clamped to 1) the tap RMS must be above **−40 dBFS** —
  that is the "the engine is alive" assertion, with ~17 dB of margin against the −22.5 dBFS
  prediction; and (ii) every lower step must read above **−75 dBFS**, an absolute sanity floor ~10 dB
  under the lowest predicted point (−64.5 dBFS), which catches a dead engine without constraining the
  law. Both are recorded in the case, and every measured point is transcribed into compliance.

`-20 dBFS` is the tone level for a reason: FR-041's `tanh` is in circuit even at drive 0 and its own
relative third harmonic is ≈ `A²/24` = 0.042 % at `A = 0.1`, two orders below SC-004's 2 % ceiling.

**Note for the implementer:** the two "silent" tones are silent because `toneLevelGain(-60) == 0.0f`
exactly (S2.1) — but their *oscillators* still run (FR-025's deviation), so the engine is **not**
dormant in this fixture and the chain is live. That is intended: the fixture measures the chain.

### S10.4 Criterion by criterion

| Criterion | TU / `TEST_CASE` | Assertion strategy, tolerance, seed |
|---|---|---|
| **SC-001 (a)** no free-running boom | `..._test.cpp` / `SubharmonicEngine_TrackingSuppressesFreeRunning` | tracking 1.0 (default), all three levels at `kMaxToneLevelDb`, wet `kMaxWetGainDb`, `f = 55`, **silent** stereo input, 60 s @ 48 kHz. Peak `\|out\|` ≤ `dbToGain(-80)`. Mechanically exact: `envNorm = 0` → the tracking ramp lands on exactly `0.0f`. |
| **SC-001 (b)** wake responsiveness | same case | 10 s silent, then a step to a −12 dBFS 55 Hz body. Steady state = sub-band (`< 200 Hz`) RMS of `out − in` over the last 1 s of a 10 s post-step render; assert the same quantity over `[step, step+400 ms]` ≥ 50 % of it, and no sample exceeds `kOutputClamp`. **Corrected prediction (S14 C-10):** the follower's ms figure is a JUCE-style ~99 %-settling time, `coeff = exp(-2π/(ms·fs))` (`envelope_follower.h:356-365`), so `kDefaultFollowerAttackMs = 120` is a time constant of **19.1 ms**, not 120 ms; composed with the FR-032 ramp (a `LinearRamp` retargeted every chunk behaves as a one-pole with τ = `kGainRampMs` = 50 ms) the prediction at 400 ms is ≈ **100 %**, not the ≈ 96 % an earlier draft of this row claimed. The floor stands at 50 %; what it tests is the tracking path. |
| **SC-001 (c)** release | same case | body removed → sub-band RMS of `out − in` < −80 dBFS within 5 s. |
| **SC-001 (c2)** the FR-031 release constant is actually pushed *(plan addition)* | same case | The spec's (b) and (c) are a floor and a ceiling that the **un-pushed** follower defaults (`kDefaultAttackMs = 10`, `kDefaultReleaseMs = 100`, `envelope_follower.h:92-93`) satisfy *more* easily than the FR-031 values, so neither can detect a build that stores 120/800 and never calls `setAttackTime`/`setReleaseTime`. This subclause brackets the release from **below**: with the −12 dBFS body removed, the sub-band RMS of `out − in` over `[400 ms, 600 ms]` after the removal must stay **above −20 dB relative to the pre-removal steady state**. **Arithmetic:** the squared-domain release τ is `800/2π` = **127.3 ms**, so the envelope's amplitude τ is 254.6 ms; `envNorm` is clamped at 1.0 until the envelope falls 6 dB (176 ms), then decays, giving ≈ **−11 dB** at 500 ms — 9 dB of margin. With the un-pushed 100 ms release (amplitude τ = 31.8 ms) the same window reads **below −100 dB**. The attack constant has **no** constructible audio bracket (S14 C-10: at 100 ms the correct and un-pushed configurations differ by ~5 % because the 50 ms FR-032 ramp dominates), so it is bracketed on the read surface instead, through S8's forwarding getters. |
| **SC-002 (a)–(e)** the tracking law | `..._spectral_test.cpp` / `SubharmonicEngine_TrackingLaw` | isolated fixture **except** (and **including the gate**, which SC-002 replaces with S10.3's two-part anchor gate — the unconditional −60 dBFS floor would fail this case at the −60 dBFS body on a correct implementation, predicted tap −64.5 dBFS): tracking 1.0 (the quantity under test), all three tones at their FR-020 defaults, breath depths 0, `f = 55`, input a mono-identical 55 Hz sine held 8 s per step with the last 4 s measured. Sweep body RMS −60/−48/−36/−24/−18/−12/−6 dBFS. Measure `subTap` RMS. (a) unity slope in dB, ±1.0 dB, across −60…−18; (b) flat ±0.5 dB across −18…−6; (c) with tracking 0, flat ±0.5 dB across the whole sweep; (d) `getTrackingGain()` agrees with `(1−a) + a·getTrackedEnvelope()` within `1e-4`; (e) with `setTrackReferenceDb(-30)` the knee moves to −30 dBFS ±1.0 dB (the −24 point becomes rising, the −18 point becomes flat). **Fixture note:** the follower is RMS-mode, so a sine of peak `A` reads `A/√2` — the test computes the body's actual RMS from the rendered buffer rather than assuming a dBFS convention. |
| **SC-003 (a)–(c)** divider frequencies | `..._spectral_test.cpp` / `SubharmonicEngine_DividerFrequencyAccuracy` | isolated fixture, `Sine`, one tone at a time, the spec's per-tone sweeps (Div2 55/110/220; Div4 110/220/440; Fifth 55/110/220 Hz). `estimatePeakFrequencyHz` over 262 144 samples. (a) within ±0.5 % of target **and** `getToneFrequencyHz` within the same tolerance of the estimate; (b) `FifthBelow` within **±2 cents** of `f·2/3` at every swept fundamental — the criterion that proves FR-012's `4f/3`-master construction rather than "some low tone appeared"; (c) the helper validated on synthetic 27.5 / 36.67 / 110 Hz sines to 0.005 Hz **first**, in the same TU. |
| **SC-004 (a)–(b)** divider THD | `..._spectral_test.cpp` / `SubharmonicEngine_DividerTHD` | same fixture and sweeps. `measureLowFrequencyThdPercent(..., maxHarmonic = 10)`. (a) ≤ **2.0 %** for every tone at every fundamental, every value transcribed into compliance; (b) monotonically non-increasing within **each tone's own** descending sweep (220→110→55 for Div2/Fifth; 440→220→110 for Div4), never across tones. S4.2 is the derivation the ceiling comes from. A negative sentinel FAILS the case. |
| **SC-005 (a)–(c)** Square spectrum | `..._spectral_test.cpp` / `SubharmonicEngine_SquareSpectrum` | isolated fixture, **`Div2` alone**, `Square`, `f = 110` (sub = 55 Hz), drive `kMinDriveDb`, LP `kMaxLowpassHz`, 262 144-point Hann. (a) every peak returned by `findSpectralPeaks(..., -40.0f)` lies within ±1 bin of an integer multiple of 55 Hz. **The peak-selection rule is part of the criterion, not an implementation detail** (S10.1): descending-magnitude greedy acceptance with a `kPeakExclusionBins = 4` exclusion window. Without it (a) is not implementable — a plain local-maxima scan reports the Hann window's own first sidelobe (−31.5 dB, ~2.4 bins off, i.e. above the threshold and outside the tolerance) and **fails on a correct implementation**, while a global-maximum scan cannot fail at all. The plan's leakage analysis at 150-bin harmonic spacing (S10.1's THD note) speaks to a different distance and does not cover these near sidelobes. The rule is validated on a synthetic 55 Hz sine (exactly one peak) in the same TU first; (b) Square-path `subTap` RMS **above −40 dBFS** — the assertion that proves the shared `MinBlepTable` is prepared, because an unprepared table makes the tone *silent* rather than un-BLEPped. Predicted RMS at the fixture's `-20 dB` level ≈ −20.5 dBFS, 20 dB of margin. (c) the `tanh`'s own odd harmonics of a single tone are integer multiples and are admitted by (a) by construction; noted in the case, not attributed to the divider. |
| **SC-006 (a)–(c)** true-peak headroom | `..._test.cpp` / `SubharmonicEngine_TruePeakHeadroom` | −6 dBFS 55 Hz body + −12 dBFS pink bed (`primitives/pink_noise_filter.h` over a seeded `Xorshift32`, the `resonance_drift_network_perf_test.cpp` source), `f = 55`, all levels `kMaxToneLevelDb`, wet `kMaxWetGainDb`, drive `kMaxDriveDb`, tracking default. (a) `getClampEngagementCount() == 0` **and** `measureTruePeakDb(out) ≤ +9.5 dBTP`, value transcribed; (b) through a default `TruePeakLimiter` (ceiling −1 dB) → ≤ −0.9 dBTP; (c) at **default** tone levels and wet gain the limiter's minimum gain over the render stays above −6 dB. **How (c) is measured — `TruePeakLimiter` exposes no gain read surface** (its public API is `prepare`/`reset`/`setCeilingDb`/`setReleaseMs`/`getCeilingLinear`/`processBlock`; `currentGain_` is private, `true_peak_limiter.h:177`) **and no getter may be added to it** — `true_peak_limiter.h` is not in FR-080's untouched list only because this phase does not touch it at all, and SC-016's `git diff --stat` is over the ten headers this phase composes. Derive the gain instead: keep an **unlimited copy** of the render, run the limiter on the copy, and compute the per-sample gain as `limited[i] / unlimited[i]` wherever `\|unlimited[i]\| > 1e-6`. The quotient is exact, not an estimate — the limiter's last act is `left[i] = inL * currentGain_; right[i] = inR * currentGain_` (`true_peak_limiter.h:159-160`), one **linked** gain for both channels — so `min(gain) > dbToGain(-6)` is asserted directly, with the measured minimum transcribed. **Arithmetic done in this plan:** at drive 12 dB the saturator's output gain is `dbToGain(-12) = 0.251` and `\|tanh\| ≤ 1`, so the sub peak is ≤ `0.251 × dbToGain(+6)` = 0.501; with the ≈ 0.65 input peak the total is ≈ +1.2 dBFS. The criterion has ~8 dB of margin and can only fail on a real ladder regression. |
| **SC-007 (a)–(c)** mono compatibility | `..._test.cpp` / `SubharmonicEngine_MonoCompatibility` | decorrelated stereo input (independent seeded pink per channel + a common 55 Hz body), default tone levels. `d = out − in` per channel. (a) side energy of `d` ≤ −100 dB relative to its mid energy, `mid = 0.5(dL+dR)`, `side = 0.5(dL−dR)`; (a2) full-output L/R correlation with subs active ≥ the same render with `setWetGainDb(kMinWetGainDb)`, within `1e-6`; (b) `0.5(dL+dR)` retains ≥ 99 % of `d`'s `< 200 Hz` energy relative to the mean of `dL`, `dR` (`spectral_analysis.h:207 detail::sumBinPower`); (c) each output channel's DC over a 30 s render ≤ `1e-4` in magnitude. Structurally, (a) can only fail through an accidental per-channel chain state, a per-channel gain ramp, or a one-sided clamp — which is exactly what it is for. |
| **SC-008** click freedom | `..._test.cpp` / `SubharmonicEngine_ClickFreedom` | 60 s, steady −12 dBFS 55 Hz body, `ClickDetectorConfig{.sampleRate = 48000.0f, …}` (**the default is 44100 and must be overridden or the frame timing is wrong**). Step, one at a time: every tone level `kMin ↔ kMax`; wet gain over the same range; tracking 0 ↔ 1; **the low-pass cutoff over its full range** (the arm S5.3's glide exists for); the fundamental `50 ↔ 44 Hz` (Div4 12.5 → 11 Hz, crossing `f = 48`) and `26 ↔ 20 Hz` (Div2 13 → 10 Hz, crossing `f = 24`). Zero detections. Each floor arm additionally asserts `isToneInfrasonicFloored` **flipped in both directions** — without that a future change to `kMinToneHz` silently re-voids the arm. Excluded by name: `setToneWaveform`, `prepare()`. |
| **SC-009 (a)–(c)** non-finite | `..._nonfinite_test.cpp` / `SubharmonicEngine_NonFinite` | `-fno-fast-math` TU. Non-finite values built from bit patterns `0x7FC00000`, `0x7F800000`, `0xFF800000` through a `volatile` sink (the `resonance_drift_network_nonfinite_test.cpp:149-155` idiom) — **never** `std::numeric_limits`. (a) every float setter rejects NaN and ±Inf with the previous value standing, verified through the getter; (b) the S7.5 probe injects into the blocker, the output is finite within one sample, and the engine renders correct audio thereafter; (c) a 30 s adversarial parameter sweep with a non-finite input buffer for the middle 10 s: (c1) `subTap` finite at **every** sample of the whole render; (c2) main output finite from the first finite input sample, and the final 10 s matches a finite-input reference render within `render_fingerprint.h` tolerances; (c3) `getClampEngagementCount() == 0`. **(c2) is the criterion S7.6's guard exists to make reachable** — without it the follower is poisoned, `trackGainRamp_` mutes, and the reference comparison fails by a wide margin. A finite *output* during the non-finite window is deliberately **not** asserted (FR-050 is an add). |
| **SC-010** zero allocation | `..._test.cpp` / `SubharmonicEngine_NoAllocation` | inside an `AllocationScope`: 10 000 `processBlock` calls of mixed sizes, every setter exercised, `reset()`, and `processBlockTapped` → `getAllocationCount() == 0`. `getAllocatedBytes() > 0`, identical across two `prepare()` calls with the same config, unchanged by `reset()`, and equal to S9's formula `8384 + 4·maxBlockSamples`. |
| **SC-011 (a)–(b)** determinism | `..._test.cpp` / `SubharmonicEngine_Determinism` | two instances, same `PrepareConfig`, same seed, same setter sequence, 120 s → `compareFingerprints` within `kSampleTolerance` / `kMetricTolerance`. (b) different seeds → the comparison **fails**, and at `t = 60 s` at least two of the three `getToneBreathValue` differ by > 0.01 in absolute value. **Only reachable because of S14 C-5** (`kDefaultBreathIrregularity = 0.25`): at the modulator's shipped irregularity of 0 the RNG is never drawn and two seeds render *identically*. Divergence budget at 60 s: tone 0 (27 s period) has ≈ 2.2 cycle wraps and tone 1 (43 s) ≈ 1.4, each drawing a fresh ±12.5 % period jitter at every wrap, so two tones qualify; tone 2 (71 s) may not have wrapped, which is precisely why the criterion says "two of the three". If the arm misses, the pre-authorised lever is to raise `kDefaultBreathIrregularity` — **never** to lower 0.01 or extend the 60 s horizon. No bit-exact goldens anywhere. |
| **SC-012 (a)–(c)** partition / aliasing | `..._test.cpp` / `SubharmonicEngine_BlockInvariance` | (a) 30 s in 512-sample blocks vs a seeded pseudo-random partition of chunk sizes in `[1, 1024]`, fingerprints within tolerance; (b) in-place (`outL == inL`, `outR == inR`) vs out-of-place, same; (c) `processBlockTapped`'s main output **bit-identical** (`==`) to `processBlock`'s — structural, because S5.1 has one body. Also covers the Edge Cases: `numSamples = 1` × 100 000 and a single `numSamples = 8192` call. |
| **SC-013 (a)–(d)** CPU | `..._perf_test.cpp` / `SubharmonicEngine_CpuBudget`, `[.perf]` | see S12. |
| **SC-014 (a)–(d)** dormancy | `..._test.cpp` / `SubharmonicEngine_Dormancy` | (a) all tones at `kMinToneLevelDb`, ramps settled → output **bit-identical** to a decorrelated stereo input over 10 s on both channels; plus the asymmetric arm (wet gain at `kMinWetGainDb` with tones *awake* → also bit-identical, chain still running). (b) `isToneDormant` true for all three, and false on the first sample after a level write above the floor, within the ≤ 64-sample control latency Q5 grants. (c) the four-step sequence, **including the ≥ 2 s charging render** and `trackingAmount = 0` throughout: output ≤ −80 dBFS over the first 500 ms after the wake. (c2) mutation check, run once during implementation and recorded in compliance: with the **two** sleep-edge lines `lowpass_.reset()` and `blocker_.reset()` removed, (c) must **fail**. It is deliberately a *two*-line mutation, not three: `saturator_.reset()` clears no audio state reachable from `processSample` (S5.4), so including it in the mutation would over-claim what the check proves. (c3) **the sensor is NOT reset at the sleep edge** — FR-026's last sentence, and the natural mistake, since the other three chain stages are. With a **−30 dBFS** steady 55 Hz body held throughout (below the −18 dBFS reference, so `envNorm` ≈ 0.251 and is **unclamped** — at the −12 dBFS body of the other arms the clamp at 1.0 would hide a reset within ~35 ms and the check would be vacuous), drive the tones to `kMinToneLevelDb` and then render 500 ms, sampling `getTrackedEnvelope()` once per 64 samples. After the 50 ms level ramp has parked, **every** sample must stay within `1e-3` of the pre-dormancy value. A `follower_.reset()` added to the sleep edge puts a dip to 0 in that trace (and, in a real session, a 120 ms sub fade-in on every wake); nothing else in the component can. (d) two seeded instances, A dormant for **37 s**: (d1) `getToneBreathValue` agree within `1e-3`; (d2) A's own value moved > 0.05 across the dormancy on ≥ 2 tones (at 37 s the 27 s / 43 s / 71 s breathers have moved 1.37 / 0.86 / 0.52 of a cycle, so all three should qualify); (d3) a 5 s post-wake render with all tones restored agrees within fingerprint tolerances. |
| **SC-015** lints + portability | not a `TEST_CASE` | `node tools/lint-layers.js`, `lint-odr.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `lint-simd-aligned-loadstore.js`, `node tools/check-portability.js`. Evidence transcribed into compliance. |
| **SC-016** shared components green + unchanged | not a `TEST_CASE` | `git diff --stat` over the ten FR-080 headers is **empty**; `dsp_core_tests`, `dsp_primitives_tests`, `dsp_processors_tests` (in particular `sub_oscillator_test.cpp`) and the `seraphis_*` cases inside `dsp_systems_tests` all pass. |
| **SC-017 (A)+(B)** long-render stationarity | `..._spectral_test.cpp` / `SubharmonicEngine_LongRenderStationarity`, `[long]` | two 10-minute arms at 48 kHz. **(A)** all defaults + a steady −12 dBFS 55 Hz body. **(B)** tracking 0, all levels `kMaxToneLevelDb`, drive `kMaxDriveDb`, wet `kMaxWetGainDb`, all three breath depths 1.0, LP `kMaxLowpassHz`, same body. Both: (a) per-minute sub-band (`< 200 Hz`) RMS varies ≤ 3 dB peak-to-peak; (b) linear trend of that RMS ≤ 0.3 dB/minute in magnitude; (c) no non-finite sample; (d) clamp count `== 0` on (A), **transcribed** on (B). **Memory note:** render in 512-sample blocks and accumulate the per-minute statistics incrementally — a 10-minute stereo buffer is 230 MB and must never be materialised. |
| **SC-018** the OQ-4 ruling | not a `TEST_CASE` | the compliance record; see S12.3. |
| **SC-019 (a)–(c)** rate / re-prepare | `..._test.cpp` / `SubharmonicEngine_RateAndReprepare` | (a) `prepare(44100)` then `prepare(96000)` on the same object with the full setter sequence re-pushed after each: all three `getToneFrequencyHz` unchanged within `1e-3` Hz; write above each ceiling and confirm `getFundamentalHz` / `getLowpassCutoffHz` move with the rate; 10 s renders at each rate agree in sub-band RMS within 1 dB. (b) `prepare()` twice on a fully configured object → **every** FR-061 getter equals a freshly prepared instance's, asserted getter by getter. (c) `prepare(4000)` → `getSampleRate() == 8000.0`; write both ends of the FR-013 and FR-040 ranges and read back, demonstrating no inverted `std::clamp` bounds. |
| **SC-020 (a)–(d)** infrasonic handling | `..._test.cpp` / `SubharmonicEngine_InfrasonicFloor` | Fixture: Div4 alone at `kMaxToneLevelDb`, tracking 0, wet 0 dB, and **`setDriveDb(kMinDriveDb)`** — pinned for the same reason the isolated-sub fixture pins it, and the arithmetic below is only valid with the pin. At the FR-041 default of `+3 dB` the stage is `tanh(1.4125 × 1.995) × 0.7079`, a peak of **0.703** rather than 1.995, i.e. ≈ 9 dB of gain reduction; an earlier draft of this row omitted it and recorded a margin ~7 dB too generous. This criterion is about the FR-016 backstop's attenuation law, so the saturator is pinned out of it rather than left to distort the number. (a) `f = 40` (Div4 = 10 Hz < 12) → `subTap` ≤ −80 dBFS over 10 s and `isToneInfrasonicFloored(Div4)` **true**. (b) `f = 55` (Div4 = 13.75 Hz) → `subTap` RMS ≥ −20 dBFS and floored **false** — the pair that proves "attenuated, not muted". **Arithmetic check (drive pinned to `kMinDriveDb`):** a `+6 dB` tone (1.995) through the 18 Hz Bessel HP at 13.75 Hz sees ≈ −7.5 dB → peak ≈ 0.84 → RMS ≈ −4.5 dBFS, **~15.5 dB of margin**. (c) step `f = 55 ↔ 40` mid-render in both directions: click-free, and `getToneCurrentGain(Div4)` reaches its new target within **52 ms** of the write (50 ms ramp + ≤ 1.333 ms control latency + 1 ms tolerance) and is monotonic once the ramp begins moving. (d) probe `isToneInfrasonicFloored` one Hz either side of `f = 48` (Div4), `f = 24` (Div2) and `f = 18` (Fifth). |
| **SC-021 (a)–(b)** default sub/body ratio | `..._test.cpp` / `SubharmonicEngine_DefaultSubToBodyRatio` | all shipped defaults, steady −12 dBFS 55 Hz sine on both channels, 10 s, first 2 s discarded, body RMS and `subTap` RMS each measured over the final 5 s, ratio transcribed. (a) ratio in `[−12, −6] dB`. **Plan's own arithmetic** (tone gains 0.1259/0.0631/0.0316 → LP at 120 Hz ≈ unity → `tanh` at drive 3 dB ≈ ×0.97 → Bessel HP at 27.5/13.75/36.67 Hz = 0.788/0.42/0.878 → `trackGain = 1` because `envNorm` clamps): sub RMS ≈ −22.7 dBFS against a body RMS of ≈ −15.0 dBFS → **≈ −7.7 dB**, inside the window with ~1.7 dB of margin at the tight end. (b) mutation check with `-6 / -12 / -18 dB` restored: predicted **≈ +4.5 dB**, i.e. above 0 dB, recorded. |
| **SC-022 (a)–(d)** sub-to-main routing | `..._test.cpp` / `SubharmonicEngine_SubToMainRouting` | **(a) is restated as a computable assertion (S14 C-11).** The spec's operand — "bit-identical to a render taken before FR-064 existed" (`spec.md:1210-1213`) — is not producible: the shipped implementation always has the flag, so any implementation of that sentence compares a flag-true render with another flag-true render and cannot fail. What FR-050's formula actually asserts, asserted directly: **(a1)** with a **silent** stereo input and the flag `true`, after 100 ms of settling `outL[i] == outR[i]` and `outL[i] == clamp(subTap[i] * wetGainLinear, ±kOutputClamp)` at **every** sample, bit-exact, where `wetGainLinear = wetGain(getWetGainDb())` (S2.1) — exact because a settled `LinearRamp` lands *on* its target (`smoother.h:379-383`), and silent-in makes `out` the add itself rather than a rounded sum. **(a2)** with the decorrelated body of (b), `(outL[i] − inL[i])` and `(outR[i] − inR[i])` agree within `1e-6 · max(1, \|in[i]\|)` — the same-scalar-on-both-channels claim under a real input, where the rounding of the add is the only admissible difference. Neither is a checked-in golden: both are within-render relations, so `lint-float-bit-goldens.js` is satisfied. (b) flag `false` → main output **bit-identical to the dry input** on both channels and both entry points, while `subTap` is **bit-identical** to the `true` render's `subTap`; **(b2) the tap is PRE-wet-gain (FR-062, plan addition):** every other criterion that reads `subTap` runs at `setWetGainDb(0.0f)` (the isolated fixture, SC-020, SC-021, and (b) above, which compares two taps at the same wet gain), so an implementation that taps *after* the wet-gain multiply is green everywhere and Phase 10's routing use of the tap silently inherits the wrong signal. Render twice, identical but for `setWetGainDb(0.0f)` vs `setWetGainDb(-12.0f)`: `subTap` must be **bit-identical** between the two (wet gain is applied strictly downstream of the tap and the follower sees the same input either way), while `out − in` drops by **12 dB ± 0.1 dB** in sub-band RMS. (c) toggling mid-render is click-free; (d) `isToneDormant`, `isToneInfrasonicFloored` and `getClampEngagementCount()` are unaffected by the flag's state. |
| **FR-009** argument contract | `..._test.cpp` / `SubharmonicEngine_ControlSurfaceContract` | every float setter: out-of-range → clamped, and the getter reports the clamp; out-of-range tone index → silent no-op and the documented neutral getter return. (Non-finite *rejection* lives in SC-009 (a), in the IEEE TU.) **Both ends of every named range are written and read back**, including the three that had no named bounds in an earlier draft: `setFollowerAttackMs` (`kMin/kMaxFollowerAttackMs`), `setFollowerReleaseMs` (`kMin/kMaxFollowerReleaseMs`) and `setToneBreathRate` (`kMin/kMaxBreathRateHz`) — the getters forward to the composed objects (S8), so this arm also proves the engine's clamp and the composed clamp agree. |
| **FR-003** default-constructed contract *(plan addition)* | same `TEST_CASE` | On a **default-constructed, unprepared** engine every scalar getter reports the FR-020/FR-021/FR-031/FR-035/FR-040/FR-041/FR-051 default — in particular `getToneLevelDb` = `{-18,-24,-30}`, `getToneBreathRate` = `{0.037,0.023,0.014}`, `getToneBreathDepth` = `{0.35,0.25,0.45}`, `getFollowerAttackMs` = 120, `getFollowerReleaseMs` = 800, `getToneWaveform` = `Sine`. This is FR-003's actual requirement and nothing else asserts it: SC-019 (b) compares two *prepared* instances and every other case calls `prepare()` first. It is what holds S1.1's constructor-calls-`applyDefaults()` rule in place, and it is the arm that fails if the FR-031 push is omitted (the follower would report its own 10 / 100 ms). |
| **FR-050** guard ladder *(plan addition)* | same `TEST_CASE` | Three normative FR-050 contracts that no criterion owned in an earlier draft. (i) **Pre-`prepare()` passthrough**: a default-constructed engine rendering non-trivial input copies in → out on both channels and writes **zeros** to `subTap` — asserted because this is a *deliberate divergence* from `FeedbackEcology`, which zero-fills (`:951-955`), and a copy-the-neighbour implementation would silence the dry path. Run it **in place** (`outL == inL`) too, which is the case S5.1's `if (outL != inL)` guard exists for. (ii) **Null pointers**: each of the four channel pointers nulled in turn (and `subTap` null, and all six positions per `spec.md:1233-1234`) leaves the output buffer byte-unchanged from a pre-filled sentinel pattern. (iii) **`numSamples == 0` consumes no control step**: interleave a zero-length call into a 64-sample-aligned render and fingerprint against the uninterrupted render — identical, because the FR-007 residue is absolute. |
| **Edge case — dry input above +12 dBFS** (Q6 / D-12 clamp scope) *(plan addition)* | `..._test.cpp` / `SubharmonicEngine_ClampScope` | **The only assertion in the phase that can fail if FR-054's clamp is mis-scoped.** Q6's ruling — the clamp binds `subChain * wetGain`, never the summed output — is currently indistinguishable from the rejected alternative: every criterion that reads `getClampEngagementCount()` asserts `== 0` (SC-006 (a), SC-009 (c3), SC-017 (A)(d)) and every planned render keeps the sum well under `kOutputClamp = 4.0` (SC-006 lands at ≈ +1.2 dBFS), so an implementation clamping `in + sub` passes all of them. The spec's own Edge Case (`spec.md:1272-1277`) is the separator and had no criterion. Fixture: subs at all defaults, flag `true`, dry input held at a constant **+6.0** linear (≈ +15.6 dBFS) on both channels for 2 s and **−6.0** for 2 s (DC is legitimate here — the dry path is a pure add with no filtering, and the follower simply reads an RMS of 6.0 and clamps `envNorm` to 1). Discarding 100 ms either side of the step, assert (i) `\|out[i]\| > 5.0` at **every** sample — an implementation clamping the sum caps it at 4.0 and fails by 1.0 linear, while the true sub contribution at defaults is ≤ ~0.15; (ii) `getClampEngagementCount() == 0`; (iii) `outL[i] − inL[i] == outR[i] − inR[i]` exactly (both channels take the same scalar and the same input here). |
| **FR-052 / FR-073** ledger | `..._test.cpp` / `SubharmonicEngine_StructuralBounds` | the two `constexpr` magnitudes and their `static_assert`s are compile-time; the case additionally asserts `getAllocatedBytes()` equals S9's formula for `maxBlockSamples` ∈ {64, 512, 8192} and that a request of 16 clamps to 64 and 99 999 clamps to 8192 through `getMaxBlockSamples()`. |

---

## S11. Build integration — the exact edits

**New files (six):**

```
dsp/include/krate/dsp/systems/subharmonic_engine.h
dsp/tests/unit/systems/subharmonic_engine_test.cpp
dsp/tests/unit/systems/subharmonic_engine_spectral_test.cpp
dsp/tests/unit/systems/subharmonic_engine_perf_test.cpp
dsp/tests/unit/systems/subharmonic_engine_nonfinite_test.cpp
tests/test_helpers/low_frequency_metrics.h
```

**Edit 1 — `dsp/tests/CMakeLists.txt`, the enumerated `dsp_systems_tests` list.** Append after the
Phase-5 block (currently `:446-449`), before the closing `)` at `:450`, with the comment header
Phases 3 and 5 carry:

```cmake
    # Vorago Phase 6 (specs/vorago-phase6-subharmonic): SubharmonicEngine.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   subharmonic_engine_test.cpp           SC-001, SC-006, SC-007, SC-008, SC-010,
    #                                         SC-011, SC-012, SC-014, SC-019, SC-020,
    #                                         SC-021, SC-022
    #   subharmonic_engine_spectral_test.cpp  SC-002, SC-003, SC-004, SC-005,
    #                                         SC-017 (A)+(B)   (the [long] set)
    #   subharmonic_engine_perf_test.cpp      SC-013 + the FR-071 stage/placement probe [.perf]
    #   subharmonic_engine_nonfinite_test.cpp SC-009 only
    unit/systems/subharmonic_engine_test.cpp
    unit/systems/subharmonic_engine_spectral_test.cpp
    unit/systems/subharmonic_engine_perf_test.cpp
    unit/systems/subharmonic_engine_nonfinite_test.cpp
```

**Edit 2 — `dsp/tests/CMakeLists.txt`, the `-fno-fast-math` block** (opens `:542`; the Phase-5 entry
is `:876` and the `PROPERTIES` line `:877`). Insert immediately before the `PROPERTIES` line:

```cmake
        # Vorago Phase 6: SC-009 injects NaN/Inf via bit patterns in this TU and needs
        # IEEE semantics to assert on them; it is also the only definition of
        # detail::SubharmonicEngineNonFiniteProbe. ONLY this one of the four Phase 6 TUs
        # is listed. subharmonic_engine_test.cpp and subharmonic_engine_spectral_test.cpp
        # stay out so the FR-008/FR-009 guards are proved in the /fp:fast + -ffast-math
        # mode the header actually ships in. The perf TU stays out too: -fno-fast-math
        # would change the figures its baselines are pinned to.
        unit/systems/subharmonic_engine_nonfinite_test.cpp
```

**Edit 3 — `dsp/lint_all_headers.cpp`**, after the Phase-5 include at `:185`:

```cpp
// Vorago Phase 6 (specs/vorago-phase6-subharmonic), FR-001
#include <krate/dsp/systems/subharmonic_engine.h>
```

**No edit to `tests/test_helpers/CMakeLists.txt`** — it is an INTERFACE library with no source list.

**Commands (Windows, full CMake path):**

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"

# build + run the layer that owns the new TUs
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5

# SC-016's consumer regression (the suites that compile the untouched shared headers)
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests; do \
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done

# the [long] set, run explicitly (per-push CI excludes it; it runs nightly on all 3 OSes)
build/windows-x64-release/bin/Release/dsp_systems_tests.exe \
  "SubharmonicEngine_LongRenderStationarity" 2>&1 | tail -5

# SC-015's gates
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
node tools/check-portability.js

# SC-016's byte-unchanged check
git diff --stat -- dsp/include/krate/dsp/processors/sub_oscillator.h \
  dsp/include/krate/dsp/primitives/minblep_table.h \
  dsp/include/krate/dsp/processors/envelope_follower.h \
  dsp/include/krate/dsp/primitives/two_pole_lp.h \
  dsp/include/krate/dsp/processors/saturation_processor.h \
  dsp/include/krate/dsp/primitives/dc_blocker.h \
  dsp/include/krate/dsp/processors/breathing_modulator.h \
  dsp/include/krate/dsp/primitives/smoother.h \
  dsp/include/krate/dsp/core/phase_utils.h \
  dsp/include/krate/dsp/core/random.h

# clang-tidy: single-TU on Windows for a small change set, .ps1 for a tree
clang-tidy -p build/windows-ninja dsp/tests/unit/systems/subharmonic_engine_test.cpp

# perf, ALONE, nothing else running
node tools/run-cpu-tests.js dsp_systems_tests
```

---

## S12. CPU budget — FR-071's probe, SC-013's gates, and the OQ-1 table

### S12.1 Measurement basis (inherited verbatim)

Nanoseconds per **512-sample block at 48 kHz**; best-of-**25** trials × **500** blocks after **400**
warm-up blocks; `[.perf]`-tagged so the per-push CI filter excludes it; checked-in baselines carried
as `static_assert`s so the absolute ceiling is still evaluated on every CI leg. One block period is
**10 666 667 ns**, so the roadmap's 0.5 % per-voice ceiling is

```cpp
constexpr double kBlockPeriodNs = (512.0 / 48000.0) * 1.0e9;   // 10 666 666.67
constexpr double kBudgetNs      = 53333.0;                      // SC-013's line — NO AGENT RAISES IT
static_assert(kBudgetNs >= kBlockPeriodNs * 0.0049 && kBudgetNs <= kBlockPeriodNs * 0.0051,
              "SC-013's budget is 0.5 % of one 512-sample block at 48 kHz");
```

*** STOP-AND-SURFACE RULE (FR-071, inherited verbatim from
`resonance_drift_network_perf_test.cpp:57-64`) — NON-NEGOTIABLE *** No implementing agent may lower
`kNumTones`, raise `kBudgetNs`, relax a threshold, or shrink a workload to make a figure fit. Reduce
cost, never move the line. If the engine total exceeds the ceiling, the build stops and surfaces the
measured per-stage table.

### S12.2 The probe's arms (FR-071 (a)–(n))

`SubharmonicEngine_StageCostProbe`, `[.perf]`, REQUIREs only that every figure is **finite and
strictly positive** — a zero or a NaN means the measurement is broken, which is the one thing that
would make the table lie. The gates live in SC-013; the transcription and ruling in FR-076.

| Arm | What is measured | Why it is shaped that way |
|---|---|---|
| (a) | the two `PhaseAccumulator`s alone, `advance()` ×2 per sample | the irreducible floor |
| (b) | one `SubOscillator` at `Sine` | isolates the `std::sin`, which S12.4 predicts dominates |
| (c) | one `SubOscillator` at `Square` | prices D-6's rejected default and gates SC-013 (b) |
| (d) | three tones summed with the FR-022 gains | (b)×3 plus the ramps, so a superlinear surprise is visible |
| (e) | `EnvelopeFollower::processSample` **including S7.6's `isFinite` guard** | the guard's cost is measured, not assumed |
| (f) | `TwoPoleLP::process` + the S5.3 glide's per-control-step `exp2`/compare | both halves of the low-pass stage |
| (g) | `SaturationProcessor::processSample` | A-2's three `OnePoleSmoother` advances per call (S14 C-6) |
| (h) | `DCBlocker2::process` | |
| (i) | the whole engine at defaults (`f = 55`, all three tones awake) with a live body | **SC-013 (a)'s gated arm** |
| (j) | the whole engine, all three tones `Square` | **SC-013 (b)'s gated arm** |
| (k) | the whole engine dormant (FR-025's skip) | **SC-013 (c)** and OQ-1's cheap-voice term |
| (n) | the whole engine at `setFundamentalHz(40.0f)` — Div4 floored, two tones awake | SC-013 (d), reported not gated |
| (l) | **one** instance at defaults | OQ-1's global-placement arm |
| (m) | **eight** instances at defaults, all rendered per block | OQ-1's per-voice-placement arm |

Arms (l) and (m) are `(i)` re-measured at the two polyphony shapes OQ-1 needs; they are separate arms
rather than an `(i) × 8` multiplication because eight instances share L1/L2 and the scaling is the
thing being measured.

### S12.3 SC-013's gates and the FR-076 / SC-018 deliverable

- **(a)** arm (i) ≤ **53 333 ns/block**. The percent figure is reported, never asserted. The baseline
  is carried as a checked-in `static_assert`.
- **(b)** arm (j) gated at the **same** 53 333 ns/block ceiling. `Square` is a shipped configuration
  and roadmap line 321's budget is unqualified; if the arm misses, the stop-and-surface rule applies
  and the *waveform option* is reconsidered — never the budget.
- **(c)** arm (k) is at least **15 %** cheaper than arm (i), with the absolute ns saving transcribed.
  The margin is tied to the ~14 % session-to-session drift the perf idiom records
  (`resonance_drift_network_perf_test.cpp:78-90`). If the saving is real but below 15 %,
  stop-and-surface and reconsider what FR-025 skips; do not lower the margin.
- **(d)** arm (n) measured and reported, not separately gated.

**FR-076 / SC-018 — the ruling, which is a phase deliverable, not a note.** The compliance record must
contain, from a `node tools/run-cpu-tests.js dsp_systems_tests` run **in isolation**:

1. arms **(k)**, **(l)** and **(m)** as measured ns per 512-sample block at 48 kHz;
2. the arithmetic projecting them onto both placements, as a percentage of the 10 666 667 ns block
   period, set beside the roadmap's 4–5 % per-voice envelope (line 92) **net of what phases 2, 3 and 5
   have already spent — 1.75 % + 0.75 % + 1.5 % = 4.0 %**, i.e. the per-voice placement has
   ≈ 0.5–1.0 % of headroom left and a 0.5 % component fits only at the top of that range;
3. the **recorded ruling** on roadmap Open Question 4 (global post-voice-sum vs per-voice) and its
   reasoning.

The table to fill:

| Placement | Instances | Cost/block | % of one core | Fits the roadmap envelope? |
|---|---|---|---|---|
| Global (post-voice-sum) | 1 | arm (l) | (l)/10 666 667 | global budget, not per-voice |
| Per-voice, 4 voices | 4 | ≈ 4 × arm (l) | | 4 × per-voice line |
| Per-voice, 6 voices | 6 | | | |
| Per-voice, 8 voices | 8 | arm (m) measured | | |
| Per-voice, 8 voices, 6 dormant | 8 | 2·(l) + 6·(k) | | the case OQ-1 says makes per-voice affordable |

**The ruling is the user's**, taken from that table at the end of the build stage; the build stage must
present it and stop. It binds Phase 10's wiring, not this component's code — the component is
placement-agnostic by construction (pitch enters through one scalar, the body as ordinary stereo
audio, and FR-035's settable reference is what makes a per-voice calibration possible).

### S12.4 Where the cost is expected to be, so a miss is diagnosable

Per sample, at defaults: 2 `PhaseAccumulator::advance` (2 adds + 2 compares); **3 `std::sin`** inside
the three `SubOscillator::process` calls; 8 `LinearRamp::process` (each one compare when parked);
1 `EnvelopeFollower::processSample` (a multiply, a compare, a `std::sqrt`, two denormal flushes) plus
one `isFinite` bit test; 1 `Biquad::process` (5 mul, 4 add); 1 `SaturationProcessor::processSample`
(3 `OnePoleSmoother` advances + the Padé `tanh`, which carries a **divide**); 1 `DCBlocker2::process`
(5 mul, 4 add); ~8 further multiplies and 2 compares.

**One non-per-sample term, recorded so the table is honest** (it is not measured by any arm, because
no arm crosses a dormancy edge): the FR-026 sleep edge and `recoverNonFinite()` each call
`SaturationProcessor::reset()`, whose `std::fill` over `dryBuffer_` (`saturation_processor.h:159`) is
an **O(`maxBlockSamples`) memset** — up to 32 KB. It is once per dormancy *edge* (`chainActive_` is an
edge latch, so arm (k)'s steady dormant render never touches it) and, for the recovery path, never at
all on a shipping audio thread (S7.5: rung 4 is unreachable through the public API). The full
accounting is in S9. If a future phase makes dormancy edges frequent, this is the term to look at
first — not the per-sample ladder.

The three `std::sin` and the `tanh`'s divide are the two candidates for a miss. The **pre-authorised
levers**, in order, none of which touches a shipped header or a threshold:

1. **A-2's lever**, already named by the spec: bypass `SaturationProcessor`'s per-sample smoother
   advances by writing its gains only on change — a change to *this* component's call pattern, never
   to the shipped processor. (Note it cannot be done by skipping `processSample`: the smoothers only
   advance inside it. The lever is to accept the three advances and, if they dominate, replace the
   stage with a direct `Sigmoid::tanh` call plus this component's own two scalars — still no shipped
   header touched, but it *is* a departure from FR-041's "one `SaturationProcessor`" and must be
   surfaced as a spec question, not taken silently.)
2. If the three `std::sin` dominate: nothing in this phase can remove them without abandoning D-4's
   reuse mandate. Surface arms (b) and (d) and let the user rule.

Neither lever may be applied to make a number fit without the measured table on the record first.

---

## S13. Risks and mitigations

| # | Risk | Why it is real here | Mitigation |
|---|---|---|---|
| R-1 | **The `MinBlepTable` is not prepared and every tone is silent, for the life of the object.** | `SubOscillator::prepare` hard-fails on an unprepared table and `process()` then returns `0.0f` forever. A criterion measuring "≤ −80 dBFS" would *pass* on a dead engine. | `prepare()` step 4 orders the table first; S1.2's `static_assert` pins `length() ≤ 64`; **SC-005 (b) is the positive assertion** (`subTap` RMS **above** −40 dBFS on the Square path) and the isolated fixture's `> −60 dBFS` gate is the general one — scoped in S10.3 to the stationary single-tone criteria, with SC-002 carrying an anchor gate of its own because the unconditional floor would fail its −60 dBFS sweep point on a correct implementation. Every spectral criterion refuses to report on silence. |
| R-2 | **The follower is poisoned and the engine mutes silently.** | The four-link trace in S7.6: `EnvelopeFollower` does not validate, `std::clamp` passes a NaN, `LinearRamp::setTarget` mutes to zero with no counter, and the result is *finite* so FR-055's trap never fires. | S7.6's two guards (per-sample input `isFinite`, per-control-step `getCurrentValue` check + `follower_.reset()`). SC-009 (c2) is the criterion that fails without them. Recorded as spec correction C-3. |
| R-3 | **The seed is inert and SC-011 (b) cannot pass.** | `BreathingModulator`'s RNG is only drawn from `drawCycleJitter()`, which does nothing at the shipped `irregularity_ = 0`. Nothing else in this component is stochastic. | `kDefaultBreathIrregularity = 0.25` written in `applyDefaults()` (S14 C-5). The 60 s divergence budget is worked in S10.4; if it misses, raise the constant, never the threshold. |
| R-4 | **A move of the engine dangles all three `MinBlepTable` pointers.** | `MinBlepTable` is movable, so the implicit move would be generated and would be silently wrong. | S1.1 deletes copy **and** move. Phase 10 holds the engine by value. |
| R-5 | **A cutoff step clicks and SC-008 fails late, after the header is written.** | The `b0` swap plus stale biquad state on a 50× cutoff move. | The S5.3 control-rate log2 glide, designed in from the start. If the perf probe (arm (f)) shows the `exp2` matters, the fallback is a linear-Hz ramp — never removing the glide. |
| R-6 | **Denormals in the sub path.** | `Div4` at 13.75 Hz is attenuated ≈ −7.5 dB by the blocker and, at a low tone level with `trackGain` near zero, the chain can carry very small values for long stretches. | `Biquad`, `DCBlocker2` and `LinearRamp` all call `detail::flushDenormal` on their state; the test main sets FTZ/DAZ (`tests/test_helpers/enable_ftz_daz.h` via `dsp_test_main.cpp`); the Edge Case "denormal input at 1e-30 for 60 s" is exercised in SC-017's neighbourhood. No new denormal-prone state is introduced. |
| R-7 | **A 262 144-point FFT is outside the size `FFT`'s doc comment advertises.** | `fft.h:148` says `[256, 8192]`; the code checks only power-of-two, and pffft handles 2^18. | The helper `REQUIRE`s `fft.isPrepared()`; SC-003 (c) validates the estimator against synthetic sines *before* any engine measurement, so a transform failure fails as a helper bug. One shared `FFT` instance per TU. |
| R-8 | **Cross-toolchain float spread on the spectral criteria.** | MSVC / GCC / AppleClang differ in the 7th–8th decimal, and the macOS leg is `-ffast-math`. | No bit-exact goldens anywhere (`tools/lint-float-bit-goldens.js`). Every render comparison goes through `render_fingerprint.h`'s measured tolerances; every spectral figure is asserted against a **measured threshold with margin** (SC-003 ±0.5 %, SC-004 2 % against a ≈ 1.9 % worst case, SC-021 a 6 dB window around a −7.7 dB prediction). |
| R-9 | **`-ffast-math` folds a finiteness test.** | Three of the four TUs build in the shipping FP mode. | `detail::isNaN`/`isInf`/`isFinite` only, everywhere, header and tests (`tools/lint-nonfinite-symbols.js` gates it); non-finite test values built from bit patterns through a `volatile` sink, never `std::numeric_limits`. |
| R-10 | **MSVC-green proves nothing about the Linux/macOS legs.** | Clang errors on narrowing in brace init where MSVC does not; GCC rejects what MSVC accepts. | Designated initialisers mandatory on `PrepareConfig`; explicit `static_cast` on every `std::size_t`↔`float`↔`double` crossing; `node tools/check-portability.js` before the commit; the WSL g++ 13 probe for any Linux doubt. |
| R-11 | **`std::clamp` with inverted bounds is UB and MSVC traps it.** | Two clamp pairs are built from `[floor, ratio × fs]`. | `kMinUsableSampleRate = 8000.0` plus the two ordering `static_assert`s in S1.2; SC-019 (c) exercises `prepare(4000)` and both range ends. |
| R-12 | **SC-013 (c)'s 15 % dormancy margin is not met.** | The skipped work is two biquads and a `tanh`; the always-on work includes three `std::sin`. | S12.4's estimate says the skip removes ~30–40 %, but that is an estimate. If it misses, stop-and-surface and reconsider what FR-025 skips (the honest candidate is skipping the `SubOscillator` calls too — which FR-025 forbids for a reason the roadmap requires be argued in terms of what the listener hears). Never lower the margin. |
| R-13 | **The 10-minute SC-017 arms exhaust memory.** | A 10-minute stereo render is 230 MB per arm. | Block-wise rendering with incremental per-minute statistics; nothing longer than one block is materialised. Both arms `[long]`, excluded from per-push CI, run nightly on all three OSes. |
| R-14 | **A future reader "fixes" the `0.495 × sampleRate` oddity in `Biquad::clampFrequency`.** | It reads like a Nyquist ratio and is not one; if it were corrected to `0.495 × Nyquist` the applied ceiling at 8 kHz would drop to 1980 Hz and this component's reported 2000 Hz would become a lie. | Recorded in S0 and in S14 C-2; the component's own ceiling (2000 Hz) is what `getLowpassCutoffHz` reports, and SC-019 (c) reads both range ends back. |

---

## S14. Spec corrections and open items

Every correction below was found by reading the shipped code, not by re-reading the spec. **None
relaxes a threshold**; two of them (C-3, C-5) make a criterion reachable that otherwise could not
pass on a correct implementation.

- **C-1 — `SubharmonicEngine` must delete its copy *and move* operations.** The spec's New-components
  table and FR-001/FR-003 say nothing about copy/move semantics. `MinBlepTable` is non-copyable but
  **movable** (`minblep_table.h:55-59`), so the implicit move constructor and assignment **would be
  generated** and would leave all three `SubOscillator`s pointing at the moved-from object's table.
  Fix: delete all four (S1.1). No FR text needs to change; the plan supplies the rule.
- **C-2 — `Biquad::clampFrequency` applies `kMaxFrequencyRatio = 0.495f` to the SAMPLE RATE, not to
  Nyquist** (`biquad.h:88`, `:155-166`). The plan checked whether FR-040's `min(2000, 0.45·fs)`
  ceiling could ever exceed what the biquad actually applies: at the 8 kHz floor the biquad's ceiling
  is 3960 Hz, so this component's 2000 Hz always binds first and `getLowpassCutoffHz()` is truthful at
  every accepted rate. **No change to FR-040.** Recorded because the constant is counter-intuitive and
  a future "correction" of it would silently break the getter's truthfulness (R-14).
- **C-3 — FR-055's ladder does not close the follower-poisoning path; the plan adds two guards.**
  Full trace in S7.6. Without them SC-009 (c2) fails on a correct-by-the-spec implementation, and a
  non-finite input during a *dormant* stretch kills the engine with no symptom at all. The guards are
  a per-sample `detail::isFinite` on the follower's input and a per-control-step rejection of a
  non-finite `getCurrentValue()`. **Neither sanitises the dry path**, so FR-055's stated principle is
  intact. Suggest a one-clause amendment to FR-055 recording the sensor guard explicitly.
- **C-4 — FR-052's `kMaxPreSaturationMagnitude` formula omits the sub-oscillator's own output
  bound.** `SubOscillator::sanitize` clamps every tone to `[-2, +2]` (`sub_oscillator.h:356-364`), and
  the Square path's minBLEP residual genuinely exceeds 1.0. The honest constant is
  `kNumTones × 2.0 × dbToGain(+6) × 1.45 ≈ 17.36`, not `≈ 8.68`. **Nothing downstream moves**:
  `kMaxPreClampMagnitude` (the number SC-006's +9.5 dBTP derivation uses) is unchanged at ≈ 2.00,
  because the `tanh` bounds the chain regardless, and the `static_assert`
  `kMaxPreSaturationMagnitude > kSaturatorOutputBound` becomes *more* true. Suggest amending FR-052's
  first bullet to carry the factor.
- **C-5 — FR-021's default table must include a non-zero breathing irregularity, or the FR-070 seed is
  inert and SC-011 (b) is unreachable.** `BreathingModulator`'s RNG is consumed **only** by
  `drawCycleJitter()` (`breathing_modulator.h:262-271`), which draws nothing at the shipped default
  `irregularity_ = 0.0f`. The three breathers are this component's *only* stochastic element, so at
  the spec's stated defaults two different seeds render **bit-identically** and SC-011 (b)'s "the
  fingerprint comparison fails" cannot happen. Fix: `applyDefaults()` writes
  `setIrregularity(kDefaultBreathIrregularity = 0.25f)` on all three (S1.2, S2.2). Not exposed as a
  setter — FR-060's list stays closed. Suggest adding the value to FR-021's default table.
- **C-6 — spec A-2 calls `SaturationProcessor`'s three parameter smoothers `LinearRamp`s; they are
  `OnePoleSmoother`s** (`saturation_processor.h:415-417`). The assumption A-2 makes (three smoother
  advances per `processSample`, measured by FR-071 arm (g)) is unaffected; only the type name is
  wrong. Recorded so the perf TU's comment does not repeat it.
- **C-7 — `measureTruePeakDb` needs a `sampleRate` argument.** Spec A-4 sketches
  `measureTruePeakDb(const float* l, const float* r, size_t n)`, but the shipped
  `Oversampler::prepare(double sampleRate, size_t maxBlockSize, …)` (`oversampler.h:288`) requires
  one. The helper's signature gains a fourth parameter (S10.1).
- **C-8 — the spec's FR-004 step list does not mention that `SubOscillator`'s constructed default
  waveform is `Square`.** `applyDefaults()` must write `Sine` explicitly or the shipped default is a
  square sub and D-6 is silently reversed. No FR text is wrong; the plan makes the write mandatory
  (S2.2) and SC-019 (b) — "every getter equals a freshly prepared instance's" — is what catches an
  omission.
- **C-9 — the spec does not say where the low-pass cutoff is pushed.** FR-040 forbids per-sample
  writes and says nothing else; SC-008 requires a full-range sweep to be click-free. The plan adds a
  control-rate log2 glide (S5.3) as the implementation of FR-040's control-surface rule, with the
  direct-step alternative recorded and rejected. No FR text needs to change.
- **C-10 — `EnvelopeFollower`'s attack/release milliseconds are JUCE-style ~99 %-settling times, not
  time constants.** `calculateCoefficient` is `exp(-2π / (ms · 0.001 · fs))`
  (`envelope_follower.h:356-365`), so the effective τ is `ms / 2π`: FR-031's `120 ms` attack is
  τ = **19.1 ms** and its `800 ms` release is τ = **127.3 ms** (in the *squared* domain, so the
  envelope's amplitude τ is 254.6 ms). Three consequences, all recorded rather than acted on
  unilaterally: (i) SC-001 (b)'s parenthetical "≈ 96 % at 400 ms" was computed on the wrong
  convention — the true prediction is ≈ 100 %, and the 50 % floor stands unchanged; (ii) **no audio
  bracket on the attack constant is constructible**, because the FR-032 gain ramp (τ = `kGainRampMs`
  = 50 ms) dominates the composite: at 100 ms after a body step the correct build reads 0.82 of
  steady state and a build that never pushed the value (10 ms attack → τ = 1.59 ms) reads 0.86 — a
  5 % separation, which is why FR-031's attack is bracketed on the read surface (S8's forwarding
  getters) and only its *release* is bracketed in audio (SC-001 (c2), where the separation is ~90 dB);
  (iii) FR-031's stated rationale ("a drone's body moves on the scale of seconds") describes a
  slower follower than these numbers deliver. **The defaults are not changed by this plan** — that is
  a spec decision, not a plan one. Surfaced for the user at the T0 amendment; if they want the stated
  behaviour the values become ~750 / 5000 ms, and SC-001 (c2)'s window moves with them.
- **C-11 — SC-022 (a)'s reference render does not exist and the criterion cannot fail as written.**
  It asks for bit-identity against "a render taken before FR-064 existed" (`spec.md:1210-1213`); the
  shipped implementation always has the flag, so the only render an author can produce for the
  right-hand side is another flag-true render. The plan replaces the phantom operand with the FR-050
  formula stated as an assertion (S10.4, (a1)/(a2)) — same claim, computable. Suggest amending
  SC-022 (a) to the (a1)/(a2) text.
- **C-12 — FR-026's `SaturationProcessor::reset()` is not what the FR's rationale implies, and it is
  not free.** The FR's justification is "a stale tail sits in the filter's `y1_`/`y2_` and replays on
  wake"; that is true of `TwoPoleLP` and `DCBlocker2` and **false** of `SaturationProcessor` as this
  component calls it — `processSample` (`saturation_processor.h:228-250`) touches neither
  `dryBuffer_` nor `dcBlocker_`, so the only state its `reset()` clears that is observable here is
  three parameter smoothers. What the call *does* cost is a `std::fill` over `dryBuffer_` (`:159`),
  an O(`maxBlockSamples`) memset on the audio thread at every dormancy edge and every FR-055
  recovery. The plan **keeps the call** (FR-026 names it, and the smoother snap is harmless), corrects
  the justification (S5.4), prices the memset (S9, S12.4), and restates SC-014 (c2) as a **two-line**
  mutation — the third line is not audio-falsifiable and including it over-claimed what the check
  proves. No FR text needs to change; a one-clause note in FR-026 would be honest.
- **C-13 — three getters forward to their composed object instead of reporting a stored scalar.**
  `getFollowerAttackMs`, `getFollowerReleaseMs` and `getToneBreathRate` (S8). Not a style choice: with
  the value stored twice, a build that stores FR-031's 120 / 800 ms and never calls
  `setAttackTime`/`setReleaseTime` (leaving the follower at its shipped 10 / 100 ms,
  `envelope_follower.h:92-93`) satisfies every criterion in the phase — SC-019 (b) compares stored
  scalars, SC-001 (b) is a floor a *faster* follower clears more easily, SC-001 (c) is a ceiling a
  faster release clears trivially, SC-002 measures steady state. Holding the value in one place makes
  the defect unrepresentable. FR-061's text is unaffected (it says what the getter reports, not where
  the byte lives); FR-031's clamp is still stated at this level through the named constants of S1.2,
  which `static_assert` against the shipped bounds so the two clamps cannot drift apart.

**Open items for the build stage** (none blocks starting):

- **OQ-1 (roadmap Open Question 4)** is unchanged and is answered from FR-071 arms (k)/(l)/(m) by the
  user at the end of the build stage. S12.3 has the table to fill and the projection arithmetic.
- SC-013 (c)'s 15 % dormancy margin and SC-004's 2 % THD ceiling are the two thresholds most likely to
  be tested by measurement. Both have a stop-and-surface response written; neither may be moved.

---

## S15. Suggested task order (`tasks.md` owns the real breakdown)

1. **T0 — docs.** Land this plan and the S14 spec amendments (C-3, C-4, C-5, C-6, C-7, **C-10, C-11,
   C-12**) in one commit, so the FRs the build asserts against are the ones on disk. Two of the new
   ones need a decision rather than a transcription, and the commit must carry both: **C-11** rewrites
   SC-022 (a) (its stated reference render is not producible, so the criterion cannot fail as
   written), and **C-10** surfaces that FR-031's 120 / 800 ms behave as τ = 19.1 / 127 ms — the
   defaults stay as specified unless the user rules otherwise, and this plan is written against the
   specified values.
2. **T1 — the perf probe, before the component exists.** Create
   `subharmonic_engine_perf_test.cpp` with arms (a)–(h) only (the composed primitives, no engine) and
   run it in isolation. This prices the three `std::sin`, the `tanh` divide and the follower guard
   *before* a line of the engine is written, exactly as Phase 3's probe did. A day now, or a phase
   later.
3. **T2 — the helper header** `low_frequency_metrics.h` plus its four self-validation cases. Written
   before the criteria that consume it, so a helper bug is never mistaken for an engine defect.
4. **T3 — the header skeleton:** constants (with every `static_assert` live), nested types, the full
   public surface with stub bodies, the deleted copy/move, the probe declaration and the friend.
   Register all four TUs and the `lint_all_headers.cpp` include in the same commit — an unregistered
   TU silently drops out of the build.
5. **T4 — `prepare()` / `reset()` / `setSeed()` / `applyDefaults()` / the clamp helpers**, then
   SC-010, SC-019 and the FR-009 contract case.
6. **T5 — the tones and the two masters** (S2.3, S4.1), then SC-003 and SC-005.
7. **T6 — the control step** (S5.2–S5.4) and the three-factor gain, then SC-011 and SC-012.
8. **T7 — the chain and the safety ladder** (S6, S7), then SC-004, SC-002, SC-006, SC-007, SC-021.
9. **T8 — dormancy, the sleep edge and the infrasonic backstop**, then SC-014 (including the (c2)
   mutation check), SC-020, SC-008.
10. **T9 — routing and the tap**, then SC-022, and SC-009 in the IEEE TU.
11. **T10 — the perf arms (i)–(n)** added to the T1 probe, run in isolation, baselines pinned as
    `static_assert`s; then SC-013.
12. **T11 — the `[long]` arms** SC-017 (A) and (B), run explicitly.
13. **T12 — gates:** lints, `check-portability`, clang-tidy, the SC-016 regression suites and the
    byte-unchanged diff.
14. **T13 — FR-076 / SC-018:** transcribe arms (k)/(l)/(m), fill S12.3's projection table, **present
    it to the user and stop** for the Open Question 4 ruling. The phase is not complete until the
    table and the ruling are both in the compliance record.

---

## S16. Review notes

Every issue from the plan review was applied. Two were applied by the reviewer's **alternative**
resolution rather than its primary one, both because the primary would have put a wrong number or a
silent spec deviation into the document. Recorded here so a later reader does not "restore" them.

- **FR-031's attack constant is bracketed on the read surface, not in audio.** The review's primary
  suggestion was an SC-001 (b) ceiling ("≤ 70 % of steady state at 100 ms") to bracket the 120 ms
  attack from above. It does not separate: `EnvelopeFollower`'s coefficient is
  `exp(-2π / (ms · 0.001 · fs))` (`envelope_follower.h:356-365`), so 120 ms is τ = 19.1 ms and the
  un-pushed 10 ms default is τ = 1.59 ms, and both are swamped by the FR-032 gain ramp (τ = 50 ms).
  Worked through in S14 C-10: at 100 ms the two configurations read **0.82 and 0.86** of steady
  state — a 5 % gap, well inside cross-toolchain and measurement spread, so a 70 % ceiling would
  either pass both builds or fail the correct one depending on where it was set. The review's second
  suggestion is what the plan takes — assert the pushed values on the follower itself — implemented
  as S8's **forwarding getters** (S14 C-13), which is stronger than a probe read because it makes the
  "stores but never pushes" build unrepresentable rather than merely detected. The *release*
  constant does separate cleanly in audio (~90 dB), so it is bracketed there too: SC-001 (c2).
- **`saturator_.reset()` stays at both audio-thread call sites.** The review's primary suggestion was
  to drop it from the sleep edge and from `recoverNonFinite()`, keeping it only in
  `prepare()`/`reset()`. The diagnosis behind the suggestion is correct and is now recorded in full
  (S5.4, S9, S14 C-12): the call clears no audio state this component can observe, and it carries an
  O(`maxBlockSamples`) `std::fill`. But **FR-026 and FR-055 both name `SaturationProcessor::reset()`
  normatively** (`spec.md:511-512`, `:684-688`), so dropping it in the plan would be a silent
  deviation from the spec the build asserts against — precisely the class of gap the T0 amendment
  commit exists to prevent. The plan therefore takes the review's stated fallback: keep the call,
  correct the justification, price the memset in S9 and S12.4, and restate SC-014 (c2) as the
  **two-line** mutation it can actually falsify. If the user prefers the drop, it is a one-clause
  amendment to FR-026 and FR-055 at T0 and two deleted lines in the header.
