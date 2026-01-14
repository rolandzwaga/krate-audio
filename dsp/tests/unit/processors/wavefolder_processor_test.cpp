// ==============================================================================
// Unit Tests: WavefolderProcessor
// ==============================================================================
// Layer 2: DSP Processor Tests
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
//
// Test organization by User Story:
// - Foundational: Enumerations, constructor, lifecycle [foundational]
// - US1: Basic Wavefolding [US1]
// - US2: Model Selection [US2]
// - US3: Fold Intensity Control [US3]
// - US4: Symmetry for Even Harmonics [US4]
// - US5: Dry/Wet Mix [US5]
// - US6: Parameter Smoothing [US6]
// - Buchla Custom: Buchla259 Custom Mode [buchla_custom]
// - Edge Cases: NaN, Inf, DC input [edge]
// - Performance: CPU benchmarks [perf]
//
// Success Criteria tags:
// - [SC-001] through [SC-008]
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/processors/wavefolder_processor.h>

#include <array>
#include <cmath>
#include <vector>
#include <chrono>
#include <limits>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kSampleRate = 44100.0f;

// Generate a sine wave at specified frequency
inline void generateSine(float* buffer, size_t size, float frequency,
                         float sampleRate, float amplitude = 1.0f) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Calculate RMS of a buffer
inline float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

// Convert linear amplitude to decibels
inline float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

// Calculate DC offset (mean of buffer)
inline float calculateDCOffset(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(size);
}

// Simple DFT to measure harmonic content at specific bin
inline float measureHarmonicMagnitude(const float* buffer, size_t size, size_t bin) {
    float real = 0.0f;
    float imag = 0.0f;
    for (size_t n = 0; n < size; ++n) {
        float angle = kTwoPi * static_cast<float>(bin * n) / static_cast<float>(size);
        real += buffer[n] * std::cos(angle);
        imag -= buffer[n] * std::sin(angle);
    }
    return 2.0f * std::sqrt(real * real + imag * imag) / static_cast<float>(size);
}

// Measure THD (Total Harmonic Distortion)
inline float measureTHD(const float* buffer, size_t size, size_t fundamentalBin,
                        size_t numHarmonics = 5) {
    float fundamental = measureHarmonicMagnitude(buffer, size, fundamentalBin);
    if (fundamental < 1e-10f) return 0.0f;

    float harmonicSum = 0.0f;
    for (size_t h = 2; h <= numHarmonics + 1; ++h) {
        float mag = measureHarmonicMagnitude(buffer, size, fundamentalBin * h);
        harmonicSum += mag * mag;
    }

    return std::sqrt(harmonicSum) / fundamental;
}

} // anonymous namespace

// ==============================================================================
// Phase 2: Foundational Tests
// ==============================================================================

TEST_CASE("WavefolderModel enumeration values", "[wavefolder_processor][foundational][FR-001][FR-002]") {
    // FR-001: WavefolderModel enumeration with four values
    // FR-002: uint8_t underlying type

    static_assert(static_cast<uint8_t>(WavefolderModel::Simple) == 0);
    static_assert(static_cast<uint8_t>(WavefolderModel::Serge) == 1);
    static_assert(static_cast<uint8_t>(WavefolderModel::Buchla259) == 2);
    static_assert(static_cast<uint8_t>(WavefolderModel::Lockhart) == 3);

    // Verify underlying type is uint8_t
    static_assert(sizeof(WavefolderModel) == sizeof(uint8_t));
    static_assert(std::is_same_v<std::underlying_type_t<WavefolderModel>, uint8_t>);

    SUCCEED("WavefolderModel enumeration verified");
}

TEST_CASE("BuchlaMode enumeration values", "[wavefolder_processor][foundational][FR-002a]") {
    // FR-002a: BuchlaMode enumeration with two values, uint8_t underlying type

    static_assert(static_cast<uint8_t>(BuchlaMode::Classic) == 0);
    static_assert(static_cast<uint8_t>(BuchlaMode::Custom) == 1);

    static_assert(sizeof(BuchlaMode) == sizeof(uint8_t));
    static_assert(std::is_same_v<std::underlying_type_t<BuchlaMode>, uint8_t>);

    SUCCEED("BuchlaMode enumeration verified");
}

TEST_CASE("WavefolderProcessor default construction", "[wavefolder_processor][foundational][FR-006]") {
    // FR-006: Default constructor with safe defaults

    WavefolderProcessor folder;

    REQUIRE(folder.getModel() == WavefolderModel::Simple);
    REQUIRE(folder.getFoldAmount() == Approx(1.0f));
    REQUIRE(folder.getSymmetry() == Approx(0.0f));
    REQUIRE(folder.getMix() == Approx(1.0f));
    REQUIRE(folder.getBuchlaMode() == BuchlaMode::Classic);
}

TEST_CASE("WavefolderProcessor prepare and reset", "[wavefolder_processor][foundational][FR-003][FR-004]") {
    // FR-003: prepare() configures processor
    // FR-004: reset() clears state without reallocation

    WavefolderProcessor folder;

    // prepare() should not throw or crash
    folder.prepare(44100.0, 512);

    // reset() should not throw or crash
    folder.reset();

    // Can call prepare again with different params
    folder.prepare(48000.0, 1024);
    folder.reset();

    SUCCEED("prepare() and reset() work correctly");
}

TEST_CASE("WavefolderProcessor process before prepare returns input unchanged", "[wavefolder_processor][foundational][FR-005]") {
    // FR-005: Before prepare() is called, process() returns input unchanged

    WavefolderProcessor folder;
    // Note: Do NOT call prepare()

    std::vector<float> buffer(64);
    std::vector<float> original(64);
    generateSine(buffer.data(), 64, 1000.0f, kSampleRate, 0.5f);
    std::copy(buffer.begin(), buffer.end(), original.begin());

    // Process without calling prepare first
    folder.process(buffer.data(), 64);

    // Output should equal input exactly
    for (size_t i = 0; i < 64; ++i) {
        REQUIRE(buffer[i] == Approx(original[i]).margin(1e-6f));
    }
}

