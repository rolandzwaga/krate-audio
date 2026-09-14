// ==============================================================================
// Layer 3: System Tests - SubharmonicEngine, main behavioural cases
// ==============================================================================
// Vorago Phase 6 (specs/vorago-phase6-subharmonic): SubharmonicEngine.
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase6-subharmonic/spec.md
//            specs/vorago-phase6-subharmonic/plan.md   (S10.2, S10.3, S11)
//            specs/vorago-phase6-subharmonic/tasks.md  (T001 creates this TU;
//                                                       T006 lands the helper
//                                                       self-check; T007 the
//                                                       compile-time half of
//                                                       StructuralBounds; T008
//                                                       ControlSurfaceContract,
//                                                       NoAllocation,
//                                                       RateAndReprepare and the
//                                                       FR-073 ledger; later
//                                                       tasks the render cases)
//
// SCOPE OF THIS TU (plan S10.2): SC-001, SC-006, SC-007, SC-008, SC-010, SC-011,
//   SC-012, SC-014, SC-019, SC-020, SC-021, SC-022, plus
//   SubharmonicEngine_ControlSurfaceContract (the FR-009 argument contract, the
//   FR-003 default-constructed contract and the FR-050 guard ladder),
//   SubharmonicEngine_ClampScope (the Q6/D-12 clamp-scope edge case) and the
//   FR-052/FR-073 ledger case.
//
// This TU is deliberately OUT of the "-fno-fast-math" block in
//   dsp/tests/CMakeLists.txt, so the FR-008/FR-009 guards are also proved in the
//   /fp:fast + -ffast-math mode the header actually ships in. Non-finite values
//   are therefore NOT named here - they live only in
//   subharmonic_engine_nonfinite_test.cpp.
//
// ALLOCATION DETECTION: include <allocation_detector.h> ONLY. The single owner
//   of the global operator new/delete override in dsp_systems_tests is
//   dsp/tests/unit/systems/selectable_oscillator_test.cpp:388, and a second
//   include of <allocation_operator_overrides.h> is a duplicate-symbol link
//   error.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "artifact_detection.h"
#include "low_frequency_metrics.h"
#include "render_fingerprint.h"
#include "spectral_analysis.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/pink_noise_filter.h>
#include <krate/dsp/primitives/two_pole_lp.h>
#include <krate/dsp/processors/sub_oscillator.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/subharmonic_engine.h>

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace lfm = Krate::DSP::TestUtils;

constexpr double kOutputSelfCheckSampleRate = 48000.0;
constexpr double kOutputSelfCheckTwoPi = 6.283185307179586;

/// Fill `buffer` with a sine. At 1 kHz / 48 kHz the period is exactly 48
/// samples, so sample index 12 lands on sin(pi/2) = 1 and the SAMPLE peak of a
/// unit-amplitude tone is exactly full scale - which makes "0 dBTP" the truth
/// the true-peak reading is compared against, not an approximation of it.
void fillSine(std::vector<float>& buffer, double freqHz, double amplitude) {
    const double omega = kOutputSelfCheckTwoPi * freqHz / kOutputSelfCheckSampleRate;
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(amplitude * std::sin(omega * static_cast<double>(i)));
    }
}

/// Fill `buffer` with uniform bipolar noise from a seeded Xorshift32
/// (core/random.h:41), the deterministic source used across the Phase 3-6 TUs.
void fillNoise(std::vector<float>& buffer, std::uint32_t seed) {
    Krate::DSP::Xorshift32 rng{seed};
    for (float& sample : buffer) {
        sample = rng.nextFloat();
    }
}

[[nodiscard]] float samplePeak(const std::vector<float>& a, const std::vector<float>& b) {
    float peak = 0.0f;
    for (const float s : a) {
        peak = std::max(peak, std::fabs(s));
    }
    for (const float s : b) {
        peak = std::max(peak, std::fabs(s));
    }
    return peak;
}

// ==============================================================================
// T008 shared helpers
// ==============================================================================

using Krate::DSP::SubharmonicEngine;
using Krate::DSP::SubWaveform;

/// FR-073's ledger, restated INDEPENDENTLY of the header (plan S9): two
/// MinBlepTable tables of 16 x 64 floats (4096 B each), three Residual ring
/// buffers of 16 floats (64 B each) and the SaturationProcessor dry buffer of
/// maxBlockSamples floats. Written as a literal here on purpose - a test that
/// reused the class's own constant would agree with any future arithmetic
/// mistake inside it.
[[nodiscard]] std::size_t expectedAllocatedBytes(std::size_t maxBlockSamples) {
    return std::size_t{8384} + std::size_t{4} * maxBlockSamples;
}

/// The rate-derived ceilings of FR-013 and FR-040, computed from the RATE
/// rather than read back from the engine.
[[nodiscard]] float expectedMaxFundamentalHz(double sampleRate) {
    return std::min(SubharmonicEngine::kMaxFundamentalHz,
                    SubharmonicEngine::kMasterNyquistRatio * static_cast<float>(sampleRate));
}

[[nodiscard]] float expectedMaxLowpassHz(double sampleRate) {
    return std::min(SubharmonicEngine::kMaxLowpassHz,
                    SubharmonicEngine::kLowpassNyquistRatio * static_cast<float>(sampleRate));
}

/// Every configuration scalar FR-060 exposes, written in declaration order.
/// SC-019 (a) re-pushes this after EVERY prepare(), because prepare() step (9)
/// runs applyDefaults() and is the one path back to the FR-003 defaults.
///
/// Tracking is pushed to 0 and every breath depth to 0 so the render the two
/// rates are compared on is a stationary, deterministic function of the tone
/// configuration alone.
constexpr std::uint32_t kFullConfigurationSeed = 0x5EED0006u;

void pushFullConfiguration(SubharmonicEngine& engine) {
    engine.setFundamentalHz(55.0f);
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        engine.setToneLevelDb(t, -12.0f - 4.0f * static_cast<float>(t));
        engine.setToneWaveform(t, SubWaveform::Sine);
        engine.setToneBreathRate(t, 0.05f + 0.01f * static_cast<float>(t));
        engine.setToneBreathDepth(t, 0.0f);
    }
    engine.setTrackingAmount(0.0f);
    engine.setTrackReferenceDb(-18.0f);
    engine.setFollowerAttackMs(120.0f);
    engine.setFollowerReleaseMs(800.0f);
    engine.setLowpassCutoffHz(400.0f);
    engine.setDriveDb(3.0f);
    engine.setWetGainDb(0.0f);
    engine.setSubToMainEnabled(true);
    engine.setSeed(kFullConfigurationSeed);
}

/// RMS after two cascaded 2-pole Butterworth low-passes at 200 Hz - the
/// "sub-band (< 200 Hz)" measure SC-019 (a) compares across sample rates. Two
/// stages give 24 dB/oct, and the magnitude response is a function of
/// FREQUENCY, so the same band is measured at 44.1 kHz and at 96 kHz (the
/// bilinear warping of a 200 Hz corner differs by well under 0.1 % between the
/// two rates).
[[nodiscard]] float subBandRms(const std::vector<float>& signal, double sampleRate) {
    if (signal.empty()) {
        return 0.0f;
    }
    Krate::DSP::TwoPoleLP stageA;
    Krate::DSP::TwoPoleLP stageB;
    stageA.prepare(sampleRate);
    stageB.prepare(sampleRate);
    stageA.setCutoff(200.0f);
    stageB.setCutoff(200.0f);

    double sum = 0.0;
    for (const float s : signal) {
        const double y = static_cast<double>(stageB.process(stageA.process(s)));
        sum += y * y;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(signal.size())));
}

/// Render `seconds` of a mono-identical 55 Hz body through the engine in
/// 512-sample blocks and return the low-passed RMS of (out - in) over the LAST
/// half of the render.
[[nodiscard]] float renderSubBandRms(SubharmonicEngine& engine, double sampleRate,
                                     double seconds) {
    constexpr std::size_t kBlock = 512;
    const auto total = static_cast<std::size_t>(seconds * sampleRate);
    const double omega = kOutputSelfCheckTwoPi * 55.0 / sampleRate;

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    std::vector<float> difference;
    difference.reserve(total);

    for (std::size_t start = 0; start < total; start += kBlock) {
        const std::size_t n = std::min(kBlock, total - start);
        for (std::size_t i = 0; i < n; ++i) {
            const auto phase = static_cast<double>(start + i) * omega;
            inL[i] = static_cast<float>(0.25 * std::sin(phase));
            inR[i] = inL[i];
        }
        engine.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            difference.push_back(outL[i] - inL[i]);
        }
    }

    const std::size_t half = difference.size() / 2;
    const std::vector<float> tail(difference.begin() + static_cast<std::ptrdiff_t>(half),
                                  difference.end());
    return subBandRms(tail, sampleRate);
}

/// Every FR-061 getter in one comparable aggregate. SC-019 (b) is "every getter
/// equals a freshly prepared instance's", which is only a real assertion if the
/// snapshot is exhaustive - so this struct carries the whole read surface.
struct ReadSurface {
    double sampleRate = 0.0;
    std::size_t maxBlockSamples = 0;
    bool prepared = false;
    float fundamentalHz = 0.0f;
    std::array<float, SubharmonicEngine::kNumTones> toneFrequencyHz{};
    std::array<float, SubharmonicEngine::kNumTones> toneLevelDb{};
    std::array<int, SubharmonicEngine::kNumTones> toneWaveform{};
    std::array<float, SubharmonicEngine::kNumTones> toneBreathRate{};
    std::array<float, SubharmonicEngine::kNumTones> toneBreathDepth{};
    std::array<float, SubharmonicEngine::kNumTones> toneBreathValue{};
    std::array<float, SubharmonicEngine::kNumTones> toneCurrentGain{};
    std::array<bool, SubharmonicEngine::kNumTones> toneDormant{};
    std::array<bool, SubharmonicEngine::kNumTones> toneFloored{};
    float trackingAmount = 0.0f;
    float trackedEnvelope = 0.0f;
    float trackingGain = 0.0f;
    float trackReferenceDb = 0.0f;
    float followerAttackMs = 0.0f;
    float followerReleaseMs = 0.0f;
    float lowpassCutoffHz = 0.0f;
    float driveDb = 0.0f;
    float wetGainDb = 0.0f;
    bool subToMainEnabled = false;
    std::uint32_t seed = 0;
    std::uint32_t clampEngagements = 0;
    std::size_t allocatedBytes = 0;
};

[[nodiscard]] ReadSurface captureReadSurface(const SubharmonicEngine& engine) {
    ReadSurface s;
    s.sampleRate = engine.getSampleRate();
    s.maxBlockSamples = engine.getMaxBlockSamples();
    s.prepared = engine.isPrepared();
    s.fundamentalHz = engine.getFundamentalHz();
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        s.toneFrequencyHz[t] = engine.getToneFrequencyHz(t);
        s.toneLevelDb[t] = engine.getToneLevelDb(t);
        s.toneWaveform[t] = static_cast<int>(engine.getToneWaveform(t));
        s.toneBreathRate[t] = engine.getToneBreathRate(t);
        s.toneBreathDepth[t] = engine.getToneBreathDepth(t);
        s.toneBreathValue[t] = engine.getToneBreathValue(t);
        s.toneCurrentGain[t] = engine.getToneCurrentGain(t);
        s.toneDormant[t] = engine.isToneDormant(t);
        s.toneFloored[t] = engine.isToneInfrasonicFloored(t);
    }
    s.trackingAmount = engine.getTrackingAmount();
    s.trackedEnvelope = engine.getTrackedEnvelope();
    s.trackingGain = engine.getTrackingGain();
    s.trackReferenceDb = engine.getTrackReferenceDb();
    s.followerAttackMs = engine.getFollowerAttackMs();
    s.followerReleaseMs = engine.getFollowerReleaseMs();
    s.lowpassCutoffHz = engine.getLowpassCutoffHz();
    s.driveDb = engine.getDriveDb();
    s.wetGainDb = engine.getWetGainDb();
    s.subToMainEnabled = engine.getSubToMainEnabled();
    s.seed = engine.getSeed();
    s.clampEngagements = engine.getClampEngagementCount();
    s.allocatedBytes = engine.getAllocatedBytes();
    return s;
}

// ==============================================================================
// T010 shared helpers - the `makeDefaults` fixture (tasks.md "The two shared
// fixtures") and the partitioned-render driver SC-012 is built on
// ==============================================================================

/// `makeDefaults` (tasks.md): 48 kHz, PrepareConfig{.maxBlockSamples = 512},
/// EVERY shipped default untouched, one fixed seed. The body the fixture is
/// rendered against is a steady -12 dBFS 55 Hz sine on both channels, produced
/// by fillBody() below.
///
/// The seed is pushed AFTER prepare(): prepare() step (12) distributes seed_
/// and step (13) resets, so a post-prepare setSeed() re-derives the three
/// breath streams and reset()s each modulator - the render therefore starts
/// from the top of the same three breath trajectories on every call.
constexpr std::uint32_t kDefaultsFixtureSeed = 0x5EED0610u;
constexpr double kDefaultsFixtureSampleRate = 48000.0;
constexpr std::size_t kDefaultsFixtureBlock = 512;

/// -12 dBFS = 10^(-12/20). Written as the literal rather than computed so the
/// fixture level is visible at the call site.
constexpr double kBodyAmplitude = 0.251188643150958;
constexpr double kBodyHz = 55.0;

void makeDefaults(SubharmonicEngine& engine,
                  double sampleRate = kDefaultsFixtureSampleRate,
                  std::size_t maxBlockSamples = kDefaultsFixtureBlock) {
    engine.prepare(sampleRate,
                   SubharmonicEngine::PrepareConfig{.maxBlockSamples = maxBlockSamples});
    engine.setSeed(kDefaultsFixtureSeed);
}

/// The fixture body: a steady -12 dBFS 55 Hz sine, identical on both channels.
/// Generated from the ABSOLUTE sample index so a partitioned render sees the
/// same waveform as a block-aligned one - the input must not be a function of
/// the partition, or SC-012 (a) would be measuring the fixture.
void fillBody(std::vector<float>& body, double sampleRate = kDefaultsFixtureSampleRate) {
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / sampleRate;
    for (std::size_t i = 0; i < body.size(); ++i) {
        body[i] = static_cast<float>(kBodyAmplitude * std::sin(omega * static_cast<double>(i)));
    }
}

/// Render `body` through `engine` in the chunk sizes `nextChunk` yields, into
/// `outL`/`outR` (both pre-sized to `body.size()`). `tap`, when non-null, is
/// pre-sized the same way and receives the FR-062 sub tap.
///
/// The chunk sizes are the ONLY thing that varies between the two arms of
/// SC-012 (a): the same input samples reach the engine in the same order in
/// both, so any difference in the output is the engine's control grid leaking
/// the host's partition.
template <typename ChunkFn>
void renderPartitioned(SubharmonicEngine& engine, const std::vector<float>& body,
                       std::vector<float>& outL, std::vector<float>& outR,
                       std::vector<float>* tap, ChunkFn nextChunk) {
    const std::size_t total = body.size();
    std::size_t done = 0;
    while (done < total) {
        const std::size_t n = std::min(nextChunk(), total - done);
        if (n == 0) {
            break;  // unreachable: both partitions below always yield >= 1
        }
        const float* in = body.data() + done;
        float* tapPtr = (tap != nullptr) ? (tap->data() + done) : nullptr;
        engine.processBlockTapped(in, in, outL.data() + done, outR.data() + done, tapPtr, n);
        done += n;
    }
}

/// A seeded pseudo-random partition of chunk sizes in [1, 1024] (SC-012 (a)).
/// Xorshift32 is the deterministic source the Phase 3-6 TUs already use, so the
/// partition is identical on every platform and the comparison is reproducible.
class RandomPartition {
public:
    explicit RandomPartition(std::uint32_t seed) : rng_(seed) {}
    std::size_t operator()() noexcept {
        return std::size_t{1} + static_cast<std::size_t>(rng_.next() % 1024u);
    }

private:
    Krate::DSP::Xorshift32 rng_;
};

/// A fixed chunk size, for the reference arm of every invariance comparison.
class FixedPartition {
public:
    explicit FixedPartition(std::size_t size) : size_(size) {}
    std::size_t operator()() const noexcept { return size_; }

private:
    std::size_t size_;
};

/// The fingerprint comparison SC-012 asserts with, on both channels.
[[nodiscard]] bool rendersAgree(const std::vector<float>& actualL,
                                const std::vector<float>& actualR,
                                const std::vector<float>& referenceL,
                                const std::vector<float>& referenceR,
                                std::string& detail) {
    const auto cmpL = Krate::DSP::TestUtils::compareFingerprints(
        Krate::DSP::TestUtils::fingerprintRender(actualL),
        Krate::DSP::TestUtils::fingerprintRender(referenceL));
    const auto cmpR = Krate::DSP::TestUtils::compareFingerprints(
        Krate::DSP::TestUtils::fingerprintRender(actualR),
        Krate::DSP::TestUtils::fingerprintRender(referenceR));
    detail = "L: worstMetric=" + std::to_string(cmpL.worstMetricRelativeError) +
             " worstSample=" + std::to_string(cmpL.worstSampleError) + " (" + cmpL.detail +
             "); R: worstMetric=" + std::to_string(cmpR.worstMetricRelativeError) +
             " worstSample=" + std::to_string(cmpR.worstSampleError) + " (" + cmpR.detail + ")";
    return cmpL.withinTolerance() && cmpR.withinTolerance();
}

}  // namespace

// ==============================================================================
// T006 - low_frequency_metrics.h self-validation (output-metrics half)
// ==============================================================================
// Plan S10.1: each helper is validated against a synthetic signal of known truth
// in the TU that consumes it, BEFORE the engine does, so a helper bug fails as a
// helper bug. This half covers the two helpers SC-006 and SC-007 lean on:
// measureTruePeakDb and calculateCorrelation.
//
// measureTruePeakDb folds the RAW sample into the maximum (the
// true_peak_limiter.h:132-136 basis), so "never below the sample peak" is a
// structural guarantee and the noise arm asserts exactly that.
// ==============================================================================
TEST_CASE("SubharmonicEngine_OutputMetricsSelfCheck", "[subharmonic_engine]") {
    SECTION("measureTruePeakDb reads 0 dBTP on a full-scale 1 kHz sine") {
        constexpr std::size_t kNumSamples = 48000;  // 1 s
        std::vector<float> left(kNumSamples, 0.0f);
        std::vector<float> right(kNumSamples, 0.0f);
        fillSine(left, 1000.0, 1.0);
        right = left;

        const float truePeakDb = lfm::measureTruePeakDb(left.data(), right.data(), kNumSamples,
                                                       kOutputSelfCheckSampleRate);

        INFO("full-scale 1 kHz sine measured " << truePeakDb << " dBTP");
        REQUIRE(std::fabs(truePeakDb) <= 0.1f);
    }

    SECTION("measureTruePeakDb is never below the sample peak") {
        constexpr std::size_t kNumSamples = 48000;
        std::vector<float> left(kNumSamples, 0.0f);
        std::vector<float> right(kNumSamples, 0.0f);
        fillNoise(left, 0x51A3C7u);
        fillNoise(right, 0x9E3779B9u);

        const float peak = samplePeak(left, right);
        REQUIRE(peak > 0.0f);
        const float samplePeakDb = 20.0f * std::log10(peak);

        const float truePeakDb = lfm::measureTruePeakDb(left.data(), right.data(), kNumSamples,
                                                       kOutputSelfCheckSampleRate);

        INFO("sample peak " << samplePeakDb << " dBFS, true peak " << truePeakDb << " dBTP");
        // Structural: the raw sample is folded into the max, so this can only
        // fail if the fold is dropped.
        REQUIRE(truePeakDb >= samplePeakDb - 1e-4f);
    }

    SECTION("calculateCorrelation reads +1, -1 and ~0 on known pairs") {
        constexpr std::size_t kNumSamples = 100000;

        std::vector<float> a(kNumSamples, 0.0f);
        fillNoise(a, 12345u);

        std::vector<float> inverted(kNumSamples, 0.0f);
        for (std::size_t i = 0; i < kNumSamples; ++i) {
            inverted[i] = -a[i];
        }

        std::vector<float> independent(kNumSamples, 0.0f);
        fillNoise(independent, 987654321u);

        const float rIdentical = lfm::calculateCorrelation(a.data(), a.data(), kNumSamples);
        const float rInverted = lfm::calculateCorrelation(a.data(), inverted.data(), kNumSamples);
        const float rIndependent =
            lfm::calculateCorrelation(a.data(), independent.data(), kNumSamples);

        INFO("identical " << rIdentical << ", inverted " << rInverted << ", independent "
                          << rIndependent);
        REQUIRE(std::fabs(rIdentical - 1.0f) <= 1e-5f);
        REQUIRE(std::fabs(rInverted + 1.0f) <= 1e-5f);
        // Two independent 100 000-sample streams: the expected |r| is ~1/sqrt(N)
        // = 0.003, so 0.05 is ~15 standard deviations of headroom.
        REQUIRE(std::fabs(rIndependent) < 0.05f);
    }
}

// ==============================================================================
// T007 - SubharmonicEngine_StructuralBounds (compile-time half)
// ==============================================================================
// Plan S1.2 / S7.1: FR-052's rung 1 is ENTIRELY compile-time - two constexpr
// magnitudes and their static_asserts, with no runtime code behind them. The
// header failing to compile IS the falsification for the skeleton, and these
// STATIC_REQUIREs are the visible half of it: they restate, in the test suite,
// the four structural facts a later task could silently break without breaking
// anything else.
//
// STATIC_REQUIRE, not REQUIRE: every operand is a compile-time constant, so a
// runtime check here would only prove the compiler can read a constant back.
//
// The FR-073 allocation-ledger half of this case (getAllocatedBytes() ==
// 8384 + 4 * maxBlockSamples, and the [64, 8192] clamp on the request) arrives
// with prepare() in T008.
// ==============================================================================
TEST_CASE("SubharmonicEngine_StructuralBounds", "[subharmonic_engine]") {
    using Krate::DSP::SubharmonicEngine;

    SECTION("FR-010: three tones") {
        STATIC_REQUIRE(SubharmonicEngine::kNumTones == std::size_t{3});
    }

    SECTION("FR-007: the shared 64-sample control grid") {
        STATIC_REQUIRE(SubharmonicEngine::kControlChunkSamples == std::size_t{64});
    }

    SECTION("FR-054: the clamp is a backstop, not a shaping stage") {
        // The worst-case magnitude reaching the clamp (~2.00) sits strictly
        // below the clamp threshold (4.0), so no reachable configuration can
        // make FR-054's rung engage on level alone.
        STATIC_REQUIRE(SubharmonicEngine::kMaxPreClampMagnitude <
                       SubharmonicEngine::kOutputClamp);
    }

    SECTION("FR-052: the saturator, not the clamp, catches the peak") {
        // ~17.36 into a stage bounded at 1.0 - and the 17.36 carries
        // SubOscillator::sanitize's [-2, +2] output bound (S14 C-4), which the
        // spec's original formula omitted.
        STATIC_REQUIRE(SubharmonicEngine::kMaxPreSaturationMagnitude >
                       SubharmonicEngine::kSaturatorOutputBound);
    }

    // ---- T008: the FR-073 allocation ledger (plan S9) -----------------------
    SECTION("FR-073: the ledger is 8384 + 4 * maxBlockSamples at every size") {
        for (const std::size_t maxBlock : {std::size_t{64}, std::size_t{512}, std::size_t{8192}}) {
            SubharmonicEngine engine;
            engine.prepare(48000.0, SubharmonicEngine::PrepareConfig{.maxBlockSamples = maxBlock});

            INFO("maxBlockSamples " << maxBlock << " reported " << engine.getAllocatedBytes()
                                    << " B, expected " << expectedAllocatedBytes(maxBlock) << " B");
            REQUIRE(engine.getMaxBlockSamples() == maxBlock);
            REQUIRE(engine.getAllocatedBytes() == expectedAllocatedBytes(maxBlock));
        }
    }

    SECTION("FR-073: the request is clamped to [64, 8192] and the ledger follows it") {
        SubharmonicEngine engine;

        engine.prepare(48000.0, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 16});
        REQUIRE(engine.getMaxBlockSamples() == std::size_t{64});
        REQUIRE(engine.getAllocatedBytes() == expectedAllocatedBytes(64));

        engine.prepare(48000.0, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 99999});
        REQUIRE(engine.getMaxBlockSamples() == std::size_t{8192});
        REQUIRE(engine.getAllocatedBytes() == expectedAllocatedBytes(8192));
    }
}

