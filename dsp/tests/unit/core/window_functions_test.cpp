// ==============================================================================
// Layer 0: Core Utility Tests - Window Functions
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: src/dsp/core/window_functions.h
// Contract: specs/007-fft-processor/contracts/fft_processor.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/window_functions.h>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Constants
// ==============================================================================

constexpr size_t kTestWindowSize = 1024;
// Note: kPi is now available from Krate::DSP via math_constants.h

// ==============================================================================
// besselI0() Tests (T011)
// ==============================================================================

TEST_CASE("besselI0 returns known values", "[window][bessel][foundational]") {
    SECTION("I0(0) = 1 exactly") {
        REQUIRE(Window::besselI0(0.0f) == Approx(1.0f));
    }

    SECTION("I0(1) ≈ 1.266") {
        REQUIRE(Window::besselI0(1.0f) == Approx(1.266065877752f).margin(0.001f));
    }

    SECTION("I0(3) ≈ 4.881") {
        REQUIRE(Window::besselI0(3.0f) == Approx(4.880792585865f).margin(0.001f));
    }

    SECTION("I0(5) ≈ 27.24") {
        REQUIRE(Window::besselI0(5.0f) == Approx(27.2398718236f).margin(0.01f));
    }

    SECTION("I0 is symmetric (even function)") {
        REQUIRE(Window::besselI0(-2.0f) == Approx(Window::besselI0(2.0f)));
    }
}

// ==============================================================================
// generateHann() Tests (T012)
// ==============================================================================

TEST_CASE("generateHann produces correct window", "[window][hann][foundational]") {
    std::vector<float> window(kTestWindowSize);
    Window::generateHann(window.data(), window.size());

    SECTION("endpoints are zero (periodic variant)") {
        // Periodic Hann: first sample is 0, last is NOT 0
        REQUIRE(window[0] == Approx(0.0f).margin(1e-6f));
    }

    SECTION("peak is at center") {
        size_t center = kTestWindowSize / 2;
        float maxVal = *std::max_element(window.begin(), window.end());
        REQUIRE(window[center] == Approx(maxVal).margin(1e-6f));
    }

    SECTION("peak value is 1.0") {
        float maxVal = *std::max_element(window.begin(), window.end());
        REQUIRE(maxVal == Approx(1.0f).margin(1e-6f));
    }

    SECTION("satisfies COLA at 50% overlap") {
        // The periodic Hann window satisfies COLA (Constant Overlap-Add) at 50% overlap
        // This is the key property for STFT reconstruction, not perfect symmetry
        REQUIRE(Window::verifyCOLA(window.data(), window.size(), window.size() / 2));
    }
}

// ==============================================================================
// generateHamming() Tests (T013)
// ==============================================================================

TEST_CASE("generateHamming produces correct window", "[window][hamming][foundational]") {
    std::vector<float> window(kTestWindowSize);
    Window::generateHamming(window.data(), window.size());

    SECTION("endpoints are approximately 0.08") {
        REQUIRE(window[0] == Approx(0.08f).margin(0.01f));
    }

    SECTION("peak is approximately 1.0") {
        float maxVal = *std::max_element(window.begin(), window.end());
        REQUIRE(maxVal == Approx(1.0f).margin(0.01f));
    }

    SECTION("peak is at center") {
        size_t center = kTestWindowSize / 2;
        float maxVal = *std::max_element(window.begin(), window.end());
        REQUIRE(window[center] == Approx(maxVal).margin(1e-5f));
    }
}

// ==============================================================================
// generateBlackman() Tests (T014)
// ==============================================================================

