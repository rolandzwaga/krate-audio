#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

namespace Ruinae {

// =============================================================================
// FX Enable Parameter Registration
// =============================================================================

inline void registerFxEnableParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Delay Enabled"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kDelayEnabledId);
    parameters.addParameter(STR16("Reverb Enabled"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kReverbEnabledId);
    // kPhaserEnabledId (1502) is DEPRECATED -- replaced by kModulationTypeId (1918).
    // Modulation type: 0=None, 1=Phaser, 2=Flanger (discrete 3-step parameter)
    parameters.addParameter(createDropdownParameter(
        STR16("Modulation Type"), kModulationTypeId,
        {STR16("None"), STR16("Phaser"), STR16("Flanger")}));
    parameters.addParameter(STR16("Harmonizer Enabled"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kHarmonizerEnabledId);
}

} // namespace Ruinae
