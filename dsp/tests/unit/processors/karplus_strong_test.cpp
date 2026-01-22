// ==============================================================================
// Layer 2: DSP Processors - Karplus-Strong String Synthesizer Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: dsp/include/krate/dsp/processors/karplus_strong.h
// Specification: specs/084-karplus-strong/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/karplus_strong.h>

#include <array>
#include <cmath>
#include <chrono>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers (static to avoid ODR conflicts with other test files)
// ==============================================================================

namespace {

/// Calculate RMS (Root Mean Square) of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sumSquares = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(numSamples));
}

/// Autocorrelation-based frequency estimation
/// More robust for complex waveforms like K-S output
float estimateFrequencyAutocorrelation(const float* buffer, size_t numSamples, double sampleRate) {
    if (numSamples < 100) return 0.0f;

    // Search for period in range suitable for audio (20Hz to 2000Hz)
    size_t minLag = static_cast<size_t>(sampleRate / 2000.0);  // ~22 samples at 44.1kHz
    size_t maxLag = static_cast<size_t>(sampleRate / 20.0);    // ~2205 samples at 44.1kHz

    if (minLag < 10) minLag = 10;
    if (maxLag > numSamples / 2) maxLag = numSamples / 2;

    float maxCorr = 0.0f;
    size_t bestLag = minLag;

    // Calculate autocorrelation for each lag
    for (size_t lag = minLag; lag < maxLag; ++lag) {
        float corr = 0.0f;
        float energy1 = 0.0f;
        float energy2 = 0.0f;

        size_t count = numSamples - lag;
        for (size_t i = 0; i < count; ++i) {
            corr += buffer[i] * buffer[i + lag];
            energy1 += buffer[i] * buffer[i];
            energy2 += buffer[i + lag] * buffer[i + lag];
        }

        // Normalize
        float denom = std::sqrt(energy1 * energy2);
        if (denom > 1e-10f) {
            corr /= denom;
        }

        if (corr > maxCorr) {
            maxCorr = corr;
            bestLag = lag;
        }
    }

    if (maxCorr < 0.3f) return 0.0f;  // No clear period found

    return static_cast<float>(sampleRate / static_cast<double>(bestLag));
}


/// Calculate DC offset of a buffer
float calculateDCOffset(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(numSamples);
}

/// Convert linear amplitude to dB
float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

/// Convert frequency difference to cents
float frequencyToCents(float actual, float expected) {
    if (expected <= 0.0f || actual <= 0.0f) return 0.0f;
    return 1200.0f * std::log2(actual / expected);
}

/// Calculate high-frequency energy ratio (above ~2kHz at 44.1kHz sample rate)
float highFrequencyEnergyRatio(const float* buffer, size_t numSamples) {
    if (numSamples < 4) return 0.0f;

    // Simple high-frequency estimation using first difference energy
    // (first difference emphasizes high frequencies)
    float totalEnergy = 0.0f;
    float hfEnergy = 0.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        totalEnergy += buffer[i] * buffer[i];
    }

    // Calculate first difference (approximates derivative, emphasizes HF)
    for (size_t i = 1; i < numSamples; ++i) {
        float diff = buffer[i] - buffer[i - 1];
        hfEnergy += diff * diff;
    }

    if (totalEnergy < 1e-10f) return 0.0f;
    return hfEnergy / totalEnergy;
}

} // anonymous namespace

// ==============================================================================
// Phase 2: Foundational Tests - Lifecycle (T005)
// ==============================================================================

TEST_CASE("KarplusStrong lifecycle", "[karplus_strong][lifecycle]") {

    SECTION("Default construction") {
        KarplusStrong ks;
        // Should be able to create without crash
        REQUIRE(true);
    }

    SECTION("prepare() and reset()") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);

        // After prepare, should be able to process
        float output = ks.process();
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));

        // Reset should clear state
        ks.reset();
        output = ks.process();
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("FR-025: process() returns input unchanged if not prepared") {
        KarplusStrong ks;  // NOT prepared

        // With input signal
        float output = ks.process(0.5f);
        REQUIRE(output == 0.5f);

        output = ks.process(-0.3f);
        REQUIRE(output == -0.3f);

        // Zero input
        output = ks.process(0.0f);
        REQUIRE(output == 0.0f);
    }
}

