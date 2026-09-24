// ==============================================================================
// atmosphere_ghost_fixtures.h
// Shared, test-only fixtures for the Vorago Phase 10a AtmosphereEngine ghost
// extension (reverse grains + event-triggered grains)
// ==============================================================================
// Spec:  specs/vorago-phase10a-ghost-extension/spec.md
// Plan:  specs/vorago-phase10a-ghost-extension/plan.md   (S3.2 "Per-criterion
//        design", P-2 "full-ring pre-roll", S3.3 non-finiteness)
// Tasks: specs/vorago-phase10a-ghost-extension/tasks.md  T002 (this file),
//        T005 (appends the base-commit PROVENANCE constants below)
//
// WHY THIS FILE EXISTS. Five Phase 10a TUs share one operating point, one
// pre-roll rule, one excitation and one non-finiteness predicate. Defining them
// once is what makes SC-001's mechanism checkable: T005 copies THIS header,
// unchanged, into a worktree at the base commit
// `374580d7d0f0631561413310bd3085e15ba7279c` and runs the IDENTICAL fixture
// function there to harvest the stored references. Duplicating the fixtures per
// TU would make "the base-commit worktree runs the identical fixture" a claim
// no reader can check.
//
// HARD CONSTRAINT: THIS HEADER MUST COMPILE AGAINST THE BASE COMMIT.
// It may therefore name ONLY shipped `AtmosphereEngine` API. No
// `setGrainReverseProbability`, no `triggerGrain`, no `getReverseRngState`, no
// `kReverseSalt` and no other Phase 10a accessor appears anywhere below - those
// arrive at T008/T010/T013/T015/T017 and belong in the cases, not here.
//
// NO CMAKE ENTRY. `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests` source list
// is enumerated and names `.cpp` only; a test-local header living beside its
// TUs is house-legal, with precedents in this directory:
//   dsp/tests/unit/systems/ecosystem_metrics_test_helpers.h
//   dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h
//   dsp/tests/unit/systems/vorago_perf_budget.h                     (T001)
//
// ODR SWEEP (roadmap line 594), run this session over `dsp/ plugins/ tools/`:
//   VoragoGhostFix        0 hits
//   applyVoragoGhost      0 hits
//   GhostConfig           0 hits
//   preRollFullRing       0 hits
//   excitePinkPlusTone    0 hits
//   exciteChirp           0 hits
//   schedulerTicks        0 hits
//   isNonFiniteBits       13 hits, ALL of them inside the anonymous namespace of
//                         `plugins/seraphis/tests/integration/effects_chain_test.cpp`
//                         (declared `:965`, defined `:2623`) - a different test
//                         binary (`seraphis_tests`), internal linkage, and this
//                         one is `VoragoGhostFix::isNonFiniteBits`. No collision.
// Everything here is `inline` and namespace-qualified, so every including TU
// shares one definition.
// ==============================================================================
#pragma once

#include <krate/dsp/core/db_utils.h>          // L0  detail::opaqueFloatBits
#include <krate/dsp/core/math_constants.h>    // L0  kTwoPi
#include <krate/dsp/core/random.h>            // L0  Xorshift32, deriveStreamSeed
#include <krate/dsp/systems/atmosphere_engine.h>  // L3  the component under test

#include <render_fingerprint.h>  // test_helpers  RenderFingerprint (section 6)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace VoragoGhostFix {

// =============================================================================
// 1. The Vorago ghost operating point (FR-017)
// =============================================================================

