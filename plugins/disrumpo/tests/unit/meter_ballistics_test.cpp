// ==============================================================================
// MeterBallistics: attack, hold, release, RMS window, clip latch
// ==============================================================================

#include "controller/meter_ballistics.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

using Disrumpo::MeterBallistics;

constexpr double kRate = 48000.0;
constexpr float kHalfDb = -6.0206f;  // 20 log10(0.5)

}  // namespace

TEST_CASE("Meter peak: instant attack, 1.5 s hold, then 20 dB/s", "[disrumpo][meters][ballistics]") {
    MeterBallistics m;
    m.addWindow(0.5f, 0.0f, 1440, false, kRate);
    m.tick(0.0);
    const float attack = m.peakDb();
    // Hold: three 0.5 s ticks of silence end exactly at 1.5 s
    m.tick(0.5);
    m.tick(0.5);
    m.tick(0.5);
    const float atHoldEnd = m.peakDb();
    m.tick(0.1);
    const float after100ms = m.peakDb();
    // Release measured at the editor's 33 ms tick: 1 s after the hold ended
    MeterBallistics r;
    r.addWindow(0.5f, 0.0f, 1440, false, kRate);
    r.tick(0.0);
    double t = 0.0;
    float lastHeld = r.peakDb();
    while (t + 0.033 <= 1.5) {
        r.tick(0.033);
        t += 0.033;
        lastHeld = r.peakDb();
    }
    const double tHeld = t;
    while (t < 2.5 - 1e-9) {
        r.tick(0.033);
        t += 0.033;
    }
    const float oneSecondLater = r.peakDb();
    std::printf("Meter peak: attack %.3f dB (want %.3f), at 1.5 s %.3f dB, 1.6 s %.3f dB (want %.3f); 33 ms ticks: "
                "held %.3f dB at %.3f s, %.3f dB at %.3f s (want %.3f +- 0.5)\n",
                static_cast<double>(attack), static_cast<double>(kHalfDb), static_cast<double>(atHoldEnd),
                static_cast<double>(after100ms), static_cast<double>(kHalfDb - 2.0f), static_cast<double>(lastHeld),
                tHeld, static_cast<double>(oneSecondLater), t,
                static_cast<double>(kHalfDb) - 20.0 * (t - 1.5));
    CHECK(std::fabs(attack - kHalfDb) < 1e-3f);
    CHECK(atHoldEnd == attack);
    CHECK(std::fabs(after100ms - (kHalfDb - 2.0f)) < 1e-3f);
    CHECK(lastHeld == attack);
    CHECK(std::fabs(oneSecondLater - (kHalfDb - 20.0f)) < 0.5f);

    // A new, louder window during the fall attacks at once and holds again
    r.addWindow(1.0f, 0.0f, 1440, false, kRate);
    r.tick(0.033);
    CHECK(std::fabs(r.peakDb()) < 1e-3f);
    r.tick(1.4);
    CHECK(std::fabs(r.peakDb()) < 1e-3f);

    // It falls to the floor and no further
    for (int i = 0; i < 400; ++i) r.tick(0.033);
    CHECK(r.peakDb() == MeterBallistics::kFloorDb);
}

TEST_CASE("Meter RMS integrates the last 300 ms of audio", "[disrumpo][meters][ballistics]") {
    // A -6.02 dBFS RMS step (mean square 0.25) in 10 ms windows, after silence
    MeterBallistics m;
    constexpr uint32_t k10ms = 480;
    float at150 = 0.0f;
    float at280 = 0.0f;
    float at290 = 0.0f;
    float at300 = 0.0f;
    float at1000 = 0.0f;
    for (int w = 1; w <= 100; ++w) {
        m.addWindow(0.7f, 0.25f, k10ms, false, kRate);
        m.tick(0.01);
        if (w == 15) at150 = m.rmsDb();
        if (w == 28) at280 = m.rmsDb();
        if (w == 29) at290 = m.rmsDb();
        if (w == 30) at300 = m.rmsDb();
        if (w == 100) at1000 = m.rmsDb();
    }
    // The step reaches -0.2 dB of its level at 0.3 s x 10^(-0.02) = 286.5 ms
    std::printf("Meter RMS step (level %.3f dB): 150 ms %.3f, 280 ms %.3f, 290 ms %.3f, 300 ms %.3f, 1 s %.3f dB\n",
                static_cast<double>(kHalfDb), static_cast<double>(at150), static_cast<double>(at280),
                static_cast<double>(at290), static_cast<double>(at300), static_cast<double>(at1000));
    CHECK(std::fabs(at150 - (kHalfDb - 3.0103f)) < 0.01f);
    CHECK(at280 < kHalfDb - 0.2f);
    CHECK(at290 > kHalfDb - 0.2f);
    CHECK(std::fabs(at300 - kHalfDb) < 1e-3f);
    CHECK(std::fabs(at1000 - kHalfDb) < 1e-3f);

    // ...and leaves the window 300 ms after the signal stops
    for (int w = 1; w <= 15; ++w) {
        m.addWindow(0.0f, 0.0f, k10ms, false, kRate);
        m.tick(0.01);
    }
    const float decay150 = m.rmsDb();
    for (int w = 16; w <= 30; ++w) {
        m.addWindow(0.0f, 0.0f, k10ms, false, kRate);
        m.tick(0.01);
    }
    std::printf("Meter RMS release: 150 ms after the stop %.3f dB, 300 ms after %.1f dB\n",
                static_cast<double>(decay150), static_cast<double>(m.rmsDb()));
    CHECK(std::fabs(decay150 - (kHalfDb - 3.0103f)) < 0.01f);
    CHECK(m.rmsDb() == MeterBallistics::kFloorDb);
}

TEST_CASE("Meter clip latches until cleared", "[disrumpo][meters][ballistics]") {
    MeterBallistics m;
    m.addWindow(0.5f, 0.1f, 1440, false, kRate);
    m.tick(0.033);
    CHECK_FALSE(m.clipLatched());
    m.addWindow(1.2f, 0.1f, 1440, true, kRate);
    m.tick(0.033);
    CHECK(m.clipLatched());
    for (int i = 0; i < 300; ++i) {  // 10 s of clean windows
        m.addWindow(0.5f, 0.1f, 1440, false, kRate);
        m.tick(0.033);
    }
    CHECK(m.clipLatched());
    m.clearClip();
    CHECK_FALSE(m.clipLatched());
    m.addWindow(0.5f, 0.1f, 1440, false, kRate);
    m.tick(0.033);
    CHECK_FALSE(m.clipLatched());
}
