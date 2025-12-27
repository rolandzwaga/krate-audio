// ==============================================================================
// Granular Delay Parameters Unit Tests
// ==============================================================================
// Tests normalization accuracy and formula correctness for Granular delay parameters.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// ==============================================================================
// Normalization Formulas (extracted from granular_params.h)
// ==============================================================================

// Grain Size: 10-500ms
float denormGrainSize(double normalized) {
    return static_cast<float>(10.0 + normalized * 490.0);
}

double normGrainSize(float ms) {
    return static_cast<double>((ms - 10.0f) / 490.0f);
}

// Density: 1-100 grains/sec
float denormDensity(double normalized) {
    return static_cast<float>(1.0 + normalized * 99.0);
}

double normDensity(float grainsSec) {
    return static_cast<double>((grainsSec - 1.0f) / 99.0f);
}

// Delay Time: 0-2000ms
float denormDelayTime(double normalized) {
    return static_cast<float>(normalized * 2000.0);
}

double normDelayTime(float ms) {
    return static_cast<double>(ms / 2000.0f);
}

// Pitch: -24 to +24 semitones
float denormPitch(double normalized) {
    return static_cast<float>(-24.0 + normalized * 48.0);
}

double normPitch(float semitones) {
    return static_cast<double>((semitones + 24.0f) / 48.0f);
}

// Spray parameters: 0-1 (passthrough)
float denormSpray(double normalized) {
    return static_cast<float>(normalized);
}

// Freeze: boolean
bool denormFreeze(double normalized) {
    return normalized >= 0.5;
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

double normFeedback(float feedback) {
    return static_cast<double>(feedback / 1.2f);
}

// Dry/Wet: 0-1 (passthrough)
float denormDryWet(double normalized) {
    return static_cast<float>(normalized);
}

// Output Gain: -96 to +6 dB
float denormOutputGain(double normalized) {
    return static_cast<float>(-96.0 + normalized * 102.0);
}

double normOutputGain(float dB) {
    return static_cast<double>((dB + 96.0f) / 102.0f);
}

// Envelope Type: 0-3 discrete
int denormEnvelopeType(double normalized) {
    return static_cast<int>(normalized * 3.0 + 0.5);
}

double normEnvelopeType(int type) {
    return static_cast<double>(type) / 3.0;
}

} // anonymous namespace

// ==============================================================================
// Grain Size Tests
// ==============================================================================