/// The seven FR-017 ghost values, plus the prepare-time geometry they need.
///
/// The values are EXACTLY what `vorago_perf_test.cpp:611-632`'s
/// `buildAtmosphere()` writes, which is in turn what
/// `VoragoEngine::prepare()` installs (`vorago_engine.h:290-307`). They are
/// reproduced as literals rather than read from `VoragoEngineConfig` for two
/// reasons: this header may not grow a `vorago_engine.h` include (it has to
/// compile in a bare base-commit worktree with the smallest possible surface),
/// and the perf TU already reproduces them for the same reason it records at
/// `vorago_perf_test.cpp:626-628` - `VoragoEngine::kDefaultAtmosBlur` is an
/// engine-owned PRIVATE default, not part of the component contract.
///
/// `level = 0.60f` is `VoragoEngine::kGhostBurstPeak` (`vorago_engine.h:206`),
/// i.e. the ghost's ACTIVE level - the same deliberate divergence from the
/// prepare-time 0.0 that `buildAtmosphere()` documents at
/// `vorago_perf_test.cpp:600-610`: a level-0 arm measures the idle path.
struct GhostConfig {
    // --- prepare-time geometry ---
    double sampleRate = 48000.0;
    std::uint32_t seed = 1u;
    float captureSeconds = 20.0f;   ///< vorago_engine.h:120; C = nextPowerOf2(960 000) = 1 048 576
    bool blurEnabled = true;        ///< vorago_engine.h:121
    bool freezeEnabled = false;     ///< vorago_engine.h:123
    std::size_t blurFftSize = 1024;    ///< vorago_engine.h:124
    std::size_t freezeFftSize = 2048;  ///< vorago_engine.h:125
    std::size_t maxBlockSamples = 2048;  ///< vorago_engine.h:107

    // --- the seven FR-017 control values ---
    float density = 0.30f;
    float grainSeconds = 12.0f;
    float pitchSemitones = -12.0f;
    float positionSpread = 0.90f;
    float blur = 0.85f;           ///< roadmap line 114's darker blur default
    float decorrelation = 0.85f;
    float level = 0.60f;          ///< == VoragoEngine::kGhostBurstPeak (vorago_engine.h:206)
};

/// Prepare `engine` and write the Vorago ghost operating point onto it.
///
/// `prepare()` ends with `reset()` (`atmosphere_engine.h:400-402`), so the
/// engine is silent, the ring is empty and every counter is zero when this
/// returns - which is the precondition `preRollFullRing()` below assumes.
///
/// `setSeed()` is called AFTER `prepare()` deliberately: `prepare()`'s trailing
/// `reset()` re-seeds every stream from the stored seed, so seeding first and
/// preparing second would work too, but this order matches
/// `buildAtmosphere()`'s (`vorago_perf_test.cpp:614-621`) and leaves one
/// obvious place where the seed is set.
inline void applyVoragoGhost(Krate::DSP::AtmosphereEngine& engine,
                             const GhostConfig& config = GhostConfig{}) {
    engine.prepare(config.sampleRate,
                   Krate::DSP::AtmosphereEngine::PrepareConfig{
                       .captureSeconds = config.captureSeconds,
                       .blurEnabled = config.blurEnabled,
                       .freezeEnabled = config.freezeEnabled,
                       .blurFftSize = config.blurFftSize,
                       .freezeFftSize = config.freezeFftSize,
                       .maxBlockSamples = config.maxBlockSamples});
    engine.setSeed(config.seed);
    engine.setDensity(config.density);
    engine.setGrainSeconds(config.grainSeconds);
    engine.setPitchSemitones(config.pitchSemitones);
    engine.setPositionSpread(config.positionSpread);
    engine.setBlur(config.blur);
    engine.setDecorrelation(config.decorrelation);
    engine.setLevel(config.level);
}

// =============================================================================
// 2. Excitation (deterministic, portable)
// =============================================================================

/// Salts for the two excitation lanes. Arbitrary, and deliberately NOT any of
/// the component's own salts - `deriveStreamSeed` is a pure function of
/// (base, salt) (`core/random.h:102-111`) and these streams belong to the test,
/// not to the engine.
inline constexpr std::size_t kExciteSaltLeft = 0xA001;
inline constexpr std::size_t kExciteSaltRight = 0xA002;

/// Tone frequency, expressed in CYCLES PER SAMPLE rather than Hz so the
/// excitation is a pure function of (span length, seed) at any rate - which is
/// what lets one stored base-commit reference cover one fixture. 110 Hz at
/// 48 kHz.
inline constexpr double kToneCyclesPerSample = 110.0 / 48000.0;

/// Combined peak sits around -12 dBFS, matching the perf TU's own excitation
/// level (`vorago_perf_test.cpp:374`).
inline constexpr float kNoiseGain = 0.20f;
inline constexpr float kToneGain = 0.10f;

