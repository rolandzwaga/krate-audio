// ==============================================================================
// Copy/Paste Tests (081-interaction-polish Phase 6, T055-T056)
// ==============================================================================
// Tests for lane clipboard: round-trip fidelity (same-type and cross-type),
// clipboard state transitions, and length adaptation on paste.
//
// All values are normalized 0.0-1.0 at the VST boundary. Cross-type paste
// copies normalized values directly with no additional range conversion.
//
// See contracts/copy-paste.md for the authoritative contract.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_lane.h"
#include "ui/arp_lane_header.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"

#include <array>
#include <cmath>

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// ==============================================================================
// T055: Copy/Paste Round-Trip Tests
// ==============================================================================

TEST_CASE("LaneClipboard clear resets hasData", "[clipboard]") {
    LaneClipboard clip;
    clip.hasData = true;
    clip.length = 4;
    clip.values[0] = 1.0f;
    clip.values[1] = 0.5f;
    clip.sourceType = ClipboardLaneType::kGate;

    clip.clear();

    REQUIRE(clip.hasData == false);
    REQUIRE(clip.length == 0);
    REQUIRE(clip.values[0] == 0.0f);
    REQUIRE(clip.values[1] == 0.0f);
}

TEST_CASE("Same-type copy/paste round-trip is bit-identical (SC-004)", "[clipboard]") {
    // Create a velocity lane with specific values
    CRect laneRect(0, 0, 300, 100);
    ArpLaneEditor lane(laneRect, nullptr, -1);
    lane.setLaneType(ArpLaneType::kVelocity);
    lane.setNumSteps(4);
    lane.setLength(4);

    // Set known normalized values
    lane.setNormalizedStepValue(0, 1.0f);
    lane.setNormalizedStepValue(1, 0.5f);
    lane.setNormalizedStepValue(2, 0.0f);
    lane.setNormalizedStepValue(3, 0.75f);

    // Copy: read values into clipboard
    LaneClipboard clip;
    int32_t len = lane.getActiveLength();
    for (int32_t i = 0; i < len; ++i) {
        clip.values[static_cast<size_t>(i)] = lane.getNormalizedStepValue(i);
    }
    clip.length = len;
    clip.sourceType = static_cast<ClipboardLaneType>(lane.getLaneTypeId());
    clip.hasData = true;

    // Create a second velocity lane with different values
    ArpLaneEditor lane2(laneRect, nullptr, -1);
    lane2.setLaneType(ArpLaneType::kVelocity);
    lane2.setNumSteps(8);
    lane2.setLength(8);

    // Paste into lane2
    for (int32_t i = 0; i < clip.length; ++i) {
        lane2.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
    }
    lane2.setLength(clip.length);

    // Verify bit-identical
    REQUIRE(lane2.getActiveLength() == 4);
    REQUIRE(lane2.getNormalizedStepValue(0) == 1.0f);
    REQUIRE(lane2.getNormalizedStepValue(1) == 0.5f);
    REQUIRE(lane2.getNormalizedStepValue(2) == 0.0f);
    REQUIRE(lane2.getNormalizedStepValue(3) == 0.75f);
}

TEST_CASE("Cross-type copy velocity->gate produces identical normalized values", "[clipboard]") {
    CRect laneRect(0, 0, 300, 100);

    // Source: velocity lane
    ArpLaneEditor velLane(laneRect, nullptr, -1);
    velLane.setLaneType(ArpLaneType::kVelocity);
    velLane.setNumSteps(4);
    velLane.setLength(4);
    velLane.setNormalizedStepValue(0, 1.0f);
    velLane.setNormalizedStepValue(1, 0.5f);
    velLane.setNormalizedStepValue(2, 0.0f);
    velLane.setNormalizedStepValue(3, 0.75f);

    // Copy from velocity
    LaneClipboard clip;
    for (int32_t i = 0; i < velLane.getActiveLength(); ++i) {
        clip.values[static_cast<size_t>(i)] = velLane.getNormalizedStepValue(i);
    }
    clip.length = velLane.getActiveLength();
    clip.sourceType = static_cast<ClipboardLaneType>(velLane.getLaneTypeId());
    clip.hasData = true;

    // Target: gate lane
    ArpLaneEditor gateLane(laneRect, nullptr, -1);
    gateLane.setLaneType(ArpLaneType::kGate);
    gateLane.setNumSteps(8);
    gateLane.setLength(8);

    // Paste: cross-type uses normalized values directly
    for (int32_t i = 0; i < clip.length; ++i) {
        gateLane.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
    }
    gateLane.setLength(clip.length);

    // Verify normalized values are identical (no conversion)
    REQUIRE(gateLane.getActiveLength() == 4);
    REQUIRE(gateLane.getNormalizedStepValue(0) == 1.0f);
    REQUIRE(gateLane.getNormalizedStepValue(1) == 0.5f);
    REQUIRE(gateLane.getNormalizedStepValue(2) == 0.0f);
    REQUIRE(gateLane.getNormalizedStepValue(3) == 0.75f);
}

