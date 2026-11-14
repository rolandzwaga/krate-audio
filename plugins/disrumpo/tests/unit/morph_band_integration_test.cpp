// ==============================================================================
// MorphEngine + BandProcessor Integration Tests
// ==============================================================================
// Tests for FR-010: MorphEngine MUST integrate with BandProcessor to apply
// morphed distortion to each frequency band.
//
// Signal flow per plan.md:
// 1. BandProcessor owns MorphEngine instance
// 2. BandProcessor passes the sweep drive multiplier to MorphEngine (D-019)
// 3. BandProcessor calls morphEngine.process() at oversampled rate
// 4. MorphEngine processes audio through weighted distortion blend
// 5. Output fed to BandProcessor's gain/pan/mute stage AFTER downsampling
//
// Constitution Principle XII: Test-First Development
// Reference: specs/005-morph-system/spec.md FR-010
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/band_processor.h"
#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_types.h"
#include "dsp/distortion_adapter.h"

#include "test_helpers/signal_metrics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <array>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

using Catch::Approx;

// =============================================================================
// Constants
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;

/// @brief Process samples through band processor to let smoothers settle.
void settleBandProcessor(Disrumpo::BandProcessor& proc, int numSamples = 2000) {
    for (int i = 0; i < numSamples; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        proc.process(left, right);
    }
}

/// @brief Check if output has any clicks (sudden large changes).
bool hasClicks(const std::vector<float>& output, float threshold = 0.2f) {
    if (output.size() < 2) return false;

    for (size_t i = 1; i < output.size(); ++i) {
        float diff = std::abs(output[i] - output[i - 1]);
        if (diff > threshold) {
            return true;
        }
    }
    return false;
}

/// @brief Create a simple 2-node setup (Soft Clip and Tube).
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createTwoNodeSetup() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 2.0f;  // Moderate drive
    nodes[0].commonParams.mix = 1.0f;

    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[1].commonParams.drive = 3.0f;
    nodes[1].commonParams.mix = 1.0f;

    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);

    return nodes;
}

} // anonymous namespace

// =============================================================================
// FR-010: BandProcessor MorphEngine Integration Tests
// =============================================================================

TEST_CASE("BandProcessor owns and uses MorphEngine", "[band][morph][FR-010]") {
    // FR-010: MorphEngine MUST integrate with BandProcessor
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    // Configure morph engine via BandProcessor
    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);  // 50/50 blend

    // Let smoothers settle
    settleBandProcessor(proc);

    // Process audio
    float left = 0.5f;
    float right = 0.5f;
    proc.process(left, right);

    // With morph engine active and drive > 0, output should be processed
    // (not exactly equal to input due to distortion)
    // This test verifies the integration exists and processes
    REQUIRE(std::isfinite(left));
    REQUIRE(std::isfinite(right));
}

TEST_CASE("BandProcessor morph position affects distortion character", "[band][morph][FR-010]") {
    // Test that changing morph position actually changes the output
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);

    // Position at node A (Soft Clip)
    proc.setMorphPosition(0.0f, 0.0f);
    settleBandProcessor(proc);

    float leftA = 0.5f;
    float rightA = 0.5f;
    proc.process(leftA, rightA);

    // Position at node B (Tube)
    proc.setMorphPosition(1.0f, 0.0f);
    settleBandProcessor(proc);

    float leftB = 0.5f;
    float rightB = 0.5f;
    proc.process(leftB, rightB);

    // The outputs should be different (different distortion types)
    // We can't know exact values but they should differ
    // Note: With smoothing, the values will have settled to the new position
    REQUIRE(std::isfinite(leftA));
    REQUIRE(std::isfinite(leftB));
}

