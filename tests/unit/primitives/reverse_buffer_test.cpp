// ==============================================================================
// Layer 1: DSP Primitive Tests - ReverseBuffer
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/primitives/reverse_buffer.h"

#include <cmath>
#include <array>
#include <numeric>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Phase 2: Foundational Tests (T004-T008)
// =============================================================================

// -----------------------------------------------------------------------------
// T005: ReverseBuffer prepare() allocates correct buffer size
// -----------------------------------------------------------------------------

TEST_CASE("ReverseBuffer prepare allocates correct buffer size", "[reverse-buffer][prepare]") {
    ReverseBuffer buffer;

    SECTION("prepares with standard sample rate and chunk size") {
        buffer.prepare(44100.0, 500.0f);  // 500ms max chunk

        // Latency should equal chunk size in samples
        // 500ms at 44.1kHz = 22050 samples
        REQUIRE(buffer.getLatencySamples() == 22050);
        REQUIRE(buffer.getChunkSizeMs() == Approx(500.0f));
    }

    SECTION("prepares with high sample rate") {
        buffer.prepare(96000.0, 1000.0f);  // 1000ms at 96kHz

        // 1000ms at 96kHz = 96000 samples
        REQUIRE(buffer.getLatencySamples() == 96000);
        REQUIRE(buffer.getChunkSizeMs() == Approx(1000.0f));
    }

    SECTION("prepares with minimum chunk size (10ms)") {
        buffer.prepare(44100.0, 10.0f);  // 10ms minimum

        // 10ms at 44.1kHz = 441 samples
        REQUIRE(buffer.getLatencySamples() == 441);
        REQUIRE(buffer.getChunkSizeMs() == Approx(10.0f));
    }

    SECTION("prepares with maximum chunk size (2000ms)") {
        buffer.prepare(44100.0, 2000.0f);  // 2000ms maximum

        // 2000ms at 44.1kHz = 88200 samples
        REQUIRE(buffer.getLatencySamples() == 88200);
        REQUIRE(buffer.getChunkSizeMs() == Approx(2000.0f));
    }

    SECTION("prepares at all standard sample rates") {
        const std::array<double, 6> sampleRates = {
            44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
        };

        for (double sr : sampleRates) {
            buffer.prepare(sr, 500.0f);

            // 500ms at each sample rate
            size_t expected = static_cast<size_t>(sr * 0.5);
            REQUIRE(buffer.getLatencySamples() == expected);
        }
    }
}

TEST_CASE("ReverseBuffer can be re-prepared", "[reverse-buffer][prepare]") {
    ReverseBuffer buffer;

    // First prepare
    buffer.prepare(44100.0, 500.0f);
    REQUIRE(buffer.getLatencySamples() == 22050);

    // Re-prepare with different settings
    buffer.prepare(96000.0, 1000.0f);
    REQUIRE(buffer.getLatencySamples() == 96000);
}

// -----------------------------------------------------------------------------
// T006: ReverseBuffer reset() clears buffer state
// -----------------------------------------------------------------------------

TEST_CASE("ReverseBuffer reset clears buffer state", "[reverse-buffer][reset]") {
    ReverseBuffer buffer;
    buffer.prepare(44100.0, 100.0f);  // 100ms chunk = 4410 samples

    SECTION("reset clears audio in buffers") {
        // Process some samples to fill buffer
        for (int i = 0; i < 1000; ++i) {
            (void)buffer.process(1.0f);
        }

        // Reset should clear all samples
        buffer.reset();

        // After reset, output should be zero (buffer is cleared)
        float output = buffer.process(0.0f);
        REQUIRE(output == Approx(0.0f));
    }

    SECTION("reset resets write position") {
        // Process to near chunk boundary
        for (int i = 0; i < 4000; ++i) {
            (void)buffer.process(0.5f);
        }

        // Reset
        buffer.reset();

        // Should not be at chunk boundary immediately after reset
        REQUIRE_FALSE(buffer.isAtChunkBoundary());
    }

    SECTION("reset preserves configuration") {
        buffer.setChunkSizeMs(200.0f);
        buffer.setCrossfadeMs(30.0f);

        float chunkBefore = buffer.getChunkSizeMs();

        buffer.reset();

        // Configuration should be unchanged
        REQUIRE(buffer.getChunkSizeMs() == Approx(chunkBefore));
    }
}

// -----------------------------------------------------------------------------
// T007: ReverseBuffer process() returns zero during first chunk capture
// -----------------------------------------------------------------------------

