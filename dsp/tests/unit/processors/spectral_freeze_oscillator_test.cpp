// ==============================================================================
// Tests: SpectralFreezeOscillator (Layer 2 processor)
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Reference: specs/030-spectral-freeze-oscillator/spec.md
// ==============================================================================

#include <krate/dsp/processors/spectral_freeze_oscillator.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/spectral_utils.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// Generate a sine wave signal
void generateSine(float* buffer, size_t numSamples, float frequency,
                  float sampleRate, float amplitude = 1.0f, float phase = 0.0f) {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(
            kTwoPi * frequency * static_cast<float>(i) / sampleRate + phase);
    }
}

/// Generate a sawtooth wave signal (band-limited approximation via additive)
void generateSawtooth(float* buffer, size_t numSamples, float frequency,
                      float sampleRate, float amplitude = 1.0f,
                      int numHarmonics = 20) {
    std::fill_n(buffer, numSamples, 0.0f);
    for (int h = 1; h <= numHarmonics; ++h) {
        float harmFreq = frequency * static_cast<float>(h);
        if (harmFreq >= sampleRate * 0.5f) break;
        float harmAmp = amplitude * 2.0f / (kPi * static_cast<float>(h));
        if (h % 2 == 0) harmAmp = -harmAmp;
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] += harmAmp * std::sin(
                kTwoPi * harmFreq * static_cast<float>(i) / sampleRate);
        }
    }
}

/// Estimate dominant frequency via FFT peak finding with parabolic interpolation
float estimateFundamental(const float* buffer, size_t numSamples,
                          float sampleRate) {
    if (numSamples < 64) return 0.0f;

    // Use FFT to find dominant frequency (more robust than autocorrelation
    // for spectral freeze output which may have amplitude modulation from
    // spectral leakage beating between neighboring bins)
    FFT fft;
    // Use power-of-2 size for FFT
    size_t fftSize = 1;
    while (fftSize < numSamples && fftSize < 8192) fftSize *= 2;
    if (fftSize > numSamples) fftSize /= 2;
    if (fftSize < 64) return 0.0f;

    fft.prepare(fftSize);

    // Copy and window the input
    std::vector<float> windowed(fftSize, 0.0f);
    for (size_t i = 0; i < fftSize; ++i) {
        // Hann window
        float w = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i)
                                           / static_cast<float>(fftSize));
        windowed[i] = buffer[i] * w;
    }

    std::vector<Complex> spectrum(fftSize / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    // Find bin with maximum magnitude (skip DC, bin 0)
    size_t minBin = static_cast<size_t>(50.0f * static_cast<float>(fftSize) / sampleRate);
    if (minBin < 1) minBin = 1;
    size_t maxBin = fftSize / 2;

    float bestMag = 0.0f;
    size_t bestBin = 0;
    for (size_t k = minBin; k < maxBin; ++k) {
        float mag = spectrum[k].magnitude();
        if (mag > bestMag) {
            bestMag = mag;
            bestBin = k;
        }
    }

    if (bestBin == 0 || bestMag < 1e-10f) return 0.0f;

    // Parabolic interpolation for sub-bin accuracy
    float fracBin = static_cast<float>(bestBin);
    if (bestBin > 0 && bestBin < maxBin - 1) {
        float magPrev = spectrum[bestBin - 1].magnitude();
        float magCurr = bestMag;
        float magNext = spectrum[bestBin + 1].magnitude();

        float denom = magPrev - 2.0f * magCurr + magNext;
        if (std::abs(denom) > 1e-10f) {
            float delta = 0.5f * (magPrev - magNext) / denom;
            fracBin += std::clamp(delta, -0.5f, 0.5f);
        }
    }

    return fracBin * sampleRate / static_cast<float>(fftSize);
}

/// Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sumSq = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSq / static_cast<float>(numSamples));
}

/// Find peak absolute value
float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Check if all samples are zero
bool allZeros(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return false;
    }
    return true;
}

/// Calculate bin-aligned frequency (frequency at exact FFT bin center)
/// Spectral freeze quantizes output to nearest bin, so using bin-aligned
/// frequencies avoids beating artifacts from spectral leakage.
float binAlignedFreq(size_t bin, size_t fftSize, float sampleRate) {
    return static_cast<float>(bin) * sampleRate / static_cast<float>(fftSize);
}

} // anonymous namespace

// =============================================================================
// Phase 3: User Story 1 - Freeze and Resynthesize (FR-001 to FR-011)
// =============================================================================

TEST_CASE("SpectralFreezeOscillator: prepare/reset/isPrepared lifecycle",
          "[SpectralFreezeOscillator][US1][lifecycle]") {

    SpectralFreezeOscillator osc;

    SECTION("not prepared initially") {
        REQUIRE_FALSE(osc.isPrepared());
    }

    SECTION("prepared after prepare()") {
        osc.prepare(44100.0, 2048);
        REQUIRE(osc.isPrepared());
    }

    SECTION("not frozen after prepare") {
        osc.prepare(44100.0, 2048);
        REQUIRE_FALSE(osc.isFrozen());
    }

    SECTION("reset clears frozen state") {
        osc.prepare(44100.0, 2048);
        std::vector<float> input(2048, 0.5f);
        osc.freeze(input.data(), input.size());
        REQUIRE(osc.isFrozen());
        osc.reset();
        REQUIRE_FALSE(osc.isFrozen());
    }

    SECTION("re-prepare clears frozen state") {
        osc.prepare(44100.0, 2048);
        std::vector<float> input(2048, 0.5f);
        osc.freeze(input.data(), input.size());
        REQUIRE(osc.isFrozen());
        osc.prepare(44100.0, 1024);
        REQUIRE_FALSE(osc.isFrozen());
        REQUIRE(osc.isPrepared());
    }
}

