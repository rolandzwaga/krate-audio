#pragma once
#include "plugin_ids.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Ruinae {

struct AmpEnvParams {
    std::atomic<float> attackMs{10.0f};    // 0-10000 ms
    std::atomic<float> decayMs{100.0f};    // 0-10000 ms
    std::atomic<float> sustain{0.8f};      // 0-1
    std::atomic<float> releaseMs{200.0f};  // 0-10000 ms
};

// Exponential time mapping: normalized 0-1 -> 0-10000 ms
// Using x^3 * 10000 for perceptually linear feel
inline float envTimeFromNormalized(double value) {
    float v = static_cast<float>(value);
    return std::clamp(v * v * v * 10000.0f, 0.0f, 10000.0f);
}

inline double envTimeToNormalized(float ms) {
    return std::clamp(static_cast<double>(std::cbrt(ms / 10000.0f)), 0.0, 1.0);
}

inline void handleAmpEnvParamChange(
    AmpEnvParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kAmpEnvAttackId:
            params.attackMs.store(envTimeFromNormalized(value), std::memory_order_relaxed);
            break;
        case kAmpEnvDecayId:
            params.decayMs.store(envTimeFromNormalized(value), std::memory_order_relaxed);
            break;
        case kAmpEnvSustainId:
            params.sustain.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kAmpEnvReleaseId:
            params.releaseMs.store(envTimeFromNormalized(value), std::memory_order_relaxed);
            break;
        default: break;
    }
}

inline void registerAmpEnvParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Default attack: 10ms -> cbrt(10/10000) ~ 0.1
    parameters.addParameter(STR16("Amp Attack"), STR16("ms"), 0, 0.1,
        ParameterInfo::kCanAutomate, kAmpEnvAttackId);
    // Default decay: 100ms -> cbrt(100/10000) ~ 0.215
    parameters.addParameter(STR16("Amp Decay"), STR16("ms"), 0, 0.215,
        ParameterInfo::kCanAutomate, kAmpEnvDecayId);
    parameters.addParameter(STR16("Amp Sustain"), STR16("%"), 0, 0.8,
        ParameterInfo::kCanAutomate, kAmpEnvSustainId);
    // Default release: 200ms -> cbrt(200/10000) ~ 0.271
    parameters.addParameter(STR16("Amp Release"), STR16("ms"), 0, 0.271,
        ParameterInfo::kCanAutomate, kAmpEnvReleaseId);
}

inline Steinberg::tresult formatAmpEnvParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kAmpEnvAttackId: case kAmpEnvDecayId: case kAmpEnvReleaseId: {
            float ms = envTimeFromNormalized(value);
            char8 text[32];
            if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kAmpEnvSustainId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveAmpEnvParams(const AmpEnvParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sustain.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
}

inline bool loadAmpEnvParams(AmpEnvParams& params, Steinberg::IBStreamer& streamer) {
    float v = 0.0f;
    if (!streamer.readFloat(v)) return false; params.attackMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.decayMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.sustain.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.releaseMs.store(v, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadAmpEnvParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float v = 0.0f;
    if (streamer.readFloat(v)) setParam(kAmpEnvAttackId, envTimeToNormalized(v));
    if (streamer.readFloat(v)) setParam(kAmpEnvDecayId, envTimeToNormalized(v));
    if (streamer.readFloat(v)) setParam(kAmpEnvSustainId, static_cast<double>(v));
    if (streamer.readFloat(v)) setParam(kAmpEnvReleaseId, envTimeToNormalized(v));
}

} // namespace Ruinae
