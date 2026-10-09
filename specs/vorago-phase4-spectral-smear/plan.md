# Implementation Plan: Vorago Phase 4 — Spectral Smear

**Spec:** `specs/vorago-phase4-spectral-smear/spec.md` (1299 lines, read in full this session)
**Roadmap:** `specs/Vorago-roadmap.md` Part A → Phase 4 (lines 243–259); reuse-inventory row L4
(line 112); ODR note (lines 127–129); cross-cutting constraints (lines 490–511)
**Deliverable:** one new Layer 2 header `dsp/include/krate/dsp/processors/spectral_smear.h`, four new
test TUs, one new test-helper header `tests/test_helpers/spectral_flux.h`, two edits in
`dsp/tests/CMakeLists.txt`, one enumerated-include edit in `dsp/lint_all_headers.cpp`.
**Already done in this stage:** the roadmap amendment Clarifications Q4 assigns to the plan stage is
**applied** — `specs/Vorago-roadmap.md:257-259` now reads "spectral-flatness increase monotonic with
decoherence amount; per-bin magnitude flux reduction monotonic with smear amount; …". The edit is
exactly three lines replacing three lines, so every roadmap line number cited by the spec is
unchanged. **That edit is outside FR-071's enumerated diff list and must be added to it** — it is the
second of the two FR-071 gaps recorded in **S17 C-1** (the first is the new flux helper).
**Test targets:** `dsp_processors_tests` (all four new TUs). Regression set that must stay green
(FR-072): `dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`,
`dsp_core_tests`, `seraphis_tests`, `innexus_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11.

---

## S0. Verification ledger — every signature below was read this session

Nothing in this plan is quoted from the spec without re-opening the header. Where the spec and the
code disagree, the code wins and the disagreement is recorded in **S17**.

| Claim this plan is built on | Verified at |
|---|---|
| `STFT::prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | `primitives/stft.h:58-83` |
| `STFT::reset()`, `pushSamples(const float*, size_t)`, `canAnalyze()`, `analyze(SpectralBuffer&)`, `fftSize()`, `hopSize()`, `latency()`, `isPrepared()` | `stft.h:90`, `:104`, `:134`, `:144`, `:178`, `:179`, `:183`, `:185` |
| `canAnalyze()` is **`samplesAvailable_ >= fftSize_`** — the first frame needs a full `fftSize`; `analyze()` consumes only `hopSize_` (`samplesAvailable_ -= hopSize_`) | `stft.h:137`, `:171` |
| `pushSamples` has **no overflow guard**; the ring is `inputBuffer_.resize(fftSize * 8)` | `stft.h:104-124`, `:78` |
| `analyze()` reads the **oldest `fftSize` samples** (`readIdx = (writeIndex_ + bufSize - samplesAvailable_) % bufSize`) | `stft.h:150-166` |
| `OverlapAdd::prepare(size_t, size_t, WindowType, float kaiserBeta, bool applySynthesisWindow = false)`; header contract: synthesis window *"Required for spectral modification processors … at >=75% overlap where Hann² satisfies COLA. Must NOT be used at 50% overlap"* | `stft.h:229-271`, contract at `:224-227` |
| `OverlapAdd::synthesize` accumulates at **offset 0 unconditionally**; the hop offset comes only from `pullSamples` shifting left | `stft.h:289-315` (`outputBuffer_[i] +=`), `:340-357` |
| `pullSamples` returns **silently** when `numSamples > samplesReady_` **or** `> outputBuffer_.size()`, without zeroing the destination | `stft.h:329-342` |
| COLA normalisation computed once in `prepare()` over `numOverlaps = (fftSize + hop - 1)/hop` window (or window²) taps | `stft.h:249-262` |
| `OverlapAdd::reset()`, `samplesAvailable()`, `isPrepared()`, `outputBuffer_.resize(fftSize*2)`, `ifftBuffer_.resize(fftSize)` | `stft.h:277`, `:319`, `:368`, `:265`, `:268` |
| `SpectralBuffer::prepare(size_t fftSize)` → `numBins_ = fftSize/2+1`, resizes `data_`/`mags_`/`phases_`; `reset()`; `getMagnitude/getPhase/setMagnitude/setPhase(size_t[,float])`; `numBins()`; `data()`; `isPrepared()` | `primitives/spectral_buffer.h:61-67`, `:71`, `:84`, `:91`, `:98`, `:106`, `:166`, `:148/:156`, `:169` |
| out-of-range bin: getters return `0.0f`, setters return silently — no bounds branch needed in the per-bin loops | `spectral_buffer.h:85`, `:92`, `:99`, `:107` |
| lazy dual representation: `ensurePolarValid()` / `ensureCartesianValid()` are **bulk** SIMD calls fired at most once per frame per representation boundary — so writing magnitude **and** phase costs the same two conversions as writing phase alone | `spectral_buffer.h:7-12`, `:179-186`, `:190-198` |
| `computePolarBulk` / `reconstructCartesianBulk` are Highway-dispatched Layer 0 bulk calls consumed only through `SpectralBuffer` | `core/spectral_simd.h:37`, `:46` (via `spectral_buffer.h:181`, `:194`) |
| `FFT` bounds: `kMinFFTSize = 256`, `kMaxFFTSize = 8192` — **namespace-scope** `inline constexpr`, not class statics | `primitives/fft.h:44`, `:47` |
| `Xorshift32::nextFloat()` is **bipolar `[-1,+1]`**; `seed(0)` silently substitutes `2463534242u` | `core/random.h:59-63`, `:72-74`, `:83` |
| `constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` — lowbias32 finaliser, guaranteed non-zero result | `core/random.h:102-113` |
| `OnePoleSmoother::configure(float smoothTimeMs, float sampleRate)`, `setTarget` (`ITERUM_NOINLINE`, NaN→0, Inf→±1e10), `getCurrentValue`, `process()`, `isComplete()`, `advanceSamples(size_t)`, `snapToTarget()`, `snapTo(float)` | `primitives/smoother.h:160`, `:170`, `:191`, `:197`, `:232`, `:243`, `:257`, `:263` |
| `process()` **snaps on completion**: `if (std::abs(current_ - target_) < kCompletionThreshold) { current_ = target_; return current_; }` — this is what makes an exactly-`0.0f` settled value reachable and stable (FR-021/FR-041's gates) | `smoother.h:197-201`, `kCompletionThreshold = 0.0001f` at `:53` |
| `advanceSamples` costs one `std::pow` unless converged — **not used by this component** (FR-035) | `smoother.h:243-255` |
| `calculateOnePolCoefficient(ms, sr) = exp(-5000/(clamp(ms,0.1,1000) * sr))` — the exact coefficient SC-017 predicts against | `smoother.h:77-93` — declaration `:77`, clamp `:82-84`, `exponent = -5000.0f / (clampedTime * sampleRate)` `:91`, bounds `kMinSmoothingTimeMs = 0.1f` `:58` / `kMaxSmoothingTimeMs = 1000.0f` `:61`. **Not `:196-211`** — that range is `OnePoleSmoother::process()` and has its own row above; the two were conflated in revision 1 |
| `detail::isNaN(float)` `:99`, `isFinite(float)` `:118`, `isFinite(double)` `:125`, `isInf(float)` `:260`, `flushDenormal(float)` `:245` — all read bits through `opaqueFloatBits`, fast-math-immune | `core/db_utils.h` |
| `Interpolation::cubicHermiteInterpolate(float ym1, float y0, float y1, float y2, float t)` — Catmull-Rom, returns `y0` exactly at `t == 0` | `core/interpolation.h:84-100` |
| `kMaxAudioFreqHz = 20000.0f` | `core/audio_constants.h:25` |
| `kPi = 3.14159265358979323846f` | `core/math_constants.h:28` |
| `Window::generateHann` is the **periodic** variant, `0.5 - 0.5*cos(2πn/N)`, so `window[0] == 0.0f` exactly | `core/window_functions.h:111-121` |
| `AtmosphereEngine::pumpBlur(std::size_t)` — the normative loop order: push both channels → `while (blurStft_[0].canAnalyze())` → advance the smoother **once, outside the channel loop** → `for ch in {0,1}` analyze / perturb / synthesize / `pullSamples(scratch, hop)` / write FIFO → advance shared cursors → pop `numSamples` | `systems/atmosphere_engine.h:2303-2392`, banner at `:2266-2302` |
| Atmosphere's perturbation law and its **magnitudes-never-written** statement | `atmosphere_engine.h:2344-2346`, `:2316` |
| Atmosphere's exact-identity skip **burns the same per-bin draws** at a settled `0.0f` | `atmosphere_engine.h:2332-2342` |
| Atmosphere skips DC and Nyquist (`for (k = 1; k + 1 < numBins; ++k)`) because their phase is not free in a real spectrum | `atmosphere_engine.h:2325-2328`, loop at `:2333`/`:2338` |
| Atmosphere's FIFO: shared power-of-two cursors, capacity `std::bit_ceil(blurFftSize_ + max(maxBlockSamples_, kControlChunkSamples) + blurHopSize_)`, `assert(blurHopSize_ * 4 == blurFftSize_)` | `atmosphere_engine.h:461-470`, assert at `:458-459` |
| Atmosphere's latency switch: `return blurEnabled_ ? blurFftSize_ : 0;` | `atmosphere_engine.h:1129` |
| Atmosphere **latches** on a non-finite frame accumulator (`chunkPoisoned_` → `silence()`, "no auto-resume: reset() … is the one documented recovery") | `atmosphere_engine.h:2244-2260` |
| `AetherReverb` spectral stage: geometry "MANDATORY", coherence make-up derivation, the five knots, and the warm-up-counter rule with its measured 960/1020/1022 offsets | `effects/aether_reverb.h:605-660` |
| `static constexpr std::size_t kCoherenceKnotCount = 5; static constexpr float kCoherenceMakeup[5] = {1.0000f, 1.0799f, 1.3435f, 1.7746f, 1.9996f};` | `aether_reverb.h:2774-2776` |
| `[[nodiscard]] static float coherenceMakeup(float amount) noexcept` — clamp → scale by `kCoherenceKnotCount-1` → floor → end-tangent clamp → `cubicHermiteInterpolate` | `aether_reverb.h:4019-4035` |
| AetherReverb applies `g` as a **scalar on the pulled time-domain samples**, one constant per hop: `const float g = coherenceMakeup(amount); for (i < hop) dstL[i] *= g;` | `aether_reverb.h:4097-4102` |
| AetherReverb's warm-up drain: `if (spectralWarmupRemaining_ > 0u) { --…; out = 0.0f; } else if (count > 0) {pop} else {0}` | `aether_reverb.h:4135-4146` |
| `SpectralGate` shape: prepare-time clamp + power-of-two snap, `kMinFFTSize/kMaxFFTSize/kDefaultFFTSize = 256/4096/1024`, `kSmoothingTimeMs = 50.0f`, `frameRate_ = sampleRate / hopSize_`, smoothers `configure(kSmoothingTimeMs, frameRate_)` + `snapTo` | `processors/spectral_gate.h:96-98`, `:129`, `:149-160`, `:166`, `:184-187` |
| `SpectralGate` runs **hop = fftSize/2 with no synthesis window** — legal for it (magnitude-only scaling), explicitly illegal here | `spectral_gate.h:170-174` |
| Vorago house conventions: `kControlChunkSamples = 64`, `kGainRampMs = 50.0f`, `kOutputClamp = 4.0f`, self-reported `getAllocatedBytes()`, clamp counter, "out-of-range read returns the documented neutral" | `systems/noise_organism.h:150`, `:178`, `:180`, `:999`, `:992`, `:270-277`; `systems/resonance_drift_network.h:143-144`, `:912` |
| perf idiom: ns per 512-sample block @ 48 kHz, `bestTrialNs` = best-of-25 × 500 blocks after 400 warm-up, `static_assert(kBaseline * 1.5 <= kReference)` plus an anti-no-op floor, `[.perf]` tag, provenance comment | `dsp/tests/unit/processors/vorago_p1_perf_test.cpp:76-110`, `:84-97`; `dsp/tests/unit/systems/noise_organism_perf_test.cpp:262-300` |
| measured anchor: Atmosphere's blur stage (stereo STFT↔OverlapAdd, fftSize 1024, 75 %, full phase randomisation) ≈ **23 000 ns/block**, corroborated by the `(b) − (a)` baseline difference 111 815 − 86 305 = 25 510 ns | `atmosphere_engine_perf_test.cpp:376`, `:241-242` |
| `calculateSpectralFlatness(const float*, size_t n, float sampleRate)` picks `fftSize` ≤ **4096** and windows **`signal[0 .. fftSize)`** — it measures at most 85 ms **from the start** of the span it is handed | `tests/test_helpers/signal_metrics.h:326-352` |
| `render_fingerprint.h`: `kSampleTolerance = 5.0e-4f` `:58`, `kMetricTolerance = 2.5e-4` `:61`, `fingerprintRender(std::span<const float>)` `:72`, `compareFingerprints(actual, reference, metricTol, sampleTol)` `:122` | `tests/test_helpers/render_fingerprint.h` |
| `AllocationScope` latches its count **in the destructor**; read it inside the scope via `AllocationDetector::instance().getAllocationCount()` | `tests/test_helpers/allocation_detector.h:111-131`, note at `:144-147` |
| the global `operator new/delete` replacements live in `allocation_operator_overrides.h` and must be included from **exactly one TU per binary**; for `dsp_processors_tests` that TU is `brownian_drift_test.cpp:28` | `tests/test_helpers/allocation_operator_overrides.h:1-10`; sweep this session found only 3 includers repo-wide (`brownian_drift_test.cpp:28`, `selectable_oscillator_test.cpp:388`, `aether_reverb_test.cpp:38`) |
| `ClickDetectorConfig{sampleRate, frameSize, hopSize, detectionThreshold = 5.0f, energyThresholdDb, mergeGap}`; `ClickDetector::prepare()`; `std::vector<ClickDetection> detect(const float*, size_t)` | `tests/test_helpers/artifact_detection.h:38-64`, `:105`, `:130` |
| `test_helpers` is an **INTERFACE** library whose include dir is `tests/test_helpers/` — a new header there needs **no** CMake edit | `tests/test_helpers/CMakeLists.txt:7-12` |
| FTZ/DAZ is enabled process-wide in every DSP test binary | `tests/test_helpers/enable_ftz_daz.h:27-32`, called from `dsp/tests/dsp_test_main.cpp` |
| `dsp_processors_tests` source list is **enumerated**, opens `:156`, closes `:294`; Vorago Phase 1 TUs at `:288-293`; `target_link_libraries` includes `test_helpers` at `:300` | `dsp/tests/CMakeLists.txt` |
| the single `-fno-fast-math -fno-finite-math-only` block: guard `:510`, `set_source_files_properties(` `:511`, last entry `unit/systems/resonance_drift_network_nonfinite_test.cpp` `:820`, `PROPERTIES` `:821` | `dsp/tests/CMakeLists.txt` |
| `dsp/lint_all_headers.cpp` is an enumerated include list; the processors block runs alphabetically and `spectral_morph_filter.h` `:137` / `spectral_tilt.h` `:138` are adjacent | `dsp/lint_all_headers.cpp:135-139` |
| lint tooling present | `tools/`: `lint-layers.js`, `lint-odr.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `lint-simd-aligned-loadstore.js`, `lint-allocation-operator-overrides.js`, `check-portability.js` |
| ODR sweeps re-run this session: `grep -rn "SpectralSmear" dsp/ plugins/ tools/` → 0 hits; `class SmearStage` / `class BinSmear` / `class SpectralFog` → 0 hits; `dsp/include/krate/dsp/processors/spectral_smear.h` does not exist | this session |

---

## S1. Component: `SpectralSmear`

### S1.1 Header, layer, includes (FR-001)

`dsp/include/krate/dsp/processors/spectral_smear.h`, **Layer 2 (Processors)**, header-only,
`namespace Krate::DSP`, banner `// Layer 2: DSP Processor - Spectral Smear` matching
`processors/spectral_gate.h`'s banner form.

```cpp
#pragma once

#include <krate/dsp/core/audio_constants.h>   // L0 kMaxAudioFreqHz
#include <krate/dsp/core/db_utils.h>          // L0 detail::isFinite / isNaN / isInf
#include <krate/dsp/core/interpolation.h>     // L0 Interpolation::cubicHermiteInterpolate
#include <krate/dsp/core/math_constants.h>    // L0 kPi
#include <krate/dsp/core/random.h>            // L0 Xorshift32, deriveStreamSeed
#include <krate/dsp/core/window_functions.h>  // L0 WindowType (named in prepare calls)
#include <krate/dsp/primitives/smoother.h>    // L1 OnePoleSmoother
#include <krate/dsp/primitives/spectral_buffer.h>  // L1
#include <krate/dsp/primitives/stft.h>             // L1 STFT, OverlapAdd

#include <algorithm>
#include <array>
#include <bit>        // std::bit_floor, std::bit_ceil
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
```

Every include reaches **down only** (Layers 0–1). No Layer 3/4 header, so `tools/lint-layers.js`
passes with no justification comment. `stft.h` already pulls `fft.h` and `spectral_buffer.h`; both
are named explicitly anyway so a future reorder cannot silently remove them.

**Naming hazard, recorded once:** `kMinFFTSize` / `kMaxFFTSize` are **namespace-scope** constants in
`Krate::DSP` (`fft.h:44`, `:47`). This component's own bounds are spelled `kMinFftSize` /
`kMaxFftSize` (lower-case `ft`), are **class statics**, and never shadow the namespace ones. Inside a
member function an unqualified `kMaxFFTSize` would silently resolve to the namespace constant 8192 —
so the component must never write that spelling.

### S1.2 Constants (all `static constexpr`, class scope)

```cpp
// --- Geometry (FR-010, FR-011) ---
static constexpr std::size_t kMinFftSize     = 512;   // NOT 256: see FR-011 / D-3
static constexpr std::size_t kMaxFftSize     = 4096;
static constexpr std::size_t kDefaultFftSize = 2048;
static constexpr std::size_t kOverlapFactor  = 4;     // hop = fftSize / 4 (75 %)
static_assert(kMinFftSize >= kMinFFTSize && kMaxFftSize <= kMaxFFTSize,
              "geometry must sit inside the FFT primitive's own bounds (fft.h:44,:47)");
static_assert(kMinFftSize % kOverlapFactor == 0, "hop must divide the smallest geometry");

// --- Render chunking (FR-012) ---
static constexpr std::size_t kProcessChunkSamples = 64;   // atmosphere_engine.h:271

// --- Magnitude integrator (FR-020..FR-024) ---
static constexpr float kMaxPole       = 0.99999f;   // defensive backstop, FR-023
static constexpr float kDenormalFloor = 1.0e-20f;   // FR-024, ordered compare

// --- Time-constant law (FR-030..FR-032) ---
static constexpr float kTauAnchorLowHz   = 20.0f;
static constexpr float kTauAnchorHighHz  = kMaxAudioFreqHz;   // 20 000, audio_constants.h:25
static constexpr float kTiltPivotHz      = 1000.0f;
static constexpr float kTiltExponentRange = 0.75f;
static constexpr float kMinSmearSeconds  = 0.02f;
static constexpr float kMaxSmearSeconds  = 10.0f;
static constexpr float kDefaultSmearTimeLow  = 3.0f;
static constexpr float kDefaultSmearTimeHigh = 0.25f;

// --- Control surface (FR-035, FR-050) ---
static constexpr float kControlSmoothMs   = 50.0f;   // atmosphere_engine.h:282
static constexpr float kDefaultSmearAmount = 0.0f;
static constexpr float kDefaultDecoherence = 0.0f;
static constexpr float kDefaultSmearTilt   = 0.0f;

// --- Safety (FR-061) ---
static constexpr float kOutputClamp = 4.0f;          // noise_organism.h:180

// --- RNG salts (FR-044) --- APPEND ONLY: renumbering rewrites every render.
static constexpr std::size_t kSaltDecohereL = 0x1000;
static constexpr std::size_t kSaltDecohereR = 0x2000;
static_assert(kSaltDecohereL != kSaltDecohereR, "the two channels must not share a stream");
static constexpr std::uint32_t kDefaultSeed = 0x5EED0004u;

// --- Coherence make-up (FR-042), PUBLIC because SC-006 must divide it out ---
static constexpr std::size_t kCoherenceKnotCount = 5;
static constexpr float kCoherenceMakeup[kCoherenceKnotCount] =
    {1.0000f, 1.0799f, 1.3435f, 1.7746f, 1.9996f};   // transcribed aether_reverb.h:2775; SC-006 re-measures
```

### S1.3 `PrepareConfig` (FR-002, FR-019)

```cpp
/// Callers MUST use designated initialisers - PrepareConfig{.fftSize = 1024, .enabled = true} -
/// so no narrowing conversion can hide in a positional brace init (Clang errors where MSVC does not).
struct PrepareConfig {
    std::size_t fftSize = kDefaultFftSize;  ///< Clamped to [512, 4096], then bit_floor'd (FR-011).
    /// DEFAULTS TO false, DELIBERATELY DIVERGING FROM AtmosphereEngine::PrepareConfig::blurEnabled_
    /// (= true, atmosphere_engine.h:371). That stage lives inside a voice layer the owner already
    /// chose; this one sits on the GLOBAL bus, where a Phase-10 owner who omits the field would
    /// otherwise silently buy 42.7 ms of latency and ~0.2 % of a core for a stage doing nothing
    /// (FR-019, D-11, Clarifications Q6). Fog is opt-in.
    bool enabled = false;
};
```

### S1.4 Public API (complete, the shape the implementer types)

```cpp
class SpectralSmear {
public:
    SpectralSmear() noexcept = default;
    ~SpectralSmear() noexcept = default;
    SpectralSmear(const SpectralSmear&) = delete;
    SpectralSmear& operator=(const SpectralSmear&) = delete;
    SpectralSmear(SpectralSmear&&) noexcept = default;
    SpectralSmear& operator=(SpectralSmear&&) noexcept = default;

    // --- Lifecycle -----------------------------------------------------------
    void prepare(double sampleRate, const PrepareConfig& config) noexcept;   // the ONLY allocator
    void reset() noexcept;                                                   // audio state only
    void setSeed(std::uint32_t seed) noexcept;

    // --- Render --------------------------------------------------------------
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept;

    // --- Control (FR-050) ----------------------------------------------------
    void setSmearAmount(float amount) noexcept;      // [0,1],      default 0
    void setDecoherence(float amount) noexcept;      // [0,1],      default 0
    void setSmearTilt(float tilt) noexcept;          // [-1,+1],    default 0
    /// @note These two setters are ALLOCATION-FREE but NOT free: each marks the pole tables
    ///       dirty, and the deferred rebuild costs `3*numBins` `exp` + `3*numBins` `pow` +
    ///       `numBins` `log` (S4.3) — ~14 000 transcendentals at `fftSize = 4096`. The rebuild
    ///       is DEFERRED to the top of the next `processBlock` (S4.3, S5.1) so that cost lands
    ///       inside the path SC-013 measures and is paid at most once per block however many
    ///       times these are called. FR-006 puts setters on the SAME thread as processBlock, so
    ///       there is no control thread to hide this on. Prefer patch-load cadence; per-block
    ///       calls are legal (Edge Cases) and SC-013 (d) is the configuration that measures them.
    void setSmearTimeLow(float seconds) noexcept;    // [0.02,10],  default 3.00
    void setSmearTimeHigh(float seconds) noexcept;   // [0.02,10],  default 0.25

    // --- Target reads (FR-051): what was SET -------------------------------- 
    [[nodiscard]] float getSmearAmount()   const noexcept;
    [[nodiscard]] float getDecoherence()   const noexcept;
    [[nodiscard]] float getSmearTilt()     const noexcept;
    [[nodiscard]] float getSmearTimeLow()  const noexcept;
    [[nodiscard]] float getSmearTimeHigh() const noexcept;
    [[nodiscard]] std::uint32_t getSeed()  const noexcept;

    // --- Applied reads (FR-053): what the LAST FRAME used -------------------
    [[nodiscard]] float getAppliedSmearAmount() const noexcept;
    [[nodiscard]] float getAppliedDecoherence() const noexcept;
    [[nodiscard]] float getAppliedSmearTilt()   const noexcept;

    // --- Query (FR-015, FR-017, FR-018, FR-054) -----------------------------
    [[nodiscard]] bool        isPrepared()          const noexcept;
    [[nodiscard]] bool        isEnabled()           const noexcept;
    [[nodiscard]] std::size_t getFftSize()          const noexcept;
    [[nodiscard]] std::size_t getHopSize()          const noexcept;
    [[nodiscard]] std::size_t getNumBins()          const noexcept;
    [[nodiscard]] double      getSampleRate()       const noexcept;
    [[nodiscard]] std::size_t getLatencySamples()   const noexcept;
    [[nodiscard]] std::size_t getAllocatedBytes()   const noexcept;
    [[nodiscard]] std::size_t getClampEngagements() const noexcept;
    [[nodiscard]] std::size_t getPoisonEngagements()const noexcept;

    // --- Public make-up surface (FR-042; SC-006 (a)/(c) are not executable without it) ---
    [[nodiscard]] static float coherenceMakeup(float decoherence) noexcept;

    // --- White-box surface for FR-023's unreachable-clamp assertion ---------
    /// The pole this component WOULD use for a bin whose tilted time constant is
    /// `tauSeconds` at `hopSize`/`sampleRate`. Static and pure, so the Edge-Cases
    /// white-box case can drive it to synthetic extremes no render can reach.
    /// NEEDS A SPEC AMENDMENT (S17 C-6): FR-018 enumerates the public query surface
    /// exhaustively and does not list this, and FR-023 names `poleTable()` — a private
    /// member — as the white-box surface. Both are corrected at the compliance pass;
    /// no member named `poleTable()` is public.
    [[nodiscard]] static float poleForTau(float tauSeconds, std::size_t hopSize,
                                          double sampleRate) noexcept;
};
```

`getSmearTimeLow/High` return the clamped stored value; there is no applied read for them because
they are not modulation targets (FR-031, FR-033) and are consumed only by the table rebuild.

### S1.5 Private state layout (exact members, in declaration order)

```cpp
private:
    // Geometry (valid whenever prepared_, even with enabled_ == false - FR-018/Q6)
    double      sampleRate_ = 0.0;
    std::size_t fftSize_    = 0;
    std::size_t hopSize_    = 0;
    std::size_t numBins_    = 0;
    float       frameRate_  = 0.0f;        // sampleRate_ / hopSize_, the smoother clock
    bool        prepared_   = false;
    bool        enabled_    = false;

    // Sub-objects, one pair per channel (allocated only when enabled_)
    std::array<STFT, 2>           stft_{};
    std::array<OverlapAdd, 2>     ola_{};
    std::array<SpectralBuffer, 2> spectrum_{};

    // Owned heap - the whole of getAllocatedBytes() (FR-017)
    std::vector<float> poleNeg_;                       // numBins_, tilt = -1
    std::vector<float> poleZero_;                      // numBins_, tilt =  0
    std::vector<float> polePos_;                       // numBins_, tilt = +1
    std::vector<float> poleScratch_;                   // numBins_, tilt-resolved, once per frame
    std::array<std::vector<float>, 2> magState_;       // numBins_ each (FR-022)
    std::array<std::vector<float>, 2> fifo_;           // fifoCapacity_ each (FR-016)
    std::array<std::vector<float>, 2> hopScratch_;     // hopSize_ each

    // FIFO cursors - SHARED by the two channels (one ring geometry, two data arrays)
    std::size_t fifoCapacity_ = 0;
    std::size_t fifoMask_     = 0;
    std::size_t fifoRead_     = 0;
    std::size_t fifoWrite_    = 0;
    std::size_t fifoCount_    = 0;
    std::size_t warmupRemaining_ = 0;                  // FR-014 counter, NOT an emptiness test

    // Control
    OnePoleSmoother smearSm_{};
    OnePoleSmoother decohereSm_{};
    OnePoleSmoother tiltSm_{};
    float smearTarget_   = kDefaultSmearAmount;
    float decohereTarget_= kDefaultDecoherence;
    float tiltTarget_    = kDefaultSmearTilt;
    float tauLow_        = kDefaultSmearTimeLow;
    float tauHigh_       = kDefaultSmearTimeHigh;

    // Per-frame derived state
    float lastResolvedTilt_ = 0.0f;   // the tilt poleScratch_ was built for
    bool  tiltScratchDirty_ = true;   // set by prepare(), and by every table rebuild
    bool  poleTablesDirty_  = false;  // set by setSmearTimeLow/High, consumed in processBlock (S4.3)
    float prevMakeup_       = 1.0f;   // FR-046's ramp origin

    // Per-channel flags / streams
    std::array<bool, 2>        primeNext_{true, true};  // FR-022
    std::array<Xorshift32, 2>  rng_{Xorshift32{1u}, Xorshift32{1u}};
    std::uint32_t              seed_ = kDefaultSeed;

    // Counters (FR-054)
    std::size_t clampEngagements_  = 0;
    std::size_t poisonEngagements_ = 0;
    std::size_t allocatedBytes_    = 0;
```

`std::array<STFT,2>` is legal: `STFT`/`OverlapAdd` are default-constructible and movable
(`stft.h:37-43`, `:209-215`); `SpectralBuffer` is movable (`spectral_buffer.h:51-53`). None is
copyable, which is why `SpectralSmear` is move-only too.

---

## S2. `prepare()` — the twelve ordered steps

```
prepare(double sampleRate, const PrepareConfig& config) noexcept
```

1. **Rate guard.** `if (!detail::isFinite(sampleRate) || sampleRate <= 0.0) { … return; }` — an
   unprepared instance (Edge Cases: "sample rate of 0 or negative → treated as unprepared"). The
   guard body is this **exact ordered list**, and nothing else — revision 1 said both "nothing else
   is written" and "also zero the geometry", which cannot both be true, and S9's read table forces the
   second (an unprepared instance must answer `0` for `getFftSize()`/`getHopSize()`/`getNumBins()` and
   `0.0` for `getSampleRate()`, which is only true if these members are zeroed):
   ```cpp
   sampleRate_ = 0.0;
   fftSize_ = hopSize_ = numBins_ = 0;
   frameRate_ = 0.0f;
   warmupRemaining_ = 0;
   allocatedBytes_ = 0;
   clampEngagements_ = poisonEngagements_ = 0;
   prepared_ = false;
   enabled_  = false;
   return;
   ```
   **The previously-allocated vectors are deliberately retained** — nothing is resized or shrunk on
   this path, because `prepare()` may be re-entered from a thread the owner also renders on and a
   shrink is a deallocation. The consequence is stated once so SC-008 is not written against the wrong
   reading: `getAllocatedBytes() == 0` means **"this instance will not render"**, not "this instance
   holds no heap" — after a successful prepare followed by a bad-rate prepare the component still owns
   up to 120 KiB of its own vectors plus ~524 KiB of sub-object heap (S10). S9's
   `getAllocatedBytes()` row carries the same sentence.
2. **Geometry.** `fftSize_ = std::bit_floor(std::clamp(config.fftSize, kMinFftSize, kMaxFftSize));`
   Clamp **then** `bit_floor`, in that order (`aether_reverb.h:1624` precedent): 0→512, 100→512,
   513→512, 5000→4096, `SIZE_MAX`→4096. `hopSize_ = fftSize_ / kOverlapFactor;`
   `numBins_ = fftSize_ / 2 + 1;` `assert(hopSize_ * kOverlapFactor == fftSize_);`
   `sampleRate_ = sampleRate; frameRate_ = float(sampleRate) / float(hopSize_);`
   **These four are written whether or not `enabled`** — FR-018/Q6 requires a disabled instance to
   answer geometry queries identically to an enabled twin.
3. **`enabled_ = config.enabled;`** If `false`: set `prepared_ = true`, `allocatedBytes_ = 0`,
   `warmupRemaining_ = 0`, zero both counters, snap the three smoothers to their targets, and
   **return before any allocation**. No sub-object is prepared, no vector is resized. A disabled
   instance owns no heap. The snap is housekeeping only (it keeps `getCurrentValue()` from reporting a
   stale value if the instance is later re-prepared enabled); it is **not** the mechanism behind
   FR-053's disabled-instance reads. Those are produced by the getters themselves — S9 states the
   rule — because on a disabled instance no frame ever calls `process()`, so a setter issued *after*
   `prepare()` would leave `getCurrentValue()` at the prepare-time value and SC-002 (e)'s
   exact-equality assertion would fail.
4. **Sub-objects.**
   `stft_[ch].prepare(fftSize_, hopSize_, WindowType::Hann);`
   `ola_[ch].prepare(fftSize_, hopSize_, WindowType::Hann, 9.0f, /*applySynthesisWindow=*/true);`
   `spectrum_[ch].prepare(fftSize_);`
   The `true` is **mandatory, not preferred** (`stft.h:224-227`) and is one decision with the
   `hopSize_ = fftSize_/4` above — the assert in step 2 is the tripwire against a later edit that
   changes one and leaves the other.
5. **Pole tables.** `poleNeg_/poleZero_/polePos_/poleScratch_.assign(numBins_, 0.0f)` then
   `rebuildPoleTables()` (S4.3) — **eagerly here**, because this is the allocating path anyway and a
   prepared instance must never render off a zero-filled table. `tiltScratchDirty_ = true;
   lastResolvedTilt_ = 0.0f; poleTablesDirty_ = false;`
6. **Magnitude memories.** `magState_[ch].assign(numBins_, 0.0f); primeNext_[ch] = true;` (FR-022).
7. **FIFO.** `fifoCapacity_ = std::bit_ceil(fftSize_ + kProcessChunkSamples + hopSize_);`
   `fifoMask_ = fifoCapacity_ - 1;` `fifo_[ch].assign(fifoCapacity_, 0.0f);`
   `hopScratch_[ch].assign(hopSize_, 0.0f);` `fifoRead_ = fifoWrite_ = fifoCount_ = 0;`
8. **Warm-up.** `warmupRemaining_ = fftSize_;` (FR-014 — this, not an emptiness test, is what makes
   the reported latency exactly `fftSize` for every partition; S11 carries the proof.)
9. **Smoothers.** `smearSm_.configure(kControlSmoothMs, frameRate_); smearSm_.snapTo(smearTarget_);`
   and likewise for `decohereSm_`/`tiltSm_`. `configure` on the **frame** clock, not the sample clock
   (`spectral_gate.h:184-187` precedent): the smoother is advanced once per frame by `process()`.
10. **Make-up.** `prevMakeup_ = coherenceMakeup(decohereTarget_);` so the first frame's ramp starts
    where a steady state would sit rather than at 1.0 after a non-zero prepare-time decoherence.
11. **Streams.** `setSeed(seed_);` last, so nothing written earlier can discard it (the
    `noise_organism.h:299-302` / `resonance_drift_network.h:396-400` rule).
12. **Counters and footprint.** `clampEngagements_ = poisonEngagements_ = 0;`
    `allocatedBytes_ = (4 * numBins_ + 2 * numBins_ + 2 * fifoCapacity_ + 2 * hopSize_) * sizeof(float);`
    (S10 has the worked table.) `prepared_ = true;`

`prepare()` may be called twice with different rates or sizes; every table, capacity, cursor and the
warm-up counter is re-derived above, so no stale pole table from a previous rate can survive
(SC-010, Edge Cases).

---

## S3. `reset()`, `setSeed()`, and what each deliberately does not touch

**`reset()` (FR-004)** — audio state only, never control values, never the derived tables, never the
seed:

```
for ch in {0,1}: stft_[ch].reset(); ola_[ch].reset(); spectrum_[ch].reset();
                 std::fill(magState_[ch].begin(), magState_[ch].end(), 0.0f);
                 primeNext_[ch] = true;                       // FR-022 re-arm
                 std::fill(fifo_[ch].begin(), fifo_[ch].end(), 0.0f);
                 std::fill(hopScratch_[ch].begin(), hopScratch_[ch].end(), 0.0f);
fifoRead_ = fifoWrite_ = fifoCount_ = 0;
warmupRemaining_ = fftSize_;                                   // re-armed (FR-004)
prevMakeup_ = coherenceMakeup(decohereSm_.getCurrentValue());
setSeed(seed_);                                                // FR-044 re-derive
// NOT touched: smearSm_/decohereSm_/tiltSm_ (values and targets), tauLow_/tauHigh_,
//              the three pole tables, clampEngagements_/poisonEngagements_ are ZEROED (FR-054).
```

`clampEngagements_`/`poisonEngagements_` are zeroed by `prepare()` **and** `reset()` only (FR-054,
`noise_organism.h:277` precedent). `reset()` on an unprepared or disabled instance is inert (guard on
`prepared_ && enabled_` before touching the sub-objects; the counters still zero).

The observable consequence, asserted rather than left to chance (Edge Cases): a mid-render `reset()`
makes the output return to exactly `fftSize` samples of silence rather than resuming mid-tail. That
is a **discontinuity by design** — the owner resets at note-off/transport boundaries.

**`setSeed(std::uint32_t)` (FR-005, FR-044)**

```
seed_ = seed;
rng_[0].seed(deriveStreamSeed(seed_, kSaltDecohereL));
rng_[1].seed(deriveStreamSeed(seed_, kSaltDecohereR));
```

`deriveStreamSeed` guarantees a non-zero result (`random.h:112`), which is load-bearing:
`Xorshift32::seed()` silently substitutes its own default for 0 (`random.h:73-74`), so two salts
hashing to 0 would collapse the channels onto one stream. `setSeed(0)` is therefore live on both
channels and is asserted (Edge Cases). `setSeed` does **not** touch audio state.

---

## S4. The time-constant law, the tilt, and the three-table interpolation

### S4.1 Base law (FR-030)

For bin `k`, `f_k = k * sampleRate_ / fftSize_` (bin 0 uses `u = 0` directly, avoiding `log(0)`):

```
u(k)   = clamp( log(f_k / 20) / log(20000 / 20), 0, 1 )        // log ratio, base-free
tau(k) = tauLow * pow(tauHigh / tauLow, u(k))                  // seconds
```

A log-frequency **geometric interpolation between two endpoint values**, chosen over a power law or
a fixed corner frequency because the two endpoints are exactly the two things the control surface
exposes (`setSmearTimeLow`, `setSmearTimeHigh`), so the law has no hidden third parameter and the
endpoints mean literally what they are named: `tau(20 Hz) == tauLow`, `tau(20 kHz) == tauHigh`.
`tauLow > tauHigh` by default **is** the roadmap's "lows smear longer" (line 250). `tauLow == tauHigh`
degenerates to a flat law (legal); `tauLow < tauHigh` inverts it (legal, never silently swapped —
Edge Cases).

Worked anchor, reference geometry, shipped defaults (`tauLow = 3.0`, `tauHigh = 0.25`):
`u(100 Hz) = ln(5)/ln(1000) = 0.23299`, `tau(100 Hz) = 3.0 · (1/12)^0.23299 = 1.6814 s`. This is the
number SC-004 (c) and SC-010 assert against: `tau · ln(100) = 7.742 s` to −40 dB.

### S4.2 Tilt (FR-032)

```
tauTilted(k) = clamp( tau(k) * pow( kTiltPivotHz / max(f_k, kTauAnchorLowHz),
                                    tilt * kTiltExponentRange ),
                      kMinSmearSeconds, kMaxSmearSeconds )
```

`tilt = 0` is the **exact** identity (`pow(x, 0) == 1`), the same "identity at zero, bounded exponent
range" shape as `HarmonicCloud::setSpectralGravity` (`harmonic_cloud.h:478`,
`pow(n, 1 + g·kGravityExponentRange)`). Positive tilt lengthens lows and shortens highs; negative
tilt flattens and then inverts.