TEST_CASE("ReverseBuffer process returns zero during first chunk capture", "[reverse-buffer][process]") {
    ReverseBuffer buffer;
    buffer.prepare(44100.0, 100.0f);  // 100ms = 4410 samples

    SECTION("output is zero while capturing first chunk") {
        // During the first chunk, the playback buffer is empty
        // so output should be zero regardless of input
        for (int i = 0; i < 4000; ++i) {
            float input = static_cast<float>(i) * 0.001f;  // Varying input
            float output = buffer.process(input);
            REQUIRE(output == Approx(0.0f));
        }
    }

    SECTION("output remains zero until chunk boundary") {
        // Process exactly chunk_size - 1 samples
        size_t chunkSamples = buffer.getLatencySamples();

        for (size_t i = 0; i < chunkSamples - 1; ++i) {
            float output = buffer.process(1.0f);
            REQUIRE(output == Approx(0.0f));
        }
    }
}

// -----------------------------------------------------------------------------
// T008: ReverseBuffer swaps buffers at chunk boundary
// -----------------------------------------------------------------------------

TEST_CASE("ReverseBuffer swaps buffers at chunk boundary", "[reverse-buffer][boundary]") {
    ReverseBuffer buffer;
    buffer.prepare(44100.0, 10.0f);  // 10ms = 441 samples for faster testing

    SECTION("isAtChunkBoundary returns true at boundary") {
        size_t chunkSamples = buffer.getLatencySamples();

        // Process up to boundary
        for (size_t i = 0; i < chunkSamples; ++i) {
            (void)buffer.process(1.0f);
        }

        // Should be at chunk boundary now
        REQUIRE(buffer.isAtChunkBoundary());
    }

    SECTION("output changes after chunk boundary in reverse mode") {
        buffer.setReversed(true);
        size_t chunkSamples = buffer.getLatencySamples();

        // Fill first chunk with a ramp: 0, 1, 2, ..., N-1
        std::vector<float> inputs(chunkSamples);
        for (size_t i = 0; i < chunkSamples; ++i) {
            inputs[i] = static_cast<float>(i);
            (void)buffer.process(inputs[i]);
        }

        // Now process next chunk - output should be REVERSED first chunk
        // The first output sample should be the LAST sample of the first chunk (N-1)
        float firstReversedOutput = buffer.process(0.0f);

        // In reverse mode, we read from end to start
        // First output from reversed playback should be value (chunkSamples - 1)
        REQUIRE(firstReversedOutput == Approx(static_cast<float>(chunkSamples - 1)));
    }

    SECTION("second output sample is second-to-last input") {
        buffer.setReversed(true);
        size_t chunkSamples = buffer.getLatencySamples();

        // Fill first chunk with a ramp
        for (size_t i = 0; i < chunkSamples; ++i) {
            (void)buffer.process(static_cast<float>(i));
        }

        // Get first two outputs from reversed playback
        float out1 = buffer.process(0.0f);  // Should be chunkSamples - 1
        float out2 = buffer.process(0.0f);  // Should be chunkSamples - 2

        REQUIRE(out1 == Approx(static_cast<float>(chunkSamples - 1)));
        REQUIRE(out2 == Approx(static_cast<float>(chunkSamples - 2)));
    }

    SECTION("complete reversed chunk playback is sample-accurate") {
        buffer.setReversed(true);
        size_t chunkSamples = buffer.getLatencySamples();

        // Fill first chunk with known values
        for (size_t i = 0; i < chunkSamples; ++i) {
            (void)buffer.process(static_cast<float>(i));
        }

        // Read entire reversed chunk
        std::vector<float> outputs(chunkSamples);
        for (size_t i = 0; i < chunkSamples; ++i) {
            outputs[i] = buffer.process(0.0f);
        }

        // Verify reversal: output[i] should equal input[chunkSamples - 1 - i]
        for (size_t i = 0; i < chunkSamples; ++i) {
            float expected = static_cast<float>(chunkSamples - 1 - i);
            REQUIRE(outputs[i] == Approx(expected));
        }
    }
}

