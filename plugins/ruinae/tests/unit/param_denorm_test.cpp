// ==============================================================================
// Unit Test: Parameter Denormalization
// ==============================================================================
// Verifies that denormalization formulas produce correct real-world values
// for representative parameters from each pack.
//
// Reference: specs/045-plugin-shell/spec.md FR-005, FR-006
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "parameters/env_follower_params.h"
#include "parameters/lfo_params.h"
#include "parameters/pitch_follower_params.h"

using Catch::Approx;

// ==============================================================================
// Extract denormalization formulas (matching the parameter pack handlers)
// ==============================================================================

namespace {

// Master Gain: 0-1 -> 0-2 (linear)
float denormMasterGain(double v) {
    return static_cast<float>(v * 2.0);
}

// Polyphony: 0-1 -> 1-16
int denormPolyphony(double v) {
    return static_cast<int>(v * 15.0 + 1.0 + 0.5);
}

// Filter Cutoff: 0-1 -> 20-20000 Hz (exponential: 20*pow(1000, v))
float denormFilterCutoff(double v) {
    return 20.0f * std::pow(1000.0f, static_cast<float>(v));
}

double normFilterCutoff(float hz) {
    return (hz > 20.0f) ? std::log(hz / 20.0f) / std::log(1000.0f) : 0.0;
}

// Envelope Time: 0-1 -> 0-10000 ms (cubic: v^3 * 10000)
float denormEnvTime(double v) {
    return static_cast<float>(v * v * v * 10000.0);
}

double normEnvTime(float ms) {
    return std::cbrt(static_cast<double>(ms) / 10000.0);
}

// LFO Rate: 0-1 -> 0.01-50 Hz (exponential: 0.01 * pow(5000, v))
float denormLFORate(double v) {
    return static_cast<float>(0.01 * std::pow(5000.0, v));
}

double normLFORate(float hz) {
    return std::log(static_cast<double>(hz) / 0.01) / std::log(5000.0);
}

// Mod Matrix Amount: 0-1 -> -1 to +1 (bipolar)
float denormModAmount(double v) {
    return static_cast<float>(v * 2.0 - 1.0);
}

double normModAmount(float amount) {
    return static_cast<double>((amount + 1.0f) / 2.0f);
}

// Osc Tune: 0-1 -> -24 to +24 semitones (linear bipolar)
float denormOscTune(double v) {
    return static_cast<float>(v * 48.0 - 24.0);
}

double normOscTune(float semitones) {
    return static_cast<double>((semitones + 24.0f) / 48.0f);
}

// Portamento Time: 0-1 -> 0-5000 ms (cubic)
float denormPortaTime(double v) {
    return static_cast<float>(v * v * v * 5000.0);
}

double normPortaTime(float ms) {
    return (ms > 0.0f) ? std::cbrt(static_cast<double>(ms) / 5000.0) : 0.0;
}

// Reverb Pre-Delay: 0-1 -> 0-100 ms (linear)
float denormPreDelay(double v) {
    return static_cast<float>(v * 100.0);
}

// Delay Time: 0-1 -> 1-5000 ms (linear)
float denormDelayTime(double v) {
    return static_cast<float>(1.0 + v * 4999.0);
}

double normDelayTime(float ms) {
    return static_cast<double>((ms - 1.0f) / 4999.0f);
}

// Filter Env Amount: 0-1 -> -48 to +48 semitones (bipolar)
float denormFilterEnvAmt(double v) {
    return static_cast<float>(v * 96.0 - 48.0);
}

} // anonymous namespace

// ==============================================================================
// Master Gain
// ==============================================================================

TEST_CASE("Master Gain denormalization", "[params][denorm]") {
    REQUIRE(denormMasterGain(0.0) == Approx(0.0f));
    REQUIRE(denormMasterGain(0.5) == Approx(1.0f));
    REQUIRE(denormMasterGain(1.0) == Approx(2.0f));
}

// ==============================================================================
// Polyphony
// ==============================================================================

TEST_CASE("Polyphony denormalization", "[params][denorm]") {
    REQUIRE(denormPolyphony(0.0) == 1);
    REQUIRE(denormPolyphony(1.0) == 16);
    // Default: 8 voices
    REQUIRE(denormPolyphony(7.0 / 15.0) == 8);
}

