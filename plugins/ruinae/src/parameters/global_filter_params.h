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

struct GlobalFilterParams {
    std::atomic<bool> enabled{false};
    std::atomic<int> type{0};            // SVFMode index (0-3: LP,HP,BP,Notch)
    std::atomic<float> cutoffHz{1000.0f}; // 20-20000 Hz
    std::atomic<float> resonance{0.707f}; // 0.1-30.0
};

inline void handleGlobalFilterParamChange(
    GlobalFilterParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kGlobalFilterEnabledId:
            params.enabled.store(value >= 0.5, std::memory_order_relaxed); break;
        case kGlobalFilterTypeId:
            params.type.store(
                std::clamp(static_cast<int>(value * (kGlobalFilterTypeCount - 1) + 0.5), 0, kGlobalFilterTypeCount - 1),
                std::memory_order_relaxed); break;
        case kGlobalFilterCutoffId: {
            float hz = 20.0f * std::pow(1000.0f, static_cast<float>(value));
            params.cutoffHz.store(std::clamp(hz, 20.0f, 20000.0f), std::memory_order_relaxed);
            break;
        }
        case kGlobalFilterResonanceId:
            params.resonance.store(
                std::clamp(static_cast<float>(0.1 + value * 29.9), 0.1f, 30.0f),
                std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerGlobalFilterParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Global Filter"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kGlobalFilterEnabledId);
    parameters.addParameter(createDropdownParameter(
        STR16("Global Filter Type"), kGlobalFilterTypeId,
        {STR16("Lowpass"), STR16("Highpass"), STR16("Bandpass"), STR16("Notch")}
    ));
    parameters.addParameter(STR16("Global Filter Cutoff"), STR16("Hz"), 0, 0.574,
        ParameterInfo::kCanAutomate, kGlobalFilterCutoffId);
    parameters.addParameter(STR16("Global Filter Reso"), STR16(""), 0, 0.020,
        ParameterInfo::kCanAutomate, kGlobalFilterResonanceId);
}

inline Steinberg::tresult formatGlobalFilterParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kGlobalFilterCutoffId: {
            float hz = 20.0f * std::pow(1000.0f, static_cast<float>(value));
            char8 text[32];
            if (hz >= 1000.0f) snprintf(text, sizeof(text), "%.1f kHz", hz / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f Hz", hz);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kGlobalFilterResonanceId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f", 0.1 + value * 29.9);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveGlobalFilterParams(const GlobalFilterParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.cutoffHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.resonance.load(std::memory_order_relaxed));
}

inline bool loadGlobalFilterParams(GlobalFilterParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (!streamer.readInt32(iv)) return false; params.enabled.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false; params.type.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.cutoffHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.resonance.store(fv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadGlobalFilterParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (streamer.readInt32(iv)) setParam(kGlobalFilterEnabledId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kGlobalFilterTypeId, static_cast<double>(iv) / (kGlobalFilterTypeCount - 1));
    if (streamer.readFloat(fv)) {
        double norm = (fv > 20.0f) ? std::log(fv / 20.0f) / std::log(1000.0f) : 0.0;
        setParam(kGlobalFilterCutoffId, std::clamp(norm, 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) setParam(kGlobalFilterResonanceId, static_cast<double>((fv - 0.1f) / 29.9f));
}

} // namespace Ruinae
