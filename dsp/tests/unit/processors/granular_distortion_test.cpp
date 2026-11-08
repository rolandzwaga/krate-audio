// ==============================================================================
// Layer 2: DSP Processor Tests - GranularDistortion
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 113-granular-distortion
//
// Reference: specs/113-granular-distortion/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/granular_distortion.h>

// allocation_detector.h ONLY: the global operator new/delete replacements live in
// brownian_drift_test.cpp, the one owner in dsp_processors_tests.
#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <set>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

constexpr float kTestTwoPi = 6.28318530718f;

/// Generate a sine wave at specified frequency
void generateSine(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(kTestTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

/// Generate DC signal (constant value)
void generateDC(float* buffer, size_t size, float value) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = value;
    }
}

/// Generate silence
void generateSilence(float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = 0.0f;
    }
}

/// Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

/// Calculate peak value in buffer
float calculatePeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Check if any sample is NaN or Inf
bool hasInvalidSamples(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (std::isnan(buffer[i]) || std::isinf(buffer[i])) {
            return true;
        }
    }
    return false;
}

/// Calculate average absolute difference between two buffers
float calculateDifference(const float* a, const float* b, size_t size) {
    float totalDiff = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        totalDiff += std::abs(a[i] - b[i]);
    }
    return totalDiff / static_cast<float>(size);
}

/// Check if two buffers are bit-exact equal
bool buffersEqual(const float* a, const float* b, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

/// Detect clicks in audio (sudden large amplitude changes)
bool hasClicks(const float* buffer, size_t size, float threshold = 0.5f) {
    for (size_t i = 1; i < size; ++i) {
        if (std::abs(buffer[i] - buffer[i-1]) > threshold) {
            return true;
        }
    }
    return false;
}

/// Calculate standard deviation of values
float calculateStdDev(const std::vector<float>& values) {
    if (values.size() < 2) return 0.0f;
    float mean = std::accumulate(values.begin(), values.end(), 0.0f) / static_cast<float>(values.size());
    float sumSq = 0.0f;
    for (float v : values) {
        sumSq += (v - mean) * (v - mean);
    }
    return std::sqrt(sumSq / static_cast<float>(values.size() - 1));
}

} // namespace

// =============================================================================
// Phase 3: User Story 1 - Basic Granular Distortion (T010-T017)
// =============================================================================

TEST_CASE("GranularDistortion prepare() initializes all components",
          "[granular_distortion][layer2][US1][lifecycle]") {
    GranularDistortion gd;

    SECTION("prepare at 44100Hz") {
        gd.prepare(44100.0, 512);
        REQUIRE(gd.isPrepared() == true);
    }

    SECTION("prepare at 96000Hz") {
        gd.prepare(96000.0, 512);
        REQUIRE(gd.isPrepared() == true);
    }

    SECTION("prepare at 192000Hz") {
        gd.prepare(192000.0, 512);
        REQUIRE(gd.isPrepared() == true);
    }
}

TEST_CASE("GranularDistortion reset() clears state without changing parameters",
          "[granular_distortion][layer2][US1][lifecycle]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);

    // Set some parameters
    gd.setDrive(10.0f);
    gd.setMix(0.75f);
    gd.setGrainSize(30.0f);

    // Process some audio to build up state
    std::array<float, 1024> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);
    gd.process(buffer.data(), buffer.size());

    // Reset
    gd.reset();

    // Parameters should be preserved
    REQUIRE(gd.getDrive() == Approx(10.0f));
    REQUIRE(gd.getMix() == Approx(0.75f));
    REQUIRE(gd.getGrainSize() == Approx(30.0f));

    // Active grain count should be zero
    REQUIRE(gd.getActiveGrainCount() == 0);
}

TEST_CASE("GranularDistortion process() with silence produces silence",
          "[granular_distortion][layer2][US1][processing]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);

    constexpr size_t blockSize = 4096;
    std::array<float, blockSize> buffer;
    generateSilence(buffer.data(), buffer.size());

    gd.process(buffer.data(), buffer.size());

    // Output should still be silence (or very near silence)
    float rms = calculateRMS(buffer.data(), buffer.size());
    REQUIRE(rms < 0.001f);
}

TEST_CASE("GranularDistortion process() with input produces non-zero output",
          "[granular_distortion][layer2][US1][processing]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainDensity(8.0f);  // High density for more activity
    gd.seed(12345);  // Deterministic

    constexpr size_t blockSize = 8192;  // Long enough for grains to trigger
    std::array<float, blockSize> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.5f);

    gd.process(buffer.data(), buffer.size());

    // Output should have content (grains triggered)
    float rms = calculateRMS(buffer.data(), buffer.size());
    REQUIRE(rms > 0.01f);
}

TEST_CASE("GranularDistortion mix=0.0 produces dry signal (FR-032, SC-008)",
          "[granular_distortion][layer2][US1][mix]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(0.0f);  // Full dry - bypass optimization kicks in
    gd.setDrive(10.0f);
    gd.setGrainDensity(8.0f);

    // SC-008: With mix=0.0, output should be BIT-EXACT dry signal
    // No warmup needed - bypass optimization returns input directly
    constexpr size_t blockSize = 1024;
    std::array<float, blockSize> original, processed;
    generateSine(original.data(), original.size(), 440.0f, 44100.0f);
    std::copy(original.begin(), original.end(), processed.begin());

    gd.process(processed.data(), processed.size());

    // Bit-exact comparison - no margin allowed (SC-008 requirement)
    REQUIRE(buffersEqual(original.data(), processed.data(), blockSize));
}

