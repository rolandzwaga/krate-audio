// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "dsp/harmonic_snapshot_json.h"
#include "parameters/innexus_params.h"
#include "plugin_ids.h"
#include "update/innexus_update_config.h"

#include "controller/views/harmonic_display_view.h"
#include "controller/views/confidence_indicator_view.h"
#include "controller/views/memory_slot_status_view.h"
#include "controller/views/evolution_position_view.h"
#include "controller/views/modulator_activity_view.h"
#include "controller/modulator_sub_controller.h"

#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
        STR16(""), 1.0, 48.0, 1.0, 47,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1RangeStartParam);

    auto* mod1RangeEndParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Range End"), kMod1RangeEndId,
        STR16(""), 1.0, 48.0, 48.0, 47,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1RangeEndParam);

    auto* mod1TargetParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 1 Target"), kMod1TargetId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod1TargetParam->appendString(STR16("Amplitude"));
    mod1TargetParam->appendString(STR16("Frequency"));
    mod1TargetParam->appendString(STR16("Pan"));
    parameters.addParameter(mod1TargetParam);

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
        STR16(""), 1.0, 48.0, 1.0, 47,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2RangeStartParam);

    auto* mod2RangeEndParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Range End"), kMod2RangeEndId,
        STR16(""), 1.0, 48.0, 48.0, 47,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2RangeEndParam);

    auto* mod2TargetParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 2 Target"), kMod2TargetId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod2TargetParam->appendString(STR16("Amplitude"));
    mod2TargetParam->appendString(STR16("Frequency"));
    mod2TargetParam->appendString(STR16("Pan"));
    parameters.addParameter(mod2TargetParam);

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

    // Update checker
    updateChecker_ = std::make_unique<Krate::Plugins::UpdateChecker>(makeInnexusUpdateConfig());

    return Steinberg::kResultOk;
}

