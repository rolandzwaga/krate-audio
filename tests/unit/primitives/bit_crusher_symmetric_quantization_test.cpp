// ==============================================================================
// BitCrusher Symmetric Quantization Fix - Test-First Implementation
// ==============================================================================
// This test documents the CORRECT symmetric quantization behavior we want.
// The current implementation has asymmetric bias that accumulates in feedback loops.
//
// Root cause: Round-trip through [0,1] introduces asymmetry
// Fix: Use symmetric quantization around zero
// ==============================================================================

#include "dsp/primitives/bit_crusher.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>

using namespace Iterum::DSP;
using Catch::Approx;

namespace {
constexpr double kSampleRate = 44100.0;
}

// ==============================================================================
// Test 1: Zero Maps to Zero (CRITICAL for DC offset prevention)
// ==============================================================================

TEST_CASE("BitCrusher: Zero input produces zero output at all bit depths", "[primitives][bit-crusher][fix][zero-mapping]") {
    // CRITICAL: Zero MUST map to zero for DC-free operation
    // Current bug: 0.0f → 0.00002f (creates accumulation in feedback)
    // Expected: 0.0f → 0.0f

    BitCrusher crusher;
    crusher.prepare(kSampleRate);
    crusher.setDither(0.0f);

    std::array<float, 10> buffer{};

    SECTION("16-bit") {
        crusher.setBitDepth(16.0f);
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        crusher.process(buffer.data(), 10);

        for (size_t i = 0; i < 10; ++i) {
            REQUIRE(buffer[i] == 0.0f);
        }
    }

    SECTION("10-bit") {
        crusher.setBitDepth(10.0f);
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        crusher.process(buffer.data(), 10);

        for (size_t i = 0; i < 10; ++i) {
            REQUIRE(buffer[i] == 0.0f);
        }
    }

    SECTION("4-bit") {
        crusher.setBitDepth(4.0f);
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        crusher.process(buffer.data(), 10);

        for (size_t i = 0; i < 10; ++i) {
            REQUIRE(buffer[i] == 0.0f);
        }
    }
}

// ==============================================================================
// Test 2: Perfect Symmetry Around Zero
// ==============================================================================

TEST_CASE("BitCrusher: Quantization is perfectly symmetric around zero", "[primitives][bit-crusher][fix][symmetry]") {
    // CRITICAL: abs(quantize(+x)) == abs(quantize(-x))
    // Current bug: Asymmetric quantization creates directional bias
    // Expected: Perfect symmetry

    BitCrusher crusher;
    crusher.prepare(kSampleRate);
    crusher.setBitDepth(8.0f);
    crusher.setDither(0.0f);

    constexpr size_t kTestPoints = 100;
    std::array<float, kTestPoints> posBuffer{};
    std::array<float, kTestPoints> negBuffer{};

    // Test range: -0.9 to +0.9 (avoid edge cases near ±1.0)
    for (size_t i = 0; i < kTestPoints; ++i) {
        float value = -0.9f + (i / static_cast<float>(kTestPoints - 1)) * 1.8f;
        posBuffer[i] = std::abs(value);
        negBuffer[i] = -std::abs(value);
    }

    crusher.process(posBuffer.data(), kTestPoints);
    crusher.reset(); // Reset RNG for identical dither
    crusher.process(negBuffer.data(), kTestPoints);

    // Verify perfect symmetry
    for (size_t i = 0; i < kTestPoints; ++i) {
        float pos = posBuffer[i];
        float neg = negBuffer[i];
        float diff = std::abs(std::abs(pos) - std::abs(neg));

        // Allow for floating-point precision
        REQUIRE(diff < 0.0001f);
    }
}

// ==============================================================================
// Test 3: Quantization Levels Are Symmetric
// ==============================================================================

TEST_CASE("BitCrusher: Quantization levels are symmetric around zero", "[primitives][bit-crusher][fix][levels]") {
    // At N-bit depth, there should be (2^N - 1) levels
    // These levels should be symmetrically distributed: -1.0 ... 0.0 ... +1.0
    //
    // For 4-bit (15 levels):
    // Levels: -1.0, -6/7, -5/7, -4/7, -3/7, -2/7, -1/7, 0, +1/7, +2/7, +3/7, +4/7, +5/7, +6/7, +1.0
    // That's 7 negative, 1 zero, 7 positive = perfectly symmetric

    BitCrusher crusher;
    crusher.prepare(kSampleRate);
    crusher.setBitDepth(4.0f); // 15 levels
    crusher.setDither(0.0f);

    // Test that input 0.5 quantizes to a level that has a symmetric negative counterpart
    std::array<float, 2> buffer = {0.5f, -0.5f};
    crusher.process(buffer.data(), 2);

    // The outputs should have equal absolute values
    REQUIRE(std::abs(buffer[0]) == Approx(std::abs(buffer[1])).margin(0.0001f));
}

// ==============================================================================
// Test 4: No DC Bias with Constant Input
// ==============================================================================

TEST_CASE("BitCrusher: Constant input produces constant output (no integration)", "[primitives][bit-crusher][fix][dc-bias]") {
    // Test that processing a constant signal doesn't create cumulative drift
    // This would happen if quantization had directional bias

    BitCrusher crusher;
    crusher.prepare(kSampleRate);
    crusher.setBitDepth(16.0f);
    crusher.setDither(0.0f);

    constexpr size_t kBufferSize = 4096;
    std::array<float, kBufferSize> buffer{};
    std::fill(buffer.begin(), buffer.end(), 0.3f);

    crusher.process(buffer.data(), kBufferSize);

    // Measure mean over first and last quarters
    float mean1 = 0.0f, mean2 = 0.0f;
    for (size_t i = 0; i < kBufferSize / 4; ++i) {
        mean1 += buffer[i];
        mean2 += buffer[kBufferSize * 3 / 4 + i];
    }
    mean1 /= (kBufferSize / 4);
    mean2 /= (kBufferSize / 4);

    // Should be identical (no drift over time)
    REQUIRE(mean1 == Approx(mean2).margin(0.0001f));
}

// ==============================================================================
// Test 5: Bipolar Signals Have Zero Mean
// ==============================================================================

TEST_CASE("BitCrusher: Symmetric bipolar signal quantizes to zero mean", "[primitives][bit-crusher][fix][bipolar]") {
    // A symmetric bipolar signal (e.g., sine wave) should quantize to zero mean
    // If quantizer has DC bias, mean will be non-zero

    BitCrusher crusher;
    crusher.prepare(kSampleRate);
    crusher.setBitDepth(8.0f);
    crusher.setDither(0.0f);

    constexpr size_t kBufferSize = 1024;
    std::array<float, kBufferSize> buffer{};

    // Generate symmetric bipolar signal (simple square wave)
    for (size_t i = 0; i < kBufferSize; ++i) {
        buffer[i] = (i % 2 == 0) ? 0.7f : -0.7f;
    }

    crusher.process(buffer.data(), kBufferSize);

    // Measure mean
    float mean = 0.0f;
    for (size_t i = 0; i < kBufferSize; ++i) {
        mean += buffer[i];
    }
    mean /= kBufferSize;

    // Mean should be near zero (allow for quantization step)
    REQUIRE(std::abs(mean) < 0.01f);
}