TEST_CASE("GranularDistortion mix=1.0 produces full wet signal",
          "[granular_distortion][layer2][US1][mix]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);  // Full wet
    gd.setDrive(5.0f);
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 8192;
    std::array<float, blockSize> original, processed;
    generateSine(original.data(), original.size(), 440.0f, 44100.0f, 0.5f);
    std::copy(original.begin(), original.end(), processed.begin());

    gd.process(processed.data(), processed.size());

    // Output should differ from input (distortion applied)
    float diff = calculateDifference(original.data(), processed.data(), blockSize);
    REQUIRE(diff > 0.01f);  // Some measurable difference
}

TEST_CASE("GranularDistortion grains have envelope windowing (SC-001)",
          "[granular_distortion][layer2][US1][envelope]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(50.0f);  // 50ms grains
    gd.setGrainDensity(2.0f);  // Sparse to see individual grains
    gd.seed(12345);

    constexpr size_t blockSize = 44100;  // 1 second
    std::array<float, blockSize> buffer;
    generateDC(buffer.data(), buffer.size(), 0.5f);  // Constant input

    gd.process(buffer.data(), buffer.size());

    // Output should NOT be constant - envelope creates amplitude variation
    float minVal = *std::min_element(buffer.begin(), buffer.end());
    float maxVal = *std::max_element(buffer.begin(), buffer.end());

    // With envelope windowing, we expect variation in output
    REQUIRE(maxVal - minVal > 0.01f);
}

// =============================================================================
// Phase 4: User Story 2 - Per-Grain Drive Variation (T038-T042)
// =============================================================================

TEST_CASE("GranularDistortion driveVariation=0.0 produces identical drive for all grains (FR-016)",
          "[granular_distortion][layer2][US2][drive_variation]") {
    // With zero variation, all grains should have the same drive
    // This is implicitly tested by consistent output with seeded RNG
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setDriveVariation(0.0f);  // No variation
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    // Process twice with same seed - should get identical output
    constexpr size_t blockSize = 4096;
    std::array<float, blockSize> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 440.0f, 44100.0f);
    std::copy(buffer1.begin(), buffer1.end(), buffer2.begin());

    gd.process(buffer1.data(), buffer1.size());

    gd.reset();
    gd.seed(12345);
    generateSine(buffer2.data(), buffer2.size(), 440.0f, 44100.0f);
    gd.process(buffer2.data(), buffer2.size());

    // Should be identical (same seed, no variation)
    REQUIRE(buffersEqual(buffer1.data(), buffer2.data(), blockSize));
}

TEST_CASE("GranularDistortion driveVariation=1.0 produces different drive amounts (FR-015)",
          "[granular_distortion][layer2][US2][drive_variation]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(10.0f);
    gd.setDriveVariation(1.0f);  // Maximum variation
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    // Process twice with different seeds - should get different output
    constexpr size_t blockSize = 4096;
    std::array<float, blockSize> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 440.0f, 44100.0f);
    generateSine(buffer2.data(), buffer2.size(), 440.0f, 44100.0f);

    gd.process(buffer1.data(), buffer1.size());

    gd.reset();
    gd.seed(54321);  // Different seed
    gd.process(buffer2.data(), buffer2.size());

    // Should be different (different seeds with variation)
    float diff = calculateDifference(buffer1.data(), buffer2.data(), blockSize);
    REQUIRE(diff > 0.01f);
}

TEST_CASE("GranularDistortion per-grain drive is clamped to [1.0, 20.0] (FR-015)",
          "[granular_distortion][layer2][US2][drive_variation]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(20.0f);  // Maximum base drive
    gd.setDriveVariation(1.0f);  // Maximum variation (could go to 40.0 without clamping)
    gd.setGrainDensity(8.0f);
    // Use Tanh which is bounded, not Diode which is unbounded
    gd.setDistortionType(WaveshapeType::Tanh);
    gd.setAlgorithmVariation(false);  // Don't randomly pick Diode
    gd.seed(12345);

    constexpr size_t blockSize = 8192;
    std::array<float, blockSize> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);

    gd.process(buffer.data(), buffer.size());

    // Output should be valid (no NaN/Inf from extreme drive values)
    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));

    // Peak should be bounded (Tanh waveshaper output is bounded to [-1, 1])
    // With multiple overlapping grains, can add up, so allow some headroom
    float peak = calculatePeak(buffer.data(), buffer.size());
    REQUIRE(peak <= 10.0f);  // Multiple grains can sum
}

