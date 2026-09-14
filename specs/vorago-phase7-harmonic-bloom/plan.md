# Implementation Plan: Vorago Phase 7 — Harmonic Bloom

**Spec:** `specs/vorago-phase7-harmonic-bloom/spec.md` (1416 lines, read in full this session, **after**
the review notes and the 2026-09-14 Clarifications Q1–Q8).
**Roadmap:** `specs/Vorago-roadmap.md` Part A → Phase 7 (lines 335–354); reuse row L7 (line 115); the
ODR note (127–129); the cross-cutting constraints (518–542) including the Dormancy rule (527–535) and
the shared-component rule (540–542); the dependency graph (498–511).
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/bloom_engine.h` (header-only,
no `.cpp`), four new test TUs, **two** edits in `dsp/tests/CMakeLists.txt`, **one** edit in
`dsp/lint_all_headers.cpp`. No new test helper (spec A-4 holds; verified in S10.1). **No existing
header is modified** (FR-080).
**Test target:** `dsp_systems_tests` (all four new TUs). SC-012 regression targets: `dsp_core_tests`,
`dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11.

**The phase in one sentence:** a fixed 16-entry lifecycle table of *child partials* — each one a
latched `(slot, ratio, amplitude)` triple carried on an integer control-step clock through a
smoothstep fade-in / hold / fade-out — written into reserved indices of the ratio/amplitude arrays a
Vorago voice is about to hand `HarmonicCloud::setSpectralTarget`, with **no audio path, no new DSP
mathematics, and no oscillator anywhere**.

---

## S0. Verification ledger — every signature below was read this session

Nothing in this plan is quoted from the spec without re-opening the header. Where the spec and the
code disagree, **the code wins**, and the disagreement is recorded in **S14 (Spec corrections)**.

| Claim this plan is built on | Verified at |
|---|---|
| `HarmonicCloud::kMaxPartials = 64` | `systems/harmonic_cloud.h:138` |
| `HarmonicCloud::kControlChunkSamples = 64` | `harmonic_cloud.h:144` |
| `HarmonicCloud::kTargetAmpEpsilon = 1e-5f`, `kTargetRatioEpsilonCents = 0.05f`, `kTargetRatioRelEpsilon` (≈2.887e-5) | `harmonic_cloud.h:255-258` |
| `kMinFundamentalHz = 20.0f`, `kMaxFundamentalHz = 4000.0f`, `kMinTiltDbPerOct = -12.0f`, `kMaxTiltDbPerOct = 12.0f`, `kAmpSmoothTimeSec = 0.002f`, `kMaxPartials` static_assert on the log2 table | `harmonic_cloud.h:184-195`, `:165`, `:140-141` |
| `void setSpectralTarget(const float* ratios, const float* amplitudes, std::size_t count) noexcept` | `harmonic_cloud.h:769` |
| Guard ladder: `ratios == nullptr \|\| amplitudes == nullptr \|\| count == 0 \|\| count > kMaxPartials` → return | `harmonic_cloud.h:770-773` |
| Whole-array bit-identical skip, `memcmp` on both arrays, gated on `hasTarget_ && count == targetCount_` | `harmonic_cloud.h:803-810` |
| Wholesale rejection loop: any `isNaN`/`isInf` on either array, any `ratios[i] <= 0.0f`, any `amplitudes[i] < 0.0f` → `return` with **nothing written** | `harmonic_cloud.h:812-818` |
| Padding law above `count`: `r = (i < count) ? ratios[i] : float(i + 1)`, `a = (i < count) ? amplitudes[i] : 0.0f` | `harmonic_cloud.h:821-823` |
| The amplitude dirty mask compares against **`committedAmp_[i]`**, not against the previously supplied `targetAmp_[i]` | `harmonic_cloud.h:841` |
| `committedAmp_[i] = targetAmp_[i]` is executed **only inside** the dirty-gated recompute branch | `harmonic_cloud.h:1481` |
| The header names the opposite design as the broken one ("sub-epsilon per-chunk motion accumulates FOREVER and never trips the threshold", deviation D14) | `harmonic_cloud.h:827-838` |
| `activeCount_ = clamp(int(round(pow(64, richness))), 1, 64)`; every `i >= activeCount_` gets `baseAmplitude_[i] = 0.0f; continue;` **before** the spectral-target branch | `harmonic_cloud.h:1462-1463`, `:1469-1473` |
| `baseAmplitude_[i] = hasTarget_ ? targetAmp_[i] * tiltGain(i) : …` | `harmonic_cloud.h:1492-1494` |
| `tiltGain(index)`: `if (tiltDb_ == 0.0f \|\| index == 0) return 1.0f;` then `std::exp2(tiltDb_ * detail::kHarmonicCloudLog2N[index] * kLog2TenOver20)` with `constexpr float kLog2TenOver20 = 3.32192809488736235f / 20.0f;` | `harmonic_cloud.h:1429-1435` |
| `detail::kHarmonicCloudLog2N` is an `inline const std::array<float,64>` filled with `std::log2(static_cast<float>(i + 1))` | `harmonic_cloud.h:57-63` |
| Read surface: `getActivePartialCount()` `:950`, `getPartialFrequencyHz(i)` `:955`, `getPartialCurrentAmplitude(i)` `:959`, `getPartialTargetAmplitude(i)` `:963`, `getPartialUnmutatedTargetAmplitude(i)` `:969`, `isQuiescent()` `:1040`, `stateFinite()` `:1054`, `hasSpectralTarget()` `:868`, `clearSpectralTarget()` `:862` | `harmonic_cloud.h` |
| `EntropyProcessor::processChunk(float* ratios, float* amplitudes, std::size_t count, std::size_t numSamples) noexcept`; rejection guard `ratios == nullptr \|\| amplitudes == nullptr \|\| count == 0` **before** any advance; `numSamples > 0` gates the advance; "internal lane state after N advanced samples is a function of N alone, never of how N was partitioned into chunks" | `processors/entropy_processor.h:269`, `:271-277`, `:255-259` |
| `EntropyProcessor::kMinRatioSpacingCents = 24.0f`, `kMinRatioSpacingLog2 = kMinRatioSpacingCents / 1200.0f`, `kMinRatioSpacingFactor = detail::constexprExp(kMinRatioSpacingLog2 * detail::kLn2)` | `entropy_processor.h:79-84` |
| `EntropyProcessor::kPartials = SpectralState::kStatePartials` (64) | `entropy_processor.h:62` |
| `SpectralState::kStatePartials = 64`, `kMinStateRatio = 0.5f`, `kMaxStateRatio = 128.0f`, `kMinStateTiltDbPerOct = -12.0f`, `kMaxStateTiltDbPerOct = 12.0f` | `processors/spectral_state.h:48`, `:51-54` |
| `MultiStageEnvelope::kMaxStageTimeMs = 10000.0f` (10 s per stage) | `processors/multi_stage_envelope.h:65` |
| `GrowthEnvelope::kMinDuration = 1.0f`, `kMaxDuration = 60.0f` seconds | `processors/growth_envelope.h:95-98` |
| `Xorshift32(uint32_t = 1)` `:45`, `next()` `:49`, `nextFloat()` bipolar `:59`, `nextUnipolar()` ∈ **(0, 1]** `:67` (`next()` returns `[1, 2^32-1]`, `kToFloat = 1/(2^32-1)`), `seed(uint32_t)` substitutes its own default for 0 `:72-74` | `core/random.h` |
| `deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` — lowbias32 finaliser over `base ^ ((salt+1)*0x9E3779B9)`, guaranteed non-zero | `core/random.h:102-113` |
| `detail::isNaN(float)` `:99`, `detail::isFinite(float)` `:118`, `detail::isFinite(double)` `:125`, `detail::flushDenormal(float)` `:245`, `detail::isInf(float)` `:260` — all bit-pattern, `-ffast-math`-proof through `opaqueFloatBits` | `core/db_utils.h` |
| `centsToPitchRatioFast(float cents)` — degree-4 Horner of `e^(cents·ln2/1200)`, documented accurate on `[-50, +50]` cents, **exactly 1.0f at 0** | `core/pitch_utils.h:63-68` |
| `LinearRamp::configure(float rampTimeMs, float sampleRate)` `:329`, `setTarget(float)` `:342` (a NaN target mutes to 0 with no way back, `:343-348`), `getCurrentValue()` `:364`, `process()` `:370` (lands exactly on the target), `isComplete()` `:409`, `snapTo(float)` `:421` | `primitives/smoother.h` |
| House shape: `kControlChunkSamples = 64` + live `static_assert` `:135-136`, `kGainRampMs = 50.0f` `:143`, `kOutputClamp = 4.0f` `:144`, `kWakeSilenceEpsilon = 1.0e-6f` `:266`, `kMinUsableSampleRate = 8000.0` + ordering `static_assert` `:281-296`, nested `PrepareConfig` with the designated-initialiser rationale `:297-306`, `setPeakWake` clamp `[0,1]` `:771-780`, `setPeakDormant` `:784-793`, `getClampEngagementCount()` `:904`, `getAllocatedBytes()` returning 0 `:906-912`, salt table + overlap `static_assert`s `:929-947` | `systems/resonance_drift_network.h` |
| House shape: `kControlChunkSamples = 64` + `static_assert` `:150,:159-161`, `kGainRampMs = 50.0f` `:178`, `kOutputClamp = 4.0f` `:180`, `PrepareConfig` `:190-195`, nested **public** `DustGrain` `:203-208` (`public:` opens at `:137`, the next `private:` not until `:1003`), `setSourceDormant` `:833`, `setSourceWake` with "Vorago Phase 10 owns the SlowEventScheduler" `:841-850`, `getSourceWakeAmount` `:880`, out-of-range read-surface neutrals documented `:856-861`, `getAllocatedBytes()` `:999` | `systems/noise_organism.h` |
| House shape: `kControlChunkSamples = 64` + `static_assert` `:168-169`, `kMinUsableSampleRate = 8000.0` `:175`, `kGainRampMs = 50.0f` `:177`, nested `enum class Tone : std::uint8_t` `:320`, nested `PrepareConfig` `:327`, `prepare(double, const PrepareConfig&) noexcept` `:380`, `reset() noexcept` `:487`, the **absolute-residue** control-chunk loop carried across calls `:592-607`, every float setter rejecting a non-finite argument `:614-618` | `systems/subharmonic_engine.h` |
| `SlowEventScheduler::Phase` — `enum class Phase : std::uint8_t { Idle = 0, Attack = 1, Hold = 2, Release = 3 }`, public and **nested** — is the **sole exact-shape precedent**. Two further nested `Phase`-family enums coexist with it and are cited for **coexistence only, not for shape**: `GrowthEnvelope::Phase` is `enum class Phase { Idle, Rising, Complete }` — three enumerators, **no** explicit underlying type, and **private** (`private:` opens at `:205`) — and `EntropyProcessor::LifePhase` is `enum class LifePhase : std::uint8_t { Alive = 0, Dying, Dead, Reborn }` | `slow_event_scheduler.h:180`, `growth_envelope.h:207`, `entropy_processor.h:166` |
| `SlowEventScheduler::isEventActive()` — the level-like poll FR-043 is written against | `slow_event_scheduler.h:361` |
| `render_fingerprint.h`: `kRenderCheckpoints = 32` `:54`, `kSampleTolerance = 5.0e-4f` `:58`, `kMetricTolerance = 2.5e-4` `:61`, `struct RenderFingerprint` `:63`, `fingerprintRender(std::span<const float>)` `:73`, `compareFingerprints(actual, ref, metricTol = …, sampleTol = …)` `:122`, namespace `Krate::DSP::TestUtils` | `tests/test_helpers/render_fingerprint.h` |
| `artifact_detection.h`: `ClickDetectorConfig` `:38` with six fields in declaration order `sampleRate`(default **44100**, only range-checked by `isValid()`), `frameSize`, `hopSize`, `detectionThreshold`, `energyThresholdDb`, `mergeGap`; `ClickDetection` `:72`; `detect(const float*, size_t)` `:130` | `tests/test_helpers/artifact_detection.h` |
| `allocation_detector.h`: `AllocationDetector` `:48`, `AllocationScope` `:111` with `getAllocationCount()` / `hadAllocations()` | `tests/test_helpers/allocation_detector.h` |
| `audio_features.h`: `struct AudioFeatures` with `centroidHz` `:27`, computed at `:88` (magnitude-weighted mean frequency); namespace `Krate::Test` | `tests/test_helpers/audio_features.h` |
| `statistical_utils.h`: `computeMean(const float*, size_t)` `:41`, `computeVariance` `:59`, `computeStdDev` `:76`, `computeMedian(float*, size_t)` (**sorts in place**) `:90` | `tests/test_helpers/statistical_utils.h` |
| The pinned click fixture idiom — `countClicks(buffer, sigma)` with all six `ClickDetectorConfig` fields designated-initialised, and `smallestZeroSigma(buffer)` walking sigma 5.0 → 30.0 in 0.5 steps for the failure message | `dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp:399-423` |
| Perf idiom: ns per 512-sample block at 48 kHz, one block period = 10 666 667 ns, best-of-25 × 500 blocks after 400 warm-up, `[.perf]`, `static_assert`ed baselines, **RUN IT ALONE**, and the **stop-and-surface rule** verbatim | `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:57-88` |
| `dsp_systems_tests` enumerated source list opens `:324`, the Phase-6 TUs sit `:460-463`, the list closes `:464`; the `-fno-fast-math` block opens `:556`, its Phase-6 entry `:899`, its `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` line `:900` | `dsp/tests/CMakeLists.txt` |
| `dsp/lint_all_headers.cpp`: Phase-2 include `:179`, Phase-3 `:182`, Phase-5 `:185`, Phase-6 `:188`, the Layer-4 block opens `:190` | `dsp/lint_all_headers.cpp` |
| `tests/test_helpers/` is the helper root (NOT `dsp/tests/test_helpers/`), wired as an INTERFACE library — a new helper header would need no CMake edit; this phase adds none | `tests/test_helpers/`, its `CMakeLists.txt` |
| Lints that exist and must pass | `tools/lint-odr.js`, `lint-layers.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `lint-simd-aligned-loadstore.js`, `check-portability.js`, `run-cpu-tests.js` |

**ODR sweep, re-run this session from the repo root:**

```
$ grep -rn "class BloomEngine\|struct BloomEngine\|struct BloomChild\|class BloomChild" dsp/ plugins/ tools/
   -> 0 hits (exit 1)
$ grep -rn "BloomEngine\|bloom_engine" dsp/ plugins/ tools/
   -> 0 hits
$ ls dsp/include/krate/dsp/systems/bloom_engine.h
   -> No such file or directory
```

The three near-name families the spec records (`AetherReverb`'s bloom resonators,
`SpectralMorphEngine`'s morph-stagger `bloom`, `SeraphisEngine::BloomEvents` /
`kBloomPartialCap`) are all **class-scoped** and survive unchanged. `BloomEngine::Phase` is a nested
enum, and nested `Phase` enums already coexist (`SlowEventScheduler::Phase`
`slow_event_scheduler.h:180` and `GrowthEnvelope::Phase` `growth_envelope.h:207`, whose enumerator
lists differ from each other and from this one — see the S0 row) — `lint-odr.js`
qualifies nested types by their enclosing class (`tools/lint-odr.js:18-21`), so there is no
collision and no namespace-scope name is claimed.

---

## S1. Component: `BloomEngine`

### S1.1 Header, layer, includes

`dsp/include/krate/dsp/systems/bloom_engine.h`, `namespace Krate::DSP`, header-only, **Layer 3**.

```cpp
#pragma once

// Layer 3 (systems/). Dependencies: Layer 0/1 + stdlib only — deliberately NOT
// Layer 2 or 3. This component is decoupled from HarmonicCloud by the array
// contract (spec D-1), so systems/harmonic_cloud.h is NOT included; the two
// facts it needs from that header (the slot-indexed tilt law and the ratio
// bounds) are RESTATED as class-scoped constants with a source comment
// (spec D-2, D-9).
#include <krate/dsp/core/db_utils.h>        // detail::isNaN/isInf/isFinite/flushDenormal
#include <krate/dsp/core/pitch_utils.h>     // centsToPitchRatioFast
#include <krate/dsp/core/random.h>          // Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/smoother.h>  // LinearRamp (FR-042 depth ramp only)

#include <algorithm>   // std::clamp, std::max, std::min
#include <array>
#include <cmath>       // std::log2, std::exp2, std::round
#include <cstddef>
#include <cstdint>
```

`lint-layers.js` reads `#include <krate/dsp/{layer}/…>` and fails a lower layer reaching upward
(`tools/lint-layers.js:8-20`); every include above is Layer 0 or 1, so a Layer-3 file is trivially
clean. **No `<vector>`, no `<memory>`, no `<functional>`, no `<random>`, no I/O, no exceptions.**

**Banner requirements** (matching `subharmonic_engine.h` and `resonance_drift_network.h`): a
`@par Layer: 3 (systems/). Dependencies: Layer 0/1 + stdlib only.` line and a
`@par Real-Time Safety:` line stating that **every** method including `prepare()` is `noexcept` and
allocation-free (this component has no heap term at all — S9).

### S1.2 Constants — all `static constexpr`, class scope, `kPascalCase` (FR-003)