// ==============================================================================
// Filter Cutoff (exponential)
// ==============================================================================

TEST_CASE("Filter Cutoff denormalization", "[params][denorm]") {
    SECTION("Boundary values") {
        REQUIRE(denormFilterCutoff(0.0) == Approx(20.0f).margin(0.1f));
        REQUIRE(denormFilterCutoff(1.0) == Approx(20000.0f).margin(10.0f));
    }

    SECTION("Round-trip 1000 Hz") {
        float original = 1000.0f;
        double normalized = normFilterCutoff(original);
        float result = denormFilterCutoff(normalized);
        REQUIRE(result == Approx(original).margin(1.0f));
    }
}

// ==============================================================================
// Envelope Time (cubic)
// ==============================================================================

TEST_CASE("Envelope Time denormalization", "[params][denorm]") {
    SECTION("Boundary values") {
        REQUIRE(denormEnvTime(0.0) == Approx(0.0f));
        REQUIRE(denormEnvTime(1.0) == Approx(10000.0f));
    }

    SECTION("Round-trip 100ms") {
        float original = 100.0f;
        double normalized = normEnvTime(original);
        float result = denormEnvTime(normalized);
        REQUIRE(result == Approx(original).margin(0.5f));
    }

    SECTION("Fine control at low values (cubic curve)") {
        // At normalized 0.1, should be 10ms (0.1^3 * 10000)
        REQUIRE(denormEnvTime(0.1) == Approx(10.0f).margin(0.1f));
    }
}

// ==============================================================================
// LFO Rate (exponential)
// ==============================================================================

