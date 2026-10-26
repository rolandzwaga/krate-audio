// ==============================================================================
// Layer 1: Primitive Tests - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Reference: specs/profundum-phase4-drop-note-behaviour/plan.md (S7, S11.1)
//            specs/profundum-phase4-drop-note-behaviour/tasks.md (T014)
//
// Holds: RetriggerDeclick_* (SC-011 unit parts, FR-041-FR-044, E-5, deviation 7).
// Gain law (plan S1 C-15): g[k] = 0.5(1 + cos(pi(k+1)/L_d)), k = 0 .. L_d - 1,
// switch sample k = L_d - 1 written exactly 0.0f, 1.0f afterwards.
// ==============================================================================

#include <krate/dsp/primitives/retrigger_declick.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

using Krate::DSP::RetriggerDeclick;

namespace {

std::size_t expectedDeclickLength(double sampleRate) {
    return static_cast<std::size_t>(
        std::ceil(static_cast<double>(RetriggerDeclick::kDeclickMs) * sampleRate / 1000.0));
}

bool bitEqual(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

} // namespace

TEST_CASE("RetriggerDeclick_Timing", "[primitives][profundum][declick]") {
    // plan S1 C-15: raised-cosine law, switch sample exactly 0.0f.
    struct RateCase {
        double fs;
        std::size_t lengthD;
    };
    const RateCase cases[] = {{44100.0, 67}, {48000.0, 72}, {96000.0, 144}};

    for (const auto& rc : cases) {
        INFO("fs = " << rc.fs);
        const std::size_t L = expectedDeclickLength(rc.fs);
        REQUIRE(L == rc.lengthD); // 66.15 -> 67 at 44.1 kHz

        RetriggerDeclick dc;
        dc.prepare(rc.fs);
        REQUIRE_FALSE(dc.isActive());
        REQUIRE(dc.samplesUntilSwitch() == 0);

        dc.arm();
        REQUIRE(dc.isActive());
        REQUIRE(dc.samplesUntilSwitch() == L - 1);

        // Restart latency (FR-042): the deferred noteOn lands L_d - 1 samples after arm().
        const std::size_t latency = dc.samplesUntilSwitch();
        const std::size_t latencyBound = expectedDeclickLength(rc.fs) + 1;
        INFO("restart latency = " << latency << " samples (bound " << latencyBound << ")");
        CHECK(latency <= latencyBound);

        // Trace with the pre-arm sample (1.0) prepended for the slope checks.
        std::vector<float> trace;
        trace.push_back(1.0f);

        double maxRefErr = 0.0;
        for (std::size_t k = 0; k < L; ++k) {
            REQUIRE(dc.samplesUntilSwitch() == L - 1 - k);
            REQUIRE(dc.isActive());
            const float g = dc.process();
            trace.push_back(g);
            if (k < L - 1) {
                const double ref =
                    0.5 * (1.0 + std::cos(std::numbers::pi * static_cast<double>(k + 1) /
                                          static_cast<double>(L)));
                maxRefErr = std::max(maxRefErr, std::abs(static_cast<double>(g) - ref));
            } else {
                // Switch sample: exactly +0.0f (bit test).
                REQUIRE(std::bit_cast<std::uint32_t>(g) == 0u);
            }
        }
        INFO("max |g - ref| = " << maxRefErr);
        CHECK(maxRefErr <= 1e-7);

        REQUIRE_FALSE(dc.isActive());
        REQUIRE(dc.samplesUntilSwitch() == 0);
        const float after = dc.process();
        REQUIRE(bitEqual(after, 1.0f));
        REQUIRE_FALSE(dc.isActive());

        // Peak |dg| over the dip (pre-arm 1.0 .. switch 0.0).
        const double Ld = static_cast<double>(L);
        double peakD1 = 0.0;
        for (std::size_t i = 1; i < trace.size(); ++i)
            peakD1 = std::max(peakD1, std::abs(static_cast<double>(trace[i]) -
                                               static_cast<double>(trace[i - 1])));
        const double d1Bound = std::numbers::pi / (2.0 * Ld) * 1.0001;
        INFO("peak |dg| = " << peakD1 << " bound " << d1Bound);
        CHECK(peakD1 <= d1Bound);

        // C1: max |d2g| over the dip, excluding the deliberate return step at the
        // switch (the trace stops at the switch sample).
        double peakD2 = 0.0;
        for (std::size_t i = 1; i + 1 < trace.size(); ++i) {
            const double d2 = static_cast<double>(trace[i + 1]) -
                              2.0 * static_cast<double>(trace[i]) +
                              static_cast<double>(trace[i - 1]);
            peakD2 = std::max(peakD2, std::abs(d2));
        }
        const double d2Bound = 1.05 * std::numbers::pi * std::numbers::pi / (2.0 * Ld * Ld);
        INFO("peak |d2g| = " << peakD2 << " bound " << d2Bound);
        CHECK(peakD2 <= d2Bound);
    }
}

TEST_CASE("RetriggerDeclick_RearmNoRestart", "[primitives][profundum][declick]") {
    // FR-043, E-5: a re-arm during the dip never restarts it.
    constexpr std::size_t kN = 200;
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        INFO("fs = " << fs);
        RetriggerDeclick single;
        RetriggerDeclick rearmed;
        single.prepare(fs);
        rearmed.prepare(fs);

        std::vector<float> a(kN);
        std::vector<float> b(kN);
        single.arm();
        rearmed.arm();
        for (std::size_t k = 0; k < kN; ++k) {
            if (k == 20) {
                rearmed.arm();
                REQUIRE(rearmed.samplesUntilSwitch() == single.samplesUntilSwitch());
            }
            a[k] = single.process();
            b[k] = rearmed.process();
        }
        REQUIRE(std::equal(a.begin(), a.end(), b.begin(), bitEqual));
    }
}

