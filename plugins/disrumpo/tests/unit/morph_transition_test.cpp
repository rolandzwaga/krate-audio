// ==============================================================================
// MorphEngine Transition Tests
// ==============================================================================
// Unit tests for morph smoothing and transition behavior.
//
// Constitution Principle XII: Test-First Development
// Reference: specs/005-morph-system/spec.md FR-009, SC-006, SC-007
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <catch2/catch_approx.hpp>

#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_adapter.h"
#include "dsp/distortion_types.h"

#include <test_helpers/artifact_detection.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using Catch::Approx;

// =============================================================================
// Test Fixtures
// =============================================================================

namespace {

/// @brief Configure a MorphEngine for testing.
void prepareTestEngine(Disrumpo::MorphEngine& engine, double sampleRate = 44100.0) {
    engine.prepare(sampleRate, 512);
}

/// @brief Create standard 2-node setup (A at 0, B at 1).
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createTwoNodes() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);
    return nodes;
}

/// @brief Process N samples through engine to advance smoothers.
void advanceSamples(Disrumpo::MorphEngine& engine, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
        [[maybe_unused]] float out = engine.process(0.0f);  // Input doesn't matter for position smoothing test
    }
}

/// @brief Calculate number of samples for a given time in ms at sample rate.
int msToSamples(float ms, double sampleRate) {
    return static_cast<int>(ms * 0.001f * sampleRate);
}

/// @brief Check if output has any clicks (sudden large changes).
bool hasClicks(const std::vector<float>& output, float threshold = 0.1f) {
    if (output.size() < 2) return false;

    for (size_t i = 1; i < output.size(); ++i) {
        float diff = std::abs(output[i] - output[i - 1]);
        if (diff > threshold) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Cross-family blend helpers (fix plan T1.13)
// -----------------------------------------------------------------------------

constexpr double kXfRate = 48000.0;
constexpr float kXfFreq = 200.0f;          // 240 samples per period: 10 ms windows hold whole periods
constexpr float kXfAmp = 0.25118864f;      // -12 dBFS
constexpr int kXfBlock = 512;

Disrumpo::MorphNode xfNode(int id, float x, float y, Disrumpo::DistortionType type, float drive) {
    Disrumpo::MorphNode n(id, x, y, type);
    n.commonParams.drive = drive;
    n.commonParams.mix = 1.0f;
    return n;
}

/// Two cross-family nodes: SoftClip at x = 0, SineFold at x = 1 (drive 3 = normalized 0.3).
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> xfTwoNodes(float drive = 3.0f) {
    return {xfNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip, drive),
            xfNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::SineFold, drive),
            xfNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::FullRectify, drive),
            xfNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::RingSaturation, drive)};
}

/// Four cross-family nodes at the corners: SoftClip, SineFold, FullRectify, RingSaturation.
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> xfFourNodes() { return xfTwoNodes(3.0f); }

float xfInput(size_t n) {
    return kXfAmp * std::sin(2.0f * std::numbers::pi_v<float> * kXfFreq *
                             static_cast<float>(static_cast<double>(n) / kXfRate));
}

double rmsDb(const std::vector<float>& y, size_t from, size_t count) {
    double sum = 0.0;
    for (size_t i = from; i < from + count; ++i) {
        sum += static_cast<double>(y[i]) * static_cast<double>(y[i]);
    }
    return 10.0 * std::log10(std::max(sum / static_cast<double>(count), 1e-30));
}

size_t xfClickCount(const std::vector<float>& y, size_t from, size_t count) {
    Krate::DSP::TestUtils::ClickDetectorConfig cfg;
    cfg.sampleRate = static_cast<float>(kXfRate);
    cfg.frameSize = 512;
    cfg.hopSize = 256;
    cfg.detectionThreshold = 5.0f;
    cfg.energyThresholdDb = -60.0f;
    Krate::DSP::TestUtils::ClickDetector det(cfg);
    det.prepare();
    return det.detect(y.data() + from, count).size();
}

std::unique_ptr<Disrumpo::MorphEngine> makeXfEngine(
    const std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes>& nodes, int count,
    Disrumpo::MorphMode mode, float smoothingMs) {
    auto engine = std::make_unique<Disrumpo::MorphEngine>();
    engine->prepare(kXfRate, kXfBlock);
    engine->setNodes(nodes, count);
    engine->setMode(mode);
    engine->setSmoothingTime(smoothingMs);
    return engine;
}

/// Level of one node's adapter alone over the samples [from, from + count) of a render of
/// @p total samples of the test sine.
double adapterAloneDb(const Disrumpo::MorphNode& node, size_t total, size_t from, size_t count) {
    auto adapter = std::make_unique<Disrumpo::DistortionAdapter>();
    adapter->prepare(kXfRate, kXfBlock);
    adapter->setType(node.type);
    adapter->setParams(node.params);
    adapter->setCommonParams(node.commonParams);
    std::vector<float> y(total);
    for (size_t n = 0; n < total; ++n) y[n] = adapter->process(xfInput(n));
    return rmsDb(y, from, count);
}

struct SweepResult {
    double startDb = 0.0;
    double endDb = 0.0;
    double maxDevDb = 0.0;   // signed, largest |deviation| from the straight dB line
    size_t clicks = 0;
};

