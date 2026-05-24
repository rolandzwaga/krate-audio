// =============================================================================
// PianoRollView Visibility + FR-036 Disabled-Controls Tests
// =============================================================================
// Spec 142 (Gradus Piano-Roll Step Sequencer), Phase 6.
//
// FR-027: piano roll view is visible iff Source = Sequencer.
//   We assert this via the kArpSourceModeId parameter value the
//   UIViewSwitchContainer reads (template index 0 = EmptyContent, 1 =
//   PianoRollContent per quickstart Phase D).
//
// FR-036: when Source = Sequencer, the FR-022 control set is visually
//   disabled. The list of disabled IDs lives in
//   `src/controller/source_mode_disable_list.h` (single source of truth
//   shared by the controller wiring and these tests).
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "controller/source_mode_disable_list.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <set>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// -----------------------------------------------------------------------------
// FR-027 — source mode parameter drives view switch
// -----------------------------------------------------------------------------

TEST_CASE("PianoRoll FR-027: source mode defaults to Live (0) → EmptyContent template",
          "[gradus][piano_roll][visibility][fr027]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Default value should be 0 (Live) per FR-001 / data-model.md Entity 1.
    auto* p = controller.getParameterObject(Gradus::kArpSourceModeId);
    REQUIRE(p != nullptr);
    CHECK(p->getNormalized() == Approx(0.0));

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("PianoRoll FR-027: setting source mode to Sequencer (1.0 normalized) → PianoRollContent",
          "[gradus][piano_roll][visibility][fr027]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Toggle to Sequencer.
    REQUIRE(controller.setParamNormalized(Gradus::kArpSourceModeId, 1.0) == kResultOk);
    auto* p = controller.getParameterObject(Gradus::kArpSourceModeId);
    REQUIRE(p != nullptr);
    CHECK(p->getNormalized() == Approx(1.0));

    // Toggle back to Live.
    REQUIRE(controller.setParamNormalized(Gradus::kArpSourceModeId, 0.0) == kResultOk);
    CHECK(p->getNormalized() == Approx(0.0));

    REQUIRE(controller.terminate() == kResultOk);
}

// -----------------------------------------------------------------------------
// FR-036 — disabled controls coverage list
// -----------------------------------------------------------------------------

TEST_CASE("PianoRoll FR-036: disabled-controls list covers all FR-022 base IDs",
          "[gradus][piano_roll][visibility][fr036]")
{
    using namespace Gradus;

    // Top-level controls from the FR-022 list (excluding cell/flag ranges
    // covered by the contiguous-range branch).
    const std::set<uint32_t> required = {
        kArpModeId,
        kArpOctaveRangeId,
        kArpOctaveModeId,
        kArpScaleQuantizeInputId,
        kArpLatchModeId,
        kArpMarkovPresetId,
        kArpEuclideanEnabledId,
        kArpEuclideanHitsId,
        kArpEuclideanStepsId,
        kArpEuclideanRotationId,
        kArpPinNoteId,
        kArpRangeLowId,
        kArpRangeHighId,
        kArpRangeModeId,
    };

    for (auto id : required) {
        INFO("Param ID " << id << " must be in FR-036 disable list");
        CHECK(isSourceSequencerDisabledParam(id));
    }
}

TEST_CASE("PianoRoll FR-036: disabled-controls list covers full Markov cell range",
          "[gradus][piano_roll][visibility][fr036]")
{
    using namespace Gradus;
    for (uint32_t id = kArpMarkovCell00Id; id <= kArpMarkovCell66Id; ++id) {
        INFO("Markov cell " << id);
        CHECK(isSourceSequencerDisabledParam(id));
    }
}

TEST_CASE("PianoRoll FR-036: disabled-controls list covers all 32 Pin Flag steps",
          "[gradus][piano_roll][visibility][fr036]")
{
    using namespace Gradus;
    for (uint32_t id = kArpPinFlagStep0Id; id <= kArpPinFlagStep31Id; ++id) {
        INFO("Pin flag " << id);
        CHECK(isSourceSequencerDisabledParam(id));
    }
}

// -----------------------------------------------------------------------------
// FR-021a / FR-022a / FR-022b — preserved controls must NOT be in the list
// -----------------------------------------------------------------------------
TEST_CASE("PianoRoll FR-021a/022a/022b: preserved controls are NOT disabled in Sequencer mode",
          "[gradus][piano_roll][visibility][fr036]")
{
    using namespace Gradus;
    // FR-021a — Transpose remains enabled.
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpTransposeId));
    // FR-022a — Retrigger remains enabled.
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpRetriggerId));
    // FR-022b — Spice / Dice / Humanize remain enabled.
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpSpiceId));
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpDiceTriggerId));
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpHumanizeId));
    // FR-022b — per-lane speed-curve depth / swing / jitter remain enabled.
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpVelocityLaneSpeedCurveDepthId));
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpVelocityLaneSwingId));
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpVelocityLaneJitterId));
    // Sequencer Note lane modulators stay enabled (lane-9 detail strip).
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpSequencerNoteLaneSpeedId));
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpSequencerNoteLaneSwingId));
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpSequencerNoteLaneJitterId));
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpSequencerNoteLaneSpeedCurveDepthId));
    // Sequencer source mode itself remains interactive.
    CHECK_FALSE(isSourceSequencerDisabledParam(kArpSourceModeId));
}
