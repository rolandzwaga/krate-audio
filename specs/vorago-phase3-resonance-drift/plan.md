# Implementation Plan: Vorago Phase 3 — Resonance Drift Network

**Spec:** `specs/vorago-phase3-resonance-drift/spec.md` (1830 lines, read in full this session)
**Roadmap:** `specs/Vorago-roadmap.md` Part A → Phase 3 (lines 212–229); cross-cutting constraints
(lines 479–500), including the Dormancy rule (lines 489–493)
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/resonance_drift_network.h`,
four new test TUs, one enumerated-list edit in `dsp/lint_all_headers.cpp`, two edits in
`dsp/tests/CMakeLists.txt`, **and one bounded edit to `spec.md` at T1.5** writing back S14's
corrections C-1/C-6 … C-13 (FR-017, FR-035, FR-044, FR-052, FR-062, SC-001 (c), SC-002, SC-011,
SC-020 (b), Edge Cases' sample-rate floor). **Conditionally** (FR-013 Tier 1, probe-gated): one purely additive
method on `ResonatorBank` plus its own cases in the already-registered
`dsp/tests/unit/processors/resonator_bank_test.cpp`.
**Test targets:** `dsp_systems_tests` (all four new TUs); `dsp_processors_tests`, `dsp_effects_tests`,
`dsp_primitives_tests`, `dsp_core_tests`, `membrum_tests`, `innexus_tests`, `seraphis_tests`
(SC-013 regression gate).
**Plugin work:** none. Vorago's plugin starts at Phase 11.

---

## S0. Verification ledger — every signature below was read this session

Nothing in this plan is quoted from the spec without re-opening the header. Where spec and code
disagree, the code wins and the disagreement is recorded in **S14 (Spec corrections)**.

| Claim this plan is built on | Verified at |
|---|---|
| `ResonatorBank::prepare(double)`, `reset()`, `setFrequency(size_t,float)`, `setDecay`, `setGain(size_t,float dB)`, `setQ(size_t,float)`, `setEnabled(size_t,bool)`, `isEnabled`, `setDamping`, `setExciterMix`, `setSpectralTilt`, `process(float)`, `processBlock(float*,size_t)`, `isPrepared()` | `processors/resonator_bank.h:184`, `:213`, `:328`, `:348`, `:367`, `:384`, `:401`, `:410`, `:421`, `:432`, `:443`, `:470`, `:522`, `:534` |
| `setFrequency` re-derives Q from the stored decay: `qValues_[index] = rt60ToQ(frequencies_[index], decays_[index]);` | `resonator_bank.h:333` |
| `setQ` clamps to `[kMinResonatorQ, kMaxResonatorQ]` and recomputes coefficients; `setGain` writes `gainsDb_`/`gains_` only — **no** coefficient recompute, **no clamp** | `resonator_bank.h:384-388`, `:367-371` |
| `reset()` is a **configuration wipe**: `filter.reset()` on every slot, then 440 Hz / `kDefaultDecayTime` / gain 1.0 / `kDefaultResonatorQ` / **`enabled_[i] = false`**, then `damping_ = exciterMix_ = spectralTilt_ = 0`, `tuningMode_ = Custom`, `numActiveResonators_ = 0` | `resonator_bank.h:213-245`, doc at `:212` |
| `prepare()` initialises the same per-slot table and sets `prepared_ = true`; it does **not** write `gainsDb_` | `resonator_bank.h:184-209` |
| render loop: 3 global smoothers per sample, then `for (i < kMaxResonators) { if (!enabled_[i]) continue; ... }`, `filterOutput *= dampingScale * gains_[i] * tiltGain`, `wetSum += filterOutput`, `output = input*currentMix + wetSum*(1-currentMix)` | `resonator_bank.h:470-517`, skip at `:487-488`, mix at `:514` |
| **no per-resonator output accessor exists** — every write to `filterOutput` (`:498-510`) is consumed by `wetSum` | swept this session over the whole class |
| `process()` returns `input` unchanged when `!prepared_` | `resonator_bank.h:471` |
| RBJ constant-0 dB-peak bandpass, `alpha = sin(omega)/(2Q)`, coefficients **hard-swapped** by `filters_[index].setCoefficients(coeffs)` with no crossfade | `resonator_bank.h:560-592`, swap at `:591` |
| `calculateTiltGain` early-returns `1.0f` at tilt exactly `0.0f`, else `std::log2` + `dbToGain` per resonator per sample | `resonator_bank.h:120-126`, called `:507` |
| `rt60ToQ(f, rt60) = clamp(π·f·rt60/kLn1000, 0.1, 100)`; `kLn1000 = 6.907755278982137f` | `resonator_bank.h:92-98`, `:81` |
| `kMaxResonators = 16`, `kMinResonatorFrequency = 20`, `kMaxResonatorFrequencyRatio = 0.45`, Q `[0.1, 100]`, decay `[0.001, 30]`, `kDefaultDecayTime = 1.0`, `kDefaultResonatorQ = 10`, `kResonatorSmoothingTimeMs = 20` | `resonator_bank.h:39,42,45,48,51,54,57,60,63,69` |
| `clampFrequency(hz) = clamp(hz, 20, 0.45·fs)` — a bare `std::clamp`, which does **not** reject NaN | `resonator_bank.h:542-545` |
| `BrownianDrift::prepare(double)` floors the rate at 1 Hz and sets `controlDtSeconds_ = kControlRateInterval / sampleRate_`; `reset()` = `initState()` (re-seeds the RNG from `configuredSeed_`, `x_ = mean_`, snaps the smoother, `samplesUntilControl_ = 0`) | `brownian_drift.h:121-129`, `:133-135`, `:242-247` |
| `setSeed(uint32_t)` re-seeds the RNG but does **not** rewind `x_` or the output smoother | `brownian_drift.h:145-148` |
| `setSmoothness(float)` clamps `[0,1]`; `tau = kTauMin + s·(kTauMax − kTauMin)`; `a = exp(−dt/tau)`; `g = kInternalStd·sqrt(1 − a²)` | `brownian_drift.h:152-155`, `:230-240` |
| `kTauMin = 0.2`, `kTauMax = 30.0`, `kInternalStd = 0.5`, `kDriftOutputSmoothMs = 150.0`, `kControlRateInterval = 32`, `kDefaultDriftSeed = 0xB17E` | `brownian_drift.h:97,99,101,103,105,109` |
| `getCurrentValue()` returns `clamp(outputSmoother_.getCurrentValue(), -1, +1)` — bounded by construction | `brownian_drift.h:212-214` |
| `processBlock(size_t)` loops in ≤32-sample slices, each calling `outputSmoother_.advanceSamples(advance)` | `brownian_drift.h:194-206` |
| `OnePoleSmoother::advanceSamples(n)` early-returns when `isComplete()`, else costs **one `std::pow`** | `primitives/smoother.h:243-255` |
| `OnePoleSmoother`: `configure(ms, sr)` `:160`, `setTarget` (NaN→0, Inf→±1e10, `ITERUM_NOINLINE`) `:170`, `getCurrentValue` `:191`, `process()` `:197`, `isComplete()` `:232`, `snapTo` `:263`, `reset()` (current=target=0) `:275` | `smoother.h` |
| `LinearRamp`: `configure(rampMs, sr)` `:329`, `setTarget` `:342`, `getCurrentValue` `:364`, `process()` `:370`, `isComplete()` (`current_ == target_`) `:409`, `snapTo` `:421`, `reset()` `:434`; `process()` clamps overshoot to land **exactly** on the target and flushes denormals | `smoother.h:305-438`, overshoot clamp `:379-383` |
| `calculateLinearIncrement(delta, ms, sr) = delta / (ms·0.001·sr)`, recomputed on **every** `setTarget` ⇒ the ramp is constant-**duration** from wherever it currently sits | `smoother.h:100-108`, `:353` |
| `deriveStreamSeed(base, salt)` — lowbias32 finaliser, guaranteed non-zero; `Xorshift32::seed(0)` silently substitutes `2463534242u` | `core/random.h:102-113`, `:73-74`, `:83` |
| `Xorshift32::nextFloat()` → `[-1,1]`, `nextUnipolar()` → `[0,1]` | `core/random.h:58-68` |
| `detail::isFinite(float)` / `isNaN` / `isInf` — fast-math-immune, read through `opaqueFloatBits` | `core/db_utils.h:118-123`, `:99-104`, `:260-264` |
| `detail::flushDenormal(float)` | `core/db_utils.h:245-247` |
| `dbToGain(float)` is `constexpr` | `core/db_utils.h:293` |
| `HarmonicCloud::processStereoBlock` guard ladder (null either channel ⇒ return; `numSamples == 0` ⇒ return; un-prepared ⇒ fill both with zeros and return) | `systems/harmonic_cloud.h:878-891` |
| `HarmonicCloud::setSpectralGravity` rejects non-finite, clamps `[-1,+1]`, `ratio_g(n) = pow(n, 1 + g·0.1)`, identity at `g == 0` | `harmonic_cloud.h:464-485` |
| shared control clock `kControlChunkSamples = 64` | `harmonic_cloud.h:144`; `continuous_body.h:97` + `static_assert` `:630`; `noise_organism.h:150` + `static_assert` `:158-160` |
| `NoiseOrganism`: `PrepareConfig` shape `:190-194`, `kGainRampMs = 50` `:178`, `kOutputClamp = 4` `:180`, `kDefaultWanderRateHz = 0.03` `:163`, absolute-grid `processBlock` `:414-443`, `gateSteady()` `:1669-1677`, `refreshGates()` (targets set **only from setters**) `:2174-2186`, `chainActive()` `:2192-2196`, `wanderScale()` `:2200-2202`, `advanceLanes()` `:2216-2240`, `laneValue()` `:2247-2249`, `sanitise()` `:1099-1106`, salt table + overflow `static_assert`s `:1018-1042`, clamp tail `:2598-2617`, `getAllocatedBytes()` `:999`, `setSourceDormant`/`setSourceWake` `:833`/`:844` | `systems/noise_organism.h` |
| the per-sample-ramp rule, stated in code: "The ramps advance PER SAMPLE and are never held across the control chunk: a 1.33 ms staircase … is not acceptable" | `noise_organism.h:1819-1821` |
| `NoiseOrganism` writes `setFrequency` then `setDecay` at configuration time and `setFrequency`→`setQ`→`setGain` on every control step | `noise_organism.h:1983-1988`, `:2286-2289` |
| `processSympatheticBankSIMD(float* y1s, float* y2s, const float* coeffs, const float* rSquareds, const float* gains, int count, float scaledInput, float* sums, float releaseCoeff, float* envelopes) noexcept` — declaration only, implemented in a Highway TU; `sums` is a **single accumulator**; per-resonator output is read back from `y1s[i]`; `gains` multiplies the **input** inside the recurrence | `systems/sympathetic_resonance_simd.h:39-50`, doc `:25-37` |
| `SlowEventScheduler::kMaxTargets = 16`, `kNoTarget = 0xFF`, default interval 20–90 s, default envelope 5/3/8 s, `Event{target, depth, polarity}` | `processors/slow_event_scheduler.h:150,152,164-168,187-191` |
| ring-out precedent: drive 60 s, stop, require tail `< 1.0e-4` (−80 dBFS) inside a bound **computed from shipped constants** | `dsp/tests/unit/systems/continuous_body_test.cpp:3361-3386` |
| perf idiom: ns per 512-sample block @48 kHz, `bestTrialNs` best-of-25 × 500 blocks after 400 warm-up, ceiling `static_assert(kBaseline * 1.5 <= kBudgetNs)` **and** anti-no-op floor `static_assert(kBaseline >= kBudgetNs / 50.0)`; the stop-and-surface rule | `noise_organism_perf_test.cpp:195-300`, `:1500-1575`, rule at `:44-58` |
| measured stage cost: `ResonatorBank` prepared at 48 kHz, **3 resonators enabled** at 70/140/260 Hz, `processBlock(buf, 512)` = **8 091.6 ns/block** | `noise_organism_perf_test.cpp:83-86` (T002 table, transcribed at `:1520`) |
| the Welch-on-an-arbitrary-probe-grid bandwidth estimator and its `fitQFromBandRatio` bisection (SC-017's basis) | `noise_organism_test.cpp:1631-1728` |
| `render_fingerprint.h`: `kSampleTolerance = 5.0e-4f` `:58`, `kMetricTolerance = 2.5e-4` `:61`, `fingerprintRender(std::span<const float>)` `:72` | `tests/test_helpers/render_fingerprint.h` |
| `AllocationDetector` `:48` / `AllocationScope` `:111`; the overrides discard `size`, so byte accounting must be self-reported | `tests/test_helpers/allocation_detector.h` |
| `Krate::Test::AudioFeatures` (`peakDbfs`, `rmsDbfs`, `centroidHz`, 5 fixed bands `[20-100, 100-500, 500-2k, 2k-8k, 8k-Nyq]`) and `extractAudioFeatures(const std::vector<float>&, double)` | `tests/test_helpers/audio_features.h:23-37` |
| `computeMean` `:41`, `computeVariance` `:58`, `computeStdDev` `:76`, `computeMedian` `:90`, `computeMAD` `:112` | `tests/test_helpers/statistical_utils.h` |
| `dsp_systems_tests` source list is **enumerated**, not globbed; block runs `:306-402` | `dsp/tests/CMakeLists.txt` |
| the single `-fno-fast-math -fno-finite-math-only` block, GCC/Clang only, ending `:799-800` | `dsp/tests/CMakeLists.txt:770-800` |
| `dsp/lint_all_headers.cpp` is an **enumerated** include list; the Layer 3 block ends at `:178` (`systems/noise_organism.h`) | `dsp/lint_all_headers.cpp:154-178` |
| ODR sweep `grep -rn "ResonanceDriftNetwork" dsp/ plugins/ tools/` | run this session — **0 hits** |
| ODR sweep `grep -rn "clearAudioState" dsp/ plugins/` | run this session — **0 hits** |
| lint tooling present: `lint-odr.js`, `lint-layers.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `lint-simd-aligned-loadstore.js`, `check-portability.js` | `tools/` listing, this session |

---

## S1. Component: `ResonanceDriftNetwork`

### S1.1 Header, layer, includes

`dsp/include/krate/dsp/systems/resonance_drift_network.h`, **Layer 3 (Systems)**, header-only,
`namespace Krate::DSP`, banner `// Layer: 3 (Systems)` matching `systems/timevar_comb_bank.h:8`
(FR-001).

```cpp
#include <krate/dsp/core/db_utils.h>          // Layer 0: isFinite, flushDenormal, dbToGain
#include <krate/dsp/core/math_constants.h>    // Layer 0: kPi, kTwoPi
#include <krate/dsp/core/random.h>            // Layer 0: Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/smoother.h>    // Layer 1: OnePoleSmoother, LinearRamp
#include <krate/dsp/processors/brownian_drift.h>  // Layer 2
#include <krate/dsp/processors/resonator_bank.h>  // Layer 2

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
```

Every include reaches **downward only** (Layers 0–2), so `tools/lint-layers.js` passes without a
justification comment. The one same-layer include that FR-013 **Tier 2** would add
(`<krate/dsp/systems/sympathetic_resonance_simd.h>`) carries the `continuous_body.h:42` precedent
comment; the lint fails only on reaching *up*.

No `<vector>`, no `<memory>`: S11 establishes the component has **no heap term at all**.

### S1.2 Constants

```cpp
/// Roadmap line 219. 12 <= ResonatorBank::kMaxResonators = 16 (resonator_bank.h:39).
static constexpr std::size_t kMaxPeaks = 12;
static_assert(kMaxPeaks >= 1 && kMaxPeaks <= kMaxResonators,
              "a peak must map onto a ResonatorBank slot");

/// The shared library-wide control clock (harmonic_cloud.h:144, continuous_body.h:97,
/// noise_organism.h:150). A component that drifted off it would decorrelate the
/// per-voice modulation grid in Vorago Phase 10.
static constexpr std::size_t kControlChunkSamples = 64;
static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

/// FR-037. 17 * BrownianDrift::kTauMax = 510 s >= the 500 s FR-016 admits.
static constexpr std::size_t kMaxLaneDecimation = 17;
static_assert(static_cast<float>(kMaxLaneDecimation) * BrownianDrift::kTauMax >= 500.0f,
              "kMaxLaneDecimation must reach the slowest FR-016 wander rate");

static constexpr float kGainRampMs   = 50.0f;   ///< FR-041 (noise_organism.h:178)
static constexpr float kOutputClamp  = 4.0f;    ///< FR-018 (noise_organism.h:180)
static constexpr float kMixSmoothMs  = kResonatorSmoothingTimeMs;  ///< 20 ms (resonator_bank.h:69)

static constexpr float kDefaultWanderRateHz   = 0.03f;  ///< FR-034 (noise_organism.h:163)
static constexpr float kMinWanderRateHz       = 0.002f;
static constexpr float kMaxWanderRateHz       = 1.0f;
static constexpr float kDefaultFreqStepOctaves = 0.02f; ///< FR-035
static constexpr float kDefaultQStepOctaves    = 0.05f; ///< FR-035
static constexpr float kMinSlewOctaves         = 0.001f;
static constexpr float kMaxSlewOctaves         = 24.0f;

/// FR-045. PROVISIONAL until T14 measures it; the header comment next to this
/// constant records the measured wet-vs-drive gap, the method and the date.
static constexpr float kDefaultWetGainDb = 30.0f;
static constexpr float kMinWetGainDb     = -24.0f;
static constexpr float kMaxWetGainDb     = 48.0f;

static constexpr float kMinPeakLevelDb = -60.0f;   ///< FR-017
static constexpr float kMaxPeakLevelDb =  12.0f;
static constexpr float kMaxFreqWanderSemis = 24.0f;///< FR-030
static constexpr float kMaxQWanderOctaves  =  2.0f;///< FR-031
static constexpr float kMaxGainWanderDb    = 24.0f;///< FR-033
static constexpr float kMinRatio = 0.25f;          ///< FR-022
static constexpr float kMaxRatio = 64.0f;
static constexpr float kMinNoteHz = 8.0f;          ///< FR-022

/// FR-031's Q range in the log2 domain S6.4 works in. `std::log2` is NOT constexpr
/// in C++20: `static constexpr float k = std::log2(x);` compiles only as a
/// GCC/MSVC builtin extension and is REJECTED by Clang, so it would break the
/// macOS and Linux legs while a Windows build stayed green
/// (`feedback_msvc_leniency_breaks_ci.md`). The repo already carries this lesson
/// twice — `harmonic_cloud.h:253` ("detail::constexprExp rather than std::exp2,
/// which is not constexpr in C++20") and `entropy_processor.h:82-85` — and both
/// resolve it the same way, with `detail::constexprLn` (`core/db_utils.h:156`)
/// over `detail::kLn2` (`:144`). So does this.
static constexpr float kMinLog2Q = detail::constexprLn(kMinResonatorQ) / detail::kLn2;  // -3.321928
static constexpr float kMaxLog2Q = detail::constexprLn(kMaxResonatorQ) / detail::kLn2;  //  6.643856
static_assert(kMinLog2Q < kMaxLog2Q, "the FR-031 Q range must be ordered in log2");
// Pinned to the shipped Q bounds by a RUNTIME equivalence check in
// ResonanceDriftNetwork_ControlSurfaceClamps (the entropy_processor.h:82 idiom:
// "the value is pinned by a runtime equivalence test"), so the constexpr series
// and std::log2 cannot drift apart on any toolchain:
//   REQUIRE(kMinLog2Q == Approx(std::log2(kMinResonatorQ)).margin(1e-5));
//   REQUIRE(kMaxLog2Q == Approx(std::log2(kMaxResonatorQ)).margin(1e-5));

/// FR-042 / S7.2. `gateSteady()` snaps any wake at or below this to EXACTLY
/// `0.0f`. This is what keeps the sleep edge's exact `== 0.0f` test valid by
/// construction once Phase 10 writes `getEnvelopeValue() * getActiveDepth()`
/// (spec.md:645-646) into `setPeakWake`: a release tail that stops at 1e-8, or a
/// depth product that lands on a denormal, would otherwise leave `target != 0.0f`
/// forever, `engineActive` would never clear, FR-042's state clear would never
/// run, and SC-004 (c)'s CPU saving would never be realised in the driven case.
static constexpr float kWakeSilenceEpsilon = 1.0e-6f;

/// S2.1 step 1, spec correction C-9. The lowest sample rate `prepare` accepts.
/// NOT 1 Hz: `kMaxResonatorFrequencyRatio * 1 Hz = 0.45 Hz` is BELOW
/// `kMinResonatorFrequency = 20 Hz`, which inverts every clamp built from that
/// pair — and `std::clamp` has the precondition `!(hi < lo)`; violating it is
/// undefined behaviour, and MSVC's `<algorithm>` fires
/// `_STL_VERIFY("invalid bounds argument passed to std::clamp")`. The inversion is
/// not confined to this header: the shipped `ResonatorBank::clampFrequency` is a
/// bare `std::clamp(hz, kMinResonatorFrequency, 0.45f * fs)`
/// (`resonator_bank.h:542-545`), so at 1 Hz EVERY frequency write is UB inside a
/// component SC-013 forbids amending. 8000 Hz gives `0.45 * fs = 3600 Hz`, two
/// orders clear of the 20 Hz floor, and sits below any rate a host presents.
static constexpr double kMinUsableSampleRate = 8000.0;
static_assert(static_cast<double>(kMaxResonatorFrequencyRatio) * kMinUsableSampleRate >
                  static_cast<double>(kMinResonatorFrequency),
              "the frequency clamp range must stay ordered at the lowest accepted rate");
```

### S1.3 Public API — the surface to implement

Real signatures. Nested types first.