/// 0.5 s held at the start corner, 2 s cursor sweep at block rate, 0.5 s held at the end.
SweepResult crossFamilySweep(const std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes>& nodes,
                             int count, Disrumpo::MorphMode mode, bool diagonal) {
    auto engine = makeXfEngine(nodes, count, mode, 10.0f);
    engine->setMorphPosition(0.0f, 0.0f);
    (*engine).reset();
    const auto S = [](double s) { return static_cast<size_t>(s * kXfRate); };
    const size_t total = S(3.0);
    std::vector<float> y(total);
    for (size_t start = 0; start < total; start += kXfBlock) {
        const double t = static_cast<double>(start) / kXfRate;
        float x = 0.0f;
        if (t >= 0.5 && t < 2.5) x = static_cast<float>((t - 0.5) / 2.0);
        else if (t >= 2.5) x = 1.0f;
        engine->setMorphPosition(x, diagonal ? x : 0.0f);
        const size_t end = std::min(total, start + static_cast<size_t>(kXfBlock));
        for (size_t n = start; n < end; ++n) y[n] = engine->process(xfInput(n));
    }
    SweepResult r;
    r.startDb = rmsDb(y, S(0.1), S(0.4));
    r.endDb = rmsDb(y, S(2.6), S(0.4));
    const size_t win = S(0.01);
    for (size_t s = S(0.5); s + win <= S(2.5); s += win) {
        const double frac = (static_cast<double>(s) + static_cast<double>(win) * 0.5 - static_cast<double>(S(0.5))) /
                            static_cast<double>(S(2.0));
        const double dev = rmsDb(y, s, win) - (r.startDb + (r.endDb - r.startDb) * frac);
        if (std::abs(dev) > std::abs(r.maxDevDb)) r.maxDevDb = dev;
    }
    r.clicks = xfClickCount(y, S(0.5), S(2.0));
    return r;
}

} // anonymous namespace

// =============================================================================
// FR-009: Morph Smoothing Tests
// =============================================================================

TEST_CASE("MorphEngine smoothing - 0ms smoothing gives fast transition", "[morph][transition][FR-009]") {
    // MorphEngine is ~936 KB (five DistortionAdapters); on the stack it overflows Windows'
    // 1 MB thread stack, so it lives on the heap here as it does in BandProcessor.
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(0.0f);  // 0ms gets clamped to minimum (0.1ms)

    // Start at position 0 and let it settle
    engine.setMorphPosition(0.0f, 0.0f);
    advanceSamples(engine, 50);  // Let it fully settle

    // Should be at 0
    REQUIRE(engine.getSmoothedX() == Approx(0.0f).margin(0.01f));

    // Jump to position 1
    engine.setMorphPosition(1.0f, 0.0f);

    // After minimum smoothing time samples, should reach target
    // 0.1ms at 44.1kHz is about 5 samples, so 50 samples should be enough
    advanceSamples(engine, 50);
    REQUIRE(engine.getSmoothedX() == Approx(1.0f).margin(0.01f));
}

TEST_CASE("MorphEngine smoothing - 100ms smoothing completes in approximately 100ms", "[morph][transition][FR-009][SC-006]") {
    constexpr double sampleRate = 44100.0;
    constexpr float smoothingMs = 100.0f;

    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    prepareTestEngine(engine, sampleRate);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(smoothingMs);

    // Start at position 0
    engine.setMorphPosition(0.0f, 0.0f);
    advanceSamples(engine, 100);  // Let it settle

    // Jump to position 1
    engine.setMorphPosition(1.0f, 0.0f);

    // Process for 95ms (should not be complete yet)
    int samplesAt95ms = msToSamples(95.0f, sampleRate);
    advanceSamples(engine, samplesAt95ms);
    float posAt95ms = engine.getSmoothedX();

    // OnePoleSmoother reaches ~99% at the configured time
    // At 95ms of 100ms, should be close but not quite at target
    // Allow some margin since OnePoleSmoother is exponential
    REQUIRE(posAt95ms > 0.9f);  // Should be at least 90% of the way
    REQUIRE(posAt95ms < 1.0f);  // But not quite there

    // Process for additional 10ms (total 105ms - should be complete)
    int additionalSamples = msToSamples(10.0f, sampleRate);
    advanceSamples(engine, additionalSamples);
    float posAt105ms = engine.getSmoothedX();

    // Should be at or very close to target
    REQUIRE(posAt105ms == Approx(1.0f).margin(0.01f));
}

TEST_CASE("MorphEngine smoothing - rapid automation produces limited sample-to-sample changes", "[morph][transition][FR-009][SC-007]") {
    constexpr double sampleRate = 44100.0;
    constexpr float smoothingMs = 10.0f;  // Fast but with some smoothing

    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    prepareTestEngine(engine, sampleRate);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(smoothingMs);

    // Simulate 20Hz square wave modulation (toggling every 25ms = 1103 samples at 44.1kHz)
    constexpr int samplesPerHalfCycle = 1103;  // ~25ms at 44.1kHz
    constexpr int numCycles = 10;

    std::vector<float> output;
    output.reserve(static_cast<size_t>(samplesPerHalfCycle) * 2 * numCycles);

    for (int cycle = 0; cycle < numCycles; ++cycle) {
        // First half: position 0
        engine.setMorphPosition(0.0f, 0.0f);
        for (int s = 0; s < samplesPerHalfCycle; ++s) {
            float out = engine.process(0.5f);  // Use constant input
            output.push_back(out);
        }

        // Second half: position 1
        engine.setMorphPosition(1.0f, 0.0f);
        for (int s = 0; s < samplesPerHalfCycle; ++s) {
            float out = engine.process(0.5f);
            output.push_back(out);
        }
    }

    // Check that sample-to-sample changes are limited (no extreme clicks)
    // With processing through distortion, the output will vary, but shouldn't have
    // sudden large jumps indicative of clicks. Use a generous threshold.
    REQUIRE_FALSE(hasClicks(output, 0.5f));  // Allow larger changes due to distortion processing
}

// =============================================================================
// SC-006: Smoothing Timing Accuracy Tests
// =============================================================================

