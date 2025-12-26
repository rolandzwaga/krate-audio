// ==============================================================================
// Tests: ShimmerDelay (Layer 4 User Feature)
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests MUST be written before implementation.
//
// Feature: 029-shimmer-delay
// Reference: specs/029-shimmer-delay/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/features/shimmer_delay.h"
#include "dsp/core/block_context.h"

#include <array>
#include <cmath>
#include <numeric>
#include <complex>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr float kMaxDelayMs = 5000.0f;

/// @brief Create a default BlockContext for testing
BlockContext makeTestContext(double sampleRate = kSampleRate, double bpm = 120.0) {
    BlockContext ctx;
    ctx.sampleRate = sampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = bpm;
    ctx.timeSignatureNumerator = 4;
    ctx.timeSignatureDenominator = 4;
    ctx.isPlaying = true;
    return ctx;
}

/// @brief Generate an impulse in a stereo buffer
void generateImpulse(float* left, float* right, size_t size) {
    std::fill(left, left + size, 0.0f);
    std::fill(right, right + size, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;
}

/// @brief Generate a sine wave
void generateSineWave(float* buffer, size_t size, float frequency, double sampleRate) {
    const double twoPi = 2.0 * 3.14159265358979323846;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<float>(std::sin(twoPi * frequency * static_cast<double>(i) / sampleRate));
    }
}

/// @brief Find peak in buffer
float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// @brief Find first sample above threshold
size_t findFirstPeak(const float* buffer, size_t size, float threshold = 0.1f) {
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(buffer[i]) > threshold) {
            return i;
        }
    }
    return size; // Not found
}

/// @brief Calculate RMS energy
float calculateRMS(const float* buffer, size_t size) {
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

/// @brief Simple DFT to estimate fundamental frequency
/// @return Estimated frequency in Hz, or 0 if no clear peak found
float estimateFundamentalFrequency(const float* buffer, size_t size, double sampleRate) {
    // Find the bin with maximum energy (excluding DC)
    std::vector<float> magnitudes(size / 2);

    for (size_t k = 1; k < size / 2; ++k) {
        float real = 0.0f, imag = 0.0f;
        const double twoPi = 2.0 * 3.14159265358979323846;
        for (size_t n = 0; n < size; ++n) {
            float angle = static_cast<float>(twoPi * k * n / size);
            real += buffer[n] * std::cos(angle);
            imag -= buffer[n] * std::sin(angle);
        }
        magnitudes[k] = std::sqrt(real * real + imag * imag);
    }

    // Find peak bin
    size_t peakBin = 1;
    float peakMag = 0.0f;
    for (size_t k = 1; k < size / 2; ++k) {
        if (magnitudes[k] > peakMag) {
            peakMag = magnitudes[k];
            peakBin = k;
        }
    }

    // Convert bin to frequency
    return static_cast<float>(peakBin * sampleRate / size);
}

/// @brief Convert semitones to frequency ratio
float semitonesToRatio(float semitones) {
    return std::pow(2.0f, semitones / 12.0f);
}

} // anonymous namespace

// =============================================================================
// Lifecycle Tests (Foundational)
// =============================================================================

TEST_CASE("ShimmerDelay lifecycle", "[shimmer-delay][lifecycle]") {
    ShimmerDelay shimmer;

    SECTION("not prepared initially") {
        REQUIRE_FALSE(shimmer.isPrepared());
    }

    SECTION("prepared after prepare()") {
        shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
        REQUIRE(shimmer.isPrepared());
    }

    SECTION("reset() doesn't change prepared state") {
        shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
        shimmer.reset();
        REQUIRE(shimmer.isPrepared());
    }
}

// =============================================================================
// Default Values Tests (FR-001 to FR-025)
// =============================================================================

