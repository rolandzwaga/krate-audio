// ==============================================================================
// Correctness-audit UI/controller batch -- regression tests for findings
// 6, 10, 11, 17 (see plugins/membrum/docs/correctness-audit.md).
//
//   #6  PadGridView selection highlight must track every source of a
//       kSelectedPadId change (host automation + state load), not only a
//       mouse-down on the grid.
//   #10 Friction Pressure global proxy default must match the per-pad default
//       (0.0) so a fresh instance displays the value the DSP actually uses.
//   #11 Morph Enabled proxy must expose a single step (boolean toggle), not a
//       continuous range.
//   #17 Choke Group proxy must be a StringListParameter with friendly labels,
//       with a normalized<->index mapping identical to the old stepped Range.
//
// Membrum audit fix plan T2.20:
//   MB-101 the production grid's meta provider returned null, so the category
//          glyph and the CG / BUS indicators (spec 141 FR-011 (c)(d)(e)) were
//          never drawn.
//   MB-157 grid mutations from setParamNormalized / setComponentState must go
//          through the deferred view refresh (finding 12) like every other
//          view mutation.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "ui/pad_grid_view.h"
#include "state/state_codec.h"
#include "dsp/body_model_type.h"
#include "dsp/default_kit.h"
#include "dsp/exciter_type.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include "base/source/updatehandler.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiattributes.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <string>
#include <thread>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

// Build the PadGridView through the real controller factory path so the test
// exercises the exact wiring the editor uses. Returns the grid (owned by the
// returned smart pointer) with the controller's `padGridView_` pointing at it.
VSTGUI::SharedPointer<Membrum::UI::PadGridView>
makeGrid(Membrum::Controller& controller)
{
    VSTGUI::UIAttributes attrs;
    attrs.setPointAttribute("size", VSTGUI::CPoint{400, 800});
    VSTGUI::CView* view =
        controller.createCustomView("PadGridView", attrs, nullptr, nullptr);
    REQUIRE(view != nullptr);
    auto* grid = dynamic_cast<Membrum::UI::PadGridView*>(view);
    REQUIRE(grid != nullptr);
    // createCustomView returns a +1 reference; VSTGUI::owned() adopts it
    // without an extra remember(), so the SharedPointer releases it on scope
    // exit (no leak, no double-free).
    return VSTGUI::owned(grid);
}

} // namespace

// ------------------------------------------------------------------------------
// #6 -- automation path: moving kSelectedPadId must move the grid highlight.
// ------------------------------------------------------------------------------
TEST_CASE("Audit #6: kSelectedPadId automation updates the grid highlight",
          "[membrum][controller][audit][selection]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto grid = makeGrid(controller);
    REQUIRE(grid->selectedPadIndex() == 0);

    // Pad index 19 of [0,31] -> normalized 19/31.
    REQUIRE(controller.setParamNormalized(Membrum::kSelectedPadId, 19.0 / 31.0)
            == kResultTrue);

    CHECK(grid->selectedPadIndex() == 19);

    REQUIRE(controller.terminate() == kResultOk);
}

// ------------------------------------------------------------------------------
// #6 -- state-load path: setComponentState with a non-zero selected pad must
// move the grid highlight (a loaded project should not leave it stale on 0).
// ------------------------------------------------------------------------------
TEST_CASE("Audit #6: setComponentState updates the grid highlight",
          "[membrum][controller][audit][selection]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto grid = makeGrid(controller);
    REQUIRE(grid->selectedPadIndex() == 0);

    // Minimal kit blob whose only field of interest is the selected pad.
    Membrum::State::KitSnapshot kit;
    kit.selectedPadIndex = 23;

    MemoryStream stream;
    REQUIRE(Membrum::State::writeKitBlob(&stream, kit) == kResultOk);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);

    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    CHECK(grid->selectedPadIndex() == 23);

    REQUIRE(controller.terminate() == kResultOk);
}

// ------------------------------------------------------------------------------
// #10 -- Friction Pressure proxy default aligned to the per-pad default (0.0).
// ------------------------------------------------------------------------------
TEST_CASE("Audit #10: Friction Pressure proxy default is 0.0 (matches per-pad)",
          "[membrum][controller][audit][defaults]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* proxy = controller.getParameterObject(Membrum::kExciterFrictionPressureId);
    REQUIRE(proxy != nullptr);
    CHECK(proxy->getInfo().defaultNormalizedValue == Approx(0.0).margin(1e-12));

    REQUIRE(controller.terminate() == kResultOk);
}

// ------------------------------------------------------------------------------
// #11 -- Morph Enabled proxy is a single-step boolean toggle.
// ------------------------------------------------------------------------------
TEST_CASE("Audit #11: Morph Enabled proxy exposes stepCount=1",
          "[membrum][controller][audit][toggle]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* proxy = controller.getParameterObject(Membrum::kMorphEnabledId);
    REQUIRE(proxy != nullptr);

    const auto& info = proxy->getInfo();
    CHECK(info.stepCount == 1);
    CHECK(info.defaultNormalizedValue == Approx(0.0).margin(1e-12));

    // Toggle endpoints still round-trip to plain 0 / 1.
    CHECK(proxy->toPlain(0.0) == Approx(0.0).margin(1e-9));
    CHECK(proxy->toPlain(1.0) == Approx(1.0).margin(1e-9));

    REQUIRE(controller.terminate() == kResultOk);
}

