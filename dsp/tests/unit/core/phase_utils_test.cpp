// ==============================================================================
// Layer 0: Core Utility Tests - Phase Accumulator Utilities
// ==============================================================================
// Tests for centralized phase accumulator and utility functions.
// Validates calculatePhaseIncrement, wrapPhase, detectPhaseWrap,
// subsamplePhaseWrapOffset, and PhaseAccumulator behavior.
// (SC-004 through SC-009)
//
// Constitution Compliance:
// - Principle XII: Test-First Development
//
// Reference: specs/013-polyblep-math/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <random>
#include <vector>

#include <krate/dsp/core/phase_utils.h>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// T033: calculatePhaseIncrement (SC-004)
// =============================================================================

TEST_CASE("calculatePhaseIncrement returns correct increment", "[phase_utils][SC-004]") {
    SECTION("440 Hz at 44100 Hz sample rate") {
        double result = calculatePhaseIncrement(440.0f, 44100.0f);
        double expected = 440.0 / 44100.0;
        REQUIRE(result == Approx(expected).margin(1e-6));
    }

    SECTION("1000 Hz at 48000 Hz sample rate") {
        double result = calculatePhaseIncrement(1000.0f, 48000.0f);
        double expected = 1000.0 / 48000.0;
        REQUIRE(result == Approx(expected).margin(1e-6));
    }

    SECTION("0 Hz returns 0") {
        double result = calculatePhaseIncrement(0.0f, 44100.0f);
        REQUIRE(result == 0.0);
    }

    SECTION("High frequency") {
        double result = calculatePhaseIncrement(20000.0f, 44100.0f);
        double expected = 20000.0 / 44100.0;
        REQUIRE(result == Approx(expected).margin(1e-6));
    }
}

// =============================================================================
// T034: calculatePhaseIncrement division-by-zero guard (FR-014)
// =============================================================================

TEST_CASE("calculatePhaseIncrement handles zero sample rate", "[phase_utils][FR-014]") {
    double result = calculatePhaseIncrement(440.0f, 0.0f);
    REQUIRE(result == 0.0);
}

// =============================================================================
// T035: wrapPhase range verification (SC-006)
// =============================================================================

TEST_CASE("wrapPhase wraps all values to [0, 1)", "[phase_utils][SC-006]") {
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(-10.0, 10.0);

    constexpr int kNumTrials = 10000;
    int inRangeCount = 0;

    for (int i = 0; i < kNumTrials; ++i) {
        double input = dist(rng);
        double result = wrapPhase(input);

        bool inRange = (result >= 0.0 && result < 1.0);
        if (inRange) ++inRangeCount;

        INFO("Input: " << input << ", Result: " << result);
        REQUIRE(result >= 0.0);
        REQUIRE(result < 1.0);
    }

    REQUIRE(inRangeCount == kNumTrials);
}

// =============================================================================
// T036: wrapPhase negative handling (FR-016)
// =============================================================================

TEST_CASE("wrapPhase handles negative values correctly", "[phase_utils][FR-016]") {
    SECTION("-0.2 wraps to 0.8") {
        double result = wrapPhase(-0.2);
        REQUIRE(result == Approx(0.8).margin(1e-12));
    }

    SECTION("-1.0 wraps to 0.0") {
        double result = wrapPhase(-1.0);
        REQUIRE(result == Approx(0.0).margin(1e-12));
    }

    SECTION("-3.7 wraps correctly") {
        double result = wrapPhase(-3.7);
        // -3.7 + 4 = 0.3
        REQUIRE(result == Approx(0.3).margin(1e-12));
    }

    SECTION("Already in range") {
        REQUIRE(wrapPhase(0.5) == Approx(0.5).margin(1e-12));
    }

    SECTION("Exactly 0.0") {
        REQUIRE(wrapPhase(0.0) == 0.0);
    }

    SECTION("Exactly 1.0 wraps to 0.0") {
        double result = wrapPhase(1.0);
        REQUIRE(result == Approx(0.0).margin(1e-12));
    }

    SECTION("1.3 wraps to 0.3") {
        double result = wrapPhase(1.3);
        REQUIRE(result == Approx(0.3).margin(1e-12));
    }

    SECTION("Large positive") {
        double result = wrapPhase(5.7);
        REQUIRE(result == Approx(0.7).margin(1e-10));
    }
}

// =============================================================================
// T037: detectPhaseWrap (FR-017)
// =============================================================================

TEST_CASE("detectPhaseWrap detects wraps correctly", "[phase_utils][FR-017]") {
    SECTION("Wrap occurred: current < previous") {
        REQUIRE(detectPhaseWrap(0.01, 0.99) == true);
    }

    SECTION("No wrap: current > previous") {
        REQUIRE(detectPhaseWrap(0.5, 0.4) == false);
    }

    SECTION("No wrap: equal values") {
        REQUIRE(detectPhaseWrap(0.5, 0.5) == false);
    }

    SECTION("Wrap with very small current") {
        REQUIRE(detectPhaseWrap(0.001, 0.999) == true);
    }

    SECTION("No wrap: ascending phase") {
        REQUIRE(detectPhaseWrap(0.8, 0.7) == false);
    }
}

