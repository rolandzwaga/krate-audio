// ==============================================================================
// Layer 2: DSP Processor Tests - Partial Tracker Polyphonic Upgrades
// ==============================================================================
// Tests for Hungarian algorithm matching, linear prediction, and
// bandwidth estimation in the upgraded PartialTracker.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/partial_tracker.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <array>
#include <cmath>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr size_t kTestFFTSize = 4096;
constexpr float kPiLocal = 3.14159265358979323846f;

/// Fill a SpectralBuffer with a single sine peak at the given frequency.
void fillSineSpectrum(SpectralBuffer& buffer, float freqHz,
                      float amplitude, size_t fftSize, float sampleRate) {
    const size_t numBins = fftSize / 2 + 1;
    for (size_t b = 0; b < numBins; ++b) {
        buffer.setCartesian(b, 0.0f, 0.0f);
    }

    const float binSpacing = sampleRate / static_cast<float>(fftSize);
    const float exactBin = freqHz / binSpacing;
    const size_t centerBin = static_cast<size_t>(exactBin + 0.5f);

    if (centerBin >= 2 && centerBin < numBins - 2) {
        buffer.setCartesian(centerBin, amplitude, 0.0f);
        buffer.setCartesian(centerBin - 1, amplitude * 0.5f, 0.0f);
        buffer.setCartesian(centerBin + 1, amplitude * 0.5f, 0.0f);
        buffer.setCartesian(centerBin - 2, amplitude * 0.1f, 0.0f);
        buffer.setCartesian(centerBin + 2, amplitude * 0.1f, 0.0f);
    }
}

/// Add a single sine peak to a SpectralBuffer (accumulates, does NOT zero first)
void addSinePeak(SpectralBuffer& buffer, float freqHz,
                 float amplitude, size_t fftSize, float sampleRate) {
    const size_t numBins = fftSize / 2 + 1;
    const float binSpacing = sampleRate / static_cast<float>(fftSize);
    const float exactBin = freqHz / binSpacing;
    const size_t centerBin = static_cast<size_t>(exactBin + 0.5f);

    if (centerBin >= 2 && centerBin < numBins - 2) {
        auto addMag = [&](size_t b, float mag) {
            float re = buffer.getReal(b) + mag;
            buffer.setCartesian(b, re, 0.0f);
        };
        addMag(centerBin, amplitude);
        addMag(centerBin - 1, amplitude * 0.5f);
        addMag(centerBin + 1, amplitude * 0.5f);
        addMag(centerBin - 2, amplitude * 0.1f);
        addMag(centerBin + 2, amplitude * 0.1f);
    }
}

/// Fill spectrum with a harmonic series
void fillHarmonicSpectrum(SpectralBuffer& buffer, float f0, float amplitude,
                          int numHarmonics, size_t fftSize, float sampleRate) {
    const size_t numBins = fftSize / 2 + 1;
    for (size_t b = 0; b < numBins; ++b) {
        buffer.setCartesian(b, 0.0f, 0.0f);
    }

    const float nyquist = sampleRate / 2.0f;
    for (int h = 1; h <= numHarmonics; ++h) {
        float freq = f0 * static_cast<float>(h);
        if (freq > nyquist - 100.0f) break;
        float amp = amplitude / static_cast<float>(h);
        addSinePeak(buffer, freq, amp, fftSize, sampleRate);
    }
}

} // anonymous namespace

// =============================================================================
// Linear Prediction Tests
// =============================================================================

TEST_CASE("PartialTracker: linear prediction tracks vibrato",
           "[partial_tracker][linear_prediction]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    F0Estimate f0{440.0f, 0.9f, true};

    // Simulate vibrato: frequency moves 440 -> 442 -> 444 -> 446
    // Linear prediction should predict ~448 for frame 5
    float vibratoFreqs[] = {440.0f, 442.0f, 444.0f, 446.0f, 448.0f};

    for (int frame = 0; frame < 5; ++frame) {
        fillHarmonicSpectrum(spectrum, vibratoFreqs[frame], 0.5f, 5,
                             kTestFFTSize, kTestSampleRate);
        f0.frequency = vibratoFreqs[frame];
        tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);
    }

    // Verify tracking is maintained across all frames
    CHECK(tracker.getActiveCount() > 0);

    // The fundamental should have been tracked continuously
    const auto& partials = tracker.getPartials();
    bool foundFundamental = false;
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (partials[i].harmonicIndex == 1) {
            foundFundamental = true;
            // Should be close to the last vibrato frequency
            CHECK(partials[i].frequency == Approx(448.0f).margin(15.0f));
            // Should have high stability from continuous tracking
            CHECK(partials[i].stability > 0.5f);
            // Should have age > 0 (tracked across frames)
            CHECK(partials[i].age >= 3);
            break;
        }
    }
    CHECK(foundFundamental);
}

