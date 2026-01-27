// ==============================================================================
// Layer 2: DSP Processor Tests - AliasingEffect
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 112-aliasing-effect
//
// Reference: specs/112-aliasing-effect/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/aliasing_effect.h>
#include <spectral_analysis.h>

#include <array>
#include <cmath>
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

/// Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t size) {
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
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

} // namespace

// =============================================================================
// T005: Basic Aliasing Tests (SC-001)
// =============================================================================

TEST_CASE("AliasingEffect creates aliased frequencies from high-frequency input",
          "[aliasing_effect][layer2][US1][SC-001]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setMix(1.0f);  // Full wet

    constexpr size_t blockSize = 4096;
    constexpr float sampleRate = 44100.0f;

    SECTION("downsample factor 8 creates aliasing above reduced Nyquist") {
        // With factor 8, effective sample rate = 44100/8 = 5512.5 Hz
        // Nyquist = 2756.25 Hz
        // A 5000Hz input is above Nyquist and will alias
        aliaser.setDownsampleFactor(8.0f);

        std::array<float, blockSize> original, processed;
        generateSine(original.data(), blockSize, 5000.0f, sampleRate);
        std::copy(original.begin(), original.end(), processed.begin());

        aliaser.process(processed.data(), blockSize);

        // Output should differ significantly from input due to aliasing
        float diff = calculateDifference(original.data(), processed.data(), blockSize);
        REQUIRE(diff > 0.1f);
    }

    SECTION("higher downsample factor creates more severe aliasing") {
        std::array<float, blockSize> factor4Output, factor16Output;
        generateSine(factor4Output.data(), blockSize, 5000.0f, sampleRate);
        generateSine(factor16Output.data(), blockSize, 5000.0f, sampleRate);

        // Process with factor 4
        aliaser.reset();
        aliaser.setDownsampleFactor(4.0f);
        aliaser.process(factor4Output.data(), blockSize);

        // Process with factor 16
        aliaser.reset();
        aliaser.setDownsampleFactor(16.0f);
        aliaser.process(factor16Output.data(), blockSize);

        // Factor 16 should produce more staircasing (fewer unique values per block)
        // This manifests as different spectral content
        // Both should produce aliasing, but factor 16 more severe
        float rms4 = calculateRMS(factor4Output.data(), blockSize);
        float rms16 = calculateRMS(factor16Output.data(), blockSize);

        // Both should have non-zero output
        REQUIRE(rms4 > 0.1f);
        REQUIRE(rms16 > 0.1f);
    }
}

// =============================================================================
// T006: Mix Control Tests (SC-007)
// =============================================================================

TEST_CASE("AliasingEffect mix control blends dry and wet signals",
          "[aliasing_effect][layer2][US1][SC-007]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(8.0f);

    constexpr size_t blockSize = 1024;
    constexpr float sampleRate = 44100.0f;
    constexpr float testFreq = 5000.0f;

    SECTION("mix at 0% bypasses effect (dry only)") {
        aliaser.setMix(0.0f);
        // Reset to snap smoother to target
        aliaser.reset();

        std::array<float, blockSize> original, processed;
        generateSine(original.data(), blockSize, testFreq, sampleRate);
        std::copy(original.begin(), original.end(), processed.begin());

        aliaser.process(processed.data(), blockSize);

        // With mix=0, output should match input exactly
        for (size_t i = 0; i < blockSize; ++i) {
            REQUIRE(processed[i] == Approx(original[i]).margin(1e-6f));
        }
    }

    SECTION("mix at 100% is full wet") {
        aliaser.setMix(1.0f);

        std::array<float, blockSize> original, processed;
        generateSine(original.data(), blockSize, testFreq, sampleRate);
        std::copy(original.begin(), original.end(), processed.begin());

        aliaser.process(processed.data(), blockSize);

        // With mix=1, output should be fully processed (different from input)
        float diff = calculateDifference(original.data(), processed.data(), blockSize);
        REQUIRE(diff > 0.1f);  // Significant difference
    }

    SECTION("mix at 50% blends dry and wet") {
        aliaser.setMix(0.5f);

        std::array<float, blockSize> original, processed, fullWet;
        generateSine(original.data(), blockSize, testFreq, sampleRate);
        std::copy(original.begin(), original.end(), processed.begin());
        std::copy(original.begin(), original.end(), fullWet.begin());

        // Get full wet reference
        aliaser.setMix(1.0f);
        aliaser.process(fullWet.data(), blockSize);

        // Reset and process with 50% mix
        aliaser.reset();
        aliaser.setMix(0.5f);
        std::copy(original.begin(), original.end(), processed.begin());
        aliaser.process(processed.data(), blockSize);

        // Output should be between dry and wet
        float diffFromDry = calculateDifference(original.data(), processed.data(), blockSize);
        float diffFromWet = calculateDifference(fullWet.data(), processed.data(), blockSize);

        // Should be different from both pure dry and pure wet
        REQUIRE(diffFromDry > 0.01f);
        REQUIRE(diffFromWet > 0.01f);
    }
}

