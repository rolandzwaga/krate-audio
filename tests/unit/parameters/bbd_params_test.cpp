// ==============================================================================
// BBD Delay Parameters Unit Tests
// ==============================================================================
// Tests normalization accuracy and formula correctness for BBD delay parameters.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// ==============================================================================
// Normalization Formulas (extracted from bbd_params.h for testing)
// ==============================================================================

// Delay Time: 20-1000ms
float denormDelayTime(double normalized) {
    return static_cast<float>(20.0 + normalized * 980.0);
}

double normDelayTime(float ms) {
    return static_cast<double>((ms - 20.0f) / 980.0f);
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

double normFeedback(float feedback) {
    return static_cast<double>(feedback / 1.2f);
}

// Modulation Depth: 0-1 (passthrough)
float denormModDepth(double normalized) {
    return static_cast<float>(normalized);
}

// Modulation Rate: 0.1-10Hz
float denormModRate(double normalized) {
    return static_cast<float>(0.1 + normalized * 9.9);
}

double normModRate(float hz) {
    return static_cast<double>((hz - 0.1f) / 9.9f);
}

// Age: 0-1 (passthrough)
float denormAge(double normalized) {
    return static_cast<float>(normalized);
}

// Era: 0-3 discrete
int denormEra(double normalized) {
    return static_cast<int>(normalized * 3.0 + 0.5);
}

double normEra(int era) {
    return static_cast<double>(era) / 3.0;
}

// Mix: 0-1 (passthrough)
float denormMix(double normalized) {
    return static_cast<float>(normalized);
}

// Output Level: -96 to +12 dB -> linear
float denormOutputLevel(double normalized) {
    double dB = -96.0 + normalized * 108.0;
    double linear = (dB <= -96.0) ? 0.0 : std::pow(10.0, dB / 20.0);
    return static_cast<float>(linear);
}

double normOutputLevel(float linear) {
    double dB = (linear <= 0.0f) ? -96.0 : 20.0 * std::log10(linear);
    return (dB + 96.0) / 108.0;
}

} // anonymous namespace

// ==============================================================================
// Delay Time Tests
// ==============================================================================