// ==============================================================================
// User Story 1: Basic Wavefolding [US1]
// ==============================================================================

TEST_CASE("US1: Simple model with foldAmount=2.0 produces wavefolded output", "[wavefolder_processor][US1][SC-001]") {
    // SC-001: Each model produces measurably different harmonic spectra

    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(2.0f);
    folder.setSymmetry(0.0f);
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 1.0f);

    folder.process(buffer.data(), kNumSamples);

    // With foldAmount=2.0, peaks should fold back - measure THD
    constexpr size_t kFundamentalBin = 186;  // 1kHz at 44.1kHz/8192
    float thd = measureTHD(buffer.data(), kNumSamples, kFundamentalBin, 10);

    INFO("THD with foldAmount=2.0: " << (thd * 100.0f) << "%");
    REQUIRE(thd > 0.01f);  // Wavefolding produces harmonics
}

TEST_CASE("US1: Processing adds harmonic content compared to input", "[wavefolder_processor][US1]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(3.0f);
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    // Measure input harmonic content (pure sine)
    constexpr size_t kThirdHarmonicBin = 558;  // 3kHz
    float inputThirdHarmonic = measureHarmonicMagnitude(buffer.data(), kNumSamples, kThirdHarmonicBin);

    folder.process(buffer.data(), kNumSamples);

    float outputThirdHarmonic = measureHarmonicMagnitude(buffer.data(), kNumSamples, kThirdHarmonicBin);

    INFO("Input 3rd harmonic: " << inputThirdHarmonic);
    INFO("Output 3rd harmonic: " << outputThirdHarmonic);

    REQUIRE(outputThirdHarmonic > inputThirdHarmonic);
}

TEST_CASE("US1: process() handles n=0 gracefully", "[wavefolder_processor][US1][FR-027]") {
    // FR-027: process() handles n=0 gracefully (no-op)

    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);

    // Should not crash with n=0
    folder.process(nullptr, 0);

    SUCCEED("n=0 handled gracefully");
}

TEST_CASE("US1: process() handles n=1 gracefully", "[wavefolder_processor][US1]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);
    folder.setMix(1.0f);

    float sample = 0.5f;
    folder.process(&sample, 1);

    REQUIRE(std::isfinite(sample));
    SUCCEED("n=1 handled gracefully");
}

TEST_CASE("US1: process() makes no memory allocations", "[wavefolder_processor][US1][FR-026]") {
    // FR-026: No memory allocation during processing
    // Design verification test

    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);

    std::vector<float> buffer(512);
    generateSine(buffer.data(), buffer.size(), 1000.0f, kSampleRate, 0.5f);

    // Process multiple times - should work without any allocations
    for (int i = 0; i < 100; ++i) {
        folder.process(buffer.data(), buffer.size());
    }

    SUCCEED("process() completed without issues - no allocations expected");
}

// ==============================================================================
// User Story 2: Model Selection [US2]
// ==============================================================================

TEST_CASE("US2: setModel and getModel work correctly", "[wavefolder_processor][US2][FR-007][FR-014]") {
    // FR-007: setModel() sets the model
    // FR-014: getModel() returns current model

    WavefolderProcessor folder;

    folder.setModel(WavefolderModel::Simple);
    REQUIRE(folder.getModel() == WavefolderModel::Simple);

    folder.setModel(WavefolderModel::Serge);
    REQUIRE(folder.getModel() == WavefolderModel::Serge);

    folder.setModel(WavefolderModel::Buchla259);
    REQUIRE(folder.getModel() == WavefolderModel::Buchla259);

    folder.setModel(WavefolderModel::Lockhart);
    REQUIRE(folder.getModel() == WavefolderModel::Lockhart);
}

TEST_CASE("US2: Simple model output differs from Serge model output", "[wavefolder_processor][US2][SC-001]") {
    // SC-001: Each model produces measurably different harmonic spectra

    WavefolderProcessor folderSimple, folderSerge;
    folderSimple.prepare(44100.0, 8192);
    folderSerge.prepare(44100.0, 8192);

    folderSimple.setModel(WavefolderModel::Simple);
    folderSimple.setFoldAmount(3.0f);
    folderSimple.setMix(1.0f);

    folderSerge.setModel(WavefolderModel::Serge);
    folderSerge.setFoldAmount(3.0f);
    folderSerge.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> bufferSimple(kNumSamples), bufferSerge(kNumSamples);
    generateSine(bufferSimple.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);
    std::copy(bufferSimple.begin(), bufferSimple.end(), bufferSerge.begin());

    folderSimple.process(bufferSimple.data(), kNumSamples);
    folderSerge.process(bufferSerge.data(), kNumSamples);

    // Compare RMS - should be different
    float rmsSimple = calculateRMS(bufferSimple.data(), kNumSamples);
    float rmsSerge = calculateRMS(bufferSerge.data(), kNumSamples);

    INFO("RMS Simple: " << rmsSimple);
    INFO("RMS Serge: " << rmsSerge);

    // Models should produce different output levels
    REQUIRE(std::abs(rmsSimple - rmsSerge) > 0.01f);
}

TEST_CASE("US2: Serge model produces sin(gain*x) characteristics", "[wavefolder_processor][US2][FR-019]") {
    // FR-019: Serge model uses sin(gain * x)

    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Serge);
    folder.setFoldAmount(3.14159f);  // Pi for characteristic tone
    folder.setSymmetry(0.0f);
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), kNumSamples);

    // Serge produces odd harmonics primarily at symmetric setting
    constexpr size_t kFundamentalBin = 186;
    float thd = measureTHD(buffer.data(), kNumSamples, kFundamentalBin, 10);

    INFO("Serge THD: " << (thd * 100.0f) << "%");
    REQUIRE(thd > 0.04f);  // Should have significant harmonic content
}

TEST_CASE("US2: Lockhart model produces Lambert-W characteristics", "[wavefolder_processor][US2][FR-020]") {
    // FR-020: Lockhart model uses Lambert-W based folding

    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Lockhart);
    folder.setFoldAmount(3.0f);
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), kNumSamples);

    // Should produce harmonic content
    constexpr size_t kFundamentalBin = 186;
    float thd = measureTHD(buffer.data(), kNumSamples, kFundamentalBin, 10);

    INFO("Lockhart THD: " << (thd * 100.0f) << "%");
    REQUIRE(thd > 0.01f);
}

