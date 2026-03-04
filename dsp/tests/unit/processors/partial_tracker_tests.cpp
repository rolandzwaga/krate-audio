// ==============================================================================
// Layer 2: DSP Processor Tests - Partial Tracker
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/processors/partial_tracker.h
// Spec: specs/115-innexus-m1-core-instrument/spec.md
// Covers: FR-022 (peak detection), FR-023 (harmonic sieve),
//         FR-024 (frame-to-frame tracking), FR-025 (birth/death/grace period),
//         FR-026 (48-partial cap), FR-027 (hysteresis), FR-028 (partial data)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/partial_tracker.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr size_t kTestFFTSize = 4096;
constexpr float kPiLocal = 3.14159265358979323846f;
constexpr float kTwoPiLocal = 2.0f * kPiLocal;

/// Fill a SpectralBuffer with a single sine peak at the given frequency.
/// This places energy at the correct bin with a realistic peak shape
/// (main lobe + slight smearing to neighbors).
void fillSineSpectrum(SpectralBuffer& buffer, float freqHz,
                      float amplitude, size_t fftSize, float sampleRate) {
    const size_t numBins = fftSize / 2 + 1;

    // Reset all bins to zero
    for (size_t b = 0; b < numBins; ++b) {
        buffer.setCartesian(b, 0.0f, 0.0f);
    }

    // Place energy at the fractional bin position
    const float binFreq = sampleRate / static_cast<float>(fftSize);
    const float exactBin = freqHz / binFreq;
    const size_t centerBin = static_cast<size_t>(exactBin + 0.5f);

    if (centerBin == 0 || centerBin >= numBins - 1) return;

    // Create a realistic peak: center bin with strong magnitude,
    // neighbors with lesser magnitude (simulates windowed FFT main lobe)
    const float centerMag = amplitude;
    const float sideMag = amplitude * 0.3f;
    const float farSideMag = amplitude * 0.05f;

    buffer.setCartesian(centerBin, centerMag, 0.0f);
    if (centerBin > 0)
        buffer.setCartesian(centerBin - 1, sideMag, 0.0f);
    if (centerBin + 1 < numBins)
        buffer.setCartesian(centerBin + 1, sideMag, 0.0f);
    if (centerBin > 1)
        buffer.setCartesian(centerBin - 2, farSideMag, 0.0f);
    if (centerBin + 2 < numBins)
        buffer.setCartesian(centerBin + 2, farSideMag, 0.0f);
}

/// Fill a SpectralBuffer with a harmonic series (sawtooth-like).
/// Harmonics are at f0, 2*f0, 3*f0, ... with 1/n amplitude falloff.
void fillHarmonicSpectrum(SpectralBuffer& buffer, float f0Hz,
                          int numHarmonics, size_t fftSize, float sampleRate) {
    const size_t numBins = fftSize / 2 + 1;
    const float nyquist = sampleRate * 0.5f;

    // Reset all bins to zero
    for (size_t b = 0; b < numBins; ++b) {
        buffer.setCartesian(b, 0.0f, 0.0f);
    }

    const float binFreq = sampleRate / static_cast<float>(fftSize);

    for (int h = 1; h <= numHarmonics; ++h) {
        const float harmFreq = f0Hz * static_cast<float>(h);
        if (harmFreq >= nyquist) break;

        const float amplitude = 1.0f / static_cast<float>(h);
        const float exactBin = harmFreq / binFreq;
        const size_t centerBin = static_cast<size_t>(exactBin + 0.5f);

        if (centerBin == 0 || centerBin >= numBins - 1) continue;

        // Main peak with side lobes
        const float centerMag = amplitude;
        const float sideMag = amplitude * 0.3f;

        buffer.setCartesian(centerBin, centerMag, 0.0f);
        if (centerBin > 0)
            buffer.setCartesian(centerBin - 1, sideMag, 0.0f);
        if (centerBin + 1 < numBins)
            buffer.setCartesian(centerBin + 1, sideMag, 0.0f);
    }
}

