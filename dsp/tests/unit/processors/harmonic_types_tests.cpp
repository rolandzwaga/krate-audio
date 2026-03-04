// ==============================================================================
// Layer 2: DSP Processor Tests - Harmonic Types
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/processors/harmonic_types.h
// Spec: specs/115-innexus-m1-core-instrument/spec.md
// Covers: FR-013 (F0Estimate), FR-028 (Partial), FR-029 (HarmonicFrame)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/harmonic_types.h>

#include <cmath>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// F0Estimate Tests (FR-013)
// =============================================================================

TEST_CASE("F0Estimate default construction", "[dsp][harmonic_types][f0estimate]") {
    F0Estimate est;
    REQUIRE(est.frequency == 0.0f);
    REQUIRE(est.confidence == 0.0f);
    REQUIRE(est.voiced == false);
}

TEST_CASE("F0Estimate voiced/unvoiced constraints", "[dsp][harmonic_types][f0estimate]") {
    SECTION("unvoiced estimate has zero frequency") {
        F0Estimate est;
        est.frequency = 0.0f;
        est.confidence = 0.1f;
        est.voiced = false;
        // FR-013: frequency = 0 when voiced = false
        REQUIRE(est.frequency == 0.0f);
        REQUIRE(est.voiced == false);
    }

    SECTION("voiced estimate has positive frequency") {
        F0Estimate est;
        est.frequency = 440.0f;
        est.confidence = 0.85f;
        est.voiced = true;
        REQUIRE(est.frequency > 0.0f);
        REQUIRE(est.voiced == true);
    }
}

TEST_CASE("F0Estimate confidence range", "[dsp][harmonic_types][f0estimate]") {
    SECTION("confidence at minimum") {
        F0Estimate est;
        est.confidence = 0.0f;
        REQUIRE(est.confidence >= 0.0f);
        REQUIRE(est.confidence <= 1.0f);
    }

    SECTION("confidence at maximum") {
        F0Estimate est;
        est.confidence = 1.0f;
        REQUIRE(est.confidence >= 0.0f);
        REQUIRE(est.confidence <= 1.0f);
    }

    SECTION("confidence at midpoint") {
        F0Estimate est;
        est.confidence = 0.5f;
        REQUIRE(est.confidence >= 0.0f);
        REQUIRE(est.confidence <= 1.0f);
    }
}

TEST_CASE("F0Estimate outputs frequency, confidence, and voiced classification",
          "[dsp][harmonic_types][f0estimate]") {
    // FR-013: The pitch detector MUST output a frequency estimate (in Hz),
    // a confidence value (0.0 to 1.0), and a voiced/unvoiced classification
    F0Estimate est;
    est.frequency = 261.63f; // Middle C
    est.confidence = 0.92f;
    est.voiced = true;

    REQUIRE(est.frequency == Approx(261.63f));
    REQUIRE(est.confidence == Approx(0.92f));
    REQUIRE(est.voiced == true);
}

// =============================================================================
// Partial Tests (FR-028)
// =============================================================================

TEST_CASE("Partial default construction", "[dsp][harmonic_types][partial]") {
    Partial p;
    REQUIRE(p.harmonicIndex == 0);
    REQUIRE(p.frequency == 0.0f);
    REQUIRE(p.amplitude == 0.0f);
    REQUIRE(p.phase == 0.0f);
    REQUIRE(p.relativeFrequency == 0.0f);
    REQUIRE(p.inharmonicDeviation == 0.0f);
    REQUIRE(p.stability == 0.0f);
    REQUIRE(p.age == 0);
}

TEST_CASE("Partial field presence per FR-028", "[dsp][harmonic_types][partial]") {
    // FR-028: Each tracked partial MUST carry: harmonic index, measured frequency,
    // amplitude, phase, relative frequency (frequency / F0), stability score, and frame age.
    Partial p;
    p.harmonicIndex = 3;
    p.frequency = 1320.0f; // 3rd harmonic of 440 Hz
    p.amplitude = 0.25f;
    p.phase = 1.57f;
    p.relativeFrequency = 3.0f; // frequency / F0
    p.inharmonicDeviation = 0.0f; // relativeFrequency - harmonicIndex
    p.stability = 0.95f;
    p.age = 12;

    REQUIRE(p.harmonicIndex == 3);
    REQUIRE(p.frequency == Approx(1320.0f));
    REQUIRE(p.amplitude == Approx(0.25f));
    REQUIRE(p.phase == Approx(1.57f));
    REQUIRE(p.relativeFrequency == Approx(3.0f));
    REQUIRE(p.inharmonicDeviation == Approx(0.0f));
    REQUIRE(p.stability == Approx(0.95f));
    REQUIRE(p.age == 12);
}

