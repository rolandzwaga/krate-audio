#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Ruinae {

struct LFO1Params {
    std::atomic<float> rateHz{1.0f};   // 0.01-50 Hz
    std::atomic<int> shape{0};         // Waveform enum (0-5)
    std::atomic<float> depth{1.0f};    // 0-1
    std::atomic<bool> sync{true};      // on/off (default: sync to host)
};

// Exponential rate: 0-1 -> 0.01-50 Hz
inline float lfoRateFromNormalized(double value) {
    float hz = 0.01f * std::pow(5000.0f, static_cast<float>(value));
    return std::clamp(hz, 0.01f, 50.0f);
}

inline double lfoRateToNormalized(float hz) {
    return std::clamp(static_cast<double>(std::log(hz / 0.01f) / std::log(5000.0f)), 0.0, 1.0);
}

inline void handleLFO1ParamChange(
    LFO1Params& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kLFO1RateId:
            params.rateHz.store(lfoRateFromNormalized(value), std::memory_order_relaxed); break;
        case kLFO1ShapeId:
            params.shape.store(std::clamp(static_cast<int>(value * (kWaveformCount - 1) + 0.5), 0, kWaveformCount - 1), std::memory_order_relaxed); break;
        case kLFO1DepthId:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kLFO1SyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerLFO1Params(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Default 1 Hz -> log(1/0.01)/log(5000) ~ 0.540
    parameters.addParameter(STR16("LFO 1 Rate"), STR16("Hz"), 0, 0.540,
        ParameterInfo::kCanAutomate, kLFO1RateId);
    parameters.addParameter(createDropdownParameter(
        STR16("LFO 1 Shape"), kLFO1ShapeId,
        {STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"),
         STR16("Square"), STR16("Sample & Hold"), STR16("Smooth Random")}
    ));
    parameters.addParameter(STR16("LFO 1 Depth"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kLFO1DepthId);
    parameters.addParameter(STR16("LFO 1 Sync"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kLFO1SyncId);
}

inline Steinberg::tresult formatLFO1Param(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kLFO1RateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", lfoRateFromNormalized(value));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO1DepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveLFO1Params(const LFO1Params& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.shape.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadLFO1Params(LFO1Params& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.shape.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadLFO1ParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kLFO1RateId, lfoRateToNormalized(fv));
    if (streamer.readInt32(iv)) setParam(kLFO1ShapeId, static_cast<double>(iv) / (kWaveformCount - 1));
    if (streamer.readFloat(fv)) setParam(kLFO1DepthId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kLFO1SyncId, iv != 0 ? 1.0 : 0.0);
}

} // namespace Ruinae