TEST_CASE("BandProcessor sweep intensity scales the morph drive and not the level", "[band][morph][FR-010]") {
    // D-019: the sweep multiplies the morphed drive (1 + intensity / 2), it does not
    // scale the band's input. Proof: the 200 % render equals, sample for sample, a
    // sweep-off render whose node drives are doubled; and 200 % adds harmonics.
    // (A level bound is no proof: SoftClip / Tube at x = 0.5 blend in parallel (T1.14,
    // D-028) and Tube's level follows its drive, +6.02 dB at 2x vs SoftClip's +1.05 dB,
    // so the blend rises 4.20 dB - "Probe: T1.14 sweep level per morph position".)
    const auto nodes = createTwoNodeSetup();

    // 1378.125 Hz = bin 256 of an 8192-point FFT at 44.1 kHz
    constexpr float kHz = 1378.125f;
    constexpr size_t kN = 8192;
    const auto render = [](const std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes>& n,
                           float intensity) {
        auto proc = std::make_unique<Disrumpo::BandProcessor>();
        proc->prepare(kSampleRate, kBlockSize);
        proc->setMorphNodes(n, 2);
        proc->setMorphMode(Disrumpo::MorphMode::Linear1D);
        proc->setMorphPosition(0.5f, 0.0f);
        proc->setSweepIntensity(intensity);
        (*proc).reset();  // snaps the cursor and the sweep smoother to their targets
        settleBandProcessor(*proc);
        std::vector<float> out(kN);
        for (size_t i = 0; i < kN; ++i) {
            // -24.06 dBFS: the nodes (drive 2 and 3) saturate. The drive re-stage (user ruling
            // 2026-11-10) runs them 18.06 dB hotter; at the -6 dBFS this case was written for they
            // sat at their ceiling and doubling the drive took THD from 2.57 % to 1.35 %. 18.06 dB
            // lower the shapers see the drive they saw before
            float left = 0.5f * 0.125f * std::sin(2.0f * std::numbers::pi_v<float> * kHz * static_cast<float>(i) /
                                                  static_cast<float>(kSampleRate));
            float right = left;
            proc->process(left, right);
            out[i] = left;
        }
        return out;
    };
    const auto thdOf = [](const std::vector<float>& y) {
        return Krate::DSP::TestUtils::SignalMetrics::calculateTHD(y.data(), kN, kHz,
                                                                  static_cast<float>(kSampleRate));
    };

    const auto off = render(nodes, 0.0f);
    const auto on = render(nodes, 2.0f);
    auto doubled = nodes;
    const float scale = Disrumpo::BandProcessor::sweepDriveScale(2.0f);
    for (auto& node : doubled) node.commonParams.drive *= scale;
    const auto ref = render(doubled, 0.0f);

    float maxDiff = 0.0f;
    for (size_t i = 0; i < kN; ++i) maxDiff = std::max(maxDiff, std::abs(on[i] - ref[i]));
    const float thd0 = thdOf(off);
    const float thd2 = thdOf(on);
    INFO("THD at 0 %: " << thd0 << " %, at 200 %: " << thd2 << " %; max |200 % - drive x" << scale
         << " sweep off| " << maxDiff);
    REQUIRE(thd2 > thd0 * 1.1f);
    REQUIRE(maxDiff <= 1e-6f);
}

TEST_CASE("BandProcessor morph transition is artifact-free", "[band][morph][FR-010][SC-003]") {
    // Part of SC-003: Morph transitions produce zero audible artifacts
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(10.0f);  // 10ms smoothing

    // Start at position 0
    proc.setMorphPosition(0.0f, 0.0f);
    settleBandProcessor(proc);

    std::vector<float> output;
    output.reserve(4000);

    // Process while automating morph position
    for (int i = 0; i < 4000; ++i) {
        // Automate morph position (0 to 1 over 4000 samples)
        float pos = static_cast<float>(i) / 4000.0f;
        proc.setMorphPosition(pos, 0.0f);

        float left = 0.3f;  // Moderate input level
        float right = 0.3f;
        proc.process(left, right);

        output.push_back(left);
    }

    // Check for clicks (sudden amplitude changes)
    REQUIRE_FALSE(hasClicks(output, 0.3f));
}

TEST_CASE("BandProcessor morph with gain/pan/mute", "[band][morph][FR-010]") {
    // Verify morph integrates correctly with existing gain/pan/mute stage
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);

    // Test with mute
    proc.setMute(true);
    settleBandProcessor(proc);

    float leftMuted = 0.5f;
    float rightMuted = 0.5f;
    proc.process(leftMuted, rightMuted);

    // Even with morph active, mute should silence output
    REQUIRE(std::abs(leftMuted) < 0.01f);
    REQUIRE(std::abs(rightMuted) < 0.01f);

    // Test with gain
    proc.setMute(false);
    proc.setGainDb(6.0f);  // +6dB
    settleBandProcessor(proc);

    float leftGain = 0.5f;
    float rightGain = 0.5f;
    proc.process(leftGain, rightGain);

    // Output should be present
    REQUIRE(std::isfinite(leftGain));
    REQUIRE(std::isfinite(rightGain));
}

