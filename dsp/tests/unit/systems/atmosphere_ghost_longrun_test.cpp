// ==============================================================================
// Layer 3: System Tests - AtmosphereEngine ghost extension, the [long] lane
//          (specs/vorago-phase10a-ghost-extension)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10a-ghost-extension/spec.md
//            specs/vorago-phase10a-ghost-extension/plan.md
//            specs/vorago-phase10a-ghost-extension/tasks.md  (T003 creates this
//            stub; T012 fills it, T011 (B) may hand it a sweep sibling)
//
// SCOPE OF THIS TU (tasks.md T003's table): SC-003 [long], SC-007 (d) [long],
// and SC-004's sweep sibling [long] - the last only if it measures > 15 s.
//
// EVERY case in this file carries the [long] tag, and qualifies for it on BOTH
// of CLAUDE.md's clauses: measured runtime > ~15 s AND assertions that are
// toolchain-INDEPENDENT (FR-048). Per-push CI excludes [long]; the nightly
// long-tests workflow runs it on all three OSes. NaN/Inf-guard, bounded-grid and
// state-format cases must therefore NEVER be moved into this TU - they live in
// atmosphere_ghost_test.cpp so they stay in the per-push lane.
//
// This TU is DELIBERATELY NOT in the -fno-fast-math block of
// dsp/tests/CMakeLists.txt: it injects no bit patterns, and the guards it
// exercises must be proved in the /fp:fast + -ffast-math mode the header
// actually ships in.
//
// ------------------------------------------------------------------------------
// LANDED SO FAR IN THIS TU
//   T012 - SC-003, the full [long] case AtmosphereGhost_ReverseLiveness:
//          (a) full-ring pre-roll + 10 minutes with the ring-cold counter frozen,
//          (b) click freedom as a RELATIVE comparison (probability 1 vs
//              probability 0 over the same render),
//          (c) FR-015's observed-age bounds.
//          Authored per tasks.md T012, whose "Verify (red)" expects this case
//          to be a NO-OP GUARD at its point in the order: with a FORWARD-only
//          window the `reversed` flag changes no admission arithmetic, so the
//          direction has no effect and all three clauses pass vacuously. It
//          must still be GREEN once T013 lands - that is the whole assertion.
//          STATE OF THE HEADER AS THIS FILE IS WRITTEN, read this session:
//          T013 has ALREADY landed in the working tree - the window is
//          direction-dependent at atmosphere_engine.h:1732-1733
//          (`wUp = reversed ? 0.0 : ...`, `wDown = reversed ? 1 + ratioMax
//          : ...`) and FR-050's reverse fill clause is present at `:1835-1843`.
//          So the clauses below are NOT vacuous here: clause (a) is already the
//          real statement that the reverse birth window is non-empty at the
//          Vorago operating point, and FR-050's `capacity - avail` deficit is
//          vacuous only because this fixture pre-rolls the ring FULL.
//   T019 - SC-007 (d), AtmosphereGhost_Determinism_ReverseFraction: the
//          accelerated 100 000-grain reverse-fraction sweep at p in
//          {0.25, 0.5, 0.75}, a CALIBRATION band of +/-0.02 and deliberately
//          not a 3-sigma RNG test. Its fixture note below states the three
//          ways the sample could be biased and the assertion that closes each.
//
//          SC-004's [long] sweep sibling is NOT here, and T019's condition for
//          adding it ("if SC-004 measures > 15 s at T011") is NOT met: as T016
//          left it, AtmosphereGhost_ReverseTruncation renders, per arm, a
//          65 536-sample full-ring pre-roll, a settle bounded by the longest
//          interonset (48 000 / 0.1 * 1.25 = 600 000 samples) and a 96-block
//          = 6 144-sample measured span - under 1.4 M samples for both arms
//          together, at 48 kHz, with at most kMaxGrains live. Its sentinel
//          assertions (NaN/Inf, kMaxLevel, lifetime >= 2) must stay in the
//          per-push lane at ANY cost (FR-048's closing sentence), so the split
//          exists only to move the EXHAUSTIVE sweep out - and there is nothing
//          to move while the case is this small. If the build agent's measured
//          runtime contradicts that sizing, the sibling lands here, not a tag
//          on the sentinels.
// ------------------------------------------------------------------------------
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "atmosphere_ghost_fixtures.h"

#include "artifact_detection.h"  // test_helpers  ClickDetector, ClickDetectorConfig

#include <krate/dsp/core/math_constants.h>        // L0  kTwoPi
#include <krate/dsp/core/random.h>                // L0  Xorshift32, deriveStreamSeed
#include <krate/dsp/systems/atmosphere_engine.h>  // L3  the component under test

#include <algorithm>
#include <array>  // T019  the three-probability sweep's arms
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

using Krate::DSP::AtmosphereEngine;
using Krate::DSP::Xorshift32;

