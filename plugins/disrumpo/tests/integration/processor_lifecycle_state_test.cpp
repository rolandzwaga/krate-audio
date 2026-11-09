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
//
// Audit findings D-050 / D-156 (fix plan T1.15): setState restored each node's
// shape slots but never mapped them onto the type-specific DistortionParams, so
// a host restore kept the defaults (fresh instance) or the previous patch's
// values (live instance). Rectified_Drums' HalfRectify band compared against
// the default threshold 0.7 instead of 0.3 and fell silent.
//
// Audit finding D-003 (fix plan T1.24): setState wrote the live crossovers,
// band processors, morph engines and mod matrix from the host thread while the
// audio thread could be inside process(). While processing is active it now
// parses into a pending state that the next process() applies; live DSP objects
// (and so the engine-reading tests below) see a load from the next block on.
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include "controller/controller.h"
#include "dsp/crossover_network.h"
#include "dsp/morph_engine.h"
#include "plugin_ids.h"

#include <allocation_detector.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <thread>
#include <utility>
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

// D-096 (T1.18): MIDI CC reaches a plugin (and its IMidiMapping is queried) only
// through an event input bus.
TEST_CASE("Processor declares one event input bus", "[disrumpo][midi][D-096]") {
    Rig rig(48000.0, 512, false);
    const auto count = rig.p->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
    std::printf("D-096 event input buses: %d, event output buses: %d\n", static_cast<int>(count),
                static_cast<int>(rig.p->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kOutput)));
    REQUIRE(count == 1);
    Steinberg::Vst::BusInfo info{};
    REQUIRE(rig.p->getBusInfo(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, 0, info) == Steinberg::kResultOk);
    CHECK(info.channelCount == 16);
    CHECK(info.busType == Steinberg::Vst::kMain);
}

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
    // Off the even log grid a band-count increase fills idle crossovers with (0.4 / 0.6 / 0.8
    // is on it, so a dropped crossover came back at its own value and the case could not fail)
    const double norms[3] = {0.4, 0.5, 0.9};
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
        rig.apply({});  // apply the load (D-003); getState of a pending load reads the stream back
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

