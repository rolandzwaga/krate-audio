// ==============================================================================
// Tests: Sweep LFO (User Story 9)
// ==============================================================================
// Tests for sweep frequency modulation via internal LFO.
//
// Reference: specs/007-sweep-system/spec.md (FR-024, FR-025, SC-015)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/sweep_lfo.h"

#include <cmath>

using Catch::Approx;
using namespace Disrumpo;

namespace {
constexpr double kTestSampleRate = 44100.0;
constexpr int kTestBlockSize = 512;
}

// ==============================================================================
// FR-024: LFO Rate Range (0.01Hz - 20Hz free, tempo-synced)
// ==============================================================================

TEST_CASE("SweepLFO rate range", "[sweep][lfo]") {
    SweepLFO lfo;
    lfo.prepare(kTestSampleRate);

    SECTION("Free mode supports 0.01Hz to 20Hz") {
        lfo.setTempoSync(false);

        // Minimum rate
        lfo.setRate(0.01f);
        REQUIRE(lfo.getRate() == Approx(0.01f).margin(0.001f));

        // Maximum rate
        lfo.setRate(20.0f);
        REQUIRE(lfo.getRate() == Approx(20.0f).margin(0.01f));

        // Out of range clamping
        lfo.setRate(0.001f);
        REQUIRE(lfo.getRate() >= 0.01f);

        lfo.setRate(50.0f);
        REQUIRE(lfo.getRate() <= 20.0f);
    }

    SECTION("Tempo sync mode uses note values") {
        lfo.setTempoSync(true);
        lfo.setTempo(120.0f);

        // Quarter note at 120 BPM = 0.5 seconds = 2 Hz
        lfo.setNoteValue(Krate::DSP::NoteValue::Quarter);
        // Rate should be derived from tempo and note value
        REQUIRE(lfo.isTempoSynced());
    }
}

// ==============================================================================
// FR-025: LFO Waveform Shapes
// ==============================================================================

TEST_CASE("SweepLFO waveform shapes", "[sweep][lfo]") {
    SweepLFO lfo;
    lfo.prepare(kTestSampleRate);
    lfo.setEnabled(true);
    lfo.setDepth(1.0f);  // Full depth for testing

    SECTION("Sine waveform produces smooth oscillation") {
        lfo.setWaveform(Krate::DSP::Waveform::Sine);
        lfo.setRate(1.0f);  // 1 Hz

        // Process samples and verify output is in [-1, 1]
        for (int i = 0; i < static_cast<int>(kTestSampleRate); ++i) {
            float value = lfo.process();
            REQUIRE(value >= -1.0f);
            REQUIRE(value <= 1.0f);
        }
    }

    SECTION("Triangle waveform") {
        lfo.setWaveform(Krate::DSP::Waveform::Triangle);
        lfo.setRate(1.0f);

        for (int i = 0; i < static_cast<int>(kTestSampleRate); ++i) {
            float value = lfo.process();
            REQUIRE(value >= -1.0f);
            REQUIRE(value <= 1.0f);
        }
    }

    SECTION("Sawtooth waveform") {
        lfo.setWaveform(Krate::DSP::Waveform::Sawtooth);
        lfo.setRate(1.0f);

        for (int i = 0; i < static_cast<int>(kTestSampleRate); ++i) {
            float value = lfo.process();
            REQUIRE(value >= -1.0f);
            REQUIRE(value <= 1.0f);
        }
    }

    SECTION("Square waveform") {
        lfo.setWaveform(Krate::DSP::Waveform::Square);
        lfo.setRate(1.0f);

        for (int i = 0; i < static_cast<int>(kTestSampleRate); ++i) {
            float value = lfo.process();
            REQUIRE(value >= -1.0f);
            REQUIRE(value <= 1.0f);
        }
    }

    SECTION("Sample & Hold waveform") {
        lfo.setWaveform(Krate::DSP::Waveform::SampleHold);
        lfo.setRate(1.0f);

        for (int i = 0; i < static_cast<int>(kTestSampleRate); ++i) {
            float value = lfo.process();
            REQUIRE(value >= -1.0f);
            REQUIRE(value <= 1.0f);
        }
    }

    SECTION("Smooth Random waveform") {
        lfo.setWaveform(Krate::DSP::Waveform::SmoothRandom);
        lfo.setRate(1.0f);

        for (int i = 0; i < static_cast<int>(kTestSampleRate); ++i) {
            float value = lfo.process();
            REQUIRE(value >= -1.0f);
            REQUIRE(value <= 1.0f);
        }
    }
}

