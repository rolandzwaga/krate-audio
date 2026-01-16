// ==============================================================================
// Layer 1: Primitive Tests - Rolling Capture Buffer
// ==============================================================================
// Unit tests for RollingCaptureBuffer (spec 069 - Pattern Freeze Mode).
//
// Tests verify:
// - Continuous circular recording
// - Slice extraction at specified positions
// - Buffer ready state detection
// - Edge cases (wrap-around, boundary conditions)
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-first development methodology
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/rolling_capture_buffer.h>

#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("RollingCaptureBuffer prepares with correct capacity", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;

    // 1 second at 44100 Hz
    buffer.prepare(44100.0, 1.0f);

    REQUIRE(buffer.getCapacitySamples() >= 44100);
    REQUIRE(buffer.getSampleRate() == Approx(44100.0));
}

TEST_CASE("RollingCaptureBuffer reset clears content", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write some data
    for (int i = 0; i < 1000; ++i) {
        buffer.writeStereo(0.5f, -0.5f);
    }

    // Reset
    buffer.reset();

    // Buffer should not be ready after reset
    REQUIRE(buffer.isReady(100.0f) == false);
}

// =============================================================================
// Write and Read Tests
// =============================================================================

TEST_CASE("RollingCaptureBuffer records stereo samples", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write sequential values
    for (int i = 0; i < 100; ++i) {
        buffer.writeStereo(static_cast<float>(i) * 0.01f,
                          static_cast<float>(i) * -0.01f);
    }

    // Extract recent slice
    std::vector<float> sliceL(50);
    std::vector<float> sliceR(50);

    // Extract last 50 samples
    buffer.extractSlice(sliceL.data(), sliceR.data(), 50, 0);

    // Last sample written was 99 * 0.01 = 0.99
    // At offset 0, we should get the most recent samples (49 back to 0 from write head)
    REQUIRE(sliceL[49] == Approx(0.99f).margin(0.001f));
}

TEST_CASE("RollingCaptureBuffer wraps around correctly", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.1f);  // 100ms = 4410 samples

    const size_t capacity = buffer.getCapacitySamples();

    // Write more than capacity to force wraparound
    for (size_t i = 0; i < capacity + 1000; ++i) {
        buffer.writeStereo(1.0f, -1.0f);
    }

    // Should still be able to read valid data
    std::vector<float> sliceL(100);
    std::vector<float> sliceR(100);

    buffer.extractSlice(sliceL.data(), sliceR.data(), 100, 0);

    // All samples should be 1.0 and -1.0
    for (size_t i = 0; i < 100; ++i) {
        REQUIRE(sliceL[i] == Approx(1.0f));
        REQUIRE(sliceR[i] == Approx(-1.0f));
    }
}

TEST_CASE("RollingCaptureBuffer extractSlice with offset", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write ramp signal
    for (int i = 0; i < 1000; ++i) {
        float value = static_cast<float>(i);
        buffer.writeStereo(value, -value);
    }

    // Extract with offset into the past
    std::vector<float> sliceL(100);
    std::vector<float> sliceR(100);

    // Offset 500 means start 500 samples before current write position
    buffer.extractSlice(sliceL.data(), sliceR.data(), 100, 500);

    // At offset 500, first sample should be around (1000 - 500 - 100) = 400
    // This is complex due to circular buffer, but the slice should be contiguous
    // and values should be monotonically increasing within the slice
    for (size_t i = 1; i < 100; ++i) {
        REQUIRE(sliceL[i] > sliceL[i - 1]);  // Increasing
        REQUIRE(sliceR[i] < sliceR[i - 1]);  // Decreasing (negative)
    }
}

// =============================================================================
// Ready State Tests
// =============================================================================

TEST_CASE("RollingCaptureBuffer isReady detects sufficient data", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Initially not ready
    REQUIRE(buffer.isReady(100.0f) == false);

    // Write exactly the required amount (100ms at 44100Hz = 4410 samples)
    const size_t requiredSamples = static_cast<size_t>(44100.0 * 0.1);
    for (size_t i = 0; i < requiredSamples; ++i) {
        buffer.writeStereo(0.5f, 0.5f);
    }

    // Now should be ready for 100ms
    REQUIRE(buffer.isReady(100.0f) == true);

    // But not ready for more than written
    REQUIRE(buffer.isReady(200.0f) == false);
}

TEST_CASE("RollingCaptureBuffer isReady with full buffer", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.5f);  // 500ms buffer

    // Fill entire buffer
    const size_t capacity = buffer.getCapacitySamples();
    for (size_t i = 0; i < capacity; ++i) {
        buffer.writeStereo(0.5f, 0.5f);
    }

    // Should be ready for any time up to buffer duration
    REQUIRE(buffer.isReady(100.0f) == true);
    REQUIRE(buffer.isReady(250.0f) == true);
    REQUIRE(buffer.isReady(500.0f) == true);
}

