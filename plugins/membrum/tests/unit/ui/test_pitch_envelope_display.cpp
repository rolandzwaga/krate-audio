// ==============================================================================
// PitchEnvelopeDisplay -- Unit tests (Phase 6, Spec 141, T078, T079)
// ==============================================================================

#include "ui/pitch_envelope_display.h"
#include "processor/macro_mapper.h"
#include "plugin_ids.h"

#include "vstgui/lib/crect.h"
#include "vstgui/lib/cpoint.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace Membrum;
using namespace Membrum::UI;

namespace {

constexpr VSTGUI::CRect kDefaultRect{ 0.0, 0.0, 360.0, 80.0 };

struct EditEvent
{
    std::uint32_t               paramId;
    PitchEnvelopeDisplay::EditOp op;
    float                        value;
};

using EventLog = std::vector<EditEvent>;

[[nodiscard]] PitchEnvelopeDisplay::EditCallback makeRecorder(EventLog& log)
{
    return [&log](std::uint32_t paramId,
                  PitchEnvelopeDisplay::EditOp op,
                  float value) {
        log.push_back({ paramId, op, value });
    };
}

[[nodiscard]] bool hasBeginPerformEnd(const EventLog& log, std::uint32_t paramId)
{
    bool begin = false, perform = false, end = false;
    for (const auto& e : log)
    {
        if (e.paramId != paramId) continue;
        if (e.op == PitchEnvelopeDisplay::EditOp::Begin)   begin   = true;
        if (e.op == PitchEnvelopeDisplay::EditOp::Perform) perform = true;
        if (e.op == PitchEnvelopeDisplay::EditOp::End)     end     = true;
    }
    return begin && perform && end;
}

} // anonymous namespace

// ------------------------------------------------------------------------------
// T078: Constructor accepts param tags for Start/End/Time/Curve.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay constructor wires default Membrum param tags",
          "[pitch_envelope][phase6]")
{
    PitchEnvelopeDisplay view(kDefaultRect);
    const auto tags = view.paramTags();

    REQUIRE(tags.start == static_cast<std::uint32_t>(kToneShaperPitchEnvStartId));
    REQUIRE(tags.end   == static_cast<std::uint32_t>(kToneShaperPitchEnvEndId));
    REQUIRE(tags.time  == static_cast<std::uint32_t>(kToneShaperPitchEnvTimeId));
    REQUIRE(tags.curve == static_cast<std::uint32_t>(kToneShaperPitchEnvCurveId));
}

TEST_CASE("PitchEnvelopeDisplay constructor accepts custom param tags",
          "[pitch_envelope][phase6]")
{
    PitchEnvelopeDisplay::ParamTags custom{ 7001, 7002, 7003, 7004 };
    PitchEnvelopeDisplay view(kDefaultRect, custom);

    REQUIRE(view.paramTags().start == 7001u);
    REQUIRE(view.paramTags().end   == 7002u);
    REQUIRE(view.paramTags().time  == 7003u);
    REQUIRE(view.paramTags().curve == 7004u);
}

// ------------------------------------------------------------------------------
// T078: Dragging Start vertically fires Begin/Perform/End on Start param.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay dragging Start handle fires begin/perform/end on Start param",
          "[pitch_envelope][phase6]")
{
    PitchEnvelopeDisplay view(kDefaultRect);
    view.setStartNormalized(0.5f);
    view.setEndNormalized(0.25f);
    view.setTimeNormalized(0.0f);

    EventLog log;
    view.setEditCallback(makeRecorder(log));

    // Mouse-down on the Start handle centre.
    const auto startCenter = view.handleCenter(PitchEnvelopeDisplay::DragTarget::Start);
    view.handleMouseDown(startCenter);
    REQUIRE(static_cast<int>(view.activeDrag()) == static_cast<int>(PitchEnvelopeDisplay::DragTarget::Start));

    // Drag vertically up (y decreases -> higher normalised pitch).
    VSTGUI::CPoint dragTo{ startCenter.x, startCenter.y - 20.0 };
    view.handleMouseMove(dragTo);

    view.handleMouseUp(dragTo);
    REQUIRE(static_cast<int>(view.activeDrag()) == static_cast<int>(PitchEnvelopeDisplay::DragTarget::None));

    const auto startId = static_cast<std::uint32_t>(kToneShaperPitchEnvStartId);
    REQUIRE(hasBeginPerformEnd(log, startId));

    // No Perform events were fired against End or Time IDs during the Start drag.
    for (const auto& e : log)
    {
        if (e.op == PitchEnvelopeDisplay::EditOp::Perform)
            REQUIRE(e.paramId == startId);
    }
}