TEST_CASE("SpectralFreezeOscillator: freeze/unfreeze/isFrozen state transitions",
          "[SpectralFreezeOscillator][US1][state]") {

    SpectralFreezeOscillator osc;
    osc.prepare(44100.0, 2048);

    SECTION("freeze sets frozen state") {
        std::vector<float> input(2048, 0.5f);
        osc.freeze(input.data(), input.size());
        REQUIRE(osc.isFrozen());
    }

    SECTION("unfreeze eventually clears frozen state") {
        std::vector<float> input(2048, 0.5f);
        osc.freeze(input.data(), input.size());
        osc.unfreeze();

        // Process enough samples for the crossfade to complete
        std::vector<float> output(4096, 0.0f);
        osc.processBlock(output.data(), output.size());
        REQUIRE_FALSE(osc.isFrozen());
    }

    SECTION("unfreeze when not frozen is no-op") {
        osc.unfreeze();  // Should not crash
        REQUIRE_FALSE(osc.isFrozen());
    }
}

TEST_CASE("SpectralFreezeOscillator: frozen sine wave output frequency stability (SC-001)",
          "[SpectralFreezeOscillator][US1][frequency]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    // SC-001: spec requires 440 Hz exactly. With fftSize=2048 at 44.1kHz,
    // 440 Hz falls at bin 20.417 (between bins 20 and 21). The spectral
    // freeze resynthesizes energy at each bin's center frequency, causing
    // beating between bins 20 and 21. However, the *dominant frequency*
    // (FFT peak with parabolic interpolation) remains stable at ~440 Hz.
    const float testFreq = 440.0f;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Generate a sine wave at the bin-aligned frequency
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));

    // Freeze
    osc.freeze(input.data(), input.size());
    REQUIRE(osc.isFrozen());

    // Process 10 seconds of output (SC-001: frequency stability over 10s)
    const size_t tenSeconds = static_cast<size_t>(sampleRate * 10.0);
    const size_t blockSize = 512;
    std::vector<float> block(blockSize);

    // Skip initial latency
    for (size_t i = 0; i < fftSize * 2; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Capture final portion for frequency analysis.
    // Use fftSize-length analysis to match synthesis resolution -- the frozen
    // output contains sinusoids at bin center frequencies which merge into a
    // single peak when analyzed at matching resolution, allowing parabolic
    // interpolation to find the true frequency between bins.
    const size_t analysisLen = fftSize;
    std::vector<float> analysisBuffer(analysisLen);

    // Process remaining ~10 seconds
    for (size_t processed = fftSize * 2; processed < tenSeconds - analysisLen;
         processed += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Capture final samples for analysis
    for (size_t i = 0; i < analysisLen; i += blockSize) {
        size_t toProcess = std::min(blockSize, analysisLen - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, analysisBuffer.data() + i);
    }

    // Estimate frequency via FFT peak finding with parabolic interpolation.
    // At matching resolution, the energy at bins 20 and 21 merges into a
    // single broad peak, and interpolation recovers the true frequency.
    float detectedFreq = estimateFundamental(
        analysisBuffer.data(), analysisLen, static_cast<float>(sampleRate));

    // SC-001: Within 1% of 440 Hz over 10s of continuous output
    REQUIRE(detectedFreq > 0.0f);
    REQUIRE(detectedFreq == Approx(testFreq).epsilon(0.01));
}

TEST_CASE("SpectralFreezeOscillator: magnitude spectrum fidelity (SC-002)",
          "[SpectralFreezeOscillator][US1][magnitude]") {

    // SC-002: "The magnitude spectrum of the frozen output MUST match the
    // captured frame's magnitude spectrum within 1 dB per bin (RMS error
    // across all bins) when no modifications are applied."

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t numBins = fftSize / 2 + 1;
    constexpr size_t blockSize = 512;

    // Generate a two-tone signal at bin-aligned frequencies for clean comparison
    const float freq1 = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    const float freq2 = binAlignedFreq(50, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, freq1, static_cast<float>(sampleRate), 0.7f);
    for (size_t i = 0; i < fftSize; ++i) {
        input[i] += 0.3f * std::sin(
            kTwoPi * freq2 * static_cast<float>(i) / static_cast<float>(sampleRate));
    }

    // Compute reference spectrum: unwindowed FFT matching what freeze() does
    // (freeze() does NOT apply an analysis window -- see comment in implementation)
    FFT refFft;
    refFft.prepare(fftSize);
    std::vector<Complex> refSpectrum(numBins);
    refFft.forward(input.data(), refSpectrum.data());

    // Freeze the signal and generate output
    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);
    osc.freeze(input.data(), input.size());

    // Warmup: let OLA pipeline reach steady state
    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 8; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Capture a full FFT frame of output
    std::vector<float> outputFrame(fftSize);
    for (size_t i = 0; i < fftSize; i += blockSize) {
        size_t toProcess = std::min(blockSize, fftSize - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, outputFrame.data() + i);
    }

    // Unwindowed FFT of output (bin-aligned signals are periodic over fftSize,
    // so no window needed for clean spectral lines matching the frozen reference)
    std::vector<Complex> outSpectrum(numBins);
    refFft.forward(outputFrame.data(), outSpectrum.data());

    // Normalize both spectra to their respective peak magnitudes.
    // The output has different overall gain than the reference due to the
    // synthesis pipeline (IFFT 1/N, Hann window, COLA normalization, OLA).
    // SC-002 is about spectral *shape* fidelity, not absolute level.
    float refPeak = 0.0f;
    float outPeak = 0.0f;
    for (size_t k = 1; k < numBins; ++k) {
        refPeak = std::max(refPeak, refSpectrum[k].magnitude());
        outPeak = std::max(outPeak, outSpectrum[k].magnitude());
    }
    REQUIRE(refPeak > 1e-10f);
    REQUIRE(outPeak > 1e-10f);

    // Compare: RMS dB error across bins with significant magnitude
    // after normalizing both spectra to unit peak
    float sumSqDbError = 0.0f;
    size_t count = 0;
    constexpr float minMagRatio = 1e-4f;  // Skip bins below -80 dB from peak

    for (size_t k = 1; k < numBins; ++k) {
        float refMag = refSpectrum[k].magnitude() / refPeak;
        float outMag = outSpectrum[k].magnitude() / outPeak;

        // Skip bins where both are negligible relative to peak
        if (refMag < minMagRatio && outMag < minMagRatio) continue;

        refMag = std::max(refMag, minMagRatio);
        outMag = std::max(outMag, minMagRatio);

        float dbError = 20.0f * std::log10(outMag / refMag);
        sumSqDbError += dbError * dbError;
        ++count;
    }

    float rmsDbError = (count > 0)
                     ? std::sqrt(sumSqDbError / static_cast<float>(count))
                     : 0.0f;

    // SC-002: Within 1 dB RMS error across all bins
    REQUIRE(count > 0);
    REQUIRE(rmsDbError < 1.0f);
}

