// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

namespace Disrumpo {

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    // Always call parent first
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // ==========================================================================
    // FR-015: Register skeleton parameters
    // Constitution Principle V: All values are normalized 0.0 to 1.0
    // ==========================================================================

    // Input Gain: 0.0-1.0 (normalized), default 0.5 (0 dB)
    parameters.addParameter(
        STR16("Input Gain"),           // title
        STR16("dB"),                   // units
        0,                             // stepCount (0 = continuous)
        0.5,                           // defaultValueNormalized
        Steinberg::Vst::ParameterInfo::kCanAutomate,  // flags
        kInputGainId                   // tag (parameter ID)
    );

    // Output Gain: 0.0-1.0 (normalized), default 0.5 (0 dB)
    parameters.addParameter(
        STR16("Output Gain"),          // title
        STR16("dB"),                   // units
        0,                             // stepCount (0 = continuous)
        0.5,                           // defaultValueNormalized
        Steinberg::Vst::ParameterInfo::kCanAutomate,  // flags
        kOutputGainId                  // tag (parameter ID)
    );

    // Global Mix: 0.0-1.0 (normalized), default 1.0 (100% wet)
    parameters.addParameter(
        STR16("Mix"),                  // title
        STR16("%"),                    // units
        0,                             // stepCount (0 = continuous)
        1.0,                           // defaultValueNormalized
        Steinberg::Vst::ParameterInfo::kCanAutomate,  // flags
        kGlobalMixId                   // tag (parameter ID)
    );

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    // Cleanup any resources allocated in initialize()
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
    // FR-016: Sync from processor state
    // This method receives the processor's state and synchronizes the controller

    if (!state) {
        return Steinberg::kResultFalse;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version first (same format as Processor::setState)
    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultFalse;
    }

    if (version < 1) {
        return Steinberg::kResultFalse;
    }

    // Read parameters in same order as Processor
    float inputGain = 0.5f;
    float outputGain = 0.5f;
    float globalMix = 1.0f;

    if (!streamer.readFloat(inputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.readFloat(outputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.readFloat(globalMix)) {
        return Steinberg::kResultFalse;
    }

    // Update controller's parameter values (for UI display)
    setParamNormalized(kInputGainId, inputGain);
    setParamNormalized(kOutputGainId, outputGain);
    setParamNormalized(kGlobalMixId, globalMix);

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    // Save controller-specific state (UI settings, etc.)
    // For the skeleton, we have no controller-specific state to save
    // Just write a version marker for future extensibility

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write controller state version (separate from preset version)
    if (!streamer.writeInt32(1)) {
        return Steinberg::kResultFalse;
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    // Restore controller-specific state
    // For the skeleton, we have no controller-specific state to restore

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read controller state version
    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        // No controller state is OK - just use defaults
        return Steinberg::kResultOk;
    }

    // Future: handle version migration for controller-specific settings

    return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
    // FR-017: Return nullptr (no UI in skeleton)
    // UI will be added in Week 4-5 per roadmap

    (void)name;  // Suppress unused parameter warning
    return nullptr;
}

} // namespace Disrumpo
