// =============================================================================
// Processor lifecycle vs. state: activation must not wipe user state
// =============================================================================
// Audit finding D-047 (fix plan T1.02): setActive(true) and setupProcessing
// called ModulationEngine::reset(), which cleared every mod routing and macro.
// The processor keeps those only inside the engine, so a host re-activation
// (or the usual load order setState -> setActive(true)) silently dropped the
// whole mod matrix, and the next getState saved it empty.
//
// Audit findings D-046 / D-048 / D-022 / D-054 / D-114 / D-115 / D-132 (fix
// plan T1.03): setupProcessing rebuilt the morph nodes as SoftClip, re-ran the
// log crossover distribution and recreated the MorphEngine (mode Linear1D,
// smoothing 10 ms); getState wrote 0 for morph mode and smoothing; setState
// applied crossovers before the band count, which dropped the upper ones.
//
// Audit findings D-049 / D-041 / D-042 (fix plan T1.04): setState restored
// sweep width and intensity only into the SweepProcessor, so the first block
// reset them from the stale base atomics; getState saved the LFO-modulated
// sweep frequency/width/intensity instead of the user's base values.
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include "controller/controller.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

// Byte offsets of the v12 state layout written by Processor::getState
// (processor_state.cpp). Everything before the custom-curve breakpoints is
// fixed-size; the breakpoint count is read from the stream.
constexpr auto kBands = static_cast<size_t>(kMaxBands);
constexpr size_t kCurveCountOffset = 4 + 3 * 4 + 4   // version, 3 globals, band count
                                   + kBands * (4 + 4 + 3)  // per band: gain, pan, solo/bypass/mute
                                   + (kBands - 1) * 4      // crossovers
                                   + 15               // sweep core
                                   + 12               // sweep LFO
                                   + 13;              // sweep envelope
constexpr size_t kModSourcesSize = 13 + 13  // LFO 1, LFO 2
                                 + 13       // envelope follower
                                 + 9 + 9 + 9  // random, chaos, S&H
                                 + 16 + 12  // pitch follower, transient
                                 + 10;      // rungler
constexpr size_t kMacroSize = 4 + 4 + 4 + 1;