TEST_CASE("MorphEngine smoothing - timing accuracy reaches target", "[morph][transition][SC-006]") {
    constexpr double sampleRate = 44100.0;

    // The OnePoleSmoother reaches ~99% at the configured time (5 time constants).
    // We test that at the configured time, the value is very close to target.

    SECTION("50ms smoothing") {
        constexpr float smoothingMs = 50.0f;

        auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
        auto& engine = *engineOwner;
        prepareTestEngine(engine, sampleRate);
        auto nodes = createTwoNodes();
        engine.setNodes(nodes, 2);
        engine.setSmoothingTime(smoothingMs);

        engine.setMorphPosition(0.0f, 0.0f);
        advanceSamples(engine, 500);  // Let it settle

        engine.setMorphPosition(1.0f, 0.0f);

        // At 50% of target time (25ms), should be partway
        int samplesAt50Percent = msToSamples(smoothingMs * 0.5f, sampleRate);
        advanceSamples(engine, samplesAt50Percent);
        float posAt50Percent = engine.getSmoothedX();
        REQUIRE(posAt50Percent > 0.3f);  // Should have moved significantly
        REQUIRE(posAt50Percent < 0.95f);  // But not complete

        // At 100% of target time, should be at or very near target
        int additionalSamples = msToSamples(smoothingMs * 0.5f, sampleRate);
        advanceSamples(engine, additionalSamples);
        float posAt100Percent = engine.getSmoothedX();
        REQUIRE(posAt100Percent > 0.95f);
    }

    SECTION("200ms smoothing") {
        constexpr float smoothingMs = 200.0f;

        auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
        auto& engine = *engineOwner;
        prepareTestEngine(engine, sampleRate);
        auto nodes = createTwoNodes();
        engine.setNodes(nodes, 2);
        engine.setSmoothingTime(smoothingMs);

        engine.setMorphPosition(0.0f, 0.0f);
        advanceSamples(engine, 1000);

        engine.setMorphPosition(1.0f, 0.0f);

        // At 50% of target time, should be partway
        int samplesAt50Percent = msToSamples(smoothingMs * 0.5f, sampleRate);
        advanceSamples(engine, samplesAt50Percent);
        float posAt50Percent = engine.getSmoothedX();
        REQUIRE(posAt50Percent > 0.3f);
        REQUIRE(posAt50Percent < 0.95f);

        // At 100% of target time, should be at or very near target
        int additionalSamples = msToSamples(smoothingMs * 0.5f, sampleRate);
        advanceSamples(engine, additionalSamples);
        float posAt100Percent = engine.getSmoothedX();
        REQUIRE(posAt100Percent > 0.95f);
    }
}

// =============================================================================
// SC-007: Rapid Automation Tests
// =============================================================================

TEST_CASE("MorphEngine handles 20Hz LFO modulation without artifacts", "[morph][transition][SC-007]") {
    constexpr double sampleRate = 44100.0;
    constexpr float smoothingMs = 5.0f;  // Fast smoothing for rapid modulation

    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    prepareTestEngine(engine, sampleRate);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(smoothingMs);

    // 20Hz = 50ms period = 2205 samples at 44.1kHz
    constexpr int samplesPerPeriod = 2205;
    constexpr int numPeriods = 5;

    std::vector<float> output;
    output.reserve(static_cast<size_t>(samplesPerPeriod) * numPeriods);

    // Simulate sine wave LFO at 20Hz
    for (int i = 0; i < samplesPerPeriod * numPeriods; ++i) {
        // LFO position oscillates between 0 and 1
        float lfoPhase = static_cast<float>(i) / static_cast<float>(samplesPerPeriod);
        float lfoValue = (std::sin(lfoPhase * 2.0f * std::numbers::pi_v<float>) + 1.0f) * 0.5f;

        engine.setMorphPosition(lfoValue, 0.0f);
        float out = engine.process(0.5f);
        output.push_back(out);
    }

    // Check for clicks
    REQUIRE_FALSE(hasClicks(output, 0.15f));
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("MorphEngine smoothing - handles sample rate changes", "[morph][transition][edge]") {
    // Start at 44.1kHz
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    prepareTestEngine(engine, 44100.0);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setSmoothingTime(100.0f);

    engine.setMorphPosition(0.0f, 0.0f);
    advanceSamples(engine, 100);

    // Change to 96kHz (simulating sample rate change)
    engine.prepare(96000.0, 512);
    engine.setNodes(nodes, 2);
    engine.setSmoothingTime(100.0f);

    engine.setMorphPosition(1.0f, 0.0f);

    // Process for 100ms at 96kHz
    int samples100ms = msToSamples(100.0f, 96000.0);
    advanceSamples(engine, samples100ms);

    // Should be at or near target
    REQUIRE(engine.getSmoothedX() > 0.95f);
}

TEST_CASE("MorphEngine smoothing - handles extreme smoothing times", "[morph][transition][edge]") {
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);

    SECTION("minimum smoothing (0ms)") {
        engine.setSmoothingTime(0.0f);
        engine.setMorphPosition(0.0f, 0.0f);
        advanceSamples(engine, 10);
        engine.setMorphPosition(1.0f, 0.0f);
        advanceSamples(engine, 10);
        REQUIRE(engine.getSmoothedX() == Approx(1.0f).margin(0.01f));
    }

    SECTION("maximum smoothing (500ms)") {
        engine.setSmoothingTime(500.0f);
        engine.setMorphPosition(0.0f, 0.0f);
        advanceSamples(engine, 2000);  // Let it fully settle
        engine.setMorphPosition(1.0f, 0.0f);

        // After 100ms (20% of 500ms), should be partway through
        advanceSamples(engine, msToSamples(100.0f, 44100.0));
        float posAt100ms = engine.getSmoothedX();
        REQUIRE(posAt100ms < 0.9f);  // Should not be complete yet
        REQUIRE(posAt100ms > 0.0f);  // But should have started moving
    }
}

TEST_CASE("MorphEngine smoothing - reset clears smoother state", "[morph][transition][edge]") {
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setSmoothingTime(100.0f);

    // Move to position 1
    engine.setMorphPosition(1.0f, 0.0f);
    advanceSamples(engine, msToSamples(50.0f, 44100.0));

    // Should be partway through transition
    float midTransition = engine.getSmoothedX();
    REQUIRE(midTransition > 0.0f);
    REQUIRE(midTransition < 1.0f);

    // Reset should clear state
    engine.reset();

    // Position should snap to target
    REQUIRE(engine.getSmoothedX() == Approx(1.0f).margin(0.01f));
}

// =============================================================================
// Cross-family blend law (fix plan T1.13: D-139, D-097, D-008, D-155, D-012, D-120)
// =============================================================================

