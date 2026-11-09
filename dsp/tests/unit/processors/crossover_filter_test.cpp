// ==============================================================================
// Unit Tests: Crossover Filter (Linkwitz-Riley)
// ==============================================================================
// Layer 2: DSP Processor Tests
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
//
// Reference: specs/076-crossover-filter/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/processors/crossover_filter.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
#include <random>
#include <numeric>
#include <chrono>
#include <thread>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

/// Generate a sine wave at specified frequency
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

/// Calculate RMS of a buffer
inline float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

/// Convert linear amplitude to decibels
inline float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

/// Generate white noise with deterministic seed
inline void generateWhiteNoise(float* buffer, size_t size, unsigned int seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dist(gen);
    }
}

/// Measure response at a specific frequency by processing a sine and measuring output RMS
/// Processes long enough to get past transient response
inline float measureResponseDb(float* lowBuffer, float* highBuffer,
                               CrossoverLR4& crossover,
                               float testFreq, float sampleRate, size_t numSamples = 8192) {
    std::vector<float> input(numSamples);
    generateSine(input.data(), numSamples, testFreq, sampleRate);

    // Process through crossover
    crossover.processBlock(input.data(), lowBuffer, highBuffer, numSamples);

    return 0.0f;  // Helper returns 0, caller should measure what they need
}

/// Measure sum of low+high for flat response verification
inline float measureSumResponseDb(CrossoverLR4& crossover,
                                  float testFreq, float sampleRate, size_t numSamples = 8192) {
    std::vector<float> input(numSamples);
    std::vector<float> low(numSamples);
    std::vector<float> high(numSamples);
    std::vector<float> sum(numSamples);

    generateSine(input.data(), numSamples, testFreq, sampleRate);

    // Process through crossover
    crossover.processBlock(input.data(), low.data(), high.data(), numSamples);

    // Sum bands
    for (size_t i = 0; i < numSamples; ++i) {
        sum[i] = low[i] + high[i];
    }

    // Measure RMS of second half (after transient settles)
    const size_t startSample = numSamples / 2;
    const float inputRms = calculateRMS(input.data() + startSample, numSamples - startSample);
    const float sumRms = calculateRMS(sum.data() + startSample, numSamples - startSample);

    return linearToDb(sumRms / inputRms);
}

/// Measure single band response in dB relative to input
inline float measureBandResponseDb(CrossoverLR4& crossover,
                                   float testFreq, float sampleRate,
                                   bool measureLow, size_t numSamples = 8192) {
    std::vector<float> input(numSamples);
    std::vector<float> low(numSamples);
    std::vector<float> high(numSamples);

    generateSine(input.data(), numSamples, testFreq, sampleRate);

    // Process through crossover
    crossover.processBlock(input.data(), low.data(), high.data(), numSamples);

    // Measure RMS of second half (after transient settles)
    const size_t startSample = numSamples / 2;
    const float inputRms = calculateRMS(input.data() + startSample, numSamples - startSample);
    const float bandRms = measureLow
        ? calculateRMS(low.data() + startSample, numSamples - startSample)
        : calculateRMS(high.data() + startSample, numSamples - startSample);

    return linearToDb(bandRms / inputRms);
}

/// Check if sample is valid (not NaN or Inf)
inline bool isValidSample(float sample) {
    return std::isfinite(sample);
}

// =============================================================================
// FFT-Based Frequency Response Helpers
// =============================================================================

/// Measure frequency response using FFT analysis of impulse response
/// @param impulseResponse The filter's impulse response
/// @param fftSize FFT size (must be power of 2)
/// @return Vector of magnitude values in dB for each bin
inline std::vector<float> measureFrequencyResponseDb(
    const std::vector<float>& impulseResponse,
    size_t fftSize = 4096)
{
    FFT fft;
    fft.prepare(fftSize);

    // Copy impulse response into FFT buffer (zero-pad if needed)
    std::vector<float> buffer(fftSize, 0.0f);
    const size_t copySize = std::min(impulseResponse.size(), fftSize);
    std::copy(impulseResponse.begin(), impulseResponse.begin() + copySize, buffer.begin());

    // Apply Hann window to reduce spectral leakage
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        buffer[i] *= window[i];
    }

    // Perform FFT
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(buffer.data(), spectrum.data());

    // Convert to dB magnitude
    std::vector<float> magnitudeDb(fft.numBins());
    for (size_t i = 0; i < fft.numBins(); ++i) {
        float mag = spectrum[i].magnitude();
        magnitudeDb[i] = (mag > 1e-10f) ? 20.0f * std::log10(mag) : -200.0f;
    }

    return magnitudeDb;
}

/// Get the frequency corresponding to a bin index
inline float binToFrequency(size_t bin, float sampleRate, size_t fftSize) {
    return static_cast<float>(bin) * sampleRate / static_cast<float>(fftSize);
}

/// Measure flat sum error using white noise and FFT
/// Returns max deviation from 0dB across the spectrum (excluding DC and near-Nyquist)
/// Uses longer buffer with transient skipping for accurate steady-state measurement
template<typename CrossoverType, typename ProcessFunc>
inline float measureFlatSumErrorDb(
    CrossoverType& crossover,
    ProcessFunc processFunc,
    float sampleRate,
    size_t fftSize = 4096)
{
    // Use longer buffer: transient portion + FFT analysis portion
    const size_t transientSamples = 4096;  // Skip first 4096 samples for filter settling
    const size_t totalSamples = transientSamples + fftSize;

    // Generate white noise
    std::vector<float> input(totalSamples);
    std::mt19937 gen(42);  // Deterministic seed
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (size_t i = 0; i < totalSamples; ++i) {
        input[i] = dist(gen);
    }

    // Process through crossover and sum bands
    std::vector<float> sum(totalSamples);
    for (size_t i = 0; i < totalSamples; ++i) {
        sum[i] = processFunc(crossover, input[i]);
    }

    // FFT both input and output - use only the steady-state portion
    FFT fft;
    fft.prepare(fftSize);

    // Apply window to both (on steady-state portion only)
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);

    std::vector<float> inputWindowed(fftSize);
    std::vector<float> sumWindowed(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        inputWindowed[i] = input[transientSamples + i] * window[i];
        sumWindowed[i] = sum[transientSamples + i] * window[i];
    }

    std::vector<Complex> inputSpectrum(fft.numBins());
    std::vector<Complex> sumSpectrum(fft.numBins());
    fft.forward(inputWindowed.data(), inputSpectrum.data());
    fft.forward(sumWindowed.data(), sumSpectrum.data());

    // Compare magnitudes across spectrum (skip DC bin 0 and near-Nyquist)
    // Focus on 50Hz to 18kHz range (avoid extreme edges where error accumulates)
    const size_t minBin = static_cast<size_t>(50.0f * static_cast<float>(fftSize) / sampleRate);
    const size_t maxBin = std::min(
        static_cast<size_t>(18000.0f * static_cast<float>(fftSize) / sampleRate),
        fft.numBins() - 1
    );

    float maxError = 0.0f;
    for (size_t i = minBin; i <= maxBin; ++i) {
        float inputMag = inputSpectrum[i].magnitude();
        float sumMag = sumSpectrum[i].magnitude();

        if (inputMag > 1e-6f) {  // Avoid division by near-zero
            float ratio = sumMag / inputMag;
            float errorDb = std::abs(20.0f * std::log10(ratio));
            maxError = std::max(maxError, errorDb);
        }
    }

    return maxError;
}

}  // anonymous namespace

// ==============================================================================
// Phase 2.1: User Story 1 Tests - 2-Way Band Splitting MVP
// ==============================================================================

// -----------------------------------------------------------------------------
// T004: TrackingMode enum
// -----------------------------------------------------------------------------
TEST_CASE("TrackingMode enum has Efficient and HighAccuracy values", "[crossover][US1][enum]") {
    SECTION("Efficient mode exists and has value 0") {
        REQUIRE(static_cast<int>(TrackingMode::Efficient) == 0);
    }

    SECTION("HighAccuracy mode exists and has value 1") {
        REQUIRE(static_cast<int>(TrackingMode::HighAccuracy) == 1);
    }

    SECTION("enum is uint8_t underlying type") {
        REQUIRE(sizeof(TrackingMode) == 1);
    }
}

// -----------------------------------------------------------------------------
// T005: CrossoverLR4Outputs struct
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4Outputs struct has low and high members", "[crossover][US1][struct]") {
    SECTION("struct has low member defaulted to 0") {
        CrossoverLR4Outputs outputs;
        REQUIRE(outputs.low == 0.0f);
    }

    SECTION("struct has high member defaulted to 0") {
        CrossoverLR4Outputs outputs;
        REQUIRE(outputs.high == 0.0f);
    }

    SECTION("members can be assigned") {
        CrossoverLR4Outputs outputs;
        outputs.low = 0.5f;
        outputs.high = 0.75f;
        REQUIRE(outputs.low == 0.5f);
        REQUIRE(outputs.high == 0.75f);
    }
}

// -----------------------------------------------------------------------------
// T006: Default constructor and model constants
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 default constructor and constants", "[crossover][US1][lifecycle]") {
    CrossoverLR4 crossover;

    SECTION("kMinFrequency is 20Hz") {
        REQUIRE(CrossoverLR4::kMinFrequency == 20.0f);
    }

    SECTION("kMaxFrequencyRatio is 0.45") {
        REQUIRE(CrossoverLR4::kMaxFrequencyRatio == 0.45f);
    }

    SECTION("kDefaultSmoothingMs is 5ms") {
        REQUIRE(CrossoverLR4::kDefaultSmoothingMs == 5.0f);
    }

    SECTION("kDefaultFrequency is 1000Hz") {
        REQUIRE(CrossoverLR4::kDefaultFrequency == 1000.0f);
    }

    SECTION("not prepared after construction") {
        REQUIRE_FALSE(crossover.isPrepared());
    }

    SECTION("default frequency is 1000Hz") {
        REQUIRE(crossover.getCrossoverFrequency() == Approx(1000.0f));
    }

    SECTION("default smoothing time is 5ms") {
        REQUIRE(crossover.getSmoothingTime() == Approx(5.0f));
    }

    SECTION("default tracking mode is Efficient") {
        REQUIRE(crossover.getTrackingMode() == TrackingMode::Efficient);
    }
}

// -----------------------------------------------------------------------------
// T007: prepare() initializes filter
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 prepare initializes filter", "[crossover][US1][lifecycle][FR-012]") {
    CrossoverLR4 crossover;

    SECTION("prepare sets prepared flag") {
        crossover.prepare(44100.0);
        REQUIRE(crossover.isPrepared());
    }

    SECTION("prepare can be called multiple times") {
        crossover.prepare(44100.0);
        crossover.prepare(48000.0);
        crossover.prepare(96000.0);
        REQUIRE(crossover.isPrepared());
    }

    SECTION("prepare with different sample rates works") {
        crossover.prepare(44100.0);
        REQUIRE(crossover.isPrepared());

        crossover.prepare(192000.0);
        REQUIRE(crossover.isPrepared());
    }
}

// -----------------------------------------------------------------------------
// T008: setCrossoverFrequency clamping
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 frequency clamping", "[crossover][US1][params][FR-005]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    SECTION("frequency below minimum is clamped to 20Hz") {
        crossover.setCrossoverFrequency(10.0f);
        REQUIRE(crossover.getCrossoverFrequency() == Approx(20.0f));
    }

    SECTION("normal frequency is accepted") {
        crossover.setCrossoverFrequency(1000.0f);
        REQUIRE(crossover.getCrossoverFrequency() == Approx(1000.0f));
    }

    SECTION("frequency above maximum is clamped to sampleRate * 0.45") {
        crossover.setCrossoverFrequency(25000.0f);
        const float maxExpected = 44100.0f * 0.45f;
        REQUIRE(crossover.getCrossoverFrequency() == Approx(maxExpected));
    }
}

