// ==============================================================================
// Oversampling Factor Switch Tests (User Story 4)
// ==============================================================================
// A factor change is a crossfade between two lanes (audit D-034, follow-up 6, user
// ruling 2026-11-09): the lane at the old factor keeps running while a second lane,
// its own shapers and oversamplers cleared, runs at the new factor, and the band
// output moves linearly from the first to the second over kFactorCrossfadeMs.
// (T1.07's 5 + 5 ms output dip replaced the original crossfade, which ran the same
// engines twice per sample; the dip read as a 1.8 dB level hole in a morph sweep.)
//
// Reference: specs/009-intelligent-oversampling/spec.md
// Tasks: T11.058, T11.059, T11.061, T11.062
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/band_processor.h"
#include "dsp/distortion_types.h"
#include "dsp/morph_node.h"
#include "test_helpers/artifact_detection.h"

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace Disrumpo;

namespace {

constexpr double kSampleRate = 44100.0;

void fillSine(float* left, float* right, size_t n, size_t offset, float amp = 0.3f) {
    for (size_t i = 0; i < n; ++i) {
        const float v = amp * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f *
                                       static_cast<float>(offset + i) / static_cast<float>(kSampleRate));
        left[i] = v;
        right[i] = v;
    }
}

std::unique_ptr<BandProcessor> makeLegacyBand(DistortionType type, float drive, size_t maxBlock) {
    auto bp = std::make_unique<BandProcessor>();
    bp->prepare(kSampleRate, maxBlock);
    bp->setDistortionType(type);
    DistortionCommonParams params;
    params.drive = drive;
    params.mix = 1.0f;
    params.toneHz = 4000.0f;
    bp->setDistortionCommonParams(params);
    return bp;
}

double maxStep(const std::vector<float>& x, size_t from, size_t to) {
    double m = 0.0;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, x.size()); ++i) {
        m = std::max(m, std::fabs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1])));
    }
    return m;
}

} // namespace

// =============================================================================
// Before any audio the selected factor applies at once
// =============================================================================

TEST_CASE("BandProcessor: factor selected before any audio applies without a switch",
          "[oversampling][crossfade]") {
    auto bp = makeLegacyBand(DistortionType::HardClip, 0.5f, 64);
    CHECK(bp->getOversampleFactor() == 4);
    CHECK(bp->getActiveOversampleFactor() == 4);
    CHECK_FALSE(bp->isOversampleTransitioning());
}

// =============================================================================
// T11.058: switch duration = one kFactorCrossfadeMs crossfade
// =============================================================================

TEST_CASE("BandProcessor: factor switch completes in one 10 ms crossfade",
          "[oversampling][crossfade][SC-005]") {
    constexpr size_t kBlockSize = 64;
    auto bp = makeLegacyBand(DistortionType::SoftClip, 0.5f, kBlockSize);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    fillSine(left.data(), right.data(), kBlockSize, 0);
    bp->processBlock(left.data(), right.data(), kBlockSize);
    CHECK(bp->getOversampleFactor() == 2);
    CHECK_FALSE(bp->isOversampleTransitioning());

    // 2x -> 4x: selected at once, the crossfade to the 4x lane starts with the next block
    bp->setDistortionType(DistortionType::HardClip);
    CHECK(bp->getOversampleFactor() == 4);
    CHECK(bp->getActiveOversampleFactor() == 2);
    CHECK(bp->isOversampleTransitioning());

    const auto rampSamples = static_cast<size_t>(std::lround(
        static_cast<double>(BandProcessor::kFactorCrossfadeMs) * 0.001 * kSampleRate));
    size_t totalProcessed = 0;
    while (bp->isOversampleTransitioning() && totalProcessed < rampSamples * 3) {
        fillSine(left.data(), right.data(), kBlockSize, kBlockSize + totalProcessed);
        bp->processBlock(left.data(), right.data(), kBlockSize);
        totalProcessed += kBlockSize;
    }

    INFO("crossfade samples " << rampSamples << ", processed " << totalProcessed);
    CHECK_FALSE(bp->isOversampleTransitioning());
    CHECK(bp->getActiveOversampleFactor() == 4);
    // Block granularity: the crossfade ends inside the last block
    CHECK(totalProcessed >= rampSamples);
    CHECK(totalProcessed < rampSamples + kBlockSize);
}

// =============================================================================
// T11.059: the switch has no step larger than 3x the steady-state step, and it
// crossfades the two factors instead of dipping the band (D-034, follow-up 6,
// user ruling 2026-11-09): every 4-period window within 1 dB of the steady level
// =============================================================================