// The cross-family gains follow a constant-voltage law (g_i = w_i, sum 1), not the
// spec's "equal-power crossfade to maintain perceived loudness": that wording presumes
// incoherent sources, but cross-family node outputs of the same input are coherent
// (SoftClip/SineFold correlation 0.987 at drive 3, 0.9987 at drive 0.5), so sqrt(w) adds
// +3 dB at the midpoint and fails SC-002's 1 dB ("Probe: T1.13 cross-family blend law
// candidates").
TEST_CASE("Cross-family sweep is level-flat and click-free", "[disrumpo][morph][SC-002][D-139]") {
    // SC-002: within 1 dB of the straight dB line between the two end levels at every
    // 10 ms window of a 2 s cursor sweep; SC-003: no clicks.
    SECTION("2 nodes SoftClip -> SineFold, Linear1D, x 0 -> 1") {
        const auto r = crossFamilySweep(xfTwoNodes(), 2, Disrumpo::MorphMode::Linear1D, false);
        WARN("2-node sweep: start " << r.startDb << " dB, end " << r.endDb << " dB, max deviation "
             << r.maxDevDb << " dB, clicks " << r.clicks);
        CHECK(std::abs(r.maxDevDb) <= 1.0);
        CHECK(r.clicks == 0);
    }
    SECTION("4 nodes SoftClip/SineFold/FullRectify/RingSat, Planar2D, (0,0) -> (1,1): clicks") {
        // The level of this sweep is asserted by the [!shouldfail] case below.
        const auto r = crossFamilySweep(xfFourNodes(), 4, Disrumpo::MorphMode::Planar2D, true);
        WARN("4-node sweep: start " << r.startDb << " dB, end " << r.endDb << " dB, max deviation "
             << r.maxDevDb << " dB, clicks " << r.clicks);
        CHECK(r.clicks == 0);
    }
}

// The linear law reads -2.06 dB here because FullRectify's output is incoherent with the
// others; SC-002 as written is not achievable with any fixed per-weight law - awaiting the
// ruling in specs/disrumpo-audit/follow-ups.md item 12 (re-scope SC-002 for >=3-node mixes
// vs an adaptive power-matched blend). [!shouldfail] turns red when this passes.
TEST_CASE("Cross-family 4-node diagonal sweep is level-flat",
          "[disrumpo][morph][SC-002][D-139][!shouldfail]") {
    const auto r = crossFamilySweep(xfFourNodes(), 4, Disrumpo::MorphMode::Planar2D, true);
    WARN("4-node sweep: start " << r.startDb << " dB, end " << r.endDb << " dB, max deviation "
         << r.maxDevDb << " dB");
    CHECK(std::abs(r.maxDevDb) <= 1.0);
}

TEST_CASE("Transition gain is continuous in weight", "[disrumpo][morph][D-008]") {
    using Disrumpo::MorphEngine;
    // Exact endpoints (by branch, not by arithmetic)
    REQUIRE(MorphEngine::transitionGainForWeight(0.0f) == 0.0f);
    REQUIRE(MorphEngine::transitionGainForWeight(1.0f) == 1.0f);

    const float across04 = std::abs(MorphEngine::transitionGainForWeight(0.3999f) -
                                    MorphEngine::transitionGainForWeight(0.4001f));
    float maxStep = 0.0f;
    float maxStepAt = 0.0f;
    int decreases = 0;
    float prev = MorphEngine::transitionGainForWeight(0.0f);
    for (int k = 1; k <= 10000; ++k) {
        const float w = static_cast<float>(k) * 1e-4f;
        const float g = MorphEngine::transitionGainForWeight(w);
        if (g < prev) ++decreases;
        if (std::abs(g - prev) > maxStep) {
            maxStep = std::abs(g - prev);
            maxStepAt = w;
        }
        prev = g;
    }
    WARN("gain step across w = 0.4: " << across04 << "; largest step on the 1e-4 grid " << maxStep
         << " at w = " << maxStepAt << "; decreasing steps " << decreases);
    CHECK(across04 < 0.01f);
    CHECK(decreases == 0);

    SECTION("summed level of a cross-family pair is continuous in the cursor") {
        // x 0.40 -> 0.60 in 0.001 steps crosses wB = 0.4 (x = 0.4495) and wA = 0.4 (x = 0.5505)
        auto engine = makeXfEngine(xfTwoNodes(), 2, Disrumpo::MorphMode::Linear1D, 0.0f);
        engine->setMorphPosition(0.4f, 0.0f);
        (*engine).reset();
        constexpr size_t kWin = 480;  // 2 periods of 200 Hz
        std::vector<float> y(2 * kWin);
        size_t n = 0;
        double prevDb = 0.0;
        double maxJumpDb = 0.0;
        float maxJumpX = 0.0f;
        for (int k = 0; k <= 200; ++k) {
            const float x = 0.4f + static_cast<float>(k) * 0.001f;
            engine->setMorphPosition(x, 0.0f);
            for (size_t i = 0; i < y.size(); ++i, ++n) y[i] = engine->process(xfInput(n));
            const double db = rmsDb(y, kWin, kWin);
            if (k > 0 && std::abs(db - prevDb) > maxJumpDb) {
                maxJumpDb = std::abs(db - prevDb);
                maxJumpX = x;
            }
            prevDb = db;
        }
        WARN("largest level jump between adjacent cursor steps (0.001): " << maxJumpDb
             << " dB at x = " << maxJumpX);
        CHECK(maxJumpDb < 0.1);
    }
}

