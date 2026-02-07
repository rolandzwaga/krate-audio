// ==============================================================================
// Tests: Wavetable Oscillator
// ==============================================================================
// Test suite for WavetableOscillator playback engine (Layer 1).
// Covers User Stories 5, 6, 7: playback, phase interface, shared data/modulation.
//
// Reference: specs/016-wavetable-oscillator/spec.md
//
// IMPORTANT: All sample-processing loops collect metrics inside the loop and
// assert ONCE after the loop. See testing-guide anti-patterns.
// ==============================================================================

#include <krate/dsp/primitives/wavetable_oscillator.h>
#include <krate/dsp/primitives/wavetable_generator.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helper: Generate a shared sawtooth wavetable for multiple tests
// =============================================================================
static WavetableData& getSharedSawTable() {
    static WavetableData table;
    static bool initialized = false;
    if (!initialized) {
        generateMipmappedSaw(table);
        initialized = true;
    }
    return table;
}

static WavetableData& getSharedSineTable() {
    static WavetableData table;
    static bool initialized = false;
    if (!initialized) {
        float harmonics[] = {1.0f};
        generateMipmappedFromHarmonics(table, harmonics, 1);
        initialized = true;
    }
    return table;
}

// =============================================================================
// User Story 5: Oscillator Lifecycle and Basic Playback (T085-T089)
// =============================================================================

TEST_CASE("WavetableOscillator default construction", "[WavetableOscillator][US5]") {
    WavetableOscillator osc;

    SECTION("default state: sampleRate=0, frequency=440, table=nullptr") {
        // Before prepare, process should return 0.0 safely
        float sample = osc.process();
        REQUIRE(sample == 0.0f);
    }
}

TEST_CASE("WavetableOscillator prepare resets state", "[WavetableOscillator][US5]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    // Generate some samples to advance state
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    // Prepare again should reset all state
    osc.prepare(48000.0);
    REQUIRE(osc.phase() == 0.0);
    REQUIRE(osc.phaseWrapped() == false);
}

TEST_CASE("WavetableOscillator reset preserves config", "[WavetableOscillator][US5]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    // Advance state
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] float s = osc.process();
    }
    REQUIRE(osc.phase() > 0.0);

    // Reset should zero phase but preserve frequency/sampleRate/table
    osc.reset();
    REQUIRE(osc.phase() == 0.0);
    REQUIRE(osc.phaseWrapped() == false);

    // Should still produce output (table and frequency preserved)
    // Process several samples since phase 0 of a sine-phased saw may be near zero
    float maxAbsOutput = 0.0f;
    for (int i = 0; i < 50; ++i) {
        float s = osc.process();
        float absS = std::abs(s);
        if (absS > maxAbsOutput) maxAbsOutput = absS;
    }
    REQUIRE(maxAbsOutput > 0.1f);  // Should have non-trivial output
}

TEST_CASE("WavetableOscillator setWavetable(nullptr) produces silence (SC-016)", "[WavetableOscillator][US5]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWavetable(nullptr);

    bool allZero = true;
    for (int i = 0; i < 1000; ++i) {
        float sample = osc.process();
        if (sample != 0.0f) {
            allZero = false;
            break;
        }
    }
    REQUIRE(allZero);
}

TEST_CASE("WavetableOscillator setFrequency clamps to Nyquist", "[WavetableOscillator][US5]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());

    // Set frequency above Nyquist -- should be clamped
    osc.setFrequency(30000.0f);
    float sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));

    // Negative frequency should be clamped to 0
    osc.setFrequency(-100.0f);
    // Phase shouldn't advance with 0 Hz
    double phaseBefore = osc.phase();
    [[maybe_unused]] float ignored = osc.process();
    double phaseAfter = osc.phase();
    // Phase should not advance (or advance very slowly)
    REQUIRE(std::abs(phaseAfter - phaseBefore) < 0.001);
}

// =============================================================================
// User Story 5: Oscillator Output Quality (T090-T094)
// =============================================================================