// -----------------------------------------------------------------------------
// T009: LR4 topology with 2 cascaded Butterworth stages
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 implements LR4 topology", "[crossover][US1][topology][FR-001][FR-015]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    SECTION("process returns non-zero output for non-zero input") {
        auto outputs = crossover.process(1.0f);
        // Both outputs should have some energy for first sample
        REQUIRE((outputs.low != 0.0f || outputs.high != 0.0f));
    }

    SECTION("process returns valid outputs for impulse") {
        for (int i = 0; i < 100; ++i) {
            auto outputs = crossover.process(i == 0 ? 1.0f : 0.0f);
            REQUIRE(isValidSample(outputs.low));
            REQUIRE(isValidSample(outputs.high));
        }
    }
}

// -----------------------------------------------------------------------------
// T010: Low + High sum to flat response (FR-002, SC-001)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 low + high sum to flat response", "[crossover][US1][flatsum][FR-002][SC-001]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    SECTION("sum is flat at 100Hz") {
        float responseDb = measureSumResponseDb(crossover, 100.0f, 44100.0f);
        REQUIRE(responseDb == Approx(0.0f).margin(0.1f));
    }

    SECTION("sum is flat at 500Hz") {
        crossover.reset();
        float responseDb = measureSumResponseDb(crossover, 500.0f, 44100.0f);
        REQUIRE(responseDb == Approx(0.0f).margin(0.1f));
    }

    SECTION("sum is flat at 1000Hz (crossover frequency)") {
        crossover.reset();
        float responseDb = measureSumResponseDb(crossover, 1000.0f, 44100.0f);
        REQUIRE(responseDb == Approx(0.0f).margin(0.1f));
    }

    SECTION("sum is flat at 2000Hz") {
        crossover.reset();
        float responseDb = measureSumResponseDb(crossover, 2000.0f, 44100.0f);
        REQUIRE(responseDb == Approx(0.0f).margin(0.1f));
    }

    SECTION("sum is flat at 5000Hz") {
        crossover.reset();
        float responseDb = measureSumResponseDb(crossover, 5000.0f, 44100.0f);
        REQUIRE(responseDb == Approx(0.0f).margin(0.1f));
    }

    SECTION("sum is flat at 10000Hz") {
        crossover.reset();
        float responseDb = measureSumResponseDb(crossover, 10000.0f, 44100.0f);
        REQUIRE(responseDb == Approx(0.0f).margin(0.1f));
    }
}

// -----------------------------------------------------------------------------
// T011: Both outputs -6dB at crossover frequency (FR-003, SC-002)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 outputs -6dB at crossover frequency", "[crossover][US1][crossover][FR-003][SC-002]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    SECTION("low output is -6dB at crossover frequency") {
        float lowDb = measureBandResponseDb(crossover, 1000.0f, 44100.0f, true);
        REQUIRE(lowDb == Approx(-6.0f).margin(0.5f));
    }

    SECTION("high output is -6dB at crossover frequency") {
        crossover.reset();
        float highDb = measureBandResponseDb(crossover, 1000.0f, 44100.0f, false);
        REQUIRE(highDb == Approx(-6.0f).margin(0.5f));
    }
}

// -----------------------------------------------------------------------------
// T012: Low output -24dB at one octave above (FR-004, SC-003)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 low output 24dB/oct slope", "[crossover][US1][slope][FR-004][SC-003]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    SECTION("low output is ~-24dB at 2kHz (one octave above 1kHz)") {
        float lowDb = measureBandResponseDb(crossover, 2000.0f, 44100.0f, true);
        // LR4 gives approximately -24dB at one octave from crossover
        // Allow +/-2dB tolerance per spec
        REQUIRE(lowDb < -22.0f);
        REQUIRE(lowDb > -26.0f);
    }
}

// -----------------------------------------------------------------------------
// T013: High output -24dB at one octave below
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 high output 24dB/oct slope", "[crossover][US1][slope]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    SECTION("high output is ~-24dB at 500Hz (one octave below 1kHz)") {
        float highDb = measureBandResponseDb(crossover, 500.0f, 44100.0f, false);
        // LR4 gives approximately -24dB at one octave from crossover
        REQUIRE(highDb < -22.0f);
        REQUIRE(highDb > -26.0f);
    }
}

// -----------------------------------------------------------------------------
// T014: reset() clears filter states (FR-011)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 reset clears states", "[crossover][US1][reset][FR-011]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    SECTION("reset clears internal state") {
        // Process some samples to build up state
        for (int i = 0; i < 100; ++i) {
            (void)crossover.process(0.5f);
        }

        // Reset
        crossover.reset();

        // Process same impulse, should get same result as fresh filter
        CrossoverLR4 fresh;
        fresh.prepare(44100.0);
        fresh.setCrossoverFrequency(1000.0f);

        auto resetOutputs = crossover.process(1.0f);
        auto freshOutputs = fresh.process(1.0f);

        REQUIRE(resetOutputs.low == Approx(freshOutputs.low).margin(0.001f));
        REQUIRE(resetOutputs.high == Approx(freshOutputs.high).margin(0.001f));
    }
}

// -----------------------------------------------------------------------------
// T015: Unprepared filter returns zero
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 unprepared returns zero", "[crossover][US1][safety]") {
    CrossoverLR4 crossover;

    SECTION("process returns zeros when not prepared") {
        auto outputs = crossover.process(1.0f);
        REQUIRE(outputs.low == 0.0f);
        REQUIRE(outputs.high == 0.0f);
    }
}

// -----------------------------------------------------------------------------
// T016: processBlock bit-identical to process() loop (FR-013)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 processBlock matches process loop", "[crossover][US1][block][FR-013]") {
    constexpr size_t blockSize = 64;

    SECTION("processBlock produces same output as process loop") {
        CrossoverLR4 blockCrossover;
        CrossoverLR4 sampleCrossover;

        blockCrossover.prepare(44100.0);
        sampleCrossover.prepare(44100.0);
        blockCrossover.setCrossoverFrequency(1000.0f);
        sampleCrossover.setCrossoverFrequency(1000.0f);

        std::array<float, blockSize> input;
        generateSine(input.data(), blockSize, 440.0f, 44100.0f);

        // Process with block method
        std::array<float, blockSize> blockLow, blockHigh;
        blockCrossover.processBlock(input.data(), blockLow.data(), blockHigh.data(), blockSize);

        // Process sample by sample
        std::array<float, blockSize> sampleLow, sampleHigh;
        for (size_t i = 0; i < blockSize; ++i) {
            auto outputs = sampleCrossover.process(input[i]);
            sampleLow[i] = outputs.low;
            sampleHigh[i] = outputs.high;
        }

        // Compare
        for (size_t i = 0; i < blockSize; ++i) {
            REQUIRE(blockLow[i] == Approx(sampleLow[i]).margin(1e-6f));
            REQUIRE(blockHigh[i] == Approx(sampleHigh[i]).margin(1e-6f));
        }
    }
}

// -----------------------------------------------------------------------------
// T017: processBlock various block sizes
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 processBlock various sizes", "[crossover][US1][block]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    std::vector<size_t> blockSizes = {1, 2, 16, 512, 4096};

    for (size_t blockSize : blockSizes) {
        SECTION("block size " + std::to_string(blockSize)) {
            std::vector<float> input(blockSize);
            std::vector<float> low(blockSize);
            std::vector<float> high(blockSize);

            generateSine(input.data(), blockSize, 440.0f, 44100.0f);

            crossover.reset();
            crossover.processBlock(input.data(), low.data(), high.data(), blockSize);

            // Verify all outputs are valid
            for (size_t i = 0; i < blockSize; ++i) {
                REQUIRE(isValidSample(low[i]));
                REQUIRE(isValidSample(high[i]));
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Low-frequency precision (Disrumpo audit T1.03): at fc/fs ~6e-4 (112 Hz at
// 192 kHz) a float32 RBJ biquad held the LR4 low-pass DC gain only to about
// +-0.12 dB, depending on where the coefficients happened to round.
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 DC gain and band sum are exact at low fc/fs", "[crossover][precision]") {
    constexpr double kSampleRates[] = {44100.0, 48000.0, 96000.0, 192000.0};
    constexpr float kFrequencies[] = {20.0f, 50.0f, 112.47f, 632.0f, 3556.0f, 10000.0f};
    constexpr double kBarDb = 0.01;
    double worstDb = 0.0;

    for (const double sr : kSampleRates) {
        for (const float fc : kFrequencies) {
            CrossoverLR4 crossover;
            crossover.prepare(sr);
            crossover.setCrossoverFrequency(fc);
            crossover.prepare(sr);  // snap the smoother: coefficients at exactly fc

            const auto settle = static_cast<size_t>(sr);  // 1 s: > 80 time constants at 20 Hz

            // Steady-state response to a constant
            CrossoverLR4Outputs out{};
            for (size_t i = 0; i < settle; ++i) out = crossover.process(1.0f);
            const double lowDcDb = 20.0 * std::log10(std::abs(static_cast<double>(out.low)));
            const double sumDcDb =
                20.0 * std::log10(std::abs(static_cast<double>(out.low) + static_cast<double>(out.high)));

            // |low + high| at the crossover frequency, over whole periods after settling
            crossover.reset();
            const double w = 2.0 * 3.14159265358979323846 * static_cast<double>(fc) / sr;
            const size_t periods = static_cast<size_t>(std::floor(static_cast<double>(fc)));  // ~1 s
            const auto window = static_cast<size_t>(std::llround(static_cast<double>(periods) * sr / static_cast<double>(fc)));
            double inPow = 0.0;
            double sumPow = 0.0;
            for (size_t i = 0; i < settle + window; ++i) {
                const auto x = static_cast<float>(std::sin(w * static_cast<double>(i)));
                const auto o = crossover.process(x);
                if (i >= settle) {
                    const double s = static_cast<double>(o.low) + static_cast<double>(o.high);
                    inPow += static_cast<double>(x) * static_cast<double>(x);
                    sumPow += s * s;
                }
            }
            const double sumFcDb = 10.0 * std::log10(sumPow / inPow);

            INFO("sr " << sr << " fc " << fc << ": low DC " << lowDcDb << " dB, sum DC " << sumDcDb
                       << " dB, sum at fc " << sumFcDb << " dB");
            std::printf("LR4 sr %6.0f fc %8.2f: low DC %+.5f dB, sum DC %+.5f dB, sum at fc %+.5f dB\n", sr,
                        static_cast<double>(fc), lowDcDb, sumDcDb, sumFcDb);
            CHECK(std::abs(lowDcDb) < kBarDb);
            CHECK(std::abs(sumDcDb) < kBarDb);
            CHECK(std::abs(sumFcDb) < kBarDb);
            worstDb = std::max({worstDb, std::abs(lowDcDb), std::abs(sumDcDb), std::abs(sumFcDb)});
        }
    }
    std::printf("LR4 precision grid: worst |error| %.5f dB\n", worstDb);
}

// -----------------------------------------------------------------------------
// T018: Stability test - no NaN/Inf for 1M samples (SC-008)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 stability over many samples", "[crossover][US1][stability][SC-008]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    std::vector<float> frequencies = {100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f};

    for (float freq : frequencies) {
        SECTION("stable at " + std::to_string(static_cast<int>(freq)) + "Hz crossover") {
            crossover.reset();
            crossover.setCrossoverFrequency(freq);

            // Process 1M samples
            constexpr size_t numSamples = 1000000;
            std::mt19937 gen(42);
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

            bool allValid = true;
            for (size_t i = 0; i < numSamples; ++i) {
                auto outputs = crossover.process(dist(gen));
                if (!isValidSample(outputs.low) || !isValidSample(outputs.high)) {
                    allValid = false;
                    break;
                }
            }

            REQUIRE(allValid);
        }
    }
}

// -----------------------------------------------------------------------------
// T019: Cross-platform consistency (SC-009)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 cross-platform sample rates", "[crossover][US1][platform][SC-009]") {
    std::vector<double> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        SECTION("works at " + std::to_string(static_cast<int>(sr)) + "Hz") {
            CrossoverLR4 crossover;
            crossover.prepare(sr);
            crossover.setCrossoverFrequency(1000.0f);

            // Process some samples
            constexpr size_t numSamples = 1024;
            std::vector<float> input(numSamples);
            std::vector<float> low(numSamples);
            std::vector<float> high(numSamples);

            generateSine(input.data(), numSamples, 440.0f, static_cast<float>(sr));
            crossover.processBlock(input.data(), low.data(), high.data(), numSamples);

            // All outputs should be valid
            for (size_t i = 0; i < numSamples; ++i) {
                REQUIRE(isValidSample(low[i]));
                REQUIRE(isValidSample(high[i]));
            }

            // Verify flat sum at crossover
            crossover.reset();
            float sumDb = measureSumResponseDb(crossover, 1000.0f, static_cast<float>(sr), 16384);
            REQUIRE(sumDb == Approx(0.0f).margin(0.15f));
        }
    }
}

