// ==============================================================================
// Shimmer Delay Parameters Unit Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// Delay Time: 10-5000ms
float denormDelayTime(double normalized) {
    return static_cast<float>(10.0 + normalized * 4990.0);
}

double normDelayTime(float ms) {
    return static_cast<double>((ms - 10.0f) / 4990.0f);
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

double normPitchCents(float cents) {
    return static_cast<double>((cents + 100.0f) / 200.0f);
}

// Shimmer Mix: 0-100%
float denormShimmerMix(double normalized) {
    return static_cast<float>(normalized * 100.0);
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

// Diffusion Amount/Size: 0-100%
float denormDiffusion(double normalized) {
    return static_cast<float>(normalized * 100.0);
}

// Filter Cutoff: 20-20000Hz (linear)
float denormFilterCutoff(double normalized) {
    return static_cast<float>(20.0 + normalized * 19980.0);
}

double normFilterCutoff(float hz) {
    return static_cast<double>((hz - 20.0f) / 19980.0f);
}

} // anonymous namespace

TEST_CASE("Shimmer Delay Time normalization", "[params][shimmer]") {
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

TEST_CASE("Shimmer Pitch Semitones normalization", "[params][shimmer]") {
    SECTION("normalized 0.0 -> -24 semitones") {
        REQUIRE(denormPitchSemitones(0.0) == Approx(-24.0f));
    }
    SECTION("normalized 0.5 -> 0 semitones") {
        REQUIRE(denormPitchSemitones(0.5) == Approx(0.0f));
    }
    SECTION("normalized 0.75 -> +12 semitones (default)") {
        REQUIRE(denormPitchSemitones(0.75) == Approx(12.0f));
    }
    SECTION("normalized 1.0 -> +24 semitones") {
        REQUIRE(denormPitchSemitones(1.0) == Approx(24.0f));
    }
    SECTION("round-trip: +12 semitones") {
        float original = 12.0f;
        double normalized = normPitchSemitones(original);
        float result = denormPitchSemitones(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

TEST_CASE("Shimmer Pitch Cents normalization", "[params][shimmer]") {
    SECTION("normalized 0.0 -> -100 cents") {
        REQUIRE(denormPitchCents(0.0) == Approx(-100.0f));
    }
    SECTION("normalized 0.5 -> 0 cents (default)") {
        REQUIRE(denormPitchCents(0.5) == Approx(0.0f));
    }
    SECTION("normalized 1.0 -> +100 cents") {
        REQUIRE(denormPitchCents(1.0) == Approx(100.0f));
    }
    SECTION("round-trip: 0 cents") {
        float original = 0.0f;
        double normalized = normPitchCents(original);
        float result = denormPitchCents(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

TEST_CASE("Shimmer Filter Cutoff normalization (linear)", "[params][shimmer]") {
    SECTION("normalized 0.0 -> 20Hz") {
        REQUIRE(denormFilterCutoff(0.0) == Approx(20.0f));
    }
    SECTION("normalized 1.0 -> 20000Hz") {
        REQUIRE(denormFilterCutoff(1.0) == Approx(20000.0f));
    }
    SECTION("round-trip: 4000Hz (default)") {
        float original = 4000.0f;
        double normalized = normFilterCutoff(original);
        float result = denormFilterCutoff(normalized);
        REQUIRE(result == Approx(original).margin(1.0f));
    }
}

TEST_CASE("Shimmer percentage parameters", "[params][shimmer]") {
    SECTION("Shimmer Mix 0-100%") {
        REQUIRE(denormShimmerMix(0.0) == Approx(0.0f));
        REQUIRE(denormShimmerMix(0.5) == Approx(50.0f));
        REQUIRE(denormShimmerMix(1.0) == Approx(100.0f));
    }
    SECTION("Diffusion Amount 0-100%") {
        REQUIRE(denormDiffusion(0.0) == Approx(0.0f));
        REQUIRE(denormDiffusion(0.5) == Approx(50.0f));
        REQUIRE(denormDiffusion(1.0) == Approx(100.0f));
    }
}