/// @brief Fill `left`/`right` with deterministic decorrelated pink noise plus a
///        steady low tone.
///
/// XORSHIFT32, NEVER `<random>`: `std::uniform_real_distribution` and friends
/// are not specified to produce the same sequence across standard libraries, so
/// a `<random>`-built excitation would give a different render on each
/// toolchain and no stored reference could survive CI. This is the same reason
/// `vorago_perf_test.cpp:380-381` states for its own excitation.
///
/// Pink shaping is Paul Kellet's three-pole economy filter - one multiply-add
/// per pole, no allocation, no transcendental. The two channels are
/// decorrelated by running two independent `Xorshift32` streams derived from
/// the same seed through `deriveStreamSeed` (`core/random.h:102-111`, which
/// never yields 0).
///
/// If the two spans differ in length, the shorter one bounds the generated
/// region and the remainder of the longer one is zeroed - so the function never
/// leaves a caller's buffer partially uninitialised.
inline void excitePinkPlusTone(std::span<float> left, std::span<float> right,
                               std::uint32_t seed) noexcept {
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);

    Krate::DSP::Xorshift32 rngLeft{Krate::DSP::deriveStreamSeed(seed, kExciteSaltLeft)};
    Krate::DSP::Xorshift32 rngRight{Krate::DSP::deriveStreamSeed(seed, kExciteSaltRight)};

    // Kellet economy pink state, per channel.
    float bL0 = 0.0f, bL1 = 0.0f, bL2 = 0.0f;
    float bR0 = 0.0f, bR1 = 0.0f, bR2 = 0.0f;

    const double twoPi = static_cast<double>(Krate::DSP::kTwoPi);
    const double phaseStep = twoPi * kToneCyclesPerSample;
    double phase = 0.0;

    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
        const float wL = rngLeft.nextFloat();   // [-1, 1]
        const float wR = rngRight.nextFloat();

        bL0 = 0.99765f * bL0 + wL * 0.0990460f;
        bL1 = 0.96300f * bL1 + wL * 0.2965164f;
        bL2 = 0.57000f * bL2 + wL * 0.1050186f;
        const float pinkL = bL0 + bL1 + bL2 + wL * 0.1848f;

        bR0 = 0.99765f * bR0 + wR * 0.0990460f;
        bR1 = 0.96300f * bR1 + wR * 0.2965164f;
        bR2 = 0.57000f * bR2 + wR * 0.1050186f;
        const float pinkR = bR0 + bR1 + bR2 + wR * 0.1848f;

        // Quadrature tone: the two channels carry the same partial 90 degrees
        // apart, so the pair is not a mono sum even before the noise.
        const float toneL = static_cast<float>(std::sin(phase));
        const float toneR = static_cast<float>(std::cos(phase));

        left[i] = kNoiseGain * pinkL + kToneGain * toneL;
        right[i] = kNoiseGain * pinkR + kToneGain * toneR;

        phase += phaseStep;
        if (phase >= twoPi) {
            phase -= twoPi;
        }
    }
}

/// @brief Fill `out` with a LINEAR chirp from `f0` to `f1` over `seconds`,
///        silence afterwards (SC-002's excitation).
///
/// The phase is evaluated in CLOSED FORM -
/// `phase(t) = 2*pi*(f0*t + 0.5*((f1-f0)/seconds)*t^2)` - rather than
/// accumulated per sample, so the instantaneous frequency at sample `i` is
/// exactly `f0 + sweepRate * i / sampleRate` with no accumulated drift. SC-002
/// (b) reads the sweep rate back off the rendered grain as a spectral-centroid
/// slope, so drift here would show up there as a measurement error.
///
/// The chirp occupies `[0, seconds)`; the caller places it in time by choosing
/// which sub-span it writes into. Samples beyond `seconds * sampleRate`, and
/// the whole span when the arguments are degenerate, are left at 0.
inline void exciteChirp(std::span<float> out, double sampleRate, double f0, double f1,
                        double seconds) noexcept {
    std::fill(out.begin(), out.end(), 0.0f);
    if (sampleRate <= 0.0 || seconds <= 0.0) {
        return;
    }

    const double sweepRate = (f1 - f0) / seconds;  // Hz per second
    const double twoPi = static_cast<double>(Krate::DSP::kTwoPi);
    const double spanSamples = seconds * sampleRate;
    const std::size_t limit =
        (spanSamples >= static_cast<double>(out.size()))
            ? out.size()
            : static_cast<std::size_t>(spanSamples);

    for (std::size_t i = 0; i < limit; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double phase = twoPi * (f0 * t + 0.5 * sweepRate * t * t);
        out[i] = static_cast<float>(std::sin(phase));
    }
}