TEST_CASE("Granular Grain Size normalization", "[params][granular]") {
    SECTION("normalized 0.0 -> 10ms (minimum)") {
        REQUIRE(denormGrainSize(0.0) == Approx(10.0f));
    }

    SECTION("normalized 0.5 -> 255ms (midpoint)") {
        REQUIRE(denormGrainSize(0.5) == Approx(255.0f));
    }

    SECTION("normalized 1.0 -> 500ms (maximum)") {
        REQUIRE(denormGrainSize(1.0) == Approx(500.0f));
    }

    SECTION("round-trip: 100ms (default)") {
        float original = 100.0f;
        double normalized = normGrainSize(original);
        float result = denormGrainSize(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Density Tests
// ==============================================================================

TEST_CASE("Granular Density normalization", "[params][granular]") {
    SECTION("normalized 0.0 -> 1 grain/sec (minimum)") {
        REQUIRE(denormDensity(0.0) == Approx(1.0f));
    }

    SECTION("normalized 0.5 -> 50.5 grains/sec (midpoint)") {
        REQUIRE(denormDensity(0.5) == Approx(50.5f));
    }

    SECTION("normalized 1.0 -> 100 grains/sec (maximum)") {
        REQUIRE(denormDensity(1.0) == Approx(100.0f));
    }

    SECTION("round-trip: 10 grains/sec (default)") {
        float original = 10.0f;
        double normalized = normDensity(original);
        float result = denormDensity(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Delay Time Tests
// ==============================================================================

TEST_CASE("Granular Delay Time normalization", "[params][granular]") {
    SECTION("normalized 0.0 -> 0ms (minimum)") {
        REQUIRE(denormDelayTime(0.0) == Approx(0.0f));
    }

    SECTION("normalized 0.5 -> 1000ms (midpoint)") {
        REQUIRE(denormDelayTime(0.5) == Approx(1000.0f));
    }

    SECTION("normalized 1.0 -> 2000ms (maximum)") {
        REQUIRE(denormDelayTime(1.0) == Approx(2000.0f));
    }

    SECTION("round-trip: 500ms (default)") {
        float original = 500.0f;
        double normalized = normDelayTime(original);
        float result = denormDelayTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Pitch Tests
// ==============================================================================

TEST_CASE("Granular Pitch normalization", "[params][granular]") {
    SECTION("normalized 0.0 -> -24 semitones (minimum)") {
        REQUIRE(denormPitch(0.0) == Approx(-24.0f));
    }

    SECTION("normalized 0.5 -> 0 semitones (default)") {
        REQUIRE(denormPitch(0.5) == Approx(0.0f));
    }

    SECTION("normalized 1.0 -> +24 semitones (maximum)") {
        REQUIRE(denormPitch(1.0) == Approx(24.0f));
    }

    SECTION("round-trip: 0 semitones") {
        float original = 0.0f;
        double normalized = normPitch(original);
        float result = denormPitch(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }

    SECTION("round-trip: +12 semitones (octave up)") {
        float original = 12.0f;
        double normalized = normPitch(original);
        float result = denormPitch(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }

    SECTION("round-trip: -12 semitones (octave down)") {
        float original = -12.0f;
        double normalized = normPitch(original);
        float result = denormPitch(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

// ==============================================================================
// Feedback Tests
// ==============================================================================

TEST_CASE("Granular Feedback normalization", "[params][granular]") {
    SECTION("normalized 0.0 -> 0.0") {
        REQUIRE(denormFeedback(0.0) == Approx(0.0f));
    }

    SECTION("normalized 0.5 -> 0.6 (60%)") {
        REQUIRE(denormFeedback(0.5) == Approx(0.6f));
    }

    SECTION("normalized 1.0 -> 1.2 (120% max)") {
        REQUIRE(denormFeedback(1.0) == Approx(1.2f));
    }

    SECTION("round-trip: 0.0 (default)") {
        float original = 0.0f;
        double normalized = normFeedback(original);
        float result = denormFeedback(normalized);
        REQUIRE(result == Approx(original).margin(0.001f));
    }
}

// ==============================================================================
// Output Gain Tests (dB not linear!)
// ==============================================================================

TEST_CASE("Granular Output Gain normalization (dB)", "[params][granular]") {
    SECTION("normalized 0.0 -> -96dB (minimum)") {
        REQUIRE(denormOutputGain(0.0) == Approx(-96.0f));
    }

    SECTION("normalized 0.941 -> 0dB (default)") {
        // (0+96)/102 = 0.941
        REQUIRE(denormOutputGain(0.941) == Approx(0.0f).margin(0.1f));
    }

    SECTION("normalized 1.0 -> +6dB (maximum)") {
        REQUIRE(denormOutputGain(1.0) == Approx(6.0f));
    }

    SECTION("round-trip: 0dB") {
        float original = 0.0f;
        double normalized = normOutputGain(original);
        float result = denormOutputGain(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

// ==============================================================================
// Envelope Type Tests
// ==============================================================================

TEST_CASE("Granular Envelope Type normalization", "[params][granular]") {
    SECTION("round-trip all envelope types") {
        // 0=Hann, 1=Trapezoid, 2=Sine, 3=Blackman
        for (int type = 0; type <= 3; ++type) {
            double normalized = normEnvelopeType(type);
            int result = denormEnvelopeType(normalized);
            REQUIRE(result == type);
        }
    }

    SECTION("boundary values") {
        REQUIRE(denormEnvelopeType(0.0) == 0);   // Hann
        REQUIRE(denormEnvelopeType(1.0) == 3);   // Blackman
    }
}

// ==============================================================================
// Boolean Tests
// ==============================================================================

TEST_CASE("Granular Freeze normalization", "[params][granular]") {
    SECTION("normalized 0.0 -> false") {
        REQUIRE(denormFreeze(0.0) == false);
    }

    SECTION("normalized 0.49 -> false") {
        REQUIRE(denormFreeze(0.49) == false);
    }

    SECTION("normalized 0.5 -> true") {
        REQUIRE(denormFreeze(0.5) == true);
    }

    SECTION("normalized 1.0 -> true") {
        REQUIRE(denormFreeze(1.0) == true);
    }
}

// ==============================================================================
// Passthrough Tests
// ==============================================================================

TEST_CASE("Granular passthrough parameters", "[params][granular]") {
    SECTION("Pitch Spray is 0-1 passthrough") {
        REQUIRE(denormSpray(0.0) == Approx(0.0f));
        REQUIRE(denormSpray(0.5) == Approx(0.5f));
        REQUIRE(denormSpray(1.0) == Approx(1.0f));
    }

    SECTION("Position Spray is 0-1 passthrough") {
        REQUIRE(denormSpray(0.0) == Approx(0.0f));
        REQUIRE(denormSpray(0.5) == Approx(0.5f));
        REQUIRE(denormSpray(1.0) == Approx(1.0f));
    }

    SECTION("Pan Spray is 0-1 passthrough") {
        REQUIRE(denormSpray(0.0) == Approx(0.0f));
        REQUIRE(denormSpray(0.5) == Approx(0.5f));
        REQUIRE(denormSpray(1.0) == Approx(1.0f));
    }

    SECTION("Reverse Probability is 0-1 passthrough") {
        REQUIRE(denormSpray(0.0) == Approx(0.0f));
        REQUIRE(denormSpray(0.5) == Approx(0.5f));
        REQUIRE(denormSpray(1.0) == Approx(1.0f));
    }

    SECTION("Dry/Wet is 0-1 passthrough") {
        REQUIRE(denormDryWet(0.0) == Approx(0.0f));
        REQUIRE(denormDryWet(0.5) == Approx(0.5f));
        REQUIRE(denormDryWet(1.0) == Approx(1.0f));
    }
}
