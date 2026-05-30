#pragma once
// Thin shim preserving the historical LFO1 API. The implementation is the
// base-ID-parameterized code in lfo_params.h, shared with LFO2. The shared map
// helpers (lfoRateFromNormalized / lfoFadeIn* / lfoQuantize* / kQuantizeStepCount)
// are defined in lfo_params.h and remain visible to sibling param files that
// include this header.
#include "parameters/lfo_params.h"

namespace Ruinae {

using LFO1Params = LFOParams;

inline void handleLFO1ParamChange(
    LFO1Params& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    handleLFOParamChange(params, kLFO1RateId, id, value);
}

inline void registerLFO1Params(Steinberg::Vst::ParameterContainer& parameters) {
    registerLFOParams(parameters, kLFO1RateId, STR16("LFO 1 "));
}

inline Steinberg::tresult formatLFO1Param(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    return formatLFOParam(kLFO1RateId, id, value, string);
}

inline void saveLFO1Params(const LFO1Params& params, Steinberg::IBStreamer& streamer) {
    saveLFOParams(params, streamer);
}

inline bool loadLFO1Params(LFO1Params& params, Steinberg::IBStreamer& streamer) {
    return loadLFOParams(params, streamer);
}

inline void saveLFO1ExtendedParams(const LFO1Params& params, Steinberg::IBStreamer& streamer) {
    saveLFOExtendedParams(params, streamer);
}

inline bool loadLFO1ExtendedParams(LFO1Params& params, Steinberg::IBStreamer& streamer) {
    return loadLFOExtendedParams(params, streamer);
}

template<typename SetParamFunc>
inline void loadLFO1ParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadLFOParamsToController(kLFO1RateId, streamer, setParam);
}

template<typename SetParamFunc>
inline void loadLFO1ExtendedParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadLFOExtendedParamsToController(kLFO1RateId, streamer, setParam);
}

} // namespace Ruinae