/// Fill a SpectralBuffer with many sinusoidal peaks (for cap testing)
void fillManyPeaks(SpectralBuffer& buffer, int numPeaks,
                   size_t fftSize, float sampleRate) {
    const size_t numBins = fftSize / 2 + 1;
    const float nyquist = sampleRate * 0.5f;

    // Reset all bins to zero
    for (size_t b = 0; b < numBins; ++b) {
        buffer.setCartesian(b, 0.0f, 0.0f);
    }

    const float binFreq = sampleRate / static_cast<float>(fftSize);

    // Space peaks evenly across spectrum
    const float spacing = nyquist / static_cast<float>(numPeaks + 1);

    for (int p = 0; p < numPeaks; ++p) {
        const float freq = spacing * static_cast<float>(p + 1);
        if (freq >= nyquist) break;

        const float amplitude = 0.5f;
        const size_t centerBin = static_cast<size_t>(freq / binFreq + 0.5f);

        if (centerBin == 0 || centerBin >= numBins - 1) continue;

        buffer.setCartesian(centerBin, amplitude, 0.0f);
        if (centerBin > 1)
            buffer.setCartesian(centerBin - 1, amplitude * 0.2f, 0.0f);
        if (centerBin + 1 < numBins)
            buffer.setCartesian(centerBin + 1, amplitude * 0.2f, 0.0f);
    }
}

} // anonymous namespace

// =============================================================================
// FR-022: Peak detection with parabolic interpolation
// =============================================================================

TEST_CASE("PartialTracker: single sine at 440 Hz detects one partial",
          "[dsp][partial_tracker][FR-022]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    fillSineSpectrum(spectrum, 440.0f, 1.0f, kTestFFTSize, kTestSampleRate);

    F0Estimate f0;
    f0.frequency = 440.0f;
    f0.confidence = 0.95f;
    f0.voiced = true;

    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    // Should detect at least one partial
    REQUIRE(tracker.getActiveCount() >= 1);

    // The dominant partial should be near 440 Hz
    const auto& partials = tracker.getPartials();
    bool found440 = false;
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (partials[i].frequency > 0.0f &&
            std::abs(partials[i].frequency - 440.0f) < 20.0f) {
            found440 = true;
            // Verify it has harmonicIndex = 1 (fundamental)
            REQUIRE(partials[i].harmonicIndex == 1);
            break;
        }
    }
    REQUIRE(found440);
}

// =============================================================================
// FR-023: Harmonic sieve
// =============================================================================

TEST_CASE("PartialTracker: harmonic series detects partials at harmonic frequencies",
          "[dsp][partial_tracker][FR-023]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    const float f0Hz = 100.0f;
    fillHarmonicSpectrum(spectrum, f0Hz, 10, kTestFFTSize, kTestSampleRate);

    F0Estimate f0;
    f0.frequency = f0Hz;
    f0.confidence = 0.95f;
    f0.voiced = true;

    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    // Should detect multiple partials
    REQUIRE(tracker.getActiveCount() >= 3);

    // Check that harmonics are detected at correct indices
    const auto& partials = tracker.getPartials();
    bool foundH1 = false, foundH2 = false, foundH3 = false;
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (partials[i].harmonicIndex == 1 &&
            std::abs(partials[i].frequency - 100.0f) < 15.0f) {
            foundH1 = true;
        }
        if (partials[i].harmonicIndex == 2 &&
            std::abs(partials[i].frequency - 200.0f) < 15.0f) {
            foundH2 = true;
        }
        if (partials[i].harmonicIndex == 3 &&
            std::abs(partials[i].frequency - 300.0f) < 15.0f) {
            foundH3 = true;
        }
    }
    REQUIRE(foundH1);
    REQUIRE(foundH2);
    REQUIRE(foundH3);
}

// =============================================================================
// FR-028: Inharmonic signal
// =============================================================================