template <typename T>
T readAt(const std::vector<uint8_t>& b, size_t off) {
    T v{};
    REQUIRE(off + sizeof(T) <= b.size());
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

size_t macrosOffset(const std::vector<uint8_t>& b) {
    const auto points = readAt<int32_t>(b, kCurveCountOffset);
    REQUIRE(points >= 0);
    return kCurveCountOffset + 4 + static_cast<size_t>(points) * 8 + kModSourcesSize;
}

size_t routing0Offset(const std::vector<uint8_t>& b) {
    return macrosOffset(b) + Krate::DSP::kMaxMacros * kMacroSize;
}

// LFO 1 -> Input Gain at full amount, macro 1 at 0.8.
PList modMatrixPatch() {
    return {
        {makeRoutingParamId(0, 0), 1.0 / static_cast<double>(kUIModSourceCount - 1)},  // LFO 1
        {makeRoutingParamId(0, 1),
         static_cast<double>(ModDest::kInputGain) / static_cast<double>(ModDest::kTotalDestinations - 1)},
        {makeRoutingParamId(0, 2), 1.0},  // amount +1
        {makeModParamId(ModParamType::kMacro1Value), 0.8},
    };
}

// -----------------------------------------------------------------------------
// Patch view of a state stream (D-046 / T1.03): the fields setupProcessing and
// setState used to lose. Handles the v10 factory-preset layout (no Rungler
// block) and the current v12 layout.
// -----------------------------------------------------------------------------
constexpr size_t kCrossoverOffset = 4 + 3 * 4 + 4 + kBands * (4 + 4 + 3);
constexpr size_t kRunglerSize = 10;           // v11+
constexpr size_t kRoutingsSize = size_t{32} * 10;     // source, dest, amount, curve
constexpr size_t kMorphNodeSize = 1 + 6 * 4   // type + 6 floats
                                + 10 * 4      // shape slots (v9+)
                                + 26 * 10 * 4;  // per-type shadow (v9+)
// Band morph header: X, Y, mode, active nodes, smoothing; v13+ adds the X and Y
// sweep-link modes (int8 each, D-137 / T1.12).
constexpr size_t kMorphBandHeaderV12 = 4 + 4 + 1 + 1 + 4;
constexpr size_t kLinkBytes = 2;
size_t morphBandHeader(int32_t version) { return kMorphBandHeaderV12 + (version >= 13 ? kLinkBytes : 0); }
size_t morphBandSize(int32_t version) { return morphBandHeader(version) + 4 * kMorphNodeSize; }

struct PatchView {
    int32_t version = 0;
    int32_t bandCount = 0;
    float crossover[kBands - 1] = {};
    struct Band {
        float x = 0.0f;
        float y = 0.0f;
        int8_t mode = 0;
        int8_t activeNodes = 0;
        float smoothingMs = 0.0f;
        int8_t xLink = 0;  // v13+, 0 (None) for older streams
        int8_t yLink = 0;
        int8_t types[kMaxMorphNodes] = {};
    } bands[kBands];
};

size_t morphOffset(const std::vector<uint8_t>& b) {
    const auto version = readAt<int32_t>(b, 0);
    const auto points = readAt<int32_t>(b, kCurveCountOffset);
    REQUIRE(points >= 0);
    const size_t sources = kModSourcesSize - (version >= 11 ? 0 : kRunglerSize);
    return kCurveCountOffset + 4 + static_cast<size_t>(points) * 8 + sources
         + Krate::DSP::kMaxMacros * kMacroSize + kRoutingsSize;
}

PatchView decodePatch(const std::vector<uint8_t>& b) {
    PatchView v;
    v.version = readAt<int32_t>(b, 0);
    REQUIRE(v.version >= 10);
    v.bandCount = readAt<int32_t>(b, 16);
    for (size_t c = 0; c + 1 < kBands; ++c) v.crossover[c] = readAt<float>(b, kCrossoverOffset + 4 * c);
    const size_t m0 = morphOffset(b);
    for (size_t band = 0; band < kBands; ++band) {
        const size_t o = m0 + band * morphBandSize(v.version);
        auto& pb = v.bands[band];
        pb.x = readAt<float>(b, o);
        pb.y = readAt<float>(b, o + 4);
        pb.mode = readAt<int8_t>(b, o + 8);
        pb.activeNodes = readAt<int8_t>(b, o + 9);
        pb.smoothingMs = readAt<float>(b, o + 10);
        if (v.version >= 13) {
            pb.xLink = readAt<int8_t>(b, o + kMorphBandHeaderV12);
            pb.yLink = readAt<int8_t>(b, o + kMorphBandHeaderV12 + 1);
        }
        for (size_t n = 0; n < static_cast<size_t>(kMaxMorphNodes); ++n)
            pb.types[n] = readAt<int8_t>(b, o + morphBandHeader(v.version) + n * kMorphNodeSize);
    }
    return v;
}

// Component chunk ("Comp") of a .vstpreset file.
std::vector<uint8_t> readPresetComponent(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(file.size() > size_t{48});
    REQUIRE(std::memcmp(file.data(), "VST3", 4) == 0);
    const auto listOffset = static_cast<size_t>(readAt<int64_t>(file, 40));
    REQUIRE(std::memcmp(file.data() + listOffset, "List", 4) == 0);
    const auto entries = readAt<uint32_t>(file, listOffset + 4);
    for (uint32_t i = 0; i < entries; ++i) {
        const size_t e = listOffset + 8 + static_cast<size_t>(i) * 20;
        if (std::memcmp(file.data() + e, "Comp", 4) == 0) {
            const auto off = static_cast<size_t>(readAt<int64_t>(file, e + 4));
            const auto size = static_cast<size_t>(readAt<int64_t>(file, e + 12));
            REQUIRE(off + size <= file.size());
            return {file.begin() + static_cast<std::ptrdiff_t>(off),
                    file.begin() + static_cast<std::ptrdiff_t>(off + size)};
        }
    }
    FAIL("no Comp chunk in " << path);
    return {};
}

size_t firstDifference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return i;
    return a.size() == b.size() ? a.size() : n;
}

double crossoverNormToHz(double norm) {
    const double logMin = std::log10(static_cast<double>(kMinCrossoverHz));
    const double logMax = std::log10(static_cast<double>(kMaxCrossoverHz));
    return std::pow(10.0, logMin + norm * (logMax - logMin));
}