TEST_CASE("WavetableOscillator sawtooth output at 440 Hz (US5 scenario 1)", "[WavetableOscillator][US5]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    // Generate one cycle worth of samples
    size_t samplesPerCycle = static_cast<size_t>(44100.0 / 440.0);
    std::vector<float> output(samplesPerCycle);
    osc.processBlock(output.data(), samplesPerCycle);

    // Verify output is in [-1, 1] range
    float maxVal = -2.0f;
    float minVal = 2.0f;
    bool hasNaN = false;
    for (size_t i = 0; i < samplesPerCycle; ++i) {
        if (detail::isNaN(output[i])) hasNaN = true;
        if (output[i] > maxVal) maxVal = output[i];
        if (output[i] < minVal) minVal = output[i];
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE(maxVal <= 1.1f);
    REQUIRE(minVal >= -1.1f);
    // Should have significant range (it's a sawtooth)
    REQUIRE(maxVal > 0.5f);
    REQUIRE(minVal < -0.5f);
}

TEST_CASE("WavetableOscillator table match at 100 Hz (US5 scenario 2)", "[WavetableOscillator][US5]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSineTable());
    osc.setFrequency(100.0f);

    // At 100 Hz, level 0 or 1 is used. The output should match the table data
    // read with cubic Hermite interpolation.
    const size_t numSamples = 441;  // About one cycle at 100 Hz
    float maxError = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float oscOut = osc.process();
        // The sine table produces sin(2*pi*phase) approximately
        double expectedPhase = static_cast<double>(i) * 100.0 / 44100.0;
        expectedPhase -= std::floor(expectedPhase);
        float expectedVal = std::sin(static_cast<float>(kTwoPi * expectedPhase));
        // Scale by normalization factor (~0.96)
        float error = std::abs(oscOut - expectedVal * 0.96f);
        // Allow for interpolation + normalization differences
        if (error > maxError) maxError = error;
    }
    // Should be within cubic Hermite tolerance
    REQUIRE(maxError < 0.1f);
}

TEST_CASE("WavetableOscillator cubic Hermite interpolation accuracy (SC-019)", "[WavetableOscillator][US5]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSineTable());
    osc.setFrequency(440.0f);

    // Generate 4096 samples and compare to true sine
    const size_t N = 4096;
    float maxError = 0.0f;
    float normFactor = 0.0f;

    // First determine normalization factor by checking peak output
    osc.reset();
    float peakOutput = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        float s = osc.process();
        float absS = std::abs(s);
        if (absS > peakOutput) peakOutput = absS;
    }
    normFactor = peakOutput;

    // Now compare
    osc.reset();
    for (size_t i = 0; i < N; ++i) {
        float oscOut = osc.process();
        double expectedPhase = static_cast<double>(i) * 440.0 / 44100.0;
        expectedPhase -= std::floor(expectedPhase);
        float expectedVal = std::sin(static_cast<float>(kTwoPi * expectedPhase)) * normFactor;
        float error = std::abs(oscOut - expectedVal);
        if (error > maxError) maxError = error;
    }
    // SC-019: within 1e-3 tolerance (spec requirement)
    INFO("Max cubic Hermite interpolation error: " << maxError);
    REQUIRE(maxError < 1e-3f);
}