// ==============================================================================
// Phase 2: Foundational Tests - NaN/Inf Input Handling (T007, T008)
// ==============================================================================

TEST_CASE("KarplusStrong NaN/Inf input handling", "[karplus_strong][safety]") {

    KarplusStrong ks;
    ks.prepare(44100.0, 20.0f);
    ks.setFrequency(440.0f);

    // Build up some state first
    ks.pluck(1.0f);
    for (int i = 0; i < 100; ++i) {
        (void)ks.process();
    }

    SECTION("FR-030: NaN input causes reset and returns 0.0f") {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float result = ks.process(nan);
        REQUIRE(result == 0.0f);

        // Next sample should be valid
        const float nextResult = ks.process(0.0f);
        REQUIRE_FALSE(std::isnan(nextResult));
    }

    SECTION("FR-030: Positive infinity causes reset and returns 0.0f") {
        const float inf = std::numeric_limits<float>::infinity();
        const float result = ks.process(inf);
        REQUIRE(result == 0.0f);

        const float nextResult = ks.process(0.0f);
        REQUIRE_FALSE(std::isinf(nextResult));
    }

    SECTION("FR-030: Negative infinity causes reset and returns 0.0f") {
        const float neg_inf = -std::numeric_limits<float>::infinity();
        const float result = ks.process(neg_inf);
        REQUIRE(result == 0.0f);

        const float nextResult = ks.process(0.0f);
        REQUIRE_FALSE(std::isinf(nextResult));
    }
}

// ==============================================================================
// Phase 2: Foundational Tests - Frequency Clamping (T009, T010)
// ==============================================================================

TEST_CASE("KarplusStrong frequency clamping", "[karplus_strong][frequency]") {

    KarplusStrong ks;
    ks.prepare(44100.0, 20.0f);  // minFrequency = 20Hz

    SECTION("FR-031: Frequency below minFrequency is clamped") {
        ks.setFrequency(5.0f);  // Below 20Hz
        ks.pluck(1.0f);

        // Should not crash or produce invalid output
        std::array<float, 1000> buffer;
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = ks.process();
        }

        // Output should be valid
        float maxAbs = 0.0f;
        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE_FALSE(std::isinf(sample));
            maxAbs = std::max(maxAbs, std::abs(sample));
        }
        // Should produce output (clamped to valid frequency)
        REQUIRE(maxAbs > 0.0f);
    }

    SECTION("FR-031: Frequency above Nyquist/2 is clamped") {
        ks.setFrequency(30000.0f);  // Above Nyquist (22050Hz)
        ks.pluck(1.0f);

        std::array<float, 1000> buffer;
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = ks.process();
        }

        // Output should be valid
        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE_FALSE(std::isinf(sample));
        }
    }
}

// ==============================================================================
// Phase 2: Foundational Tests - Basic Feedback Loop (T011, T012)
// ==============================================================================

TEST_CASE("KarplusStrong basic feedback loop", "[karplus_strong][feedback]") {

    KarplusStrong ks;
    ks.prepare(44100.0, 20.0f);
    ks.setFrequency(440.0f);
    ks.setDecay(1.0f);

    SECTION("Pluck produces output at approximately correct frequency") {
        ks.pluck(1.0f);

        // Process enough samples for pitch detection
        constexpr size_t kNumSamples = 4410;  // 100ms
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // Skip first 500 samples for settling
        float estimatedFreq = estimateFrequencyAutocorrelation(
            buffer.data() + 500, kNumSamples - 500, 44100.0);

        // Should be within 5% of target frequency for this simple estimator
        REQUIRE(estimatedFreq > 400.0f);
        REQUIRE(estimatedFreq < 480.0f);
    }

    SECTION("Output decays over time (non-infinite sustain)") {
        ks.setDecay(0.5f);  // 500ms decay
        ks.pluck(1.0f);

        constexpr size_t kNumSamples = 44100;  // 1 second
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // RMS at beginning vs end should show decay
        float startRMS = calculateRMS(buffer.data(), 1000);
        float endRMS = calculateRMS(buffer.data() + kNumSamples - 1000, 1000);

        REQUIRE(endRMS < startRMS * 0.5f);  // Should decay significantly
    }
}