// D-124 (T5.04): a stream whose crossovers sit closer than half an octave
// (another host's automation, an old project) loads with each running crossover
// held at 2^0.5 times the one below, as the spectrum display's drag keeps them.
TEST_CASE("setState holds loaded crossovers half an octave apart", "[disrumpo][lifecycle][crossover][D-124]") {
    std::vector<uint8_t> stream;
    {
        Rig source(48000.0);
        stream = getStateBytes(*source.p);
    }
    REQUIRE(decodePatch(stream).bandCount == 4);
    const float written[3] = {1000.0f, 1200.0f, 1300.0f};
    for (size_t c = 0; c < 3; ++c) std::memcpy(stream.data() + kCrossoverOffset + 4 * c, &written[c], 4);

    Rig rig(48000.0);
    REQUIRE(setStateBytes(*rig.p, stream));
    rig.apply({});  // a load made while active lands at the next block (D-003)
    const PatchView got = decodePatch(getStateBytes(*rig.p));
    const float ratio = std::pow(2.0f, kMinCrossoverSpacingOctaves);
    std::printf("D-124 state crossovers 1000 / 1200 / 1300 Hz load as %.2f / %.2f / %.2f Hz\n",
                static_cast<double>(got.crossover[0]), static_cast<double>(got.crossover[1]),
                static_cast<double>(got.crossover[2]));
    REQUIRE(got.bandCount == 4);
    CHECK(got.crossover[0] == 1000.0f);
    CHECK(got.crossover[1] >= got.crossover[0] * ratio);
    CHECK(got.crossover[2] >= got.crossover[1] * ratio);
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

// -----------------------------------------------------------------------------
// D-050 / D-156 (T1.15): setState re-derives every node's type-specific
// DistortionParams from the restored shape slots, as replaying the parameters
// does (Processor::mapShapeSlotsToParams over DistortionParams{} plus the
// stream's bias / folds / bitDepth).
// -----------------------------------------------------------------------------
namespace {

// Per-node record of the morph section: type, drive, mix, tone, bias, folds,
// bitDepth, then the 10 shape slots.
constexpr size_t kNodeBiasOffset = 1 + 3 * 4;
constexpr size_t kNodeFoldsOffset = kNodeBiasOffset + 4;
constexpr size_t kNodeBitDepthOffset = kNodeFoldsOffset + 4;
constexpr size_t kNodeSlotsOffset = kNodeBitDepthOffset + 4;

struct SavedNode {
    DistortionType type = DistortionType::SoftClip;
    DistortionParams params;  // what replaying the node's parameters derives
};

SavedNode savedNode(const std::vector<uint8_t>& b, size_t band, size_t node) {
    const auto version = readAt<int32_t>(b, 0);
    const size_t o = morphOffset(b) + band * morphBandSize(version) + morphBandHeader(version)
                   + node * kMorphNodeSize;
    SavedNode s;
    s.type = static_cast<DistortionType>(std::clamp(static_cast<int>(readAt<int8_t>(b, o)), 0, 25));
    s.params.bias = readAt<float>(b, o + kNodeBiasOffset);
    s.params.folds = readAt<float>(b, o + kNodeFoldsOffset);
    s.params.bitDepth = readAt<float>(b, o + kNodeBitDepthOffset);
    std::array<float, MorphNode::kShapeSlotCount> slots{};
    for (size_t i = 0; i < slots.size(); ++i) slots[i] = readAt<float>(b, o + kNodeSlotsOffset + 4 * i);
    Processor::mapShapeSlotsToParams(s.type, slots.data(), s.params);
    return s;
}

const std::pair<const char*, float DistortionParams::*> kFloatFields[] = {
    {"bias", &DistortionParams::bias}, {"sag", &DistortionParams::sag}, {"curve", &DistortionParams::curve},
    {"knee", &DistortionParams::knee}, {"threshold", &DistortionParams::threshold},
    {"ceiling", &DistortionParams::ceiling}, {"speed", &DistortionParams::speed},
    {"hfRoll", &DistortionParams::hfRoll}, {"flutter", &DistortionParams::flutter},
    {"octave", &DistortionParams::octave}, {"sustain", &DistortionParams::sustain},
    {"asymmetry", &DistortionParams::asymmetry}, {"body", &DistortionParams::body},
    {"folds", &DistortionParams::folds}, {"shape", &DistortionParams::shape},
    {"symmetry", &DistortionParams::symmetry}, {"angle", &DistortionParams::angle},
    {"bitDepth", &DistortionParams::bitDepth}, {"sampleRateRatio", &DistortionParams::sampleRateRatio},
    {"smoothness", &DistortionParams::smoothness}, {"dither", &DistortionParams::dither},
    {"jitter", &DistortionParams::jitter}, {"quantLevels", &DistortionParams::quantLevels},
    {"quantOffset", &DistortionParams::quantOffset}, {"bitwiseIntensity", &DistortionParams::bitwiseIntensity},
    {"bitwisePattern", &DistortionParams::bitwisePattern},
    {"sensitivity", &DistortionParams::sensitivity}, {"attackMs", &DistortionParams::attackMs},
    {"releaseMs", &DistortionParams::releaseMs}, {"dynamicCurve", &DistortionParams::dynamicCurve},
    {"dynamicDepth", &DistortionParams::dynamicDepth}, {"hold", &DistortionParams::hold},
    {"feedback", &DistortionParams::feedback}, {"delayMs", &DistortionParams::delayMs},
    {"modDepth", &DistortionParams::modDepth}, {"rsCurve", &DistortionParams::rsCurve},
    {"rsCarrierFreq", &DistortionParams::rsCarrierFreq}, {"fbCurve", &DistortionParams::fbCurve},
    {"filterFreq", &DistortionParams::filterFreq}, {"limThreshold", &DistortionParams::limThreshold},
    {"fbDecay", &DistortionParams::fbDecay}, {"freqShift", &DistortionParams::freqShift},
    {"chaosAmount", &DistortionParams::chaosAmount}, {"attractorSpeed", &DistortionParams::attractorSpeed},
    {"chaosCoupling", &DistortionParams::chaosCoupling}, {"chaosXDrive", &DistortionParams::chaosXDrive},
    {"chaosYDrive", &DistortionParams::chaosYDrive}, {"formantShift", &DistortionParams::formantShift},
    {"formantCurve", &DistortionParams::formantCurve}, {"formantReso", &DistortionParams::formantReso},
    {"formantBW", &DistortionParams::formantBW}, {"formantGender", &DistortionParams::formantGender},
    {"formantBlend", &DistortionParams::formantBlend}, {"grainSizeMs", &DistortionParams::grainSizeMs},
    {"grainDensity", &DistortionParams::grainDensity}, {"grainPVar", &DistortionParams::grainPVar},
    {"grainDVar", &DistortionParams::grainDVar}, {"grainPos", &DistortionParams::grainPos},
    {"grainCurve", &DistortionParams::grainCurve}, {"spectralCurve", &DistortionParams::spectralCurve},
    {"spectralTilt", &DistortionParams::spectralTilt},
    {"spectralThreshold", &DistortionParams::spectralThreshold},
    {"spectralFreq", &DistortionParams::spectralFreq}, {"scaleFactor", &DistortionParams::scaleFactor},
    {"frequencyDecay", &DistortionParams::frequencyDecay}, {"fractalCurve", &DistortionParams::fractalCurve},
    {"fractalFB", &DistortionParams::fractalFB}, {"fractalDepth", &DistortionParams::fractalDepth},
    {"jitterAmount", &DistortionParams::jitterAmount}, {"jitterRate", &DistortionParams::jitterRate},
    {"coefficientNoise", &DistortionParams::coefficientNoise},
    {"stochasticDrift", &DistortionParams::stochasticDrift},
    {"stochasticSmooth", &DistortionParams::stochasticSmooth},
    {"resonantFreq", &DistortionParams::resonantFreq}, {"allpassFeedback", &DistortionParams::allpassFeedback},
    {"decayTimeS", &DistortionParams::decayTimeS}, {"allpassDrive", &DistortionParams::allpassDrive},
    {"allpassDamp", &DistortionParams::allpassDamp},
};
const std::pair<const char*, int DistortionParams::*> kIntFields[] = {
    {"satStage", &DistortionParams::satStage}, {"tapeModel", &DistortionParams::tapeModel},
    {"transistor", &DistortionParams::transistor}, {"foldModel", &DistortionParams::foldModel},
    {"bitcrushMode", &DistortionParams::bitcrushMode}, {"sampleMode", &DistortionParams::sampleMode},
    {"bitwiseOp", &DistortionParams::bitwiseOp}, {"dynamicMode", &DistortionParams::dynamicMode},
    {"stages", &DistortionParams::stages}, {"carrierType", &DistortionParams::carrierType},
    {"rsFreqSelect", &DistortionParams::rsFreqSelect}, {"filterType", &DistortionParams::filterType},
    {"chaosAttractor", &DistortionParams::chaosAttractor}, {"vowelSelect", &DistortionParams::vowelSelect},
    {"formantCount", &DistortionParams::formantCount}, {"grainEnvType", &DistortionParams::grainEnvType},
    {"spectralMode", &DistortionParams::spectralMode}, {"fftSize", &DistortionParams::fftSize},
    {"magnitudeBits", &DistortionParams::magnitudeBits}, {"spectralMagMode", &DistortionParams::spectralMagMode},
    {"spectralPhase", &DistortionParams::spectralPhase}, {"fractalMode", &DistortionParams::fractalMode},
    {"iterations", &DistortionParams::iterations}, {"fractalBlend", &DistortionParams::fractalBlend},
    {"stochasticCurve", &DistortionParams::stochasticCurve},
    {"stochasticCorr", &DistortionParams::stochasticCorr},
    {"stochasticNoiseColor", &DistortionParams::stochasticNoiseColor},
    {"allpassTopo", &DistortionParams::allpassTopo}, {"allpassSatType", &DistortionParams::allpassSatType},
};

struct ParamMismatch {
    const char* name;
    double want;
    double got;
};

// Every field of `got` that differs from `want` (floats beyond 1e-6).
std::vector<ParamMismatch> paramMismatches(const DistortionParams& want, const DistortionParams& got) {
    std::vector<ParamMismatch> out;
    const auto add = [&out](const char* name, double w, double g) {
        out.push_back(ParamMismatch{.name = name, .want = w, .got = g});
    };
    for (const auto& [name, m] : kFloatFields)
        if (std::fabs(want.*m - got.*m) > 1e-6f) add(name, want.*m, got.*m);
    for (const auto& [name, m] : kIntFields)
        if (want.*m != got.*m) add(name, want.*m, got.*m);
    if (want.dcBlock != got.dcBlock) add("dcBlock", want.dcBlock ? 1.0 : 0.0, got.dcBlock ? 1.0 : 0.0);
    if (want.limiter != got.limiter) add("limiter", want.limiter ? 1.0 : 0.0, got.limiter ? 1.0 : 0.0);
    if (want.xorPattern != got.xorPattern) add("xorPattern", want.xorPattern, got.xorPattern);
    return out;
}

std::vector<float> sine440(double sampleRate, double dbfs, size_t len) {
    constexpr double kTwoPi = 6.283185307179586476925;
    const double amp = std::pow(10.0, dbfs / 20.0);
    std::vector<float> x(len);
    for (size_t i = 0; i < len; ++i)
        x[i] = static_cast<float>(amp * std::sin(kTwoPi * 440.0 * static_cast<double>(i) / sampleRate));
    return x;
}

}  // namespace

TEST_CASE("Host setState restores HalfRectify threshold from shape slots (Rectified_Drums)",
          "[disrumpo][state][D-050][D-156]") {
    constexpr double kSampleRate = 44100.0;
    const auto preset =
        readPresetComponent(std::string(DISRUMPO_RESOURCES_DIR "/presets/") + "Drums/Rectified_Drums.vstpreset");
    const SavedNode want = savedNode(preset, 1, 0);
    REQUIRE(want.type == DistortionType::HalfRectify);

    Rig rig(kSampleRate);
    REQUIRE(setStateBytes(*rig.p, preset));
    const DistortionParams& got = rig.p->getBandProcessor(1).getMorphEngine()->getNodeParams(0);

    const auto len = static_cast<size_t>(kSampleRate);  // 1 s
    const auto in = sine440(kSampleRate, -12.0, len);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    const size_t half = len / 2;
    const double rmsL = toDb(rmsOf(outL.data() + half, len - half));
    const double rmsR = toDb(rmsOf(outR.data() + half, len - half));
    std::printf("D-156 Rectified_Drums (stream v%d): band 1 node 0 threshold restored %.6f (slots say %.6f), "
                "smoothness %.6f (%.6f); RMS last 0.5 s L %.2f dBFS R %.2f dBFS\n",
                readAt<int32_t>(preset, 0), static_cast<double>(got.threshold),
                static_cast<double>(want.params.threshold), static_cast<double>(got.smoothness),
                static_cast<double>(want.params.smoothness), rmsL, rmsR);

    CHECK(std::fabs(got.threshold - 0.3f) < 1e-6f);
    REQUIRE(rmsL > -30.0);
    REQUIRE(rmsR > -30.0);
}

TEST_CASE("setState A -> B -> A leaves no type-specific params of B behind",
          "[disrumpo][state][D-050]") {
    constexpr double kSampleRate = 44100.0;
    // A: one band, node 0 Tube, drive 5, shape slot 0 (bias) at 0.9; the other
    // slots stay 0.5 (sag 0.5, stage 2). B: node 0 Tape with sag (slot 1) 1.0.
    std::vector<uint8_t> a;
    std::vector<uint8_t> b;
    {
        Rig rig(kSampleRate);
        rig.apply({{kBandCountId, 0.0},
                   {makeNodeParamId(0, 0, NodeParamType::kNodeType), 2.0 / 25.0},  // Tube
                   {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.5},
                   {makeNodeParamId(0, 0, NodeParamType::kNodeShape0), 0.9}});
        a = getStateBytes(*rig.p);
        rig.apply({{makeNodeParamId(0, 0, NodeParamType::kNodeType), 3.0 / 25.0},  // Tape
                   {makeNodeParamId(0, 0, NodeParamType::kNodeShape1), 1.0}});
        b = getStateBytes(*rig.p);
    }
    const SavedNode wantA = savedNode(a, 0, 0);
    REQUIRE(wantA.type == DistortionType::Tube);
    REQUIRE(savedNode(b, 0, 0).type == DistortionType::Tape);

    // Each render starts from a re-activation. The instance's FIRST render differs from
    // every later render of the same patch in its first ~10 ms (start-up state that
    // setActive does not clear, follow-ups item 18; measured in fix plan T1.22: a second
    // render of A matches A2 bit for bit from sample 0, B leaves nothing behind). So A1 is
    // the second render of A, and the comparison covers every sample.
    const auto in = sine440(kSampleRate, -12.0, static_cast<size_t>(kSampleRate / 2));
    constexpr size_t kSkip = 0;
    Rig rig(kSampleRate);
    const auto render = [&](std::vector<float>& l, std::vector<float>& r) {
        rig.reactivate();
        rig.render(in, in, l, r);
    };
    std::vector<float> a1L;
    std::vector<float> a1R;
    std::vector<float> bL;
    std::vector<float> bR;
    std::vector<float> a2L;
    std::vector<float> a2R;
    REQUIRE(setStateBytes(*rig.p, a));
    render(a1L, a1R);  // the instance's first render (see above)
    render(a1L, a1R);
    const DistortionParams first = rig.p->getBandProcessor(0).getMorphEngine()->getNodeParams(0);
    // B through the host, then the user moves B's Sag knob (to the value B holds):
    // the live instance now runs B's Tape params.
    REQUIRE(setStateBytes(*rig.p, b));
    rig.apply({{makeNodeParamId(0, 0, NodeParamType::kNodeShape1), 1.0}});
    render(bL, bR);
    REQUIRE(setStateBytes(*rig.p, a));
    render(a2L, a2R);
    const DistortionParams& second = rig.p->getBandProcessor(0).getMorphEngine()->getNodeParams(0);

    double maxDiff = 0.0;
    for (size_t i = kSkip; i < in.size(); ++i) {
        maxDiff = std::max(maxDiff, std::fabs(static_cast<double>(a1L[i]) - static_cast<double>(a2L[i])));
        maxDiff = std::max(maxDiff, std::fabs(static_cast<double>(a1R[i]) - static_cast<double>(a2R[i])));
    }
    const auto firstVsSlots = paramMismatches(wantA.params, first);
    const auto secondVsSlots = paramMismatches(wantA.params, second);
    std::printf("D-050 A->B->A: max |A1 - A2| over all samples = %.3e (A1 RMS %.2f dBFS); node 0 sag %.3f then %.3f, "
                "satStage %d then %d (slots say %.3f, %d); fields off the slots: A1 %zu, A2 %zu\n",
                maxDiff, toDb(rmsOf(a1L.data() + kSkip, a1L.size() - kSkip)), static_cast<double>(first.sag),
                static_cast<double>(second.sag), first.satStage, second.satStage,
                static_cast<double>(wantA.params.sag), wantA.params.satStage, firstVsSlots.size(),
                secondVsSlots.size());
    for (const auto& m : secondVsSlots) std::printf("  A2 %s: slots %.6f restored %.6f\n", m.name, m.want, m.got);

    CHECK(firstVsSlots.empty());
    CHECK(secondVsSlots.empty());
    REQUIRE(maxDiff < 1e-5);
}

TEST_CASE("Every factory preset restores the slot-derived node params through setState",
          "[disrumpo][state][D-050][D-156]") {
    namespace fs = std::filesystem;
    std::vector<fs::path> presets;
    for (const auto& e : fs::recursive_directory_iterator(DISRUMPO_RESOURCES_DIR "/presets"))
        if (e.is_regular_file() && e.path().extension() == ".vstpreset") presets.push_back(e.path());
    std::sort(presets.begin(), presets.end());
    REQUIRE(presets.size() > size_t{100});

    // One live instance loads every preset in turn, as a host's preset menu does.
    // Streams before v9 carry no shape slots (none ship since T3.01 regenerated
    // Formant_Shift and removed Init_5_Bands): they are loaded but have nothing to
    // compare against.
    Rig rig(44100.0);
    int wrongPresets = 0;
    int wrongNodes = 0;
    int nodes = 0;
    int examples = 0;
    int legacy = 0;
    for (const auto& path : presets) {
        const auto comp = readPresetComponent(path.string());
        REQUIRE(setStateBytes(*rig.p, comp));
        rig.apply({});  // the live engines take a load made while active at the next block (D-003)
        const std::string name = fs::relative(path, DISRUMPO_RESOURCES_DIR "/presets").generic_string();
        const auto version = readAt<int32_t>(comp, 0);
        if (version < 9) {
            ++legacy;
            std::printf("  %s: stream v%d, no shape slots\n", name.c_str(), version);
            continue;
        }
        REQUIRE(version >= 10);  // the morph-section offsets assume the v10+ layout
        bool presetWrong = false;
        for (size_t band = 0; band < kBands; ++band) {
            const MorphEngine* engine = rig.p->getBandProcessor(static_cast<int>(band)).getMorphEngine();
            REQUIRE(engine != nullptr);
            for (size_t n = 0; n < static_cast<size_t>(kMaxMorphNodes); ++n) {
                ++nodes;
                const SavedNode want = savedNode(comp, band, n);
                REQUIRE(engine->getNodeType(static_cast<int>(n)) == want.type);
                const auto diffs = paramMismatches(want.params, engine->getNodeParams(static_cast<int>(n)));
                if (diffs.empty()) continue;
                ++wrongNodes;
                // One example line per preset (its first wrong field), at most 15.
                if (!presetWrong && wrongPresets < 15)
                    std::printf("  %s band %zu node %zu type %d %s: slots %.6f restored %.6f (%zu fields)\n",
                                name.c_str(), band, n, static_cast<int>(want.type), diffs[0].name, diffs[0].want,
                                diffs[0].got, diffs.size());
                presetWrong = true;
                examples += static_cast<int>(diffs.size());
            }
        }
        if (presetWrong) ++wrongPresets;
    }
    std::printf("D-050 factory presets: %zu presets (%d legacy without slots), %d with a wrong node; %d of %d "
                "nodes wrong (%d field mismatches)\n",
                presets.size(), legacy, wrongPresets, wrongNodes, nodes, examples);
    REQUIRE(nodes > 0);
    REQUIRE(wrongNodes == 0);
}

// D-118 (T5.01): the factory presets are v10 streams, written with 6 mod
// destinations per band (6 + band * 6 + param). Since v12 a band has 8
// (Tone, Bias added), so setState must move every band destination to
// 6 + band * 8 + param; a global or sweep destination (< 6) keeps its index.
// Read each preset's raw routing bytes, load it, save it, and compare.
TEST_CASE("v10 factory preset routing destinations migrate to the 8-per-band layout",
          "[disrumpo][preset][state][migration][D-118]") {
    namespace fs = std::filesystem;
    std::vector<fs::path> presets;
    for (const auto& e : fs::recursive_directory_iterator(DISRUMPO_RESOURCES_DIR "/presets"))
        if (e.is_regular_file() && e.path().extension() == ".vstpreset") presets.push_back(e.path());
    std::sort(presets.begin(), presets.end());
    REQUIRE(presets.size() > size_t{100});

    constexpr int32_t kOldParamsPerBand = 6;
    constexpr auto kBase = static_cast<int32_t>(ModDest::kBandBase);
    constexpr auto kPerBand = static_cast<int32_t>(ModDest::kParamsPerBand);
    constexpr size_t kRoutingSize = 10;  // source int8, dest int32, amount float, curve int8

    Rig rig(44100.0);
    int v10Presets = 0;
    int bandRoutings = 0;  // active routings to a band destination
    int moved = 0;         // of those, the ones whose index changes (band 1-3)
    int wrong = 0;
    for (const auto& path : presets) {
        const auto comp = readPresetComponent(path.string());
        const auto version = readAt<int32_t>(comp, 0);
        if (version < 10 || version > 11) continue;  // the 6-per-band layout with the v10+ offsets
        ++v10Presets;
        REQUIRE(setStateBytes(*rig.p, comp));
        rig.apply({});  // a load made while active lands at the next block (D-003)
        const auto saved = getStateBytes(*rig.p);
        REQUIRE(readAt<int32_t>(saved, 0) == kPresetVersion);

        const std::string name = fs::relative(path, DISRUMPO_RESOURCES_DIR "/presets").generic_string();
        const size_t raw0 = morphOffset(comp) - kRoutingsSize;
        const size_t saved0 = morphOffset(saved) - kRoutingsSize;
        for (size_t r = 0; r < Krate::DSP::kMaxModRoutings; ++r) {
            const size_t a = raw0 + r * kRoutingSize;
            const size_t b = saved0 + r * kRoutingSize;
            const auto source = readAt<int8_t>(comp, a);
            const auto rawDest = readAt<int32_t>(comp, a + 1);
            const auto amount = readAt<float>(comp, a + 5);
            // The raw read sits on the routing block: same source and amount after the load.
            REQUIRE(readAt<int8_t>(saved, b) == source);
            REQUIRE(readAt<float>(saved, b + 5) == amount);
            REQUIRE(rawDest >= 0);
            REQUIRE(rawDest < kBase + 4 * kOldParamsPerBand);
            const int32_t want = rawDest < kBase ? rawDest
                                                 : kBase + ((rawDest - kBase) / kOldParamsPerBand) * kPerBand +
                                                       (rawDest - kBase) % kOldParamsPerBand;
            const auto got = readAt<int32_t>(saved, b + 1);
            if (source != 0 && rawDest >= kBase) {
                ++bandRoutings;
                if (want != rawDest) ++moved;
            }
            if (got == want) continue;
            if (++wrong <= 10)
                std::printf("  %s routing %zu: stored dest %d, saved %d, want %d\n", name.c_str(), r, rawDest, got,
                            want);
        }
    }
    std::printf("D-118 routing migration: %d v10 presets, %d active band routings (%d on bands 1-3), %d of %d "
                "routings wrong\n",
                v10Presets, bandRoutings, moved, wrong, v10Presets * static_cast<int>(Krate::DSP::kMaxModRoutings));
    REQUIRE(v10Presets > 100);
    REQUIRE(moved > 0);
    REQUIRE(wrong == 0);
}

// -----------------------------------------------------------------------------
// D-122 (T5.02): every continuous, automatable parameter the host sees must
// show the value the processor runs. The registered Parameter's plain value
// (what the host prints and automates) is compared with the processor's own
// value after the change went through process(): a live getter where one is
// public (sweep, morph nodes), else the processor's saved state decoded with
// the inverse of the writer's encoding, else (input gain, output gain, mix,
// which the processor keeps normalized) a level measurement of its output.
// -----------------------------------------------------------------------------
namespace {

using StateRead = std::function<double(const Rig&, const std::vector<uint8_t>&)>;

std::string asciiOf(const Steinberg::Vst::TChar* s) {
    std::string out;
    for (int i = 0; i < 128 && s[i] != 0; ++i) out += s[i] < 128 ? static_cast<char>(s[i]) : '?';
    return out;
}

// Byte offset of `field` in a v13 stream: the mod sources start after the
// custom-curve breakpoints, whose count the stream holds.
size_t modSourcesOffset(const std::vector<uint8_t>& b) {
    const auto points = readAt<int32_t>(b, kCurveCountOffset);
    REQUIRE(points >= 0);
    return kCurveCountOffset + 4 + static_cast<size_t>(points) * 8;
}

StateRead f32At(size_t off, double scale = 1.0) {
    return [off, scale](const Rig&, const std::vector<uint8_t>& b) { return scale * readAt<float>(b, off); };
}

StateRead modF32(size_t rel, double scale = 1.0) {
    return [rel, scale](const Rig&, const std::vector<uint8_t>& b) {
        return scale * readAt<float>(b, modSourcesOffset(b) + rel);
    };
}

StateRead modRange(size_t rel, LinearRange r) {
    return [rel, r](const Rig&, const std::vector<uint8_t>& b) {
        return static_cast<double>(r.toPlain(readAt<float>(b, modSourcesOffset(b) + rel)));
    };
}

StateRead modLfoRate(size_t rel) {
    return [rel](const Rig&, const std::vector<uint8_t>& b) {
        return static_cast<double>(lfoRateNormToHz(readAt<float>(b, modSourcesOffset(b) + rel)));
    };
}

StateRead morphBandF32(size_t band, size_t rel) {
    return [band, rel](const Rig&, const std::vector<uint8_t>& b) {
        return static_cast<double>(readAt<float>(b, morphOffset(b) + band * morphBandSize(kPresetVersion) + rel));
    };
}

StateRead nodeSlot(size_t band, size_t node, size_t slot) {
    return [band, node, slot](const Rig&, const std::vector<uint8_t>& b) {
        const size_t o = morphOffset(b) + band * morphBandSize(kPresetVersion) + morphBandHeader(kPresetVersion) +
                         node * kMorphNodeSize + size_t{1 + 6 * 4} + slot * 4;  // type, 6 floats, slots
        return static_cast<double>(readAt<float>(b, o));
    };
}

const MorphEngine& liveEngine(const Rig& rig, size_t band) {
    const MorphEngine* e = rig.p->getBandProcessor(static_cast<int>(band)).getMorphEngine();
    REQUIRE(e != nullptr);
    return *e;
}

// Output / input level ratio in dB over the last 100 ms of a 250 ms 1 kHz sine render.
double renderGainDb(Rig& rig) {
    const auto n = static_cast<size_t>(0.25 * rig.sr);
    std::vector<float> in(n);
    for (size_t i = 0; i < n; ++i)
        in[i] = 0.25f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / rig.sr));
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR, 256);
    const auto tail = static_cast<size_t>(0.1 * rig.sr);
    const double ratio = rmsOf(outL.data() + (n - tail), tail) / rmsOf(in.data() + (n - tail), tail);
    return 20.0 * std::log10(std::max(ratio, 1e-9));
}