TEST_CASE("WavetableOscillator processBlock equivalence (SC-011)", "[WavetableOscillator][US5]") {
    // processBlock(output, 512) must produce output identical to 512 sequential process() calls
    WavetableOscillator osc1, osc2;
    osc1.prepare(44100.0);
    osc2.prepare(44100.0);
    osc1.setWavetable(&getSharedSawTable());
    osc2.setWavetable(&getSharedSawTable());
    osc1.setFrequency(440.0f);
    osc2.setFrequency(440.0f);

    const size_t N = 512;
    std::vector<float> blockOutput(N);
    std::vector<float> singleOutput(N);

    osc1.processBlock(blockOutput.data(), N);
    for (size_t i = 0; i < N; ++i) {
        singleOutput[i] = osc2.process();
    }

    float maxDiff = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        float diff = std::abs(blockOutput[i] - singleOutput[i]);
        if (diff > maxDiff) maxDiff = diff;
    }
    REQUIRE(maxDiff == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("WavetableOscillator alias suppression at 1000 Hz (SC-009)", "[WavetableOscillator][US5]") {
    // SC-009: alias components measured via FFT over 4096+ samples, at 1000 Hz
    // / 44100 Hz with mipmapped sawtooth.
    //
    // The oscillator uses ceil-based mipmap level selection with a +1.0 shift
    // on the fractional level, ensuring BOTH crossfade levels have all harmonics
    // below Nyquist. At 1000 Hz, fracLevel = log2(46.44) + 1.0 = 6.54, so the
    // crossfade is between level 6 (16 harmonics, max 16kHz) and level 7
    // (8 harmonics, max 8kHz) -- both safe. Alias suppression is limited only
    // by numerical noise and spectral leakage from windowing.
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(1000.0f);

    // Skip startup transients
    for (int i = 0; i < 200; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    // Generate 4096 samples (spec requires 4096+)
    const size_t N = 4096;
    std::vector<float> output(N);
    osc.processBlock(output.data(), N);

    // Apply Hann window to reduce spectral leakage
    std::vector<float> window(N);
    Window::generateHann(window.data(), N);
    for (size_t i = 0; i < N; ++i) {
        output[i] *= window[i];
    }

    // FFT analysis
    FFT fft;
    fft.prepare(N);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(output.data(), spectrum.data());

    // Frequency resolution: 44100 / 4096 = ~10.77 Hz/bin
    const float binResolution = 44100.0f / static_cast<float>(N);
    const size_t fundamentalBin = static_cast<size_t>(
        std::round(1000.0f / binResolution));
    const float fundamentalMag = spectrum[fundamentalBin].magnitude();

    // Mark expected harmonic bins (multiples of 1000 Hz below Nyquist)
    // and their neighbors (±3 bins for Hann window leakage)
    const size_t numBins = fft.numBins();
    std::vector<bool> isExpectedBin(numBins, false);

    // Exclude DC region
    for (size_t b = 0; b < 5; ++b) {
        if (b < numBins) isExpectedBin[b] = true;
    }

    for (int h = 1; h <= 22; ++h) {
        float harmonicFreq = 1000.0f * static_cast<float>(h);
        if (harmonicFreq < 22050.0f) {
            auto bin = static_cast<int>(std::round(harmonicFreq / binResolution));
            for (int d = -3; d <= 3; ++d) {
                size_t idx = static_cast<size_t>(std::max(0, bin + d));
                if (idx < numBins)
                    isExpectedBin[idx] = true;
            }
        }
    }

    // Find peak non-harmonic bin magnitude (alias energy)
    float peakAlias = 0.0f;
    for (size_t b = 0; b < numBins; ++b) {
        if (!isExpectedBin[b]) {
            float mag = spectrum[b].magnitude();
            if (mag > peakAlias) peakAlias = mag;
        }
    }

    // Compute alias suppression ratio in dB
    float aliasSuppression = 20.0f * std::log10(
        fundamentalMag / std::max(peakAlias, 1e-10f));

    INFO("Fundamental magnitude: " << fundamentalMag);
    INFO("Peak alias magnitude: " << peakAlias);
    INFO("Alias suppression: " << aliasSuppression << " dB");

    // SC-009: alias components at least 50 dB below the fundamental.
    REQUIRE(aliasSuppression >= 50.0f);
}

// =============================================================================
// User Story 5: Mipmap Crossfading (T095-T097b)
// =============================================================================

TEST_CASE("WavetableOscillator frequency sweep crossfade (SC-020)", "[WavetableOscillator][US5]") {
    // SC-020: During frequency sweep 440-880 Hz, crossfading must be smooth.
    // Use a sine wavetable to isolate crossfade artifacts from waveform shape.
    // (1) Max sample-to-sample diff at mipmap transition < 0.05
    // (2) Spectral analysis: no energy spikes above -60 dB at transition

    // --- Part 1: Crossfade discontinuity measurement using sine table ---
    // A sine wave has no natural discontinuities, so any extra sample-to-sample
    // jump beyond the expected waveform change is a crossfade artifact.
    {
        WavetableOscillator osc;
        osc.prepare(44100.0);
        osc.setWavetable(&getSharedSineTable());

        const size_t numSamples = 4410;  // 100ms sweep
        std::vector<float> output(numSamples);

        for (size_t i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(numSamples);
            float freq = 440.0f + t * 440.0f;  // 440 to 880 Hz
            osc.setFrequency(freq);
            output[i] = osc.process();
        }

        // Compute max sample-to-sample difference.
        // For a sine at up to 880 Hz / 44100 Hz, max natural diff is
        // 2*pi*880/44100 * amplitude ≈ 0.125 * ~0.96 ≈ 0.12.
        // A crossfade artifact adds to this. SC-020 requires the
        // artifact at the transition boundary be < 0.05.
        // So total max diff should be < natural_max + 0.05 ≈ 0.17.
        float maxSampleDiff = 0.0f;
        for (size_t i = 1; i < numSamples; ++i) {
            float diff = std::abs(output[i] - output[i - 1]);
            if (diff > maxSampleDiff) maxSampleDiff = diff;
        }

        // Expected natural max for swept sine up to 880 Hz (with amplitude ~0.96):
        // 2*pi*880/44100 * 0.96 ≈ 0.120. Adding crossfade artifact tolerance of 0.05
        // gives an upper bound of about 0.18.
        INFO("Max sample-to-sample difference: " << maxSampleDiff);
        float maxNaturalDiff = kTwoPi * 880.0f / 44100.0f * 1.0f;  // ~0.125
        REQUIRE(maxSampleDiff < maxNaturalDiff + 0.05f);

        // Also verify no NaN and bounded output
        bool hasNaN = false;
        float maxAbsOutput = 0.0f;
        for (size_t i = 0; i < numSamples; ++i) {
            if (detail::isNaN(output[i])) hasNaN = true;
            float absVal = std::abs(output[i]);
            if (absVal > maxAbsOutput) maxAbsOutput = absVal;
        }
        REQUIRE_FALSE(hasNaN);
        REQUIRE(maxAbsOutput <= 1.5f);
    }

    // --- Part 2: Spectral analysis for crossfade artifacts ---
    // Use a sine table to isolate crossfade artifacts from harmonic content.
    // A sine wave has only the fundamental at all mipmap levels, so any
    // spectral energy outside the sweep range is a crossfade artifact.
    {
        WavetableOscillator osc;
        osc.prepare(44100.0);
        osc.setWavetable(&getSharedSineTable());

        const size_t N = 4096;
        std::vector<float> output(N);

        for (size_t i = 0; i < N; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(N);
            float freq = 440.0f + t * 440.0f;  // 440 to 880 Hz
            osc.setFrequency(freq);
            output[i] = osc.process();
        }

        // Apply Hann window
        std::vector<float> window(N);
        Window::generateHann(window.data(), N);
        for (size_t i = 0; i < N; ++i) {
            output[i] *= window[i];
        }

        // FFT
        FFT fft;
        fft.prepare(N);
        std::vector<Complex> spectrum(fft.numBins());
        fft.forward(output.data(), spectrum.data());

        // Find the peak magnitude (signal energy from sweep)
        float peakMag = 0.0f;
        for (size_t b = 1; b < fft.numBins(); ++b) {
            float mag = spectrum[b].magnitude();
            if (mag > peakMag) peakMag = mag;
        }

        // Signal region: the swept sine covers 440-880 Hz. With Hann window
        // spectral leakage, extend generously to 0-1200 Hz.
        const float binRes = 44100.0f / static_cast<float>(N);
        const size_t signalEndBin = static_cast<size_t>(1200.0f / binRes);

        // Check bins above the signal region for crossfade artifact spikes
        float peakArtifact = 0.0f;
        for (size_t b = signalEndBin; b < fft.numBins(); ++b) {
            float mag = spectrum[b].magnitude();
            if (mag > peakArtifact) peakArtifact = mag;
        }

        if (peakMag > 1e-10f && peakArtifact > 1e-10f) {
            float artifactDb = 20.0f * std::log10(peakArtifact / peakMag);
            INFO("Peak signal magnitude: " << peakMag);
            INFO("Peak artifact magnitude: " << peakArtifact);
            INFO("Artifact level relative to signal: " << artifactDb << " dB");
            REQUIRE(artifactDb < -60.0f);
        }
        // If peakArtifact is negligible (< 1e-10), the test trivially passes
    }
}

TEST_CASE("WavetableOscillator crossfade threshold values", "[WavetableOscillator][US5]") {
    // Verify that when fractional level is near integer, single lookup occurs,
    // and when between levels, two lookups are blended.
    // We test this indirectly by checking output consistency.
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());

    // At exactly an octave boundary, fractional level should be near integer
    // fundamental = 44100/2048 = 21.53 Hz
    // At 21.53 * 2^4 = 345 Hz, level should be exactly 4.0 (single lookup)
    float fundamental = 44100.0f / 2048.0f;
    float exactOctave = fundamental * 16.0f;  // 2^4
    osc.setFrequency(exactOctave);

    // Should produce valid output
    float sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));

    // At a frequency between octaves, fractional level has significant fraction
    float betweenOctaves = fundamental * 24.0f;  // Not a power of 2
    osc.setFrequency(betweenOctaves);
    sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));
}

