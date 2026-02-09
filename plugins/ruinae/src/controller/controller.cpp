// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"
#include "preset/ruinae_preset_config.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

namespace Ruinae {

// ==============================================================================
// Destructor
// ==============================================================================

Controller::~Controller() = default;

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // ==========================================================================
    // Register Parameters
    // Constitution Principle V: All values normalized (0.0 to 1.0)
    // ==========================================================================

    // --- Global Parameters (0-99) ---
    parameters.addParameter(STR16("Master Gain"), STR16("%"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kMasterGainId);

    parameters.addParameter(
        new Steinberg::Vst::StringListParameter(
            STR16("Voice Mode"), kVoiceModeId, nullptr,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList));
    if (auto* p = static_cast<Steinberg::Vst::StringListParameter*>(
            parameters.getParameter(kVoiceModeId))) {
        p->appendString(STR16("Poly"));
        p->appendString(STR16("Mono"));
    }

    parameters.addParameter(STR16("Polyphony"), STR16(""), 15, 7.0 / 15.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kPolyphonyId);

    parameters.addParameter(STR16("Soft Limit"), STR16(""), 1, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kSoftLimitId);

    // --- OSC A Parameters (100-199) ---
    parameters.addParameter(STR16("OSC A Type"), STR16(""), 9, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kOscATypeId);
    parameters.addParameter(STR16("OSC A Tune"), STR16("st"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kOscATuneId);
    parameters.addParameter(STR16("OSC A Fine"), STR16("ct"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kOscAFineId);
    parameters.addParameter(STR16("OSC A Level"), STR16("%"), 0, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kOscALevelId);

    // --- OSC B Parameters (200-299) ---
    parameters.addParameter(STR16("OSC B Type"), STR16(""), 9, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kOscBTypeId);
    parameters.addParameter(STR16("OSC B Tune"), STR16("st"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kOscBTuneId);
    parameters.addParameter(STR16("OSC B Fine"), STR16("ct"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kOscBFineId);
    parameters.addParameter(STR16("OSC B Level"), STR16("%"), 0, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kOscBLevelId);

    // --- Mixer Parameters (300-399) ---
    parameters.addParameter(STR16("Mix Mode"), STR16(""), 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kMixerModeId);
    parameters.addParameter(STR16("Mix Position"), STR16("%"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kMixerPositionId);

    // --- Filter Parameters (400-499) ---
    parameters.addParameter(STR16("Filter Type"), STR16(""), 3, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterTypeId);
    parameters.addParameter(STR16("Filter Cutoff"), STR16("Hz"), 0, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterCutoffId);
    parameters.addParameter(STR16("Filter Resonance"), STR16(""), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterResonanceId);
    parameters.addParameter(STR16("Filter Env Amount"), STR16("st"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterEnvAmountId);
    parameters.addParameter(STR16("Filter Key Track"), STR16("%"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterKeyTrackId);

    // --- Distortion Parameters (500-599) ---
    parameters.addParameter(STR16("Distortion Type"), STR16(""), 5, 0.0 / 5.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kDistortionTypeId);
    parameters.addParameter(STR16("Distortion Drive"), STR16("%"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kDistortionDriveId);
    parameters.addParameter(STR16("Distortion Character"), STR16(""), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kDistortionCharacterId);
    parameters.addParameter(STR16("Distortion Mix"), STR16("%"), 0, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kDistortionMixId);

    // --- Amp Envelope Parameters (700-799) ---
    parameters.addParameter(STR16("Amp Attack"), STR16("ms"), 0, 0.01,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kAmpEnvAttackId);
    parameters.addParameter(STR16("Amp Decay"), STR16("ms"), 0, 0.1,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kAmpEnvDecayId);
    parameters.addParameter(STR16("Amp Sustain"), STR16("%"), 0, 0.8,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kAmpEnvSustainId);
    parameters.addParameter(STR16("Amp Release"), STR16("ms"), 0, 0.05,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kAmpEnvReleaseId);

    // --- Filter Envelope Parameters (800-899) ---
    parameters.addParameter(STR16("Filter Env Attack"), STR16("ms"), 0, 0.01,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterEnvAttackId);
    parameters.addParameter(STR16("Filter Env Decay"), STR16("ms"), 0, 0.2,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterEnvDecayId);
    parameters.addParameter(STR16("Filter Env Sustain"), STR16("%"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterEnvSustainId);
    parameters.addParameter(STR16("Filter Env Release"), STR16("ms"), 0, 0.1,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kFilterEnvReleaseId);

    // --- Reverb Parameters (1700-1799) ---
    parameters.addParameter(STR16("Reverb Size"), STR16(""), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kReverbSizeId);
    parameters.addParameter(STR16("Reverb Damping"), STR16(""), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kReverbDampingId);
    parameters.addParameter(STR16("Reverb Width"), STR16(""), 0, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kReverbWidthId);
    parameters.addParameter(STR16("Reverb Mix"), STR16("%"), 0, 0.3,
        Steinberg::Vst::ParameterInfo::kCanAutomate, kReverbMixId);

    // ==========================================================================
    // Initialize Preset Manager
    // ==========================================================================
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeRuinaePresetConfig(), nullptr, this);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    presetManager_.reset();
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(
    Steinberg::IBStream* state) {

    if (!state) {
        return Steinberg::kResultFalse;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read in same order as Processor::getState()
    float gain = 1.0f;
    if (streamer.readFloat(gain)) {
        setParamNormalized(kMasterGainId, gain / 2.0);
    }

    Steinberg::int32 voiceMode = 0;
    if (streamer.readInt32(voiceMode)) {
        setParamNormalized(kVoiceModeId, static_cast<double>(voiceMode));
    }

    Steinberg::int32 polyphony = 8;
    if (streamer.readInt32(polyphony)) {
        setParamNormalized(kPolyphonyId, (polyphony - 1.0) / 15.0);
    }

    Steinberg::int32 softLimit = 1;
    if (streamer.readInt32(softLimit)) {
        setParamNormalized(kSoftLimitId, softLimit ? 1.0 : 0.0);
    }

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    // Controller-specific state (UI settings, etc.)
    // Currently no controller-only state to save
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    // Controller-specific state restore
    return Steinberg::kResultTrue;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
    if (Steinberg::FIDStringsEqual(name, Steinberg::Vst::ViewType::kEditor)) {
        auto* editor = new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
        return editor;
    }
    return nullptr;
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string) {
    // Use default implementation for now
    return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& valueNormalized) {
    // Use default implementation for now
    return EditControllerEx1::getParamValueByString(id, string, valueNormalized);
}

// ==============================================================================
// VST3EditorDelegate
// ==============================================================================

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    if (activeEditor_ == editor) {
        activeEditor_ = nullptr;
    }
}

} // namespace Ruinae