double outputRatio(Rig& rig) { return std::pow(10.0, renderGainDb(rig) / 20.0); }

}  // namespace

TEST_CASE("Registered plain value matches the processor's applied value", "[disrumpo][controller][state][D-122]") {
    using SP = SweepParamType;
    using MP = ModParamType;
    using BP = BandParamType;
    constexpr double kSampleRate = 48000.0;  // the 0.45 fs crossover cap (21.6 kHz) stays above 20 kHz

    // Read-back for each processor-backed parameter, in the registered unit.
    std::map<Steinberg::Vst::ParamID, std::pair<std::string, StateRead>> reads;
    std::map<Steinberg::Vst::ParamID, std::pair<std::string, StateRead>> slotReads;  // pushed in their own pass
    const auto add = [&reads](Steinberg::Vst::ParamID id, const char* how, StateRead r) {
        reads.emplace(id, std::make_pair(std::string(how), std::move(r)));
    };

    for (uint8_t c = 0; c + 1 < kMaxBands; ++c)
        add(makeCrossoverParamId(c), "state crossover Hz", f32At(kCrossoverOffset + 4 * size_t{c}));

    add(makeSweepParamId(SP::kSweepFrequency), "live SweepProcessor::getTargetFrequency",
        [](const Rig& r, const std::vector<uint8_t>&) {
            return static_cast<double>(r.p->getSweepProcessor().getTargetFrequency());
        });
    add(makeSweepParamId(SP::kSweepWidth), "live SweepProcessor::getWidth",
        [](const Rig& r, const std::vector<uint8_t>&) {
            return static_cast<double>(r.p->getSweepProcessor().getWidth());
        });
    add(makeSweepParamId(SP::kSweepIntensity), "live SweepProcessor::getIntensity x 100",
        [](const Rig& r, const std::vector<uint8_t>&) {
            return 100.0 * static_cast<double>(r.p->getSweepProcessor().getIntensity());
        });
    // Sweep LFO and envelope (state: core 15 bytes from 76, LFO 12, envelope 13)
    add(makeSweepParamId(SP::kSweepLFORate), "state sweep LFO rate",
        [](const Rig&, const std::vector<uint8_t>& b) {
            return static_cast<double>(lfoRateNormToHz(readAt<float>(b, kCrossoverOffset + 12 + 15 + 1)));
        });
    add(makeSweepParamId(SP::kSweepLFODepth), "state sweep LFO depth x 100", f32At(kCrossoverOffset + 12 + 15 + 6, 100.0));
    add(makeSweepParamId(SP::kSweepEnvAttack), "state sweep env attack ms",
        [](const Rig&, const std::vector<uint8_t>& b) {
            return static_cast<double>(kMinSweepEnvAttackMs +
                                       readAt<float>(b, kCrossoverOffset + 12 + 27 + 1) *
                                           (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs));
        });
    add(makeSweepParamId(SP::kSweepEnvRelease), "state sweep env release ms",
        [](const Rig&, const std::vector<uint8_t>& b) {
            return static_cast<double>(kMinSweepEnvReleaseMs +
                                       readAt<float>(b, kCrossoverOffset + 12 + 27 + 5) *
                                           (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs));
        });
    add(makeSweepParamId(SP::kSweepEnvSensitivity), "state sweep env sensitivity x 100",
        f32At(kCrossoverOffset + 12 + 27 + 9, 100.0));

    // Mod sources (13 + 13 LFOs, 13 envelope, 9 random, 9 chaos, 9 S&H, 16 pitch,
    // 12 transient, 10 Rungler), then 4 x 13 macros and 32 x 10 routings.
    add(makeModParamId(MP::kLFO1Rate), "state LFO 1 rate", modLfoRate(0));
    add(makeModParamId(MP::kLFO1Phase), "state LFO 1 phase", modRange(5, kLfoPhaseDegRange));
    add(makeModParamId(MP::kLFO2Rate), "state LFO 2 rate", modLfoRate(13));
    add(makeModParamId(MP::kLFO2Phase), "state LFO 2 phase", modRange(18, kLfoPhaseDegRange));
    add(makeModParamId(MP::kEnvFollowerAttack), "state env attack", modRange(26, kEnvFollowerAttackMsRange));
    add(makeModParamId(MP::kEnvFollowerRelease), "state env release", modRange(30, kEnvFollowerReleaseMsRange));
    add(makeModParamId(MP::kEnvFollowerSensitivity), "state env sensitivity x 100", modF32(34, 100.0));
    add(makeModParamId(MP::kRandomRate), "state random rate", modRange(39, kRandomRateHzRange));
    add(makeModParamId(MP::kRandomSmoothness), "state random smoothness x 100", modF32(43, 100.0));
    add(makeModParamId(MP::kChaosSpeed), "state chaos speed", modRange(49, kChaosSpeedRange));
    add(makeModParamId(MP::kChaosCoupling), "state chaos coupling", modF32(53));
    add(makeModParamId(MP::kSampleHoldRate), "state S&H rate", modRange(58, kSampleHoldRateHzRange));
    add(makeModParamId(MP::kSampleHoldSlew), "state S&H slew", modRange(62, kSampleHoldSlewMsRange));
    add(makeModParamId(MP::kPitchFollowerMinHz), "state pitch min", modRange(66, kPitchFollowerMinHzRange));
    add(makeModParamId(MP::kPitchFollowerMaxHz), "state pitch max", modRange(70, kPitchFollowerMaxHzRange));
    add(makeModParamId(MP::kPitchFollowerConfidence), "state pitch confidence", modF32(74));
    add(makeModParamId(MP::kPitchFollowerTrackingSpeed), "state pitch tracking",
        modRange(78, kPitchFollowerTrackingMsRange));
    add(makeModParamId(MP::kTransientSensitivity), "state transient sensitivity", modF32(82));
    add(makeModParamId(MP::kTransientAttack), "state transient attack", modRange(86, kTransientAttackMsRange));
    add(makeModParamId(MP::kTransientDecay), "state transient decay", modRange(90, kTransientDecayMsRange));
    add(makeModParamId(MP::kRunglerRate), "state Rungler rate", modRange(94, kRunglerRateHzRange));
    add(makeModParamId(MP::kRunglerDepth), "state Rungler depth", modF32(98));
    const std::array<std::array<MP, 3>, 4> macroIds{{{MP::kMacro1Value, MP::kMacro1Min, MP::kMacro1Max},
                                                     {MP::kMacro2Value, MP::kMacro2Min, MP::kMacro2Max},
                                                     {MP::kMacro3Value, MP::kMacro3Min, MP::kMacro3Max},
                                                     {MP::kMacro4Value, MP::kMacro4Min, MP::kMacro4Max}}};
    for (size_t m = 0; m < macroIds.size(); ++m)
        for (size_t f = 0; f < 3; ++f)
            add(makeModParamId(macroIds[m][f]), "state macro value/min/max", modF32(kModSourcesSize + m * kMacroSize + 4 * f));
    for (uint8_t r = 0; r < Krate::DSP::kMaxModRoutings; ++r)
        add(makeRoutingParamId(r, 2), "state routing amount x 100",
            modF32(kModSourcesSize + Krate::DSP::kMaxMacros * kMacroSize + size_t{r} * 10 + 5, 100.0));

    for (uint8_t band = 0; band < kMaxBands; ++band) {
        const size_t b = band;
        add(makeBandParamId(band, BP::kBandGain), "state band gain dB", f32At(20 + 11 * b));
        add(makeBandParamId(band, BP::kBandPan), "state band pan", f32At(24 + 11 * b));
        add(makeBandParamId(band, BP::kBandMorphX), "state morph X", morphBandF32(b, 0));
        add(makeBandParamId(band, BP::kBandMorphY), "state morph Y", morphBandF32(b, 4));
        add(makeBandParamId(band, BP::kBandMorphSmoothing), "state morph smoothing ms", morphBandF32(b, 10));
        for (uint8_t node = 0; node < kMaxMorphNodes; ++node) {
            const auto n = static_cast<int>(node);
            add(makeNodeParamId(band, node, NodeParamType::kNodeDrive), "live node drive",
                [b, n](const Rig& r, const std::vector<uint8_t>&) {
                    return static_cast<double>(liveEngine(r, b).getNodeCommonParams(n).drive);
                });
            add(makeNodeParamId(band, node, NodeParamType::kNodeMix), "live node mix x 100",
                [b, n](const Rig& r, const std::vector<uint8_t>&) {
                    return 100.0 * static_cast<double>(liveEngine(r, b).getNodeCommonParams(n).mix);
                });
            add(makeNodeParamId(band, node, NodeParamType::kNodeTone), "live node tone Hz",
                [b, n](const Rig& r, const std::vector<uint8_t>&) {
                    return static_cast<double>(liveEngine(r, b).getNodeCommonParams(n).toneHz);
                });
            add(makeNodeParamId(band, node, NodeParamType::kNodeBias), "live node bias",
                [b, n](const Rig& r, const std::vector<uint8_t>&) {
                    return static_cast<double>(liveEngine(r, b).getNodeParams(n).bias);
                });
            for (size_t s = 0; s < static_cast<size_t>(MorphNode::kShapeSlotCount); ++s) {
                const auto t = static_cast<NodeParamType>(static_cast<size_t>(NodeParamType::kNodeShape0) + s);
                slotReads.emplace(makeNodeParamId(band, node, t),
                                  std::make_pair(std::string("state shape slot"), nodeSlot(b, node, s)));
            }
        }
    }

    // Kept normalized by the processor; read back as output levels below.
    const std::array<Steinberg::Vst::ParamID, 3> measured{kInputGainId, kOutputGainId, kGlobalMixId};

    // Not applied live (follow-up 19): the processor takes the custom curve from setState only.
    std::set<Steinberg::Vst::ParamID> notApplied;
    for (int p = 0; p < 16; ++p)
        notApplied.insert(makeSweepParamId(static_cast<SP>(static_cast<int>(SP::kSweepCustomCurveP0X) + p)));

    // Plain values that are angles: the processor wraps 360 degrees to 0.
    const std::set<Steinberg::Vst::ParamID> degrees{makeModParamId(MP::kLFO1Phase), makeModParamId(MP::kLFO2Phase)};

    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);

    // Every continuous, automatable, host-visible parameter has a read-back.
    std::vector<Steinberg::Vst::ParamID> continuous;
    int unclassified = 0;
    for (int32 i = 0; i < c.getParameterCount(); ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(c.getParameterInfo(i, info) == Steinberg::kResultOk);
        constexpr auto kSkip = Steinberg::Vst::ParameterInfo::kIsReadOnly | Steinberg::Vst::ParameterInfo::kIsHidden;
        if (info.stepCount != 0 || (info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) == 0 ||
            (info.flags & kSkip) != 0)
            continue;
        continuous.push_back(info.id);
        const bool known = reads.contains(info.id) || slotReads.contains(info.id) || notApplied.contains(info.id) ||
                           std::find(measured.begin(), measured.end(), info.id) != measured.end();
        if (known) continue;
        ++unclassified;
        std::printf("  no read-back for parameter %u\n", static_cast<unsigned>(info.id));
    }
    REQUIRE(unclassified == 0);

    // |registered - processor| within 1 % of the value, plus 0.01 % of the range
    // (for values at or near 0: pan, bias, 0 dB).
    int checked = 0;
    int wrong = 0;
    const auto agrees = [&](Steinberg::Vst::ParamID id, double n, double got) {
        Steinberg::Vst::Parameter* param = c.getParameterObject(id);
        REQUIRE(param != nullptr);
        const double want = param->toPlain(n);
        const double span = std::fabs(param->toPlain(1.0) - param->toPlain(0.0));
        const double diff = degrees.contains(id) ? std::remainder(want - got, 360.0) : want - got;
        return std::fabs(diff) <= 0.01 * std::fabs(want) + 1e-4 * span;
    };
    const auto report = [&](Steinberg::Vst::ParamID id, const std::string& how, double n, double got) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(c.getParameterInfoByTag(id, info) == Steinberg::kResultTrue);
        std::printf("  %s (id %u, %s) at norm %.2f: registered plain %.5g, processor %.5g\n",
                    asciiOf(info.title).c_str(), static_cast<unsigned>(id), how.c_str(), n,
                    c.getParameterObject(id)->toPlain(n), got);
    };
    const auto compare = [&](Steinberg::Vst::ParamID id, const std::string& how, double n, double got) {
        ++checked;
        if (agrees(id, n, got)) return;
        if (++wrong <= 40) report(id, how, n, got);
    };

    for (const double n : {0.0, 0.25, 0.5, 1.0}) {
        // Pass 1: everything with a state or live read-back, one band (no crossover active).
        {
            Rig rig(kSampleRate);
            PList l{{kBandCountId, 0.0}};
            for (const auto& [id, r] : reads) l.emplace_back(id, n);
            rig.apply(l);
            const auto state = getStateBytes(*rig.p);
            REQUIRE(readAt<int32_t>(state, 0) == kPresetVersion);
            for (const auto& [id, r] : reads) compare(id, r.first, n, r.second(rig, state));
        }
        // Pass 2: shape slots (a slot can remap a node's bias, so not in pass 1).
        {
            Rig rig(kSampleRate);
            PList l;
            for (const auto& [id, r] : slotReads) l.emplace_back(id, n);
            rig.apply(l);
            const auto state = getStateBytes(*rig.p);
            for (const auto& [id, r] : slotReads) compare(id, r.first, n, r.second(rig, state));
        }
        // Pass 3: levels. Output gain on the dry path (mix 0); mix with every band
        // muted (out = dry * (1 - mix)); input gain with the band bypassed at mix 1.
        {
            Rig rig(kSampleRate);
            rig.apply({{kBandCountId, 0.0}, {kInputGainId, 0.5}, {kOutputGainId, n}, {kGlobalMixId, 0.0}});
            const double outDb = renderGainDb(rig);
            rig.apply({{kOutputGainId, 0.5}, {kGlobalMixId, n}, {makeBandParamId(0, BP::kBandMute), 1.0}});
            const double mixPct = 100.0 * (1.0 - outputRatio(rig));
            rig.apply({{kGlobalMixId, 1.0}, {makeBandParamId(0, BP::kBandMute), 0.0},
                       {makeBandParamId(0, BP::kBandBypass), 1.0}, {kInputGainId, n}});
            const double inDb = renderGainDb(rig);
            std::printf("D-122 norm %.2f measured: output gain %.3f dB, mix %.3f %%, input gain %.3f dB\n", n, outDb,
                        mixPct, inDb);
            compare(kOutputGainId, "measured output level dB", n, outDb);
            compare(kGlobalMixId, "measured dry level -> mix %", n, mixPct);
            compare(kInputGainId, "measured wet level dB", n, inDb);
        }
    }
    std::printf("D-122 registered vs processor: %zu continuous parameters (%zu by state/live read, %zu shape slots, "
                "3 measured, %zu custom-curve points not applied live), %d of %d comparisons wrong\n",
                continuous.size(), reads.size(), slotReads.size(), notApplied.size(), wrong, checked);
    REQUIRE(checked == 4 * static_cast<int>(reads.size() + slotReads.size() + measured.size()));
    CHECK(wrong == 0);
    c.terminate();
}

