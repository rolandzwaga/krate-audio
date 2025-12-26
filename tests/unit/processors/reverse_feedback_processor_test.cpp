// ==============================================================================
// Layer 2: DSP Processor Tests - ReverseFeedbackProcessor
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/processors/reverse_feedback_processor.h"

#include <cmath>
#include <array>
#include <vector>
#include <random>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Phase 3: User Story 1 Tests - Basic Reverse Echo
// =============================================================================

TEST_CASE("ReverseFeedbackProcessor implements IFeedbackProcessor", "[reverse-processor][interface]") {
    ReverseFeedbackProcessor processor;

    SECTION("prepare does not throw") {
        processor.prepare(44100.0, 512);
        REQUIRE(true);  // If we get here, prepare succeeded
    }

    SECTION("reset does not throw after prepare") {
        processor.prepare(44100.0, 512);
        processor.reset();
        REQUIRE(true);
    }

    SECTION("getLatencySamples returns chunk size") {
        processor.prepare(44100.0, 512);
        processor.setChunkSizeMs(100.0f);  // 100ms = 4410 samples at 44.1kHz

        std::size_t latency = processor.getLatencySamples();
        REQUIRE(latency == 4410);
    }
}

TEST_CASE("ReverseFeedbackProcessor stereo processing", "[reverse-processor][stereo]") {
    ReverseFeedbackProcessor processor;
    processor.prepare(44100.0, 512);
    processor.setChunkSizeMs(10.0f);  // 10ms = 441 samples (small for testing)

    SECTION("processes stereo buffers independently") {
        std::size_t chunkSamples = processor.getLatencySamples();

        // Create distinct L/R input patterns
        std::vector<float> left(chunkSamples);
        std::vector<float> right(chunkSamples);

        for (std::size_t i = 0; i < chunkSamples; ++i) {
            left[i] = static_cast<float>(i);           // 0, 1, 2, ...
            right[i] = static_cast<float>(i) * 2.0f;   // 0, 2, 4, ...
        }

        // Process first chunk (fills capture buffer)
        processor.process(left.data(), right.data(), chunkSamples);

        // Reset buffers for second chunk
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        // Process second chunk - should get reversed first chunk
        processor.process(left.data(), right.data(), chunkSamples);

        // Verify L/R are reversed independently
        // Left should be: chunkSamples-1, chunkSamples-2, ..., 0
        // Right should be: (chunkSamples-1)*2, (chunkSamples-2)*2, ..., 0
        REQUIRE(left[0] == Approx(static_cast<float>(chunkSamples - 1)));
        REQUIRE(right[0] == Approx(static_cast<float>(chunkSamples - 1) * 2.0f));
    }
}

TEST_CASE("ReverseFeedbackProcessor FullReverse mode", "[reverse-processor][modes]") {
    ReverseFeedbackProcessor processor;
    processor.prepare(44100.0, 512);
    processor.setChunkSizeMs(10.0f);  // 441 samples
    processor.setPlaybackMode(PlaybackMode::FullReverse);

    std::size_t chunkSamples = processor.getLatencySamples();

    SECTION("every chunk is reversed") {
        // Test 3 consecutive chunks
        for (int chunk = 0; chunk < 3; ++chunk) {
            std::vector<float> left(chunkSamples);
            std::vector<float> right(chunkSamples);

            // Fill with ramp
            float baseValue = static_cast<float>(chunk * 100);
            for (std::size_t i = 0; i < chunkSamples; ++i) {
                left[i] = baseValue + static_cast<float>(i);
                right[i] = baseValue + static_cast<float>(i);
            }

            // Process chunk
            processor.process(left.data(), right.data(), chunkSamples);

            // After first chunk, outputs should be reversed
            if (chunk > 0) {
                // Check first output is last input of previous chunk
                float prevBase = static_cast<float>((chunk - 1) * 100);
                float expectedFirst = prevBase + static_cast<float>(chunkSamples - 1);
                REQUIRE(left[0] == Approx(expectedFirst));
            }
        }
    }
}

