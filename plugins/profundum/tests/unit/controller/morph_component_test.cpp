// ==============================================================================
// Profundum - morph component controller tests (Phase 12)
// ==============================================================================
// Profundum_MorphComponentUi (SC-007 (a)(b)(c)(e) controller arms; FR-041 - FR-043, FR-046, FR-049; plan 4.6.3) -
// written by T022. The segment-UI arms of (a), (b) and (d), Profundum_MorphLockDimming and
// Profundum_UiPortamentoUnitFollowsGlideMode are T028's (specs/profundum-phase12-ui/tasks.md).
//
// Every arm runs on a ConnectedPair: the controller's messages reach the real processor, whose replies come back
// through the RecordingConnectionPoint toController (kept, not delivered, while toController.hold is set).
//
// T028 (built tree, HeadlessEditor; FR-035, FR-040 - FR-045, FR-049; SC-007 UI arms, SC-008, SC-018):
//   Profundum_MorphComponentUi  SegmentsShowControllerState (a)(b), CopyButtons (c), PositionReadout (d),
//                               EditTargetControl (e) - each drives the real controls of the built tree;
//   Profundum_MorphLockDimming  (SC-008) the 3 x 2 grid of 1000 x 1001;
//   Profundum_UiPortamentoUnitFollowsGlideMode (SC-018) the 412 readout's suffix follows 414.
// A segment click is CSegmentButton::setSelectedSegment, the call its mouse handler makes (csegmentbutton.cpp:327),
// which wraps beginEdit / valueChanged / endEdit (:122-130). A kick button press is the onMouseDown / onMouseUp pair:
// beginEdit, value max, valueChanged, value min, valueChanged, endEdit.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "profundum_test_fixture.h"
#include "ui_tree_helpers.h"

#include "controller/controller.h"
#include "parameters/morph_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"
#include "ui/panel_sub_controller.h"

#include "base/source/updatehandler.h"
#include "pluginterfaces/base/ustring.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/cparamdisplay.h"
#include "vstgui/lib/controls/csegmentbutton.h"
#include "vstgui/lib/controls/ctextlabel.h"

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <typeinfo>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;
using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;
using Krate::DSP::MorphState;
using Steinberg::Vst::kParamValuesChanged;
using Steinberg::Vst::ParamID;

constexpr ParamID kEditTarget = P::kMorphEditTargetId;
constexpr ParamID kPosition = P::kMorphPositionId;
static_assert(kEditTarget == 1001);
static_assert(kPosition == 105);

[[nodiscard]] int countIn(const std::map<ParamID, int>& m, ParamID id) {
    const auto it = m.find(id);
    return it == m.end() ? 0 : it->second;
}

// Prepares the pair and loads endpoints a / b through the processor's setState (which reports both to the
// controller). The recorders and the handler's restart list are cleared afterwards.
void loadEndpoints(ConnectedPair& cp, const MorphState& a, const MorphState& b) {
    REQUIRE(cp.fx.prepare(48000.0, 4096) == Steinberg::kResultOk);
    const StateBytes bytes = withEndpoints(writeV2Stream(*cp.fx.proc), a, b);
    REQUIRE(loadStateBytes(*cp.fx.proc, bytes) == Steinberg::kResultOk);
    cp.clearRecords();
    cp.cf.handler.restartFlags.clear();
}

// The normalised value an E row displays for mirror state s: Sync Rate pairs syncRateIndexOf / 29 (FR-012), every
// other row normalizedFromLeafWord of its leaf (the controller.cpp redisplayEditTarget law).
[[nodiscard]] double mirrorNormalized(const MorphState& s, const P::SoundParamRow& row) {
    const P::LeafBits bits = P::leafBitsOf(s);
    if (row.leaf2 != P::kNoLeaf) {
        const int idx = P::syncRateIndexOf(bits[row.leaf], bits[row.leaf2]);
        REQUIRE(idx >= 0);
        return static_cast<double>(idx) / static_cast<double>(Krate::DSP::kNoteValueDropdownCount - 1);
    }
    return P::normalizedFromLeafWord(row.leaf, bits[row.leaf]);
}

