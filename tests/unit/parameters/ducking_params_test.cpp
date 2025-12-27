// ==============================================================================
// Ducking Delay Parameters Unit Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// Ducking Enabled: boolean
bool denormDuckingEnabled(double normalized) {
    return normalized >= 0.5;
}

// Threshold: -60 to 0 dB
float denormThreshold(double normalized) {
    return static_cast<float>(-60.0 + normalized * 60.0);
}

double normThreshold(float dB) {
    return static_cast<double>((dB + 60.0f) / 60.0f);
}

// Duck Amount: 0-100%
float denormDuckAmount(double normalized) {
    return static_cast<float>(normalized * 100.0);
}

// Attack Time: 0.1-100ms
float denormAttackTime(double normalized) {
    return static_cast<float>(0.1 + normalized * 99.9);
}

double normAttackTime(float ms) {
    return static_cast<double>((ms - 0.1f) / 99.9f);
}

// Release Time: 10-2000ms
float denormReleaseTime(double normalized) {
    return static_cast<float>(10.0 + normalized * 1990.0);
}

double normReleaseTime(float ms) {
    return static_cast<double>((ms - 10.0f) / 1990.0f);
}

// Hold Time: 0-500ms
float denormHoldTime(double normalized) {
    return static_cast<float>(normalized * 500.0);
}

// Duck Target: 0-2 discrete
int denormDuckTarget(double normalized) {
    return static_cast<int>(normalized * 2.0 + 0.5);
}

double normDuckTarget(int target) {
    return static_cast<double>(target) / 2.0;
}

// Sidechain Filter Cutoff: 20-500Hz
float denormSCFilterCutoff(double normalized) {
    return static_cast<float>(20.0 + normalized * 480.0);
}

double normSCFilterCutoff(float hz) {
    return static_cast<double>((hz - 20.0f) / 480.0f);
}

// Delay Time: 10-5000ms
float denormDelayTime(double normalized) {
    return static_cast<float>(10.0 + normalized * 4990.0);
}

// Feedback: 0-120%
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 120.0);
}

// Dry/Wet: 0-100%
float denormDryWet(double normalized) {
    return static_cast<float>(normalized * 100.0);
}

// Output Gain: -96 to +6 dB (stored as dB!)
float denormOutputGain(double normalized) {
    return static_cast<float>(-96.0 + normalized * 102.0);
}

double normOutputGain(float dB) {
    return static_cast<double>((dB + 96.0f) / 102.0f);
}

} // anonymous namespace

TEST_CASE("Ducking Enabled normalization", "[params][ducking]") {
    SECTION("normalized 0.0 -> false") {
        REQUIRE(denormDuckingEnabled(0.0) == false);
    }
    SECTION("normalized 0.5 -> true") {
        REQUIRE(denormDuckingEnabled(0.5) == true);
    }
}

