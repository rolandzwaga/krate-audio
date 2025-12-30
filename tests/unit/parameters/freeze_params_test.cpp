// ==============================================================================
// Freeze Mode Parameters Unit Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// Freeze Enabled: boolean
bool denormFreezeEnabled(double normalized) {
    return normalized >= 0.5;
}

// Delay Time: 10-5000ms
float denormDelayTime(double normalized) {
    return static_cast<float>(10.0 + normalized * 4990.0);
}

double normDelayTime(float ms) {
    return static_cast<double>((ms - 10.0f) / 4990.0f);
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

// Pitch Semitones: -24 to +24
float denormPitchSemitones(double normalized) {
    return static_cast<float>(-24.0 + normalized * 48.0);
}

double normPitchSemitones(float semitones) {
    return static_cast<double>((semitones + 24.0f) / 48.0f);
}

// Pitch Cents: -100 to +100
float denormPitchCents(double normalized) {
    return static_cast<float>(-100.0 + normalized * 200.0);
}

// Passthrough params: 0-1
float denormPassthrough(double normalized) {
    return static_cast<float>(normalized);
}

// Filter Type: 0-2 discrete
int denormFilterType(double normalized) {
    return static_cast<int>(normalized * 2.0 + 0.5);
}

// Filter Cutoff: 20-20000Hz (logarithmic)
float denormFilterCutoff(double normalized) {
    return static_cast<float>(20.0 * std::pow(1000.0, normalized));
}

double normFilterCutoff(float hz) {
    return std::log(hz / 20.0f) / std::log(1000.0f);
}

} // anonymous namespace

TEST_CASE("Freeze Enabled normalization", "[params][freeze]") {
    SECTION("normalized 0.0 -> false") {
        REQUIRE(denormFreezeEnabled(0.0) == false);
    }
    SECTION("normalized 0.49 -> false") {
        REQUIRE(denormFreezeEnabled(0.49) == false);
    }
    SECTION("normalized 0.5 -> true") {
        REQUIRE(denormFreezeEnabled(0.5) == true);
    }
    SECTION("normalized 1.0 -> true") {
        REQUIRE(denormFreezeEnabled(1.0) == true);
    }
}

TEST_CASE("Freeze Delay Time normalization", "[params][freeze]") {
    SECTION("normalized 0.0 -> 10ms") {
        REQUIRE(denormDelayTime(0.0) == Approx(10.0f));
    }
    SECTION("normalized 1.0 -> 5000ms") {
        REQUIRE(denormDelayTime(1.0) == Approx(5000.0f));
    }
    SECTION("round-trip: 500ms (default)") {
        float original = 500.0f;
        double normalized = normDelayTime(original);
        float result = denormDelayTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Freeze Pitch Semitones normalization", "[params][freeze]") {
    SECTION("normalized 0.0 -> -24 semitones") {
        REQUIRE(denormPitchSemitones(0.0) == Approx(-24.0f));
    }
    SECTION("normalized 0.5 -> 0 semitones (default)") {
        REQUIRE(denormPitchSemitones(0.5) == Approx(0.0f));
    }
    SECTION("normalized 1.0 -> +24 semitones") {
        REQUIRE(denormPitchSemitones(1.0) == Approx(24.0f));
    }
    SECTION("round-trip: 0 semitones") {
        float original = 0.0f;
        double normalized = normPitchSemitones(original);
        float result = denormPitchSemitones(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

TEST_CASE("Freeze Filter Cutoff normalization (logarithmic)", "[params][freeze]") {
    SECTION("normalized 0.0 -> 20Hz") {
        REQUIRE(denormFilterCutoff(0.0) == Approx(20.0f));
    }
    SECTION("normalized 1.0 -> 20000Hz") {
        REQUIRE(denormFilterCutoff(1.0) == Approx(20000.0f));
    }
    SECTION("round-trip: 1000Hz (default)") {
        float original = 1000.0f;
        double normalized = normFilterCutoff(original);
        float result = denormFilterCutoff(normalized);
        REQUIRE(result == Approx(original).margin(1.0f));
    }
}

TEST_CASE("Freeze Filter Type normalization", "[params][freeze]") {
    SECTION("normalized 0.0 -> LowPass (0)") {
        REQUIRE(denormFilterType(0.0) == 0);
    }
    SECTION("normalized 0.5 -> HighPass (1)") {
        REQUIRE(denormFilterType(0.5) == 1);
    }
    SECTION("normalized 1.0 -> BandPass (2)") {
        REQUIRE(denormFilterType(1.0) == 2);
    }
}

TEST_CASE("Freeze passthrough parameters", "[params][freeze]") {
    SECTION("Shimmer Mix is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.5) == Approx(0.5f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
    SECTION("Decay is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.5) == Approx(0.5f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
    SECTION("Diffusion Amount is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.3) == Approx(0.3f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
    SECTION("Dry/Wet is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.5) == Approx(0.5f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
}