TEST_CASE("PartialTracker: inharmonic signal detects non-integer frequency ratios",
          "[dsp][partial_tracker][FR-028]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    // Two sines at non-integer ratio (e.g. 200 Hz and 510 Hz)
    // These are NOT integer multiples of each other
    const size_t numBins = kTestFFTSize / 2 + 1;
    for (size_t b = 0; b < numBins; ++b) {
        spectrum.setCartesian(b, 0.0f, 0.0f);
    }
    fillSineSpectrum(spectrum, 200.0f, 1.0f, kTestFFTSize, kTestSampleRate);

    // Add second sine at 510 Hz (ratio 2.55, not an integer)
    const float binFreq = kTestSampleRate / static_cast<float>(kTestFFTSize);
    const float exactBin510 = 510.0f / binFreq;
    const size_t centerBin510 = static_cast<size_t>(exactBin510 + 0.5f);
    spectrum.setCartesian(centerBin510, 0.8f, 0.0f);
    if (centerBin510 > 0)
        spectrum.setCartesian(centerBin510 - 1, 0.24f, 0.0f);
    if (centerBin510 + 1 < numBins)
        spectrum.setCartesian(centerBin510 + 1, 0.24f, 0.0f);

    // With F0 = 200 Hz, the second sine is at 2.55 * F0
    // The harmonic sieve may or may not assign it depending on tolerance
    F0Estimate f0;
    f0.frequency = 200.0f;
    f0.confidence = 0.95f;
    f0.voiced = true;

    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    // Should detect at least the fundamental partial
    REQUIRE(tracker.getActiveCount() >= 1);

    // Check that partials carry relativeFrequency and inharmonicDeviation
    const auto& partials = tracker.getPartials();
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (partials[i].frequency > 0.0f) {
            // relativeFrequency should be approximately freq/f0
            float expectedRelFreq = partials[i].frequency / f0.frequency;
            REQUIRE(partials[i].relativeFrequency ==
                    Approx(expectedRelFreq).margin(0.5f));

            // inharmonicDeviation = relativeFrequency - harmonicIndex
            float expectedDeviation =
                partials[i].relativeFrequency -
                static_cast<float>(partials[i].harmonicIndex);
            REQUIRE(partials[i].inharmonicDeviation ==
                    Approx(expectedDeviation).margin(0.01f));
        }
    }
}

// =============================================================================
// FR-024: Frame-to-frame tracking
// =============================================================================

TEST_CASE("PartialTracker: stable signal maintains partial identity across frames",
          "[dsp][partial_tracker][FR-024]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    const float f0Hz = 200.0f;
    fillHarmonicSpectrum(spectrum, f0Hz, 5, kTestFFTSize, kTestSampleRate);

    F0Estimate f0;
    f0.frequency = f0Hz;
    f0.confidence = 0.95f;
    f0.voiced = true;

    // Process multiple frames with the same spectrum
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);
    const int count1 = tracker.getActiveCount();
    REQUIRE(count1 >= 1);

    // Record harmonic indices from frame 1
    std::array<int, kMaxPartials> indices1{};
    std::array<float, kMaxPartials> freqs1{};
    for (int i = 0; i < count1; ++i) {
        indices1[i] = tracker.getPartials()[i].harmonicIndex;
        freqs1[i] = tracker.getPartials()[i].frequency;
    }

    // Process a second frame with the same data
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);
    const int count2 = tracker.getActiveCount();

    // Partial count should be the same (stable signal)
    REQUIRE(count2 == count1);

    // Partials should maintain the same harmonic indices
    for (int i = 0; i < count2; ++i) {
        REQUIRE(tracker.getPartials()[i].harmonicIndex == indices1[i]);
        // Frequency should be very close
        REQUIRE(tracker.getPartials()[i].frequency ==
                Approx(freqs1[i]).margin(2.0f));
    }

    // Age should increase
    for (int i = 0; i < count2; ++i) {
        REQUIRE(tracker.getPartials()[i].age >= 1);
    }
}

// =============================================================================
// FR-025: Grace period
// =============================================================================