// Spread (max - min) of the per-block output RMS in dB, skipping the first
// `skipBlocks` blocks (filter start-up and the 120 ms amount smoother ramp).
double blockRmsSpreadDb(const std::vector<float>& x, size_t block, size_t skipBlocks) {
    double lo = 1e9;
    double hi = -1e9;
    for (size_t s = skipBlocks * block; s + block <= x.size(); s += block) {
        const double db = toDb(rmsOf(x.data() + s, block));
        lo = std::min(lo, db);
        hi = std::max(hi, db);
    }
    return hi - lo;
}

}  // namespace

TEST_CASE("Mod routings and macros survive setActive(false)/setActive(true)",
          "[disrumpo][lifecycle][D-047]") {
    constexpr double kSampleRate = 48000.0;
    Rig rig(kSampleRate);
    rig.apply(modMatrixPatch());

    SECTION("getState is unchanged by a re-activation") {
        const auto a = getStateBytes(*rig.p);
        REQUIRE(readAt<int32_t>(a, 0) == kPresetVersion);
        REQUIRE(readAt<uint8_t>(a, routing0Offset(a)) == static_cast<uint8_t>(Krate::DSP::ModSource::LFO1));

        rig.reactivate();
        const auto b = getStateBytes(*rig.p);

        const size_t r0 = routing0Offset(b);
        const size_t m0 = macrosOffset(b);
        const auto source = readAt<uint8_t>(b, r0);
        const auto amount = readAt<float>(b, r0 + 1 + 4);
        const auto macro0 = readAt<float>(b, m0);
        std::printf("D-047 after reactivate: routing0 source=%u amount=%.3f macro0=%.3f\n",
                    static_cast<unsigned>(source), static_cast<double>(amount),
                    static_cast<double>(macro0));

        CHECK(source == static_cast<uint8_t>(Krate::DSP::ModSource::LFO1));
        CHECK(amount == 1.0f);
        CHECK(macro0 == 0.8f);

        // A == B, reported as the first differing byte (not an 18 KB dump).
        REQUIRE(a.size() == b.size());
        const auto firstDiff = static_cast<size_t>(
            std::mismatch(a.begin(), a.end(), b.begin()).first - a.begin());
        INFO("first differing state byte: " << firstDiff << " (routing 0 at " << r0
                                            << ", macros at " << m0 << ")");
        REQUIRE(firstDiff == a.size());
    }

    SECTION("LFO 1 -> Input Gain still modulates the output after re-activation") {
        constexpr double kTwoPi = 6.283185307179586476925;
        constexpr size_t kLen = 24000;  // 0.5 s
        constexpr size_t kBlock = 512;
        constexpr size_t kSkipBlocks = 14;  // ~0.15 s
        const double amp = std::pow(10.0, -24.0 / 20.0);
        std::vector<float> in(kLen);
        for (size_t i = 0; i < kLen; ++i)
            in[i] = static_cast<float>(amp * std::sin(kTwoPi * 1000.0 * static_cast<double>(i) / kSampleRate));

        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(in, in, outL, outR, static_cast<int32>(kBlock));
        const double before = blockRmsSpreadDb(outL, kBlock, kSkipBlocks);

        rig.reactivate();
        rig.render(in, in, outL, outR, static_cast<int32>(kBlock));
        const double after = blockRmsSpreadDb(outL, kBlock, kSkipBlocks);

        std::printf("D-047 block-RMS spread: before=%.2f dB after=%.2f dB\n", before, after);
        REQUIRE(before > 1.0);
        REQUIRE(after > 1.0);
    }
}