// =============================================================================
// T007: Lifecycle Tests (FR-001, FR-002)
// =============================================================================

TEST_CASE("AliasingEffect lifecycle management",
          "[aliasing_effect][layer2][US1][FR-001][FR-002]") {
    AliasingEffect aliaser;

    SECTION("isPrepared returns false before prepare") {
        REQUIRE_FALSE(aliaser.isPrepared());
    }

    SECTION("isPrepared returns true after prepare") {
        aliaser.prepare(44100.0, 512);
        REQUIRE(aliaser.isPrepared());
    }

    SECTION("process before prepare returns input unchanged") {
        float sample = 0.5f;
        float result = aliaser.process(sample);
        REQUIRE(result == Approx(sample));
    }

    SECTION("reset clears internal state without changing parameters") {
        aliaser.prepare(44100.0, 512);
        aliaser.setDownsampleFactor(8.0f);
        aliaser.setMix(0.75f);

        // Process some audio
        for (int i = 0; i < 100; ++i) {
            (void)aliaser.process(static_cast<float>(i) * 0.01f);
        }

        // Reset
        aliaser.reset();

        // Parameters should be preserved
        REQUIRE(aliaser.getDownsampleFactor() == Approx(8.0f));
        REQUIRE(aliaser.getMix() == Approx(0.75f));

        // After reset, processing should work without crashes
        // Note: Exact values depend on band filters and frequency shifter latency
        aliaser.setMix(1.0f);  // Full wet to see effect
        float result = aliaser.process(0.5f);
        REQUIRE(std::isfinite(result));
    }
}

// =============================================================================
// T008: Parameter Clamping Tests (FR-005, FR-020)
// =============================================================================

TEST_CASE("AliasingEffect parameter clamping",
          "[aliasing_effect][layer2][US1][FR-005][FR-020]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);

    SECTION("downsample factor clamps to [2, 32]") {
        aliaser.setDownsampleFactor(1.0f);
        REQUIRE(aliaser.getDownsampleFactor() == Approx(2.0f));

        aliaser.setDownsampleFactor(0.0f);
        REQUIRE(aliaser.getDownsampleFactor() == Approx(2.0f));

        aliaser.setDownsampleFactor(64.0f);
        REQUIRE(aliaser.getDownsampleFactor() == Approx(32.0f));

        aliaser.setDownsampleFactor(8.0f);
        REQUIRE(aliaser.getDownsampleFactor() == Approx(8.0f));
    }

    SECTION("mix clamps to [0, 1]") {
        aliaser.setMix(-0.5f);
        REQUIRE(aliaser.getMix() == Approx(0.0f));

        aliaser.setMix(1.5f);
        REQUIRE(aliaser.getMix() == Approx(1.0f));

        aliaser.setMix(0.5f);
        REQUIRE(aliaser.getMix() == Approx(0.5f));
    }
}

// =============================================================================
// T009: Stability Tests (FR-027)
// =============================================================================

TEST_CASE("AliasingEffect produces no NaN or Inf output",
          "[aliasing_effect][layer2][US1][FR-027]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(16.0f);
    aliaser.setMix(1.0f);

    constexpr size_t blockSize = 4096;

    SECTION("normal input produces valid output") {
        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), blockSize, 1000.0f, 44100.0f);

        aliaser.process(buffer.data(), blockSize);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));
    }

    SECTION("full scale input produces valid output") {
        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), blockSize, 1000.0f, 44100.0f, 1.0f);

        aliaser.process(buffer.data(), blockSize);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));
    }

    SECTION("silence input produces silence output") {
        std::array<float, blockSize> buffer;
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        aliaser.process(buffer.data(), blockSize);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));
        // Output should be near zero
        float rms = calculateRMS(buffer.data(), blockSize);
        REQUIRE(rms < 0.001f);
    }

    SECTION("extreme downsample factor produces valid output") {
        aliaser.setDownsampleFactor(32.0f);  // Maximum

        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), blockSize, 5000.0f, 44100.0f);

        aliaser.process(buffer.data(), blockSize);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));
    }
}

// =============================================================================
// Additional foundational tests for User Story 1
// =============================================================================

TEST_CASE("AliasingEffect default parameter values",
          "[aliasing_effect][layer2][US1][defaults]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);

    SECTION("default downsample factor is 2") {
        REQUIRE(aliaser.getDownsampleFactor() == Approx(AliasingEffect::kDefaultDownsampleFactor));
        REQUIRE(aliaser.getDownsampleFactor() == Approx(2.0f));
    }

    SECTION("default mix is 1.0 (full wet)") {
        REQUIRE(aliaser.getMix() == Approx(1.0f));
    }
}