// Follow-up 75: the host's default for Chaos Speed is the speed a fresh processor runs.
TEST_CASE("Chaos Speed's registered default is what a fresh processor runs", "[disrumpo][controller][state][D-122]") {
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    const Steinberg::Vst::ParamID id = makeModParamId(ModParamType::kChaosSpeed);
    Steinberg::Vst::Parameter* param = c.getParameterObject(id);
    REQUIRE(param != nullptr);
    const double registered = param->toPlain(param->getInfo().defaultNormalizedValue);

    Rig rig(48000.0);
    const double fresh = modRange(49, kChaosSpeedRange)(rig, getStateBytes(*rig.p));
    std::printf("Chaos Speed default: registered plain %.5g, fresh processor %.5g\n", registered, fresh);
    CHECK(std::fabs(registered - fresh) <= 1e-4);
    c.terminate();
}

// -----------------------------------------------------------------------------
// D-003 (T1.24): setState on an active processor hands the state to the audio
// thread. The live DSP objects change at the top of the next process() call,
// never on the thread that called setState.
// -----------------------------------------------------------------------------
namespace {

std::string presetPath(const char* name) { return std::string(DISRUMPO_RESOURCES_DIR "/presets/") + name; }

// A zero-sample process() call: the host's parameter flush, the first thing a
// pending state is applied in.
void flushBlock(Rig& rig, PChanges& empty) {
    float z[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    rig.processBlock(z, z + 1, z + 2, z + 3, 0, &empty);
}

}  // namespace

TEST_CASE("setState during processing does not touch live DSP objects", "[disrumpo][rt-safety][D-003]") {
    // Multi_Band_Morph: 4 bands; the instance runs 2, so the load changes the band count.
    const auto preset = readPresetComponent(presetPath("Morph/Multi_Band_Morph.vstpreset"));
    const PatchView want = decodePatch(preset);
    REQUIRE(want.bandCount == 4);

    Rig rig(48000.0);
    rig.apply({{kBandCountId, 1.0 / 3.0}});  // 2 bands
    const auto nodes0 = MorphEngine::debugSetNodesCalls.load();
    const auto bands0 = CrossoverNetwork::debugSetBandCountCalls.load();
    REQUIRE(setStateBytes(*rig.p, preset));
    const auto nodesAfterSetState = MorphEngine::debugSetNodesCalls.load() - nodes0;
    const auto bandsAfterSetState = CrossoverNetwork::debugSetBandCountCalls.load() - bands0;

    // getState before any block returns the restored patch, not the 2-band one.
    const PatchView got = decodePatch(getStateBytes(*rig.p));

    PChanges empty;
    flushBlock(rig, empty);
    const auto nodesAfterBlock = MorphEngine::debugSetNodesCalls.load() - nodes0;
    const auto bandsAfterBlock = CrossoverNetwork::debugSetBandCountCalls.load() - bands0;
    std::printf("D-003 live DSP calls: right after setState setNodes=%llu setBandCount=%llu; after the next "
                "block setNodes=%llu setBandCount=%llu; getState before the block: bands=%d crossover0=%.1f Hz\n",
                static_cast<unsigned long long>(nodesAfterSetState),
                static_cast<unsigned long long>(bandsAfterSetState),
                static_cast<unsigned long long>(nodesAfterBlock), static_cast<unsigned long long>(bandsAfterBlock),
                got.bandCount, static_cast<double>(got.crossover[0]));

    CHECK(got.bandCount == want.bandCount);
    for (size_t c = 0; c + 1 < kBands; ++c) {
        INFO("crossover " << c);
        CHECK(std::fabs(got.crossover[c] - want.crossover[c]) < 0.1f);
    }
    for (size_t band = 0; band < kBands; ++band) {
        INFO("band " << band);
        CHECK(got.bands[band].mode == want.bands[band].mode);
        for (size_t n = 0; n < static_cast<size_t>(kMaxMorphNodes); ++n) CHECK(got.bands[band].types[n] == want.bands[band].types[n]);
    }
    CHECK(nodesAfterSetState == 0u);
    CHECK(bandsAfterSetState == 0u);
    REQUIRE(nodesAfterBlock > 0u);
    REQUIRE(bandsAfterBlock > 0u);
    REQUIRE(rig.p->getBandProcessor(0).getMorphEngine()->getNodeType(0) ==
            static_cast<DistortionType>(want.bands[0].types[0]));
}

TEST_CASE("Two setState calls before a block: the last one wins", "[disrumpo][rt-safety][D-003]") {
    const auto a = readPresetComponent(presetPath("Morph/Radial_Blend.vstpreset"));
    std::vector<uint8_t> b;  // a current-version stream: every section present
    {
        Rig src(48000.0);
        src.apply(linkPatch());
        src.apply({{kBandCountId, 1.0 / 3.0}, {makeCrossoverParamId(0), 0.3}});
        b = getStateBytes(*src.p);
    }
    REQUIRE(readAt<int32_t>(b, 0) == kPresetVersion);

    // The trailing 8 bytes are the SharedDisplayBridge instance ID: two live
    // processors restoring b never share it (D-056), so it is left out here.
    const auto withoutInstanceId = [](std::vector<uint8_t> s) {
        REQUIRE(s.size() >= size_t{8});
        s.resize(s.size() - 8);
        return s;
    };

    PChanges empty;
    Rig twice(48000.0);
    REQUIRE(setStateBytes(*twice.p, a));
    REQUIRE(setStateBytes(*twice.p, b));
    const auto twiceBefore = withoutInstanceId(getStateBytes(*twice.p));
    flushBlock(twice, empty);
    const auto twiceAfter = withoutInstanceId(getStateBytes(*twice.p));

    Rig once(48000.0);
    REQUIRE(setStateBytes(*once.p, b));
    const auto onceBefore = withoutInstanceId(getStateBytes(*once.p));
    flushBlock(once, empty);
    const auto onceAfter = withoutInstanceId(getStateBytes(*once.p));

    std::printf("D-003 last wins: A then B vs B alone, first differing byte before the block %zu of %zu, "
                "after it %zu of %zu; B alone before vs after the block %zu of %zu\n",
                firstDifference(twiceBefore, onceBefore), onceBefore.size(), firstDifference(twiceAfter, onceAfter),
                onceAfter.size(), firstDifference(onceBefore, onceAfter), onceAfter.size());
    REQUIRE(twiceBefore.size() == onceBefore.size());
    REQUIRE(firstDifference(twiceBefore, onceBefore) == onceBefore.size());
    REQUIRE(twiceAfter.size() == onceAfter.size());
    REQUIRE(firstDifference(twiceAfter, onceAfter) == onceAfter.size());
    // The applied state saves back exactly what the pending one did.
    REQUIRE(firstDifference(onceBefore, onceAfter) == onceAfter.size());
}

TEST_CASE("A deferred load renders like a load made while inactive; applying it allocates nothing",
          "[disrumpo][rt-safety][D-003]") {
    SECTION("render: deferred (active) vs immediate (inactive) load") {
        constexpr double kSampleRate = 48000.0;
        constexpr size_t kBlock = 512;
        const auto preset = readPresetComponent(presetPath("Morph/Multi_Band_Morph.vstpreset"));
        const auto in = sine440(kSampleRate, -12.0, static_cast<size_t>(kSampleRate / 2));
        // Max |diff| over the first block and over every sample after it.
        const auto compare = [&](const std::vector<float>& aL, const std::vector<float>& aR,
                                 const std::vector<float>& bL, const std::vector<float>& bR) {
            std::pair<double, double> m{0.0, 0.0};
            for (size_t i = 0; i < in.size(); ++i) {
                const double d = std::max(std::fabs(static_cast<double>(aL[i]) - static_cast<double>(bL[i])),
                                          std::fabs(static_cast<double>(aR[i]) - static_cast<double>(bR[i])));
                double& slot = (i < kBlock) ? m.first : m.second;
                slot = std::max(slot, d);
            }
            return m;
        };
        // Both instances run the same lifecycle (setup, load, setupProcessing +
        // setActive, render); only the thread that applies the load differs.
        // Inactive: setState applies it at once.
        std::vector<float> iL;
        std::vector<float> iR;
        {
            Rig inactive(kSampleRate);
            inactive.deactivate();
            REQUIRE(setStateBytes(*inactive.p, preset));
            inactive.setup();
            inactive.render(in, in, iL, iR, static_cast<int32>(kBlock));
        }
        // Active: a process() call applies it.
        std::vector<float> dL;
        std::vector<float> dR;
        {
            Rig deferred(kSampleRate);
            PChanges empty;
            REQUIRE(setStateBytes(*deferred.p, preset));
            flushBlock(deferred, empty);
            deferred.deactivate();
            deferred.setup();
            deferred.render(in, in, dL, dR, static_cast<int32>(kBlock));
        }
        // Straight after a load while active, without re-preparation, the instance
        // glides from its previous (default) patch: its smoothers start where they
        // were. Recorded, not gated.
        std::vector<float> gL;
        std::vector<float> gR;
        {
            Rig deferred(kSampleRate);
            REQUIRE(setStateBytes(*deferred.p, preset));
            deferred.render(in, in, gL, gR, static_cast<int32>(kBlock));
        }
        const auto applied = compare(dL, dR, iL, iR);
        const auto glide = compare(gL, gR, iL, iR);
        std::printf("D-003 deferred vs inactive load (Multi_Band_Morph, 0.5 s, RMS %.2f dBFS): max |diff| first "
                    "block %.3e, after it %.3e; straight after the load while active (glide from the previous "
                    "patch): first block %.3e, after it %.3e\n",
                    toDb(rmsOf(iL.data(), iL.size())), applied.first, applied.second, glide.first, glide.second);
        REQUIRE(applied.second < 1e-6);
    }

    SECTION("allocations in the block that applies each factory preset") {
        namespace fs = std::filesystem;
        std::vector<fs::path> presets;
        for (const auto& e : fs::recursive_directory_iterator(DISRUMPO_RESOURCES_DIR "/presets"))
            if (e.is_regular_file() && e.path().extension() == ".vstpreset") presets.push_back(e.path());
        std::sort(presets.begin(), presets.end());
        REQUIRE(presets.size() > size_t{100});

        {
            // The detector counts in this binary (non-vacuity).
            TestHelpers::ThreadScopedAllocationScope probe;
            std::vector<int> v(16);
            v[0] = 1;
            REQUIRE(TestHelpers::AllocationDetector::instance().getAllocationCount() > 0u);
        }

        Rig rig(44100.0);
        PChanges empty;
        size_t allocations = 0;
        size_t appliedPresets = 0;
        for (const auto& path : presets) {
            const auto comp = readPresetComponent(path.string());
            REQUIRE(setStateBytes(*rig.p, comp));
            const auto nodes0 = MorphEngine::debugSetNodesCalls.load();
            {
                TestHelpers::ThreadScopedAllocationScope scope;
                flushBlock(rig, empty);
                allocations += TestHelpers::AllocationDetector::instance().getAllocationCount();
            }
            if (MorphEngine::debugSetNodesCalls.load() - nodes0 >= static_cast<uint64_t>(kMaxBands)) ++appliedPresets;
        }
        std::printf("D-003 applying %zu factory presets in process(): %zu applied there, %zu allocations\n",
                    presets.size(), appliedPresets, allocations);
        CHECK(appliedPresets == presets.size());
        REQUIRE(allocations == 0u);
    }
}

// Measurement probe (hidden): cost of the process() call that applies a pending state.
TEST_CASE("Probe: cost of the block that applies a pending state", "[.][t124-cpu-probe]") {
    const auto a = readPresetComponent(presetPath("Morph/Multi_Band_Morph.vstpreset"));
    const auto b = readPresetComponent(presetPath("Morph/Radial_Blend.vstpreset"));
    Rig rig(48000.0);
    PChanges empty;
    constexpr int kRuns = 200;
    double total = 0.0;
    double worst = 0.0;
    for (int i = 0; i < kRuns; ++i) {
        REQUIRE(setStateBytes(*rig.p, (i % 2 == 0) ? a : b));
        const auto t0 = std::chrono::steady_clock::now();
        flushBlock(rig, empty);
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        total += us;
        worst = std::max(worst, us);
    }
    WARN("PROBE apply block: mean " << (total / kRuns) << " us, max " << worst << " us over " << kRuns
                                    << " loads (512-sample budget at 48 kHz: 10667 us)");
}

// -----------------------------------------------------------------------------
// D-026 / D-081 / D-098 / D-108 and follow-up 5 (T2.10): latency and tail.
// The per-band oversampler (4 samples at 2x, 7 at 4x), Spectral (its FFT) and the
// rest delayed some bands and not others or the dry path, and getLatencySamples()
// said 0. The processor now reports the worst case its discrete settings allow and
// delays every band and the dry path to it; the reported value never moves with
// morph, sweep or automation. getTailSamples() covers the ringing types.
// -----------------------------------------------------------------------------
namespace {

constexpr double kLatSr = 48000.0;
constexpr size_t kLatAt = 9600;             // impulse position (0.2 s of settling first)
constexpr size_t kLatLen = kLatAt + 12288;  // room for a 4096-sample latency and the response
constexpr float kLatImpulse = 0.25f;

double latTypeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

double osMaxNorm(int factor) {
    if (factor <= 1) return 0.0;
    return factor == 2 ? 1.0 / 3.0 : 2.0 / 3.0;
}

/// Spectral shape slot 1: 512 * 2^round(slot * 3)
double fftSlotNorm(int fftSize) {
    if (fftSize <= 512) return 0.0;
    if (fftSize == 1024) return 1.0 / 3.0;
    return fftSize == 2048 ? 2.0 / 3.0 : 1.0;
}

/// Node `node` of `band` as `type`, drive `driveNorm` (x 10) and node mix `mixNorm`.
void setNode(PList& l, uint8_t band, uint8_t node, DistortionType type, double driveNorm, double mixNorm) {
    l.emplace_back(makeNodeParamId(band, node, NodeParamType::kNodeType), latTypeNorm(type));
    l.emplace_back(makeNodeParamId(band, node, NodeParamType::kNodeDrive), driveNorm);
    l.emplace_back(makeNodeParamId(band, node, NodeParamType::kNodeMix), mixNorm);
}

/// `numBands` bands at morph (0, 0) (node 0 alone), Oversample Max `osMax`.
PList latBase(int numBands, int osMax) {
    PList l{{kBandCountId, static_cast<double>(numBands - 1) / 3.0}, {kOversampleMaxId, osMaxNorm(osMax)}};
    for (uint8_t b = 0; b < static_cast<uint8_t>(numBands); ++b) {
        l.emplace_back(makeBandParamId(b, BandParamType::kBandMorphX), 0.0);
        l.emplace_back(makeBandParamId(b, BandParamType::kBandMorphY), 0.0);
    }
    return l;
}

/// Left channel of a kLatImpulse impulse at kLatAt (the whole render).
std::vector<float> latImpulseRender(Rig& rig) {
    std::vector<float> in(kLatLen, 0.0f);
    in[kLatAt] = kLatImpulse;
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    return outL;
}

/// Offset of the largest |sample| from the impulse position.
long latPeakOffset(const std::vector<float>& y) {
    size_t p = 0;
    for (size_t i = 1; i < y.size(); ++i) {
        if (std::fabs(y[i]) > std::fabs(y[p])) p = i;
    }
    return static_cast<long>(p) - static_cast<long>(kLatAt);
}

/// Impulse-peak offset of a rig configured with `l` (applied, then reactivated).
struct LatReading {
    long measured = 0;
    long reported = 0;
};
LatReading latMeasure(const PList& l) {
    Rig rig(kLatSr);
    rig.apply(l);
    rig.reactivate();
    const auto y = latImpulseRender(rig);
    return {.measured = latPeakOffset(y), .reported = static_cast<long>(rig.p->getLatencySamples())};
}

/// Lag (0..maxLag) at which `out` best matches `in` (largest |cross-correlation|).
long bestLag(const std::vector<float>& in, const std::vector<float>& out, size_t from, long maxLag) {
    long best = 0;
    double bestV = -1.0;
    for (long lag = 0; lag <= maxLag; ++lag) {
        double acc = 0.0;
        for (size_t n = from; n + static_cast<size_t>(lag) < out.size(); ++n) {
            acc += static_cast<double>(in[n]) * static_cast<double>(out[n + static_cast<size_t>(lag)]);
        }
        if (std::fabs(acc) > bestV) {
            bestV = std::fabs(acc);
            best = lag;
        }
    }
    return best;
}

}  // namespace

TEST_CASE("getLatencySamples equals the measured impulse delay at every Oversample Max",
          "[disrumpo][latency][D-026][D-098]") {
    for (const auto type : {DistortionType::SoftClip, DistortionType::HardClip}) {
        for (const int osMax : {1, 2, 4}) {
            // Drive 10 keeps the drive gate open; node mix 0 makes the node output its
            // (pre-shaper) input, so only the oversampler's filters shape the impulse.
            auto l = latBase(1, osMax);
            setNode(l, 0, 0, type, 1.0, 0.0);
            const auto r = latMeasure(l);
            std::printf("T2.10 latency %s Oversample Max %dx: impulse peak offset %ld, getLatencySamples %ld\n",
                        type == DistortionType::SoftClip ? "SoftClip" : "HardClip", osMax, r.measured, r.reported);
            INFO("type " << static_cast<int>(type) << ", Oversample Max " << osMax << "x");
            CHECK(std::labs(r.measured - r.reported) <= 1);
        }
    }
}

TEST_CASE("getLatencySamples equals the measured impulse delay with a Spectral node",
          "[disrumpo][latency][D-026][D-081][D-098]") {
    for (const int fft : {512, 2048, 4096}) {
        auto l = latBase(1, 4);
        setNode(l, 0, 0, DistortionType::Spectral, 0.1, 1.0);
        l.emplace_back(makeNodeParamId(0, 0, NodeParamType::kNodeShape1), fftSlotNorm(fft));
        const auto r = latMeasure(l);
        std::printf("T2.10 latency Spectral fftSize %d: impulse peak offset %ld, getLatencySamples %ld\n", fft,
                    r.measured, r.reported);
        INFO("fftSize " << fft);
        CHECK(std::labs(r.measured - r.reported) <= 1);
    }
}

TEST_CASE("A fresh processor reports its default patch's latency once set up", "[disrumpo][latency][D-026]") {
    // Before setupProcessing the oversamplers are not prepared, so there is nothing to
    // measure yet; the default patch (SoftClip nodes, Oversample Max 4x) reaches 2x.
    Rig unprepared(kLatSr, 512, false);
    const auto before = unprepared.p->getLatencySamples();
    Rig rig(kLatSr);
    const auto after = rig.p->getLatencySamples();
    const auto oversampler2x = rig.p->getBandProcessor(0).getOversamplerLatency(2);
    std::printf("T2.10 default patch: getLatencySamples before setupProcessing %u, after %u (2x oversampler %d)\n",
                static_cast<unsigned>(before), static_cast<unsigned>(after), oversampler2x);
    CHECK(before == 0u);
    CHECK(std::cmp_equal(after, oversampler2x));
    CHECK(after > 0u);
}

TEST_CASE("getLatencySamples equals the measured delay with a Granular node", "[disrumpo][latency][D-026]") {
    // Grain position 0 and pitch (position) variance 0: every grain reads the newest input.
    // Position and its random jitter are a lookback into the grain buffer, an effect delay
    // like FeedbackDist's delay time, not a latency.
    auto l = latBase(1, 4);
    setNode(l, 0, 0, DistortionType::Granular, 0.1, 1.0);
    l.emplace_back(makeNodeParamId(0, 0, NodeParamType::kNodeShape2), 0.0);
    l.emplace_back(makeNodeParamId(0, 0, NodeParamType::kNodeShape4), 0.0);
    Rig rig(kLatSr);
    rig.apply(l);
    rig.reactivate();
    const auto y = latImpulseRender(rig);
    const long peak = latPeakOffset(y);
    // White noise: the lag of the best input/output match
    std::vector<float> noise(static_cast<size_t>(kLatSr));
    uint32_t s = 0x12345u;
    for (auto& v : noise) {
        s = s * 1664525u + 1013904223u;
        v = 0.25f * (static_cast<float>(s >> 8) / 8388608.0f - 1.0f);
    }
    Rig rig2(kLatSr);
    rig2.apply(l);
    rig2.reactivate();
    std::vector<float> outL;
    std::vector<float> outR;
    rig2.render(noise, noise, outL, outR);
    const long lag = bestLag(noise, outL, 4800, 3000);
    const auto reported = static_cast<long>(rig.p->getLatencySamples());
    std::printf("T2.10 latency Granular: impulse peak offset %ld, noise best lag %ld, getLatencySamples %ld\n",
                peak, lag, reported);
    CHECK(std::labs(lag - reported) <= 1);
    CHECK(std::labs(peak - reported) <= 1);
}

TEST_CASE("A Spectral node at mix 50 % gives one impulse image, not two", "[disrumpo][latency][D-081]") {
    auto l = latBase(1, 4);
    setNode(l, 0, 0, DistortionType::Spectral, 0.1, 0.5);
    l.emplace_back(makeNodeParamId(0, 0, NodeParamType::kNodeShape1), fftSlotNorm(2048));
    Rig rig(kLatSr);
    rig.apply(l);
    rig.reactivate();
    const auto y = latImpulseRender(rig);
    const auto reported = static_cast<size_t>(rig.p->getLatencySamples());
    double atZero = 0.0;
    double atLatency = 0.0;
    for (size_t i = kLatAt - 2; i <= kLatAt + 2; ++i) atZero = std::max(atZero, std::fabs(static_cast<double>(y[i])));
    for (size_t i = kLatAt + reported - 2; i <= kLatAt + reported + 2 && i < y.size(); ++i) {
        atLatency = std::max(atLatency, std::fabs(static_cast<double>(y[i])));
    }
    std::printf("T2.10 Spectral node mix 50 %%: |y| at the input instant %.6f, at the reported latency %zu: %.6f\n",
                atZero, reported, atLatency);
    CHECK(reported > 0u);
    CHECK(atZero < 1e-4);
    CHECK(atLatency > 0.05);
}

namespace {

/// Impulse-peak offset of band `band` alone (soloed) under `l`.
long soloPeakOffset(PList l, uint8_t band) {
    l.emplace_back(makeBandParamId(band, BandParamType::kBandSolo), 1.0);
    Rig rig(kLatSr);
    rig.apply(l);
    rig.reactivate();
    return latPeakOffset(latImpulseRender(rig));
}

/// Each band's arrival delay under `cfg` against `ref` (same bands, nothing latent), and
/// the latency `cfg` reports.
struct Arrival {
    std::array<long, 2> delay{};
    long reported = 0;
};
Arrival bandArrival(const PList& cfg, const PList& ref) {
    Arrival a;
    for (uint8_t b = 0; b < 2; ++b) a.delay[b] = soloPeakOffset(cfg, b) - soloPeakOffset(ref, b);
    a.reported = latMeasure(cfg).reported;
    return a;
}

}  // namespace

TEST_CASE("Bands at different oversampling factors arrive on the same sample", "[disrumpo][latency][D-026]") {
    // Band 0 SoftClip at drive 0 (a passthrough, clocked at 1x); band 1 HardClip at drive 10
    // and node mix 0 (4x, only the oversampler's filters). Reference: Oversample Max 1x.
    const auto nodes = [](PList l) {
        setNode(l, 0, 0, DistortionType::SoftClip, 0.0, 1.0);
        setNode(l, 1, 0, DistortionType::HardClip, 1.0, 0.0);
        return l;
    };
    const auto a = bandArrival(nodes(latBase(2, 4)), nodes(latBase(2, 1)));
    std::printf("T2.10 arrival 1x band + 4x band: band 0 delayed %ld, band 1 delayed %ld (inter-band offset %ld), "
                "getLatencySamples %ld\n", a.delay[0], a.delay[1], a.delay[1] - a.delay[0], a.reported);
    CHECK(a.delay[1] - a.delay[0] == 0);
    CHECK(std::labs(a.delay[1] - a.reported) <= 1);
}

TEST_CASE("Band with Spectral is time-aligned with a SoftClip band", "[disrumpo][latency][D-026][D-098]") {
    // Band 0 SoftClip at drive 0 (passthrough); band 1 Spectral fftSize 2048 at mix 1.
    // Reference: band 1 SoftClip at drive 0, Oversample Max 1x.
    auto cfg = latBase(2, 4);
    setNode(cfg, 0, 0, DistortionType::SoftClip, 0.0, 1.0);
    setNode(cfg, 1, 0, DistortionType::Spectral, 0.1, 1.0);
    cfg.emplace_back(makeNodeParamId(1, 0, NodeParamType::kNodeShape1), fftSlotNorm(2048));
    auto ref = latBase(2, 1);
    setNode(ref, 0, 0, DistortionType::SoftClip, 0.0, 1.0);
    setNode(ref, 1, 0, DistortionType::SoftClip, 0.0, 1.0);
    const auto a = bandArrival(cfg, ref);
    std::printf("T2.10 arrival SoftClip band + Spectral band: band 0 delayed %ld, band 1 delayed %ld "
                "(inter-band offset %ld), getLatencySamples %ld\n",
                a.delay[0], a.delay[1], a.delay[1] - a.delay[0], a.reported);
    CHECK(std::labs(a.delay[1] - a.delay[0]) <= 1);
    CHECK(std::labs(a.delay[0] - a.reported) <= 1);
}

TEST_CASE("Global mix: dry and wet impulse peaks coincide with an oversampled band",
          "[disrumpo][latency][mix][D-026]") {
    std::array<long, 2> peak{};
    long reported = 0;
    for (const int wet : {0, 1}) {
        auto l = latBase(1, 4);
        setNode(l, 0, 0, DistortionType::HardClip, 1.0, 0.0);
        l.emplace_back(kGlobalMixId, static_cast<double>(wet));
        const auto r = latMeasure(l);
        peak[static_cast<size_t>(wet)] = r.measured;
        reported = r.reported;
    }
    std::printf("T2.10 global mix: dry impulse peak %ld, wet impulse peak %ld (HardClip at 4x), "
                "getLatencySamples %ld\n", peak[0], peak[1], reported);
    CHECK(peak[0] == peak[1]);
}

TEST_CASE("Reported latency holds through a morph sweep; Oversample Max and node type changes report it once",
          "[disrumpo][latency][D-026]") {
    auto l = latBase(1, 4);
    l.emplace_back(makeBandParamId(0, BandParamType::kBandActiveNodes), 1.0 / 3.0);  // 2 nodes
    setNode(l, 0, 0, DistortionType::SoftClip, 0.1, 1.0);  // recommends 2x
    setNode(l, 0, 1, DistortionType::HardClip, 0.1, 1.0);  // recommends 4x
    Rig rig(kLatSr);
    rig.apply(l);
    rig.reactivate();

    // Morph X over 2 s: max(0, |sin(2 pi t)| - 0.1) / 0.9, so it rests on node 0 (2x) for
    // ~32 ms around each zero of the sine and moves toward node 1 (4x) in between: four
    // crossings of the 2x / 4x boundary
    constexpr size_t kBlock = 512;
    constexpr auto kSweepEnd = static_cast<size_t>(2.0 * kLatSr);
    constexpr size_t kOsChangeAt = kSweepEnd + 24000;               // Oversample Max 4x -> 2x
    constexpr size_t kTypeChangeAt = kOsChangeAt + 24000;           // node 1 HardClip -> Spectral
    constexpr size_t kTotal = kTypeChangeAt + 24000;
    std::vector<float> in(kTotal);
    for (size_t i = 0; i < kTotal; ++i) {
        in[i] = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / kLatSr));
    }
    // Phase 2 starts with the block that carries the Oversample Max change, phase 3 with
    // the block that carries the type change
    const auto phaseOf = [&](size_t start) {
        if (start == 0) return 0;
        if (start + kBlock <= kOsChangeAt) return 1;
        return start + kBlock <= kTypeChangeAt ? 2 : 3;
    };
    const auto hits = [](size_t start, size_t at) { return start <= at && at < start + kBlock; };
    std::array<int, 4> signals{};
    std::array<long, 4> reportedMin{LONG_MAX, LONG_MAX, LONG_MAX, LONG_MAX};
    std::array<long, 4> reportedMax{-1, -1, -1, -1};
    bool saw2x = false;
    bool saw4x = false;
    int lastFactor = 0;
    int factorChanges = 0;
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(
        in, in, outL, outR, static_cast<Steinberg::int32>(kBlock),
        [&](size_t /*blk*/, size_t start, PChanges& pc) {
            if (start < kSweepEnd) {
                const double t = static_cast<double>(start) / kLatSr;
                const double s = std::fabs(std::sin(2.0 * std::numbers::pi * t));
                pc.addChange(makeBandParamId(0, BandParamType::kBandMorphX), std::max(0.0, s - 0.1) / 0.9);
            }
            if (hits(start, kOsChangeAt)) pc.addChange(kOversampleMaxId, osMaxNorm(2));
            if (hits(start, kTypeChangeAt)) {
                pc.addChange(makeNodeParamId(0, 1, NodeParamType::kNodeType), latTypeNorm(DistortionType::Spectral));
            }
        },
        [&](size_t blk, CaptureChanges& out) {
            const int ph = phaseOf(blk * kBlock);
            double v = 0.0;
            if (out.last(kLatencyOutputId, v)) ++signals[static_cast<size_t>(ph)];
            const auto rep = static_cast<long>(rig.p->getLatencySamples());
            reportedMin[static_cast<size_t>(ph)] = std::min(reportedMin[static_cast<size_t>(ph)], rep);
            reportedMax[static_cast<size_t>(ph)] = std::max(reportedMax[static_cast<size_t>(ph)], rep);
            if (ph == 1) {
                const int f = rig.p->getBandProcessor(0).getActiveOversampleFactor();
                saw2x = saw2x || f == 2;
                saw4x = saw4x || f == 4;
                if (lastFactor != 0 && f != lastFactor) ++factorChanges;
                lastFactor = f;
            }
        });
    std::printf("T2.10 latency signals: first block %d; 2 s morph sweep %d (reported %ld..%ld, factors seen 2x %s "
                "4x %s, %d factor changes at block ends); Oversample Max 4x->2x %d (reported %ld..%ld); node type "
                "-> Spectral %d (reported %ld..%ld)\n",
                signals[0], signals[1], reportedMin[1], reportedMax[1], saw2x ? "yes" : "no", saw4x ? "yes" : "no",
                factorChanges,
                signals[2], reportedMin[2], reportedMax[2], signals[3], reportedMin[3], reportedMax[3]);
    const size_t sweepClicks = clickCount(outL.data() + kBlock, kSweepEnd - kBlock, kLatSr);
    std::printf("T2.10 morph sweep across the 2x / 4x boundary: %zu clicks (ClickDetector)\n", sweepClicks);
    CHECK(sweepClicks == 0u);
    CHECK(saw2x);
    CHECK(saw4x);
    CHECK(signals[1] == 0);
    CHECK(reportedMin[1] == reportedMax[1]);
    CHECK(signals[2] == 1);
    CHECK(reportedMin[2] != reportedMax[1]);
    CHECK(signals[3] == 1);
    CHECK(reportedMax[3] != reportedMax[2]);
}

