// ==============================================================================
// Material Morph XY pad <-> controller sync (Membrum audit MB-097 / MB-098, fix plan T1.25)
// ==============================================================================
// The Advanced template's XYMorphPad edits Morph Start on X (its control tag) and
// Morph End on Y (its secondary tag). MB-097: the Y axis reached the host through
// performEdit only, so the controller's proxy, the selected pad's per-pad value and
// the pad-preset save kept the old Morph End. MB-098: the cursor was placed once at
// view build and never followed pad selection, automation or a state load, so a
// wheel nudge after a pad switch edited the new pad from the old pad's values.
// These cases open the real editor headlessly, switch to the Advanced template and
// drive the pad.
// ==============================================================================

#include "controller/controller.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "state/state_codec.h"
#include "ui/xy_morph_pad.h"

#include "unit/controller/recording_component_handler.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/events.h>
#include <vstgui/plugin-bindings/vst3editor.h>
#include <vstgui/uidescription/uiviewswitchcontainer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;
using Catch::Approx;
using Steinberg::Vst::ParamID;

ParamID padId(int pad, Membrum::PadParamOffset offset)
{
    return static_cast<ParamID>(Membrum::padParamId(pad, offset));
}

double padNorm(int pad)
{
    return static_cast<double>(pad) / 31.0;
}

/// The real editor, opened headlessly and switched to the Advanced (Extended) template, which holds the
/// Material Morph XY pad.
class AdvancedEditor
{
public:
    explicit AdvancedEditor(Membrum::Controller& controller)
        : controller_(controller)
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(MEMBRUM_RESOURCES_DIR) + "/editor.uidesc";
        editor_ = new VSTGUI::VST3Editor(&controller, "EditorDefault", uidesc.c_str());
        view_ = editor_;
        opened_ = view_->attached(nullptr, Krate::TestSupport::nativePlatformType())
                  == Steinberg::kResultTrue;
        if (!opened_)
            return;
        VSTGUI::CFrame* cframe = editor_->getFrame();
        root_ = (cframe != nullptr && cframe->getView(0) != nullptr)
                    ? cframe->getView(0)->asViewContainer()
                    : nullptr;
        if (root_ == nullptr)
            return;
        // A headless frame has no window, so CFrame::open never attaches the tree.
        if (!root_->isAttached())
            root_->attached(cframe);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        // The mode swap fades over 120 ms once attached; no timer runs here.
        if (auto* panel = Shots::findFirst<VSTGUI::UIViewSwitchContainer>(root_))
            panel->setAnimationTime(0);
        controller_.setParamNormalized(Membrum::kUiModeId, 1.0);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
    }

    ~AdvancedEditor()
    {
        if (opened_)
            view_->removed();
        view_->release();
    }

    AdvancedEditor(const AdvancedEditor&) = delete;
    AdvancedEditor& operator=(const AdvancedEditor&) = delete;
    AdvancedEditor(AdvancedEditor&&) = delete;
    AdvancedEditor& operator=(AdvancedEditor&&) = delete;

    [[nodiscard]] Krate::Plugins::XYMorphPad* pad() const
    {
        if (root_ == nullptr)
            return nullptr;
        auto* panel = Shots::findFirst<VSTGUI::UIViewSwitchContainer>(root_);
        if (panel == nullptr || panel->getCurrentViewIndex() != 1)
            return nullptr;
        return Shots::findFirst<Krate::Plugins::XYMorphPad>(root_);
    }

private:
    Membrum::Controller& controller_;
    VSTGUI::VST3Editor* editor_ = nullptr;
    Steinberg::IPlugView* view_ = nullptr;
    VSTGUI::CViewContainer* root_ = nullptr;
    bool opened_ = false;
};

VSTGUI::CPoint pixelOf(const Krate::Plugins::XYMorphPad& pad, float normX, float normY)
{
    float px = 0.0f;
    float py = 0.0f;
    pad.positionToPixel(normX, normY, px, py);
    return {px, py};
}

} // namespace