// =============================================================================
// User Story 6: Phase Interface (T115-T119)
// =============================================================================

TEST_CASE("WavetableOscillator phase accessor", "[WavetableOscillator][US6]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    SECTION("phase returns value in [0, 1)") {
        for (int i = 0; i < 1000; ++i) {
            double p = osc.phase();
            REQUIRE(p >= 0.0);
            REQUIRE(p < 1.0);
            [[maybe_unused]] float s = osc.process();
        }
    }
}

TEST_CASE("WavetableOscillator phase wrap counting (SC-010)", "[WavetableOscillator][US6]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    int wrapCount = 0;
    for (int i = 0; i < 44100; ++i) {
        [[maybe_unused]] float s = osc.process();
        if (osc.phaseWrapped()) {
            ++wrapCount;
        }
    }
    // Should be approximately 440 wraps (plus or minus 1)
    REQUIRE(wrapCount >= 439);
    REQUIRE(wrapCount <= 441);
}

TEST_CASE("WavetableOscillator resetPhase (SC-012)", "[WavetableOscillator][US6]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSineTable());
    osc.setFrequency(440.0f);

    // Advance a bit
    for (int i = 0; i < 50; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    // Reset phase to 0.5
    osc.resetPhase(0.5);
    REQUIRE(osc.phase() == Approx(0.5).margin(1e-10));

    // Process should generate output from phase 0.5
    float sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));
}

