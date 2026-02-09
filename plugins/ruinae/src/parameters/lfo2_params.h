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

struct LFO2Params {
    std::atomic<float> rateHz{1.0f};
    std::atomic<int> shape{0};
    std::atomic<float> depth{1.0f};
    std::atomic<bool> sync{false};
};

inline void handleLFO2ParamChange(
    LFO2Params& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kLFO2RateId:
            params.rateHz.store(lfoRateFromNormalized(value), std::memory_order_relaxed); break;
        case kLFO2ShapeId:
            params.shape.store(std::clamp(static_cast<int>(value * (kWaveformCount - 1) + 0.5), 0, kWaveformCount - 1), std::memory_order_relaxed); break;
        case kLFO2DepthId:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kLFO2SyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerLFO2Params(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("LFO 2 Rate"), STR16("Hz"), 0, 0.540,
        ParameterInfo::kCanAutomate, kLFO2RateId);
    parameters.addParameter(createDropdownParameter(
        STR16("LFO 2 Shape"), kLFO2ShapeId,
        {STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"),
         STR16("Square"), STR16("Sample & Hold"), STR16("Smooth Random")}
    ));
    parameters.addParameter(STR16("LFO 2 Depth"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kLFO2DepthId);
    parameters.addParameter(STR16("LFO 2 Sync"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kLFO2SyncId);
}

inline Steinberg::tresult formatLFO2Param(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kLFO2RateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", lfoRateFromNormalized(value));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO2DepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveLFO2Params(const LFO2Params& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.shape.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadLFO2Params(LFO2Params& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) return false; params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false; params.shape.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false; params.sync.store(iv != 0, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadLFO2ParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kLFO2RateId, lfoRateToNormalized(fv));
    if (streamer.readInt32(iv)) setParam(kLFO2ShapeId, static_cast<double>(iv) / (kWaveformCount - 1));
    if (streamer.readFloat(fv)) setParam(kLFO2DepthId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kLFO2SyncId, iv != 0 ? 1.0 : 0.0);
}

} // namespace Ruinae
