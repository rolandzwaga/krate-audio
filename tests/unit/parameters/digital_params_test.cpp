// ==============================================================================
// Digital Delay Parameters Unit Tests
// ==============================================================================
// Tests normalization accuracy and formula correctness for Digital delay parameters.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// ==============================================================================
// Normalization Formulas (extracted from digital_params.h)
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

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

double normFeedback(float feedback) {
    return static_cast<double>(feedback / 1.2f);
}

// Limiter Character: 0-2 discrete
int denormLimiterCharacter(double normalized) {
    return static_cast<int>(normalized * 2.0 + 0.5);
}

double normLimiterCharacter(int limiter) {
    return static_cast<double>(limiter) / 2.0;
}

// Era: 0-2 discrete
int denormEra(double normalized) {
    return static_cast<int>(normalized * 2.0 + 0.5);
}

double normEra(int era) {
    return static_cast<double>(era) / 2.0;
}

// Age: 0-1 (passthrough)
float denormAge(double normalized) {
    return static_cast<float>(normalized);
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

// Mod Waveform: 0-5 discrete
int denormModWaveform(double normalized) {
    return static_cast<int>(normalized * 5.0 + 0.5);
}

double normModWaveform(int waveform) {
    return static_cast<double>(waveform) / 5.0;
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

TEST_CASE("Digital Delay Time normalization", "[params][digital]") {
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

TEST_CASE("Digital Time Mode normalization", "[params][digital]") {
    SECTION("normalized 0.0 -> Free (0)") {
        REQUIRE(denormTimeMode(0.0) == 0);
    }

    SECTION("normalized 0.49 -> Free (0)") {
        REQUIRE(denormTimeMode(0.49) == 0);
    }

    SECTION("normalized 0.5 -> Synced (1)") {
        REQUIRE(denormTimeMode(0.5) == 1);
    }

    SECTION("normalized 1.0 -> Synced (1)") {
        REQUIRE(denormTimeMode(1.0) == 1);
    }
}

TEST_CASE("Digital Note Value normalization", "[params][digital]") {
    SECTION("round-trip all note values") {
        for (int note = 0; note <= 9; ++note) {
            double normalized = normNoteValue(note);
            int result = denormNoteValue(normalized);
            REQUIRE(result == note);
        }
    }

    SECTION("boundary values") {
        REQUIRE(denormNoteValue(0.0) == 0);   // 1/32
        REQUIRE(denormNoteValue(1.0) == 9);   // 1/1
    }
}

TEST_CASE("Digital Limiter Character normalization", "[params][digital]") {
    SECTION("round-trip all limiter modes") {
        for (int limiter = 0; limiter <= 2; ++limiter) {
            double normalized = normLimiterCharacter(limiter);
            int result = denormLimiterCharacter(normalized);
            REQUIRE(result == limiter);
        }
    }
}

TEST_CASE("Digital Era normalization", "[params][digital]") {
    SECTION("round-trip all eras") {
        for (int era = 0; era <= 2; ++era) {
            double normalized = normEra(era);
            int result = denormEra(normalized);
            REQUIRE(result == era);
        }
    }
}

TEST_CASE("Digital Mod Waveform normalization", "[params][digital]") {
    SECTION("round-trip all waveforms") {
        for (int waveform = 0; waveform <= 5; ++waveform) {
            double normalized = normModWaveform(waveform);
            int result = denormModWaveform(normalized);
            REQUIRE(result == waveform);
        }
    }
}

// ==============================================================================
// Continuous Parameter Tests
// ==============================================================================

TEST_CASE("Digital Feedback normalization", "[params][digital]") {
    SECTION("normalized 0.0 -> 0.0") {
        REQUIRE(denormFeedback(0.0) == Approx(0.0f));
    }

    SECTION("normalized 1.0 -> 1.2 (120%)") {
        REQUIRE(denormFeedback(1.0) == Approx(1.2f));
    }

    SECTION("round-trip: 0.4 (40% default)") {
        float original = 0.4f;
        double normalized = normFeedback(original);
        float result = denormFeedback(normalized);
        REQUIRE(result == Approx(original).margin(0.001f));
    }
}

TEST_CASE("Digital Mod Rate normalization", "[params][digital]") {
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

TEST_CASE("Digital Output Level normalization", "[params][digital]") {
    SECTION("normalized 0.0 -> 0 linear (-96dB)") {
        REQUIRE(denormOutputLevel(0.0) == Approx(0.0f));
    }

    SECTION("normalized 0.889 -> 1.0 linear (0dB)") {
        REQUIRE(denormOutputLevel(0.889) == Approx(1.0f).margin(0.01f));
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
// Passthrough Tests
// ==============================================================================

TEST_CASE("Digital passthrough parameters", "[params][digital]") {
    SECTION("Age is 0-1 passthrough") {
        REQUIRE(denormAge(0.0) == Approx(0.0f));
        REQUIRE(denormAge(0.5) == Approx(0.5f));
        REQUIRE(denormAge(1.0) == Approx(1.0f));
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