TEST_CASE("BandProcessor: factor switch crossfades the two factors without a dip or a step",
          "[oversampling][crossfade][click-free]") {
    constexpr size_t kSettle = 4096;
    constexpr size_t kSwitchSpan = 4096;
    auto bp = makeLegacyBand(DistortionType::SoftClip, 0.3f, kSettle);

    std::vector<float> left(kSettle + kSwitchSpan);
    std::vector<float> right(kSettle + kSwitchSpan);
    fillSine(left.data(), right.data(), left.size(), 0);

    bp->processBlock(left.data(), right.data(), kSettle);
    bp->setDistortionType(DistortionType::HardClip);  // 2x -> 4x
    REQUIRE(bp->isOversampleTransitioning());
    bp->processBlock(left.data() + kSettle, right.data() + kSettle, kSwitchSpan);
    REQUIRE_FALSE(bp->isOversampleTransitioning());

    const double steadyStep = maxStep(left, kSettle / 2, kSettle);
    const double switchStep = maxStep(left, kSettle, kSettle + kSwitchSpan);
    // Level: RMS of 401-sample windows (4.0 periods of 440 Hz) hopped by 20 samples across
    // the switch; each must lie within 1 dB of the range spanned by the steady window before
    // the switch (2x SoftClip) and the settled one after it (4x HardClip, a different level)
    constexpr size_t kWin = 401;
    const auto rms = [&](size_t from) {
        double acc = 0.0;
        for (size_t i = from; i < from + kWin; ++i) acc += static_cast<double>(left[i]) * left[i];
        return 10.0 * std::log10(acc / static_cast<double>(kWin));
    };
    const double before = rms(kSettle - kWin);
    const double after = rms(left.size() - kWin);
    const double lo = std::min(before, after);
    const double hi = std::max(before, after);
    double worst = 0.0;  // signed distance outside [lo, hi]
    for (size_t from = kSettle - kWin / 2; from + kWin <= kSettle + 2048; from += 20) {
        const double level = rms(from);
        const double dev = std::min(level - lo, 0.0) + std::max(level - hi, 0.0);
        if (std::fabs(dev) > std::fabs(worst)) worst = dev;
    }
    INFO("steady max step " << steadyStep << ", max step during switch " << switchStep << ", level before "
         << before << " dB, after " << after << " dB, worst window deviation " << worst << " dB");
    CHECK(switchStep <= 3.0 * steadyStep);
    CHECK(std::fabs(worst) <= 1.0);

    Krate::DSP::TestUtils::ClickDetectorConfig clickConfig;
    clickConfig.sampleRate = static_cast<float>(kSampleRate);
    clickConfig.frameSize = 512;
    clickConfig.hopSize = 256;
    clickConfig.detectionThreshold = 8.0f;
    clickConfig.energyThresholdDb = -60.0f;
    Krate::DSP::TestUtils::ClickDetector detector(clickConfig);
    detector.prepare();
    const auto clicks = detector.detect(left.data() + kSettle, kSwitchSpan);
    INFO("Clicks detected: " << clicks.size());
    CHECK(clicks.empty());
}

// =============================================================================
// Processing through a factor switch is allocation-free
// =============================================================================