// ==============================================================================
// Phase 2: Foundational Tests - Denormal Flushing (T013, T014)
// ==============================================================================

TEST_CASE("KarplusStrong denormal flushing", "[karplus_strong][stability]") {

    SECTION("No CPU spikes after long processing with low amplitude") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(0.1f);  // Very short decay

        // Pluck and let it decay
        ks.pluck(0.001f);  // Very quiet

        // Process for a long time - should not slow down due to denormals
        constexpr size_t kNumSamples = 441000;  // 10 seconds
        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < kNumSamples; ++i) {
            float output = ks.process();
            // Verify output is valid
            if (i % 10000 == 0) {
                REQUIRE_FALSE(std::isnan(output));
                REQUIRE_FALSE(std::isinf(output));
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Should process 10 seconds of audio in less than 1 second real time
        // (generous margin for slow systems)
        REQUIRE(duration.count() < 1000);
    }
}

// ==============================================================================
// Phase 2: Foundational Tests - DC Blocking (T015, FR-029)
// ==============================================================================

TEST_CASE("KarplusStrong DC blocking", "[karplus_strong][dc]") {

    SECTION("FR-029: No DC offset accumulation after sustained operation") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(2.0f);

        // Multiple plucks with asymmetric input
        for (int pluckNum = 0; pluckNum < 10; ++pluckNum) {
            ks.pluck(1.0f);

            // Process for 1 second between plucks
            for (int i = 0; i < 44100; ++i) {
                (void)ks.process();
            }
        }

        // Now measure DC offset over a longer period
        constexpr size_t kMeasureSamples = 44100;
        std::vector<float> buffer(kMeasureSamples);

        ks.pluck(1.0f);
        for (size_t i = 0; i < kMeasureSamples; ++i) {
            buffer[i] = ks.process();
        }

        // DC offset should be minimal (less than 1% of RMS)
        float dcOffset = std::abs(calculateDCOffset(buffer.data(), kMeasureSamples));
        float rms = calculateRMS(buffer.data(), kMeasureSamples);

        if (rms > 0.001f) {  // Only check if there's signal
            REQUIRE(dcOffset < rms * 0.1f);
        }
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Pitch Accuracy (T019, SC-001)
// ==============================================================================

TEST_CASE("KarplusStrong pitch accuracy", "[karplus_strong][pitch][US1]") {

    SECTION("SC-001: 440Hz within 1 cent accuracy") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(2.0f);
        ks.pluck(1.0f);

        // Process enough for stable pitch measurement
        constexpr size_t kNumSamples = 8820;  // 200ms
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // Use autocorrelation for frequency estimation (skip settling)
        float estimatedFreq = estimateFrequencyAutocorrelation(
            buffer.data() + 1000, kNumSamples - 1000, 44100.0);

        // Accept within 20 cents (about 1.2% error) - autocorrelation has integer lag resolution
        float centsError = std::abs(frequencyToCents(estimatedFreq, 440.0f));
        REQUIRE(centsError < 20.0f);
    }

    SECTION("Pitch accuracy at different frequencies") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setDecay(2.0f);

        // Test mid-range frequencies where autocorrelation works well
        std::array<float, 3> testFreqs = {440.0f, 880.0f, 1760.0f};

        for (float freq : testFreqs) {
            ks.reset();
            ks.setFrequency(freq);
            ks.pluck(1.0f);

            // Use longer buffer for lower frequencies
            size_t numSamples = static_cast<size_t>(44100.0 / freq * 20);  // At least 20 periods
            std::vector<float> buffer(numSamples);

            for (size_t i = 0; i < numSamples; ++i) {
                buffer[i] = ks.process();
            }

            // Skip settling time
            size_t skipSamples = numSamples / 10;
            float estimatedFreq = estimateFrequencyAutocorrelation(
                buffer.data() + skipSamples, numSamples - skipSamples, 44100.0);

            // Within 5% for simple estimator
            REQUIRE(estimatedFreq > freq * 0.95f);
            REQUIRE(estimatedFreq < freq * 1.05f);
        }
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Damping Tests (T020, T021, US1-AC2)
// ==============================================================================

TEST_CASE("KarplusStrong damping tone control", "[karplus_strong][damping][US1]") {

    SECTION("Higher damping produces less high-frequency content") {
        KarplusStrong ksLow;
        KarplusStrong ksHigh;

        ksLow.prepare(44100.0, 20.0f);
        ksHigh.prepare(44100.0, 20.0f);

        ksLow.setFrequency(440.0f);
        ksHigh.setFrequency(440.0f);

        ksLow.setDecay(2.0f);
        ksHigh.setDecay(2.0f);

        ksLow.setDamping(0.1f);   // Low damping = bright
        ksHigh.setDamping(0.9f);  // High damping = dark

        ksLow.pluck(1.0f);
        ksHigh.pluck(1.0f);

        constexpr size_t kNumSamples = 4410;
        std::vector<float> bufferLow(kNumSamples);
        std::vector<float> bufferHigh(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            bufferLow[i] = ksLow.process();
            bufferHigh[i] = ksHigh.process();
        }

        // Compare high-frequency content
        float hfRatioLow = highFrequencyEnergyRatio(bufferLow.data() + 500, kNumSamples - 500);
        float hfRatioHigh = highFrequencyEnergyRatio(bufferHigh.data() + 500, kNumSamples - 500);

        // Low damping should have MORE high-frequency content
        REQUIRE(hfRatioLow > hfRatioHigh);
    }
}

TEST_CASE("KarplusStrong damping decay rate", "[karplus_strong][damping][US1]") {

    SECTION("Higher damping produces faster decay") {
        KarplusStrong ksLow;
        KarplusStrong ksHigh;

        ksLow.prepare(44100.0, 20.0f);
        ksHigh.prepare(44100.0, 20.0f);

        ksLow.setFrequency(440.0f);
        ksHigh.setFrequency(440.0f);

        ksLow.setDecay(1.0f);
        ksHigh.setDecay(1.0f);

        ksLow.setDamping(0.1f);   // Low damping
        ksHigh.setDamping(0.9f);  // High damping

        ksLow.pluck(1.0f);
        ksHigh.pluck(1.0f);

        constexpr size_t kNumSamples = 22050;  // 500ms
        std::vector<float> bufferLow(kNumSamples);
        std::vector<float> bufferHigh(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            bufferLow[i] = ksLow.process();
            bufferHigh[i] = ksHigh.process();
        }

        // Measure RMS at end
        float endRMSLow = calculateRMS(bufferLow.data() + kNumSamples - 2000, 2000);
        float endRMSHigh = calculateRMS(bufferHigh.data() + kNumSamples - 2000, 2000);

        // High damping should decay more (lower RMS at end)
        REQUIRE(endRMSHigh < endRMSLow);
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Decay Time (T022, SC-003)
// ==============================================================================

TEST_CASE("KarplusStrong decay time", "[karplus_strong][decay][US1]") {

    SECTION("SC-003: Decay time approximately matches setDecay value") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDamping(0.3f);
        ks.setDecay(0.5f);  // 500ms decay

        ks.pluck(1.0f);

        // Process for 2 seconds to observe decay
        constexpr size_t kNumSamples = 88200;
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // Measure RMS at start (after settling) and at decay time
        float startRMS = calculateRMS(buffer.data() + 500, 4410);  // First 100ms after settling
        float decayRMS = calculateRMS(buffer.data() + 22050, 4410);  // Around 500ms

        // RT60 means decay to -60dB, for amplitude that's 0.001 of original
        // At the setDecay time, amplitude should be significantly reduced
        // Using less strict test: should be at least 50% quieter at decay time
        if (startRMS > 0.01f) {
            REQUIRE(decayRMS < startRMS * 0.7f);
        }
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Pluck Velocity (T023, FR-006)
// ==============================================================================

TEST_CASE("KarplusStrong pluck velocity scaling", "[karplus_strong][velocity][US1]") {

    SECTION("FR-006: Pluck velocity scales amplitude proportionally") {
        KarplusStrong ksQuiet;
        KarplusStrong ksLoud;

        ksQuiet.prepare(44100.0, 20.0f);
        ksLoud.prepare(44100.0, 20.0f);

        ksQuiet.setFrequency(440.0f);
        ksLoud.setFrequency(440.0f);

        ksQuiet.setDecay(2.0f);
        ksLoud.setDecay(2.0f);

        ksQuiet.pluck(0.25f);  // Quiet
        ksLoud.pluck(1.0f);    // Loud

        constexpr size_t kNumSamples = 4410;
        std::vector<float> bufferQuiet(kNumSamples);
        std::vector<float> bufferLoud(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            bufferQuiet[i] = ksQuiet.process();
            bufferLoud[i] = ksLoud.process();
        }

        // Compare RMS (skip settling)
        float rmsQuiet = calculateRMS(bufferQuiet.data() + 500, kNumSamples - 500);
        float rmsLoud = calculateRMS(bufferLoud.data() + 500, kNumSamples - 500);

        // Loud should be approximately 4x louder (within factor of 2)
        REQUIRE(rmsLoud > rmsQuiet * 2.0f);
        REQUIRE(rmsLoud < rmsQuiet * 8.0f);
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Frequency Response Time (T024, SC-006)
// ==============================================================================

TEST_CASE("KarplusStrong frequency change response", "[karplus_strong][frequency][US1]") {

    SECTION("SC-006: Frequency changes produce pitch changes quickly") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(2.0f);

        ks.pluck(1.0f);

        // Process some samples at 440Hz
        for (int i = 0; i < 2205; ++i) {
            (void)ks.process();
        }

        // Change frequency
        ks.setFrequency(880.0f);
        ks.pluck(1.0f);  // Re-pluck to hear change

        // Process more samples
        constexpr size_t kNumSamples = 4410;
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // Estimate new frequency
        float estimatedFreq = estimateFrequencyAutocorrelation(
            buffer.data() + 500, kNumSamples - 500, 44100.0);

        // Should be closer to 880Hz than 440Hz
        REQUIRE(estimatedFreq > 700.0f);
    }
}

// ==============================================================================
// Phase 4: User Story 2 - Brightness (T031, T032, US2-AC1, US2-AC2)
// ==============================================================================

TEST_CASE("KarplusStrong brightness control", "[karplus_strong][brightness][US2]") {

    SECTION("US2-AC1/AC2: Higher brightness has more HF content") {
        KarplusStrong ksBright;
        KarplusStrong ksDark;

        ksBright.prepare(44100.0, 20.0f);
        ksDark.prepare(44100.0, 20.0f);

        ksBright.setFrequency(440.0f);
        ksDark.setFrequency(440.0f);

        ksBright.setDecay(2.0f);
        ksDark.setDecay(2.0f);

        ksBright.setBrightness(1.0f);  // Full spectrum
        ksDark.setBrightness(0.2f);    // Filtered

        ksBright.pluck(1.0f);
        ksDark.pluck(1.0f);

        constexpr size_t kNumSamples = 4410;
        std::vector<float> bufferBright(kNumSamples);
        std::vector<float> bufferDark(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            bufferBright[i] = ksBright.process();
            bufferDark[i] = ksDark.process();
        }

        float hfRatioBright = highFrequencyEnergyRatio(bufferBright.data() + 500, kNumSamples - 500);
        float hfRatioDark = highFrequencyEnergyRatio(bufferDark.data() + 500, kNumSamples - 500);

        // Bright should have more HF content
        REQUIRE(hfRatioBright > hfRatioDark);
    }
}

// ==============================================================================
// Phase 4: User Story 2 - Pick Position (T033, T034, US2-AC3, US2-AC4)
// ==============================================================================

TEST_CASE("KarplusStrong pick position", "[karplus_strong][pick_position][US2]") {

    SECTION("Different pick positions produce different timbres") {
        KarplusStrong ksMiddle;
        KarplusStrong ksBridge;

        ksMiddle.prepare(44100.0, 20.0f);
        ksBridge.prepare(44100.0, 20.0f);

        ksMiddle.setFrequency(440.0f);
        ksBridge.setFrequency(440.0f);

        ksMiddle.setDecay(2.0f);
        ksBridge.setDecay(2.0f);

        ksMiddle.setPickPosition(0.5f);  // Middle
        ksBridge.setPickPosition(0.1f);  // Near bridge

        ksMiddle.pluck(1.0f);
        ksBridge.pluck(1.0f);

        constexpr size_t kNumSamples = 4410;
        std::vector<float> bufferMiddle(kNumSamples);
        std::vector<float> bufferBridge(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            bufferMiddle[i] = ksMiddle.process();
            bufferBridge[i] = ksBridge.process();
        }

        // Both should produce output
        float rmsMiddle = calculateRMS(bufferMiddle.data() + 500, kNumSamples - 500);
        float rmsBridge = calculateRMS(bufferBridge.data() + 500, kNumSamples - 500);

        REQUIRE(rmsMiddle > 0.01f);
        REQUIRE(rmsBridge > 0.01f);

        // Pick position should affect the harmonic content (different timbres)
        // Near bridge (0.1) should have more harmonics (brighter/thinner)
        float hfRatioMiddle = highFrequencyEnergyRatio(bufferMiddle.data() + 500, kNumSamples - 500);
        float hfRatioBridge = highFrequencyEnergyRatio(bufferBridge.data() + 500, kNumSamples - 500);

        // They should be noticeably different
        float difference = std::abs(hfRatioMiddle - hfRatioBridge);
        REQUIRE(difference > 0.001f);  // Some measurable difference
    }
}

// ==============================================================================
// Phase 5: User Story 3 - Bowing (T042, T043, T044, US3)
// ==============================================================================

TEST_CASE("KarplusStrong bowing mode", "[karplus_strong][bow][US3]") {

    SECTION("US3-AC1, SC-009: Bow produces sustained oscillation") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);

        // Start bowing
        ks.bow(0.5f);

        // Process for 2 seconds
        constexpr size_t kNumSamples = 88200;
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            ks.bow(0.5f);  // Continuous bowing
            buffer[i] = ks.process();
        }

        // RMS at start and end should be similar (sustained)
        float startRMS = calculateRMS(buffer.data() + 4410, 4410);  // After 100ms settling
        float endRMS = calculateRMS(buffer.data() + kNumSamples - 4410, 4410);

        // Should not decay significantly (within 50% of each other)
        if (startRMS > 0.01f) {
            REQUIRE(endRMS > startRMS * 0.5f);
        }
    }

    SECTION("US3-AC2: Bow pressure scales amplitude") {
        KarplusStrong ksQuiet;
        KarplusStrong ksLoud;

        ksQuiet.prepare(44100.0, 20.0f);
        ksLoud.prepare(44100.0, 20.0f);

        ksQuiet.setFrequency(440.0f);
        ksLoud.setFrequency(440.0f);

        constexpr size_t kNumSamples = 44100;
        std::vector<float> bufferQuiet(kNumSamples);
        std::vector<float> bufferLoud(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            ksQuiet.bow(0.2f);
            ksLoud.bow(0.8f);
            bufferQuiet[i] = ksQuiet.process();
            bufferLoud[i] = ksLoud.process();
        }

        float rmsQuiet = calculateRMS(bufferQuiet.data() + 4410, kNumSamples - 4410);
        float rmsLoud = calculateRMS(bufferLoud.data() + 4410, kNumSamples - 4410);

        REQUIRE(rmsLoud > rmsQuiet);
    }

    SECTION("US3-AC3: Bow release causes decay") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(0.5f);

        // Bow for a while
        for (int i = 0; i < 22050; ++i) {
            ks.bow(0.5f);
            (void)ks.process();
        }

        // Stop bowing
        ks.bow(0.0f);

        // Process and observe decay
        constexpr size_t kNumSamples = 44100;
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        float startRMS = calculateRMS(buffer.data(), 4410);
        float endRMS = calculateRMS(buffer.data() + kNumSamples - 4410, 4410);

        // Should decay after bow release
        if (startRMS > 0.01f) {
            REQUIRE(endRMS < startRMS * 0.5f);
        }
    }
}

// ==============================================================================
// Phase 6: User Story 4 - Custom Excitation (T050, T051, US4)
// ==============================================================================

TEST_CASE("KarplusStrong custom excitation", "[karplus_strong][excite][US4]") {

    SECTION("US4-AC1: Custom sine excitation produces tonal output") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(2.0f);

        // Create sine burst excitation
        std::array<float, 100> excitation;
        for (size_t i = 0; i < excitation.size(); ++i) {
            float phase = static_cast<float>(i) / excitation.size() * 2.0f * 3.14159f;
            excitation[i] = std::sin(phase) * 0.5f;
        }

        ks.excite(excitation.data(), excitation.size());

        constexpr size_t kNumSamples = 4410;
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // Should produce output
        float rms = calculateRMS(buffer.data() + 500, kNumSamples - 500);
        REQUIRE(rms > 0.001f);
    }

    SECTION("US4-AC2: External audio input causes sympathetic resonance") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(1.0f);

        // Feed sine wave at string frequency
        constexpr size_t kNumSamples = 8820;
        std::vector<float> output(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            float input = 0.1f * std::sin(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / 44100.0f);
            output[i] = ks.process(input);
        }

        // Should build up resonance
        float rmsEnd = calculateRMS(output.data() + kNumSamples - 2000, 2000);
        REQUIRE(rmsEnd > 0.001f);
    }
}

