// ==============================================================================
// Spectral Delay Parameters Unit Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// Normalization Formulas (extracted from spectral_params.h)

// FFT Size: 512, 1024, 2048, 4096 (index 0-3)
int denormFFTSize(double normalized) {
    int index = static_cast<int>(normalized * 3.0 + 0.5);
    int sizes[] = {512, 1024, 2048, 4096};
    return sizes[index < 0 ? 0 : (index > 3 ? 3 : index)];
}

// Base Delay: 0-2000ms
float denormBaseDelay(double normalized) {
    return static_cast<float>(normalized * 2000.0);
}

double normBaseDelay(float ms) {
    return static_cast<double>(ms / 2000.0f);
}

// Spread: 0-2000ms (same as base delay)
float denormSpread(double normalized) {
    return static_cast<float>(normalized * 2000.0);
}

// Spread Direction: 0-2 discrete
int denormSpreadDirection(double normalized) {
    return static_cast<int>(normalized * 2.0 + 0.5);
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

// Feedback Tilt: -1.0 to +1.0
float denormFeedbackTilt(double normalized) {
    return static_cast<float>(-1.0 + normalized * 2.0);
}

double normFeedbackTilt(float tilt) {
    return static_cast<double>((tilt + 1.0f) / 2.0f);
}

// Freeze: boolean
bool denormFreeze(double normalized) {
    return normalized >= 0.5;
}

// Diffusion: 0-1 passthrough
float denormDiffusion(double normalized) {
    return static_cast<float>(normalized);
}

// Dry/Wet: 0-100%
float denormDryWet(double normalized) {
    return static_cast<float>(normalized * 100.0);
}

// Spread Curve: 0-1 discrete (Linear=0, Logarithmic=1)
int denormSpreadCurve(double normalized) {
    return normalized >= 0.5 ? 1 : 0;
}

// Stereo Width: 0-1 passthrough
float denormStereoWidth(double normalized) {
    return static_cast<float>(normalized);
}

} // anonymous namespace

TEST_CASE("Spectral FFT Size normalization", "[params][spectral]") {
    SECTION("normalized 0.0 -> 512") {
        REQUIRE(denormFFTSize(0.0) == 512);
    }
    SECTION("normalized 0.333 -> 1024") {
        REQUIRE(denormFFTSize(0.333) == 1024);
    }
    SECTION("normalized 0.667 -> 2048") {
        REQUIRE(denormFFTSize(0.667) == 2048);
    }
    SECTION("normalized 1.0 -> 4096") {
        REQUIRE(denormFFTSize(1.0) == 4096);
    }
}

TEST_CASE("Spectral Base Delay normalization", "[params][spectral]") {
    SECTION("normalized 0.0 -> 0ms") {
        REQUIRE(denormBaseDelay(0.0) == Approx(0.0f));
    }
    SECTION("normalized 0.5 -> 1000ms") {
        REQUIRE(denormBaseDelay(0.5) == Approx(1000.0f));
    }
    SECTION("normalized 1.0 -> 2000ms") {
        REQUIRE(denormBaseDelay(1.0) == Approx(2000.0f));
    }
    SECTION("round-trip: 250ms (default)") {
        float original = 250.0f;
        double normalized = normBaseDelay(original);
        float result = denormBaseDelay(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Spectral Feedback Tilt normalization", "[params][spectral]") {
    SECTION("normalized 0.0 -> -1.0") {
        REQUIRE(denormFeedbackTilt(0.0) == Approx(-1.0f));
    }
    SECTION("normalized 0.5 -> 0.0 (center)") {
        REQUIRE(denormFeedbackTilt(0.5) == Approx(0.0f));
    }
    SECTION("normalized 1.0 -> +1.0") {
        REQUIRE(denormFeedbackTilt(1.0) == Approx(1.0f));
    }
    SECTION("round-trip: 0.0") {
        float original = 0.0f;
        double normalized = normFeedbackTilt(original);
        float result = denormFeedbackTilt(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

TEST_CASE("Spectral Spread Direction normalization", "[params][spectral]") {
    SECTION("normalized 0.0 -> LowToHigh (0)") {
        REQUIRE(denormSpreadDirection(0.0) == 0);
    }
    SECTION("normalized 0.5 -> HighToLow (1)") {
        REQUIRE(denormSpreadDirection(0.5) == 1);
    }
    SECTION("normalized 1.0 -> CenterOut (2)") {
        REQUIRE(denormSpreadDirection(1.0) == 2);
    }
}

TEST_CASE("Spectral passthrough parameters", "[params][spectral]") {
    SECTION("Diffusion is 0-1 passthrough") {
        REQUIRE(denormDiffusion(0.0) == Approx(0.0f));
        REQUIRE(denormDiffusion(0.5) == Approx(0.5f));
        REQUIRE(denormDiffusion(1.0) == Approx(1.0f));
    }
    SECTION("Stereo Width is 0-1 passthrough") {
        REQUIRE(denormStereoWidth(0.0) == Approx(0.0f));
        REQUIRE(denormStereoWidth(0.5) == Approx(0.5f));
        REQUIRE(denormStereoWidth(1.0) == Approx(1.0f));
    }
}

TEST_CASE("Spectral Spread Curve normalization", "[params][spectral]") {
    SECTION("normalized 0.0 -> Linear (0)") {
        REQUIRE(denormSpreadCurve(0.0) == 0);
    }
    SECTION("normalized 0.49 -> Linear (0)") {
        REQUIRE(denormSpreadCurve(0.49) == 0);
    }
    SECTION("normalized 0.5 -> Logarithmic (1)") {
        REQUIRE(denormSpreadCurve(0.5) == 1);
    }
    SECTION("normalized 1.0 -> Logarithmic (1)") {
        REQUIRE(denormSpreadCurve(1.0) == 1);
    }
}