namespace {

// =============================================================================
// Geometry
// =============================================================================

constexpr double kSampleRate = 48000.0;  ///< VoragoGhostFix::GhostConfig::sampleRate
constexpr std::uint32_t kSeed = 1u;      ///< VoragoGhostFix::GhostConfig::seed

/// Render block. 512 divides kSpanSamples exactly (56 250 blocks), so the block
/// grid is a property of the test and not of where the span happens to end.
constexpr std::size_t kBlock = 512u;

/// SC-003's measured span: TEN MINUTES of audio at 48 kHz.
/// 600 s x 48 000 = 28 800 000 samples. NOT shortened for runtime - that is
/// exactly what the [long] tag buys (FR-048).
constexpr std::size_t kSpanSamples = 28800000u;

/// Click-analysis chunk, in samples: 96 render blocks = 49 152 samples
/// (1.024 s). A whole multiple of kBlock so the chunk boundaries fall on block
/// boundaries, and >> the detector's 512-sample frame.
///
/// WHY CHUNKED AT ALL. The span is 28 800 000 samples per channel; holding two
/// channels of it as float costs 230 MB per arm, and clause (b) needs TWO arms.
/// The detector's statistic is per-frame (artifact_detection.h:186-193 - local
/// mean + sigma*stddev of |dx| inside one frame), so chunking changes only which
/// frames exist at the chunk seams - IDENTICALLY in both arms, which is all a
/// RELATIVE comparison needs.
constexpr std::size_t kClickChunkSamples = 96u * kBlock;

// =============================================================================
// Clause (b)'s detector
// =============================================================================

/// The click-detector configuration, TRANSCRIBED - not re-invented.
///
/// DEVIATION FROM tasks.md T012, recorded here because it must be checkable:
/// T012 says to transcribe `frameSize`/`hopSize`/`detectionThreshold` from
/// "atmosphere_engine_test.cpp's own forward-liveness cell". That cell
/// (`AtmosphereEngine_GrainLiveness`, dsp/tests/unit/systems/
/// atmosphere_engine_test.cpp:841) contains NO ClickDetector - verified this
/// session: the only occurrence of the string "Click" anywhere in that file is
/// the comment at `:3799` which states that "Clause (c)'s ClickDetector half is
/// NOT repeated". So the values below are transcribed from the AtmosphereEngine
/// cell that does own a detector, at the same 48 kHz and on the same component:
///
///   dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp:301-306
///       .sampleRate = kSampleRate, .frameSize = 512, .hopSize = 256,
///       .detectionThreshold = kClickThresholdSigma, .energyThresholdDb = -60.0f,
///       .mergeGap = 5
///   dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp:291
///       constexpr float kClickThresholdSigma = 14.0f;
///
/// and its banner at `:100-101` records that frameSize 512 / hopSize 256 /
/// energyThresholdDb -60 / mergeGap 5 are themselves the pinned Seraphis
/// Phase 5 values, the sigma being the one authorised field.
///
/// THE SIGMA IS NOT LOAD-BEARING HERE, which is exactly why the deviation is
/// tolerable: clause (b) is a RELATIVE bound (probability 1 vs probability 0
/// over the same render, same detector), so a mis-set sigma can only make both
/// counts move together - it can neither hide a reverse-only click nor make the
/// comparison vacuously true.
constexpr float kClickThresholdSigma = 14.0f;

/// @brief Detections reported over one buffer. Fresh detector per call: the
///        detector carries no cross-call state worth preserving (`reset()` only
///        clears the detection vector, artifact_detection.h:122-124).
[[nodiscard]] std::size_t countClicks(const std::vector<float>& buffer) {
    if (buffer.size() < 2u) {
        return 0u;  // detect() returns empty below 2 samples (artifact_detection.h:136)
    }
    // Designated initialisers in DECLARATION order (artifact_detection.h:38-44):
    // Clang rejects narrowing in brace initialisation and rejects out-of-order
    // designators outright.
    Krate::DSP::TestUtils::ClickDetectorConfig cfg{
        .sampleRate = static_cast<float>(kSampleRate),
        .frameSize = std::size_t{512},
        .hopSize = std::size_t{256},
        .detectionThreshold = kClickThresholdSigma,
        .energyThresholdDb = -60.0f,
        .mergeGap = std::size_t{5}};
    Krate::DSP::TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(buffer.data(), buffer.size()).size();
}

// =============================================================================
// A STREAMING form of the shared excitation
// =============================================================================

/// @brief The `VoragoGhostFix::excitePinkPlusTone` recipe, generated one sample
///        at a time so a ten-minute span costs no buffer.
///
/// WHY NOT THE FIXTURE ITSELF. `excitePinkPlusTone` fills a `std::span`, and the
/// span here would be 28 800 000 samples x 2 channels = 230 MB. Calling it
/// repeatedly on a small buffer is NOT an option either: it re-seeds both RNG
/// streams, re-zeroes the three-pole pink state and resets the tone phase on
/// every call (atmosphere_ghost_fixtures.h:182-192), so a chunked call pattern
/// would emit the SAME chunk over and over and would slam a step discontinuity
/// into the capture ring at every chunk boundary - straight into clause (b)'s
/// detector.
///
/// This class is the same arithmetic with the state hoisted into members. Every
/// constant below is read from the fixture header rather than retyped
/// (`kExciteSaltLeft`/`kExciteSaltRight` `:148-149`, `kToneCyclesPerSample`
/// `:155`, `kNoiseGain`/`kToneGain` `:159-160`); the three pink coefficients and
/// the 0.1848f white mix-in are Paul Kellet's economy filter, transcribed from
/// `:201-207`. `advance()` below then re-synchronises this stream with the
/// pre-roll the fixture's own helper rendered, so the span is the genuine
/// CONTINUATION of the pre-roll and the junction carries no discontinuity.
class StreamExciter {
public:
    explicit StreamExciter(std::uint32_t seed) noexcept
        : rngLeft_(Krate::DSP::deriveStreamSeed(seed, VoragoGhostFix::kExciteSaltLeft)),
          rngRight_(Krate::DSP::deriveStreamSeed(seed, VoragoGhostFix::kExciteSaltRight)) {}