TEST_CASE("generateBlackman produces correct window", "[window][blackman][foundational]") {
    std::vector<float> window(kTestWindowSize);
    Window::generateBlackman(window.data(), window.size());

    SECTION("endpoints are near zero") {
        REQUIRE(window[0] == Approx(0.0f).margin(0.01f));
    }

    SECTION("peak is approximately 1.0") {
        float maxVal = *std::max_element(window.begin(), window.end());
        REQUIRE(maxVal == Approx(1.0f).margin(0.01f));
    }

    SECTION("peak is at center") {
        size_t center = kTestWindowSize / 2;
        float maxVal = *std::max_element(window.begin(), window.end());
        REQUIRE(window[center] == Approx(maxVal).margin(1e-5f));
    }
}

// ==============================================================================
// generateKaiser() Tests (T015)
// ==============================================================================

TEST_CASE("generateKaiser produces correct window", "[window][kaiser][foundational]") {
    std::vector<float> window(kTestWindowSize);

    SECTION("beta=0 produces rectangular-like window") {
        Window::generateKaiser(window.data(), window.size(), 0.0f);
        // With beta=0, I0(0) = 1, so all samples should be ~1
        for (size_t i = 1; i < kTestWindowSize - 1; ++i) {
            REQUIRE(window[i] == Approx(1.0f).margin(0.1f));
        }
    }

    SECTION("beta=9 produces tapered window") {
        Window::generateKaiser(window.data(), window.size(), 9.0f);

        // Endpoints should be small
        REQUIRE(window[0] < 0.1f);
        REQUIRE(window[kTestWindowSize - 1] < 0.1f);

        // Center should be 1.0
        REQUIRE(window[kTestWindowSize / 2] == Approx(1.0f).margin(0.01f));
    }

    SECTION("higher beta produces narrower window") {
        std::vector<float> windowLowBeta(kTestWindowSize);
        std::vector<float> windowHighBeta(kTestWindowSize);

        Window::generateKaiser(windowLowBeta.data(), kTestWindowSize, 4.0f);
        Window::generateKaiser(windowHighBeta.data(), kTestWindowSize, 12.0f);

        // At 25% from edge, higher beta should be smaller
        size_t quarter = kTestWindowSize / 4;
        REQUIRE(windowHighBeta[quarter] < windowLowBeta[quarter]);
    }
}

// ==============================================================================
// verifyCOLA() Tests (T016)
// ==============================================================================

TEST_CASE("verifyCOLA verifies COLA property", "[window][cola][foundational]") {
    std::vector<float> window(kTestWindowSize);

    SECTION("Hann window is COLA at 50% overlap") {
        Window::generateHann(window.data(), window.size());
        bool isCOLA = Window::verifyCOLA(window.data(), window.size(),
                                          window.size() / 2);  // 50% overlap
        REQUIRE(isCOLA);
    }

    SECTION("Hann window is COLA at 75% overlap") {
        Window::generateHann(window.data(), window.size());
        bool isCOLA = Window::verifyCOLA(window.data(), window.size(),
                                          window.size() / 4);  // 75% overlap
        REQUIRE(isCOLA);
    }

    SECTION("Hamming window is COLA at 50% overlap") {
        Window::generateHamming(window.data(), window.size());
        bool isCOLA = Window::verifyCOLA(window.data(), window.size(),
                                          window.size() / 2);
        REQUIRE(isCOLA);
    }

    SECTION("Blackman window is COLA at 75% overlap") {
        Window::generateBlackman(window.data(), window.size());
        bool isCOLA = Window::verifyCOLA(window.data(), window.size(),
                                          window.size() / 4);
        REQUIRE(isCOLA);
    }
}

// ==============================================================================
// generate() Factory Tests (T023)
// ==============================================================================

TEST_CASE("generate factory function dispatches correctly", "[window][factory]") {
    SECTION("generates Hann window") {
        auto window = Window::generate(WindowType::Hann, 512);
        REQUIRE(window.size() == 512);
        REQUIRE(window[0] == Approx(0.0f).margin(1e-6f));
    }

    SECTION("generates Hamming window") {
        auto window = Window::generate(WindowType::Hamming, 512);
        REQUIRE(window.size() == 512);
        REQUIRE(window[0] == Approx(0.08f).margin(0.01f));
    }

    SECTION("generates Blackman window") {
        auto window = Window::generate(WindowType::Blackman, 512);
        REQUIRE(window.size() == 512);
        REQUIRE(window[0] == Approx(0.0f).margin(0.01f));
    }

    SECTION("generates Kaiser window with beta parameter") {
        auto window = Window::generate(WindowType::Kaiser, 512, 9.0f);
        REQUIRE(window.size() == 512);
        REQUIRE(window[256] == Approx(1.0f).margin(0.01f));
    }
}

