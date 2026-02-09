#pragma once
#include "plugin_ids.h"
#include "parameters/amp_env_params.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct ModEnvParams {
    std::atomic<float> attackMs{10.0f};
    std::atomic<float> decayMs{300.0f};
    std::atomic<float> sustain{0.5f};
    std::atomic<float> releaseMs{500.0f};
};

inline void handleModEnvParamChange(
    ModEnvParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kModEnvAttackId:
            params.attackMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        case kModEnvDecayId:
            params.decayMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        case kModEnvSustainId:
            params.sustain.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kModEnvReleaseId:
            params.releaseMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerModEnvParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Mod Env Attack"), STR16("ms"), 0, 0.1,
        ParameterInfo::kCanAutomate, kModEnvAttackId);
    parameters.addParameter(STR16("Mod Env Decay"), STR16("ms"), 0, 0.310,
        ParameterInfo::kCanAutomate, kModEnvDecayId);
    parameters.addParameter(STR16("Mod Env Sustain"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kModEnvSustainId);
    parameters.addParameter(STR16("Mod Env Release"), STR16("ms"), 0, 0.368,
        ParameterInfo::kCanAutomate, kModEnvReleaseId);
}

inline Steinberg::tresult formatModEnvParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kModEnvAttackId: case kModEnvDecayId: case kModEnvReleaseId: {
            float ms = envTimeFromNormalized(value);
            char8 text[32];
            if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kModEnvSustainId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveModEnvParams(const ModEnvParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sustain.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
}

inline bool loadModEnvParams(ModEnvParams& params, Steinberg::IBStreamer& streamer) {
    float v = 0.0f;
    if (!streamer.readFloat(v)) return false; params.attackMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.decayMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.sustain.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.releaseMs.store(v, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadModEnvParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float v = 0.0f;
    if (streamer.readFloat(v)) setParam(kModEnvAttackId, envTimeToNormalized(v));
    if (streamer.readFloat(v)) setParam(kModEnvDecayId, envTimeToNormalized(v));
    if (streamer.readFloat(v)) setParam(kModEnvSustainId, static_cast<double>(v));
    if (streamer.readFloat(v)) setParam(kModEnvReleaseId, envTimeToNormalized(v));
}

} // namespace Ruinae