TEST_CASE("RollingCaptureBuffer getSamplesWritten tracks correctly", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    REQUIRE(buffer.getSamplesWritten() == 0);

    for (int i = 0; i < 500; ++i) {
        buffer.writeStereo(0.0f, 0.0f);
    }

    REQUIRE(buffer.getSamplesWritten() == 500);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("RollingCaptureBuffer handles zero-length extraction", "[primitives][capture_buffer][layer1][edge]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write some data
    for (int i = 0; i < 100; ++i) {
        buffer.writeStereo(0.5f, 0.5f);
    }

    // Zero-length extraction should not crash
    float dummyL = 0.0f, dummyR = 0.0f;
    buffer.extractSlice(&dummyL, &dummyR, 0, 0);

    // No assertion needed - just verify no crash
    REQUIRE(true);
}

TEST_CASE("RollingCaptureBuffer clamps extraction length", "[primitives][capture_buffer][layer1][edge]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.1f);  // 100ms buffer

    // Fill buffer
    const size_t capacity = buffer.getCapacitySamples();
    for (size_t i = 0; i < capacity; ++i) {
        buffer.writeStereo(1.0f, 1.0f);
    }

    // Try to extract more than capacity
    std::vector<float> sliceL(capacity * 2);
    std::vector<float> sliceR(capacity * 2);

    // Should not crash, extracts up to available data
    buffer.extractSlice(sliceL.data(), sliceR.data(), capacity * 2, 0);

    // At least capacity samples should be valid
    size_t validCount = 0;
    for (size_t i = 0; i < capacity; ++i) {
        if (sliceL[i] == Approx(1.0f)) ++validCount;
    }
    REQUIRE(validCount == capacity);
}

TEST_CASE("RollingCaptureBuffer handles offset beyond buffer", "[primitives][capture_buffer][layer1][edge]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.1f);

    // Write some data
    for (int i = 0; i < 1000; ++i) {
        buffer.writeStereo(0.5f, 0.5f);
    }

    // Offset beyond written data - should wrap or clamp
    std::vector<float> sliceL(100);
    std::vector<float> sliceR(100);

    // Large offset - should still extract something valid (wraps in circular buffer)
    buffer.extractSlice(sliceL.data(), sliceR.data(), 100, 10000);

    // Should not crash, values should be defined
    // After wraparound, we might get either valid data or zeros
    REQUIRE(true);  // Just verify no crash
}

// =============================================================================
// Real-Time Safety Tests
// =============================================================================

TEST_CASE("RollingCaptureBuffer writeStereo is noexcept", "[primitives][capture_buffer][layer1][realtime]") {
    // Compile-time check
    RollingCaptureBuffer buffer;
    static_assert(noexcept(buffer.writeStereo(0.0f, 0.0f)),
                  "writeStereo() must be noexcept");
}

TEST_CASE("RollingCaptureBuffer extractSlice is noexcept", "[primitives][capture_buffer][layer1][realtime]") {
    RollingCaptureBuffer buffer;
    float L, R;
    static_assert(noexcept(buffer.extractSlice(&L, &R, 1, 0)),
                  "extractSlice() must be noexcept");
}

// =============================================================================
// Multi-Slice Extraction Tests (for Pattern Mode)
// =============================================================================

TEST_CASE("RollingCaptureBuffer supports multiple slice extractions", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write unique values for each sample
    for (int i = 0; i < 10000; ++i) {
        buffer.writeStereo(static_cast<float>(i), 0.0f);
    }

    // Extract multiple non-overlapping slices
    std::vector<float> slice1L(100), slice1R(100);
    std::vector<float> slice2L(100), slice2R(100);
    std::vector<float> slice3L(100), slice3R(100);

    buffer.extractSlice(slice1L.data(), slice1R.data(), 100, 0);
    buffer.extractSlice(slice2L.data(), slice2R.data(), 100, 200);
    buffer.extractSlice(slice3L.data(), slice3R.data(), 100, 400);

    // Verify each slice has internally consistent values (monotonically increasing)
    for (size_t i = 1; i < 100; ++i) {
        REQUIRE(slice1L[i] > slice1L[i - 1]);
        REQUIRE(slice2L[i] > slice2L[i - 1]);
        REQUIRE(slice3L[i] > slice3L[i - 1]);
    }

    // Verify slices are from different time periods
    // slice3 (offset 400) should have older values than slice1 (offset 0)
    REQUIRE(slice3L[0] < slice1L[0]);
}

// =============================================================================
// Available Samples Query Test
// =============================================================================

TEST_CASE("RollingCaptureBuffer getAvailableSamples", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.5f);  // 500ms

    REQUIRE(buffer.getAvailableSamples() == 0);

    for (int i = 0; i < 1000; ++i) {
        buffer.writeStereo(0.0f, 0.0f);
    }

    REQUIRE(buffer.getAvailableSamples() == 1000);

    // Writing more than capacity caps at capacity
    const size_t capacity = buffer.getCapacitySamples();
    for (size_t i = 0; i < capacity * 2; ++i) {
        buffer.writeStereo(0.0f, 0.0f);
    }

    REQUIRE(buffer.getAvailableSamples() == capacity);
}