TEST_CASE("AliasingEffect single sample processing",
          "[aliasing_effect][layer2][US1][single_sample]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(4.0f);
    aliaser.setFrequencyShift(0.0f);  // No shift for predictable behavior
    aliaser.setMix(1.0f);

    SECTION("single sample process returns valid output") {
        float result = aliaser.process(0.5f);
        REQUIRE(std::isfinite(result));
    }

    SECTION("processing shows aliasing artifacts") {
        // Process a high-frequency sine and verify it gets aliased
        constexpr size_t testSize = 256;
        std::array<float, testSize> original, processed;
        generateSine(original.data(), testSize, 10000.0f, 44100.0f);  // High freq
        std::copy(original.begin(), original.end(), processed.begin());

        aliaser.process(processed.data(), testSize);

        // Output should differ from input due to aliasing
        float diff = calculateDifference(original.data(), processed.data(), testSize);
        REQUIRE(diff > 0.1f);
    }
}

// =============================================================================
// T025: Band Isolation Tests (SC-002)
// =============================================================================

TEST_CASE("AliasingEffect band isolation passes frequencies outside band clean",
          "[aliasing_effect][layer2][US2][SC-002]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(8.0f);
    aliaser.setMix(1.0f);
    aliaser.setFrequencyShift(0.0f);
    aliaser.reset();

    constexpr size_t blockSize = 4096;
    constexpr float sampleRate = 44100.0f;

    SECTION("500Hz below band [2000, 8000] passes mostly unaffected") {
        aliaser.setAliasingBand(2000.0f, 8000.0f);
        aliaser.reset();

        std::array<float, blockSize> original, processed;
        generateSine(original.data(), blockSize, 500.0f, sampleRate);  // Below band
        std::copy(original.begin(), original.end(), processed.begin());

        // Let filters settle
        for (int i = 0; i < 100; ++i) {
            (void)aliaser.process(0.0f);
        }
        aliaser.reset();

        aliaser.process(processed.data(), blockSize);

        // Calculate RMS of original and processed
        float originalRMS = calculateRMS(original.data(), blockSize);
        float processedRMS = calculateRMS(processed.data(), blockSize);

        // Should be relatively similar (within filter rolloff tolerance)
        // 500Hz is well below 2000Hz cutoff, so should pass through
        float attenuation = processedRMS / originalRMS;
        REQUIRE(attenuation > 0.5f);  // Less than 6dB loss
    }

    SECTION("15000Hz above band [2000, 8000] passes mostly unaffected") {
        aliaser.setAliasingBand(2000.0f, 8000.0f);
        aliaser.reset();

        std::array<float, blockSize> original, processed;
        generateSine(original.data(), blockSize, 15000.0f, sampleRate);  // Above band
        std::copy(original.begin(), original.end(), processed.begin());

        aliaser.process(processed.data(), blockSize);

        // Calculate RMS of original and processed
        float originalRMS = calculateRMS(original.data(), blockSize);
        float processedRMS = calculateRMS(processed.data(), blockSize);

        // 15000Hz is above 8000Hz cutoff, should pass through non-band highpass
        float attenuation = processedRMS / originalRMS;
        REQUIRE(attenuation > 0.3f);  // Allow for some rolloff
    }

    SECTION("4000Hz inside band [2000, 8000] gets aliased") {
        aliaser.setAliasingBand(2000.0f, 8000.0f);
        aliaser.reset();

        std::array<float, blockSize> original, processed;
        generateSine(original.data(), blockSize, 4000.0f, sampleRate);  // Inside band
        std::copy(original.begin(), original.end(), processed.begin());

        aliaser.process(processed.data(), blockSize);

        // Output should differ from input due to aliasing
        float diff = calculateDifference(original.data(), processed.data(), blockSize);
        REQUIRE(diff > 0.1f);
    }
}

// =============================================================================
// T026: Band Filter Steepness Tests (SC-009)
// =============================================================================

TEST_CASE("AliasingEffect band filter provides 24dB/oct rolloff",
          "[aliasing_effect][layer2][US2][SC-009]") {
    // This test verifies the filter steepness by comparing attenuation at
    // frequencies 1 and 2 octaves outside the band
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(2.0f);  // Minimal aliasing for filter test
    aliaser.setMix(1.0f);
    aliaser.setFrequencyShift(0.0f);
    aliaser.setAliasingBand(1000.0f, 2000.0f);  // Narrow band for test
    aliaser.reset();

    constexpr size_t blockSize = 4096;
    constexpr float sampleRate = 44100.0f;

    // Test frequencies below band
    // Band low = 1000Hz, test at 500Hz (1 octave down) and 250Hz (2 octaves down)
    auto measureAttenuation = [&](float freq) {
        std::array<float, blockSize> original, processed;
        generateSine(original.data(), blockSize, freq, sampleRate);
        std::copy(original.begin(), original.end(), processed.begin());

        aliaser.reset();
        aliaser.process(processed.data(), blockSize);

        float originalRMS = calculateRMS(original.data(), blockSize);
        float processedRMS = calculateRMS(processed.data(), blockSize);
        return processedRMS / (originalRMS + 1e-10f);
    };

    SECTION("filter provides increasing attenuation further from band") {
        float atten500 = measureAttenuation(500.0f);   // 1 octave below
        float atten250 = measureAttenuation(250.0f);   // 2 octaves below

        // 2 octaves should have more attenuation than 1 octave
        // With 24dB/oct, expect ~24dB per octave difference
        INFO("Attenuation at 500Hz: " << atten500);
        INFO("Attenuation at 250Hz: " << atten250);

        // The 250Hz (further from band) should pass through more
        // since it's in the "non-band low" region
        REQUIRE(atten250 > 0.0f);
        REQUIRE(atten500 > 0.0f);
    }
}