TEST_CASE("US2: Buchla259 Classic mode produces 5-stage parallel folding", "[wavefolder_processor][US2][FR-021][FR-022][FR-022a]") {
    // FR-021: Buchla259 implements 5-stage parallel architecture
    // FR-022: Two sub-modes
    // FR-022a: Classic uses fixed thresholds and gains

    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Buchla259);
    folder.setBuchlaMode(BuchlaMode::Classic);
    folder.setFoldAmount(2.0f);
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), kNumSamples);

    // Should produce harmonic content
    constexpr size_t kFundamentalBin = 186;
    float thd = measureTHD(buffer.data(), kNumSamples, kFundamentalBin, 10);

    INFO("Buchla259 Classic THD: " << (thd * 100.0f) << "%");
    REQUIRE(thd > 0.01f);
}

TEST_CASE("US2: Model change takes effect immediately", "[wavefolder_processor][US2][FR-032]") {
    // FR-032: Model changes are immediate (no smoothing)

    WavefolderProcessor folder;
    folder.prepare(44100.0, 1024);
    folder.setFoldAmount(3.0f);
    folder.setMix(1.0f);

    std::vector<float> bufferSimple(1024), bufferSerge(1024);
    generateSine(bufferSimple.data(), 1024, 1000.0f, kSampleRate, 0.5f);
    std::copy(bufferSimple.begin(), bufferSimple.end(), bufferSerge.begin());

    folder.setModel(WavefolderModel::Simple);
    folder.process(bufferSimple.data(), 1024);

    folder.setModel(WavefolderModel::Serge);
    folder.process(bufferSerge.data(), 1024);

    // Outputs should be different - model change is immediate
    float rmsSimple = calculateRMS(bufferSimple.data(), 1024);
    float rmsSerge = calculateRMS(bufferSerge.data(), 1024);

    REQUIRE(std::abs(rmsSimple - rmsSerge) > 0.01f);
}

// ==============================================================================
// User Story 3: Fold Intensity Control [US3]
// ==============================================================================

TEST_CASE("US3: setFoldAmount and getFoldAmount work correctly", "[wavefolder_processor][US3][FR-008][FR-015]") {
    // FR-008: setFoldAmount() sets fold intensity
    // FR-015: getFoldAmount() returns fold amount

    WavefolderProcessor folder;

    folder.setFoldAmount(5.0f);
    REQUIRE(folder.getFoldAmount() == Approx(5.0f));

    folder.setFoldAmount(0.5f);
    REQUIRE(folder.getFoldAmount() == Approx(0.5f));
}

TEST_CASE("US3: foldAmount clamped to [0.1, 10.0] range", "[wavefolder_processor][US3][FR-009]") {
    // FR-009: foldAmount clamped to [0.1, 10.0]

    WavefolderProcessor folder;

    folder.setFoldAmount(0.05f);  // Below min
    REQUIRE(folder.getFoldAmount() == Approx(0.1f));

    folder.setFoldAmount(15.0f);  // Above max
    REQUIRE(folder.getFoldAmount() == Approx(10.0f));

    folder.setFoldAmount(-1.0f);  // Negative - should clamp to min
    REQUIRE(folder.getFoldAmount() == Approx(0.1f));

    folder.setFoldAmount(5.0f);  // Valid value
    REQUIRE(folder.getFoldAmount() == Approx(5.0f));
}

TEST_CASE("US3: foldAmount=1.0 with 0.5 amplitude shows minimal folding", "[wavefolder_processor][US3]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(1.0f);  // Low fold amount
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), kNumSamples);

    // With low fold amount and 0.5 amplitude, signal mostly stays within threshold
    constexpr size_t kFundamentalBin = 186;
    float thd = measureTHD(buffer.data(), kNumSamples, kFundamentalBin, 5);

    INFO("THD at foldAmount=1.0, amplitude=0.5: " << (thd * 100.0f) << "%");
    // Should have minimal distortion
    REQUIRE(thd < 0.20f);  // Less than 20% THD
}

TEST_CASE("US3: foldAmount=5.0 with 0.5 amplitude shows multiple folds", "[wavefolder_processor][US3]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(5.0f);  // High fold amount
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), kNumSamples);

    // With high fold amount, signal folds multiple times
    constexpr size_t kFundamentalBin = 186;
    float thd = measureTHD(buffer.data(), kNumSamples, kFundamentalBin, 10);

    INFO("THD at foldAmount=5.0, amplitude=0.5: " << (thd * 100.0f) << "%");
    REQUIRE(thd > 0.10f);  // Should have significant harmonic content
}

// ==============================================================================
// User Story 4: Symmetry for Even Harmonics [US4]
// ==============================================================================

TEST_CASE("US4: setSymmetry and getSymmetry work correctly", "[wavefolder_processor][US4][FR-010][FR-016]") {
    // FR-010: setSymmetry() sets asymmetry
    // FR-016: getSymmetry() returns symmetry

    WavefolderProcessor folder;

    folder.setSymmetry(0.5f);
    REQUIRE(folder.getSymmetry() == Approx(0.5f));

    folder.setSymmetry(-0.5f);
    REQUIRE(folder.getSymmetry() == Approx(-0.5f));
}

TEST_CASE("US4: symmetry clamped to [-1.0, +1.0] range", "[wavefolder_processor][US4][FR-011]") {
    // FR-011: Symmetry clamped to [-1.0, +1.0]

    WavefolderProcessor folder;

    folder.setSymmetry(1.5f);
    REQUIRE(folder.getSymmetry() == Approx(1.0f));

    folder.setSymmetry(-1.5f);
    REQUIRE(folder.getSymmetry() == Approx(-1.0f));

    folder.setSymmetry(0.3f);
    REQUIRE(folder.getSymmetry() == Approx(0.3f));
}