    /// Generate and DISCARD `samples` samples, so this stream stands exactly
    /// where `excitePinkPlusTone(seed)` would stand after that many samples.
    void advance(std::size_t samples) noexcept {
        float dummyL = 0.0f;
        float dummyR = 0.0f;
        for (std::size_t i = 0; i < samples; ++i) {
            step(dummyL, dummyR);
        }
    }

    /// Fill both spans; the shorter one bounds the written region, mirroring the
    /// fixture's own contract (atmosphere_ghost_fixtures.h:176-178).
    void fill(std::span<float> left, std::span<float> right) noexcept {
        const std::size_t count = std::min(left.size(), right.size());
        for (std::size_t i = 0; i < count; ++i) {
            step(left[i], right[i]);
        }
    }

private:
    void step(float& outLeft, float& outRight) noexcept {
        const float wL = rngLeft_.nextFloat();  // [-1, 1]
        const float wR = rngRight_.nextFloat();

        bL0_ = 0.99765f * bL0_ + wL * 0.0990460f;
        bL1_ = 0.96300f * bL1_ + wL * 0.2965164f;
        bL2_ = 0.57000f * bL2_ + wL * 0.1050186f;
        const float pinkL = bL0_ + bL1_ + bL2_ + wL * 0.1848f;

        bR0_ = 0.99765f * bR0_ + wR * 0.0990460f;
        bR1_ = 0.96300f * bR1_ + wR * 0.2965164f;
        bR2_ = 0.57000f * bR2_ + wR * 0.1050186f;
        const float pinkR = bR0_ + bR1_ + bR2_ + wR * 0.1848f;

        // Quadrature tone, exactly as the fixture (`:213-215`).
        const float toneL = static_cast<float>(std::sin(phase_));
        const float toneR = static_cast<float>(std::cos(phase_));

        outLeft = VoragoGhostFix::kNoiseGain * pinkL + VoragoGhostFix::kToneGain * toneL;
        outRight = VoragoGhostFix::kNoiseGain * pinkR + VoragoGhostFix::kToneGain * toneR;

        phase_ += kPhaseStep;
        if (phase_ >= kTwoPiD) {
            phase_ -= kTwoPiD;
        }
    }

    static constexpr double kTwoPiD = static_cast<double>(Krate::DSP::kTwoPi);
    static constexpr double kPhaseStep = kTwoPiD * VoragoGhostFix::kToneCyclesPerSample;

    Xorshift32 rngLeft_;
    Xorshift32 rngRight_;
    float bL0_ = 0.0f;
    float bL1_ = 0.0f;
    float bL2_ = 0.0f;
    float bR0_ = 0.0f;
    float bR1_ = 0.0f;
    float bR2_ = 0.0f;
    double phase_ = 0.0;
};

// =============================================================================
// One arm of SC-003
// =============================================================================

/// Everything the three clauses need, gathered in ONE render: the full-ring
/// pre-roll plus ten minutes is the expensive part and is paid once per
/// probability, never once per assertion.
struct LivenessArm {
    std::size_t capacity = 0u;         ///< getCaptureCapacitySamples()
    std::size_t preRollSamples = 0u;   ///< what preRollFullRing() actually rendered
    std::uint64_t coldStartSkips = 0u; ///< ring-cold counter at the END of the pre-roll
    std::uint64_t coldSkipsAtEnd = 0u; ///< ... and after the ten minutes
    std::uint64_t bornAtSpanStart = 0u;
    std::uint64_t bornAtSpanEnd = 0u;
    std::size_t clickDetections = 0u;  ///< summed over both channels and all chunks
    float minObservedAge = 0.0f;
    float maxObservedAge = 0.0f;
    /// Read back off the engine, never transcribed: clause (a)'s ceiling is
    /// computed from what the component actually holds.
    float density = 0.0f;
    float jitter = 0.0f;
    double wallSeconds = 0.0;          ///< measured, reported, never asserted on
};

/// @brief Full-ring pre-roll, then `kSpanSamples` of render, at `probability`.
///
/// The ONLY difference between the two arms is
/// `setGrainReverseProbability()`. Everything else - prepare geometry, seed,
/// the seven FR-017 control values, the excitation, the block grid and the
/// chunk grid - is bit-identical, which is what makes clause (b)'s comparison a
/// measurement of REVERSE rather than of two unrelated renders.
[[nodiscard]] LivenessArm runLivenessArm(float probability) {
    LivenessArm arm;

    const auto started = std::chrono::steady_clock::now();

    // The engine is far too large for the stack in a Catch2 test frame.
    auto engine = std::make_unique<AtmosphereEngine>();
    VoragoGhostFix::applyVoragoGhost(*engine);
    engine->setGrainReverseProbability(probability);

    arm.capacity = engine->getCaptureCapacitySamples();
    arm.density = engine->getDensity();  // :849
    arm.jitter = engine->getJitter();    // :856

    // --- (a) the full-ring pre-roll: T002's helper, which renders exactly
    //     getCaptureCapacitySamples() samples of the shared excitation.
    arm.preRollSamples = VoragoGhostFix::preRollFullRing(*engine, kSeed, kBlock);
    arm.coldStartSkips = engine->getSkippedTriggerCountRingCold();
    arm.bornAtSpanStart = engine->getTotalGrainsBorn();

    // --- the measured span. The exciter is wound forward to the end of the
    //     pre-roll so the span continues that same stream sample-for-sample.
    StreamExciter exciter{kSeed};
    exciter.advance(arm.preRollSamples);

    std::vector<float> inLeft(kBlock, 0.0f);
    std::vector<float> inRight(kBlock, 0.0f);
    std::vector<float> outLeft(kBlock, 0.0f);
    std::vector<float> outRight(kBlock, 0.0f);

    std::vector<float> chunkLeft;
    std::vector<float> chunkRight;
    chunkLeft.reserve(kClickChunkSamples);
    chunkRight.reserve(kClickChunkSamples);

    std::size_t rendered = 0u;
    while (rendered < kSpanSamples) {
        const std::size_t n = std::min(kBlock, kSpanSamples - rendered);
        exciter.fill(std::span<float>(inLeft).first(n), std::span<float>(inRight).first(n));
        engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                   n);
        rendered += n;

        chunkLeft.insert(chunkLeft.end(), outLeft.data(), outLeft.data() + n);
        chunkRight.insert(chunkRight.end(), outRight.data(), outRight.data() + n);
        if (chunkLeft.size() >= kClickChunkSamples) {
            arm.clickDetections += countClicks(chunkLeft) + countClicks(chunkRight);
            chunkLeft.clear();
            chunkRight.clear();
        }
    }
    // The tail chunk (kSpanSamples / kClickChunkSamples is not an integer), so
    // no rendered sample escapes the detector.
    arm.clickDetections += countClicks(chunkLeft) + countClicks(chunkRight);

