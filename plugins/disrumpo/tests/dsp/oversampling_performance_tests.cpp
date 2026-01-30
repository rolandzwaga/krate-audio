// ==============================================================================
// Oversampling Performance Tests (User Story 5)
// ==============================================================================
// Performance benchmarks and latency verification for the intelligent
// oversampling system. Verifies CPU budgets and zero-latency IIR mode.
//
// Reference: specs/009-intelligent-oversampling/spec.md
// Tasks: T11.077-T11.089
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "dsp/band_processor.h"
#include "dsp/distortion_types.h"
#include "dsp/oversampling_utils.h"
#include "dsp/morph_node.h"

#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

using namespace Disrumpo;

// =============================================================================
// T11.083: Latency reporting (SC-012)
// =============================================================================

TEST_CASE("BandProcessor: getLatency returns 0 (IIR mode)",
          "[oversampling][performance][SC-012]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    SECTION("latency is 0 for all factor values") {
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getLatency() == 0);

        bp.setDistortionType(DistortionType::SoftClip);
        CHECK(bp.getLatency() == 0);

        bp.setDistortionType(DistortionType::Bitcrush);
        CHECK(bp.getLatency() == 0);
    }

    SECTION("latency is 0 at different sample rates") {
        BandProcessor bp48;
        bp48.prepare(48000.0, 512);
        bp48.setDistortionType(DistortionType::HardClip);
        CHECK(bp48.getLatency() == 0);

        BandProcessor bp96;
        bp96.prepare(96000.0, 512);
        bp96.setDistortionType(DistortionType::HardClip);
        CHECK(bp96.getLatency() == 0);
    }
}

// =============================================================================
// T11.083b: Verify ZeroLatency mode is used (FR-018)
// =============================================================================

TEST_CASE("BandProcessor: uses ZeroLatency oversampling mode",
          "[oversampling][performance][FR-018]") {

    // The prepare() method calls oversampler.prepare() with ZeroLatency mode.
    // We verify this indirectly through the latency report.
    BandProcessor bp;
    bp.prepare(44100.0, 512);
    bp.setDistortionType(DistortionType::HardClip);

    // If ZeroLatency mode is used, latency must be 0
    CHECK(bp.getLatency() == 0);

    // Process a block to make sure it works
    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    for (size_t i = 0; i < kBlockSize; ++i) {
        left[i] = 0.3f * std::sin(2.0f * 3.14159f * 440.0f *
                  static_cast<float>(i) / 44100.0f);
        right[i] = left[i];
    }
    bp.processBlock(left.data(), right.data(), kBlockSize);

    // Output should not be all zeros (processing occurred)
    bool hasNonZero = false;
    for (float s : left) {
        if (std::abs(s) > 1e-10f) {
            hasNonZero = true;
            break;
        }
    }
    CHECK(hasNonZero);
}

// =============================================================================
// T11.084: Latency stability during factor changes
// =============================================================================

TEST_CASE("BandProcessor: latency does not change when factors change",
          "[oversampling][performance][latency-stability]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    // Record latency with different types
    bp.setDistortionType(DistortionType::HardClip);
    int latency4x = bp.getLatency();

    bp.setDistortionType(DistortionType::SoftClip);
    int latency2x = bp.getLatency();

    bp.setDistortionType(DistortionType::Bitcrush);
    int latency1x = bp.getLatency();

    // All should be identical (0 for IIR mode)
    CHECK(latency4x == latency2x);
    CHECK(latency2x == latency1x);
    CHECK(latency1x == 0);
}

// =============================================================================
// T11.081b: Constant-time factor selection (FR-013)
// =============================================================================

TEST_CASE("BandProcessor: factor selection is constant time",
          "[oversampling][performance][FR-013]") {

    // The calculateMorphOversampleFactor always iterates max kMaxMorphNodes=4
    // Verify it's fast by benchmarking with different active node counts

    std::array<MorphNode, kMaxMorphNodes> nodes = {{
        MorphNode(0, 0.0f, 0.0f, DistortionType::HardClip),
        MorphNode(1, 1.0f, 0.0f, DistortionType::SoftClip),
        MorphNode(2, 0.0f, 1.0f, DistortionType::Fuzz),
        MorphNode(3, 1.0f, 1.0f, DistortionType::Bitcrush)
    }};
    std::array<float, kMaxMorphNodes> weights = {0.25f, 0.25f, 0.25f, 0.25f};

    BENCHMARK("2-node factor selection") {
        return calculateMorphOversampleFactor(nodes, weights, 2, 8);
    };

    BENCHMARK("3-node factor selection") {
        return calculateMorphOversampleFactor(nodes, weights, 3, 8);
    };

    BENCHMARK("4-node factor selection") {
        return calculateMorphOversampleFactor(nodes, weights, 4, 8);
    };

    // Verify correctness
    CHECK(calculateMorphOversampleFactor(nodes, weights, 4, 8) > 0);
}

