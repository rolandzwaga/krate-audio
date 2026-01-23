// ==============================================================================
// MIDI Utilities - Unit Tests
// ==============================================================================
// Layer 0: Core Utilities
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: dsp/include/krate/dsp/core/midi_utils.h
// Feature: 088-self-osc-filter
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/db_utils.h>

#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// midiNoteToFrequency Tests (T003)
// ==============================================================================

TEST_CASE("midiNoteToFrequency converts MIDI notes to frequency using 12-TET",
          "[dsp][core][midi_utils][midiNoteToFrequency]") {

    SECTION("MIDI note 69 (A4) returns exactly 440 Hz") {
        REQUIRE(midiNoteToFrequency(69) == Approx(440.0f).margin(0.001f));
    }

    SECTION("MIDI note 60 (C4, middle C) returns 261.63 Hz") {
        // C4 = 440 * 2^((60-69)/12) = 440 * 2^(-9/12) = 261.626 Hz
        REQUIRE(midiNoteToFrequency(60) == Approx(261.63f).margin(0.01f));
    }

    SECTION("MIDI note 72 (C5) returns 523.25 Hz") {
        // C5 = 440 * 2^((72-69)/12) = 440 * 2^(3/12) = 523.251 Hz
        REQUIRE(midiNoteToFrequency(72) == Approx(523.25f).margin(0.01f));
    }

    SECTION("Boundary: MIDI note 0 returns valid low frequency") {
        // Note 0 = 440 * 2^((0-69)/12) = 440 * 2^(-69/12) = ~8.18 Hz
        float freq = midiNoteToFrequency(0);
        REQUIRE(freq > 8.0f);
        REQUIRE(freq < 8.5f);
        REQUIRE(freq == Approx(8.176f).margin(0.01f));
    }

    SECTION("Boundary: MIDI note 127 returns valid high frequency") {
        // Note 127 = 440 * 2^((127-69)/12) = 440 * 2^(58/12) = ~12543 Hz
        float freq = midiNoteToFrequency(127);
        REQUIRE(freq > 12500.0f);
        REQUIRE(freq < 12600.0f);
        REQUIRE(freq == Approx(12543.85f).margin(1.0f));
    }

    SECTION("Custom A4 frequency: 432 Hz tuning") {
        // A4 at 432 Hz alternate tuning
        REQUIRE(midiNoteToFrequency(69, 432.0f) == Approx(432.0f).margin(0.001f));

        // C4 with 432 Hz tuning
        // C4 = 432 * 2^(-9/12) = 256.87 Hz
        REQUIRE(midiNoteToFrequency(60, 432.0f) == Approx(256.87f).margin(0.1f));
    }

    SECTION("Octave relationship: note+12 doubles frequency") {
        float freqA4 = midiNoteToFrequency(69);
        float freqA5 = midiNoteToFrequency(81);  // A5 = A4 + 12
        REQUIRE(freqA5 == Approx(freqA4 * 2.0f).margin(0.01f));
    }

    SECTION("Fifth relationship: note+7 gives ~1.5x frequency") {
        // Perfect fifth = 7 semitones = 2^(7/12) = 1.4983 (just under 3:2)
        float freqA4 = midiNoteToFrequency(69);
        float freqE5 = midiNoteToFrequency(76);  // E5 = A4 + 7
        REQUIRE(freqE5 == Approx(freqA4 * 1.4983f).margin(0.01f));
    }

    SECTION("Function is constexpr") {
        constexpr float compiletimeFreq = midiNoteToFrequency(69);
        REQUIRE(compiletimeFreq == Approx(440.0f).margin(0.001f));
    }
}

// ==============================================================================
// velocityToGain Tests (T004)
// ==============================================================================

TEST_CASE("velocityToGain converts MIDI velocity to linear gain",
          "[dsp][core][midi_utils][velocityToGain]") {

    SECTION("Velocity 127 returns 1.0 (0 dB, full level)") {
        REQUIRE(velocityToGain(127) == 1.0f);
    }

    SECTION("Velocity 64 returns approximately 0.504 (-5.95 dB, within 0.1 dB of -6 dB)") {
        float gain = velocityToGain(64);
        // Linear mapping: 64/127 = 0.5039...
        REQUIRE(gain == Approx(64.0f / 127.0f).margin(0.001f));

        // Verify it's within 0.1 dB of -6 dB
        // -6 dB linear = 0.501 (from gainToDb(0.501) = -6.0)
        // gainToDb(64/127) = gainToDb(0.504) = -5.95 dB
        float db = gainToDb(gain);
        REQUIRE(db == Approx(-6.0f).margin(0.1f));
    }

    SECTION("Velocity 0 returns 0.0 (silence)") {
        REQUIRE(velocityToGain(0) == 0.0f);
    }

    SECTION("Velocity 1 returns minimum non-zero gain") {
        float gain = velocityToGain(1);
        REQUIRE(gain > 0.0f);
        REQUIRE(gain == Approx(1.0f / 127.0f).margin(0.0001f));
        // Approximately -42 dB
        REQUIRE(gainToDb(gain) == Approx(-42.0f).margin(0.5f));
    }

    SECTION("Clamping: negative velocity treated as 0") {
        REQUIRE(velocityToGain(-1) == 0.0f);
        REQUIRE(velocityToGain(-100) == 0.0f);
    }

    SECTION("Clamping: velocity > 127 treated as 127") {
        REQUIRE(velocityToGain(128) == 1.0f);
        REQUIRE(velocityToGain(200) == 1.0f);
    }

    SECTION("Linear relationship: double velocity gives double gain (approximately)") {
        float gain32 = velocityToGain(32);
        float gain64 = velocityToGain(64);
        // 64/127 / 32/127 = 2.0
        REQUIRE(gain64 == Approx(gain32 * 2.0f).margin(0.001f));
    }

    SECTION("Function is constexpr") {
        constexpr float compiletimeGain = velocityToGain(127);
        REQUIRE(compiletimeGain == 1.0f);
    }
}