TEST_CASE("Partial inharmonic deviation", "[dsp][harmonic_types][partial]") {
    SECTION("perfectly harmonic partial has zero deviation") {
        Partial p;
        p.harmonicIndex = 5;
        p.relativeFrequency = 5.0f;
        p.inharmonicDeviation = p.relativeFrequency - static_cast<float>(p.harmonicIndex);
        REQUIRE(p.inharmonicDeviation == Approx(0.0f));
    }

    SECTION("inharmonic partial has non-zero deviation") {
        // Piano string: 5th partial slightly sharp
        Partial p;
        p.harmonicIndex = 5;
        p.relativeFrequency = 5.07f;
        p.inharmonicDeviation = p.relativeFrequency - static_cast<float>(p.harmonicIndex);
        REQUIRE(p.inharmonicDeviation == Approx(0.07f).margin(0.001f));
    }
}

// =============================================================================
// HarmonicFrame Tests (FR-029)
// =============================================================================

TEST_CASE("HarmonicFrame default construction", "[dsp][harmonic_types][harmonic_frame]") {
    HarmonicFrame frame;
    REQUIRE(frame.f0 == 0.0f);
    REQUIRE(frame.f0Confidence == 0.0f);
    REQUIRE(frame.numPartials == 0);
    REQUIRE(frame.spectralCentroid == 0.0f);
    REQUIRE(frame.brightness == 0.0f);
    REQUIRE(frame.noisiness == 0.0f);
    REQUIRE(frame.globalAmplitude == 0.0f);
}

TEST_CASE("HarmonicFrame partials array has capacity 48", "[dsp][harmonic_types][harmonic_frame]") {
    HarmonicFrame frame;
    REQUIRE(frame.partials.size() == 48);
}

TEST_CASE("HarmonicFrame numPartials starts at 0", "[dsp][harmonic_types][harmonic_frame]") {
    HarmonicFrame frame;
    REQUIRE(frame.numPartials == 0);
}

TEST_CASE("HarmonicFrame descriptor fields per FR-029", "[dsp][harmonic_types][harmonic_frame]") {
    // FR-029: per-frame harmonic model containing: F0, F0 confidence, up to 48 partials
    // with their attributes, spectral centroid, brightness descriptor, noisiness estimate,
    // and smoothed global amplitude
    HarmonicFrame frame;
    frame.f0 = 440.0f;
    frame.f0Confidence = 0.95f;
    frame.numPartials = 3;
    frame.spectralCentroid = 1200.0f;
    frame.brightness = 2.73f; // centroid / f0
    frame.noisiness = 0.15f;
    frame.globalAmplitude = 0.8f;

    REQUIRE(frame.f0 == Approx(440.0f));
    REQUIRE(frame.f0Confidence == Approx(0.95f));
    REQUIRE(frame.numPartials == 3);
    REQUIRE(frame.spectralCentroid == Approx(1200.0f));
    REQUIRE(frame.brightness == Approx(2.73f));
    REQUIRE(frame.noisiness == Approx(0.15f));
    REQUIRE(frame.globalAmplitude == Approx(0.8f));
}

TEST_CASE("HarmonicFrame can store partials up to capacity", "[dsp][harmonic_types][harmonic_frame]") {
    HarmonicFrame frame;
    frame.f0 = 100.0f;
    frame.numPartials = 48;

    // Fill all 48 partials
    for (int i = 0; i < 48; ++i) {
        frame.partials[static_cast<size_t>(i)].harmonicIndex = i + 1;
        frame.partials[static_cast<size_t>(i)].frequency =
            static_cast<float>(i + 1) * 100.0f;
        frame.partials[static_cast<size_t>(i)].amplitude =
            1.0f / static_cast<float>(i + 1);
    }

    REQUIRE(frame.numPartials == 48);
    REQUIRE(frame.partials[0].harmonicIndex == 1);
    REQUIRE(frame.partials[0].frequency == Approx(100.0f));
    REQUIRE(frame.partials[47].harmonicIndex == 48);
    REQUIRE(frame.partials[47].frequency == Approx(4800.0f));
}

TEST_CASE("HarmonicFrame partials are default-initialized", "[dsp][harmonic_types][harmonic_frame]") {
    HarmonicFrame frame;
    // All partials should be default-constructed
    for (size_t i = 0; i < 48; ++i) {
        REQUIRE(frame.partials[i].harmonicIndex == 0);
        REQUIRE(frame.partials[i].frequency == 0.0f);
        REQUIRE(frame.partials[i].amplitude == 0.0f);
        REQUIRE(frame.partials[i].phase == 0.0f);
        REQUIRE(frame.partials[i].relativeFrequency == 0.0f);
        REQUIRE(frame.partials[i].inharmonicDeviation == 0.0f);
        REQUIRE(frame.partials[i].stability == 0.0f);
        REQUIRE(frame.partials[i].age == 0);
    }
}

// =============================================================================
// kMaxPartials constant
// =============================================================================

TEST_CASE("kMaxPartials constant equals 48", "[dsp][harmonic_types]") {
    REQUIRE(kMaxPartials == 48);
}