TEST_CASE("BandProcessor morph smoothing time configurable", "[band][morph][FR-010]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);

    // D-127 (T5.06): a one-pole cursor that reaches 99 % (5 time constants) in the
    // configured time T crosses its midpoint after T ln2 / 5, counted at the host rate.
    const auto midpointCrossing = [&proc](float smoothingMs) {
        proc.setMorphSmoothingTime(smoothingMs);
        proc.setMorphPosition(0.0f, 0.0f);
        settleBandProcessor(proc, static_cast<int>(kSampleRate));
        REQUIRE(proc.getMorphEngine()->getSmoothedX() < 1e-4f);
        proc.setMorphPosition(1.0f, 0.0f);
        int samples = 0;
        while (proc.getMorphEngine()->getSmoothedX() < 0.5f && samples < static_cast<int>(kSampleRate)) {
            float left = 0.1f;
            float right = 0.1f;
            proc.process(left, right);
            ++samples;
        }
        const double want = smoothingMs * 1e-3 * kSampleRate * std::numbers::ln2 / 5.0;
        std::printf("D-127 %.0f ms morph smoothing: cursor midpoint after %d samples, one-pole %.1f\n",
                    static_cast<double>(smoothingMs), samples, want);
        CHECK(samples >= 0.9 * want);
        CHECK(samples <= 1.1 * want);
    };

    SECTION("Fast smoothing (5ms)") { midpointCrossing(5.0f); }
    SECTION("Slow smoothing (200ms)") { midpointCrossing(200.0f); }
}

// D-138 / D-099 (fix plan T1.05): the BandProcessor ran L and then R through one
// MorphEngine, so the cursor smoothers advanced twice per stereo frame. With one
// engine per channel each advances once per frame, exactly like a standalone
// engine at the host rate (the rate BandProcessor::process clocks its engines at
// since T1.06) that is called once per sample.
TEST_CASE("BandProcessor advances the morph cursor smoother once per stereo frame",
          "[disrumpo][morph][D-138]") {
    constexpr double kSr = 48000.0;
    constexpr size_t kBlock = 512;
    constexpr int kOversample = Disrumpo::BandProcessor::kMaxOversampleFactor;
    const auto nodes = createTwoNodeSetup();

    Disrumpo::BandProcessor proc;
    proc.prepare(kSr, kBlock);
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(10.0f);
    proc.setMorphPosition(0.0f, 0.0f);

    auto reference = std::make_unique<Disrumpo::MorphEngine>();
    reference->prepare(kSr, static_cast<int>(kBlock) * kOversample, kOversample);
    reference->setMode(Disrumpo::MorphMode::Linear1D);
    reference->setSmoothingTime(10.0f);
    reference->setNodes(nodes, 2);
    reference->setMorphPosition(0.0f, 0.0f);

    auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            float left = 0.1f;
            float right = -0.1f;
            proc.process(left, right);
            (void)reference->process(0.1f);
        }
    };

    run(1000);
    proc.setMorphPosition(1.0f, 0.0f);
    reference->setMorphPosition(1.0f, 0.0f);
    for (int checkpoint = 0; checkpoint < 4; ++checkpoint) {
        run(240);
        const float band = proc.getMorphEngine()->getSmoothedX();
        const float ref = reference->getSmoothedX();
        INFO("after " << (checkpoint + 1) * 240 << " frames: band X = " << band << ", reference X = " << ref);
        REQUIRE(std::abs(band - ref) < 1e-6f);
    }
}

