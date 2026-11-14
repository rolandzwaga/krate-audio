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

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/allpass_saturator.h>

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
    auto bp = makeLegacyBand(DistortionType::Tube, 0.5f, kBlockSize);

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
    auto bp = makeLegacyBand(DistortionType::Tube, 0.3f, kSettle);

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
    // the switch (2x Tube) and the settled one after it (4x HardClip, a different level)
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
    auto bp = makeLegacyBand(DistortionType::Tube, 0.5f, kBlockSize);
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
    auto bp = makeLegacyBand(DistortionType::Tube, 0.5f, kBlockSize);
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
        bp->setDistortionType(DistortionType::Tube);  // back to 2x
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
        bp->setDistortionType(DistortionType::Tube);
        bp->processBlock(flushL.data(), flushR.data(), flushL.size());
        CHECK(bp->getOversampleFactor() == 2);
        CHECK_FALSE(bp->isOversampleTransitioning());

        // Change to another 2x type - same factor, no switch
        bp->setDistortionType(DistortionType::Fractal);
        CHECK(bp->getOversampleFactor() == 2);
        CHECK_FALSE(bp->isOversampleTransitioning());

        // Change to another 2x type
        bp->setDistortionType(DistortionType::Chaos);
        CHECK(bp->getOversampleFactor() == 2);
        CHECK_FALSE(bp->isOversampleTransitioning());
    }

    SECTION("morph within same factor region does not trigger a switch") {
        // Set up morph between two 4x types
        std::array<MorphNode, kMaxMorphNodes> nodes = {{
            MorphNode(0, 0.0f, 0.0f, DistortionType::HardClip),  // 4x
            MorphNode(1, 1.0f, 0.0f, DistortionType::HalfRectify),  // 4x
            MorphNode(2, 0.0f, 1.0f, DistortionType::HardClip),
            MorphNode(3, 1.0f, 1.0f, DistortionType::HalfRectify)
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
        bp->setDistortionType(DistortionType::Tube);
        bp->processBlock(flushL.data(), flushR.data(), flushL.size());
        CHECK(bp->getOversampleFactor() == 2);
        CHECK_FALSE(bp->isOversampleTransitioning());

        // Change to 4x type
        bp->setDistortionType(DistortionType::HardClip);
        CHECK(bp->getOversampleFactor() == 4);
        CHECK(bp->isOversampleTransitioning());
    }
}

// =============================================================================
// The crossfade's second lane holds only the shapers its path runs (user ruling
// 2026-11-10, slim spare lane). A DistortionAdapter holds every type's processor
// (about 1 MB prepared at 48 kHz, 630 KB of it Spectral's). The lane the band runs
// keeps one per slot: single-type, identical-type and four nodes per channel (12);
// the other lane, which runs only as a crossfade's incoming lane, at most four per
// channel (the active nodes, or the one identical-type / single-type adapter): 8.
// Before the ruling the second lane was a full copy: 24 adapters per band.
// =============================================================================

TEST_CASE("BandProcessor: the crossfade's second lane holds only the shapers its path runs",
          "[disrumpo][oversampling][crossfade][memory]") {
    constexpr size_t kBlock = 256;
    const int before = DistortionAdapter::debugLiveInstances.load();
    auto bp = std::make_unique<BandProcessor>();
    bp->prepare(48000.0, kBlock);
    const int held = DistortionAdapter::debugLiveInstances.load() - before;
    INFO("adapters held by a prepared band: " << held);
    CHECK(held == 2 * (6 + kMaxMorphNodes));

    // Four cross-family nodes: the incoming lane runs four node adapters per channel
    std::array<MorphNode, kMaxMorphNodes> nodes{};
    const DistortionType types[kMaxMorphNodes] = {DistortionType::SoftClip, DistortionType::SineFold,
                                                  DistortionType::FullRectify, DistortionType::HardClip};
    for (int i = 0; i < kMaxMorphNodes; ++i) {
        auto& n = nodes[static_cast<size_t>(i)];
        n.id = i;
        n.type = types[i];
        n.commonParams = {.drive = 3.0f, .mix = 1.0f, .toneHz = 4000.0f};
        n.posX = (i == 1 || i == 3) ? 1.0f : 0.0f;
        n.posY = i >= 2 ? 1.0f : 0.0f;
    }
    bp->setMorphMode(MorphMode::Planar2D);
    bp->setMorphNodes(nodes, kMaxMorphNodes);
    bp->setMorphPosition(0.4f, 0.4f);
    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    size_t offset = 0;
    size_t nonFinite = 0;
    const auto run = [&](int blocks) {
        for (int b = 0; b < blocks; ++b, offset += kBlock) {
            fillSine(left.data(), right.data(), kBlock, offset);
            bp->processBlock(left.data(), right.data(), kBlock);
            for (size_t i = 0; i < kBlock; ++i) {
                if (!Krate::DSP::detail::isFinite(left[i]) || !Krate::DSP::detail::isFinite(right[i])) ++nonFinite;
            }
        }
    };
    run(8);
    // 4x -> 2x -> 4x, a reversal, and the path changing to identical types during a crossfade
    bp->setMaxOversampleFactor(2);
    run(4);
    bp->setMaxOversampleFactor(4);
    run(1);
    bp->setMaxOversampleFactor(2);
    run(4);
    for (auto& n : nodes) n.type = DistortionType::HardClip;
    bp->setMaxOversampleFactor(4);
    run(1);
    bp->setMorphNodes(nodes, kMaxMorphNodes);
    run(8);
    CHECK(DistortionAdapter::debugLiveInstances.load() - before == held);
    CHECK(nonFinite == 0);
    bp = nullptr;
    CHECK(DistortionAdapter::debugLiveInstances.load() == before);
}

// =============================================================================
// Lane switch warm start (user ruling 2026-11-11, round 4)
// =============================================================================
// A factor change starts the incoming lane with its processing state cleared. Round 3 measured a
// burst at every switch on Allpass Resonant and Sine Fold bands (-24 / -35 dBFS peak from a
// -58 dBFS input, decaying over ~150 ms), at 2x <-> 4x as at 4x <-> 8x ([allpass-lane-probe]).

namespace {

struct LaneCase {
    const char* name;
    DistortionType type;
    int topo;
    float bias;
};

constexpr double kLaneRate = 48000.0;
constexpr size_t kLaneBlock = 512;
constexpr float kQuietAmp = 0.00126f;  // -58 dBFS

/// A 1-node morph band (drive 2, mix 100 %) on a quiet 220 Hz sine at Oversample Max @p first, set to
/// @p second at block @p switchBlock (0: never). Returns the left channel.
std::vector<float> laneRender(const LaneCase& c, int first, int second, size_t switchBlock, size_t blocks) {
    auto bp = std::make_unique<BandProcessor>();
    bp->prepare(kLaneRate, kLaneBlock);
    DistortionParams params;
    params.allpassTopo = c.topo;
    params.resonantFreq = 500.0f;
    params.allpassFeedback = 0.6f;
    params.decayTimeS = 1.5f;
    params.bias = c.bias;
    std::array<MorphNode, kMaxMorphNodes> nodes{};
    nodes[0] = MorphNode(0, 0.0f, 0.0f, c.type);
    nodes[0].commonParams.drive = 2.0f;
    nodes[0].commonParams.mix = 1.0f;
    nodes[0].params = params;
    bp->setMorphEnabled(true);
    bp->setMorphNodes(nodes, 1);
    bp->setMaxOversampleFactor(first);
    std::vector<float> out(blocks * kLaneBlock);
    std::vector<float> l(kLaneBlock);
    std::vector<float> r(kLaneBlock);
    for (size_t b = 0; b < blocks; ++b) {
        if (switchBlock > 0 && b == switchBlock) bp->setMaxOversampleFactor(second);
        for (size_t i = 0; i < kLaneBlock; ++i) {
            const double t = static_cast<double>(b * kLaneBlock + i) / kLaneRate;
            l[i] = static_cast<float>(kQuietAmp * std::sin(2.0 * std::numbers::pi * 220.0 * t));
            r[i] = l[i];
        }
        bp->processBlock(l.data(), r.data(), kLaneBlock);
        std::copy(l.begin(), l.end(), out.begin() + static_cast<std::ptrdiff_t>(b * kLaneBlock));
    }
    return out;
}

double meanSquare(const std::vector<float>& x, size_t from, size_t n) {
    double s = 0.0;
    for (size_t i = from; i < from + n; ++i) s += static_cast<double>(x[i]) * x[i];
    return s / static_cast<double>(n);
}

double toDb(double ms) { return 10.0 * std::log10(ms + 1e-30); }

constexpr size_t kLaneBlocks = 94;  // ~1 s
constexpr size_t kLaneSwitchBlock = 47;

}  // namespace

// The incoming lane's 10 Hz DC blockers start on the means the running lane's remove, and Allpass
// Resonant's loop on its state (DistortionAdapter::warmStartFrom). Measured before the warm start
// (RMS over the 200 ms after a switch, excess over the held renders): Sine Fold +10.2 dB, bias 0.3
// +15.0, Serge Fold +10.2, Asymmetric Fuzz +9.2, Triangle Fold +4.3, the burst decaying at the
// blocker's 10 Hz pole (Allpass Resonant matrix: -546 dB/s, [lane-burst-probe]); Allpass Resonant
// chain -21.6 dBFS (+57.1, its cleared loop latched into a sustained state), matrix -35.9 dBFS.
// The DC-blocker types must come within 3 dB of the held renders. Allpass Resonant's loops keep a
// residual that still decays at the pole: their allpass state, carried across rates as the DC it
// settles on, lacks the input's AC history, whose transient moves the seeded estimate (chain -62.5,
// matrix -74.3 dBFS from a -58 dBFS input; open question, round 4). They are held to no more than
// the input's RMS level (-61.0 dBFS).
TEST_CASE("BandProcessor: a lane switch adds no burst over the held renders",
          "[disrumpo][oversampling][crossfade][lane-warm-start]") {
    const std::array<LaneCase, 9> cases{{
        {.name = "Allpass Resonant single", .type = DistortionType::AllpassResonant, .topo = 0, .bias = 0.0f},
        {.name = "Allpass Resonant chain", .type = DistortionType::AllpassResonant, .topo = 1, .bias = 0.0f},
        {.name = "Allpass Resonant matrix", .type = DistortionType::AllpassResonant, .topo = 3, .bias = 0.0f},
        {.name = "Sine Fold", .type = DistortionType::SineFold, .topo = 0, .bias = 0.0f},
        {.name = "Sine Fold bias 0.3", .type = DistortionType::SineFold, .topo = 0, .bias = 0.3f},
        {.name = "Serge Fold", .type = DistortionType::SergeFold, .topo = 0, .bias = 0.0f},
        {.name = "Triangle Fold", .type = DistortionType::TriangleFold, .topo = 0, .bias = 0.0f},
        {.name = "Asymmetric Fuzz", .type = DistortionType::AsymmetricFuzz, .topo = 0, .bias = 0.0f},
        {.name = "Hard Clip (no DC blocker)", .type = DistortionType::HardClip, .topo = 0, .bias = 0.0f},
    }};
    // The input's RMS level (a sine of peak kQuietAmp, -58 dBFS): -61.0 dBFS
    const double inputRmsDb = 20.0 * std::log10(static_cast<double>(kQuietAmp) / std::numbers::sqrt2);
    const size_t at = kLaneSwitchBlock * kLaneBlock;
    const auto win = static_cast<size_t>(0.2 * kLaneRate);
    constexpr double kFloorDb = -80.0;  // 22 dB under the input
    for (const auto& c : cases) {
        for (const auto& [from, to] : {std::pair{4, 8}, std::pair{8, 4}, std::pair{2, 4}, std::pair{4, 2}}) {
            const auto held0 = laneRender(c, from, from, 0, kLaneBlocks);
            const auto held1 = laneRender(c, to, to, 0, kLaneBlocks);
            const auto sw = laneRender(c, from, to, kLaneSwitchBlock, kLaneBlocks);
            const double held0Db = toDb(meanSquare(held0, at, win));
            const double held1Db = toDb(meanSquare(held1, at, win));
            const double refDb = std::max({held0Db, held1Db, kFloorDb});
            const double swDb = toDb(meanSquare(sw, at, win));
            std::printf("Lane switch %s %dx -> %dx: RMS over the 200 ms after the switch %.1f dBFS, held %.1f / %.1f "
                        "dBFS (reference %.1f), excess %.1f dB\n",
                        c.name, from, to, swDb, held0Db, held1Db, refDb, swDb - refDb);
            INFO(c.name << " " << from << "x -> " << to << "x: " << swDb << " dBFS against " << refDb);
            if (c.type == DistortionType::AllpassResonant) {
                CHECK(swDb <= inputRmsDb);
            } else {
                CHECK(swDb - refDb <= 3.0);
            }
        }
    }
}

// Probe (hidden, round 4 ruling 2): the decay rate of a lane switch's burst (the switch render minus
// the render held at the new factor, 5 ms windows) against the incoming lane's 10 Hz DC-blocker
// pole, and every type's excess over the held renders at 4x -> 8x and 2x -> 4x.
TEST_CASE("Probe: lane-switch burst decay and type survey", "[.][lane-burst-probe]") {
    const size_t at = kLaneSwitchBlock * kLaneBlock;
    const auto w = static_cast<size_t>(0.005 * kLaneRate);
    for (const LaneCase c : {LaneCase{.name = "Allpass Resonant chain", .type = DistortionType::AllpassResonant, .topo = 1, .bias = 0.0f},
                             LaneCase{.name = "Allpass Resonant matrix", .type = DistortionType::AllpassResonant, .topo = 3, .bias = 0.0f},
                             LaneCase{.name = "Sine Fold", .type = DistortionType::SineFold, .topo = 0, .bias = 0.0f}}) {
        for (const auto& [from, to] : {std::pair{4, 8}, std::pair{8, 4}, std::pair{2, 4}}) {
            const auto held1 = laneRender(c, to, to, 0, kLaneBlocks);
            const auto sw = laneRender(c, from, to, kLaneSwitchBlock, kLaneBlocks);
            std::vector<float> diff(sw.size());
            for (size_t i = 0; i < sw.size(); ++i) diff[i] = sw[i] - held1[i];
            // Least-squares slope of the windows' dB from 20 to 120 ms after the switch
            double sx = 0.0;
            double sy = 0.0;
            double sxx = 0.0;
            double sxy = 0.0;
            int n = 0;
            std::printf("LBP %s %dx -> %dx windows:", c.name, from, to);
            for (size_t k = 0; k < 40; ++k) {
                const double db = toDb(meanSquare(diff, at + k * w, w));
                const double t = (static_cast<double>(k) + 0.5) * 0.005;
                if (k % 4 == 0) std::printf(" %.0f", db);
                if (t >= 0.02 && t <= 0.12 && db > -150.0) {
                    sx += t; sy += db; sxx += t * t; sxy += t * db; ++n;
                }
            }
            const double slope = n > 1 ? (n * sxy - sx * sy) / (n * sxx - sx * sx) : 0.0;
            const double fsClk = kLaneRate * static_cast<double>(to);
            const double rPole = std::clamp(std::exp(-2.0 * std::numbers::pi * 10.0 / fsClk), 0.9, 0.9999);
            std::printf("\nLBP %s %dx -> %dx: burst decay %.0f dB/s; the %dx lane's DC blocker pole (R %.6f) %.0f dB/s\n",
                        c.name, from, to, slope, to, rPole, 20.0 * std::log10(rPole) * fsClk);
        }
    }
    for (int t = 0; t < kDistortionTypeCount; ++t) {
        const auto type = static_cast<DistortionType>(t);
        const LaneCase c{.name = getTypeName(type), .type = type, .topo = 0, .bias = 0.0f};
        for (const auto& [from, to] : {std::pair{4, 8}, std::pair{2, 4}}) {
            const auto held0 = laneRender(c, from, from, 0, kLaneBlocks);
            const auto held1 = laneRender(c, to, to, 0, kLaneBlocks);
            const auto sw = laneRender(c, from, to, kLaneSwitchBlock, kLaneBlocks);
            const auto win = static_cast<size_t>(0.2 * kLaneRate);
            const double refDb =
                std::max({toDb(meanSquare(held0, at, win)), toDb(meanSquare(held1, at, win)), -80.0});
            std::printf("LBP survey %s %dx -> %dx: excess %.1f dB (switch %.1f, reference %.1f dBFS)\n", c.name, from,
                        to, toDb(meanSquare(sw, at, win)) - refDb, toDb(meanSquare(sw, at, win)), refDb);
        }
    }
}

// Probe (hidden, round 4 ruling 2): AllpassSaturator::warmStartFrom at the same rate and across rates.
// A runs a 220 Hz sine at rate a for 0.5 s; a cleared saturator warm-started from it at 0.5 s (at
// rate a, and at rate b) runs on, against A itself (same rate) and against one at rate b run from
// 0 (held). RMS of the difference in the first 20 ms and in 20-100 ms after the start.
TEST_CASE("Probe: AllpassSaturator warm start same rate and across rates", "[.][allpass-warm-probe]") {
    using Krate::DSP::AllpassSaturator;
    using Krate::DSP::NetworkTopology;
    const auto setUp = [](AllpassSaturator& s, double rate, int topo) {
        s.prepare(rate, 512);
        s.setTopology(static_cast<NetworkTopology>(topo));
        s.setFrequency(500.0f);
        s.setFeedback(0.6f);
        s.setDecay(1.5f);
        s.setDrive(5.05f);
        s.setDamping(0.0f);
    };
    const auto sine = [](double t) { return static_cast<float>(0.02 * std::sin(2.0 * std::numbers::pi * 220.0 * t)); };
    for (const int topo : {0, 1, 3}) {
        for (const auto& [ra, rb] : {std::pair{192000.0, 384000.0}, std::pair{384000.0, 192000.0}}) {
            auto a = std::make_unique<AllpassSaturator>();
            auto held = std::make_unique<AllpassSaturator>();
            auto sameRate = std::make_unique<AllpassSaturator>();
            auto otherRate = std::make_unique<AllpassSaturator>();
            setUp(*a, ra, topo);
            setUp(*held, rb, topo);
            setUp(*sameRate, ra, topo);
            setUp(*otherRate, rb, topo);
            const auto na = static_cast<size_t>(0.5 * ra);
            const auto nb = static_cast<size_t>(0.5 * rb);
            for (size_t i = 0; i < na; ++i) (void)a->process(sine(static_cast<double>(i) / ra));
            for (size_t i = 0; i < nb; ++i) (void)held->process(sine(static_cast<double>(i) / rb));
            (*sameRate).reset();  // as the band clears the incoming lane (smoothers on their targets)
            (*otherRate).reset();
            sameRate->warmStartFrom(*a);
            otherRate->warmStartFrom(*a);
            std::array<double, 2> sSame{};
            std::array<double, 2> sOther{};
            std::array<double, 2> sHeld{};
            const auto nA = static_cast<size_t>(0.1 * ra);
            for (size_t i = 0; i < nA; ++i) {
                const float x = sine(static_cast<double>(na + i) / ra);
                const double d = static_cast<double>(sameRate->process(x)) - a->process(x);
                sSame[i < nA / 5 ? 0 : 1] += d * d / static_cast<double>(nA);
            }
            const auto nB = static_cast<size_t>(0.1 * rb);
            for (size_t i = 0; i < nB; ++i) {
                const float x = sine(static_cast<double>(nb + i) / rb);
                const float h = held->process(x);
                const double d = static_cast<double>(otherRate->process(x)) - h;
                sOther[i < nB / 5 ? 0 : 1] += d * d / static_cast<double>(nB);
                sHeld[i < nB / 5 ? 0 : 1] += static_cast<double>(h) * h / static_cast<double>(nB);
            }
            std::printf("AWP topo %d %.0f -> %.0f: same-rate diff %.1f / %.1f dB, cross-rate diff %.1f / %.1f dB, "
                        "held %.1f / %.1f dB (0-20 / 20-100 ms)\n",
                        topo, ra, rb, toDb(sSame[0]), toDb(sSame[1]), toDb(sOther[0]), toDb(sOther[1]),
                        toDb(sHeld[0]), toDb(sHeld[1]));
        }
    }
    // The same through DistortionAdapter (the band's node settings, -58 dBFS sine, no oversampler)
    for (const int topo : {0, 1, 3}) {
        for (const auto& [ra, rb] : {std::pair{192000.0, 384000.0}, std::pair{384000.0, 192000.0}}) {
            const auto make = [topo](double rate) {
                auto ad = std::make_unique<DistortionAdapter>();
                ad->prepare(kLaneRate, static_cast<int>(kLaneBlock * 8), 8);
                ad->setType(DistortionType::AllpassResonant);
                DistortionParams p;
                p.allpassTopo = topo;
                p.resonantFreq = 500.0f;
                p.allpassFeedback = 0.6f;
                p.decayTimeS = 1.5f;
                ad->setParams(p);
                DistortionCommonParams cp;
                cp.drive = 2.0f;
                cp.mix = 1.0f;
                ad->setCommonParams(cp);
                ad->setProcessingRate(rate);
                return ad;
            };
            const auto x = [](double t) { return static_cast<float>(kQuietAmp * std::sin(2.0 * std::numbers::pi * 220.0 * t)); };
            auto a = make(ra);
            auto held = make(rb);
            auto warm = make(rb);
            const auto na = static_cast<size_t>(0.5 * ra);
            const auto nb = static_cast<size_t>(0.5 * rb);
            for (size_t i = 0; i < na; ++i) (void)a->process(x(static_cast<double>(i) / ra));
            for (size_t i = 0; i < nb; ++i) (void)held->process(x(static_cast<double>(i) / rb));
            (*warm).reset();  // as the band clears the incoming lane
            warm->warmStartFrom(*a);
            std::array<double, 2> sDiff{};
            std::array<double, 2> sHeld{};
            const auto nB = static_cast<size_t>(0.1 * rb);
            for (size_t i = 0; i < nB; ++i) {
                const float in = x(static_cast<double>(nb + i) / rb);
                const float h = held->process(in);
                const double d = static_cast<double>(warm->process(in)) - h;
                sDiff[i < nB / 5 ? 0 : 1] += d * d / static_cast<double>(nB);
                sHeld[i < nB / 5 ? 0 : 1] += static_cast<double>(h) * h / static_cast<double>(nB);
            }
            std::printf("AWP adapter topo %d %.0f -> %.0f: cross-rate diff %.1f / %.1f dB, held %.1f / %.1f dB\n", topo,
                        ra, rb, toDb(sDiff[0]), toDb(sDiff[1]), toDb(sHeld[0]), toDb(sHeld[1]));
        }
    }
}