TEST_CASE("Ducking Threshold normalization", "[params][ducking]") {
    SECTION("normalized 0.0 -> -60dB") {
        REQUIRE(denormThreshold(0.0) == Approx(-60.0f));
    }
    SECTION("normalized 0.5 -> -30dB (default)") {
        REQUIRE(denormThreshold(0.5) == Approx(-30.0f));
    }
    SECTION("normalized 1.0 -> 0dB") {
        REQUIRE(denormThreshold(1.0) == Approx(0.0f));
    }
    SECTION("round-trip: -30dB") {
        float original = -30.0f;
        double normalized = normThreshold(original);
        float result = denormThreshold(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

TEST_CASE("Ducking Attack Time normalization", "[params][ducking]") {
    SECTION("normalized 0.0 -> 0.1ms") {
        REQUIRE(denormAttackTime(0.0) == Approx(0.1f));
    }
    SECTION("normalized 1.0 -> 100ms") {
        REQUIRE(denormAttackTime(1.0) == Approx(100.0f));
    }
    SECTION("round-trip: 10ms (default)") {
        float original = 10.0f;
        double normalized = normAttackTime(original);
        float result = denormAttackTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Ducking Release Time normalization", "[params][ducking]") {
    SECTION("normalized 0.0 -> 10ms") {
        REQUIRE(denormReleaseTime(0.0) == Approx(10.0f));
    }
    SECTION("normalized 1.0 -> 2000ms") {
        REQUIRE(denormReleaseTime(1.0) == Approx(2000.0f));
    }
    SECTION("round-trip: 200ms (default)") {
        float original = 200.0f;
        double normalized = normReleaseTime(original);
        float result = denormReleaseTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Ducking Hold Time normalization", "[params][ducking]") {
    SECTION("normalized 0.0 -> 0ms") {
        REQUIRE(denormHoldTime(0.0) == Approx(0.0f));
    }
    SECTION("normalized 0.1 -> 50ms (default)") {
        REQUIRE(denormHoldTime(0.1) == Approx(50.0f));
    }
    SECTION("normalized 1.0 -> 500ms") {
        REQUIRE(denormHoldTime(1.0) == Approx(500.0f));
    }
}

TEST_CASE("Ducking Duck Target normalization", "[params][ducking]") {
    SECTION("round-trip all targets") {
        for (int target = 0; target <= 2; ++target) {
            double normalized = normDuckTarget(target);
            int result = denormDuckTarget(normalized);
            REQUIRE(result == target);
        }
    }
    SECTION("boundary values") {
        REQUIRE(denormDuckTarget(0.0) == 0);   // Output
        REQUIRE(denormDuckTarget(0.5) == 1);   // Feedback
        REQUIRE(denormDuckTarget(1.0) == 2);   // Both
    }
}

TEST_CASE("Ducking Sidechain Filter Cutoff normalization", "[params][ducking]") {
    SECTION("normalized 0.0 -> 20Hz") {
        REQUIRE(denormSCFilterCutoff(0.0) == Approx(20.0f));
    }
    SECTION("normalized 1.0 -> 500Hz") {
        REQUIRE(denormSCFilterCutoff(1.0) == Approx(500.0f));
    }
    SECTION("round-trip: 80Hz (default)") {
        float original = 80.0f;
        double normalized = normSCFilterCutoff(original);
        float result = denormSCFilterCutoff(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Ducking Output Gain normalization (dB)", "[params][ducking]") {
    SECTION("normalized 0.0 -> -96dB") {
        REQUIRE(denormOutputGain(0.0) == Approx(-96.0f));
    }
    SECTION("normalized 0.941 -> ~0dB (default)") {
        REQUIRE(denormOutputGain(0.941) == Approx(0.0f).margin(0.1f));
    }
    SECTION("normalized 1.0 -> +6dB") {
        REQUIRE(denormOutputGain(1.0) == Approx(6.0f));
    }
    SECTION("round-trip: 0dB") {
        float original = 0.0f;
        double normalized = normOutputGain(original);
        float result = denormOutputGain(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

TEST_CASE("Ducking percentage parameters", "[params][ducking]") {
    SECTION("Duck Amount 0-100%") {
        REQUIRE(denormDuckAmount(0.0) == Approx(0.0f));
        REQUIRE(denormDuckAmount(0.5) == Approx(50.0f));
        REQUIRE(denormDuckAmount(1.0) == Approx(100.0f));
    }
    SECTION("Feedback 0-120%") {
        REQUIRE(denormFeedback(0.0) == Approx(0.0f));
        REQUIRE(denormFeedback(0.5) == Approx(60.0f));
        REQUIRE(denormFeedback(1.0) == Approx(120.0f));
    }
    SECTION("Dry/Wet 0-100%") {
        REQUIRE(denormDryWet(0.0) == Approx(0.0f));
        REQUIRE(denormDryWet(0.5) == Approx(50.0f));
        REQUIRE(denormDryWet(1.0) == Approx(100.0f));
    }
}
