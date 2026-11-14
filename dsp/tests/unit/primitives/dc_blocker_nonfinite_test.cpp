// ==============================================================================
// Layer 1: Primitive Tests - DCBlocker, non-finite TU
// ==============================================================================
// Disrumpo audit finding D-162 (fix plan T2.17): DCBlocker::process stored a
// NaN/Inf input in x1_ / y1_ (flushDenormal keeps NaN), so one bad sample made
// every later output non-finite until reset(). DCBlocker now passes the bad
// sample on (FR-016 / FR-017: it propagates, so a downstream finiteness trap
// such as FeedbackEcology's rung 5 still sees an upstream fault) but resets its
// state, so it never latches. FR-016 / FR-017 moved here from dc_blocker_test.cpp.
//
// Built only into dsp_ieee_tests (-fno-fast-math): it injects NaN/+-Inf built
// from bit patterns through a volatile. Finiteness is read only through
// detail::isFinite.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>

using namespace Krate::DSP;

namespace {

float dcnfFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(sink));
}

constexpr std::array<std::pair<std::uint32_t, const char*>, 3> kDcnfBad{
    {{0x7FC00000u, "NaN"}, {0x7F800000u, "+Inf"}, {0xFF800000u, "-Inf"}}};

} // namespace

TEST_CASE("FR-016/FR-017: a non-finite input propagates for that sample and resets the state",
          "[dc_blocker][FR-016][FR-017][nonfinite][D-162]") {
    for (const auto& [bits, name] : kDcnfBad) {
        DCBlocker blocker;
        blocker.prepare(44100.0, 10.0f);
        for (int i = 0; i < 10; ++i) {
            (void)blocker.process(0.5f);
        }
        const float output = blocker.process(dcnfFromBits(bits));
        INFO(name << ": output = " << output);
        // Bits, not ==: under MSVC /fp:fast a NaN compares equal to anything.
        REQUIRE(std::bit_cast<std::uint32_t>(output) == bits);
        // The state was cleared: the next sample is processed from zero state.
        const float next = blocker.process(0.25f);
        REQUIRE(detail::isFinite(next));
        REQUIRE(next == 0.25f);
    }
}

TEST_CASE("DCBlocker recovers after one NaN / +Inf sample", "[dc_blocker][nonfinite][D-162]") {
    for (const auto& [bits, name] : kDcnfBad) {
        DCBlocker blocker;
        blocker.prepare(48000.0, 10.0f);
        for (int i = 0; i < 100; ++i) {
            (void)blocker.process(0.5f * std::sin(0.05f * static_cast<float>(i)));
        }
        (void)blocker.process(dcnfFromBits(bits));
        float last = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            last = blocker.process(0.0f);
        }
        std::printf("D-162 DCBlocker one %s then 1000 zeros: last output %g (finite: %s)\n", name,
                    static_cast<double>(last), detail::isFinite(last) ? "yes" : "no");
        INFO(name << ": last output = " << last);
        REQUIRE(detail::isFinite(last));
        REQUIRE(std::abs(last) < 1e-3f);

        // It filters again once finite input returns.
        float y = 0.0f;
        for (int i = 0; i < 100; ++i) {
            y = blocker.process(0.5f * std::sin(0.05f * static_cast<float>(i)));
        }
        REQUIRE(detail::isFinite(y));
        REQUIRE(std::abs(y) <= 1.0f);
    }
}
