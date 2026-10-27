// ==============================================================================
// Profundum Phase 12 - editor sync timer, delegate names, zoom (T026)
// ==============================================================================
// Spec:  specs/profundum-phase12-ui/spec.md  (FR-014, FR-090 - FR-092; SC-015, SC-032 core)
// Plan:  specs/profundum-phase12-ui/plan.md  (sections 4.6.2, 4.6.7, 4.6.9, 6.2 "Editor sync and lifecycle"; R-6)
// Tasks: specs/profundum-phase12-ui/tasks.md (T026)
//
// Everything runs headless: no CVSTGUITimer and no processor SDK timer is ever
// pumped (there is no portable headless pump, Vorago R-5), so the editor sync
// timer is driven through editorSyncTickForTest(). That reproduces a host with no
// run loop, where only the controller's own request keeps an open editor fresh.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "profundum_test_fixture.h"
#include "ui_tree_helpers.h"

#include "controller/controller.h"
#include "parameters/sound_param_row.h"
#include "plugin_ids.h"
#include "ui/harmonic_bar_view.h"
#include "ui/panel_sub_controller.h"
#include "ui/spread_curve_view.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiattributes.h"

#include <krate/dsp/systems/bass_morph_engine.h>

#include <array>
#include <cstdint>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;
using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;

constexpr std::uint8_t kDepthLeaf = 0;  // recipe.depth, the leaf of Core Depth (200)

}  // namespace