TEST_CASE("WavetableOscillator resetPhase with out-of-range value", "[WavetableOscillator][US6]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSineTable());

    osc.resetPhase(1.5);
    REQUIRE(osc.phase() == Approx(0.5).margin(1e-10));

    osc.resetPhase(-0.3);
    REQUIRE(osc.phase() == Approx(0.7).margin(1e-10));
}

TEST_CASE("WavetableOscillator phaseWrapped detection", "[WavetableOscillator][US6]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    // Process until we get a wrap
    bool foundWrap = false;
    for (int i = 0; i < 200; ++i) {
        [[maybe_unused]] float s = osc.process();
        if (osc.phaseWrapped()) {
            foundWrap = true;
            break;
        }
    }
    REQUIRE(foundWrap);
}

// =============================================================================
// User Story 7: Shared Data and Modulation (T127-T133a)
// =============================================================================

TEST_CASE("WavetableOscillator shared data (SC-014)", "[WavetableOscillator][US7]") {
    // SC-014: Two oscillators sharing the same WavetableData, running at
    // different frequencies, produce correct independent output with no data
    // corruption over 100,000 samples.
    //
    // Strategy: compare each shared-table oscillator against a reference
    // oscillator using its own private copy. If sharing causes corruption,
    // the outputs will diverge.
    WavetableData& sharedTable = getSharedSawTable();

    // Create independent private copies for reference
    WavetableData privateCopy1;
    generateMipmappedSaw(privateCopy1);
    WavetableData privateCopy2;
    generateMipmappedSaw(privateCopy2);

    WavetableOscillator oscShared1, oscShared2;
    WavetableOscillator oscPrivate1, oscPrivate2;

    oscShared1.prepare(44100.0);
    oscShared2.prepare(44100.0);
    oscPrivate1.prepare(44100.0);
    oscPrivate2.prepare(44100.0);

    oscShared1.setWavetable(&sharedTable);
    oscShared2.setWavetable(&sharedTable);
    oscPrivate1.setWavetable(&privateCopy1);
    oscPrivate2.setWavetable(&privateCopy2);

    oscShared1.setFrequency(440.0f);
    oscPrivate1.setFrequency(440.0f);
    oscShared2.setFrequency(880.0f);
    oscPrivate2.setFrequency(880.0f);

    float maxDiff1 = 0.0f;
    float maxDiff2 = 0.0f;
    bool anyNaN = false;

    for (int i = 0; i < 100000; ++i) {
        float s1 = oscShared1.process();
        float s2 = oscShared2.process();
        float p1 = oscPrivate1.process();
        float p2 = oscPrivate2.process();

        if (detail::isNaN(s1) || detail::isNaN(s2) ||
            detail::isNaN(p1) || detail::isNaN(p2)) {
            anyNaN = true;
        }

        float d1 = std::abs(s1 - p1);
        float d2 = std::abs(s2 - p2);
        if (d1 > maxDiff1) maxDiff1 = d1;
        if (d2 > maxDiff2) maxDiff2 = d2;
    }

    REQUIRE_FALSE(anyNaN);
    // Shared oscillators must produce output identical to private-copy ones
    INFO("Max diff osc1 (440 Hz): " << maxDiff1);
    INFO("Max diff osc2 (880 Hz): " << maxDiff2);
    REQUIRE(maxDiff1 == Approx(0.0f).margin(1e-6f));
    REQUIRE(maxDiff2 == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("WavetableOscillator setWavetable(nullptr) mid-stream (US7 scenario 2)", "[WavetableOscillator][US7]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    // Generate some output
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    // Set to nullptr mid-stream
    osc.setWavetable(nullptr);

    bool allZero = true;
    for (int i = 0; i < 100; ++i) {
        float s = osc.process();
        if (s != 0.0f) allZero = false;
    }
    REQUIRE(allZero);
}

TEST_CASE("WavetableOscillator setWavetable mid-stream (US7 scenario 3)", "[WavetableOscillator][US7]") {
    WavetableData squareTable;
    generateMipmappedSquare(squareTable);

    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    // Generate some output with saw
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    // Switch to square mid-stream
    osc.setWavetable(&squareTable);

    // Should produce output from square table (no crash)
    float sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));
}

