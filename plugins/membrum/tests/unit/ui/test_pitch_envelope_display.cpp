// ==============================================================================
// PitchEnvelopeDisplay -- Unit tests (Phase 6, Spec 141, T078, T079)
// ==============================================================================
// Exercises the shared Krate::Plugins::PitchEnvelopeDisplay control that now
// lives in plugins/shared/src/ui/pitch_envelope_display.h. The view is
// patterned on ADSRDisplay: configurable parameter IDs, ParameterCallback +
// BeginEditCallback + EndEditCallback.

#include "ui/pitch_envelope_display.h"
#include "processor/macro_mapper.h"
#include "plugin_ids.h"

#include "vstgui/lib/crect.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/controls/ccontrol.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace Membrum;
using Krate::Plugins::PitchEnvelopeDisplay;

namespace {

constexpr VSTGUI::CRect kDefaultRect{ 0.0, 0.0, 360.0, 80.0 };

struct PerformEvent
{
    std::uint32_t paramId;
    float         value;
};

struct EventLog
{
    std::vector<std::uint32_t> beginCalls;
    std::vector<PerformEvent>  performCalls;
    std::vector<std::uint32_t> endCalls;
};

/// Wire all three callbacks on the view to record events into the given log.
void wireRecorder(PitchEnvelopeDisplay& view, EventLog& log)
{
    view.setBeginEditCallback(
        [&log](std::uint32_t paramId) {
            log.beginCalls.push_back(paramId);
        });
    view.setParameterCallback(
        [&log](std::uint32_t paramId, float value) {
            log.performCalls.push_back({ paramId, value });
        });
    view.setEndEditCallback(
        [&log](std::uint32_t paramId) {
            log.endCalls.push_back(paramId);
        });
}

[[nodiscard]] bool hasBeginPerformEnd(const EventLog& log, std::uint32_t paramId)
{
    bool begin = false, perform = false, end = false;
    for (auto id : log.beginCalls) if (id == paramId) begin = true;
    for (const auto& e : log.performCalls) if (e.paramId == paramId) perform = true;
    for (auto id : log.endCalls) if (id == paramId) end = true;
    return begin && perform && end;
}

/// Build a view wired to the Membrum tone-shaper pitch-envelope parameter IDs
/// (matching what the uidesc / verifyView wiring sets up at plugin runtime).
[[nodiscard]] PitchEnvelopeDisplay makeViewWithMembrumTags()
{
    PitchEnvelopeDisplay view(kDefaultRect, nullptr, -1);
    view.setStartParamId(static_cast<std::uint32_t>(kToneShaperPitchEnvStartId));
    view.setEndParamId  (static_cast<std::uint32_t>(kToneShaperPitchEnvEndId));
    view.setTimeParamId (static_cast<std::uint32_t>(kToneShaperPitchEnvTimeId));
    view.setCurveParamId(static_cast<std::uint32_t>(kToneShaperPitchEnvCurveId));
    return view;
}

} // anonymous namespace

// ------------------------------------------------------------------------------
// T078: Param-id setters wire to expected Membrum tone-shaper tags.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay parameter IDs are configurable",
          "[pitch_envelope][phase6]")
{
    auto view = makeViewWithMembrumTags();

    REQUIRE(view.getStartParamId() == static_cast<std::uint32_t>(kToneShaperPitchEnvStartId));
    REQUIRE(view.getEndParamId()   == static_cast<std::uint32_t>(kToneShaperPitchEnvEndId));
    REQUIRE(view.getTimeParamId()  == static_cast<std::uint32_t>(kToneShaperPitchEnvTimeId));
    REQUIRE(view.getCurveParamId() == static_cast<std::uint32_t>(kToneShaperPitchEnvCurveId));
}

TEST_CASE("PitchEnvelopeDisplay param-id setters accept custom values",
          "[pitch_envelope][phase6]")
{
    PitchEnvelopeDisplay view(kDefaultRect, nullptr, -1);
    view.setStartParamId(7001u);
    view.setEndParamId  (7002u);
    view.setTimeParamId (7003u);
    view.setCurveParamId(7004u);

    REQUIRE(view.getStartParamId() == 7001u);
    REQUIRE(view.getEndParamId()   == 7002u);
    REQUIRE(view.getTimeParamId()  == 7003u);
    REQUIRE(view.getCurveParamId() == 7004u);
}

// ------------------------------------------------------------------------------
// T078: Dragging Start vertically fires Begin/Perform/End on Start param.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay dragging Start handle fires begin/perform/end on Start param",
          "[pitch_envelope][phase6]")
{
    auto view = makeViewWithMembrumTags();
    view.setStartNormalized(0.5f);
    view.setEndNormalized(0.25f);
    view.setTimeNormalized(0.0f);

    EventLog log;
    wireRecorder(view, log);

    // Simulate a left-button drag on the Start handle.
    const auto startCenter = view.getHandleCenter(PitchEnvelopeDisplay::DragTarget::Start);
    VSTGUI::CPoint where = startCenter;
    VSTGUI::CButtonState buttons(VSTGUI::kLButton);

    REQUIRE(view.onMouseDown(where, buttons) == VSTGUI::kMouseEventHandled);

    // Drag vertically up (y decreases -> higher normalised pitch).
    where = VSTGUI::CPoint{ startCenter.x, startCenter.y - 20.0 };
    REQUIRE(view.onMouseMoved(where, buttons) == VSTGUI::kMouseEventHandled);

    REQUIRE(view.onMouseUp(where, buttons) == VSTGUI::kMouseEventHandled);

    const auto startId = static_cast<std::uint32_t>(kToneShaperPitchEnvStartId);
    REQUIRE(hasBeginPerformEnd(log, startId));

    // No Perform events for End / Time / Curve during a Start drag.
    for (const auto& e : log.performCalls)
    {
        REQUIRE(e.paramId == startId);
    }
}