// The number of the 60 E rows whose displayed value differs from the value of mirror state s by more than 1e-9.
[[nodiscard]] int displayMismatches(P::Controller& ctl, const MorphState& s) {
    int bad = 0;
    for (const P::SoundParamRow& row : P::kSoundRows) {
        const double want = mirrorNormalized(s, row);
        const double got = ctl.getParamNormalized(row.id);
        if (std::fabs(got - want) > 1e-9) {
            UNSCOPED_INFO("E row " << row.id << ": displayed " << got << ", mirror " << want);
            ++bad;
        }
    }
    return bad;
}

// The number of the 60 E rows whose displayed values differ between a and b (non-vacuity).
[[nodiscard]] int rowsThatDiffer(const MorphState& a, const MorphState& b) {
    int n = 0;
    for (const P::SoundParamRow& row : P::kSoundRows) {
        if (std::fabs(mirrorNormalized(a, row) - mirrorNormalized(b, row)) > 1e-9) {
            ++n;
        }
    }
    return n;
}

// A processor -> controller kMsgCompareMode with int "mode".
[[nodiscard]] Steinberg::IPtr<Steinberg::Vst::HostMessage> makeCompareModeMessage(Steinberg::int64 mode) {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID(P::kMsgCompareMode);
    Steinberg::Vst::IAttributeList* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);
    REQUIRE(attrs->setInt("mode", mode) == Steinberg::kResultOk);
    return msg;
}

// ---- T028 built-tree helpers ----

// The VST3Editor fills the segments of a bound control through a deferred update (vst3editor.cpp:151-154). Calling
// it before the editor exists also creates the UpdateHandler the parameters' dependents register with.
void flushDeferredUpdates() { Steinberg::UpdateHandler::instance()->triggerDeferedUpdates(); }

// Every control of the built tree whose tag is @p tag (session tags included: the panel set them in verifyView).
template <class T>
[[nodiscard]] std::vector<T*> controlsOf(VSTGUI::CFrame* frame, std::int32_t tag) {
    std::vector<VSTGUI::CControl*> all;
    collectBoundControls(frame, all);
    std::vector<T*> out;
    for (VSTGUI::CControl* c : all) {
        if (c->getTag() == tag) {
            if (auto* typed = dynamic_cast<T*>(c)) {
                out.push_back(typed);
            }
        }
    }
    return out;
}

// The one CSegmentButton of the built tree carrying @p tag.
[[nodiscard]] VSTGUI::CSegmentButton* onlySegment(VSTGUI::CFrame* frame, std::int32_t tag) {
    const auto found = controlsOf<VSTGUI::CSegmentButton>(frame, tag);
    REQUIRE(found.size() == 1u);
    return found.front();
}

// The string CParamDisplay::draw would show (cparamdisplay.cpp:183-184): the INSTALLED value-to-string function
// applied to the control's value. Returns false when no function is installed or it declines (the display would
// then fall back to its own "%.Nf" formatting, i.e. a formatter of its own).
struct ParamDisplayText : VSTGUI::CParamDisplay {
    [[nodiscard]] static bool read(VSTGUI::CParamDisplay& display, std::string& out) {
        const auto member = &ParamDisplayText::valueToStringFunction;  // protected (cparamdisplay.h:131)
        const ValueToStringFunction2& fn = display.*member;
        out.clear();
        return fn ? fn(display.getValue(), out, &display) : false;
    }
};

// The registered formatter's ASCII string for (id, v).
[[nodiscard]] std::string paramString(P::Controller& ctl, ParamID id, double v) {
    Steinberg::Vst::String128 utf16{};
    REQUIRE(ctl.getParamStringByValue(id, v, utf16) == Steinberg::kResultOk);
    std::array<char, 128> ascii{};
    Steinberg::UString(utf16, 128).toAscii(ascii.data(), static_cast<Steinberg::int32>(ascii.size()));
    return {ascii.data()};
}

// The unit of a "<number> <unit>" readout: everything after the first space ("" when there is none).
[[nodiscard]] std::string unitOf(const std::string& text) {
    const std::size_t at = text.find(' ');
    return at == std::string::npos ? std::string{} : text.substr(at + 1u);
}

