#pragma once

// ==============================================================================
// OSC B Parameters (ID 200-299)
// ==============================================================================
// Binding of the shared oscillator parameter module to the OSC B block. The
// implementation lives in osc_params.h; this header exists so call sites keep
// their bank-specific names.

#include "parameters/osc_params.h"

namespace Ruinae {

using OscBParams = OscParams;

inline void handleOscBParamChange(
    OscBParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    handleOscParamChange(params, kOscBTypeId, id, value);
}

inline void registerOscBParams(Steinberg::Vst::ParameterContainer& parameters) {
    registerOscParams(parameters, kOscBTypeId, STR16("OSC B "));
}

inline Steinberg::tresult formatOscBParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    return formatOscParam(kOscBTypeId, id, value, string);
}

inline void saveOscBParams(const OscBParams& params, Steinberg::IBStreamer& streamer) {
    saveOscParams(params, streamer);
}

inline bool loadOscBParams(OscBParams& params, Steinberg::IBStreamer& streamer) {
    return loadOscParams(params, streamer);
}

template<typename SetParamFunc>
inline void loadOscBParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadOscParamsToController(kOscBTypeId, streamer, setParam);
}

} // namespace Ruinae