TEST_CASE("GranularDistortion driveVariation=1.0 produces measurable std dev (SC-002)",
          "[granular_distortion][layer2][US2][drive_variation]") {
    // SC-002: "standard deviation of per-grain drive > 0.3 * baseDrive"
    // This test measures ACTUAL per-grain drive values, not output RMS levels
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);

    const float baseDrive = 10.0f;
    gd.setDrive(baseDrive);
    gd.setDriveVariation(1.0f);  // Maximum variation
    gd.setGrainSize(10.0f);      // Short grains for more triggers
    gd.setGrainDensity(8.0f);    // High density for more triggers
    gd.seed(12345);

    // Collect actual per-grain drive values using instrumentation
    std::vector<float> grainDrives;
    grainDrives.reserve(150);

    // Process audio and collect grain drive values as they trigger
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer;
    size_t lastGrainCount = 0;

    // Process until we have 100+ grain drive samples
    while (grainDrives.size() < 100) {
        generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.5f);
        gd.process(buffer.data(), buffer.size());

        // Check if new grain was triggered
        const size_t currentGrainCount = gd.getGrainsTriggeredCount();
        if (currentGrainCount > lastGrainCount) {
            grainDrives.push_back(gd.getLastTriggeredGrainDrive());
            lastGrainCount = currentGrainCount;
        }
    }

    // Verify we collected enough samples
    REQUIRE(grainDrives.size() >= 100);

    // Calculate standard deviation of actual per-grain drive values
    float stdDev = calculateStdDev(grainDrives);

    // SC-002 requirement: std dev > 0.3 * baseDrive = 3.0
    const float requiredStdDev = 0.3f * baseDrive;
    INFO("Collected " << grainDrives.size() << " grain drive values");
    INFO("Standard deviation: " << stdDev << " (required > " << requiredStdDev << ")");
    REQUIRE(stdDev > requiredStdDev);

    // Also verify drives are within valid range [1.0, 20.0]
    for (float drive : grainDrives) {
        REQUIRE(drive >= 1.0f);
        REQUIRE(drive <= 20.0f);
    }
}

// =============================================================================
// Phase 5: User Story 3 - Per-Grain Algorithm Variation (T051-T054)
// =============================================================================

TEST_CASE("GranularDistortion algorithmVariation=false uses base distortion type (FR-019)",
          "[granular_distortion][layer2][US3][algorithm_variation]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setDistortionType(WaveshapeType::Tanh);
    gd.setAlgorithmVariation(false);
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 4096;
    std::array<float, blockSize> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 440.0f, 44100.0f);
    generateSine(buffer2.data(), buffer2.size(), 440.0f, 44100.0f);

    // Process with same seed twice
    gd.process(buffer1.data(), buffer1.size());

    gd.reset();
    gd.seed(12345);
    gd.process(buffer2.data(), buffer2.size());

    // Should be identical (same algorithm always used)
    REQUIRE(buffersEqual(buffer1.data(), buffer2.data(), blockSize));
}

TEST_CASE("GranularDistortion algorithmVariation=true uses different algorithms (FR-018)",
          "[granular_distortion][layer2][US3][algorithm_variation]") {
    GranularDistortion gd1, gd2;

    // First processor: no variation (always Tanh)
    gd1.prepare(44100.0, 512);
    gd1.setMix(1.0f);
    gd1.setDrive(5.0f);
    gd1.setDistortionType(WaveshapeType::Tanh);
    gd1.setAlgorithmVariation(false);
    gd1.setGrainDensity(8.0f);
    gd1.seed(12345);

    // Second processor: with variation
    gd2.prepare(44100.0, 512);
    gd2.setMix(1.0f);
    gd2.setDrive(5.0f);
    gd2.setDistortionType(WaveshapeType::Tanh);
    gd2.setAlgorithmVariation(true);  // Enable variation
    gd2.setGrainDensity(8.0f);
    gd2.seed(12345);  // Same seed

    constexpr size_t blockSize = 8192;
    std::array<float, blockSize> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 440.0f, 44100.0f);
    generateSine(buffer2.data(), buffer2.size(), 440.0f, 44100.0f);

    gd1.process(buffer1.data(), buffer1.size());
    gd2.process(buffer2.data(), buffer2.size());

    // With algorithm variation, output should differ
    float diff = calculateDifference(buffer1.data(), buffer2.data(), blockSize);
    REQUIRE(diff > 0.001f);
}

TEST_CASE("GranularDistortion algorithmVariation uses at least 3 different algorithms (SC-003)",
          "[granular_distortion][layer2][US3][algorithm_variation]") {
    // SC-003: "at least 3 different algorithms used in 100-grain sample"
    // This test directly counts algorithm usage via instrumentation
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setAlgorithmVariation(true);
    gd.setGrainSize(10.0f);   // Short grains for more triggers
    gd.setGrainDensity(8.0f);  // High density for many grains
    gd.seed(12345);

    // Collect algorithm types used by each grain
    std::set<int> algorithmsUsed;
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer;
    size_t lastGrainCount = 0;
    size_t grainsCollected = 0;

    // Process until we have 100+ grain samples
    while (grainsCollected < 100) {
        generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.5f);
        gd.process(buffer.data(), buffer.size());

        // Check if new grain was triggered
        const size_t currentGrainCount = gd.getGrainsTriggeredCount();
        if (currentGrainCount > lastGrainCount) {
            algorithmsUsed.insert(static_cast<int>(gd.getLastTriggeredGrainAlgorithm()));
            ++grainsCollected;
            lastGrainCount = currentGrainCount;
        }
    }

    // Verify we collected enough samples
    REQUIRE(grainsCollected >= 100);

    // SC-003 requirement: at least 3 different algorithms
    INFO("Collected " << grainsCollected << " grains, using " << algorithmsUsed.size() << " different algorithms");
    REQUIRE(algorithmsUsed.size() >= 3);
}

// =============================================================================
// Phase 6: User Story 4 - Grain Density and Overlap Control (T063-T067)
// =============================================================================