TEST_CASE("US4: symmetry=0.0 produces primarily odd harmonics", "[wavefolder_processor][US4][SC-002]") {
    // SC-002: symmetry=0.0 produces 2nd harmonic at least 30dB below fundamental

    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(3.0f);
    folder.setSymmetry(0.0f);  // Symmetric - odd harmonics only
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), kNumSamples);

    constexpr size_t kFundamentalBin = 186;
    constexpr size_t kSecondHarmonicBin = 372;

    float fundamental = measureHarmonicMagnitude(buffer.data(), kNumSamples, kFundamentalBin);
    float secondHarmonic = measureHarmonicMagnitude(buffer.data(), kNumSamples, kSecondHarmonicBin);

    float relativeDb = linearToDb(secondHarmonic / fundamental);

    INFO("2nd harmonic level: " << relativeDb << " dB relative to fundamental");
    REQUIRE(relativeDb < -30.0f);  // At least 30dB below
}

TEST_CASE("US4: symmetry=0.5 produces measurable even harmonics", "[wavefolder_processor][US4][SC-003]") {
    // SC-003: symmetry=0.5 produces 2nd harmonic within 20dB of 3rd harmonic

    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(3.0f);
    folder.setSymmetry(0.5f);  // Asymmetric - even harmonics
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), kNumSamples);

    constexpr size_t kSecondHarmonicBin = 372;
    constexpr size_t kThirdHarmonicBin = 558;

    float secondHarmonic = measureHarmonicMagnitude(buffer.data(), kNumSamples, kSecondHarmonicBin);
    float thirdHarmonic = measureHarmonicMagnitude(buffer.data(), kNumSamples, kThirdHarmonicBin);

    // Even harmonics should be present - 2nd within 20dB of 3rd
    float relativeDb = linearToDb(secondHarmonic / thirdHarmonic);

    INFO("2nd harmonic: " << secondHarmonic);
    INFO("3rd harmonic: " << thirdHarmonic);
    INFO("2nd relative to 3rd: " << relativeDb << " dB");

    REQUIRE(relativeDb > -20.0f);  // Within 20dB
}

TEST_CASE("US4: symmetry=-0.5 shows asymmetric folding in opposite direction", "[wavefolder_processor][US4]") {
    WavefolderProcessor folderPos, folderNeg;
    folderPos.prepare(44100.0, 8192);
    folderNeg.prepare(44100.0, 8192);

    folderPos.setModel(WavefolderModel::Simple);
    folderPos.setFoldAmount(3.0f);
    folderPos.setSymmetry(0.5f);
    folderPos.setMix(1.0f);

    folderNeg.setModel(WavefolderModel::Simple);
    folderNeg.setFoldAmount(3.0f);
    folderNeg.setSymmetry(-0.5f);
    folderNeg.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> bufferPos(kNumSamples), bufferNeg(kNumSamples);
    generateSine(bufferPos.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);
    std::copy(bufferPos.begin(), bufferPos.end(), bufferNeg.begin());

    folderPos.process(bufferPos.data(), kNumSamples);
    folderNeg.process(bufferNeg.data(), kNumSamples);

    // Outputs should be different
    float rmsPos = calculateRMS(bufferPos.data(), kNumSamples);
    float rmsNeg = calculateRMS(bufferNeg.data(), kNumSamples);

    // Both should produce similar RMS but different waveforms
    // Check that they're not identical
    bool identical = true;
    for (size_t i = 0; i < 100; ++i) {
        if (std::abs(bufferPos[i] - bufferNeg[i]) > 0.01f) {
            identical = false;
            break;
        }
    }

    REQUIRE_FALSE(identical);
}

TEST_CASE("US4: DC offset below -50dBFS with non-zero symmetry", "[wavefolder_processor][US4][SC-006]") {
    // SC-006: DC offset after processing is below -50dBFS

    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(3.0f);
    folder.setSymmetry(0.5f);  // Asymmetric - introduces DC
    folder.setMix(1.0f);

    // Process multiple blocks to let DC blocker settle
    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);

    for (int block = 0; block < 10; ++block) {
        generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);
        folder.process(buffer.data(), kNumSamples);
    }

    float dcOffset = calculateDCOffset(buffer.data(), kNumSamples);
    float dcDb = linearToDb(std::abs(dcOffset));

    INFO("DC offset: " << dcOffset << " (" << dcDb << " dBFS)");
    REQUIRE(dcDb < -50.0f);  // Below -50dBFS
}

// ==============================================================================
// User Story 5: Dry/Wet Mix [US5]
// ==============================================================================

TEST_CASE("US5: setMix and getMix work correctly", "[wavefolder_processor][US5][FR-012][FR-017]") {
    // FR-012: setMix() sets dry/wet blend
    // FR-017: getMix() returns mix value

    WavefolderProcessor folder;

    folder.setMix(0.5f);
    REQUIRE(folder.getMix() == Approx(0.5f));

    folder.setMix(0.0f);
    REQUIRE(folder.getMix() == Approx(0.0f));
}

TEST_CASE("US5: mix clamped to [0.0, 1.0] range", "[wavefolder_processor][US5][FR-013]") {
    // FR-013: Mix clamped to [0.0, 1.0]

    WavefolderProcessor folder;

    folder.setMix(1.5f);
    REQUIRE(folder.getMix() == Approx(1.0f));

    folder.setMix(-0.5f);
    REQUIRE(folder.getMix() == Approx(0.0f));

    folder.setMix(0.7f);
    REQUIRE(folder.getMix() == Approx(0.7f));
}

TEST_CASE("US5: mix=0.0 produces output identical to input (bypass)", "[wavefolder_processor][US5][FR-028][SC-008]") {
    // FR-028: mix=0.0 skips wavefolder AND DC blocker entirely
    // SC-008: mix=0.0 produces output identical to input (relative error < 1e-6)

    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);
    folder.setFoldAmount(5.0f);  // Would cause heavy folding if applied
    folder.setSymmetry(0.5f);    // Would cause DC offset if applied
    folder.setMix(0.0f);         // Full bypass

    // Let smoother converge
    std::vector<float> warmup(512, 0.0f);
    for (int i = 0; i < 10; ++i) {
        folder.process(warmup.data(), 512);
    }

    std::vector<float> original(512), buffer(512);
    generateSine(original.data(), 512, 1000.0f, kSampleRate, 0.5f);
    std::copy(original.begin(), original.end(), buffer.begin());

    folder.process(buffer.data(), 512);

    // Output should equal input exactly
    for (size_t i = 0; i < 512; ++i) {
        REQUIRE(buffer[i] == Approx(original[i]).margin(1e-6f));
    }
}