// ==============================================================================
// T008 / T010 - SubharmonicEngine_ControlSurfaceContract
// ==============================================================================
// T008 landed three normative contracts, none of which needs a rendered sample:
//
//  * FR-003: a DEFAULT-CONSTRUCTED, UNPREPARED engine already reports every
//    documented default. Three of those rows differ from the composed object's
//    own shipped default (waveform Sine over SubOscillator's Square, follower
//    120/800 ms over the shipped 10/100), and because getToneBreathRate,
//    getFollowerAttackMs and getFollowerReleaseMs FORWARD to the composed
//    object (plan S8), a build that stores a default and never pushes it is
//    caught here and nowhere else on the read surface.
//  * FR-009: every named range clamps at BOTH ends, written and read back.
//  * FR-060/FR-061: an out-of-range tone index is a silent no-op on every
//    setter and the documented neutral on every getter.
//
// T010 adds the FR-050 GUARD LADDER - the three render-entry contracts no
// criterion otherwise owns: the pre-prepare() passthrough (a DIVERGENCE from
// FeedbackEcology, which zero-fills at :951-955, so a copy-the-neighbour
// implementation silences the dry path and fails here), the null-pointer no-op
// at every position, and the zero-length call that must consume no control
// step because FR-007's residue is ABSOLUTE rather than block-relative.
//
// Non-finite REJECTION is SC-009 (a) and lives in the IEEE TU (T019): this TU
// compiles with -ffast-math and must never name a NaN.
// ==============================================================================
TEST_CASE("SubharmonicEngine_ControlSurfaceContract", "[subharmonic_engine]") {
    constexpr float kTol = 1.0e-5f;

    SECTION("FR-003: the defaults are live on a default-constructed, unprepared engine") {
        const SubharmonicEngine engine;

        REQUIRE_FALSE(engine.isPrepared());

        constexpr std::array<float, 3> kLevels{-18.0f, -24.0f, -30.0f};
        constexpr std::array<float, 3> kRates{0.037f, 0.023f, 0.014f};
        constexpr std::array<float, 3> kDepths{0.35f, 0.25f, 0.45f};

        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t << ": level " << engine.getToneLevelDb(t) << " dB, breath rate "
                         << engine.getToneBreathRate(t) << " Hz, depth "
                         << engine.getToneBreathDepth(t));
            REQUIRE(std::fabs(engine.getToneLevelDb(t) - kLevels[t]) <= kTol);
            // Forwarded from BreathingModulator::getRate() - a stored-but-never
            // -pushed rate is not representable here.
            REQUIRE(std::fabs(engine.getToneBreathRate(t) - kRates[t]) <= kTol);
            REQUIRE(std::fabs(engine.getToneBreathDepth(t) - kDepths[t]) <= kTol);
            REQUIRE(engine.getToneWaveform(t) == SubWaveform::Sine);
        }

        // FR-031 pushes 120 / 800 into the follower, whose own shipped defaults
        // are 10 / 100 ms (envelope_follower.h:92-93). Both getters forward, so
        // this arm is what fails if applyDefaults() omits the push.
        INFO("follower " << engine.getFollowerAttackMs() << " / " << engine.getFollowerReleaseMs()
                         << " ms");
        REQUIRE(std::fabs(engine.getFollowerAttackMs() - 120.0f) <= kTol);
        REQUIRE(std::fabs(engine.getFollowerReleaseMs() - 800.0f) <= kTol);

        REQUIRE(std::fabs(engine.getFundamentalHz() - 55.0f) <= kTol);
        REQUIRE(std::fabs(engine.getTrackingAmount() - 1.0f) <= kTol);
        REQUIRE(std::fabs(engine.getTrackReferenceDb() + 18.0f) <= kTol);
        REQUIRE(std::fabs(engine.getLowpassCutoffHz() - 120.0f) <= kTol);
        REQUIRE(std::fabs(engine.getDriveDb() - 3.0f) <= kTol);
        REQUIRE(std::fabs(engine.getWetGainDb()) <= kTol);
        REQUIRE(engine.getSubToMainEnabled());
    }

    SECTION("FR-009: every named range clamps at both ends") {
        constexpr double kFs = 48000.0;
        SubharmonicEngine engine;
        engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        // ---- fundamental: [8, min(4186, 0.3 * fs)] --------------------------
        engine.setFundamentalHz(-1.0f);
        REQUIRE(std::fabs(engine.getFundamentalHz() - SubharmonicEngine::kMinFundamentalHz)
                <= kTol);
        engine.setFundamentalHz(1.0e6f);
        INFO("fundamental ceiling at 48 kHz: " << engine.getFundamentalHz());
        REQUIRE(std::fabs(engine.getFundamentalHz() - expectedMaxFundamentalHz(kFs)) <= 1.0e-3f);

        // ---- per-tone ranges -------------------------------------------------
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);

            engine.setToneLevelDb(t, -1.0e4f);
            REQUIRE(std::fabs(engine.getToneLevelDb(t) - SubharmonicEngine::kMinToneLevelDb)
                    <= kTol);
            engine.setToneLevelDb(t, 1.0e4f);
            REQUIRE(std::fabs(engine.getToneLevelDb(t) - SubharmonicEngine::kMaxToneLevelDb)
                    <= kTol);

            // The forwarding getter is the point: the engine clamps with
            // kMin/kMaxBreathRateHz and the modulator clamps again with its own,
            // and the two must agree or this reads the modulator bound instead.
            engine.setToneBreathRate(t, 0.0f);
            REQUIRE(std::fabs(engine.getToneBreathRate(t) - SubharmonicEngine::kMinBreathRateHz)
                    <= kTol);
            engine.setToneBreathRate(t, 1.0e3f);
            REQUIRE(std::fabs(engine.getToneBreathRate(t) - SubharmonicEngine::kMaxBreathRateHz)
                    <= kTol);

            engine.setToneBreathDepth(t, -5.0f);
            REQUIRE(std::fabs(engine.getToneBreathDepth(t)) <= kTol);
            engine.setToneBreathDepth(t, 5.0f);
            REQUIRE(std::fabs(engine.getToneBreathDepth(t) - 1.0f) <= kTol);

            for (const SubWaveform w :
                 {SubWaveform::Square, SubWaveform::Sine, SubWaveform::Triangle}) {
                engine.setToneWaveform(t, w);
                REQUIRE(engine.getToneWaveform(t) == w);
            }
        }

        // ---- tracking --------------------------------------------------------
        engine.setTrackingAmount(-3.0f);
        REQUIRE(std::fabs(engine.getTrackingAmount()) <= kTol);
        engine.setTrackingAmount(3.0f);
        REQUIRE(std::fabs(engine.getTrackingAmount() - 1.0f) <= kTol);

        engine.setTrackReferenceDb(-1.0e4f);
        REQUIRE(std::fabs(engine.getTrackReferenceDb() - SubharmonicEngine::kMinTrackReferenceDb)
                <= kTol);
        engine.setTrackReferenceDb(1.0e4f);
        REQUIRE(std::fabs(engine.getTrackReferenceDb() - SubharmonicEngine::kMaxTrackReferenceDb)
                <= kTol);

        // Forwarded from the follower, which clamps with the same bounds the
        // header static_asserts against.
        engine.setFollowerAttackMs(0.0f);
        REQUIRE(std::fabs(engine.getFollowerAttackMs() - SubharmonicEngine::kMinFollowerAttackMs)
                <= kTol);
        engine.setFollowerAttackMs(1.0e5f);
        REQUIRE(std::fabs(engine.getFollowerAttackMs() - SubharmonicEngine::kMaxFollowerAttackMs)
                <= kTol);
        engine.setFollowerReleaseMs(0.0f);
        REQUIRE(std::fabs(engine.getFollowerReleaseMs() - SubharmonicEngine::kMinFollowerReleaseMs)
                <= kTol);
        engine.setFollowerReleaseMs(1.0e5f);
        REQUIRE(std::fabs(engine.getFollowerReleaseMs() - SubharmonicEngine::kMaxFollowerReleaseMs)
                <= kTol);

        // ---- chain -----------------------------------------------------------
        engine.setLowpassCutoffHz(0.0f);
        REQUIRE(std::fabs(engine.getLowpassCutoffHz() - SubharmonicEngine::kMinLowpassHz) <= kTol);
        engine.setLowpassCutoffHz(1.0e5f);
        INFO("low-pass ceiling at 48 kHz: " << engine.getLowpassCutoffHz());
        REQUIRE(std::fabs(engine.getLowpassCutoffHz() - expectedMaxLowpassHz(kFs)) <= 1.0e-3f);

        engine.setDriveDb(-50.0f);
        REQUIRE(std::fabs(engine.getDriveDb() - SubharmonicEngine::kMinDriveDb) <= kTol);
        engine.setDriveDb(50.0f);
        REQUIRE(std::fabs(engine.getDriveDb() - SubharmonicEngine::kMaxDriveDb) <= kTol);

        engine.setWetGainDb(-1.0e4f);
        REQUIRE(std::fabs(engine.getWetGainDb() - SubharmonicEngine::kMinWetGainDb) <= kTol);
        engine.setWetGainDb(1.0e4f);
        REQUIRE(std::fabs(engine.getWetGainDb() - SubharmonicEngine::kMaxWetGainDb) <= kTol);
    }

    SECTION("FR-060/FR-061: an out-of-range tone index is a silent no-op") {
        SubharmonicEngine engine;
        engine.prepare(48000.0, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        // A neighbour whose state must survive every out-of-range write.
        engine.setToneLevelDb(0, -9.0f);
        engine.setToneBreathRate(0, 0.2f);
        engine.setToneBreathDepth(0, 0.6f);
        engine.setToneWaveform(0, SubWaveform::Triangle);

        for (const std::size_t bad : {SubharmonicEngine::kNumTones, ~std::size_t{0}}) {
            INFO("out-of-range tone index " << bad);

            engine.setToneLevelDb(bad, 5.0f);
            engine.setToneBreathRate(bad, 0.4f);
            engine.setToneBreathDepth(bad, 0.1f);
            engine.setToneWaveform(bad, SubWaveform::Square);

            // The neighbour is untouched.
            REQUIRE(std::fabs(engine.getToneLevelDb(0) + 9.0f) <= kTol);
            REQUIRE(std::fabs(engine.getToneBreathRate(0) - 0.2f) <= kTol);
            REQUIRE(std::fabs(engine.getToneBreathDepth(0) - 0.6f) <= kTol);
            REQUIRE(engine.getToneWaveform(0) == SubWaveform::Triangle);

            // Every getter returns the documented neutral.
            REQUIRE(std::fabs(engine.getToneFrequencyHz(bad)) <= kTol);
            REQUIRE(std::fabs(engine.getToneLevelDb(bad)) <= kTol);
            REQUIRE(std::fabs(engine.getToneBreathRate(bad)) <= kTol);
            REQUIRE(std::fabs(engine.getToneBreathDepth(bad)) <= kTol);
            REQUIRE(std::fabs(engine.getToneBreathValue(bad)) <= kTol);
            REQUIRE(std::fabs(engine.getToneCurrentGain(bad)) <= kTol);
            REQUIRE(engine.getToneWaveform(bad) == SubWaveform::Sine);
            REQUIRE_FALSE(engine.isToneDormant(bad));
            REQUIRE_FALSE(engine.isToneInfrasonicFloored(bad));
        }
    }

    // =====================================================================
    // T010 - the FR-050 guard ladder
    // =====================================================================
    // Three normative contracts no other criterion owns. Each is a promise the
    // spec makes explicitly (spec.md:672-675, :1310-1319), not a consequence of
    // some composed object's behaviour, so each is asserted directly.

    SECTION("FR-050: before prepare() the dry path passes through and the tap is zeroed") {
        constexpr std::size_t kN = 96;
        std::vector<float> inL(kN, 0.0f);
        std::vector<float> inR(kN, 0.0f);
        for (std::size_t i = 0; i < kN; ++i) {
            // Non-trivial AND channel-distinct: a passthrough that copied one
            // channel over both, or wrote zeros, must fail here.
            inL[i] = static_cast<float>(0.7 * std::sin(0.31 * static_cast<double>(i)));
            inR[i] = static_cast<float>(-0.4 * std::cos(0.17 * static_cast<double>(i)) + 0.05);
        }

        SubharmonicEngine engine;
        REQUIRE_FALSE(engine.isPrepared());

        // (i) out of place.
        std::vector<float> outL(kN, -99.0f);
        std::vector<float> outR(kN, -99.0f);
        std::vector<float> tap(kN, 7.0f);
        engine.processBlockTapped(inL.data(), inR.data(), outL.data(), outR.data(), tap.data(),
                                  kN);
        for (std::size_t i = 0; i < kN; ++i) {
            INFO("sample " << i);
            REQUIRE(outL[i] == inL[i]);
            REQUIRE(outR[i] == inR[i]);
            REQUIRE(tap[i] == 0.0f);
        }

        // (ii) IN PLACE (outL == inL, outR == inR). The plan's guard skips the
        // self-copy rather than relying on std::copy_n over a range whose
        // destination lies inside the source - which is UB, not benign.
        std::vector<float> ioL = inL;
        std::vector<float> ioR = inR;
        std::fill(tap.begin(), tap.end(), 7.0f);
        engine.processBlockTapped(ioL.data(), ioR.data(), ioL.data(), ioR.data(), tap.data(), kN);
        for (std::size_t i = 0; i < kN; ++i) {
            INFO("in-place sample " << i);
            REQUIRE(ioL[i] == inL[i]);
            REQUIRE(ioR[i] == inR[i]);
            REQUIRE(tap[i] == 0.0f);
        }

        // (iii) the untapped entry point carries the same promise.
        std::fill(outL.begin(), outL.end(), -99.0f);
        std::fill(outR.begin(), outR.end(), -99.0f);
        engine.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kN);
        for (std::size_t i = 0; i < kN; ++i) {
            INFO("untapped sample " << i);
            REQUIRE(outL[i] == inL[i]);
            REQUIRE(outR[i] == inR[i]);
        }

        // Nothing was prepared and nothing advanced.
        REQUIRE_FALSE(engine.isPrepared());
        REQUIRE(engine.getClampEngagementCount() == 0u);
    }

    SECTION("FR-050: a null pointer is a silent no-op at every position") {
        constexpr std::size_t kN = 128;
        std::vector<float> body(kN, 0.0f);
        fillBody(body);
        const float* in = body.data();

        SubharmonicEngine control;
        SubharmonicEngine victim;
        makeDefaults(control);
        makeDefaults(victim);

        // A sentinel pattern the guard must leave byte-unchanged.
        std::vector<float> outL(kN, 0.0f);
        std::vector<float> outR(kN, 0.0f);
        std::vector<float> tap(kN, 0.0f);
        for (std::size_t i = 0; i < kN; ++i) {
            outL[i] = -3.5f + 0.25f * static_cast<float>(i);
            outR[i] = 2.75f - 0.125f * static_cast<float>(i);
            tap[i] = 9.0f + static_cast<float>(i);
        }
        const std::vector<float> sentinelL = outL;
        const std::vector<float> sentinelR = outR;
        const std::vector<float> sentinelTap = tap;

        // All six positions of spec.md:1318-1319, on the tapped entry point and
        // on the untapped one. A null `subTap` is deliberately NOT in this list:
        // it is LEGAL (processBlock is exactly that call) and is asserted as a
        // rendering path by SC-012 (c).
        victim.processBlockTapped(nullptr, in, outL.data(), outR.data(), tap.data(), kN);
        victim.processBlockTapped(in, nullptr, outL.data(), outR.data(), tap.data(), kN);
        victim.processBlockTapped(in, in, nullptr, outR.data(), tap.data(), kN);
        victim.processBlockTapped(in, in, outL.data(), nullptr, tap.data(), kN);
        victim.processBlockTapped(nullptr, nullptr, nullptr, nullptr, nullptr, kN);
        victim.processBlock(nullptr, in, outL.data(), outR.data(), kN);

        for (std::size_t i = 0; i < kN; ++i) {
            INFO("sentinel sample " << i);
            REQUIRE(outL[i] == sentinelL[i]);
            REQUIRE(outR[i] == sentinelR[i]);
            REQUIRE(tap[i] == sentinelTap[i]);
        }

        // ...and NOTHING advanced: the victim now renders exactly what the
        // untouched control does. A guard that returned after running a control
        // step, or after advancing the oscillators, diverges here.
        std::vector<float> victimL(kN, 0.0f);
        std::vector<float> victimR(kN, 0.0f);
        std::vector<float> controlL(kN, 0.0f);
        std::vector<float> controlR(kN, 0.0f);
        victim.processBlockTapped(in, in, victimL.data(), victimR.data(), nullptr, kN);
        control.processBlockTapped(in, in, controlL.data(), controlR.data(), nullptr, kN);
        for (std::size_t i = 0; i < kN; ++i) {
            INFO("post-guard sample " << i);
            REQUIRE(victimL[i] == controlL[i]);
            REQUIRE(victimR[i] == controlR[i]);
        }
    }

    SECTION("FR-050/FR-007: a zero-length call consumes no control step") {
        constexpr std::size_t kN = 2048;  // 32 control steps
        std::vector<float> body(kN, 0.0f);
        fillBody(body);
        const float* in = body.data();

        std::vector<float> referenceL(kN, 0.0f);
        std::vector<float> referenceR(kN, 0.0f);
        std::vector<float> victimL(kN, 0.0f);
        std::vector<float> victimR(kN, 0.0f);

        // Arm 1: zero-length calls at 64-sample-ALIGNED boundaries, i.e.
        // exactly where controlPhase_ == 0. An implementation that runs
        // updateControl() before testing numSamples burns one breath advance,
        // one tracking retarget and one glide step per interleaved call.
        {
            SubharmonicEngine reference;
            SubharmonicEngine victim;
            makeDefaults(reference);
            makeDefaults(victim);
            for (std::size_t done = 0; done < kN; done += std::size_t{64}) {
                reference.processBlockTapped(in + done, in + done, referenceL.data() + done,
                                             referenceR.data() + done, nullptr, std::size_t{64});
                victim.processBlockTapped(in + done, in + done, victimL.data() + done,
                                          victimR.data() + done, nullptr, std::size_t{0});
                victim.processBlockTapped(in + done, in + done, victimL.data() + done,
                                          victimR.data() + done, nullptr, std::size_t{64});
            }
            std::string detail;
            const bool agree = rendersAgree(victimL, victimR, referenceL, referenceR, detail);
            INFO("aligned zero-length interleave: " << detail);
            REQUIRE(agree);
        }

        // Arm 2: zero-length calls at UNALIGNED positions (controlPhase_ == 32),
        // which is what catches an implementation that zeroes the residue on a
        // zero-length call. FR-007's residue is ABSOLUTE, not block-relative.
        {
            SubharmonicEngine reference;
            SubharmonicEngine victim;
            makeDefaults(reference);
            makeDefaults(victim);
            std::fill(referenceL.begin(), referenceL.end(), 0.0f);
            std::fill(referenceR.begin(), referenceR.end(), 0.0f);
            std::fill(victimL.begin(), victimL.end(), 0.0f);
            std::fill(victimR.begin(), victimR.end(), 0.0f);
            for (std::size_t done = 0; done < kN; done += std::size_t{32}) {
                reference.processBlockTapped(in + done, in + done, referenceL.data() + done,
                                             referenceR.data() + done, nullptr, std::size_t{32});
                victim.processBlockTapped(in + done, in + done, victimL.data() + done,
                                          victimR.data() + done, nullptr, std::size_t{32});
                victim.processBlockTapped(in + done, in + done, victimL.data() + done,
                                          victimR.data() + done, nullptr, std::size_t{0});
            }
            std::string detail;
            const bool agree = rendersAgree(victimL, victimR, referenceL, referenceR, detail);
            INFO("unaligned zero-length interleave: " << detail);
            REQUIRE(agree);
        }
    }
}

// ==============================================================================
// T008 - SubharmonicEngine_NoAllocation (SC-010)
// ==============================================================================
// prepare() is the ONLY allocating method (FR-004, FR-073). This case walks
// 10 000 blocks of mixed sizes through BOTH render entry points while pushing
// the ENTIRE setter surface every block, calls reset() repeatedly, and requires
// the process-wide allocation counter to be exactly zero across all of it.
//
// Nothing inside the AllocationScope may allocate for reasons of its own, which
// rules out INFO, CAPTURE and REQUIRE: every buffer is sized before the scope
// opens, every counter is a plain scalar, and every assertion is made after the
// scope closes. The counter is read from the detector singleton while the scope
// is still OPEN - AllocationScope latches its own count in its DESTRUCTOR
// (tests/test_helpers/allocation_detector.h:111-119).
//
// The counters below are the NON-VACUITY evidence: a walk that never crossed
// the fader bottom, never reached the tapped entry point and never reset would
// read zero allocations too, and would prove nothing.
// ==============================================================================
TEST_CASE("SubharmonicEngine_NoAllocation", "[subharmonic_engine]") {
    constexpr std::size_t kBlocks = 10000;
    constexpr std::size_t kMaxSize = 1024;
    constexpr std::array<std::size_t, 6> kSizes{std::size_t{1},  std::size_t{63},
                                                std::size_t{64}, std::size_t{65},
                                                std::size_t{512}, std::size_t{1024}};
    static_assert(kSizes[5] == kMaxSize, "the buffers below must hold the largest block");

    constexpr std::array<SubWaveform, 3> kWaveforms{SubWaveform::Square, SubWaveform::Sine,
                                                    SubWaveform::Triangle};
    constexpr std::size_t kMaxBlockSamples = 512;

    SubharmonicEngine engine;
    // Warm-up prepare OUTSIDE the scope: any first-touch cost belongs to
    // neither measurement (the atmosphere_engine_test.cpp:2270 idiom). Note the
    // configured maxBlockSamples (512) is deliberately SMALLER than the largest
    // rendered block (1024) - FR-073 says the field sizes only the dead
    // SaturationProcessor dry buffer, so a render that quietly grew a scratch
    // buffer to fit would show up here as an allocation.
    engine.prepare(48000.0,
                   SubharmonicEngine::PrepareConfig{.maxBlockSamples = kMaxBlockSamples});

    std::vector<float> inL(kMaxSize, 0.0f);
    std::vector<float> inR(kMaxSize, 0.0f);
    std::vector<float> outL(kMaxSize, 0.0f);
    std::vector<float> outR(kMaxSize, 0.0f);
    std::vector<float> tap(kMaxSize, 0.0f);
    fillSine(inL, 55.0, 0.25);
    fillSine(inR, 55.0, 0.25);

    std::size_t allocations = 0;
    std::size_t blocksRendered = 0;
    std::size_t tappedBlocks = 0;
    std::size_t resets = 0;
    std::size_t seedWrites = 0;
    std::array<bool, 3> sawWaveform{false, false, false};
    bool sawFaderBottom = false;
    bool sawFaderTop = false;

    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        for (std::size_t b = 0; b < kBlocks; ++b) {
            const std::size_t n = kSizes[b % kSizes.size()];
            const auto fb = static_cast<float>(b);

            // ---- the whole setter surface, in declaration order --------------
            engine.setFundamentalHz(20.0f + std::fmod(fb * 3.7f, 200.0f));
            for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
                const auto ft = static_cast<float>(t);
                // -60 .. +10 dB, so both ends of the FR-020 range are crossed and
                // the fader bottom (exact-zero gain, dormancy) is visited.
                const float levelDb = -60.0f + std::fmod(fb * 0.7f + ft * 11.0f, 70.0f);
                engine.setToneLevelDb(t, levelDb);
                if (levelDb <= SubharmonicEngine::kMinToneLevelDb + 0.5f) {
                    sawFaderBottom = true;
                }
                if (levelDb >= SubharmonicEngine::kMaxToneLevelDb) {
                    sawFaderTop = true;
                }
                const std::size_t w = (b + t) % kWaveforms.size();
                engine.setToneWaveform(t, kWaveforms[w]);
                sawWaveform[w] = true;
                engine.setToneBreathRate(t, std::fmod(fb * 0.011f + ft * 0.1f, 0.8f));
                engine.setToneBreathDepth(t, std::fmod(fb * 0.013f + ft * 0.2f, 1.4f));
            }
            engine.setTrackingAmount(std::fmod(fb * 0.017f, 1.3f));
            engine.setTrackReferenceDb(-48.0f + std::fmod(fb * 0.9f, 60.0f));
            engine.setFollowerAttackMs(0.05f + std::fmod(fb * 3.1f, 600.0f));
            engine.setFollowerReleaseMs(0.5f + std::fmod(fb * 17.0f, 6000.0f));
            engine.setLowpassCutoffHz(20.0f + std::fmod(fb * 23.0f, 2500.0f));
            engine.setDriveDb(std::fmod(fb * 0.31f, 14.0f));
            engine.setWetGainDb(-60.0f + std::fmod(fb * 0.53f, 70.0f));
            engine.setSubToMainEnabled((b % 7) != 0);
            if ((b % 5) == 0) {
                engine.setSeed(0xA5000000u + static_cast<std::uint32_t>(b));
                ++seedWrites;
            }

            // ---- both render entry points -----------------------------------
            if ((b % 3) == 0) {
                engine.processBlockTapped(inL.data(), inR.data(), outL.data(), outR.data(),
                                          tap.data(), n);
                ++tappedBlocks;
            } else {
                engine.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
            }
            ++blocksRendered;

            // ---- reset() on the audio thread ---------------------------------
            if ((b % 1000) == 999) {
                engine.reset();
                ++resets;
            }
        }

        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    INFO("allocations " << allocations << " over " << blocksRendered << " blocks (" << tappedBlocks
                        << " tapped, " << resets << " resets, " << seedWrites << " seed writes)");
    REQUIRE(allocations == std::size_t{0});

    // Non-vacuity.
    REQUIRE(blocksRendered == kBlocks);
    REQUIRE(tappedBlocks > std::size_t{0});
    REQUIRE(resets >= std::size_t{10});
    REQUIRE(seedWrites > std::size_t{0});
    REQUIRE(sawFaderBottom);
    REQUIRE(sawFaderTop);
    for (std::size_t w = 0; w < sawWaveform.size(); ++w) {
        INFO("waveform index " << w);
        REQUIRE(sawWaveform[w]);
    }

    // ---- the FR-073 ledger's stability contract ------------------------------
    // The engine was re-prepared by nothing above, so the ledger still describes
    // the warm-up prepare.
    const std::size_t afterWalk = engine.getAllocatedBytes();
    INFO("ledger after the walk: " << afterWalk << " B");
    REQUIRE(afterWalk > std::size_t{0});
    REQUIRE(afterWalk == expectedAllocatedBytes(kMaxBlockSamples));

    engine.reset();
    REQUIRE(engine.getAllocatedBytes() == afterWalk);

    engine.prepare(48000.0,
                   SubharmonicEngine::PrepareConfig{.maxBlockSamples = kMaxBlockSamples});
    REQUIRE(engine.getAllocatedBytes() == afterWalk);
}

namespace {

/// SC-019 (b): "every FR-061 getter equals a freshly prepared instance's",
/// asserted getter by getter rather than through an operator== that could hide
/// which field moved.
void requireSameReadSurface(const ReadSurface& a, const ReadSurface& b) {
    constexpr float kTol = 1.0e-5f;

    REQUIRE(std::fabs(a.sampleRate - b.sampleRate) <= 1.0e-9);
    REQUIRE(a.maxBlockSamples == b.maxBlockSamples);
    REQUIRE(a.prepared == b.prepared);
    REQUIRE(std::fabs(a.fundamentalHz - b.fundamentalHz) <= kTol);

    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        INFO("tone " << t);
        REQUIRE(std::fabs(a.toneFrequencyHz[t] - b.toneFrequencyHz[t]) <= 1.0e-3f);
        REQUIRE(std::fabs(a.toneLevelDb[t] - b.toneLevelDb[t]) <= kTol);
        REQUIRE(a.toneWaveform[t] == b.toneWaveform[t]);
        REQUIRE(std::fabs(a.toneBreathRate[t] - b.toneBreathRate[t]) <= kTol);
        REQUIRE(std::fabs(a.toneBreathDepth[t] - b.toneBreathDepth[t]) <= kTol);
        REQUIRE(std::fabs(a.toneBreathValue[t] - b.toneBreathValue[t]) <= kTol);
        REQUIRE(std::fabs(a.toneCurrentGain[t] - b.toneCurrentGain[t]) <= kTol);
        REQUIRE(a.toneDormant[t] == b.toneDormant[t]);
        REQUIRE(a.toneFloored[t] == b.toneFloored[t]);
    }

    REQUIRE(std::fabs(a.trackingAmount - b.trackingAmount) <= kTol);
    REQUIRE(std::fabs(a.trackedEnvelope - b.trackedEnvelope) <= kTol);
    REQUIRE(std::fabs(a.trackingGain - b.trackingGain) <= kTol);
    REQUIRE(std::fabs(a.trackReferenceDb - b.trackReferenceDb) <= kTol);
    REQUIRE(std::fabs(a.followerAttackMs - b.followerAttackMs) <= kTol);
    REQUIRE(std::fabs(a.followerReleaseMs - b.followerReleaseMs) <= kTol);
    REQUIRE(std::fabs(a.lowpassCutoffHz - b.lowpassCutoffHz) <= kTol);
    REQUIRE(std::fabs(a.driveDb - b.driveDb) <= kTol);
    REQUIRE(std::fabs(a.wetGainDb - b.wetGainDb) <= kTol);
    REQUIRE(a.subToMainEnabled == b.subToMainEnabled);
    REQUIRE(a.seed == b.seed);
    REQUIRE(a.clampEngagements == b.clampEngagements);
    REQUIRE(a.allocatedBytes == b.allocatedBytes);
}

}  // namespace