TEST_CASE("WavetableOscillator setPhaseModulation(0.0) identical to unmodulated (SC-013)", "[WavetableOscillator][US7]") {
    WavetableOscillator osc1, osc2;
    osc1.prepare(44100.0);
    osc2.prepare(44100.0);
    osc1.setWavetable(&getSharedSineTable());
    osc2.setWavetable(&getSharedSineTable());
    osc1.setFrequency(440.0f);
    osc2.setFrequency(440.0f);

    float maxDiff = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        osc2.setPhaseModulation(0.0f);
        float s1 = osc1.process();
        float s2 = osc2.process();
        float diff = std::abs(s1 - s2);
        if (diff > maxDiff) maxDiff = diff;
    }
    REQUIRE(maxDiff == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("WavetableOscillator setPhaseModulation applies offset", "[WavetableOscillator][US7]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSineTable());
    osc.setFrequency(440.0f);

    // Apply PM and verify it affects output
    // Use a sawtooth table instead of sine since saw has distinct values at different phases
    osc.setWavetable(&getSharedSawTable());

    // Advance past phase 0 (which may be near zero for both saw and sine)
    osc.reset();
    for (int i = 0; i < 10; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    // Now read with and without PM
    float unmodulated = osc.process();

    // Read from same phase but with PM offset (kPi/2 = quarter cycle)
    osc.resetPhase(osc.phase() - calculatePhaseIncrement(440.0f, 44100.0f));
    osc.setPhaseModulation(kPi * 0.5f);  // Quarter cycle offset
    float modulated = osc.process();

    // The PM should cause different output
    REQUIRE(std::abs(modulated - unmodulated) > 0.01f);
}

TEST_CASE("WavetableOscillator setFrequencyModulation applies offset", "[WavetableOscillator][US7]") {
    WavetableOscillator osc1, osc2;
    osc1.prepare(44100.0);
    osc2.prepare(44100.0);
    osc1.setWavetable(&getSharedSawTable());
    osc2.setWavetable(&getSharedSawTable());
    osc1.setFrequency(440.0f);
    osc2.setFrequency(440.0f);

    // Apply FM to osc2 but not osc1
    osc2.setFrequencyModulation(100.0f);
    float s1 = osc1.process();
    float s2 = osc2.process();

    // They should differ since FM changes effective frequency
    // (The first sample might be similar since both start at phase 0,
    //  but phase advance will differ)
    // Process more samples and check divergence
    float totalDiff = 0.0f;
    for (int i = 0; i < 100; ++i) {
        osc2.setFrequencyModulation(100.0f);
        s1 = osc1.process();
        s2 = osc2.process();
        totalDiff += std::abs(s1 - s2);
    }
    REQUIRE(totalDiff > 1.0f);  // Should have significant divergence
}

TEST_CASE("WavetableOscillator processBlock FM variant", "[WavetableOscillator][US7]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    const size_t N = 256;
    std::vector<float> fmBuffer(N, 0.0f);  // Zero FM = constant frequency
    std::vector<float> output(N);

    osc.processBlock(output.data(), fmBuffer.data(), N);

    // With zero FM, should produce valid output
    bool hasNaN = false;
    for (size_t i = 0; i < N; ++i) {
        if (detail::isNaN(output[i])) hasNaN = true;
    }
    REQUIRE_FALSE(hasNaN);
}

TEST_CASE("WavetableOscillator PM offset > 2*pi wrapping", "[WavetableOscillator][US7]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSineTable());
    osc.setFrequency(440.0f);

    // PM > 2*pi should wrap correctly
    osc.setPhaseModulation(3.0f * kTwoPi);
    float sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));
    REQUIRE(std::abs(sample) <= 1.5f);
}

