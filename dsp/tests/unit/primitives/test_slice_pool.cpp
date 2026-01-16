// ==============================================================================
// Layer 1: Primitive Tests - Slice Pool
// ==============================================================================
// Unit tests for SlicePool (spec 069 - Pattern Freeze Mode).
//
// Tests verify:
// - Allocation and deallocation of slices
// - Pool size management
// - Slice state tracking
// - Edge cases and bounds
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-first development methodology
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/slice_pool.h>

#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("SlicePool prepares with correct capacity", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(8, 44100.0, 100);  // 8 slices, 100 samples each

    REQUIRE(pool.getMaxSlices() == 8);
    REQUIRE(pool.getMaxSliceSamples() == 100);
}

TEST_CASE("SlicePool reset returns all slices to available", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(4, 44100.0, 100);

    // Allocate some slices
    for (int i = 0; i < 3; ++i) {
        [[maybe_unused]] auto* slice = pool.allocateSlice();
    }

    pool.reset();

    // All slices should be available again
    REQUIRE(pool.getAvailableSlices() == 4);
}

// =============================================================================
// Allocation Tests
// =============================================================================

TEST_CASE("SlicePool allocates slices", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(4, 44100.0, 100);

    REQUIRE(pool.getAvailableSlices() == 4);

    Slice* slice1 = pool.allocateSlice();
    REQUIRE(slice1 != nullptr);
    REQUIRE(pool.getAvailableSlices() == 3);

    Slice* slice2 = pool.allocateSlice();
    REQUIRE(slice2 != nullptr);
    REQUIRE(slice2 != slice1);  // Different slices
    REQUIRE(pool.getAvailableSlices() == 2);
}

TEST_CASE("SlicePool returns nullptr when exhausted", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(2, 44100.0, 100);

    [[maybe_unused]] auto* s1 = pool.allocateSlice();
    [[maybe_unused]] auto* s2 = pool.allocateSlice();

    // Pool is now empty
    Slice* slice = pool.allocateSlice();
    REQUIRE(slice == nullptr);
}

TEST_CASE("SlicePool deallocates slices for reuse", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(2, 44100.0, 100);

    Slice* slice1 = pool.allocateSlice();
    Slice* slice2 = pool.allocateSlice();

    REQUIRE(pool.getAvailableSlices() == 0);

    pool.deallocateSlice(slice1);
    REQUIRE(pool.getAvailableSlices() == 1);

    // Can allocate again
    Slice* slice3 = pool.allocateSlice();
    REQUIRE(slice3 != nullptr);
    REQUIRE(pool.getAvailableSlices() == 0);

    pool.deallocateSlice(slice2);
    pool.deallocateSlice(slice3);
    REQUIRE(pool.getAvailableSlices() == 2);
}

// =============================================================================
// Slice Data Access Tests
// =============================================================================

TEST_CASE("Slice buffers are accessible", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(2, 44100.0, 100);

    Slice* slice = pool.allocateSlice();
    REQUIRE(slice != nullptr);

    // Can access left and right buffers
    float* left = slice->getLeft();
    float* right = slice->getRight();

    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    REQUIRE(left != right);  // Different buffers

    // Can write to buffers
    for (int i = 0; i < 100; ++i) {
        left[i] = static_cast<float>(i) * 0.01f;
        right[i] = static_cast<float>(i) * -0.01f;
    }

    // Can read back
    REQUIRE(left[50] == Approx(0.5f));
    REQUIRE(right[50] == Approx(-0.5f));
}

TEST_CASE("Slice tracks length", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(2, 44100.0, 1000);

    Slice* slice = pool.allocateSlice();

    slice->setLength(500);
    REQUIRE(slice->getLength() == 500);

    slice->setLength(1500);  // Exceeds max
    REQUIRE(slice->getLength() <= 1000);  // Should clamp
}