```cpp
/// FR-020. APPEND ONLY — these become a persisted plugin parameter at Phase 12.
/// Deliberately NOT named TuningMode: that name exists at namespace scope in
/// resonator_bank.h:133 and this header includes it (New-components table).
enum class AnchorMode : std::uint8_t { Free = 0, Keyed = 1, Hybrid = 2 };

/// FR-002. Callers MUST use designated initialisers — PrepareConfig{.numPeaks = 8} —
/// so no narrowing conversion hides in a positional brace init (Clang errors
/// where MSVC does not). Nested, following NoiseOrganism::PrepareConfig
/// (noise_organism.h:190) and AtmosphereEngine::PrepareConfig (atmosphere_engine.h:369).
struct PrepareConfig {
    std::size_t maxBlockSamples = 2048;      ///< Clamped [64, 8192]. See S11: sizes nothing.
    std::size_t numPeaks        = kMaxPeaks; ///< Clamped [1, kMaxPeaks].
};

// ---- lifecycle -------------------------------------------------------------
void prepare(double sampleRate, const PrepareConfig& config) noexcept;  // FR-002
void reset() noexcept;                                                  // FR-004
void clearAudioState() noexcept;                                        // FR-046
void setSeed(std::uint32_t seed) noexcept;                              // FR-005
void processBlock(const float* inL, const float* inR,
                  float* outL, float* outR, std::size_t numSamples) noexcept;  // FR-003

// ---- peaks and anchors -----------------------------------------------------
void setNumPeaks(std::size_t n) noexcept;                        // FR-010
void setAnchorMode(AnchorMode mode) noexcept;                    // FR-020
void setPeakAnchorHz(std::size_t peak, float hz) noexcept;       // FR-021
void setPeakRatio(std::size_t peak, float ratio) noexcept;       // FR-022
void setNoteFrequency(float hz) noexcept;                        // FR-022
void setGravity(float g) noexcept;                               // FR-023

// ---- per-peak base values and wander depths --------------------------------
void setPeakLevel(std::size_t peak, float dB) noexcept;          // FR-017
void setPeakQ(std::size_t peak, float q) noexcept;               // FR-031
void setFreqWander(std::size_t peak, float semitones) noexcept;  // FR-030
void setQWander(std::size_t peak, float octaves) noexcept;       // FR-031
void setGainWander(std::size_t peak, float dB) noexcept;         // FR-033
void setPeakPan(std::size_t peak, float position) noexcept;      // FR-038
void setPeakPanWander(std::size_t peak, float depth) noexcept;   // FR-038

// ---- network-wide motion controls ------------------------------------------
void setWanderRate(float hz) noexcept;                           // FR-034
void setWanderEnabled(bool enabled) noexcept;                    // FR-034
void setSlewCeilings(float freqOctavesPerStep,
                     float qOctavesPerStep) noexcept;            // FR-035

// ---- life cycle and output --------------------------------------------------
void setPeakWake(std::size_t peak, float amount) noexcept;       // FR-040, CLAMPED [0, 1]
void setPeakDormant(std::size_t peak, bool dormant) noexcept;    // FR-042
void setMix(float mix) noexcept;                                 // FR-043
void setWetGain(float dB) noexcept;                              // FR-045

// ---- configuration read surface (FR-051) -----------------------------------
[[nodiscard]] std::size_t getNumPeaks() const noexcept;
[[nodiscard]] AnchorMode  getAnchorMode() const noexcept;
[[nodiscard]] float getPeakAnchorHz(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakRatio(std::size_t peak) const noexcept;
[[nodiscard]] float getNoteFrequency() const noexcept;
[[nodiscard]] float getGravity() const noexcept;
[[nodiscard]] float getPeakLevel(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakQ(std::size_t peak) const noexcept;
[[nodiscard]] float getFreqWander(std::size_t peak) const noexcept;
[[nodiscard]] float getQWander(std::size_t peak) const noexcept;
[[nodiscard]] float getGainWander(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakPan(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakPanWander(std::size_t peak) const noexcept;
[[nodiscard]] float getWanderRate() const noexcept;
[[nodiscard]] float getFreqSlewCeiling() const noexcept;
[[nodiscard]] float getQSlewCeiling() const noexcept;
[[nodiscard]] bool  isWanderEnabled() const noexcept;
[[nodiscard]] bool  isPeakDormant(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakWakeAmount(std::size_t peak) const noexcept;
[[nodiscard]] float getMix() const noexcept;
[[nodiscard]] float getWetGain() const noexcept;

// ---- realised-state read surface (FR-052) ----------------------------------
[[nodiscard]] float getPeakCurrentFrequency(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakCurrentQ(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakCurrentGainDb(std::size_t peak) const noexcept;  // excludes the gate
[[nodiscard]] float getPeakCurrentPan(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakGate(std::size_t peak) const noexcept;
[[nodiscard]] float getPeakEquivalentRt60(std::size_t peak) const noexcept; // reported, never written
[[nodiscard]] std::size_t getNormalisationPeakCount() const noexcept;       // FR-019's N
[[nodiscard]] bool  isPeakEngineActive(std::size_t peak) const noexcept;
[[nodiscard]] std::size_t getLaneDecimation() const noexcept;
[[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept;
[[nodiscard]] std::size_t getAllocatedBytes() const noexcept;
[[nodiscard]] bool  isPrepared() const noexcept;
```

There is deliberately **no** `getActivePeakCount` (FR-019, D-11) and **no** `ModulationSource`
inheritance (Non-Goals). Copy and move are left implicitly defaulted: S11 establishes there is no
heap state to deep-copy, unlike `NoiseOrganism` which deletes copy for that reason
(`noise_organism.h:212-215`).

### S1.4 Out-of-range and non-finite argument contract — NORMATIVE

Reproduced verbatim in the header, next to the setters.

* **Out-of-range `peak`** (FR-050): every setter is a **silent no-op**
  (`resonator_bank.h:329` idiom); every getter returns the documented neutral and never indexes the
  array — `0.0f` for floats, `0` for sizes, `false` for bools, `AnchorMode::Free` for the mode.
* **Non-finite float argument** (FR-008): the setter is a **no-op and the previous value stands**.
  This differs deliberately from `NoiseOrganism::sanitise`, which substitutes a per-argument neutral
  (`noise_organism.h:1099-1101`); FR-008 specifies rejection, and SC-009 (b) asserts the *previous*
  value is still reported. The guard is one line at the top of every float setter:

  ```cpp
  if (!detail::isFinite(v)) return;   // never std::isnan (macOS CI is -ffast-math)
  ```

  This guard is load-bearing, not defensive hygiene. `std::clamp` does **not** reject NaN (with
  `v = NaN` both `v < lo` and `hi < v` are false, so `v` is returned unchanged), and the trace is
  fatal: `setPeakAnchorHz(i, NaN)` → anchor NaN → `exp2(depth·lane)·NaN` = NaN →
  `ResonatorBank::setFrequency` clamps with a bare `std::clamp` (`:544`) → `omega` NaN →
  `sin`/`cos` NaN → NaN coefficients, and `Biquad::process` resets only on a non-finite **input
  sample**, never on non-finite coefficients, so the resonator would emit NaN forever — through
  FR-018's clamp, which is itself a `std::clamp` and propagates NaN.
* **Out-of-range float argument**: every setter whose FR-016 row carries a range **clamps into that
  range** and the matching getter reports the clamped value. The list is exactly FR-016's table and
  it includes the two the first draft of this plan omitted from both the contract and S12.2's clamp
  case: **`setPeakWake` clamps `[0, 1]`** (FR-040 says the scalar is "in `[0, 1]`" and FR-041/FR-044
  state `gate[i][n] ∈ [0, 1]`) and **`setPeakPanWander` clamps `[0, 1]`** (spec.md:609). The wake
  bound is load-bearing, not cosmetic: S5.3's `wetL += y * p.panGainL` under
  `scale = (1/sqrt(numPeaks)) * dbToGain(wetGainDb_)` is the whole FR-019 headroom argument, and
  S5.3's exact-zero properties assume a non-negative gate. A Phase-8 caller driving wake from agent
  energy, or a Phase-10 caller writing `getEnvelopeValue() * getActiveDepth()`, that passed `1.5`
  would scale one peak 50 % past unity into the wet trim; a negative value would invert that peak's
  polarity against the other eleven, which no criterion measures. Clamping in the setter is the only
  place that cannot be bypassed:
  `peaks_[peak].wakeAmount = std::clamp(amount, 0.0f, 1.0f);`