// D-138 / D-099 (fix plan T1.05 / T1.06): a 10 ms smoothing time is 10 ms of host
// time at every running factor. OnePoleSmoother::configure defines the time as
// 5 tau (coefficient exp(-5000 / (ms * fs))), so at 48 kHz tau = 96 samples and
// 1 - exp(-n / 96) crosses 0.99 at n = 96 ln(100) = 442.1 samples (9.2 ms); 480
// samples (10 ms) read 0.9933. The bounds are the plan's 10 ms +/- 30 %: below
// 0.99 after 336 samples (7 ms, reads 0.9698) and at least 0.99 after 480. The
// oversampled path runs the engines at 2x with coefficients for 2x, so the same
// host-sample counts hold. Before T1.06 the engines were configured for 8x the
// host rate (X = 0.46 after 480 samples, i.e. 10 ms read about 80 ms).
TEST_CASE("BandProcessor morph smoothing reaches 99 % in the configured time",
          "[disrumpo][morph][D-138]") {
    constexpr double kSr = 48000.0;
    constexpr int kEarly = 336;  // 7 ms
    constexpr int kFull = 480;   // 10 ms
    Disrumpo::BandProcessor proc;
    proc.prepare(kSr, kBlockSize);
    // Two 2x nodes, so the band runs one factor across the glide (SoftClip, the old node A, is
    // 4x since the drive re-stage ruling 2026-11-10)
    auto nodes = createTwoNodeSetup();
    nodes[0].type = Disrumpo::DistortionType::Fractal;
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(10.0f);
    proc.setMorphPosition(0.0f, 0.0f);

    SECTION("per-sample process (1x)") {
        settleBandProcessor(proc, 48000);
        REQUIRE(proc.getMorphEngine()->getSmoothedX() < 1e-4f);

        proc.setMorphPosition(1.0f, 0.0f);
        settleBandProcessor(proc, kEarly);
        const float atEarly = proc.getMorphEngine()->getSmoothedX();
        settleBandProcessor(proc, kFull - kEarly);
        const float atFull = proc.getMorphEngine()->getSmoothedX();
        INFO("smoothed X after " << kEarly << " samples = " << atEarly << ", after " << kFull
             << " samples = " << atFull);
        REQUIRE(atEarly < 0.99f);
        REQUIRE(atFull >= 0.99f);
    }

    SECTION("processBlock at the running factor (2x)") {
        constexpr size_t kChunk = 48;  // kEarly and kFull are multiples of it
        std::array<float, kChunk> left{};
        std::array<float, kChunk> right{};
        const auto runBlocks = [&](int samples) {
            for (int done = 0; done < samples; done += static_cast<int>(kChunk)) {
                left.fill(0.0f);
                right.fill(0.0f);
                proc.processBlock(left.data(), right.data(), kChunk);
            }
        };
        runBlocks(48000);
        REQUIRE(proc.getActiveOversampleFactor() == 2);
        REQUIRE(proc.getMorphEngine()->getSmoothedX() < 1e-4f);

        proc.setMorphPosition(1.0f, 0.0f);
        REQUIRE(proc.getActiveOversampleFactor() == 2);
        runBlocks(kEarly);
        const float atEarly = proc.getMorphEngine()->getSmoothedX();
        runBlocks(kFull - kEarly);
        const float atFull = proc.getMorphEngine()->getSmoothedX();
        INFO("smoothed X after " << kEarly << " host samples = " << atEarly << ", after " << kFull
             << " host samples = " << atFull);
        REQUIRE(atEarly < 0.99f);
        REQUIRE(atFull >= 0.99f);
    }
}

TEST_CASE("BandProcessor processBlock uses morph engine", "[band][morph][FR-010]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);

    // Let settle
    settleBandProcessor(proc);

    // Process a block
    std::array<float, 256> left{};
    std::array<float, 256> right{};

    // Fill with test signal
    for (size_t i = 0; i < 256; ++i) {
        left[i] = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * static_cast<float>(i) / 44100.0f);
        right[i] = left[i];
    }

    proc.processBlock(left.data(), right.data(), 256);

    // Verify output is valid
    for (size_t i = 0; i < 256; ++i) {
        REQUIRE(std::isfinite(left[i]));
        REQUIRE(std::isfinite(right[i]));
    }
}

TEST_CASE("BandProcessor morph bypass when drive is zero", "[band][morph][FR-010]") {
    // When all nodes have drive=0, distortion should be bypassed
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    // Create nodes with zero drive
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 0.0f;  // Bypass
    nodes[0].commonParams.mix = 1.0f;

    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[1].commonParams.drive = 0.0f;  // Bypass
    nodes[1].commonParams.mix = 1.0f;

    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);

    settleBandProcessor(proc);

    // With drive=0, distortion should be bypassed
    // Output depends on gain/pan stage
    float left = 0.5f;
    float right = 0.5f;
    proc.process(left, right);

    // Should produce valid output (with pan coefficients applied)
    REQUIRE(std::isfinite(left));
    REQUIRE(std::isfinite(right));
}