**The saturation is normative, not a bug (Clarifications Q5).** At the shipped defaults and
`tilt = +1`: `tau(20 Hz) = 3.0 · 50^0.75 = 56.4 s`, clamped to 10 s; solving
`tauTilted(f) = kMaxSmearSeconds` gives `f ≈ 95 Hz`, so every bin below ~95 Hz sits on the clamp. The
header must say so at the law's definition site. `kTiltExponentRange` is **not** shrunk to avoid it:
that would cost tilt its audible travel and would still clamp at non-default `tauLow`.

At `tilt = −1` the same arithmetic gives `tauTilted(20 Hz) = 3.0 · 0.02^0.75 = 0.160 s` and
`tauTilted(20 kHz) = 0.25 · 20^0.75 = 2.364 s` — inverted, both inside the clamp.

### S4.3 Three tables + a per-frame resolve (FR-034, D-7)

`rebuildPoleTables()` runs from `prepare()` (eagerly, step 5) and from **the top of `processBlock`
whenever `poleTablesDirty_` is set** — `setSmearTimeLow()` / `setSmearTimeHigh()` only raise the flag
(FR-036, corrected in S17 C-7):

```
for k in [0, numBins_):
    f    = (k == 0) ? kTauAnchorLowHz : float(k) * float(sampleRate_) / float(fftSize_);
    base = tau(k)                                        // S4.1, one log + one pow
    poleNeg_[k]  = poleForTau(tilted(base, f, -1.0f), hopSize_, sampleRate_);   // one pow + one exp
    poleZero_[k] = poleForTau(clampTau(base),          hopSize_, sampleRate_);   // NO pow - see below
    polePos_[k]  = poleForTau(tilted(base, f, +1.0f), hopSize_, sampleRate_);   // one pow + one exp
tiltScratchDirty_ = true;
poleTablesDirty_  = false;
```