TEST_CASE("A factor switch with its compensation-delay change does not click", "[disrumpo][latency][D-026]") {
    // SoftClip (2x) and HardClip (4x) nodes, both near-linear at -12 dBFS; morph X jumps
    // 0 -> 1 at 1 s, so the band switches 2x -> 4x and its compensation delay changes.
    auto l = latBase(1, 4);
    l.emplace_back(makeBandParamId(0, BandParamType::kBandActiveNodes), 1.0 / 3.0);
    setNode(l, 0, 0, DistortionType::SoftClip, 0.1, 1.0);
    setNode(l, 0, 1, DistortionType::HardClip, 0.1, 1.0);
    Rig rig(kLatSr);
    rig.apply(l);
    rig.reactivate();
    const auto kAt = static_cast<size_t>(kLatSr);
    const size_t kLen = 2 * kAt;
    std::vector<float> in(kLen);
    for (size_t i = 0; i < kLen; ++i) {
        in[i] = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / kLatSr));
    }
    int factorBefore = 0;
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR, 512, [&](size_t /*blk*/, size_t start, PChanges& pc) {
        if (start <= kAt && kAt < start + 512) {
            factorBefore = rig.p->getBandProcessor(0).getActiveOversampleFactor();
            pc.addChange(makeBandParamId(0, BandParamType::kBandMorphX), 1.0);
        }
    });
    const int factorAfter = rig.p->getBandProcessor(0).getActiveOversampleFactor();
    const auto c = continuityAround(outL, kAt - 512, kAt + 1440, kAt / 2, kAt - 1024, kAt + 9600, kLen - 512, kLatSr);
    std::printf("T2.10 factor switch %dx -> %dx with compensation: max step ratio %.3f vs steady, clicks %zu "
                "(steady window %zu), non-finite %zu, getLatencySamples %d\n",
                factorBefore, factorAfter, c.ratio, c.clicksEvent, c.clicksBase, c.nonFinite,
                static_cast<int>(rig.p->getLatencySamples()));
    REQUIRE(factorBefore == 2);
    REQUIRE(factorAfter == 4);
    CHECK(c.clicksEvent == 0u);
    CHECK(c.ratio <= 3.0);
    CHECK(c.nonFinite == 0u);
}