// ==============================================================================
// T008 - SubharmonicEngine_RateAndReprepare (SC-019)
// ==============================================================================
// Three separate contracts that all live on prepare():
//
//  (a) the configuration is expressed in Hz, dB and ms, so re-preparing at a
//      different rate and re-pushing the same setter sequence must land on the
//      same tone frequencies and the same rendered sub-band level;
//  (b) prepare() is idempotent: running it twice on a fully configured object
//      leaves the read surface where a single prepare() on a fresh object does
//      (prepare() step (9) runs applyDefaults(), which is the ONE path back to
//      the FR-003 defaults);
//  (c) the FR-006 floor is a real floor, and every rate-derived clamp pair stays
//      ORDERED at it - an inverted std::clamp pair is UB and MSVC's <algorithm>
//      fires _STL_VERIFY, so writing both ends of FR-013 and FR-040 at the floor
//      is the assertion that the pairs are the right way round.
// ==============================================================================
TEST_CASE("SubharmonicEngine_RateAndReprepare", "[subharmonic_engine]") {
    constexpr float kTol = 1.0e-5f;

    SECTION("SC-019 (a): the same configuration renders the same at 44.1 and 96 kHz") {
        constexpr double kRateA = 44100.0;
        constexpr double kRateB = 96000.0;
        constexpr double kSeconds = 10.0;

        SubharmonicEngine engine;

        engine.prepare(kRateA, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});
        pushFullConfiguration(engine);
        std::array<float, SubharmonicEngine::kNumTones> freqA{};
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            freqA[t] = engine.getToneFrequencyHz(t);
        }
        const float rmsA = renderSubBandRms(engine, kRateA, kSeconds);

        engine.prepare(kRateB, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});
        pushFullConfiguration(engine);
        std::array<float, SubharmonicEngine::kNumTones> freqB{};
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            freqB[t] = engine.getToneFrequencyHz(t);
        }
        const float rmsB = renderSubBandRms(engine, kRateB, kSeconds);

        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t << ": " << freqA[t] << " Hz at 44.1 kHz, " << freqB[t]
                         << " Hz at 96 kHz");
            REQUIRE(std::fabs(freqA[t] - freqB[t]) <= 1.0e-3f);
        }

        // The sub-band level. The guard is an IMPLICATION rather than a bare
        // comparison so the arm cannot divide by a silent render: it becomes
        // live the moment T010 lands the render path, and until then both sides
        // are silence and there is nothing to compare.
        constexpr float kSilenceFloor = 1.0e-6f;  // -120 dBFS
        INFO("sub-band RMS " << rmsA << " at 44.1 kHz vs " << rmsB << " at 96 kHz");
        if (rmsA > kSilenceFloor || rmsB > kSilenceFloor) {
            REQUIRE(rmsA > kSilenceFloor);
            REQUIRE(rmsB > kSilenceFloor);
            const float deltaDb = 20.0f * std::log10(rmsA / rmsB);
            INFO("delta " << deltaDb << " dB");
            REQUIRE(std::fabs(deltaDb) <= 1.0f);
        }
    }

    SECTION("SC-019 (a): the rate-derived ceilings move with the rate") {
        SubharmonicEngine engine;

        for (const double fs : {8000.0, 48000.0, 96000.0}) {
            engine.prepare(fs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

            engine.setFundamentalHz(1.0e6f);
            engine.setLowpassCutoffHz(1.0e5f);

            INFO("fs " << fs << ": fundamental ceiling " << engine.getFundamentalHz()
                       << " Hz, low-pass ceiling " << engine.getLowpassCutoffHz() << " Hz");
            REQUIRE(std::fabs(engine.getFundamentalHz() - expectedMaxFundamentalHz(fs)) <= 1.0e-3f);
            // kLowpassNyquistRatio * 8000 = 3600 > kMaxLowpassHz, so the FR-040
            // ceiling is INERT at every accepted rate (the S1.2 static_assert
            // states exactly this). The assertion is still written against the
            // rate-derived formula rather than the constant, so a future floor
            // change cannot make it stale.
            REQUIRE(std::fabs(engine.getLowpassCutoffHz() - expectedMaxLowpassHz(fs)) <= 1.0e-3f);
        }

        // The ceiling at 8 kHz is genuinely lower than at 48 kHz - without this
        // the arm above would pass on an implementation that ignored the rate.
        REQUIRE(expectedMaxFundamentalHz(8000.0) < expectedMaxFundamentalHz(48000.0));
        REQUIRE(std::fabs(expectedMaxFundamentalHz(8000.0) - 2400.0f) <= 1.0e-3f);
    }

    SECTION("SC-019 (b): prepare() twice lands where one prepare() on a fresh object does") {
        constexpr double kFs = 48000.0;
        const SubharmonicEngine::PrepareConfig cfg{.maxBlockSamples = 512};

        SubharmonicEngine fresh;
        // THE SEED IS NOT A DEFAULT prepare() RESTORES. FR-004 step (6) names
        // FR-013, FR-020, FR-021, FR-030, FR-035, FR-040, FR-041, FR-050 and
        // FR-064 - the FR-070 seed is deliberately absent, and plan S2 step (12)
        // re-distributes the EXISTING seed rather than the default one, because
        // FR-070's determinism contract would otherwise not survive a host
        // sample-rate change. The reference instance therefore carries the same
        // seed, so the getter-by-getter comparison below can stay exhaustive.
        fresh.setSeed(kFullConfigurationSeed);
        fresh.prepare(kFs, cfg);
        const ReadSurface reference = captureReadSurface(fresh);

        SubharmonicEngine configured;
        configured.prepare(kFs, cfg);
        pushFullConfiguration(configured);
        // Everything pushed above is configuration; prepare() step (9) runs
        // applyDefaults(), so the second prepare must erase all of it.
        configured.prepare(kFs, cfg);
        const ReadSurface after = captureReadSurface(configured);

        // The seed survives, asserted directly rather than only through the
        // aggregate below.
        REQUIRE(after.seed == kFullConfigurationSeed);
        requireSameReadSurface(after, reference);
    }

    SECTION("SC-019 (c): the FR-006 floor, and both clamp pairs at it") {
        SubharmonicEngine engine;
        engine.prepare(4000.0, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        INFO("requested 4000 Hz, applied " << engine.getSampleRate());
        REQUIRE(std::fabs(engine.getSampleRate() - SubharmonicEngine::kMinUsableSampleRate)
                <= 1.0e-9);
        REQUIRE(std::fabs(engine.getSampleRate() - 8000.0) <= 1.0e-9);

        // FR-013 at the floor: [8, min(4186, 2400)] = [8, 2400].
        engine.setFundamentalHz(SubharmonicEngine::kMinFundamentalHz);
        REQUIRE(std::fabs(engine.getFundamentalHz() - SubharmonicEngine::kMinFundamentalHz)
                <= kTol);
        engine.setFundamentalHz(1.0e6f);
        REQUIRE(std::fabs(engine.getFundamentalHz() - expectedMaxFundamentalHz(8000.0))
                <= 1.0e-3f);

        // FR-040 at the floor: [40, min(2000, 3600)] = [40, 2000].
        engine.setLowpassCutoffHz(SubharmonicEngine::kMinLowpassHz);
        REQUIRE(std::fabs(engine.getLowpassCutoffHz() - SubharmonicEngine::kMinLowpassHz) <= kTol);
        engine.setLowpassCutoffHz(1.0e5f);
        REQUIRE(std::fabs(engine.getLowpassCutoffHz() - expectedMaxLowpassHz(8000.0)) <= 1.0e-3f);
    }
}

namespace {

/// The exactly-double-rounded 4/3 that plan S2.3 builds `masterFifth_` from.
/// Written here as its own constant so the assertion below compares against the
/// RATIO the construction promises, not against a decimal transcription of it.
constexpr double kFourThirds = 4.0 / 3.0;

/// 2 cents expressed as a relative deviation: 2^(2/1200) - 1 = 1.1553e-3.
/// SC-003 (b)'s tuning tolerance, stated at the arithmetic that produces it.
constexpr double kTwoCentsRelative = 1.2e-3;

/// FR-012's mapping, restated INDEPENDENTLY of the header: a test that reused
/// the engine's own arithmetic would agree with any future mistake inside it.
[[nodiscard]] double expectedToneHz(std::size_t tone, double fundamentalHz) {
    switch (tone) {
        case 0:  return fundamentalHz * 0.5;              // Div2       -> f/2
        case 1:  return fundamentalHz * 0.25;             // Div4       -> f/4
        case 2:  return fundamentalHz * 2.0 / 3.0;        // FifthBelow -> 2f/3
        default: return 0.0;
    }
}

/// The three floored flags, LATCHED.
///
/// Plan S8 defines `isToneInfrasonicFloored(t)` as "`gateSteady(t) == 0.0f` as
/// latched at the last control step", and the latch is written in exactly three
/// places: prepare() step (10), reset() step (5) and updateControl() step (3)
/// (plan S5.2). reset() is the only one of the three reachable without a
/// render, and FR-005 makes it configuration-preserving - it rewinds phase and
/// re-snaps the ramps but keeps the fundamental just written - so it is the
/// correct way to observe the backstop through the read surface alone. The
/// RENDERED arm of the same law is SC-020 (a)-(c) in T014.
[[nodiscard]] std::array<bool, SubharmonicEngine::kNumTones> latchedFloorFlags(
    SubharmonicEngine& engine, float fundamentalHz) {
    engine.setFundamentalHz(fundamentalHz);
    engine.reset();

    std::array<bool, SubharmonicEngine::kNumTones> flags{};
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        flags[t] = engine.isToneInfrasonicFloored(t);
    }
    return flags;
}

/// FR-014's read-surface arm: a `setFundamentalHz` write retunes the two master
/// increments and does NOTHING else. Every FR-061 getter except the fundamental
/// and the three derived tone frequencies must be unchanged across the write -
/// no ramp re-snapped, no breather re-seeded, no counter cleared, no gate
/// retargeted. A write that rebuilt either PhaseAccumulator (the shape that
/// would also zero its phase) cannot be distinguished from a pure retune by any
/// getter, so this is the strongest read-surface statement available; the audio
/// arm is SC-008 in T014.
///
/// THE EXACT `==` IS DELIBERATE and is not a float golden: both sides are the
/// SAME getter read twice from the same object in the same process, so this is
/// a within-render structural identity of exactly the kind tasks.md's
/// convention list admits (SC-012 (c), SC-014 (a), SC-022, the `== 0.0f`
/// dormancy predicate). No constant is pinned here, nothing crosses a
/// toolchain, and a tolerance would be strictly weaker: "unchanged" is the
/// claim, and any epsilon would pass a build that quietly re-snapped a ramp.
void requireOnlyPitchMoved(const ReadSurface& before, const ReadSurface& after) {
    REQUIRE(before.sampleRate == after.sampleRate);
    REQUIRE(before.maxBlockSamples == after.maxBlockSamples);
    REQUIRE(before.prepared == after.prepared);

    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        INFO("tone " << t);
        REQUIRE(before.toneLevelDb[t] == after.toneLevelDb[t]);
        REQUIRE(before.toneWaveform[t] == after.toneWaveform[t]);
        REQUIRE(before.toneBreathRate[t] == after.toneBreathRate[t]);
        REQUIRE(before.toneBreathDepth[t] == after.toneBreathDepth[t]);
        REQUIRE(before.toneBreathValue[t] == after.toneBreathValue[t]);
        REQUIRE(before.toneCurrentGain[t] == after.toneCurrentGain[t]);
        REQUIRE(before.toneDormant[t] == after.toneDormant[t]);
        REQUIRE(before.toneFloored[t] == after.toneFloored[t]);
    }

    REQUIRE(before.trackingAmount == after.trackingAmount);
    REQUIRE(before.trackedEnvelope == after.trackedEnvelope);
    REQUIRE(before.trackingGain == after.trackingGain);
    REQUIRE(before.trackReferenceDb == after.trackReferenceDb);
    REQUIRE(before.followerAttackMs == after.followerAttackMs);
    REQUIRE(before.followerReleaseMs == after.followerReleaseMs);
    REQUIRE(before.lowpassCutoffHz == after.lowpassCutoffHz);
    REQUIRE(before.driveDb == after.driveDb);
    REQUIRE(before.wetGainDb == after.wetGainDb);
    REQUIRE(before.subToMainEnabled == after.subToMainEnabled);
    REQUIRE(before.seed == after.seed);
    REQUIRE(before.clampEngagements == after.clampEngagements);
    REQUIRE(before.allocatedBytes == after.allocatedBytes);
}

}  // namespace

// ==============================================================================
// T009 - SubharmonicEngine_ToneMapping (FR-012, FR-013, FR-014, SC-020 (d))
// ==============================================================================
// Four contracts that all hang off setFundamentalHz(), the ONE owner of both
// master increments (plan S2.3):
//
//  * FR-012's frequencies, read back through getToneFrequencyHz(), which is
//    derived FROM the increments rather than recomputed from fundamentalHz_ -
//    so this case measures the same numbers the render will use;
//  * the fifth is EXACT. `masterFifth_.increment = masterUnison_.increment *
//    (4.0 / 3.0)` in DOUBLE, never setFrequency(4.0f/3.0f * f, fs). The
//    rejected form rounds 4f/3 to float before the divide and leaves the two
//    increments' ratio only float-accurate (~1e-7 relative), which shows up as
//    a slow relative phase creep over long horizons - invisible to a 2-cent
//    tuning check, which is why the DOUBLE-precision ratio is asserted directly
//    at 1e-6 as well;
//  * FR-013's ceiling is min(4186, 0.3 * fs) - rate-derived, so it is 2400 Hz
//    at the FR-006 floor of 8 kHz, where the 4f/3 master's increment is exactly
//    0.4 and stays inside PhaseAccumulator::advance's single-subtraction wrap
//    contract (phase_utils.h:161-168);
//  * SC-020 (d): the FR-016 backstop thresholds, probed one Hz either side of
//    each boundary through isToneInfrasonicFloored, plus the precondition every
//    "all three tones awake" fixture rests on (SC-013, SC-017, SC-021) - that
//    at the FR-013 default of 55 Hz none of the three is floored.
//
// FR-014's phase continuity gets its read-surface arm here (a write moves the
// pitch and nothing else); its audio arm is SC-008 in T014.
// ==============================================================================
TEST_CASE("SubharmonicEngine_ToneMapping", "[subharmonic_engine]") {
    constexpr double kFs = 48000.0;
    constexpr std::array<float, 5> kFundamentals{20.0f, 55.0f, 110.0f, 220.0f, 440.0f};

    SECTION("FR-012: the three tones are f/2, f/4 and 2f/3") {
        SubharmonicEngine engine;
        engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        for (const float f : kFundamentals) {
            engine.setFundamentalHz(f);

            // None of the five is clamped: all sit inside [8, min(4186, 14400)].
            INFO("fundamental " << f << " Hz, applied " << engine.getFundamentalHz());
            REQUIRE(std::fabs(engine.getFundamentalHz() - f) <= 1.0e-3f);

            for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
                const double expected = expectedToneHz(t, static_cast<double>(f));
                const auto measured = static_cast<double>(engine.getToneFrequencyHz(t));
                INFO("f = " << f << " Hz, tone " << t << ": expected " << expected
                            << " Hz, read " << measured << " Hz");
                REQUIRE(std::fabs(measured - expected) <= 1.0e-3);
            }
        }
    }

    SECTION("FR-012: the fifth is exact, built as 4f/3 in double") {
        SubharmonicEngine engine;
        engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        for (const float f : kFundamentals) {
            engine.setFundamentalHz(f);

            const auto div2 = static_cast<double>(engine.getToneFrequencyHz(0));
            const auto fifth = static_cast<double>(engine.getToneFrequencyHz(2));
            REQUIRE(div2 > 0.0);

            const double ratio = fifth / div2;
            const double relative = std::fabs(ratio - kFourThirds) / kFourThirds;

            // (1) SC-003 (b)'s tuning criterion: within 2 cents of a just fifth.
            INFO("f = " << f << " Hz: FifthBelow/Div2 = " << ratio
                        << ", relative deviation " << relative);
            REQUIRE(relative <= kTwoCentsRelative);

            // (2) The CONSTRUCTION check. The rejected
            //     setFrequency(4.0f/3.0f * f, fs) form passes (1) comfortably
            //     and fails this one: rounding 4f/3 through float before the
            //     divide leaves ~1e-7 of relative error in the ratio, orders
            //     above what one double multiply leaves.
            REQUIRE(std::fabs(ratio - kFourThirds) <= 1.0e-6);
        }
    }

    SECTION("FR-013: the ceiling is min(4186, 0.3 * fs), rate-derived") {
        SubharmonicEngine engine;
        engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        engine.setFundamentalHz(1.0e6f);
        INFO("ceiling at 48 kHz: " << engine.getFundamentalHz());
        // 0.3 * 48000 = 14400, so the absolute C8 ceiling binds here.
        REQUIRE(std::fabs(engine.getFundamentalHz() - expectedMaxFundamentalHz(kFs)) <= 1.0e-3f);
        REQUIRE(std::fabs(engine.getFundamentalHz() - SubharmonicEngine::kMaxFundamentalHz)
                <= 1.0e-3f);

        // At the FR-006 floor the Nyquist ratio binds instead: 0.3 * 8000.
        engine.prepare(8000.0, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});
        engine.setFundamentalHz(1.0e6f);
        INFO("ceiling at 8 kHz: " << engine.getFundamentalHz());
        REQUIRE(std::fabs(engine.getFundamentalHz() - 2400.0f) <= 1.0e-3f);
        REQUIRE(std::fabs(engine.getFundamentalHz() - expectedMaxFundamentalHz(8000.0))
                <= 1.0e-3f);

        // The 4f/3 master at that ceiling is 3200 Hz, i.e. an increment of
        // exactly 0.4 - inside the accumulator's single-subtraction wrap
        // contract, which is the whole reason kMasterNyquistRatio is 0.3.
        const double fifthMasterHz = 2.0 * static_cast<double>(engine.getToneFrequencyHz(2));
        INFO("4f/3 master at the 8 kHz ceiling: " << fifthMasterHz << " Hz");
        REQUIRE(std::fabs(fifthMasterHz - 3200.0) <= 1.0e-2);
        REQUIRE(fifthMasterHz / 8000.0 < 1.0);
    }

    SECTION("FR-014: a write retunes the pitch and touches nothing else") {
        SubharmonicEngine engine;
        engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        const ReadSurface before = captureReadSurface(engine);
        engine.setFundamentalHz(110.0f);
        const ReadSurface after = captureReadSurface(engine);

        // The pitch DID move (otherwise the comparison below is vacuous).
        REQUIRE(std::fabs(after.fundamentalHz - 110.0f) <= 1.0e-3f);
        REQUIRE(std::fabs(after.toneFrequencyHz[0] - before.toneFrequencyHz[0]) > 1.0f);
        requireOnlyPitchMoved(before, after);

        // ... and it is a PURE function of its argument: a walk through four
        // other fundamentals lands on exactly the same three tone frequencies
        // as a single direct write. Any hysteresis - an increment accumulated
        // rather than assigned, or the 4/3 ratio applied to the previous fifth
        // instead of to the unison master - shows up here and nowhere else on
        // the read surface. The `==` is again a same-process structural
        // identity between two evaluations of one pure expression, not a
        // pinned constant.
        SubharmonicEngine direct;
        direct.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});
        direct.setFundamentalHz(110.0f);

        for (const float f : {20.0f, 440.0f, 55.0f, 220.0f, 110.0f}) {
            engine.setFundamentalHz(f);
        }
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t << ": walked " << engine.getToneFrequencyHz(t) << " Hz, direct "
                         << direct.getToneFrequencyHz(t) << " Hz");
            REQUIRE(engine.getToneFrequencyHz(t) == direct.getToneFrequencyHz(t));
        }
    }

    SECTION("SC-020 (d): the FR-016 backstop boundaries, one Hz either side") {
        SubharmonicEngine engine;
        engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        // Div4 (f/4) crosses kMinToneHz = 12 at f = 48. Div2 (f/2) is 23.5 /
        // 24.5 Hz here and FifthBelow (2f/3) 31.3 / 32.7 Hz - both awake on
        // BOTH sides, which is what "on the named tone only" means.
        const auto below48 = latchedFloorFlags(engine, 47.0f);
        const auto above48 = latchedFloorFlags(engine, 49.0f);
        INFO("f = 47 Hz -> Div4 floored " << below48[1] << ", f = 49 Hz -> Div4 floored "
                                          << above48[1]);
        REQUIRE(below48[1]);
        REQUIRE_FALSE(above48[1]);
        REQUIRE(below48[0] == above48[0]);
        REQUIRE(below48[2] == above48[2]);
        REQUIRE_FALSE(below48[0]);
        REQUIRE_FALSE(below48[2]);

        // Div2 (f/2) crosses at f = 24. Div4 is 5.75 / 6.25 Hz - floored on
        // both sides, so it too is "unaffected" by this boundary.
        const auto below24 = latchedFloorFlags(engine, 23.0f);
        const auto above24 = latchedFloorFlags(engine, 25.0f);
        INFO("f = 23 Hz -> Div2 floored " << below24[0] << ", f = 25 Hz -> Div2 floored "
                                          << above24[0]);
        REQUIRE(below24[0]);
        REQUIRE_FALSE(above24[0]);
        REQUIRE(below24[1] == above24[1]);
        REQUIRE(below24[2] == above24[2]);
        REQUIRE(below24[1]);
        REQUIRE_FALSE(below24[2]);

        // FifthBelow (2f/3) crosses at f = 18, the lowest of the three: Div2
        // (8.5 / 9.5 Hz) and Div4 (4.25 / 4.75 Hz) are floored on both sides.
        const auto below18 = latchedFloorFlags(engine, 17.0f);
        const auto above18 = latchedFloorFlags(engine, 19.0f);
        INFO("f = 17 Hz -> FifthBelow floored " << below18[2]
                                                << ", f = 19 Hz -> FifthBelow floored "
                                                << above18[2]);
        REQUIRE(below18[2]);
        REQUIRE_FALSE(above18[2]);
        REQUIRE(below18[0] == above18[0]);
        REQUIRE(below18[1] == above18[1]);
        REQUIRE(below18[0]);
        REQUIRE(below18[1]);

        // The FR-013 lower clamp end: 8 Hz is below all three thresholds, so
        // the engine is silent-but-correct (the spec's edge-case table, FR-013).
        const auto atFloor = latchedFloorFlags(engine, 0.0f);
        REQUIRE(std::fabs(engine.getFundamentalHz() - SubharmonicEngine::kMinFundamentalHz)
                <= 1.0e-5f);
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t << " at f = 8 Hz: " << engine.getToneFrequencyHz(t) << " Hz");
            REQUIRE(atFloor[t]);
        }
    }

    SECTION("FR-013 default: at f = 55 Hz none of the three is floored") {
        SubharmonicEngine engine;
        engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512});

        // prepare() step (9) pushes kDefaultFundamentalHz and step (10) latches
        // the backstop, so this reads the SHIPPED state with no setter call at
        // all - the precondition SC-013, SC-017 and SC-021 all rest on.
        REQUIRE(std::fabs(engine.getFundamentalHz() - SubharmonicEngine::kDefaultFundamentalHz)
                <= 1.0e-5f);

        constexpr std::array<double, 3> kExpected{27.5, 13.75, 36.666666};
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            const auto hz = static_cast<double>(engine.getToneFrequencyHz(t));
            INFO("tone " << t << ": " << hz << " Hz, kMinToneHz = "
                         << SubharmonicEngine::kMinToneHz);
            REQUIRE(std::fabs(hz - kExpected[t]) <= 1.0e-3);
            REQUIRE(hz > static_cast<double>(SubharmonicEngine::kMinToneHz));
            REQUIRE_FALSE(engine.isToneInfrasonicFloored(t));
        }

        // The tightest of the three, spelled out: Div4 clears the 12 Hz
        // backstop by 1.75 Hz and is attenuated by the 18 Hz DCBlocker2
        // instead of silenced - SC-020 (b)'s "attenuated, not hard-muted".
        REQUIRE(static_cast<double>(engine.getToneFrequencyHz(1))
                > static_cast<double>(SubharmonicEngine::kMinToneHz) + 1.5);
    }
}

// ==============================================================================
// T010 - SubharmonicEngine_BlockInvariance (SC-012)
// ==============================================================================
// The host owns the partition; the engine must not. Three arms plus the two
// extreme block sizes the spec's Edge Cases name (spec.md:1370-1373):
//
//  (a) a 30 s render in 512-sample blocks against the SAME 30 s in a seeded
//      pseudo-random partition of chunk sizes in [1, 1024]. This is the arm a
//      BLOCK-RELATIVE control grid fails: FR-007's residue is absolute and is
//      carried across calls, so a 36 + 28 split must run exactly the one
//      control step an unsplit 64 runs. It is also the arm that fails if
//      updateControl() advances the breathers by `chunk` instead of the
//      constant kControlChunkSamples - the breath rate would then be a
//      function of the host's block size.
//  (b) IN-PLACE rendering (outL == inL, outR == inR) against out-of-place.
//      renderChunk() reads BOTH inputs before it writes either output; an
//      implementation that wrote outL before reading inR would feed the
//      already-summed left sample into the mono sensor and diverge here.
//  (c) processBlockTapped's MAIN output against processBlock's, BIT-IDENTICAL.
//      Structural rather than maintained: S5.1 has one body and processBlock
//      forwards to it with subTap = nullptr (FR-063). An `==` comparison is
//      legitimate here because both sides are the same computation in the same
//      binary - it is a within-render structural identity, not a stored golden.
//
// Comparisons are render_fingerprint.h's measured tolerances
// (kSampleTolerance = 5.0e-4f, kMetricTolerance = 2.5e-4), never a bit-exact
// float golden (roadmap line 526, tools/lint-float-bit-goldens.js), except for
// arm (c) as explained above.
// ==============================================================================
TEST_CASE("SubharmonicEngine_BlockInvariance", "[subharmonic_engine]") {
    SECTION("SC-012 (a): a 30 s render is independent of the host's partition") {
        const auto total = static_cast<std::size_t>(30.0 * kDefaultsFixtureSampleRate);
        std::vector<float> body(total, 0.0f);
        fillBody(body);

        std::vector<float> referenceL(total, 0.0f);
        std::vector<float> referenceR(total, 0.0f);
        std::vector<float> partitionedL(total, 0.0f);
        std::vector<float> partitionedR(total, 0.0f);
        std::vector<float> referenceTap(total, 0.0f);
        std::vector<float> partitionedTap(total, 0.0f);

        SubharmonicEngine reference;
        SubharmonicEngine partitioned;
        makeDefaults(reference);
        makeDefaults(partitioned);

        renderPartitioned(reference, body, referenceL, referenceR, &referenceTap,
                          FixedPartition(512));
        renderPartitioned(partitioned, body, partitionedL, partitionedR, &partitionedTap,
                          RandomPartition(0x51A7C012u));

        // NON-VACUITY, measured on the TAP rather than on the summed output:
        // outL is dominated by the -12 dBFS dry body, so its RMS would clear any
        // floor even from a silent engine. The tap is the sub alone.
        const auto tapFp = Krate::DSP::TestUtils::fingerprintRender(referenceTap);
        INFO("reference sub tap RMS " << tapFp.rms << ", peak " << tapFp.peak);
        REQUIRE(tapFp.rms > 1.0e-3);

        std::string detail;
        const bool agree =
            rendersAgree(partitionedL, partitionedR, referenceL, referenceR, detail);
        INFO("random partition vs 512-sample blocks: " << detail);
        REQUIRE(agree);

        // The TAP is partition-invariant too - FR-062 writes it inside the same
        // chunk loop, so a grid that leaked the partition would show there first.
        std::string tapDetail;
        const bool tapsAgree =
            rendersAgree(partitionedTap, partitionedTap, referenceTap, referenceTap, tapDetail);
        INFO("sub tap, random partition vs 512-sample blocks: " << tapDetail);
        REQUIRE(tapsAgree);
    }

    SECTION("SC-012 (b): in-place rendering agrees with out-of-place") {
        const auto total = static_cast<std::size_t>(5.0 * kDefaultsFixtureSampleRate);
        std::vector<float> body(total, 0.0f);
        fillBody(body);

        std::vector<float> referenceL(total, 0.0f);
        std::vector<float> referenceR(total, 0.0f);
        SubharmonicEngine reference;
        makeDefaults(reference);
        renderPartitioned(reference, body, referenceL, referenceR, nullptr,
                          FixedPartition(512));

        // In place: the output buffers ARE the input buffers.
        std::vector<float> inPlaceL = body;
        std::vector<float> inPlaceR = body;
        SubharmonicEngine inPlace;
        makeDefaults(inPlace);
        for (std::size_t done = 0; done < total; done += std::size_t{512}) {
            const std::size_t n = std::min(std::size_t{512}, total - done);
            inPlace.processBlockTapped(inPlaceL.data() + done, inPlaceR.data() + done,
                                       inPlaceL.data() + done, inPlaceR.data() + done, nullptr,
                                       n);
        }

        std::string detail;
        const bool agree = rendersAgree(inPlaceL, inPlaceR, referenceL, referenceR, detail);
        INFO("in-place vs out-of-place: " << detail);
        REQUIRE(agree);
    }

    SECTION("SC-012 (c): the tapped entry point's main output is bit-identical") {
        const auto total = static_cast<std::size_t>(5.0 * kDefaultsFixtureSampleRate);
        std::vector<float> body(total, 0.0f);
        fillBody(body);

        std::vector<float> untappedL(total, 0.0f);
        std::vector<float> untappedR(total, 0.0f);
        std::vector<float> tappedL(total, 0.0f);
        std::vector<float> tappedR(total, 0.0f);
        std::vector<float> tap(total, 0.0f);

        SubharmonicEngine untapped;
        SubharmonicEngine tapped;
        makeDefaults(untapped);
        makeDefaults(tapped);

        for (std::size_t done = 0; done < total; done += std::size_t{512}) {
            const std::size_t n = std::min(std::size_t{512}, total - done);
            untapped.processBlock(body.data() + done, body.data() + done,
                                  untappedL.data() + done, untappedR.data() + done, n);
            tapped.processBlockTapped(body.data() + done, body.data() + done,
                                      tappedL.data() + done, tappedR.data() + done,
                                      tap.data() + done, n);
        }

        // FR-063: EXACT, because S5.1 has one body and the tap is a write, never
        // a change of arithmetic.
        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < total; ++i) {
            if (tappedL[i] != untappedL[i] || tappedR[i] != untappedR[i]) {
                ++mismatches;
            }
        }
        INFO("bit-identical mismatches: " << mismatches << " of " << total);
        REQUIRE(mismatches == std::size_t{0});

        // Non-vacuity: the tap carried a real signal, so "identical" is not the
        // identity of two silent renders.
        const auto tapFp = Krate::DSP::TestUtils::fingerprintRender(tap);
        INFO("tap RMS " << tapFp.rms);
        REQUIRE(tapFp.rms > 1.0e-3);
    }

    SECTION("SC-012: numSamples = 1 for 100 000 calls agrees with the reference partition") {
        constexpr std::size_t kTotal = 100000;
        std::vector<float> body(kTotal, 0.0f);
        fillBody(body);

        std::vector<float> referenceL(kTotal, 0.0f);
        std::vector<float> referenceR(kTotal, 0.0f);
        std::vector<float> singleL(kTotal, 0.0f);
        std::vector<float> singleR(kTotal, 0.0f);

        SubharmonicEngine reference;
        SubharmonicEngine single;
        makeDefaults(reference);
        makeDefaults(single);

        renderPartitioned(reference, body, referenceL, referenceR, nullptr,
                          FixedPartition(512));
        renderPartitioned(single, body, singleL, singleR, nullptr, FixedPartition(1));

        std::string detail;
        const bool agree = rendersAgree(singleL, singleR, referenceL, referenceR, detail);
        INFO("100 000 single-sample calls: " << detail);
        REQUIRE(agree);
    }

    SECTION("SC-012: a single 8192-sample call agrees with the reference partition") {
        constexpr std::size_t kTotal = 8192;  // the PrepareConfig ceiling, in ONE call
        std::vector<float> body(kTotal, 0.0f);
        fillBody(body);

        std::vector<float> referenceL(kTotal, 0.0f);
        std::vector<float> referenceR(kTotal, 0.0f);
        std::vector<float> oneShotL(kTotal, 0.0f);
        std::vector<float> oneShotR(kTotal, 0.0f);

        SubharmonicEngine reference;
        SubharmonicEngine oneShot;
        makeDefaults(reference, kDefaultsFixtureSampleRate, std::size_t{8192});
        makeDefaults(oneShot, kDefaultsFixtureSampleRate, std::size_t{8192});
        REQUIRE(reference.getMaxBlockSamples() == std::size_t{8192});

        renderPartitioned(reference, body, referenceL, referenceR, nullptr,
                          FixedPartition(512));
        oneShot.processBlockTapped(body.data(), body.data(), oneShotL.data(), oneShotR.data(),
                                   nullptr, kTotal);

        std::string detail;
        const bool agree = rendersAgree(oneShotL, oneShotR, referenceL, referenceR, detail);
        INFO("one 8192-sample call: " << detail);
        REQUIRE(agree);
    }
}

