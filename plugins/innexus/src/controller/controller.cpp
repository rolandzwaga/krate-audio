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
        1.0,    // default plain
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

    // Cross-Synthesis: Timbral Blend (FR-001)
    auto* timbralBlendParam = new Steinberg::Vst::RangeParameter(
        STR16("Timbral Blend"), kTimbralBlendId,
        STR16("%"), 0.0, 1.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(timbralBlendParam);

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
    resonanceTypeParam->appendString(STR16("Body"));
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
// Set Component State (FR-056: Restore controller parameters from processor state)
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::setComponentState(
    Steinberg::IBStream* state)
{
    if (!state)
        return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version (must match Processor::getState format)
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return Steinberg::kResultFalse;

    if (version != 1)
        return Steinberg::kResultFalse;

    float floatVal = 0.0f;

    // --- M1 parameters ---
    if (streamer.readFloat(floatVal))
    {
        double normalized = releaseTimeToNormalized(
            std::clamp(floatVal, 20.0f, 5000.0f));
        setParamNormalized(kReleaseTimeId, normalized);
    }

    if (streamer.readFloat(floatVal))
    {
        setParamNormalized(kInharmonicityAmountId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
    }

    if (streamer.readFloat(floatVal))
    {
            setParamNormalized(kMasterGainId,
                static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
        }

        // Read bypass
        if (streamer.readFloat(floatVal))
        {
            setParamNormalized(kBypassId,
                floatVal > 0.5f ? 1.0 : 0.0);
        }

        // Read sample file path for filename display
        Steinberg::int32 pathLen = 0;
        if (streamer.readInt32(pathLen) && pathLen > 0 && pathLen < 4096)
        {
            std::vector<char> pathBuf(static_cast<size_t>(pathLen));
            Steinberg::int32 bytesRead = 0;
            state->read(pathBuf.data(), pathLen, &bytesRead);
            if (bytesRead == pathLen)
            {
                std::string fullPath(pathBuf.data(),
                                     static_cast<size_t>(pathLen));
                auto filename = std::filesystem::path(fullPath)
                                    .filename().string();
                loadedSampleFilename_ = filename;
                loadedSampleFullPath_ = fullPath;
            }
        }

    // --- M2 parameters (FR-027) ---
    if (streamer.readFloat(floatVal))
    {
        double normalized = static_cast<double>(
            std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
        setParamNormalized(kHarmonicLevelId, normalized);
    }

    if (streamer.readFloat(floatVal))
    {
        double normalized = static_cast<double>(
            std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
        setParamNormalized(kResidualLevelId, normalized);
    }

    if (streamer.readFloat(floatVal))
    {
        double normalized = static_cast<double>(
            std::clamp(floatVal, -1.0f, 1.0f) + 1.0f) / 2.0;
        setParamNormalized(kResidualBrightnessId, normalized);
    }

    if (streamer.readFloat(floatVal))
    {
        double normalized = static_cast<double>(
            std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
        setParamNormalized(kTransientEmphasisId, normalized);
    }

    // Skip residual frames block (mirrors processor getState format)
    {
        Steinberg::int32 residualFrameCount = 0;
        Steinberg::int32 analysisFFTSizeInt = 0;
        Steinberg::int32 analysisHopSizeInt = 0;

        if (streamer.readInt32(residualFrameCount) &&
            streamer.readInt32(analysisFFTSizeInt) &&
            streamer.readInt32(analysisHopSizeInt) &&
            residualFrameCount > 0)
        {
            for (Steinberg::int32 f = 0; f < residualFrameCount; ++f)
            {
                for (size_t b = 0; b < 16; ++b)
                {
                    float skipFloat = 0.0f;
                    streamer.readFloat(skipFloat);
                }
                float skipFloat = 0.0f;
                streamer.readFloat(skipFloat); // totalEnergy
                Steinberg::int8 skipByte = 0;
                streamer.readInt8(skipByte); // transientFlag
            }
        }
    }

    // --- M3 parameters (sidechain) ---
    {
        Steinberg::int32 inputSourceInt = 0;
        Steinberg::int32 latencyModeInt = 0;
        if (streamer.readInt32(inputSourceInt))
            setParamNormalized(kInputSourceId, inputSourceInt > 0 ? 1.0 : 0.0);
        if (streamer.readInt32(latencyModeInt))
            setParamNormalized(kLatencyModeId, latencyModeInt > 0 ? 1.0 : 0.0);
    }

    // --- M4 parameters (musical control) ---
    {
        Steinberg::int8 freezeState = 0;
        if (streamer.readInt8(freezeState))
            setParamNormalized(kFreezeId, freezeState ? 1.0 : 0.0);

        float morphPos = 0.0f;
        if (streamer.readFloat(morphPos))
            setParamNormalized(kMorphPositionId,
                static_cast<double>(std::clamp(morphPos, 0.0f, 1.0f)));

        Steinberg::int32 filterType = 0;
        if (streamer.readInt32(filterType))
            setParamNormalized(kHarmonicFilterTypeId,
                static_cast<double>(std::clamp(
                    static_cast<float>(filterType) / 4.0f, 0.0f, 1.0f)));

        float resp = 0.5f;
        if (streamer.readFloat(resp))
            setParamNormalized(kResponsivenessId,
                static_cast<double>(std::clamp(resp, 0.0f, 1.0f)));
    }

    // --- M5 parameters (harmonic memory) ---
    {
        Steinberg::int32 selectedSlot = 0;
        if (streamer.readInt32(selectedSlot))
        {
            selectedSlot = std::clamp(selectedSlot,
                static_cast<Steinberg::int32>(0),
                static_cast<Steinberg::int32>(7));
            setParamNormalized(kMemorySlotId,
                std::clamp(static_cast<double>(selectedSlot) / 7.0, 0.0, 1.0));
        }

        // Skip all 8 memory slots' binary snapshot data
        for (int s = 0; s < 8; ++s)
        {
            Steinberg::int8 occupiedByte = 0;
            if (!streamer.readInt8(occupiedByte))
                break;

            if (occupiedByte != 0)
            {
                float skipFloat = 0.0f;
                Steinberg::int32 skipInt = 0;

                streamer.readFloat(skipFloat);  // f0Reference
                streamer.readInt32(skipInt);     // numPartials

                // 96*4 floats for per-partial arrays
                for (int i = 0; i < 96 * 4; ++i)
                    streamer.readFloat(skipFloat);

                // 16 floats for residualBands
                for (int i = 0; i < 16; ++i)
                    streamer.readFloat(skipFloat);

                // 4 scalar floats
                streamer.readFloat(skipFloat);   // residualEnergy
                streamer.readFloat(skipFloat);   // globalAmplitude
                streamer.readFloat(skipFloat);   // spectralCentroid
                streamer.readFloat(skipFloat);   // brightness
            }
        }

        // Momentary triggers always reset to 0 (FR-023, FR-030)
        setParamNormalized(kMemoryCaptureId, 0.0);
        setParamNormalized(kMemoryRecallId, 0.0);
    }

    // --- M6 parameters (creative extensions) ---
    {
        float m6Val = 0.0f;
        if (streamer.readFloat(m6Val))
            setParamNormalized(kTimbralBlendId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kStereoSpreadId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kEvolutionEnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kEvolutionSpeedId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kEvolutionDepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kEvolutionModeId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1EnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1WaveformId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1RateId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1DepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1RangeStartId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1RangeEndId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1TargetId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2EnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2WaveformId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2RateId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2DepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2RangeStartId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2RangeEndId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2TargetId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kDetuneSpreadId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendEnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight1Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight2Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight3Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight4Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight5Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight6Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight7Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight8Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendLiveWeightId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
    }

    // --- Harmonic Physics parameters ---
    {
        float physVal = 0.0f;
        if (streamer.readFloat(physVal))
            setParamNormalized(kWarmthId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParamNormalized(kCouplingId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParamNormalized(kStabilityId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParamNormalized(kEntropyId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
    }

    // --- Analysis Feedback Loop parameters ---
    {
        float fbVal = 0.0f;
        if (streamer.readFloat(fbVal))
            setParamNormalized(kAnalysisFeedbackId, static_cast<double>(std::clamp(fbVal, 0.0f, 1.0f)));
        if (streamer.readFloat(fbVal))
            setParamNormalized(kAnalysisFeedbackDecayId, static_cast<double>(std::clamp(fbVal, 0.0f, 1.0f)));
    }

    // --- ADSR Envelope Detection parameters ---
    {
        constexpr double kMin = 1.0;
        constexpr double kMax = 5000.0;
        const double kLogRatio = std::log(kMax / kMin);
        auto logNorm = [&](double plainMs) -> double {
            double clamped = std::clamp(plainMs, kMin, kMax);
            return std::log(clamped / kMin) / kLogRatio;
        };

        float adsrVal = 0.0f;
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrAttackId, logNorm(static_cast<double>(adsrVal)));
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrDecayId, logNorm(static_cast<double>(adsrVal)));
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrSustainId, static_cast<double>(std::clamp(adsrVal, 0.0f, 1.0f)));
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrReleaseId, logNorm(static_cast<double>(adsrVal)));
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrAmountId, static_cast<double>(std::clamp(adsrVal, 0.0f, 1.0f)));
        if (streamer.readFloat(adsrVal))
        {
            double norm = static_cast<double>(std::clamp(adsrVal, 0.25f, 4.0f) - 0.25f) / (4.0 - 0.25);
            setParamNormalized(kAdsrTimeScaleId, norm);
        }
        if (streamer.readFloat(adsrVal))
        {
            double norm = static_cast<double>(std::clamp(adsrVal, -1.0f, 1.0f) + 1.0f) / 2.0;
            setParamNormalized(kAdsrAttackCurveId, norm);
        }
        if (streamer.readFloat(adsrVal))
        {
            double norm = static_cast<double>(std::clamp(adsrVal, -1.0f, 1.0f) + 1.0f) / 2.0;
            setParamNormalized(kAdsrDecayCurveId, norm);
        }
        if (streamer.readFloat(adsrVal))
        {
            double norm = static_cast<double>(std::clamp(adsrVal, -1.0f, 1.0f) + 1.0f) / 2.0;
            setParamNormalized(kAdsrReleaseCurveId, norm);
        }

        // Skip per-slot ADSR data (controller does not store slot data)
        for (int s = 0; s < 8; ++s)
        {
            float skipFloat = 0.0f;
            for (int f = 0; f < 9; ++f)
                streamer.readFloat(skipFloat);
        }
    }

    // --- Partial Count parameter ---
    {
        float pcVal = 0.0f;
        if (streamer.readFloat(pcVal))
            setParamNormalized(kPartialCountId,
                static_cast<double>(std::clamp(pcVal, 0.0f, 1.0f)));
    }

    // --- Modulator Tempo Sync parameters (skip 4 floats) ---
    {
        float skipVal = 0.0f;
        streamer.readFloat(skipVal); // mod1RateSync
        streamer.readFloat(skipVal); // mod1NoteValue
        streamer.readFloat(skipVal); // mod2RateSync
        streamer.readFloat(skipVal); // mod2NoteValue
    }

    // --- Voice Mode parameter ---
    {
        float vmVal = 0.0f;
        if (streamer.readFloat(vmVal))
            setParamNormalized(kVoiceModeId,
                static_cast<double>(std::clamp(vmVal, 0.0f, 1.0f)));
    }

    // --- Physical Modelling parameters (Spec 127, graceful fallback for old states) ---
    {
        float pmVal = 0.0f;
        if (streamer.readFloat(pmVal))
            setParamNormalized(kPhysModelMixId,
                static_cast<double>(std::clamp(pmVal, 0.0f, 1.0f)));
        if (streamer.readFloat(pmVal))
        {
            // resonanceDecay_ is stored as plain seconds (0.01-5.0)
            // Reverse log mapping: norm = log(plain/0.01) / log(500)
            float plain = std::clamp(pmVal, 0.01f, 5.0f);
            float norm = std::log(plain / 0.01f) / std::log(500.0f);
            setParamNormalized(kResonanceDecayId,
                static_cast<double>(std::clamp(norm, 0.0f, 1.0f)));
        }
        if (streamer.readFloat(pmVal))
            setParamNormalized(kResonanceBrightnessId,
                static_cast<double>(std::clamp(pmVal, 0.0f, 1.0f)));
        if (streamer.readFloat(pmVal))
            setParamNormalized(kResonanceStretchId,
                static_cast<double>(std::clamp(pmVal, 0.0f, 1.0f)));
        if (streamer.readFloat(pmVal))
            setParamNormalized(kResonanceScatterId,
                static_cast<double>(std::clamp(pmVal, 0.0f, 1.0f)));
    }

    // --- Impact Exciter parameters (Spec 128, graceful fallback for old states) ---
    {
        float ieVal = 0.0f;
        if (streamer.readFloat(ieVal))
            setParamNormalized(kExciterTypeId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
        if (streamer.readFloat(ieVal))
            setParamNormalized(kImpactHardnessId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
        if (streamer.readFloat(ieVal))
            setParamNormalized(kImpactMassId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
        if (streamer.readFloat(ieVal))
            setParamNormalized(kImpactBrightnessId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
        if (streamer.readFloat(ieVal))
            setParamNormalized(kImpactPositionId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
    }

    // --- Waveguide String Resonance parameters (Spec 129, graceful fallback for old states) ---
    {
        float wgVal = 0.0f;
        if (streamer.readFloat(wgVal))
            setParamNormalized(kResonanceTypeId,
                static_cast<double>(std::clamp(wgVal, 0.0f, 1.0f)));
        if (streamer.readFloat(wgVal))
            setParamNormalized(kWaveguideStiffnessId,
                static_cast<double>(std::clamp(wgVal, 0.0f, 1.0f)));
        if (streamer.readFloat(wgVal))
            setParamNormalized(kWaveguidePickPositionId,
                static_cast<double>(std::clamp(wgVal, 0.0f, 1.0f)));
    }

    // --- Bow Exciter parameters (Spec 130, graceful fallback for old states) ---
    {
        float bowVal = 0.0f;
        if (streamer.readFloat(bowVal))
            setParamNormalized(kBowPressureId,
                static_cast<double>(std::clamp(bowVal, 0.0f, 1.0f)));
        if (streamer.readFloat(bowVal))
            setParamNormalized(kBowSpeedId,
                static_cast<double>(std::clamp(bowVal, 0.0f, 1.0f)));
        if (streamer.readFloat(bowVal))
            setParamNormalized(kBowPositionId,
                static_cast<double>(std::clamp(bowVal, 0.0f, 1.0f)));
        if (streamer.readFloat(bowVal))
            setParamNormalized(kBowOversamplingId,
                static_cast<double>(std::clamp(bowVal, 0.0f, 1.0f)));
    }

    // SharedDisplayBridge: try to read instance ID from state trailer
    {
        Steinberg::int32 marker = 0;
        Steinberg::int64 storedId = 0;
        if (streamer.readInt32(marker) && marker == kInstanceIdMarker
            && streamer.readInt64(storedId))
        {
            instanceId_ = static_cast<uint64_t>(storedId);
            sharedDisplay_ = static_cast<SharedDisplay*>(
                Krate::Plugins::SharedDisplayBridge::instance().lookupInstance(instanceId_));
            KRATE_BRIDGE_LOG("Innexus::Controller::setComponentState() — id=0x%llx, bridge=%s",
                static_cast<unsigned long long>(instanceId_),
                sharedDisplay_ ? "found" : "NOT found");
        }
    }

    return Steinberg::kResultOk;
}

// ==============================================================================
// Import Snapshot from JSON (FR-025, FR-029)
// ==============================================================================
bool Controller::importSnapshotFromJson(const std::string& filePath,
                                        int slotIndex)
{
    // Read file
    std::ifstream f(filePath);
    if (!f.is_open()) return false;
    const std::string json((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

    // Parse
    Krate::DSP::HarmonicSnapshot snap{};
    if (!Innexus::jsonToSnapshot(json, snap)) return false;

    // Dispatch to processor via IMessage (FR-029)
    auto* msg = allocateMessage();
    if (!msg) return false;
    msg->setMessageID("HarmonicSnapshotImport");
    auto* attrs = msg->getAttributes();
    attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slotIndex));
    attrs->setBinary("snapshotData", &snap, sizeof(snap));
    sendMessage(msg);
    msg->release();
    return true;
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
// updateAdsrDisplayFromParams — push current ADSR parameter values to display
// ==============================================================================
void Controller::updateAdsrDisplayFromParams()
{
    if (!adsrDisplayView_)
        return;

    // Read normalized values and convert to plain ms using log mapping
    // This matches the processor's denormalization: ms = kMin * pow(kMax/kMin, norm)
    constexpr float kMin = 1.0f;
    constexpr float kMax = 5000.0f;
    auto normToMs = [&](Steinberg::Vst::ParamID id) -> float {
        auto norm = static_cast<float>(getParamNormalized(id));
        return kMin * std::pow(kMax / kMin, norm);
    };

    adsrDisplayView_->setAttackMs(normToMs(kAdsrAttackId));
    adsrDisplayView_->setDecayMs(normToMs(kAdsrDecayId));
    adsrDisplayView_->setSustainLevel(
        static_cast<float>(getParamNormalized(kAdsrSustainId)));
    adsrDisplayView_->setReleaseMs(normToMs(kAdsrReleaseId));

    // Curve amounts: normalized [0,1] -> plain [-1,+1]
    auto normToCurve = [&](Steinberg::Vst::ParamID id) -> float {
        return static_cast<float>(getParamNormalized(id)) * 2.0f - 1.0f;
    };

    adsrDisplayView_->setAttackCurve(normToCurve(kAdsrAttackCurveId));
    adsrDisplayView_->setDecayCurve(normToCurve(kAdsrDecayCurveId));
    adsrDisplayView_->setReleaseCurve(normToCurve(kAdsrReleaseCurveId));
}

// ==============================================================================
// setParamNormalized — forward ADSR param changes to ADSRDisplay
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value)
{
    auto result = EditControllerEx1::setParamNormalized(id, value);

    // Forward ADSR parameter changes to both display views
    if (result == Steinberg::kResultOk
        && id >= kAdsrAttackId && id <= kAdsrReleaseCurveId)
    {
        auto norm = static_cast<float>(value);
        forwardAdsrParamToDisplay(adsrDisplayView_, id, norm);

        if (adsrExpandedOverlay_)
            forwardAdsrParamToDisplay(adsrExpandedOverlay_->getDisplay(), id, norm);
    }

    return result;
}

// ==============================================================================
// onSampleFileSelected — called by SampleLoadSubController
// ==============================================================================
void Controller::onSampleFileSelected(const std::string& filePath)
{
    // Extract filename for display
    auto filename = std::filesystem::path(filePath).filename().string();
    loadedSampleFilename_ = filename;
    loadedSampleFullPath_ = filePath;
    setSampleFilenameDisplay(filename, filePath);

    // Send file path to processor via IMessage
    auto* msg = allocateMessage();
    if (!msg)
        return;
    msg->setMessageID("LoadSampleFile");
    auto* attrs = msg->getAttributes();
    attrs->setBinary("path", filePath.data(),
                     static_cast<Steinberg::uint32>(filePath.size()));
    sendMessage(msg);
    msg->release();
}

// ==============================================================================
// setSampleFilenameDisplay
// ==============================================================================
void Controller::setSampleFilenameDisplay(const std::string& filename,
                                          const std::string& fullPath)
{
    if (sampleFilenameLabel_)
    {
        sampleFilenameLabel_->setText(VSTGUI::UTF8String(filename));
        sampleFilenameLabel_->setTooltipText(VSTGUI::UTF8String(fullPath));
        sampleFilenameLabel_->invalid();
    }
}

// ==============================================================================
// updateSampleLoadVisibility
// ==============================================================================
void Controller::updateSampleLoadVisibility()
{
    if (!sampleLoadContainer_)
        return;

    // InputSource: 0 = Sample (show), 1 = Sidechain (hide)
    auto* param = getParameterObject(kInputSourceId);
    if (!param)
        return;

    bool isSampleMode = param->getNormalized() < 0.5;
    sampleLoadContainer_->setVisible(isSampleMode);
    if (sampleLoadContainer_->getParentView())
        sampleLoadContainer_->getParentView()->invalid();
}

// ==============================================================================
// updateImpactKnobVisibility
// ==============================================================================
void Controller::updateImpactKnobVisibility()
{
    // Lazy-find the impact knob container by locating a child with the
    // ImpactHardness tag (806) and taking its parent container
    if (!impactKnobContainer_ && activeEditor_) {
        if (auto* frame = activeEditor_->getFrame()) {
            std::function<void(VSTGUI::CViewContainer*)> search;
            search = [this, &search](VSTGUI::CViewContainer* container) {
                if (!container || impactKnobContainer_) return;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                        if (control->getTag() == kImpactHardnessId) {
                            impactKnobContainer_ = container;
                            return;
                        }
                    }
                    if (auto* child = (*it)->asViewContainer())
                        search(child);
                    ++it;
                }
            };
            search(frame);
        }
    }

    if (!impactKnobContainer_)
        return;

    // ExciterType: StringListParameter with 3 values
    // Normalized: 0.0 = Residual, 0.5 = Impact, 1.0 = Bow
    // Show Impact knobs only when ExciterType == Impact (norm ~0.5)
    auto* param = getParameterObject(kExciterTypeId);
    if (!param)
        return;

    float norm = param->getNormalized();
    bool isImpact = (norm >= 0.25f && norm < 0.75f);
    impactKnobContainer_->setVisible(isImpact);
    if (impactKnobContainer_->getParentView())
        impactKnobContainer_->getParentView()->invalid();
}

// ==============================================================================
// updateResonatorVisibility
// ==============================================================================
void Controller::updateResonatorVisibility()
{
    // Lazy-find the two resonator knob containers by locating children with
    // unique tags: ResonanceStretch (803) for modal, WaveguideStiffness (811)
    // for waveguide. Take their parent containers.
    if ((!modalKnobContainer_ || !waveguideKnobContainer_) && activeEditor_) {
        if (auto* frame = activeEditor_->getFrame()) {
            std::function<void(VSTGUI::CViewContainer*)> search;
            search = [this, &search](VSTGUI::CViewContainer* container) {
                if (!container || (modalKnobContainer_ && waveguideKnobContainer_))
                    return;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                        if (control->getTag() == kResonanceStretchId
                            && !modalKnobContainer_) {
                            modalKnobContainer_ = container;
                        }
                        if (control->getTag() == kWaveguideStiffnessId
                            && !waveguideKnobContainer_) {
                            waveguideKnobContainer_ = container;
                        }
                    }
                    if (auto* child = (*it)->asViewContainer())
                        search(child);
                    ++it;
                }
            };
            search(frame);
        }
    }

    if (!modalKnobContainer_ || !waveguideKnobContainer_)
        return;

    // ResonanceType: StringListParameter with 3 values
    // Normalized: 0.0 = Modal, 0.5 = Waveguide, 1.0 = Body
    auto* param = getParameterObject(kResonanceTypeId);
    if (!param)
        return;

    float norm = param->getNormalized();
    bool isModal = (norm < 0.25f);     // Modal = 0.0
    bool isWaveguide = (norm >= 0.25f && norm < 0.75f); // Waveguide = 0.5

    modalKnobContainer_->setVisible(isModal);
    waveguideKnobContainer_->setVisible(isWaveguide);

    if (modalKnobContainer_->getParentView())
        modalKnobContainer_->getParentView()->invalid();
}

// ==============================================================================
// onDisplayTimerFired (T016: FR-049)
// ==============================================================================
void Controller::onDisplayTimerFired()
{
    // Update sample load panel visibility (cheap check every 30ms)
    updateSampleLoadVisibility();

    // Update impact exciter knob visibility based on exciter type
    updateImpactKnobVisibility();

    // Update resonator knob containers based on resonance type
    updateResonatorVisibility();

    // Tier 3 fallback: if DataExchange hasn't delivered data after ~330ms,
    // read directly from the processor's shared display buffer
    if (!dataExchangeActive_ && sharedDisplay_) {
        ++fallbackTickCounter_;
        if (fallbackTickCounter_ > 10) { // ~330ms at 30ms timer
            if (fallbackTickCounter_ == 11)
                KRATE_BRIDGE_LOG("Innexus::Controller — Tier 3 fallback ACTIVATED (no DataExchange after ~330ms)");
            auto counter = sharedDisplay_->frameCounter.load(std::memory_order_acquire);
            if (counter != lastProcessedFrameCounter_) {
                std::memcpy(&cachedDisplayData_, &sharedDisplay_->buffer,
                    sizeof(DisplayData));
                cachedDisplayData_.frameCounter = counter;
            }
        }
    }

    // Check if we have new data (frame counter changed)
    if (cachedDisplayData_.frameCounter == lastProcessedFrameCounter_)
    {
        // No new frame — track staleness. When the host stops calling
        // process() (e.g. playback stopped), clear the display so it
        // doesn't show stale partial data indefinitely.
        if (staleTickCount_ < kStaleTickThreshold)
        {
            ++staleTickCount_;
            if (staleTickCount_ == kStaleTickThreshold)
            {
                // Clear cached data and push empty data to all views
                DisplayData empty{};
                cachedDisplayData_ = empty;
                if (harmonicDisplayView_)
                    harmonicDisplayView_->updateData(empty);
                if (confidenceIndicatorView_)
                    confidenceIndicatorView_->updateData(empty);
                if (memorySlotStatusView_)
                    memorySlotStatusView_->updateData(empty);
                if (evolutionPositionView_)
                    evolutionPositionView_->updateData(empty, false);
                if (modActivityView0_)
                    modActivityView0_->updateData(0.0f, false);
                if (modActivityView1_)
                    modActivityView1_->updateData(0.0f, false);
            }
        }
        return;
    }

    staleTickCount_ = 0;
    lastProcessedFrameCounter_ = cachedDisplayData_.frameCounter;

    // Update each custom view with cached display data
    if (harmonicDisplayView_)
        harmonicDisplayView_->updateData(cachedDisplayData_);

    if (confidenceIndicatorView_)
        confidenceIndicatorView_->updateData(cachedDisplayData_);

    if (memorySlotStatusView_)
        memorySlotStatusView_->updateData(cachedDisplayData_);

    if (evolutionPositionView_)
    {
        // Evolution is active when the enable parameter is on
        // We approximate by checking if the position is non-zero
        // or if the cached data shows it active
        bool evolutionActive = cachedDisplayData_.evolutionPosition > 0.001f ||
                               cachedDisplayData_.manualMorphPosition > 0.001f;
        evolutionPositionView_->updateData(cachedDisplayData_, evolutionActive);
    }

    if (modActivityView0_)
        modActivityView0_->updateData(
            cachedDisplayData_.mod1Phase,
            cachedDisplayData_.mod1Active);

    if (modActivityView1_)
        modActivityView1_->updateData(
            cachedDisplayData_.mod2Phase,
            cachedDisplayData_.mod2Active);
}

// ==============================================================================
// createAdsrExpandButton — factory for the ADSR expand button
// ==============================================================================
VSTGUI::CView* Controller::createAdsrExpandButton(const VSTGUI::CRect& rect)
{
    return new ADSRExpandButton(rect, this);
}

// ==============================================================================
// wireAdsrDisplay — wire parameter callbacks to an ADSRDisplay instance
// ==============================================================================
void Controller::wireAdsrDisplay(Krate::Plugins::ADSRDisplay* display)
{
    if (!display)
        return;

    display->setAdsrBaseParamId(kAdsrAttackId);
    display->setCurveBaseParamId(kAdsrAttackCurveId);

    display->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            // ADSRDisplay uses cube-root normalization for time params:
            //   norm_cube = cbrt(ms / 10000)
            // The processor expects log normalization:
            //   norm_log = log(ms / kMin) / log(kMax / kMin)
            // Convert cube-root → ms → log for time parameters.
            double corrected = static_cast<double>(normalizedValue);
            if (paramId == kAdsrAttackId || paramId == kAdsrDecayId
                || paramId == kAdsrReleaseId) {
                constexpr float kMin = 1.0f;
                constexpr float kMax = 5000.0f;
                // Cube-root → plain ms
                float ms = normalizedValue * normalizedValue * normalizedValue * 10000.0f;
                ms = std::clamp(ms, kMin, kMax);
                // Plain ms → log-normalized
                corrected = static_cast<double>(
                    std::log(ms / kMin) / std::log(kMax / kMin));
            }
            performEdit(paramId, corrected);
            setParamNormalized(paramId, corrected);
        });
    display->setBeginEditCallback(
        [this](uint32_t paramId) { beginEdit(paramId); });
    display->setEndEditCallback(
        [this](uint32_t paramId) { endEdit(paramId); });

    if (adsrOutputPtr_ && adsrStagePtr_ && adsrActivePtr_)
        display->setPlaybackStatePointers(adsrOutputPtr_, adsrStagePtr_, adsrActivePtr_);
}

// ==============================================================================
// forwardAdsrParamToDisplay — forward a single ADSR parameter to a display
// ==============================================================================
void Controller::forwardAdsrParamToDisplay(Krate::Plugins::ADSRDisplay* display,
                                           Steinberg::Vst::ParamID id,
                                           float norm)
{
    if (!display)
        return;

    // Time params use log mapping: ms = kMin * pow(kMax/kMin, norm)
    // This matches the processor's denormalization in processParameterChanges.
    constexpr float kMin = 1.0f;
    constexpr float kMax = 5000.0f;

    switch (id) {
        case kAdsrAttackId:
            display->setAttackMs(kMin * std::pow(kMax / kMin, norm));
            break;
        case kAdsrDecayId:
            display->setDecayMs(kMin * std::pow(kMax / kMin, norm));
            break;
        case kAdsrSustainId:
            display->setSustainLevel(norm);
            break;
        case kAdsrReleaseId:
            display->setReleaseMs(kMin * std::pow(kMax / kMin, norm));
            break;
        case kAdsrAttackCurveId:
            display->setAttackCurve(norm * 2.0f - 1.0f);
            break;
        case kAdsrDecayCurveId:
            display->setDecayCurve(norm * 2.0f - 1.0f);
            break;
        case kAdsrReleaseCurveId:
            display->setReleaseCurve(norm * 2.0f - 1.0f);
            break;
        default:
            break;
    }
}

// ==============================================================================
// openAdsrExpandedOverlay / closeAdsrExpandedOverlay
// ==============================================================================
void Controller::openAdsrExpandedOverlay()
{
    if (adsrExpandedOverlay_ && !adsrExpandedOverlay_->isOpen()) {
        // Sync expanded display with current parameter values
        if (auto* display = adsrExpandedOverlay_->getDisplay()) {
            constexpr float kMin = 1.0f;
            constexpr float kMax = 5000.0f;
            auto normToMs = [&](Steinberg::Vst::ParamID id) -> float {
                auto norm = static_cast<float>(getParamNormalized(id));
                return kMin * std::pow(kMax / kMin, norm);
            };
            auto normToCurve = [&](Steinberg::Vst::ParamID id) -> float {
                return static_cast<float>(getParamNormalized(id)) * 2.0f - 1.0f;
            };

            display->setAttackMs(normToMs(kAdsrAttackId));
            display->setDecayMs(normToMs(kAdsrDecayId));
            display->setSustainLevel(
                static_cast<float>(getParamNormalized(kAdsrSustainId)));
            display->setReleaseMs(normToMs(kAdsrReleaseId));
            display->setAttackCurve(normToCurve(kAdsrAttackCurveId));
            display->setDecayCurve(normToCurve(kAdsrDecayCurveId));
            display->setReleaseCurve(normToCurve(kAdsrReleaseCurveId));
        }
        adsrExpandedOverlay_->open();
    }
}

void Controller::closeAdsrExpandedOverlay()
{
    if (adsrExpandedOverlay_ && adsrExpandedOverlay_->isOpen())
        adsrExpandedOverlay_->close();
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
