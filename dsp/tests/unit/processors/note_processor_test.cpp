// ==============================================================================
// NoteProcessor Unit Tests
// ==============================================================================
// Test-first development per Constitution Principle XII.
// Tests cover all user stories (US1-US4), edge cases, and success criteria.
//
// Tags:
// [note_processor] - All NoteProcessor tests
// [us1] - User Story 1: Note-to-frequency conversion with tunable reference
// [us2] - User Story 2: Pitch bend with smoothing
// [us3] - User Story 3: Velocity curve mapping
// [us4] - User Story 4: Multi-destination velocity routing
// [edge] - Edge case tests
// [sc] - Success criteria verification tests
// ==============================================================================

#include <krate/dsp/processors/note_processor.h>

#include <catch2/catch_all.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helper Constants
// =============================================================================

static constexpr float kSampleRate = 44100.0f;
static constexpr float kFreqTolerance = 0.01f;  // Hz tolerance per SC-001

/// Expected frequency for a MIDI note using 12-TET: a4 * 2^((note-69)/12)
static float expectedFrequency(int note, float a4 = 440.0f) {
    return a4 * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
}

// =============================================================================
// Phase 2: Foundational - Layer 0 Velocity Utilities
// =============================================================================

TEST_CASE("VelocityCurve enum values", "[note_processor][foundational]") {
    // Verify enum values match data model
    REQUIRE(static_cast<int>(VelocityCurve::Linear) == 0);
    REQUIRE(static_cast<int>(VelocityCurve::Soft) == 1);
    REQUIRE(static_cast<int>(VelocityCurve::Hard) == 2);
    REQUIRE(static_cast<int>(VelocityCurve::Fixed) == 3);
}

TEST_CASE("mapVelocity Linear curve", "[note_processor][foundational][sc]") {
    // FR-011: Linear curve: output = velocity / 127.0
    REQUIRE(mapVelocity(0, VelocityCurve::Linear) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(127, VelocityCurve::Linear) == Approx(1.0f).margin(0.001f));
    REQUIRE(mapVelocity(64, VelocityCurve::Linear) == Approx(64.0f / 127.0f).margin(0.001f));
    REQUIRE(mapVelocity(1, VelocityCurve::Linear) == Approx(1.0f / 127.0f).margin(0.001f));
}

TEST_CASE("mapVelocity Soft curve", "[note_processor][foundational][sc]") {
    // FR-012: Soft curve: output = sqrt(velocity / 127.0)
    REQUIRE(mapVelocity(0, VelocityCurve::Soft) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(127, VelocityCurve::Soft) == Approx(1.0f).margin(0.001f));
    float expected64 = std::sqrt(64.0f / 127.0f);  // ~0.710
    REQUIRE(mapVelocity(64, VelocityCurve::Soft) == Approx(expected64).margin(0.001f));
    // Soft curve at 64 should be > linear at 64
    REQUIRE(mapVelocity(64, VelocityCurve::Soft) > mapVelocity(64, VelocityCurve::Linear));
}

TEST_CASE("mapVelocity Hard curve", "[note_processor][foundational][sc]") {
    // FR-013: Hard curve: output = (velocity / 127.0)^2
    REQUIRE(mapVelocity(0, VelocityCurve::Hard) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(127, VelocityCurve::Hard) == Approx(1.0f).margin(0.001f));
    float expected64 = (64.0f / 127.0f) * (64.0f / 127.0f);  // ~0.254
    REQUIRE(mapVelocity(64, VelocityCurve::Hard) == Approx(expected64).margin(0.001f));
    // Hard curve at 64 should be < linear at 64
    REQUIRE(mapVelocity(64, VelocityCurve::Hard) < mapVelocity(64, VelocityCurve::Linear));
}

TEST_CASE("mapVelocity Fixed curve", "[note_processor][foundational][sc]") {
    // FR-014: Fixed curve: returns 1.0 for any velocity > 0
    REQUIRE(mapVelocity(0, VelocityCurve::Fixed) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(1, VelocityCurve::Fixed) == Approx(1.0f).margin(0.001f));
    REQUIRE(mapVelocity(64, VelocityCurve::Fixed) == Approx(1.0f).margin(0.001f));
    REQUIRE(mapVelocity(127, VelocityCurve::Fixed) == Approx(1.0f).margin(0.001f));
}

