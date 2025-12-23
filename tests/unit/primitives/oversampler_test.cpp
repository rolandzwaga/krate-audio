// ==============================================================================
// Layer 1: DSP Primitive - Oversampler Tests
// ==============================================================================
// Tests for Oversampler class (2x/4x upsampling/downsampling for anti-aliased
// nonlinear processing).
// Following Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "dsp/primitives/oversampler.h"

#include <array>
#include <cmath>
#include <limits>
#include <numeric>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

// Generate a sine wave at given frequency
void generateSineWave(float* buffer, size_t numSamples, float frequency,
                      float sampleRate, float amplitude = 1.0f) {
    const float omega = 2.0f * 3.14159265358979323846f * frequency / sampleRate;
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(omega * static_cast<float>(i));
    }
}

// Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sumSquares = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(numSamples));
}

// Simple tanh saturation for testing
void applySaturation(float* left, float* right, size_t numSamples, float drive = 2.0f) {
    for (size_t i = 0; i < numSamples; ++i) {
        left[i] = std::tanh(left[i] * drive);
        right[i] = std::tanh(right[i] * drive);
    }
}

} // namespace

// =============================================================================
// Phase 2: Enum Value Tests (T005-T006)
// =============================================================================

TEST_CASE("OversamplingFactor enum values", "[oversampler][enums]") {
    SECTION("TwoX has value 2") {
        REQUIRE(static_cast<int>(OversamplingFactor::TwoX) == 2);
    }

    SECTION("FourX has value 4") {
        REQUIRE(static_cast<int>(OversamplingFactor::FourX) == 4);
    }
}

TEST_CASE("OversamplingQuality enum values", "[oversampler][enums]") {
    SECTION("Economy is defined") {
        REQUIRE(OversamplingQuality::Economy == OversamplingQuality::Economy);
    }

    SECTION("Standard is defined") {
        REQUIRE(OversamplingQuality::Standard == OversamplingQuality::Standard);
    }

    SECTION("High is defined") {
        REQUIRE(OversamplingQuality::High == OversamplingQuality::High);
    }
}

TEST_CASE("OversamplingMode enum values", "[oversampler][enums]") {
    SECTION("ZeroLatency is defined") {
        REQUIRE(OversamplingMode::ZeroLatency == OversamplingMode::ZeroLatency);
    }

    SECTION("LinearPhase is defined") {
        REQUIRE(OversamplingMode::LinearPhase == OversamplingMode::LinearPhase);
    }
}

// =============================================================================
// Phase 3: User Story 1 - Basic 2x Oversampling (T011-T030)
// =============================================================================

TEST_CASE("Oversampler2x default construction", "[oversampler][US1]") {
    Oversampler2x os;

    SECTION("default latency is 0 before prepare") {
        REQUIRE(os.getLatency() == 0);
    }

    SECTION("default factor is 2") {
        REQUIRE(os.getFactor() == 2);
    }
}

