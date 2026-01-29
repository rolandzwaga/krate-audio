// ==============================================================================
// Layer 2: Processor Tests - Sample & Hold Source
// ==============================================================================
// Tests for the SampleHoldSource modulation source.
//
// Reference: specs/008-modulation-system/spec.md (FR-036 to FR-040, SC-017)
// ==============================================================================

#include <krate/dsp/processors/sample_hold_source.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Hold Behavior Tests
// =============================================================================

TEST_CASE("SampleHoldSource holds value between samples", "[processors][sample_hold]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::Random);
    src.setRate(2.0f);  // 2 Hz = hold for ~22050 samples
    src.setSlewTime(0.0f);  // No slew

    // Process past first trigger to get initial value
    for (int i = 0; i < 22100; ++i) {
        src.process();
    }

    float heldValue = src.getCurrentValue();

    // Process several hundred more samples - value should remain held
    bool allSame = true;
    for (int i = 0; i < 1000; ++i) {
        src.process();
        if (std::abs(src.getCurrentValue() - heldValue) > 0.001f) {
            allSame = false;
            break;
        }
    }

    REQUIRE(allSame);
}

// =============================================================================
// Rate Tests
// =============================================================================

TEST_CASE("SampleHoldSource rate controls sampling frequency", "[processors][sample_hold]") {
    auto countChanges = [](float rate, int numSamples) {
        SampleHoldSource src;
        src.prepare(44100.0);
        src.setInputType(SampleHoldInputType::Random);
        src.setRate(rate);
        src.setSlewTime(0.0f);

        int changes = 0;
        float prev = src.getCurrentValue();
        for (int i = 0; i < numSamples; ++i) {
            src.process();
            float val = src.getCurrentValue();
            if (std::abs(val - prev) > 0.001f) {
                ++changes;
                prev = val;
            }
        }
        return changes;
    };

    // Use 2 seconds to give enough time for triggers at slow rates
    int slowChanges = countChanges(2.0f, 88200);   // 2 Hz for 2 seconds = ~4 triggers
    int fastChanges = countChanges(20.0f, 88200);   // 20 Hz for 2 seconds = ~40 triggers

    // Faster rate should produce more value changes
    REQUIRE(fastChanges > slowChanges);

    // At 2 Hz for 2 seconds, we expect approximately 4 changes
    REQUIRE(slowChanges >= 2);
    REQUIRE(slowChanges <= 8);

    // At 20 Hz for 2 seconds, we expect approximately 40 changes
    REQUIRE(fastChanges >= 30);
    REQUIRE(fastChanges <= 50);
}

// =============================================================================
// Slew Tests (SC-017)
// =============================================================================

TEST_CASE("SampleHoldSource slew smooths transitions", "[processors][sample_hold][sc017]") {
    auto measureMaxJump = [](float slewMs) {
        SampleHoldSource src;
        src.prepare(44100.0);
        src.setInputType(SampleHoldInputType::Random);
        src.setRate(10.0f);  // Fast triggers
        src.setSlewTime(slewMs);

        float maxJump = 0.0f;
        float prev = src.getCurrentValue();
        for (int i = 0; i < 44100; ++i) {
            src.process();
            float val = src.getCurrentValue();
            float jump = std::abs(val - prev);
            maxJump = std::max(maxJump, jump);
            prev = val;
        }
        return maxJump;
    };

    float noSlewMaxJump = measureMaxJump(0.0f);
    float slewMaxJump = measureMaxJump(200.0f);

    // With slew, max per-sample jump should be smaller
    REQUIRE(slewMaxJump < noSlewMaxJump);
}

// =============================================================================
// Input Source Tests (FR-037)
// =============================================================================

TEST_CASE("SampleHoldSource Random input produces random values", "[processors][sample_hold]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::Random);
    src.setRate(20.0f);
    src.setSlewTime(0.0f);

    // Collect several held values
    bool hasVariation = false;
    float firstVal = 0.0f;
    bool gotFirst = false;

    for (int i = 0; i < 44100; ++i) {
        src.process();
    }
    firstVal = src.getCurrentValue();
    gotFirst = true;

    for (int i = 0; i < 44100; ++i) {
        src.process();
        if (gotFirst && std::abs(src.getCurrentValue() - firstVal) > 0.1f) {
            hasVariation = true;
            break;
        }
    }

    REQUIRE(hasVariation);
}

TEST_CASE("SampleHoldSource External input returns external level", "[processors][sample_hold]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::External);
    src.setRate(50.0f);  // Fast sampling
    src.setSlewTime(0.0f);
    src.setExternalLevel(0.75f);

    // Process enough to trigger several times
    for (int i = 0; i < 4410; ++i) {
        src.process();
    }

    // Value should be close to the external level (0.75)
    float val = src.getCurrentValue();
    REQUIRE(val == Approx(0.75f).margin(0.05f));
}

// =============================================================================
// Output Range Tests (FR-040)
// =============================================================================

TEST_CASE("SampleHoldSource Random output range is [-1, +1]", "[processors][sample_hold][fr040]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::Random);
    src.setRate(50.0f);
    src.setSlewTime(0.0f);

    auto range = src.getSourceRange();
    REQUIRE(range.first == Approx(-1.0f));
    REQUIRE(range.second == Approx(1.0f));
}

TEST_CASE("SampleHoldSource External output range is [0, +1]", "[processors][sample_hold][fr040]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::External);

    auto range = src.getSourceRange();
    REQUIRE(range.first == Approx(0.0f));
    REQUIRE(range.second == Approx(1.0f));
}

// =============================================================================
// Interface Tests
// =============================================================================

TEST_CASE("SampleHoldSource implements ModulationSource interface", "[processors][sample_hold]") {
    SampleHoldSource src;
    src.prepare(44100.0);

    // Default is Random input
    auto range = src.getSourceRange();
    REQUIRE(range.first == Approx(-1.0f));
    REQUIRE(range.second == Approx(1.0f));

    // Process and verify output is valid
    for (int i = 0; i < 1000; ++i) {
        src.process();
    }
    float val = src.getCurrentValue();
    REQUIRE(val >= -1.0f);
    REQUIRE(val <= 1.0f);
}