TEST_CASE("State restored before setupProcessing survives it byte for byte",
          "[disrumpo][lifecycle][D-046][D-115]") {
    // Host load order: initialize -> setState -> setupProcessing -> setActive.
    // Radial_Blend: 3 bands, crossovers 200/3500(/6000), band 0 Radial2D with
    // 4 active nodes (Tube, SineFold, ...). Multi_Band_Morph: 4 bands at
    // 150/1200/5000 Hz. Smooth_Crossfade: 2 bands, 250 ms morph smoothing.
    const char* presets[] = {"Morph/Radial_Blend.vstpreset", "Morph/Multi_Band_Morph.vstpreset",
                             "Morph/Smooth_Crossfade.vstpreset"};
    for (const char* name : presets) {
        DYNAMIC_SECTION(name) {
            const auto preset = readPresetComponent(std::string(DISRUMPO_RESOURCES_DIR "/presets/") + name);
            const PatchView want = decodePatch(preset);

            Rig rig(48000.0, 512, /*doSetup*/ false);
            REQUIRE(setStateBytes(*rig.p, preset));
            const auto a = getStateBytes(*rig.p);
            rig.setup();
            const auto b = getStateBytes(*rig.p);
            const PatchView got = decodePatch(b);

            for (size_t c = 0; c + 1 < kBands; ++c) {
                INFO("crossover " << c);
                CHECK(std::fabs(got.crossover[c] - want.crossover[c]) < 0.1f);
            }
            for (size_t band = 0; band < kBands; ++band) {
                INFO("band " << band);
                CHECK(got.bands[band].mode == want.bands[band].mode);
                CHECK(got.bands[band].activeNodes == want.bands[band].activeNodes);
                CHECK(got.bands[band].smoothingMs == want.bands[band].smoothingMs);
                for (size_t n = 0; n < static_cast<size_t>(kMaxMorphNodes); ++n) {
                    INFO("node " << n);
                    CHECK(got.bands[band].types[n] == want.bands[band].types[n]);
                }
            }

            // setupProcessing must not change a single saved byte.
            const size_t diff = firstDifference(a, b);
            INFO("first differing state byte: " << diff << " of " << a.size() << " (crossovers at "
                                                << kCrossoverOffset << ", morph at " << morphOffset(b) << ")");
            REQUIRE(a.size() == b.size());
            REQUIRE(diff == a.size());
        }
    }
}

TEST_CASE("Sample-rate change keeps nodes, crossovers, morph mode and smoothing",
          "[disrumpo][lifecycle][D-046][D-132]") {
    Rig rig(48000.0);
    rig.apply({
        {makeNodeParamId(0, 0, NodeParamType::kNodeType), 2.0 / 25.0},  // Tube
        {makeBandParamId(0, BandParamType::kBandMorphMode), 1.0},       // Radial2D
        {makeBandParamId(0, BandParamType::kBandMorphSmoothing), 0.4},  // 200 ms
        {makeCrossoverParamId(0), 0.5},
    });
    const auto a = getStateBytes(*rig.p);
    const PatchView before = decodePatch(a);
    REQUIRE(before.bands[0].types[0] == static_cast<int8_t>(DistortionType::Tube));
    REQUIRE(std::fabs(static_cast<double>(before.crossover[0]) - crossoverNormToHz(0.5)) < 0.1);

    rig.deactivate();
    rig.sr = 96000.0;
    rig.setup();
    const auto b = getStateBytes(*rig.p);
    const PatchView after = decodePatch(b);
    std::printf("D-046 after 48k->96k: node0 type=%d crossover0=%.1f Hz mode=%d smoothing=%.1f ms\n",
                static_cast<int>(after.bands[0].types[0]), static_cast<double>(after.crossover[0]),
                static_cast<int>(after.bands[0].mode), static_cast<double>(after.bands[0].smoothingMs));
    CHECK(after.bands[0].types[0] == static_cast<int8_t>(DistortionType::Tube));
    CHECK(std::fabs(static_cast<double>(after.crossover[0]) - crossoverNormToHz(0.5)) < 0.1);
    CHECK(after.bands[0].mode == static_cast<int8_t>(MorphMode::Radial2D));
    CHECK(after.bands[0].smoothingMs == 200.0f);
    {
        // sampleRate is not part of the stream, so nothing may change.
        const size_t diff = firstDifference(a, b);
        INFO("first differing state byte: " << diff << " of " << a.size());
        CHECK(a.size() == b.size());
        CHECK(diff == a.size());
    }

    // The engine, not just the cache, must run Tube: compare a 440 Hz render
    // against a fresh instance at the same rate with the same crossover, so
    // the residual comes from the node type alone.
    constexpr double kTwoPi = 6.283185307179586476925;
    constexpr size_t kLen = 24000;  // 0.25 s at 96 kHz
    const double amp = std::pow(10.0, -6.0 / 20.0);
    std::vector<float> in(kLen);
    for (size_t i = 0; i < kLen; ++i)
        in[i] = static_cast<float>(amp * std::sin(kTwoPi * 440.0 * static_cast<double>(i) / 96000.0));
    Rig fresh(96000.0);
    fresh.apply({{makeCrossoverParamId(0), 0.5}});
    std::vector<float> outL;
    std::vector<float> outR;
    std::vector<float> refL;
    std::vector<float> refR;
    rig.render(in, in, outL, outR);
    fresh.render(in, in, refL, refR);
    constexpr size_t kSkip = 4800;  // 50 ms start-up
    std::vector<float> residual(kLen - kSkip);
    for (size_t i = kSkip; i < kLen; ++i) residual[i - kSkip] = outL[i] - refL[i];
    const double refDb = toDb(rmsOf(refL.data() + kSkip, kLen - kSkip));
    const double outDb = toDb(rmsOf(outL.data() + kSkip, kLen - kSkip));
    const double residualDb = toDb(rmsOf(residual.data(), residual.size())) - refDb;
    std::printf("D-046 96k render: Tube %.2f dB, SoftClip %.2f dB, residual %.2f dB re SoftClip\n", outDb,
                refDb, residualDb);
    // Measured: Tube vs SoftClip residual -21.2 dB. Two default patches (what
    // the pre-fix setupProcessing left) differ by -75.7 dB, the noise floor.
    REQUIRE(residualDb > -30.0);
}