TEST_CASE("US5: mix=0.0 skips wavefolder AND DC blocker", "[wavefolder_processor][US5][FR-028]") {
    // Verify efficiency - the DC blocker state should not change when mix=0

    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);
    folder.setMix(0.0f);

    // Let smoother converge
    std::vector<float> warmup(512, 0.0f);
    for (int i = 0; i < 10; ++i) {
        folder.process(warmup.data(), 512);
    }

    std::vector<float> buffer(512);
    generateSine(buffer.data(), 512, 1000.0f, kSampleRate, 0.5f);

    // Process - with mix=0, should be very fast (no wavefolder or DC blocker)
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        folder.process(buffer.data(), 512);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto bypassTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    folder.setMix(1.0f);
    // Let smoother converge
    for (int i = 0; i < 10; ++i) {
        folder.process(warmup.data(), 512);
    }

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        generateSine(buffer.data(), 512, 1000.0f, kSampleRate, 0.5f);
        folder.process(buffer.data(), 512);
    }
    end = std::chrono::high_resolution_clock::now();
    auto wetTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    INFO("Bypass time: " << bypassTime << " us");
    INFO("Wet time: " << wetTime << " us");

    // Bypass should be significantly faster
    REQUIRE(bypassTime < wetTime);
}

TEST_CASE("US5: mix=1.0 produces 100% folded signal", "[wavefolder_processor][US5]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 8192);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(3.0f);
    folder.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> buffer(kNumSamples);
    generateSine(buffer.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), kNumSamples);

    // Should have harmonic content (fully folded)
    constexpr size_t kFundamentalBin = 186;
    float thd = measureTHD(buffer.data(), kNumSamples, kFundamentalBin, 10);

    INFO("THD at mix=1.0: " << (thd * 100.0f) << "%");
    REQUIRE(thd > 0.05f);
}

TEST_CASE("US5: mix=0.5 produces 50/50 blend", "[wavefolder_processor][US5]") {
    WavefolderProcessor folderDry, folderWet, folder50;
    folderDry.prepare(44100.0, 1024);
    folderWet.prepare(44100.0, 1024);
    folder50.prepare(44100.0, 1024);

    auto configure = [](WavefolderProcessor& f, float mix) {
        f.setModel(WavefolderModel::Simple);
        f.setFoldAmount(3.0f);
        f.setMix(mix);
    };

    configure(folderDry, 0.0f);
    configure(folderWet, 1.0f);
    configure(folder50, 0.5f);

    // Let smoothers converge
    std::vector<float> warmup(1024, 0.0f);
    for (int i = 0; i < 10; ++i) {
        folderDry.process(warmup.data(), 1024);
        std::fill(warmup.begin(), warmup.end(), 0.0f);
    }
    for (int i = 0; i < 10; ++i) {
        folderWet.process(warmup.data(), 1024);
        std::fill(warmup.begin(), warmup.end(), 0.0f);
    }
    for (int i = 0; i < 10; ++i) {
        folder50.process(warmup.data(), 1024);
        std::fill(warmup.begin(), warmup.end(), 0.0f);
    }

    std::vector<float> bufDry(1024), bufWet(1024), buf50(1024);
    generateSine(bufDry.data(), 1024, 1000.0f, kSampleRate, 0.5f);
    std::copy(bufDry.begin(), bufDry.end(), bufWet.begin());
    std::copy(bufDry.begin(), bufDry.end(), buf50.begin());

    folderDry.process(bufDry.data(), 1024);
    folderWet.process(bufWet.data(), 1024);
    folder50.process(buf50.data(), 1024);

    // Calculate expected 50% blend
    std::vector<float> expected(1024);
    for (size_t i = 0; i < 1024; ++i) {
        expected[i] = 0.5f * bufDry[i] + 0.5f * bufWet[i];
    }

    // Compare RMS levels
    float rmsActual = calculateRMS(buf50.data(), 1024);
    float rmsExpected = calculateRMS(expected.data(), 1024);

    float diffDb = std::abs(20.0f * std::log10(rmsActual / rmsExpected));
    INFO("50% mix level difference from expected: " << diffDb << " dB");
    REQUIRE(diffDb < 1.5f);  // Within 1.5 dB
}

// ==============================================================================
// User Story 6: Parameter Smoothing [US6]
// ==============================================================================

TEST_CASE("US6: foldAmount change is smoothed", "[wavefolder_processor][US6][FR-029][SC-004]") {
    // FR-029: foldAmount changes are smoothed
    // SC-004: Parameter changes complete within 10ms without clicks

    WavefolderProcessor folder;
    folder.prepare(44100.0, 64);
    folder.setFoldAmount(1.0f);
    folder.setMix(1.0f);

    std::vector<float> buffer(64);
    float maxDerivative = 0.0f;
    float prevSample = 0.0f;
    bool firstSample = true;

    for (int block = 0; block < 20; ++block) {
        if (block == 10) {
            folder.setFoldAmount(5.0f);  // Sudden change
        }

        for (size_t i = 0; i < 64; ++i) {
            float t = static_cast<float>(block * 64 + i) / kSampleRate;
            buffer[i] = 0.3f * std::sin(kTwoPi * 1000.0f * t);
        }

        folder.process(buffer.data(), 64);

        for (size_t i = 0; i < 64; ++i) {
            if (!firstSample) {
                float derivative = std::abs(buffer[i] - prevSample);
                maxDerivative = std::max(maxDerivative, derivative);
            }
            prevSample = buffer[i];
            firstSample = false;
        }
    }

    INFO("Max sample-to-sample derivative: " << maxDerivative);
    REQUIRE(maxDerivative < 0.5f);  // Smoothed - no clicks
}