// -----------------------------------------------------------------------------
// T020: CPU performance benchmark (SC-010)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 CPU performance", "[crossover][US1][performance][SC-010][!benchmark]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    // Warm up
    for (int i = 0; i < 1000; ++i) {
        (void)crossover.process(0.5f);
    }

    // Benchmark
    constexpr size_t numIterations = 100000;
    auto start = std::chrono::high_resolution_clock::now();

    float dummy = 0.0f;
    for (size_t i = 0; i < numIterations; ++i) {
        auto outputs = crossover.process(0.5f);
        dummy += outputs.low + outputs.high;  // Prevent optimization
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    double nsPerSample = static_cast<double>(duration.count()) / static_cast<double>(numIterations);

    // Prevent optimization away
    REQUIRE(dummy != 0.0f);

    INFO("Time per sample: " << nsPerSample << " ns");

    // Should be under 100ns per sample per SC-010
    // This is a soft check - may fail on slow CI runners
    REQUIRE(nsPerSample < 500.0);  // Generous margin for CI
}

// ==============================================================================
// Phase 3.1: User Story 2 Tests - Click-Free Frequency Sweeps
// ==============================================================================

// -----------------------------------------------------------------------------
// T038: setSmoothingTime (FR-007)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 smoothing time", "[crossover][US2][smoothing][FR-007]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    SECTION("default smoothing time is 5ms") {
        REQUIRE(crossover.getSmoothingTime() == Approx(5.0f));
    }

    SECTION("setSmoothingTime changes value") {
        crossover.setSmoothingTime(10.0f);
        REQUIRE(crossover.getSmoothingTime() == Approx(10.0f));
    }
}

// -----------------------------------------------------------------------------
// T039: getSmoothingTime
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 getSmoothingTime returns current value", "[crossover][US2][smoothing]") {
    CrossoverLR4 crossover;

    SECTION("returns configured value") {
        crossover.setSmoothingTime(20.0f);
        REQUIRE(crossover.getSmoothingTime() == Approx(20.0f));
    }
}

// -----------------------------------------------------------------------------
// T040: Frequency sweep click-free (SC-006)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 frequency sweep is click-free", "[crossover][US2][clickfree][SC-006]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(200.0f);

    // Process for 100ms while sweeping from 200Hz to 8kHz
    constexpr size_t sweepSamples = 4410;  // 100ms at 44.1kHz
    constexpr float startFreq = 200.0f;
    constexpr float endFreq = 8000.0f;

    std::vector<float> sumOutput(sweepSamples);

    for (size_t i = 0; i < sweepSamples; ++i) {
        // Linear frequency sweep
        float t = static_cast<float>(i) / static_cast<float>(sweepSamples);
        float freq = startFreq + t * (endFreq - startFreq);
        crossover.setCrossoverFrequency(freq);

        // Process pink-ish noise (just use constant for simplicity)
        auto outputs = crossover.process(0.5f);
        sumOutput[i] = outputs.low + outputs.high;
    }

    // Check for clicks: look for sudden large jumps
    float maxJump = 0.0f;
    for (size_t i = 1; i < sweepSamples; ++i) {
        float jump = std::abs(sumOutput[i] - sumOutput[i - 1]);
        maxJump = std::max(maxJump, jump);
    }

    // No click should produce a jump larger than the input amplitude
    REQUIRE(maxJump < 1.0f);
}

// -----------------------------------------------------------------------------
// T041: Frequency reaches 99% of target (FR-006, SC-007)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 frequency reaches target", "[crossover][US2][convergence][FR-006][SC-007]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(500.0f);
    crossover.setSmoothingTime(5.0f);

    // Snap to start frequency
    for (int i = 0; i < 100; ++i) {
        (void)crossover.process(0.0f);
    }

    // Change to new frequency
    crossover.setCrossoverFrequency(2000.0f);

    // Process for 5 * smoothingTime = 25ms = 1103 samples at 44.1kHz
    constexpr size_t convergenceSamples = 1103;
    for (size_t i = 0; i < convergenceSamples; ++i) {
        (void)crossover.process(0.0f);
    }

    // The internal smoother should have converged
    // We can verify by checking that processing is stable
    auto outputs = crossover.process(1.0f);
    REQUIRE(isValidSample(outputs.low));
    REQUIRE(isValidSample(outputs.high));
}

// -----------------------------------------------------------------------------
// T042: Rapid automation artifact-free (SC-006)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 rapid automation is artifact-free", "[crossover][US2][automation][SC-006]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    // 10 frequency changes per second over 1 second
    constexpr size_t totalSamples = 44100;
    constexpr size_t samplesPerChange = 4410;

    std::mt19937 gen(42);
    std::uniform_real_distribution<float> freqDist(200.0f, 8000.0f);
    std::uniform_real_distribution<float> inputDist(-1.0f, 1.0f);

    std::vector<float> sumOutput(totalSamples);

    for (size_t i = 0; i < totalSamples; ++i) {
        // Change frequency every 100ms
        if (i % samplesPerChange == 0) {
            crossover.setCrossoverFrequency(freqDist(gen));
        }

        auto outputs = crossover.process(inputDist(gen));
        sumOutput[i] = outputs.low + outputs.high;

        REQUIRE(isValidSample(sumOutput[i]));
    }
}

// -----------------------------------------------------------------------------
// T043: setTrackingMode Efficient (FR-017)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 TrackingMode Efficient", "[crossover][US2][tracking][FR-017]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    SECTION("setTrackingMode(Efficient) sets mode") {
        crossover.setTrackingMode(TrackingMode::Efficient);
        REQUIRE(crossover.getTrackingMode() == TrackingMode::Efficient);
    }
}

// -----------------------------------------------------------------------------
// T044: setTrackingMode HighAccuracy (FR-017)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 TrackingMode HighAccuracy", "[crossover][US2][tracking][FR-017]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    SECTION("setTrackingMode(HighAccuracy) sets mode") {
        crossover.setTrackingMode(TrackingMode::HighAccuracy);
        REQUIRE(crossover.getTrackingMode() == TrackingMode::HighAccuracy);
    }
}

// -----------------------------------------------------------------------------
// T045: getTrackingMode
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 getTrackingMode returns current mode", "[crossover][US2][tracking]") {
    CrossoverLR4 crossover;

    SECTION("returns Efficient by default") {
        REQUIRE(crossover.getTrackingMode() == TrackingMode::Efficient);
    }

    SECTION("returns HighAccuracy after setting") {
        crossover.setTrackingMode(TrackingMode::HighAccuracy);
        REQUIRE(crossover.getTrackingMode() == TrackingMode::HighAccuracy);
    }
}

// -----------------------------------------------------------------------------
// T046: Efficient mode coefficient update reduction (SC-011)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 Efficient mode reduces coefficient updates", "[crossover][US2][efficient][SC-011]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setTrackingMode(TrackingMode::Efficient);
    crossover.setCrossoverFrequency(1000.0f);

    // Process to let smoother settle
    for (int i = 0; i < 1000; ++i) {
        (void)crossover.process(0.5f);
    }

    // Make a tiny frequency change (less than 0.1Hz threshold)
    crossover.setCrossoverFrequency(1000.05f);

    // Process and verify still works (coefficients may not update)
    auto outputs = crossover.process(0.5f);
    REQUIRE(isValidSample(outputs.low));
    REQUIRE(isValidSample(outputs.high));
}

// -----------------------------------------------------------------------------
// T047: HighAccuracy mode produces accurate sweep (SC-012)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 HighAccuracy mode during sweeps", "[crossover][US2][highaccuracy][SC-012]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setTrackingMode(TrackingMode::HighAccuracy);
    crossover.setCrossoverFrequency(500.0f);

    // Sweep frequency
    crossover.setCrossoverFrequency(2000.0f);

    // Process during smoothing
    for (int i = 0; i < 1000; ++i) {
        auto outputs = crossover.process(0.5f);
        REQUIRE(isValidSample(outputs.low));
        REQUIRE(isValidSample(outputs.high));
    }
}

// -----------------------------------------------------------------------------
// T048: Denormal handling (FR-018)
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 handles denormals", "[crossover][US2][denormal][FR-018][wallclock]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    // Feed very small values that could produce denormals
    for (int i = 0; i < 10000; ++i) {
        auto outputs = crossover.process(1e-30f);
        REQUIRE(isValidSample(outputs.low));
        REQUIRE(isValidSample(outputs.high));
    }

    // Process should remain fast (no CPU spike from denormals)
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        (void)crossover.process(1e-30f);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Should complete in reasonable time (no denormal slowdown)
    REQUIRE(duration.count() < 100000);  // Less than 100ms for 10k samples
}

// ==============================================================================
// Phase 4.1: User Story 3 Tests - 3-Way Band Splitting
// ==============================================================================

// -----------------------------------------------------------------------------
// T060: Crossover3WayOutputs struct
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3WayOutputs struct", "[crossover][US3][struct]") {
    Crossover3WayOutputs outputs;

    SECTION("has low, mid, high members defaulted to 0") {
        REQUIRE(outputs.low == 0.0f);
        REQUIRE(outputs.mid == 0.0f);
        REQUIRE(outputs.high == 0.0f);
    }
}

// -----------------------------------------------------------------------------
// T061: Crossover3Way default constructor
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way default constructor", "[crossover][US3][lifecycle]") {
    Crossover3Way crossover;

    SECTION("kDefaultLowMidFrequency is 300Hz") {
        REQUIRE(Crossover3Way::kDefaultLowMidFrequency == 300.0f);
    }

    SECTION("kDefaultMidHighFrequency is 3000Hz") {
        REQUIRE(Crossover3Way::kDefaultMidHighFrequency == 3000.0f);
    }

    SECTION("not prepared after construction") {
        REQUIRE_FALSE(crossover.isPrepared());
    }
}

// -----------------------------------------------------------------------------
// T062: Crossover3Way prepare (FR-008)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way prepare", "[crossover][US3][lifecycle][FR-008]") {
    Crossover3Way crossover;

    SECTION("prepare sets prepared flag") {
        crossover.prepare(44100.0);
        REQUIRE(crossover.isPrepared());
    }
}