TEST_CASE("LFO Rate denormalization", "[params][denorm]") {
    SECTION("Boundary values") {
        REQUIRE(denormLFORate(0.0) == Approx(0.01f).margin(0.001f));
        REQUIRE(denormLFORate(1.0) == Approx(50.0f).margin(0.5f));
    }

    SECTION("Round-trip 1.0 Hz") {
        float original = 1.0f;
        double normalized = normLFORate(original);
        float result = denormLFORate(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

// ==============================================================================
// Mod Matrix Amount (bipolar)
// ==============================================================================

TEST_CASE("Mod Matrix Amount denormalization", "[params][denorm]") {
    REQUIRE(denormModAmount(0.0) == Approx(-1.0f));
    REQUIRE(denormModAmount(0.5) == Approx(0.0f));
    REQUIRE(denormModAmount(1.0) == Approx(1.0f));

    SECTION("Round-trip 0.75") {
        float original = 0.75f;
        double normalized = normModAmount(original);
        float result = denormModAmount(normalized);
        REQUIRE(result == Approx(original).margin(0.001f));
    }
}

// ==============================================================================
// Osc Tune (bipolar)
// ==============================================================================

TEST_CASE("Osc Tune denormalization", "[params][denorm]") {
    REQUIRE(denormOscTune(0.0) == Approx(-24.0f));
    REQUIRE(denormOscTune(0.5) == Approx(0.0f));
    REQUIRE(denormOscTune(1.0) == Approx(24.0f));

    SECTION("Round-trip 12 semitones") {
        float original = 12.0f;
        double normalized = normOscTune(original);
        float result = denormOscTune(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

// ==============================================================================
// Portamento Time (cubic)
// ==============================================================================

TEST_CASE("Portamento Time denormalization", "[params][denorm]") {
    REQUIRE(denormPortaTime(0.0) == Approx(0.0f));
    REQUIRE(denormPortaTime(1.0) == Approx(5000.0f));

    SECTION("Round-trip 200ms") {
        float original = 200.0f;
        double normalized = normPortaTime(original);
        float result = denormPortaTime(normalized);
        REQUIRE(result == Approx(original).margin(1.0f));
    }
}

// ==============================================================================
// Reverb Pre-Delay (linear)
// ==============================================================================

TEST_CASE("Reverb Pre-Delay denormalization", "[params][denorm]") {
    REQUIRE(denormPreDelay(0.0) == Approx(0.0f));
    REQUIRE(denormPreDelay(0.5) == Approx(50.0f));
    REQUIRE(denormPreDelay(1.0) == Approx(100.0f));
}

// ==============================================================================
// Delay Time (linear)
// ==============================================================================

TEST_CASE("Delay Time denormalization", "[params][denorm]") {
    REQUIRE(denormDelayTime(0.0) == Approx(1.0f));
    REQUIRE(denormDelayTime(1.0) == Approx(5000.0f));

    SECTION("Round-trip 500ms (default)") {
        float original = 500.0f;
        double normalized = normDelayTime(original);
        float result = denormDelayTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Filter Env Amount (bipolar)
// ==============================================================================

TEST_CASE("Filter Env Amount denormalization", "[params][denorm]") {
    REQUIRE(denormFilterEnvAmt(0.0) == Approx(-48.0f));
    REQUIRE(denormFilterEnvAmt(0.5) == Approx(0.0f));
    REQUIRE(denormFilterEnvAmt(1.0) == Approx(48.0f));
}

// =============================================================================
// Logarithmic parameter mappings: pinned values and round-trip stability
// =============================================================================
// Five normalized<->units pairs now delegate to the shared logMap helpers in
// parameter_helpers.h instead of each re-deriving `mn * pow(mx/mn, x)`. Four
// were already evaluated in double and are unchanged; the LFO rate pair moved
// from float to double, so its values are pinned here rather than left to drift
// silently.

TEST_CASE("Logarithmic parameter mappings hit their documented anchors",
          "[params][denorm][logmap]") {
    using namespace Ruinae;

    SECTION("endpoints") {
        CHECK(envFollowerAttackFromNormalized(0.0) == Approx(0.1f));
        CHECK(envFollowerAttackFromNormalized(1.0) == Approx(500.0f));
        CHECK(envFollowerReleaseFromNormalized(0.0) == Approx(1.0f));
        CHECK(envFollowerReleaseFromNormalized(1.0) == Approx(5000.0f));
        CHECK(pitchFollowerMinHzFromNormalized(0.0) == Approx(20.0f));
        CHECK(pitchFollowerMinHzFromNormalized(1.0) == Approx(500.0f));
        CHECK(pitchFollowerMaxHzFromNormalized(0.0) == Approx(200.0f));
        CHECK(pitchFollowerMaxHzFromNormalized(1.0) == Approx(5000.0f));
        CHECK(lfoRateFromNormalized(0.0) == Approx(0.01f));
        CHECK(lfoRateFromNormalized(1.0) == Approx(50.0f));
    }

    SECTION("documented defaults") {
        // The comments in each module quote these; they are what a host shows
        // when a fresh preset is loaded.
        CHECK(envFollowerAttackToNormalized(10.0f) == Approx(0.5406).margin(1e-3));
        CHECK(envFollowerReleaseToNormalized(100.0f) == Approx(0.5406).margin(1e-3));
        CHECK(pitchFollowerMinHzToNormalized(80.0f) == Approx(0.4307).margin(1e-3));
        CHECK(pitchFollowerMaxHzToNormalized(2000.0f) == Approx(0.7153).margin(1e-3));
    }

    SECTION("round trip") {
        for (double n : {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0}) {
            INFO("normalized " << n);
            CHECK(envFollowerAttackToNormalized(envFollowerAttackFromNormalized(n))
                  == Approx(n).margin(1e-6));
            CHECK(envFollowerReleaseToNormalized(envFollowerReleaseFromNormalized(n))
                  == Approx(n).margin(1e-6));
            CHECK(pitchFollowerMinHzToNormalized(pitchFollowerMinHzFromNormalized(n))
                  == Approx(n).margin(1e-6));
            CHECK(pitchFollowerMaxHzToNormalized(pitchFollowerMaxHzFromNormalized(n))
                  == Approx(n).margin(1e-6));
            CHECK(lfoRateToNormalized(lfoRateFromNormalized(n))
                  == Approx(n).margin(1e-6));
        }
    }

    SECTION("LFO rate: values pinned across the float-to-double move") {
        CHECK(lfoRateFromNormalized(0.25) == Approx(0.0840896f).margin(1e-5f));
        CHECK(lfoRateFromNormalized(0.5) == Approx(0.7071068f).margin(1e-5f));
        CHECK(lfoRateFromNormalized(0.75) == Approx(5.9460354f).margin(1e-5f));
    }
}