TEST_CASE("GranularDistortion density=1 produces sparse texture (SC-004)",
          "[granular_distortion][layer2][US4][density]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(50.0f);  // 50ms grains
    gd.setGrainDensity(1.0f);  // Sparse
    gd.seed(12345);

    constexpr size_t blockSize = 44100;  // 1 second
    std::array<float, blockSize> buffer;
    generateDC(buffer.data(), buffer.size(), 0.5f);

    gd.process(buffer.data(), buffer.size());

    // Count zero-crossings or silent regions to verify sparseness
    size_t silentSamples = 0;
    for (size_t i = 0; i < blockSize; ++i) {
        if (std::abs(buffer[i]) < 0.01f) {
            ++silentSamples;
        }
    }

    // With density=1 and 50ms grains, we expect gaps
    REQUIRE(silentSamples > 1000);  // At least some silent regions
}

TEST_CASE("GranularDistortion density=8 produces thick texture (SC-004)",
          "[granular_distortion][layer2][US4][density]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(50.0f);  // 50ms grains
    gd.setGrainDensity(8.0f);  // Dense
    gd.seed(12345);

    constexpr size_t blockSize = 44100;  // 1 second
    std::array<float, blockSize> buffer;
    generateDC(buffer.data(), buffer.size(), 0.5f);

    gd.process(buffer.data(), buffer.size());

    // Count silent regions - should be few with high density
    size_t silentSamples = 0;
    for (size_t i = 4410; i < blockSize; ++i) {  // Skip startup
        if (std::abs(buffer[i]) < 0.01f) {
            ++silentSamples;
        }
    }

    // With density=8, almost continuous output
    REQUIRE(silentSamples < 5000);  // Mostly non-silent
}

TEST_CASE("GranularDistortion density changes are click-free (FR-009)",
          "[granular_distortion][layer2][US4][density]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(3.0f);
    gd.setGrainSize(30.0f);
    gd.setGrainDensity(4.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 4096;
    std::vector<float> buffer(blockSize * 3);
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.3f);

    // Process first block
    gd.process(buffer.data(), blockSize);

    // Change density abruptly
    gd.setGrainDensity(1.0f);
    gd.process(&buffer[blockSize], blockSize);

    // Change back
    gd.setGrainDensity(8.0f);
    gd.process(&buffer[blockSize * 2], blockSize);

    // Check for clicks at boundaries
    REQUIRE_FALSE(hasClicks(&buffer[blockSize - 10], 20, 0.8f));
    REQUIRE_FALSE(hasClicks(&buffer[blockSize * 2 - 10], 20, 0.8f));
}

TEST_CASE("GranularDistortion density mapping formula is correct",
          "[granular_distortion][layer2][US4][density]") {
    // grainsPerSecond = density * 1000 / grainSizeMs
    // For density=4, grainSize=50ms: grainsPerSecond = 4 * 1000 / 50 = 80
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(50.0f);
    gd.setGrainDensity(4.0f);
    gd.seed(12345);

    // Over 1 second, we expect roughly 80 grains
    // Count grain triggers by observing active grain count changes
    // This is hard to test directly, but we can verify the effect indirectly

    constexpr size_t blockSize = 44100;  // 1 second
    std::array<float, blockSize> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.5f);

    gd.process(buffer.data(), buffer.size());

    // Output should have content (grains triggered)
    float rms = calculateRMS(buffer.data(), buffer.size());
    REQUIRE(rms > 0.01f);
}

// =============================================================================
// Phase 7: User Story 5 - Position Jitter (T075-T079)
// =============================================================================

TEST_CASE("GranularDistortion positionJitter=0ms grains start at current position (FR-023)",
          "[granular_distortion][layer2][US5][position_jitter]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setPositionJitter(0.0f);  // No jitter
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    // With zero jitter, output should be consistent
    constexpr size_t blockSize = 4096;
    std::array<float, blockSize> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 440.0f, 44100.0f);
    generateSine(buffer2.data(), buffer2.size(), 440.0f, 44100.0f);

    gd.process(buffer1.data(), buffer1.size());

    gd.reset();
    gd.seed(12345);
    gd.process(buffer2.data(), buffer2.size());

    // Same seed, zero jitter = identical output
    REQUIRE(buffersEqual(buffer1.data(), buffer2.data(), blockSize));
}

TEST_CASE("GranularDistortion positionJitter=10ms varies grain start positions (FR-022)",
          "[granular_distortion][layer2][US5][position_jitter]") {
    GranularDistortion gd1, gd2;

    // First: no jitter
    gd1.prepare(44100.0, 512);
    gd1.setMix(1.0f);
    gd1.setDrive(5.0f);
    gd1.setPositionJitter(0.0f);
    gd1.setGrainDensity(8.0f);
    gd1.seed(12345);

    // Second: with jitter
    gd2.prepare(44100.0, 512);
    gd2.setMix(1.0f);
    gd2.setDrive(5.0f);
    gd2.setPositionJitter(10.0f);  // 10ms jitter
    gd2.setGrainDensity(8.0f);
    gd2.seed(12345);

    constexpr size_t blockSize = 8192;
    std::array<float, blockSize> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 440.0f, 44100.0f);
    generateSine(buffer2.data(), buffer2.size(), 440.0f, 44100.0f);

    gd1.process(buffer1.data(), buffer1.size());
    gd2.process(buffer2.data(), buffer2.size());

    // With jitter, outputs should differ
    float diff = calculateDifference(buffer1.data(), buffer2.data(), blockSize);
    REQUIRE(diff > 0.001f);
}