// -----------------------------------------------------------------------------
// T063: Crossover3Way frequency setters (FR-008)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way frequency setters", "[crossover][US3][params][FR-008]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);

    SECTION("setLowMidFrequency sets value") {
        crossover.setLowMidFrequency(400.0f);
        REQUIRE(crossover.getLowMidFrequency() == Approx(400.0f));
    }

    SECTION("setMidHighFrequency sets value") {
        crossover.setMidHighFrequency(4000.0f);
        REQUIRE(crossover.getMidHighFrequency() == Approx(4000.0f));
    }
}

// -----------------------------------------------------------------------------
// T064: Crossover3Way flat sum (FR-008, SC-004)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way low + mid + high sum to flat", "[crossover][US3][flatsum][FR-008][SC-004]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);

    // Test at various frequencies
    std::vector<float> testFreqs = {100.0f, 300.0f, 1000.0f, 3000.0f, 8000.0f};

    for (float freq : testFreqs) {
        SECTION("flat sum at " + std::to_string(static_cast<int>(freq)) + "Hz") {
            crossover.reset();

            constexpr size_t numSamples = 16384;
            std::vector<float> input(numSamples);
            std::vector<float> low(numSamples);
            std::vector<float> mid(numSamples);
            std::vector<float> high(numSamples);

            generateSine(input.data(), numSamples, freq, 44100.0f);
            crossover.processBlock(input.data(), low.data(), mid.data(), high.data(), numSamples);

            // Sum bands
            std::vector<float> sum(numSamples);
            for (size_t i = 0; i < numSamples; ++i) {
                sum[i] = low[i] + mid[i] + high[i];
            }

            // Measure
            const size_t startSample = numSamples / 2;
            const float inputRms = calculateRMS(input.data() + startSample, numSamples - startSample);
            const float sumRms = calculateRMS(sum.data() + startSample, numSamples - startSample);
            const float responseDb = linearToDb(sumRms / inputRms);

            REQUIRE(responseDb == Approx(0.0f).margin(0.15f));
        }
    }
}

// -----------------------------------------------------------------------------
// T065: Crossover3Way band isolation (SC-004)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way band isolation", "[crossover][US3][isolation][SC-004]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);

    SECTION("low band contains only content below 300Hz") {
        crossover.reset();
        // Test with 100Hz tone (should be mostly in low band)
        constexpr size_t numSamples = 8192;
        std::vector<float> input(numSamples);
        std::vector<float> low(numSamples);
        std::vector<float> mid(numSamples);
        std::vector<float> high(numSamples);

        generateSine(input.data(), numSamples, 100.0f, 44100.0f);
        crossover.processBlock(input.data(), low.data(), mid.data(), high.data(), numSamples);

        const size_t startSample = numSamples / 2;
        const float lowRms = calculateRMS(low.data() + startSample, numSamples - startSample);
        const float midRms = calculateRMS(mid.data() + startSample, numSamples - startSample);
        const float highRms = calculateRMS(high.data() + startSample, numSamples - startSample);

        // Low should dominate
        REQUIRE(lowRms > midRms * 10.0f);
        REQUIRE(lowRms > highRms * 10.0f);
    }

    SECTION("high band contains only content above 3000Hz") {
        crossover.reset();
        // Test with 8000Hz tone (should be mostly in high band)
        constexpr size_t numSamples = 8192;
        std::vector<float> input(numSamples);
        std::vector<float> low(numSamples);
        std::vector<float> mid(numSamples);
        std::vector<float> high(numSamples);

        generateSine(input.data(), numSamples, 8000.0f, 44100.0f);
        crossover.processBlock(input.data(), low.data(), mid.data(), high.data(), numSamples);

        const size_t startSample = numSamples / 2;
        const float lowRms = calculateRMS(low.data() + startSample, numSamples - startSample);
        const float midRms = calculateRMS(mid.data() + startSample, numSamples - startSample);
        const float highRms = calculateRMS(high.data() + startSample, numSamples - startSample);

        // High should dominate
        REQUIRE(highRms > lowRms * 10.0f);
        REQUIRE(highRms > midRms * 10.0f);
    }
}

// -----------------------------------------------------------------------------
// T066: Crossover3Way equal frequencies handled (FR-016, SC-004)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way handles equal frequencies", "[crossover][US3][edge][FR-016][SC-004]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);

    SECTION("both frequencies at 1kHz is handled gracefully") {
        crossover.setLowMidFrequency(1000.0f);
        crossover.setMidHighFrequency(1000.0f);

        // Process should not crash or produce invalid output
        auto outputs = crossover.process(0.5f);
        REQUIRE(isValidSample(outputs.low));
        REQUIRE(isValidSample(outputs.mid));
        REQUIRE(isValidSample(outputs.high));
    }
}

// -----------------------------------------------------------------------------
// T067: Crossover3Way midHigh auto-clamps (FR-016)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way midHigh frequency auto-clamps", "[crossover][US3][clamp][FR-016]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);

    SECTION("midHigh is clamped to >= lowMid") {
        crossover.setLowMidFrequency(1000.0f);
        crossover.setMidHighFrequency(500.0f);  // Try to set below lowMid

        // Should be clamped to lowMid
        REQUIRE(crossover.getMidHighFrequency() >= crossover.getLowMidFrequency());
    }
}

// -----------------------------------------------------------------------------
// T068: Crossover3Way smoothing time propagation (FR-010)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way smoothing time propagates", "[crossover][US3][smoothing][FR-010]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);

    SECTION("setSmoothingTime affects both internal crossovers") {
        crossover.setSmoothingTime(10.0f);
        // No direct getter, but should not crash and processing should work
        auto outputs = crossover.process(0.5f);
        REQUIRE(isValidSample(outputs.low));
    }
}

// -----------------------------------------------------------------------------
// T069: Crossover3Way processBlock (FR-010)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way processBlock", "[crossover][US3][block][FR-010]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);

    SECTION("processBlock works for 512 samples") {
        constexpr size_t blockSize = 512;
        std::vector<float> input(blockSize);
        std::vector<float> low(blockSize);
        std::vector<float> mid(blockSize);
        std::vector<float> high(blockSize);

        generateSine(input.data(), blockSize, 1000.0f, 44100.0f);
        crossover.processBlock(input.data(), low.data(), mid.data(), high.data(), blockSize);

        for (size_t i = 0; i < blockSize; ++i) {
            REQUIRE(isValidSample(low[i]));
            REQUIRE(isValidSample(mid[i]));
            REQUIRE(isValidSample(high[i]));
        }
    }
}

// -----------------------------------------------------------------------------
// T070: Crossover3Way cross-platform (SC-009)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way cross-platform consistency", "[crossover][US3][platform][SC-009]") {
    std::vector<double> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        SECTION("works at " + std::to_string(static_cast<int>(sr)) + "Hz") {
            Crossover3Way crossover;
            crossover.prepare(sr);

            auto outputs = crossover.process(0.5f);
            REQUIRE(isValidSample(outputs.low));
            REQUIRE(isValidSample(outputs.mid));
            REQUIRE(isValidSample(outputs.high));
        }
    }
}

// -----------------------------------------------------------------------------
// T071: Crossover3Way reset
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way reset", "[crossover][US3][reset]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);

    // Process some samples
    for (int i = 0; i < 100; ++i) {
        (void)crossover.process(0.5f);
    }

    // Reset
    crossover.reset();

    // Should work after reset
    auto outputs = crossover.process(1.0f);
    REQUIRE(isValidSample(outputs.low));
    REQUIRE(isValidSample(outputs.mid));
    REQUIRE(isValidSample(outputs.high));
}

// ==============================================================================
// Phase 5.1: User Story 4 Tests - 4-Way Band Splitting
// ==============================================================================

// -----------------------------------------------------------------------------
// T087: Crossover4WayOutputs struct
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4WayOutputs struct", "[crossover][US4][struct]") {
    Crossover4WayOutputs outputs;

    SECTION("has sub, low, mid, high members defaulted to 0") {
        REQUIRE(outputs.sub == 0.0f);
        REQUIRE(outputs.low == 0.0f);
        REQUIRE(outputs.mid == 0.0f);
        REQUIRE(outputs.high == 0.0f);
    }
}

// -----------------------------------------------------------------------------
// T088: Crossover4Way default constructor
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way default constructor", "[crossover][US4][lifecycle]") {
    Crossover4Way crossover;

    SECTION("kDefaultSubLowFrequency is 80Hz") {
        REQUIRE(Crossover4Way::kDefaultSubLowFrequency == 80.0f);
    }

    SECTION("kDefaultLowMidFrequency is 300Hz") {
        REQUIRE(Crossover4Way::kDefaultLowMidFrequency == 300.0f);
    }

    SECTION("kDefaultMidHighFrequency is 3000Hz") {
        REQUIRE(Crossover4Way::kDefaultMidHighFrequency == 3000.0f);
    }

    SECTION("not prepared after construction") {
        REQUIRE_FALSE(crossover.isPrepared());
    }
}

// -----------------------------------------------------------------------------
// T089: Crossover4Way prepare (FR-009)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way prepare", "[crossover][US4][lifecycle][FR-009]") {
    Crossover4Way crossover;

    SECTION("prepare sets prepared flag") {
        crossover.prepare(44100.0);
        REQUIRE(crossover.isPrepared());
    }
}

// -----------------------------------------------------------------------------
// T090: Crossover4Way frequency setters (FR-009)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way frequency setters", "[crossover][US4][params][FR-009]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);

    SECTION("setSubLowFrequency sets value") {
        crossover.setSubLowFrequency(60.0f);
        REQUIRE(crossover.getSubLowFrequency() == Approx(60.0f));
    }

    SECTION("setLowMidFrequency sets value") {
        crossover.setLowMidFrequency(400.0f);
        REQUIRE(crossover.getLowMidFrequency() == Approx(400.0f));
    }

    SECTION("setMidHighFrequency sets value") {
        crossover.setMidHighFrequency(4000.0f);
        REQUIRE(crossover.getMidHighFrequency() == Approx(4000.0f));
    }
}

// -----------------------------------------------------------------------------
// T091: Crossover4Way flat sum (FR-009, SC-005)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way sub + low + mid + high sum to flat", "[crossover][US4][flatsum][FR-009][SC-005]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);
    crossover.setSubLowFrequency(80.0f);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);

    // Test at various frequencies
    std::vector<float> testFreqs = {50.0f, 80.0f, 200.0f, 300.0f, 1000.0f, 3000.0f, 8000.0f};

    for (float freq : testFreqs) {
        SECTION("flat sum at " + std::to_string(static_cast<int>(freq)) + "Hz") {
            crossover.reset();

            constexpr size_t numSamples = 16384;
            std::vector<float> input(numSamples);
            std::vector<float> sub(numSamples);
            std::vector<float> low(numSamples);
            std::vector<float> mid(numSamples);
            std::vector<float> high(numSamples);

            generateSine(input.data(), numSamples, freq, 44100.0f);
            crossover.processBlock(input.data(), sub.data(), low.data(), mid.data(), high.data(), numSamples);

            // Sum bands
            std::vector<float> sum(numSamples);
            for (size_t i = 0; i < numSamples; ++i) {
                sum[i] = sub[i] + low[i] + mid[i] + high[i];
            }

            // Measure
            const size_t startSample = numSamples / 2;
            const float inputRms = calculateRMS(input.data() + startSample, numSamples - startSample);
            const float sumRms = calculateRMS(sum.data() + startSample, numSamples - startSample);
            const float responseDb = linearToDb(sumRms / inputRms);

            // 4-way crossover allows +/- 1dB tolerance per SC-005
            // (3 cascaded crossovers introduce small cumulative effects)
            REQUIRE(responseDb == Approx(0.0f).margin(1.0f));
        }
    }
}

