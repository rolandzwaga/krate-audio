// Layer 3: System Component Tests - Granular Engine
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/systems/granular_engine.h"

#include <array>
#include <cmath>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// GranularEngine Lifecycle Tests
// =============================================================================

TEST_CASE("GranularEngine prepare and reset lifecycle", "[systems][granular-engine][layer3]") {
    GranularEngine engine;

    SECTION("prepare initializes engine") {
        engine.prepare(44100.0);
        REQUIRE(engine.activeGrainCount() == 0);
    }

    SECTION("prepare with custom delay buffer size") {
        engine.prepare(44100.0, 5.0f);  // 5 second buffer
        // Should not crash, just verify initialization works
        REQUIRE(engine.activeGrainCount() == 0);
    }

    SECTION("reset clears all state") {
        engine.prepare(44100.0);
        engine.setDensity(100.0f);  // High density for quick triggering
        engine.seed(42);

        // Process to trigger some grains
        float outL, outR;
        for (int i = 0; i < 1000; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
        }

        size_t activeBeforeReset = engine.activeGrainCount();
        REQUIRE(activeBeforeReset > 0);

        engine.reset();
        REQUIRE(engine.activeGrainCount() == 0);
    }
}

// =============================================================================
// Parameter Setting Tests
// =============================================================================

TEST_CASE("GranularEngine parameter clamping", "[systems][granular-engine][layer3]") {
    GranularEngine engine;
    engine.prepare(44100.0);

    SECTION("grain size is clamped to valid range") {
        engine.setGrainSize(5.0f);   // Below min (10ms)
        engine.setGrainSize(1000.0f); // Above max (500ms)
        // Should not crash; values should be internally clamped
    }

    SECTION("density is clamped to valid range") {
        engine.setDensity(0.0f);    // Below min (1)
        engine.setDensity(200.0f);  // Above max (100)
    }

    SECTION("pitch is clamped to valid range") {
        engine.setPitch(-48.0f);   // Below min (-24)
        engine.setPitch(48.0f);    // Above max (+24)
    }

    SECTION("spray amounts are clamped 0-1") {
        engine.setPitchSpray(-0.5f);
        engine.setPitchSpray(1.5f);
        engine.setPositionSpray(-0.5f);
        engine.setPositionSpray(1.5f);
        engine.setPanSpray(-0.5f);
        engine.setPanSpray(1.5f);
    }

    SECTION("reverse probability is clamped 0-1") {
        engine.setReverseProbability(-0.5f);
        engine.setReverseProbability(1.5f);
    }
}

// =============================================================================
// Grain Triggering Tests
// =============================================================================

TEST_CASE("GranularEngine triggers grains based on density", "[systems][granular-engine][layer3]") {
    GranularEngine engine;
    engine.prepare(44100.0);
    engine.seed(42);

    SECTION("low density produces fewer grains") {
        engine.setDensity(5.0f);  // 5 grains/sec
        engine.reset();

        float outL, outR;
        int maxActiveGrains = 0;

        // Process 1 second of audio
        for (int i = 0; i < 44100; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
            maxActiveGrains = std::max(maxActiveGrains,
                                       static_cast<int>(engine.activeGrainCount()));
        }

        // With 5 grains/sec and 100ms grains, expect ~0.5 concurrent
        REQUIRE(maxActiveGrains < 10);
    }

    SECTION("high density produces more grains") {
        engine.setDensity(100.0f);  // 100 grains/sec
        engine.reset();

        float outL, outR;
        int maxActiveGrains = 0;

        // Process 1 second of audio
        for (int i = 0; i < 44100; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
            maxActiveGrains = std::max(maxActiveGrains,
                                       static_cast<int>(engine.activeGrainCount()));
        }

        // With 100 grains/sec and 100ms grains, expect ~10 concurrent
        REQUIRE(maxActiveGrains >= 5);
    }
}

// =============================================================================
// Audio Processing Tests
// =============================================================================