**The `tilt = 0` row must be written without the `pow`.** `tilted(base, f, 0.0f)` is
`base * pow(pivot / max(f, 20), 0.0f)` — mathematically `base`, but `std::pow(x, 0.0f)` is a library
call the compiler is **not** required to fold, so the zero-tilt row is spelled as the clamp of `base`
directly. `clampTau(x) = std::clamp(x, kMinSmearSeconds, kMaxSmearSeconds)`, i.e. exactly what
`tilted()` does after its `pow`.

where `poleForTau` is

```cpp
static float poleForTau(float tauSeconds, std::size_t hopSize, double sampleRate) noexcept {
    const float denom = float(sampleRate) * tauSeconds;
    if (!(denom > 0.0f)) return 0.0f;                       // ordered compare, NaN -> 0
    const float p = std::exp(-float(hopSize) / denom);
    return std::clamp(p, 0.0f, kMaxPole);                   // FR-023 backstop
}
```

**Cost, counted from the code above rather than asserted** (revision 1 said "`3 * numBins` `exp` plus
`numBins` `pow`", which undercounts by roughly 2×, and FR-036 says "`3 * numBins` `exp`", which
undercounts by more):

| per bin | count | source |
|---|---|---|
| `log` | 1 | `u(k) = clamp(log(f_k/20) / log(1000), 0, 1)` (S4.1; `log(1000)` is a compile-time constant) |
| `pow` | 3 | one for `tau(k) = tauLow * pow(tauHigh/tauLow, u)` (S4.1), plus one in each of the two **non-zero** `tilted()` calls (S4.2). It would be **4** without the zero-tilt special case above. |
| `exp` | 3 | one per `poleForTau` |

So a rebuild is `numBins` `log` + `3·numBins` `pow` + `3·numBins` `exp` ≈ **7 transcendentals per
bin**: ~7 200 at the reference geometry (`numBins = 1025`) and **~14 300 at `kMaxFftSize = 4096`**
(`numBins = 2049`: 2 049 `log` + 6 147 `pow` + 6 147 `exp`).

**That cost is on the audio thread by the spec's own contract** — FR-006: "the owner calls setters and
`processBlock` from the same thread, as every Vorago component does" — so it cannot be waved away as
"control-thread cadence". Two consequences, both binding:

1. **The rebuild is deferred, not synchronous.** `setSmearTimeLow` / `setSmearTimeHigh` clamp, store,
   and set `poleTablesDirty_ = true`; nothing else. The rebuild happens at the top of `processBlock`,
   after the entry guards and **before** the chunk loop (S5.1), so (i) N setter calls inside one block
   cost **one** rebuild rather than N, (ii) a setter on an unprepared instance is inert by
   construction — it can never touch a zero-sized vector — which is exactly what the Edge Case
   "`setSmearTimeLow` on an unprepared instance must be inert, not a crash" asks for, and (iii) the
   cost lands **inside the path SC-013 measures** instead of in a gap between criteria. It stays
   allocation-free (FR-064): three already-sized tables are overwritten, never resized.
2. **It is measured.** SC-013 gains a fourth configuration **(d)** — `fftSize = 4096`, one
   `setSmearTimeLow()` per block — with its own checked-in baseline (S15). Configuration (b) still
   does **not** sweep the endpoints; (d) exists precisely so that the one operation in this component
   with a large, geometry-scaled synchronous cost is not absent from every criterion.

The two setters carry an `@note` in S1.4 saying all of this, in the form
`STFT::prepare`'s `@note NOT real-time safe (allocates memory)` uses (`stft.h:57`, read this session).

Run time, once per frame, into the fourth table:

```
if (tiltScratchDirty_ || t != lastResolvedTilt_) {
    for k: poleScratch_[k] = (t <= 0.0f) ? lerp(poleNeg_[k],  poleZero_[k], t + 1.0f)
                                         : lerp(poleZero_[k], polePos_[k],  t);
    lastResolvedTilt_ = t;  tiltScratchDirty_ = false;
}
```

Two multiplies and an add per bin, **no transcendental in the per-frame path** (the only
transcendentals in `processBlock` are the deferred rebuild's, and only on a block where an endpoint
setter fired — SC-013 (d) is the configuration that prices that), and both channels read
the one table because the control value is shared by construction (FR-013 invariant (a)). The
`t != lastResolvedTilt_` guard is behaviour-identical, not an approximation: the resolve is a pure
function of `t` and the three tables, and every path that can change the tables sets
`tiltScratchDirty_`. Exact float equality is the right test here precisely because a settled smoother
snaps (`smoother.h:197-201`).

**The interpolated form IS the specified law** (FR-034, D-7) — it is not an approximation of some
other law a test could find a discrepancy against. It is monotone in `t` bin-by-bin (a linear blend
of two ordered endpoints) and stays in `[0, kMaxPole]` by construction.

---

## S5. `processBlock` — chunking, drain loop, frame body, FIFO pop

### S5.1 Entry guards (FR-003)

```cpp
void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
    if (!prepared_ || !enabled_ || left == nullptr || right == nullptr || numSamples == 0) return;
    if (poleTablesDirty_) rebuildPoleTables();     // S4.3: deferred, at most once per block
```

Buffers are left **untouched** — a disabled instance is a true bypass, bit-identical in and out
(FR-019, SC-002 (d)), not a zero-parameter round trip. There is no mono entry point and no
single-sample `process()`: `SpectralGate`'s single-sample adapter buffers (`spectral_gate.h:759-761`)
are the cost of having tried.

### S5.2 Chunking (FR-012)

```cpp
    std::size_t pos = 0;
    while (pos < numSamples) {
        const std::size_t c = std::min(kProcessChunkSamples, numSamples - pos);
        pumpChunk(left + pos, right + pos, c);
        pos += c;
    }
}
```

This bounds `STFT::samplesAvailable_` to `fftSize + 64` at all times, which is what makes the
unguarded `pushSamples` ring (`stft.h:78`, `fftSize * 8`) structurally safe rather than merely large
enough. The component therefore accepts **any** `numSamples` — no `maxBlockSamples` field, no
caller-side contract (Edge Cases renders 16 384 in one call).

### S5.3 `pumpChunk` — the order is load-bearing, not style (FR-013)

```cpp
void pumpChunk(float* l, float* r, std::size_t n) noexcept {
    stft_[0].pushSamples(l, n);
    stft_[1].pushSamples(r, n);

    while (stft_[0].canAnalyze()) {
        assert(stft_[1].canAnalyze());              // identical push counts => lockstep

        // (a) ONCE per frame-pair, OUTSIDE the channel loop, BEFORE the values are read.
        const float a = std::clamp(smearSm_.process(),    0.0f, 1.0f);
        const float d = std::clamp(decohereSm_.process(), 0.0f, 1.0f);
        const float t = std::clamp(tiltSm_.process(),    -1.0f, 1.0f);

        resolveTiltScratch(t);                      // S4.3, shared by both channels
        const float g = coherenceMakeup(d);         // FR-042, one value per frame

        for (std::size_t ch = 0; ch < 2; ++ch) {    // (c) L strictly before R
            stft_[ch].analyze(spectrum_[ch]);
            const bool poisoned = smearMagnitudes(ch, a);        // S6  (magnitude FIRST)
            // FR-043, ONE rule governing BOTH skips: decohere() is called on EVERY frame.
            // Passing 0.0f takes its burn-only branch, so the draw count per channel per
            // frame is numBins-2 whether the identity gate engaged, the poison path fired,
            // or the full path ran. Calling it conditionally - `if (!poisoned) decohere(ch, d);`
            // - would leave rng_[ch] numBins-2 draws behind a clean render FOREVER after the
            // first poisoned frame, and FR-062's deliberate no-latch design makes that
            // reachable in normal operation, not only at a terminal state.
            decohere(ch, poisoned ? 0.0f : d);                   // S7  (phase SECOND, FR-045)
            ola_[ch].synthesize(spectrum_[ch]);
            ola_[ch].pullSamples(hopScratch_[ch].data(), hopSize_);   // (b) ALWAYS hopSize_
            writeHopToFifo(ch, g);                               // S7.3 ramped make-up + clamp
        }

        fifoWrite_ = (fifoWrite_ + hopSize_) & fifoMask_;   // cursors SHARED, advanced once
        fifoCount_ += hopSize_;
        prevMakeup_ = g;                                    // FR-046's next ramp origin
    }

    popFifo(l, r, n);                                       // S5.4
}
```

**Three invariants ride on this order and each fails silently if disturbed** (the same three
`atmosphere_engine.h:2266-2302` records, transcribed to this component):

- **(a) Frame-major, channel-minor.** The smoothers advance exactly once per hop of audio and both
  channels share one control value. Moving the advance inside the channel loop doubles the advance
  rate, halving the 50 ms constant to ~25 ms, and hands L and R values one hop apart inside the same
  frame. **No criterion sweeping settled values can see it** — it is still click-free (SC-012 (c))
  and still costs the same (SC-013). FR-053's applied reads exist to make it observable and
  **SC-017 is the criterion that reads them**.
- **(b) The pull is inside the drain loop and is always exactly `hopSize_`, never `n`.**
  `synthesize()` accumulates at offset 0 unconditionally (`stft.h:300-308`); the per-frame hop offset
  comes only from `pullSamples` shifting the buffer left (`stft.h:340-357`). Two synthesises without
  an intervening pull of exactly `hopSize` stack both frames at the same offset and destroy COLA —
  and it presents as a windowing bug. A wrong pull size is worse: `pullSamples` returns **silently**
  on an oversized request (`stft.h:339-342`) without zeroing the destination, i.e. a stale-buffer
  read rather than a detectable failure.