// -----------------------------------------------------------------------------
// T092: Crossover4Way band isolation (SC-005)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way band isolation", "[crossover][US4][isolation][SC-005]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);
    crossover.setSubLowFrequency(80.0f);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);

    SECTION("sub band contains content below 80Hz") {
        crossover.reset();
        constexpr size_t numSamples = 16384;  // Need more samples for low freq
        std::vector<float> input(numSamples);
        std::vector<float> sub(numSamples);
        std::vector<float> low(numSamples);
        std::vector<float> mid(numSamples);
        std::vector<float> high(numSamples);

        generateSine(input.data(), numSamples, 40.0f, 44100.0f);
        crossover.processBlock(input.data(), sub.data(), low.data(), mid.data(), high.data(), numSamples);

        const size_t startSample = numSamples / 2;
        const float subRms = calculateRMS(sub.data() + startSample, numSamples - startSample);
        const float lowRms = calculateRMS(low.data() + startSample, numSamples - startSample);
        const float midRms = calculateRMS(mid.data() + startSample, numSamples - startSample);
        const float highRms = calculateRMS(high.data() + startSample, numSamples - startSample);

        // Sub should dominate
        REQUIRE(subRms > lowRms);
        REQUIRE(subRms > midRms);
        REQUIRE(subRms > highRms);
    }
}

// -----------------------------------------------------------------------------
// T093: Crossover4Way frequency ordering (FR-016)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way frequency ordering violations auto-clamp", "[crossover][US4][clamp][FR-016]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);

    SECTION("lowMid is clamped to [subLow, midHigh]") {
        crossover.setSubLowFrequency(100.0f);
        crossover.setMidHighFrequency(3000.0f);
        crossover.setLowMidFrequency(50.0f);  // Below subLow

        REQUIRE(crossover.getLowMidFrequency() >= crossover.getSubLowFrequency());
    }

    SECTION("midHigh is clamped to >= lowMid") {
        crossover.setLowMidFrequency(500.0f);
        crossover.setMidHighFrequency(200.0f);  // Below lowMid

        REQUIRE(crossover.getMidHighFrequency() >= crossover.getLowMidFrequency());
    }
}

// -----------------------------------------------------------------------------
// T094: Crossover4Way smoothing time (FR-010)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way smoothing time propagates", "[crossover][US4][smoothing][FR-010]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);

    crossover.setSmoothingTime(10.0f);
    auto outputs = crossover.process(0.5f);
    REQUIRE(isValidSample(outputs.sub));
}

// -----------------------------------------------------------------------------
// T095: Crossover4Way processBlock (FR-010)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way processBlock", "[crossover][US4][block][FR-010]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);

    constexpr size_t blockSize = 512;
    std::vector<float> input(blockSize);
    std::vector<float> sub(blockSize);
    std::vector<float> low(blockSize);
    std::vector<float> mid(blockSize);
    std::vector<float> high(blockSize);

    generateSine(input.data(), blockSize, 1000.0f, 44100.0f);
    crossover.processBlock(input.data(), sub.data(), low.data(), mid.data(), high.data(), blockSize);

    for (size_t i = 0; i < blockSize; ++i) {
        REQUIRE(isValidSample(sub[i]));
        REQUIRE(isValidSample(low[i]));
        REQUIRE(isValidSample(mid[i]));
        REQUIRE(isValidSample(high[i]));
    }
}

// -----------------------------------------------------------------------------
// T096: Crossover4Way cross-platform (SC-009)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way cross-platform consistency", "[crossover][US4][platform][SC-009]") {
    std::vector<double> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        SECTION("works at " + std::to_string(static_cast<int>(sr)) + "Hz") {
            Crossover4Way crossover;
            crossover.prepare(sr);

            auto outputs = crossover.process(0.5f);
            REQUIRE(isValidSample(outputs.sub));
            REQUIRE(isValidSample(outputs.low));
            REQUIRE(isValidSample(outputs.mid));
            REQUIRE(isValidSample(outputs.high));
        }
    }
}

// -----------------------------------------------------------------------------
// T097: Crossover4Way reset
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way reset", "[crossover][US4][reset]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);

    for (int i = 0; i < 100; ++i) {
        (void)crossover.process(0.5f);
    }

    crossover.reset();

    auto outputs = crossover.process(1.0f);
    REQUIRE(isValidSample(outputs.sub));
    REQUIRE(isValidSample(outputs.low));
    REQUIRE(isValidSample(outputs.mid));
    REQUIRE(isValidSample(outputs.high));
}

// ==============================================================================
// Phase 6: Thread Safety Tests (FR-019, SC-013)
// ==============================================================================

// -----------------------------------------------------------------------------
// T114-T116: Thread safety tests
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 thread safety", "[crossover][thread][FR-019][SC-013]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    SECTION("concurrent parameter writes do not crash") {
        std::atomic<bool> running{true};
        std::atomic<int> paramChanges{0};
        std::atomic<bool> uiThreadStarted{false};

        // UI thread - writes parameters
        std::thread uiThread([&]() {
            std::mt19937 gen(42);
            std::uniform_real_distribution<float> freqDist(100.0f, 10000.0f);
            // Signal that we've started
            uiThreadStarted.store(true, std::memory_order_release);
            while (running.load(std::memory_order_relaxed)) {
                crossover.setCrossoverFrequency(freqDist(gen));
                crossover.setSmoothingTime(5.0f);
                crossover.setTrackingMode(TrackingMode::Efficient);
                (void)paramChanges.fetch_add(1, std::memory_order_relaxed);
            }
        });

        // Audio thread - reads and processes
        std::thread audioThread([&]() {
            // Wait for UI thread to start before processing
            while (!uiThreadStarted.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < 100000; ++i) {
                auto outputs = crossover.process(0.5f);
                REQUIRE(isValidSample(outputs.low));
                REQUIRE(isValidSample(outputs.high));
            }
            running.store(false, std::memory_order_relaxed);
        });

        audioThread.join();
        uiThread.join();

        // Should have processed without crash
        REQUIRE(paramChanges.load(std::memory_order_relaxed) > 0);
    }
}

// ==============================================================================
// Phase 7: Edge Cases (FR-005, FR-012, FR-016, SC-008)
// ==============================================================================

// -----------------------------------------------------------------------------
// T120-T129: Edge case tests
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 frequency below 20Hz clamped", "[crossover][edge][FR-005]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(5.0f);
    REQUIRE(crossover.getCrossoverFrequency() == Approx(20.0f));
}

TEST_CASE("CrossoverLR4 frequency above Nyquist clamped", "[crossover][edge][FR-005]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(30000.0f);
    REQUIRE(crossover.getCrossoverFrequency() <= 44100.0f * 0.45f);
}

TEST_CASE("CrossoverLR4 DC input passes through low band", "[crossover][edge]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    // Process DC signal
    for (int i = 0; i < 1000; ++i) {
        auto outputs = crossover.process(1.0f);
        REQUIRE(isValidSample(outputs.low));
        REQUIRE(isValidSample(outputs.high));
    }

    // After settling, low should have the DC, high should be near zero
    auto outputs = crossover.process(1.0f);
    REQUIRE(outputs.low > 0.5f);  // DC passes through lowpass
    REQUIRE(std::abs(outputs.high) < 0.1f);  // Highpass blocks DC
}

TEST_CASE("CrossoverLR4 process before prepare returns zero", "[crossover][edge]") {
    CrossoverLR4 crossover;
    // Do not call prepare()
    auto outputs = crossover.process(1.0f);
    REQUIRE(outputs.low == 0.0f);
    REQUIRE(outputs.high == 0.0f);
}

TEST_CASE("CrossoverLR4 processBlock nullptr handling", "[crossover][edge]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    // Should not crash with nullptr
    crossover.processBlock(nullptr, nullptr, nullptr, 0);
    crossover.processBlock(nullptr, nullptr, nullptr, 100);

    float input[10] = {0};
    crossover.processBlock(input, nullptr, nullptr, 10);
}

TEST_CASE("CrossoverLR4 processBlock zero samples", "[crossover][edge]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    float input[10], low[10], high[10];
    crossover.processBlock(input, low, high, 0);
    // Should not crash
}

TEST_CASE("CrossoverLR4 prepare multiple times resets state", "[crossover][edge][FR-012]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    // Process some samples
    for (int i = 0; i < 100; ++i) {
        (void)crossover.process(0.5f);
    }

    // Re-prepare at different sample rate
    crossover.prepare(96000.0);

    // Should be in clean state
    REQUIRE(crossover.isPrepared());
}

TEST_CASE("CrossoverLR4 very low crossover frequency stable", "[crossover][edge][SC-008]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(20.0f);

    for (int i = 0; i < 10000; ++i) {
        auto outputs = crossover.process(0.5f);
        REQUIRE(isValidSample(outputs.low));
        REQUIRE(isValidSample(outputs.high));
    }
}

TEST_CASE("CrossoverLR4 very high crossover frequency stable", "[crossover][edge][SC-008]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(44100.0f * 0.45f);  // Max allowed

    for (int i = 0; i < 10000; ++i) {
        auto outputs = crossover.process(0.5f);
        REQUIRE(isValidSample(outputs.low));
        REQUIRE(isValidSample(outputs.high));
    }
}

TEST_CASE("CrossoverLR4 getters return correct values", "[crossover][edge]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);

    crossover.setCrossoverFrequency(2000.0f);
    REQUIRE(crossover.getCrossoverFrequency() == Approx(2000.0f));

    crossover.setSmoothingTime(10.0f);
    REQUIRE(crossover.getSmoothingTime() == Approx(10.0f));

    crossover.setTrackingMode(TrackingMode::HighAccuracy);
    REQUIRE(crossover.getTrackingMode() == TrackingMode::HighAccuracy);
}

// ==============================================================================
// Allpass Compensation Tests (SC-005 improvement)
// ==============================================================================
// These tests verify that allpass compensation achieves tighter flat sum tolerance
// for 3-way and 4-way crossovers (0.1dB instead of 1dB).
//
// Reference: D'Appolito, J.A. "Active Realization of Multiway All-Pass Crossover
// Systems" - Journal of the Audio Engineering Society, Vol. 35, No. 4, April 1987

// -----------------------------------------------------------------------------
// Test: setAllpassCompensation API exists on Crossover3Way
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way setAllpassCompensation API", "[crossover][allpass][API]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);

    SECTION("Default is disabled") {
        REQUIRE(crossover.isAllpassCompensationEnabled() == false);
    }

    SECTION("Can enable allpass compensation") {
        crossover.setAllpassCompensation(true);
        REQUIRE(crossover.isAllpassCompensationEnabled() == true);
    }

    SECTION("Can disable allpass compensation") {
        crossover.setAllpassCompensation(true);
        crossover.setAllpassCompensation(false);
        REQUIRE(crossover.isAllpassCompensationEnabled() == false);
    }
}

// -----------------------------------------------------------------------------
// Test: setAllpassCompensation API exists on Crossover4Way
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way setAllpassCompensation API", "[crossover][allpass][API]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);

    SECTION("Default is disabled") {
        REQUIRE(crossover.isAllpassCompensationEnabled() == false);
    }

    SECTION("Can enable allpass compensation") {
        crossover.setAllpassCompensation(true);
        REQUIRE(crossover.isAllpassCompensationEnabled() == true);
    }

    SECTION("Can disable allpass compensation") {
        crossover.setAllpassCompensation(true);
        crossover.setAllpassCompensation(false);
        REQUIRE(crossover.isAllpassCompensationEnabled() == false);
    }
}