```cpp
// ---- structure ----------------------------------------------------------
/// The cloud's hard slot ceiling. NOT included from harmonic_cloud.h (D-1/D-2):
/// restated, with the static_assert below standing in for the include.
static constexpr std::size_t kMaxSlots    = 64;
static_assert(kMaxSlots == 64, "== HarmonicCloud::kMaxPartials (harmonic_cloud.h:138)");
static_assert(kMaxSlots <= 255, "Child::slot is a std::uint8_t (S1.3)");

/// FR-034 lifecycle table size. Fixed at compile time; no free list, no growth.
static constexpr std::size_t kMaxChildren        = 16;
static constexpr std::size_t kMaxParents         = 8;      // FR-010 K ceiling
static constexpr std::size_t kDefaultParentCount = 4;      // FR-010
static constexpr std::size_t kMaxChildrenPerEvent     = 4; // FR-015
static constexpr std::size_t kDefaultChildrenPerEvent = 2; // FR-015
static constexpr std::size_t kMaxSpawnAttempts   = 4;      // FR-026 (first draw + 3 retries)
static constexpr std::size_t kDefaultChildSlots  = 8;      // PrepareConfig::numChildSlots

/// The shared library-wide control clock (harmonic_cloud.h:144,
/// noise_organism.h:150, resonance_drift_network.h:135, subharmonic_engine.h:168).
static constexpr std::size_t kControlChunkSamples = 64;    // FR-007
static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

/// prepare()'s sample-rate floor (the resonance_drift_network.h:281 figure).
/// Nothing here inverts a clamp at a low rate, but the floor also bounds the
/// step-count conversions in S5.1: at 8 kHz a 600 s fade is 75 000 control
/// steps, and the 1-step floor below is what keeps a 1 s fade non-degenerate.
static constexpr double kMinUsableSampleRate = 8000.0;

// ---- ratio domain -------------------------------------------------------
/// Restated from SpectralState::kMinStateRatio / kMaxStateRatio
/// (spectral_state.h:51-52) rather than included (D-2). Used ONLY as the
/// comparison bounds of FR-021's rejection test — never as a clamp.
static constexpr float kMinChildRatio = 0.5f;
static constexpr float kMaxChildRatio = 128.0f;

/// Restated from EntropyProcessor::kMinRatioSpacingCents (entropy_processor.h:80).
static constexpr float kMinRatioSpacingCents = 24.0f;
static constexpr float kMinRatioSpacingLog2  = kMinRatioSpacingCents / 1200.0f;  // 0.02

static constexpr float kMinDetuneCents = kMinRatioSpacingCents;  // 24 — FR-020
static constexpr float kMaxDetuneCents = 50.0f;                  // centsToPitchRatioFast domain
static_assert(kMaxDetuneCents <= 50.0f,
              "wider than centsToPitchRatioFast's documented accurate domain "
              "(pitch_utils.h:55-62) — use std::exp2 instead of widening this");
static_assert(kMinDetuneCents < kMaxDetuneCents, "empty detune band");

// ---- relations ----------------------------------------------------------
static constexpr float kOctaveFactor = 2.0f;
static constexpr float kFifthFactor  = 1.5f;

// ---- amplitude ----------------------------------------------------------
/// FR-011. Matches HarmonicCloud::kTargetAmpEpsilon (harmonic_cloud.h:258):
/// a partial the cloud cannot tell from silence has no harmonics to grow.
static constexpr float kSilentParentAmplitude = 1.0e-5f;
static constexpr float kDefaultChildGain      = 0.35f;   // FR-023, configurable [0, 1]

/// FR-023 / Clarification Q1. Restated verbatim from harmonic_cloud.h:1433.
static constexpr float kLog2TenOver20 = 3.32192809488736235f / 20.0f;
/// Restated from HarmonicCloud::kMinTiltDbPerOct / kMaxTiltDbPerOct (:194-195).
static constexpr float kMinConsumerTiltDbPerOct = -12.0f;
static constexpr float kMaxConsumerTiltDbPerOct =  12.0f;

// ---- lifecycle (FR-030, roadmap line 347) --------------------------------
static constexpr float kDefaultFadeInSeconds  =  45.0f;
static constexpr float kDefaultHoldSeconds    = 120.0f;
static constexpr float kDefaultFadeOutSeconds = 180.0f;
static constexpr float kMinFadeInSeconds  =   1.0f, kMaxFadeInSeconds  = 300.0f;
static constexpr float kMinHoldSeconds    =   0.0f, kMaxHoldSeconds    = 900.0f;
static constexpr float kMinFadeOutSeconds =   1.0f, kMaxFadeOutSeconds = 600.0f;
static constexpr float kDefaultHoldJitterFraction = 0.5f;   // FR-036, Q5

// ---- triggering ---------------------------------------------------------
static constexpr float kMaxSpawnRateHz     = 0.05f;          // FR-040: one per 20 s
static constexpr float kDefaultSpawnRateHz = 1.0f / 240.0f;  // one per 4 minutes
static constexpr float kDefaultDepth       = 1.0f;           // FR-042

/// The house 50 ms control ramp (noise_organism.h:178, resonance_drift_network.h:143,
/// subharmonic_engine.h:177). Used for `depth` only (FR-042) — there is no gain here.
static constexpr float kGainRampMs = 50.0f;

/// resonance_drift_network.h:266. A wake at or below this snaps to EXACTLY 0,
/// which is what makes setWake(0) and setDormant(true) identical BY CONSTRUCTION
/// rather than by luck (FR-035, SC-014 (d)).
static constexpr float kWakeSilenceEpsilon = 1.0e-6f;

static constexpr std::uint32_t kDefaultSeed = 0xB10035EDu;  // "bloomseed"

// ---- diagnostics --------------------------------------------------------
/// FR-052's engagement counter SATURATES here rather than wrapping - the
/// ResonanceDriftNetwork::kMaxClampCount idiom (resonance_drift_network.h:985,
/// applied at :1871-1872). A bare ++ on a std::uint32_t WRAPS; SC-003 asserts
/// this counter reads EXACTLY 0 when parentCount <= reserveBase(), and a wrap
/// would let a long-running engaged render - the one case where the number
/// actually matters - report a FALSE ZERO.
static constexpr std::uint32_t kMaxOverlapCount = 0xFFFFFFFFu;
```

**Every one of the restated constants above carries a source comment naming the header and line it
was copied from.** SC-012's `static_assert`s (S10.4) are the live cross-check that the copies have
not drifted — they name `HarmonicCloud::kMaxPartials`, `HarmonicCloud::kControlChunkSamples`,
`HarmonicCloud::kTargetAmpEpsilon` and `EntropyProcessor::kMinRatioSpacingCents` **from the test
TU**, which is where the two headers may legally meet.

### S1.3 Nested types

```cpp
/// FR-020. APPEND ONLY — this becomes a persisted plugin parameter at Phase 12.
/// Nested, following SubharmonicEngine::Tone (subharmonic_engine.h:320) and
/// ResonanceDriftNetwork::AnchorMode (:294).
enum class Relation : std::uint8_t { Octave = 0, Fifth = 1, DetunedNeighbour = 2 };
static constexpr std::size_t kNumRelations = 3;

/// FR-030. Nested. The exact-shape precedent is SlowEventScheduler::Phase
/// (slow_event_scheduler.h:180) — `enum class Phase : std::uint8_t { Idle = 0,
/// Attack = 1, Hold = 2, Release = 3 }`, public and nested. GrowthEnvelope::Phase
/// (growth_envelope.h:207) is a DIFFERENT shape — `{ Idle, Rising, Complete }`,
/// private, no explicit underlying type — and is cited only as evidence that
/// nested `Phase` enums coexist, never as a shape to copy.
enum class Phase : std::uint8_t { Idle = 0, FadeIn = 1, Hold = 2, FadeOut = 3 };

/// FR-050. Callers MUST use designated initialisers — PrepareConfig{.capacity = 32}
/// — so no narrowing conversion hides in a positional brace init (Clang errors
/// where MSVC does not). Nested, following NoiseOrganism::PrepareConfig
/// (noise_organism.h:190), ResonanceDriftNetwork::PrepareConfig (:299) and
/// SubharmonicEngine::PrepareConfig (:327).
struct PrepareConfig {
    /// THE CALLER'S PROMISE ABOUT HOW MANY SLOTS THE CLOUD WILL ACTUALLY SOUND,
    /// i.e. HarmonicCloud::getActivePartialCount() (harmonic_cloud.h:950) — NOT
    /// kMaxPartials. recalculateAmplitudes() zeroes and `continue`s every slot
    /// at or above activeCount_ BEFORE the spectral-target branch
    /// (harmonic_cloud.h:1469-1473), so a child written past it is silently
    /// inaudible. Clamped [1, kMaxSlots]. Only the INITIAL value: setCapacity()
    /// owns it afterwards (FR-055) and this field is never re-read.
    /// THIS FIELD DOES NOT BOUND THE WRITE REGION and must not be used to
    /// size the arrays. processChunk() requires at least kMaxSlots (64)
    /// writable floats whatever this value is, because setCapacity() can
    /// raise the ceiling afterwards - see the processChunk precondition
    /// in S1.4 and S14 C-11.
    std::size_t capacity      = kMaxSlots;
    /// Clamped [0, min(kMaxChildren, capacity)]. 0 makes the component an exact
    /// pass-through (FR-054).
    std::size_t numChildSlots = kDefaultChildSlots;
};
```

`Child` is a **private nested struct** (FR-034), following `ResonanceDriftNetwork::Peak`
(`resonance_drift_network.h:995`, inside the `private:` section that opens at `:928`) — the one
verified in-tree precedent for the **access level**. `NoiseOrganism::DustGrain`
(`noise_organism.h:203-208`) is the precedent for a nested fixed-size per-slot record, but it is
**public** (`public:` at `:137`, the next `private:` not until `:1003`), so it is deliberately not
cited here for the access level:

```cpp
struct Child {
    std::uint32_t step         = 0;  ///< Control steps since spawn. EXACT — see S5.1.
    std::uint32_t fadeInSteps  = 1;  ///< Latched at spawn (FR-033).
    std::uint32_t holdSteps    = 0;  ///< Latched, INCLUDING the FR-036 jitter.
    std::uint32_t fadeOutSteps = 1;  ///< Latched at spawn.
    float ratio     = 1.0f;          ///< Latched at spawn (FR-024).
    float target    = 0.0f;          ///< Latched amplitude (FR-023), tilt-compensated.
    float amplitude = 0.0f;          ///< Current envelope output; exactly 0 at both ends.
    std::uint8_t slot       = 0;     ///< Owned slot index; kMaxSlots <= 255 (S1.2).
    std::uint8_t parentIndex = 0;    ///< Informational, for SC-005 (S8 addition A-4).
    Relation relation = Relation::Octave;
    Phase    phase    = Phase::Idle;
    bool     fallback = false;       ///< FR-026 detuned-fallback child.
};
```

32 bytes with natural padding; 16 of them is 512 bytes (S9).

### S1.4 Public API — the complete shape the implementer types

Every method is `noexcept`. Every float setter **rejects** a non-finite argument (FR-009 (a)); every
index setter **silently no-ops** out of range (FR-009 (b)); every out-of-range float is **clamped**
and the getter reports the clamp (FR-009 (c)).

```cpp
class BloomEngine {
public:
    // constants, Relation, Phase, PrepareConfig  ............. S1.2 / S1.3

    BloomEngine() noexcept = default;                       // all defaults are member initialisers
    BloomEngine(const BloomEngine&) = default;              // no pointers into self — copyable
    BloomEngine& operator=(const BloomEngine&) = default;
    BloomEngine(BloomEngine&&) noexcept = default;
    BloomEngine& operator=(BloomEngine&&) noexcept = default;

    // ---- lifecycle (FR-004) ------------------------------------------------
    void prepare(double sampleRate, const PrepareConfig& config) noexcept;
    void reset() noexcept;

    // ---- the one per-chunk entry point (FR-005) ---------------------------
    /// PRECONDITION - NORMATIVE, and deliberately NOT the EntropyProcessor
    /// contract: `ratios` and `amplitudes` MUST EACH address at least
    /// `kMaxSlots` (64) writable floats, regardless of `parentCount` and
    /// regardless of the current `capacity()`.
    ///
    /// Unlike EntropyProcessor::processChunk (entropy_processor.h:269), whose
    /// `count` is documented as "Number of valid entries, clamped to kPartials"
    /// (:266) and which touches only [0, min(count, kPartials)) (:296), THIS
    /// component WRITES ABOVE `parentCount` - the FR-051 gap pad and the whole
    /// owned region - and setCapacity() (FR-055) may RAISE that write ceiling
    /// after prepare(), from the control thread, through a call the caller
    /// cannot correlate with its buffer length. Sizing a buffer from
    /// PrepareConfig::capacity is therefore NOT sufficient: size it from
    /// kMaxSlots. `parentCount` is an ANALYSIS length, never a buffer length.
    [[nodiscard]] std::size_t processChunk(float* ratios, float* amplitudes,
                                           std::size_t parentCount,
                                           std::size_t numSamples) noexcept;

    // ---- control surface (FR-060) -----------------------------------------
    void setSeed(std::uint32_t seed) noexcept;
    void setDepth(float depth) noexcept;                      // [0, 1]
    void setSpawnRateHz(float hz) noexcept;                   // [0, kMaxSpawnRateHz]
    void setParentCount(std::size_t k) noexcept;              // [1, kMaxParents]
    void setChildrenPerEvent(std::size_t n) noexcept;         // [1, kMaxChildrenPerEvent]
    void setChildGain(float gain) noexcept;                   // [0, 1]
    void setFadeInSeconds(float seconds) noexcept;            // [1, 300]
    void setHoldSeconds(float seconds) noexcept;              // [0, 900]
    void setFadeOutSeconds(float seconds) noexcept;           // [1, 600]
    void setHoldJitterFraction(float fraction) noexcept;      // [0, 1]
    void setRelationWeight(Relation r, float weight) noexcept;// [0, 1]; all-zero => uniform
    void setConsumerTiltDb(float dbPerOct) noexcept;          // [-12, +12]
    void setCapacity(std::size_t capacity) noexcept;          // [1, kMaxSlots]  (FR-055)
    void setDormant(bool dormant) noexcept;                   // (FR-035)
    void setWake(float amount) noexcept;                      // [0, 1]; <= 1e-6 snaps to 0
    void triggerBloom() noexcept;                             // edge-like (FR-040 a, FR-043)

    // ---- read surface (FR-061) --------------------------------------------
    [[nodiscard]] std::uint32_t getSeed()           const noexcept;
    [[nodiscard]] float  getDepth()                 const noexcept;  // the CONFIGURED value
    [[nodiscard]] float  getSmoothedDepth()         const noexcept;  // S8 addition A-6
    [[nodiscard]] float  getSpawnRateHz()           const noexcept;
    [[nodiscard]] std::size_t getParentCount()      const noexcept;
    [[nodiscard]] std::size_t getChildrenPerEvent() const noexcept;
    [[nodiscard]] float  getChildGain()             const noexcept;
    [[nodiscard]] float  getFadeInSeconds()         const noexcept;
    [[nodiscard]] float  getHoldSeconds()           const noexcept;
    [[nodiscard]] float  getFadeOutSeconds()        const noexcept;
    [[nodiscard]] float  getHoldJitterFraction()    const noexcept;
    [[nodiscard]] float  getRelationWeight(Relation r) const noexcept;
    [[nodiscard]] float  getConsumerTiltDb()        const noexcept;
    [[nodiscard]] bool   isDormant()                const noexcept;
    [[nodiscard]] float  getWakeAmount()            const noexcept;
    [[nodiscard]] std::size_t capacity()            const noexcept;
    [[nodiscard]] std::size_t numChildSlots()       const noexcept;  // min(requested, capacity)
    [[nodiscard]] std::size_t reserveBase()         const noexcept;  // capacity - numChildSlots
    [[nodiscard]] std::size_t getLiveChildCount()   const noexcept;
    [[nodiscard]] bool   isEngaged()                const noexcept;  // S8 addition A-5 (FR-051, Q8)
    [[nodiscard]] double getSampleRate()            const noexcept;

    // cumulative counters, all std::uint64_t, all zeroed by prepare()/reset()
    [[nodiscard]] std::uint64_t getSpawnEventCount()     const noexcept;  // executed events
    [[nodiscard]] std::uint64_t getDiscardedEventCount() const noexcept;  // S8 addition A-1 (Q7)
    [[nodiscard]] std::uint64_t getParentScanCount()     const noexcept;  // FR-013
    [[nodiscard]] std::uint64_t getOfferedChildCount()   const noexcept;  // S8 addition A-2
    [[nodiscard]] std::uint64_t getSpawnedChildCount()   const noexcept;
    [[nodiscard]] std::uint64_t getRefusedChildCount()   const noexcept;  // S8 addition A-3
    [[nodiscard]] std::uint64_t getFallbackChildCount()  const noexcept;  // FR-026
    [[nodiscard]] std::uint64_t getCompletedChildCount() const noexcept;
    [[nodiscard]] std::uint64_t getRejectedSpawnCount()  const noexcept;  // per ATTEMPT (FR-026)
    [[nodiscard]] std::uint32_t getOverlapEngagementCount() const noexcept;  // FR-052

    // last-event parent selection — S8 addition A-4, SC-005's only direct observable
    [[nodiscard]] std::size_t getLastParentSelectionCount() const noexcept;
    [[nodiscard]] std::size_t getLastParentIndex(std::size_t k) const noexcept;

    // per-child introspection by TABLE index i in [0, kMaxChildren)
    [[nodiscard]] std::size_t getChildSlotIndex(std::size_t i)     const noexcept;
    [[nodiscard]] std::size_t getChildParentIndex(std::size_t i)   const noexcept;  // A-4
    [[nodiscard]] float       getChildRatio(std::size_t i)         const noexcept;
    [[nodiscard]] float       getChildAmplitude(std::size_t i)     const noexcept;
    [[nodiscard]] float       getChildTargetAmplitude(std::size_t i) const noexcept; // A-7
    [[nodiscard]] Relation    getChildRelation(std::size_t i)      const noexcept;
    [[nodiscard]] Phase       getChildPhase(std::size_t i)         const noexcept;
    [[nodiscard]] float       getChildElapsedSeconds(std::size_t i) const noexcept;
    [[nodiscard]] bool        getIsChildFallback(std::size_t i)    const noexcept;
    [[nodiscard]] float       getChildHoldSeconds(std::size_t i)   const noexcept;   // A-8 (FR-036)

    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return 0u; }      // FR-004
    [[nodiscard]] bool stateFinite() const noexcept;                                 // FR-062
    [[nodiscard]] bool isPrepared() const noexcept;
```

**Out-of-range read-surface neutrals**, documented once above the block in the
`noise_organism.h:856-861` form: `0` for size getters, `0.0f` for float getters,
`Relation::Octave` and `Phase::Idle` for the two enums, `false` for `getIsChildFallback`,
`kMaxSlots` (an impossible index) for `getLastParentIndex` and `getChildSlotIndex` on a table entry
in `Phase::Idle`.

### S1.5 Private state layout (exact members, declaration order)

```cpp
private:
    // --- configuration ------------------------------------------------------
    double        sampleRate_ = 48000.0;
    float         invSampleRate_ = 1.0f / 48000.0f;
    float         controlDtSec_  = 64.0f / 48000.0f;      ///< kControlChunkSamples / fs
    float         controlRateHz_ = 48000.0f / 64.0f;      ///< fs / kControlChunkSamples
    bool          prepared_ = false;

    std::size_t   capacity_           = kMaxSlots;
    std::size_t   requestedChildSlots_= kDefaultChildSlots;  ///< see numChildSlots() in S1.4
    std::size_t   parentCountK_       = kDefaultParentCount;
    std::size_t   childrenPerEvent_   = kDefaultChildrenPerEvent;

    float depth_        = kDefaultDepth;
    float wake_         = 1.0f;
    bool  dormant_      = false;
    float spawnRateHz_  = kDefaultSpawnRateHz;
    float childGain_    = kDefaultChildGain;
    float fadeInSec_    = kDefaultFadeInSeconds;
    float holdSec_      = kDefaultHoldSeconds;
    float fadeOutSec_   = kDefaultFadeOutSeconds;
    float holdJitter_   = kDefaultHoldJitterFraction;
    float consumerTiltDb_ = 0.0f;
    std::array<float, kNumRelations> relationWeight_{1.0f, 1.0f, 1.0f};  // FR-016 default

    // --- smoothing ----------------------------------------------------------
    LinearRamp depthRamp_{kDefaultDepth};   ///< advanced ONCE PER CONTROL STEP (S3.3)

    // --- RNG (S1.6) ---------------------------------------------------------
    std::uint32_t seed_ = kDefaultSeed;
    Xorshift32    clockRng_{deriveStreamSeed(kDefaultSeed, kSaltClock)};
    Xorshift32    eventRng_{1u};            ///< RE-SEEDED per event from the step index
    std::uint64_t controlStep_ = 0;         ///< total control steps since prepare()/reset()

    // --- grid ---------------------------------------------------------------
    std::size_t   controlPhase_ = 0;        ///< absolute residue carried ACROSS calls (S3.2)

    // --- lifecycle table (FR-034) -------------------------------------------
    std::array<Child, kMaxChildren> children_{};
    std::uint64_t slotMask_ = 0;            ///< bit i set == some live child holds slot i
    std::size_t   liveCount_ = 0;
    std::size_t   cursor_ = 0;              ///< FR-056 round-robin cursor
    bool          engaged_ = false;         ///< FR-051 STICKY latch (Q8)
    bool          armed_   = false;         ///< one pending event (FR-043 edge-like)

    // --- per-event scratch (fixed size, never resized) ----------------------
    std::array<std::size_t, kMaxParents> parentIdx_{};
    std::array<float,       kMaxParents> parentAmp_{};
    std::array<float,       kMaxParents> parentRatio_{};
    std::size_t                          parentSelected_ = 0;
    std::uint32_t                        parentUsedMask_ = 0;   // FR-016 without-replacement
    std::array<float, kMaxSlots + kMaxChildrenPerEvent> occupiedLog2_{};
    std::size_t                                        occupiedCount_ = 0;

    // --- counters -----------------------------------------------------------
    std::uint64_t spawnEvents_ = 0, discardedEvents_ = 0, parentScans_ = 0;
    std::uint64_t offeredChildren_ = 0, spawnedChildren_ = 0, refusedChildren_ = 0;
    std::uint64_t fallbackChildren_ = 0, completedChildren_ = 0, rejectedSpawns_ = 0;
    std::uint32_t overlapEngagements_ = 0;

    friend struct detail::BloomEngineNonFiniteProbe;   // S7.5
```

`detail::BloomEngineNonFiniteProbe` is **forward-declared and never defined** in the header (the
`feedback_ecology.h:166-172` idiom); its single definition lives in
`bloom_engine_nonfinite_test.cpp`.

### S1.6 RNG architecture and the salt table (FR-070) — THE load-bearing design decision

```cpp
// APPEND ONLY. Renumbering a base silently changes every Phase-7 render.
static constexpr std::size_t kSaltClock   = 0;   // the FR-041 Bernoulli stream
static constexpr std::size_t kSaltEvent   = 1;   // the per-event stream BASE (see below)
static constexpr std::size_t kSaltNextFree = 2;
static_assert(kSaltClock < kSaltEvent && kSaltEvent < kSaltNextFree, "salt table overlap");
```

Two streams, and they are **shaped differently on purpose**:

1. **`clockRng_` — a persistent sequential stream, drawn exactly once per control step,
   unconditionally** (FR-041, Clarification Q7). Seeded `deriveStreamSeed(seed_, kSaltClock)`. It is
   drawn before the probability is even computed, so the stream position after *n* control steps is
   *n* draws — a pure function of elapsed control steps, never of dormancy/wake/depth/rate history.
   A rate of exactly 0 still draws; it simply can never succeed, because `nextUnipolar()` returns
   `(0, 1]` (`random.h:67-70`, `next()` ∈ `[1, 2^32-1]`) and the comparison is `u < p` with `p == 0`.

2. **`eventRng_` — a counter-based stream, RE-SEEDED at the head of every event** from
   `deriveStreamSeed(deriveStreamSeed(seed_, kSaltEvent), static_cast<std::size_t>(controlStep_))`.
   Both calls are the documented `core/random.h:102` API, and the outer call's non-zero guarantee is
   what keeps two events from collapsing onto one stream.

**Why the event stream is counter-based rather than sequential, and why this is not optional.**
FR-041 requires the RNG stream position to be "a pure function of elapsed control steps alone, never
of the engine's dormancy/wake/depth history", and SC-008 (c) asserts exactly that across a dormancy
edge. A *sequential* event stream cannot satisfy it: an instance that was awake during a prefix
consumes event draws that a dormant instance does not, so the two stream positions diverge
permanently and every later child differs. Re-seeding from the control-step index makes the event
draw sequence a function of `(seed, step)` alone, so the property holds by construction rather than
by bookkeeping — and the number of draws one event consumes (variable, because FR-026 retries are
outcome-dependent) becomes irrelevant to every later event. The alternative considered and rejected
was a fixed-size pre-draw block consumed on every control step: it makes the same guarantee but
burns ~57 `next()` calls per step (≈450 per 512-sample block, ~6 % of FR-072's whole budget) to
discard nearly all of them.

**Draw order inside one event is normative** (it is what the determinism criterion pins), and the
**complete** sequence — (d1) parent, (d2) relation, (d3) detune cents, (d4) the FR-026 final-attempt
fallback cents, (d5) the FR-036 hold jitter — is listed in S4.2. **Nothing outside that list draws:**
the parent scan (S4.1) consumes no draw, slot selection (S4.4) consumes no draw (Clarification Q6),
and the latch (S4.5) consumes exactly the one `(d5)` draw the list names. An implementer who placed
the jitter draw elsewhere in the sequence would shift every subsequent child's draws, and only
SC-008 (a) would catch it — and only if the two instances happened to differ.

---

## S2. `prepare()`, `reset()`, `setSeed()`

### S2.1 `prepare(double sampleRate, const PrepareConfig& config)` — the numbered step order

```
(1) sampleRate_ = max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));
    // sanitise(x, fallback) = detail::isFinite(x) ? x : fallback   (FR-004)
