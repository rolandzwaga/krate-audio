# Implementation Plan: Vorago Phase 5 — Feedback Ecology

**Spec:** `specs/vorago-phase5-feedback-ecology/spec.md` (1800 lines, read in full this session,
**after** the 2026-09-12 review that added Clarifications Q1–Q8, OQ-1–OQ-3 and DECISIONS-CONFIRMED).
**Roadmap:** `specs/Vorago-roadmap.md` Part A → Phase 5 (lines 265–282); reuse row L5 (line 113);
ODR note (127–129); cross-cutting constraints (492–513) including the Dormancy rule (501–506).
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/feedback_ecology.h`
(header-only), four new test TUs, one new test-helper header, two edits in
`dsp/tests/CMakeLists.txt`, one edit in `dsp/lint_all_headers.cpp`, and the bounded documentation
write-back OQ-3 schedules (roadmap line 272 amendment + deletion of the two ratified `DEVIATION`
annotations in the spec's Traceability table) **together with the six spec edits S16 C-4…C-9 name**,
all in the same T0 docs commit.
**Test target:** `dsp_systems_tests` (all four new TUs). SC-020 regression targets:
`dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11.

> **This document supersedes the previous plan revision in this directory.** That revision was
> written against the pre-review spec and designed the loop resonator as a `SmoothedBiquad` with a
> 20 ms coefficient glide. OQ-1 struck that down: `SmoothedBiquad::setTarget` (`biquad.h:547`) routes
> through `BiquadCoefficients::calculate` (`:673`), which clamps `Q` to `biquad.h`'s own
> `kMaxQ = 30.0f` (`:53`) and silently undercuts the `kMaxResonatorQ = 100.0f` ceiling FR-013
> specifies. Every `SmoothedBiquad` mention in the old revision is dead. The resonator is now a plain
> `Biquad` fed coefficients this component computes itself, with **stepped** retunes (FR-013, FR-076).

---

## S0. Verification ledger — every signature below was read this session

Nothing in this plan is quoted from the spec without re-opening the header. Where the spec and the
code disagree, **the code wins**, and the disagreement is recorded in **S16 (Spec corrections)**.