TEST_CASE("ShimmerDelay default values", "[shimmer-delay][defaults]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("delay time defaults") {
        REQUIRE(shimmer.getDelayTimeMs() == Approx(500.0f));
        REQUIRE(shimmer.getTimeMode() == TimeMode::Free);
    }

    SECTION("pitch defaults") {
        REQUIRE(shimmer.getPitchSemitones() == Approx(12.0f));  // Octave up
        REQUIRE(shimmer.getPitchCents() == Approx(0.0f));
        REQUIRE(shimmer.getPitchMode() == PitchMode::Granular);
    }

    SECTION("shimmer defaults") {
        REQUIRE(shimmer.getShimmerMix() == Approx(100.0f));  // Full shimmer
        REQUIRE(shimmer.getFeedbackAmount() == Approx(0.5f));
    }

    SECTION("diffusion defaults") {
        REQUIRE(shimmer.getDiffusionAmount() == Approx(50.0f));
        REQUIRE(shimmer.getDiffusionSize() == Approx(50.0f));
    }

    SECTION("filter defaults") {
        REQUIRE_FALSE(shimmer.isFilterEnabled());
        REQUIRE(shimmer.getFilterCutoff() == Approx(4000.0f));
    }

    SECTION("output defaults") {
        REQUIRE(shimmer.getDryWetMix() == Approx(50.0f));
        REQUIRE(shimmer.getOutputGainDb() == Approx(0.0f));
    }
}

// =============================================================================
// Parameter Clamping Tests
// =============================================================================

TEST_CASE("ShimmerDelay parameter clamping", "[shimmer-delay][clamping]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("delay time clamping") {
        shimmer.setDelayTimeMs(1.0f);  // Below min (10ms)
        REQUIRE(shimmer.getDelayTimeMs() == Approx(10.0f));

        shimmer.setDelayTimeMs(10000.0f);  // Above max (5000ms)
        REQUIRE(shimmer.getDelayTimeMs() == Approx(5000.0f));
    }

    SECTION("pitch semitones clamping") {
        shimmer.setPitchSemitones(-48.0f);  // Below min (-24)
        REQUIRE(shimmer.getPitchSemitones() == Approx(-24.0f));

        shimmer.setPitchSemitones(48.0f);  // Above max (+24)
        REQUIRE(shimmer.getPitchSemitones() == Approx(24.0f));
    }

    SECTION("pitch cents clamping") {
        shimmer.setPitchCents(-200.0f);  // Below min (-100)
        REQUIRE(shimmer.getPitchCents() == Approx(-100.0f));

        shimmer.setPitchCents(200.0f);  // Above max (+100)
        REQUIRE(shimmer.getPitchCents() == Approx(100.0f));
    }

    SECTION("shimmer mix clamping") {
        shimmer.setShimmerMix(-10.0f);
        REQUIRE(shimmer.getShimmerMix() == Approx(0.0f));

        shimmer.setShimmerMix(150.0f);
        REQUIRE(shimmer.getShimmerMix() == Approx(100.0f));
    }

    SECTION("feedback amount clamping") {
        shimmer.setFeedbackAmount(-0.5f);
        REQUIRE(shimmer.getFeedbackAmount() == Approx(0.0f));

        shimmer.setFeedbackAmount(2.0f);
        REQUIRE(shimmer.getFeedbackAmount() == Approx(1.2f));  // 120% max
    }

    SECTION("diffusion clamping") {
        shimmer.setDiffusionAmount(-10.0f);
        REQUIRE(shimmer.getDiffusionAmount() == Approx(0.0f));

        shimmer.setDiffusionAmount(150.0f);
        REQUIRE(shimmer.getDiffusionAmount() == Approx(100.0f));
    }

    SECTION("filter cutoff clamping") {
        shimmer.setFilterCutoff(5.0f);  // Below min (20Hz)
        REQUIRE(shimmer.getFilterCutoff() == Approx(20.0f));

        shimmer.setFilterCutoff(30000.0f);  // Above max (20kHz)
        REQUIRE(shimmer.getFilterCutoff() == Approx(20000.0f));
    }

    SECTION("dry/wet mix clamping") {
        shimmer.setDryWetMix(-10.0f);
        REQUIRE(shimmer.getDryWetMix() == Approx(0.0f));

        shimmer.setDryWetMix(150.0f);
        REQUIRE(shimmer.getDryWetMix() == Approx(100.0f));
    }

    SECTION("output gain clamping") {
        shimmer.setOutputGainDb(-24.0f);  // Below min (-12dB)
        REQUIRE(shimmer.getOutputGainDb() == Approx(-12.0f));

        shimmer.setOutputGainDb(24.0f);  // Above max (+12dB)
        REQUIRE(shimmer.getOutputGainDb() == Approx(12.0f));
    }
}