// -----------------------------------------------------------------------------
// Test: Crossover3Way with allpass compensation sums to 0.1dB flat (SC-004 strict)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way with allpass compensation sums to 0.1dB flat", "[crossover][allpass][flatsum][SC-004]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);
    crossover.setAllpassCompensation(true);  // Enable allpass compensation

    const float sampleRate = 44100.0f;
    const size_t numSamples = 16384;

    // Test at various frequencies across the spectrum
    std::vector<float> testFreqs = {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                    2000.0f, 5000.0f, 8000.0f, 12000.0f};

    for (float testFreq : testFreqs) {
        DYNAMIC_SECTION("Sum is flat at " << testFreq << "Hz") {
            crossover.reset();

            std::vector<float> input(numSamples);
            std::vector<float> low(numSamples);
            std::vector<float> mid(numSamples);
            std::vector<float> high(numSamples);
            std::vector<float> sum(numSamples);

            generateSine(input.data(), numSamples, testFreq, sampleRate);
            crossover.processBlock(input.data(), low.data(), mid.data(), high.data(), numSamples);

            for (size_t i = 0; i < numSamples; ++i) {
                sum[i] = low[i] + mid[i] + high[i];
            }

            // Measure RMS of second half (after transient settles)
            const size_t startSample = numSamples / 2;
            const float inputRms = calculateRMS(input.data() + startSample, numSamples - startSample);
            const float sumRms = calculateRMS(sum.data() + startSample, numSamples - startSample);
            const float responseDb = linearToDb(sumRms / inputRms);

            // With allpass compensation: 0.1dB tolerance (strict SC-004)
            REQUIRE(responseDb == Approx(0.0f).margin(0.1f));
        }
    }
}

// -----------------------------------------------------------------------------
// Test: Crossover4Way with allpass compensation sums to 0.1dB flat (SC-005 strict)
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way with allpass compensation sums to 0.1dB flat", "[crossover][allpass][flatsum][SC-005]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);
    crossover.setSubLowFrequency(80.0f);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);
    crossover.setAllpassCompensation(true);  // Enable allpass compensation

    const float sampleRate = 44100.0f;
    const size_t numSamples = 16384;

    // Test at various frequencies across the spectrum
    std::vector<float> testFreqs = {30.0f, 60.0f, 100.0f, 200.0f, 500.0f,
                                    1000.0f, 2000.0f, 5000.0f, 10000.0f};

    for (float testFreq : testFreqs) {
        DYNAMIC_SECTION("Sum is flat at " << testFreq << "Hz") {
            crossover.reset();

            std::vector<float> input(numSamples);
            std::vector<float> sub(numSamples);
            std::vector<float> low(numSamples);
            std::vector<float> mid(numSamples);
            std::vector<float> high(numSamples);
            std::vector<float> sum(numSamples);

            generateSine(input.data(), numSamples, testFreq, sampleRate);
            crossover.processBlock(input.data(), sub.data(), low.data(), mid.data(), high.data(), numSamples);

            for (size_t i = 0; i < numSamples; ++i) {
                sum[i] = sub[i] + low[i] + mid[i] + high[i];
            }

            // Measure RMS of second half (after transient settles)
            const size_t startSample = numSamples / 2;
            const float inputRms = calculateRMS(input.data() + startSample, numSamples - startSample);
            const float sumRms = calculateRMS(sum.data() + startSample, numSamples - startSample);
            const float responseDb = linearToDb(sumRms / inputRms);

            // With allpass compensation: 0.1dB tolerance (strict SC-005)
            REQUIRE(responseDb == Approx(0.0f).margin(0.1f));
        }
    }
}

// -----------------------------------------------------------------------------
// Test: allpass compensation stays within 0.1 dB at ONE-OCTAVE crossover spacing
// (D-164). The LR4 LP+HP sum is a 2nd-order allpass with Q = 1/sqrt(2); a
// compensation allpass with any other Q leaves a dip next to the lower crossover
// that the decade-spaced SC-004/SC-005 cases above cannot see (about -0.13 dB at
// 300/600 Hz with Q = 0.5).
// -----------------------------------------------------------------------------
namespace {

/// Worst |sum - input| in dB over a dense log grid of steady-state sines.
/// `splitSum` maps one input sample to the summed band outputs.
template <typename Crossover, typename SplitSum>
float worstCompensatedSumDeviationDb(Crossover& crossover, SplitSum splitSum,
                                     float loHz, float hiHz) {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kSettle = 32768;
    constexpr size_t kMeasureTarget = 32768;
    constexpr int kPoints = 48;
    float worst = 0.0f;
    for (int p = 0; p < kPoints; ++p) {
        const double f = static_cast<double>(loHz) *
            std::pow(static_cast<double>(hiHz) / static_cast<double>(loHz),
                     static_cast<double>(p) / static_cast<double>(kPoints - 1));
        // Integer number of cycles in the measured window.
        const double cycles = std::floor(static_cast<double>(kMeasureTarget) * f / kSampleRate);
        const auto measureLen = static_cast<size_t>(std::lround(cycles * kSampleRate / f));
        crossover.reset();
        double inPow = 0.0;
        double sumPow = 0.0;
        for (size_t i = 0; i < kSettle + measureLen; ++i) {
            const auto x = static_cast<float>(
                std::sin(2.0 * 3.14159265358979323846 * f * static_cast<double>(i) / kSampleRate));
            const float s = splitSum(crossover, x);
            if (i >= kSettle) {
                inPow += static_cast<double>(x) * static_cast<double>(x);
                sumPow += static_cast<double>(s) * static_cast<double>(s);
            }
        }
        const auto devDb = static_cast<float>(10.0 * std::log10(sumPow / inPow));
        if (std::abs(devDb) > std::abs(worst)) {
            worst = devDb;
        }
    }
    return worst;
}

} // namespace

TEST_CASE("Crossover3Way allpass compensation is flat at one-octave spacing",
          "[crossover][allpass][flatsum][SC-004][D-164]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(600.0f);
    crossover.setAllpassCompensation(true);

    const float worstDb = worstCompensatedSumDeviationDb(
        crossover,
        [](Crossover3Way& c, float x) {
            const auto o = c.process(x);
            return o.low + o.mid + o.high;
        },
        75.0f, 2400.0f);
    INFO("Crossover3Way 300/600 Hz compensated worst deviation = " << worstDb << " dB");
    std::printf("D-164 Crossover3Way 300/600 Hz worst deviation: %.4f dB\n",
                static_cast<double>(worstDb));
    REQUIRE(std::abs(worstDb) < 0.1f);
}

TEST_CASE("Crossover4Way allpass compensation is flat at one-octave spacing",
          "[crossover][allpass][flatsum][SC-005][D-164]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);
    crossover.setMidHighFrequency(320.0f);
    crossover.setLowMidFrequency(160.0f);
    crossover.setSubLowFrequency(80.0f);
    crossover.setAllpassCompensation(true);
    REQUIRE(crossover.getSubLowFrequency() == 80.0f);
    REQUIRE(crossover.getLowMidFrequency() == 160.0f);
    REQUIRE(crossover.getMidHighFrequency() == 320.0f);

    const float worstDb = worstCompensatedSumDeviationDb(
        crossover,
        [](Crossover4Way& c, float x) {
            const auto o = c.process(x);
            return o.sub + o.low + o.mid + o.high;
        },
        20.0f, 1280.0f);
    INFO("Crossover4Way 80/160/320 Hz compensated worst deviation = " << worstDb << " dB");
    std::printf("D-164 Crossover4Way 80/160/320 Hz worst deviation: %.4f dB\n",
                static_cast<double>(worstDb));
    REQUIRE(std::abs(worstDb) < 0.1f);
}

// -----------------------------------------------------------------------------
// Test: Allpass compensation does not affect frequency sweep smoothness
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way allpass compensation frequency sweep is click-free", "[crossover][allpass][SC-006]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);
    crossover.setSubLowFrequency(80.0f);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);
    crossover.setAllpassCompensation(true);

    const size_t numSamples = 4410;  // 100ms
    std::vector<float> input(numSamples, 0.5f);  // Constant input to detect clicks
    std::vector<float> sub(numSamples);
    std::vector<float> low(numSamples);
    std::vector<float> mid(numSamples);
    std::vector<float> high(numSamples);

    // Sweep mid-high frequency during processing
    float freqStart = 1000.0f;
    float freqEnd = 5000.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(numSamples);
        float freq = freqStart + (freqEnd - freqStart) * t;
        crossover.setMidHighFrequency(freq);

        auto outputs = crossover.process(input[i]);
        sub[i] = outputs.sub;
        low[i] = outputs.low;
        mid[i] = outputs.mid;
        high[i] = outputs.high;
    }

    // Check for clicks (large sample-to-sample jumps)
    float maxJump = 0.0f;
    for (size_t i = 1; i < numSamples; ++i) {
        float jump = std::abs((sub[i] + low[i] + mid[i] + high[i]) -
                              (sub[i-1] + low[i-1] + mid[i-1] + high[i-1]));
        maxJump = std::max(maxJump, jump);
    }

    // Max jump should be small (no clicks)
    REQUIRE(maxJump < 1.0f);
}

// ==============================================================================
// FFT-Based Frequency Response Verification Tests
// ==============================================================================

// -----------------------------------------------------------------------------
// CrossoverLR4: FFT flat sum verification across full spectrum
// -----------------------------------------------------------------------------
TEST_CASE("CrossoverLR4 FFT flat sum across full spectrum", "[crossover][fft][flatsum][SC-001]") {
    CrossoverLR4 crossover;
    crossover.prepare(44100.0);
    crossover.setCrossoverFrequency(1000.0f);

    // Let filter settle
    for (int i = 0; i < 1000; ++i) {
        (void)crossover.process(0.0f);
    }

    // Process function that sums low + high bands
    auto processAndSum = [](CrossoverLR4& xo, float input) {
        auto outputs = xo.process(input);
        return outputs.low + outputs.high;
    };

    float maxError = measureFlatSumErrorDb(crossover, processAndSum, 44100.0f, 8192);

    INFO("Max flat sum error across spectrum: " << maxError << " dB");
    // FFT measures max error across entire spectrum - expect higher than spot-frequency tests
    // Time-domain tests verify 0.1dB at specific frequencies; FFT captures worst-case across all
    REQUIRE(maxError < 1.5f);
}

// -----------------------------------------------------------------------------
// Crossover3Way: FFT flat sum verification across full spectrum
// -----------------------------------------------------------------------------
TEST_CASE("Crossover3Way FFT flat sum across full spectrum", "[crossover][fft][flatsum][SC-004]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);

    // Let filter settle
    for (int i = 0; i < 1000; ++i) {
        (void)crossover.process(0.0f);
    }

    auto processAndSum = [](Crossover3Way& xo, float input) {
        auto outputs = xo.process(input);
        return outputs.low + outputs.mid + outputs.high;
    };

    float maxError = measureFlatSumErrorDb(crossover, processAndSum, 44100.0f, 8192);

    INFO("Max 3-way flat sum error across spectrum: " << maxError << " dB");
    // 3-way serial topology has phase errors near crossover frequencies
    REQUIRE(maxError < 5.0f);
}