// ==============================================================================
// SC-015: LFO Rate Accuracy
// ==============================================================================

TEST_CASE("SweepLFO rate accuracy", "[sweep][lfo]") {
    SweepLFO lfo;
    lfo.prepare(kTestSampleRate);
    lfo.setEnabled(true);
    lfo.setWaveform(Krate::DSP::Waveform::Sine);
    lfo.setDepth(1.0f);

    SECTION("1 Hz rate completes one cycle per second") {
        lfo.setRate(1.0f);
        lfo.reset();

        // Track zero crossings
        int zeroCrossings = 0;
        float prevValue = lfo.process();

        for (int i = 1; i < static_cast<int>(kTestSampleRate); ++i) {
            float value = lfo.process();
            if ((prevValue < 0.0f && value >= 0.0f) ||
                (prevValue >= 0.0f && value < 0.0f)) {
                zeroCrossings++;
            }
            prevValue = value;
        }

        // Sine wave has 2 zero crossings per cycle
        // 1 Hz = 2 crossings in 1 second
        REQUIRE(zeroCrossings == Approx(2).margin(1));
    }

    SECTION("2 Hz rate produces two cycles per second") {
        lfo.setRate(2.0f);
        lfo.reset();

        int zeroCrossings = 0;
        float prevValue = lfo.process();

        for (int i = 1; i < static_cast<int>(kTestSampleRate); ++i) {
            float value = lfo.process();
            if ((prevValue < 0.0f && value >= 0.0f) ||
                (prevValue >= 0.0f && value < 0.0f)) {
                zeroCrossings++;
            }
            prevValue = value;
        }

        // 2 Hz = 4 crossings in 1 second
        REQUIRE(zeroCrossings == Approx(4).margin(1));
    }
}

// ==============================================================================
// Depth Parameter
// ==============================================================================

TEST_CASE("SweepLFO depth parameter", "[sweep][lfo]") {
    SweepLFO lfo;
    lfo.prepare(kTestSampleRate);
    lfo.setEnabled(true);
    lfo.setWaveform(Krate::DSP::Waveform::Sine);
    lfo.setRate(10.0f);  // Fast rate for quick testing

    SECTION("Zero depth produces zero modulation") {
        lfo.setDepth(0.0f);

        float maxValue = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            float value = std::abs(lfo.process());
            if (value > maxValue) maxValue = value;
        }

        REQUIRE(maxValue < 0.01f);  // Near zero
    }

    SECTION("50% depth scales output") {
        lfo.setDepth(0.5f);
        lfo.reset();

        float maxValue = 0.0f;
        for (int i = 0; i < 10000; ++i) {
            float value = std::abs(lfo.process());
            if (value > maxValue) maxValue = value;
        }

        REQUIRE(maxValue == Approx(0.5f).margin(0.1f));
    }

    SECTION("100% depth produces full range") {
        lfo.setDepth(1.0f);
        lfo.reset();

        float maxValue = 0.0f;
        for (int i = 0; i < 10000; ++i) {
            float value = std::abs(lfo.process());
            if (value > maxValue) maxValue = value;
        }

        REQUIRE(maxValue == Approx(1.0f).margin(0.1f));
    }
}

// ==============================================================================
// Frequency Modulation Output
// ==============================================================================

TEST_CASE("SweepLFO frequency modulation", "[sweep][lfo]") {
    SweepLFO lfo;
    lfo.prepare(kTestSampleRate);
    lfo.setEnabled(true);
    lfo.setWaveform(Krate::DSP::Waveform::Sine);
    lfo.setRate(1.0f);
    lfo.setDepth(1.0f);

    SECTION("getModulatedFrequency returns frequency in range") {
        constexpr float baseFreq = 1000.0f;  // 1 kHz base

        for (int i = 0; i < 1000; ++i) {
            float modFreq = lfo.getModulatedFrequency(baseFreq);

            // Should be within sweep frequency range
            REQUIRE(modFreq >= 20.0f);
            REQUIRE(modFreq <= 20000.0f);
        }
    }
}
