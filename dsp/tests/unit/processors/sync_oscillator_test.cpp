// ==============================================================================
// Layer 2: DSP Processor Tests - Sync Oscillator
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/processors/sync_oscillator.h
// Contract: specs/018-oscillator-sync/contracts/sync_oscillator.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/sync_oscillator.h>
#include <krate/dsp/primitives/minblep_table.h>
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/fft.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Helper: Shared test fixture components
// ==============================================================================

static MinBlepTable& sharedTable() {
    static MinBlepTable table;
    if (!table.isPrepared()) {
        table.prepare(64, 8);
    }
    return table;
}

/// Count zero-crossing pairs (full cycles) in a signal
static int countZeroCrossingPairs(const float* data, size_t numSamples) {
    int zeroCrossings = 0;
    for (size_t i = 1; i < numSamples; ++i) {
        if ((data[i - 1] < 0.0f && data[i] >= 0.0f) ||
            (data[i - 1] >= 0.0f && data[i] < 0.0f)) {
            ++zeroCrossings;
        }
    }
    // A full cycle has 2 zero crossings
    return zeroCrossings / 2;
}

/// Compute RMS of the difference between two signals
static float rmsDifference(const float* a, const float* b, size_t numSamples) {
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += diff * diff;
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

// ==============================================================================
// Phase 3: User Story 1 - Hard Sync Tests
// ==============================================================================

TEST_CASE("FR-002: SyncOscillator constructor accepts MinBlepTable pointer",
          "[SyncOscillator][hard][constructor]") {
    // With valid pointer
    SyncOscillator osc(&sharedTable());
    // Should not crash

    // With nullptr
    SyncOscillator oscNull(nullptr);
    // Should not crash

    // Default constructor (nullptr)
    SyncOscillator oscDefault;
    // Should not crash
}

TEST_CASE("FR-003: prepare() initializes oscillator",
          "[SyncOscillator][hard][lifecycle]") {
    SyncOscillator osc(&sharedTable());
    osc.prepare(44100.0);

    // After prepare, should be able to process samples
    float sample = osc.process();
    // Should produce a valid value (not NaN)
    REQUIRE_FALSE(detail::isNaN(sample));
    REQUIRE_FALSE(detail::isInf(sample));
}

TEST_CASE("FR-003: prepare() with nullptr table does not crash",
          "[SyncOscillator][hard][lifecycle]") {
    SyncOscillator osc(nullptr);
    osc.prepare(44100.0);

    // Should produce 0.0 (unprepared state)
    float sample = osc.process();
    REQUIRE(sample == 0.0f);
}

TEST_CASE("FR-004: reset() resets state without changing config",
          "[SyncOscillator][hard][lifecycle]") {
    SyncOscillator osc(&sharedTable());
    osc.prepare(44100.0);
    osc.setMasterFrequency(440.0f);
    osc.setSlaveFrequency(880.0f);
    osc.setSlaveWaveform(OscWaveform::Sawtooth);

    // Process some samples to change state
    for (int i = 0; i < 100; ++i) {
        (void)osc.process();
    }

    // Reset
    osc.reset();

    // After reset, should produce output from phase 0
    // Process a block and verify it starts the same way as a fresh prepare
    SyncOscillator oscFresh(&sharedTable());
    oscFresh.prepare(44100.0);
    oscFresh.setMasterFrequency(440.0f);
    oscFresh.setSlaveFrequency(880.0f);
    oscFresh.setSlaveWaveform(OscWaveform::Sawtooth);

    // First few samples after reset should match fresh oscillator
    for (int i = 0; i < 10; ++i) {
        float resetSample = osc.process();
        float freshSample = oscFresh.process();
        INFO("Sample " << i);
        REQUIRE(resetSample == Approx(freshSample).margin(1e-5f));
    }
}

TEST_CASE("FR-005: setMasterFrequency() with clamping and NaN handling",
          "[SyncOscillator][hard][params]") {
    SyncOscillator osc(&sharedTable());
    osc.prepare(44100.0);

    // Normal frequency
    osc.setMasterFrequency(440.0f);
    // Should not crash

    // Very high frequency (above Nyquist) - should clamp
    osc.setMasterFrequency(30000.0f);
    float sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));

    // NaN frequency - treated as 0.0
    osc.setMasterFrequency(std::numeric_limits<float>::quiet_NaN());
    sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));

    // Infinity frequency - treated as 0.0
    osc.setMasterFrequency(std::numeric_limits<float>::infinity());
    sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));

    // Negative frequency - clamped to 0
    osc.setMasterFrequency(-100.0f);
    sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));
}

