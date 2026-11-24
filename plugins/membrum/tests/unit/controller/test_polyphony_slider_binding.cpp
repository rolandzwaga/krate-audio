// ==============================================================================
// PolyphonySlider binding (Membrum audit MB-096, fix plan T1.24)
// ==============================================================================
// The Kit column's Max Polyphony slider is a custom view (uidesc class "CView",
// custom-view-name "PolyphonySlider"), so VSTGUI's control creator never sets a
// listener on it, and VST3Editor::verifyView only binds a ParameterChangeListener
// to a control whose listener is the editor. Without it a drag edited nothing
// and the bar never followed the parameter. This case opens the real editor
// headlessly, drags the slider and moves the parameter from the controller side.
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"
#include "ui/polyphony_slider.h"

#include "unit/controller/recording_component_handler.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <vstgui/lib/cbuttonstate.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/plugin-bindings/vst3editor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;

int countId(const std::vector<Steinberg::Vst::ParamID>& ids, Steinberg::Vst::ParamID id)
{
    return static_cast<int>(std::count(ids.begin(), ids.end(), id));
}

int countEdits(const Membrum::TestSupport::RecordingComponentHandler& handler,
               Steinberg::Vst::ParamID id)
{
    return static_cast<int>(std::count_if(handler.edits.begin(), handler.edits.end(),
                                          [id](const auto& e) { return e.first == id; }));
}

} // namespace

TEST_CASE("PolyphonySlider: a drag edits Max Polyphony and the slider follows the parameter",
          "[voice_management][ui]")
{
    using Membrum::kMaxPolyphonyId;

    Steinberg::UpdateHandler::instance(); // IDependent updates need the update handler
    auto controller = Steinberg::owned(new Membrum::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    Membrum::TestSupport::RecordingComponentHandler handler;
    REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultOk);

    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(MEMBRUM_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "EditorDefault", uidesc.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);
        VSTGUI::CFrame* cframe = editor->getFrame();
        REQUIRE(cframe != nullptr);
        VSTGUI::CViewContainer* root =
            cframe->getView(0) != nullptr ? cframe->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        auto* slider = Shots::findFirst<Membrum::UI::PolyphonySliderView>(root);
        REQUIRE(slider != nullptr);
        CHECK(slider->getTag() == static_cast<int32_t>(kMaxPolyphonyId));
        CHECK(slider->getListener() != nullptr);

        // Anything the editor sent while opening is not part of the gesture.
        handler.edits.clear();
        handler.beginEditIds.clear();
        handler.endEditIds.clear();

        // Drag: press and release at 75 % of the bar = 13 voices = (13 - 4) / 12.
        const VSTGUI::CRect rect = slider->getViewSize();
        VSTGUI::CPoint where(rect.left + 0.75 * rect.getWidth(), rect.top + rect.getHeight() * 0.5);
        const VSTGUI::CButtonState left(VSTGUI::kLButton);

        CHECK(slider->onMouseDown(where, left) == VSTGUI::kMouseEventHandled);
        // The edit lands inside the gesture: begin and performEdit before the release.
        // (VSTGUI's ParameterChangeListener echoes the edit: setParamNormalized updates
        // the bound control, whose valueChanged re-enters performEdit with the same
        // value, vst3editor.cpp ParameterChangeListener::performEdit / updateControlValue.
        // Every editor-bound control does this, so the count is not pinned to one.)
        const int editsAfterPress = countEdits(handler, kMaxPolyphonyId);
        CHECK(countId(handler.beginEditIds, kMaxPolyphonyId) == 1);
        CHECK(editsAfterPress >= 1);
        CHECK(countId(handler.endEditIds, kMaxPolyphonyId) == 0);

        CHECK(slider->onMouseUp(where, left) == VSTGUI::kMouseEventHandled);
        // The release lands on the same value: no further edit, and one gesture closed.
        CHECK(countId(handler.beginEditIds, kMaxPolyphonyId) == 1);
        CHECK(countEdits(handler, kMaxPolyphonyId) == editsAfterPress);
        CHECK(countId(handler.endEditIds, kMaxPolyphonyId) == 1);

        CHECK(controller->getParamNormalized(kMaxPolyphonyId) == Catch::Approx(0.75).margin(1e-6));
        for (const auto& [id, value] : handler.edits)
        {
            if (id == kMaxPolyphonyId)
                CHECK(value == Catch::Approx(0.75).margin(1e-6));
        }

        // Automation, a kit load or setComponentState move the parameter on the
        // controller: the parameter's IDependent update reaches the slider through
        // the editor's ParameterChangeListener.
        controller->setParamNormalized(kMaxPolyphonyId, 2.0 / 12.0);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        CHECK(slider->getValueNormalized() == Catch::Approx(2.0 / 12.0).margin(1e-6));

        view->removed();
        view->release();
    }
    controller->setComponentHandler(nullptr);
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}
