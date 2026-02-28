// ==============================================================================
// Layer 0: Core Utility Tests - Transport Sync
// ==============================================================================
// Tests for calculateMusicalStepPosition() shared utility.
//
// Constitution Compliance:
// - Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/transport_sync.h>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Basic Step Calculation
// =============================================================================

TEST_CASE("calculateMusicalStepPosition basic cases", "[transport_sync]") {

    SECTION("PPQ 0.0 is step 0, fraction 0.0") {
        auto pos = calculateMusicalStepPosition(
            0.0, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos.step == 0);
        CHECK(pos.stepFraction == Approx(0.0));
    }

    SECTION("PPQ exactly on step boundary") {
        // Quarter notes, 4 steps: pattern = 4 beats
        // PPQ 1.0 = step 1, PPQ 2.0 = step 2, PPQ 3.0 = step 3
        auto pos1 = calculateMusicalStepPosition(
            1.0, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos1.step == 1);
        CHECK(pos1.stepFraction == Approx(0.0));

        auto pos2 = calculateMusicalStepPosition(
            2.0, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos2.step == 2);
        CHECK(pos2.stepFraction == Approx(0.0));

        auto pos3 = calculateMusicalStepPosition(
            3.0, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos3.step == 3);
        CHECK(pos3.stepFraction == Approx(0.0));
    }

    SECTION("PPQ mid-step gives correct fraction") {
        // Quarter notes: beatsPerStep = 1.0
        // PPQ 0.5 = step 0, fraction 0.5
        auto pos = calculateMusicalStepPosition(
            0.5, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos.step == 0);
        CHECK(pos.stepFraction == Approx(0.5));

        // PPQ 1.75 = step 1, fraction 0.75
        auto pos2 = calculateMusicalStepPosition(
            1.75, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos2.step == 1);
        CHECK(pos2.stepFraction == Approx(0.75));
    }
}

// =============================================================================
// Pattern Wrapping
// =============================================================================

TEST_CASE("calculateMusicalStepPosition wraps around pattern", "[transport_sync]") {

    SECTION("PPQ beyond pattern length wraps") {
        // 4 steps of quarter notes = 4 beats pattern
        // PPQ 5.0 = fmod(5.0, 4.0) = 1.0 -> step 1
        auto pos = calculateMusicalStepPosition(
            5.0, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos.step == 1);
        CHECK(pos.stepFraction == Approx(0.0));

        // PPQ 10.5 = fmod(10.5, 4.0) = 2.5 -> step 2, fraction 0.5
        auto pos2 = calculateMusicalStepPosition(
            10.5, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos2.step == 2);
        CHECK(pos2.stepFraction == Approx(0.5));
    }

    SECTION("PPQ exactly at pattern length goes to step 0") {
        // PPQ 4.0 = fmod(4.0, 4.0) = 0.0 -> step 0
        auto pos = calculateMusicalStepPosition(
            4.0, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos.step == 0);
        CHECK(pos.stepFraction == Approx(0.0));
    }
}

// =============================================================================
// Negative PPQ (pre-count)
// =============================================================================

TEST_CASE("calculateMusicalStepPosition handles negative PPQ", "[transport_sync]") {

    SECTION("Negative PPQ wraps correctly") {
        // Pattern is 4 beats. PPQ = -1.0
        // fmod(-1.0, 4.0) = -1.0 -> + 4.0 = 3.0 -> step 3, fraction 0.0
        auto pos = calculateMusicalStepPosition(
            -1.0, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos.step == 3);
        CHECK(pos.stepFraction == Approx(0.0));
    }

    SECTION("Negative PPQ mid-step") {
        // PPQ = -0.5: fmod(-0.5, 4.0) = -0.5 -> + 4.0 = 3.5
        // step = floor(3.5 / 1.0) % 4 = 3, fraction = 0.5
        auto pos = calculateMusicalStepPosition(
            -0.5, NoteValue::Quarter, NoteModifier::None, 4);
        CHECK(pos.step == 3);
        CHECK(pos.stepFraction == Approx(0.5));
    }
}

// =============================================================================
// Different Note Values
// =============================================================================