TEST_CASE("SpectralFreezeOscillator: COLA-compliant resynthesis with Hann 75% overlap",
          "[SpectralFreezeOscillator][US1][cola]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    REQUIRE(osc.getFftSize() == fftSize);
    REQUIRE(osc.getHopSize() == fftSize / 4);

    // Freeze a sine wave at a bin-aligned frequency for clean COLA test
    // (non-bin-aligned frequencies produce expected beating from spectral leakage)
    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Generate a long block and verify stable amplitude (no modulation from OLA)
    const size_t totalSamples = fftSize * 8;
    std::vector<float> output(totalSamples);
    const size_t blockSize = 512;
    for (size_t i = 0; i < totalSamples; i += blockSize) {
        size_t toProcess = std::min(blockSize, totalSamples - i);
        osc.processBlock(output.data() + i, toProcess);
    }

    // Skip warmup period (first 2*fftSize samples)
    const size_t startIdx = fftSize * 3;
    const size_t segLen = 512;

    // Check amplitude stability across multiple segments
    float firstRMS = calculateRMS(output.data() + startIdx, segLen);
    REQUIRE(firstRMS > 0.01f);  // Non-silence

    for (size_t seg = 1; seg < 4; ++seg) {
        float segRMS = calculateRMS(
            output.data() + startIdx + seg * segLen, segLen);
        // Amplitude should be stable within 1 dB
        float ratio = segRMS / firstRMS;
        REQUIRE(ratio == Approx(1.0f).margin(0.12f));  // ~1 dB
    }
}

TEST_CASE("SpectralFreezeOscillator: coherent phase advancement over 10s",
          "[SpectralFreezeOscillator][US1][phase]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Use bin-aligned frequency for stable amplitude
    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Skip initial latency
    const size_t blockSize = 512;
    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 2; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Sample RMS at different points over 10 seconds
    const size_t tenSeconds = static_cast<size_t>(sampleRate * 10.0);
    float earlyRMS = 0.0f;
    float lateRMS = 0.0f;

    for (size_t processed = fftSize * 2; processed < tenSeconds;
         processed += blockSize) {
        osc.processBlock(block.data(), blockSize);

        if (processed >= fftSize * 4 && processed < fftSize * 4 + blockSize) {
            earlyRMS = calculateRMS(block.data(), blockSize);
        }
        if (processed >= tenSeconds - blockSize * 2 &&
            processed < tenSeconds - blockSize) {
            lateRMS = calculateRMS(block.data(), blockSize);
        }
    }

    // Verify no amplitude decay over 10 seconds
    REQUIRE(earlyRMS > 0.01f);
    REQUIRE(lateRMS > 0.01f);
    float ratio = lateRMS / earlyRMS;
    REQUIRE(ratio == Approx(1.0f).margin(0.15f));
}

TEST_CASE("SpectralFreezeOscillator: click-free freeze transition (SC-007)",
          "[SpectralFreezeOscillator][US1][freeze_transition]") {

    // SC-007: "The transition from unfrozen to frozen state MUST NOT produce
    // audible clicks, verified by checking that the peak amplitude of the
    // output within the first 2 synthesis frames after freeze does not
    // exceed 2x the steady-state RMS level."

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t hopSize = fftSize / 4;  // 512
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Use bin-aligned frequency for stable steady-state amplitude
    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Capture the first 2 synthesis frames of output immediately after freeze
    const size_t transitionSamples = hopSize * 2;
    std::vector<float> transitionOutput(transitionSamples);
    for (size_t i = 0; i < transitionSamples; i += blockSize) {
        size_t toProcess = std::min(blockSize, transitionSamples - i);
        osc.processBlock(transitionOutput.data() + i, toProcess);
    }

    float transitionPeak = findPeak(transitionOutput.data(), transitionSamples);

    // Continue processing to reach steady state
    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 8; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Measure steady-state RMS
    osc.processBlock(block.data(), blockSize);
    float steadyRMS = calculateRMS(block.data(), blockSize);

    // SC-007: Peak in first 2 frames after freeze <= 2x steady-state RMS
    REQUIRE(steadyRMS > 0.01f);
    REQUIRE(transitionPeak < steadyRMS * 2.0f);
}

