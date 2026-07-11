// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "dsp/harmonic_snapshot_json.h"
#include "parameters/innexus_params.h"
#include "parameters/note_value_ui.h"
#include "plugin_ids.h"
#include "preset/innexus_preset_config.h"
#include "update/innexus_update_config.h"
#include "version.h"

#include "controller/views/harmonic_display_view.h"
#include "controller/views/confidence_indicator_view.h"
#include "controller/views/memory_slot_status_view.h"
#include "controller/views/evolution_position_view.h"
#include "controller/views/modulator_activity_view.h"
#include "controller/modulator_sub_controller.h"
#include "controller/adsr_expanded_overlay.h"
#include "controller/sample_drop_target.h"
#include "ui/adsr_display.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/update_banner_view.h"
#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

namespace Innexus {

// ==============================================================================
// Controller: init, VST3 boilerplate, view creation (incl. local CView helpers), editor lifecycle, messaging, note expression
// ==============================================================================

// ==============================================================================
// SampleLoadButton — custom CView button (no CControl / no parameter tag)
// ==============================================================================
// Follows the same pattern as Ruinae's PresetBrowserButton: a CView that
// handles onMouseDown directly, avoiding VSTGUI's parameter binding machinery
// which crashes when a CControl has a tag that doesn't map to a VST3 parameter.
class SampleLoadButton : public VSTGUI::CView {
public:
    SampleLoadButton(const VSTGUI::CRect& size, Controller* controller)
        : CView(size), controller_(controller) {}

    void draw(VSTGUI::CDrawContext* context) override
    {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        auto r = getViewSize();
        r.inset(0.5, 0.5);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path)
        {
            constexpr double kRadius = 3.0;
            path->addRoundRect(r, kRadius);

            if (hovered_)
            {
                context->setFillColor(VSTGUI::CColor(255, 255, 255, 20));
                context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
            }

            context->setFrameColor(VSTGUI::CColor(64, 64, 72));
            context->setLineWidth(1.0);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>(*VSTGUI::kNormalFontSmaller);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(192, 192, 192));
        context->drawString(VSTGUI::UTF8String("Load"), getViewSize(), VSTGUI::kCenterText);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseEntered(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override
    {
        hovered_ = true;
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorHand);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override
    {
        hovered_ = false;
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorDefault);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& buttons) override
    {
        if (buttons.isLeftButton())
        {
            openFileSelector();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

private:
    void openFileSelector()
    {
        auto* frame = getFrame();
        if (!frame || !controller_)
            return;

        auto selector = VSTGUI::owned(
            VSTGUI::CNewFileSelector::create(
                frame, VSTGUI::CNewFileSelector::kSelectFile));
        if (!selector)
            return;

        selector->setTitle("Load Sample");
        VSTGUI::CFileExtension wavExt("WAV Audio", "wav");
        VSTGUI::CFileExtension aiffExt("AIFF Audio", "aiff");
        VSTGUI::CFileExtension aifExt("AIF Audio", "aif");
        selector->addFileExtension(wavExt);
        selector->addFileExtension(aiffExt);
        selector->addFileExtension(aifExt);
        selector->setDefaultExtension(wavExt);

        auto* ctrl = controller_;
        selector->run([ctrl](VSTGUI::CNewFileSelector* sel) {
            if (sel->getNumSelectedFiles() > 0)
            {
                std::string fullPath(sel->getSelectedFile(0));
                ctrl->onSampleFileSelected(fullPath);
            }
        });
    }

    Controller* controller_;
    bool hovered_ = false;
};

// ==============================================================================
// ADSRExpandButton — CView button that opens the expanded ADSR overlay
// ==============================================================================
class ADSRExpandButton : public VSTGUI::CView {
public:
    ADSRExpandButton(const VSTGUI::CRect& size, Controller* controller)
        : CView(size), controller_(controller) {}

    void draw(VSTGUI::CDrawContext* context) override
    {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        auto r = getViewSize();
        r.inset(0.5, 0.5);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path) {
            constexpr double kRadius = 3.0;
            path->addRoundRect(r, kRadius);

            if (hovered_) {
                context->setFillColor(VSTGUI::CColor(255, 255, 255, 20));
                context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
            }

            context->setFrameColor(VSTGUI::CColor(85, 85, 102));
            context->setLineWidth(1.0);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }

        // Draw expand arrows icon (four outward arrows)
        auto center = r.getCenter();
        auto iconFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
        context->setFont(iconFont);
        context->setFontColor(VSTGUI::CColor(192, 192, 192));
        context->drawString(VSTGUI::UTF8String("Expand"), getViewSize(),
                            VSTGUI::kCenterText);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseEntered(
        VSTGUI::CPoint&, const VSTGUI::CButtonState&) override
    {
        hovered_ = true;
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorHand);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint&, const VSTGUI::CButtonState&) override
    {
        hovered_ = false;
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorDefault);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint&, const VSTGUI::CButtonState& buttons) override
    {
        if (buttons.isLeftButton() && controller_) {
            controller_->openAdsrExpandedOverlay();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

private:
    Controller* controller_;
    bool hovered_ = false;
};

// ==============================================================================
// Initialize
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::initialize(Steinberg::FUnknown* context)
{
    auto result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultOk)
        return result;

    // --- Register parameters ---
    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsBypass,
        kBypassId);

    parameters.addParameter(STR16("Master Gain"), STR16("dB"), 0, 0.8,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMasterGainId);

    // M1 parameters
    registerInnexusParams(parameters);