// =============================================================================
// User Story 1: Classic Shimmer (MVP)
// FR-001, FR-007, FR-011, FR-013, FR-022
// SC-001: Pitch accuracy ±5 cents
// =============================================================================

TEST_CASE("US1: Classic shimmer creates audible output", "[shimmer-delay][US1]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    // Configure classic shimmer
    shimmer.setDelayTimeMs(500.0f);
    shimmer.setPitchSemitones(12.0f);  // Octave up
    shimmer.setShimmerMix(100.0f);
    shimmer.setFeedbackAmount(0.5f);
    shimmer.setDryWetMix(100.0f);  // Full wet for testing
    shimmer.setDiffusionAmount(0.0f);  // No diffusion for simpler test
    shimmer.snapParameters();

    constexpr size_t kBufferSize = 44100;  // 1 second
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;
    right[0] = 1.0f;

    auto ctx = makeTestContext();
    shimmer.process(left.data(), right.data(), kBufferSize, ctx);

    // Should have audible output after delay time
    constexpr size_t kDelaySamples = static_cast<size_t>(500.0f * 44100.0f / 1000.0f);  // ~22050
    float outputPeak = findPeak(left.data() + kDelaySamples, kBufferSize - kDelaySamples);
    REQUIRE(outputPeak > 0.01f);  // Should have some output
}

TEST_CASE("US1: Shimmer mix at 0% produces standard delay", "[shimmer-delay][US1][SC-003]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    // Shimmer mix 0% = no pitch shifting (standard delay)
    shimmer.setDelayTimeMs(100.0f);
    shimmer.setShimmerMix(0.0f);  // No shimmer
    shimmer.setFeedbackAmount(0.3f);
    shimmer.setDryWetMix(100.0f);
    shimmer.setDiffusionAmount(0.0f);
    shimmer.snapParameters();

    constexpr size_t kBufferSize = 22050;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};

    // Use a 440Hz sine wave as input
    generateSineWave(left.data(), 4410, 440.0f, kSampleRate);
    std::copy(left.begin(), left.begin() + 4410, right.begin());

    auto ctx = makeTestContext();
    shimmer.process(left.data(), right.data(), kBufferSize, ctx);

    // With 0% shimmer mix, output frequency should remain ~440Hz
    // Check the delayed portion (after 100ms = 4410 samples)
    float estimatedFreq = estimateFundamentalFrequency(left.data() + 4410, 4410, kSampleRate);

    // Should be close to 440Hz (within 10%)
    REQUIRE(estimatedFreq > 396.0f);  // 440 - 10%
    REQUIRE(estimatedFreq < 484.0f);  // 440 + 10%
}

// =============================================================================
// User Story 2: Tempo-Synced Shimmer
// FR-002, FR-004, FR-005, FR-006
// =============================================================================