* **Non-finite `sampleRate`** in `prepare`: substituted by `48000.0`, then floored at
  `kMinUsableSampleRate` (`noise_organism.h:227` shape; the floor VALUE is spec correction C-9, see
  S1.2 and S2.1 step 1 — a 1 Hz floor makes `std::clamp`'s bounds invert, which is UB).
* **Non-finite audio sample** (FR-009): replaced by `0.0f` per channel independently, **before**
  the mono sum, the banks or the dry path see it (S5).

### S1.5 State layout

```cpp
private:
    /// One peak. Nested POD-ish aggregate: `Peak` at namespace scope is NOT free
    /// (struct Peak exists file-locally in seraphis_macro_test.cpp:841 and
    /// macro_wiring_test.cpp:821); nesting removes the question entirely, the
    /// SlowEventScheduler::Event precedent (slow_event_scheduler.h:187-191).
    struct Peak {
        // ---- configuration (FR-016 defaults, restored only by prepare) -------
        float freeAnchorHz   = 40.0f;
        float ratio          = 0.5f;
        float levelDb        = -6.0f;
        float baseQ          = 12.0f;
        float freqWanderSemis= 3.0f;
        float qWanderOct     = 0.5f;
        float gainWanderDb   = 6.0f;
        float panPosition    = 0.0f;   ///< seeded one-shot draw at prepare/setSeed
        float panWanderDepth = 0.2f;
        float wakeAmount     = 1.0f;
        bool  dormant        = false;

        // ---- cached log-domain configuration (recomputed only on a setter) ---
        float anchorLog2Hz   = 0.0f;   ///< log2 of the mode-resolved, clamped anchor
        float freeLog2Hz     = 0.0f;   ///< log2(freeAnchorHz), for the Hybrid crossfade
        float baseLog2Q      = 0.0f;   ///< log2(baseQ)

        // ---- lanes (FR-005 salts; 4 per peak, 48 per network) ----------------
        BrownianDrift freqLane, qLane, gainLane, panLane;

        // ---- per-sample ramps (FR-041, FR-035) -------------------------------
        LinearRamp gate;               ///< configured kGainRampMs; target set ONLY from setters
        float      lastGateTarget = 0.0f;  ///< S7.3: re-target ONLY when this actually changes
        LinearRamp levelRamp;          ///< FR-017 base level as dbToGain(levelDb); setter-only target

        // ---- computed targets, maintained for EVERY peak incl. dormant -------
        float targetLog2Hz   = 0.0f;
        float targetLog2Q    = 0.0f;
        float targetGainDb   = 0.0f;
        float targetPan      = 0.0f;

        // ---- applied (slew-limited) state = the FR-052 read surface ---------
        float appliedLog2Hz  = 0.0f;
        float appliedLog2Q   = 0.0f;
        float appliedHz      = 0.0f;   ///< exp2(appliedLog2Hz), cached for the getter
        float appliedQ       = 0.0f;
        float appliedGainDb  = 0.0f;   ///< TOTAL dB = clamp(levelDb + wander); the FR-052 read
        float appliedBankGainDb = 0.0f;///< = appliedGainDb - levelDb; the only dB the bank is told
        float appliedPan     = 0.0f;
        float panGainL       = 0.70710678f;  ///< cos(theta), FR-038
        float panGainR       = 0.70710678f;  ///< sin(theta)

        // ---- change detection against the bank (FR-015) ---------------------
        float lastWrittenHz     = 0.0f;
        float lastWrittenQ      = 0.0f;
        float lastWrittenBankGainDb = 0.0f;

        // ---- engine ---------------------------------------------------------
        bool engineActive = false;     ///< FR-052's isPeakEngineActive
    };

    std::array<Peak, kMaxPeaks> peaks_{};

    // ---- the audio engine, behind the FR-013 seam (S9) ----------------------
    std::array<ResonatorBank, kMaxPeaks> banks_{};   // FR-011 default shape (Tier 0)

    // ---- network-level state -----------------------------------------------
    double        sampleRate_    = 48000.0;
    PrepareConfig config_{};
    bool          prepared_      = false;
    std::uint32_t seed_          = 0;
    AnchorMode    anchorMode_    = AnchorMode::Free;
    float         noteHz_        = 55.0f;
    float         gravity_       = 0.0f;
    float         wanderRateHz_  = kDefaultWanderRateHz;
    bool          wanderEnabled_ = true;
    float         freqSlewOct_   = kDefaultFreqStepOctaves;
    float         qSlewOct_      = kDefaultQStepOctaves;
    float         mix_           = 1.0f;
    float         wetGainDb_     = kDefaultWetGainDb;

    /// Cached Nyquist-derived clamps, recomputed in prepare only.
    float         minLog2Hz_ = 0.0f;   ///< log2(kMinResonatorFrequency)
    float         maxLog2Hz_ = 0.0f;   ///< log2(0.45 * fs)
    float         maxHz_     = 0.0f;

    /// FR-037. Per NETWORK, not per lane: a rate change costs one integer
    /// recompute and lane phase cannot drift apart.
    std::size_t   laneDecimation_ = 1;
    std::size_t   laneCounter_    = 0;

    /// FR-007's ABSOLUTE grid residue, carried across calls — deliberately NOT
    /// HarmonicCloud's block-relative chunking (noise_organism.h:426-431 records
    /// why: a 36+28 split must run ONE control step, not two).
    std::size_t   controlPhase_ = 0;

    bool          anchorsDirty_ = true;

    /// FR-019 + FR-045 folded into ONE per-sample ramp (S5.3).
    LinearRamp       wetScaleRamp_;
    OnePoleSmoother  mixSmoother_;

    std::uint32_t clampEngagements_ = 0;
    static constexpr std::uint32_t kMaxClampCount = 0xFFFFFFFFu;
```

Footprint: `ResonatorBank` is ≈ 1.1 KB (16 `Biquad`s + six 16-float arrays + three smoothers), so
Tier 0's twelve banks are ≈ 13 KB and the peak table ≈ 5 KB — ≈ 18 KB total, inside L1 on every
target. Tier 1 replaces the twelve banks with one, taking the total to ≈ 7 KB. **No heap** (S11).

---

## S2. Lifecycle

### S2.1 `prepare(double sampleRate, const PrepareConfig& config)` — FR-002

The only method allowed to allocate, and it allocates nothing (S11). Order is load-bearing.

1. `sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));`
   **The floor is 8 kHz, not 1 Hz — spec correction C-9.** spec.md:1502 names "a sample rate below
   1 Hz" as floored at 1 Hz, "which keeps every derived coefficient finite". It does not: at
   `fs = 1` the derived pair `[kMinResonatorFrequency, kMaxResonatorFrequencyRatio * fs]` is
   `[20, 0.45]`, i.e. **inverted**, and `std::clamp` with `hi < lo` is undefined behaviour (MSVC
   fires `_STL_VERIFY("invalid bounds argument passed to std::clamp")`). Three clamps in this plan
   are built from that pair (S4's anchor clamp, S6.4's frequency map, `setPeakAnchorHz`), and the
   shipped `ResonatorBank::clampFrequency` already carries the same latent inversion
   (`resonator_bank.h:542-545`) in a component SC-013 forbids amending. Flooring at
   `kMinUsableSampleRate = 8000.0` keeps the range ordered by construction at every accepted rate.
2. `config_.maxBlockSamples = clamp(config.maxBlockSamples, 64, 8192);`
   `config_.numPeaks = clamp(config.numPeaks, 1, kMaxPeaks);`
3. `minLog2Hz_ = std::log2(kMinResonatorFrequency);`
   `maxHz_ = std::max(kMinResonatorFrequency, float(sampleRate_) * kMaxResonatorFrequencyRatio);`
   `maxLog2Hz_ = std::max(minLog2Hz_, std::log2(maxHz_));`
   Step 1's floor already makes both `std::max` calls no-ops at every accepted rate; they are belt
   and braces, written so that the ordering `std::clamp` requires is visible at the point of
   definition rather than inferred from a constant three pages away (C-9).
4. `for (auto& b : banks_) b.prepare(sampleRate_);` — then pin the three globals **once**, for the
   life of the object (FR-016): `b.setDamping(0.0f); b.setExciterMix(0.0f); b.setSpectralTilt(0.0f);`
   Each is already the constructed default (`resonator_bank.h:625-627`) and `prepare` snaps the
   smoothers to them (`:194-196`); the explicit writes are documentation that survives a future
   change of those defaults, and they cost three stores at prepare time.
5. `for each peak: freqLane.prepare(sampleRate_); qLane…; gainLane…; panLane…;`
6. `applyDefaults()` — restores the entire FR-016 table (S3). **`prepare` is the only path back to
   the defaults**; `reset()` is configuration-preserving (FR-004).
7. `prepared_ = true;` — set **before** step 8 so the configuration push is not a no-op.
8. `setWanderRate(kDefaultWanderRateHz)` — the single owner of `laneDecimation_` and of every lane's
   `setSmoothness` (S7.2). Never recomputed anywhere else, so `prepare` and a later caller cannot
   disagree about the mapping.
9. `mixSmoother_.configure(kMixSmoothMs, fs); mixSmoother_.snapTo(mix_);`
   `wetScaleRamp_.configure(kGainRampMs, fs); wetScaleRamp_.snapTo(wetScaleTarget());`
10. `for each peak: gate.configure(kGainRampMs, fs); gate.snapTo(gateSteady(i));`
    `p.lastGateTarget = gateSteady(i);` — S7.3's change-detection shadow must start in sync, or the
    first `refreshGates()` would skip a real re-target.
    `levelRamp.configure(kGainRampMs, fs); levelRamp.snapTo(dbToGain(p.levelDb));` — the FR-035
    base-level ramp (S6.6, spec correction C-6).
11. `controlPhase_ = 0; laneCounter_ = 0; clampEngagements_ = 0; anchorsDirty_ = true;`
12. `setSeed(seed_)` — **last**, for the same reason `NoiseOrganism` does it last
    (`noise_organism.h:299-302`): it calls `lane.reset()`, which re-seeds the RNG and snaps the
    output smoother, so a seed distributed earlier would be discarded by step 5's `prepare`.
    It also draws the FR-038 default pan positions (S2.4).
13. `snapControlState()` — one full control pass that recomputes anchors and targets, sets
    `applied = target` for every peak with **no** slew limiting, writes the FR-014 triple into every
    awake peak's bank, sets `lastWritten*` to the written values, and calls
    `bank.setEnabled(0, engineActive)`. Without this the first control step would slew from
    `appliedLog2Hz = 0` (1 Hz) toward the anchor at 0.02 octaves/step, taking ~250 control steps to
    arrive, and the very first render would be silent because no bank slot is enabled yet.

Re-preparing a live object is legal and fully re-initialises (Edge Cases). Lane seeds survive because
step 12 re-applies `seed_`.

### S2.2 `reset()` — configuration-preserving (FR-004, Q5)

Identical in meaning to `NoiseOrganism::reset()` (`noise_organism.h:310-382`), so Phase 10 can call
`reset()` on both siblings at the same moments.

1. `controlPhase_ = 0; laneCounter_ = 0; clampEngagements_ = 0; anchorsDirty_ = true;`
2. `for (auto& b : banks_) b.reset();` — **and this is why step 5 is mandatory**:
   `ResonatorBank::reset()` is a configuration wipe leaving every slot at 440 Hz, default Q and
   `enabled_[i] = false` (`resonator_bank.h:225-231`), so a forwarded `reset()` without a re-apply
   renders **digital silence**. SC-007 (a) is the arm that catches exactly that. It also re-zeroes
   `damping_`/`exciterMix_`/`spectralTilt_` (`:234-236`) — which are the values FR-016 pins anyway,
   so nothing is lost there; step 5 re-pins them regardless for the same documentation reason as
   S2.1 step 4.
3. `for each peak: freqLane.reset(); qLane.reset(); gainLane.reset(); panLane.reset();` —
   `BrownianDrift::reset()` re-seeds from `configuredSeed_` and rewinds `x_` to `mean_`
   (`brownian_drift.h:242-247`), which is what makes SC-006 (d) hold.
4. `mixSmoother_.configure(kMixSmoothMs, fs); mixSmoother_.snapTo(mix_);`
   `wetScaleRamp_.configure(kGainRampMs, fs); wetScaleRamp_.snapTo(wetScaleTarget());`
   `for each peak: gate.configure(kGainRampMs, fs); gate.snapTo(gateSteady(i));`
   `p.lastGateTarget = gateSteady(i);`
   `levelRamp.configure(kGainRampMs, fs); levelRamp.snapTo(dbToGain(p.levelDb));`
   `snapTo`, not `setTarget` — a reset is a state clear, not a fade.
5. `snapControlState()` — the mandatory re-apply, identical to S2.1 step 13.

`reset()` does **not** redraw the seeded pan positions: `getPeakPan` is a configuration getter and
SC-007 (c) requires every configuration getter to survive. A user's `setPeakPan` therefore survives
`reset()`, and SC-006 (d) still holds because with no setter called since `prepare` the positions
are already the seeded ones.

### S2.3 `clearAudioState()` — FR-046, the lighter sibling (Q5)

Exactly steps 1, 2, 4 and 5 of S2.2, **omitting step 3**. The four lanes per peak keep freewheeling
from wherever they were. That single omission is the whole difference between the two methods, and
SC-022 (b)/(c) is what proves the difference is real rather than two names for one effect.

This phase adds the method and calls it from nowhere — matching the Non-Goals' "nothing drives it
yet". Phase 10 is its consumer.

### S2.4 `setSeed(std::uint32_t seed)` and the salt table — FR-005

```cpp
// APPEND ONLY. Renumbering a base silently changes every Phase-3 render, because
// each lane's stream is deriveStreamSeed(seed_, base + peak) (core/random.h:102).
static constexpr std::size_t kSaltFreqLane    =  0;   // + peak
static constexpr std::size_t kSaltQLane       = 16;   // + peak
static constexpr std::size_t kSaltGainLane    = 32;   // + peak
static constexpr std::size_t kSaltPanLane     = 48;   // + peak
static constexpr std::size_t kSaltPanPosition = 64;   // one-shot draw, not a lane
static constexpr std::size_t kSaltNextFree    = 80;

static_assert(kSaltFreqLane    + kMaxPeaks <= kSaltQLane,       "freq salts overlap the Q block");
static_assert(kSaltQLane       + kMaxPeaks <= kSaltGainLane,    "Q salts overlap the gain block");
static_assert(kSaltGainLane    + kMaxPeaks <= kSaltPanLane,     "gain salts overlap the pan block");
static_assert(kSaltPanLane     + kMaxPeaks <= kSaltPanPosition, "pan-lane salts overlap the position block");
static_assert(kSaltPanPosition + kMaxPeaks <= kSaltNextFree,    "salt table overflow");
```

Body, per peak `i`:

```cpp
p.freqLane.setSeed(deriveStreamSeed(seed_, kSaltFreqLane + i));  p.freqLane.reset();
p.qLane   .setSeed(deriveStreamSeed(seed_, kSaltQLane    + i));  p.qLane.reset();
p.gainLane.setSeed(deriveStreamSeed(seed_, kSaltGainLane + i));  p.gainLane.reset();
p.panLane .setSeed(deriveStreamSeed(seed_, kSaltPanLane  + i));  p.panLane.reset();
```

The `reset()` after each `setSeed` is **mandatory and load-bearing**: `BrownianDrift::setSeed`
re-seeds the RNG but leaves `x_` and the output smoother where they were
(`brownian_drift.h:145-148`), so without it a late `setSeed` would change the *increments* but not
the *position*, and the Edge Cases clause "re-seeds every lane and rewinds their streams" would be
half-true. `reset()` calls `initState()`, which re-seeds again from `configuredSeed_` — the same
value just written — so the two calls compose correctly.

**Default pan positions** (FR-038), drawn in the same call from a dedicated one-shot generator:

```cpp
Xorshift32 panRng{deriveStreamSeed(seed_, kSaltPanPosition)};
for (std::size_t i = 0; i < kMaxPeaks; ++i) {
    // Deterministic alternating spread: peaks alternate sides with increasing
    // |pan|, so the LOWEST peaks sit near centre (mono-compatible bass, which a
    // subterranean instrument needs) and the highest sit widest, and adjacent
    // peaks never share a side — pitch is decorrelated from position.
    const float side  = ((i % 2u) == 0u) ? 1.0f : -1.0f;
    const float step  = static_cast<float>((i / 2u) + 1u) / (static_cast<float>(kMaxPeaks) * 0.5f);
    const float base  = side * step;                       // ±1/6 … ±6/6
    const float jitter = panRng.nextFloat() * (1.0f / static_cast<float>(kMaxPeaks));
    peaks_[i].panPosition = std::clamp(base + jitter, -1.0f, 1.0f);
}
```

Deterministic under seed (SC-021 (b)), spread across the full range out of the box (FR-016), and
never collapsed to centre. **Documented consequence, stated in the header:** `setSeed` overwrites
`setPeakPan`, so a caller that wants both must call `setSeed` first. That is the literal reading of
FR-005/FR-038 ("drawn once, at `prepare`/`setSeed`"); an "explicit override" flag was rejected as
speculative state this phase has no requirement for.

A `seed` of 0 is legal: `deriveStreamSeed` substitutes `0x2545F491u` for a zero hash and
`Xorshift32::seed` substitutes its own default for 0 (`core/random.h:110`, `:73-74`), so no lane can
collapse onto a degenerate stream.

---

## S3. FR-016 normative default table → `applyDefaults()`

```cpp
static constexpr std::array<float, kMaxPeaks> kDefaultAnchorHz{
    40.0f, 55.0f, 75.0f, 103.0f, 141.0f, 193.0f,
    265.0f, 363.0f, 497.0f, 681.0f, 933.0f, 1278.0f};   // geometric, ratio ~1.37

/// Harmonic-ISH: integers detuned by up to ±0.8 % so a keyed patch does not
/// collapse onto an exact harmonic comb. NORMATIVE — used verbatim, never
/// regenerated to fit a tighter prose bound (SC-014 (b)/(d)/(e) assert against
/// exactly these twelve numbers).
static constexpr std::array<float, kMaxPeaks> kDefaultRatio{
    0.5f, 1.0f, 1.5f, 2.0f, 2.98f, 4.0f,
    5.04f, 6.0f, 7.02f, 8.0f, 9.98f, 12.0f};
```

`applyDefaults()` writes, for every peak: `freeAnchorHz = kDefaultAnchorHz[i]`,
`ratio = kDefaultRatio[i]`, `levelDb = -6`, `baseQ = 12`, `freqWanderSemis = 3`, `qWanderOct = 0.5`,
`gainWanderDb = 6`, `panWanderDepth = 0.2`, `wakeAmount = 1`, `dormant = false`; and network-wide
`anchorMode_ = Free`, `noteHz_ = 55`, `gravity_ = 0`, `wanderEnabled_ = true`,
`freqSlewOct_ = 0.02`, `qSlewOct_ = 0.05`, `mix_ = 1`, `wetGainDb_ = kDefaultWetGainDb`.
`panPosition` is **not** written here — S2.4's seeded draw owns it, and step 12 runs after step 6.

`PrepareConfig` fields are excluded: `prepare` has already clamped them from the caller's request
(the `noise_organism.h:1895-1900` rule).

---

## S4. Anchors and the three modes — exact math (FR-020 – FR-025)

Recomputed on the control grid, **only when `anchorsDirty_`** is set by `setAnchorMode`,
`setPeakAnchorHz`, `setPeakRatio`, `setNoteFrequency` or `setGravity` (FR-024). Cost is then exactly
`kMaxPeaks` evaluations, each with a bounded, configuration-independent cost — no search, no
grid, no bracketing, no extension rule (all deleted by Clarifications Q1).

```cpp
void recomputeAnchors() noexcept {
    const float noteLog2 = std::log2(noteHz_);
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        Peak& p = peaks_[i];
        float log2Hz;
        switch (anchorMode_) {
        case AnchorMode::Free:
            log2Hz = p.freeLog2Hz;                       // cached by setPeakAnchorHz
            break;
        case AnchorMode::Keyed:
            log2Hz = noteLog2 + std::log2(p.ratio);      // = log2(noteHz * ratio[i])
            break;
        case AnchorMode::Hybrid: {
            if (gravity_ == 0.0f) {
                // FR-023 identity, and Edge Cases' "g = 0 must be BIT-EQUAL to
                // Free mode, which forbids an implementation that always runs
                // the log/exp round trip". This branch is that prohibition.
                log2Hz = p.freeLog2Hz;
            } else {
                const float keyedLog2 = noteLog2 + std::log2(p.ratio);
                log2Hz = p.freeLog2Hz + gravity_ * (keyedLog2 - p.freeLog2Hz);
            }
            break;
        }
        }
        p.anchorLog2Hz = std::clamp(log2Hz, minLog2Hz_, maxLog2Hz_);   // FR-025
    }
    anchorsDirty_ = false;
}
```

Everything is done in **log2**, which is the same closed form FR-023 writes in natural logs
(`log f = log f_free + g·(log f_keyed − log f_free)` is base-invariant), and it removes the `exp`/
`log` round trip that a linear-domain implementation would need:

* `g = 0` ⇒ exactly `f_free[i]`, **bit-equal** to Free mode — taken by the short-circuit branch, not
  by arithmetic that happens to be exact;
* `g = 1` ⇒ exactly `noteHz × ratio[i]`, i.e. peak *i*'s own keyed anchor — and because every peak
  pulls toward its **own** ratio, the twelve results are the twelve distinct keyed frequencies, never
  collapsed onto a shared grid (the defect the superseded nearest-neighbour law produced);
* `g = −1` ⇒ `2·log2(f_free) − log2(f_keyed)`, the per-peak log-mirror;
* every intermediate `g` is monotone in `g` for a fixed pair — no branch on which side of anything
  the free anchor falls.

`std::log2(p.ratio)` could be cached per peak (`setPeakRatio` writes it), and **is**: `ratioLog2` is
added to `Peak` alongside `freeLog2Hz` and `baseLog2Q`. That makes a Hybrid recomputation cost one
`std::log2` for `noteHz_` plus twelve fused multiply-adds and twelve clamps — cheaper than FR-024's
own stated bound of twelve log/exp pairs, and the clamp is the only per-peak branch.

Every clamp in this section — and the one in S6.4's frequency map, and `setPeakAnchorHz`'s — reads
`minLog2Hz_`/`maxLog2Hz_` or `[kMinResonatorFrequency, maxHz_]`, and **relies on S2.1 step 1's
`kMinUsableSampleRate` floor plus step 3's explicit ordering for `std::clamp`'s `!(hi < lo)`
precondition** (C-9). `ResonanceDriftNetwork_ControlSurfaceClamps` asserts the ordering rather than
assuming it, by calling `prepare(0.0, cfg)` and `prepare(1.0, cfg)` and then driving
`setPeakAnchorHz` and one control step through both.

**Free-anchor caching:** `setPeakAnchorHz` sanitises, clamps to `[kMinResonatorFrequency, maxHz_]`,
stores `freeAnchorHz`, computes `freeLog2Hz = std::log2(freeAnchorHz)`, sets `anchorsDirty_ = true`.
Clamping is silent — no engagement counter — because it is a legitimate configuration outcome at low
sample rates (FR-025).

---

## S5. Render contract

### S5.1 `processBlock` and the absolute control grid (FR-003, FR-007)

```cpp
void processBlock(const float* inL, const float* inR,
                  float* outL, float* outR, std::size_t numSamples) noexcept {
    // Guard ladder, in order (harmonic_cloud.h:880-891, noise_organism.h:414-430).
    if (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) {
        return;                       // nothing written on EITHER channel, NOTHING advanced
    }
    if (numSamples == 0) {
        return;                       // no control step consumed, buffers untouched
    }
    if (!prepared_) {
        std::fill_n(outL, numSamples, 0.0f);
        std::fill_n(outR, numSamples, 0.0f);
        return;                       // ...and no state advance
    }

    std::size_t done = 0;
    while (done < numSamples) {
        if (controlPhase_ == 0) {
            updateControl();
        }
        const std::size_t chunk =
            std::min(numSamples - done, kControlChunkSamples - controlPhase_);
        renderChunk(inL + done, inR + done, outL + done, outR + done, chunk);
        controlPhase_  = (controlPhase_ + chunk) % kControlChunkSamples;
        done          += chunk;
    }
}
```

`controlPhase_` is a **residue carried across calls**, deliberately not `HarmonicCloud`'s
block-relative chunking: that runs two control steps for a 36 + 28 split where an unsplit 64 runs
one, and SC-010 requires `max|diff| <= kSampleTolerance` across an irregular partition. The
`noise_organism.h:426-431` comment records the same finding.

Any `numSamples` is legal, including values far above `config_.maxBlockSamples` — the loop is
chunked by the control grid, not by a buffer length, and there is no buffer (S11).

### S5.2 Aliasing and the in-place contract (FR-003, SC-019 (a))

The render is **per sample with local dry capture**, so every aliasing case FR-003 admits is safe
without a scratch buffer:

```
for each sample s:
    dryL = sanitise(inL[s]);  dryR = sanitise(inR[s]);   // BOTH captured first
    ... compute ...
    outL[s] = ...;  outR[s] = ...;                        // written after
```

`inL == outL` and `inR == outR` are safe because index `s` of both inputs is read before either
output is written. The cross-aliased case `inL == outR`, `inR == outL` is safe for the same reason:
writing `outR[s]` clobbers `inL[s]`, but that value was already consumed this iteration and is never
read again. This is what makes S11's "no heap term" possible, and it is the reason the plan does
**not** carry the spec's dry-path scratch buffer (S14, C-1).

### S5.3 `renderChunk` — the canonical FR-044 signal path in code

```cpp
void renderChunk(const float* inL, const float* inR,
                 float* outL, float* outR, std::size_t n) noexcept {
    for (std::size_t s = 0; s < n; ++s) {
        // ---- (0) sanitise, per channel independently (FR-009) --------------
        const float dryL = detail::isFinite(inL[s]) ? inL[s] : 0.0f;
        const float dryR = detail::isFinite(inR[s]) ? inR[s] : 0.0f;
        const float x    = 0.5f * (dryL + dryR);           // the mono engine input (FR-011)

        // ---- (1)+(2) gated mono peak, equal-power panned into the stereo sum -
        float wetL = 0.0f;
        float wetR = 0.0f;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            Peak& p = peaks_[i];
            // ALWAYS advanced, for EVERY peak, dormant and out-of-count included:
            // the ramps must be a pure function of the sample count however the
            // caller partitions its blocks (SC-010), and a LinearRamp already at
            // its target early-outs (smoother.h:372-374). This is the
            // noise_organism.h:1819-1821 rule.
            const float gate      = p.gate.process();
            const float levelGain = p.levelRamp.process();  // FR-017 base level, C-6
            if (!p.engineActive) {
                continue;                                   // FR-042: bank not called at all
            }
            // FR-044 step 1, with the base level factored OUT of the bank's dB
            // write and into this per-sample linear ramp (S6.6, C-6). The bank
            // carries only appliedBankGainDb = appliedGainDb - levelDb, so the
            // product is dbToGain(appliedGainDb) EXACTLY, unchanged from FR-017.
            const float y = bankProcess(i, x) * gate * levelGain;   // S9's seam
            wetL += y * p.panGainL;
            wetR += y * p.panGainR;
        }

        // ---- (3)+(4) normalise and trim: ONE scalar, both channels ----------
        const float scale = wetScaleRamp_.process();        // (1/sqrt(numPeaks)) * wetGainLinear

        // ---- (5) crossfade against the stereo dry path (FR-043) -------------
        const float m = mixSmoother_.process();
        float l = (1.0f - m) * dryL + m * (wetL * scale);
        float r = (1.0f - m) * dryR + m * (wetR * scale);

        // ---- (6) final clamp, per channel, ONE shared counter (FR-018) ------
        outL[s] = detail::flushDenormal(clampCount(l));
        outR[s] = detail::flushDenormal(clampCount(r));
    }
}
```

`clampCount` clamps to `±kOutputClamp` and increments a local engagement counter folded into
`clampEngagements_` with saturation at the end of the chunk (`noise_organism.h:2611-2616` idiom) —
never wrapping, so a pathological render cannot report zero.

**Why steps 3–4 use one ramp.** FR-019 requires the `1/sqrt(numPeaks)` factor to be ramped over
`kGainRampMs` on a `setNumPeaks` change, and FR-045's trim is a static scalar with no ramp of its
own — but a mid-render `setWetGain` would then step. Folding both into one `LinearRamp` whose target
is `(1.0f / std::sqrt(float(config_.numPeaks))) * dbToGain(wetGainDb_)` satisfies FR-019 exactly,
makes `setWetGain` click-free for free, and costs one ramp instead of two. SC-015 (g)'s
`20·log10(sqrt(12/4)) = 4.77 dB` assertion is unaffected (it is measured on a settled window), and
SC-020 (c)'s expected-dB-difference assertion likewise.

**Exact-zero properties this composition guarantees, and which criteria need them:**

* `gate == 0.0f` makes a peak's contribution **exactly** `0.0f` by multiplication — never a dB path,
  whose `-60 dB` floor (FR-033's clamp) is `1.0e-3` of full scale, i.e. audible bleed. SC-015 (a).
* Every peak dormant and `mix = 1` ⇒ `wetL = wetR = 0.0f` exactly; `scale` is finite because
  `numPeaks >= 1` always (D-11); `m` reaches **exactly** `1.0f` because `OnePoleSmoother::process`
  snaps `current_ = target_` inside `kCompletionThreshold` (`smoother.h:199-202`); so
  `outL = 0·dryL + 1·0 = 0.0f`. SC-015 (c), SC-004 (c).
* `mix = 0` ⇒ `m` reaches exactly `0.0f`, so `outL = dryL` and `outR = dryR` **bit-exactly**, better
  than the `kSampleTolerance` SC-015 (c) allows.

### S5.4 RT safety (FR-006)

Every method `noexcept`. No allocation anywhere (S11). No lock, no exception, no I/O, no
`std::function`, no virtual dispatch on the per-sample path — `BrownianDrift::getCurrentValue()` is
the base class's virtual, so it is read through the **concrete** type via a
`laneValue(const BrownianDrift&)` helper (`noise_organism.h:2244-2249` precedent), never through a
`ModulationSource&`. Per-sample transcendentals: **none**. `std::exp2`/`std::log2` appear only on the
control grid; `std::sin`/`std::cos` appear only inside `ResonatorBank::setFrequency`/`setQ`, which
change detection suppresses when nothing moved (FR-015).

---

## S6. Wander lanes — exact math (FR-030 – FR-039)

### S6.1 What each lane is

Four `BrownianDrift` instances per peak — 48 per network. Each is a discrete Ornstein–Uhlenbeck
process with the **exact** AR(1) discretisation (not forward Euler), already implemented and
verified: `a = exp(−dt/τ)`, `g = kInternalStd·sqrt(1 − a²)`, `X ← μ + a(X − μ) + g·Z`
(`brownian_drift.h:19-38`, `:230-240`, `:253-270`), with `dt = kControlRateInterval / fs = 32/fs`,
a hard `±kWalkLimit = 4` divergence guard, a denormal floor, and an output clamped to `[−1, +1]`
through a 150 ms one-pole (`:212-214`). This phase writes **no** stochastic math of its own; it maps
a bounded `[−1, +1]` lane value into a bounded parameter range.

`laneValue(lane) = std::clamp(sanitise(lane.getCurrentValue(), 0.0f), -1.0f, 1.0f)` — guarded even
though it is bounded by construction (FR-008).

`wanderScale() = wanderEnabled_ ? 1.0f : 0.0f` multiplies every **depth**, exactly as
`NoiseOrganism::wanderScale()` does (`noise_organism.h:2200-2202`); the lanes themselves keep
advancing, so re-enabling never jumps (FR-034's "freezes … without rewinding it"). See S14, C-3.

### S6.2 `setWanderRate(float hz)` and FR-037's decimation — the joint mapping

```cpp
void setWanderRate(float hz) noexcept {
    if (!detail::isFinite(hz)) return;
    wanderRateHz_ = std::clamp(hz, kMinWanderRateHz, kMaxWanderRateHz);   // [0.002, 1.0]

    const float requestedTau = 1.0f / wanderRateHz_;                      // T in [1, 500] s
    const std::size_t newDecimation = static_cast<std::size_t>(std::clamp(
        std::ceil(requestedTau / BrownianDrift::kTauMax), 1.0f,
        static_cast<float>(kMaxLaneDecimation)));
    const float tau = requestedTau / static_cast<float>(newDecimation);   // lands in [0.2, 30]
    const float smoothness = std::clamp(
        (tau - BrownianDrift::kTauMin) /
        (BrownianDrift::kTauMax - BrownianDrift::kTauMin), 0.0f, 1.0f);

    for (Peak& p : peaks_) {
        p.freqLane.setSmoothness(smoothness);
        p.qLane   .setSmoothness(smoothness);
        p.gainLane.setSmoothness(smoothness);
        p.panLane .setSmoothness(smoothness);
    }
    // Rebase rather than restart: a mid-render rate change must not force an
    // immediate extra advance (which would be a control-grid discontinuity) nor
    // skip one. The residue is well defined for any old/new pair.
    laneCounter_    = laneCounter_ % newDecimation;
    laneDecimation_ = newDecimation;
}
```

Worked values, checked against the shipped constants:

| requested rate | `T = 1/rate` | `decimation` | `tau = T/dec` | `smoothness` | realised `tau·dec` |
|---|---|---|---|---|---|
| 1.0 Hz (ceiling) | 1.0 s | 1 | 1.0 s | 0.0268 | 1.0 s |
| 0.0333 Hz (`1/kTauMax`) | 30.0 s | 1 | 30.0 s | 1.0 | 30 s |
| **0.03 Hz (default)** | 33.33 s | **2** | 16.67 s | 0.552 | 33.3 s |
| 0.005 Hz (SC-003 (f) slow arm) | 200 s | 7 | 28.57 s | 0.952 | 200 s |
| 0.002 Hz (floor) | 500 s | 17 | 29.41 s | 0.980 | 500 s |

`tau` can never fall below `kTauMin = 0.2` because `T >= 1.0` by the rate clamp, so `smoothness` is
never pinned at 0 by the mapping itself. **Without the decimation the entire sub-range
`[0.002, 0.0333]` Hz — including this spec's own 0.03 Hz default — would be a dead zone** in which
`setWanderRate` did nothing while `getWanderRate` kept reporting the requested number
(`brownian_drift.h:99`, `:152-156`). SC-003 (f) is the only criterion that detects that, which is
why it exists.

### S6.3 Lane advance (FR-036, FR-037)

At the top of `updateControl()`, before anything reads a lane:

```cpp
if (laneCounter_ == 0) {
    for (Peak& p : peaks_) {
        p.freqLane.processBlock(kControlChunkSamples);
        p.qLane   .processBlock(kControlChunkSamples);
        p.gainLane.processBlock(kControlChunkSamples);
        p.panLane .processBlock(kControlChunkSamples);
    }
}
laneCounter_ = (laneCounter_ + 1u) % laneDecimation_;
```

Unconditional by contract: a lane whose depth is 0 still advances (so raising the depth resumes the
trajectory an always-on lane would have been on); a **dormant** peak's lanes still advance (the
Dormancy rule's "modulation lanes keep running", roadmap lines 490–491 — SC-015 (b) is the arm); and
`setWanderEnabled(false)` scales the depths, it does not freeze the motion.

The fixed `kControlChunkSamples` argument is what makes decimation sample-rate independent: between
advances the lane's value is **held**, so it experiences `64/fs` seconds of its own evolution per
`decimation·64/fs` seconds elapsed, and the realised correlation time is `tau·decimation` with `fs`
cancelling (SC-008 (c)). `BrownianDrift::processBlock` is bit-identical to N `process()` calls
(`brownian_drift.h:190-206`), so it is also partition-invariant — but it is called only in exact
64-sample units on the absolute grid regardless, so SC-010 does not depend on that property.

A decimated lane holds its output for at most `kMaxLaneDecimation · 64 = 1088` samples (22.7 ms at
48 kHz), and the value it then moves to differs by at most one `kTauMin`-scale OU increment seen
through a 150 ms output smoother that advanced only 64 samples — orders of magnitude below
`freqSlewCeiling`. SC-002 renders at `wanderRate = 1.0 Hz`, where `decimation == 1`, so the
**un-decimated** path is the one the zipper criterion stresses.

### S6.4 The four maps, all in the log2 domain

Computed for **every** peak on every control step, dormant included (FR-042, Q6):

```cpp
const float ws = wanderScale();

// FREQUENCY (FR-030): f = anchor * 2^(depthSemitones * lane / 12)
p.targetLog2Hz = std::clamp(
    p.anchorLog2Hz + (ws * p.freqWanderSemis * laneValue(p.freqLane)) * (1.0f / 12.0f),
    minLog2Hz_, maxLog2Hz_);

// Q (FR-031): Q = baseQ * 2^(amount * lane), multiplicative
p.targetLog2Q = std::clamp(
    p.baseLog2Q + ws * p.qWanderOct * laneValue(p.qLane),
    kMinLog2Q, kMaxLog2Q);            // S1.2, via detail::constexprLn (NOT std::log2)

// GAIN (FR-033): additive in dB. targetGainDb is the TOTAL - base + wander,
// jointly clamped exactly as FR-017 states - and stays the FR-052 read surface.
p.targetGainDb = std::clamp(
    p.levelDb + ws * p.gainWanderDb * laneValue(p.gainLane),
    kMinPeakLevelDb, kMaxPeakLevelDb);

// PAN (FR-038)
p.targetPan = std::clamp(
    p.panPosition + ws * p.panWanderDepth * laneValue(p.panLane), -1.0f, 1.0f);
```

Working in log2 throughout is not a micro-optimisation, it is what makes FR-035's slew ceiling
expressible without a transcendental: the ceiling is stated in **octaves per control step**, i.e. a
difference in `log2`, so the limiter is a `std::clamp` and the only `exp2` in the whole control step
is the one that produces the Hz the bank must be told. It also makes FR-030's bound provable by
inspection — `|targetLog2Hz − anchorLog2Hz| <= depthSemitones/12` because `laneValue ∈ [−1, +1]` —
which is exactly what SC-003 (c) asserts.

Because the depths are **per peak** and the rate is **network-wide** (FR-034, D-17), the decimation
counter can ride the absolute control grid as one integer instead of twelve, and lane phase cannot
drift apart.

### S6.5 Slew limiting (FR-035) and the one exemption

```cpp
if (p.engineActive) {
    p.appliedLog2Hz = std::clamp(p.targetLog2Hz,
                                 p.appliedLog2Hz - freqSlewOct_,
                                 p.appliedLog2Hz + freqSlewOct_);
    p.appliedLog2Q  = std::clamp(p.targetLog2Q,
                                 p.appliedLog2Q  - qSlewOct_,
                                 p.appliedLog2Q  + qSlewOct_);
} else {
    // FR-042's dormant interval. A dormant peak's gate is exactly 0, so there is
    // nothing audible to protect and nothing to slew toward: applied TRACKS
    // target directly for the WHOLE interval, not only at the wake edge. This is
    // what makes FR-042's "written in full on that control step regardless of how
    // far it moved during the dormant interval" true, and what SC-015 (f)'s
    // extended arm reads. It is a DEVIATION from FR-035's "one exemption, and
    // only one" and from FR-052's "exactly as an awake peak" - recorded as spec
    // correction C-8, not left implicit.
    p.appliedLog2Hz = p.targetLog2Hz;
    p.appliedLog2Q  = p.targetLog2Q;
}
p.appliedHz     = std::exp2(p.appliedLog2Hz);
p.appliedQ      = std::exp2(p.appliedLog2Q);
p.appliedGainDb = p.targetGainDb;      // FR-035: gain needs no slew (S6.6)
// The bank is told ONLY the wander part. The base level rides p.levelRamp per
// sample instead (S5.3, S6.6, C-6), and dbToGain(bankDb) * dbToGain(levelDb) ==
// dbToGain(appliedGainDb) exactly, so FR-017's composition is unchanged.
p.appliedBankGainDb = p.appliedGainDb - p.levelDb;
p.appliedPan    = p.targetPan;
const float theta = (p.appliedPan + 1.0f) * (kPi * 0.25f);   // [0, pi/2]
p.panGainL = std::cos(theta);
p.panGainR = std::sin(theta);          // gainL^2 + gainR^2 == 1 for every pan (SC-021 (a))
```

Two `exp2`, one `sin` and one `cos` per peak per control step — 48 transcendentals per 64 samples,
i.e. 0.75 per sample-equivalent, and none on the per-sample path.

**The dormant bypass is a recorded deviation, and it has a criterion.** FR-035 grants exactly one
exemption from the ceiling ("the FR-042 wake-edge snap") and FR-052 says a dormant peak's reported
applied values "move exactly as they would for an awake peak", whose are slew-limited. The `else`
branch above is wider than either: it runs unslewed for the entire dormant interval. It is kept —
rather than narrowed to the wake edge — because FR-042's own normative sentence requires it ("written
in full on that control step **regardless of how far it moved during the dormant interval**"), which
a slew-limited dormant peak cannot satisfy: after a mid-dormancy `setNoteFrequency` octave jump it
would still be 50 control steps from target at the wake edge. The two designs are observationally
identical under drift (drift never reaches the default ceiling, S6.5 above), so the distinguishing
arm is stated in SC-015 (f): after a mid-dormancy anchor jump, `getPeakCurrentFrequency(i)` reflects
the new anchor within **one** control step, not fifty. Recorded as spec correction **C-8** (S14),
which amends FR-035's exemption count and FR-052's wording rather than leaving the plan silently
wider than the FR it cites.

At the defaults, `freqSlewCeiling = 0.02` octaves/step is ≈ 24 cents per 64 samples ≈ 18 octaves/s at
48 kHz — **far above any in-spec drift**, so at the default it never shapes normal motion. The
limiter exists for discontinuous inputs: a `setNoteFrequency` jump, an anchor-mode switch, a gravity
sweep. SC-018 is the only criterion that can fail on a missing, inverted or mis-signed limiter, and
it measures the step count (`1.0 / 0.02 = 50 ± 2` control steps for a one-octave move) rather than
asserting a property every implementation with a limiter satisfies.

Both ceilings are a **real control surface**, not a test backdoor (D-13): `setSlewCeilings` clamps
each to `[0.001, 24]` octaves per control step, and SC-002 (c)'s injection arm reaches its
discontinuity through `setSlewCeilings(24, 24)` — an in-spec setting — with **no `#ifdef` hook and
no edit to the header under test**.

### S6.6 Gain: the FR-035 base-level ramp, and the two control-rate writes that remain

FR-035 exempts gain from the slew ceiling and justifies the exemption on the ground that gain "is
ramped per sample by the FR-041 gate and the **FR-017 base-level ramp**" (spec.md:560-561).
**FR-017 defines no such ramp**, and the first draft of this plan built none: base + wander dB went
straight to `ResonatorBank::setGain`, which writes `gains_[index] = dbToGain(dB)`
(`resonator_bank.h:367-371`) consumed at `:504` by a bare `filterOutput *= gains_[i]` with **no
smoother of any kind**. A mid-render `setPeakLevel(i, -60 → +12)` would therefore step that peak's
gain by **72 dB in one sample**, with nothing anywhere to smooth it, and FR-035's stated
justification would be a citation of a mechanism that does not exist.

**The mechanism is built, not the citation deleted (spec correction C-6).** The shipped sibling this
component copies already has exactly it: `Slot::levelRamp`, a `kGainRampMs = 50 ms` `LinearRamp`
documented as "the SOLE owner of the user's slot level" (`noise_organism.h:1189`, targeted at
`:1826`, advanced per sample at `:502-507`). Phase 3 takes the same shape:

* `Peak::levelRamp` is a `LinearRamp` configured at `kGainRampMs`, whose target is
  `dbToGain(p.levelDb)` and is set **only from `setPeakLevel`** (plus a `snapTo` in `prepare`,
  `reset` and `clearAudioState`). Setter-only targeting makes it immune to R3's re-target trap by
  the same construction as the gate.
* It advances **per sample, for every peak, dormant and out-of-count included**, beside the gate in
  S5.3 — before the `engineActive` early-out, so SC-010's block-size invariance holds.
* The bank is told only `appliedBankGainDb = appliedGainDb − levelDb` (S6.5). Since
  `dbToGain(appliedBankGainDb) · dbToGain(levelDb) == dbToGain(appliedGainDb)`, FR-017's composition
  and FR-052's `getPeakCurrentGainDb` (still the jointly-clamped total) are **unchanged**.

SC-002 (d) gains an arm for it: a mid-render `setPeakLevel(i, −60 → +12)` on a steady sine must
produce a per-cycle envelope that rises over 50 ms ± 5 ms, with no per-sample step in the ramp above
`1.05 · dbToGain(12) / (kGainRampMs · 0.001 · fs)`. Without the ramp that render steps in one sample.

**Two control-rate writes remain, and both are bounded here.** After C-6 the component still writes
two amplitude quantities on the control grid with neither a slew ceiling nor a per-sample ramp — the
first draft of this section named only one of them, and R13 repeated the omission.

1. **The bank's wander-offset gain** (`appliedBankGainDb`, through `ResonatorBank::setGain`). The
   gain lane's own 150 ms output smoother inside `BrownianDrift` (`kDriftOutputSmoothMs`) limits
   `laneValue` to a per-sample change of ≈ `1.39e-3` of the `[−1,+1]` span at 48 kHz — the proof is
   written out at `brownian_drift.h:41-59` and holds **regardless of how large a step the OU takes**,
   because the smoother never jumps. Across one 64-sample control chunk the lane moves at most
   `64 × 1.39e-3 ≈ 0.089` of its span, so at the maximum `gainWanderDb = 24` the write steps by at
   most **≈ 2.1 dB per control chunk**, and by orders of magnitude less in practice because the
   smoother is chasing a target that itself moved by one OU increment. That worst case is reachable
   only immediately after a `setGainWander` jump from 0 to 24 dB with the lane already at an extreme.
   *Residual introduced by C-6:* a `setPeakLevel` jump moves `appliedBankGainDb` only when the joint
   `[−60, +12]` clamp is engaged at one end, and then by at most `gainWanderDb`. At the FR-016
   defaults (`levelDb = −6`, `gainWanderDb = 6`) the sum lives in `[−12, 0]`, far inside the clamp,
   so the default patch produces **no** bank step from a level move at all.
2. **The equal-power pan pair** (`panGainL`/`panGainR`). These are recomputed only in the control
   step (S6.5, `theta = (appliedPan + 1)·π/4`) and applied per sample in S5.3 as bare multiplies
   `wetL += y · panGainL`. This is the network's **own** code, not the bank's, and it has no ramp
   either. Same derivation: the pan lane moves ≤ 0.089 of its span per 64-sample chunk, so at
   `panWanderDepth = 1` the applied pan moves ≤ 0.089 per chunk, `theta` by ≤ 0.070 rad, and
   `panGainL` by up to ≈ **0.07 linear (≈ 0.6 dB) every 1.33 ms** — a smaller step than term 1's,
   but the same kind, and it must be attributed to pan rather than to gain if SC-002 (a) goes red.

**Why no further ramp for either.** Both bounds sit inside the range SC-002 (a) is measured over, and
the per-sample FR-041 gate (S7) plus the C-6 level ramp protect every discontinuous case — wake,
sleep, `setNumPeaks`, `setPeakLevel`. So the plan **records both bounds** and hands them to
**SC-002 (a)**, whose `B/P` statistic is computed with **all four lanes at maximum depth** and
therefore measures both terms together. If SC-002 (a) goes red, the response is FR-060's
stop-and-surface with the measured attribution **naming which of the two terms dominates** — never a
quiet threshold relaxation, and never a silent depth cap (R13).

---

## S7. Peak life cycle (FR-040 – FR-042)

### S7.1 The gate target, and the `LinearRamp` re-target trap

```cpp
[[nodiscard]] float gateSteady(std::size_t i) const noexcept {
    if (i >= kMaxPeaks) return 0.0f;
    const Peak& p = peaks_[i];
    if (p.dormant || i >= config_.numPeaks) return 0.0f;
    // Snap a vanishing wake to EXACTLY zero. setPeakWake already clamped to
    // [0, 1] (S1.4), but FR-040's Phase-10 caller writes
    // getEnvelopeValue() * getActiveDepth() (spec.md:645-646): a release tail
    // that stops at 1e-8, or a depth product that lands on a denormal, is a
    // legal argument that would leave the target != 0.0f forever. S7.2's sleep
    // edge tests for exact zero, so without this snap engineActive would never
    // clear on the ONE path this component exists to serve: the bank would keep
    // being processed every sample, FR-042's state clear would never run (the
    // Q = 100 stored ring never cleared), and SC-004 (c)'s CPU saving would
    // never be realised in the driven case. Snapping at the SOURCE keeps the
    // edge test valid by construction instead of loosening it to an epsilon.
    return (p.wakeAmount <= kWakeSilenceEpsilon) ? 0.0f : p.wakeAmount;
}
```

Same shape as `NoiseOrganism::gateSteady` (`noise_organism.h:1669-1677`): a peak dropped by
`setNumPeaks` is silenced exactly like a dormant one.

**`gate.setTarget()` is called ONLY from setters** — `setPeakWake`, `setPeakDormant`, `setNumPeaks`,
`prepare`, `reset`, `clearAudioState` — through one `refreshGates()` helper, and **never from
`updateControl()`**. This is the single most easily-broken detail in the component:
`LinearRamp::setTarget` recomputes `increment_ = (target − current) / (rampMs·0.001·fs)` on **every**
call (`smoother.h:353`), so re-targeting the same value every control step would restart the 50 ms
clock every 1.33 ms and turn the linear ramp into an asymptotic one that never arrives —
`getPeakGate` would creep toward 1 over hundreds of milliseconds instead of 50 ms ± 5 ms.
`NoiseOrganism` avoids it the same way (`refreshGates()` is reachable only from `setNumSources`,
`setSourceDormant` and `setSourceWake`). SC-002 (d) is the arm that catches it.

**The same trap, one level deeper: `refreshGates()` must not re-target a peak whose target did not
move.** Restricting `setTarget` to setters is necessary but not sufficient. `refreshGates()` walks
**all twelve** peaks, and FR-040 documents the Phase-10 usage as the scheduler calling
`setPeakWake(getActiveTarget(), env·depth)` **once per block**. At 512-sample blocks that is one
`refreshGates()` every 512 samples, and both calls it makes on each peak restart the clock:
`LinearRamp::setTarget` recomputes `increment_ = (target − current)/(rampMs·0.001·fs)` on every call
(`smoother.h:342-354`) **and so does `configure`** when a transition is in flight (`:328-336`). A peak
that is mid-ramp and whose own target has not changed would therefore have its remaining travel
stretched over a fresh 50 ms every block: the remaining distance would shrink by only
`1 − 512/2400 = 0.787` per block, and a 0 → 1 wake would reach 90 % in ≈ **107 ms** and 99 % in
≈ **213 ms** against FR-041's 50 ms ± 5 ms. That is R3 re-entered through `refreshGates` rather than
through `updateControl`, and **no criterion as first written could see it**: SC-002 (d) ramped one
peak with one setter call, and SC-015 (g)'s only gate assertion is an *upper* bound on the per-sample
step, which re-targeting always **lowers**. The fix is the per-peak `lastGateTarget` shadow in S7.3
plus a new SC-002 (d) arm that calls `setPeakWake` on a **different** peak once per block throughout
the measured ramp and still requires 50 ms ± 5 ms.

The gate ramp's other property is what FR-041 actually promises: a caller that jumps wake 0 → 1 in
one call gets a 50 ms ± 5 ms 0 → 100 % ramp; a caller that walks it over 40 s gets a 40 s fade,
because the target simply tracks. The roadmap's "gain → 0 and back over tens of seconds" (line 223)
is the *caller's* trajectory; the 50 ms ramp is the guard that makes any trajectory click-free.

Worst-case per-sample step: `1.0 / (kGainRampMs · 0.001 · fs)` = `4.1667e-4` at 48 kHz, which is
below SC-002 (d)'s bound of `1.05 / (kGainRampMs · 0.001 · fs)` = `4.375e-4` with the criterion's
own 5 % margin. This step size is reachable **only** because FR-011 gives each peak its own gated
output; a shared bank's finest gate step would be one control chunk, `64/2400 = 0.0267`, **61×**
coarser (D-10).

### S7.2 The sleep edge (FR-042)

Evaluated in `updateControl()`, before the bank writes:

```cpp
if (p.engineActive && p.gate.isComplete() && p.gate.getCurrentValue() == 0.0f) {
    // Exact comparison is correct BY CONSTRUCTION, not by luck: gateSteady()
    // returns a literal 0.0f for a dormant, out-of-count OR vanishing wake
    // (S7.1's kWakeSilenceEpsilon snap is what makes the third case true), and
    // LinearRamp lands exactly on its target (smoother.h:379-383). The same
    // exact test the shipped sibling uses (noise_organism.h:2194-2195).
    bankReset(i);                                            // STATE CLEAR ONLY (S9.1)
    bankApply(i, p.appliedHz, p.appliedQ, p.appliedBankGainDb);  // MANDATORY re-apply, FR-014 order
    bankSetEnabled(i, false);
    p.engineActive = false;
}
```

`bankReset(i)` is a **pure state clear and nothing else** (S9.1/S9.2): the FR-014-order re-apply and
the disable are the caller's, written explicitly above. An earlier draft folded them into
`bankReset` as well, which ran both twice per sleep edge — two redundant
`updateFilterCoefficients` calls (each a `sin`, a `cos` and a divide, `resonator_bank.h:560-592`)
plus a redundant `updateActiveCount()` sweep — and left the seam's contract ambiguous for whoever
implements the Tier-1 variant, in the one place FR-013 requires the substitution to be a
one-function change.

Three things happen here and each is load-bearing:

1. **The filter state is cleared, not frozen.** Without it the biquad would hold whatever energy it
   had — at `peakQ = 100` and a 40 Hz anchor that is `RT60 = Q·ln1000/(π·f) = 5.5 s` of stored ring —
   and would release it at full amplitude, seconds after the 50 ms gate ramp had finished
   attenuating it, into coefficients that had drifted for the whole dormant interval. SC-015 (f) is
   the arm; it goes red without this.
2. **The re-apply is mandatory**, for exactly the FR-004 reason: `ResonatorBank::reset()` is a
   configuration wipe (`:225-231`). `reset()` is also the **only** public path to a state clear —
   there is no state-only method — which is why it is used here at all, and it is cheap precisely
   because FR-011 gives each peak its own bank (Tier 0) or because Tier 1's per-slot clear is one
   `Biquad::reset()` (S9).
3. **`bank.process` stops being called at all.** The bank's own `if (!enabled_[i]) continue` skip
   (`:487-488`) is only half the saving: an entered-but-idle bank still runs its three global
   smoothers per sample (`:474-476`). Not calling it is what earns SC-004 (c)'s ≥ 40 % arm.

Detecting the edge at the control step means the bank runs for at most 63 samples at `gate == 0`
after the ramp lands. Those samples contribute exactly `0.0f` (S5.3), so the delay is inaudible; it
costs at most 1.3 ms of one bank's work per sleep event, against a 20–90 s event schedule.

### S7.3 The wake edge (FR-042, FR-035's exemption)

Handled **in the setter**, not at the next control step:

```cpp
void refreshGates() noexcept {
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        Peak& p = peaks_[i];
        const float target = gateSteady(i);
        if (!p.engineActive && target != 0.0f) {
            // WAKE EDGE. Snap the stored applied values into the bank in ONE
            // write, in the FR-014 order, exempt from FR-035's limiter. The
            // exemption is inaudible BY CONSTRUCTION: the ramp has not lifted
            // off, so the gate is provably still exactly 0.0f at this instant.
            bankApply(i, p.appliedHz, p.appliedQ, p.appliedBankGainDb);
            bankSetEnabled(i, true);
            p.engineActive = true;
        }
        // RE-TARGET ONLY ON A REAL CHANGE. Both calls below restart the 50 ms
        // clock (setTarget always, smoother.h:342-354; configure whenever a
        // transition is in flight, :328-336), so touching a peak whose target
        // did not move would stretch its in-flight ramp by the caller's block
        // period - once per block from FR-040's scheduler is enough to turn
        // FR-041's 50 ms ramp into a ~213 ms asymptote for every OTHER peak
        // (S7.1). The shadow is exact-compared because gateSteady() returns
        // either a literal 0.0f or the value setPeakWake stored verbatim, so
        // "unchanged" is bit-identical and no epsilon is needed.
        if (target != p.lastGateTarget) {
            p.gate.configure(kGainRampMs, static_cast<float>(sampleRate_));
            p.gate.setTarget(target);
            p.lastGateTarget = target;
        }
    }
}
```

The wake-edge test above is deliberately **outside** the change-detection guard. It costs one bool
and one float compare per peak per call, and keeping it unconditional means a peak can never be left
with `engineActive == false` while its gate target is non-zero — the state that would produce a
silent peak with a rising gate. `prepare`, `reset` and `clearAudioState` seed `lastGateTarget` from
`gateSteady(i)` at the same moment they `snapTo` the gate (S2.1 step 10, S2.2 step 4), so the shadow
can never start out of sync.

Doing it in the setter rather than at the next control step matters: a setter called mid-chunk starts
the ramp moving on the *next sample*, and if `engineActive` were still false the peak's first up to
63 samples would be silent while the gate was already non-zero — a ≤ 1.3 ms attack notch at gate
values under 2.6 %. Inaudible, but it would falsify FR-042's "the first audible sample is already
filtered", and SC-015 (f)'s extended arm reads the bank's state on exactly that edge. The snap is
RT-safe: fixed-size, no allocation, once per wake.

`p.appliedHz`/`appliedQ`/`appliedGainDb` are already the live drifted values because S6.5 keeps
`applied = target` for every peak while `!engineActive`, and S6.4 keeps computing targets for every
peak including dormant ones. That is also what makes FR-052's read surface report a dormant peak's
values as if it were awake (SC-015 (b)).

### S7.4 What a dormant peak does and does not do

| | dormant peak | awake peak |
|---|---|---|
| its four wander lanes advance | **yes** (FR-036) | yes |
| its anchor is recomputed | **yes** (FR-024) | yes |
| targets computed and stored | **yes** (Q6) | yes |
| `applied` tracks target | **yes, unslewed** (S6.5) | slew-limited |
| FR-052 getters report live values | **yes** | yes |
| `setFrequency`/`setQ`/`setGain` reach the bank | **no** (Q6) | yes, change-detected |
| `bank.process` is called | **no** (FR-042) | yes |
| gate ramp advances per sample | **yes** (SC-010) | yes |
| base-level ramp advances per sample (C-6) | **yes** (SC-010) | yes |

`setPeakDormant(i, true)` and `setPeakWake(i, 0.0f)` are **behaviourally identical** and differ only
on the read surface (`isPeakDormant` / `getPeakWakeAmount`) — the cross-cutting Dormancy rule, and
SC-015 (d) is the arm. Never writing the bank while dormant is the **one designed CPU difference
between the two Vorago siblings** (`NoiseOrganism` keeps writing its resonator lanes while dormant);
D-16 records it so a later reader does not "fix" the asymmetry.

---

## S8. The control-step bank writes (FR-014, FR-015) — the silent-failure site

For every peak with `engineActive`, in `updateControl()`, after S6.5:

```cpp
// Frequency and Q are ONE INDIVISIBLE WRITE PAIR whose change detection is the
// OR of the two comparisons — the form FR-015 states it prefers.
const bool freqOrQMoved = (p.appliedHz != p.lastWrittenHz) ||
                          (p.appliedQ  != p.lastWrittenQ);
if (freqOrQMoved) {
    bankSetFrequency(i, p.appliedHz);   // FIRST
    bankSetQ(i, p.appliedQ);            // ALWAYS immediately after — never skipped
    p.lastWrittenHz = p.appliedHz;
    p.lastWrittenQ  = p.appliedQ;
}
// The bank is told the WANDER-ONLY dB (S6.5, S6.6, C-6). The base level rides
// Peak::levelRamp per sample instead, so a setPeakLevel jump does not reach the
// bank as a step at all unless the joint [-60, +12] clamp is engaged.
if (p.appliedBankGainDb != p.lastWrittenBankGainDb) {
    bankSetGain(i, p.appliedBankGainDb);
    p.lastWrittenBankGainDb = p.appliedBankGainDb;
}
// ResonatorBank::setDecay is NEVER called by this component (FR-014, FR-032).
```

**Why the order is normative, and why Q is exempt from change detection.**
`ResonatorBank::setFrequency` re-derives Q from the stored decay —
`qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])` (`resonator_bank.h:333`) — off a
decay table this network **never writes**, so `decays_[i]` stands at `kDefaultDecayTime = 1.0 s`
(`:60`, written at `:201` and `:227`) forever. A frequency write after a Q write therefore silently
discards the Q, and skipping an "unchanged" Q write after a frequency write leaves the filter at
`rt60ToQ(f, 1.0) = 0.4548·f` — **Q ≈ 18 at 40 Hz instead of the configured 12, and clamped to 100
above ≈ 220 Hz** — permanently, for every peak.

This failure is *silent*: the audio still sounds plausible, and `getPeakCurrentQ(i)` still reports
the intended number because FR-052 makes it the network's own applied value, not a read of the
filter. Every Q criterion that reads that echo — SC-003 (c), SC-014 — would pass. SC-001 (d)'s
ring-out is an upper bound and passes with a *shorter* decay. **SC-017 is the only criterion that can
fail on it**, which is why it measures the realised −3 dB bandwidth from the audio and carries a
mandatory injection arm (D-9).

Cost: `setFrequency` costs a `sin`, a `cos`, a divide and a coefficient store (`:572-591`); `setQ`
costs the same recompute (`:387`). With wander disabled (S6.1's `wanderScale() == 0`) the targets are
constant and neither fires, so the network approaches zero control-rate cost — which is exactly what
SC-004 (b)'s "≥ 10 % below reference" arm measures. A change-detection path that saves nothing is not
implemented.

---

## S9. The audio engine and the FR-013 seam

### S9.1 The seam

Every bank interaction in S5–S8 goes through six private one-line inlines:

```cpp
float bankProcess(std::size_t i, float x) noexcept;
void  bankSetFrequency(std::size_t i, float hz) noexcept;
void  bankSetQ(std::size_t i, float q) noexcept;
void  bankSetGain(std::size_t i, float dB) noexcept;
void  bankSetEnabled(std::size_t i, bool on) noexcept;
void  bankReset(std::size_t i) noexcept;      // STATE CLEAR ONLY; caller owns the re-apply
```

`bankReset(i)` **clears slot `i`'s filter state and nothing else.** The caller owns the FR-014-order
re-apply and the `setEnabled` write, and S7.2 spells that sequence out as the single normative form.
Keeping the seam's contract this narrow is what makes OQ-1's Tier-1 companion
(`resetResonatorState(std::size_t)`) a **drop-in** for `bankReset` rather than a partial one — and it
is why the caller, not the seam, decides what a cleared slot is re-configured to.

Under **Tier 0** (FR-011, the spec's default shape) they forward to `banks_[i]` slot `0`. Under
**Tier 1** they forward to one shared `bank_` slot `i`. Nothing else in the component changes. This
is not speculative abstraction: FR-013 makes the substitution a pre-approved, probe-gated outcome, so
the seam is the cheapest way to keep the substitution a one-file, one-function change with the
public surface and every other FR untouched.

### S9.2 Tier 0 — twelve single-resonator `ResonatorBank` instances (FR-011)

`banks_[i]`, slot `0` and only slot `0` configured and enabled; slots `[1, 16)` stay permanently
disabled and cost one predicted branch each per sample (`:487-488`). `bankProcess(i, x)` is
`banks_[i].process(x)`, taken **individually**, multiplied by that peak's per-sample gate, and only
then summed — which is precisely what a shared bank cannot give, because `process(float)` returns
only `wetSum` and no per-resonator accessor exists anywhere in the class (verified by sweeping every
write to `filterOutput`, `:498-510`).

`bankReset(i)` is **exactly** `banks_[i].reset()` — nothing else. Note the Tier-0 quirk this exposes:
`ResonatorBank::reset()` is a state clear *and* a configuration wipe (`resonator_bank.h:213-245`), so
the caller's mandatory re-apply is restoring configuration as well as re-arming the slot. That is
already S7.2's explicit sequence (`bankReset` → `bankApply` → `bankSetEnabled(false)`), and stating
the seam this way means Tier 1 substitutes `filters_[i].reset()` for the same call without the
caller changing.

### S9.3 The measured cost problem, stated before the probe runs

The one directly comparable shipped measurement is `ResonatorBank` prepared at 48 kHz with **3**
resonators enabled, `processBlock(buf, 512)` = **8 091.6 ns/block**
(`noise_organism_perf_test.cpp:83-86`, transcribed at `:1520`) — i.e. **15.8 ns per `process()`
call**. Decomposing that as `F + 3B` (fixed per-call overhead `F` = three global smoothers plus the
16-slot skip loop plus the exciter-mix tail; `B` ≈ one `Biquad::process` plus three multiplies plus
the tilt early-return) with a plausible `B ≈ 2.5 ns` gives `F ≈ 8.3 ns`.

| shape | calls per 512-block | projected ns/block | vs SC-004's 80 000 ns | vs FR-013's 48 000 ns gate |
|---|---|---|---|---|
| **Tier 0**: 12 banks × 1 enabled slot | 12 × 512 = 6 144 | `6144 × (8.3 + 2.5)` ≈ **66 400** | 83 % | **1.38× over** |
| **Tier 1**: 1 bank × 12 enabled slots | 512 | `512 × (8.3 + 12 × 2.5)` ≈ **19 600** | 25 % | clear |

Tier 0 pays the 8.3 ns fixed overhead **twelve times per sample** instead of once; that term, not the
biquad math, is the whole difference (3.4×). Adding the other stages — 48 lanes at the default
`decimation = 2` (S10), the control writes, and the per-sample gate/pan/mix arithmetic — puts Tier 0
at roughly **95 000–100 000 ns/block, over the SC-004 ceiling**, and Tier 1 at roughly **45 000 ns,
at 56 % of it**.

**This projection does not decide anything, and it is not the trigger.** FR-013's trigger is stated
on **one number only** — probe stage (a)'s twelve-single-resonator-bank **engine** figure
(spec.md:292-296): "If it measures the scalar engine … at 48 000 ns/block or less … FR-011 stands
exactly as written and **nothing** in this FR is exercised." The total-projection-vs-80 000 ns
comparison is a **different** question with a **different** escalation (S10.5), and the two must not
be conflated: an engine at 60 000 ns inside a 75 000 ns total still mandates Tier 1, and an engine at
40 000 ns inside an 85 000 ns total still forbids exercising Tier 1 — the gap must be closed
elsewhere. With the projection above (Tier 0 engine ≈ 66 400 ns) FR-013's gate is already tripped
**irrespective of the total**.

The projection is recorded here so the implementation order (S15) puts the probe first and budgets
time for the Tier-1 branch rather than discovering it at the end — the same reason the Phase 2 probe
existed before `NoiseOrganism` did.

### S9.3b MEASURED — the probe ran, and Tier 1 IS TAKEN (FR-013's mandatory record)

FR-013 makes one thing mandatory when a tier is exercised: *"the substitution is recorded in
`plan.md` **with the measured figures**."* This subsection is that record. Everything above it in S9.3
is the pre-build **projection** and decides nothing; everything here is measurement.

**Probe:** `ResonanceDriftNetwork_StageCostProbe` (`[.perf]`, FR-060), 48 kHz, 512-sample blocks,
best-of-25 × 500 blocks after 400 warm-up, run in isolation. Measured 2026-09-11 on the Windows
x64 Release build (the run this record is transcribed from), and independently reproduced twice
during the compliance pass at 74 507.6 and 80 998.0 ns/block for stage (a1) — all three agree on the
verdict by a wide margin.

| stage | measured (ns/block) | % of one core |
|---|---|---|
| **(a1) engine, Tier 0: 12 × single-resonator `ResonatorBank`** | **71 712.8** | 0.672 % |
| (a2) engine, Tier 1: 1 × 12-resonator bank, raw | 17 461.6 | 0.164 % |
| (a2) engine, Tier 1: net of the shared 512-float refill | 17 452.8 | 0.164 % |
| (b) 48 lanes, decimation 1 (rate 1.0 Hz) | 11 968.4 | 0.112 % |
| (b) 48 lanes, decimation 2 (the FR-016 default rate) | 5 458.8 | 0.051 % |
| (b) 48 lanes, decimation 17 (rate 0.002 Hz) | 704.8 | 0.007 % |
| (b) 12 pan lanes ISOLATED, decimation 2 (FR-038's own term) | 1 375.6 | 0.013 % |
| (c) control writes, no change detection | 2 873.6 | 0.027 % |
| (c) control writes, FR-015 OR-pair (freq+Q always together) | 1 814.6 | 0.017 % |
| (c) control writes, static (FR-034 off) | 219.0 | 0.002 % |
| (d) FR-044 per-sample tail (no banks) | 13 892.6 | 0.130 % |
| (e) FR-042 sleep edge | 49.0 **ns per edge** | amortised ~0 |

**Gate 1 — FR-013's trigger, on stage (a1) alone (spec.md:292-296).** 71 712.8 ns/block against the
48 000 ns/block trigger: **exceeded by 49 %, so Tier 1 is taken.** The projection in S9.3 had put
Tier 0 at ≈ 66 400 ns; the measurement is 8 % worse than that, and the direction is the same. Tier 2
(`processSympatheticBankSIMD`, S9.5) is **not** exercised: Tier 1 alone puts the engine at 17 452.8
ns, a factor of 4.1 below Tier 0 and comfortably inside the budget, and FR-013 orders Tier 2 behind
Tier 1 for exactly this outcome.

**Gate 2 — the SC-004 projection on the total, recomputed on the (a2) net figure.**
17 452.8 (engine) + 5 458.8 (lanes at the default decimation) + 1 814.6 (control writes) + 13 892.6
(per-sample tail) = **38 618.8 ns/block against the 80 000 ns ceiling — 48 % of it, with 41 381.2 ns
of headroom.** No escalation is triggered on this path.

**What was actually added, and under which authority.** Two methods on `ResonatorBank`, both purely
additive:

| method | site | authority |
|---|---|---|
| `void processIndividual(float in, float* outPerResonator) noexcept` | `resonator_bank.h:528` | FR-013 Tier 1's own example signature |
| `void resetResonatorState(std::size_t index) noexcept` | `resonator_bank.h:608` | **OQ-1, ruled 2026-09-10, option (i)** — Q3's "one additive method" is read as one additive *capability*, and FR-042's per-peak state clear needs the companion |

`git diff --numstat dsp/include/krate/dsp/processors/resonator_bank.h` = **`90  0`** — ninety
insertions, **zero** deletions, a single hunk at `@@ -527,0 +528,90 @@`. Every pre-existing method's
body, signature and semantics are therefore byte-for-byte unchanged, which is FR-013's binding
condition. The new method **duplicates** `process()`'s loop rather than extracting a shared helper,
for that reason, and says so in its own comment.

**The regression gate (SC-013).** Recorded with the before/after suite results in
`compliance.md` under SC-013.

### S9.4 Tier 1 — one purely additive `ResonatorBank` method (preferred fallback)

Exactly **one** new method. Every existing method's body, signature and semantics stay
**byte-for-byte unchanged** — which means the new method **duplicates** the render loop rather than
extracting a shared helper out of `process()`. Extracting a helper is the instinctive move and it
violates FR-013's condition; the duplication carries a comment saying so.

```cpp
/// @brief Process one sample, writing each resonator's INDIVIDUAL contribution.
///
/// Additive companion to process() (:470), added for Vorago Phase 3 FR-013 Tier 1:
/// a caller that needs a per-resonator per-sample gate cannot get it from
/// process(), which returns only the summed wet output.
///
/// Writes exactly kMaxResonators floats. A disabled slot writes 0.0f, so the
/// caller never reads stale data. The exciter-mix stage (:514) is deliberately
/// NOT applied: there is no meaningful per-resonator dry term to blend.
///
/// Advances the three global smoothers exactly once, exactly as process() does.
/// A caller must therefore call EITHER process() OR processIndividual() for a
/// given sample, never both — calling both would advance them twice.
///
/// The loop below intentionally DUPLICATES process()'s body instead of sharing a
/// helper with it: FR-013 requires every pre-existing method to stay
/// byte-for-byte unchanged, so process() is not refactored.
void processIndividual(float input, float* outPerResonator) noexcept;
```

Behaviour, mirroring `process()` term for term: null `outPerResonator` ⇒ no-op; `!prepared_` ⇒ fill
`kMaxResonators` zeros and return without advancing; otherwise advance the three smoothers, handle
`triggerPending_`, and for each slot write `enabled_[i] ? filters_[i].process(excitation) *
dampingScale * gains_[i] * calculateTiltGain(frequencies_[i], currentTilt) : 0.0f`.

Under Tier 1 the network holds one `ResonatorBank bank_` plus one `std::array<float,
kMaxResonators> perPeakOut_` scratch **member** (not heap). `renderChunk` calls
`bank_.processIndividual(x, perPeakOut_.data())` once per sample **before** the peak loop, and
`bankProcess(i, x)` becomes a read of `perPeakOut_[i]`. Everything else in S5–S8 is unchanged.

**The one thing Tier 1 cannot do as FR-013 words it.** `bankReset(i)` must clear **one** slot's
biquad state (S7.2). `ResonatorBank` exposes no such method: `reset()` is whole-bank *and* a
configuration wipe, and coefficient writes do not clear `Biquad` state, so `setEnabled(i, false)`
followed by a `setFrequency`+`setQ` pair on the wake edge is **not** equivalent — the stored ring
survives. Folding the clear into `setEnabled`'s semantics is not available either: FR-013 requires
every existing method to stay byte-for-byte unchanged.

**OQ-1 (S17), not taken unilaterally.** FR-013 says "exactly **one** new, purely additive method",
which is a numeric constraint the spec chose deliberately, so the plan records the conflict rather
than resolving it silently. Options: **(i)** read "one method" as one *capability* and add a
five-line companion
`void resetResonatorState(std::size_t index) noexcept { if (index < kMaxResonators) filters_[index].reset(); }`
— no existing behaviour touched, SC-013's before/after consumer-suite gate unchanged; **(ii)** drop
FR-042's state clear under Tier 1 — rejected, it deletes a roadmap constraint and turns SC-015 (f)
red by design; **(iii)** keep Tier 0 banks purely as state containers — defeats the purpose.
The plan proceeds on **(i)** unless the user rules otherwise, and only if T0's probe sends it to
Tier 1 at all.

### S9.5 Tier 2 — `processSympatheticBankSIMD` (last resort)

Taken only if Tier 1 is unavailable or itself insufficient. Three consequences, all verified against
`sympathetic_resonance_simd.h` this session:

* Per-peak output comes from the **state** array — `y1s[i]` holds `y[n]` for resonator *i* after the
  call (`:29-30`) — not from `float* sums`, which is a **single accumulator** (`:37`) and must be
  ignored.
* The FR-041 gate must multiply the read-back `y1s[i]` **outside** the recurrence. It must **not**
  go through the kernel's `gains` array, which multiplies the **input** inside the recurrence
  (`:26`, `scaledInput * gain`) and would corrupt the filter state rather than scale its output.
* The caller owns coefficient derivation (`coeff = 2r·cos ω`, `r²`) **and peak-gain normalisation**:
  an all-pole resonator's peak magnitude scales with `1/(1−r)`, unlike `ResonatorBank`'s
  constant-0 dB-peak form, so FR-031's "Q changes bandwidth without changing peak height" would
  otherwise break. The kernel's per-resonator envelope follower runs unconditionally and is unused.

The include is legal at the same layer (`continuous_body.h:42` precedent; `tools/lint-layers.js`
fails only on reaching *up*), and it carries that justification comment.

### S9.6 The regression gate for either tier (SC-013)

`ResonatorBank` has **real consumers** — every Membrum body (`plugins/membrum/src/dsp/bodies/*.h`,
`body_bank.h`, `drum_voice.h`, `tone_shaper.h`, `voice_pool.cpp`), Innexus
(`physical_model_mixer.h`, `processor/innexus_voice.h`), plus `ContinuousBody`, `NoiseOrganism`,
`iresonator.h` and `modal_resonator_bank_simd.h` — so the gate is **not** a blast-radius argument.
It is running `membrum_tests`, `innexus_tests`, `dsp_processors_tests`, `dsp_systems_tests` and
`seraphis_tests` **before and after** the change and diffing the results. **Any moved result is a
regression to investigate and surface, never a golden to update.**

---

## S10. CPU model and the FR-060 stage probe

### S10.1 Measurement basis

ns per **512-sample block at 48 kHz** (`harmonic_cloud_perf_test.cpp` basis, reused by
`continuous_body_perf_test.cpp`, `atmosphere_engine_perf_test.cpp:22-44` and
`noise_organism_perf_test.cpp:201-272`). One block period is `512/48000 × 1e9 = 10 666 667 ns`, so
roadmap line 228's **0.75 % per voice = 80 000 ns/block**. Percent figures are reported, never
asserted. Trial shape: **best-of-25 × 500 blocks after 400 warm-up blocks**, run in isolation via
`node tools/run-cpu-tests.js dsp_systems_tests`.

### S10.2 The five probe stages (FR-060)

`ResonanceDriftNetwork_StageCostProbe`, `[.perf]`, written and run **before the component exists**:

| # | Stage | Shape |
|---|---|---|
| (a) | **engine, both shapes** | 12 × single-resonator `ResonatorBank` at the FR-016 default anchors, each `process()`d individually **and** 1 × twelve-resonator bank (the Tier-1 shape, measured through a stand-in loop over 12 enabled slots) — reported side by side so FR-011's composition cost is a measured number, not a guess |
| (b) | **lanes** | 48 `BrownianDrift` advanced with `processBlock(64)`, at `decimation = 1` and at `decimation = 17`; the 12 **pan** lanes reported **both** pooled with the other 36 **and isolated on their own**, so a budget miss can be attributed to FR-038 specifically |
| (c) | **control writes** | the twelve-peak `setFrequency` + `setQ` + `setGain` path, with and without change detection, and with FR-015's mandatory Q write |
| (d) | **per-sample tail** | the FR-044 arithmetic: 12 gate `LinearRamp::process()`, 12 pan multiply-pairs, the wet-scale ramp, the mix smoother, the clamp |
| (e) | **sleep edge** | one `ResonatorBank::reset()` + FR-014-order re-apply, reported as **ns per edge** so its amortised cost against a 20–90 s event schedule is visible |

The case is a **probe, not a gate**: it `REQUIRE`s only that every figure is finite and strictly
positive (a zero or NaN means the measurement is broken, the one thing that would make the table
lie), prints the table and the projection, and emits the verdict loudly via `WARN`.

### S10.3 The `std::pow` term, and why (b) is isolated

`BrownianDrift::processBlock(64)` loops in ≤ 32-sample slices, each calling
`OnePoleSmoother::advanceSamples`, which costs **one `std::pow`** unless the smoother has converged
(`smoother.h:243-255`). A 64-sample chunk straddles two or three 32-sample lane steps, so one lane
costs up to 3 `std::pow` per advance. At `decimation = 1`: `48 lanes × 8 chunks × ~2.5 pow ≈ 960
std::pow per block`; at ~25 ns that is ≈ **24 000 ns/block, 30 % of the whole budget**. At the FR-016
default rate (`decimation = 2`) it halves to ≈ 12 000 ns. This is the dominant uncertainty in the
SC-004 projection, it is inherent to the shipped lane (SC-013 forbids amending it), and the only
lever this phase owns is FR-038's **static-pan fallback**, which removes 12 of the 48 lanes (25 %).
That is why (b) reports the pan lanes isolated.

### S10.4 Projection

| term | Tier 0 | Tier 1 |
|---|---|---|
| engine (a) | ≈ 66 400 | ≈ 19 600 |
| lanes (b), `decimation = 2` | ≈ 12 000 | ≈ 12 000 |
| control writes (c) | ≈ 7 700 | ≈ 7 700 |
| per-sample tail (d) | ≈ 5 000 | ≈ 5 000 |
| sleep edge (e) | amortised ≈ 0 | ≈ 0 |
| **total** | **≈ 91 000 (114 % of budget)** | **≈ 44 300 (55 %)** |

### S10.5 Two separate gates, and the stop-and-surface rule

The probe answers two questions with two different numbers. Conflating them — which the first draft
of this plan did, by gating the Tier-1 decision on the total — makes the plan contradict FR-013 in
both directions.

**Gate 1 — FR-013's Tier-1 trigger, stated verbatim (spec.md:292-296).** Tier 1 is triggered by
**probe stage (a)'s twelve-single-resonator-bank engine figure exceeding 48 000 ns/block** (60 % of
the SC-004 ceiling), measured at 512-sample blocks and 48 kHz. At **48 000 ns or less, FR-011 stands
exactly as written and nothing in FR-013 is exercised** — no shipped component is touched, whatever
the total says. Above it, Tier 1 is taken. This is one number against one threshold; no other stage
and no projection enters it.

**Gate 2 — the SC-004 budget, on the total projection.** Independently of gate 1, if the **total**
projection (S10.4) exceeds 80 000 ns the response is, **in order**: FR-013 **Tier 2** when stage (a)
is the dominant term; FR-038's **static-pan fallback** when (b)-isolated is the dominant term; and if
neither closes the gap, a **stop-and-surface to the user with the measured table**. (Tier 1 does not
appear in this ladder because gate 1 has already decided it — if gate 1 was tripped the total is
re-projected on the Tier-1 engine figure before gate 2 is evaluated at all.)

**No implementing agent may** lower `kMaxPeaks`, raise the budget, relax a threshold, or shrink a
workload (`noise_organism_perf_test.cpp:45-57`).

### S10.6 SC-004's compile-time clauses

Each of the three gated arms carries a checked-in baseline with **two different** `static_assert`s
(`noise_organism_perf_test.cpp:1560-1575` idiom):

```cpp
static_assert(kBaselineX * kRegressionFactor <= kBudgetNs, "…exceeds the 0.75 % budget");
static_assert(kBaselineX >= kBudgetNs / 50.0,             "…looks like a no-op run");
```

with `kRegressionFactor = 1.5` and `kBudgetNs = 80000.0`. The floor is not a restatement of the
ceiling: its documented purpose is catching a baseline recorded from a no-op or misconfigured run —
without it a baseline taken from an un-`prepare`d network (which S5.1 makes fill silence and advance
nothing) would compile and pass forever. The clauses are evaluated on **every CI leg** even though
the timing cases are `[.perf]`-hidden, which is exactly why the absolute gate lives there.

---

## S11. Memory (FR-062) — and the correction it forces

**This component allocates nothing, ever.** Every member is fixed-size:
`ResonatorBank` allocates nothing in `prepare` (`resonator_bank.h:184-209`; its `filters_`,
per-resonator arrays and three smoothers are `std::array`/value members `:609-622`), `BrownianDrift`
likewise (`brownian_drift.h:121-128`; its state is scalars plus one `OnePoleSmoother`), and
`LinearRamp`/`OnePoleSmoother` are five floats each. The peak table is a `std::array<Peak,
kMaxPeaks>`.

The spec's FR-062 declares one heap term — a `maxBlockSamples`-sized dry-path scratch buffer — and
SC-011 asserts `getAllocatedBytes() == maxBlockSamples * sizeof(float)` with exactly one allocation.
**That buffer is unnecessary**: S5.2 shows per-sample local dry capture already satisfies every
aliasing case FR-003 admits, including the cross-aliased one, because index `s` of both inputs is
read before either output is written.

FR-062 anticipates the *opposite* direction ("If the implementation finds it needs a second buffer,
the constant is declared here first and SC-011's expected values are re-derived from the
declaration"). The same discipline is applied in the direction the code actually goes:

> **Declared here first:** the prepare-time heap footprint of `ResonanceDriftNetwork` is **zero
> bytes** and `prepare` performs **zero** allocations. `getAllocatedBytes()` returns `0`.

SC-011 is re-derived from that declaration (S12.2), **and `spec.md`'s FR-062 (`:846-856`) and SC-011
(`:1139-1148`) are edited to match at T1.5** — recording the change only here would leave the
compliance table carrying two rows that contradict the shipped assertions (C-7).
`PrepareConfig::maxBlockSamples` is **retained**
in the public surface — FR-002 mandates the field, Phase 10/12 may want it, and removing it would be
a gratuitous API change — clamped to `[64, 8192]`, reported, and documented in the header as **not
sizing anything in this component**. This strengthens SC-005 and SC-011 rather than relaxing them,
and it is recorded as spec correction **C-1** (S14).

---

## S12. Test plan

### S12.1 New translation units

| TU | Target | Contents | `-fno-fast-math`? |
|---|---|---|---|
| `dsp/tests/unit/systems/resonance_drift_network_test.cpp` | `dsp_systems_tests` | SC-005, SC-006, SC-007, SC-008, SC-010, SC-011, SC-014, SC-017, SC-018, SC-019, SC-020, SC-021, SC-022 + `ResonanceDriftNetwork_ControlSurfaceClamps` | **No** — must build in the FP mode the header ships in |
| `dsp/tests/unit/systems/resonance_drift_network_spectral_test.cpp` | `dsp_systems_tests` | SC-001, SC-002, SC-003 `[long]`, SC-015 `[long]`, SC-016 `[long]`, plus `ResonanceDriftNetwork_MeasureThresholds` `[.calibration]` | No |
| `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp` | `dsp_systems_tests` | SC-004 (a)–(d) + the FR-060 stage probe, both `[.perf]` | **No** — `-fno-fast-math` would move the figures the baselines pin |
| `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp` | `dsp_systems_tests` | SC-009 only | **Yes** — the only Phase-3 TU in that block |

The three siblings stay **out** of the `-fno-fast-math` block deliberately and load-bearingly: the
FR-008/FR-009 guards must be proved in the `/fp:fast` + `-ffast-math` mode the header actually ships
in on the macOS leg, and the perf baselines are pinned to figures that flag would change.

If FR-013 Tier 1 is taken, cases are added to the already-registered
`dsp/tests/unit/processors/resonator_bank_test.cpp` (`dsp/tests/CMakeLists.txt` processors list) —
no CMake change for that file.

### S12.2 Criterion → case → assertion

| SC | Case (TU) | Assertion strategy |
|---|---|---|
| **SC-001** | `ResonanceDriftNetwork_MaxQRingOut` (spectral) | 12 peaks, every `peakQ = kMaxResonatorQ = 100`, `qWander = 0`, FR-016 anchors, `mix = 1`; white noise at −12 dBFS **on both channels** for 60 s, then silence. (a) every sample `detail::isFinite` — never `std::isnan`; (b) `getClampEngagementCount() == 0`; (c) **the `1/sqrt(N)` law asserted as a derived relationship, not as agreement.** The spec's form ("render at `numPeaks = 4` and at `numPeaks = 12` with peaks 0–3 identical, require the two peak magnitudes to agree within a measured bound") assumes a correct build produces the SAME magnitude in both arms. It does not: under FR-016's geometric anchors at equal Q each peak's noise-equivalent bandwidth is ∝ `f/Q`, and `Σf` over peaks 0–3 is 273 Hz against 4624 Hz over peaks 0–11, so on a broadband drive the correct build differs by ≈ `20·log10(sqrt(4624/273)·sqrt(4/12))` ≈ **7.5 dB** — a systematic offset LARGER than the 4.77 dB defect the arm exists to detect, which any "bound set above the observed maximum" would then swallow (and an over-normalising `1/N` exponent moves the difference to ≈ 2.7 dB, i.e. *further inside* the bound). Corrected construction, the one SC-015 (g) already uses: **configure peaks 4–11 dormant in BOTH arms**, so the two renders contain identical acoustic content and the only difference is FR-019's `N` — which is `numPeaks`, not the awake count, so it is still 4 vs 12. Require the level difference to equal `20·log10(sqrt(12/4)) = **4.77 dB ± 0.5 dB**`. This simultaneously asserts FR-019's load-bearing "`N` is `numPeaks`, not awake peaks" clause, which no other criterion reaches. Recorded as spec correction **C-10**; (d) tail below `1.0e-4` within a bound **computed in the test from shipped constants**: `kMaxResonatorQ * kLn1000 / (kPi * kMinResonatorFrequency) + 5.0` ≈ 16.0 s, `REQUIRE`d to sit in [15.9, 16.1] before use (the `continuous_body_test.cpp:3370-3386` pattern); (e) repeat with all four depths at maxima (24 st / 2 oct / 24 dB / pan 1.0) and `wanderRate = 1.0` — (a)–(d) unchanged |
| **SC-002** | `ResonanceDriftNetwork_NoZipperUnderDrift` (spectral) | 220 Hz sine at −12 dBFS on both channels, 12 peaks with anchors spread across it, all four lanes at max depth, `wanderRate = 1.0` (so `decimation == 1`, the un-decimated path). **Exactly 60 s** (2 880 000 samples, 45 000 chunks) — pinned because the boundary population size determines the statistic. `x[n] = outL[n]`; `d[n] = |x[n] − 2x[n−1] + x[n−2]|`; partition on **`n mod 64 ∈ {0,1,2}`** (3/64 boundary, 61/64 interior) — **not** the spec's `{0,1}` (spec correction **C-11**). The control step runs when `controlPhase_ == 0` (S5.1), so new coefficients first affect the sample at absolute `n ≡ 0 (mod 64)`, and the second difference spans that sample in **three** windows: `n ≡ 0`, `1` **and** `2`. Leaving residue 2 in the interior population puts contaminated samples into the reference: residue 2 is `1/62 = 1.6 %` of the interior, an order of magnitude above the 0.1 % tail `P` is drawn from, so `P` would itself be computed from discontinuity-carrying samples, `B/P` would collapse toward 1 however bad the retuning step was, (a) would pass vacuously under `B <= 1.5·P`, and (c)'s `B/P > 10` would be harder still. `kBoundaryRatio` is therefore **measured under the corrected partition** in T14 and (c)'s ratio re-verified against it. **`B` and `P` are the same 99.9th percentile of their own populations** (a max-vs-percentile comparison would sit at the 99.9989th quantile for a 60 s render and go red from extreme-value statistics alone). (a) `B <= kBoundaryRatio · P`, `kBoundaryRatio` **measured** across ≥ 8 seeds (provisional 1.5); (b) control arm `setWanderEnabled(false)`: `B_off/P_off <= 1.05` **and** `|P_on − P_off|/P_off <= 0.10`; (c) **injection, mandatory, through the public surface — and SYSTEMATIC, not one-shot (spec correction C-12).** The spec's form ("a ±1-octave `setPeakAnchorHz` jump on ONE control step") is **unsatisfiable as written**, which would leave the spec's own anti-vacuity proof for its primary no-zipper criterion unproducible: the pinned 60 s render gives a boundary population of `3/64 × 2 880 000 = 135 000` samples, `B` is its 99.9th percentile (≈ the 135th largest value), and one jump contributes at most the 3 second-difference windows spanning that single boundary sample. Three outliers cannot move a 135 000-sample 99.9th percentile at all, so `B/P > 10` is unreachable no matter how large the injected discontinuity is. Corrected: with `setSlewCeilings(24, 24)` apply an **alternating ±1-octave `setPeakAnchorHz` jump on EVERY control chunk for the full pinned render**, so the boundary population is uniformly perturbed and `B` responds to the injected discontinuity rather than to three samples in the tail. Same duration, same partition, same statistic as (a); only the injection schedule changes. `B / P` must exceed **10**; (d) gate ramp — primary on `getPeakGate(i)` sampled at **`numSamples == 1`** granularity: 0→1 in 50 ms ± 5 ms, monotone, no step above `1.05/(kGainRampMs·0.001·fs)` = 4.375e-4; secondary audio arm on the per-cycle peak magnitude over `round(fs/f_peak)`-sample windows reaching 90 % within 50 ms ± 5 ms, with **no** per-sample bound asserted on audio. **Three further arms, each covering a mechanism no other criterion can fail on:** (d-ii) **`refreshGates` re-target immunity (S7.1)** — repeat the primary measurement while calling `setPeakWake` on a **different** peak once per 512-sample block for the whole ramp, exactly as FR-040's Phase-10 scheduler will; the measured peak must still reach 1.0 in 50 ms ± 5 ms. A build whose `refreshGates()` re-targets every peak reads ≈ 107 ms to 90 % and ≈ 213 ms to 99 %, so this arm is red on it while the upper-bound step assertions all still pass (re-targeting only *lowers* the step); (d-iii) **FR-035's base-level ramp (C-6)** — a mid-render `setPeakLevel(i, −60 → +12)` on a steady sine at that peak's centre frequency: the per-cycle envelope rises over 50 ms ± 5 ms and no per-sample step exceeds `1.05 · dbToGain(12)/(kGainRampMs·0.001·fs)`. A build without `Peak::levelRamp` steps 72 dB in one sample and is red; (d-iv) **FR-043's mix smoother** — precondition: a 2 kHz sine (outside every default peak's passband, wet RMS ≥ 40 dB below dry, `REQUIRE`d before the arm runs) rendered at `mix = 0`, then `setMix(1)` mid-render; the per-cycle envelope, which tracks `(1 − m)`, must fall to 1 % of its initial value in **20 ms ± 5 ms** (`OnePoleSmoother::configure`'s documented "time to reach 99 % of target", `smoother.h:158-159`) and must be monotone. A build that applied `setMix` instantaneously — a click on every mix move — reads 0 ms and is red; as first written **no criterion in the spec could fail on it**, SC-015 (c) and SC-020 reading settled renders only |
| **SC-003** | `ResonanceDriftNetwork_WanderRateSpectral` `[long]` (spectral) | ≥ `10·T` (350 s at the default), pink noise −12 dBFS, `mix = 1`. Every 100 ms extract the five `AudioFeatures::band` fractions. **Band selection is fixed:** eligible = mean fraction ≥ 0.01; among eligible, highest CV **in the wander-on arm**; that index used in **every** arm; recorded in `compliance.md`. (a) lag-`T/8` normalised autocorrelation of the mean-removed trajectory ≥ **0.20**; (b) control arm CV ≥ **1.8×** below; (c) sampling `getPeakCurrentFrequency`/`getPeakCurrentQ` every control step, **zero** excursions outside `[anchor·2^(∓st/12)] ∩ [20, 0.45fs]` and `[baseQ·2^(∓oct)] ∩ [0.1,100]` in ≥ 10⁵ samplings; (d) per-peak lag-`T/8` ACF ≥ 0.20 **and** lag-`8T` ≤ 0.10; (e) **independence against a measured null** — mean `|r|` over the `C(12,2) = 66` pairs of mean-removed `log2(getPeakCurrentFrequency(i))` lane trajectories sampled per control step, bound `kLaneIndependenceR` derived from ≥ 12 network seeds (Phase 2's 0.05 is **not** transcribed: Bartlett's formula gives `E|r| ≈ 0.23` here from estimator noise alone), **plus an in-process anti-vacuity control** — twelve lanes on the same seed **and salt** must read ≥ 0.95; (f) **`setWanderRate` is not inert** — 0.005 Hz (`dec = 7`) vs 0.3 Hz (`dec = 1`), each ≥ `10·T`, ACFs evaluated at the **same absolute lag of 1.67 s**, `ρ_slow − ρ_fast >= kRateSeparation` measured across ≥ 8 seeds. **(f) is the only arm that fails if FR-037 is missing** |
| **SC-004** | `ResonanceDriftNetwork_CpuBudget` `[.perf]` (perf) | Reference: 12 peaks, `Hybrid` at `gravity = 0.5`, all four lanes at FR-016 defaults, wander on, `mix = 1`, all awake, drive identical on both channels. (a) ≤ **80 000 ns**/512-block @48 kHz; (b) wander-disabled arm ≥ **10 %** below reference; (c) all-dormant arm ≥ **40 %** below, with `setPeakDormant(i,true)` on all twelve **then ≥ 50 ms of rendering** so every gate landed at exactly 0 and every sleep edge fired, `isPeakEngineActive(i) == false` for all twelve **asserted before timing starts**, and the output verified exactly `0.0f` on both channels throughout so a "cheap" figure cannot come from an accidental early return; (d) the two `static_assert`s of S10.6 per gated arm |
| **SC-005** | `ResonanceDriftNetwork_NoAllocationAfterPrepare` (main) | `AllocationScope` (`allocation_detector.h:111`) around 20 000 blocks of sizes {1, 63, 64, 65, 512, 2048, 4096} that also drive one `touchEverySetter(net, block)` helper calling **every setter in S1.3 declaration order** (so a setter added later is covered by construction — the Phase 2 lesson, where a hand-written list silently omitted seven), including `setWanderRate` across decimation-changing values and `setSlewCeilings` across its clamps, all six anchor-mode transitions, `setNumPeaks` up and down, dormancy toggled on all twelve so all twelve sleep edges fire, and `reset()`. **0 allocations** |
| **SC-006** | `ResonanceDriftNetwork_SeedDeterminism` (main) | (a) same seed/config/rate ⇒ `max|diff| == 0` over 10 s on both channels; (b) **trajectory** decorrelation over `≥ 20·τ_effective` — the cheap option is `wanderRate = 1.0` ⇒ `τ = 1 s` ⇒ a 20 s render, and the duration and resulting `τ` are transcribed into `compliance.md`; pairing is **peak *i* of A against peak *i* of B**, 12 pairs, not 24 cross-paired; `kSeedDecorrelationR` derived from ≥ 12 seed pairs; **anti-vacuity: the same estimator on a same-seed pair must read ≥ 0.95**; (c) the audio of the two differs — RMS of the difference ≥ **−30 dB** of either render; (d) `reset()` with no setter since `prepare` reproduces the post-`prepare` stream exactly |
| **SC-007** | `ResonanceDriftNetwork_ResetPreservesConfiguration` (main) | (a) after a full non-default configuration + `reset()`, the render is **non-silent** (RMS > −60 dBFS on a −12 dBFS drive) — **the arm that catches a forwarded `ResonatorBank::reset()` without S2.2's step-5 re-apply**, which would leave every slot disabled and the output at digital silence; (b) that render equals a fresh `prepare` + the same setter sequence sample-exactly; (c) every configuration getter returns its pre-`reset` value, `getPeakPan` included |
| **SC-008** | `ResonanceDriftNetwork_SampleRateIndependence` (main) | 44.1/48/96 kHz, identical config and seed. (a) `getPeakCurrentFrequency` (wander off) within **0.1 %** for every peak whose anchor is below `0.45 × 44100`; (b) the gate ramp measures 50 ms ± 5 ms at every rate via SC-002 (d)'s named estimator; (c) **a rate-invariant quantity, not three independent draws** — over ≥ `50·τ` at each rate (e.g. 20 s at `wanderRate = 1.0`), estimate the **1/e decorrelation lag in seconds** of a peak's `log2` frequency trajectory and require the three to agree within a tolerance **measured across ≥ 8 seeds at one fixed rate** (provisional ±15 %), with an **injection check** that a hardcoded sample rate moves the statistic; (d) at 44.1 kHz an anchor above `0.45·fs` is clamped silently, reported clamped, no non-finite value, **no clamp-counter engagement** |
| **SC-009** | `ResonanceDriftNetwork_NonFiniteGuards` (nonfinite TU) | Non-finite values built **from bit patterns through a volatile sink**, never `std::numeric_limits`. (a) a NaN or Inf input sample on either channel leaves every output sample on **both** channels finite, and the next block of finite input renders normally; (b) NaN/±Inf into **every** setter is rejected and the matching getter still reports the **previous** value (FR-008's rejection semantics, not a neutral substitution); (c) `getClampEngagementCount()` unchanged after every injection |
| **SC-010** | `ResonanceDriftNetwork_BlockSizeInvariance` (main) | the same total render as 512-blocks and as the irregular partition {1, 63, 64, 65, 200, 512, 1024, …} from identical fresh instances ⇒ `max|diff| <= kSampleTolerance = 5.0e-4` (`render_fingerprint.h:58`); same binary, same process, no stored golden |
| **SC-011** | `ResonanceDriftNetwork_PrepareFootprint` (main) | **Re-derived from S11's declaration:** `getAllocatedBytes() == 0` and `prepare` performs **0** allocations inside an `AllocationScope`, checked at `maxBlockSamples` = 64, 2048 and 8192 so the *relationship* passes, not one value. Nothing is transcribed from the implementation |
| **SC-012** | the commands themselves, into `compliance.md` | `node tools/lint-odr.js`, `lint-layers.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `check-portability.js` all exit 0; `./tools/run-clang-tidy.ps1 -Target dsp` zero new warnings; warning-free on MSVC, GCC, AppleClang |
| **SC-013** | suite runs, into `compliance.md` | Default (no fallback): `dsp_core_tests`, `dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`, `membrum_tests`, `innexus_tests`, `seraphis_tests` green with **no edits to existing cases**, and `git diff --stat` showing no change under `dsp/include/` outside the one new header. If Tier 1 or Tier 2 is taken: the same suites run **before and after** and diffed (S9.6) |
| **SC-014** | `ResonanceDriftNetwork_AnchorModes` (main) | FR-016 defaults, 48 kHz, wander off. **Settling clause applied to every arm:** ≥ 100 control chunks (6 400 samples) rendered after the last setter and before the first read, because FR-035 caps a one-octave move at 50 control steps ≈ 67 ms. (a) Free within 1 cent; (b) Keyed within 1 cent, and an **upward** 55 → 110 Hz `setNoteFrequency` moves every peak by 1200 ± 2 cents — the downward arm is an explicit clamp check instead, since `27.5 × 0.5 = 13.75 Hz` clamps to 20 Hz (a −551 cent move) and those peaks are named and asserted to sit **at** 20 Hz; (c) Hybrid at `g = 0` **bit-equal** to Free (S4's short-circuit branch is what makes this pass); (d) Hybrid at `g = 1` — every peak within 1 cent of **its own** `noteHz × ratio[i]` **and** the twelve results **pairwise distinct** to 1 cent (the assertion that catches the superseded nearest-neighbour collapse), plus `g = 0.5` matching the closed form; (e) `g = −1` within 1 cent of `exp(2·log f_free − log f_keyed)`; (f) sweeping `g` from −1 to +1 in 0.05 steps with settling at every step moves each peak's `log2 f` monotonically, no reversal, no step above `freqSlewCeiling` |
| **SC-015** | `ResonanceDriftNetwork_PeakLifeCycle` `[long]` (spectral) | (a) **after ≥ 50 ms of rendering at `wake == 0`** the peak contributes exactly zero: sweeping its anchor across a sine changes both channels by `max|diff| == 0`, and `isPeakEngineActive(i) == false`; (b) lanes freewheel — `getPeakCurrentFrequency(i)` across a 120 s dormant interval differs by ≥ 20 cents for ≥ 9 of 12 peaks; (c) all dormant + `mix = 1` ⇒ digital silence on both channels, and `mix = 0` ⇒ both channels equal their own inputs within `kSampleTolerance` for **any** pan configuration; (d) `setPeakDormant(i,true)` vs `setPeakWake(i,0)` ⇒ `max|diff| == 0` on both channels, distinguished only by `isPeakDormant`/`getPeakWakeAmount`; (e) a mid-render `setNumPeaks` reduction silences the dropped peaks over the ramp with no `getPeakGate` step above SC-002 (d)'s bound; (f) **wake after 120 s dormancy at `peakQ = 100`** (own `RT60 = 5.5 s` at 40 Hz): gate satisfies SC-002 (d), and the peak's contribution **never exceeds its own pre-dormancy steady-state peak magnitude** in the first 5 s — red without S7.2's state clear — **extended**: with the peak held at `wake = 0` one extra control step through the public surface, `getPeakCurrentFrequency`/`Q`/`GainDb` already reflect the drifted value **before** the gate lifts, so a build that defers the snap or never writes it fails even though the echo alone would look correct; **and the C-8 arm** — a `setNoteFrequency` octave jump applied *during* the dormant interval must be reflected by `getPeakCurrentFrequency(i)` within **one** control step, not fifty, which is what distinguishes S6.5's unslewed dormant tracking (required by FR-042's "regardless of how far it moved during the dormant interval") from a slew-limited dormant peak; (g) over a scripted 20–90 s wake/sleep sequence at `numPeaks = 12`, 200 ms-window RMS moves by no more than a bound **measured across ≥ 8 seeds**, and a mid-render `setNumPeaks(12→4)` moves the survivors by `4.77 dB ± 0.5 dB`; (h) **dormancy reached through a vanishing wake, not through `setPeakDormant`** — `setPeakWake(i, 1e-8f)` (a legal `[0, 1]` argument, and exactly the shape FR-040's Phase-10 caller produces from `getEnvelopeValue() × getActiveDepth()`) followed by ≥ 50 ms of rendering must leave `isPeakEngineActive(i) == false`. Every other dormancy arm — SC-004 (c), SC-015 (a)/(d) — reaches dormancy through `setPeakDormant`, so this is the only arm that exercises S7.1's `kWakeSilenceEpsilon` snap and the only one that would fail on a build where a residual wake keeps the engine alive forever |
| **SC-016** | `ResonanceDriftNetwork_LongRenderBoundedness` `[long]` (spectral) | every depth and `wanderRate` at maximum, `peakQ = 100`, an external wake pattern on a 20–90 s stochastic schedule (`slow_event_scheduler.h:164-165` defaults), pink noise −12 dBFS, 30 min @48 kHz. (a) every 10 s window's RMS within `±kSoakWindowDb` of the median, **measured** across ≥ 8 seeds (provisional ±6 dB and explicitly not trusted — Phase 2 had to widen ±3.0 → ±4.5 dB for a *less* extreme configuration); (b) least-squares slope within **±0.5 dB / 30 min**; (c) no window below `kSoakFloorDbfs` (**measured**, provisional −60 dBFS) nor above −3 dBFS; (d) zero non-finite samples and `getClampEngagementCount() == 0` |
| **SC-017** | `ResonanceDriftNetwork_RealisedQ` (main) | **The criterion D-9 needs, and the only one that can fail on the write-order trap.** One peak driven in isolation (all others dormant), white noise −12 dBFS, `mix = 1`, wander off; estimate that peak's **−3 dB bandwidth** and require `f/BW` to agree with `getPeakCurrentQ(i)` within **25 %**. **Estimator: the Phase 2 Welch-on-an-arbitrary-probe-grid band-ratio fit** (`noise_organism_test.cpp:1631-1728`) — `welchPowerGrid` + `measuredBandSum`/`modelBandSum` + `fitQFromBandRatio` bisection — **not** a −3 dB crossing search, whose ~1 dB of noise on a Lorentzian flank is ~23 % of the half-width and would consume the entire ±25 % budget before any real defect. Coverage: `peakQ` ∈ {2, 12, 100} × anchors {40, 265, 1278 Hz}, each with SC-014's settling clause and each with frequency written **before** the Q read. **Injection arm, mandatory:** a build that swaps the FR-014 order, or that lets change detection skip an "unchanged" Q after a frequency write, must go **red**, with the mutated build's measured `f/BW` recorded and the unmutated build re-verified afterwards. Also: `getPeakEquivalentRt60(i)` consistent within the same tolerance, and it must **change** when `peakQ` changes at a fixed anchor — which it cannot if Q is being derived from the stale decay table |
| **SC-018** | `ResonanceDriftNetwork_SlewLimit` (main) | `Keyed`, defaults, wander off, `freqSlewCeiling = 0.02`; upward 55 → 110 Hz `setNoteFrequency`, sampling `getPeakCurrentFrequency(i)` once per control step. (a) the per-step `log2 f` change never exceeds the ceiling, for any peak, at any step; (b) each peak reaches the new target within **50 ± 2 control steps** (`1.0/0.02`) — fails if the limiter is absent (1 step), slower, or applied in the wrong domain; (c) SC-002's `B/P` across that transition stays under SC-002 (a)'s bound — **on a population the bound is valid for**. A single 55 → 110 Hz transition at the default ceiling lasts 50 control steps ≈ 3 200 samples, giving ~150 boundary and ~3 050 interior samples, at which size the 99.9th percentile of each population *is* the max and the ~3rd largest respectively — precisely the max-vs-percentile mismatch SC-002's own rewrite paragraph (spec.md:928-932) was written to eliminate, and a different extreme-value regime from the one `kBoundaryRatio` was measured in. So **(c) renders for the same pinned 60 s as SC-002 (a), repeating the 55 ↔ 110 Hz note step on a fixed 1 s schedule**, and computes `B/P` over that whole render. Same duration, same partition, same population size, same statistic — the bound transfers legitimately, and the claim it makes is stronger than the spec's (sixty note changes must not step the coefficients, not one). Stated here rather than in S12.3 because it is a construction change, not a threshold change; (d) the same three at `setSlewCeilings(0.005, 0.005)` give **200 ± 5** steps, proving the ceiling is a real control |
| **SC-019** | `ResonanceDriftNetwork_RenderPathBoundaries` (main) | All four pointers. (a) **in-place equality** — `inL==outL, inR==outR` equals the out-of-place render from the same state, `max|diff| == 0` on both channels; the cross-aliased `inL==outR, inR==outL` case gives the same guarantee (S5.2 is why); (b) **each** of the four pointers null, independently: render a reference block, call with one null, render again, `max|diff| == 0` against the reference continuation on both channels; (c) `numSamples == 0` a no-op consuming no control step — same construction, repeated 1000× so a one-sample drift would show; (d) `processBlock` **before `prepare`** writes exactly `numSamples` zeros to both and advances nothing; (e) `numSamples = 65 536` with `maxBlockSamples = 64` renders correctly and performs **0** allocations inside an `AllocationScope` |
| **SC-020** | `ResonanceDriftNetwork_WetGainTrim` (main) | (a) the FR-016 reference patch on SC-001's broadband drive at the measured `kDefaultWetGainDb` lands within the window recorded in `compliance.md` by T14's FR-045 measurement (a compliance row, not a re-measurement); (b) `mix = 0.5` is a **blend, not a mute** — **isolated directly, never by differencing (spec correction C-13).** The spec's method (difference the `mix = 0.5` render against the `mix = 0` dry-only render) measures the wrong quantity and is blind in exactly the failure it exists to catch: per FR-044 that difference is `0.5·wetTrimmed − 0.5·dry`, not `wetTrimmed`, so if the wet path is broken — the "shipped at an implicit `wetGain = 0 dB`" case the arm names, where wet sits ≈ 33 dB below dry per C-5 — the difference degenerates to `0.5·dry`, whose RMS is `dry − 6.02 dB`, landing **exactly** on the "within roughly 6 dB of the dry path's RMS" pass window. The criterion would report a healthy blend for a mute. Corrected: render the same patch and drive at **`mix = 1`** (wet only, FR-043) and at **`mix = 0`** (dry only) and require `|RMS_wet_dB − RMS_dry_dB| <= 6 dB`; the `mix = 0.5` render is kept only as a **monotonicity** check, its RMS lying between the two; (c) `getWetGain()` echoes the last set value clamped to `[-24, +48]`, and a 0 dB trim reduces the wet RMS relative to (a) by the expected dB difference within **0.5 dB**; (d) folded into SC-002 (d-iv) — FR-043's 20 ms mix smoother is asserted there, on a transient, because every arm in this case reads a settled render and none of them can fail if the ramp is absent |
| **SC-021** | `ResonanceDriftNetwork_PeakPan` (main) | One peak isolated (others dormant), sine at its centre frequency, `mix = 1`, `Free`. (a) **equal-power constancy** — sweeping `setPeakPan` across `[-1,+1]` in 0.1 steps with wander off, `outL_rms² + outR_rms²` stays within **0.5 dB** of its `pan = 0` value across the whole sweep; (b) **determinism** — two instances with the same seed report identical `getPeakPan(i)` defaults for all twelve, and with `panWander > 0` identical `getPeakCurrentPan(i)` trajectories; (c) **non-vacuity, measured** — ≥ 60 s at the default `panWander`/`wanderRate`, the inter-channel level difference `20·log10(outL_rms/outR_rms)` over 1 s windows has a range exceeding `kPanNonVacuityDb`, **set from ≥ 8 seeds below the observed minimum** (provisional 1 dB) — proof the lane is live, not a static split that would pass (a) and (b) vacuously; (d) folded into SC-015 (c) |
| **SC-022** | `ResonanceDriftNetwork_ClearAudioState` (main) | (a) one peak driven to steady state at `peakQ = 100` (`RT60 ≈ 5.5 s`), `clearAudioState()`, contribution **silent** (below `kSampleTolerance`) on the very next sample; (b) two identically-seeded instances run forward together, `clearAudioState()` on one — every peak's `getPeakCurrentFrequency`/`Q`/`GainDb`/`Pan` agrees with the untouched twin within `kMetricTolerance` for ≥ 60 s afterward (the lanes never rewound); (c) the same comparison with `reset()` instead shows the reset instance's trajectories **diverge** — proving the two methods are behaviourally distinct, not two names for one effect |

Two further cases, covering normative clauses no criterion above reaches:

| Case (TU) | Covers | Assertion |
|---|---|---|
| `ResonanceDriftNetwork_ControlSurfaceClamps` (main) | FR-010, FR-016 ranges, FR-050 | (i) `setNumPeaks(0)` ⇒ `getNumPeaks() == 1`; `setNumPeaks(99)` ⇒ `12`. (ii) every clamped setter driven past both ends and read back: `setPeakQ(i, 400)` ⇒ 100, `setPeakQ(i, 0)` ⇒ 0.1, `setPeakLevel(i, 99)` ⇒ 12, `setWanderRate(5)` ⇒ 1.0, `setWanderRate(0)` ⇒ 0.002, `setSlewCeilings(99, 0)` ⇒ (24, 0.001), `setGravity(9)` ⇒ 1, `setPeakRatio(i, 0)` ⇒ 0.25, `setNoteFrequency(2)` ⇒ 8, `setWetGain(99)` ⇒ 48, `setPeakPan(i, 9)` ⇒ 1, **`setPeakWake(i, 9)` ⇒ `getPeakWakeAmount(i) == 1.0f` and `setPeakWake(i, -1)` ⇒ `0.0f`**, **`setPeakPanWander(i, 9)` ⇒ 1 and `setPeakPanWander(i, -1)` ⇒ 0** — the last two were absent from the first draft of this list and from the S1.4 contract, which is why nothing would have caught an unclamped wake scaling a peak 50 % past unity or inverting its polarity against the other eleven (S1.4). (iii) `getLaneDecimation()` returns 1 at `wanderRate = 1.0`, 2 at 0.03, 17 at 0.002 — the FR-037 mapping asserted directly rather than only through SC-003 (f)'s statistic. (iv) every setter called with `peak == kMaxPeaks` is a silent no-op (no getter anywhere changes) and every getter out of range returns its S1.4 neutral. (v) **the C-9 sample-rate ordering, asserted rather than assumed** — `prepare(0.0, cfg)` and `prepare(1.0, cfg)` (both floored to `kMinUsableSampleRate`), then `setPeakAnchorHz(i, 10.0f)`, `setPeakAnchorHz(i, 1e6f)` and one `processBlock` of 64 samples on each: no crash, no MSVC `_STL_VERIFY` abort from an inverted `std::clamp`, every output sample finite, and `getPeakCurrentFrequency(i)` inside `[kMinResonatorFrequency, kMaxResonatorFrequencyRatio · kMinUsableSampleRate]`. (vi) **the S1.2 log2-Q constants pinned to the shipped Q bounds** (the `entropy_processor.h:82` runtime-equivalence idiom, because `std::log2` is not constexpr): `REQUIRE(kMinLog2Q == Approx(std::log2(kMinResonatorQ)).margin(1e-5))` and the same for `kMaxLog2Q`/`kMaxResonatorQ`, so the constexpr-series literals cannot drift from the constants the clamp is supposed to express |
| `ResonanceDriftNetwork_OutputClampEngages` (main) | FR-018 | **The clamp and its counter have no criterion that can fail on their absence.** Every criterion that reads `getClampEngagementCount()` requires it to be zero or unchanged — SC-001 (b) `== 0`, SC-016 (d) `== 0`, SC-009 (c) "unchanged", SC-008 (d) "no engagement" — and nothing anywhere drives the output past `±kOutputClamp` or asserts the output is bounded there. **A build with no clamp at all and `getClampEngagementCount()` hardcoded to `0` passes the entire spec.** This case closes that, entirely through in-spec settings: one peak at `peakQ = kMaxResonatorQ = 100` anchored at 265 Hz, `setPeakLevel(i, +12)` (FR-017's ceiling), `setWetGain(48)` (FR-045's ceiling), `mix = 1`, driven by a 265 Hz sine at 0 dBFS on both channels for 5 s. Assert **both** halves: `max|outL| <= kOutputClamp` and `max|outR| <= kOutputClamp` sample by sample (the clamp is doing its job), **and** `getClampEngagementCount() > 0` (the counter can increment — the half that fails on a hardcoded zero). Then record the count, render 5 s of the FR-016 default patch on SC-001's −12 dBFS drive, and require the count to be **unchanged** (an in-spec configuration does not engage it, which is what makes SC-001 (b)'s `== 0` a real assertion rather than an untested one). If the drive above turns out not to reach `±4.0` in practice, the drive is raised — never the assertion weakened — and the measured peak is recorded in `compliance.md` |

### S12.3 Measured thresholds — where the numbers come from

Ten thresholds ship **provisional** and must be replaced by measurement before they become
compliance rows. `SC-001 (c)` has **left** this list: C-10 turns it into a derived relationship
(`4.77 dB ± 0.5 dB`) with no measured null distribution to calibrate against. The remaining ten are
`SC-002 (a)` `kBoundaryRatio` (1.5, and it is now measured under C-11's corrected `{0,1,2}`
partition, so a figure measured under the old partition may not be carried over), `SC-003 (e)`
`kLaneIndependenceR`, `SC-003 (f)` `kRateSeparation`, `SC-006 (b)` `kSeedDecorrelationR`,
`SC-008 (c)` (±15 %), `SC-015 (g)`, `SC-016 (a)` `kSoakWindowDb` (±6 dB), `SC-016 (c)`
`kSoakFloorDbfs` (−60 dBFS), `SC-020 (a)` (tied to FR-045's `kDefaultWetGainDb`) and `SC-021 (c)`
`kPanNonVacuityDb` (1 dB).

They are produced by **one hidden case**, `ResonanceDriftNetwork_MeasureThresholds`, tagged
`[.calibration]` (the `NoiseOrganism_MeasureSourceDrive` precedent,
`noise_organism_perf_test.cpp:19-20`), which renders each null distribution across **≥ 8 seeds**
(≥ 12 seed pairs for SC-006 (b); ≥ 12 seeds for SC-003 (e)) and prints mean, spread and observed
extreme. T14 transcribes each result into the test constant **and** into `compliance.md` with its
distribution. The FR-045 `kDefaultWetGainDb` measurement runs in the same case.

Criteria carrying a **mandatory injection or anti-vacuity arm** that must go red on the defect it
guards: SC-002 (c) (now systematic per C-12 — as one-shot it was arithmetically unsatisfiable and
could never have been produced), SC-002 (d-ii)/(d-iii)/(d-iv), SC-003 (e)'s same-seed control,
SC-015 (h), SC-017's write-order swap, and `ResonanceDriftNetwork_OutputClampEngages`. A criterion
that cannot fail on a real defect is not a criterion.

**Bound provenance rule, binding:** any threshold that proves unable to separate a correct
implementation from an injected defect is **re-derived from a measured distribution across seeds and
the change recorded** — never merely widened. Phase 2 had to rewrite five criteria after measurement
(`specs/vorago-phase2-noise-organism/compliance.md:71-86`), including one that "passed on seed luck".

### S12.4 Seeds, tolerances, determinism hygiene

Every case pins `setSeed(k)` with a case-local constant; no case depends on another's RNG state. No
stored float goldens anywhere — SC-010 is the only render pin and it uses `render_fingerprint.h`'s
`kSampleTolerance`; `tools/lint-float-bit-goldens.js` must stay green. Every finiteness check uses
`detail::isFinite`/`isNaN`/`isInf`; no `std::isnan`, no
`std::numeric_limits<float>::quiet_NaN()`/`infinity()` (they fold to finite garbage on the macOS
`-ffast-math` leg — `reference_fastmath_nan_in_tests.md`).

### S12.5 `[long]` tagging

`[long]` on SC-003, SC-015 and SC-016 only. Each is multi-minute (SC-003 renders ≥ 350 s across ≥ 12
seeds; SC-015 (b)/(f) are 120 s dormant intervals ≈ 5.8 M samples each; SC-016 is 30 min) and every
assertion in them is a property of the seeded walk, of exact zeros, or of a measured statistic —
**toolchain-independent**, so they belong in the nightly lane. SC-001, SC-002 and SC-021 are **not**
tagged: at the projected ≈ 45 µs/512-block a 60 s render costs ≈ 0.25 s of CPU, far under CLAUDE.md's
~15 s threshold. Nothing NaN/Inf-guard-, bounded-grid- or state-format-related is tagged (SC-009,
SC-010, SC-011, SC-019 stay in the per-push lane — they are the cross-platform sentinels).

---

## S13. Build integration

* `dsp/tests/CMakeLists.txt` — add the **four** new TUs by name to the enumerated `dsp_systems_tests`
  source list (the Vorago block ending at `:402`), each with a comment naming the spec and the
  criteria it owns, following the Phase 2 block's `:394-401` shape. The list is enumerated, not
  globbed: an unregistered TU silently drops out of the build and its cases never run.
* `dsp/tests/CMakeLists.txt` — add **only**
  `unit/systems/resonance_drift_network_nonfinite_test.cpp` to the
  `-fno-fast-math -fno-finite-math-only` block (ending `:799`), with the standard comment explaining
  why its three siblings are deliberately absent (S12.1).
* `dsp/lint_all_headers.cpp` — add `#include <krate/dsp/systems/resonance_drift_network.h>` to the
  Layer 3 block (after `:178`). **This file is an enumerated list and picks nothing up
  automatically**; without the line the new header gets zero strict-tidy coverage and SC-012's
  clang-tidy gate passes vacuously for it.
* **No change** to `dsp/CMakeLists.txt` — the component is header-only.
* **Conditional (FR-013 Tier 1 only):** `dsp/include/krate/dsp/processors/resonator_bank.h` gains
  `processIndividual` (plus S9.4's OQ-1 state-clear method if the user rules that way), and
  `dsp/tests/unit/processors/resonator_bank_test.cpp` gains its cases. That TU is already registered
  and already sits in the `-fno-fast-math` list — no CMake change.
* **No** plugin, CI, clang-tidy-script, preset or CMake-preset changes: this phase adds no plugin and
  no new target.

Build and run:

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release \
  --target dsp_systems_tests dsp_processors_tests dsp_primitives_tests dsp_core_tests \
           dsp_effects_tests membrum_tests innexus_tests seraphis_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.calibration]" 2>&1 | tail -40
```

Timing runs **alone**, nothing else executing (CLAUDE.md's isolation rule, and
`feedback_cpu_tests_isolation_only.md`):

```bash
node tools/run-cpu-tests.js dsp_systems_tests
```

Pre-commit gates: `node tools/check-portability.js`, `node tools/lint-layers.js`,
`node tools/lint-odr.js`, `node tools/lint-nonfinite-symbols.js`,
`node tools/lint-float-bit-goldens.js`, `node tools/lint-simd-aligned-loadstore.js`, then
`./tools/run-clang-tidy.ps1 -Target dsp`.

---

## S14. Spec corrections applied by this plan

Recorded, not silently applied. Each is a place where reading the code — or checking the arithmetic
of a criterion — changed what the spec said.

**Every correction below is written back into `spec.md` at step T1.5 (S15), before the component is
written.** Recording a correction only here is not enough: FR-062's own rule is that the constant is
"**declared here first**" — in the spec — "and SC-011's expected values are re-derived from the
declaration", and the same applies to every FR and SC below. If the spec text is left standing, T16's
compliance table carries rows whose stated requirement contradicts the shipped assertion, and a
reviewer cannot tell which one is authoritative. T1.5 amends the FR/SC text; this section stays as
the rationale record.

* **C-1 — There is no dry-path scratch buffer, and no heap allocation at all (FR-062, SC-011).**
  The spec declares one heap term of `maxBlockSamples × sizeof(float)` and asserts it in SC-011.
  S5.2 shows per-sample local dry capture already satisfies every aliasing case FR-003 admits,
  including the cross-aliased one. FR-062's own rule ("the constant is **declared here first** and
  SC-011's expected values are re-derived from the declaration") is applied in the direction the
  code goes: the declared footprint is **zero bytes, zero allocations**, and SC-011 is re-derived
  from that. `PrepareConfig::maxBlockSamples` is retained, clamped and reported, documented as
  sizing nothing. This **strengthens** SC-005 and SC-011; it relaxes nothing.
* **C-2 — FR-013 Tier 1 needs a per-slot state clear, which "exactly one additive method" cannot
  deliver.** Coefficient writes do not clear `Biquad` state, so a shared bank cannot reproduce
  FR-042's sleep-edge clear through `processIndividual` alone. The plan recommends reading "one
  method" as one *capability* and adding a five-line `resetResonatorState(size_t)` alongside it, but
  does not take that unilaterally — **OQ-1** (S17).
* **C-3 — `setWanderEnabled(false)` is implemented as depth-zeroing, not as freezing each lane at
  its current offset.** FR-034 says "freezes every lane at its current value without rewinding it".
  The shipped sibling's mechanism is `wanderScale()` multiplying every depth
  (`noise_organism.h:2200-2202`) with the lanes still advancing, and that is what this plan does,
  because it makes SC-014's "wander disabled so anchors are directly readable" **exactly** true
  (offset 0, applied frequency == anchor) where freezing-at-current would leave an arbitrary residual
  offset that SC-014 (a)'s 1-cent bound would then fail. Both readings satisfy SC-002 (b) and
  SC-003 (b); only depth-zeroing satisfies SC-014.
* **C-4 — FR-019's normalisation and FR-045's trim share one `LinearRamp`.** FR-019 requires the
  normalisation ramped over `kGainRampMs`; FR-045 specifies a static scalar with no ramp, which would
  step on a mid-render `setWetGain`. Folding both into one ramp satisfies FR-019 exactly, makes
  `setWetGain` click-free, and costs one ramp instead of two. No criterion changes.
* **C-6 — FR-035 cites an "FR-017 base-level ramp" that FR-017 never defines; the plan builds it
  rather than deleting the citation (S6.6).** FR-035 exempts gain from the slew ceiling because gain
  "is ramped per sample by the FR-041 gate and the FR-017 base-level ramp" (spec.md:560-561). FR-017
  (`:402-406`) defines no ramp, and `ResonatorBank::setGain` writes `gains_[i] = dbToGain(dB)`
  (`resonator_bank.h:367-371`) consumed by a bare multiply at `:504` with no smoother — so a
  mid-render `setPeakLevel(i, −60 → +12)` would step 72 dB in one sample and no criterion exercised
  `setPeakLevel` mid-render at all. The shipped sibling has the missing mechanism
  (`Slot::levelRamp`, `noise_organism.h:1189`, "the SOLE owner of the level", advanced per sample at
  `:502-507`). Phase 3 adds `Peak::levelRamp` on the same shape, folds it into FR-044 step 1, and
  writes only `appliedGainDb − levelDb` to the bank so FR-017's composition is arithmetically
  unchanged. **Spec edits:** FR-017 gains the ramp; FR-044 step 1 gains the `levelGain` factor;
  FR-035's citation becomes true. **New assertion:** SC-002 (d-iii).
* **C-7 — SC-011 and FR-062 are amended to the zero-byte declaration (C-1's spec-side half).** C-1
  derives the declaration; T1.5 writes it into FR-062 (`spec.md:846-856`) and SC-011 (`:1139-1148`),
  which still say `maxBlockSamples × sizeof(float)` and "exactly **1** allocation". Without that edit
  the compliance table would carry an FR-062 row and an SC-011 row contradicting the shipped
  assertions.
* **C-8 — a dormant peak's applied values track target UNSLEWED for the whole dormant interval, not
  only at the wake edge (S6.5).** FR-035 says "one exemption, and only one: the FR-042 wake-edge
  snap"; FR-052 says a dormant peak's reported values "move exactly as they would for an awake peak",
  whose are slew-limited. The plan is wider than both — and stays wider, because FR-042's own
  normative sentence requires it ("written in full on that control step **regardless of how far it
  moved during the dormant interval**"), which a slew-limited dormant peak cannot satisfy after a
  mid-dormancy octave jump. **Spec edits:** FR-035's exemption clause becomes "the FR-042 dormant
  interval, of which the wake-edge snap is the audible boundary"; FR-052's "exactly as an awake
  peak" becomes "as an awake peak does, except that the FR-035 ceiling does not apply while the gate
  sits at exactly zero". **New assertion:** SC-015 (f)'s C-8 arm.
* **C-9 — the sample-rate floor is `kMinUsableSampleRate = 8000.0`, not 1 Hz (S1.2, S2.1 step 1).**
  spec.md:1502 floors at 1 Hz "which keeps every derived coefficient finite". It does not: at 1 Hz
  the derived pair `[20, 0.45]` is inverted, and `std::clamp` with `hi < lo` is undefined behaviour
  (MSVC fires `_STL_VERIFY`). Three clamps in this plan are built from that pair, and the shipped
  `ResonatorBank::clampFrequency` (`resonator_bank.h:542-545`) carries the same latent inversion in a
  component SC-013 forbids amending. **Spec edit:** the Edge Cases bullet names 8 kHz. **New
  assertion:** clamps case (v).
* **C-10 — SC-001 (c) asserts the derived `4.77 dB ± 0.5 dB` relationship, not agreement within a
  measured bound.** Under FR-016's geometric anchors at equal Q the two arms' acoustic content
  differs (`Σf` = 273 Hz vs 4624 Hz), so a correct build differs by ≈ 7.5 dB — larger than the
  4.77 dB defect the arm targets, which any bound "set above the observed maximum" would then absorb.
  Dormanting peaks 4–11 in both arms makes the content identical and leaves only FR-019's `N`.
  **Spec edit:** SC-001 (c) restated. **Side benefit:** it becomes the only assertion of FR-019's
  "`N` is `numPeaks`, not the awake count".
* **C-11 — SC-002's boundary population is `n mod 64 ∈ {0,1,2}`, not `{0,1}`.** The control step runs
  at `controlPhase_ == 0` (S5.1), so the second difference spans the first affected sample in three
  windows. Residue 2 in the interior is 1.6 % of that population against the 0.1 % tail `P` is drawn
  from, so `P` would be computed from discontinuity-carrying samples and `B/P` would collapse toward
  1. **Spec edit:** SC-002's partition sentence. **Consequence:** `kBoundaryRatio` is measured under
  the corrected partition; a figure measured under `{0,1}` may not be carried over.
* **C-12 — SC-002 (c)'s injection is systematic, not one-shot.** At the pinned 60 s duration the
  boundary population is 135 000 samples and `B` is its 99.9th percentile; one jump contributes ≈ 3
  outliers, which cannot move that percentile at all, so `B/P > 10` was **unreachable by
  construction** — the spec's own anti-vacuity proof for its primary no-zipper criterion could never
  have been produced. Alternating ±1-octave `setPeakAnchorHz` jumps on **every** control chunk
  perturb the boundary population uniformly. **Spec edit:** SC-002 (c)'s injection sentence.
* **C-13 — SC-020 (b) isolates the wet path directly, never by differencing.** `mix = 0.5` minus
  `mix = 0` is `0.5·wetTrimmed − 0.5·dry`, which degenerates to `0.5·dry` (RMS = dry − 6.02 dB) when
  the wet path is muted — landing exactly on the "within roughly 6 dB" pass window, so the criterion
  reported a healthy blend for the very failure it names. **Spec edit:** SC-020 (b) compares a
  `mix = 1` render against a `mix = 0` render, with `mix = 0.5` kept as a monotonicity check.
* **C-5 — FR-016's `kDefaultWetGainDb = +30 dB` provisional figure is independently corroborated.**
  Twelve constant-0 dB-peak bandpasses at `Q = 12` on the FR-016 anchors have a summed
  noise-equivalent bandwidth of roughly `(π/2)·Σ(f_i/Q) ≈ 605 Hz` against a 24 kHz white-noise
  bandwidth — ≈ −16 dB — plus `1/sqrt(12)` (−10.8 dB) plus the −6 dB default `peakLevel`, i.e.
  ≈ **−33 dB** of passive attenuation. The provisional +30 dB is the right order; T14 measures it.

---

## S15. Implementation order

Each step ends green; nothing later depends on a step's cleanup.

| # | Step | Verify |
|---|---|---|
| **T0** | **FR-060 stage-cost probe** (`resonance_drift_network_perf_test.cpp`, `[.perf]`), written and run **before the component exists**: the five stages of S10.2, both engine shapes in (a), pan isolated in (b), `decimation` 1 and 17 in (b) | every figure finite and positive; the table and projection printed. **Two separate gates, evaluated in this order (S10.5) — do not substitute one number for the other.** Gate 1, FR-013 verbatim (spec.md:292-296): if **stage (a)'s twelve-single-resonator-bank engine figure exceeds 48 000 ns/block**, take Tier 1 (T1) before writing the component; at 48 000 ns or less FR-011 stands exactly as written and nothing in FR-013 is exercised, whatever the total says. Gate 2, on the **total** projection re-computed with whichever engine gate 1 selected: if it exceeds 80 000 ns, escalate to Tier 2 when (a) dominates, FR-038's static-pan fallback when (b)-isolated dominates, else **STOP AND SURFACE with the measured table**. No cap change, no budget change, no threshold change |
| T1 | *(conditional on T0)* Failing test for `ResonatorBank::processIndividual` (+ OQ-1's state clear) in `resonator_bank_test.cpp`; then implement it, purely additively | the new cases pass; `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`, `membrum_tests`, `innexus_tests`, `seraphis_tests` green **with no edits to existing cases**, before/after diffed (S9.6) |
| **T1.5** | **Write S14's corrections back into `spec.md`** — C-1/C-7 (FR-062 `:846-856` and SC-011 `:1139-1148` → zero bytes, zero allocations), C-6 (FR-017 gains the base-level ramp; FR-044 step 1 gains the `levelGain` factor; FR-035's citation becomes true), C-8 (FR-035's exemption clause; FR-052's dormant wording), C-9 (Edge Cases' sample-rate floor → 8 kHz), C-10 (SC-001 (c) → the derived 4.77 dB ± 0.5 dB relationship), C-11 (SC-002's partition → `{0,1,2}`), C-12 (SC-002 (c) → systematic injection), C-13 (SC-020 (b) → direct isolation). Nothing else in `spec.md` is touched | each edited FR/SC reads consistently with S12.2's assertion for it; `git diff specs/vorago-phase3-resonance-drift/spec.md` shows only the C-numbered clauses. **This step exists so T16's compliance table cannot carry a row whose requirement contradicts the shipped assertion** |
| T2 | Header skeleton: constants, `AnchorMode`, `PrepareConfig`, `Peak`, the full S1.3 surface with bodies for the setters/getters and the S1.4 guards; `prepare`/`reset`/`clearAudioState`/`setSeed`; `processBlock` guard ladder + control grid; a silent `updateControl`/`renderChunk`. Register the four TUs and `lint_all_headers.cpp` (S13) | builds warning-free; `ResonanceDriftNetwork_ControlSurfaceClamps` (all six arms, incl. (v)'s `prepare(0.0)`/`prepare(1.0)` ordering check and (vi)'s log2-Q pin), SC-011, SC-019 (b)(c)(d) pass; `node tools/lint-layers.js`, `lint-odr.js` green |
| T3 | Anchors and modes (S4) + slew limiting (S6.5) | SC-014, SC-018 pass |
| T4 | Lanes: `setWanderRate` mapping + decimation (S6.2, S6.3), the four maps (S6.4) | SC-003 (c)(d), `getLaneDecimation` arm of the clamps case pass |
| T5 | Engine wiring behind the S9.1 seam + FR-014/FR-015 write path (S8) + FR-044 render tail (S5.3) | non-silent render; SC-010, SC-019 (a)(e) pass |
| T6 | Gate + `lastGateTarget` change detection, `refreshGates`, `kWakeSilenceEpsilon` snap, sleep/wake edges, dormancy skip (S7), and the C-6 base-level ramp (S6.6) | SC-015 (a)(c)(d)(e)(h), SC-002 (d) **including (d-ii), (d-iii) and (d-iv)** pass. (d-ii) is run against a deliberately un-guarded `refreshGates()` first and must be **red**, then green with the guard — the same injection discipline SC-017 gets, because an unguarded re-target lowers every step bound and passes everything else |
| T7 | Pan (S2.4 draw + S6.5 equal-power split) | SC-021 (a)(b) pass |
| T8 | `reset`/`clearAudioState` re-apply (S2.2, S2.3) | SC-007, SC-022 pass |
| T8.5 | FR-018 clamp + saturating counter (S5.3's `clampCount`) | `ResonanceDriftNetwork_OutputClampEngages` passes **both** halves — bounded output *and* a counter that actually increments — and the follow-on in-spec render leaves the count unchanged |
| T9 | Non-finite guards end to end | SC-009 passes in the `-fno-fast-math` TU |
| T10 | SC-017 with its **injection arm**, run first on a deliberately mutated write order | mutated build **red**, unmutated build green, both recorded |
| T11 | SC-001, SC-002 (a)(b)(c), SC-006, SC-008 | pass; SC-002 (c)'s injection ratio > 10 recorded |
| T12 | SC-004 baselines measured in isolation, transcribed with the two `static_assert`s | `node tools/run-cpu-tests.js dsp_systems_tests` green; (b) ≥ 10 % and (c) ≥ 40 % savings **measured**, not claimed |
| T13 | `[long]` set: SC-003, SC-015, SC-016 | pass |
| T14 | **Calibration pass** (`[.calibration]`): measure the eleven S12.3 thresholds and FR-045's `kDefaultWetGainDb` across their seed counts; transcribe into the test constants, the header comment next to `kDefaultWetGainDb`, and `compliance.md` | every provisional constant replaced; distributions recorded; re-run T11–T13 against the measured bounds |
| T15 | SC-012 lints + clang-tidy; SC-013 full consumer-suite run + `git diff --stat` | all green, transcribed into `compliance.md` |
| T16 | `compliance.md`: one row per FR and per SC with file:line and **actual measured numbers** | no ✅ without evidence recorded this session |

---

## S16. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R1 | **Tier 0 misses the CPU budget** (S9.3 projects ≈ 91 000 ns vs an 80 000 ns ceiling) — the most likely single failure of this phase | T0 runs before the component exists; the S9.1 seam makes the Tier-1 substitution a one-function change; FR-013 pre-approves it with a measured gate and SC-013 gates the regression |
| R2 | **`std::pow` in 48 lane advances** ≈ 24 000 ns/block at `decimation = 1` (S10.3) — inherent to the shipped lane, which SC-013 forbids amending | probe stage (b) reports pan lanes **isolated**; FR-038's static-pan fallback removes 25 % of the lanes; anything beyond that is stop-and-surface |
| R3 | **`LinearRamp` re-target trap** — calling `gate.setTarget` from `updateControl` turns the 50 ms linear ramp asymptotic (S7.1) | targets set **only** from setters via one `refreshGates()`; SC-002 (d)'s 50 ms ± 5 ms + monotonicity + per-sample-step bound is the gate |
| R4 | **`ResonatorBank::reset()` is a configuration wipe** — a forwarded reset renders silence (S2.2, S7.2) | mandatory re-apply in `reset`, `clearAudioState` **and** the sleep edge; SC-007 (a)'s non-silence arm catches all three |
| R5 | **Frequency clobbers Q, silently** (S8) | fixed write order, Q exempt from change detection, `setDecay` never called; **SC-017 with a mandatory injection arm** is the only criterion that can fail on it |
| R6 | **`g = 0` not bit-equal to Free** if the implementation always runs the log/exp round trip | S4's explicit short-circuit branch; SC-014 (c) asserts bit-equality |
| R7 | **Denormals** in twelve `Q = 100` biquads on a decaying tail | `dsp_test_main.cpp` sets FTZ/DAZ; `detail::flushDenormal` on both outputs; `BrownianDrift` has its own `kDenormalFloor` and `LinearRamp::process` flushes |
| R8 | **Numerical stability at `Q = 100`** near `kMinResonatorFrequency` (`alpha = sin ω / 2Q` is ≈ 1.3e-5 at 40 Hz/48 kHz, so `a1`, `a2` sit close to the unit circle) | `ResonatorBank` is the shipped, Membrum-proven path at exactly these settings, and `Biquad` is direct-form with float state; SC-001 (a)(b)(d) and SC-016 (d) are the boundedness gates, and SC-008 re-runs at 44.1/96 kHz where the pole radius changes |
| R9 | **SC-002's percentile statistic is toolchain-sensitive** | both statistics are the same quantile of their own population; the bound is **measured** across ≥ 8 seeds; the (c) injection arm proves the estimator can see a real discontinuity, and is re-checked whenever the bound moves |
| R10 | **Tier 1 regresses Membrum/Innexus/Seraphis** | purely additive method, existing bodies byte-for-byte unchanged, and the before/after consumer-suite diff (S9.6) — **any moved result is a regression, never a golden to update** |
| R11 | **Portability**: MSVC-green proves nothing | no `std::isnan`/`std::isinf` (S1.4); designated initialisers for `PrepareConfig` (Clang narrowing); no SIMD written (so `lint-simd-aligned-loadstore.js` has nothing to check unless Tier 2 is taken, in which case the kernel is *called*, not written); `node tools/check-portability.js` before every commit; WSL probe if a Linux doubt arises |
| R12 | **`exp2`/`log2` differ in the last bits across toolchains** | never asserted bit-exactly: SC-014 asserts to 1 cent, SC-010 to `kSampleTolerance`, and no criterion in this phase uses a bit-exact float golden (roadmap line 494) |
| R13 | **Two control-rate writes carry neither a slew ceiling nor a per-sample ramp** — the bank's wander-offset gain (≤ 2.1 dB per chunk, only at max depth right after a depth jump) **and the equal-power pan pair** (≤ ≈ 0.07 linear, ≈ 0.6 dB, per 1.33 ms at `panWanderDepth = 1`), S6.6. The base level is no longer among them: C-6 gives it a per-sample ramp | SC-002 (a) runs with **all four** lanes at maximum depth and therefore measures both terms together; a red result is attributed by FR-060's stop-and-surface **naming which term dominates**, never absorbed by a threshold change. Naming only the gain write — as the first draft of this row did — would misdirect a future reader whose red SC-002 (a) is actually the pan multiply |
| R14 | **`setSeed` overwrites `setPeakPan`** (S2.4) | documented in the header ("call `setSeed` first"); SC-021 (b) asserts the seeded defaults, SC-007 (c) asserts `setPeakPan` survives `reset()` |
| R15 | **A criterion that cannot fail on a real defect** — the review of this plan found five: SC-001 (c) absorbed a 7.5 dB systematic offset, SC-002 (c) was arithmetically unsatisfiable, SC-002's partition contaminated its own reference population, SC-020 (b) reported a blend for a mute, and FR-018's clamp had no criterion at all | C-10 … C-13 plus `ResonanceDriftNetwork_OutputClampEngages`; and S12.3's binding rule is extended: before a threshold is transcribed at T14, the arm it belongs to is **run against an injected defect** and must go red |
| R16 | **`refreshGates()` re-targeting an unmoved peak** turns FR-041's 50 ms ramp into a ~213 ms asymptote for every peak *other* than the one being set, under exactly FR-040's documented once-per-block Phase-10 usage (S7.1) | `lastGateTarget` change detection (S7.3); SC-002 (d-ii) drives a second peak once per block throughout the measured ramp and is run red-first against an un-guarded build |

---

## S17. Open questions for the user

Only two, and neither blocks starting T0.

* **OQ-1 — FR-013 Tier 1's "exactly one additive method" vs FR-042's per-slot state clear (S9.4,
  C-2).** Under Tier 1 the sleep edge needs to clear **one** resonator's biquad state, which
  `processIndividual` cannot do and which no existing `ResonatorBank` method exposes (`reset()` is
  whole-bank and a configuration wipe). Options: **(i)** read "one method" as one capability and add
  a five-line `void resetResonatorState(std::size_t index) noexcept` alongside it — **recommended**,
  since it touches no existing behaviour and the SC-013 before/after gate is unchanged; **(ii)** drop
  FR-042's state clear under Tier 1 — rejected here, it would delete a roadmap constraint (lines
  489–493) and turn SC-015 (f) red by design; **(iii)** keep Tier 0 solely as a state container —
  defeats the purpose. The plan proceeds on **(i)** unless the user rules otherwise, and Tier 1 is
  only reached if T0 says so. **Ruled 2026-09-10: (i).** C-1's retained-but-inert
  `PrepareConfig::maxBlockSamples` was put to the user at the same time and **kept as the plan has it**
  (same `PrepareConfig` shape as `NoiseOrganism`, documented as sizing nothing here).
* **OQ-2 — SC-004's ceiling, if T0's projection straddles 80 000 ns after both tiers.** The spec's
  own escalation is Tier 1 → Tier 2 → FR-038's static-pan fallback → **user decision**. This plan
  changes none of that; it flags only that the S9.3/S10.4 projection puts Tier 0 **over** and Tier 1
  comfortably **under**, so the likely outcome is a Tier-1 substitution recorded with measured
  figures rather than a budget conversation. Recorded so the user is not surprised by a shipped
  component whose engine is one bank rather than twelve.

Everything else the roadmap and the 2026-09-10 clarification session left open is decided: all eight
clarification answers (Q1–Q8) are encoded in S4, S5.3, S6, S7, S9 and S11, and D-1 … D-18 are carried
through unchanged except where S14 records a correction.

---

## S18. Review response notes

**No issue from the plan review was rejected.** All seventeen were applied. This section records only
the four places where the review offered two admissible resolutions and the plan had to pick one, so
a later reader does not have to re-derive the choice.

* **The missing base-level ramp (FR-035 / FR-017, S6.6).** Offered: build the ramp, **or** record the
  removal and amend FR-035 to stop citing it. **Built the ramp (C-6).** Amending FR-035 would have
  left `setPeakLevel(i, −60 → +12)` stepping 72 dB in one sample with nothing to smooth it — a real
  click, and resolving a coverage gap by deleting the requirement is exactly the move the brief
  forbids. The bank keeps the wander-only dB so FR-017's composition is arithmetically identical.
* **SC-002 (c)'s unsatisfiable injection (C-12).** Offered: make the injection systematic, **or**
  compute `B/P` over a short window around one jump with a separately derived bound. **Took the
  systematic form.** The windowed alternative would need a second measured bound in a second
  extreme-value regime — the same defect SC-018 (c) had — for no gain; the systematic form reuses
  (a)'s duration, partition, population size and bound unchanged.
* **SC-018 (c)'s bound provenance.** Offered: give (c) its own measured bound, **or** extend the (c)
  render so its population matches SC-002 (a)'s. **Extended the render** (60 s, note step repeated on
  a fixed 1 s schedule). It adds no eleventh provisional threshold, and the claim it makes is
  strictly stronger than the spec's.
* **The dormant-peak slew bypass (C-8).** Offered: run the limiter while dormant and snap only at the
  wake edge, **or** record the deviation and amend FR-035/FR-052. **Recorded and amended.** Running
  the limiter while dormant satisfies FR-035's and FR-052's wording but breaks FR-042's normative
  "regardless of how far it moved during the dormant interval" — after a mid-dormancy octave jump the
  peak would still be fifty control steps from target at the wake edge. FR-042 is the clause with the
  behaviour in it, so the two looser clauses are the ones that move, and SC-015 (f) gains the arm
  that distinguishes the two designs.

One further note on scope. Four of the review's findings (C-10 … C-13, plus FR-018's missing
criterion) are **criteria that could not fail on the defect they name**. They are the same class of
finding Phase 2's calibration pass produced after measurement, and they are why S12.3's bound
provenance rule is extended at R15: an arm is run against an injected defect **before** its threshold
is transcribed, not after a red run.
