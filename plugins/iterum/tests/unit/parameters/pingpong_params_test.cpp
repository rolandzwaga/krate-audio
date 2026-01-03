// ==============================================================================
// PingPong Delay Parameters Unit Tests
// ==============================================================================
// Tests normalization accuracy and formula correctness for PingPong delay parameters.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// ==============================================================================
// Normalization Formulas (extracted from pingpong_params.h)
// ==============================================================================

// Delay Time: 1-10000ms
float denormDelayTime(double normalized) {
    return static_cast<float>(1.0 + normalized * 9999.0);
}

double normDelayTime(float ms) {
    return static_cast<double>((ms - 1.0f) / 9999.0f);
}

// Time Mode: 0-1 (boolean)
int denormTimeMode(double normalized) {
    return normalized >= 0.5 ? 1 : 0;
}

// Note Value: 0-9 discrete
int denormNoteValue(double normalized) {
    return static_cast<int>(normalized * 9.0 + 0.5);
}

double normNoteValue(int note) {
    return static_cast<double>(note) / 9.0;
}

// L/R Ratio: 0-6 discrete
int denormLRRatio(double normalized) {
    return static_cast<int>(normalized * 6.0 + 0.5);
}

double normLRRatio(int ratio) {
    return static_cast<double>(ratio) / 6.0;
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

double normFeedback(float feedback) {
    return static_cast<double>(feedback / 1.2f);
}

// Cross-Feedback: 0-1 (passthrough)
float denormCrossFeedback(double normalized) {
    return static_cast<float>(normalized);
}

// Width: 0-200%
float denormWidth(double normalized) {
    return static_cast<float>(normalized * 200.0);
}

double normWidth(float width) {
    return static_cast<double>(width / 200.0f);
}

// Mod Depth: 0-1 (passthrough)
float denormModDepth(double normalized) {
    return static_cast<float>(normalized);
}

// Mod Rate: 0.1-10Hz
float denormModRate(double normalized) {
    return static_cast<float>(0.1 + normalized * 9.9);
}

double normModRate(float hz) {
    return static_cast<double>((hz - 0.1f) / 9.9f);
}

// Mix: 0-1 (passthrough)
float denormMix(double normalized) {
    return static_cast<float>(normalized);
}

// Output Level: -120 to +12 dB -> linear (note: different range than others!)
float denormOutputLevel(double normalized) {
    double dB = -120.0 + normalized * 132.0;
    double linear = (dB <= -120.0) ? 0.0 : std::pow(10.0, dB / 20.0);
    return static_cast<float>(linear);
}

double normOutputLevel(float linear) {
    double dB = (linear <= 0.0f) ? -120.0 : 20.0 * std::log10(linear);
    return (dB + 120.0) / 132.0;
}

} // anonymous namespace

// ==============================================================================
// Delay Time Tests
// ==============================================================================

