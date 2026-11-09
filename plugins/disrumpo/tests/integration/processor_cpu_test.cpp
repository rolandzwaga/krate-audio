// ==============================================================================
// Disrumpo - Processor-level CPU budgets (D-130, follow-up 1)
// ==============================================================================
// Every case is tagged [performance] and wall-clock based: run it ALONE, nothing
// else executing (node tools/run-cpu-tests.js disrumpo_tests). Per-push CI and
// the gates exclude it.
//
// CPU % = summed wall time of Processor::process() over the timed blocks / the
// audio time those blocks hold x 100, one thread (the convention of the
// BandProcessor budgets in distortion_adapter_test.cpp). Rendered at 44.1 kHz,
// 512-sample blocks, after a 2 s warm-up that lets every factor switch, type
// fade and smoother settle.
//
// Budgets (spec numbers, never relaxed):
//   - 4 bands, 4x, one node per band (single type per band):
//       5 %  specs/_archive2_/003-distortion-integration/spec.md PT-DI-002
//       (stricter than the 15 % of specs-overview.md for "4 bands, 4x OS", which
//       003's clarification scopes to the full morph system)
//   - 4 bands x 4 nodes at the morph centre (morph system):
//      15 %  specs/Disrumpo/specs-overview.md success criteria / NFR "4 bands,
//            4x OS", 009 SC-001, 003 PT-DI-004 (2-type cross-family morph)
// ==============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/distortion_types.h"

#include "../test_support/disrumpo_rig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace Disrumpo;

namespace {

namespace ts = Disrumpo::TestSupport;

constexpr double kCpuSampleRate = 44100.0;
constexpr Steinberg::int32 kCpuBlock = 512;
constexpr double kWarmupSeconds = 2.0;
constexpr double kTimedSeconds = 10.0;
constexpr int kBands = 4;

double typeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

/// Four bands, every band's nodes set to @p types (node i gets types[i]),
/// @p nodeCount active nodes, Planar 2D at the centre, drive 5, mix 100 %.
ts::PList fourBands(const std::array<DistortionType, 4>& types, int nodeCount) {
    ts::PList l{{kBandCountId, 1.0}, {kGlobalMixId, 1.0}};
    for (int b = 0; b < kBands; ++b) {
        const auto band = static_cast<uint8_t>(b);
        l.emplace_back(makeBandParamId(band, BandParamType::kBandActiveNodes),
                       static_cast<double>(nodeCount - 1) / 3.0);
        l.emplace_back(makeBandParamId(band, BandParamType::kBandMorphMode), 0.5);  // Planar 2D
        l.emplace_back(makeBandParamId(band, BandParamType::kBandMorphX), 0.5);
        l.emplace_back(makeBandParamId(band, BandParamType::kBandMorphY), 0.5);
        for (uint8_t n = 0; n < 4; ++n) {
            l.emplace_back(makeNodeParamId(band, n, NodeParamType::kNodeType), typeNorm(types[n]));
            l.emplace_back(makeNodeParamId(band, n, NodeParamType::kNodeDrive), 0.5);
            l.emplace_back(makeNodeParamId(band, n, NodeParamType::kNodeMix), 1.0);
        }
    }
    return l;
}

/// Broadband input that reaches every band: 80 Hz, 440 Hz, 2.5 kHz and 9 kHz,
/// each at -18 dBFS (about -12 dBFS together).
std::vector<float> fourTones(size_t n, double phaseOffset) {
    std::vector<float> x(n);
    constexpr std::array<double, 4> kHz{80.0, 440.0, 2500.0, 9000.0};
    const double amp = std::pow(10.0, -18.0 / 20.0);
    for (size_t i = 0; i < n; ++i) {
        double s = 0.0;
        for (double f : kHz) {
            s += std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / kCpuSampleRate + phaseOffset);
        }
        x[i] = static_cast<float>(amp * s);
    }
    return x;
}

struct CpuReading {
    double percent = 0.0;          ///< summed process() time / audio time
    double bestTrialPercent = 0.0; ///< fastest 10-block run, for the record
    std::array<int, kBands> factor{};
    std::array<int, kBands> nodes{};
};