// A kick button press and release (CTextButton kick style), as its mouse handlers do.
void pressKickButton(VSTGUI::CControl& button) {
    button.beginEdit();
    button.setValueNormalized(1.0f);
    button.valueChanged();
    button.setValueNormalized(0.0f);
    button.valueChanged();
    button.endEdit();
}

void clearEdits(RecordingComponentHandler& h) {
    h.beginEdits.clear();
    h.performEdits.clear();
    h.endEdits.clear();
    h.restartFlags.clear();
}

}  // namespace

TEST_CASE("Profundum_MorphComponentUi", "[profundum][ui][morph]") {
    const MorphState a = randomValidMorphState(0x22Au);
    const MorphState b = randomValidMorphState(0x22Bu);

    ConnectedPair cp;
    loadEndpoints(cp, a, b);
    P::Controller& ctl = *cp.cf.controller;
    RecordingComponentHandler& h = cp.cf.handler;
    REQUIRE(ctl.compareMode() == P::CompareMode::AB);
    REQUIRE(ctl.getParamNormalized(kEditTarget) == 0.0);  // Edit Target A

    // SC-007 (a): FR-041, FR-049.
    SECTION("CompareMessagesAndEditTarget") {
        // Compare A: one kMsgCompare mode 1 and one begin / perform / end on 1001 carrying A (0.0).
        ctl.pressCompare(P::CompareMode::A);
        CHECK(cp.toProcessor.count(P::kMsgCompare) == 1u);
        CHECK(cp.toProcessor.count(P::kMsgCompare, "mode", 1) == 1u);
        CHECK(countIn(h.beginEdits, kEditTarget) == 1);
        CHECK(countIn(h.performEdits, kEditTarget) == 1);
        CHECK(countIn(h.endEdits, kEditTarget) == 1);
        CHECK(ctl.getParamNormalized(kEditTarget) == 0.0);
        CHECK(ctl.compareMode() == P::CompareMode::A);  // the processor's synchronous reply

        // Compare B: one kMsgCompare mode 2 and one more begin / perform / end on 1001, carrying B (1.0).
        cp.clearRecords();
        ctl.pressCompare(P::CompareMode::B);
        CHECK(cp.toProcessor.count(P::kMsgCompare) == 1u);
        CHECK(cp.toProcessor.count(P::kMsgCompare, "mode", 2) == 1u);
        CHECK(countIn(h.beginEdits, kEditTarget) == 2);
        CHECK(countIn(h.performEdits, kEditTarget) == 2);
        CHECK(countIn(h.endEdits, kEditTarget) == 2);
        CHECK(ctl.getParamNormalized(kEditTarget) == 1.0);
        CHECK(ctl.compareMode() == P::CompareMode::B);

        // Compare A.B: one kMsgCompare mode 0, zero performEdit, 1001 unchanged.
        cp.clearRecords();
        const double editTargetBefore = ctl.getParamNormalized(kEditTarget);
        ctl.pressCompare(P::CompareMode::AB);
        CHECK(cp.toProcessor.count(P::kMsgCompare) == 1u);
        CHECK(cp.toProcessor.count(P::kMsgCompare, "mode", 0) == 1u);
        CHECK(countIn(h.beginEdits, kEditTarget) == 2);
        CHECK(countIn(h.performEdits, kEditTarget) == 2);
        CHECK(countIn(h.endEdits, kEditTarget) == 2);
        CHECK(ctl.getParamNormalized(kEditTarget) == editTargetBefore);
        CHECK(ctl.compareMode() == P::CompareMode::AB);

        // Morph Position 105: no kMsgCompare; the compare mode and the change count are unchanged.
        cp.clearRecords();
        const P::CompareMode modeBefore = ctl.compareMode();
        const std::uint32_t changesBefore = ctl.compareModeChangeCount();
        REQUIRE(ctl.beginEdit(kPosition) == Steinberg::kResultOk);
        REQUIRE(ctl.performEdit(kPosition, 0.42) == Steinberg::kResultOk);
        REQUIRE(ctl.setParamNormalized(kPosition, 0.42) == Steinberg::kResultTrue);
        REQUIRE(ctl.endEdit(kPosition) == Steinberg::kResultOk);
        CHECK(countIn(h.performEdits, kPosition) == 1);  // non-vacuity: the gesture reached the host
        CHECK(cp.toProcessor.count(P::kMsgCompare) == 0u);
        CHECK(ctl.compareMode() == modeBefore);
        CHECK(ctl.compareModeChangeCount() == changesBefore);
    }

    // SC-007 (b): FR-046. The getter changes only when kMsgCompareMode arrives; one notification per accepted message.
    SECTION("ReplyDrivesState") {
        const P::CompareMode modeBefore = ctl.compareMode();
        const std::uint32_t changesBefore = ctl.compareModeChangeCount();
        cp.toController.hold = true;
        ctl.pressCompare(P::CompareMode::B);
        CHECK(ctl.compareMode() == modeBefore);
        CHECK(ctl.compareModeChangeCount() == changesBefore);

        // The press sends one kMsgCompare; the Edit Target switch it drives (FR-049) also sends the FR-025
        // kMsgRequestSync pull, whose reply ends with a kMsgCompareMode as well. Each held kMsgCompareMode is one
        // accepted message, so the count rises by exactly the number delivered.
        const std::size_t heldReplies = cp.toController.count(P::kMsgCompareMode);
        REQUIRE(heldReplies >= 1u);
        cp.toController.releaseHeld();
        CHECK(ctl.compareMode() == P::CompareMode::B);
        CHECK(ctl.compareModeChangeCount() == changesBefore + static_cast<std::uint32_t>(heldReplies));

        // One accepted message: the count rises by exactly 1. A rejected one changes nothing.
        const std::uint32_t c0 = ctl.compareModeChangeCount();
        REQUIRE(ctl.notify(makeCompareModeMessage(static_cast<Steinberg::int64>(P::CompareMode::A))) ==
                Steinberg::kResultOk);
        CHECK(ctl.compareMode() == P::CompareMode::A);
        CHECK(ctl.compareModeChangeCount() == c0 + 1u);
        CHECK(ctl.notify(makeCompareModeMessage(7)) == Steinberg::kResultFalse);
        CHECK(ctl.compareMode() == P::CompareMode::A);
        CHECK(ctl.compareModeChangeCount() == c0 + 1u);
    }

    // SC-007 (c): FR-030.
    SECTION("CopySendsOneMessage") {
        const int dirtyBefore = h.dirtyCount;
        ctl.copyEndpoint(Endpoint::A);
        CHECK(cp.toProcessor.count(P::kMsgCopyEndpoint) == 1u);
        CHECK(cp.toProcessor.count(P::kMsgCopyEndpoint, "from", 0) == 1u);
        CHECK(h.dirtyCount == dirtyBefore + 1);

        cp.clearRecords();
        ctl.copyEndpoint(Endpoint::B);
        CHECK(cp.toProcessor.count(P::kMsgCopyEndpoint) == 1u);
        CHECK(cp.toProcessor.count(P::kMsgCopyEndpoint, "from", 1) == 1u);
        CHECK(h.dirtyCount == dirtyBefore + 2);
    }

    // SC-007 (e): FR-043, FR-022, FR-024.
    SECTION("EditTargetRedisplays") {
        REQUIRE(rowsThatDiffer(a, b) > 0);  // non-vacuity: B displays differently from A
        REQUIRE(displayMismatches(ctl, ctl.mirrorForTest(Endpoint::A)) == 0);
        REQUIRE(ctl.beginEdit(kEditTarget) == Steinberg::kResultOk);
        REQUIRE(ctl.performEdit(kEditTarget, 1.0) == Steinberg::kResultOk);
        REQUIRE(ctl.setParamNormalized(kEditTarget, 1.0) == Steinberg::kResultTrue);
        REQUIRE(ctl.endEdit(kEditTarget) == Steinberg::kResultOk);
        CHECK(countIn(h.beginEdits, kEditTarget) == 1);
        CHECK(countIn(h.performEdits, kEditTarget) == 1);
        CHECK(countIn(h.endEdits, kEditTarget) == 1);
        CHECK(displayMismatches(ctl, ctl.mirrorForTest(Endpoint::B)) == 0);
        CHECK(displayMismatches(ctl, b) == 0);
        std::size_t restarts = 0;
        for (const Steinberg::int32 f : h.restartFlags) {
            if ((f & kParamValuesChanged) != 0) {
                ++restarts;
            }
        }
        CHECK(restarts == 1u);
    }

    // ---- T028: the same arms through the controls of the built tree ----

    // SC-007 (a)(b) UI arm (FR-041, FR-046, FR-049): the compare segments of Main (9200) and Advanced (9203).
    SECTION("SegmentsShowControllerState") {
        flushDeferredUpdates();
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        flushDeferredUpdates();
        REQUIRE(ctl.panelForTest() != nullptr);
        VSTGUI::CSegmentButton* mainSeg = onlySegment(ed.frame(), P::UI::kCompareMainTag);
        VSTGUI::CSegmentButton* advSeg = onlySegment(ed.frame(), P::UI::kCompareAdvancedTag);
        const std::array<VSTGUI::CSegmentButton*, 2> segments{mainSeg, advSeg};
        const auto bothShow = [&](P::CompareMode m) {
            const auto want = static_cast<std::uint32_t>(P::UI::segmentOfCompareMode(m));
            return mainSeg->getSelectedSegment() == want && advSeg->getSelectedSegment() == want;
        };
        for (VSTGUI::CSegmentButton* seg : segments) {
            REQUIRE(seg->getSegments().size() == 3u);
        }
        CHECK(bothShow(P::CompareMode::AB));  // the editor opens on the reported mode

        // (a) Every segment of each control: one kMsgCompare with its mode; A / B also one begin / perform / end on
        // 1001 (FR-049), A.B none. The processor replies synchronously here, so both controls then show the reply.
        for (VSTGUI::CSegmentButton* seg : segments) {
            for (const P::CompareMode m : {P::CompareMode::A, P::CompareMode::B, P::CompareMode::AB}) {
                CAPTURE(seg->getTag(), static_cast<int>(m));
                cp.clearRecords();
                clearEdits(h);
                const double editTargetBefore = ctl.getParamNormalized(kEditTarget);
                const std::uint32_t changesBefore = ctl.compareModeChangeCount();

                seg->setSelectedSegment(static_cast<std::uint32_t>(P::UI::segmentOfCompareMode(m)));

                CHECK(cp.toProcessor.count(P::kMsgCompare) == 1u);
                CHECK(cp.toProcessor.count(P::kMsgCompare, "mode", static_cast<Steinberg::int64>(m)) == 1u);
                const int gestures = (m == P::CompareMode::AB) ? 0 : 1;
                CHECK(countIn(h.beginEdits, kEditTarget) == gestures);
                CHECK(countIn(h.performEdits, kEditTarget) == gestures);
                CHECK(countIn(h.endEdits, kEditTarget) == gestures);
                double wantTarget = editTargetBefore;  // A.B leaves 1001 unchanged
                if (m == P::CompareMode::A) {
                    wantTarget = 0.0;
                } else if (m == P::CompareMode::B) {
                    wantTarget = 1.0;
                }
                CHECK(ctl.getParamNormalized(kEditTarget) == wantTarget);
                const std::size_t replies = cp.toController.count(P::kMsgCompareMode);
                CHECK(replies >= 1u);
                CHECK(ctl.compareModeChangeCount() == changesBefore + static_cast<std::uint32_t>(replies));
                CHECK(ctl.compareMode() == m);
                CHECK(bothShow(m));
            }
        }

        // (b) The reply held: a click shows nothing of its own, on the clicked control or the other one.
        REQUIRE(ctl.compareMode() == P::CompareMode::AB);
        cp.clearRecords();
        const std::uint32_t changesBefore = ctl.compareModeChangeCount();
        cp.toController.hold = true;
        mainSeg->setSelectedSegment(static_cast<std::uint32_t>(P::UI::segmentOfCompareMode(P::CompareMode::B)));
        CHECK(ctl.compareMode() == P::CompareMode::AB);
        CHECK(ctl.compareModeChangeCount() == changesBefore);
        CHECK(bothShow(P::CompareMode::AB));
        advSeg->setSelectedSegment(static_cast<std::uint32_t>(P::UI::segmentOfCompareMode(P::CompareMode::A)));
        CHECK(ctl.compareMode() == P::CompareMode::AB);
        CHECK(ctl.compareModeChangeCount() == changesBefore);
        CHECK(bothShow(P::CompareMode::AB));
        CHECK(cp.toProcessor.count(P::kMsgCompare) == 2u);

        // Each press's kMsgCompare and its Edit Target pull (kMsgRequestSync) end with a kMsgCompareMode reply.
        const std::size_t held = cp.toController.count(P::kMsgCompareMode);
        REQUIRE(held >= 2u);
        cp.toController.releaseHeld();
        CHECK(ctl.compareMode() == P::CompareMode::A);  // delivered in order: the last press's reply is the last
        CHECK(ctl.compareModeChangeCount() == changesBefore + static_cast<std::uint32_t>(held));
        CHECK(bothShow(ctl.compareMode()));
    }

    // SC-007 (c) UI arm (FR-030, FR-042): the four Copy buttons.
    SECTION("CopyButtons") {
        flushDeferredUpdates();
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        flushDeferredUpdates();
        struct CopyButton {
            std::int32_t tag;
            Steinberg::int64 from;
        };
        constexpr std::array<CopyButton, 4> kButtons{{{.tag = P::UI::kCopyAToBMainTag, .from = 0},
                                                      {.tag = P::UI::kCopyBToAMainTag, .from = 1},
                                                      {.tag = P::UI::kCopyAToBAdvancedTag, .from = 0},
                                                      {.tag = P::UI::kCopyBToAAdvancedTag, .from = 1}}};
        for (const CopyButton& button : kButtons) {
            CAPTURE(button.tag);
            const auto found = controlsOf<VSTGUI::CControl>(ed.frame(), button.tag);
            REQUIRE(found.size() == 1u);
            cp.clearRecords();
            clearEdits(h);
            const int dirtyBefore = h.dirtyCount;

            pressKickButton(*found.front());

            CHECK(cp.toProcessor.count(P::kMsgCopyEndpoint) == 1u);
            CHECK(cp.toProcessor.count(P::kMsgCopyEndpoint, "from", button.from) == 1u);
            CHECK(cp.toProcessor.records().size() == 1u);  // the copy only: no compare, no name
            CHECK(h.dirtyCount == dirtyBefore + 1);
            CHECK(h.beginEdits.empty());  // a session control is never a host gesture
            CHECK(h.performEdits.empty());
            CHECK(h.endEdits.empty());
        }
    }

    // SC-007 (d) (FR-045): the 105 readout prints the registered formatter's string; it has no formatter of its own.
    SECTION("PositionReadout") {
        flushDeferredUpdates();
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        flushDeferredUpdates();
        std::vector<VSTGUI::CParamDisplay*> readouts;
        for (VSTGUI::CParamDisplay* d :
             controlsOf<VSTGUI::CParamDisplay>(ed.frame(), static_cast<std::int32_t>(kPosition))) {
            if (typeid(*d) == typeid(VSTGUI::CParamDisplay)) {  // not a CTextLabel / CTextEdit
                readouts.push_back(d);
            }
        }
        REQUIRE_FALSE(readouts.empty());

        constexpr std::array<double, 5> kPositions{0.0, 0.005, 0.42, 0.995, 1.0};
        constexpr std::array<const char*, 5> kTexts{"0%", "0%", "42%", "100%", "100%"};
        for (std::size_t i = 0; i < kPositions.size(); ++i) {
            CAPTURE(kPositions[i]);
            REQUIRE(ctl.setParamNormalized(kPosition, kPositions[i]) == Steinberg::kResultTrue);
            flushDeferredUpdates();
            const std::string registered = paramString(ctl, kPosition, kPositions[i]);
            CHECK(registered == kTexts[i]);
            for (VSTGUI::CParamDisplay* d : readouts) {
                std::string shown;
                REQUIRE(ParamDisplayText::read(*d, shown));  // the installed (VST3Editor) function, not "%.Nf"
                CHECK(shown == registered);
                CHECK(shown == kTexts[i]);
            }
        }
    }

    // SC-007 (e) UI arm (FR-043, FR-022, FR-024): selecting B in the built 1001 control.
    SECTION("EditTargetControl") {
        flushDeferredUpdates();
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        flushDeferredUpdates();
        VSTGUI::CSegmentButton* target = onlySegment(ed.frame(), static_cast<std::int32_t>(kEditTarget));
        REQUIRE(target->getSegments().size() == 2u);
        CHECK(target->getSelectedSegment() == 0u);
        REQUIRE(rowsThatDiffer(a, b) > 0);  // non-vacuity: B displays differently from A
        REQUIRE(displayMismatches(ctl, ctl.mirrorForTest(Endpoint::A)) == 0);
        cp.clearRecords();
        clearEdits(h);

        target->setSelectedSegment(1u);

        CHECK(countIn(h.beginEdits, kEditTarget) == 1);
        CHECK(countIn(h.performEdits, kEditTarget) == 1);
        CHECK(countIn(h.endEdits, kEditTarget) == 1);
        CHECK(ctl.getParamNormalized(kEditTarget) == 1.0);
        CHECK(cp.toProcessor.count(P::kMsgCompare) == 0u);  // an Edit Target switch is not a compare press
        CHECK(displayMismatches(ctl, ctl.mirrorForTest(Endpoint::B)) == 0);
        CHECK(displayMismatches(ctl, b) == 0);
        std::size_t restarts = 0;
        for (const Steinberg::int32 f : h.restartFlags) {
            if ((f & kParamValuesChanged) != 0) {
                ++restarts;
            }
        }
        CHECK(restarts == 1u);
    }
}