// ==============================================================================
// T011 shared helpers - the control-grid-aligned drivers SC-014 is built on
// ==============================================================================
namespace {

/// Every SC-014 arm renders in blocks of exactly the control chunk, so "after N
/// samples" and "after N/64 control steps" are the same statement and the
/// <= 64-sample wake latency of arm (b) is measurable at all.
constexpr std::size_t kDormancyBlock = SubharmonicEngine::kControlChunkSamples;

/// Advance `engine` by `total` samples of a 55 Hz sine of PEAK amplitude
/// `amplitude` (pass 0.0 for silence), in control-grid-aligned blocks,
/// discarding the output. Returns the new absolute sample index.
///
/// The waveform is generated from the ABSOLUTE index, so a sequence of calls is
/// ONE continuous body rather than a train of restarted sines - a phase
/// discontinuity at a stage boundary would charge the follower and the chain
/// with a transient the criterion never asked for.
std::size_t advanceOne(SubharmonicEngine& engine, std::size_t index, std::size_t total,
                       double amplitude) {
    std::vector<float> in(kDormancyBlock, 0.0f);
    std::vector<float> outL(kDormancyBlock, 0.0f);
    std::vector<float> outR(kDormancyBlock, 0.0f);
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / kDefaultsFixtureSampleRate;

    for (std::size_t done = 0; done < total; done += kDormancyBlock) {
        const std::size_t n = std::min(kDormancyBlock, total - done);
        for (std::size_t i = 0; i < n; ++i) {
            in[i] = static_cast<float>(amplitude *
                                       std::sin(omega * static_cast<double>(index + i)));
        }
        engine.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
        index += n;
    }
    return index;
}

/// As advanceOne(), driving TWO engines with the SAME input buffer, so any
/// divergence between them is the engines' own state and never the fixture.
/// When `traceA`/`traceB` are non-null, getTrackedEnvelope() is appended to each
/// after every control block - the once-per-64-samples sampling SC-014 (c3)
/// specifies.
std::size_t advanceBoth(SubharmonicEngine& a, SubharmonicEngine& b, std::size_t index,
                        std::size_t total, double amplitude,
                        std::vector<float>* traceA = nullptr,
                        std::vector<float>* traceB = nullptr) {
    std::vector<float> in(kDormancyBlock, 0.0f);
    std::vector<float> outL(kDormancyBlock, 0.0f);
    std::vector<float> outR(kDormancyBlock, 0.0f);
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / kDefaultsFixtureSampleRate;

    for (std::size_t done = 0; done < total; done += kDormancyBlock) {
        const std::size_t n = std::min(kDormancyBlock, total - done);
        for (std::size_t i = 0; i < n; ++i) {
            in[i] = static_cast<float>(amplitude *
                                       std::sin(omega * static_cast<double>(index + i)));
        }
        a.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
        b.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
        index += n;
        if (traceA != nullptr) {
            traceA->push_back(a.getTrackedEnvelope());
        }
        if (traceB != nullptr) {
            traceB->push_back(b.getTrackedEnvelope());
        }
    }
    return index;
}

/// Render silence through `engine`, capturing both channels into pre-sized
/// buffers.
void captureSilentRender(SubharmonicEngine& engine, std::vector<float>& outL,
                         std::vector<float>& outR) {
    const std::size_t total = outL.size();
    const std::vector<float> silence(kDefaultsFixtureBlock, 0.0f);
    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
        engine.processBlock(silence.data(), silence.data(), outL.data() + done,
                            outR.data() + done, n);
    }
}

/// Drive every tone to the FR-020 fader bottom.
void sleepAllTones(SubharmonicEngine& engine) {
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        engine.setToneLevelDb(t, SubharmonicEngine::kMinToneLevelDb);
    }
}

/// Restore every tone to its FR-020 default.
void wakeAllTones(SubharmonicEngine& engine) {
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        engine.setToneLevelDb(t, SubharmonicEngine::kDefaultToneLevelDb[t]);
    }
}

[[nodiscard]] float bufferRms(const std::vector<float>& v) {
    if (v.empty()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (const float s : v) {
        sum += static_cast<double>(s) * static_cast<double>(s);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(v.size())));
}

/// SC-014 (c3)'s absolute tolerance, and where the number comes from.
///
/// EnvelopeFollower in RMS mode is an ASYMMETRIC one-pole in the SQUARED domain
/// (envelope_follower.h:312-326) whose coefficient is exp(-2*pi/timeSamples)
/// (:359-365). A steady 55 Hz sine presents a 110 Hz ripple in the squared
/// domain, and at the FR-031 constants (120 ms attack / 800 ms release) that
/// ripple survives into getTrackedEnvelope() with a measured peak-to-peak of
/// 3.25e-3, i.e. a worst |value - preDormancyValue| of 1.64e-3 - above a
/// 1e-3 absolute band on a CORRECT implementation, with no reset anywhere. Measured
/// this session by simulating the shipped recursion at 48 kHz against a
/// -30 dBFS peak 55 Hz body, sampled once per 64 samples over 500 ms after a
/// 2 s settle.
///
/// 5.0e-3 is ~3x the measured spread - the same "3x the measured spread"
/// convention render_fingerprint.h:27-28 uses - and is still ~44x below the
/// ~0.22 excursion a follower reset at the sleep edge would produce, so the arm
/// keeps its teeth. The DIFFERENTIAL assertion beside it preserves the spec's
/// 1e-3 exactly and is strictly stronger: the two instances see byte-identical
/// input, so their followers run in lockstep and the only thing that can
/// separate them is a reset in the dormant one.
constexpr float kSensorAbsoluteTolerance = 5.0e-3f;

/// The spec's own number, kept on the differential arm.
constexpr float kSensorDifferentialTolerance = 1.0e-3f;

/// -30 dBFS PEAK, the SC-014 (c3) body: below the -18 dBFS FR-035 reference, so
/// envNorm lands near 0.22 and is UNCLAMPED. At -12 dBFS the clamp at 1.0 would
/// hide a reset within ~35 ms and the arm would be vacuous.
constexpr double kSensorBodyAmplitude = 0.0316227766016838;

}  // namespace

// ==============================================================================
// SC-014 - Dormancy behaves as FR-025/FR-026 specify (tasks.md T011)
// ==============================================================================
// (a) Three dormant tones make the component a bit-identical passthrough, and so
//     does a muted wet gain with the tones AWAKE - two different mechanisms
//     reaching the same output (spec.md:1342-1348).
// (b) isToneDormant() flips on the WRITE (it is a pure predicate over the level
//     ramp) and the audio wakes within the <= 64-sample control latency Q5
//     grants.
// (c) The FR-026 sleep-edge clear, with the >= 2 s charging render that makes it
//     observable.
// (c3) The follower is NOT reset at that edge - FR-026's last sentence, and the
//     natural mistake, since the other three chain stages are.
// (d) Every generator keeps advancing through 37 s of dormancy (FR-025's stated
//     deviation), measured differentially against an instance that never slept.
//
// Two fixture facts an earlier spec draft had differently; spec.md SC-014 (c)
// and (c3) now state them, and the measurements behind them are these:
//
//  1. ARM (c) USES trackingAmount = 1.0 (the FR-033 DEFAULT), not 0. An earlier
//     draft required trackingAmount = 0 "so a stale tail would be at full
//     level", and asserts the output is <= -80 dBFS over the 500 ms after
//     the wake. Those two cannot both hold: step (3) RESTORES all three tone
//     levels, and the tones are GENERATORS, not input-driven (the header's own
//     opening note). At the FR-020 defaults with trackGain pinned to 1.0 by
//     trackingAmount = 0, the restored sub is ~0.22 linear, about -13 dBFS -
//     67 dB ABOVE the threshold - so (c) fails on a CORRECT implementation.
//     The earlier draft's reason for choosing 0 is also inverted by the shipped
//     chain order: renderChunk() applies `y *= tg` BEFORE `blocker_.process(y)`,
//     so at trackingAmount = 1.0 with a silent input (tg == 0 exactly) the
//     restored sub is muted, while the DC blocker's frozen state sits
//     DOWNSTREAM of that mute and still rings. Simulating the shipped
//     DCBlocker2 recursion at 18 Hz / 48 kHz through this exact sequence gives a
//     wake-window peak of 1.49e-2 (-36.6 dBFS) when the sleep-edge resets are
//     absent, against exactly 0.0 when they are present. So at
//     trackingAmount = 1.0 the criterion is BOTH satisfiable and 43 dB clear of
//     its own threshold, and the (c2) two-line mutation is what detects it.
//  2. ARM (c3) holds 1e-3 on a DIFFERENTIAL assertion and a measured 5.0e-3 on
//     the absolute one - see kSensorAbsoluteTolerance above for the measurement
//     and the arithmetic.
//
// (c2) MUTATION CHECK - NOT RUN BY THIS AGENT. tasks.md T011 requires the
//      two-line mutation (remove `lowpass_.reset()` and `blocker_.reset()` from
//      updateControl() step (6), leaving `saturator_.reset()`) to be run once
//      and its result recorded. This agent is instructed not to build or run
//      anything, so the run belongs to the build agent. The PREDICTION, from the
//      simulation above, is a wake-window peak of 1.49e-2 (-36.6 dBFS) against
//      the 1.0e-4 threshold - i.e. (c) fails, as SC-014 (c2) requires.
// ==============================================================================
TEST_CASE("SubharmonicEngine_Dormancy", "[subharmonic_engine]") {
    SECTION("SC-014 (a): three dormant tones are a bit-identical passthrough") {
        SubharmonicEngine engine;
        makeDefaults(engine);
        sleepAllTones(engine);

        // 200 ms is four times the 50 ms level ramp, so every ramp is PARKED on
        // the literal 0.0f and the control step that latches the sleep edge has
        // run.
        static_cast<void>(advanceOne(
            engine, 0, static_cast<std::size_t>(0.2 * kDefaultsFixtureSampleRate), 0.0));
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE(engine.isToneDormant(t));
        }

        const auto total = static_cast<std::size_t>(10.0 * kDefaultsFixtureSampleRate);
        std::vector<float> inL(total, 0.0f);
        std::vector<float> inR(total, 0.0f);
        fillNoise(inL, 0x0D0E0A11u);
        fillNoise(inR, 0x0D0E0A22u);

        // NON-VACUITY: a decorrelated, full-amplitude stereo input. An identity
        // over two silent buffers would be no assertion at all.
        REQUIRE(bufferRms(inL) > 0.4f);
        REQUIRE(bufferRms(inR) > 0.4f);
        REQUIRE(std::fabs(lfm::calculateCorrelation(inL.data(), inR.data(), total)) < 0.1f);

        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);
        // Sentinel: a tap the skip never writes would leave 1.0f behind.
        std::vector<float> tap(total, 1.0f);
        for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
            const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
            engine.processBlockTapped(inL.data() + done, inR.data() + done, outL.data() + done,
                                      outR.data() + done, tap.data() + done, n);
        }

        std::size_t mismatches = 0;
        std::size_t nonZeroTap = 0;
        for (std::size_t i = 0; i < total; ++i) {
            if (outL[i] != inL[i] || outR[i] != inR[i]) {
                ++mismatches;
            }
            if (tap[i] != 0.0f) {
                ++nonZeroTap;
            }
        }
        INFO("mismatches " << mismatches << " of " << total << ", non-zero tap " << nonZeroTap);
        REQUIRE(mismatches == std::size_t{0});
        REQUIRE(nonZeroTap == std::size_t{0});

        // Still dormant at the end, and the FR-054 counter never moved: the
        // clamp cannot engage on a path that never runs.
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            REQUIRE(engine.isToneDormant(t));
        }
        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
    }

    SECTION("SC-014 (a): a muted wet gain is bit-identical too, chain still running") {
        SubharmonicEngine engine;
        makeDefaults(engine);
        engine.setWetGainDb(SubharmonicEngine::kMinWetGainDb);

        static_cast<void>(advanceOne(
            engine, 0, static_cast<std::size_t>(0.2 * kDefaultsFixtureSampleRate), 0.0));

        // The tones are AWAKE - this arm reaches the same bit-identical output
        // through FR-051's exact-zero wet gain, not through FR-025's skip
        // (spec.md:1344-1348).
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE_FALSE(engine.isToneDormant(t));
        }

        const auto total = static_cast<std::size_t>(2.0 * kDefaultsFixtureSampleRate);
        std::vector<float> inL(total, 0.0f);
        std::vector<float> inR(total, 0.0f);
        fillNoise(inL, 0x0D0E0B11u);
        fillNoise(inR, 0x0D0E0B22u);

        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);
        std::vector<float> tap(total, 0.0f);
        for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
            const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
            engine.processBlockTapped(inL.data() + done, inR.data() + done, outL.data() + done,
                                      outR.data() + done, tap.data() + done, n);
        }

        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < total; ++i) {
            if (outL[i] != inL[i] || outR[i] != inR[i]) {
                ++mismatches;
            }
        }
        INFO("mismatches " << mismatches << " of " << total);
        REQUIRE(mismatches == std::size_t{0});

        // THE CHAIN IS STILL RUNNING: the FR-062 tap is PRE-wet-gain, so it
        // carries the full sub even while the wet fader sits at its exact-zero
        // bottom. That is what separates this arm from the dormant one above,
        // where the tap is identically zero.
        const auto tapFp = Krate::DSP::TestUtils::fingerprintRender(tap);
        INFO("tap RMS " << tapFp.rms);
        REQUIRE(tapFp.rms > 1.0e-3);
    }

    SECTION("SC-014 (b): the predicate flips on the write, the audio wakes within 64") {
        SubharmonicEngine engine;
        makeDefaults(engine);
        // trackingAmount = 0 pins trackGain to 1.0, so the wake is visible in
        // the FR-062 tap. At the FR-033 default of 1.0 with a silent input the
        // tap is POST-tracking and therefore zero whether the chain runs or not,
        // and the latency this arm exists to measure would be unobservable.
        engine.setTrackingAmount(0.0f);
        sleepAllTones(engine);

        // 9600 = 150 * 64: the FR-007 residue returns to zero, so the next
        // rendered sample is also the next control step.
        static_cast<void>(advanceOne(engine, 0, std::size_t{9600}, 0.0));
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE(engine.isToneDormant(t));
        }

        const std::vector<float> silence(kDormancyBlock, 0.0f);
        std::vector<float> outL(kDormancyBlock, 0.0f);
        std::vector<float> outR(kDormancyBlock, 0.0f);
        std::vector<float> tapBefore(kDormancyBlock, 1.0f);
        engine.processBlockTapped(silence.data(), silence.data(), outL.data(), outR.data(),
                                  tapBefore.data(), kDormancyBlock);
        std::size_t nonZeroBefore = 0;
        for (const float v : tapBefore) {
            if (v != 0.0f) {
                ++nonZeroBefore;
            }
        }
        REQUIRE(nonZeroBefore == std::size_t{0});

        // THE WRITE. isToneDormant() is a pure predicate over the level ramp and
        // LinearRamp::setTarget() writes target_ immediately (smoother.h:350),
        // so it flips on the write itself - no render required.
        engine.setToneLevelDb(0, SubharmonicEngine::kDefaultToneLevelDb[0]);
        REQUIRE_FALSE(engine.isToneDormant(0));
        REQUIRE(engine.isToneDormant(1));
        REQUIRE(engine.isToneDormant(2));

        // THE AUDIO WAKE, inside the <= 64-sample control latency Q5 grants.
        std::vector<float> tapAfter(kDormancyBlock, 0.0f);
        engine.processBlockTapped(silence.data(), silence.data(), outL.data(), outR.data(),
                                  tapAfter.data(), kDormancyBlock);
        std::size_t nonZeroAfter = 0;
        for (const float v : tapAfter) {
            if (v != 0.0f) {
                ++nonZeroAfter;
            }
        }
        INFO("non-zero tap samples in the 64 after the write: " << nonZeroAfter);
        REQUIRE(nonZeroAfter > std::size_t{0});

        // Waking ONE tone is enough: FR-025's condition is engine-wide.
        REQUIRE(engine.isToneDormant(1));
        REQUIRE(engine.isToneDormant(2));
    }

    SECTION("SC-014 (c): the sleep edge clears the chain, so the wake is silent") {
        SubharmonicEngine engine;
        makeDefaults(engine);

        // (1) CHARGE. The FR-033 default tracking of 1.0 - see the deviation
        //     note above for why this is 1.0 and not the spec's 0 - with a
        //     -12 dBFS 55 Hz body for 2.5 s, past both the >= 2 s the criterion
        //     requires and the 800 ms follower release.
        REQUIRE(engine.getTrackingAmount() == SubharmonicEngine::kDefaultTrackingAmount);
        REQUIRE(engine.getWetGainDb() == SubharmonicEngine::kDefaultWetGainDb);
        REQUIRE(engine.getFundamentalHz() == SubharmonicEngine::kDefaultFundamentalHz);

        std::size_t index =
            advanceOne(engine, 0, static_cast<std::size_t>(2.5 * kDefaultsFixtureSampleRate),
                       kBodyAmplitude);
        INFO("charged envelope " << engine.getTrackedEnvelope());
        REQUIRE(engine.getTrackedEnvelope() > 0.5f);

        // (2) SLEEP. Levels to the fader bottom and the body removed, held 10 s.
        sleepAllTones(engine);
        index = advanceOne(engine, index,
                           static_cast<std::size_t>(10.0 * kDefaultsFixtureSampleRate), 0.0);
        static_cast<void>(index);
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE(engine.isToneDormant(t));
        }
        // The sensor has released to zero, which is what leaves the frozen
        // DC-blocker tail as the ONLY thing the wake window can contain.
        REQUIRE(engine.getTrackedEnvelope() < 1.0e-6f);

        // (3) WAKE into a silent input, all three levels restored.
        wakeAllTones(engine);
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            REQUIRE_FALSE(engine.isToneDormant(t));
        }

        // (4) The first 500 ms after the wake.
        const auto window = static_cast<std::size_t>(0.5 * kDefaultsFixtureSampleRate);
        std::vector<float> outL(window, 0.0f);
        std::vector<float> outR(window, 0.0f);
        captureSilentRender(engine, outL, outR);

        const float peak = samplePeak(outL, outR);
        const float threshold = Krate::DSP::dbToGain(-80.0f);
        INFO("wake-window peak " << peak << " (threshold " << threshold
                                 << "); the two-line (c2) mutation predicts ~1.49e-2");
        REQUIRE(peak <= threshold);
        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
    }

    SECTION("SC-014 (c3): the follower is NOT reset at the sleep edge") {
        SubharmonicEngine slept;
        SubharmonicEngine awake;
        makeDefaults(slept);
        makeDefaults(awake);

        // 2 s of the -30 dBFS body settles the 120 ms / 800 ms follower.
        std::size_t index =
            advanceBoth(slept, awake, 0,
                        static_cast<std::size_t>(2.0 * kDefaultsFixtureSampleRate),
                        kSensorBodyAmplitude);

        const float preDormancy = slept.getTrackedEnvelope();
        INFO("pre-dormancy envNorm " << preDormancy);
        // UNCLAMPED and far from zero, or the arm cannot see a reset.
        REQUIRE(preDormancy > 0.15f);
        REQUIRE(preDormancy < 0.90f);
        REQUIRE(awake.getTrackedEnvelope() == preDormancy);

        sleepAllTones(slept);

        std::vector<float> traceSlept;
        std::vector<float> traceAwake;
        const auto window = static_cast<std::size_t>(0.5 * kDefaultsFixtureSampleRate);
        index = advanceBoth(slept, awake, index, window, kSensorBodyAmplitude, &traceSlept,
                            &traceAwake);
        static_cast<void>(index);

        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE(slept.isToneDormant(t));
            REQUIRE_FALSE(awake.isToneDormant(t));
        }

        // Skip the 50 ms during which the level ramp is still in flight; the
        // sleep edge cannot have latched before it parks.
        const std::size_t skip =
            static_cast<std::size_t>(0.05 * kDefaultsFixtureSampleRate) / kDormancyBlock;
        REQUIRE(traceSlept.size() > skip);
        REQUIRE(traceAwake.size() == traceSlept.size());

        float worstDifferential = 0.0f;
        float worstAbsolute = 0.0f;
        for (std::size_t k = skip; k < traceSlept.size(); ++k) {
            worstDifferential =
                std::max(worstDifferential, std::fabs(traceSlept[k] - traceAwake[k]));
            worstAbsolute = std::max(worstAbsolute, std::fabs(traceSlept[k] - preDormancy));
        }

        INFO("worst |slept - awake| " << worstDifferential << ", worst |slept - preDormancy| "
                                      << worstAbsolute
                                      << " (the measured 110 Hz follower ripple gives ~1.64e-3 "
                                         "on a correct build)");
        REQUIRE(worstDifferential <= kSensorDifferentialTolerance);
        REQUIRE(worstAbsolute <= kSensorAbsoluteTolerance);
    }

    SECTION("SC-014 (d): the generators keep running through 37 s of dormancy") {
        SubharmonicEngine slept;
        SubharmonicEngine never;
        makeDefaults(slept);
        makeDefaults(never);
        // trackingAmount = 0 keeps the subs free-running, so (d3)'s post-wake
        // render carries real signal. At the default 1.0 with a silent input
        // both renders would be identically zero and (d3) would be vacuous.
        slept.setTrackingAmount(0.0f);
        never.setTrackingAmount(0.0f);

        // Lockstep both for 200 ms before anything diverges.
        std::size_t index = advanceBoth(
            slept, never, 0, static_cast<std::size_t>(0.2 * kDefaultsFixtureSampleRate), 0.0);

        sleepAllTones(slept);
        index = advanceBoth(slept, never, index,
                            static_cast<std::size_t>(0.2 * kDefaultsFixtureSampleRate), 0.0);
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE(slept.isToneDormant(t));
            REQUIRE_FALSE(never.isToneDormant(t));
        }

        std::array<float, SubharmonicEngine::kNumTones> atSleepEdge{};
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            atSleepEdge[t] = slept.getToneBreathValue(t);
        }

        // 37 s: deliberately not a whole multiple of any FR-021 default breath
        // period (27.03 / 43.48 / 71.43 s at 0.037 / 0.023 / 0.014 Hz), so the
        // breath values at the wake are guaranteed to differ from the values at
        // the sleep edge - 1.37 / 0.86 / 0.52 of a cycle.
        index = advanceBoth(slept, never, index,
                            static_cast<std::size_t>(37.0 * kDefaultsFixtureSampleRate), 0.0);

        // (d1) The dormant instance's breathers really kept running.
        std::size_t moved = 0;
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            const float a = slept.getToneBreathValue(t);
            const float b = never.getToneBreathValue(t);
            INFO("tone " << t << ": slept " << a << ", never " << b << ", at sleep edge "
                         << atSleepEdge[t]);
            REQUIRE(std::fabs(a - b) <= 1.0e-3f);
            if (std::fabs(a - atSleepEdge[t]) > 0.05f) {
                ++moved;
            }
        }
        // (d2) NON-VACUITY: the agreement above is not two frozen values that
        //      happen to match.
        INFO("tones whose breath value moved by > 0.05 across the dormancy: " << moved);
        REQUIRE(moved >= std::size_t{2});

        // (d3) Both master accumulators advanced too: a 5 s post-wake render
        //      agrees within render_fingerprint.h's measured tolerances.
        wakeAllTones(slept);
        // 200 ms of settling first: the woken chain restarts from the state
        // FR-026 cleared and its level ramps climb for 50 ms, neither of which
        // is what (d3) is about. Both have decayed to nothing by 200 ms (the
        // 18 Hz blocker's slowest constant is ~8.8 ms).
        index = advanceBoth(slept, never, index,
                            static_cast<std::size_t>(0.2 * kDefaultsFixtureSampleRate), 0.0);
        static_cast<void>(index);

        const auto total = static_cast<std::size_t>(5.0 * kDefaultsFixtureSampleRate);
        std::vector<float> sleptL(total, 0.0f);
        std::vector<float> sleptR(total, 0.0f);
        std::vector<float> neverL(total, 0.0f);
        std::vector<float> neverR(total, 0.0f);
        captureSilentRender(slept, sleptL, sleptR);
        captureSilentRender(never, neverL, neverR);

        // NON-VACUITY: the comparison is between two real renders.
        INFO("post-wake RMS: slept " << bufferRms(sleptL) << ", never " << bufferRms(neverL));
        REQUIRE(bufferRms(neverL) > 1.0e-3f);

        std::string detail;
        const bool agree = rendersAgree(sleptL, sleptR, neverL, neverR, detail);
        INFO("37 s dormant vs never dormant: " << detail);
        REQUIRE(agree);
    }
}

// ==============================================================================
// T012 shared helpers - the routing / clamp-scope fixtures
// ==============================================================================

namespace {

/// The per-channel noise added on top of the COMMON 55 Hz body to make the
/// SC-022 (a2)/(b) input a real stereo signal rather than two copies of one
/// mono buffer. 0.15 peak against the body's 0.2512 peak: the two channels
/// differ on essentially every sample (which is the whole point - a
/// channel-identical input would make "both channels took the same scalar"
/// unfalsifiable), while the mono sum the FR-030 follower reads is still
/// dominated by the body.
constexpr double kRoutingNoiseAmplitude = 0.15;

/// The common -12 dBFS 55 Hz body of fillBody(), plus INDEPENDENT seeded noise
/// per channel. Both vectors must already be sized; `inR` is overwritten.
void fillStereoBody(std::vector<float>& inL, std::vector<float>& inR, std::uint32_t seedL,
                    std::uint32_t seedR) {
    fillBody(inL);
    inR = inL;
    Krate::DSP::Xorshift32 rngL{seedL};
    Krate::DSP::Xorshift32 rngR{seedR};
    for (std::size_t i = 0; i < inL.size(); ++i) {
        inL[i] += static_cast<float>(kRoutingNoiseAmplitude) * rngL.nextFloat();
        inR[i] += static_cast<float>(kRoutingNoiseAmplitude) * rngR.nextFloat();
    }
}

/// Render all of `inL`/`inR` through `engine` in kDefaultsFixtureBlock blocks via
/// the TAPPED entry point. `tap` may be null. Every output vector must already
/// be sized to inL.size().
void renderTapped(SubharmonicEngine& engine, const std::vector<float>& inL,
                  const std::vector<float>& inR, std::vector<float>& outL,
                  std::vector<float>& outR, std::vector<float>* tap) {
    const std::size_t total = inL.size();
    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
        engine.processBlockTapped(inL.data() + done, inR.data() + done, outL.data() + done,
                                  outR.data() + done,
                                  (tap != nullptr) ? (tap->data() + done) : nullptr, n);
    }
}

/// The same render through the UNTAPPED entry point. SC-022 (b) says "both
/// entry points", so the flag-false identity is asserted on processBlock() as
/// well as on processBlockTapped() - FR-063 makes them the same arithmetic, but
/// the criterion names both and a delegation that stopped delegating would only
/// show up here.
void renderPlain(SubharmonicEngine& engine, const std::vector<float>& inL,
                 const std::vector<float>& inR, std::vector<float>& outL,
                 std::vector<float>& outR) {
    const std::size_t total = inL.size();
    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
        engine.processBlock(inL.data() + done, inR.data() + done, outL.data() + done,
                            outR.data() + done, n);
    }
}

/// `out - in` over `[first, last)`, the isolated sub contribution the level
/// ratio in (b2) is measured on.
[[nodiscard]] std::vector<float> differenceWindow(const std::vector<float>& out,
                                                  const std::vector<float>& in,
                                                  std::size_t first, std::size_t last) {
    std::vector<float> d;
    d.reserve(last - first);
    for (std::size_t i = first; i < last; ++i) {
        d.push_back(out[i] - in[i]);
    }
    return d;
}

/// FR-051's linear wet gain, restated INDEPENDENTLY of the class (the header's
/// wetGain() is private). Same exact-zero fader bottom, same dbToGain, so a
/// settled wetGainRamp_ returns exactly this float and SC-022 (a1)'s identity
/// can be asserted with `==` rather than an epsilon.
[[nodiscard]] float expectedWetGainLinear(float db) {
    return (db <= SubharmonicEngine::kMinWetGainDb) ? 0.0f : Krate::DSP::dbToGain(db);
}

/// SC-022 (a2)/(b) render length. 4 s is long past the 50 ms gain ramps and the
/// 120 ms follower attack.
constexpr double kRoutingSeconds = 4.0;

}  // namespace