// =============================================================================
// 3. The full-ring pre-roll, defined ONCE (plan P-2 / S3.2)
// =============================================================================

/// @brief Render exactly `engine.getCaptureCapacitySamples()` samples of
///        excitation through `engine`, saturating its capture ring. Returns the
///        number of samples rendered.
///
/// WHY A SAMPLE COUNT AND NEVER A DURATION. `RollingCaptureBuffer::prepare()`
/// rounds the capacity UP to a power of two (`rolling_capture_buffer.h:75-93`,
/// echoed in the engine banner at `:37-40`), so the ring is NOT `captureSeconds`
/// of audio: at 48 kHz with `captureSeconds = 20` it is
/// `nextPowerOf2(960 000) = 1 048 576` samples = 21.845 s. Every seconds-stated
/// pre-roll in the reviewed drafts was short, and a short pre-roll leaves the
/// ring FILLING, where FR-050's reverse admission clause rejects births and a
/// ring-cold delta assertion fails on correct code. Availability saturates
/// because `RollingCaptureBuffer::getAvailableSamples()` is
/// `std::min(samplesWritten_, capacity_)` (`rolling_capture_buffer.h:443-445`),
/// so rendering `capacity_` samples saturates it and it stays saturated.
///
/// NO FIXTURE IN THIS PHASE STATES A PRE-ROLL IN SECONDS. A case that wants a
/// deliberately FILLING ring (T011 (A)) states its own explicit sample count
/// instead and does not call this helper.
///
/// @pre `engine` was just prepared (e.g. by `applyVoragoGhost`) or `reset()`.
/// @note Returns 0 for an unprepared engine (capacity is then 0), which is the
///       honest answer: nothing was rendered.
inline std::size_t preRollFullRing(Krate::DSP::AtmosphereEngine& engine, std::uint32_t seed = 1u,
                                   std::size_t blockSize = 512) {
    const std::size_t capacity = engine.getCaptureCapacitySamples();
    if (capacity == 0) {
        return std::size_t{0};
    }

    std::vector<float> inLeft(capacity, 0.0f);
    std::vector<float> inRight(capacity, 0.0f);
    excitePinkPlusTone(inLeft, inRight, seed);

    const std::size_t block = (blockSize == 0) ? std::size_t{1} : blockSize;
    std::vector<float> outLeft(block, 0.0f);
    std::vector<float> outRight(block, 0.0f);

    std::size_t rendered = 0;
    while (rendered < capacity) {
        const std::size_t n = std::min(block, capacity - rendered);
        engine.processStereoBlock(inLeft.data() + rendered, inRight.data() + rendered,
                                  outLeft.data(), outRight.data(), n);
        rendered += n;
    }
    return rendered;
}

/// @brief The capture ring's AVAILABLE sample count, computed by the test.
///
/// `AtmosphereEngine` exposes no `getAvailableSamples()` of its own (the name
/// appears at `atmosphere_engine.h:247`, `:255`, `:950`, `:1663`, `:1738`,
/// `:1995`, every one of them an internal `capture_.` use) and this phase does
/// NOT add one. The identity is exact:
/// `RollingCaptureBuffer::getAvailableSamples() == std::min(samplesWritten_,
/// capacity_)` (`rolling_capture_buffer.h:443-445`), and `samplesWritten_` is
/// the number of samples rendered since `prepare()`/`reset()`.
///
/// Returns `double` because every consumer compares it against the admission
/// arithmetic in `tryBirthGrain()`, which is computed in `double`.
[[nodiscard]] inline double availableSamples(std::size_t rendered, std::size_t capacity) noexcept {
    return static_cast<double>(std::min(rendered, capacity));
}

// =============================================================================
// 4. Non-finiteness, by bit pattern only
// =============================================================================