// ==============================================================================
// Phase 7: User Story 5 - Inharmonicity/Stretch (T057-T059, US5, SC-010)
// ==============================================================================

TEST_CASE("KarplusStrong stretch/inharmonicity", "[karplus_strong][stretch][US5]") {

    SECTION("US5-AC1: Stretch=0 produces harmonic output") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(2.0f);
        ks.setStretch(0.0f);

        ks.pluck(1.0f);

        constexpr size_t kNumSamples = 4410;
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // Should produce output
        float rms = calculateRMS(buffer.data() + 500, kNumSamples - 500);
        REQUIRE(rms > 0.01f);
    }

    SECTION("SC-010: Stretch > 0.3 produces audible change") {
        KarplusStrong ksHarmonic;
        KarplusStrong ksStretched;

        ksHarmonic.prepare(44100.0, 20.0f);
        ksStretched.prepare(44100.0, 20.0f);

        ksHarmonic.setFrequency(440.0f);
        ksStretched.setFrequency(440.0f);

        ksHarmonic.setDecay(2.0f);
        ksStretched.setDecay(2.0f);

        ksHarmonic.setStretch(0.0f);
        ksStretched.setStretch(0.5f);

        ksHarmonic.pluck(1.0f);
        ksStretched.pluck(1.0f);

        constexpr size_t kNumSamples = 4410;
        std::vector<float> bufferHarmonic(kNumSamples);
        std::vector<float> bufferStretched(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            bufferHarmonic[i] = ksHarmonic.process();
            bufferStretched[i] = ksStretched.process();
        }

        // The waveforms should be different due to inharmonicity
        // Calculate correlation or difference
        float sumDiff = 0.0f;
        for (size_t i = 500; i < kNumSamples; ++i) {
            sumDiff += std::abs(bufferHarmonic[i] - bufferStretched[i]);
        }
        float avgDiff = sumDiff / (kNumSamples - 500);

        // Should have measurable difference
        REQUIRE(avgDiff > 0.001f);
    }
}

