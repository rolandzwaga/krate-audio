// ==============================================================================
// Context gate -- the editor dims only the controls the DSP ignores
// ==============================================================================
// Membrum audit MB-082 / MB-085 / MB-104, fix plan T2.19.
//
// The pitch envelope and Material Morph drive every body (drum_voice.h: the
// pitch envelope retunes String through setStringFrequency and every modal body
// through updateModalFundamentalOnBank; refreshBodyForMaterialOnBank re-maps
// every body), so their controls stay live whatever the body.
//
// Context-only controls act in one context only and are dimmed (alpha 0.35,
// mouse off) elsewhere:
//   FM (202) FMImpulse, FB (203) Feedback, Burst (204) NoiseBurst, Press (205)
//   Friction (exciter_bank.h forwards each only to its own exciter);
//   Air (302) and Tens (308) Membrane only;
//   Shell (304) only with 2nd (305) on; 2Sz / 2Mat (306 / 307) only with 2nd on
//   AND Shell > 0 (drum_voice.h: secondaryEnabled_ >= 0.5 && couplingStrength_ > 0);
//   noise Cutf / Res / Dec / Color (291-294) only at noise mix (290) > 0;
//   Wire (326) only at noise mix > 0 and on a modal body (not String).
//
// The decision is read from the selected-pad proxies through
// Controller::isContextControlLiveForTest; the editor case checks the open
// editor's views.
// ==============================================================================

#include "controller/controller.h"
#include "dsp/body_model_type.h"
#include "dsp/exciter_type.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "ui/arc_knob.h"
#include "ui/pitch_envelope_display.h"
#include "ui/toggle_button.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/plugin-bindings/vst3editor.h>
#include <vstgui/uidescription/uiviewswitchcontainer.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

using namespace Membrum;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;

/// StringListParameter normalizes idx -> idx / stepCount, where stepCount =
/// count - 1. The processor's clamp later maps back via int(norm * count).
constexpr double bodyNormFor(BodyModelType body) noexcept
{
    // 6 entries -> stepCount = 5. idx N -> norm = N / 5.0.
    return static_cast<double>(static_cast<int>(body))
         / static_cast<double>(static_cast<int>(BodyModelType::kCount) - 1);
}

/// 7 exciters -> stepCount = 6. idx N -> norm = N / 6.0.
constexpr double exciterNormFor(ExciterType exciter) noexcept
{
    return static_cast<double>(static_cast<int>(exciter))
         / static_cast<double>(static_cast<int>(ExciterType::kCount) - 1);
}

constexpr std::array kAllBodies{
    BodyModelType::Membrane, BodyModelType::Plate, BodyModelType::Shell,
    BodyModelType::String,   BodyModelType::Bell,  BodyModelType::NoiseBody};

constexpr std::array kAllExciters{
    ExciterType::Impulse,   ExciterType::Mallet,   ExciterType::NoiseBurst,
    ExciterType::Friction,  ExciterType::FMImpulse, ExciterType::Feedback,
    ExciterType::Clap};

/// The exciter-secondary knob (202-205) that acts on @p exciter, or 0 when none.
constexpr ParamID liveExciterKnobFor(ExciterType exciter) noexcept
{
    switch (exciter)
    {
    case ExciterType::FMImpulse:  return kExciterFMRatioId;
    case ExciterType::Feedback:   return kExciterFeedbackAmountId;
    case ExciterType::NoiseBurst: return kExciterNoiseBurstDurationId;
    case ExciterType::Friction:   return kExciterFrictionPressureId;
    default:                      return 0;
    }
}

/// The ArcKnob or other CControl carrying @p tag in the open editor, or nullptr.
VSTGUI::CControl* findControl(VSTGUI::CViewContainer* root, ParamID tag)
{
    return Shots::findFirst<VSTGUI::CControl>(
        root, [tag](VSTGUI::CControl* c) {
            return c->getTag() == static_cast<int32_t>(tag);
        });
}

/// Switches the selected-pad panel through the UiMode parameter; the switch
/// container rebuilds its template, so view pointers into it go stale.
void setMode(Controller& controller, VSTGUI::CViewContainer* root, int mode)
{
    controller.setParamNormalized(kUiModeId, static_cast<double>(mode));
    UpdateHandler::instance()->triggerDeferedUpdates();
    auto* panel = Shots::findFirst<VSTGUI::UIViewSwitchContainer>(root);
    REQUIRE(panel != nullptr);
    REQUIRE(panel->getCurrentViewIndex() == mode);
}

} // namespace