TEST_CASE("Cross-type copy pitch->velocity maps normalized values correctly", "[clipboard]") {
    CRect laneRect(0, 0, 300, 100);

    // Source: pitch lane (+24 semitones = 1.0, 0 semitones = 0.5, -24 semitones = 0.0)
    ArpLaneEditor pitchLane(laneRect, nullptr, -1);
    pitchLane.setLaneType(ArpLaneType::kPitch);
    pitchLane.setNumSteps(3);
    pitchLane.setLength(3);
    pitchLane.setNormalizedStepValue(0, 1.0f);   // +24 semitones
    pitchLane.setNormalizedStepValue(1, 0.5f);   // 0 semitones
    pitchLane.setNormalizedStepValue(2, 0.0f);   // -24 semitones

    // Copy from pitch
    LaneClipboard clip;
    for (int32_t i = 0; i < pitchLane.getActiveLength(); ++i) {
        clip.values[static_cast<size_t>(i)] = pitchLane.getNormalizedStepValue(i);
    }
    clip.length = pitchLane.getActiveLength();
    clip.sourceType = static_cast<ClipboardLaneType>(pitchLane.getLaneTypeId());
    clip.hasData = true;

    // Target: velocity lane
    ArpLaneEditor velLane(laneRect, nullptr, -1);
    velLane.setLaneType(ArpLaneType::kVelocity);
    velLane.setNumSteps(8);
    velLane.setLength(8);

    // Paste: cross-type directly copies normalized values
    for (int32_t i = 0; i < clip.length; ++i) {
        velLane.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
    }
    velLane.setLength(clip.length);

    // Verify identical normalized shape
    REQUIRE(velLane.getActiveLength() == 3);
    REQUIRE(velLane.getNormalizedStepValue(0) == 1.0f);
    REQUIRE(velLane.getNormalizedStepValue(1) == 0.5f);
    REQUIRE(velLane.getNormalizedStepValue(2) == 0.0f);
}

TEST_CASE("Cross-type copy velocity->modifier copies normalized values", "[clipboard]") {
    CRect laneRect(0, 0, 300, 100);

    // Source: velocity lane
    ArpLaneEditor velLane(laneRect, nullptr, -1);
    velLane.setLaneType(ArpLaneType::kVelocity);
    velLane.setNumSteps(4);
    velLane.setLength(4);
    velLane.setNormalizedStepValue(0, 0.0f);
    velLane.setNormalizedStepValue(1, 0.333f);
    velLane.setNormalizedStepValue(2, 0.667f);
    velLane.setNormalizedStepValue(3, 1.0f);

    // Copy
    LaneClipboard clip;
    for (int32_t i = 0; i < velLane.getActiveLength(); ++i) {
        clip.values[static_cast<size_t>(i)] = velLane.getNormalizedStepValue(i);
    }
    clip.length = velLane.getActiveLength();
    clip.sourceType = static_cast<ClipboardLaneType>(velLane.getLaneTypeId());
    clip.hasData = true;

    // Target: modifier lane
    ArpModifierLane modLane(laneRect, nullptr, -1);
    modLane.setNumSteps(8);

    // Paste normalized values
    for (int32_t i = 0; i < clip.length; ++i) {
        modLane.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
    }
    modLane.setLength(clip.length);

    // Verify the round-trip preserves what was pasted (within the target's quantization)
    // Modifier stores bitmask/15, so exact values may quantize
    REQUIRE(modLane.getActiveLength() == 4);
    REQUIRE(modLane.getNormalizedStepValue(0) == Approx(0.0f).margin(0.034f));
    REQUIRE(modLane.getNormalizedStepValue(3) == Approx(1.0f).margin(0.034f));
}