// ==============================================================================
// T012 - SC-022, the FR-064 sub-to-main routing flag
// ==============================================================================
// The flag gates renderChunk() step (9)'s ADD and nothing else. Everything
// upstream of it - the FR-062 tap (step (7)), the FR-054 clamp (step (8)),
// FR-025's dormancy bookkeeping - is computed on `subChain` and is therefore
// identical in both flag states. That is the whole content of SC-022, and each
// arm below asserts one half of it.
//
// (a) is asserted in the S14 C-11 rewritten form. The spec's original operand -
// "bit-identical to a render taken before FR-064 existed" - is not producible:
// the shipped implementation always has the flag, so the only render an author
// can put on the right-hand side is another flag-true render, and the criterion
// could not fail on any implementation. (a1)/(a2) assert what FR-050's formula
// actually claims instead.
//
// ------------------------------------------------------------------------------
// ARM (c): the gate is a hard step (a fade would violate (b)'s bit-identity),
// so the arm asserts the toggle adds no discontinuity BEYOND its own step - no
// ClickDetector detection outside a one-sample window of either toggle index.
// The arithmetic behind that shape is in the comment above its SECTION.
// ------------------------------------------------------------------------------
// ==============================================================================
TEST_CASE("SubharmonicEngine_SubToMainRouting", "[subharmonic_engine]") {
    SECTION("SC-022 (a1): silent in, the output IS the clamped, wet-scaled tap") {
        // Both wet gains: 0 dB is the shipped default, where the multiply is the
        // identity and the arm would not notice a dropped wet stage; -6 dB makes
        // the multiply real. Bit-exactness survives both - dbToGain(-6.0f) here
        // is the same constexpr call setWetGainDb() handed to the ramp, and a
        // settled LinearRamp returns target_ itself (smoother.h:370-372).
        for (const float wetDb : {0.0f, -6.0f}) {
            SubharmonicEngine engine;
            makeDefaults(engine);

            // Tracking at its FR-033 default of 1.0 with a SILENT input drives
            // envNorm to 0, which pins trackGain to 0 and makes the tap a flat
            // zero whether the chain runs or not (the mechanism SC-014 (b)
            // documents above). The arm would then assert 0 == 0 at every
            // sample. 0 makes the FR-032 target exactly (1 - 0) + 0 * envNorm =
            // 1.0f, so the sub runs free and the identity has something to be an
            // identity about - which the peak-tap check below enforces.
            engine.setTrackingAmount(0.0f);
            engine.setWetGainDb(wetDb);
            REQUIRE(engine.getSubToMainEnabled());  // FR-064's default is true

            // 100 ms is twice kGainRampMs, so every per-sample ramp is PARKED ON
            // its target before the measured render starts.
            static_cast<void>(advanceOne(
                engine, 0, static_cast<std::size_t>(0.1 * kDefaultsFixtureSampleRate), 0.0));

            const auto total = static_cast<std::size_t>(1.0 * kDefaultsFixtureSampleRate);
            const std::vector<float> silence(kDefaultsFixtureBlock, 0.0f);
            std::vector<float> outL(total, 0.0f);
            std::vector<float> outR(total, 0.0f);
            std::vector<float> tap(total, 0.0f);
            for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
                const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
                engine.processBlockTapped(silence.data(), silence.data(), outL.data() + done,
                                          outR.data() + done, tap.data() + done, n);
            }

            const float wetLinear = expectedWetGainLinear(wetDb);
            std::size_t mismatches = 0;
            std::size_t channelMismatches = 0;
            float peakTap = 0.0f;
            for (std::size_t i = 0; i < total; ++i) {
                peakTap = std::max(peakTap, std::fabs(tap[i]));
                const float expected =
                    std::clamp(tap[i] * wetLinear, -SubharmonicEngine::kOutputClamp,
                               SubharmonicEngine::kOutputClamp);
                if (outL[i] != expected) {
                    ++mismatches;
                }
                if (outL[i] != outR[i]) {
                    ++channelMismatches;
                }
            }

            INFO("wet " << wetDb << " dB (linear " << wetLinear << "), peak tap " << peakTap
                        << ", value mismatches " << mismatches << ", L/R mismatches "
                        << channelMismatches << " of " << total);
            // NON-VACUITY: the identity is asserted on a tap that carries real
            // audio. At the FR-020 defaults the sub peaks around 0.15.
            REQUIRE(peakTap > 1.0e-2f);
            REQUIRE(mismatches == std::size_t{0});
            REQUIRE(channelMismatches == std::size_t{0});
            // The clamp is a backstop here, not a shaping stage.
            REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
        }
    }

    SECTION("SC-022 (a2): under a real stereo body both channels take the same scalar") {
        SubharmonicEngine engine;
        makeDefaults(engine);

        const auto total = static_cast<std::size_t>(kRoutingSeconds * kDefaultsFixtureSampleRate);
        std::vector<float> inL(total, 0.0f);
        std::vector<float> inR(total, 0.0f);
        fillStereoBody(inL, inR, 0x5C220A21u, 0x5C220A22u);

        // NON-VACUITY of the FIXTURE: two channels that were bit-identical would
        // make "the same scalar reached both" true for free.
        std::size_t differing = 0;
        for (std::size_t i = 0; i < total; ++i) {
            if (inL[i] != inR[i]) {
                ++differing;
            }
        }
        INFO("stereo fixture: " << differing << " of " << total << " samples differ, correlation "
                                << lfm::calculateCorrelation(inL.data(), inR.data(), total));
        REQUIRE(differing > (total * 99) / 100);

        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);
        std::vector<float> tap(total, 0.0f);
        renderTapped(engine, inL, inR, outL, outR, &tap);

        // NON-VACUITY of the SUB: a silent sub makes both differences zero.
        REQUIRE(bufferRms(tap) > 1.0e-3f);

        // No settling window: the claim is per-sample and holds from the first
        // sample, because both channels read the SAME `add` in the same
        // iteration. The only admissible difference is the rounding of the add
        // itself, bounded by half an ulp of |x| + |add|.
        float worst = 0.0f;
        std::size_t worstIndex = 0;
        std::size_t violations = 0;
        for (std::size_t i = 0; i < total; ++i) {
            const float dL = outL[i] - inL[i];
            const float dR = outR[i] - inR[i];
            const float tolerance =
                1.0e-6f * std::max(1.0f, std::max(std::fabs(inL[i]), std::fabs(inR[i])));
            const float error = std::fabs(dL - dR);
            if (error > worst) {
                worst = error;
                worstIndex = i;
            }
            if (error > tolerance) {
                ++violations;
            }
        }
        INFO("worst |dL - dR| " << worst << " at sample " << worstIndex << ", violations "
                                << violations << " of " << total);
        REQUIRE(violations == std::size_t{0});
    }

    SECTION("SC-022 (b): the flag routes the sub out of main and leaves the tap alone") {
        // Three engines, identical fixture and setter sequence apart from the
        // flag itself. makeDefaults() pushes the same seed after prepare(), so
        // all three start from the top of the same three breath trajectories.
        SubharmonicEngine enabled;
        SubharmonicEngine disabledTapped;
        SubharmonicEngine disabledPlain;
        makeDefaults(enabled);
        makeDefaults(disabledTapped);
        makeDefaults(disabledPlain);
        disabledTapped.setSubToMainEnabled(false);
        disabledPlain.setSubToMainEnabled(false);
        REQUIRE(enabled.getSubToMainEnabled());
        REQUIRE_FALSE(disabledTapped.getSubToMainEnabled());
        REQUIRE_FALSE(disabledPlain.getSubToMainEnabled());

        const auto total = static_cast<std::size_t>(kRoutingSeconds * kDefaultsFixtureSampleRate);
        std::vector<float> inL(total, 0.0f);
        std::vector<float> inR(total, 0.0f);
        fillStereoBody(inL, inR, 0x5C220B31u, 0x5C220B32u);

        std::vector<float> onL(total, 0.0f);
        std::vector<float> onR(total, 0.0f);
        std::vector<float> onTap(total, 0.0f);
        renderTapped(enabled, inL, inR, onL, onR, &onTap);

        std::vector<float> offL(total, 0.0f);
        std::vector<float> offR(total, 0.0f);
        std::vector<float> offTap(total, 0.0f);
        renderTapped(disabledTapped, inL, inR, offL, offR, &offTap);

        std::vector<float> plainL(total, 0.0f);
        std::vector<float> plainR(total, 0.0f);
        renderPlain(disabledPlain, inL, inR, plainL, plainR);

        // NON-VACUITY: with the flag TRUE the sub really did reach the output,
        // so "with the flag false it does not" is a difference rather than a
        // restatement of a silent engine.
        std::size_t audibleSamples = 0;
        for (std::size_t i = 0; i < total; ++i) {
            if (onL[i] != inL[i]) {
                ++audibleSamples;
            }
        }
        INFO("flag-true render differs from the dry input on " << audibleSamples << " of " << total
                                                               << " samples");
        REQUIRE(audibleSamples > (total * 90) / 100);
        REQUIRE(bufferRms(onTap) > 1.0e-3f);

        std::size_t tappedLeaks = 0;
        std::size_t plainLeaks = 0;
        std::size_t tapMismatches = 0;
        for (std::size_t i = 0; i < total; ++i) {
            if (offL[i] != inL[i] || offR[i] != inR[i]) {
                ++tappedLeaks;
            }
            if (plainL[i] != inL[i] || plainR[i] != inR[i]) {
                ++plainLeaks;
            }
            if (offTap[i] != onTap[i]) {
                ++tapMismatches;
            }
        }
        INFO("flag-false leaks: tapped entry " << tappedLeaks << ", plain entry " << plainLeaks
                                               << "; tap mismatches " << tapMismatches << " of "
                                               << total);
        // The main output IS the dry input, on both entry points (FR-064).
        REQUIRE(tappedLeaks == std::size_t{0});
        REQUIRE(plainLeaks == std::size_t{0});
        // ...while the tap is untouched by the flag (FR-062). This is the whole
        // point of promoting the tap: Phase 10 disables the add and still reads
        // the full sub.
        REQUIRE(tapMismatches == std::size_t{0});
    }

    SECTION("SC-022 (b2): the tap is PRE-wet-gain") {
        // Every OTHER criterion that reads subTap runs at setWetGainDb(0.0f) -
        // the isolated fixture, SC-020, SC-021, and (b) above, which compares
        // two taps at the SAME wet gain - so an implementation that taps after
        // the wet multiply is green everywhere and Phase 10 silently inherits a
        // wet-scaled signal. This is the one arm that separates them.
        SubharmonicEngine unity;
        SubharmonicEngine cut;
        makeDefaults(unity);
        makeDefaults(cut);
        unity.setWetGainDb(0.0f);
        cut.setWetGainDb(-12.0f);

        const auto total = static_cast<std::size_t>(3.0 * kDefaultsFixtureSampleRate);
        std::vector<float> body(total, 0.0f);
        fillBody(body);

        std::vector<float> unityL(total, 0.0f);
        std::vector<float> unityR(total, 0.0f);
        std::vector<float> unityTap(total, 0.0f);
        renderTapped(unity, body, body, unityL, unityR, &unityTap);

        std::vector<float> cutL(total, 0.0f);
        std::vector<float> cutR(total, 0.0f);
        std::vector<float> cutTap(total, 0.0f);
        renderTapped(cut, body, body, cutL, cutR, &cutTap);

        std::size_t tapMismatches = 0;
        for (std::size_t i = 0; i < total; ++i) {
            if (unityTap[i] != cutTap[i]) {
                ++tapMismatches;
            }
        }
        INFO("tap mismatches between 0 dB and -12 dB wet: " << tapMismatches << " of " << total);
        REQUIRE(bufferRms(unityTap) > 1.0e-3f);
        REQUIRE(tapMismatches == std::size_t{0});

        // The MAIN output, however, must move by exactly the fader. Measured
        // over the last second, well past the 50 ms wet ramp.
        const std::size_t last = total;
        const std::size_t first = total - static_cast<std::size_t>(kDefaultsFixtureSampleRate);
        const float unityRms =
            subBandRms(differenceWindow(unityL, body, first, last), kDefaultsFixtureSampleRate);
        const float cutRms =
            subBandRms(differenceWindow(cutL, body, first, last), kDefaultsFixtureSampleRate);

        REQUIRE(unityRms > 1.0e-3f);
        REQUIRE(cutRms > 0.0f);
        const float ratioDb = 20.0f * std::log10(cutRms / unityRms);
        INFO("sub-band RMS of (out - in): unity " << unityRms << ", cut " << cutRms << " -> "
                                                  << ratioDb << " dB");
        REQUIRE(std::fabs(ratioDb + 12.0f) <= 0.1f);
        REQUIRE(unity.getClampEngagementCount() == std::uint32_t{0});
        REQUIRE(cut.getClampEngagementCount() == std::uint32_t{0});
    }

    // ==========================================================================
    // SC-022 (c) - the toggle adds nothing beyond its own step
    // ==========================================================================
    // SC-022 (b) makes the main output BIT-IDENTICAL to the dry input while the
    // flag is false, which forbids a de-zippered gate (a fade would differ on
    // every sample of the fade), so setSubToMainEnabled is a hard step (see the
    // setter's own comment in subharmonic_engine.h). A hard step of the sub
    // contribution IS a discontinuity, and ClickDetector finds it:
    //   * ClickDetector thresholds |x[i] - x[i-1]| at mean + 5*stdDev computed
    //     over the containing 512-sample frame (artifact_detection.h:171-217).
    //   * The dry body is a 0.2512-peak 55 Hz sine at 48 kHz, whose per-sample
    //     first difference peaks at 0.2512 * 2*pi*55/48000 = 1.81e-3, with
    //     mean|d| = (2/pi)*1.81e-3 = 1.15e-3 and stdDev 5.6e-4.
    //   * One outlier of magnitude S in a 512-sample frame moves the frame
    //     statistics to mean ~ 1.15e-3 + S/512 and stdDev ~ sqrt(S^2/512), so
    //     the threshold is ~ 1.15e-3 + 0.221*S: ANY S above ~1.5e-3 is flagged,
    //     and the sub at the FR-020 defaults is ~0.02-0.15.
    // So the arm does not ask the step to be invisible (an earlier draft did;
    // ruled 2026-09-13). It asks that the step at the toggle index be the ONLY
    // discontinuity: no detection outside a one-sample window of either toggle,
    // and, for non-vacuity, at least one toggle step above the detector's
    // threshold so the exclusion window is proven to exclude a real step.
    // ==========================================================================
    SECTION("SC-022 (c): toggling the flag mid-render adds nothing beyond its own step") {
        SubharmonicEngine engine;
        makeDefaults(engine);

        const auto segment = static_cast<std::size_t>(1.0 * kDefaultsFixtureSampleRate);
        const std::size_t total = 3 * segment;
        std::vector<float> body(total, 0.0f);
        fillBody(body);

        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);
        std::vector<float> tap(total, 0.0f);

        // Block sizes are clamped so a render never steps OVER a toggle point:
        // 48000 is not a multiple of 512, so without the clamp the setter would
        // land at an arbitrary offset inside a block and the step index read
        // below would be wrong.
        std::size_t done = 0;
        while (done < total) {
            if (done == segment) {
                engine.setSubToMainEnabled(false);
            } else if (done == 2 * segment) {
                engine.setSubToMainEnabled(true);
            }
            std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
            if (done < segment) {
                n = std::min(n, segment - done);
            } else if (done < 2 * segment) {
                n = std::min(n, 2 * segment - done);
            }
            engine.processBlockTapped(body.data() + done, body.data() + done, outL.data() + done,
                                      outR.data() + done, tap.data() + done, n);
            done += n;
        }

        // NON-VACUITY: a silent sub makes any toggle click-free for free.
        REQUIRE(bufferRms(tap) > 1.0e-3f);
        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});

        lfm::ClickDetector detector{lfm::ClickDetectorConfig{.sampleRate = 48000.0f}};
        detector.prepare();
        const auto detections = detector.detect(outL.data(), outL.size());

        const float stepAtDisable = std::fabs(outL[segment] - outL[segment - 1]);
        const float stepAtEnable = std::fabs(outL[2 * segment] - outL[2 * segment - 1]);
        INFO("toggle at samples " << segment << " and " << 2 * segment << "; |step| at disable "
                                  << stepAtDisable << ", at enable " << stepAtEnable
                                  << "; sub just before disable " << tap[segment - 1]
                                  << ", just before enable " << tap[2 * segment - 1]
                                  << "; detections " << detections.size()
                                  << (detections.empty()
                                          ? std::string{}
                                          : (", first at sample " +
                                             std::to_string(detections.front().sampleIndex))));

        // The step AT each toggle index is the sub's own instantaneous value and
        // is expected; anything outside a one-sample window of it is a defect.
        constexpr std::size_t kToggleWindow = 1;
        std::size_t outsideWindow = 0;
        for (const auto& d : detections) {
            const std::size_t i = d.sampleIndex;
            const bool nearDisable = (i + kToggleWindow >= segment) && (i <= segment + kToggleWindow);
            const bool nearEnable =
                (i + kToggleWindow >= 2 * segment) && (i <= 2 * segment + kToggleWindow);
            if (!nearDisable && !nearEnable) {
                ++outsideWindow;
            }
        }
        REQUIRE(outsideWindow == std::size_t{0});

        // NON-VACUITY of the exclusion: at least one toggle produced a real step
        // (above the detector's ~1.5e-3 threshold against this body), so the
        // window above is excluding something rather than nothing.
        constexpr float kDetectorThresholdFloor = 1.5e-3f;
        REQUIRE(std::max(stepAtDisable, stepAtEnable) > kDetectorThresholdFloor);
    }

    SECTION("SC-022 (d): dormancy, the infrasonic latch and the clamp counter ignore the flag") {
        SubharmonicEngine on;
        SubharmonicEngine off;
        makeDefaults(on);
        makeDefaults(off);
        off.setSubToMainEnabled(false);

        // A fixture in which the predicates take BOTH values, so "the two engines
        // agree" is not two copies of all-false. f = 40 Hz puts Div4 at 10 Hz,
        // below kMinToneHz = 12, so the FR-016 backstop latches it and only it
        // (Div2 = 20 Hz, FifthBelow = 26.67 Hz). Tone 2 is driven to the FR-020
        // fader bottom, so exactly one tone is dormant.
        for (SubharmonicEngine* engine : {&on, &off}) {
            engine->setFundamentalHz(40.0f);
            engine->setToneLevelDb(2, SubharmonicEngine::kMinToneLevelDb);
        }

        // 500 ms: ten times the 50 ms level ramp, so tone 2's ramp is parked ON
        // the literal 0.0f isToneDormant() tests for, and many control steps have
        // run the FR-016 latch.
        const auto total = static_cast<std::size_t>(0.5 * kDefaultsFixtureSampleRate);
        std::vector<float> body(total, 0.0f);
        fillBody(body);
        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);
        renderTapped(on, body, body, outL, outR, nullptr);
        renderTapped(off, body, body, outL, outR, nullptr);

        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t << ": dormant on=" << on.isToneDormant(t)
                         << " off=" << off.isToneDormant(t)
                         << ", floored on=" << on.isToneInfrasonicFloored(t)
                         << " off=" << off.isToneInfrasonicFloored(t));
            REQUIRE(on.isToneDormant(t) == off.isToneDormant(t));
            REQUIRE(on.isToneInfrasonicFloored(t) == off.isToneInfrasonicFloored(t));
        }
        REQUIRE(on.getClampEngagementCount() == off.getClampEngagementCount());

        // TEETH: the fixture really did produce a mixed pattern.
        REQUIRE_FALSE(on.isToneDormant(0));
        REQUIRE_FALSE(on.isToneDormant(1));
        REQUIRE(on.isToneDormant(2));
        REQUIRE_FALSE(on.isToneInfrasonicFloored(0));
        REQUIRE(on.isToneInfrasonicFloored(1));
        REQUIRE_FALSE(on.isToneInfrasonicFloored(2));
        REQUIRE(on.getClampEngagementCount() == std::uint32_t{0});
    }
}

// ==============================================================================
// T012 - the Q6/D-12 clamp-scope edge case
// ==============================================================================
// THE ONLY ASSERTION IN THE PHASE THAT CAN FAIL IF FR-054's CLAMP IS MIS-SCOPED.
// Every other criterion that reads getClampEngagementCount() asserts == 0, and
// every other planned render keeps the summed output well under kOutputClamp =
// 4.0, so an implementation clamping (in + sub) instead of the wet sub alone is
// green on all of them. The spec's own Edge Case (spec.md:1368-1377) is the
// separator and had no criterion attached; this is it.
//
// The dry path is a pure add with no filtering, so a DC fixture is legitimate
// here: the follower simply reads an RMS of 6.0 and clamps envNorm to 1. +6.0
// linear is ~ +15.6 dBFS, i.e. 2.0 linear ABOVE the clamp - so a mis-scoped
// clamp shows up as a hard 4.0 ceiling and fails arm (i) by a full 1.0, while
// the true sub contribution at the defaults is <= ~0.15 and leaves |out| >= 5.85.
// ==============================================================================
TEST_CASE("SubharmonicEngine_ClampScope", "[subharmonic_engine]") {
    SubharmonicEngine engine;
    makeDefaults(engine);
    REQUIRE(engine.getSubToMainEnabled());

    const auto half = static_cast<std::size_t>(2.0 * kDefaultsFixtureSampleRate);
    const std::size_t total = 2 * half;
    std::vector<float> inL(total, 6.0f);
    std::vector<float> inR(total, 6.0f);
    for (std::size_t i = half; i < total; ++i) {
        inL[i] = -6.0f;
        inR[i] = -6.0f;
    }

    std::vector<float> outL(total, 0.0f);
    std::vector<float> outR(total, 0.0f);
    std::vector<float> tap(total, 0.0f);
    renderTapped(engine, inL, inR, outL, outR, &tap);

    // NON-VACUITY: the sub really is running. Without this an engine that added
    // nothing at all would pass every arm below.
    REQUIRE(bufferRms(tap) > 1.0e-3f);

    // 100 ms is discarded either side of the step: the +6 -> -6 reversal is a
    // 12.0-linear transient into the FR-030 follower, and nothing about the
    // clamp's SCOPE is measured during it.
    const auto guard = static_cast<std::size_t>(0.1 * kDefaultsFixtureSampleRate);
    REQUIRE(half > 2 * guard);

    float minMagnitude = 1.0e30f;
    std::size_t minIndex = 0;
    float maxContribution = 0.0f;
    std::size_t asymmetries = 0;
    std::size_t examined = 0;
    for (std::size_t i = 0; i < total; ++i) {
        const bool inFirstWindow = (i >= guard) && (i < half - guard);
        const bool inSecondWindow = (i >= half + guard) && (i < total - guard);
        if (!inFirstWindow && !inSecondWindow) {
            continue;
        }
        ++examined;
        const float magnitude = std::min(std::fabs(outL[i]), std::fabs(outR[i]));
        if (magnitude < minMagnitude) {
            minMagnitude = magnitude;
            minIndex = i;
        }
        maxContribution = std::max(maxContribution, std::fabs(outL[i] - inL[i]));
        // (iii) both channels take the SAME scalar and see the same input here,
        //       so the two differences are bit-equal, not merely close.
        if ((outL[i] - inL[i]) != (outR[i] - inR[i])) {
            ++asymmetries;
        }
    }

    INFO("examined " << examined << " samples; min |out| " << minMagnitude << " at sample "
                     << minIndex << "; max |out - in| " << maxContribution << "; asymmetries "
                     << asymmetries << "; clamp engagements " << engine.getClampEngagementCount());
    REQUIRE(examined > std::size_t{0});
    // (i) an implementation clamping (in + sub) caps this at exactly 4.0.
    REQUIRE(minMagnitude > 5.0f);
    // (ii) the clamp binds the wet sub, which never comes near kOutputClamp.
    REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
    // (iii)
    REQUIRE(asymmetries == std::size_t{0});
    // The sub contribution really is the small thing the arithmetic above
    // assumes - if it were near 1.0 the 5.0 threshold would be measuring the
    // sub's level rather than the clamp's scope.
    REQUIRE(maxContribution < 1.0f);
}

// ==============================================================================
// T013 - SC-001 (a), (b), (c) and (c2): the free-running and release brackets
// ==============================================================================
// FOUR ARMS, ONE MECHANISM: renderChunk() step (5)'s `y *= tg`, where `tg` is
// trackGainRamp_.process() and the ramp's target is updateControl() step (4)'s
// `(1 - trackingAmount_) + trackingAmount_ * trackedEnvNorm_`. At the FR-033
// default trackingAmount = 1.0 that target IS envNorm, so the sub's level is
// the follower's reading of the body and nothing else.
//
// (a) is mechanically exact rather than statistical: with a silent input the
//     follower reads exactly 0.0f, envNorm is exactly 0.0f, and prepare() step
//     (11) snapped the ramp to `1 - trackingAmount_` = exactly 0.0f - so the
//     ramp is already parked on its target and every rendered sub sample is
//     `blocker_.process(0.0f * something)`. There is nothing for 60 s to
//     accumulate. The -80 dBFS ceiling is 1e-4; a correct build reads 0.
//
// (b)/(c)/(c2) share ONE continuous timeline (10 s silent -> 10 s body -> 5 s
//     silent) because they are three readings of the same wake-and-release
//     transient; re-rendering per arm would cost three times as much and would
//     let the three arms disagree about which render they describe. They live
//     in one SECTION for that reason.
//
// WHY (c2) EXISTS (plan S10.4). The spec's (b) and (c) are a floor and a
// ceiling that the UN-PUSHED EnvelopeFollower defaults (10 ms attack / 100 ms
// release, envelope_follower.h:92-93) satisfy MORE easily than the FR-031
// values do: a faster follower wakes faster and releases faster. So neither can
// detect a build that stores 120/800 and never pushes them. (c2) brackets the
// release from BELOW instead, and the two configurations are ~90 dB apart in
// that window.
//
// (c2) arithmetic, at 48 kHz with the -12 dBFS body:
//   * EnvelopeFollower is RMS-mode, i.e. an asymmetric one-pole in the SQUARED
//     domain with coeff = exp(-2*pi/timeSamples) (envelope_follower.h:356-365).
//     A "release time" of 800 ms is therefore a squared-domain time constant of
//     800/(2*pi) = 127.3 ms, and an AMPLITUDE time constant of 254.6 ms.
//   * The body RMS is 0.2512/sqrt(2) = 0.1776, against the FR-035 default
//     reference dbToGain(-18) = 0.1259. envNorm = env/reference is therefore
//     CLAMPED at 1.0 until the envelope has fallen by a factor 0.709, which
//     takes -ln(0.709) * 254.6 ms = 88 ms.
//   * At 500 ms the remaining 412 ms of decay give exp(-412/254.6) = 0.198,
//     about -14 dB relative to the pre-removal steady state - well above the
//     -20 dB floor asserted below.
//   * With the un-pushed 100 ms release (amplitude tau 31.8 ms) the same window
//     reads exp(-489/31.8) = 2e-7, i.e. below -130 dB. That separation is what
//     this arm buys.
//
// FALSIFICATION (tasks.md T013; the result belongs in the compliance notes):
// run this case once with the `follower_.setReleaseTime(...)` line inside
// SubharmonicEngine::setFollowerReleaseMs() commented out - equivalently, with
// applyDefaults()'s `setFollowerReleaseMs(kDefaultFollowerReleaseMs)` line
// removed - and confirm that (c2) FAILS while (a), (b) and (c) still pass.
// Then restore.
// ==============================================================================

namespace {

/// SC-001's fixture: the FR-013 default fundamental, all three tone levels at
/// `kMaxToneLevelDb` and wet gain at `kMaxWetGainDb`. Tracking is deliberately
/// NOT written - the criterion is stated at "the default", and the case asserts
/// that the default really is 1.0 rather than assuming it.
void makeTrackingFixture(SubharmonicEngine& engine) {
    makeDefaults(engine);
    engine.setFundamentalHz(static_cast<float>(kBodyHz));
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        engine.setToneLevelDb(t, SubharmonicEngine::kMaxToneLevelDb);
    }
    engine.setWetGainDb(SubharmonicEngine::kMaxWetGainDb);
}

