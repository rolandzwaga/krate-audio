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
#include <cstdio>

namespace Ruinae {

struct RuinaeDistortionParams {
    std::atomic<int> type{0};          // RuinaeDistortionType (0-5)
    std::atomic<float> drive{0.0f};    // 0-1
    std::atomic<float> character{0.5f}; // 0-1
    std::atomic<float> mix{1.0f};      // 0-1
};

inline void handleDistortionParamChange(
    RuinaeDistortionParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kDistortionTypeId:
            params.type.store(std::clamp(static_cast<int>(value * (kDistortionTypeCount - 1) + 0.5), 0, kDistortionTypeCount - 1), std::memory_order_relaxed);
            break;
        case kDistortionDriveId:
            params.drive.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionCharacterId:
            params.character.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionMixId:
            params.mix.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        default: break;
    }
}

inline void registerDistortionParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(createDropdownParameter(
        STR16("Distortion Type"), kDistortionTypeId,
        {STR16("Clean"), STR16("Chaos Waveshaper"), STR16("Spectral"),
         STR16("Granular"), STR16("Wavefolder"), STR16("Tape Saturator")}
    ));
    parameters.addParameter(STR16("Distortion Drive"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDistortionDriveId);
    parameters.addParameter(STR16("Distortion Character"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDistortionCharacterId);
    parameters.addParameter(STR16("Distortion Mix"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kDistortionMixId);
}

inline Steinberg::tresult formatDistortionParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kDistortionDriveId: case kDistortionMixId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionCharacterId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveDistortionParams(const RuinaeDistortionParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.drive.load(std::memory_order_relaxed));
    streamer.writeFloat(params.character.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
}

inline bool loadDistortionParams(RuinaeDistortionParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (!streamer.readInt32(intVal)) return false;
    params.type.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.drive.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.character.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.mix.store(floatVal, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadDistortionParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (streamer.readInt32(intVal))
        setParam(kDistortionTypeId, static_cast<double>(intVal) / (kDistortionTypeCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionDriveId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionCharacterId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionMixId, static_cast<double>(floatVal));
}

} // namespace Ruinae