TEST_CASE("PartialTracker: crossing partials resolved by Hungarian",
           "[partial_tracker][hungarian]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    // Frame 1: Two partials at 400 Hz and 600 Hz (well-separated)
    F0Estimate f0{400.0f, 0.5f, false}; // unvoiced to skip harmonic sieve
    {
        const size_t numBins = kTestFFTSize / 2 + 1;
        for (size_t b = 0; b < numBins; ++b)
            spectrum.setCartesian(b, 0.0f, 0.0f);
        addSinePeak(spectrum, 400.0f, 0.8f, kTestFFTSize, kTestSampleRate);
        addSinePeak(spectrum, 600.0f, 0.6f, kTestFFTSize, kTestSampleRate);
    }
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    // Verify two partials detected
    int firstCount = tracker.getActiveCount();
    REQUIRE(firstCount >= 2);

    // Record frequencies
    float freq1 = 0.0f, freq2 = 0.0f;
    for (int i = 0; i < firstCount; ++i) {
        float f = tracker.getPartials()[i].frequency;
        if (std::abs(f - 400.0f) < 20.0f) freq1 = f;
        if (std::abs(f - 600.0f) < 20.0f) freq2 = f;
    }
    REQUIRE(freq1 > 0.0f);
    REQUIRE(freq2 > 0.0f);

    // Frame 2: Partials move toward each other — now at 410 and 590
    {
        const size_t numBins = kTestFFTSize / 2 + 1;
        for (size_t b = 0; b < numBins; ++b)
            spectrum.setCartesian(b, 0.0f, 0.0f);
        addSinePeak(spectrum, 410.0f, 0.8f, kTestFFTSize, kTestSampleRate);
        addSinePeak(spectrum, 590.0f, 0.6f, kTestFFTSize, kTestSampleRate);
    }
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    // Both should still be tracked (age > 0)
    int trackedCount = 0;
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (tracker.getPartials()[i].age > 0) {
            ++trackedCount;
        }
    }
    CHECK(trackedCount >= 2);
}

// =============================================================================
// Bandwidth Estimation Tests
// =============================================================================

TEST_CASE("PartialTracker: bandwidth estimation - pure tone has low bandwidth",
           "[partial_tracker][bandwidth]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    // Pure harmonic tone (sharp peaks)
    F0Estimate f0{440.0f, 0.95f, true};
    fillHarmonicSpectrum(spectrum, 440.0f, 1.0f, 5, kTestFFTSize, kTestSampleRate);
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    REQUIRE(tracker.getActiveCount() > 0);

    // Pure tones should have low bandwidth
    const auto& partials = tracker.getPartials();
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (partials[i].harmonicIndex > 0) {
            // Bandwidth should be on the low side for sharp spectral peaks
            CHECK(partials[i].bandwidth < 0.8f);
        }
    }
}

TEST_CASE("PartialTracker: bandwidth estimation - wide peaks have higher bandwidth",
           "[partial_tracker][bandwidth]") {
    PartialTracker tracker;
    tracker.prepare(kTestFFTSize, kTestSampleRate);

    SpectralBuffer spectrum;
    spectrum.prepare(kTestFFTSize);

    // Create a noisy partial: peak with significant noise floor around it.
    // A clean sinusoid concentrates energy in the main lobe (±2 bins).
    // Noise spreads energy to all surrounding bins.
    const size_t numBins = kTestFFTSize / 2 + 1;
    for (size_t b = 0; b < numBins; ++b) {
        spectrum.setCartesian(b, 0.0f, 0.0f);
    }
    const float binSpacing = kTestSampleRate / static_cast<float>(kTestFFTSize);
    const size_t centerBin = static_cast<size_t>(440.0f / binSpacing);
    // Peak at center with main lobe
    spectrum.setCartesian(centerBin, 0.5f, 0.0f);
    spectrum.setCartesian(centerBin - 1, 0.15f, 0.0f);
    spectrum.setCartesian(centerBin + 1, 0.15f, 0.0f);
    // Noise floor in surrounding bins (±3 to ±6): energy that a pure
    // sinusoid wouldn't have, indicating noisiness
    for (int offset = -6; offset <= 6; ++offset) {
        if (std::abs(offset) <= 2) continue; // skip main lobe
        auto b = static_cast<size_t>(centerBin + offset);
        if (b > 0 && b < numBins)
            spectrum.setCartesian(b, 0.25f, 0.0f);
    }

    F0Estimate f0{440.0f, 0.8f, true};
    tracker.processFrame(spectrum, f0, kTestFFTSize, kTestSampleRate);

    REQUIRE(tracker.getActiveCount() > 0);

    // Wide peaks should have higher bandwidth
    const auto& partials = tracker.getPartials();
    bool foundWide = false;
    for (int i = 0; i < tracker.getActiveCount(); ++i) {
        if (std::abs(partials[i].frequency - 440.0f) < 30.0f) {
            foundWide = true;
            // A flatter peak should have higher bandwidth than a sharp one
            CHECK(partials[i].bandwidth > 0.3f);
        }
    }
    CHECK(foundWide);
}