TEST_CASE("ReverseBuffer continuous operation with seamless recycling", "[reverse-buffer][continuous]") {
    ReverseBuffer buffer;
    buffer.prepare(44100.0, 10.0f);  // 10ms = 441 samples
    buffer.setReversed(true);

    size_t chunkSamples = buffer.getLatencySamples();

    SECTION("multiple chunks process without gaps") {
        // Process 5 chunks worth of samples
        size_t totalSamples = chunkSamples * 5;

        for (size_t i = 0; i < totalSamples; ++i) {
            float output = buffer.process(1.0f);
            // After first chunk, output should be non-zero
            if (i >= chunkSamples) {
                REQUIRE(output == Approx(1.0f));
            }
        }
    }

    SECTION("each chunk independently reversed") {
        // Process 3 chunks with distinct values
        // Chunk 1: all 1.0
        for (size_t i = 0; i < chunkSamples; ++i) {
            (void)buffer.process(1.0f);
        }

        // Chunk 2: all 2.0, output should be reversed chunk 1 (all 1.0)
        for (size_t i = 0; i < chunkSamples; ++i) {
            float output = buffer.process(2.0f);
            REQUIRE(output == Approx(1.0f));
        }

        // Chunk 3: all 3.0, output should be reversed chunk 2 (all 2.0)
        for (size_t i = 0; i < chunkSamples; ++i) {
            float output = buffer.process(3.0f);
            REQUIRE(output == Approx(2.0f));
        }
    }
}

// =============================================================================
// Additional foundational tests for complete coverage
// =============================================================================

TEST_CASE("ReverseBuffer setChunkSizeMs updates chunk size", "[reverse-buffer][config]") {
    ReverseBuffer buffer;
    buffer.prepare(44100.0, 2000.0f);  // Prepare with max capacity

    SECTION("chunk size can be changed after prepare") {
        buffer.setChunkSizeMs(500.0f);
        REQUIRE(buffer.getChunkSizeMs() == Approx(500.0f));
        REQUIRE(buffer.getLatencySamples() == 22050);
    }

    SECTION("chunk size clamps to minimum") {
        buffer.setChunkSizeMs(5.0f);  // Below minimum of 10ms
        REQUIRE(buffer.getChunkSizeMs() >= 10.0f);
    }

    SECTION("chunk size clamps to maximum") {
        buffer.setChunkSizeMs(3000.0f);  // Above prepared max of 2000ms
        REQUIRE(buffer.getChunkSizeMs() <= 2000.0f);
    }
}

TEST_CASE("ReverseBuffer forward playback mode", "[reverse-buffer][forward]") {
    ReverseBuffer buffer;
    buffer.prepare(44100.0, 10.0f);  // 10ms = 441 samples
    buffer.setReversed(false);  // Forward mode

    size_t chunkSamples = buffer.getLatencySamples();

    SECTION("forward mode plays chunk in original order") {
        // Fill first chunk with a ramp
        for (size_t i = 0; i < chunkSamples; ++i) {
            (void)buffer.process(static_cast<float>(i));
        }

        // Read chunk - should be in original order
        std::vector<float> outputs(chunkSamples);
        for (size_t i = 0; i < chunkSamples; ++i) {
            outputs[i] = buffer.process(0.0f);
        }

        // In forward mode: output[i] should equal input[i]
        for (size_t i = 0; i < chunkSamples; ++i) {
            REQUIRE(outputs[i] == Approx(static_cast<float>(i)));
        }
    }
}

TEST_CASE("ReverseBuffer noexcept specifications", "[reverse-buffer][realtime]") {
    SECTION("constructors are noexcept") {
        static_assert(std::is_nothrow_default_constructible_v<ReverseBuffer>,
                      "Default constructor must be noexcept");
        static_assert(std::is_nothrow_destructible_v<ReverseBuffer>,
                      "Destructor must be noexcept");
        REQUIRE(true);
    }

    SECTION("processing methods are noexcept") {
        ReverseBuffer buffer;
        buffer.prepare(44100.0, 100.0f);

        static_assert(noexcept(buffer.process(0.0f)), "process() must be noexcept");
        static_assert(noexcept(buffer.reset()), "reset() must be noexcept");
        REQUIRE(true);
    }

    SECTION("configuration methods are noexcept") {
        ReverseBuffer buffer;
        buffer.prepare(44100.0, 100.0f);

        static_assert(noexcept(buffer.setChunkSizeMs(100.0f)), "setChunkSizeMs() must be noexcept");
        static_assert(noexcept(buffer.setCrossfadeMs(20.0f)), "setCrossfadeMs() must be noexcept");
        static_assert(noexcept(buffer.setReversed(true)), "setReversed() must be noexcept");
        REQUIRE(true);
    }

    SECTION("query methods are noexcept") {
        ReverseBuffer buffer;
        buffer.prepare(44100.0, 100.0f);

        static_assert(noexcept(buffer.isAtChunkBoundary()), "isAtChunkBoundary() must be noexcept");
        static_assert(noexcept(buffer.getChunkSizeMs()), "getChunkSizeMs() must be noexcept");
        static_assert(noexcept(buffer.getLatencySamples()), "getLatencySamples() must be noexcept");
        REQUIRE(true);
    }
}
