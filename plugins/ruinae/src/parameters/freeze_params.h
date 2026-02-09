#pragma once
#include "plugin_ids.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <atomic>

namespace Ruinae {

struct RuinaeFreezeParams {
    std::atomic<bool> enabled{false};
    std::atomic<bool> freeze{false};
};

inline void handleFreezeParamChange(
    RuinaeFreezeParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kFreezeEnabledId:
            params.enabled.store(value >= 0.5, std::memory_order_relaxed); break;
        case kFreezeToggleId:
            params.freeze.store(value >= 0.5, std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerFreezeParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Freeze Enabled"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kFreezeEnabledId);
    parameters.addParameter(STR16("Freeze"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kFreezeToggleId);
}

inline Steinberg::tresult formatFreezeParam(
    [[maybe_unused]] Steinberg::Vst::ParamID id,
    [[maybe_unused]] Steinberg::Vst::ParamValue value,
    [[maybe_unused]] Steinberg::Vst::String128 string) {
    return Steinberg::kResultFalse; // Toggle params use default
}

inline void saveFreezeParams(const RuinaeFreezeParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.freeze.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadFreezeParams(RuinaeFreezeParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0;
    if (!streamer.readInt32(iv)) return false; params.enabled.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false; params.freeze.store(iv != 0, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadFreezeParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0;
    if (streamer.readInt32(iv)) setParam(kFreezeEnabledId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kFreezeToggleId, iv != 0 ? 1.0 : 0.0);
}

} // namespace Ruinae