TEST_CASE("Oversampler2x prepare()", "[oversampler][US1]") {
    Oversampler2x os;

    SECTION("prepares successfully with valid parameters") {
        REQUIRE_NOTHROW(os.prepare(44100.0, 512));
    }

    SECTION("prepares with different sample rates") {
        REQUIRE_NOTHROW(os.prepare(48000.0, 512));
        REQUIRE_NOTHROW(os.prepare(96000.0, 512));
        REQUIRE_NOTHROW(os.prepare(192000.0, 512));
    }

    SECTION("prepares with different block sizes") {
        REQUIRE_NOTHROW(os.prepare(44100.0, 1));
        REQUIRE_NOTHROW(os.prepare(44100.0, 64));
        REQUIRE_NOTHROW(os.prepare(44100.0, 256));
        REQUIRE_NOTHROW(os.prepare(44100.0, 1024));
        REQUIRE_NOTHROW(os.prepare(44100.0, 8192));
    }

    SECTION("sets latency based on quality") {
        os.prepare(44100.0, 512, OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
        size_t economyLatency = os.getLatency();

        os.prepare(44100.0, 512, OversamplingQuality::Standard);
        size_t standardLatency = os.getLatency();

        os.prepare(44100.0, 512, OversamplingQuality::High);
        size_t highLatency = os.getLatency();

        // Economy with ZeroLatency should have 0 latency
        REQUIRE(economyLatency == 0);
        // Standard and High may have latency (FIR filters)
        // Just verify they're reasonable values
        REQUIRE(standardLatency < 100);
        REQUIRE(highLatency < 100);
    }
}

TEST_CASE("Oversampler2x process() with callback", "[oversampler][US1]") {
    Oversampler2x os;
    os.prepare(44100.0, 512);

    constexpr size_t blockSize = 64;
    std::array<float, blockSize> left{}, right{};

    // Fill with test signal
    generateSineWave(left.data(), blockSize, 1000.0f, 44100.0f);
    generateSineWave(right.data(), blockSize, 1000.0f, 44100.0f);

    SECTION("output buffer size equals input buffer size") {
        std::array<float, blockSize> leftCopy = left;
        std::array<float, blockSize> rightCopy = right;

        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) { /* passthrough */ });

        // Output should have same size (blockSize samples)
        // and be similar to input for passthrough
        float rmsIn = calculateRMS(leftCopy.data(), blockSize);
        float rmsOut = calculateRMS(left.data(), blockSize);

        // RMS should be similar for passthrough (allowing for filter response)
        REQUIRE(rmsOut > rmsIn * 0.5f);
        REQUIRE(rmsOut < rmsIn * 1.5f);
    }

    SECTION("callback receives upsampled buffer") {
        size_t callbackSamples = 0;

        os.process(left.data(), right.data(), blockSize,
            [&callbackSamples](float*, float*, size_t n) {
                callbackSamples = n;
            });

        // Callback should receive 2x samples
        REQUIRE(callbackSamples == blockSize * 2);
    }

    SECTION("applies saturation through callback") {
        // Store original RMS
        float originalRMS = calculateRMS(left.data(), blockSize);

        os.process(left.data(), right.data(), blockSize,
            [](float* L, float* R, size_t n) {
                applySaturation(L, R, n, 4.0f);
            });

        // Saturation should reduce peaks but maintain energy
        float saturatedRMS = calculateRMS(left.data(), blockSize);

        // Saturated signal should have different characteristics
        // (not identical to passthrough)
        REQUIRE(saturatedRMS > 0.0f);
    }
}

TEST_CASE("Oversampler2x upsample/downsample separate calls", "[oversampler][US1]") {
    Oversampler2x os;
    os.prepare(44100.0, 512);

    constexpr size_t blockSize = 64;
    constexpr size_t oversampledSize = blockSize * 2;

    std::array<float, blockSize> input{};
    std::array<float, oversampledSize> oversampled{};
    std::array<float, blockSize> output{};

    generateSineWave(input.data(), blockSize, 1000.0f, 44100.0f);

    SECTION("upsample produces 2x samples") {
        os.upsample(input.data(), oversampled.data(), blockSize, 0);

        // Upsampled buffer should have content
        float rms = calculateRMS(oversampled.data(), oversampledSize);
        REQUIRE(rms > 0.0f);
    }

    SECTION("downsample produces original sample count") {
        os.upsample(input.data(), oversampled.data(), blockSize, 0);
        os.downsample(oversampled.data(), output.data(), blockSize, 0);

        // Output should have content
        float rms = calculateRMS(output.data(), blockSize);
        REQUIRE(rms > 0.0f);
    }

    SECTION("round-trip preserves signal energy") {
        float inputRMS = calculateRMS(input.data(), blockSize);

        os.upsample(input.data(), oversampled.data(), blockSize, 0);
        os.downsample(oversampled.data(), output.data(), blockSize, 0);

        float outputRMS = calculateRMS(output.data(), blockSize);

        // Energy should be preserved within 1 dB
        REQUIRE(outputRMS > inputRMS * 0.89f);  // -1 dB
        REQUIRE(outputRMS < inputRMS * 1.12f);  // +1 dB
    }
}