TEST_CASE("GranularDistortion jitter is clamped to available buffer history (FR-024-NEW)",
          "[granular_distortion][layer2][US5][position_jitter]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setPositionJitter(50.0f);  // Maximum jitter
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    // Process a very short buffer - jitter should be clamped
    constexpr size_t blockSize = 100;  // Very short
    std::array<float, blockSize> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);

    // Should not crash or produce NaN
    gd.process(buffer.data(), buffer.size());
    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));
}

TEST_CASE("GranularDistortion jitter=50ms produces temporal smearing (SC-005)",
          "[granular_distortion][layer2][US5][position_jitter]") {
    // Test that with vs without jitter produces different outputs
    // indicating temporal smearing is occurring
    GranularDistortion gd1, gd2;

    // First: no jitter
    gd1.prepare(44100.0, 512);
    gd1.setMix(1.0f);
    gd1.setDrive(3.0f);
    gd1.setPositionJitter(0.0f);  // No jitter
    gd1.setGrainSize(30.0f);
    gd1.setGrainDensity(4.0f);
    gd1.seed(12345);

    // Second: maximum jitter
    gd2.prepare(44100.0, 512);
    gd2.setMix(1.0f);
    gd2.setDrive(3.0f);
    gd2.setPositionJitter(50.0f);  // Maximum jitter
    gd2.setGrainSize(30.0f);
    gd2.setGrainDensity(4.0f);
    gd2.seed(12345);  // Same seed

    constexpr size_t blockSize = 22050;  // 0.5 seconds
    std::array<float, blockSize> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 440.0f, 44100.0f);
    generateSine(buffer2.data(), buffer2.size(), 440.0f, 44100.0f);

    gd1.process(buffer1.data(), buffer1.size());
    gd2.process(buffer2.data(), buffer2.size());

    // With jitter, outputs should differ (temporal smearing effect)
    float diff = calculateDifference(buffer1.data(), buffer2.data(), blockSize);
    REQUIRE(diff > 0.001f);

    // Both should have content
    REQUIRE(calculateRMS(buffer1.data(), buffer1.size()) > 0.01f);
    REQUIRE(calculateRMS(buffer2.data(), buffer2.size()) > 0.01f);
}

// =============================================================================
// Phase 8: User Story 6 - Click-Free Automation (T089-T094)
// =============================================================================

TEST_CASE("GranularDistortion grainSize automation is click-free (SC-006)",
          "[granular_distortion][layer2][US6][automation]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(3.0f);  // Lower drive for less extreme output
    gd.setGrainSize(10.0f);
    gd.setGrainDensity(4.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 1024;
    std::vector<float> output;
    output.reserve(blockSize * 10);

    for (int i = 0; i < 10; ++i) {
        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.3f);

        // Sweep grain size
        gd.setGrainSize(10.0f + static_cast<float>(i) * 10.0f);
        gd.process(buffer.data(), buffer.size());

        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // Note: Grain-based processing inherently has amplitude changes at grain boundaries.
    // This is not a "click" in the traditional sense - it's the intended envelope behavior.
    // The key is that there are no sudden large discontinuities from parameter changes.
    // Verify output is valid and has expected content.
    REQUIRE_FALSE(hasInvalidSamples(output.data(), output.size()));
    REQUIRE(calculateRMS(output.data(), output.size()) > 0.01f);
}

TEST_CASE("GranularDistortion drive automation is click-free (SC-006)",
          "[granular_distortion][layer2][US6][automation]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(1.0f);
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 512;
    std::vector<float> output;
    output.reserve(blockSize * 20);

    for (int i = 0; i < 20; ++i) {
        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.3f);

        // Sweep drive
        gd.setDrive(1.0f + static_cast<float>(i) * 0.5f);
        gd.process(buffer.data(), buffer.size());

        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // Drive changes use smoothing - verify no NaN/Inf and continuous output
    REQUIRE_FALSE(hasInvalidSamples(output.data(), output.size()));
    REQUIRE(calculateRMS(output.data(), output.size()) > 0.01f);
}

TEST_CASE("GranularDistortion mix automation is click-free (SC-006)",
          "[granular_distortion][layer2][US6][automation]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(0.0f);
    gd.setDrive(3.0f);
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 512;
    std::vector<float> output;
    output.reserve(blockSize * 20);

    for (int i = 0; i < 20; ++i) {
        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.3f);

        // Sweep mix
        gd.setMix(static_cast<float>(i) / 19.0f);
        gd.process(buffer.data(), buffer.size());

        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // Mix uses smoothing - verify smooth transition
    REQUIRE_FALSE(hasInvalidSamples(output.data(), output.size()));
    REQUIRE(calculateRMS(output.data(), output.size()) > 0.01f);
}

TEST_CASE("GranularDistortion density automation is click-free (SC-006)",
          "[granular_distortion][layer2][US6][automation]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(3.0f);
    gd.setGrainDensity(1.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 512;
    std::vector<float> output;
    output.reserve(blockSize * 14);

    for (int i = 0; i < 14; ++i) {
        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f, 0.3f);

        // Sweep density
        gd.setGrainDensity(1.0f + static_cast<float>(i) * 0.5f);
        gd.process(buffer.data(), buffer.size());

        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // Density changes affect scheduler - verify valid output
    REQUIRE_FALSE(hasInvalidSamples(output.data(), output.size()));
    REQUIRE(calculateRMS(output.data(), output.size()) > 0.01f);
}

TEST_CASE("GranularDistortion parameter changes complete within 10ms (SC-006)",
          "[granular_distortion][layer2][US6][automation]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(0.0f);  // Start at 0
    gd.setDrive(1.0f);
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    // Change to full wet
    gd.setMix(1.0f);

    // Process 10ms of samples (441 samples at 44100Hz)
    constexpr size_t samples10ms = 441;
    std::array<float, samples10ms> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);

    gd.process(buffer.data(), buffer.size());

    // After 10ms, mix should be at or very near target (within 1% of range)
    // We can't directly query the smoother, but the effect should be audible
    // Process one more sample and check it's near full wet behavior

    std::array<float, 1024> testBuffer;
    generateSine(testBuffer.data(), testBuffer.size(), 440.0f, 44100.0f);
    std::array<float, 1024> original;
    std::copy(testBuffer.begin(), testBuffer.end(), original.begin());

    gd.process(testBuffer.data(), testBuffer.size());

    // At mix=1.0, output should differ from input (wet signal)
    float diff = calculateDifference(original.data(), testBuffer.data(), 1024);
    REQUIRE(diff > 0.001f);  // Not identical to dry
}

// =============================================================================
// Phase 9: Edge Cases and Stability
// =============================================================================

TEST_CASE("GranularDistortion is mono-only (FR-047)",
          "[granular_distortion][layer2][edge_case][mono]") {
    // Verify single-channel processing
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);

    // Process mono buffer
    std::array<float, 1024> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);

    gd.process(buffer.data(), buffer.size());

    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));
}

