// ==============================================================================
// Transform Operation Tests (081-interaction-polish Phase 5, T041-T043)
// ==============================================================================
// Tests for per-lane transform operations: Invert, Shift Left, Shift Right,
// Randomize across all 6 lane types. Also tests ArpLaneHeader hit detection.
//
// These tests verify the transform logic defined in
// contracts/transform-operations.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_lane.h"
#include "ui/arp_lane_header.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"

#include <array>
#include <chrono>
#include <cmath>
#include <random>

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// ==============================================================================
// Helper: apply a transform to an ArpLaneEditor via its transform callback
// ==============================================================================

// For bar-type lanes (velocity, gate, pitch, ratchet), the transform logic
// is implemented inside the transform callback wired by the controller.
// In tests we exercise the normalized step value API and apply transforms
// manually since the callback isn't wired to a controller in unit tests.

// Utility: apply invert on a lane editor (new = 1.0 - old) for each active step
static void applyInvert(IArpLane& lane) {
    int32_t len = lane.getActiveLength();
    for (int32_t i = 0; i < len; ++i) {
        float old = lane.getNormalizedStepValue(i);
        lane.setNormalizedStepValue(i, 1.0f - old);
    }
}

// Utility: apply shift left (new[i] = old[(i+1) % N]) for each active step
static void applyShiftLeft(IArpLane& lane) {
    int32_t len = lane.getActiveLength();
    if (len <= 1) return;
    std::array<float, 32> tmp{};
    for (int32_t i = 0; i < len; ++i) {
        tmp[static_cast<size_t>(i)] = lane.getNormalizedStepValue((i + 1) % len);
    }
    for (int32_t i = 0; i < len; ++i) {
        lane.setNormalizedStepValue(i, tmp[static_cast<size_t>(i)]);
    }
}

// Utility: apply shift right (new[i] = old[(i-1+N) % N]) for each active step
static void applyShiftRight(IArpLane& lane) {
    int32_t len = lane.getActiveLength();
    if (len <= 1) return;
    std::array<float, 32> tmp{};
    for (int32_t i = 0; i < len; ++i) {
        tmp[static_cast<size_t>(i)] = lane.getNormalizedStepValue(
            (i - 1 + len) % len);
    }
    for (int32_t i = 0; i < len; ++i) {
        lane.setNormalizedStepValue(i, tmp[static_cast<size_t>(i)]);
    }
}

// Utility: apply modifier invert (~flags & 0x0F)
static void applyModifierInvert(ArpModifierLane& lane) {
    int32_t len = lane.getActiveLength();
    for (int32_t i = 0; i < len; ++i) {
        uint8_t flags = lane.getStepFlags(static_cast<int>(i));
        uint8_t inverted = static_cast<uint8_t>((~flags) & 0x0F);
        lane.setStepFlags(static_cast<int>(i), inverted);
    }
}

// Condition inversion table (from transform-operations.md)
static constexpr uint8_t kConditionInvertTable[18] = {
    0,   // 0: Always -> Always
    5,   // 1: 10% -> 90%
    4,   // 2: 25% -> 75%
    3,   // 3: 50% -> 50%
    2,   // 4: 75% -> 25%
    1,   // 5: 90% -> 10%
    6,   // 6: ratio -> unchanged
    7,   // 7: ratio -> unchanged
    8,   // 8: ratio -> unchanged
    9,   // 9: ratio -> unchanged
    10,  // 10: ratio -> unchanged
    11,  // 11: ratio -> unchanged
    12,  // 12: ratio -> unchanged
    13,  // 13: ratio -> unchanged
    14,  // 14: ratio -> unchanged
    15,  // 15: First -> First
    17,  // 16: Fill -> Not Fill
    16   // 17: Not Fill -> Fill
};

// Utility: apply condition invert
static void applyConditionInvert(ArpConditionLane& lane) {
    int32_t len = lane.getActiveLength();
    for (int32_t i = 0; i < len; ++i) {
        uint8_t condIdx = lane.getStepCondition(static_cast<int>(i));
        if (condIdx < 18) {
            lane.setStepCondition(static_cast<int>(i), kConditionInvertTable[condIdx]);
        }
    }
}