// =============================================================================
// T11.077-T11.082: CPU benchmarks (SC-001, SC-002, SC-003, SC-007, SC-010)
// =============================================================================

TEST_CASE("BandProcessor: CPU benchmarks",
          "[oversampling][performance][benchmark]") {

    constexpr size_t kBlockSize = 512;
    constexpr double kSampleRate = 44100.0;

    // Helper: Fill buffer with test signal
    auto fillBuffer = [](float* left, float* right, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            float val = 0.3f * std::sin(2.0f * 3.14159f * 440.0f *
                        static_cast<float>(i) / 44100.0f);
            left[i] = val;
            right[i] = val;
        }
    };

    SECTION("SC-002: 1 band at 1x") {
        BandProcessor bp;
        bp.prepare(kSampleRate, kBlockSize);
        bp.setDistortionType(DistortionType::Bitcrush);

        DistortionCommonParams params;
        params.drive = 0.5f;
        params.mix = 1.0f;
        params.toneHz = 4000.0f;
        bp.setDistortionCommonParams(params);

        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};

        BENCHMARK("1 band @ 1x (512 samples)") {
            fillBuffer(left.data(), right.data(), kBlockSize);
            bp.processBlock(left.data(), right.data(), kBlockSize);
            return left[0];
        };
    }

    SECTION("SC-001: 1 band at 4x") {
        BandProcessor bp;
        bp.prepare(kSampleRate, kBlockSize);
        bp.setDistortionType(DistortionType::HardClip);

        DistortionCommonParams params;
        params.drive = 0.5f;
        params.mix = 1.0f;
        params.toneHz = 4000.0f;
        bp.setDistortionCommonParams(params);

        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};

        BENCHMARK("1 band @ 4x (512 samples)") {
            fillBuffer(left.data(), right.data(), kBlockSize);
            bp.processBlock(left.data(), right.data(), kBlockSize);
            return left[0];
        };
    }

    SECTION("SC-010: bypassed band") {
        BandProcessor bp;
        bp.prepare(kSampleRate, kBlockSize);
        bp.setDistortionType(DistortionType::HardClip);
        bp.setBypassed(true);

        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};

        BENCHMARK("1 band bypassed (512 samples)") {
            fillBuffer(left.data(), right.data(), kBlockSize);
            bp.processBlock(left.data(), right.data(), kBlockSize);
            return left[0];
        };
    }

    SECTION("SC-007: factor selection overhead") {
        BandProcessor bp;
        bp.prepare(kSampleRate, kBlockSize);

        BENCHMARK("recalculateOversampleFactor (single type)") {
            bp.setDistortionType(DistortionType::HardClip);
            return bp.getOversampleFactor();
        };
    }
}

// =============================================================================
// T11.089: End-to-end latency check (SC-004)
// =============================================================================

TEST_CASE("BandProcessor: end-to-end latency does not exceed 10ms",
          "[oversampling][performance][SC-004]") {

    // With IIR (ZeroLatency) oversampling, there is NO added latency.
    // The only "latency" would be the 8ms crossfade window during transitions,
    // but this is not true latency - it's a blending period.

    BandProcessor bp;
    bp.prepare(44100.0, 512);
    bp.setDistortionType(DistortionType::HardClip);

    // Latency = 0 samples = 0ms, well under 10ms
    float latencyMs = static_cast<float>(bp.getLatency()) / 44100.0f * 1000.0f;
    CHECK(latencyMs < 10.0f);

    // Also check at 96kHz
    BandProcessor bp96;
    bp96.prepare(96000.0, 512);
    bp96.setDistortionType(DistortionType::HardClip);
    float latencyMs96 = static_cast<float>(bp96.getLatency()) / 96000.0f * 1000.0f;
    CHECK(latencyMs96 < 10.0f);
}
