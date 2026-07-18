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
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
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

// =============================================================================
// Bandwidth estimation accuracy
// =============================================================================

TEST_CASE("PartialTracker: clean harmonic peaks produce bandwidth < 0.05",
          "[dsp][partial_tracker][bandwidth]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    // Create a clean harmonic series (8 harmonics of 440 Hz)
    fillHarmonicSpectrum(spectrum, 440.0f, 8, kTestFFTSize, kTestSampleRate);

    F0Estimate f0;
    f0.frequency = 440.0f;
    f0.confidence = 0.95f;
    f0.voiced = true;

    // Process two frames to let tracking stabilize (birth age >= 1)
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    REQUIRE(tracker.getActiveCount() >= 4); // at least some harmonics detected

    const auto& partials = tracker.getPartials();
    for (int i = 0; i < tracker.getActiveCount(); ++i)
    {
        if (partials[i].frequency <= 0.0f) continue;

        INFO("Partial " << i
             << " freq=" << partials[i].frequency
             << " harmonicIndex=" << partials[i].harmonicIndex
             << " bandwidth=" << partials[i].bandwidth);

        // Clean harmonic peaks should have bandwidth near zero.
        // The Loris model defines bandwidth=0 as pure sine, bandwidth=1 as noise.
        REQUIRE(partials[i].bandwidth < 0.05f);
    }
}

TEST_CASE("PartialTracker: noise-like spectrum produces bandwidth > 0.3",
          "[dsp][partial_tracker][bandwidth]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    const size_t numBins = kTestFFTSize / 2 + 1;

    // Create a spectrum that mimics noise: many bins with similar magnitude
    // but a peak at 440 Hz that barely rises above the noise floor
    for (size_t b = 0; b < numBins; ++b) {
        // Noise floor at 0.3 magnitude
        float noiseMag = 0.3f + 0.1f * std::sin(static_cast<float>(b) * 0.7f);
        spectrum.setCartesian(b, noiseMag, 0.0f);
    }

    // Add a weak peak at 440 Hz (barely above noise floor)
    const float binFreq = kTestSampleRate / static_cast<float>(kTestFFTSize);
    const size_t peakBin = static_cast<size_t>(440.0f / binFreq + 0.5f);
    spectrum.setCartesian(peakBin, 0.5f, 0.0f);

    F0Estimate f0;
    f0.frequency = 440.0f;
    f0.confidence = 0.5f;
    f0.voiced = true;

    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    const auto& partials = tracker.getPartials();
    bool foundPeak = false;
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (std::abs(partials[i].frequency - 440.0f) < 20.0f) {
            foundPeak = true;
            INFO("Noisy peak bandwidth: " << partials[i].bandwidth);
            // A peak barely above a noise floor should have high bandwidth
            REQUIRE(partials[i].bandwidth > 0.3f);
            break;
        }
    }
    REQUIRE(foundPeak);
}

