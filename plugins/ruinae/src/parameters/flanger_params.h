#pragma once

// ==============================================================================
// Flanger Parameters (ID 1910-1919)
// ==============================================================================
// Binding of the shared modulation-effect parameter module to the flanger block.
// The implementation lives in modulation_effect_params.h.

#include "parameters/modulation_effect_params.h"

namespace Ruinae {

using RuinaeFlangerParams = ModEffectParams<kFlangerConfig>;

inline void handleFlangerParamChange(
    RuinaeFlangerParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    handleModEffectParamChange(kFlangerConfig, params, id, value);
}

inline void registerFlangerParams(Steinberg::Vst::ParameterContainer& parameters) {
    registerModEffectParams(parameters, kFlangerConfig, STR16("Flanger "));
}

inline Steinberg::tresult formatFlangerParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    return formatModEffectParam(kFlangerConfig, id, value, string);
}

inline void saveFlangerParams(const RuinaeFlangerParams& params,
                              Steinberg::IBStreamer& streamer) {
    saveModEffectParams(kFlangerConfig, params, streamer);
}

inline bool loadFlangerParams(RuinaeFlangerParams& params,
                              Steinberg::IBStreamer& streamer) {
    return loadModEffectParams(kFlangerConfig, params, streamer);
}

template<typename SetParamFunc>
inline void loadFlangerParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadModEffectParamsToController(kFlangerConfig, streamer, setParam);
}

} // namespace Ruinae