TEST_CASE("US2: Tempo sync calculates correct delay", "[shimmer-delay][US2][tempo-sync]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    shimmer.setTimeMode(TimeMode::Synced);
    shimmer.setNoteValue(NoteValue::Quarter, NoteModifier::None);
    shimmer.snapParameters();

    // At 120 BPM, quarter note = 500ms
    auto ctx = makeTestContext(kSampleRate, 120.0);

    constexpr size_t kBufferSize = 44100;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;
    right[0] = 1.0f;

    shimmer.setDryWetMix(100.0f);
    shimmer.setFeedbackAmount(0.3f);
    shimmer.setDiffusionAmount(0.0f);
    shimmer.setShimmerMix(0.0f);  // Clean delay for timing test
    shimmer.snapParameters();

    shimmer.process(left.data(), right.data(), kBufferSize, ctx);

    // First echo should appear around 22050 samples (500ms at 44.1kHz)
    // Allow some tolerance for smoothing
    constexpr size_t kExpectedDelaySamples = 22050;
    size_t firstPeakPos = findFirstPeak(left.data() + 100, kBufferSize - 100, 0.05f);

    // Should be within 5% of expected
    REQUIRE(firstPeakPos > kExpectedDelaySamples * 95 / 100 - 100);
    REQUIRE(firstPeakPos < kExpectedDelaySamples * 105 / 100 + 100);
}

// =============================================================================
// User Story 3: Downward Shimmer
// FR-007, FR-008
// =============================================================================

TEST_CASE("US3: Negative pitch creates downward shimmer", "[shimmer-delay][US3]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    shimmer.setDelayTimeMs(200.0f);
    shimmer.setPitchSemitones(-12.0f);  // Octave DOWN
    shimmer.setShimmerMix(100.0f);
    shimmer.setFeedbackAmount(0.5f);
    shimmer.setDryWetMix(100.0f);
    shimmer.setDiffusionAmount(0.0f);
    shimmer.snapParameters();

    REQUIRE(shimmer.getPitchSemitones() == Approx(-12.0f));
    REQUIRE(shimmer.getPitchRatio() == Approx(0.5f).margin(0.01f));  // Octave down = 0.5x
}

// =============================================================================
// User Story 4: Subtle Shimmer
// FR-011, FR-012
// =============================================================================

TEST_CASE("US4: Subtle shimmer blends pitched/unpitched", "[shimmer-delay][US4]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    shimmer.setDelayTimeMs(300.0f);
    shimmer.setPitchSemitones(12.0f);
    shimmer.setShimmerMix(30.0f);  // Only 30% pitch-shifted
    shimmer.setFeedbackAmount(0.5f);
    shimmer.setDryWetMix(100.0f);
    shimmer.snapParameters();

    REQUIRE(shimmer.getShimmerMix() == Approx(30.0f));

    // Process should produce output (functional test)
    constexpr size_t kBufferSize = 22050;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;
    right[0] = 1.0f;

    auto ctx = makeTestContext();
    shimmer.process(left.data(), right.data(), kBufferSize, ctx);

    float outputPeak = findPeak(left.data(), kBufferSize);
    REQUIRE(outputPeak > 0.01f);
}

// =============================================================================
// User Story 5: Feedback Stability (SC-005)
// FR-013, FR-014, FR-015
// =============================================================================

TEST_CASE("US5: High feedback remains stable", "[shimmer-delay][US5][SC-005]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    // Configure with high feedback (120%)
    shimmer.setDelayTimeMs(100.0f);
    shimmer.setPitchSemitones(12.0f);
    shimmer.setShimmerMix(100.0f);
    shimmer.setFeedbackAmount(1.2f);  // 120% feedback
    shimmer.setDryWetMix(100.0f);
    shimmer.setDiffusionAmount(0.0f);
    shimmer.snapParameters();

    // Process for 10 seconds
    constexpr size_t kBufferSize = 4096;
    constexpr size_t kNumBlocks = 108;  // ~10 seconds at 44.1kHz
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};

    // Initial impulse
    left[0] = 1.0f;
    right[0] = 1.0f;

    auto ctx = makeTestContext();

    float maxPeak = 0.0f;
    for (size_t block = 0; block < kNumBlocks; ++block) {
        shimmer.process(left.data(), right.data(), kBufferSize, ctx);

        float blockPeak = std::max(findPeak(left.data(), kBufferSize),
                                   findPeak(right.data(), kBufferSize));
        maxPeak = std::max(maxPeak, blockPeak);

        // Clear for next block (feedback is internal)
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    // SC-005: Output should never exceed +6dBFS (~2.0 linear)
    REQUIRE(maxPeak < 2.0f);
}