// ==============================================================================
// generateBlackmanHarris() Tests (T020 - Innexus Phase 3)
// ==============================================================================

TEST_CASE("generateBlackmanHarris produces correct window", "[window][blackmanharris][foundational]") {
    std::vector<float> window(kTestWindowSize);
    Window::generateBlackmanHarris(window.data(), window.size());

    SECTION("first coefficient matches a0 = 0.35875") {
        // The Blackman-Harris window at n=0 (periodic variant, divides by N):
        // w[0] = a0 - a1*cos(0) + a2*cos(0) - a3*cos(0)
        // w[0] = 0.35875 - 0.48829 + 0.14128 - 0.01168 = 0.00006
        REQUIRE(window[0] == Approx(0.00006f).margin(0.001f));
    }

    SECTION("window is symmetric (periodic variant)") {
        // Periodic variant: w[n] = w[N-n] for n=1..N-1
        for (size_t n = 1; n < kTestWindowSize / 2; ++n) {
            REQUIRE(window[n] == Approx(window[kTestWindowSize - n]).margin(1e-5f));
        }
    }

    SECTION("peak is approximately 1.0 at center") {
        float maxVal = *std::max_element(window.begin(), window.end());
        REQUIRE(maxVal == Approx(1.0f).margin(0.01f));
    }

    SECTION("sidelobe rejection exceeds 90 dB") {
        // For a 1024-point BlackmanHarris, compute DFT and check sidelobe level
        // The theoretical sidelobe level of a 4-term Blackman-Harris window is ~-92 dB
        const size_t fftSize = kTestWindowSize;
        const size_t numBins = fftSize / 2 + 1;

        // Compute magnitude spectrum using DFT
        std::vector<float> magnitudes(numBins, 0.0f);
        for (size_t k = 0; k < numBins; ++k) {
            float realPart = 0.0f;
            float imagPart = 0.0f;
            for (size_t n = 0; n < fftSize; ++n) {
                float angle = kTwoPi * static_cast<float>(k) * static_cast<float>(n) / static_cast<float>(fftSize);
                realPart += window[n] * std::cos(angle);
                imagPart -= window[n] * std::sin(angle);
            }
            magnitudes[k] = std::sqrt(realPart * realPart + imagPart * imagPart);
        }

        // Find main lobe peak (should be at bin 0)
        float mainLobePeak = magnitudes[0];
        REQUIRE(mainLobePeak > 0.0f);

        // Find first sidelobe: skip the main lobe region (first ~8 bins for BH),
        // then find the maximum
        float sidelobePeak = 0.0f;
        for (size_t k = 8; k < numBins; ++k) {
            sidelobePeak = std::max(sidelobePeak, magnitudes[k]);
        }

        // Convert to dB
        float sidelobeDb = 20.0f * std::log10(sidelobePeak / mainLobePeak);

        // Blackman-Harris should have sidelobes below -90 dB
        REQUIRE(sidelobeDb < -90.0f);
    }
}

TEST_CASE("WindowType::BlackmanHarris enum compiles and dispatches via generate()", "[window][blackmanharris]") {
    auto window = Window::generate(WindowType::BlackmanHarris, 512);
    REQUIRE(window.size() == 512);

    // BlackmanHarris at n=0 should be near zero (like Blackman)
    REQUIRE(window[0] == Approx(0.0f).margin(0.01f));

    // Center should be near 1.0
    REQUIRE(window[256] == Approx(1.0f).margin(0.01f));
}
