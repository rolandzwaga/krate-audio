// ==============================================================================
// Profundum - custom view law tests (Phase 12)
// ==============================================================================
// Profundum_UiCustomViewLaws — written by T025, T030 (specs/profundum-phase12-ui/tasks.md)
//
// SECTION "AdapterOnTestDisplay" (T025; FR-034, SC-024 adapter arm; plan 4.6.10, D-8): a heap ADSRDisplay (the
// shared header, unmodified) wired by Controller::wireAdsrDisplay, driven through its public mouse path
// (onMouseDown at a control point / curve segment, one onMouseMoved, onMouseUp). Every recorded performEdit value
// equals normalizedFromPlain(law, the display's own plain value after the drag) within 1e-6, the parameter's string
// round-trips to that plain value at its display precision, and no ID outside 420 - 426 is ever emitted. Reverse
// path: setParamNormalized(420 / 421 / 423, v) leaves the display at plainFromNormalized(law, v). Negative arm: a
// display whose callback goes straight to performEdit emits a different 420 value for the same 100 ms drag.
//
// SECTION "BuiltTreeDisplay" (T030; SC-024 built-tree arm): the one ADSRDisplay of the open editor (the instance the
// panel's verifyView wired, panel_sub_controller.cpp:202-203), driven through the same mouse path and assertions
// (420, 421, 423 at min / ~100 ms / max; 422, 424 - 426) and the reverse path.
// SECTION "BarViewWritesNothing" (T030): mouse events on both built HarmonicBarViews record 0 performEdit.
//
// The edit values are recorded by AdsrEditRecorder below: the fixture's RecordingComponentHandler
// (profundum_test_fixture.h:400) counts performEdit calls per ID but drops the value.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "profundum_test_fixture.h"
#include "ui_tree_helpers.h"  // HeadlessEditor, collectViewsOfType (T017)

#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"
#include "ui/adsr_display.h"  // plugins/shared/src/ui
#include "ui/harmonic_bar_view.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "vstgui/lib/cbuttonstate.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/events.h"

#include <krate/dsp/primitives/envelope_utils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace P = ::Profundum;
using Krate::Plugins::ADSRDisplay;
using DragTarget = Krate::Plugins::ADSRDisplay::DragTarget;
using Steinberg::Vst::ParamID;
using ProfundumPluginTest::ControllerFixture;

// An IComponentHandler that records every edit with its value, in call order.
class AdsrEditRecorder final : public Steinberg::Vst::IComponentHandler {
public:
    struct Edit {
        ParamID id;
        double value;
    };
    std::vector<Edit> performs;
    std::vector<ParamID> begins;
    std::vector<ParamID> ends;

    void clear() {
        performs.clear();
        begins.clear();
        ends.clear();
    }