    // Partial Count (oscillator bank range 200-299)
    auto* partialCountParam = new Steinberg::Vst::StringListParameter(
        STR16("Partial Count"), kPartialCountId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    partialCountParam->appendString(STR16("48"));
    partialCountParam->appendString(STR16("64"));
    partialCountParam->appendString(STR16("80"));
    partialCountParam->appendString(STR16("96"));
    parameters.addParameter(partialCountParam);

    // M2 Residual parameters (FR-021, FR-024)
    auto* harmonicLevelParam = new Steinberg::Vst::RangeParameter(
        STR16("Harmonic Level"), kHarmonicLevelId,
        STR16(""),
        0.0,    // min plain
        2.0,    // max plain
        1.0,    // default plain
        0,      // stepCount (continuous)
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(harmonicLevelParam);

    auto* residualLevelParam = new Steinberg::Vst::RangeParameter(
        STR16("Residual Level"), kResidualLevelId,
        STR16(""),
        0.0,    // min plain
        2.0,    // max plain
        0.3,    // default plain
        0,      // stepCount (continuous)
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(residualLevelParam);

    // M2 Residual shaping parameters (FR-022, FR-023)
    auto* brightnessParam = new Steinberg::Vst::RangeParameter(
        STR16("Residual Brightness"), kResidualBrightnessId,
        STR16("%"),
        -1.0,   // min plain
        1.0,    // max plain
        0.0,    // default plain (neutral)
        0,      // stepCount (continuous)
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(brightnessParam);

    auto* transientParam = new Steinberg::Vst::RangeParameter(
        STR16("Transient Emphasis"), kTransientEmphasisId,
        STR16("%"),
        0.0,    // min plain
        2.0,    // max plain
        0.0,    // default plain (no boost)
        0,      // stepCount (continuous)
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(transientParam);

    // M3 Sidechain parameters (FR-002, FR-004)
    auto* inputSourceParam = new Steinberg::Vst::StringListParameter(
        STR16("Input Source"), kInputSourceId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    inputSourceParam->appendString(STR16("Sample"));
    inputSourceParam->appendString(STR16("Sidechain"));
    parameters.addParameter(inputSourceParam);

    auto* latencyModeParam = new Steinberg::Vst::StringListParameter(
        STR16("Latency Mode"), kLatencyModeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    latencyModeParam->appendString(STR16("Low Latency"));
    latencyModeParam->appendString(STR16("High Precision"));
    parameters.addParameter(latencyModeParam);

    // M4 Musical Control parameters (FR-001, FR-010, FR-019, FR-029)
    parameters.addParameter(STR16("Freeze"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kFreezeId);

    auto* morphParam = new Steinberg::Vst::RangeParameter(
        STR16("Morph Position"), kMorphPositionId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(morphParam);

    auto* filterParam = new Steinberg::Vst::StringListParameter(
        STR16("Harmonic Filter"), kHarmonicFilterTypeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    filterParam->appendString(STR16("All-Pass"));
    filterParam->appendString(STR16("Odd Only"));
    filterParam->appendString(STR16("Even Only"));
    filterParam->appendString(STR16("Low Harmonics"));
    filterParam->appendString(STR16("High Harmonics"));
    parameters.addParameter(filterParam);

    auto* respParam = new Steinberg::Vst::RangeParameter(
        STR16("Responsiveness"), kResponsivenessId,
        STR16("%"), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(respParam);

    // M5 Harmonic Memory parameters (FR-005, FR-006, FR-011)
    auto* memorySlotParam = new Steinberg::Vst::StringListParameter(
        STR16("Memory Slot"), kMemorySlotId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    memorySlotParam->appendString(STR16("Slot 1"));
    memorySlotParam->appendString(STR16("Slot 2"));
    memorySlotParam->appendString(STR16("Slot 3"));
    memorySlotParam->appendString(STR16("Slot 4"));
    memorySlotParam->appendString(STR16("Slot 5"));
    memorySlotParam->appendString(STR16("Slot 6"));
    memorySlotParam->appendString(STR16("Slot 7"));
    memorySlotParam->appendString(STR16("Slot 8"));
    parameters.addParameter(memorySlotParam);

    parameters.addParameter(STR16("Memory Capture"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMemoryCaptureId);

    parameters.addParameter(STR16("Memory Recall"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMemoryRecallId);

    // ==================================================================
    // M6 Creative Extensions parameters (FR-043)
    // ==================================================================

    // Stereo Spread (FR-006)
    auto* stereoSpreadParam = new Steinberg::Vst::RangeParameter(
        STR16("Stereo Spread"), kStereoSpreadId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(stereoSpreadParam);

    // Evolution Engine (FR-014 to FR-017)
    parameters.addParameter(STR16("Evolution Enable"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kEvolutionEnableId);

    auto* evoSpeedParam = new Steinberg::Vst::RangeParameter(
        STR16("Evolution Speed"), kEvolutionSpeedId,
        STR16("Hz"), 0.01, 10.0, 0.1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(evoSpeedParam);

    auto* evoDepthParam = new Steinberg::Vst::RangeParameter(
        STR16("Evolution Depth"), kEvolutionDepthId,
        STR16("%"), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(evoDepthParam);

    auto* evoModeParam = new Steinberg::Vst::StringListParameter(
        STR16("Evolution Mode"), kEvolutionModeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    evoModeParam->appendString(STR16("Cycle"));
    evoModeParam->appendString(STR16("PingPong"));
    evoModeParam->appendString(STR16("Random Walk"));
    parameters.addParameter(evoModeParam);

    // Modulator 1 (FR-024)
    parameters.addParameter(STR16("Mod 1 Enable"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMod1EnableId);

    auto* mod1WaveParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 1 Waveform"), kMod1WaveformId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod1WaveParam->appendString(STR16("Sine"));
    mod1WaveParam->appendString(STR16("Triangle"));
    mod1WaveParam->appendString(STR16("Square"));
    mod1WaveParam->appendString(STR16("Saw"));
    mod1WaveParam->appendString(STR16("Random S&H"));
    parameters.addParameter(mod1WaveParam);

    auto* mod1RateParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Rate"), kMod1RateId,
        STR16("Hz"), 0.01, 20.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1RateParam);

    auto* mod1DepthParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Depth"), kMod1DepthId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1DepthParam);

    auto* mod1RangeStartParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Range Start"), kMod1RangeStartId,
        STR16(""), 1.0, 96.0, 1.0, 95,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1RangeStartParam);

    auto* mod1RangeEndParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Range End"), kMod1RangeEndId,
        STR16(""), 1.0, 96.0, 96.0, 95,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1RangeEndParam);

    auto* mod1TargetParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 1 Target"), kMod1TargetId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod1TargetParam->appendString(STR16("Amplitude"));
    mod1TargetParam->appendString(STR16("Frequency"));
    mod1TargetParam->appendString(STR16("Pan"));
    parameters.addParameter(mod1TargetParam);

    // Mod 1 Rate Sync (tempo sync toggle)
    parameters.addParameter(STR16("Mod 1 Rate Sync"), nullptr, 1, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMod1RateSyncId);

    // Mod 1 Note Value (tempo sync note value dropdown, default index 10 = 1/8)
    auto* mod1NoteValParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 1 Note Value"), kMod1NoteValueId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    for (const auto* noteStr : Parameters::kNoteValueDropdownStrings)
        mod1NoteValParam->appendString(noteStr);
    mod1NoteValParam->getInfo().defaultNormalizedValue = 10.0 / 20.0; // 1/8 note
    parameters.addParameter(mod1NoteValParam);

    // Modulator 2 (FR-024)
    parameters.addParameter(STR16("Mod 2 Enable"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMod2EnableId);

    auto* mod2WaveParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 2 Waveform"), kMod2WaveformId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod2WaveParam->appendString(STR16("Sine"));
    mod2WaveParam->appendString(STR16("Triangle"));
    mod2WaveParam->appendString(STR16("Square"));
    mod2WaveParam->appendString(STR16("Saw"));
    mod2WaveParam->appendString(STR16("Random S&H"));
    parameters.addParameter(mod2WaveParam);

    auto* mod2RateParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Rate"), kMod2RateId,
        STR16("Hz"), 0.01, 20.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2RateParam);

    auto* mod2DepthParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Depth"), kMod2DepthId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2DepthParam);

    auto* mod2RangeStartParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Range Start"), kMod2RangeStartId,
        STR16(""), 1.0, 96.0, 1.0, 95,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2RangeStartParam);

    auto* mod2RangeEndParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Range End"), kMod2RangeEndId,
        STR16(""), 1.0, 96.0, 96.0, 95,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2RangeEndParam);

    auto* mod2TargetParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 2 Target"), kMod2TargetId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod2TargetParam->appendString(STR16("Amplitude"));
    mod2TargetParam->appendString(STR16("Frequency"));
    mod2TargetParam->appendString(STR16("Pan"));
    parameters.addParameter(mod2TargetParam);

    // Mod 2 Rate Sync (tempo sync toggle)
    parameters.addParameter(STR16("Mod 2 Rate Sync"), nullptr, 1, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMod2RateSyncId);

    // Mod 2 Note Value (tempo sync note value dropdown, default index 10 = 1/8)
    auto* mod2NoteValParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 2 Note Value"), kMod2NoteValueId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    for (const auto* noteStr : Parameters::kNoteValueDropdownStrings)
        mod2NoteValParam->appendString(noteStr);
    mod2NoteValParam->getInfo().defaultNormalizedValue = 10.0 / 20.0; // 1/8 note
    parameters.addParameter(mod2NoteValParam);

    // Detune Spread (FR-030)
    auto* detuneSpreadParam = new Steinberg::Vst::RangeParameter(
        STR16("Detune Spread"), kDetuneSpreadId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(detuneSpreadParam);

    // Multi-Source Blend (FR-034 to FR-036)
    parameters.addParameter(STR16("Blend Enable"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kBlendEnableId);

    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 1 Weight"), kBlendSlotWeight1Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 2 Weight"), kBlendSlotWeight2Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 3 Weight"), kBlendSlotWeight3Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 4 Weight"), kBlendSlotWeight4Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 5 Weight"), kBlendSlotWeight5Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 6 Weight"), kBlendSlotWeight6Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 7 Weight"), kBlendSlotWeight7Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 8 Weight"), kBlendSlotWeight8Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));

    auto* blendLiveWeightParam = new Steinberg::Vst::RangeParameter(
        STR16("Blend Live Weight"), kBlendLiveWeightId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(blendLiveWeightParam);

    // Harmonic Physics (Spec A)
    auto* warmthParam = new Steinberg::Vst::RangeParameter(
        STR16("Warmth"), kWarmthId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(warmthParam);

    auto* couplingParam = new Steinberg::Vst::RangeParameter(
        STR16("Coupling"), kCouplingId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(couplingParam);

    auto* stabilityParam = new Steinberg::Vst::RangeParameter(
        STR16("Stability"), kStabilityId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(stabilityParam);

    auto* entropyParam = new Steinberg::Vst::RangeParameter(
        STR16("Entropy"), kEntropyId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(entropyParam);

    // Analysis Feedback Loop (Spec B)
    auto* feedbackAmountParam = new Steinberg::Vst::RangeParameter(
        STR16("Feedback Amount"), kAnalysisFeedbackId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(feedbackAmountParam);

    auto* feedbackDecayParam = new Steinberg::Vst::RangeParameter(
        STR16("Feedback Decay"), kAnalysisFeedbackDecayId,
        STR16("%"), 0.0, 1.0, 0.2, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(feedbackDecayParam);

    // ==================================================================
    // ADSR Envelope parameters (Spec 124: 124-adsr-envelope-detection)
    // ==================================================================
    // Attack/Decay/Release: RangeParameter 1-5000ms (processor applies log mapping)
    // Sustain/Amount: linear 0-1
    // TimeScale: linear 0.25-4.0
    // Curve amounts: linear -1.0 to +1.0

    auto* adsrAttackParam = new Steinberg::Vst::RangeParameter(
        STR16("ADSR Attack"), kAdsrAttackId,
        STR16("ms"), 1.0, 5000.0, 10.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrAttackParam);

    auto* adsrDecayParam = new Steinberg::Vst::RangeParameter(
        STR16("ADSR Decay"), kAdsrDecayId,
        STR16("ms"), 1.0, 5000.0, 100.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrDecayParam);

    auto* adsrSustainParam = new Steinberg::Vst::RangeParameter(
        STR16("ADSR Sustain"), kAdsrSustainId,
        STR16(""), 0.0, 1.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrSustainParam);

    auto* adsrReleaseParam = new Steinberg::Vst::RangeParameter(
        STR16("ADSR Release"), kAdsrReleaseId,
        STR16("ms"), 1.0, 5000.0, 100.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrReleaseParam);

    auto* adsrAmountParam = new Steinberg::Vst::RangeParameter(
        STR16("Envelope Amount"), kAdsrAmountId,
        STR16(""), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrAmountParam);

    auto* adsrTimeScaleParam = new Steinberg::Vst::RangeParameter(
        STR16("ADSR Time Scale"), kAdsrTimeScaleId,
        STR16("x"), 0.25, 4.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrTimeScaleParam);

    auto* adsrAttackCurveParam = new Steinberg::Vst::RangeParameter(
        STR16("ADSR Attack Curve"), kAdsrAttackCurveId,
        STR16(""), -1.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrAttackCurveParam);

    auto* adsrDecayCurveParam = new Steinberg::Vst::RangeParameter(
        STR16("ADSR Decay Curve"), kAdsrDecayCurveId,
        STR16(""), -1.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrDecayCurveParam);

    auto* adsrReleaseCurveParam = new Steinberg::Vst::RangeParameter(
        STR16("ADSR Release Curve"), kAdsrReleaseCurveId,
        STR16(""), -1.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(adsrReleaseCurveParam);

    // Voice Mode (MPE polyphony)
    auto* voiceModeParam = new Steinberg::Vst::StringListParameter(
        STR16("Voice Mode"), kVoiceModeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    voiceModeParam->appendString(STR16("Mono"));
    voiceModeParam->appendString(STR16("4 Voices"));
    voiceModeParam->appendString(STR16("8 Voices"));
    parameters.addParameter(voiceModeParam);

    // Physical Modelling (Spec 127)
    auto* physModelMixParam = new Steinberg::Vst::RangeParameter(
        STR16("Physical Model Mix"), kPhysModelMixId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(physModelMixParam);

    // Log mapping: plain = 0.01 * 500^norm, so norm = log(plain/0.01) / log(500)
    // For default 0.5s: norm = log(50) / log(500) = ~0.6295
    const double kDecayDefaultNorm = std::log(50.0) / std::log(500.0);
    auto* resonanceDecayParam = new Steinberg::Vst::RangeParameter(
        STR16("Resonance Decay"), kResonanceDecayId,
        STR16("s"), 0.0, 1.0, kDecayDefaultNorm, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(resonanceDecayParam);

    auto* resonanceBrightnessParam = new Steinberg::Vst::RangeParameter(
        STR16("Resonance Brightness"), kResonanceBrightnessId,
        STR16("%"), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(resonanceBrightnessParam);

    auto* resonanceStretchParam = new Steinberg::Vst::RangeParameter(
        STR16("Resonance Stretch"), kResonanceStretchId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(resonanceStretchParam);

    auto* resonanceScatterParam = new Steinberg::Vst::RangeParameter(
        STR16("Resonance Scatter"), kResonanceScatterId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(resonanceScatterParam);

    // Impact Exciter (Spec 128)
    auto* exciterTypeParam = new Steinberg::Vst::StringListParameter(
        STR16("Exciter Type"), kExciterTypeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    exciterTypeParam->appendString(STR16("Residual"));
    exciterTypeParam->appendString(STR16("Impact"));
    exciterTypeParam->appendString(STR16("Bow"));
    parameters.addParameter(exciterTypeParam);

    auto* impactHardnessParam = new Steinberg::Vst::RangeParameter(
        STR16("Impact Hardness"), kImpactHardnessId,
        STR16("%"), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(impactHardnessParam);

    auto* impactMassParam = new Steinberg::Vst::RangeParameter(
        STR16("Impact Mass"), kImpactMassId,
        STR16("%"), 0.0, 1.0, 0.3, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(impactMassParam);

    // Brightness: plain -1.0 to +1.0, normalized 0.0-1.0, default plain 0.0 (norm 0.5)
    auto* impactBrightnessParam = new Steinberg::Vst::RangeParameter(
        STR16("Impact Brightness"), kImpactBrightnessId,
        STR16(""), -1.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(impactBrightnessParam);

    auto* impactPositionParam = new Steinberg::Vst::RangeParameter(
        STR16("Impact Position"), kImpactPositionId,
        STR16(""), 0.0, 1.0, 0.13, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(impactPositionParam);

    // Bow Exciter (Spec 130)
    auto* bowPressureParam = new Steinberg::Vst::RangeParameter(
        STR16("Bow Pressure"), kBowPressureId,
        STR16("%"), 0.0, 1.0, 0.3, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(bowPressureParam);

    auto* bowSpeedParam = new Steinberg::Vst::RangeParameter(
        STR16("Bow Speed"), kBowSpeedId,
        STR16("%"), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(bowSpeedParam);

    auto* bowPositionParam = new Steinberg::Vst::RangeParameter(
        STR16("Bow Position"), kBowPositionId,
        STR16(""), 0.0, 1.0, 0.13, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(bowPositionParam);

    auto* bowOversamplingParam = new Steinberg::Vst::RangeParameter(
        STR16("Bow Oversampling"), kBowOversamplingId,
        STR16(""), 0.0, 1.0, 0.0, 1,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(bowOversamplingParam);

    // Waveguide String Resonance (Spec 129)
    auto* resonanceTypeParam = new Steinberg::Vst::StringListParameter(
        STR16("Resonance Type"), kResonanceTypeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    resonanceTypeParam->appendString(STR16("Modal"));
    resonanceTypeParam->appendString(STR16("Waveguide"));
    parameters.addParameter(resonanceTypeParam);

    auto* waveguideStiffnessParam = new Steinberg::Vst::RangeParameter(
        STR16("Waveguide Stiffness"), kWaveguideStiffnessId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(waveguideStiffnessParam);

    auto* waveguidePickPosParam = new Steinberg::Vst::RangeParameter(
        STR16("Waveguide Pick Position"), kWaveguidePickPositionId,
        STR16(""), 0.0, 1.0, 0.13, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(waveguidePickPosParam);

    // Body Resonance (Spec 131)
    auto* bodySizeParam = new Steinberg::Vst::RangeParameter(
        STR16("Body Size"), kBodySizeId,
        STR16(""), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(bodySizeParam);

    auto* bodyMaterialParam = new Steinberg::Vst::RangeParameter(
        STR16("Material"), kBodyMaterialId,
        STR16(""), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(bodyMaterialParam);

    auto* bodyMixParam = new Steinberg::Vst::RangeParameter(
        STR16("Body Mix"), kBodyMixId,
        STR16(""), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(bodyMixParam);

    // Sympathetic Resonance (Spec 132)
    auto* sympatheticAmountParam = new Steinberg::Vst::RangeParameter(
        STR16("Sympathetic Amount"), kSympatheticAmountId,
        STR16(""), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(sympatheticAmountParam);

    auto* sympatheticDecayParam = new Steinberg::Vst::RangeParameter(
        STR16("Sympathetic Decay"), kSympatheticDecayId,
        STR16(""), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(sympatheticDecayParam);

    // NoteExpression types (Phase 4: MPE support)
    {
        using namespace Steinberg::Vst;
        auto* tuning = new NoteExpressionType(
            NoteExpressionTypeIDs::kTuningTypeID,
            STR16("Tuning"), STR16("Tune"), STR16(""),
            -1, nullptr, // no associated parameter
            NoteExpressionTypeInfo::kIsBipolar);
        tuning->getInfo().valueDesc.minimum = 0.0;
        tuning->getInfo().valueDesc.maximum = 1.0;
        tuning->getInfo().valueDesc.defaultValue = 0.5;
        tuning->getInfo().valueDesc.stepCount = 0;
        noteExpressionTypes_.addNoteExpressionType(tuning);

        auto* volume = new NoteExpressionType(
            NoteExpressionTypeIDs::kVolumeTypeID,
            STR16("Volume"), STR16("Vol"), STR16(""),
            -1, nullptr, 0);
        volume->getInfo().valueDesc.minimum = 0.0;
        volume->getInfo().valueDesc.maximum = 1.0;
        volume->getInfo().valueDesc.defaultValue = 0.25; // maps to 1.0x gain
        volume->getInfo().valueDesc.stepCount = 0;
        noteExpressionTypes_.addNoteExpressionType(volume);

        auto* pan = new NoteExpressionType(
            NoteExpressionTypeIDs::kPanTypeID,
            STR16("Pan"), STR16("Pan"), STR16(""),
            -1, nullptr,
            NoteExpressionTypeInfo::kIsBipolar);
        pan->getInfo().valueDesc.minimum = 0.0;
        pan->getInfo().valueDesc.maximum = 1.0;
        pan->getInfo().valueDesc.defaultValue = 0.5;
        pan->getInfo().valueDesc.stepCount = 0;
        noteExpressionTypes_.addNoteExpressionType(pan);

        auto* brightness = new NoteExpressionType(
            NoteExpressionTypeIDs::kBrightnessTypeID,
            STR16("Brightness"), STR16("Bright"), STR16(""),
            -1, nullptr, 0);
        brightness->getInfo().valueDesc.minimum = 0.0;
        brightness->getInfo().valueDesc.maximum = 1.0;
        brightness->getInfo().valueDesc.defaultValue = 0.5;
        brightness->getInfo().valueDesc.stepCount = 0;
        noteExpressionTypes_.addNoteExpressionType(brightness);
    }

    // Update checker
    updateChecker_ = std::make_unique<Krate::Plugins::UpdateChecker>(makeInnexusUpdateConfig());

    // Preset manager
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeInnexusPresetConfig(), nullptr, this);
    presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return createComponentStateStream();
    });
    presetManager_->setLoadProvider(
        [this](Steinberg::IBStream* stream,
               const Krate::Plugins::PresetInfo& /*info*/) -> bool {
            return loadComponentStateWithNotify(stream);
        });

    return Steinberg::kResultOk;
}

// ==============================================================================
// Terminate
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::terminate()
{
    presetManager_.reset();
    updateChecker_.reset();
    adsrOutputPtr_ = nullptr;
    adsrStagePtr_ = nullptr;
    adsrActivePtr_ = nullptr;
    return EditControllerEx1::terminate();
}

// ==============================================================================
// createView (T012: FR-047)
// ==============================================================================
Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name)
{
    if (std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
    {
        return new VSTGUI::VST3Editor(this, "Editor", "editor.uidesc");
    }
    return nullptr;
}

// ==============================================================================
// createCustomView (T030: FR-047)
// ==============================================================================
VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/)
{
    if (!name)
        return nullptr;

    const std::string viewName(name);

    // Extract size from attributes for the CRect
    VSTGUI::CPoint origin;
    VSTGUI::CPoint size;
    VSTGUI::CRect viewRect;
    if (const auto* o = attributes.getAttributeValue("origin"))
    {
        double x = 0;
        double y = 0;
        if (std::sscanf(o->c_str(), "%lf, %lf", &x, &y) == 2)
            origin = VSTGUI::CPoint(x, y);
    }
    if (const auto* s = attributes.getAttributeValue("size"))
    {
        double w = 0;
        double h = 0;
        if (std::sscanf(s->c_str(), "%lf, %lf", &w, &h) == 2)
            size = VSTGUI::CPoint(w, h);
    }
    viewRect = VSTGUI::CRect(0, 0, size.x, size.y);

    // FR-047: Create custom views by name
    if (viewName == "HarmonicDisplay")
    {
        auto* view = new HarmonicDisplayView(viewRect);
        harmonicDisplayView_ = view;
        return view;
    }
    if (viewName == "ConfidenceIndicator")
    {
        auto* view = new ConfidenceIndicatorView(viewRect);
        confidenceIndicatorView_ = view;
        return view;
    }
    if (viewName == "MemorySlotStatus")
    {
        auto* view = new MemorySlotStatusView(viewRect);
        memorySlotStatusView_ = view;
        return view;
    }
    if (viewName == "EvolutionPosition")
    {
        auto* view = new EvolutionPositionView(viewRect);
        evolutionPositionView_ = view;
        return view;
    }
    if (viewName == "ModulatorActivity")
    {
        auto* view = new ModulatorActivityView(viewRect);
        // Store pointer in the first available slot.
        // Views are created in XML order: Mod1 first, Mod2 second.
        // The sub-controller's verifyView() will set the modIndex on each.
        if (!modActivityView0_)
            modActivityView0_ = view;
        else if (!modActivityView1_)
            modActivityView1_ = view;
        return view;
    }
    if (viewName == "SampleLoadButton")
    {
        return new SampleLoadButton(viewRect, this);
    }
    if (viewName == "SampleLoadContainer")
    {
        auto* container = new VSTGUI::CViewContainer(viewRect);
        container->setTransparency(true);
        sampleLoadContainer_ = container;
        return container;
    }
    if (viewName == "SampleFilenameLabel")
    {
        auto* label = new VSTGUI::CTextLabel(viewRect);
        label->setTransparency(true);
        sampleFilenameLabel_ = label;
        return label;
    }
    if (viewName == "VersionLabel")
    {
        auto* label = new VSTGUI::CTextLabel(viewRect);
        label->setTransparency(true);
        label->setText("Innexus v" VERSION_STR " | Krate Audio");
        return label;
    }
    if (viewName == "PresetBrowserButton")
        return createPresetButton(viewRect, true);
    if (viewName == "SavePresetButton")
        return createPresetButton(viewRect, false);
    if (viewName == "UpdateBanner")
    {
        auto* banner = new Krate::Plugins::UpdateBannerView(viewRect, updateChecker_.get());
        updateBannerView_ = banner;
        return banner;
    }
    if (viewName == "ADSRExpandButton")
        return createAdsrExpandButton(viewRect);
    if (viewName == "ADSRDisplay")
    {
        auto* display = new Krate::Plugins::ADSRDisplay(viewRect, nullptr, -1);
        wireAdsrDisplay(display);
        adsrDisplayView_ = display;
        updateAdsrDisplayFromParams();
        return display;
    }

    return nullptr;
}

// ==============================================================================
// createSubController (T058: FR-046)
// ==============================================================================
VSTGUI::IController* Controller::createSubController(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::IUIDescription* /*description*/,
    [[maybe_unused]] VSTGUI::VST3Editor* editor)
{
    if (!name)
        return nullptr;

    const std::string controllerName(name);

    // FR-046: Modulator sub-controller for tag remapping
    // VSTGUI takes ownership of the returned pointer (raw new is mandated)
    // editor (VST3Editor*) is the IController parent for delegation
    if (controllerName == "ModulatorController")
        return new ModulatorSubController(modInstanceCounter_++, editor);

    return nullptr;
}

// ==============================================================================
// didOpen (T013: FR-049)
// ==============================================================================
void Controller::didOpen(VSTGUI::VST3Editor* editor)
{
    activeEditor_ = editor;

    // Primary counter reset: ensures Mod 1 gets index 0, Mod 2 gets index 1
    modInstanceCounter_ = 0;

    // Create display update timer (30ms = ~33fps, exceeds SC-003 >= 10fps)
    displayTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
        [this](VSTGUI::CVSTGUITimer*) { onDisplayTimerFired(); }, 30);

    // Restore filename display if a sample was previously loaded
    if (!loadedSampleFilename_.empty())
        setSampleFilenameDisplay(loadedSampleFilename_, loadedSampleFullPath_);

    // Set initial visibility of sample load panel
    updateSampleLoadVisibility();

    // Set initial visibility of impact exciter knobs
    updateImpactKnobVisibility();

    // Set initial visibility of bow exciter knobs
    updateBowVisibility();

    // Set initial visibility of feedback section (sidechain only)
    updateFeedbackVisibility();

    // Create preset browser and save dialog overlay views
    if (auto* frame = editor->getFrame())
    {
        auto frameSize = frame->getViewSize();

        if (presetManager_) {
            if (!presetBrowserView_) {
                presetBrowserView_ = new Krate::Plugins::PresetBrowserView(
                    frameSize, presetManager_.get(), getInnexusTabLabels());
                frame->addView(presetBrowserView_);
            }
            if (!savePresetDialogView_) {
                savePresetDialogView_ = new Krate::Plugins::SavePresetDialogView(
                    frameSize, presetManager_.get(),
                    makeInnexusPresetConfig().subcategoryNames);
                frame->addView(savePresetDialogView_);
            }
        }

        // Create ADSR expanded overlay (initially hidden)
        if (!adsrExpandedOverlay_) {
            adsrExpandedOverlay_ = new ADSRExpandedOverlayView(frameSize);
            wireAdsrDisplay(adsrExpandedOverlay_->getDisplay());
            adsrExpandedOverlay_->setCloseCallback([this] { closeAdsrExpandedOverlay(); });
            frame->addView(adsrExpandedOverlay_);
        }

        // Add drag-and-drop overlay to the frame (topmost child, transparent)
        auto* overlay = new SampleDropOverlayView(
            VSTGUI::CRect(0, 0, frameSize.getWidth(), frameSize.getHeight()), this);
        overlay->setTransparency(true);
        overlay->setMouseEnabled(true);
        frame->addView(overlay);
        sampleDropOverlay_ = overlay;
    }

    // Start update banner polling
    if (updateBannerView_)
        updateBannerView_->startPolling();
}

// ==============================================================================
// willClose (T014: timer lifecycle)
// ==============================================================================
void Controller::willClose(VSTGUI::VST3Editor* /*editor*/)
{
    // Stop and release timer
    if (displayTimer_)
    {
        displayTimer_->stop();
        displayTimer_ = nullptr;
    }

    // Stop update banner polling
    if (updateBannerView_)
    {
        updateBannerView_->stopPolling();
        updateBannerView_ = nullptr;
    }

    // Remove drag-and-drop overlay from frame
    if (sampleDropOverlay_)
    {
        if (auto* frame = sampleDropOverlay_->getFrame())
            frame->removeView(sampleDropOverlay_);
        sampleDropOverlay_ = nullptr;
    }

    // Null all custom view pointers (VSTGUI owns the views)
    presetBrowserView_ = nullptr;
    savePresetDialogView_ = nullptr;
    harmonicDisplayView_ = nullptr;
    confidenceIndicatorView_ = nullptr;
    memorySlotStatusView_ = nullptr;
    evolutionPositionView_ = nullptr;
    modActivityView0_ = nullptr;
    modActivityView1_ = nullptr;
    sampleFilenameLabel_ = nullptr;
    sampleLoadContainer_ = nullptr;
    impactKnobContainer_ = nullptr;
    bowKnobContainer_ = nullptr;
    feedbackContainer_ = nullptr;
    latencyModeContainer_ = nullptr;
    modalKnobContainer_ = nullptr;
    waveguideKnobContainer_ = nullptr;
    adsrDisplayView_ = nullptr;
    adsrExpandedOverlay_ = nullptr;

    activeEditor_ = nullptr;

    // Reset shared display fallback state
    sharedDisplay_ = nullptr;
    dataExchangeActive_ = false;
    fallbackTickCounter_ = 0;

    // Defensive counter reset
    modInstanceCounter_ = 0;
}

// ==============================================================================
// IDataExchangeReceiver implementation
// ==============================================================================
void PLUGIN_API Controller::queueOpened(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/,
    Steinberg::uint32 /*blockSize*/,
    Steinberg::TBool& dispatchOnBackgroundThread)
{
    // Dispatch on UI thread so we can safely update cachedDisplayData_
    // without synchronization (timer also fires on UI thread).
    dispatchOnBackgroundThread = static_cast<Steinberg::TBool>(false);
}

void PLUGIN_API Controller::queueClosed(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/)
{
    // Nothing to clean up
}

void PLUGIN_API Controller::onDataExchangeBlocksReceived(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/,
    Steinberg::uint32 numBlocks,
    Steinberg::Vst::DataExchangeBlock* blocks,
    Steinberg::TBool /*onBackgroundThread*/)
{
    // Copy the latest block's DisplayData into the cached buffer.
    // If multiple blocks arrived, use the last one (most recent data).
    for (Steinberg::uint32 i = 0; i < numBlocks; ++i)
    {
        if (blocks[i].data && blocks[i].size >= sizeof(DisplayData))
        {
            std::memcpy(&cachedDisplayData_, blocks[i].data, sizeof(DisplayData));
            if (!dataExchangeActive_)
                KRATE_BRIDGE_LOG("Innexus::Controller — DataExchange data received (Tier 1/2 active)");
            dataExchangeActive_ = true;
            fallbackTickCounter_ = 0;
        }
    }
}

// ==============================================================================
// notify (T015: FR-048 IMessage protocol)
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::notify(
    Steinberg::Vst::IMessage* message)
{
    if (!message)
        return Steinberg::kInvalidArgument;

    // DataExchange fallback: IMessage-based display data transport.
    // The DataExchangeReceiverHandler decodes fallback messages and calls
    // onDataExchangeBlocksReceived() above. Returns true if handled.
    if (dataExchangeReceiver_.onMessage(message))
        return Steinberg::kResultOk;

    // Sample file loaded notification from processor
    if (std::strcmp(message->getMessageID(), "SampleFileLoaded") == 0)
    {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        const void* data = nullptr;
        Steinberg::uint32 dataSize = 0;
        if (attrs->getBinary("path", data, dataSize) == Steinberg::kResultOk
            && dataSize > 0)
        {
            std::string fullPath(static_cast<const char*>(data),
                                 static_cast<size_t>(dataSize));
            auto filename = std::filesystem::path(fullPath)
                                .filename().string();
            loadedSampleFilename_ = filename;
            loadedSampleFullPath_ = fullPath;
            setSampleFilenameDisplay(filename, fullPath);
        }
        return Steinberg::kResultOk;
    }

    // Spec 124 T018: Detected ADSR values from sample analysis
    // Processor sends plain values; we convert to normalized and update knobs.
    if (std::strcmp(message->getMessageID(), "DetectedADSR") == 0)
    {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        // Logarithmic normalization for time parameters:
        // plain = kMin * pow(kMax / kMin, norm) => norm = log(plain / kMin) / log(kMax / kMin)
        constexpr double kMin = 1.0;
        constexpr double kMax = 5000.0;
        const double kLogRatio = std::log(kMax / kMin);

        double attackMs = 10.0;
        double decayMs = 100.0;
        double sustainLevel = 1.0;
        double releaseMs = 100.0;

        attrs->getFloat("attackMs", attackMs);
        attrs->getFloat("decayMs", decayMs);
        attrs->getFloat("sustainLevel", sustainLevel);
        attrs->getFloat("releaseMs", releaseMs);

        // Clamp and normalize time params (log)
        auto logNorm = [&](double plainMs) -> double {
            double clamped = std::clamp(plainMs, kMin, kMax);
            return std::log(clamped / kMin) / kLogRatio;
        };

        double amount = 0.0;
        attrs->getFloat("amount", amount);

        setParamNormalized(kAdsrAttackId, logNorm(attackMs));
        setParamNormalized(kAdsrDecayId, logNorm(decayMs));
        setParamNormalized(kAdsrSustainId,
            std::clamp(sustainLevel, 0.0, 1.0));
        setParamNormalized(kAdsrReleaseId, logNorm(releaseMs));
        setParamNormalized(kAdsrAmountId, std::clamp(amount, 0.0, 1.0));

        // Push detected values to ADSRDisplay visualization
        updateAdsrDisplayFromParams();

        return Steinberg::kResultOk;
    }

    // Spec 124: Recalled ADSR values from memory slot recall
    if (std::strcmp(message->getMessageID(), "RecalledADSR") == 0)
    {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        constexpr double kMin = 1.0;
        constexpr double kMax = 5000.0;
        const double kLogRatio = std::log(kMax / kMin);

        auto logNorm = [&](double plainMs) -> double {
            double clamped = std::clamp(plainMs, kMin, kMax);
            return std::log(clamped / kMin) / kLogRatio;
        };

        double attackMs = 10.0;
        double decayMs = 100.0;
        double sustainLevel = 1.0;
        double releaseMs = 100.0;
        double amount = 0.0;
        double timeScale = 1.0;
        double attackCurve = 0.0;
        double decayCurve = 0.0;
        double releaseCurve = 0.0;

        attrs->getFloat("attackMs", attackMs);
        attrs->getFloat("decayMs", decayMs);
        attrs->getFloat("sustainLevel", sustainLevel);
        attrs->getFloat("releaseMs", releaseMs);
        attrs->getFloat("amount", amount);
        attrs->getFloat("timeScale", timeScale);
        attrs->getFloat("attackCurve", attackCurve);
        attrs->getFloat("decayCurve", decayCurve);
        attrs->getFloat("releaseCurve", releaseCurve);

        setParamNormalized(kAdsrAttackId, logNorm(attackMs));
        setParamNormalized(kAdsrDecayId, logNorm(decayMs));
        setParamNormalized(kAdsrSustainId, std::clamp(sustainLevel, 0.0, 1.0));
        setParamNormalized(kAdsrReleaseId, logNorm(releaseMs));
        setParamNormalized(kAdsrAmountId, std::clamp(amount, 0.0, 1.0));
        setParamNormalized(kAdsrTimeScaleId,
            std::clamp((timeScale - 0.25) / (4.0 - 0.25), 0.0, 1.0));
        setParamNormalized(kAdsrAttackCurveId,
            std::clamp((attackCurve + 1.0) / 2.0, 0.0, 1.0));
        setParamNormalized(kAdsrDecayCurveId,
            std::clamp((decayCurve + 1.0) / 2.0, 0.0, 1.0));
        setParamNormalized(kAdsrReleaseCurveId,
            std::clamp((releaseCurve + 1.0) / 2.0, 0.0, 1.0));

        // Push recalled values to ADSRDisplay visualization
        updateAdsrDisplayFromParams();

        return Steinberg::kResultOk;
    }

    // Spec 124 T049: Receive ADSR playback state atomic pointers from processor
    if (std::strcmp(message->getMessageID(), "ADSRPlaybackState") == 0)
    {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 val = 0;

        if (attrs->getInt("outputPtr", val) == Steinberg::kResultOk) {
            adsrOutputPtr_ = reinterpret_cast<std::atomic<float>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("stagePtr", val) == Steinberg::kResultOk) {
            adsrStagePtr_ = reinterpret_cast<std::atomic<int>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("activePtr", val) == Steinberg::kResultOk) {
            adsrActivePtr_ = reinterpret_cast<std::atomic<bool>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }

        // Wire pointers to ADSRDisplay if already created
        if (adsrDisplayView_ && adsrOutputPtr_ && adsrStagePtr_ && adsrActivePtr_) {
            adsrDisplayView_->setPlaybackStatePointers(
                adsrOutputPtr_, adsrStagePtr_, adsrActivePtr_);
        }

        return Steinberg::kResultOk;
    }

    return EditControllerEx1::notify(message);
}

// ==============================================================================
// createAdsrExpandButton — factory for the ADSR expand button
// ==============================================================================
VSTGUI::CView* Controller::createAdsrExpandButton(const VSTGUI::CRect& rect)
{
    return new ADSRExpandButton(rect, this);
}

// ==============================================================================
// INoteExpressionController (Phase 4: MPE support)
// ==============================================================================

Steinberg::int32 PLUGIN_API Controller::getNoteExpressionCount(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/)
{
    if (busIndex != 0)
        return 0;
    return noteExpressionTypes_.getNoteExpressionCount();
}

Steinberg::tresult PLUGIN_API Controller::getNoteExpressionInfo(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
    Steinberg::int32 noteExpressionIndex,
    Steinberg::Vst::NoteExpressionTypeInfo& info)
{
    if (busIndex != 0)
        return Steinberg::kResultFalse;
    return noteExpressionTypes_.getNoteExpressionInfo(noteExpressionIndex, info);
}

Steinberg::tresult PLUGIN_API Controller::getNoteExpressionStringByValue(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
    Steinberg::Vst::NoteExpressionTypeID id,
    Steinberg::Vst::NoteExpressionValue valueNormalized,
    Steinberg::Vst::String128 string)
{
    if (busIndex != 0)
        return Steinberg::kResultFalse;
    return noteExpressionTypes_.getNoteExpressionStringByValue(
        id, valueNormalized, string);
}

Steinberg::tresult PLUGIN_API Controller::getNoteExpressionValueByString(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
    Steinberg::Vst::NoteExpressionTypeID id,
    const Steinberg::Vst::TChar* string,
    Steinberg::Vst::NoteExpressionValue& valueNormalized)
{
    if (busIndex != 0)
        return Steinberg::kResultFalse;
    return noteExpressionTypes_.getNoteExpressionValueByString(
        id, string, valueNormalized);
}

// ==============================================================================
// INoteExpressionPhysicalUIMapping (Phase 4: MPE controller mapping)
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::getPhysicalUIMapping(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
    Steinberg::Vst::PhysicalUIMapList& list)
{
    if (busIndex != 0)
        return Steinberg::kResultFalse;

    for (Steinberg::uint32 i = 0; i < list.count; ++i)
    {
        using namespace Steinberg::Vst;
        auto& entry = list.map[i];
        switch (entry.physicalUITypeID)
        {
        case PhysicalUITypeIDs::kPUIXMovement:
            entry.noteExpressionTypeID = NoteExpressionTypeIDs::kTuningTypeID;
            break;
        case PhysicalUITypeIDs::kPUIYMovement:
            entry.noteExpressionTypeID = NoteExpressionTypeIDs::kBrightnessTypeID;
            break;
        case PhysicalUITypeIDs::kPUIPressure:
            entry.noteExpressionTypeID = NoteExpressionTypeIDs::kVolumeTypeID;
            break;
        default:
            entry.noteExpressionTypeID = kInvalidTypeID;
            break;
        }
    }

    return Steinberg::kResultOk;
}
} // namespace Innexus