TEST_CASE("RetriggerDeclick_Idle", "[primitives][profundum][declick]") {
    SECTION("never armed: exactly 1.0f for 10 000 samples") {
        RetriggerDeclick dc;
        dc.prepare(48000.0);
        bool allOne = true;
        for (int i = 0; i < 10000; ++i)
            allOne = allOne && bitEqual(dc.process(), 1.0f);
        REQUIRE(allOne);
        REQUIRE_FALSE(dc.isActive());
    }

    SECTION("processBlock equals the per-sample trace for blocks {1, 37, 512}") {
        constexpr std::size_t kN = 2048;
        // Arm points land on idle states (0, 1000) and on an active one (40).
        const std::size_t armAt[] = {0, 40, 1000};

        RetriggerDeclick ref;
        ref.prepare(48000.0);
        std::vector<float> perSample(kN);
        for (std::size_t n = 0; n < kN; ++n) {
            for (const auto at : armAt)
                if (n == at)
                    ref.arm();
            perSample[n] = ref.process();
        }

        for (const std::size_t block : {std::size_t{1}, std::size_t{37}, std::size_t{512}}) {
            INFO("block = " << block);
            RetriggerDeclick dc;
            dc.prepare(48000.0);
            std::vector<float> blocked(kN, -1.0f);
            std::size_t n = 0;
            while (n < kN) {
                // Split blocks at the arm points so arming happens at the same sample.
                std::size_t len = std::min(block, kN - n);
                for (const auto at : armAt) {
                    if (at == n)
                        dc.arm();
                    else if (at > n && at < n + len)
                        len = at - n;
                }
                dc.processBlock(blocked.data() + n, len);
                n += len;
            }
            REQUIRE(std::equal(perSample.begin(), perSample.end(), blocked.begin(), bitEqual));
        }

        // Never-armed blocks are all exactly 1.0f.
        RetriggerDeclick idle;
        idle.prepare(48000.0);
        std::vector<float> buf(512, -1.0f);
        idle.processBlock(buf.data(), buf.size());
        REQUIRE(std::all_of(buf.begin(), buf.end(), [](float g) { return bitEqual(g, 1.0f); }));
    }

    SECTION("processBlock(nullptr, n) and (p, 0) are no-ops") {
        RetriggerDeclick dc;
        dc.prepare(48000.0);
        dc.arm();
        (void)dc.process();
        const std::size_t before = dc.samplesUntilSwitch();

        dc.processBlock(nullptr, 64);
        REQUIRE(dc.samplesUntilSwitch() == before);
        REQUIRE(dc.isActive());

        float sentinel[4] = {-2.0f, -2.0f, -2.0f, -2.0f};
        dc.processBlock(sentinel, 0);
        REQUIRE(dc.samplesUntilSwitch() == before);
        for (const float s : sentinel)
            REQUIRE(bitEqual(s, -2.0f));
    }

    SECTION("before prepare: arm() is a no-op and process() returns 1.0f") {
        RetriggerDeclick dc;
        dc.arm();
        REQUIRE_FALSE(dc.isActive());
        REQUIRE(dc.samplesUntilSwitch() == 0);
        bool allOne = true;
        for (int i = 0; i < 256; ++i)
            allOne = allOne && bitEqual(dc.process(), 1.0f);
        REQUIRE(allOne);
    }

    SECTION("prepare(NaN) is a no-op (deviation 7)") {
        const double nan = std::numeric_limits<double>::quiet_NaN();

        // Unprepared stays unprepared.
        RetriggerDeclick fresh;
        fresh.prepare(nan);
        fresh.arm();
        REQUIRE_FALSE(fresh.isActive());
        REQUIRE(bitEqual(fresh.process(), 1.0f));

        // Prepared keeps its rate (L_d = 72 at 48 kHz) and its running dip.
        RetriggerDeclick dc;
        dc.prepare(48000.0);
        dc.arm();
        (void)dc.process();
        const std::size_t before = dc.samplesUntilSwitch();
        dc.prepare(nan);
        REQUIRE(dc.isActive());
        REQUIRE(dc.samplesUntilSwitch() == before);

        dc.reset();
        REQUIRE_FALSE(dc.isActive());
        dc.arm();
        REQUIRE(dc.samplesUntilSwitch() == expectedDeclickLength(48000.0) - 1);
    }
}