// ==============================================================================
// SC-008 - Lock dimming (FR-044): every 1000 x 1001 combination through setParamNormalized
// ==============================================================================

TEST_CASE("Profundum_MorphLockDimming", "[profundum][ui][morph]") {
    ControllerFixture cf;
    P::Controller& ctl = *cf.controller;
    flushDeferredUpdates();
    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);
    flushDeferredUpdates();
    P::UI::ProfundumPanelSubController* panel = ctl.panelForTest();
    REQUIRE(panel != nullptr);

    // Every built control bound to 300 - 306; at least one per ID (isAnchorDimmedForTest is false for an ID it lacks).
    static_assert(P::kAnchorFoldFeedId - P::kAnchorWaveformId == 6);
    std::vector<VSTGUI::CControl*> anchors;
    for (ParamID id = P::kAnchorWaveformId; id <= P::kAnchorFoldFeedId; ++id) {
        CAPTURE(id);
        const auto found = controlsOf<VSTGUI::CControl>(ed.frame(), static_cast<std::int32_t>(id));
        REQUIRE_FALSE(found.empty());
        anchors.insert(anchors.end(), found.begin(), found.end());
    }

    VSTGUI::CTextLabel* held = nullptr;
    int heldLabels = 0;
    for (VSTGUI::CTextLabel* label : collectViewsOfType<VSTGUI::CTextLabel>(ed.frame())) {
        if (label->getText().getString() == "Held at A") {
            held = label;
            ++heldLabels;
        }
    }
    REQUIRE(heldLabels == 1);
    REQUIRE(held != nullptr);

    const auto setGrid = [&](int mode, int target) {
        REQUIRE(ctl.setParamNormalized(P::kMorphAnchorModeId, static_cast<double>(mode) /
                                                                  static_cast<double>(P::kNumAnchorMorphModes - 1)) ==
                Steinberg::kResultTrue);
        REQUIRE(ctl.setParamNormalized(P::kMorphEditTargetId,
                                       static_cast<double>(target) / static_cast<double>(P::kNumEditTargets - 1)) ==
                Steinberg::kResultTrue);
        flushDeferredUpdates();
    };
    const auto checkGrid = [&](int mode, int target) {
        CAPTURE(mode, target);
        const bool dim = mode == 0 && target == 1;  // Hold (list "Hold, Blend, Yield", morph_params.h:168) and B
        for (ParamID id = P::kAnchorWaveformId; id <= P::kAnchorFoldFeedId; ++id) {
            CAPTURE(id);
            CHECK(panel->isAnchorDimmedForTest(id) == dim);
        }
        for (const VSTGUI::CControl* c : anchors) {
            CAPTURE(c->getTag());
            CHECK((c->getAlphaValue() < 1.0f) == dim);
            CHECK(c->getMouseEnabled());  // dimmed controls still accept edits
        }
        CHECK(held->isVisible() == dim);
    };

    // Mode outer, then target outer, so the dimming turns on through each half of the condition (1001, then 1000).
    for (int mode = 0; mode < P::kNumAnchorMorphModes; ++mode) {
        for (int target = 0; target < P::kNumEditTargets; ++target) {
            setGrid(mode, target);
            checkGrid(mode, target);
        }
    }
    for (int target = 0; target < P::kNumEditTargets; ++target) {
        for (int mode = P::kNumAnchorMorphModes - 1; mode >= 0; --mode) {
            setGrid(mode, target);
            checkGrid(mode, target);
        }
    }
}