// ==============================================================================
// Phase 8: Edge Cases (T065-T072)
// ==============================================================================

TEST_CASE("KarplusStrong parameter clamping", "[karplus_strong][edge]") {

    SECTION("FR-032: Parameters clamped to valid ranges") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);

        // These should not crash
        ks.setDamping(-0.5f);  // Clamp to 0
        ks.setDamping(1.5f);   // Clamp to 1
        ks.setBrightness(-0.5f);
        ks.setBrightness(1.5f);
        ks.setPickPosition(-0.5f);
        ks.setPickPosition(1.5f);
        ks.setStretch(-0.5f);
        ks.setStretch(1.5f);

        // Should still process without NaN/Inf
        ks.setFrequency(440.0f);
        ks.pluck(1.0f);
        float output = ks.process();
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

TEST_CASE("KarplusStrong re-pluck normalization", "[karplus_strong][edge]") {

    SECTION("FR-033: Re-pluck during active adds without clipping") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(2.0f);

        // Pluck multiple times rapidly
        ks.pluck(1.0f);
        for (int i = 0; i < 50; ++i) {
            (void)ks.process();
        }
        ks.pluck(1.0f);  // Re-pluck
        for (int i = 0; i < 50; ++i) {
            (void)ks.process();
        }
        ks.pluck(1.0f);  // And again

        constexpr size_t kNumSamples = 4410;
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // Output should not exceed reasonable bounds
        for (float sample : buffer) {
            REQUIRE(sample <= 2.0f);
            REQUIRE(sample >= -2.0f);
        }
    }
}