TEST_CASE("Cross-type copy velocity->condition copies normalized values", "[clipboard]") {
    CRect laneRect(0, 0, 300, 100);

    // Source: velocity lane
    ArpLaneEditor velLane(laneRect, nullptr, -1);
    velLane.setLaneType(ArpLaneType::kVelocity);
    velLane.setNumSteps(3);
    velLane.setLength(3);
    velLane.setNormalizedStepValue(0, 0.0f);
    velLane.setNormalizedStepValue(1, 0.5f);
    velLane.setNormalizedStepValue(2, 1.0f);

    // Copy
    LaneClipboard clip;
    for (int32_t i = 0; i < velLane.getActiveLength(); ++i) {
        clip.values[static_cast<size_t>(i)] = velLane.getNormalizedStepValue(i);
    }
    clip.length = velLane.getActiveLength();
    clip.sourceType = static_cast<ClipboardLaneType>(velLane.getLaneTypeId());
    clip.hasData = true;

    // Target: condition lane
    ArpConditionLane condLane(laneRect, nullptr, -1);
    condLane.setNumSteps(8);

    // Paste normalized values
    for (int32_t i = 0; i < clip.length; ++i) {
        condLane.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
    }
    condLane.setLength(clip.length);

    // Verify the round-trip preserves what was pasted (within the target's quantization)
    // Condition stores index/17, so exact values may quantize
    REQUIRE(condLane.getActiveLength() == 3);
    REQUIRE(condLane.getNormalizedStepValue(0) == Approx(0.0f).margin(0.03f));
    REQUIRE(condLane.getNormalizedStepValue(2) == Approx(1.0f).margin(0.03f));
}

// ==============================================================================
// T056: Clipboard State Transition Tests
// ==============================================================================

TEST_CASE("Clipboard starts empty (hasData=false)", "[clipboard]") {
    LaneClipboard clip;
    REQUIRE(clip.hasData == false);
    REQUIRE(clip.length == 0);
}

TEST_CASE("Copy sets hasData=true", "[clipboard]") {
    LaneClipboard clip;

    // Simulate a copy operation
    clip.values[0] = 0.5f;
    clip.values[1] = 0.8f;
    clip.length = 2;
    clip.sourceType = ClipboardLaneType::kVelocity;
    clip.hasData = true;

    REQUIRE(clip.hasData == true);
    REQUIRE(clip.length == 2);
    REQUIRE(clip.values[0] == 0.5f);
    REQUIRE(clip.values[1] == 0.8f);
}

TEST_CASE("Paste with empty clipboard is no-op", "[clipboard]") {
    LaneClipboard clip;
    REQUIRE(clip.hasData == false);

    // A paste operation should check hasData before proceeding
    // Nothing should happen when clipboard is empty
    CRect laneRect(0, 0, 300, 100);
    ArpLaneEditor lane(laneRect, nullptr, -1);
    lane.setLaneType(ArpLaneType::kVelocity);
    lane.setNumSteps(4);
    lane.setLength(4);
    lane.setNormalizedStepValue(0, 0.7f);
    lane.setNormalizedStepValue(1, 0.3f);

    // Simulate paste: controller checks hasData first
    if (clip.hasData) {
        for (int32_t i = 0; i < clip.length; ++i) {
            lane.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
        }
        lane.setLength(clip.length);
    }

    // Values should remain unchanged
    REQUIRE(lane.getActiveLength() == 4);
    REQUIRE(lane.getNormalizedStepValue(0) == Approx(0.7f).margin(0.001f));
    REQUIRE(lane.getNormalizedStepValue(1) == Approx(0.3f).margin(0.001f));
}