    arm.coldSkipsAtEnd = engine->getSkippedTriggerCountRingCold();
    arm.bornAtSpanEnd = engine->getTotalGrainsBorn();
    arm.minObservedAge = engine->getMinObservedGrainAgeSamples();
    arm.maxObservedAge = engine->getMaxObservedGrainAgeSamples();

    const auto finished = std::chrono::steady_clock::now();
    arm.wallSeconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(finished - started).count();

    return arm;
}

/// @brief The ceiling on ring-cold skips a pre-roll of `preRollSamples` can
///        contain, COMPUTED rather than transcribed (spec SC-003 (a)).
///
/// `1 + floor(preRollSeconds / shortestInteronsetSeconds)`:
///   * the leading 1 is the FREE TICK at sample 0 - `GrainScheduler::reset()`
///     leaves `samplesUntilNextGrain_ = 0.0f` (grain_scheduler.h:40) and
///     `process()` decrements BEFORE testing `<= 0.0f` (`:71-76`), so a freshly
///     reset engine attempts a birth on its very first sample, with
///     `getAvailableSamples() == 1`;
///   * the shortest interonset is the nominal `1 / density` scaled by the most
///     favourable jitter draw: `process()` sets the next interval to
///     `interonsetSamples_ * (1 + u * jitter * 0.5)` with `u` in [-1, 1]
///     (grain_scheduler.h:78-84), so the floor factor is `1 - 0.5 * jitter`.
/// At the Vorago point (`density = 0.30`, `jitter = 0.5`): `(1/0.30) * 0.75 =
/// 2.5 s`, and `1 + floor(21.845 / 2.5) = 9`.
///
/// It is a CEILING, never an equality: `samplesUntilNextGrain_` is a draw, so
/// the realised count is bounded and not predicted.
[[nodiscard]] std::uint64_t coldSkipCeiling(std::size_t preRollSamples, double sampleRate,
                                            float density, float jitter) {
    const double effectiveDensity =
        static_cast<double>(std::max(AtmosphereEngine::kMinDensity, density));
    const double shortestInteronsetSeconds =
        (1.0 / effectiveDensity) * (1.0 - 0.5 * static_cast<double>(jitter));
    const double preRollSeconds = static_cast<double>(preRollSamples) / sampleRate;
    return std::uint64_t{1} +
           static_cast<std::uint64_t>(std::floor(preRollSeconds / shortestInteronsetSeconds));
}

// =============================================================================
// SC-007 (d) - the accelerated reverse-fraction sweep
// =============================================================================
//
// WHY AN ACCELERATED FIXTURE AT ALL. 100 000 grains at the Vorago ghost
// operating point (`density = 0.30`) would be 333 333 s of audio - ninety-two
// hours. The sweep below buys the same 100 000 BIRTHS in ~78 s of audio per
// probability by changing only the three quantities that set the birth RATE and
// none that touch the reverse DECISION:
//   * `grainSeconds = kMinGrainSeconds` (0.05, atmosphere_engine.h:310), so a
//     grain occupies its pool slot for 400 samples and the 64-slot pool
//     (`kMaxGrains`, `:198`) turns over 64 grains every 400 samples;
//   * one `triggerGrain()` per sample (FR-020's second birth site), which is
//     what actually claims those slots - pass A consumes at most one pending
//     request per sample (`atmosphere_engine.h:2377-2381`);
//   * 8 kHz, the same accelerated rate Phase 10's own ghost fixture runs at
//     (`vorago_engine_test.cpp:2730-2756`), so those 400 samples are 0.05 s of
//     audio rather than 2 400 samples of it.
// The reverse draw itself is untouched: it is one `reverseRng_.nextUnipolar() <
// reverseProbability_` per birth ATTEMPT that clears the slot sweep
// (`atmosphere_engine.h:1761`), on its own stream, at whatever rate births
// happen.
//
// WHY THE SAMPLE IS A CLEAN BINOMIAL, WHICH IS THE WHOLE POINT. The measured
// fraction is `totalReverseBorn_ / totalBorn_`, and both counters move at the
// SAME site (`:1974-1977`). Three things could bias it, and the fixture closes
// each one:
//   1. an attempt that consumes the draw but is then REJECTED counts in neither
//      numerator nor denominator - harmless only if rejection is direction-
//      BLIND, which on a FILLING ring it is not (FR-050's deficit clause,
//      `:1899-1906`, is reverse-only). So the ring is pre-rolled FULL
//      (`preRollFullRing`) and the case REQUIREs the ring-cold counter not to
//      move once across the span: every draw taken inside it became a birth.
//   2. pool-full rejections are taken BEFORE the four birth draws and before
//      the reverse draw - the slot sweep returns early at `:1739-1741` - so the
//      ~525 000 of them this driver provokes consume no reverse randomness at
//      all and cannot skew the fraction.
//   3. a DIRECTION-DEPENDENT lifetime would let reverse and forward grains hold
//      their slots for different spans and so bias WHICH draws become births.
//      `pitchSemitones = 0`, `pitchSpread = 0` and `driftRangeSemitones = 0`
//      pin `ratioMin == ratioMax == 1`, so the forward window is `w = 0` and
//      the reverse one `w = 1 + ratioMax = 2` (`:1796-1797`); both are far
//      inside the slack at this geometry, so BOTH directions truncate to the
//      full requested 400 samples. The case ASSERTS that lifetime rather than
//      assuming it.
//
// THIS IS A CALIBRATION CHECK, NOT A TIGHT RNG TEST, AND IT MUST NOT BE
// TIGHTENED INTO ONE. At n = 100 000 the 3-sigma band on the fraction is
// 3*sqrt(p(1-p)/n) <= 0.0048, so the +/-0.02 the spec names (SC-007 (d)) is
// roughly a 13-sigma bound: it catches a MISCALIBRATED probability - an
// inverted comparison, a draw on the wrong stream, a `<=` where `<` belongs, a
// per-block rather than per-birth decision - and it is deliberately wide enough
// that no seed will ever flake it. Tightening it to the 3-sigma band would
// convert a calibration criterion into a flake generator.