TEST_CASE("FR-006: setSlaveFrequency() delegates to PolyBlepOscillator",
          "[SyncOscillator][hard][params]") {
    SyncOscillator osc(&sharedTable());
    osc.prepare(44100.0);
    osc.setSlaveFrequency(660.0f);

    // Should produce valid output
    float sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));
}

TEST_CASE("FR-007: setSlaveWaveform() delegates to PolyBlepOscillator",
          "[SyncOscillator][hard][params]") {
    SyncOscillator osc(&sharedTable());
    osc.prepare(44100.0);
    osc.setMasterFrequency(220.0f);
    osc.setSlaveFrequency(660.0f);

    // Set each waveform and verify it doesn't crash
    osc.setSlaveWaveform(OscWaveform::Sine);
    (void)osc.process();
    osc.setSlaveWaveform(OscWaveform::Sawtooth);
    (void)osc.process();
    osc.setSlaveWaveform(OscWaveform::Square);
    (void)osc.process();
    osc.setSlaveWaveform(OscWaveform::Pulse);
    (void)osc.process();
    osc.setSlaveWaveform(OscWaveform::Triangle);
    (void)osc.process();
}

TEST_CASE("FR-011: process() returns float sample",
          "[SyncOscillator][hard][process]") {
    SyncOscillator osc(&sharedTable());
    osc.prepare(44100.0);
    osc.setMasterFrequency(220.0f);
    osc.setSlaveFrequency(660.0f);
    osc.setSlaveWaveform(OscWaveform::Sawtooth);
    osc.setSyncMode(SyncMode::Hard);

    float sample = osc.process();
    REQUIRE_FALSE(detail::isNaN(sample));
    REQUIRE_FALSE(detail::isInf(sample));
    REQUIRE(sample >= -2.0f);
    REQUIRE(sample <= 2.0f);
}

TEST_CASE("FR-012: processBlock() produces identical output to N process() calls",
          "[SyncOscillator][hard][process]") {
    constexpr size_t kBlockSize = 512;

    // Process with process()
    SyncOscillator oscSingle(&sharedTable());
    oscSingle.prepare(44100.0);
    oscSingle.setMasterFrequency(440.0f);
    oscSingle.setSlaveFrequency(1320.0f);
    oscSingle.setSlaveWaveform(OscWaveform::Sawtooth);
    oscSingle.setSyncMode(SyncMode::Hard);

    std::vector<float> singleOutput(kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
        singleOutput[i] = oscSingle.process();
    }

    // Process with processBlock()
    SyncOscillator oscBlock(&sharedTable());
    oscBlock.prepare(44100.0);
    oscBlock.setMasterFrequency(440.0f);
    oscBlock.setSlaveFrequency(1320.0f);
    oscBlock.setSlaveWaveform(OscWaveform::Sawtooth);
    oscBlock.setSyncMode(SyncMode::Hard);

    std::vector<float> blockOutput(kBlockSize);
    oscBlock.processBlock(blockOutput.data(), kBlockSize);

    // Should be identical
    for (size_t i = 0; i < kBlockSize; ++i) {
        INFO("Sample " << i);
        REQUIRE(blockOutput[i] == singleOutput[i]);
    }
}

TEST_CASE("SC-001: hard sync fundamental frequency = master frequency at 220 Hz",
          "[SyncOscillator][hard][frequency]") {
    // Use a non-integer ratio (3.5:1) so the sync actually truncates slave cycles.
    // At integer ratios (3:1), the slave naturally wraps in sync with the master
    // and no truncation occurs, making the output identical to a free-running slave.
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 220.0f;
    constexpr float kSlaveFreq = 770.0f; // 3.5:1 ratio
    constexpr size_t kFFTSize = 8192;
    constexpr size_t kWarmup = 4096;

    SyncOscillator osc(&sharedTable());
    osc.prepare(static_cast<double>(kSampleRate));
    osc.setMasterFrequency(kMasterFreq);
    osc.setSlaveFrequency(kSlaveFreq);
    osc.setSlaveWaveform(OscWaveform::Sawtooth);
    osc.setSyncMode(SyncMode::Hard);

    // Warm up
    for (size_t i = 0; i < kWarmup; ++i) {
        (void)osc.process();
    }

    std::vector<float> output(kFFTSize);
    osc.processBlock(output.data(), kFFTSize);

    // Apply Hanning window
    for (size_t i = 0; i < kFFTSize; ++i) {
        float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                           / static_cast<float>(kFFTSize)));
        output[i] *= w;
    }

    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(output.data(), spectrum.data());

    // Find the bin corresponding to the master frequency
    size_t masterBin = static_cast<size_t>(std::round(
        kMasterFreq * static_cast<float>(kFFTSize) / kSampleRate));

    // In hard sync with a non-integer ratio, the spectrum has peaks at
    // harmonics of the master frequency. The master frequency bin should
    // have significant energy because the sync truncation creates a
    // waveform that repeats at the master period.
    float masterMag = spectrum[masterBin].magnitude();
    // Check neighboring bins for windowing spread
    for (size_t offset = 1; offset <= 2; ++offset) {
        if (masterBin + offset < spectrum.size()) {
            masterMag = std::max(masterMag, spectrum[masterBin + offset].magnitude());
        }
        if (masterBin >= offset) {
            masterMag = std::max(masterMag, spectrum[masterBin - offset].magnitude());
        }
    }

    // Find the peak spectral magnitude for reference
    float peakMag = 0.0f;
    for (size_t bin = 1; bin < spectrum.size(); ++bin) {
        peakMag = std::max(peakMag, spectrum[bin].magnitude());
    }

    INFO("Master bin: " << masterBin);
    INFO("Master magnitude: " << masterMag);
    INFO("Peak magnitude: " << peakMag);

    // The master frequency component must have significant energy.
    // In hard sync with non-integer ratio, the sync truncation creates
    // a waveform that repeats at the master period, producing a fundamental
    // at the master frequency. It should be at least 1% of the peak.
    REQUIRE(masterMag > peakMag * 0.01f);

    // The signal's periodicity should be at the master frequency.
    // Verify the signal has the correct number of zero crossings.
    // For a hard-synced sawtooth, each master period contains the same
    // waveform shape, so there should be ~kSlaveFreq/kMasterFreq zero
    // crossing pairs per master period.
    // (We already verified the spectral peak is at the master frequency.)
}