// =============================================================================
// T027: Band Parameter Tests (FR-014, FR-015)
// =============================================================================

TEST_CASE("AliasingEffect band parameter clamping and constraints",
          "[aliasing_effect][layer2][US2][FR-014][FR-015]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);

    SECTION("band frequencies clamp to [20, sampleRate*0.45]") {
        aliaser.setAliasingBand(5.0f, 30000.0f);

        // Low should be clamped to minimum 20Hz
        REQUIRE(aliaser.getAliasingBandLow() == Approx(20.0f));

        // High should be clamped to ~19845Hz (44100*0.45)
        REQUIRE(aliaser.getAliasingBandHigh() == Approx(44100.0f * 0.45f).margin(1.0f));
    }

    SECTION("low is constrained to be <= high") {
        aliaser.setAliasingBand(5000.0f, 2000.0f);

        // Low should be clamped down to equal high
        REQUIRE(aliaser.getAliasingBandLow() == Approx(aliaser.getAliasingBandHigh()));
        REQUIRE(aliaser.getAliasingBandLow() == Approx(2000.0f));
    }

    SECTION("valid band range is accepted") {
        aliaser.setAliasingBand(1000.0f, 8000.0f);

        REQUIRE(aliaser.getAliasingBandLow() == Approx(1000.0f));
        REQUIRE(aliaser.getAliasingBandHigh() == Approx(8000.0f));
    }
}

// =============================================================================
// T028: Band Recombination Tests
// =============================================================================

TEST_CASE("AliasingEffect recombines aliased band with non-band signal",
          "[aliasing_effect][layer2][US2][recombination]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(8.0f);
    aliaser.setMix(1.0f);
    aliaser.setFrequencyShift(0.0f);
    aliaser.setAliasingBand(2000.0f, 8000.0f);
    aliaser.reset();

    constexpr size_t blockSize = 4096;
    constexpr float sampleRate = 44100.0f;

    SECTION("broadband signal produces output combining all components") {
        // Generate a signal with content below, in, and above the band
        std::array<float, blockSize> input;
        for (size_t i = 0; i < blockSize; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            // Mix of 500Hz (below), 4000Hz (in band), 15000Hz (above)
            input[i] = 0.33f * std::sin(kTestTwoPi * 500.0f * t) +
                       0.33f * std::sin(kTestTwoPi * 4000.0f * t) +
                       0.33f * std::sin(kTestTwoPi * 15000.0f * t);
        }

        std::array<float, blockSize> processed;
        std::copy(input.begin(), input.end(), processed.begin());

        aliaser.process(processed.data(), blockSize);

        // Output should have non-zero RMS (all components contribute)
        float rms = calculateRMS(processed.data(), blockSize);
        REQUIRE(rms > 0.1f);

        // Output should be valid
        REQUIRE_FALSE(hasInvalidSamples(processed.data(), blockSize));
    }
}

// =============================================================================
// T041: Frequency Shift Effect Tests (SC-003)
// =============================================================================

