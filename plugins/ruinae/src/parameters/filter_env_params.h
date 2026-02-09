#pragma once
#include "plugin_ids.h"
#include "parameters/amp_env_params.h"  // for envTimeFromNormalized/envTimeToNormalized
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct FilterEnvParams {
    std::atomic<float> attackMs{10.0f};
    std::atomic<float> decayMs{200.0f};
    std::atomic<float> sustain{0.5f};
    std::atomic<float> releaseMs{300.0f};
};

inline void handleFilterEnvParamChange(
    FilterEnvParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kFilterEnvAttackId:
            params.attackMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        case kFilterEnvDecayId:
            params.decayMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        case kFilterEnvSustainId:
            params.sustain.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kFilterEnvReleaseId:
            params.releaseMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerFilterEnvParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Filter Env Attack"), STR16("ms"), 0, 0.1,
        ParameterInfo::kCanAutomate, kFilterEnvAttackId);
    parameters.addParameter(STR16("Filter Env Decay"), STR16("ms"), 0, 0.271,
        ParameterInfo::kCanAutomate, kFilterEnvDecayId);
    parameters.addParameter(STR16("Filter Env Sustain"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kFilterEnvSustainId);
    parameters.addParameter(STR16("Filter Env Release"), STR16("ms"), 0, 0.310,
        ParameterInfo::kCanAutomate, kFilterEnvReleaseId);
}

inline Steinberg::tresult formatFilterEnvParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kFilterEnvAttackId: case kFilterEnvDecayId: case kFilterEnvReleaseId: {
            float ms = envTimeFromNormalized(value);
            char8 text[32];
            if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterEnvSustainId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveFilterEnvParams(const FilterEnvParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sustain.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
}

inline bool loadFilterEnvParams(FilterEnvParams& params, Steinberg::IBStreamer& streamer) {
    float v = 0.0f;
    if (!streamer.readFloat(v)) return false; params.attackMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.decayMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.sustain.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.releaseMs.store(v, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadFilterEnvParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float v = 0.0f;
    if (streamer.readFloat(v)) setParam(kFilterEnvAttackId, envTimeToNormalized(v));
    if (streamer.readFloat(v)) setParam(kFilterEnvDecayId, envTimeToNormalized(v));
    if (streamer.readFloat(v)) setParam(kFilterEnvSustainId, static_cast<double>(v));
    if (streamer.readFloat(v)) setParam(kFilterEnvReleaseId, envTimeToNormalized(v));
}

} // namespace Ruinae