CpuReading measure(const ts::PList& params) {
    ts::Rig rig(kCpuSampleRate, kCpuBlock);
    rig.apply(params);

    const auto block = static_cast<size_t>(kCpuBlock);
    const auto warmupBlocks = static_cast<size_t>(kWarmupSeconds * kCpuSampleRate) / block;
    const auto timedBlocks = static_cast<size_t>(kTimedSeconds * kCpuSampleRate) / block;
    const auto inL = fourTones((warmupBlocks + timedBlocks) * block, 0.0);
    const auto inR = fourTones((warmupBlocks + timedBlocks) * block, 0.3);
    std::vector<float> bL(block);
    std::vector<float> bR(block);
    std::vector<float> oL(block);
    std::vector<float> oR(block);

    using Clock = std::chrono::steady_clock;
    constexpr size_t kTrialBlocks = 10;
    double totalNs = 0.0;
    double trialNs = 0.0;
    double bestTrialNs = 1.0e300;
    for (size_t k = 0; k < warmupBlocks + timedBlocks; ++k) {
        std::copy_n(inL.begin() + static_cast<std::ptrdiff_t>(k * block), block, bL.begin());
        std::copy_n(inR.begin() + static_cast<std::ptrdiff_t>(k * block), block, bR.begin());
        const auto t0 = Clock::now();
        rig.processBlock(bL.data(), bR.data(), oL.data(), oR.data(), kCpuBlock, nullptr);
        const auto t1 = Clock::now();
        if (k < warmupBlocks) continue;
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        totalNs += ns;
        trialNs += ns;
        if ((k - warmupBlocks + 1) % kTrialBlocks == 0) {
            bestTrialNs = std::min(bestTrialNs, trialNs);
            trialNs = 0.0;
        }
    }

    CpuReading r;
    const double audioNs = static_cast<double>(timedBlocks * block) / kCpuSampleRate * 1.0e9;
    const double trialAudioNs = static_cast<double>(kTrialBlocks * block) / kCpuSampleRate * 1.0e9;
    r.percent = 100.0 * totalNs / audioNs;
    r.bestTrialPercent = 100.0 * bestTrialNs / trialAudioNs;
    for (int b = 0; b < kBands; ++b) {
        r.factor[static_cast<size_t>(b)] = rig.p->getBandProcessor(b).getActiveOversampleFactor();
        r.nodes[static_cast<size_t>(b)] = rig.p->getMorphCacheActiveNodes(b);
    }
    return r;
}

void report(const char* label, const CpuReading& r, double budget) {
    std::printf("T5.09 CPU %s: %.2f %% (best 10-block run %.2f %%), budget %.1f %%, factors %d/%d/%d/%d, "
                "nodes %d/%d/%d/%d\n",
                label, r.percent, r.bestTrialPercent, budget, r.factor[0], r.factor[1], r.factor[2], r.factor[3],
                r.nodes[0], r.nodes[1], r.nodes[2], r.nodes[3]);
}

}  // namespace

TEST_CASE("Processor CPU: 4 bands at 4x, one HardClip node per band, under 5 %",
          "[disrumpo][processor][performance]") {
    constexpr double kBudget = 5.0;  // PT-DI-002
    const auto r = measure(fourBands({DistortionType::HardClip, DistortionType::HardClip,
                                      DistortionType::HardClip, DistortionType::HardClip}, 1));
    report("4 bands x 1 HardClip node, 4x", r, kBudget);
    for (int b = 0; b < kBands; ++b) {
        REQUIRE(r.factor[static_cast<size_t>(b)] == 4);
        REQUIRE(r.nodes[static_cast<size_t>(b)] == 1);
    }
    INFO("CPU " << r.percent << " % (best 10-block run " << r.bestTrialPercent << " %)");
    REQUIRE(r.percent < kBudget);
}

TEST_CASE("Processor CPU: 4 bands x 4 Experimental nodes at the morph centre, under 15 %",
          "[disrumpo][processor][morph][performance]") {
    constexpr double kBudget = 15.0;  // specs-overview.md NFR, 009 SC-001
    SECTION("same type: 4 Formant nodes (one interpolated processor per engine)") {
        const auto r = measure(fourBands({DistortionType::Formant, DistortionType::Formant,
                                          DistortionType::Formant, DistortionType::Formant}, 4));
        report("4 bands x 4 Formant nodes", r, kBudget);
        for (int b = 0; b < kBands; ++b) REQUIRE(r.nodes[static_cast<size_t>(b)] == 4);
        INFO("CPU " << r.percent << " % (best 10-block run " << r.bestTrialPercent << " %)");
        REQUIRE(r.percent < kBudget);
    }
    SECTION("same family: Formant, Chaos, Granular, Fractal (parallel adapters)") {
        const auto r = measure(fourBands({DistortionType::Formant, DistortionType::Chaos,
                                          DistortionType::Granular, DistortionType::Fractal}, 4));
        report("4 bands x Formant/Chaos/Granular/Fractal", r, kBudget);
        for (int b = 0; b < kBands; ++b) REQUIRE(r.nodes[static_cast<size_t>(b)] == 4);
        INFO("CPU " << r.percent << " % (best 10-block run " << r.bestTrialPercent << " %)");
        REQUIRE(r.percent < kBudget);
    }
}

TEST_CASE("Processor CPU: 4 bands x 4 cross-family nodes at 4x at the morph centre, under 15 %",
          "[disrumpo][processor][morph][performance]") {
    constexpr double kBudget = 15.0;  // specs-overview.md NFR "4 bands, 4x OS", 003 PT-DI-004
    // One node from each of four families, every one a 4x type, so the band runs at 4x
    const auto r = measure(fourBands({DistortionType::HardClip, DistortionType::SineFold,
                                      DistortionType::FullRectify, DistortionType::RingSaturation}, 4));
    report("4 bands x HardClip/SineFold/FullRectify/RingSaturation, 4x", r, kBudget);
    for (int b = 0; b < kBands; ++b) {
        REQUIRE(r.factor[static_cast<size_t>(b)] == 4);
        REQUIRE(r.nodes[static_cast<size_t>(b)] == 4);
    }
    INFO("CPU " << r.percent << " % (best 10-block run " << r.bestTrialPercent << " %)");
    REQUIRE(r.percent < kBudget);
}