/// Sweep rate. See the acceleration note above.
constexpr double kSweepSampleRate = 8000.0;

/// Render AND trigger block. Equal to `AtmosphereEngine::kControlChunkSamples`
/// (64, `atmosphere_engine.h:280`), so one block is exactly one control chunk
/// and the `anyPending` hoist at `:2334` sees the queue this loop just filled.
constexpr std::size_t kSweepBlock = 64u;
static_assert(kSweepBlock == AtmosphereEngine::kControlChunkSamples,
              "one sweep block must be exactly one control chunk, or the per-block trigger "
              "arithmetic below stops being exact");
static_assert(kSweepBlock == AtmosphereEngine::kMaxGrains,
              "the driver queues one trigger per sample of a block and pass A consumes at most "
              "one per sample, so a block longer than the kMaxGrains-deep pending queue would "
              "DROP requests and a shorter one would leave them pending across the boundary");

/// SC-007 (d)'s sample size: 100 000 births per probability.
constexpr std::uint64_t kSweepTargetGrains = 100000u;

/// SC-007 (d)'s calibration tolerance. NOT to be tightened - see above.
constexpr double kReverseFractionTolerance = 0.02;

/// Hard ceiling on the render loop, ~4x the expected 9 766 blocks (100 000
/// births at the steady-state 64 births / 400 samples = 10.24 per block). It
/// exists so that an engine which STOPS birthing fails the sample-size REQUIRE
/// loudly instead of spinning for ever; it is never a silent early exit.
constexpr std::size_t kSweepBlockCap = 40000u;

/// One probability's worth of the sweep. Everything the assertions read is
/// gathered here, so the render is paid once per probability.
struct FractionArm {
    float probability = 0.0f;
    std::size_t capacity = 0u;         ///< getCaptureCapacitySamples()
    std::size_t preRollSamples = 0u;   ///< what preRollFullRing() actually rendered
    std::size_t spanSamples = 0u;      ///< the measured span, in samples
    std::uint64_t born = 0u;           ///< delta getTotalGrainsBorn() over the span
    std::uint64_t reverseBorn = 0u;    ///< delta getTotalReverseGrainsBorn()
    std::uint64_t ringColdDelta = 0u;  ///< delta getSkippedTriggerCountRingCold()
    std::uint64_t droppedDelta = 0u;   ///< delta getDroppedTriggerCount()
    std::uint64_t lastLifetime = 0u;   ///< getLastBornGrainLifetimeSamples() at the end
    double wallSeconds = 0.0;          ///< measured, reported, never asserted on
};

