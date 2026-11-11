// ==============================================================================
// Level-meter windows: accumulation, merge, non-finite input (IEEE TU)
// ==============================================================================
// MeterAccumulator / mergeMeterWindow (meter_window.h) and the processor's meters
// read through the Tier-3 SharedMeters. This TU injects NaN and Inf, so it is
// built without fast-math (tests/CMakeLists.txt).
// ==============================================================================

#include "meter_window.h"
#include "plugin_ids.h"
#include "shared_display.h"

#include "display/shared_display_bridge.h"
#include "../test_support/disrumpo_rig.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {

namespace ts = Disrumpo::TestSupport;

double linDb(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }

bool windowFinite(const Disrumpo::MeterWindow& w) {
    using Krate::DSP::detail::isFinite;
    bool ok = true;
    for (int c = 0; c < 2; ++c)
        ok = ok && isFinite(w.inPeak[c]) && isFinite(w.inMeanSq[c]) && isFinite(w.outPeak[c]) &&
             isFinite(w.outMeanSq[c]);
    for (const float b : w.bandPeak) ok = ok && isFinite(b);
    return ok;
}

}  // namespace

TEST_CASE("MeterAccumulator reports a non-finite value as 0 and Inf as a clip", "[disrumpo][meters]") {
    Disrumpo::MeterAccumulator acc;
    acc.inPeak[0] = std::numeric_limits<float>::quiet_NaN();
    acc.inSumSq[0] = std::numeric_limits<float>::quiet_NaN();
    acc.outPeak[1] = std::numeric_limits<float>::infinity();
    acc.outSumSq[1] = std::numeric_limits<float>::infinity();
    acc.outPreClipPeak[1] = std::numeric_limits<float>::infinity();  // the output clip bit reads it
    acc.bandPeak[2] = 0.5f;
    acc.inSumSq[1] = 0.25f * 100.0f;
    acc.numSamples = 100;
    const Disrumpo::MeterWindow w = acc.take();
    std::printf("Meters non-finite: in L peak %g meanSq %g, out R peak %g meanSq %g, clip 0x%x, in R meanSq %g\n",
                static_cast<double>(w.inPeak[0]), static_cast<double>(w.inMeanSq[0]),
                static_cast<double>(w.outPeak[1]), static_cast<double>(w.outMeanSq[1]), w.clipFlags,
                static_cast<double>(w.inMeanSq[1]));
    CHECK(windowFinite(w));
    CHECK(w.inPeak[0] == 0.0f);
    CHECK(w.inMeanSq[0] == 0.0f);
    CHECK(w.outPeak[1] == 0.0f);
    CHECK(w.clipFlags == Disrumpo::kMeterClipOutR);  // NaN is no clip, Inf is
    CHECK(w.inMeanSq[1] == 0.25f);
    CHECK(w.bandPeak[2] == 0.5f);
    CHECK(w.numSamples == 100u);

    // take() starts the next window
    const Disrumpo::MeterWindow next = acc.take();
    CHECK(next.numSamples == 0u);
    CHECK(next.clipFlags == 0u);
    CHECK(next.bandPeak[2] == 0.0f);
}

TEST_CASE("The output clip bit reads the output before the safety clipper", "[disrumpo][meters]") {
    // User ruling 2026-11-10: the light shows when the clipper catches an over, so the bit
    // follows outPreClipPeak; the delivered output peak (after the clipper) never sets it
    Disrumpo::MeterAccumulator acc;
    acc.outPeak[0] = 0.999f;
    acc.outPreClipPeak[0] = 1.5f;
    acc.outPeak[1] = 1.2f;  // not reachable through the clipper; the bit ignores it
    acc.outPreClipPeak[1] = 0.9f;
    acc.numSamples = 10;
    const Disrumpo::MeterWindow w = acc.take();
    CHECK(w.clipFlags == Disrumpo::kMeterClipOutL);
    CHECK(w.outPeak[0] == 0.999f);
}

TEST_CASE("mergeMeterWindow keeps the peaks, weights the mean squares and joins the clips", "[disrumpo][meters]") {
    Disrumpo::MeterWindow a;
    a.inPeak[0] = 0.5f;
    a.inMeanSq[0] = 0.1f;
    a.bandPeak[3] = 0.2f;
    a.clipFlags = Disrumpo::kMeterClipInL;
    a.numSamples = 100;
    Disrumpo::MeterWindow b;
    b.inPeak[0] = 0.25f;
    b.inMeanSq[0] = 0.4f;
    b.bandPeak[3] = 0.7f;
    b.clipFlags = Disrumpo::kMeterClipBand0 << 3;
    b.numSamples = 300;
    Disrumpo::mergeMeterWindow(a, b);
    CHECK(a.inPeak[0] == 0.5f);
    CHECK(std::fabs(a.inMeanSq[0] - 0.325f) < 1e-6f);  // (0.1 * 100 + 0.4 * 300) / 400
    CHECK(a.bandPeak[3] == 0.7f);
    CHECK(a.clipFlags == (Disrumpo::kMeterClipInL | (Disrumpo::kMeterClipBand0 << 3)));
    CHECK(a.numSamples == 400u);
}