- **(c) L is always processed before R.** Part of the determinism contract even though FR-044 gives
  the channels independent streams: it fixes the order of any future shared state.
  **Discharged by inspection at the compliance pass, like FR-045 — no criterion can observe it.**
  With two independent streams and no shared per-channel state, swapping the loop order is
  bit-identical today, so SC-002/SC-003/SC-011/SC-017 (FR-013's Traceability row) cannot change
  verdict on it. The implementer must not go looking for a test that cannot exist; the header carries
  a comment **at the channel loop** stating that the order is part of the determinism contract and
  that it exists to fix the order of any future shared state. S17 C-8 carries the Traceability
  correction.

`while`, not `if` — `analyze()` consumes only `hopSize_` (`stft.h:171`), so a chunk that crosses two
hop boundaries must drain both.

### S5.4 `popFifo` — the warm-up counter (FR-014)

```cpp
void popFifo(float* l, float* r, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        if (warmupRemaining_ > 0) {            // THE COUNTER IS THE RULE
            --warmupRemaining_;
            l[i] = 0.0f; r[i] = 0.0f;
        } else if (fifoCount_ > 0) {
            l[i] = fifo_[0][fifoRead_];
            r[i] = fifo_[1][fifoRead_];
            fifoRead_ = (fifoRead_ + 1) & fifoMask_;
            --fifoCount_;
        } else {
            l[i] = 0.0f; r[i] = 0.0f;          // unreachable after warm-up (S11); degrade to silence
        }
    }
}
```

**Not "emit zeros whenever the FIFO happens to be empty".** Draining as soon as something is
available makes the offset a function of the caller's block size:
`(ceil(fftSize/chunk) − 1) * chunk` — measured at 960 / 1020 / 1022 samples for chunks 64 / 30 / 7 at
fftSize 1024 (`aether_reverb.h:643-660`). Never `fftSize`, and never the same twice, which would make
`getLatencySamples()` a lie in every host and put SC-011 out of reach.

---

## S6. The magnitude pass (FR-020 – FR-025, FR-062)

One pass over `k ∈ [0, numBins_)`, per channel, per frame. DC (bin 0) and Nyquist (`numBins_−1`)
**are** included: unlike phase, their magnitude is a free real quantity and excluding them would
leave two unsmeared spikes in the fog (FR-025).

```cpp
/// @returns true if the frame was poisoned (FR-062) and the caller must skip the phase pass.
bool smearMagnitudes(std::size_t ch, float amount) noexcept {
    SpectralBuffer& s  = spectrum_[ch];
    float* state       = magState_[ch].data();
    const float* pole  = poleScratch_.data();
    float accum        = 0.0f;                       // FR-062: ONE finiteness test per frame

    if (primeNext_[ch]) {                            // FR-022 priming (Clarifications Q1)
        for (std::size_t k = 0; k < numBins_; ++k) {
            const float m = s.getMagnitude(k);
            accum += m;
            state[k] = m;                            // memory := this frame, no integration
        }
        primeNext_[ch] = false;
        // amount blends state against mag, and state == mag, so the write is the identity:
        // skip it entirely (this is also what SC-018 asserts).
    } else if (amount == 0.0f) {                     // FR-021 EXACT-identity gate
        for (std::size_t k = 0; k < numBins_; ++k) {
            const float m = s.getMagnitude(k);
            accum += m;
            state[k] = m + pole[k] * (state[k] - m);   // integrator STILL runs - never stale
            if (state[k] < kDenormalFloor) state[k] = 0.0f;   // FR-024, ordered compare
        }
        // NO magnitude write at all: leaving the analysed spectrum untouched is MORE exactly
        // transparent than writing mag[k] back, which would force a polar round trip.
    } else {
        for (std::size_t k = 0; k < numBins_; ++k) {
            const float m = s.getMagnitude(k);
            accum += m;
            const float st = m + pole[k] * (state[k] - m);    // FR-020, normalised one-pole
            state[k] = (st < kDenormalFloor) ? 0.0f : st;     // FR-024
            s.setMagnitude(k, m + amount * (state[k] - m));   // FR-021, magnitude-domain BLEND
        }
    }

    if (!detail::isFinite(accum)) return handlePoison(ch);    // S8
    return false;
}
```

**The integrator form (FR-020).** `state[k] = (1 − p)·mag[k] + p·state[k]`, written above in the
fused `m + p·(state − m)` form (one FMA). Normalisation by `(1 − p)` is a requirement, not a detail:
it makes the steady-state gain **exactly unity at every `p`**, which is what lets FR-021's identity
claim and SC-003's null test coexist with a smear knob that never changes the long-term average
spectrum (a Non-Goal). Exact discretisation, stated so no one has to guess: the continuous
one-pole `dx/dt = (m − x)/tau` sampled at the frame period `hopSize/sampleRate` has the **exact**
solution `x[n+1] = m + (x[n] − m)·exp(−hop/(sampleRate·tau))`, which is precisely `poleForTau`. There
is no bilinear/backward-Euler approximation anywhere in this component, so `tau` means exactly what
SC-004 (c) measures.

**The blend (FR-021, D-12).** `amount` blends the analysed magnitude against the integrator state;
it is **not** a scale on the pole. The rejected form `p(k) = amount · poleTable(k)` puts the entire
audible travel of the knob in the last fraction of a percent below 1.0: at the reference geometry
with the shipped defaults, `poleTable(100 Hz) = 0.99366`, so `amount ∈ {0, .25, .5, .75, 1}` maps to
effective time constants `0, 7.6 ms, 15 ms, 36 ms, 1.68 s` — four "no smearing" points and an
endpoint. That makes a control the roadmap names as a **modulation target** (line 255) do nothing
over 99 % of a modulator's sweep, and makes SC-004 (a)'s intermediate points non-discriminating. The
blend is cheaper (one FMA, no per-bin pole product), is even in `amount` by construction (flux is
linear in it), is continuous at zero, keeps the pole a pure function of frequency and tilt — which is
what makes SC-004 (c)'s analytic target well defined — and is a convex combination of two
unity-steady-state-gain quantities, so FR-020's unity gain and FR-063's boundedness hold unchanged.

**Priming (FR-022, Clarifications Q1).** `primeNext_[ch]` is set by `prepare()`, `reset()` and the
poison clear. The next analysed frame writes `state[k] = mag[k]` directly and every subsequent frame
integrates. No start-up fade; unity steady-state gain and boundedness unaffected — priming changes
only the memory's first value, not the law that updates it. The warm-up discard the criteria use is
therefore `2 * fftSize`, not `5 * tau`.

**Denormal safety (FR-024) — discharged BY INSPECTION, not by a criterion.** FR-024's Traceability
row currently points at SC-015 (the lint set) and SC-016 (bit-pattern NaN/Inf injection); neither
touches denormals, and **no render can detect the flush's absence**: every DSP test binary enables
FTZ/DAZ process-wide (`tests/test_helpers/enable_ftz_daz.h:27-32`, called from
`dsp/tests/dsp_test_main.cpp`), so both the numerical result and the timing are identical with and
without it. This gets the FR-045 treatment rather than a false ✅ (S17 C-9): the compliance row reads
**"by inspection at the compliance pass"**, and the header must carry a comment **at the flush site**
saying the flush exists for hosts that have not set the MXCSR FTZ/DAZ bits, that the repo's own test
binaries cannot observe it, and that it is an ordered `<` rather than a bit test so NaN passes through
to FR-062. No white-box helper is added for it: FR-018 enumerates the public surface and nothing in
the spec authorises one (the same rule that made `poleForTau` an S17 amendment rather than a silent
addition).

Magnitude states decay geometrically toward zero and are the one place
in this component where denormals are reachable at long time constants. The repo enables FTZ/DAZ
process-wide in tests (`enable_ftz_daz.h:27-32`) and hosts normally do the same, but the flush is
applied anyway so the component is correct on a host that has not set the MXCSR bits. It is an
**ordered comparison** (`state < kDenormalFloor`), not a bit test, so `-ffast-math` cannot fold it —
and NaN fails an ordered `<`, so a poisoned state is *not* silently flushed to zero before the frame
accumulator gets to see it. Magnitudes from `computePolarBulk` are `sqrt(re²+im²) ≥ 0`, so the
one-sided test is complete.

**Cost note.** The magnitude half is close to free relative to the round trip it shares:
`SpectralBuffer` keeps a lazy dual representation with dirty flags, so writing **both** magnitude and
phase costs the *same* two bulk SIMD conversions as writing phase alone (`spectral_buffer.h:7-12`,
`:179-198`).

---

## S7. The phase pass, the RNG, and the ramped make-up (FR-040 – FR-046)

### S7.1 Decoherence

```cpp
void decohere(std::size_t ch, float d) noexcept {
    SpectralBuffer& s = spectrum_[ch];
    Xorshift32& rng   = rng_[ch];
    if (d == 0.0f) {                                   // FR-041 exact-identity gate
        for (std::size_t k = 1; k + 1 < numBins_; ++k) (void)rng.nextFloat();   // FR-043: BURN
    } else {
        for (std::size_t k = 1; k + 1 < numBins_; ++k)
            s.setPhase(k, s.getPhase(k) + d * kPi * rng.nextFloat());
    }
}
```

`nextFloat()` is bipolar `[-1,+1]` (`random.h:59-63`), so the perturbation is uniform on `±d·π`: `0`
is the identity and `1` is full decoherence. **DC and Nyquist are excluded** because their phase is
not free in a real spectrum (`atmosphere_engine.h:2325-2328`). The draw count is therefore
`numBins − 2` per channel per frame — 1023 at the reference geometry.

The gate is an **exact-value** gate on a settled float, never a threshold: any non-zero `d`, however
small, takes the full path. It is reachable and stable because `OnePoleSmoother::process()` snaps on
completion (`smoother.h:197-201`). Skipping the write is *more* exactly transparent than adding zero,
which would still force a polar round trip and re-round the spectrum.

**FR-043's draw-burning rule** is what makes the stream position a function of **elapsed frames
alone**, never of whether the gate engaged **and never of whether the frame was poisoned** — S5.3
calls `decohere(ch, poisoned ? 0.0f : d)` rather than skipping the call, so the burn-only branch
covers both skips. The observable consequence is *not* that a post-park
segment is independent of the park's length — it is that at any **absolute** sample index the streams
are where the frame count puts them, which is exactly what SC-009 (c) measures over a fixed absolute
window.

**FR-045 (ordering, code-review-only).** The phase pass runs **after** the magnitude write in the
same frame, so the magnitude memory always integrates the analysed magnitudes and never a value this
component perturbed. Since FR-040 writes only phase, the two orderings are **bit-identical today** and
no render can distinguish them; the header must carry a comment at the ordering site stating the
ordering and this reason, so that a future magnitude-domain addition cannot silently create a
feedback path. Its Traceability row says "by inspection", and the compliance pass discharges it by
inspection — no criterion can.

### S7.2 Coherence make-up (FR-042)

Independently randomised per-frame phases sum **incoherently**, so `OverlapAdd`'s COLA factor —
computed once at prepare (`stft.h:249-262`) and applied unconditionally (`:300-308`) — is wrong by a
level that grows with `d`, about 6 dB at `d = 1` (`aether_reverb.h:623-626`). The make-up is a scalar
on the **pulled time-domain samples, never on the bins**, so FR-020's unity-gain claim holds
literally.

```cpp
[[nodiscard]] static float coherenceMakeup(float d) noexcept {
    const float x = std::clamp(d, 0.0f, 1.0f) * float(kCoherenceKnotCount - 1u);
    const float floored = std::floor(x);
    auto  k = static_cast<std::size_t>(floored);
    float t = x - floored;
    if (k >= kCoherenceKnotCount - 1u) { k = kCoherenceKnotCount - 2u; t = 1.0f; }
    const float ym1 = kCoherenceMakeup[(k > 0u) ? k - 1u : 0u];            // end tangent clamped
    const float y0  = kCoherenceMakeup[k];
    const float y1  = kCoherenceMakeup[k + 1u];
    const float y2  = kCoherenceMakeup[((k + 2u) < kCoherenceKnotCount) ? k + 2u
                                                                       : kCoherenceKnotCount - 1u];
    return Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, t);
}
```

Byte-for-byte the shape `AetherReverb::coherenceMakeup` ships (`aether_reverb.h:4019-4035`); the end
tangents are clamped, which is what keeps `g(0)` exactly 1.0 (`cubicHermiteInterpolate` returns
`c0 == y0` at `t == 0`, `interpolation.h:88`).

**The knots are transcribed as the expectation, and SC-006 measures ours.** The transfer argument is
that the incoherent/coherent amplitude ratio `sqrt(Σw⁴)/Σw²` depends on the **window and the overlap
count**, not on `fftSize` (`aether_reverb.h:640-643`), and our geometry is identical (Hann, 75 %,
synthesis window on). If any measured knot differs from the transcribed value by more than 2 %, **our
measured table ships** and the discrepancy is written into the header with its cause (FR-042). Both
the table and the interpolant are **public** because SC-006 has to divide the make-up back out and
nothing else on the query surface exposes `g(d)` — without them the criterion is not executable and
its knot arm is circular.

### S7.3 The ramp, the clamp, the FIFO write (FR-046, FR-061)

```cpp
void writeHopToFifo(std::size_t ch, float g) noexcept {
    const float* src = hopScratch_[ch].data();
    const float  inv = 1.0f / float(hopSize_);
    for (std::size_t i = 0; i < hopSize_; ++i) {
        const float w    = float(i + 1) * inv;                 // ends EXACTLY at g
        const float gain = prevMakeup_ + (g - prevMakeup_) * w;
        float       v    = src[i] * gain;
        const float cl   = std::clamp(v, -kOutputClamp, kOutputClamp);   // FR-061, ordered
        if (cl != v) ++clampEngagements_;                      // once per engaging SAMPLE
        fifo_[ch][(fifoWrite_ + i) & fifoMask_] = cl;
    }
}
```

**Why a ramp and not AetherReverb's per-hop constant** (`aether_reverb.h:4097-4102`, `dstL[i] *= g;`)
— this is a **deliberate deviation** and FR-046 carries the arithmetic: `decoherence` is a modulation
target here (FR-033) and FR-035's 50 ms smoothing runs on the **frame** clock (93.75 Hz at the
reference geometry, ~4.7 frames), so a `0 → 1` step moves `d` to ~0.656 after **one** frame — the
smoother's measured coefficient is `exp(−5000/(50 · 93.75)) = 0.34424`, so the first frame lands at
`1 − 0.34424` — and `g` from 1.000 to ~1.55 at a single sample boundary. That is a ~55 %
instantaneous amplitude step, an order of magnitude above the peak inter-sample delta of a 1 kHz tone
at 48 kHz (`A·2π·1000/48000 = 0.131·A`), i.e. a textbook 5-sigma derivative outlier for
`ClickDetector` (`artifact_detection.h:44` is the sigma rule). **SC-012 (c) is the enforcing
measurement and it is the arm that fails without FR-046.**

The clamp is an ordered comparison (`std::clamp`), not a bit test, so it survives `-ffast-math`. Note
that `std::clamp` does **not** reject NaN (`v < lo` and `hi < v` are both false), which is why the
finiteness backstop is FR-062's frame accumulator, upstream, and not this clamp.

`prevMakeup_` is initialised in `prepare()` to `coherenceMakeup(decohereTarget_)` and re-initialised
by `reset()`, and is updated **once per frame after both channels have consumed it** (S5.3) — a
per-channel update would give R a different ramp from L.

---

## S8. Non-finite handling: clear, re-prime, continue (FR-062, D-8)

```cpp
bool handlePoison(std::size_t ch) noexcept {
    std::fill(magState_[ch].begin(), magState_[ch].end(), 0.0f);   // (a)
    primeNext_[ch] = true;                                         // (a) re-arm the priming flag
    for (std::size_t k = 0; k < numBins_; ++k) {                   // (b) synthesise silence
        spectrum_[ch].setMagnitude(k, 0.0f);
        spectrum_[ch].setPhase(k, 0.0f);
    }
    ++poisonEngagements_;                                          // (c)
    return true;                                                   // (d) CONTINUE - no latch
}
```

The caller does **not** skip the phase pass on this path — it calls `decohere(ch, 0.0f)` (S5.3), whose
burn-only branch advances `rng_[ch]` by the same `numBins - 2` draws every other frame costs. Writing
nothing and burning nothing would put the stream permanently behind a clean render's and make FR-043's
"stream position depends only on the number of frames elapsed" literally false. The zeroed spectrum is
left zeroed: the burn branch performs no `setPhase`.

**Both magnitude and phase are zeroed, and that is not belt-and-braces.** `computePolarBulk` produces
`phase = atan2(imag, real)`, which is NaN for a NaN input; `reconstructCartesianBulk` then computes
`mag · cos(phase)`, and `0.0f · NaN` is NaN. Zeroing magnitudes alone would therefore still synthesise
NaN. Zeroing the phase too makes the reconstruction exactly `0 · cos(0) = 0`.

One `detail::isFinite` call per frame per channel, on an accumulator, **not per bin**
(`atmosphere_engine.h:2250-2260` cost rule: the helper is deliberately non-inlinable so it survives
`/fp:fast`, and a per-bin test would be a measurable fraction of the budget for something that fires
essentially never). The accumulator sums the **analysed** magnitudes, which is sound because that is
the only entry point for poison: a non-finite input sample gives `mag = sqrt(NaN…) = NaN` and the sum
inherits it, while the integrator state can only have become non-finite through a previous frame's
magnitude — and that frame's clear already zeroed it.

**It does not latch, and that is a deliberate, documented deviation from `AtmosphereEngine`**
(`:2244-2260`, "no auto-resume: reset() … is the one documented recovery"). This component sits on the
**global** bus post-voice-sum (roadmap line 78), where latching converts one poisoned frame into a
dead instrument that only `reset()` can revive. The magnitude memory is the only state that can carry
poison forward, and (a) clears **and re-primes** it, so recovery is automatic within one frame after
the poisoned one rather than merely within `fftSize` samples. SC-016 (iii)/(iv) are the enforcing
measurements; SC-018's second arm asserts the re-prime specifically.

**The silent gap is bounded in shape, and the arithmetic is why SC-016's window is where it is.**
FR-062 does **not** clear `STFT`'s input ring, which keeps returning the injected samples for another
`fftSize` samples (`stft.h:150-166` reads the oldest `fftSize`). A one-block (512-sample) injection at
the reference geometry is therefore covered by **5** consecutive analysis frames — a span of `L` input
samples is overlapped by `floor((L - 1 + fftSize - 1)/hopSize) + 1` windows, i.e.
`floor(2558/512) + 1 = 5` at `L = 512`, `fftSize = 2048`, `hop = 512`, and that is the maximum over
alignments — and the last of them contributes over a further `fftSize` output samples of overlap-add.
Hence SC-016 (iv)'s `ceil(fftSize/hopSize) + 1 = 5` frame bound, which is **exactly** this figure and
not a padded one, and SC-016 (iii)'s recovery window at `[E + 2·fftSize, E + 3·fftSize)`, clear of the
gap. Because each output sample is covered by `numOverlaps = 4` frames, 5 silent frames produce a
silent **output run** of only `(5 - 4 + 1) * hopSize = 2 * hopSize` samples — that run, not the frame
count, is what SC-016 (iv) can actually measure.

**Setter-level non-finiteness (FR-009)** uses **substitution**, not rejection:

```cpp
static float sanitise(float v, float dflt) noexcept { return detail::isFinite(v) ? v : dflt; }
void setSmearAmount(float a) noexcept {
    smearTarget_ = std::clamp(sanitise(a, kDefaultSmearAmount), 0.0f, 1.0f);
    smearSm_.setTarget(smearTarget_);
}
```

This is `atmosphere_engine.h:911`'s shape and **differs deliberately from `ResonanceDriftNetwork`**,
whose FR-008 *rejects* (`if (!isFinite(v)) return;`, previous value stands). A reviewer arriving from
Phase 3 will expect rejection; the header must say which rule it follows and that FR-009 chose
substitution. Either way a NaN write is inert rather than poisoning, which is the property that
matters. `std::isnan`/`std::isinf`/`std::isfinite` appear nowhere in the header or the tests
(FR-008; `tools/lint-nonfinite-symbols.js` enforces it).

---

## S9. Read surface semantics (FR-015, FR-017, FR-018, FR-019, FR-053)

| Read | unprepared | prepared, `enabled = false` | prepared, enabled |
|---|---|---|---|
| `isPrepared()` | `false` | `true` | `true` |
| `isEnabled()` | `false` | `false` | `true` |
| `getFftSize()` / `getHopSize()` / `getNumBins()` | `0` | **the clamped/snapped values** | same |
| `getSampleRate()` | `0.0` | **the prepared rate** | same |
| `getLatencySamples()` | `0` | `0` | `fftSize_` |
| `getAllocatedBytes()` | `0` — meaning **"will not render"**, not "holds no heap" (S2 step 1) | `0` | the S10 figure |
| `getSmear*` / `getDecoherence` / `getSeed` (targets) | the stored, clamped value | same | same |
| `getAppliedSmearAmount/Decoherence/SmearTilt` | `0.0f` | **the matching target** | the smoothed value |
| `getClampEngagements()` / `getPoisonEngagements()` | `0` | `0` | live counters |

The "prepared-but-disabled answers geometry" row is Clarifications Q6 and is what lets Phase 10 lay
out its bus and decide its host-latency report before enabling anything (SC-002 (e) asserts the
disabled instance agrees with an enabled twin on all four geometry reads). The applied reads mirror
their targets on a disabled instance because **no frame ever runs to smooth them**, so there is
nothing to mirror but the target. On an unprepared instance they return `0.0f`, per FR-018's
"documented neutral" rule (`noise_organism.h:855-859`).

**The applied getters produce that column themselves — this is the normative implementation rule, and
the prepare-time smoother snap is NOT the mechanism** (S2 step 3). Each of the three is exactly:

```cpp
[[nodiscard]] float getAppliedSmearAmount() const noexcept {
    if (!prepared_)          return 0.0f;          // FR-018 documented neutral
    if (!enabled_)           return smearTarget_;  // FR-019: no frame runs, so mirror the target
    return smearSm_.getCurrentValue();             // FR-053: what the LAST FRAME used
}
```

and likewise for `getAppliedDecoherence()` (`decohereTarget_`) and `getAppliedSmearTilt()`
(`tiltTarget_`). Relying on the snap instead would fail SC-002 (e): the setter (S8) only calls
`setTarget()`, and on a disabled instance nothing ever calls `process()`, so a target set **after**
`prepare()` would never reach `current_`.

`getLatencySamples()` is **constant for a prepared instance**: no control value, including
`smearAmount = 0` and `decoherence = 0`, changes it. The only switch that moves it is the
prepare-time `enabled` flag, which cannot move mid-render — a latency that moves mid-render is a click
plus a host renegotiation (`aether_reverb.h:614-616`). This is exactly the shape
`AtmosphereEngine::getLatencySamples()` ships (`blurEnabled_ ? blurFftSize_ : 0`, `:1129`).

---

## S10. Allocation ledger (FR-017, FR-064) — the assertion surface for SC-008

`getAllocatedBytes()` reports **the component's own vectors only**, self-reported because
`AllocationDetector`'s operator replacements discard `size` (`allocation_detector.h`, the
`noise_organism.h:996-999` precedent). The scope is normative because the two readings differ by a
factor of ~4.5:

```
allocatedBytes_ = ( 4 * numBins_          // poleNeg_, poleZero_, polePos_, poleScratch_
                  + 2 * numBins_          // magState_[0..1]
                  + 2 * fifoCapacity_     // fifo_[0..1]
                  + 2 * hopSize_ )        // hopScratch_[0..1]
                  * sizeof(float);
```

| `fftSize` | `hop` | `numBins` | `fifoCapacity` = `bit_ceil(fft+64+hop)` | reported bytes |
|---|---|---|---|---|
| 512 | 128 | 257 | `bit_ceil(704)` = 1024 | `(1028+514+2048+256)·4` = **15 384** |
| 1024 | 256 | 513 | `bit_ceil(1344)` = 2048 | `(2052+1026+4096+512)·4` = **30 744** |
| 2048 | 512 | 1025 | `bit_ceil(2624)` = 4096 | `(4100+2050+8192+1024)·4` = **61 464** |
| 4096 | 1024 | 2049 | `bit_ceil(5184)` = 8192 | `(8196+4098+16384+2048)·4` = **122 904** (120.0 KiB ≤ 128 KiB ✔) |