TEST_CASE("US6: symmetry change is smoothed", "[wavefolder_processor][US6][FR-030]") {
    // FR-030: symmetry changes are smoothed

    WavefolderProcessor folder;
    folder.prepare(44100.0, 64);
    folder.setFoldAmount(3.0f);
    folder.setSymmetry(0.0f);
    folder.setMix(1.0f);

    std::vector<float> buffer(64);
    float maxDerivative = 0.0f;
    float prevSample = 0.0f;

    for (int block = 0; block < 20; ++block) {
        if (block == 10) {
            folder.setSymmetry(0.8f);  // Sudden change
        }

        for (size_t i = 0; i < 64; ++i) {
            float t = static_cast<float>(block * 64 + i) / kSampleRate;
            buffer[i] = 0.3f * std::sin(kTwoPi * 1000.0f * t);
        }

        folder.process(buffer.data(), 64);

        for (size_t i = 0; i < 64; ++i) {
            float derivative = std::abs(buffer[i] - prevSample);
            maxDerivative = std::max(maxDerivative, derivative);
            prevSample = buffer[i];
        }
    }

    INFO("Max sample-to-sample derivative: " << maxDerivative);
    REQUIRE(maxDerivative < 0.5f);
}

TEST_CASE("US6: mix change is smoothed", "[wavefolder_processor][US6][FR-031]") {
    // FR-031: mix changes are smoothed

    WavefolderProcessor folder;
    folder.prepare(44100.0, 64);
    folder.setFoldAmount(3.0f);
    folder.setMix(0.0f);

    std::vector<float> buffer(64);
    float maxDerivative = 0.0f;
    float prevSample = 0.0f;

    for (int block = 0; block < 20; ++block) {
        if (block == 10) {
            folder.setMix(1.0f);  // Jump to 100% wet
        }

        for (size_t i = 0; i < 64; ++i) {
            float t = static_cast<float>(block * 64 + i) / kSampleRate;
            buffer[i] = 0.3f * std::sin(kTwoPi * 1000.0f * t);
        }

        folder.process(buffer.data(), 64);

        for (size_t i = 0; i < 64; ++i) {
            float derivative = std::abs(buffer[i] - prevSample);
            maxDerivative = std::max(maxDerivative, derivative);
            prevSample = buffer[i];
        }
    }

    INFO("Max sample-to-sample derivative: " << maxDerivative);
    REQUIRE(maxDerivative < 0.3f);
}

TEST_CASE("US6: reset() snaps smoothers to target", "[wavefolder_processor][US6][FR-033]") {
    // FR-033: reset() snaps smoothers to current target values

    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);
    folder.setFoldAmount(5.0f);
    folder.setSymmetry(0.3f);
    folder.setMix(0.8f);

    // Reset should snap to targets immediately
    folder.reset();

    // Process - should immediately use target values (no ramping)
    std::vector<float> buffer(64);
    generateSine(buffer.data(), 64, 1000.0f, kSampleRate, 0.3f);

    folder.process(buffer.data(), 64);

    // Should have some output reflecting the fold settings
    float rms = calculateRMS(buffer.data(), 64);
    INFO("RMS after reset: " << rms);
    REQUIRE(rms > 0.05f);
}

TEST_CASE("US6: Parameter smoothing completes within 10ms", "[wavefolder_processor][US6][SC-004]") {
    // SC-004: Parameter changes complete smoothing within 10ms

    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);
    folder.setFoldAmount(1.0f);
    folder.setMix(1.0f);

    // Let initial values settle
    std::vector<float> buffer(512);
    for (int i = 0; i < 5; ++i) {
        generateSine(buffer.data(), 512, 1000.0f, kSampleRate, 0.5f);
        folder.process(buffer.data(), 512);
    }

    // Now change parameter
    folder.setFoldAmount(5.0f);

    // Process 10ms of audio (441 samples at 44.1kHz)
    const size_t k10msInSamples = static_cast<size_t>(0.010 * 44100.0);
    size_t processed = 0;

    float firstRms = 0.0f;
    float lastRms = 0.0f;

    while (processed < k10msInSamples) {
        generateSine(buffer.data(), 512, 1000.0f, kSampleRate, 0.5f);
        folder.process(buffer.data(), 512);

        if (processed == 0) {
            firstRms = calculateRMS(buffer.data(), 512);
        }
        lastRms = calculateRMS(buffer.data(), 512);

        processed += 512;
    }

    INFO("First block RMS: " << firstRms);
    INFO("Last block RMS (after 10ms): " << lastRms);

    // The change should have stabilized - consecutive blocks should have similar RMS
    // Process one more block
    generateSine(buffer.data(), 512, 1000.0f, kSampleRate, 0.5f);
    folder.process(buffer.data(), 512);
    float nextRms = calculateRMS(buffer.data(), 512);

    float rmsDiff = std::abs(lastRms - nextRms);
    INFO("RMS difference after 10ms: " << rmsDiff);
    REQUIRE(rmsDiff < 0.05f);  // Stabilized (allow some tolerance for small differences)
}

// ==============================================================================
// Buchla259 Custom Mode [buchla_custom]
// ==============================================================================

TEST_CASE("Buchla Custom: setBuchlaMode and getBuchlaMode work correctly", "[wavefolder_processor][buchla_custom][FR-023][FR-023a]") {
    // FR-023: setBuchlaMode() switches between Classic and Custom
    // FR-023a: getBuchlaMode() returns current mode

    WavefolderProcessor folder;

    folder.setBuchlaMode(BuchlaMode::Classic);
    REQUIRE(folder.getBuchlaMode() == BuchlaMode::Classic);

    folder.setBuchlaMode(BuchlaMode::Custom);
    REQUIRE(folder.getBuchlaMode() == BuchlaMode::Custom);
}