TEST_CASE("SharedMeters hands each window to the reader once", "[disrumpo][meters][tier3]") {
    Disrumpo::SharedMeters shared;
    Disrumpo::MeterWindow out;
    CHECK_FALSE(shared.consume(out));  // nothing published yet
    Disrumpo::MeterWindow w;
    w.outPeak[1] = 0.75f;
    w.numSamples = 1440;
    shared.publish(w);
    REQUIRE(shared.consume(out));
    CHECK(out == w);
    CHECK_FALSE(shared.consume(out));
    // Read: the next window replaces it (no merge with a window already read)
    Disrumpo::MeterWindow w2;
    w2.outPeak[1] = 0.25f;
    w2.numSamples = 1440;
    shared.publish(w2);
    REQUIRE(shared.consume(out));
    CHECK(out.outPeak[1] == 0.25f);
    CHECK(out.numSamples == 1440u);
}

TEST_CASE("A NaN input sample costs the input meter one window", "[disrumpo][meters][tier3]") {
    constexpr double kRate = 48000.0;
    constexpr Steinberg::int32 kBlock = 256;
    ts::Rig rig(kRate, kBlock);
    ts::PList l{{Disrumpo::kBandCountId, 0.0}, {Disrumpo::kOversampleMaxId, 0.0}};
    for (uint8_t n = 0; n < 4; ++n)
        l.emplace_back(Disrumpo::makeNodeParamId(0, n, Disrumpo::NodeParamType::kNodeDrive), 0.0);
    rig.apply(l);
    rig.reactivate();

    const std::vector<uint8_t> state = ts::getStateBytes(*rig.p);
    REQUIRE(state.size() >= 8);
    uint64_t id = 0;
    std::memcpy(&id, state.data() + state.size() - 8, sizeof id);
    auto* shared = static_cast<Disrumpo::SharedDisplay*>(Krate::Plugins::SharedDisplayBridge::instance().lookupInstance(id));
    REQUIRE(shared != nullptr);
    REQUIRE(shared->meters != nullptr);

    std::vector<float> inL(kBlock);
    std::vector<float> inR(kBlock);
    std::vector<float> outL(kBlock);
    std::vector<float> outR(kBlock);
    std::vector<Disrumpo::MeterWindow> windows;
    int nanWindow = -1;
    for (int block = 0; block < 60; ++block) {
        for (Steinberg::int32 i = 0; i < kBlock; ++i) {
            const double t = static_cast<double>(block * kBlock + i) / kRate;
            inL[static_cast<size_t>(i)] = static_cast<float>(0.5 * std::sin(6.283185307179586 * 1000.0 * t));
        }
        inR = inL;
        if (block == 20) inL[10] = std::numeric_limits<float>::quiet_NaN();
        rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock, nullptr);
        Disrumpo::MeterWindow w;
        if (shared->meters->consume(w)) {
            windows.push_back(w);
            if (block >= 20 && nanWindow < 0) nanWindow = static_cast<int>(windows.size()) - 1;
        }
    }
    REQUIRE(nanWindow >= 1);
    REQUIRE(static_cast<size_t>(nanWindow) + 2 < windows.size());
    size_t nonFinite = 0;
    for (const auto& w : windows) nonFinite += windowFinite(w) ? 0u : 1u;
    const auto& before = windows[static_cast<size_t>(nanWindow) - 1];
    const auto& hit = windows[static_cast<size_t>(nanWindow)];
    const auto& after = windows[static_cast<size_t>(nanWindow) + 1];
    std::printf("Meters NaN: %zu windows, %zu non-finite; in L peak before %.3f dB, NaN window %.3f dB (RMS lin %g), "
                "after %.3f dB; in R after %.3f dB; out L after %.3f dB; clip flags NaN window 0x%x\n",
                windows.size(), nonFinite, linDb(before.inPeak[0]), linDb(hit.inPeak[0]),
                static_cast<double>(hit.inMeanSq[0]), linDb(after.inPeak[0]), linDb(after.inPeak[1]),
                linDb(after.outPeak[0]), hit.clipFlags);
    CHECK(nonFinite == 0u);
    CHECK((hit.clipFlags & (Disrumpo::kMeterClipInL | Disrumpo::kMeterClipInR)) == 0u);
    CHECK(std::fabs(linDb(before.inPeak[0]) - -6.02) < 0.05);
    CHECK(std::fabs(linDb(after.inPeak[0]) - -6.02) < 0.05);
    CHECK(std::fabs(10.0 * std::log10(static_cast<double>(after.inMeanSq[0])) - -9.03) < 0.05);
    CHECK(std::fabs(linDb(after.inPeak[1]) - -6.02) < 0.05);
}
