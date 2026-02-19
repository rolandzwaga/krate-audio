#pragma once
#include "plugin_ids.h"
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
    parameters.addParameter(STR16("Phaser Enabled"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kPhaserEnabledId);
    parameters.addParameter(STR16("Harmonizer Enabled"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kHarmonizerEnabledId);
}

} // namespace Ruinae