(2) controlDtSec_  = float(kControlChunkSamples) / float(sampleRate_);
    controlRateHz_ = float(sampleRate_) / float(kControlChunkSamples);
    invSampleRate_ = 1.0f / float(sampleRate_);
(3) capacity_            = clamp(config.capacity, 1, kMaxSlots);
    requestedChildSlots_ = clamp(config.numChildSlots, 0, min(kMaxChildren, capacity_));
    // NOTE: requestedChildSlots_ is clamped against the INITIAL capacity here and
    // never again; numChildSlots() re-derives min(requested, capacity_) so an
    // FR-055 shrink/grow pair is reversible (S6.3).
(4) depthRamp_.configure(kGainRampMs, controlRateHz_);   // ONE STEP PER CONTROL CHUNK
    depthRamp_.snapTo(depth_);                           // no 50 ms window at t=0 (SC-014 (a))
(5) prepared_ = true;
(6) setSeed(seed_);   // re-seeds clockRng_; LAST configuration step, house order
(7) reset();          // rewinds every clock, table, counter and cursor
```

`prepare()` **allocates nothing** — every member is a fixed-size array or a scalar (S9), so this
method is RT-safe in fact even though the house contract does not require it to be. Re-preparing a
live object is legal and fully re-initialises; `seed_` and every configuration scalar survive
(FR-004). A non-finite sample rate is substituted by 48 000 and then floored at 8 000.

`getAllocatedBytes()` returns `0` unconditionally — the `resonance_drift_network.h:906-912`
precedent, kept so the Phase-10 host can total its children uniformly.

### S2.2 `reset()` — configuration-preserving

```
children_.fill(Child{});   // every phase back to Idle
slotMask_ = 0; liveCount_ = 0; cursor_ = reserveBase(); engaged_ = false; armed_ = false;
controlStep_ = 0; controlPhase_ = 0;
clockRng_.seed(deriveStreamSeed(seed_, kSaltClock));
depthRamp_.snapTo(depth_);
every counter = 0;   parentSelected_ = 0; occupiedCount_ = 0;
```

`cursor_` starts at `reserveBase()` (FR-056). Resetting the counters is deliberate: SC-015's
expectations are stated over a run that begins at a `prepare()`.

### S2.3 `setSeed(std::uint32_t)`

Stores `seed_`, re-seeds `clockRng_` from `deriveStreamSeed(seed_, kSaltClock)`. It does **not**
touch `controlStep_`, the child table or the counters — a live re-seed is legal and changes only
future draws. `eventRng_` needs no action: it is re-seeded at every event from `seed_` and the step
index (S1.6).

---

## S3. `processChunk()` — the normative order

### S3.1 Guard ladder (FR-005), in this order

```cpp
std::size_t processChunk(float* ratios, float* amplitudes,
                         std::size_t parentCount, std::size_t numSamples) noexcept {
    // (0) PRECONDITION, not checkable here and therefore stated normatively in
    //     the doxygen (S1.4) and in S14 C-11: both arrays address >= kMaxSlots
    //     floats. NOTHING in this ladder bounds the write region by the caller's
    //     buffer, because the signature carries no length for it - parentCount is
    //     an ANALYSIS length. The write region is [pc, capacity_), and capacity_
    //     can be raised by setCapacity() after prepare().
    // (1) FR-005 / entropy_processor.h:271-277: a rejected call advances NOTHING.
    if (ratios == nullptr || amplitudes == nullptr) {
        return parentCount;
    }
    // (2) An unprepared object behaves as a pass-through, never as a crash.
    if (!prepared_) {
        return parentCount;
    }
    // (3) FR-005: parentCount is clamped to capacity() for every later use.
    const std::size_t pc = std::min(parentCount, capacity_);
    // (4) FR-005: numSamples == 0 applies the current state WITHOUT advancing.
    if (numSamples > 0) {
        advance(ratios, amplitudes, pc, numSamples);   // S3.2
    }
    return applyOutput(ratios, amplitudes, parentCount, pc);   // S3.4
}
```

Note (1) returns the caller's **unclamped** `parentCount`, exactly as a component that did nothing
must — and so does (2), and so does the **not-engaged** path of S3.4, for the same reason. `pc` is
an internal analysis-and-write bound only; it is **never** the return value of a call that wrote
nothing. Everything after (3) uses `pc`.

**Why the unclamped return matters in practice, not only in principle.** FR-050 tells a Phase-10
caller to set `capacity` from `HarmonicCloud::getActivePartialCount()` (`harmonic_cloud.h:950`),
which is routinely **below** the number of live partials the voice supplies — so `parentCount >
capacity()` is reachable at the default configuration, not only under abuse. A component configured
as an exact pass-through (FR-054 `numChildSlots = 0`, or a cold start at `depth = 0`) that returned
the clamped `pc` would hand the cloud a shorter count, and the cloud would pad `[pc, parentCount)`
to amplitude 0 (`harmonic_cloud.h:821-823`) — audible partial loss from a component specified to be
bit-identical to absent. **SC-014 (f)** is the arm that catches it.

### S3.2 The control-step loop — an absolute residue, not a block-relative grid (FR-006, FR-007)

```cpp
void advance(float* ratios, float* amplitudes, std::size_t pc, std::size_t numSamples) noexcept {
    // controlPhase_ lives ACROSS calls, so a 36 + 28 split runs exactly the one
    // control step an unsplit 64 runs (subharmonic_engine.h:592-607 idiom, with
    // the rendering removed because this component renders nothing).
    std::size_t remaining = numSamples;
    while (remaining > 0) {
        const std::size_t take = std::min(remaining, kControlChunkSamples - controlPhase_);
        controlPhase_ += take;
        remaining     -= take;
        if (controlPhase_ == kControlChunkSamples) {
            controlPhase_ = 0;
            controlStep(ratios, amplitudes, pc);     // S3.3
        }
    }
}
```

The number of control steps executed after *N* total advanced samples is `floor((N + phase0) / 64)`
for a fixed starting phase — a function of *N* alone (FR-006). `numSamples` may be any value
including one larger than 64; the loop runs as many steps as the sample count spans.

**The arrays are passed into the control step on purpose:** the FR-010 parent scan reads live
partial content, and the only live content the engine ever sees is the caller's array on the call
that crosses the step boundary. This is the one place the analysis and the clock meet.

### S3.3 `controlStep()` — the order is normative

```
(1) const float u = clockRng_.nextUnipolar();       // UNCONDITIONAL (FR-041, Q7)
(2) const float smoothedDepth = depthRamp_.process();   // one step per control chunk.
    // LinearRamp::process() is [[nodiscard]] (smoother.h:370): discarding the
    // return is MSVC C4834 / GCC-Clang -Wunused-result under the zero-warning
    // rule, and EVERY in-tree caller binds it (feedback_ecology.h:2087-2090,
    // noise_organism.h:1826, resonance_drift_network.h:1789,
    // subharmonic_engine.h:1230). Binding it also removes the read-after-write
    // ordering coupling a second getCurrentValue() read would create.
(3) const float gate = dormant_ ? 0.0f : wake_ * smoothedDepth;
    const float p = spawnRateHz_ * gate
                    * float(kControlChunkSamples) * invSampleRate_;
(4) advanceChildren();                              // S5.2 — BEFORE any spawn, so a new
                                                    //  child emits amplitude 0 on its
                                                    //  first chunk (FR-031's C1 start)
(5) const bool clockArm = (u < p);
    const bool arm      = clockArm || armed_;
    armed_ = false;                                 // FR-043: edge-like, consumed here
    if (arm) {
        if (gate == 0.0f) { ++discardedEvents_; }   // FR-035 / Q7: DISCARD, never hold
        else              { runEvent(ratios, amplitudes, pc, smoothedDepth); }  // S4
    }