// ==============================================================================
// SC-018 - the 412 readout's unit follows Glide Mode 414 (FR-035)
// ==============================================================================

TEST_CASE("Profundum_UiPortamentoUnitFollowsGlideMode", "[profundum][ui]") {
    ControllerFixture cf;
    P::Controller& ctl = *cf.controller;
    flushDeferredUpdates();
    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);
    flushDeferredUpdates();
    P::UI::ProfundumPanelSubController* panel = ctl.panelForTest();
    REQUIRE(panel != nullptr);

    // The built tree holds exactly one 412-bound CParamDisplay (CTextLabel / CTextEdit are counted too: they derive
    // from it). The panel collects its readout only from this tree's verifyView (panel_sub_controller.cpp
    // collectBoundControl), and it counts an invalidation only while it holds one, so the display whose invalidations
    // rise below is this pointer.
    const auto displays =
        controlsOf<VSTGUI::CParamDisplay>(ed.frame(), static_cast<std::int32_t>(P::kNotePortamentoId));
    REQUIRE(displays.size() == 1u);
    VSTGUI::CParamDisplay* readout = displays.front();

    REQUIRE(ctl.setParamNormalized(P::kNotePortamentoId, 0.3) == Steinberg::kResultTrue);
    flushDeferredUpdates();
    const double portamento = ctl.getParamNormalized(P::kNotePortamentoId);

    const auto glideNormalized = [](int index) { return static_cast<double>(index) / 2.0; };  // three list values
    REQUIRE(ctl.setParamNormalized(P::kNoteGlideModeId, glideNormalized(0)) == Steinberg::kResultTrue);
    flushDeferredUpdates();
    std::string text;
    REQUIRE(ParamDisplayText::read(*readout, text));
    std::string unit = unitOf(text);
    REQUIRE_FALSE(unit.empty());

    // 0 -> 1 -> 2 -> 1 -> 0: every step enters or leaves Constant Rate, whose suffix is per octave (note_params.h).
    for (const int index : {1, 2, 1, 0}) {
        CAPTURE(index);
        const std::uint32_t before = panel->portaReadoutInvalidationsForTest();
        REQUIRE(ctl.setParamNormalized(P::kNoteGlideModeId, glideNormalized(index)) == Steinberg::kResultTrue);
        flushDeferredUpdates();
        CHECK(panel->portaReadoutInvalidationsForTest() == before + 1u);

        std::string now;
        REQUIRE(ParamDisplayText::read(*readout, now));
        CHECK(now == paramString(ctl, P::kNotePortamentoId, static_cast<double>(readout->getValue())));
        const std::string nowUnit = unitOf(now);
        CHECK(nowUnit != unit);
        CHECK((nowUnit.find("/oct") != std::string::npos) == (index == 1));
        unit = nowUnit;
        CHECK(ctl.getParamNormalized(P::kNotePortamentoId) == portamento);  // 412 itself untouched
    }

    // An unrelated ID (200) does not re-format the readout.
    const std::uint32_t before = panel->portaReadoutInvalidationsForTest();
    const double depth = ctl.getParamNormalized(P::kCoreDepthId);
    REQUIRE(ctl.setParamNormalized(P::kCoreDepthId, depth < 0.5 ? 0.9 : 0.1) == Steinberg::kResultTrue);
    flushDeferredUpdates();
    CHECK(panel->portaReadoutInvalidationsForTest() == before);
}