/// Render `total` samples of SILENCE and return the peak |out| over BOTH
/// channels. Streamed rather than captured: 60 s at 48 kHz is 2.88 M samples
/// per channel and (a) needs only the running maximum.
[[nodiscard]] float renderSilentPeak(SubharmonicEngine& engine, std::size_t total) {
    const std::vector<float> silence(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> outL(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> outR(kDefaultsFixtureBlock, 0.0f);

    float peak = 0.0f;
    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
        engine.processBlock(silence.data(), silence.data(), outL.data(), outR.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            peak = std::max(peak, std::fabs(outL[i]));
            peak = std::max(peak, std::fabs(outR[i]));
        }
    }
    return peak;
}

/// Render `total` samples of a 55 Hz sine of PEAK amplitude `amplitude` (pass
/// 0.0 for silence) starting at ABSOLUTE sample index `index`, appending the
/// input and the left output to `in` / `out` and folding the peak of BOTH
/// output channels into `peak`. Returns the new absolute index.
///
/// Absolute-index phase, as in advanceOne(): the 10 s silent lead-in is exactly
/// 550 cycles of 55 Hz at 48 kHz, so the body switches on at a zero crossing
/// and the wake transient (b) measures is the ENGINE's, not a step edge the
/// fixture injected.
std::size_t renderCapture(SubharmonicEngine& engine, std::size_t index, std::size_t total,
                          double amplitude, std::vector<float>& in, std::vector<float>& out,
                          float& peak) {
    std::vector<float> blockIn(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> outL(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> outR(kDefaultsFixtureBlock, 0.0f);
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / kDefaultsFixtureSampleRate;

    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
        for (std::size_t i = 0; i < n; ++i) {
            blockIn[i] =
                static_cast<float>(amplitude * std::sin(omega * static_cast<double>(index + i)));
        }
        engine.processBlock(blockIn.data(), blockIn.data(), outL.data(), outR.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            in.push_back(blockIn[i]);
            out.push_back(outL[i]);
            peak = std::max(peak, std::fabs(outL[i]));
            peak = std::max(peak, std::fabs(outR[i]));
        }
        index += n;
    }
    return index;
}

/// Samples in `seconds` at the fixture rate.
[[nodiscard]] std::size_t fixtureSamples(double seconds) {
    return static_cast<std::size_t>(seconds * kDefaultsFixtureSampleRate);
}

/// dB of `value` relative to `reference`. Both are RMS amplitudes, so this is
/// the 20*log10 form. The floor keeps a legitimately-zero release tail from
/// producing -inf in an INFO line; the non-vacuity REQUIREs at the call sites
/// are what keep the reference itself honest.
[[nodiscard]] float relativeDb(float value, float reference) {
    constexpr float kFloor = 1.0e-12f;
    return 20.0f * std::log10(std::max(value, kFloor) / std::max(reference, kFloor));
}

}  // namespace

TEST_CASE("SubharmonicEngine_TrackingSuppressesFreeRunning", "[subharmonic_engine]") {
    SECTION("SC-001 (a): 60 s of silent input never exceeds -80 dBFS") {
        SubharmonicEngine engine;
        makeTrackingFixture(engine);
        // The criterion is stated at the DEFAULT tracking amount, so the
        // default is asserted rather than assumed.
        REQUIRE(engine.getTrackingAmount() == SubharmonicEngine::kDefaultTrackingAmount);
        REQUIRE(engine.getTrackingAmount() == 1.0f);
        // Non-vacuity of the CONFIGURATION (the render is silent by design, so
        // there is no signal to gate on): the tones really are at the top of
        // the fader, awake and unfloored, and the wet gain really is at its
        // ceiling - which is what makes "no free-running boom" a claim about a
        // loud engine rather than about a muted one.
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            REQUIRE(engine.getToneLevelDb(t) == SubharmonicEngine::kMaxToneLevelDb);
            REQUIRE_FALSE(engine.isToneDormant(t));
            REQUIRE_FALSE(engine.isToneInfrasonicFloored(t));
        }
        REQUIRE(engine.getWetGainDb() == SubharmonicEngine::kMaxWetGainDb);

        const float peak = renderSilentPeak(engine, fixtureSamples(60.0));
        const float ceiling = Krate::DSP::dbToGain(-80.0f);

        INFO("SC-001 (a): 60 s silent peak |out| = " << peak << " (ceiling " << ceiling
                                                     << " = -80 dBFS)");
        REQUIRE(peak <= ceiling);
        // The clamp is a level statement and nothing here comes near it.
        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
    }

    SECTION("SC-001 (b)/(c)/(c2): wake, release, and the FR-031 release constant") {
        SubharmonicEngine engine;
        makeTrackingFixture(engine);
        REQUIRE(engine.getTrackingAmount() == 1.0f);
        // (c2) is a statement about THIS constant. If the forwarding getter
        // does not report it, the arm below is measuring something else.
        REQUIRE(std::fabs(engine.getFollowerReleaseMs() -
                          SubharmonicEngine::kDefaultFollowerReleaseMs) <= 1.0e-3f);

        const std::size_t oneSecond   = fixtureSamples(1.0);
        const std::size_t tenSeconds  = fixtureSamples(10.0);
        const std::size_t fiveSeconds = fixtureSamples(5.0);

        // ---- phase 1: 10 s silent, discarded --------------------------------
        std::size_t index = advanceOne(engine, 0, tenSeconds, 0.0);
        REQUIRE(index == tenSeconds);

        // ---- phase 2: the step to a -12 dBFS 55 Hz body, 10 s captured ------
        std::vector<float> wakeIn;
        std::vector<float> wakeOut;
        wakeIn.reserve(tenSeconds);
        wakeOut.reserve(tenSeconds);
        float wakePeak = 0.0f;
        index =
            renderCapture(engine, index, tenSeconds, kBodyAmplitude, wakeIn, wakeOut, wakePeak);
        REQUIRE(wakeOut.size() == tenSeconds);

        // Steady state: the sub-band (< 200 Hz) RMS of (out - in) over the LAST
        // SECOND of the post-step render - which is also the second immediately
        // before the removal, so (c2) is measured against the state it decays
        // from and not against a different render.
        const float steadyRms = subBandRms(
            differenceWindow(wakeOut, wakeIn, tenSeconds - oneSecond, tenSeconds),
            kDefaultsFixtureSampleRate);
        // The same quantity over the first 400 ms after the step.
        const float earlyRms =
            subBandRms(differenceWindow(wakeOut, wakeIn, 0, fixtureSamples(0.4)),
                       kDefaultsFixtureSampleRate);

        // NON-VACUITY: a silent engine would satisfy "early >= 50 % of steady"
        // with 0 >= 0. The steady state has to be a real signal first.
        INFO("SC-001 (b): steady sub-band RMS "
             << steadyRms << ", first-400 ms RMS " << earlyRms << " ("
             << relativeDb(earlyRms, steadyRms) << " dB relative), peak |out| " << wakePeak);
        REQUIRE(steadyRms > 1.0e-3f);
        REQUIRE(earlyRms >= 0.5f * steadyRms);
        REQUIRE(wakePeak <= SubharmonicEngine::kOutputClamp);

        // ---- phase 3: the body is removed, 5 s captured ---------------------
        std::vector<float> releaseIn;
        std::vector<float> releaseOut;
        releaseIn.reserve(fiveSeconds);
        releaseOut.reserve(fiveSeconds);
        float releasePeak = 0.0f;
        index = renderCapture(engine, index, fiveSeconds, 0.0, releaseIn, releaseOut, releasePeak);
        REQUIRE(releaseOut.size() == fiveSeconds);
        REQUIRE(index == 2 * tenSeconds + fiveSeconds);

        // (c2) FIRST, because it is the arm that separates the pushed FR-031
        // release from the shipped 100 ms default: [400 ms, 600 ms] after the
        // removal must still be within 20 dB of the pre-removal steady state.
        const float releaseWindowRms =
            subBandRms(differenceWindow(releaseOut, releaseIn, fixtureSamples(0.4),
                                        fixtureSamples(0.6)),
                       kDefaultsFixtureSampleRate);
        const float releaseWindowDb = relativeDb(releaseWindowRms, steadyRms);
        INFO("SC-001 (c2): [400, 600] ms sub-band RMS "
             << releaseWindowRms << " = " << releaseWindowDb
             << " dB relative to steady (floor -20 dB)");
        REQUIRE(releaseWindowDb > -20.0f);

        // (c) the release really does complete: the WHOLE final second of the
        // 5 s tail is already below the -80 dBFS floor, so "falls below
        // -80 dBFS within 5 s" holds with a second to spare.
        const float tailRms =
            subBandRms(differenceWindow(releaseOut, releaseIn, fixtureSamples(4.0), fiveSeconds),
                       kDefaultsFixtureSampleRate);
        const float floorRms = Krate::DSP::dbToGain(-80.0f);
        INFO("SC-001 (c): [4, 5] s sub-band RMS " << tailRms << " (floor " << floorRms
                                                  << " = -80 dBFS)");
        REQUIRE(tailRms < floorRms);
        REQUIRE(releasePeak <= SubharmonicEngine::kOutputClamp);
        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
    }
}

// ==============================================================================
// T014 - SC-020 (a)-(c), the infrasonic floor, and SC-008, click freedom
// ==============================================================================
// SC-020 (d), the four backstop boundaries read straight off the latch, already
// landed in T009's SubharmonicEngine_ToneMapping; this task owns the AUDIO arms.
//
// FALSIFICATIONS (tasks.md T014; run once during implementation, recorded in
// compliance):
//   * SC-008, the cutoff arm. In subharmonic_engine.h, replace
//     setLowpassCutoffHz()'s `cutoffGlide_.setTarget(std::log2(lowpassHz_))`
//     with a direct `lowpass_.setCutoff(lowpassHz_)` (and drop the control-step
//     glide push). SubharmonicEngine_ClickFreedom must then report detections
//     within a few hundred samples of the 40 Hz and 2000 Hz cutoff writes - a
//     Biquad whose b0 moves by three orders while z1_/z2_ still hold old-pole
//     state IS the step the 5-sigma test exists to find. Restore afterwards.
//   * SC-020, the fixture itself. Invert (a): at `f = 40` assert the Div4 tone
//     is AUDIBLE (tap RMS >= -20 dBFS, latch false). It must FAIL - the arm is
//     otherwise satisfiable by any engine that never generates anything.
// ==============================================================================

namespace {

/// The two tone indices the FR-016 arms name. Div4 is the tone that crosses the
/// backstop at `f = 48`, Div2 the one that crosses at `f = 24` (plan S5.2).
constexpr std::size_t kDiv2Index = SubharmonicEngine::index(SubharmonicEngine::Tone::Div2);
constexpr std::size_t kDiv4Index = SubharmonicEngine::index(SubharmonicEngine::Tone::Div4);

/// spec.md:883-897's ISOLATED-SUB fixture with SC-020's two documented
/// overrides: the enabled tone is `Div4` (not "one tone" generically) and its
/// level is `kMaxToneLevelDb` rather than the -20 dBFS the spectral criteria
/// use, because (b)'s "attenuated, not muted" arithmetic is stated on a +6 dB
/// tone.
///
/// `setDriveDb(kMinDriveDb)` is PINNED, not inherited (tasks.md T014): at the
/// FR-041 default of +3 dB the stage is tanh(1.4125 * 1.995) * 0.7079 - a peak
/// of ~0.703 rather than 1.995, ~9 dB of gain reduction - and the margin quoted
/// below would not be the margin measured.
///
/// Breath depth is zeroed on all three tones so `breathGain` is a LITERAL 1.0f
/// (refreshBreath: 1 + 0.45 * 0 * b). That is what makes (c)'s monotonicity
/// claim a statement about the FR-016 gate ramp alone: with the default depth
/// the three-factor product of FR-022 also carries a breath term that moves
/// every 64 samples, in either direction, and "monotonic" would be a claim
/// about the breather.
void makeInfrasonicFixture(SubharmonicEngine& engine, float fundamentalHz) {
    makeDefaults(engine);
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        engine.setToneLevelDb(t, SubharmonicEngine::kMinToneLevelDb);
        engine.setToneBreathDepth(t, 0.0f);
    }
    engine.setToneLevelDb(kDiv4Index, SubharmonicEngine::kMaxToneLevelDb);
    engine.setTrackingAmount(0.0f);  // trackGain == 1 exactly; no body needed
    engine.setWetGainDb(0.0f);
    engine.setLowpassCutoffHz(SubharmonicEngine::kMaxLowpassHz);
    engine.setDriveDb(SubharmonicEngine::kMinDriveDb);
    engine.setFundamentalHz(fundamentalHz);
}

/// Peak |x| over a buffer.
[[nodiscard]] float bufferPeak(const std::vector<float>& v) {
    float peak = 0.0f;
    for (const float s : v) {
        peak = std::max(peak, std::fabs(s));
    }
    return peak;
}

/// Render SILENCE through `engine`, capturing the FR-062 tap into `tap` (which
/// is pre-sized to the render length). The main output is discarded: SC-020
/// (a)/(b) are stated on the isolated sub, and with a silent input the tap IS
/// the whole sub contribution.
void renderSilentTap(SubharmonicEngine& engine, std::vector<float>& tap) {
    const std::size_t total = tap.size();
    const std::vector<float> silence(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> outL(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> outR(kDefaultsFixtureBlock, 0.0f);
    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t n = std::min(kDefaultsFixtureBlock, total - done);
        engine.processBlockTapped(silence.data(), silence.data(), outL.data(), outR.data(),
                                  tap.data() + done, n);
    }
}

/// A one-line summary of a detection list, for the INFO that accompanies every
/// click assertion below.
[[nodiscard]] std::string describeDetections(const std::vector<lfm::ClickDetection>& d) {
    std::string s = std::to_string(d.size()) + " detection(s)";
    const std::size_t shown = std::min<std::size_t>(d.size(), std::size_t{6});
    for (std::size_t i = 0; i < shown; ++i) {
        s += "; [" + std::to_string(i) + "] sample " + std::to_string(d[i].sampleIndex) +
             " t=" + std::to_string(d[i].timeSeconds) + " s amp=" + std::to_string(d[i].amplitude);
    }
    return s;
}

/// The detector configuration for this TU. THE SAMPLE RATE MUST BE OVERRIDDEN:
/// ClickDetectorConfig defaults to 44100 (artifact_detection.h:39) and every
/// `timeSeconds` it reports on a 48 kHz render would otherwise be wrong by
/// 8.8 %.
[[nodiscard]] lfm::ClickDetectorConfig fixtureClickConfig() {
    return lfm::ClickDetectorConfig{.sampleRate = static_cast<float>(kDefaultsFixtureSampleRate)};
}

}  // namespace

TEST_CASE("SubharmonicEngine_InfrasonicFloor", "[subharmonic_engine]") {
    // The first 2 s of every arm are discarded (spec.md:893-894): the 50 ms
    // gates and level ramps are then at target and the 18 Hz DCBlocker2 has
    // settled out of its start-up transient.
    const std::size_t settle = fixtureSamples(2.0);
    const std::size_t measured = fixtureSamples(10.0);

    SECTION("SC-020 (a): f = 40 Hz floors Div4 - the tap is silent and the latch is true") {
        SubharmonicEngine engine;
        makeInfrasonicFixture(engine, 40.0f);
        static_cast<void>(advanceOne(engine, 0, settle, 0.0));

        // NON-VACUITY, and the whole point of the arm: the tone is FLOORED, not
        // faded out. Its fader is at the TOP, the FR-023 dormancy predicate is
        // false, and the only thing that can silence it is the FR-016 backstop.
        REQUIRE(engine.getToneLevelDb(kDiv4Index) == SubharmonicEngine::kMaxToneLevelDb);
        REQUIRE_FALSE(engine.isToneDormant(kDiv4Index));
        REQUIRE(engine.isToneInfrasonicFloored(kDiv4Index));
        INFO("Div4 at f = 40 Hz is " << engine.getToneFrequencyHz(kDiv4Index) << " Hz (backstop "
                                     << SubharmonicEngine::kMinToneHz << " Hz)");
        REQUIRE(engine.getToneFrequencyHz(kDiv4Index) < SubharmonicEngine::kMinToneHz);

        std::vector<float> tap(measured, 0.0f);
        renderSilentTap(engine, tap);

        const float peak = bufferPeak(tap);
        const float ceiling = Krate::DSP::dbToGain(-80.0f);
        INFO("SC-020 (a): 10 s tap peak " << peak << ", RMS " << bufferRms(tap) << " (ceiling "
                                          << ceiling << " = -80 dBFS)");
        REQUIRE(peak <= ceiling);
        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
    }

    SECTION("SC-020 (b): f = 55 Hz - attenuated by the 18 Hz filter, not muted") {
        SubharmonicEngine engine;
        makeInfrasonicFixture(engine, 55.0f);
        static_cast<void>(advanceOne(engine, 0, settle, 0.0));

        REQUIRE_FALSE(engine.isToneInfrasonicFloored(kDiv4Index));
        // 13.75 Hz: above the 12 Hz backstop, inside the 18 Hz filter's
        // stop-band - the exact condition the "attenuated, not silent" claim is
        // about.
        const float toneHz = engine.getToneFrequencyHz(kDiv4Index);
        INFO("Div4 at f = 55 Hz is " << toneHz << " Hz");
        REQUIRE(toneHz > SubharmonicEngine::kMinToneHz);
        REQUIRE(toneHz < SubharmonicEngine::kInfrasonicFilterHz);

        std::vector<float> tap(measured, 0.0f);
        renderSilentTap(engine, tap);

        const float rms = bufferRms(tap);
        const float floorRms = Krate::DSP::dbToGain(-20.0f);
        // A +6 dB tone through the 18 Hz Bessel HP at 13.75 Hz sees ~ -7.5 dB,
        // so the expected reading is ~ -4.5 dBFS before the FR-041 shaper takes
        // its share: well over 10 dB of margin above the floor asserted here.
        INFO("SC-020 (b): 10 s tap RMS " << rms << " = " << relativeDb(rms, 1.0f)
                                         << " dBFS (floor " << floorRms << " = -20 dBFS), peak "
                                         << bufferPeak(tap));
        REQUIRE(rms >= floorRms);
        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
    }

    // ==========================================================================
    // (c) The step, in both directions. Rendered on the 64-sample control grid
    // so "the write landed at sample S" and "the control step that saw it ran at
    // sample S" are the same statement, and the <= 1.333 ms retarget latency of
    // Q5 is the only slack in the 52 ms budget.
    //
    // With breath depth zeroed and the level ramp parked, FR-022's three-factor
    // product reduces to `dbToGain(+6) * gate`, so getToneCurrentGain() reports
    // the FR-016 gate ramp scaled by a constant - which is what makes both the
    // arrival bound and the monotonicity claim statements about that ramp.
    // ==========================================================================
    SECTION("SC-020 (c): stepping f = 55 <-> 40 Hz is click-free and arrives within 52 ms") {
        SubharmonicEngine engine;
        makeInfrasonicFixture(engine, 55.0f);
        static_cast<void>(advanceOne(engine, 0, settle, 0.0));

        constexpr std::size_t kStepBlock = SubharmonicEngine::kControlChunkSamples;
        const std::size_t segment = fixtureSamples(1.0);  // 48000 = 750 * 64
        REQUIRE(segment % kStepBlock == std::size_t{0});
        const std::size_t total = 3 * segment;

        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);
        const std::vector<float> silence(kStepBlock, 0.0f);

        // One gain sample per control block: entry j is the value after the last
        // sample of block j, i.e. after sample (j + 1) * 64 - 1.
        std::vector<float> gainTrace;
        gainTrace.reserve(total / kStepBlock);
        bool flooredAfterDown = false;
        bool flooredAfterUp = true;

        for (std::size_t done = 0; done < total; done += kStepBlock) {
            if (done == segment) {
                engine.setFundamentalHz(40.0f);
            } else if (done == 2 * segment) {
                engine.setFundamentalHz(55.0f);
            }
            engine.processBlock(silence.data(), silence.data(), outL.data() + done,
                                outR.data() + done, kStepBlock);
            gainTrace.push_back(engine.getToneCurrentGain(kDiv4Index));
            if (done == segment) {
                flooredAfterDown = engine.isToneInfrasonicFloored(kDiv4Index);
            } else if (done == 2 * segment) {
                flooredAfterUp = engine.isToneInfrasonicFloored(kDiv4Index);
            }
        }

        // The latch flipped in BOTH directions, within the one control step the
        // write is visible to. Without this the arms below could be measuring a
        // ramp that never had a reason to move.
        REQUIRE(flooredAfterDown);
        REQUIRE_FALSE(flooredAfterUp);

        const std::size_t downBlock = segment / kStepBlock;  // first block after the write
        const std::size_t upBlock = 2 * segment / kStepBlock;
        // 52 ms = the 50 ms kGainRampMs fade + <= 1.333 ms control latency +
        // 1 ms measurement tolerance. downArrival/upArrival are the last trace
        // entries whose whole block lies inside that budget.
        // Integer arithmetic on purpose: 0.052 has no exact double
        // representation, and 0.052 * 48000.0 truncating to 2495 rather than
        // 2496 would silently tighten the budget by one control block.
        const std::size_t arrivalSamples =
            (std::size_t{52} * static_cast<std::size_t>(kDefaultsFixtureSampleRate)) /
            std::size_t{1000};
        const std::size_t downArrival = (segment + arrivalSamples) / kStepBlock - 1;
        const std::size_t upArrival = (2 * segment + arrivalSamples) / kStepBlock - 1;
        REQUIRE(downArrival < upBlock);
        REQUIRE(upArrival < gainTrace.size());

        const float open = Krate::DSP::dbToGain(SubharmonicEngine::kMaxToneLevelDb);
        constexpr float kArrivalTolerance = 1.0e-5f;

        // The pre-write state: parked wide open, so the step really is a full
        // 1 -> 0 traverse of the gate.
        INFO("pre-write gain " << gainTrace[downBlock - 1] << " (open " << open << ")");
        REQUIRE(std::fabs(gainTrace[downBlock - 1] - open) <= kArrivalTolerance);

        // ---- 55 -> 40: closes, monotonically, inside the budget --------------
        INFO("SC-020 (c) down: gain at +52 ms = "
             << gainTrace[downArrival] << ", at the write " << gainTrace[downBlock]
             << ", at the end of the segment " << gainTrace[upBlock - 1]);
        REQUIRE(gainTrace[downArrival] <= kArrivalTolerance);
        // Non-strict: the <= 64-sample retarget latency leaves the first entry
        // flat, and the ramp is parked on 0 for the rest of the segment. Any
        // RISE inside the segment is a defect. Scanned into one assertion
        // rather than 750 of them.
        std::size_t downViolation = 0;
        for (std::size_t j = downBlock; j < upBlock; ++j) {
            if (gainTrace[j] > gainTrace[j - 1]) {
                downViolation = j;
                break;
            }
        }
        INFO("first non-monotonic block on the way down: "
             << downViolation
             << (downViolation == 0 ? std::string{" (none)"}
                                    : (" (" + std::to_string(gainTrace[downViolation - 1]) +
                                       " -> " + std::to_string(gainTrace[downViolation]) + ")")));
        REQUIRE(downViolation == std::size_t{0});

        // ---- 40 -> 55: reopens, monotonically, inside the budget -------------
        INFO("SC-020 (c) up: gain at +52 ms = " << gainTrace[upArrival] << ", at the write "
                                                << gainTrace[upBlock] << ", at the end "
                                                << gainTrace.back());
        REQUIRE(std::fabs(gainTrace[upArrival] - open) <= kArrivalTolerance);
        std::size_t upViolation = 0;
        for (std::size_t j = upBlock; j < gainTrace.size(); ++j) {
            if (gainTrace[j] < gainTrace[j - 1]) {
                upViolation = j;
                break;
            }
        }
        INFO("first non-monotonic block on the way up: "
             << upViolation
             << (upViolation == 0 ? std::string{" (none)"}
                                  : (" (" + std::to_string(gainTrace[upViolation - 1]) + " -> " +
                                     std::to_string(gainTrace[upViolation]) + ")")));
        REQUIRE(upViolation == std::size_t{0});

        // ---- and neither step is a click -------------------------------------
        lfm::ClickDetector detector{fixtureClickConfig()};
        detector.prepare();
        const auto detections = detector.detect(outL.data(), outL.size());
        INFO("SC-020 (c): " << describeDetections(detections) << "; writes at samples " << segment
                            << " and " << 2 * segment);
        REQUIRE(detections.empty());
        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
    }
}

// ==============================================================================
// T014 - SC-008, no clicks anywhere on the control surface
// ==============================================================================
// 60 s, a steady -12 dBFS 55 Hz body, every control stepped ONE AT A TIME on the
// 64-sample control grid with 2 s between writes - long past the 50 ms gain
// ramps, the 50 ms cutoff glide and the 120 ms follower attack, so each step is
// measured against a settled engine and never against the previous step's tail.
//
// EXCLUDED BY NAME (spec.md:1132-1133), because they are declared stepped and
// are rare control events rather than audio-rate moves: `setToneWaveform` and
// `prepare()`.
//
// TONE LEVELS RETURN TO THEIR FR-020 DEFAULT after each tone's kMax/kMin
// excursion. Two reasons, both structural rather than cosmetic:
//   * leaving all three at kMinToneLevelDb would put the engine to sleep
//     (FR-023/FR-025) and every remaining arm would be measuring a passthrough;
//   * leaving all three at kMaxToneLevelDb sums to ~6.0 pre-saturation, which at
//     the FR-041 default drive is tanh(8.5) - a hard square whose zero crossings
//     are the only non-flat samples in a 512-point frame, and whose frame
//     statistics make the detector's mean + 5 sigma test a coin toss on a
//     CORRECT build. The criterion is about control steps, not about whether a
//     fully-saturated square reads as an artifact.
// Each tone therefore traverses the full fader in both directions
// (default -> kMax -> kMin -> default) with at most one tone hot at a time.
//
// The tracking arm (0 <-> 1) is asserted as specified and is recorded here as
// deliberately WEAK: the body is -12 dBFS (RMS 0.178) against the FR-035
// reference of -18 dBFS (0.126), so envNorm clamps to 1.0 and FR-032's
// (1 - a) + a * envNorm equals 1.0 at BOTH ends of the sweep. The arm proves the
// write is click-free; SC-002 is what proves the law moves at all.
// ==============================================================================

namespace {

/// "no latch check on this step".
constexpr std::size_t kNoFlipTone = SubharmonicEngine::kNumTones;

enum class ClickAction : std::uint8_t { ToneLevel, WetGain, Tracking, Cutoff, Fundamental };

struct ClickStep {
    double timeSeconds = 0.0;
    ClickAction action = ClickAction::ToneLevel;
    std::size_t tone = 0;  ///< ToneLevel only
    float value = 0.0f;
    std::size_t flipTone = kNoFlipTone;  ///< the tone whose FR-016 latch must flip
    bool flooredAfter = false;           ///< and the state it must hold afterwards
};

void applyClickStep(SubharmonicEngine& engine, const ClickStep& step) {
    switch (step.action) {
        case ClickAction::ToneLevel:
            engine.setToneLevelDb(step.tone, step.value);
            break;
        case ClickAction::WetGain:
            engine.setWetGainDb(step.value);
            break;
        case ClickAction::Tracking:
            engine.setTrackingAmount(step.value);
            break;
        case ClickAction::Cutoff:
            engine.setLowpassCutoffHz(step.value);
            break;
        case ClickAction::Fundamental:
            engine.setFundamentalHz(step.value);
            break;
    }
}

/// SC-008's schedule. Every entry is a 2 s multiple, and 2 s at 48 kHz is
/// 96000 = 1500 * 64 samples, so every write lands exactly on a control-grid
/// boundary and none of them can be split across a block.
[[nodiscard]] std::vector<ClickStep> clickSchedule() {
    const auto& defaults = SubharmonicEngine::kDefaultToneLevelDb;
    return {
        // ---- every tone level, both directions across the full fader --------
        ClickStep{.timeSeconds = 2.0, .action = ClickAction::ToneLevel, .tone = 0,
                  .value = SubharmonicEngine::kMaxToneLevelDb},
        ClickStep{.timeSeconds = 4.0, .action = ClickAction::ToneLevel, .tone = 0,
                  .value = SubharmonicEngine::kMinToneLevelDb},
        ClickStep{.timeSeconds = 6.0, .action = ClickAction::ToneLevel, .tone = 0,
                  .value = defaults[0]},
        ClickStep{.timeSeconds = 8.0, .action = ClickAction::ToneLevel, .tone = 1,
                  .value = SubharmonicEngine::kMaxToneLevelDb},
        ClickStep{.timeSeconds = 10.0, .action = ClickAction::ToneLevel, .tone = 1,
                  .value = SubharmonicEngine::kMinToneLevelDb},
        ClickStep{.timeSeconds = 12.0, .action = ClickAction::ToneLevel, .tone = 1,
                  .value = defaults[1]},
        ClickStep{.timeSeconds = 14.0, .action = ClickAction::ToneLevel, .tone = 2,
                  .value = SubharmonicEngine::kMaxToneLevelDb},
        ClickStep{.timeSeconds = 16.0, .action = ClickAction::ToneLevel, .tone = 2,
                  .value = SubharmonicEngine::kMinToneLevelDb},
        ClickStep{.timeSeconds = 18.0, .action = ClickAction::ToneLevel, .tone = 2,
                  .value = defaults[2]},
        // ---- the wet fader, the same range ----------------------------------
        ClickStep{.timeSeconds = 20.0, .action = ClickAction::WetGain, .tone = 0,
                  .value = SubharmonicEngine::kMinWetGainDb},
        ClickStep{.timeSeconds = 22.0, .action = ClickAction::WetGain, .tone = 0,
                  .value = SubharmonicEngine::kMaxWetGainDb},
        ClickStep{.timeSeconds = 24.0, .action = ClickAction::WetGain, .tone = 0,
                  .value = SubharmonicEngine::kDefaultWetGainDb},
        // ---- tracking 0 <-> 1 ------------------------------------------------
        ClickStep{.timeSeconds = 26.0, .action = ClickAction::Tracking, .tone = 0,
                  .value = 0.0f},
        ClickStep{.timeSeconds = 28.0, .action = ClickAction::Tracking, .tone = 0,
                  .value = 1.0f},
        // ---- the low-pass cutoff over its FULL range (the S5.3 glide's arm) --
        ClickStep{.timeSeconds = 30.0, .action = ClickAction::Cutoff, .tone = 0,
                  .value = SubharmonicEngine::kMinLowpassHz},
        ClickStep{.timeSeconds = 32.0, .action = ClickAction::Cutoff, .tone = 0,
                  .value = SubharmonicEngine::kMaxLowpassHz},
        ClickStep{.timeSeconds = 34.0, .action = ClickAction::Cutoff, .tone = 0,
                  .value = SubharmonicEngine::kDefaultLowpassHz},
        // ---- the fundamental across BOTH backstop boundaries -----------------
        // 50 -> 44 takes Div4 from 12.5 Hz to 11 Hz across f = 48; Div2
        // (25 -> 22) and FifthBelow (33.3 -> 29.3) stay awake throughout.
        ClickStep{.timeSeconds = 36.0, .action = ClickAction::Fundamental, .tone = 0,
                  .value = 50.0f},
        ClickStep{.timeSeconds = 38.0, .action = ClickAction::Fundamental, .tone = 0,
                  .value = 44.0f, .flipTone = kDiv4Index, .flooredAfter = true},
        ClickStep{.timeSeconds = 40.0, .action = ClickAction::Fundamental, .tone = 0,
                  .value = 50.0f, .flipTone = kDiv4Index, .flooredAfter = false},
        // 26 -> 20 takes Div2 from 13 Hz to 10 Hz across f = 24, with FifthBelow
        // (17.3 -> 13.3 Hz) awake so the render is never silent. The move to
        // 26 Hz floors Div4 (6.5 Hz) on the way, which is asserted rather than
        // left implicit.
        ClickStep{.timeSeconds = 42.0, .action = ClickAction::Fundamental, .tone = 0,
                  .value = 26.0f, .flipTone = kDiv4Index, .flooredAfter = true},
        ClickStep{.timeSeconds = 44.0, .action = ClickAction::Fundamental, .tone = 0,
                  .value = 20.0f, .flipTone = kDiv2Index, .flooredAfter = true},
        ClickStep{.timeSeconds = 46.0, .action = ClickAction::Fundamental, .tone = 0,
                  .value = 26.0f, .flipTone = kDiv2Index, .flooredAfter = false},
        ClickStep{.timeSeconds = 48.0, .action = ClickAction::Fundamental, .tone = 0,
                  .value = 55.0f, .flipTone = kDiv4Index, .flooredAfter = false},
    };
}

}  // namespace