    Steinberg::tresult PLUGIN_API beginEdit(ParamID id) override {
        begins.push_back(id);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(ParamID id, Steinberg::Vst::ParamValue value) override {
        performs.push_back({.id = id, .value = value});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(ParamID id) override {
        ends.push_back(id);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 /*flags*/) override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    // Owned by the test (never by the controller's IPtr): reference counting is a no-op.
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

[[nodiscard]] const P::FieldInfo& lawOfId(ParamID id) {
    const P::SoundParamRow* row = P::soundRowOf(id);
    REQUIRE(row != nullptr);
    return P::lawOf(row->leaf);
}

// The display's own plain value for an amp envelope ID (ms, level, or curve in [-1, 1]).
[[nodiscard]] double displayPlain(const ADSRDisplay& d, ParamID id) {
    switch (id) {
        case P::kAmpAttackId: return static_cast<double>(d.getAttackMs());
        case P::kAmpDecayId: return static_cast<double>(d.getDecayMs());
        case P::kAmpSustainId: return static_cast<double>(d.getSustainLevel());
        case P::kAmpReleaseId: return static_cast<double>(d.getReleaseMs());
        case P::kAmpAttackCurveId: return static_cast<double>(d.getAttackCurve());
        case P::kAmpDecayCurveId: return static_cast<double>(d.getDecayCurve());
        case P::kAmpReleaseCurveId: return static_cast<double>(d.getReleaseCurve());
        default: FAIL("not an amp envelope ID: " << id); return 0.0;
    }
}

// A known layout before each drag: 100 ms segments (each 98 px wide on the 400 x 150 view), sustain 0.5, flat curves.
void resetDisplay(ADSRDisplay& d) {
    d.setAttackMs(100.0f);
    d.setDecayMs(100.0f);
    d.setSustainLevel(0.5f);
    d.setReleaseMs(100.0f);
    d.setAttackCurve(0.0f);
    d.setDecayCurve(0.0f);
    d.setReleaseCurve(0.0f);
}

// The public mouse path: press at @p start, one move by (dx, dy), release.
void dragFrom(ADSRDisplay& d, VSTGUI::CPoint start, double dx, double dy) {
    const VSTGUI::CButtonState lb(VSTGUI::kLButton);
    VSTGUI::CPoint down = start;
    REQUIRE(d.onMouseDown(down, lb) == VSTGUI::kMouseEventHandled);
    VSTGUI::CPoint to(start.x + dx, start.y + dy);
    REQUIRE(d.onMouseMoved(to, lb) == VSTGUI::kMouseEventHandled);
    REQUIRE(d.onMouseUp(to, lb) == VSTGUI::kMouseEventHandled);
}

// The middle of a segment's curve (inside its middle third, far from every control point).
[[nodiscard]] VSTGUI::CPoint curvePoint(const ADSRDisplay& d, DragTarget curve) {
    const ADSRDisplay::SegmentLayout l = d.getLayout();
    const double y = 0.5 * static_cast<double>(l.topY + l.bottomY);
    switch (curve) {
        case DragTarget::AttackCurve: return {0.5 * static_cast<double>(l.attackStartX + l.attackEndX), y};
        case DragTarget::DecayCurve: return {0.5 * static_cast<double>(l.attackEndX + l.decayEndX), y};
        case DragTarget::ReleaseCurve: return {0.5 * static_cast<double>(l.sustainEndX + l.releaseEndX), y};
        default: FAIL("not a curve target"); return {};
    }
}

// The parameter's displayed string parsed back to a plain value, and the half unit of its last printed digit.
struct Parsed {
    double plain = 0.0;
    double halfUnit = 0.0;
};

[[nodiscard]] Parsed parseDisplayed(P::Controller& c, ParamID id, double v) {
    Steinberg::Vst::String128 s{};
    REQUIRE(c.getParamStringByValue(id, v, s) == Steinberg::kResultOk);
    char text[128]{};
    Steinberg::UString(s, 128).toAscii(text, 128);
    double x = 0.0;
    char unit[16]{};
    INFO("displayed: " << text);
    REQUIRE(std::sscanf(text, "%lf %15s", &x, unit) == 2);
    const std::string u(unit);
    if (u == "ms") {
        return {.plain = x, .halfUnit = 0.05};  // "%.1f ms" (sound_param_row.h formatSoundRow, DisplayUnit::Time)
    }
    if (u == "s") {
        return {.plain = 1000.0 * x, .halfUnit = 5.0};  // "%.2f s"
    }
    REQUIRE(u == "%");  // "%.0f%%" (Percent) and "%+.0f%%" (BipolarPercent)
    return {.plain = x / 100.0, .halfUnit = 0.005};
}

// Every recorded edit: an ID in 420 - 426, its value the registered law of the display's own plain value within 1e-6,
// and its string round-trips to that plain value at the display precision. @p mustEmit must be among them.
void checkEdits(P::Controller& c, const AdsrEditRecorder& rec, const ADSRDisplay& d, ParamID mustEmit) {
    REQUIRE_FALSE(rec.performs.empty());
    bool emitted = false;
    for (const AdsrEditRecorder::Edit& e : rec.performs) {
        INFO("ID " << e.id << " value " << e.value);
        REQUIRE(e.id >= P::kAmpAttackId);
        REQUIRE(e.id <= P::kAmpReleaseCurveId);
        emitted = emitted || e.id == mustEmit;
        const double plain = displayPlain(d, e.id);
        const double expected = P::normalizedFromPlain(lawOfId(e.id), plain);
        INFO("plain " << plain << " expected " << expected);
        CHECK(std::abs(e.value - expected) <= 1e-6);
        const Parsed parsed = parseDisplayed(c, e.id, e.value);
        INFO("parsed " << parsed.plain);
        CHECK(std::abs(parsed.plain - plain) <= parsed.halfUnit * 1.001 + 1e-6 * std::abs(plain));
    }
    CHECK(emitted);
    for (ParamID id : rec.begins) {
        CHECK((id >= P::kAmpAttackId && id <= P::kAmpReleaseCurveId));
    }
    for (ParamID id : rec.ends) {
        CHECK((id >= P::kAmpAttackId && id <= P::kAmpReleaseCurveId));
    }
}

constexpr double kFarDrag = 5000.0;  // far past either clamp in one move

// The wiring wireAdsrDisplay applies (plan 4.6.10).
void checkWiring(const ADSRDisplay& display) {
    CHECK(display.getAttackParamId() == P::kAmpAttackId);
    CHECK(display.getReleaseParamId() == P::kAmpReleaseId);
    CHECK(display.getAttackCurveParamId() == P::kAmpAttackCurveId);
    CHECK(display.getReleaseCurveParamId() == P::kAmpReleaseCurveId);
    CHECK_FALSE(display.getBezierEnabled());
    CHECK(display.getAttackMaxMs() == Krate::DSP::kMaxEnvelopeTimeMs);
}

// The public mouse path over 420 - 426 (T025): every edit at the registered law within 1e-6 and round-tripping its
// string; then the reverse path setParamNormalized -> the display. Shared by the test-built and the built-tree arms.
void runAdapterArms(P::Controller& c, AdsrEditRecorder& rec, ADSRDisplay& display) {
    // ---- 420, 421, 423: clamped minimum, near 100 ms (a 1-pixel drag), clamped maximum ----
    struct TimeCase {
        ParamID id;
        DragTarget point;
    };
    for (const TimeCase tc : {TimeCase{.id = P::kAmpAttackId, .point = DragTarget::PeakPoint},
                              TimeCase{.id = P::kAmpDecayId, .point = DragTarget::SustainPoint},
                              TimeCase{.id = P::kAmpReleaseId, .point = DragTarget::EndPoint}}) {
        for (const double dx : {-kFarDrag, 1.0, kFarDrag}) {
            INFO("time ID " << tc.id << " dx " << dx);
            resetDisplay(display);
            rec.clear();
            dragFrom(display, display.getControlPointPosition(tc.point), dx, 0.0);
            checkEdits(c, rec, display, tc.id);
            const double plain = displayPlain(display, tc.id);
            if (dx < 0.0) {
                CHECK(plain == static_cast<double>(ADSRDisplay::kMinTimeMs));
            } else if (dx > 0.0 && dx < 2.0) {
                CHECK(plain > 100.0);
                CHECK(plain < 110.0);
            } else {
                CHECK(plain == static_cast<double>(Krate::DSP::kMaxEnvelopeTimeMs));
            }
            // The adapter also displayed the value it sent (setParamNormalized before performEdit).
            const auto last = std::find_if(rec.performs.rbegin(), rec.performs.rend(),
                                           [&tc](const AdsrEditRecorder::Edit& e) { return e.id == tc.id; });
            REQUIRE(last != rec.performs.rend());
            CHECK(std::abs(c.getParamNormalized(tc.id) - last->value) <= 1e-12);
        }
    }

    // ---- 422 (sustain, vertical drag of the sustain point): min / mid / max ----
    for (const double dy : {kFarDrag, -1.0, -kFarDrag}) {
        INFO("sustain dy " << dy);
        resetDisplay(display);
        rec.clear();
        dragFrom(display, display.getControlPointPosition(DragTarget::SustainPoint), 0.0, dy);
        checkEdits(c, rec, display, P::kAmpSustainId);
        const double level = displayPlain(display, P::kAmpSustainId);
        if (dy > 0.0) {
            CHECK(level == 0.0);
        } else if (dy > -2.0) {
            CHECK(level > 0.5);
            CHECK(level < 0.6);
        } else {
            CHECK(level == 1.0);
        }
    }

    // ---- 424 - 426 (curves, vertical drag of the segment): min / mid / max ----
    struct CurveCase {
        ParamID id;
        DragTarget curve;
    };
    for (const CurveCase cc : {CurveCase{.id = P::kAmpAttackCurveId, .curve = DragTarget::AttackCurve},
                               CurveCase{.id = P::kAmpDecayCurveId, .curve = DragTarget::DecayCurve},
                               CurveCase{.id = P::kAmpReleaseCurveId, .curve = DragTarget::ReleaseCurve}}) {
        for (const double dy : {-kFarDrag, 1.0, kFarDrag}) {
            INFO("curve ID " << cc.id << " dy " << dy);
            resetDisplay(display);
            rec.clear();
            const VSTGUI::CPoint at = curvePoint(display, cc.curve);
            REQUIRE(display.hitTest(at) == cc.curve);
            dragFrom(display, at, 0.0, dy);
            checkEdits(c, rec, display, cc.id);
            const double curve = displayPlain(display, cc.id);
            if (dy < 0.0) {
                CHECK(curve == -1.0);
            } else if (dy < 2.0) {
                CHECK(curve > 0.0);
                CHECK(curve < 0.1);
            } else {
                CHECK(curve == 1.0);
            }
        }
    }

    // ---- Reverse path: setParamNormalized -> the display's field ----
    for (const ParamID id : {static_cast<ParamID>(P::kAmpAttackId), static_cast<ParamID>(P::kAmpDecayId),
                             static_cast<ParamID>(P::kAmpReleaseId)}) {
        for (const double v : {0.0, 0.5, 1.0}) {
            INFO("reverse ID " << id << " v " << v);
            REQUIRE(c.setParamNormalized(id, v) == Steinberg::kResultTrue);
            const double expected = P::plainFromNormalized(lawOfId(id), v);
            const double shown = displayPlain(display, id);
            CHECK(std::abs(shown - expected) <= 1e-4 * expected);
        }
    }
}

// Left press, one move and release at the centre of @p view, through both the legacy entry points and the event
// dispatch (CView::dispatchEvent), plus one wheel step.
void pokeView(VSTGUI::CView& view) {
    const VSTGUI::CPoint centre = view.getViewSize().getCenter();
    const VSTGUI::CButtonState lb(VSTGUI::kLButton);
    VSTGUI::CPoint down = centre;
    static_cast<void>(view.onMouseDown(down, lb));
    VSTGUI::CPoint to(centre.x + 10.0, centre.y - 10.0);
    static_cast<void>(view.onMouseMoved(to, lb));
    static_cast<void>(view.onMouseUp(to, lb));

    VSTGUI::MouseDownEvent downEvent(centre, VSTGUI::MouseButton::Left);
    view.dispatchEvent(downEvent);
    VSTGUI::MouseMoveEvent moveEvent(to, VSTGUI::MouseButton::Left);
    view.dispatchEvent(moveEvent);
    VSTGUI::MouseUpEvent upEvent(to, VSTGUI::MouseButton::Left);
    view.dispatchEvent(upEvent);
    VSTGUI::MouseWheelEvent wheel;
    wheel.mousePosition = centre;
    wheel.deltaY = 1.0;
    view.dispatchEvent(wheel);
}

}  // namespace

TEST_CASE("Profundum_UiCustomViewLaws", "[profundum][ui]") {
    SECTION("AdapterOnTestDisplay") {  // SC-024 adapter arm, FR-034
        AdsrEditRecorder rec;  // declared before the fixture: it outlives the controller's handler pointer
        ControllerFixture cf;
        REQUIRE(cf.controller->setComponentHandler(&rec) == Steinberg::kResultTrue);
        P::Controller& c = *cf.controller;

        auto display = VSTGUI::makeOwned<ADSRDisplay>(VSTGUI::CRect(0, 0, 400, 150), nullptr, -1);
        c.wireAdsrDisplay(display.get());
        checkWiring(*display);

        runAdapterArms(c, rec, *display);

        // ---- Negative arm: a raw-wired display emits the cubic value, not the registered law ----
        {
            auto raw = VSTGUI::makeOwned<ADSRDisplay>(VSTGUI::CRect(0, 0, 400, 150), nullptr, -1);
            raw->setAdsrBaseParamId(P::kAmpAttackId);
            raw->setCurveBaseParamId(P::kAmpAttackCurveId);
            raw->setParameterCallback([&c](std::uint32_t id, float n) {
                static_cast<void>(c.performEdit(id, static_cast<double>(n)));
            });
            resetDisplay(*raw);
            rec.clear();
            dragFrom(*raw, raw->getControlPointPosition(DragTarget::PeakPoint), 1.0, 0.0);
            const auto it = std::find_if(rec.performs.begin(), rec.performs.end(),
                                         [](const AdsrEditRecorder::Edit& e) { return e.id == P::kAmpAttackId; });
            REQUIRE(it != rec.performs.end());
            const double plain = static_cast<double>(raw->getAttackMs());
            const double registered = P::normalizedFromPlain(lawOfId(P::kAmpAttackId), plain);
            INFO("raw " << it->value << " registered " << registered << " at " << plain << " ms");
            CHECK(std::abs(it->value - registered) > 0.1);  // cubic 0.22 against logarithmic 0.60 near 100 ms
            CHECK(std::abs(P::Controller::adsrToRegistered(P::kAmpAttackId, static_cast<float>(it->value)) -
                           registered) <= 1e-6);
        }

        c.wireAdsrDisplay(nullptr);  // the view is released before the controller
    }

    SECTION("BuiltTreeDisplay") {  // SC-024 built-tree arm (T030)
        AdsrEditRecorder rec;  // declared before the fixture: it outlives the controller's handler pointer
        ControllerFixture cf;
        P::Controller& c = *cf.controller;
        ProfundumPluginTest::HeadlessEditor ed(cf.controller.get());  // destroyed first: willClose unwires
        REQUIRE(ed.frame() != nullptr);
        REQUIRE(c.setComponentHandler(&rec) == Steinberg::kResultTrue);

        const std::vector<ADSRDisplay*> displays = ProfundumPluginTest::collectViewsOfType<ADSRDisplay>(ed.frame());
        REQUIRE(displays.size() == 1u);
        ADSRDisplay& display = *displays[0];
        checkWiring(display);  // wired by the panel's verifyView, not by this test

        // The controller drives this very instance: a reverse write lands on it.
        REQUIRE(c.setParamNormalized(P::kAmpAttackId, 1.0) == Steinberg::kResultTrue);
        CHECK(displayPlain(display, P::kAmpAttackId) == static_cast<double>(Krate::DSP::kMaxEnvelopeTimeMs));

        runAdapterArms(c, rec, display);
    }

    SECTION("BarViewWritesNothing") {  // FR-070: the bar views are display only
        AdsrEditRecorder rec;  // declared before the fixture: it outlives the controller's handler pointer
        ControllerFixture cf;
        ProfundumPluginTest::HeadlessEditor ed(cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        REQUIRE(cf.controller->setComponentHandler(&rec) == Steinberg::kResultTrue);

        const std::vector<P::UI::HarmonicBarView*> bars =
            ProfundumPluginTest::collectViewsOfType<P::UI::HarmonicBarView>(ed.frame());
        REQUIRE(bars.size() == 2u);
        rec.clear();
        for (P::UI::HarmonicBarView* bar : bars) {
            pokeView(*bar);
        }
        CHECK(rec.performs.empty());
        CHECK(rec.begins.empty());
        CHECK(rec.ends.empty());

        // Non-vacuity: the same recorder sees an edit the controller does issue.
        REQUIRE(cf.controller->beginEdit(P::kAmpSustainId) == Steinberg::kResultTrue);
        REQUIRE(cf.controller->performEdit(P::kAmpSustainId, 0.25) == Steinberg::kResultTrue);
        REQUIRE(cf.controller->endEdit(P::kAmpSustainId) == Steinberg::kResultTrue);
        CHECK(rec.performs.size() == 1u);
    }
}