TEST_CASE("ReverseFeedbackProcessor Alternating mode", "[reverse-processor][modes]") {
    ReverseFeedbackProcessor processor;
    processor.prepare(44100.0, 512);
    processor.setChunkSizeMs(10.0f);  // 441 samples
    processor.setPlaybackMode(PlaybackMode::Alternating);

    std::size_t chunkSamples = processor.getLatencySamples();

    SECTION("chunks alternate between reverse and forward") {
        // Process 4 chunks with distinct values
        std::vector<std::vector<float>> inputChunks(4);
        std::vector<std::vector<float>> outputChunks(4);

        for (int chunk = 0; chunk < 4; ++chunk) {
            inputChunks[chunk].resize(chunkSamples);
            outputChunks[chunk].resize(chunkSamples);

            // Fill with unique ramp: chunk 0 = 0..N-1, chunk 1 = N..2N-1, etc.
            for (std::size_t i = 0; i < chunkSamples; ++i) {
                inputChunks[chunk][i] = static_cast<float>(chunk * chunkSamples + i);
            }

            // Copy input and process
            std::copy(inputChunks[chunk].begin(), inputChunks[chunk].end(),
                      outputChunks[chunk].begin());
            std::vector<float> right(chunkSamples, 0.0f);
            processor.process(outputChunks[chunk].data(), right.data(), chunkSamples);
        }

        // Chunk 0: fills capture, output is zero (first chunk)
        // Chunk 1: output is REVERSED chunk 0 (reverse)
        // Chunk 2: output is FORWARD chunk 1 (forward)
        // Chunk 3: output is REVERSED chunk 2 (reverse)

        // Verify chunk 1 output (reversed chunk 0)
        REQUIRE(outputChunks[1][0] == Approx(static_cast<float>(chunkSamples - 1)));

        // Verify chunk 2 output (forward chunk 1)
        REQUIRE(outputChunks[2][0] == Approx(static_cast<float>(chunkSamples)));  // First sample of chunk 1

        // Verify chunk 3 output (reversed chunk 2)
        REQUIRE(outputChunks[3][0] == Approx(static_cast<float>(3 * chunkSamples - 1)));
    }
}

TEST_CASE("ReverseFeedbackProcessor Random mode", "[reverse-processor][modes]") {
    ReverseFeedbackProcessor processor;
    processor.prepare(44100.0, 512);
    processor.setChunkSizeMs(10.0f);  // 441 samples
    processor.setPlaybackMode(PlaybackMode::Random);

    std::size_t chunkSamples = processor.getLatencySamples();

    SECTION("produces both forward and reverse over many chunks") {
        // Process many chunks and verify we get both directions
        int forwardCount = 0;
        int reverseCount = 0;

        for (int chunk = 0; chunk < 100; ++chunk) {
            std::vector<float> left(chunkSamples);
            std::vector<float> right(chunkSamples);

            // Fill with ramp
            for (std::size_t i = 0; i < chunkSamples; ++i) {
                left[i] = static_cast<float>(i);
                right[i] = static_cast<float>(i);
            }

            processor.process(left.data(), right.data(), chunkSamples);

            // After first chunk, check if output is reversed or forward
            if (chunk > 0) {
                // If first sample is chunkSamples-1, it's reversed
                // If first sample is 0, it's forward
                if (left[0] > static_cast<float>(chunkSamples / 2)) {
                    reverseCount++;
                } else {
                    forwardCount++;
                }
            }
        }

        // Should have some of each (with 99 trials, probability of all same is ~2^-99)
        INFO("Forward count: " << forwardCount << ", Reverse count: " << reverseCount);
        REQUIRE(forwardCount > 0);
        REQUIRE(reverseCount > 0);
    }
}

TEST_CASE("ReverseFeedbackProcessor chunk size configuration", "[reverse-processor][config]") {
    ReverseFeedbackProcessor processor;
    processor.prepare(44100.0, 512);

    SECTION("setChunkSizeMs updates latency") {
        processor.setChunkSizeMs(500.0f);  // 500ms = 22050 samples
        REQUIRE(processor.getLatencySamples() == 22050);
        REQUIRE(processor.getChunkSizeMs() == Approx(500.0f));
    }

    SECTION("chunk size clamps to valid range") {
        processor.setChunkSizeMs(5.0f);  // Below minimum
        REQUIRE(processor.getChunkSizeMs() >= 10.0f);

        processor.setChunkSizeMs(5000.0f);  // Above maximum (if prepared with smaller)
        // Should clamp to prepared maximum
    }
}