(6) ++controlStep_;
```

Step (3) is the **single owner of the effective probability**. `gate == 0.0f` is an exact test and is
reachable exactly because `setWake` snaps `<= kWakeSilenceEpsilon` to `0.0f` at the source
(`resonance_drift_network.h:266` + its `gateSteady()` snap at `:1249`) and `dormant_` forces the
product to zero — which is what makes `setWake(0.0f)` and `setDormant(true)` behaviourally identical
**by construction** (FR-035, SC-014 (d)). `depth == 0` reaches the same test through the ramp: once
`smoothedDepth` — the value `depthRamp_.process()` just returned — has landed exactly on `0.0f`
(`LinearRamp::process()` lands exactly on its target, `smoother.h:379-383`), `gate` is exactly zero.

`smoothedDepth` is bound **once** per control step and is the **only** depth value read for the rest
of that step: step (5)'s `runEvent()` takes it as an argument and S4.5's latch uses it, so the gate
and the latch can never read two different ramp positions. There is no second `getCurrentValue()`
call on the hot path.

**`p` bounds, for the record:** `p <= kMaxSpawnRateHz * 64 / kMinUsableSampleRate = 4.0e-4`; at
48 kHz and the default rate it is `1.0f/240 * 64/48000 = 5.6e-6`, i.e. a mean inter-event time of
178 571 control steps = 240 s. No saturation, no `p >= 1` degeneracy anywhere in the configured
domain.

### S3.4 `applyOutput()` — the write region, the gap, and the sticky latch (FR-051, Q8)

```cpp
std::size_t applyOutput(float* ratios, float* amplitudes,
                        std::size_t parentCount, std::size_t pc) noexcept {
    if (!engaged_) {                 // FR-051: NOT ENGAGED => write nothing at all.
        return parentCount;          //   the caller's UNCLAMPED count (S3.1 note):
    }                                //   a call that wrote nothing returns what it
                                     //   was handed, exactly as the nullptr path does.
    if (pc > reserveBase()) {                              // FR-052
        // SATURATING, never wrapping - the resonance_drift_network.h:1871-1872
        // idiom with its own ceiling constant (S1.2 kMaxOverlapCount). One
        // increment per ENGAGED CALL, which is what SC-003's positive arm counts.
        const std::uint32_t headroom = kMaxOverlapCount - overlapEngagements_;
        overlapEngagements_ += std::min(std::uint32_t{1}, headroom);
    }

    // (a) THE GAP, and it is a hard requirement (FR-051). Slots in
    //     [pc, reserveBase()) sit BELOW the returned count, so the cloud reads
    //     them as live partial content (harmonic_cloud.h:821-823) and one stale
    //     NaN there rejects the WHOLE array (:812-818). Padded with the cloud's
    //     own form so the two agree by construction.
    for (std::size_t i = pc; i < reserveBase(); ++i) {
        ratios[i]     = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    // (b) THE OWNED REGION, one pass, occupancy from slotMask_.
    for (std::size_t i = reserveBase(); i < capacity_; ++i) {
        ratios[i]     = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    for (std::size_t c = 0; c < kMaxChildren; ++c) {
        const Child& ch = children_[c];
        if (ch.phase == Phase::Idle) continue;
        const std::size_t s = ch.slot;
        if (s < reserveBase() || s >= capacity_) continue;   // FR-055 legacy child: not written
        ratios[s]     = ch.ratio;                            // latched, finite, > 0 by S4.3
        amplitudes[s] = detail::flushDenormal(ch.amplitude); // in [0, target], finite by S5.3;
                                                             // the R10 / S7.3 flush, written HERE
                                                             // rather than left as prose
    }
    return capacity_;
}
```

Four properties this shape guarantees, each of which a criterion asserts:

* **No index at or above `capacity()` is ever written** (SC-003's canary), and no index below
  `kMaxSlots` is safe to omit from the caller's buffer — the write region is bounded by
  `capacity_`, which `setCapacity()` can raise (the S1.4 precondition, S14 **C-11**).
* **Every owned index not held by a live child carries the cloud's padding form** —
  `ratio = float(i + 1)`, `amplitude = 0.0f` — after **every** engaged call, including the slot of a
  child that retired on this very step (FR-032). That is loop (b) running unconditionally, before
  the child loop, on every call. Without it a retired child's slot keeps its last fade-out value and
  sounds forever, which is precisely the "the drone grows and never dies back" failure this phase
  exists to prevent. SC-003's owned-region arm asserts it index by index, cross-checked against
  `getChildSlotIndex`.
* **No index in `[0, min(pc, reserveBase()))` is ever written** (SC-003's before/after snapshot) —
  the loops start at `pc` and at `reserveBase()` respectively, so the parent region is untouched
  except for FR-052's documented overlap.
* **Every index below the returned count is finite, `ratio > 0`, `amplitude >= 0`** (FR-009 (d)) —
  the padding form is `i + 1 >= 1` and `0.0f`, and a child's latched pair passed S4.3's tests. So
  this component can never be the cause of `setSpectralTarget`'s wholesale rejection.

`engaged_` is set **once**, inside the spawn path, and cleared only by `prepare()`/`reset()`
(Clarification Q8). It is deliberately not level-tracking: an implementation that fell back to a
pass-through when `getLiveChildCount()` reaches 0 would stop padding the gap, and the caller's stale
bytes would sound. `isEngaged()` (S8 addition A-5) is the read-surface observable SC-003's sticky
arm needs.

**Cost, for S12:** the write phase is `(capacity_ - pc) + 2·numChildSlots()` float stores per
**call**. At the Phase-10 call shape (one `processChunk` per 64-sample control chunk) that is
8 calls × ≤ 144 stores = ≤ 1 152 stores per 512-sample block. Measured separately by FR-073's stage
probe (S12.2 arm (b)).

---

## S4. The spawn event, step by step

`runEvent()` executes only on a control step where `gate != 0` and an event is armed (S3.3 step 5).

### S4.1 Strongest-K parent scan (FR-010, FR-011, FR-012, FR-013)

```
++spawnEvents_;
++parentScans_;                      // FR-013: EXACTLY ONE SCAN PER EXECUTED EVENT
parentSelected_ = 0; parentUsedMask_ = 0;
const std::size_t scanEnd = std::min(pc, reserveBase());     // FR-010, load-bearing
for (std::size_t i = 0; i < scanEnd; ++i) {
    const float r = ratios[i], a = amplitudes[i];
    if (!detail::isFinite(r) || !detail::isFinite(a)) continue;   // FR-009 (e)
    if (r <= 0.0f) continue;
    if (a <= kSilentParentAmplitude) continue;                    // FR-011
    insertDescending(i, a, r);        // stable: strict `>` on ascending i
}
```

`insertDescending` maintains `parentIdx_/parentAmp_/parentRatio_` as a fixed `K`-entry
insertion-sorted list, where `K = min(parentCountK_, kMaxParents)`. **The comparison is strict
`a > parentAmp_[j]`**, and the scan runs on ascending `i`, so an equal amplitude never displaces an
earlier (lower) index — FR-011's tie-break falls out of the loop shape rather than being a special
case. Cost: `O(scanEnd · K)` = 48 × 8 = 384 compares, once per event.

`getParentScanCount() == getSpawnEventCount()` holds by the two increments sitting adjacent, and
SC-005 asserts it. A discarded event (S3.3) increments neither.

If `parentSelected_ == 0`, the event is **consumed and produces no child** (FR-014): no child is
offered, `getOfferedChildCount()` does not move, and `runEvent()` returns. This is not an error.

**Counter semantics on this path are normative (S14 C-10).** An FR-014 event increments
`getSpawnEventCount()` and `getParentScanCount()` and **nothing else**. It does **not** touch
`getRejectedSpawnCount()`: that counter counts rejected *candidate ratios*, and an event with no
eligible parent never forms one. FR-061 lists FR-014 among `getRejectedSpawnCount()`'s contributors
(`spec.md:652-653`); that is corrected in S14 **C-10**, because counting it would make the counter's
unit ambiguous (rejected candidates plus consumed events) and would corrupt SC-015 (e)'s FR-025
arithmetic. FR-014 also had **no criterion at all** in the spec; **SC-005 gains an FR-014 arm**
(S10.3) that drives an all-silent and an all-non-finite parent array through a forced event.

### S4.2 Per-child draw sequence — normative order

`eventRng_.seed(deriveStreamSeed(deriveStreamSeed(seed_, kSaltEvent), std::size_t(controlStep_)));`
is the **first** statement of `runEvent()`, before the scan, so the stream is a function of
`(seed, step)` only.

For each of `childrenPerEvent_` children, in index order:

```
++offeredChildren_;
// (s0) SLOT AVAILABILITY IS A PRECONDITION OF THE CHILD, tested ONCE, BEFORE the
//      attempt loop and BEFORE any draw, with a PURE PEEK (S4.4: peekSlot reads
//      cursor_ and slotMask_ and mutates NEITHER). FR-025 is a per-EVENT
//      condition - "every owned slot is live" - not a per-draw one: retrying a
//      draw cannot make a slot appear. So a slot-exhausted child consumes NO
//      attempt and NO draw, and is counted refused EXACTLY ONCE, here.
std::size_t slot = 0;
if (!peekSlot(slot)) {                     // FR-025
    ++rejectedSpawns_;                     // ONE rejection for this child, not four
    ++refusedChildren_;
    continue;                              // on to the next child of this event
}
bool placed = false;
for (attempt = 0; attempt < kMaxSpawnAttempts && !placed; ++attempt) {
    (d1) parent   = drawParent();          // FR-016: without replacement while unused remain
    (d2) relation = drawRelation();        // FR-060 weights; all-zero => uniform
    (d3) cents    = (relation == DetunedNeighbour) ? drawDetuneCents() : 0
    candidate ratio = parentRatio * factor(relation, cents)
    if (accept(candidate))  { place(slot); placed = true; break; }   // place() -> S4.5 latch
    ++rejectedSpawns_;                     // FR-026: EVERY failed attempt counts
    if (attempt == kMaxSpawnAttempts - 1 && relation != DetunedNeighbour) {
        (d4) cents = drawDetuneCents();    // FR-026 final-attempt fallback
        candidate ratio = parentRatio * factor(relation, 0) * centsToPitchRatioFast(cents)
        if (accept(candidate)) { place(slot, fallback = true); ++fallbackChildren_; placed = true; }
        else                   { ++rejectedSpawns_; }
    }
}
if (!placed) { ++refusedChildren_; }       // the ONLY other refusal increment; nothing to
                                           // release, because (s0) mutated no state
// (d5) HOLD JITTER - one nextFloat(), drawn ONLY on an accepted candidate, inside
//      the S4.5 latch, as the LAST draw of the child. Listed here because THIS
//      SEQUENCE IS NORMATIVE AND COMPLETE (S1.6).
```

**Refusal is counted exactly once per offered child**, at exactly one of the two sites above — the
`(s0)` FR-025 precondition or the `!placed` tail — never both. An earlier draft incremented
`refusedChildren_` inside the slot-taking helper's failure path (the single `takeSlot()` that S4.4
has since been split into `peekSlot`/`commitSlot`) *and* in the tail, which counted a
slot-exhausted child up to five times, broke S14 **C-7**'s exact identity
`offered == spawned + refused` (SC-015 (e)) and inflated `getRejectedSpawnCount()` fourfold in
exactly the FR-025 case SC-015 (e)'s "roughly 77 % of offered children are refused" arithmetic is
written against.

* `drawParent()` — one `nextUnipolar()`. Candidates are the `parentSelected_` entries with their bit
  clear in `parentUsedMask_`; when the mask is full it is cleared first (FR-016's with-replacement
  fallback once every selected parent has supplied a child). Index chosen as
  `min(std::size_t(u * float(available)), available - 1)` — the `min` is what keeps `u == 1.0f`
  (reachable, `nextUnipolar()` ∈ `(0,1]`) in range.
* `drawRelation()` — one `nextUnipolar()` against the cumulative weight table. Weights are
  `relationWeight_[]`, each `[0,1]`, default `1.0` each (FR-016). `total == 0.0f` falls back to
  uniform (Edge Case: all three weights zero).
* `drawDetuneCents()` — **one** `nextFloat()` (bipolar, `random.h:59`), mapped to the punctured band
  in a single expression:
  `c = (b < 0 ? -1.0f : 1.0f) * (kMinDetuneCents + std::abs(b) * (kMaxDetuneCents - kMinDetuneCents))`
  so `c ∈ [-50, -24] ∪ [24, 50]` exactly (FR-020), and `b == 0` takes the positive branch rather
  than producing 0.
* `factor(Octave, c) = kOctaveFactor`, `factor(Fifth, c) = kFifthFactor`,
  `factor(DetunedNeighbour, c) = centsToPitchRatioFast(c)` — in-domain by
  `kMaxDetuneCents == 50` (`pitch_utils.h:55-62`).
* The **fallback** form is `parentRatio * kOctaveFactor * centsToPitchRatioFast(c)` (or the fifth
  factor) — a detuned octave/fifth. `getChildRelation()` still reports the **original** relation;
  `getIsChildFallback()` reports `true` (FR-026).
* A `DetunedNeighbour` whose final attempt fails has **no** separate fallback form (it is already
  detuned) — the `relation != DetunedNeighbour` guard above.

`kMaxSpawnAttempts` is a compile-time constant, so the loop is RT-bounded: at most
`kMaxChildrenPerEvent × (kMaxSpawnAttempts + 1) = 20` candidate evaluations per event.

### S4.3 `accept(candidate)` — three tests, all rejections, no clamps

Run in this order, cheapest first:

1. **Finiteness and positivity.** `detail::isFinite(r) && r > 0.0f`. A parent ratio near the float
   ceiling multiplied by 2 can produce `Inf`; `Inf` also fails test 2, but the explicit test keeps
   the later `std::log2` in domain.
2. **Ratio bounds (FR-021).** `r >= kMinChildRatio && r <= kMaxChildRatio`. **Never a clamp** —
   "clamping moves a partial to a pitch nobody asked for, which is a defect; refusing to grow one is
   not."
3. **Minimum spacing (FR-022, FR-016).** `|log2(r) - occupiedLog2_[j]| >= kMinRatioSpacingLog2` for
   every `j < occupiedCount_`.
4. **Latched-target finiteness** (S14 **C-4**, a plan addition): compute the FR-023 target (S4.5)
   and require `detail::isFinite(target) && target >= 0.0f`. A pathological parent amplitude
   combined with the tilt division (whose gain reaches ×3993 at `tiltDb = -12`, slot 63) can
   overflow to `Inf`, which FR-009 (d) forbids writing. Rejecting is the only response consistent
   with FR-021's "reject, never clamp" and with FR-023's explicit refusal to clamp the target to 1.

`occupiedLog2_` is built **once per event**, immediately after the parent scan:

```
occupiedCount_ = 0;
for (i in [0, min(pc, reserveBase())))   if finite & > 0: occupiedLog2_[occupiedCount_++] = log2(ratios[i]);
for each live child with slot in [reserveBase(), capacity_):  occupiedLog2_[occupiedCount_++] = log2(child.ratio);
```

and each **accepted sibling appends its own `log2`** before the next child of the same event is
drawn (FR-016, Clarification Q4) — which is why the array is sized
`kMaxSlots + kMaxChildrenPerEvent`. Cost: ≤ 64 `std::log2` per event plus one per candidate.

A **legacy child** (FR-055, slot outside `[reserveBase(), capacity_)`) is deliberately **excluded**
from the spacing set: it is never written into the array, so it is not a partial anything can
collide with. It is **not** excluded from slot occupancy (S4.4) — FR-053's no-shared-slot guarantee
holds wherever the slot sits.

### S4.4 Owned-slot selection (FR-056, Clarification Q6)

Slot handling is split into a **pure peek** and a **commit**, and the split is load-bearing:

```cpp
/// PURE PEEK. Reads cursor_ and slotMask_ and mutates NEITHER. Called once per
/// offered child at S4.2 (s0), before any draw.
[[nodiscard]] bool peekSlot(std::size_t& out) const noexcept {
    const std::size_t base = reserveBase(), cap = capacity_;
    if (base >= cap) return false;                     // numChildSlots() == 0
    // re-clamp AS A LOCAL after setCapacity(); cursor_ itself is not written here
    const std::size_t start = (cursor_ < base || cursor_ >= cap) ? base : cursor_;
    for (std::size_t n = 0; n < (cap - base); ++n) {
        const std::size_t s = base + ((start - base + n) % (cap - base));
        if ((slotMask_ & (std::uint64_t{1} << s)) == 0) { out = s; return true; }
    }
    return false;                                      // every owned slot live -> FR-025
}

/// COMMIT. Called ONLY from the S4.5 latch, ONLY on an accepted candidate.
void commitSlot(std::size_t s) noexcept {
    const std::size_t base = reserveBase(), cap = capacity_;
    slotMask_ |= (std::uint64_t{1} << s);
    cursor_ = base + ((s - base + 1) % (cap - base));   // always past the taken slot
}
```

**Why the split.** If the cursor advanced on a child that was later refused, the slot sequence would
depend on the seeded accept/reject outcomes, and SC-018 (d) — an identical slot sequence under a
*different* seed — would fail on correct code. `peekSlot` mutates nothing, and nothing between
`(s0)` and the latch can change `slotMask_` or `cursor_` (no draw touches them, Q6), so the slot the
peek returned is exactly the slot the commit takes.

**Slot choice consumes no draw from the seeded stream** (Q6) — it is derived from `cursor_` and
`slotMask_` only. That is what SC-018 (d) asserts by re-running with a different seed and requiring
an identical slot sequence, and it is why the fixture for that clause must drive spawns through
`triggerBloom()` rather than through the internal clock (S14 **C-6**).

`peekSlot()` returning `false` is FR-025: the child is **refused exactly once** —
`++rejectedSpawns_` and `++refusedChildren_`, both at S4.2 `(s0)` and **nowhere else** — it consumes
no attempt and no draw, and **nothing is stolen or evicted**: killing a sounding child is a click by
construction.

### S4.5 The latch (FR-023, FR-024, FR-033, FR-036) — and the tilt compensation arithmetic

```cpp
// The cloud's own law, RESTATED (D-2/D-9). Identity branch copied verbatim from
// harmonic_cloud.h:1430-1432 so tilt 0 and slot 0 are EXACTLY 1.0f and the
// latch is bit-identical to the pre-Q1 form.
[[nodiscard]] float tiltGain(std::size_t index) const noexcept {
    if (consumerTiltDb_ == 0.0f || index == 0) return 1.0f;
    return std::exp2(consumerTiltDb_ * std::log2(static_cast<float>(index + 1)) * kLog2TenOver20);
}
```

`std::log2(static_cast<float>(index + 1))` is the **same expression** that fills
`detail::kHarmonicCloudLog2N` (`harmonic_cloud.h:57-63`), so on a given platform the two agree
bit-for-bit; SC-017's ±0.5 dB band has ~7 orders of margin over any residual difference.

The latch, executed once per accepted child:

```
child.ratio       = candidateRatio;                                   // FR-024
child.target      = parentAmp * childGain_ * smoothedDepth / tiltGain(slot);
                  // ^ smoothedDepth is the value S3.3 step (2) bound from
                  //   depthRamp_.process() and passed down through runEvent() -
                  //   NOT a second getCurrentValue() read, so the gate and the
                  //   latch can never disagree about the ramp position.
child.amplitude   = 0.0f;                                             // C1 start
child.step        = 0;
child.fadeInSteps  = secondsToSteps(fadeInSec_);                      // FR-033, UNJITTERED
child.fadeOutSteps = secondsToSteps(fadeOutSec_);                     // FR-033, UNJITTERED
child.holdSteps    = secondsToSteps(holdSec_ * (1.0f + holdJitter_ * eventRng_.nextFloat()));
                     // ^ draw (d5) of S4.2's normative sequence: the LAST draw of
                     //   the child, taken only now that the candidate is accepted
child.relation    = relation;  child.fallback = isFallback;
child.parentIndex = parentIdx_[chosen];
child.phase       = Phase::FadeIn;                                    // step 0 => amplitude 0
commitSlot(slot);  ++liveCount_;  ++spawnedChildren_;  engaged_ = true;   // S4.4 commit
```

* **The division cancels the cloud's own `tiltGain(slot)` multiply** (`harmonic_cloud.h:1493`), so
  the child sounds at `parentAmplitude_at_spawn × childGain × depth` after the cloud applies tilt —
  whatever slot it landed in. At the `consumerTiltDb = 0` default the divisor is exactly `1.0f`,
  so the whole Q1 mechanism is bit-inert by default.
* **The target is NOT clamped to 1.0** (FR-023, review note RN-2): `setSpectralTarget` documents
  that "amplitudes above 1 are all **ACCEPTED** — that is the point of the surface"
  (`harmonic_cloud.h:759-762`), and FR-009 (d) requires only `>= 0`.
* **The hold jitter draw is the last draw of the child** (S4.2 order), and it is `nextFloat()`
  (bipolar), so `u ∈ [-1, 1]` and `1 + holdJitter_·u >= 0` for every `holdJitter_ ∈ [0,1]` — the
  jittered hold is never negative (FR-036). At `holdJitter_ == 0` the product is exactly
  `holdSec_ * 1.0f`, so every child's latched hold equals the configured value **bit-for-bit**,
  which SC-002 (e) asserts.
* `secondsToSteps(s) = max(1u, uint32(round(clamp(s, lo, hi) * controlRateHz_)))` for the two fades;
  the hold uses the same conversion with a floor of `0` (FR-030 admits `holdSeconds == 0`).

---

## S5. The lifecycle clock and the envelope

### S5.1 Time is counted in integer control steps, not accumulated seconds

`Child::step` is a `std::uint32_t` incremented once per control step; `getChildElapsedSeconds(i)`
returns `float(step) * controlDtSec_`. The alternative — accumulating `elapsedSec += controlDtSec_`
— was rejected on arithmetic, not taste: over a 300 s fade at 48 kHz that is 225 000 float additions
whose worst-case accumulated rounding is `N·eps·value/2 ≈ 225000 · 6e-8 · 300/2 ≈ 2.0 s`, i.e.
**0.67 %** — outside SC-010's ±0.5 % band on the maximum configured fade, on correct code. Integer
counting is exact; the only quantisation left is the one-step rounding at spawn (≤ 0.67 ms at
48 kHz, 1.5e-5 relative at the 45 s default).

Range: the longest configurable lifetime is `300 + 900 + 600 = 1800` s, which at 192 kHz is
`1800 × 192000/64 = 5.4e6` steps — three orders below `UINT32_MAX`.

### S5.2 `advanceChildren()` — one pass, executed before any spawn (S3.3 step 4)

```
for each child c with phase != Idle:
    ++c.step;
    const uint32 endIn   = c.fadeInSteps;
    const uint32 endHold = endIn + c.holdSteps;
    const uint32 endOut  = endHold + c.fadeOutSteps;
    if      (c.step <  endIn)   { c.phase = FadeIn;
                                  c.amplitude = c.target * smoothstep(float(c.step)/float(endIn)); }
    else if (c.step <  endHold) { c.phase = Hold;    c.amplitude = c.target; }
    else if (c.step <  endOut)  { c.phase = FadeOut;
                                  c.amplitude = c.target *
                                      (1.0f - smoothstep(float(c.step-endHold)/float(c.fadeOutSteps))); }
    else                        { retire(c); }     // FR-032
```

`retire(c)`: `c.phase = Idle; c.amplitude = 0.0f; slotMask_ &= ~(1ull << c.slot); --liveCount_;
++completedChildren_;`. The slot is released **on the same control step** the amplitude reaches 0,
and S3.4's owned-region pad writes it as `ratio = index + 1, amplitude = 0` on that same chunk
(FR-032, FR-051) — unless the slot lies outside `[0, capacity())` after an FR-055 shrink, in which
case the final padding write is skipped because the index is not in the write region.

Deriving the phase from a single `step` counter against three latched bounds — rather than keeping a
per-phase clock — is what makes FR-033 (no retiming of a live child) true by construction: nothing a
setter writes is ever read again by a child already in flight.

### S5.3 The envelope: smoothstep, and why it is C1 (FR-031)

```cpp
[[nodiscard]] static constexpr float smoothstep(float u) noexcept {
    return u * u * (3.0f - 2.0f * u);   // == 3u^2 - 2u^3, one fewer multiply
}
```

* `f(0) = 0`, `f(1) = 1·(3−2) = 1` — **both exact in float**, so a fade-in reaches the latched target
  exactly and a fade-out reaches exactly `0.0f` (`1.0f - 1.0f`), never asymptotically. That is why
  the Edge Cases entry on denormals is true: no denormal tail accumulates.
* `f'(u) = 6u(1−u)`, so `f'(0) = f'(1) = 0` — value **and** first derivative are zero at both ends,
  which is the definition FR-031 states.
* The FadeOut form is written as `1 - smoothstep(v)`, not as `smoothstep(1 - v)`. The two are
  algebraically identical (`3(1−v)² − 2(1−v)³ = 1 − 3v² + 2v³`) but only the first lands on exact
  zero at `v = 1` in float.
* **Hold junctions are C1 too:** FadeIn ends with derivative 0 and Hold has derivative 0, likewise
  Hold → FadeOut. `holdSteps == 0` is therefore legal and still C1 (Edge Case).

**Plateau analysis (SC-002 (d)'s "no plateau of identical consecutive samples exceeds 1 s").** Near
the end of a fade the per-step change is `Δ ≈ 6ε·du` at `u = 1−ε`, while the value sits near the
target, where `ulp ≈ 5.96e-8 × target`. The `target` factor cancels. A plateau begins when
`6ε·du < 5.96e-8`. At the 300 s maximum fade and 48 kHz, `du = 64/(48000·300) = 4.44e-6`, giving
`ε < 2.24e-3`, i.e. the last `2.24e-3 × 300 s = 0.67 s` — **under the 1 s gate**, on the longest
configured fade at the rate where the step is smallest. At the 45 s default it is 0.10 s. No floor,
no special case, and nothing in the design needs to change to satisfy the criterion.

**Monotonicity** is exact: `smoothstep` is monotone increasing on `[0,1]` and `float(step)/float(end)`
is monotone increasing in `step`, so the emitted series is monotone non-decreasing over the fade-in
and non-increasing over the fade-out (SC-002 (d)).

**The retracted step-size floor.** FR-031's earlier prepare-time assert and fade-time clamp are not
implemented, and the plan states why in the header comment: the cloud's amplitude mask compares
against `committedAmp_[i]` (`harmonic_cloud.h:841`), committed only inside the dirty-gated recompute
(`:1481`), so sub-`kTargetAmpEpsilon` per-chunk motion **accumulates until it trips the threshold**
— the header names the opposite design as the broken one (`:827-838`). A long fade's target is
**quantised at ~1e-5 (−100 dB), never stalled**, and the cloud's own 2 ms amplitude smoother
(`kAmpSmoothTimeSec = 0.002f`, `:165`) smooths what reaches the kernel.

---

## S6. Dormancy, wake, depth, capacity, triggering

### S6.1 Dormancy and wake (FR-035, D-7)

`setDormant(bool)` sets `dormant_`; `setWake(float)` stores
`std::clamp(sanitise(amount, 1.0f), 0.0f, 1.0f)` and then **snaps `<= kWakeSilenceEpsilon` to exactly
`0.0f`**. Both feed the single `gate` expression of S3.3 step (3) and **nothing else**: wake does not
scale child gain, child ratio, an in-flight envelope, or `triggerBloom()`. The composition with depth
is multiplicative, giving exactly FR-035's
`rateHz * depth * wake * kControlChunkSamples / sampleRate`.

Dormant means **no new spawns**; the FR-041 clock draw keeps running (S3.3 step 1), the depth ramp
keeps advancing, and **children already in flight run their lifecycle to completion**. This
component has no audio chain to skip (FR-002), so the cross-cutting rule's "skip the chain" has no
referent; cutting a 45-second swell at a dormancy edge is exactly the click FR-031 exists to
prevent. Re-entry needs no 50 ms fade because nothing was gated (D-7).

An event armed on a step where `gate == 0.0f` is **consumed and discarded on that same step**
(`++discardedEvents_`), never held (Q7) — there is no catch-up burst at a wake edge.

### S6.2 Depth (FR-042)

`setDepth(float)` clamps to `[0,1]`, stores `depth_`, and calls `depthRamp_.setTarget(depth_)`. The
ramp is configured at `kGainRampMs` against the **control rate** (`fs / 64`), so `process()` is
called once per control step and the ramp completes in 50 ms of real time regardless of block size —
the `subharmonic_engine.h:441` idiom for a control-rate ramp.

The **smoothed** value — bound once per control step as `smoothedDepth` from
`depthRamp_.process()`'s `[[nodiscard]]` return (S3.3 step (2)) and passed down to `runEvent()` —
gates both the latch (S4.5) and the clock probability (S3.3). There is no second, raw path, and no
second `getCurrentValue()` read on the hot path. `getDepth()` reports the configured value and
`getSmoothedDepth()` (S8 addition A-6) the ramp's current value, which is what SC-014 (a)'s
precondition needs to be checkable rather than assumed.

### S6.3 Live capacity change (FR-055, Clarification Q3)

`setCapacity(std::size_t)` clamps to `[1, kMaxSlots]` and writes `capacity_`. `numChildSlots()`
re-derives `min(requestedChildSlots_, capacity_)` on every read, so:

* **Growth** takes effect immediately — `reserveBase()` and the cursor recompute on the next
  `processChunk`, and the newly available slots are spawnable at once (`peekSlot()` re-clamps the
  cursor into the new range as a local, and `commitSlot()` writes it back — S4.4).
* **Shrinkage** is deferred — `peekSlot()` only ever returns a slot in `[reserveBase(), capacity_)`,
  so no new child lands at or above the new capacity; a live child already above it is **not**
  killed, retimed or evicted. It keeps advancing its `step` in the table (so
  `getChildElapsedSeconds` strictly increases and its phase transitions land at the same latched
  offsets), is skipped by S3.4's write loop, and on completion frees its slot with the padding write
  skipped (S5.2). Its bit stays set in `slotMask_`, so the slot is never reused while it lives, even
  after a grow-back (FR-053).

Storing the *requested* child-slot count rather than overwriting it is what makes a shrink/grow pair
reversible; overwriting would silently and permanently shrink the reserve.

### S6.4 Triggering (FR-040, FR-043)

`triggerBloom()` sets `armed_ = true`. It is **edge-like**: calling it *n* times between two control
steps arms one event, because `armed_` is a `bool` consumed and cleared in S3.3 step (5). That is
what makes it safe for a caller polling `SlowEventScheduler::isEventActive()`
(`slow_event_scheduler.h:361`) rather than its onset. The natural bug is `armed_` as a **counter** —
exactly the shape a level-polling caller invites — which would produce a burst of *n* events, i.e.
`n × childrenPerEvent` simultaneous children. **SC-018 (e)** is the arm written to fail on it; no
other criterion can see the difference. The engine holds **no** scheduler reference and
does not include `slow_event_scheduler.h` — Phase 10 owns the scheduler (`noise_organism.h:841-843`).

`setSpawnRateHz(0.0f)` disables the internal clock entirely (`p == 0`), leaving `triggerBloom()` as
the only source. The two sources are additive and independent.

---

## S7. The safety ladder and FR-009's argument contract

### S7.1 Rung 1 — structural bounds

Every array index is derived from `capacity_ <= kMaxSlots = 64`, `reserveBase() = capacity_ -
numChildSlots()` with `numChildSlots() <= capacity_`, and `slot < capacity_`. `parentCount` is
clamped to `capacity_` at the top of `processChunk`. No index arithmetic can leave `[0, 64)`, and the
`kMaxSlots <= 255` static_assert keeps `Child::slot` (a `std::uint8_t`) lossless.

### S7.2 Rung 2 — candidate rejection (S4.3)

Ratio bounds, spacing, finiteness of both the ratio and the latched target. Nothing is clamped into
range; a candidate that fails is refused.

### S7.3 Rung 3 — emitted-value hygiene (FR-009 (d))

Every value the engine writes is either the cloud's own padding form (`float(i+1)`, `0.0f`) or a
latched pair that passed rung 2 multiplied by a smoothstep in `[0,1]`. So every written ratio is
finite and `> 0`, and every written amplitude is finite and in `[0, target]`.
`detail::flushDenormal` is applied to the emitted amplitude as belt-and-braces, and it appears **in
S3.4's normative write loop** (`amplitudes[s] = detail::flushDenormal(ch.amplitude);`) rather than
only in prose here — an implementer who types the code blocks verbatim must end up with the
mitigation R10 claims, not without it. The library-wide FTZ/DAZ setup applies anyway
(`tests/test_helpers/enable_ftz_daz.h` via `dsp_test_main.cpp`).

### S7.4 Rung 4 — the non-finite trap

`stateFinite()` walks the 16 `Child` records testing `ratio`, `target` and `amplitude` with
`detail::isNaN` / `detail::isInf` (`db_utils.h:99`, `:260`) — never `std::isnan`/`isinf`/`isfinite`,
which `tools/lint-nonfinite-symbols.js` enforces (FR-008). It is a **read**, not a repair: nothing in
rungs 1–3 can produce a non-finite child, so a `false` here is a defect report, not a recovery path.

### S7.5 The fault-injection probe (SC-009)

```cpp
namespace detail {
/// Declared, never defined here. Its ONE definition lives in
/// bloom_engine_nonfinite_test.cpp (feedback_ecology.h:166-172 idiom), which is
/// the only TU in the -fno-fast-math block.
struct BloomEngineNonFiniteProbe;
}  // namespace detail
```

The probe exists so SC-009 can prove `stateFinite()` **fires** — poison a `Child::amplitude` with a
NaN bit pattern through a volatile sink and assert `stateFinite() == false`. Without it the trap is
only ever observed returning `true`, which proves nothing.

### S7.6 FR-009's normative argument contract — one rule, applied everywhere

| Argument shape | Response | Getter afterwards |
|---|---|---|
| non-finite float to any setter | **reject**, previous value stands | reports the previous value |
| out-of-range index (`Relation`, child index, slot) | silent no-op / documented neutral | unchanged |
| out-of-range float | **clamp**, applied value stored | reports the clamp |
| non-finite sample rate to `prepare` | substituted by 48 000, then floored at 8 000 | `getSampleRate()` reports the applied value |
| `nullptr` array to `processChunk` | no-op, **no advance**, returns `parentCount` | — |
| non-finite `ratios[i]`/`amplitudes[i]` in the parent region | that slot is **disqualified** as a parent; never copied, never written back | — |

The one place a float is **rejected rather than clamped** in the *candidate* path is S4.3 — and that
is a rejection of a spawn, not of an argument.

---

## S8. Read-surface additions this plan makes, and why each is needed

The spec's FR-061 list is the floor, and two success criteria assert against observables it does not
contain. Eight additions, each tied to a clause that is otherwise unassertable. All are `const
noexcept` getters over state the engine already holds; none adds a byte of hot-path work.

| # | Addition | The clause that needs it |
|---|---|---|
| **A-1** | `getDiscardedEventCount()` | FR-035/FR-043's Q7 discard rule. Without it, "discarded, never held" is indistinguishable from "never armed", and no test can see a `triggerBloom()` swallowed by a dormant step. |
| **A-2** | `getOfferedChildCount()` | SC-015 (e)'s accounting clause. See S14 **C-7**: `spawned + rejected` cannot "account for every offered child" once FR-026 counts *attempts*. |
| **A-3** | `getRefusedChildCount()` | The other half of C-7: the exact identity `offered == spawned + refused`. |
| **A-4** | `getLastParentSelectionCount()`, `getLastParentIndex(k)`, `getChildParentIndex(i)` | SC-005 asserts "the selected parents are exactly the K largest". With `K` up to 8 and `childrenPerEvent` up to 4, most selected parents never produce a child, so the selection is invisible through the child table. |
| **A-5** | `isEngaged()` | SC-003's sticky-latch arm (Q8) and SC-014's cold-start arms. The latch is currently inferable only from the return value, which conflates it with `parentCount == capacity()`. |
| **A-6** | `getSmoothedDepth()` | SC-014 (a)'s precondition ("the smoothed depth is never non-zero") must be *checked*, not assumed, or the criterion is a coin flip on ramp timing. |
| **A-7** | `getChildTargetAmplitude(i)` | SC-015 (b) asserts every emitted amplitude lies in `[0, latched target]` and that the target equals the FR-023 formula within `1e-6`. The target is not otherwise readable. |
| **A-8** | `getChildHoldSeconds(i)` | SC-002 (e) asserts two children of one event get *different latched holds* and that each lies in `holdSeconds·(1 ± 0.5)`. The jittered hold is not otherwise readable. |

Each getter's doc comment names the criterion it serves, in the
`entropy_processor.h:299-302` "public contract, not `#ifdef` scaffolding" form.

---

## S9. Allocation and footprint ledger (FR-004, FR-071, FR-074)

| Term | Bytes | Note |
|---|---|---|
| `children_` | 16 × 32 = 512 | fixed `std::array` |
| `occupiedLog2_` | (64 + 4) × 4 = 272 | per-event scratch, fixed |
| `parentIdx_/parentAmp_/parentRatio_` | 8×8 + 8×4 + 8×4 = 128 | fixed |
| counters (10 × `uint64` + 1 × `uint32`) | 84 | |
| configuration scalars, two `Xorshift32`, one `LinearRamp` | ≈ 120 | |
| **total `sizeof(BloomEngine)`** | **≈ 1.2 KB** | FR-074: **reported** by a `WARN(sizeof(BloomEngine))` in SC-007's case (S10.3 names it), never asserted to a byte — the figure above is an estimate and pinning it would be a portability trap |

**Heap term: zero.** No `std::vector`, no `resize`, no `new`. `getAllocatedBytes()` returns `0`
unconditionally and `prepare()` is allocation-free in fact (the getter exists so the Phase-10 host
can total its children uniformly, `resonance_drift_network.h:906-912`). SC-007 wraps 10 000
`processChunk` calls plus every setter and `triggerBloom()` in an `AllocationScope`
(`allocation_detector.h:111`) and requires **0**.

---

## S10. Test plan

### S10.1 No new helper (spec A-4, re-verified)

Every metric the criteria need already exists, at `tests/test_helpers/` (repo root — **not**
`dsp/tests/test_helpers/`): `artifact_detection.h:130` (`detect`), `audio_features.h:27,88`
(`centroidHz`), `statistical_utils.h:41,76,90`, `render_fingerprint.h:73,122`,
`allocation_detector.h:111`, `spectral_analysis.h`, `signal_metrics.h`. SC-016 and SC-017 need only
`HarmonicCloud`'s own introspection (`:950`, `:959`, `:963`, `:868`). **This phase adds no helper
header and therefore no `tests/test_helpers/CMakeLists.txt` edit** (that target is an INTERFACE
library with no source list).

Three fixtures are coded once inside the TUs and shared by file-local free functions:

* `countClicks(buffer, sigma)` / `smallestZeroSigma(buffer)` — copied from
  `atmosphere_engine_spectral_test.cpp:399-423`, with **all six `ClickDetectorConfig` fields
  designated-initialised** and `.sampleRate` set to the render rate (the struct default is 44100 and
  `isValid()` only range-checks it, so a wrong rate is used silently).
* `CloudRig` — a `HarmonicCloud` + `BloomEngine` pair with the Phase-10 call shape
  (`seraphis_voice.h:1050-1054`): fill the parent arrays, `bloom.processChunk(r, a, n, 64)`,
  `cloud.setSpectralTarget(r, a, returned)`, `cloud.processStereoBlock(...)`, once per 64-sample
  control chunk.
* `stepEngine(engine, steps)` — the accelerated, cloud-free driver used by SC-003, SC-010 and
  SC-015: a loop of `processChunk(r, a, pc, 64)` over a fixed synthetic parent array.

### S10.2 TU assignment (FR-080's "four new TUs")

| TU | Contents |
|---|---|
| `dsp/tests/unit/systems/bloom_engine_test.cpp` | SC-003, SC-005, SC-006, SC-007, SC-008, SC-012, SC-014, SC-016, SC-017, SC-018, **plus SC-009's range-and-neutral arm** (`BloomEngine_ArgumentContract`: FR-009's clamp and documented-neutral limbs need IEEE semantics nowhere, so they belong in the ordinary TU) |
| `dsp/tests/unit/systems/bloom_engine_spectral_test.cpp` | SC-001, SC-002 `[long]`, SC-004 `[long]`, SC-010 `[long]`, SC-015 `[long]` |
| `dsp/tests/unit/systems/bloom_engine_perf_test.cpp` | SC-011 `[.perf]` + the FR-073 stage probe `[.perf]` |
| `dsp/tests/unit/systems/bloom_engine_nonfinite_test.cpp` | SC-009's **non-finite** arms only (`BloomEngine_NonFiniteGuards` + the probe arm); the **only** TU in the `-fno-fast-math` block, and the only definition of `detail::BloomEngineNonFiniteProbe` |

`[long]` is applied per the roadmap rule: only to cases costing more than ~15 s whose assertions are
toolchain-**independent**. SC-001 is a differential click gate over ~120 s of rendered audio and is
expected to land well under that; it is tagged `[long]` **only if the measured runtime says so** at
implementation time. SC-009 and SC-003 are never tagged — they are the cross-platform sentinels.

### S10.3 Criterion by criterion

| SC | TU | `TEST_CASE` | Assertion strategy, thresholds, seeds |
|---|---|---|---|
| **SC-001** | spectral | `BloomEngine_ChildSpawnAndDeathAreClickFree` | `CloudRig` at richness 1.0 (`activeCount_ == 64`), fades compressed to 2 s / 1 s / 3 s. For each of **10 seeds**, render twice — bloom engaged, and `numChildSlots = 0` as reference. Gate is **differential**: `countClicks(bloom, 5.0) <= countClicks(ref, 5.0)`, plus **0 detections** inside the 200 ms window after every spawn instant and every death instant (the `feedback_ecology_test.cpp:3689-3696` windowed idiom; instants taken from `getSpawnEventCount()` / `getCompletedChildCount()` transitions). On failure report `smallestZeroSigma()` for both runs. |
| **SC-002** | spectral `[long]` | `BloomEngine_LifecycleTimingAndC1Shape` | Explicit overrides `childGain = 1.0f`, `depth = 1.0f`. Parent amplitude swept over `{1.1e-5, 0.01, 0.35, 1.0}`. Sample `getChildAmplitude()` every control chunk at 48 kHz. (a) 50 % of the latched target at `0.5·fadeIn ± 1 %`, 99 % at `>= 0.9·fadeIn`, at the **default 45 s**. (b) mirrored at the default 180 s, ending at **exactly** `0.0f`. (c) `mean(\|d\|)` over the first and last 2 % of each segment `<= 10 %` of `max(\|d\|)` (smoothstep ≈ 6 %, a linear ramp = 100 %), and `\|d²\|` at the FadeIn→Hold and Hold→FadeOut junctions within `3×` the segment-interior median `\|d²\|`. (d) monotone non-decreasing / non-increasing, reaches the target and `0.0f` within the configured durations, **no plateau of identical consecutive samples > 1 s** (S5.3 computes the worst case at 0.67 s). **No minimum-first-difference threshold.** (e) jitter: 200 seeded ≥2-child events, ≥ 90 % have differing `getChildHoldSeconds`, every latched hold inside `holdSeconds·(1 ± 0.5)`; at `holdJitterFraction = 0` every latched hold equals `holdSeconds()` exactly and (a)–(d) are unchanged. **(f) FR-033, setters moved while a child is in flight — the clause is otherwise true only "by construction" and asserted nowhere.** Spawn a child at the 45 s / 120 s / 180 s defaults; at 20 s elapsed call `setFadeInSeconds(300.0f)`, `setHoldSeconds(0.0f)` and `setFadeOutSeconds(600.0f)`. The in-flight child's phase transitions must still land at the **originally latched** step offsets (read through `getChildPhase` and `getChildElapsedSeconds`), `getChildHoldSeconds(i)` must be unchanged, and its amplitude series must stay monotone across the setter instant with **no first-difference outlier** there (the same `3×`-of-interior-median test as (c)); then the **next** spawned child must use the new values. An implementation that recomputed `fadeInSteps` per chunk from the current setter value — the natural mistake, and exactly the derivative discontinuity FR-031 forbids — passes every other clause of every other criterion. |
| **SC-003** | behaviour | `BloomEngine_SlotAccountingInvariantsUnderFuzz` | 1 000 seeded configs over `capacity ∈ [1,64]`, `numChildSlots ∈ [0,16]`, `parentCount ∈ [0,64]`, max spawn rate, max `childrenPerEvent`, 30 simulated minutes on a coarse grid via `stepEngine`. Hard: every write index in `[reserveBase(), capacity())` except the FR-051 gap; `getLiveChildCount() <= numChildSlots()`; no shared slot (tracked by the harness from `getChildSlotIndex`); returned count `<= capacity()`; `getOverlapEngagementCount() == 0` when `parentCount <= reserveBase()`. **Overlap counter, POSITIVE direction (FR-052):** a dedicated arm with `parentCount = capacity()` and `numChildSlots() > 0` runs N engaged calls and asserts `getOverlapEngagementCount() == N` **exactly** (one increment per engaged call, saturating at `kMaxOverlapCount`) and that the returned count is still `capacity()`. Without it an engine whose counter never increments — i.e. that has lost FR-052's only observable — passes, and the Phase-10 integration test told to assert the counter is zero is silently green forever. **Padding, hard, over the WHOLE write region:** the array is poisoned before **every** call (NaN through a volatile sink, `-1.0f`, large garbage); after **every** call, every index in `[parentCount, capacity())` that is **not** held by a live child — cross-checked against `getChildSlotIndex(i)` over the live entries, `getLiveChildCount()` of them — is **exactly** `float(i+1)` and **exactly** `0.0f`. That covers the FR-051 gap `[parentCount, reserveBase())` **and** the unoccupied part of the owned region `[reserveBase(), capacity())`, which no earlier draft asserted anywhere: every array-content clause was scoped to the gap, so an engine that wrote children but never repadded a vacated owned slot passed SC-001, SC-003, SC-004, SC-014, SC-015 and SC-016 unchanged. **FR-032 death arm:** spawn exactly one child, step until `getCompletedChildCount()` increments, and on **that same chunk** assert its former slot reads `float(slot+1)` / `0.0f` — the direct test that a retired child's slot is repadded rather than left sounding forever at its last fade-out value. **Observation = canary *and* snapshot:** an out-of-capacity canary that must be bit-unchanged, **plus** a full pre-call snapshot after which `[0, min(parentCount, reserveBase()))` must be byte-identical. **Sticky arm (Q8):** after all children complete, the next `capacity()-1` calls still return `capacity()` and still pad; `isEngaged()` stays `true`. **Capacity arm (Q3):** 200 runs, prepare at 64/8, spawn, `setCapacity(32)`, 30 s more — no write `>= 32`; every legacy child's `getChildElapsedSeconds` strictly increases with phase transitions at the unshifted latched offsets; no shared slot; then `setCapacity(64)` and assert a legacy-held slot is re-used only after that child completes. **Buffer-precondition arms (S1.4, S14 C-11):** (i) the engine runs against a `kMaxSlots`-float working array embedded in a larger poisoned buffer whose surrounding bytes must be bit-unchanged — the canary is placed where a *short-buffer* caller would be overrun, not only past index 63, because a fixture that always allocates 64 floats cannot model the caller the precondition exists for; (ii) `prepare({.capacity = 16})` followed by `setCapacity(64)` must write indices 16..63, which is the case proving the precondition has to be stated against `kMaxSlots` and **not** against the prepare-time capacity. |
| **SC-004** | spectral `[long]` | `BloomEngine_ThirtyMinuteEvolutionTrajectory` | 30-minute `CloudRig` render at defaults, logging per second: `centroidHz` (`audio_features.h:88`), the count of `i < cloud.getActivePartialCount()` whose `cloud.getPartialCurrentAmplitude(i)` is above −60 dB of the largest such value, and broadband RMS. (a) **differential**: same seed/config rendered twice (bloom on, `numChildSlots = 0` off); `max\|centroid_on − centroid_off\| >= 5 %` of the reference mean, sustained ≥ 60 s after a spawn; the bloom run must reach a partial count the reference never reaches. (b) RMS of the last 5 min within **±1.5 dB** of minutes 5–10; peak sample `< 1.0`; `cloud.stateFinite()` throughout. (c) **pooled over 5 seeds**: `Σ getSpawnEventCount() >= 30` against `λ_total = 37.5` written into the test as a named constant, per-seed counts reported. |
| **SC-005** | behaviour | `BloomEngine_StrongestKParentSelection` | 20 crafted arrays (known ordering, exact ties, sub-threshold slots) + 200 random arrays. Reference ordering is a **`std::stable_sort` over `(amplitude, index)` keyed on `(-amplitude, index)`** — an unqualified `std::partial_sort` is not acceptable. Read the selection through `getLastParentSelectionCount()` / `getLastParentIndex(k)` (S8 A-4). Assert exactly the K largest; ties to the lower index; no slot with amplitude `<= 1e-5f`; no index `>= min(parentCount, reserveBase())`. **FR-013:** after a run spanning many events, `getParentScanCount() == getSpawnEventCount()`. **FR-014 arm (S14 C-10) — FR-014 has no other criterion anywhere:** drive a forced event (`triggerBloom()` + one control step) against (i) a parent array whose every amplitude is at or below `kSilentParentAmplitude` and (ii) a parent array whose every entry is non-finite (bit patterns through a volatile sink); in both, assert `getSpawnEventCount()` advanced by exactly 1, `getParentScanCount()` advanced by exactly 1, `getOfferedChildCount()` **unchanged**, `getRejectedSpawnCount()` **unchanged** (the C-10 counter semantics), `getLiveChildCount() == 0`, `stateFinite()` true, and the array byte-unchanged outside the FR-051 pad regions. |
| **SC-006** | behaviour | `BloomEngine_ChildRatioRelationships` | 500 seeded events. Non-fallback `Octave` within **0.1 cent** of `2×` parent (`getChildParentIndex` gives the parent); non-fallback `Fifth` within 0.1 cent of `1.5×`; `DetunedNeighbour` offset in `[24, 50]` cents; no child within 24 cents of any partial present at spawn **or any sibling of the same event**; no ratio outside `[0.5, 128]`, and such a candidate appears in `getRejectedSpawnCount()` rather than being clamped. **Fallback arm:** every `getIsChildFallback(i) == true` child has relation `Octave`/`Fifth` and lies `[24, 50]` cents off the exact interval. **Path-is-live arm:** 500 further events against an exactly-harmonic parent spectrum (integer ratios) require `getRejectedSpawnCount() > 0` **and** `getFallbackChildCount() > 0`. **Consumer arm:** through a real `HarmonicCloud`, `getPartialFrequencyHz(slot)` equals `fundamentalHz × childRatio` within 0.1 cent and `getPartialTargetAmplitude(slot)` rises monotonically over the fade-in. **Negative control:** repeat with `capacity` set *above* `cloud.getActivePartialCount()` and assert the consumer arm **fails**. **Relation-weight arm (FR-060) — without it every per-relation clause above is a conditional an engine that always returns `Relation::Octave` satisfies vacuously:** with weights `{1,0,0}`, `{0,1,0}` and `{0,0,1}` in turn, over 200 events each, **every** child carries the enabled relation (non-fallback children through `getChildRelation(i)`, fallback children through the original relation the same getter reports); with `{0,0,0}` all three relations appear over 200 events — the spec's "all three relation weights zero falls back to uniform" Edge Case, which had no criterion. **Without-replacement arm (FR-016):** for every event with `getChildrenPerEvent() <= getLastParentSelectionCount()`, that event's children have **pairwise distinct** `getChildParentIndex()` values; for larger events the first `getLastParentSelectionCount()` children do. (`getChildParentIndex` was added as A-4 precisely to make this visible, and nothing asserted it.) **Per-event cap arm (FR-015):** no single event raises `getOfferedChildCount()` by more than `getChildrenPerEvent()`, swept over `childrenPerEvent ∈ {1,2,3,4}`. |
| **SC-007** | behaviour | `BloomEngine_NoAllocationAfterPrepare` | `AllocationScope` around 10 000 `processChunk` calls spanning many spawn/death cycles, plus every setter and `triggerBloom()`. **0 allocations.** `getAllocatedBytes() == 0`. A second scope around `prepare()` itself (this component allocates nothing there either). **FR-074's footprint report lives here, and nowhere else:** a `WARN(sizeof(BloomEngine))` beside the `getAllocatedBytes() == 0` assertion. FR-074 requires the footprint to be *declared, not hidden* — **reported**, never asserted to a byte (S9's ≈ 1.2 KB is an estimate, and pinning it would be a portability trap). Without this row owning it, FR-074 has no TU and no criterion and is dropped at implementation time. |
| **SC-008** | behaviour | `BloomEngine_SeedDeterminism`, `BloomEngine_BlockPartitionInvariance`, `BloomEngine_RngPositionIsStepPure` | (a) two instances, same seed/config/input, 10 simulated minutes → identical child tables (exact on the integer/enum surface) and `compareFingerprints` within tolerance on a `CloudRig` render. **No bit-exact float golden is checked in** (`lint-float-bit-goldens.js`). (b) the same total sample count as `64`, `512`, `2048` and a ragged `{1, 7, 383, 4096, …}` sequence → identical child table and fingerprint-equal output. **Rejected-call arm (FR-005's three unasserted clauses):** one ragged partition is polluted with interleaved `processChunk(nullptr, amplitudes, pc, 512)`, `processChunk(ratios, nullptr, pc, 512)` and `processChunk(ratios, amplitudes, pc, 0)` calls; the resulting child table and fingerprint must be **identical** to the unpolluted run, the return value must be the caller's **unclamped** `parentCount` on both `nullptr` paths, and `getChildElapsedSeconds(i)` must be unchanged across a `numSamples == 0` call. An engine that advanced its control phase on a rejected call breaks FR-006 in a way the unpolluted partitions structurally cannot see, because they never issue one. (c) **restated per S14 C-1**: (i) four instances suppressed for the same prefix by *different* means (dormant / `wake = 0` / `depth = 0` / `spawnRateHz = 0`) then enabled produce **identical** child tables and outputs over the post-enable window, where **the comparison window opens `ceil(kGainRampMs · controlRateHz / 1000)` control steps after the enable instant** (50 ms ≈ 37 steps at 48 kHz) — stated in the criterion as that expression, so an implementer cannot substitute a lucky seed for the window. The window is required because only three of the four re-enable instantly (`setDormant(false)`, `setWake(1)`, `setSpawnRateHz(r)`): `setDepth(1)` re-enables through `depthRamp_`, whose `gate`, and therefore whose Bernoulli `p`, is strictly **lower** than the other three's for the whole ramp, so an event can arm on a step for one instance and not for another (~0.25 % of runs per seed at the max spawn rate the other arms use) and the arm would fail on correct code — the same "cannot pass on correct code" class of defect C-1 was written to remove. The arm asserts `getSmoothedDepth() == 1.0f` **exactly** at the window's open as an explicit precondition, and every instance's child table and event-step list is compared only from that step onward; (ii) an instance awake and spawning throughout arms events at **exactly the same control-step indices** (recorded from `getSpawnEventCount()` transitions) as the suppressed instances do after their enable point. (iii) slot choice consumes no draw: see SC-018 (d). |
| **SC-009** | nonfinite | `BloomEngine_NonFiniteGuards` | `-fno-fast-math` TU only, all non-finite values built from **bit patterns through a volatile sink** (never `std::numeric_limits<float>::quiet_NaN()`). (a) NaN/Inf into every float setter → rejected, previous value stands. (b) NaN/Inf in `ratios[i]`/`amplitudes[i]` → that slot is never a parent and never copied into an owned slot; the engine's own writes stay finite. (c) NaN/Inf sample rate → substituted then floored. After each, `stateFinite()` is true and every written slot is finite, `ratio > 0`, `amplitude >= 0`. **Probe arm:** `detail::BloomEngineNonFiniteProbe` poisons a live `Child::amplitude` and `stateFinite()` must return **false** — the trap is proved to fire. **Range-and-neutral arm — `BloomEngine_ArgumentContract`, in the *behaviour* TU** (`bloom_engine_test.cpp`; it needs no `-fno-fast-math`, only the non-finite arms above do). It covers FR-009's **other two limbs** (S7.6's normative table), which no criterion touched: every float setter is driven past **both** range ends (`setDepth(-1.0f)` / `(2.0f)`, `setSpawnRateHz(-1.0f)` / `(10.0f)`, `setChildGain(-0.5f)` / `(5.0f)`, `setFadeInSeconds(0.0f)` / `(1e6f)`, `setHoldSeconds(-1.0f)` / `(1e6f)`, `setFadeOutSeconds(0.0f)` / `(1e6f)`, `setHoldJitterFraction(-1.0f)` / `(2.0f)`, `setConsumerTiltDb(-99.0f)` / `(99.0f)`, `setWake(-1.0f)` / `(2.0f)`, `setRelationWeight(r, -1.0f)` / `(r, 2.0f)`) and the matching getter must report **exactly the clamp**; every size setter likewise (`setParentCount(0)` and `(99)`, `setChildrenPerEvent(0)` and `(99)`, `setCapacity(0)` and `(99)` — `capacity()` reporting `1` and `kMaxSlots`, and `numChildSlots()` re-deriving per S6.3); and every indexed read is called at `kMaxChildren`, `kMaxParents` and `SIZE_MAX` and must return the S1.4 documented neutrals (`0`, `0.0f`, `Relation::Octave`, `Phase::Idle`, `false`, and `kMaxSlots` for `getLastParentIndex` and for `getChildSlotIndex` on an `Idle` entry), with `setRelationWeight` on an out-of-range `Relation` (cast from `std::uint8_t{7}`) a silent no-op that leaves all three weights unchanged. |
| **SC-010** | spectral `[long]` | `BloomEngine_SampleRateIndependence` | At 44 100 / 48 000 / 88 200 / 96 000 / 192 000 Hz, at `depth = 1` and `wake = 1`. **Event-rate arm** driven with `numChildSlots = 0` (the engine stays disengaged, so the run is a bare clock and costs nothing) over **N = 2 000 events**: mean inter-event time inside `1/spawnRateHz · (1 ± 4/√N)`, written as that expression so the 4.5 σ arithmetic is visible rather than a magic number. **Timing arm:** a child's fade-in duration in **seconds** within ±0.5 % of the configured value at every rate. **Re-prepare arm:** re-preparing mid-lifecycle leaves the exact post-prepare state (no half-faded child, seed retained, counters zeroed). |
| **SC-011** | perf `[.perf]` | `BloomEngine_CpuBudget` | ns per 512-sample block at 48 kHz, worst case (`numChildSlots = 16` all live, `parentCount = 48`, `K = 8`, `childrenPerEvent = 4`, max spawn rate), **best-of-25 × 500 blocks after 400 warm-up**. **Threshold: ≤ 10 667 ns/block** (0.1 % of one core), `static_assert`ed on the checked-in baseline so the ceiling is evaluated on every CI leg. Percent reported, never asserted. RUN ALONE: `node tools/run-cpu-tests.js dsp_systems_tests`. |
| **SC-012** | behaviour | `BloomEngine_CloudContractAssumptions` | `static_assert`s on the four contract constants this spec depends on: `HarmonicCloud::kMaxPartials == 64`, `HarmonicCloud::kControlChunkSamples == 64`, `HarmonicCloud::kTargetAmpEpsilon == 1e-5f`, `EntropyProcessor::kMinRatioSpacingCents == 24.0f` — plus the same-valued `BloomEngine::kMaxSlots`, `kControlChunkSamples`, `kSilentParentAmplitude`, `kMinRatioSpacingCents`. **`sizeof(HarmonicCloud)` is deliberately NOT pinned.** The rest of SC-012 is a compliance checklist item: `git diff --name-only` shows no existing file under `dsp/include/`, and the Seraphis cloud/morph/entropy/voice cases stay green. |
| **SC-013** | — | (lints, not a `TEST_CASE`) | `node tools/lint-odr.js`, `lint-layers.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `lint-simd-aligned-loadstore.js`, `check-portability.js` all clean; the header compiles under g++/libstdc++ (MSYS2 or WSL, the Phase-3 precedent) as well as MSVC; clang-tidy `dsp` target clean. |
| **SC-014** | behaviour | `BloomEngine_DisabledIsBitIdenticalPassThrough` | Three **cold-start** arms, each over 100 000 chunks: (a) `depth = 0` configured **before** `prepare()` (or snapped), with `getSmoothedDepth() == 0.0f` asserted on the first chunk as an explicit precondition; (b) `numChildSlots = 0`; (c) `setDormant(true)`. In each: `processChunk` returns `parentCount`, both arrays `std::memcmp`-unchanged, `isEngaged() == false`, and the `CloudRig` render is bit-identical to one with no `BloomEngine` in the chain. (d) **fractional wake**: `wake ∈ {0, 0.25, 0.5, 1.0}` at a fixed seed and max rate — `getSpawnEventCount()` inside the SC-010 interval of `wake ×` the `wake = 1` count, and **exactly 0** at `wake = 0`; `getDiscardedEventCount()` counts a `triggerBloom()` issued during the `wake = 0` arm, and no spawn follows the subsequent wake (no catch-up burst). **(e) fractional DEPTH, mirroring (d) — FR-042 (b)'s linear scaling of the clock probability had no test at all, and SC-015 runs at a single constant depth:** `depth ∈ {0, 0.25, 0.5, 1.0}` snapped before the run at a fixed seed and max rate, `getSpawnEventCount()` inside the SC-010 interval of `depth ×` the `depth = 1` count, and **exactly 0** at `depth = 0` once `getSmoothedDepth() == 0.0f`. **In-flight arm (the spec's own `1 → 0` Edge Case):** spawn children at `depth = 1`, then `setDepth(0)` — every live child's `getChildTargetAmplitude(i)` is **unchanged** (FR-042 (a) scales the *latched* amplitude, not a live one), `isEngaged()` stays `true`, and the returned count stays `capacity()` until the last child completes. **(f) unclamped return (S3.1):** `parentCount = 64` with `capacity = 32`, disengaged — the returned count is **64**, not 32, and both arrays are `std::memcmp`-unchanged. An engine returning the clamped `pc` here silently truncates the caller's spectrum, and every other arm of this criterion runs with `parentCount <= capacity()` and cannot see it. |
| **SC-015** | spectral `[long]` | `BloomEngine_EightHourAcceleratedSoak` | 8 simulated hours via `stepEngine` (no audio render) at SC-011's worst case, 25 seeds. (a) live-child count within `numChildSlots()`; (b) every emitted amplitude in `[0, getChildTargetAmplitude(i)]` and every target equal to `parentAmp·childGain·depth / tiltGain(slot)` within `1e-6` (at the default `consumerTiltDb = 0`, `tiltGain ≡ 1`). **No absolute `[0,1]` bound.** (c) every emitted ratio in `[0.5, 128]`; (d) `stateFinite()` throughout; (e) `getSpawnEventCount() ≈ spawnRateHz·depth·wake·T` within **±25 %**; `getCompletedChildCount() ≈ min(childrenPerEvent·events, numChildSlots·T/(fadeIn+hold+fadeOut))` within ±25 %, **reporting which branch of the `min` is active**; and the exact identity `getOfferedChildCount() == getSpawnedChildCount() + getRefusedChildCount()` (S14 C-7). (f) no stall: `getLiveChildCount()` does not hold one value for > 60 simulated minutes and `getSpawnEventCount()` advances at least once per 60 simulated minutes. |
| **SC-016** | behaviour | `BloomEngine_ChildrenAreAudibleWithinCloudActiveCount` | Real `HarmonicCloud` at `richness` chosen so `getActivePartialCount() ≈ 32`; `PrepareConfig::capacity = cloud.getActivePartialCount()`. Hard: every live child's slot `< getActivePartialCount()`; `cloud.getPartialTargetAmplitude(slot) > 0` once past the fade-in onset (audible, not merely written); `cloud.hasSpectralTarget()` true after **every** handoff — the direct test that the array was **accepted** rather than wholesale-rejected (`harmonic_cloud.h:812-818`). Repeat with `parentCount ∈ {0, 1, reserveBase()/2}` and a **poisoned** gap so an unpadded implementation fails here. |
| **SC-017** | behaviour | `BloomEngine_TiltCompensationMatchesIntendedLevel` | Cloud at `richness = 1.0` with `setSpectralTiltDb(-6.0f)`, engine with `setConsumerTiltDb(-6.0f)`, `childGain = 1.0f`, `depth = 1.0f` (explicit overrides), children spread across parent and reserved slots. Once a fade-in completes, `cloud.getPartialCurrentAmplitude(childSlot)` within **±0.5 dB** of `parentAmplitude_at_spawn`. **Negative control:** with `setConsumerTiltDb(0.0f)` against a cloud still at `-6.0f`, the measured level must be outside the band by **> 10 dB**. |
| **SC-018** | behaviour | `BloomEngine_OwnedSlotRoundRobinRotation` | 200 spawn/death cycles at `numChildSlots = 8`, **driven by `triggerBloom()`** (S14 C-6 — the internal clock would make the cycle schedule seed-dependent and clause (d) unfalsifiable). (a) all 8 owned slots used within the first 16 cycles; (b) two consecutive cycles never reuse a slot while another is free; (c) with `childrenPerEvent >= 2`, simultaneous children take different slots and the cursor still advances (cross-checked against SC-003's no-shared-slot invariant); (d) the slot-index sequence is identical across two runs with the same seed **and identical to a run with a different seed**. (e) **FR-043, `triggerBloom()` is edge-like — the requirement has a named failure mode and no criterion asserted it:** call `triggerBloom()` **50 times** inside one control chunk (a run of `processChunk` calls whose `numSamples` sum to `< 64`, so no control step runs between them), then advance one control step — `getSpawnEventCount()` must advance by **exactly 1** and `getOfferedChildCount()` by **at most `getChildrenPerEvent()`**. An `armed_` implemented as a counter — the natural bug when the caller polls `SlowEventScheduler::isEventActive()` (`slow_event_scheduler.h:361`) as a level rather than an onset — passes every other clause while producing `n × childrenPerEvent` simultaneous children. |

### S10.4 What the `static_assert`s in SC-012 cost

`bloom_engine_test.cpp` is the **only** place `harmonic_cloud.h`, `entropy_processor.h` and
`bloom_engine.h` meet in one TU. That is deliberate: it is the compile that would fail on an ODR or
namespace-scope collision, and it is where the restated-constant cross-checks belong. The header
itself never includes either (FR-080, D-1, D-2).

---

## S11. Build integration — the exact edits

**(1) `dsp/tests/CMakeLists.txt`, inside the enumerated `dsp_systems_tests` list** — insert after the
Phase-6 block (`:460-463`), before the closing `)` at `:464`:

```cmake
    # Vorago Phase 7 (specs/vorago-phase7-harmonic-bloom): BloomEngine.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   bloom_engine_test.cpp           SC-003, SC-005, SC-006, SC-007, SC-008,
    #                                   SC-012, SC-014, SC-016, SC-017, SC-018
    #   bloom_engine_spectral_test.cpp  SC-001, SC-002, SC-004, SC-010, SC-015
    #                                   (the [long] set)
    #   bloom_engine_perf_test.cpp      SC-011 + the FR-073 stage probe   [.perf]
    #   bloom_engine_nonfinite_test.cpp SC-009 only
    unit/systems/bloom_engine_test.cpp
    unit/systems/bloom_engine_spectral_test.cpp
    unit/systems/bloom_engine_perf_test.cpp
    unit/systems/bloom_engine_nonfinite_test.cpp
```

**(2) `dsp/tests/CMakeLists.txt`, the `-fno-fast-math` block** — insert after the Phase-6 entry
(`:899`), before `PROPERTIES COMPILE_FLAGS` (`:900`):

```cmake
        # Vorago Phase 7: SC-009 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them; it is also the only definition
        # of detail::BloomEngineNonFiniteProbe. ONLY this one of the four Phase 7
        # TUs is listed. bloom_engine_test.cpp and bloom_engine_spectral_test.cpp
        # stay out so the FR-008/FR-009 guards are proved in the /fp:fast +
        # -ffast-math mode the header actually ships in. The perf TU stays out
        # too: -fno-fast-math would change the figures its baselines are pinned to.
        unit/systems/bloom_engine_nonfinite_test.cpp
```

**(3) `dsp/lint_all_headers.cpp`** — insert after the Phase-6 include (`:188`), before the Layer-4
block (`:190`):

```cpp
// Vorago Phase 7 (specs/vorago-phase7-harmonic-bloom), FR-001
#include <krate/dsp/systems/bloom_engine.h>
```

**No other build file changes.** No `tests/test_helpers/CMakeLists.txt` edit (INTERFACE library, no
new helper). No plugin CMake, no `ci.yml`, no clang-tidy script edit — this phase adds no target.

**Commands:**

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# build + run the layer that owns the new TUs
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5

# SC-012's consumer regression (the suites that compile the untouched shared headers)
"$CMAKE" --build build/windows-x64-release --config Release \
    --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_effects_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_effects_tests; do \
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done

# the [long] set, run explicitly (per-push CI excludes it; it runs nightly on all 3 OSes)
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tail -5

# SC-013's gates
node tools/lint-odr.js && node tools/lint-layers.js && node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js && node tools/lint-simd-aligned-loadstore.js
node tools/check-portability.js

# SC-012's byte-unchanged check
git diff --name-only -- dsp/include/

# clang-tidy: single-TU on Windows for a small change set
clang-tidy -p build/windows-ninja dsp/tests/unit/systems/bloom_engine_test.cpp

# perf, ALONE, nothing else running
node tools/run-cpu-tests.js dsp_systems_tests
```

---

## S12. CPU budget — FR-072's ceiling, FR-073's probe

### S12.1 Measurement basis (inherited verbatim)

Nanoseconds per **512-sample block at 48 kHz** — `resonance_drift_network_perf_test.cpp:67-76`: "a
percent-of-core figure is not reproducible across dev machines or CI runners". One block period is
**10 666 667 ns**, so FR-072's 0.1 %-of-one-core ceiling is **10 667 ns/block**. Trial shape:
best-of-25 × 500 blocks after 400 warm-up blocks. Tagged `[.perf]`, excluded by the per-push CI
filter, with the threshold `static_assert`ed against a checked-in baseline so the ceiling is
evaluated on every CI leg even though the case never runs there.

**The stop-and-surface rule (`:57-64`) is inherited verbatim and is non-negotiable:** no implementing
agent may lower `kMaxChildren`, raise the budget, relax a threshold or shrink the workload to make a
figure fit. Reduce cost, never move the line. A miss is put to the user with the measured table, the
route that amended Phase 2 (1 → 1.75 %) and Phase 5 (1 → 1.5 %).

### S12.2 The FR-073 stage probe's arms (WARN-reported, not asserted)

| Arm | Shape | Why it is priced separately |
|---|---|---|
| (a) **child bookkeeping** | 16 live children, `advanceChildren()` only, 8 control steps per block | one `smoothstep` + three compares × 16 × 8 = the per-block lifecycle floor |
| (b) **owned-slot writes** | `applyOutput()` only, `capacity = 64`, `numChildSlots = 16`, `parentCount = 48`, engaged | ≤ 1 152 float stores per block (S3.4) — expected to **dominate**, and the one term a caller can reduce (by calling `processChunk` at 512 instead of 64) |
| (c) **spawn-event scan** | one full `runEvent()` (parent scan + 64 `std::log2` + 4 children × up to 5 candidates) | amortised over 15 000 control steps at max rate; priced alone so the amortisation is visible rather than assumed |
| (d) **clock only** | `numChildSlots = 0` (disengaged), 8 steps/block | one `nextUnipolar()` + one `LinearRamp::process()` per step — the floor a caller pays for having the component in the chain at all |

Expected shape: (b) ≫ (a) ≫ (d) ≫ amortised (c). If the total misses 10 667 ns, (b) is where the
lever is, and the lever is **not** shrinking `kMaxChildren`.

**The dirty-flag lever is UNAVAILABLE, and this statement is normative.** An earlier draft of this
plan named "skip the owned-region rewrite when nothing changed since the last call (a `dirty` flag
set by the control step and by `setCapacity`)" as a pure addition that changes no observable. That
is **false**, and the reason is structural: **the arrays are the caller's, not the engine's.** The
engine cannot know what happened to them between two calls. A Phase-10 voice rewrites the parent
region every chunk before handing it over, and SC-003's own fixture poisons the whole array before
**every** call. Skipping the rewrite would leave poison or stale bytes in the gap and in the owned
region, which violates FR-051's per-chunk write requirement and FR-009 (d)'s guarantee that this
component is never the cause of `setSpectralTarget`'s wholesale rejection
(`harmonic_cloud.h:812-818`). It would fail SC-003's gap and owned-region arms and SC-016's
poisoned-gap arm. **Do not implement it.**

**The levers that do exist**, in order of preference:

1. **Call `processChunk` at a larger `numSamples`.** The write phase costs
   `(capacity - pc) + 2 · numChildSlots()` stores **per call**, and the control-step loop (S3.2)
   makes one call of 512 samples exactly equivalent to eight of 64 (FR-006, asserted by
   SC-008 (b)). Eight calls per block become one and arm (b)'s term drops ~8×. This is a
   **caller-side** change at Phase 10, costs this component nothing, and is already named in
   arm (b).
2. **Narrow `capacity - parentCount`.** The gap loop is pure overhead when the caller's parent
   region is short; a Phase-10 caller that sets `capacity` from `getActivePartialCount()` (FR-050)
   and supplies that many parents shrinks the gap to zero.
3. If a skip is nonetheless wanted, it **must** be gated on an **engine-owned shadow copy** of the
   write region compared with `memcmp` — `capacity` floats of extra state plus a `memcmp` and a
   `memcpy` per call — and that cost must be **measured and priced** against the stores it avoids
   before it is adopted. It is not obviously a win and it is not a free addition.

The stop-and-surface rule (S12.1) still governs: if none of these closes the gap, the measured table
goes to the user. No threshold moves, and `kMaxChildren` does not shrink.

### S12.3 Where the cost is expected to be, so a miss is diagnosable

At the Phase-10 call shape there are 8 `processChunk` calls per 512-sample block. Per call:
`(capacity - parentCount)` + `2 × numChildSlots` stores ≈ 16 + 32 = 48 in the SC-011 worst case,
plus 16 `smoothstep` evaluations on the one call in eight that crosses a control step. That is
~400 stores and ~16 transcendental-free polynomial evaluations per block — roughly two orders below
the ceiling. **If the measured figure is anywhere near 10 667 ns, something structural is wrong
(most likely a per-chunk parent scan violating FR-013), and SC-005's scan-count clause is the test
that names it.**

---

## S13. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R1 | Float accumulation of `elapsedSeconds` drifts a 300 s fade outside SC-010's ±0.5 % | Integer control-step counters; seconds are derived, never accumulated (S5.1, arithmetic shown) |
| R2 | A long fade's per-step amplitude increment lands under a float ulp and stalls (the retracted FR-031 concern) | Computed in S5.3: worst case is a 0.67 s plateau at the 300 s maximum, under SC-002 (d)'s 1 s gate. No step floor, no fade-time clamp |
| R3 | The tilt division overflows the latched target to `Inf` on a pathological parent amplitude (gain reaches ×3993) | S4.3 test 4: a non-finite latched target **rejects the candidate**. Recorded as spec correction C-4 |
| R4 | SC-008 (c) as written compares an instance that spawned during the prefix with one that did not, and cannot pass on correct code | Restated in S10.3 as a suppression-equivalence arm plus an arm-step-index-identity arm. Spec correction C-1 |
| R5 | The 24-cent spacing rule starves `Octave`/`Fifth` children on a harmonic parent spectrum, making the relations dead code | FR-026's bounded retry + final-attempt detuned fallback; SC-006's "path-is-live" arm requires `getFallbackChildCount() > 0` on exactly that fixture |
| R6 | The per-call owned-region rewrite dominates the 0.1 % budget | Priced separately as probe arm (b). The levers are **caller-side**: one `processChunk` per 512-sample block instead of eight (exact by FR-006 / SC-008 (b)), and narrowing `capacity - parentCount`. The dirty-flag skip an earlier draft named is **unavailable** — the arrays are the caller's and the engine cannot know they are unchanged; it would fail SC-003 and SC-016 (S12.2) |
| R7 | Clang rejects narrowing in a positional brace init where MSVC accepts it | `PrepareConfig` documented as designated-initialiser-only; every literal in a brace init carries its `f`/`u` suffix; `node tools/check-portability.js` before every commit |
| R8 | `std::partial_sort` is not stable, so SC-005's tie cases would be arbitrated by the reference rather than by the requirement | The reference is a `std::stable_sort` over `(amplitude, index)` (S10.3, SC-005) |
| R9 | `Child::slot` as `std::uint8_t` silently truncates if `kMaxSlots` ever grows | `static_assert(kMaxSlots <= 255, …)` beside the constant |
| R10 | Denormal amplitudes at the end of a 180 s fade | The smoothstep reaches **exactly** `0.0f`, not asymptotically; `detail::flushDenormal` applied on emit — **written into the normative code**, `amplitudes[s] = detail::flushDenormal(ch.amplitude);` in S3.4, not left as prose in S7.3; library-wide FTZ/DAZ in `dsp_test_main.cpp` |
| R11 | MSVC / GCC / Apple Clang disagree on `std::log2`/`std::exp2` in the last bits | No bit-exact golden anywhere (`lint-float-bit-goldens.js`); every comparison is a measured tolerance or `compareFingerprints`. SC-017's ±0.5 dB band has ~7 orders of margin |
| R12 | `std::isnan` slips into the header or a TU under `-ffast-math` | FR-008 forbids it; `tools/lint-nonfinite-symbols.js` enforces it; SC-009's injections are built from bit patterns through a volatile sink (`reference_fastmath_nan_in_tests`) |
| R13 | A future reader assumes `BloomEngine` is related to `AetherReverb`'s / `SeraphisEngine`'s / `SpectralMorphEngine`'s "bloom" | The three near-name families are recorded in S0 and in the header banner, with the one-sentence statement of what each actually is |
| R14 | A Phase-10 caller sets `capacity` from `kMaxPartials` instead of `getActivePartialCount()`, silencing every child | SC-016 is the tripwire, and the `PrepareConfig::capacity` doc comment states the contract in those words. `getOverlapEngagementCount()` is **not** a tripwire for this (spec A-2's own correction) |

---

## S14. Spec corrections and open items

Where the spec and the code (or the arithmetic) disagree, the code wins. Eleven items; **none
relaxes a threshold.** Two (**C-1**, **C-6**) make a criterion *able to fail* where as written it
could not pass on correct code, and two (**C-10**, **C-11**) close a gap where a spec contract had
no test and, in C-11's case, no statement at all.

* **C-1 — SC-008 (c) is unsatisfiable as written.** It compares an instance awake for the whole run
  (which spawns during the prefix, taking slots, moving the cursor and creating children) against one
  dormant for the prefix, and requires fingerprint-equal output after the wake. Those two instances
  differ in *state*, not in RNG position, so the criterion fails on a perfectly correct engine. It is
  restated in S10.3 as (i) an equivalence arm across the four suppression means and (ii) an
  arm-step-index-identity arm against the always-awake instance — which is the property FR-041
  actually claims, and it is assertable through `getSpawnEventCount()` transitions.
* **C-2 — FR-041 and FR-035 state different per-step probabilities.** FR-041 gives
  `rateHz · chunk / fs`; FR-035 and FR-042 give `rateHz · depth · wake · chunk / fs`. SC-014 (d) and
  SC-015 (e) both require the second. Resolved in S3.3: one `gate = dormant ? 0 : wake · smoothedDepth`
  owns it, and FR-041's "pure function of elapsed control steps" is preserved by drawing
  unconditionally **before** the probability is computed (and by S1.6's counter-based event stream).
* **C-3 — the spec never says whether a discarded event is observable.** S8 addition **A-1**
  (`getDiscardedEventCount()`) makes Q7's rule assertable; a discarded event increments **no** other
  counter, so SC-014 (d)'s "exactly 0 at `wake = 0`" holds for `getSpawnEventCount()` trivially.
* **C-4 — FR-023's latched target can overflow to `Inf`.** `tiltGain` reaches ×3993 at
  `consumerTiltDb = -12` and slot 63, and FR-023 deliberately does not clamp the target. A non-finite
  target would violate FR-009 (d). S4.3 test 4 rejects such a candidate (counted in
  `getRejectedSpawnCount()`), which is the same "reject, never clamp" response FR-021 mandates.
* **C-5 — FR-070's "streams seeded through `deriveStreamSeed`" is kept, but the event stream is
  counter-based.** Re-seeding `eventRng_` per event from the control-step index is what makes FR-041's
  stated purity hold for the *spawn* draws and not only for the Bernoulli draw (S1.6). Recorded
  because it is a visible deviation from the persistent-per-lane-stream shape of Phases 2/3/5/6.
* **C-6 — SC-018 (d) needs a deterministic spawn schedule.** "The sequence of slot indices is
  identical … to a run with a different seed" is false if the *cycles themselves* are scheduled by the
  seeded Bernoulli clock, because a different seed arms at different steps. The fixture must drive
  spawns with `triggerBloom()` (S10.3). Without this the clause either fails on correct code or is
  passed by accident.
* **C-7 — SC-015 (e)'s accounting identity does not close.** FR-026 counts every *attempt*, so
  `getSpawnedChildCount() + getRejectedSpawnCount()` exceeds the number of offered children by up to
  `kMaxSpawnAttempts - 1` per child. The assertable identity is
  `getOfferedChildCount() == getSpawnedChildCount() + getRefusedChildCount()` (S8 additions A-2, A-3),
  with `getRejectedSpawnCount()` retained unchanged as the per-attempt diagnostic.
* **C-8 — spacing is checked against ratios that will actually be emitted.** A legacy child outside
  `[reserveBase(), capacity())` after an FR-055 shrink is never written into the array, so it is
  excluded from the FR-022 spacing set — but **not** from slot occupancy. The spec does not address
  the interaction; S4.3 states the rule.
* **C-9 — `PrepareConfig::numChildSlots` must be stored as a *request*.** FR-050 clamps it to
  `min(kMaxChildren, capacity)` at prepare, and FR-055 lets capacity change afterwards. Overwriting
  the stored value on a shrink would make a shrink/grow pair lossy, contradicting FR-055's
  "growth takes effect immediately". `numChildSlots()` therefore re-derives
  `min(requestedChildSlots_, capacity_)` on every read (S2.1 step 3, S6.3).

* **C-10 — FR-061 lists FR-014 among `getRejectedSpawnCount()`'s contributors, and it cannot be.**
  FR-061 defines the counter as "FR-014 + FR-021 + FR-022 + FR-025 + every failed FR-026 attempt
  combined" (`spec.md:652-653`). But FR-014's case is an event with **no eligible parent**: no
  candidate ratio is ever formed, no child is ever offered, and there is nothing to reject. Counting
  it would make the counter's unit ambiguous — rejected *candidates* plus consumed *events* — and
  would corrupt SC-015 (e)'s FR-025 arithmetic. **Resolution, normative (S4.1):** an FR-014 event
  increments `getSpawnEventCount()` and `getParentScanCount()` **only**, and FR-061's list drops
  FR-014. FR-014 also had **no criterion at all**; SC-005 gains the FR-014 arm that asserts exactly
  these counter movements against an all-silent and an all-non-finite parent array (S10.3).
* **C-11 — neither the spec nor an earlier draft of this plan states a buffer-length precondition,
  and the signature carries none.** `processChunk(float*, float*, std::size_t parentCount,
  std::size_t numSamples)` is copied from `EntropyProcessor::processChunk`
  (`entropy_processor.h:269`), which `spec.md:36-41` explicitly presents as the call-shape model —
  but that component's `count` is documented as "Number of valid entries, clamped to kPartials"
  (`:266`) and it touches only `[0, min(count, kPartials))` (`:296`). **This** component writes
  **above** `parentCount` (the FR-051 gap and the whole owned region), and `setCapacity()` (FR-055)
  can **raise** that ceiling from the control thread after `prepare()`, through a call the caller
  cannot correlate with its buffer length — so even a caller that sized its arrays from
  `PrepareConfig::capacity` can be overrun later. A Phase-10 caller reusing the Entropy call shape
  hands over a `parentCount`-sized buffer and takes an out-of-bounds write **on the audio thread**.
  **Resolution, normative:** the precondition is stated against `kMaxSlots` (64), **not** against
  `capacity()`, in the `processChunk` doxygen (S1.4), repeated on `PrepareConfig::capacity`,
  restated as step (0) of the guard ladder (S3.1), and tested by SC-003's two buffer-precondition
  arms — the short-buffer canary and the `prepare({.capacity = 16})` → `setCapacity(64)` growth
  case. (SC-003's pre-existing out-of-capacity canary cannot catch this: its fixture allocates a
  full 64-float array, so the canary sits past index 63 and a short-buffer caller is never
  modelled.)

**Open items carried from the spec, unchanged and not re-opened here:** OQ-A (the 0.1 % ceiling is
this spec's reading of "≈ free" — if FR-073's probe says it is unreachable, the stop-and-surface rule
puts the measured table to the user rather than moving the line) and OQ-B (the dormancy reading for a
chain-less component; a future phase wanting a dormancy edge to kill live children is a spec
amendment with an audible-consequence justification, not an implementation choice).

---

## S15. Suggested task order (`tasks.md` owns the real breakdown)

1. **T-perf-first.** `bloom_engine_perf_test.cpp` with the FR-073 stage probe, written against the
   *shapes* of S12.2 using stand-ins (a raw 16-entry array walk, a 48-float store loop) — so the
   budget's dominant term is known **before** the component exists, the Phase-3 pattern.
2. Header skeleton: banner, includes, constants, `Relation`/`Phase`/`PrepareConfig`/`Child`, salt
   table, `prepare`/`reset`/`setSeed`, the full setter/getter surface with FR-009 semantics, and
   `processChunk` returning `parentCount` (an explicit pass-through). Lints green at this point.
3. SC-012's contract `static_assert`s + SC-013's lint pass — the cheapest gates, run first.
4. The control grid (S3.2/S3.3) and the FR-051 write phase (S3.4) → SC-003, SC-014, SC-007.
5. The lifecycle clock and envelope (S5) → SC-002, and SC-001's click gate.
6. The spawn event (S4) → SC-005, SC-006, SC-018.
7. Dormancy/wake/depth/capacity (S6) → SC-014 (d), SC-003's capacity arm, SC-008 (c).
8. The cloud-facing criteria (SC-016, SC-017) — they need the whole chain and a real
   `HarmonicCloud`, and SC-016's negative control is the one that proves Overview fact 1 is real.
9. `bloom_engine_nonfinite_test.cpp` + the probe (SC-009).
10. The `[long]` set (SC-004, SC-010, SC-015), then the perf run **alone**, then the compliance pass.