// ------------------------------------------------------------------------------
// T078: Dragging End vertically fires on End param.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay dragging End handle fires begin/perform/end on End param",
          "[pitch_envelope][phase6]")
{
    PitchEnvelopeDisplay view(kDefaultRect);
    view.setStartNormalized(0.75f);
    view.setEndNormalized(0.25f);

    EventLog log;
    view.setEditCallback(makeRecorder(log));

    const auto endCenter = view.handleCenter(PitchEnvelopeDisplay::DragTarget::End);
    view.handleMouseDown(endCenter);
    REQUIRE(static_cast<int>(view.activeDrag()) == static_cast<int>(PitchEnvelopeDisplay::DragTarget::End));

    VSTGUI::CPoint dragTo{ endCenter.x, endCenter.y - 15.0 };
    view.handleMouseMove(dragTo);
    view.handleMouseUp(dragTo);

    const auto endId = static_cast<std::uint32_t>(kToneShaperPitchEnvEndId);
    REQUIRE(hasBeginPerformEnd(log, endId));
}

// ------------------------------------------------------------------------------
// T078: Dragging Time horizontally fires on Time param.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay dragging Time handle fires begin/perform/end on Time param",
          "[pitch_envelope][phase6]")
{
    PitchEnvelopeDisplay view(kDefaultRect);
    view.setStartNormalized(0.5f);
    view.setEndNormalized(0.25f);
    view.setTimeNormalized(0.5f);

    EventLog log;
    view.setEditCallback(makeRecorder(log));

    const auto timeCenter = view.handleCenter(PitchEnvelopeDisplay::DragTarget::Time);
    view.handleMouseDown(timeCenter);
    REQUIRE(static_cast<int>(view.activeDrag()) == static_cast<int>(PitchEnvelopeDisplay::DragTarget::Time));

    VSTGUI::CPoint dragTo{ timeCenter.x + 40.0, timeCenter.y };
    view.handleMouseMove(dragTo);
    view.handleMouseUp(dragTo);

    const auto timeId = static_cast<std::uint32_t>(kToneShaperPitchEnvTimeId);
    REQUIRE(hasBeginPerformEnd(log, timeId));

    // Time handle should respond to horizontal drag: normalised value increased.
    REQUIRE(view.timeNormalized() > 0.5f);
}

// ------------------------------------------------------------------------------
// T078: Curve selector wires to kToneShaperPitchEnvCurveId string-list.
// The view owns the curve normalised value; setCurveNormalized() stores it, and
// setNormalized(paramId, ...) routes curve edits by the curve tag.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay curve selector is bound to curve param tag",
          "[pitch_envelope][phase6]")
{
    PitchEnvelopeDisplay view(kDefaultRect);

    const auto curveId = static_cast<std::uint32_t>(kToneShaperPitchEnvCurveId);
    REQUIRE(view.paramTags().curve == curveId);

    // A two-entry string-list (Exp=0, Lin=1) is expressed as normalised 0.0 / 1.0.
    view.setNormalized(curveId, 1.0f);
    REQUIRE_THAT(view.curveNormalized(), WithinAbs(1.0f, 1e-6f));
    view.setNormalized(curveId, 0.0f);
    REQUIRE_THAT(view.curveNormalized(), WithinAbs(0.0f, 1e-6f));
}