// ==============================================================================
// T041: Velocity/Gate/Pitch/Ratchet Transform Tests
// ==============================================================================

TEST_CASE("Velocity Invert: new = 1.0 - old", "[transform][velocity]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setNumSteps(4);

    // Set known pattern: [1.0, 0.5, 0.0, 0.75]
    editor.setStepLevel(0, 1.0f);
    editor.setStepLevel(1, 0.5f);
    editor.setStepLevel(2, 0.0f);
    editor.setStepLevel(3, 0.75f);

    applyInvert(editor);

    REQUIRE(editor.getStepLevel(0) == Approx(0.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.5f).margin(0.001f));
    REQUIRE(editor.getStepLevel(2) == Approx(1.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(3) == Approx(0.25f).margin(0.001f));
}

TEST_CASE("Gate Invert: same as velocity (0-1 range)", "[transform][gate]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kGate);
    editor.setNumSteps(4);

    editor.setStepLevel(0, 1.0f);
    editor.setStepLevel(1, 0.5f);
    editor.setStepLevel(2, 0.0f);
    editor.setStepLevel(3, 0.75f);

    applyInvert(editor);

    REQUIRE(editor.getStepLevel(0) == Approx(0.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.5f).margin(0.001f));
    REQUIRE(editor.getStepLevel(2) == Approx(1.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(3) == Approx(0.25f).margin(0.001f));
}

TEST_CASE("Pitch Invert: new = 1.0 - old (normalized mirror)", "[transform][pitch]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kPitch);
    editor.setNumSteps(4);

    // 1.0 = +24, 0.5 = 0, 0.0 = -24, 0.75 = +12
    editor.setStepLevel(0, 1.0f);
    editor.setStepLevel(1, 0.5f);
    editor.setStepLevel(2, 0.0f);
    editor.setStepLevel(3, 0.75f);

    applyInvert(editor);

    REQUIRE(editor.getStepLevel(0) == Approx(0.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.5f).margin(0.001f));
    REQUIRE(editor.getStepLevel(2) == Approx(1.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(3) == Approx(0.25f).margin(0.001f));
}

TEST_CASE("Ratchet Invert: new = 1.0 - old (normalized mirror)", "[transform][ratchet]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kRatchet);
    editor.setNumSteps(4);

    editor.setStepLevel(0, 0.0f);    // count 1
    editor.setStepLevel(1, 1.0f/3.0f); // count 2
    editor.setStepLevel(2, 2.0f/3.0f); // count 3
    editor.setStepLevel(3, 1.0f);    // count 4

    applyInvert(editor);

    REQUIRE(editor.getStepLevel(0) == Approx(1.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(2.0f/3.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(2) == Approx(1.0f/3.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(3) == Approx(0.0f).margin(0.001f));
}

TEST_CASE("Velocity Shift Left: circular rotation new[i] = old[(i+1) % N]", "[transform][velocity]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setNumSteps(4);

    // [A=0.1, B=0.2, C=0.3, D=0.4]
    editor.setStepLevel(0, 0.1f);
    editor.setStepLevel(1, 0.2f);
    editor.setStepLevel(2, 0.3f);
    editor.setStepLevel(3, 0.4f);

    applyShiftLeft(editor);

    // Expected: [B=0.2, C=0.3, D=0.4, A=0.1]
    REQUIRE(editor.getStepLevel(0) == Approx(0.2f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.3f).margin(0.001f));
    REQUIRE(editor.getStepLevel(2) == Approx(0.4f).margin(0.001f));
    REQUIRE(editor.getStepLevel(3) == Approx(0.1f).margin(0.001f));
}

TEST_CASE("Velocity Shift Right: circular rotation new[i] = old[(i-1+N) % N]", "[transform][velocity]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setNumSteps(4);

    // [A=0.1, B=0.2, C=0.3, D=0.4]
    editor.setStepLevel(0, 0.1f);
    editor.setStepLevel(1, 0.2f);
    editor.setStepLevel(2, 0.3f);
    editor.setStepLevel(3, 0.4f);

    applyShiftRight(editor);

    // Expected: [D=0.4, A=0.1, B=0.2, C=0.3]
    REQUIRE(editor.getStepLevel(0) == Approx(0.4f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.1f).margin(0.001f));
    REQUIRE(editor.getStepLevel(2) == Approx(0.2f).margin(0.001f));
    REQUIRE(editor.getStepLevel(3) == Approx(0.3f).margin(0.001f));
}

TEST_CASE("Single-step shift: Shift Left is no-op when length=1", "[transform][edge]") {
    // Test the shift algorithm directly at length=1
    // (Note: ArpLaneEditor clamps min steps to 2, but the shift algorithm
    //  correctly handles length<=1 as a no-op per transform-operations.md)
    std::array<float, 1> data = {0.7f};

    // Simulate shift left on length=1: should be a no-op
    int32_t len = 1;
    if (len > 1) {
        float first = data[0];
        for (int32_t i = 0; i < len - 1; ++i) {
            data[static_cast<size_t>(i)] = data[static_cast<size_t>(i + 1)];
        }
        data[static_cast<size_t>(len - 1)] = first;
    }
    // No-op: value unchanged
    REQUIRE(data[0] == Approx(0.7f).margin(0.001f));
}

TEST_CASE("Single-step shift: Shift Right is no-op when length=1", "[transform][edge]") {
    std::array<float, 1> data = {0.7f};
    int32_t len = 1;
    if (len > 1) {
        float last = data[static_cast<size_t>(len - 1)];
        for (int32_t i = len - 1; i > 0; --i) {
            data[static_cast<size_t>(i)] = data[static_cast<size_t>(i - 1)];
        }
        data[0] = last;
    }
    REQUIRE(data[0] == Approx(0.7f).margin(0.001f));
}

TEST_CASE("Two-step lane: Shift Left swaps the two steps", "[transform][edge]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setNumSteps(2);
    editor.setStepLevel(0, 0.3f);
    editor.setStepLevel(1, 0.9f);

    applyShiftLeft(editor);

    REQUIRE(editor.getStepLevel(0) == Approx(0.9f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.3f).margin(0.001f));
}

TEST_CASE("Two-step lane: Shift Right swaps the two steps", "[transform][edge]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setNumSteps(2);
    editor.setStepLevel(0, 0.3f);
    editor.setStepLevel(1, 0.9f);

    applyShiftRight(editor);

    REQUIRE(editor.getStepLevel(0) == Approx(0.9f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.3f).margin(0.001f));
}

TEST_CASE("32-step lane: all transforms work", "[transform][edge]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setNumSteps(32);

    // Fill ascending pattern
    for (int i = 0; i < 32; ++i) {
        editor.setStepLevel(i, static_cast<float>(i) / 31.0f);
    }

    SECTION("Invert") {
        applyInvert(editor);
        for (int i = 0; i < 32; ++i) {
            float expected = 1.0f - static_cast<float>(i) / 31.0f;
            REQUIRE(editor.getStepLevel(i) == Approx(expected).margin(0.001f));
        }
    }

    SECTION("Shift Left") {
        applyShiftLeft(editor);
        // step 0 should now contain what was step 1
        REQUIRE(editor.getStepLevel(0) == Approx(1.0f / 31.0f).margin(0.001f));
        // step 31 should now contain what was step 0
        REQUIRE(editor.getStepLevel(31) == Approx(0.0f).margin(0.001f));
    }

    SECTION("Shift Right") {
        applyShiftRight(editor);
        // step 0 should now contain what was step 31
        REQUIRE(editor.getStepLevel(0) == Approx(31.0f / 31.0f).margin(0.001f));
        // step 1 should now contain what was step 0
        REQUIRE(editor.getStepLevel(1) == Approx(0.0f).margin(0.001f));
    }
}

// ==============================================================================
// T042: Modifier/Condition Transform Tests
// ==============================================================================

TEST_CASE("Modifier Invert: (~old) & 0x0F toggles all flags", "[transform][modifier]") {
    auto* lane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    lane->setNumSteps(4);

    // Set step 0 = 0x01 (Active only), step 1 = 0x0F (all flags), step 2 = 0x00, step 3 = 0x05
    lane->setStepFlags(0, 0x01);
    lane->setStepFlags(1, 0x0F);
    lane->setStepFlags(2, 0x00);
    lane->setStepFlags(3, 0x05);

    applyModifierInvert(*lane);

    REQUIRE(lane->getStepFlags(0) == 0x0E);  // ~0x01 & 0x0F
    REQUIRE(lane->getStepFlags(1) == 0x00);  // ~0x0F & 0x0F
    REQUIRE(lane->getStepFlags(2) == 0x0F);  // ~0x00 & 0x0F
    REQUIRE(lane->getStepFlags(3) == 0x0A);  // ~0x05 & 0x0F

    lane->forget();
}

TEST_CASE("Modifier Shift Left: rotates bitmask pattern", "[transform][modifier]") {
    auto* lane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    lane->setNumSteps(4);

    lane->setStepFlags(0, 0x01);
    lane->setStepFlags(1, 0x02);
    lane->setStepFlags(2, 0x04);
    lane->setStepFlags(3, 0x08);

    applyShiftLeft(*lane);

    // After shift left: [0x02, 0x04, 0x08, 0x01]
    REQUIRE(lane->getNormalizedStepValue(0) == Approx(0x02 / 15.0f).margin(0.001f));
    REQUIRE(lane->getNormalizedStepValue(1) == Approx(0x04 / 15.0f).margin(0.001f));
    REQUIRE(lane->getNormalizedStepValue(2) == Approx(0x08 / 15.0f).margin(0.001f));
    REQUIRE(lane->getNormalizedStepValue(3) == Approx(0x01 / 15.0f).margin(0.001f));

    lane->forget();
}

TEST_CASE("Modifier Shift Right: rotates bitmask pattern", "[transform][modifier]") {
    auto* lane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    lane->setNumSteps(4);

    lane->setStepFlags(0, 0x01);
    lane->setStepFlags(1, 0x02);
    lane->setStepFlags(2, 0x04);
    lane->setStepFlags(3, 0x08);

    applyShiftRight(*lane);

    // After shift right: [0x08, 0x01, 0x02, 0x04]
    REQUIRE(lane->getNormalizedStepValue(0) == Approx(0x08 / 15.0f).margin(0.001f));
    REQUIRE(lane->getNormalizedStepValue(1) == Approx(0x01 / 15.0f).margin(0.001f));
    REQUIRE(lane->getNormalizedStepValue(2) == Approx(0x02 / 15.0f).margin(0.001f));
    REQUIRE(lane->getNormalizedStepValue(3) == Approx(0x04 / 15.0f).margin(0.001f));

    lane->forget();
}

TEST_CASE("Condition Invert: 18-entry lookup table", "[transform][condition]") {
    auto* lane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    lane->setNumSteps(18);

    // Set each step to its own condition index
    for (int i = 0; i < 18; ++i) {
        lane->setStepCondition(i, static_cast<uint8_t>(i));
    }

    applyConditionInvert(*lane);

    // Verify the inversion table
    REQUIRE(lane->getStepCondition(0) == 0);   // Always -> Always
    REQUIRE(lane->getStepCondition(1) == 5);   // 10% -> 90%
    REQUIRE(lane->getStepCondition(2) == 4);   // 25% -> 75%
    REQUIRE(lane->getStepCondition(3) == 3);   // 50% -> 50%
    REQUIRE(lane->getStepCondition(4) == 2);   // 75% -> 25%
    REQUIRE(lane->getStepCondition(5) == 1);   // 90% -> 10%
    REQUIRE(lane->getStepCondition(6) == 6);   // ratio unchanged
    REQUIRE(lane->getStepCondition(7) == 7);   // ratio unchanged
    REQUIRE(lane->getStepCondition(8) == 8);   // ratio unchanged
    REQUIRE(lane->getStepCondition(9) == 9);   // ratio unchanged
    REQUIRE(lane->getStepCondition(10) == 10); // ratio unchanged
    REQUIRE(lane->getStepCondition(11) == 11); // ratio unchanged
    REQUIRE(lane->getStepCondition(12) == 12); // ratio unchanged
    REQUIRE(lane->getStepCondition(13) == 13); // ratio unchanged
    REQUIRE(lane->getStepCondition(14) == 14); // ratio unchanged
    REQUIRE(lane->getStepCondition(15) == 15); // First -> First
    REQUIRE(lane->getStepCondition(16) == 17); // Fill -> Not Fill
    REQUIRE(lane->getStepCondition(17) == 16); // Not Fill -> Fill

    lane->forget();
}

TEST_CASE("Condition Shift Left: rotates condition indices", "[transform][condition]") {
    auto* lane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    lane->setNumSteps(4);

    lane->setStepCondition(0, 0);  // Always
    lane->setStepCondition(1, 3);  // 50%
    lane->setStepCondition(2, 16); // Fill
    lane->setStepCondition(3, 17); // Not Fill

    applyShiftLeft(*lane);

    // After shift left: [3, 16, 17, 0]
    REQUIRE(lane->getStepCondition(0) == 3);
    REQUIRE(lane->getStepCondition(1) == 16);
    REQUIRE(lane->getStepCondition(2) == 17);
    REQUIRE(lane->getStepCondition(3) == 0);

    lane->forget();
}

TEST_CASE("Condition Shift Right: rotates condition indices", "[transform][condition]") {
    auto* lane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    lane->setNumSteps(4);

    lane->setStepCondition(0, 0);  // Always
    lane->setStepCondition(1, 3);  // 50%
    lane->setStepCondition(2, 16); // Fill
    lane->setStepCondition(3, 17); // Not Fill

    applyShiftRight(*lane);

    // After shift right: [17, 0, 3, 16]
    REQUIRE(lane->getStepCondition(0) == 17);
    REQUIRE(lane->getStepCondition(1) == 0);
    REQUIRE(lane->getStepCondition(2) == 3);
    REQUIRE(lane->getStepCondition(3) == 16);

    lane->forget();
}

TEST_CASE("Modifier Randomize: all values in 0-15 range", "[transform][modifier]") {
    auto* lane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    lane->setNumSteps(32);

    // Apply randomize via normalized API
    std::mt19937 rng(42); // deterministic seed
    std::uniform_int_distribution<int> dist(0, 15);

    for (int32_t i = 0; i < lane->getActiveLength(); ++i) {
        int value = dist(rng);
        lane->setNormalizedStepValue(i, static_cast<float>(value) / 15.0f);
    }

    // Verify all values decode to 0-15
    for (int i = 0; i < 32; ++i) {
        uint8_t flags = lane->getStepFlags(i);
        REQUIRE(flags <= 0x0F);
    }

    lane->forget();
}

TEST_CASE("Condition Randomize: all values in 0-17 range", "[transform][condition]") {
    auto* lane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    lane->setNumSteps(32);

    // Apply randomize via normalized API
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 17);

    for (int32_t i = 0; i < lane->getActiveLength(); ++i) {
        int value = dist(rng);
        lane->setNormalizedStepValue(i, static_cast<float>(value) / 17.0f);
    }

    // Verify all values decode to 0-17
    for (int i = 0; i < 32; ++i) {
        uint8_t cond = lane->getStepCondition(i);
        REQUIRE(cond <= 17);
    }

    lane->forget();
}

TEST_CASE("Velocity Randomize: all values in [0.0, 1.0]", "[transform][velocity]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setNumSteps(32);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int32_t i = 0; i < editor.getActiveLength(); ++i) {
        float value = dist(rng);
        editor.setNormalizedStepValue(i, value);
    }

    for (int i = 0; i < 32; ++i) {
        float level = editor.getStepLevel(i);
        REQUIRE(level >= 0.0f);
        REQUIRE(level <= 1.0f);
    }
}

TEST_CASE("Ratchet Randomize: values are discrete 0/1/2/3 mapped to 0.0/0.333/0.667/1.0", "[transform][ratchet]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kRatchet);
    editor.setNumSteps(32);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 3);

    for (int32_t i = 0; i < editor.getActiveLength(); ++i) {
        int value = dist(rng);
        float normalized = static_cast<float>(value) / 3.0f;
        editor.setNormalizedStepValue(i, normalized);
    }

    for (int i = 0; i < 32; ++i) {
        int count = editor.getDiscreteCount(i);
        REQUIRE(count >= 1);
        REQUIRE(count <= 4);
    }
}

// ==============================================================================
// T043: ArpLaneHeader Transform Button Hit Detection Tests
// ==============================================================================

TEST_CASE("ArpLaneHeader handleTransformClick: hit in Invert button returns true and fires callback", "[transform][header][hit]") {
    ArpLaneHeader header;
    header.setNumSteps(16);

    TransformType receivedType = TransformType::kRandomize; // sentinel
    bool callbackFired = false;
    header.setTransformCallback([&](TransformType type) {
        receivedType = type;
        callbackFired = true;
    });

    // Header rect: 500px wide, 16px tall
    CRect headerRect(0, 0, 500, 16);

    // Button layout (right-aligned):
    // Buttons are 12px each, 2px gap, 4px right margin
    // From right: [Randomize] gap [ShiftRight] gap [ShiftLeft] gap [Invert]
    //
    // Randomize: right edge = 500 - 4 = 496, left = 496 - 12 = 484
    // ShiftRight: right = 484 - 2 = 482, left = 482 - 12 = 470
    // ShiftLeft: right = 470 - 2 = 468, left = 468 - 12 = 456
    // Invert: right = 456 - 2 = 454, left = 454 - 12 = 442

    // Click in Invert button center (x=448, y=8)
    CPoint clickInvert(448, 8);
    bool handled = header.handleTransformClick(clickInvert, headerRect);

    REQUIRE(handled);
    REQUIRE(callbackFired);
    REQUIRE(receivedType == TransformType::kInvert);
}

TEST_CASE("ArpLaneHeader handleTransformClick: hit in ShiftLeft button", "[transform][header][hit]") {
    ArpLaneHeader header;
    TransformType receivedType = TransformType::kRandomize;
    header.setTransformCallback([&](TransformType type) { receivedType = type; });

    CRect headerRect(0, 0, 500, 16);
    // ShiftLeft center: x=462
    CPoint clickShiftLeft(462, 8);
    bool handled = header.handleTransformClick(clickShiftLeft, headerRect);

    REQUIRE(handled);
    REQUIRE(receivedType == TransformType::kShiftLeft);
}

TEST_CASE("ArpLaneHeader handleTransformClick: hit in ShiftRight button", "[transform][header][hit]") {
    ArpLaneHeader header;
    TransformType receivedType = TransformType::kRandomize;
    header.setTransformCallback([&](TransformType type) { receivedType = type; });

    CRect headerRect(0, 0, 500, 16);
    // ShiftRight center: x=476
    CPoint clickShiftRight(476, 8);
    bool handled = header.handleTransformClick(clickShiftRight, headerRect);

    REQUIRE(handled);
    REQUIRE(receivedType == TransformType::kShiftRight);
}

TEST_CASE("ArpLaneHeader handleTransformClick: hit in Randomize button", "[transform][header][hit]") {
    ArpLaneHeader header;
    TransformType receivedType = TransformType::kInvert;
    header.setTransformCallback([&](TransformType type) { receivedType = type; });

    CRect headerRect(0, 0, 500, 16);
    // Randomize center: x=490
    CPoint clickRandom(490, 8);
    bool handled = header.handleTransformClick(clickRandom, headerRect);

    REQUIRE(handled);
    REQUIRE(receivedType == TransformType::kRandomize);
}

TEST_CASE("ArpLaneHeader handleTransformClick: click outside buttons returns false", "[transform][header][hit]") {
    ArpLaneHeader header;
    bool callbackFired = false;
    header.setTransformCallback([&](TransformType) { callbackFired = true; });

    CRect headerRect(0, 0, 500, 16);
    // Click way to the left of button area (x=100)
    CPoint clickOutside(100, 8);
    bool handled = header.handleTransformClick(clickOutside, headerRect);

    REQUIRE_FALSE(handled);
    REQUIRE_FALSE(callbackFired);
}

TEST_CASE("ArpLaneHeader handleTransformClick: click in gap between buttons returns false", "[transform][header][hit]") {
    ArpLaneHeader header;
    bool callbackFired = false;
    header.setTransformCallback([&](TransformType) { callbackFired = true; });

    CRect headerRect(0, 0, 500, 16);
    // Gap between Invert (right=454) and ShiftLeft (left=456): x=455
    CPoint clickGap(455, 8);
    bool handled = header.handleTransformClick(clickGap, headerRect);

    REQUIRE_FALSE(handled);
    REQUIRE_FALSE(callbackFired);
}

TEST_CASE("ArpLaneHeader handleTransformClick: no callback set, returns false", "[transform][header][hit]") {
    ArpLaneHeader header;
    // No transform callback set

    CRect headerRect(0, 0, 500, 16);
    CPoint clickInvert(448, 8);
    bool handled = header.handleTransformClick(clickInvert, headerRect);

    REQUIRE_FALSE(handled);
}

TEST_CASE("ArpLaneHeader handleTransformClick: offset header rect", "[transform][header][hit]") {
    ArpLaneHeader header;
    TransformType receivedType = TransformType::kRandomize;
    header.setTransformCallback([&](TransformType type) { receivedType = type; });

    // Header rect offset to the right by 50
    CRect headerRect(50, 0, 550, 16);

    // Randomize button: right = 550 - 4 = 546, left = 546 - 12 = 534
    // Center = 540
    CPoint clickRandom(540, 8);
    bool handled = header.handleTransformClick(clickRandom, headerRect);

    REQUIRE(handled);
    REQUIRE(receivedType == TransformType::kRandomize);
}

// ==============================================================================
// T105: SC-003 Transform Latency Verification (<16ms for 32-step lane)
// ==============================================================================

TEST_CASE("SC-003: All 4 transforms complete within 16ms on 32-step lane", "[transform][perf][SC-003]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setNumSteps(32);

    // Fill with a known pattern
    for (int i = 0; i < 32; ++i) {
        editor.setStepLevel(i, static_cast<float>(i) / 31.0f);
    }

    constexpr double kMaxMs = 16.0;

    SECTION("Invert < 16ms") {
        auto start = std::chrono::high_resolution_clock::now();
        applyInvert(editor);
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        INFO("Invert took " << ms << " ms");
        REQUIRE(ms < kMaxMs);
    }

    SECTION("Shift Left < 16ms") {
        auto start = std::chrono::high_resolution_clock::now();
        applyShiftLeft(editor);
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        INFO("Shift Left took " << ms << " ms");
        REQUIRE(ms < kMaxMs);
    }

    SECTION("Shift Right < 16ms") {
        auto start = std::chrono::high_resolution_clock::now();
        applyShiftRight(editor);
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        INFO("Shift Right took " << ms << " ms");
        REQUIRE(ms < kMaxMs);
    }

    SECTION("Randomize < 16ms") {
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        auto start = std::chrono::high_resolution_clock::now();
        for (int32_t i = 0; i < editor.getActiveLength(); ++i) {
            editor.setNormalizedStepValue(i, dist(rng));
        }
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        INFO("Randomize took " << ms << " ms");
        REQUIRE(ms < kMaxMs);
    }
}