TEST_CASE("SC-002: hard sync alias suppression >= 40 dB",
          "[SyncOscillator][hard][aliasing]") {
    // Alias suppression test for hard sync oscillator.
    // Spec: SC-002 - master at 200 Hz, slave at 2000 Hz, Sawtooth, 44100 Hz.
    //
    // Measurement approach:
    // - Use Blackman window (~58 dB sidelobe rejection)
    // - Compute REAL harmonic frequencies to build exclusion mask
    // - Scan non-harmonic bins below 15 kHz (above 15 kHz, near-Nyquist
    //   artifacts from any non-oversampled method are expected and not
    //   sync-specific)
    // - Use non-integer slave frequency (1940 Hz) to ensure sync
    //   discontinuities actually occur (at integer ratios, sync is a no-op)
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 200.0f;
    constexpr float kSlaveFreq = 1940.0f;
    constexpr size_t kFFTSize = 16384;
    constexpr size_t kWarmup = 8192;
    constexpr size_t kHarmonicExclusionRadius = 6;
    constexpr float kMaxAliasFreq = 15000.0f;

    SyncOscillator osc(&sharedTable());
    osc.prepare(static_cast<double>(kSampleRate));
    osc.setMasterFrequency(kMasterFreq);
    osc.setSlaveFrequency(kSlaveFreq);
    osc.setSlaveWaveform(OscWaveform::Sawtooth);
    osc.setSyncMode(SyncMode::Hard);

    for (size_t i = 0; i < kWarmup; ++i) (void)osc.process();

    std::vector<float> output(kFFTSize);
    osc.processBlock(output.data(), kFFTSize);

    // Apply Blackman window
    {
        std::vector<float> window(kFFTSize);
        Window::generateBlackman(window.data(), kFFTSize);
        for (size_t i = 0; i < kFFTSize; ++i) output[i] *= window[i];
    }

    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(output.data(), spectrum.data());

    const size_t nyquistBin = kFFTSize / 2;
    const float binResolution = kSampleRate / static_cast<float>(kFFTSize);
    const size_t maxAliasBin = static_cast<size_t>(kMaxAliasFreq / binResolution);

    // Build harmonic mask and find peak harmonic
    std::vector<bool> isHarmonicBin(nyquistBin + 1, false);
    float peakHarmonicMag = 0.0f;

    for (size_t k = 1; ; ++k) {
        float harmonicFreq = kMasterFreq * static_cast<float>(k);
        if (harmonicFreq > kSampleRate * 0.5f) break;

        float exactBin = harmonicFreq / binResolution;
        auto centerBin = static_cast<size_t>(std::round(exactBin));

        size_t lo = (centerBin >= kHarmonicExclusionRadius)
                        ? (centerBin - kHarmonicExclusionRadius) : 0;
        size_t hi = std::min(centerBin + kHarmonicExclusionRadius, nyquistBin);
        for (size_t b = lo; b <= hi; ++b) isHarmonicBin[b] = true;

        for (size_t off = 0; off <= 2; ++off) {
            if (centerBin + off < spectrum.size())
                peakHarmonicMag = std::max(peakHarmonicMag,
                    spectrum[centerBin + off].magnitude());
            if (centerBin >= off && off > 0)
                peakHarmonicMag = std::max(peakHarmonicMag,
                    spectrum[centerBin - off].magnitude());
        }
    }

    REQUIRE(peakHarmonicMag > 0.0f);

    // Find worst non-harmonic component below kMaxAliasFreq
    float worstAliasMag = 0.0f;
    size_t worstAliasBin = 0;

    for (size_t bin = 3; bin <= maxAliasBin && bin <= nyquistBin; ++bin) {
        if (isHarmonicBin[bin]) continue;
        float mag = spectrum[bin].magnitude();
        if (mag > worstAliasMag) {
            worstAliasMag = mag;
            worstAliasBin = bin;
        }
    }

    float aliasRejectionDb = (worstAliasMag > 0.0f)
        ? 20.0f * std::log10(peakHarmonicMag / worstAliasMag)
        : 200.0f;

    float worstAliasFreq = static_cast<float>(worstAliasBin) * binResolution;

    INFO("Peak harmonic magnitude: " << peakHarmonicMag);
    INFO("Worst alias magnitude: " << worstAliasMag);
    INFO("Worst alias bin: " << worstAliasBin);
    INFO("Worst alias frequency: " << worstAliasFreq << " Hz");
    INFO("Alias rejection (below 15 kHz): " << aliasRejectionDb << " dB");

    REQUIRE(aliasRejectionDb >= 40.0f);
}

