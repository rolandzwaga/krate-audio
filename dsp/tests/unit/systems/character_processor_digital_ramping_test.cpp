// ==============================================================================
// CharacterProcessor Digital Vintage Mode Ramping Investigation
// ==============================================================================
// This test isolates the CharacterProcessor to determine if the ramping issue
// is within CharacterProcessor itself or in DigitalDelay's integration.
//
// If CharacterProcessor shows ramping: Problem is in CharacterProcessor
// If CharacterProcessor is stable: Problem is in DigitalDelay wiring
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/systems/character_processor.h>
#include <array>
#include <cmath>
#include <algorithm>

using Catch::Approx;
using namespace Krate::DSP;

namespace {
constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr size_t kTestBufferSize = 10000;

float measureMean(const float* buffer, size_t start, size_t length) {
    float sum = 0.0f;
    for (size_t i = start; i < start + length; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(length);
}
}

// ==============================================================================
// Test 1: CharacterProcessor DigitalVintage Mode with 16-bit
// ==============================================================================

TEST_CASE("CharacterProcessor DigitalVintage 16-bit produces stable output",
          "[systems][character-processor][digital][ramping]") {
    // Test CharacterProcessor in isolation with same settings as DigitalDelay Age 0%
    // Age 0% = 16-bit quantization

    CharacterProcessor character;
    character.prepare(kSampleRate, kBlockSize);
    character.setMode(CharacterMode::DigitalVintage);
    character.setDigitalBitDepth(16.0f);       // Same as Age 0%
    character.setDigitalDitherAmount(0.5f);    // Default dither
    character.setDigitalSampleRateReduction(1.0f); // No SR reduction
    character.reset(); // Snap crossfade state to avoid transient

    std::array<float, kTestBufferSize> buffer{};
    std::fill(buffer.begin(), buffer.end(), 0.5f); // Constant input

    character.process(buffer.data(), kTestBufferSize);

    // Measure mean over first quarter vs last quarter
    float mean1 = measureMean(buffer.data(), 0, kTestBufferSize / 4);
    float mean2 = measureMean(buffer.data(), kTestBufferSize * 3 / 4, kTestBufferSize / 4);

    INFO("First quarter mean: " << mean1);
    INFO("Last quarter mean: " << mean2);
    INFO("Difference: " << std::abs(mean1 - mean2));

    // Should not ramp
    REQUIRE(std::abs(mean1 - mean2) < 0.01f);

    // Output should be close to input (0.5)
    REQUIRE(mean1 == Approx(0.5f).margin(0.05f));
}

// ==============================================================================
// Test 2: Stereo Processing
// ==============================================================================

TEST_CASE("CharacterProcessor DigitalVintage stereo produces stable output",
          "[systems][character-processor][digital][ramping][stereo]") {
    // Test stereo processing (same as DigitalDelay uses)

    CharacterProcessor character;
    character.prepare(kSampleRate, kBlockSize);
    character.setMode(CharacterMode::DigitalVintage);
    character.setDigitalBitDepth(16.0f);
    character.setDigitalDitherAmount(0.5f);
    character.setDigitalSampleRateReduction(1.0f);
    character.reset(); // Snap crossfade state to avoid transient

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};
    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    character.processStereo(left.data(), right.data(), kTestBufferSize);

    // Measure left channel
    float meanL1 = measureMean(left.data(), 0, kTestBufferSize / 4);
    float meanL2 = measureMean(left.data(), kTestBufferSize * 3 / 4, kTestBufferSize / 4);

    // Measure right channel
    float meanR1 = measureMean(right.data(), 0, kTestBufferSize / 4);
    float meanR2 = measureMean(right.data(), kTestBufferSize * 3 / 4, kTestBufferSize / 4);

    INFO("Left: first=" << meanL1 << " last=" << meanL2 << " diff=" << std::abs(meanL1 - meanL2));
    INFO("Right: first=" << meanR1 << " last=" << meanR2 << " diff=" << std::abs(meanR1 - meanR2));

    // Neither channel should ramp
    REQUIRE(std::abs(meanL1 - meanL2) < 0.01f);
    REQUIRE(std::abs(meanR1 - meanR2) < 0.01f);

    // Both should be close to input
    REQUIRE(meanL1 == Approx(0.5f).margin(0.05f));
    REQUIRE(meanR1 == Approx(0.5f).margin(0.05f));
}

// ==============================================================================
// Test 3: Early Samples Analysis
// ==============================================================================

TEST_CASE("CharacterProcessor DigitalVintage early samples behavior",
          "[systems][character-processor][digital][ramping][diagnostic]") {
    // Check if ramping starts immediately or gradually

    CharacterProcessor character;
    character.prepare(kSampleRate, kBlockSize);
    character.setMode(CharacterMode::DigitalVintage);
    character.setDigitalBitDepth(16.0f);
    character.setDigitalDitherAmount(0.5f);  // Re-enable dither to test if RNG fix resolved ramping
    character.setDigitalSampleRateReduction(1.0f);
    character.reset(); // Snap crossfade state

    std::array<float, kTestBufferSize> buffer{};
    std::fill(buffer.begin(), buffer.end(), 0.5f);

    character.process(buffer.data(), kTestBufferSize);

    // Measure at different time points
    float mean_0_10 = measureMean(buffer.data(), 0, 10);
    float mean_100_110 = measureMean(buffer.data(), 100, 10);
    float mean_500_510 = measureMean(buffer.data(), 500, 10);
    float mean_1000_1010 = measureMean(buffer.data(), 1000, 10);

    INFO("Mean at samples 0-10: " << mean_0_10);
    INFO("Mean at samples 100-110: " << mean_100_110);
    INFO("Mean at samples 500-510: " << mean_500_510);
    INFO("Mean at samples 1000-1010: " << mean_1000_1010);
    INFO("Early change (0→100): " << std::abs(mean_0_10 - mean_100_110));
    INFO("Mid change (100→500): " << std::abs(mean_100_110 - mean_500_510));
    INFO("Late change (500→1000): " << std::abs(mean_500_510 - mean_1000_1010));

    // All should be stable
    REQUIRE(std::abs(mean_0_10 - mean_1000_1010) < 0.01f);
}