/// @brief Full-ring pre-roll, then render at one trigger per sample until
///        `kSweepTargetGrains` grains have been born at `probability`.
[[nodiscard]] FractionArm runReverseFractionArm(float probability) {
    FractionArm arm;
    arm.probability = probability;

    const auto started = std::chrono::steady_clock::now();

    // Too large for a Catch2 stack frame, exactly as the liveness arm above.
    auto engine = std::make_unique<AtmosphereEngine>();

    // The shared fixture with the acceleration note's declared deviations:
    // 8 kHz, the smallest legal ring, blur OFF (an output-stage STFT that no
    // birth statistic can see, and the dominant per-sample cost), and the
    // shortest legal grain. `density` goes to its floor so the density
    // scheduler contributes at most a handful of the 100 000 births - they are
    // drawn from the same stream at the same site, so they are not excluded,
    // merely made negligible.
    VoragoGhostFix::GhostConfig cfg{};
    cfg.sampleRate = kSweepSampleRate;
    cfg.seed = kSeed;
    cfg.captureSeconds = AtmosphereEngine::kMinCaptureSeconds;
    cfg.blurEnabled = false;
    cfg.density = AtmosphereEngine::kMinDensity;
    cfg.grainSeconds = AtmosphereEngine::kMinGrainSeconds;
    cfg.pitchSemitones = 0.0f;  // with the two spreads below: ratioMin == ratioMax == 1
    cfg.positionSpread = 0.0f;
    cfg.decorrelation = 0.0f;  // decorrAge is an ungettable draw; 0 keeps the window exact
    VoragoGhostFix::applyVoragoGhost(*engine, cfg);

    // Not fields of GhostConfig, so written directly. These two are what pin the
    // ratio - and therefore the direction-blindness of the truncation; see point
    // 3 of the note above.
    engine->setPitchSpread(0.0f);          // atmosphere_engine.h:906
    engine->setDriftRangeSemitones(0.0f);  // atmosphere_engine.h:928
    engine->setGrainReverseProbability(probability);

    arm.capacity = engine->getCaptureCapacitySamples();
    arm.preRollSamples = VoragoGhostFix::preRollFullRing(*engine, kSeed, kSweepBlock);

    // Snapshots AFTER the pre-roll: the cold-ring skips of a FILLING ring belong
    // to the pre-roll, and it is the span's own delta that must be zero.
    const std::uint64_t born0 = engine->getTotalGrainsBorn();
    const std::uint64_t reverse0 = engine->getTotalReverseGrainsBorn();
    const std::uint64_t ringCold0 = engine->getSkippedTriggerCountRingCold();
    const std::uint64_t dropped0 = engine->getDroppedTriggerCount();

    // The span continues the pre-roll's excitation sample-for-sample: a step
    // discontinuity at the junction would be captured into the ring and then
    // read back by every grain born afterwards.
    StreamExciter exciter{kSeed};
    exciter.advance(arm.preRollSamples);

    std::vector<float> inLeft(kSweepBlock, 0.0f);
    std::vector<float> inRight(kSweepBlock, 0.0f);
    std::vector<float> outLeft(kSweepBlock, 0.0f);
    std::vector<float> outRight(kSweepBlock, 0.0f);

    std::size_t blocks = 0u;
    while ((engine->getTotalGrainsBorn() - born0) < kSweepTargetGrains && blocks < kSweepBlockCap) {
        // Fill the pending queue to exactly kMaxGrains. It is empty at every
        // block boundary (each of the previous block's kSweepBlock samples
        // consumed one, `:2377-2381`), and `triggerGrain()` drops only once the
        // count has REACHED kMaxGrains (`:1087-1091`), so the kSweepBlock-th
        // call here still lands - which is what the zero-drop assertion in the
        // case below witnesses per run.
        for (std::size_t t = 0; t < kSweepBlock; ++t) {
            engine->triggerGrain();
        }
        exciter.fill(std::span<float>(inLeft), std::span<float>(inRight));
        engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                   kSweepBlock);
        ++blocks;
    }

    arm.spanSamples = blocks * kSweepBlock;
    arm.born = engine->getTotalGrainsBorn() - born0;
    arm.reverseBorn = engine->getTotalReverseGrainsBorn() - reverse0;
    arm.ringColdDelta = engine->getSkippedTriggerCountRingCold() - ringCold0;
    arm.droppedDelta = engine->getDroppedTriggerCount() - dropped0;
    arm.lastLifetime = engine->getLastBornGrainLifetimeSamples();

    const auto finished = std::chrono::steady_clock::now();
    arm.wallSeconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(finished - started).count();

    return arm;
}
}  // namespace

// =============================================================================
// SC-003 - AtmosphereGhost_ReverseLiveness  [long]
// =============================================================================
// TAGGED [long] ON BOTH OF CLAUDE.md's CLAUSES (FR-048): the measured runtime is
// far over ~15 s (two ten-minute renders plus two full-ring pre-rolls), and
// every assertion below is toolchain-INDEPENDENT - three integer counter
// comparisons, one relative detection-count comparison and two inequalities on
// a folded age. No NaN/Inf guard, no bounded grid and no state format is
// asserted here, so nothing that belongs in the per-push cross-platform lane is
// being smuggled into the nightly one.
//
// WHAT MAKES CLAUSE (a) BITE. tasks.md T012 frames this case as a NO-OP GUARD
// "until T013": against a FORWARD-only window - `wUp = max(ratioMax - 1, 0)`,
// `wDown = max(1 - ratioMin, 0)` with no `reversed` term - probability 1 and
// probability 0 render the same admissions, so every clause would pass
// vacuously. READ THIS SESSION, that is no longer the state of the header:
// `tryBirthGrain()` computes `wUp = reversed ? 0.0 : max(ratioMax - 1, 0)` and
// `wDown = reversed ? 1 + ratioMax : max(1 - ratioMin, 0)`
// (atmosphere_engine.h:1732-1733), and FR-050's reverse fill clause is present
// at `:1835-1843`. For a reverse grain `wDown = 1 + ratioMax` nearly triples the
// window, which is precisely the change that could start emptying it - and
// clause (a) is the assertion, over ten minutes, that it does not. FR-050's
// `ratioMax * min(lifetime, capacity - avail)` deficit is zero here only because
// this fixture pre-rolls the ring FULL; the filling-ring case is T011 (A)'s.

// NO `SECTION`s IN THIS CASE, DELIBERATELY. Catch2 re-executes the whole
// TEST_CASE body once per leaf SECTION, so wrapping the three clauses in
// sections would render the two arms THREE TIMES - six ten-minute renders for
// three assertions that read the same two results. The clauses are therefore
// plain sequential blocks, each labelled with its spec letter, and the arms are
// rendered exactly once.

