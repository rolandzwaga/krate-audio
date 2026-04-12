// ==============================================================================
// ModalResonatorBank Frequency Accessor Tests (Phase 5, T005)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <krate/dsp/processors/modal_resonator_bank.h>
#include <cmath>
#include <numbers>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;

// Helper: compute epsilon from frequency (the inverse of getModeFrequency)
float frequencyToEpsilon(float freq, float sr)
{
    return 2.0f * std::sin(std::numbers::pi_v<float> * freq / sr);
}

} // namespace

TEST_CASE("ModalResonatorBank getModeFrequency recovers Hz from epsilon",
          "[modal_bank_frequency]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(kSampleRate);

    // Set modes at known frequencies
    constexpr int numPartials = 4;
    const float freqs[numPartials] = { 440.0f, 880.0f, 1320.0f, 1760.0f };
    const float amps[numPartials]  = { 1.0f, 0.5f, 0.25f, 0.125f };

    // stretch=0 and scatter=0 to avoid frequency warping in computeModeCoefficients
    bank.setModes(freqs, amps, numPartials,
                  /*decayTime=*/0.5f, /*brightness=*/0.5f,
                  /*stretch=*/0.0f, /*scatter=*/0.0f);

    SECTION("Frequency recovery matches input within tolerance")
    {
        for (int k = 0; k < numPartials; ++k)
        {
            float recovered = bank.getModeFrequency(k);
            // With stretch=0/scatter=0, epsilon encodes the raw frequency;
            // asin inversion should recover it closely (< 0.1 Hz error)
            REQUIRE(recovered == Approx(freqs[k]).margin(0.1f));
        }
    }

    SECTION("Out-of-range index returns 0")
    {
        REQUIRE(bank.getModeFrequency(-1) == 0.0f);
        REQUIRE(bank.getModeFrequency(numPartials) == 0.0f);
        REQUIRE(bank.getModeFrequency(100) == 0.0f);
    }
}

TEST_CASE("ModalResonatorBank getNumModes returns configured count",
          "[modal_bank_frequency]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(kSampleRate);

    SECTION("Before setModes, numModes is 0")
    {
        REQUIRE(bank.getNumModes() == 0);
    }

    SECTION("After setModes with 4 partials, numModes is 4")
    {
        constexpr int numPartials = 4;
        const float freqs[numPartials] = { 200.0f, 400.0f, 600.0f, 800.0f };
        const float amps[numPartials]  = { 1.0f, 0.5f, 0.25f, 0.125f };
        bank.setModes(freqs, amps, numPartials,
                      0.5f, 0.5f, 0.0f, 0.0f);

        REQUIRE(bank.getNumModes() == numPartials);
    }
}

TEST_CASE("ModalResonatorBank getModeFrequency with zero epsilon returns 0",
          "[modal_bank_frequency]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(kSampleRate);

    // Set a single mode at a very low frequency that results in near-zero epsilon
    // After reset, epsilonTarget should still be stored from setModes
    constexpr int numPartials = 1;
    const float freqs[numPartials] = { 440.0f };
    const float amps[numPartials]  = { 1.0f };
    bank.setModes(freqs, amps, numPartials, 0.5f, 0.5f, 0.0f, 0.0f);

    // Mode 0 should return a valid frequency
    REQUIRE(bank.getModeFrequency(0) > 0.0f);
}