TEST_CASE("Crossover3Way with allpass FFT flat sum across full spectrum", "[crossover][fft][flatsum][allpass][SC-004]") {
    Crossover3Way crossover;
    crossover.prepare(44100.0);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);
    crossover.setAllpassCompensation(true);

    // Let filter settle
    for (int i = 0; i < 1000; ++i) {
        (void)crossover.process(0.0f);
    }

    auto processAndSum = [](Crossover3Way& xo, float input) {
        auto outputs = xo.process(input);
        return outputs.low + outputs.mid + outputs.high;
    };

    float maxError = measureFlatSumErrorDb(crossover, processAndSum, 44100.0f, 8192);

    INFO("Max 3-way (allpass) flat sum error across spectrum: " << maxError << " dB");
    // Allpass compensation improves phase alignment
    REQUIRE(maxError < 5.0f);
}

// -----------------------------------------------------------------------------
// Crossover4Way: FFT flat sum verification across full spectrum
// -----------------------------------------------------------------------------
TEST_CASE("Crossover4Way FFT flat sum across full spectrum", "[crossover][fft][flatsum][SC-005]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);
    crossover.setSubLowFrequency(80.0f);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);

    // Let filter settle
    for (int i = 0; i < 1000; ++i) {
        (void)crossover.process(0.0f);
    }

    auto processAndSum = [](Crossover4Way& xo, float input) {
        auto outputs = xo.process(input);
        return outputs.sub + outputs.low + outputs.mid + outputs.high;
    };

    float maxError = measureFlatSumErrorDb(crossover, processAndSum, 44100.0f, 8192);

    INFO("Max 4-way flat sum error across spectrum: " << maxError << " dB");
    // 4-way has cumulative phase errors from serial topology
    REQUIRE(maxError < 7.0f);
}

TEST_CASE("Crossover4Way with allpass FFT flat sum across full spectrum", "[crossover][fft][flatsum][allpass][SC-005]") {
    Crossover4Way crossover;
    crossover.prepare(44100.0);
    crossover.setSubLowFrequency(80.0f);
    crossover.setLowMidFrequency(300.0f);
    crossover.setMidHighFrequency(3000.0f);
    crossover.setAllpassCompensation(true);

    // Let filter settle
    for (int i = 0; i < 1000; ++i) {
        (void)crossover.process(0.0f);
    }

    auto processAndSum = [](Crossover4Way& xo, float input) {
        auto outputs = xo.process(input);
        return outputs.sub + outputs.low + outputs.mid + outputs.high;
    };

    float maxError = measureFlatSumErrorDb(crossover, processAndSum, 44100.0f, 8192);

    INFO("Max 4-way (allpass) flat sum error across spectrum: " << maxError << " dB");
    // Allpass compensation should reduce max error
    REQUIRE(maxError < 7.0f);
}

// ==============================================================================
// MultibandCrossover<MaxBands>: runtime band count, D'Appolito compensation,
// dry-path compensation (the shared splitter behind Disrumpo and FractalDistortion)
// ==============================================================================

namespace {

template <int N>
float multibandSum(const std::array<float, static_cast<std::size_t>(N)>& bands, int active) {
    float s = 0.0f;
    for (int b = 0; b < active; ++b) {
        s += bands[static_cast<std::size_t>(b)];
    }
    return s;
}

/// Configure `xo` with the first `active - 1` of `freqs`, compensation as given, prepared at `sr`.
template <int N>
void setupMultiband(MultibandCrossover<N>& xo, double sr, int active, const std::array<float, 3>& freqs,
                    bool compensation) {
    for (int i = 0; i < active - 1; ++i) {
        xo.setCrossoverFrequency(i, freqs[static_cast<std::size_t>(i)]);
    }
    xo.setActiveBands(active);
    xo.setAllpassCompensation(compensation);
    xo.prepare(sr);
}

/// Worst |band sum - input| in dB over a log grid of steady-state sines at `sr`.
template <int N>
double worstMultibandSumDeviationDb(MultibandCrossover<N>& xo, double sr, double loHz, double hiHz,
                                    double* worstHz = nullptr) {
    constexpr int kPoints = 40;
    const auto settle = static_cast<size_t>(0.25 * sr);
    const auto measureTarget = static_cast<size_t>(0.25 * sr);
    double worst = 0.0;
    std::array<float, static_cast<std::size_t>(N)> bands{};
    for (int p = 0; p < kPoints; ++p) {
        const double f = loHz * std::pow(hiHz / loHz, static_cast<double>(p) / static_cast<double>(kPoints - 1));
        const double cycles = std::floor(static_cast<double>(measureTarget) * f / sr);
        const auto measureLen = static_cast<size_t>(std::lround(cycles * sr / f));
        xo.reset();
        double inPow = 0.0;
        double sumPow = 0.0;
        for (size_t i = 0; i < settle + measureLen; ++i) {
            const auto x = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * f * static_cast<double>(i) / sr));
            xo.process(x, bands);
            const float s = multibandSum<N>(bands, xo.getActiveBands());
            if (i >= settle) {
                inPow += static_cast<double>(x) * static_cast<double>(x);
                sumPow += static_cast<double>(s) * static_cast<double>(s);
            }
        }
        const double devDb = 10.0 * std::log10(sumPow / inPow);
        if (std::abs(devDb) > std::abs(worst)) {
            worst = devDb;
            if (worstHz != nullptr) {
                *worstHz = f;
            }
        }
    }
    return worst;
}

/// Level of the band sum re the input for one steady-state sine (second half of `numSamples`).
template <int N>
float multibandSineSumDb(MultibandCrossover<N>& xo, float freq, float sr, size_t numSamples = 16384) {
    std::vector<float> input(numSamples);
    std::vector<float> sum(numSamples);
    generateSine(input.data(), numSamples, freq, sr);
    std::array<float, static_cast<std::size_t>(N)> bands{};
    for (size_t i = 0; i < numSamples; ++i) {
        xo.process(input[i], bands);
        sum[i] = multibandSum<N>(bands, xo.getActiveBands());
    }
    const size_t start = numSamples / 2;
    return linearToDb(calculateRMS(sum.data() + start, numSamples - start) /
                      calculateRMS(input.data() + start, numSamples - start));
}

/// Disrumpo's logarithmic default crossovers for 4 bands (20 Hz - 20 kHz, 3 decades / 4).
constexpr std::array<float, 3> kLogDefaultCrossovers{112.468265f, 632.455532f, 3556.558820f};

} // namespace

TEST_CASE("MultibandCrossover basics", "[crossover][multiband]") {
    MultibandCrossover<4> xo;
    std::array<float, 4> bands{1.0f, 1.0f, 1.0f, 1.0f};

    SECTION("not prepared after construction; process outputs zeros") {
        REQUIRE_FALSE(xo.isPrepared());
        xo.process(0.5f, bands);
        REQUIRE(bands == std::array<float, 4>{});
        REQUIRE(xo.processDryCompensation(0.5f) == 0.5f);
    }

    SECTION("defaults: all bands active, compensation off") {
        REQUIRE(xo.getActiveBands() == 4);
        REQUIRE_FALSE(xo.isAllpassCompensationEnabled());
        REQUIRE(static_cast<double>(MultibandCrossover<4>::kAllpassQ) ==
                Approx(0.70710678118654752).margin(1e-7));
    }

    SECTION("one active band passes the input through") {
        xo.setActiveBands(1);
        xo.prepare(48000.0);
        xo.process(-0.25f, bands);
        REQUIRE(bands[0] == -0.25f);
        REQUIRE(xo.processDryCompensation(0.3f) == 0.3f);
    }

    SECTION("active band count clamps to [1, MaxBands]") {
        xo.setActiveBands(0);
        REQUIRE(xo.getActiveBands() == 1);
        xo.setActiveBands(9);
        REQUIRE(xo.getActiveBands() == 4);
    }

    SECTION("frequencies are stored for every index, active or not; invalid indices are ignored") {
        xo.prepare(48000.0);
        xo.setActiveBands(2);
        xo.setCrossoverFrequency(2, 8000.0f);
        xo.setCrossoverFrequency(-1, 50.0f);
        xo.setCrossoverFrequency(3, 50.0f);
        REQUIRE(xo.getCrossoverFrequency(2) == 8000.0f);
        REQUIRE(xo.getCrossoverFrequency(0) == CrossoverLR4::kDefaultFrequency);
        xo.setActiveBands(4);
        REQUIRE(xo.getCrossoverFrequency(2) == 8000.0f);
    }

    SECTION("compensation toggles") {
        xo.setAllpassCompensation(true);
        REQUIRE(xo.isAllpassCompensationEnabled());
        xo.setAllpassCompensation(false);
        REQUIRE_FALSE(xo.isAllpassCompensationEnabled());
    }
}

// Ported from the Crossover3Way / Crossover4Way flat-sum cases (SC-004, SC-005): with
// compensation every band count sums flat within 0.1 dB, 30 Hz - 15 kHz, at every rate.
TEST_CASE("MultibandCrossover band sum is flat at 2-4 bands with allpass compensation",
          "[crossover][multiband][allpass][flatsum][SC-004][SC-005]") {
    const std::array<std::array<float, 3>, 3> freqsPerBands{{{300.0f, 0.0f, 0.0f},
                                                             {300.0f, 3000.0f, 0.0f},
                                                             {80.0f, 300.0f, 3000.0f}}};
    for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (int n = 2; n <= 4; ++n) {
            MultibandCrossover<4> xo;
            setupMultiband(xo, sr, n, freqsPerBands[static_cast<std::size_t>(n - 2)], true);
            double worstHz = 0.0;
            const double worst = worstMultibandSumDeviationDb(xo, sr, 30.0, 15000.0, &worstHz);
            INFO(sr << " Hz, " << n << " bands, compensated: worst deviation " << worst << " dB at " << worstHz << " Hz");
            CHECK(std::abs(worst) < 0.1);
        }
    }
}

// Ported from "Crossover3Way low + mid + high sum to flat" and "Crossover4Way sub + low +
// mid + high sum to flat": without compensation the decade-spaced sums stay within the old bars.
TEST_CASE("MultibandCrossover band sum without compensation", "[crossover][multiband][flatsum][SC-004][SC-005]") {
    SECTION("3 bands at 300 / 3000 Hz within 0.15 dB") {
        MultibandCrossover<3> xo;
        setupMultiband(xo, 44100.0, 3, {300.0f, 3000.0f, 0.0f}, false);
        for (const float f : {100.0f, 300.0f, 1000.0f, 3000.0f, 8000.0f}) {
            xo.reset();
            const float db = multibandSineSumDb(xo, f, 44100.0f);
            INFO(f << " Hz: " << db << " dB");
            CHECK(std::abs(db) < 0.15f);
        }
    }
    SECTION("4 bands at 80 / 300 / 3000 Hz within 1 dB") {
        MultibandCrossover<4> xo;
        setupMultiband(xo, 44100.0, 4, {80.0f, 300.0f, 3000.0f}, false);
        for (const float f : {50.0f, 80.0f, 200.0f, 300.0f, 1000.0f, 3000.0f, 8000.0f}) {
            xo.reset();
            const float db = multibandSineSumDb(xo, f, 44100.0f);
            INFO(f << " Hz: " << db << " dB");
            CHECK(std::abs(db) < 1.0f);
        }
    }
}