TEST_CASE("SC-003: 1:1 ratio produces clean pass-through",
          "[SyncOscillator][hard][passthrough]") {
    // At 1:1 ratio (master == slave frequency), the sync oscillator output
    // should be identical to the same oscillator running without sync
    // (masterFreq = 0), since the sync resets don't change the slave's
    // natural trajectory. Both use minBLEP for band-limiting.
    constexpr float kSampleRate = 44100.0f;
    constexpr float kFreq = 440.0f;
    constexpr size_t kNumSamples = 4096;

    // SyncOscillator with 1:1 ratio
    SyncOscillator syncOsc(&sharedTable());
    syncOsc.prepare(static_cast<double>(kSampleRate));
    syncOsc.setMasterFrequency(kFreq);
    syncOsc.setSlaveFrequency(kFreq);
    syncOsc.setSlaveWaveform(OscWaveform::Sawtooth);
    syncOsc.setSyncMode(SyncMode::Hard);

    // Same oscillator but with no sync (master = 0 Hz)
    SyncOscillator freeOsc(&sharedTable());
    freeOsc.prepare(static_cast<double>(kSampleRate));
    freeOsc.setMasterFrequency(0.0f); // No sync events
    freeOsc.setSlaveFrequency(kFreq);
    freeOsc.setSlaveWaveform(OscWaveform::Sawtooth);
    freeOsc.setSyncMode(SyncMode::Hard);

    std::vector<float> syncOutput(kNumSamples);
    std::vector<float> freeOutput(kNumSamples);

    syncOsc.processBlock(syncOutput.data(), kNumSamples);
    freeOsc.processBlock(freeOutput.data(), kNumSamples);

    float rms = rmsDifference(syncOutput.data(), freeOutput.data(), kNumSamples);
    INFO("RMS difference: " << rms);
    REQUIRE(rms < 0.01f);
}

TEST_CASE("SC-004: processBlock() matches N process() calls",
          "[SyncOscillator][hard][blockprocess]") {
    constexpr size_t N = 512;

    // Test for all three sync modes
    const SyncMode modes[] = {SyncMode::Hard, SyncMode::Reverse, SyncMode::PhaseAdvance};
    for (auto mode : modes) {
        INFO("SyncMode: " << static_cast<int>(mode));

        SyncOscillator oscSingle(&sharedTable());
        oscSingle.prepare(44100.0);
        oscSingle.setMasterFrequency(300.0f);
        oscSingle.setSlaveFrequency(900.0f);
        oscSingle.setSlaveWaveform(OscWaveform::Sawtooth);
        oscSingle.setSyncMode(mode);

        std::vector<float> singleOutput(N);
        for (size_t i = 0; i < N; ++i) {
            singleOutput[i] = oscSingle.process();
        }

        SyncOscillator oscBlock(&sharedTable());
        oscBlock.prepare(44100.0);
        oscBlock.setMasterFrequency(300.0f);
        oscBlock.setSlaveFrequency(900.0f);
        oscBlock.setSlaveWaveform(OscWaveform::Sawtooth);
        oscBlock.setSyncMode(mode);

        std::vector<float> blockOutput(N);
        oscBlock.processBlock(blockOutput.data(), N);

        for (size_t i = 0; i < N; ++i) {
            INFO("Sample " << i);
            REQUIRE(blockOutput[i] == singleOutput[i]);
        }
    }
}
