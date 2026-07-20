#pragma once

// ==============================================================================
// OSC A Parameters (ID 100-199)
// ==============================================================================
// Binding of the shared oscillator parameter module to the OSC A block. The
// implementation lives in osc_params.h; this header exists so call sites keep
// their bank-specific names.

#include "parameters/osc_params.h"

namespace Ruinae {

using OscAParams = OscParams;

inline void handleOscAParamChange(
    OscAParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    handleOscParamChange(params, kOscATypeId, id, value);
}

inline void registerOscAParams(Steinberg::Vst::ParameterContainer& parameters) {
    registerOscParams(parameters, kOscATypeId, STR16("OSC A "));
}

inline Steinberg::tresult formatOscAParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    return formatOscParam(kOscATypeId, id, value, string);
}

inline void saveOscAParams(const OscAParams& params, Steinberg::IBStreamer& streamer) {
    saveOscParams(params, streamer);
}

inline bool loadOscAParams(OscAParams& params, Steinberg::IBStreamer& streamer) {
    return loadOscParams(params, streamer);
}

template<typename SetParamFunc>
inline void loadOscAParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadOscParamsToController(kOscATypeId, streamer, setParam);
}

} // namespace Ruinae