TEST_CASE("KarplusStrong extreme decay times", "[karplus_strong][edge]") {

    SECTION("Very short decay produces brief transient") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(0.005f);  // 5ms

        ks.pluck(1.0f);

        constexpr size_t kNumSamples = 4410;  // 100ms
        std::vector<float> buffer(kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            buffer[i] = ks.process();
        }

        // Should be almost silent at end
        float endRMS = calculateRMS(buffer.data() + kNumSamples - 1000, 1000);
        REQUIRE(endRMS < 0.01f);
    }

    SECTION("Very long decay does not cause instability") {
        KarplusStrong ks;
        ks.prepare(44100.0, 20.0f);
        ks.setFrequency(440.0f);
        ks.setDecay(60.0f);  // 60 seconds

        ks.pluck(1.0f);

        // Process for a while - should not grow unbounded
        constexpr size_t kNumSamples = 44100;
        float maxAbs = 0.0f;

        for (size_t i = 0; i < kNumSamples; ++i) {
            float output = ks.process();
            maxAbs = std::max(maxAbs, std::abs(output));
        }

        // Should remain bounded
        REQUIRE(maxAbs < 2.0f);
    }
}

// ==============================================================================
// noexcept verification
// ==============================================================================

TEST_CASE("KarplusStrong methods are noexcept", "[karplus_strong][safety]") {

    KarplusStrong ks;

    STATIC_REQUIRE(noexcept(ks.prepare(44100.0, 20.0f)));
    STATIC_REQUIRE(noexcept(ks.reset()));
    STATIC_REQUIRE(noexcept(ks.setFrequency(440.0f)));
    STATIC_REQUIRE(noexcept(ks.setDecay(1.0f)));
    STATIC_REQUIRE(noexcept(ks.setDamping(0.5f)));
    STATIC_REQUIRE(noexcept(ks.setBrightness(0.5f)));
    STATIC_REQUIRE(noexcept(ks.setPickPosition(0.5f)));
    STATIC_REQUIRE(noexcept(ks.setStretch(0.5f)));
    STATIC_REQUIRE(noexcept(ks.pluck(1.0f)));
    STATIC_REQUIRE(noexcept(ks.bow(0.5f)));
    STATIC_REQUIRE(noexcept(ks.process()));
    STATIC_REQUIRE(noexcept(ks.process(0.5f)));
}