TEST_CASE("Cursor parked on a node after a mode switch plays that node at unity",
          "[disrumpo][morph][D-155][D-012]") {
    constexpr size_t kWin = 960;  // 4 periods of 200 Hz
    const std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> d155 = {
        xfNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip, 3.0f),
        xfNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::FullRectify, 3.0f),
        xfNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::SineFold, 3.0f),
        xfNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::RingSaturation, 3.0f)};

    SECTION("Planar2D (0, 0.9) then Linear1D") {
        auto engine = makeXfEngine(d155, 4, Disrumpo::MorphMode::Planar2D, 10.0f);
        engine->setMorphPosition(0.0f, 0.9f);
        std::vector<float> y(4000);
        for (size_t n = 0; n < 2000; ++n) y[n] = engine->process(xfInput(n));
        engine->setMode(Disrumpo::MorphMode::Linear1D);
        for (size_t n = 2000; n < 4000; ++n) y[n] = engine->process(xfInput(n));
        const double level = rmsDb(y, 4000 - kWin, kWin);
        const double ref = adapterAloneDb(d155[0], 4000, 4000 - kWin, kWin);
        WARN("after Planar2D -> Linear1D on node A: " << level << " dB, SoftClip alone " << ref
             << " dB, delta " << (level - ref) << " dB");
        CHECK(std::abs(level - ref) <= 0.5);
    }
    SECTION("fresh Linear1D 4-node band arriving at x = 0") {
        auto engine = makeXfEngine(d155, 4, Disrumpo::MorphMode::Linear1D, 10.0f);
        engine->setMorphPosition(0.0f, 0.0f);
        std::vector<float> y(4000);
        for (size_t n = 0; n < y.size(); ++n) y[n] = engine->process(xfInput(n));
        const double level = rmsDb(y, 4000 - kWin, kWin);
        const double ref = adapterAloneDb(d155[0], 4000, 4000 - kWin, kWin);
        WARN("fresh Linear1D at x = 0: " << level << " dB, SoftClip alone " << ref << " dB, delta "
             << (level - ref) << " dB");
        CHECK(std::abs(level - ref) <= 0.5);
    }
    SECTION("node re-activated under a parked cursor (D-012), then off and back on") {
        const std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes = {
            xfNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip, 3.0f),
            xfNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::SoftClip, 3.0f),
            xfNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Bitcrush, 3.0f),
            xfNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold, 3.0f)};
        auto engine = makeXfEngine(nodes, 3, Disrumpo::MorphMode::Planar2D, 0.0f);
        std::vector<float> y(10000);
        size_t n = 0;
        const auto run = [&](size_t count) {
            for (size_t i = 0; i < count; ++i, ++n) y[n] = engine->process(xfInput(n));
        };
        engine->setMorphPosition(1.0f, 0.0f);  // park on B
        run(2000);
        engine->setNodes(nodes, 2);             // C leaves
        engine->setMorphPosition(0.0f, 1.0f);   // C's corner, not an active node now
        run(2000);
        engine->setNodes(nodes, 3);             // C returns under the cursor
        run(2000);
        const double onC = rmsDb(y, 6000 - kWin, kWin);
        const double refOnC = adapterAloneDb(nodes[2], 6000, 6000 - kWin, kWin);
        engine->setMorphPosition(0.5f, 0.5f);
        run(2000);
        engine->setMorphPosition(0.0f, 1.0f);
        run(2000);
        const double backOnC = rmsDb(y, 10000 - kWin, kWin);
        const double refBack = adapterAloneDb(nodes[2], 10000, 10000 - kWin, kWin);
        WARN("C re-activated under the cursor: " << onC << " dB vs Bitcrush alone " << refOnC
             << " dB; off and back on: " << backOnC << " dB vs " << refBack << " dB");
        CHECK(std::abs(onC - refOnC) <= 0.5);
        CHECK(std::abs(backOnC - refBack) <= 0.5);
    }
    SECTION("parked node changes type and drive") {
        auto nodes = xfTwoNodes();
        auto engine = makeXfEngine(nodes, 2, Disrumpo::MorphMode::Linear1D, 10.0f);
        engine->setMorphPosition(0.0f, 0.0f);
        std::vector<float> y(6000);
        size_t n = 0;
        const auto run = [&](size_t count) {
            for (size_t i = 0; i < count; ++i, ++n) y[n] = engine->process(xfInput(n));
        };
        run(2000);
        const double before = rmsDb(y, 2000 - kWin, kWin);
        const double refBefore = adapterAloneDb(nodes[0], 2000, 2000 - kWin, kWin);
        nodes[0].type = Disrumpo::DistortionType::Bitcrush;
        engine->setNodes(nodes, 2);
        run(2000);
        const double afterType = rmsDb(y, 4000 - kWin, kWin);
        const double refType = adapterAloneDb(nodes[0], 4000, 4000 - kWin, kWin);
        nodes[0].commonParams.drive = 6.0f;
        engine->setNodes(nodes, 2);
        run(2000);
        const double afterDrive = rmsDb(y, 6000 - kWin, kWin);
        const double refDrive = adapterAloneDb(nodes[0], 6000, 6000 - kWin, kWin);
        WARN("parked on A: SoftClip " << before << " dB (alone " << refBefore << "), -> Bitcrush "
             << afterType << " dB (alone " << refType << "), -> drive 6 " << afterDrive
             << " dB (alone " << refDrive << ")");
        CHECK(std::abs(before - refBefore) <= 0.5);
        CHECK(std::abs(afterType - refType) <= 0.5);
        CHECK(std::abs(afterDrive - refDrive) <= 0.5);
    }
}

TEST_CASE("Cross-family morph automation is click-free at every smoothing time",
          "[disrumpo][morph][SC-003][D-139]") {
    for (const float ms : {0.0f, 5.0f, 10.0f, 50.0f}) {
        auto engine = makeXfEngine(xfTwoNodes(), 2, Disrumpo::MorphMode::Linear1D, ms);
        engine->setMorphPosition(0.0f, 0.0f);
        (*engine).reset();
        const size_t hold = static_cast<size_t>(kXfRate * 0.25);
        const size_t ramp = static_cast<size_t>(kXfRate);
        std::vector<float> y(hold + 2 * ramp + hold);
        for (size_t n = 0; n < y.size(); ++n) {
            float x = 0.0f;
            if (n >= hold && n < hold + ramp) x = static_cast<float>(n - hold) / static_cast<float>(ramp);
            else if (n >= hold + ramp && n < hold + 2 * ramp)
                x = 1.0f - static_cast<float>(n - hold - ramp) / static_cast<float>(ramp);
            engine->setMorphPosition(x, 0.0f);
            y[n] = engine->process(xfInput(n));
        }
        const size_t clicks = xfClickCount(y, 0, y.size());
        WARN("smoothing " << ms << " ms: clicks " << clicks);
        CHECK(clicks == 0);
    }
}

