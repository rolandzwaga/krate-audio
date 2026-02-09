#pragma once

// ==============================================================================
// Mixer Parameters (ID 300-399)
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct MixerParams {
    std::atomic<int> mode{0};         // 0=Crossfade, 1=SpectralMorph
    std::atomic<float> position{0.5f}; // 0=A, 1=B
};

inline void handleMixerParamChange(
    MixerParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kMixerModeId:
            params.mode.store(
                std::clamp(static_cast<int>(value * (kMixModeCount - 1) + 0.5), 0, kMixModeCount - 1),
                std::memory_order_relaxed);
            break;
        case kMixerPositionId:
            params.position.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

inline void registerMixerParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(createDropdownParameter(
        STR16("Mix Mode"), kMixerModeId,
        {STR16("Crossfade"), STR16("Spectral Morph")}
    ));
    parameters.addParameter(STR16("Mix Position"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kMixerPositionId);
}

inline Steinberg::tresult formatMixerParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    if (id == kMixerPositionId) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

inline void saveMixerParams(const MixerParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.mode.load(std::memory_order_relaxed));
    streamer.writeFloat(params.position.load(std::memory_order_relaxed));
}

inline bool loadMixerParams(MixerParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (!streamer.readInt32(intVal)) return false;
    params.mode.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.position.store(floatVal, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadMixerParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (streamer.readInt32(intVal))
        setParam(kMixerModeId, static_cast<double>(intVal) / (kMixModeCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kMixerPositionId, static_cast<double>(floatVal));
}

} // namespace Ruinae