TEST_CASE("Slice tracks playback position", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(2, 44100.0, 100);

    Slice* slice = pool.allocateSlice();
    slice->setLength(50);

    REQUIRE(slice->getPosition() == 0);
    REQUIRE(slice->isComplete() == false);

    slice->advancePosition(25);
    REQUIRE(slice->getPosition() == 25);
    REQUIRE(slice->isComplete() == false);

    slice->advancePosition(30);  // Goes beyond length
    REQUIRE(slice->getPosition() >= 50);
    REQUIRE(slice->isComplete() == true);
}

TEST_CASE("Slice resets position", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(2, 44100.0, 100);

    Slice* slice = pool.allocateSlice();
    slice->setLength(50);
    slice->advancePosition(30);

    slice->resetPosition();
    REQUIRE(slice->getPosition() == 0);
    REQUIRE(slice->isComplete() == false);
}

// =============================================================================
// Envelope Support Tests
// =============================================================================

TEST_CASE("Slice stores envelope phase", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(2, 44100.0, 100);

    Slice* slice = pool.allocateSlice();

    slice->setEnvelopePhase(0.5f);
    REQUIRE(slice->getEnvelopePhase() == Approx(0.5f));

    slice->setEnvelopePhase(1.5f);  // Out of range
    REQUIRE(slice->getEnvelopePhase() <= 1.0f);  // Should clamp
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("SlicePool handles zero slices gracefully", "[primitives][slice_pool][layer1][edge]") {
    SlicePool pool;
    pool.prepare(0, 44100.0, 100);

    REQUIRE(pool.getMaxSlices() == 0);
    REQUIRE(pool.getAvailableSlices() == 0);

    Slice* slice = pool.allocateSlice();
    REQUIRE(slice == nullptr);
}

TEST_CASE("SlicePool handles deallocate nullptr", "[primitives][slice_pool][layer1][edge]") {
    SlicePool pool;
    pool.prepare(4, 44100.0, 100);

    // Should not crash
    pool.deallocateSlice(nullptr);

    REQUIRE(pool.getAvailableSlices() == 4);  // Unchanged
}

TEST_CASE("SlicePool handles double deallocation gracefully", "[primitives][slice_pool][layer1][edge]") {
    SlicePool pool;
    pool.prepare(2, 44100.0, 100);

    Slice* slice = pool.allocateSlice();
    pool.deallocateSlice(slice);

    // Double deallocation should be safe (though undefined behavior in real use)
    // The implementation should handle this gracefully
    pool.deallocateSlice(slice);

    // Should have at most 2 available (original pool size)
    REQUIRE(pool.getAvailableSlices() <= 2);
}

// =============================================================================
// Real-Time Safety Tests
// =============================================================================

TEST_CASE("SlicePool allocateSlice is noexcept", "[primitives][slice_pool][layer1][realtime]") {
    SlicePool pool;
    static_assert(noexcept(pool.allocateSlice()),
                  "allocateSlice() must be noexcept");
}

TEST_CASE("SlicePool deallocateSlice is noexcept", "[primitives][slice_pool][layer1][realtime]") {
    SlicePool pool;
    Slice* ptr = nullptr;
    static_assert(noexcept(pool.deallocateSlice(ptr)),
                  "deallocateSlice() must be noexcept");
}

// =============================================================================
// Active Slice Query Tests
// =============================================================================

TEST_CASE("SlicePool tracks active slices", "[primitives][slice_pool][layer1]") {
    SlicePool pool;
    pool.prepare(4, 44100.0, 100);

    REQUIRE(pool.getActiveSlices() == 0);

    Slice* s1 = pool.allocateSlice();
    REQUIRE(pool.getActiveSlices() == 1);

    Slice* s2 = pool.allocateSlice();
    REQUIRE(pool.getActiveSlices() == 2);

    pool.deallocateSlice(s1);
    REQUIRE(pool.getActiveSlices() == 1);

    pool.deallocateSlice(s2);
    REQUIRE(pool.getActiveSlices() == 0);
}
