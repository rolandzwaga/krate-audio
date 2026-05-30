#pragma once
// Thin shim preserving the historical LFO2 API. The implementation is the
// base-ID-parameterized code in lfo_params.h, shared with LFO1.
#include "parameters/lfo_params.h"

namespace Ruinae {

using LFO2Params = LFOParams;

inline void handleLFO2ParamChange(
    LFO2Params& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    handleLFOParamChange(params, kLFO2RateId, id, value);
}

inline void registerLFO2Params(Steinberg::Vst::ParameterContainer& parameters) {
    registerLFOParams(parameters, kLFO2RateId, STR16("LFO 2 "));
}

inline Steinberg::tresult formatLFO2Param(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    return formatLFOParam(kLFO2RateId, id, value, string);
}

inline void saveLFO2Params(const LFO2Params& params, Steinberg::IBStreamer& streamer) {
    saveLFOParams(params, streamer);
}

inline bool loadLFO2Params(LFO2Params& params, Steinberg::IBStreamer& streamer) {
    return loadLFOParams(params, streamer);
}

inline void saveLFO2ExtendedParams(const LFO2Params& params, Steinberg::IBStreamer& streamer) {
    saveLFOExtendedParams(params, streamer);
}

inline bool loadLFO2ExtendedParams(LFO2Params& params, Steinberg::IBStreamer& streamer) {
    return loadLFOExtendedParams(params, streamer);
}

template<typename SetParamFunc>
inline void loadLFO2ParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadLFOParamsToController(kLFO2RateId, streamer, setParam);
}

template<typename SetParamFunc>
inline void loadLFO2ExtendedParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadLFOExtendedParamsToController(kLFO2RateId, streamer, setParam);
}

} // namespace Ruinae