TEST_CASE("setState with 4 bands into a 2-band instance restores all crossovers",
          "[disrumpo][lifecycle][D-022][D-054]") {
    const double norms[3] = {0.4, 0.6, 0.8};
    std::vector<uint8_t> source;
    {
        Rig four(48000.0);
        four.apply({{makeCrossoverParamId(0), norms[0]},
                    {makeCrossoverParamId(1), norms[1]},
                    {makeCrossoverParamId(2), norms[2]}});
        source = getStateBytes(*four.p);
    }
    const PatchView saved = decodePatch(source);
    REQUIRE(saved.bandCount == 4);
    for (size_t c = 0; c < 3; ++c)
        REQUIRE(std::fabs(static_cast<double>(saved.crossover[c]) - crossoverNormToHz(norms[c])) < 0.1);

    SECTION("through setState") {
        Rig rig(48000.0);
        rig.apply({{kBandCountId, 1.0 / 3.0}});  // 2 bands
        REQUIRE(decodePatch(getStateBytes(*rig.p)).bandCount == 2);
        REQUIRE(setStateBytes(*rig.p, source));
        const PatchView got = decodePatch(getStateBytes(*rig.p));
        std::printf("D-022 setState 4 bands into 2: %.1f / %.1f / %.1f Hz (saved %.1f / %.1f / %.1f)\n",
                    static_cast<double>(got.crossover[0]), static_cast<double>(got.crossover[1]),
                    static_cast<double>(got.crossover[2]), static_cast<double>(saved.crossover[0]),
                    static_cast<double>(saved.crossover[1]), static_cast<double>(saved.crossover[2]));
        REQUIRE(got.bandCount == 4);
        for (size_t c = 0; c < 3; ++c) {
            INFO("crossover " << c);
            CHECK(std::fabs(got.crossover[c] - saved.crossover[c]) < 0.1f);
        }
    }

    SECTION("crossover queues delivered before the band-count queue in one block") {
        Rig rig(48000.0);
        rig.apply({{kBandCountId, 1.0 / 3.0}});  // 2 bands
        rig.apply({{makeCrossoverParamId(0), norms[0]},
                   {makeCrossoverParamId(1), norms[1]},
                   {makeCrossoverParamId(2), norms[2]},
                   {kBandCountId, 1.0}});  // 4 bands, last in the block
        const PatchView got = decodePatch(getStateBytes(*rig.p));
        REQUIRE(got.bandCount == 4);
        for (size_t c = 0; c < 3; ++c) {
            INFO("crossover " << c);
            CHECK(std::fabs(got.crossover[c] - saved.crossover[c]) < 0.1f);
        }
    }
}