// D-031 (fix plan T2.12): processCrossFamily re-applied node params plus the modulation
// offsets only while an offset was non-zero, so offsets returning to exactly 0 left the
// last modulated drive in the node adapters until the next setNodes.
TEST_CASE("Cross-family adapters return to the base params when the mod offsets return to 0",
          "[disrumpo][morph][modulation][D-031]") {
    const auto nodes = xfTwoNodes();
    auto modulated = makeXfEngine(nodes, 2, Disrumpo::MorphMode::Linear1D, 10.0f);
    auto reference = makeXfEngine(nodes, 2, Disrumpo::MorphMode::Linear1D, 10.0f);
    for (auto* e : {modulated.get(), reference.get()}) {
        e->setMorphPosition(0.5f, 0.0f);
        e->reset();
    }
    REQUIRE_FALSE(modulated->getAllSameType());

    const size_t total = static_cast<size_t>(kXfRate);  // 1 s
    std::vector<float> yMod(total);
    std::vector<float> yRef(total);
    for (size_t start = 0; start < total; start += kXfBlock) {
        // One block with every offset non-zero, then all four exactly 0
        const bool first = start == 0;
        modulated->setDriveMixModOffset(first ? 0.5f : 0.0f, first ? -0.25f : 0.0f);
        modulated->setToneBiasModOffset(first ? 0.25f : 0.0f, first ? 0.25f : 0.0f);
        const size_t end = std::min(total, start + static_cast<size_t>(kXfBlock));
        for (size_t n = start; n < end; ++n) {
            yMod[n] = modulated->process(xfInput(n));
            yRef[n] = reference->process(xfInput(n));
        }
    }

    // The second half: the filters have long forgotten the modulated block
    double maxDiff = 0.0;
    for (size_t n = total / 2; n < total; ++n)
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(yMod[n] - yRef[n])));
    std::printf("T2.12 offsets back to 0: adapter drive A %.4f B %.4f (base %.4f), mix A %.4f, tone A %.1f Hz, "
                "bias A %.4f; max |modulated - never modulated| over 0.5-1 s %.3g\n",
                static_cast<double>(modulated->getAdapter(0).getCommonParams().drive),
                static_cast<double>(modulated->getAdapter(1).getCommonParams().drive),
                static_cast<double>(nodes[0].commonParams.drive),
                static_cast<double>(modulated->getAdapter(0).getCommonParams().mix),
                static_cast<double>(modulated->getAdapter(0).getCommonParams().toneHz),
                static_cast<double>(modulated->getAdapter(0).getParams().bias), maxDiff);

    for (int i = 0; i < 2; ++i) {
        INFO("node " << i);
        const auto& cp = modulated->getAdapter(i).getCommonParams();
        REQUIRE(cp.drive == nodes[static_cast<size_t>(i)].commonParams.drive);
        REQUIRE(cp.mix == nodes[static_cast<size_t>(i)].commonParams.mix);
        REQUIRE(cp.toneHz == nodes[static_cast<size_t>(i)].commonParams.toneHz);
        REQUIRE(modulated->getAdapter(i).getParams().bias == nodes[static_cast<size_t>(i)].params.bias);
    }
    REQUIRE(maxDiff < 1e-5);
}

// Measurement probe (hidden, fix plan T1.13): the summed level of a cross-family sweep
// under three blend laws, computed offline from each node's adapter output rendered
// alone (every node runs continuously). "old" = w * transition gain (pre-T1.13),
// "linear" = w (the T1.13 law), "sqrt" = sqrt(w) (equal power for uncorrelated outputs),
// "power-matched (ideal)" = output power set to sum(w_i * P_i) (not implemented).
TEST_CASE("Probe: T1.13 cross-family blend law candidates", "[.][t113-law-probe]") {
    const auto oldGain = [](float w) {
        constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;
        if (w >= 0.6f) return 1.0f;
        if (w <= 0.001f) return 0.0f;
        if (w < 0.4f) return std::sin(w / 0.4f * kHalfPi);
        return std::sin((w - 0.4f) / 0.2f * kHalfPi);
    };
    struct Setup {
        const char* name = "";
        std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
        int count = 2;
        Disrumpo::MorphMode mode = Disrumpo::MorphMode::Linear1D;
        bool diagonal = false;
    };
    const Setup setups[] = {
        {.name = "neutral drive 0 (identical outputs)", .nodes = xfTwoNodes(0.0f)},
        {.name = "SoftClip/SineFold drive 0.5", .nodes = xfTwoNodes(0.5f)},
        {.name = "SoftClip/SineFold drive 3", .nodes = xfTwoNodes(3.0f)},
        {.name = "4-node diagonal drive 3", .nodes = xfFourNodes(), .count = 4,
         .mode = Disrumpo::MorphMode::Planar2D, .diagonal = true},
    };
    constexpr size_t kLen = 48000;
    constexpr size_t kSkip = 9600;  // settle
    for (const auto& s : setups) {
        std::vector<std::vector<float>> streams;
        for (int i = 0; i < s.count; ++i) {
            auto a = std::make_unique<Disrumpo::DistortionAdapter>();
            a->prepare(kXfRate, kXfBlock);
            a->setType(s.nodes[static_cast<size_t>(i)].type);
            a->setParams(s.nodes[static_cast<size_t>(i)].params);
            a->setCommonParams(s.nodes[static_cast<size_t>(i)].commonParams);
            std::vector<float> y(kLen);
            for (size_t n = 0; n < kLen; ++n) y[n] = a->process(xfInput(n));
            streams.push_back(std::move(y));
        }
        if (s.count == 2) {
            double sab = 0.0;
            double saa = 0.0;
            double sbb = 0.0;
            for (size_t n = kSkip; n < kLen; ++n) {
                sab += static_cast<double>(streams[0][n]) * streams[1][n];
                saa += static_cast<double>(streams[0][n]) * streams[0][n];
                sbb += static_cast<double>(streams[1][n]) * streams[1][n];
            }
            WARN("PROBE t113 " << s.name << ": correlation " << sab / std::sqrt(saa * sbb));
        }
        auto engine = makeXfEngine(s.nodes, s.count, s.mode, 0.0f);
        for (int law = 0; law < 4; ++law) {
            const auto levelAt = [&](float x) {
                engine->calculateMorphWeights(x, s.diagonal ? x : 0.0f);
                const auto w = engine->getWeights();
                std::vector<float> mix(kLen - kSkip, 0.0f);
                double targetPower = 0.0;  // law 3: sum of w_i * P_i (ideal power-matched blend)
                for (int i = 0; i < s.count; ++i) {
                    const float wi = w[static_cast<size_t>(i)];
                    float g = wi;  // linear, and the mix of law 3 (unused there)
                    if (law == 0) g = wi * oldGain(wi);
                    if (law == 2) g = std::sqrt(wi);
                    for (size_t n = kSkip; n < kLen; ++n) mix[n - kSkip] += g * streams[static_cast<size_t>(i)][n];
                    targetPower += wi * std::pow(10.0, rmsDb(streams[static_cast<size_t>(i)], kSkip, kLen - kSkip) / 10.0);
                }
                if (law == 3) return 10.0 * std::log10(targetPower);
                return rmsDb(mix, 0, mix.size());
            };
            const double l0 = levelAt(0.0f);
            const double l1 = levelAt(1.0f);
            double maxDev = 0.0;
            float at = 0.0f;
            for (int k = 1; k < 200; ++k) {
                const float x = static_cast<float>(k) / 200.0f;
                const double dev = levelAt(x) - (l0 + (l1 - l0) * x);
                if (std::abs(dev) > std::abs(maxDev)) {
                    maxDev = dev;
                    at = x;
                }
            }
            static constexpr const char* kLaw[] = {"old", "linear", "sqrt", "power-matched (ideal)"};
            WARN("PROBE t113 " << s.name << " law " << kLaw[law] << ": ends " << l0 << " / " << l1
                 << " dB, max deviation from the dB line " << maxDev << " dB at x = " << at);
        }
        const auto r = crossFamilySweep(s.nodes, s.count, s.mode, s.diagonal);
        WARN("PROBE t113 " << s.name << " real engine sweep: ends " << r.startDb << " / " << r.endDb
             << " dB, max deviation " << r.maxDevDb << " dB, clicks " << r.clicks);
    }
}