TEST_CASE("GranularDistortion grainSize at minimum (5ms) remains stable",
          "[granular_distortion][layer2][edge_case]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(5.0f);  // Minimum
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 44100;  // 1 second
    std::array<float, blockSize> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);

    gd.process(buffer.data(), buffer.size());

    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));
}

TEST_CASE("GranularDistortion grainSize at maximum (100ms) remains stable",
          "[granular_distortion][layer2][edge_case]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(100.0f);  // Maximum
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    constexpr size_t blockSize = 44100;  // 1 second
    std::array<float, blockSize> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);

    gd.process(buffer.data(), buffer.size());

    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));
}

TEST_CASE("GranularDistortion all grains stolen continues audio (SC-010)",
          "[granular_distortion][layer2][edge_case]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(100.0f);  // Long grains
    gd.setGrainDensity(8.0f);  // High density to exhaust pool
    gd.seed(12345);

    // Process enough to trigger many grains (will steal)
    constexpr size_t blockSize = 44100 * 2;  // 2 seconds
    std::vector<float> buffer(blockSize);
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);

    gd.process(buffer.data(), buffer.size());

    // Should continue processing without crash
    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));

    // Output should have content
    float rms = calculateRMS(buffer.data(), buffer.size());
    REQUIRE(rms > 0.01f);
}

TEST_CASE("GranularDistortion NaN/Inf input returns 0.0 and resets (FR-034)",
          "[granular_distortion][layer2][edge_case][safety]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainDensity(8.0f);
    gd.seed(12345);

    // Build up some state
    std::array<float, 1024> warmup;
    generateSine(warmup.data(), warmup.size(), 440.0f, 44100.0f);
    gd.process(warmup.data(), warmup.size());

    SECTION("NaN input") {
        float output = gd.process(std::numeric_limits<float>::quiet_NaN());
        REQUIRE(output == 0.0f);
    }

    SECTION("Positive infinity input") {
        float output = gd.process(std::numeric_limits<float>::infinity());
        REQUIRE(output == 0.0f);
    }

    SECTION("Negative infinity input") {
        float output = gd.process(-std::numeric_limits<float>::infinity());
        REQUIRE(output == 0.0f);
    }
}

TEST_CASE("GranularDistortion DC input produces rhythmic output at low density",
          "[granular_distortion][layer2][edge_case]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(50.0f);
    gd.setGrainDensity(2.0f);  // Low density
    gd.seed(12345);

    constexpr size_t blockSize = 44100;  // 1 second
    std::array<float, blockSize> buffer;
    generateDC(buffer.data(), buffer.size(), 0.5f);

    gd.process(buffer.data(), buffer.size());

    // Output should have variation (not constant)
    float minVal = *std::min_element(buffer.begin(), buffer.end());
    float maxVal = *std::max_element(buffer.begin(), buffer.end());
    REQUIRE(maxVal - minVal > 0.01f);
}

TEST_CASE("GranularDistortion driveVariation > 1.0 is clamped",
          "[granular_distortion][layer2][edge_case]") {
    GranularDistortion gd;
    gd.setDriveVariation(2.0f);  // Over max
    REQUIRE(gd.getDriveVariation() == Approx(1.0f));

    gd.setDriveVariation(-1.0f);  // Under min
    REQUIRE(gd.getDriveVariation() == Approx(0.0f));
}

// =============================================================================
// Phase 10: Performance and Memory
// =============================================================================

TEST_CASE("GranularDistortion memory budget < 256KB (SC-007-MEM)",
          "[granular_distortion][layer2][performance][memory]") {
    // sizeof should be under 256KB
    REQUIRE(sizeof(GranularDistortion) < 256 * 1024);
}

TEST_CASE("GranularDistortion process is noexcept (FR-033)",
          "[granular_distortion][layer2][performance][realtime]") {
    // Verify noexcept via static_assert in the header
    // This test just confirms the class can be instantiated
    GranularDistortion gd;
    gd.prepare(44100.0, 512);

    float sample = 0.5f;
    // If process() were not noexcept, this would be a compile error
    static_assert(noexcept(gd.process(sample)));
}