TEST_CASE("Context gate: pitch envelope and morph are live on every body",
          "[pitch_env_gate][context_gate]")
{
    Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    for (const BodyModelType body : kAllBodies)
    {
        INFO("body " << static_cast<int>(body));
        controller.setParamNormalized(kBodyModelId, bodyNormFor(body));
        CHECK(controller.isContextControlLiveForTest(kToneShaperPitchEnvStartId));
        CHECK(controller.isContextControlLiveForTest(kToneShaperPitchEnvEndId));
        CHECK(controller.isContextControlLiveForTest(kToneShaperPitchEnvTimeId));
        CHECK(controller.isContextControlLiveForTest(kMorphEnabledId));
    }

    controller.terminate();
}

TEST_CASE("Context gate: exciter, body, shell and noise controls are live only where the DSP reads them",
          "[context_gate]")
{
    Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    SECTION("exactly the matching exciter knob is live")
    {
        constexpr std::array kExciterKnobs{
            kExciterFMRatioId, kExciterFeedbackAmountId,
            kExciterNoiseBurstDurationId, kExciterFrictionPressureId};
        for (const ExciterType exciter : kAllExciters)
        {
            INFO("exciter " << static_cast<int>(exciter));
            controller.setParamNormalized(kExciterTypeId, exciterNormFor(exciter));
            const ParamID live = liveExciterKnobFor(exciter);
            int liveCount = 0;
            for (const auto knob : kExciterKnobs)
            {
                INFO("knob " << static_cast<int>(knob));
                const bool isLive = controller.isContextControlLiveForTest(
                    static_cast<ParamID>(knob));
                CHECK(isLive == (static_cast<ParamID>(knob) == live));
                liveCount += isLive ? 1 : 0;
            }
            CHECK(liveCount == (live != 0 ? 1 : 0));
        }
    }

    SECTION("Air and Tension are live only on Membrane")
    {
        for (const BodyModelType body : kAllBodies)
        {
            INFO("body " << static_cast<int>(body));
            controller.setParamNormalized(kBodyModelId, bodyNormFor(body));
            const bool membrane = body == BodyModelType::Membrane;
            CHECK(controller.isContextControlLiveForTest(kAirLoadingId) == membrane);
            CHECK(controller.isContextControlLiveForTest(kTensionModAmtId) == membrane);
        }
    }

    SECTION("Shell needs 2nd on; 2Sz and 2Mat need 2nd on and Shell > 0")
    {
        struct Row { double secondary; double shell; bool shellLive; bool sizeLive; };
        constexpr std::array kRows{
            Row{.secondary = 0.0, .shell = 0.0, .shellLive = false, .sizeLive = false},
            Row{.secondary = 0.0, .shell = 0.7, .shellLive = false, .sizeLive = false},
            Row{.secondary = 1.0, .shell = 0.0, .shellLive = true,  .sizeLive = false},
            Row{.secondary = 1.0, .shell = 0.7, .shellLive = true,  .sizeLive = true},
        };
        for (const Row& row : kRows)
        {
            INFO("2nd " << row.secondary << ", Shell " << row.shell);
            controller.setParamNormalized(kSecondaryEnabledId, row.secondary);
            controller.setParamNormalized(kCouplingStrengthId, row.shell);
            CHECK(controller.isContextControlLiveForTest(kCouplingStrengthId) == row.shellLive);
            CHECK(controller.isContextControlLiveForTest(kSecondarySizeId) == row.sizeLive);
            CHECK(controller.isContextControlLiveForTest(kSecondaryMaterialId) == row.sizeLive);
        }
    }

    SECTION("noise-layer knobs need noise mix > 0; Wire also needs a modal body")
    {
        constexpr std::array kNoiseKnobs{
            kNoiseLayerCutoffId, kNoiseLayerResonanceId,
            kNoiseLayerDecayId, kNoiseLayerColorId};
        for (const double mix : {0.0, 0.4})
        {
            for (const BodyModelType body : kAllBodies)
            {
                INFO("noise mix " << mix << ", body " << static_cast<int>(body));
                controller.setParamNormalized(kNoiseLayerMixId, mix);
                controller.setParamNormalized(kBodyModelId, bodyNormFor(body));
                for (const auto knob : kNoiseKnobs)
                    CHECK(controller.isContextControlLiveForTest(
                              static_cast<ParamID>(knob)) == (mix > 0.0));
                CHECK(controller.isContextControlLiveForTest(kWireCouplingId)
                      == (mix > 0.0 && body != BodyModelType::String));
            }
        }
    }

    SECTION("controls outside the table stay live")
    {
        controller.setParamNormalized(kExciterTypeId, exciterNormFor(ExciterType::Impulse));
        controller.setParamNormalized(kBodyModelId, bodyNormFor(BodyModelType::String));
        controller.setParamNormalized(kNoiseLayerMixId, 0.0);
        controller.setParamNormalized(kSecondaryEnabledId, 0.0);
        CHECK(controller.isContextControlLiveForTest(kNoiseLayerMixId));
        CHECK(controller.isContextControlLiveForTest(kSecondaryEnabledId));
        CHECK(controller.isContextControlLiveForTest(kExciterTypeId));
        CHECK(controller.isContextControlLiveForTest(kBodyModelId));
        CHECK(controller.isContextControlLiveForTest(kMorphEnabledId));
    }

    controller.terminate();
}