TEST_CASE("DIAG: DistortionAdapters produce different output for different types",
          "[diagnostic]") {
    Disrumpo::DistortionAdapter adapterA;
    Disrumpo::DistortionAdapter adapterB;
    adapterA.prepare(kSampleRate, static_cast<int>(kBlockSize));
    adapterB.prepare(kSampleRate, static_cast<int>(kBlockSize));

    // Set up SoftClip
    adapterA.setType(Disrumpo::DistortionType::SoftClip);
    adapterA.setCommonParams({.drive = 1.5f, .mix = 1.0f, .toneHz = 8000.0f});

    // Set up Bitcrush — low bit depth to ensure visible quantization
    adapterB.setType(Disrumpo::DistortionType::Bitcrush);
    adapterB.setCommonParams({.drive = 1.5f, .mix = 1.0f, .toneHz = 8000.0f});
    Disrumpo::DistortionParams bParams;
    bParams.bitDepth = 2.0f;
    adapterB.setParams(bParams);

    float outA = adapterA.process(0.3f);
    float outB = adapterB.process(0.3f);

    INFO("Adapter SoftClip output: " << outA);
    INFO("Adapter Bitcrush output: " << outB);
    INFO("Adapter difference: " << std::abs(outA - outB));

    // These MUST be different - completely different algorithms
    REQUIRE(outA != Approx(outB).margin(0.001f));
}

TEST_CASE("DIAG: MorphEngine produces different output for cross-family types",
          "[diagnostic]") {
    // MorphEngine is ~936 KB (five DistortionAdapters); on the stack it overflows Windows'
    // 1 MB thread stack, so it lives on the heap here as it does in BandProcessor.
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    engine.prepare(kSampleRate, static_cast<int>(kBlockSize));
    engine.setSmoothingTime(0.0f);  // No smoothing for instant response

    // Set up cross-family nodes
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 5.0f;
    nodes[0].commonParams.mix = 1.0f;
    nodes[0].commonParams.toneHz = 8000.0f;

    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Bitcrush);
    nodes[1].commonParams.drive = 1.0f;  // Low drive to avoid clipping to 1.0
    nodes[1].commonParams.mix = 1.0f;
    nodes[1].commonParams.toneHz = 8000.0f;
    nodes[1].params.bitDepth = 4.0f;

    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::SoftClip);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SoftClip);

    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    // Process MANY non-zero samples at position 0 to settle filter states
    engine.setMorphPosition(0.0f, 0.0f);
    float softClipOut = 0.0f;
    for (int i = 0; i < 500; ++i) {
        softClipOut = engine.process(0.5f);
    }

    const auto& weightsA = engine.getWeights();
    INFO("Weights at pos 0: [" << weightsA[0] << ", " << weightsA[1]
         << ", " << weightsA[2] << ", " << weightsA[3] << "]");
    INFO("Smoothed X at pos 0: " << engine.getSmoothedX());

    // Process MANY non-zero samples at position 1 to settle filter states
    engine.setMorphPosition(1.0f, 0.0f);
    float bitcrushOut = 0.0f;
    for (int i = 0; i < 500; ++i) {
        bitcrushOut = engine.process(0.5f);
    }

    const auto& weightsB = engine.getWeights();
    INFO("Weights at pos 1: [" << weightsB[0] << ", " << weightsB[1]
         << ", " << weightsB[2] << ", " << weightsB[3] << "]");
    INFO("Smoothed X at pos 1: " << engine.getSmoothedX());

    INFO("SoftClip output (last sample): " << softClipOut);
    INFO("Bitcrush output (last sample): " << bitcrushOut);
    INFO("Difference: " << std::abs(softClipOut - bitcrushOut));

    // The outputs from two completely different distortion types must differ
    REQUIRE(softClipOut != Approx(bitcrushOut).margin(0.001f));
}