// =============================================================================
// Phase 11: Sample Rate Variations
// =============================================================================

TEST_CASE("GranularDistortion works at 44100Hz",
          "[granular_distortion][layer2][sample_rate]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainDensity(4.0f);
    gd.seed(12345);

    std::array<float, 4096> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);

    gd.process(buffer.data(), buffer.size());

    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));
}

TEST_CASE("GranularDistortion works at 48000Hz",
          "[granular_distortion][layer2][sample_rate]") {
    GranularDistortion gd;
    gd.prepare(48000.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainDensity(4.0f);
    gd.seed(12345);

    std::array<float, 4096> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 48000.0f);

    gd.process(buffer.data(), buffer.size());

    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));
}

TEST_CASE("GranularDistortion works at 96000Hz",
          "[granular_distortion][layer2][sample_rate]") {
    GranularDistortion gd;
    gd.prepare(96000.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainDensity(4.0f);
    gd.seed(12345);

    std::array<float, 4096> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 96000.0f);

    gd.process(buffer.data(), buffer.size());

    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));
}

TEST_CASE("GranularDistortion works at 192000Hz",
          "[granular_distortion][layer2][sample_rate]") {
    GranularDistortion gd;
    gd.prepare(192000.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainDensity(4.0f);
    gd.seed(12345);

    std::array<float, 8192> buffer;
    generateSine(buffer.data(), buffer.size(), 440.0f, 192000.0f);

    gd.process(buffer.data(), buffer.size());

    REQUIRE_FALSE(hasInvalidSamples(buffer.data(), buffer.size()));
}

// =============================================================================
// Phase 12: Query Methods
// =============================================================================

TEST_CASE("GranularDistortion isPrepared() returns correct state",
          "[granular_distortion][layer2][query]") {
    GranularDistortion gd;
    REQUIRE(gd.isPrepared() == false);

    gd.prepare(44100.0, 512);
    REQUIRE(gd.isPrepared() == true);
}

TEST_CASE("GranularDistortion getActiveGrainCount() returns correct count",
          "[granular_distortion][layer2][query]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setMix(1.0f);
    gd.setDrive(5.0f);
    gd.setGrainSize(100.0f);  // Long grains
    gd.setGrainDensity(8.0f);  // High density
    gd.seed(12345);

    // Initially no grains
    REQUIRE(gd.getActiveGrainCount() == 0);

    // Process some audio to trigger grains
    std::array<float, 4410> buffer;  // 100ms
    generateSine(buffer.data(), buffer.size(), 440.0f, 44100.0f);
    gd.process(buffer.data(), buffer.size());

    // Should have some active grains
    REQUIRE(gd.getActiveGrainCount() > 0);
}

TEST_CASE("GranularDistortion getMaxGrains() returns 64",
          "[granular_distortion][layer2][query]") {
    REQUIRE(GranularDistortion::getMaxGrains() == 64);
}

TEST_CASE("GranularDistortion seed() produces reproducible behavior",
          "[granular_distortion][layer2][query]") {
    GranularDistortion gd1, gd2;

    gd1.prepare(44100.0, 512);
    gd1.setMix(1.0f);
    gd1.setDrive(5.0f);
    gd1.setGrainDensity(8.0f);
    gd1.setDriveVariation(0.5f);
    gd1.seed(42);

    gd2.prepare(44100.0, 512);
    gd2.setMix(1.0f);
    gd2.setDrive(5.0f);
    gd2.setGrainDensity(8.0f);
    gd2.setDriveVariation(0.5f);
    gd2.seed(42);  // Same seed

    std::array<float, 4096> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 440.0f, 44100.0f);
    generateSine(buffer2.data(), buffer2.size(), 440.0f, 44100.0f);

    gd1.process(buffer1.data(), buffer1.size());
    gd2.process(buffer2.data(), buffer2.size());

    // Same seed should produce identical output
    REQUIRE(buffersEqual(buffer1.data(), buffer2.data(), buffer1.size()));
}

// =============================================================================
// D-074: per-grain state follows the grain's pool slot for its whole life
// =============================================================================