// ------------------------------------------------------------------------------
// T078: Dragging End vertically fires on End param.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay dragging End handle fires begin/perform/end on End param",
          "[pitch_envelope][phase6]")
{
    auto view = makeViewWithMembrumTags();
    view.setStartNormalized(0.75f);
    view.setEndNormalized(0.25f);

    EventLog log;
    wireRecorder(view, log);

    const auto endCenter = view.getHandleCenter(PitchEnvelopeDisplay::DragTarget::End);
    VSTGUI::CPoint where = endCenter;
    VSTGUI::CButtonState buttons(VSTGUI::kLButton);

    REQUIRE(view.onMouseDown(where, buttons) == VSTGUI::kMouseEventHandled);

    where = VSTGUI::CPoint{ endCenter.x, endCenter.y - 15.0 };
    REQUIRE(view.onMouseMoved(where, buttons) == VSTGUI::kMouseEventHandled);
    REQUIRE(view.onMouseUp(where, buttons) == VSTGUI::kMouseEventHandled);

    const auto endId = static_cast<std::uint32_t>(kToneShaperPitchEnvEndId);
    REQUIRE(hasBeginPerformEnd(log, endId));
}

// ------------------------------------------------------------------------------
// T078: Dragging Time horizontally fires on Time param.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay dragging Time handle fires begin/perform/end on Time param",
          "[pitch_envelope][phase6]")
{
    auto view = makeViewWithMembrumTags();
    view.setStartNormalized(0.5f);
    view.setEndNormalized(0.25f);
    view.setTimeNormalized(0.5f);

    EventLog log;
    wireRecorder(view, log);

    const auto timeCenter = view.getHandleCenter(PitchEnvelopeDisplay::DragTarget::Time);
    VSTGUI::CPoint where = timeCenter;
    VSTGUI::CButtonState buttons(VSTGUI::kLButton);

    REQUIRE(view.onMouseDown(where, buttons) == VSTGUI::kMouseEventHandled);

    where = VSTGUI::CPoint{ timeCenter.x + 40.0, timeCenter.y };
    REQUIRE(view.onMouseMoved(where, buttons) == VSTGUI::kMouseEventHandled);
    REQUIRE(view.onMouseUp(where, buttons) == VSTGUI::kMouseEventHandled);

    const auto timeId = static_cast<std::uint32_t>(kToneShaperPitchEnvTimeId);
    REQUIRE(hasBeginPerformEnd(log, timeId));

    // Time handle should respond to horizontal drag: normalised value increased.
    REQUIRE(view.getTimeNormalized() > 0.5f);
}

// ------------------------------------------------------------------------------
// T078: Curve param is bound to kToneShaperPitchEnvCurveId and setCurveNormalized
// clamps and stores.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay curve param is bound to curve tag and setter round-trips",
          "[pitch_envelope][phase6]")
{
    auto view = makeViewWithMembrumTags();

    REQUIRE(view.getCurveParamId()
            == static_cast<std::uint32_t>(kToneShaperPitchEnvCurveId));

    // setCurveNormalized stores the value verbatim when no drag is active.
    view.setCurveNormalized(1.0f);
    REQUIRE_THAT(view.getCurveNormalized(), WithinAbs(1.0f, 1e-6f));
    view.setCurveNormalized(0.0f);
    REQUIRE_THAT(view.getCurveNormalized(), WithinAbs(0.0f, 1e-6f));

    // Clamping: out-of-range values are clamped to [0, 1].
    view.setCurveNormalized(2.0f);
    REQUIRE_THAT(view.getCurveNormalized(), WithinAbs(1.0f, 1e-6f));
    view.setCurveNormalized(-1.0f);
    REQUIRE_THAT(view.getCurveNormalized(), WithinAbs(0.0f, 1e-6f));
}

// ------------------------------------------------------------------------------
// T078: removed() must terminate any in-flight drag so Begin/End pairs stay
// balanced when the editor closes mid-drag.
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay::removed balances an in-flight drag",
          "[pitch_envelope][phase6]")
{
    auto view = makeViewWithMembrumTags();

    EventLog log;
    wireRecorder(view, log);

    const auto startCenter = view.getHandleCenter(PitchEnvelopeDisplay::DragTarget::Start);
    VSTGUI::CPoint where = startCenter;
    VSTGUI::CButtonState buttons(VSTGUI::kLButton);
    REQUIRE(view.onMouseDown(where, buttons) == VSTGUI::kMouseEventHandled);
    REQUIRE_FALSE(log.beginCalls.empty());

    view.removed(nullptr);

    // An End event must have been dispatched so host-side Begin/End pairs balance.
    REQUIRE_FALSE(log.endCalls.empty());
    REQUIRE(log.endCalls.back()
            == static_cast<std::uint32_t>(kToneShaperPitchEnvStartId));
}

// ------------------------------------------------------------------------------
// T078: PitchEnvelopeDisplay is present in the SelectedPadSimple template of
// editor.uidesc (not only in the Extended template). This is a file-scan check
// that guards against a future UI refactor silently demoting the envelope into
// an Extended-only tab (FR / US8 promotes it to a primary control).
// ------------------------------------------------------------------------------
TEST_CASE("PitchEnvelopeDisplay appears in SelectedPadSimple template",
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

    // Locate the SelectedPadSimple template and ensure it contains a
    // PitchEnvelopeDisplay view *before* the next template block starts.
    const auto acousticPos = contents.find("template name=\"SelectedPadSimple\"");
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