// FR-048 TAG DECISION, MADE FROM THE MEASUREMENT AND NOT FROM THE ESTIMATE.
// Measured 2026-09-24, Release, MSVC 19.44, nothing else running: **6.990 s**
// (Catch2 `-d yes`). FR-048 tags [long] ONLY above ~15 s, so the tag this case
// carried on the estimate ("the full-ring pre-roll, 21.845 s, plus 10 min" -
// both AUDIO durations, not wall-clock) comes off: 600 s of audio at 48 kHz in
// 64-sample blocks costs 7 s of CPU, not 7 minutes. Untagged, it runs every
// push, which is where a toolchain-independent liveness assertion belongs.
TEST_CASE("AtmosphereGhost_ReverseLiveness", "[atmosphere][ghost]") {
    const LivenessArm reverseArm = runLivenessArm(1.0f);
    const LivenessArm forwardArm = runLivenessArm(0.0f);

    WARN("AtmosphereGhost_ReverseLiveness measured runtime: reverse arm "
         << reverseArm.wallSeconds << " s, forward arm " << forwardArm.wallSeconds << " s, total "
         << (reverseArm.wallSeconds + forwardArm.wallSeconds) << " s (2 x ["
         << reverseArm.preRollSamples << " pre-roll + " << kSpanSamples
         << " span] samples at 48 kHz); reverse clicks " << reverseArm.clickDetections
         << ", forward clicks " << forwardArm.clickDetections);

    // --- Fixture sanity. Not the criterion: these are the preconditions every
    //     clause below reads, and a silent failure of any of them would make the
    //     case vacuous rather than red.
    REQUIRE(reverseArm.capacity == std::size_t{1048576});  // nextPowerOf2(20 x 48 000)
    REQUIRE(reverseArm.preRollSamples == reverseArm.capacity);
    REQUIRE(forwardArm.capacity == reverseArm.capacity);
    REQUIRE(forwardArm.preRollSamples == reverseArm.preRollSamples);

    // --- (a) the ring-cold counter does not advance over ten minutes.
    //     density and jitter come off the engine, not off this line, so the
    //     ceiling tracks the fixture if the operating point ever moves.
    REQUIRE(reverseArm.density == 0.30f);  // the FR-017 value, vorago_perf_test.cpp:622
    REQUIRE(reverseArm.jitter == 0.5f);    // the shipped default, atmosphere_engine.h:2820
    const std::uint64_t ceiling = coldSkipCeiling(reverseArm.preRollSamples, kSampleRate,
                                                  reverseArm.density, reverseArm.jitter);
    REQUIRE(ceiling == std::uint64_t{9});  // the spec's figure, recomputed rather than trusted

    // Cold-start skips belong to the pre-roll, where the ring genuinely is cold
    // and the counter is monotonic until reset() (which also empties the ring,
    // so it cannot be cleared into a warm state).
    REQUIRE(reverseArm.coldStartSkips <= ceiling);

    // THE ASSERTION WITH TEETH: across ten minutes on a FULL ring, not one
    // further birth is rejected for want of history. The birth window is
    // non-empty at the Vorago ghost operating point.
    REQUIRE(reverseArm.coldSkipsAtEnd == reverseArm.coldStartSkips);

    // --- (b) reverse adds no clicks relative to forward.
    //     A RELATIVE bound (spec SC-003 (b), amended 2026-09-23 R-10). The
    //     absolute count is a property of the granulation's own artifact floor
    //     and of the detector sigma; the DIFFERENCE is a property of the reverse
    //     read walk, which is what this phase changes.
    REQUIRE(reverseArm.clickDetections <= forwardArm.clickDetections);

    // --- (c) FR-015: every observed read age stays inside [kMinAgeSamples, C - 2].
    //     The folds are documented as meaningless before the first birth
    //     (atmosphere_engine.h:1128-1134), so the guard comes first - and it is
    //     also what stops this clause passing on an engine that never sounded.
    REQUIRE(reverseArm.bornAtSpanEnd > reverseArm.bornAtSpanStart);

    // THIS IS A BOUND ON THE COMPUTED AGE, AND IT IS EXPLICITLY NOT FR-050's
    // BACKSTOP (plan P-1). The computed age stays inside [64, C - 2] exactly
    // while the READ is clamped and therefore stale: the clamp is what keeps the
    // fold in range, so this clause can never observe a stale read. FR-050's
    // binding criterion is T011 (A)'s filling-ring case, and the protected
    // quantity is measured only in T013's clause-disabled run.
    REQUIRE(reverseArm.minObservedAge >= static_cast<float>(AtmosphereEngine::kMinAgeSamples));
    REQUIRE(reverseArm.maxObservedAge <= static_cast<float>(reverseArm.capacity) - 2.0f);
}

// =============================================================================
// SC-007 (d) - AtmosphereGhost_Determinism_ReverseFraction  [long]
// =============================================================================
// TAGGED [long] ON BOTH OF CLAUDE.md's CLAUSES (FR-048): three arms of 100 000
// grains each cost far more than ~15 s, and the assertion is toolchain-
// INDEPENDENT - an integer ratio against a +/-0.02 calibration band, with no
// float golden, no NaN/Inf guard, no bounded grid and no state format anywhere
// in the case. Nothing that belongs in the per-push cross-platform lane is
// being smuggled into the nightly one.
//
// SC-007 (a)(b)(c) live in `atmosphere_ghost_test.cpp`'s
// `AtmosphereGhost_Determinism` (T018) and stay in the per-push lane; only
// clause (d)'s sweep is here, because only it needs 300 000 grains.
//
// NO `SECTION`s, for the reason the liveness case above states: Catch2 re-runs
// the whole body per leaf section, so sections would triple an already
// expensive render. The three probabilities are arms, rendered once each.

