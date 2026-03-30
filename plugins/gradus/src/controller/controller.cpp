// ==============================================================================
// Gradus Controller Implementation
// ==============================================================================

#include "controller.h"
#include "../plugin_ids.h"
#include "../version.h"
#include "../parameters/arpeggiator_params.h"
#include "../preset/gradus_preset_config.h"

#include "ui/arp_lane_editor.h"
#include "ui/arp_lane_container.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Gradus {

tresult PLUGIN_API Controller::initialize(FUnknown* context)
{
    tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    // Register all arpeggiator parameters (shared IDs with Ruinae)
    registerArpParams(parameters);

    // Audition sound parameters
    parameters.addParameter(STR16("Audition Enabled"), nullptr, 1,
        1.0, ParameterInfo::kCanAutomate, kAuditionEnabledId);

    parameters.addParameter(
        new RangeParameter(STR16("Audition Volume"), kAuditionVolumeId,
            STR16("%"), 0.0, 1.0, 0.7,
            0, ParameterInfo::kCanAutomate));

    parameters.addParameter(createDropdownParameter(
        STR16("Audition Waveform"), kAuditionWaveformId,
        { STR16("Sine"), STR16("Saw"), STR16("Square") }));

    parameters.addParameter(
        new RangeParameter(STR16("Audition Decay"), kAuditionDecayId,
            STR16("ms"), 10.0, 2000.0, 200.0,
            0, ParameterInfo::kCanAutomate));

    // Preset manager
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeGradusPresetConfig(), nullptr, this);
    presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return createComponentStateStream();
    });
    presetManager_->setLoadProvider(
        [this](Steinberg::IBStream* stream,
               const Krate::Plugins::PresetInfo& /*info*/) -> bool {
            return loadComponentStateWithNotify(stream);
        });

    return kResultOk;
}

tresult PLUGIN_API Controller::terminate()
{
    presetManager_.reset();
    return EditControllerEx1::terminate();
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    // Read state version
    int32 version = 0;
    if (!streamer.readInt32(version))
        return kResultFalse;

    // Load arp params to controller (restores UI to match saved state)
    auto setParam = [this](ParamID id, ParamValue value) {
        setParamNormalized(id, value);
    };
    loadArpParamsToController(streamer, setParam, version);

    // Audition params are session-only — not restored from presets

    return kResultOk;
}

// ==============================================================================
// Parameter Display Formatting
// ==============================================================================

tresult PLUGIN_API Controller::getParamStringByValue(
    ParamID id, ParamValue valueNormalized, String128 string)
{
    // Try arp params first
    if (formatArpParam(id, valueNormalized, string) == kResultTrue)
        return kResultTrue;

    // Audition params
    char buf[32];
    switch (id) {
        case kAuditionVolumeId:
            snprintf(buf, sizeof(buf), "%.0f%%", valueNormalized * 100.0);
            Steinberg::UString(string, 128).fromAscii(buf);
            return kResultTrue;
        case kAuditionDecayId: {
            float ms = static_cast<float>(10.0 + valueNormalized * 1990.0);
            if (ms >= 1000.0f)
                snprintf(buf, sizeof(buf), "%.2f s", ms / 1000.0f);
            else
                snprintf(buf, sizeof(buf), "%.0f ms", ms);
            Steinberg::UString(string, 128).fromAscii(buf);
            return kResultTrue;
        }
        default:
            break;
    }

    return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
}

// ==============================================================================
// IMessage Handler
// ==============================================================================

tresult PLUGIN_API Controller::notify(IMessage* message)
{
    if (!message) return kResultFalse;

    // Handle arp skip events from processor
    if (strcmp(message->getMessageID(), "ArpSkip") == 0) {
        auto* attrs = message->getAttributes();
        int64 lane = 0;
        int64 step = 0;
        if (attrs && attrs->getInt("lane", lane) == kResultOk &&
            attrs->getInt("step", step) == kResultOk) {
            handleArpSkipEvent(static_cast<int>(lane), static_cast<int>(step));
        }
        return kResultOk;
    }

    return EditControllerEx1::notify(message);
}

// ==============================================================================
// VST3Editor Delegate
// ==============================================================================

IPlugView* PLUGIN_API Controller::createView(FIDString name)
{
    if (FIDStringsEqual(name, ViewType::kEditor))
    {
        auto* view = new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
        return view;
    }
    return nullptr;
}

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    [[maybe_unused]] const VSTGUI::IUIDescription* description,
    [[maybe_unused]] VSTGUI::VST3Editor* editor)
{
    std::string viewName(name ? name : "");
    VSTGUI::CRect viewRect;
    if (auto* sizeAttr = attributes.getAttributeValue("size")) {
        VSTGUI::CPoint p;
        VSTGUI::UIAttributes::stringToPoint(*sizeAttr, p);
        viewRect = VSTGUI::CRect(0, 0, p.x, p.y);
    }

    if (viewName == "PresetBrowserButton")
        return createPresetButton(viewRect, true);
    if (viewName == "SavePresetButton")
        return createPresetButton(viewRect, false);

    return nullptr;
}

void Controller::didOpen(VSTGUI::VST3Editor* editor)
{
    activeEditor_ = editor;
    wireCopyPasteCallbacks();

    // Create preset browser and save dialog overlay views
    if (auto* frame = editor->getFrame()) {
        auto frameSize = frame->getViewSize();

        if (presetManager_) {
            if (!presetBrowserView_) {
                presetBrowserView_ = new Krate::Plugins::PresetBrowserView(
                    frameSize, presetManager_.get(), getGradusTabLabels());
                frame->addView(presetBrowserView_);
            }
            if (!savePresetDialogView_) {
                savePresetDialogView_ = new Krate::Plugins::SavePresetDialogView(
                    frameSize, presetManager_.get(),
                    makeGradusPresetConfig().subcategoryNames);
                frame->addView(savePresetDialogView_);
            }
        }
    }
}

void Controller::willClose([[maybe_unused]] VSTGUI::VST3Editor* editor)
{
    // Null all lane pointers (VSTGUI owns their lifetime)
    velocityLane_ = nullptr;
    gateLane_ = nullptr;
    pitchLane_ = nullptr;
    ratchetLane_ = nullptr;
    modifierLane_ = nullptr;
    conditionLane_ = nullptr;
    chordLane_ = nullptr;
    inversionLane_ = nullptr;
    arpLaneContainer_ = nullptr;
    presetBrowserView_ = nullptr;
    savePresetDialogView_ = nullptr;
    activeEditor_ = nullptr;
}

} // namespace Gradus