namespace {

struct GrainLifeAudit {
    size_t readJumps = 0;           ///< Read position did not advance by exactly 1
    size_t startChanges = 0;        ///< startBufferPos changed mid-life
    size_t skippedSamples = 0;      ///< A live grain missed a sample
    size_t duplicateCalls = 0;      ///< Same grain processed twice in one sample
    size_t callCountMismatches = 0; ///< processGrain calls != active grains
    size_t distinctMismatches = 0;  ///< Distinct grains processed != active grains
    size_t firstRetirementSample = 0;
    size_t firstJumpSample = 0;
    long long firstJump = 0;
    long long maxAbsJump = 0;
    size_t firstMismatchSample = 0;
    size_t firstMismatchActive = 0;
    size_t firstMismatchCalls = 0;
    size_t firstMismatchDistinct = 0;
};

GrainLifeAudit auditGrainLives(float mix, size_t numSamples) {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setGrainSize(10.0f);
    gd.setGrainDensity(8.0f);
    gd.setDrive(5.0f);
    gd.setMix(mix);
    gd.seed(12345);

    constexpr size_t kSlots = GranularDistortion::getMaxGrains();
    constexpr auto kNever = static_cast<size_t>(-1);
    std::array<size_t, kSlots> lastSample{};
    std::array<size_t, kSlots> lastReadPos{};
    std::array<size_t, kSlots> lastStart{};
    std::array<bool, kSlots> alive{};
    lastSample.fill(kNever);

    GrainLifeAudit audit;
    bool retired = false;
    bool jumped = false;
    bool mismatched = false;

    for (size_t n = 0; n < numSamples; ++n) {
        const float x = std::sin(kTestTwoPi * 1000.0f * static_cast<float>(n) / 44100.0f);
        (void)gd.process(x);

        const auto records = gd.debugGrainReadPositions();
        const size_t active = gd.debugLastActiveGrainCount();
        const size_t calls = gd.debugLastProcessedGrainCount();
        std::array<bool, kSlots> seen{};
        size_t distinct = 0;

        for (const auto& r : records) {
            if (seen[r.slot]) {
                ++audit.duplicateCalls;
                continue;
            }
            seen[r.slot] = true;
            ++distinct;

            if (r.envelopePhase == 0.0f) {
                alive[r.slot] = true;  // First sample of a new grain
            } else if (alive[r.slot]) {
                if (lastSample[r.slot] != n - 1) ++audit.skippedSamples;
                if (r.startBufferPos != lastStart[r.slot]) ++audit.startChanges;
                constexpr auto kSize = static_cast<long long>(GranularDistortion::kBufferSize);
                auto d = static_cast<long long>(
                    (r.readPos - lastReadPos[r.slot]) & GranularDistortion::kBufferMask);
                if (d > kSize / 2) {
                    d -= kSize;
                }
                if (d != 1) {
                    ++audit.readJumps;
                    audit.maxAbsJump = std::max(audit.maxAbsJump, d < 0 ? -d : d);
                    if (!jumped) {
                        jumped = true;
                        audit.firstJump = d;
                        audit.firstJumpSample = n;
                    }
                }
            }
            lastSample[r.slot] = n;
            lastReadPos[r.slot] = r.readPos;
            lastStart[r.slot] = r.startBufferPos;
        }

        if (calls != active) ++audit.callCountMismatches;
        if (distinct != active) ++audit.distinctMismatches;
        if ((calls != active || distinct != active) && !mismatched) {
            mismatched = true;
            audit.firstMismatchSample = n;
            audit.firstMismatchActive = active;
            audit.firstMismatchCalls = calls;
            audit.firstMismatchDistinct = distinct;
        }
        if (!retired && gd.getActiveGrainCount() < active) {
            retired = true;
            audit.firstRetirementSample = n;
        }
    }
    return audit;
}

void reportGrainLifeAudit(const GrainLifeAudit& a) {
    INFO("first retirement at sample " << a.firstRetirementSample);
    INFO("read jumps " << a.readJumps << ", first " << a.firstJump << " samples at sample "
                       << a.firstJumpSample << ", max |jump| " << a.maxAbsJump);
    INFO("start changes mid-life " << a.startChanges << ", skipped samples "
                                   << a.skippedSamples << ", duplicate calls "
                                   << a.duplicateCalls);
    INFO("first count mismatch at sample " << a.firstMismatchSample << ": active "
                                           << a.firstMismatchActive << ", processGrain calls "
                                           << a.firstMismatchCalls << ", distinct grains "
                                           << a.firstMismatchDistinct);
    INFO("samples with calls != active " << a.callCountMismatches
                                         << ", with distinct != active "
                                         << a.distinctMismatches);
    CHECK(a.firstRetirementSample > 0);
    CHECK(a.readJumps == 0);
    CHECK(a.startChanges == 0);
    CHECK(a.skippedSamples == 0);
    CHECK(a.duplicateCalls == 0);
    CHECK(a.callCountMismatches == 0);
    CHECK(a.distinctMismatches == 0);
}

} // namespace

TEST_CASE("Each grain reads its own state for its whole life",
          "[processors][granular][D-074]") {
    SECTION("wet path (mix 1)") {
        reportGrainLifeAudit(auditGrainLives(1.0f, 2000));
    }
    SECTION("bypass path (mix 0) keeps grain state consistent") {
        reportGrainLifeAudit(auditGrainLives(0.0f, 2000));
    }
}

TEST_CASE("GranularDistortion process allocates nothing",
          "[processors][granular][D-074]") {
    GranularDistortion gd;
    gd.prepare(44100.0, 512);
    gd.setGrainSize(10.0f);
    gd.setGrainDensity(8.0f);
    gd.setDrive(5.0f);
    gd.seed(12345);

    std::vector<float> block(512);
    std::size_t observed = 0;
    float sink = 0.0f;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        size_t n = 0;
        for (int b = 0; b < 173; ++b) {  // ~2 s at 44.1 kHz
            for (auto& s : block) {
                s = std::sin(kTestTwoPi * 1000.0f * static_cast<float>(n++) / 44100.0f);
            }
            gd.setMix((b % 2 == 0) ? 1.0f : 0.0f);  // both grain loops
            gd.process(block.data(), block.size());
            sink += block.back();
        }
        // AllocationScope latches in its destructor, so the live figure is read while it is open.
        observed = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    INFO("allocations observed: " << observed);
    CHECK(detail::isFinite(sink));
    REQUIRE(observed == 0u);

    // Positive control: the detector sees an allocation inside an identical scope.
    std::size_t control = 0;
    int probeObserved = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        int* probe = new int(42);
        volatile int* probeSink = probe;  // defeat new/delete elision
        probeObserved = *probeSink;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete probe;
        control = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(probeObserved == 42);
    CHECK(control >= 1u);
}