| Claim this plan is built on | Verified at |
|---|---|
| `SVF`: `prepare(double)` `:161`, `setMode(SVFMode)` `:188`, `setCutoff(float)` `:204`, `setResonance(float)` `:221`, `enableSmoothing(bool, float = kDefaultSmoothingTimeSec)` `:239`, `reset()` `:277`, `resetIntegrators()` `:295`, `snapToTarget()` `:309`, `getCutoff()` `:328`, `getResonance()` `:331`, `isPrepared()` `:337`, `process(float)` `:353` | `primitives/svf.h` |
| `SVF::process` **resets and returns 0.0f on a non-finite input** (`:359-363`), flushes both integrators (`:379-380`), and returns the input unchanged when `!prepared_` (`:349-351`) | `svf.h` |
| `SVF` smoothing: `setCutoff` writes only `gTarget_` when smoothing is on (`:205-207`); `advanceSmoother()` (`:494`) early-outs when settled, else interpolates `g`/`k`, recomputes `a1 = 1/(1+g(g+k))`, `a2`, `a3` and calls `computeMixTargets()`; `computeG()` = `std::tan(kPi·hz/fs)` is called **only** from `setCutoff`/`updateCoefficients` (`:489`) | `svf.h:489-527` |
| `SVF` **Bandpass mix is `m0=0, m1=k=1/Q, m2=0` — constant 0 dB peak gain at ANY Q** (`updateMixCoefficients`, `:565-572`, doc block `:540-551`). Lowpass/Highpass peak at `Q`, so FR-012's `Q <= kButterworthQ` clamp is required for those two modes and redundant (but harmless) for Bandpass | `svf.h` |
| `SVF` constants: `kButterworthQ = 0.7071067811865476f` `:117`, `kMinQ = 0.1f` `:120`, `kMaxQ = 30.0f` `:123`, `kMinCutoff = 1.0f` `:126`, `kMaxCutoffRatio = 0.495f` `:129`, `kDefaultSmoothingTimeSec = 0.005f` `:153`. `SVFMode` `:38` | `svf.h` |
| `CrossfadingDelayLine`: `prepare(double, float maxDelaySeconds)` `:100`, `reset()` `:123`, `setCrossfadeTime(float)` `:141`, `setDelaySamples(float)` `:161`, `setDelayMs(float)` `:195`, `snapToDelaySamples(float)` `:202`, `snapToDelayMs(float)` `:213`, `write(float)` `:223`, `read()` `:233`, `process(float)` `:291`, `isCrossfading()` `:301`, `getCurrentDelaySamples()` `:306`, `maxDelaySamples()` `:312` | `primitives/crossfading_delay_line.h` |
| `setDelaySamples` moves only the **inactive** tap and starts a crossfade only when the target has drifted `kCrossfadeThresholdSamples = 100.0f` (`:78`) from the **active** tap (`:180-190`); below that the active read position does not move | `crossfading_delay_line.h:161-191` |
| The crossfade is advanced **inside `read()`** (`:241-283`); on completion the taps swap and the new inactive tap is synced to `targetDelaySamples_`. `getCurrentDelaySamples()` is the gain-weighted tap average (`:306-309`) — it therefore **cannot advance while `read()` is not called** | `crossfading_delay_line.h` |
| `prepare()` sets `sampleRate_` first (`:101`) and then **overwrites the crossfade time with its own default** via `setCrossfadeTime(kDefaultCrossfadeTimeMs)` (`:119`). `reset()` re-syncs both taps to `targetDelaySamples_` and cancels any crossfade (`:123-133`) | `crossfading_delay_line.h` |
| `DelayLine::prepare` sets `maxDelaySamples_ = sampleRate * maxDelaySeconds` and resizes to `nextPowerOf2(maxDelaySamples_ + 1)` floats (`:267-275`); `nextPowerOf2` at `:26`. This is the component's **entire** heap term | `primitives/delay_line.h` |
| `Biquad`: `setCoefficients(const BiquadCoefficients&)` `:325`, `configure(FilterType,float,float,float,float)` `:330`, `coefficients()` `:341`, `process(float)` `:352`, `processBlock` `:374`, `reset()` `:385`. `struct BiquadCoefficients` `:243` (`b0,b1,b2,a1,a2`, `a0 = 1` implied) | `primitives/biquad.h` |
| `Biquad::process` **resets and returns 0.0f on a non-finite input** (`:353-356`, `detail::isFiniteBits`) and flushes `z1_`/`z2_` (`:367-368`). It does **not** guard non-finite coefficients | `biquad.h` |
| `BiquadCoefficients::calculate` clamps `Q` to `kMaxQ = 30.0f` (`:53`); `Biquad::configure` (`:330`) routes through it. `SmoothedBiquad::setTarget` (`:547`) does too. **Neither is used here** (OQ-1) | `biquad.h` |
| `FilterType::Bandpass` is documented "Constant 0 dB peak gain" (`:71`) | `biquad.h` |
| `ResonatorBank::updateFilterCoefficients` (`:651-682`) builds the RBJ constant-peak-gain bandpass **directly** and calls `Biquad::setCoefficients`, deliberately bypassing `calculate()`'s `kMaxQ = 30` clamp — the exact route FR-013 specifies. Formula transcribed in S4.1 | `processors/resonator_bank.h` |
| `rt60ToQ(float frequency, float rt60Seconds)` (`:92-99`) = `clamp(kPi·f·rt60 / kLn1000, kMinResonatorQ, kMaxResonatorQ)`; `kLn1000 = 6.907755278982137f` `:81` | `resonator_bank.h` |
| Namespace-scope constants: `kMaxResonators = 16` `:39`, `kMinResonatorFrequency = 20.0f` `:42`, `kMaxResonatorFrequencyRatio = 0.45f` `:45`, `kMinResonatorQ = 0.1f` `:48`, `kMaxResonatorQ = 100.0f` `:51`, `kMinDecayTime = 0.001f` `:54`, `kMaxDecayTime = 30.0f` `:57` | `resonator_bank.h` |
| `DCBlocker`: `prepare(double, float cutoffHz = 10.0f)` `:135`, `reset()` `:155`, `setCutoff(float)` `:169`, `process(float)` `:190`. Law `y = x - x1 + R·y1` (`:194-201`), `y1_` flushed (`:203`). **No finiteness branch — "NaN inputs are propagated (FR-016)" (`:188`)** | `primitives/dc_blocker.h` |
| `EnvelopeFollower`: `prepare(double, size_t)` `:106` (**ignores `maxBlockSize`, `(void)maxBlockSize;` `:107`, allocates nothing**), `reset()` `:128`, `processSample(float)` `:164`, `getCurrentValue()` `:192`, `setMode(DetectionMode)` `:202`, `setAttackTime(float)` `:220`, `setReleaseTime(float)` `:227`, `setSidechainEnabled(bool)` `:234`. `DetectionMode::RMS` `:43`. **"Does NOT validate input" (`:163`)**. `kMinAttackMs = 0.1f`/`kMaxAttackMs = 500.0f` `:88-89`, `kMinReleaseMs = 1.0f`/`kMaxReleaseMs = 5000.0f` `:90-91` | `processors/envelope_follower.h` |
| `EnvelopeFollower::processRMS` (`:311-327`): asymmetric smoothing in the squared domain, then `std::sqrt`. Both `envelope_` and `squaredEnvelope_` are flushed (`:183-184`) | `envelope_follower.h` |
| `BrownianDrift`: `prepare(double)` `:121`, `reset()` `:133`, `setSeed(uint32_t)` `:145`, `setSmoothness(float)` `:152`, `setDepth(float)` `:159`, `setMean(float)` `:165`, `process()` `:178`, `processBlock(size_t)` `:194`, `getCurrentValue()` `:212` (`override`, clamped `[-1,+1]`) | `processors/brownian_drift.h` |
| `BrownianDrift` constants: `kTauMin = 0.2f` `:96`, `kTauMax = 30.0f` `:98`, `kInternalStd = 0.5f` `:100`, `kDriftOutputSmoothMs = 150.0f` `:102`, `kControlRateInterval = 32` `:104`, `kDefaultDepth = 1.0f` `:107`. Exact OU AR(1) discretisation `:230-240`; `outputTarget() = clamp(depth_·x_, -1, 1)` `:249-251`; per-sample slew bound proof `:41-58` | `brownian_drift.h` |
| `OnePoleSmoother`: `configure(float smoothTimeMs, float sampleRate)` `:160`, `setTarget` `:170`, `getTarget` `:185`, `getCurrentValue` `:191`, `process()` `:197`, `isComplete()` `:232`, `advanceSamples(size_t)` `:243`, `snapToTarget()` `:257`, `snapTo(float)` `:263`, `reset()` `:275`. **`process()` snaps `current_ = target_` when within `kCompletionThreshold = 0.0001f` (`:55`, `:200-203`)**. Re-counted line by line this session after a review found eight of these citations anchored on the last line of the preceding doc comment rather than on the declaration | `primitives/smoother.h` |
| `calculateOnePolCoefficient` = `exp(-5000/(smoothTimeMs·sampleRate))` (`:77-93`) — the argument is a **per-sample** rate, so a smoother advanced once per 64-sample control step must be configured with `fs / 64` (S5.0, the single most likely implementer trap in this phase) | `smoother.h` |
| `LinearRamp`: `configure(float rampTimeMs, float sampleRate)` `:329`, `setTarget` `:342`, `getTarget` `:358`, `getCurrentValue` `:364`, `process()` `:370`, `isComplete()` `:409`, `snapToTarget()` `:414`, `snapTo(float)` `:421`. `process()` early-outs on `current_ == target_` (`:372-374`) and lands **exactly** on the target (`:379-383`) — which is what makes FR-060's `== 0.0f` sleep test valid | `smoother.h` |
| **`LinearRamp::setTarget` does NOT propagate a NaN target — it MUTES.** `if (detail::isNaN(target)) { target_ = 0.0f; current_ = 0.0f; increment_ = 0.0f; return; }` (`:342-348`, behind `ITERUM_NOINLINE`): an instantaneous, unramped step to **zero**, with no counter, no flag and no way back except another `setTarget`. This is the mechanism behind R-16 and the S5.6 governor guard — a NaN reaching `governorRamp_.setTarget` silences the whole component permanently while every health counter reads clean | `smoother.h:342-355` |
| `EnvelopeFollower::processRMS` **never self-heals from a NaN**: every comparison with NaN is false, so `squared > squaredEnvelope_` takes the release branch and recomputes `squaredEnvelope_ = squared + releaseCoeff_ * (squaredEnvelope_ - squared)` = NaN forever (`:313-322`); `envelope_ = std::sqrt(NaN)` (`:325`) is NaN; `detail::flushDenormal` (`db_utils.h:245-247`) returns NaN unchanged, both range comparisons being false | `envelope_follower.h:312-326` |
| `CrossfadingDelayLine::write(float)` `:223` and `read()` `:233` are **public and separable** — a caller may advance the write head without taking the read. That is what makes FR-063's stale-ring guarantee reachable in O(1) (S3.1) instead of through an O(buffer) `DelayLine::reset()` on the audio thread. `snapToDelaySamples(float)` (`:202-209`) is O(1): `targetDelaySamples_`, both taps, `crossfading_ = false`, `crossfadePosition_ = 0` | `crossfading_delay_line.h` |
| `DelayLine::reset()` is `std::fill(buffer_.begin(), buffer_.end(), 0.0f)` over the **whole power-of-two buffer** (`:281-285`) — 131 072 B per line at 44.1/48 kHz, 524 288 B at 192 kHz (S10). It is **not** an O(1) state clear, and Phase 3's sleep-edge precedent (`resonance_drift_network.h:1680-1686`) clears biquad state only | `primitives/delay_line.h` |
| `SVF::setCutoff` is safe on an **unprepared** instance: `clampCutoff` (`:690`) runs against the constructed default `sampleRate_ = 44100.0` (`:716`), never zero. This is what lets FR-009's contract carry no `prepared_` gate (S2 step 8, S8.6) | `svf.h` |
| `Xorshift32` `:41` (`seed(0)` silently substitutes `kDefaultSeed = 2463534242u`, `:73-75`); `deriveStreamSeed(uint32_t base, size_t salt)` `:102-113`, lowbias32 with a guaranteed-non-zero result | `core/random.h` |
| `detail::isNaN` `:99`, `detail::isFinite(float)` `:118`, `detail::isInf` `:260`, `detail::flushDenormal` `:245`, `detail::kLn2 = 0.693147181f` `:144`, `detail::constexprLn` `:156`, `dbToGain` `:293`, `gainToDb` `:317` | `core/db_utils.h` |
| `FastMath::fastTanh(float)` — `constexpr`, `[[nodiscard]]`, max error 0.05 % for `|x| < 3.5`, **exactly ±1 beyond ±3.5** (`:65-80`), namespace `Krate::DSP::FastMath` (`:27`) | `core/fast_math.h` |
| `equalPowerGains(float, float&, float&)` `:50` and the `std::pair` overload `:64`; `crossfadeIncrement(float, double)` `:89` | `core/crossfade_utils.h` |
| `kMaxAudioFreqHz = 20000.0f` `:25` | `core/audio_constants.h` |
| `FilterFeedbackMatrix<N>`: `static_assert(N >= 2 && N <= 4, "Filter count must be 2-4")` `:72-73`; explicit instantiations for 2/3/4 only `:671-673`; `std::array<SVF, N>` in the filter position — the members `filtersL_` `:296` and `filtersR_` `:305`, and the same type as a parameter at `:287` and `:558` (a review found the earlier `:174` citation pointing at `setFeedbackAmount`'s doc comment; re-read this session); `processNetwork` forms **every** filter input from previous-sample delayed outputs before any filter runs (`:566-599`), applies `std::tanh` **before** the feedback routing (`:609-610`), DC-blocks on the feedback path (`:591`), and advances the delay smoother it did **not** use on the skipped path (`:595-598`) | `systems/filter_feedback_matrix.h` |
| `FeedbackNetwork::kMaxFeedback = 1.2f` "120% for self-oscillation" `:64` — the delay-effect contract this component rejects | `systems/feedback_network.h` |
| `ResonanceDriftNetwork` house pattern actually read: `kControlChunkSamples = 64` + `static_assert` `:135-136`, `kMaxLaneDecimation = 17` + `static_assert` `:139-141`, `kGainRampMs = 50.0f` `:143`, `kOutputClamp = 4.0f` `:144`, `kWakeSilenceEpsilon = 1.0e-6f` `:266`, `kMinUsableSampleRate = 8000.0` + ordering `static_assert` `:283-296`, `PrepareConfig` `:297-306`, the 13-step `prepare()` `:322-405`, `reset()` `:409-...`, the absolute-residue control grid `:530-546`, the normative argument contract `:548-576`, `setNumPeaks` `:578-586`, `setWanderRate` `:715-740`, `setWanderEnabled` "Off zeroes every DEPTH; it does not rewind a lane" `:742-744`, `gateSteady` `:1245-1251`, `refreshGates` + wake edge `:1288-1312`, `updateControl` lane advance + sleep edge `:1567-1686`, `renderChunk` `:1751-...`, the salt table + overlap `static_assert`s `:935-947` | `systems/resonance_drift_network.h` |
| `NoiseOrganism`: `kControlChunkSamples = 64` `:150` (+`static_assert` `:157`), `kDefaultWanderRateHz = 0.03f` `:163`, `kGainRampMs = 50.0f` `:178`, `kOutputClamp = 4.0f` `:180`, `PrepareConfig` `:190-195` (designated initialisers mandatory), `getAllocatedBytes()` `:999`, `getClampEngagementCount()` `:991` | `systems/noise_organism.h` |
| The fault-injection probe pattern: `namespace detail { struct SeraphisEngineNonFiniteProbe; }` declared and never defined `:181-196`, `friend struct detail::SeraphisEngineNonFiniteProbe;` `:1074` | `systems/seraphis_engine.h` |
| `dsp_systems_tests` source list opens `:323`, closes `:436`; the four Phase-3 TUs at `:432-435`. The `-fno-fast-math` block opens `:527` (`if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`), the Phase-3 non-finite entry at `:838`, block closes `:855` | `dsp/tests/CMakeLists.txt` |
| `dsp/lint_all_headers.cpp` — Vorago Phase 2 include `:179`, Phase 3 include `:182`, Layer 4 block opens `:184` | `dsp/lint_all_headers.cpp` |
| `tests/test_helpers/CMakeLists.txt` is `add_library(test_helpers INTERFACE)` with an include directory and **no source list** — a new helper header needs **no CMake edit** (spec correction C-1) | `tests/test_helpers/CMakeLists.txt` |
| `render_fingerprint.h`: `kRenderCheckpoints = 32` `:56`, `kSampleTolerance = 5.0e-4f` `:59`, `kMetricTolerance = 2.5e-4` `:62`, `struct RenderFingerprint` `:64`, `fingerprintRender(std::span<const float>)` `:73`, `compareFingerprints(actual, reference, metricTol, sampleTol)` `:124` | `tests/test_helpers/render_fingerprint.h` |
| `artifact_detection.h`: `ClickDetectorConfig` `:38` (**`sampleRate` defaults to `44100.0f`**), `ClickDetection` `:72`, `ClickDetector(config)` `:103`, `prepare()` `:107`, `detect(const float*, size_t)` `:131` | `tests/test_helpers/artifact_detection.h` |
| `allocation_detector.h`: `AllocationDetector` `:48`, `AllocationScope` `:111` (`getAllocationCount()`, `hadAllocations()`) | `tests/test_helpers/allocation_detector.h` |
| `statistical_utils.h`: `computeMean` `:41`, `computeVariance` `:58`, `computeStdDev` `:76`, `computeMedian` `:90`. `signal_metrics.h`: `calculateCrestFactorDb` `:222`, `calculateSpectralFlatness` `:326`. `spectral_flux.h`: `computeMagnitudeFlux` `:75` | `tests/test_helpers/` |
| `FFT`: `prepare(size_t)` `:147`, `forward(const float*, Complex*)` `:186`, `inverse(const Complex*, float*)` `:216`; `struct Complex` `:55`. **No coherence estimator exists in `tests/test_helpers/`** (directory listed this session) | `primitives/fft.h`, `tests/test_helpers/` |
| Lints that exist and must pass: `tools/lint-layers.js`, `lint-odr.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `lint-arch-guarded-includes.js`, `lint-simd-aligned-loadstore.js`, `tools/check-portability.js`, `tools/run-cpu-tests.js` | `tools/` |
| `resonance_drift_network_perf_test.cpp` idiom: ns per 512-sample block at 48 kHz (`:68-76`), best-of-25 × 500 blocks after 400 warm-up (`:78-84`), `[.perf]` tag, `static_assert`ed baselines evaluated on every CI leg (`:26-28`), the **stop-and-surface rule** verbatim (`:59-65`) | `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp` |

**ODR sweep, re-run this session:** `FeedbackEcology`, `MicroLoop`, `EnergyGovernor`, `EcologyLoop`,
`FeedbackLoop`, `LoopMatrix`, `FeedbackEcologyNonFiniteProbe` — zero hits in `dsp/`, `plugins/`,
`tools/`; the word "ecology" appears nowhere. `CouplingMatrix` exists only as
`plugins/membrum/src/dsp/coupling_matrix.h:21` in `namespace Membrum`. No collision.

---

## S1. Component: `FeedbackEcology`

### S1.1 Header, layer, includes (FR-001)

`dsp/include/krate/dsp/systems/feedback_ecology.h`, **Layer 3**, header-only,
`namespace Krate::DSP`.

```cpp
#pragma once

#include <krate/dsp/core/db_utils.h>          // L0: detail::isFinite/isNaN/isInf, flushDenormal,
                                              //     dbToGain, gainToDb, constexprLn, kLn2
#include <krate/dsp/core/math_constants.h>    // L0: kPi, kTwoPi
#include <krate/dsp/core/random.h>            // L0: deriveStreamSeed
#include <krate/dsp/primitives/biquad.h>      // L1: Biquad, BiquadCoefficients, FilterType
#include <krate/dsp/primitives/crossfading_delay_line.h>  // L1
#include <krate/dsp/primitives/dc_blocker.h>  // L1
#include <krate/dsp/primitives/delay_line.h>  // L1: nextPowerOf2 (:26), for FR-082's footprint
#include <krate/dsp/primitives/smoother.h>    // L1: OnePoleSmoother, LinearRamp
#include <krate/dsp/primitives/svf.h>         // L1: SVF, SVFMode
#include <krate/dsp/processors/brownian_drift.h>     // L2
#include <krate/dsp/processors/envelope_follower.h>  // L2: EnvelopeFollower, DetectionMode
#include <krate/dsp/processors/resonator_bank.h>     // L2: rt60ToQ + the resonator constants ONLY

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
```

Two entries in that list are the result of a review correction, and FR-001's enumeration is amended to
match (S16 C-9): **`primitives/delay_line.h` is added** because `getAllocatedBytes()`'s computation
calls `Krate::DSP::nextPowerOf2` (`delay_line.h:26`, verified this session), which reaches the header
today only transitively through `crossfading_delay_line.h:31`; a future re-plumbing of that header, or
an IWYU / clang-tidy pass, would break this one on a leg the Windows build cannot catch first.
**`core/audio_constants.h` is removed**: it was listed for `kMaxAudioFreqHz`, and no expression in
S1.2–S6 uses it — the cutoff ceiling is `SVF::kMaxCutoffRatio * fs` (S2 step 3). If a later phase
introduces a use, the include comes back with that use.

No `<vector>`, no `<memory>`: the only heap in this component belongs to the six
`CrossfadingDelayLine`s (S10). **No Layer 3 or Layer 4 include** — in particular not
`filter_feedback_matrix.h`, `feedback_network.h` or `flexible_feedback_network.h`. A same-layer
include would be *legal* (`tools/lint-layers.js:74` fails only when `layerIndex(to) > layerIndex(from)`,
and `noise_organism.h:108` already does it) but there is nothing in them to consume: what transfers is
topology knowledge, cited requirement by requirement.

`resonator_bank.h` is included for **`rt60ToQ` and the namespace-scope constants only**. The class
`ResonatorBank` is never instantiated (D-3).

### S1.2 Constants (all `static constexpr`, class scope, `kPascalCase`)

```cpp
// --- topology -------------------------------------------------------------
static constexpr std::size_t kMaxLoops = 6;                 // FR-010, roadmap line 272
static constexpr std::size_t kControlChunkSamples = 64;     // FR-007
static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

static constexpr std::size_t kMaxLaneDecimation = 17;       // FR-055
static_assert(static_cast<float>(kMaxLaneDecimation) * BrownianDrift::kTauMax >= 500.0f,
              "kMaxLaneDecimation must reach the slowest FR-055 wander rate");

// --- the filter stage (FR-011, FR-012) ------------------------------------
static constexpr float kDefaultFilterQ  = SVF::kButterworthQ;   // 0.7071067811865476f
static constexpr float kMinFilterQ      = SVF::kMinQ;           // 0.1f
static constexpr float kMaxFilterQ      = SVF::kButterworthQ;   // FR-012's rung-1 ceiling
static constexpr float kMinCutoffHz     = 20.0f;                // FR-053
static constexpr std::array<float, kMaxLoops> kDefaultLoopCutoffHz =
    {2400.0f, 1700.0f, 1200.0f, 850.0f, 600.0f, 420.0f};        // FR-053

// --- the delay stage (FR-020, FR-022) -------------------------------------
static constexpr float kMinDelayMs      = 10.0f;
static constexpr float kMaxDelayMs      = 500.0f;
static constexpr float kDelayHeadroomMs = 20.0f;
static constexpr float kMaxDelaySeconds = (kMaxDelayMs + kDelayHeadroomMs) / 1000.0f;   // 0.52
static constexpr float kCrossfadeMs     = 20.0f;   // == CrossfadingDelayLine's own default,
                                                   //    restated so it survives a change of that default
static constexpr std::array<float, kMaxLoops> kDefaultLoopDelayMs =
    {41.0f, 67.0f, 109.0f, 173.0f, 281.0f, 449.0f};             // FR-022, mutually prime

// --- the resonator stage (FR-013, Clarifications Q1 / OQ-1) ---------------
static constexpr std::array<float, kMaxLoops> kDefaultLoopResonanceHz =
    {1200.0f, 850.0f, 600.0f, 425.0f, 300.0f, 210.0f};          // one octave below the cutoffs
static constexpr float kDefaultResonanceRt60 = 1.0f;            // seconds; clamped PER CENTRE
static constexpr float kDcBlockerCutoffHz    = 10.0f;           // FR-014

// --- gains and the boundedness ladder (FR-018, FR-035, FR-041..FR-046) ----
static constexpr float kMinLoopGain         = 0.0f;
static constexpr float kMaxLoopGain         = 0.90f;    // roadmap line 272, "gain (< 1)"
static constexpr float kDefaultLoopGain     = 0.72f;
static constexpr float kDefaultLoopInputGain = 1.0f;    // FR-074
static constexpr float kMaxCouplingPerPair  = 0.5f;     // FR-032
static constexpr float kDefaultCoupling     = 0.04f;    // FR-033, the neighbour ring
static constexpr float kMaxTotalLoopGain    = 0.95f;    // FR-035 row-sum ceiling
static constexpr float kOutputClamp         = 4.0f;     // FR-046 (noise_organism.h:180)

// --- the governor (FR-043..FR-045) ----------------------------------------
static constexpr float kDefaultGovernorThresholdDb = -52.0f;  // AMENDED T012, MEASURED - see below
static constexpr float kMinGovernorThresholdDb     = -72.0f;  // AMENDED T012, MEASURED - see below
static constexpr float kMaxGovernorThresholdDb     =   0.0f;
static constexpr float kDefaultGovernorRatio = 8.0f;
static constexpr float kMinGovernorRatio     = 1.0f;
static constexpr float kMaxGovernorRatio     = 20.0f;
static constexpr float kGovernorMinGain      = 0.05f;
static constexpr float kGovernorAttackMs     = 20.0f;
static constexpr float kGovernorReleaseMs    = 800.0f;
static_assert(kGovernorAttackMs  >= EnvelopeFollower::kMinAttackMs
           && kGovernorAttackMs  <= EnvelopeFollower::kMaxAttackMs,  "attack inside the follower's range");
static_assert(kGovernorReleaseMs >= EnvelopeFollower::kMinReleaseMs
           && kGovernorReleaseMs <= EnvelopeFollower::kMaxReleaseMs, "release inside the follower's range");
// AMENDED DURING THE BUILD (T012): the threshold default and range floor above read
// -6.0f and -36.0f until SC-006's sweep was first run. Both were sized from the
// component's INPUT level (spec Assumption 1), but FR-043's tracker reads
// normGain * sum_i b_i, which on the default tables (six Q ~ 100 resonators, ENBW
// 3.3-18.8 Hz of 24 kHz) sits ~30 dB BELOW a broadband drive: measured -42.3 dB at the
// -12 dBFS reference drive, -31.0 dB at 0 dBFS. The whole old [-36, 0] window was
// therefore unreachable and the governor inert at every setting. -52.0f is 10 dB under
// the measured nominal and is the placement that satisfies every SC-006 arm with margin;
// the sweep, the reasoning and the rejected alternatives live in the header's
// DERIVATION TABLE 3, which is the normative record. spec.md FR-044 and Assumption 1
// carry the same amendment.

// --- wander (FR-052, FR-053, FR-055) --------------------------------------
static constexpr float kDefaultWanderRateHz = 0.03f;
static constexpr float kMinWanderRateHz     = 0.002f;
static constexpr float kMaxWanderRateHz     = 1.0f;
static constexpr float kMaxDelayWanderFraction     = 0.5f;
static constexpr float kMaxCutoffWanderOctaves     = 4.0f;
static constexpr float kDefaultCutoffWanderOctaves = 0.5f;
static constexpr std::array<float, kMaxLoops> kDefaultDelayWanderFraction =
    {0.16f, 0.10f, 0.06f, 0.04f, 0.03f, 0.02f};                 // FR-023's DERIVED table

// --- ramps and smoothing (FR-076) -----------------------------------------
static constexpr float kGainRampMs       = 50.0f;   // gates, normGain (noise_organism.h:178)
static constexpr float kMixRampMs        = 20.0f;   // mix, wet trim, input taps
static constexpr float kCouplingSmoothMs = 20.0f;   // ownFb + coupling, CONTROL-rate (S5.0)
static constexpr float kGovernorRampMs   = 20.0f;

// --- life cycle and rate floor --------------------------------------------
static constexpr float  kWakeSilenceEpsilon  = 1.0e-6f;         // FR-060
static constexpr double kMinUsableSampleRate = 8000.0;          // FR-083

/// FR-002: construction is equivalent to prepare(48000.0, PrepareConfig{}). The
/// rate-derived clamp bounds are SEEDED FROM THIS at construction (S1.5) and are
/// never left at 0.0f - std::clamp(v, 20.0f, 0.0f) is UB (R-8) and MSVC's
/// <algorithm> traps it, and SC-017 calls every getter on an unprepared instance.
static constexpr double kConstructionSampleRate = 48000.0;

static_assert(static_cast<double>(kMaxResonatorFrequencyRatio) * kMinUsableSampleRate
                  > static_cast<double>(kMinResonatorFrequency),
              "the resonator clamp pair must stay ordered at the lowest accepted rate");
static_assert(static_cast<double>(SVF::kMaxCutoffRatio) * kMinUsableSampleRate
                  > static_cast<double>(kMinCutoffHz),
              "the cutoff clamp pair must stay ordered at the lowest accepted rate");
static_assert(kConstructionSampleRate >= kMinUsableSampleRate,
              "the construction rate must satisfy both clamp-pair orderings");

// --- output stage (FR-072) ------------------------------------------------
static constexpr float kDefaultMix       = 0.15f;
static constexpr float kDefaultWetGainDb = 0.0f;
static constexpr float kMinWetGainDb     = -24.0f;
static constexpr float kMaxWetGainDb     =  24.0f;
```

Two tables the header must record with their **reasoning**, not merely their values:

* **`kDefaultDelayWanderFraction` is derived, not chosen** (FR-023). `BrownianDrift`'s stationary
  standard deviation is `kInternalStd = 0.5f` (`brownian_drift.h:100`), so the difference between two
  decorrelated lane readings has `sigma_delta = 0.5·sqrt(2) = 0.707`. A crossfade costs a lane
  displacement of `100 / (fraction · baseMs · 0.001 · fs)`. At **44.1 kHz** — the binding rate, since
  100 samples is 2.268 ms there against 2.083 ms at 48 kHz and 0.521 ms at 192 kHz — the table puts
  every loop at `Δlane ≈ 0.25…0.35`, i.e. `0.36…0.49 σ_Δ`. Re-derived here from the header's own
  numbers:

  | loop | base ms | base samples @ 44.1 kHz | fraction | samples per unit lane | Δlane per step | in σ_Δ | peak swing |
  |---|---|---|---|---|---|---|---|
  | 0 | 41 | 1808.1 | 0.16 | 289.3 | 0.346 | 0.49 | ±6.6 ms |
  | 1 | 67 | 2954.7 | 0.10 | 295.5 | 0.338 | 0.48 | ±6.7 ms |
  | 2 | 109 | 4806.9 | 0.06 | 288.4 | 0.347 | 0.49 | ±6.5 ms |
  | 3 | 173 | 7629.3 | 0.04 | 305.2 | 0.328 | 0.46 | ±6.9 ms |
  | 4 | 281 | 12392.1 | 0.03 | 371.8 | 0.269 | 0.38 | ±8.4 ms |
  | 5 | 449 | 19800.9 | 0.02 | 396.0 | 0.253 | 0.36 | ±9.0 ms |

  Every peak swing is strictly inside `[kMinDelayMs, kMaxDelayMs]`, so **no default loop's lane is
  rectified by the clamp**. A flat 0.08 would cost the 41 ms loop `Δlane = 0.69 ≈ 1 σ_Δ` and make
  SC-005 a coin flip; a flat 0.16 would swing the 449 ms loop ±72 ms into the clamp.

* **`kDefaultResonanceRt60 = 1.0f` is silently clamped per centre, and the getter tells the truth**
  (Q1). `rt60ToQ` caps `Q` at `kMaxResonatorQ = 100`, so the longest reachable ring at a centre `f` is
  `rt60_max(f) = 100 · kLn1000 / (kPi · f)`. With `kPi / kLn1000 = 0.4547473`:

  | loop | centre Hz | Q at a 1.0 s request | applied Q | realised RT60 |
  |---|---|---|---|---|
  | 0 | 1200 | 545.70 | 100 | 0.1833 s |
  | 1 | 850 | 386.54 | 100 | 0.2587 s |
  | 2 | 600 | 272.85 | 100 | 0.3665 s |
  | 3 | 425 | 193.27 | 100 | 0.5174 s |
  | 4 | 300 | 136.42 | 100 | 0.7330 s |
  | 5 | 210 | 95.50 | **95.50 (no clamp)** | **1.0000 s** |

  1.0 s is exactly reachable at the lowest default centre and reduced above it — which is why 1.0 s
  and not 2.0 s. `getLoopResonanceRt60(i)` reports the **realised** figure, derived back from the
  applied `Q` (S4.2), never the request.

### S1.3 Nested types

```cpp
/// FR-011. APPEND ONLY - this becomes a persisted plugin parameter at Phase 12.
/// Deliberately NOT named SVFMode / FilterType / FilterMode at namespace scope:
/// SVFMode (svf.h:38) and FilterType (biquad.h:68) are BOTH already at namespace
/// scope in headers this one includes. Nested, the EnvelopeFilter precedent
/// (envelope_filter.h:89).
enum class FilterMode : std::uint8_t { Lowpass = 0, Bandpass = 1, Highpass = 2 };

/// FR-002. Callers MUST use designated initialisers - PrepareConfig{.numLoops = 5}
/// - so no narrowing conversion hides in a positional brace init (Clang errors
/// where MSVC does not). Nested, following NoiseOrganism::PrepareConfig
/// (noise_organism.h:190) and ResonanceDriftNetwork::PrepareConfig (:297).
struct PrepareConfig {
    /// Clamped [64, 8192]; retained and reported, but SIZES NOTHING - the render
    /// is per-sample with local dry capture, so processBlock accepts any
    /// numSamples whatever this says (FR-085).
    std::size_t maxBlockSamples = 2048;
    std::size_t numLoops        = kMaxLoops;   ///< Clamped [1, kMaxLoops].
};
```

`Loop` is a **private** nested struct (S1.5): a namespace-scope type for a private implementation
detail is a future ODR liability for no gain. `MicroLoop` and `EcologyLoop` both swept clean and are
still rejected as top-level names.

### S1.4 Public API — the complete shape the implementer types

```cpp
namespace detail {
/// FR-048 fault-injection probe. DECLARED HERE, DEFINED ONLY BY A TEST TU.
/// FR-047's non-finite trap is unreachable through the public API (S8.5), so this
/// friend is the only way rung 5 is testable at all. It costs nothing at run time
/// and adds no public surface: the library never defines it, so a shipping build
/// has no way to call it. Pattern quoted from seraphis_engine.h:181-196.
/// ODR: swept this session - zero matches in dsp/, plugins/, tools/.
struct FeedbackEcologyNonFiniteProbe;
}  // namespace detail

class FeedbackEcology {
public:
    // ---- constants, FilterMode, PrepareConfig: S1.2 / S1.3 -----------------

    // ---- lifecycle ---------------------------------------------------------
    FeedbackEcology() noexcept = default;

    void prepare(double sampleRate, const PrepareConfig& config) noexcept;  // the ONLY allocator
    void reset() noexcept;

    void processBlock(const float* inL, const float* inR,
                      float* outL, float* outR, std::size_t numSamples) noexcept;
    void processBlockTapped(const float* inL, const float* inR,
                            float* outL, float* outR,
                            float* const* loopTaps, std::size_t numSamples) noexcept;

    // ---- topology ----------------------------------------------------------
    void setNumLoops(std::size_t n) noexcept;                                    // FR-075

    // ---- per-loop stages ---------------------------------------------------
    void setLoopFilterMode(std::size_t loop, FilterMode mode) noexcept;          // FR-011 (stepped)
    void setLoopCutoffHz(std::size_t loop, float hz) noexcept;                   // FR-011/FR-053 base
    void setLoopFilterQ(std::size_t loop, float q) noexcept;                     // FR-012
    void setLoopDelayMs(std::size_t loop, float ms) noexcept;                    // FR-022 base
    void setLoopResonanceHz(std::size_t loop, float hz) noexcept;                // FR-013 (stepped)
    void setLoopResonanceRt60(std::size_t loop, float seconds) noexcept;         // FR-013 (stepped)
    void setLoopGain(std::size_t loop, float gain) noexcept;                     // FR-018 own feedback
    void setLoopInputGain(std::size_t loop, float gain) noexcept;                // FR-074 tap

    // ---- coupling ----------------------------------------------------------
    void setCoupling(std::size_t from, std::size_t to, float amount) noexcept;   // FR-032
    void setCouplingMatrix(
        const std::array<std::array<float, kMaxLoops>, kMaxLoops>& m) noexcept;  // FR-032

    // ---- governor ----------------------------------------------------------
    void setGovernorThresholdDb(float db) noexcept;                              // FR-044
    void setGovernorRatio(float ratio) noexcept;                                 // FR-044

    // ---- life modulation ---------------------------------------------------
    void setLoopDelayWander(std::size_t loop, float fraction) noexcept;          // FR-052
    void setLoopCutoffWander(std::size_t loop, float octaves) noexcept;          // FR-053
    void setWanderRate(float hz) noexcept;                                       // FR-055, sole owner
    void setWanderEnabled(bool enabled) noexcept;                                // FR-056
    void setSeed(std::uint32_t seed) noexcept;                                   // FR-054

    // ---- loop life cycle ---------------------------------------------------
    void setLoopWake(std::size_t loop, float amount) noexcept;                   // FR-060
    void setLoopDormant(std::size_t loop, bool dormant) noexcept;                // FR-060

    // ---- output stage ------------------------------------------------------
    void setMix(float mix) noexcept;                                             // FR-072
    void setWetGain(float db) noexcept;                                          // FR-072

    // ---- FR-070: configuration read surface (all [[nodiscard]] noexcept) ----
    [[nodiscard]] std::size_t   getNumLoops() const noexcept;
    [[nodiscard]] std::size_t   getMaxBlockSamples() const noexcept;
    [[nodiscard]] double        getSampleRate() const noexcept;
    [[nodiscard]] bool          isPrepared() const noexcept;
    [[nodiscard]] float         getWanderRate() const noexcept;
    [[nodiscard]] bool          isWanderEnabled() const noexcept;
    [[nodiscard]] float         getMix() const noexcept;
    [[nodiscard]] float         getWetGain() const noexcept;                       // dB
    [[nodiscard]] float         getGovernorThresholdDb() const noexcept;
    [[nodiscard]] float         getGovernorRatio() const noexcept;
    [[nodiscard]] std::size_t   getNormalisationLoopCount() const noexcept;
    [[nodiscard]] std::size_t   getAllocatedBytes() const noexcept;
    [[nodiscard]] float         getLoopDelayMs(std::size_t loop) const noexcept;      // base
    [[nodiscard]] float         getLoopCutoffHz(std::size_t loop) const noexcept;     // base
    [[nodiscard]] FilterMode    getLoopFilterMode(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopFilterQ(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopResonanceHz(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopResonanceRt60(std::size_t loop) const noexcept;  // REALISED
    [[nodiscard]] float         getLoopGain(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopInputGain(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopDelayWander(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopCutoffWander(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopWakeAmount(std::size_t loop) const noexcept;
    [[nodiscard]] bool          isLoopDormant(std::size_t loop) const noexcept;
    [[nodiscard]] float         getCoupling(std::size_t from, std::size_t to) const noexcept;

    // ---- FR-071: realised-state read surface -------------------------------
    [[nodiscard]] float         getLoopCurrentDelayMs(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopCurrentCutoffHz(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopTargetDelayMs(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopTargetCutoffHz(std::size_t loop) const noexcept;
    [[nodiscard]] std::uint32_t getLoopCrossfadeCount(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopAppliedOwnFeedback(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopAppliedCoupling(std::size_t from,
                                                       std::size_t to) const noexcept;  // S16 C-7
    [[nodiscard]] float         getLoopAppliedTotalGain(std::size_t loop) const noexcept;
    [[nodiscard]] float         getLoopGate(std::size_t loop) const noexcept;
    [[nodiscard]] bool          isLoopEngineActive(std::size_t loop) const noexcept;
    [[nodiscard]] float         getGovernorGain() const noexcept;
    [[nodiscard]] float         getGovernorRms() const noexcept;
    [[nodiscard]] std::size_t   getLaneDecimation() const noexcept;
    [[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept;
    [[nodiscard]] std::uint32_t getNonFiniteResetCount() const noexcept;

private:
    friend struct detail::FeedbackEcologyNonFiniteProbe;   // FR-048
    // ... S1.5
};
```

Everything except `prepare()` is allocation-free; every method, `prepare()` included, is `noexcept`,
lock-free, exception-free and I/O-free. There is **no virtual dispatch** anywhere on the control or
audio path: `BrownianDrift` is held by concrete type, never through a `ModulationSource&` (whose
`getCurrentValue` is virtual, `modulation_source.h:37`).

### S1.5 Private state layout (exact members, in declaration order)

```cpp
    struct Loop {
        // --- audio stages, in signal order ---------------------------------
        SVF                  svf;          // FR-011, smoothing enabled in prepare()
        CrossfadingDelayLine delay;        // FR-020
        Biquad               resonator;    // FR-013, direct RBJ coefficients
        DCBlocker            dcBlocker;    // FR-014

        // --- life-modulation lanes (FR-050) --------------------------------
        BrownianDrift        delayLane;
        BrownianDrift        cutoffLane;

        // --- per-sample ramps (FR-061, FR-074, FR-076) ---------------------
        LinearRamp           gate;         // kGainRampMs
        LinearRamp           inputRamp;    // kMixRampMs
        float                lastGateTarget = 0.0f;   // the load-bearing re-target shadow (S7.3)

        // --- control-rate smoother (FR-076; configured at fs/64 - S5.0) ----
        OnePoleSmoother      ownFbSmoother;           // kCouplingSmoothMs

        // --- configuration (restored by prepare(), preserved by reset()) ----
        float      baseDelayMs       = kDefaultLoopDelayMs[i];      // set by applyDefaults()
        float      baseCutoffHz      = kDefaultLoopCutoffHz[i];
        float      baseLog2Cutoff    = 0.0f;   // cached by setLoopCutoffHz: no std::log2 per step
        float      filterQ           = kDefaultFilterQ;
        FilterMode filterMode        = FilterMode::Lowpass;
        float      resonanceHz       = kDefaultLoopResonanceHz[i];
        float      resonanceRt60Req  = kDefaultResonanceRt60;   // the REQUEST, clamped
        float      appliedResonanceQ = kMinResonatorQ;          // what rt60ToQ actually produced
        float      ownFbTarget       = kDefaultLoopGain;
        float      inputGain         = kDefaultLoopInputGain;
        float      delayWanderFrac   = kDefaultDelayWanderFraction[i];
        float      cutoffWanderOct   = kDefaultCutoffWanderOctaves;
        float      wakeAmount        = 1.0f;
        bool       dormant           = false;

        // --- realised / reported state -------------------------------------
        float         targetDelayMs   = 0.0f;   // FR-062: updated even while skipped
        float         targetCutoffHz  = 0.0f;   // FR-062: updated even while skipped
        std::uint32_t crossfadeCount  = 0;      // FR-071: monotone onset count
        bool          lastCrossfading = false;
        bool          engineActive    = false;  // FR-062's chain-skip flag

        // FR-063's stale-ring guarantee, realised in O(1) (S3.1). While this is
        // non-zero the loop WRITES the delay line but does not READ it, so no
        // pre-clear sample can reach the output; it counts down one per rendered
        // sample and freezes the delay position while it runs.
        std::size_t   readMuteSamples = 0;
    };

    std::array<Loop, kMaxLoops> loops_{};

    // FR-031: the TWO previous-sample vectors.
    std::array<float, kMaxLoops> prevY_{};     // pre-gate  y_j, read by loop j's OWN feedback
    std::array<float, kMaxLoops> prevOut_{};   // post-gate out_j, read by every NEIGHBOUR

    // FR-030 / FR-034 / FR-035: coupling targets, their smoothers, applied values.
    std::array<std::array<float, kMaxLoops>, kMaxLoops>           couplingTarget_{};
    std::array<std::array<OnePoleSmoother, kMaxLoops>, kMaxLoops> couplingSmoother_{};
    std::array<std::array<float, kMaxLoops>, kMaxLoops>           appliedCoupling_{};
    std::array<float, kMaxLoops>                                  appliedOwnFb_{};
    std::array<float, kMaxLoops>                                  appliedTotalGain_{};

    // FR-075: the count mask. One control-grid OnePoleSmoother per SOURCE slot,
    // target 1.0 while the slot is inside numLoops and 0.0 once setNumLoops drops
    // it, at kGainRampMs - the SAME constant FR-076 assigns setNumLoops and the
    // same one the gate uses. It multiplies that slot's OUTGOING coupling inside
    // normaliseRows(), so a dropped loop leaves its neighbours' input sums as a
    // 50 ms glide instead of a one-control-step step, and the surviving rows'
    // FR-035 renormalisation moves continuously with it (S5.3). It reaches
    // EXACTLY 0.0f because OnePoleSmoother::process snaps inside
    // kCompletionThreshold (smoother.h:200-203), which is what keeps SC-015 (d)'s
    // 1e-6 count-inertness claim exact.
    std::array<OnePoleSmoother, kMaxLoops>                        countSmoother_{};

    // Governor (FR-043 - FR-045)
    EnvelopeFollower follower_{};
    LinearRamp       governorRamp_{};        // kGovernorRampMs, advanced per sample
    float            governorThresholdDb_ = kDefaultGovernorThresholdDb;
    float            governorRatio_       = kDefaultGovernorRatio;

    // Output stage (FR-017, FR-072)
    LinearRamp normGainRamp_{};   // kGainRampMs, target 1/sqrt(numLoops)
    LinearRamp mixRamp_{};        // kMixRampMs
    LinearRamp wetGainRamp_{};    // kMixRampMs, carries the LINEAR trim dbToGain(wetGainDb_)
    float      mix_       = kDefaultMix;
    float      wetGainDb_ = kDefaultWetGainDb;

    // Grid, wander, seed
    std::size_t   controlPhase_   = 0;   // FR-007: an ABSOLUTE residue carried ACROSS calls
    std::size_t   laneCounter_    = 0;
    std::size_t   laneDecimation_ = 1;
    float         wanderRateHz_   = kDefaultWanderRateHz;
    bool          wanderEnabled_  = true;
    std::uint32_t seed_           = kDefaultSeed;        // FR-054

    // Cached rate-dependent bounds. Recomputed in prepare() step 3, but SEEDED HERE
    // from kConstructionSampleRate and NEVER left at 0.0f. Both are the upper half
    // of a std::clamp pair whose lower half is a fixed constant, and std::clamp with
    // hi < lo is UB that MSVC's <algorithm> traps with _STL_VERIFY (R-8). SC-017
    // calls getLoopResonanceRt60(i) - which clamps against maxResonanceHz_ - on an
    // UNPREPARED instance, so a 0.0f initialiser is a guaranteed trap, not a latent
    // one. INVARIANT, enforced by the S1.2 static_asserts and restated here: neither
    // member may ever hold a value below its paired minimum, at any point in the
    // object's life. Any getter added later that clamps against a rate-derived bound
    // must be audited against this rule (S9).
    double sampleRate_     = kConstructionSampleRate;
    float  maxCutoffHz_    = std::max(kMinCutoffHz,
                                      static_cast<float>(kConstructionSampleRate) * SVF::kMaxCutoffRatio);
    float  maxResonanceHz_ = std::max(kMinResonatorFrequency,
                                      static_cast<float>(kConstructionSampleRate) * kMaxResonatorFrequencyRatio);

    PrepareConfig config_{};
    bool          prepared_ = false;

    std::size_t   allocatedBytes_   = 0;   // FR-082
    std::uint32_t clampEngagements_ = 0;   // FR-046
    std::uint32_t nonFiniteResets_  = 0;   // FR-047
```

(The `[i]`-indexed default initialisers above are notation, not C++: they are written by a
`constexpr`-fed `applyDefaults()` that the constructor also runs, so an unprepared object already
reports the default tables — see S9's unprepared-state rule.)

Nothing here is heap except the six `CrossfadingDelayLine`s' private `DelayLine::buffer_`. The object
itself is a few kilobytes, dominated by the 42 `OnePoleSmoother`s (36 coupling + 6 own-feedback + 6 count-mask, 20 B each) and the twelve
`BrownianDrift`s — comfortably a member of a Phase-10 voice.

### S1.6 Salt table (FR-054) — APPEND ONLY

```cpp
    static constexpr std::size_t kSaltDelayLane  = 0;    // + loop
    static constexpr std::size_t kSaltCutoffLane = 16;   // + loop
    static constexpr std::size_t kSaltNextFree   = 32;
    static_assert(kSaltDelayLane  + kMaxLoops <= kSaltCutoffLane, "delay salts overlap the cutoff block");
    static_assert(kSaltCutoffLane + kMaxLoops <= kSaltNextFree,   "salt table overflow");
```

Renumbering a base silently changes every Phase-5 render. A later phase adding a lane takes a **new**
base at `kSaltNextFree` and moves that constant up.

---

## S2. `prepare()` — the numbered, comment-annotated step order (FR-005)

Each numbered comment in the header states **what breaks if that step moves**. Three orderings are
load-bearing and are called out by FR-005 (a)/(b)/(c).

```
 1. Rate. sampleRate_ = max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));
    const float fs = static_cast<float>(sampleRate_);
    WHY THE 8 kHz FLOOR AND NOT 1 Hz (FR-083): this component uses
    [kMinResonatorFrequency, kMaxResonatorFrequencyRatio * fs] as its resonator clamp
    pair. At 1 Hz that pair is [20, 0.45] - INVERTED - and std::clamp with hi < lo is
    UB that MSVC's <algorithm> traps with _STL_VERIFY("invalid bounds argument passed
    to std::clamp"). The class-scope static_assert in S1.2 pins the ordering at the floor.

 2. Clamp the caller's request ONCE, here. config_.maxBlockSamples -> [64, 8192];
    config_.numLoops -> [1, kMaxLoops]. applyDefaults() (step 7) never touches
    PrepareConfig fields (the noise_organism.h:1895-1900 rule).

 3. RE-cache the rate-dependent bounds so the ordering std::clamp needs is visible HERE
    rather than inferred from a constant three pages away:
      maxCutoffHz_    = max(kMinCutoffHz,           fs * SVF::kMaxCutoffRatio);
      maxResonanceHz_ = max(kMinResonatorFrequency, fs * kMaxResonatorFrequencyRatio);
    Step 1's floor already makes both max() calls no-ops at every accepted rate.
    "RE-cache", not "cache": both members are SEEDED AT CONSTRUCTION from
    kConstructionSampleRate (S1.5). They are never 0.0f, because SC-017 calls
    getLoopResonanceRt60(i) - which clamps against maxResonanceHz_ - on an
    UNPREPARED instance, and std::clamp(1200.0f, 20.0f, 0.0f) is UB that MSVC
    traps (R-8).

 4. The six loops' audio stages, in signal order, per loop:
      svf.prepare(sampleRate_);
      svf.enableSmoothing(true);                  // FR-011, kDefaultSmoothingTimeSec = 5 ms
      delay.prepare(sampleRate_, kMaxDelaySeconds);
      delay.setCrossfadeTime(kCrossfadeMs);       // *** FR-005 (a) ***
      dcBlocker.prepare(sampleRate_, kDcBlockerCutoffHz);
      // Biquad has no prepare(); coefficients are written in step 10.
    *** FR-005 (a) IS LOAD-BEARING ***: CrossfadingDelayLine::prepare sets sampleRate_
    (:101) and THEN overwrites the crossfade time with its own default (:119). A
    setCrossfadeTime placed before prepare() is discarded, and on a fresh object it
    would have computed its increment against the constructed 44100.0 default (:335).

 5. The twelve wander lanes: delayLane.prepare(sampleRate_); cutoffLane.prepare(sampleRate_).
    BrownianDrift::setDepth is LEFT AT kDefaultDepth = 1.0 - the FR-052/FR-053 depth
    terms are this component's own multipliers, and using the lane's internal depth as
    well would square the control.

 6. The governor's tracker:
      follower_.prepare(sampleRate_, config_.maxBlockSamples);  // maxBlockSize IGNORED (:107)
      follower_.setMode(DetectionMode::RMS);
      follower_.setAttackTime(kGovernorAttackMs);
      follower_.setReleaseTime(kGovernorReleaseMs);
      follower_.setSidechainEnabled(false);   // FR-043: the governor MUST see the sub content,
                                              //         which is Vorago's identity

 7. applyDefaults(). *** FR-005 (c) ***: EVERY call to prepare(), including a re-prepare
    on a live, previously-configured object, restores the FR-013 resonator table (centre
    Hz, RT60 request, filter Q, filter mode), the FR-022 delay table, the FR-053 cutoff
    table, the FR-052/FR-053 wander tables, the FR-033 coupling ring, kDefaultLoopGain,
    kDefaultLoopInputGain, kDefaultMix, kDefaultWetGainDb, the governor threshold/ratio,
    and wake = 1 / dormant = false on every loop - DISCARDING whatever the caller had
    configured. prepare() is therefore NOT idempotent with respect to configuration; only
    reset() is (FR-004). It does NOT touch seed_ (step 14 re-applies it) and does NOT
    touch PrepareConfig fields (step 2 owns those).

 8. prepared_ = true.  READABILITY ONLY - this step is order-INDEPENDENT. NO SETTER IS
    GATED ON prepared_ (FR-006 "callable at any time"; FR-009's contract has exactly
    three rejection rules and "unprepared" is not one of them - S8.6). An earlier
    revision of this plan carried the comment "before step 9, or every configuration
    push below is a no-op", which implied a gate no FR states and which contradicted
    S9's unprepared-state rule; a review struck it. Every setter is safe on an
    unprepared object: SVF::setCutoff clamps against the SVF's own constructed
    44100.0 (svf.h:690, :716), updateResonator() uses the S1.5-seeded bounds, and
    every ramp/smoother write is superseded by steps 11-12's snap.

 9. setWanderRate(kDefaultWanderRateHz). THE SINGLE OWNER of laneDecimation_ and of every
    lane's setSmoothness (FR-055). Never recomputed anywhere else, so prepare() and a
    later caller cannot disagree about the mapping.

10. Resonator coefficients: for every loop, updateResonator(i) (S4.1).

11. Control-rate smoothers, configured at fs / kControlChunkSamples (S5.0) and SNAPPED:
      ownFbSmoother.configure(kCouplingSmoothMs, fs / 64.0f);  .snapTo(ownFbTarget);
      couplingSmoother_[f][t].configure(same);                 .snapTo(couplingTarget_[f][t]);
      countSmoother_[i].configure(kGainRampMs, fs / 64.0f);
      countSmoother_[i].snapTo(i < config_.numLoops ? 1.0f : 0.0f);   // FR-075, S5.3
    The count mask is SNAPPED here, not ramped, which is what makes a count chosen
    through PrepareConfig{.numLoops = n} exact from sample 0 - SC-015 (d)'s first arm
    depends on it. Then one normaliseRows() so appliedOwnFb_ / appliedCoupling_ /
    appliedTotalGain_ are correct before the first sample.

12. Per-sample ramps, configured at fs and SNAPPED - the
    FlexibleFeedbackNetwork::snapParameters() idiom (:453): a prepared network must be
    able to reach steady state without a ramp.
      normGainRamp_.configure(kGainRampMs, fs);     .snapTo(1.0f / sqrt(numLoops));
      governorRamp_.configure(kGovernorRampMs, fs); .snapTo(1.0f);
      mixRamp_.configure(kMixRampMs, fs);           .snapTo(mix_);
      wetGainRamp_.configure(kMixRampMs, fs);       .snapTo(dbToGain(wetGainDb_));
      per loop: inputRamp.configure(kMixRampMs, fs);  .snapTo(inputGain);
                gate.configure(kGainRampMs, fs);      .snapTo(gateSteady(i));
                lastGateTarget = gateSteady(i);   // MUST start in sync, or the first
                                                  // refreshGates() would skip a real re-target
                engineActive   = (gateSteady(i) > 0.0f);

13. Counters, grid, previous-sample vectors, footprint:
      controlPhase_ = 0; laneCounter_ = 0; clampEngagements_ = 0; nonFiniteResets_ = 0;
      prevY_.fill(0.0f); prevOut_.fill(0.0f);
      per loop: clearLoopAudio(i);   // FR-019's one owner
                delay.reset();       // the full O(buffer) wipe, which clearLoopAudio
                                     //   deliberately does NOT do (S3.1): prepare() is a
                                     //   control-thread call, not an audio-thread one
                readMuteSamples = 0; // nothing stale is left to mute after the wipe
                crossfadeCount  = 0; lastCrossfading = false;
      allocatedBytes_ = kMaxLoops
                      * nextPowerOf2(size_t(sampleRate_ * kMaxDelaySeconds) + 1)
                      * sizeof(float);                              // S10, FR-082

14. setSeed(seed_). *** FR-005 (b) *** LAST, for the reason NoiseOrganism records
    (:299-302) and Phase 3 repeats (:399-402): BrownianDrift::setSeed reseeds the RNG and
    the mandatory per-lane reset() that follows it snaps the output smoother, so a seed
    distributed before step 5's lane prepare() would be discarded.

15. snapControlState(): evaluate the FR-052/FR-053 mappings once from the just-reset lanes
    (both sit at mean 0, so both map to base), store them into targetDelayMs /
    targetCutoffHz, and write them to every loop's audio objects with snapToDelayMs and
    setCutoff + snapToTarget. Without this the first control step would issue a setDelayMs
    against taps prepare() left at 0 samples and every loop would open with a spurious
    crossfade from silence.
```

`prepare()` touches the heap only inside step 4's `delay.prepare` (S10). Everything else is a
fixed-size member.

---

## S3. `reset()`, `setSeed()`, and the one owner of the loop's audio clear

### S3.1 `clearLoopAudio(i)` — FR-019, one owner, four callers, and **O(1) on the audio thread**

**The correction this section carries.** FR-019 names `delay_[i].reset()` as the delay half of the
clear. That call is `CrossfadingDelayLine::reset` → `DelayLine::reset()` →
`std::fill(buffer_.begin(), buffer_.end(), 0.0f)` (`delay_line.h:281-285`) over the **whole
power-of-two buffer**: by S10's own table, **131 072 B per loop** at 44.1/48 kHz and **524 288 B per
loop** at 192 kHz. Two of `clearLoopAudio`'s four callers run **on the audio thread**: the FR-063
sleep edge inside `updateControl()` and the FR-047 trap inside the per-sample body of `renderChunk`.
`setNumLoops(6 → 1)` drops five loops whose gates settle at zero on the same sample — SC-007 and
SC-022 (c) both drive exactly that — so six clears can land inside **one 64-sample control chunk**:
786 KB of stores at 48 kHz, 3.1 MB at 192 kHz. Against S14's own basis (106 666 ns absolute ceiling
per 512-sample block at 48 kHz, i.e. **8 889 ns per 64-sample chunk**), a *single* 131 KB fill already
exceeds the whole chunk budget. This is not covered by house precedent: Phase 3's sleep-edge clear
(`resonance_drift_network.h:1680-1686`) clears biquad state, which is O(1). None of SC-004's arms
(a)–(d) contains a sleep, wake or trap transition, so the spike would have been unmeasured by design.

**The fix keeps FR-063's audible property and drops the fill.** What FR-063 actually requires is that
*a woken loop refills from its input tap rather than replaying the stale ring* — the listener-facing
statement the Dormancy-rule deviation is justified in. That is reachable in O(1), because
`CrossfadingDelayLine::write(float)` (`:223`) and `read()` (`:233`) are **separable public calls**: a
loop that writes but does not read cannot emit anything the buffer already held. So the delay half of
the clear becomes a **read-mute window** of exactly one delay length, and the buffer is never wiped on
the audio thread at all.

```cpp
/// FR-019's ONE owner. O(1): no call in this body touches more than a handful of
/// floats. RT-safe on the audio thread, which prepare()/reset()'s extra delay.reset()
/// (S2 step 13, S3.2) deliberately is not.
void clearLoopAudio(std::size_t i) noexcept {
    Loop& L = loops_[i];
    L.svf.reset();          // svf.h:277 - clears both integrators AND snaps g/k/mix to target
    L.resonator.reset();    // biquad.h:385 - clears z1_/z2_ and LEAVES the coefficients; a
                            //   hard-swap resonator has no coefficient smoothers to snap (FR-013)
    L.dcBlocker.reset();    // dc_blocker.h:155 - x1_ = y1_ = 0

    // The delay half, O(1). snapToDelaySamples (:202-209) writes targetDelaySamples_,
    // both taps, crossfading_ = false and crossfadePosition_ = 0 - everything
    // CrossfadingDelayLine::reset() does EXCEPT the O(buffer) DelayLine::reset().
    // Snapping to the CURRENT position (not the target) means the mute length below
    // is exact.
    const float dsamples = L.delay.getCurrentDelaySamples();       // :306
    L.delay.snapToDelaySamples(dsamples);
    L.readMuteSamples = static_cast<std::size_t>(std::ceil(dsamples)) + 1u;

    prevY_[i]   = 0.0f;     // FR-031's two previous-sample vectors
    prevOut_[i] = 0.0f;
    L.lastCrossfading = false;
}
```

**Why one delay length is exactly enough, and why the position must be frozen while it runs.** If the
loop resumes writing at buffer index `W0` and the read head sits `D` samples behind the write head,
then at sample `t` after the clear the read lands at `W0 + t − D`, which is at or past `W0` — i.e.
inside data written after the clear — as soon as `t >= D`. That is only true if `D` does not grow
during the window, so `renderChunk` **freezes the delay position** while `readMuteSamples > 0`: the
control step skips `setDelayMs` for that loop (S5.2 step 4d) exactly as it already does for a skipped
one, and the `+1u` absorbs the fractional read. The target getters keep moving throughout (FR-062), and
the position resumes tracking the moment the window closes. The worst-case freeze is one delay length
— 41 ms on loop 0, 449 ms on loop 5 — against a wander lane whose fastest correlation time is 1 s, so
no crossfade is lost, only deferred.

Per-sample cost of the whole mechanism: **one predicted branch per active loop** (S6 step 3). Nothing
is amortised, nothing is scheduled, and there is no state machine to get wrong.

**`prepare()` and `reset()` additionally call `L.delay.reset()`** (S2 step 13, S3.2 step 2) — those two
are control-thread calls with no real-time contract, they are the only paths that must leave the buffer
literally zeroed for `getAllocatedBytes()`-style determinism and for SC-009 (b)'s reproducibility, and
they set `readMuteSamples = 0` because there is nothing stale left to mute.

Exactly four callers of `clearLoopAudio`, so the four paths cannot drift apart: `prepare()` (step 13),
`reset()`, the FR-063 sleep edge (S7.2), and the FR-047 non-finite trap (S8.5). It does **not** reset
`crossfadeCount` — that counter is monotone for the life of the object except across `prepare()`
and `reset()`.

**What this changes downstream, stated so no reader has to infer it:** SC-014 (d)'s "tap below
−80 dBFS for the first 500 ms after a silent wake" is satisfied by the mute window for every loop
(449 ms on the longest, and the fresh data behind it is silence), and SC-004 gains an arm (f) that
measures the block containing a simultaneous six-loop sleep edge and one containing a trap fire against
the same 106 666 ns ceiling (S14.2). The deviation from FR-019's literal `delay_[i].reset()` is
recorded in S16 C-8.

### S3.2 `reset()` — FR-004, configuration-preserving

Rewinds **all audio and modulation state** while preserving every configured value, identical in
meaning to `NoiseOrganism::reset()` and `ResonanceDriftNetwork::reset()` (`:409-419`) so Phase 10 can
reset all three siblings at the same moment:

1. `controlPhase_ = 0; laneCounter_ = 0; clampEngagements_ = 0; nonFiniteResets_ = 0;`
2. `for i: clearLoopAudio(i); loops_[i].delay.reset(); loops_[i].crossfadeCount = 0;`
   `loops_[i].readMuteSamples = 0;` — the extra `delay.reset()` is the O(buffer) wipe S3.1 keeps off
   the audio thread; `reset()` is a control-thread call, and a literally-zeroed buffer is what
   SC-009 (b)'s "`reset()` then re-render reproduces the first render" needs.
3. `follower_.reset();`
4. Ramps **snapped**, not ramped: `normGainRamp_.snapTo(1/sqrt(numLoops))`,
   `governorRamp_.snapTo(1.0f)`, `mixRamp_.snapTo(mix_)`,
   `wetGainRamp_.snapTo(dbToGain(wetGainDb_))`; per loop `inputRamp.snapTo(inputGain)`,
   `gate.snapTo(gateSteady(i))`, `lastGateTarget = gateSteady(i)`,
   `engineActive = (gateSteady(i) > 0.0f)`.
5. Control-rate smoothers snapped to their targets — `ownFbSmoother`, every `couplingSmoother_[f][t]`,
   and `countSmoother_[i].snapTo(i < config_.numLoops ? 1.0f : 0.0f)` — then one `normaliseRows()`.
6. `setSeed(seed_)` — re-derives every lane stream from the stored seed. **This is the one line that
   makes a render reproducible from the top** (SC-009 (b)).
7. `snapControlState()` (S2 step 15).

**`reset()` is the test suite's only exact way to put a configuration in force from sample 0**, and
several criteria depend on that: it snaps every per-sample ramp (`inputRamp` included) and every
control-rate smoother (`couplingSmoother_` included) to the value the caller most recently set, and
clears all audio in the same breath. The pattern **configure → `reset()` → render** is therefore
mandatory wherever an arm asserts on a configuration being exact from the first sample — SC-002 (a)'s
exact-zero taps and SC-015 (d)'s first arm both use it (S12).

`reset()` does **not** restore the FR-013/FR-022/FR-033/FR-052/FR-053 default tables, the coupling
matrix, loop gains, `mix` or `wetGain`. `prepare()` is the only path back to those (FR-005 (c),
SC-011's re-prepare arm).

### S3.3 `setSeed()` — FR-054

```cpp
void setSeed(std::uint32_t seed) noexcept {
    seed_ = seed;
    for (std::size_t i = 0; i < kMaxLoops; ++i) {
        loops_[i].delayLane.setSeed(deriveStreamSeed(seed_, kSaltDelayLane + i));
        loops_[i].delayLane.reset();      // MANDATORY: setSeed reseeds the RNG but does NOT
        loops_[i].cutoffLane.setSeed(deriveStreamSeed(seed_, kSaltCutoffLane + i));
        loops_[i].cutoffLane.reset();     //   rewind the walk (brownian_drift.h:145-148 vs :133-135)
    }
}
```

`deriveStreamSeed`'s guaranteed-non-zero result is load-bearing, not hygiene: `Xorshift32::seed(0)`
silently substitutes its own default (`random.h:73-75`), so two lanes hashing to 0 would **collapse
onto one stream**. `setSeed` mid-render is legal and does **not** clear the audio state — the render
continues without a discontinuity while the modulation restarts. That is exactly why it is declared
**stepped** (FR-076) and is separate from `reset()`.

---

## S4. The resonator: direct RBJ coefficients, and a truthful RT60 getter

### S4.1 `updateResonator(i)` — FR-013's coefficient path

Transcribed from `ResonatorBank::updateFilterCoefficients` (`resonator_bank.h:651-682`), which is the
route that actually reaches `kMaxResonatorQ = 100`. `BiquadCoefficients::calculate` clamps `Q` at
`kMaxQ = 30.0f` (`biquad.h:673`); `Biquad::configure` (`:330`) and `SmoothedBiquad::setTarget`
(`:547`) both route through it. **Neither is called anywhere in this component.**

```cpp
void updateResonator(std::size_t i) noexcept {
    Loop& L = loops_[i];
    const float fs = static_cast<float>(sampleRate_);

    // Both clamps use the pair step 3 cached, whose ordering the class static_assert pins.
    const float f = std::clamp(L.resonanceHz, kMinResonatorFrequency, maxResonanceHz_);
    const float q = std::clamp(rt60ToQ(f, L.resonanceRt60Req), kMinResonatorQ, kMaxResonatorQ);
    L.appliedResonanceQ = q;              // the TRUTH the getter reports back (S4.2)

    const float omega    = kTwoPi * f / fs;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha    = sinOmega / (2.0f * q);

    const float a0    = 1.0f + alpha;
    const float invA0 = 1.0f / a0;

    BiquadCoefficients c;
    c.b0 =  alpha * invA0;
    c.b1 =  0.0f;                          // the RBJ bandpass has no b1 term
    c.b2 = -alpha * invA0;
    c.a1 = (-2.0f * cosOmega) * invA0;
    c.a2 = ( 1.0f - alpha)    * invA0;
    L.resonator.setCoefficients(c);        // biquad.h:325 - NEVER configure()
}
```

`rt60ToQ` already clamps to `[kMinResonatorQ, kMaxResonatorQ]` (`resonator_bank.h:97`); the outer
`std::clamp` is written anyway so the ceiling this component depends on is visible at the call site.

**Why an arbitrarily high Q is safe here.** The RBJ bandpass is documented "Constant 0 dB peak gain"
(`biquad.h:71`) and is built normalised by `a0 = 1 + alpha`: its magnitude is exactly 1 at the centre
and strictly below 1 everywhere else, whatever `Q` is. A higher `Q` lengthens the ring without raising
the peak. That is **rung 2 of FR-041**.

**Retunes are a stepped hard swap.** `setLoopResonanceHz` and `setLoopResonanceRt60` each store the
new configuration and call `updateResonator(i)` once; the new coefficients take effect on the very
next sample with no interpolation. The centre and RT60 are user-set controls, not wander targets, so a
retune is a rare control event and a stepped swap is proportionate (OQ-1, FR-076). FR-041 is
unaffected: there is no interpolated intermediate coefficient set, because each end point is
independently unity-peak.

### S4.2 The truthful RT60 getter (Q1)

```cpp
[[nodiscard]] float getLoopResonanceRt60(std::size_t loop) const noexcept {
    if (loop >= kMaxLoops) return 0.0f;                     // FR-009 neutral, no indexing
    const Loop& L = loops_[loop];
    // maxResonanceHz_ is SEEDED AT CONSTRUCTION (S1.5) and is never 0.0f, so this
    // clamp pair is ordered on an UNPREPARED instance too - which SC-017 exercises
    // directly. With a 0.0f initialiser this line was std::clamp(1200.0f, 20.0f, 0.0f):
    // UB, trapped by MSVC's _STL_VERIFY, and where it is not trapped it returns 0 and
    // the division below yields inf (R-8).
    const float f = std::clamp(L.resonanceHz, kMinResonatorFrequency, maxResonanceHz_);
    // Exact inverse of rt60ToQ (resonator_bank.h:92-99): Q = pi*f*rt60 / ln1000.
    return (L.appliedResonanceQ * kLn1000) / (kPi * f);
}
```

**`appliedResonanceQ` is bookkeeping, and bookkeeping is not evidence.** This getter is derived from
the value `updateResonator` stored *before* it wrote the coefficients (S4.1), so a build that
regressed to `L.resonator.configure(FilterType::Bandpass, f, q, 0.0f, fs)` — the `kMaxQ = 30` route
OQ-1 struck down — would still store `appliedResonanceQ = 95.5` and still report 1.0 s while the
filter in force ran at `Q = 30`, i.e. 0.314 s. **SC-023 exists because of that**: it measures the
realised ring rather than the field, and requires this getter to agree with the measurement (S12.3).

It reports the decay implied by the coefficient set **actually in force**, derived back from the
clamped `Q` — never an echo of the request. On the default table that is the six figures in S1.2:
1.0 s only at 210 Hz, 0.183 s at 1200 Hz. It is deliberately **not** re-clamped into
`[kMinDecayTime, kMaxDecayTime]` on the way out: a second clamp would restore exactly the lie this
getter exists to prevent.

---

## S5. The control step

### S5.0 The single most likely implementer trap: control-rate smoother configuration

`calculateOnePolCoefficient(smoothTimeMs, sampleRate) = exp(-5000/(smoothTimeMs · sampleRate))`
(`smoother.h:77-93`) — the second argument is a **per-sample** rate. The 42 coupling / own-feedback / count-mask
`OnePoleSmoother`s are advanced **once per 64-sample control step**, so they MUST be configured with

```cpp
    smoother.configure(kCouplingSmoothMs, static_cast<float>(sampleRate_) / kControlChunkSamples);
```

Configuring them with `fs` would make the realised time constant `64 × 20 ms = 1.28 s`. No criterion
in the spec measures the coupling glide directly, so that bug would ship silently — which is why it is
called out here rather than left to the reader. Every ramp advanced **per sample** (`gate`,
`inputRamp`, `normGainRamp_`, `mixRamp_`, `wetGainRamp_`, `governorRamp_`) is configured with `fs`.

### S5.1 The chunk loop (FR-007) — an absolute residue, not a block-relative grid

```cpp
void processBlockTapped(const float* inL, const float* inR, float* outL, float* outR,
                        float* const* loopTaps, std::size_t numSamples) noexcept {
    // FR-003's guard ladder, in THIS order.
    if (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) return;  // nothing
    if (numSamples == 0) return;                                     // no control step consumed
    if (!prepared_) { std::fill_n(outL, numSamples, 0.0f);           // exactly numSamples zeros
                      std::fill_n(outR, numSamples, 0.0f); return; } // ...and no state advance

    std::size_t done = 0;
    while (done < numSamples) {
        if (controlPhase_ == 0) updateControl();
        const std::size_t chunk = std::min(numSamples - done, kControlChunkSamples - controlPhase_);
        renderChunk(inL + done, inR + done, outL + done, outR + done, loopTaps, done, chunk);
        controlPhase_ = (controlPhase_ + chunk) % kControlChunkSamples;
        done += chunk;
    }
}

void processBlock(const float* inL, const float* inR, float* outL, float* outR,
                  std::size_t numSamples) noexcept {
    processBlockTapped(inL, inR, outL, outR, nullptr, numSamples);   // FR-073: ONE function body
}
```

`controlPhase_` is a residue carried **across calls** (`resonance_drift_network.h:530-546`). A
block-relative grid runs two control steps for a 36 + 28 split where an unsplit 64 runs one, and
SC-010 fails.

### S5.2 `updateControl()` — the order is normative

```
(1) The decimated lane advance, FIRST, before anything reads a lane.
      if (laneCounter_ == 0)
          for each loop: delayLane.processBlock(64); cutoffLane.processBlock(64);
      laneCounter_ = (laneCounter_ + 1) % laneDecimation_;

    UNCONDITIONAL in three separate ways, each with a criterion behind it:
      * a ZERO-DEPTH lane advances, so raising a depth resumes the trajectory an
        always-on lane would have been on (FR-056);
      * a DORMANT loop's lanes advance - the Dormancy rule's other half, and what makes a
        woken loop's delay and cutoff arrive already displaced (FR-062, SC-014 (b2));
      * setWanderEnabled(false) scales the DEPTHS via wanderScale(); it does not freeze
        the motion (FR-056, verbatim from resonance_drift_network.h:742-744).
    The fixed 64-sample argument is what makes the decimation sample-rate independent:
    between advances a lane HOLDS its output, so it experiences 64/fs seconds of its own
    evolution per laneDecimation_*64/fs seconds elapsed, and fs cancels.

(2) Advance the 42 control-rate smoothers (36 coupling + 6 own-feedback + 6 count-mask; FR-034, FR-075). EVERY pair advances on EVERY control
    step whether or not its path contributed - the else branch at
    filter_feedback_matrix.h:595-598 exists precisely because a smoother that stops
    advancing desynchronises from its neighbours and steps when its path re-engages.
    THE ONE CARVE-OUT (OQ-2 lever 3): a smoother whose current value is BIT-EXACT at its
    target may skip its process() call, because advancing a settled one-pole is a no-op by
    construction - the value it would write is the value already there:
        static float advance(OnePoleSmoother& s) noexcept {
            return (s.getCurrentValue() == s.getTarget()) ? s.getTarget() : s.process();
        }
    Exact, because OnePoleSmoother::process() SNAPS current_ = target_ inside
    kCompletionThreshold = 1e-4 (smoother.h:199-202), so a settling smoother reaches
    bit-equality rather than approaching it. A CPU lever, never a licence to skip an
    unsettled smoother.

(3) normaliseRows() - FR-035 (S5.3).

(4) Per loop, for ALL kMaxLoops (not just numLoops - a slot above the count must still fade
    and take its sleep edge, FR-075):
      a. map the two lanes into targetDelayMs / targetCutoffHz (S5.4). DONE FOR EVERY LOOP,
         INCLUDING A SKIPPED ONE - FR-062 makes the target getters the only observable proof
         the lanes are alive, and SC-014 (b) asserts on them.
      b. THE SLEEP EDGE, before any write (S7.2).
      c. if (!engineActive) continue;   // FR-062: nothing reaches the audio objects
      d. if (readMuteSamples == 0) { delay.setDelayMs(targetDelayMs); then the
         crossfade-onset count (S5.5) }
         THE FREEZE IS LOAD-BEARING, not an optimisation: S3.1's read-mute window is
         only exactly one delay length if the delay position does not GROW while it
         runs. Skipping the write for at most one delay length costs a deferred
         crossfade against a lane whose fastest correlation time is 1 s.
      e. svf.setCutoff(targetCutoffHz);    // the SVF's own per-sample smoother carries it

(5) The governor (S5.6).
```

Step (4b) sits **before** (4d)/(4e) so a loop that just went to sleep falls through the
`engineActive` early-out on the same step instead of being written and then cleared. The **wake** edge
is not here at all — it lives in `refreshGates()`, i.e. in the setter (S7.3).

### S5.3 `normaliseRows()` — FR-035, the structural half of boundedness

**The count mask, and why the earlier shape was a defect.** An earlier revision keyed the exclusion of
an out-of-count slot on `i >= config_.numLoops` and zeroed that slot's coefficients the instant
`setNumLoops` returned. That is wrong twice over against FR-075 and FR-076. FR-076 declares
`setNumLoops` **smoothed** at `kGainRampMs`; FR-075 says a dropped loop is dropped "exactly the way a
sleep is done" and is "excluded from their neighbours' coupling **as soon as the gate reaches zero**,
because `prevOut_i` is then held at `0.0f`". Instant zeroing removes a dropped loop's contribution from
every neighbour's input sum on the *next control step* — ≤ 1.33 ms, some 50 ms before the gate reaches
zero — and removes it by deleting the coefficient rather than by the gate. At SC-001 (d)'s fixture,
where **every** off-diagonal pair is pre-seeded at `kMaxCouplingPerPair = 0.5` and `setNumLoops` is one
of the jumped smoothed setters, a 0.5-weighted contribution vanishing in one control step is precisely
the discontinuity that arm exists to catch. (SC-022 (c) would not have caught it: it runs on the
default 0.04 ring.) Simply deferring the exclusion to the sleep edge does not fix it either — the
*renormalisation* would then step at the sleep edge instead, because a smaller row sum means a larger
`scale` and every surviving coefficient would jump up at that instant.

The fix is a **count mask**: one control-grid `OnePoleSmoother` per source slot at `kGainRampMs`
(S1.5), gliding `1 → 0` when the slot is dropped and `0 → 1` when it is re-admitted, multiplying that
slot's **outgoing** coupling both in the coefficients and in the row sum they are normalised against.
Every quantity in the per-sample path is then continuous through a count change, and the exclusion is
complete before the gate finishes its own 50 ms fade, so FR-075's mechanism (`prevOut_i` held at
`0.0f`) still holds as its final state.

```cpp
void normaliseRows() noexcept {
    // FR-075's count mask, read once. Exactly 1.0f / 0.0f in the steady state
    // (OnePoleSmoother snaps inside kCompletionThreshold, smoother.h:200-203).
    std::array<float, kMaxLoops> cm{};
    for (std::size_t j = 0; j < kMaxLoops; ++j) cm[j] = countSmoother_[j].getCurrentValue();

    for (std::size_t i = 0; i < kMaxLoops; ++i) {
        // A slot that is BOTH fully out of the count AND silent contributes nothing and
        // reports nothing. Both halves are required: cm[i] == 0.0f alone would zero a row
        // whose gate is still fading, and !engineActive alone would zero a DORMANT row and
        // break SC-015 (d)'s second arm, which requires dormancy to be inert.
        if (cm[i] == 0.0f && !loops_[i].engineActive) {
            appliedOwnFb_[i]     = 0.0f;
            appliedTotalGain_[i] = 0.0f;
            for (std::size_t j = 0; j < kMaxLoops; ++j) appliedCoupling_[j][i] = 0.0f;
            continue;
        }
        const float own = loops_[i].ownFbSmoother.getCurrentValue();
        float g = own;
        for (std::size_t j = 0; j < kMaxLoops; ++j) {
            if (j == i) continue;
            g += cm[j] * couplingSmoother_[j][i].getCurrentValue();
        }
        const float scale = (g > kMaxTotalLoopGain) ? (kMaxTotalLoopGain / g) : 1.0f;
        appliedOwnFb_[i] = own * scale;
        for (std::size_t j = 0; j < kMaxLoops; ++j) {
            appliedCoupling_[j][i] = (j == i)
                ? 0.0f
                : cm[j] * couplingSmoother_[j][i].getCurrentValue() * scale;
        }
        appliedTotalGain_[i] = std::min(g, kMaxTotalLoopGain);
    }
}
```

Note what did **not** change: the mask is applied on the **source** index `j` only. A loop that is
still fading out keeps receiving its neighbours' energy at full coupling exactly as a loop fading into
dormancy does — the two are the same behaviour, which is what FR-075 asks for.

Five properties this shape delivers, each answering a criterion:

* **It runs on the smoothed values and is the LAST step before the coefficients reach the per-sample
  path**, so the applied row sum is bounded at *every instant*, not only at the settled end points
  (SC-015 (a)).
* **The count enters only through a smoothed mask, never as a hard index bound**, so a mid-render
  `setNumLoops` moves every applied coefficient continuously over `kGainRampMs` (FR-075, FR-076;
  SC-001 (d), SC-022 (c)).
* **The sum excludes `i`, ignores wake/dormancy, and reaches the count-excluded answer exactly** (Q3).
  Once `countSmoother_[j]` has snapped to `0.0f` the term is bit-exactly absent, so at
  `numLoops = 1` the survivor's `G_0 = ownFb_0` whatever the unused slots hold and
  `getLoopAppliedTotalGain(0) == getLoopGain(0)` to `1e-6` (SC-015 (d), first arm — which sets the
  count through `PrepareConfig` or a `reset()` so the mask is *snapped*, not gliding). A neighbour
  going dormant changes no survivor's applied gain, because dormancy never touches the mask
  (SC-015 (d), second arm).
* **Only the applied values are scaled; the stored targets are untouched**, so a caller's
  configuration survives round-trip through `getLoopGain`/`getCoupling` (SC-015 (b)).
* **Worst case:** `own = 0.90` with five neighbours at `0.5` → raw `g = 3.40`, `scale = 0.279412`,
  applied own feedback `0.251471`, applied per-pair coupling `0.139706`, applied total exactly `0.95`.

`coupling_[i][i]` is never used: a diagonal write is a silent no-op and the diagonal reads back
`0.0f` (FR-030). Self-feedback is `ownFb_i`, a separate, separately-clamped quantity.

### S5.4 The two lane mappings (FR-052, FR-053)

```cpp
[[nodiscard]] static float laneValue(const BrownianDrift& lane) noexcept {
    const float v = lane.getCurrentValue();               // already clamped [-1,+1] (:212-214)
    return detail::isFinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
}
[[nodiscard]] float wanderScale() const noexcept { return wanderEnabled_ ? 1.0f : 0.0f; }

// delay: MULTIPLICATIVE, so a 449 ms loop and a 41 ms loop move by comparable fractions.
// A fixed +/-ms depth would move the long loop imperceptibly and the short one by a tenth
// of its length.
L.targetDelayMs = std::clamp(
    L.baseDelayMs * (1.0f + wanderScale() * L.delayWanderFrac * laneValue(L.delayLane)),
    kMinDelayMs, kMaxDelayMs);

// cutoff: LOG2 DOMAIN, so an octave of wander is an octave at every base.
L.targetCutoffHz = std::clamp(
    std::exp2(L.baseLog2Cutoff + wanderScale() * L.cutoffWanderOct * laneValue(L.cutoffLane)),
    kMinCutoffHz, maxCutoffHz_);
```

`baseLog2Cutoff` is cached by `setLoopCutoffHz` and by `applyDefaults()`, so the control step costs one
`exp2` per loop and **no `std::log2`**. Any `constexpr` log2 constant in this header must use
`detail::constexprLn(x) / detail::kLn2` (`db_utils.h:156`, `:144`) and **never a `constexpr`
`std::log2`**, which is a GCC/MSVC builtin extension that **Clang rejects** — the macOS and Linux legs
break while Windows stays green (`resonance_drift_network.h:255-266`). SC-017's constexpr-log arm pins
the series to `std::log2` at runtime.

`setWanderEnabled(false)` zeroes the **depth term** and nothing else (Q2, FR-056): the lanes keep
advancing, and the mapped delay and cutoff glide back to base over their own smoothing paths — the
crossfade staircase for the delay, the `SVF`'s coefficient smoother for the cutoff. Re-enabling moves
from base to wherever the still-advancing lane now sits. That is Phase 3's shipped behaviour verbatim;
there is **no** reading under which a re-enable is jump-free, and the header must not claim one.

### S5.5 The crossfade-onset counter (FR-071)

```cpp
    L.delay.setDelayMs(L.targetDelayMs);
    const bool xf = L.delay.isCrossfading();          // crossfading_delay_line.h:301
    if (xf && !L.lastCrossfading) ++L.crossfadeCount;
    L.lastCrossfading = xf;
```

A crossfade can begin **only** inside `setDelaySamples` (`:180-190`), which this component calls only
here, on the control grid. The counter is therefore an exact count of **onsets**, not of completed
steps — the two differ whenever a crossfade is retriggered mid-fade (`:176-181`). SC-003 (c) counts
these instead of guessing at step detection. The sampling cannot miss an onset: a 20 ms crossfade spans
~15 control steps at 48 kHz.

### S5.6 The governor (FR-043, FR-044, FR-045)

```cpp
    const float rms = follower_.getCurrentValue();                // read ONLY here

    // *** THE FOLLOWER'S OWN HEALTH GUARD (FR-047, rung 5's OTHER half) ***
    // NOT defensive, and NOT redundant with the per-sample b_i trap - the per-sample
    // trap is UNREACHABLE from this failure mode, and without this guard the
    // component mutes itself permanently and silently. The chain, verified end to
    // end this session:
    //   (1) EnvelopeFollower::processRMS takes the release branch on ANY NaN
    //       comparison and recomputes squaredEnvelope_ = squared + releaseCoeff_ *
    //       (NaN - squared) = NaN forever (envelope_follower.h:313-322);
    //       envelope_ = std::sqrt(NaN) (:325); detail::flushDenormal
    //       (db_utils.h:245-247) returns NaN unchanged, both comparisons being false.
    //   (2) the law below then yields target = std::clamp(std::pow(NaN, ...), 0.05f,
    //       1.0f) = NaN, because std::clamp returns v when both comparisons are false.
    //   (3) LinearRamp::setTarget does NOT propagate that NaN - it MUTES:
    //       target_ = 0.0f; current_ = 0.0f; increment_ = 0.0f (smoother.h:342-348).
    //   (4) govGain is then exactly 0.0f, y_i = std::tanh(b_i * 0) = 0, and every b_i
    //       stays FINITE on every subsequent sample - so the S6 step-3 trap never
    //       fires, getNonFiniteResetCount() stays 0, getClampEngagementCount() stays 0,
    //       and nothing outside reset()/prepare() ever clears follower_.
    // The result is a component muted for the life of the object with both health
    // counters reading clean: exactly the "fails catastrophically and quietly" mode
    // S8.5 claims to close. The claim that the governor "can never mute" because
    // kGovernorMinGain = 0.05 is false on this path, and this guard is what makes it
    // true. Cost: one ordered bit-pattern comparison per control step (750/s).
    if (!detail::isFinite(rms)) {
        follower_.reset();                 // envelope_follower.h:128 - the only cure
        ++nonFiniteResets_;                // the counter SC-012 (c) asserts on
        governorRamp_.setTarget(1.0f);     // recover to unity, over kGovernorRampMs
        return;
    }

    const float threshold = dbToGain(governorThresholdDb_);       // in (0, 1]
    const float over      = rms / threshold;
    const float target    = (over <= 1.0f)
        ? 1.0f
        : std::clamp(std::pow(over, 1.0f / governorRatio_ - 1.0f), kGovernorMinGain, 1.0f);

    // Second half of the same guard: governorThresholdDb_ and governorRatio_ are
    // finite by FR-009, and rms is finite by the branch above, so `target` is finite
    // by construction TODAY. The test is written anyway, because the failure mode it
    // catches is a silent permanent mute rather than an audible artefact, and because
    // a later phase adding a term to this law must not be able to reintroduce it.
    // AMENDED DURING THE BUILD (T012): this write goes through
    //   retargetGovernorRamp(v) { if (v != governorRamp_.getTarget()) governorRamp_.setTarget(v); }
    // and so does the health guard's setTarget(1.0f) above. Re-issuing an unchanged
    // target is NOT a no-op on LinearRamp: setTarget recomputes
    // increment_ = (target_ - current_) / rampSamples (smoother.h:353) from the current
    // position, so a target re-issued every 64 samples becomes a geometric approach
    // covering 64/960 = 6.67 % of the remainder per step, never trips process()'s
    // overshoot clamp (smoother.h:378-382) - the only place a LinearRamp is set exactly
    // equal to its target - and STALLS permanently at ~2.9e-5 short, where
    // current_ + increment_ rounds back to current_. Measured before the guard: every
    // SC-006 step after the first momentary engagement reported getGovernorGain() ==
    // 0.999973f even 20 dB below the threshold, breaking SC-006 (a)'s exact-1.0f arm and
    // shifting SC-006 (f)'s crossing level by a full 6 dB step at numLoops = 1.
    governorRamp_.setTarget(detail::isFinite(target) ? target : 1.0f);
```

* **What the tracker sees** (Q6): `normGain × Σ_i b_i` — the loops' post-DC-blocker values, summed and
  scaled by the **same** `1/sqrt(numLoops)` `normGain` FR-017 applies to the wet sum. It is fed once
  per sample in `renderChunk` (S6 step 4). Scaling here, and not only at the output, is load-bearing:
  without it `Σ_i b_i` grows with `numLoops`, so `-6 dB` would name a level up to
  `10·log10(6) = 7.8 dB` apart at `numLoops = 1` versus 6, and the threshold's meaning would shift
  again whenever a loop slept or `setNumLoops` was called. SC-006 (f) is the arm a raw unscaled tracker
  fails, and no build that scales by `normGain` can fail it. Cost: one multiply per sample, using the
  `normGain` already computed for the output stage — no second ramp.
* **The measurement point is pre-governor, pre-gate, pre-`tanh`** — the quantity being controlled,
  measured before its own action, which is what makes the loop a first-order regulator rather than an
  oscillator.
* **`ratio == 1` is exactly unity**: the exponent `1/1 - 1` is exactly `0.0f` and `std::pow(x, 0.0f)`
  is exactly `1.0f` for every finite positive `x`. That is the documented "governor off" setting, and
  SC-006 (d)'s exact-identity claim is reachable because of it.
* **It can never mute** — `kGovernorMinGain = 0.05` bounds the *law*, and the finiteness guard above
  bounds the *path into* the law. The law alone is not enough: `LinearRamp::setTarget` turns a NaN
  target into an unramped step to zero (`smoother.h:342-348`), which is a mute the clamp never sees.
  Both together are the other half of "a drone left running overnight must neither die nor explode"
  (roadmap 94–95).
* **One `std::pow` per control step**, 750/s at 48 kHz, never per sample. The follower's own 20 ms
  attack is far slower than the 1.33 ms control grid, so evaluating the law at 750 Hz loses nothing the
  follower could have expressed (D-9).

---

## S6. `renderChunk()` — the per-sample law (FR-015)

```cpp
void renderChunk(const float* inL, const float* inR, float* outL, float* outR,
                 float* const* loopTaps, std::size_t tapOffset, std::size_t n) noexcept {
    std::uint32_t engagements = 0;             // chunk-local; folded into the member once
    const std::size_t nLoops = config_.numLoops;

    for (std::size_t s = 0; s < n; ++s) {
        // (0) sanitise the dry, per channel INDEPENDENTLY (S16 C-2)
        const float dryL   = detail::isFinite(inL[s]) ? inL[s] : 0.0f;
        const float dryR   = detail::isFinite(inR[s]) ? inR[s] : 0.0f;
        const float monoIn = 0.5f * (dryL + dryR);              // FR-016: the engine is MONO

        // (1) advance EVERY per-sample ramp, unconditionally, for EVERY slot - dormant and
        //     out-of-count included. The ramps must be a pure function of the absolute
        //     sample count however the caller partitions its blocks, which is exactly what
        //     SC-010 asserts. A LinearRamp already at its target early-outs
        //     (smoother.h:372-374), so unconditional costs one predicted branch.
        const float normGain = normGainRamp_.process();
        const float govGain  = governorRamp_.process();
        const float m        = mixRamp_.process();
        const float wetTrim  = wetGainRamp_.process();
        std::array<float, kMaxLoops> gate{}, tapGain{};
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            gate[i]    = loops_[i].gate.process();
            tapGain[i] = loops_[i].inputRamp.process();
        }

        // (2) form ALL inputs from the PREVIOUS sample, before ANY loop's stages run.
        //     This is what makes the network independent of loop index order (FR-031, D-7).
        std::array<float, kMaxLoops> x{};
        for (std::size_t i = 0; i < nLoops; ++i) {
            float acc = tapGain[i] * monoIn + appliedOwnFb_[i] * prevY_[i];   // PRE-gate
            for (std::size_t j = 0; j < nLoops; ++j) {
                if (j == i) continue;
                acc += appliedCoupling_[j][i] * prevOut_[j];                  // POST-gate (Q4)
            }
            x[i] = acc;
        }

        // (3) run the chains. prevY_ and prevOut_ were fully consumed by (2), so writing
        //     them here cannot disturb any other loop this sample.
        float bSum = 0.0f, wetSum = 0.0f;
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            if (!L.engineActive) continue;   // FR-062: chain skipped; prevY_/prevOut_ are
                                             //   ALREADY exactly 0 from the sleep-edge clear
            const float sf = L.svf.process(x[i]);          // FR-011

            // FR-020: write then read - EXCEPT inside S3.1's read-mute window, where
            // the loop writes but does not read, so nothing the buffer held before
            // clearLoopAudio() can reach the output. This is FR-063's stale-ring
            // guarantee in O(1); the alternative, DelayLine::reset()'s 131 KB-524 KB
            // std::fill, cannot run on the audio thread (S3.1). One predicted branch.
            float d;
            if (L.readMuteSamples != 0) {
                L.delay.write(sf);                         // crossfading_delay_line.h:223
                d = 0.0f;
                --L.readMuteSamples;
            } else {
                d = L.delay.process(sf);                   // :291 - write then read
            }

            const float r  = L.resonator.process(d);       // FR-013
            float       b  = L.dcBlocker.process(r);       // FR-014, LAST in the circulating path

            if (!detail::isFinite(b)) {                    // RUNG 5 (FR-047)
                clearLoopAudio(i);                         // O(1) - S3.1. A PER-SAMPLE call
                                                           //   site cannot afford an O(buffer)
                                                           //   std::fill, and this one has none.
                follower_.reset();
                ++nonFiniteResets_;
                b = 0.0f;
            }
            bSum += b;

            const float y = std::tanh(b * govGain);        // RUNGS 2 + 3, NO loop-gain factor
            const float o = y * gate[i];                   // FR-061's 50 ms wake ramp
            prevY_[i]   = detail::flushDenormal(y);        // FR-084
            prevOut_[i] = detail::flushDenormal(o);
            wetSum     += prevOut_[i];
        }

        // (4) the governor's tracker, EVERY sample, on the FR-043 quantity
        static_cast<void>(follower_.processSample(normGain * bSum));

        // (5) the output stage - THE ORDER IS NORMATIVE (FR-015, FR-046, FR-072)
        float wet = wetTrim * (wetSum * normGain);         // FR-017 then FR-072's trim
        wet = detail::flushDenormal(wet);
        if      (wet >  kOutputClamp) { wet =  kOutputClamp; ++engagements; }   // RUNG 4
        else if (wet < -kOutputClamp) { wet = -kOutputClamp; ++engagements; }

        if (m == 0.0f) {                     // FR-072: BIT-EXACT dry, the wet is NOT summed
            outL[s] = dryL;  outR[s] = dryR;
        } else {
            outL[s] = (1.0f - m) * dryL + m * wet;
            outR[s] = (1.0f - m) * dryR + m * wet;
        }

        // (6) FR-073's observation-only tap write
        if (loopTaps != nullptr) {
            for (std::size_t i = 0; i < kMaxLoops; ++i) {
                if (loopTaps[i] != nullptr) loopTaps[i][tapOffset + s] = prevOut_[i];
            }
        }
    }
    clampEngagements_ += engagements;
}
```

Seven things in this body are decisions, not style:

1. **There is exactly ONE loop-gain factor per round trip, and it sits in the input sum.** An earlier
   spec draft carried both `ownFb_i · prevY_i` *and* a `· loopGain_i` at the output, applying the same
   stored quantity twice per circulation (`0.90² = 0.81`, not `0.90`). Putting every feedback
   coefficient in the input sum is also what makes FR-041 an **ℓ∞ row-sum** argument: the sum of the
   absolute coefficients feeding loop `i` is exactly `G_i ≤ 0.95` (S8.1). The symbol `loopGain_i`
   appears nowhere else in the component.
2. **Two previous-sample vectors, not one** (Q4, FR-031). Own feedback reads `prevY_` (**pre-gate**),
   so a loop's circulation is untouched by its own wake state and the gate stays a pure output
   multiplier. Cross-coupling reads `prevOut_` (**post-gate**), so a loop fading toward sleep fades its
   contribution to its neighbours continuously instead of delivering full amplitude for 50 ms and then
   stepping to zero. At the `kMaxCouplingPerPair = 0.5` fixture the rejected pre-gate reading would
   carry a 0.5-amplitude step — SC-014 (c) is measured exactly there.
3. **The dry is sanitised per channel** and the same sanitised value feeds the mono engine (the Phase-3
   shape). Without it a non-finite input sample reaches `outL` through the crossfade at any `mix < 1`.
   Consequence for SC-012 (b)'s narrative recorded in S16 C-2.
4. **`mix == 0.0f` takes an explicit branch.** `(1-0)·dry + 0·wet` is bit-exact for every finite `dry`
   **except `-0.0f`**, where `-0.0f + 0.0f` is `+0.0f`. SC-018 (a) asserts bit-identity, so the branch
   is required rather than tidy.
5. **A skipped loop's `continue` does no stores.** `prevY_[i]` and `prevOut_[i]` are already exactly
   `0.0f` from `clearLoopAudio` at the sleep edge, and nothing writes them while skipped — which makes
   FR-062's "contributes exactly nothing to its neighbours" true *by construction* rather than by a
   special-cased write, and is what buys SC-004 (c)'s ≥ 40 % dormant saving.
6. **The tap write reads `prevOut_`**, a value already computed and stored, after the loop. There is no
   second arithmetic path — which is what SC-016's bit-identity requires (risk R-5).
7. **The read-mute branch is the whole of FR-063's clear on the audio thread**, and it is one predicted
   branch rather than a 131 KB–524 KB `std::fill` (S3.1, R-17). It is inside the `engineActive` block
   deliberately: a *sleeping* loop is not rendered at all, so its window does not count down while it
   sleeps and is instead recomputed at the FR-064 wake edge from the position it wakes at (S7.3).

**`wet` cannot be NaN by construction**, so the ordered clamp (which a NaN would walk through, every
comparison against NaN being false) is sufficient here: the FR-047 trap has already made every `b_i`
finite, `std::tanh` of a finite product is finite, every ramp is finite, and `wetTrim ≤ 15.85`. Phase 3
needed an extra NaN guard at this point because its resonators could be parametrically pumped by an
audio-rate anchor sweep; nothing in this component retunes at audio rate (S15 R-3 re-examines this).

---

## S7. Loop life cycle and the Dormancy rule (FR-060 – FR-064)

### S7.1 `gateSteady(i)` — one value, two setters

```cpp
[[nodiscard]] float gateSteady(std::size_t i) const noexcept {
    if (i >= kMaxLoops || i >= config_.numLoops) return 0.0f;  // FR-075: an out-of-count loop sleeps
    const Loop& L = loops_[i];
    if (L.dormant) return 0.0f;
    return (L.wakeAmount <= kWakeSilenceEpsilon) ? 0.0f : L.wakeAmount;
}
```

Folding `setLoopWake` and `setLoopDormant` into one steady-state value is what makes them
behaviourally indistinguishable — the Dormancy rule's core claim (SC-014 (a)). The
`kWakeSilenceEpsilon = 1e-6` snap happens **here, at the source**, which keeps the sleep edge's exact
`== 0.0f` test valid by construction once a Phase-8 agent or a Phase-10 caller writes
`getEnvelopeValue() * getActiveDepth()` into `setLoopWake`: a release tail that stops at 1e-8 must not
leave a loop burning forever (SC-014 (e)). `setLoopWake` clamps its argument to `[0, 1]`, equally
load-bearing — an energy-driven agent must not be able to push a loop past unity or invert it.

### S7.2 The sleep edge (FR-063) — inside `updateControl()`, before the writes

```cpp
    if (L.engineActive && L.gate.isComplete() && L.gate.getCurrentValue() == 0.0f) {
        clearLoopAudio(i);          // *** THE DEVIATION FR-063 JUSTIFIES ***
        L.engineActive = false;
    }
```

The exact `== 0.0f` comparison is correct by construction, not by luck: `gateSteady()` returns a
literal `0.0f` in all three sleeping cases, and `LinearRamp::process()` lands **exactly** on its target
rather than approaching it (`smoother.h:379-383`).

**Why the clear is right, and why the header must justify it.** The Dormancy rule as written only says
the chain is skipped. A feedback loop has no generator behind it — the loop *is* the chain — so
"skipping" it freezes a fully charged delay line. On the wake edge that frozen ring would be
re-injected at full amplitude behind a 50 ms fade, minutes after the audio that produced it: a listener
hears a stale burst, not a loop opening. At `kMaxDecayTime = 30 s` of RT60 and a 449 ms line the stored
energy is substantial. Clearing on the sleep edge makes a woken loop refill from its input tap, which
is what "a feedback agent opens an ecology loop's coupling" (roadmap line 351) is supposed to sound
like. SC-014 (d) asserts it at −80 dBFS over the first 500 ms after a silent wake; a build that only
skips the chain fails by 60 dB or more. House support: Phase 3's own sleep-edge clear
(`resonance_drift_network.h:1681-1686`).

### S7.3 The wake edge (FR-064) — inside `refreshGates()`, i.e. inside the setter

```cpp
void refreshGates() noexcept {
    for (std::size_t i = 0; i < kMaxLoops; ++i) {
        Loop& L = loops_[i];
        const float target = gateSteady(i);

        if (!L.engineActive && target != 0.0f) {
            // WAKE EDGE, deliberately OUTSIDE the change-detection guard below.
            // Both writes are safe here precisely because the loop is silent and FR-063
            // has already cleared it - there is no signal in flight to click.
            L.delay.snapToDelayMs(L.targetDelayMs);   // NOT setDelayMs: a queued crossfade
                                                      //   would fire at the wake edge from a
                                                      //   stale tap position
            // S3.1's read-mute window, RECOMPUTED from the position the loop actually
            // wakes at - the snap above may have moved it a long way from the position
            // clearLoopAudio() measured at the sleep edge, and the window is only
            // exactly one delay length if it is measured after the snap.
            L.readMuteSamples =
                static_cast<std::size_t>(std::ceil(L.delay.getCurrentDelaySamples())) + 1u;
            L.svf.setCutoff(L.targetCutoffHz);
            L.svf.snapToTarget();                     // no 5 ms glide from a minutes-old coefficient
            L.lastCrossfading = false;
            L.engineActive = true;
        }

        if (target != L.lastGateTarget) {             // *** LOAD-BEARING, not an optimisation ***
            L.gate.setTarget(target);
            L.lastGateTarget = target;
        }
    }
}
```

**Why the wake edge is in the setter, not at the next control step:** a setter called mid-chunk starts
the ramp moving on the next *sample*, so a loop left `engineActive == false` would be silent for up to
63 samples while its gate was already rising — a ≤ 1.3 ms attack notch. Keeping the test unconditional
costs one bool and one float compare and makes the bad state unreachable
(`resonance_drift_network.h:1288-1306`).

**Why the `target != lastGateTarget` guard is load-bearing:** `LinearRamp::setTarget` recomputes
`increment_ = (target - current) / (rampMs·0.001·fs)` on **every** call (`smoother.h:342-354`), so
re-targeting a gate that is mid-ramp to the value it is already heading for **restarts** its 50 ms from
wherever it has got to. A Phase-8 agent writing one loop's wake per block would otherwise stretch every
*other* loop's ramp without bound. The compare is exact because the target is either a literal `0.0f`
or the value `setLoopWake` stored verbatim.

Callers of `refreshGates()`: `setLoopWake`, `setLoopDormant`, `setNumLoops` — and nothing else.
`prepare()` and `reset()` bypass it deliberately, snapping the gate and seeding `lastGateTarget` in the
same breath so the shadow can never start out of sync.

### S7.4 What runs and what does not while a loop is skipped (FR-062) — normative

| | while `engineActive == false` |
|---|---|
| the two `BrownianDrift` lanes | **advance**, on the decimated grid (S5.2 step 1) |
| the FR-052/FR-053 mappings | **evaluated**, into `targetDelayMs` / `targetCutoffHz` |
| `getLoopTargetDelayMs` / `getLoopTargetCutoffHz` | **keep changing** — the only observable proof the lanes are alive |
| `CrossfadingDelayLine::setDelayMs` | **not called** — a queued crossfade would fire at the wake edge instead of being snapped by FR-064 |
| `SVF::setCutoff` | **not called** |
| `getLoopCurrentDelayMs` | **frozen** — `getCurrentDelaySamples()` is the gain-weighted tap average and the taps only move when a crossfade completes inside `read()`, which is the call being skipped (`:169-174`, `:245-285`, `:306`) |
| `getLoopCurrentCutoffHz` | **frozen** — it is `svf.getCutoff()` (Q7), the last **commanded** value, and nothing commands it |
| the gate and input `LinearRamp`s | **advance** (S6 step 1) — partition invariance |
| `prevY_[i]`, `prevOut_[i]` | held at exactly `0.0f` by the sleep-edge clear |
| `readMuteSamples` | **frozen** — it counts down per *rendered* sample and a skipped loop renders none; it is recomputed at the FR-064 wake edge from the post-snap position (S7.3) |

SC-014 (b) asserts both halves as two distinct claims — targets moving, realised values still — and
SC-014 (b2) proves the lane advance behaviourally, depending on no getter being live while dormant:
sleep a loop, hold 60 s of silence, wake it, and the delay and cutoff it wakes with (read after
FR-064's snap) differ from the values it slept with.

### S7.5 `setNumLoops` (FR-075)

```cpp
void setNumLoops(std::size_t n) noexcept {
    config_.numLoops = std::clamp(n, std::size_t{1}, kMaxLoops);
    normGainRamp_.setTarget(1.0f / std::sqrt(static_cast<float>(config_.numLoops)));
    for (std::size_t i = 0; i < kMaxLoops; ++i) {          // FR-075's count mask (S5.3)
        countSmoother_[i].setTarget(i < config_.numLoops ? 1.0f : 0.0f);
    }
    refreshGates();     // dropped loops get gateSteady() == 0 -> a 50 ms fade, never a cut
}
```

Allocates nothing: all six delay lines are prepared regardless (FR-081, FR-082). Loops with index
`>= n` are dropped **exactly the way a sleep is done** — gate target to zero, 50 ms fade, then the
FR-063 sleep-edge clear when the gate settles; they leave their neighbours' coupling as soon as
`prevOut_` reaches `0.0f`. **Three quantities move, and all three are ramped at `kGainRampMs`**: the
dropped loops' gates, the `normGain` divisor, and the count mask that removes the dropped loops'
outgoing coupling coefficients (S5.3). Nothing steps. The mask is what makes the second of FR-075's
clauses honest — without it the coefficient would be deleted on the next control step, ~50 ms *before*
the gate it is supposed to be paced by, and SC-001 (d) is measured on the fixture where that is a
0.5-weighted discontinuity. Loops re-admitted by a later, larger `n` take the FR-064 wake path
unchanged. The FR-017 divisor changes to the **new** `numLoops` immediately as a *target* —
`getNumLoops()` and `getNormalisationLoopCount()` report it the moment the setter returns — but reaches
the per-sample path through the `normGain` `LinearRamp` at `kGainRampMs`, so the
`1/sqrt(6) → 1/sqrt(5)` change (**0.792 dB**) is a 50 ms glide rather than a step. Without that ramp
SC-022 (c)'s click assertion is unreachable. The divisor is **always** `numLoops`, never the awake
count: an awake-count reading computes `1/sqrt(0) = inf` when every loop sleeps, and `inf * 0.0f` is
NaN.

---

## S8. The five-rung boundedness ladder (FR-040 – FR-048)

The rungs are independent and separately asserted. **No rung may be removed on the grounds that
another one covers it** (FR-040) — each fails differently.

### S8.1 Rung 1 — the linear network is a strict contraction, by construction (FR-041)

The header must carry this arithmetic, not a claim.

| in-loop stage | worst-case magnitude gain | why |
|---|---|---|
| `SVF` at `Q <= kButterworthQ` | `1` | Lowpass/Highpass peak at `Q`, so `Q ≤ 0.7071` caps them at unity; Bandpass is `m1 = k = 1/Q`, constant 0 dB peak at any `Q` (`svf.h:565-572`) |
| `CrossfadingDelayLine`, **outside** a crossfade | `1` | a pure delay: one tap at unity gain |
| `CrossfadingDelayLine`, **during** a crossfade | `≤ sqrt(2) = 1.41421` | an equal-power two-tap blend, `gA + gB ≤ sqrt(2)` with `gA² + gB² = 1`. **Reached**, not hypothetical, at any tone whose period divides the tap separation. Carried through the product below rather than dropped |
| `Biquad` RBJ bandpass | `1` | "Constant 0 dB peak gain" (`biquad.h:71`), built normalised by `a0 = 1 + alpha` (S4.1) |
| `DCBlocker` | `2/(1+R) ≈ 1.000654` at Nyquist | `R = exp(-2π·10/48000) = 0.9986919`. **The one in-loop stage that is not non-expansive** — absorbed explicitly rather than hand-waved |

Every feedback coefficient — own feedback and every incoming coupling — sits in FR-015's input sum and
is applied **once** per circulation, so the sum of the absolute coefficients feeding loop `i` is
exactly `G_i ≤ kMaxTotalLoopGain = 0.95`, and the worst-case round-trip magnitude gain **outside a
crossfade window** is

```
0.95 × 1.000654 = 0.950621 < 1        (-0.4399 dB per circulation)
```

**The crossfade term, carried rather than dropped.** An earlier revision of this section listed the
`sqrt(2)` in the table above and then silently omitted it from this product. Included, the product is
`0.95 × 1.41421 × 1.000654 = 1.34438 > 1`, so **rung 1 is not a contraction during a crossfade in the
worst case**, and the "the taps are decorrelated" escape is weakest at precisely SC-001 (a)'s own
fixture: every resonator RT60 at `kMaxDecayTime = 30 s` makes each loop's circulating signal
narrow-band, so during a crossfade the two taps carry near-sinusoids and can be strongly correlated,
and wander at `kMaxWanderRateHz` makes crossfades fire as often as the component can make them. The
honest statement, which the header must carry in these three parts:

1. **Outside crossfade windows the network is a strict contraction**, ℓ∞ gain `0.950621`, and its state
   decays in the absence of input. This is the deterministic half and it is unconditional.
2. **Inside a crossfade the two-tap blend can add up to +3.010 dB**, and the worst-case instantaneous
   round-trip gain is `1.34438`. That is a **bounded transient**, because a crossfade lasts
   `kCrossfadeMs = 20 ms`: the shortest loop (41 ms) completes 0.488 circulations in that window, so
   the amplification from one worst-case crossfade is at most `1.34438^0.488 = 1.1560` (+1.26 dB),
   after which the gain returns below unity. The bound is per crossfade and does not accumulate
   deterministically, because the tap separation drifts and the phase relation with it.
3. **Averaged over the tap phase the crossfade is non-expansive.** With `gA = cos θ`, `gB = sin θ` and
   a relative tap phase `φ` uniform on `[0, 2π)`, `E[log|gA + gB·e^{jφ}|] = log(max(gA, gB)) ≤ 0`, with
   equality only at the crossfade's end points. The mean log-gain contributed by crossfades is
   therefore **negative**, which is why the component decays in practice — but it is a statistical
   statement, not a proof, and this plan does not let a criterion rest on it.

**What that costs, and what it does not.** It does not touch rungs 2–5: `tanh` still bounds every
`|y_i| ≤ 1` unconditionally (S8.2), so no crossfade sequence can make the output unbounded, and
SC-001's four bounded assertions (peak, clamp count, non-finite count, finiteness) are unaffected and
still run on the full-wander corner. What it does cost is the *decay* assertion: a `≥ 60 dB` decay over
a silent tail is a test of (1), and (1) does not hold while crossfades keep firing. SC-001 (a) is
therefore split (S12.3): **(a1)** keeps the full-wander corner and the four bounded assertions plus a
non-divergence clause, and **(a2)** runs the same corner with wander disabled at the input cut-off — no
new crossfades, so (1) holds strictly — and carries the `≥ 60 dB` contraction assertion. Neither
threshold is relaxed; the contraction assertion is moved onto the fixture where the argument it tests
is actually the operative one.

**Margin arithmetic for (a2), corrected.** `0.950621` is a per-**circulation** gain, not a per-sample
one: the loop contains a 41–449 ms delay. `20·log10(0.950621) = −0.4399 dB per circulation`. The
shortest loop circulates `1/0.041 = 24.39` times per second (**−10.73 dB/s**) and the longest
`1/0.449 = 2.227` times per second (**−0.980 dB/s**), so 60 dB of decay takes **5.6 s** on loop 0 and
**61.2 s** on loop 5. In series with a resonator whose own RT60 is `kMaxDecayTime = 30 s`, an
order-of-magnitude margin estimate for the composite tail is `30 + 61 ≈ 91 s` — against SC-001 (a2)'s
**1 740 s** silent tail, a factor of ~19. (An earlier revision of this note read "~13.9 dB per
512-sample block", which is arithmetic on no defensible reading: `0.950621` yields neither
`−0.44 dB × 512 = −225 dB` per block nor 13.9. The figure above is the one to keep, and it is a margin
estimate, not the criterion — the criterion is the measurement.)

The argument is stated for **settled coefficients**; a resonator retune (S4.1) is a stepped hard swap
between two independently unity-peak coefficient sets, so there is no interpolated intermediate for
this rung to reason about and no transient magnitude excursion at a retune. The swap's audible
discontinuity is a *click* concern the spec accepts and names (FR-013, FR-076), not a boundedness
concern — which is exactly why FR-040 forbids removing rungs 2 and 3 on the grounds that rung 1 holds.
The crossfade excursion above is the sharpest illustration of that rule in the whole component: rung 1
alone does not cover it, and rung 2 does.

### S8.2 Rung 2 — the per-loop `tanh` (FR-042)

`y_i = std::tanh(b_i · governorGain)`, placed exactly where `FilterFeedbackMatrix` places it: "Apply
soft clipping (tanh) for stability before feedback routing (FR-011)" (`filter_feedback_matrix.h:609-610`).
It bounds `|y_i| ≤ 1` **unconditionally**, whatever a future coefficient change, sample-rate extreme or
arithmetic surprise does to rung 1, and therefore bounds the wet sum at `sqrt(numLoops) ≤ 2.4495`
before the FR-072 trim. It is a bound, not a tone control: at `kDefaultLoopGain`'s levels `tanh` is
within 2 % of linear.

Two consequences worth stating: **rung 4's clamp cannot be reached from the audio input at any drive
level** (S8.4), and if OQ-2's lever 1 is taken, `FastMath::fastTanh` (`fast_math.h:65`) preserves the
bound exactly — it returns exactly ±1 beyond ±3.5 and is within 0.05 % below it.

### S8.3 Rung 3 — the energy governor (FR-044). Design in S5.6.

### S8.4 Rung 4 — the hard output clamp, and where exactly it sits (FR-046)

The clamp is applied to `wetGain × (Σ out_i × normGain)` — **after** the FR-017 normalisation and
**after** the FR-072 wet trim, immediately **before** the mix crossfade (S6 step 5 is normative).
Placing it before the trim would leave the single largest gain in the component downstream of its own
guard: `setWetGain(+24 dB)` is ×15.85, and `2.4495 × 15.85 = 38.8` would leave the clamp having done
nothing.

The zero-engagement requirement's scope is deliberately narrow:

* With `wetGain ≤ 0 dB` the bound is **structural** (rung 2 caps the wet sum at 2.4495 against a
  ceiling of 4.0), so `getClampEngagementCount() == 0` is a **defect detector**, not a limiter, and
  SC-013 (a) asserts nothing until (b) proves the counter increments at all.
* **The one control that can reach the clamp is `setWetGain` above `+4.27 dB`**
  (`4.0 / sqrt(6) = 1.633`, i.e. `20·log10(1.633) = 4.256 dB`). Above that the clamp is a working
  limiter and engagements are expected. SC-013 (b) uses `+24 dB` as the fixture that proves the counter
  is wired; SC-001 (c) draws `wetGain` from `[-24, 0]` dB and says why.

### S8.5 Rung 5 — the non-finite trap, and why it needs a probe (FR-047, FR-048)

Two of the shipped primitives in the loop do **not** self-heal, which is the whole reason rung 5 exists:

* `DCBlocker::process` — "NaN inputs are propagated (FR-016)" (`dc_blocker.h:188`), no finiteness
  branch, and `detail::flushDenormal` (`:203`) does not clear a NaN. A NaN sticks in `y1_` forever.
* `EnvelopeFollower::processSample` — "Does NOT validate input — caller must ensure no NaN/Inf for
  maximum speed" (`:163`). One non-finite sample sets `squaredEnvelope_` to NaN, `std::sqrt(NaN)` is
  NaN (`:313-325`), and `detail::flushDenormal` does not clear it. This is the single most likely way
  for this component to fail catastrophically and quietly.

Hence the trap resets **both**: `clearLoopAudio(i)` (O(1), S3.1), `follower_.reset()`,
`++nonFiniteResets_`, `b = 0.0f`. The check is one ordered comparison per active loop per sample and is
`-ffast-math`-proof by construction (bit-pattern test behind an opaque barrier, `db_utils.h:250-259`).

**The follower is protected by its OWN control-step guard, not by the per-sample `b_i` trap, and the
distinction is not a nicety.** An earlier revision of this plan asserted that a poisoned follower
"reaches the trap indirectly: the poisoned follower makes `governorGain` NaN, which makes `y_i` NaN,
which circulates into the next sample's `b_i` and fires rung 5". That is false, and the failure it
hides is worse than the one it describes. `governorRamp_.setTarget(NaN)` does **not** propagate the
NaN: `LinearRamp::setTarget` detects it with the fast-math-proof `detail::isNaN` behind
`ITERUM_NOINLINE` and writes `target_ = 0.0f; current_ = 0.0f; increment_ = 0.0f`
(`smoother.h:342-348`) — an instantaneous, unramped step to **zero gain**. So `govGain == 0.0f`,
`y_i = std::tanh(b_i · 0) = 0`, every `b_i` stays finite on every subsequent sample, and the per-sample
trap **never fires**. The component is muted for the life of the object — nothing outside
`reset()`/`prepare()` ever clears `follower_` — with `getNonFiniteResetCount()` reading 0 and
`getClampEngagementCount()` reading 0. Both health lights green, no audio.

The guard that actually closes this is in S5.6, at the source: the control step tests
`detail::isFinite(follower_.getCurrentValue())` **before** the value can reach the ramp, and on failure
resets the follower, increments `nonFiniteResets_` and re-targets the governor to `1.0f`. SC-012 (c)'s
second arm asserts against *that* guard — counter `+1` at the next control step,
`getGovernorGain()` back to `1.0` over `kGovernorRampMs`, `getGovernorRms()` finite from the following
control step — and not against a per-sample trap that this path cannot reach (S12.3, R-16).

**Rung 5 is defence in depth and is not reachable through the public API — and the header must say so**
rather than let a future reader read a zero counter as evidence of a tested guard. Three doors are
closed:

1. The dry sanitiser (S6 step 0) intercepts a non-finite input sample before it becomes `monoIn`.
2. Behind it, the loop's first stage `SVF::process` **resets and returns 0.0f** on a non-finite input
   (`svf.h:359-363`), and the `Biquad` behind that does the same (`:353-356`).
3. FR-009's rejection of non-finite setter arguments closes the coefficient route Phase 3 found.
   `std::clamp` does **not** reject NaN (with `v = NaN` both comparisons are false and `v` is
   returned), and the trace is fatal: a NaN cutoff reaches `SVF::setCutoff` → `computeG` →
   `std::tan(NaN)` → NaN coefficients, and `SVF::process` resets only on a non-finite **input sample**,
   never on non-finite coefficients.

What rung 5 still covers is everything that is not a public call: a future in-loop stage without a
self-heal, an arithmetic surprise at an extreme sample rate, a coefficient path a later phase adds. Its
counter must therefore read **zero** in every in-spec configuration (SC-012 (a)–(b)), and its behaviour
is proven through the declared, test-only probe:

```cpp
// In the header, immediately before the class:
namespace detail { struct FeedbackEcologyNonFiniteProbe; }
// Inside the class, first line of the private section:
friend struct detail::FeedbackEcologyNonFiniteProbe;
```

Declared and **never defined** by the library, so a shipping build has no way to call it and it adds no
public surface (`seraphis_engine.h:181-196`, friend at `:1074`). The Phase-5 non-finite TU defines it
and uses it to write a bit-pattern non-finite value into (a) one loop's `DCBlocker` state and (b) the
governor's `EnvelopeFollower` — the two stages FR-047 names — **and nothing else**. SC-012 (c) is its
only consumer; no shipping code path, no plugin and no other test may use it.

Probe shape the non-finite TU implements (it needs access to `loops_[i].dcBlocker` and `follower_`;
`DCBlocker`'s state is private, so the poison is injected by *processing* a non-finite sample through
that one blocker, which propagates it into `y1_` — the documented behaviour that makes the trap
necessary in the first place):

```cpp
namespace Krate::DSP::detail {
struct FeedbackEcologyNonFiniteProbe {
    static void poisonDcBlocker(FeedbackEcology& fe, std::size_t loop, float nonFinite) noexcept {
        static_cast<void>(fe.loops_[loop].dcBlocker.process(nonFinite));   // sticks in y1_ (:188)
    }
    static void poisonFollower(FeedbackEcology& fe, float nonFinite) noexcept {
        static_cast<void>(fe.follower_.processSample(nonFinite));          // sticks in squaredEnvelope_
    }
};
}  // namespace Krate::DSP::detail
```

### S8.6 FR-009's normative argument contract — one rule for all three Vorago siblings

Reproduced from `resonance_drift_network.h:549-576` so a Phase-10 caller learns it once:

* **Out-of-range index** → every setter is a **silent no-op**; every getter returns the documented
  neutral (`0.0f`, `0`, `false`, the enum's zero value) **without indexing the array**.
* **Non-finite float argument** → the setter is a **no-op and the previous value stands**. One line at
  the top of every float setter: `if (!detail::isFinite(v)) return;`
* **Out-of-range finite float** → **clamped**, and the matching getter reports the clamped value.
* **There is no fourth rule, and in particular no `prepared_` gate.** FR-006 says setters are callable
  at any time; FR-009's contract has exactly the three rules above. A setter called before `prepare()`
  therefore **takes effect**, and its getter reports the new value immediately — which is the same
  statement S9's unprepared-state rule makes from the other side. This is safe on every setter:
  `SVF::setCutoff` clamps against the SVF's own constructed `44100.0` (`svf.h:690`, `:716`),
  `updateResonator()` uses the construction-seeded bounds (S1.5), and every ramp or smoother write is
  superseded by `prepare()` steps 11–12's snap. The one caveat a caller must know is FR-005 (c): a
  later `prepare()` **discards** that configuration, because `applyDefaults()` runs on every call. This
  bullet exists because an earlier revision implied the gate in a `prepare()` step comment (S2 step 8)
  and nothing stated it; SC-017 now asserts the rule directly.

`setCoupling(i, i, x)` is a no-op and `getCoupling(i, i)` is `0.0f` (FR-030). `setCouplingMatrix`
applies the same per-entry rule element by element: a non-finite entry leaves that pair's previous
value standing, and the diagonal is ignored. Each argument of a multi-argument setter is judged
independently, so one bad value cannot discard a good one.

---

## S9. Read-surface semantics (FR-070, FR-071)

Four getters exist because a criterion could not otherwise be written against a **correct**
implementation. Their semantics are normative:

| getter | returns | why it is not the obvious thing |
|---|---|---|
| `getLoopTargetDelayMs(i)` | the FR-052 mapped value as of the last control step, **whether or not the loop is skipped** | it is the continuous lane trajectory; the `Current` value is the realised crossfade staircase, quantised to ≥ 100-sample steps and frozen while asleep. SC-009 (d) correlates the **target** series because a near-constant staircase makes a Pearson correlation meaningless (0/0, or dominated by two quantisation steps) |
| `getLoopTargetCutoffHz(i)` | the FR-053 mapped value, same rule | same |
| `getLoopCurrentCutoffHz(i)` | `svf_[i].getCutoff()` (`svf.h:328`) — the last **commanded** cutoff (Q7) | it **coincides exactly** with the target getter while the loop runs: `SVF`'s per-sample smoothing changes the internal `g`/`k`, not the value `getCutoff()` reports, so there is no smoothed intermediate to read back. The two diverge **only** under FR-062's write skip. No `SVF` amendment (FR-090), no component-side smoothed mirror |
| `getLoopCrossfadeCount(i)` | a monotone `std::uint32_t` of crossfades **started** (S5.5) | onsets, not completed steps — the two differ whenever a crossfade is retriggered mid-fade (`crossfading_delay_line.h:176-181`) |

A fifth was added by review, for the same reason: **`getLoopAppliedCoupling(from, to)`** returns
`appliedCoupling_[from][to]` — `0.0f` for an out-of-range index or `from == to`, without indexing —
because without it SC-015 could not observe the FR-035 normalisation at all.
`getLoopAppliedTotalGain(i)` is `std::min(g, kMaxTotalLoopGain)` **by construction** (S5.3), so it is
`<= kMaxTotalLoopGain` and `== kMaxTotalLoopGain` whenever the raw sum exceeded it *whatever the
coefficients in force are*: a build that computed the reported total correctly but dropped the
`* scale` from the coefficient writes passed the old SC-015 (a), (b) and (d) unchanged. The per-pair
getter is what turns that criterion from a tautology into a measurement (S12.3, S16 C-7).

The rest exist for the reasons their criteria name: `getLoopCurrentDelayMs` for SC-005,
`getGovernorGain`/`getGovernorRms` for SC-006, `getLoopGate`/`isLoopEngineActive` for SC-014,
`getLoopAppliedOwnFeedback`/`getLoopAppliedCoupling`/`getLoopAppliedTotalGain` for SC-015,
`getClampEngagementCount`/`getNonFiniteResetCount` for SC-012/SC-013,
`getLaneDecimation` for the FR-055 mapping.

**Unprepared state** (SC-017): every *configuration* getter returns its post-construction default —
`getNumLoops()` is 6, `getMix()` is `kDefaultMix`, `getLoopDelayMs(0)` is `41.0f`, and so on — because
FR-002 leaves construction in the state `prepare(48000.0, PrepareConfig{})` produces. The **only two
exceptions** are `getAllocatedBytes()` (0 before `prepare()`, SC-008) and `isPrepared()` (false).
"Neutral" is reserved for FR-009's out-of-range-index case and is **not** a second meaning for the
unprepared state.

**Two consequences of that rule the implementer must not discover at run time.**

1. **Every rate-derived clamp bound must be pre-seeded, never zero.** A configuration getter that
   clamps against a bound `prepare()` computes is called on an unprepared instance by SC-017, so a
   `0.0f` initialiser makes it `std::clamp(v, lo, 0.0f)` with `lo > 0` — inverted bounds, UB, trapped
   by MSVC's `_STL_VERIFY` (R-8), and where it is not trapped it returns 0 and whatever divides by it
   returns inf. This bit `getLoopResonanceRt60` in the previous revision, whose only guard was
   `maxResonanceHz_`, initialised to `0.0f` and written only in `prepare()` step 3. Both cached bounds
   are therefore seeded at construction from `kConstructionSampleRate` (S1.5), and the invariant
   "neither may ever hold a value below its paired minimum" is pinned by the S1.2 `static_assert`s.
   **Any getter added later that clamps against a rate-derived bound must be audited against this
   rule** — today there are exactly two such bounds, `maxCutoffHz_` and `maxResonanceHz_`, and only
   `getLoopResonanceRt60` reads one of them from the read surface.
2. **Setters are not gated on `prepared_`** (S8.6). A setter called before `prepare()` takes effect and
   its getter reports it; the unprepared *default* is what a getter returns when nothing has been set,
   not a frozen state. SC-017 asserts both halves.

Implementation consequence: `applyDefaults()` must be callable from the constructor as well as from
`prepare()`. Concretely, `FeedbackEcology()` is `= default` only if the member initialisers already
carry the tables; the simpler and less error-prone form is a user-provided default constructor whose
body is exactly `applyDefaults();` — one owner for the default tables, no duplicated literals, and
`getLoopDelayMs(0) == 41.0f` before `prepare()` by construction. `applyDefaults()` writes
`baseLog2Cutoff` with `detail::constexprLn(hz)/detail::kLn2`, which needs no rate, and it must not
touch `PrepareConfig` fields (S2 step 2) or any *audio object* state — the rate-dependent **snap** of
delay positions and `SVF` coefficients is S2 step 15's `snapControlState()`, which needs a prepared
`CrossfadingDelayLine` and stays there.

**The one rate-dependent thing the constructor must still do**, because a getter reports it: after
`applyDefaults()`, the constructor runs `updateResonator(i)` for every loop, at
`kConstructionSampleRate`. Without it `appliedResonanceQ` sits at its `kMinResonatorQ = 0.1`
initialiser and `getLoopResonanceRt60(i)` on an unprepared instance reports 0.0003 s instead of S1.2's
table — which SC-017's unprepared-state arm and SC-023 (c) both read. This is safe and is *not* an
exception to the rule above: `updateResonator` needs only `sampleRate_` and the two clamp bounds, and
all three are seeded at construction (S1.5). `prepare()` step 10 recomputes them at the real rate.

---

## S10. Allocation ledger (FR-081, FR-082) — the assertion surface for SC-008

Everything except the six delay buffers is a fixed-size `std::array` member. `EnvelopeFollower::prepare`
**ignores `maxBlockSize`** (`:107`) and allocates nothing. `BrownianDrift`, `SVF`, `Biquad`, `DCBlocker`,
`OnePoleSmoother` and `LinearRamp` are all allocation-free.

`CrossfadingDelayLine::prepare` forwards to `DelayLine::prepare` (`delay_line.h:267-275`):

```
maxDelaySamples_ = size_t(sampleRate * kMaxDelaySeconds)     // kMaxDelaySeconds = 0.52
bufferSize       = nextPowerOf2(maxDelaySamples_ + 1)         // delay_line.h:26
bytes            = bufferSize * sizeof(float)
```

```cpp
[[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return allocatedBytes_; }
// computed once, in prepare() step 13:
allocatedBytes_ = kMaxLoops
                * nextPowerOf2(static_cast<std::size_t>(sampleRate_ * kMaxDelaySeconds) + 1)
                * sizeof(float);
```

**The multiplier is `kMaxLoops`, not `numLoops`,** and the difference is not cosmetic: all six lines are
prepared regardless of the count because `setNumLoops` must not allocate (FR-075, FR-081). A
`numLoops`-scaled figure would under-report the real footprint by 6× at `numLoops = 1` and SC-008 would
fail against it.

| rate | `sampleRate × 0.52` | `+1` → `nextPowerOf2` | bytes per line | **six lines** |
|---|---|---|---|---|
| 44 100 | 22 932 | 32 768 | 131 072 | **786 432** |
| 48 000 | 24 960 | 32 768 | 131 072 | **786 432** |
| 96 000 | 49 920 | 65 536 | 262 144 | **1 572 864** |
| 192 000 | 99 840 | 131 072 | 524 288 | **3 145 728** |

Identical for `maxBlockSamples` of 64, 512 and 8192 and for `numLoops` of 1 and 6; `0` before
`prepare()`. That is exactly SC-008's assertion set. (44.1 and 48 kHz coincide because both land in the
same power-of-two bucket — worth noting so a reader does not read the equality as a bug.)

---

## S11. The FR-076 smoothing table, realised

| setter | mechanism in code | constant | notes |
|---|---|---|---|
| `setMix` | `mixRamp_.setTarget(mix_)`, advanced per sample | `kMixRampMs = 20` | |
| `setWetGain` | `wetGainRamp_.setTarget(dbToGain(wetGainDb_))`, per sample | `kMixRampMs` | the ramp carries the **linear** trim, not dB |
| `setLoopInputGain` | `loops_[i].inputRamp.setTarget(g)`, per sample | `kMixRampMs` | a tap opening from zero is a 20 ms fade, not a step in `x_i` |
| `setLoopGain` | `ownFbSmoother.setTarget(g)`, control grid, then `normaliseRows()` | `kCouplingSmoothMs = 20` | configured at `fs/64` (S5.0) |
| `setCoupling` / `setCouplingMatrix` | `couplingSmoother_[f][t].setTarget(a)`, control grid, then `normaliseRows()` | `kCouplingSmoothMs` | same |
| `setLoopCutoffHz`, `setLoopFilterQ` | `svf.setCutoff` / `svf.setResonance`; the `SVF`'s own per-sample coefficient smoother carries it | `SVF::kDefaultSmoothingTimeSec = 0.005f` | enabled once, in `prepare()` step 4 |
| `setLoopResonanceHz`, `setLoopResonanceRt60` | **stepped** — `updateResonator(i)` → `Biquad::setCoefficients` | — | S4.1; exempt from SC-001 (d)'s click assertion **by name** |
| `setLoopDelayMs`, `setLoopDelayWander` | the FR-021 crossfade staircase, via the control-grid `setDelayMs` | `kCrossfadeMs = 20`, equal-power | **no extra smoother**: one would be dead weight below the 100-sample threshold and would fight the crossfade above it |
| `setGovernorThresholdDb`, `setGovernorRatio` | `governorRamp_`, re-targeted per control step, advanced per sample | `kGovernorRampMs = 20` | |
| `setLoopWake`, `setLoopDormant` | `refreshGates()` → the gate `LinearRamp` | `kGainRampMs = 50` | the Dormancy rule's fade, verbatim |
| `setNumLoops` | gate ramp for dropped loops + the `normGain` ramp + the FR-075 **count mask** (`countSmoother_`, control grid) on the dropped loops' outgoing coupling | `kGainRampMs` | S7.5, S5.3. All three ramps share the constant; nothing steps |
| `setLoopFilterMode` | **stepped** — re-points the `SVF` output tap, a genuine signal discontinuity | — | exempt from the click assertion by name |
| `setWanderRate` | **stepped**, but **click-free** — it writes only `laneDecimation_` and the twelve lanes' `setSmoothness`, and steps no mapped value in the signal path | — | therefore **held to** SC-001 (d)'s click assertion, not exempt from it (S12.3) |
| `setWanderEnabled`, `setSeed`, `prepare`, `reset` | **stepped** | — | `setSeed` restarts the modulation by design; `setWanderEnabled` zeroes a depth term, and S5.4 records that there is no reading under which a re-enable is jump-free |

Anything not in this table does not exist on the surface. **FR-076's stepped class has seven members**
— `setLoopFilterMode`, `setLoopResonanceHz`, `setLoopResonanceRt60`, `setWanderRate`,
`setWanderEnabled`, `setSeed`, and the pair `prepare`/`reset` — and "stepped" is not the same as
"exempt from the click assertion". The split SC-001 (d) actually uses is set out there: `prepare` and
`reset` are not parameter jumps and are not jumped at all; `setWanderRate` is stepped-but-click-free
and is held to the assertion; the remaining five are the named exemption array.

`setWanderRate` (FR-055), the single owner of the decimation and of every lane's smoothness, is
Phase 3's mapping verbatim (`resonance_drift_network.h:715-740`):

```cpp
void setWanderRate(float hz) noexcept {
    if (!detail::isFinite(hz)) return;
    wanderRateHz_ = std::clamp(hz, kMinWanderRateHz, kMaxWanderRateHz);
    const float requestedTau = 1.0f / wanderRateHz_;                    // [1, 500] s
    const auto  ceilSteps    = static_cast<std::size_t>(
                                   std::ceil(requestedTau / BrownianDrift::kTauMax));
    const std::size_t newDecimation = std::clamp(ceilSteps, std::size_t{1}, kMaxLaneDecimation);
    const float tau        = requestedTau / static_cast<float>(newDecimation);
    const float smoothness = std::clamp(
        (tau - BrownianDrift::kTauMin) / (BrownianDrift::kTauMax - BrownianDrift::kTauMin),
        0.0f, 1.0f);
    for (Loop& L : loops_) {
        L.delayLane.setSmoothness(smoothness);
        L.cutoffLane.setSmoothness(smoothness);
    }
    laneCounter_ %= newDecimation;        // REBASE, do not reset: a mid-render rate change must
    laneDecimation_ = newDecimation;      // neither force an extra lane advance nor skip one
}
```

Without the decimation, `BrownianDrift`'s `tau` saturates at `kTauMax = 30 s` and the whole sub-range
`[0.002, 0.0333]` Hz — **including this component's own 0.03 Hz default** — would be a dead zone in
which every rate rendered identically.

**The mapping, evaluated at the three rates SC-025 pins** (so the criterion is arithmetic, not a
re-derivation):

| `hz` | `requestedTau` | `ceil(tau / 30)` | `getLaneDecimation()` | per-advance `tau` | `smoothness` | effective correlation time |
|---|---|---|---|---|---|---|
| `kMinWanderRateHz = 0.002` | 500 s | 17 | **17** | 29.412 s | 0.98027 | 500 s |
| `kDefaultWanderRateHz = 0.03` | 33.333 s | 2 | **2** | 16.667 s | 0.55257 | 33.3 s |
| `kMaxWanderRateHz = 1.0` | 1 s | 1 | **1** | 1 s | 0.02685 | 1 s |

**This mechanism had no discriminating test before this revision, and that is why SC-025 exists.**
Every criterion that exercised wander ran at either the default or `kMaxWanderRateHz`. At the default,
decimation 2 with `tau = 16.67 s` gives an effective 33.3 s against a hard-wired-decimation-1 build's
saturated 30 s — indistinguishable in any render. At `kMaxWanderRateHz` the decimation is 1 in both
builds. The mechanism only bites near `kMinWanderRateHz`, where the correct build is 16.7× slower than
the saturated one, and no arm rendered there. The `laneCounter_ %= newDecimation` rebase was untested
entirely. SC-025 covers all three (S12.3).

---

## S12. Test plan

Four new TUs plus one new shared helper. Measurement conventions from the spec, restated where a build
decision is needed:

* Renders at 48 kHz unless a criterion names another rate.
* **Reference patch:** `numLoops = 6`, all loops awake, the FR-013/FR-022/FR-033/FR-052/FR-053 default
  tables, wander on at `kDefaultWanderRateHz`, governor at its defaults, `mix = 1.0` (so the criterion
  measures the wet path, not the crossfade), `wetGain = 0 dB`, fixed seed.
* **Reference drive:** white noise at **−12 dBFS RMS** (not peak — the two differ by 10–12 dB for white
  noise and every governor threshold assertion turns on which is meant), fixed seed, both channels.
* **Every `ClickDetectorConfig` sets `sampleRate` to the render rate.** The struct's default is
  `44100.0f` (`artifact_detection.h:39`), which at a 48 kHz render mis-reports every `timeSeconds`.
* No bit-exact float golden anywhere; `render_fingerprint.h` where a render must be pinned
  (`tools/lint-float-bit-goldens.js` gates it).
* A shared fixture helper in each TU builds the reference patch, so a default-table change is one edit.
* **`configure → reset() → render` wherever an arm asserts on a configuration being in force from
  sample 0.** `prepare()` snaps every ramp and smoother to the **default** tables (S2 steps 11–12), so
  a setter called after it *glides*: `setLoopInputGain(i, 0.0f)` takes `kMixRampMs = 20 ms` to come
  down from `kDefaultLoopInputGain = 1.0f`, and `setCoupling(..., 0.0f)` takes `kCouplingSmoothMs` to
  come down from FR-033's 0.04 ring. Energy injected in those first 20 ms then circulates at
  `kDefaultLoopGain = 0.72` for seconds. `reset()` snaps all of it and clears the audio in the same
  call (S3.2), so the configuration is exact from the first sample. SC-002 (a)'s exact-zero taps and
  SC-015 (d)'s first arm are the two arms that would otherwise be unsatisfiable on a correct build.

**Render cost sanity, so the `[long]` tag is a considered cost and not a guess.** At the FR-080 gated
baseline of 71 111 ns per 512-sample block, one second of 48 kHz audio costs ≈ 6.7 ms of CPU. SC-001's
two 30-minute arms are ≈ 12 s each; SC-001 (c)'s 256 × 60 s is 4.27 h of audio ≈ 100 s of CPU; SC-021's
30 minutes is another ≈ 12 s plus FFT analysis. The `[long]` set is minutes, not hours.

### S12.1 The one new shared helper — magnitude-squared coherence

`tests/test_helpers/coherence.h` (new; **no CMake edit needed** — `test_helpers` is an INTERFACE
library exposing an include directory, S0/C-1). Built on `primitives/fft.h` and
`core/window_functions.h`, following `spectral_flux.h`'s shape (own STFT, never reads a processor's
internals, allocates, test-only):

```cpp
/// Welch magnitude-squared coherence between two equal-length signals.
/// @return mean coherence over [lowHz, highHz]; 0.0 when nothing measurable was produced.
/// NOT real-time safe (allocates). Test-only.
[[nodiscard]] inline double computeMeanCoherence(const float* a, const float* b,
                                                 std::size_t numSamples, double sampleRate,
                                                 std::size_t fftSize, std::size_t hopSize,
                                                 float lowHz, float highHz);
```

Implementation: Hann window, 4096-point, 50 % overlap; accumulate `Sxx`, `Syy` and complex `Sxy` per
bin over all frames; `C(f) = |Sxy|² / (Sxx·Syy)`; average over the bin range for `[lowHz, highHz]`;
guard `Sxx·Syy` against zero with the same `1e-12` floor `render_fingerprint.h` uses. Same ordered
input guards as `computeMagnitudeFlux` (`:78-84`): null pointer, zero length, non-power-of-two
`fftSize`, `hopSize == 0 || > fftSize`, non-finite/non-positive `sampleRate`, non-finite band edges.

It is used **only** by SC-002 (d), which **reports** the figure and does not gate on it.

### S12.2 TU assignment

| TU | Criteria | Notes |
|---|---|---|
| `dsp/tests/unit/systems/feedback_ecology_test.cpp` | SC-005, SC-006, SC-007, SC-008, SC-009, SC-010, SC-011, SC-013, SC-014, SC-015, SC-016, SC-017, SC-018, SC-022, **SC-023**, **SC-024**, **SC-025** | the default suite |
| `dsp/tests/unit/systems/feedback_ecology_spectral_test.cpp` | SC-001, SC-002, SC-003, SC-021 | the `[long]` set |
| `dsp/tests/unit/systems/feedback_ecology_perf_test.cpp` | SC-004 (a)–(f) incl. the FR-080 stage probe | `[.perf]` only, plus the `static_assert`ed baselines CI evaluates on every leg |
| `dsp/tests/unit/systems/feedback_ecology_nonfinite_test.cpp` | SC-012 only | **the one TU in the `-fno-fast-math` block**; also the only definition of `detail::FeedbackEcologyNonFiniteProbe` |

**SC-023, SC-024 and SC-025 are new in this plan revision**, added because a review found three
mechanisms with no criterion at all: FR-013's `kMaxResonatorQ = 100` ceiling (the headline correction
of the OQ-1 revision), FR-003's guard ladder and aliasing contract, and FR-055's lane decimation. All
three are recorded as spec additions in S16 C-6 and must land in the spec's Success Criteria and
Traceability table in the T0 docs write-back, or they will read as untraceable tests in the compliance
pass.

SC-019 and SC-020 are gate/compliance steps, not TEST_CASEs in the DSP suite (S13). The spec names
SC-019 as a TEST_CASE (`FeedbackEcology_StaticGates`); this plan reassigns it, and the reassignment is
recorded in S16 C-4 rather than left as a silent divergence.

### S12.3 Criterion by criterion

#### SC-001 — `FeedbackEcology_BoundednessSoak` `[long]` (spectral TU)

*The phase.* Five arms, one TEST_CASE with five `SECTION`s. **The worst-case corner** referred to
below is: `numLoops = 6`; every `ownFb = kMaxLoopGain = 0.90`; every off-diagonal
`coupling = kMaxCouplingPerPair = 0.5` (raw row sum 3.40, FR-035-normalised to 0.95); every filter
`Q = kButterworthQ`; every resonator RT60 at `kMaxDecayTime = 30 s`; wander at `kMaxWanderRateHz` with
`delayWanderFraction = kMaxDelayWanderFraction` and `cutoffWanderOctaves = kMaxCutoffWanderOctaves`.
**The four bounded assertions** are: peak `|sample| < kOutputClamp`; `getClampEngagementCount() == 0`;
`getNonFiniteResetCount() == 0`; every sample finite by `detail::isFinite`.

* **(a1) worst-case corner, held, wander on throughout.** The corner above, **governor `ratio = 1`
  (off)** so the structural rungs are measured without it. Reference drive for the first 60 s, silence
  for the remaining 29 min. The four bounded assertions over the whole render, plus **non-divergence**:
  the RMS of the final 60 s is not greater than the RMS of the 60 s ending at input cut-off, and the
  measured decay across the tail is **reported**. This arm carries no decay *threshold*, for the reason
  S8.1 works out: with wander at `kMaxWanderRateHz` the crossfade fires as often as the component can
  make it, and during a crossfade the equal-power two-tap blend can contribute up to
  `sqrt(2)` (+3.01 dB), so the worst-case instantaneous round-trip gain is
  `0.95 × 1.41421 × 1.000654 = 1.34438 > 1`. Rung 1's contraction argument does not hold inside a
  crossfade window; rungs 2–5 still bound the output, which is exactly what this arm measures. The
  correlated-tap case is *not* remote here: every resonator at a 30 s RT60 makes each loop's circulating
  signal narrow-band, so the two taps carry near-sinusoids.
* **(a2) the contraction arm — the same corner, on a fixture where rung 1 is the operative argument.**
  Identical to (a1) except that `setWanderEnabled(false)` is called at the input cut-off, so no new
  crossfade is triggered during the tail and the loop is the strict contraction S8.1 derives
  (`0.950621` per circulation, `−0.4399 dB`). Same four bounded assertions, **plus the RMS of the final
  60 s at least 60 dB below the RMS of the 60 s ending at input cut-off**. Margin, corrected: the
  shortest loop circulates 24.39 times/s (`−10.73 dB/s`, 60 dB in 5.6 s), the longest 2.227 times/s
  (`−0.980 dB/s`, 60 dB in 61.2 s); in series with a 30 s resonator RT60 an order-of-magnitude estimate
  for the composite tail is ~91 s against the arm's 1 740 s — a factor of ~19. **The 60 dB threshold is
  not relaxed by this split; it is moved onto the fixture where the argument it tests holds.** If (a2)
  fails, the component is wrong. If (a1)'s non-divergence clause fails, the finding is that crossfade
  regeneration is real at the corner, and the response is to reduce gain — never to widen the clamp,
  shorten the render or drop the arm.
* **(b) sustained drive, 30 min, governor on.** Same corner, drive for the full 30 min, governor at
  defaults. Same four bounded assertions plus **level stationarity**: the RMS of each of the thirty
  1-minute windows within **±1.5 dB** of the median window RMS.
* **(c) randomised sweep, accelerated.** 256 seeded configurations (`Xorshift32` in the test, seed
  index 0…255), every scalar drawn uniformly from its full clamped range and every off-diagonal
  coupling from `[0, kMaxCouplingPerPair]`; each rendered 60 s with the drive on for the first 10 s.
  Same four assertions. **`wetGain` is drawn from `[kMinWetGainDb, 0]` dB, not its full range**, because
  above `+4.27 dB` the trim can legitimately drive the FR-046 clamp (S8.4) and this arm's third
  assertion is that the clamp never engages; SC-013 (b) covers the positive half. Every other scalar,
  `mix` included, is drawn from its full clamped range.
* **(d) parameter-jump arm.** Reference patch, **with every off-diagonal pair pre-seeded at
  `kMaxCouplingPerPair`** (not the default 0.04 ring) so every `setLoopDormant` jump is drawn against
  the worst-case fixture for FR-031's post-gate construction. Every 500 ms for 5 minutes, jump one
  randomly chosen parameter to a randomly chosen extreme, including `setNumLoops`, `setLoopDormant` and
  whole-matrix `setCouplingMatrix` writes; `wetGain` again from `[-24, 0]` dB. The four bounded
  assertions apply to **every** setter without exception. **The click assertion** (no detection above
  SC-003's threshold at any jump instant) applies to every setter FR-076 declares **smoothed** —
  `setNumLoops` among them, which is why S5.3's count mask exists and why this arm's pre-seeding at
  `kMaxCouplingPerPair` is the fixture that catches an instantly-deleted coupling coefficient.

  **The stepped class and the exemption list are not the same set, and the previous revision conflated
  them.** FR-076 declares **seven** setters stepped: `setLoopFilterMode`, `setLoopResonanceHz`,
  `setLoopResonanceRt60`, `setWanderRate`, `setWanderEnabled`, `setSeed`, and the pair
  `prepare`/`reset`. This arm classifies all of them explicitly, so no setter falls between the two
  classes as `setWanderRate` did:
  * **not jumped at all:** `prepare` and `reset` — they are lifecycle calls, not parameter jumps, and
    SC-011 owns the re-`prepare()` behaviour.
  * **jumped and held to the click assertion:** `setWanderRate`. It is stepped only in the sense that
    it writes `laneDecimation_` and the lanes' `setSmoothness` immediately; it steps **no** mapped
    value in the signal path, so it cannot click and must not be excused from proving it.
  * **jumped and exempt from the click assertion only:** the remaining five — `setLoopFilterMode`
    (re-points the `SVF` output tap), `setLoopResonanceHz` and `setLoopResonanceRt60` (the OQ-1
    coefficient hard-swap), `setSeed` (restarts the modulation by design) and `setWanderEnabled` (S5.4:
    there is no reading under which a re-enable is jump-free). **The exemption list is a named
    `constexpr` array of exactly these five in the test** so it cannot quietly widen.

*If this criterion cannot be made to pass by reducing gain — never by widening the clamp or shortening
the render — the component is wrong.*

#### SC-002 — `FeedbackEcology_CrossLoopTransfer` `[long]` (spectral TU)

Gated instrument: **single-loop excitation, cross-loop transfer.** `setLoopInputGain(0, 1.0f)` and
`setLoopInputGain(i, 0.0f)` for `i ≥ 1`; reference patch otherwise; drive throughout a 60 s render with
the FR-073 taps installed (six `std::vector<float>` of `numSamples`, pointers in a
`std::array<float*, kMaxLoops>`).

**Fixture rule, mandatory for every point in the sweep: `prepare()` → write the whole configuration →
`reset()` → render.** Without the `reset()` this criterion is unsatisfiable on a *correct* build, and
arm (a) is where it shows. `prepare()` snaps every `inputRamp` to `kDefaultLoopInputGain = 1.0f`
(S2 step 12) and every coupling smoother to FR-033's 0.04 neighbour ring (S2 steps 7 and 11), so
`setLoopInputGain(i, 0.0f)` glides `1.0 → 0` over `kMixRampMs = 20 ms` and `setCoupling(..., 0.0f)`
glides over `kCouplingSmoothMs = 20 ms`: loops 1–5 would be **directly driven for ~960 samples**, and
that energy would then circulate at `kDefaultLoopGain = 0.72` through a 1.0 s-RT60 resonator for
seconds before `flushDenormal` took the taps to exact zero. `reset()` snaps all of it and clears the
audio in one call (S3.2), so the configuration is exact from sample 0 and (a)'s `== 0.0f` assertion is
a statement about a correct build rather than about a settle window. Applying the same rule at every
`c` also makes the five sweep points comparable, since `reset()` re-derives every lane from the stored
seed. Define

```
T(c) = 20·log10( RMS over the taps of loops 1..5 / RMS of loop 0's tap )
```

with every off-diagonal coupling set uniformly to `c`. Sweep `c ∈ {0, 0.02, 0.05, 0.10, 0.20}`.

* **(a) The floor is exact, not statistical.** Under the fixture rule above, at `c = 0` every undriven
  loop's tap is **exactly** `0.0f` on every sample from the first: its input sum is
  `0·monoIn + appliedOwnFb·0 + 0`, `reset()` snapped both the tap gain and the coupling to zero, and
  `reset()` zeroed the delay buffers. Assertion: `== 0.0f`, five taps, every sample of the 60 s render,
  no warm-up and no discarded window. The arm additionally asserts, as its own anti-vacuity guard, that
  `getLoopInputGain(i) == 0.0f` and `getLoopAppliedCoupling(j, i) == 0.0f` for every `j != i` at the
  moment the render starts — so a build that silently ignored the `reset()` snap fails here rather than
  hiding behind a tolerance.
* **(b) Monotone rise.** `T(c)` strictly increasing across the four non-zero points.
* **(c) Anti-vacuity: the rise must be more than mixing.** Repeat the sweep with every `ownFb = 0`
  (six parallel feed-forward chains) and record `T_ff(c)`. There the dominant path is a single hop, so
  `T_ff(c) ≈ 20·log10(c) + K` and `T_ff(0.20) − T_ff(0.02) ≈ 20 dB` (slightly more, because two-hop
  `c²` paths exist once the coupling is all-to-all — which is exactly why the baseline is **measured**
  in the same test rather than assumed). With feedback at `kDefaultLoopGain`, regeneration must make the
  same difference larger:
  `T(0.20) − T(0.02) ≥ (T_ff(0.20) − T_ff(0.02)) + kInteractionMarginDb`. A build that merely mixes the
  loop outputs reproduces the feed-forward figure and fails.
* **(d) Reported, never gated.** Mean pairwise magnitude-squared coherence over `[40 Hz, 4 kHz]`
  (Welch, 4096-point Hann, 50 % overlap) from S12.1's estimator, printed beside the transfer table.
  **Why it cannot gate:** all six loops are driven by the same mono signal through unity taps, so loop
  `i`'s output is `H_i(f)·X(f)` for one common `X`, and the magnitude-squared coherence of two outputs
  of a common source is `1` at every frequency **independent of `H_1`, `H_2` and therefore of the
  coupling**. The only decorrelation in the component is the `tanh` (within 2 % of linear at default
  levels) and the 0.03 Hz wander (near time-invariant across 85 ms Welch segments) — neither is a
  function of coupling. The roadmap's named metric goes on the record; the gate is the transfer
  measurement.
* **Provisional constant.** `kInteractionMarginDb` (provisional **1.5 dB**) is the only free number
  here and nothing in the tree anchors it. The build runs the sweep as a **probe first**, prints `T(c)`
  and `T_ff(c)`, and pins the constant from the measurement — under the FR-080 stop-and-surface rule.
  The **shape** assertions ((a)'s exact zero, (b)'s strict monotonicity, (c)'s strict inequality against
  the measured feed-forward baseline) may never be weakened, and the margin may be re-pinned only by
  recording the measurement that justifies it.

#### SC-003 — `FeedbackEcology_DelayDriftClickFree` `[long]` (spectral TU)

Input: 110 Hz sine at −12 dBFS (a pitched tone makes delay-position artefacts audible where noise hides
them), 300 s, reference patch but `delayWanderFraction = kMaxDelayWanderFraction = 0.5` and
`wanderRate = kMaxWanderRateHz` on every loop, so the crossfade fires as often as the component can
make it.

* **(a)** `ClickDetector` reports **zero** detections over the whole render, at a config whose
  `sampleRate` is the render rate.
* **(b)** Two clauses, both relative to the render's own level so a quiet render cannot pass: the peak
  absolute sample is **≥ 0.05**, and given that, the maximum absolute first difference is **below 5 %**
  of that measured peak and no first difference exceeds **8×** the median of the largest 1 000 first
  differences.
* **(c) The crossfades actually happened.** `Σ_i getLoopCrossfadeCount(i) ≥ 200` over the 300 s. Without
  this arm a build whose delay never moves passes (a) and (b) trivially. The count is defined on one
  named accessor (S5.5), not on step detection.

#### SC-004 — `FeedbackEcology_CpuBudget` `[.perf]` (perf TU) — see S14.

#### SC-005 — `FeedbackEcology_DelayMotion` (main TU)

* **(a)** At **44.1 kHz** (the binding rate, FR-023) on the reference patch, over **300 s**: for
  **every** one of the six loops, `getLoopCurrentDelayMs(i)` takes at least **3 distinct settled
  values** whose total spread is at least **2 %** of that loop's base delay; and within the first
  **120 s**, every loop whose base delay is ≥ 109 ms takes at least **4**.

  **Sampling, stated once and unambiguously.** The render is driven in **512-sample blocks** and both
  `getLoopCurrentDelayMs(i)` and `getLoopCrossfadeCount(i)` are read **once per block, between blocks**
  — one series, 10.67 ms apart, not the 1.33 ms control-chunk series an earlier revision named in the
  same sentence. The distinction matters and is not stylistic: `getCurrentDelaySamples()` is the
  **gain-weighted tap average** (`crossfading_delay_line.h:306-309`), so a reading taken while a
  crossfade is in flight returns an intermediate position. Counting those would inflate the
  distinct-value count with in-flight values and weaken the arm from "the delay actually stepped" to
  "the getter moved". A reading is therefore counted as **settled** only when
  `getLoopCrossfadeCount(i)` is unchanged from the previous sampled block **and** unchanged at the next
  — i.e. no crossfade started in the 21 ms bracketing it, and a 20 ms crossfade cannot have been in
  flight across the whole of it. The arm additionally reports `getLoopCrossfadeCount(i)` per loop, so
  "≥ 2 distinct settled values" and "≥ 1 completed step" are cross-checked against each other.
  **The floor that may never be weakened is ≥ 2 distinct settled values (one completed step) for every
  loop.**
  The counts above that floor are provisional until the build measures the step rate and may be
  re-pinned only by recording the measurement (FR-080's rule). If the shortest loop cannot clear the
  floor, the fix is FR-023's derivation — raise that loop's default wander fraction — never a shorter
  assertion.
* **(b)** At 44.1 kHz with `baseDelayMs = 10` and `delayWanderFraction = 0.05` (peak excursion
  `0.5 ms = 22 samples`, below `kCrossfadeThresholdSamples = 100`), `getLoopCurrentDelayMs(0)` is
  **constant** over 120 s — the documented consequence of the shipped delay line, asserted so it is a
  known property rather than a discovered bug.
* **(c)** At 192 kHz the reference patch also satisfies (a), which it must, since 100 samples is only
  0.521 ms there.

#### SC-006 — `FeedbackEcology_Governor` (main TU)

* **(a)** Drive the reference patch with white noise (RMS, fixed seed) swept −40 → 0 dBFS in 6 dB
  steps, 20 s per step. **The threshold crossing is measured, not assumed:** the governor reads
  `normGain · Σ b_i`, not the input, and the loops amplify. Run the sweep **twice** — once at
  `ratio = 1` (off) recording `getGovernorRms()` at the end of each step, then at the defaults — and
  assert against the recorded readings and `threshold = dbToGain(getGovernorThresholdDb())`:
  `getGovernorGain()` is **non-increasing** across the sweep; **exactly `1.0f`** at every step whose
  recorded RMS is below `threshold`; **strictly below `1.0f`** at every step above it; and **< 0.6** at
  0 dBFS. The sweep must **straddle** the threshold and the straddle is itself asserted. If it does not
  straddle, Assumption 1's input-level estimate is wrong and the response is to re-measure and record
  `kDefaultGovernorThresholdDb` the Phase-3 way — never to move an assertion.
* **(b)** Output RMS rises **sub-linearly**: the total output RMS increase from −40 to 0 dBFS is at
  least **12 dB less** than the 40 dB input increase, at `ratio = 8`.
* **(c)** `getGovernorGain()` never below `kGovernorMinGain = 0.05` anywhere in SC-001's sweep
  (asserted in this TU on a re-run of a representative subset, and again inside SC-001 (c)).
* **(d)** At `ratio = 1.0` the governor gain is **exactly `1.0f`** at every input level — reachable
  because the exponent `1/ratio − 1` is exactly `0`.
* **(e) No zipper from the governor:** on a 20 dB input step, `ClickDetector` reports zero detections
  in the 200 ms window beginning **one sample after the step sample**. The offset is load-bearing: at
  `mix = 1` the reference patch passes the drive's own discontinuity into the wet path, so a window that
  includes the step sample fires the detector on the fixture. The step is applied at a zero crossing of
  the noise generator's output where one exists within ±1 sample.
* **(f) Loop-count invariance — the arm a raw, unscaled tracker fails.** Repeat (a)'s sweep at
  `numLoops = 1` and at `numLoops = 6` and record each count's threshold-crossing input level (the input
  dBFS at which `getGovernorGain()` first drops below `1.0f`). The two agree **within 1 dB**. Under the
  rejected raw `Σ_i b_i` reading they differ by `10·log10(6) ≈ 7.8 dB`.

#### SC-007 — `FeedbackEcology_NoAllocation` (main TU)

`AllocationScope` (`allocation_detector.h:111`) around: 1 000 `processBlock` calls at irregular sizes
(1, 7, 64, 511, 512, 4096, 8193 in a repeating pattern); every setter on the surface driven to its
extremes; `reset()`; `setSeed()`; `setNumLoops()` across `[1, 6]`; `setLoopDormant`/`setLoopWake` edges;
`setCouplingMatrix`; `processBlockTapped` with all six taps. **Zero allocations, zero frees.** The
`prepare()` call is outside the scope.

#### SC-008 — `FeedbackEcology_Footprint` (main TU)

`getAllocatedBytes()` equals S10's table exactly at 44.1, 48, 96 and 192 kHz — **786 432** at 48 kHz —
and is **identical** for `maxBlockSamples` of 64, 512 and 8192 and for `numLoops` of 1 and 6. Before
`prepare()` it is 0.

#### SC-009 — `FeedbackEcology_SeedDeterminism` (main TU)

* **(a)** Two instances prepared identically with the same seed produce renders equal within
  `render_fingerprint.h` tolerances (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`).
* **(b)** `reset()` followed by the same render reproduces the first render within the same tolerances.
* **(c)** Two different seeds differ: `compareFingerprints(...).withinTolerance()` is **false**, the
  mean absolute difference exceeds **50 % of the reference render's own `meanAbs`**, and it also
  exceeds `kSampleTolerance` in absolute terms. **AMENDED after T015's first build, by measurement —
  spec.md SC-009 (c) carries the figures.** The original `100 × kSampleTolerance` = 0.05 absolute
  floor is unsatisfiable by any implementation by 43×: the reference patch renders at meanAbs
  0.0011695 (RMS −56.7 dBFS), so `mean|a−b| ≤ 0.00234` whatever the seeds. Measured: 0.0013306, i.e.
  1.138 × the render's own meanAbs (√2 for two independent renders, 0 for a seed that never reaches
  the lanes).
* **(d) Lane independence, two arms.**
  *Statistical:* over a 120 s render, the pairwise Pearson correlation between the twelve **lane
  target** trajectories read through `getLoopTargetDelayMs(i)` / `getLoopTargetCutoffHz(i)` is **below
  0.25** for every pair. The realised readings must **not** be used: `getLoopCurrentDelayMs` is the
  crossfade staircase, which changes only on completed ≥ 100-sample steps and, per SC-005 (b), is
  documented never to change at all in some configurations — a near-constant series has ~zero variance
  and its correlation is 0/0 or dominated by two quantisation steps.
  *Deterministic:* the twelve `deriveStreamSeed(seed, salt)` values from S1.6's salt table are pairwise
  **distinct**, computed directly in the test. This is the actual salt-collision guard; the statistical
  arm alone cannot tell a collision from two independent lanes that each happened to step twice.

#### SC-010 — `FeedbackEcology_BlockPartition` (main TU)

The same 60 s render produced with block sizes 512, 64, 7, 4096 and an irregular pseudo-random
partition, all within `render_fingerprint.h` tolerances. This is FR-007's absolute control grid under
test: a block-relative grid runs two control steps for a 36 + 28 split where an unsplit 64 runs one.

#### SC-011 — `FeedbackEcology_SampleRate` (main TU)

Rendered at 44.1, 48, 96 and 192 kHz with the reference patch: RMS within **1 dB**, spectral centroid
within **10 %**, no clamp engagements and no non-finite resets at any rate.

**Realised delays, with a staircase-aware bound.** With `setWanderEnabled(false)` called immediately
after `prepare()` — so every mapped delay is still the base value `prepare()` snapped it to (S2 step 15,
`snapToDelayMs`) — assert for every loop and every rate

```
|getLoopCurrentDelayMs(i) − configured ms| <= max(1 % of the configured ms,
                                                 kCrossfadeThresholdSamples / sampleRate in ms)
```

i.e. 2.268 ms at 44.1 kHz, 2.083 at 48, 1.042 at 96, 0.521 at 192. A bare 1 % is not achievable on the
reference patch (wander on moves the delay ±16 % of base by design), and even with wander off
`getCurrentDelaySamples()` is the gain-weighted tap average while `setDelaySamples` moves only the
inactive tap until the target has drifted 100 samples from the active one. The **measured** error is
reported at every rate; with wander off and the FR-005 snap it is expected to be **exactly zero**, and
the staircase term is the tolerance that keeps the criterion honest rather than a licence to drift.

Plus: `prepare()` at 1 Hz, at 0, at a negative rate and at a NaN rate all leave a usable object at
`kMinUsableSampleRate` or above (FR-083), and rendering through it produces finite output.

**Re-`prepare()` restores defaults (FR-005 (c)).** From the reference patch, set every off-diagonal
coupling to `kMaxCouplingPerPair` and every loop gain to `kMinLoopGain`, then call `prepare()` again at
the same rate. Assert `getCoupling(from, to)` reads back FR-033's default ring (`kDefaultCoupling` on
cyclic neighbours, `0.0f` elsewhere) and `getLoopGain(i)` reads back `kDefaultLoopGain` — the caller's
prior configuration does **not** survive a re-`prepare()`; only a `reset()` preserves it.

#### SC-012 — `FeedbackEcology_NonFinite` (the `-fno-fast-math` TU)

Non-finite values are built **from bit patterns behind a volatile sink**, never
`std::numeric_limits<float>::quiet_NaN()`, which folds to finite garbage on the `-ffast-math` legs
(`reference_fastmath_nan_in_tests`):

```cpp
[[nodiscard]] inline float bitNaN() noexcept {
    volatile std::uint32_t bits = 0x7FC00000u;  // quiet NaN
    std::uint32_t v = bits; float f; std::memcpy(&f, &v, sizeof f); return f;
}
[[nodiscard]] inline float bitInf(bool negative) noexcept {
    volatile std::uint32_t bits = negative ? 0xFF800000u : 0x7F800000u;
    std::uint32_t v = bits; float f; std::memcpy(&f, &v, sizeof f); return f;
}
```

* **(a)** Every float setter, given NaN and ±Inf, is a **no-op** and the matching getter still reports
  the previous value (FR-009).
* **(b) The public path.** A single non-finite **input sample** injected into a settled render: the
  render is finite and bounded from the injection sample onward; `getNonFiniteResetCount()` stays **0**
  (the poison never reaches `b_i`, so FR-047 correctly does not fire);
  `getClampEngagementCount()` stays 0; and the recovery edge is **click-bounded** — `ClickDetector` over
  the 200 ms following the injection reports at most **1** detection and the maximum absolute first
  difference stays inside SC-003 (b)'s shape bound. See S16 C-2 for the mechanism correction: this
  implementation intercepts at the dry sanitiser, one stage earlier than the spec's narrative, so in
  practice zero detections are expected and the "at most 1" bound is slack rather than tight.
* **(c) The trap itself, through the declared probe (FR-048).** Run once per non-self-healing stage.
  The two stages reach two **different** guards, and the arms are written against the guard each one
  actually reaches.
  * *the in-loop `DCBlocker`, reaching the per-sample rung-5 trap* —
    `detail::FeedbackEcologyNonFiniteProbe::poisonDcBlocker(fe, 2, bitNaN())` on a settled render.
    Assertions: `getNonFiniteResetCount()` increments by **exactly 1**; `clearLoopAudio(2)` ran (loop 2's
    tap is `0.0f` at the trap sample, and it stays `0.0f` for the S3.1 read-mute window — one delay
    length — after which the loop refills from its input tap rather than resuming its ring); the
    governor's follower was reset — `getGovernorGain()` finite at every subsequent sample and
    `getGovernorRms()` back within 1 dB of its pre-poison value within 2 s; and the main output is
    finite and bounded from the trap sample onward.

    **The other five loops, stated with the right scope.** The trap also calls `follower_.reset()`,
    which zeroes the governor's RMS state **globally**; at the next control step (≤ 64 samples later,
    inside the same 512-sample block) the governor recomputes its target from a near-zero RMS,
    `governorRamp_` re-targets toward `1.0` and every loop's `y_i = tanh(b_i · govGain)` moves. A
    blanket "bit-identical over the same block" claim is therefore false on a correct build unless the
    fixture happens to sit at `governorGain == 1.0f`, which SC-006 (a) explicitly says must be measured
    rather than assumed. So: the five other loops' taps are **bit-identical to an unpoisoned reference
    for every sample before the first control step after the injection**, and thereafter converge back
    to the reference within `kSampleTolerance` once `kGovernorRampMs` plus the follower's
    `kGovernorReleaseMs` have elapsed. The arm is additionally **repeated at `ratio = 1`** (governor
    off, `govGain` exactly `1.0f` by S5.6's exponent identity), where `follower_.reset()` has no
    observable effect on any other loop and the bit-identity claim holds for the whole block — that
    repeat is the clean isolation proof, and the first run is what pins the governor-coupled behaviour.
  * *the governor's `EnvelopeFollower`, reaching the S5.6 control-step guard* —
    `poisonFollower(fe, bitNaN())`. **This path does not and cannot reach the per-sample `b_i` trap**,
    and an earlier revision of this arm was unsatisfiable because it assumed it did: `LinearRamp::
    setTarget` converts a NaN target into an unramped step to **zero** (`smoother.h:342-348`), so
    `govGain` becomes `0.0f`, every `y_i` becomes `0`, every `b_i` stays finite, the trap never fires
    and the component sits permanently muted with both counters reading clean (S8.5). The guard that
    fires is S5.6's, at the source. Assertions, all against that guard: `getNonFiniteResetCount()`
    increments by **exactly 1** at the **next control step** (≤ 64 samples after the injection);
    `getGovernorRms()` is finite from the control step **after** that one and non-decreasing back
    toward its pre-poison value; `getGovernorGain()` is finite at every sample and returns to `1.0f`
    within `kGovernorRampMs` + one control chunk; the render is finite and bounded from the injection
    sample onward; and — the anti-mute clause, which is the whole point of the arm — the output RMS of
    the 2 s **after** the injection is within **1 dB** of the 2 s before it, so a build that merely
    muted itself fails here by 60 dB or more instead of passing every other assertion.
* **(d)** After the probe injection, a `reset()` returns the component to a state whose subsequent
  render matches a never-poisoned reference within `render_fingerprint.h` tolerances.

#### SC-013 — `FeedbackEcology_OutputClamp` (main TU)

* **(a)** `getClampEngagementCount() == 0` across a representative arm of each of SC-001, SC-002,
  SC-003, SC-006 and SC-011 (all `wetGain ≤ 0 dB`). Note what (a) alone is worth: with `wetGain ≤ 0 dB`
  the bound is structural, so (a) asserts nothing until (b) proves the counter increments at all.
* **(b) The one control that can reach the clamp.** Raising the *input* cannot — `tanh` discards it,
  which is the point of rung 2. Reference patch, `mix = 1`, `setWetGain(+24.0f)` (×15.85), drive at
  0 dBFS RMS for 10 s. The pre-clamp wet need only exceed `4.0 / 15.85 = 0.2524`, against a structural
  ceiling of `2.4495 × 15.85 = 38.8`. Assertions: `getClampEngagementCount() > 0`; every output sample
  inside `[-kOutputClamp, +kOutputClamp]`; every sample finite; `getNonFiniteResetCount() == 0`. The
  test **reports** the measured pre-clamp peak; if a build's wet path is quiet enough that 0.2524 is not
  reached, the response is a louder drive or a longer render — never a lower `kOutputClamp`.

#### SC-014 — `FeedbackEcology_Dormancy` (main TU)

* **(a)** `setLoopDormant(i, true)` and `setLoopWake(i, 0.0f)` produce **the same render** within
  `render_fingerprint.h` tolerances.
* **(b)** A settled-dormant loop reports `isLoopEngineActive(i) == false` and `getLoopGate(i) == 0.0f`
  exactly, while `getLoopTargetDelayMs(i)` and `getLoopTargetCutoffHz(i)` **keep changing** and
  `getLoopCurrentDelayMs(i)` / `getLoopCurrentCutoffHz(i)` are **frozen** (S7.4). Two distinct claims,
  asserted separately: the delay freeze (no `read()` call, so no tap advance) and the cutoff freeze (no
  `setCutoff` call, so the last commanded value stands).
* **(b2)** Behavioural lane-advance proof, depending on no getter being live while dormant: sleep a
  loop, hold 60 s of silence, wake it; the delay and cutoff it wakes with (read after FR-064's snap)
  differ from the values it slept with by more than one FR-023 step (delay) and by more than 1 %
  (cutoff).
* **(c)** Wake re-entry is a 50 ms ramp: `getLoopGate(i)` reaches its target in `50 ms × sampleRate`
  samples ±1 control chunk, monotonically, and `ClickDetector` reports zero detections across the edge.
  **Measured with every off-diagonal coupling at `kMaxCouplingPerPair = 0.5`** — the fixture at which a
  neighbour reading the *pre-gate* value would see a 0.5-amplitude step the instant the sleeping loop's
  gate settled at zero. Zero detections are asserted on every neighbour's tap as well as on the sleeping
  loop's own gate. This is the click-free proof of FR-031's post-gate construction, not a coincidence of
  the default 0.04 ring.
* **(d) The sleep edge clears the loop (FR-063).** Charge a loop with 30 s of drive, sleep it, wait 60 s
  of silence, then wake it with the input still silent: the loop's tap stays below **−80 dBFS** for the
  first 500 ms after the gate opens. A build that only skips the chain replays the frozen ring and fails
  by 60 dB or more.
* **(e)** `setLoopWake(i, 1e-9f)` snaps the gate target to exactly `0.0f` and `isLoopEngineActive(i)`
  becomes false (after the ramp settles and the next control step runs the sleep edge).

#### SC-015 — `FeedbackEcology_GainNormalisation` (main TU)

Every arm renders **≥ 100 ms** (5 × the 20 ms one-pole time-to-99 %, plus the `kCompletionThreshold`
snap) after writing the configuration and before reading the applied getters, because FR-035 normalises
the **smoothed** values and the raw/applied comparison is only meaningful once they have settled.

* **(a) The coefficients in force, not the reported total.** For 1 000 randomly drawn `ownFb` +
  coupling configurations, reconstruct the row sum from the FR-071 realised getters:

  ```
  S_i = getLoopAppliedOwnFeedback(i) + sum over j != i of getLoopAppliedCoupling(j, i)
  ```

  and assert `S_i <= kMaxTotalLoopGain + 1e-6f` for every loop; `S_i == kMaxTotalLoopGain` within
  `1e-5` whenever the raw configured sum exceeded it; and `getLoopAppliedTotalGain(i) == S_i` within
  `1e-6`. The `<=` half and the agreement clause are additionally sampled *before* settling, once per
  control chunk during the glide, to prove the bound holds at every instant and not only at the
  endpoints.

  **Why the arm is written this way.** `getLoopAppliedTotalGain(i)` is
  `std::min(g, kMaxTotalLoopGain)` **by construction** (S5.3), so on its own it satisfies both clauses
  identically and at every instant *whatever the coefficients in force are*: a build that computed the
  reported total correctly but dropped the `* scale` from the coefficient writes passed the previous
  revision's SC-015 (a), (b) and (d) unchanged, and the pre-settle sampling added nothing because the
  reported quantity was a tautology at every instant. `getLoopAppliedCoupling(from, to)` was added to
  the FR-071 surface for this arm (S9, S16 C-7); it is what makes the criterion a measurement.
* **(b)** The **configuration** getters (`getLoopGain`, `getCoupling`) still report the caller's clamped
  values, unchanged by the normalisation.
* **(c)** Worst case `ownFb = 0.90` with all five neighbours at `0.5` (raw 3.40) → applied total `0.95`,
  and SC-001 (a2)'s decay assertion still holds on a shortened (120 s) render.
* **(d)** *Count-inert, first arm:* `numLoops = 1` — established through `PrepareConfig{.numLoops = 1}`
  or by `setNumLoops(1)` followed by `reset()`, so FR-075's count mask is **snapped** to `0.0f` on the
  five unused slots rather than gliding over `kGainRampMs` (S2 step 11, S3.2, S5.3) — with every entry
  of the coupling matrix, including every pair among the unused slots 1…5, written to
  `kMaxCouplingPerPair`; then `getLoopAppliedTotalGain(0) == getLoopGain(0)` within `1e-6`, and
  `getLoopAppliedCoupling(j, 0) == 0.0f` exactly for every `j`. *Dormancy-inert, second arm:*
  `numLoops = 6`, every pair at `kMaxCouplingPerPair`, then loops 1…5 put to sleep one at a time; at
  every step `getLoopAppliedTotalGain(0)` is unchanged to within `1e-6` of its all-awake value.

#### SC-016 — `FeedbackEcology_TapEquivalence` (main TU)

A 60 s render through `processBlock` and the same render through `processBlockTapped` with all six taps
installed are **bit-identical** on both output channels (exact `==`, not a tolerance — this is the same
code path, so any difference is a real divergence). Also: `clamp(wetGain_linear × (Σ_i tap_i) /
sqrt(numLoops), ±kOutputClamp)` — S6 step 5's block, **in that order** — reproduces the wet output
within `kSampleTolerance`, which proves the taps are the real per-loop signals and not a debug
approximation. (With `mix = 1` the "wet output" is the output channel itself.)

#### SC-017 — `FeedbackEcology_ControlSurfaceClamps` (main TU)

Every float setter driven to ±10× its range reports the clamped value; every index-taking setter at
`kMaxLoops`, `kMaxLoops + 1` and `SIZE_MAX` is a silent no-op; every index-taking getter at those
indices returns the documented neutral without reading out of bounds (ASan-clean);
`setCoupling(i, i, x)` is a no-op and `getCoupling(i, i)` is `0.0f`; `setNumLoops` clamps to `[1, 6]`;
every getter on an **unprepared** instance returns its post-construction default (S9) — `getNumLoops()`
is 6, `getMix()` is `kDefaultMix`, `getLoopDelayMs(0)` is `41.0f` — with `getAllocatedBytes()` (0) and
`isPrepared()` (false) the only exceptions. **Two arms are new in this revision and are not optional
extras:**

* *the seeded-bounds arm.* `getLoopResonanceRt60(i)` is called for every `i` on a
  **default-constructed, never-prepared** instance and must return S1.2's realised table
  (0.1833 … 1.0000 s) to within `1e-3 s`. This getter clamps against `maxResonanceHz_`, and with the
  previous revision's `0.0f` initialiser the call was `std::clamp(1200.0f, 20.0f, 0.0f)` — UB, and
  MSVC's `<algorithm>` traps it with `_STL_VERIFY` (R-8). **This arm must therefore be run under an
  MSVC debug build as well as under ASan**, because the defect it guards is a trap rather than a wrong
  value and a Release run can sail past it.
* *the no-`prepared_`-gate arm.* On an unprepared instance, `setMix(0.42f)`, `setLoopGain(2, 0.5f)`,
  `setLoopDelayMs(3, 200.0f)` and `setCoupling(0, 1, 0.3f)` are each honoured by their getter
  immediately (FR-006, FR-009 — S8.6), and a subsequent `prepare()` **discards** all four back to the
  default tables (FR-005 (c)). Both halves are asserted, because the previous revision's `prepare()`
  step-8 comment implied a silent no-op gate that no FR states.

Plus the **constexpr-log equivalence arm**: the
`detail::constexprLn`-derived log2 constants of S5.4 agree with `std::log2` to within `1e-6` at runtime,
so the constexpr series and the library function cannot drift apart on any toolchain.

#### SC-018 — `FeedbackEcology_DryIdentity` (main TU)

* **(a)** With `mix = 0.0f` settled, a 10 s stereo render is **bit-identical** to its input on both
  channels (the S6 step-5 branch; a `-0.0f` input sample is included in the fixture, since that is the
  case the branch exists for).
* **(b)** During that render `getGovernorRms()`, `getLoopCurrentDelayMs(i)` and `getLoopGate(i)` all
  change, and a subsequent `setMix(1.0f)` produces, after the crossfade settles, a wet signal within
  `render_fingerprint.h` tolerances of a reference render that ran at `mix = 1` throughout — i.e. the
  loops were charged, not asleep.

#### SC-021 — `FeedbackEcology_LongEvolution` `[long]` (spectral TU)

A 30-minute render of the reference patch on 60 s of drive followed by a slow 0.05 Hz noise excitation.
Spectral centroid and an 8-octave-band energy vector on 10-second windows (180 windows).

* **(a) Not static.** The standard deviation of the centroid across the 180 windows is **> 3 %** of its
  mean.
* **(b) Not periodic.** For every lag from 15 to 25 minutes, the **cosine similarity between the
  log-band-energy vectors** (natural log of each of the 8 band energies, no per-vector normalisation
  beyond the log) of the two windows at that lag is **< 0.99**; the maximum over all such pairs is
  reported.
* **(c) Not divergent.** The centroid never leaves `[0.5×, 2×]` its median.
* **(d) Neither dying nor creeping.** The **least-squares slope** of the window RMS series in dB against
  time, multiplied by the render length, is within **±1 dB**.

Arm (d) deliberately overlaps SC-001 (b) — that one runs on the worst-case corner with the governor on,
this one on the reference patch under slow excitation, and a drift that appears under only one of the
two is exactly the finding worth having.

#### SC-022 — `FeedbackEcology_LoopCount` (main TU)

* **(a)** `numLoops = 5` and `numLoops = 6` each render non-silent and satisfy SC-001's four bounded
  assertions on a 60 s corner render.
* **(b) The FR-017 divisor is verified exactly, not through a level tolerance.** At each count the wet
  output reproduces `clamp(wetGain_linear × Σ_i tap_i / sqrt(numLoops))` — SC-016's identity evaluated
  at that count — within `kSampleTolerance`. A build that omits FR-017, or divides by the awake count,
  or divides by `kMaxLoops`, misses by 0.79 dB or produces NaN and fails by orders of magnitude more
  than the tolerance. **The wet RMS of the two patches is reported, never gated:** with six partially
  correlated loops the correct five-against-six ratio lies anywhere between 0 dB (incoherent summing,
  where `1/sqrt(n)` cancels exactly) and +0.79 dB (fully coherent), so *any* fixed band on that ratio is
  the wrong instrument.
* **(c) The mid-render count change is FR-075's, and it is click-free.** `setNumLoops` from 6 to 5
  during a render fades loop 5 out through the gate ramp, ramps the FR-017 divisor over `kGainRampMs`
  instead of stepping it by 0.79 dB, **and** fades loop 5's outgoing coupling out through the count
  mask (S5.3) instead of deleting it on the next control step; `ClickDetector` reports zero detections
  across the call and for 500 ms after it. **Run twice: once on the default 0.04 ring, and once with
  every off-diagonal pair pre-seeded at `kMaxCouplingPerPair = 0.5`.** The second fixture is the one
  that has teeth — on the 0.04 ring an instantly-deleted coupling coefficient is a −28 dB event the
  detector can miss, and at 0.5 it is the discontinuity SC-001 (d) is also measured against.

#### SC-023 — `FeedbackEcology_ResonatorRing` (main TU)

**The criterion that was missing.** OQ-1's whole correction — compute RBJ coefficients directly and
call `Biquad::setCoefficients`, never `configure`/`calculate`, because those clamp `Q` at
`biquad.h`'s `kMaxQ = 30` and silently undercut FR-013's `kMaxResonatorQ = 100` — had **no** success
criterion and **no** TEST_CASE in the previous revision. The only check was task-level prose in S17 T5
on `getLoopResonanceRt60`, and that getter cannot detect the defect: it derives from
`L.appliedResonanceQ`, which `updateResonator` stores from `rt60ToQ(...)` *before* the coefficients are
written (S4.1), so a build that regressed to `configure()` would still store `Q = 95.5`, still report
1.0 s, and still run a filter at `Q = 30`. This criterion measures the **realised ring**.

*Fixture (per loop under test, one `SECTION` each).* `prepare()`; then `setLoopGain(i, 0.0f)` on every
loop and every coupling to `0.0f` — so there is **no feedback** and the tap is the loop chain's own
impulse response, not a loop resonance; `setLoopCutoffHz(i, maxCutoffHz)` and
`setLoopFilterMode(i, Lowpass)` so the `SVF` in front of the resonator is near-transparent at the
resonator centre; `setWanderEnabled(false)`; `setLoopInputGain` 1.0 on the loop under test and 0.0
elsewhere; then **`reset()`** (the S12 fixture rule), then a single-sample unit impulse followed by
3 s of silence, read through the FR-073 tap.

* **(a) The realised ring at the lowest default centre.** Loop 5 (`kDefaultLoopResonanceHz[5] = 210 Hz`,
  `kDefaultResonanceRt60 = 1.0 s`, the one centre at which 1.0 s is reachable without clamping —
  `rt60ToQ(210, 1.0) = 95.50 < kMaxResonatorQ`). Measure the decay: take the peak of `|tap|` in each
  successive 1-cycle window, convert to dB, least-squares fit the dB-versus-time line over the span
  from −5 dB to −40 dB below the initial peak, and extrapolate to −60 dB. **Assert the measured RT60 is
  within ±10 % of 1.000 s**, and **report** it.
* **(b) The realised ring at a clamped centre.** Loop 0 (1200 Hz, request 1.0 s, `rt60ToQ` returns
  545.70 and is clamped to `kMaxResonatorQ = 100`, realised 0.1833 s). Same measurement, **within
  ±10 % of 0.1833 s**.
* **(c) The getter tells the truth about the filter in force.** For every loop,
  `getLoopResonanceRt60(i)` agrees with the arm's measured RT60 to within the same ±10 %, and
  reproduces S1.2's six-row table (0.1833, 0.2587, 0.3665, 0.5174, 0.7330, 1.0000 s) to within
  `1e-3 s`. (b) and (c) together are what tie the bookkeeping field to the coefficients.
* **The floor that may never be weakened**, because it is the whole point of the criterion: **loop 5's
  measured RT60 must exceed 0.5 s.** At `kMaxQ = 30` the realised figure is
  `30 × kLn1000 / (π × 210) = 0.3141 s` and at loop 0 it is `0.0550 s` — the defect misses by 219 %
  and 233 % respectively, against a ±10 % band. If the ±10 % band proves too tight for the measurement
  method (the delay and DC blocker are in the path, and the fit is over a finite window), the band may
  be re-pinned from a recorded measurement under FR-080's stop-and-surface rule; the 0.5 s floor and
  the shape of the assertion may not move, and the response to a genuine miss is to fix the coefficient
  path, never to lower the floor.

#### SC-024 — `FeedbackEcology_EntryPointContract` (main TU)

**FR-003's guard ladder and aliasing contract had no criterion.** The spec's Traceability maps FR-003
to SC-016/SC-017/SC-018 and SC-007/SC-008, none of which touches any of these paths, and the Edge Cases
section lists all four (spec `Edge Cases`, RT-safety boundaries). Four arms, on a prepared, settled
reference-patch instance:

* **(a) Null pointers write nothing and advance nothing.** For each of `inL`, `inR`, `outL`, `outR`
  passed as `nullptr` in turn (and all four at once): pre-fill both output buffers with a sentinel
  (`-7.5f`), record `getLoopCurrentDelayMs(i)`, `getLoopGate(i)`, `getGovernorRms()` and
  `getLoopCrossfadeCount(i)` for every loop, call `processBlock`, and assert the output buffers are
  **unchanged sample for sample** and every recorded value is **unchanged**.
* **(b) `numSamples == 0` consumes no control step.** Render 512 samples, call `processBlock(..., 0)`
  a hundred times, render another 512; the concatenation is **bit-identical** to an unbroken
  1 024-sample render on a second instance. This is the partition-invariance instrument SC-010 uses,
  applied to the zero-length case, and it is the only way to observe `controlPhase_`.
* **(c) An unprepared instance writes exactly `numSamples` zeros.** On a default-constructed instance,
  pre-fill both outputs with the sentinel beyond `numSamples`, call `processBlock` with
  `numSamples = 333`, and assert the first 333 samples of both channels are exactly `0.0f`, sample 333
  onward still holds the sentinel, and every FR-071 getter is unchanged.
* **(d) Both legal aliasings reproduce the non-aliased render bit-identically.** `inL == outL` (with
  `inR == outR`), and the crossed pairing `inL == outR` with `inR == outL`, each rendered 10 s on an
  instance configured identically to a non-aliased reference and compared with **exact `==`**, not a
  tolerance — the aliased and non-aliased paths are the same code, and S6 step 0's per-sample local dry
  capture is the reason both are legal. Any difference is a real divergence.

#### SC-025 — `FeedbackEcology_WanderRateMapping` (main TU)

**FR-055's decimation had no discriminating test.** Every wander-exercising criterion ran at the
default (decimation 2, effective 33.3 s, indistinguishable from a hard-wired-decimation-1 build's
`kTauMax`-saturated 30 s) or at `kMaxWanderRateHz` (decimation 1 in both builds). The mechanism only
bites near `kMinWanderRateHz`, and the `laneCounter_ %= newDecimation` rebase was untested entirely.

* **(a) The mapping, against S11's table.** `setWanderRate(kMinWanderRateHz)` →
  `getLaneDecimation() == 17`; `setWanderRate(kDefaultWanderRateHz)` → `2`;
  `setWanderRate(kMaxWanderRateHz)` → `1`. Plus the clamp ends: `setWanderRate(0.0f)` and
  `setWanderRate(1e6f)` land on `kMinWanderRateHz` / `kMaxWanderRateHz` as reported by
  `getWanderRate()`, with the matching decimations; a non-finite argument is a no-op (FR-009).
* **(b) The behavioural arm — the one a hard-wired decimation fails.** Two 300 s renders of the
  reference patch at 48 kHz from the same seed, one at `kMinWanderRateHz = 0.002` (decimation 17,
  effective correlation time 500 s) and one at `0.0333 Hz` (decimation 1, `tau` saturated at
  `kTauMax = 30 s`). For every loop, record the **range** (max − min) of `getLoopTargetDelayMs(i)`
  sampled once per block over the window. **Assert the mean range across the six loops at
  `kMinWanderRateHz` is at most half the mean range at 0.0333 Hz**, and report both. A build with
  `laneDecimation_` hard-wired to 1 produces a ratio of **1.0** and fails; the correct build's expected
  ratio is ≈ `sqrt(30 / 500)`-driven and comfortably beyond 2. The **factor of 2 is the floor** and may
  be re-pinned upward, never downward, from a recorded measurement (FR-080's rule).
* **(c) The rebase, not a reset.** Render 60 s of the reference patch while calling
  `setWanderRate(getWanderRate())` — the **same** value, so `smoothness` and `newDecimation` are
  unchanged — every 96 samples, a phase that is not a multiple of `kControlChunkSamples`. Compare
  against an unmolested 60 s reference from the same seed: **within `render_fingerprint.h`
  tolerances.** A build that wrote `laneCounter_ = 0` instead of `laneCounter_ %= newDecimation`
  advances the lanes on every control step instead of every second one and diverges grossly.

---

## S13. Build integration — the exact edits

**1. `dsp/tests/CMakeLists.txt`, the `dsp_systems_tests` source list.** Append after the Phase-3 block
(currently ending at `:435`, immediately before the closing `)` at `:436`), with the same comment shape
Phases 2 and 3 use — the list is **enumerated, not globbed**, so an unregistered TU silently drops out
of the build and its cases never run:

```cmake
    # Vorago Phase 5 (specs/vorago-phase5-feedback-ecology): FeedbackEcology.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   feedback_ecology_test.cpp           SC-005, SC-006, SC-007, SC-008, SC-009,
    #                                       SC-010, SC-011, SC-013, SC-014, SC-015,
    #                                       SC-016, SC-017, SC-018, SC-022, SC-023,
    #                                       SC-024, SC-025
    #   feedback_ecology_spectral_test.cpp  SC-001, SC-002, SC-003, SC-021   (the [long] set)
    #   feedback_ecology_perf_test.cpp      SC-004 (a)-(f) + the FR-080 stage probe   [.perf]
    #   feedback_ecology_nonfinite_test.cpp SC-012 only
    unit/systems/feedback_ecology_test.cpp
    unit/systems/feedback_ecology_spectral_test.cpp
    unit/systems/feedback_ecology_perf_test.cpp
    unit/systems/feedback_ecology_nonfinite_test.cpp
```

**2. `dsp/tests/CMakeLists.txt`, the `-fno-fast-math` block** (opens `:527`, Phase-3 entry `:838`,
closes `:855`). Add **exactly one** of the four TUs, after the Phase-3 entry:

```cmake
        # Vorago Phase 5: SC-012 injects NaN/Inf via bit patterns in this TU and needs
        # IEEE semantics to assert on them; it is also the only definition of
        # detail::FeedbackEcologyNonFiniteProbe. ONLY this one of the four Phase 5 TUs is
        # listed; the other three must NOT be. feedback_ecology_test.cpp and
        # feedback_ecology_spectral_test.cpp stay out so the FR-008/FR-009 guards are
        # proved in the /fp:fast + -ffast-math mode the header actually ships in. The perf
        # TU stays out too: -fno-fast-math would change the figures its baselines are
        # pinned to.
        unit/systems/feedback_ecology_nonfinite_test.cpp
```

A file may appear in only one `set_source_files_properties()` call, so this TU must not also appear in
the `-O2` block below it.

**3. `dsp/lint_all_headers.cpp`** — after the Phase-3 include at `:182`, before the Layer 4 block at
`:184`:

```cpp
// Vorago Phase 5 (specs/vorago-phase5-feedback-ecology), FR-001
#include <krate/dsp/systems/feedback_ecology.h>
```

**4. `tests/test_helpers/coherence.h`** — new file. **No CMake edit**: `test_helpers` is
`add_library(test_helpers INTERFACE)` with an include directory and no source list (S0). The spec's
"registered in `tests/test_helpers/CMakeLists.txt`" is corrected in S16 C-1.

**5. Documentation write-back (OQ-3), in the docs commit that precedes the build stage:**

* `specs/Vorago-roadmap.md` line 272 — amend "filter (`MultimodeFilter`)" → "filter (`SVF`)" and
  "resonator (single `IResonator` mode)" → "resonator (one RBJ bandpass, the `ResonatorBank` slot's Q
  range)". The Phase-4 precedent is to amend rather than carry deviations.
* `specs/vorago-phase5-feedback-ecology/spec.md` Traceability table — delete the two ratified
  `DEVIATION` annotations for roadmap line 272 (the `SVF` row and the `Biquad` row) now that the roadmap
  says the same thing. The **other** two deviation notes (line 278's pre-summed input, and the Dormancy
  row's FR-063 exception) stay: neither is written back to the roadmap.

**No other build file changes.** KrateDSP is header-only with a directory include and no enumerated
header list, so a new `systems/` header needs no `dsp/CMakeLists.txt` edit. There is no plugin work and
therefore no `ci.yml`, `release.yml`, clang-tidy-script or roster edit in this phase.

**Commands.**

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# build + run the layer that owns the new TUs
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5

# SC-020's consumer regression (the three suites that compile the untouched shared headers)
"$CMAKE" --build build/windows-x64-release --config Release \
    --target dsp_primitives_tests dsp_processors_tests dsp_systems_tests
for t in dsp_primitives_tests dsp_processors_tests dsp_systems_tests; do \
    build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done

# the [long] set, run explicitly (per-push CI excludes it; it runs nightly on all 3 OSes)
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -20

# SC-019's gates
node tools/lint-layers.js && node tools/lint-odr.js && node tools/lint-nonfinite-symbols.js \
  && node tools/lint-float-bit-goldens.js && node tools/lint-arch-guarded-includes.js \
  && node tools/lint-simd-aligned-loadstore.js && node tools/check-portability.js

# SC-020's byte-unchanged check
git diff --stat -- dsp/include/krate/dsp/systems/filter_feedback_matrix.h \
  dsp/include/krate/dsp/systems/feedback_network.h \
  dsp/include/krate/dsp/systems/flexible_feedback_network.h \
  dsp/include/krate/dsp/primitives/i_feedback_processor.h \
  dsp/include/krate/dsp/processors/multimode_filter.h \
  dsp/include/krate/dsp/processors/resonator_bank.h \
  dsp/include/krate/dsp/primitives/svf.h dsp/include/krate/dsp/primitives/biquad.h \
  dsp/include/krate/dsp/primitives/dc_blocker.h \
  dsp/include/krate/dsp/primitives/crossfading_delay_line.h \
  dsp/include/krate/dsp/primitives/delay_line.h \
  dsp/include/krate/dsp/processors/envelope_follower.h \
  dsp/include/krate/dsp/processors/brownian_drift.h        # must print NOTHING

# clang-tidy, single-TU on Windows for a small change set, .ps1 for the tree
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

# perf, ALONE, nothing else running (feedback_no_cpu_tests_isolation_only)
node tools/run-cpu-tests.js dsp_systems_tests
```

---

## S14. CPU budget — the plan for SC-004 and FR-080's lever ladder

**Basis:** nanoseconds per 512-sample block at 48 kHz, the
`resonance_drift_network_perf_test.cpp:68-76` basis ("a percent-of-core figure is not reproducible
across dev machines or CI runners"). One block period is **10 666 667 ns**, so the roadmap's 1 %/voice
is **106 666 ns/block**, and the gated baseline is `kBaseline × kRegressionFactor(1.5) <= 106 666`,
i.e. **`kBaseline <= 71 111 ns`**. Trial shape: best-of-25 × 500 blocks after 400 warm-up blocks. Tagged
`[.perf]` so the per-push CI filter (`~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`) excludes
it, with

```cpp
static_assert(kBaseline * kRegressionFactor <= kReferenceNs, "SC-004: over the 1 %/voice ceiling");
static_assert(kBaseline >= kReferenceNs / 50.0,              "SC-004: baseline implausibly low");
```

so the **absolute** ceiling is evaluated on every CI leg even though the case never runs there.

### S14.1 The stage-cost probe runs BEFORE the component is written (FR-080)

`FeedbackEcology_StageCostProbe`, `[.perf]`, a **probe not a gate**: it `REQUIRE`s only that every
figure is finite and strictly positive (a zero or a NaN means the measurement is broken, the one thing
that would make the table lie) and emits its verdict via `WARN`. Stages, each measured in the loop
position, per 512-sample block at 48 kHz:

| # | stage | shape | decides |
|---|---|---|---|
| (a) | `SVF::process` × 6 | smoothing enabled, a cutoff write every 64 samples | D-1, against (b) |
| (b) | `MultimodeFilter::processSample` × 6 | the roadmap's named filter, per-sample entry point (`multimode_filter.h:204`, which calls `updateCoefficientsFromSmoothed()` at `:218` **every sample**) | D-1 |
| (c) | `CrossfadingDelayLine::process` × 6 | 520 ms lines, a `setDelayMs` every 64 samples | the delay term |
| (d) | `Biquad::process` × 6 | fed directly-computed RBJ coefficients (S4.1) | FR-013's realisation (ii), against (g) |
| (e) | `DCBlocker::process` × 6 | | |
| (f) | `std::tanh` × 6 | | OQ-2 lever 1, against `FastMath::fastTanh` × 6 |
| (g) | six single-slot `ResonatorBank` instances, one enabled slot each | FR-013's realisation (i) | D-3 / OQ-1, against (d) |
| (h) | `EnvelopeFollower` RMS tracker | one `processSample` per sample | |
| (i) | twelve `BrownianDrift` lanes | `processBlock(64)` at decimation 1, 2 and 17 | FR-055's cost |
| (j) | the per-sample ramp bank | **fifteen** `LinearRamp::process` advances per sample: `mix`, wet trim, 6 input gains, 6 gates, governor, `normGain` | the fixed overhead |

*(g) is expected to lose to (d) and D-3 already predicts why: a `ResonatorBank` costs, per sample, three
`OnePoleSmoother` advances plus a 16-iteration loop with 15 `continue`s (`resonator_bank.h:471-473`,
`:486-489`) to reach one enabled biquad — six of those is 18 smoother advances and 96 loop iterations
per sample for six biquads' worth of work. The probe measures it anyway so the decision is a number, not
a reading of the header.*

### S14.2 The SC-004 arms

* **(a)** reference patch, six loops awake, wander on — **this is `kBaseline`.**
* **(b)** six loops, wander **off** (FR-056) — reported, and required to be **within +2 % of (a)**, a
  band and not a bare inequality. FR-056 keeps the lanes advancing and only zeroes the depth term, so
  the true difference is a handful of control-rate multiplies inside a best-of-25 × 500-block
  measurement, well under run-to-run noise. An untoleranced comparison fails on a scheduling accident,
  not on a defect — the Phase-3 precedent, where "≥ 10 % saving with wander off" was structurally
  unreachable once the Dormancy rule fixed that lanes keep advancing, and had to be amended mid-build.
* **(c)** all six loops dormant — must be **at least 40 % cheaper** than (a). This is the observable
  consequence of FR-062's skipped chain (S6 note 5) and the only thing that proves dormancy is not
  cosmetic.
* **(d)** `numLoops = 1` — reported; sets the per-loop marginal cost.
* **(e)** the S14.1 probe table, printed not gated.
* **(f) The transition blocks — the ones the steady-state arms cannot see, and the arm S3.1's
  correction requires.** Arms (a)–(d) are all steady state, so a per-transition cost spike is
  unmeasured by design. Two blocks are timed here, each **gated against the same absolute
  106 666 ns/block ceiling** (not against `kBaseline`, since a transition is allowed to cost more than
  the steady state, just not more than the block period):
  * the 512-sample block containing a **simultaneous six-loop sleep edge** — drive the reference patch
    to steady state, then `setNumLoops(6 → 1)` and time the block in which all five dropped gates
    settle at zero and `clearLoopAudio` runs five times inside one 64-sample control chunk;
  * the 512-sample block containing a **rung-5 trap fire**, injected through the FR-048 probe.

  This arm exists because the previous revision put `CrossfadingDelayLine::reset()` — a 131 072-byte
  `std::fill` per loop at 48 kHz, 524 288 at 192 kHz — on both of those paths, which is a single fill
  larger than the whole 8 889 ns control-chunk budget and, at six loops, 786 KB at 48 kHz and 3.1 MB at
  192 kHz inside one chunk. S3.1 replaced it with an O(1) read-mute window; **this arm is the
  measurement that keeps it O(1)**, and the measured figures for both blocks are recorded in this
  section when the build runs. The same arm is repeated at **192 kHz**, where the block period is
  2 666 667 ns and the ceiling is 26 667 ns, because that is where an O(buffer) regression would show
  first.

The percent-of-core figure is **reported, never asserted**. Run in isolation, nothing else executing:
`node tools/run-cpu-tests.js dsp_systems_tests`. A test that flips verdicts between runs is measuring
the machine, not the code (settle 20 s between suites; the runner does this).

### S14.3 The lever ladder, and the stop line

***STOP-AND-SURFACE RULE (inherited verbatim from `resonance_drift_network_perf_test.cpp:59-65`) —
NON-NEGOTIABLE:*** no implementing agent may lower `kMaxLoops`, raise the budget, relax a threshold or
shrink a workload to make a figure fit. **Reduce cost, never move the line.**

**Levers 1–3 are pre-authorised from the measured probe table (OQ-2)** and may be applied without a
further ask:

1. Replace `std::tanh` with `FastMath::fastTanh` (`fast_math.h:65`) under its own error-bound test. The
   FR-042 bound is **preserved exactly** — the approximation returns exactly ±1 beyond ±3.5 and is
   within 0.05 % below it — so rung 2 is unchanged. The error-bound test asserts `|fastTanh(x) −
   std::tanh(x)| < 5e-4` over `x ∈ [-6, 6]` on a 10 001-point grid and `|fastTanh(x)| <= 1` everywhere.
2. Stepped resonator retunes (FR-013) — **already the default** after OQ-1, so this lever is banked in
   the baseline rather than a further step to take.
3. Skip a coupling-pair smoother's advance once it has settled (FR-034's carve-out, S5.2 step 2).

**If the total still exceeds `71 111 ns`/block after levers 1–3, the build STOPS and surfaces the
measured per-stage table.** Levers 4 and 5 are **user** decisions taken from that table, not agent
decisions:

4. Drop the resonator to a second `SVF` in Bandpass mode, dropping the RT60 surface (and with it
   `getLoopResonanceRt60`, FR-013's `kMaxResonatorQ = 100` ceiling, SC-023 in its entirety, and part of SC-001's 30 s-RT60
   corner).
5. Reduce the default `numLoops` from 6 to 5 (roadmap line 272 permits it, D-4).

**A first-order cost estimate, so the probe has something to falsify.** Per sample the per-loop chain is
one `SVF::process` (≈ 12 flops + one divide while the smoother moves), one
`CrossfadingDelayLine::process` (one write + two linear-interpolated reads + a mix), one
`Biquad::process` (5 mul + 4 add, TDF2), one `DCBlocker::process` (2 add + 1 mul), one `tanh`, and two
multiplies — call it 60–90 ns per loop-sample on a modern core if `tanh` dominates, i.e. 360–540 ns per
sample for six loops, which is **far** over budget: the ceiling is 71 111 ns / 512 samples = **139 ns
per sample for the whole component**. So `tanh` at six per sample is the term to watch, and lever 1 is
the expected outcome. `FastMath::fastTanh` is a Padé rational (~5 flops, no branch on the common path)
and should bring the six-`tanh` term from tens of ns to a few. The fifteen `LinearRamp::process`
advances (stage (j)) are the second term to watch: each early-outs on `current_ == target_`
(`smoother.h:372-374`), so in the steady state they are fifteen predicted branches, but during any
ramp they are fifteen add-compare-store sequences. If (j) alone is a large fraction of the budget the
finding is real and belongs in the surfaced table — **not** in a decision to advance ramps less often,
which would break SC-010's partition invariance.

---

## S15. Risks and mitigations

| # | Risk | Why it is plausible here | Mitigation, and what fails if the mitigation is wrong |
|---|---|---|---|
| **R-1** | **Control-rate smoothers configured at the audio rate** (S5.0) — a 20 ms coupling glide silently becomes 1.28 s | `OnePoleSmoother::configure` takes a per-sample rate and nothing in the type system distinguishes the two clocks | Configure with `fs / kControlChunkSamples`, stated in S5.0 and repeated in the header at the call site. No criterion measures the coupling glide directly, so **this bug would ship**; the plan's mitigation is the comment plus a targeted assertion inside SC-015 (a)'s pre-settle sampling — the applied value must reach the target within ~25 ms of control steps, which a 1.28 s constant fails |
| **R-2** | **`tanh` six times per sample blows the 139 ns/sample budget** | `std::tanh` is tens of ns on MSVC | OQ-2 lever 1 is pre-authorised (S14.3) and preserves the FR-042 bound exactly. If both `tanh` variants still miss, **stop and surface** — never lower `kMaxLoops` |
| **R-3** | **A NaN escapes the ordered output clamp** | `std::clamp`/ordered comparisons pass NaN through; Phase 3 hit exactly this via parametric pumping and had to add an explicit guard (`resonance_drift_network.h:1810+`) | Here every `b_i` is made finite by rung 5 *before* it is used, and nothing retunes at audio rate, so `wet` is finite by construction (S6). **If SC-001 (c)'s 256-config sweep ever produces a non-finite output sample, the response is to add Phase 3's explicit pre-clamp finiteness guard on `wet`, not to widen a tolerance.** The sweep is the detector |
| **R-4** | **The delay never moves and nothing notices** | `CrossfadingDelayLine`'s 100-sample threshold makes a sub-threshold wander produce *no* motion, silently | FR-023's derived default table (S1.2) puts every loop at ≈0.5 σ_Δ per step at the binding 44.1 kHz rate, and SC-005 (a)'s never-weakenable floor is "≥ 2 distinct values for every loop". SC-005 (b) asserts the documented *absence* of motion in the sub-threshold configuration so the property is known rather than discovered |
| **R-5** | **`processBlockTapped` is not bit-identical to `processBlock`** (SC-016) | the DSP library builds with `-ffast-math` on the macOS and Linux legs, where a compiler may in principle schedule the two branches differently | The tap write is a pure store of an already-computed `prevOut_[i]`, placed **after** the loop, in **one** function body (`processBlock` forwards). The loop body is scalar and sequentially dependent (feedback), so it cannot vectorize differently. If a leg still diverges, the fix is to make the tap write unconditional into a member array and copy out — **never** to relax SC-016 to a tolerance |
| **R-6** | **Denormals circulate on the coupling path** as the network decays over SC-001 (a1)/(a2)'s 29-minute silent tail | every sub-object flushes its **own** state, but `prevY_`/`prevOut_` and the wet sum belong to no sub-object | FR-084's explicit `detail::flushDenormal` on all three (S6 steps 3 and 5). Correct on a host that has not set the MXCSR bits, independently of the process-wide FTZ/DAZ the test harness enables (`tests/test_helpers/enable_ftz_daz.h`, applied at `dsp/tests/dsp_test_main.cpp:10,13`) — which is exactly why a green local test run is **not** evidence here |
| **R-7** | **`constexpr std::log2` breaks the macOS/Linux legs while Windows stays green** | it is a GCC/MSVC builtin extension Clang rejects; the Phase-3 header records the same trap (`:255-266`) | S5.4 mandates `detail::constexprLn(x)/detail::kLn2`; SC-017's constexpr-log arm pins the series to `std::log2` at runtime; `node tools/check-portability.js` before every commit. MSVC-green proves nothing |
| **R-8** | **`std::clamp` with inverted bounds is UB that MSVC traps** at a low sample rate | `[kMinResonatorFrequency, 0.45·fs]` inverts below ~44 Hz | `kMinUsableSampleRate = 8000.0` floor plus the class-scope ordering `static_assert` (S1.2); SC-011 renders through 1 Hz / 0 / negative / NaN rates |
| **R-9** | **`std::numeric_limits<float>::quiet_NaN()` folds to finite garbage** in the non-finite TU on the macOS leg | `-ffast-math` implies `-ffinite-math-only` | Bit patterns behind a volatile sink (S12.3 SC-012), **and** the TU is the one entry in the `-fno-fast-math` block (S13 edit 2). The other three TUs must never name a non-finite value |
| **R-10** | **A `[long]` case is tagged that should not be, or a sentinel is tagged that must not be** | the project rule is explicit | `[long]` goes on SC-001, SC-002, SC-003 and SC-021 — multi-minute renders whose assertions are toolchain-**independent**. It goes on **none** of SC-012 (non-finite sentinel), SC-010/SC-011 (bounded-grid / rate sentinels) or SC-008 (state-format), which must stay in the per-push lane |
| **R-11** | **`getLoopAppliedTotalGain` read before the smoothers settle** makes SC-015 (a)'s "equals `kMaxTotalLoopGain` whenever the raw sum exceeded it" flaky | FR-035 normalises the **smoothed** values | Every SC-015 arm renders ≥ 100 ms after writing the configuration (S12.3). The `<=` half is additionally sampled during the glide, which is a stronger claim, not a weaker one |
| **R-12** | **The wet path is too quiet or too loud at `kDefaultWetGainDb = 0`** — D-8 flags this as an *unmeasured* constant, unlike Phase 3's measured +34.5 dB | this component's loops are broadband and unity-ish rather than twelve narrow bandpasses, so the Phase-3 reasoning does not transfer | SC-018 (b) and SC-022 are the criteria that would catch it. If they show the wet is unusable, the response is to **measure and record** a constant the Phase-3 way (a `[.calibration]` case that prints the figure), never to nudge one |
| **R-13** | **An unregistered TU silently drops out** of `dsp_systems_tests` and its cases never run | the list is enumerated, not globbed | S13 edit 1 names all four; the build stage's first check is that `dsp_systems_tests.exe "FeedbackEcology_*"` reports a non-zero case count |
| **R-14** | **A shared header gets "improved" while composing it** — `ResonatorBank`, `BrownianDrift`, `CrossfadingDelayLine` and `SVF` are consumed by Seraphis and by Vorago Phases 2–3, whose criteria pin their behaviour | the RBJ formula is *copied* out of `resonator_bank.h`, which invites "just add a free function there instead" | FR-090's byte-unchanged requirement, checked by SC-020's `git diff --stat` (S13) plus the three-suite regression. FR-091: nothing here extracts, generalises or unifies the single-loop infrastructure; if a future phase wants a shared micro-loop primitive it can be lifted then, from two working implementations rather than one and a guess |
| **R-15** | **A same-layer include creeps in** (`filter_feedback_matrix.h`) because the lint would allow it | `tools/lint-layers.js:74` permits Layer 3 → Layer 3 | FR-001's include list is exhaustive and the header carries the reason: the bar is `FilterFeedbackMatrix`'s capacity `static_assert(N >= 2 && N <= 4)` (`:72-73`), not the layer. FR-092 forbids raising it: it would add two explicit instantiations to a header every Iterum/Disrumpo build already compiles, its `std::array<std::array<DelayLine, N>, N>` grows as `N²` (36 lines at N = 6 against this component's 6), and its per-path delay model is not the roadmap's topology |
| **R-16** | **A NaN in the governor's `EnvelopeFollower` mutes the whole component permanently, silently, and with both health counters reading clean** | `processRMS` never self-heals (`envelope_follower.h:313-325`), the S5.6 law turns NaN into a NaN target, and `LinearRamp::setTarget` converts a NaN target into an unramped step to **zero** rather than propagating it (`smoother.h:342-348`) — after which every `b_i` is finite and the per-sample rung-5 trap can never fire | The S5.6 control-step guard reads the follower's value, tests it with `detail::isFinite` **before** it can reach the ramp, resets the follower, increments `nonFiniteResets_` and re-targets the governor to `1.0f`; a second finiteness test guards the computed target. SC-012 (c)'s second arm asserts against that guard, including an explicit **anti-mute clause** (output RMS within 1 dB across the injection). Without the guard that arm is unsatisfiable as written and the failure ships |
| **R-17** | **An O(buffer) delay clear lands on the audio thread and blows the block budget** | FR-019 names `delay_[i].reset()`, which is `std::fill` over 131 072 B per loop (524 288 B at 192 kHz); two of `clearLoopAudio`'s four callers are audio-thread paths, and `setNumLoops(6 → 1)` fires up to six of them inside one 64-sample control chunk — 786 KB at 48 kHz against an 8 889 ns chunk budget, 3.1 MB at 192 kHz. Phase 3's precedent does not cover it: its sleep-edge clear is an O(1) biquad-state clear | S3.1 replaces the fill with an O(1) **read-mute window** of exactly one delay length, using `CrossfadingDelayLine`'s separable public `write()`/`read()`; `prepare()`/`reset()` keep the real wipe because they are control-thread calls. SC-004 (f) measures the six-loop sleep-edge block and the trap block at 48 and 192 kHz against the absolute ceiling. **If a future edit puts a buffer wipe back on an audio-thread path, SC-004 (f) is the detector** |

---

## S16. Spec corrections and open items

Where the code disagrees with the spec, the code wins. **Nine corrections** and four open items. C-1 to
C-3 are unchanged from the previous revision; C-4 to C-9 are the review findings this revision resolves,
and each names the spec edit the T0 docs write-back must make so the compliance pass has a row to fill.

* **C-1 — `tests/test_helpers/CMakeLists.txt` needs no edit.** The spec's Traceability note says the new
  coherence estimator is "registered in `tests/test_helpers/CMakeLists.txt`". Verified this session:
  that file is `add_library(test_helpers INTERFACE)` with `target_include_directories(... INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR})` and **no source list**. A new header is picked up by the include
  directory alone. The plan drops that edit (S13); nothing else changes.
* **C-2 — SC-012 (b)'s stated mechanism is one stage off, and the assertions still hold.** The spec
  attributes the interception of a non-finite input sample to `SVF::process` resetting six filters, and
  bounds the resulting edge at "at most 1 detection". This implementation sanitises the dry per channel
  **before** the mono sum (S6 step 0), following Phase 3 (`resonance_drift_network.h:1766-1772`), which
  is **required** for the render to stay finite at any `mix < 1` — otherwise the poisoned sample reaches
  `outL` through the crossfade untouched. Consequently the SVFs are never poisoned, no `reset()` storm
  happens, and the expected detection count is **zero**, not one. Every SC-012 (b) assertion as written
  remains true (finite from the injection onward, both counters 0, ≤ 1 detection, first-difference
  bound); only the narrative's mechanism differs, and the header records both stages of the interception
  so a later reader does not "fix" the sanitiser away. `SVF::process`'s self-heal remains the second
  line and is still what the header cites for FR-047's unreachability.
* **C-3 — `kMaxDelaySeconds` is a derived constant, and 44.1 and 48 kHz share a footprint.** The spec
  gives FR-082's formula and the 48 kHz figure. Worked out at all four rates (S10), 44.1 and 48 kHz both
  land in the 32 768-sample power-of-two bucket and report the **same** 786 432 bytes. SC-008 must not
  read that equality as a defect; the plan states it so the test author does not write an inequality
  between them.

* **C-4 — SC-019 is not a Catch2 case, and the plan says so instead of silently reassigning it.** The
  spec names SC-019 as a TEST_CASE, `FeedbackEcology_StaticGates` (spec `SC-019`), but every clause of
  it is a `node tools/lint-*.js` / `check-portability.js` invocation — nothing a runtime test can
  observe. This plan discharges SC-019's compliance row with **the S13 gate commands and their
  transcript**, and the T0 write-back **drops the case name from the spec** so the compliance pass does
  not read it as a missing test. SC-020 is already written as a compliance-pass `git diff` check and
  needs no edit. Nothing else about either criterion changes.
* **C-5 — SC-002 (a)'s exact-zero assertion needs the fixture rule, not a weaker assertion.** The spec
  justifies `== 0.0f` on "its input sum is `0·monoIn + appliedOwnFb·0 + 0`, and `prepare()` zeroes the
  delay buffers". True of the *coefficients*, but not of the path to them: `prepare()` snaps
  `inputRamp` to `kDefaultLoopInputGain = 1.0f` and the coupling smoothers to FR-033's 0.04 ring, so
  the setters that establish `c = 0` **glide** over 20 ms and drive loops 1–5 for ~960 samples first.
  The assertion is kept **verbatim and at full strength**; the plan adds the mandatory fixture rule
  (`configure → reset() → render`, S3.2 and S12) that makes it reachable on a correct build. No spec
  edit is required beyond a one-line note in SC-002's fixture description recording the rule.
* **C-6 — three mechanisms had no criterion, and the spec gains SC-023, SC-024 and SC-025.** FR-013's
  `kMaxResonatorQ = 100` ceiling (the headline correction of the OQ-1 revision, whose only check was
  task-level prose on a getter that cannot detect the defect — S4.2), FR-003's guard ladder and
  aliasing contract (mapped in the spec's Traceability to five criteria, none of which touches a null
  pointer, a zero length, an unprepared render or either aliasing), and FR-055's lane decimation (no
  arm rendered anywhere near `kMinWanderRateHz`, where the mechanism is the difference between 500 s
  and a `kTauMax`-saturated 30 s). The three new criteria are specified in full in S12.3; the T0
  write-back adds them to the spec's Success Criteria and to the Traceability rows for FR-013,
  FR-003 and FR-050–FR-056.
* **C-7 — FR-071's realised-state surface gains `getLoopAppliedCoupling(from, to)`.** Without it
  SC-015 could not observe the FR-035 normalisation at all: `getLoopAppliedTotalGain(i)` is
  `std::min(g, kMaxTotalLoopGain)` by construction, so it satisfies both of SC-015 (a)'s clauses
  identically whatever the coefficients in force are, and a build that omitted the `* scale` on the
  coefficient writes passed (a), (b) and (d) unchanged. The getter is `[[nodiscard]] noexcept`, returns
  `0.0f` for an out-of-range index or `from == to` without indexing (FR-009), and adds no state. The T0
  write-back adds it to FR-071's enumeration.
* **C-8 — FR-019's `delay_[i].reset()` becomes an O(1) read-mute window on the two audio-thread
  callers.** FR-019 names the call; the call is a `std::fill` over 131 072 B per loop at 48 kHz and
  524 288 B at 192 kHz (`delay_line.h:281-285`), and two of the four callers — the FR-063 sleep edge
  and the FR-047 trap — run on the audio thread, where `setNumLoops(6 → 1)` can fire six of them inside
  one 64-sample control chunk. S3.1 keeps FR-019's one-owner shape and FR-063's **audible** property
  ("a woken loop refills from its input tap rather than replaying the stale ring") and replaces the
  fill with a read-mute window of exactly one delay length, using `CrossfadingDelayLine`'s separable
  public `write()`/`read()`. `prepare()` and `reset()` keep the real wipe; they are control-thread
  calls. The T0 write-back amends FR-019 to state the split, and SC-004 gains arm (f) as the detector.
* **C-9 — FR-001's include enumeration adds `primitives/delay_line.h` and drops
  `core/audio_constants.h`.** `getAllocatedBytes()` calls `Krate::DSP::nextPowerOf2`
  (`delay_line.h:26`), which reaches the header only transitively via `crossfading_delay_line.h:31`
  today; `kMaxAudioFreqHz` has no user anywhere in S1.2–S6. See S1.1.

**Open items — decisions the build takes from measurement, under FR-080's stop-and-surface rule:**

* **O-1 — `kInteractionMarginDb` (SC-002 (c)), provisional 1.5 dB.** Nothing in the tree anchors it. The
  build runs the sweep as a probe first, prints `T(c)` and `T_ff(c)`, and pins the constant from the
  measurement. The shape assertions may never be weakened.
* **O-2 — SC-005 (a)'s counts above the floor.** "≥ 3 distinct values over 300 s" and "≥ 4 within 120 s
  for loops ≥ 109 ms" are provisional: `BrownianDrift`'s decorrelation time under the FR-055 mapping is
  not pinned by this spec, so the number of independent excursions in a window is a measured quantity.
  The floor — **≥ 2 distinct values for every loop** — may never move. If the shortest loop cannot clear
  the floor, the fix is FR-023's derivation (raise that loop's default wander fraction), never a shorter
  assertion.
* **O-3 — `kDefaultWetGainDb = 0.0f` is unmeasured** (D-8, R-12). SC-018 (b) and SC-022 are the
  criteria that would catch it. If the wet is unusably quiet or loud, add a `[.calibration]` case that
  prints the figure and record the constant the Phase-3 way (`resonance_drift_network.h:172-229`).
* **O-4 — SC-004's outcome may reach lever 4 or 5** (S14.3). Both are **user** decisions. The build
  stops and surfaces the measured per-stage table rather than reaching for them.

---

## S17. Suggested task order (`tasks.md` owns the real breakdown)

The ordering below exists so `tasks.md` has a dependency-correct skeleton; it is not itself the task
list.

| # | Task | Verify |
|---|---|---|
| **T0** | **Docs write-back (OQ-3 + S16 C-4…C-9):** roadmap line 272 amendment; deletion of the two ratified `DEVIATION` annotations in the spec's Traceability table; **and the six spec edits S16 C-4 to C-9 name** — drop SC-019's case name, note SC-002's `reset()` fixture rule, add SC-023/SC-024/SC-025 to the Success Criteria and Traceability, add `getLoopAppliedCoupling` to FR-071, amend FR-019's audio-thread clear, amend FR-001's include list. Committed **before** any code cites any of them | roadmap line 272 names `SVF` and the RBJ bandpass; the spec's Traceability table has two DEVIATION notes left (line 278, Dormancy), not four; the spec's SC list runs to SC-025 and every one of them has a Traceability row |
| **T1** | Create `feedback_ecology_perf_test.cpp` with **only** the S14.1 stage probe, `[.perf]`. Register all four TU names in `dsp/tests/CMakeLists.txt` now (three empty), plus the `-fno-fast-math` entry and the `lint_all_headers.cpp` include | probe builds and prints a finite, strictly positive table; `dsp_systems_tests` links |
| **T2** | **Run the probe alone** (`node tools/run-cpu-tests.js dsp_systems_tests`) and record the table. Decide D-1 (`SVF` vs `MultimodeFilter`) and OQ-1's realisation (direct `Biquad` vs single-slot `ResonatorBank`) **from the numbers** | the table is in the transcript; both decisions cite a measured figure |
| **T3** | Write the header skeleton: S1.1–S1.6 (includes, constants with their derivation tables, `FilterMode`, `PrepareConfig`, `Loop`, salt table, the full S1.4 API, the probe forward-declaration and friend). Bodies may be stubs | `dsp_systems_tests` compiles; `node tools/lint-layers.js` and `lint-odr.js` pass |
| **T4** | `applyDefaults()`, the constructor (including S1.5's construction-seeded `maxCutoffHz_`/`maxResonanceHz_`), `prepare()` (S2's 15 steps), `reset()`, `setSeed()`, `clearLoopAudio()` (O(1), S3.1), `getAllocatedBytes()` | SC-008; SC-017's unprepared-state arm run under ASan **and** an MSVC debug build, since the seeded-bounds defect it guards is an `_STL_VERIFY` trap rather than a wrong value; SC-007's `prepare`-outside-scope shape |
| **T5** | `updateResonator()` + the truthful RT60 getter (S4) | **SC-023** — the MEASURED ring, not the stored `appliedResonanceQ`: loop 5's realised RT60 within ±10 % of 1.000 s (floor: > 0.5 s, unreachable at `kMaxQ = 30`'s 0.314 s), loop 0's within ±10 % of 0.1833 s, and `getLoopResonanceRt60(i)` agreeing with both. The S1.2 table reproduces to 1e-3 s |
| **T6** | The control step: `updateControl()`, `normaliseRows()` **with FR-075's count mask**, the two lane mappings, `setWanderRate`, the crossfade counter, the governor **including S5.6's follower finiteness guard** (S5) | SC-015 (against `getLoopAppliedCoupling`, not the tautological total), SC-006, SC-005, **SC-025** |
| **T7** | `renderChunk()` + `processBlockTapped` + `processBlock` (S6), including FR-003's guard ladder, the read-mute branch, the FR-047 trap and the FR-046 clamp | SC-010, SC-016, SC-018, SC-013, **SC-024** |
| **T8** | The life cycle: `gateSteady`, `refreshGates`, the sleep and wake edges, `setNumLoops` (S7) | SC-014, SC-022 |
| **T9** | The whole FR-070/FR-071 read surface + FR-009's argument contract on every setter (S8.6, S9) | SC-017 |
| **T10** | `tests/test_helpers/coherence.h` (S12.1) | a unit sanity check: coherence of a signal with itself is 1, with independent noise ≈ 0 |
| **T11** | `feedback_ecology_test.cpp` — the seventeen main-suite criteria (S12.3), SC-023/SC-024/SC-025 included | all green |
| **T12** | `feedback_ecology_nonfinite_test.cpp` — SC-012, including the probe definition | green under `-fno-fast-math`; probe compiles and links only in this TU |
| **T13** | `feedback_ecology_spectral_test.cpp` — SC-001, SC-002 (probe-then-pin for `kInteractionMarginDb`), SC-003, SC-021, all `[long]` | green; O-1 pinned from a recorded measurement |
| **T14** | SC-004's gated arms (a)–(d) **and (f)'s two transition blocks at 48 and 192 kHz** + the `static_assert`ed baselines, from a fresh isolated perf run. Apply OQ-2 levers 1–3 if needed; **stop and surface** if still over | `kBaseline <= 71 111 ns`; both (f) blocks inside the absolute per-block ceiling at both rates, with the measured figures written into S14.2; or a surfaced table |
| **T15** | Gates: the six lints + `check-portability`, SC-020's `git diff --stat` and the three-suite regression, clang-tidy | all clean |

---

## Review notes on this revision

1. **Every `SmoothedBiquad` decision from the previous revision is gone.** OQ-1 makes the loop resonator
   a plain `Biquad` fed directly-computed RBJ coefficients (S4.1), retuned by a stepped hard swap, so
   the `kMaxResonatorQ = 100` ceiling FR-013 specifies is actually reached. The consequences propagate
   into FR-076's table (S11), `clearLoopAudio` (no coefficient smoother to snap, S3.1), FR-041's
   contraction argument (no interpolated intermediate, S8.1) and SC-001 (d)'s named click exemption
   (S12.3).
2. **The Q1 default resonator table is worked out per centre, not asserted.** S1.2's second table shows
   `kDefaultResonanceRt60 = 1.0 s` is reachable only at 210 Hz and is clamped to 0.183 s at 1200 Hz,
   which is why `getLoopResonanceRt60` must derive from the applied `Q` (S4.2) rather than echo the
   request.
3. **The Q6 `normGain` scaling of the governor's tracker is in the per-sample path, using the ramp value
   already computed for the output stage** (S6 step 4) — one multiply, no second ramp. SC-006 (f) is the
   arm that discriminates.
4. **One implementer trap is promoted to its own section (S5.0)** because no criterion in the spec
   measures it: the 42 control-rate `OnePoleSmoother`s must be configured at `fs / 64`, not `fs`.
5. **Two spec statements are corrected against the code** (S16 C-1, C-2) rather than reproduced. Neither
   changes an assertion; C-2 makes SC-012 (b)'s bound slack instead of tight and records why the dry
   sanitiser must not be "fixed" away.
6. **The CPU section carries a first-order estimate that predicts a miss** (S14.3): six `std::tanh` per
   sample against a 139 ns/sample whole-component budget. Lever 1 is pre-authorised and preserves rung 2
   exactly; the ladder above that stops and surfaces rather than moving the line.

### Review notes — the 2026-09-12 plan review, issue by issue

Nineteen issues were raised against the previous revision. **All nineteen are accepted; none is
rejected, and no threshold is relaxed to dodge one.** Where a criterion moved, it moved onto a fixture
where the argument it tests is the operative one, and the threshold itself is unchanged.

1. **`normaliseRows()` deleted a dropped loop's coupling in one control step** (blocker). FR-076
   declares `setNumLoops` smoothed at `kGainRampMs`; FR-075 paces the drop by the gate. The exclusion
   is now a **count mask** — one control-grid `OnePoleSmoother` per source slot at `kGainRampMs`,
   multiplying that slot's outgoing coupling in both the coefficients and the row sum they are
   normalised against (S1.5, S5.3, S7.5). Deferring to the sleep edge alone was rejected as a fix: the
   *renormalisation* would then step instead, because a smaller row sum means a larger `scale`. Keying
   the exclusion on `engineActive` was rejected too — it breaks SC-015 (d)'s dormancy-inertness arm.
   SC-022 (c) now runs at `kMaxCouplingPerPair` as well as on the default ring.
2. **SC-002 (a)'s exact zero was unreachable on a correct build** (blocker). Fixed by the fixture, not
   by the assertion: `configure → reset() → render` snaps every ramp and smoother and clears the audio,
   so `== 0.0f` holds from sample 0 with no warm-up (S3.2, S12, S12.3 SC-002; S16 C-5). The arm gains
   an anti-vacuity check that the snap actually happened.
3. **FR-013's `Q = 100` ceiling had no criterion** (blocker) — the headline correction of the OQ-1
   revision was untested, and `getLoopResonanceRt60` structurally cannot detect the defect because it
   derives from a field written before the coefficients. **SC-023** measures the realised ring
   (S12.3, S4.2, S16 C-6), with a never-weakenable floor of "loop 5 rings longer than 0.5 s", which
   `kMaxQ = 30`'s 0.314 s cannot reach.
4. **Rung 5 could not fire for the `EnvelopeFollower` failure it was written for** (blocker). Verified
   chain: `processRMS` never self-heals; the S5.6 law yields NaN; `LinearRamp::setTarget` converts a
   NaN target into an unramped step to **zero** rather than propagating it; every `b_i` then stays
   finite and the per-sample trap is unreachable — a permanent silent mute with both counters clean.
   The guard moved to the source (S5.6), S8.5 now says the follower is protected by its own
   control-step guard, and SC-012 (c)'s second arm was rewritten against that guard with an explicit
   anti-mute clause (R-16).
5. **`clearLoopAudio` put a 131 KB–3.1 MB `std::fill` on the audio thread** (major). Replaced by an
   O(1) read-mute window of exactly one delay length (S3.1, S6, S7.3), with the delay position frozen
   while it runs (S5.2 step 4d) so the window is exact. `prepare()`/`reset()` keep the real wipe.
   SC-004 gains arm (f), which times the six-loop sleep-edge block and the trap block at 48 and 192 kHz
   against the absolute per-block ceiling (R-17, S16 C-8).
6. **`getLoopResonanceRt60` clamped against a `0.0f` bound on an unprepared instance** (major, raised
   twice) — inverted `std::clamp` bounds, UB, and MSVC's `_STL_VERIFY` traps it on a path SC-017
   guarantees to exercise. Both cached bounds are now seeded at construction from
   `kConstructionSampleRate` (S1.5, S1.2's new `static_assert`s, S2 step 3), and S9 states the audit
   rule for any future getter that clamps against a rate-derived bound.
7. **SC-015 was a tautology** (major): `getLoopAppliedTotalGain` is `std::min(g, kMaxTotalLoopGain)` by
   construction, so a build that dropped `* scale` from the coefficient writes passed unchanged.
   `getLoopAppliedCoupling(from, to)` was added to FR-071 and SC-015 (a) now asserts on the
   reconstructed coefficients in force (S1.4, S9, S12.3, S16 C-7).
8. **FR-003 had no test** (major) — null pointers, `numSamples == 0`, unprepared render and both legal
   aliasings. **SC-024** (S12.3, S16 C-6).
9. **FR-055's decimation had no discriminating test** (major) — every wander arm ran where a
   hard-wired decimation of 1 is indistinguishable. **SC-025**, with the mapping table in S11, a
   behavioural arm at `kMinWanderRateHz` and a rebase arm (S12.3, S16 C-6).
10. **Rung 1 dropped the `sqrt(2)` its own table supplied, and the margin note was wrong arithmetic**
    (major). S8.1 now carries the crossfade factor through (`1.34438 > 1`), states the bound as a
    per-crossfade transient with the phase-averaged log-gain, and gives the corrected per-circulation
    figures (−0.4399 dB per circulation; 5.6 s and 61.2 s to 60 dB on the shortest and longest loops).
    SC-001 (a) splits into **(a1)** the full-wander corner with the four bounded assertions plus
    non-divergence and **(a2)** the same corner with wander disabled at the input cut-off carrying the
    **unchanged** ≥ 60 dB contraction assertion.
11. **SC-001 (d)'s exemption list said "five" for a seven-member stepped class** and left
    `setWanderRate` in neither class (minor). Both classes are now enumerated, and `setWanderRate` is
    classified explicitly as stepped-but-click-free and **held to** the assertion (S11, S12.3).
12. **SC-005 (a)'s sampling rule contradicted itself** (minor). One series — once per 512-sample block
    — and a reading counts only when `getLoopCrossfadeCount(i)` is unchanged either side of it, so
    "distinct values" means completed steps and not in-flight tap averages (S12.3).
13. **SC-012 (c)'s bit-identity claim ignored `follower_.reset()`'s global effect** (minor). Scoped to
    the samples before the next control step, with convergence asserted thereafter, and repeated at
    `ratio = 1` where the reset is unobservable (S12.3).
14. **SC-019 was silently demoted from a TEST_CASE** (minor). Recorded as S16 C-4, with the spec edit
    named.
15. **S2 step 8 implied a `prepared_` setter gate no FR states** (minor). The gate is dropped, the rule
    is stated once in FR-009's contract (S8.6) and once in S9, and SC-017 asserts it. `SVF::setCutoff`
    was verified safe on an unprepared instance (`svf.h:690`, `:716`).
16. **`primitives/delay_line.h` was missing from FR-001's exhaustive include list** and
    `core/audio_constants.h` had no user (minor). Both fixed (S1.1, S16 C-9).
17. **Two S0 citations were wrong** (minor, two issues): `std::array<SVF, N>` is at
    `filter_feedback_matrix.h:287/296/305/558`, not `:174` (which is `setFeedbackAmount`'s doc
    comment); and eight `OnePoleSmoother` citations were anchored one line early. Both re-read and
    corrected, and the S0 ledger gained six rows for the facts this revision now depends on.