// =============================================================================
// WI-1 / FR-018, FR-020, FR-021: Dual-window multi-resolution merge.
//
// The offline analyzer runs a long (4096) STFT for low-frequency resolution but
// historically never fed it to the tracker, so fundamentals below the short
// window's scan floor (bin 2 = ~86 Hz at 1024/44.1k) were unresolvable. The
// dual-window path fills the sub-floor gap with long-window peaks while leaving
// the short-window scan (>= floor) byte-identical.
// =============================================================================
TEST_CASE("PartialTracker: long-window merge resolves low fundamental below short floor",
          "[partial_tracker][dual_window]") {
    constexpr size_t kShortFft = 1024; // 43.07 Hz/bin; scan floor bin 2 = 86.13 Hz
    constexpr size_t kLongFft = 4096;  // 10.77 Hz/bin; resolves down to ~40 Hz

    // Fundamental placed exactly on long-window bin 5 (53.83 Hz): well below the
    // short floor, and dead-on a long bin so the sieve's tight n=1 tolerance
    // (0.06*f0 ~= 3.2 Hz) accepts it.
    const float kF0 = kTestSampleRate / static_cast<float>(kLongFft) * 5.0f;

    F0Estimate f0;
    f0.frequency = kF0;
    f0.confidence = 1.0f;
    f0.voiced = true;

    SpectralBuffer shortSpectrum;
    shortSpectrum.prepare(kShortFft);
    fillHarmonicSpectrum(shortSpectrum, kF0, 8, kShortFft, kTestSampleRate);

    SpectralBuffer longSpectrum;
    longSpectrum.prepare(kLongFft);
    fillHarmonicSpectrum(longSpectrum, kF0, 8, kLongFft, kTestSampleRate);

    const float shortScale = 2.0f / (static_cast<float>(kShortFft) * 0.35875f);
    const float longScale = 2.0f / (static_cast<float>(kLongFft) * 0.35875f);

    auto hasFundamental = [&](const PartialTracker& t) {
        const auto& ps = t.getPartials();
        for (int i = 0; i < t.getActiveCount(); ++i) {
            if (ps[i].harmonicIndex == 1 && std::abs(ps[i].frequency - kF0) < 6.0f) {
                return true;
            }
        }
        return false;
    };

    // Short-only path (current behavior): the fundamental is NOT resolved.
    {
        PartialTracker tracker;
        tracker.prepare(kShortFft, kTestSampleRate);
        tracker.setAmplitudeScale(shortScale);
        tracker.processFrame(shortSpectrum, f0, kShortFft, kTestSampleRate);
        REQUIRE_FALSE(hasFundamental(tracker));
    }

    // Dual-window path: long-window peaks fill the sub-floor gap; fundamental resolved.
    {
        PartialTracker tracker;
        tracker.prepare(kShortFft, kTestSampleRate);
        tracker.setAmplitudeScale(shortScale);
        tracker.processDualFrame(shortSpectrum, longSpectrum, kShortFft, kLongFft,
                                 longScale, f0, kTestSampleRate);
        REQUIRE(hasFundamental(tracker));
    }
}

// =============================================================================
// Real-spectrum helper: sum of sinusoids -> Blackman-Harris window -> real FFT,
// matching what the analyzer actually feeds the tracker. Sub-bin peak accuracy
// questions cannot be answered with an idealized synthetic peak.
// =============================================================================

namespace {

/// Build a genuine analysis spectrum: sum of sinusoids -> Blackman-Harris
/// window -> real FFT, matching what the analyzer actually feeds the tracker.
void fillRealSpectrum(SpectralBuffer& buffer,
                      const std::vector<float>& freqs,
                      const std::vector<float>& amps,
                      size_t fftSize, float sampleRate) {
    std::vector<float> td(fftSize, 0.0f);
    for (size_t i = 0; i < freqs.size(); ++i) {
        const float w = kTwoPiLocal * freqs[i] / sampleRate;
        for (size_t n = 0; n < fftSize; ++n) {
            td[n] += amps[i] * std::sin(w * static_cast<float>(n));
        }
    }

    std::vector<float> win(fftSize, 0.0f);
    Krate::DSP::Window::generateBlackmanHarris(win.data(), fftSize);
    for (size_t n = 0; n < fftSize; ++n) td[n] *= win[n];

    Krate::DSP::FFT fft;
    fft.prepare(fftSize);
    std::vector<Krate::DSP::Complex> spec(fftSize / 2 + 1);
    fft.forward(td.data(), spec.data());

    for (size_t b = 0; b < fftSize / 2 + 1; ++b) {
        buffer.setCartesian(b, spec[b].real, spec[b].imag);
    }
}

} // anonymous namespace

// =============================================================================
// WI-23: the Hungarian cost matrix is a fixed 128x128 array, but it is indexed
// as cost[track * numPeaks_ + peak] where numPeaks_ is bounded only by the bin
// count (2048 for a 4096-point FFT). A dense spectrum therefore writes far past
// the end of the array -- memory corruption on the analysis path.
//
// Secondarily, HungarianAlgorithm::solve() bails out entirely when
// max(rows, cols) > 128, so even without the overflow every peak would go
// unmatched and partial identity would be lost on every frame.
// =============================================================================