/// @brief True iff `value` is NaN or +/-Inf, decided on the BIT PATTERN.
///
/// NEVER `std::isnan` / `std::isinf` / `std::numeric_limits<float>::infinity()`
/// (FR-043, plan S3.3). Under `-ffast-math` / `-ffinite-math-only` the compiler
/// may assume no float is ever non-finite and fold such a test to `false`,
/// silently deleting the guard. A non-finite float is exactly "all eight
/// exponent bits set" - an INTEGER test on the bits, which no FP flag can
/// reshape.
///
/// The bits are read through the repo's own hardened launder,
/// `Krate::DSP::detail::opaqueFloatBits` (`core/db_utils.h:68-74`), rather than
/// a bare `memcpy`: on GCC/Clang it routes the integer through an empty `asm`
/// so value-provenance facts (LLVM `nofpclass`) cannot reach the test either.
/// The mask is the same one
/// `plugins/seraphis/tests/integration/effects_chain_test.cpp:2623-2627` uses.
///
/// Usable in EVERY Phase 10a TU, including the four that are deliberately NOT
/// on the `-fno-fast-math` list - which is the point: SC-004's detection-only
/// NaN/Inf check stays on the fast-math path and still has teeth.
[[nodiscard]] inline bool isNonFiniteBits(float value) noexcept {
    return (Krate::DSP::detail::opaqueFloatBits(value) & 0x7F800000u) == 0x7F800000u;
}

// =============================================================================
// 5. Scheduler tick accounting
// =============================================================================

/// @brief The number of density-scheduler birth ATTEMPTS a render of
///        `renderedSamples` samples produces at `density`.
///
/// `= 1 + floor(renderedSeconds / interonsetSeconds)`.
///
/// THE LEADING 1 IS THE FREE TICK AT SAMPLE 0, and it is the reason no test in
/// this phase hard-codes a tick count. `GrainScheduler::reset()` sets
/// `samplesUntilNextGrain_ = 0.0f` (`grain_scheduler.h:40`) and `process()`
/// decrements BEFORE testing `<= 0.0f` (`:73-76`), so the scheduler fires on the
/// very first rendered sample of a freshly reset engine. Assertions that
/// forgot it (the reviewed draft's bare `== 64`) are wrong on correct code.
///
/// THIS IS THE `jitter = 0` COUNT, i.e. the count at the nominal interonset
/// `sampleRate / density` (`grain_scheduler.h:100-103`). With jitter the
/// interval is scaled by `1 + u * jitter * 0.5`, `u` in [-1, 1] (`:78-88`), so
/// the realised count is bounded rather than predicted: a case that runs with
/// jitter uses the SHORTEST admissible interval to get a ceiling (e.g. T011 (C)
/// / T012 (a), where `density = 0.30`, `jitter = 0.5` gives
/// `(1/0.30) * (1 - 0.5*0.5) = 2.5 s`) and never claims equality.
///
/// The density floor mirrors the component's own: `GrainScheduler::setDensity`
/// applies `std::max(0.1f, grainsPerSecond)` (`grain_scheduler.h:47`) and
/// `AtmosphereEngine::kMinDensity` is documented as the same floor (`:303`), so
/// the effective interonset never divides by something smaller.
[[nodiscard]] inline std::size_t schedulerTicks(std::size_t renderedSamples, double sampleRate,
                                                float density) noexcept {
    if (sampleRate <= 0.0) {
        return std::size_t{1};  // the free tick at sample 0, and nothing else
    }
    const float effectiveDensity =
        std::max(Krate::DSP::AtmosphereEngine::kMinDensity, density);
    const double renderedSeconds = static_cast<double>(renderedSamples) / sampleRate;
    const double interonsetSeconds = 1.0 / static_cast<double>(effectiveDensity);
    return std::size_t{1} + static_cast<std::size_t>(
                                std::floor(renderedSeconds / interonsetSeconds));
}

