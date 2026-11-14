// ==============================================================================
// Layer 0: Core Tests - FastMath, non-finite TU
// ==============================================================================
// Disrumpo audit finding D-166 (fix plan T4.14): fastTanh answered a NaN input
// with std::numeric_limits<float>::quiet_NaN(), a NaN constant in a header that
// the macOS leg compiles with -ffast-math (undefined there; clang
// -Wnan-infinity-disabled). It now returns the incoming NaN itself, so the
// documented "NaN input returns NaN" contract holds without the constant, and
// the NaN that comes out is the one that went in (payload and sign kept).
//
// Built only into dsp_ieee_tests (-fno-fast-math): it injects NaN/+-Inf built
// from bit patterns through a volatile. Finiteness is read only through
// detail::isFinite / detail::isNaN.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/fast_math.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>

using namespace Krate::DSP;

namespace {

float fmnfFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(sink));
}

} // namespace

TEST_CASE("fastTanh returns the incoming NaN unchanged", "[fast_math][tanh][nonfinite][D-166]") {
    // Canonical quiet NaN, quiet NaNs with payloads, and a negative quiet NaN.
    constexpr std::array<std::uint32_t, 4> kNaNs{0x7FC00000u, 0x7FC01234u, 0x7FD5A5A5u, 0xFFC00042u};
    for (const std::uint32_t bits : kNaNs) {
        const float out = FastMath::fastTanh(fmnfFromBits(bits));
        const auto outBits = std::bit_cast<std::uint32_t>(out);
        std::printf("D-166 fastTanh(NaN 0x%08X) -> 0x%08X\n", static_cast<unsigned>(bits),
                    static_cast<unsigned>(outBits));
        INFO("input bits 0x" << std::hex << bits << ", output bits 0x" << outBits);
        REQUIRE(detail::isNaN(out));
        // Bits, not ==: under MSVC /fp:fast a NaN compares equal to anything.
        REQUIRE(outBits == bits);
    }
}

TEST_CASE("fastTanh maps +-Inf to +-1", "[fast_math][tanh][nonfinite][D-166]") {
    REQUIRE(FastMath::fastTanh(fmnfFromBits(0x7F800000u)) == 1.0f);
    REQUIRE(FastMath::fastTanh(fmnfFromBits(0xFF800000u)) == -1.0f);
}

// Hidden printing probe (T4.14 finite-output identity): fastTanh over EVERY
// finite float, folded into a 64-bit FNV-1a of the output bits. Run it before
// and after a change to fastTanh; equal digests mean the finite path did not
// move. It asserts nothing.
TEST_CASE("fastTanh finite-input digest probe", "[.][t414-fasttanh-digest]") {
    std::uint64_t digest = 0xCBF29CE484222325ull;
    std::uint64_t count = 0;
    for (std::uint64_t b = 0; b <= 0xFFFFFFFFull; ++b) {
        const float in = fmnfFromBits(static_cast<std::uint32_t>(b));
        if (!detail::isFinite(in)) {
            continue;
        }
        const float out = FastMath::fastTanh(in);
        digest = (digest ^ std::bit_cast<std::uint32_t>(out)) * 0x100000001B3ull;
        ++count;
    }
    std::printf("T4.14 fastTanh finite digest: %llu inputs, FNV-1a 0x%016llX\n",
                static_cast<unsigned long long>(count), static_cast<unsigned long long>(digest));
    SUCCEED();
}
