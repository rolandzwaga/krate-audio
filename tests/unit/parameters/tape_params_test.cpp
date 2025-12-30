// ==============================================================================
// Tape Delay Parameters Unit Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// Motor Speed: 20-2000ms
float denormMotorSpeed(double normalized) {
    return static_cast<float>(20.0 + normalized * 1980.0);
}

double normMotorSpeed(float ms) {
    return static_cast<double>((ms - 20.0f) / 1980.0f);
}

// Motor Inertia: 100-1000ms
float denormMotorInertia(double normalized) {
    return static_cast<float>(100.0 + normalized * 900.0);
}

double normMotorInertia(float ms) {
    return static_cast<double>((ms - 100.0f) / 900.0f);
}

// Wear/Saturation/Age/Mix: 0-1 passthrough
float denormPassthrough(double normalized) {
    return static_cast<float>(normalized);
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

// Head Level: -96 to +6 dB -> linear
float denormHeadLevel(double normalized) {
    double dB = -96.0 + normalized * 102.0;
    double linear = (dB <= -96.0) ? 0.0 : std::pow(10.0, dB / 20.0);
    return static_cast<float>(linear);
}

// Head Pan: -1 to +1
float denormHeadPan(double normalized) {
    return static_cast<float>(normalized * 2.0 - 1.0);
}

double normHeadPan(float pan) {
    return static_cast<double>((pan + 1.0f) / 2.0f);
}

} // anonymous namespace

TEST_CASE("Tape Motor Speed normalization", "[params][tape]") {
    SECTION("normalized 0.0 -> 20ms") {
        REQUIRE(denormMotorSpeed(0.0) == Approx(20.0f));
    }
    SECTION("normalized 1.0 -> 2000ms") {
        REQUIRE(denormMotorSpeed(1.0) == Approx(2000.0f));
    }
    SECTION("round-trip: 500ms (default)") {
        float original = 500.0f;
        double normalized = normMotorSpeed(original);
        float result = denormMotorSpeed(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Tape Motor Inertia normalization", "[params][tape]") {
    SECTION("normalized 0.0 -> 100ms") {
        REQUIRE(denormMotorInertia(0.0) == Approx(100.0f));
    }
    SECTION("normalized 1.0 -> 1000ms") {
        REQUIRE(denormMotorInertia(1.0) == Approx(1000.0f));
    }
    SECTION("round-trip: 300ms (default)") {
        float original = 300.0f;
        double normalized = normMotorInertia(original);
        float result = denormMotorInertia(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Tape Feedback normalization", "[params][tape]") {
    SECTION("normalized 0.0 -> 0.0") {
        REQUIRE(denormFeedback(0.0) == Approx(0.0f));
    }
    SECTION("normalized 1.0 -> 1.2 (120%)") {
        REQUIRE(denormFeedback(1.0) == Approx(1.2f));
    }
}

TEST_CASE("Tape Head Level (dB to linear)", "[params][tape]") {
    SECTION("normalized 0.0 -> 0 linear (-96dB)") {
        REQUIRE(denormHeadLevel(0.0) == Approx(0.0f));
    }
    SECTION("normalized 0.941 -> ~1.0 linear (0dB)") {
        REQUIRE(denormHeadLevel(0.941) == Approx(1.0f).margin(0.02f));
    }
    SECTION("normalized 1.0 -> ~2.0 linear (+6dB)") {
        REQUIRE(denormHeadLevel(1.0) == Approx(1.995f).margin(0.01f));
    }
}

TEST_CASE("Tape Head Pan normalization", "[params][tape]") {
    SECTION("normalized 0.0 -> -1 (left)") {
        REQUIRE(denormHeadPan(0.0) == Approx(-1.0f));
    }
    SECTION("normalized 0.5 -> 0 (center)") {
        REQUIRE(denormHeadPan(0.5) == Approx(0.0f));
    }
    SECTION("normalized 1.0 -> +1 (right)") {
        REQUIRE(denormHeadPan(1.0) == Approx(1.0f));
    }
    SECTION("round-trip: center") {
        float original = 0.0f;
        double normalized = normHeadPan(original);
        float result = denormHeadPan(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

TEST_CASE("Tape passthrough parameters", "[params][tape]") {
    SECTION("Wear is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.3) == Approx(0.3f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
    SECTION("Saturation is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.5) == Approx(0.5f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
    SECTION("Age is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.3) == Approx(0.3f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
}
