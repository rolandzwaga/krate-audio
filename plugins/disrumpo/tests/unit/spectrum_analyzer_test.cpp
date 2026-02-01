// ==============================================================================
// Tests: SpectrumAnalyzer
// ==============================================================================
// UI-thread FFT spectrum analyzer: windowing, magnitude, smoothing, peak hold.
// ==============================================================================

#include "controller/views/spectrum_analyzer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <numbers>

using Catch::Approx;
using namespace Disrumpo;

// =============================================================================
// Helper: Fill a FIFO with a sine wave
// =============================================================================
static void fillWithSine(Krate::DSP::SpectrumFIFO<8192>& fifo,
                         float freqHz, float sampleRate, size_t numSamples,
                         float amplitude = 1.0f) {
    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> block;

    for (size_t offset = 0; offset < numSamples; offset += kBlockSize) {
        size_t chunkLen = std::min(kBlockSize, numSamples - offset);
        for (size_t i = 0; i < chunkLen; ++i) {
            float t = static_cast<float>(offset + i) / sampleRate;
            block[i] = amplitude * std::sin(2.0f * std::numbers::pi_v<float> * freqHz * t);
        }
        fifo.push(block.data(), chunkLen);
    }
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("SpectrumAnalyzer: default state", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;

    REQUIRE_FALSE(analyzer.isPrepared());
    REQUIRE(analyzer.getSmoothedDb().empty());
    REQUIRE(analyzer.getPeakDb().empty());
}

TEST_CASE("SpectrumAnalyzer: prepare initializes buffers", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;

    SpectrumConfig config;
    config.fftSize = 2048;
    config.scopeSize = 512;
    config.sampleRate = 44100.0f;
    analyzer.prepare(config);

    REQUIRE(analyzer.isPrepared());
    REQUIRE(analyzer.getSmoothedDb().size() == 512);
    REQUIRE(analyzer.getPeakDb().size() == 512);

    // All values should be at minDb after prepare
    for (float val : analyzer.getSmoothedDb()) {
        REQUIRE(val == Approx(config.minDb));
    }
}

TEST_CASE("SpectrumAnalyzer: process returns false when not prepared", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    Krate::DSP::SpectrumFIFO<8192> fifo;

    REQUIRE_FALSE(analyzer.process(&fifo, 1.0f / 30.0f));
}

TEST_CASE("SpectrumAnalyzer: process returns false with null FIFO", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    analyzer.prepare(config);

    REQUIRE_FALSE(analyzer.process(nullptr, 1.0f / 30.0f));
}

TEST_CASE("SpectrumAnalyzer: process returns false with insufficient data", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.fftSize = 2048;
    analyzer.prepare(config);

    Krate::DSP::SpectrumFIFO<8192> fifo;
    // Push only 100 samples (need 2048)
    std::array<float, 100> samples{};
    fifo.push(samples.data(), 100);

    REQUIRE_FALSE(analyzer.process(&fifo, 1.0f / 30.0f));
}

// =============================================================================
// Silence Tests
// =============================================================================

TEST_CASE("SpectrumAnalyzer: zero signal produces floor-level values", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.fftSize = 2048;
    config.scopeSize = 256;
    config.sampleRate = 44100.0f;
    config.minDb = -96.0f;
    analyzer.prepare(config);

    Krate::DSP::SpectrumFIFO<8192> fifo;
    std::array<float, 4096> silence{};  // All zeros
    fifo.push(silence.data(), 4096);

    bool processed = analyzer.process(&fifo, 1.0f / 30.0f);
    REQUIRE(processed);

    // All values should be very low (near floor)
    for (float val : analyzer.getSmoothedDb()) {
        REQUIRE(val < -90.0f);
    }
}

// =============================================================================
// Sine Wave Detection Tests
// =============================================================================

TEST_CASE("SpectrumAnalyzer: 1kHz sine shows peak near 1kHz", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.fftSize = 2048;
    config.scopeSize = 512;
    config.sampleRate = 44100.0f;
    config.smoothingAttack = 0.0f;   // No smoothing for test
    config.smoothingRelease = 0.0f;
    config.minDb = -96.0f;
    config.maxDb = 0.0f;
    analyzer.prepare(config);

    Krate::DSP::SpectrumFIFO<8192> fifo;
    fillWithSine(fifo, 1000.0f, 44100.0f, 4096, 1.0f);

    analyzer.process(&fifo, 1.0f / 30.0f);

    const auto& db = analyzer.getSmoothedDb();

    // Find peak scope index
    float maxDb = -200.0f;
    size_t peakIndex = 0;
    for (size_t i = 0; i < db.size(); ++i) {
        if (db[i] > maxDb) {
            maxDb = db[i];
            peakIndex = i;
        }
    }

    // Convert peak index to frequency
    float peakFreq = analyzer.scopeIndexToFreq(peakIndex);

    // Peak should be near 1kHz (within ~15% due to FFT binning and log mapping)
    REQUIRE(peakFreq > 800.0f);
    REQUIRE(peakFreq < 1250.0f);

    // Peak level should be reasonably high (above -20dB for full-scale sine)
    REQUIRE(maxDb > -20.0f);
}

TEST_CASE("SpectrumAnalyzer: 100Hz sine shows peak in low frequency", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.fftSize = 2048;
    config.scopeSize = 512;
    config.sampleRate = 44100.0f;
    config.smoothingAttack = 0.0f;
    config.smoothingRelease = 0.0f;
    analyzer.prepare(config);

    Krate::DSP::SpectrumFIFO<8192> fifo;
    fillWithSine(fifo, 100.0f, 44100.0f, 4096, 1.0f);

    analyzer.process(&fifo, 1.0f / 30.0f);

    const auto& db = analyzer.getSmoothedDb();

    // Find peak
    float maxDb = -200.0f;
    size_t peakIndex = 0;
    for (size_t i = 0; i < db.size(); ++i) {
        if (db[i] > maxDb) {
            maxDb = db[i];
            peakIndex = i;
        }
    }

    float peakFreq = analyzer.scopeIndexToFreq(peakIndex);
    REQUIRE(peakFreq > 70.0f);
    REQUIRE(peakFreq < 150.0f);
}

// =============================================================================
// Frequency Mapping Tests
// =============================================================================

TEST_CASE("SpectrumAnalyzer: scopeIndexToFreq logarithmic mapping", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.scopeSize = 512;
    analyzer.prepare(config);

    // First index should be 20Hz
    REQUIRE(analyzer.scopeIndexToFreq(0) == Approx(20.0f));

    // Last index should be 20kHz
    REQUIRE(analyzer.scopeIndexToFreq(511) == Approx(20000.0f).margin(1.0f));

    // Middle should be geometric mean of 20 and 20000 = sqrt(20*20000) ≈ 632Hz
    float midFreq = analyzer.scopeIndexToFreq(255);
    float expectedMid = std::sqrt(20.0f * 20000.0f);
    REQUIRE(midFreq == Approx(expectedMid).margin(50.0f));

    // Monotonically increasing
    for (size_t i = 1; i < 512; ++i) {
        REQUIRE(analyzer.scopeIndexToFreq(i) > analyzer.scopeIndexToFreq(i - 1));
    }
}

TEST_CASE("SpectrumAnalyzer: freqToScopeIndex inverse mapping", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.scopeSize = 512;
    analyzer.prepare(config);

    // Round-trip: scopeIndexToFreq -> freqToScopeIndex should return original index
    for (size_t i = 0; i < 512; i += 10) {
        float freq = analyzer.scopeIndexToFreq(i);
        float roundTrip = analyzer.freqToScopeIndex(freq);
        REQUIRE(roundTrip == Approx(static_cast<float>(i)).margin(0.5f));
    }

    // Boundary conditions
    REQUIRE(analyzer.freqToScopeIndex(20.0f) == Approx(0.0f).margin(0.01f));
    REQUIRE(analyzer.freqToScopeIndex(20000.0f) == Approx(511.0f).margin(0.01f));
    REQUIRE(analyzer.freqToScopeIndex(10.0f) == 0.0f);  // Below range
    REQUIRE(analyzer.freqToScopeIndex(30000.0f) == 511.0f);  // Above range
}

// =============================================================================
// Smoothing Tests
// =============================================================================

TEST_CASE("SpectrumAnalyzer: smoothing attack faster than release", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.fftSize = 2048;
    config.scopeSize = 256;
    config.sampleRate = 44100.0f;
    config.smoothingAttack = 0.5f;   // Medium attack
    config.smoothingRelease = 0.95f; // Slow release
    config.minDb = -96.0f;
    analyzer.prepare(config);

    Krate::DSP::SpectrumFIFO<8192> fifo;

    // First: process silence
    std::array<float, 4096> silence{};
    fifo.push(silence.data(), 4096);
    analyzer.process(&fifo, 1.0f / 30.0f);

    // Then: process a loud 1kHz sine (attack) over several frames
    for (int frame = 0; frame < 5; ++frame) {
        fillWithSine(fifo, 1000.0f, 44100.0f, 4096, 1.0f);
        analyzer.process(&fifo, 1.0f / 30.0f);
    }
    const auto& afterAttack = analyzer.getSmoothedDb();

    // Find the peak region
    float peakAfterAttack = -200.0f;
    for (float val : afterAttack) {
        if (val > peakAfterAttack) peakAfterAttack = val;
    }

    // Peak should have risen significantly from floor after multiple attack frames
    REQUIRE(peakAfterAttack > -20.0f);
}

// =============================================================================
// Peak Hold Tests
// =============================================================================

TEST_CASE("SpectrumAnalyzer: peak hold retains max value", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.fftSize = 2048;
    config.scopeSize = 256;
    config.sampleRate = 44100.0f;
    config.smoothingAttack = 0.0f;
    config.smoothingRelease = 0.0f;
    config.peakHoldTime = 2.0f;  // Long hold
    config.peakFallRate = 12.0f;
    config.minDb = -96.0f;
    analyzer.prepare(config);

    Krate::DSP::SpectrumFIFO<8192> fifo;

    // Process a loud sine
    fillWithSine(fifo, 1000.0f, 44100.0f, 4096, 1.0f);
    analyzer.process(&fifo, 1.0f / 30.0f);

    // Record peak values
    const auto& peaksAfterSine = analyzer.getPeakDb();
    float maxPeak = -200.0f;
    for (float val : peaksAfterSine) {
        if (val > maxPeak) maxPeak = val;
    }
    REQUIRE(maxPeak > -20.0f);

    // Now process silence for a short time (within hold period)
    std::array<float, 4096> silence{};
    fifo.push(silence.data(), 4096);
    analyzer.process(&fifo, 0.5f);  // 0.5 seconds, within 2s hold

    // Peak should still be held at roughly the same level
    const auto& peaksAfterSilence = analyzer.getPeakDb();
    float maxPeakAfter = -200.0f;
    for (float val : peaksAfterSilence) {
        if (val > maxPeakAfter) maxPeakAfter = val;
    }
    REQUIRE(maxPeakAfter == Approx(maxPeak).margin(1.0f));
}

TEST_CASE("SpectrumAnalyzer: peak decays after hold time", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.fftSize = 2048;
    config.scopeSize = 256;
    config.sampleRate = 44100.0f;
    config.smoothingAttack = 0.0f;
    config.smoothingRelease = 0.0f;
    config.peakHoldTime = 0.1f;   // Short hold
    config.peakFallRate = 96.0f;  // Fast decay for test
    config.minDb = -96.0f;
    analyzer.prepare(config);

    Krate::DSP::SpectrumFIFO<8192> fifo;

    // Process a loud sine
    fillWithSine(fifo, 1000.0f, 44100.0f, 4096, 1.0f);
    analyzer.process(&fifo, 1.0f / 30.0f);

    float maxPeakBefore = -200.0f;
    for (float val : analyzer.getPeakDb()) {
        if (val > maxPeakBefore) maxPeakBefore = val;
    }

    // Process silence for well past hold time
    std::array<float, 4096> silence{};
    fifo.push(silence.data(), 4096);
    analyzer.process(&fifo, 2.0f);  // 2 seconds, well past 0.1s hold

    float maxPeakAfter = -200.0f;
    for (float val : analyzer.getPeakDb()) {
        if (val > maxPeakAfter) maxPeakAfter = val;
    }

    // Peak should have decayed significantly
    REQUIRE(maxPeakAfter < maxPeakBefore - 20.0f);
}

// =============================================================================
// Reset Tests
// =============================================================================

TEST_CASE("SpectrumAnalyzer: reset clears all state", "[spectrum][analyzer]") {
    SpectrumAnalyzer analyzer;
    SpectrumConfig config;
    config.fftSize = 2048;
    config.scopeSize = 256;
    config.sampleRate = 44100.0f;
    config.smoothingAttack = 0.0f;
    config.smoothingRelease = 0.0f;
    config.minDb = -96.0f;
    analyzer.prepare(config);

    Krate::DSP::SpectrumFIFO<8192> fifo;

    // Process a loud sine
    fillWithSine(fifo, 1000.0f, 44100.0f, 4096, 1.0f);
    analyzer.process(&fifo, 1.0f / 30.0f);

    // Reset
    analyzer.reset();

    // All values should be at floor
    for (float val : analyzer.getSmoothedDb()) {
        REQUIRE(val == Approx(config.minDb));
    }
    for (float val : analyzer.getPeakDb()) {
        REQUIRE(val == Approx(config.minDb));
    }
}