TEST_CASE("GranularEngine audio processing", "[systems][granular-engine][layer3]") {
    GranularEngine engine;
    engine.prepare(44100.0);
    engine.seed(12345);

    SECTION("produces output when grains are active") {
        engine.setDensity(50.0f);  // Moderate density
        engine.setPosition(10.0f); // Short delay to avoid reading zeros
        engine.reset();

        // Fill delay buffer with signal
        float outL, outR;
        for (int i = 0; i < 4410; ++i) {  // 100ms
            engine.process(0.5f, 0.5f, outL, outR);
        }

        // Check if any output was produced
        bool anyOutput = false;
        for (int i = 0; i < 4410; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
            if (std::abs(outL) > 0.001f || std::abs(outR) > 0.001f) {
                anyOutput = true;
            }
        }

        REQUIRE(anyOutput);
    }

    SECTION("produces silence with no input and grains completed") {
        engine.setDensity(1.0f);  // Low density
        engine.reset();

        // Process some silence
        float outL, outR;
        for (int i = 0; i < 88200; ++i) {  // 2 seconds of silence
            engine.process(0.0f, 0.0f, outL, outR);
        }

        // After silence, output should be near zero
        // (grains reading from silent buffer)
        float sumAbsOutput = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            engine.process(0.0f, 0.0f, outL, outR);
            sumAbsOutput += std::abs(outL) + std::abs(outR);
        }

        REQUIRE(sumAbsOutput < 0.01f);
    }
}

// =============================================================================
// Freeze Mode Tests
// =============================================================================

TEST_CASE("GranularEngine freeze mode", "[systems][granular-engine][layer3][freeze]") {
    GranularEngine engine;
    engine.prepare(44100.0);
    engine.seed(42);

    SECTION("freeze disables buffer writing") {
        engine.setDensity(50.0f);
        engine.setPosition(50.0f);  // 50ms delay
        engine.reset();

        // Fill buffer with signal
        float outL, outR;
        for (int i = 0; i < 4410; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
        }

        // Enable freeze
        engine.setFreeze(true);
        REQUIRE(engine.isFrozen());

        // Now send different signal (zeros) - buffer should preserve old content
        for (int i = 0; i < 4410; ++i) {
            engine.process(0.0f, 0.0f, outL, outR);
        }

        // Grains should still read from frozen buffer content
        bool anyOutput = false;
        for (int i = 0; i < 4410; ++i) {
            engine.process(0.0f, 0.0f, outL, outR);
            if (std::abs(outL) > 0.001f || std::abs(outR) > 0.001f) {
                anyOutput = true;
            }
        }

        REQUIRE(anyOutput);  // Should still produce output from frozen buffer
    }

    SECTION("unfreeze resumes buffer writing") {
        engine.reset();

        engine.setFreeze(true);
        REQUIRE(engine.isFrozen());

        engine.setFreeze(false);
        REQUIRE_FALSE(engine.isFrozen());
    }
}

// =============================================================================
// Spray/Randomization Tests
// =============================================================================

TEST_CASE("GranularEngine spray parameters add variation", "[systems][granular-engine][layer3]") {
    GranularEngine engine;
    engine.prepare(44100.0);

    SECTION("zero spray produces consistent results") {
        engine.setDensity(50.0f);
        engine.setPitchSpray(0.0f);
        engine.setPositionSpray(0.0f);
        engine.setPanSpray(0.0f);
        engine.setReverseProbability(0.0f);
        engine.seed(42);
        engine.reset();

        float outL, outR;
        std::array<float, 100> outputsL{};

        for (int i = 0; i < 4410; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
        }
        for (size_t i = 0; i < 100; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
            outputsL[i] = outL;
        }

        // With same seed and no spray, first run output recorded
        engine.seed(42);
        engine.reset();

        for (int i = 0; i < 4410; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
        }

        // Second run should match
        for (size_t i = 0; i < 100; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
            REQUIRE(outL == Approx(outputsL[i]).margin(0.0001f));
        }
    }
}

