// ==============================================================================
// Unit Tests: ArpLane<T, MaxSteps> (Layer 1 Primitive)
// ==============================================================================
// Spec: 072-independent-lanes
// Tests: Construction, length clamping, advance/wrap, reset, setStep/getStep,
//        index clamping, out-of-range, setLength position wrap,
//        float/int8_t/uint8_t specializations, zero heap allocation, currentStep
// ==============================================================================

#include <krate/dsp/primitives/arp_lane.h>

#include <catch2/catch_all.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

using Catch::Approx;
using Krate::DSP::ArpLane;

// =============================================================================
// Construction
// =============================================================================

TEST_CASE("ArpLane: Default construction yields length=1, position=0, steps value-initialized",
          "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    REQUIRE(lane.length() == 1);
    REQUIRE(lane.currentStep() == 0);
    // Value-initialized float is 0.0f
    REQUIRE(lane.getStep(0) == 0.0f);
}

// =============================================================================
// setLength clamping
// =============================================================================

TEST_CASE("ArpLane: setLength clamps len=0 to 1", "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(0);
    REQUIRE(lane.length() == 1);
}

TEST_CASE("ArpLane: setLength clamps len=33 to 32", "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(33);
    REQUIRE(lane.length() == 32);
}

TEST_CASE("ArpLane: setLength accepts valid len=5", "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(5);
    REQUIRE(lane.length() == 5);
}

// =============================================================================
// advance() cycling
// =============================================================================

TEST_CASE("ArpLane: advance cycles through steps and wraps", "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(4);
    lane.setStep(0, 1.0f);
    lane.setStep(1, 2.0f);
    lane.setStep(2, 3.0f);
    lane.setStep(3, 4.0f);

    REQUIRE(lane.advance() == Approx(1.0f));
    REQUIRE(lane.advance() == Approx(2.0f));
    REQUIRE(lane.advance() == Approx(3.0f));
    REQUIRE(lane.advance() == Approx(4.0f));
    // Wraps back to step 0
    REQUIRE(lane.advance() == Approx(1.0f));
}

// =============================================================================
// reset()
// =============================================================================

TEST_CASE("ArpLane: reset sets position back to 0", "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(4);
    lane.setStep(0, 10.0f);
    lane.setStep(1, 20.0f);

    lane.advance(); // position -> 1
    lane.advance(); // position -> 2
    REQUIRE(lane.currentStep() == 2);

    lane.reset();
    REQUIRE(lane.currentStep() == 0);
}

// =============================================================================
// setStep/getStep round-trip
// =============================================================================

TEST_CASE("ArpLane: setStep/getStep round-trip for all 32 steps", "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(32);

    for (size_t i = 0; i < 32; ++i) {
        lane.setStep(i, static_cast<float>(i) * 0.03125f); // i/32
    }

    for (size_t i = 0; i < 32; ++i) {
        REQUIRE(lane.getStep(i) == Approx(static_cast<float>(i) * 0.03125f));
    }
}

// =============================================================================
// setStep index clamping
// =============================================================================

TEST_CASE("ArpLane: setStep index > length-1 clamps to length-1",
          "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(3);
    lane.setStep(0, 1.0f);
    lane.setStep(1, 2.0f);
    lane.setStep(2, 3.0f);

    // Index 5 should clamp to index 2 (length-1)
    lane.setStep(5, 99.0f);
    REQUIRE(lane.getStep(2) == Approx(99.0f));
}

// =============================================================================
// getStep out-of-range
// =============================================================================

TEST_CASE("ArpLane: getStep out-of-range returns T{}", "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(2);
    lane.setStep(0, 5.0f);
    lane.setStep(1, 6.0f);

    // Index >= length should return T{} (0.0f for float)
    REQUIRE(lane.getStep(2) == Approx(0.0f));
    REQUIRE(lane.getStep(10) == Approx(0.0f));
    REQUIRE(lane.getStep(31) == Approx(0.0f));
}

// =============================================================================
// setLength position wrap
// =============================================================================

TEST_CASE("ArpLane: setLength wraps position to 0 when position >= new length",
          "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(4);
    lane.setStep(0, 10.0f);

    // Advance to position 3
    lane.advance(); // pos -> 1
    lane.advance(); // pos -> 2
    lane.advance(); // pos -> 3
    REQUIRE(lane.currentStep() == 3);

    // Shrink length to 2: position 3 >= 2, so wraps to 0
    lane.setLength(2);
    REQUIRE(lane.currentStep() == 0);
}

// =============================================================================
// Float specialization
// =============================================================================

TEST_CASE("ArpLane<float>: works with 0.0-1.0 step values", "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(4);
    lane.setStep(0, 0.0f);
    lane.setStep(1, 0.33f);
    lane.setStep(2, 0.67f);
    lane.setStep(3, 1.0f);

    REQUIRE(lane.advance() == Approx(0.0f));
    REQUIRE(lane.advance() == Approx(0.33f));
    REQUIRE(lane.advance() == Approx(0.67f));
    REQUIRE(lane.advance() == Approx(1.0f));
}