TEST_CASE("Oversampler2x reset()", "[oversampler][US1]") {
    Oversampler2x os;
    os.prepare(44100.0, 512);

    constexpr size_t blockSize = 64;
    std::array<float, blockSize> left{}, right{};

    SECTION("reset clears filter state") {
        // Process some audio to build up filter state
        generateSineWave(left.data(), blockSize, 1000.0f, 44100.0f);
        generateSineWave(right.data(), blockSize, 1000.0f, 44100.0f);

        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        // Reset
        os.reset();

        // Process silence - should output near-silence
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        float rms = calculateRMS(left.data(), blockSize);
        REQUIRE(rms < 0.001f);  // Should be near-silent
    }
}

// =============================================================================
// Phase 4: User Story 2 - 4x Oversampling (T031-T045)
// =============================================================================

TEST_CASE("Oversampler4x default construction", "[oversampler][US2]") {
    Oversampler4x os;

    SECTION("default factor is 4") {
        REQUIRE(os.getFactor() == 4);
    }
}

TEST_CASE("Oversampler4x process() with callback", "[oversampler][US2]") {
    Oversampler4x os;
    os.prepare(48000.0, 256);

    constexpr size_t blockSize = 64;
    std::array<float, blockSize> left{}, right{};

    generateSineWave(left.data(), blockSize, 1000.0f, 48000.0f);
    generateSineWave(right.data(), blockSize, 1000.0f, 48000.0f);

    SECTION("callback receives 4x samples") {
        size_t callbackSamples = 0;

        os.process(left.data(), right.data(), blockSize,
            [&callbackSamples](float*, float*, size_t n) {
                callbackSamples = n;
            });

        REQUIRE(callbackSamples == blockSize * 4);
    }

    SECTION("output buffer size equals input buffer size") {
        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        // Should still have blockSize samples in output
        float rms = calculateRMS(left.data(), blockSize);
        REQUIRE(rms > 0.0f);
    }
}

TEST_CASE("Oversampler4x upsample produces 4x samples", "[oversampler][US2]") {
    Oversampler4x os;
    os.prepare(48000.0, 256);

    constexpr size_t blockSize = 64;
    constexpr size_t oversampledSize = blockSize * 4;

    std::array<float, blockSize> input{};
    std::array<float, oversampledSize> oversampled{};

    generateSineWave(input.data(), blockSize, 1000.0f, 48000.0f);

    os.upsample(input.data(), oversampled.data(), blockSize, 0);

    float rms = calculateRMS(oversampled.data(), oversampledSize);
    REQUIRE(rms > 0.0f);
}

// =============================================================================
// Phase 5: User Story 3 - Configurable Filter Quality (T046-T057)
// =============================================================================

TEST_CASE("Oversampler quality levels", "[oversampler][US3]") {
    Oversampler2x os;

    SECTION("Economy quality prepares successfully") {
        REQUIRE_NOTHROW(os.prepare(44100.0, 512, OversamplingQuality::Economy));
    }

    SECTION("Standard quality prepares successfully") {
        REQUIRE_NOTHROW(os.prepare(44100.0, 512, OversamplingQuality::Standard));
    }

    SECTION("High quality prepares successfully") {
        REQUIRE_NOTHROW(os.prepare(44100.0, 512, OversamplingQuality::High));
    }
}

// =============================================================================
// Phase 6: User Story 4 - Zero-Latency Mode (T058-T070)
// =============================================================================

