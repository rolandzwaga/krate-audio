// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "parameters/innexus_params.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

namespace Innexus {

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

    return Steinberg::kResultOk;
}

// ==============================================================================
// Terminate
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::terminate()
{
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

        // Skip sample file path (controller does not need it)
    }

    return Steinberg::kResultOk;
}

} // namespace Innexus