// =============================================================================
// User Story 6: Diffusion Effects
// FR-016, FR-017, FR-018, FR-019
// =============================================================================

TEST_CASE("US6: Diffusion creates smeared texture", "[shimmer-delay][US6]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    shimmer.setDelayTimeMs(300.0f);  // 300ms = ~13230 samples at 44.1kHz
    shimmer.setPitchSemitones(12.0f);
    shimmer.setShimmerMix(100.0f);
    shimmer.setFeedbackAmount(0.5f);
    shimmer.setDiffusionAmount(100.0f);  // Maximum diffusion
    shimmer.setDiffusionSize(50.0f);
    shimmer.setDryWetMix(100.0f);
    shimmer.snapParameters();

    constexpr size_t kBufferSize = 44100;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;
    right[0] = 1.0f;

    auto ctx = makeTestContext();
    shimmer.process(left.data(), right.data(), kBufferSize, ctx);

    // With high diffusion, output should be present after the delay time
    // 300ms at 44.1kHz = 13230 samples
    constexpr size_t kDelaySamples = 13230;
    float outputPeak = findPeak(left.data() + kDelaySamples, kBufferSize - kDelaySamples);
    REQUIRE(outputPeak > 0.001f);
}

// =============================================================================
// Filter Tests
// FR-020, FR-021
// =============================================================================

TEST_CASE("Filter in feedback path", "[shimmer-delay][filter]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("Filter can be enabled/disabled") {
        REQUIRE_FALSE(shimmer.isFilterEnabled());

        shimmer.setFilterEnabled(true);
        REQUIRE(shimmer.isFilterEnabled());

        shimmer.setFilterEnabled(false);
        REQUIRE_FALSE(shimmer.isFilterEnabled());
    }

    SECTION("Filter cutoff can be set") {
        shimmer.setFilterCutoff(2000.0f);
        REQUIRE(shimmer.getFilterCutoff() == Approx(2000.0f));
    }
}

// =============================================================================
// Pitch Mode Tests
// FR-008, FR-009, FR-010
// =============================================================================

TEST_CASE("Pitch mode selection", "[shimmer-delay][pitch-mode]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("Can set Simple mode") {
        shimmer.setPitchMode(PitchMode::Simple);
        REQUIRE(shimmer.getPitchMode() == PitchMode::Simple);
        REQUIRE(shimmer.getLatencySamples() == 0);  // Simple = zero latency
    }

    SECTION("Can set Granular mode (default)") {
        shimmer.setPitchMode(PitchMode::Granular);
        REQUIRE(shimmer.getPitchMode() == PitchMode::Granular);
        REQUIRE(shimmer.getLatencySamples() > 0);  // Granular has latency
    }

    SECTION("Can set PhaseVocoder mode") {
        shimmer.setPitchMode(PitchMode::PhaseVocoder);
        REQUIRE(shimmer.getPitchMode() == PitchMode::PhaseVocoder);
        REQUIRE(shimmer.getLatencySamples() > 0);  // PhaseVocoder has latency
    }
}

// =============================================================================
// Latency Reporting Tests
// =============================================================================