// =============================================================================
// T038: subsamplePhaseWrapOffset (SC-007)
// =============================================================================

TEST_CASE("subsamplePhaseWrapOffset returns correct fractional position", "[phase_utils][SC-007]") {
    SECTION("Basic wrap offset calculation") {
        // Phase was 0.98, increment is 0.05, after advance: unwrapped = 1.03, wrapped = 0.03
        // offset = 0.03 / 0.05 = 0.6
        double offset = subsamplePhaseWrapOffset(0.03, 0.05);
        REQUIRE(offset == Approx(0.6).margin(1e-10));
    }

    SECTION("Wrap right at boundary") {
        // Phase just barely wrapped: phase = 0.001, increment = 0.01
        // offset = 0.001 / 0.01 = 0.1
        double offset = subsamplePhaseWrapOffset(0.001, 0.01);
        REQUIRE(offset == Approx(0.1).margin(1e-10));
    }

    SECTION("Zero increment returns 0") {
        double offset = subsamplePhaseWrapOffset(0.03, 0.0);
        REQUIRE(offset == 0.0);
    }

    SECTION("Offset is in [0, 1) range") {
        // Test multiple combinations
        std::mt19937 rng(123);
        std::uniform_real_distribution<double> phaseDist(0.0, 0.1);
        std::uniform_real_distribution<double> incDist(0.001, 0.1);

        for (int i = 0; i < 1000; ++i) {
            double phase = phaseDist(rng);
            double inc = incDist(rng);
            // Only valid when phase < inc (just wrapped)
            if (phase < inc) {
                double offset = subsamplePhaseWrapOffset(phase, inc);
                INFO("phase=" << phase << " inc=" << inc << " offset=" << offset);
                REQUIRE(offset >= 0.0);
                REQUIRE(offset < 1.0);
            }
        }
    }

    SECTION("Reconstructs original crossing point (SC-007)") {
        // subsamplePhaseWrapOffset returns offset = phase / increment.
        // This is the fraction of the sample interval AFTER the crossing.
        // The crossing happened at fraction (1 - offset) from the start of the sample.
        //
        // Reconstruction: prevPhase + (1 - offset) * increment = 1.0
        // Or equivalently: offset * increment = wrapped_phase (which is the definition)
        //
        // Verify: offset * increment reconstructs the wrapped phase exactly.
        struct TestCase {
            double prevPhase;
            double increment;
        };

        constexpr TestCase cases[] = {
            {.prevPhase = 0.995, .increment = 0.01},    // wraps to 0.005
            {.prevPhase = 0.997, .increment = 0.005},   // wraps to 0.002
            {.prevPhase = 0.9999, .increment = 0.001},  // wraps to 0.0009
            {.prevPhase = 0.998, .increment = 0.009977324263038548}, // 440Hz, wraps to ~0.007977
        };

        for (const auto& tc : cases) {
            double unwrapped = tc.prevPhase + tc.increment;
            REQUIRE(unwrapped >= 1.0); // Ensure wrap actually occurs

            double wrapped = unwrapped - 1.0;
            double offset = subsamplePhaseWrapOffset(wrapped, tc.increment);

            // Verify offset is in [0, 1)
            REQUIRE(offset >= 0.0);
            REQUIRE(offset < 1.0);

            // Verify reconstruction: offset * increment = wrapped phase
            double reconstructedPhase = offset * tc.increment;
            double relError = std::abs(reconstructedPhase - wrapped);
            if (std::abs(wrapped) > 1e-15) {
                relError /= std::abs(wrapped);
            }

            INFO("prevPhase=" << tc.prevPhase << " inc=" << tc.increment
                 << " wrapped=" << wrapped << " offset=" << offset
                 << " reconstructed=" << reconstructedPhase << " relError=" << relError);
            REQUIRE(relError < 1e-10);

            // Also verify crossing point reconstruction
            double crossingFraction = 1.0 - offset;
            double crossingPhase = tc.prevPhase + crossingFraction * tc.increment;
            double crossingError = std::abs(crossingPhase - 1.0);
            INFO("crossingFraction=" << crossingFraction << " crossingPhase=" << crossingPhase
                 << " crossingError=" << crossingError);
            REQUIRE(crossingError < 1e-10);
        }
    }
}

// =============================================================================
// T047: PhaseAccumulator::advance() basic behavior
// =============================================================================

TEST_CASE("PhaseAccumulator advance increments phase correctly", "[phase_utils]") {
    PhaseAccumulator acc;
    acc.increment = 0.1;

    SECTION("Single advance") {
        (void)acc.advance();
        REQUIRE(acc.phase == Approx(0.1).margin(1e-12));
    }

    SECTION("Multiple advances") {
        for (int i = 0; i < 5; ++i) {
            (void)acc.advance();
        }
        REQUIRE(acc.phase == Approx(0.5).margin(1e-12));
    }

    SECTION("Phase stays in [0, 1)") {
        for (int i = 0; i < 15; ++i) {
            (void)acc.advance();
            REQUIRE(acc.phase >= 0.0);
            REQUIRE(acc.phase < 1.0);
        }
    }
}

