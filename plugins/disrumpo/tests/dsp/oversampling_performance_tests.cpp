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
#include <numbers>
#include <vector>

using namespace Disrumpo;

// =============================================================================
// T11.083 / T11.084 / T11.089: latency, measured (audit D-125, fix plan T2.10)
// =============================================================================
// These cases asserted a static getLatency() that returned 0 and carried the
// [performance] tag, so per-push lanes skipped them. The IIR oversampler delays a
// band by its impulse-peak delay; the cases now measure that delay through
// processBlock and compare it with what the band reports.
//
// BandProcessor holds two DistortionAdapters (one per channel, ~340 KB in all): every
// case keeps them on the heap, one at a time, so the frame stays under the 1 MB
// Windows thread stack.

namespace {

/// Legacy single-type band at @p sr: @p type at drive 2 and mix 0, so the adapter outputs
/// its input and only the oversampler's filters shape an impulse.
std::unique_ptr<BandProcessor> makeLatencyBand(double sr, DistortionType type) {
    auto bp = std::make_unique<BandProcessor>();
    bp->prepare(sr, 512);
    bp->setDistortionType(type);
    bp->setDistortionCommonParams({.drive = 2.0f, .mix = 0.0f, .toneHz = 4000.0f});
    return bp;
}

/// Impulse-peak delay of one 512-sample block through @p bp, in samples.
long measuredBandDelay(BandProcessor& bp) {
    constexpr size_t kBlock = 512;
    constexpr size_t kAt = 64;
    std::array<float, kBlock> left{};
    std::array<float, kBlock> right{};
    left[kAt] = 0.25f;
    right[kAt] = 0.25f;
    bp.processBlock(left.data(), right.data(), kBlock);
    size_t peak = 0;
    for (size_t i = 1; i < kBlock; ++i) {
        if (std::fabs(left[i]) > std::fabs(left[peak])) peak = i;
    }
    return static_cast<long>(peak) - static_cast<long>(kAt);
}

}  // namespace

TEST_CASE("BandProcessor: getLatency equals the measured impulse delay at each factor",
          "[oversampling][latency][SC-012][D-125]") {
    for (const double sr : {44100.0, 48000.0, 96000.0}) {
        for (const auto type : {DistortionType::Bitcrush, DistortionType::SoftClip, DistortionType::HardClip}) {
            auto bp = makeLatencyBand(sr, type);
            const long measured = measuredBandDelay(*bp);
            INFO(sr << " Hz, factor " << bp->getOversampleFactor() << ": measured " << measured
                 << ", getLatency " << bp->getLatency());
            CHECK(measured == bp->getLatency());
            CHECK(bp->getLatency() == bp->getOversamplerLatency(bp->getOversampleFactor()));
        }
    }
}

TEST_CASE("BandProcessor: IIR (ZeroLatency) oversampling delays a band by a few samples",
          "[oversampling][latency][FR-018][D-125]") {
    // FR-018: Economy / ZeroLatency mode uses minimum-phase IIR filters; a linear-phase FIR
    // pair would delay a 2x band by 15 samples per stage (Oversampler Standard quality).
    auto bp = makeLatencyBand(44100.0, DistortionType::HardClip);
    REQUIRE(bp->getOversampleFactor() == 4);
    const long measured = measuredBandDelay(*bp);
    INFO("4x HardClip band: measured impulse delay " << measured << " samples");
    CHECK(measured > 0);
    CHECK(measured < 15);
    CHECK(bp->getOversamplerLatency(2) > 0);
    CHECK(bp->getOversamplerLatency(2) < bp->getOversamplerLatency(4));
}

TEST_CASE("BandProcessor: compensated latency holds across a factor switch",
          "[oversampling][latency][latency-stability][D-125]") {
    // SoftClip node (2x) and HardClip node (4x), both at drive 2 and node mix 0. The worst
    // case is the 4x delay; at morph (0, 0) the band runs 2x and waits the difference.
    auto bp = std::make_unique<BandProcessor>();
    bp->prepare(48000.0, 512);
    std::array<MorphNode, kMaxMorphNodes> nodes = {{MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip),
                                                     MorphNode(1, 1.0f, 0.0f, DistortionType::HardClip),
                                                     MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip),
                                                     MorphNode(3, 1.0f, 1.0f, DistortionType::SoftClip)}};
    for (auto& n : nodes) n.commonParams = {.drive = 2.0f, .mix = 0.0f, .toneHz = 4000.0f};
    bp->setMorphEnabled(true);
    bp->setMorphSmoothingTime(0.0f);
    bp->setMorphNodes(nodes, 2);
    bp->setMorphPosition(0.0f, 0.0f);
    const int worst = bp->getWorstCaseLatency();
    bp->setLatencyTarget(worst);
    REQUIRE(bp->getOversampleFactor() == 2);
    const long at2x = measuredBandDelay(*bp);

    bp->setMorphPosition(1.0f, 0.0f);  // HardClip only: 4x after the switch dip
    std::array<float, 512> left{};
    std::array<float, 512> right{};
    for (int i = 0; i < 4; ++i) bp->processBlock(left.data(), right.data(), left.size());
    REQUIRE(bp->getActiveOversampleFactor() == 4);
    REQUIRE_FALSE(bp->isOversampleTransitioning());
    const long at4x = measuredBandDelay(*bp);
    INFO("worst case " << worst << ", measured at 2x " << at2x << ", at 4x " << at4x);
    CHECK(worst == bp->getOversamplerLatency(4));
    CHECK(at2x == worst);
    CHECK(at4x == worst);
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
            float val = 0.3f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f *
                        static_cast<float>(i) / 44100.0f);
            left[i] = val;
            right[i] = val;
        }
    };

    SECTION("SC-002: 1 band at 1x") {
        auto bpOwner = std::make_unique<BandProcessor>();
        auto& bp = *bpOwner;
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
        auto bpOwner = std::make_unique<BandProcessor>();
        auto& bp = *bpOwner;
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
        auto bpOwner = std::make_unique<BandProcessor>();
        auto& bp = *bpOwner;
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
        auto bpOwner = std::make_unique<BandProcessor>();
        auto& bp = *bpOwner;
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

TEST_CASE("BandProcessor: oversampling latency stays under 10 ms",
          "[oversampling][latency][SC-004][D-125]") {
    // SC-004 bounds what oversampling adds. A Spectral node adds its FFT size - 1 on top
    // (up to 4095 samples); that latency is reported to the host and compensated (D-098,
    // T2.10), not bounded.
    for (const double sr : {44100.0, 96000.0}) {
        auto bp = makeLatencyBand(sr, DistortionType::HardClip);
        const long measured = measuredBandDelay(*bp);
        const double ms = static_cast<double>(measured) / sr * 1000.0;
        INFO(sr << " Hz, 4x: measured " << measured << " samples = " << ms << " ms");
        CHECK(measured == bp->getWorstCaseLatency());
        CHECK(ms < 10.0);
    }
}
