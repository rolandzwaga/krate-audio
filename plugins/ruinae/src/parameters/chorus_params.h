#pragma once

// ==============================================================================
// Chorus Parameters (ID 1920-1929)
// ==============================================================================
// Binding of the shared modulation-effect parameter module to the chorus block.
// The implementation lives in modulation_effect_params.h.

#include "parameters/modulation_effect_params.h"

namespace Ruinae {

using RuinaeChorusParams = ModEffectParams<kChorusConfig>;

inline void handleChorusParamChange(
    RuinaeChorusParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    handleModEffectParamChange(kChorusConfig, params, id, value);
}

inline void registerChorusParams(Steinberg::Vst::ParameterContainer& parameters) {
    registerModEffectParams(parameters, kChorusConfig, STR16("Chorus "));
}

inline Steinberg::tresult formatChorusParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    return formatModEffectParam(kChorusConfig, id, value, string);
}

inline void saveChorusParams(const RuinaeChorusParams& params,
                              Steinberg::IBStreamer& streamer) {
    saveModEffectParams(kChorusConfig, params, streamer);
}

inline bool loadChorusParams(RuinaeChorusParams& params,
                              Steinberg::IBStreamer& streamer) {
    return loadModEffectParams(kChorusConfig, params, streamer);
}

template<typename SetParamFunc>
inline void loadChorusParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadModEffectParamsToController(kChorusConfig, streamer, setParam);
}

} // namespace Ruinae