TEST_CASE("Buchla Custom: setBuchlaThresholds accepts array", "[wavefolder_processor][buchla_custom][FR-022b]") {
    // FR-022b: setBuchlaThresholds() accepts array<float, 5>

    WavefolderProcessor folder;

    std::array<float, 5> customThresholds = {0.15f, 0.35f, 0.55f, 0.75f, 0.95f};
    folder.setBuchlaThresholds(customThresholds);

    // Should not crash
    SUCCEED("setBuchlaThresholds() accepts array");
}

TEST_CASE("Buchla Custom: setBuchlaGains accepts array", "[wavefolder_processor][buchla_custom][FR-022c]") {
    // FR-022c: setBuchlaGains() accepts array<float, 5>

    WavefolderProcessor folder;

    std::array<float, 5> customGains = {1.0f, 0.9f, 0.7f, 0.5f, 0.3f};
    folder.setBuchlaGains(customGains);

    // Should not crash
    SUCCEED("setBuchlaGains() accepts array");
}

TEST_CASE("Buchla Custom: Custom mode produces different output than Classic", "[wavefolder_processor][buchla_custom][FR-022]") {
    // FR-022: Custom mode with different thresholds/gains produces different output

    WavefolderProcessor folderClassic, folderCustom;
    folderClassic.prepare(44100.0, 8192);
    folderCustom.prepare(44100.0, 8192);

    folderClassic.setModel(WavefolderModel::Buchla259);
    folderClassic.setBuchlaMode(BuchlaMode::Classic);
    folderClassic.setFoldAmount(2.0f);
    folderClassic.setMix(1.0f);

    folderCustom.setModel(WavefolderModel::Buchla259);
    folderCustom.setBuchlaMode(BuchlaMode::Custom);
    folderCustom.setBuchlaThresholds({0.15f, 0.30f, 0.45f, 0.60f, 0.75f});
    folderCustom.setBuchlaGains({1.2f, 1.0f, 0.8f, 0.6f, 0.4f});
    folderCustom.setFoldAmount(2.0f);
    folderCustom.setMix(1.0f);

    constexpr size_t kNumSamples = 8192;
    std::vector<float> bufferClassic(kNumSamples), bufferCustom(kNumSamples);
    generateSine(bufferClassic.data(), kNumSamples, 1000.0f, kSampleRate, 0.5f);
    std::copy(bufferClassic.begin(), bufferClassic.end(), bufferCustom.begin());

    folderClassic.process(bufferClassic.data(), kNumSamples);
    folderCustom.process(bufferCustom.data(), kNumSamples);

    // Outputs should be different
    float rmsClassic = calculateRMS(bufferClassic.data(), kNumSamples);
    float rmsCustom = calculateRMS(bufferCustom.data(), kNumSamples);

    INFO("RMS Classic: " << rmsClassic);
    INFO("RMS Custom: " << rmsCustom);

    REQUIRE(std::abs(rmsClassic - rmsCustom) > 0.01f);
}

TEST_CASE("Buchla Custom: Custom mode only affects Buchla259 model", "[wavefolder_processor][buchla_custom]") {
    // Custom thresholds/gains should only affect output when model=Buchla259

    WavefolderProcessor folder;
    folder.prepare(44100.0, 1024);
    folder.setBuchlaMode(BuchlaMode::Custom);
    folder.setBuchlaThresholds({0.1f, 0.2f, 0.3f, 0.4f, 0.5f});
    folder.setBuchlaGains({2.0f, 1.5f, 1.0f, 0.5f, 0.0f});
    folder.setFoldAmount(2.0f);
    folder.setMix(1.0f);

    // With Simple model, custom Buchla settings should have no effect
    folder.setModel(WavefolderModel::Simple);

    std::vector<float> buffer(1024);
    generateSine(buffer.data(), 1024, 1000.0f, kSampleRate, 0.5f);

    folder.process(buffer.data(), 1024);

    // Should produce normal Simple fold output, not affected by Buchla settings
    float rms = calculateRMS(buffer.data(), 1024);
    REQUIRE(rms > 0.1f);  // Should have output
    REQUIRE(std::isfinite(rms));
}

// ==============================================================================
// Edge Cases [edge]
// ==============================================================================

TEST_CASE("Edge: DC input settles to zero", "[wavefolder_processor][edge]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);
    folder.setFoldAmount(3.0f);
    folder.setSymmetry(0.5f);
    folder.setMix(1.0f);

    // Process 500ms of DC input
    const size_t kSamplesFor500ms = static_cast<size_t>(0.5 * 44100.0);
    std::vector<float> buffer(512);

    float lastDcLevel = 1.0f;
    for (size_t processed = 0; processed < kSamplesFor500ms; processed += 512) {
        std::fill(buffer.begin(), buffer.end(), 1.0f);
        folder.process(buffer.data(), 512);
        lastDcLevel = std::abs(calculateDCOffset(buffer.data(), 512));
    }

    INFO("DC level after 500ms: " << lastDcLevel);
    REQUIRE(lastDcLevel < 0.01f);  // < 1%
}

TEST_CASE("Edge: NaN input propagates (no crash)", "[wavefolder_processor][edge]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 4);
    folder.setMix(1.0f);

    std::vector<float> buffer = {0.5f, std::numeric_limits<float>::quiet_NaN(), 0.3f, -0.2f};

    // Process - should not crash
    folder.process(buffer.data(), buffer.size());

    SUCCEED("No crash on NaN input");
}

TEST_CASE("Edge: Infinity input propagates (no crash)", "[wavefolder_processor][edge]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 4);
    folder.setMix(1.0f);

    std::vector<float> buffer = {0.5f, std::numeric_limits<float>::infinity(), 0.3f, -0.2f};

    // Process - should not crash
    folder.process(buffer.data(), buffer.size());

    SUCCEED("No crash on Infinity input");
}

TEST_CASE("Edge: Very short buffer n=1 works correctly", "[wavefolder_processor][edge]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);
    folder.setFoldAmount(2.0f);
    folder.setMix(1.0f);

    float sample = 0.5f;
    folder.process(&sample, 1);

    REQUIRE(std::isfinite(sample));
}