// ------------------------------------------------------------------------------
// #17 -- Choke Group proxy is a StringListParameter, mapping unchanged.
// ------------------------------------------------------------------------------
TEST_CASE("Audit #17: Choke Group proxy is a StringList with friendly labels",
          "[membrum][controller][audit][stringlist]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* param = controller.getParameterObject(Membrum::kChokeGroupId);
    REQUIRE(param != nullptr);

    const auto& info = param->getInfo();
    CHECK((info.flags & ParameterInfo::kIsList) != 0);
    CHECK(info.stepCount == 8);  // 9 entries -> 8 steps

    // First entry reads "None", and the mapping is bit-identical to the old
    // RangeParameter(0..8, stepCount=8): norm idx/8 -> plain index idx.
    String128 label{};
    param->toString(0.0, label);
    CHECK(std::u16string(reinterpret_cast<const char16_t*>(label))
          == std::u16string(u"None"));

    for (int idx = 0; idx <= 8; ++idx)
    {
        const double norm = static_cast<double>(idx) / 8.0;
        CHECK(static_cast<int>(param->toPlain(norm)) == idx);
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// ------------------------------------------------------------------------------
// MB-101 -- the production grid reads each pad's choke group, output bus and
// category from the controller's per-pad parameters (spec 141 FR-011 (c)(d)(e),
// acceptance scenario: pad 4 shows "CG2" / "BUS3").
// ------------------------------------------------------------------------------
TEST_CASE("Pad grid shows the choke group, output bus and category of each pad",
          "[membrum][controller][pad_grid]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto grid = makeGrid(controller);
    const auto padTag = [](int pad, int offset) {
        return static_cast<ParamID>(Membrum::padParamId(pad, offset));
    };

    controller.setParamNormalized(padTag(4, Membrum::kPadChokeGroup), 2.0 / 8.0);
    controller.setParamNormalized(padTag(4, Membrum::kPadOutputBus), 3.0 / 15.0);

    const Membrum::PadConfig* cfg = grid->padConfigFor(4);
    REQUIRE(cfg != nullptr);
    CHECK(cfg->chokeGroup == 2);
    CHECK(cfg->outputBus == 3);
    CHECK(Membrum::UI::chokeGroupIndicatorText(cfg->chokeGroup) == "CG2");
    CHECK(Membrum::UI::outputBusIndicatorText(cfg->outputBus) == "BUS3");

    // Pad 4 takes the default kit's kick (pad 0): Membrane, Impulse, pitch
    // envelope on. Selectors encode at the centre of their slot, as the
    // controller's state codec writes them.
    std::array<Membrum::PadConfig, Membrum::kNumPads> kit{};
    Membrum::DefaultKit::apply(kit);
    const Membrum::PadConfig& kick = kit[0];
    controller.setParamNormalized(
        padTag(4, Membrum::kPadExciterType),
        (static_cast<double>(kick.exciterType) + 0.5)
            / static_cast<double>(Membrum::ExciterType::kCount));
    controller.setParamNormalized(
        padTag(4, Membrum::kPadBodyModel),
        (static_cast<double>(kick.bodyModel) + 0.5)
            / static_cast<double>(Membrum::BodyModelType::kCount));
    controller.setParamNormalized(padTag(4, Membrum::kPadTSPitchEnvTime),
                                  static_cast<double>(kick.tsPitchEnvTime));

    cfg = grid->padConfigFor(4);
    REQUIRE(cfg != nullptr);
    CHECK(Membrum::UI::categoryGlyphForConfig(*cfg) == "K");
    // The routing fields set above are kept.
    CHECK(cfg->chokeGroup == 2);
    CHECK(cfg->outputBus == 3);

    REQUIRE(controller.terminate() == kResultOk);
}

// ------------------------------------------------------------------------------
// MB-157 -- with the editor open, a setParamNormalized from a thread other than
// the UI thread leaves the grid alone; the poll timer's drain applies it.
// ------------------------------------------------------------------------------
TEST_CASE("Pad grid mutations from a non-UI thread are deferred to the UI thread",
          "[membrum][controller][pad_grid][threading]")
{
    namespace Shots = ::Krate::TestSupport::Screenshots;

    UpdateHandler::instance();
    auto controller = Steinberg::owned(new Membrum::Controller());
    REQUIRE(controller->initialize(nullptr) == kResultOk);
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(MEMBRUM_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "EditorDefault", uidesc.c_str());
        IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == kResultTrue);
        VSTGUI::CFrame* cframe = editor->getFrame();
        REQUIRE(cframe != nullptr);
        VSTGUI::CViewContainer* root =
            cframe->getView(0) != nullptr ? cframe->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        // A headless frame has no window, so CFrame::open never attaches the tree.
        if (!root->isAttached())
            root->attached(cframe);
        UpdateHandler::instance()->triggerDeferedUpdates();

        auto* grid = Shots::findFirst<Membrum::UI::PadGridView>(root);
        REQUIRE(grid != nullptr);
        REQUIRE(grid->selectedPadIndex() == 0);

        std::thread worker([&controller] {
            controller->setParamNormalized(Membrum::kSelectedPadId, 19.0 / 31.0);
        });
        worker.join();

        // Nothing touched the view off the UI thread.
        CHECK(grid->selectedPadIndex() == 0);

        // The poll timer's next tick (no timer runs headless) applies it.
        controller->drainPendingViewRefreshForTest();
        CHECK(grid->selectedPadIndex() == 19);

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == kResultOk);
}