TEST_CASE("Context gate: the open editor dims the dead knobs and shows the morph toggle on Plate",
          "[context_gate][editor]")
{
    UpdateHandler::instance();
    auto controller = Steinberg::owned(new Controller());
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
        // The mode swap fades over 120 ms once attached; no timer runs here.
        auto* panel = Shots::findFirst<VSTGUI::UIViewSwitchContainer>(root);
        REQUIRE(panel != nullptr);
        panel->setAnimationTime(0);
        setMode(*controller, root, 1);

        controller->setParamNormalized(kExciterTypeId, exciterNormFor(ExciterType::Impulse));
        controller->setParamNormalized(kBodyModelId, bodyNormFor(BodyModelType::Plate));

        auto* burst = findControl(root, kExciterNoiseBurstDurationId);
        REQUIRE(burst != nullptr);
        REQUIRE(dynamic_cast<Krate::Plugins::ArcKnob*>(burst) != nullptr);
        CHECK(burst->getAlphaValue() == 0.35f);
        CHECK(burst->getMouseEnabled() == false);

        controller->setParamNormalized(kExciterTypeId, exciterNormFor(ExciterType::NoiseBurst));
        CHECK(burst->getAlphaValue() == 1.0f);
        CHECK(burst->getMouseEnabled() == true);

        auto* pitchEnv = Shots::findFirst<Krate::Plugins::PitchEnvelopeDisplay>(root);
        REQUIRE(pitchEnv != nullptr);
        CHECK(pitchEnv->getAlphaValue() == 1.0f);
        CHECK(pitchEnv->getMouseEnabled() == true);

        auto* morphToggle = findControl(root, kMorphEnabledId);
        REQUIRE(morphToggle != nullptr);
        REQUIRE(dynamic_cast<Krate::Plugins::ToggleButton*>(morphToggle) != nullptr);
        CHECK(morphToggle->isVisible());

        // A freshly built Extended panel ends in the right state once the
        // editor's parameter listeners have bound its controls (they re-enable
        // the mouse) and the poll timer's next tick has run.
        controller->setParamNormalized(kExciterTypeId, exciterNormFor(ExciterType::Impulse));
        setMode(*controller, root, 0);
        setMode(*controller, root, 1);
        controller->drainPendingViewRefreshForTest();
        auto* rebuiltBurst = findControl(root, kExciterNoiseBurstDurationId);
        REQUIRE(rebuiltBurst != nullptr);
        CHECK(rebuiltBurst->getAlphaValue() == 0.35f);
        CHECK(rebuiltBurst->getMouseEnabled() == false);

        // A pad switch that moves the dead knob's own value keeps it dimmed.
        controller->setParamNormalized(
            static_cast<ParamID>(padParamId(3, kPadExciterType)),
            exciterNormFor(ExciterType::Mallet));
        controller->setParamNormalized(
            static_cast<ParamID>(padParamId(3, kPadNoiseBurstDuration)), 0.8);
        controller->setParamNormalized(kSelectedPadId, 3.0 / 31.0);
        controller->drainPendingViewRefreshForTest();
        CHECK(rebuiltBurst->getAlphaValue() == 0.35f);
        CHECK(rebuiltBurst->getMouseEnabled() == false);

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == kResultOk);
}
