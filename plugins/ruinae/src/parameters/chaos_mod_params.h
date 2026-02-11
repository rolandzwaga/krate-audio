#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/lfo1_params.h"  // for lfoRateFromNormalized/lfoRateToNormalized
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct ChaosModParams {
    std::atomic<float> rateHz{1.0f};   // 0.01-50 Hz (same mapping as LFO)
    std::atomic<int> type{0};          // 0=Lorenz, 1=Rossler
    std::atomic<float> depth{0.0f};    // 0-1
};

inline void handleChaosModParamChange(
    ChaosModParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kChaosModRateId:
            params.rateHz.store(lfoRateFromNormalized(value), std::memory_order_relaxed); break;
        case kChaosModTypeId:
            params.type.store(std::clamp(static_cast<int>(value * (kChaosTypeCount - 1) + 0.5), 0, kChaosTypeCount - 1), std::memory_order_relaxed); break;
        case kChaosModDepthId:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerChaosModParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Chaos Rate"), STR16("Hz"), 0, 0.540,
        ParameterInfo::kCanAutomate, kChaosModRateId);
    parameters.addParameter(createDropdownParameter(
        STR16("Chaos Type"), kChaosModTypeId,
        {STR16("Lorenz"), STR16("Rossler")}
    ));
    parameters.addParameter(STR16("Chaos Depth"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kChaosModDepthId);
}

inline Steinberg::tresult formatChaosModParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kChaosModRateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", lfoRateFromNormalized(value));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kChaosModDepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveChaosModParams(const ChaosModParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
}

inline bool loadChaosModParams(ChaosModParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.type.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadChaosModParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kChaosModRateId, lfoRateToNormalized(fv));
    if (streamer.readInt32(iv)) setParam(kChaosModTypeId, static_cast<double>(iv) / (kChaosTypeCount - 1));
    if (streamer.readFloat(fv)) setParam(kChaosModDepthId, static_cast<double>(fv));
}

} // namespace Ruinae