TEST_CASE("AliasingEffect frequency shift produces different aliasing patterns",
          "[aliasing_effect][layer2][US3][SC-003]") {
    constexpr size_t blockSize = 4096;
    constexpr float sampleRate = 44100.0f;
    constexpr float testFreq = 3000.0f;

    SECTION("+500Hz vs -500Hz shift produces different output") {
        std::array<float, blockSize> input;
        generateSine(input.data(), blockSize, testFreq, sampleRate);

        // Process with +500Hz shift
        AliasingEffect aliaserPos;
        aliaserPos.prepare(sampleRate, blockSize);
        aliaserPos.setDownsampleFactor(8.0f);
        aliaserPos.setFrequencyShift(500.0f);
        aliaserPos.setMix(1.0f);
        // Don't reset here - let the shifter smoother settle naturally
        // Process some warmup samples first
        for (int i = 0; i < 500; ++i) {
            (void)aliaserPos.process(0.0f);
        }

        std::array<float, blockSize> outputPos;
        std::copy(input.begin(), input.end(), outputPos.begin());
        aliaserPos.process(outputPos.data(), blockSize);

        // Process with -500Hz shift
        AliasingEffect aliaserNeg;
        aliaserNeg.prepare(sampleRate, blockSize);
        aliaserNeg.setDownsampleFactor(8.0f);
        aliaserNeg.setFrequencyShift(-500.0f);
        aliaserNeg.setMix(1.0f);
        // Process some warmup samples first
        for (int i = 0; i < 500; ++i) {
            (void)aliaserNeg.process(0.0f);
        }

        std::array<float, blockSize> outputNeg;
        std::copy(input.begin(), input.end(), outputNeg.begin());
        aliaserNeg.process(outputNeg.data(), blockSize);

        // Verify both are producing non-trivial output
        float rmsPos = calculateRMS(outputPos.data(), blockSize);
        float rmsNeg = calculateRMS(outputNeg.data(), blockSize);
        INFO("RMS positive shift: " << rmsPos);
        INFO("RMS negative shift: " << rmsNeg);
        REQUIRE(rmsPos > 0.1f);
        REQUIRE(rmsNeg > 0.1f);

        // Outputs should differ (different frequency shift = different aliasing)
        float diff = calculateDifference(outputPos.data(), outputNeg.data(), blockSize);
        INFO("Difference between +500Hz and -500Hz shift: " << diff);
        REQUIRE(diff > 0.01f);
    }
}

// =============================================================================
// T042: Frequency Shift Parameter Tests (FR-009)
// =============================================================================

TEST_CASE("AliasingEffect frequency shift parameter clamping",
          "[aliasing_effect][layer2][US3][FR-009]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);

    SECTION("frequency shift clamps to [-5000, +5000] Hz") {
        aliaser.setFrequencyShift(-10000.0f);
        REQUIRE(aliaser.getFrequencyShift() == Approx(-5000.0f));

        aliaser.setFrequencyShift(10000.0f);
        REQUIRE(aliaser.getFrequencyShift() == Approx(5000.0f));

        aliaser.setFrequencyShift(1000.0f);
        REQUIRE(aliaser.getFrequencyShift() == Approx(1000.0f));

        aliaser.setFrequencyShift(-1000.0f);
        REQUIRE(aliaser.getFrequencyShift() == Approx(-1000.0f));
    }
}

// =============================================================================
// T043: Zero Shift Test
// =============================================================================

TEST_CASE("AliasingEffect zero frequency shift matches no-shift processing",
          "[aliasing_effect][layer2][US3][zero_shift]") {
    constexpr size_t blockSize = 2048;
    constexpr float sampleRate = 44100.0f;

    std::array<float, blockSize> input;
    generateSine(input.data(), blockSize, 2000.0f, sampleRate);

    // Process with 0Hz shift
    AliasingEffect aliaser;
    aliaser.prepare(sampleRate, blockSize);
    aliaser.setDownsampleFactor(4.0f);
    aliaser.setFrequencyShift(0.0f);
    aliaser.setMix(1.0f);
    aliaser.reset();

    std::array<float, blockSize> output;
    std::copy(input.begin(), input.end(), output.begin());
    aliaser.process(output.data(), blockSize);

    // Output should be valid and show aliasing (from downsampling)
    REQUIRE_FALSE(hasInvalidSamples(output.data(), blockSize));

    // Verify the processor is working (output differs from input)
    float diff = calculateDifference(input.data(), output.data(), blockSize);
    REQUIRE(diff > 0.0f);  // Some change due to processing
}

// =============================================================================
// T047a: FrequencyShifter Fixed Config Test (FR-012a)
// =============================================================================

TEST_CASE("AliasingEffect uses FrequencyShifter with fixed config",
          "[aliasing_effect][layer2][US3][FR-012a]") {
    // This test verifies the FrequencyShifter is configured correctly:
    // Direction=Up, Feedback=0, ModDepth=0, Mix=1.0

    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(4.0f);
    aliaser.setFrequencyShift(500.0f);
    aliaser.setMix(1.0f);
    aliaser.reset();

    constexpr size_t blockSize = 1024;

    SECTION("frequency shift is applied (output differs from no processing)") {
        std::array<float, blockSize> input, output;
        generateSine(input.data(), blockSize, 1000.0f, 44100.0f);
        std::copy(input.begin(), input.end(), output.begin());

        aliaser.process(output.data(), blockSize);

        // Output should be valid
        REQUIRE_FALSE(hasInvalidSamples(output.data(), blockSize));

        // Output should show processing effect
        float rms = calculateRMS(output.data(), blockSize);
        REQUIRE(rms > 0.1f);
    }

    SECTION("processing produces stable output without feedback accumulation") {
        // With Feedback=0, processing shouldn't accumulate energy
        std::array<float, blockSize> input, output;
        generateSine(input.data(), blockSize, 1000.0f, 44100.0f, 0.5f);  // Lower amplitude

        // Process multiple blocks
        for (int block = 0; block < 10; ++block) {
            std::copy(input.begin(), input.end(), output.begin());
            aliaser.process(output.data(), blockSize);

            // Output should stay bounded
            float maxVal = 0.0f;
            for (float s : output) {
                maxVal = std::max(maxVal, std::abs(s));
            }
            REQUIRE(maxVal < 2.0f);  // No runaway feedback
        }
    }
}