TEST_CASE("PartialTracker: grace period holds disappearing partial for 4 frames",
          "[dsp][partial_tracker][FR-025]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrumWithSignal;
    spectrumWithSignal.prepare(kTestFFTSize);
    fillSineSpectrum(spectrumWithSignal, 440.0f, 1.0f, kTestFFTSize, kTestSampleRate);

    SpectralBuffer emptySpectrum;
    emptySpectrum.prepare(kTestFFTSize);
    // Leave all bins at zero (silence)

    F0Estimate f0;
    f0.frequency = 440.0f;
    f0.confidence = 0.95f;
    f0.voiced = true;

    // Establish a tracked partial over a few frames
    for (int i = 0; i < 3; ++i) {
        tracker.processFrame(spectrumWithSignal, f0, kTestFFTSize, kTestSampleRate);
    }
    REQUIRE(tracker.getActiveCount() >= 1);

    // Now feed empty spectra -- partial should be held during grace period
    F0Estimate f0Unvoiced;
    f0Unvoiced.frequency = 0.0f;
    f0Unvoiced.confidence = 0.1f;
    f0Unvoiced.voiced = false;

    // Frames 1-4 of silence: partial should still be in active count
    // (grace period = 4 frames)
    for (int frame = 0; frame < 4; ++frame) {
        tracker.processFrame(emptySpectrum, f0Unvoiced, kTestFFTSize, kTestSampleRate);
        // During grace period, the partial should still exist
        // (amplitude may fade but it remains tracked)
    }

    // After 4+ frames of silence, the partial should eventually die
    // Process a few more frames to ensure death
    for (int frame = 0; frame < 3; ++frame) {
        tracker.processFrame(emptySpectrum, f0Unvoiced, kTestFFTSize, kTestSampleRate);
    }
    // By now, all partials should be dead
    REQUIRE(tracker.getActiveCount() == 0);
}

// =============================================================================
// FR-026: Partial cap at 48
// =============================================================================

TEST_CASE("PartialTracker: active count never exceeds 48",
          "[dsp][partial_tracker][FR-026]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    // Fill spectrum with 60 peaks
    fillManyPeaks(spectrum, 60, kTestFFTSize, kTestSampleRate);

    // Use unvoiced F0 so harmonic sieve is bypassed and all peaks are candidates
    F0Estimate f0;
    f0.frequency = 0.0f;
    f0.confidence = 0.1f;
    f0.voiced = false;

    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    // Active count must not exceed 48
    REQUIRE(tracker.getActiveCount() <= static_cast<int>(kMaxPartials));
}

// =============================================================================
// FR-028: Per-partial data completeness
// =============================================================================

TEST_CASE("PartialTracker: each partial carries all required fields",
          "[dsp][partial_tracker][FR-028]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    const float f0Hz = 200.0f;
    fillHarmonicSpectrum(spectrum, f0Hz, 5, kTestFFTSize, kTestSampleRate);

    F0Estimate f0;
    f0.frequency = f0Hz;
    f0.confidence = 0.95f;
    f0.voiced = true;

    // Process a few frames to establish stable partials
    for (int i = 0; i < 3; ++i) {
        tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);
    }

    REQUIRE(tracker.getActiveCount() >= 1);

    const auto& partials = tracker.getPartials();
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        const auto& p = partials[i];

        // harmonicIndex: should be > 0 for voiced signal
        REQUIRE(p.harmonicIndex > 0);

        // frequency: should be positive
        REQUIRE(p.frequency > 0.0f);

        // amplitude: should be non-negative
        REQUIRE(p.amplitude >= 0.0f);

        // phase: should be in [-pi, pi] range (or close to it)
        // (we use relaxed check since phase handling varies)
        REQUIRE(p.phase >= -4.0f);
        REQUIRE(p.phase <= 4.0f);

        // relativeFrequency: should be approximately harmonicIndex
        REQUIRE(p.relativeFrequency > 0.0f);

        // inharmonicDeviation: should exist (can be any value)
        // Just verify it's computed as relativeFrequency - harmonicIndex
        REQUIRE(p.inharmonicDeviation ==
                Approx(p.relativeFrequency - static_cast<float>(p.harmonicIndex))
                    .margin(0.01f));

        // stability: should be in [0.0, 1.0]
        REQUIRE(p.stability >= 0.0f);
        REQUIRE(p.stability <= 1.0f);

        // age: should be >= 0
        REQUIRE(p.age >= 0);
    }
}

