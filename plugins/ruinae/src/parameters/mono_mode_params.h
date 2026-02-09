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

struct MonoModeParams {
    std::atomic<int> priority{0};          // MonoMode index (0-2: Last/High/Low)
    std::atomic<bool> legato{false};
    std::atomic<float> portamentoTimeMs{0.0f}; // 0-5000 ms
    std::atomic<int> portaMode{0};         // PortaMode index (0-1: Always/Legato)
};

inline void handleMonoModeParamChange(
    MonoModeParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kMonoPriorityId:
            params.priority.store(
                std::clamp(static_cast<int>(value * (kMonoModeCount - 1) + 0.5), 0, kMonoModeCount - 1),
                std::memory_order_relaxed); break;
        case kMonoLegatoId:
            params.legato.store(value >= 0.5, std::memory_order_relaxed); break;
        case kMonoPortamentoTimeId:
            // 0-1 -> 0-5000 ms (cubic mapping for fine control at low values)
            params.portamentoTimeMs.store(
                std::clamp(static_cast<float>(value * value * value * 5000.0), 0.0f, 5000.0f),
                std::memory_order_relaxed); break;
        case kMonoPortaModeId:
            params.portaMode.store(
                std::clamp(static_cast<int>(value * (kPortaModeCount - 1) + 0.5), 0, kPortaModeCount - 1),
                std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerMonoModeParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(createDropdownParameter(
        STR16("Mono Priority"), kMonoPriorityId,
        {STR16("Last"), STR16("High"), STR16("Low")}
    ));
    parameters.addParameter(STR16("Legato"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kMonoLegatoId);
    parameters.addParameter(STR16("Portamento Time"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kMonoPortamentoTimeId);
    parameters.addParameter(createDropdownParameter(
        STR16("Portamento Mode"), kMonoPortaModeId,
        {STR16("Always"), STR16("Legato")}
    ));
}

inline Steinberg::tresult formatMonoModeParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kMonoPortamentoTimeId: {
            float ms = static_cast<float>(value * value * value * 5000.0);
            char8 text[32];
            if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveMonoModeParams(const MonoModeParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.priority.load(std::memory_order_relaxed));
    streamer.writeInt32(params.legato.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.portamentoTimeMs.load(std::memory_order_relaxed));
    streamer.writeInt32(params.portaMode.load(std::memory_order_relaxed));
}

inline bool loadMonoModeParams(MonoModeParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (!streamer.readInt32(iv)) return false; params.priority.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false; params.legato.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.portamentoTimeMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false; params.portaMode.store(iv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadMonoModeParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (streamer.readInt32(iv)) setParam(kMonoPriorityId, static_cast<double>(iv) / (kMonoModeCount - 1));
    if (streamer.readInt32(iv)) setParam(kMonoLegatoId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) {
        // Inverse of cubic: normalized = cbrt(ms / 5000)
        double norm = (fv > 0.0f) ? std::cbrt(static_cast<double>(fv) / 5000.0) : 0.0;
        setParam(kMonoPortamentoTimeId, std::clamp(norm, 0.0, 1.0));
    }
    if (streamer.readInt32(iv)) setParam(kMonoPortaModeId, static_cast<double>(iv) / (kPortaModeCount - 1));
}

} // namespace Ruinae
