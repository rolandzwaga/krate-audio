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
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
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
std::vector<float> fourTones(size_t n, double phaseOffset, double sampleRate = kCpuSampleRate) {
    std::vector<float> x(n);
    constexpr std::array<double, 4> kHz{80.0, 440.0, 2500.0, 9000.0};
    const double amp = std::pow(10.0, -18.0 / 20.0);
    for (size_t i = 0; i < n; ++i) {
        double s = 0.0;
        for (double f : kHz) {
            s += std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / sampleRate + phaseOffset);
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

// Output-equivalence probe for the T5.09 optimisations (hidden, asserts nothing):
// renders each budget configuration, plus one whose morph cursor moves every block,
// and writes the raw float32 L/R output to t509_<name>.bin in the working directory.
// Two builds' files are compared sample by sample.
TEST_CASE("Probe: T5.09 render dump for A/B output equivalence", "[.][t509-render-dump]") {
    const auto dump = [](const char* name, const ts::PList& params, bool moveCursor,
                         double sampleRate = kCpuSampleRate) {
        ts::Rig rig(sampleRate, kCpuBlock);
        rig.apply(params);
        const size_t n = static_cast<size_t>(3.0 * sampleRate);
        const auto inL = fourTones(n, 0.0, sampleRate);
        const auto inR = fourTones(n, 0.3, sampleRate);
        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(inL, inR, outL, outR, kCpuBlock, [moveCursor](size_t blk, size_t, ts::PChanges& pc) {
            if (!moveCursor) return;
            const double x = 0.5 + 0.45 * std::sin(0.05 * static_cast<double>(blk));
            const double y = 0.5 + 0.45 * std::cos(0.07 * static_cast<double>(blk));
            for (uint8_t b = 0; b < kBands; ++b) {
                pc.addChange(makeBandParamId(b, BandParamType::kBandMorphX), x);
                pc.addChange(makeBandParamId(b, BandParamType::kBandMorphY), y);
            }
        });
        const std::string path = std::string("t509_") + name + ".bin";
        FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fwrite(outL.data(), sizeof(float), outL.size(), f);
        std::fwrite(outR.data(), sizeof(float), outR.size(), f);
        std::fclose(f);
    };
    using DT = DistortionType;
    dump("hardclip4x", fourBands({DT::HardClip, DT::HardClip, DT::HardClip, DT::HardClip}, 1), false);
    dump("formant4", fourBands({DT::Formant, DT::Formant, DT::Formant, DT::Formant}, 4), false);
    dump("expmix", fourBands({DT::Formant, DT::Chaos, DT::Granular, DT::Fractal}, 4), false);
    dump("cross4x", fourBands({DT::HardClip, DT::SineFold, DT::FullRectify, DT::RingSaturation}, 4), false);
    dump("cross4x_moving", fourBands({DT::HardClip, DT::SineFold, DT::FullRectify, DT::RingSaturation}, 4), true);
    dump("formant4_moving", fourBands({DT::Formant, DT::Formant, DT::Formant, DT::Formant}, 4), true);
    dump("mixed_moving", fourBands({DT::Tube, DT::SergeFold, DT::Bitcrush, DT::Fractal}, 4), true);
    // Each node type of the two morph budget cases alone (one node per band) at 44.1 and 96 kHz
    for (const auto t : {DT::Formant, DT::Chaos, DT::Granular, DT::Fractal, DT::HardClip, DT::SineFold,
                         DT::FullRectify, DT::RingSaturation}) {
        for (const double sr : {44100.0, 96000.0}) {
            const std::string name = std::string(getTypeName(t)) + "_" + std::to_string(static_cast<int>(sr));
            dump(name.c_str(), fourBands({t, t, t, t}, 1), false, sr);
        }
    }
}

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

// =============================================================================
// Audit T6.09 (probe "denormals in silence tail"): after 0.25 s of full-scale
// noise, 1 s of digital silence through one node costs no more without FTZ/DAZ
// than with them (slowdown <= 1.3x, the fastest of 5 runs each) and writes no
// denormal output sample. The test main sets FTZ/DAZ and the plugin sets none, so
// the case clears and sets them itself (x86 MXCSR; other targets skip it).
// Audit baseline (2026-11-05): slowdown 0.98 / 1.14 / 1.12 / 0.95 (FeedbackDist,
// AllpassResonant, Tube, SoftClip), 0 denormal samples.
// =============================================================================

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>

namespace {

/// Sets or clears FTZ (bit 15) and DAZ (bit 6) for its lifetime.
class MxcsrFlushGuard {
public:
    explicit MxcsrFlushGuard(bool flush) : saved_(_mm_getcsr()) {
        constexpr unsigned int kFtzDaz = 0x8000u | 0x0040u;
        _mm_setcsr(flush ? (saved_ | kFtzDaz) : (saved_ & ~kFtzDaz));
    }
    ~MxcsrFlushGuard() { _mm_setcsr(saved_); }
    MxcsrFlushGuard(const MxcsrFlushGuard&) = delete;
    MxcsrFlushGuard& operator=(const MxcsrFlushGuard&) = delete;
    MxcsrFlushGuard(MxcsrFlushGuard&&) = delete;
    MxcsrFlushGuard& operator=(MxcsrFlushGuard&&) = delete;

private:
    unsigned int saved_;
};

struct TailReading {
    double tailMs = 0.0;
    size_t denormalSamples = 0;
    size_t nonZeroSamples = 0;
};

TailReading silenceTail(const ts::PList& setup, bool flush) {
    const MxcsrFlushGuard guard(flush);
    constexpr double kSr = 48000.0;
    constexpr Steinberg::int32 kBlock = 512;
    ts::Rig rig(kSr, kBlock);
    rig.apply(setup);
    const auto burst = static_cast<size_t>(kSr * 0.25);
    const auto tail = static_cast<size_t>(kSr * 1.0);
    std::vector<float> noise(burst);
    uint32_t state = 12345u;
    for (auto& v : noise) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(static_cast<double>(state >> 8) / 8388608.0 - 1.0) * 0.9f;
    }
    std::vector<float> bL(static_cast<size_t>(kBlock));
    std::vector<float> bR(static_cast<size_t>(kBlock));
    std::vector<float> oL(static_cast<size_t>(kBlock));
    std::vector<float> oR(static_cast<size_t>(kBlock));
    for (size_t p = 0; p < burst; p += static_cast<size_t>(kBlock)) {
        const auto len = static_cast<Steinberg::int32>(std::min(static_cast<size_t>(kBlock), burst - p));
        std::copy_n(noise.begin() + static_cast<std::ptrdiff_t>(p), len, bL.begin());
        std::copy_n(noise.begin() + static_cast<std::ptrdiff_t>(p), len, bR.begin());
        rig.processBlock(bL.data(), bR.data(), oL.data(), oR.data(), len, nullptr);
    }
    std::fill(bL.begin(), bL.end(), 0.0f);
    std::fill(bR.begin(), bR.end(), 0.0f);
    TailReading r;
    constexpr float kSmallestNormal = std::numeric_limits<float>::min();
    for (size_t p = 0; p < tail; p += static_cast<size_t>(kBlock)) {
        const auto len = static_cast<Steinberg::int32>(std::min(static_cast<size_t>(kBlock), tail - p));
        const auto t0 = std::chrono::steady_clock::now();
        rig.processBlock(bL.data(), bR.data(), oL.data(), oR.data(), len, nullptr);
        const auto t1 = std::chrono::steady_clock::now();
        r.tailMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        for (Steinberg::int32 i = 0; i < len; ++i) {
            for (const float v : {oL[static_cast<size_t>(i)], oR[static_cast<size_t>(i)]}) {
                const float a = std::fabs(v);
                if (a != 0.0f) ++r.nonZeroSamples;
                if (a != 0.0f && a < kSmallestNormal) ++r.denormalSamples;
            }
        }
    }
    return r;
}

}  // namespace

TEST_CASE("Silence tails cost no more without FTZ/DAZ and hold no denormal sample",
          "[disrumpo][audit][denormal][performance]") {
    const auto shape = [](uint8_t slot, double v) {
        return std::pair<Steinberg::Vst::ParamID, double>{
            makeNodeParamId(0, 0, static_cast<NodeParamType>(static_cast<uint8_t>(NodeParamType::kNodeShape0) + slot)),
            v};
    };
    struct Case {
        const char* name;
        DistortionType type;
        ts::PList extra;
    };
    const Case cases[] = {
        {.name = "FeedbackDist (feedback 0.9, delay 10.9 ms, decay 0.5)",
         .type = DistortionType::FeedbackDist,
         .extra = {shape(0, 0.6), shape(1, 0.1), shape(8, 0.5)}},
        {.name = "AllpassResonant (topology 1, feedback 0.89, decay 5 s)",
         .type = DistortionType::AllpassResonant,
         .extra = {shape(0, 1.0 / 3.0), shape(2, 0.9), shape(3, 0.5)}},
        {.name = "Tube (defaults)", .type = DistortionType::Tube, .extra = {}},
        {.name = "SoftClip (defaults, control)", .type = DistortionType::SoftClip, .extra = {}},
    };
    for (const auto& c : cases) {
        ts::PList setup{{kBandCountId, 0.0},
                        {makeBandParamId(0, BandParamType::kBandActiveNodes), 0.0},
                        {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(c.type)},
                        {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.5}};
        setup.insert(setup.end(), c.extra.begin(), c.extra.end());
        double offMs = 1e30;
        double onMs = 1e30;
        TailReading off;
        // Fastest of 5 runs each, the order alternating so neither mode always runs first
        for (int rep = 0; rep < 5; ++rep) {
            const bool offFirst = rep % 2 == 0;
            const auto first = silenceTail(setup, !offFirst);
            const auto second = silenceTail(setup, offFirst);
            const auto& a = offFirst ? first : second;
            const auto& b = offFirst ? second : first;
            if (rep == 0) off = a;
            offMs = std::min(offMs, a.tailMs);
            onMs = std::min(onMs, b.tailMs);
        }
        const double slowdown = offMs / onMs;
        std::printf("T6.09 denormal tail %s: %.2f ms without FTZ/DAZ, %.2f ms with (slowdown %.2fx), denormal output "
                    "samples %zu, non-zero tail samples %zu\n",
                    c.name, offMs, onMs, slowdown, off.denormalSamples, off.nonZeroSamples);
        INFO(c.name << ": slowdown " << slowdown << "x, denormal output samples " << off.denormalSamples);
        CHECK(off.denormalSamples == 0u);
        CHECK(slowdown <= 1.3);
    }
}
#endif