// =============================================================================
// 6. Base-commit stored references
// =============================================================================
//
// ============================ PROVENANCE (T005) ==============================
//
// THESE ARE STORED REFERENCES, RE-MEASURED UNDER A DOCUMENTED RULE AND NEVER
// HAND-EDITED. A failure against one of them is a FINDING, not an invitation to
// update the literal. (The wording is the perf TU's own,
// `vorago_perf_test.cpp:236-253`, adopted deliberately.)
//
//   Base commit    374580d7d0f0631561413310bd3085e15ba7279c
//                  "feat(vorago): Phase 10 Voice and Engine"
//   Harvested by   a throwaway `git worktree add ../iterum-basecommit <sha>`,
//                  into which THIS header was copied unchanged alongside a
//                  throwaway `dsp/tests/ghost_ref_dump.cpp` registered in the
//                  worktree's `dsp/tests/CMakeLists.txt` only. The worktree was
//                  removed afterwards; nothing of it is committed. The dump
//                  therefore ran the IDENTICAL fixture functions defined above -
//                  which is the whole mechanism SC-001 clause 1 rests on.
//   Machine        CODEBOX, 13th Gen Intel(R) Core(TM) i9-13900HX,
//                  Windows 11 Pro 10.0.26200
//   Toolchain      MSVC 19.44 (cl.exe 14.44.35207), Visual Studio 17 2022
//                  generator, x64, Release. `enableFTZDAZ()` called before the
//                  first render, exactly as `dsp/tests/dsp_test_main.cpp` does
//                  for every KrateDSP test binary - denormal handling is part of
//                  the measured result.
//   Date           2026-09-23
//   Reproducible   The dump binary was run TWICE on this machine and the two
//                  outputs are byte-identical: every integer bit-identical and
//                  every fingerprint metric/checkpoint identical to the 17
//                  significant digits printed.
//
// NO BIT-EXACT FLOAT GOLDEN IS INTRODUCED HERE (roadmap line 605, FR-046). The
// two `RenderFingerprint` constants are consumed ONLY through
// `compareFingerprints(actual, reference).withinTolerance()`
// (`render_fingerprint.h:108-110`) at MEASURED per-comparison bounds; they are
// never compared for equality. The integer constants ARE exact equalities, and
// deliberately so - an integer counter and an `Xorshift32` state have no
// toolchain spread to absorb.
//
// =============================================================================

// -----------------------------------------------------------------------------
// 6.1 Fixture A - the 60 s AtmosphereEngine ghost render
//     (SC-001 clauses 1-3)
// -----------------------------------------------------------------------------
//
// EXACT PROTOCOL, so a consuming case can reproduce it without guessing:
//   AtmosphereEngine on the heap, `applyVoragoGhost(engine)` with the DEFAULT
//   `GhostConfig` above (48 kHz, seed 1, captureSeconds = 20, blur on, freeze
//   off, blurFftSize = 1024, freezeFftSize = 2048, maxBlockSamples = 2048, and
//   the seven FR-017 control values); excitation
//   `excitePinkPlusTone(inLeft, inRight, 1u)` over the WHOLE 2 880 000-sample
//   span; rendered front to back in blocks of `kReferenceRenderBlock` = 512;
//   the fingerprint is taken over the LEFT output channel, all 2 880 000
//   samples.
//
// Measured alongside, and recorded because a consumer should not have to
// rediscover it: `getCaptureCapacitySamples() == 1 048 576` at this
// configuration, i.e. the ring is 21.845 s and NOT the configured 20 s - the
// reason `preRollFullRing()` above states a sample count and never a duration.

/// The 60 s reference renders' block partition. Stated as a constant because
/// `AtmosphereEngine`'s output is partition-invariant only to within the
/// `render_fingerprint.h` tolerances (that invariance is itself a criterion,
/// SC-008 (e)), so a consumer that renders the reference fixture at a different
/// block size is not reproducing this measurement.
inline constexpr std::size_t kReferenceRenderBlock = 512u;

/// Sample count of both 60 s reference renders (60 s at 48 kHz).
inline constexpr std::size_t kReferenceRenderSamples = 2880000u;

/// Capture capacity observed at the `GhostConfig` default geometry.
/// `nextPowerOf2(20 * 48 000) = 1 048 576` (`rolling_capture_buffer.h:75-93`).
inline constexpr std::size_t kBaseCommitCaptureCapacity = 1048576u;

/// @brief Base-commit fingerprint of Fixture A's left channel.
/// @see PROVENANCE above. Consumed ONLY via `compareFingerprints(...)`.
inline constexpr Krate::DSP::TestUtils::RenderFingerprint kBaseCommitFingerprint{
    .rms = 0.045187897370990084,
    .peak = 0.33692830801010132,
    .meanAbs = 0.032592553826707081,
    .totalVariation = 10665.913020271684,
    .checkpoints = {0.0f,           0.0f,           0.0106146391f,  -0.0268118661f,
                    0.0180485547f,  -0.0440961868f, 0.0131073035f,  -0.0413083509f,
                    0.00852230657f, 0.0368464105f,  0.0120678088f,  0.0335803144f,
                    -0.120695569f,  0.0142731145f,  -0.0414592922f, -0.0222398806f,
                    0.0609053336f,  -0.0864504129f, 0.00113214867f, 0.0223359391f,
                    0.0264347196f,  -0.0045719496f, -0.0384042189f, -0.0134430099f,
                    -0.0516529046f, -0.0402912796f, -0.108581103f,  0.0112841651f,
                    0.0489074551f,  0.0338527374f,  -0.0236443058f, -0.0199564919f}};

