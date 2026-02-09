// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"
#include "preset/ruinae_preset_config.h"

// Parameter pack headers (for registration, display, and controller sync)
#include "parameters/global_params.h"
#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"
#include "parameters/mixer_params.h"
#include "parameters/filter_params.h"
#include "parameters/distortion_params.h"
#include "parameters/trance_gate_params.h"
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"
#include "parameters/lfo1_params.h"
#include "parameters/lfo2_params.h"
#include "parameters/chaos_mod_params.h"
#include "parameters/mod_matrix_params.h"
#include "parameters/global_filter_params.h"
#include "parameters/freeze_params.h"
#include "parameters/delay_params.h"
#include "parameters/reverb_params.h"
#include "parameters/mono_mode_params.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

namespace Ruinae {

// State version must match processor
constexpr Steinberg::int32 kControllerStateVersion = 1;

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
    // Register All Parameters (19 sections)
    // ==========================================================================

    registerGlobalParams(parameters);
    registerOscAParams(parameters);
    registerOscBParams(parameters);
    registerMixerParams(parameters);
    registerFilterParams(parameters);
    registerDistortionParams(parameters);
    registerTranceGateParams(parameters);
    registerAmpEnvParams(parameters);
    registerFilterEnvParams(parameters);
    registerModEnvParams(parameters);
    registerLFO1Params(parameters);
    registerLFO2Params(parameters);
    registerChaosModParams(parameters);
    registerModMatrixParams(parameters);
    registerGlobalFilterParams(parameters);
    registerFreezeParams(parameters);
    registerDelayParams(parameters);
    registerReverbParams(parameters);
    registerMonoModeParams(parameters);

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

    // Read state version (must match Processor::getState format)
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultTrue; // Empty stream, keep defaults
    }

    // Lambda to sync controller parameter display
    auto setParam = [this](Steinberg::Vst::ParamID id, double value) {
        setParamNormalized(id, value);
    };

    if (version == 1) {
        // Sync all 19 parameter packs in same order as Processor::getState
        loadGlobalParamsToController(streamer, setParam);
        loadOscAParamsToController(streamer, setParam);
        loadOscBParamsToController(streamer, setParam);
        loadMixerParamsToController(streamer, setParam);
        loadFilterParamsToController(streamer, setParam);
        loadDistortionParamsToController(streamer, setParam);
        loadTranceGateParamsToController(streamer, setParam);
        loadAmpEnvParamsToController(streamer, setParam);
        loadFilterEnvParamsToController(streamer, setParam);
        loadModEnvParamsToController(streamer, setParam);
        loadLFO1ParamsToController(streamer, setParam);
        loadLFO2ParamsToController(streamer, setParam);
        loadChaosModParamsToController(streamer, setParam);
        loadModMatrixParamsToController(streamer, setParam);
        loadGlobalFilterParamsToController(streamer, setParam);
        loadFreezeParamsToController(streamer, setParam);
        loadDelayParamsToController(streamer, setParam);
        loadReverbParamsToController(streamer, setParam);
        loadMonoModeParamsToController(streamer, setParam);
    }
    // Unknown versions: keep defaults (fail closed)

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

    // Route to appropriate parameter pack formatter by ID range
    Steinberg::tresult result = Steinberg::kResultFalse;

    if (id <= kGlobalEndId) {
        result = formatGlobalParam(id, valueNormalized, string);
    } else if (id >= kOscABaseId && id <= kOscAEndId) {
        result = formatOscAParam(id, valueNormalized, string);
    } else if (id >= kOscBBaseId && id <= kOscBEndId) {
        result = formatOscBParam(id, valueNormalized, string);
    } else if (id >= kMixerBaseId && id <= kMixerEndId) {
        result = formatMixerParam(id, valueNormalized, string);
    } else if (id >= kFilterBaseId && id <= kFilterEndId) {
        result = formatFilterParam(id, valueNormalized, string);
    } else if (id >= kDistortionBaseId && id <= kDistortionEndId) {
        result = formatDistortionParam(id, valueNormalized, string);
    } else if (id >= kTranceGateBaseId && id <= kTranceGateEndId) {
        result = formatTranceGateParam(id, valueNormalized, string);
    } else if (id >= kAmpEnvBaseId && id <= kAmpEnvEndId) {
        result = formatAmpEnvParam(id, valueNormalized, string);
    } else if (id >= kFilterEnvBaseId && id <= kFilterEnvEndId) {
        result = formatFilterEnvParam(id, valueNormalized, string);
    } else if (id >= kModEnvBaseId && id <= kModEnvEndId) {
        result = formatModEnvParam(id, valueNormalized, string);
    } else if (id >= kLFO1BaseId && id <= kLFO1EndId) {
        result = formatLFO1Param(id, valueNormalized, string);
    } else if (id >= kLFO2BaseId && id <= kLFO2EndId) {
        result = formatLFO2Param(id, valueNormalized, string);
    } else if (id >= kChaosModBaseId && id <= kChaosModEndId) {
        result = formatChaosModParam(id, valueNormalized, string);
    } else if (id >= kModMatrixBaseId && id <= kModMatrixEndId) {
        result = formatModMatrixParam(id, valueNormalized, string);
    } else if (id >= kGlobalFilterBaseId && id <= kGlobalFilterEndId) {
        result = formatGlobalFilterParam(id, valueNormalized, string);
    } else if (id >= kFreezeBaseId && id <= kFreezeEndId) {
        result = formatFreezeParam(id, valueNormalized, string);
    } else if (id >= kDelayBaseId && id <= kDelayEndId) {
        result = formatDelayParam(id, valueNormalized, string);
    } else if (id >= kReverbBaseId && id <= kReverbEndId) {
        result = formatReverbParam(id, valueNormalized, string);
    } else if (id >= kMonoBaseId && id <= kMonoEndId) {
        result = formatMonoModeParam(id, valueNormalized, string);
    }

    // Fall back to default implementation for unhandled parameters
    // (StringListParameter handles its own formatting)
    if (result != Steinberg::kResultOk) {
        return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
    }
    return result;
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
