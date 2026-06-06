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
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "ui/pad_grid_view.h"
#include "state/state_codec.h"
#include "plugin_ids.h"

#include "vstgui/lib/cview.h"
#include "vstgui/uidescription/uiattributes.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <string>

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
