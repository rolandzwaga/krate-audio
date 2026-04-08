// ==============================================================================
// Membrum Controller -- Parameter registration, state sync
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"

namespace Membrum {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::initialize(FUnknown* context)
{
    auto result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    // FR-020: Register 5 parameters with correct names, ranges, defaults
    parameters.addParameter(
        new RangeParameter(STR16("Material"), kMaterialId, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));

    parameters.addParameter(
        new RangeParameter(STR16("Size"), kSizeId, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));

    parameters.addParameter(
        new RangeParameter(STR16("Resonance"), kDecayId, nullptr,
                           0.0, 1.0, 0.3, 0, ParameterInfo::kCanAutomate));

    parameters.addParameter(
        new RangeParameter(STR16("Strike Point"), kStrikePositionId, nullptr,
                           0.0, 1.0, 0.3, 0, ParameterInfo::kCanAutomate));

    parameters.addParameter(
        new RangeParameter(STR16("Level"), kLevelId, STR16("dB"),
                           0.0, 1.0, 0.8, 0, ParameterInfo::kCanAutomate));

    return kResultOk;
}

// FR-016: Read processor state and sync controller parameter values
tresult PLUGIN_API Controller::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    // Read version
    int32 version = 0;
    if (state->read(&version, sizeof(version), nullptr) != kResultOk)
        return kResultFalse;

    // Read parameters in order: Material, Size, Decay, StrikePosition, Level
    const ParamID paramOrder[] = {
        kMaterialId, kSizeId, kDecayId, kStrikePositionId, kLevelId};
    const double defaults[] = {0.5, 0.5, 0.3, 0.3, 0.8};

    for (int i = 0; i < 5; ++i)
    {
        double value = defaults[i];
        if (state->read(&value, sizeof(value), nullptr) != kResultOk)
        {
            // Fewer fields than expected -- use defaults for remaining
            setParamNormalized(paramOrder[i], defaults[i]);
            continue;
        }
        setParamNormalized(paramOrder[i], value);
    }

    return kResultOk;
}

// FR-021: No custom UI in Phase 1
IPlugView* PLUGIN_API Controller::createView(const char* /*name*/)
{
    return nullptr;
}

} // namespace Membrum