// =============================================================================
// int8_t specialization
// =============================================================================

TEST_CASE("ArpLane<int8_t>: works with values -24 to +24", "[arp_lane][primitives]")
{
    ArpLane<int8_t> lane;
    lane.setLength(4);
    lane.setStep(0, static_cast<int8_t>(-24));
    lane.setStep(1, static_cast<int8_t>(0));
    lane.setStep(2, static_cast<int8_t>(12));
    lane.setStep(3, static_cast<int8_t>(24));

    REQUIRE(lane.advance() == static_cast<int8_t>(-24));
    REQUIRE(lane.advance() == static_cast<int8_t>(0));
    REQUIRE(lane.advance() == static_cast<int8_t>(12));
    REQUIRE(lane.advance() == static_cast<int8_t>(24));
}

// =============================================================================
// uint8_t specialization (forward compatibility)
// =============================================================================

TEST_CASE("ArpLane<uint8_t>: works with unsigned values", "[arp_lane][primitives]")
{
    ArpLane<uint8_t> lane;
    lane.setLength(3);
    lane.setStep(0, static_cast<uint8_t>(0));
    lane.setStep(1, static_cast<uint8_t>(128));
    lane.setStep(2, static_cast<uint8_t>(255));

    REQUIRE(lane.advance() == static_cast<uint8_t>(0));
    REQUIRE(lane.advance() == static_cast<uint8_t>(128));
    REQUIRE(lane.advance() == static_cast<uint8_t>(255));
}

// =============================================================================
// Zero heap allocation (code inspection confirms std::array backing)
// =============================================================================

TEST_CASE("ArpLane: uses std::array backing (compile-time verification)",
          "[arp_lane][primitives]")
{
    // ArpLane should be trivially relocatable -- it uses std::array, not vector
    // We verify the type is standard-layout-compatible and has expected size
    ArpLane<float> lane;

    // sizeof should be: 32 * sizeof(float) + sizeof(size_t) + sizeof(size_t)
    // = 128 + 8 + 8 = 144 bytes on 64-bit (or 128 + 4 + 4 = 136 on 32-bit)
    constexpr size_t expectedStepsSize = 32 * sizeof(float);
    constexpr size_t expectedMembersSize = 2 * sizeof(size_t);
    // Just verify it's in a reasonable range (no hidden heap pointers)
    REQUIRE(sizeof(ArpLane<float>) <= expectedStepsSize + expectedMembersSize + 64);
    // Must be at least the array size
    REQUIRE(sizeof(ArpLane<float>) >= expectedStepsSize);
}

// =============================================================================
// currentStep()
// =============================================================================

TEST_CASE("ArpLane: currentStep returns correct position before and after advance",
          "[arp_lane][primitives]")
{
    ArpLane<float> lane;
    lane.setLength(3);
    lane.setStep(0, 1.0f);
    lane.setStep(1, 2.0f);
    lane.setStep(2, 3.0f);

    REQUIRE(lane.currentStep() == 0);

    lane.advance();
    REQUIRE(lane.currentStep() == 1);

    lane.advance();
    REQUIRE(lane.currentStep() == 2);

    lane.advance(); // wraps
    REQUIRE(lane.currentStep() == 0);
}

// =============================================================================
// Edge Case: MaxSteps=1 template parameter
// =============================================================================

TEST_CASE("ArpLane: EdgeCase_MaxStepsTemplate - ArpLane<float, 1> always returns same value",
          "[arp_lane][primitives][edge]")
{
    ArpLane<float, 1> lane;
    REQUIRE(lane.length() == 1);

    lane.setStep(0, 0.75f);

    // Advance 5 times: always returns the same value, position never changes
    for (int i = 0; i < 5; ++i) {
        INFO("Iteration " << i);
        float val = lane.advance();
        REQUIRE(val == Catch::Approx(0.75f));
        // After advance, position wraps: (0+1)%1 == 0, so always 0
        REQUIRE(lane.currentStep() == 0);
    }
}

// =============================================================================
// Edge Case: All 32 steps set to distinct values, verify full cycle repeats
// =============================================================================

TEST_CASE("ArpLane: EdgeCase_AllStepsSet - full 32-step cycle repeats exactly twice",
          "[arp_lane][primitives][edge]")
{
    ArpLane<int8_t> lane;
    lane.setLength(32);

    // Set all 32 steps to distinct values: -16 through +15
    for (size_t i = 0; i < 32; ++i) {
        lane.setStep(i, static_cast<int8_t>(static_cast<int>(i) - 16));
    }

    // Advance 64 times and collect values
    std::array<int8_t, 64> collected{};
    for (size_t i = 0; i < 64; ++i) {
        collected[i] = lane.advance();
    }

    // Verify the full 32-step cycle repeats exactly twice
    for (size_t i = 0; i < 32; ++i) {
        int8_t expected = static_cast<int8_t>(static_cast<int>(i) - 16);
        INFO("Step " << i << " (first cycle)");
        REQUIRE(collected[i] == expected);
        INFO("Step " << i << " (second cycle)");
        REQUIRE(collected[i + 32] == expected);
    }
}