TEST_CASE("Latency reporting", "[shimmer-delay][latency]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("Simple mode reports zero latency") {
        shimmer.setPitchMode(PitchMode::Simple);
        REQUIRE(shimmer.getLatencySamples() == 0);
    }

    SECTION("Granular mode reports ~46ms latency") {
        shimmer.setPitchMode(PitchMode::Granular);
        size_t latency = shimmer.getLatencySamples();
        // ~46ms at 44.1kHz = ~2029 samples
        REQUIRE(latency > 1500);
        REQUIRE(latency < 3000);
    }

    SECTION("PhaseVocoder mode reports ~116ms latency") {
        shimmer.setPitchMode(PitchMode::PhaseVocoder);
        size_t latency = shimmer.getLatencySamples();
        // ~116ms at 44.1kHz = ~5116 samples
        REQUIRE(latency > 4000);
        REQUIRE(latency < 7000);
    }
}

// =============================================================================
// Pitch Ratio Tests
// =============================================================================

TEST_CASE("Pitch ratio calculation", "[shimmer-delay][pitch-ratio]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("Octave up = 2.0 ratio") {
        shimmer.setPitchSemitones(12.0f);
        shimmer.setPitchCents(0.0f);
        REQUIRE(shimmer.getPitchRatio() == Approx(2.0f).margin(0.001f));
    }

    SECTION("Octave down = 0.5 ratio") {
        shimmer.setPitchSemitones(-12.0f);
        shimmer.setPitchCents(0.0f);
        REQUIRE(shimmer.getPitchRatio() == Approx(0.5f).margin(0.001f));
    }

    SECTION("Perfect fifth up = 1.5 ratio") {
        shimmer.setPitchSemitones(7.0f);
        shimmer.setPitchCents(0.0f);
        REQUIRE(shimmer.getPitchRatio() == Approx(1.4983f).margin(0.01f));
    }

    SECTION("Zero semitones = 1.0 ratio") {
        shimmer.setPitchSemitones(0.0f);
        shimmer.setPitchCents(0.0f);
        REQUIRE(shimmer.getPitchRatio() == Approx(1.0f).margin(0.001f));
    }
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST_CASE("Edge cases", "[shimmer-delay][edge-case]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("Zero-length buffer processing") {
        std::array<float, 1> left{1.0f};
        std::array<float, 1> right{1.0f};
        auto ctx = makeTestContext();

        // Should not crash with 0 samples
        shimmer.process(left.data(), right.data(), 0, ctx);
    }

    SECTION("Processing without prepare should be no-op") {
        ShimmerDelay unprepared;
        std::array<float, 512> left{};
        std::array<float, 512> right{};
        left[0] = 1.0f;
        right[0] = 1.0f;
        auto ctx = makeTestContext();

        unprepared.process(left.data(), right.data(), 512, ctx);

        // Output should be unchanged (no crash, no processing)
        REQUIRE(left[0] == 1.0f);
    }

    SECTION("Reset clears delay state") {
        shimmer.setDelayTimeMs(100.0f);
        shimmer.setDryWetMix(100.0f);
        shimmer.setFeedbackAmount(0.5f);
        shimmer.setShimmerMix(0.0f);
        shimmer.snapParameters();

        // Process an impulse
        std::array<float, 8820> left{};
        std::array<float, 8820> right{};
        left[0] = 1.0f;
        right[0] = 1.0f;
        auto ctx = makeTestContext();
        shimmer.process(left.data(), right.data(), 8820, ctx);

        // Reset
        shimmer.reset();

        // Process silence - should get silence out
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        shimmer.process(left.data(), right.data(), 8820, ctx);

        float peak = findPeak(left.data(), 8820);
        REQUIRE(peak < 0.01f);  // Should be nearly silent
    }
}

// =============================================================================
// Modulation Matrix Connection Tests
// =============================================================================

TEST_CASE("Modulation matrix connection", "[shimmer-delay][modulation]") {
    ShimmerDelay shimmer;
    shimmer.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("Can connect modulation matrix") {
        ModulationMatrix matrix;
        matrix.prepare(kSampleRate, kBlockSize, 32);

        shimmer.connectModulationMatrix(&matrix);
        // Should not crash
    }

    SECTION("Can disconnect modulation matrix") {
        shimmer.connectModulationMatrix(nullptr);
        // Should not crash
    }
}