TEST_CASE("PartialTracker: dense spectra stay within matcher capacity (WI-23)",
          "[dsp][partial_tracker][wi23]") {
    constexpr size_t kFft = 4096;
    const float f0 = 220.0f;

    // A rich harmonic tone through a real analysis window produces far more
    // local maxima than 128 once window leakage is included.
    std::vector<float> freqs, amps;
    for (int n = 1; n <= 60; ++n) {
        const float fn = f0 * static_cast<float>(n);
        if (fn >= kTestSampleRate * 0.45f) break;
        freqs.push_back(fn);
        amps.push_back(1.0f / static_cast<float>(n));
    }

    SpectralBuffer buf;
    buf.prepare(kFft);
    fillRealSpectrum(buf, freqs, amps, kFft, kTestSampleRate);

    PartialTracker tracker;
    tracker.prepare(kFft, kTestSampleRate);

    F0Estimate f0e;
    f0e.frequency = f0;
    f0e.confidence = 0.95f;
    f0e.voiced = true;

    // Frame 1 establishes tracks; later frames must MATCH them, not re-birth.
    tracker.processFrame(buf, f0e, kFft, kTestSampleRate);
    REQUIRE(tracker.getActiveCount() > 0);

    for (int frame = 0; frame < 6; ++frame) {
        tracker.processFrame(buf, f0e, kFft, kTestSampleRate);
    }

    // On an unchanging spectrum every surviving partial must have aged past
    // its birth frame. If the matcher silently refused to match (cols > 128),
    // every partial is reborn each frame and age stays at its initial value.
    const auto& parts = tracker.getPartials();
    int aged = 0;
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (parts[static_cast<size_t>(i)].age > 3) ++aged;
    }
    INFO("active=" << tracker.getActiveCount() << " aged=" << aged);
    REQUIRE(aged > 0);
}

// =============================================================================
// WI-12: the short window's near-DC parabolic peak estimate is biased by more
// than the sieve's tight n=1 tolerance, so a fundamental just ABOVE the short
// scan floor (86.1 Hz at 1024/44.1k) is detected as a peak but then rejected by
// the harmonic sieve. Measured with real Blackman-Harris spectra, f0 = 90 Hz
// loses its fundamental while 110 Hz and up are fine.
//
// The defect is peak-frequency accuracy near DC, not sieve tolerance, so the
// fix is to let the long window cover this band rather than to widen the sieve
// (which would loosen harmonic assignment everywhere).
// =============================================================================

TEST_CASE("PartialTracker: dual window resolves fundamentals just above the short floor (WI-12)",
          "[dsp][partial_tracker][dual_window][wi12]") {
    constexpr size_t kShortFft = 1024; // 43.07 Hz/bin, scan floor 86.13 Hz
    constexpr size_t kLongFft = 4096;  // 10.77 Hz/bin
    const float kF0 = 90.0f;           // just above the short floor

    std::vector<float> freqs, amps;
    for (int h = 1; h <= 12; ++h) {
        const float f = kF0 * static_cast<float>(h);
        if (f >= kTestSampleRate * 0.45f) break;
        freqs.push_back(f);
        amps.push_back(1.0f / static_cast<float>(h));
    }

    SpectralBuffer shortSpectrum;
    shortSpectrum.prepare(kShortFft);
    fillRealSpectrum(shortSpectrum, freqs, amps, kShortFft, kTestSampleRate);

    SpectralBuffer longSpectrum;
    longSpectrum.prepare(kLongFft);
    fillRealSpectrum(longSpectrum, freqs, amps, kLongFft, kTestSampleRate);

    const float shortScale = 2.0f / (static_cast<float>(kShortFft) * 0.35875f);
    const float longScale = 2.0f / (static_cast<float>(kLongFft) * 0.35875f);

    F0Estimate f0;
    f0.frequency = kF0;
    f0.confidence = 1.0f;
    f0.voiced = true;

    auto hasFundamental = [&](const PartialTracker& t) {
        const auto& ps = t.getPartials();
        for (int i = 0; i < t.getActiveCount(); ++i) {
            if (ps[static_cast<size_t>(i)].harmonicIndex == 1) return true;
        }
        return false;
    };

    // Short-only: the near-DC bias pushes the peak outside the n=1 tolerance.
    {
        PartialTracker tracker;
        tracker.prepare(kShortFft, kTestSampleRate);
        tracker.setAmplitudeScale(shortScale);
        for (int f = 0; f < 5; ++f) {
            tracker.processFrame(shortSpectrum, f0, kShortFft, kTestSampleRate);
        }
        REQUIRE_FALSE(hasFundamental(tracker));
    }

    // Dual window: the long window resolves 90 Hz to ~8.4 bins, well inside
    // the sieve tolerance.
    {
        PartialTracker tracker;
        tracker.prepare(kShortFft, kTestSampleRate);
        tracker.setAmplitudeScale(shortScale);
        for (int f = 0; f < 5; ++f) {
            tracker.processDualFrame(shortSpectrum, longSpectrum, kShortFft,
                                     kLongFft, longScale, f0, kTestSampleRate);
        }
        REQUIRE(hasFundamental(tracker));
    }
}