// ------------------------------------------------------------------------------
// T078: removed() deregisters in-flight drag so Begin/End are balanced.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay::removed balances an in-flight drag",
          "[pitch_envelope][phase6]")
{
    PitchEnvelopeDisplay view(kDefaultRect);

    EventLog log;
    view.setEditCallback(makeRecorder(log));

    const auto startCenter = view.handleCenter(PitchEnvelopeDisplay::DragTarget::Start);
    view.handleMouseDown(startCenter);
    REQUIRE(static_cast<int>(view.activeDrag()) == static_cast<int>(PitchEnvelopeDisplay::DragTarget::Start));

    view.removed(nullptr);
    REQUIRE(static_cast<int>(view.activeDrag()) == static_cast<int>(PitchEnvelopeDisplay::DragTarget::None));

    // An End event must have been dispatched so host-side Begin/End pairs balance.
    bool sawEnd = false;
    for (const auto& e : log)
        if (e.op == PitchEnvelopeDisplay::EditOp::End) sawEnd = true;
    REQUIRE(sawEnd);
}

// ------------------------------------------------------------------------------
// T078: PitchEnvelopeDisplay is present in the SelectedPadAcoustic template of
// editor.uidesc (not only in the Extended template). This is a file-scan check
// that guards against a future UI refactor silently demoting the envelope into
// an Extended-only tab (FR / US8 promotes it to a primary control).
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay appears in SelectedPadAcoustic template",
          "[pitch_envelope][phase6][editor_uidesc]")
{
    const std::string uidescPath =
        "plugins/membrum/resources/editor.uidesc";

    std::ifstream file(uidescPath);
    if (!file.is_open())
    {
        // When the test binary is launched from a different cwd (e.g. the
        // build tree), try a relative-up path as a fallback.
        file.open("../../../" + uidescPath);
    }
    if (!file.is_open())
    {
        file.open("../../../../" + uidescPath);
    }
    REQUIRE(file.is_open());

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string contents = buffer.str();

    // Locate the SelectedPadAcoustic template and ensure it contains a
    // PitchEnvelopeDisplay view *before* the next template block starts.
    const auto acousticPos = contents.find("template name=\"SelectedPadAcoustic\"");
    REQUIRE(acousticPos != std::string::npos);

    const auto nextTemplatePos = contents.find("<template ", acousticPos + 1);
    const std::string acousticBlock =
        contents.substr(acousticPos,
                        (nextTemplatePos == std::string::npos)
                            ? std::string::npos
                            : nextTemplatePos - acousticPos);

    REQUIRE(acousticBlock.find("PitchEnvelopeDisplay") != std::string::npos);
}

// ------------------------------------------------------------------------------
// T079: Punch curve constants are correct in MacroCurves namespace. This test
// directly reads the constants from macro_mapper.h (FR / US8 Punch macro wiring
// verification). The MacroMapper's numerical path is covered by T020 tests;
// this test only verifies the constants themselves.
// ------------------------------------------------------------------------------
TEST_CASE("MacroCurves Punch pitch-envelope span constants match US8 mapping",
          "[pitch_envelope][phase6][macro_curves]")
{
    // Punch @ 1.0 must move tsPitchEnvStart by +kPunchPitchEnvDepthSpan * 0.5
    // and tsPitchEnvTime  by -kPunchPitchEnvTimeSpan  * 0.5 (expressed as
    // macro delta m = (value - 0.5) -> at value=1.0, m=0.5).
    REQUIRE_THAT(MacroCurves::kPunchPitchEnvDepthSpan, WithinAbs(0.50f, 1e-6f));
    REQUIRE_THAT(MacroCurves::kPunchPitchEnvTimeSpan,  WithinAbs(0.40f, 1e-6f));

    // Sanity: spans are strictly positive and <= 1.0 so macro @ 1.0 keeps the
    // output in the normalised [0, 1] VST parameter range when the registered
    // default is near 0.5.
    REQUIRE(MacroCurves::kPunchPitchEnvDepthSpan > 0.0f);
    REQUIRE(MacroCurves::kPunchPitchEnvDepthSpan <= 1.0f);
    REQUIRE(MacroCurves::kPunchPitchEnvTimeSpan  > 0.0f);
    REQUIRE(MacroCurves::kPunchPitchEnvTimeSpan  <= 1.0f);
}