TEST_CASE("Sweep width/intensity/frequency survive setState + one block and are saved unmodulated",
          "[disrumpo][lifecycle][D-049][D-042]") {
    // Sweep core: enable (int8), frequency, width, intensity (normalized floats).
    constexpr size_t kSweepCoreOffset = kCrossoverOffset + (kBands - 1) * 4;
    struct SweepView {
        float freq;
        float width;
        float intensity;
    };
    const auto decodeSweep = [](const std::vector<uint8_t>& b) {
        return SweepView{.freq = readAt<float>(b, kSweepCoreOffset + 1),
                         .width = readAt<float>(b, kSweepCoreOffset + 5),
                         .intensity = readAt<float>(b, kSweepCoreOffset + 9)};
    };

    constexpr double kSampleRate = 48000.0;
    constexpr double kTwoPi = 6.283185307179586476925;
    constexpr size_t kLen = 9600;  // 0.2 s
    const double amp = std::pow(10.0, -12.0 / 20.0);
    std::vector<float> in(kLen);
    for (size_t i = 0; i < kLen; ++i)
        in[i] = static_cast<float>(amp * std::sin(kTwoPi * 440.0 * static_cast<double>(i) / kSampleRate));
    std::vector<float> outL;
    std::vector<float> outR;

    std::vector<uint8_t> a;
    {
        Rig rig(kSampleRate);
        rig.apply({
            {makeSweepParamId(SweepParamType::kSweepEnable), 1.0},
            {makeSweepParamId(SweepParamType::kSweepWidth), 0.9},
            {makeSweepParamId(SweepParamType::kSweepIntensity), 0.8},
            {makeSweepParamId(SweepParamType::kSweepFrequency), 0.5},
            {makeSweepParamId(SweepParamType::kSweepLFOEnable), 1.0},
            {makeSweepParamId(SweepParamType::kSweepLFODepth), 1.0},
        });
        rig.render(in, in, outL, outR);
        a = getStateBytes(*rig.p);
    }
    const SweepView sa = decodeSweep(a);
    std::printf("D-042 saved after 0.2 s with LFO: freq=%.6f width=%.6f intensity=%.6f\n",
                static_cast<double>(sa.freq), static_cast<double>(sa.width), static_cast<double>(sa.intensity));
    CHECK(std::fabs(sa.freq - 0.5f) < 1e-5f);
    CHECK(std::fabs(sa.width - 0.9f) < 1e-5f);
    CHECK(std::fabs(sa.intensity - 0.8f) < 1e-5f);

    Rig fresh(kSampleRate);
    REQUIRE(setStateBytes(*fresh.p, a));
    const std::vector<float> oneBlock(in.begin(), in.begin() + 512);
    fresh.render(oneBlock, oneBlock, outL, outR);
    const SweepView sb = decodeSweep(getStateBytes(*fresh.p));
    std::printf("D-049 after setState + one block: freq=%.6f width=%.6f intensity=%.6f\n",
                static_cast<double>(sb.freq), static_cast<double>(sb.width), static_cast<double>(sb.intensity));
    CHECK(std::fabs(sb.freq - sa.freq) < 1e-5f);
    CHECK(std::fabs(sb.width - sa.width) < 1e-5f);
    CHECK(std::fabs(sb.intensity - sa.intensity) < 1e-5f);
}

// -----------------------------------------------------------------------------
// D-137 (T1.12, STATE-COMPAT v12 -> v13): the per-band Morph X/Y Link modes are
// saved after each band's morph smoothing (int8 each). A v12 stream, captured
// from Processor::getState before the change (tests/fixtures/
// disrumpo_state_v12.bin), loads with both links None and every other field as
// it was.
// -----------------------------------------------------------------------------
namespace {

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Band 1: X Stepped, Y Hold-Rise. Band 0: X Custom. Band 3: Y Sweep Freq.
PList linkPatch() {
    return {
        {makeBandParamId(1, BandParamType::kBandMorphXLink), 6.0 / 7.0},
        {makeBandParamId(1, BandParamType::kBandMorphYLink), 5.0 / 7.0},
        {makeBandParamId(0, BandParamType::kBandMorphXLink), 1.0},
        {makeBandParamId(3, BandParamType::kBandMorphYLink), 1.0 / 7.0},
    };
}
constexpr int8_t kWantXLink[kBands] = {7, 6, 0, 0};
constexpr int8_t kWantYLink[kBands] = {0, 5, 0, 1};

// The same v12 content in the v13 layout: version 13, both links None per band.
std::vector<uint8_t> asV13WithNoLinks(const std::vector<uint8_t>& v12) {
    REQUIRE(readAt<int32_t>(v12, 0) == 12);
    std::vector<uint8_t> out = v12;
    const int32_t v13 = 13;
    std::memcpy(out.data(), &v13, sizeof(v13));
    const size_t m0 = morphOffset(v12);
    for (size_t band = kBands; band-- > 0;) {  // back to front keeps earlier offsets valid
        const size_t at = m0 + band * morphBandSize(12) + kMorphBandHeaderV12;
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), kLinkBytes, uint8_t{0});
    }
    return out;
}