// =============================================================================
// Same-family, different-type morph (fix plan T1.14: D-028)
// =============================================================================

namespace {

constexpr float kSfAmp = 0.5f;  // -6 dBFS

float sfInput(size_t n, float freq) {
    return kSfAmp * std::sin(2.0f * std::numbers::pi_v<float> * freq *
                             static_cast<float>(static_cast<double>(n) / kXfRate));
}

/// Two nodes of one family: @p a at x = 0, @p b at x = 1, drive 4, mix 1.
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> sfPair(Disrumpo::DistortionType a,
                                                                  Disrumpo::DistortionType b) {
    return {xfNode(0, 0.0f, 0.0f, a, 4.0f), xfNode(1, 1.0f, 0.0f, b, 4.0f),
            xfNode(2, 0.0f, 1.0f, a, 4.0f), xfNode(3, 1.0f, 1.0f, b, 4.0f)};
}

float maxStep(const std::vector<float>& y, size_t from, size_t count) {
    float m = 0.0f;
    for (size_t i = from + 1; i < from + count; ++i) m = std::max(m, std::abs(y[i] - y[i - 1]));
    return m;
}

/// THD (%) of y[from, from + count) at @p f0, harmonics 2..10, by direct DFT at each
/// harmonic; @p count must hold whole periods of @p f0.
double harmonicThdPct(const std::vector<float>& y, size_t from, size_t count, double f0) {
    const auto mag = [&](int h) {
        double re = 0.0;
        double im = 0.0;
        const double w = 2.0 * std::numbers::pi * f0 * static_cast<double>(h) / kXfRate;
        for (size_t i = 0; i < count; ++i) {
            const double ph = w * static_cast<double>(i);
            re += static_cast<double>(y[from + i]) * std::cos(ph);
            im -= static_cast<double>(y[from + i]) * std::sin(ph);
        }
        return std::sqrt(re * re + im * im);
    };
    const double h1 = mag(1);
    double hs = 0.0;
    for (int h = 2; h <= 10; ++h) {
        const double m = mag(h);
        hs += m * m;
    }
    return h1 > 0.0 ? 100.0 * std::sqrt(hs) / h1 : 0.0;
}

} // anonymous namespace

// FR-MORPH-003 (artifact-free transitions) and FR-MORPH-004/006: two different types of
// one family are blended, not switched at the dominance boundary.
TEST_CASE("Same-family different-type morph has no step at the midpoint", "[disrumpo][morph][D-028]") {
    const auto run = [](Disrumpo::DistortionType a, Disrumpo::DistortionType b, const char* label) {
        // 0.5 s held at x = 0.4, 1 s cursor sweep 0.4 -> 0.6 at block rate, 0.5 s held at 0.6
        auto engine = makeXfEngine(sfPair(a, b), 2, Disrumpo::MorphMode::Linear1D, 10.0f);
        engine->setMorphPosition(0.4f, 0.0f);
        (*engine).reset();
        const auto S = [](double s) { return static_cast<size_t>(s * kXfRate); };
        const size_t total = S(2.0);
        std::vector<float> y(total);
        for (size_t start = 0; start < total; start += kXfBlock) {
            const double t = static_cast<double>(start) / kXfRate;
            float x = 0.4f;
            if (t >= 0.5 && t < 1.5) x = 0.4f + 0.2f * static_cast<float>(t - 0.5);
            else if (t >= 1.5) x = 0.6f;
            engine->setMorphPosition(x, 0.0f);
            const size_t end = std::min(total, start + static_cast<size_t>(kXfBlock));
            for (size_t n = start; n < end; ++n) y[n] = engine->process(sfInput(n, 220.0f));
        }
        const float steadyA = maxStep(y, S(0.1), S(0.4));
        const float steadyB = maxStep(y, S(1.6), S(0.4));
        const float sweep = maxStep(y, S(0.5), S(1.0));
        const size_t clicks = xfClickCount(y, S(0.5), S(1.0));
        WARN(label << ": max |y[n]-y[n-1]| sweep " << sweep << ", held at 0.4 " << steadyA
             << ", held at 0.6 " << steadyB << " (ratio " << sweep / std::max(steadyA, steadyB)
             << "), clicks " << clicks);
        CHECK(clicks == 0);
        CHECK(sweep <= 2.0f * std::max(steadyA, steadyB));
    };
    SECTION("Saturation: SoftClip -> HardClip") {
        run(Disrumpo::DistortionType::SoftClip, Disrumpo::DistortionType::HardClip, "SoftClip/HardClip");
    }
    SECTION("Experimental: Chaos -> Formant") {
        run(Disrumpo::DistortionType::Chaos, Disrumpo::DistortionType::Formant, "Chaos/Formant");
    }
}

