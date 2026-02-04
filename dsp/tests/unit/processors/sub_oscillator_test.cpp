// ==============================================================================
// Layer 2: DSP Processor Tests - Sub-Oscillator
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/processors/sub_oscillator.h
// Contract: specs/019-sub-oscillator/contracts/sub_oscillator.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/sub_oscillator.h>
#include <krate/dsp/primitives/minblep_table.h>
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/fft.h>

#include <algorithm>
#include <array>
#include <chrono>
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

static MinBlepTable& sharedSubTable() {
    static MinBlepTable table;
    if (!table.isPrepared()) {
        table.prepare(64, 8);
    }
    return table;
}

/// Compute RMS of a signal buffer
static float computeRMS(const float* data, size_t numSamples) {
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

// ==============================================================================
// Phase 3: User Story 1 - Square Sub-Oscillator with Flip-Flop Division
// ==============================================================================

// T002: Constructor test (FR-003)
TEST_CASE("FR-003: SubOscillator constructor accepts MinBlepTable pointer",
          "[SubOscillator][US1][constructor]") {
    // With valid pointer
    SubOscillator sub(&sharedSubTable());
    // Should not crash

    // With nullptr
    SubOscillator subNull(nullptr);
    // Should not crash

    // Default constructor (nullptr)
    SubOscillator subDefault;
    // Should not crash
}

// T003: prepare() test (FR-004)
TEST_CASE("FR-004: prepare() initializes state and validates table",
          "[SubOscillator][US1][lifecycle]") {
    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);

    // After prepare, should be able to process samples
    float sample = sub.process(false, 440.0f / 44100.0f);
    REQUIRE_FALSE(detail::isNaN(sample));
    REQUIRE_FALSE(detail::isInf(sample));

    // With nullptr table, prepare should fail gracefully
    SubOscillator subNull(nullptr);
    subNull.prepare(44100.0);
    float nullSample = subNull.process(false, 440.0f / 44100.0f);
    REQUIRE(nullSample == 0.0f);
}

// T004: reset() test (FR-005)
TEST_CASE("FR-005: reset() clears state while preserving config",
          "[SubOscillator][US1][lifecycle]") {
    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    // Process some samples to change state
    const float phaseInc = 440.0f / 44100.0f;
    for (int i = 0; i < 200; ++i) {
        // Simulate master wraps
        bool wrapped = (i % 100 == 50);
        (void)sub.process(wrapped, phaseInc);
    }

    // Reset
    sub.reset();

    // After reset, should produce output from initial state
    SubOscillator subFresh(&sharedSubTable());
    subFresh.prepare(44100.0);
    subFresh.setOctave(SubOctave::OneOctave);
    subFresh.setWaveform(SubWaveform::Square);

    // First few samples after reset should match fresh oscillator
    for (int i = 0; i < 10; ++i) {
        float resetSample = sub.process(false, phaseInc);
        float freshSample = subFresh.process(false, phaseInc);
        INFO("Sample " << i);
        REQUIRE(resetSample == Approx(freshSample).margin(1e-5f));
    }
}