**Excluded, and *reported* by SC-008 as an informational figure** (it is `STFT`/`OverlapAdd`/
`SpectralBuffer`/`FFT` policy, not this component's): at `fftSize = 4096`, `STFT::inputBuffer_` is
`fftSize*8` floats **per channel** = 262 144 B (`stft.h:78`), `windowedFrame_` + `window_` = 65 536 B
(`stft.h:189-191`), `OverlapAdd::outputBuffer_` at `fftSize*2` = 65 536 B (`stft.h:265`) plus
`ifftBuffer_` + `synthesisWindow_` = 65 536 B (`stft.h:372-374`), two `SpectralBuffer`s ≈ 65 568 B
(`spectral_buffer.h:61-66`) — **≈ 524 KiB**, plus pffft's own setup inside `FFT`.

**Zero allocation after `prepare()` (FR-064).** `processBlock` touches no `std::vector` size — the
deferred `rebuildPoleTables()` it may run at the top (S4.3) **overwrites** three already-sized tables
and never resizes them; `setSmearTimeLow`/`setSmearTimeHigh` only clamp, store and raise
`poleTablesDirty_`; `setSeed` reseeds two PODs. The `while` and `for` loops use no
`std::function`, no `std::string`, no exception path. Tests must include **only**
`allocation_detector.h` and **never** `allocation_operator_overrides.h`: the global replacements are
program-wide and `dsp_processors_tests` already has its single owner in `brownian_drift_test.cpp:28`
— a second include is a duplicate-symbol link error, and
`tools/lint-allocation-operator-overrides.js` gates it.

---

## S11. Latency and partition invariance — the occupancy proof (FR-014, SC-002, SC-011)

Let `n` be the 0-based output sample index and count everything at the moment output `n` is produced
(all pushes for its chunk have happened, because `pumpChunk` pushes and drains before it pops).

- Samples pushed by then: `n + 1`.
- Frames produced: `F(n) = 0` while `n + 1 < fftSize`, else `floor((n + 1 − fftSize)/hop) + 1`
  (`canAnalyze()` is `samplesAvailable_ >= fftSize`, `stft.h:137`, and `analyze()` consumes `hop`,
  `:171`).
- FIFO samples written: `F(n) · hop`. FIFO samples consumed: `max(0, n + 1 − fftSize)` — the first
  `fftSize` outputs come from the warm-up counter and **never touch the read cursor**.

Write `n + 1 = fftSize + m`, `m ≥ 0`. Occupancy `= (floor(m/hop) + 1)·hop − m ∈ (0, hop]`. So the
FIFO is **never empty after the warm-up** (the `else` branch in S5.4 is unreachable in a prepared
instance and exists so a future cadence change degrades to silence rather than to unwritten memory)
and its peak occupancy inside a chunk is `hop + kProcessChunkSamples`, comfortably inside
`bit_ceil(fftSize + 64 + hop)`.

**Latency is exactly `fftSize`, for every partition.** `OverlapAdd` reconstructs with zero delay of
its own — frame 0 covers input `[0, fftSize)` and accumulates at output offset 0 — so the FIFO
stream is time-aligned with the input, and the `fftSize` counter-emitted zeros in front of it are the
whole of the reported latency. The count depends only on the counter, never on the chunking, which is
the direct assertion of SC-011 (a "drain when available" implementation fails it with a
partition-dependent offset).

**Two regions are NOT equal to the delayed input and every criterion must skip them:** output
`[0, fftSize)` is the counter's zeros, and output `[fftSize, 2·fftSize − hop)` is `OverlapAdd`'s COLA
ramp-up, where fewer than `numOverlaps` frames have summed (`stft.h:249-262`, `:300-308`). Discarding
`2 * fftSize` output samples clears both at every geometry — which is why SC-002 (b) puts its impulse
at input index `2 * fftSize` (peak then lands at output `3 * fftSize`, deep in the steady region) and
why SC-003 and SC-004 use a `2 * fftSize` warm-up discard.

SC-002 (b) also cannot put the impulse at sample 0 for a second, independent reason: `generateHann`
is the **periodic** variant, so `window[0] == 0.5 − 0.5·cos(0) == 0.0f` exactly
(`window_functions.h:111-121`), and input sample 0 is covered by frame 0 only, at window index 0. An
impulse there is annihilated, the whole output is identically `0.0f`, and `argmax` of an all-zero
buffer is index 0 — a correct implementation would "pass" for the wrong reason and a broken one too.

---

## S12. Boundedness (FR-063) — structural, then measured

1. The integrator is a **convex combination** of `mag[k]` and `state[k]` with `p ∈ [0, kMaxPole]`,
   `kMaxPole < 1`, so `|state[k]| ≤ sup|mag|` for all time; there is no growth mode.
2. FR-021's blend is a convex combination of two such quantities, so the written magnitude is bounded
   by the same supremum.
3. Phase perturbation does not change magnitude, so it cannot change the bound (it moves energy
   between output samples, which the make-up then corrects in the mean).
4. The make-up gain is bounded by `max(kCoherenceMakeup) ≈ 2.0`.
5. `kOutputClamp = 4.0f` is an unconditional backstop (FR-061) and is instrumented, so SC-005 (iii)
   can assert it is **not** a working part at realistic drive.

**What actually bounds the hold time is `kMaxSmearSeconds = 10 s` (FR-031), not `kMaxPole`**, and the
plan says so rather than claiming a guard it does not need. `p = exp(−hop/(sampleRate·tau))`; reaching
`kMaxPole = 0.99999f` needs `hop/(sampleRate·tau) ≤ 1.0e−5`, whereas the largest reachable hop is
`kMaxFftSize/4 = 1024` and the largest `tau` is 10 s: `1024/(44100·10) = 2.32e−3` → pole 0.99768 at
44.1 kHz, and `1024/(192000·10) = 5.33e−4` → pole 0.99947 even at 192 kHz. Three orders of magnitude
below the clamp. `kMaxPole` is therefore a **defensive backstop** against a future range change or a
degenerate derived `tau`, asserted white-box through `poleForTau()` at synthetic extremes rather than
through a render — which is exactly why `poleForTau` is public and static (S1.4). SC-005 (v) asserts
the `kMaxSmearSeconds` bound instead, i.e. that the tail decays at the rate the configured `tau`
implies, which is what keeps this component from becoming a second undocumented freeze.

The other reachable extreme, for the opposite reason: `tau = kMinSmearSeconds = 0.02 s` at
`fftSize = 4096` / 44.1 kHz gives `hop/(sampleRate·tau) = 1024/882 = 1.161` and a pole of
`exp(−1.161) = 0.313` — the only shipped configuration where the exponent exceeds 1. The integrator
must remain a convex combination there (it does: `exp(−x) ∈ (0,1)` for every finite `x > 0`, so a
negative pole is unreachable and no clause asserts against one).

---

## S13. Test plan

Four TUs, all in `dsp/tests/unit/processors/`, all registered by name (the list is enumerated, not
globbed — an unregistered TU silently drops out and its cases never run).

| TU | Criteria | Tag posture |
|---|---|---|
| `spectral_smear_test.cpp` | SC-002 (a)–(e), SC-003, SC-007, SC-008, SC-009, SC-011, SC-012 (c), SC-017, SC-018, Edge Cases **except the non-finite-setter arm** | untagged (per-push lane) |
| `spectral_smear_spectral_test.cpp` | SC-001, SC-004, SC-005, SC-006, SC-010, SC-012 (a)(b), `SpectralSmear_TimeConstantLaw`, `SpectralSmear_DcNyquistSmear` | `[long]` where stated |
| `spectral_smear_perf_test.cpp` | SC-013 (a)–(d) | `[.perf]` only |
| `spectral_smear_nonfinite_test.cpp` | SC-016 **+ the FR-009 non-finite setter arm** (see below) | the **one** TU in the `-fno-fast-math` block |

**Non-finite values live in ONE TU, and it is the `-fno-fast-math` one.** Revision 1 put "non-finite
arguments" inside `SpectralSmear_ControlClamps` in `spectral_smear_test.cpp` while listing only
`spectral_smear_nonfinite_test.cpp` in the `-fno-fast-math` block. On the macOS/Linux `-ffast-math`
legs `std::numeric_limits<float>::quiet_NaN()/infinity()` fold to finite garbage, so that arm would
either pass vacuously (FR-009's substitution never exercised) or red on a correct build, depending on
what the fold produced — and `spectral_smear_test.cpp` is not the TU where the bit-pattern idiom is
mandated. The Phase-3 precedent is explicit and is followed here verbatim:
`dsp/tests/unit/systems/resonance_drift_network_test.cpp:38-42` reads *"NON-FINITE VALUES: never
`std::numeric_limits<float>::quiet_NaN()/infinity()` here - this TU is deliberately NOT in
`dsp/tests/CMakeLists.txt`'s `-fno-fast-math` block … SC-009 owns bit-pattern injection and lives in
`resonance_drift_network_nonfinite_test.cpp`"*. So: `SpectralSmear_ControlClamps` keeps the range-end,
exactly-`0.0f`/`1.0f`, smallest-positive-float, before-`prepare()` and `setSeed(0)` arms, and the
**non-finite setter arm moves to `spectral_smear_nonfinite_test.cpp` as
`SpectralSmear_NonFiniteSetters`**, where every non-finite value is built from a bit pattern through a
volatile sink (`resonance_drift_network_nonfinite_test.cpp:56-66`) and `std::numeric_limits` is banned
by the TU banner.

Shared fixture conventions (one anonymous namespace per TU): `kFs48 = 48000.0`,
`kRefFft = 2048`, `kRefHop = 512`, a `renderStereo(SpectralSmear&, span in, span out, partition)`
driver, and a `makePrepared(fftSize, enabled = true)` factory. Every render discards
`2 * fftSize` output samples before measuring (S11).

### S13.1 The new flux helper (SC-004, Clarifications Q2)

New header `tests/test_helpers/spectral_flux.h` — **a plain function, not a class**; no CMake edit is
needed because `test_helpers` is an INTERFACE library whose include directory is that folder
(`tests/test_helpers/CMakeLists.txt:7-12`). A sweep of `signal_metrics.h`, `spectral_analysis.h`,
`audio_features.h` and `statistical_utils.h` this session found no existing flux metric.

```cpp
namespace Krate::DSP::TestUtils {
/// Mean normalised frame-to-frame L1 magnitude flux inside [lowHz, highHz].
///   flux = mean over frames f of ( sum_k |mag_f[k] - mag_{f-1}[k]| / sum_k mag_f[k] )
/// Frames whose denominator is below kFluxSilenceFloor are skipped (silence has no flux);
/// returns 0.0 if every frame is skipped.
[[nodiscard]] double computeMagnitudeFlux(const float* signal, std::size_t numSamples,
                                          double sampleRate, std::size_t fftSize,
                                          std::size_t hopSize, float lowHz, float highHz);
}
```

Implementation notes that are **load-bearing, not style**:

- It runs its **own, independent** analysis `STFT` + `SpectralBuffer` over the component's *output*,
  never the component's internal buffers.
- It must push in chunks of at most `hopSize` and drain with `while (canAnalyze())`. `STFT::pushSamples`
  has **no overflow guard** (`stft.h:104-124`) and the ring is `fftSize * 8`; a helper that pushes a
  30-second render in one call corrupts memory. This is the single most likely way to get the helper
  wrong.
- Band → bin range: `kLo = ceil(lowHz * fftSize / sampleRate)`, `kHi = floor(highHz * fftSize /
  sampleRate)`, clamped to `[0, numBins)`.
- Analysis geometry is the **component's own** `fftSize`/`hopSize` for SC-004 arms (a)–(d); arm (e)
  fixes it at `fftSize = 1024`, hop `256` **on both sides** so the two runs are computed on the same
  grid and are numerically comparable.
- "Reduction" is **always** the ratio `flux(amount=0) / flux(amount=1)`, never the subtractive form —
  at the shipped endpoints the ratio reads ≈ 4.8 at tilt 0 and ≈ 114 at tilt +1 while the subtractive
  form reads 1.10 and 1.91 and **fails SC-004 (b)'s factor-of-2 gate on a correct build**.

The helper gets one self-test case in the same TU that uses it (`SpectralSmear_FluxHelperSanity`,
untagged): white noise scores a high flux, a steady sine a near-zero one, and a signal passed through
a known one-pole magnitude smoother scores between them — so a helper bug cannot be mistaken for a
component bug.

### S13.2 Criterion-by-criterion

**SC-001 — `SpectralSmear_FlatnessVsDecoherence`, `[long]`** (`spectral_smear_spectral_test.cpp`)
1 kHz sine at −12 dBFS, 40 s, five separate renders at `decoherence ∈ {0, .25, .5, .75, 1}`,
`smearAmount = 0`, tilt 0.
*Arm (a):* flatness is the **mean over N ≥ 200 non-overlapping 4096-sample windows tiling the last
20 s** (234 windows at 48 kHz), each window passed to `calculateSpectralFlatness`
(`signal_metrics.h:326`). Tiling is mandatory, not stylistic: that helper caps `fftSize` at 4096 and
windows `signal[0 .. fftSize)` (`:335-337`, `:349-352`), so handing it a 20 s span measures 85 ms of
it and makes the render length, the span and the warm-up discard inoperative. Assert: non-decreasing,
and each of the four steps ≥ 10 % relative increase.
*Arm (b):* out-of-mainlobe energy fraction `ρ(d) = 1 − E(1 kHz ± 2·sampleRate/fftSize) / E(bins > DC)`,
averaged over ≥ 100 non-overlapping 8192-sample Hann frames (5.86 Hz/bin). Analytic prediction
`ρ(d) = 1 − (sin(dπ)/(dπ))²` = `{0, 0.19, 0.59, 0.91, 1.00}`. Assert: non-decreasing; `ρ(0) ≤ 0.02`;
`ρ(1) ≥ 0.80`; each step ≥ 0.10 absolute; each `ρ(d)` within ±0.10 of the prediction. Every quantity
is an energy **fraction**, so it is invariant to the make-up gain and to drive level — which is why
the floor-anchored ratio `flatness(1)/flatness(0)` is deliberately **not** used (`flatness(0)` is the
leakage/round-off floor of a windowed pure tone and differs between MSVC, GCC and AppleClang).
*Arm (c):* inter-channel correlation of the output falls monotonically with `decoherence` and reaches
≤ 0.3 at `d = 1` — the observable consequence of FR-044's two streams.

**SC-002 — `SpectralSmear_Latency`** (`spectral_smear_test.cpp`)
(a) `getLatencySamples() == fftSize` for `{512, 1024, 2048, 4096}`, `0` before `prepare()`.
(b) Kronecker impulse at input index `2 * fftSize`, defaults: output peak index **exactly**
`impulseIndex + getLatencySamples()` (no tolerance), and every sample in `[0, latency)` exactly
`0.0f`. The impulse position and the exactness are both S11's consequences and the comment in the TU
must carry the periodic-Hann reason.
(c) Repeat (b) at `smearAmount = 1`, `decoherence = 1`, tilt ±1: latency unchanged, leading zeros
still exact. **No peak-index claim** in this arm — at `d = 1` there is no impulse left to locate.
(d) `PrepareConfig{.fftSize = 2048, .enabled = false}`: `isPrepared()` true, `isEnabled()` false,
`getLatencySamples() == 0`, `getAllocatedBytes() == 0`, and a 10 s white-noise render comes back
**bit-identical** (a true bypass — exact equality is the assertion, and `lint-float-bit-goldens.js`
is satisfied because this is "these bytes were not written", not a pinned computation).
(e) Enabled/disabled twins agree on `getFftSize/getHopSize/getNumBins/getSampleRate`; on the disabled
one the three applied reads equal the just-set targets exactly while latency and bytes stay 0.

**SC-003 — `SpectralSmear_NullAtZero`** (`spectral_smear_test.cpp`)
20 s white noise at −12 dBFS, plus a 5-partial-tone arm; `smearAmount = 0`, `decoherence = 0`, tilt
swept `{−1, 0, +1}` (tilt must be inert when amount is zero). Align by `getLatencySamples()`, discard
`2 * fftSize`, compare over the steady region. Assert peak `|residual| ≤ 1.0e-4` and residual RMS
≤ −80 dBFS relative to input RMS. Hann at 75 % with the synthesis window is COLA (`stft.h:224-227`),
so the round trip is analytically exact and the tolerance covers FFT-pair round-off only.

**SC-004 — `SpectralSmear_MagnitudeMemory`, `[long]`** (`spectral_smear_spectral_test.cpp`)
The discriminating criterion for the half SC-001 cannot see; all arms use S13.1's helper.
(a) Noise band-limited to `[20 Hz, 16 kHz]`, AM at 4 Hz; full-band flux of the **output** at
`smearAmount ∈ {0,.25,.5,.75,1}`, tilt 0, `decoherence = 0`, after a `2 * fftSize` discard. Assert
non-increasing; `flux(1) ≤ 0.5 · flux(0)`; and the **anti-vacuity clause** — each of the four steps
falls by ≥ `0.08 · flux(0)` (reachable only because FR-021 makes `amount` a blend; under the rejected
pole scaling the first four points are all "no smearing" — D-12).
(b) At `smearAmount = 1`, tilt 0: per-band reduction `flux(0)/flux(1)` for `[20, 200]` Hz and
`[4k, 12k]` Hz; assert `reduction(low)/reduction(high) ≥ 2`.
(c) Controls **pinned and restated in the TU** (`smearAmount = 1`, `decoherence = 0`, `tilt = 0`,
`tauLow = 3.0`, `tauHigh = 0.25`) because the measured quantity is undefined without them: at
`amount == 1` the written magnitude **is** `state[k]`, so the decay is exactly `exp(−t/tau(k))`.
Render ≥ 25 s, gate a 100 Hz tone off at 10 s, measure the decay to −40 dB of the steady level.
Assert within ±25 % of **7.74 s** (`tau(100 Hz) = 1.6814 s` × `ln(100)`; S4.1 carries the arithmetic).
(d) Re-run (b) at tilt +1 and −1: the low/high reduction ratio is strictly larger at +1 and strictly
smaller at −1 than at 0.
(e) Re-run (b) with the component at `fftSize = kMinFftSize = 512` and the measuring STFT fixed at
1024/256 on both sides: the factor-of-2 separation still holds. This is the criterion that justifies
FR-011's raised minimum (D-3).

**SC-005 — `SpectralSmear_BoundednessSoak`, `[long]`** (`spectral_smear_spectral_test.cpp`)
48 kHz, reference geometry, **30 minutes of real audio, no acceleration** (Clarifications Q7 — at
FR-060's ~25 000 ns/block projection this is ≈168 750 blocks ≈ 4–5 s of wall clock, and accelerating
the rate would change the bin spacing and the tau-in-frames ratio the soak exists to measure). Pink
noise at −6 dBFS with 60 s gaps of digital silence (≥ 6 · `kMaxSmearSeconds`, which is the sizing rule
clause (v) depends on); controls swept to extremes on a slow schedule, **and parked at a fixed
reference setting (`smearAmount = 1`, `decoherence = 0.5`, tilt 0, default endpoints) for one full
60 s pink-noise-active window in each 5-minute pass** — six reference windows.
Assert: (i) every output sample finite via `detail::isFinite`; (ii) peak ≤ `kOutputClamp`;
(iii) `getClampEngagements() == 0` at −6 dBFS; (iv) RMS of the **six reference windows** drifts ≤ 0.5 dB
(measured only over pink-noise-active audio and only at equal control settings — SC-006 allows the
make-up ±0.5 dB of its own, so two windows at different `decoherence` would differ by ~1 dB and the
statistic would be measuring the make-up curve, not creep); (v) during each silent gap the output
falls ≥ 40 dB **relative to its pre-gap RMS** within `5 · max(tauLow, tauHigh)` seconds and is
monotonically non-increasing through the rest of the gap to within 1 dB of ripple. Relative, never an
absolute dBFS floor: −80 dBFS absolute from a −6 dBFS steady level needs `8.5·tau` = 85 s at the
extreme pass, longer than the gap, and a correct implementation would fail it.
Run in isolation like every `[long]` case; the tag already keeps it out of the per-push lane.

**SC-006 — `SpectralSmear_CoherenceMakeup`** (`spectral_smear_spectral_test.cpp`)
`smearAmount = 0`; 30 s white noise at each of the five knot values; measure output RMS / input RMS
with the make-up **divided out** through the component's own public
`SpectralSmear::coherenceMakeup(d)`.
(a) **Knots (the informative arm):** each raw ratio within **2 %** of `1 / kCoherenceMakeup[i]` as
shipped. (b) **Interpolant:** with the make-up applied, output RMS within ±0.5 dB of input RMS at
**ten intermediate** `d` values — here the cubic-Hermite interpolant, not the table, is under test.
(c) **Transfer check:** report each measured knot against AetherReverb's shipped table
(`aether_reverb.h:2775`); agreement within 2 % confirms the `sqrt(Σw⁴)/Σw²` argument, otherwise our
measured table ships and the divergence is documented in the header. Splitting (a) from (b) is what
keeps the criterion non-circular: if the shipped knots were the reciprocals of values measured in this
same test, "make-up applied ⇒ RMS restored" would be a tautology.
At settled `d` the FR-046 ramp is from `g` to `g`, i.e. constant, so the ramp does not perturb this
measurement.

**SC-007 — `SpectralSmear_NoAllocation`** (`spectral_smear_test.cpp`)
Inside an `AllocationScope`: 5 000 `processBlock` calls at block sizes `{1, 7, 30, 64, 65, 511, 512,
2048}` interleaved with every FR-050 setter (including `setSmearTimeLow`/`setSmearTimeHigh`) and
`setSeed`. Assert `getAllocationCount() == 0`. Read the count **after** the scope closes or via
`AllocationDetector::instance().getAllocationCount()` inside it — `AllocationScope` latches in its
destructor (`allocation_detector.h:117-119`). **Include `allocation_detector.h` only**, never
`allocation_operator_overrides.h` (S10).

**SC-008 — `SpectralSmear_AllocatedBytes`** (`spectral_smear_test.cpp`)
`getAllocatedBytes()` equals the S10 table **exactly** at `{512, 1024, 2048, 4096}` — the test
recomputes the formula from the public geometry reads rather than hard-coding four literals, so a
geometry change cannot silently invalidate it — and is ≤ 128 KiB at 4096. The sub-object figure is
**computed and printed** (`INFO`/`WARN`), never asserted. Second arm: `enabled = false` ⇒ 0 at every
geometry.

**SC-009 — `SpectralSmear_SeedDeterminism`** (`spectral_smear_test.cpp`)
(a) Same seed, same prepare, same control script ⇒ `compareFingerprints` passes at the shipped
tolerances (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`). (b) **Anti-vacuity:** different
seeds ⇒ the same comparison **fails**. (c) Two scripts differing **only** in the length of a mid-render
park at `decoherence == 0` (both a whole number of frames, both ending at or before absolute sample
`S`) ⇒ fingerprints over the **fixed absolute window** `[S + 2·fftSize, end)` match. The window is
pinned absolutely on purpose: FR-043 burns one draw per bin per channel per frame regardless of the
gate, so the stream position is a function of **elapsed frames**, not dwell time. Stating it as
"aligned to the moment the park ends" would fail a correct implementation and pass one that skipped
the draws — i.e. exactly inverted. (d) `reset()` + the same script reproduces (a).

**SC-010 — `SpectralSmear_SampleRateIndependence`** (`spectral_smear_spectral_test.cpp`)
SC-004 (c)'s decay at 44 100 / 48 000 / 96 000 Hz with the controls **restated** (`smearAmount = 1`,
`decoherence = 0`, tilt 0, `tauLow = 3.0`, `tauHigh = 0.25`); analytic target 7.74 s at every rate
because `tau` is in seconds (FR-065). Assert the three measured −40 dB times agree within ±10 % of one
another and each is within ±25 % of the target. Second arm: `getLatencySamples() == fftSize` at every
rate (latency is a sample count, not a time).

**SC-011 — `SpectralSmear_PartitionInvariance`** (`spectral_smear_test.cpp`)
The same 60 s input rendered (i) in uniform 512-sample blocks and (ii) with a pseudo-random schedule
drawn from `{1, 7, 30, 63, 64, 65, 300, 512, 1024, 4096}` produces outputs that pass
`compareFingerprints` **sample-aligned with no offset correction** — the direct assertion of FR-014's
counter.

**SC-012 — `SpectralSmear_PreEchoAndClicks`** (`(a)(b)` in the spectral TU, `(c)` in the main TU)
(a) 2 s digital silence then a 1 kHz burst at −6 dBFS; align by latency; RMS in
`[onset − 2·fftSize, onset − fftSize)` ≤ **−90 dBFS** relative to the burst peak at every corner of
`{smearAmount, decoherence} ∈ {0,1}²` and both tilt extremes. The window `[onset − fftSize, onset)` is
*expected* to carry energy (inherent STFT window spread) and is **reported, not asserted**.
(b) **Anti-vacuity:** the post-burst decay is longer at `smearAmount = 1` than at 0.
(c) Step `smearAmount` 0→1, `decoherence` 0→1 and tilt −1→+1 in a single block during a sustained
tone; `ClickDetector` (`artifact_detection.h:105`, `:130`; config `{48000, 512, 256, 5.0f}`) reports
zero clicks and the peak inter-sample delta exceeds the reference render's by ≤ 6 dB. The
`decoherence 0 → 1` arm is the one that fails without FR-046 (S7.3 carries the ~55 %-step arithmetic).

**The reference render in the 6 dB arm is the DESTINATION setting held constant for the whole
render** — for the decoherence arm, `decoherence = 1` set before `prepare()` and never stepped; for
the smear arm, `smearAmount = 1` held; for the tilt arm, `tilt = +1` held — **and the delta is taken
over the same absolute sample window in both renders**. The spec's "the un-stepped render" is
ambiguous and one of its two readings fails a correct implementation: at `decoherence = 1` the output
is a narrowband process built from per-frame-randomised phases across the tone's ~3 occupied bins, and
FR-042's make-up restores its RMS, so its crest factor — and therefore its peak inter-sample delta —
is materially higher than a pure 1 kHz tone's `A·2π·1000/48000 = 0.131·A` **with no click present at
all**. Comparing the stepped render against the *origin* setting would therefore measure crest factor
and could exceed 6 dB on a correct build. Comparing against the destination measures what the arm
intends: the transient cost of the step itself. The "zero clicks" assertion is unaffected — it is
self-referenced to each render's own local statistics. S17 C-10 carries the spec wording correction.

**SC-013 — `SpectralSmear_CpuBudget`, `[.perf]`** (`spectral_smear_perf_test.cpp`) — see S15.

**SC-014 — build + diff gate, no new test.** `git diff --stat` shows only
`dsp/include/krate/dsp/processors/spectral_smear.h` under `dsp/include/`;
`systems/atmosphere_engine.h` and `effects/aether_reverb.h` are byte-unchanged (verify with
`git diff --stat -- dsp/include/krate/dsp/systems/atmosphere_engine.h dsp/include/krate/dsp/effects/aether_reverb.h`
→ empty). The FR-072 consumer set must be green.

**SC-015 — tooling.** `node tools/lint-layers.js`, `lint-odr.js`, `lint-nonfinite-symbols.js`,
`lint-float-bit-goldens.js`, `lint-simd-aligned-loadstore.js`,
`lint-allocation-operator-overrides.js` and `check-portability.js` all pass; the four TUs are
registered by name; `spectral_smear_nonfinite_test.cpp` **only** is in the `-fno-fast-math` block.

**SC-016 — `SpectralSmear_NonFinite`** (`spectral_smear_nonfinite_test.cpp`, the `-fno-fast-math` TU)
Inject NaN and ±Inf **built from bit patterns through a volatile sink** — never
`std::numeric_limits<>::quiet_NaN()/infinity()`, which fold to finite garbage under `-ffast-math`
(`resonance_drift_network_nonfinite_test.cpp:56-66` is the transcribable idiom) — into one channel for
one block at `smearAmount = 1`. Assert: (i) no non-finite output sample after the injecting block;
(ii) `getPoisonEngagements() >= 1`; (iii) measured from the **last injected sample `E`**, output RMS
over the absolute window `[E + 2·fftSize, E + 3·fftSize)` is within **0.5 dB** of an un-injected
reference's RMS over the **same absolute window** (reachable in one frame only because FR-062 re-arms
the priming flag; a zero-initialised recovery misses it by 12–26 dB); (iv) no more than
`ceil(fftSize/hopSize) + 1 = 5` consecutive frames synthesise silence. S8 carries the arithmetic for
why the window sits where it does. Arms (iii)/(iv) are the direct assertion of the deliberate
deviation from Atmosphere's latch.

**(iv) — the frame count is 5, not 4, and the assertion is on an output RUN LENGTH.** Two corrections
to revision 1, which said "covered by 4 consecutive analysis frames" in prose (S8) while asserting a
bound of 5:
- *The count.* A span of `L = 512` injected samples is overlapped by
  `floor((L - 1 + N - 1)/hop) + 1 = floor(2558/512) + 1 = 5` analysis windows at `N = 2048`,
  `hop = 512`. The bound `ceil(fftSize/hopSize) + 1 = 5` is therefore **exactly tight and exactly
  correct** — it is the maximum over injection alignments — and it is **kept at 5, not padded**
  (see Review notes). Window tapering does not reduce the count: `generateHann` is periodic so
  `window[0] == 0.0f`, but `NaN * 0.0f` is `NaN`, so a frame whose window touches even one injected
  sample still poisons. The prose that said "4" is what a reader would have trusted into asserting
  `<= 4` and failing a correct build.
- *The measurement.* "Frames synthesise silence" is **not observable** — S1.4 exposes no per-frame
  hook and none is added. The observable is the **maximal run of consecutive near-silent output
  samples**, and it is shorter than `silentFrames * hopSize` because each output sample is covered by
  `numOverlaps = fftSize/hopSize = 4` frames and is silent only when **all four** are:
  `runLength = (silentFrames - numOverlaps + 1) * hopSize = (5 - 4 + 1) * 512 = 1024 samples`.
  So: locate the longest run of consecutive output samples after the first injected sample whose
  magnitude is below a floor stated relative to the un-injected reference render's local RMS
  (-80 dB of it), and assert
  `runLength <= (ceil(fftSize/hopSize) + 1 - fftSize/hopSize + 1) * hopSize = 2 * hopSize`.
  The run-length arithmetic goes in a comment beside the assertion in the TU: the two quantities
  (silent *frames* and silent *samples*) differ by a factor of 2.5 here and the wrong one is a
  plausible transcription. S17 C-11 carries the spec correction.

**SC-017 — `SpectralSmear_ControlCadence`** (`spectral_smear_test.cpp`)
The criterion that closes FR-013 (a)'s declared silent-failure mode. With everything else static,
call `setSmearAmount(1.0f)` **before the first `processBlock` call** — see the origin rule below —
then sample `getAppliedSmearAmount()` after each
of the first 64 blocks and assert the trajectory matches `1 − coeff^f` to within `1e-4`, where
`coeff = calculateOnePolCoefficient(kControlSmoothMs, sampleRate / hopSize)` (0.34424 at the reference
geometry) and
```
f = max(0, floor((samplesProcessed − fftSize) / hopSize) + 1)      // Clarifications Q3
```
— **not** `floor(samplesProcessed / hopSize)`, which over-counts by `fftSize/hopSize − 1 = 3` frames
at every 75 % geometry because the smoothers only advance inside `while (stft_[0].canAnalyze())` and
`canAnalyze()` is `samplesAvailable_ >= fftSize_` (`stft.h:137`). Run at two partitions (512 and
SC-011's pseudo-random schedule) and two `fftSize` values (2048 and 512). A double advance shows up
immediately as `f` doubling; a per-block advance shows up as the trajectory decoupling from `hopSize`.

**Two things the criterion's own wording leaves open, pinned here so the case cannot be written
wrong** (S17 C-12 carries the spec correction):

- **The origin of `f`.** `1 − coeff^f` is only the correct trajectory if `f` counts frames elapsed
  **since the `setSmearAmount(1.0f)` call**, whereas the formula as written counts frames since
  `prepare()`. The criterion says the setter is issued "at a known sample index", and a test author
  who warms up for a few blocks first — the natural reading — would assert a trajectory offset by the
  intervening frames and **fail a correct component**. The rule is therefore: **issue the setter
  before the first `processBlock`**, so `samplesProcessed` is counted from the first sample of the
  render and the two origins coincide. If a variant ever needs the setter mid-render, the formula must
  become `f = max(0, floor((samplesProcessed − callIndex − fftSize) / hopSize) + 1)` with `callIndex`
  the absolute input sample index at which the setter was called — stated so the generalisation is
  written down rather than rediscovered. The smoother's shape supports this exactly:
  `process()` is `current_ = target_ + coefficient_ * (current_ − target_)` (`smoother.h:205`) with
  `coefficient_ = calculateOnePolCoefficient(50.0f, frameRate_)` (`configure()` at `smoother.h:160-164`,
  helper at `:77-93`), so from a snapped `0.0f` the trajectory is exactly `1 − coeff^f` in frames since the
  target changed.
- **The comparison is `≤ 1e-4`, not `<`.** `process()` snaps once `|current_ − target_| <
  kCompletionThreshold = 1e-4f` (`smoother.h:199-201`, `:53`), so at the frame where the trajectory
  crosses that threshold the predicted and actual values differ by **exactly** up to `1e-4`. A strict
  `<` at a `1e-4` tolerance is a coin flip on the last frame before the snap.

**SC-018 — `SpectralSmear_MagnitudePriming`** (`spectral_smear_test.cpp`)

**The measurement is in the sample domain, not the bin domain.** The criterion's stated form — push
exactly one frame, compare the first written frame's magnitudes against the analysed ones per bin at
`|Δ| ≤ 1e-6`, "by re-analysing that hop region" — **cannot be executed at that tolerance**, and two
facts from this plan prove it:

- **FR-014 / S5.4:** after `prepare()`/`reset()` the FIFO pop emits `fftSize` literal zeros before any
  synthesised sample. Push exactly `fftSize` samples and the output is identically `0.0f` — frame 0 is
  not observable at all.
- **S11:** the only frame-0-exclusive output is the first `hopSize` samples of `[fftSize, 2·fftSize −
  hop)`, which is the `OverlapAdd` COLA ramp-up that *every* criterion is told to skip. Those 512
  samples are one `w²`-tapered, COLA-incomplete window (`synthesize` accumulates at offset 0 with
  `w² · colaNormalization_`, `stft.h:289-315`; COLA needs 4 frames, `:249-262`), so they cannot be
  re-analysed onto the component's 1025-bin grid at all, let alone to `1e-6` per bin. FR-022's
  priming would be left with no executable criterion.

**What is executed instead, same discriminating power, no bin-domain claim.** At
`smearAmount = 1`, `decoherence = 0`, `tilt = 0`, `tauLow`/`tauHigh` at the shipped defaults:

*Arm (a), after `reset()`.* Render exactly `fftSize + hopSize` samples of a 1 kHz burst and take
`out[fftSize, fftSize + hopSize)` — frame 0's exclusive contribution. The **reference** is the *same
input through the same component at `smearAmount = 0`*, i.e. FR-021's exact-identity path, which
leaves the analysed spectrum untouched and therefore reproduces the bare `STFT`/`OverlapAdd` round
trip including the identical ramp-up taper. (A bare `STFT` + `OverlapAdd` pair configured identically
is an equally valid reference and the TU may use it; the ramp-up cancels either way, which is the
whole point.) Assert `rms(actual − reference) / rms(reference) ≤ −60 dB`.
- A **primed** memory writes `state[k] = mag[k]` on frame 0, so at `amount = 1` the written magnitude
  **is** the analysed magnitude and the residual is FFT round-trip round-off — tens of dB below the
  gate.
- A **zero-initialised** memory writes `state[k] = mag[k] + p·(0 − mag[k]) = (1 − p)·mag[k]`, i.e.
  ≈ 1 % of amplitude at the reference geometry (`p ≈ 0.99` across the band), leaving a residual of
  ≈ 99 % of the reference — about **−0.1 dB**, a margin of more than 30 dB over the gate. The arm
  cannot pass by accident and cannot fail on round-off.

*Arm (b), after a poison clear.* Prepare two instances identically, one at `smearAmount = 1` and one
at `smearAmount = 0`, inject the **same** single non-finite sample into both at the same absolute
index (FR-062's accumulator test is independent of `amount`, so both poison and both synthesise the
same silent frames — S8), then feed both the same burst. Locate `G`, the last index of the maximal
silent run after the injection in the **reference** render, and compare `[G + 1, G + 1 + hopSize)` —
the priming frame's exclusive contribution — with the same `≤ −60 dB` relative-RMS gate. This is the
direct assertion that FR-062 (a) re-arms the priming flag rather than merely zeroing the memory.

Neither arm is a bit-exact float golden (`lint-float-bit-goldens.js`): both are relative-RMS
tolerances against a same-run reference. S17 C-5 carries the spec correction to SC-018's wording.

**Edge Cases → named cases** (`spectral_smear_test.cpp` unless stated):

- `SpectralSmear_RenderPathBoundaries` — numSamples 0/1/7/63/64/65/16384, null pointers, unprepared,
  disabled-then-re-prepared.
- `SpectralSmear_ControlClamps` — every setter at range ends, exactly `0.0f` and `1.0f` and the
  smallest positive float above zero (which must take the **full** path, not the identity gate);
  setters before `prepare()`; `setSeed(0)` keeps both streams live and distinct. **Non-finite
  arguments are NOT here** — that arm is `SpectralSmear_NonFiniteSetters` in the `-fno-fast-math` TU
  (S13's table carries the reasoning).
- `SpectralSmear_Geometry` — fftSize 0/1/100/513/5000/`SIZE_MAX`; sampleRate 0 and negative; double
  `prepare()` at different rates and sizes; and the S2-step-1 re-prepare path (a good prepare followed
  by a bad-rate prepare leaves `isPrepared()` false and all four geometry reads at `0`, while
  `getAllocatedBytes()` reads `0` meaning "will not render", not "holds no heap").
- `SpectralSmear_PoleTableBounds` (white-box) — `poleForTau` at synthetic extremes stays inside
  `[0, kMaxPole]`; a derived `tau` far above `kMaxSmearSeconds`; a degenerate `sampleRate · tau`
  product; and the two reachable extremes from S12.
- **`SpectralSmear_OutputClamp` — NEW, and it is the only thing that can detect FR-061 failing.**
  Traceability maps FR-061 to SC-005 alone, and **both** of SC-005's relevant arms are satisfied by a
  build with no clamp at all: (ii) "peak ≤ `kOutputClamp`" at −6 dBFS pink noise, where S12's own
  boundedness argument (state ≤ `sup|mag|`, make-up ≤ ~2.0) puts the peak near 1.0, and (iii)
  `getClampEngagements() == 0`, which asserts the path is **not** taken. Deleting the `std::clamp`
  **and** deleting the `++clampEngagements_` both leave the suite green, and FR-054's
  `getClampEngagements()` reader is never exercised in the non-zero direction anywhere else in this
  plan. So: drive a prepared, enabled instance at the reference geometry with an input of amplitude
  ≥ 6.0 (or ≥ 3.0 with `decoherence = 1`, where the ~2× make-up carries it past 4.0), and assert
  (1) every output sample satisfies `|x| ≤ kOutputClamp`, (2) `getClampEngagements() > 0`, and
  (3) `reset()` and a fresh `prepare()` each return the counter to `0` (FR-054). SC-005 (iii) keeps
  its meaning unchanged — it is the "not a working part at realistic drive" arm, and this case is the
  "it is a working part when it must be" arm. S17 C-6 carries the Traceability addition.

**Two Edge Cases the spec states as behaviour and revision 1 left with no case at all**
(`spectral_smear_spectral_test.cpp`, `[long]`, both driving S13.1's flux helper):

- **`SpectralSmear_TimeConstantLaw` — the endpoint-ordering behaviour.** The spec's parameter-extremes
  list requires that `tauLow == tauHigh` be "legal, and SC-004 (b)'s separation must then vanish
  rather than invert", and that `tauLow < tauHigh` be "legal and not clamped … the component must not
  silently swap the endpoints". Every SC-004 arm runs at the shipped defaults `tauLow = 3.0 >
  tauHigh = 0.25`, and `SpectralSmear_ControlClamps` tests no ordering behaviour, so **nothing in
  revision 1 would fail if an implementer added `if (tauLow_ < tauHigh_) std::swap(tauLow_,
  tauHigh_);` to `rebuildPoleTables()`** — precisely the silent failure the Edge Case names. The
  behaviour is also load-bearing for another criterion: SC-005 (v) references
  `tauMax = max(tauLow, tauHigh)` "as configured for that pass", which is only meaningful if the
  inverted law is real. Two arms, both computing the per-band reduction ratio
  `R = reduction([20, 200] Hz) / reduction([4k, 12k] Hz)` exactly as SC-004 (b) does, at
  `smearAmount = 1`, `decoherence = 0`, `tilt = 0`:
  (i) `tauLow = tauHigh = 1.0` ⇒ `R ∈ [0.8, 1.25]` — the separation **vanishes** and does not invert;
  (ii) `tauLow = 0.25`, `tauHigh = 3.0` ⇒ `R ≤ 0.5` — strictly inverted, which **fails immediately**
  if the endpoints are silently swapped (a swap would reproduce arm SC-004 (b)'s `R ≥ 2`).
- **`SpectralSmear_DcNyquistSmear` — FR-025, which currently has no criterion that can see it fail.**
  Traceability gives FR-025 SC-004 (a) and SC-005 (v). SC-004 (a) is a normalised **full-band** L1
  flux: at the reference geometry, omitting 2 of 1025 bins perturbs it by O(0.2 %) against gates of
  "each of the four steps falls by ≥ `0.08·flux(0)`" and "`flux(1) ≤ 0.5·flux(0)`" — three orders of
  magnitude of slack. SC-005 (v) is a broadband relative-RMS decay and is equally blind to two bins.
  An implementer who copies FR-040's `for (k = 1; k + 1 < numBins; ++k)` bounds into the magnitude
  pass — the most likely mistake, since the two loops sit adjacent in S6/S7.1 — passes every existing
  criterion. The case therefore applies SC-004 (c)'s *shape* at the two excluded bins:
  at `smearAmount = 1`, `decoherence = 0`, `tilt = 0`, shipped endpoints, render ≥ 20 s of an input
  carrying deliberate DC **and** Nyquist content whose levels **step at a known instant** (a DC offset
  stepping `0 → 0.5` at `t = 5 s`, plus an alternating `±0.25` Nyquist component gated on at the same
  instant, on top of a small band-limited noise floor so the frames are never degenerate).
  Both components are read out of the *output* by sliding-window means over `4 · hopSize` samples:
  DC as `mean(out[n])`, Nyquist as `mean(out[n] · (−1)^n)` — a demodulation, cheaper and sharper than
  a band-energy integral, and exactly the two quantities bins 0 and `numBins − 1` carry.
  **Assert** that each rises as a first-order step rather than instantaneously: the time to reach
  `1 − 1/e` of the settled value is within ±25 % of the `tau` FR-030 implies for that bin —
  `tau(bin 0) = tauLow = 3.0 s` (`u(0) = 0` by the bin-0 rule in S4.1) and
  `tau(bin numBins − 1) = tauHigh = 0.25 s` (`f = 24 kHz ⇒ u = 1` after the clamp). The `k`-from-1
  bug makes both rise times ≈ 0 and fails both assertions by orders of magnitude; a correct build
  passes with the same ±25 % tolerance SC-004 (c) and SC-010 already use.
  S17 C-6 carries the Traceability addition for FR-025.

---

## S14. Build integration — the exact edits

1. **`dsp/tests/CMakeLists.txt`, before the `)` at `:294`** (i.e. after the Vorago Phase 1 block at
   `:287-293`), inside `add_executable(dsp_processors_tests`:
   ```cmake
       # Vorago Phase 4 (specs/vorago-phase4-spectral-smear)
       unit/processors/spectral_smear_test.cpp
       unit/processors/spectral_smear_spectral_test.cpp
       unit/processors/spectral_smear_perf_test.cpp
       unit/processors/spectral_smear_nonfinite_test.cpp
   ```
2. **`dsp/tests/CMakeLists.txt`, before the `PROPERTIES` line at `:821`** (after
   `unit/systems/resonance_drift_network_nonfinite_test.cpp` at `:820`), with the house comment:
   ```cmake
           # Vorago Phase 4: this TU owns EVERY non-finite value in the phase - SC-016's
           # NaN/Inf audio injection AND FR-009's non-finite setter arm - because both
           # build their values from bit patterns through a volatile sink and need IEEE
           # semantics to assert on them. ONLY this one of the four Phase 4 TUs is listed;
           # the other three must NOT be, and none of them may name a non-finite value:
           # std::numeric_limits<float>::quiet_NaN()/infinity() fold to finite garbage on
           # the -ffast-math legs, so such a test passes vacuously or reds at random.
           # spectral_smear_test.cpp and spectral_smear_spectral_test.cpp stay out so the
           # FR-008 guards are proved in the /fp:fast + -ffast-math mode the header ships
           # in (the resonance_drift_network_test.cpp:38-42 precedent). The perf TU stays
           # out too: -fno-fast-math would change the figures its baselines are pinned to.
           unit/processors/spectral_smear_nonfinite_test.cpp
   ```
3. **`dsp/lint_all_headers.cpp`, insert at `:138`** (alphabetically between
   `spectral_morph_filter.h` at `:137` and `spectral_tilt.h` at `:138`):
   ```cpp
   #include <krate/dsp/processors/spectral_smear.h>
   ```
4. **`tests/test_helpers/spectral_flux.h`** — new file. **No CMake edit**: `test_helpers` is an
   INTERFACE library exporting that directory (`tests/test_helpers/CMakeLists.txt:7-12`), and
   `dsp_processors_tests` already links it (`dsp/tests/CMakeLists.txt:300`).

Nothing else changes. No plugin CMake, no root CMake, no CI file: this phase adds no target.

**Build + run sequence** (Windows, full CMake path, per CLAUDE.md):

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release \
    --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear*" 2>&1 | tail -5
# full layer + consumer regression (FR-072):
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release \
    --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests \
             dsp_effects_tests seraphis_tests innexus_tests
# perf, ALONE, nothing else running:
node tools/run-cpu-tests.js dsp_processors_tests
```

`[long]` cases (SC-001, SC-004, SC-005) run locally and in the nightly lane; the `[.perf]` case is
developer-run. Neither reaches the per-push CI filter
`~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`.

---

## S15. CPU budget — the plan for SC-013 and FR-060's ladder

**Two numbers, and they must not be confused.** The roadmap's budget is 0.5 % of one core at 48 kHz =
53 333 ns per 512-sample block. SC-013 inherits the Phase-1 gate idiom, whose
`static_assert(kBaseline * kRegressionFactor <= kReferenceNs)` with `kRegressionFactor = 1.5`
(`vorago_p1_perf_test.cpp:109-110`) caps any checked-in baseline at
**`53 333 / 1.5 = 35 555 ns/block = 0.333 % of one core`**. **35 555 ns is the binding figure for this
phase.** A measurement in `[35 556, 53 333]` ns is *not shippable* even though it satisfies the
roadmap sentence: no baseline can be encoded for it, so it takes the lever ladder and then the
stop-and-surface rule.

**The projection is measured, not assumed.** Atmosphere's structurally identical stereo blur stage
(STFT↔OverlapAdd, fftSize 1024, 75 %, full per-bin phase randomisation) costs ~23 000 ns/block
(`atmosphere_engine_perf_test.cpp:376`), corroborated by its own `(b) − (a)` baseline difference of
25 510 ns (`:241-242`). This component adds, per bin: one FMA for the integrator, one FMA for FR-021's
blend, one ordered compare (FR-024), one shared per-frame table lerp, and the magnitude half of a
polar round trip `SpectralBuffer` already pays for. Projection: **~0.25–0.30 %, i.e. 26 667–32 000
ns/block**, 10–33 % under the effective ceiling.

**Cost is approximately geometry-independent** at fixed 75 % overlap: the per-block frame count is
`2048/N` and the per-frame cost is `O(N log N) + O(N)`, so the products go as `2048·log N` and `1024`
— the same reasoning Atmosphere recorded when it declined to shrink its blur FFT as a cost measure
(`atmosphere_engine_perf_test.cpp:327-332`).

**TU shape**, transcribed from `vorago_p1_perf_test.cpp:76-125`: `kBlockSize = 512`,
`kBlockBudgetNs = 512/48000·1e9 = 10 666 667`, `kReferenceNs = kBlockBudgetNs * 0.005 = 53 333`,
`bestTrialNs` = best-of-25 × 500 blocks after 400 warm-up, **four** configurations each with its own
checked-in baseline and provenance comment (machine, five consecutive idle runs, spread):

**(a) the transparent cost — `PrepareConfig{.fftSize = kDefaultFftSize, .enabled = true}` with default
control values (`smearAmount = 0`, `decoherence = 0`, `tilt = 0`), both identity gates engaged, the
full STFT round trip still paid.** The configuration is spelled out here and must be spelled out in
the TU, because revision 2 flipped `PrepareConfig::enabled` to default **`false`** (FR-019,
Clarifications Q6) and the criterion still said "defaults". Under the new default, a literal reading
prepares a **true bypass** whose `processBlock` returns at the first guard (S5.1) and measures nothing
— which collides head-on with SC-013's own anti-no-op floor
`static_assert(kBaseline >= kReferenceNs / 50.0)` = 1 066 ns: a bypass measures tens of ns, so no
honest baseline could be checked in, and the runtime gate would pass vacuously against whatever
fabricated number satisfied the assert. The spec's intent is the opposite — D-11 says SC-013 (a)
"measures precisely that configuration at roughly Atmosphere's ~0.2 %", i.e. an **enabled** instance
at default control values. (If a bypass figure is ever wanted, it is a separate, explicitly-labelled
configuration **exempt from the anti-no-op floor** — not this one.) S17 C-13 carries the spec wording
correction.

**(b)** reference geometry, `smearAmount = 1`, `decoherence = 1`, tilt written every block — the worst
case. The endpoints are still **not** swept here: tilt is the modulation target; the endpoint setters
get their own configuration (d).
**(c)** `fftSize = 512`, same worst-case controls — the highest frame-rate geometry.
**(d) NEW — `fftSize = kMaxFftSize = 4096`, worst-case controls, plus one `setSmearTimeLow()` call per
block**, so the deferred pole-table rebuild (S4.3) fires once per block at the geometry where it is
most expensive (`numBins = 2049` ⇒ ~14 300 transcendentals). This exists because the rebuild is the
one operation in the component with a large, geometry-scaled **synchronous** cost that FR-006 puts on
the audio thread, and revision 1 left it absent from every criterion. Its baseline is checked in like
the others; if it lands over the 35 555 ns ceiling, the levers below apply to it too (the natural
first lever is a coarser rebuild cadence, never a relaxed number).

Each baseline carries `static_assert(kBaseline * 1.5 <= kReferenceNs)` **and** the anti-no-op floor
`static_assert(kBaseline >= kReferenceNs / 50.0)`; the runtime gate is
`REQUIRE(measured <= kBaseline * 1.5)`.

**If the measurement exceeds 35 555 ns/block, reduce cost — never raise the baseline, never relax the
budget, never shrink the workload.** The pre-approved levers, in order: (i) the two identity-gate
skips (already specified, FR-021/FR-041); (ii) fuse the integrator and the blend into one pass reading
the shared per-frame pole table, so the per-bin work is two FMAs and a compare (already the S6 shape);
(iii) raise the default `fftSize` (cheaper per block by the `log N` term). If none suffices,
**stop and surface to the user with the measurement**.

Baselines are measured on the Phase-1/2/3 reference machine (13th Gen Intel Core i9-13900HX,
Windows 11, MSVC Release, `build/windows-x64-release`) through `node tools/run-cpu-tests.js`, so the
isolation and 20 s settle rules apply. A 6.4 % run-to-run spread with ~14 % session drift under
sustained benchmarking is the documented reality (`noise_organism_perf_test.cpp:228-236`); a number
measured once is not evidence.

---

## S16. Risks and mitigations

| # | Risk | Why it is real here | Mitigation |
|---|---|---|---|
| R1 | **Wrong pull size / pull outside the drain loop** destroys COLA and presents as a windowing bug | `synthesize()` accumulates at offset 0 unconditionally and `pullSamples` returns **silently** on an oversized request without zeroing (`stft.h:300-308`, `:339-342`) | S5.3's pull is always exactly `hopSize_` and inside the loop; SC-003's null test fails loudly if it is not |
| R2 | **`STFT` ring overflow** — `pushSamples` has no guard | a caller can hand any `numSamples`; Edge Cases renders 16 384 | FR-012's 64-sample chunking bounds `samplesAvailable_` to `fftSize + 64` against an `8·fftSize` ring; SC-007's block-size sweep exercises it |
| R3 | **Latency that depends on the caller's block size** | draining "when available" yields `(ceil(fftSize/chunk) − 1)·chunk` — 960/1020/1022 measured at chunk 64/30/7 (`aether_reverb.h:643-660`) | FR-014's counter (S5.4); SC-002 (b) and SC-011 assert it directly |
| R4 | **Double-advanced control grid** — invisible to every other criterion | it merely halves the smoothing time; still click-free, still the same cost | FR-053's applied reads + **SC-017**, with Q3's corrected frame formula |
| R5 | **Denormals** in the magnitude memory at long `tau` | geometric decay toward zero is exactly the denormal-generating shape, and a host may not have set MXCSR | FR-024's ordered-compare flush **plus** the process-wide FTZ/DAZ; the compare is `-ffast-math`-proof and NaN-transparent (so poison still reaches FR-062) |
| R6 | **NaN survives the poison clear via phase** | `0.0f · cos(NaN)` is NaN; zeroing magnitudes alone is insufficient | S8 zeroes **magnitude and phase** per bin on the poison path |
| R7 | **`std::clamp` does not reject NaN** | `v < lo` and `hi < v` are both false for NaN, so FR-061's clamp propagates it | the finiteness backstop is upstream (FR-062's frame accumulator); the clamp is explicitly *not* the guard, and S7.3 says so |
| R8 | **Fast-math folding of finiteness tests** | the macOS leg builds with `-ffast-math` | only `detail::isNaN/isInf/isFinite` (bit pattern behind `opaqueFloatBits`); `std::isnan/isinf/isfinite` banned and linted; SC-016's TU is the single `-fno-fast-math` entry |
| R9 | **MSVC-green proves nothing about GCC/AppleClang** | narrowing in brace init, `<bit>` availability, `[[nodiscard]]` diagnostics | designated initialisers for `PrepareConfig`; `node tools/check-portability.js` before committing; WSL probe if a Linux doubt arises (the machine's WSL was broken in Phase 3 — if it still is, syntax-check the new TUs against libstdc++ with MSYS2 g++ as Phase 3 did, and say so in compliance) |
| R10 | **Bit-exact float goldens** creeping into SC-002 (d) / SC-018 | both use exact or near-exact comparisons | SC-002 (d) is "these bytes were not written" (a bypass); SC-018 is now a **relative-RMS ≤ −60 dB** comparison against a same-run reference (S13.2), not a per-bin `1e-6` claim; SC-009/SC-011 use `render_fingerprint.h` tolerances; `lint-float-bit-goldens.js` gates the real thing |
| R17 | **A criterion that cannot observe the requirement it is mapped to** | four found in revision 1: FR-061 (SC-005's two arms both pass with no clamp), FR-025 (2 of 1025 bins under a full-band metric), FR-024 (FTZ/DAZ makes the flush unobservable), FR-013 (c) (bit-identical under swap) | two new cases (`SpectralSmear_OutputClamp`, `SpectralSmear_DcNyquistSmear`) for the two that *are* observable; explicit **"by inspection at the compliance pass"** rows plus a named header comment for the two that are not (S17 C-8, C-9) — never a ✅ without a measurement or a stated inspection |
| R18 | **The pole-table rebuild on the audio thread** | FR-006 puts setters on the render thread; the rebuild is ~14 300 transcendentals at `fftSize = 4096` | deferred behind `poleTablesDirty_` to the top of `processBlock` (at most once per block, S4.3), `@note` on both setters (S1.4), and **measured** by the new SC-013 (d) |
| R11 | **Duplicate-symbol link error** from a second `allocation_operator_overrides.h` include | `dsp_processors_tests` already has its owner at `brownian_drift_test.cpp:28` | S10's rule: include `allocation_detector.h` only; `lint-allocation-operator-overrides.js` enforces |
| R12 | **The flux helper corrupts memory** by pushing a long render into `STFT` in one call | no overflow guard, `8·fftSize` ring | S13.1 mandates `≤ hopSize` pushes with a `while (canAnalyze())` drain, and the helper gets its own sanity case |
| R13 | **SC-004's "reduction" computed subtractively** fails a correct build | the subtractive form reads 1.10/1.91 against a factor-of-2 gate | the ratio form is written into the helper's doc comment and into every arm |
| R14 | **CPU projection is a projection** | it transfers from a structurally identical but not identical stage | SC-013 measures **four** configurations (S15, including (d)'s pole-table rebuild); FR-060's ordered levers then stop-and-surface |
| R15 | **Tilt-scratch staleness** if a table rebuild forgets the dirty flag | the `t != lastResolvedTilt_` guard would then serve a stale table | `rebuildPoleTables()` is the **only** writer of the three tables and its last two statements are `tiltScratchDirty_ = true; poleTablesDirty_ = false;` (S4.3), so no path can change a table without invalidating the scratch; the setters only raise `poleTablesDirty_`. SC-004 (d) sweeps tilt after endpoint changes, and `SpectralSmear_TimeConstantLaw` changes endpoints mid-configuration |
| R16 | **`kMaxFFTSize` shadowing** — an unqualified namespace constant silently means 8192 | `fft.h:44/:47` are namespace-scope `inline constexpr` | the component's bounds are spelled `kMinFftSize`/`kMaxFftSize` and the hazard is recorded in the header (S1.1) |

---

## S17. Spec corrections, and the open items

**C-1 (open, needs a spec amendment — TWO entries, not one).** FR-071 enumerates the files the phase's
`git diff --stat` may touch: "the new header, the four new test TUs, `dsp/tests/CMakeLists.txt`,
`dsp/lint_all_headers.cpp` … and this spec directory". **Two files fall outside that list, and both
must be added to it at the compliance pass:**

1. `tests/test_helpers/spectral_flux.h`. SC-004 and Clarifications Q2 **require** a new flux helper
   "added to `tests/test_helpers/`", which is not on FR-071's list; the two cannot both be satisfied
   as written. **Resolution:** add it as a **new file** (nothing existing is modified, no CMake change
   is needed, SC-014's "only `spectral_smear.h` under `dsp/include/`" and
   "`atmosphere_engine.h`/`aether_reverb.h` byte-unchanged" both remain true). The alternative — a
   file-local helper in `spectral_smear_spectral_test.cpp`, where all SC-004 arms live — also works
   mechanically but contradicts Q2's explicit wording.
2. **`specs/Vorago-roadmap.md`.** C-4 below records the roadmap amendment as **already applied**, and
   `git status` confirms `M specs/Vorago-roadmap.md` in the working tree. That file is not inside
   `specs/vorago-phase4-spectral-smear/`, so it is outside FR-071's enumerated list too — which means
   **FR-071's gate is already red before implementation starts** unless the list is amended. Revision 1
   flagged only entry 1.

SC-014 is unaffected by either: it is scoped to `dsp/include/` plus the two byte-unchanged engine
headers. **Flagged for the user rather than decided silently.**

**C-2 (no spec change; a clarification the header must carry).** FR-009 specifies *substitution* of
the documented default for a non-finite setter argument (`atmosphere_engine.h:911`'s shape), which is
the **opposite** of `ResonanceDriftNetwork`'s FR-008 *rejection* rule
(`resonance_drift_network_nonfinite_test.cpp:41-47`). Both are legitimate; a reviewer arriving from
Phase 3 will assume the wrong one. The header states which rule it follows and why at the setter
block (S8).

**C-3 (no spec change; an implementation consequence the spec left implicit).** FR-062 says the poison
path "zeroes the frame's spectrum". Zeroing magnitudes alone is **not sufficient** — a NaN phase
survives into `reconstructCartesianBulk` as `0 · cos(NaN)` — so S8 zeroes magnitude **and** phase.
Recorded because a literal reading of FR-062 (b) would ship the bug SC-016 (i) then catches.

**C-4 (done).** The roadmap amendment Clarifications Q4 assigns to this stage is applied:
`specs/Vorago-roadmap.md:257-259`. Three lines replaced by three lines, so every roadmap line number
the spec cites is unchanged. (This is the edit C-1 entry 2 asks FR-071 to enumerate.)

**C-5 (spec amendment at the compliance pass — SC-018's measurement).** SC-018 as written asks for a
per-bin `|Δ| ≤ 1e-6` comparison of "the first frame the component writes" against the analysed
magnitudes, "by re-analysing that hop region". That measurement **cannot be executed at that
tolerance** — FR-014 makes frame 0 unobservable after exactly `fftSize` pushed samples, and the only
frame-0-exclusive output is one `w²`-tapered, COLA-incomplete window that cannot be re-analysed onto
the 1025-bin grid. S13.2 replaces it with a sample-domain relative-RMS comparison of the same two
arms, carrying a >30 dB discrimination margin against the zero-initialised failure mode, and the
criterion's text is corrected to match. **No threshold was relaxed to dodge anything:** the old
threshold had no realisable input, and the new one still fails a zero-initialised memory by ~30 dB.

**C-6 (spec amendment — three Traceability/API corrections the plan needs and the spec does not
authorise).**
1. **`poleForTau`.** FR-018 enumerates the public query surface exhaustively and does not list it;
   FR-023 names `poleTable()` — a private member — as the white-box surface the Edge-Case assertion
   drives. Amend FR-018 to include
   `[[nodiscard]] static float poleForTau(float tauSeconds, std::size_t hopSize, double sampleRate) noexcept`,
   and correct FR-023's sentence to name `poleForTau(...)` instead of `poleTable()`, with the reason:
   a pure static lets the Edge-Case case reach synthetic extremes no render can. (The alternative —
   keeping the surface private and granting the test access — would require S13.2's
   `SpectralSmear_PoleTableBounds` to say *how*, and nothing in the spec does.) This is the same
   discipline FR-042's public statics were added under: an API exists because a criterion needs it,
   and the spec says so.
2. **FR-061 gains an enforcing criterion.** Its only Traceability target, SC-005, is satisfied by a
   build with no clamp and no counter increment (both relevant arms assert the path is *not* taken).
   `SpectralSmear_OutputClamp` (S13.2) is the positive arm; FR-054's `getClampEngagements()` reader is
   otherwise never exercised non-zero anywhere in the phase.
3. **FR-025 gains an enforcing criterion.** SC-004 (a) and SC-005 (v) both perturb by O(0.2 %) when
   2 of 1025 bins are omitted, against gates with three orders of magnitude of slack — so the most
   likely implementation mistake (copying FR-040's `k = 1; k + 1 < numBins` bounds into the magnitude
   pass) passes everything. `SpectralSmear_DcNyquistSmear` (S13.2) measures the two bins directly.

**C-7 (spec amendment — FR-036's cost and cadence).** FR-036 states the rebuild as "`3 * numBins`
`exp` calls" and "control-thread cadence". Both are wrong as written. The real cost is
`numBins` `log` + `3·numBins` `pow` + `3·numBins` `exp` (S4.3's table), ~2× the stated figure and
~14 300 transcendentals at `kMaxFftSize`; and FR-006 explicitly puts setters on the **render** thread
("the owner calls setters and `processBlock` from the same thread, as every Vorago component does"),
so there is no control thread to hide it on. Amend FR-036 to (i) carry the corrected count, (ii) say
the setters **mark dirty** and the rebuild happens at the top of the next `processBlock`, at most once
per block, and (iii) point at SC-013 (d) as the configuration that measures it.

**C-8 (no spec change beyond a Traceability qualifier — FR-013 invariant (c)).** FR-013 declares three
invariants that "each fail silently if disturbed". (a) is closed by SC-017 and (b) fails loudly
through SC-003, but (c) "L is always processed before R" is, by the spec's own admission,
**bit-identical under swap today** (FR-044 gives the channels independent streams and there is no
shared state), so none of FR-013's mapped criteria — SC-002/003/011/017 — can observe channel order.
It gets FR-045's treatment: **discharged by inspection at the compliance pass**, with the header
carrying a comment at the channel loop. S5.3's bullet (c) says so too, so the implementer does not
hunt for a test that cannot exist.

**C-9 (Traceability qualifier — FR-024's denormal flush).** FR-024 is mapped to SC-015 (the lint set)
and SC-016 (bit-pattern NaN/Inf); neither touches denormals, and **no render can**: every DSP test
binary enables FTZ/DAZ process-wide (`enable_ftz_daz.h:27-32` from `dsp/tests/dsp_test_main.cpp`), so
result and timing are identical with and without the flush. FR-045 was handled honestly ("by
inspection", with a named header comment); FR-024 is the same situation and gets the same row, plus
the required comment at the flush site (S6).

**C-10 (spec wording — SC-012 (c)'s reference render).** "The peak inter-sample delta does not exceed
the un-stepped render's by more than 6 dB" does not say *which* un-stepped render, and the two
readings disagree: against the **origin** setting the arm compares a random-phase narrowband process
(at `decoherence = 1`, RMS restored by FR-042's make-up) against a sinusoid and can exceed 6 dB from
crest factor alone, failing a correct build. The reference is the **destination** setting held
constant for the whole render, compared over the same absolute window; same for the `smearAmount` and
`tilt` arms (S13.2).

**C-11 (spec wording — SC-016 (iv)).** The criterion's own prose says a one-block injection is
"covered by 4 consecutive analysis frames" while the bound is `ceil(fftSize/hopSize) + 1 = 5`. The
true count is **5** (`floor((L − 1 + N − 1)/hop) + 1 = 5` at `L = 512`, `N = 2048`, `hop = 512`), so
the bound is right and the prose is wrong — a reader trusting the "4" asserts `≤ 4` and fails a
correct build. Correct the prose to 5, and state the **measurement**, which the criterion omits
entirely: silent *frames* are not observable through any public API, so the assertion is on the
maximal run of consecutive near-silent output **samples**, which is
`(silentFrames − numOverlaps + 1) · hopSize = 2 · hopSize`, not `5 · hopSize` (S13.2).

**C-12 (spec wording — SC-017's frame origin and comparison).** `1 − coeff^f` is correct only if `f`
counts frames since the `setSmearAmount(1.0f)` call, but the formula counts frames since `prepare()`;
"issue the setter at a known sample index" invites a warm-up that offsets the trajectory and fails a
correct component. Amend to "issue the setter **before the first `processBlock`**", with the
generalisation `f = max(0, floor((samplesProcessed − callIndex − fftSize)/hopSize) + 1)` recorded for
any future mid-render variant. Also state the comparison as `≤ 1e-4`, not `<`: the smoother snaps once
`|current_ − target_| < kCompletionThreshold = 1e-4f` (`smoother.h:199-201`), so the tolerance is
exactly tight at that frame.

**C-13 (spec wording — SC-013 (a)'s configuration).** The criterion says "(a) defaults", but revision
2 flipped `PrepareConfig::enabled` to default `false`, so a literal reading measures a true bypass and
collides with SC-013's own anti-no-op floor (`kBaseline ≥ kReferenceNs / 50` = 1 066 ns; a bypass
measures tens of ns). D-11's intent is an **enabled** instance at default control values. Amend (a) to
state the configuration explicitly, as S15 now does.

**C-14 (no spec change; an implementation consequence — FR-043 and the poison path).** FR-043 states
the invariant as "the stream position depends only on the number of frames elapsed, never on whether
the gate engaged". Revision 1's ordering (`if (!poisoned) decohere(ch, d);`) made that **false**: a
poisoned frame ran neither the per-bin path nor the compensating burn, leaving `rng_[ch]`
`numBins − 2` draws behind a clean render **permanently**, and FR-062's deliberate no-latch design
makes that reachable in normal operation rather than only at a terminal state. S5.3 now calls
`decohere(ch, poisoned ? 0.0f : d)` unconditionally, so one rule governs both skips.

**C-15 (no spec change; a plan self-contradiction fixed).** `prepare()`'s rate guard said both
"nothing else is written" and "also zero the geometry", and S9's read table forces the second. S2 step
1 is now an explicit ordered list, and the footprint self-report is qualified in both S2 and S9:
`getAllocatedBytes() == 0` means "this instance will not render", **not** "this instance holds no
heap" — the vectors from a previous successful `prepare()` are deliberately retained.

**C-16 (no spec change; a TU placement fix).** Non-finite **setter** arguments moved out of
`SpectralSmear_ControlClamps` (fast-math TU) into `SpectralSmear_NonFiniteSetters` in
`spectral_smear_nonfinite_test.cpp`, the one `-fno-fast-math` TU, matching the Phase-3 precedent at
`resonance_drift_network_test.cpp:38-42`. In the fast-math TU those values fold to finite garbage, so
the arm would pass vacuously or red at random depending on the fold.

**Still measurement-contingent, as the spec's own Open Questions say** — each carries a named
stop-and-surface rule, neither is decided here:
1. **The coherence-make-up knots** (FR-042, SC-006): AetherReverb's table is transcribed as the
   expectation; if any knot is off by > 2 %, our measured table ships and the header documents the
   cause.
2. **The CPU baselines** (FR-060, SC-013): projected 26 667–32 000 ns/block against the 35 555 ns
   effective ceiling. Over ⇒ the ordered levers, then stop and surface. Never a relaxed number.

---

## S18. Suggested task order (tasks.md owns the real breakdown)

1. Header skeleton: constants, `PrepareConfig`, the full public surface with the FR-009/FR-018
   contracts, `prepare()`/`reset()`/`setSeed()`, `poleForTau`, `coherenceMakeup` — no render path yet.
   Register all four TUs and the `lint_all_headers.cpp` include now, so nothing can silently drop out.
2. `spectral_smear_test.cpp` with SC-002 (a)(d)(e), SC-008, the clamp/geometry Edge Cases and the
   white-box pole-bounds case; `spectral_smear_nonfinite_test.cpp` with
   `SpectralSmear_NonFiniteSetters` (bit-pattern idiom, `std::numeric_limits` banned by the banner).
   These are failing-then-passing against step 1 only.
3. S4's pole tables, the deferred-rebuild flag and the per-frame resolve; extend step 2's cases.
4. The render path: S5 (chunking, drain loop, FIFO, warm-up counter) with the magnitude and phase
   passes stubbed to identity ⇒ SC-002 (b)(c), SC-003, SC-011, SC-017, SC-007 go green.
5. S6's magnitude pass (integrator, blend, priming, denormal flush, poison accumulator) ⇒ SC-018
   arm (a) and `SpectralSmear_OutputClamp`, then the spectral TU's SC-004, SC-010,
   `SpectralSmear_TimeConstantLaw` and `SpectralSmear_DcNyquistSmear` with S13.1's helper landed first
   and self-tested.
6. S7's phase pass, make-up table and FR-046 ramp ⇒ SC-001, SC-006, SC-009, SC-012.
7. S8's poison path (including the unconditional `decohere(ch, poisoned ? 0.0f : d)` burn) ⇒ SC-016
   and SC-018 arm (b).
8. SC-005's soak, then the perf TU and its **four** baselines including SC-013 (d)'s rebuild
   configuration (measured alone, `node tools/run-cpu-tests.js`).
9. Gates: the six lints + `check-portability.js`, the FR-072 consumer suites, the SC-014 diff check,
   clang-tidy, then compliance.

---

## Review notes (revision 2 of this plan)

Eighteen issues were raised against revision 1 of this plan. **Seventeen were accepted in full**; the
remaining one was accepted in substance and **partially rejected on one sub-point**, which is recorded
here rather than applied silently. **No threshold was relaxed anywhere** — where a threshold changed
(SC-018), it changed because the old one had no realisable input, and the replacement still fails the
named failure mode by more than 30 dB.

**Partial rejection — SC-016 (iv): the `≤ 5` frame bound is kept, not padded to `≤ 7`.** The issue
correctly showed that the plan's prose ("covered by 4 consecutive analysis frames") contradicted its
own bound of `ceil(fftSize/hopSize) + 1 = 5`, and that no measurement recipe was given. Both of those
are fixed in S13.2 and S8: the prose now says 5, with the derivation, and the assertion is restated on
the observable quantity (a silent output **run** of `2 · hopSize`, not a frame count). The issue's
further suggestion — restate the bound "with margin, e.g. `≤ ceil(fftSize/hopSize) + 2`" — is
**rejected**, because the bound is not merely a plausible figure that happens to be tight: it is the
exact maximum over injection alignments. A span of `L` injected samples overlaps
`floor((L − 1 + fftSize − 1)/hopSize) + 1` analysis windows, which at the criterion's own
`L = 512`, `fftSize = 2048`, `hopSize = 512` is exactly 5 for every alignment; window tapering cannot
reduce it (`NaN * 0.0f` is `NaN`, so a frame touching one injected sample still poisons). Padding to
7 would let a regression that poisons two extra frames — e.g. a poison test moved from the analysed
magnitudes to the integrator state, or a clear that misses a frame — pass unnoticed, which is exactly
what the arm exists to catch. The zero-margin tightness is a property of the arithmetic, not a
fragility of the assertion, and the risk the issue names (an implementer trusting the "4") is removed
by fixing the prose, which is the actual defect.

The other seventeen, in the order they were raised, with where each landed:

| Issue | Landed in |
|---|---|
| SC-018's per-bin `1e-6` re-analysis is not executable | S13.2 (SC-018 rewritten to a sample-domain relative-RMS comparison, both arms), S17 C-5, S16 R10 |
| FR-061 has no test that can detect it failing | S13.2 `SpectralSmear_OutputClamp`, S17 C-6 (2), S16 R17 |
| FR-025 (DC/Nyquist smear) has no criterion that can see it fail | S13.2 `SpectralSmear_DcNyquistSmear`, S17 C-6 (3), S16 R17 |
| `tauLow == tauHigh` / `tauLow < tauHigh` have no case | S13.2 `SpectralSmear_TimeConstantLaw` |
| SC-013 (a) says "defaults" after `enabled` defaulted to `false` | S15 configuration (a), S17 C-13 |
| SC-012 (c)'s reference render undefined | S13.2 (destination setting held constant, same absolute window), S17 C-10 |
| FR-024's denormal flush is not criterion-coverable | S6 (by inspection + named header comment), S17 C-9 |
| FR-013 invariant (c) carries no "by inspection" note | S5.3 bullet (c), S17 C-8 |
| FR-071 already has a second violation (`specs/Vorago-roadmap.md`) | S17 C-1 entry 2 |
| `poleForTau` has no FR behind it | S1.4 comment, S17 C-6 (1) |
| SC-017's frame origin and `<` vs `≤` | S13.2 (setter before the first `processBlock`; `≤ 1e-4`), S17 C-12 |
| SC-016 (iv) off by one, no measurement recipe | S8, S13.2, S17 C-11 (**and the partial rejection above**) |
| FR-053 disabled-instance reads: two mechanisms, no getter body | S9 (getter body is normative), S2 step 3 (rationale removed) |
| Pole-rebuild cost undercounted ~2×, never classified, never measured | S4.3 (cost table + deferred rebuild), S1.4 (`@note`), S5.1, S15 (d), S17 C-7, S16 R18 |
| Non-finite setter args in the fast-math TU | S13 TU table, S13.2 Edge Cases, S14 item 2, S17 C-16 |
| The poison path skips the FR-043 RNG burn | S5.3, S7.1, S8, S17 C-14 |
| `prepare()` rate guard self-contradictory; footprint self-report wrong | S2 step 1, S9, S17 C-15 |
| S0 ledger cites the wrong lines for `calculateOnePolCoefficient` | S0 (`smoother.h:77-93`, verified this session) |