std::vector<uint8_t> streamBytes(Steinberg::IBStream* s) {
    Steinberg::int64 sz = 0;
    s->seek(0, Steinberg::IBStream::kIBSeekEnd, &sz);
    s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<uint8_t> out(static_cast<size_t>(sz));
    Steinberg::int32 got = 0;
    if (sz > 0) s->read(out.data(), static_cast<Steinberg::int32>(sz), &got);
    return out;
}

constexpr size_t kSweepMorphLinkOffset = kCrossoverOffset + (kBands - 1) * 4 + 14;

}  // namespace

TEST_CASE("Morph link modes round-trip through processor state",
          "[disrumpo][state][STATE-COMPAT][D-137]") {
    SECTION("getState -> setState into a fresh instance -> getState") {
        std::vector<uint8_t> a;
        {
            Rig rig(48000.0);
            rig.apply(linkPatch());
            a = getStateBytes(*rig.p);
        }
        const PatchView pa = decodePatch(a);
        std::printf("D-137 saved: version=%d band1 xLink=%d yLink=%d band0 xLink=%d band3 yLink=%d\n",
                    pa.version, pa.bands[1].xLink, pa.bands[1].yLink, pa.bands[0].xLink, pa.bands[3].yLink);
        REQUIRE(pa.version == 13);
        for (size_t band = 0; band < kBands; ++band) {
            INFO("band " << band);
            CHECK(pa.bands[band].xLink == kWantXLink[band]);
            CHECK(pa.bands[band].yLink == kWantYLink[band]);
        }

        std::vector<uint8_t> b;
        {
            Rig fresh(48000.0);
            REQUIRE(setStateBytes(*fresh.p, a));
            b = getStateBytes(*fresh.p);
        }
        const PatchView pb = decodePatch(b);
        for (size_t band = 0; band < kBands; ++band) {
            INFO("band " << band);
            CHECK(pb.bands[band].xLink == kWantXLink[band]);
            CHECK(pb.bands[band].yLink == kWantYLink[band]);
        }
        // The whole morph section (every band header with its links, every node)
        // saves back byte for byte. The sweep-envelope and mod-source float codecs
        // ahead of it move by 1 ulp per round trip (first difference printed).
        REQUIRE(a.size() == b.size());
        const size_t m0 = morphOffset(a);
        const std::vector<uint8_t> morphA(a.begin() + static_cast<std::ptrdiff_t>(m0), a.end());
        const std::vector<uint8_t> morphB(b.begin() + static_cast<std::ptrdiff_t>(m0), b.end());
        std::printf("D-137 round trip: first differing byte %zu of %zu, morph section from %zu: first difference %zu of %zu\n",
                    firstDifference(a, b), a.size(), m0, firstDifference(morphA, morphB), morphA.size());
        REQUIRE(firstDifference(morphA, morphB) == morphA.size());
    }

    SECTION("v12 fixture loads with both link modes None and every other field unchanged") {
        const auto fixture = readFileBytes(DISRUMPO_FIXTURES_DIR "/disrumpo_state_v12.bin");
        REQUIRE(readAt<int32_t>(fixture, 0) == 12);
        std::vector<uint8_t> s13;
        {
            Rig rig(48000.0);
            rig.apply(linkPatch());  // a v12 load must reset links set before it
            REQUIRE(setStateBytes(*rig.p, fixture));
            s13 = getStateBytes(*rig.p);
        }
        std::vector<uint8_t> t13;
        {
            Rig rig(48000.0);
            REQUIRE(setStateBytes(*rig.p, asV13WithNoLinks(fixture)));
            t13 = getStateBytes(*rig.p);
        }
        const PatchView want = decodePatch(fixture);
        const PatchView got = decodePatch(s13);
        std::printf("D-137 v12 fixture (%zu bytes) re-saved: version=%d size=%zu links b0..b3 x=%d,%d,%d,%d "
                    "y=%d,%d,%d,%d; first difference vs the same content in v13 layout: %zu of %zu\n",
                    fixture.size(), got.version, s13.size(), got.bands[0].xLink, got.bands[1].xLink,
                    got.bands[2].xLink, got.bands[3].xLink, got.bands[0].yLink, got.bands[1].yLink,
                    got.bands[2].yLink, got.bands[3].yLink, firstDifference(s13, t13), s13.size());
        REQUIRE(got.version == 13);
        CHECK(s13.size() == fixture.size() + kBands * kLinkBytes);
        CHECK(got.bandCount == want.bandCount);
        CHECK(readAt<int8_t>(s13, kSweepMorphLinkOffset) == readAt<int8_t>(fixture, kSweepMorphLinkOffset));
        for (size_t c = 0; c + 1 < kBands; ++c) CHECK(got.crossover[c] == want.crossover[c]);
        for (size_t band = 0; band < kBands; ++band) {
            INFO("band " << band);
            CHECK(got.bands[band].xLink == 0);
            CHECK(got.bands[band].yLink == 0);
            CHECK(got.bands[band].x == want.bands[band].x);
            CHECK(got.bands[band].y == want.bands[band].y);
            CHECK(got.bands[band].mode == want.bands[band].mode);
            CHECK(got.bands[band].activeNodes == want.bands[band].activeNodes);
            CHECK(got.bands[band].smoothingMs == want.bands[band].smoothingMs);
            for (size_t n = 0; n < static_cast<size_t>(kMaxMorphNodes); ++n) {
                INFO("node " << n);
                CHECK(got.bands[band].types[n] == want.bands[band].types[n]);
            }
        }
        // Every other byte: the v12 load equals a load of the same content in the v13 layout.
        REQUIRE(s13.size() == t13.size());
        REQUIRE(firstDifference(s13, t13) == s13.size());
    }
}