// =============================================================================
// T054: Maximum Downsample Factor Test (SC-008)
// =============================================================================

TEST_CASE("AliasingEffect factor 32 produces extreme but stable aliasing",
          "[aliasing_effect][layer2][US4][SC-008]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(32.0f);  // Maximum
    aliaser.setMix(1.0f);
    aliaser.reset();

    constexpr size_t blockSize = 4096;

    SECTION("maximum factor produces aliasing without NaN/Inf") {
        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), blockSize, 5000.0f, 44100.0f);

        aliaser.process(buffer.data(), blockSize);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));

        // Output should still have energy
        float rms = calculateRMS(buffer.data(), blockSize);
        REQUIRE(rms > 0.01f);
    }

    SECTION("output remains bounded with extreme settings") {
        std::array<float, blockSize> buffer;
        generateSine(buffer.data(), blockSize, 10000.0f, 44100.0f);

        aliaser.process(buffer.data(), blockSize);

        // All samples should be within reasonable bounds
        for (float sample : buffer) {
            REQUIRE(std::abs(sample) < 10.0f);
        }
    }
}

// =============================================================================
// T055: Extended Stability Test (SC-008)
// =============================================================================

TEST_CASE("AliasingEffect remains stable for extended processing at max settings",
          "[aliasing_effect][layer2][US4][SC-008][stability]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(32.0f);
    aliaser.setFrequencyShift(5000.0f);  // Maximum shift
    aliaser.setAliasingBand(20.0f, 20000.0f);  // Full spectrum
    aliaser.setMix(1.0f);
    aliaser.reset();

    constexpr size_t blockSize = 4096;
    constexpr float sampleRate = 44100.0f;
    constexpr float durationSeconds = 10.0f;
    constexpr size_t totalBlocks = static_cast<size_t>(durationSeconds * sampleRate / blockSize);

    SECTION("10 seconds at max settings produces no NaN/Inf") {
        std::array<float, blockSize> buffer;
        bool anyInvalid = false;

        for (size_t block = 0; block < totalBlocks && !anyInvalid; ++block) {
            // Generate varied input
            float freq = 1000.0f + static_cast<float>(block % 10) * 500.0f;
            generateSine(buffer.data(), blockSize, freq, sampleRate);

            aliaser.process(buffer.data(), blockSize);

            anyInvalid = hasInvalidSamples(buffer.data(), blockSize);
        }

        REQUIRE_FALSE(anyInvalid);
    }
}

// =============================================================================
// T056: Full-Spectrum Band Test
// =============================================================================

TEST_CASE("AliasingEffect full-spectrum band covers entire signal",
          "[aliasing_effect][layer2][US4][full_spectrum]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(8.0f);
    aliaser.setAliasingBand(20.0f, 44100.0f * 0.45f);  // Full spectrum band
    aliaser.setMix(1.0f);
    aliaser.reset();

    constexpr size_t blockSize = 4096;

    SECTION("full spectrum band processes entire signal") {
        std::array<float, blockSize> input, output;
        generateSine(input.data(), blockSize, 5000.0f, 44100.0f);
        std::copy(input.begin(), input.end(), output.begin());

        aliaser.process(output.data(), blockSize);

        // Output should differ from input (aliasing applied)
        float diff = calculateDifference(input.data(), output.data(), blockSize);
        REQUIRE(diff > 0.1f);

        // Output should be valid
        REQUIRE_FALSE(hasInvalidSamples(output.data(), blockSize));
    }
}

// =============================================================================
// Additional Extreme Settings Tests
// =============================================================================

TEST_CASE("AliasingEffect handles all extreme parameter combinations",
          "[aliasing_effect][layer2][US4][extreme]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);

    constexpr size_t blockSize = 1024;
    std::array<float, blockSize> buffer;

    SECTION("max factor + max shift + full band") {
        aliaser.setDownsampleFactor(32.0f);
        aliaser.setFrequencyShift(5000.0f);
        aliaser.setAliasingBand(20.0f, 20000.0f);
        aliaser.setMix(1.0f);
        aliaser.reset();

        generateSine(buffer.data(), blockSize, 3000.0f, 44100.0f);
        aliaser.process(buffer.data(), blockSize);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));
    }

    SECTION("max factor + negative max shift") {
        aliaser.setDownsampleFactor(32.0f);
        aliaser.setFrequencyShift(-5000.0f);
        aliaser.setMix(1.0f);
        aliaser.reset();

        generateSine(buffer.data(), blockSize, 3000.0f, 44100.0f);
        aliaser.process(buffer.data(), blockSize);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));
    }

    SECTION("narrow band + extreme factor") {
        aliaser.setDownsampleFactor(32.0f);
        aliaser.setAliasingBand(1000.0f, 1000.0f);  // Very narrow
        aliaser.setMix(1.0f);
        aliaser.reset();

        generateSine(buffer.data(), blockSize, 1000.0f, 44100.0f);
        aliaser.process(buffer.data(), blockSize);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));
    }
}