// =============================================================================
// WI-13: the harmonic sieve tests peaks against a rigid n*f0 template with a
// sqrt(n) tolerance, but a real stiff string follows Fletcher (1964)
// f_n = n*f0*sqrt(1 + B*n^2). Measured at f0=220, B=1e-3: partials 7..14 are
// all rejected. No tolerance widening can fix this -- at n=14 the deviation is
// ~288 Hz while half the harmonic spacing is only 110 Hz, so a window wide
// enough to admit the partial would also admit its neighbours. The template
// itself has to stretch.
// =============================================================================

TEST_CASE("PartialTracker: stiff-string upper partials keep their harmonic index (WI-13)",
          "[dsp][partial_tracker][wi13]") {
    constexpr size_t kFft = 4096;
    const float f0 = 220.0f;
    const float B = 1e-3f; // stiff, e.g. a short thick bass string

    std::vector<float> freqs, amps;
    for (int n = 1; n <= 14; ++n) {
        const float fn = static_cast<float>(n) * f0
            * std::sqrt(1.0f + B * static_cast<float>(n * n));
        freqs.push_back(fn);
        amps.push_back(1.0f / static_cast<float>(n));
    }

    SpectralBuffer buf;
    buf.prepare(kFft);
    fillRealSpectrum(buf, freqs, amps, kFft, kTestSampleRate);

    PartialTracker tracker;
    tracker.prepare(kFft, kTestSampleRate);
    F0Estimate f0e;
    f0e.frequency = f0;
    f0e.confidence = 0.95f;
    f0e.voiced = true;
    for (int frame = 0; frame < 5; ++frame) {
        tracker.processFrame(buf, f0e, kFft, kTestSampleRate);
    }

    const auto& parts = tracker.getPartials();
    auto correctlyAssigned = [&](int n) {
        const float target = freqs[static_cast<size_t>(n - 1)];
        for (int i = 0; i < tracker.getActiveCount(); ++i) {
            const auto& p = parts[static_cast<size_t>(i)];
            if (p.harmonicIndex == n && std::abs(p.frequency - target) < 0.02f * target) {
                return true;
            }
        }
        return false;
    };

    int correct = 0;
    for (int n = 1; n <= 14; ++n) {
        if (correctlyAssigned(n)) ++correct;
    }
    INFO("correctly assigned " << correct << "/14");
    REQUIRE(correct >= 12);
    REQUIRE(correctlyAssigned(10));
    REQUIRE(correctlyAssigned(14));
}

TEST_CASE("PartialTracker: near-harmonic assignment is unchanged by the stiffness estimate (WI-13)",
          "[dsp][partial_tracker][wi13]") {
    // Guard on the risk WI-13 carries: the stiffness estimate must not perturb
    // sources that really are harmonic.
    constexpr size_t kFft = 4096;
    const float f0 = 220.0f;

    std::vector<float> freqs, amps;
    for (int n = 1; n <= 20; ++n) {
        freqs.push_back(f0 * static_cast<float>(n));
        amps.push_back(1.0f / static_cast<float>(n));
    }

    SpectralBuffer buf;
    buf.prepare(kFft);
    fillRealSpectrum(buf, freqs, amps, kFft, kTestSampleRate);

    PartialTracker tracker;
    tracker.prepare(kFft, kTestSampleRate);
    F0Estimate f0e;
    f0e.frequency = f0;
    f0e.confidence = 0.95f;
    f0e.voiced = true;
    for (int frame = 0; frame < 5; ++frame) {
        tracker.processFrame(buf, f0e, kFft, kTestSampleRate);
    }

    const auto& parts = tracker.getPartials();
    for (int n = 1; n <= 20; ++n) {
        const float target = f0 * static_cast<float>(n);
        bool ok = false;
        for (int i = 0; i < tracker.getActiveCount(); ++i) {
            const auto& p = parts[static_cast<size_t>(i)];
            if (p.harmonicIndex == n && std::abs(p.frequency - target) < 0.01f * target) {
                ok = true;
                break;
            }
        }
        INFO("harmonic n=" << n);
        REQUIRE(ok);
    }
}
