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

#include "../test_support/audit_metrics.h"
#include "dsp/band_processor.h"
#include "dsp/distortion_types.h"
#include "dsp/oversampling_utils.h"
#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

/// Group delay at 1 kHz (48 kHz) of two 512-sample blocks through @p bp, impulse at 64.
double bandGroupDelay(BandProcessor& bp) {
    constexpr size_t kBlock = 512;
    constexpr size_t kAt = 64;
    std::vector<float> left(2 * kBlock, 0.0f);
    std::vector<float> right(2 * kBlock, 0.0f);
    left[kAt] = 0.25f;
    right[kAt] = 0.25f;
    bp.processBlock(left.data(), right.data(), kBlock);
    bp.processBlock(left.data() + kBlock, right.data() + kBlock, kBlock);
    const std::vector<float> h(left.begin() + static_cast<std::ptrdiff_t>(kAt), left.end());
    return Disrumpo::TestSupport::groupDelaySamples(h, 1000.0, 48000.0);
}

}  // namespace

TEST_CASE("BandProcessor: getLatency equals the measured impulse delay at each factor",
          "[oversampling][latency][SC-012][D-125]") {
    for (const double sr : {44100.0, 48000.0, 96000.0}) {
        for (const auto type : {DistortionType::Bitcrush, DistortionType::Tube, DistortionType::HardClip,
                                DistortionType::Tape}) {
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
    // Tube node (2x) and HardClip node (4x), both at drive 2 and node mix 0. The worst
    // case is the Oversample Max (8x) delay; the band waits the difference at 2x and at 4x.
    auto bp = std::make_unique<BandProcessor>();
    bp->prepare(48000.0, 512);
    std::array<MorphNode, kMaxMorphNodes> nodes = {{MorphNode(0, 0.0f, 0.0f, DistortionType::Tube),
                                                     MorphNode(1, 1.0f, 0.0f, DistortionType::HardClip),
                                                     MorphNode(2, 0.0f, 1.0f, DistortionType::Tube),
                                                     MorphNode(3, 1.0f, 1.0f, DistortionType::Tube)}};
    for (auto& n : nodes) n.commonParams = {.drive = 2.0f, .mix = 0.0f, .toneHz = 4000.0f};
    bp->setMorphEnabled(true);
    bp->setMorphSmoothingTime(0.0f);
    bp->setMorphNodes(nodes, 2);
    bp->setMorphPosition(0.0f, 0.0f);
    const int worst = bp->getWorstCaseLatency();
    bp->setLatencyTarget(worst);
    REQUIRE(bp->getOversampleFactor() == 2);
    const double at2x = bandGroupDelay(*bp);

    bp->setMorphPosition(1.0f, 0.0f);  // HardClip only: 4x after the switch dip
    std::array<float, 512> left{};
    std::array<float, 512> right{};
    for (int i = 0; i < 4; ++i) bp->processBlock(left.data(), right.data(), left.size());
    REQUIRE(bp->getActiveOversampleFactor() == 4);
    REQUIRE_FALSE(bp->isOversampleTransitioning());
    const double at4x = bandGroupDelay(*bp);
    // Follow-up 81, minimax ruling (2026-11-09): the band arrives at the target plus its 48 kHz
    // minimax offset, +/- 0.1 sample (at 1 kHz): 2x in a 4x-capable band -0.409, 4x -0.506
    // (ffd51f47: 7.00 / 7.01; the integer alignment before it 6.01 / 6.48)
    INFO("worst case " << worst << ", group delay at 1 kHz at 2x " << at2x << ", at 4x " << at4x);
    // The worst case is the Oversample Max factor's delay (8x by default), not the nodes' highest
    // factor (user ruling 2026-11-11, round 3: a type switch must not change the latency)
    CHECK(worst == bp->getOversamplerLatency(bp->getMaxOversampleFactor()));
    CHECK(std::fabs(at2x - (worst - 0.409)) <= 0.1);
    CHECK(std::fabs(at4x - (worst - 0.506)) <= 0.1);
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
        // The band reports the latency of its Oversample Max (8x by default) whatever its type runs
        // at (user ruling 2026-11-11, round 3): the uncompensated band delays by its own factor's
        // latency, and both stay under the bound
        const double worstMs = static_cast<double>(bp->getWorstCaseLatency()) / sr * 1000.0;
        INFO(sr << " Hz, 4x: measured " << measured << " samples = " << ms << " ms, reported worst case "
                << bp->getWorstCaseLatency() << " = " << worstMs << " ms");
        CHECK(measured == bp->getLatency());
        CHECK(ms < 10.0);
        CHECK(worstMs < 10.0);
    }
}

// =============================================================================
// D-005 (fix plan T2.20): identical-type morph routes its params at control rate
// =============================================================================
// processSameFamily used to run setType + interpolateParams + setParams +
// setCommonParams on every sample (Formant: two std::pow and the vowel filter
// updates each time). It now routes only when the weights move, a node / offset
// setter runs, or the sweep drive scale changes. The reference render is the
// pre-change behaviour, reproduced through the public API: a mod-offset setter
// before every sample forces the full routing on every sample.
// MorphEngine holds five DistortionAdapters: every engine lives on the heap.

namespace {

constexpr double kMorphRate = 48000.0;

/// Four nodes of one type at the corners of the XY pad, with distinct params.
std::array<MorphNode, kMaxMorphNodes> cornerNodes(const std::array<DistortionType, 4>& types) {
    constexpr std::array<std::array<float, 2>, 4> kCorners{{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}};
    std::array<MorphNode, kMaxMorphNodes> nodes{};
    for (size_t i = 0; i < 4; ++i) {
        const auto f = static_cast<float>(i);
        nodes[i] = MorphNode(static_cast<int>(i), kCorners[i][0], kCorners[i][1], types[i]);
        nodes[i].commonParams = {.drive = 2.0f + f, .mix = 1.0f - 0.1f * f, .toneHz = 2500.0f + 1500.0f * f};
        nodes[i].params.vowelSelect = static_cast<int>(i);
        nodes[i].params.formantShift = -6.0f + 4.0f * f;
        nodes[i].params.formantReso = 0.2f + 0.2f * f;
        nodes[i].params.formantBW = 0.8f - 0.2f * f;
        nodes[i].params.formantBlend = 0.25f * f;
        nodes[i].params.bias = -0.2f + 0.1f * f;
    }
    return nodes;
}

constexpr std::array<DistortionType, 4> kFourFormants{DistortionType::Formant, DistortionType::Formant,
                                                       DistortionType::Formant, DistortionType::Formant};
// Four different Experimental-family types (Spectral left out: it adds FFT latency)
constexpr std::array<DistortionType, 4> kFourExperimental{DistortionType::Formant, DistortionType::Chaos,
                                                           DistortionType::Granular, DistortionType::Fractal};

std::unique_ptr<MorphEngine> makeMorphEngine(const std::array<DistortionType, 4>& types, float smoothingMs) {
    auto e = std::make_unique<MorphEngine>();
    e->prepare(kMorphRate, 512);
    e->setMode(MorphMode::Planar2D);
    e->setSmoothingTime(smoothingMs);
    e->setNodes(cornerNodes(types), 4);
    e->setMorphPosition(0.5f, 0.5f);
    return e;
}

float morphTestInput(size_t n, double scale = 1.0) {
    const double t = static_cast<double>(n) / kMorphRate;
    return static_cast<float>(scale * (0.4 * std::sin(2.0 * std::numbers::pi * 220.0 * t) +
                                       0.2 * std::sin(2.0 * std::numbers::pi * 1370.0 * t)));
}

/// Block-rate modulation of a scenario: drive/mix and tone/bias offsets for one block.
struct BlockMod {
    float drive = 0.0f;
    float mix = 0.0f;
    float tone = 0.0f;
    float bias = 0.0f;
};

BlockMod modForBlock(size_t block) {
    // Offsets step every 8 blocks, and are 0 for a stretch (the base-param return)
    const size_t step = block / 8;
    if (step % 5 == 4) return {};
    const auto s = static_cast<float>(step % 5);
    return {.drive = 0.05f * s, .mix = -0.04f * s, .tone = 0.03f * s - 0.05f, .bias = 0.02f * s};
}

/// Renders the scenario through @p e: 1 s at 48 kHz in 64-sample blocks, offsets per
/// block, a sweep drive ramp per sample in the third quarter, two cursor moves. With
/// @p forcePerSample the offset setter also runs before every sample (the pre-change
/// per-sample routing).
std::vector<float> renderMorphScenario(MorphEngine& e, bool forcePerSample) {
    constexpr size_t kTotal = 48000;
    constexpr size_t kBlock = 64;
    std::vector<float> out(kTotal);
    for (size_t start = 0; start < kTotal; start += kBlock) {
        const size_t block = start / kBlock;
        if (start == 12000) e.setMorphPosition(0.1f, 0.9f);
        if (start == 28800) e.setMorphPosition(0.5f, 0.5f);
        if (start == 40000) {  // a node edit while the cursor is still
            auto nodes = cornerNodes(kFourFormants);
            nodes[0].params.formantShift = 5.0f;
            nodes[3].commonParams.drive = 8.0f;
            e.setNodes(nodes, 4);
        }
        const BlockMod m = modForBlock(block);
        e.setDriveMixModOffset(m.drive, m.mix);
        e.setToneBiasModOffset(m.tone, m.bias);
        for (size_t n = start; n < start + kBlock; ++n) {
            const bool sweeping = n >= 24000 && n < 36000;
            e.setSweepDriveScale(sweeping ? 1.0f + static_cast<float>(n - 24000) / 12000.0f : 1.0f);
            if (forcePerSample) e.setDriveMixModOffset(m.drive, m.mix);
            out[n] = e.process(morphTestInput(n));
        }
    }
    return out;
}

/// Wall-clock seconds for @p seconds of audio at 48 kHz through four bands of @p types
/// at the centre of the pad (512-sample blocks, after a 0.5 s warm-up).
double fourBandSeconds(const std::array<DistortionType, 4>& types, double seconds) {
    constexpr size_t kBlock = 512;
    std::array<std::unique_ptr<BandProcessor>, 4> bands;
    for (auto& b : bands) {
        b = std::make_unique<BandProcessor>();
        b->prepare(kMorphRate, kBlock);
        b->setMorphEnabled(true);
        b->setMorphMode(MorphMode::Planar2D);
        b->setMorphNodes(cornerNodes(types), 4);
        b->setMorphPosition(0.5f, 0.5f);
    }
    std::array<float, kBlock> left{};
    std::array<float, kBlock> right{};
    const auto blocksFor = [](double s) { return static_cast<size_t>(s * kMorphRate) / kBlock; };
    size_t n = 0;
    const auto run = [&](size_t blocks) {
        for (size_t blk = 0; blk < blocks; ++blk) {
            for (auto& b : bands) {
                for (size_t i = 0; i < kBlock; ++i) {
                    left[i] = morphTestInput(n + i);
                    right[i] = left[i];
                }
                b->processBlock(left.data(), right.data(), kBlock);
            }
            n += kBlock;
        }
    };
    run(blocksFor(0.5));
    const auto t0 = std::chrono::steady_clock::now();
    run(blocksFor(seconds));
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

/// Wall-clock seconds for @p seconds of audio through one engine, optionally forcing
/// the per-sample routing (the pre-change cost).
double engineSeconds(const std::array<DistortionType, 4>& types, double seconds, bool forcePerSample) {
    auto e = makeMorphEngine(types, 0.0f);
    const auto total = static_cast<size_t>(seconds * kMorphRate);
    float sink = 0.0f;
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t n = 0; n < total; ++n) {
        if (forcePerSample) e->setDriveMixModOffset(0.0f, 0.0f);
        sink += e->process(morphTestInput(n));
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(Krate::DSP::detail::isFinite(sink));
    return s;
}

}  // namespace

TEST_CASE("Same-type morph routes its params only when they change", "[disrumpo][morph][D-005]") {
    auto e = makeMorphEngine(kFourFormants, 0.0f);
    const auto routesAfter = [&e](size_t samples, size_t& n) {
        const auto before = e->getBlendedRouteCount();
        for (size_t i = 0; i < samples; ++i, ++n) (void)e->process(morphTestInput(n));
        return e->getBlendedRouteCount() - before;
    };
    size_t n = 0;
    const auto staticRoutes = routesAfter(4800, n);  // 0.1 s, cursor still, no setter
    e->setDriveMixModOffset(0.2f, 0.0f);
    const auto offsetRoutes = routesAfter(4800, n);
    e->setSweepDriveScale(1.5f);
    const auto sweepRoutes = routesAfter(4800, n);
    e->setMorphPosition(0.2f, 0.7f);  // the shortest smoothing still settles over a few samples
    const auto moveRoutes = routesAfter(4800, n);
    const auto settledRoutes = routesAfter(4800, n);
    std::printf("T2.20 D-005 routes per 4800 samples: still %llu, after an offset change %llu, after a sweep "
                "scale change %llu, after a cursor jump %llu, settled again %llu (want <= 1, 1, 1, 1..48, 0)\n",
                static_cast<unsigned long long>(staticRoutes), static_cast<unsigned long long>(offsetRoutes),
                static_cast<unsigned long long>(sweepRoutes), static_cast<unsigned long long>(moveRoutes),
                static_cast<unsigned long long>(settledRoutes));
    CHECK(staticRoutes <= 1u);
    CHECK(offsetRoutes == 1u);
    CHECK(sweepRoutes == 1u);
    CHECK(moveRoutes >= 1u);
    CHECK(moveRoutes <= 48u);  // routes only while the weights move (1 ms here)
    CHECK(settledRoutes == 0u);
}

TEST_CASE("Same-type morph render is unchanged by control-rate routing", "[disrumpo][morph][D-005]") {
    auto fast = makeMorphEngine(kFourFormants, 20.0f);
    auto reference = makeMorphEngine(kFourFormants, 20.0f);
    const auto out = renderMorphScenario(*fast, false);
    const auto ref = renderMorphScenario(*reference, true);
    double maxDiff = 0.0;
    double peak = 0.0;
    size_t worst = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        const double d = std::fabs(static_cast<double>(out[i]) - static_cast<double>(ref[i]));
        if (d > maxDiff) {
            maxDiff = d;
            worst = i;
        }
        peak = std::max(peak, std::fabs(static_cast<double>(ref[i])));
    }
    std::printf("T2.20 D-005 render vs per-sample routing: max |diff| %.3g at sample %zu (want <= 1e-6), "
                "reference peak %.3f, routes %llu vs %llu\n",
                maxDiff, worst, peak, static_cast<unsigned long long>(fast->getBlendedRouteCount()),
                static_cast<unsigned long long>(reference->getBlendedRouteCount()));
    REQUIRE(peak > 0.01);
    CHECK(maxDiff <= 1e-6);
}

TEST_CASE("Same-type morph applies a new mod offset within the block it arrives", "[disrumpo][morph][D-005]") {
    // Two engines run the same input; at sample 512 one gets a drive offset (as
    // BandProcessor hands it over at a block start). Its output must change at once.
    // The input is 18.06 dB lower than the other cases': the drive re-stage (user ruling
    // 2026-11-10) runs Formant 18.06 dB hotter, and at full level its saturated output stayed
    // identical for 7 samples after the change (first differing sample 519); at this level the
    // shaper sees the drive this case was written for.
    auto moved = makeMorphEngine(kFourFormants, 0.0f);
    auto still = makeMorphEngine(kFourFormants, 0.0f);
    constexpr size_t kChange = 512;
    constexpr double kOldStaging = 0.125;
    size_t firstDiff = SIZE_MAX;
    for (size_t n = 0; n < 1024; ++n) {
        if (n == kChange) moved->setDriveMixModOffset(0.3f, 0.0f);
        const float a = moved->process(morphTestInput(n, kOldStaging));
        const float b = still->process(morphTestInput(n, kOldStaging));
        if (firstDiff == SIZE_MAX && a != b) firstDiff = n;
    }
    std::printf("T2.20 D-005 drive offset set at sample %zu: output first differs at sample %zu (want %zu)\n",
                kChange, firstDiff, kChange);
    CHECK(firstDiff == kChange);
}

// Hidden cost probe (prints, asserts no budget): run alone, nothing else executing,
// disrumpo_tests.exe "[t220-cpu-probe]". Measured 2026-11-09 at 48 kHz: 4 bands x
// 4 Formant 14.27 %, 4 bands x 4 mixed types 21.66 %; one engine 0.669 % with
// control-rate routing vs 2.437 % with per-sample routing.
TEST_CASE("Probe: T2.20 same-type morph routing cost", "[.][t220-cpu-probe]") {
    constexpr double kSeconds = 10.0;
    const double formantBands = fourBandSeconds(kFourFormants, kSeconds);
    const double mixedBands = fourBandSeconds(kFourExperimental, kSeconds);
    const double engineFast = engineSeconds(kFourFormants, kSeconds, false);
    const double enginePerSample = engineSeconds(kFourFormants, kSeconds, true);
    const double engineMixed = engineSeconds(kFourExperimental, kSeconds, false);
    const auto pct = [kSeconds](double s) { return 100.0 * s / kSeconds; };
    std::printf("T2.20 D-005 CPU, 10 s at 48 kHz: 4 bands x 4 Formant %.2f %%; 4 bands x "
                "Formant/Chaos/Granular/Fractal (parallel path) %.2f %%\n",
                pct(formantBands), pct(mixedBands));
    std::printf("T2.20 D-005 CPU, one engine, 4 Formant: control-rate routing %.3f %%, per-sample routing "
                "(pre-change) %.3f %% (x%.2f); 4 mixed types (parallel path) %.3f %%\n",
                pct(engineFast), pct(enginePerSample), enginePerSample / engineFast, pct(engineMixed));
}
