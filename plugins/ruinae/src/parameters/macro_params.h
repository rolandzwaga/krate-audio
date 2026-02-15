#pragma once
#include "plugin_ids.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct MacroParams {
    std::atomic<float> values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

inline void handleMacroParamChange(
    MacroParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kMacro1ValueId:
            params.values[0].store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kMacro2ValueId:
            params.values[1].store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kMacro3ValueId:
            params.values[2].store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kMacro4ValueId:
            params.values[3].store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerMacroParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Macro 1"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kMacro1ValueId);
    parameters.addParameter(STR16("Macro 2"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kMacro2ValueId);
    parameters.addParameter(STR16("Macro 3"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kMacro3ValueId);
    parameters.addParameter(STR16("Macro 4"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kMacro4ValueId);
}

inline Steinberg::tresult formatMacroParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    if (id >= kMacro1ValueId && id <= kMacro4ValueId) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

inline void saveMacroParams(const MacroParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.values[0].load(std::memory_order_relaxed));
    streamer.writeFloat(params.values[1].load(std::memory_order_relaxed));
    streamer.writeFloat(params.values[2].load(std::memory_order_relaxed));
    streamer.writeFloat(params.values[3].load(std::memory_order_relaxed));
}

inline bool loadMacroParams(MacroParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;
    if (!streamer.readFloat(fv)) { return false; } params.values[0].store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.values[1].store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.values[2].store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.values[3].store(fv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadMacroParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f;
    if (streamer.readFloat(fv)) setParam(kMacro1ValueId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kMacro2ValueId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kMacro3ValueId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kMacro4ValueId, static_cast<double>(fv));
}

} // namespace Ruinae