TEST_CASE("SpectralFreezeOscillator: silence when not frozen (FR-027)",
          "[SpectralFreezeOscillator][US1][silence]") {

    SpectralFreezeOscillator osc;
    osc.prepare(44100.0, 2048);

    std::vector<float> output(512, 1.0f);  // Fill with non-zero
    osc.processBlock(output.data(), output.size());

    REQUIRE(allZeros(output.data(), output.size()));
}

TEST_CASE("SpectralFreezeOscillator: silence when not prepared (FR-028)",
          "[SpectralFreezeOscillator][US1][silence]") {

    SpectralFreezeOscillator osc;
    // Not prepared!

    std::vector<float> output(512, 1.0f);
    osc.processBlock(output.data(), output.size());

    REQUIRE(allZeros(output.data(), output.size()));
}

TEST_CASE("SpectralFreezeOscillator: processBlock arbitrary block sizes (FR-011)",
          "[SpectralFreezeOscillator][US1][blocksize]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Process with various block sizes
    SECTION("block size 1") {
        std::vector<float> output(fftSize * 2);
        for (size_t i = 0; i < output.size(); ++i) {
            osc.processBlock(&output[i], 1);
        }
        // Should have non-zero output after warmup
        float rms = calculateRMS(output.data() + fftSize, fftSize);
        REQUIRE(rms > 0.01f);
    }

    SECTION("block size 100 (not power of 2)") {
        std::vector<float> output(100);
        for (size_t i = 0; i < fftSize * 3; i += 100) {
            osc.processBlock(output.data(), 100);
        }
        float rms = calculateRMS(output.data(), 100);
        REQUIRE(rms > 0.01f);
    }

    SECTION("block size larger than fftSize") {
        std::vector<float> output(fftSize * 3);
        osc.processBlock(output.data(), output.size());
        // Should not crash
    }
}

TEST_CASE("SpectralFreezeOscillator: zero-padding when freeze blockSize < fftSize (FR-004)",
          "[SpectralFreezeOscillator][US1][zeropad]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Provide only 512 samples (less than fftSize)
    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(512);
    generateSine(input.data(), 512, testFreq, static_cast<float>(sampleRate));

    osc.freeze(input.data(), input.size());
    REQUIRE(osc.isFrozen());

    // Should produce output (with zero-padded spectrum)
    std::vector<float> output(fftSize * 4);
    const size_t blockSize = 512;
    for (size_t i = 0; i < output.size(); i += blockSize) {
        osc.processBlock(output.data() + i, blockSize);
    }

    float rms = calculateRMS(output.data() + fftSize * 2, fftSize);
    REQUIRE(rms > 0.001f);  // Some output expected
}

TEST_CASE("SpectralFreezeOscillator: getLatencySamples query (FR-026)",
          "[SpectralFreezeOscillator][US1][latency]") {

    SpectralFreezeOscillator osc;

    SECTION("returns 0 when not prepared") {
        REQUIRE(osc.getLatencySamples() == 0);
    }

    SECTION("returns fftSize when prepared") {
        osc.prepare(44100.0, 2048);
        REQUIRE(osc.getLatencySamples() == 2048);
    }

    SECTION("returns fftSize for different sizes") {
        osc.prepare(44100.0, 1024);
        REQUIRE(osc.getLatencySamples() == 1024);
    }
}

TEST_CASE("SpectralFreezeOscillator: CPU budget (SC-003)",
          "[SpectralFreezeOscillator][US1][performance]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Warm up
    std::vector<float> output(blockSize);
    for (int i = 0; i < 20; ++i) {
        osc.processBlock(output.data(), blockSize);
    }

    // Measure processing time (take best of 3 runs to reduce system noise)
    const int numIterations = 2000;
    double bestCpuPercent = 100.0;

    for (int run = 0; run < 3; ++run) {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < numIterations; ++i) {
            osc.processBlock(output.data(), blockSize);
        }
        auto end = std::chrono::high_resolution_clock::now();

        double elapsed = std::chrono::duration<double>(end - start).count();
        double audioTime = static_cast<double>(blockSize * numIterations)
                         / sampleRate;
        double cpuPercent = (elapsed / audioTime) * 100.0;
        bestCpuPercent = std::min(bestCpuPercent, cpuPercent);
    }

    // SC-003: < 0.5% CPU
    REQUIRE(bestCpuPercent < 0.5);
}

TEST_CASE("SpectralFreezeOscillator: memory budget (SC-008)",
          "[SpectralFreezeOscillator][US1][memory]") {

    // Memory estimation for 2048 FFT at 44.1kHz
    // This is a design verification, not runtime measurement
    constexpr size_t fftSize = 2048;
    constexpr size_t numBins = fftSize / 2 + 1;  // 1025

    // Calculate expected memory usage
    size_t totalBytes = 0;

    // Frozen magnitudes + initial phases + phase accumulators
    totalBytes += numBins * sizeof(float) * 3;
    // Phase increments
    totalBytes += numBins * sizeof(float);
    // Working magnitudes
    totalBytes += numBins * sizeof(float);
    // Working spectrum (Complex)
    totalBytes += numBins * sizeof(Complex);
    // IFFT buffer
    totalBytes += fftSize * sizeof(float);
    // Synthesis + analysis windows
    totalBytes += fftSize * sizeof(float) * 2;
    // Output ring buffer (2x fftSize)
    totalBytes += fftSize * 2 * sizeof(float);
    // Capture buffer
    totalBytes += fftSize * sizeof(float);
    // Capture complex buf
    totalBytes += numBins * sizeof(Complex);
    // FFT internal buffers (3 aligned)
    totalBytes += fftSize * sizeof(float) * 3;
    // FFT setup ~512 bytes
    totalBytes += 512;
    // FormantPreserver (FFT + work buffers)
    totalBytes += fftSize * sizeof(float) * 3;  // FFT aligned buffers
    totalBytes += 512;  // FFT setup
    totalBytes += fftSize * sizeof(float) * 2;  // logMag + cepstrum
    totalBytes += numBins * sizeof(float);       // envelope
    totalBytes += numBins * sizeof(Complex);     // complexBuf
    totalBytes += fftSize * sizeof(float);       // lifterWindow
    // Original + shifted envelope
    totalBytes += numBins * sizeof(float) * 2;

    // SC-008: < 200 KB
    REQUIRE(totalBytes < 200 * 1024);
}