TEST_CASE("BBD Delay Time normalization", "[params][bbd]") {
    SECTION("normalized 0.0 -> 20ms (minimum)") {
        REQUIRE(denormDelayTime(0.0) == Approx(20.0f));
    }

    SECTION("normalized 0.5 -> 510ms (midpoint)") {
        REQUIRE(denormDelayTime(0.5) == Approx(510.0f));
    }

    SECTION("normalized 1.0 -> 1000ms (maximum)") {
        REQUIRE(denormDelayTime(1.0) == Approx(1000.0f));
    }

    SECTION("round-trip: 300ms") {
        float original = 300.0f;
        double normalized = normDelayTime(original);
        float result = denormDelayTime(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }

    SECTION("default value (300ms)") {
        // Default normalized is 0.286 = (300-20)/980
        REQUIRE(denormDelayTime(0.286) == Approx(300.28f).margin(1.0f));
    }
}

// ==============================================================================
// Feedback Tests
// ==============================================================================

TEST_CASE("BBD Feedback normalization", "[params][bbd]") {
    SECTION("normalized 0.0 -> 0.0 (minimum)") {
        REQUIRE(denormFeedback(0.0) == Approx(0.0f));
    }

    SECTION("normalized 0.5 -> 0.6 (60%)") {
        REQUIRE(denormFeedback(0.5) == Approx(0.6f));
    }

    SECTION("normalized 1.0 -> 1.2 (120% max)") {
        REQUIRE(denormFeedback(1.0) == Approx(1.2f));
    }

    SECTION("round-trip: 0.4 (40% default)") {
        float original = 0.4f;
        double normalized = normFeedback(original);
        float result = denormFeedback(normalized);
        REQUIRE(result == Approx(original).margin(0.001f));
    }
}

// ==============================================================================
// Modulation Rate Tests
// ==============================================================================

TEST_CASE("BBD Modulation Rate normalization", "[params][bbd]") {
    SECTION("normalized 0.0 -> 0.1Hz (minimum)") {
        REQUIRE(denormModRate(0.0) == Approx(0.1f));
    }

    SECTION("normalized 0.5 -> 5.05Hz (midpoint)") {
        REQUIRE(denormModRate(0.5) == Approx(5.05f));
    }

    SECTION("normalized 1.0 -> 10Hz (maximum)") {
        REQUIRE(denormModRate(1.0) == Approx(10.0f));
    }

    SECTION("round-trip: 0.5Hz (default)") {
        float original = 0.5f;
        double normalized = normModRate(original);
        float result = denormModRate(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

// ==============================================================================
// Era (Discrete) Tests
// ==============================================================================

TEST_CASE("BBD Era discrete normalization", "[params][bbd]") {
    SECTION("normalized 0.0 -> era 0 (MN3005)") {
        REQUIRE(denormEra(0.0) == 0);
    }

    SECTION("normalized 0.333 -> era 1 (MN3007)") {
        REQUIRE(denormEra(0.333) == 1);
    }

    SECTION("normalized 0.667 -> era 2 (MN3205)") {
        REQUIRE(denormEra(0.667) == 2);
    }

    SECTION("normalized 1.0 -> era 3 (SAD1024)") {
        REQUIRE(denormEra(1.0) == 3);
    }

    SECTION("round-trip all eras") {
        for (int era = 0; era <= 3; ++era) {
            double normalized = normEra(era);
            int result = denormEra(normalized);
            REQUIRE(result == era);
        }
    }
}

// ==============================================================================
// Output Level (dB) Tests
// ==============================================================================

TEST_CASE("BBD Output Level normalization", "[params][bbd]") {
    SECTION("normalized 0.0 -> 0 linear (-96dB = silence)") {
        REQUIRE(denormOutputLevel(0.0) == Approx(0.0f));
    }

    SECTION("normalized 0.889 -> 1.0 linear (0dB = unity)") {
        // 0dB normalized = (0+96)/108 = 0.889
        REQUIRE(denormOutputLevel(0.889) == Approx(1.0f).margin(0.01f));
    }

    SECTION("normalized 1.0 -> ~3.98 linear (+12dB)") {
        // +12dB = 10^(12/20) = 3.981
        REQUIRE(denormOutputLevel(1.0) == Approx(3.981f).margin(0.01f));
    }

    SECTION("round-trip: unity gain (1.0)") {
        float original = 1.0f;
        double normalized = normOutputLevel(original);
        float result = denormOutputLevel(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }

    SECTION("round-trip: -6dB (0.5 linear)") {
        float original = 0.5f;
        double normalized = normOutputLevel(original);
        float result = denormOutputLevel(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

// ==============================================================================
// Passthrough Parameter Tests
// ==============================================================================

TEST_CASE("BBD passthrough parameters", "[params][bbd]") {
    SECTION("Modulation Depth is 0-1 passthrough") {
        REQUIRE(denormModDepth(0.0) == Approx(0.0f));
        REQUIRE(denormModDepth(0.5) == Approx(0.5f));
        REQUIRE(denormModDepth(1.0) == Approx(1.0f));
    }

    SECTION("Age is 0-1 passthrough") {
        REQUIRE(denormAge(0.0) == Approx(0.0f));
        REQUIRE(denormAge(0.5) == Approx(0.5f));
        REQUIRE(denormAge(1.0) == Approx(1.0f));
    }

    SECTION("Mix is 0-1 passthrough") {
        REQUIRE(denormMix(0.0) == Approx(0.0f));
        REQUIRE(denormMix(0.5) == Approx(0.5f));
        REQUIRE(denormMix(1.0) == Approx(1.0f));
    }
}

// ==============================================================================
// Edge Cases
// ==============================================================================

TEST_CASE("BBD parameter edge cases", "[params][bbd]") {
    SECTION("Output level at exactly -96dB boundary") {
        // Just above -96dB should produce non-zero
        double justAbove = 0.001;
        REQUIRE(denormOutputLevel(justAbove) > 0.0f);
    }

    SECTION("Era rounding at boundaries") {
        // Test that values near boundaries round correctly
        REQUIRE(denormEra(0.16) == 0);  // Should round to 0
        REQUIRE(denormEra(0.17) == 1);  // Should round to 1
        REQUIRE(denormEra(0.49) == 1);  // Should round to 1
        REQUIRE(denormEra(0.50) == 2);  // Should round to 2
    }
}