TEST_CASE("Oversampler zero-latency mode", "[oversampler][US4]") {
    Oversampler2x os;

    SECTION("ZeroLatency mode has 0 latency") {
        os.prepare(44100.0, 512, OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
        REQUIRE(os.getLatency() == 0);
    }

    SECTION("ZeroLatency mode processes audio") {
        os.prepare(44100.0, 512, OversamplingQuality::Economy, OversamplingMode::ZeroLatency);

        constexpr size_t blockSize = 64;
        std::array<float, blockSize> left{}, right{};

        generateSineWave(left.data(), blockSize, 1000.0f, 44100.0f);
        generateSineWave(right.data(), blockSize, 1000.0f, 44100.0f);

        float inputRMS = calculateRMS(left.data(), blockSize);

        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        float outputRMS = calculateRMS(left.data(), blockSize);

        // Should preserve most energy
        REQUIRE(outputRMS > inputRMS * 0.5f);
    }
}

TEST_CASE("Oversampler linear-phase mode", "[oversampler][US4]") {
    Oversampler2x os;

    SECTION("LinearPhase mode reports latency") {
        os.prepare(44100.0, 512, OversamplingQuality::Standard, OversamplingMode::LinearPhase);
        // Linear phase FIR filters have latency
        // Just verify it's a reasonable value
        REQUIRE(os.getLatency() < 100);
    }
}

// =============================================================================
// Phase 7: User Story 5 - Sample Rate Changes (T071-T086)
// =============================================================================

TEST_CASE("Oversampler sample rate changes", "[oversampler][US5]") {
    Oversampler2x os;

    SECTION("re-prepare with different sample rate") {
        os.prepare(44100.0, 512);
        REQUIRE_NOTHROW(os.prepare(96000.0, 512));
    }

    SECTION("works at 22.05kHz") {
        REQUIRE_NOTHROW(os.prepare(22050.0, 512));

        constexpr size_t blockSize = 64;
        std::array<float, blockSize> left{}, right{};

        generateSineWave(left.data(), blockSize, 1000.0f, 22050.0f);
        generateSineWave(right.data(), blockSize, 1000.0f, 22050.0f);

        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        float rms = calculateRMS(left.data(), blockSize);
        REQUIRE(rms > 0.0f);
    }

    SECTION("works at 192kHz") {
        REQUIRE_NOTHROW(os.prepare(192000.0, 512));
    }

    SECTION("first block after sample rate change is valid") {
        os.prepare(44100.0, 512);

        constexpr size_t blockSize = 64;
        std::array<float, blockSize> left{}, right{};

        // Process at 44.1kHz
        generateSineWave(left.data(), blockSize, 1000.0f, 44100.0f);
        generateSineWave(right.data(), blockSize, 1000.0f, 44100.0f);
        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        // Change to 96kHz
        os.prepare(96000.0, 512);

        // Process first block at new rate
        generateSineWave(left.data(), blockSize, 1000.0f, 96000.0f);
        generateSineWave(right.data(), blockSize, 1000.0f, 96000.0f);
        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        // Should produce valid output
        float rms = calculateRMS(left.data(), blockSize);
        REQUIRE(rms > 0.0f);

        // Should not contain NaN or Inf
        for (size_t i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
        }
    }
}

// =============================================================================
// Phase 8: Edge Cases (T087-T096)
// =============================================================================

TEST_CASE("Oversampler edge cases", "[oversampler][edge]") {
    Oversampler2x os;
    os.prepare(44100.0, 512);

    SECTION("block size 1 sample") {
        std::array<float, 1> left{0.5f}, right{0.5f};

        os.process(left.data(), right.data(), 1,
            [](float*, float*, size_t n) {
                REQUIRE(n == 2);  // 1 * 2x = 2 samples
            });

        REQUIRE(std::isfinite(left[0]));
        REQUIRE(std::isfinite(right[0]));
    }

    SECTION("processes silence without issues") {
        constexpr size_t blockSize = 64;
        std::array<float, blockSize> left{}, right{};

        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        // Should still be near-zero
        float rms = calculateRMS(left.data(), blockSize);
        REQUIRE(rms < 0.0001f);
    }

    SECTION("handles DC offset") {
        constexpr size_t blockSize = 64;
        std::array<float, blockSize> left{}, right{};

        // Fill with DC offset
        std::fill(left.begin(), left.end(), 0.5f);
        std::fill(right.begin(), right.end(), 0.5f);

        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        // Filters should not amplify DC
        for (size_t i = 0; i < blockSize; ++i) {
            REQUIRE(std::abs(left[i]) < 2.0f);
            REQUIRE(std::abs(right[i]) < 2.0f);
        }
    }
}

TEST_CASE("Oversampler process() before prepare()", "[oversampler][edge]") {
    Oversampler2x os;
    // Do NOT call prepare()

    constexpr size_t blockSize = 64;
    std::array<float, blockSize> left{}, right{};

    generateSineWave(left.data(), blockSize, 1000.0f, 44100.0f);
    generateSineWave(right.data(), blockSize, 1000.0f, 44100.0f);

    SECTION("does not crash") {
        // Should either passthrough, output silence, or do nothing
        // but MUST NOT crash or produce garbage
        REQUIRE_NOTHROW(os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {}));
    }

    SECTION("outputs valid values") {
        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        for (size_t i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
        }
    }
}