// =============================================================================
// Reproducibility Tests
// =============================================================================

TEST_CASE("GranularEngine seed produces reproducible output", "[systems][granular-engine][layer3]") {
    SECTION("same seed produces same output") {
        GranularEngine engine1;
        GranularEngine engine2;

        engine1.prepare(44100.0);
        engine2.prepare(44100.0);

        engine1.setDensity(25.0f);
        engine2.setDensity(25.0f);

        engine1.seed(12345);
        engine2.seed(12345);
        engine1.reset();
        engine2.reset();

        float out1L, out1R, out2L, out2R;
        bool allMatch = true;

        for (int i = 0; i < 10000; ++i) {
            engine1.process(0.5f, 0.5f, out1L, out1R);
            engine2.process(0.5f, 0.5f, out2L, out2R);

            if (std::abs(out1L - out2L) > 0.0001f ||
                std::abs(out1R - out2R) > 0.0001f) {
                allMatch = false;
                break;
            }
        }

        REQUIRE(allMatch);
    }

    SECTION("different seeds produce different output") {
        GranularEngine engine1;
        GranularEngine engine2;

        engine1.prepare(44100.0);
        engine2.prepare(44100.0);

        // Set same parameters but add spray to make output seed-dependent
        engine1.setDensity(50.0f);  // Higher density
        engine2.setDensity(50.0f);
        engine1.setPitchSpray(0.5f);  // Add spray to see difference
        engine2.setPitchSpray(0.5f);
        engine1.setPositionSpray(0.5f);
        engine2.setPositionSpray(0.5f);
        engine1.setPanSpray(0.5f);
        engine2.setPanSpray(0.5f);
        engine1.setPosition(50.0f);  // Short delay to read from buffer faster
        engine2.setPosition(50.0f);

        engine1.seed(12345);
        engine2.seed(54321);  // Different seed
        engine1.reset();
        engine2.reset();

        float out1L, out1R, out2L, out2R;
        float phase = 0.0f;
        const float phaseIncrement = 440.0f / 44100.0f;  // 440 Hz sine

        // First, fill the delay buffers with audio
        for (int i = 0; i < 10000; ++i) {
            const float input = 0.5f * std::sin(phase * 6.28318f);
            phase += phaseIncrement;
            if (phase >= 1.0f) phase -= 1.0f;

            engine1.process(input, input, out1L, out1R);
            engine2.process(input, input, out2L, out2R);
        }

        // Now compare outputs - should differ due to spray randomization
        bool anyDifferent = false;
        for (int i = 0; i < 20000; ++i) {
            const float input = 0.5f * std::sin(phase * 6.28318f);
            phase += phaseIncrement;
            if (phase >= 1.0f) phase -= 1.0f;

            engine1.process(input, input, out1L, out1R);
            engine2.process(input, input, out2L, out2R);

            if (std::abs(out1L - out2L) > 0.001f ||
                std::abs(out1R - out2R) > 0.001f) {
                anyDifferent = true;
            }
        }

        REQUIRE(anyDifferent);
    }
}

// =============================================================================
// Envelope Type Tests
// =============================================================================

TEST_CASE("GranularEngine envelope type control", "[systems][granular-engine][layer3]") {
    GranularEngine engine;
    engine.prepare(44100.0);

    SECTION("setEnvelopeType does not crash") {
        engine.setEnvelopeType(GrainEnvelopeType::Hann);
        engine.setEnvelopeType(GrainEnvelopeType::Trapezoid);
        engine.setEnvelopeType(GrainEnvelopeType::Blackman);
        engine.setEnvelopeType(GrainEnvelopeType::Sine);

        // Process some samples to ensure no crash
        float outL, outR;
        for (int i = 0; i < 1000; ++i) {
            engine.process(0.5f, 0.5f, outL, outR);
        }
    }
}