TEST_CASE("XYMorphPad: a vertical drag sets Morph End on the proxy, the selected pad and the saved pad preset",
          "[ui][morph]")
{
    Steinberg::UpdateHandler::instance();
    auto controller = Steinberg::owned(new Membrum::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    Membrum::TestSupport::RecordingComponentHandler handler;
    REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultOk);
    {
        AdvancedEditor editor(*controller);
        auto* pad = editor.pad();
        REQUIRE(pad != nullptr);

        constexpr int kPad = 2;
        controller->setParamNormalized(Membrum::kSelectedPadId, padNorm(kPad));

        const VSTGUI::MouseEventButtonState left(VSTGUI::MouseButton::Left);
        VSTGUI::MouseDownEvent down(pixelOf(*pad, 0.3f, 0.6f), left);
        down.clickCount = 1;
        pad->onMouseDownEvent(down);
        VSTGUI::MouseMoveEvent move(pixelOf(*pad, 0.7f, 0.35f), left);
        pad->onMouseMoveEvent(move);
        VSTGUI::MouseUpEvent up(move.mousePosition, left);
        pad->onMouseUpEvent(up);

        const double y = pad->getMorphY();
        CHECK(y == Approx(0.35).margin(0.01));
        CHECK(controller->getParamNormalized(Membrum::kMorphEndId) == Approx(y).margin(1e-6));
        CHECK(controller->getParamNormalized(padId(kPad, Membrum::kPadMorphEnd))
              == Approx(y).margin(1e-6));

        Steinberg::IBStream* blob = controller->padPresetStateProvider();
        REQUIRE(blob != nullptr);
        blob->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        Membrum::State::PadPresetSnapshot snap;
        CHECK(Membrum::State::readPadPresetBlob(blob, snap) == Steinberg::kResultOk);
        blob->release();
        CHECK(snap.sound[25] == Approx(y).margin(1e-6)); // sound[25] = offset 27 = morphEnd
    }
    controller->setComponentHandler(nullptr);
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

TEST_CASE("XYMorphPad: the cursor follows pad selection, automation and preset loads", "[ui][morph]")
{
    Steinberg::UpdateHandler::instance();
    auto controller = Steinberg::owned(new Membrum::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    constexpr int kPadA = 1;
    constexpr int kPadB = 2;
    constexpr int kStatePad = 5;
    constexpr int kPresetPad = 7;

    // The presets and the state to load later, from another controller in Extended mode: the kit holds pad B at
    // morph 0.33 / 0.66 and pad 5 (selected) at 0.44 / 0.77; the pad preset is pad 7 at 0.55 / 0.25.
    auto source = Steinberg::owned(new Membrum::Controller());
    REQUIRE(source->initialize(nullptr) == Steinberg::kResultOk);
    source->setParamNormalized(Membrum::kUiModeId, 1.0);
    source->setParamNormalized(padId(kPadB, Membrum::kPadMorphStart), 0.33);
    source->setParamNormalized(padId(kPadB, Membrum::kPadMorphEnd), 0.66);
    source->setParamNormalized(padId(kStatePad, Membrum::kPadMorphStart), 0.44);
    source->setParamNormalized(padId(kStatePad, Membrum::kPadMorphEnd), 0.77);
    source->setParamNormalized(padId(kPresetPad, Membrum::kPadMorphStart), 0.55);
    source->setParamNormalized(padId(kPresetPad, Membrum::kPadMorphEnd), 0.25);
    source->setParamNormalized(Membrum::kSelectedPadId, padNorm(kPresetPad));
    Steinberg::IBStream* padPreset = source->padPresetStateProvider();
    REQUIRE(padPreset != nullptr);
    source->setParamNormalized(Membrum::kSelectedPadId, padNorm(kStatePad));
    Steinberg::IBStream* kit = source->kitPresetStateProvider();
    REQUIRE(kit != nullptr);
    {
        AdvancedEditor editor(*controller);
        auto* pad = editor.pad();
        REQUIRE(pad != nullptr);

        controller->setParamNormalized(padId(kPadA, Membrum::kPadMorphStart), 0.2);
        controller->setParamNormalized(padId(kPadA, Membrum::kPadMorphEnd), 0.8);
        controller->setParamNormalized(padId(kPadB, Membrum::kPadMorphStart), 0.9);
        controller->setParamNormalized(padId(kPadB, Membrum::kPadMorphEnd), 0.1);

        controller->setParamNormalized(Membrum::kSelectedPadId, padNorm(kPadA));
        CHECK(pad->getMorphX() == Approx(0.2).margin(1e-6));
        CHECK(pad->getMorphY() == Approx(0.8).margin(1e-6));

        controller->setParamNormalized(Membrum::kSelectedPadId, padNorm(kPadB));
        CHECK(pad->getMorphX() == Approx(0.9).margin(1e-6));
        CHECK(pad->getMorphY() == Approx(0.1).margin(1e-6));

        // Automation of the proxy
        controller->setParamNormalized(Membrum::kMorphStartId, 0.4);
        CHECK(pad->getMorphX() == Approx(0.4).margin(1e-6));
        CHECK(pad->getMorphY() == Approx(0.1).margin(1e-6));

        // A kit preset load (kit browser): the kit keeps Extended mode, so the pad lives on, and the selected
        // pad (B) stays selected (FR-052) with the kit's morph values.
        kit->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        REQUIRE(controller->kitPresetLoadProvider(kit));
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        REQUIRE(editor.pad() == pad);
        CHECK(pad->getMorphX() == Approx(0.33).margin(1e-6));
        CHECK(pad->getMorphY() == Approx(0.66).margin(1e-6));

        // A pad preset load onto the selected pad.
        padPreset->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        REQUIRE(controller->padPresetLoadProvider(padPreset));
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        REQUIRE(editor.pad() == pad);
        CHECK(pad->getMorphX() == Approx(0.55).margin(1e-6));
        CHECK(pad->getMorphY() == Approx(0.25).margin(1e-6));

        // A host state load selecting pad 5. setComponentState resets the UI mode to Acoustic (session scope),
        // which removes the Extended panel and the pad with it; back in Extended mode the rebuilt pad shows
        // the loaded pad's values.
        kit->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        REQUIRE(controller->setComponentState(kit) == Steinberg::kResultOk);
        controller->setParamNormalized(Membrum::kUiModeId, 1.0);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        pad = editor.pad();
        REQUIRE(pad != nullptr);
        CHECK(pad->getMorphX() == Approx(0.44).margin(1e-6));
        CHECK(pad->getMorphY() == Approx(0.77).margin(1e-6));
    }
    kit->release();
    padPreset->release();
    REQUIRE(source->terminate() == Steinberg::kResultOk);
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

TEST_CASE("XYMorphPad: a wheel nudge after a pad switch edits from the new pad's values", "[ui][morph]")
{
    Steinberg::UpdateHandler::instance();
    auto controller = Steinberg::owned(new Membrum::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    Membrum::TestSupport::RecordingComponentHandler handler;
    REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultOk);
    {
        AdvancedEditor editor(*controller);
        auto* pad = editor.pad();
        REQUIRE(pad != nullptr);

        constexpr int kPadA = 1;
        constexpr int kPadB = 2;
        controller->setParamNormalized(padId(kPadA, Membrum::kPadMorphStart), 0.2);
        controller->setParamNormalized(padId(kPadA, Membrum::kPadMorphEnd), 0.8);
        controller->setParamNormalized(padId(kPadB, Membrum::kPadMorphStart), 0.9);
        controller->setParamNormalized(padId(kPadB, Membrum::kPadMorphEnd), 0.1);
        controller->setParamNormalized(Membrum::kSelectedPadId, padNorm(kPadA));
        controller->setParamNormalized(Membrum::kSelectedPadId, padNorm(kPadB));

        // One horizontal wheel step: X moves by XYMorphPad::kScrollSensitivity.
        VSTGUI::MouseWheelEvent wheel;
        wheel.mousePosition = pixelOf(*pad, 0.5f, 0.5f);
        wheel.deltaX = 1.0;
        pad->onMouseWheelEvent(wheel);

        const double expectedX = 0.9 + static_cast<double>(Krate::Plugins::XYMorphPad::kScrollSensitivity);
        CHECK(pad->getMorphX() == Approx(expectedX).margin(1e-6));
        CHECK(pad->getMorphY() == Approx(0.1).margin(1e-6));
        CHECK(controller->getParamNormalized(padId(kPadB, Membrum::kPadMorphStart))
              == Approx(expectedX).margin(1e-6));
        CHECK(controller->getParamNormalized(padId(kPadB, Membrum::kPadMorphEnd))
              == Approx(0.1).margin(1e-6));
        // Pad A is untouched.
        CHECK(controller->getParamNormalized(padId(kPadA, Membrum::kPadMorphStart))
              == Approx(0.2).margin(1e-6));
        CHECK(controller->getParamNormalized(padId(kPadA, Membrum::kPadMorphEnd))
              == Approx(0.8).margin(1e-6));
    }
    controller->setComponentHandler(nullptr);
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}