TEST_CASE("mapVelocity velocity 0 always returns 0", "[note_processor][foundational][sc]") {
    // FR-015: All velocity curves MUST return 0.0 for velocity 0
    REQUIRE(mapVelocity(0, VelocityCurve::Linear) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(0, VelocityCurve::Soft) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(0, VelocityCurve::Hard) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(0, VelocityCurve::Fixed) == Approx(0.0f).margin(0.001f));
}

TEST_CASE("mapVelocity clamps out-of-range input", "[note_processor][foundational][edge]") {
    // FR-016: clamp to [0, 127]
    REQUIRE(mapVelocity(-1, VelocityCurve::Linear) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(-100, VelocityCurve::Linear) == Approx(0.0f).margin(0.001f));
    REQUIRE(mapVelocity(128, VelocityCurve::Linear) == Approx(1.0f).margin(0.001f));
    REQUIRE(mapVelocity(255, VelocityCurve::Linear) == Approx(1.0f).margin(0.001f));
}

// =============================================================================
// Phase 3: User Story 1 - Note-to-Frequency Conversion
// =============================================================================

TEST_CASE("NoteProcessor default constructor", "[note_processor][us1]") {
    NoteProcessor np;
    // Default A4 reference should be 440 Hz
    REQUIRE(np.getTuningReference() == Approx(440.0f).margin(0.001f));
}

TEST_CASE("NoteProcessor prepare sets sample rate", "[note_processor][us1]") {
    NoteProcessor np;
    np.prepare(48000.0);
    // After prepare, processor should be functional
    // Tuning reference should be unchanged
    REQUIRE(np.getTuningReference() == Approx(440.0f).margin(0.001f));
    // getFrequency should still work
    float freq = np.getFrequency(69);
    REQUIRE(freq == Approx(440.0f).margin(kFreqTolerance));
}

TEST_CASE("NoteProcessor setTuningReference and getTuningReference", "[note_processor][us1]") {
    NoteProcessor np;

    SECTION("valid tuning references") {
        np.setTuningReference(442.0f);
        REQUIRE(np.getTuningReference() == Approx(442.0f).margin(0.001f));

        np.setTuningReference(432.0f);
        REQUIRE(np.getTuningReference() == Approx(432.0f).margin(0.001f));

        np.setTuningReference(400.0f);
        REQUIRE(np.getTuningReference() == Approx(400.0f).margin(0.001f));

        np.setTuningReference(480.0f);
        REQUIRE(np.getTuningReference() == Approx(480.0f).margin(0.001f));
    }

    SECTION("NaN resets to 440 Hz") {
        np.setTuningReference(442.0f);
        np.setTuningReference(std::numeric_limits<float>::quiet_NaN());
        REQUIRE(np.getTuningReference() == Approx(440.0f).margin(0.001f));
    }

    SECTION("Inf resets to 440 Hz") {
        np.setTuningReference(442.0f);
        np.setTuningReference(std::numeric_limits<float>::infinity());
        REQUIRE(np.getTuningReference() == Approx(440.0f).margin(0.001f));

        np.setTuningReference(442.0f);
        np.setTuningReference(-std::numeric_limits<float>::infinity());
        REQUIRE(np.getTuningReference() == Approx(440.0f).margin(0.001f));
    }
}

TEST_CASE("NoteProcessor getFrequency default tuning A4=440Hz", "[note_processor][us1][sc]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    // SC-001: All 128 MIDI notes must match 12-TET within 0.01 Hz
    SECTION("A4 = note 69 = 440 Hz") {
        REQUIRE(np.getFrequency(69) == Approx(440.0f).margin(kFreqTolerance));
    }

    SECTION("C4 = note 60 = ~261.626 Hz") {
        REQUIRE(np.getFrequency(60) == Approx(expectedFrequency(60)).margin(kFreqTolerance));
    }

    SECTION("C5 = note 72 = ~523.25 Hz") {
        REQUIRE(np.getFrequency(72) == Approx(expectedFrequency(72)).margin(kFreqTolerance));
    }

    SECTION("A0 = note 21 = ~27.5 Hz") {
        REQUIRE(np.getFrequency(21) == Approx(expectedFrequency(21)).margin(kFreqTolerance));
    }

    SECTION("Full MIDI range 0-127 within tolerance") {
        for (int note = 0; note <= 127; ++note) {
            float expected = expectedFrequency(note);
            float actual = np.getFrequency(static_cast<uint8_t>(note));
            REQUIRE(actual == Approx(expected).margin(kFreqTolerance));
        }
    }
}