TEST_CASE("Morph link modes survive the controller's preset parser and writer",
          "[disrumpo][state][STATE-COMPAT][D-137]") {
    std::vector<uint8_t> a;
    {
        Rig rig(48000.0);
        rig.apply(linkPatch());
        a = getStateBytes(*rig.p);
    }

    Controller ctrl;
    REQUIRE(ctrl.initialize(nullptr) == Steinberg::kResultOk);
    {
        auto* in = new Steinberg::MemoryStream(a.data(), static_cast<Steinberg::TSize>(a.size()));
        CHECK(ctrl.setComponentState(in) == Steinberg::kResultOk);
        in->release();
    }
    const double xLinkNorm = ctrl.getParamNormalized(makeBandParamId(1, BandParamType::kBandMorphXLink));
    const double yLinkNorm = ctrl.getParamNormalized(makeBandParamId(1, BandParamType::kBandMorphYLink));
    std::vector<uint8_t> written;
    {
        auto* out = ctrl.createComponentStateStream();
        REQUIRE(out != nullptr);
        written = streamBytes(out);
        out->release();
    }
    ctrl.terminate();

    const PatchView pw = decodePatch(written);
    std::printf("D-137 controller: band1 X link norm %.6f Y link norm %.6f; preset stream version=%d "
                "links b0..b3 x=%d,%d,%d,%d y=%d,%d,%d,%d\n",
                xLinkNorm, yLinkNorm, pw.version, pw.bands[0].xLink, pw.bands[1].xLink, pw.bands[2].xLink,
                pw.bands[3].xLink, pw.bands[0].yLink, pw.bands[1].yLink, pw.bands[2].yLink, pw.bands[3].yLink);
    CHECK(std::fabs(xLinkNorm - 6.0 / 7.0) < 1e-9);
    CHECK(std::fabs(yLinkNorm - 5.0 / 7.0) < 1e-9);
    REQUIRE(pw.version == 13);

    std::vector<uint8_t> d;
    {
        Rig fresh(48000.0);
        REQUIRE(setStateBytes(*fresh.p, written));
        d = getStateBytes(*fresh.p);
    }
    const PatchView pd = decodePatch(d);
    for (size_t band = 0; band < kBands; ++band) {
        INFO("band " << band);
        CHECK(pw.bands[band].xLink == kWantXLink[band]);
        CHECK(pw.bands[band].yLink == kWantYLink[band]);
        CHECK(pd.bands[band].xLink == kWantXLink[band]);
        CHECK(pd.bands[band].yLink == kWantYLink[band]);
    }
}