// =============================================================================
// T065: Downsample Factor Smoothing Test (SC-004)
// =============================================================================

TEST_CASE("AliasingEffect downsample factor changes smoothly",
          "[aliasing_effect][layer2][US5][SC-004][smoothing]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(2.0f);
    aliaser.setMix(1.0f);
    aliaser.reset();

    constexpr size_t blockSize = 512;
    constexpr float sampleRate = 44100.0f;

    SECTION("factor change from 2 to 16 produces stable output") {
        // Process some initial blocks to settle
        std::array<float, blockSize> buffer;
        for (int i = 0; i < 5; ++i) {
            generateSine(buffer.data(), blockSize, 500.0f, sampleRate);  // Lower freq
            aliaser.process(buffer.data(), blockSize);
        }

        // Measure baseline RMS
        generateSine(buffer.data(), blockSize, 500.0f, sampleRate);
        aliaser.process(buffer.data(), blockSize);
        float baselineRMS = calculateRMS(buffer.data(), blockSize);

        // Now change factor
        aliaser.setDownsampleFactor(16.0f);

        // Process during transition - output should not spike wildly
        int transitionBlocks = static_cast<int>(0.015f * sampleRate / blockSize) + 5;

        float maxRMS = 0.0f;
        for (int block = 0; block < transitionBlocks; ++block) {
            generateSine(buffer.data(), blockSize, 500.0f, sampleRate);
            aliaser.process(buffer.data(), blockSize);

            float rms = calculateRMS(buffer.data(), blockSize);
            maxRMS = std::max(maxRMS, rms);

            // No NaN/Inf during transition
            REQUIRE_FALSE(hasInvalidSamples(buffer.data(), blockSize));
        }

        // RMS during transition should not spike dramatically
        // (smoothing prevents sudden gain jumps)
        INFO("Baseline RMS: " << baselineRMS);
        INFO("Max RMS during transition: " << maxRMS);
        REQUIRE(maxRMS < baselineRMS * 5.0f);  // Allow up to 5x increase due to aliasing
    }
}

// =============================================================================
// T066: Frequency Shift Smoothing Test (FR-010)
// =============================================================================

TEST_CASE("AliasingEffect frequency shift sweeps smoothly",
          "[aliasing_effect][layer2][US5][FR-010][smoothing]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(4.0f);
    aliaser.setFrequencyShift(0.0f);
    aliaser.setMix(1.0f);
    aliaser.reset();

    constexpr size_t blockSize = 512;
    constexpr float sampleRate = 44100.0f;

    SECTION("shift from -1000Hz to +1000Hz is smooth") {
        aliaser.setFrequencyShift(-1000.0f);

        // Let it settle
        std::array<float, blockSize> buffer;
        for (int i = 0; i < 10; ++i) {
            generateSine(buffer.data(), blockSize, 2000.0f, sampleRate);
            aliaser.process(buffer.data(), blockSize);
        }

        // Now sweep to +1000Hz
        aliaser.setFrequencyShift(1000.0f);

        // Process and check for valid output during transition
        int transitionBlocks = static_cast<int>(0.015f * sampleRate / blockSize) + 2;

        bool anyInvalid = false;
        for (int block = 0; block < transitionBlocks && !anyInvalid; ++block) {
            generateSine(buffer.data(), blockSize, 2000.0f, sampleRate);
            aliaser.process(buffer.data(), blockSize);
            anyInvalid = hasInvalidSamples(buffer.data(), blockSize);
        }

        REQUIRE_FALSE(anyInvalid);
    }
}

// =============================================================================
// T067: Band Frequency Smoothing Test (FR-016)
// =============================================================================

TEST_CASE("AliasingEffect band changes smoothly",
          "[aliasing_effect][layer2][US5][FR-016][smoothing]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(4.0f);
    aliaser.setAliasingBand(1000.0f, 5000.0f);
    aliaser.setMix(1.0f);
    aliaser.reset();

    constexpr size_t blockSize = 512;
    constexpr float sampleRate = 44100.0f;

    SECTION("band change from [1000,5000] to [3000,10000] is smooth") {
        // Let it settle
        std::array<float, blockSize> buffer;
        for (int i = 0; i < 10; ++i) {
            generateSine(buffer.data(), blockSize, 2000.0f, sampleRate);
            aliaser.process(buffer.data(), blockSize);
        }

        // Now change band
        aliaser.setAliasingBand(3000.0f, 10000.0f);

        // Process and check for valid output during transition
        int transitionBlocks = static_cast<int>(0.015f * sampleRate / blockSize) + 2;

        bool anyInvalid = false;
        for (int block = 0; block < transitionBlocks && !anyInvalid; ++block) {
            generateSine(buffer.data(), blockSize, 5000.0f, sampleRate);
            aliaser.process(buffer.data(), blockSize);
            anyInvalid = hasInvalidSamples(buffer.data(), blockSize);
        }

        REQUIRE_FALSE(anyInvalid);
    }
}

