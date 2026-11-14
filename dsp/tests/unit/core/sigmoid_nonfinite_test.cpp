// ==============================================================================
// Layer 0: Core Tests - Sigmoid / Asymmetric, non-finite TU
// ==============================================================================
// Disrumpo audit finding D-167 (fix plan T4.15): Asymmetric::diode's reverse
// branch x / (1 - 0.5x) tends to -2 as x -> -Inf, but at x = -Inf itself it
// evaluates -Inf / +Inf = NaN, against Waveshaper::process()'s "Infinity inputs
// are handled gracefully". A -Inf reaches the shaper from finite input too:
// drive * x overflows (drive 10 * -FLT_MAX).
//
// Built only into dsp_ieee_tests (-fno-fast-math): it injects +-Inf / NaN built
// from bit patterns through a volatile. Finiteness is read only through
// detail::isFinite / detail::isNaN.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/primitives/waveshaper.h>

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cfloat>
#include <cstdint>
#include <cstdio>

using namespace Krate::DSP;

namespace {

float sgnfFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(sink));
}

} // namespace

TEST_CASE("Diode returns -2 for -Inf", "[sigmoid][diode][nonfinite][D-167]") {
    const float negInf = sgnfFromBits(0xFF800000u);

    SECTION("Asymmetric::diode") {
        const float out = Asymmetric::diode(negInf);
        std::printf("D-167 Asymmetric::diode(-Inf) -> 0x%08X\n",
                    static_cast<unsigned>(std::bit_cast<std::uint32_t>(out)));
        REQUIRE(detail::isFinite(out));
        REQUIRE(out == -2.0f);
    }

    SECTION("Waveshaper Diode, -Inf input") {
        Waveshaper shaper;
        shaper.setType(WaveshapeType::Diode);
        shaper.setDrive(1.0f);
        const float out = shaper.process(negInf);
        REQUIRE(detail::isFinite(out));
        REQUIRE(out == -2.0f);
    }

    SECTION("Waveshaper Diode, drive overflows a finite input to -Inf") {
        Waveshaper shaper;
        shaper.setType(WaveshapeType::Diode);
        shaper.setDrive(10.0f);
        const float out = shaper.process(-FLT_MAX);
        REQUIRE(detail::isFinite(out));
        REQUIRE(out == -2.0f);
    }
}

TEST_CASE("Diode stays in (-2, 1] and keeps NaN and +Inf behaviour", "[sigmoid][diode][nonfinite][D-167]") {
    // The documented bound: (-2, 1) for every finite input; 1 is reached only
    // by rounding once exp(-1.5x) drops below half an ulp of 1.
    REQUIRE(Asymmetric::diode(-FLT_MAX) >= -2.0f);
    REQUIRE(Asymmetric::diode(-1.0e6f) > -2.0f);
    REQUIRE(Asymmetric::diode(FLT_MAX) <= 1.0f);
    REQUIRE(Asymmetric::diode(sgnfFromBits(0x7F800000u)) == 1.0f);

    // NaN still propagates (Waveshaper: "NaN inputs are propagated").
    REQUIRE(detail::isNaN(Asymmetric::diode(sgnfFromBits(0x7FC00000u))));
    REQUIRE(detail::isNaN(Asymmetric::diode(sgnfFromBits(0xFFC00000u))));
}