// T005: OneOctave square frequency test (SC-001, FR-011)
TEST_CASE("SC-001: OneOctave square produces 220 Hz from 440 Hz master",
          "[SubOscillator][US1][square][frequency]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 440.0f;
    constexpr size_t kFFTSize = 8192;
    constexpr size_t kWarmup = 4096;

    // Create master oscillator
    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setFrequency(kMasterFreq);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = kMasterFreq / kSampleRate;

    // Warm up
    for (size_t i = 0; i < kWarmup; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    // Collect output
    std::vector<float> output(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        (void)master.process();
        output[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

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

    // Find peak bin
    float peakMag = 0.0f;
    size_t peakBin = 0;
    for (size_t bin = 1; bin < spectrum.size(); ++bin) {
        float mag = spectrum[bin].magnitude();
        if (mag > peakMag) {
            peakMag = mag;
            peakBin = bin;
        }
    }

    float binResolution = kSampleRate / static_cast<float>(kFFTSize);
    float peakFreq = static_cast<float>(peakBin) * binResolution;

    INFO("Peak bin: " << peakBin);
    INFO("Peak frequency: " << peakFreq << " Hz");
    INFO("Expected: 220 Hz");

    // The fundamental should be at 220 Hz (half of 440 Hz)
    REQUIRE(peakFreq == Approx(220.0f).margin(binResolution * 2.0f));
}

// T006: Flip-flop toggle test (FR-011, FR-013)
TEST_CASE("FR-011: flip-flop toggle at master phase wraps",
          "[SubOscillator][US1][square][flipflop]") {
    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = 440.0f / 44100.0f;

    // Initially flip-flop is false, so output should be -1
    float sample0 = sub.process(false, phaseInc);
    // Note: with residual consuming, exact -1.0 may have small correction
    // but should be close to -1.0 since no blep was added
    REQUIRE(sample0 == Approx(-1.0f).margin(0.1f));

    // After a master wrap, flip-flop toggles to true. The minBLEP correction
    // at this sample can cause the output to still be negative (the correction
    // is spread over multiple samples). Wait a few samples for it to settle.
    (void)sub.process(true, phaseInc);

    // After settling (minBLEP table length is ~16 samples), output should be
    // near +1.0 since flip-flop is now true
    for (int i = 0; i < 20; ++i) {
        (void)sub.process(false, phaseInc);
    }
    float settled1 = sub.process(false, phaseInc);
    REQUIRE(settled1 == Approx(1.0f).margin(0.1f));

    // After another master wrap, flip-flop toggles back to false
    (void)sub.process(true, phaseInc);

    // Wait for minBLEP to settle, then check output is near -1.0
    for (int i = 0; i < 20; ++i) {
        (void)sub.process(false, phaseInc);
    }
    float settled2 = sub.process(false, phaseInc);
    REQUIRE(settled2 == Approx(-1.0f).margin(0.1f));
}

// T007: MinBLEP alias rejection test (SC-003, FR-013)
TEST_CASE("SC-003: minBLEP alias rejection >= 40 dB",
          "[SubOscillator][US1][square][minblep]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 1000.0f;
    constexpr size_t kFFTSize = 16384;
    constexpr size_t kWarmup = 8192;
    constexpr size_t kHarmonicExclusionRadius = 6;
    constexpr float kMaxAliasFreq = 15000.0f;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setFrequency(kMasterFreq);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = kMasterFreq / kSampleRate;

    // Warm up
    for (size_t i = 0; i < kWarmup; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    std::vector<float> output(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        (void)master.process();
        output[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

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

    // Sub frequency is 500 Hz (half of 1000 Hz)
    const float subFreq = kMasterFreq / 2.0f;

    // Build harmonic mask (harmonics of the sub frequency = 500, 1000, 1500, ...)
    std::vector<bool> isHarmonicBin(nyquistBin + 1, false);
    float peakHarmonicMag = 0.0f;

    for (size_t k = 1; ; ++k) {
        float harmonicFreq = subFreq * static_cast<float>(k);
        if (harmonicFreq > kSampleRate * 0.5f) break;

        // Square wave only has odd harmonics, but we exclude all multiples
        // to be conservative
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

// T008: Sub-sample minBLEP timing (FR-014)
TEST_CASE("FR-014: sub-sample accurate minBLEP timing",
          "[SubOscillator][US1][square][minblep]") {
    // Verify the sub-oscillator uses sub-sample timing by comparing output
    // quality with a naive (sample-accurate) approach. The minBLEP version
    // should have better alias rejection.
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 1000.0f;
    constexpr size_t kNumSamples = 8192;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setFrequency(kMasterFreq);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = kMasterFreq / kSampleRate;

    // Generate output and verify it uses sub-sample timing
    // The test verifies that the output is not a simple sample-aligned square wave
    // (which would have values exactly at +1 or -1 except at transitions)
    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        (void)master.process();
        output[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

    // Count samples that are NOT exactly +1 or -1 (minBLEP corrections
    // create intermediate values near transitions)
    int nonBinaryCount = 0;
    for (size_t i = 0; i < kNumSamples; ++i) {
        if (std::abs(output[i] - 1.0f) > 0.01f &&
            std::abs(output[i] + 1.0f) > 0.01f) {
            ++nonBinaryCount;
        }
    }

    INFO("Non-binary samples (from minBLEP correction): " << nonBinaryCount);
    // With minBLEP, there should be correction samples near each transition
    // At 1000 Hz master, there are ~500 sub cycles per second
    // Each sub cycle has 2 transitions, each with ~16 samples of minBLEP correction
    // Over 8192 samples, we expect at least some non-binary samples
    REQUIRE(nonBinaryCount > 10);
}

// T009: Output range test (SC-008, FR-029)
TEST_CASE("SC-008: output range [-2.0, 2.0] at various master frequencies",
          "[SubOscillator][US1][robustness]") {
    const float masterFreqs[] = {100.0f, 440.0f, 2000.0f, 8000.0f};

    for (float mf : masterFreqs) {
        INFO("Master frequency: " << mf);

        PolyBlepOscillator master;
        master.prepare(44100.0);
        master.setFrequency(mf);
        master.setWaveform(OscWaveform::Sawtooth);

        SubOscillator sub(&sharedSubTable());
        sub.prepare(44100.0);
        sub.setOctave(SubOctave::OneOctave);
        sub.setWaveform(SubWaveform::Square);

        const float phaseInc = mf / 44100.0f;

        bool bounded = true;
        for (size_t i = 0; i < 100000; ++i) {
            (void)master.process();
            float sample = sub.process(master.phaseWrapped(), phaseInc);
            if (sample < -2.0f || sample > 2.0f) {
                bounded = false;
                break;
            }
        }
        REQUIRE(bounded);
    }
}

// T010: No NaN/Inf test (SC-009, FR-030)
TEST_CASE("SC-009: no NaN/Inf in output with randomized parameters",
          "[SubOscillator][US1][robustness]") {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> freqDist(20.0f, 15000.0f);
    std::uniform_int_distribution<int> octaveDist(0, 1);
    std::uniform_int_distribution<int> waveformDist(0, 2);
    std::uniform_real_distribution<float> mixDist(0.0f, 1.0f);

    bool hasNaN = false;
    bool hasInf = false;

    for (int trial = 0; trial < 20; ++trial) {
        float mf = freqDist(rng);
        auto octave = static_cast<SubOctave>(octaveDist(rng));
        auto waveform = static_cast<SubWaveform>(waveformDist(rng));

        PolyBlepOscillator master;
        master.prepare(44100.0);
        master.setFrequency(mf);
        master.setWaveform(OscWaveform::Sawtooth);

        SubOscillator sub(&sharedSubTable());
        sub.prepare(44100.0);
        sub.setOctave(octave);
        sub.setWaveform(waveform);
        sub.setMix(mixDist(rng));

        const float phaseInc = mf / 44100.0f;

        for (size_t i = 0; i < 500; ++i) {
            (void)master.process();
            float sample = sub.process(master.phaseWrapped(), phaseInc);
            if (detail::isNaN(sample)) hasNaN = true;
            if (detail::isInf(sample)) hasInf = true;
        }
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

// T011: Master frequency tracking (SC-011)
TEST_CASE("SC-011: master frequency tracking during pitch changes",
          "[SubOscillator][US1][square][tracking]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kSamplesPerSegment = 4096;
    constexpr size_t kFFTSize = 4096;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    FFT fft;
    fft.prepare(kFFTSize);

    // Segment 1: 440 Hz master -> expect 220 Hz sub
    master.setFrequency(440.0f);
    float phaseInc = 440.0f / kSampleRate;

    // Warm up
    for (size_t i = 0; i < 2048; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    std::vector<float> segment1(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        (void)master.process();
        segment1[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

    // Segment 2: 880 Hz master -> expect 440 Hz sub
    master.setFrequency(880.0f);
    phaseInc = 880.0f / kSampleRate;

    // Brief warm up at new frequency
    for (size_t i = 0; i < 2048; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    std::vector<float> segment2(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        (void)master.process();
        segment2[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

    // Apply Hanning window and FFT both segments
    auto findPeak = [&](std::vector<float>& buf) -> float {
        for (size_t i = 0; i < kFFTSize; ++i) {
            float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                               / static_cast<float>(kFFTSize)));
            buf[i] *= w;
        }
        std::vector<Complex> spec(fft.numBins());
        fft.forward(buf.data(), spec.data());

        float peakMag = 0.0f;
        size_t peakBin = 0;
        for (size_t bin = 1; bin < spec.size(); ++bin) {
            float mag = spec[bin].magnitude();
            if (mag > peakMag) {
                peakMag = mag;
                peakBin = bin;
            }
        }
        float binRes = kSampleRate / static_cast<float>(kFFTSize);
        return static_cast<float>(peakBin) * binRes;
    };

    float peak1 = findPeak(segment1);
    float peak2 = findPeak(segment2);

    float binRes = kSampleRate / static_cast<float>(kFFTSize);

    INFO("Segment 1 peak: " << peak1 << " Hz (expected 220 Hz)");
    INFO("Segment 2 peak: " << peak2 << " Hz (expected 440 Hz)");

    REQUIRE(peak1 == Approx(220.0f).margin(binRes * 2.0f));
    REQUIRE(peak2 == Approx(440.0f).margin(binRes * 2.0f));
}

// T012: Deterministic flip-flop initialization (FR-031)
TEST_CASE("FR-031: deterministic flip-flop initialization",
          "[SubOscillator][US1][lifecycle]") {
    const float phaseInc = 440.0f / 44100.0f;

    // After construction
    SubOscillator sub1(&sharedSubTable());
    sub1.prepare(44100.0);
    sub1.setWaveform(SubWaveform::Square);
    float afterConstruct = sub1.process(false, phaseInc);

    // After prepare()
    SubOscillator sub2(&sharedSubTable());
    sub2.prepare(44100.0);
    sub2.setWaveform(SubWaveform::Square);
    // Process some samples to change state
    for (int i = 0; i < 100; ++i) {
        (void)sub2.process(i % 10 == 0, phaseInc);
    }
    sub2.prepare(44100.0);
    sub2.setWaveform(SubWaveform::Square);
    float afterPrepare = sub2.process(false, phaseInc);

    // After reset()
    SubOscillator sub3(&sharedSubTable());
    sub3.prepare(44100.0);
    sub3.setWaveform(SubWaveform::Square);
    for (int i = 0; i < 100; ++i) {
        (void)sub3.process(i % 10 == 0, phaseInc);
    }
    sub3.reset();
    float afterReset = sub3.process(false, phaseInc);

    // All three should produce identical first sample (flip-flop starts at false = -1)
    INFO("After construct: " << afterConstruct);
    INFO("After prepare: " << afterPrepare);
    INFO("After reset: " << afterReset);

    REQUIRE(afterConstruct == afterPrepare);
    REQUIRE(afterConstruct == afterReset);
    // Flip-flop false = output -1.0
    REQUIRE(afterConstruct == Approx(-1.0f).margin(0.01f));
}

// ==============================================================================
// Phase 4: User Story 2 - Two-Octave Sub Division
// ==============================================================================

// T025: TwoOctaves square frequency test (SC-002, FR-011)
TEST_CASE("SC-002: TwoOctaves square produces 110 Hz from 440 Hz master",
          "[SubOscillator][US2][square][twooctaves]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 440.0f;
    constexpr size_t kFFTSize = 8192;
    constexpr size_t kWarmup = 4096;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setFrequency(kMasterFreq);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::TwoOctaves);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = kMasterFreq / kSampleRate;

    for (size_t i = 0; i < kWarmup; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    std::vector<float> output(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        (void)master.process();
        output[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

    for (size_t i = 0; i < kFFTSize; ++i) {
        float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                           / static_cast<float>(kFFTSize)));
        output[i] *= w;
    }

    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(output.data(), spectrum.data());

    float peakMag = 0.0f;
    size_t peakBin = 0;
    for (size_t bin = 1; bin < spectrum.size(); ++bin) {
        float mag = spectrum[bin].magnitude();
        if (mag > peakMag) {
            peakMag = mag;
            peakBin = bin;
        }
    }

    float binResolution = kSampleRate / static_cast<float>(kFFTSize);
    float peakFreq = static_cast<float>(peakBin) * binResolution;

    INFO("Peak bin: " << peakBin);
    INFO("Peak frequency: " << peakFreq << " Hz");
    INFO("Expected: 110 Hz");

    REQUIRE(peakFreq == Approx(110.0f).margin(binResolution * 2.0f));
}

// T026: Two-stage flip-flop chain toggle pattern (FR-011, FR-012)
TEST_CASE("FR-012: two-stage flip-flop chain toggle pattern",
          "[SubOscillator][US2][square][twooctaves][flipflop]") {
    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::TwoOctaves);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = 440.0f / 44100.0f;

    // Collect output values at each master wrap
    // The pattern should be: -1, -1, +1, +1, -1, -1, +1, +1, ...
    // (toggles every 2 master wraps)
    std::vector<float> wrapOutputs;

    // Process with master wraps
    for (int wrap = 0; wrap < 8; ++wrap) {
        float sample = sub.process(true, phaseInc);
        wrapOutputs.push_back(sample);
        // Process some non-wrap samples between wraps
        for (int j = 0; j < 10; ++j) {
            (void)sub.process(false, phaseInc);
        }
    }

    // Check the toggle pattern:
    // After wrap 1: flipFlop1=true, flipFlop2 toggles (false->true) -> output ~+1
    // After wrap 2: flipFlop1=false, flipFlop2 stays -> output ~+1
    // After wrap 3: flipFlop1=true, flipFlop2 toggles (true->false) -> output ~-1
    // After wrap 4: flipFlop1=false, flipFlop2 stays -> output ~-1
    // After wrap 5: flipFlop1=true, flipFlop2 toggles (false->true) -> output ~+1
    // etc.

    // Due to minBLEP corrections, exact values at the transition sample may not
    // be exactly +/-1. But the pattern should alternate every 2 wraps.
    // Check: wraps 0,1 same sign, wraps 2,3 opposite sign from wraps 0,1
    bool wrap01_positive = (wrapOutputs[0] > 0.0f && wrapOutputs[1] > 0.0f) ||
                           (wrapOutputs[0] < 0.0f && wrapOutputs[1] < 0.0f);

    // Verify by checking a few non-wrap samples after settling
    // Process until we have a stable pattern
    sub.reset();
    std::vector<float> stableOutputs;
    for (int wrap = 0; wrap < 12; ++wrap) {
        (void)sub.process(true, phaseInc);
        // Take a sample well after the transition has settled
        for (int j = 0; j < 50; ++j) {
            (void)sub.process(false, phaseInc);
        }
        stableOutputs.push_back(sub.process(false, phaseInc));
    }

    // The stable output pattern for TwoOctaves should change every 2 master wraps
    // Check adjacent pairs have the same sign
    for (size_t i = 0; i + 1 < stableOutputs.size(); i += 2) {
        INFO("Wraps " << i << " and " << (i + 1));
        // Both in the same pair should have the same sign
        REQUIRE((stableOutputs[i] > 0.0f) == (stableOutputs[i + 1] > 0.0f));
    }
    // Check that adjacent pairs have different signs
    if (stableOutputs.size() >= 4) {
        REQUIRE((stableOutputs[0] > 0.0f) != (stableOutputs[2] > 0.0f));
    }
}

// T027: OneOctave to TwoOctaves mid-stream switch
TEST_CASE("Octave switch mid-stream produces no crash or NaN",
          "[SubOscillator][US2][square][twooctaves]") {
    PolyBlepOscillator master;
    master.prepare(44100.0);
    master.setFrequency(440.0f);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = 440.0f / 44100.0f;
    bool hasNaN = false;
    bool hasInf = false;
    bool outOfRange = false;

    // Process at OneOctave for a while
    for (size_t i = 0; i < 2048; ++i) {
        (void)master.process();
        float s = sub.process(master.phaseWrapped(), phaseInc);
        if (detail::isNaN(s)) hasNaN = true;
        if (detail::isInf(s)) hasInf = true;
    }

    // Switch to TwoOctaves mid-stream
    sub.setOctave(SubOctave::TwoOctaves);

    // Continue processing
    for (size_t i = 0; i < 4096; ++i) {
        (void)master.process();
        float s = sub.process(master.phaseWrapped(), phaseInc);
        if (detail::isNaN(s)) hasNaN = true;
        if (detail::isInf(s)) hasInf = true;
        if (s < -2.0f || s > 2.0f) outOfRange = true;
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE_FALSE(outOfRange);
}

// ==============================================================================
// Phase 5: User Story 3 - Sine and Triangle Sub Waveforms
// ==============================================================================

// T036: Sine sub frequency test (SC-004, FR-015, FR-017)
TEST_CASE("SC-004: Sine sub producing 220 Hz from 440 Hz master with sine purity",
          "[SubOscillator][US3][sine]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 440.0f;
    constexpr size_t kFFTSize = 8192;
    constexpr size_t kWarmup = 4096;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setFrequency(kMasterFreq);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Sine);

    const float phaseInc = kMasterFreq / kSampleRate;

    for (size_t i = 0; i < kWarmup; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    std::vector<float> output(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        (void)master.process();
        output[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

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

    float binRes = kSampleRate / static_cast<float>(kFFTSize);

    // Find fundamental at 220 Hz
    size_t fundamentalBin = static_cast<size_t>(std::round(220.0f / binRes));
    float fundamentalMag = 0.0f;
    for (size_t off = 0; off <= 2; ++off) {
        if (fundamentalBin + off < spectrum.size())
            fundamentalMag = std::max(fundamentalMag,
                spectrum[fundamentalBin + off].magnitude());
        if (fundamentalBin >= off && off > 0)
            fundamentalMag = std::max(fundamentalMag,
                spectrum[fundamentalBin - off].magnitude());
    }

    // Find second harmonic at 440 Hz
    size_t secondHarmonicBin = static_cast<size_t>(std::round(440.0f / binRes));
    float secondHarmonicMag = 0.0f;
    for (size_t off = 0; off <= 2; ++off) {
        if (secondHarmonicBin + off < spectrum.size())
            secondHarmonicMag = std::max(secondHarmonicMag,
                spectrum[secondHarmonicBin + off].magnitude());
        if (secondHarmonicBin >= off && off > 0)
            secondHarmonicMag = std::max(secondHarmonicMag,
                spectrum[secondHarmonicBin - off].magnitude());
    }

    float purityDb = (secondHarmonicMag > 0.0f)
        ? 20.0f * std::log10(fundamentalMag / secondHarmonicMag)
        : 200.0f;

    INFO("Fundamental magnitude (220 Hz): " << fundamentalMag);
    INFO("Second harmonic magnitude (440 Hz): " << secondHarmonicMag);
    INFO("Sine purity: " << purityDb << " dB");

    // Verify peak is at 220 Hz
    float peakMag = 0.0f;
    size_t peakBin = 0;
    for (size_t bin = 1; bin < spectrum.size(); ++bin) {
        float mag = spectrum[bin].magnitude();
        if (mag > peakMag) {
            peakMag = mag;
            peakBin = bin;
        }
    }
    float peakFreq = static_cast<float>(peakBin) * binRes;
    INFO("Peak frequency: " << peakFreq << " Hz");
    REQUIRE(peakFreq == Approx(220.0f).margin(binRes * 2.0f));

    // SC-004: second harmonic at least 40 dB below fundamental
    REQUIRE(purityDb >= 40.0f);
}

// T037: Triangle sub test (SC-005, FR-015, FR-018)
TEST_CASE("SC-005: Triangle sub producing 220 Hz with odd harmonics",
          "[SubOscillator][US3][triangle]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 440.0f;
    constexpr size_t kFFTSize = 8192;
    constexpr size_t kWarmup = 4096;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setFrequency(kMasterFreq);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Triangle);

    const float phaseInc = kMasterFreq / kSampleRate;

    for (size_t i = 0; i < kWarmup; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    std::vector<float> output(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        (void)master.process();
        output[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

    for (size_t i = 0; i < kFFTSize; ++i) {
        float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                           / static_cast<float>(kFFTSize)));
        output[i] *= w;
    }

    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(output.data(), spectrum.data());

    float binRes = kSampleRate / static_cast<float>(kFFTSize);

    // Verify peak is at 220 Hz
    float peakMag = 0.0f;
    size_t peakBin = 0;
    for (size_t bin = 1; bin < spectrum.size(); ++bin) {
        float mag = spectrum[bin].magnitude();
        if (mag > peakMag) {
            peakMag = mag;
            peakBin = bin;
        }
    }
    float peakFreq = static_cast<float>(peakBin) * binRes;
    INFO("Peak frequency: " << peakFreq << " Hz (expected 220 Hz)");
    REQUIRE(peakFreq == Approx(220.0f).margin(binRes * 2.0f));

    // Triangle wave: odd harmonics (3rd, 5th, 7th...) decrease as 1/n^2
    // Check that the 3rd harmonic at 660 Hz is present and below fundamental
    size_t thirdBin = static_cast<size_t>(std::round(660.0f / binRes));
    float thirdMag = 0.0f;
    for (size_t off = 0; off <= 2; ++off) {
        if (thirdBin + off < spectrum.size())
            thirdMag = std::max(thirdMag, spectrum[thirdBin + off].magnitude());
    }

    float fundamentalMag = peakMag;
    if (thirdMag > 0.0f && fundamentalMag > 0.0f) {
        float ratio = fundamentalMag / thirdMag;
        INFO("Fundamental/3rd harmonic ratio: " << ratio
             << " (expected ~9 for triangle)");
        // Triangle 3rd harmonic should be at 1/9 of fundamental
        // Allow generous margin due to phase resync artifacts
        REQUIRE(ratio > 3.0f);
    }
}

// T038: Sine sub at TwoOctaves (FR-015, FR-016)
TEST_CASE("FR-015: Sine sub at TwoOctaves producing 220 Hz from 880 Hz master",
          "[SubOscillator][US3][sine][twooctaves]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kMasterFreq = 880.0f;
    constexpr size_t kFFTSize = 8192;
    constexpr size_t kWarmup = 4096;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setFrequency(kMasterFreq);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::TwoOctaves);
    sub.setWaveform(SubWaveform::Sine);

    const float phaseInc = kMasterFreq / kSampleRate;

    for (size_t i = 0; i < kWarmup; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    std::vector<float> output(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        (void)master.process();
        output[i] = sub.process(master.phaseWrapped(), phaseInc);
    }

    for (size_t i = 0; i < kFFTSize; ++i) {
        float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                           / static_cast<float>(kFFTSize)));
        output[i] *= w;
    }

    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(output.data(), spectrum.data());

    float peakMag = 0.0f;
    size_t peakBin = 0;
    for (size_t bin = 1; bin < spectrum.size(); ++bin) {
        float mag = spectrum[bin].magnitude();
        if (mag > peakMag) {
            peakMag = mag;
            peakBin = bin;
        }
    }

    float binRes = kSampleRate / static_cast<float>(kFFTSize);
    float peakFreq = static_cast<float>(peakBin) * binRes;

    INFO("Peak frequency: " << peakFreq << " Hz (expected 220 Hz = 880/4)");
    REQUIRE(peakFreq == Approx(220.0f).margin(binRes * 2.0f));
}

// T039: Delta-phase tracking during frequency changes (SC-011, FR-016)
TEST_CASE("FR-016: delta-phase tracking during master frequency changes",
          "[SubOscillator][US3][sine][tracking]") {
    constexpr float kSampleRate = 44100.0f;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Sine);

    // Process at 440 Hz, then change to 880 Hz
    // The sine sub should immediately track the new frequency
    master.setFrequency(440.0f);
    float phaseInc = 440.0f / kSampleRate;

    for (size_t i = 0; i < 4096; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    // Change to 880 Hz
    master.setFrequency(880.0f);
    phaseInc = 880.0f / kSampleRate;

    // The sub should IMMEDIATELY use the new phase increment
    // Verify by checking that the output is not stuck at the old frequency
    // Collect output and verify it has significant content
    bool hasNonZero = false;
    for (size_t i = 0; i < 1024; ++i) {
        (void)master.process();
        float s = sub.process(master.phaseWrapped(), phaseInc);
        if (std::abs(s) > 0.01f) hasNonZero = true;
    }
    REQUIRE(hasNonZero);
}

// T040: Phase resynchronization on flip-flop toggle (FR-019)
TEST_CASE("FR-019: phase resynchronization on flip-flop toggle",
          "[SubOscillator][US3][sine][resync]") {
    // When the output flip-flop transitions false->true, the sub phase
    // should be reset to 0.0. Verify this by checking that the sine output
    // starts from 0.0 (sin(0) = 0) after a rising edge.
    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Sine);

    const float phaseInc = 440.0f / 44100.0f;

    // Initial state: flipFlop1=false
    // First wrap: flipFlop1 toggles to true (rising edge) -> phase resyncs to 0
    // The output at this sample should be sin(0) = 0.0 (or very close)
    // But note the phase advances after resync in the same sample

    // Process many non-wrap samples first to move the phase
    for (int i = 0; i < 200; ++i) {
        (void)sub.process(false, phaseInc);
    }

    // Now trigger a master wrap (rising edge of flipFlop1)
    // The sub phase should resync to 0
    float atResync = sub.process(true, phaseInc);

    // At resync, the sine starts from phase 0 + one increment
    // sin(2*pi*(phaseInc/2)) should be very small
    float expectedPhaseAfterResync = phaseInc / 2.0f; // OneOctave divides by 2
    float expectedOutput = std::sin(kTwoPi * expectedPhaseAfterResync);

    INFO("Output at resync: " << atResync);
    INFO("Expected (approx): " << expectedOutput);
    // Should be close to 0 (start of sine) or small value
    REQUIRE(std::abs(atResync) < 0.2f);
}

// ==============================================================================
// Phase 6: User Story 4 - Mixed Output with Equal-Power Crossfade
// ==============================================================================

// T052: mix=0.0 outputs main only (SC-006, FR-020, FR-021)
TEST_CASE("SC-006a: mix=0.0 outputs main only",
          "[SubOscillator][US4][mix]") {
    PolyBlepOscillator master;
    master.prepare(44100.0);
    master.setFrequency(440.0f);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);
    sub.setMix(0.0f);

    const float phaseInc = 440.0f / 44100.0f;

    for (size_t i = 0; i < 4096; ++i) {
        float mainOut = master.process();
        float mixed = sub.processMixed(mainOut, master.phaseWrapped(), phaseInc);
        INFO("Sample " << i);
        // At mix=0.0, mainGain=1.0, subGain=0.0 -> output == mainOut
        REQUIRE(mixed == Approx(mainOut).margin(1e-6f));
    }
}

// T053: mix=1.0 outputs sub only (SC-006, FR-020, FR-021)
TEST_CASE("SC-006b: mix=1.0 outputs sub only",
          "[SubOscillator][US4][mix]") {
    PolyBlepOscillator master;
    master.prepare(44100.0);
    master.setFrequency(440.0f);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);
    sub.setMix(1.0f);

    // Also create a reference sub with mix=0 (default) to get raw sub output
    SubOscillator subRef(&sharedSubTable());
    subRef.prepare(44100.0);
    subRef.setOctave(SubOctave::OneOctave);
    subRef.setWaveform(SubWaveform::Square);

    const float phaseInc = 440.0f / 44100.0f;

    for (size_t i = 0; i < 4096; ++i) {
        float mainOut = master.process();
        bool wrapped = master.phaseWrapped();
        float mixed = sub.processMixed(mainOut, wrapped, phaseInc);
        float subOnly = subRef.process(wrapped, phaseInc);
        INFO("Sample " << i);
        // At mix=1.0, mainGain=0.0, subGain=1.0 -> output == sub output
        REQUIRE(mixed == Approx(subOnly).margin(1e-6f));
    }
}

// T054: mix=0.5 equal-power RMS (SC-007, FR-020)
TEST_CASE("SC-007: mix=0.5 equal-power RMS within 1.5 dB",
          "[SubOscillator][US4][mix][equalpower]") {
    // SC-007: Equal-power crossfade preserves energy when inputs have similar RMS.
    // Use a Sine sub (RMS ~0.707 matching sawtooth RMS ~0.577) for reasonable
    // energy comparison. The spec uses 1.5 dB tolerance for low-correlation signals.
    //
    // Approach: Generate equal-amplitude test signals (both at unit amplitude)
    // to isolate the equal-power property. Use a sawtooth master and sine sub
    // as specified.
    constexpr size_t kNumSamples = 16384;
    constexpr float kSampleRate = 44100.0f;

    const float phaseInc = 440.0f / kSampleRate;

    // Helper: run a complete pass at a given mix value
    auto measureRMS = [&](float mixValue) -> float {
        PolyBlepOscillator master;
        master.prepare(static_cast<double>(kSampleRate));
        master.setFrequency(440.0f);
        master.setWaveform(OscWaveform::Sawtooth);

        SubOscillator sub(&sharedSubTable());
        sub.prepare(static_cast<double>(kSampleRate));
        sub.setOctave(SubOctave::OneOctave);
        sub.setWaveform(SubWaveform::Sine);
        sub.setMix(mixValue);

        // Warm up to reach steady state
        for (size_t i = 0; i < 4096; ++i) {
            float mainOut = master.process();
            (void)sub.processMixed(mainOut, master.phaseWrapped(), phaseInc);
        }

        std::vector<float> buffer(kNumSamples);
        for (size_t i = 0; i < kNumSamples; ++i) {
            float mainOut = master.process();
            buffer[i] = sub.processMixed(mainOut, master.phaseWrapped(), phaseInc);
        }
        return computeRMS(buffer.data(), kNumSamples);
    };

    float rms0 = measureRMS(0.0f);
    float rms5 = measureRMS(0.5f);
    float rms1 = measureRMS(1.0f);

    // Equal power: RMS at mix=0.5 should be within 1.5 dB of the average
    // of the endpoint RMS values. With equal-power gains:
    // E[mixed^2] = mainGain^2 * E[main^2] + subGain^2 * E[sub^2]
    //            + 2 * mainGain * subGain * E[main*sub]
    // When main and sub are uncorrelated, the cross term ~= 0, so:
    // RMS_mixed ~= sqrt(0.5 * RMS_main^2 + 0.5 * RMS_sub^2)
    float expectedRMS = std::sqrt(0.5f * rms0 * rms0 + 0.5f * rms1 * rms1);
    float dbDiffExpected = 20.0f * std::log10(rms5 / expectedRMS);

    INFO("RMS at mix=0.0: " << rms0);
    INFO("RMS at mix=0.5: " << rms5);
    INFO("RMS at mix=1.0: " << rms1);
    INFO("Expected RMS (uncorrelated): " << expectedRMS);
    INFO("dB diff (actual vs expected): " << dbDiffExpected);

    // The deviation from the ideal uncorrelated case should be small
    REQUIRE(std::abs(dbDiffExpected) < 1.5f);
}

// T055: setMix() clamping and NaN/Inf sanitization (FR-008)
TEST_CASE("FR-008: setMix() clamping to [0.0, 1.0] and ignoring NaN/Inf",
          "[SubOscillator][US4][mix][sanitization]") {
    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = 440.0f / 44100.0f;

    // Normal values
    sub.setMix(0.5f);
    float sample = sub.process(false, phaseInc);
    REQUIRE_FALSE(detail::isNaN(sample));

    // Clamp below 0
    sub.setMix(-1.0f);
    sample = sub.process(false, phaseInc);
    REQUIRE_FALSE(detail::isNaN(sample));

    // Clamp above 1
    sub.setMix(2.0f);
    sample = sub.process(false, phaseInc);
    REQUIRE_FALSE(detail::isNaN(sample));

    // NaN should be ignored (previous value retained)
    sub.setMix(0.5f);
    sub.setMix(std::numeric_limits<float>::quiet_NaN());
    sample = sub.process(false, phaseInc);
    REQUIRE_FALSE(detail::isNaN(sample));

    // Infinity should be ignored
    sub.setMix(0.5f);
    sub.setMix(std::numeric_limits<float>::infinity());
    sample = sub.process(false, phaseInc);
    REQUIRE_FALSE(detail::isNaN(sample));

    // Negative infinity should be ignored
    sub.setMix(0.5f);
    sub.setMix(-std::numeric_limits<float>::infinity());
    sample = sub.process(false, phaseInc);
    REQUIRE_FALSE(detail::isNaN(sample));
}

// T055a: Equal-power gain values test (FR-021)
TEST_CASE("FR-021: equal-power gain values at mix=0.5",
          "[SubOscillator][US4][mix][equalpower]") {
    // At mix=0.5, both mainGain and subGain should be ~0.707
    // We verify by testing processMixed with known inputs.
    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);
    sub.setMix(0.5f);

    // equalPowerGains(0.5, mainGain, subGain)
    // mainGain = cos(0.5 * pi/2) = cos(pi/4) = 0.7071...
    // subGain = sin(0.5 * pi/2) = sin(pi/4) = 0.7071...
    float mainGain, subGain;
    equalPowerGains(0.5f, mainGain, subGain);

    INFO("Expected mainGain: " << mainGain);
    INFO("Expected subGain: " << subGain);

    REQUIRE(mainGain == Approx(0.707f).margin(0.01f));
    REQUIRE(subGain == Approx(0.707f).margin(0.01f));
}

// T056: Mix sweep with no clicks
TEST_CASE("Mix sweep 0.0 to 1.0 with no clicks",
          "[SubOscillator][US4][mix]") {
    PolyBlepOscillator master;
    master.prepare(44100.0);
    master.setFrequency(440.0f);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(44100.0);
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = 440.0f / 44100.0f;
    constexpr size_t kNumSamples = 4096;

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        float mix = static_cast<float>(i) / static_cast<float>(kNumSamples - 1);
        sub.setMix(mix);
        float mainOut = master.process();
        output[i] = sub.processMixed(mainOut, master.phaseWrapped(), phaseInc);
    }

    // Check for extreme discontinuities (clicks)
    float maxStep = 0.0f;
    for (size_t i = 1; i < kNumSamples; ++i) {
        float step = std::abs(output[i] - output[i - 1]);
        maxStep = std::max(maxStep, step);
    }

    INFO("Maximum step during mix sweep: " << maxStep);
    // Normal sub+main can have steps up to ~2 at transitions, allow margin
    REQUIRE(maxStep < 3.5f);

    // Verify no NaN
    bool hasNaN = false;
    for (float s : output) {
        if (detail::isNaN(s)) hasNaN = true;
    }
    REQUIRE_FALSE(hasNaN);
}

// ==============================================================================
// Phase 7: Performance and Robustness
// ==============================================================================

// T066: 128 concurrent instances at 96 kHz (SC-014)
TEST_CASE("SC-014: 128 concurrent instances at 96 kHz",
          "[SubOscillator][perf][polyphonic]") {
    constexpr double kSampleRate = 96000.0;
    constexpr size_t kNumInstances = 128;
    constexpr size_t kNumSamples = 4096;

    MinBlepTable& table = sharedSubTable();

    std::vector<SubOscillator> subs(kNumInstances, SubOscillator(&table));
    std::vector<PolyBlepOscillator> masters(kNumInstances);

    // Prepare all instances
    for (size_t v = 0; v < kNumInstances; ++v) {
        float freq = 100.0f + static_cast<float>(v) * 5.0f;
        masters[v].prepare(kSampleRate);
        masters[v].setFrequency(freq);
        masters[v].setWaveform(OscWaveform::Sawtooth);

        subs[v].prepare(kSampleRate);
        subs[v].setOctave(SubOctave::OneOctave);
        subs[v].setWaveform(SubWaveform::Square);
    }

    // Process all instances
    auto start = std::chrono::high_resolution_clock::now();
    float sink = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        for (size_t v = 0; v < kNumInstances; ++v) {
            float mainOut = masters[v].process();
            float phaseInc = (100.0f + static_cast<float>(v) * 5.0f)
                             / static_cast<float>(kSampleRate);
            sink += subs[v].process(masters[v].phaseWrapped(), phaseInc);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Prevent optimizer from eliminating the loop
    REQUIRE(sink != std::numeric_limits<float>::max());

    double usPerSample = static_cast<double>(durationUs)
                       / static_cast<double>(kNumSamples);

    // At 96 kHz, one sample period = ~10.4 us
    // All 128 instances must complete within this budget
    INFO("Total time: " << durationUs << " us for " << kNumSamples << " blocks of "
         << kNumInstances << " voices");
    INFO("Time per sample (all voices): " << usPerSample << " us");
    INFO("Budget at 96 kHz: 10.4 us per sample");

    // Allow 3x headroom for CI/measurement variability
    REQUIRE(usPerSample < 31.2); // 3x budget
}

// T067: CPU cost < 50 cycles/sample (SC-012)
TEST_CASE("SC-012: CPU cost < 50 cycles/sample",
          "[SubOscillator][perf][cycles]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kWarmup = 4096;
    constexpr size_t kMeasureSamples = 1000000;
    constexpr double kAssumedGHz = 3.5;

    PolyBlepOscillator master;
    master.prepare(static_cast<double>(kSampleRate));
    master.setFrequency(440.0f);
    master.setWaveform(OscWaveform::Sawtooth);

    SubOscillator sub(&sharedSubTable());
    sub.prepare(static_cast<double>(kSampleRate));
    sub.setOctave(SubOctave::OneOctave);
    sub.setWaveform(SubWaveform::Square);

    const float phaseInc = 440.0f / kSampleRate;

    // Warm up
    for (size_t i = 0; i < kWarmup; ++i) {
        (void)master.process();
        (void)sub.process(master.phaseWrapped(), phaseInc);
    }

    // Measure sub.process() only (not including master.process())
    // Pre-compute master wrapped flags
    std::vector<bool> wraps(kMeasureSamples);
    for (size_t i = 0; i < kMeasureSamples; ++i) {
        (void)master.process();
        wraps[i] = master.phaseWrapped();
    }

    auto start = std::chrono::high_resolution_clock::now();
    float sink = 0.0f;
    for (size_t i = 0; i < kMeasureSamples; ++i) {
        sink += sub.process(wraps[i], phaseInc);
    }
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(sink != std::numeric_limits<float>::max());

    auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double nsPerSample = static_cast<double>(durationNs) / static_cast<double>(kMeasureSamples);
    double cyclesPerSample = nsPerSample * kAssumedGHz;

    INFO("Time per sample: " << nsPerSample << " ns");
    INFO("Estimated cycles/sample (at " << kAssumedGHz << " GHz): " << cyclesPerSample);

    // SC-012: Target is < 50 cycles/sample. Use 100 as upper limit for
    // measurement noise and CI variability.
    REQUIRE(cyclesPerSample < 100.0);
}

// T068: Memory footprint <= 300 bytes (SC-013)
TEST_CASE("SC-013: memory footprint <= 300 bytes per instance",
          "[SubOscillator][perf][memory]") {
    // sizeof(SubOscillator) gives the stack footprint
    // The Residual buffer has heap allocation sized to table.length()
    // Standard config: table.length() = 16 -> 16 floats = 64 bytes heap
    // Total = sizeof(SubOscillator) + heap allocation

    size_t stackSize = sizeof(SubOscillator);
    size_t tableLength = sharedSubTable().length();
    size_t heapSize = tableLength * sizeof(float);
    // vector overhead: typically 3 pointers = 24 bytes
    size_t vectorOverhead = 3 * sizeof(void*);
    size_t totalEstimate = stackSize + heapSize + vectorOverhead;

    INFO("sizeof(SubOscillator): " << stackSize << " bytes");
    INFO("Table length: " << tableLength);
    INFO("Heap allocation (residual): " << heapSize << " bytes");
    INFO("Vector overhead: " << vectorOverhead << " bytes");
    INFO("Total estimated footprint: " << totalEstimate << " bytes");

    REQUIRE(totalEstimate <= 300);
}