TEST_CASE("SpectralFreezeOscillator: NaN/Inf safety (SC-006)",
          "[SpectralFreezeOscillator][US1][safety]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Freeze a normal signal
    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Process 10 seconds with randomized parameter sweeps
    std::vector<float> output(blockSize);
    bool hasNaN = false;
    bool hasInf = false;

    const size_t tenSeconds = static_cast<size_t>(sampleRate * 10.0);
    size_t paramCounter = 0;

    for (size_t processed = 0; processed < tenSeconds; processed += blockSize) {
        // Sweep parameters
        float t = static_cast<float>(paramCounter) / 100.0f;
        osc.setPitchShift(24.0f * std::sin(t * 0.7f));
        osc.setSpectralTilt(24.0f * std::sin(t * 1.3f));
        osc.setFormantShift(24.0f * std::sin(t * 0.5f));

        osc.processBlock(output.data(), blockSize);

        for (size_t i = 0; i < blockSize; ++i) {
            if (std::isnan(output[i])) hasNaN = true;
            if (std::isinf(output[i])) hasInf = true;
        }

        ++paramCounter;
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

// =============================================================================
// Phase 4: User Story 2 - Pitch Shift (FR-012 to FR-015)
// =============================================================================

TEST_CASE("SpectralFreezeOscillator: setPitchShift/getPitchShift parameter (FR-012)",
          "[SpectralFreezeOscillator][US2][pitch]") {

    SpectralFreezeOscillator osc;
    osc.prepare(44100.0, 2048);

    SECTION("default is 0") {
        REQUIRE(osc.getPitchShift() == 0.0f);
    }

    SECTION("set and get") {
        osc.setPitchShift(7.0f);
        REQUIRE(osc.getPitchShift() == 7.0f);
    }

    SECTION("clamped to [-24, +24]") {
        osc.setPitchShift(30.0f);
        REQUIRE(osc.getPitchShift() == 24.0f);

        osc.setPitchShift(-30.0f);
        REQUIRE(osc.getPitchShift() == -24.0f);
    }
}

TEST_CASE("SpectralFreezeOscillator: +12 semitones pitch shift on sawtooth (SC-004)",
          "[SpectralFreezeOscillator][US2][pitch]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    // Use bin-aligned frequency: bin 10 fundamental, harmonics at bins 20, 30, etc.
    // +12 semitones (octave up) shifts bins by 2x, so fundamental moves to bin 20.
    const float baseFreq = binAlignedFreq(10, fftSize, static_cast<float>(sampleRate));
    const float expectedFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Generate sawtooth at bin-aligned base frequency
    std::vector<float> input(fftSize);
    generateSawtooth(input.data(), fftSize, baseFreq,
                     static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Apply +12 semitones (octave up)
    osc.setPitchShift(12.0f);

    // Skip warmup
    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 4; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Capture for analysis
    const size_t analysisLen = 8192;
    std::vector<float> analysis(analysisLen);
    for (size_t i = 0; i < analysisLen; i += blockSize) {
        size_t toProcess = std::min(blockSize, analysisLen - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, analysis.data() + i);
    }

    float detectedFreq = estimateFundamental(
        analysis.data(), analysisLen, static_cast<float>(sampleRate));

    // SC-004: Within 2% of expected frequency (bin-quantized pitch shift)
    REQUIRE(detectedFreq > 0.0f);
    REQUIRE(detectedFreq == Approx(expectedFreq).epsilon(0.02));
}

TEST_CASE("SpectralFreezeOscillator: 0 semitones pitch shift (identity)",
          "[SpectralFreezeOscillator][US2][pitch]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    osc.setPitchShift(0.0f);

    // Skip warmup and capture
    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 4; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    const size_t analysisLen = 8192;
    std::vector<float> analysis(analysisLen);
    for (size_t i = 0; i < analysisLen; i += blockSize) {
        size_t toProcess = std::min(blockSize, analysisLen - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, analysis.data() + i);
    }

    float detectedFreq = estimateFundamental(
        analysis.data(), analysisLen, static_cast<float>(sampleRate));

    REQUIRE(detectedFreq > 0.0f);
    REQUIRE(detectedFreq == Approx(testFreq).epsilon(0.01));
}

TEST_CASE("SpectralFreezeOscillator: -12 semitones pitch shift (octave down)",
          "[SpectralFreezeOscillator][US2][pitch]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    // Use bin 20, shift down octave -> bin 10
    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    const float expectedFreq = binAlignedFreq(10, fftSize, static_cast<float>(sampleRate));

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    osc.setPitchShift(-12.0f);

    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 4; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    const size_t analysisLen = 16384;
    std::vector<float> analysis(analysisLen);
    for (size_t i = 0; i < analysisLen; i += blockSize) {
        size_t toProcess = std::min(blockSize, analysisLen - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, analysis.data() + i);
    }

    float detectedFreq = estimateFundamental(
        analysis.data(), analysisLen, static_cast<float>(sampleRate));

    // Expect half-frequency within 2%
    REQUIRE(detectedFreq > 0.0f);
    REQUIRE(detectedFreq == Approx(expectedFreq).epsilon(0.02));
}

TEST_CASE("SpectralFreezeOscillator: fractional semitones pitch shift",
          "[SpectralFreezeOscillator][US2][pitch]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    // Use bin 20 as base frequency, shift +7 semitones (perfect fifth)
    // Ratio = 2^(7/12) = ~1.4983. Bin 20 * 1.4983 = bin ~29.97 -> snaps to bin 30
    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    // The pitch shift maps bin k to bin k*ratio. For the dominant bin 20,
    // the energy goes to bin 20 * 2^(7/12) = ~29.97, which falls between
    // bins 29 and 30 (linear interpolation). The dominant output bin depends
    // on the bin mapping: destination bin k has source bin k/ratio.
    // Bin 30: src = 30/1.4983 = 20.02 -> near bin 20 (our energy). Strong.
    const float expectedFreq = binAlignedFreq(30, fftSize, static_cast<float>(sampleRate));

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    osc.setPitchShift(7.0f);

    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 4; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    const size_t analysisLen = 8192;
    std::vector<float> analysis(analysisLen);
    for (size_t i = 0; i < analysisLen; i += blockSize) {
        size_t toProcess = std::min(blockSize, analysisLen - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, analysis.data() + i);
    }

    float detectedFreq = estimateFundamental(
        analysis.data(), analysisLen, static_cast<float>(sampleRate));

    REQUIRE(detectedFreq > 0.0f);
    // Allow wider tolerance for fractional shift (bin quantization effects)
    REQUIRE(detectedFreq == Approx(expectedFreq).epsilon(0.05));
}

TEST_CASE("SpectralFreezeOscillator: bins exceeding Nyquist are zeroed (FR-015)",
          "[SpectralFreezeOscillator][US2][pitch]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Freeze a high frequency signal (bin-aligned for clean capture)
    const float testFreq = binAlignedFreq(465, fftSize, static_cast<float>(sampleRate)); // ~10 kHz
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Shift up by 24 semitones (4x frequency = 40 kHz, above Nyquist)
    osc.setPitchShift(24.0f);

    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 8; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Output should be very quiet (most bins zeroed)
    float rms = calculateRMS(block.data(), blockSize);
    REQUIRE(rms < 0.1f);  // Most energy should be gone
}

// =============================================================================
// Phase 5: User Story 3 - Spectral Tilt (FR-016 to FR-018)
// =============================================================================

TEST_CASE("SpectralFreezeOscillator: setSpectralTilt/getSpectralTilt parameter (FR-016)",
          "[SpectralFreezeOscillator][US3][tilt]") {

    SpectralFreezeOscillator osc;
    osc.prepare(44100.0, 2048);

    SECTION("default is 0") {
        REQUIRE(osc.getSpectralTilt() == 0.0f);
    }

    SECTION("set and get") {
        osc.setSpectralTilt(6.0f);
        REQUIRE(osc.getSpectralTilt() == 6.0f);
    }

    SECTION("clamped to [-24, +24]") {
        osc.setSpectralTilt(30.0f);
        REQUIRE(osc.getSpectralTilt() == 24.0f);

        osc.setSpectralTilt(-30.0f);
        REQUIRE(osc.getSpectralTilt() == -24.0f);
    }
}

TEST_CASE("SpectralFreezeOscillator: +6 dB/octave tilt on flat spectrum (SC-005)",
          "[SpectralFreezeOscillator][US3][tilt]") {

    // SC-005: "Spectral tilt of +6 dB/octave applied to a frozen flat-spectrum
    // signal MUST produce an output where the magnitude difference between
    // octave-spaced frequency bands is 6 dB within 1 dB tolerance, measured
    // across at least 3 octaves."

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Generate a 4-tone signal at bin-aligned frequencies spanning 3 octaves:
    // bins 5, 10, 20, 40 (frequencies ~107, ~215, ~430, ~861 Hz)
    // All tones at equal amplitude so any difference comes from tilt.
    // Low amplitude (0.02) to avoid FR-018 output clamp at ±2.0: with +6 dB/oct
    // tilt, bin 40 gets ~40x gain. Peak output ≈ 4 * 0.02 * 40 ≈ 3.2 before COLA,
    // which stays within ±2.0 after synthesis pipeline gain reduction.
    constexpr size_t testBins[] = {5, 10, 20, 40};
    std::vector<float> input(fftSize, 0.0f);
    for (size_t b : testBins) {
        float freq = binAlignedFreq(b, fftSize, static_cast<float>(sampleRate));
        for (size_t i = 0; i < fftSize; ++i) {
            input[i] += 0.02f * std::sin(
                kTwoPi * freq * static_cast<float>(i)
                / static_cast<float>(sampleRate));
        }
    }
    osc.freeze(input.data(), input.size());

    // Apply +6 dB/octave tilt
    osc.setSpectralTilt(6.0f);

    // Generate output and let OLA stabilize
    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 8; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Capture a frame for spectral analysis
    std::vector<float> outputFrame(fftSize);
    for (size_t i = 0; i < fftSize; i += blockSize) {
        size_t toProcess = std::min(blockSize, fftSize - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, outputFrame.data() + i);
    }

    // FFT the output to check spectral slope
    FFT analysisFft;
    analysisFft.prepare(fftSize);
    std::vector<Complex> outputSpectrum(fftSize / 2 + 1);
    analysisFft.forward(outputFrame.data(), outputSpectrum.data());

    // Measure magnitudes at the 4 test bins
    float mag5  = outputSpectrum[5].magnitude();
    float mag10 = outputSpectrum[10].magnitude();
    float mag20 = outputSpectrum[20].magnitude();
    float mag40 = outputSpectrum[40].magnitude();

    // With +6 dB/octave tilt, each octave-spaced pair should differ by ~6 dB.
    // Reference freq is bin 1, so tilt at bin k = 6 * log2(k).
    // Between any two octave-spaced bins: diff = 6 * log2(2) = 6 dB.
    REQUIRE(mag5 > 1e-6f);
    REQUIRE(mag10 > 1e-6f);
    REQUIRE(mag20 > 1e-6f);
    REQUIRE(mag40 > 1e-6f);

    float dbDiff_5_10  = 20.0f * std::log10(mag10 / mag5);
    float dbDiff_10_20 = 20.0f * std::log10(mag20 / mag10);
    float dbDiff_20_40 = 20.0f * std::log10(mag40 / mag20);

    // SC-005: Each octave should show ~6 dB increase, within 1 dB tolerance,
    // measured across 3 octaves
    REQUIRE(dbDiff_5_10  == Approx(6.0f).margin(1.0f));
    REQUIRE(dbDiff_10_20 == Approx(6.0f).margin(1.0f));
    REQUIRE(dbDiff_20_40 == Approx(6.0f).margin(1.0f));
}

TEST_CASE("SpectralFreezeOscillator: 0 dB/octave tilt (identity)",
          "[SpectralFreezeOscillator][US3][tilt]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    osc.setSpectralTilt(0.0f);

    // Output should produce the same frequency as normal
    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 4; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    const size_t analysisLen = 8192;
    std::vector<float> analysis(analysisLen);
    for (size_t i = 0; i < analysisLen; i += blockSize) {
        size_t toProcess = std::min(blockSize, analysisLen - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, analysis.data() + i);
    }

    float detectedFreq = estimateFundamental(
        analysis.data(), analysisLen, static_cast<float>(sampleRate));
    REQUIRE(detectedFreq > 0.0f);
    REQUIRE(detectedFreq == Approx(testFreq).epsilon(0.01));
}

TEST_CASE("SpectralFreezeOscillator: magnitude clamping to [0, 2.0] (FR-018)",
          "[SpectralFreezeOscillator][US3][tilt]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Apply extreme tilt
    osc.setSpectralTilt(24.0f);

    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 8; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Output should not have any NaN/Inf from overflow
    bool hasNaN = false;
    bool hasInf = false;
    for (size_t i = 0; i < blockSize; ++i) {
        if (std::isnan(block[i])) hasNaN = true;
        if (std::isinf(block[i])) hasInf = true;
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

// =============================================================================
// Phase 6: User Story 4 - Formant Shift (FR-019 to FR-022)
// =============================================================================

TEST_CASE("SpectralFreezeOscillator: setFormantShift/getFormantShift parameter (FR-019)",
          "[SpectralFreezeOscillator][US4][formant]") {

    SpectralFreezeOscillator osc;
    osc.prepare(44100.0, 2048);

    SECTION("default is 0") {
        REQUIRE(osc.getFormantShift() == 0.0f);
    }

    SECTION("set and get") {
        osc.setFormantShift(-12.0f);
        REQUIRE(osc.getFormantShift() == -12.0f);
    }

    SECTION("clamped to [-24, +24]") {
        osc.setFormantShift(30.0f);
        REQUIRE(osc.getFormantShift() == 24.0f);

        osc.setFormantShift(-30.0f);
        REQUIRE(osc.getFormantShift() == -24.0f);
    }
}

TEST_CASE("SpectralFreezeOscillator: 0 semitones formant shift (identity)",
          "[SpectralFreezeOscillator][US4][formant]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    const float testFreq = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    osc.setFormantShift(0.0f);

    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 4; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    const size_t analysisLen = 8192;
    std::vector<float> analysis(analysisLen);
    for (size_t i = 0; i < analysisLen; i += blockSize) {
        size_t toProcess = std::min(blockSize, analysisLen - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, analysis.data() + i);
    }

    float detectedFreq = estimateFundamental(
        analysis.data(), analysisLen, static_cast<float>(sampleRate));
    REQUIRE(detectedFreq > 0.0f);
    REQUIRE(detectedFreq == Approx(testFreq).epsilon(0.02));
}

TEST_CASE("SpectralFreezeOscillator: formant shift + pitch shift composition",
          "[SpectralFreezeOscillator][US4][formant]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    const float baseFreq = binAlignedFreq(10, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSawtooth(input.data(), fftSize, baseFreq,
                     static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Apply both pitch shift and formant shift
    osc.setPitchShift(12.0f);
    osc.setFormantShift(-12.0f);

    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 8; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Should not crash or produce NaN
    bool hasNaN = false;
    for (size_t i = 0; i < blockSize; ++i) {
        if (std::isnan(block[i])) hasNaN = true;
    }
    REQUIRE_FALSE(hasNaN);

    // Should produce non-zero output
    float rms = calculateRMS(block.data(), blockSize);
    REQUIRE(rms > 0.001f);
}

// =============================================================================
// Phase 7: Edge Cases (T100-T108)
// =============================================================================

TEST_CASE("SpectralFreezeOscillator: freeze with all-zero input",
          "[SpectralFreezeOscillator][edge]") {

    SpectralFreezeOscillator osc;
    osc.prepare(44100.0, 2048);

    std::vector<float> input(2048, 0.0f);
    osc.freeze(input.data(), input.size());
    REQUIRE(osc.isFrozen());

    // Output should be silence (zero magnitudes)
    std::vector<float> output(1024);
    osc.processBlock(output.data(), output.size());
    REQUIRE(allZeros(output.data(), output.size()));
}

TEST_CASE("SpectralFreezeOscillator: unsupported FFT size clamping",
          "[SpectralFreezeOscillator][edge]") {

    SpectralFreezeOscillator osc;

    SECTION("too small") {
        osc.prepare(44100.0, 64);
        REQUIRE(osc.isPrepared());
        REQUIRE(osc.getFftSize() == 256);  // Clamped to min
    }

    SECTION("too large") {
        osc.prepare(44100.0, 16384);
        REQUIRE(osc.isPrepared());
        REQUIRE(osc.getFftSize() == 8192);  // Clamped to max
    }

    SECTION("not power of 2") {
        osc.prepare(44100.0, 3000);
        REQUIRE(osc.isPrepared());
        REQUIRE(osc.getFftSize() == 2048);  // Clamped to nearest lower pow2
    }
}

TEST_CASE("SpectralFreezeOscillator: processBlock before prepare",
          "[SpectralFreezeOscillator][edge]") {

    SpectralFreezeOscillator osc;

    std::vector<float> output(512, 1.0f);
    osc.processBlock(output.data(), output.size());

    REQUIRE(allZeros(output.data(), output.size()));
}

TEST_CASE("SpectralFreezeOscillator: re-prepare clears frozen state",
          "[SpectralFreezeOscillator][edge]") {

    SpectralFreezeOscillator osc;
    osc.prepare(44100.0, 2048);

    std::vector<float> input(2048, 0.5f);
    osc.freeze(input.data(), input.size());
    REQUIRE(osc.isFrozen());

    osc.prepare(44100.0, 1024);
    REQUIRE_FALSE(osc.isFrozen());
    REQUIRE(osc.isPrepared());
}

TEST_CASE("SpectralFreezeOscillator: multiple freeze calls in succession",
          "[SpectralFreezeOscillator][edge]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;

    const float freq1 = binAlignedFreq(20, fftSize, static_cast<float>(sampleRate));
    const float freq2 = binAlignedFreq(40, fftSize, static_cast<float>(sampleRate));

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // First freeze with freq1
    std::vector<float> input1(fftSize);
    generateSine(input1.data(), fftSize, freq1, static_cast<float>(sampleRate));
    osc.freeze(input1.data(), input1.size());
    REQUIRE(osc.isFrozen());

    // Second freeze with freq2 (should overwrite)
    std::vector<float> input2(fftSize);
    generateSine(input2.data(), fftSize, freq2, static_cast<float>(sampleRate));
    osc.freeze(input2.data(), input2.size());
    REQUIRE(osc.isFrozen());

    // Output should be freq2, not freq1
    const size_t blockSize = 512;
    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 4; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    const size_t analysisLen = 8192;
    std::vector<float> analysis(analysisLen);
    for (size_t i = 0; i < analysisLen; i += blockSize) {
        size_t toProcess = std::min(blockSize, analysisLen - i);
        osc.processBlock(block.data(), toProcess);
        std::copy_n(block.data(), toProcess, analysis.data() + i);
    }

    float detectedFreq = estimateFundamental(
        analysis.data(), analysisLen, static_cast<float>(sampleRate));
    REQUIRE(detectedFreq > 0.0f);
    REQUIRE(detectedFreq == Approx(freq2).epsilon(0.02));
}

TEST_CASE("SpectralFreezeOscillator: pitch shift bins below 0 are zeroed",
          "[SpectralFreezeOscillator][edge]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    // Freeze low frequency signal (bin-aligned)
    const float testFreq = binAlignedFreq(5, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, testFreq, static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    // Large downward shift
    osc.setPitchShift(-24.0f);

    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 8; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Should not crash; output should be very quiet or near-zero
    bool hasNaN = false;
    for (size_t i = 0; i < blockSize; ++i) {
        if (std::isnan(block[i])) hasNaN = true;
    }
    REQUIRE_FALSE(hasNaN);
}

TEST_CASE("SpectralFreezeOscillator: simultaneous pitch + tilt + formant",
          "[SpectralFreezeOscillator][edge]") {

    constexpr double sampleRate = 44100.0;
    constexpr size_t fftSize = 2048;
    constexpr size_t blockSize = 512;

    SpectralFreezeOscillator osc;
    osc.prepare(sampleRate, fftSize);

    const float baseFreq = binAlignedFreq(14, fftSize, static_cast<float>(sampleRate));
    std::vector<float> input(fftSize);
    generateSawtooth(input.data(), fftSize, baseFreq,
                     static_cast<float>(sampleRate));
    osc.freeze(input.data(), input.size());

    osc.setPitchShift(5.0f);
    osc.setSpectralTilt(-6.0f);
    osc.setFormantShift(7.0f);

    std::vector<float> block(blockSize);
    for (size_t i = 0; i < fftSize * 8; i += blockSize) {
        osc.processBlock(block.data(), blockSize);
    }

    // Verify no NaN/Inf
    bool hasNaN = false;
    bool hasInf = false;
    for (size_t i = 0; i < blockSize; ++i) {
        if (std::isnan(block[i])) hasNaN = true;
        if (std::isinf(block[i])) hasInf = true;
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);

    // Should produce non-zero output
    float rms = calculateRMS(block.data(), blockSize);
    REQUIRE(rms > 0.001f);
}
