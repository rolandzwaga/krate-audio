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

struct RuinaeReverbParams {
    std::atomic<float> size{0.5f};        // 0-1
    std::atomic<float> damping{0.5f};     // 0-1
    std::atomic<float> width{1.0f};       // 0-1
    std::atomic<float> mix{0.3f};         // 0-1
    std::atomic<float> preDelayMs{0.0f};  // 0-100 ms
    std::atomic<float> diffusion{0.7f};   // 0-1
    std::atomic<bool> freeze{false};
    std::atomic<float> modRateHz{0.5f};   // 0-2 Hz
    std::atomic<float> modDepth{0.0f};    // 0-1
};

inline void handleReverbParamChange(
    RuinaeReverbParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kReverbSizeId:
            params.size.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbDampingId:
            params.damping.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbWidthId:
            params.width.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbMixId:
            params.mix.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbPreDelayId:
            // 0-1 -> 0-100 ms
            params.preDelayMs.store(
                std::clamp(static_cast<float>(value * 100.0), 0.0f, 100.0f),
                std::memory_order_relaxed); break;
        case kReverbDiffusionId:
            params.diffusion.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbFreezeId:
            params.freeze.store(value >= 0.5, std::memory_order_relaxed); break;
        case kReverbModRateId:
            // 0-1 -> 0-2 Hz
            params.modRateHz.store(
                std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
                std::memory_order_relaxed); break;
        case kReverbModDepthId:
            params.modDepth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerReverbParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Reverb Size"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kReverbSizeId);
    parameters.addParameter(STR16("Reverb Damping"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kReverbDampingId);
    parameters.addParameter(STR16("Reverb Width"), STR16(""), 0, 1.0,
        ParameterInfo::kCanAutomate, kReverbWidthId);
    parameters.addParameter(STR16("Reverb Mix"), STR16("%"), 0, 0.3,
        ParameterInfo::kCanAutomate, kReverbMixId);
    parameters.addParameter(STR16("Reverb Pre-Delay"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kReverbPreDelayId);
    parameters.addParameter(STR16("Reverb Diffusion"), STR16(""), 0, 0.7,
        ParameterInfo::kCanAutomate, kReverbDiffusionId);
    parameters.addParameter(STR16("Reverb Freeze"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kReverbFreezeId);
    parameters.addParameter(STR16("Reverb Mod Rate"), STR16("Hz"), 0, 0.25,
        ParameterInfo::kCanAutomate, kReverbModRateId);
    parameters.addParameter(STR16("Reverb Mod Depth"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kReverbModDepthId);
}

inline Steinberg::tresult formatReverbParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kReverbSizeId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbDampingId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbWidthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbMixId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbPreDelayId: {
            float ms = static_cast<float>(value * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbDiffusionId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbModRateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", value * 2.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbModDepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveReverbParams(const RuinaeReverbParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.size.load(std::memory_order_relaxed));
    streamer.writeFloat(params.damping.load(std::memory_order_relaxed));
    streamer.writeFloat(params.width.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.preDelayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.diffusion.load(std::memory_order_relaxed));
    streamer.writeInt32(params.freeze.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.modRateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modDepth.load(std::memory_order_relaxed));
}

inline bool loadReverbParams(RuinaeReverbParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) return false; params.size.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.damping.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.width.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.mix.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.preDelayMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.diffusion.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false; params.freeze.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.modRateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false; params.modDepth.store(fv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadReverbParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kReverbSizeId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kReverbDampingId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kReverbWidthId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kReverbMixId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kReverbPreDelayId, static_cast<double>(fv / 100.0f));
    if (streamer.readFloat(fv)) setParam(kReverbDiffusionId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kReverbFreezeId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) setParam(kReverbModRateId, static_cast<double>(fv / 2.0f));
    if (streamer.readFloat(fv)) setParam(kReverbModDepthId, static_cast<double>(fv));
}

} // namespace Ruinae