TEST_CASE("calculateMusicalStepPosition with different note values", "[transport_sync]") {

    SECTION("Sixteenth notes (0.25 beats per step)") {
        // 16 steps * 0.25 = 4 beats pattern
        // PPQ 1.0 = posInPattern 1.0, step = floor(1.0/0.25) % 16 = 4
        auto pos = calculateMusicalStepPosition(
            1.0, NoteValue::Sixteenth, NoteModifier::None, 16);
        CHECK(pos.step == 4);
        CHECK(pos.stepFraction == Approx(0.0));
    }

    SECTION("Eighth notes (0.5 beats per step)") {
        // 8 steps * 0.5 = 4 beats pattern
        // PPQ 1.5 = posInPattern 1.5, step = floor(1.5/0.5) % 8 = 3
        auto pos = calculateMusicalStepPosition(
            1.5, NoteValue::Eighth, NoteModifier::None, 8);
        CHECK(pos.step == 3);
        CHECK(pos.stepFraction == Approx(0.0));
    }

    SECTION("Dotted eighth (0.75 beats per step)") {
        // 4 steps * 0.75 = 3.0 beats pattern
        // PPQ 0.75 = step 1, fraction 0.0
        auto pos = calculateMusicalStepPosition(
            0.75, NoteValue::Eighth, NoteModifier::Dotted, 4);
        CHECK(pos.step == 1);
        CHECK(pos.stepFraction == Approx(0.0));

        // PPQ 0.375 = step 0, fraction 0.5
        auto pos2 = calculateMusicalStepPosition(
            0.375, NoteValue::Eighth, NoteModifier::Dotted, 4);
        CHECK(pos2.step == 0);
        CHECK(pos2.stepFraction == Approx(0.5));
    }

    SECTION("Triplet quarter (0.667 beats per step)") {
        // beatsPerStep ≈ 0.6667
        // PPQ 0.6667 = step 1
        const double beatsPerStep = static_cast<double>(
            getBeatsForNote(NoteValue::Quarter, NoteModifier::Triplet));
        auto pos = calculateMusicalStepPosition(
            beatsPerStep, NoteValue::Quarter, NoteModifier::Triplet, 4);
        CHECK(pos.step == 1);
        CHECK(pos.stepFraction == Approx(0.0).margin(1e-9));
    }
}

// =============================================================================
// Single-Step Pattern (Arp Use Case)
// =============================================================================

TEST_CASE("calculateMusicalStepPosition with numSteps=1", "[transport_sync]") {

    SECTION("Step is always 0, fraction tracks position within one step") {
        // numSteps = 1, quarter note: pattern = 1 beat
        // PPQ 0.25 -> step 0, fraction 0.25
        auto pos = calculateMusicalStepPosition(
            0.25, NoteValue::Quarter, NoteModifier::None, 1);
        CHECK(pos.step == 0);
        CHECK(pos.stepFraction == Approx(0.25));

        // PPQ 0.99 -> step 0, fraction 0.99
        auto pos2 = calculateMusicalStepPosition(
            0.99, NoteValue::Quarter, NoteModifier::None, 1);
        CHECK(pos2.step == 0);
        CHECK(pos2.stepFraction == Approx(0.99));

        // PPQ 1.5 wraps: fmod(1.5, 1.0) = 0.5 -> step 0, fraction 0.5
        auto pos3 = calculateMusicalStepPosition(
            1.5, NoteValue::Quarter, NoteModifier::None, 1);
        CHECK(pos3.step == 0);
        CHECK(pos3.stepFraction == Approx(0.5));
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("calculateMusicalStepPosition edge cases", "[transport_sync]") {

    SECTION("numSteps <= 0 returns default") {
        auto pos = calculateMusicalStepPosition(
            1.0, NoteValue::Quarter, NoteModifier::None, 0);
        CHECK(pos.step == 0);
        CHECK(pos.stepFraction == 0.0);

        auto pos2 = calculateMusicalStepPosition(
            1.0, NoteValue::Quarter, NoteModifier::None, -1);
        CHECK(pos2.step == 0);
        CHECK(pos2.stepFraction == 0.0);
    }

    SECTION("Large PPQ values don't break") {
        // PPQ at 1000 bars of 4/4 = 4000.0
        auto pos = calculateMusicalStepPosition(
            4000.0, NoteValue::Sixteenth, NoteModifier::None, 16);
        // 16 * 0.25 = 4 beats pattern, fmod(4000, 4) = 0 -> step 0
        CHECK(pos.step == 0);
        CHECK(pos.stepFraction == Approx(0.0));
    }
}