TEST_CASE("NoteProcessor getFrequency with various A4 references", "[note_processor][us1][sc]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    // SC-005: Tuning reference changes reflected immediately
    SECTION("A4 = 432 Hz") {
        np.setTuningReference(432.0f);
        REQUIRE(np.getFrequency(69) == Approx(432.0f).margin(kFreqTolerance));
        REQUIRE(np.getFrequency(60) == Approx(expectedFrequency(60, 432.0f)).margin(kFreqTolerance));
    }

    SECTION("A4 = 442 Hz") {
        np.setTuningReference(442.0f);
        REQUIRE(np.getFrequency(69) == Approx(442.0f).margin(kFreqTolerance));
    }

    SECTION("A4 = 443 Hz") {
        np.setTuningReference(443.0f);
        REQUIRE(np.getFrequency(69) == Approx(443.0f).margin(kFreqTolerance));
    }

    SECTION("A4 = 444 Hz") {
        np.setTuningReference(444.0f);
        REQUIRE(np.getFrequency(69) == Approx(444.0f).margin(kFreqTolerance));
    }
}

TEST_CASE("NoteProcessor tuning reference edge cases", "[note_processor][us1][edge]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    SECTION("out-of-range low clamps to 400 Hz") {
        np.setTuningReference(300.0f);
        REQUIRE(np.getTuningReference() == Approx(400.0f).margin(0.001f));
        REQUIRE(np.getFrequency(69) == Approx(400.0f).margin(kFreqTolerance));
    }

    SECTION("out-of-range high clamps to 480 Hz") {
        np.setTuningReference(600.0f);
        REQUIRE(np.getTuningReference() == Approx(480.0f).margin(0.001f));
        REQUIRE(np.getFrequency(69) == Approx(480.0f).margin(kFreqTolerance));
    }

    SECTION("NaN resets to 440 Hz") {
        np.setTuningReference(std::numeric_limits<float>::quiet_NaN());
        REQUIRE(np.getTuningReference() == Approx(440.0f).margin(0.001f));
        REQUIRE(np.getFrequency(69) == Approx(440.0f).margin(kFreqTolerance));
    }

    SECTION("Inf resets to 440 Hz") {
        np.setTuningReference(std::numeric_limits<float>::infinity());
        REQUIRE(np.getTuningReference() == Approx(440.0f).margin(0.001f));
    }
}

// =============================================================================
// Phase 4: User Story 2 - Pitch Bend with Smoothing
// =============================================================================

TEST_CASE("NoteProcessor setPitchBend stores target", "[note_processor][us2]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    // After setting pitch bend and processing many samples, should converge
    np.setPitchBend(0.5f);
    for (int i = 0; i < 1000; ++i) {
        (void)np.processPitchBend();
    }
    // getFrequency should now reflect the bend
    float freqNoBend = expectedFrequency(69);
    float freqWithBend = np.getFrequency(69);
    // Default bend range = 2 semitones, bend = 0.5 -> 1 semitone up
    float expectedBent = freqNoBend * std::pow(2.0f, 1.0f / 12.0f);
    REQUIRE(freqWithBend == Approx(expectedBent).margin(0.1f));
}

TEST_CASE("NoteProcessor processPitchBend returns smoothed value", "[note_processor][us2]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    // Initially at 0.0
    float val = np.processPitchBend();
    REQUIRE(val == Approx(0.0f).margin(0.001f));

    // Set target to 1.0 and process some samples
    np.setPitchBend(1.0f);
    float first = np.processPitchBend();
    // First sample should be > 0 (moving toward target) but < 1.0 (not instant)
    REQUIRE(first > 0.0f);
    REQUIRE(first < 1.0f);
}