// FR-048 TAG DECISION FROM THE MEASUREMENT: **1.361 s** (2026-09-24, Release,
// MSVC 19.44, `-d yes`, nothing else running) - two orders of magnitude under
// FR-048s ~15 s bar, so the estimate-era [long] tag comes off.
TEST_CASE("AtmosphereGhost_Determinism_ReverseFraction", "[atmosphere][ghost]") {
    const std::array<FractionArm, 3> arms{runReverseFractionArm(0.25f),
                                          runReverseFractionArm(0.50f),
                                          runReverseFractionArm(0.75f)};

    double totalWall = 0.0;
    for (const FractionArm& arm : arms) {
        totalWall += arm.wallSeconds;
    }
    WARN("AtmosphereGhost_Determinism_ReverseFraction measured runtime: "
         << arms[0].wallSeconds << " s (p=0.25) + " << arms[1].wallSeconds << " s (p=0.50) + "
         << arms[2].wallSeconds << " s (p=0.75) = " << totalWall << " s total; spans "
         << arms[0].spanSamples << " / " << arms[1].spanSamples << " / " << arms[2].spanSamples
         << " samples at " << kSweepSampleRate << " Hz; births " << arms[0].born << " / "
         << arms[1].born << " / " << arms[2].born << "; reverse fractions "
         << (static_cast<double>(arms[0].reverseBorn) / static_cast<double>(arms[0].born)) << " / "
         << (static_cast<double>(arms[1].reverseBorn) / static_cast<double>(arms[1].born)) << " / "
         << (static_cast<double>(arms[2].reverseBorn) / static_cast<double>(arms[2].born)));

    // The ring geometry, CHARACTERISED rather than transcribed:
    // `RollingCaptureBuffer::prepare()` rounds the requested capacity UP to a
    // power of two (`rolling_capture_buffer.h:75-93`), so at
    // `kMinCaptureSeconds = 1` and 8 kHz the capacity is the unique power of two
    // in [8 000, 16 000) - i.e. 8 192 - and saying so this way survives a
    // geometry change instead of hiding one behind a literal.
    const std::size_t requestedRing = static_cast<std::size_t>(
        static_cast<double>(AtmosphereEngine::kMinCaptureSeconds) * kSweepSampleRate);

    // The requested grain length in samples. `tryBirthGrain()` forms it as
    // `std::round(grainSeconds_ * sampleRate_)` (atmosphere_engine.h:1805), and
    // at this geometry neither direction's window truncates it - which is point
    // 3 of the fixture note, asserted rather than assumed.
    const std::uint64_t requestedLifetime = static_cast<std::uint64_t>(std::llround(
        static_cast<double>(AtmosphereEngine::kMinGrainSeconds) * kSweepSampleRate));

    for (const FractionArm& arm : arms) {
        const double fraction =
            static_cast<double>(arm.reverseBorn) / static_cast<double>(arm.born);

        INFO("p=" << arm.probability << " born=" << arm.born << " reverse=" << arm.reverseBorn
                  << " fraction=" << fraction << " span=" << arm.spanSamples
                  << " capacity=" << arm.capacity << " preRoll=" << arm.preRollSamples
                  << " ringColdDelta=" << arm.ringColdDelta
                  << " droppedDelta=" << arm.droppedDelta
                  << " lastLifetime=" << arm.lastLifetime);

        // --- Fixture preconditions. Not the criterion: these are what make the
        //     denominator a clean binomial sample, and a silent failure of any
        //     of them would make the fraction a measurement of the ADMISSION
        //     path rather than of the reverse draw.
        REQUIRE(arm.capacity >= requestedRing);
        REQUIRE(arm.capacity < 2u * requestedRing);
        REQUIRE((arm.capacity & (arm.capacity - std::size_t{1})) == std::size_t{0});
        REQUIRE(arm.preRollSamples == arm.capacity);  // the ring is WARM, so FR-050 is vacuous

        // Sample size actually achieved. This is also what catches the render
        // loop having hit kSweepBlockCap: a stalled birth path fails HERE, with
        // the counts in the INFO above, instead of quietly shrinking n.
        REQUIRE(arm.born >= kSweepTargetGrains);

        // Every reverse draw taken inside the span became a birth: no attempt
        // that consumed randomness was rejected, in either direction. (Pool-full
        // skips take no draw at all - they return before it.)
        REQUIRE(arm.ringColdDelta == std::uint64_t{0});

        // The driver never overfilled the pending queue, so no requested birth
        // was silently lost and the trigger rate is the one the note claims.
        REQUIRE(arm.droppedDelta == std::uint64_t{0});

        // Direction-blind truncation: the last birth of a 100 000-grain run at
        // p = 0.25 / 0.5 / 0.75 is reverse or forward depending on one draw, and
        // it lives the full requested span either way.
        REQUIRE(arm.lastLifetime == requestedLifetime);

        // --- SC-007 (d), THE CRITERION. A CALIBRATION BAND, NOT A 3-SIGMA RNG
        //     TEST: at n >= 100 000 the 3-sigma band is <= 0.0048, so +/-0.02 is
        //     ~13 sigma and will not flake. DO NOT TIGHTEN IT.
        REQUIRE(std::abs(fraction - static_cast<double>(arm.probability))
                <= kReverseFractionTolerance);
    }
}
