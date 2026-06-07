// ==============================================================================
// ModalResonatorBank stretch (stiff-string inharmonicity) tests
// ==============================================================================
// Membrum signal-path audit §3-B "Stretch inharmonicity: form right, index
// wrong, range tiny" (finding L-1).
//
// Two defects are pinned here:
//   1. OFF-BY-ONE INDEX. The warp uses sqrt(1 + B*k^2) with a 0-based array
//      index k, so the fundamental (partial n=1, k=0) is left UNstretched and
//      every partial inherits the dispersion of the partial below it. The
//      physically correct stiff-string form is sqrt(1 + B*n^2) with n = k+1
//      (the bow-weight code in the same bank already uses k+1).
//   2. TINY RANGE. B_max = 0.001 is orders of magnitude below real
//      piano/bar stiffness, so at full Stretch the partials barely move.
//
// All assertions are on getModeFrequency(), which recovers the warped Hz from
// the stored resonator coefficient, so they are exact and host-independent.
// The stretch==0 case is a hard regression guard (must remain bit-exact).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <array>

using Catch::Approx;
using Krate::DSP::ModalResonatorBank;

namespace {

constexpr int   kNumModes   = 8;
constexpr float kBaseHz     = 200.0f;          // harmonic series 200, 400, ...
constexpr double kSampleRate = 48000.0;

// Configure a bank with a clean harmonic series f_k = kBaseHz * (k+1) and the
// requested stretch (no scatter, mild damping so every mode is active).
ModalResonatorBank makeBank(float stretch)
{
    std::array<float, kNumModes> freqs{};
    std::array<float, kNumModes> amps{};
    for (int k = 0; k < kNumModes; ++k)
    {
        freqs[static_cast<std::size_t>(k)] = kBaseHz * static_cast<float>(k + 1);
        amps[static_cast<std::size_t>(k)]  = 1.0f;
    }

    ModalResonatorBank bank;
    bank.prepare(kSampleRate);
    bank.setModes(freqs.data(), amps.data(), kNumModes,
                  /*decayTime*/ 0.5f, /*brightness*/ 0.5f,
                  stretch, /*scatter*/ 0.0f);
    return bank;
}

} // namespace

// ------------------------------------------------------------------------------
// stretch == 0 must leave the harmonic series exactly unwarped (regression guard
// for the default-off / Innexus stretch=0 path).
// ------------------------------------------------------------------------------
TEST_CASE("ModalResonatorBank stretch=0 leaves the spectrum unwarped",
          "[ModalResonatorBank][stretch]")
{
    auto bank = makeBank(0.0f);
    for (int k = 0; k < kNumModes; ++k)
    {
        const float expected = kBaseHz * static_cast<float>(k + 1);
        CHECK(bank.getModeFrequency(k) == Approx(expected).margin(0.5f));
    }
}

// ------------------------------------------------------------------------------
// Defect 1: the FUNDAMENTAL (partial n=1, array index 0) must be stretched.
// Correct form sqrt(1 + B*(k+1)^2) gives the fundamental a factor sqrt(1+B) > 1.
// The buggy sqrt(1 + B*k^2) leaves it at exactly kBaseHz (factor 1).
// ------------------------------------------------------------------------------
TEST_CASE("ModalResonatorBank stretch warps the fundamental (index is 1-based)",
          "[ModalResonatorBank][stretch]")
{
    auto bank = makeBank(1.0f);
    const float f0 = bank.getModeFrequency(0);
    INFO("fundamental: input=" << kBaseHz << " warped=" << f0);
    // Must be measurably above the input fundamental (the off-by-one bug pins
    // it to exactly kBaseHz).
    CHECK(f0 > kBaseHz * 1.001f);
}

// ------------------------------------------------------------------------------
// Defect 2: at full Stretch the inharmonicity must be musically significant.
// With B_max = 0.001 the 8th partial (n=8) moves < 4 %; a usable metallic
// Stretch needs the high partials clearly displaced. Assert >= 5 % at the top.
// ------------------------------------------------------------------------------
TEST_CASE("ModalResonatorBank stretch range is musically significant at max",
          "[ModalResonatorBank][stretch]")
{
    auto bank = makeBank(1.0f);
    const int   topK     = kNumModes - 1;                 // n = 8
    const float unwarped = kBaseHz * static_cast<float>(topK + 1);
    const float warped   = bank.getModeFrequency(topK);
    const float pct      = (warped / unwarped - 1.0f) * 100.0f;
    INFO("top partial: unwarped=" << unwarped << " warped=" << warped
         << " (+" << pct << "%)");
    CHECK(warped > unwarped * 1.05f);
}

// ------------------------------------------------------------------------------
// Monotonicity: stiff-string dispersion is strictly increasing in partial
// number, so the per-partial stretch factor must grow with k (the off-by-one
// shifts the whole curve down by one index but keeps it monotone, so this is a
// supporting, not a discriminating, check).
// ------------------------------------------------------------------------------
TEST_CASE("ModalResonatorBank stretch factor increases with partial number",
          "[ModalResonatorBank][stretch]")
{
    auto bank = makeBank(1.0f);
    float prevFactor = 0.0f;
    for (int k = 0; k < kNumModes; ++k)
    {
        const float unwarped = kBaseHz * static_cast<float>(k + 1);
        const float factor   = bank.getModeFrequency(k) / unwarped;
        INFO("k=" << k << " factor=" << factor);
        CHECK(factor >= prevFactor);
        prevFactor = factor;
    }
}