TEST_CASE("SubharmonicEngine_ClickFreedom", "[subharmonic_engine]") {
    SubharmonicEngine engine;
    makeDefaults(engine);

    constexpr std::size_t kBlock = SubharmonicEngine::kControlChunkSamples;
    const std::size_t total = fixtureSamples(60.0);
    REQUIRE(total % kBlock == std::size_t{0});

    const std::vector<ClickStep> schedule = clickSchedule();
    // Every scheduled write must land on the control grid, or the "one write per
    // block" bookkeeping below would silently skip it.
    for (const ClickStep& step : schedule) {
        REQUIRE(fixtureSamples(step.timeSeconds) % kBlock == std::size_t{0});
        REQUIRE(fixtureSamples(step.timeSeconds) < total);
    }

    std::vector<float> out(total, 0.0f);
    std::vector<float> blockIn(kBlock, 0.0f);
    std::vector<float> blockOutR(kBlock, 0.0f);
    std::vector<float> blockTap(kBlock, 0.0f);
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / kDefaultsFixtureSampleRate;

    std::size_t nextStep = 0;
    std::size_t pendingCheck = schedule.size();  // "no post-write check pending"
    double tapPowerSum = 0.0;
    bool channelsIdentical = true;

    for (std::size_t done = 0; done < total; done += kBlock) {
        if (nextStep < schedule.size() &&
            done == fixtureSamples(schedule[nextStep].timeSeconds)) {
            const ClickStep& step = schedule[nextStep];
            if (step.flipTone < SubharmonicEngine::kNumTones) {
                // The latch must be in the OPPOSITE state BEFORE the write.
                // Without this half, a future change to kMinToneHz would leave
                // the arm asserting a state that never changed - which is
                // exactly how an earlier draft's arm became vacuous.
                INFO("step " << nextStep << " at " << step.timeSeconds << " s: tone "
                             << step.flipTone << " must be "
                             << (step.flooredAfter ? "unfloored" : "floored") << " beforehand");
                REQUIRE(engine.isToneInfrasonicFloored(step.flipTone) != step.flooredAfter);
            }
            applyClickStep(engine, step);
            pendingCheck = nextStep;
            ++nextStep;
        }

        // The body is generated from the ABSOLUTE sample index, so 60 s is ONE
        // continuous 55 Hz sine and no block boundary injects a phase step the
        // detector would (correctly) call a click.
        for (std::size_t i = 0; i < kBlock; ++i) {
            blockIn[i] = static_cast<float>(kBodyAmplitude *
                                            std::sin(omega * static_cast<double>(done + i)));
        }
        engine.processBlockTapped(blockIn.data(), blockIn.data(), out.data() + done,
                                  blockOutR.data(), blockTap.data(), kBlock);

        for (std::size_t i = 0; i < kBlock; ++i) {
            // FR-043: the SAME scalar is added to both channels, so a
            // mono-identical input must come out mono-identical. Accumulated
            // rather than REQUIREd per sample - 2.88 M assertions would dominate
            // the suite's runtime.
            channelsIdentical = channelsIdentical && (blockOutR[i] == out[done + i]);
            tapPowerSum += static_cast<double>(blockTap[i]) * static_cast<double>(blockTap[i]);
        }

        if (pendingCheck < schedule.size()) {
            const ClickStep& step = schedule[pendingCheck];
            if (step.flipTone < SubharmonicEngine::kNumTones) {
                INFO("step " << pendingCheck << " at " << step.timeSeconds << " s: tone "
                             << step.flipTone << " must be "
                             << (step.flooredAfter ? "floored" : "unfloored")
                             << " within one control step of the write");
                REQUIRE(engine.isToneInfrasonicFloored(step.flipTone) == step.flooredAfter);
            }
            pendingCheck = schedule.size();
        }
    }

    // Every scheduled write actually fired.
    REQUIRE(nextStep == schedule.size());
    REQUIRE(channelsIdentical);

    // NON-VACUITY: there IS a sub in this render. A muted engine would be
    // click-free for free.
    const auto subRms = static_cast<float>(std::sqrt(tapPowerSum / static_cast<double>(total)));
    INFO("SC-008: sub tap RMS over the whole 60 s = " << subRms << ", output peak "
                                                      << bufferPeak(out) << ", clamp engagements "
                                                      << engine.getClampEngagementCount());
    REQUIRE(subRms > 1.0e-3f);
    REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});

    lfm::ClickDetector detector{fixtureClickConfig()};
    detector.prepare();
    const auto detections = detector.detect(out.data(), out.size());
    INFO("SC-008: " << describeDetections(detections));
    REQUIRE(detections.empty());

    // NON-VACUITY OF THE DETECTOR ITSELF, on THIS material: an 8192-sample
    // window taken from the quiet tail (51 s, three seconds after the last
    // write, so nothing of the schedule is in flight) is copied, one sample is
    // displaced by 0.05 - roughly 30x the 1.8e-3 per-sample first difference of
    // the body - and the same detector must find it. Without this arm, a
    // detector that had silently stopped detecting would pass the assertion
    // above.
    const std::size_t probeStart = fixtureSamples(51.0);
    constexpr std::size_t kProbeLength = 8192;
    REQUIRE(probeStart + kProbeLength <= total);
    std::vector<float> probe(
        out.begin() + static_cast<std::ptrdiff_t>(probeStart),
        out.begin() + static_cast<std::ptrdiff_t>(probeStart + kProbeLength));
    REQUIRE(detector.detect(probe.data(), probe.size()).empty());
    probe[kProbeLength / 2] += 0.05f;
    const auto probeDetections = detector.detect(probe.data(), probe.size());
    INFO("SC-008 detector self-check: " << describeDetections(probeDetections));
    REQUIRE_FALSE(probeDetections.empty());
}

// ==============================================================================
// T015 shared helpers - the whole-render measurement fixtures (SC-006, SC-007,
// SC-011, SC-021)
// ==============================================================================

namespace {

/// Deterministic pink noise at a chosen RMS in dBFS.
///
/// Copied from `resonance_drift_network_perf_test.cpp:1334-1361`'s `PinkDrive`
/// (the source tasks.md T015 names), including its CALIBRATION pass: Kellet's
/// gains are published for a particular white-noise convention, and the realised
/// RMS of `PinkNoiseFilter::process(Xorshift32::nextFloat())` is not a round
/// number, so a private copy of the same stream is run once, the measured RMS
/// becomes the scale, and the live generator then starts again from the seed.
/// Without it "a -12 dBFS pink bed" would be a statement about an unmeasured
/// drive rather than about the level SC-006 and SC-007 are stated on.
///
/// Renamed from `PinkDrive` deliberately: this TU's copy lives in an anonymous
/// namespace (internal linkage, so no ODR issue either way), but a distinct name
/// keeps a future reader from assuming the two are kept in sync.
class PinkBed {
public:
    PinkBed(std::uint32_t seed, double sampleRate, float rmsDbfs) noexcept : rng_(seed) {
        filter_.prepare(static_cast<float>(sampleRate));

        Krate::DSP::Xorshift32 calibrationRng{seed};
        Krate::DSP::PinkNoiseFilter calibrationFilter;
        calibrationFilter.prepare(static_cast<float>(sampleRate));
        constexpr std::size_t kCalibrationSamples = 200000;  // ~4 s at 48 kHz
        double sumSquares = 0.0;
        for (std::size_t i = 0; i < kCalibrationSamples; ++i) {
            const auto v =
                static_cast<double>(calibrationFilter.process(calibrationRng.nextFloat()));
            sumSquares += v * v;
        }
        const double rms = std::sqrt(sumSquares / static_cast<double>(kCalibrationSamples));
        scale_ = (rms > 1.0e-12)
                     ? static_cast<float>(static_cast<double>(Krate::DSP::dbToGain(rmsDbfs)) / rms)
                     : 1.0f;
    }

    [[nodiscard]] float next() noexcept { return filter_.process(rng_.nextFloat()) * scale_; }

private:
    Krate::DSP::PinkNoiseFilter filter_{};
    Krate::DSP::Xorshift32 rng_;
    float scale_ = 1.0f;
};

/// Mean of a buffer, accumulated in double - the DC measurement SC-007 (c) is
/// stated on. A 30 s render at 48 kHz is 1.44 M terms and a float accumulator
/// would lose the small end of the sum long before the answer mattered.
[[nodiscard]] double bufferMean(const std::vector<float>& v) {
    if (v.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const float s : v) {
        sum += static_cast<double>(s);
    }
    return sum / static_cast<double>(v.size());
}

// ------------------------------------------------------------------------------
// SC-006 fixture
// ------------------------------------------------------------------------------

/// spec.md:1054-1058's input: a -6 dBFS 55 Hz body plus a -12 dBFS pink bed.
/// The bed is generated INDEPENDENTLY per channel - the criterion is about peak
/// behaviour, and two uncorrelated beds give the ladder a harder input than one
/// mono bed would (a mono bed would make the two channels bit-identical and the
/// true peak a single-channel statement).
constexpr double kHeadroomSeconds = 10.0;
constexpr double kHeadroomBodyAmplitude = 0.5011872336272722;  // dbToGain(-6), as a peak
constexpr float kHeadroomPinkRmsDbfs = -12.0f;
constexpr std::uint32_t kHeadroomPinkSeedL = 0x5EED0611u;
constexpr std::uint32_t kHeadroomPinkSeedR = 0x5EED0612u;

/// SC-006's true-peak ceiling, from FR-052: `kMaxPreClampMagnitude` (~2.00,
/// +6.0 dBFS) plus this input's ~1.0 peak (+0.0 dBFS) is a worst case of
/// ~+9.5 dBFS IF sub and body peaked coherently, which they do not. NOT
/// `kOutputClamp` (+12.04 dBFS): FR-054's clamp would make that outcome nearly
/// unfalsifiable, and as a TRUE-peak figure it would not even be guaranteed,
/// since the inter-sample peaks of a clipped waveform exceed the clip level.
constexpr float kHeadroomTruePeakCeilingDbTp = 9.5f;

/// spec.md:1065-1088 (b): the same render through a DEFAULT TruePeakLimiter
/// (ceiling kDefaultCeilingDb = -1.0 dB, true_peak_limiter.h:46).
///
/// SPEC AMENDED 2026-09-14 (spec.md D-16): this is now exactly what SC-006 (b)
/// says, and the three assertions below ARE the criterion. An EARLIER spec.md
/// stated the pass as "a measured true peak <= -0.9 dBTP". THAT FIGURE IS NOT
/// REACHABLE WITH THE SHIPPED LIMITER, AND NOT BECAUSE OF THIS ENGINE, which is
/// why the criterion moved rather than the code. Three figures, all measured
/// on this fixture (48 kHz, 10 s, the Economy/ZeroLatency basis the criterion
/// itself names) with a standalone probe built against the shipped headers:
///   engine render            -> limiter:  -0.069 dBTP  (sample peak -1.000 dBFS)
///   THE SAME DRY INPUT, scaled to the engine render's peak, NO ENGINE AT ALL
///                            -> limiter:  +0.030 dBTP  (0.10 dB WORSE)
///   a 1 kHz sine at +6 dBFS (the shipped limiter test's own signal)
///                            -> limiter:  -0.672 dBTP
/// TruePeakLimiter is a ZERO-LATENCY limiter with an instantaneous attack
/// (true_peak_limiter.h:9-18, :148-160). The gain it multiplies in is itself a
/// broadband signal, so while every OUTPUT SAMPLE is bounded exactly (the
/// detector folds the raw sample in, so tp >= |sample| and |out| <= ceiling),
/// the INTER-SAMPLE peaks of the product are not. The limiter's own unit test
/// pins that slack instead of the ceiling - REQUIRE(tp <= ceil + 0.06f),
/// "within ~0.5 dB of the -1 dBTP target" (true_peak_limiter_test.cpp:88-89),
/// i.e. ~-0.43 dBTP on a pure sine, already above spec.md's -0.9, and measured
/// there with a LINEAR-PHASE FIR meter. Re-measuring this render on that FIR
/// basis does not rescue the number either (-0.251 dBTP), so the measurement
/// basis is not the cause and A-4's basis stays exactly as specified.
///
/// The arm below therefore asserts the three things that ARE about this engine
/// and are falsifiable on it, keeping the spec's -0.9 figure where the shipped
/// limiter's guarantee is exact - and spec.md SC-006 (b) now says the same:
///   (1) the limited SAMPLE peak is at or under -0.9 dBFS - the exact
///       guarantee, and a statement that the render reached the limiter finite;
///   (2) the limited true peak is under a +0.5 dBTP backstop;
///   (3) the limited true peak is no worse than the SAME dry input limited at
///       the SAME level - the arm that keeps (b) a claim about this engine
///       rather than about the shipped limiter.
constexpr float kHeadroomLimitedCeilingDbFs = -0.9f;

/// (2) above. 0.57 dB over the measured -0.069, and 0.47 dB over the measured
/// dry control: a regression that made this engine's output materially harder
/// to limit than an equally loud plain signal trips (3) first.
constexpr float kHeadroomLimitedCeilingDbTp = 0.5f;

/// (3) above. The engine measured 0.099 dB BELOW its dry control, so this is
/// ~2.5x the measured separation - the render_fingerprint.h:27-28 convention
/// for a measured tolerance.
constexpr float kHeadroomLimitedVsDryMarginDb = 0.25f;

/// spec.md:1089-1092 (c): at the SHIPPED defaults the downstream limiter must
/// not be forced into pathological gain reduction.
constexpr float kHeadroomMinLimiterGainDb = -6.0f;

/// SC-006's fixture. `maxLevels` selects between the (a)/(b) worst case (all
/// three tones at kMaxToneLevelDb, wet at kMaxWetGainDb) and (c)'s shipped
/// defaults. Drive is kMaxDriveDb in BOTH arms: the plan's (c) arithmetic is
/// stated at drive 12 dB, where the unity-through pair (+12 dB in, -12 dB out,
/// subharmonic_engine.h:713-724) bounds the saturator's output at
/// dbToGain(-12) = 0.251.
void makeHeadroomFixture(SubharmonicEngine& engine, bool maxLevels) {
    makeDefaults(engine);  // f = 55, tracking at its 1.0 default, seed pushed
    engine.setDriveDb(SubharmonicEngine::kMaxDriveDb);
    if (maxLevels) {
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            engine.setToneLevelDb(t, SubharmonicEngine::kMaxToneLevelDb);
        }
        engine.setWetGainDb(SubharmonicEngine::kMaxWetGainDb);
    }
}

/// Render SC-006's input through `engine`. All three buffers are pre-sized to
/// the render length; `tap` receives the FR-062 sub tap, which is what makes the
/// non-vacuity assertion ("there IS a sub in this render") possible without a
/// second render. `processBlockTapped`'s main output is bit-identical to
/// `processBlock`'s by construction (FR-063, asserted in SC-012 (c)), so taking
/// the tap costs the measurement nothing.
/// SC-006's input, generated in full: a -6 dBFS 55 Hz body plus an independent
/// -12 dBFS pink bed per channel. Both buffers must be pre-sized to the render
/// length. Split out of renderHeadroom() so (b)'s dry control is built from the
/// SAME generator rather than from a second copy of it. The body is generated
/// from the ABSOLUTE sample index, so the whole render is one continuous 55 Hz
/// sine and the per-block loop below sees exactly what it saw before.
void fillHeadroomInput(std::vector<float>& inL, std::vector<float>& inR) {
    PinkBed pinkL{kHeadroomPinkSeedL, kDefaultsFixtureSampleRate, kHeadroomPinkRmsDbfs};
    PinkBed pinkR{kHeadroomPinkSeedR, kDefaultsFixtureSampleRate, kHeadroomPinkRmsDbfs};
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / kDefaultsFixtureSampleRate;
    for (std::size_t i = 0; i < inL.size(); ++i) {
        const auto body = static_cast<float>(
            kHeadroomBodyAmplitude * std::sin(omega * static_cast<double>(i)));
        inL[i] = body + pinkL.next();
        inR[i] = body + pinkR.next();
    }
}

void renderHeadroom(SubharmonicEngine& engine, std::vector<float>& outL,
                    std::vector<float>& outR, std::vector<float>& tap) {
    const std::size_t total = outL.size();
    std::vector<float> inL(total, 0.0f);
    std::vector<float> inR(total, 0.0f);
    fillHeadroomInput(inL, inR);

    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t count = std::min(kDefaultsFixtureBlock, total - done);
        engine.processBlockTapped(inL.data() + done, inR.data() + done, outL.data() + done,
                                  outR.data() + done, tap.data() + done, count);
    }
}

/// Limit a stereo render in place with a DEFAULT TruePeakLimiter (ceiling
/// -1 dB, release 80 ms). Chunked at the prepared block size, which is what the
/// limiter's own `processBlock` would do internally anyway
/// (true_peak_limiter.h:104-118) - done explicitly here so the chunking is
/// visible at the call site.
void applyDefaultTruePeakLimiter(std::vector<float>& l, std::vector<float>& r) {
    Krate::DSP::TruePeakLimiter limiter;
    limiter.prepare(kDefaultsFixtureSampleRate, kDefaultsFixtureBlock);
    const std::size_t total = l.size();
    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t count = std::min(kDefaultsFixtureBlock, total - done);
        limiter.processBlock(l.data() + done, r.data() + done, static_cast<int>(count));
    }
}

}  // namespace

// ==============================================================================
// SC-006 - True-peak safety with the subs at maximum (tasks.md T015)
// ==============================================================================
// (a) The FR-052 ladder holds: no clamp engagement anywhere, and the output's
//     4x-oversampled true peak stays under +9.5 dBTP.
// (b) A default TruePeakLimiter downstream brings the same render inside its
//     -1 dB ceiling: every SAMPLE at or under -0.9 dBFS (the limiter's exact
//     guarantee, and spec.md's figure), the residual inter-sample peak under a
//     +0.5 dBTP backstop, and no worse than the SAME input limited at the SAME
//     level with no engine in the path. An EARLIER spec.md asked instead for a
//     TRUE peak at or under -0.9 dBTP; that is unreachable with this shipped
//     zero-latency limiter for reasons that have nothing to do with this
//     engine - the measurements and the derivation are on
//     kHeadroomLimitedCeilingDbFs, and SC-006 (b) WAS AMENDED to match on
//     2026-09-14 (spec.md D-16). This case now asserts the criterion verbatim.
// (c) At the SHIPPED defaults that limiter is barely working - its minimum gain
//     over the render stays above -6 dB. This is the arm that makes the defaults
//     a claim rather than a guess.
//
// HOW (c) IS MEASURED, and why no getter is added to TruePeakLimiter:
//   `TruePeakLimiter`'s public surface is prepare/reset/setCeilingDb/
//   setReleaseMs/getCeilingLinear/processBlock; `currentGain_` is private
//   (true_peak_limiter.h:171-177). This phase does not touch that header and
//   will not start now. The gain is DERIVED instead: keep an unlimited copy of
//   the render, run the limiter on the copy, and read the per-sample gain off
//   the quotient `limited[i] / unlimited[i]` wherever the denominator is above
//   1e-6. The quotient is exact rather than an estimate - the limiter's last act
//   is `left[i] = inL * currentGain_; right[i] = inR * currentGain_`
//   (true_peak_limiter.h:159-160), ONE linked gain applied to both channels.
//
// FALSIFICATION (tasks.md T015; run once, restored afterwards): delete
//   `renderChunk`'s step (8) clamp and raise the tone levels past the FR-052
//   bound - (a)'s `getClampEngagementCount() == 0` and the +9.5 dBTP ceiling
//   separate a correct ladder from a broken one. The arm is NOT self-falsifying,
//   so the (c) quotient carries its own non-vacuity check instead: both channels
//   must clear the 1e-6 denominator floor on more than half their samples, or
//   the "minimum gain" would just be the initialiser.
// ==============================================================================

TEST_CASE("SubharmonicEngine_TruePeakHeadroom", "[subharmonic_engine]") {
    const std::size_t total = fixtureSamples(kHeadroomSeconds);

    SECTION("SC-006 (a)/(b): worst-case levels stay inside the FR-052 ladder") {
        SubharmonicEngine engine;
        makeHeadroomFixture(engine, /*maxLevels=*/true);

        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);
        std::vector<float> tap(total, 0.0f);
        renderHeadroom(engine, outL, outR, tap);

        // NON-VACUITY: all three tones are awake and the sub is audible. Without
        // this, a silent engine would pass every peak assertion below for free.
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE_FALSE(engine.isToneDormant(t));
            REQUIRE_FALSE(engine.isToneInfrasonicFloored(t));
        }
        const float tapRms = bufferRms(tap);
        REQUIRE(tapRms > 1.0e-2f);

        const float truePeakDb = lfm::measureTruePeakDb(outL.data(), outR.data(), total,
                                                        kDefaultsFixtureSampleRate);
        const float samplePeakDb = Krate::DSP::gainToDb(samplePeak(outL, outR));

        // TRANSCRIBED INTO THE COMPLIANCE RECORD (spec.md:1050-1064).
        WARN("SC-006 (a): true peak = "
             << truePeakDb << " dBTP (ceiling " << kHeadroomTruePeakCeilingDbTp
             << "), sample peak = " << samplePeakDb
             << " dBFS, sub tap RMS = " << Krate::DSP::gainToDb(tapRms)
             << " dBFS, clamp engagements = " << engine.getClampEngagementCount());

        REQUIRE(engine.getClampEngagementCount() == std::uint32_t{0});
        REQUIRE(truePeakDb <= kHeadroomTruePeakCeilingDbTp);
        // A true peak is never below the sample peak - measureTruePeakDb folds
        // the raw sample into the max (low_frequency_metrics.h:447-450). If this
        // fails the MEASUREMENT is broken, not the engine.
        REQUIRE(truePeakDb >= samplePeakDb - 1.0e-3f);

        std::vector<float> limitedL = outL;
        std::vector<float> limitedR = outR;
        applyDefaultTruePeakLimiter(limitedL, limitedR);
        const float limitedDb = lfm::measureTruePeakDb(limitedL.data(), limitedR.data(), total,
                                                       kDefaultsFixtureSampleRate);
        const float limitedSamplePeakDb = Krate::DSP::gainToDb(samplePeak(limitedL, limitedR));

        // THE DRY CONTROL (see kHeadroomLimitedCeilingDbFs): the SAME fixture
        // input with no engine in the path, scaled to the engine render's sample
        // peak so the limiter is asked to do the same amount of work, through
        // the SAME limiter. Without it, arm (b) would only be re-measuring the
        // shipped limiter's inter-sample slack, which is not a Vorago claim.
        std::vector<float> controlL(total, 0.0f);
        std::vector<float> controlR(total, 0.0f);
        fillHeadroomInput(controlL, controlR);
        const float dryPeak = samplePeak(controlL, controlR);
        REQUIRE(dryPeak > 1.0e-3f);
        const float controlGain = samplePeak(outL, outR) / dryPeak;
        REQUIRE(controlGain > 1.0f);  // the engine render IS the hotter signal
        for (std::size_t i = 0; i < total; ++i) {
            controlL[i] *= controlGain;
            controlR[i] *= controlGain;
        }
        applyDefaultTruePeakLimiter(controlL, controlR);
        const float controlDb = lfm::measureTruePeakDb(controlL.data(), controlR.data(), total,
                                                       kDefaultsFixtureSampleRate);

        // TRANSCRIBED INTO THE COMPLIANCE RECORD, with the deviation attached.
        WARN("SC-006 (b): after a default TruePeakLimiter - true peak = "
             << limitedDb << " dBTP (backstop " << kHeadroomLimitedCeilingDbTp
             << "), sample peak = " << limitedSamplePeakDb << " dBFS (the spec figure, "
             << kHeadroomLimitedCeilingDbFs << "), the same input at the same level WITHOUT the"
             << " engine = " << controlDb << " dBTP");

        REQUIRE(limitedSamplePeakDb <= kHeadroomLimitedCeilingDbFs);
        REQUIRE(limitedDb <= kHeadroomLimitedCeilingDbTp);
        REQUIRE(limitedDb <= controlDb + kHeadroomLimitedVsDryMarginDb);
    }

    SECTION("SC-006 (c): the shipped defaults do not force pathological limiting") {
        SubharmonicEngine engine;
        makeHeadroomFixture(engine, /*maxLevels=*/false);

        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);
        std::vector<float> tap(total, 0.0f);
        renderHeadroom(engine, outL, outR, tap);

        std::vector<float> limitedL = outL;
        std::vector<float> limitedR = outR;
        applyDefaultTruePeakLimiter(limitedL, limitedR);

        constexpr float kQuotientFloor = 1.0e-6f;
        float minGain = 1.0f;
        std::size_t countedL = 0;
        std::size_t countedR = 0;
        for (std::size_t i = 0; i < total; ++i) {
            if (std::fabs(outL[i]) > kQuotientFloor) {
                minGain = std::min(minGain, limitedL[i] / outL[i]);
                ++countedL;
            }
            if (std::fabs(outR[i]) > kQuotientFloor) {
                minGain = std::min(minGain, limitedR[i] / outR[i]);
                ++countedR;
            }
        }

        // NON-VACUITY of the quotient itself: both channels must actually carry
        // signal above the denominator floor, or `minGain` would still be its
        // initialiser and the assertion would be about nothing.
        REQUIRE(countedL > total / 2);
        REQUIRE(countedR > total / 2);

        const float minGainDb = Krate::DSP::gainToDb(minGain);
        // TRANSCRIBED INTO THE COMPLIANCE RECORD (spec.md:1089-1092).
        WARN("SC-006 (c): minimum downstream limiter gain at the shipped defaults = "
             << minGainDb << " dB (floor " << kHeadroomMinLimiterGainDb << "), measured over "
             << countedL << " L / " << countedR << " R samples");
        REQUIRE(minGain > Krate::DSP::dbToGain(kHeadroomMinLimiterGainDb));
    }
}

// ==============================================================================
// T015 - SC-007 helpers
// ==============================================================================