TEST_CASE("ReverseFeedbackProcessor sample-accurate reverse (SC-001)", "[reverse-processor][SC-001]") {
    ReverseFeedbackProcessor processor;
    processor.prepare(44100.0, 512);
    processor.setChunkSizeMs(10.0f);  // 441 samples
    processor.setPlaybackMode(PlaybackMode::FullReverse);

    std::size_t chunkSamples = processor.getLatencySamples();

    SECTION("first sample of input becomes last sample of reversed output") {
        // Fill chunk with known values
        std::vector<float> left(chunkSamples);
        std::vector<float> right(chunkSamples);

        for (std::size_t i = 0; i < chunkSamples; ++i) {
            left[i] = static_cast<float>(i);
            right[i] = static_cast<float>(i);
        }

        // Process first chunk
        processor.process(left.data(), right.data(), chunkSamples);

        // Process second chunk (all zeros)
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        processor.process(left.data(), right.data(), chunkSamples);

        // Verify complete reversal
        for (std::size_t i = 0; i < chunkSamples; ++i) {
            float expected = static_cast<float>(chunkSamples - 1 - i);
            REQUIRE(left[i] == Approx(expected));
            REQUIRE(right[i] == Approx(expected));
        }
    }
}

TEST_CASE("ReverseFeedbackProcessor noexcept specifications", "[reverse-processor][realtime]") {
    SECTION("constructors are noexcept") {
        static_assert(std::is_nothrow_default_constructible_v<ReverseFeedbackProcessor>,
                      "Default constructor must be noexcept");
        static_assert(std::is_nothrow_destructible_v<ReverseFeedbackProcessor>,
                      "Destructor must be noexcept");
        REQUIRE(true);
    }

    SECTION("IFeedbackProcessor methods are noexcept") {
        ReverseFeedbackProcessor processor;
        processor.prepare(44100.0, 512);
        processor.setChunkSizeMs(10.0f);

        std::vector<float> left(512, 0.0f);
        std::vector<float> right(512, 0.0f);

        static_assert(noexcept(processor.process(left.data(), right.data(), 512)),
                      "process() must be noexcept");
        static_assert(noexcept(processor.reset()), "reset() must be noexcept");
        static_assert(noexcept(processor.getLatencySamples()),
                      "getLatencySamples() must be noexcept");
        REQUIRE(true);
    }
}

TEST_CASE("ReverseFeedbackProcessor continuous operation (FR-003)", "[reverse-processor][FR-003]") {
    ReverseFeedbackProcessor processor;
    processor.prepare(44100.0, 512);
    processor.setChunkSizeMs(10.0f);  // 441 samples
    processor.setPlaybackMode(PlaybackMode::FullReverse);

    std::size_t chunkSamples = processor.getLatencySamples();

    SECTION("seamless buffer recycling over many chunks") {
        // Process 10 chunks and verify no gaps or artifacts
        for (int chunk = 0; chunk < 10; ++chunk) {
            std::vector<float> left(chunkSamples);
            std::vector<float> right(chunkSamples);

            // Fill with constant value for this chunk
            float value = static_cast<float>(chunk + 1);
            std::fill(left.begin(), left.end(), value);
            std::fill(right.begin(), right.end(), value);

            processor.process(left.data(), right.data(), chunkSamples);

            // After first chunk, output should be constant (reversed constant = same constant)
            if (chunk > 0) {
                float expectedValue = static_cast<float>(chunk);  // Previous chunk value
                for (std::size_t i = 0; i < chunkSamples; ++i) {
                    REQUIRE(left[i] == Approx(expectedValue));
                    REQUIRE(right[i] == Approx(expectedValue));
                }
            }
        }
    }
}