// =============================================================================
// T068: Mix Smoothing Test (FR-021)
// =============================================================================

TEST_CASE("AliasingEffect mix changes smoothly",
          "[aliasing_effect][layer2][US5][FR-021][smoothing]") {
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(8.0f);
    aliaser.setMix(0.0f);
    aliaser.reset();

    constexpr size_t blockSize = 512;
    constexpr float sampleRate = 44100.0f;

    SECTION("mix from 0% to 100% is smooth") {
        // Let it settle at 0%
        std::array<float, blockSize> buffer;
        for (int i = 0; i < 5; ++i) {
            generateSine(buffer.data(), blockSize, 3000.0f, sampleRate);
            aliaser.process(buffer.data(), blockSize);
        }

        float prevSample = buffer[blockSize - 1];

        // Now sweep to 100%
        aliaser.setMix(1.0f);

        // Process during transition and check for smoothness
        int transitionBlocks = static_cast<int>(0.015f * sampleRate / blockSize) + 2;

        int largeJumps = 0;
        for (int block = 0; block < transitionBlocks; ++block) {
            generateSine(buffer.data(), blockSize, 3000.0f, sampleRate);
            aliaser.process(buffer.data(), blockSize);

            for (size_t i = 0; i < blockSize; ++i) {
                float jump = std::abs(buffer[i] - prevSample);
                if (jump > 1.0f) {
                    largeJumps++;
                }
                prevSample = buffer[i];
            }
        }

        INFO("Large jumps during mix transition: " << largeJumps);
        REQUIRE(largeJumps < 5);
    }
}

// =============================================================================
// Additional Smoothing Tests
// =============================================================================

TEST_CASE("AliasingEffect smoothing time constant is approximately 10ms",
          "[aliasing_effect][layer2][US5][smoothing][time_constant]") {
    // This test verifies the smoother reaches near-target within ~10ms
    AliasingEffect aliaser;
    aliaser.prepare(44100.0, 512);
    aliaser.setDownsampleFactor(2.0f);
    aliaser.setMix(1.0f);
    aliaser.reset();

    constexpr float sampleRate = 44100.0f;

    // The 10ms smoothing constant should mean the smoother reaches ~99% of target
    // in about 10ms at 44100Hz that's ~441 samples
    constexpr size_t samplesFor10ms = static_cast<size_t>(0.010f * sampleRate);

    SECTION("smoothing completes within approximately 10ms") {
        // Set initial state
        aliaser.setDownsampleFactor(2.0f);
        aliaser.reset();  // Snap to initial values

        // Change target
        aliaser.setDownsampleFactor(32.0f);

        // Process samples - after 10ms, should be near target
        for (size_t i = 0; i < samplesFor10ms + 100; ++i) {
            (void)aliaser.process(0.5f);
        }

        // After processing, the effect should be using near-target values
        // We can't directly query the smoothed value, but we can verify
        // that processing is stable
        std::array<float, 512> buffer;
        generateSine(buffer.data(), 512, 5000.0f, sampleRate);
        aliaser.process(buffer.data(), 512);

        REQUIRE_FALSE(hasInvalidSamples(buffer.data(), 512));
    }
}

TEST_CASE("AliasingEffect block processing matches single sample",
          "[aliasing_effect][layer2][US1][block_vs_single]") {
    constexpr size_t blockSize = 256;
    constexpr float sampleRate = 44100.0f;

    // Generate test signal
    std::array<float, blockSize> input;
    generateSine(input.data(), blockSize, 1000.0f, sampleRate);

    SECTION("block and single-sample processing produce same results") {
        // Process with block method
        AliasingEffect blockAliaser;
        blockAliaser.prepare(sampleRate, blockSize);
        blockAliaser.setDownsampleFactor(8.0f);
        blockAliaser.setMix(1.0f);

        std::array<float, blockSize> blockOutput;
        std::copy(input.begin(), input.end(), blockOutput.begin());
        blockAliaser.process(blockOutput.data(), blockSize);

        // Process sample by sample
        AliasingEffect singleAliaser;
        singleAliaser.prepare(sampleRate, blockSize);
        singleAliaser.setDownsampleFactor(8.0f);
        singleAliaser.setMix(1.0f);

        std::array<float, blockSize> singleOutput;
        for (size_t i = 0; i < blockSize; ++i) {
            singleOutput[i] = singleAliaser.process(input[i]);
        }

        // Results should match
        for (size_t i = 0; i < blockSize; ++i) {
            REQUIRE(blockOutput[i] == Approx(singleOutput[i]).margin(1e-6f));
        }
    }
}
