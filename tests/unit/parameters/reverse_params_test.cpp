// ==============================================================================
// Reverse Delay Parameters Unit Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// Chunk Size: 10-2000ms
float denormChunkSize(double normalized) {
    return static_cast<float>(10.0 + normalized * 1990.0);
}

double normChunkSize(float ms) {
    return static_cast<double>((ms - 10.0f) / 1990.0f);
}

// Crossfade: 0-100%
float denormCrossfade(double normalized) {
    return static_cast<float>(normalized * 100.0);
}

// Playback Mode: 0-2 discrete
int denormPlaybackMode(double normalized) {
    return static_cast<int>(normalized * 2.0 + 0.5);
}

double normPlaybackMode(int mode) {
    return static_cast<double>(mode) / 2.0;
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

// Filter Cutoff: 20-20000Hz (logarithmic)
float denormFilterCutoff(double normalized) {
    return static_cast<float>(20.0 * std::pow(1000.0, normalized));
}

double normFilterCutoff(float hz) {
    return std::log(hz / 20.0f) / std::log(1000.0f);
}

// Filter Type: 0-2 discrete
int denormFilterType(double normalized) {
    return static_cast<int>(normalized * 2.0 + 0.5);
}

// Dry/Wet: 0-1 passthrough
float denormDryWet(double normalized) {
    return static_cast<float>(normalized);
}

// Output Gain: -96 to +6 dB -> linear
float denormOutputGain(double normalized) {
    double dB = -96.0 + normalized * 102.0;
    double linear = (dB <= -96.0) ? 0.0 : std::pow(10.0, dB / 20.0);
    return static_cast<float>(linear);
}

} // anonymous namespace

TEST_CASE("Reverse Chunk Size normalization", "[params][reverse]") {
    SECTION("normalized 0.0 -> 10ms") {
        REQUIRE(denormChunkSize(0.0) == Approx(10.0f));
    }
    SECTION("normalized 1.0 -> 2000ms") {
        REQUIRE(denormChunkSize(1.0) == Approx(2000.0f));
    }
    SECTION("round-trip: 500ms (default)") {
        float original = 500.0f;
        double normalized = normChunkSize(original);
        float result = denormChunkSize(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Reverse Playback Mode normalization", "[params][reverse]") {
    SECTION("round-trip all modes") {
        for (int mode = 0; mode <= 2; ++mode) {
            double normalized = normPlaybackMode(mode);
            int result = denormPlaybackMode(normalized);
            REQUIRE(result == mode);
        }
    }
    SECTION("boundary values") {
        REQUIRE(denormPlaybackMode(0.0) == 0);   // FullReverse
        REQUIRE(denormPlaybackMode(0.5) == 1);   // Alternating
        REQUIRE(denormPlaybackMode(1.0) == 2);   // Random
    }
}

TEST_CASE("Reverse Filter Cutoff normalization (logarithmic)", "[params][reverse]") {
    SECTION("normalized 0.0 -> 20Hz") {
        REQUIRE(denormFilterCutoff(0.0) == Approx(20.0f));
    }
    SECTION("normalized 0.5 -> ~632Hz (geometric midpoint)") {
        REQUIRE(denormFilterCutoff(0.5) == Approx(632.5f).margin(1.0f));
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

TEST_CASE("Reverse Filter Type normalization", "[params][reverse]") {
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

TEST_CASE("Reverse Output Gain normalization", "[params][reverse]") {
    SECTION("normalized 0.0 -> 0 linear (-96dB)") {
        REQUIRE(denormOutputGain(0.0) == Approx(0.0f));
    }
    SECTION("normalized 0.941 -> ~1.0 linear (0dB)") {
        REQUIRE(denormOutputGain(0.941) == Approx(1.0f).margin(0.02f));
    }
    SECTION("normalized 1.0 -> ~2.0 linear (+6dB)") {
        REQUIRE(denormOutputGain(1.0) == Approx(1.995f).margin(0.01f));
    }
}

TEST_CASE("Reverse passthrough parameters", "[params][reverse]") {
    SECTION("Dry/Wet is 0-1 passthrough") {
        REQUIRE(denormDryWet(0.0) == Approx(0.0f));
        REQUIRE(denormDryWet(0.5) == Approx(0.5f));
        REQUIRE(denormDryWet(1.0) == Approx(1.0f));
    }
}