TEST_CASE("Same-family different-type morph level and THD are continuous in the cursor",
          "[disrumpo][morph][D-028]") {
    // x 0.40 -> 0.60 in 0.001 steps, 200 Hz -6 dBFS (240 samples per period): level and THD
    // of the last 4 periods of each step.
    auto engine = makeXfEngine(sfPair(Disrumpo::DistortionType::SoftClip, Disrumpo::DistortionType::HardClip),
                               2, Disrumpo::MorphMode::Linear1D, 0.0f);
    engine->setMorphPosition(0.4f, 0.0f);
    (*engine).reset();
    constexpr size_t kWin = 960;
    std::vector<float> y(2 * kWin);
    size_t n = 0;
    double prevDb = 0.0;
    double prevThd = 0.0;
    double maxJumpDb = 0.0;
    double maxJumpThd = 0.0;
    float maxJumpDbX = 0.0f;
    float maxJumpThdX = 0.0f;
    double thdStart = 0.0;
    double thdEnd = 0.0;
    for (int k = 0; k <= 200; ++k) {
        const float x = 0.4f + static_cast<float>(k) * 0.001f;
        engine->setMorphPosition(x, 0.0f);
        for (size_t i = 0; i < y.size(); ++i, ++n) y[i] = engine->process(sfInput(n, 200.0f));
        const double db = rmsDb(y, kWin, kWin);
        const double thd = harmonicThdPct(y, kWin, kWin, 200.0);
        if (k == 0) thdStart = thd;
        thdEnd = thd;
        if (k > 0 && std::abs(db - prevDb) > maxJumpDb) {
            maxJumpDb = std::abs(db - prevDb);
            maxJumpDbX = x;
        }
        if (k > 0 && std::abs(thd - prevThd) > maxJumpThd) {
            maxJumpThd = std::abs(thd - prevThd);
            maxJumpThdX = x;
        }
        prevDb = db;
        prevThd = thd;
    }
    WARN("SoftClip/HardClip, 0.001 cursor steps: largest level jump " << maxJumpDb << " dB at x = "
         << maxJumpDbX << "; largest THD jump " << maxJumpThd << " % at x = " << maxJumpThdX
         << " (THD " << thdStart << " % at x = 0.4, " << thdEnd << " % at x = 0.6)");
    CHECK(maxJumpDb < 0.1);
    CHECK(maxJumpThd < 0.5);
}

TEST_CASE("Same-family different-type morph automation is click-free at every smoothing time",
          "[disrumpo][morph][SC-003][D-028]") {
    for (const float ms : {0.0f, 5.0f, 10.0f, 50.0f}) {
        auto engine = makeXfEngine(sfPair(Disrumpo::DistortionType::SoftClip, Disrumpo::DistortionType::HardClip),
                                   2, Disrumpo::MorphMode::Linear1D, ms);
        engine->setMorphPosition(0.0f, 0.0f);
        (*engine).reset();
        const size_t hold = static_cast<size_t>(kXfRate * 0.25);
        const size_t ramp = static_cast<size_t>(kXfRate);
        std::vector<float> y(hold + 2 * ramp + hold);
        for (size_t n = 0; n < y.size(); ++n) {
            float x = 0.0f;
            if (n >= hold && n < hold + ramp) x = static_cast<float>(n - hold) / static_cast<float>(ramp);
            else if (n >= hold + ramp && n < hold + 2 * ramp)
                x = 1.0f - static_cast<float>(n - hold - ramp) / static_cast<float>(ramp);
            engine->setMorphPosition(x, 0.0f);
            y[n] = engine->process(sfInput(n, 220.0f));
        }
        const size_t clicks = xfClickCount(y, 0, y.size());
        WARN("SoftClip/HardClip, smoothing " << ms << " ms: clicks " << clicks);
        CHECK(clicks == 0);
    }
}

// Measurement probe (hidden, fix plan T1.14): MorphEngine cost of a two-node morph,
// identical types (one interpolated processor) vs different types of one family.
TEST_CASE("Probe: T1.14 same-family morph cost", "[.][t114-cpu-probe]") {
    const auto cost = [](Disrumpo::DistortionType a, Disrumpo::DistortionType b) {
        auto engine = makeXfEngine(sfPair(a, b), 2, Disrumpo::MorphMode::Linear1D, 10.0f);
        engine->setMorphPosition(0.5f, 0.0f);
        (*engine).reset();
        const size_t total = static_cast<size_t>(kXfRate * 10.0);
        float sink = 0.0f;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t n = 0; n < total; ++n) sink += engine->process(sfInput(n, 220.0f));
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();
        return std::pair<double, float>(secs / 10.0 * 100.0, sink);
    };
    const auto same = cost(Disrumpo::DistortionType::SoftClip, Disrumpo::DistortionType::SoftClip);
    const auto diff = cost(Disrumpo::DistortionType::SoftClip, Disrumpo::DistortionType::HardClip);
    WARN("PROBE t114 cpu (one channel, 48 kHz, x = 0.5): SoftClip/SoftClip " << same.first
         << " % of real time, SoftClip/HardClip " << diff.first << " % (sinks " << same.second
         << ", " << diff.second << ")");
}