/// `getGrainRngState()` (`atmosphere_engine.h:1118`) after Fixture A.
/// SC-001 clause 2's no-trigger arm compares this as an INTEGER, at
/// probability 0 and at probability 1 - the check ADR-2's structural claim
/// needs and that no tolerance could absorb.
inline constexpr std::uint32_t kBaseCommitGrainRngState = 8918586u;

// SC-001 clause 3's counter identity, all after Fixture A
// (`atmosphere_engine.h:1053-1070`, `:1128`).
inline constexpr std::uint64_t kBaseCommitTotalBorn = 17u;
inline constexpr std::uint64_t kBaseCommitTotalRetired = 14u;
inline constexpr std::uint64_t kBaseCommitSkipPoolFull = 0u;
inline constexpr std::uint64_t kBaseCommitSkipRingCold = 1u;
inline constexpr std::size_t kBaseCommitLatencySamples = 1024u;

// -----------------------------------------------------------------------------
// 6.2 Fixture B - the 60 s VoragoEngine default render plus one held note
//     (SC-010 (a), as amended by ruling B-2, spec.md "Session 2026-09-23
//     (build stage, T025 stop-and-surface)")
// -----------------------------------------------------------------------------
//
// EXACT PROTOCOL (the SOUNDING recipe of ruling B-2):
//   `TestUtils::Vorago::makeEngine(48000.0, VoragoEngineConfig{})`
//   (`tests/test_helpers/vorago_fixtures.h:734`), then, before any rendering,
//   `engine->setSeed(0x6057u)`, `engine->setPolyphony(1u)`,
//   `engine->noteOn(33u, 100u)` at sample 0 - and NOTHING else written: no
//   macro write, no `applyFastAttack`, no ecosystem-depth write, no other
//   setter. Then `TestUtils::Vorago::renderEngine(engine, l, r, 2 880 000, 512)`
//   (`:750`); the fingerprint is taken over the LEFT channel. The note is held
//   for the whole 60 s (no note-off). Any deviation makes the stored constant
//   inapplicable.
//
// HISTORY: the first harvest of this constant (2026-09-23, same base commit)
// used `VoragoEngineConfig` defaults with NO note-on and rendered 60 s of
// exact digital silence - every metric and all 32 checkpoints were 0. That
// made FR-046's measured-bounds derivation unsatisfiable (`compareFingerprints`
// divides by max(|reference|, 1e-12)), so ruling B-2 superseded it with the
// sounding recipe above. The all-zero constant is gone; it is not a fallback.
//
// PROVENANCE (this constant, third harvest - ruling B-3):
//   Base commit    374580d7d0f0631561413310bd3085e15ba7279c
//   Recipe         sounding recipe per B-2 (above)
//   Harvested      INSIDE `dsp_systems_tests` (B-3): the test binary's own link
//                  context, not a standalone dump - a standalone dump of the
//                  same commit reads 1.08e-4 higher on peak through /fp:fast
//                  COMDAT selection (`ContinuousBody::prepare` and
//                  `SubharmonicEngine::updateControl` are taken from
//                  continuous_body_test.obj / subharmonic_engine_test.obj in
//                  the test link, and compile differently in a small TU).
//   Harvested by   throwaway `git worktree add ../iterum-basecommit 374580d7`;
//                  one throwaway TEST_CASE with the paste-ready printer
//                  (`zz_harvest_fixture_b_test.cpp`) appended as the LAST
//                  entry of the worktree's `dsp_systems_tests` source list in
//                  `dsp/tests/CMakeLists.txt` - everything the shipped list
//                  has, in its order, then the harvest TU; worktree removed
//                  afterwards, nothing of it is committed.
//   Machine        CODEBOX, 13th Gen Intel(R) Core(TM) i9-13900HX,
//                  Windows 11 Pro 10.0.26200
//   Toolchain      MSVC 19.44.35228 (cl.exe 14.44.35207), Visual Studio 17 2022
//                  generator, x64, preset `windows-x64-release` (Release);
//                  `enableFTZDAZ()` via `dsp_test_main.cpp` before the render
//   Date           2026-09-23
//   Reproducible   `dsp_systems_tests.exe "ZZ_HarvestFixtureB"` was run TWICE;
//                  the two printed literals are byte-identical
//                  (md5 2b59e1ad46ffc3da16abc67f833a2f62)
//   Non-vacuous    rms = 0.10584018809847234 and peak = 0.28841844201087952 are
//                  both NON-ZERO, so a tolerance comparison against this
//                  reference exercises every metric and checkpoint; the
//                  consuming clause additionally guards `actual.rms > 0`.
//   Checkpoint 0   is exactly 0 because the render starts from a silent engine
//                  at sample 0 (printed with `std::showpoint` as `0.00000000f`).
inline constexpr Krate::DSP::TestUtils::RenderFingerprint kBaseCommitVoragoFingerprint{
    .rms = 0.10584018809847234,
    .peak = 0.28841844201087952,
    .meanAbs = 0.084950359182566965,
    .totalVariation = 1595.9558681194799,
    .checkpoints = {
        0.00000000f, 0.0102007203f, 0.0254982039f, 0.0518970042f,
        -0.0978992134f, -0.0179690085f, -0.163007259f, 0.0488205999f,
        -0.0893889517f, 0.0580466166f, 0.158383504f, 0.0916293189f,
        0.177875027f, -0.120974518f, 0.0338675305f, -0.174887195f,
        -0.108078532f, -0.102700792f, -0.0340998136f, 0.207726911f,
        0.0203725398f, 0.126996040f, -0.0808575451f, 0.0179890692f,
        -0.102840424f, -0.160841048f, -0.0418781452f, -0.0654609054f,
        0.143871486f, 0.0234865677f, 0.242045194f, 0.00852372590f,}};