namespace {

/// spec.md:1093-1113's input: a COMMON 55 Hz body at the fixture's -12 dBFS plus
/// an INDEPENDENT pink bed per channel. The decorrelation is the point - a
/// mono-identical input would make (a2)'s correlation comparison 1.0 against 1.0
/// and it could not fail.
constexpr double kMonoSeconds = 30.0;
constexpr float kMonoPinkRmsDbfs = -12.0f;
constexpr std::uint32_t kMonoPinkSeedL = 0x5EED0621u;
constexpr std::uint32_t kMonoPinkSeedR = 0x5EED0622u;

/// (b)'s analysis frame: 65 536 points at 48 kHz is 1.365 s and 0.7324 Hz bins,
/// which puts ~273 bins under the 200 Hz band edge - ample resolution for an
/// energy RATIO between three signals analysed with the identical window.
constexpr std::size_t kMonoFftSize = 65536;
constexpr double kMonoWindowStartSeconds = 10.0;  // well clear of every 50 ms ramp
constexpr float kMonoBandEdgeHz = 200.0f;

/// (a): side energy relative to mid energy, as a POWER ratio in dB.
constexpr double kMonoSideToMidCeilingDb = -100.0;

/// (b): the fraction of the sub contribution's sub-band energy the mono sum must
/// retain.
constexpr double kMonoMidRetentionFraction = 0.99;

/// (c): the DC this component itself may contribute, per channel. See the long
/// comment at the (c) arm for why it is asserted on the SUB CONTRIBUTION and not
/// on the raw output.
constexpr double kMonoDcCeiling = 1.0e-4;

void fillMonoInput(std::vector<float>& inL, std::vector<float>& inR) {
    PinkBed pinkL{kMonoPinkSeedL, kDefaultsFixtureSampleRate, kMonoPinkRmsDbfs};
    PinkBed pinkR{kMonoPinkSeedR, kDefaultsFixtureSampleRate, kMonoPinkRmsDbfs};
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / kDefaultsFixtureSampleRate;
    for (std::size_t i = 0; i < inL.size(); ++i) {
        const auto body =
            static_cast<float>(kBodyAmplitude * std::sin(omega * static_cast<double>(i)));
        inL[i] = body + pinkL.next();
        inR[i] = body + pinkR.next();
    }
}

void renderMono(SubharmonicEngine& engine, const std::vector<float>& inL,
                const std::vector<float>& inR, std::vector<float>& outL,
                std::vector<float>& outR) {
    const std::size_t total = inL.size();
    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t count = std::min(kDefaultsFixtureBlock, total - done);
        engine.processBlock(inL.data() + done, inR.data() + done, outL.data() + done,
                            outR.data() + done, count);
    }
}

/// Hann-windowed energy of `x[0..n)` below `edgeHz`, summed with
/// `spectral_analysis.h:207 detail::sumBinPower` - the helper SC-007 (b) names.
/// `sumBinPower` returns the ROOT of the summed squared magnitudes, so the value
/// is squared back into an energy here, which is the quantity (b)'s ratio is
/// stated on.
///
/// One FFT and one window are kept per size in function-local statics: three
/// call sites share the frame, and a fresh 65 536-point FFT per call would
/// allocate ~1.3 MB every time.
///
/// @return the band energy, or a negative sentinel if the frame cannot be
///         analysed (a size the FFT refuses).
[[nodiscard]] double lowBandEnergy(const float* x, std::size_t n, double sampleRate,
                                   float edgeHz) {
    static Krate::DSP::FFT fft;
    static std::size_t preparedSize = 0;
    if (preparedSize != n) {
        fft.prepare(n);
        preparedSize = fft.isPrepared() ? n : 0;
    }
    if (!fft.isPrepared()) {
        return -1.0;
    }

    static std::vector<float> window;
    if (window.size() != n) {
        window.assign(n, 0.0f);
        Krate::DSP::Window::generateHann(window.data(), n);
    }

    std::vector<float> windowed(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        windowed[i] = x[i] * window[i];
    }

    const std::size_t numBins = n / 2 + 1;
    std::vector<Krate::DSP::Complex> bins(numBins);
    fft.forward(windowed.data(), bins.data());

    const std::size_t edgeBin = std::min(
        numBins - 1,
        Krate::DSP::TestUtils::frequencyToBin(edgeHz, static_cast<float>(sampleRate), n));
    std::vector<std::size_t> indices;
    indices.reserve(edgeBin + 1);
    for (std::size_t k = 0; k <= edgeBin; ++k) {
        indices.push_back(k);
    }

    const float rootPower = Krate::DSP::TestUtils::detail::sumBinPower(bins.data(), indices);
    return static_cast<double>(rootPower) * static_cast<double>(rootPower);
}

}  // namespace

// ==============================================================================
// SC-007 - Mono compatibility (tasks.md T015)
// ==============================================================================
// FR-043 adds the SAME scalar to both channels, so the sub contribution
// `d = out - in` is mono by construction. Every arm below is a different way of
// failing that claim:
//
// (a)  side energy of `d` <= -100 dB relative to its mid energy. An L/R
//      CORRELATION of `d` is deliberately NOT used: it is exactly 1.0 for any
//      implementation that satisfies FR-043 including a broken one, so it could
//      never fail (spec.md:1097-1103). The mid/side form catches an accidental
//      per-channel chain state, a per-channel gain ramp, or an FR-054 clamp that
//      engages on one channel and not the other.
// (a2) the FULL-output L/R correlation with the subs active is not lower than
//      the same render with the wet fader at kMinWetGainDb: adding a common mono
//      signal can only raise correlation, so any drop is a per-channel
//      divergence.
// (b)  the mono sum 0.5*(dL + dR) retains >= 99 % of `d`'s < 200 Hz energy - no
//      cancellation in the band the subs live in.
// (c)  the component contributes <= 1e-4 of DC per channel over 30 s.
//
// FALSIFICATION (tasks.md T015; run once, restored afterwards): in `renderChunk`
//   step (9), give one channel its own offset -
//       outL[i] = xl + add + 0.001f;   outR[i] = xr + add;
//   - and (a) must fail. At the measured mid RMS of the default sub (~0.07) a
//   0.001 per-sample side term is a side/mid POWER ratio of ~-43 dB, i.e. ~57 dB
//   above the -100 dB ceiling, so the arm fails unambiguously. (c) fails on the
//   same mutation, which is the cross-check that the two arms are not measuring
//   the same thing by accident: (a) is blind to a symmetric offset and (c) is
//   blind to an antisymmetric one.
// ==============================================================================

TEST_CASE("SubharmonicEngine_MonoCompatibility", "[subharmonic_engine]") {
    const std::size_t total = fixtureSamples(kMonoSeconds);
    const std::size_t windowStart = fixtureSamples(kMonoWindowStartSeconds);
    REQUIRE(windowStart + kMonoFftSize <= total);

    std::vector<float> inL(total, 0.0f);
    std::vector<float> inR(total, 0.0f);
    fillMonoInput(inL, inR);

    std::vector<float> outL(total, 0.0f);
    std::vector<float> outR(total, 0.0f);

    SubharmonicEngine engine;
    makeDefaults(engine);  // every shipped default, tone levels included
    renderMono(engine, inL, inR, outL, outR);

    // ---------------------------------------------------------------------
    // The sub contribution, and its mid/side decomposition. Streamed in one
    // pass: only the 65 536-sample analysis window is materialised.
    // ---------------------------------------------------------------------
    std::vector<float> windowDl(kMonoFftSize, 0.0f);
    std::vector<float> windowDr(kMonoFftSize, 0.0f);
    std::vector<float> windowMid(kMonoFftSize, 0.0f);

    double midPower = 0.0;
    double sidePower = 0.0;
    double sumDl = 0.0;
    double sumDr = 0.0;
    for (std::size_t i = 0; i < total; ++i) {
        const double dl = static_cast<double>(outL[i]) - static_cast<double>(inL[i]);
        const double dr = static_cast<double>(outR[i]) - static_cast<double>(inR[i]);
        const double mid = 0.5 * (dl + dr);
        const double side = 0.5 * (dl - dr);
        midPower += mid * mid;
        sidePower += side * side;
        sumDl += dl;
        sumDr += dr;
        if (i >= windowStart && i < windowStart + kMonoFftSize) {
            const std::size_t k = i - windowStart;
            windowDl[k] = static_cast<float>(dl);
            windowDr[k] = static_cast<float>(dr);
            windowMid[k] = static_cast<float>(mid);
        }
    }
    const auto sampleCount = static_cast<double>(total);
    const double dcL = sumDl / sampleCount;
    const double dcR = sumDr / sampleCount;

    // NON-VACUITY: there IS a sub contribution. Every ratio below is a statement
    // about it, and all of them are satisfiable by silence.
    REQUIRE(midPower > 0.0);
    const double midRms = std::sqrt(midPower / sampleCount);
    REQUIRE(midRms > 1.0e-3);

    // ---------------------------------------------------------------------
    // (a) side energy <= -100 dB relative to mid energy
    // ---------------------------------------------------------------------
    // Stated as a POWER ratio (10*log10), which is what "energy ... relative to"
    // means. The floor keeps an exactly-zero side term - the outcome a
    // bit-identical pair of channels would give - from producing -inf here; a
    // zero side energy passes, which is correct.
    const double sideToMidDb =
        10.0 * std::log10(std::max(sidePower, 1.0e-300) / std::max(midPower, 1.0e-300));
    WARN("SC-007 (a): side/mid energy of the sub contribution = "
         << sideToMidDb << " dB (ceiling " << kMonoSideToMidCeilingDb
         << "), mid RMS = " << midRms);
    REQUIRE(sideToMidDb <= kMonoSideToMidCeilingDb);

    // ---------------------------------------------------------------------
    // (a2) the subs do not lower the full-output L/R correlation
    // ---------------------------------------------------------------------
    const float correlationActive = lfm::calculateCorrelation(outL.data(), outR.data(), total);

    // The muted reference re-uses the SAME input buffers and the SAME output
    // buffers: `outL`/`outR` are not needed again after this point, and a 30 s
    // stereo render is 11.5 MB that does not need a second copy.
    {
        SubharmonicEngine muted;
        makeDefaults(muted);
        muted.setWetGainDb(SubharmonicEngine::kMinWetGainDb);
        renderMono(muted, inL, inR, outL, outR);
    }
    const float correlationMuted = lfm::calculateCorrelation(outL.data(), outR.data(), total);

    WARN("SC-007 (a2): L/R correlation active = " << correlationActive
         << ", muted = " << correlationMuted);
    // NON-VACUITY: the input really is decorrelated. At a muted correlation of
    // 1.0 the comparison would be 1.0 >= 1.0 and could not fail.
    REQUIRE(correlationMuted < 0.99f);
    REQUIRE(correlationActive >= correlationMuted - 1.0e-6f);

    // ---------------------------------------------------------------------
    // (b) the mono sum keeps >= 99 % of the sub-band energy
    // ---------------------------------------------------------------------
    const double energyMid = lowBandEnergy(windowMid.data(), kMonoFftSize,
                                           kDefaultsFixtureSampleRate, kMonoBandEdgeHz);
    const double energyL = lowBandEnergy(windowDl.data(), kMonoFftSize,
                                         kDefaultsFixtureSampleRate, kMonoBandEdgeHz);
    const double energyR = lowBandEnergy(windowDr.data(), kMonoFftSize,
                                         kDefaultsFixtureSampleRate, kMonoBandEdgeHz);
    REQUIRE(energyMid >= 0.0);  // the frame was analysable at all
    REQUIRE(energyL >= 0.0);
    REQUIRE(energyR >= 0.0);
    const double energyMean = 0.5 * (energyL + energyR);
    REQUIRE(energyMean > 0.0);
    const double retention = energyMid / energyMean;
    WARN("SC-007 (b): mono-sum retention of the < " << kMonoBandEdgeHz << " Hz sub energy = "
         << retention << " (floor " << kMonoMidRetentionFraction << ")");
    REQUIRE(retention >= kMonoMidRetentionFraction);

    // ---------------------------------------------------------------------
    // (c) DC
    // ---------------------------------------------------------------------
    // MEASURED ON THE SUB CONTRIBUTION, NOT ON THE RAW OUTPUT - and the reason is
    // arithmetic, not convenience.
    //
    // spec.md:1112-1113 states (c) as "the DC offset of each output channel over
    // a 30 s render is <= 1e-4", attributed to "what FR-042's blocker and D-1's
    // symmetric saturator exist to deliver". But FR-050 / Q6 make the dry path
    // bit-transparent: `out = in + sub`, so DC(out) = DC(in) + DC(sub), and the
    // raw-output form is a statement about the FIXTURE's DC as much as about the
    // component. This fixture's DC is not negligible: pink noise is 1/f, its
    // running mean is a slow random walk, and a -12 dBFS Kellet bed at 48 kHz was
    // simulated over 30 s for six seeds this session and measured means of
    // -6.1e-3, -3.3e-3, -5.9e-3, -7.8e-3, -2.7e-3 and +7.8e-3 - 27x to 78x above
    // the 1e-4 ceiling, before the engine contributes anything at all. Asserting
    // 1e-4 on the raw output would therefore FAIL a perfectly correct build, for
    // a reason the component is contractually forbidden from fixing (high-passing
    // a host's audio behind its back is exactly what FR-050 rules out).
    //
    // `DC(out) - DC(in)` IS `DC(d)`, so what is asserted here is the criterion's
    // own number, 1e-4, applied to the only part of the output this component
    // owns. That is strictly STRONGER than the spec sentence, not weaker: the
    // fixture's own DC can no longer mask a real offset nor manufacture a
    // failure. The input DCs and the muted-render output DCs are transcribed
    // alongside so the compliance record carries all of the numbers.
    //
    // NOTE FOR THE COMPLIANCE PASS: this is a deviation from the LETTER of
    // SC-007 (c) and must be recorded as one.
    const double outDcL = bufferMean(outL);  // NB: outL/outR now hold the MUTED render
    const double outDcR = bufferMean(outR);
    const double inDcL = bufferMean(inL);
    const double inDcR = bufferMean(inR);
    WARN("SC-007 (c): sub-contribution DC L = " << dcL << ", R = " << dcR << " (ceiling "
         << kMonoDcCeiling << "); fixture input DC L = " << inDcL << ", R = " << inDcR
         << "; muted-render output DC L = " << outDcL << ", R = " << outDcR);
    REQUIRE(std::fabs(dcL) <= kMonoDcCeiling);
    REQUIRE(std::fabs(dcR) <= kMonoDcCeiling);
}

// ==============================================================================
// T015 - SC-011 helpers
// ==============================================================================

namespace {

/// spec.md:1163-1173. 120 s is the horizon the criterion names; 60 s is where
/// the breath values are probed, and that choice is arithmetic: the three
/// default breathers run at 0.037 / 0.023 / 0.014 Hz (27 / 43 / 71 s per cycle),
/// so at 60 s the first two have wrapped at least once and drawn fresh cycle
/// jitter while the third may not have - which is precisely why the criterion
/// says "two of the three".
constexpr double kDeterminismSeconds = 120.0;
constexpr double kDeterminismProbeSeconds = 60.0;
constexpr std::uint32_t kDeterminismSeedA = 0x5EED0631u;
constexpr std::uint32_t kDeterminismSeedB = 0x5EED06FFu;

/// The absolute breath-value separation two of the three tones must show at the
/// 60 s probe under a different seed. NEVER lowered to make the arm pass, and
/// the 60 s horizon is never extended: if the arm misses, the pre-authorised
/// lever is to raise `kDefaultBreathIrregularity` (plan R-3), because a miss
/// means the seed is doing less work than the design intends.
constexpr float kDeterminismBreathSeparation = 0.01f;

struct DeterminismRun {
    Krate::DSP::TestUtils::RenderFingerprint fingerprint;
    std::array<float, SubharmonicEngine::kNumTones> breathAtProbe{};
    float rms = 0.0f;
};

/// One 120 s render of the `makeDefaults` body through a freshly prepared,
/// freshly seeded instance, reduced to a fingerprint plus the three breath
/// values at the probe instant.
///
/// Only the LEFT channel is fingerprinted, and that is exact rather than a
/// shortcut: the input is mono-identical and FR-043 adds the same scalar to both
/// channels, so `outR[i] == outL[i]` at every sample - the property SC-008
/// already asserts sample by sample on this same body.
///
/// `scratch` is passed in and reused across the three runs: 120 s at 48 kHz is
/// 23 MB, three simultaneous copies would be 69 MB, and `compareFingerprints`
/// consumes fingerprints rather than buffers, so nothing needs two of them
/// alive at once.
[[nodiscard]] DeterminismRun runDeterminism(std::uint32_t seed, std::size_t total,
                                            std::size_t probeIndex,
                                            std::vector<float>& scratch) {
    SubharmonicEngine engine;
    engine.prepare(kDefaultsFixtureSampleRate,
                   SubharmonicEngine::PrepareConfig{.maxBlockSamples = kDefaultsFixtureBlock});
    engine.setSeed(seed);

    scratch.assign(total, 0.0f);
    std::vector<float> in(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> discardR(kDefaultsFixtureBlock, 0.0f);
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / kDefaultsFixtureSampleRate;

    DeterminismRun run;
    bool probed = false;
    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t count = std::min(kDefaultsFixtureBlock, total - done);
        if (!probed && done >= probeIndex) {
            for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
                run.breathAtProbe[t] = engine.getToneBreathValue(t);
            }
            probed = true;
        }
        for (std::size_t i = 0; i < count; ++i) {
            in[i] = static_cast<float>(kBodyAmplitude *
                                       std::sin(omega * static_cast<double>(done + i)));
        }
        engine.processBlock(in.data(), in.data(), scratch.data() + done, discardR.data(),
                            count);
    }
    if (!probed) {
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            run.breathAtProbe[t] = engine.getToneBreathValue(t);
        }
    }

    run.fingerprint = Krate::DSP::TestUtils::fingerprintRender(scratch);
    run.rms = static_cast<float>(run.fingerprint.rms);
    return run;
}

}  // namespace

// ==============================================================================
// SC-011 - Seed determinism (tasks.md T015)
// ==============================================================================
// (a) Two instances prepared identically, given the SAME seed and the same
//     setter sequence, render identically over 120 s within
//     render_fingerprint.h's measured tolerances. No bit-exact golden is used
//     anywhere: both operands are rendered in this process.
// (b) Two instances with DIFFERENT seeds diverge - the same comparison must
//     FAIL - and at t = 60 s at least two of the three tones' breath values
//     differ by more than 0.01 in absolute value.
//
// (b) IS ITS OWN FALSIFICATION (tasks.md T015): it asserts that the comparison
//   fails. It is only reachable because applyDefaults() writes
//   kDefaultBreathIrregularity = 0.25 (S14 C-5) - at BreathingModulator's shipped
//   irregularity of 0 the RNG is never drawn, the FR-070 seed is inert, and two
//   seeds render BIT-IDENTICALLY. Deleting that one line in applyDefaults() is
//   therefore the second, sharper falsification: (a) still passes and (b) fails
//   immediately.
//
// getToneBreathValue is probed rather than getToneCurrentGain because the latter
// is the FR-022 composite `levelGain * breathGain * gate` and cannot separate a
// breath difference from a level or gate difference (plan S8).
// ==============================================================================

TEST_CASE("SubharmonicEngine_Determinism", "[subharmonic_engine]") {
    const std::size_t total = fixtureSamples(kDeterminismSeconds);
    const std::size_t probe = fixtureSamples(kDeterminismProbeSeconds);
    REQUIRE(probe < total);

    std::vector<float> scratch;
    const DeterminismRun first = runDeterminism(kDeterminismSeedA, total, probe, scratch);
    const DeterminismRun second = runDeterminism(kDeterminismSeedA, total, probe, scratch);
    const DeterminismRun other = runDeterminism(kDeterminismSeedB, total, probe, scratch);

    // NON-VACUITY: the render is not silence. Two silent renders would agree
    // perfectly and (a) would prove nothing.
    REQUIRE(first.rms > 1.0e-3f);

    // ---- (a) the same seed renders the same audio ------------------------
    const auto same =
        Krate::DSP::TestUtils::compareFingerprints(second.fingerprint, first.fingerprint);
    INFO("SC-011 (a): worstMetric=" << same.worstMetricRelativeError
                                    << " worstSample=" << same.worstSampleError << " ("
                                    << same.detail << ")");
    REQUIRE(same.withinTolerance());
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        INFO("tone " << t);
        REQUIRE(first.breathAtProbe[t] == second.breathAtProbe[t]);
    }

    // ---- (b) a different seed renders different audio --------------------
    const auto differing =
        Krate::DSP::TestUtils::compareFingerprints(other.fingerprint, first.fingerprint);
    std::size_t movedTones = 0;
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        if (std::fabs(first.breathAtProbe[t] - other.breathAtProbe[t]) >
            kDeterminismBreathSeparation) {
            ++movedTones;
        }
    }
    WARN("SC-011 (b): seed " << kDeterminismSeedB << " vs " << kDeterminismSeedA
         << " -> worstMetric=" << differing.worstMetricRelativeError
         << " worstSample=" << differing.worstSampleError << "; breath separation at "
         << kDeterminismProbeSeconds << " s = "
         << std::fabs(first.breathAtProbe[0] - other.breathAtProbe[0]) << " / "
         << std::fabs(first.breathAtProbe[1] - other.breathAtProbe[1]) << " / "
         << std::fabs(first.breathAtProbe[2] - other.breathAtProbe[2]) << " (" << movedTones
         << " of 3 above " << kDeterminismBreathSeparation << ")");
    REQUIRE_FALSE(differing.withinTolerance());
    REQUIRE(movedTones >= std::size_t{2});
}

// ==============================================================================
// T015 - SC-021 helpers
// ==============================================================================

namespace {

/// spec.md:1279-1292. 10 s at the shipped defaults, the first 2 s discarded so
/// every 50 ms ramp and the 18 Hz Bessel high-pass have settled, and the two RMS
/// figures taken over the FINAL 5 s - i.e. the window [5 s, 10 s), which
/// discards more than the 2 s the criterion requires.
constexpr double kRatioSeconds = 10.0;
constexpr double kRatioWindowStartSeconds = 5.0;

/// (a)'s window. The plan's arithmetic predicts ~-7.7 dB (tone gains
/// 0.1259 / 0.0631 / 0.0316 -> the 120 Hz low-pass is ~unity at 13-37 Hz -> the
/// tanh at drive 3 dB is ~x0.97 -> the 18 Hz Bessel high-pass is
/// 0.788 / 0.42 / 0.878 at 27.5 / 13.75 / 36.67 Hz -> trackGain = 1 because
/// envNorm clamps), i.e. 1.7 dB of margin at the tight end. NEITHER BOUND MOVES
/// to make a figure fit.
constexpr float kRatioFloorDb = -12.0f;
constexpr float kRatioCeilingDb = -6.0f;

struct RatioMeasurement {
    float bodyRms = 0.0f;
    float subRms = 0.0f;
    float ratioDb = 0.0f;
};

/// Render the SC-021 body through `engine` and measure the body RMS (the input)
/// and the isolated sub RMS (the FR-062 tap) over the final 5 s.
///
/// The tap is the right operand rather than `out - in`: it is the sub
/// contribution PRE-wet-gain and PRE-clamp (renderChunk step (7)), which is what
/// "the isolated sub RMS" means, and at the default wet gain of 0 dB the two
/// agree anyway - so measuring the tap keeps the figure meaningful if a later
/// phase ever changes kDefaultWetGainDb.
///
/// Streamed: nothing larger than one block plus the two accumulators is
/// materialised.
[[nodiscard]] RatioMeasurement measureSubToBodyRatio(SubharmonicEngine& engine) {
    const std::size_t total = fixtureSamples(kRatioSeconds);
    const std::size_t windowStart = fixtureSamples(kRatioWindowStartSeconds);

    std::vector<float> in(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> outL(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> outR(kDefaultsFixtureBlock, 0.0f);
    std::vector<float> tap(kDefaultsFixtureBlock, 0.0f);
    const double omega = kOutputSelfCheckTwoPi * kBodyHz / kDefaultsFixtureSampleRate;

    double bodySquares = 0.0;
    double subSquares = 0.0;
    std::size_t counted = 0;

    for (std::size_t done = 0; done < total; done += kDefaultsFixtureBlock) {
        const std::size_t count = std::min(kDefaultsFixtureBlock, total - done);
        for (std::size_t i = 0; i < count; ++i) {
            in[i] = static_cast<float>(kBodyAmplitude *
                                       std::sin(omega * static_cast<double>(done + i)));
        }
        engine.processBlockTapped(in.data(), in.data(), outL.data(), outR.data(), tap.data(),
                                  count);
        for (std::size_t i = 0; i < count; ++i) {
            if (done + i < windowStart) {
                continue;
            }
            bodySquares += static_cast<double>(in[i]) * static_cast<double>(in[i]);
            subSquares += static_cast<double>(tap[i]) * static_cast<double>(tap[i]);
            ++counted;
        }
    }

    RatioMeasurement m;
    if (counted == 0) {
        return m;
    }
    const auto denominator = static_cast<double>(counted);
    m.bodyRms = static_cast<float>(std::sqrt(bodySquares / denominator));
    m.subRms = static_cast<float>(std::sqrt(subSquares / denominator));
    m.ratioDb = relativeDb(m.subRms, m.bodyRms);
    return m;
}

}  // namespace

// ==============================================================================
// SC-021 - The default sub-to-body level ratio is pinned (tasks.md T015)
// ==============================================================================
// Q3's ruling lowered the FR-020 tone defaults by ~12 dB, to -18 / -24 / -30 dB,
// so the default add sits ~9 dB under a -12 dBFS body. (a) pins that with a
// measured ratio in [-12, -6] dB.
//
// (b) IS THE MUTATION, AND IT IS WRITTEN INTO THE CASE (tasks.md T015): the same
// measurement with the pre-Q3 defaults -6 / -12 / -18 dB restored must land ABOVE
// 0 dB - the sub louder than the body. Without it, (a) would be a window that
// happens to contain the shipped numbers; with it, (a) is a window that
// demonstrably EXCLUDES the defaults Q3 rejected. The two tone sets differ by
// exactly 12 dB per tone, so the mutation is a pure level move: everything else
// in the chain is identical, and the ~+4.5 dB it lands on (rather than the naive
// -7.7 + 12 = +4.3) is the tanh beginning to compress the louder sum.
// ==============================================================================

TEST_CASE("SubharmonicEngine_DefaultSubToBodyRatio", "[subharmonic_engine]") {
    // ---- (a) the shipped defaults ---------------------------------------
    SubharmonicEngine engine;
    makeDefaults(engine);

    // The fixture precondition every "all three tones awake" criterion rests on
    // (T009): at f = 55 the tones are 27.5 / 13.75 / 36.67 Hz and none is
    // backstop-floored. A floored tone would make the ratio a measurement of the
    // FR-016 gate rather than of the FR-020 defaults. The default read-backs
    // beside it are what make "left entirely at its shipped defaults" an
    // assertion instead of a comment.
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        INFO("tone " << t);
        REQUIRE_FALSE(engine.isToneInfrasonicFloored(t));
        REQUIRE_FALSE(engine.isToneDormant(t));
        REQUIRE(engine.getToneLevelDb(t) == SubharmonicEngine::kDefaultToneLevelDb[t]);
    }
    REQUIRE(engine.getWetGainDb() == SubharmonicEngine::kDefaultWetGainDb);
    REQUIRE(engine.getDriveDb() == SubharmonicEngine::kDefaultDriveDb);
    REQUIRE(engine.getTrackingAmount() == SubharmonicEngine::kDefaultTrackingAmount);
    REQUIRE(engine.getTrackReferenceDb() == SubharmonicEngine::kDefaultTrackReferenceDb);
    REQUIRE(engine.getFundamentalHz() == SubharmonicEngine::kDefaultFundamentalHz);

    const RatioMeasurement shipped = measureSubToBodyRatio(engine);
    // TRANSCRIBED INTO THE COMPLIANCE RECORD (spec.md:1286-1287).
    WARN("SC-021 (a): body RMS = " << Krate::DSP::gainToDb(shipped.bodyRms)
         << " dBFS, sub tap RMS = " << Krate::DSP::gainToDb(shipped.subRms)
         << " dBFS, ratio = " << shipped.ratioDb << " dB (window [" << kRatioFloorDb << ", "
         << kRatioCeilingDb << "])");

    // NON-VACUITY: both operands carry signal. A silent tap would report the
    // relativeDb floor and a silent BODY would make the ratio meaningless in the
    // other direction.
    REQUIRE(shipped.bodyRms > 1.0e-3f);
    REQUIRE(shipped.subRms > 1.0e-4f);
    REQUIRE(shipped.ratioDb >= kRatioFloorDb);
    REQUIRE(shipped.ratioDb <= kRatioCeilingDb);

    // ---- (b) the mutation arm: the pre-Q3 defaults -----------------------
    SubharmonicEngine mutated;
    makeDefaults(mutated);
    mutated.setToneLevelDb(0, -6.0f);
    mutated.setToneLevelDb(1, -12.0f);
    mutated.setToneLevelDb(2, -18.0f);

    const RatioMeasurement preQ3 = measureSubToBodyRatio(mutated);
    WARN("SC-021 (b) mutation, pre-Q3 defaults -6 / -12 / -18 dB: ratio = "
         << preQ3.ratioDb
         << " dB (must be above 0 dB, i.e. the sub louder than the body; predicted ~+4.5)");
    REQUIRE(preQ3.ratioDb > 0.0f);
    // And the window the shipped defaults pass must REJECT the mutated set -
    // that is the whole point of the arm.
    REQUIRE(preQ3.ratioDb > kRatioCeilingDb);
}