TEST_CASE("BandProcessor: processing through a factor switch allocates nothing",
          "[oversampling][crossfade][realtime]") {
    constexpr size_t kBlockSize = 128;
    auto bp = makeLegacyBand(DistortionType::SoftClip, 0.5f, kBlockSize);
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    fillSine(left.data(), right.data(), kBlockSize, 0);
    bp->processBlock(left.data(), right.data(), kBlockSize);  // settle outside the scope

    size_t allocations = 0;
    {
        TestHelpers::AllocationScope scope;
        bp->setDistortionType(DistortionType::HardClip);  // 2x -> 4x
        for (size_t b = 0; b < 8; ++b) {
            fillSine(left.data(), right.data(), kBlockSize, (b + 1) * kBlockSize);
            bp->processBlock(left.data(), right.data(), kBlockSize);
        }
        bp->setDistortionType(DistortionType::Bitcrush);  // 4x -> 1x
        for (size_t b = 0; b < 8; ++b) {
            fillSine(left.data(), right.data(), kBlockSize, (b + 9) * kBlockSize);
            bp->processBlock(left.data(), right.data(), kBlockSize);
        }
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(bp->getActiveOversampleFactor() == 1);
    REQUIRE(allocations == 0);
}

// =============================================================================
// T11.061: a new request during a switch (FR-010)
// =============================================================================

TEST_CASE("BandProcessor: factor request during an active switch",
          "[oversampling][crossfade][FR-010]") {
    constexpr size_t kBlockSize = 64;
    auto bp = makeLegacyBand(DistortionType::SoftClip, 0.5f, kBlockSize);
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    const auto processUntilIdle = [&]() {
        size_t processed = 0;
        while (bp->isOversampleTransitioning() && processed < 44100) {
            left.fill(0.3f);
            right.fill(0.3f);
            bp->processBlock(left.data(), right.data(), kBlockSize);
            processed += kBlockSize;
        }
        return processed;
    };
    left.fill(0.3f);
    right.fill(0.3f);
    bp->processBlock(left.data(), right.data(), kBlockSize);

    SECTION("a third factor during the crossfade ends at that factor") {
        bp->setDistortionType(DistortionType::HardClip);  // 2x -> 4x
        bp->processBlock(left.data(), right.data(), kBlockSize);  // part of the crossfade
        bp->setDistortionType(DistortionType::Bitcrush);  // -> 1x
        CHECK(bp->getOversampleFactor() == 1);
        CHECK(bp->isOversampleTransitioning());
        processUntilIdle();
        CHECK_FALSE(bp->isOversampleTransitioning());
        CHECK(bp->getActiveOversampleFactor() == 1);
    }

    SECTION("returning to the running factor during the crossfade reverses it without switching") {
        bp->setDistortionType(DistortionType::HardClip);  // 2x -> 4x
        bp->processBlock(left.data(), right.data(), kBlockSize);
        REQUIRE(bp->getActiveOversampleFactor() == 4);  // crossfading into the 4x lane
        bp->setDistortionType(DistortionType::SoftClip);  // back to 2x
        CHECK(bp->isOversampleTransitioning());
        bp->processBlock(left.data(), right.data(), kBlockSize);  // reverses toward the 2x lane
        CHECK(bp->getActiveOversampleFactor() == 2);
        processUntilIdle();
        CHECK_FALSE(bp->isOversampleTransitioning());
        CHECK(bp->getActiveOversampleFactor() == 2);
    }

    SECTION("a request during the crossfade starts a new one after it") {
        bp->setDistortionType(DistortionType::HardClip);  // 2x -> 4x
        // 256 samples into the 441-sample crossfade (44.1 kHz)
        for (int i = 0; i < 4; ++i) {
            bp->processBlock(left.data(), right.data(), kBlockSize);
        }
        REQUIRE(bp->getActiveOversampleFactor() == 4);
        REQUIRE(bp->isOversampleTransitioning());
        bp->setDistortionType(DistortionType::Bitcrush);  // -> 1x
        processUntilIdle();
        CHECK_FALSE(bp->isOversampleTransitioning());
        CHECK(bp->getActiveOversampleFactor() == 1);
    }
}

// =============================================================================
// T11.062: Hysteresis - no switch within same factor region (FR-017)
// =============================================================================

TEST_CASE("BandProcessor: hysteresis prevents unnecessary transitions",
          "[oversampling][crossfade][FR-017]") {
    auto bp = std::make_unique<BandProcessor>();
    bp->prepare(kSampleRate, 512);
    std::array<float, 512> flushL{};
    std::array<float, 512> flushR{};

    SECTION("changing between types with same factor does not trigger a switch") {
        bp->setDistortionType(DistortionType::SoftClip);
        bp->processBlock(flushL.data(), flushR.data(), flushL.size());
        CHECK(bp->getOversampleFactor() == 2);
        CHECK_FALSE(bp->isOversampleTransitioning());

        // Change to another 2x type - same factor, no switch
        bp->setDistortionType(DistortionType::Tube);
        CHECK(bp->getOversampleFactor() == 2);
        CHECK_FALSE(bp->isOversampleTransitioning());

        // Change to another 2x type
        bp->setDistortionType(DistortionType::Tape);
        CHECK(bp->getOversampleFactor() == 2);
        CHECK_FALSE(bp->isOversampleTransitioning());
    }

    SECTION("morph within same factor region does not trigger a switch") {
        // Set up morph between two 4x types
        std::array<MorphNode, kMaxMorphNodes> nodes = {{
            MorphNode(0, 0.0f, 0.0f, DistortionType::HardClip),  // 4x
            MorphNode(1, 1.0f, 0.0f, DistortionType::Fuzz),      // 4x
            MorphNode(2, 0.0f, 1.0f, DistortionType::HardClip),
            MorphNode(3, 1.0f, 1.0f, DistortionType::Fuzz)
        }};
        bp->setMorphNodes(nodes, 2);
        bp->setMorphMode(MorphMode::Linear1D);
        bp->setMorphPosition(0.0f, 0.0f);
        CHECK(bp->getOversampleFactor() == 4);

        for (int i = 0; i < 10; ++i) {
            bp->processBlock(flushL.data(), flushR.data(), flushL.size());
        }
        CHECK_FALSE(bp->isOversampleTransitioning());

        // Move morph position - both nodes are 4x, so factor stays 4
        bp->setMorphPosition(0.5f, 0.0f);
        CHECK(bp->getOversampleFactor() == 4);
        CHECK_FALSE(bp->isOversampleTransitioning());

        bp->setMorphPosition(1.0f, 0.0f);
        CHECK(bp->getOversampleFactor() == 4);
        CHECK_FALSE(bp->isOversampleTransitioning());
    }

    SECTION("changing to a different factor DOES trigger a switch") {
        bp->setDistortionType(DistortionType::SoftClip);
        bp->processBlock(flushL.data(), flushR.data(), flushL.size());
        CHECK(bp->getOversampleFactor() == 2);
        CHECK_FALSE(bp->isOversampleTransitioning());

        // Change to 4x type
        bp->setDistortionType(DistortionType::HardClip);
        CHECK(bp->getOversampleFactor() == 4);
        CHECK(bp->isOversampleTransitioning());
    }
}