TEST_CASE("getTailSamples reports a tail while a ringing type is active", "[disrumpo][latency][tail][D-108]") {
    const auto tailOf = [](DistortionType type) {
        auto l = latBase(1, 4);
        setNode(l, 0, 0, type, 0.1, 1.0);
        Rig rig(kLatSr);
        rig.apply(l);
        return rig.p->getTailSamples();
    };
    const auto none = tailOf(DistortionType::SoftClip);
    std::printf("T2.10 getTailSamples SoftClip %u\n", static_cast<unsigned>(none));
    CHECK(none == Steinberg::Vst::kNoTail);
    for (const auto type : {DistortionType::FeedbackDist, DistortionType::AllpassResonant, DistortionType::Granular,
                            DistortionType::Spectral}) {
        const auto tail = tailOf(type);
        std::printf("T2.10 getTailSamples type %d: %u\n", static_cast<int>(type), static_cast<unsigned>(tail));
        INFO("type " << static_cast<int>(type));
        CHECK(tail == Steinberg::Vst::kInfiniteTail);
    }
}

TEST_CASE("A latency change allocates nothing in process()", "[disrumpo][latency][rt-safety][D-026]") {
    auto l = latBase(2, 4);
    setNode(l, 0, 0, DistortionType::SoftClip, 0.1, 1.0);
    setNode(l, 1, 0, DistortionType::HardClip, 0.1, 1.0);
    Rig rig(kLatSr);
    rig.apply(l);
    rig.reactivate();

    constexpr Steinberg::int32 kBlock = 512;
    std::vector<float> iL(kBlock, 0.1f);
    std::vector<float> iR(kBlock, 0.1f);
    std::vector<float> oL(kBlock, 0.0f);
    std::vector<float> oR(kBlock, 0.0f);
    PChanges empty;
    PChanges toOs2;
    toOs2.addChange(kOversampleMaxId, osMaxNorm(2));
    PChanges toSpectral;
    toSpectral.addChange(makeNodeParamId(1, 0, NodeParamType::kNodeType), latTypeNorm(DistortionType::Spectral));
    // Output queues created and reserved up front, never cleared: addPoint() stays within capacity
    CaptureChanges out;
    Steinberg::int32 idx = 0;
    for (const auto id : {kSweepModulatedFrequencyOutputId, kLatencyOutputId}) {
        auto* q = dynamic_cast<CaptureQueue*>(out.addParameterData(id, idx));
        REQUIRE(q != nullptr);
        q->pts_.reserve(4096);
    }
    rig.processBlock(iL.data(), iR.data(), oL.data(), oR.data(), kBlock, &empty, &out);
    const auto before = static_cast<long>(rig.p->getLatencySamples());

    size_t allocations = 0;
    {
        TestHelpers::ThreadScopedAllocationScope scope;
        for (int blk = 0; blk < 60; ++blk) {
            PChanges* in = &empty;
            if (blk == 10) in = &toOs2;
            if (blk == 30) in = &toSpectral;
            rig.processBlock(iL.data(), iR.data(), oL.data(), oR.data(), kBlock, in, &out);
        }
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    const auto after = static_cast<long>(rig.p->getLatencySamples());
    double last = 0.0;
    const bool signalled = out.last(kLatencyOutputId, last);
    std::printf("T2.10 latency change in process(): reported %ld -> %ld, last signalled %.0f samples, %zu allocations\n",
                before, after, signalled ? last * kMaxReportedLatencySamples : -1.0, allocations);
    CHECK(after != before);
    CHECK(signalled);
    REQUIRE(allocations == 0u);
}

namespace {

/// Host component handler stub: counts restartComponent(kLatencyChanged).
class LatencyRestartHandler final : public Steinberg::Vst::IComponentHandler {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid)) {
            *obj = this;
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID /*id*/,
                                              Steinberg::Vst::ParamValue /*value*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override {
        if ((flags & Steinberg::Vst::kLatencyChanged) != 0) ++latencyRestarts;
        return Steinberg::kResultOk;
    }
    int latencyRestarts = 0;
};

}  // namespace

TEST_CASE("The controller turns a latency report into restartComponent(kLatencyChanged)",
          "[disrumpo][latency][D-026]") {
    LatencyRestartHandler handler;
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    c.setComponentHandler(&handler);
    auto* param = c.getParameterObject(kLatencyOutputId);
    std::printf("T2.10 controller latency output parameter registered: %s\n", param ? "yes" : "no");
    REQUIRE(param != nullptr);
    const auto& info = param->getInfo();
    CHECK((info.flags & Steinberg::Vst::ParameterInfo::kIsReadOnly) != 0);
    CHECK((info.flags & Steinberg::Vst::ParameterInfo::kIsHidden) != 0);
    CHECK((info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) == 0);

    const double seven = 7.0 / kMaxReportedLatencySamples;
    c.setParamNormalized(kLatencyOutputId, seven);
    const int afterFirst = handler.latencyRestarts;
    c.setParamNormalized(kLatencyOutputId, seven);  // unchanged: no restart
    const int afterSame = handler.latencyRestarts;
    c.setParamNormalized(kLatencyOutputId, 4.0 / kMaxReportedLatencySamples);
    const int afterChange = handler.latencyRestarts;
    c.setParamNormalized(kGlobalMixId, 0.5);  // any other parameter: no restart
    std::printf("T2.10 controller restartComponent(kLatencyChanged): first report %d, same value %d, new value %d, "
                "other parameter %d\n", afterFirst, afterSame, afterChange, handler.latencyRestarts);
    CHECK(afterFirst == 1);
    CHECK(afterSame == 1);
    CHECK(afterChange == 2);
    CHECK(handler.latencyRestarts == 2);
    c.terminate();
}

// D-013 / D-162 (fix plan T2.17): one non-finite input sample latched the
// EnvelopeFollower state (and the DC blockers) for good. With the sweep envelope
// on, the sweep centre collapsed to 20 Hz for the rest of the session. This TU is
// IEEE (-fno-fast-math), and the bad samples are built from bit patterns through
// a volatile, so the injection survives every toolchain.
TEST_CASE("Processor output stays finite after one NaN sample with env->mix routing and sweep env on",
          "[disrumpo][numerics][D-013][D-162]") {
    constexpr double kSampleRate = 48000.0;
    constexpr Steinberg::int32 kBlock = 512;
    constexpr size_t kInjectBlock = 20;
    const auto in = sine440(kSampleRate, -12.0, static_cast<size_t>(kSampleRate * 2.0));
    const auto blocksPerSecond = static_cast<size_t>(kSampleRate) / static_cast<size_t>(kBlock);
    const auto norm = [](double v, double count) { return v / (count - 1.0); };

    double controlLastSecondDb = 0.0;
    // A control run without injection (bits 0), then NaN, then +Inf.
    const std::array<std::pair<std::uint32_t, const char*>, 3> runs{
        {{0u, "control"}, {0x7FC00000u, "NaN"}, {0x7F800000u, "+Inf"}}};
    for (const auto& [badBits, name] : runs) {
        volatile std::uint32_t bitsSink = badBits;
        const float bad = std::bit_cast<float>(static_cast<std::uint32_t>(bitsSink));

        Rig rig(kSampleRate, kBlock);
        rig.apply({{kBandCountId, 0.0},  // 1 band: the crossover passes the input straight through
                   {makeSweepParamId(SweepParamType::kSweepEnable), 1.0},
                   {makeSweepParamId(SweepParamType::kSweepEnvEnable), 1.0},
                   {makeSweepParamId(SweepParamType::kSweepEnvSensitivity), 1.0},
                   {makeRoutingParamId(0, 0), norm(static_cast<double>(Krate::DSP::ModSource::EnvFollower),
                                                   static_cast<double>(kUIModSourceCount))},
                   {makeRoutingParamId(0, 1), norm(static_cast<double>(ModDest::kGlobalMix),
                                                   static_cast<double>(ModDest::kTotalDestinations))},
                   {makeRoutingParamId(0, 2), 0.25}});  // amount -0.5

        size_t injectBlockNonFinite = 0;
        size_t laterBlocksNonFinite = 0;
        size_t recoveredAfterBlocks = SIZE_MAX;
        double lastSweepHz = 0.0;
        std::vector<float> bL(kBlock);
        std::vector<float> bR(kBlock);
        std::vector<float> oL(kBlock);
        std::vector<float> oR(kBlock);
        std::vector<float> outL(in.size(), 0.0f);
        CaptureChanges cap;
        size_t blk = 0;
        for (size_t s = 0; s + static_cast<size_t>(kBlock) <= in.size(); s += kBlock, ++blk) {
            std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(s), kBlock, bL.begin());
            std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(s), kBlock, bR.begin());
            if (blk == kInjectBlock && badBits != 0u) {
                bL[0] = bad;
                bR[0] = bad;
            }
            cap.clear();
            rig.processBlock(bL.data(), bR.data(), oL.data(), oR.data(), kBlock, nullptr, &cap);
            std::copy_n(oL.begin(), kBlock, outL.begin() + static_cast<std::ptrdiff_t>(s));
            size_t nonFinite = 0;
            for (Steinberg::int32 i = 0; i < kBlock; ++i)
                if (!fin(oL[static_cast<size_t>(i)]) || !fin(oR[static_cast<size_t>(i)])) ++nonFinite;
            double sweepNorm = 0.0;
            const bool haveSweep = cap.last(kSweepModulatedFrequencyOutputId, sweepNorm);
            const double sweepHz = 20.0 * std::pow(1000.0, sweepNorm);
            if (blk == kInjectBlock) injectBlockNonFinite = nonFinite;
            if (blk > kInjectBlock) {
                if (nonFinite > 0) ++laterBlocksNonFinite;
                lastSweepHz = sweepHz;
                if (recoveredAfterBlocks == SIZE_MAX && haveSweep && fin(static_cast<float>(sweepNorm)) &&
                    sweepHz > 20.5)
                    recoveredAfterBlocks = blk - kInjectBlock;
            }
        }
        const double recoveredMs = recoveredAfterBlocks == SIZE_MAX
                                       ? -1.0
                                       : 1000.0 * static_cast<double>(recoveredAfterBlocks * kBlock) / kSampleRate;
        // Level of the last second (the env -> mix routing is live there).
        const size_t lastSecond = static_cast<size_t>(kSampleRate);
        const double lastSecondDb = toDb(rmsOf(outL.data() + outL.size() - lastSecond, lastSecond));
        std::printf("T2.17 one %s sample: non-finite samples in the inject block %zu, later blocks with non-finite "
                    "output %zu of %zu, sweep centre back above 20 Hz after %.1f ms (-1 = never), last sweep %.1f Hz, "
                    "last-second level %.2f dBFS\n",
                    name, injectBlockNonFinite, laterBlocksNonFinite, blk - kInjectBlock - 1, recoveredMs,
                    lastSweepHz, lastSecondDb);
        INFO(name);
        CHECK(laterBlocksNonFinite == size_t{0});
        REQUIRE(recoveredAfterBlocks <= blocksPerSecond);
        if (badBits == 0u) {
            controlLastSecondDb = lastSecondDb;
        } else {
            CHECK(std::abs(lastSecondDb - controlLastSecondDb) < 1.0);
        }
    }
}

// D-006 (T4.19): every node parameter in a ParameterChanges batch pushed all 4
// nodes of its band to the BandProcessor (MorphEngine::setNodes on each engine,
// all 4 adapters re-routed), so a preset load through the controller (272 node
// IDs) did that 272 times in one process() call. A batch now pushes each band
// it touched once, after the queue loop.
TEST_CASE("A batch of node parameter changes pushes each band's nodes once", "[disrumpo][rt-safety][D-006]") {
    Rig rig(48000.0);

    // The engines one push reaches (setNodes per BandProcessor::setMorphNodes).
    auto before = MorphEngine::debugSetNodesCalls.load();
    rig.apply({{makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.3}});
    const auto perPush = MorphEngine::debugSetNodesCalls.load() - before;
    REQUIRE(perPush > 0u);

    // All 17 node parameters of band 0, node 1.
    const auto nodeParams = [](uint8_t band) {
        PList l;
        for (const NodeParamType t : {NodeParamType::kNodeType, NodeParamType::kNodeDrive, NodeParamType::kNodeMix,
                                      NodeParamType::kNodeTone, NodeParamType::kNodeBias, NodeParamType::kNodeFolds,
                                      NodeParamType::kNodeBitDepth}) {
            l.emplace_back(makeNodeParamId(band, 1, t), 0.4);
        }
        for (int s = 0; s < 10; ++s) {
            l.emplace_back(makeNodeParamId(band, 1,
                                           static_cast<NodeParamType>(static_cast<int>(NodeParamType::kNodeShape0) + s)),
                           0.25);
        }
        return l;
    };
    const PList band0 = nodeParams(0);
    REQUIRE(band0.size() == size_t{17});

    before = MorphEngine::debugSetNodesCalls.load();
    rig.apply(band0);
    const auto oneBand = MorphEngine::debugSetNodesCalls.load() - before;

    PList twoBands = band0;
    const PList band1 = nodeParams(1);
    twoBands.insert(twoBands.end(), band1.begin(), band1.end());
    before = MorphEngine::debugSetNodesCalls.load();
    rig.apply(twoBands);
    const auto bothBands = MorphEngine::debugSetNodesCalls.load() - before;

    std::printf("D-006 setNodes calls: one node param %llu, 17 node params of one band %llu, 34 of two bands %llu\n",
                static_cast<unsigned long long>(perPush), static_cast<unsigned long long>(oneBand),
                static_cast<unsigned long long>(bothBands));
    CHECK(oneBand == perPush);
    CHECK(bothBands == 2 * perPush);
}

// -----------------------------------------------------------------------------
// D-129 (T5.08): host-facing edge cases at processor level. This TU is IEEE
// (-fno-fast-math -fno-finite-math-only -ffp-contract=off); non-finite samples are
// built from bit patterns through a volatile and classified with detail::isFinite.
// -----------------------------------------------------------------------------

// One band (the crossover passes the input straight to the node), node 0 alone,
// Oversample Max 4x: one NaN, then (a fresh instance) one +Inf sample on both
// channels between finite sine samples. Every output from 1024 samples after the
// bad sample has left the processor (its reported latency) on must be finite.
TEST_CASE("Every distortion type recovers from one NaN and one +Inf input sample", "[disrumpo][numerics][D-129]") {
    constexpr double kSr = 48000.0;
    constexpr int32 kBlock = 256;
    constexpr size_t kWarm = 1024;
    constexpr size_t kSettle = 1024;
    constexpr size_t kChecked = 4096;
    const std::array<std::pair<std::uint32_t, const char*>, 2> bads{{{0x7FC00000u, "NaN"}, {0x7F800000u, "+Inf"}}};

    int runs = 0;
    int latchedRuns = 0;
    int reachedOutput = 0;
    for (int t = 0; t < static_cast<int>(DistortionType::COUNT); ++t) {
        const auto type = static_cast<DistortionType>(t);
        for (const auto& [bits, name] : bads) {
            volatile std::uint32_t bitsSink = bits;
            const float bad = std::bit_cast<float>(static_cast<std::uint32_t>(bitsSink));
            REQUIRE_FALSE(fin(bad));

            Rig rig(kSr, kBlock);
            auto l = latBase(1, 4);
            setNode(l, 0, 0, type, 0.3, 1.0);
            rig.apply(l);
            const auto latency = static_cast<size_t>(rig.p->getLatencySamples());
            const size_t checkFrom = kWarm + latency + kSettle;
            auto x = sine440(kSr, -12.0, checkFrom + kChecked);
            x[kWarm] = bad;
            std::vector<float> outL;
            std::vector<float> outR;
            rig.render(x, x, outL, outR, kBlock);

            size_t nonFinite = 0;
            size_t lastNonFinite = 0;
            for (size_t i = checkFrom; i < outL.size(); ++i) {
                if (fin(outL[i]) && fin(outR[i])) continue;
                ++nonFinite;
                lastNonFinite = i;
            }
            ++runs;
            for (size_t i = kWarm; i < checkFrom; ++i) {
                if (fin(outL[i]) && fin(outR[i])) continue;
                ++reachedOutput;  // the bad sample got through: the window below is not vacuous
                break;
            }
            if (nonFinite == 0) continue;
            ++latchedRuns;
            std::printf("D-129 %s type %d: %zu non-finite samples from %zu samples after the bad one (latency %zu), "
                        "the last %zu samples after it\n",
                        name, t, nonFinite, checkFrom - kWarm, latency, lastNonFinite - kWarm);
        }
    }
    std::printf("D-129 non-finite input: %d of %d type/sample runs put a non-finite sample out inside the settle "
                "window, %d still non-finite after it\n",
                reachedOutput, runs, latchedRuns);
    REQUIRE(runs == 2 * static_cast<int>(DistortionType::COUNT));
    CHECK(latchedRuns == 0);
}

TEST_CASE("Processor rejects every non-stereo bus arrangement", "[disrumpo][lifecycle][D-129]") {
    using Steinberg::Vst::SpeakerArrangement;
    namespace Arr = Steinberg::Vst::SpeakerArr;
    auto p = std::make_unique<Processor>();
    REQUIRE(p->initialize(nullptr) == Steinberg::kResultOk);
    SpeakerArrangement mono = Arr::kMono;
    SpeakerArrangement stereo = Arr::kStereo;
    SpeakerArrangement surround = Arr::k51;
    CHECK(p->setBusArrangements(&mono, 1, &mono, 1) == Steinberg::kResultFalse);
    CHECK(p->setBusArrangements(&mono, 1, &stereo, 1) == Steinberg::kResultFalse);
    CHECK(p->setBusArrangements(&stereo, 1, &mono, 1) == Steinberg::kResultFalse);
    CHECK(p->setBusArrangements(&surround, 1, &surround, 1) == Steinberg::kResultFalse);
    CHECK(p->setBusArrangements(&stereo, 1, &stereo, 1) == Steinberg::kResultTrue);
    SpeakerArrangement in = 0;
    SpeakerArrangement out = 0;
    REQUIRE(p->getBusArrangement(Steinberg::Vst::kInput, 0, in) == Steinberg::kResultTrue);
    REQUIRE(p->getBusArrangement(Steinberg::Vst::kOutput, 0, out) == Steinberg::kResultTrue);
    CHECK(in == Arr::kStereo);
    CHECK(out == Arr::kStereo);
    p->terminate();
}

// numSamples 0 is the host's parameter flush; a mono bus is a layout the processor
// rejects (above) but a host may still send. Neither may read or write a sample.
TEST_CASE("process() with no samples or a mono bus leaves every buffer untouched", "[disrumpo][lifecycle][D-129]") {
    constexpr int32 kBlock = 512;
    constexpr float kInSentinel = 0.25f;
    constexpr float kOutSentinel = -0.75f;
    Rig rig(48000.0, kBlock);
    std::vector<float> iL(kBlock, kInSentinel);
    std::vector<float> iR(kBlock, kInSentinel);
    std::vector<float> oL(kBlock, kOutSentinel);
    std::vector<float> oR(kBlock, kOutSentinel);
    float* ic[2] = {iL.data(), iR.data()};
    float* oc[2] = {oL.data(), oR.data()};
    Steinberg::Vst::AudioBusBuffers ib{};
    Steinberg::Vst::AudioBusBuffers ob{};
    ib.channelBuffers32 = ic;
    ob.channelBuffers32 = oc;
    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numInputs = 1;
    d.numOutputs = 1;
    d.inputs = &ib;
    d.outputs = &ob;

    const char* what = "";
    SECTION("numSamples 0, stereo") {
        what = "numSamples 0";
        ib.numChannels = 2;
        ob.numChannels = 2;
        d.numSamples = 0;
    }
    SECTION("numSamples 512, mono in and out") {
        what = "mono bus";
        ib.numChannels = 1;
        ob.numChannels = 1;
        d.numSamples = kBlock;
    }
    REQUIRE(rig.p->process(d) == Steinberg::kResultTrue);

    size_t changed = 0;
    for (size_t i = 0; i < static_cast<size_t>(kBlock); ++i) {
        if (iL[i] != kInSentinel || iR[i] != kInSentinel) ++changed;
        if (oL[i] != kOutSentinel || oR[i] != kOutSentinel) ++changed;
    }
    std::printf("D-129 process() with %s: %zu buffer samples changed\n", what, changed);
    CHECK(changed == 0u);
}

// D-003 follow-up (T5.08): the host's thread loads states while the audio thread
// runs. 200 loads alternating two factory presets with different band counts and
// types; every block is rendered while they land, and every sample must be finite.
TEST_CASE("setState from a second thread during processing keeps the output finite",
          "[disrumpo][rt-safety][D-003][D-129][long]") {
    constexpr int32 kBlock = 256;
    constexpr int kLoads = 200;
    const auto a = readPresetComponent(presetPath("Morph/Multi_Band_Morph.vstpreset"));
    const auto b = readPresetComponent(presetPath("Morph/Radial_Blend.vstpreset"));
    REQUIRE(decodePatch(a).bandCount != decodePatch(b).bandCount);

    Rig rig(48000.0, kBlock);
    const auto in = sine440(48000.0, -12.0, static_cast<size_t>(kBlock) * 64);
    std::atomic<bool> stop{false};
    std::atomic<size_t> blocks{0};
    size_t nonFinite = 0;  // audio thread only, read after join
    std::thread audio([&] {
        std::vector<float> bL(kBlock);
        std::vector<float> bR(kBlock);
        std::vector<float> oL(kBlock);
        std::vector<float> oR(kBlock);
        size_t pos = 0;
        while (!stop.load(std::memory_order_acquire)) {
            std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(pos), kBlock, bL.begin());
            std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(pos), kBlock, bR.begin());
            pos = (pos + static_cast<size_t>(kBlock)) % in.size();
            rig.processBlock(bL.data(), bR.data(), oL.data(), oR.data(), kBlock, nullptr);
            for (int32 i = 0; i < kBlock; ++i)
                if (!fin(oL[static_cast<size_t>(i)]) || !fin(oR[static_cast<size_t>(i)])) ++nonFinite;
            blocks.fetch_add(1, std::memory_order_release);
        }
    });
    // Each load waits for one more rendered block, so every load lands while the
    // audio thread is running and is applied before the next one.
    const auto waitForBlock = [&blocks] {
        const size_t seen = blocks.load(std::memory_order_acquire);
        while (blocks.load(std::memory_order_acquire) == seen) std::this_thread::yield();
    };
    int failedLoads = 0;
    for (int i = 0; i < kLoads; ++i) {
        waitForBlock();
        if (!setStateBytes(*rig.p, (i % 2 == 0) ? a : b)) ++failedLoads;
    }
    waitForBlock();
    waitForBlock();  // the last load applied and rendered
    stop.store(true, std::memory_order_release);
    audio.join();
    std::printf("D-003 concurrent setState: %d loads (%d failed), %zu blocks rendered, %zu non-finite samples\n",
                kLoads, failedLoads, blocks.load(), nonFinite);
    CHECK(failedLoads == 0);
    CHECK(blocks.load() > static_cast<size_t>(kLoads));
    CHECK(nonFinite == 0u);
}