// =============================================================================
// T048: PhaseAccumulator::advance() wrap detection (FR-020)
// =============================================================================

TEST_CASE("PhaseAccumulator advance returns true on wrap", "[phase_utils][FR-020]") {
    PhaseAccumulator acc;
    acc.increment = 0.3;

    // Advances: 0.3, 0.6, 0.9, 1.2->0.2 (wrap!)
    REQUIRE(acc.advance() == false); // 0.3
    REQUIRE(acc.advance() == false); // 0.6
    REQUIRE(acc.advance() == false); // 0.9
    REQUIRE(acc.advance() == true);  // wrap to 0.2
    REQUIRE(acc.phase == Approx(0.2).margin(1e-12));
}

// =============================================================================
// T049: PhaseAccumulator wrap count (SC-005)
// =============================================================================

TEST_CASE("PhaseAccumulator produces correct wrap count for 440 Hz", "[phase_utils][SC-005]") {
    PhaseAccumulator acc;
    acc.setFrequency(440.0f, 44100.0f);

    int wrapCount = 0;
    constexpr int kNumSamples = 44100;

    for (int i = 0; i < kNumSamples; ++i) {
        if (acc.advance()) {
            ++wrapCount;
        }
    }

    // Should be exactly 440 wraps (plus or minus 1 due to boundary alignment)
    INFO("Wrap count: " << wrapCount);
    REQUIRE(wrapCount >= 439);
    REQUIRE(wrapCount <= 441);
}

// =============================================================================
// T050: PhaseAccumulator::reset()
// =============================================================================

TEST_CASE("PhaseAccumulator reset returns phase to 0", "[phase_utils]") {
    PhaseAccumulator acc;
    acc.increment = 0.1;

    // Advance a few times
    (void)acc.advance();
    (void)acc.advance();
    (void)acc.advance();
    REQUIRE(acc.phase > 0.0);

    // Reset
    acc.reset();
    REQUIRE(acc.phase == 0.0);

    // Increment should be preserved
    REQUIRE(acc.increment == Approx(0.1).margin(1e-12));
}

// =============================================================================
// T051: PhaseAccumulator::setFrequency()
// =============================================================================

TEST_CASE("PhaseAccumulator setFrequency sets correct increment", "[phase_utils]") {
    PhaseAccumulator acc;
    acc.setFrequency(440.0f, 44100.0f);

    double expected = 440.0 / 44100.0;
    REQUIRE(acc.increment == Approx(expected).margin(1e-6));
}

TEST_CASE("PhaseAccumulator setFrequency with zero sample rate", "[phase_utils]") {
    PhaseAccumulator acc;
    acc.setFrequency(440.0f, 0.0f);
    REQUIRE(acc.increment == 0.0);
}

// =============================================================================
// T061-T063: LFO compatibility test (SC-009, User Story 3)
// =============================================================================

TEST_CASE("PhaseAccumulator matches LFO phase logic over 1M samples", "[phase_utils][SC-009]") {
    // Simulate the exact LFO phase logic from lfo.h lines 138-151:
    // phase_ += phaseIncrement_;
    // if (phase_ >= 1.0) phase_ -= 1.0;

    constexpr float frequency = 440.0f;
    constexpr double sampleRate = 44100.0;
    constexpr int kNumSamples = 1000000;

    // LFO-style phase logic
    double lfoPhase = 0.0;
    double lfoIncrement = static_cast<double>(frequency) / sampleRate;

    // PhaseAccumulator
    PhaseAccumulator acc;
    acc.phase = 0.0;
    acc.increment = static_cast<double>(frequency) / sampleRate;

    SECTION("Phase values match within 1e-12 over 1M samples (T062)") {
        for (int i = 0; i < kNumSamples; ++i) {
            // LFO advance
            lfoPhase += lfoIncrement;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;

            // PhaseAccumulator advance
            (void)acc.advance();

            INFO("Sample " << i << ": LFO=" << lfoPhase << " ACC=" << acc.phase);
            REQUIRE(acc.phase == Approx(lfoPhase).margin(1e-12));
        }
    }

    SECTION("Double precision characteristics match (T063)") {
        // Verify both use double precision
        static_assert(std::is_same_v<decltype(acc.phase), double>,
                      "PhaseAccumulator::phase must be double");
        static_assert(std::is_same_v<decltype(acc.increment), double>,
                      "PhaseAccumulator::increment must be double");

        // Verify increment matches the LFO pattern: static_cast<double>(freq) / sampleRate
        REQUIRE(acc.increment == lfoIncrement);

        SUCCEED("Both use double precision with identical increment calculation");
    }
}