// ==============================================================================
// Terminate
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::terminate()
{
    updateChecker_.reset();
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

    if (version >= 1)
    {
        float floatVal = 0.0f;

        // Read releaseTimeMs and convert to normalized for controller
        if (streamer.readFloat(floatVal))
        {
            double normalized = releaseTimeToNormalized(
                std::clamp(floatVal, 20.0f, 5000.0f));
            setParamNormalized(kReleaseTimeId, normalized);
        }

        // Read inharmonicityAmount (0-1 maps directly to normalized)
        if (streamer.readFloat(floatVal))
        {
            setParamNormalized(kInharmonicityAmountId,
                static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
        }

        // Read masterGain (0-1 maps directly to normalized)
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

        // M2: Read residual parameters if version >= 2 (FR-027)
        if (version >= 2)
        {
            // Harmonic Level (plain 0.0-2.0, normalized = plain / 2.0)
            if (streamer.readFloat(floatVal))
            {
                double normalized = static_cast<double>(
                    std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
                setParamNormalized(kHarmonicLevelId, normalized);
            }

            // Residual Level (plain 0.0-2.0, normalized = plain / 2.0)
            if (streamer.readFloat(floatVal))
            {
                double normalized = static_cast<double>(
                    std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
                setParamNormalized(kResidualLevelId, normalized);
            }

            // Residual Brightness (plain -1.0 to +1.0, normalized = (plain + 1.0) / 2.0)
            if (streamer.readFloat(floatVal))
            {
                double normalized = static_cast<double>(
                    std::clamp(floatVal, -1.0f, 1.0f) + 1.0f) / 2.0;
                setParamNormalized(kResidualBrightnessId, normalized);
            }

            // Transient Emphasis (plain 0.0-2.0, normalized = plain / 2.0)
            if (streamer.readFloat(floatVal))
            {
                double normalized = static_cast<double>(
                    std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
                setParamNormalized(kTransientEmphasisId, normalized);
            }
        }

        // Controller needs to skip residual frames data to reach M3 parameters
        // The processor state contains residual frame data between M2 and M3 sections
        if (version >= 2)
        {
            // Skip residual frames block (mirrors processor getState format)
            Steinberg::int32 residualFrameCount = 0;
            Steinberg::int32 analysisFFTSizeInt = 0;
            Steinberg::int32 analysisHopSizeInt = 0;

            if (streamer.readInt32(residualFrameCount) &&
                streamer.readInt32(analysisFFTSizeInt) &&
                streamer.readInt32(analysisHopSizeInt) &&
                residualFrameCount > 0)
            {
                // Skip each frame: 16 floats (bandEnergies) + 1 float (totalEnergy) + 1 int8 (transientFlag)
                for (Steinberg::int32 f = 0; f < residualFrameCount; ++f)
                {
                    for (size_t b = 0; b < 16; ++b) // kResidualBands = 16
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

        // M3: Read sidechain parameters if version >= 3
        if (version >= 3)
        {
            Steinberg::int32 inputSourceInt = 0;
            Steinberg::int32 latencyModeInt = 0;
            if (streamer.readInt32(inputSourceInt))
            {
                setParamNormalized(kInputSourceId,
                    inputSourceInt > 0 ? 1.0 : 0.0);
            }
            if (streamer.readInt32(latencyModeInt))
            {
                setParamNormalized(kLatencyModeId,
                    latencyModeInt > 0 ? 1.0 : 0.0);
            }
        }

        // M4: Read musical control parameters if version >= 4
        if (version >= 4)
        {
            Steinberg::int8 freezeState = 0;
            if (streamer.readInt8(freezeState))
            {
                setParamNormalized(kFreezeId,
                    freezeState ? 1.0 : 0.0);
            }

            float morphPos = 0.0f;
            if (streamer.readFloat(morphPos))
            {
                setParamNormalized(kMorphPositionId,
                    static_cast<double>(std::clamp(morphPos, 0.0f, 1.0f)));
            }

            Steinberg::int32 filterType = 0;
            if (streamer.readInt32(filterType))
            {
                setParamNormalized(kHarmonicFilterTypeId,
                    static_cast<double>(std::clamp(
                        static_cast<float>(filterType) / 4.0f, 0.0f, 1.0f)));
            }

            float resp = 0.5f;
            if (streamer.readFloat(resp))
            {
                setParamNormalized(kResponsivenessId,
                    static_cast<double>(std::clamp(resp, 0.0f, 1.0f)));
            }
        }
        else
        {
            // Default M4 values for older states
            setParamNormalized(kFreezeId, 0.0);
            setParamNormalized(kMorphPositionId, 0.0);
            setParamNormalized(kHarmonicFilterTypeId, 0.0);  // All-Pass
            setParamNormalized(kResponsivenessId, 0.5);
        }

        // M5: Read harmonic memory parameters if version >= 5
        if (version >= 5)
        {
            // Read selected slot index and set parameter
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
            // (controller does not store snapshot binary)
            for (int s = 0; s < 8; ++s)
            {
                Steinberg::int8 occupiedByte = 0;
                if (!streamer.readInt8(occupiedByte))
                    break;

                if (occupiedByte != 0)
                {
                    // Skip snapshot data: f0Reference (float) + numPartials (int32)
                    // + 48*4 floats (relativeFreqs, normalizedAmps, phases, inharmonicDeviation)
                    // + 16 floats (residualBands)
                    // + 4 floats (residualEnergy, globalAmplitude, spectralCentroid, brightness)
                    // Total: 1 float + 1 int32 + (48*4 + 16 + 4) floats = 1 + 1 + 212 = 213 reads
                    float skipFloat = 0.0f;
                    Steinberg::int32 skipInt = 0;

                    streamer.readFloat(skipFloat);  // f0Reference
                    streamer.readInt32(skipInt);     // numPartials

                    // 48*4 = 192 floats for per-partial arrays
                    for (int i = 0; i < 48 * 4; ++i)
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
        else
        {
            // Default M5 values for older states
            setParamNormalized(kMemorySlotId, 0.0);
            setParamNormalized(kMemoryCaptureId, 0.0);
            setParamNormalized(kMemoryRecallId, 0.0);
        }

        // M6: Read creative extension parameters if version >= 6
        if (version >= 6)
        {
            float m6Val = 0.0f;
            // 31 normalized floats in data-model.md v6 state layout order
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
        else
        {
            // Default M6 values for v5 and older states
            setParamNormalized(kTimbralBlendId, 1.0);
            setParamNormalized(kStereoSpreadId, 0.0);
            setParamNormalized(kEvolutionEnableId, 0.0);
            setParamNormalized(kEvolutionSpeedId, 0.0);
            setParamNormalized(kEvolutionDepthId, 0.5);
            setParamNormalized(kEvolutionModeId, 0.0);
            setParamNormalized(kMod1EnableId, 0.0);
            setParamNormalized(kMod1WaveformId, 0.0);
            setParamNormalized(kMod1RateId, 0.0);
            setParamNormalized(kMod1DepthId, 0.0);
            setParamNormalized(kMod1RangeStartId, 0.0);
            setParamNormalized(kMod1RangeEndId, 1.0);
            setParamNormalized(kMod1TargetId, 0.0);
            setParamNormalized(kMod2EnableId, 0.0);
            setParamNormalized(kMod2WaveformId, 0.0);
            setParamNormalized(kMod2RateId, 0.0);
            setParamNormalized(kMod2DepthId, 0.0);
            setParamNormalized(kMod2RangeStartId, 0.0);
            setParamNormalized(kMod2RangeEndId, 1.0);
            setParamNormalized(kMod2TargetId, 0.0);
            setParamNormalized(kDetuneSpreadId, 0.0);
            setParamNormalized(kBlendEnableId, 0.0);
            setParamNormalized(kBlendSlotWeight1Id, 0.0);
            setParamNormalized(kBlendSlotWeight2Id, 0.0);
            setParamNormalized(kBlendSlotWeight3Id, 0.0);
            setParamNormalized(kBlendSlotWeight4Id, 0.0);
            setParamNormalized(kBlendSlotWeight5Id, 0.0);
            setParamNormalized(kBlendSlotWeight6Id, 0.0);
            setParamNormalized(kBlendSlotWeight7Id, 0.0);
            setParamNormalized(kBlendSlotWeight8Id, 0.0);
            setParamNormalized(kBlendLiveWeightId, 0.0);
        }

        // --- Spec A: Harmonic Physics parameters (v7) ---
        if (version >= 7)
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
        else
        {
            // Default harmonic physics values for v6 and older states
            setParamNormalized(kWarmthId, 0.0);
            setParamNormalized(kCouplingId, 0.0);
            setParamNormalized(kStabilityId, 0.0);
            setParamNormalized(kEntropyId, 0.0);
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

    // Null all custom view pointers (VSTGUI owns the views)
    harmonicDisplayView_ = nullptr;
    confidenceIndicatorView_ = nullptr;
    memorySlotStatusView_ = nullptr;
    evolutionPositionView_ = nullptr;
    modActivityView0_ = nullptr;
    modActivityView1_ = nullptr;
    sampleFilenameLabel_ = nullptr;
    sampleLoadContainer_ = nullptr;

    activeEditor_ = nullptr;

    // Defensive counter reset
    modInstanceCounter_ = 0;
}

// ==============================================================================
// notify (T015: FR-048 IMessage protocol)
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::notify(
    Steinberg::Vst::IMessage* message)
{
    if (!message)
        return Steinberg::kInvalidArgument;

    // Display data from processor (FR-048)
    if (std::strcmp(message->getMessageID(), "DisplayData") == 0)
    {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        const void* data = nullptr;
        Steinberg::uint32 dataSize = 0;
        if (attrs->getBinary("data", data, dataSize) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        // Validate size matches DisplayData struct
        if (dataSize != sizeof(DisplayData))
            return Steinberg::kResultFalse;

        std::memcpy(&cachedDisplayData_, data, sizeof(DisplayData));
        return Steinberg::kResultOk;
    }

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

    return EditControllerEx1::notify(message);
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
// onDisplayTimerFired (T016: FR-049)
// ==============================================================================
void Controller::onDisplayTimerFired()
{
    // Update sample load panel visibility (cheap check every 30ms)
    updateSampleLoadVisibility();

    // Check if we have new data (frame counter changed)
    if (cachedDisplayData_.frameCounter == lastProcessedFrameCounter_)
        return;

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

} // namespace Innexus