// =============================================================================
// User Story 7: Edge Cases and Robustness (T140-T142a)
// =============================================================================

TEST_CASE("WavetableOscillator NaN/Inf frequency inputs (SC-017)", "[WavetableOscillator][US7]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());

    osc.setFrequency(std::numeric_limits<float>::quiet_NaN());
    bool hasNaN = false;
    bool hasInf = false;
    for (int i = 0; i < 1000; ++i) {
        float s = osc.process();
        if (detail::isNaN(s)) hasNaN = true;
        if (detail::isInf(s)) hasInf = true;
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);

    osc.setFrequency(std::numeric_limits<float>::infinity());
    for (int i = 0; i < 1000; ++i) {
        float s = osc.process();
        if (detail::isNaN(s)) hasNaN = true;
        if (detail::isInf(s)) hasInf = true;
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

TEST_CASE("WavetableOscillator processBlock with 0 samples", "[WavetableOscillator][US7]") {
    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&getSharedSawTable());
    osc.setFrequency(440.0f);

    double phaseBefore = osc.phase();
    osc.processBlock(nullptr, 0);
    double phaseAfter = osc.phase();

    REQUIRE(phaseBefore == phaseAfter);
}

TEST_CASE("WavetableOscillator used without prepare", "[WavetableOscillator][US7]") {
    WavetableOscillator osc;
    // No prepare() called, sampleRate=0
    osc.setWavetable(&getSharedSawTable());

    bool allZero = true;
    for (int i = 0; i < 100; ++i) {
        float s = osc.process();
        if (s != 0.0f && !detail::isNaN(s)) {
            // With sampleRate=0, increment=0, phase stays at 0
            // It reads a constant value from the table at phase 0
            // This is acceptable -- it won't advance but may read table[0]
        }
        if (detail::isNaN(s)) {
            allZero = false;  // NaN is not acceptable
            break;
        }
    }
    // The key requirement: no NaN
    REQUIRE_FALSE(detail::isNaN(osc.process()));
}

TEST_CASE("WavetableOscillator corrupted table data with NaN", "[WavetableOscillator][US7]") {
    WavetableData corruptedTable;
    generateMipmappedSaw(corruptedTable);

    // Corrupt some data with NaN
    float* level0 = corruptedTable.getMutableLevel(0);
    level0[100] = std::numeric_limits<float>::quiet_NaN();
    level0[500] = std::numeric_limits<float>::infinity();

    WavetableOscillator osc;
    osc.prepare(44100.0);
    osc.setWavetable(&corruptedTable);
    osc.setFrequency(440.0f);

    bool hasNaN = false;
    bool hasInf = false;
    for (int i = 0; i < 10000; ++i) {
        float s = osc.process();
        if (detail::isNaN(s)) hasNaN = true;
        if (detail::isInf(s)) hasInf = true;
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}