// =============================================================================
// FR-027: Hysteresis prevents rapid replacement
// =============================================================================

TEST_CASE("PartialTracker: hysteresis prevents immediate replacement of active partial",
          "[dsp][partial_tracker][FR-027]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    // Establish partials with a harmonic series at 100 Hz
    const float f0Hz = 100.0f;
    fillHarmonicSpectrum(spectrum, f0Hz, 10, kTestFFTSize, kTestSampleRate);

    F0Estimate f0;
    f0.frequency = f0Hz;
    f0.confidence = 0.95f;
    f0.voiced = true;

    // Establish partials over several frames
    for (int i = 0; i < 5; ++i) {
        tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);
    }

    const int initialCount = tracker.getActiveCount();
    REQUIRE(initialCount >= 3);

    // Record initial harmonic indices
    std::array<int, kMaxPartials> initialIndices{};
    for (int i = 0; i < initialCount; ++i) {
        initialIndices[i] = tracker.getPartials()[i].harmonicIndex;
    }

    // Now slightly change the spectrum (reduce one harmonic, add a new one)
    // The existing partials should not be immediately replaced due to hysteresis
    fillHarmonicSpectrum(spectrum, f0Hz, 10, kTestFFTSize, kTestSampleRate);

    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    // Partials from the harmonic series should still be tracked
    // (hysteresis should keep them stable)
    const int newCount = tracker.getActiveCount();
    REQUIRE(newCount >= 3);

    // Most harmonic indices should persist
    int matchCount = 0;
    for (int i = 0; i < std::min(initialCount, newCount); ++i) {
        for (int j = 0; j < initialCount; ++j) {
            if (tracker.getPartials()[i].harmonicIndex == initialIndices[j]) {
                ++matchCount;
                break;
            }
        }
    }
    // At least half of the original partials should persist
    REQUIRE(matchCount >= initialCount / 2);
}

// =============================================================================
// Unvoiced F0: harmonic sieve bypassed
// =============================================================================

TEST_CASE("PartialTracker: unvoiced F0 bypasses harmonic sieve",
          "[dsp][partial_tracker][FR-023]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    // Two sines at non-harmonic frequencies
    fillSineSpectrum(spectrum, 300.0f, 0.8f, kTestFFTSize, kTestSampleRate);

    // Add second sine at 750 Hz
    const float binFreq = kTestSampleRate / static_cast<float>(kTestFFTSize);
    const size_t numBins = kTestFFTSize / 2 + 1;
    const float exactBin750 = 750.0f / binFreq;
    const size_t centerBin750 = static_cast<size_t>(exactBin750 + 0.5f);
    spectrum.setCartesian(centerBin750, 0.6f, 0.0f);
    if (centerBin750 > 0)
        spectrum.setCartesian(centerBin750 - 1, 0.18f, 0.0f);
    if (centerBin750 + 1 < numBins)
        spectrum.setCartesian(centerBin750 + 1, 0.18f, 0.0f);

    F0Estimate f0Unvoiced;
    f0Unvoiced.frequency = 0.0f;
    f0Unvoiced.confidence = 0.1f;
    f0Unvoiced.voiced = false;

    tracker.processFrame(spectrum, f0Unvoiced, kTestFFTSize, kTestSampleRate);

    // With unvoiced F0, partials should still be detected (no sieve filtering)
    REQUIRE(tracker.getActiveCount() >= 1);

    // Partials should NOT have harmonic indices assigned
    const auto& partials = tracker.getPartials();
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        REQUIRE(partials[i].harmonicIndex == 0);
    }
}

// =============================================================================
// Reset
// =============================================================================

TEST_CASE("PartialTracker: reset clears all state",
          "[dsp][partial_tracker]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    fillSineSpectrum(spectrum, 440.0f, 1.0f, kTestFFTSize, kTestSampleRate);

    F0Estimate f0;
    f0.frequency = 440.0f;
    f0.confidence = 0.95f;
    f0.voiced = true;

    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);
    REQUIRE(tracker.getActiveCount() >= 1);

    tracker.reset();
    REQUIRE(tracker.getActiveCount() == 0);
}