TEST_CASE("Profundum_EditorSyncTimer", "[profundum][ui][lifecycle]") {
    SECTION("TimerExistsOnlyWhileOpen") {  // SC-015 bullet 1, FR-090, FR-091
        ControllerFixture cf;
        STATIC_REQUIRE(P::Controller::kEditorSyncIntervalMs == 100u);
        STATIC_REQUIRE(P::Controller::kEditorSyncIntervalMs <= 100u);
        CHECK_FALSE(cf.controller->hasEditorSyncTimerForTest());
        for (int cycle = 0; cycle < 3; ++cycle) {
            {
                HeadlessEditor ed(cf.controller.get());
                REQUIRE(ed.frame() != nullptr);
                CHECK(cf.controller->hasEditorSyncTimerForTest());
                CHECK(cf.controller->editorSyncIntervalForTest() == 100u);
                CHECK(cf.controller->editorSyncIntervalForTest() <= 100u);
            }
            CHECK_FALSE(cf.controller->hasEditorSyncTimerForTest());
            CHECK(cf.controller->viewPointersNullForTest());
        }
    }

    SECTION("OneRequestPerTick") {  // SC-015 bullet 2, R-6
        Steinberg::Vst::HostApplication host;  // allocateMessage() works
        CountingConnectionPoint peer;
        ControllerFixture cf{&host};
        REQUIRE(cf.controller->connect(&peer) == Steinberg::kResultOk);
        {
            HeadlessEditor ed(cf.controller.get());
            REQUIRE(ed.frame() != nullptr);
            const int n0 = peer.notifications;
            cf.controller->editorSyncTickForTest();
            CHECK(peer.notifications == n0 + 1);
            cf.controller->editorSyncTickForTest();
            CHECK(peer.notifications == n0 + 2);

            // R-6: no request while a parameter gesture is open.
            REQUIRE(cf.controller->beginEdit(P::kCoreDepthId) == Steinberg::kResultOk);
            cf.controller->editorSyncTickForTest();
            CHECK(peer.notifications == n0 + 2);
            REQUIRE(cf.controller->endEdit(P::kCoreDepthId) == Steinberg::kResultOk);
            cf.controller->editorSyncTickForTest();
            CHECK(peer.notifications == n0 + 3);
        }
        const int afterClose = peer.notifications;
        cf.controller->editorSyncTickForTest();
        CHECK(peer.notifications == afterClose);  // the timer is gone: nothing is sent
        CHECK(peer.dataExchangeMessages == 0);
        static_cast<void>(cf.controller->disconnect(&peer));
    }

    SECTION("SynchronousPathUpdatesDisplay") {  // SC-015 bullet 3
        ConnectedPair cp;
        REQUIRE(cp.fx.prepare() == Steinberg::kResultOk);
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        REQUIRE(cp.cf.controller->getParamNormalized(P::kMorphEditTargetId) == 0.0);  // Edit Target A

        cp.clearRecords();
        cp.cf.controller->editorSyncTickForTest();
        CHECK(cp.toProcessor.count(P::kMsgRequestSync) == 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 0) == 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 1) == 1u);
        CHECK(cp.toController.count(P::kMsgCompareMode) == 1u);

        // A processor-side endpoint change, delivered through process(); no timer runs on either side.
        const double before = cp.cf.controller->getParamNormalized(P::kCoreDepthId);
        const double written = before < 0.5 ? 0.83 : 0.17;
        REQUIRE(cp.fx.setParam(P::kCoreDepthId, written) == Steinberg::kResultOk);
        const std::uint32_t slotA = cp.fx.proc->endpointSlotForTest(Endpoint::A, kDepthLeaf);
        REQUIRE(slotA == P::leafWordFromNormalized(kDepthLeaf, written));
        const double expected = P::normalizedFromLeafWord(kDepthLeaf, slotA);
        REQUIRE(expected != before);
        CHECK(cp.cf.controller->getParamNormalized(P::kCoreDepthId) == before);  // nothing pumped: still stale

        cp.clearRecords();
        cp.cf.controller->editorSyncTickForTest();
        CHECK(cp.toProcessor.count(P::kMsgRequestSync) == 1u);
        CHECK(cp.cf.controller->getParamNormalized(P::kCoreDepthId) == expected);
    }

    SECTION("DelegateNames") {  // FR-092
        ControllerFixture cf;
        Krate::TestSupport::ensureVstguiInitialized();

        VSTGUI::UIAttributes attrs;
        CHECK(cf.controller->createCustomView("nonsense", attrs, nullptr, nullptr) == nullptr);
        CHECK(cf.controller->createSubController("nonsense", nullptr, nullptr) == nullptr);

        // The three names the delegate does serve, with the rect taken from origin / size (plan 4.6.2).
        attrs.setPointAttribute("origin", VSTGUI::CPoint(10.0, 20.0));
        attrs.setPointAttribute("size", VSTGUI::CPoint(300.0, 120.0));
        attrs.setAttribute("bar-variant", "core");
        VSTGUI::CView* bars = cf.controller->createCustomView("HarmonicBarView", attrs, nullptr, nullptr);
        auto* barView = dynamic_cast<P::UI::HarmonicBarView*>(bars);
        REQUIRE(barView != nullptr);
        CHECK(barView->variant() == P::UI::HarmonicBarView::Variant::Core);
        CHECK(bars->getViewSize() == VSTGUI::CRect(10.0, 20.0, 310.0, 140.0));
        VSTGUI::CView* spread = cf.controller->createCustomView("SpreadCurveView", attrs, nullptr, nullptr);
        REQUIRE(dynamic_cast<P::UI::SpreadCurveView*>(spread) != nullptr);
        VSTGUI::IController* panel = cf.controller->createSubController("ProfundumPanel", nullptr, nullptr);
        REQUIRE(dynamic_cast<P::UI::ProfundumPanelSubController*>(panel) != nullptr);
        delete panel;  // a DelegationController is not reference counted
        bars->forget();
        spread->forget();
        cf.controller->willClose(nullptr);  // forgets the view pointers the delegate stored
        CHECK(cf.controller->viewPointersNullForTest());

        Steinberg::IPlugView* editorView = cf.controller->createView(Steinberg::Vst::ViewType::kEditor);
        REQUIRE(editorView != nullptr);
        const bool isVst3Editor = dynamic_cast<VSTGUI::VST3Editor*>(editorView) != nullptr;
        editorView->release();
        CHECK(isVst3Editor);
        CHECK(cf.controller->createView("other") == nullptr);
    }

    SECTION("ZoomIsSessionState") {  // FR-014, SC-032 core (T027 adds the control arm)
        ConnectedPair cp;
        P::Controller& ctl = *cp.cf.controller.get();
        const StateBytes state0 = writeV2Stream(*cp.fx.proc);
        const Steinberg::int32 paramCount0 = ctl.getParameterCount();
        REQUIRE(paramCount0 == 95);
        constexpr std::array<double, 4> kExpected{1.0, 1.25, 1.5, 2.0};
        STATIC_REQUIRE(P::Controller::kZoomSteps.size() == kExpected.size());

        cp.clearRecords();
        {
            HeadlessEditor ed(cp.cf.controller.get());
            REQUIRE(ed.editor() != nullptr);
            CHECK(ed.editor()->getZoomFactor() == 1.0);
            for (int i = 0; i < 4; ++i) {
                ctl.setZoomStep(i);
                CHECK(ctl.zoomStep() == i);
                CHECK(ed.editor()->getZoomFactor() == kExpected[static_cast<std::size_t>(i)]);
            }
            ctl.setZoomStep(7);  // clamped
            CHECK(ctl.zoomStep() == 3);
            ctl.setZoomStep(-1);
            CHECK(ctl.zoomStep() == 0);
            CHECK(ed.editor()->getZoomFactor() == 1.0);
            ctl.setZoomStep(2);
        }
        CHECK(ctl.zoomStep() == 2);  // session state survives the close
        {
            HeadlessEditor ed(cp.cf.controller.get());
            REQUIRE(ed.editor() != nullptr);
            CHECK(ed.editor()->getZoomFactor() == 1.5);  // didOpen re-applies the session step
        }
        ctl.setZoomStep(1);  // closed: stored only
        CHECK(ctl.zoomStep() == 1);

        // Zoom writes no parameter, no state and no message.
        CHECK(cp.toProcessor.records().empty());
        CHECK(cp.cf.handler.performEdits.empty());
        CHECK(ctl.getParameterCount() == paramCount0);
        CHECK(writeV2Stream(*cp.fx.proc) == state0);
    }
}