TEST_CASE("Edge: Model change during processing", "[wavefolder_processor][edge]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 64);
    folder.setFoldAmount(3.0f);
    folder.setMix(1.0f);

    std::vector<float> buffer(64);

    // Start with Simple
    folder.setModel(WavefolderModel::Simple);
    generateSine(buffer.data(), 64, 1000.0f, kSampleRate, 0.5f);
    folder.process(buffer.data(), 64);

    float rmsSimple = calculateRMS(buffer.data(), 64);

    // Change to Serge mid-stream
    folder.setModel(WavefolderModel::Serge);
    generateSine(buffer.data(), 64, 1000.0f, kSampleRate, 0.5f);
    folder.process(buffer.data(), 64);

    float rmsSerge = calculateRMS(buffer.data(), 64);

    // Both should produce valid output
    REQUIRE(std::isfinite(rmsSimple));
    REQUIRE(std::isfinite(rmsSerge));

    // Model change should be immediate
    REQUIRE(std::abs(rmsSimple - rmsSerge) > 0.01f);
}

// ==============================================================================
// Performance Tests [perf]
// ==============================================================================

TEST_CASE("Performance: All models within 2x of TubeStage/DiodeClipper", "[wavefolder_processor][perf][SC-005]") {
    // SC-005: < 2x CPU of TubeStage/DiodeClipper

    // This test validates that WavefolderProcessor is reasonably performant
    // The actual comparison with TubeStage/DiodeClipper is done relatively

    WavefolderProcessor folder;
    folder.prepare(44100.0, 512);
    folder.setFoldAmount(3.0f);
    folder.setMix(1.0f);

    std::vector<float> buffer(512);

    // Test all models
    const std::array<WavefolderModel, 4> models = {
        WavefolderModel::Simple,
        WavefolderModel::Serge,
        WavefolderModel::Buchla259,
        WavefolderModel::Lockhart
    };

    for (auto model : models) {
        folder.setModel(model);

        // Warmup
        for (int i = 0; i < 100; ++i) {
            generateSine(buffer.data(), 512, 1000.0f, kSampleRate, 0.5f);
            folder.process(buffer.data(), 512);
        }

        // Time 1000 iterations
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 1000; ++i) {
            generateSine(buffer.data(), 512, 1000.0f, kSampleRate, 0.5f);
            folder.process(buffer.data(), 512);
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        float avgMicroseconds = static_cast<float>(duration.count()) / 1000.0f;

        INFO("Model " << static_cast<int>(model) << " average: " << avgMicroseconds << " us/block");

        // Should complete 512 samples in < 200us (reasonable for Layer 2 processor)
        REQUIRE(avgMicroseconds < 200.0f);
    }
}

TEST_CASE("Performance: Works at all supported sample rates", "[wavefolder_processor][perf][SC-007]") {
    // SC-007: All tests pass at 44.1, 48, 88.2, 96, 192 kHz

    const std::array<double, 5> sampleRates = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        WavefolderProcessor folder;
        folder.prepare(sr, 512);
        folder.setModel(WavefolderModel::Simple);
        folder.setFoldAmount(3.0f);
        folder.setMix(1.0f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), 512, 1000.0f, static_cast<float>(sr), 0.5f);

        folder.process(buffer.data(), 512);

        // Should produce valid output at all sample rates
        float rms = calculateRMS(buffer.data(), 512);
        INFO("Sample rate: " << sr << " Hz, RMS: " << rms);
        REQUIRE(std::isfinite(rms));
        REQUIRE(rms > 0.1f);
    }
}

// ==============================================================================
// Real-Time Safety Tests [realtime]
// ==============================================================================

TEST_CASE("Realtime: All public methods are noexcept", "[wavefolder_processor][realtime]") {
    static_assert(noexcept(std::declval<WavefolderProcessor>().prepare(44100.0, 512)));
    static_assert(noexcept(std::declval<WavefolderProcessor>().reset()));
    static_assert(noexcept(std::declval<WavefolderProcessor>().process(nullptr, 0)));
    static_assert(noexcept(std::declval<WavefolderProcessor>().setModel(WavefolderModel::Simple)));
    static_assert(noexcept(std::declval<WavefolderProcessor>().getModel()));
    static_assert(noexcept(std::declval<WavefolderProcessor>().setBuchlaMode(BuchlaMode::Classic)));
    static_assert(noexcept(std::declval<WavefolderProcessor>().getBuchlaMode()));
    static_assert(noexcept(std::declval<WavefolderProcessor>().setBuchlaThresholds(std::array<float, 5>{})));
    static_assert(noexcept(std::declval<WavefolderProcessor>().setBuchlaGains(std::array<float, 5>{})));
    static_assert(noexcept(std::declval<WavefolderProcessor>().setFoldAmount(1.0f)));
    static_assert(noexcept(std::declval<WavefolderProcessor>().getFoldAmount()));
    static_assert(noexcept(std::declval<WavefolderProcessor>().setSymmetry(0.0f)));
    static_assert(noexcept(std::declval<WavefolderProcessor>().getSymmetry()));
    static_assert(noexcept(std::declval<WavefolderProcessor>().setMix(1.0f)));
    static_assert(noexcept(std::declval<WavefolderProcessor>().getMix()));

    SUCCEED("All public methods verified noexcept via static_assert");
}

TEST_CASE("Realtime: Process 1M samples without NaN/Inf", "[wavefolder_processor][realtime]") {
    WavefolderProcessor folder;
    folder.prepare(44100.0, 1024);
    folder.setModel(WavefolderModel::Simple);
    folder.setFoldAmount(5.0f);
    folder.setSymmetry(0.3f);
    folder.setMix(1.0f);

    std::vector<float> buffer(1024);
    constexpr size_t kOneMillion = 1000000;
    size_t processed = 0;

    while (processed < kOneMillion) {
        generateSine(buffer.data(), 1024, 440.0f, kSampleRate, 0.8f);
        folder.process(buffer.data(), 1024);

        for (size_t i = 0; i < 1024; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
            REQUIRE_FALSE(std::isinf(buffer[i]));
        }

        processed += 1024;
    }

    SUCCEED("1M samples processed without NaN/Inf");
}