TEST_CASE("Oversampler at low sample rate 22.05kHz", "[oversampler][edge]") {
    Oversampler2x os;

    SECTION("prepares successfully") {
        REQUIRE_NOTHROW(os.prepare(22050.0, 512));
    }

    SECTION("processes audio correctly") {
        os.prepare(22050.0, 512);

        constexpr size_t blockSize = 64;
        std::array<float, blockSize> left{}, right{};

        // Generate a lower frequency sine (Nyquist is ~11kHz)
        generateSineWave(left.data(), blockSize, 1000.0f, 22050.0f);
        generateSineWave(right.data(), blockSize, 1000.0f, 22050.0f);

        float inputRMS = calculateRMS(left.data(), blockSize);

        os.process(left.data(), right.data(), blockSize,
            [](float*, float*, size_t) {});

        float outputRMS = calculateRMS(left.data(), blockSize);

        // Should preserve energy
        REQUIRE(outputRMS > inputRMS * 0.5f);
        REQUIRE(outputRMS < inputRMS * 1.5f);
    }
}

// =============================================================================
// Mono Variants
// =============================================================================

TEST_CASE("Oversampler2xMono", "[oversampler][mono]") {
    Oversampler2xMono os;
    os.prepare(44100.0, 512);

    SECTION("processes mono signal") {
        constexpr size_t blockSize = 64;
        std::array<float, blockSize> buffer{};

        generateSineWave(buffer.data(), blockSize, 1000.0f, 44100.0f);
        float inputRMS = calculateRMS(buffer.data(), blockSize);

        os.process(buffer.data(), blockSize,
            [](float* buf, size_t n) {
                for (size_t i = 0; i < n; ++i) {
                    buf[i] = std::tanh(buf[i] * 2.0f);
                }
            });

        float outputRMS = calculateRMS(buffer.data(), blockSize);
        REQUIRE(outputRMS > 0.0f);
    }
}

TEST_CASE("Oversampler4xMono", "[oversampler][mono]") {
    Oversampler4xMono os;
    os.prepare(44100.0, 512);

    SECTION("callback receives 4x samples") {
        constexpr size_t blockSize = 64;
        std::array<float, blockSize> buffer{};
        size_t callbackSamples = 0;

        generateSineWave(buffer.data(), blockSize, 1000.0f, 44100.0f);

        os.process(buffer.data(), blockSize,
            [&callbackSamples](float*, size_t n) {
                callbackSamples = n;
            });

        REQUIRE(callbackSamples == blockSize * 4);
    }
}

// =============================================================================
// Benchmarks (Optional - run with --benchmark)
// =============================================================================

TEST_CASE("Oversampler2x benchmark", "[oversampler][benchmark][!benchmark]") {
    Oversampler2x os;
    os.prepare(44100.0, 512);

    constexpr size_t blockSize = 512;
    std::array<float, blockSize> left{}, right{};

    generateSineWave(left.data(), blockSize, 1000.0f, 44100.0f);
    generateSineWave(right.data(), blockSize, 1000.0f, 44100.0f);

    BENCHMARK("2x stereo 512 samples") {
        os.process(left.data(), right.data(), blockSize,
            [](float* L, float* R, size_t n) {
                for (size_t i = 0; i < n; ++i) {
                    L[i] = std::tanh(L[i]);
                    R[i] = std::tanh(R[i]);
                }
            });
        return left[0];
    };
}

TEST_CASE("Oversampler4x benchmark", "[oversampler][benchmark][!benchmark]") {
    Oversampler4x os;
    os.prepare(44100.0, 512);

    constexpr size_t blockSize = 512;
    std::array<float, blockSize> left{}, right{};

    generateSineWave(left.data(), blockSize, 1000.0f, 44100.0f);
    generateSineWave(right.data(), blockSize, 1000.0f, 44100.0f);

    BENCHMARK("4x stereo 512 samples") {
        os.process(left.data(), right.data(), blockSize,
            [](float* L, float* R, size_t n) {
                for (size_t i = 0; i < n; ++i) {
                    L[i] = std::tanh(L[i]);
                    R[i] = std::tanh(R[i]);
                }
            });
        return left[0];
    };
}