TEST_CASE("NoteProcessor setPitchBendRange clamps to [0, 24]", "[note_processor][us2]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    SECTION("valid range") {
        np.setPitchBendRange(12.0f);
        np.setPitchBend(1.0f);
        // Converge smoother
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        // At +1.0 with 12 semitone range: A4 up one octave = 880 Hz
        REQUIRE(np.getFrequency(69) == Approx(880.0f).margin(0.5f));
    }

    SECTION("negative clamps to 0") {
        np.setPitchBendRange(-5.0f);
        np.setPitchBend(1.0f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        // Range = 0, bend should have no effect
        REQUIRE(np.getFrequency(69) == Approx(440.0f).margin(kFreqTolerance));
    }

    SECTION("above 24 clamps to 24") {
        np.setPitchBendRange(48.0f);
        np.setPitchBend(1.0f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        // Range = 24 semitones = 2 octaves
        float expected = 440.0f * std::pow(2.0f, 24.0f / 12.0f);
        REQUIRE(np.getFrequency(69) == Approx(expected).margin(1.0f));
    }
}

TEST_CASE("NoteProcessor setSmoothingTime", "[note_processor][us2]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    // Very fast smoothing - should converge in fewer samples
    np.setSmoothingTime(0.5f);
    np.setPitchBend(1.0f);

    // Process enough samples for fast convergence
    for (int i = 0; i < 100; ++i) (void)np.processPitchBend();

    float freq = np.getFrequency(69);
    float expected = 440.0f * std::pow(2.0f, 2.0f / 12.0f);  // +2 semitones
    REQUIRE(freq == Approx(expected).margin(0.5f));
}

TEST_CASE("NoteProcessor getFrequency with pitch bend at endpoints", "[note_processor][us2][sc]") {
    NoteProcessor np;
    np.prepare(kSampleRate);
    np.setPitchBendRange(2.0f);

    SECTION("SC-002: +1.0 bipolar = +2 semitones (B4 ~493.88 Hz)") {
        np.setPitchBend(1.0f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        float freq = np.getFrequency(69);
        float expected = 440.0f * std::pow(2.0f, 2.0f / 12.0f);  // ~493.88
        REQUIRE(freq == Approx(expected).margin(0.1f));
    }

    SECTION("SC-002: -1.0 bipolar = -2 semitones (G4 ~392.00 Hz)") {
        np.setPitchBend(-1.0f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        float freq = np.getFrequency(69);
        float expected = 440.0f * std::pow(2.0f, -2.0f / 12.0f);  // ~392.00
        REQUIRE(freq == Approx(expected).margin(0.1f));
    }
}

TEST_CASE("NoteProcessor pitch bend 12 semitone range (one octave)", "[note_processor][us2][sc]") {
    NoteProcessor np;
    np.prepare(kSampleRate);
    np.setPitchBendRange(12.0f);

    np.setPitchBend(1.0f);
    for (int i = 0; i < 1000; ++i) { (void)np.processPitchBend(); }

    // +1.0 at 12 semitone range = exactly one octave up
    float freq = np.getFrequency(69);
    REQUIRE(freq == Approx(880.0f).margin(0.5f));

    // Reset and try -1.0
    np.reset();
    np.setPitchBend(-1.0f);
    for (int i = 0; i < 1000; ++i) { (void)np.processPitchBend(); }

    freq = np.getFrequency(69);
    REQUIRE(freq == Approx(220.0f).margin(0.5f));
}

TEST_CASE("NoteProcessor pitch bend smoothing convergence", "[note_processor][us2][sc]") {
    // SC-003: After a jump from 0 to 1, smoothed output must reach 99% within
    // configured smoothing time, with no single step > 10% of total range
    NoteProcessor np;
    np.prepare(kSampleRate);
    np.setSmoothingTime(5.0f);

    np.setPitchBend(1.0f);

    // 5ms at 44100 Hz = 220.5 samples
    int samplesFor5ms = static_cast<int>(5.0f * 0.001f * kSampleRate);

    float prevVal = 0.0f;
    float maxJump = 0.0f;
    float smoothedVal = 0.0f;

    for (int i = 0; i < samplesFor5ms; ++i) {
        smoothedVal = np.processPitchBend();
        float jump = std::abs(smoothedVal - prevVal);
        if (jump > maxJump) maxJump = jump;
        prevVal = smoothedVal;
    }

    // Must reach 99% of target (0.99) within smoothing time
    REQUIRE(smoothedVal >= 0.99f);
    // No individual jump > 10% of total range (0.1)
    REQUIRE(maxJump <= 0.1f);
}

TEST_CASE("NoteProcessor pitch bend NaN/Inf ignored (FR-020)", "[note_processor][us2][edge]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    SECTION("NaN pitch bend ignored") {
        np.setPitchBend(0.5f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        float freqBefore = np.getFrequency(69);

        // Send NaN - should be ignored
        np.setPitchBend(std::numeric_limits<float>::quiet_NaN());
        for (int i = 0; i < 100; ++i) { (void)np.processPitchBend(); }
        float freqAfter = np.getFrequency(69);

        REQUIRE(freqAfter == Approx(freqBefore).margin(0.1f));
    }

    SECTION("Inf pitch bend ignored") {
        np.setPitchBend(-0.3f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        float freqBefore = np.getFrequency(69);

        np.setPitchBend(std::numeric_limits<float>::infinity());
        for (int i = 0; i < 100; ++i) { (void)np.processPitchBend(); }
        float freqAfter = np.getFrequency(69);

        REQUIRE(freqAfter == Approx(freqBefore).margin(0.1f));
    }

    SECTION("zero range means no effect") {
        np.setPitchBendRange(0.0f);
        np.setPitchBend(1.0f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        REQUIRE(np.getFrequency(69) == Approx(440.0f).margin(kFreqTolerance));
    }

    SECTION("neutral (0.0) means no offset") {
        np.setPitchBend(0.0f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        REQUIRE(np.getFrequency(69) == Approx(440.0f).margin(kFreqTolerance));
    }
}

TEST_CASE("NoteProcessor NaN/Inf guard ordering (FR-020, R10)", "[note_processor][us2][edge]") {
    // T034b: After setting a valid pitch bend (0.5), sending NaN must NOT reset
    // the smoother state to 0.0 -- the smoothed value must remain at last valid state.
    NoteProcessor np;
    np.prepare(kSampleRate);

    // Set valid bend and converge
    np.setPitchBend(0.5f);
    for (int i = 0; i < 1000; ++i) { (void)np.processPitchBend(); }

    float smoothedBefore = np.processPitchBend();
    REQUIRE(smoothedBefore == Approx(0.5f).margin(0.01f));

    // Send NaN
    np.setPitchBend(std::numeric_limits<float>::quiet_NaN());

    // Process a few more samples - value should NOT drop to 0
    float smoothedAfter = np.processPitchBend();
    REQUIRE(smoothedAfter == Approx(0.5f).margin(0.01f));
}

TEST_CASE("NoteProcessor reset snaps pitch bend to zero", "[note_processor][us2]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    // Set a non-zero pitch bend and converge
    np.setPitchBend(1.0f);
    for (int i = 0; i < 1000; ++i) { (void)np.processPitchBend(); }

    // Reset should snap everything to zero
    np.reset();

    // Frequency should be unbent
    REQUIRE(np.getFrequency(69) == Approx(440.0f).margin(kFreqTolerance));

    // Smoothed value should be 0.0
    float val = np.processPitchBend();
    REQUIRE(val == Approx(0.0f).margin(0.001f));
}

TEST_CASE("NoteProcessor extreme frequency edge cases", "[note_processor][us2][edge]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    SECTION("note 0 with -24 semitone bend (maximum downward)") {
        np.setPitchBendRange(24.0f);
        np.setPitchBend(-1.0f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        float freq = np.getFrequency(0);
        // Should be positive and finite
        REQUIRE(freq > 0.0f);
        REQUIRE(!detail::isNaN(freq));
        REQUIRE(!detail::isInf(freq));
    }

    SECTION("note 127 with +24 semitone bend (maximum upward)") {
        np.setPitchBendRange(24.0f);
        np.setPitchBend(1.0f);
        for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();
        float freq = np.getFrequency(127);
        // Should be finite and not overflow
        REQUIRE(freq > 0.0f);
        REQUIRE(!detail::isNaN(freq));
        REQUIRE(!detail::isInf(freq));
    }
}

TEST_CASE("NoteProcessor prepare mid-transition preserves state (FR-003)", "[note_processor][us2]") {
    NoteProcessor np;
    np.prepare(kSampleRate);

    // Start a pitch bend transition
    np.setPitchBend(1.0f);
    // Process a few samples (mid-transition)
    for (int i = 0; i < 50; ++i) { (void)np.processPitchBend(); }
    float midValue = np.processPitchBend();
    // Should be between 0 and 1 (mid-transition)
    REQUIRE(midValue > 0.0f);
    REQUIRE(midValue < 1.0f);

    // Change sample rate mid-transition
    np.prepare(96000.0);

    // The current smoothed value should be preserved (not reset)
    float afterPrepare = np.processPitchBend();
    // Should still be near the mid-value (coefficient changes but value preserved)
    REQUIRE(afterPrepare > 0.0f);
    REQUIRE(afterPrepare <= 1.0f);
}

// =============================================================================
// Phase 5: User Story 3 - Velocity Curve Mapping (NoteProcessor member)
// =============================================================================

TEST_CASE("NoteProcessor setVelocityCurve", "[note_processor][us3]") {
    NoteProcessor np;

    // Default should be Linear
    VelocityOutput out = np.mapVelocity(64);
    float linearExpected = 64.0f / 127.0f;
    REQUIRE(out.amplitude == Approx(linearExpected).margin(0.001f));

    // Switch to Soft
    np.setVelocityCurve(VelocityCurve::Soft);
    out = np.mapVelocity(64);
    float softExpected = std::sqrt(64.0f / 127.0f);
    REQUIRE(out.amplitude == Approx(softExpected).margin(0.001f));
}

TEST_CASE("NoteProcessor mapVelocity Linear curve", "[note_processor][us3][sc]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Linear);

    // SC-004: velocity 127 -> 1.0
    VelocityOutput out127 = np.mapVelocity(127);
    REQUIRE(out127.amplitude == Approx(1.0f).margin(0.001f));

    // SC-004: velocity 64 -> ~0.504
    VelocityOutput out64 = np.mapVelocity(64);
    REQUIRE(out64.amplitude == Approx(64.0f / 127.0f).margin(0.001f));

    // velocity 0 -> 0.0
    VelocityOutput out0 = np.mapVelocity(0);
    REQUIRE(out0.amplitude == Approx(0.0f).margin(0.001f));
}

TEST_CASE("NoteProcessor mapVelocity Soft curve", "[note_processor][us3][sc]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Soft);

    // velocity 64 should map to > 0.504 (~0.710)
    VelocityOutput out = np.mapVelocity(64);
    float softExpected = std::sqrt(64.0f / 127.0f);
    REQUIRE(out.amplitude == Approx(softExpected).margin(0.001f));
    REQUIRE(out.amplitude > 64.0f / 127.0f);  // Greater than linear
}

TEST_CASE("NoteProcessor mapVelocity Hard curve", "[note_processor][us3][sc]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Hard);

    // velocity 64 should map to < 0.504 (~0.254)
    VelocityOutput out = np.mapVelocity(64);
    float hardExpected = (64.0f / 127.0f) * (64.0f / 127.0f);
    REQUIRE(out.amplitude == Approx(hardExpected).margin(0.001f));
    REQUIRE(out.amplitude < 64.0f / 127.0f);  // Less than linear
}

TEST_CASE("NoteProcessor mapVelocity Fixed curve", "[note_processor][us3][sc]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Fixed);

    // Any non-zero velocity -> 1.0
    REQUIRE(np.mapVelocity(1).amplitude == Approx(1.0f).margin(0.001f));
    REQUIRE(np.mapVelocity(64).amplitude == Approx(1.0f).margin(0.001f));
    REQUIRE(np.mapVelocity(127).amplitude == Approx(1.0f).margin(0.001f));
}

TEST_CASE("NoteProcessor velocity edge cases", "[note_processor][us3][edge]") {
    NoteProcessor np;

    SECTION("velocity 0 always maps to 0 regardless of curve") {
        np.setVelocityCurve(VelocityCurve::Linear);
        REQUIRE(np.mapVelocity(0).amplitude == Approx(0.0f).margin(0.001f));

        np.setVelocityCurve(VelocityCurve::Soft);
        REQUIRE(np.mapVelocity(0).amplitude == Approx(0.0f).margin(0.001f));

        np.setVelocityCurve(VelocityCurve::Hard);
        REQUIRE(np.mapVelocity(0).amplitude == Approx(0.0f).margin(0.001f));

        np.setVelocityCurve(VelocityCurve::Fixed);
        REQUIRE(np.mapVelocity(0).amplitude == Approx(0.0f).margin(0.001f));
    }

    SECTION("out-of-range velocities clamped") {
        np.setVelocityCurve(VelocityCurve::Linear);
        REQUIRE(np.mapVelocity(-1).amplitude == Approx(0.0f).margin(0.001f));
        REQUIRE(np.mapVelocity(128).amplitude == Approx(1.0f).margin(0.001f));
        REQUIRE(np.mapVelocity(255).amplitude == Approx(1.0f).margin(0.001f));
    }
}

// =============================================================================
// Phase 6: User Story 4 - Multi-Destination Velocity Routing
// =============================================================================

TEST_CASE("NoteProcessor setAmplitudeVelocityDepth clamps to [0, 1]", "[note_processor][us4]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Linear);

    np.setAmplitudeVelocityDepth(0.5f);
    VelocityOutput out = np.mapVelocity(127);
    REQUIRE(out.amplitude == Approx(0.5f).margin(0.001f));

    // Clamp below 0
    np.setAmplitudeVelocityDepth(-0.5f);
    out = np.mapVelocity(127);
    REQUIRE(out.amplitude == Approx(0.0f).margin(0.001f));

    // Clamp above 1
    np.setAmplitudeVelocityDepth(2.0f);
    out = np.mapVelocity(127);
    REQUIRE(out.amplitude == Approx(1.0f).margin(0.001f));
}

TEST_CASE("NoteProcessor setFilterVelocityDepth clamps to [0, 1]", "[note_processor][us4]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Linear);

    np.setFilterVelocityDepth(0.75f);
    VelocityOutput out = np.mapVelocity(127);
    REQUIRE(out.filter == Approx(0.75f).margin(0.001f));

    // Clamp below 0
    np.setFilterVelocityDepth(-1.0f);
    out = np.mapVelocity(127);
    REQUIRE(out.filter == Approx(0.0f).margin(0.001f));

    // Clamp above 1
    np.setFilterVelocityDepth(5.0f);
    out = np.mapVelocity(127);
    REQUIRE(out.filter == Approx(1.0f).margin(0.001f));
}

TEST_CASE("NoteProcessor setEnvelopeTimeVelocityDepth clamps to [0, 1]", "[note_processor][us4]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Linear);

    np.setEnvelopeTimeVelocityDepth(0.3f);
    VelocityOutput out = np.mapVelocity(127);
    REQUIRE(out.envelopeTime == Approx(0.3f).margin(0.001f));

    // Clamp below 0
    np.setEnvelopeTimeVelocityDepth(-0.1f);
    out = np.mapVelocity(127);
    REQUIRE(out.envelopeTime == Approx(0.0f).margin(0.001f));

    // Clamp above 1
    np.setEnvelopeTimeVelocityDepth(10.0f);
    out = np.mapVelocity(127);
    REQUIRE(out.envelopeTime == Approx(1.0f).margin(0.001f));
}

TEST_CASE("NoteProcessor multi-destination independent scaling", "[note_processor][us4][sc]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Linear);

    // amplitude 100%, filter 50%, envelope 0%
    np.setAmplitudeVelocityDepth(1.0f);
    np.setFilterVelocityDepth(0.5f);
    np.setEnvelopeTimeVelocityDepth(0.0f);

    VelocityOutput out = np.mapVelocity(127);
    // curvedVel = 1.0 (linear, vel 127)
    REQUIRE(out.amplitude == Approx(1.0f).margin(0.001f));    // 1.0 * 1.0
    REQUIRE(out.filter == Approx(0.5f).margin(0.001f));       // 1.0 * 0.5
    REQUIRE(out.envelopeTime == Approx(0.0f).margin(0.001f)); // 1.0 * 0.0

    // With velocity 64: curvedVel = 64/127 ~ 0.504
    out = np.mapVelocity(64);
    float curvedVel = 64.0f / 127.0f;
    REQUIRE(out.amplitude == Approx(curvedVel * 1.0f).margin(0.001f));
    REQUIRE(out.filter == Approx(curvedVel * 0.5f).margin(0.001f));
    REQUIRE(out.envelopeTime == Approx(curvedVel * 0.0f).margin(0.001f));
}

TEST_CASE("NoteProcessor multi-destination depth edge cases", "[note_processor][us4][edge]") {
    NoteProcessor np;
    np.setVelocityCurve(VelocityCurve::Linear);

    SECTION("depth 0.0 produces 0.0 output") {
        np.setAmplitudeVelocityDepth(0.0f);
        np.setFilterVelocityDepth(0.0f);
        np.setEnvelopeTimeVelocityDepth(0.0f);

        VelocityOutput out = np.mapVelocity(127);
        REQUIRE(out.amplitude == Approx(0.0f).margin(0.001f));
        REQUIRE(out.filter == Approx(0.0f).margin(0.001f));
        REQUIRE(out.envelopeTime == Approx(0.0f).margin(0.001f));
    }

    SECTION("depth 1.0 produces full curve output") {
        np.setAmplitudeVelocityDepth(1.0f);
        np.setFilterVelocityDepth(1.0f);
        np.setEnvelopeTimeVelocityDepth(1.0f);

        VelocityOutput out = np.mapVelocity(127);
        REQUIRE(out.amplitude == Approx(1.0f).margin(0.001f));
        REQUIRE(out.filter == Approx(1.0f).margin(0.001f));
        REQUIRE(out.envelopeTime == Approx(1.0f).margin(0.001f));
    }
}

// =============================================================================
// Performance Benchmark: SC-006 getFrequency() CPU budget
// =============================================================================

TEST_CASE("NoteProcessor getFrequency performance (SC-006)", "[note_processor][sc][benchmark]") {
    // SC-006: getFrequency() must take <0.1% CPU at 44.1 kHz.
    // At 44.1 kHz, one sample period = 1/44100 = ~22.68 us.
    // 0.1% of that = ~22.68 ns per call.
    //
    // We measure by calling getFrequency() 1M times in a tight loop,
    // then computing the per-call time.

    NoteProcessor np;
    np.prepare(44100.0);
    np.setPitchBend(0.3f);
    // Converge smoother so we test the steady-state path
    for (int i = 0; i < 1000; ++i) (void)np.processPitchBend();

    constexpr int kIterations = 1000000;
    volatile float sink = 0.0f;  // Prevent optimization

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kIterations; ++i) {
        uint8_t note = static_cast<uint8_t>(i & 127);
        sink = np.getFrequency(note);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double nsPerCall = static_cast<double>(elapsed.count()) / kIterations;
    double samplePeriodNs = 1e9 / 44100.0;  // ~22675.7 ns
    double cpuPercent = (nsPerCall / samplePeriodNs) * 100.0;

    // Report measured values
    WARN("getFrequency() benchmark:");
    WARN("  " << kIterations << " iterations in " << elapsed.count() / 1000000.0 << " ms");
    WARN("  Per call: " << nsPerCall << " ns");
    WARN("  CPU at 44.1 kHz: " << cpuPercent << "%");
    WARN("  Budget: <0.1% (22.68 ns)");

    // SC-006: Must be under 0.1% CPU
    REQUIRE(cpuPercent < 0.1);

    (void)sink;
}