// -----------------------------------------------------------------------------
// 6.3 Fixture C - SC-011 (c)'s forward arm at the SHORT (filling-ring) pre-roll
// -----------------------------------------------------------------------------
//
// EXACT PROTOCOL: `applyVoragoGhost(engine)` with the default `GhostConfig`,
// then SC-011's ONE declared deviation - `setPitchSpread(0.0f)` and
// `setDriftRangeSemitones(0.0f)`, so `ratioMax == ratioMin == 2^(-12/12) = 0.5`
// exactly. Excitation `excitePinkPlusTone(inLeft, inRight, 1u)` over the whole
// 280 000-sample span. Rendered front to back in blocks of
// `kShortPreRollBlock` = 64, i.e. the 240 000-sample pre-roll AND the
// 40 000-sample measured span use the same 64-sample partition - a consumer
// that pre-rolls in a different block size is not reproducing this measurement.
// The reverse probability is absent at the base commit, so these are simply the
// forward values; SC-011 (c) re-renders the same fixture at probability 0.
//
// `triggerGrain()` does not exist at the base commit, so no trigger was fired
// here. A consuming case that fires one is NOT measuring against these figures.

/// Fixture C's pre-roll, in SAMPLES. Deliberately shorter than
/// `getCaptureCapacitySamples()` (1 048 576): this is the one fixture family
/// that renders the FILLING-ring regime, which is why it does not call
/// `preRollFullRing()`.
inline constexpr std::size_t kShortPreRollSamples = 240000u;

/// Fixture C's measured span, in samples (SC-011's `kRejectSpan`).
inline constexpr std::size_t kShortPreRollSpanSamples = 40000u;

/// Fixture C's block partition.
inline constexpr std::size_t kShortPreRollBlock = 64u;

// Counters after the full 280 000 samples of Fixture C.
inline constexpr std::uint64_t kBaseCommitShortPreRollBorn = 1u;
inline constexpr std::uint64_t kBaseCommitShortPreRollRetired = 0u;
inline constexpr std::uint64_t kBaseCommitShortPreRollRingCold = 1u;
inline constexpr std::uint64_t kBaseCommitShortPreRollPoolFull = 0u;
inline constexpr std::uint32_t kBaseCommitShortPreRollGrainRngState = 1385444124u;

}  // namespace VoragoGhostFix