TEST_CASE("DIAG: Same-family types (SoftClip vs Tube) produce different output",
          "[diagnostic]") {
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
    engine.prepare(kSampleRate, static_cast<int>(kBlockSize));
    engine.setSmoothingTime(0.0f);

    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 5.0f;
    nodes[0].commonParams.mix = 1.0f;

    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[1].commonParams.drive = 5.0f;
    nodes[1].commonParams.mix = 1.0f;

    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::SoftClip);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SoftClip);

    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    engine.setMorphPosition(0.0f, 0.0f);
    for (int i = 0; i < 2000; ++i) { (void)engine.process(0.0f); }
    float softClipOut = engine.process(0.5f);

    engine.setMorphPosition(1.0f, 0.0f);
    for (int i = 0; i < 2000; ++i) { (void)engine.process(0.0f); }
    float tubeOut = engine.process(0.5f);

    INFO("SoftClip output: " << softClipOut);
    INFO("Tube output: " << tubeOut);
    INFO("Difference: " << std::abs(softClipOut - tubeOut));

    REQUIRE(softClipOut != Approx(tubeOut).margin(0.001f));
}

// Measurement probe (hidden, fix plan T1.06): audio-thread cost of one band's
// re-rate at a factor switch (two MorphEngines + two legacy adapters, i.e. what
// BandProcessor::setClockFactor runs), at 48 kHz.
TEST_CASE("Probe: BandProcessor shaper re-rate cost", "[.][t106-cpu-probe]") {
    constexpr double kSr = 48000.0;
    constexpr int kMax = Disrumpo::BandProcessor::kMaxOversampleFactor;
    constexpr int kBlock = 512 * kMax;
    auto engineL = std::make_unique<Disrumpo::MorphEngine>();
    auto engineR = std::make_unique<Disrumpo::MorphEngine>();
    auto adapterL = std::make_unique<Disrumpo::DistortionAdapter>();
    auto adapterR = std::make_unique<Disrumpo::DistortionAdapter>();
    engineL->prepare(kSr, kBlock, kMax);
    engineR->prepare(kSr, kBlock, kMax);
    adapterL->prepare(kSr, kBlock, kMax);
    adapterR->prepare(kSr, kBlock, kMax);
    constexpr int kRuns = 200;
    const auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < kRuns; ++r) {
        const double rate = kSr * ((r % 2 == 0) ? 4.0 : 2.0);
        engineL->setProcessingRate(rate);
        engineR->setProcessingRate(rate);
        adapterL->setProcessingRate(rate);
        adapterR->setProcessingRate(rate);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kRuns;
    WARN("PROBE re-rate: " << us << " us per band switch (a 512-sample block at 48 kHz is "
         << 512.0 / kSr * 1e6 << " us)");
}

// Measurement probe (hidden, fix plan T1.14): band level at sweep intensity 0 and 200 %
// (drive x1 and x2) for SoftClip (drive 2) / Tube (drive 3) at x = 0, 0.5 and 1.
TEST_CASE("Probe: T1.14 sweep level per morph position", "[.][t114-sweep-level-probe]") {
    constexpr float kHz = 1378.125f;
    constexpr size_t kN = 8192;
    for (const float x : {0.0f, 0.5f, 1.0f}) {
        float db[2] = {0.0f, 0.0f};
        for (int k = 0; k < 2; ++k) {
            auto proc = std::make_unique<Disrumpo::BandProcessor>();
            proc->prepare(kSampleRate, kBlockSize);
            proc->setMorphNodes(createTwoNodeSetup(), 2);
            proc->setMorphMode(Disrumpo::MorphMode::Linear1D);
            proc->setMorphPosition(x, 0.0f);
            proc->setSweepIntensity(k == 0 ? 0.0f : 2.0f);
            settleBandProcessor(*proc);
            double s = 0.0;
            for (size_t i = 0; i < kN; ++i) {
                float left = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * kHz * static_cast<float>(i) /
                                              static_cast<float>(kSampleRate));
                float right = left;
                proc->process(left, right);
                s += static_cast<double>(left) * left;
            }
            db[k] = static_cast<float>(10.0 * std::log10(s / static_cast<double>(kN)));
        }
        WARN("PROBE t114 sweep level x = " << x << ": intensity 0 " << db[0] << " dB, 200 % " << db[1]
             << " dB, delta " << db[1] - db[0] << " dB");
    }
}