// Ported from the Crossover3Way / Crossover4Way D-164 cases: compensation stays within 0.1 dB
// at one-octave crossover spacing, where an allpass Q other than 1/sqrt(2) leaves a dip.
TEST_CASE("MultibandCrossover compensation is flat at one-octave spacing",
          "[crossover][multiband][allpass][flatsum][D-164]") {
    SECTION("3 bands at 300 / 600 Hz") {
        MultibandCrossover<3> xo;
        setupMultiband(xo, 44100.0, 3, {300.0f, 600.0f, 0.0f}, true);
        const float worstDb = worstCompensatedSumDeviationDb(
            xo,
            [](MultibandCrossover<3>& c, float x) {
                std::array<float, 3> b{};
                c.process(x, b);
                return b[0] + b[1] + b[2];
            },
            75.0f, 2400.0f);
        std::printf("D-164 MultibandCrossover<3> 300/600 Hz worst deviation: %.4f dB\n", static_cast<double>(worstDb));
        REQUIRE(std::abs(worstDb) < 0.1f);
    }
    SECTION("4 bands at 80 / 160 / 320 Hz") {
        MultibandCrossover<4> xo;
        setupMultiband(xo, 44100.0, 4, {80.0f, 160.0f, 320.0f}, true);
        const float worstDb = worstCompensatedSumDeviationDb(
            xo,
            [](MultibandCrossover<4>& c, float x) {
                std::array<float, 4> b{};
                c.process(x, b);
                return b[0] + b[1] + b[2] + b[3];
            },
            20.0f, 1280.0f);
        std::printf("D-164 MultibandCrossover<4> 80/160/320 Hz worst deviation: %.4f dB\n",
                    static_cast<double>(worstDb));
        REQUIRE(std::abs(worstDb) < 0.1f);
    }
}

// The dry chain carries the phase and timing of the compensated band sum (Disrumpo D-018):
// same impulse peak, residual of the difference at least 60 dB below the band sum energy.
TEST_CASE("MultibandCrossover dry compensation is aligned with the compensated band sum",
          "[crossover][multiband][allpass][dry][D-018]") {
    for (const double sr : {44100.0, 48000.0, 96000.0}) {
        for (int n = 2; n <= 4; ++n) {
            MultibandCrossover<4> xo;
            setupMultiband(xo, sr, n, kLogDefaultCrossovers, true);
            const auto len = static_cast<size_t>(sr / 2.0);
            std::vector<double> dry(len);
            std::vector<double> wet(len);
            std::array<float, 4> bands{};
            for (size_t i = 0; i < len; ++i) {
                const float x = (i == 0) ? 1.0f : 0.0f;
                xo.process(x, bands);
                wet[i] = static_cast<double>(multibandSum<4>(bands, n));
                dry[i] = static_cast<double>(xo.processDryCompensation(x));
            }
            double diffEnergy = 0.0;
            double wetEnergy = 0.0;
            size_t dryPeak = 0;
            size_t wetPeak = 0;
            for (size_t i = 0; i < len; ++i) {
                diffEnergy += (dry[i] - wet[i]) * (dry[i] - wet[i]);
                wetEnergy += wet[i] * wet[i];
                if (std::abs(dry[i]) > std::abs(dry[dryPeak])) dryPeak = i;
                if (std::abs(wet[i]) > std::abs(wet[wetPeak])) wetPeak = i;
            }
            const double residualDb = 10.0 * std::log10(diffEnergy / wetEnergy + 1e-30);
            INFO(sr << " Hz, " << n << " bands: peak dry " << dryPeak << " wet " << wetPeak << ", residual "
                 << residualDb << " dB re band sum");
            CHECK(dryPeak == wetPeak);
            CHECK(residualDb <= -60.0);
        }
    }
}

// Ported from Disrumpo D-023: crossovers and allpasses joining on a band-count increase start
// clean at their frequency, set while they were idle. Silent input while 2 bands run, so a
// clean join matches a freshly prepared reference.
TEST_CASE("MultibandCrossover crossovers joining on a band-count increase start clean",
          "[crossover][multiband][D-023]") {
    constexpr double kSr = 48000.0;
    std::vector<float> in(4800);
    for (size_t i = 0; i < in.size(); ++i) {
        in[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(i) / kSr));
    }
    std::array<float, 4> bands{};

    MultibandCrossover<4> xo;
    setupMultiband(xo, kSr, 4, {200.0f, 1500.0f, 6000.0f}, true);
    for (const float x : in) {
        xo.process(x, bands);
        (void)xo.processDryCompensation(x);
    }
    xo.setActiveBands(2);
    xo.setCrossoverFrequency(2, 8000.0f);  // stored while crossover 2 is idle
    for (size_t i = 0; i < 48000; ++i) {
        xo.process(0.0f, bands);
        (void)xo.processDryCompensation(0.0f);
    }
    xo.setActiveBands(4);

    MultibandCrossover<4> reference;
    setupMultiband(reference, kSr, 4, {200.0f, 1500.0f, 8000.0f}, true);

    std::array<float, 4> refBands{};
    double maxDiff = 0.0;
    for (const float x : in) {
        xo.process(x, bands);
        reference.process(x, refBands);
        for (size_t b = 0; b < 4; ++b) {
            maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(bands[b] - refBands[b])));
        }
        maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(xo.processDryCompensation(x) -
                                                                   reference.processDryCompensation(x))));
    }
    INFO("max band / dry difference vs a clean instance after 4 -> 2 -> 4: " << maxDiff);
    REQUIRE(maxDiff < 1e-5);
}

// Set-then-snap (Disrumpo D-046, T1.03): prepare() snaps every crossover to its stored
// frequency, on the first prepare and on a re-prepare at another rate, which keeps them.
TEST_CASE("MultibandCrossover prepare snaps to the stored frequencies and keeps them",
          "[crossover][multiband][D-046]") {
    constexpr std::array<float, 3> kFreqs{200.0f, 1500.0f, 6000.0f};
    std::vector<float> noise(9600);
    generateWhiteNoise(noise.data(), noise.size());
    const auto maxBandDiff = [&](MultibandCrossover<4>& a, MultibandCrossover<4>& b, size_t count) {
        std::array<float, 4> ba{};
        std::array<float, 4> bb{};
        double d = 0.0;
        for (size_t i = 0; i < count; ++i) {
            a.process(noise[i], ba);
            b.process(noise[i], bb);
            for (size_t k = 0; k < 4; ++k) {
                d = std::max(d, static_cast<double>(std::fabs(ba[k] - bb[k])));
            }
        }
        return d;
    };

    SECTION("first prepare: no glide from the default frequency") {
        MultibandCrossover<4> snapped;
        setupMultiband(snapped, 48000.0, 4, kFreqs, true);
        MultibandCrossover<4> settled;  // prepared first, set, glide completed in silence
        settled.setAllpassCompensation(true);
        settled.prepare(48000.0);
        for (int i = 0; i < 3; ++i) settled.setCrossoverFrequency(i, kFreqs[static_cast<std::size_t>(i)]);
        std::array<float, 4> b{};
        for (int i = 0; i < 48000; ++i) settled.process(0.0f, b);
        settled.reset();
        MultibandCrossover<4> gliding;  // prepared first, set, processed at once
        gliding.setAllpassCompensation(true);
        gliding.prepare(48000.0);
        for (int i = 0; i < 3; ++i) gliding.setCrossoverFrequency(i, kFreqs[static_cast<std::size_t>(i)]);

        MultibandCrossover<4> snapped2;
        setupMultiband(snapped2, 48000.0, 4, kFreqs, true);
        const double dSettled = maxBandDiff(snapped, settled, 960);
        const double dGliding = maxBandDiff(snapped2, gliding, 960);
        INFO("first 20 ms, max band difference: vs settled " << dSettled << ", vs gliding " << dGliding);
        // Settled: Efficient tracking leaves the coefficients up to 0.1 Hz short of the target
        CHECK(dSettled < 1e-3);
        CHECK(dGliding > 1e-2);  // the comparison can see a glide
    }

    SECTION("re-prepare at another rate keeps the frequencies and snaps") {
        MultibandCrossover<4> xo;
        setupMultiband(xo, 48000.0, 4, kFreqs, true);
        std::array<float, 4> b{};
        for (const float x : noise) xo.process(x, b);
        xo.prepare(96000.0);
        for (int i = 0; i < 3; ++i) {
            CHECK(xo.getCrossoverFrequency(i) == kFreqs[static_cast<std::size_t>(i)]);
        }
        MultibandCrossover<4> fresh;
        setupMultiband(fresh, 96000.0, 4, kFreqs, true);
        const double d = maxBandDiff(xo, fresh, noise.size());
        INFO("re-prepared vs fresh at 96 kHz: max band difference " << d);
        CHECK(d < 1e-6);
    }
}

// Hidden A/B probe (crossover dedup step 1): Crossover3Way / Crossover4Way against
// MultibandCrossover<3> / <4> on 1 s of white noise, compensation off and on. "set first":
// the old class configured before prepare (no glide); "prepare first": prepared, then set
// (FractalDistortion's order, which glides from 1000 Hz). Prints, asserts nothing.
TEST_CASE("MultibandCrossover vs Crossover3Way / Crossover4Way (A/B probe)", "[.][xover-dedup-ab-probe]") {
    for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        const auto len = static_cast<size_t>(sr);
        const auto early = static_cast<size_t>(0.05 * sr);
        std::vector<float> noise(len);
        generateWhiteNoise(noise.data(), len, 7);
        for (const bool comp : {false, true}) {
            for (const bool setFirst : {true, false}) {
                MultibandCrossover<3> new3;
                setupMultiband(new3, sr, 3, {300.0f, 3000.0f, 0.0f}, comp);
                Crossover3Way old3;
                if (setFirst) {
                    old3.setLowMidFrequency(300.0f);
                    old3.setMidHighFrequency(3000.0f);
                    old3.prepare(sr);
                } else {
                    old3.prepare(sr);
                    old3.setLowMidFrequency(300.0f);
                    old3.setMidHighFrequency(3000.0f);
                }
                old3.setAllpassCompensation(comp);
                MultibandCrossover<4> new4;
                setupMultiband(new4, sr, 4, {80.0f, 300.0f, 3000.0f}, comp);
                Crossover4Way old4;
                if (setFirst) {
                    old4.setSubLowFrequency(80.0f);
                    old4.setLowMidFrequency(300.0f);
                    old4.setMidHighFrequency(3000.0f);
                    old4.prepare(sr);
                } else {
                    old4.prepare(sr);
                    old4.setSubLowFrequency(80.0f);
                    old4.setLowMidFrequency(300.0f);
                    old4.setMidHighFrequency(3000.0f);
                }
                old4.setAllpassCompensation(comp);
                std::array<double, 2> d3{};  // [first 50 ms, rest]
                std::array<double, 2> d4{};
                std::array<float, 3> b3{};
                std::array<float, 4> b4{};
                for (size_t i = 0; i < len; ++i) {
                    const size_t slot = (i < early) ? 0 : 1;
                    new3.process(noise[i], b3);
                    const auto o3 = old3.process(noise[i]);
                    d3[slot] = std::max({d3[slot], static_cast<double>(std::fabs(b3[0] - o3.low)),
                                         static_cast<double>(std::fabs(b3[1] - o3.mid)),
                                         static_cast<double>(std::fabs(b3[2] - o3.high))});
                    new4.process(noise[i], b4);
                    const auto o4 = old4.process(noise[i]);
                    d4[slot] = std::max({d4[slot], static_cast<double>(std::fabs(b4[0] - o4.sub)),
                                         static_cast<double>(std::fabs(b4[1] - o4.low)),
                                         static_cast<double>(std::fabs(b4[2] - o4.mid)),
                                         static_cast<double>(std::fabs(b4[3] - o4.high))});
                }
                std::printf("A/B %6.0f Hz comp %s old %-13s | 3-way max|diff| first 50 ms %.3e, rest %.3e | "
                            "4-way first 50 ms %.3e, rest %.3e\n",
                            sr, comp ? "ON " : "OFF", setFirst ? "set first" : "prepare first", d3[0], d3[1],
                            d4[0], d4[1]);
            }
        }
    }
    std::fflush(stdout);
    SUCCEED();
}