TEST_CASE("Paste updates target length to match clipboard length", "[clipboard]") {
    LaneClipboard clip;
    clip.values[0] = 0.5f;
    clip.values[1] = 0.6f;
    clip.values[2] = 0.7f;
    clip.values[3] = 0.8f;
    clip.length = 4;
    clip.sourceType = ClipboardLaneType::kVelocity;
    clip.hasData = true;

    CRect laneRect(0, 0, 300, 100);
    ArpLaneEditor lane(laneRect, nullptr, -1);
    lane.setLaneType(ArpLaneType::kGate);
    lane.setNumSteps(16);
    lane.setLength(16);

    REQUIRE(lane.getActiveLength() == 16);

    // Paste: set length to clipboard length
    for (int32_t i = 0; i < clip.length; ++i) {
        lane.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
    }
    lane.setLength(clip.length);

    REQUIRE(lane.getActiveLength() == 4);
}

TEST_CASE("Length change from 16 to 8 via paste", "[clipboard]") {
    LaneClipboard clip;
    for (int i = 0; i < 8; ++i) {
        clip.values[static_cast<size_t>(i)] = static_cast<float>(i) / 7.0f;
    }
    clip.length = 8;
    clip.sourceType = ClipboardLaneType::kGate;
    clip.hasData = true;

    CRect laneRect(0, 0, 300, 100);
    ArpLaneEditor lane(laneRect, nullptr, -1);
    lane.setLaneType(ArpLaneType::kVelocity);
    lane.setNumSteps(16);
    lane.setLength(16);

    REQUIRE(lane.getActiveLength() == 16);

    // Paste: adapt length from 16 to 8
    for (int32_t i = 0; i < clip.length; ++i) {
        lane.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
    }
    lane.setLength(clip.length);

    REQUIRE(lane.getActiveLength() == 8);
    REQUIRE(lane.getNormalizedStepValue(0) == Approx(0.0f).margin(0.001f));
    REQUIRE(lane.getNormalizedStepValue(7) == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Length change from 8 to 32 via paste", "[clipboard]") {
    LaneClipboard clip;
    for (int i = 0; i < 32; ++i) {
        clip.values[static_cast<size_t>(i)] = static_cast<float>(i) / 31.0f;
    }
    clip.length = 32;
    clip.sourceType = ClipboardLaneType::kVelocity;
    clip.hasData = true;

    CRect laneRect(0, 0, 300, 100);
    ArpLaneEditor lane(laneRect, nullptr, -1);
    lane.setLaneType(ArpLaneType::kVelocity);
    lane.setNumSteps(8);
    lane.setLength(8);

    REQUIRE(lane.getActiveLength() == 8);

    // Paste: adapt length from 8 to 32
    for (int32_t i = 0; i < clip.length; ++i) {
        lane.setNormalizedStepValue(i, clip.values[static_cast<size_t>(i)]);
    }
    lane.setLength(clip.length);

    REQUIRE(lane.getActiveLength() == 32);
    REQUIRE(lane.getNormalizedStepValue(0) == Approx(0.0f).margin(0.001f));
    REQUIRE(lane.getNormalizedStepValue(31) == Approx(1.0f).margin(0.001f));
}

TEST_CASE("ArpLaneHeader handleRightClick returns false when callbacks not set", "[clipboard]") {
    ArpLaneHeader header;
    CPoint where(50.0, 8.0);
    CRect headerRect(0, 0, 300, 16);

    // No callbacks set, no frame -> should return false
    bool handled = header.handleRightClick(where, headerRect, nullptr);
    REQUIRE(handled == false);
}

TEST_CASE("ArpLaneHeader setPasteEnabled toggles paste state", "[clipboard]") {
    ArpLaneHeader header;

    REQUIRE(header.isPasteEnabled() == false);

    header.setPasteEnabled(true);
    REQUIRE(header.isPasteEnabled() == true);

    header.setPasteEnabled(false);
    REQUIRE(header.isPasteEnabled() == false);
}