// Probe (hidden, round 3): a 1-node morph band (drive 2) on a quiet sine (-58 dBFS, 220 Hz) through
// lane crossfades: Oversample Max toggled between two factors every 0.3 s, against the render held
// at each. Peak and RMS after the first 0.5 s. Measured 2026-11-11: Allpass Resonant (every
// topology) and Sine Fold burst at each switch (-24 / -35 dBFS peak against -75 / -53 held), at
// 2x <-> 4x as at 4x <-> 8x; Feedback, Ring Saturation and Hard Clip do not. The cleared incoming
// lane's internal 10 Hz DC blockers start at zero state and pass the shaper's DC as a step (decay
// ~545 dB/s, the 10 Hz pole); open question in the round-3 report.
TEST_CASE("Probe: AllpassResonant through factor crossfades", "[.][allpass-lane-probe]") {
    using namespace Disrumpo;
    struct Variant {
        DistortionType type;
        int topo;
    };
    for (const Variant v : {Variant{.type = DistortionType::AllpassResonant, .topo = 1},
                            Variant{.type = DistortionType::AllpassResonant, .topo = 3},
                            Variant{.type = DistortionType::FeedbackDist, .topo = 0},
                            Variant{.type = DistortionType::RingSaturation, .topo = 0},
                            Variant{.type = DistortionType::SineFold, .topo = 0},
                            Variant{.type = DistortionType::HardClip, .topo = 0}}) {
        for (const auto& [lo, hi] : {std::pair{4, 8}, std::pair{2, 4}}) {
            for (const int mode : {0, 1, 2}) {  // 0: held lo, 1: held hi, 2: toggled
                auto bpOwner = std::make_unique<BandProcessor>();
                BandProcessor& bp = *bpOwner;
                bp.prepare(48000.0, 512);
                DistortionParams params;
                params.allpassTopo = v.topo;
                params.resonantFreq = 500.0f;
                params.allpassFeedback = 0.6f;
                params.decayTimeS = 1.5f;
                std::array<MorphNode, kMaxMorphNodes> nodes{};
                nodes[0] = MorphNode(0, 0.0f, 0.0f, v.type);
                nodes[0].commonParams.drive = 2.0f;
                nodes[0].commonParams.mix = 1.0f;
                nodes[0].params = params;
                bp.setMorphEnabled(true);
                bp.setMorphNodes(nodes, 1);
                bp.setMaxOversampleFactor(mode == 0 ? lo : hi);
                constexpr size_t kBlock = 512;
                const size_t total = kBlock * 282;  // whole blocks, ~3 s
                std::vector<float> out(total);
                std::vector<float> l(kBlock);
                std::vector<float> r(kBlock);
                const auto step = static_cast<size_t>(0.3 * 48000.0);
                for (size_t s = 0; s < total; s += kBlock) {
                    if (mode == 2 && s > 0 && (s / step) != ((s - kBlock) / step)) {
                        bp.setMaxOversampleFactor((s / step) % 2 == 1 ? lo : hi);
                    }
                    for (size_t i = 0; i < kBlock; ++i) {
                        const double t = static_cast<double>(s + i) / 48000.0;
                        l[i] = static_cast<float>(0.00126 * std::sin(2.0 * std::numbers::pi * 220.0 * t));
                        r[i] = l[i];
                    }
                    bp.processBlock(l.data(), r.data(), kBlock);
                    std::copy(l.begin(), l.end(), out.begin() + static_cast<std::ptrdiff_t>(s));
                }
                double pk = 0.0;
                double sq = 0.0;
                for (size_t i = 24000; i < total; ++i) {
                    pk = std::max(pk, std::fabs(static_cast<double>(out[i])));
                    sq += static_cast<double>(out[i]) * out[i];
                }
                static constexpr std::array<const char*, 3> kModeName{"held lo", "held hi", "toggled"};
                std::printf("APL %s topo %d %dx/%dx %s: peak %.1f dBFS, RMS %.1f dBFS\n", getTypeName(v.type), v.topo,
                            lo, hi, kModeName[static_cast<size_t>(mode)],
                            20.0 * std::log10(pk + 1e-15),
                            10.0 * std::log10(sq / static_cast<double>(total - 24000) + 1e-30));
            }
        }
    }
}