TEST_CASE("PingPong Delay Time normalization", "[params][pingpong]") {
    SECTION("normalized 0.0 -> 1ms (minimum)") {
        REQUIRE(denormDelayTime(0.0) == Approx(1.0f));
    }

    SECTION("normalized 0.5 -> 5000.5ms (midpoint)") {
        REQUIRE(denormDelayTime(0.5) == Approx(5000.5f));
    }

    SECTION("normalized 1.0 -> 10000ms (maximum)") {
        REQUIRE(denormDelayTime(1.0) == Approx(10000.0f));
    }

    SECTION("round-trip: 500ms (default)") {
        float original = 500.0f;
        double normalized = normDelayTime(original);
        float result = denormDelayTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Discrete Parameter Tests
// ==============================================================================

TEST_CASE("PingPong L/R Ratio normalization", "[params][pingpong]") {
    SECTION("round-trip all ratios") {
        // 0=1:1, 1=2:1, 2=3:2, 3=4:3, 4=1:2, 5=2:3, 6=3:4
        for (int ratio = 0; ratio <= 6; ++ratio) {
            double normalized = normLRRatio(ratio);
            int result = denormLRRatio(normalized);
            REQUIRE(result == ratio);
        }
    }

    SECTION("boundary values") {
        REQUIRE(denormLRRatio(0.0) == 0);   // 1:1
        REQUIRE(denormLRRatio(1.0) == 6);   // 3:4
    }
}

TEST_CASE("PingPong Note Value normalization", "[params][pingpong]") {
    SECTION("round-trip all note values") {
        for (int note = 0; note <= 9; ++note) {
            double normalized = normNoteValue(note);
            int result = denormNoteValue(normalized);
            REQUIRE(result == note);
        }
    }
}

// ==============================================================================
// Width Tests (unique to PingPong)
// ==============================================================================

TEST_CASE("PingPong Width normalization", "[params][pingpong]") {
    SECTION("normalized 0.0 -> 0%") {
        REQUIRE(denormWidth(0.0) == Approx(0.0f));
    }

    SECTION("normalized 0.5 -> 100% (default)") {
        REQUIRE(denormWidth(0.5) == Approx(100.0f));
    }

    SECTION("normalized 1.0 -> 200% (max)") {
        REQUIRE(denormWidth(1.0) == Approx(200.0f));
    }

    SECTION("round-trip: 100%") {
        float original = 100.0f;
        double normalized = normWidth(original);
        float result = denormWidth(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Output Level Tests (-120 to +12 dB range)
// ==============================================================================

TEST_CASE("PingPong Output Level normalization", "[params][pingpong]") {
    SECTION("normalized 0.0 -> 0 linear (-120dB)") {
        REQUIRE(denormOutputLevel(0.0) == Approx(0.0f));
    }

    SECTION("normalized 0.909 -> ~1.0 linear (0dB)") {
        // 0dB normalized = (0+120)/132 = 0.909
        REQUIRE(denormOutputLevel(0.909) == Approx(1.0f).margin(0.02f));
    }

    SECTION("normalized 1.0 -> ~3.98 linear (+12dB)") {
        REQUIRE(denormOutputLevel(1.0) == Approx(3.981f).margin(0.01f));
    }

    SECTION("round-trip: unity gain") {
        float original = 1.0f;
        double normalized = normOutputLevel(original);
        float result = denormOutputLevel(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

// ==============================================================================
// Continuous Parameter Tests
// ==============================================================================

TEST_CASE("PingPong Feedback normalization", "[params][pingpong]") {
    SECTION("normalized 0.0 -> 0.0") {
        REQUIRE(denormFeedback(0.0) == Approx(0.0f));
    }

    SECTION("normalized 1.0 -> 1.2 (120%)") {
        REQUIRE(denormFeedback(1.0) == Approx(1.2f));
    }

    SECTION("round-trip: 0.5 (50% default)") {
        float original = 0.5f;
        double normalized = normFeedback(original);
        float result = denormFeedback(normalized);
        REQUIRE(result == Approx(original).margin(0.001f));
    }
}

TEST_CASE("PingPong Mod Rate normalization", "[params][pingpong]") {
    SECTION("normalized 0.0 -> 0.1Hz") {
        REQUIRE(denormModRate(0.0) == Approx(0.1f));
    }

    SECTION("normalized 1.0 -> 10Hz") {
        REQUIRE(denormModRate(1.0) == Approx(10.0f));
    }

    SECTION("round-trip: 1Hz (default)") {
        float original = 1.0f;
        double normalized = normModRate(original);
        float result = denormModRate(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

// ==============================================================================
// Passthrough Tests
// ==============================================================================

TEST_CASE("PingPong passthrough parameters", "[params][pingpong]") {
    SECTION("Cross-Feedback is 0-1 passthrough") {
        REQUIRE(denormCrossFeedback(0.0) == Approx(0.0f));
        REQUIRE(denormCrossFeedback(0.5) == Approx(0.5f));
        REQUIRE(denormCrossFeedback(1.0) == Approx(1.0f));
    }

    SECTION("Mod Depth is 0-1 passthrough") {
        REQUIRE(denormModDepth(0.0) == Approx(0.0f));
        REQUIRE(denormModDepth(0.5) == Approx(0.5f));
        REQUIRE(denormModDepth(1.0) == Approx(1.0f));
    }

    SECTION("Mix is 0-1 passthrough") {
        REQUIRE(denormMix(0.0) == Approx(0.0f));
        REQUIRE(denormMix(0.5) == Approx(0.5f));
        REQUIRE(denormMix(1.0) == Approx(1.0f));
    }
}
